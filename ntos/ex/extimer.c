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
    PETIMER Timer = DeferredContext;

    UNREFERENCED_PARAMETER(Dpc);
    UNREFERENCED_PARAMETER(SystemArgument1);
    UNREFERENCED_PARAMETER(SystemArgument2);

    KeAcquireSpinLockAtDpcLevel(&Timer->Lock);

    if (!Timer->ApcAssociated)
    {
        KeReleaseSpinLockFromDpcLevel(&Timer->Lock);
        return;
    }

    /*
     * The routine runs as an APC in the thread that asked for it. A one-shot
     * timer is done with once the APC has been queued.
     */
    if (KeInsertQueueApc(&Timer->TimerApc,
                         Timer->TimerApc.SystemArgument1,
                         Timer->TimerApc.SystemArgument2,
                         IO_NO_INCREMENT))
    {
        Timer->ApcQueuedSequence = Timer->SetSequence;
    }

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
        return;
    }

    /* A timer that does not repeat is finished with its thread now. */
    if (Timer->Period == 0)
    {
        Timer->ApcAssociated = FALSE;

        RemoveEntryList(&Timer->ActiveTimerListEntry);
        Timer->ActiveTimerListEntry.Flink = NULL;

        KeReleaseSpinLock(&Timer->Lock, OldIrql);

        ObDereferenceObject(Timer);
        return;
    }

    KeReleaseSpinLock(&Timer->Lock, OldIrql);
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
        if (KeInsertQueueApc(&Timer->TimerApc,
                             Timer->TimerApc.SystemArgument1,
                             Timer->TimerApc.SystemArgument2,
                             IO_NO_INCREMENT))
        {
            Timer->ApcQueuedSequence = Timer->SetSequence;
        }
    }

    KeReleaseSpinLock(&Timer->Lock, OldIrql);
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

    KeCancelTimer(&Timer->KeTimer);

    KeAcquireSpinLock(&ExpWakeListLock, &OldIrql);

    if (Timer->WakeTimerListEntry.Flink != NULL)
    {
        RemoveEntryList(&Timer->WakeTimerListEntry);
        Timer->WakeTimerListEntry.Flink = NULL;
    }

    KeReleaseSpinLock(&ExpWakeListLock, OldIrql);
}

static
VOID
ExpCancelTimerApc(
    _Inout_ PETIMER Timer)
{
    if (!Timer->ApcAssociated)
    {
        return;
    }

    Timer->ApcAssociated = FALSE;

    if (Timer->ActiveTimerListEntry.Flink != NULL)
    {
        RemoveEntryList(&Timer->ActiveTimerListEntry);
        Timer->ActiveTimerListEntry.Flink = NULL;
    }

    KeRemoveQueueApc(&Timer->TimerApc);
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
    Initializer.PoolType = NonPagedPool;
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
    PLIST_ENTRY Entry;
    KIRQL OldIrql;

    for (;;)
    {
        KeAcquireSpinLock(&ExpWakeListLock, &OldIrql);

        if (IsListEmpty(&Thread->ActiveTimerListHead))
        {
            KeReleaseSpinLock(&ExpWakeListLock, OldIrql);
            return;
        }

        Entry = Thread->ActiveTimerListHead.Flink;
        Timer = CONTAINING_RECORD(Entry, ETIMER, ActiveTimerListEntry);

        RemoveEntryList(&Timer->ActiveTimerListEntry);
        Timer->ActiveTimerListEntry.Flink = NULL;

        KeReleaseSpinLock(&ExpWakeListLock, OldIrql);

        KeAcquireSpinLock(&Timer->Lock, &OldIrql);

        if (Timer->ApcAssociated)
        {
            Timer->ApcAssociated = FALSE;
            KeCancelTimer(&Timer->KeTimer);
            KeRemoveQueueApc(&Timer->TimerApc);
        }

        KeReleaseSpinLock(&Timer->Lock, OldIrql);

        ObDereferenceObject(Timer);
    }
}

NTSTATUS
NTAPI
NtCreateTimer(
    _Out_ PHANDLE TimerHandle,
    _In_ ACCESS_MASK DesiredAccess,
    _In_opt_ POBJECT_ATTRIBUTES ObjectAttributes,
    _In_ TIMER_TYPE TimerType)
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
    KeInitializeDpc(&Timer->TimerDpc, ExpTimerDpcRoutine, Timer);
    KeInitializeSpinLock(&Timer->Lock);

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
NtCreateTimer2(
    _Out_ PHANDLE TimerHandle,
    _In_opt_ PVOID Reserved1,
    _In_opt_ POBJECT_ATTRIBUTES ObjectAttributes,
    _In_ ULONG Attributes,
    _In_ ACCESS_MASK DesiredAccess)
{
    UNREFERENCED_PARAMETER(Reserved1);

    /*
     * The attributes are the ones a driver's own timer takes, of which only the
     * kind of timer to make means anything to a timer a caller can name.
     */
    if (Attributes & ~(ULONG)(EX_TIMER_NOTIFICATION | EX_TIMER_HIGH_RESOLUTION))
    {
        return STATUS_INVALID_PARAMETER_4;
    }

    return NtCreateTimer(TimerHandle,
                         DesiredAccess,
                         ObjectAttributes,
                         (Attributes & EX_TIMER_NOTIFICATION) ?
                         NotificationTimer : SynchronizationTimer);
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
    ExpCancelTimerApc(Timer);

    State = KeCancelTimer(&Timer->KeTimer);

    Timer->SetSequence++;
    Timer->Period = Period;

    if (TimerApcRoutine != NULL)
    {
        KeInitializeApc(&Timer->TimerApc,
                        &Thread->Tcb,
                        CurrentApcEnvironment,
                        ExpTimerApcKernelRoutine,
                        NULL,
                        (PKNORMAL_ROUTINE)TimerApcRoutine,
                        PreviousMode,
                        TimerContext);

        Timer->ApcAssociated = TRUE;

        ObReferenceObject(Timer);

        KeAcquireSpinLockAtDpcLevel(&ExpWakeListLock);
        InsertTailList(&Thread->ActiveTimerListHead,
                       &Timer->ActiveTimerListEntry);
        KeReleaseSpinLockFromDpcLevel(&ExpWakeListLock);
    }

    KeReleaseSpinLock(&Timer->Lock, OldIrql);

    ExpSetWakeTimer(Timer, WakeTimer);

    KeSetTimerEx(&Timer->KeTimer,
                 SafeDueTime,
                 Period,
                 (TimerApcRoutine != NULL) ? &Timer->TimerDpc : NULL);

    ObDereferenceObject(Timer);

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
    BOOLEAN Dereference = FALSE;
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

    if (Timer->ApcAssociated)
    {
        ExpCancelTimerApc(Timer);
        Dereference = TRUE;
    }

    State = KeCancelTimer(&Timer->KeTimer);

    KeReleaseSpinLock(&Timer->Lock, OldIrql);

    ExpSetWakeTimer(Timer, FALSE);

    if (Dereference)
    {
        ObDereferenceObject(Timer);
    }

    ObDereferenceObject(Timer);

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
