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
        JOB_OBJECT_SET_ATTRIBUTES | JOB_OBJECT_TERMINATE |
        JOB_OBJECT_SET_SECURITY_ATTRIBUTES,
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
 * Calls back for every process in the job, with the job held so that the
 * list cannot change underneath. A callback that fails stops the walk,
 * because a caller that cannot handle one process cannot handle the rest.
 */
NTSTATUS
NTAPI
PspEnumerateProcessesInJob(
    _In_ PEJOB Job,
    _In_ PJOB_ENUMERATOR_CALLBACK Callback,
    _In_opt_ PVOID Context)
{
    NTSTATUS Status = STATUS_SUCCESS;
    PLIST_ENTRY Entry;

    PAGED_CODE();

    KeEnterCriticalRegion();
    ExAcquireResourceSharedLite(&Job->JobLock, TRUE);

    for (Entry = Job->ProcessListHead.Flink;
         Entry != &Job->ProcessListHead;
         Entry = Entry->Flink)
    {
        PEPROCESS Process = CONTAINING_RECORD(Entry, EPROCESS, JobLinks);

        Status = Callback(Process, Context);
        if (!NT_SUCCESS(Status)) break;
    }

    ExReleaseResourceLite(&Job->JobLock);
    KeLeaveCriticalRegion();

    return Status;
}

static
NTSTATUS
NTAPI
PspTerminateJobMember(
    _In_ PEPROCESS Process,
    _In_opt_ PVOID Context)
{
    NTSTATUS ExitStatus = (NTSTATUS)(ULONG_PTR)Context;

    PspTerminateProcess(Process, ExitStatus);
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

    /* TODO: apply the working-set and memory limits through the memory
     * manager, and the per-process time limit through a timer */
    return STATUS_SUCCESS;
}

/* PUBLIC FUNCTIONS ***********************************************************/

CODE_SEG("INIT")
VOID
NTAPI
PspInitializeJobStructures(VOID)
{
    OBJECT_TYPE_INITIALIZER Initializer;
    UNICODE_STRING Name = RTL_CONSTANT_STRING(L"Job");

    PAGED_CODE();

    InitializeListHead(&PspJobList);
    ExInitializeResourceLite(&PspJobListLock);

    RtlZeroMemory(&Initializer, sizeof(Initializer));
    Initializer.Length = sizeof(Initializer);
    Initializer.InvalidAttributes = OBJ_OPENLINK;
    Initializer.GenericMapping = PspJobMapping;
    Initializer.PoolType = NonPagedPool;
    Initializer.ValidAccessMask = JOB_OBJECT_ALL_ACCESS;
    Initializer.DefaultNonPagedPoolCharge = sizeof(EJOB);
    Initializer.MaintainHandleCount = TRUE;
    Initializer.SecurityRequired = TRUE;
    Initializer.CloseProcedure = PspCloseJob;
    Initializer.DeleteProcedure = PspDeleteJob;
    Initializer.SecurityProcedure = SeDefaultObjectMethod;

    ObCreateObjectType(&Name, &Initializer, NULL, &PsJobType);
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

    PspInvokeW32JobCallout(Job, PsW32JobCalloutTerminate, NULL);

    if ((Job->LimitFlags & JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE) != 0)
        PspTerminateJobObject(Job, KernelMode, STATUS_SUCCESS);
}

VOID
NTAPI
PspDeleteJob(
    _In_ PVOID ObjectBody)
{
    PEJOB Job = ObjectBody;

    PAGED_CODE();

    KeEnterCriticalRegion();
    ExAcquireResourceExclusiveLite(&PspJobListLock, TRUE);
    if (Job->JobLinks.Flink != NULL) RemoveEntryList(&Job->JobLinks);
    ExReleaseResourceLite(&PspJobListLock);
    KeLeaveCriticalRegion();

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
    PAGED_CODE();

    UNREFERENCED_PARAMETER(AccessMode);

    KeEnterCriticalRegion();
    ExAcquireResourceExclusiveLite(&Job->JobLock, TRUE);
    Job->Terminating = TRUE;
    ExReleaseResourceLite(&Job->JobLock);
    KeLeaveCriticalRegion();

    PspEnumerateProcessesInJob(Job,
                               PspTerminateJobMember,
                               (PVOID)(ULONG_PTR)ExitStatus);

    KeEnterCriticalRegion();
    ExAcquireResourceExclusiveLite(&Job->JobLock, TRUE);
    Job->Terminating = FALSE;
    ExReleaseResourceLite(&Job->JobLock);
    KeLeaveCriticalRegion();

    return STATUS_SUCCESS;
}

/*
 * Puts a process into a job. A process belongs to one job for the rest of
 * its life: it may be put into a job once, and never taken out.
 */
NTSTATUS
NTAPI
PspAssignProcessToJob(
    _In_ PEPROCESS Process,
    _In_ PEJOB Job)
{
    NTSTATUS Status;

    PAGED_CODE();

    if (!ExAcquireRundownProtection(&Process->RundownProtect))
        return STATUS_PROCESS_IS_TERMINATING;

    KeEnterCriticalRegion();
    ExAcquireResourceExclusiveLite(&Job->JobLock, TRUE);

    if (Process->Job != NULL)
    {
        ExReleaseResourceLite(&Job->JobLock);
        KeLeaveCriticalRegion();
        ExReleaseRundownProtection(&Process->RundownProtect);

        /* A process belongs to one job for the rest of its life */
        return STATUS_ACCESS_DENIED;
    }

    if (Job->CloseDone || Job->DisallowNewProcesses)
    {
        ExReleaseResourceLite(&Job->JobLock);
        KeLeaveCriticalRegion();
        ExReleaseRundownProtection(&Process->RundownProtect);
        return STATUS_INVALID_PARAMETER;
    }

    /*
     * A job may be told how many processes it will hold at once, and a
     * process that would be one too many is refused.
     */
    if ((Job->LimitFlags & JOB_OBJECT_LIMIT_ACTIVE_PROCESS) != 0 &&
        Job->ActiveProcesses >= Job->ActiveProcessLimit)
    {
        ExReleaseResourceLite(&Job->JobLock);
        KeLeaveCriticalRegion();
        ExReleaseRundownProtection(&Process->RundownProtect);
        return STATUS_QUOTA_EXCEEDED;
    }

    ObReferenceObject(Job);
    Process->Job = Job;
    InsertTailList(&Job->ProcessListHead, &Process->JobLinks);
    Job->TotalProcesses++;
    Job->ActiveProcesses++;

    Status = PspAddProcessToJobLimits(Process, Job);

    ExReleaseResourceLite(&Job->JobLock);
    KeLeaveCriticalRegion();

    ExReleaseRundownProtection(&Process->RundownProtect);

    PspInvokeW32JobCallout(Job, PsW32JobCalloutAddProcess, Process);

    return Status;
}

/*
 * A process in a job is exiting. The job's account is brought up to date
 * while the process can still be read, and whoever watches the job is told.
 */
VOID
NTAPI
PspExitProcessFromJob(
    _In_ PEPROCESS Process)
{
    PEJOB Job = Process->Job;

    PAGED_CODE();

    if (Job == NULL) return;

    KeEnterCriticalRegion();
    ExAcquireResourceExclusiveLite(&Job->JobLock, TRUE);

    if (!Process->AccountingFolded)
    {
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
        Process->AccountingFolded = TRUE;
    }

    if (Job->ActiveProcesses != 0) Job->ActiveProcesses--;
    Job->TotalTerminatedProcesses++;

    ExReleaseResourceLite(&Job->JobLock);
    KeLeaveCriticalRegion();

    PspNotifyJobProcessExit(Process);
}

VOID
NTAPI
PspNotifyJobProcessExit(
    _In_ PEPROCESS Process)
{
    PEJOB Job = Process->Job;

    PAGED_CODE();

    if (Job == NULL) return;

    KeEnterCriticalRegion();
    ExAcquireResourceSharedLite(&Job->JobLock, TRUE);

    PspSendJobMessageLocked(Job,
                            JOB_OBJECT_MSG_EXIT_PROCESS,
                            Process->UniqueProcessId,
                            FALSE);

    /*
     * A job that just lost its last process is a job that is done, and
     * whoever is waiting on it is let go.
     */
    if (Job->ActiveProcesses == 0)
    {
        PspSendJobMessageLocked(Job,
                                JOB_OBJECT_MSG_ACTIVE_PROCESS_ZERO,
                                NULL,
                                FALSE);
        KeSetEvent(&Job->Event, IO_NO_INCREMENT, FALSE);
    }

    ExReleaseResourceLite(&Job->JobLock);
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
    ULONGLONG New;

    if (Job == NULL) return STATUS_SUCCESS;

    KeEnterCriticalRegion();
    ExAcquireResourceExclusiveLite(&Job->JobLock, TRUE);

    New = Job->CurrentJobMemoryUsed + PageCount;

    if ((Job->LimitFlags & JOB_OBJECT_LIMIT_JOB_MEMORY) != 0 &&
        New > Job->JobMemoryLimit)
    {
        PspSendJobMessageLocked(Job,
                                JOB_OBJECT_MSG_JOB_MEMORY_LIMIT,
                                NULL,
                                TRUE);
        ExReleaseResourceLite(&Job->JobLock);
        KeLeaveCriticalRegion();
        return STATUS_QUOTA_EXCEEDED;
    }

    Job->CurrentJobMemoryUsed = New;
    if (New > Job->PeakJobMemoryUsed) Job->PeakJobMemoryUsed = New;

    ExReleaseResourceLite(&Job->JobLock);
    KeLeaveCriticalRegion();

    return STATUS_SUCCESS;
}

VOID
NTAPI
PsReturnJobCommitment(
    _In_ PEPROCESS Process,
    _In_ SIZE_T PageCount)
{
    PEJOB Job = Process->Job;

    if (Job == NULL) return;

    KeEnterCriticalRegion();
    ExAcquireResourceExclusiveLite(&Job->JobLock, TRUE);

    if (PageCount > Job->CurrentJobMemoryUsed) Job->CurrentJobMemoryUsed = 0;
    else Job->CurrentJobMemoryUsed -= PageCount;

    ExReleaseResourceLite(&Job->JobLock);
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

    PspInvokeW32JobCallout(Job, PsW32JobCalloutSetInformation, NULL);
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

        InJob = (BOOLEAN)(Process->Job == Job);
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

    Status = PspTerminateJobObject(Job, PreviousMode, ExitStatus);
    ObDereferenceObject(Job);

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
    ULONG Length = 0;
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

    KeEnterCriticalRegion();
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

            RtlCopyMemory(JobInformation, &BasicAndIo, Length);
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
            Extended.PeakProcessMemoryUsed = (SIZE_T)Job->PeakProcessMemoryUsed;
            Extended.PeakJobMemoryUsed = (SIZE_T)Job->PeakJobMemoryUsed;

            Length = (JobInformationClass == JobObjectBasicLimitInformation)
                         ? sizeof(Extended.BasicLimitInformation)
                         : sizeof(Extended);

            if (JobInformationLength < Length)
            {
                Status = STATUS_INFO_LENGTH_MISMATCH;
                break;
            }

            RtlCopyMemory(JobInformation, &Extended, Length);
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

            RtlCopyMemory(JobInformation, &Restrictions, Length);
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

            RtlCopyMemory(JobInformation, &EndOfJob, Length);
            Status = STATUS_SUCCESS;
            break;

        case JobObjectBasicProcessIdList:
        {
            PJOBOBJECT_BASIC_PROCESS_ID_LIST List = JobInformation;
            PLIST_ENTRY Entry;
            ULONG Room;
            ULONG Count = 0;

            if (JobInformationLength <
                FIELD_OFFSET(JOBOBJECT_BASIC_PROCESS_ID_LIST, ProcessIdList))
            {
                Length = sizeof(JOBOBJECT_BASIC_PROCESS_ID_LIST);
                Status = STATUS_INFO_LENGTH_MISMATCH;
                break;
            }

            Room = (JobInformationLength -
                    (ULONG)FIELD_OFFSET(JOBOBJECT_BASIC_PROCESS_ID_LIST,
                                        ProcessIdList)) / sizeof(ULONG_PTR);

            for (Entry = Job->ProcessListHead.Flink;
                 Entry != &Job->ProcessListHead && Count < Room;
                 Entry = Entry->Flink)
            {
                PEPROCESS Member = CONTAINING_RECORD(Entry,
                                                     EPROCESS,
                                                     JobLinks);

                List->ProcessIdList[Count] =
                    (ULONG_PTR)Member->UniqueProcessId;
                Count++;
            }

            List->NumberOfAssignedProcesses = Job->ActiveProcesses;
            List->NumberOfProcessIdsInList = Count;

            Length = (ULONG)FIELD_OFFSET(JOBOBJECT_BASIC_PROCESS_ID_LIST,
                                         ProcessIdList) +
                     Count * (ULONG)sizeof(ULONG_PTR);

            /*
             * A caller with room for some of the processes is given those and
             * told there were more, which is how it learns to ask again with
             * room for all of them.
             */
            Status = (Count < Job->ActiveProcesses) ? STATUS_BUFFER_OVERFLOW
                                                    : STATUS_SUCCESS;
            break;
        }

        case JobObjectJobSetInformation:
        {
            JOBOBJECT_JOBSET_INFORMATION Set;

            Set.MemberLevel = Job->NestingDepth;
            Length = sizeof(Set);

            if (JobInformationLength < Length)
            {
                Status = STATUS_INFO_LENGTH_MISMATCH;
                break;
            }

            RtlCopyMemory(JobInformation, &Set, Length);
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
    KeLeaveCriticalRegion();

    ObDereferenceObject(Job);

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
                         sizeof(ULONG_PTR));
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
            RtlCopyMemory(&Extended, JobInformation, Length);

            Valid = (JobInformationClass == JobObjectBasicLimitInformation)
                        ? PSP_JOB_BASIC_LIMIT_VALID_FLAGS
                        : PSP_JOB_EXTENDED_LIMIT_VALID_FLAGS;

            if ((Extended.BasicLimitInformation.LimitFlags & ~Valid) != 0)
            {
                Status = STATUS_INVALID_PARAMETER;
                break;
            }

            if (Extended.BasicLimitInformation.SchedulingClass >=
                PSP_JOB_SCHEDULING_CLASSES)
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

            Job->LimitFlags = Extended.BasicLimitInformation.LimitFlags;
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

            Restrictions = *(PJOBOBJECT_BASIC_UI_RESTRICTIONS)JobInformation;
            PsSetJobUIRestrictionsClass(Job,
                                        Restrictions.UIRestrictionsClass);
            Status = STATUS_SUCCESS;
            break;

        case JobObjectEndOfJobTimeInformation:
            if (JobInformationLength != sizeof(EndOfJob))
            {
                Status = STATUS_INFO_LENGTH_MISMATCH;
                break;
            }

            EndOfJob = *(PJOBOBJECT_END_OF_JOB_TIME_INFORMATION)JobInformation;

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

            Port = *(PJOBOBJECT_ASSOCIATE_COMPLETION_PORT)JobInformation;

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
                ExAcquireResourceExclusiveLite(&Job->JobLock, TRUE);

                if (Job->CompletionPort == NULL)
                {
                    Job->CompletionPort = Completion;
                    Job->CompletionKey = Port.CompletionKey;
                    Completion = NULL;
                }
                else
                {
                    Status = STATUS_INVALID_PARAMETER;
                }

                ExReleaseResourceLite(&Job->JobLock);
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
