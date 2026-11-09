/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     What the kernel tells a debugger about what it is watching
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

/* PRIVATE FUNCTIONS **********************************************************/

/*
 * A process is held still while it is being reported on, so that what the
 * debugger is told about it is still true when it reads it.
 */
BOOLEAN
NTAPI
DbgkpSuspendProcess(VOID)
{
    PAGED_CODE();

    /* A process on its way out is already as still as it will ever be */
    if (PsGetCurrentProcess()->ProcessDelete) return FALSE;

    PsSuspendProcess(PsGetCurrentProcess());
    return TRUE;
}

VOID
NTAPI
DbgkpResumeProcess(VOID)
{
    PAGED_CODE();

    PsResumeProcess(PsGetCurrentProcess());
}

/* The file a mapping was made from, as something the debugger can open */
HANDLE
NTAPI
DbgkpSectionToFileHandle(
    _In_ PVOID Section)
{
    OBJECT_ATTRIBUTES ObjectAttributes;
    IO_STATUS_BLOCK IoStatusBlock;
    POBJECT_NAME_INFORMATION Name = NULL;
    HANDLE Handle;
    NTSTATUS Status;

    PAGED_CODE();

    Status = MmGetFileNameForSection(Section, &Name);
    if (!NT_SUCCESS(Status)) return NULL;

    InitializeObjectAttributes(&ObjectAttributes,
                               &Name->Name,
                               OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE,
                               NULL,
                               NULL);

    Status = ZwOpenFile(&Handle,
                        GENERIC_READ | SYNCHRONIZE,
                        &ObjectAttributes,
                        &IoStatusBlock,
                        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                        FILE_SYNCHRONOUS_IO_NONALERT);

    ExFreePool(Name);

    return NT_SUCCESS(Status) ? Handle : NULL;
}

/*
 * Reports one thing to whoever is watching this process and waits to be told
 * what to do about it.
 */
NTSTATUS
NTAPI
DbgkpSendApiMessage(
    _Inout_ PDBGKM_MSG ApiMsg,
    _In_ BOOLEAN SuspendProcess)
{
    BOOLEAN Suspended = FALSE;
    NTSTATUS Status;

    PAGED_CODE();

    if (SuspendProcess) Suspended = DbgkpSuspendProcess();

    ApiMsg->ReturnedStatus = STATUS_PENDING;

    Status = DbgkpQueueMessage(PsGetCurrentProcess(),
                               PsGetCurrentThread(),
                               ApiMsg,
                               0,
                               NULL);

    if (Suspended) DbgkpResumeProcess();

    return Status;
}

/* The flag a process's own code reads to know it is being watched */
VOID
NTAPI
DbgkpMarkProcessPeb(
    _In_ PEPROCESS Process)
{
    KAPC_STATE ApcState;

    PAGED_CODE();

    if (Process->Peb == NULL) return;

    KeStackAttachProcess(&Process->Pcb, &ApcState);

    _SEH2_TRY
    {
        Process->Peb->BeingDebugged =
            (BOOLEAN)(Process->DebugPort != NULL);
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        /* A process whose own memory is gone cannot be told anything */
    }
    _SEH2_END;

    KeUnstackDetachProcess(&ApcState);
}

/*
 * Catches a debugger up on one thread that already exists, as though it had
 * just been created.
 */
NTSTATUS
NTAPI
DbgkpPostFakeThreadMessages(
    _In_ PEPROCESS Process,
    _In_opt_ PDEBUG_OBJECT DebugObject,
    _In_opt_ PETHREAD StartThread,
    _Out_ PETHREAD *FirstThread,
    _Out_ PETHREAD *LastThread)
{
    PETHREAD Thread = NULL;
    PETHREAD First = NULL;
    PETHREAD Last = NULL;
    DBGKM_MSG ApiMsg;
    BOOLEAN FirstOne = TRUE;
    NTSTATUS Status = STATUS_UNSUCCESSFUL;

    PAGED_CODE();

    *FirstThread = NULL;
    *LastThread = NULL;

    if (StartThread != NULL)
    {
        First = StartThread;
        FirstOne = FALSE;
        ObReferenceObject(StartThread);
    }

    while ((Thread = PsGetNextProcessThread(Process, Thread)) != NULL)
    {
        if (Last != NULL) ObDereferenceObject(Last);

        Last = Thread;
        ObReferenceObject(Last);

        RtlZeroMemory(&ApiMsg, sizeof(ApiMsg));

        if (FirstOne)
        {
            /* The first thread stands for the process itself coming to be */
            First = Thread;
            ObReferenceObject(First);
            FirstOne = FALSE;

            ApiMsg.ApiNumber = DbgKmCreateProcessApi;
            ApiMsg.CreateProcess.InitialThread.StartAddress = NULL;
            ApiMsg.CreateProcess.BaseOfImage = Process->SectionBaseAddress;

            if (Process->SectionObject != NULL)
            {
                ApiMsg.CreateProcess.FileHandle =
                    DbgkpSectionToFileHandle(Process->SectionObject);
            }
        }
        else
        {
            ApiMsg.ApiNumber = DbgKmCreateThreadApi;
            ApiMsg.CreateThread.StartAddress = Thread->StartAddress;
        }

        Status = DbgkpQueueMessage(Process,
                                   Thread,
                                   &ApiMsg,
                                   DEBUG_EVENT_NOWAIT | DEBUG_EVENT_RELEASE,
                                   DebugObject);
        if (!NT_SUCCESS(Status))
        {
            ObDereferenceObject(Thread);
            break;
        }
    }

    if (!NT_SUCCESS(Status))
    {
        if (First != NULL) ObDereferenceObject(First);
        if (Last != NULL) ObDereferenceObject(Last);
        return Status;
    }

    *FirstThread = First;
    *LastThread = Last;

    return (First != NULL) ? STATUS_SUCCESS : STATUS_UNSUCCESSFUL;
}

/*
 * Catches a debugger up on everything the process has already mapped, read
 * out of the process's own list of what it loaded.
 */
VOID
NTAPI
DbgkpPostFakeModuleMessages(
    _In_ PEPROCESS Process,
    _In_ PETHREAD Thread,
    _In_ PDEBUG_OBJECT DebugObject)
{
    PPEB_LDR_DATA Loader;
    PLIST_ENTRY Entry;
    PLIST_ENTRY Head;
    PLDR_DATA_TABLE_ENTRY Module;
    KAPC_STATE ApcState;
    DBGKM_MSG ApiMsg;
    PVOID Base;
    ULONG Counted = 0;

    PAGED_CODE();

    if (Process->Peb == NULL) return;

    KeStackAttachProcess(&Process->Pcb, &ApcState);

    _SEH2_TRY
    {
        Loader = Process->Peb->Ldr;
        Head = &Loader->InLoadOrderModuleList;

        for (Entry = Head->Flink; Entry != Head; Entry = Entry->Flink)
        {
            Module = CONTAINING_RECORD(Entry,
                                       LDR_DATA_TABLE_ENTRY,
                                       InLoadOrderLinks);
            Base = Module->DllBase;

            /* The image the process runs was already told as its own coming
             * to be, so only what it loaded besides is told here */
            if (Counted++ == 0) continue;

            RtlZeroMemory(&ApiMsg, sizeof(ApiMsg));
            ApiMsg.ApiNumber = DbgKmLoadDllApi;
            ApiMsg.LoadDll.BaseOfDll = Base;
            ApiMsg.LoadDll.NamePointer = NULL;

            KeUnstackDetachProcess(&ApcState);

            DbgkpQueueMessage(Process,
                              Thread,
                              &ApiMsg,
                              DEBUG_EVENT_NOWAIT | DEBUG_EVENT_RELEASE,
                              DebugObject);

            KeStackAttachProcess(&Process->Pcb, &ApcState);
        }
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        /* TODO: a process whose loader list cannot be read is told as bare */
    }
    _SEH2_END;

    KeUnstackDetachProcess(&ApcState);
}

NTSTATUS
NTAPI
DbgkpPostFakeProcessCreateMessages(
    _In_ PEPROCESS Process,
    _In_ PDEBUG_OBJECT DebugObject,
    _Out_ PETHREAD *LastThread)
{
    PETHREAD First = NULL;
    PETHREAD Last = NULL;
    NTSTATUS Status;

    PAGED_CODE();

    *LastThread = NULL;

    Status = DbgkpPostFakeThreadMessages(Process,
                                         DebugObject,
                                         NULL,
                                         &First,
                                         &Last);
    if (!NT_SUCCESS(Status)) return Status;

    DbgkpPostFakeModuleMessages(Process, First, DebugObject);

    ObDereferenceObject(First);

    *LastThread = Last;
    return STATUS_SUCCESS;
}

/*
 * Hands a process over to a debugger and catches that debugger up on
 * everything the process already is.
 */
NTSTATUS
NTAPI
DbgkpSetProcessDebugObject(
    _In_ PEPROCESS Process,
    _In_ PDEBUG_OBJECT DebugObject,
    _In_ NTSTATUS MsgStatus,
    _In_opt_ PETHREAD LastThread)
{
    PETHREAD Last = LastThread;
    NTSTATUS Status;

    PAGED_CODE();

    if (!NT_SUCCESS(MsgStatus))
    {
        if (Last != NULL) ObDereferenceObject(Last);
        return MsgStatus;
    }

    ExAcquireFastMutex(&DbgkpProcessDebugPortMutex);

    if (Process->DebugPort != NULL)
    {
        ExReleaseFastMutex(&DbgkpProcessDebugPortMutex);
        if (Last != NULL) ObDereferenceObject(Last);
        return STATUS_PORT_ALREADY_SET;
    }

    ObReferenceObject(DebugObject);
    Process->DebugPort = DebugObject;

    ExReleaseFastMutex(&DbgkpProcessDebugPortMutex);

    DbgkpMarkProcessPeb(Process);

    /* A thread that came to be while the catching up was going on is told
     * about now, so that nothing is missed and nothing is told twice */
    if (Last != NULL)
    {
        PETHREAD First = NULL;
        PETHREAD Newest = NULL;

        Status = DbgkpPostFakeThreadMessages(Process,
                                             DebugObject,
                                             Last,
                                             &First,
                                             &Newest);
        if (NT_SUCCESS(Status))
        {
            if (First != NULL) ObDereferenceObject(First);
            if (Newest != NULL) ObDereferenceObject(Newest);
        }

        ObDereferenceObject(Last);
    }

    return STATUS_SUCCESS;
}

/* PUBLIC FUNCTIONS ***********************************************************/

VOID
NTAPI
DbgkCreateThread(
    _In_ PETHREAD Thread,
    _In_ PVOID StartAddress)
{
    PEPROCESS Process = PsGetThreadProcess(Thread);
    DBGKM_MSG ApiMsg;

    PAGED_CODE();

    if (PsGetCurrentProcess()->DebugPort == NULL) return;

    RtlZeroMemory(&ApiMsg, sizeof(ApiMsg));

    /* The first thread of a process is the process coming to be */
    if (Process->Peb != NULL &&
        PsGetCurrentProcess()->DebugPort != NULL &&
        Thread == PsGetNextProcessThread(Process, NULL))
    {
        ApiMsg.ApiNumber = DbgKmCreateProcessApi;
        ApiMsg.CreateProcess.InitialThread.StartAddress = StartAddress;
        ApiMsg.CreateProcess.BaseOfImage = Process->SectionBaseAddress;

        if (Process->SectionObject != NULL)
        {
            ApiMsg.CreateProcess.FileHandle =
                DbgkpSectionToFileHandle(Process->SectionObject);
        }
    }
    else
    {
        ApiMsg.ApiNumber = DbgKmCreateThreadApi;
        ApiMsg.CreateThread.StartAddress = StartAddress;
    }

    DbgkpSendApiMessage(&ApiMsg, TRUE);

    if (ApiMsg.ApiNumber == DbgKmCreateProcessApi &&
        ApiMsg.CreateProcess.FileHandle != NULL)
    {
        ObCloseHandle(ApiMsg.CreateProcess.FileHandle, KernelMode);
    }
}

VOID
NTAPI
DbgkExitThread(
    _In_ NTSTATUS ExitStatus)
{
    DBGKM_MSG ApiMsg;

    PAGED_CODE();

    if (PsGetCurrentProcess()->DebugPort == NULL) return;

    RtlZeroMemory(&ApiMsg, sizeof(ApiMsg));
    ApiMsg.ApiNumber = DbgKmExitThreadApi;
    ApiMsg.ExitThread.ExitStatus = ExitStatus;

    DbgkpSendApiMessage(&ApiMsg, TRUE);
}

VOID
NTAPI
DbgkExitProcess(
    _In_ NTSTATUS ExitStatus)
{
    DBGKM_MSG ApiMsg;

    PAGED_CODE();

    if (PsGetCurrentProcess()->DebugPort == NULL) return;

    RtlZeroMemory(&ApiMsg, sizeof(ApiMsg));
    ApiMsg.ApiNumber = DbgKmExitProcessApi;
    ApiMsg.ExitProcess.ExitStatus = ExitStatus;

    /* A process on its way out is not held still: there is nothing left to
     * hold, and the debugger must still be told */
    DbgkpSendApiMessage(&ApiMsg, FALSE);
}

VOID
NTAPI
DbgkMapViewOfSection(
    _In_ PVOID Section,
    _In_ PVOID BaseAddress,
    _In_ ULONG SectionOffset,
    _In_ ULONG_PTR ViewSize)
{
    DBGKM_MSG ApiMsg;

    PAGED_CODE();

    UNREFERENCED_PARAMETER(ViewSize);

    if (PsGetCurrentProcess()->DebugPort == NULL) return;
    if (KeGetCurrentThread()->SystemThread) return;

    RtlZeroMemory(&ApiMsg, sizeof(ApiMsg));
    ApiMsg.ApiNumber = DbgKmLoadDllApi;
    ApiMsg.LoadDll.BaseOfDll = BaseAddress;
    ApiMsg.LoadDll.DebugInfoFileOffset = SectionOffset;
    ApiMsg.LoadDll.FileHandle = DbgkpSectionToFileHandle(Section);

    DbgkpSendApiMessage(&ApiMsg, TRUE);

    if (ApiMsg.LoadDll.FileHandle != NULL)
        ObCloseHandle(ApiMsg.LoadDll.FileHandle, KernelMode);
}

VOID
NTAPI
DbgkUnMapViewOfSection(
    _In_ PVOID BaseAddress)
{
    DBGKM_MSG ApiMsg;

    PAGED_CODE();

    if (PsGetCurrentProcess()->DebugPort == NULL) return;
    if (KeGetCurrentThread()->SystemThread) return;

    RtlZeroMemory(&ApiMsg, sizeof(ApiMsg));
    ApiMsg.ApiNumber = DbgKmUnloadDllApi;
    ApiMsg.UnloadDll.BaseAddress = BaseAddress;

    DbgkpSendApiMessage(&ApiMsg, TRUE);
}

/*
 * Offers an exception to whoever is watching. A debugger that handles it
 * stops it going any further; one that does not leaves it to the system.
 */
BOOLEAN
NTAPI
DbgkForwardException(
    _In_ PEXCEPTION_RECORD ExceptionRecord,
    _In_ BOOLEAN DebugPort,
    _In_ BOOLEAN SecondChance)
{
    DBGKM_MSG ApiMsg;
    NTSTATUS Status;

    PAGED_CODE();

    RtlZeroMemory(&ApiMsg, sizeof(ApiMsg));
    ApiMsg.ApiNumber = DbgKmExceptionApi;
    ApiMsg.Exception.ExceptionRecord = *ExceptionRecord;
    ApiMsg.Exception.FirstChance = !SecondChance;

    if (DebugPort)
    {
        if (PsGetCurrentProcess()->DebugPort == NULL) return FALSE;

        Status = DbgkpSendApiMessage(&ApiMsg, TRUE);
    }
    else
    {
        /* TODO: an exception offered to the subsystem's own port is not
         * carried there, no such port being connected to */
        return FALSE;
    }

    if (!NT_SUCCESS(Status)) return FALSE;

    return (BOOLEAN)(ApiMsg.ReturnedStatus != DBG_EXCEPTION_NOT_HANDLED);
}

/* A process started by a watched process may be watched with it */
VOID
NTAPI
DbgkCopyProcessDebugPort(
    _In_ PEPROCESS Process,
    _In_ PEPROCESS Parent)
{
    PDEBUG_OBJECT DebugObject;

    PAGED_CODE();

    Process->DebugPort = NULL;

    ExAcquireFastMutex(&DbgkpProcessDebugPortMutex);

    DebugObject = Parent->DebugPort;

    /* Only a port that says so is passed on, and never to a process that
     * was told not to be watched */
    if (DebugObject != NULL && !Process->NoDebugInherit)
    {
        ObReferenceObject(DebugObject);
        Process->DebugPort = DebugObject;
    }

    ExReleaseFastMutex(&DbgkpProcessDebugPortMutex);

    if (Process->DebugPort != NULL) DbgkpMarkProcessPeb(Process);
}

NTSTATUS
NTAPI
DbgkClearProcessDebugObject(
    _In_ PEPROCESS Process,
    _In_opt_ PDEBUG_OBJECT SourceDebugObject)
{
    PDEBUG_OBJECT DebugObject;
    PDEBUG_EVENT DebugEvent;
    PLIST_ENTRY Entry;
    PLIST_ENTRY Next;

    PAGED_CODE();

    ExAcquireFastMutex(&DbgkpProcessDebugPortMutex);

    DebugObject = Process->DebugPort;

    if (DebugObject == NULL ||
        (SourceDebugObject != NULL && DebugObject != SourceDebugObject))
    {
        ExReleaseFastMutex(&DbgkpProcessDebugPortMutex);
        return STATUS_PORT_NOT_SET;
    }

    Process->DebugPort = NULL;

    ExReleaseFastMutex(&DbgkpProcessDebugPortMutex);

    DbgkpMarkProcessPeb(Process);

    /* Whatever this process still had to say to that debugger is dropped,
     * and the threads waiting to be told to go on are let go */
    ExAcquireFastMutex(&DebugObject->Mutex);

    for (Entry = DebugObject->EventList.Flink;
         Entry != &DebugObject->EventList;
         Entry = Next)
    {
        Next = Entry->Flink;
        DebugEvent = CONTAINING_RECORD(Entry, DEBUG_EVENT, EventList);

        if (DebugEvent->ClientId.UniqueProcess != Process->UniqueProcessId)
            continue;

        RemoveEntryList(&DebugEvent->EventList);
        DebugEvent->Status = STATUS_DEBUGGER_INACTIVE;
        DbgkpWakeTarget(DebugEvent);
    }

    ExReleaseFastMutex(&DebugObject->Mutex);

    ObDereferenceObject(DebugObject);
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
DbgkOpenProcessDebugPort(
    _In_ PEPROCESS Process,
    _In_ KPROCESSOR_MODE PreviousMode,
    _Out_ HANDLE *DebugHandle)
{
    PDEBUG_OBJECT DebugObject;
    NTSTATUS Status;

    PAGED_CODE();

    ExAcquireFastMutex(&DbgkpProcessDebugPortMutex);

    DebugObject = Process->DebugPort;
    if (DebugObject != NULL) ObReferenceObject(DebugObject);

    ExReleaseFastMutex(&DbgkpProcessDebugPortMutex);

    if (DebugObject == NULL) return STATUS_PORT_NOT_SET;

    Status = ObOpenObjectByPointer(DebugObject,
                                   0,
                                   NULL,
                                   MAXIMUM_ALLOWED,
                                   DbgkDebugObjectType,
                                   PreviousMode,
                                   DebugHandle);

    ObDereferenceObject(DebugObject);
    return Status;
}

NTSTATUS
NTAPI
DbgkWerCaptureLiveKernelDump(
    _In_ PUNICODE_STRING Name,
    _In_ ULONG Flags)
{
    PAGED_CODE();

    UNREFERENCED_PARAMETER(Name);
    UNREFERENCED_PARAMETER(Flags);

    /* TODO: no dump is taken of a running system on a debugger's asking */
    return STATUS_NOT_IMPLEMENTED;
}

/* SYSTEM CALLS ***************************************************************/

NTSTATUS
NTAPI
NtDebugActiveProcess(
    _In_ HANDLE ProcessHandle,
    _In_ HANDLE DebugHandle)
{
    KPROCESSOR_MODE PreviousMode = ExGetPreviousMode();
    PDEBUG_OBJECT DebugObject;
    PETHREAD Last = NULL;
    PEPROCESS Process;
    NTSTATUS Status;

    PAGED_CODE();

    Status = ObReferenceObjectByHandle(ProcessHandle,
                                       PROCESS_SUSPEND_RESUME,
                                       PsProcessType,
                                       PreviousMode,
                                       (PVOID *)&Process,
                                       NULL);
    if (!NT_SUCCESS(Status)) return Status;

    /* Nobody watches the system, and nobody watches its own watcher */
    if (Process == PsGetCurrentProcess() || Process == PsInitialSystemProcess)
    {
        ObDereferenceObject(Process);
        return STATUS_ACCESS_DENIED;
    }

    Status = ObReferenceObjectByHandle(DebugHandle,
                                       DEBUG_OBJECT_ADD_REMOVE_PROCESS,
                                       DbgkDebugObjectType,
                                       PreviousMode,
                                       (PVOID *)&DebugObject,
                                       NULL);
    if (!NT_SUCCESS(Status))
    {
        ObDereferenceObject(Process);
        return Status;
    }

    Status = DbgkpPostFakeProcessCreateMessages(Process, DebugObject, &Last);

    Status = DbgkpSetProcessDebugObject(Process, DebugObject, Status, Last);

    ObDereferenceObject(DebugObject);
    ObDereferenceObject(Process);

    return Status;
}

NTSTATUS
NTAPI
NtRemoveProcessDebug(
    _In_ HANDLE ProcessHandle,
    _In_ HANDLE DebugHandle)
{
    KPROCESSOR_MODE PreviousMode = ExGetPreviousMode();
    PDEBUG_OBJECT DebugObject;
    PEPROCESS Process;
    NTSTATUS Status;

    PAGED_CODE();

    Status = ObReferenceObjectByHandle(ProcessHandle,
                                       PROCESS_SUSPEND_RESUME,
                                       PsProcessType,
                                       PreviousMode,
                                       (PVOID *)&Process,
                                       NULL);
    if (!NT_SUCCESS(Status)) return Status;

    Status = ObReferenceObjectByHandle(DebugHandle,
                                       DEBUG_OBJECT_ADD_REMOVE_PROCESS,
                                       DbgkDebugObjectType,
                                       PreviousMode,
                                       (PVOID *)&DebugObject,
                                       NULL);
    if (NT_SUCCESS(Status))
    {
        Status = DbgkClearProcessDebugObject(Process, DebugObject);
        ObDereferenceObject(DebugObject);
    }

    ObDereferenceObject(Process);
    return Status;
}
