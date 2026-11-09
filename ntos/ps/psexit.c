/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Ending a thread and a process, and taking apart what is left
 *              once the last thread is gone
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#include "pspriv.h"
#define NDEBUG
#include <debug.h>

/* GLOBALS ********************************************************************/

LIST_ENTRY PspReaperListHead;
WORK_QUEUE_ITEM PspReaperWorkItem;
BOOLEAN PspReaping;

/* PRIVATE FUNCTIONS **********************************************************/

/*
 * Whether the process has begun to go away. A caller that sees this is told
 * the process is terminating rather than being allowed to act on it.
 */
BOOLEAN
NTAPI
PspIsProcessExiting(
    _In_ PEPROCESS Process)
{
    return (BOOLEAN)(Process->ProcessExiting || Process->ProcessDelete);
}

/*
 * A process or thread marked to break on termination is one the system was
 * told it cannot do without, so the debugger is given the chance to look at
 * it before anything else happens.
 */
VOID
NTAPI
PspCatchCriticalBreak(
    _In_ PCSTR Message,
    _In_ PVOID Object,
    _In_ PCSTR ImageName)
{
    UNREFERENCED_PARAMETER(Object);

    if (!KdDebuggerEnabled || KdDebuggerNotPresent)
    {
        KeBugCheckEx(CRITICAL_PROCESS_DIED,
                     (ULONG_PTR)Object,
                     0,
                     0,
                     0);
    }

    DbgPrint(Message, ImageName);
    DbgBreakPoint();
}

/*
 * Tells the ports a caller registered that the thread is on its way out, and
 * gives up what was kept for each.
 */
static
VOID
PspSendTerminationMessages(
    _In_ PETHREAD Thread)
{
    PTERMINATION_PORT Port;
    PTERMINATION_PORT Next;

    PAGED_CODE();

    Port = Thread->TerminationPort;
    Thread->TerminationPort = NULL;

    while (Port != NULL)
    {
        Next = Port->Next;

        /* TODO: send LPC_CLIENT_DIED to Port->Port once ALPC carries it */
        ObDereferenceObject(Port->Port);
        ExFreePoolWithTag(Port, TAG_TERMINATE_APC);

        Port = Next;
    }
}

/*
 * A user-mode APC that was queued but never delivered is dropped here. A
 * thread on its way out will not run one, and whoever queued it has no claim
 * on the memory the control block sits in.
 */
static
VOID
PspDrainUserApcs(
    _In_ PETHREAD Thread)
{
    PLIST_ENTRY First;
    PLIST_ENTRY Entry;

    PAGED_CODE();

    First = KeFlushQueueApc(&Thread->Tcb, UserMode);
    if (First == NULL) return;

    /* What comes back is the list with its head taken out, so it is a ring */
    Entry = First;
    do
    {
        PKAPC Apc = CONTAINING_RECORD(Entry, KAPC, ApcListEntry);

        Entry = Entry->Flink;

        if (Apc->RundownRoutine != NULL) Apc->RundownRoutine(Apc);
        else ExFreePool(Apc);
    } while (Entry != First);
}

/* PUBLIC FUNCTIONS ***********************************************************/

/*
 * The work item that finishes with a thread the scheduler left behind. A
 * thread cannot free the stack it is standing on, so the last of it is done
 * here, on another thread's stack.
 */
VOID
NTAPI
PspReapRoutine(
    _In_ PVOID Context)
{
    PETHREAD *Link;

    UNREFERENCED_PARAMETER(Context);

    PAGED_CODE();

    /*
     * The whole chain is taken at once, which also leaves the head empty so
     * that the next thread to be reaped queues this work item again.
     */
    Link = (PETHREAD*)InterlockedExchangePointer((PVOID*)&PspReaperListHead.Flink,
                                                 NULL);

    while (Link != NULL)
    {
        PETHREAD Thread = CONTAINING_RECORD(Link, ETHREAD, ReaperLink);
        PETHREAD *Next = (PETHREAD*)(*Link);

        KeUninitThread(&Thread->Tcb);
        ObDereferenceObject(Thread);

        Link = Next;
    }
}

/*
 * Everything a thread does on its own way out, running on the thread itself.
 * It never returns: the scheduler takes the thread away at the end.
 */
VOID
NTAPI
PspExitThread(
    _In_ NTSTATUS ExitStatus)
{
    PETHREAD Thread = PsGetCurrentThread();
    PEPROCESS Process = THREAD_TO_PROCESS(Thread);
    BOOLEAN LastThread;
    PTEB Teb;

    PAGED_CODE();

    PspSetCrossThreadFlag(Thread, CT_TERMINATED_BIT);

    if (Thread->BreakOnTermination)
    {
        PspCatchCriticalBreak("Terminating critical thread 0x%p\n",
                              Thread,
                              (PCSTR)Process->ImageFileName);
    }

    Thread->ExitStatus = ExitStatus;
    KeQuerySystemTime(&Thread->ExitTime);

    /*
     * Nothing may be looking the thread up by identifier from here on, and
     * the wait is what makes sure nobody still is.
     */
    ExWaitForRundownProtectionRelease(&Thread->RundownProtect);

    KeEnterCriticalRegion();
    ExAcquirePushLockExclusive(&Process->ProcessLock);

    LastThread = (InterlockedDecrement((PLONG)&Process->ActiveThreads) == 0);
    Process->LastThreadExitStatus = ExitStatus;

    if (LastThread)
    {
        Process->ProcessExiting = TRUE;
        if (Process->ExitStatus == STATUS_PENDING)
            Process->ExitStatus = ExitStatus;
    }

    ExReleasePushLockExclusive(&Process->ProcessLock);
    KeLeaveCriticalRegion();

    PspDrainUserApcs(Thread);
    PspSendTerminationMessages(Thread);
    LpcExitThread(Thread);

    if (!Thread->SystemThread) PspRunCreateThreadNotifyRoutines(Thread, FALSE);

    if (Thread->PicoNotifyExit) PspPicoThreadExit(Thread);

    if (!Thread->HideFromDebugger) DbgkExitThread(ExitStatus);

    PspTlsThreadCleanup(Thread);
    PspRundownPropertySet(&Thread->PropertySet);

    /*
     * The process is finished with before the thread leaves its own list,
     * because what the process does on its way out needs its address space
     * and this is the last thread that can still reach it.
     */
    if (LastThread) PspExitProcess(TRUE, Process);

    KeEnterCriticalRegion();
    ExAcquirePushLockExclusive(&Process->ProcessLock);
    if (Thread->ThreadInserted)
    {
        RemoveEntryList(&Thread->ThreadListEntry);
        Thread->ThreadInserted = FALSE;
    }
    ExReleasePushLockExclusive(&Process->ProcessLock);
    KeLeaveCriticalRegion();

    PspDeleteThreadSecurity(Thread);

    Teb = Thread->Tcb.Teb;
    if (Teb != NULL)
    {
        MmDeleteTeb(Process, Teb);
        Thread->Tcb.Teb = NULL;
    }

    PspRunLegoRoutine(&Thread->Tcb);

    /* The identifier is given back last: until now the thread still has one */
    if (Thread->Cid.UniqueThread != NULL)
    {
        PspRemoveCid(Thread->Cid.UniqueThread);
        Thread->Cid.UniqueThread = NULL;
    }

    /* The scheduler does not come back to a terminated thread */
    KeTerminateThread(0);
}

/*
 * What the process does once its last thread is leaving. The handle table
 * goes first, because closing a handle can need the address space.
 */
VOID
NTAPI
PspExitProcess(
    _In_ BOOLEAN LastThread,
    _In_ PEPROCESS Process)
{
    PAGED_CODE();

    Process->ProcessExiting = TRUE;
    KeQuerySystemTime(&Process->ExitTime);

    if (!LastThread) return;

    if (Process->BreakOnTermination)
    {
        PspCatchCriticalBreak("Terminating critical process 0x%p (%s)\n",
                              Process,
                              (PCSTR)Process->ImageFileName);
    }

    if (Process->PicoCreated) PspPicoProcessExit(Process);

    if (Process->CreateReported)
    {
        Process->CreateReported = FALSE;
        Process->ExitProcessReported = TRUE;
        PspRunCreateProcessNotifyRoutines(Process, FALSE);
    }

    ObKillProcess(Process);

    if (Process->DebugPort != NULL) DbgkExitProcess(Process->ExitStatus);

    MmCleanProcessAddressSpace(Process);

    PspExitProcessFromJob(Process);

    KeEnterCriticalRegion();
    ExAcquirePushLockExclusive(&PspActiveProcessLock);
    if (Process->ProcessInserted)
    {
        RemoveEntryList(&Process->ActiveProcessLinks);
        Process->ProcessInserted = FALSE;
    }
    ExReleasePushLockExclusive(&PspActiveProcessLock);
    KeLeaveCriticalRegion();
}

/*
 * The special kernel APC a thread runs to end itself when somebody else
 * asked for it. It runs on the thread, which is the only place the exit can
 * happen.
 */
VOID
NTAPI
PsExitSpecialApc(
    _In_ PKAPC Apc,
    _Inout_ PKNORMAL_ROUTINE *NormalRoutine,
    _Inout_ PVOID *NormalContext,
    _Inout_ PVOID *SystemArgument1,
    _Inout_ PVOID *SystemArgument2)
{
    NTSTATUS ExitStatus = (NTSTATUS)(ULONG_PTR)Apc->NormalContext;

    UNREFERENCED_PARAMETER(NormalRoutine);
    UNREFERENCED_PARAMETER(NormalContext);
    UNREFERENCED_PARAMETER(SystemArgument1);
    UNREFERENCED_PARAMETER(SystemArgument2);

    PAGED_CODE();

    ExFreePoolWithTag(Apc, TAG_PS_APC);
    PspExitThread(ExitStatus);
}

VOID
NTAPI
PspExitNormalApc(
    _In_ PVOID NormalContext,
    _In_ PVOID SystemArgument1,
    _In_ PVOID SystemArgument2)
{
    UNREFERENCED_PARAMETER(NormalContext);
    UNREFERENCED_PARAMETER(SystemArgument1);
    UNREFERENCED_PARAMETER(SystemArgument2);

    PAGED_CODE();
}

/*
 * An exit APC that was queued but never ran leaves the control block behind,
 * and this is where it is given back.
 */
VOID
NTAPI
PspExitApcRundown(
    _In_ PKAPC Apc)
{
    PAGED_CODE();

    ExFreePoolWithTag(Apc, TAG_PS_APC);
}

/*
 * Ends a thread named by pointer. A thread ends itself on the spot; another
 * thread is ended by an APC it runs for itself, because only the thread can
 * take itself apart.
 */
NTSTATUS
NTAPI
PspTerminateThreadByPointer(
    _In_ PETHREAD Thread,
    _In_ NTSTATUS ExitStatus,
    _In_ BOOLEAN bSelf)
{
    PKAPC Apc;

    PAGED_CODE();

    if (bSelf && Thread == PsGetCurrentThread())
    {
        PspExitThread(ExitStatus);
        return STATUS_SUCCESS;
    }

    /* A thread already told to end is not told twice */
    if ((PspSetCrossThreadFlag(Thread, CT_TERMINATED_BIT) &
         CT_TERMINATED_BIT) != 0)
    {
        return STATUS_SUCCESS;
    }

    Apc = ExAllocatePoolWithTag(NonPagedPool, sizeof(KAPC), TAG_PS_APC);
    if (Apc == NULL)
    {
        PspClearCrossThreadFlag(Thread, CT_TERMINATED_BIT);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    KeInitializeApc(Apc,
                    &Thread->Tcb,
                    OriginalApcEnvironment,
                    PsExitSpecialApc,
                    PspExitApcRundown,
                    PspExitNormalApc,
                    KernelMode,
                    (PVOID)(ULONG_PTR)ExitStatus);

    if (!KeInsertQueueApc(Apc, Apc, NULL, 2))
    {
        ExFreePoolWithTag(Apc, TAG_PS_APC);
        return STATUS_UNSUCCESSFUL;
    }

    /*
     * A thread waiting in user mode has to be woken for the APC to run, and
     * one that never started has to be let go.
     */
    if (Thread->Tcb.State == Terminated) return STATUS_SUCCESS;

    KeForceResumeThread(&Thread->Tcb);
    return STATUS_SUCCESS;
}

/*
 * Ends every thread of a process. The process is marked as going first, so
 * no new thread can be made into it while its threads are being ended.
 */
NTSTATUS
NTAPI
PspTerminateProcess(
    _In_ PEPROCESS Process,
    _In_ NTSTATUS ExitStatus)
{
    PETHREAD Thread;
    PETHREAD Self = PsGetCurrentThread();
    NTSTATUS Status = STATUS_NOTHING_TO_TERMINATE;

    PAGED_CODE();

    if (Process->BreakOnTermination)
    {
        PspCatchCriticalBreak("Terminating critical process 0x%p (%s)\n",
                              Process,
                              (PCSTR)Process->ImageFileName);
    }

    KeEnterCriticalRegion();
    ExAcquirePushLockExclusive(&Process->ProcessLock);
    if (Process->ExitStatus == STATUS_PENDING) Process->ExitStatus = ExitStatus;
    Process->ProcessExiting = TRUE;
    ExReleasePushLockExclusive(&Process->ProcessLock);
    KeLeaveCriticalRegion();

    Thread = PsGetNextProcessThread(Process, NULL);
    while (Thread != NULL)
    {
        /*
         * A thread that asks for its own process to end finishes the walk
         * first and only then ends itself, so the other threads are told.
         */
        if (Thread != Self)
        {
            PspTerminateThreadByPointer(Thread, ExitStatus, FALSE);
        }

        Status = STATUS_SUCCESS;
        Thread = PsGetNextProcessThread(Process, Thread);
    }

    if (THREAD_TO_PROCESS(Self) == Process)
    {
        PspTerminateThreadByPointer(Self, ExitStatus, TRUE);
    }

    return Status;
}

NTSTATUS
NTAPI
PsTerminateProcess(
    _In_ PEPROCESS Process,
    _In_ NTSTATUS ExitStatus)
{
    return PspTerminateProcess(Process, ExitStatus);
}

/*
 * Ends the kernel thread that calls it. A thread that is not a system thread
 * has a user-mode side that would be left behind, so it is refused.
 */
NTSTATUS
NTAPI
PsTerminateSystemThread(
    _In_ NTSTATUS ExitStatus)
{
    PETHREAD Thread = PsGetCurrentThread();

    PAGED_CODE();

    if (!Thread->SystemThread) return STATUS_INVALID_PARAMETER;

    return PspTerminateThreadByPointer(Thread, ExitStatus, TRUE);
}

/*
 * The last of a thread object, once nothing names it any more. A thread that
 * never ran is taken apart here instead of by the reaper.
 */
VOID
NTAPI
PspDeleteThread(
    _In_ PVOID ObjectBody)
{
    PETHREAD Thread = ObjectBody;
    PEPROCESS Process = (PEPROCESS)Thread->Tcb.ApcState.Process;

    PAGED_CODE();

    if (Thread->Cid.UniqueThread != NULL)
    {
        PspRemoveCid(Thread->Cid.UniqueThread);
        Thread->Cid.UniqueThread = NULL;
    }

    PspDeleteThreadSecurity(Thread);

    if (Thread->ThreadName != NULL)
    {
        ExFreePoolWithTag(Thread->ThreadName, TAG_THREAD_NAME);
        Thread->ThreadName = NULL;
    }

    /* A thread that ran had its stack freed by the reaper */
    if (Thread->Tcb.InitialStack != NULL) KeUninitThread(&Thread->Tcb);

    if (Process != NULL)
    {
        Thread->Tcb.ApcState.Process = NULL;
        ObDereferenceObject(Process);
    }
}

/*
 * The last of a process object. By now it has no threads, so its address
 * space and everything it held are given back.
 */
VOID
NTAPI
PspDeleteProcess(
    _In_ PVOID ObjectBody)
{
    PEPROCESS Process = ObjectBody;

    PAGED_CODE();

    Process->ProcessDelete = TRUE;

    if (Process->UniqueProcessId != NULL)
    {
        PspRemoveCid(Process->UniqueProcessId);
        Process->UniqueProcessId = NULL;
    }

    if (Process->ProcessInserted)
    {
        KeEnterCriticalRegion();
        ExAcquirePushLockExclusive(&PspActiveProcessLock);
        RemoveEntryList(&Process->ActiveProcessLinks);
        ExReleasePushLockExclusive(&PspActiveProcessLock);
        KeLeaveCriticalRegion();
        Process->ProcessInserted = FALSE;
    }

    PspRemoveProcessFromJob(Process);
    PspDeleteProcessSecurity(Process);
    ObDereferenceDeviceMap(Process);

    if (Process->DebugPort != NULL)
    {
        ObDereferenceObject(Process->DebugPort);
        Process->DebugPort = NULL;
    }

    if (Process->ExceptionPortData != NULL)
    {
        ObDereferenceObject(Process->ExceptionPortData);
        Process->ExceptionPortData = NULL;
    }

    if (Process->SectionObject != NULL)
    {
        ObDereferenceObject(Process->SectionObject);
        Process->SectionObject = NULL;
    }

    if (Process->ImageFilePointer != NULL)
    {
        ObDereferenceObject(Process->ImageFilePointer);
        Process->ImageFilePointer = NULL;
    }

    if (Process->SeAuditProcessCreationInfo.ImageFileName != NULL)
    {
        ExFreePoolWithTag(Process->SeAuditProcessCreationInfo.ImageFileName,
                          TAG_SEPA);
        Process->SeAuditProcessCreationInfo.ImageFileName = NULL;
    }

    if (Process->WorkingSetWatch != NULL)
    {
        ExFreePoolWithTag(Process->WorkingSetWatch, TAG_PS_WS_WATCH);
        Process->WorkingSetWatch = NULL;
    }

    if (Process->DiskCounters != NULL)
    {
        ExFreePoolWithTag(Process->DiskCounters, TAG_PS_ENERGY);
        Process->DiskCounters = NULL;
    }

    if (Process->EnergyContext != NULL)
    {
        ExFreePoolWithTag(Process->EnergyContext, TAG_PS_ENERGY);
        Process->EnergyContext = NULL;
    }

    /* TODO: on x86 the process's LDT and its DOS-machine objects are no
     * longer torn down here; that belongs in an architecture hook */
    if (Process->HasAddressSpace)
    {
        MmDeleteProcessAddressSpace(Process);
        Process->HasAddressSpace = FALSE;
    }

    PspReturnQuotasOnDestroy(Process);
    PspDereferenceQuotaBlock(Process, Process->QuotaBlock);
}

/*
 * Keeps a process from finishing its exit while a caller looks at it. What
 * the caller is promised is that the process's address space and handle
 * table are still there until the matching release.
 */
NTSTATUS
NTAPI
PsAcquireProcessExitSynchronization(
    _In_ PEPROCESS Process)
{
    PAGED_CODE();

    if (!ExAcquireRundownProtection(&Process->RundownProtect))
        return STATUS_PROCESS_IS_TERMINATING;

    return STATUS_SUCCESS;
}

VOID
NTAPI
PsReleaseProcessExitSynchronization(
    _In_ PEPROCESS Process)
{
    PAGED_CODE();

    ExReleaseRundownProtection(&Process->RundownProtect);
}

/*
 * Walks the processes and names each one that is still there. Only the
 * debugger asks for this, when it wants to know what is left running.
 */
VOID
NTAPI
PspDumpThreads(
    _In_ BOOLEAN IncludeSystem)
{
    PEPROCESS Process;

    Process = PsGetNextProcess(NULL);
    while (Process != NULL)
    {
        PETHREAD Thread = PsGetNextProcessThread(Process, NULL);

        while (Thread != NULL)
        {
            if (IncludeSystem || !Thread->SystemThread)
            {
                DbgPrint("%-16s PID %-6Iu TID %-6Iu THREAD %p state %u\n",
                         Process->ImageFileName,
                         (ULONG_PTR)Process->UniqueProcessId,
                         (ULONG_PTR)Thread->Cid.UniqueThread,
                         Thread,
                         Thread->Tcb.State);
            }

            Thread = PsGetNextProcessThread(Process, Thread);
        }

        Process = PsGetNextProcess(Process);
    }
}

/*
 * Shutdown. Every process but the one doing the shutting down is ended, so
 * that nothing is still running when the system stops.
 */
VOID
NTAPI
PspShutdownProcessManager(VOID)
{
    PEPROCESS Self = PsGetCurrentProcess();
    PEPROCESS Process;

    PAGED_CODE();

    Process = PsGetNextProcess(NULL);
    while (Process != NULL)
    {
        if (Process != Self && Process != PsInitialSystemProcess)
            PspTerminateProcess(Process, STATUS_SYSTEM_SHUTDOWN);

        Process = PsGetNextProcess(Process);
    }

    ApphelpCacheShutdown();
}

/* SYSTEM CALLS ***************************************************************/

/*
 * Ends a process. A caller that names no process ends its own, and a caller
 * that names its own process is told so after the other threads are gone,
 * because its own thread does not come back from here.
 */
NTSTATUS
NTAPI
NtTerminateProcess(
    _In_opt_ HANDLE ProcessHandle,
    _In_ NTSTATUS ExitStatus)
{
    KPROCESSOR_MODE PreviousMode = ExGetPreviousMode();
    PEPROCESS Process;
    NTSTATUS Status;

    PAGED_CODE();

    if (ProcessHandle == NULL)
    {
        Process = PsGetCurrentProcess();
        ObReferenceObject(Process);
    }
    else
    {
        Status = ObReferenceObjectByHandle(ProcessHandle,
                                           PROCESS_TERMINATE,
                                           PsProcessType,
                                           PreviousMode,
                                           (PVOID*)&Process,
                                           NULL);
        if (!NT_SUCCESS(Status)) return Status;
    }

    /*
     * A process that is already going away cannot be told to go away again;
     * the caller is told that is what is happening.
     */
    if (Process->ProcessDelete)
    {
        ObDereferenceObject(Process);
        return STATUS_PROCESS_IS_TERMINATING;
    }

    Status = PspTerminateProcess(Process, ExitStatus);
    ObDereferenceObject(Process);

    /* Nothing was running in it, so there was nothing to end */
    if (Status == STATUS_NOTHING_TO_TERMINATE) return STATUS_SUCCESS;

    return Status;
}

/*
 * Ends a thread. A caller that names no thread ends its own. Ending the only
 * thread of a process ends the process with it, because a process without a
 * thread cannot run.
 */
NTSTATUS
NTAPI
NtTerminateThread(
    _In_opt_ HANDLE ThreadHandle,
    _In_ NTSTATUS ExitStatus)
{
    KPROCESSOR_MODE PreviousMode = ExGetPreviousMode();
    PETHREAD Self = PsGetCurrentThread();
    PETHREAD Thread;
    NTSTATUS Status;

    PAGED_CODE();

    if (ThreadHandle == NULL)
    {
        Thread = Self;
        ObReferenceObject(Thread);
    }
    else
    {
        Status = ObReferenceObjectByHandle(ThreadHandle,
                                           THREAD_TERMINATE,
                                           PsThreadType,
                                           PreviousMode,
                                           (PVOID*)&Thread,
                                           NULL);
        if (!NT_SUCCESS(Status)) return Status;
    }

    /* A thread outside the kernel may not be ended by a user-mode caller */
    if (Thread->SystemThread && PreviousMode != KernelMode)
    {
        ObDereferenceObject(Thread);
        return STATUS_INVALID_HANDLE;
    }

    if (Thread->Terminated)
    {
        ObDereferenceObject(Thread);
        return STATUS_THREAD_IS_TERMINATING;
    }

    Status = PspTerminateThreadByPointer(Thread,
                                         ExitStatus,
                                         (BOOLEAN)(Thread == Self));
    ObDereferenceObject(Thread);
    return Status;
}

/*
 * Asks to be told, on a port, when the calling thread ends. The port is kept
 * until then, and more than one may be registered.
 */
NTSTATUS
NTAPI
NtRegisterThreadTerminatePort(
    _In_ HANDLE PortHandle)
{
    KPROCESSOR_MODE PreviousMode = ExGetPreviousMode();
    PETHREAD Thread = PsGetCurrentThread();
    PTERMINATION_PORT Entry;
    PVOID Port;
    NTSTATUS Status;

    PAGED_CODE();

    Status = ObReferenceObjectByHandle(PortHandle,
                                       0,
                                       LpcPortObjectType,
                                       PreviousMode,
                                       &Port,
                                       NULL);
    if (!NT_SUCCESS(Status)) return Status;

    Entry = ExAllocatePoolWithTag(PagedPool, sizeof(*Entry), TAG_TERMINATE_APC);
    if (Entry == NULL)
    {
        ObDereferenceObject(Port);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    Entry->Port = Port;

    KeEnterCriticalRegion();
    ExAcquirePushLockExclusive(&Thread->ThreadLock);
    Entry->Next = Thread->TerminationPort;
    Thread->TerminationPort = Entry;
    ExReleasePushLockExclusive(&Thread->ThreadLock);
    KeLeaveCriticalRegion();

    return STATUS_SUCCESS;
}
