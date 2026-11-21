/*
 * PROJECT:     LiberNT NetIO driver
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Hands WSK name resolution requests to the DNS Client service
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntifs.h>
#include <wdmsec.h>
#include <ws2def.h>
#include <ws2ipdef.h>
#include <wsk.h>

#include "wskname.h"

#define NDEBUG
#include <reactos/debug.h>

typedef struct _WSKNAME_FILE
{
    BOOLEAN Serving;
    ULONG ServerBit;
} WSKNAME_FILE, *PWSKNAME_FILE;

typedef struct _WSKNAME_PENDING
{
    LIST_ENTRY Link;
    ULONG Id;
    BOOLEAN Dispatched;
    BOOLEAN Answered;
    ULONG Declined;
    PFILE_OBJECT Server;
    NTSTATUS Status;
    KEVENT Done;
    WSKNAME_REQUEST Request;
    WSKNAME_REPLY Reply;
} WSKNAME_PENDING, *PWSKNAME_PENDING;

static KSPIN_LOCK WskNameLock;
static LIST_ENTRY WskNamePending;
static LIST_ENTRY WskNameWaiters;
static ULONG WskNameServers;
static LONG WskNameNextId;
static PDEVICE_OBJECT WskNameDevice;
static PDRIVER_DISPATCH WskNameNext[IRP_MJ_MAXIMUM_FUNCTION + 1];

static
PIRP
WskNameTakeWaiterLocked(
    _In_ PWSKNAME_PENDING Pending)
{
    PLIST_ENTRY Entry, Next;
    PWSKNAME_FILE File;
    PIRP Irp;

    ASSERT(KeGetCurrentIrql() == DISPATCH_LEVEL);
    ASSERT(!Pending->Dispatched);

    for (Entry = WskNameWaiters.Flink; Entry != &WskNameWaiters; Entry = Next)
    {
        Next = Entry->Flink;
        Irp = CONTAINING_RECORD(Entry, IRP, Tail.Overlay.ListEntry);
        File = IoGetCurrentIrpStackLocation(Irp)->FileObject->FsContext;
        ASSERT(File->Serving);
        if (Pending->Declined & File->ServerBit)
            continue;
        RemoveEntryList(Entry);
        InitializeListHead(Entry);
        if (IoSetCancelRoutine(Irp, NULL) != NULL)
            return Irp;
    }
    return NULL;
}

static
PWSKNAME_PENDING
WskNameNextUndispatchedLocked(
    _In_ PWSKNAME_FILE File)
{
    PLIST_ENTRY Entry;
    PWSKNAME_PENDING Pending;

    ASSERT(KeGetCurrentIrql() == DISPATCH_LEVEL);
    ASSERT(File->Serving);

    for (Entry = WskNamePending.Flink; Entry != &WskNamePending; Entry = Entry->Flink)
    {
        Pending = CONTAINING_RECORD(Entry, WSKNAME_PENDING, Link);
        if (!Pending->Dispatched && !(Pending->Declined & File->ServerBit))
            return Pending;
    }
    return NULL;
}

static
VOID
WskNameHandOutLocked(
    _Inout_ PWSKNAME_PENDING Pending,
    _Inout_ PIRP Irp)
{
    PIO_STACK_LOCATION Stack = IoGetCurrentIrpStackLocation(Irp);

    ASSERT(KeGetCurrentIrql() == DISPATCH_LEVEL);
    ASSERT(!Pending->Dispatched);

    Pending->Dispatched = TRUE;
    Pending->Server = Stack->FileObject;
    RtlCopyMemory(Irp->AssociatedIrp.SystemBuffer, &Pending->Request, sizeof(Pending->Request));
    Irp->IoStatus.Status = STATUS_SUCCESS;
    Irp->IoStatus.Information = sizeof(Pending->Request);
}

NTSTATUS
WskNameForward(
    _In_ const WSKNAME_REQUEST *Request,
    _Out_ PWSKNAME_REPLY Reply)
{
    PWSKNAME_PENDING Pending;
    PIRP Irp = NULL;
    NTSTATUS Status;
    KIRQL Irql;

    PAGED_CODE();

    Pending = ExAllocatePoolZero(NonPagedPoolNx, sizeof(*Pending), TAG_WSKNAME);
    if (Pending == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;

    RtlCopyMemory(&Pending->Request, Request, sizeof(*Request));
    Pending->Id = (ULONG)InterlockedIncrement(&WskNameNextId);
    Pending->Request.Id = Pending->Id;
    KeInitializeEvent(&Pending->Done, NotificationEvent, FALSE);

    KeAcquireSpinLock(&WskNameLock, &Irql);
    if (WskNameServers == 0)
    {
        KeReleaseSpinLock(&WskNameLock, Irql);
        ExFreePoolWithTag(Pending, TAG_WSKNAME);
        return STATUS_NO_MATCH;
    }
    InsertTailList(&WskNamePending, &Pending->Link);
    Irp = WskNameTakeWaiterLocked(Pending);
    if (Irp != NULL)
        WskNameHandOutLocked(Pending, Irp);
    KeReleaseSpinLock(&WskNameLock, Irql);

    if (Irp != NULL)
        IoCompleteRequest(Irp, IO_NETWORK_INCREMENT);

    KeWaitForSingleObject(&Pending->Done, Executive, KernelMode, FALSE, NULL);

    Status = Pending->Status;
    if (NT_SUCCESS(Status))
        RtlCopyMemory(Reply, &Pending->Reply, sizeof(*Reply));
    ExFreePoolWithTag(Pending, TAG_WSKNAME);
    return Status;
}

static
VOID
NTAPI
WskNameCancelGet(
    _Inout_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    KIRQL Irql;

    UNREFERENCED_PARAMETER(DeviceObject);

    IoReleaseCancelSpinLock(Irp->CancelIrql);

    KeAcquireSpinLock(&WskNameLock, &Irql);
    RemoveEntryList(&Irp->Tail.Overlay.ListEntry);
    InitializeListHead(&Irp->Tail.Overlay.ListEntry);
    KeReleaseSpinLock(&WskNameLock, Irql);

    Irp->IoStatus.Status = STATUS_CANCELLED;
    Irp->IoStatus.Information = 0;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
}

static
NTSTATUS
WskNameGetRequest(
    _Inout_ PIRP Irp,
    _In_ PWSKNAME_FILE File,
    _In_ ULONG OutputLength)
{
    PWSKNAME_PENDING Pending;
    KIRQL Irql;

    if (OutputLength < sizeof(WSKNAME_REQUEST))
        return STATUS_BUFFER_TOO_SMALL;

    KeAcquireSpinLock(&WskNameLock, &Irql);
    if (!File->Serving)
    {
        ULONG Free = ~WskNameServers;

        if (Free == 0)
        {
            KeReleaseSpinLock(&WskNameLock, Irql);
            return STATUS_INSUFFICIENT_RESOURCES;
        }
        File->ServerBit = Free & (0 - Free);
        File->Serving = TRUE;
        WskNameServers |= File->ServerBit;
    }

    Pending = WskNameNextUndispatchedLocked(File);
    if (Pending != NULL)
    {
        WskNameHandOutLocked(Pending, Irp);
        KeReleaseSpinLock(&WskNameLock, Irql);
        return STATUS_SUCCESS;
    }

    IoSetCancelRoutine(Irp, WskNameCancelGet);
    if (Irp->Cancel && IoSetCancelRoutine(Irp, NULL) != NULL)
    {
        KeReleaseSpinLock(&WskNameLock, Irql);
        return STATUS_CANCELLED;
    }
    IoMarkIrpPending(Irp);
    InsertTailList(&WskNameWaiters, &Irp->Tail.Overlay.ListEntry);
    KeReleaseSpinLock(&WskNameLock, Irql);
    return STATUS_PENDING;
}

static
NTSTATUS
WskNameCompleteRequest(
    _In_ PFILE_OBJECT FileObject,
    _In_reads_bytes_(InputLength) const WSKNAME_REPLY *Reply,
    _In_ ULONG InputLength)
{
    PWSKNAME_FILE File = FileObject->FsContext;
    PWSKNAME_PENDING Pending = NULL;
    BOOLEAN Done = TRUE;
    PLIST_ENTRY Entry;
    PIRP Irp = NULL;
    KIRQL Irql;

    if (InputLength != sizeof(WSKNAME_REPLY) || Reply->AddressCount > WSKNAME_MAX_REPLY_ADDRESSES)
        return STATUS_INVALID_PARAMETER;

    KeAcquireSpinLock(&WskNameLock, &Irql);
    for (Entry = WskNamePending.Flink; Entry != &WskNamePending; Entry = Entry->Flink)
    {
        PWSKNAME_PENDING Candidate = CONTAINING_RECORD(Entry, WSKNAME_PENDING, Link);

        if (Candidate->Id == Reply->Id && Candidate->Dispatched && Candidate->Server == FileObject)
        {
            Pending = Candidate;
            break;
        }
    }
    if (Pending != NULL)
    {
        ASSERT(File->Serving);
        RtlCopyMemory(&Pending->Reply, Reply, sizeof(*Reply));
        Pending->Reply.Name[WSKNAME_MAX_HOST - 1] = UNICODE_NULL;
        Pending->Status = STATUS_SUCCESS;
        Pending->Answered = TRUE;
        Pending->Declined |= File->ServerBit;

        /* A resolver with no answer leaves the request to the ones not asked yet; the last such answer stands if none has one */
        if (!NT_SUCCESS(Reply->Status) && (WskNameServers & ~Pending->Declined) != 0)
        {
            Pending->Dispatched = FALSE;
            Pending->Server = NULL;
            Irp = WskNameTakeWaiterLocked(Pending);
            if (Irp != NULL)
                WskNameHandOutLocked(Pending, Irp);
            Done = FALSE;
        }
        else
        {
            RemoveEntryList(&Pending->Link);
        }
    }
    KeReleaseSpinLock(&WskNameLock, Irql);

    if (Pending == NULL)
        return STATUS_NOT_FOUND;

    if (Irp != NULL)
        IoCompleteRequest(Irp, IO_NETWORK_INCREMENT);
    if (Done)
        KeSetEvent(&Pending->Done, IO_NETWORK_INCREMENT, FALSE);
    return STATUS_SUCCESS;
}

static
VOID
WskNameCleanup(
    _In_ PFILE_OBJECT FileObject,
    _In_ PWSKNAME_FILE File)
{
    LIST_ENTRY Cancelled, Failed, HandedOut;
    PLIST_ENTRY Entry, Next;
    ULONG ServerBit = 0;
    KIRQL Irql;

    InitializeListHead(&Cancelled);
    InitializeListHead(&Failed);
    InitializeListHead(&HandedOut);

    KeAcquireSpinLock(&WskNameLock, &Irql);
    if (File->Serving)
    {
        ASSERT((WskNameServers & File->ServerBit) != 0);
        ServerBit = File->ServerBit;
        File->Serving = FALSE;
        File->ServerBit = 0;
        WskNameServers &= ~ServerBit;
    }

    for (Entry = WskNameWaiters.Flink; Entry != &WskNameWaiters; Entry = Next)
    {
        PIRP Irp = CONTAINING_RECORD(Entry, IRP, Tail.Overlay.ListEntry);

        Next = Entry->Flink;
        if (IoGetCurrentIrpStackLocation(Irp)->FileObject != FileObject)
            continue;
        RemoveEntryList(Entry);
        InitializeListHead(Entry);
        if (IoSetCancelRoutine(Irp, NULL) != NULL)
            InsertTailList(&Cancelled, Entry);
    }

    for (Entry = WskNamePending.Flink; Entry != &WskNamePending; Entry = Next)
    {
        PWSKNAME_PENDING Pending = CONTAINING_RECORD(Entry, WSKNAME_PENDING, Link);

        Next = Entry->Flink;
        Pending->Declined &= ~ServerBit;
        if (Pending->Dispatched && Pending->Server == FileObject)
        {
            Pending->Dispatched = FALSE;
            Pending->Server = NULL;
        }
        if (Pending->Dispatched)
            continue;

        if ((WskNameServers & ~Pending->Declined) != 0)
        {
            PIRP Irp = WskNameTakeWaiterLocked(Pending);

            if (Irp != NULL)
            {
                WskNameHandOutLocked(Pending, Irp);
                InsertTailList(&HandedOut, &Irp->Tail.Overlay.ListEntry);
            }
            continue;
        }

        RemoveEntryList(Entry);
        if (!Pending->Answered)
            Pending->Status = STATUS_NO_MATCH;
        InsertTailList(&Failed, Entry);
    }
    KeReleaseSpinLock(&WskNameLock, Irql);

    while (!IsListEmpty(&HandedOut))
    {
        PIRP Irp = CONTAINING_RECORD(RemoveHeadList(&HandedOut), IRP, Tail.Overlay.ListEntry);

        IoCompleteRequest(Irp, IO_NETWORK_INCREMENT);
    }

    while (!IsListEmpty(&Cancelled))
    {
        PIRP Irp = CONTAINING_RECORD(RemoveHeadList(&Cancelled), IRP, Tail.Overlay.ListEntry);

        Irp->IoStatus.Status = STATUS_CANCELLED;
        Irp->IoStatus.Information = 0;
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
    }

    while (!IsListEmpty(&Failed))
    {
        PWSKNAME_PENDING Pending = CONTAINING_RECORD(RemoveHeadList(&Failed), WSKNAME_PENDING, Link);

        KeSetEvent(&Pending->Done, IO_NETWORK_INCREMENT, FALSE);
    }
}

static
NTSTATUS
NTAPI
WskNameDispatch(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    PIO_STACK_LOCATION Stack = IoGetCurrentIrpStackLocation(Irp);
    PWSKNAME_FILE File;
    NTSTATUS Status = STATUS_SUCCESS;

    if (DeviceObject != WskNameDevice)
        return WskNameNext[Stack->MajorFunction](DeviceObject, Irp);

    File = Stack->FileObject->FsContext;
    Irp->IoStatus.Information = 0;

    switch (Stack->MajorFunction)
    {
        case IRP_MJ_CREATE:
            File = ExAllocatePoolZero(NonPagedPoolNx, sizeof(*File), TAG_WSKNAME);
            if (File == NULL)
            {
                Status = STATUS_INSUFFICIENT_RESOURCES;
                break;
            }
            Stack->FileObject->FsContext = File;
            break;

        case IRP_MJ_CLEANUP:
            WskNameCleanup(Stack->FileObject, File);
            break;

        case IRP_MJ_CLOSE:
            ExFreePoolWithTag(File, TAG_WSKNAME);
            Stack->FileObject->FsContext = NULL;
            break;

        case IRP_MJ_DEVICE_CONTROL:
            switch (Stack->Parameters.DeviceIoControl.IoControlCode)
            {
                case IOCTL_WSKNAME_GET_REQUEST:
                    Status = WskNameGetRequest(Irp, File, Stack->Parameters.DeviceIoControl.OutputBufferLength);
                    if (Status == STATUS_PENDING)
                        return STATUS_PENDING;
                    if (NT_SUCCESS(Status))
                    {
                        IoCompleteRequest(Irp, IO_NETWORK_INCREMENT);
                        return Status;
                    }
                    break;

                case IOCTL_WSKNAME_COMPLETE_REQUEST:
                    Status = WskNameCompleteRequest(Stack->FileObject,
                                                    Irp->AssociatedIrp.SystemBuffer,
                                                    Stack->Parameters.DeviceIoControl.InputBufferLength);
                    break;

                default:
                    Status = STATUS_INVALID_DEVICE_REQUEST;
                    break;
            }
            break;

        default:
            Status = STATUS_INVALID_DEVICE_REQUEST;
            break;
    }

    Irp->IoStatus.Status = Status;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return Status;
}

NTSTATUS
WskNameCreateDevice(
    _In_ PDRIVER_OBJECT DriverObject)
{
    static const UNICODE_STRING Sddl = RTL_CONSTANT_STRING(L"D:P(A;;GA;;;SY)(A;;GA;;;NS)");
    UNICODE_STRING DeviceName = RTL_CONSTANT_STRING(WSKNAME_DEVICE_NAME);
    UNICODE_STRING DosName = RTL_CONSTANT_STRING(WSKNAME_DOS_DEVICE_NAME);
    static const UCHAR Majors[] = { IRP_MJ_CREATE, IRP_MJ_CLEANUP, IRP_MJ_CLOSE, IRP_MJ_DEVICE_CONTROL };
    NTSTATUS Status;
    ULONG Index;

    KeInitializeSpinLock(&WskNameLock);
    InitializeListHead(&WskNamePending);
    InitializeListHead(&WskNameWaiters);

    Status = IoCreateDeviceSecure(DriverObject,
                                  0,
                                  &DeviceName,
                                  FILE_DEVICE_NETWORK,
                                  FILE_DEVICE_SECURE_OPEN,
                                  FALSE,
                                  &Sddl,
                                  NULL,
                                  &WskNameDevice);
    if (!NT_SUCCESS(Status))
        return Status;

    /* The resolver runs in user mode, so the device needs a name it can open. */
    Status = IoCreateSymbolicLink(&DosName, &DeviceName);
    if (!NT_SUCCESS(Status))
    {
        IoDeleteDevice(WskNameDevice);
        WskNameDevice = NULL;
        return Status;
    }

    for (Index = 0; Index < RTL_NUMBER_OF(Majors); Index++)
    {
        WskNameNext[Majors[Index]] = DriverObject->MajorFunction[Majors[Index]];
        DriverObject->MajorFunction[Majors[Index]] = WskNameDispatch;
    }

    WskNameDevice->Flags |= DO_BUFFERED_IO;
    WskNameDevice->Flags &= ~DO_DEVICE_INITIALIZING;
    return STATUS_SUCCESS;
}
