/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Holding a thread still, letting it go again, alerting it and
 *              handing it work to run on its own time
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#include "pspriv.h"
#define NDEBUG
#include <debug.h>

/* How many thread identifiers one alert call may name at once */
#define PSP_MAX_ALERT_BY_THREAD_ID 64

/* PRIVATE FUNCTIONS **********************************************************/

/*
 * Writes a count back to a caller that asked for one. A caller is told what
 * the count was before the call, which is what it uses to balance its own
 * suspends against ours.
 */
static
NTSTATUS
PspReturnCount(
    _Out_opt_ PULONG Destination,
    _In_ ULONG Value,
    _In_ KPROCESSOR_MODE PreviousMode)
{
    NTSTATUS Status = STATUS_SUCCESS;

    if (Destination == NULL) return STATUS_SUCCESS;

    if (PreviousMode == KernelMode)
    {
        *Destination = Value;
        return STATUS_SUCCESS;
    }

    _SEH2_TRY
    {
        *Destination = Value;
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Status = _SEH2_GetExceptionCode();
    }
    _SEH2_END;

    return Status;
}

/* PUBLIC FUNCTIONS ***********************************************************/

/*
 * Holds a thread still. The thread stops at the next point it can be made
 * to, not where it is now, and a thread already on its way out is not held
 * at all, because it has to be allowed to finish.
 */
NTSTATUS
NTAPI
PsSuspendThread(
    _In_ PETHREAD Thread,
    _Out_opt_ PULONG PreviousCount)
{
    ULONG Count = 0;
    NTSTATUS Status = STATUS_SUCCESS;

    PAGED_CODE();

    if (!ExAcquireRundownProtection(&Thread->RundownProtect))
        return STATUS_THREAD_IS_TERMINATING;

    if (Thread->Terminated)
    {
        ExReleaseRundownProtection(&Thread->RundownProtect);
        return STATUS_THREAD_IS_TERMINATING;
    }

    _SEH2_TRY
    {
        Count = KeSuspendThread(&Thread->Tcb);
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Status = _SEH2_GetExceptionCode();
    }
    _SEH2_END;

    ExReleaseRundownProtection(&Thread->RundownProtect);

    if (NT_SUCCESS(Status) && PreviousCount != NULL) *PreviousCount = Count;
    return Status;
}

NTSTATUS
NTAPI
PsResumeThread(
    _In_ PETHREAD Thread,
    _Out_opt_ PULONG PreviousCount)
{
    ULONG Count;

    PAGED_CODE();

    Count = KeResumeThread(&Thread->Tcb);

    if (PreviousCount != NULL) *PreviousCount = Count;
    return STATUS_SUCCESS;
}

/*
 * Holds every thread of a process still. A thread that appears while the
 * walk is going is not held, so a caller that needs the whole process still
 * has to keep it from making threads as well.
 */
NTSTATUS
NTAPI
PsSuspendProcess(
    _In_ PEPROCESS Process)
{
    PETHREAD Thread;
    NTSTATUS Status = STATUS_SUCCESS;

    PAGED_CODE();

    if (!ExAcquireRundownProtection(&Process->RundownProtect))
        return STATUS_PROCESS_IS_TERMINATING;

    Thread = PsGetNextProcessThread(Process, NULL);
    while (Thread != NULL)
    {
        NTSTATUS One = PsSuspendThread(Thread, NULL);

        /* A thread that is already leaving is not what the caller asked about */
        if (!NT_SUCCESS(One) && One != STATUS_THREAD_IS_TERMINATING)
            Status = One;

        Thread = PsGetNextProcessThread(Process, Thread);
    }

    ExReleaseRundownProtection(&Process->RundownProtect);
    return Status;
}

NTSTATUS
NTAPI
PsResumeProcess(
    _In_ PEPROCESS Process)
{
    PETHREAD Thread;

    PAGED_CODE();

    if (!ExAcquireRundownProtection(&Process->RundownProtect))
        return STATUS_PROCESS_IS_TERMINATING;

    Thread = PsGetNextProcessThread(Process, NULL);
    while (Thread != NULL)
    {
        PsResumeThread(Thread, NULL);
        Thread = PsGetNextProcessThread(Process, Thread);
    }

    ExReleaseRundownProtection(&Process->RundownProtect);
    return STATUS_SUCCESS;
}

/*
 * The kernel side of an APC a caller queued. It hands the routine and its
 * arguments to the user-mode dispatcher and gives the control block back.
 */
VOID
NTAPI
PspQueueApcSpecialApc(
    _In_ PKAPC Apc,
    _Inout_ PKNORMAL_ROUTINE *NormalRoutine,
    _Inout_ PVOID *NormalContext,
    _Inout_ PVOID *SystemArgument1,
    _Inout_ PVOID *SystemArgument2)
{
    UNREFERENCED_PARAMETER(NormalRoutine);
    UNREFERENCED_PARAMETER(NormalContext);
    UNREFERENCED_PARAMETER(SystemArgument1);
    UNREFERENCED_PARAMETER(SystemArgument2);

    PAGED_CODE();

    ExFreePoolWithTag(Apc, TAG_PS_APC);
}

/* SYSTEM CALLS ***************************************************************/

NTSTATUS
NTAPI
NtSuspendThread(
    _In_ HANDLE ThreadHandle,
    _Out_opt_ PULONG PreviousSuspendCount)
{
    KPROCESSOR_MODE PreviousMode = ExGetPreviousMode();
    ULONG Count = 0;
    PETHREAD Thread;
    NTSTATUS Status;

    PAGED_CODE();

    if (PreviousMode != KernelMode && PreviousSuspendCount != NULL)
    {
        _SEH2_TRY
        {
            ProbeForWriteUlong(PreviousSuspendCount);
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            _SEH2_YIELD(return _SEH2_GetExceptionCode());
        }
        _SEH2_END;
    }

    Status = ObReferenceObjectByHandle(ThreadHandle,
                                       THREAD_SUSPEND_RESUME,
                                       PsThreadType,
                                       PreviousMode,
                                       (PVOID*)&Thread,
                                       NULL);
    if (!NT_SUCCESS(Status)) return Status;

    Status = PsSuspendThread(Thread, &Count);
    ObDereferenceObject(Thread);

    if (!NT_SUCCESS(Status)) return Status;

    return PspReturnCount(PreviousSuspendCount, Count, PreviousMode);
}

NTSTATUS
NTAPI
NtResumeThread(
    _In_ HANDLE ThreadHandle,
    _Out_opt_ PULONG PreviousSuspendCount)
{
    KPROCESSOR_MODE PreviousMode = ExGetPreviousMode();
    ULONG Count = 0;
    PETHREAD Thread;
    NTSTATUS Status;

    PAGED_CODE();

    if (PreviousMode != KernelMode && PreviousSuspendCount != NULL)
    {
        _SEH2_TRY
        {
            ProbeForWriteUlong(PreviousSuspendCount);
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            _SEH2_YIELD(return _SEH2_GetExceptionCode());
        }
        _SEH2_END;
    }

    Status = ObReferenceObjectByHandle(ThreadHandle,
                                       THREAD_SUSPEND_RESUME,
                                       PsThreadType,
                                       PreviousMode,
                                       (PVOID*)&Thread,
                                       NULL);
    if (!NT_SUCCESS(Status)) return Status;

    Status = PsResumeThread(Thread, &Count);
    ObDereferenceObject(Thread);

    if (!NT_SUCCESS(Status)) return Status;

    return PspReturnCount(PreviousSuspendCount, Count, PreviousMode);
}

NTSTATUS
NTAPI
NtSuspendProcess(
    _In_ HANDLE ProcessHandle)
{
    KPROCESSOR_MODE PreviousMode = ExGetPreviousMode();
    PEPROCESS Process;
    NTSTATUS Status;

    PAGED_CODE();

    Status = ObReferenceObjectByHandle(ProcessHandle,
                                       PROCESS_SUSPEND_RESUME,
                                       PsProcessType,
                                       PreviousMode,
                                       (PVOID*)&Process,
                                       NULL);
    if (!NT_SUCCESS(Status)) return Status;

    Status = PsSuspendProcess(Process);
    ObDereferenceObject(Process);
    return Status;
}

NTSTATUS
NTAPI
NtResumeProcess(
    _In_ HANDLE ProcessHandle)
{
    KPROCESSOR_MODE PreviousMode = ExGetPreviousMode();
    PEPROCESS Process;
    NTSTATUS Status;

    PAGED_CODE();

    Status = ObReferenceObjectByHandle(ProcessHandle,
                                       PROCESS_SUSPEND_RESUME,
                                       PsProcessType,
                                       PreviousMode,
                                       (PVOID*)&Process,
                                       NULL);
    if (!NT_SUCCESS(Status)) return Status;

    Status = PsResumeProcess(Process);
    ObDereferenceObject(Process);
    return Status;
}

/*
 * Alerts a thread, which makes the wait it is in, or the next alertable wait
 * it enters, end with STATUS_ALERTED instead of completing.
 */
NTSTATUS
NTAPI
NtAlertThread(
    _In_ HANDLE ThreadHandle)
{
    KPROCESSOR_MODE PreviousMode = ExGetPreviousMode();
    PETHREAD Thread;
    NTSTATUS Status;

    PAGED_CODE();

    Status = ObReferenceObjectByHandle(ThreadHandle,
                                       THREAD_ALERT,
                                       PsThreadType,
                                       PreviousMode,
                                       (PVOID*)&Thread,
                                       NULL);
    if (!NT_SUCCESS(Status)) return Status;

    /*
     * A caller in the kernel alerting on behalf of user mode still alerts the
     * user-mode side, which is the side a thread waits alertably on.
     */
    KeAlertThread(&Thread->Tcb, UserMode);
    ObDereferenceObject(Thread);

    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
NtAlertResumeThread(
    _In_ HANDLE ThreadHandle,
    _Out_opt_ PULONG PreviousSuspendCount)
{
    KPROCESSOR_MODE PreviousMode = ExGetPreviousMode();
    PETHREAD Thread;
    NTSTATUS Status;
    ULONG Count;

    PAGED_CODE();

    if (PreviousMode != KernelMode && PreviousSuspendCount != NULL)
    {
        _SEH2_TRY
        {
            ProbeForWriteUlong(PreviousSuspendCount);
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            _SEH2_YIELD(return _SEH2_GetExceptionCode());
        }
        _SEH2_END;
    }

    Status = ObReferenceObjectByHandle(ThreadHandle,
                                       THREAD_SUSPEND_RESUME,
                                       PsThreadType,
                                       PreviousMode,
                                       (PVOID*)&Thread,
                                       NULL);
    if (!NT_SUCCESS(Status)) return Status;

    Count = KeAlertResumeThread(&Thread->Tcb);
    ObDereferenceObject(Thread);

    return PspReturnCount(PreviousSuspendCount, Count, PreviousMode);
}

/*
 * Tells the calling thread to notice an alert that was left for it. A thread
 * that was alerted is told so; one that was not carries on.
 */
NTSTATUS
NTAPI
NtTestAlert(VOID)
{
    PAGED_CODE();

    return KeTestAlertThread(ExGetPreviousMode()) ? STATUS_ALERTED
                                                  : STATUS_SUCCESS;
}

/*
 * Alerts a thread by identifier rather than by handle, which is what a lock
 * in user mode uses: it has the identifier of the thread it is waking and no
 * handle to it.
 */
NTSTATUS
NTAPI
NtAlertThreadByThreadId(
    _In_ HANDLE ThreadId)
{
    PETHREAD Thread;
    NTSTATUS Status;

    PAGED_CODE();

    Status = PsLookupThreadByThreadId(ThreadId, &Thread);
    if (!NT_SUCCESS(Status)) return Status;

    /* Only a thread of the same process may be woken this way */
    if (THREAD_TO_PROCESS(Thread) != PsGetCurrentProcess())
    {
        ObDereferenceObject(Thread);
        return STATUS_ACCESS_DENIED;
    }

    KeAlertThreadByThreadId(&Thread->Tcb);
    ObDereferenceObject(Thread);

    return STATUS_SUCCESS;
}

/*
 * The same for several threads at once, which lets a lock wake everybody
 * waiting on it without a call for each.
 */
NTSTATUS
NTAPI
NtAlertMultipleThreadByThreadId(
    _In_reads_(ThreadCount) PHANDLE ThreadIds,
    _In_ ULONG ThreadCount,
    _Reserved_ PVOID Reserved1,
    _Reserved_ PVOID Reserved2)
{
    KPROCESSOR_MODE PreviousMode = ExGetPreviousMode();
    HANDLE Safe[PSP_MAX_ALERT_BY_THREAD_ID];
    ULONG Index;

    PAGED_CODE();

    if (Reserved1 != NULL || Reserved2 != NULL)
        return STATUS_INVALID_PARAMETER;

    if (ThreadCount == 0) return STATUS_SUCCESS;
    if (ThreadCount > PSP_MAX_ALERT_BY_THREAD_ID)
        return STATUS_INVALID_PARAMETER;

    if (PreviousMode != KernelMode)
    {
        _SEH2_TRY
        {
            ProbeForRead(ThreadIds,
                         ThreadCount * sizeof(HANDLE),
                         sizeof(HANDLE));
            RtlCopyMemory(Safe, ThreadIds, ThreadCount * sizeof(HANDLE));
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            _SEH2_YIELD(return _SEH2_GetExceptionCode());
        }
        _SEH2_END;
    }
    else
    {
        RtlCopyMemory(Safe, ThreadIds, ThreadCount * sizeof(HANDLE));
    }

    /*
     * Every identifier that names a thread is alerted; one that names none is
     * passed over, because a thread that has already gone needs no waking.
     */
    for (Index = 0; Index < ThreadCount; Index++)
    {
        NtAlertThreadByThreadId(Safe[Index]);
    }

    return STATUS_SUCCESS;
}

/*
 * Waits until somebody alerts this thread by its identifier. The address is
 * what the waiter and the waker agree on, and the timeout bounds the wait.
 */
NTSTATUS
NTAPI
NtWaitForAlertByThreadId(
    _In_ PVOID Address,
    _In_opt_ PLARGE_INTEGER Timeout)
{
    KPROCESSOR_MODE PreviousMode = ExGetPreviousMode();
    LARGE_INTEGER SafeTimeout;
    PLARGE_INTEGER Used = NULL;

    PAGED_CODE();

    if (Timeout != NULL)
    {
        if (PreviousMode != KernelMode)
        {
            _SEH2_TRY
            {
                ProbeForRead(Timeout,
                             sizeof(LARGE_INTEGER),
                             sizeof(ULONG));
                SafeTimeout = *Timeout;
            }
            _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
            {
                _SEH2_YIELD(return _SEH2_GetExceptionCode());
            }
            _SEH2_END;
        }
        else
        {
            SafeTimeout = *Timeout;
        }

        Used = &SafeTimeout;
    }

    return KeWaitForAlertByThreadId(Address, PreviousMode, Used);
}

/*
 * Hands a thread a routine to run in its own user-mode context the next time
 * it waits alertably or returns to user mode.
 */
NTSTATUS
NTAPI
NtQueueApcThreadEx2(
    _In_ HANDLE ThreadHandle,
    _In_opt_ HANDLE ReserveHandle,
    _In_ ULONG QueueUserApcFlags,
    _In_ PKNORMAL_ROUTINE ApcRoutine,
    _In_opt_ PVOID NormalContext,
    _In_opt_ PVOID SystemArgument1,
    _In_opt_ PVOID SystemArgument2)
{
    KPROCESSOR_MODE PreviousMode = ExGetPreviousMode();
    PVOID Reserve = NULL;
    PETHREAD Thread;
    NTSTATUS Status;
    PKAPC Apc;

    PAGED_CODE();

    /* TODO: honour QUEUE_USER_APC_FLAGS_SPECIAL_USER_APC and the callback-data
     * context flags; only an ordinary user APC is queued today */
    if (QueueUserApcFlags != 0) return STATUS_INVALID_PARAMETER;

    if (ReserveHandle != NULL)
    {
        Status = ObReferenceObjectByHandle(ReserveHandle,
                                           EVENT_ALL_ACCESS,
                                           ExUserApcReserveObjectType,
                                           PreviousMode,
                                           &Reserve,
                                           NULL);
        if (!NT_SUCCESS(Status)) return Status;
    }

    Status = ObReferenceObjectByHandle(ThreadHandle,
                                       THREAD_SET_CONTEXT,
                                       PsThreadType,
                                       PreviousMode,
                                       (PVOID*)&Thread,
                                       NULL);
    if (!NT_SUCCESS(Status))
    {
        if (Reserve != NULL) ObDereferenceObject(Reserve);
        return Status;
    }

    /* A thread with no user-mode side has nowhere to run a user APC */
    if (Thread->SystemThread)
    {
        ObDereferenceObject(Thread);
        if (Reserve != NULL) ObDereferenceObject(Reserve);
        return STATUS_THREAD_IS_TERMINATING;
    }

    /* TODO: take the control block out of the reserve object when one is given */
    Apc = ExAllocatePoolWithTag(NonPagedPool, sizeof(KAPC), TAG_PS_APC);
    if (Apc == NULL)
    {
        ObDereferenceObject(Thread);
        if (Reserve != NULL) ObDereferenceObject(Reserve);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    KeInitializeApc(Apc,
                    &Thread->Tcb,
                    OriginalApcEnvironment,
                    PspQueueApcSpecialApc,
                    NULL,
                    ApcRoutine,
                    UserMode,
                    NormalContext);

    if (!KeInsertQueueApc(Apc,
                          SystemArgument1,
                          SystemArgument2,
                          IO_NO_INCREMENT))
    {
        ExFreePoolWithTag(Apc, TAG_PS_APC);
        Status = STATUS_UNSUCCESSFUL;
    }

    ObDereferenceObject(Thread);
    if (Reserve != NULL) ObDereferenceObject(Reserve);

    return Status;
}

NTSTATUS
NTAPI
NtQueueApcThreadEx(
    _In_ HANDLE ThreadHandle,
    _In_opt_ HANDLE UserApcReserveHandle,
    _In_ PKNORMAL_ROUTINE ApcRoutine,
    _In_opt_ PVOID NormalContext,
    _In_opt_ PVOID SystemArgument1,
    _In_opt_ PVOID SystemArgument2)
{
    PAGED_CODE();

    return NtQueueApcThreadEx2(ThreadHandle,
                               UserApcReserveHandle,
                               0,
                               ApcRoutine,
                               NormalContext,
                               SystemArgument1,
                               SystemArgument2);
}

NTSTATUS
NTAPI
NtQueueApcThread(
    _In_ HANDLE ThreadHandle,
    _In_ PKNORMAL_ROUTINE ApcRoutine,
    _In_opt_ PVOID NormalContext,
    _In_opt_ PVOID SystemArgument1,
    _In_opt_ PVOID SystemArgument2)
{
    PAGED_CODE();

    return NtQueueApcThreadEx2(ThreadHandle,
                               NULL,
                               0,
                               ApcRoutine,
                               NormalContext,
                               SystemArgument1,
                               SystemArgument2);
}
