/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Timers a caller can name, and timers a driver keeps to itself
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

/* GLOBALS ********************************************************************/

POBJECT_TYPE ExTimerObjectType = NULL;

GENERIC_MAPPING ExpTimerMapping =
{
    STANDARD_RIGHTS_READ | TIMER_QUERY_STATE,
    STANDARD_RIGHTS_WRITE | TIMER_MODIFY_STATE,
    STANDARD_RIGHTS_EXECUTE | SYNCHRONIZE,
    TIMER_ALL_ACCESS
};

LIST_ENTRY ExpWakeList;
KSPIN_LOCK ExpWakeListLock;

/* PRIVATE DATA ***************************************************************/

#define EXP_TIMER_TAG 'emiT'

/* FIXME: only three bits of the setting count fit beside the timer pointer, so an expiry eight settings old passes for the current one */
#define EXP_TIMER_ARMING_MASK 0x7

/*
 * A timer a driver allocates for itself. It is not an object anybody can name,
 * so the kernel keeps whatever it needs of its own here.
 */
typedef struct _EX_TIMER
{
    KTIMER KeTimer;
    KDPC Dpc;
    PEXT_CALLBACK Callback;
    PVOID CallbackContext;
    PEXT_DELETE_CALLBACK DeleteCallback;
    PVOID DeleteContext;
    ULONG Attributes;
    volatile LONG RefCount;
    BOOLEAN DpcQueued;
} EX_TIMER;

/* PRIVATE FUNCTIONS **********************************************************/

/*
 * A high-resolution timer keeps the clock at its finest step while it is set,
 * through the counted request drivers share. Called with the timer's lock held.
 */
static
VOID
ExpArmHighResolutionTimer(
    _Inout_ PETIMER Timer,
    _In_ BOOLEAN Arm)
{
    if (!Timer->HighResolution || (Timer->HighResolutionArmed == Arm))
        return;

    Timer->HighResolutionArmed = Arm;
    /* FIXME: called under the timer lock and from the timer DPC, at DISPATCH_LEVEL, where the WDK allows ExSetTimerResolution only up to APC_LEVEL */
    ExSetTimerResolution(KeMinimumIncrement, Arm);
}

/* Called with the timer's lock held; the thread's own lock guards the list of timers it set */
static
VOID
ExpUnlinkThreadTimer(
    _Inout_ PETIMER Timer)
{
    PETHREAD Thread = CONTAINING_RECORD(Timer->TimerApc.Thread, ETHREAD, Tcb);

    KeAcquireSpinLockAtDpcLevel(&Thread->ActiveTimerListLock);

    if (Timer->ActiveTimerListEntry.Flink != NULL)
    {
        RemoveEntryList(&Timer->ActiveTimerListEntry);
        Timer->ActiveTimerListEntry.Flink = NULL;
    }

    KeReleaseSpinLockFromDpcLevel(&Thread->ActiveTimerListLock);
}

static
VOID
ExpQueueTimerApc(
    _Inout_ PETIMER Timer)
{
    ASSERT(Timer->ApcAssociated);

    ObReferenceObject(Timer);

    if (KeInsertQueueApc(&Timer->TimerApc,
                         Timer->TimerApc.SystemArgument1,
                         Timer->TimerApc.SystemArgument2,
                         IO_NO_INCREMENT))
    {
        Timer->ApcQueuedSequence = Timer->SetSequence;
        return;
    }

    ObDereferenceObject(Timer);
}

static
PVOID
ExpTimerDpcContext(
    _In_ PETIMER Timer)
{
    ASSERT(((ULONG_PTR)Timer & EXP_TIMER_ARMING_MASK) == 0);

    return (PVOID)((ULONG_PTR)Timer | (Timer->SetSequence & EXP_TIMER_ARMING_MASK));
}

/*
 * Runs when a named timer goes off. The timer only tells the thread that set it,
 * through the routine the thread asked for, and only while that request stands.
 */
VOID
NTAPI
ExpTimerDpcRoutine(
    _In_ PKDPC Dpc,
    _In_opt_ PVOID DeferredContext,
    _In_opt_ PVOID SystemArgument1,
    _In_opt_ PVOID SystemArgument2)
{
    PETIMER Timer = (PETIMER)((ULONG_PTR)DeferredContext & ~(ULONG_PTR)EXP_TIMER_ARMING_MASK);
    ULONG Arming = (ULONG)((ULONG_PTR)DeferredContext & EXP_TIMER_ARMING_MASK);

    UNREFERENCED_PARAMETER(Dpc);
    UNREFERENCED_PARAMETER(SystemArgument1);
    UNREFERENCED_PARAMETER(SystemArgument2);

    KeAcquireSpinLockAtDpcLevel(&Timer->Lock);

    /* An expiry captured before the timer was set again belongs to the earlier arming */
    if (Arming != (Timer->SetSequence & EXP_TIMER_ARMING_MASK))
    {
        KeReleaseSpinLockFromDpcLevel(&Timer->Lock);
        return;
    }

    /* A one-shot timer that went off and was not set again needs the fine clock no more */
    if ((Timer->Period == 0) && !Timer->KeTimer.Header.Inserted)
        ExpArmHighResolutionTimer(Timer, FALSE);

    if (!Timer->ApcAssociated)
    {
        KeReleaseSpinLockFromDpcLevel(&Timer->Lock);
        return;
    }

    /*
     * The routine runs as an APC in the thread that asked for it. A one-shot
     * timer is done with once the APC has been queued.
     */
    ExpQueueTimerApc(Timer);

    KeReleaseSpinLockFromDpcLevel(&Timer->Lock);
}

/*
 * Runs in the thread that set the timer, before the thread's own routine. A
 * timer that has been cancelled or set again in the meantime says nothing.
 */
VOID
NTAPI
ExpTimerApcKernelRoutine(
    _In_ PKAPC Apc,
    _Inout_ PKNORMAL_ROUTINE *NormalRoutine,
    _Inout_ PVOID *NormalContext,
    _Inout_ PVOID *SystemArgument1,
    _Inout_ PVOID *SystemArgument2)
{
    PETIMER Timer = CONTAINING_RECORD(Apc, ETIMER, TimerApc);
    KIRQL OldIrql;

    UNREFERENCED_PARAMETER(NormalContext);
    UNREFERENCED_PARAMETER(SystemArgument1);
    UNREFERENCED_PARAMETER(SystemArgument2);

    KeAcquireSpinLock(&Timer->Lock, &OldIrql);

    if (!Timer->ApcAssociated ||
        (Timer->ApcQueuedSequence != Timer->SetSequence))
    {
        *NormalRoutine = NULL;
        KeReleaseSpinLock(&Timer->Lock, OldIrql);
        ObDereferenceObject(Timer);
        return;
    }

    /* A timer that does not repeat is finished with its thread now. */
    if (Timer->Period == 0)
    {
        Timer->ApcAssociated = FALSE;

        ExpUnlinkThreadTimer(Timer);

        KeReleaseSpinLock(&Timer->Lock, OldIrql);

        ObDereferenceObjectEx(Timer, 2);
        return;
    }

    KeReleaseSpinLock(&Timer->Lock, OldIrql);
    ObDereferenceObject(Timer);
}

static
VOID
NTAPI
ExpTimerApcRundown(
    _In_ PKAPC Apc)
{
    ObDereferenceObject(CONTAINING_RECORD(Apc, ETIMER, TimerApc));
}

VOID
NTAPI
ExpQueueTimerApcAfterWait(
    _Inout_ PETIMER Timer)
{
    KIRQL OldIrql;

    KeAcquireSpinLock(&Timer->Lock, &OldIrql);

    if (Timer->ApcAssociated)
    {
        ExpQueueTimerApc(Timer);
    }

    KeReleaseSpinLock(&Timer->Lock, OldIrql);
}

static
BOOLEAN
ExpTimerMayHaveRunDpc(
    _In_ PETIMER Timer)
{
    return Timer->HighResolution || (Timer->TimerApc.KernelRoutine != NULL);
}

/*
 * Takes a timer off whatever it is still listed on when the last handle to it
 * goes away, so nothing is left pointing at it.
 */
VOID
NTAPI
ExpDeleteTimer(
    _In_ PVOID ObjectBody)
{
    PETIMER Timer = ObjectBody;
    KIRQL OldIrql;

    PAGED_CODE();

    ASSERT(!Timer->ApcAssociated);

    KeCancelTimer(&Timer->KeTimer);

    if (ExpTimerMayHaveRunDpc(Timer))
    {
        KeFlushQueuedDpcs();
    }

    KeAcquireSpinLock(&Timer->Lock, &OldIrql);
    ExpArmHighResolutionTimer(Timer, FALSE);
    KeReleaseSpinLock(&Timer->Lock, OldIrql);

    KeAcquireSpinLock(&ExpWakeListLock, &OldIrql);

    if (Timer->WakeTimerListEntry.Flink != NULL)
    {
        RemoveEntryList(&Timer->WakeTimerListEntry);
        Timer->WakeTimerListEntry.Flink = NULL;
    }

    KeReleaseSpinLock(&ExpWakeListLock, OldIrql);
}

static
LONG
ExpCancelTimerApc(
    _Inout_ PETIMER Timer)
{
    LONG References = 1;

    if (!Timer->ApcAssociated)
    {
        return 0;
    }

    Timer->ApcAssociated = FALSE;

    ExpUnlinkThreadTimer(Timer);

    if (KeRemoveQueueApc(&Timer->TimerApc)) References++;

    return References;
}

static
VOID
ExpSetWakeTimer(
    _Inout_ PETIMER Timer,
    _In_ BOOLEAN Wake)
{
    KIRQL OldIrql;

    KeAcquireSpinLock(&ExpWakeListLock, &OldIrql);

    if (Wake && (Timer->WakeTimerListEntry.Flink == NULL))
    {
        InsertTailList(&ExpWakeList, &Timer->WakeTimerListEntry);
    }
    else if (!Wake && (Timer->WakeTimerListEntry.Flink != NULL))
    {
        RemoveEntryList(&Timer->WakeTimerListEntry);
        Timer->WakeTimerListEntry.Flink = NULL;
    }

    Timer->WakeTimer = Wake;

    KeReleaseSpinLock(&ExpWakeListLock, OldIrql);
}

/* PUBLIC FUNCTIONS ***********************************************************/

CODE_SEG("INIT")
BOOLEAN
NTAPI
ExpInitializeTimerImplementation(VOID)
{
    OBJECT_TYPE_INITIALIZER Initializer;
    UNICODE_STRING Name = RTL_CONSTANT_STRING(L"Timer");

    RtlZeroMemory(&Initializer, sizeof(Initializer));

    Initializer.Length = sizeof(Initializer);
    Initializer.InvalidAttributes = OBJ_OPENLINK;
    Initializer.ValidAccessMask = TIMER_ALL_ACCESS;
    Initializer.GenericMapping = ExpTimerMapping;
    Initializer.PoolType = NonPagedPoolNx;
    Initializer.DefaultNonPagedPoolCharge = sizeof(ETIMER);
    Initializer.DeleteProcedure = ExpDeleteTimer;

    InitializeListHead(&ExpWakeList);
    KeInitializeSpinLock(&ExpWakeListLock);

    return NT_SUCCESS(ObCreateObjectType(&Name,
                                         &Initializer,
                                         NULL,
                                         &ExTimerObjectType));
}

/*
 * Stops every timer a thread set before the thread goes away, because the
 * routines it asked for can no longer be run anywhere.
 */
VOID
NTAPI
ExTimerRundown(VOID)
{
    PETHREAD Thread = PsGetCurrentThread();
    PETIMER Timer;
    KIRQL OldIrql;
    LONG References;

    PAGED_CODE();

    for (;;)
    {
        KeAcquireSpinLock(&Thread->ActiveTimerListLock, &OldIrql);

        if (IsListEmpty(&Thread->ActiveTimerListHead))
        {
            KeReleaseSpinLock(&Thread->ActiveTimerListLock, OldIrql);
            return;
        }

        /* The listing holds a reference, so the timer is alive until the timer lock settles who drops it */
        Timer = CONTAINING_RECORD(Thread->ActiveTimerListHead.Flink, ETIMER, ActiveTimerListEntry);
        ObReferenceObject(Timer);

        KeReleaseSpinLock(&Thread->ActiveTimerListLock, OldIrql);

        KeAcquireSpinLock(&Timer->Lock, &OldIrql);

        References = 0;
        if (Timer->ApcAssociated && (Timer->TimerApc.Thread == &Thread->Tcb))
        {
            References = ExpCancelTimerApc(Timer);
            KeCancelTimer(&Timer->KeTimer);
            ExpArmHighResolutionTimer(Timer, FALSE);
        }

        KeReleaseSpinLock(&Timer->Lock, OldIrql);

        if (References != 0)
        {
            ExpSetWakeTimer(Timer, FALSE);
        }

        ObDereferenceObjectEx(Timer, References + 1);
    }
}

static
NTSTATUS
ExpCreateTimer(
    _Out_ PHANDLE TimerHandle,
    _In_ ACCESS_MASK DesiredAccess,
    _In_opt_ POBJECT_ATTRIBUTES ObjectAttributes,
    _In_ TIMER_TYPE TimerType,
    _In_ BOOLEAN HighResolution)
{
    KPROCESSOR_MODE PreviousMode = ExGetPreviousMode();
    PETIMER Timer;
    HANDLE Handle;
    NTSTATUS Status;

    PAGED_CODE();

    if ((TimerType != NotificationTimer) &&
        (TimerType != SynchronizationTimer))
    {
        return STATUS_INVALID_PARAMETER_4;
    }

    if (PreviousMode != KernelMode)
    {
        _SEH2_TRY
        {
            ProbeForWriteHandle(TimerHandle);
        }
        _SEH2_EXCEPT(ExSystemExceptionFilter())
        {
            _SEH2_YIELD(return _SEH2_GetExceptionCode());
        }
        _SEH2_END;
    }

    Status = ObCreateObject(PreviousMode,
                            ExTimerObjectType,
                            ObjectAttributes,
                            PreviousMode,
                            NULL,
                            sizeof(ETIMER),
                            0,
                            0,
                            (PVOID *)&Timer);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    RtlZeroMemory(Timer, sizeof(ETIMER));

    KeInitializeTimerEx(&Timer->KeTimer, TimerType);
    KeInitializeDpc(&Timer->TimerDpc, ExpTimerDpcRoutine, ExpTimerDpcContext(Timer));
    KeInitializeSpinLock(&Timer->Lock);
    Timer->HighResolution = HighResolution;

    Status = ObInsertObject(Timer, NULL, DesiredAccess, 0, NULL, &Handle);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    if (PreviousMode != KernelMode)
    {
        _SEH2_TRY
        {
            *TimerHandle = Handle;
        }
        _SEH2_EXCEPT(ExSystemExceptionFilter())
        {
            Status = _SEH2_GetExceptionCode();
        }
        _SEH2_END;

        return Status;
    }

    *TimerHandle = Handle;
    return Status;
}

NTSTATUS
NTAPI
NtCreateTimer(
    _Out_ PHANDLE TimerHandle,
    _In_ ACCESS_MASK DesiredAccess,
    _In_opt_ POBJECT_ATTRIBUTES ObjectAttributes,
    _In_ TIMER_TYPE TimerType)
{
    return ExpCreateTimer(TimerHandle, DesiredAccess, ObjectAttributes, TimerType, FALSE);
}

NTSTATUS
NTAPI
NtCreateTimer2(
    _Out_ PHANDLE TimerHandle,
    _In_opt_ PVOID Reserved1,
    _In_opt_ POBJECT_ATTRIBUTES ObjectAttributes,
    _In_ ULONG Attributes,
    _In_ ACCESS_MASK DesiredAccess)
{
    UNREFERENCED_PARAMETER(Reserved1);

    /*
     * The attributes are the ones a driver's own timer takes: the kind of timer
     * to make, and whether it keeps the clock at its finest step while set.
     */
    if (Attributes & ~(ULONG)(EX_TIMER_NOTIFICATION | EX_TIMER_HIGH_RESOLUTION))
    {
        return STATUS_INVALID_PARAMETER_4;
    }

    return ExpCreateTimer(TimerHandle,
                          DesiredAccess,
                          ObjectAttributes,
                          (Attributes & EX_TIMER_NOTIFICATION) ?
                          NotificationTimer : SynchronizationTimer,
                          (Attributes & EX_TIMER_HIGH_RESOLUTION) != 0);
}

NTSTATUS
NTAPI
NtOpenTimer(
    _Out_ PHANDLE TimerHandle,
    _In_ ACCESS_MASK DesiredAccess,
    _In_ POBJECT_ATTRIBUTES ObjectAttributes)
{
    KPROCESSOR_MODE PreviousMode = ExGetPreviousMode();
    HANDLE Handle;
    NTSTATUS Status;

    PAGED_CODE();

    if (PreviousMode != KernelMode)
    {
        _SEH2_TRY
        {
            ProbeForWriteHandle(TimerHandle);
        }
        _SEH2_EXCEPT(ExSystemExceptionFilter())
        {
            _SEH2_YIELD(return _SEH2_GetExceptionCode());
        }
        _SEH2_END;
    }

    Status = ObOpenObjectByName(ObjectAttributes,
                                ExTimerObjectType,
                                PreviousMode,
                                NULL,
                                DesiredAccess,
                                NULL,
                                &Handle);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    if (PreviousMode != KernelMode)
    {
        _SEH2_TRY
        {
            *TimerHandle = Handle;
        }
        _SEH2_EXCEPT(ExSystemExceptionFilter())
        {
            Status = _SEH2_GetExceptionCode();
        }
        _SEH2_END;

        return Status;
    }

    *TimerHandle = Handle;
    return Status;
}

NTSTATUS
NTAPI
NtSetTimer(
    _In_ HANDLE TimerHandle,
    _In_ PLARGE_INTEGER DueTime,
    _In_opt_ PTIMER_APC_ROUTINE TimerApcRoutine,
    _In_opt_ PVOID TimerContext,
    _In_ BOOLEAN WakeTimer,
    _In_opt_ LONG Period,
    _Out_opt_ PBOOLEAN PreviousState)
{
    KPROCESSOR_MODE PreviousMode = ExGetPreviousMode();
    PETHREAD Thread = PsGetCurrentThread();
    PETIMER Timer;
    LARGE_INTEGER SafeDueTime;
    BOOLEAN State;
    LONG References;
    KIRQL OldIrql;
    NTSTATUS Status;

    PAGED_CODE();

    if (Period < 0)
    {
        return STATUS_INVALID_PARAMETER_6;
    }

    if (PreviousMode != KernelMode)
    {
        _SEH2_TRY
        {
            ProbeForReadLargeInteger(DueTime);
            SafeDueTime = *DueTime;
            if (PreviousState != NULL) ProbeForWriteBoolean(PreviousState);
        }
        _SEH2_EXCEPT(ExSystemExceptionFilter())
        {
            _SEH2_YIELD(return _SEH2_GetExceptionCode());
        }
        _SEH2_END;
    }
    else
    {
        SafeDueTime = *DueTime;
    }

    Status = ObReferenceObjectByHandle(TimerHandle,
                                       TIMER_MODIFY_STATE,
                                       ExTimerObjectType,
                                       PreviousMode,
                                       (PVOID *)&Timer,
                                       NULL);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    KeAcquireSpinLock(&Timer->Lock, &OldIrql);

    /* Whatever the timer was told to do before is forgotten. */
    References = ExpCancelTimerApc(Timer);

    State = KeReadStateTimer(&Timer->KeTimer);
    KeCancelTimer(&Timer->KeTimer);
    KeRemoveQueueDpc(&Timer->TimerDpc);

    Timer->SetSequence++;
    KeInitializeDpc(&Timer->TimerDpc, ExpTimerDpcRoutine, ExpTimerDpcContext(Timer));
    Timer->Period = Period;
    ExpArmHighResolutionTimer(Timer, TRUE);

    if (TimerApcRoutine != NULL)
    {
        KeInitializeApc(&Timer->TimerApc,
                        &Thread->Tcb,
                        CurrentApcEnvironment,
                        ExpTimerApcKernelRoutine,
                        ExpTimerApcRundown,
                        (PKNORMAL_ROUTINE)TimerApcRoutine,
                        PreviousMode,
                        TimerContext);

        Timer->ApcAssociated = TRUE;

        ObReferenceObject(Timer);

        KeAcquireSpinLockAtDpcLevel(&Thread->ActiveTimerListLock);
        InsertTailList(&Thread->ActiveTimerListHead,
                       &Timer->ActiveTimerListEntry);
        KeReleaseSpinLockFromDpcLevel(&Thread->ActiveTimerListLock);
    }

    KeSetTimerEx(&Timer->KeTimer,
                 SafeDueTime,
                 Period,
                 ((TimerApcRoutine != NULL) || Timer->HighResolution) ? &Timer->TimerDpc : NULL);

    KeReleaseSpinLock(&Timer->Lock, OldIrql);

    ExpSetWakeTimer(Timer, WakeTimer);

    ObDereferenceObjectEx(Timer, References + 1);

    if (PreviousState == NULL)
    {
        return STATUS_SUCCESS;
    }

    if (PreviousMode != KernelMode)
    {
        _SEH2_TRY
        {
            *PreviousState = State;
        }
        _SEH2_EXCEPT(ExSystemExceptionFilter())
        {
            Status = _SEH2_GetExceptionCode();
        }
        _SEH2_END;

        return Status;
    }

    *PreviousState = State;
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
NtCancelTimer(
    _In_ HANDLE TimerHandle,
    _Out_opt_ PBOOLEAN CurrentState)
{
    KPROCESSOR_MODE PreviousMode = ExGetPreviousMode();
    PETIMER Timer;
    BOOLEAN State;
    LONG References;
    KIRQL OldIrql;
    NTSTATUS Status;

    PAGED_CODE();

    if ((CurrentState != NULL) && (PreviousMode != KernelMode))
    {
        _SEH2_TRY
        {
            ProbeForWriteBoolean(CurrentState);
        }
        _SEH2_EXCEPT(ExSystemExceptionFilter())
        {
            _SEH2_YIELD(return _SEH2_GetExceptionCode());
        }
        _SEH2_END;
    }

    Status = ObReferenceObjectByHandle(TimerHandle,
                                       TIMER_MODIFY_STATE,
                                       ExTimerObjectType,
                                       PreviousMode,
                                       (PVOID *)&Timer,
                                       NULL);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    KeAcquireSpinLock(&Timer->Lock, &OldIrql);

    References = ExpCancelTimerApc(Timer);

    KeCancelTimer(&Timer->KeTimer);
    State = KeReadStateTimer(&Timer->KeTimer);
    ExpArmHighResolutionTimer(Timer, FALSE);

    KeReleaseSpinLock(&Timer->Lock, OldIrql);

    ExpSetWakeTimer(Timer, FALSE);

    ObDereferenceObjectEx(Timer, References + 1);

    if (CurrentState == NULL)
    {
        return STATUS_SUCCESS;
    }

    if (PreviousMode != KernelMode)
    {
        _SEH2_TRY
        {
            *CurrentState = State;
        }
        _SEH2_EXCEPT(ExSystemExceptionFilter())
        {
            Status = _SEH2_GetExceptionCode();
        }
        _SEH2_END;

        return Status;
    }

    *CurrentState = State;
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
NtQueryTimer(
    _In_ HANDLE TimerHandle,
    _In_ TIMER_INFORMATION_CLASS TimerInformationClass,
    _Out_ PVOID TimerInformation,
    _In_ ULONG TimerInformationLength,
    _Out_opt_ PULONG ReturnLength)
{
    KPROCESSOR_MODE PreviousMode = ExGetPreviousMode();
    PETIMER Timer;
    TIMER_BASIC_INFORMATION Basic;
    NTSTATUS Status;

    PAGED_CODE();

    if (TimerInformationClass != TimerBasicInformation)
    {
        return STATUS_INVALID_INFO_CLASS;
    }

    if (TimerInformationLength < sizeof(TIMER_BASIC_INFORMATION))
    {
        return STATUS_INFO_LENGTH_MISMATCH;
    }

    if (PreviousMode != KernelMode)
    {
        _SEH2_TRY
        {
            ProbeForWrite(TimerInformation,
                          sizeof(TIMER_BASIC_INFORMATION),
                          sizeof(ULONG));
            if (ReturnLength != NULL) ProbeForWriteUlong(ReturnLength);
        }
        _SEH2_EXCEPT(ExSystemExceptionFilter())
        {
            _SEH2_YIELD(return _SEH2_GetExceptionCode());
        }
        _SEH2_END;
    }

    Status = ObReferenceObjectByHandle(TimerHandle,
                                       TIMER_QUERY_STATE,
                                       ExTimerObjectType,
                                       PreviousMode,
                                       (PVOID *)&Timer,
                                       NULL);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    Basic.TimeRemaining.QuadPart = 0;
    Basic.SignalState = KeReadStateTimer(&Timer->KeTimer);

    if (!Basic.SignalState)
    {
        LARGE_INTEGER Now;

        KeQuerySystemTime(&Now);
        Basic.TimeRemaining.QuadPart =
            Timer->KeTimer.DueTime.QuadPart - Now.QuadPart;
    }

    ObDereferenceObject(Timer);

    _SEH2_TRY
    {
        *(PTIMER_BASIC_INFORMATION)TimerInformation = Basic;
        if (ReturnLength != NULL)
        {
            *ReturnLength = sizeof(TIMER_BASIC_INFORMATION);
        }
    }
    _SEH2_EXCEPT(ExSystemExceptionFilter())
    {
        Status = _SEH2_GetExceptionCode();
    }
    _SEH2_END;

    return Status;
}

/* TIMERS A DRIVER KEEPS TO ITSELF ********************************************/

static
VOID
NTAPI
ExpExTimerDpcRoutine(
    _In_ PKDPC Dpc,
    _In_opt_ PVOID DeferredContext,
    _In_opt_ PVOID SystemArgument1,
    _In_opt_ PVOID SystemArgument2)
{
    PEX_TIMER Timer = DeferredContext;

    UNREFERENCED_PARAMETER(Dpc);
    UNREFERENCED_PARAMETER(SystemArgument1);
    UNREFERENCED_PARAMETER(SystemArgument2);

    if (Timer->Callback != NULL)
    {
        Timer->Callback((PEX_TIMER)Timer, Timer->CallbackContext);
    }
}

PEX_TIMER
NTAPI
ExAllocateTimer(
    _In_opt_ PEXT_CALLBACK Callback,
    _In_opt_ PVOID CallbackContext,
    _In_ ULONG Attributes)
{
    PEX_TIMER Timer;

    Timer = ExAllocatePoolZero(NonPagedPool,
                               sizeof(EX_TIMER),
                               EXP_TIMER_TAG);
    if (Timer == NULL)
    {
        return NULL;
    }

    Timer->Callback = Callback;
    Timer->CallbackContext = CallbackContext;
    Timer->Attributes = Attributes;
    Timer->RefCount = 1;

    KeInitializeTimerEx(&Timer->KeTimer,
                        (Attributes & EX_TIMER_NOTIFICATION) ?
                        NotificationTimer : SynchronizationTimer);
    KeInitializeDpc(&Timer->Dpc, ExpExTimerDpcRoutine, Timer);

    return Timer;
}

BOOLEAN
NTAPI
ExSetTimer(
    _In_ PEX_TIMER Timer,
    _In_ LONGLONG DueTime,
    _In_ LONGLONG Period,
    _In_opt_ PEXT_SET_PARAMETERS Parameters)
{
    LARGE_INTEGER Due;
    LONG Interval;

    UNREFERENCED_PARAMETER(Parameters);

    Due.QuadPart = DueTime;

    /*
     * The period is given in the same units as the due time, but the kernel's
     * own timers count their period in milliseconds.
     */
    Interval = (LONG)(Period / 10000);

    return KeSetTimerEx(&Timer->KeTimer,
                        Due,
                        Interval,
                        (Timer->Callback != NULL) ? &Timer->Dpc : NULL);
}

BOOLEAN
NTAPI
ExCancelTimer(
    _Inout_ PEX_TIMER Timer,
    _In_opt_ PEXT_CANCEL_PARAMETERS Parameters)
{
    UNREFERENCED_PARAMETER(Parameters);

    return KeCancelTimer(&Timer->KeTimer);
}

BOOLEAN
NTAPI
ExDeleteTimer(
    _In_ PEX_TIMER Timer,
    _In_ BOOLEAN Cancel,
    _In_ BOOLEAN Wait,
    _In_opt_ PEXT_DELETE_PARAMETERS Parameters)
{
    BOOLEAN WasSet = FALSE;

    if (Cancel)
    {
        WasSet = KeCancelTimer(&Timer->KeTimer);
    }

    if (Wait)
    {
        KeFlushQueuedDpcs();
    }

    if ((Parameters != NULL) && (Parameters->DeleteCallback != NULL))
    {
        Parameters->DeleteCallback(Parameters->DeleteContext);
    }

    ExFreePoolWithTag(Timer, EXP_TIMER_TAG);

    return WasSet;
}

/* EOF */
