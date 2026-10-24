/*
 * PROJECT:     LiberNT Linux Subsystem
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Window channel between a Linux compositor and the Windows window host
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "lxcore.h"

#define LX_WIN_QUEUE_LIMIT  (128 * 1024 * 1024)

typedef struct _LX_WIN_MESSAGE
{
    LIST_ENTRY Link;
    ULONG Size;
    UCHAR Data[ANYSIZE_ARRAY];
} LX_WIN_MESSAGE, *PLX_WIN_MESSAGE;

typedef struct _LX_WIN_CHANNEL
{
    LIST_ENTRY Link;
    volatile LONG RefCount;
    UNICODE_STRING RootNt;
    KSPIN_LOCK Lock;
    LIST_ENTRY ToHost;
    ULONG64 ToHostBytes;
    LIST_ENTRY Pending;
    KEVENT ToHostSpace;
    LIST_ENTRY ToLinux;
    LX_POLL_HEAD Poll;
    BOOLEAN LinuxOpen;
    BOOLEAN HostAttached;
} LX_WIN_CHANNEL, *PLX_WIN_CHANNEL;

typedef struct _LX_WIN_HOST
{
    PLX_WIN_CHANNEL Channel;
} LX_WIN_HOST, *PLX_WIN_HOST;

static LIST_ENTRY LxWinChannels;
static FAST_MUTEX LxWinChannelsLock;

VOID
LxWinInitialize(VOID)
{
    InitializeListHead(&LxWinChannels);
    ExInitializeFastMutex(&LxWinChannelsLock);
}

static
PLX_WIN_CHANNEL
LxWinChannelGet(
    _In_ PCUNICODE_STRING RootNt)
{
    PLX_WIN_CHANNEL Channel;
    PLIST_ENTRY Entry;

    ExAcquireFastMutex(&LxWinChannelsLock);
    for (Entry = LxWinChannels.Flink; Entry != &LxWinChannels; Entry = Entry->Flink)
    {
        Channel = CONTAINING_RECORD(Entry, LX_WIN_CHANNEL, Link);
        if (RtlEqualUnicodeString(&Channel->RootNt, RootNt, TRUE))
        {
            InterlockedIncrement(&Channel->RefCount);
            ExReleaseFastMutex(&LxWinChannelsLock);
            return Channel;
        }
    }

    Channel = LxAlloc(sizeof(*Channel) + RootNt->Length);
    if (Channel != NULL)
    {
        Channel->RefCount = 1;
        Channel->RootNt.Buffer = (PWCHAR)(Channel + 1);
        Channel->RootNt.Length = Channel->RootNt.MaximumLength = RootNt->Length;
        RtlCopyMemory(Channel->RootNt.Buffer, RootNt->Buffer, RootNt->Length);
        KeInitializeSpinLock(&Channel->Lock);
        InitializeListHead(&Channel->ToHost);
        InitializeListHead(&Channel->Pending);
        InitializeListHead(&Channel->ToLinux);
        KeInitializeEvent(&Channel->ToHostSpace, NotificationEvent, TRUE);
        LxPollHeadInit(&Channel->Poll);
        InsertTailList(&LxWinChannels, &Channel->Link);
    }
    ExReleaseFastMutex(&LxWinChannelsLock);
    return Channel;
}

static
VOID
LxWinPurge(
    _Inout_ PLIST_ENTRY Queue)
{
    while (!IsListEmpty(Queue))
        LxFree(CONTAINING_RECORD(RemoveHeadList(Queue), LX_WIN_MESSAGE, Link));
}

static
VOID
LxWinChannelRelease(
    _In_ PLX_WIN_CHANNEL Channel)
{
    ExAcquireFastMutex(&LxWinChannelsLock);
    if (InterlockedDecrement(&Channel->RefCount) != 0)
    {
        ExReleaseFastMutex(&LxWinChannelsLock);
        return;
    }
    RemoveEntryList(&Channel->Link);
    ExReleaseFastMutex(&LxWinChannelsLock);
    LxWinPurge(&Channel->ToHost);
    LxWinPurge(&Channel->ToLinux);
    LxFree(Channel);
}

static
PLX_WIN_MESSAGE
LxWinNotice(
    _In_ ULONG Type)
{
    PLX_WIN_MESSAGE Message = LxAlloc(FIELD_OFFSET(LX_WIN_MESSAGE, Data) + sizeof(LX_WIN_HEADER));
    PLX_WIN_HEADER Header;

    if (Message == NULL)
        return NULL;
    Message->Size = sizeof(LX_WIN_HEADER);
    Header = (PLX_WIN_HEADER)Message->Data;
    Header->Type = Type;
    Header->Size = sizeof(LX_WIN_HEADER);
    return Message;
}

static
VOID
NTAPI
LxWinCancel(
    _Inout_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    PLX_WIN_CHANNEL Channel = Irp->Tail.Overlay.DriverContext[0];
    KIRQL Irql;

    UNREFERENCED_PARAMETER(DeviceObject);

    IoReleaseCancelSpinLock(Irp->CancelIrql);
    KeAcquireSpinLock(&Channel->Lock, &Irql);
    RemoveEntryList(&Irp->Tail.Overlay.ListEntry);
    KeReleaseSpinLock(&Channel->Lock, Irql);
    Irp->IoStatus.Status = STATUS_CANCELLED;
    Irp->IoStatus.Information = 0;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
}

static
VOID
LxWinComplete(
    _Inout_ PIRP Irp,
    _In_opt_ PLX_WIN_MESSAGE Message,
    _In_ NTSTATUS Status)
{
    PIO_STACK_LOCATION Stack = IoGetCurrentIrpStackLocation(Irp);
    PVOID Buffer;

    Irp->IoStatus.Information = 0;
    if (Message != NULL)
    {
        if (Stack->Parameters.DeviceIoControl.OutputBufferLength < Message->Size)
        {
            Status = STATUS_BUFFER_OVERFLOW;
            Irp->IoStatus.Information = Message->Size;
        }
        else
        {
            Buffer = (Irp->MdlAddress != NULL) ? MmGetSystemAddressForMdlSafe(Irp->MdlAddress, NormalPagePriority) : NULL;
            if (Buffer == NULL)
            {
                Status = STATUS_INSUFFICIENT_RESOURCES;
            }
            else
            {
                RtlCopyMemory(Buffer, Message->Data, Message->Size);
                Irp->IoStatus.Information = Message->Size;
                Status = STATUS_SUCCESS;
            }
        }
    }
    Irp->IoStatus.Status = Status;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
}

static
PIRP
LxWinTakePending(
    _In_ PLX_WIN_CHANNEL Channel)
{
    while (!IsListEmpty(&Channel->Pending))
    {
        PIRP Irp = CONTAINING_RECORD(RemoveHeadList(&Channel->Pending), IRP, Tail.Overlay.ListEntry);

        if (IoSetCancelRoutine(Irp, NULL) != NULL)
            return Irp;
        InitializeListHead(&Irp->Tail.Overlay.ListEntry);
    }
    return NULL;
}

static
VOID
LxWinToHost(
    _In_ PLX_WIN_CHANNEL Channel,
    _In_ PLX_WIN_MESSAGE Message)
{
    PIRP Irp = NULL;
    KIRQL Irql;

    KeAcquireSpinLock(&Channel->Lock, &Irql);
    if (IsListEmpty(&Channel->ToHost))
        Irp = LxWinTakePending(Channel);
    if (Irp == NULL)
    {
        InsertTailList(&Channel->ToHost, &Message->Link);
        Channel->ToHostBytes += Message->Size;
        if (Channel->ToHostBytes >= LX_WIN_QUEUE_LIMIT)
            KeClearEvent(&Channel->ToHostSpace);
    }
    KeReleaseSpinLock(&Channel->Lock, Irql);

    if (Irp != NULL)
    {
        PIO_STACK_LOCATION Stack = IoGetCurrentIrpStackLocation(Irp);

        if (Stack->Parameters.DeviceIoControl.OutputBufferLength < Message->Size)
        {
            KeAcquireSpinLock(&Channel->Lock, &Irql);
            InsertHeadList(&Channel->ToHost, &Message->Link);
            Channel->ToHostBytes += Message->Size;
            KeReleaseSpinLock(&Channel->Lock, Irql);
            LxWinComplete(Irp, Message, STATUS_SUCCESS);
            return;
        }
        LxWinComplete(Irp, Message, STATUS_SUCCESS);
        LxFree(Message);
    }
}

static
VOID
LxWinToLinux(
    _In_ PLX_WIN_CHANNEL Channel,
    _In_ PLX_WIN_MESSAGE Message)
{
    KIRQL Irql;

    KeAcquireSpinLock(&Channel->Lock, &Irql);
    InsertTailList(&Channel->ToLinux, &Message->Link);
    KeReleaseSpinLock(&Channel->Lock, Irql);
    LxPollWake(&Channel->Poll, LX_POLLIN | LX_POLLRDNORM);
}

static
LONG64
LxWinRead(
    _In_ PLX_FILE File,
    _In_ ULONG64 Buffer,
    _In_ ULONG64 Length,
    _Inout_opt_ PLONG64 Offset)
{
    PLX_WIN_CHANNEL Channel = File->Object;
    PLX_WIN_MESSAGE Message;
    LONG64 Result;
    KIRQL Irql;

    UNREFERENCED_PARAMETER(Offset);

    for (;;)
    {
        KeAcquireSpinLock(&Channel->Lock, &Irql);
        if (!IsListEmpty(&Channel->ToLinux))
        {
            Message = CONTAINING_RECORD(Channel->ToLinux.Flink, LX_WIN_MESSAGE, Link);
            if (Length < Message->Size)
            {
                KeReleaseSpinLock(&Channel->Lock, Irql);
                return -LX_EMSGSIZE;
            }
            RemoveEntryList(&Message->Link);
            KeReleaseSpinLock(&Channel->Lock, Irql);
            Result = LxCopyToUser(Buffer, Message->Data, Message->Size);
            if (Result == 0)
                Result = Message->Size;
            LxFree(Message);
            return Result;
        }
        KeReleaseSpinLock(&Channel->Lock, Irql);

        if (File->Flags & LX_O_NONBLOCK)
            return -LX_EAGAIN;
        Result = LxFileWait(File, LX_POLLIN, NULL);
        if (Result < 0)
            return Result;
    }
}

static
LONG64
LxWinWrite(
    _In_ PLX_FILE File,
    _In_ ULONG64 Buffer,
    _In_ ULONG64 Length,
    _Inout_opt_ PLONG64 Offset)
{
    PLX_WIN_CHANNEL Channel = File->Object;
    PLX_WIN_MESSAGE Message;
    PLX_WIN_HEADER Header;
    LONG64 Result;
    KIRQL Irql;

    UNREFERENCED_PARAMETER(Offset);

    if ((Length < sizeof(LX_WIN_HEADER)) || (Length > LX_WIN_MAX_MESSAGE))
        return -LX_EINVAL;
    Message = LxAlloc(FIELD_OFFSET(LX_WIN_MESSAGE, Data) + (SIZE_T)Length);
    if (Message == NULL)
        return -LX_ENOMEM;
    Result = LxCopyFromUser(Message->Data, Buffer, (SIZE_T)Length);
    if (Result != 0)
    {
        LxFree(Message);
        return Result;
    }
    Message->Size = (ULONG)Length;
    Header = (PLX_WIN_HEADER)Message->Data;
    Header->Size = (ULONG)Length;

    for (;;)
    {
        BOOLEAN Attached, Room;

        KeAcquireSpinLock(&Channel->Lock, &Irql);
        Attached = Channel->HostAttached;
        Room = (Channel->ToHostBytes + Length <= LX_WIN_QUEUE_LIMIT) || IsListEmpty(&Channel->ToHost);
        KeReleaseSpinLock(&Channel->Lock, Irql);

        if (!Attached)
        {
            LxFree(Message);
            return -LX_ENOTCONN;
        }
        if (Room)
            break;
        if (File->Flags & LX_O_NONBLOCK)
        {
            LxFree(Message);
            return -LX_EAGAIN;
        }
        if (LxWaitForEvent(&Channel->ToHostSpace, NULL) != STATUS_SUCCESS)
        {
            LxFree(Message);
            return -LX_ERESTARTSYS;
        }
    }

    LxWinToHost(Channel, Message);
    return (LONG64)Length;
}

static
ULONG
LxWinPoll(
    _In_ PLX_FILE File,
    _In_opt_ PLX_POLL_ENTRY Entry)
{
    PLX_WIN_CHANNEL Channel = File->Object;
    ULONG Mask = 0;
    KIRQL Irql;

    if (Entry != NULL)
        LxPollAdd(&Channel->Poll, Entry);

    KeAcquireSpinLock(&Channel->Lock, &Irql);
    if (!IsListEmpty(&Channel->ToLinux))
        Mask |= LX_POLLIN | LX_POLLRDNORM;
    if (Channel->HostAttached && (Channel->ToHostBytes < LX_WIN_QUEUE_LIMIT))
        Mask |= LX_POLLOUT | LX_POLLWRNORM;
    KeReleaseSpinLock(&Channel->Lock, Irql);
    return Mask;
}

static
LONG
LxWinStat(
    _In_ PLX_FILE File,
    _Out_ PLX_STAT Stat)
{
    UNREFERENCED_PARAMETER(File);

    RtlZeroMemory(Stat, sizeof(*Stat));
    Stat->Mode = LX_S_IFCHR | 0666;
    Stat->Nlink = 1;
    Stat->Rdev = ((ULONG64)LX_WIN_DEVICE_MAJOR << 8) | LX_WIN_DEVICE_MINOR;
    Stat->BlkSize = 4096;
    return 0;
}

static
VOID
LxWinRelease(
    _In_ PLX_FILE File)
{
    PLX_WIN_CHANNEL Channel = File->Object;
    PLX_WIN_MESSAGE Notice;
    BOOLEAN Attached;
    KIRQL Irql;

    if (Channel == NULL)
        return;
    Notice = LxWinNotice(LX_WIN_COMPOSITOR_DETACHED);
    KeAcquireSpinLock(&Channel->Lock, &Irql);
    Channel->LinuxOpen = FALSE;
    Attached = Channel->HostAttached;
    KeReleaseSpinLock(&Channel->Lock, Irql);

    if (Notice != NULL)
    {
        if (Attached)
            LxWinToHost(Channel, Notice);
        else
            LxFree(Notice);
    }
    KeAcquireSpinLock(&Channel->Lock, &Irql);
    while (!IsListEmpty(&Channel->ToLinux))
    {
        PLX_WIN_MESSAGE Message = CONTAINING_RECORD(RemoveHeadList(&Channel->ToLinux), LX_WIN_MESSAGE, Link);

        KeReleaseSpinLock(&Channel->Lock, Irql);
        LxFree(Message);
        KeAcquireSpinLock(&Channel->Lock, &Irql);
    }
    KeReleaseSpinLock(&Channel->Lock, Irql);
    File->Object = NULL;
    LxWinChannelRelease(Channel);
}

static const LX_FILE_OPS LxWinOps =
{
    LxWinRead, LxWinWrite, NULL, LxWinPoll, LxWinStat, NULL, NULL, NULL, LxWinRelease
};

PLX_FILE
LxWinOpen(
    _In_ PLX_PROCESS Process,
    _In_ ULONG Flags,
    _In_ PCSTR Path)
{
    PLX_WIN_CHANNEL Channel = LxWinChannelGet(&Process->Instance->RootNt);
    PLX_WIN_MESSAGE ToHost, ToLinux;
    BOOLEAN Busy, Attached;
    PLX_FILE File;
    KIRQL Irql;

    if (Channel == NULL)
        return NULL;
    File = LxFileAlloc(&LxWinOps, Flags & (LX_O_ACCMODE | LX_O_NONBLOCK), LX_S_IFCHR);
    ToHost = LxWinNotice(LX_WIN_COMPOSITOR_ATTACHED);
    ToLinux = LxWinNotice(LX_WIN_HOST_ATTACHED);
    if ((File == NULL) || (ToHost == NULL) || (ToLinux == NULL))
    {
        if (File != NULL)
            LxFileDereference(File);
        if (ToHost != NULL)
            LxFree(ToHost);
        if (ToLinux != NULL)
            LxFree(ToLinux);
        LxWinChannelRelease(Channel);
        return NULL;
    }

    KeAcquireSpinLock(&Channel->Lock, &Irql);
    Busy = Channel->LinuxOpen;
    if (!Busy)
        Channel->LinuxOpen = TRUE;
    Attached = Channel->HostAttached;
    KeReleaseSpinLock(&Channel->Lock, Irql);

    if (Busy)
    {
        LxFree(ToHost);
        LxFree(ToLinux);
        LxFileDereference(File);
        LxWinChannelRelease(Channel);
        return NULL;
    }

    File->Object = Channel;
    File->Path = LxStrDup(Path);
    File->Rdev = ((ULONG64)LX_WIN_DEVICE_MAJOR << 8) | LX_WIN_DEVICE_MINOR;
    if (Attached)
    {
        LxWinToLinux(Channel, ToLinux);
        LxWinToHost(Channel, ToHost);
    }
    else
    {
        LxFree(ToLinux);
        LxFree(ToHost);
    }
    return File;
}

NTSTATUS
LxWinHostCreate(
    _Inout_ PFILE_OBJECT FileObject)
{
    PLX_WIN_HOST Host = LxAlloc(sizeof(*Host));

    if (Host == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;
    FileObject->FsContext2 = Host;
    return STATUS_SUCCESS;
}

VOID
LxWinHostCleanup(
    _Inout_ PFILE_OBJECT FileObject)
{
    PLX_WIN_HOST Host = FileObject->FsContext2;
    PLX_WIN_CHANNEL Channel = (Host != NULL) ? Host->Channel : NULL;
    PLX_WIN_MESSAGE Notice;
    LIST_ENTRY Purge;
    BOOLEAN LinuxOpen;
    KIRQL Irql;
    PIRP Irp;

    if (Channel == NULL)
        return;
    Host->Channel = NULL;
    Notice = LxWinNotice(LX_WIN_HOST_DETACHED);
    InitializeListHead(&Purge);

    KeAcquireSpinLock(&Channel->Lock, &Irql);
    Channel->HostAttached = FALSE;
    LinuxOpen = Channel->LinuxOpen;
    while (!IsListEmpty(&Channel->ToHost))
        InsertTailList(&Purge, RemoveHeadList(&Channel->ToHost));
    Channel->ToHostBytes = 0;
    KeSetEvent(&Channel->ToHostSpace, IO_NO_INCREMENT, FALSE);
    while ((Irp = LxWinTakePending(Channel)) != NULL)
    {
        KeReleaseSpinLock(&Channel->Lock, Irql);
        LxWinComplete(Irp, NULL, STATUS_CANCELLED);
        KeAcquireSpinLock(&Channel->Lock, &Irql);
    }
    KeReleaseSpinLock(&Channel->Lock, Irql);

    LxWinPurge(&Purge);
    if (Notice != NULL)
    {
        if (LinuxOpen)
            LxWinToLinux(Channel, Notice);
        else
            LxFree(Notice);
    }
    LxPollWake(&Channel->Poll, LX_POLLIN | LX_POLLOUT);
    LxWinChannelRelease(Channel);
}

VOID
LxWinHostClose(
    _Inout_ PFILE_OBJECT FileObject)
{
    PLX_WIN_HOST Host = FileObject->FsContext2;

    if (Host == NULL)
        return;
    FileObject->FsContext2 = NULL;
    if (Host->Channel != NULL)
        LxWinHostCleanup(FileObject);
    LxFree(Host);
}

static
NTSTATUS
LxWinHostAttach(
    _In_ PLX_WIN_HOST Host,
    _In_ PIRP Irp,
    _In_ PIO_STACK_LOCATION Stack)
{
    ULONG Length = Stack->Parameters.DeviceIoControl.InputBufferLength;
    PLX_WIN_MESSAGE ToHost, ToLinux;
    UNICODE_STRING RootNt;
    PLX_WIN_CHANNEL Channel;
    BOOLEAN Busy, LinuxOpen;
    KIRQL Irql;

    if (Host->Channel != NULL)
        return STATUS_DEVICE_BUSY;
    if ((Length < 2 * sizeof(WCHAR)) || (Length & 1) || (Length > MAXUSHORT))
        return STATUS_INVALID_PARAMETER;

    RootNt.Buffer = Irp->AssociatedIrp.SystemBuffer;
    RootNt.Length = (USHORT)Length;
    while ((RootNt.Length > 2 * sizeof(WCHAR)) &&
           ((RootNt.Buffer[RootNt.Length / sizeof(WCHAR) - 1] == L'\\') ||
            (RootNt.Buffer[RootNt.Length / sizeof(WCHAR) - 1] == UNICODE_NULL)))
    {
        RootNt.Length -= sizeof(WCHAR);
    }
    RootNt.MaximumLength = RootNt.Length;

    Channel = LxWinChannelGet(&RootNt);
    ToHost = LxWinNotice(LX_WIN_COMPOSITOR_ATTACHED);
    ToLinux = LxWinNotice(LX_WIN_HOST_ATTACHED);
    if ((Channel == NULL) || (ToHost == NULL) || (ToLinux == NULL))
    {
        if (Channel != NULL)
            LxWinChannelRelease(Channel);
        if (ToHost != NULL)
            LxFree(ToHost);
        if (ToLinux != NULL)
            LxFree(ToLinux);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    KeAcquireSpinLock(&Channel->Lock, &Irql);
    Busy = Channel->HostAttached;
    if (!Busy)
        Channel->HostAttached = TRUE;
    LinuxOpen = Channel->LinuxOpen;
    KeReleaseSpinLock(&Channel->Lock, Irql);

    if (Busy)
    {
        LxFree(ToHost);
        LxFree(ToLinux);
        LxWinChannelRelease(Channel);
        return STATUS_DEVICE_BUSY;
    }
    Host->Channel = Channel;
    if (LinuxOpen)
    {
        LxWinToHost(Channel, ToHost);
        LxWinToLinux(Channel, ToLinux);
    }
    else
    {
        LxFree(ToHost);
        LxFree(ToLinux);
    }
    LxPollWake(&Channel->Poll, LX_POLLOUT | LX_POLLWRNORM);
    return STATUS_SUCCESS;
}

static
NTSTATUS
LxWinHostReceive(
    _In_ PLX_WIN_HOST Host,
    _Inout_ PIRP Irp,
    _In_ PIO_STACK_LOCATION Stack)
{
    PLX_WIN_CHANNEL Channel = Host->Channel;
    PLX_WIN_MESSAGE Message = NULL;
    KIRQL Irql;

    if (Channel == NULL)
    {
        LxWinComplete(Irp, NULL, STATUS_INVALID_DEVICE_STATE);
        return STATUS_INVALID_DEVICE_STATE;
    }

    KeAcquireSpinLock(&Channel->Lock, &Irql);
    if (!IsListEmpty(&Channel->ToHost))
    {
        Message = CONTAINING_RECORD(Channel->ToHost.Flink, LX_WIN_MESSAGE, Link);
        if (Stack->Parameters.DeviceIoControl.OutputBufferLength >= Message->Size)
        {
            RemoveEntryList(&Message->Link);
            Channel->ToHostBytes -= Message->Size;
            if (Channel->ToHostBytes < LX_WIN_QUEUE_LIMIT)
                KeSetEvent(&Channel->ToHostSpace, IO_NO_INCREMENT, FALSE);
            KeReleaseSpinLock(&Channel->Lock, Irql);
            LxWinComplete(Irp, Message, STATUS_SUCCESS);
            LxFree(Message);
            return STATUS_SUCCESS;
        }
        KeReleaseSpinLock(&Channel->Lock, Irql);
        Irp->IoStatus.Information = Message->Size;
        Irp->IoStatus.Status = STATUS_BUFFER_OVERFLOW;
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
        return STATUS_BUFFER_OVERFLOW;
    }

    Irp->Tail.Overlay.DriverContext[0] = Channel;
    IoSetCancelRoutine(Irp, LxWinCancel);
    if (Irp->Cancel && (IoSetCancelRoutine(Irp, NULL) != NULL))
    {
        KeReleaseSpinLock(&Channel->Lock, Irql);
        LxWinComplete(Irp, NULL, STATUS_CANCELLED);
        return STATUS_CANCELLED;
    }
    IoMarkIrpPending(Irp);
    InsertTailList(&Channel->Pending, &Irp->Tail.Overlay.ListEntry);
    KeReleaseSpinLock(&Channel->Lock, Irql);
    return STATUS_PENDING;
}

static
NTSTATUS
LxWinHostSend(
    _In_ PLX_WIN_HOST Host,
    _In_ PIRP Irp,
    _In_ PIO_STACK_LOCATION Stack)
{
    ULONG Length = Stack->Parameters.DeviceIoControl.InputBufferLength;
    PLX_WIN_MESSAGE Message;
    PLX_WIN_HEADER Header;

    if (Host->Channel == NULL)
        return STATUS_INVALID_DEVICE_STATE;
    if ((Length < sizeof(LX_WIN_HEADER)) || (Length > LX_WIN_MAX_EVENT))
        return STATUS_INVALID_PARAMETER;
    Message = LxAlloc(FIELD_OFFSET(LX_WIN_MESSAGE, Data) + Length);
    if (Message == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;
    RtlCopyMemory(Message->Data, Irp->AssociatedIrp.SystemBuffer, Length);
    Message->Size = Length;
    Header = (PLX_WIN_HEADER)Message->Data;
    Header->Size = Length;
    LxWinToLinux(Host->Channel, Message);
    return STATUS_SUCCESS;
}

BOOLEAN
LxWinHostIoctl(
    _In_ PFILE_OBJECT FileObject,
    _Inout_ PIRP Irp,
    _In_ PIO_STACK_LOCATION Stack,
    _Out_ PNTSTATUS Status)
{
    PLX_WIN_HOST Host = FileObject->FsContext2;
    ULONG Code = Stack->Parameters.DeviceIoControl.IoControlCode;

    if ((Code != IOCTL_LX_WIN_ATTACH) && (Code != IOCTL_LX_WIN_RECEIVE) && (Code != IOCTL_LX_WIN_SEND))
        return FALSE;
    if (Host == NULL)
    {
        *Status = STATUS_INVALID_DEVICE_REQUEST;
        Irp->IoStatus.Status = *Status;
        Irp->IoStatus.Information = 0;
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
        return TRUE;
    }

    if (Code == IOCTL_LX_WIN_RECEIVE)
    {
        *Status = LxWinHostReceive(Host, Irp, Stack);
        return TRUE;
    }

    *Status = (Code == IOCTL_LX_WIN_ATTACH) ? LxWinHostAttach(Host, Irp, Stack) : LxWinHostSend(Host, Irp, Stack);
    Irp->IoStatus.Status = *Status;
    Irp->IoStatus.Information = 0;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return TRUE;
}
