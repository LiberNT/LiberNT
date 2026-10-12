/*
 * PROJECT:     LiberNT Console Driver
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Console device objects, process attachment and console host queue
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntifs.h>
#include <pseh/pseh2.h>
#include <drivers/condrv/condrvhost.h>

#define NDEBUG
#include <debug.h>

#define CD_TAG 'vDdC'

#define IOCTL_CONDRV_READ_CONSOLE          CTL_CODE(FILE_DEVICE_CONSOLE, 10, METHOD_BUFFERED, FILE_READ_ACCESS)
#define IOCTL_CONDRV_READ_FILE             CTL_CODE(FILE_DEVICE_CONSOLE, 11, METHOD_BUFFERED, FILE_READ_ACCESS)
#define IOCTL_CONDRV_READ_INPUT            CTL_CODE(FILE_DEVICE_CONSOLE, 12, METHOD_BUFFERED, FILE_READ_ACCESS)
#define IOCTL_CONDRV_CTRL_EVENT            CTL_CODE(FILE_DEVICE_CONSOLE, 19, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_CONDRV_FLUSH                 CTL_CODE(FILE_DEVICE_CONSOLE, 21, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_CONDRV_GET_PROCESS_LIST      CTL_CODE(FILE_DEVICE_CONSOLE, 23, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_CONDRV_READ_CONSOLE_CONTROL  CTL_CODE(FILE_DEVICE_CONSOLE, 24, METHOD_BUFFERED, FILE_READ_ACCESS)
#define IOCTL_CONDRV_WRITE_FILE            CTL_CODE(FILE_DEVICE_CONSOLE, 31, METHOD_BUFFERED, FILE_WRITE_ACCESS)
#define IOCTL_CONDRV_ACTIVATE              CTL_CODE(FILE_DEVICE_CONSOLE, 36, METHOD_BUFFERED, FILE_WRITE_ACCESS)
#define IOCTL_CONDRV_BIND_PID              CTL_CODE(FILE_DEVICE_CONSOLE, 51, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_CONDRV_SETUP_INPUT           CTL_CODE(FILE_DEVICE_CONSOLE, 60, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_CONDRV_INIT_OUTPUT           CTL_CODE(FILE_DEVICE_CONSOLE, 90, METHOD_BUFFERED, 0)
#define IOCTL_CONDRV_CLOSE_OUTPUT          CTL_CODE(FILE_DEVICE_CONSOLE, 91, METHOD_BUFFERED, 0)

#define CD_ATTACH_PARENT_PROCESS ((ULONG)-1)
#define CD_CTRL_C_EVENT        0
#define CD_CTRL_BREAK_EVENT    1
#define CD_CTRL_SHUTDOWN_EVENT 6

NTSYSAPI
NTSTATUS
NTAPI
RtlCreateUserThread(
    _In_ HANDLE ProcessHandle,
    _In_opt_ PSECURITY_DESCRIPTOR SecurityDescriptor,
    _In_ BOOLEAN CreateSuspended,
    _In_ ULONG StackZeroBits,
    _In_ SIZE_T StackReserve,
    _In_ SIZE_T StackCommit,
    _In_ PVOID StartAddress,
    _In_opt_ PVOID Parameter,
    _Out_opt_ PHANDLE ThreadHandle,
    _Out_opt_ PCLIENT_ID ClientId);

NTKERNELAPI
HANDLE
NTAPI
PsGetProcessInheritedFromUniqueProcessId(
    _In_ PEPROCESS Process);

typedef enum _CD_FILE_TYPE
{
    CdFileServer = 1,
    CdFileConsole,
    CdFileScreen,
    CdFileInput,
    CdFileOutput,
    CdFileConnection
} CD_FILE_TYPE;

typedef struct _CD_CONSOLE CD_CONSOLE, *PCD_CONSOLE;
typedef struct _CD_SERVER CD_SERVER, *PCD_SERVER;
typedef struct _CD_SCREEN CD_SCREEN, *PCD_SCREEN;

struct _CD_CONSOLE
{
    LONG RefCount;
    PCD_SERVER Server;
    PCD_SCREEN Active;
    ULONG LastId;
    BOOLEAN Signaled;
    LIST_ENTRY Screens;
    LIST_ENTRY Files;
};

struct _CD_SCREEN
{
    LONG RefCount;
    ULONG Id;
    PCD_CONSOLE Console;
    LIST_ENTRY Entry;
};

struct _CD_SERVER
{
    PCD_CONSOLE Console;
    PFILE_OBJECT FileObject;
    PEPROCESS Process;
    LIST_ENTRY Queue;
    LIST_ENTRY ReadQueue;
    BOOLEAN Busy;
    LONG ScratchBusy;
    ULONG ScratchSize;
    PUCHAR Scratch;
};

typedef struct _CD_FILE
{
    CD_FILE_TYPE Type;
    PFILE_OBJECT FileObject;
    PCD_SERVER Server;
    PCD_CONSOLE Console;
    PCD_SCREEN Screen;
    PCD_CONSOLE Mirror;
    LIST_ENTRY MirrorEntry;
    LONG NoEventIrps;
} CD_FILE, *PCD_FILE;

typedef struct _CD_REQUEST
{
    LIST_ENTRY Entry;
    ULONG Code;
    ULONG Output;
    PIRP Irp;
    PVOID InData;
    ULONG InSize;
    ULONG OutSize;
    BOOLEAN Delivered;
    BOOLEAN Fast;
    BOOLEAN Abandoned;
    NTSTATUS Status;
    ULONG_PTR Information;
    PVOID OutBuffer;
    KEVENT Done;
} CD_REQUEST, *PCD_REQUEST;

typedef struct _CD_PROCESS
{
    LIST_ENTRY Entry;
    LIST_ENTRY HashEntry;
    PEPROCESS Process;
    PCD_CONSOLE Console;
    PFILE_OBJECT Connection;
    ULONGLONG CtrlRoutine;
    ULONG GroupId;
} CD_PROCESS, *PCD_PROCESS;

typedef struct _CD_PROCESS_REF
{
    PEPROCESS Process;
    ULONGLONG Routine;
} CD_PROCESS_REF, *PCD_PROCESS_REF;

typedef struct _CD_CTRL_EVENT
{
    LONG Event;
    ULONG GroupId;
} CD_CTRL_EVENT, *PCD_CTRL_EVENT;

typedef struct _CD_DEFERRED
{
    LIST_ENTRY Irps;
    LIST_ENTRY Processes;
} CD_DEFERRED, *PCD_DEFERRED;

typedef struct _CD_DEFERRED_PROCESS
{
    LIST_ENTRY Entry;
    PEPROCESS Process;
    BOOLEAN Terminate;
} CD_DEFERRED_PROCESS, *PCD_DEFERRED_PROCESS;

#define CD_PROCESS_BUCKETS 64

static KSPIN_LOCK CdLock;
static LIST_ENTRY CdProcessList;
static LIST_ENTRY CdProcessHash[CD_PROCESS_BUCKETS];

static const UNICODE_STRING CdNameServer = RTL_CONSTANT_STRING(L"\\Server");
static const UNICODE_STRING CdNameConnection = RTL_CONSTANT_STRING(L"\\Connection");
static const UNICODE_STRING CdNameInput = RTL_CONSTANT_STRING(L"\\Input");
static const UNICODE_STRING CdNameOutput = RTL_CONSTANT_STRING(L"\\Output");
static const UNICODE_STRING CdNameCurrentIn = RTL_CONSTANT_STRING(L"\\CurrentIn");
static const UNICODE_STRING CdNameCurrentOut = RTL_CONSTANT_STRING(L"\\CurrentOut");
static const UNICODE_STRING CdNameScreenBuffer = RTL_CONSTANT_STRING(L"\\ScreenBuffer");
static const UNICODE_STRING CdNameConsole = RTL_CONSTANT_STRING(L"\\Console");
static const UNICODE_STRING CdNameRoot = RTL_CONSTANT_STRING(L"\\");
static const UNICODE_STRING CdNameRelReference = RTL_CONSTANT_STRING(L"Reference");
static const UNICODE_STRING CdNameRelConnection = RTL_CONSTANT_STRING(L"Connection");

static
VOID
CdInitDeferred(
    _Out_ PCD_DEFERRED Deferred)
{
    InitializeListHead(&Deferred->Irps);
    InitializeListHead(&Deferred->Processes);
}

static
PLIST_ENTRY
CdProcessBucket(
    _In_ PEPROCESS Process)
{
    ULONG_PTR Key = (ULONG_PTR)Process >> 6;

    return &CdProcessHash[(Key ^ (Key >> 6) ^ (Key >> 12)) & (CD_PROCESS_BUCKETS - 1)];
}

static
PCD_PROCESS
CdFindProcess(
    _In_ PEPROCESS Process)
{
    PLIST_ENTRY Bucket = CdProcessBucket(Process);
    PLIST_ENTRY Entry;

    for (Entry = Bucket->Flink; Entry != Bucket; Entry = Entry->Flink)
    {
        PCD_PROCESS Attached = CONTAINING_RECORD(Entry, CD_PROCESS, HashEntry);
        if (Attached->Process == Process)
            return Attached;
    }
    return NULL;
}

static
PCD_CONSOLE
CdCurrentConsole(VOID)
{
    PCD_PROCESS Attached = CdFindProcess(PsGetCurrentProcess());
    return Attached ? Attached->Console : NULL;
}

static
VOID
CdSyncFileEvent(
    _In_ PCD_FILE File)
{
    if (File->NoEventIrps)
        return;
    File->FileObject->Event.Header.Type = NotificationEvent;
    if (File->Mirror && File->Mirror->Signaled)
        KeSetEvent(&File->FileObject->Event, IO_NO_INCREMENT, FALSE);
    else
        KeClearEvent(&File->FileObject->Event);
}

static
VOID
CdUpdateFileEvent(
    _In_ PCD_FILE File)
{
    if (File->FileObject->Busy)
        return;
    CdSyncFileEvent(File);
}

static
VOID
CdSetMirror(
    _In_ PCD_FILE File,
    _In_opt_ PCD_CONSOLE Console)
{
    if (File->Mirror == Console)
        return;
    if (File->Mirror)
        RemoveEntryList(&File->MirrorEntry);
    File->Mirror = Console;
    if (Console)
        InsertTailList(&Console->Files, &File->MirrorEntry);
    else
        InitializeListHead(&File->MirrorEntry);
}

static
VOID
CdSignalConsole(
    _In_ PCD_CONSOLE Console,
    _In_ BOOLEAN Signaled)
{
    PLIST_ENTRY Entry;

    Console->Signaled = Signaled;
    for (Entry = Console->Files.Flink; Entry != &Console->Files; Entry = Entry->Flink)
        CdUpdateFileEvent(CONTAINING_RECORD(Entry, CD_FILE, MirrorEntry));
}

static
VOID
CdUpdateServerEvent(
    _In_ PCD_SERVER Server)
{
    if (!Server->FileObject)
        return;
    if (!Server->Console || !IsListEmpty(&Server->Queue))
        KeSetEvent(&Server->FileObject->Event, IO_NO_INCREMENT, FALSE);
    else
        KeClearEvent(&Server->FileObject->Event);
}

static
BOOLEAN
CdIsBlockingRead(
    _In_ ULONG Code)
{
    switch (Code)
    {
        case IOCTL_CONDRV_READ_INPUT:
        case IOCTL_CONDRV_READ_CONSOLE:
        case IOCTL_CONDRV_READ_CONSOLE_CONTROL:
        case IOCTL_CONDRV_READ_FILE:
            return TRUE;
        default:
            return FALSE;
    }
}

static
VOID
CdQueueNotify(
    _In_ PCD_SERVER Server,
    _In_ ULONG Code,
    _In_ ULONG Output)
{
    PCD_REQUEST Request;

    Request = ExAllocatePoolWithTag(NonPagedPool, sizeof(*Request), CD_TAG);
    if (!Request)
        return;
    RtlZeroMemory(Request, sizeof(*Request));
    Request->Code = Code;
    Request->Output = Output;
    InsertTailList(&Server->Queue, &Request->Entry);
    CdUpdateServerEvent(Server);
}

static
VOID
CdDetachIrp(
    _In_ PCD_REQUEST Request,
    _In_ NTSTATUS Status,
    _In_ ULONG_PTR Information,
    _Inout_ PCD_DEFERRED Deferred)
{
    PIRP Irp = Request->Irp;

    if (!Irp)
        return;
    Request->Irp = NULL;
    Irp->Tail.Overlay.DriverContext[0] = NULL;
    if (!IoSetCancelRoutine(Irp, NULL))
        return;
    Irp->IoStatus.Status = Status;
    Irp->IoStatus.Information = Information;
    InsertTailList(&Deferred->Irps, &Irp->Tail.Overlay.ListEntry);
}

static
VOID
CdFreeRequest(
    _In_ PCD_REQUEST Request)
{
    ExFreePoolWithTag(Request, CD_TAG);
}

static
VOID
CdFinishFastRequest(
    _In_ PCD_REQUEST Request,
    _In_ NTSTATUS Status,
    _In_ ULONG_PTR Information)
{
    if (Request->Abandoned)
    {
        CdFreeRequest(Request);
        return;
    }
    Request->Status = Status;
    Request->Information = Information;
    KeSetEvent(&Request->Done, IO_NO_INCREMENT, FALSE);
}

static
VOID
CdAbortRequest(
    _In_ PCD_REQUEST Request,
    _Inout_ PCD_DEFERRED Deferred)
{
    if (Request->Fast)
    {
        CdFinishFastRequest(Request, STATUS_CANCELLED, 0);
        return;
    }
    CdDetachIrp(Request, STATUS_CANCELLED, 0, Deferred);
    CdFreeRequest(Request);
}

static
VOID
CdDeferProcess(
    _In_ PEPROCESS Process,
    _In_ BOOLEAN Terminate,
    _Inout_ PCD_DEFERRED Deferred)
{
    PCD_DEFERRED_PROCESS Item;

    Item = ExAllocatePoolWithTag(NonPagedPool, sizeof(*Item), CD_TAG);
    if (!Item)
    {
        if (!Terminate)
            ObDereferenceObject(Process);
        return;
    }
    Item->Process = Process;
    Item->Terminate = Terminate;
    if (Terminate)
        ObReferenceObject(Process);
    InsertTailList(&Deferred->Processes, &Item->Entry);
}

static
VOID
CdDisconnectServer(
    _In_ PCD_SERVER Server,
    _Inout_ PCD_DEFERRED Deferred)
{
    while (!IsListEmpty(&Server->Queue))
        CdAbortRequest(CONTAINING_RECORD(RemoveHeadList(&Server->Queue), CD_REQUEST, Entry), Deferred);
    while (!IsListEmpty(&Server->ReadQueue))
        CdAbortRequest(CONTAINING_RECORD(RemoveHeadList(&Server->ReadQueue), CD_REQUEST, Entry), Deferred);
    Server->Busy = FALSE;
    if (Server->Console)
    {
        Server->Console->Server = NULL;
        Server->Console = NULL;
    }
    CdUpdateServerEvent(Server);
}

static
VOID
CdDereferenceScreen(
    _In_ PCD_SCREEN Screen)
{
    if (--Screen->RefCount)
        return;
    if (Screen->Console)
    {
        RemoveEntryList(&Screen->Entry);
        if (Screen->Console->Server)
            CdQueueNotify(Screen->Console->Server, IOCTL_CONDRV_CLOSE_OUTPUT, Screen->Id);
    }
    ExFreePoolWithTag(Screen, CD_TAG);
}

static
VOID
CdDestroyConsole(
    _In_ PCD_CONSOLE Console,
    _Inout_ PCD_DEFERRED Deferred)
{
    PCD_SCREEN Active;

    if (Console->Server)
        CdDisconnectServer(Console->Server, Deferred);

    while (!IsListEmpty(&Console->Files))
    {
        PCD_FILE File = CONTAINING_RECORD(Console->Files.Flink, CD_FILE, MirrorEntry);
        CdSetMirror(File, NULL);
        CdUpdateFileEvent(File);
    }

    while (!IsListEmpty(&Console->Screens))
    {
        PCD_SCREEN Screen = CONTAINING_RECORD(RemoveHeadList(&Console->Screens), CD_SCREEN, Entry);
        Screen->Console = NULL;
    }

    Active = Console->Active;
    Console->Active = NULL;
    if (Active)
        CdDereferenceScreen(Active);

    ExFreePoolWithTag(Console, CD_TAG);
}

static
VOID
CdDereferenceConsole(
    _In_ PCD_CONSOLE Console,
    _Inout_ PCD_DEFERRED Deferred)
{
    if (--Console->RefCount == 0)
        CdDestroyConsole(Console, Deferred);
}

static
VOID
CdSetActive(
    _In_ PCD_CONSOLE Console,
    _In_ PCD_SCREEN Screen)
{
    PCD_SCREEN Old = Console->Active;

    if (Old == Screen)
        return;
    Screen->RefCount++;
    Console->Active = Screen;
    if (Old)
        CdDereferenceScreen(Old);
    if (Console->Server)
        CdQueueNotify(Console->Server, IOCTL_CONDRV_ACTIVATE, Screen->Id);
}

static
PCD_SCREEN
CdCreateScreen(
    _In_ PCD_CONSOLE Console)
{
    PCD_SCREEN Screen;

    if (Console->LastId == MAXULONG)
        return NULL;
    Screen = ExAllocatePoolWithTag(NonPagedPool, sizeof(*Screen), CD_TAG);
    if (!Screen)
        return NULL;
    RtlZeroMemory(Screen, sizeof(*Screen));
    Screen->RefCount = 1;
    Screen->Id = ++Console->LastId;
    Screen->Console = Console;
    InsertHeadList(&Console->Screens, &Screen->Entry);
    if (Console->Server)
        CdQueueNotify(Console->Server, IOCTL_CONDRV_INIT_OUTPUT, Screen->Id);
    if (!Console->Active)
        CdSetActive(Console, Screen);
    return Screen;
}

static
PCD_CONSOLE
CdCreateConsole(
    _In_ PCD_SERVER Server)
{
    PCD_CONSOLE Console;
    PCD_SCREEN Screen;

    Console = ExAllocatePoolWithTag(NonPagedPool, sizeof(*Console), CD_TAG);
    if (!Console)
        return NULL;
    RtlZeroMemory(Console, sizeof(*Console));
    Console->RefCount = 1;
    InitializeListHead(&Console->Screens);
    InitializeListHead(&Console->Files);

    Screen = CdCreateScreen(Console);
    if (!Screen)
    {
        ExFreePoolWithTag(Console, CD_TAG);
        return NULL;
    }
    CdDereferenceScreen(Screen);

    Console->Server = Server;
    Server->Console = Console;
    CdUpdateServerEvent(Server);
    return Console;
}

static
NTSTATUS
CdAttachProcess(
    _In_ PEPROCESS Process,
    _In_ PCD_CONSOLE Console,
    _In_ PFILE_OBJECT Connection)
{
    PCD_PROCESS Attached;

    if (CdFindProcess(Process))
        return STATUS_ACCESS_DENIED;
    Attached = ExAllocatePoolWithTag(NonPagedPool, sizeof(*Attached), CD_TAG);
    if (!Attached)
        return STATUS_NO_MEMORY;
    RtlZeroMemory(Attached, sizeof(*Attached));
    ObReferenceObject(Process);
    Attached->Process = Process;
    Attached->Console = Console;
    Attached->Connection = Connection;
    Console->RefCount++;
    InsertHeadList(&CdProcessList, &Attached->Entry);
    InsertHeadList(CdProcessBucket(Process), &Attached->HashEntry);
    return STATUS_SUCCESS;
}

static
VOID
CdDetachConnection(
    _In_ PFILE_OBJECT Connection,
    _Inout_ PCD_DEFERRED Deferred)
{
    PLIST_ENTRY Entry;

    for (Entry = CdProcessList.Flink; Entry != &CdProcessList; Entry = Entry->Flink)
    {
        PCD_PROCESS Attached = CONTAINING_RECORD(Entry, CD_PROCESS, Entry);
        if (Attached->Connection != Connection)
            continue;
        RemoveEntryList(&Attached->Entry);
        RemoveEntryList(&Attached->HashEntry);
        CdDereferenceConsole(Attached->Console, Deferred);
        CdDeferProcess(Attached->Process, FALSE, Deferred);
        ExFreePoolWithTag(Attached, CD_TAG);
        return;
    }
}

static
VOID
CdFinishIrp(
    _In_ PIRP Irp)
{
    PFILE_OBJECT FileObject = IoGetCurrentIrpStackLocation(Irp)->FileObject;
    BOOLEAN NoEvent = Irp->Tail.Overlay.DriverContext[1] != NULL;
    KIRQL OldIrql;

    if (NoEvent)
        ObReferenceObject(FileObject);
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    if (NoEvent)
    {
        PCD_FILE File = FileObject->FsContext;

        KeSetEvent(&FileObject->Event, IO_NO_INCREMENT, FALSE);
        KeAcquireSpinLock(&CdLock, &OldIrql);
        File->NoEventIrps--;
        KeReleaseSpinLock(&CdLock, OldIrql);
        ObDereferenceObject(FileObject);
    }
}

static
VOID
CdRunDeferred(
    _Inout_ PCD_DEFERRED Deferred)
{
    while (!IsListEmpty(&Deferred->Irps))
    {
        PIRP Irp = CONTAINING_RECORD(RemoveHeadList(&Deferred->Irps), IRP, Tail.Overlay.ListEntry);
        CdFinishIrp(Irp);
    }

    while (!IsListEmpty(&Deferred->Processes))
    {
        PCD_DEFERRED_PROCESS Item = CONTAINING_RECORD(RemoveHeadList(&Deferred->Processes), CD_DEFERRED_PROCESS, Entry);

        if (Item->Terminate)
        {
            HANDLE ProcessHandle;

            if (NT_SUCCESS(ObOpenObjectByPointer(Item->Process, OBJ_KERNEL_HANDLE, NULL, PROCESS_ALL_ACCESS,
                                                 *PsProcessType, KernelMode, &ProcessHandle)))
            {
                ZwTerminateProcess(ProcessHandle, STATUS_SUCCESS);
                ZwClose(ProcessHandle);
            }
        }
        ObDereferenceObject(Item->Process);
        ExFreePoolWithTag(Item, CD_TAG);
    }
}

static
VOID
NTAPI
CdCancelIrp(
    _Inout_ PDEVICE_OBJECT DeviceObject,
    _Inout_ _IRQL_uses_cancel_ PIRP Irp)
{
    PCD_REQUEST Request;
    KIRQL OldIrql;

    UNREFERENCED_PARAMETER(DeviceObject);

    IoReleaseCancelSpinLock(Irp->CancelIrql);

    KeAcquireSpinLock(&CdLock, &OldIrql);
    Request = Irp->Tail.Overlay.DriverContext[0];
    if (Request)
    {
        Irp->Tail.Overlay.DriverContext[0] = NULL;
        Request->Irp = NULL;
        if (!Request->Delivered)
        {
            RemoveEntryList(&Request->Entry);
            CdFreeRequest(Request);
        }
    }
    KeReleaseSpinLock(&CdLock, OldIrql);

    Irp->IoStatus.Status = STATUS_CANCELLED;
    Irp->IoStatus.Information = 0;
    CdFinishIrp(Irp);
}

static
NTSTATUS
CdQueueIrp(
    _In_ PCD_SERVER Server,
    _In_ PCD_FILE File,
    _In_ PIRP Irp,
    _In_ ULONG Code,
    _In_ ULONG Output,
    _In_opt_ PVOID InData,
    _In_ ULONG InSize,
    _In_ ULONG OutSize)
{
    PCD_REQUEST Request;

    Request = ExAllocatePoolWithTag(NonPagedPool, sizeof(*Request), CD_TAG);
    if (!Request)
        return STATUS_NO_MEMORY;
    RtlZeroMemory(Request, sizeof(*Request));
    Request->Code = Code;
    Request->Output = Output;
    Request->Irp = Irp;
    Request->InData = InSize ? InData : NULL;
    Request->InSize = InSize;
    Request->OutSize = OutSize;

    Irp->Tail.Overlay.DriverContext[0] = Request;
    Irp->Tail.Overlay.DriverContext[1] = NULL;
    IoSetCancelRoutine(Irp, CdCancelIrp);
    if (Irp->Cancel && IoSetCancelRoutine(Irp, NULL))
    {
        Irp->Tail.Overlay.DriverContext[0] = NULL;
        ExFreePoolWithTag(Request, CD_TAG);
        return STATUS_CANCELLED;
    }

    if (!Irp->UserEvent || (File->FileObject->Flags & FO_SYNCHRONOUS_IO))
    {
        Irp->Tail.Overlay.DriverContext[1] = (PVOID)1;
        File->NoEventIrps++;
        if (File->FileObject->Flags & FO_SYNCHRONOUS_IO)
        {
            KeClearEvent(&File->FileObject->Event);
            File->FileObject->Event.Header.Type = SynchronizationEvent;
        }
    }
    IoMarkIrpPending(Irp);
    InsertTailList(&Server->Queue, &Request->Entry);
    CdUpdateServerEvent(Server);
    return STATUS_PENDING;
}

static
VOID
CdReplyRequest(
    _In_ PCD_REQUEST Request,
    _In_ NTSTATUS Status,
    _In_reads_bytes_(Size) PVOID Data,
    _In_ ULONG Size,
    _Inout_ PCD_DEFERRED Deferred)
{
    PIRP Irp = Request->Irp;

    if (Status == STATUS_PENDING)
        Status = STATUS_INVALID_PARAMETER;

    if (Request->Fast)
    {
        ULONG Copy = min(Request->OutSize, Size);

        if (NT_ERROR(Status) || Request->Abandoned)
            Copy = 0;
        if (Copy)
            RtlCopyMemory(Request->OutBuffer, Data, Copy);
        CdFinishFastRequest(Request, Status, Copy);
        return;
    }

    if (Irp)
    {
        ULONG Copy = min(Request->OutSize, Size);
        ULONG_PTR Information;

        if (NT_ERROR(Status))
        {
            Information = 0;
        }
        else
        {
            if (Copy)
                RtlCopyMemory(Irp->AssociatedIrp.SystemBuffer, Data, Copy);
            Information = (Request->Code == IOCTL_CONDRV_WRITE_FILE) ? Request->InSize : Copy;
        }
        CdDetachIrp(Request, Status, Information, Deferred);
    }
    CdFreeRequest(Request);
}

static
VOID
CdCollectCtrlTargets(
    _In_ PCD_CONSOLE Console,
    _In_ ULONG GroupId,
    _Out_writes_to_(Max, *Count) PCD_PROCESS_REF Targets,
    _In_ ULONG Max,
    _Out_ PULONG Count)
{
    PLIST_ENTRY Entry;

    *Count = 0;
    for (Entry = CdProcessList.Flink; Entry != &CdProcessList && *Count < Max; Entry = Entry->Flink)
    {
        PCD_PROCESS Attached = CONTAINING_RECORD(Entry, CD_PROCESS, Entry);
        if (Attached->Console != Console || !Attached->CtrlRoutine)
            continue;
        if (GroupId && Attached->GroupId != GroupId)
            continue;
        ObReferenceObject(Attached->Process);
        Targets[*Count].Process = Attached->Process;
        Targets[*Count].Routine = Attached->CtrlRoutine;
        (*Count)++;
    }
}

static
ULONG
CdCountProcesses(
    _In_ PCD_CONSOLE Console)
{
    PLIST_ENTRY Entry;
    ULONG Count = 0;

    for (Entry = CdProcessList.Flink; Entry != &CdProcessList; Entry = Entry->Flink)
    {
        if (CONTAINING_RECORD(Entry, CD_PROCESS, Entry)->Console == Console)
            Count++;
    }
    return Count;
}

static
VOID
CdStartCtrlThread(
    _In_ PEPROCESS Process,
    _In_ ULONGLONG Routine,
    _In_ ULONG Event)
{
    HANDLE ProcessHandle, ThreadHandle;
    NTSTATUS Status;

    Status = ObOpenObjectByPointer(Process, OBJ_KERNEL_HANDLE, NULL, PROCESS_ALL_ACCESS,
                                   *PsProcessType, KernelMode, &ProcessHandle);
    if (!NT_SUCCESS(Status))
        return;
    Status = RtlCreateUserThread(ProcessHandle, NULL, FALSE, 0, 0, 0,
                                 (PVOID)(ULONG_PTR)Routine, ULongToPtr(Event),
                                 &ThreadHandle, NULL);
    if (NT_SUCCESS(Status))
        ZwClose(ThreadHandle);
    else
        DPRINT1("Console control event %lu not delivered: 0x%08lx\n", Event, Status);
    ZwClose(ProcessHandle);
}

static
NTSTATUS
CdPropagateCtrl(
    _In_ PCD_CONSOLE Console,
    _In_ ULONG Event,
    _In_ ULONG GroupId,
    _In_ KIRQL OldIrql)
{
    PCD_PROCESS_REF Targets;
    ULONG Count, Max, Index;

    Max = CdCountProcesses(Console);
    if (!Max)
    {
        KeReleaseSpinLock(&CdLock, OldIrql);
        return STATUS_SUCCESS;
    }
    Targets = ExAllocatePoolWithTag(NonPagedPool, Max * sizeof(*Targets), CD_TAG);
    if (!Targets)
    {
        KeReleaseSpinLock(&CdLock, OldIrql);
        return STATUS_NO_MEMORY;
    }
    CdCollectCtrlTargets(Console, GroupId, Targets, Max, &Count);
    KeReleaseSpinLock(&CdLock, OldIrql);

    for (Index = 0; Index < Count; Index++)
    {
        CdStartCtrlThread(Targets[Index].Process, Targets[Index].Routine, Event);
        ObDereferenceObject(Targets[Index].Process);
    }
    ExFreePoolWithTag(Targets, CD_TAG);
    return STATUS_SUCCESS;
}

static
NTSTATUS
CdGetProcessList(
    _In_ PCD_CONSOLE Console,
    _Out_writes_bytes_(OutLength) PVOID Buffer,
    _In_ ULONG OutLength,
    _Out_ PULONG_PTR Information)
{
    PULONG List = Buffer;
    PLIST_ENTRY Entry;
    ULONG Count;

    *Information = 0;
    if (OutLength < sizeof(ULONG))
        return STATUS_INVALID_PARAMETER;

    Count = CdCountProcesses(Console);
    if (Count * sizeof(ULONG) > OutLength)
    {
        List[0] = Count;
        *Information = sizeof(ULONG);
        return STATUS_BUFFER_OVERFLOW;
    }

    Count = 0;
    for (Entry = CdProcessList.Flink; Entry != &CdProcessList; Entry = Entry->Flink)
    {
        PCD_PROCESS Attached = CONTAINING_RECORD(Entry, CD_PROCESS, Entry);
        if (Attached->Console == Console)
            List[Count++] = HandleToULong(PsGetProcessId(Attached->Process));
    }
    *Information = Count * sizeof(ULONG);
    return STATUS_SUCCESS;
}

static
NTSTATUS
CdConsoleIoctl(
    _In_ PCD_FILE File,
    _In_ PCD_CONSOLE Console,
    _In_ PIRP Irp,
    _In_ PIO_STACK_LOCATION Stack,
    _In_ KIRQL OldIrql,
    _Out_ PBOOLEAN Unlocked)
{
    ULONG Code = Stack->Parameters.DeviceIoControl.IoControlCode;
    ULONG InLength = Stack->Parameters.DeviceIoControl.InputBufferLength;
    ULONG OutLength = Stack->Parameters.DeviceIoControl.OutputBufferLength;

    *Unlocked = FALSE;

    switch (Code)
    {
        case IOCTL_CONDRV_CTRL_EVENT:
        {
            PCD_CTRL_EVENT Event = Irp->AssociatedIrp.SystemBuffer;
            PCD_PROCESS Caller;
            ULONG GroupId;

            if (InLength != sizeof(*Event))
                return STATUS_INVALID_PARAMETER;
            if (Event->Event != CD_CTRL_C_EVENT && Event->Event != CD_CTRL_BREAK_EVENT)
                return STATUS_NOT_IMPLEMENTED;
            Caller = CdFindProcess(PsGetCurrentProcess());
            GroupId = Event->GroupId ? Event->GroupId : (Caller ? Caller->GroupId : 0);
            if (!GroupId)
                return STATUS_INVALID_PARAMETER;
            *Unlocked = TRUE;
            return CdPropagateCtrl(Console, Event->Event, GroupId, OldIrql);
        }

        case IOCTL_CONDRV_GET_PROCESS_LIST:
            return CdGetProcessList(Console, Irp->AssociatedIrp.SystemBuffer, OutLength,
                                    &Irp->IoStatus.Information);

        default:
            if (!Console->Server || DEVICE_TYPE_FROM_CTL_CODE(Code) != FILE_DEVICE_CONSOLE ||
                METHOD_FROM_CTL_CODE(Code) != METHOD_BUFFERED)
            {
                return STATUS_INVALID_HANDLE;
            }
            return CdQueueIrp(Console->Server, File, Irp, Code, 0,
                              Irp->AssociatedIrp.SystemBuffer, InLength, OutLength);
    }
}

static
NTSTATUS
CdScreenIoctl(
    _In_ PCD_FILE File,
    _In_ PCD_SCREEN Screen,
    _In_ PIRP Irp,
    _In_ PIO_STACK_LOCATION Stack)
{
    ULONG Code = Stack->Parameters.DeviceIoControl.IoControlCode;
    PCD_CONSOLE Console = Screen->Console;

    if (Code == IOCTL_CONDRV_ACTIVATE)
    {
        if (!Console)
            return STATUS_INVALID_HANDLE;
        CdSetActive(Console, Screen);
        return STATUS_SUCCESS;
    }

    if (!Console || !Console->Server || DEVICE_TYPE_FROM_CTL_CODE(Code) != FILE_DEVICE_CONSOLE ||
        METHOD_FROM_CTL_CODE(Code) != METHOD_BUFFERED || CdIsBlockingRead(Code))
    {
        return STATUS_INVALID_HANDLE;
    }
    return CdQueueIrp(Console->Server, File, Irp, Code, Screen->Id,
                      Irp->AssociatedIrp.SystemBuffer,
                      Stack->Parameters.DeviceIoControl.InputBufferLength,
                      Stack->Parameters.DeviceIoControl.OutputBufferLength);
}

static
NTSTATUS
CdServerGetNext(
    _In_ PCD_SERVER Server,
    _Inout_ PUCHAR Buffer,
    _In_ ULONG InLength,
    _In_ ULONG OutLength,
    _Out_ PULONG_PTR Information)
{
    CONDRV_NEXT_REQUEST_PARAMS Params;
    CONDRV_NEXT_REQUEST_REPLY Reply;
    PCD_REQUEST Request = NULL;
    CD_DEFERRED Deferred;
    KIRQL OldIrql;

    *Information = 0;
    if (InLength < sizeof(Params) || OutLength < sizeof(Reply))
        return STATUS_INVALID_PARAMETER;

    RtlCopyMemory(&Params, Buffer, sizeof(Params));
    RtlZeroMemory(&Reply, sizeof(Reply));
    CdInitDeferred(&Deferred);

    KeAcquireSpinLock(&CdLock, &OldIrql);

    if (!Server->Console)
    {
        CdUpdateServerEvent(Server);
        KeReleaseSpinLock(&CdLock, OldIrql);
        return STATUS_INVALID_HANDLE;
    }

    CdSignalConsole(Server->Console, Params.Signal != 0);

    if (Params.Read)
    {
        if (IsListEmpty(&Server->ReadQueue))
        {
            CdUpdateServerEvent(Server);
            KeReleaseSpinLock(&CdLock, OldIrql);
            return STATUS_INVALID_HANDLE;
        }
        Request = CONTAINING_RECORD(RemoveHeadList(&Server->ReadQueue), CD_REQUEST, Entry);
        while (!IsListEmpty(&Server->ReadQueue))
            InsertTailList(&Server->Queue, RemoveHeadList(&Server->ReadQueue));
    }
    else if (Server->Busy)
    {
        Request = CONTAINING_RECORD(RemoveHeadList(&Server->Queue), CD_REQUEST, Entry);
    }

    if (Request)
    {
        CdReplyRequest(Request, (NTSTATUS)Params.Status, Buffer + sizeof(Params),
                       InLength - sizeof(Params), &Deferred);
        if (Params.Read)
        {
            CdUpdateServerEvent(Server);
            KeReleaseSpinLock(&CdLock, OldIrql);
            RtlCopyMemory(Buffer, &Reply, sizeof(Reply));
            *Information = sizeof(Reply);
            CdRunDeferred(&Deferred);
            return STATUS_SUCCESS;
        }
        Server->Busy = FALSE;
    }

    if (!IsListEmpty(&Server->Queue) && !IsListEmpty(&Server->ReadQueue))
    {
        Request = CONTAINING_RECORD(Server->Queue.Flink, CD_REQUEST, Entry);
        if (CdIsBlockingRead(Request->Code))
        {
            PLIST_ENTRY Entry = Server->Queue.Flink;

            while (Entry != &Server->Queue)
            {
                PLIST_ENTRY Next = Entry->Flink;

                Request = CONTAINING_RECORD(Entry, CD_REQUEST, Entry);
                if (CdIsBlockingRead(Request->Code))
                {
                    RemoveEntryList(Entry);
                    InsertTailList(&Server->ReadQueue, Entry);
                }
                Entry = Next;
            }
        }
    }

    *Information = sizeof(Reply);
    if (!IsListEmpty(&Server->Queue))
    {
        Request = CONTAINING_RECORD(Server->Queue.Flink, CD_REQUEST, Entry);
        if (OutLength - sizeof(Reply) >= Request->InSize)
        {
            Reply.Status = STATUS_SUCCESS;
            Reply.Code = Request->Code;
            Reply.Output = Request->Output;
            Reply.OutSize = Request->OutSize;
            if (Request->InSize)
                RtlCopyMemory(Buffer + sizeof(Reply), Request->InData, Request->InSize);
            *Information = sizeof(Reply) + Request->InSize;
            Request->InData = NULL;
            Request->Delivered = TRUE;
            if (CdIsBlockingRead(Request->Code))
            {
                RemoveEntryList(&Request->Entry);
                InsertTailList(&Server->ReadQueue, &Request->Entry);
            }
            else
            {
                Server->Busy = TRUE;
            }
        }
        else
        {
            Reply.Status = STATUS_BUFFER_OVERFLOW;
            Reply.OutSize = Request->InSize;
        }
    }
    else
    {
        Reply.Status = STATUS_PENDING;
    }

    CdUpdateServerEvent(Server);
    KeReleaseSpinLock(&CdLock, OldIrql);

    RtlCopyMemory(Buffer, &Reply, sizeof(Reply));
    CdRunDeferred(&Deferred);
    return STATUS_SUCCESS;
}

static
NTSTATUS
CdServerIoctl(
    _In_ PCD_SERVER Server,
    _In_ PIRP Irp,
    _In_ PIO_STACK_LOCATION Stack)
{
    ULONG Code = Stack->Parameters.DeviceIoControl.IoControlCode;
    KIRQL OldIrql;

    switch (Code)
    {
        case IOCTL_CONDRV_GET_NEXT_REQUEST:
            return CdServerGetNext(Server, Irp->AssociatedIrp.SystemBuffer,
                                   Stack->Parameters.DeviceIoControl.InputBufferLength,
                                   Stack->Parameters.DeviceIoControl.OutputBufferLength,
                                   &Irp->IoStatus.Information);

        case IOCTL_CONDRV_CTRL_EVENT:
        {
            PCD_CTRL_EVENT Event = Irp->AssociatedIrp.SystemBuffer;

            if (Stack->Parameters.DeviceIoControl.InputBufferLength != sizeof(*Event))
                return STATUS_INVALID_PARAMETER;
            if (Event->Event < CD_CTRL_C_EVENT || Event->Event > CD_CTRL_SHUTDOWN_EVENT)
                return STATUS_INVALID_PARAMETER;
            KeAcquireSpinLock(&CdLock, &OldIrql);
            if (!Server->Console)
            {
                KeReleaseSpinLock(&CdLock, OldIrql);
                return STATUS_INVALID_HANDLE;
            }
            return CdPropagateCtrl(Server->Console, Event->Event, Event->GroupId, OldIrql);
        }

        case IOCTL_CONDRV_SETUP_INPUT:
            return STATUS_NOT_SUPPORTED;

        default:
            return STATUS_INVALID_HANDLE;
    }
}

static
NTSTATUS
CdConnectionIoctl(
    _In_ PFILE_OBJECT FileObject,
    _In_ PIRP Irp,
    _In_ PIO_STACK_LOCATION Stack)
{
    ULONG Code = Stack->Parameters.DeviceIoControl.IoControlCode;
    ULONG InLength = Stack->Parameters.DeviceIoControl.InputBufferLength;
    PCD_PROCESS Attached, Target;
    PEPROCESS Process;
    NTSTATUS Status;
    KIRQL OldIrql;

    switch (Code)
    {
        case IOCTL_CONDRV_BIND_PID:
        {
            ULONG Pid;

            if (InLength != sizeof(ULONG))
                return STATUS_INVALID_PARAMETER;
            Pid = *(PULONG)Irp->AssociatedIrp.SystemBuffer;
            if (Pid == CD_ATTACH_PARENT_PROCESS)
                Pid = HandleToULong(PsGetProcessInheritedFromUniqueProcessId(PsGetCurrentProcess()));
            Status = PsLookupProcessByProcessId(ULongToHandle(Pid), &Process);
            if (!NT_SUCCESS(Status))
                return STATUS_INVALID_PARAMETER;

            KeAcquireSpinLock(&CdLock, &OldIrql);
            if (CdFindProcess(PsGetCurrentProcess()))
            {
                Status = STATUS_INVALID_HANDLE;
            }
            else
            {
                Target = CdFindProcess(Process);
                if (Target)
                    Status = CdAttachProcess(PsGetCurrentProcess(), Target->Console, FileObject);
                else
                    Status = STATUS_ACCESS_DENIED;
            }
            KeReleaseSpinLock(&CdLock, OldIrql);
            ObDereferenceObject(Process);
            return Status;
        }

        case IOCTL_CONDRV_SET_CTRL_ROUTINE:
        {
            PCONDRV_CTRL_ROUTINE Ctrl = Irp->AssociatedIrp.SystemBuffer;

            if (InLength != sizeof(*Ctrl))
                return STATUS_INVALID_PARAMETER;
            KeAcquireSpinLock(&CdLock, &OldIrql);
            Attached = CdFindProcess(PsGetCurrentProcess());
            if (Attached)
            {
                Attached->CtrlRoutine = Ctrl->Routine;
                Attached->GroupId = Ctrl->GroupId;
                Status = STATUS_SUCCESS;
            }
            else
            {
                Status = STATUS_INVALID_HANDLE;
            }
            KeReleaseSpinLock(&CdLock, OldIrql);
            return Status;
        }

        default:
            return STATUS_NOT_SUPPORTED;
    }
}

static
NTSTATUS
CdCompleteDispatch(
    _In_ PIRP Irp,
    _In_ NTSTATUS Status)
{
    if (Status != STATUS_PENDING)
    {
        Irp->IoStatus.Status = Status;
        if (NT_ERROR(Status))
            Irp->IoStatus.Information = 0;
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
    }
    return Status;
}

static
BOOLEAN
CdNameIs(
    _In_ PCUNICODE_STRING Name,
    _In_ PCUNICODE_STRING Expected)
{
    return RtlEqualUnicodeString(Name, Expected, TRUE);
}

static
NTSTATUS
NTAPI
CdDispatchCreate(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    PIO_STACK_LOCATION Stack = IoGetCurrentIrpStackLocation(Irp);
    PFILE_OBJECT FileObject = Stack->FileObject;
    PUNICODE_STRING Name = &FileObject->FileName;
    PCD_FILE Related = FileObject->RelatedFileObject ? FileObject->RelatedFileObject->FsContext : NULL;
    ACCESS_MASK Access = Stack->Parameters.Create.SecurityContext->DesiredAccess;
    PEPROCESS Process = PsGetCurrentProcess();
    PCD_PROCESS Attached;
    PCD_SERVER Server = NULL;
    CD_DEFERRED Deferred;
    NTSTATUS Status = STATUS_SUCCESS;
    PCD_FILE File;
    KIRQL OldIrql;

    UNREFERENCED_PARAMETER(DeviceObject);

    File = ExAllocatePoolWithTag(NonPagedPool, sizeof(*File), CD_TAG);
    if (!File)
        return CdCompleteDispatch(Irp, STATUS_NO_MEMORY);
    RtlZeroMemory(File, sizeof(*File));
    File->FileObject = FileObject;
    InitializeListHead(&File->MirrorEntry);

    if (!Related && CdNameIs(Name, &CdNameServer))
    {
        Server = ExAllocatePoolWithTag(NonPagedPool, sizeof(*Server), CD_TAG);
        if (!Server)
        {
            ExFreePoolWithTag(File, CD_TAG);
            return CdCompleteDispatch(Irp, STATUS_NO_MEMORY);
        }
        RtlZeroMemory(Server, sizeof(*Server));
        InitializeListHead(&Server->Queue);
        InitializeListHead(&Server->ReadQueue);
        Server->FileObject = FileObject;
        Server->Process = Process;
    }

    if (Access & GENERIC_READ)
        Access |= FILE_READ_DATA;
    if (Access & GENERIC_WRITE)
        Access |= FILE_WRITE_DATA;

    CdInitDeferred(&Deferred);
    KeAcquireSpinLock(&CdLock, &OldIrql);
    Attached = CdFindProcess(Process);

    if (Related && !Name->Length)
    {
        Status = STATUS_NOT_FOUND;
    }
    else if (Related)
    {
        if (Related->Type == CdFileServer && CdNameIs(Name, &CdNameRelReference))
        {
            if (Related->Server->Console)
            {
                Status = STATUS_INVALID_HANDLE;
            }
            else
            {
                File->Type = CdFileConsole;
                File->Console = CdCreateConsole(Related->Server);
                if (!File->Console)
                    Status = STATUS_NO_MEMORY;
            }
        }
        else if (Related->Type == CdFileConsole && CdNameIs(Name, &CdNameRelConnection))
        {
            File->Type = CdFileConnection;
            Status = CdAttachProcess(Process, Related->Console, FileObject);
        }
        else if (Related->Type == CdFileConnection && CdNameIs(Name, &CdNameRelReference))
        {
            if (!Attached)
            {
                Status = STATUS_INVALID_HANDLE;
            }
            else
            {
                File->Type = CdFileConsole;
                File->Console = Attached->Console;
                File->Console->RefCount++;
            }
        }
        else
        {
            Status = STATUS_OBJECT_NAME_NOT_FOUND;
        }
    }
    else if (Server)
    {
        File->Type = CdFileServer;
        File->Server = Server;
    }
    else if (CdNameIs(Name, &CdNameConnection))
    {
        File->Type = CdFileConnection;
    }
    else if (CdNameIs(Name, &CdNameInput))
    {
        File->Type = CdFileInput;
    }
    else if (CdNameIs(Name, &CdNameOutput))
    {
        File->Type = CdFileOutput;
    }
    else if (CdNameIs(Name, &CdNameCurrentIn))
    {
        if (!Attached)
        {
            Status = STATUS_INVALID_HANDLE;
        }
        else
        {
            File->Type = CdFileConsole;
            File->Console = Attached->Console;
            File->Console->RefCount++;
        }
    }
    else if (CdNameIs(Name, &CdNameCurrentOut))
    {
        if (!Attached || !Attached->Console->Active)
        {
            Status = STATUS_INVALID_HANDLE;
        }
        else
        {
            File->Type = CdFileScreen;
            File->Screen = Attached->Console->Active;
            File->Screen->RefCount++;
        }
    }
    else if (CdNameIs(Name, &CdNameScreenBuffer))
    {
        if (!Attached)
        {
            Status = STATUS_INVALID_HANDLE;
        }
        else
        {
            File->Type = CdFileScreen;
            File->Screen = CdCreateScreen(Attached->Console);
            if (!File->Screen)
                Status = STATUS_NO_MEMORY;
        }
    }
    else if (!Name->Length || CdNameIs(Name, &CdNameRoot) || CdNameIs(Name, &CdNameConsole))
    {
        BOOLEAN IsOutput = (Access & FILE_WRITE_DATA) != 0;

        if (!Attached)
        {
            Status = STATUS_INVALID_HANDLE;
        }
        else if (IsOutput && (Access & FILE_READ_DATA))
        {
            Status = STATUS_INVALID_PARAMETER;
        }
        else if (IsOutput)
        {
            if (!Attached->Console->Active)
            {
                Status = STATUS_INVALID_HANDLE;
            }
            else
            {
                File->Type = CdFileScreen;
                File->Screen = Attached->Console->Active;
                File->Screen->RefCount++;
            }
        }
        else
        {
            File->Type = CdFileConsole;
            File->Console = Attached->Console;
            File->Console->RefCount++;
        }
    }
    else
    {
        Status = STATUS_OBJECT_NAME_NOT_FOUND;
    }

    if (NT_SUCCESS(Status))
    {
        switch (File->Type)
        {
            case CdFileConsole:
                CdSetMirror(File, File->Console);
                break;
            case CdFileScreen:
                CdSetMirror(File, File->Screen->Console);
                break;
            case CdFileInput:
            case CdFileOutput:
                CdSetMirror(File, Attached ? Attached->Console : NULL);
                break;
            default:
                break;
        }
        if (File->Type != CdFileServer && File->Type != CdFileConnection)
            CdUpdateFileEvent(File);
    }
    KeReleaseSpinLock(&CdLock, OldIrql);
    CdRunDeferred(&Deferred);

    if (!NT_SUCCESS(Status))
    {
        if (Server)
            ExFreePoolWithTag(Server, CD_TAG);
        ExFreePoolWithTag(File, CD_TAG);
        return CdCompleteDispatch(Irp, Status);
    }

    FileObject->FsContext = File;
    Irp->IoStatus.Information = FILE_OPENED;
    if (File->Type == CdFileServer)
    {
        FileObject->Flags &= ~(FO_SYNCHRONOUS_IO | FO_ALERTABLE_IO);
        FileObject->Flags |= FO_SKIP_SET_EVENT;
        KeClearEvent(&FileObject->Event);
    }
    else if (File->Type != CdFileConnection)
    {
        KeInitializeEvent(&FileObject->Event, SynchronizationEvent, FALSE);
        Irp->IoStatus.Status = STATUS_SUCCESS;
        IoMarkIrpPending(Irp);
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
        FileObject->Flags |= FO_SKIP_SET_EVENT;
        return STATUS_PENDING;
    }

    return CdCompleteDispatch(Irp, STATUS_SUCCESS);
}

static
NTSTATUS
NTAPI
CdDispatchCleanup(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    PFILE_OBJECT FileObject = IoGetCurrentIrpStackLocation(Irp)->FileObject;
    PCD_FILE File = FileObject->FsContext;
    CD_DEFERRED Deferred;
    KIRQL OldIrql;

    UNREFERENCED_PARAMETER(DeviceObject);

    if (!File)
        return CdCompleteDispatch(Irp, STATUS_SUCCESS);

    CdInitDeferred(&Deferred);
    KeAcquireSpinLock(&CdLock, &OldIrql);
    if (File->Type == CdFileServer)
    {
        PCD_CONSOLE Console = File->Server->Console;

        if (Console)
        {
            PLIST_ENTRY Entry;

            for (Entry = CdProcessList.Flink; Entry != &CdProcessList; Entry = Entry->Flink)
            {
                PCD_PROCESS Attached = CONTAINING_RECORD(Entry, CD_PROCESS, Entry);
                if (Attached->Console == Console && Attached->Process != File->Server->Process)
                    CdDeferProcess(Attached->Process, TRUE, &Deferred);
            }
        }
        CdDisconnectServer(File->Server, &Deferred);
    }
    else if (File->Type == CdFileConnection)
    {
        CdDetachConnection(FileObject, &Deferred);
    }
    KeReleaseSpinLock(&CdLock, OldIrql);
    CdRunDeferred(&Deferred);

    return CdCompleteDispatch(Irp, STATUS_SUCCESS);
}

static
NTSTATUS
NTAPI
CdDispatchClose(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    PFILE_OBJECT FileObject = IoGetCurrentIrpStackLocation(Irp)->FileObject;
    PCD_FILE File = FileObject->FsContext;
    CD_DEFERRED Deferred;
    KIRQL OldIrql;

    UNREFERENCED_PARAMETER(DeviceObject);

    if (!File)
        return CdCompleteDispatch(Irp, STATUS_SUCCESS);

    CdInitDeferred(&Deferred);
    KeAcquireSpinLock(&CdLock, &OldIrql);
    CdSetMirror(File, NULL);
    switch (File->Type)
    {
        case CdFileServer:
            CdDisconnectServer(File->Server, &Deferred);
            File->Server->FileObject = NULL;
            if (File->Server->Scratch)
                ExFreePoolWithTag(File->Server->Scratch, CD_TAG);
            ExFreePoolWithTag(File->Server, CD_TAG);
            break;
        case CdFileConsole:
            CdDereferenceConsole(File->Console, &Deferred);
            break;
        case CdFileScreen:
            CdDereferenceScreen(File->Screen);
            break;
        case CdFileConnection:
            CdDetachConnection(FileObject, &Deferred);
            break;
        default:
            break;
    }
    FileObject->FsContext = NULL;
    KeReleaseSpinLock(&CdLock, OldIrql);
    CdRunDeferred(&Deferred);

    ExFreePoolWithTag(File, CD_TAG);
    return CdCompleteDispatch(Irp, STATUS_SUCCESS);
}

static
PCD_CONSOLE
CdResolveInput(
    _In_ PCD_FILE File)
{
    PCD_CONSOLE Console;

    Console = CdCurrentConsole();
    if (File->Type == CdFileConsole)
        return (File->Console == Console) ? Console : NULL;
    CdSetMirror(File, Console);
    return Console;
}

static
PCD_SCREEN
CdResolveOutput(
    _In_ PCD_FILE File)
{
    PCD_CONSOLE Console;

    if (File->Type == CdFileScreen)
    {
        if (!File->Screen->Console || File->Screen->Console != CdCurrentConsole())
            return NULL;
        return File->Screen;
    }
    Console = CdCurrentConsole();
    CdSetMirror(File, Console);
    return Console ? Console->Active : NULL;
}

static
NTSTATUS
NTAPI
CdDispatchDeviceControl(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    PIO_STACK_LOCATION Stack = IoGetCurrentIrpStackLocation(Irp);
    PCD_FILE File = Stack->FileObject->FsContext;
    BOOLEAN Unlocked = FALSE;
    NTSTATUS Status;
    KIRQL OldIrql;

    UNREFERENCED_PARAMETER(DeviceObject);

    Irp->IoStatus.Information = 0;
    if (!File)
        return CdCompleteDispatch(Irp, STATUS_INVALID_HANDLE);

    switch (File->Type)
    {
        case CdFileServer:
            Status = CdServerIoctl(File->Server, Irp, Stack);
            if (Status != STATUS_PENDING)
            {
                KeAcquireSpinLock(&CdLock, &OldIrql);
                CdUpdateServerEvent(File->Server);
                KeReleaseSpinLock(&CdLock, OldIrql);
            }
            return CdCompleteDispatch(Irp, Status);

        case CdFileConnection:
            return CdCompleteDispatch(Irp, CdConnectionIoctl(Stack->FileObject, Irp, Stack));

        case CdFileConsole:
        case CdFileInput:
        {
            PCD_CONSOLE Console;

            KeAcquireSpinLock(&CdLock, &OldIrql);
            Console = CdResolveInput(File);
            if (!Console)
                Status = STATUS_INVALID_HANDLE;
            else
                Status = CdConsoleIoctl(File, Console, Irp, Stack, OldIrql, &Unlocked);
            if (!Unlocked)
            {
                CdUpdateFileEvent(File);
                KeReleaseSpinLock(&CdLock, OldIrql);
            }
            return CdCompleteDispatch(Irp, Status);
        }

        case CdFileScreen:
        case CdFileOutput:
        {
            PCD_SCREEN Screen;

            KeAcquireSpinLock(&CdLock, &OldIrql);
            Screen = CdResolveOutput(File);
            Status = Screen ? CdScreenIoctl(File, Screen, Irp, Stack) : STATUS_INVALID_HANDLE;
            CdUpdateFileEvent(File);
            KeReleaseSpinLock(&CdLock, OldIrql);
            return CdCompleteDispatch(Irp, Status);
        }

        default:
            return CdCompleteDispatch(Irp, STATUS_INVALID_DEVICE_REQUEST);
    }
}

static
NTSTATUS
NTAPI
CdDispatchRead(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    PIO_STACK_LOCATION Stack = IoGetCurrentIrpStackLocation(Irp);
    PCD_FILE File = Stack->FileObject->FsContext;
    PCD_CONSOLE Console;
    NTSTATUS Status;
    KIRQL OldIrql;

    UNREFERENCED_PARAMETER(DeviceObject);

    Irp->IoStatus.Information = 0;
    if (!File || (File->Type != CdFileConsole && File->Type != CdFileInput))
        return CdCompleteDispatch(Irp, STATUS_OBJECT_TYPE_MISMATCH);

    KeAcquireSpinLock(&CdLock, &OldIrql);
    Console = CdResolveInput(File);
    if (!Console || !Console->Server)
        Status = STATUS_INVALID_HANDLE;
    else
        Status = CdQueueIrp(Console->Server, File, Irp, IOCTL_CONDRV_READ_FILE, 0, NULL, 0,
                            Stack->Parameters.Read.Length);
    CdUpdateFileEvent(File);
    KeReleaseSpinLock(&CdLock, OldIrql);
    return CdCompleteDispatch(Irp, Status);
}

static
NTSTATUS
NTAPI
CdDispatchWrite(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    PIO_STACK_LOCATION Stack = IoGetCurrentIrpStackLocation(Irp);
    PCD_FILE File = Stack->FileObject->FsContext;
    PCD_SCREEN Screen;
    NTSTATUS Status;
    KIRQL OldIrql;

    UNREFERENCED_PARAMETER(DeviceObject);

    Irp->IoStatus.Information = 0;
    if (!File || (File->Type != CdFileScreen && File->Type != CdFileOutput))
        return CdCompleteDispatch(Irp, STATUS_OBJECT_TYPE_MISMATCH);

    KeAcquireSpinLock(&CdLock, &OldIrql);
    Screen = CdResolveOutput(File);
    if (!Screen || !Screen->Console || !Screen->Console->Server)
        Status = STATUS_INVALID_HANDLE;
    else
        Status = CdQueueIrp(Screen->Console->Server, File, Irp, IOCTL_CONDRV_WRITE_FILE, Screen->Id,
                            Irp->AssociatedIrp.SystemBuffer, Stack->Parameters.Write.Length, 0);
    CdUpdateFileEvent(File);
    KeReleaseSpinLock(&CdLock, OldIrql);
    return CdCompleteDispatch(Irp, Status);
}

static
NTSTATUS
NTAPI
CdDispatchFlush(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    PCD_FILE File = IoGetCurrentIrpStackLocation(Irp)->FileObject->FsContext;
    PCD_CONSOLE Console;
    NTSTATUS Status;
    KIRQL OldIrql;

    UNREFERENCED_PARAMETER(DeviceObject);

    Irp->IoStatus.Information = 0;
    if (!File || (File->Type != CdFileConsole && File->Type != CdFileInput))
        return CdCompleteDispatch(Irp, STATUS_OBJECT_TYPE_MISMATCH);

    KeAcquireSpinLock(&CdLock, &OldIrql);
    Console = CdResolveInput(File);
    if (!Console || !Console->Server)
    {
        Status = STATUS_INVALID_HANDLE;
    }
    else
    {
        CdQueueNotify(Console->Server, IOCTL_CONDRV_FLUSH, 0);
        Status = STATUS_SUCCESS;
    }
    CdUpdateFileEvent(File);
    KeReleaseSpinLock(&CdLock, OldIrql);
    return CdCompleteDispatch(Irp, Status);
}

static
NTSTATUS
NTAPI
CdDispatchQueryVolume(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    PIO_STACK_LOCATION Stack = IoGetCurrentIrpStackLocation(Irp);
    PFILE_FS_DEVICE_INFORMATION Info = Irp->AssociatedIrp.SystemBuffer;

    UNREFERENCED_PARAMETER(DeviceObject);

    Irp->IoStatus.Information = 0;
    if (Stack->Parameters.QueryVolume.FsInformationClass != FileFsDeviceInformation)
        return CdCompleteDispatch(Irp, STATUS_NOT_IMPLEMENTED);
    if (Stack->Parameters.QueryVolume.Length < sizeof(*Info))
        return CdCompleteDispatch(Irp, STATUS_BUFFER_TOO_SMALL);
    Info->DeviceType = FILE_DEVICE_CONSOLE;
    Info->Characteristics = FILE_DEVICE_ALLOW_APPCONTAINER_TRAVERSAL;
    Irp->IoStatus.Information = sizeof(*Info);
    return CdCompleteDispatch(Irp, STATUS_SUCCESS);
}

static
NTSTATUS
NTAPI
CdDispatchInvalid(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    UNREFERENCED_PARAMETER(DeviceObject);

    Irp->IoStatus.Information = 0;
    return CdCompleteDispatch(Irp, STATUS_INVALID_DEVICE_REQUEST);
}

static
BOOLEAN
CdFastIoServer(
    _In_ PCD_SERVER Server,
    _In_opt_ PVOID InputBuffer,
    _In_ ULONG InputBufferLength,
    _Out_opt_ PVOID OutputBuffer,
    _In_ ULONG OutputBufferLength,
    _In_ ULONG IoControlCode,
    _Out_ PIO_STATUS_BLOCK IoStatus)
{
    KPROCESSOR_MODE PreviousMode = ExGetPreviousMode();
    ULONG Size = max(InputBufferLength, OutputBufferLength);
    ULONG_PTR Information = 0;
    BOOLEAN Scratch = FALSE;
    PUCHAR Buffer;
    NTSTATUS Status;

    if (IoControlCode != IOCTL_CONDRV_GET_NEXT_REQUEST || !Size)
        return FALSE;

    if (!InterlockedExchange(&Server->ScratchBusy, 1))
    {
        if (Server->ScratchSize < Size)
        {
            if (Server->Scratch)
                ExFreePoolWithTag(Server->Scratch, CD_TAG);
            Server->Scratch = ExAllocatePoolWithTag(NonPagedPool, Size, CD_TAG);
            Server->ScratchSize = Server->Scratch ? Size : 0;
        }
        if (Server->Scratch)
            Scratch = TRUE;
        else
            InterlockedExchange(&Server->ScratchBusy, 0);
    }
    Buffer = Scratch ? Server->Scratch : ExAllocatePoolWithTag(NonPagedPool, Size, CD_TAG);
    if (!Buffer)
        return FALSE;

    Status = STATUS_SUCCESS;
    _SEH2_TRY
    {
        if (PreviousMode != KernelMode)
        {
            if (InputBufferLength)
                ProbeForRead(InputBuffer, InputBufferLength, sizeof(UCHAR));
            if (OutputBufferLength)
                ProbeForWrite(OutputBuffer, OutputBufferLength, sizeof(UCHAR));
        }
        if (InputBufferLength)
            RtlCopyMemory(Buffer, InputBuffer, InputBufferLength);
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Status = _SEH2_GetExceptionCode();
    }
    _SEH2_END;

    if (NT_SUCCESS(Status))
        Status = CdServerGetNext(Server, Buffer, InputBufferLength, OutputBufferLength, &Information);
    if (NT_ERROR(Status))
        Information = 0;
    if (Information)
    {
        _SEH2_TRY
        {
            RtlCopyMemory(OutputBuffer, Buffer, Information);
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            Status = _SEH2_GetExceptionCode();
            Information = 0;
        }
        _SEH2_END;
    }

    if (Scratch)
        InterlockedExchange(&Server->ScratchBusy, 0);
    else
        ExFreePoolWithTag(Buffer, CD_TAG);

    IoStatus->Status = Status;
    IoStatus->Information = Information;
    return TRUE;
}

static
BOOLEAN
NTAPI
CdFastIoDeviceControl(
    _In_ PFILE_OBJECT FileObject,
    _In_ BOOLEAN Wait,
    _In_opt_ PVOID InputBuffer,
    _In_ ULONG InputBufferLength,
    _Out_opt_ PVOID OutputBuffer,
    _In_ ULONG OutputBufferLength,
    _In_ ULONG IoControlCode,
    _Out_ PIO_STATUS_BLOCK IoStatus,
    _In_ PDEVICE_OBJECT DeviceObject)
{
    PCD_FILE File = FileObject->FsContext;
    KPROCESSOR_MODE PreviousMode = ExGetPreviousMode();
    PCD_REQUEST Request;
    PCD_SERVER Server = NULL;
    ULONG_PTR Information = 0;
    BOOLEAN Queued = FALSE;
    LARGE_INTEGER Timeout;
    ULONG InSpace;
    NTSTATUS Status;
    ULONG Output = 0;
    KIRQL OldIrql;

    UNREFERENCED_PARAMETER(Wait);
    UNREFERENCED_PARAMETER(DeviceObject);

    if (!File || File->Type == CdFileConnection)
        return FALSE;
    if (File->Type == CdFileServer)
    {
        return CdFastIoServer(File->Server, InputBuffer, InputBufferLength, OutputBuffer,
                              OutputBufferLength, IoControlCode, IoStatus);
    }
    if (DEVICE_TYPE_FROM_CTL_CODE(IoControlCode) != FILE_DEVICE_CONSOLE ||
        METHOD_FROM_CTL_CODE(IoControlCode) != METHOD_BUFFERED ||
        CdIsBlockingRead(IoControlCode) || IoControlCode == IOCTL_CONDRV_CTRL_EVENT)
    {
        return FALSE;
    }
    if (InputBufferLength > MAXUSHORT * 0x100 || OutputBufferLength > MAXUSHORT * 0x100)
        return FALSE;

    InSpace = ALIGN_UP_BY(InputBufferLength, sizeof(ULONGLONG));
    Request = ExAllocatePoolWithTag(NonPagedPool, sizeof(*Request) + InSpace + OutputBufferLength, CD_TAG);
    if (!Request)
        return FALSE;
    RtlZeroMemory(Request, sizeof(*Request));
    if (InputBufferLength)
        Request->InData = Request + 1;
    if (OutputBufferLength)
        Request->OutBuffer = (PUCHAR)(Request + 1) + InSpace;

    Status = STATUS_SUCCESS;
    _SEH2_TRY
    {
        if (PreviousMode != KernelMode)
        {
            if (InputBufferLength)
                ProbeForRead(InputBuffer, InputBufferLength, sizeof(UCHAR));
            if (OutputBufferLength)
                ProbeForWrite(OutputBuffer, OutputBufferLength, sizeof(UCHAR));
        }
        if (InputBufferLength)
            RtlCopyMemory(Request->InData, InputBuffer, InputBufferLength);
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Status = _SEH2_GetExceptionCode();
    }
    _SEH2_END;

    if (NT_SUCCESS(Status))
    {
        KeAcquireSpinLock(&CdLock, &OldIrql);
        if (File->Type == CdFileConsole || File->Type == CdFileInput)
        {
            PCD_CONSOLE Console = CdResolveInput(File);

            if (!Console)
                Status = STATUS_INVALID_HANDLE;
            else if (IoControlCode == IOCTL_CONDRV_GET_PROCESS_LIST)
                Status = CdGetProcessList(Console, Request->OutBuffer, OutputBufferLength, &Information);
            else if (!Console->Server)
                Status = STATUS_INVALID_HANDLE;
            else
                Server = Console->Server;
        }
        else
        {
            PCD_SCREEN Screen = CdResolveOutput(File);

            if (!Screen || !Screen->Console)
            {
                Status = STATUS_INVALID_HANDLE;
            }
            else if (IoControlCode == IOCTL_CONDRV_ACTIVATE)
            {
                CdSetActive(Screen->Console, Screen);
            }
            else if (!Screen->Console->Server)
            {
                Status = STATUS_INVALID_HANDLE;
            }
            else
            {
                Server = Screen->Console->Server;
                Output = Screen->Id;
            }
        }

        if (Server)
        {
            Request->Fast = TRUE;
            Request->Code = IoControlCode;
            Request->Output = Output;
            Request->InSize = InputBufferLength;
            Request->OutSize = OutputBufferLength;
            KeInitializeEvent(&Request->Done, NotificationEvent, FALSE);
            InsertTailList(&Server->Queue, &Request->Entry);
            CdUpdateServerEvent(Server);
            Queued = TRUE;
        }
        KeReleaseSpinLock(&CdLock, OldIrql);
    }

    if (Queued)
    {
        KeEnterCriticalRegion();
        for (;;)
        {
            Timeout.QuadPart = -10000000LL;
            if (KeWaitForSingleObject(&Request->Done, Executive, KernelMode, FALSE, &Timeout) == STATUS_SUCCESS)
                break;
            if (!PsIsThreadTerminating(PsGetCurrentThread()))
                continue;
            KeAcquireSpinLock(&CdLock, &OldIrql);
            if (KeReadStateEvent(&Request->Done))
            {
                KeReleaseSpinLock(&CdLock, OldIrql);
                break;
            }
            if (Request->Delivered)
            {
                Request->Abandoned = TRUE;
            }
            else
            {
                RemoveEntryList(&Request->Entry);
                CdFreeRequest(Request);
            }
            KeReleaseSpinLock(&CdLock, OldIrql);
            Request = NULL;
            Status = STATUS_THREAD_IS_TERMINATING;
            break;
        }
        KeLeaveCriticalRegion();

        if (Request)
        {
            Status = Request->Status;
            Information = Request->Information;
        }
    }

    if (NT_ERROR(Status))
        Information = 0;
    if (Request && Information && Request->OutBuffer)
    {
        _SEH2_TRY
        {
            RtlCopyMemory(OutputBuffer, Request->OutBuffer, Information);
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            Status = _SEH2_GetExceptionCode();
            Information = 0;
        }
        _SEH2_END;
    }
    if (Request)
        CdFreeRequest(Request);

    KeAcquireSpinLock(&CdLock, &OldIrql);
    CdSyncFileEvent(File);
    KeReleaseSpinLock(&CdLock, OldIrql);

    IoStatus->Status = Status;
    IoStatus->Information = Information;
    return TRUE;
}

static FAST_IO_DISPATCH CdFastIoDispatch;

static
VOID
CdCreateLink(
    _In_ PCWSTR Link,
    _In_ PCWSTR Target)
{
    UNICODE_STRING LinkName, TargetName;

    RtlInitUnicodeString(&LinkName, Link);
    RtlInitUnicodeString(&TargetName, Target);
    IoCreateSymbolicLink(&LinkName, &TargetName);
}

NTSTATUS
NTAPI
DriverEntry(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PUNICODE_STRING RegistryPath)
{
    UNICODE_STRING DeviceName = RTL_CONSTANT_STRING(L"\\Device\\ConDrv");
    PDEVICE_OBJECT DeviceObject;
    NTSTATUS Status;
    ULONG Index;

    UNREFERENCED_PARAMETER(RegistryPath);

    KeInitializeSpinLock(&CdLock);
    InitializeListHead(&CdProcessList);
    for (Index = 0; Index < CD_PROCESS_BUCKETS; Index++)
        InitializeListHead(&CdProcessHash[Index]);

    for (Index = 0; Index <= IRP_MJ_MAXIMUM_FUNCTION; Index++)
        DriverObject->MajorFunction[Index] = CdDispatchInvalid;
    DriverObject->MajorFunction[IRP_MJ_CREATE] = CdDispatchCreate;
    DriverObject->MajorFunction[IRP_MJ_CLEANUP] = CdDispatchCleanup;
    DriverObject->MajorFunction[IRP_MJ_CLOSE] = CdDispatchClose;
    DriverObject->MajorFunction[IRP_MJ_READ] = CdDispatchRead;
    DriverObject->MajorFunction[IRP_MJ_WRITE] = CdDispatchWrite;
    DriverObject->MajorFunction[IRP_MJ_DEVICE_CONTROL] = CdDispatchDeviceControl;
    DriverObject->MajorFunction[IRP_MJ_FLUSH_BUFFERS] = CdDispatchFlush;
    DriverObject->MajorFunction[IRP_MJ_QUERY_VOLUME_INFORMATION] = CdDispatchQueryVolume;
    CdFastIoDispatch.SizeOfFastIoDispatch = sizeof(CdFastIoDispatch);
    CdFastIoDispatch.FastIoDeviceControl = CdFastIoDeviceControl;
    DriverObject->FastIoDispatch = &CdFastIoDispatch;

    Status = IoCreateDevice(DriverObject, 0, &DeviceName, FILE_DEVICE_CONSOLE,
                            FILE_DEVICE_ALLOW_APPCONTAINER_TRAVERSAL, FALSE, &DeviceObject);
    if (!NT_SUCCESS(Status))
        return Status;
    DeviceObject->Flags |= DO_BUFFERED_IO;
    DeviceObject->Flags &= ~DO_DEVICE_INITIALIZING;

    CdCreateLink(L"\\DosDevices\\CON", L"\\Device\\ConDrv\\Console");
    CdCreateLink(L"\\DosDevices\\CONIN$", L"\\Device\\ConDrv\\CurrentIn");
    CdCreateLink(L"\\DosDevices\\CONOUT$", L"\\Device\\ConDrv\\CurrentOut");
    return STATUS_SUCCESS;
}
