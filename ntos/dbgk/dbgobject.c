/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     The object a debugger holds and what it is told through it
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

/* GLOBALS ********************************************************************/

POBJECT_TYPE DbgkDebugObjectType = NULL;
FAST_MUTEX DbgkpProcessDebugPortMutex;
ULONG DbgkpTraceLevel = 0;

/* PRIVATE DATA ***************************************************************/

static GENERIC_MAPPING DbgkpDebugObjectMapping =
{
    STANDARD_RIGHTS_READ | DEBUG_OBJECT_WAIT_STATE_CHANGE,
    STANDARD_RIGHTS_WRITE | DEBUG_OBJECT_ADD_REMOVE_PROCESS,
    STANDARD_RIGHTS_EXECUTE | SYNCHRONIZE,
    DEBUG_OBJECT_ALL_ACCESS
};

/* What the state a debugger is told about is called */
typedef struct _DBGKP_STATE_NAME
{
    DBGKM_APINUMBER ApiNumber;
    DBG_STATE State;
} DBGKP_STATE_NAME;

static const DBGKP_STATE_NAME DbgkpStateNames[] =
{
    {DbgKmExceptionApi, DbgExceptionStateChange},
    {DbgKmCreateThreadApi, DbgCreateThreadStateChange},
    {DbgKmCreateProcessApi, DbgCreateProcessStateChange},
    {DbgKmExitThreadApi, DbgExitThreadStateChange},
    {DbgKmExitProcessApi, DbgExitProcessStateChange},
    {DbgKmLoadDllApi, DbgLoadDllStateChange},
    {DbgKmUnloadDllApi, DbgUnloadDllStateChange},
};

/* PRIVATE FUNCTIONS **********************************************************/

VOID
NTAPI
DbgkpFreeDebugEvent(
    _In_ PDEBUG_EVENT DebugEvent)
{
    PAGED_CODE();

    /* The handles a debugger was offered and never took are let go */
    if (DebugEvent->Flags & DEBUG_EVENT_RELEASE)
    {
        if (DebugEvent->ApiMsg.ApiNumber == DbgKmCreateProcessApi &&
            DebugEvent->ApiMsg.CreateProcess.FileHandle != NULL)
        {
            ObCloseHandle(DebugEvent->ApiMsg.CreateProcess.FileHandle, KernelMode);
        }

        if (DebugEvent->ApiMsg.ApiNumber == DbgKmLoadDllApi &&
            DebugEvent->ApiMsg.LoadDll.FileHandle != NULL)
        {
            ObCloseHandle(DebugEvent->ApiMsg.LoadDll.FileHandle, KernelMode);
        }
    }

    if (DebugEvent->Process != NULL) ObDereferenceObject(DebugEvent->Process);
    if (DebugEvent->Thread != NULL) ObDereferenceObject(DebugEvent->Thread);

    ExFreePoolWithTag(DebugEvent, TAG_DEBUG_EVENT);
}

/* Lets a thread that is waiting to be continued go on its way */
VOID
NTAPI
DbgkpWakeTarget(
    _In_ PDEBUG_EVENT DebugEvent)
{
    PAGED_CODE();

    if (DebugEvent->Flags & DEBUG_EVENT_SUSPEND)
        PsResumeThread(DebugEvent->Thread, NULL);

    if (DebugEvent->Flags & DEBUG_EVENT_NOWAIT)
    {
        DbgkpFreeDebugEvent(DebugEvent);
        return;
    }

    KeSetEvent(&DebugEvent->ContinueEvent, IO_NO_INCREMENT, FALSE);
}

static
DBG_STATE
DbgkpStateOfApi(
    _In_ DBGKM_APINUMBER ApiNumber)
{
    ULONG i;

    for (i = 0; i < RTL_NUMBER_OF(DbgkpStateNames); i++)
    {
        if (DbgkpStateNames[i].ApiNumber == ApiNumber)
            return DbgkpStateNames[i].State;
    }

    return DbgIdle;
}

/*
 * Turns what the kernel reported into what a debugger reads, naming the state
 * and taking the handles the debugger will be given.
 */
NTSTATUS
NTAPI
DbgkpConvertKernelToUserStateChange(
    _Out_ PDBGUI_WAIT_STATE_CHANGE WaitStateChange,
    _In_ PDEBUG_EVENT DebugEvent)
{
    PAGED_CODE();

    RtlZeroMemory(WaitStateChange, sizeof(*WaitStateChange));

    WaitStateChange->NewState = DbgkpStateOfApi(DebugEvent->ApiMsg.ApiNumber);
    WaitStateChange->AppClientId = DebugEvent->ClientId;

    switch (DebugEvent->ApiMsg.ApiNumber)
    {
        case DbgKmExceptionApi:
            WaitStateChange->StateInfo.Exception = DebugEvent->ApiMsg.Exception;

            /* A breakpoint and a step are told apart by what was raised */
            if (DebugEvent->ApiMsg.Exception.ExceptionRecord.ExceptionCode ==
                STATUS_BREAKPOINT)
            {
                WaitStateChange->NewState = DbgBreakpointStateChange;
            }
            else if (DebugEvent->ApiMsg.Exception.ExceptionRecord.ExceptionCode ==
                     STATUS_SINGLE_STEP)
            {
                WaitStateChange->NewState = DbgSingleStepStateChange;
            }
            break;

        case DbgKmCreateThreadApi:
            WaitStateChange->StateInfo.CreateThread.NewThread =
                DebugEvent->ApiMsg.CreateThread;
            break;

        case DbgKmCreateProcessApi:
            WaitStateChange->StateInfo.CreateProcessInfo.NewProcess =
                DebugEvent->ApiMsg.CreateProcess;
            break;

        case DbgKmExitThreadApi:
            WaitStateChange->StateInfo.ExitThread = DebugEvent->ApiMsg.ExitThread;
            break;

        case DbgKmExitProcessApi:
            WaitStateChange->StateInfo.ExitProcess = DebugEvent->ApiMsg.ExitProcess;
            break;

        case DbgKmLoadDllApi:
            WaitStateChange->StateInfo.LoadDll = DebugEvent->ApiMsg.LoadDll;
            break;

        case DbgKmUnloadDllApi:
            WaitStateChange->StateInfo.UnloadDll = DebugEvent->ApiMsg.UnloadDll;
            break;

        default:
            return STATUS_UNSUCCESSFUL;
    }

    return STATUS_SUCCESS;
}

/*
 * Hands the debugger the handles that go with what it is being told: the
 * thread and, when a process starts, the process and the file it runs.
 */
NTSTATUS
NTAPI
DbgkpOpenHandles(
    _Inout_ PDBGUI_WAIT_STATE_CHANGE WaitStateChange,
    _In_ PEPROCESS Process,
    _In_ PETHREAD Thread)
{
    NTSTATUS Status;
    HANDLE Handle;

    PAGED_CODE();

    if (WaitStateChange->NewState == DbgCreateThreadStateChange)
    {
        Status = ObOpenObjectByPointer(Thread,
                                       0,
                                       NULL,
                                       THREAD_ALL_ACCESS,
                                       PsThreadType,
                                       KernelMode,
                                       &Handle);
        if (!NT_SUCCESS(Status)) return Status;

        WaitStateChange->StateInfo.CreateThread.HandleToThread = Handle;
        return STATUS_SUCCESS;
    }

    if (WaitStateChange->NewState != DbgCreateProcessStateChange)
        return STATUS_SUCCESS;

    Status = ObOpenObjectByPointer(Thread,
                                   0,
                                   NULL,
                                   THREAD_ALL_ACCESS,
                                   PsThreadType,
                                   KernelMode,
                                   &Handle);
    if (!NT_SUCCESS(Status)) return Status;

    WaitStateChange->StateInfo.CreateProcessInfo.HandleToThread = Handle;

    Status = ObOpenObjectByPointer(Process,
                                   0,
                                   NULL,
                                   PROCESS_ALL_ACCESS,
                                   PsProcessType,
                                   KernelMode,
                                   &Handle);
    if (!NT_SUCCESS(Status))
    {
        ObCloseHandle(WaitStateChange->StateInfo.CreateProcessInfo.HandleToThread,
                      KernelMode);
        WaitStateChange->StateInfo.CreateProcessInfo.HandleToThread = NULL;
        return Status;
    }

    WaitStateChange->StateInfo.CreateProcessInfo.HandleToProcess = Handle;
    return STATUS_SUCCESS;
}

/*
 * Puts what happened on the debugger's list and, unless the report is only
 * told and not asked about, waits until the debugger says to go on.
 */
NTSTATUS
NTAPI
DbgkpQueueMessage(
    _In_ PEPROCESS Process,
    _In_ PETHREAD Thread,
    _Inout_ PDBGKM_MSG ApiMsg,
    _In_ ULONG Flags,
    _In_opt_ PDEBUG_OBJECT TargetObject)
{
    PDEBUG_EVENT DebugEvent;
    PDEBUG_OBJECT DebugObject;
    NTSTATUS Status;

    PAGED_CODE();

    DebugEvent = ExAllocatePoolWithTag(NonPagedPool,
                                       sizeof(DEBUG_EVENT),
                                       TAG_DEBUG_EVENT);
    if (DebugEvent == NULL) return STATUS_INSUFFICIENT_RESOURCES;

    RtlZeroMemory(DebugEvent, sizeof(*DebugEvent));

    KeInitializeEvent(&DebugEvent->ContinueEvent, SynchronizationEvent, FALSE);

    DebugEvent->Process = Process;
    DebugEvent->Thread = Thread;
    DebugEvent->ApiMsg = *ApiMsg;
    DebugEvent->ClientId = Thread->Cid;
    DebugEvent->Flags = Flags & ~DEBUG_EVENT_INACTIVE;
    DebugEvent->BackoutThread = PsGetCurrentThread();

    if (TargetObject != NULL)
    {
        /* A debugger that is only being caught up owns the list already */
        DebugObject = TargetObject;
        DebugEvent->Flags |= DEBUG_EVENT_NOWAIT;
        ObReferenceObject(Process);
        ObReferenceObject(Thread);
    }
    else
    {
        ExAcquireFastMutex(&DbgkpProcessDebugPortMutex);

        DebugObject = Process->DebugPort;
        if (DebugObject == NULL)
        {
            ExReleaseFastMutex(&DbgkpProcessDebugPortMutex);
            ExFreePoolWithTag(DebugEvent, TAG_DEBUG_EVENT);
            return STATUS_PORT_NOT_SET;
        }

        ObReferenceObject(DebugObject);
        ExReleaseFastMutex(&DbgkpProcessDebugPortMutex);

        DebugEvent->Process = NULL;
        DebugEvent->Thread = NULL;
    }

    ExAcquireFastMutex(&DebugObject->Mutex);

    if (DebugObject->DebuggerInactive)
    {
        Status = STATUS_DEBUGGER_INACTIVE;
    }
    else
    {
        InsertTailList(&DebugObject->EventList, &DebugEvent->EventList);
        KeSetEvent(&DebugObject->EventsPresent, IO_NO_INCREMENT, FALSE);
        Status = STATUS_SUCCESS;
    }

    ExReleaseFastMutex(&DebugObject->Mutex);

    if (TargetObject == NULL) ObDereferenceObject(DebugObject);

    if (!NT_SUCCESS(Status))
    {
        if (DebugEvent->Process != NULL) ObDereferenceObject(DebugEvent->Process);
        if (DebugEvent->Thread != NULL) ObDereferenceObject(DebugEvent->Thread);
        ExFreePoolWithTag(DebugEvent, TAG_DEBUG_EVENT);
        return Status;
    }

    if (DebugEvent->Flags & DEBUG_EVENT_NOWAIT) return STATUS_SUCCESS;

    /* The thread that reported waits here until the debugger has answered */
    KeWaitForSingleObject(&DebugEvent->ContinueEvent,
                          Executive,
                          KernelMode,
                          FALSE,
                          NULL);

    Status = DebugEvent->Status;
    *ApiMsg = DebugEvent->ApiMsg;

    DbgkpFreeDebugEvent(DebugEvent);

    return Status;
}

/* The debugger is gone: everything still waiting on it is let go */
static
VOID
DbgkpDrainDebugObject(
    _In_ PDEBUG_OBJECT DebugObject)
{
    PDEBUG_EVENT DebugEvent;
    PLIST_ENTRY Entry;

    PAGED_CODE();

    ExAcquireFastMutex(&DebugObject->Mutex);

    DebugObject->DebuggerInactive = TRUE;

    while (!IsListEmpty(&DebugObject->EventList))
    {
        Entry = RemoveHeadList(&DebugObject->EventList);
        DebugEvent = CONTAINING_RECORD(Entry, DEBUG_EVENT, EventList);

        DebugEvent->Status = STATUS_DEBUGGER_INACTIVE;
        DbgkpWakeTarget(DebugEvent);
    }

    ExReleaseFastMutex(&DebugObject->Mutex);
}

VOID
NTAPI
DbgkpCloseObject(
    _In_opt_ PEPROCESS Process,
    _In_ PVOID Object,
    _In_ ULONG_PTR ProcessHandleCount,
    _In_ ULONG_PTR SystemHandleCount)
{
    PDEBUG_OBJECT DebugObject = Object;
    PEPROCESS Debugged = NULL;

    PAGED_CODE();

    UNREFERENCED_PARAMETER(Process);
    UNREFERENCED_PARAMETER(ProcessHandleCount);

    /* Only the last handle going means the debugger is gone */
    if (SystemHandleCount > 1) return;

    DbgkpDrainDebugObject(DebugObject);

    /* Every process this debugger held is handed back to itself */
    while ((Debugged = PsGetNextProcess(Debugged)) != NULL)
    {
        if (Debugged->DebugPort != DebugObject) continue;

        DbgkClearProcessDebugObject(Debugged, DebugObject);
    }
}

VOID
NTAPI
DbgkpDeleteObject(
    _In_ PVOID Object)
{
    PDEBUG_OBJECT DebugObject = Object;

    PAGED_CODE();

    DbgkpDrainDebugObject(DebugObject);
}

/* PUBLIC FUNCTIONS ***********************************************************/

CODE_SEG("INIT")
VOID
NTAPI
DbgkInitialize(VOID)
{
    OBJECT_TYPE_INITIALIZER Initializer;
    UNICODE_STRING Name = RTL_CONSTANT_STRING(L"DebugObject");

    PAGED_CODE();

    ExInitializeFastMutex(&DbgkpProcessDebugPortMutex);

    RtlZeroMemory(&Initializer, sizeof(Initializer));

    Initializer.Length = sizeof(Initializer);
    Initializer.InvalidAttributes = OBJ_OPENLINK | OBJ_PERMANENT;
    Initializer.ValidAccessMask = DEBUG_OBJECT_ALL_ACCESS;
    Initializer.GenericMapping = DbgkpDebugObjectMapping;
    Initializer.PoolType = NonPagedPool;
    Initializer.DefaultNonPagedPoolCharge = sizeof(DEBUG_OBJECT);
    Initializer.CloseProcedure = DbgkpCloseObject;
    Initializer.DeleteProcedure = DbgkpDeleteObject;
    Initializer.SecurityProcedure = SeDefaultObjectMethod;

    ObCreateObjectType(&Name, &Initializer, NULL, &DbgkDebugObjectType);
}

/* SYSTEM CALLS ***************************************************************/

NTSTATUS
NTAPI
NtCreateDebugObject(
    _Out_ PHANDLE DebugHandle,
    _In_ ACCESS_MASK DesiredAccess,
    _In_ POBJECT_ATTRIBUTES ObjectAttributes,
    _In_ ULONG Flags)
{
    KPROCESSOR_MODE PreviousMode = ExGetPreviousMode();
    PDEBUG_OBJECT DebugObject;
    HANDLE Handle;
    NTSTATUS Status;

    PAGED_CODE();

    if (Flags & ~DBGK_ALL_FLAGS) return STATUS_INVALID_PARAMETER;

    if (PreviousMode != KernelMode)
    {
        _SEH2_TRY
        {
            ProbeForWriteHandle(DebugHandle);
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            _SEH2_YIELD(return _SEH2_GetExceptionCode());
        }
        _SEH2_END;
    }

    Status = ObCreateObject(PreviousMode,
                            DbgkDebugObjectType,
                            ObjectAttributes,
                            PreviousMode,
                            NULL,
                            sizeof(DEBUG_OBJECT),
                            0,
                            0,
                            (PVOID *)&DebugObject);
    if (!NT_SUCCESS(Status)) return Status;

    RtlZeroMemory(DebugObject, sizeof(*DebugObject));

    KeInitializeEvent(&DebugObject->EventsPresent, NotificationEvent, FALSE);
    ExInitializeFastMutex(&DebugObject->Mutex);
    InitializeListHead(&DebugObject->EventList);

    if (Flags & DBGK_KILL_PROCESS_ON_EXIT)
        DebugObject->KillProcessOnExit = TRUE;

    Status = ObInsertObject(DebugObject,
                            NULL,
                            DesiredAccess,
                            0,
                            NULL,
                            &Handle);
    if (!NT_SUCCESS(Status)) return Status;

    _SEH2_TRY
    {
        *DebugHandle = Handle;
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Status = _SEH2_GetExceptionCode();
    }
    _SEH2_END;

    if (!NT_SUCCESS(Status)) ObCloseHandle(Handle, PreviousMode);

    return Status;
}

NTSTATUS
NTAPI
NtSetInformationDebugObject(
    _In_ HANDLE DebugHandle,
    _In_ DEBUGOBJECTINFOCLASS InformationClass,
    _In_ PVOID Information,
    _In_ ULONG InformationLength,
    _Out_opt_ PULONG ReturnLength)
{
    KPROCESSOR_MODE PreviousMode = ExGetPreviousMode();
    DEBUG_OBJECT_KILL_PROCESS_ON_EXIT_INFORMATION Told;
    PDEBUG_OBJECT DebugObject;
    NTSTATUS Status;

    PAGED_CODE();

    if (InformationClass != DebugObjectKillProcessOnExitInformation)
        return STATUS_INVALID_PARAMETER;

    if (InformationLength != sizeof(Told)) return STATUS_INFO_LENGTH_MISMATCH;

    _SEH2_TRY
    {
        if (PreviousMode != KernelMode)
        {
            ProbeForRead(Information, sizeof(Told), sizeof(ULONG));
            if (ReturnLength != NULL) ProbeForWriteUlong(ReturnLength);
        }

        Told = *(PDEBUG_OBJECT_KILL_PROCESS_ON_EXIT_INFORMATION)Information;
        if (ReturnLength != NULL) *ReturnLength = 0;
        Status = STATUS_SUCCESS;
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Status = _SEH2_GetExceptionCode();
    }
    _SEH2_END;

    if (!NT_SUCCESS(Status)) return Status;

    Status = ObReferenceObjectByHandle(DebugHandle,
                                       DEBUG_OBJECT_SET_INFORMATION,
                                       DbgkDebugObjectType,
                                       PreviousMode,
                                       (PVOID *)&DebugObject,
                                       NULL);
    if (!NT_SUCCESS(Status)) return Status;

    ExAcquireFastMutex(&DebugObject->Mutex);
    DebugObject->KillProcessOnExit = (Told.KillProcessOnExit != 0);
    ExReleaseFastMutex(&DebugObject->Mutex);

    ObDereferenceObject(DebugObject);
    return STATUS_SUCCESS;
}

/*
 * Reads the next thing the debugger has not been told yet. What is read stays
 * on the list until the debugger says to go on, so it can be read again if
 * the debugger's own memory could not be written.
 */
NTSTATUS
NTAPI
NtWaitForDebugEvent(
    _In_ HANDLE DebugHandle,
    _In_ BOOLEAN Alertable,
    _In_opt_ PLARGE_INTEGER Timeout,
    _Out_ PDBGUI_WAIT_STATE_CHANGE StateChange)
{
    KPROCESSOR_MODE PreviousMode = ExGetPreviousMode();
    DBGUI_WAIT_STATE_CHANGE Told;
    LARGE_INTEGER Wait;
    PLARGE_INTEGER WaitFor = NULL;
    PDEBUG_OBJECT DebugObject;
    PDEBUG_EVENT DebugEvent = NULL;
    PDEBUG_EVENT Looked;
    PLIST_ENTRY Entry;
    NTSTATUS Status;

    PAGED_CODE();

    _SEH2_TRY
    {
        if (PreviousMode != KernelMode)
        {
            if (Timeout != NULL)
            {
                ProbeForRead(Timeout, sizeof(LARGE_INTEGER), sizeof(ULONG));
                Wait = *Timeout;
                WaitFor = &Wait;
            }

            ProbeForWrite(StateChange, sizeof(*StateChange), sizeof(ULONG));
        }
        else
        {
            WaitFor = Timeout;
        }

        Status = STATUS_SUCCESS;
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Status = _SEH2_GetExceptionCode();
    }
    _SEH2_END;

    if (!NT_SUCCESS(Status)) return Status;

    Status = ObReferenceObjectByHandle(DebugHandle,
                                       DEBUG_OBJECT_WAIT_STATE_CHANGE,
                                       DbgkDebugObjectType,
                                       PreviousMode,
                                       (PVOID *)&DebugObject,
                                       NULL);
    if (!NT_SUCCESS(Status)) return Status;

    for (;;)
    {
        Status = KeWaitForSingleObject(&DebugObject->EventsPresent,
                                       Executive,
                                       PreviousMode,
                                       Alertable,
                                       WaitFor);
        if (Status != STATUS_SUCCESS) break;

        ExAcquireFastMutex(&DebugObject->Mutex);

        if (DebugObject->DebuggerInactive)
        {
            ExReleaseFastMutex(&DebugObject->Mutex);
            Status = STATUS_DEBUGGER_INACTIVE;
            break;
        }

        for (Entry = DebugObject->EventList.Flink;
             Entry != &DebugObject->EventList;
             Entry = Entry->Flink)
        {
            Looked = CONTAINING_RECORD(Entry, DEBUG_EVENT, EventList);

            if (Looked->Flags & (DEBUG_EVENT_READ | DEBUG_EVENT_INACTIVE))
                continue;

            DebugEvent = Looked;
            break;
        }

        if (DebugEvent == NULL)
        {
            /* Nothing is left to tell, so the next waiter waits again */
            KeClearEvent(&DebugObject->EventsPresent);
            ExReleaseFastMutex(&DebugObject->Mutex);
            continue;
        }

        DebugEvent->Flags |= DEBUG_EVENT_READ;

        Status = DbgkpConvertKernelToUserStateChange(&Told, DebugEvent);
        if (NT_SUCCESS(Status))
        {
            Status = DbgkpOpenHandles(&Told,
                                      DebugEvent->Process,
                                      DebugEvent->Thread);
        }

        ExReleaseFastMutex(&DebugObject->Mutex);
        break;
    }

    ObDereferenceObject(DebugObject);

    if (!NT_SUCCESS(Status) || DebugEvent == NULL) return Status;

    _SEH2_TRY
    {
        *StateChange = Told;
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Status = _SEH2_GetExceptionCode();
    }
    _SEH2_END;

    return Status;
}

/*
 * Says what is to become of what the debugger was told about, and lets the
 * thread that reported it go on.
 */
NTSTATUS
NTAPI
NtDebugContinue(
    _In_ HANDLE DebugHandle,
    _In_ PCLIENT_ID ClientId,
    _In_ NTSTATUS ContinueStatus)
{
    KPROCESSOR_MODE PreviousMode = ExGetPreviousMode();
    PDEBUG_OBJECT DebugObject;
    PDEBUG_EVENT DebugEvent = NULL;
    PDEBUG_EVENT Looked;
    PLIST_ENTRY Entry;
    CLIENT_ID Told;
    NTSTATUS Status;

    PAGED_CODE();

    if (ContinueStatus != DBG_CONTINUE &&
        ContinueStatus != DBG_EXCEPTION_HANDLED &&
        ContinueStatus != DBG_EXCEPTION_NOT_HANDLED &&
        ContinueStatus != DBG_TERMINATE_THREAD &&
        ContinueStatus != DBG_TERMINATE_PROCESS)
    {
        return STATUS_INVALID_PARAMETER;
    }

    _SEH2_TRY
    {
        if (PreviousMode != KernelMode)
            ProbeForRead(ClientId, sizeof(CLIENT_ID), sizeof(ULONG));

        Told = *ClientId;
        Status = STATUS_SUCCESS;
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Status = _SEH2_GetExceptionCode();
    }
    _SEH2_END;

    if (!NT_SUCCESS(Status)) return Status;

    Status = ObReferenceObjectByHandle(DebugHandle,
                                       DEBUG_OBJECT_WAIT_STATE_CHANGE,
                                       DbgkDebugObjectType,
                                       PreviousMode,
                                       (PVOID *)&DebugObject,
                                       NULL);
    if (!NT_SUCCESS(Status)) return Status;

    ExAcquireFastMutex(&DebugObject->Mutex);

    for (Entry = DebugObject->EventList.Flink;
         Entry != &DebugObject->EventList;
         Entry = Entry->Flink)
    {
        Looked = CONTAINING_RECORD(Entry, DEBUG_EVENT, EventList);

        if (Looked->ClientId.UniqueProcess != Told.UniqueProcess) continue;
        if (Looked->ClientId.UniqueThread != Told.UniqueThread) continue;

        /* Only what the debugger has already been told may be answered */
        if (!(Looked->Flags & DEBUG_EVENT_READ)) continue;

        DebugEvent = Looked;
        RemoveEntryList(&DebugEvent->EventList);
        break;
    }

    ExReleaseFastMutex(&DebugObject->Mutex);
    ObDereferenceObject(DebugObject);

    if (DebugEvent == NULL) return STATUS_INVALID_PARAMETER;

    DebugEvent->Status = ContinueStatus;
    DbgkpWakeTarget(DebugEvent);

    return STATUS_SUCCESS;
}
