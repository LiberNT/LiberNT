/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Job objects: a set of processes that are limited, accounted
 *              for and terminated together
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#include "pspriv.h"
#define NDEBUG
#include <debug.h>

/* GLOBALS ********************************************************************/

POBJECT_TYPE PsJobType;

GENERIC_MAPPING PspJobMapping =
{
    STANDARD_RIGHTS_READ | JOB_OBJECT_QUERY,
    STANDARD_RIGHTS_WRITE | JOB_OBJECT_ASSIGN_PROCESS |
        JOB_OBJECT_SET_ATTRIBUTES | JOB_OBJECT_TERMINATE,
    STANDARD_RIGHTS_EXECUTE | SYNCHRONIZE,
    JOB_OBJECT_ALL_ACCESS
};

BOOLEAN PspUseJobSchedulingClasses;

/* How many jobs one set may hold */
#define PSP_MAX_JOB_SET_MEMBERS 64

/*
 * How much of a quantum a job's processes are given, by scheduling class.
 * The middle class is what a job gets unless it asks for another.
 */
CHAR PspJobSchedulingClasses[PSP_JOB_SCHEDULING_CLASSES] =
{
    1 * 6, 2 * 6, 3 * 6, 4 * 6, 5 * 6, 6 * 6, 7 * 6, 8 * 6, 9 * 6, 10 * 6
};

static LIST_ENTRY PspJobList;
static ERESOURCE PspJobListLock;

/* PRIVATE FUNCTIONS **********************************************************/

/*
 * Tells whoever is watching the job that something happened in it. A job
 * with no port to be told on simply has nobody listening.
 */
NTSTATUS
NTAPI
PspSendJobMessageLocked(
    _In_ PEJOB Job,
    _In_ ULONG Message,
    _In_opt_ PVOID CompletionValue,
    _In_ BOOLEAN Quota)
{
    PAGED_CODE();

    UNREFERENCED_PARAMETER(Quota);

    if (Job->CompletionPort == NULL) return STATUS_SUCCESS;

    return IoSetIoCompletion(Job->CompletionPort,
                             Job->CompletionKey,
                             CompletionValue,
                             STATUS_SUCCESS,
                             Message,
                             TRUE);
}

/*
 * Asks the windowing kernel to apply, or lift, the restrictions a job puts
 * on what its processes may do to the desktop.
 */
NTSTATUS
NTAPI
PspInvokeW32JobCallout(
    _In_ PEJOB Job,
    _In_ PSW32JOBCALLOUTTYPE CalloutType,
    _In_opt_ PVOID Data)
{
    WIN32_JOBCALLOUT_PARAMETERS Parameters;

    PAGED_CODE();

    if (PspW32JobCallout == NULL) return STATUS_SUCCESS;

    Parameters.Job = Job;
    Parameters.CalloutType = CalloutType;
    Parameters.Data = Data;

    return PspW32JobCallout(&Parameters);
}

/*
 * Whether Ancestor is the job itself or one of the jobs it is nested in.
 * Entered with the job hierarchy lock held.
 */
static
BOOLEAN
PspIsJobWithin(
    _In_opt_ PEJOB Job,
    _In_ PEJOB Ancestor)
{
    for (; Job != NULL; Job = Job->ParentJob)
    {
        if (Job == Ancestor) return TRUE;
    }

    return FALSE;
}

/* Whether the job or one it is nested in restricts the desktop. Entered with the job hierarchy lock held. */
static
BOOLEAN
PspIsJobChainUiRestricted(
    _In_opt_ PEJOB Job)
{
    for (; Job != NULL; Job = Job->ParentJob)
    {
        if (Job->UIRestrictionsClass != 0) return TRUE;
    }

    return FALSE;
}

/* Whether a job nested below this one restricts the desktop. Entered with the job hierarchy lock held. */
static
BOOLEAN
PspIsJobTreeUiRestricted(
    _In_ PEJOB Job)
{
    PLIST_ENTRY Entry;

    for (Entry = Job->ChildJobListHead.Flink;
         Entry != &Job->ChildJobListHead;
         Entry = Entry->Flink)
    {
        PEJOB Child = CONTAINING_RECORD(Entry, EJOB, SiblingJobLinks);

        if (Child->UIRestrictionsClass != 0 || PspIsJobTreeUiRestricted(Child)) return TRUE;
    }

    return FALSE;
}

/* Entered with the job hierarchy lock held */
static
NTSTATUS
PspEnumerateJobTree(
    _In_ PEJOB Job,
    _In_ PJOB_ENUMERATOR_CALLBACK Callback,
    _In_opt_ PVOID Context)
{
    NTSTATUS Status = STATUS_SUCCESS;
    PLIST_ENTRY Entry;

    ExAcquireResourceSharedLite(&Job->JobLock, TRUE);

    for (Entry = Job->ProcessListHead.Flink;
         Entry != &Job->ProcessListHead;
         Entry = Entry->Flink)
    {
        Status = Callback(CONTAINING_RECORD(Entry, EPROCESS, JobLinks), Context);
        if (!NT_SUCCESS(Status)) break;
    }

    ExReleaseResourceLite(&Job->JobLock);

    for (Entry = Job->ChildJobListHead.Flink;
         NT_SUCCESS(Status) && Entry != &Job->ChildJobListHead;
         Entry = Entry->Flink)
    {
        Status = PspEnumerateJobTree(CONTAINING_RECORD(Entry, EJOB, SiblingJobLinks),
                                     Callback,
                                     Context);
    }

    return Status;
}

/*
 * Calls back for every process in the job and in the jobs nested in it, with
 * the hierarchy held so that neither the lists nor the nesting can change
 * underneath. A callback that fails stops the walk.
 */
NTSTATUS
NTAPI
PspEnumerateProcessesInJob(
    _In_ PEJOB Job,
    _In_ PJOB_ENUMERATOR_CALLBACK Callback,
    _In_opt_ PVOID Context)
{
    NTSTATUS Status;

    PAGED_CODE();

    KeEnterCriticalRegion();
    ExAcquireResourceSharedLite(&PspJobListLock, TRUE);
    Status = PspEnumerateJobTree(Job, Callback, Context);
    ExReleaseResourceLite(&PspJobListLock);
    KeLeaveCriticalRegion();

    return Status;
}

typedef struct _PSP_JOB_TERMINATE_CONTEXT
{
    NTSTATUS ExitStatus;
    BOOLEAN EndCaller;
} PSP_JOB_TERMINATE_CONTEXT, *PPSP_JOB_TERMINATE_CONTEXT;

static
NTSTATUS
NTAPI
PspTerminateJobMember(
    _In_ PEPROCESS Process,
    _In_opt_ PVOID Context)
{
    PPSP_JOB_TERMINATE_CONTEXT Terminate = Context;

    /* A process already on its way out is left to finish: its last thread may be the one closing the job */
    if (Process->ProcessExiting) return STATUS_SUCCESS;

    /* The caller does not come back from ending itself, so it goes once the walk is over */
    if (Process == PsGetCurrentProcess())
    {
        Terminate->EndCaller = TRUE;
        return STATUS_SUCCESS;
    }

    PspTerminateProcess(Process, Terminate->ExitStatus);
    return STATUS_SUCCESS;
}

/*
 * Applies to one process the limits its job carries. A process joining a job
 * is brought under them here, which is also where a changed limit reaches
 * the processes already in the job.
 */
NTSTATUS
NTAPI
PspAddProcessToJobLimits(
    _In_ PEPROCESS Process,
    _In_ PEJOB Job)
{
    PAGED_CODE();

    if ((Job->LimitFlags & JOB_OBJECT_LIMIT_PRIORITY_CLASS) != 0)
        PspSetProcessPriorityClass(Process, Job->PriorityClass);

    if ((Job->LimitFlags & JOB_OBJECT_LIMIT_AFFINITY) != 0)
        KeSetAffinityProcess(&Process->Pcb, Job->Affinity.Bitmap[0]);

    if ((Job->LimitFlags & JOB_OBJECT_LIMIT_SCHEDULING_CLASS) != 0 &&
        PspUseJobSchedulingClasses)
    {
        KeSetQuantumProcess(
            &Process->Pcb,
            (UCHAR)PspJobSchedulingClasses[Job->SchedulingClass]);
    }

    /* TODO: apply the working-set limits through the memory manager, and the
     * per-process time limit through a timer */
    return STATUS_SUCCESS;
}

static
VOID
PspRecordJobMemoryPeaks(
    _Inout_ PEJOB Job,
    _In_ ULONGLONG ProcessPages)
{
    if (ProcessPages > Job->PeakProcessMemoryUsed) Job->PeakProcessMemoryUsed = ProcessPages;
    if (Job->CurrentJobMemoryUsed > Job->PeakJobMemoryUsed) Job->PeakJobMemoryUsed = Job->CurrentJobMemoryUsed;
}

static
VOID
PspSendJobChainMessage(
    _In_ PEJOB Job,
    _In_ ULONG Message,
    _In_opt_ PVOID CompletionValue)
{
    ExAcquireResourceSharedLite(&PspJobListLock, TRUE);

    for (; Job != NULL; Job = Job->ParentJob)
    {
        ExAcquireResourceSharedLite(&Job->JobLock, TRUE);
        PspSendJobMessageLocked(Job, Message, CompletionValue, TRUE);
        ExReleaseResourceLite(&Job->JobLock);
    }

    ExReleaseResourceLite(&PspJobListLock);
}

/* PUBLIC FUNCTIONS ***********************************************************/

CODE_SEG("INIT")
VOID
NTAPI
PspInitializeJobStructures(VOID)
{
    struct
    {
        OBJECT_TYPE_INITIALIZER Initializer;
        ULONG SeMandatoryLabelMask;
        ULONG SeTrustConstraintMask;
    } Extended;
    UNICODE_STRING Name = RTL_CONSTANT_STRING(L"Job");

    PAGED_CODE();

    InitializeListHead(&PspJobList);
    ExInitializeResourceLite(&PspJobListLock);

    RtlZeroMemory(&Extended, sizeof(Extended));
    Extended.SeMandatoryLabelMask = SYSTEM_MANDATORY_LABEL_NO_WRITE_UP;
    Extended.Initializer.Length = sizeof(Extended);
    Extended.Initializer.GenericMapping = PspJobMapping;
    Extended.Initializer.PoolType = NonPagedPoolNx;
    Extended.Initializer.ValidAccessMask = JOB_OBJECT_ALL_ACCESS;
    Extended.Initializer.DefaultNonPagedPoolCharge = sizeof(EJOB);
    Extended.Initializer.SecurityRequired = TRUE;
    Extended.Initializer.CloseProcedure = PspCloseJob;
    Extended.Initializer.DeleteProcedure = PspDeleteJob;
    Extended.Initializer.SecurityProcedure = SeDefaultObjectMethod;

    ObCreateObjectType(&Name, &Extended.Initializer, NULL, &PsJobType);
}

/*
 * The last handle to the job was closed. A job told to do so takes its
 * processes with it, which is what makes a job a way of cleaning up after a
 * set of processes without having to find them all.
 */
VOID
NTAPI
PspCloseJob(
    _In_ PEPROCESS Process,
    _In_ PVOID ObjectBody,
    _In_ ULONG_PTR ProcessHandleCount,
    _In_ ULONG_PTR SystemHandleCount)
{
    PEJOB Job = ObjectBody;

    PAGED_CODE();

    UNREFERENCED_PARAMETER(Process);
    UNREFERENCED_PARAMETER(ProcessHandleCount);

    if (SystemHandleCount != 1) return;

    KeEnterCriticalRegion();
    ExAcquireResourceExclusiveLite(&Job->JobLock, TRUE);

    if (Job->CloseDone)
    {
        ExReleaseResourceLite(&Job->JobLock);
        KeLeaveCriticalRegion();
        return;
    }

    Job->CloseDone = TRUE;

    ExReleaseResourceLite(&Job->JobLock);
    KeLeaveCriticalRegion();

    if (Job->UIRestrictionsClass != 0)
    {
        PspInvokeW32JobCallout(Job, PsW32JobCalloutTerminate, NULL);
    }

    if ((Job->LimitFlags & JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE) != 0)
        PspTerminateJobObject(Job, KernelMode, STATUS_SUCCESS);
}

VOID
NTAPI
PspDeleteJob(
    _In_ PVOID ObjectBody)
{
    PEJOB Job = ObjectBody;
    PEJOB Parent;

    PAGED_CODE();

    KeEnterCriticalRegion();
    ExAcquireResourceExclusiveLite(&PspJobListLock, TRUE);
    if (Job->JobLinks.Flink != NULL) RemoveEntryList(&Job->JobLinks);

    /* A nested job holds its parent until it goes */
    Parent = Job->ParentJob;
    if (Parent != NULL)
    {
        RemoveEntryList(&Job->SiblingJobLinks);
        Job->ParentJob = NULL;
    }

    ExReleaseResourceLite(&PspJobListLock);
    KeLeaveCriticalRegion();

    if (Parent != NULL) ObDereferenceObject(Parent);

    if (Job->CompletionPort != NULL)
    {
        ObDereferenceObject(Job->CompletionPort);
        Job->CompletionPort = NULL;
    }

    PspRundownPropertySet(&Job->PropertySet);
    ExDeleteResourceLite(&Job->JobLock);
}

/*
 * Ends every process in the job. The walk takes a reference on each process
 * as it goes, because terminating one can let go of the job's list.
 */
NTSTATUS
NTAPI
PspTerminateJobObject(
    _In_ PEJOB Job,
    _In_ KPROCESSOR_MODE AccessMode,
    _In_ NTSTATUS ExitStatus)
{
    PSP_JOB_TERMINATE_CONTEXT Terminate;

    PAGED_CODE();

    UNREFERENCED_PARAMETER(AccessMode);

    KeEnterCriticalRegion();
    ExAcquireResourceExclusiveLite(&Job->JobLock, TRUE);
    Job->Terminating = TRUE;
    ExReleaseResourceLite(&Job->JobLock);
    KeLeaveCriticalRegion();

    Terminate.ExitStatus = ExitStatus;
    Terminate.EndCaller = FALSE;
    PspEnumerateProcessesInJob(Job, PspTerminateJobMember, &Terminate);

    KeSetEvent(&Job->Event, IO_NO_INCREMENT, FALSE);

    KeEnterCriticalRegion();
    ExAcquireResourceExclusiveLite(&Job->JobLock, TRUE);
    Job->Terminating = FALSE;
    ExReleaseResourceLite(&Job->JobLock);
    KeLeaveCriticalRegion();

    /* The caller's process goes too: its other threads now, the caller once it is back out of the call */
    if (Terminate.EndCaller)
    {
        ZwTerminateProcess(NULL, ExitStatus);
        PspTerminateThreadByPointer(PsGetCurrentThread(), ExitStatus, FALSE);
    }

    return STATUS_SUCCESS;
}

/*
 * Puts a process into a job. A process already in a job may also be put into
 * a job nested inside it, or into a job that is in no hierarchy and holds
 * nothing, which is then nested inside the process's job; a job the process
 * is already in, directly or through a nested one, is already its job. Every
 * job the process enters counts it and is told of it, innermost first.
 */
NTSTATUS
NTAPI
PspAssignProcessToJob(
    _In_ PEPROCESS Process,
    _In_ PEJOB Job)
{
    PEJOB Current;
    PEJOB Joined;
    ULONGLONG Committed;
    BOOLEAN Nest = FALSE;
    NTSTATUS Status = STATUS_SUCCESS;

    PAGED_CODE();

    if (!ExAcquireRundownProtection(&Process->RundownProtect))
        return STATUS_PROCESS_IS_TERMINATING;

    KeEnterCriticalRegion();
    ExAcquireResourceExclusiveLite(&PspJobListLock, TRUE);

    Current = Process->Job;

    if (Current != NULL && PspIsJobWithin(Current, Job))
    {
        ExReleaseResourceLite(&PspJobListLock);
        KeLeaveCriticalRegion();
        ExReleaseRundownProtection(&Process->RundownProtect);
        return STATUS_SUCCESS;
    }

    if (Current != NULL && !PspIsJobWithin(Job, Current))
    {
        if (Job->ParentJob != NULL ||
            Job->TotalProcesses != 0 ||
            !IsListEmpty(&Job->ChildJobListHead))
        {
            Status = STATUS_ACCESS_DENIED;
        }
        else
        {
            Nest = TRUE;
        }
    }

    /* One job of a chain may restrict the desktop, not two */
    if (NT_SUCCESS(Status) && Current != NULL && Job->UIRestrictionsClass != 0 &&
        PspIsJobChainUiRestricted(Current))
    {
        Status = STATUS_NOT_SUPPORTED;
    }

    if (NT_SUCCESS(Status))
    {
        ExAcquireResourceSharedLite(&Job->JobLock, TRUE);

        if (Job->CloseDone || Job->DisallowNewProcesses)
        {
            Status = STATUS_INVALID_PARAMETER;
        }
        /* A job may be told how many processes it holds at once; one too many is refused */
        else if ((Job->LimitFlags & JOB_OBJECT_LIMIT_ACTIVE_PROCESS) != 0 &&
                 Job->ActiveProcesses >= Job->ActiveProcessLimit)
        {
            Status = STATUS_QUOTA_EXCEEDED;
        }

        ExReleaseResourceLite(&Job->JobLock);
    }

    if (NT_SUCCESS(Status) && Nest)
    {
        ObReferenceObject(Current);
        Job->ParentJob = Current;
        Job->RootJob = (Current->RootJob != NULL) ? Current->RootJob : Current;
        Job->NestingDepth = Current->NestingDepth + 1;
        Job->AncestorCount = Current->AncestorCount + 1;
        InsertTailList(&Current->ChildJobListHead, &Job->SiblingJobLinks);
    }

    if (NT_SUCCESS(Status))
    {
        if (Current != NULL)
        {
            ExAcquireResourceExclusiveLite(&Current->JobLock, TRUE);
            RemoveEntryList(&Process->JobLinks);
            ExReleaseResourceLite(&Current->JobLock);
        }

        ObReferenceObject(Job);

        ExAcquireResourceExclusiveLite(&Job->JobLock, TRUE);
        Process->Job = Job;
        InsertTailList(&Job->ProcessListHead, &Process->JobLinks);
        Status = PspAddProcessToJobLimits(Process, Job);
        ExReleaseResourceLite(&Job->JobLock);

        Committed = MmQueryProcessCommitCharge(Process);

        for (Joined = Job; Joined != Current; Joined = Joined->ParentJob)
        {
            ExAcquireResourceExclusiveLite(&Joined->JobLock, TRUE);
            Joined->TotalProcesses++;
            Joined->ActiveProcesses++;
            ExAcquirePushLockExclusive(&Joined->MemoryLimitsLock);
            Joined->CurrentJobMemoryUsed += Committed;
            PspRecordJobMemoryPeaks(Joined, Committed);
            ExReleasePushLockExclusive(&Joined->MemoryLimitsLock);
            PspSendJobMessageLocked(Joined,
                                    JOB_OBJECT_MSG_NEW_PROCESS,
                                    Process->UniqueProcessId,
                                    FALSE);
            ExReleaseResourceLite(&Joined->JobLock);
        }

        /* The process's reference moved to the nested job, which keeps the one above it */
        if (Current != NULL) ObDereferenceObject(Current);
    }

    ExReleaseResourceLite(&PspJobListLock);
    KeLeaveCriticalRegion();

    ExReleaseRundownProtection(&Process->RundownProtect);

    if (NT_SUCCESS(Status) && PsGetProcessWin32Process(Process) != NULL)
    {
        PspInvokeW32JobCallout(Job,
                               PsW32JobCalloutAddProcess,
                               PsGetProcessWin32Process(Process));
    }

    return Status;
}

/*
 * A new process starts in the job of the process that made it. It leaves
 * every job that lets it go, from the innermost outwards: a job with silent
 * breakaway always does, and one that allows breakaway does when the creator
 * asked to break away. Asking to break away when the creator's own job does
 * not allow it fails the creation.
 */
NTSTATUS
NTAPI
PspInheritParentJob(
    _In_ PEPROCESS Process,
    _In_opt_ PEPROCESS Parent,
    _In_ ULONG Flags)
{
    BOOLEAN Breakaway = ((Flags & PROCESS_CREATE_FLAGS_BREAKAWAY) != 0);
    NTSTATUS Status;
    PEJOB Job;

    PAGED_CODE();

    if (Parent == NULL || Parent->Job == NULL) return STATUS_SUCCESS;

    KeEnterCriticalRegion();
    ExAcquireResourceSharedLite(&PspJobListLock, TRUE);

    Job = Parent->Job;

    if (Breakaway &&
        (Job->LimitFlags & (JOB_OBJECT_LIMIT_BREAKAWAY_OK |
                            JOB_OBJECT_LIMIT_SILENT_BREAKAWAY_OK)) == 0)
    {
        ExReleaseResourceLite(&PspJobListLock);
        KeLeaveCriticalRegion();
        return STATUS_ACCESS_DENIED;
    }

    while (Job != NULL &&
           ((Job->LimitFlags & JOB_OBJECT_LIMIT_SILENT_BREAKAWAY_OK) != 0 ||
            (Breakaway && (Job->LimitFlags & JOB_OBJECT_LIMIT_BREAKAWAY_OK) != 0)))
    {
        Job = Job->ParentJob;
    }

    if (Job != NULL) ObReferenceObject(Job);

    ExReleaseResourceLite(&PspJobListLock);
    KeLeaveCriticalRegion();

    if (Job == NULL) return STATUS_SUCCESS;

    Status = PspAssignProcessToJob(Process, Job);
    ObDereferenceObject(Job);
    return Status;
}

/*
 * A process in a job is leaving it. Each job it is in, innermost first, takes
 * its account while it can still be read, stops counting it as active and
 * tells whoever watches the job. Only the first of exit and deletion does
 * this, because a process whose creation failed is deleted without exiting.
 */
VOID
NTAPI
PspExitProcessFromJob(
    _In_ PEPROCESS Process)
{
    PEJOB Job;

    PAGED_CODE();

    if (Process->Job == NULL) return;

    if ((InterlockedOr((PLONG)&Process->Flags2, PSP_JOB_ACCOUNTING_FOLDED) &
         PSP_JOB_ACCOUNTING_FOLDED) != 0)
    {
        return;
    }

    KeEnterCriticalRegion();
    ExAcquireResourceSharedLite(&PspJobListLock, TRUE);

    for (Job = Process->Job; Job != NULL; Job = Job->ParentJob)
    {
        ExAcquireResourceExclusiveLite(&Job->JobLock, TRUE);

        Job->TotalKernelTime.QuadPart += Process->Pcb.KernelTime;
        Job->TotalUserTime.QuadPart += Process->Pcb.UserTime;
        Job->TotalPageFaultCount += Process->Vm.Instance.PageFaultCount;
        Job->ReadOperationCount +=
            (ULONGLONG)Process->ReadOperationCount.QuadPart;
        Job->WriteOperationCount +=
            (ULONGLONG)Process->WriteOperationCount.QuadPart;
        Job->OtherOperationCount +=
            (ULONGLONG)Process->OtherOperationCount.QuadPart;
        Job->ReadTransferCount +=
            (ULONGLONG)Process->ReadTransferCount.QuadPart;
        Job->WriteTransferCount +=
            (ULONGLONG)Process->WriteTransferCount.QuadPart;
        Job->OtherTransferCount +=
            (ULONGLONG)Process->OtherTransferCount.QuadPart;

        if (Job->ActiveProcesses != 0) Job->ActiveProcesses--;

        PspSendJobMessageLocked(Job,
                                JOB_OBJECT_MSG_EXIT_PROCESS,
                                Process->UniqueProcessId,
                                FALSE);

        /* A job is not signalled by running out of processes, only by being terminated */
        if (Job->ActiveProcesses == 0)
        {
            PspSendJobMessageLocked(Job,
                                    JOB_OBJECT_MSG_ACTIVE_PROCESS_ZERO,
                                    NULL,
                                    FALSE);
        }

        ExReleaseResourceLite(&Job->JobLock);
    }

    ExReleaseResourceLite(&PspJobListLock);
    KeLeaveCriticalRegion();
}

/*
 * Takes the process out of its job's list. This happens when the process
 * object is destroyed, not when the process exits, because the job's account
 * of it is read until then.
 */
VOID
NTAPI
PspRemoveProcessFromJob(
    _In_ PEPROCESS Process)
{
    PEJOB Job = Process->Job;

    PAGED_CODE();

    if (Job == NULL) return;

    PspExitProcessFromJob(Process);

    KeEnterCriticalRegion();
    ExAcquireResourceExclusiveLite(&Job->JobLock, TRUE);
    RemoveEntryList(&Process->JobLinks);
    Process->Job = NULL;
    ExReleaseResourceLite(&Job->JobLock);
    KeLeaveCriticalRegion();

    ObDereferenceObject(Job);
}

/*
 * A job may be told how much its processes may commit between them. The
 * charge is kept on the job, so one process going over is refused even when
 * it is within its own limits.
 */
NTSTATUS
NTAPI
PsChargeJobCommitment(
    _In_ PEPROCESS Process,
    _In_ SIZE_T PageCount)
{
    PEJOB Job = Process->Job;
    PEJOB Walk;
    PEJOB Violated = NULL;
    ULONGLONG ProcessPages;
    ULONGLONG ProcessLimit = MAXULONGLONG;
    ULONG Message = 0;

    if (Job == NULL) return STATUS_SUCCESS;

    KeEnterCriticalRegion();

    /* Taken from the process's own job outwards, and never with a job lock held */
    for (Walk = Job; Walk != NULL; Walk = Walk->ParentJob)
        ExAcquirePushLockExclusive(&Walk->MemoryLimitsLock);

    ProcessPages = MmQueryProcessCommitCharge(Process) + PageCount;

    /* The limits are kept in bytes, the usage in pages */
    for (Walk = Job; Walk != NULL; Walk = Walk->ParentJob)
    {
        if ((Walk->LimitFlags & JOB_OBJECT_LIMIT_PROCESS_MEMORY) != 0 &&
            (Walk->ProcessMemoryLimit >> PAGE_SHIFT) < ProcessLimit)
        {
            ProcessLimit = Walk->ProcessMemoryLimit >> PAGE_SHIFT;
            Violated = Walk;
        }
    }

    if (ProcessPages > ProcessLimit)
    {
        Message = JOB_OBJECT_MSG_PROCESS_MEMORY_LIMIT;
    }
    else
    {
        Violated = NULL;
        for (Walk = Job; Walk != NULL && Violated == NULL; Walk = Walk->ParentJob)
        {
            if ((Walk->LimitFlags & JOB_OBJECT_LIMIT_JOB_MEMORY) != 0 &&
                Walk->CurrentJobMemoryUsed + PageCount > (Walk->JobMemoryLimit >> PAGE_SHIFT))
            {
                Message = JOB_OBJECT_MSG_JOB_MEMORY_LIMIT;
                Violated = Walk;
            }
        }
    }

    for (Walk = Job; Walk != NULL; Walk = Walk->ParentJob)
    {
        if (Violated == NULL)
        {
            Walk->CurrentJobMemoryUsed += PageCount;
            PspRecordJobMemoryPeaks(Walk, ProcessPages);
        }

        ExReleasePushLockExclusive(&Walk->MemoryLimitsLock);
    }

    if (Violated != NULL)
        PspSendJobChainMessage(Violated, Message, Process->UniqueProcessId);

    KeLeaveCriticalRegion();

    return (Violated != NULL) ? STATUS_COMMITMENT_LIMIT : STATUS_SUCCESS;
}

VOID
NTAPI
PsReturnJobCommitment(
    _In_ PEPROCESS Process,
    _In_ SIZE_T PageCount)
{
    PEJOB Job;

    KeEnterCriticalRegion();

    for (Job = Process->Job; Job != NULL; Job = Job->ParentJob)
    {
        ExAcquirePushLockExclusive(&Job->MemoryLimitsLock);
        if (PageCount > Job->CurrentJobMemoryUsed) Job->CurrentJobMemoryUsed = 0;
        else Job->CurrentJobMemoryUsed -= PageCount;
        ExReleasePushLockExclusive(&Job->MemoryLimitsLock);
    }

    KeLeaveCriticalRegion();
}

PEJOB
NTAPI
PsGetProcessJob(
    _In_ PEPROCESS Process)
{
    return Process->Job;
}

/*
 * The job the two processes are both in, which is the deepest job that holds
 * them both. With no nesting there is either one job or none.
 */
PVOID
NTAPI
PsGetProcessCommonJob(
    _In_ PEPROCESS FirstProcess,
    _In_ PEPROCESS SecondProcess)
{
    PEJOB First = FirstProcess->Job;
    PEJOB Second = SecondProcess->Job;

    if (First == NULL || Second == NULL) return NULL;
    if (First == Second) return First;

    /* TODO: walk the two chains of ancestors and return where they meet */
    return NULL;
}

PVOID
NTAPI
PsGetJobLock(
    _In_ PEJOB Job)
{
    return &Job->JobLock;
}

ULONG
NTAPI
PsGetJobSessionId(
    _In_ PEJOB Job)
{
    return Job->SessionId;
}

ULONG
NTAPI
PsGetJobUIRestrictionsClass(
    _In_ PEJOB Job)
{
    return Job->UIRestrictionsClass;
}

VOID
NTAPI
PsSetJobUIRestrictionsClass(
    _Inout_ PEJOB Job,
    _In_ ULONG UIRestrictionsClass)
{
    PAGED_CODE();

    KeEnterCriticalRegion();
    ExAcquireResourceExclusiveLite(&Job->JobLock, TRUE);
    Job->UIRestrictionsClass = UIRestrictionsClass;
    ExReleaseResourceLite(&Job->JobLock);
    KeLeaveCriticalRegion();

    PspInvokeW32JobCallout(Job,
                           PsW32JobCalloutSetInformation,
                           (PVOID)(ULONG_PTR)UIRestrictionsClass);
}

/* A job may restrict the desktop only while no job above or below it does */
static
NTSTATUS
PspSetJobUiRestrictions(
    _Inout_ PEJOB Job,
    _In_ ULONG UIRestrictionsClass)
{
    PAGED_CODE();

    KeEnterCriticalRegion();
    ExAcquireResourceExclusiveLite(&PspJobListLock, TRUE);

    if (UIRestrictionsClass != 0 &&
        (PspIsJobChainUiRestricted(Job->ParentJob) || PspIsJobTreeUiRestricted(Job)))
    {
        ExReleaseResourceLite(&PspJobListLock);
        KeLeaveCriticalRegion();
        return STATUS_NOT_SUPPORTED;
    }

    ExAcquireResourceExclusiveLite(&Job->JobLock, TRUE);
    Job->UIRestrictionsClass = UIRestrictionsClass;
    ExReleaseResourceLite(&Job->JobLock);

    ExReleaseResourceLite(&PspJobListLock);
    KeLeaveCriticalRegion();

    PspInvokeW32JobCallout(Job,
                           PsW32JobCalloutSetInformation,
                           (PVOID)(ULONG_PTR)UIRestrictionsClass);
    return STATUS_SUCCESS;
}

/* SYSTEM CALLS ***************************************************************/

NTSTATUS
NTAPI
NtCreateJobObject(
    _Out_ PHANDLE JobHandle,
    _In_ ACCESS_MASK DesiredAccess,
    _In_opt_ POBJECT_ATTRIBUTES ObjectAttributes)
{
    KPROCESSOR_MODE PreviousMode = ExGetPreviousMode();
    PEPROCESS Current = PsGetCurrentProcess();
    HANDLE Handle;
    NTSTATUS Status;
    PEJOB Job;

    PAGED_CODE();

    if (PreviousMode != KernelMode)
    {
        _SEH2_TRY
        {
            ProbeForWriteHandle(JobHandle);
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            _SEH2_YIELD(return _SEH2_GetExceptionCode());
        }
        _SEH2_END;
    }

    Status = ObCreateObject(PreviousMode,
                            PsJobType,
                            ObjectAttributes,
                            PreviousMode,
                            NULL,
                            sizeof(EJOB),
                            0,
                            0,
                            (PVOID*)&Job);
    if (!NT_SUCCESS(Status)) return Status;

    RtlZeroMemory(Job, sizeof(EJOB));

    InitializeListHead(&Job->ProcessListHead);
    InitializeListHead(&Job->JobLinks);
    InitializeListHead(&Job->SiblingJobLinks);
    InitializeListHead(&Job->ChildJobListHead);
    InitializeListHead(&Job->IteratorListHead);
    ExInitializePushLock(&Job->MemoryLimitsLock);
    KeInitializeEvent(&Job->Event, NotificationEvent, FALSE);
    Job->SchedulingClass = PSP_JOB_SCHEDULING_CLASS_DEFAULT;
    Job->SessionId = MmGetSessionId(Current);

    Status = ExInitializeResourceLite(&Job->JobLock);
    if (!NT_SUCCESS(Status))
    {
        ObDereferenceObject(Job);
        return Status;
    }

    KeEnterCriticalRegion();
    ExAcquireResourceExclusiveLite(&PspJobListLock, TRUE);
    InsertTailList(&PspJobList, &Job->JobLinks);
    ExReleaseResourceLite(&PspJobListLock);
    KeLeaveCriticalRegion();

    Status = ObInsertObject(Job, NULL, DesiredAccess, 0, NULL, &Handle);
    if (!NT_SUCCESS(Status)) return Status;

    if (PreviousMode != KernelMode)
    {
        _SEH2_TRY
        {
            *JobHandle = Handle;
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            Status = _SEH2_GetExceptionCode();
        }
        _SEH2_END;
    }
    else
    {
        *JobHandle = Handle;
    }

    return Status;
}

NTSTATUS
NTAPI
NtOpenJobObject(
    _Out_ PHANDLE JobHandle,
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
            ProbeForWriteHandle(JobHandle);
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            _SEH2_YIELD(return _SEH2_GetExceptionCode());
        }
        _SEH2_END;
    }

    Status = ObOpenObjectByName(ObjectAttributes,
                                PsJobType,
                                PreviousMode,
                                NULL,
                                DesiredAccess,
                                NULL,
                                &Handle);
    if (!NT_SUCCESS(Status)) return Status;

    if (PreviousMode != KernelMode)
    {
        _SEH2_TRY
        {
            *JobHandle = Handle;
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            Status = _SEH2_GetExceptionCode();
        }
        _SEH2_END;
    }
    else
    {
        *JobHandle = Handle;
    }

    return Status;
}

NTSTATUS
NTAPI
NtAssignProcessToJobObject(
    _In_ HANDLE JobHandle,
    _In_ HANDLE ProcessHandle)
{
    KPROCESSOR_MODE PreviousMode = ExGetPreviousMode();
    PEPROCESS Process;
    NTSTATUS Status;
    PEJOB Job;

    PAGED_CODE();

    Status = ObReferenceObjectByHandle(JobHandle,
                                       JOB_OBJECT_ASSIGN_PROCESS,
                                       PsJobType,
                                       PreviousMode,
                                       (PVOID*)&Job,
                                       NULL);
    if (!NT_SUCCESS(Status)) return Status;

    /*
     * Putting a process into a job changes what it may do and when it ends,
     * so it takes the right to set the process's state, not merely to name
     * the process.
     */
    Status = ObReferenceObjectByHandle(ProcessHandle,
                                       PROCESS_SET_QUOTA | PROCESS_TERMINATE,
                                       PsProcessType,
                                       PreviousMode,
                                       (PVOID*)&Process,
                                       NULL);
    if (!NT_SUCCESS(Status))
    {
        ObDereferenceObject(Job);
        return Status;
    }

    Status = PspAssignProcessToJob(Process, Job);

    ObDereferenceObject(Process);
    ObDereferenceObject(Job);

    return Status;
}

/*
 * Whether a process is in a job. A caller that names no job is asking only
 * whether the process is in any job at all.
 */
NTSTATUS
NTAPI
NtIsProcessInJob(
    _In_ HANDLE ProcessHandle,
    _In_opt_ HANDLE JobHandle)
{
    KPROCESSOR_MODE PreviousMode = ExGetPreviousMode();
    PEPROCESS Process;
    NTSTATUS Status;
    PEJOB Job = NULL;
    BOOLEAN InJob;

    PAGED_CODE();

    Status = ObReferenceObjectByHandle(ProcessHandle,
                                       PROCESS_QUERY_LIMITED_INFORMATION,
                                       PsProcessType,
                                       PreviousMode,
                                       (PVOID*)&Process,
                                       NULL);
    if (!NT_SUCCESS(Status)) return Status;

    if (JobHandle != NULL)
    {
        Status = ObReferenceObjectByHandle(JobHandle,
                                           JOB_OBJECT_QUERY,
                                           PsJobType,
                                           PreviousMode,
                                           (PVOID*)&Job,
                                           NULL);
        if (!NT_SUCCESS(Status))
        {
            ObDereferenceObject(Process);
            return Status;
        }

        KeEnterCriticalRegion();
        ExAcquireResourceSharedLite(&PspJobListLock, TRUE);
        InJob = PspIsJobWithin(Process->Job, Job);
        ExReleaseResourceLite(&PspJobListLock);
        KeLeaveCriticalRegion();

        ObDereferenceObject(Job);
    }
    else
    {
        InJob = (BOOLEAN)(Process->Job != NULL);
    }

    ObDereferenceObject(Process);

    return InJob ? STATUS_PROCESS_IN_JOB : STATUS_PROCESS_NOT_IN_JOB;
}

NTSTATUS
NTAPI
NtTerminateJobObject(
    _In_ HANDLE JobHandle,
    _In_ NTSTATUS ExitStatus)
{
    KPROCESSOR_MODE PreviousMode = ExGetPreviousMode();
    NTSTATUS Status;
    PEJOB Job;

    PAGED_CODE();

    Status = ObReferenceObjectByHandle(JobHandle,
                                       JOB_OBJECT_TERMINATE,
                                       PsJobType,
                                       PreviousMode,
                                       (PVOID*)&Job,
                                       NULL);
    if (!NT_SUCCESS(Status)) return Status;

    /* A caller ending its own job keeps the exit it queued for itself until it has let go of the job */
    KeEnterCriticalRegion();
    Status = PspTerminateJobObject(Job, PreviousMode, ExitStatus);
    ObDereferenceObject(Job);
    KeLeaveCriticalRegion();

    return Status;
}

/*
 * Several jobs limited as one. The jobs are named in order of how deeply
 * nested they are, and each is made a child of the one before it, so a limit
 * set on an outer job bounds every job inside it.
 *
 * The first entry names the level the set starts at and carries no job of
 * its own, which is how a caller says where in an existing nesting the new
 * jobs belong.
 */
NTSTATUS
NTAPI
NtCreateJobSet(
    _In_ ULONG NumJob,
    _In_ PJOB_SET_ARRAY UserJobSet,
    _In_ ULONG Flags)
{
    KPROCESSOR_MODE PreviousMode = ExGetPreviousMode();
    PJOB_SET_ARRAY Captured = NULL;
    PEJOB *Jobs = NULL;
    ULONG Index;
    ULONG Held = 0;
    NTSTATUS Status;

    PAGED_CODE();

    if (Flags != 0) return STATUS_INVALID_PARAMETER;
    if (NumJob < 2) return STATUS_INVALID_PARAMETER;
    if (NumJob > PSP_MAX_JOB_SET_MEMBERS) return STATUS_INVALID_PARAMETER;

    Captured = ExAllocatePoolWithTag(PagedPool,
                                     NumJob * sizeof(JOB_SET_ARRAY),
                                     TAG_EJOB);
    if (Captured == NULL) return STATUS_INSUFFICIENT_RESOURCES;

    if (PreviousMode != KernelMode)
    {
        _SEH2_TRY
        {
            ProbeForRead(UserJobSet,
                         NumJob * sizeof(JOB_SET_ARRAY),
                         sizeof(ULONG));
            RtlCopyMemory(Captured,
                          UserJobSet,
                          NumJob * sizeof(JOB_SET_ARRAY));
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            ExFreePoolWithTag(Captured, TAG_EJOB);
            _SEH2_YIELD(return _SEH2_GetExceptionCode());
        }
        _SEH2_END;
    }
    else
    {
        RtlCopyMemory(Captured, UserJobSet, NumJob * sizeof(JOB_SET_ARRAY));
    }

    /*
     * The first entry only says where the set begins, so it names no job,
     * and every entry after it has to be deeper than the one before.
     */
    if (Captured[0].JobHandle != NULL)
    {
        ExFreePoolWithTag(Captured, TAG_EJOB);
        return STATUS_INVALID_PARAMETER;
    }

    for (Index = 1; Index < NumJob; Index++)
    {
        if (Captured[Index].JobHandle == NULL ||
            Captured[Index].MemberLevel <= Captured[Index - 1].MemberLevel)
        {
            ExFreePoolWithTag(Captured, TAG_EJOB);
            return STATUS_INVALID_PARAMETER;
        }
    }

    Jobs = ExAllocatePoolZero(PagedPool, NumJob * sizeof(PEJOB), TAG_EJOB);
    if (Jobs == NULL)
    {
        ExFreePoolWithTag(Captured, TAG_EJOB);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    /* Every job is taken hold of before any of them is changed */
    Status = STATUS_SUCCESS;
    for (Index = 1; Index < NumJob; Index++)
    {
        Status = ObReferenceObjectByHandle(Captured[Index].JobHandle,
                                           JOB_OBJECT_ASSIGN_PROCESS,
                                           PsJobType,
                                           PreviousMode,
                                           (PVOID*)&Jobs[Index],
                                           NULL);
        if (!NT_SUCCESS(Status)) break;

        Held = Index;
    }

    if (NT_SUCCESS(Status))
    {
        KeEnterCriticalRegion();
        ExAcquireResourceExclusiveLite(&PspJobListLock, TRUE);

        /*
         * A job that already has a parent, or already holds processes, is
         * not something a new set may rearrange: the processes inside it
         * are running under the limits it has now.
         */
        for (Index = 1; Index < NumJob; Index++)
        {
            if (Jobs[Index]->ParentJob != NULL ||
                Jobs[Index]->TotalProcesses != 0)
            {
                Status = STATUS_ACCESS_DENIED;
                break;
            }
        }

        if (NT_SUCCESS(Status))
        {
            for (Index = 2; Index < NumJob; Index++)
            {
                PEJOB Parent = Jobs[Index - 1];
                PEJOB Child = Jobs[Index];

                ObReferenceObject(Parent);
                Child->ParentJob = Parent;
                Child->RootJob = (Jobs[1]->RootJob != NULL) ? Jobs[1]->RootJob
                                                            : Jobs[1];
                Child->NestingDepth = (UCHAR)Captured[Index].MemberLevel;
                Child->AncestorCount = Index - 1;
                InsertTailList(&Parent->ChildJobListHead,
                               &Child->SiblingJobLinks);
            }

            Jobs[1]->NestingDepth = (UCHAR)Captured[1].MemberLevel;
            Jobs[1]->RootJob = Jobs[1];
        }

        ExReleaseResourceLite(&PspJobListLock);
        KeLeaveCriticalRegion();
    }

    for (Index = 1; Index <= Held; Index++)
    {
        if (Jobs[Index] != NULL) ObDereferenceObject(Jobs[Index]);
    }

    ExFreePoolWithTag(Jobs, TAG_EJOB);
    ExFreePoolWithTag(Captured, TAG_EJOB);

    return Status;
}

/* Reports a running process of a job tree to the job whose port was just set */
static
NTSTATUS
NTAPI
PspReportJobMember(
    _In_ PEPROCESS Process,
    _In_opt_ PVOID Context)
{
    if (Process->AccountingFolded) return STATUS_SUCCESS;

    PspSendJobMessageLocked((PEJOB)Context,
                            JOB_OBJECT_MSG_NEW_PROCESS,
                            Process->UniqueProcessId,
                            FALSE);
    return STATUS_SUCCESS;
}

typedef struct _PSP_JOB_ID_LIST_CONTEXT
{
    PJOBOBJECT_BASIC_PROCESS_ID_LIST List;
    ULONG Room;
    ULONG Count;
} PSP_JOB_ID_LIST_CONTEXT, *PPSP_JOB_ID_LIST_CONTEXT;

/* Counts, or lists while there is room, the processes still running in a job tree */
static
NTSTATUS
NTAPI
PspListJobMember(
    _In_ PEPROCESS Process,
    _In_opt_ PVOID Context)
{
    PPSP_JOB_ID_LIST_CONTEXT IdList = Context;

    /* A process that has exited stays associated with the job but is no longer in it */
    if (Process->AccountingFolded) return STATUS_SUCCESS;

    if (IdList->List != NULL)
    {
        if (IdList->Count >= IdList->Room) return STATUS_SUCCESS;
        IdList->List->ProcessIdList[IdList->Count] = (ULONG_PTR)Process->UniqueProcessId;
    }

    IdList->Count++;
    return STATUS_SUCCESS;
}

static
NTSTATUS
PspCopyJobInformation(
    _Out_writes_bytes_(Length) PVOID Destination,
    _In_reads_bytes_(Length) PVOID JobInformation,
    _In_ ULONG Length)
{
    NTSTATUS Status = STATUS_SUCCESS;

    _SEH2_TRY
    {
        RtlCopyMemory(Destination, JobInformation, Length);
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Status = _SEH2_GetExceptionCode();
    }
    _SEH2_END;

    return Status;
}

NTSTATUS
NTAPI
NtQueryInformationJobObject(
    _In_opt_ HANDLE JobHandle,
    _In_ JOBOBJECTINFOCLASS JobInformationClass,
    _Out_writes_bytes_(JobInformationLength) PVOID JobInformation,
    _In_ ULONG JobInformationLength,
    _Out_opt_ PULONG ReturnLength)
{
    KPROCESSOR_MODE PreviousMode = ExGetPreviousMode();
    JOBOBJECT_BASIC_AND_IO_ACCOUNTING_INFORMATION BasicAndIo;
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION Extended;
    JOBOBJECT_BASIC_UI_RESTRICTIONS Restrictions;
    JOBOBJECT_END_OF_JOB_TIME_INFORMATION EndOfJob;
    JOBOBJECT_JOBSET_INFORMATION Set;
    PJOBOBJECT_BASIC_PROCESS_ID_LIST List = NULL;
    PVOID Source = NULL;
    ULONG Length = 0;
    NTSTATUS CopyStatus;
    NTSTATUS Status;
    PEJOB Job;

    PAGED_CODE();

    if (JobInformationClass < JobObjectBasicAccountingInformation ||
        JobInformationClass >= MaxJobObjectInfoClass)
    {
        return STATUS_INVALID_INFO_CLASS;
    }

    if (PreviousMode != KernelMode)
    {
        _SEH2_TRY
        {
            if (JobInformationLength != 0)
            {
                ProbeForWrite(JobInformation,
                              JobInformationLength,
                              sizeof(ULONG));
            }

            if (ReturnLength != NULL) ProbeForWriteUlong(ReturnLength);
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            _SEH2_YIELD(return _SEH2_GetExceptionCode());
        }
        _SEH2_END;
    }

    /* A caller that names no job is asking about its own process's job */
    if (JobHandle != NULL)
    {
        Status = ObReferenceObjectByHandle(JobHandle,
                                           JOB_OBJECT_QUERY,
                                           PsJobType,
                                           PreviousMode,
                                           (PVOID*)&Job,
                                           NULL);
        if (!NT_SUCCESS(Status)) return Status;
    }
    else
    {
        Job = PsGetCurrentProcess()->Job;
        if (Job == NULL) return STATUS_ACCESS_DENIED;
        ObReferenceObject(Job);
    }

    /* The hierarchy is taken first, as everywhere, because a job's answer includes the jobs nested in it */
    KeEnterCriticalRegion();
    ExAcquireResourceSharedLite(&PspJobListLock, TRUE);
    ExAcquireResourceSharedLite(&Job->JobLock, TRUE);

    RtlZeroMemory(&BasicAndIo, sizeof(BasicAndIo));
    RtlZeroMemory(&Extended, sizeof(Extended));

    switch (JobInformationClass)
    {
        case JobObjectBasicAccountingInformation:
        case JobObjectBasicAndIoAccountingInformation:
            BasicAndIo.BasicInfo.TotalUserTime = Job->TotalUserTime;
            BasicAndIo.BasicInfo.TotalKernelTime = Job->TotalKernelTime;
            BasicAndIo.BasicInfo.ThisPeriodTotalUserTime =
                Job->ThisPeriodTotalUserTime;
            BasicAndIo.BasicInfo.ThisPeriodTotalKernelTime =
                Job->ThisPeriodTotalKernelTime;
            BasicAndIo.BasicInfo.TotalPageFaultCount = Job->TotalPageFaultCount;
            BasicAndIo.BasicInfo.TotalProcesses = Job->TotalProcesses;
            BasicAndIo.BasicInfo.ActiveProcesses = Job->ActiveProcesses;
            BasicAndIo.BasicInfo.TotalTerminatedProcesses =
                Job->TotalTerminatedProcesses;

            BasicAndIo.IoInfo.ReadOperationCount = Job->ReadOperationCount;
            BasicAndIo.IoInfo.WriteOperationCount = Job->WriteOperationCount;
            BasicAndIo.IoInfo.OtherOperationCount = Job->OtherOperationCount;
            BasicAndIo.IoInfo.ReadTransferCount = Job->ReadTransferCount;
            BasicAndIo.IoInfo.WriteTransferCount = Job->WriteTransferCount;
            BasicAndIo.IoInfo.OtherTransferCount = Job->OtherTransferCount;

            Length = (JobInformationClass ==
                      JobObjectBasicAccountingInformation)
                         ? sizeof(BasicAndIo.BasicInfo)
                         : sizeof(BasicAndIo);

            if (JobInformationLength < Length)
            {
                Status = STATUS_INFO_LENGTH_MISMATCH;
                break;
            }

            Source = &BasicAndIo;
            Status = STATUS_SUCCESS;
            break;

        case JobObjectBasicLimitInformation:
        case JobObjectExtendedLimitInformation:
            Extended.BasicLimitInformation.PerProcessUserTimeLimit =
                Job->PerProcessUserTimeLimit;
            Extended.BasicLimitInformation.PerJobUserTimeLimit =
                Job->PerJobUserTimeLimit;
            Extended.BasicLimitInformation.LimitFlags = Job->LimitFlags;
            Extended.BasicLimitInformation.MinimumWorkingSetSize =
                (SIZE_T)Job->MinimumWorkingSetSize;
            Extended.BasicLimitInformation.MaximumWorkingSetSize =
                (SIZE_T)Job->MaximumWorkingSetSize;
            Extended.BasicLimitInformation.ActiveProcessLimit =
                Job->ActiveProcessLimit;
            Extended.BasicLimitInformation.Affinity = Job->Affinity.Bitmap[0];
            Extended.BasicLimitInformation.PriorityClass = Job->PriorityClass;
            Extended.BasicLimitInformation.SchedulingClass =
                Job->SchedulingClass;

            Extended.ProcessMemoryLimit = (SIZE_T)Job->ProcessMemoryLimit;
            Extended.JobMemoryLimit = (SIZE_T)Job->JobMemoryLimit;
            Extended.PeakProcessMemoryUsed = (SIZE_T)(Job->PeakProcessMemoryUsed << PAGE_SHIFT);
            Extended.PeakJobMemoryUsed = (SIZE_T)(Job->PeakJobMemoryUsed << PAGE_SHIFT);

            Length = (JobInformationClass == JobObjectBasicLimitInformation)
                         ? sizeof(Extended.BasicLimitInformation)
                         : sizeof(Extended);

            if (JobInformationLength < Length)
            {
                Status = STATUS_INFO_LENGTH_MISMATCH;
                break;
            }

            Source = &Extended;
            Status = STATUS_SUCCESS;
            break;

        case JobObjectBasicUIRestrictions:
            Restrictions.UIRestrictionsClass = Job->UIRestrictionsClass;
            Length = sizeof(Restrictions);

            if (JobInformationLength < Length)
            {
                Status = STATUS_INFO_LENGTH_MISMATCH;
                break;
            }

            Source = &Restrictions;
            Status = STATUS_SUCCESS;
            break;

        case JobObjectEndOfJobTimeInformation:
            EndOfJob.EndOfJobTimeAction = Job->EndOfJobTimeAction;
            Length = sizeof(EndOfJob);

            if (JobInformationLength < Length)
            {
                Status = STATUS_INFO_LENGTH_MISMATCH;
                break;
            }

            Source = &EndOfJob;
            Status = STATUS_SUCCESS;
            break;

        case JobObjectBasicProcessIdList:
        {
            PSP_JOB_ID_LIST_CONTEXT IdList;
            ULONG Room;

            /* The record has to hold at least one identifier */
            if (JobInformationLength < sizeof(JOBOBJECT_BASIC_PROCESS_ID_LIST))
            {
                Length = sizeof(JOBOBJECT_BASIC_PROCESS_ID_LIST);
                Status = STATUS_INFO_LENGTH_MISMATCH;
                break;
            }

            Room = (JobInformationLength -
                    (ULONG)FIELD_OFFSET(JOBOBJECT_BASIC_PROCESS_ID_LIST,
                                        ProcessIdList)) / sizeof(ULONG_PTR);

            /* The processes of the jobs nested in this one are in it too */
            RtlZeroMemory(&IdList, sizeof(IdList));
            PspEnumerateJobTree(Job, PspListJobMember, &IdList);
            if (Room > IdList.Count) Room = IdList.Count;

            List = ExAllocatePoolWithTag(PagedPool,
                                         FIELD_OFFSET(JOBOBJECT_BASIC_PROCESS_ID_LIST,
                                                      ProcessIdList) +
                                             Room * sizeof(ULONG_PTR),
                                         TAG_EJOB);
            if (List == NULL)
            {
                Status = STATUS_INSUFFICIENT_RESOURCES;
                break;
            }

            IdList.List = List;
            IdList.Room = Room;
            IdList.Count = 0;
            PspEnumerateJobTree(Job, PspListJobMember, &IdList);

            List->NumberOfAssignedProcesses = Job->ActiveProcesses;
            List->NumberOfProcessIdsInList = IdList.Count;

            Length = (ULONG)FIELD_OFFSET(JOBOBJECT_BASIC_PROCESS_ID_LIST,
                                         ProcessIdList) +
                     IdList.Count * (ULONG)sizeof(ULONG_PTR);
            Source = List;

            /*
             * A caller with room for some of the processes is given those and
             * told there were more, which is how it learns to ask again with
             * room for all of them.
             */
            Status = (IdList.Count < Job->ActiveProcesses) ? STATUS_BUFFER_OVERFLOW
                                                           : STATUS_SUCCESS;
            break;
        }

        case JobObjectJobSetInformation:
        {
            Set.MemberLevel = Job->NestingDepth;
            Length = sizeof(Set);

            if (JobInformationLength < Length)
            {
                Status = STATUS_INFO_LENGTH_MISMATCH;
                break;
            }

            Source = &Set;
            Status = STATUS_SUCCESS;
            break;
        }

        case JobObjectSecurityLimitInformation:
            /*
             * A job used to be able to cut down the token of every process
             * in it. That was taken out of the system, and a caller that
             * still asks is told so rather than being told it worked.
             */
            Status = STATUS_NOT_SUPPORTED;
            break;

        case JobObjectAssociateCompletionPortInformation:
            /*
             * The port a job reports on is write-only: handing it back
             * would let anyone who can read the job reach a port it was
             * never given.
             */
            Status = STATUS_INVALID_INFO_CLASS;
            break;

        default:
            Status = STATUS_INVALID_INFO_CLASS;
            break;
    }

    ExReleaseResourceLite(&Job->JobLock);
    ExReleaseResourceLite(&PspJobListLock);
    KeLeaveCriticalRegion();

    ObDereferenceObject(Job);

    /* The caller's buffer is written only after the job is let go, because writing it may fault */
    if (!NT_ERROR(Status) && Source != NULL)
    {
        CopyStatus = PspCopyJobInformation(JobInformation, Source, Length);
        if (!NT_SUCCESS(CopyStatus)) Status = CopyStatus;
    }

    if (List != NULL) ExFreePoolWithTag(List, TAG_EJOB);

    if (ReturnLength == NULL) return Status;

    if (PreviousMode != KernelMode)
    {
        _SEH2_TRY
        {
            *ReturnLength = Length;
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            /* The answer was written; only the length could not be */
        }
        _SEH2_END;
    }
    else
    {
        *ReturnLength = Length;
    }

    return Status;
}

static
ULONG
PspJobInformationAlignment(
    _In_ JOBOBJECTINFOCLASS JobInformationClass)
{
    switch (JobInformationClass)
    {
        case JobObjectBasicUIRestrictions:
            return TYPE_ALIGNMENT(JOBOBJECT_BASIC_UI_RESTRICTIONS);

        case JobObjectEndOfJobTimeInformation:
            return TYPE_ALIGNMENT(JOBOBJECT_END_OF_JOB_TIME_INFORMATION);

        default:
            return sizeof(ULONG_PTR);
    }
}

NTSTATUS
NTAPI
NtSetInformationJobObject(
    _In_ HANDLE JobHandle,
    _In_ JOBOBJECTINFOCLASS JobInformationClass,
    _In_reads_bytes_(JobInformationLength) PVOID JobInformation,
    _In_ ULONG JobInformationLength)
{
    KPROCESSOR_MODE PreviousMode = ExGetPreviousMode();
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION Extended;
    JOBOBJECT_ASSOCIATE_COMPLETION_PORT Port;
    JOBOBJECT_BASIC_UI_RESTRICTIONS Restrictions;
    JOBOBJECT_END_OF_JOB_TIME_INFORMATION EndOfJob;
    ULONG Valid;
    ULONG Length;
    NTSTATUS Status;
    PEJOB Job;

    PAGED_CODE();

    if (JobInformationClass < JobObjectBasicAccountingInformation ||
        JobInformationClass >= MaxJobObjectInfoClass)
    {
        return STATUS_INVALID_INFO_CLASS;
    }

    if (PreviousMode != KernelMode && JobInformationLength != 0)
    {
        _SEH2_TRY
        {
            ProbeForRead(JobInformation,
                         JobInformationLength,
                         PspJobInformationAlignment(JobInformationClass));
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            _SEH2_YIELD(return _SEH2_GetExceptionCode());
        }
        _SEH2_END;
    }

    Status = ObReferenceObjectByHandle(JobHandle,
                                       JOB_OBJECT_SET_ATTRIBUTES,
                                       PsJobType,
                                       PreviousMode,
                                       (PVOID*)&Job,
                                       NULL);
    if (!NT_SUCCESS(Status)) return Status;

    switch (JobInformationClass)
    {
        case JobObjectBasicLimitInformation:
        case JobObjectExtendedLimitInformation:
            Length = (JobInformationClass == JobObjectBasicLimitInformation)
                         ? (ULONG)sizeof(Extended.BasicLimitInformation)
                         : (ULONG)sizeof(Extended);

            if (JobInformationLength != Length)
            {
                Status = STATUS_INFO_LENGTH_MISMATCH;
                break;
            }

            RtlZeroMemory(&Extended, sizeof(Extended));
            Status = PspCopyJobInformation(&Extended, JobInformation, Length);
            if (!NT_SUCCESS(Status)) break;

            Valid = (JobInformationClass == JobObjectBasicLimitInformation)
                        ? PSP_JOB_BASIC_LIMIT_VALID_FLAGS
                        : PSP_JOB_EXTENDED_LIMIT_VALID_FLAGS;

            if ((Extended.BasicLimitInformation.LimitFlags & ~Valid) != 0)
            {
                Status = STATUS_INVALID_PARAMETER;
                break;
            }

            /* A field is only looked at when its limit is asked for */
            if ((Extended.BasicLimitInformation.LimitFlags &
                 JOB_OBJECT_LIMIT_SCHEDULING_CLASS) != 0 &&
                Extended.BasicLimitInformation.SchedulingClass >=
                PSP_JOB_SCHEDULING_CLASSES)
            {
                Status = STATUS_INVALID_PARAMETER;
                break;
            }

            if ((Extended.BasicLimitInformation.LimitFlags &
                 JOB_OBJECT_LIMIT_AFFINITY) != 0 &&
                (Extended.BasicLimitInformation.Affinity == 0 ||
                 (Extended.BasicLimitInformation.Affinity &
                  ~(ULONG_PTR)KeActiveProcessors) != 0))
            {
                Status = STATUS_INVALID_PARAMETER;
                break;
            }

            /*
             * Asking for the real-time class for a whole job needs the
             * privilege to raise priority, as it does for one process.
             */
            if ((Extended.BasicLimitInformation.LimitFlags &
                 JOB_OBJECT_LIMIT_PRIORITY_CLASS) != 0 &&
                Extended.BasicLimitInformation.PriorityClass ==
                    PROCESS_PRIORITY_CLASS_REALTIME &&
                !SeSinglePrivilegeCheck(SeIncreaseBasePriorityPrivilege,
                                        PreviousMode))
            {
                Status = STATUS_PRIVILEGE_NOT_HELD;
                break;
            }

            KeEnterCriticalRegion();
            ExAcquireResourceExclusiveLite(&Job->JobLock, TRUE);
            ExAcquirePushLockExclusive(&Job->MemoryLimitsLock);

            Job->LimitFlags = Extended.BasicLimitInformation.LimitFlags;
            /* TODO: the user time limits are recorded but not enforced, so no process is terminated for one and TotalTerminatedProcesses stays 0 */
            Job->PerProcessUserTimeLimit =
                Extended.BasicLimitInformation.PerProcessUserTimeLimit;
            Job->PerJobUserTimeLimit =
                Extended.BasicLimitInformation.PerJobUserTimeLimit;
            Job->MinimumWorkingSetSize =
                Extended.BasicLimitInformation.MinimumWorkingSetSize;
            Job->MaximumWorkingSetSize =
                Extended.BasicLimitInformation.MaximumWorkingSetSize;
            Job->ActiveProcessLimit =
                Extended.BasicLimitInformation.ActiveProcessLimit;
            Job->Affinity.Bitmap[0] =
                (KAFFINITY)Extended.BasicLimitInformation.Affinity;
            Job->PriorityClass =
                (UCHAR)Extended.BasicLimitInformation.PriorityClass;
            Job->SchedulingClass =
                Extended.BasicLimitInformation.SchedulingClass;

            if (JobInformationClass == JobObjectExtendedLimitInformation)
            {
                Job->ProcessMemoryLimit = Extended.ProcessMemoryLimit;
                Job->JobMemoryLimit = Extended.JobMemoryLimit;
            }

            ExReleasePushLockExclusive(&Job->MemoryLimitsLock);

            /* A limit that is set now reaches the processes already in */
            {
                PLIST_ENTRY Entry;

                for (Entry = Job->ProcessListHead.Flink;
                     Entry != &Job->ProcessListHead;
                     Entry = Entry->Flink)
                {
                    PspAddProcessToJobLimits(
                        CONTAINING_RECORD(Entry, EPROCESS, JobLinks),
                        Job);
                }
            }

            ExReleaseResourceLite(&Job->JobLock);
            KeLeaveCriticalRegion();

            Status = STATUS_SUCCESS;
            break;

        case JobObjectBasicUIRestrictions:
            if (JobInformationLength != sizeof(Restrictions))
            {
                Status = STATUS_INFO_LENGTH_MISMATCH;
                break;
            }

            Status = PspCopyJobInformation(&Restrictions,
                                           JobInformation,
                                           sizeof(Restrictions));
            if (!NT_SUCCESS(Status)) break;

            if ((Restrictions.UIRestrictionsClass &
                 ~JOB_OBJECT_UILIMIT_ALL) != 0)
            {
                Status = STATUS_INVALID_PARAMETER;
                break;
            }

            Status = PspSetJobUiRestrictions(Job, Restrictions.UIRestrictionsClass);
            break;

        case JobObjectEndOfJobTimeInformation:
            if (JobInformationLength != sizeof(EndOfJob))
            {
                Status = STATUS_INFO_LENGTH_MISMATCH;
                break;
            }

            Status = PspCopyJobInformation(&EndOfJob,
                                           JobInformation,
                                           sizeof(EndOfJob));
            if (!NT_SUCCESS(Status)) break;

            if (EndOfJob.EndOfJobTimeAction > JOB_OBJECT_POST_AT_END_OF_JOB)
            {
                Status = STATUS_INVALID_PARAMETER;
                break;
            }

            Job->EndOfJobTimeAction = EndOfJob.EndOfJobTimeAction;
            Status = STATUS_SUCCESS;
            break;

        case JobObjectAssociateCompletionPortInformation:
            if (JobInformationLength != sizeof(Port))
            {
                Status = STATUS_INFO_LENGTH_MISMATCH;
                break;
            }

            Status = PspCopyJobInformation(&Port,
                                           JobInformation,
                                           sizeof(Port));
            if (!NT_SUCCESS(Status)) break;

            /* A job is given one port for the rest of its life */
            if (Job->CompletionPort != NULL)
            {
                Status = STATUS_INVALID_PARAMETER;
                break;
            }

            {
                PVOID Completion;

                Status = ObReferenceObjectByHandle(Port.CompletionPort,
                                                   IO_COMPLETION_MODIFY_STATE,
                                                   IoCompletionType,
                                                   PreviousMode,
                                                   &Completion,
                                                   NULL);
                if (!NT_SUCCESS(Status)) break;

                KeEnterCriticalRegion();
                ExAcquireResourceSharedLite(&PspJobListLock, TRUE);
                ExAcquireResourceExclusiveLite(&Job->JobLock, TRUE);

                if (Job->CompletionPort == NULL)
                {
                    Job->CompletionPort = Completion;
                    Job->CompletionKey = Port.CompletionKey;
                    Completion = NULL;

                    /* The processes already in the job, and in the jobs nested in it, are reported as if they had just joined */
                    PspEnumerateJobTree(Job, PspReportJobMember, Job);
                }
                else
                {
                    Status = STATUS_INVALID_PARAMETER;
                }

                ExReleaseResourceLite(&Job->JobLock);
                ExReleaseResourceLite(&PspJobListLock);
                KeLeaveCriticalRegion();

                if (Completion != NULL) ObDereferenceObject(Completion);
            }
            break;

        case JobObjectSecurityLimitInformation:
            /* Taken out of the system; see the query side */
            Status = STATUS_NOT_SUPPORTED;
            break;

        case JobObjectJobSetInformation:
            /*
             * A job's place in a nesting is decided when the set is made,
             * not afterwards: moving a job that already holds processes
             * would change the limits they are running under.
             */
            Status = STATUS_INVALID_INFO_CLASS;
            break;

        case JobObjectBasicAccountingInformation:
        case JobObjectBasicProcessIdList:
        case JobObjectBasicAndIoAccountingInformation:
            Status = STATUS_INVALID_INFO_CLASS;
            break;

        default:
            Status = STATUS_INVALID_INFO_CLASS;
            break;
    }

    ObDereferenceObject(Job);
    return Status;
}
