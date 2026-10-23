/*
 * PROJECT:     LiberNT Linux Subsystem
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Linux IPv4 and IPv6 sockets on AFD endpoints
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "lxcore.h"
#include <windef.h>
#define _INC_WINDOWS
#include <winsock2.h>
#include <tdi.h>
#include <reactos/drivers/afd/shared.h>

#define NDEBUG
#include <debug.h>

#define LX_INET_CREATED     0
#define LX_INET_BOUND       1
#define LX_INET_LISTENING   2
#define LX_INET_CONNECTING  3
#define LX_INET_CONNECTED   4

#define LX_INET_CHUNK       65536
#define LX_INET_BUFFER      212992
#define LX_INET_OPTIONS     24
#define LX_INET_EVENTS      (AFD_EVENT_RECEIVE | AFD_EVENT_OOB_RECEIVE | AFD_EVENT_SEND | AFD_EVENT_DISCONNECT | \
                             AFD_EVENT_ABORT | AFD_EVENT_CLOSE | AFD_EVENT_CONNECT | AFD_EVENT_ACCEPT | \
                             AFD_EVENT_CONNECT_FAIL)

typedef struct _LX_INET_OPTION
{
    ULONG Level;
    ULONG Name;
    LONG Value;
} LX_INET_OPTION, *PLX_INET_OPTION;

typedef struct _LX_INET_SOCKET
{
    volatile LONG RefCount;
    KEVENT Lock;
    HANDLE Handle;
    PFILE_OBJECT FileObject;
    PDEVICE_OBJECT DeviceObject;
    ULONG Family;
    ULONG Type;
    ULONG Protocol;
    volatile ULONG State;
    BOOLEAN Closing;
    BOOLEAN ShutRead;
    BOOLEAN ShutWrite;
    BOOLEAN ReuseAddress;
    BOOLEAN PeerValid;
    BOOLEAN PeerLoopbackV6;
    volatile LONG Error;
    ULONG PeerAddress;
    USHORT PeerPort;
    ULONG BoundAddress;
    ULONG BoundIndex;
    LONG64 RecvTimeout;
    LONG64 SendTimeout;
    ULONG OptionCount;
    LX_INET_OPTION Options[LX_INET_OPTIONS];
    LX_POLL_HEAD Poll;
    KSPIN_LOCK SelectLock;
    UCHAR Watching[AFD_MAX_EVENT];
    UCHAR ConnectBuffer[FIELD_OFFSET(AFD_CONNECT_INFO, RemoteAddress) + sizeof(TA_IP_ADDRESS)];
    ULONG64 Ino;
} LX_INET_SOCKET, *PLX_INET_SOCKET;

typedef struct _LX_INET_WORK
{
    WORK_QUEUE_ITEM Item;
    PLX_INET_SOCKET Socket;
} LX_INET_WORK, *PLX_INET_WORK;

typedef struct _LX_INET_SELECT
{
    PLX_INET_SOCKET Socket;
    ULONG Mask;
    AFD_POLL_INFO Info;
} LX_INET_SELECT, *PLX_INET_SELECT;

static volatile LONG64 LxInetInode = 0x400000;

static PLX_INET_OPTION LxInetFindOption(_In_ PLX_INET_SOCKET Socket, _In_ ULONG Level, _In_ ULONG Name);
static LONG64 LxInetRead(_In_ PLX_FILE File, _In_ ULONG64 Buffer, _In_ ULONG64 Length, _Inout_opt_ PLONG64 Offset);
static LONG64 LxInetWrite(_In_ PLX_FILE File, _In_ ULONG64 Buffer, _In_ ULONG64 Length, _Inout_opt_ PLONG64 Offset);
static LONG64 LxInetIoctl(_In_ PLX_FILE File, _In_ ULONG Request, _In_ ULONG64 Argument);
static ULONG LxInetPoll(_In_ PLX_FILE File, _In_opt_ PLX_POLL_ENTRY Entry);
static LONG LxInetStat(_In_ PLX_FILE File, _Out_ PLX_STAT Stat);
static VOID LxInetRelease(_In_ PLX_FILE File);

static const LX_FILE_OPS LxInetOps =
{
    LxInetRead, LxInetWrite, LxInetIoctl, LxInetPoll, LxInetStat, NULL, NULL, NULL, LxInetRelease
};

static
VOID
LxInetLock(
    _In_ PLX_INET_SOCKET Socket)
{
    KeWaitForSingleObject(&Socket->Lock, Executive, KernelMode, FALSE, NULL);
}

static
VOID
LxInetUnlock(
    _In_ PLX_INET_SOCKET Socket)
{
    KeSetEvent(&Socket->Lock, IO_NO_INCREMENT, FALSE);
}

static
VOID
LxInetReference(
    _In_ PLX_INET_SOCKET Socket)
{
    InterlockedIncrement(&Socket->RefCount);
}

static
VOID
LxInetDereference(
    _In_ PLX_INET_SOCKET Socket)
{
    if (InterlockedDecrement(&Socket->RefCount) != 0)
        return;
    if (Socket->FileObject != NULL)
        ObDereferenceObject(Socket->FileObject);
    LxFree(Socket);
}

static
LONG
LxInetErrno(
    _In_ NTSTATUS Status)
{
    switch (Status)
    {
        case STATUS_CANT_WAIT:
            return LX_EAGAIN;
        case STATUS_CONNECTION_REFUSED:
        case STATUS_PORT_UNREACHABLE:
            return LX_ECONNREFUSED;
        case STATUS_IO_TIMEOUT:
            return LX_ETIMEDOUT;
        case STATUS_NETWORK_UNREACHABLE:
            return LX_ENETUNREACH;
        case STATUS_HOST_UNREACHABLE:
        case STATUS_PROTOCOL_UNREACHABLE:
            return LX_EHOSTUNREACH;
        case STATUS_CONNECTION_RESET:
        case STATUS_REMOTE_DISCONNECT:
            return LX_ECONNRESET;
        case STATUS_CONNECTION_ABORTED:
        case STATUS_LOCAL_DISCONNECT:
            return LX_ECONNABORTED;
        case STATUS_ADDRESS_ALREADY_EXISTS:
        case STATUS_ADDRESS_ALREADY_ASSOCIATED:
        case STATUS_SHARING_VIOLATION:
            return LX_EADDRINUSE;
        case STATUS_INVALID_ADDRESS_COMPONENT:
        case STATUS_INVALID_ADDRESS:
            return LX_EADDRNOTAVAIL;
        case STATUS_INVALID_CONNECTION:
            return LX_ENOTCONN;
        case STATUS_GRACEFUL_DISCONNECT:
        case STATUS_PIPE_DISCONNECTED:
            return LX_EPIPE;
        default:
            return LxStatusToErrno(Status);
    }
}

static
NTSTATUS
LxAfdIoctl(
    _In_ PLX_INET_SOCKET Socket,
    _In_ ULONG Code,
    _In_opt_ PVOID Input,
    _In_ ULONG InputLength,
    _Out_opt_ PVOID Output,
    _In_ ULONG OutputLength,
    _Out_opt_ PULONG_PTR Information)
{
    IO_STATUS_BLOCK Iosb;
    NTSTATUS Status;

    Iosb.Information = 0;
    Status = ZwDeviceIoControlFile(Socket->Handle, NULL, NULL, NULL, &Iosb, Code, Input, InputLength, Output, OutputLength);
    if (Status == STATUS_PENDING)
        Status = Iosb.Status;
    if (Information != NULL)
        *Information = Iosb.Information;
    return Status;
}

static
NTSTATUS
LxAfdCreate(
    _In_ ULONG Type,
    _Out_ PHANDLE Handle)
{
    static const WCHAR Endpoint[] = L"\\Device\\Afd\\Endpoint";
    PCWSTR Transport = (Type == LX_SOCK_STREAM) ? L"\\Device\\Tcp" : L"\\Device\\Udp";
    ULONG TransportBytes = (ULONG)(wcslen(Transport) * sizeof(WCHAR));
    ULONG ValueLength = FIELD_OFFSET(AFD_CREATE_PACKET_NT6, TransportName) + TransportBytes + sizeof(WCHAR);
    ULONG EaLength = FIELD_OFFSET(FILE_FULL_EA_INFORMATION, EaName) + AFD_PACKET_COMMAND_LENGTH + 1 + ValueLength;
    PFILE_FULL_EA_INFORMATION Ea;
    PAFD_CREATE_PACKET_NT6 Packet;
    OBJECT_ATTRIBUTES ObjectAttributes;
    UNICODE_STRING Name;
    IO_STATUS_BLOCK Iosb;
    NTSTATUS Status;

    Ea = LxAlloc(EaLength);
    if (Ea == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;
    Ea->EaNameLength = AFD_PACKET_COMMAND_LENGTH;
    Ea->EaValueLength = (USHORT)ValueLength;
    RtlCopyMemory(Ea->EaName, AfdCommand, AFD_PACKET_COMMAND_LENGTH + 1);
    Packet = (PAFD_CREATE_PACKET_NT6)(Ea->EaName + AFD_PACKET_COMMAND_LENGTH + 1);
    Packet->EndpointFlags = (Type == LX_SOCK_STREAM) ? 0 : (AFD_ENDPOINT_CONNECTIONLESS | AFD_ENDPOINT_MESSAGE_ORIENTED);
    Packet->GroupID = 0;
    Packet->AddressFamily = LX_AF_INET;
    Packet->SocketType = (Type == LX_SOCK_STREAM) ? 1 : 2;
    Packet->Protocol = (Type == LX_SOCK_STREAM) ? LX_IPPROTO_TCP : LX_IPPROTO_UDP;
    Packet->SizeOfTransportName = TransportBytes;
    RtlCopyMemory(Packet->TransportName, Transport, TransportBytes + sizeof(WCHAR));

    RtlInitUnicodeString(&Name, Endpoint);
    InitializeObjectAttributes(&ObjectAttributes, &Name, OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, NULL, NULL);
    Status = ZwCreateFile(Handle,
                          GENERIC_READ | GENERIC_WRITE | SYNCHRONIZE,
                          &ObjectAttributes,
                          &Iosb,
                          NULL,
                          0,
                          FILE_SHARE_READ | FILE_SHARE_WRITE,
                          FILE_OPEN_IF,
                          FILE_SYNCHRONOUS_IO_NONALERT,
                          Ea,
                          EaLength);
    LxFree(Ea);
    return Status;
}

static
PLX_INET_SOCKET
LxInetCreate(
    _In_ ULONG Family,
    _In_ ULONG Type,
    _In_ ULONG Protocol,
    _Out_ PLONG Error)
{
    PLX_INET_SOCKET Socket = LxAlloc(sizeof(*Socket));
    AFD_INFO Info;
    NTSTATUS Status;

    if (Socket == NULL)
    {
        *Error = -LX_ENOMEM;
        return NULL;
    }

    Socket->RefCount = 1;
    KeInitializeEvent(&Socket->Lock, SynchronizationEvent, TRUE);
    KeInitializeSpinLock(&Socket->SelectLock);
    LxPollHeadInit(&Socket->Poll);
    Socket->Family = Family;
    Socket->Type = Type;
    Socket->Protocol = Protocol;
    Socket->Ino = (ULONG64)InterlockedIncrement64(&LxInetInode);

    Status = LxAfdCreate(Type, &Socket->Handle);
    if (NT_SUCCESS(Status))
    {
        Status = ObReferenceObjectByHandle(Socket->Handle, 0, *IoFileObjectType, KernelMode,
                                           (PVOID*)&Socket->FileObject, NULL);
    }
    if (NT_SUCCESS(Status))
    {
        Socket->DeviceObject = IoGetRelatedDeviceObject(Socket->FileObject);
        RtlZeroMemory(&Info, sizeof(Info));
        Info.InformationClass = AFD_INFO_BLOCKING_MODE;
        Info.Information.Boolean = TRUE;
        Status = LxAfdIoctl(Socket, IOCTL_AFD_SET_INFO, &Info, sizeof(Info), NULL, 0, NULL);
    }
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("Linux socket cannot open an AFD endpoint: 0x%08lx\n", Status);
        if (Socket->Handle != NULL)
            ZwClose(Socket->Handle);
        LxInetDereference(Socket);
        *Error = (Status == STATUS_OBJECT_NAME_NOT_FOUND) ? -LX_EAFNOSUPPORT : -LxInetErrno(Status);
        return NULL;
    }
    *Error = 0;
    return Socket;
}

static
PLX_FILE
LxInetFileCreate(
    _In_ PLX_INET_SOCKET Socket,
    _In_ ULONG Flags)
{
    PLX_FILE File = LxFileAlloc(&LxInetOps, LX_O_RDWR | (Flags & LX_O_NONBLOCK), LX_S_IFSOCK);

    if (File == NULL)
        return NULL;
    File->Object = Socket;
    File->Ino = Socket->Ino;
    File->Path = LxStrPrintf("socket:[%llu]", Socket->Ino);
    return File;
}

static
VOID
LxInetSetAddress(
    _Out_ PTA_IP_ADDRESS Target,
    _In_ ULONG Address,
    _In_ USHORT Port)
{
    RtlZeroMemory(Target, sizeof(*Target));
    Target->TAAddressCount = 1;
    Target->Address[0].AddressLength = TDI_ADDRESS_LENGTH_IP;
    Target->Address[0].AddressType = TDI_ADDRESS_TYPE_IP;
    Target->Address[0].Address[0].sin_port = Port;
    Target->Address[0].Address[0].in_addr = Address;
}

static
LONG
LxInetParseAddress(
    _In_ PLX_INET_SOCKET Socket,
    _In_ ULONG64 UserAddress,
    _In_ ULONG Length,
    _In_ BOOLEAN Connect,
    _Out_ PULONG Address,
    _Out_ PUSHORT Port,
    _Out_ PBOOLEAN LoopbackV6)
{
    UCHAR Raw[sizeof(LX_SOCKADDR_IN6)];
    USHORT Family;
    LONG64 Result;

    *LoopbackV6 = FALSE;
    if (Length < sizeof(USHORT))
        return -LX_EINVAL;
    Result = LxCopyFromUser(Raw, UserAddress, min(Length, sizeof(Raw)));
    if (Result != 0)
        return (LONG)Result;
    Family = *(PUSHORT)Raw;

    if (Socket->Family == LX_AF_INET)
    {
        PLX_SOCKADDR_IN In = (PLX_SOCKADDR_IN)Raw;

        if (Length < sizeof(LX_SOCKADDR_IN))
            return -LX_EINVAL;
        if ((Family != LX_AF_INET) && !(Connect && (Family == LX_AF_UNSPEC)))
            return -LX_EAFNOSUPPORT;
        *Address = In->Address;
        *Port = In->Port;
        return 0;
    }
    else
    {
        PLX_SOCKADDR_IN6 In6 = (PLX_SOCKADDR_IN6)Raw;
        static const UCHAR Any[16] = { 0 };
        static const UCHAR Loopback[16] = { 0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,1 };
        static const UCHAR Mapped[12] = { 0,0,0,0, 0,0,0,0, 0,0,0xFF,0xFF };

        if (Length < 24)
            return -LX_EINVAL;
        if (Family != LX_AF_INET6)
            return -LX_EAFNOSUPPORT;
        *Port = In6->Port;
        if (RtlCompareMemory(In6->Address, Any, 16) == 16)
            *Address = 0;
        else if (RtlCompareMemory(In6->Address, Loopback, 16) == 16)
        {
            *Address = 0x0100007F;
            *LoopbackV6 = TRUE;
        }
        else if (RtlCompareMemory(In6->Address, Mapped, 12) == 12)
            RtlCopyMemory(Address, In6->Address + 12, sizeof(ULONG));
        else
            return Connect ? -LX_ENETUNREACH : -LX_EADDRNOTAVAIL;
        return 0;
    }
}

static
ULONG
LxInetFormatAddress(
    _In_ PLX_INET_SOCKET Socket,
    _In_ ULONG Address,
    _In_ USHORT Port,
    _In_ BOOLEAN LoopbackV6,
    _Out_writes_bytes_(sizeof(LX_SOCKADDR_IN6)) PUCHAR Buffer)
{
    RtlZeroMemory(Buffer, sizeof(LX_SOCKADDR_IN6));
    if (Socket->Family == LX_AF_INET)
    {
        PLX_SOCKADDR_IN In = (PLX_SOCKADDR_IN)Buffer;

        In->Family = LX_AF_INET;
        In->Port = Port;
        In->Address = Address;
        return sizeof(LX_SOCKADDR_IN);
    }
    else
    {
        PLX_SOCKADDR_IN6 In6 = (PLX_SOCKADDR_IN6)Buffer;

        In6->Family = LX_AF_INET6;
        In6->Port = Port;
        if (LoopbackV6 && (Address == 0x0100007F))
        {
            In6->Address[15] = 1;
        }
        else if (Address != 0)
        {
            In6->Address[10] = 0xFF;
            In6->Address[11] = 0xFF;
            RtlCopyMemory(In6->Address + 12, &Address, sizeof(ULONG));
        }
        return sizeof(LX_SOCKADDR_IN6);
    }
}

static
LONG64
LxInetCopyAddressOut(
    _In_ PLX_INET_SOCKET Socket,
    _In_ ULONG Address,
    _In_ USHORT Port,
    _In_ BOOLEAN LoopbackV6,
    _In_ ULONG64 UserAddress,
    _In_ ULONG64 UserLength)
{
    UCHAR Buffer[sizeof(LX_SOCKADDR_IN6)];
    ULONG Length = LxInetFormatAddress(Socket, Address, Port, LoopbackV6, Buffer);
    LONG Available;
    LONG64 Result;

    if ((UserAddress == 0) || (UserLength == 0))
        return 0;
    Result = LxCopyFromUser(&Available, UserLength, sizeof(Available));
    if (Result != 0)
        return Result;
    if (Available < 0)
        return -LX_EINVAL;
    Result = LxCopyToUser(UserAddress, Buffer, min((ULONG)Available, Length));
    if (Result == 0)
        Result = LxCopyToUser(UserLength, &Length, sizeof(Length));
    return Result;
}

static
ULONG
LxInetMapEvents(
    _In_ PLX_INET_SOCKET Socket,
    _In_ ULONG Events)
{
    ULONG Mask = 0;

    if (Events & (AFD_EVENT_RECEIVE | AFD_EVENT_ACCEPT))
        Mask |= LX_POLLIN | LX_POLLRDNORM;
    if (Events & AFD_EVENT_OOB_RECEIVE)
        Mask |= LX_POLLPRI;
    if (Events & (AFD_EVENT_SEND | AFD_EVENT_CONNECT))
        Mask |= LX_POLLOUT | LX_POLLWRNORM;
    if (Events & AFD_EVENT_DISCONNECT)
        Mask |= LX_POLLIN | LX_POLLRDNORM | LX_POLLRDHUP;
    if (Events & (AFD_EVENT_ABORT | AFD_EVENT_CLOSE))
        Mask |= LX_POLLIN | LX_POLLERR | LX_POLLHUP | LX_POLLRDHUP;
    if (Events & AFD_EVENT_CONNECT_FAIL)
        Mask |= LX_POLLOUT | LX_POLLERR | LX_POLLHUP;
    if ((Socket->Type == LX_SOCK_DGRAM) && !(Events & AFD_EVENT_SEND))
        Mask |= LX_POLLOUT | LX_POLLWRNORM;
    return Mask;
}

static
ULONG
LxInetCurrentEvents(
    _In_ PLX_INET_SOCKET Socket)
{
    AFD_POLL_INFO Info;
    ULONG_PTR Information;

    RtlZeroMemory(&Info, sizeof(Info));
    Info.Timeout.QuadPart = 0;
    Info.HandleCount = 1;
    Info.Handles[0].Handle = (SOCKET)Socket->Handle;
    Info.Handles[0].Events = LX_INET_EVENTS;
    if (!NT_SUCCESS(LxAfdIoctl(Socket, IOCTL_AFD_SELECT, &Info, sizeof(Info), &Info, sizeof(Info), &Information)))
        return 0;
    return Info.Handles[0].Events & LX_INET_EVENTS;
}

static VOID NTAPI LxInetSelectWork(_In_ PVOID Context);

static
PIRP
LxInetBuildIrp(
    _In_ PLX_INET_SOCKET Socket,
    _In_ ULONG Code,
    _In_ PVOID Buffer,
    _In_ ULONG Length,
    _In_ PIO_COMPLETION_ROUTINE Routine,
    _In_opt_ PVOID Context)
{
    PIRP Irp = IoAllocateIrp(Socket->DeviceObject->StackSize, FALSE);
    PIO_STACK_LOCATION Stack;

    if (Irp == NULL)
        return NULL;
    Irp->RequestorMode = KernelMode;
    Irp->Tail.Overlay.OriginalFileObject = Socket->FileObject;
    if (METHOD_FROM_CTL_CODE(Code) == METHOD_BUFFERED)
    {
        Irp->AssociatedIrp.SystemBuffer = Buffer;
        Irp->UserBuffer = Buffer;
    }
    Stack = IoGetNextIrpStackLocation(Irp);
    Stack->MajorFunction = IRP_MJ_DEVICE_CONTROL;
    Stack->FileObject = Socket->FileObject;
    Stack->Parameters.DeviceIoControl.IoControlCode = Code;
    Stack->Parameters.DeviceIoControl.InputBufferLength = Length;
    Stack->Parameters.DeviceIoControl.OutputBufferLength = (METHOD_FROM_CTL_CODE(Code) == METHOD_BUFFERED) ? Length : 0;
    Stack->Parameters.DeviceIoControl.Type3InputBuffer = Buffer;
    IoSetCompletionRoutine(Irp, Routine, Context, TRUE, TRUE, TRUE);
    return Irp;
}

static
VOID
LxInetFreeIrp(
    _In_ PIRP Irp)
{
    while (Irp->MdlAddress != NULL)
    {
        PMDL Mdl = Irp->MdlAddress;

        Irp->MdlAddress = Mdl->Next;
        if (Mdl->MdlFlags & MDL_PAGES_LOCKED)
            MmUnlockPages(Mdl);
        IoFreeMdl(Mdl);
    }
    IoFreeIrp(Irp);
}

static
VOID
LxInetQueueWork(
    _In_ PLX_INET_SOCKET Socket)
{
    PLX_INET_WORK Work = ExAllocatePoolZero(NonPagedPoolNx, sizeof(*Work), LX_TAG);

    if (Work == NULL)
        return;
    Work->Socket = Socket;
    ExInitializeWorkItem(&Work->Item, LxInetSelectWork, Work);
    ExQueueWorkItem(&Work->Item, DelayedWorkQueue);
}

static
VOID
LxInetWatch(
    _In_ PLX_INET_SOCKET Socket,
    _In_ ULONG Mask,
    _In_ BOOLEAN Add)
{
    KIRQL OldIrql;
    ULONG Bit;

    KeAcquireSpinLock(&Socket->SelectLock, &OldIrql);
    for (Bit = 0; Bit < AFD_MAX_EVENT; Bit++)
    {
        if (Mask & (1UL << Bit))
        {
            if (Add)
                Socket->Watching[Bit]++;
            else
                Socket->Watching[Bit]--;
        }
    }
    KeReleaseSpinLock(&Socket->SelectLock, OldIrql);
}

static
NTSTATUS
NTAPI
LxInetSelectComplete(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp,
    _In_opt_ PVOID Context)
{
    PLX_INET_SELECT Select = Context;
    PLX_INET_SOCKET Socket = Select->Socket;
    ULONG Events = NT_SUCCESS(Irp->IoStatus.Status) ? (Select->Info.Handles[0].Events & Select->Mask) : 0;

    UNREFERENCED_PARAMETER(DeviceObject);
    LxInetFreeIrp(Irp);
    LxInetWatch(Socket, Select->Mask, FALSE);
    ExFreePoolWithTag(Select, LX_TAG);
    if (Events != 0)
        LxPollWake(&Socket->Poll, LxInetMapEvents(Socket, Events));
    LxInetQueueWork(Socket);
    return STATUS_MORE_PROCESSING_REQUIRED;
}

static
BOOLEAN
LxInetHasWaiters(
    _In_ PLX_INET_SOCKET Socket)
{
    KIRQL OldIrql;
    BOOLEAN Waiters;

    KeAcquireSpinLock(&Socket->Poll.Lock, &OldIrql);
    Waiters = !IsListEmpty(&Socket->Poll.Entries);
    KeReleaseSpinLock(&Socket->Poll.Lock, OldIrql);
    return Waiters;
}

static
VOID
LxInetArmWith(
    _In_ PLX_INET_SOCKET Socket,
    _In_ ULONG Current,
    _In_ BOOLEAN Wake)
{
    PLX_INET_SELECT Select;
    ULONG Wanted, Bit;
    KIRQL OldIrql;
    PIRP Irp;

    if (Wake && (Current != 0))
        LxPollWake(&Socket->Poll, LxInetMapEvents(Socket, Current));

    Wanted = LX_INET_EVENTS & ~Current;
    KeAcquireSpinLock(&Socket->SelectLock, &OldIrql);
    for (Bit = 0; Bit < AFD_MAX_EVENT; Bit++)
    {
        if (Socket->Watching[Bit] != 0)
            Wanted &= ~(1UL << Bit);
    }
    KeReleaseSpinLock(&Socket->SelectLock, OldIrql);
    if ((Wanted == 0) || Socket->Closing)
        return;

    Select = ExAllocatePoolZero(NonPagedPoolNx, sizeof(*Select), LX_TAG);
    if (Select == NULL)
        return;
    Select->Socket = Socket;
    Select->Mask = Wanted;
    Select->Info.Timeout.QuadPart = -36000000000LL;
    Select->Info.HandleCount = 1;
    Select->Info.Handles[0].Handle = (SOCKET)Socket->Handle;
    Select->Info.Handles[0].Events = Wanted;

    Irp = LxInetBuildIrp(Socket, IOCTL_AFD_SELECT, &Select->Info, sizeof(Select->Info), LxInetSelectComplete, Select);
    if (Irp == NULL)
    {
        ExFreePoolWithTag(Select, LX_TAG);
        return;
    }
    LxInetWatch(Socket, Wanted, TRUE);
    LxInetReference(Socket);
    IoCallDriver(Socket->DeviceObject, Irp);
}

static
VOID
LxInetArm(
    _In_ PLX_INET_SOCKET Socket)
{
    if (Socket->Closing || !LxInetHasWaiters(Socket))
        return;
    LxInetArmWith(Socket, LxInetCurrentEvents(Socket), TRUE);
}

static
VOID
NTAPI
LxInetSelectWork(
    _In_ PVOID Context)
{
    PLX_INET_WORK Work = Context;
    PLX_INET_SOCKET Socket = Work->Socket;

    ExFreePoolWithTag(Work, LX_TAG);
    LxInetArm(Socket);
    LxInetDereference(Socket);
}

static
BOOLEAN
LxInetNonBlocking(
    _In_ PLX_FILE File,
    _In_ ULONG Flags)
{
    return ((File->Flags & LX_O_NONBLOCK) != 0) || ((Flags & LX_MSG_DONTWAIT) != 0);
}

static
LONG64
LxInetWait(
    _In_ PLX_FILE File,
    _In_ ULONG Events,
    _In_ LONG64 Timeout)
{
    LARGE_INTEGER Due;

    if (Timeout <= 0)
        return LxFileWait(File, Events, NULL);
    Due.QuadPart = -Timeout;
    return LxFileWait(File, Events, &Due);
}

static
LONG64
LxInetTransfer(
    _In_ PLX_FILE File,
    _In_ PLX_INET_SOCKET Socket,
    _In_ BOOLEAN Send,
    _Inout_updates_bytes_(Length) PUCHAR Buffer,
    _In_ ULONG Length,
    _In_ ULONG Flags,
    _Inout_opt_ PTA_IP_ADDRESS Address,
    _Out_opt_ PBOOLEAN Truncated)
{
    AFD_WSABUF Buf;
    ULONG_PTR Information = 0;
    NTSTATUS Status;
    LONG64 Result;

    if (Truncated != NULL)
        *Truncated = FALSE;
    for (;;)
    {
        Buf.len = Length;
        Buf.buf = (PCHAR)Buffer;

        if (Send && (Socket->Type == LX_SOCK_DGRAM))
        {
            AFD_SEND_INFO_UDP Info;

            RtlZeroMemory(&Info, sizeof(Info));
            Info.BufferArray = &Buf;
            Info.BufferCount = 1;
            Info.AfdFlags = AFD_IMMEDIATE;
            Info.TdiConnection.RemoteAddressLength = sizeof(*Address);
            Info.TdiConnection.RemoteAddress = Address;
            Status = LxAfdIoctl(Socket, IOCTL_AFD_SEND_DATAGRAM, &Info, sizeof(Info), NULL, 0, &Information);
        }
        else if (Send)
        {
            AFD_SEND_INFO Info;

            RtlZeroMemory(&Info, sizeof(Info));
            Info.BufferArray = &Buf;
            Info.BufferCount = 1;
            Info.AfdFlags = AFD_IMMEDIATE;
            Status = LxAfdIoctl(Socket, IOCTL_AFD_SEND, &Info, sizeof(Info), NULL, 0, &Information);
        }
        else if (Socket->Type == LX_SOCK_DGRAM)
        {
            AFD_RECV_INFO_UDP Info;
            INT AddressLength = sizeof(*Address);

            RtlZeroMemory(&Info, sizeof(Info));
            Info.BufferArray = &Buf;
            Info.BufferCount = 1;
            Info.AfdFlags = AFD_IMMEDIATE;
            Info.TdiFlags = TDI_RECEIVE_NORMAL | ((Flags & LX_MSG_PEEK) ? TDI_RECEIVE_PEEK : 0);
            Info.Address = Address;
            Info.AddressLength = &AddressLength;
            Status = LxAfdIoctl(Socket, IOCTL_AFD_RECV_DATAGRAM, &Info, sizeof(Info), NULL, 0, &Information);
            if ((Status == STATUS_BUFFER_OVERFLOW) && (Truncated != NULL))
            {
                *Truncated = TRUE;
                Status = STATUS_SUCCESS;
            }
        }
        else
        {
            AFD_RECV_INFO Info;

            RtlZeroMemory(&Info, sizeof(Info));
            Info.BufferArray = &Buf;
            Info.BufferCount = 1;
            Info.AfdFlags = AFD_IMMEDIATE;
            Info.TdiFlags = TDI_RECEIVE_NORMAL | ((Flags & LX_MSG_PEEK) ? TDI_RECEIVE_PEEK : 0);
            Status = LxAfdIoctl(Socket, IOCTL_AFD_RECV, &Info, sizeof(Info), NULL, 0, &Information);
            if ((Status == STATUS_GRACEFUL_DISCONNECT) || (Status == STATUS_END_OF_FILE))
            {
                Status = STATUS_SUCCESS;
                Information = 0;
            }
        }

        if (NT_SUCCESS(Status))
        {
            LxInetArm(Socket);
            return (LONG64)Information;
        }
        if ((Status != STATUS_CANT_WAIT) || LxInetNonBlocking(File, Flags))
            break;

        Result = LxInetWait(File, Send ? LX_POLLOUT : LX_POLLIN, Send ? Socket->SendTimeout : Socket->RecvTimeout);
        if (Result != 0)
            return Result;
    }

    LxInetArm(Socket);
    return -LxInetErrno(Status);
}

LONG64
LxInetSocket(
    _In_ PLX_PROCESS Process,
    _In_ ULONG Family,
    _In_ ULONG Type,
    _In_ ULONG Protocol,
    _In_ ULONG Flags)
{
    PLX_INET_SOCKET Socket;
    PLX_FILE File;
    LONG Error;
    LONG Fd;

    if ((Type != LX_SOCK_STREAM) && (Type != LX_SOCK_DGRAM))
        return (Type == LX_SOCK_RAW) ? -LX_EPROTONOSUPPORT : -LX_ESOCKTNOSUPPORT;
    if ((Type == LX_SOCK_STREAM) && (Protocol != 0) && (Protocol != LX_IPPROTO_TCP))
        return -LX_EPROTONOSUPPORT;
    if ((Type == LX_SOCK_DGRAM) && (Protocol != 0) && (Protocol != LX_IPPROTO_UDP))
        return -LX_EPROTONOSUPPORT;

    Socket = LxInetCreate(Family, Type, (Protocol != 0) ? Protocol :
                          ((Type == LX_SOCK_STREAM) ? LX_IPPROTO_TCP : LX_IPPROTO_UDP), &Error);
    if (Socket == NULL)
        return Error;
    File = LxInetFileCreate(Socket, Flags);
    if (File == NULL)
    {
        Socket->Closing = TRUE;
        ZwClose(Socket->Handle);
        LxInetDereference(Socket);
        return -LX_ENOMEM;
    }
    Fd = LxFdInstall(Process, File, 0, (Flags & LX_SOCK_CLOEXEC) != 0);
    LxFileDereference(File);
    return Fd;
}

BOOLEAN
LxInetIsFile(
    _In_ PLX_FILE File)
{
    return File->Ops == &LxInetOps;
}

static
LONG
LxInetBindAddress(
    _In_ PLX_INET_SOCKET Socket,
    _In_ ULONG Address,
    _In_ USHORT Port)
{
    UCHAR Buffer[FIELD_OFFSET(AFD_BIND_DATA, Address) + sizeof(TA_IP_ADDRESS)];
    PAFD_BIND_DATA Bind = (PAFD_BIND_DATA)Buffer;
    NTSTATUS Status;

    RtlZeroMemory(Buffer, sizeof(Buffer));
    Bind->ShareType = Socket->ReuseAddress ? AFD_SHARE_REUSE : AFD_SHARE_UNIQUE;
    LxInetSetAddress((PTA_IP_ADDRESS)&Bind->Address, Address, Port);
    Status = LxAfdIoctl(Socket, IOCTL_AFD_BIND, Buffer, sizeof(Buffer), Buffer, sizeof(Buffer), NULL);
    if (!NT_SUCCESS(Status))
        return -LxInetErrno(Status);
    Socket->State = LX_INET_BOUND;
    Socket->BoundAddress = Address;
    Socket->BoundIndex = (Address != 0) ? LxNetlinkAddressIndex(Address) : 0;
    return 0;
}

LONG64
LxInetBind(
    _In_ PLX_CALL Call,
    _In_ PLX_FILE File)
{
    PLX_INET_SOCKET Socket = File->Object;
    ULONG Address;
    USHORT Port;
    BOOLEAN LoopbackV6;
    LONG Error;

    Error = LxInetParseAddress(Socket, Call->Args[1], (ULONG)Call->Args[2], FALSE, &Address, &Port, &LoopbackV6);
    if (Error != 0)
        return Error;
    LxInetLock(Socket);
    Error = (Socket->State != LX_INET_CREATED) ? -LX_EINVAL : LxInetBindAddress(Socket, Address, Port);
    LxInetUnlock(Socket);
    return Error;
}

LONG64
LxInetListen(
    _In_ PLX_CALL Call,
    _In_ PLX_FILE File)
{
    PLX_INET_SOCKET Socket = File->Object;
    AFD_LISTEN_DATA Listen;
    LONG Backlog = (LONG)Call->Args[1];
    NTSTATUS Status;
    LONG Error = 0;

    if (Socket->Type != LX_SOCK_STREAM)
        return -LX_EOPNOTSUPP;

    LxInetLock(Socket);
    if (Socket->State == LX_INET_LISTENING)
    {
        LxInetUnlock(Socket);
        return 0;
    }
    if ((Socket->State == LX_INET_CONNECTED) || (Socket->State == LX_INET_CONNECTING))
    {
        LxInetUnlock(Socket);
        return -LX_EINVAL;
    }
    if (Socket->State == LX_INET_CREATED)
        Error = LxInetBindAddress(Socket, 0, 0);
    if (Error == 0)
    {
        RtlZeroMemory(&Listen, sizeof(Listen));
        Listen.Backlog = (Backlog <= 0) ? 1 : (ULONG)min(Backlog, 4096);
        Status = LxAfdIoctl(Socket, IOCTL_AFD_START_LISTEN, &Listen, sizeof(Listen), NULL, 0, NULL);
        if (NT_SUCCESS(Status))
            Socket->State = LX_INET_LISTENING;
        else
            Error = -LxInetErrno(Status);
    }
    LxInetUnlock(Socket);
    LxInetArm(Socket);
    return Error;
}

static
NTSTATUS
NTAPI
LxInetConnectComplete(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp,
    _In_opt_ PVOID Context)
{
    PLX_INET_SOCKET Socket = Context;
    NTSTATUS Status = Irp->IoStatus.Status;

    UNREFERENCED_PARAMETER(DeviceObject);
    LxInetFreeIrp(Irp);
    if (NT_SUCCESS(Status))
    {
        Socket->State = LX_INET_CONNECTED;
        LxPollWake(&Socket->Poll, LX_POLLOUT | LX_POLLWRNORM);
    }
    else
    {
        InterlockedExchange(&Socket->Error,
                            ((Status == STATUS_REMOTE_DISCONNECT) || (Status == STATUS_CONNECTION_RESET)) ?
                            LX_ECONNREFUSED : LxInetErrno(Status));
        Socket->State = LX_INET_BOUND;
        LxPollWake(&Socket->Poll, LX_POLLOUT | LX_POLLERR | LX_POLLHUP);
    }
    LxInetQueueWork(Socket);
    return STATUS_MORE_PROCESSING_REQUIRED;
}

LONG64
LxInetConnect(
    _In_ PLX_CALL Call,
    _In_ PLX_FILE File)
{
    PLX_INET_SOCKET Socket = File->Object;
    PAFD_CONNECT_INFO Connect = (PAFD_CONNECT_INFO)Socket->ConnectBuffer;
    ULONG Address;
    USHORT Port, Family = 0;
    BOOLEAN LoopbackV6;
    KIRQL OldIrql;
    LONG64 Result;
    LONG Error;
    PIRP Irp;

    if ((ULONG)Call->Args[2] >= sizeof(USHORT))
        LxCopyFromUser(&Family, Call->Args[1], sizeof(Family));
    if ((Family == LX_AF_UNSPEC) && (Socket->Type == LX_SOCK_DGRAM))
    {
        Socket->PeerValid = FALSE;
        return 0;
    }

    Error = LxInetParseAddress(Socket, Call->Args[1], (ULONG)Call->Args[2], TRUE, &Address, &Port, &LoopbackV6);
    if (Error != 0)
        return Error;

    LxInetLock(Socket);
    if (Socket->Type == LX_SOCK_DGRAM)
    {
        if (Socket->State == LX_INET_CREATED)
            Error = LxInetBindAddress(Socket, 0, 0);
        if (Error == 0)
        {
            Socket->PeerAddress = Address;
            Socket->PeerPort = Port;
            Socket->PeerLoopbackV6 = LoopbackV6;
            Socket->PeerValid = TRUE;
        }
        LxInetUnlock(Socket);
        return Error;
    }

    if (Socket->State == LX_INET_CONNECTED)
    {
        LxInetUnlock(Socket);
        return -LX_EISCONN;
    }
    if (Socket->State == LX_INET_CONNECTING)
    {
        LxInetUnlock(Socket);
        return -LX_EALREADY;
    }
    if (Socket->State == LX_INET_LISTENING)
    {
        LxInetUnlock(Socket);
        return -LX_EINVAL;
    }

    RtlZeroMemory(Socket->ConnectBuffer, sizeof(Socket->ConnectBuffer));
    LxInetSetAddress((PTA_IP_ADDRESS)&Connect->RemoteAddress, Address, Port);
    Socket->PeerAddress = Address;
    Socket->PeerPort = Port;
    Socket->PeerLoopbackV6 = LoopbackV6;
    InterlockedExchange(&Socket->Error, 0);

    Irp = LxInetBuildIrp(Socket, IOCTL_AFD_CONNECT, Socket->ConnectBuffer, sizeof(Socket->ConnectBuffer),
                         LxInetConnectComplete, Socket);
    if (Irp == NULL)
    {
        LxInetUnlock(Socket);
        return -LX_ENOMEM;
    }
    KeAcquireSpinLock(&Socket->SelectLock, &OldIrql);
    Socket->State = LX_INET_CONNECTING;
    KeReleaseSpinLock(&Socket->SelectLock, OldIrql);
    LxInetReference(Socket);
    LxInetReference(Socket);
    LxInetUnlock(Socket);
    IoCallDriver(Socket->DeviceObject, Irp);

    while (Socket->State == LX_INET_CONNECTING)
    {
        if (File->Flags & LX_O_NONBLOCK)
        {
            LxInetDereference(Socket);
            return -LX_EINPROGRESS;
        }
        Result = LxInetWait(File, LX_POLLOUT, Socket->SendTimeout);
        if (Result != 0)
        {
            LxInetDereference(Socket);
            return (Result == -LX_EAGAIN) ? -LX_EINPROGRESS : Result;
        }
    }

    Error = (Socket->State == LX_INET_CONNECTED) ? 0 : -InterlockedExchange(&Socket->Error, 0);
    LxInetDereference(Socket);
    return Error;
}

LONG64
LxInetAccept(
    _In_ PLX_CALL Call,
    _In_ PLX_FILE File,
    _In_ ULONG Flags)
{
    PLX_INET_SOCKET Socket = File->Object, Child;
    UCHAR Received[FIELD_OFFSET(AFD_RECEIVED_ACCEPT_DATA, Address) + sizeof(TA_IP_ADDRESS)];
    PAFD_RECEIVED_ACCEPT_DATA Pending = (PAFD_RECEIVED_ACCEPT_DATA)Received;
    PTA_IP_ADDRESS Remote = (PTA_IP_ADDRESS)&Pending->Address;
    AFD_ACCEPT_DATA Accept;
    PLX_FILE ChildFile;
    NTSTATUS Status;
    LONG64 Result;
    LONG Error;
    LONG Fd;

    if (Flags & ~(LX_SOCK_NONBLOCK | LX_SOCK_CLOEXEC))
        return -LX_EINVAL;
    if (Socket->State != LX_INET_LISTENING)
        return -LX_EINVAL;

    for (;;)
    {
        RtlZeroMemory(Received, sizeof(Received));
        Status = LxAfdIoctl(Socket, IOCTL_AFD_WAIT_FOR_LISTEN, NULL, 0, Received, sizeof(Received), NULL);
        if (NT_SUCCESS(Status))
            break;
        if ((Status != STATUS_CANT_WAIT) || (File->Flags & LX_O_NONBLOCK))
        {
            LxInetArm(Socket);
            return -LxInetErrno(Status);
        }
        Result = LxInetWait(File, LX_POLLIN, Socket->RecvTimeout);
        if (Result != 0)
            return Result;
    }

    Child = LxInetCreate(Socket->Family, Socket->Type, Socket->Protocol, &Error);
    if (Child == NULL)
    {
        LxInetArm(Socket);
        return Error;
    }

    RtlZeroMemory(&Accept, sizeof(Accept));
    Accept.SequenceNumber = Pending->SequenceNumber;
    Accept.ListenHandle = Child->Handle;
    Status = LxAfdIoctl(Socket, IOCTL_AFD_ACCEPT, &Accept, sizeof(Accept), NULL, 0, NULL);
    LxInetArm(Socket);
    if (!NT_SUCCESS(Status))
    {
        Child->Closing = TRUE;
        ZwClose(Child->Handle);
        LxInetDereference(Child);
        return -LxInetErrno(Status);
    }

    Child->State = LX_INET_CONNECTED;
    Child->PeerAddress = Remote->Address[0].Address[0].in_addr;
    Child->PeerPort = Remote->Address[0].Address[0].sin_port;
    Child->PeerLoopbackV6 = (Socket->Family == LX_AF_INET6) && (Child->PeerAddress == 0x0100007F);
    Child->ReuseAddress = Socket->ReuseAddress;

    ChildFile = LxInetFileCreate(Child, Flags);
    if (ChildFile == NULL)
    {
        Child->Closing = TRUE;
        ZwClose(Child->Handle);
        LxInetDereference(Child);
        return -LX_ENOMEM;
    }
    Fd = LxFdInstall(Call->Process, ChildFile, 0, (Flags & LX_SOCK_CLOEXEC) != 0);
    LxFileDereference(ChildFile);
    if (Fd < 0)
        return Fd;

    Result = LxInetCopyAddressOut(Child, Child->PeerAddress, Child->PeerPort, Child->PeerLoopbackV6,
                                  Call->Args[1], Call->Args[2]);
    if (Result != 0)
    {
        LxFdClose(Call->Process, Fd);
        return Result;
    }
    return Fd;
}

LONG64
LxInetName(
    _In_ PLX_CALL Call,
    _In_ PLX_FILE File,
    _In_ BOOLEAN Peer)
{
    PLX_INET_SOCKET Socket = File->Object;
    UCHAR Buffer[FIELD_OFFSET(TDI_ADDRESS_INFO, Address) + sizeof(TA_IP_ADDRESS)];
    PTA_IP_ADDRESS Address = (PTA_IP_ADDRESS)&((PTDI_ADDRESS_INFO)Buffer)->Address;
    NTSTATUS Status;

    if (Peer)
    {
        if ((Socket->Type == LX_SOCK_DGRAM) ? !Socket->PeerValid : (Socket->State != LX_INET_CONNECTED))
            return -LX_ENOTCONN;
        return LxInetCopyAddressOut(Socket, Socket->PeerAddress, Socket->PeerPort, Socket->PeerLoopbackV6,
                                    Call->Args[1], Call->Args[2]);
    }

    RtlZeroMemory(Buffer, sizeof(Buffer));
    if (Socket->State != LX_INET_CREATED)
    {
        Status = LxAfdIoctl(Socket, IOCTL_AFD_GET_SOCK_NAME, NULL, 0, Buffer, sizeof(Buffer), NULL);
        if (!NT_SUCCESS(Status))
            RtlZeroMemory(Buffer, sizeof(Buffer));
    }
    return LxInetCopyAddressOut(Socket,
                                Address->Address[0].Address[0].in_addr,
                                Address->Address[0].Address[0].sin_port,
                                Socket->PeerLoopbackV6 || (Address->Address[0].Address[0].in_addr == 0x0100007F),
                                Call->Args[1], Call->Args[2]);
}

LONG64
LxInetSendmsg(
    _In_ PLX_FILE File,
    _In_ PLX_MSGHDR Header,
    _In_ BOOLEAN KernelIov,
    _In_ ULONG Flags)
{
    PLX_INET_SOCKET Socket = File->Object;
    TA_IP_ADDRESS Destination;
    PLX_IOVEC Vectors;
    ULONG64 Total, Done = 0;
    PUCHAR Bounce;
    LONG64 Result;

    if (Socket->ShutWrite)
        return (Flags & LX_MSG_NOSIGNAL) ? -LX_EPIPE : LxWriteFailed(LX_EPIPE);

    if (Socket->Type == LX_SOCK_DGRAM)
    {
        ULONG Address;
        USHORT Port;
        BOOLEAN LoopbackV6;

        if ((Header->Name != 0) && (Header->NameLength != 0))
        {
            LONG Error = LxInetParseAddress(Socket, Header->Name, (ULONG)Header->NameLength, TRUE, &Address, &Port, &LoopbackV6);

            if (Error != 0)
                return Error;
        }
        else if (Socket->PeerValid)
        {
            Address = Socket->PeerAddress;
            Port = Socket->PeerPort;
        }
        else
        {
            return -LX_EDESTADDRREQ;
        }
        if (Socket->State == LX_INET_CREATED)
        {
            LONG Error;

            LxInetLock(Socket);
            Error = (Socket->State == LX_INET_CREATED) ? LxInetBindAddress(Socket, 0, 0) : 0;
            LxInetUnlock(Socket);
            if (Error != 0)
                return Error;
        }
        LxInetSetAddress(&Destination, Address, Port);
    }
    else if (Socket->State != LX_INET_CONNECTED)
    {
        return (Socket->State == LX_INET_CONNECTING) ? -LX_EAGAIN : LxWriteFailed(LX_ENOTCONN) ;
    }

    Result = LxSocketGatherIov(Header, KernelIov, &Vectors, &Total);
    if (Result != 0)
        return Result;
    if ((Socket->Type == LX_SOCK_DGRAM) && (Total > 65507))
    {
        LxFree(Vectors);
        return -LX_EMSGSIZE;
    }

    Bounce = LxAllocPaged((SIZE_T)min(max(Total, 1), LX_INET_CHUNK));
    if (Bounce == NULL)
    {
        LxFree(Vectors);
        return -LX_ENOMEM;
    }

    do
    {
        ULONG Chunk = (ULONG)min(Total - Done, LX_INET_CHUNK);
        LONG64 Sent;

        Result = LxSocketCopyIn(Vectors, Header->IovLength, Done, Bounce, Chunk);
        if (Result != 0)
            break;
        Sent = LxInetTransfer(File, Socket, TRUE, Bounce, Chunk, (Done != 0) ? (Flags | LX_MSG_DONTWAIT) : Flags,
                              (Socket->Type == LX_SOCK_DGRAM) ? &Destination : NULL, NULL);
        if (Sent < 0)
        {
            Result = Sent;
            break;
        }
        Done += (ULONG64)Sent;
        if ((ULONG)Sent < Chunk)
            break;
    } while (Done < Total);

    LxFreePaged(Bounce);
    LxFree(Vectors);
    if (Done != 0)
        return (LONG64)Done;
    if ((Result == -LX_EPIPE) || (Result == -LX_ECONNRESET))
        return (Flags & LX_MSG_NOSIGNAL) ? Result : LxWriteFailed((LONG)-Result);
    return Result;
}

static
LONG64
LxInetDeliverPktinfo(
    _In_ PLX_INET_SOCKET Socket,
    _Inout_ PLX_MSGHDR Header)
{
    PLX_INET_OPTION Option;
    struct
    {
        ULONG64 Length;
        LONG Level;
        LONG Type;
        LONG Index;
        ULONG SpecificDestination;
        ULONG Address;
        ULONG Pad;
    } Message;
    ULONG64 Available = Header->ControlLength;
    LONG64 Result;

    Header->ControlLength = 0;
    if ((Socket->Type != LX_SOCK_DGRAM) || (Socket->Family != LX_AF_INET) ||
        (Socket->BoundAddress == 0) || (Header->Control == 0))
    {
        return 0;
    }
    Option = LxInetFindOption(Socket, LX_IPPROTO_IP, LX_IP_PKTINFO);
    if ((Option == NULL) || (Option->Value == 0))
        return 0;
    if (Available < sizeof(Message))
    {
        Header->Flags |= LX_MSG_CTRUNC;
        return 0;
    }

    RtlZeroMemory(&Message, sizeof(Message));
    Message.Length = sizeof(LX_CMSGHDR) + 3 * sizeof(ULONG);
    Message.Level = LX_IPPROTO_IP;
    Message.Type = LX_IP_PKTINFO;
    Message.Index = (LONG)Socket->BoundIndex;
    Message.SpecificDestination = Socket->BoundAddress;
    Message.Address = Socket->BoundAddress;
    Result = LxCopyToUser(Header->Control, &Message, sizeof(Message));
    if (Result == 0)
        Header->ControlLength = sizeof(Message);
    return Result;
}

LONG64
LxInetRecvmsg(
    _In_ PLX_FILE File,
    _Inout_ PLX_MSGHDR Header,
    _In_ BOOLEAN KernelIov,
    _In_ ULONG Flags)
{
    PLX_INET_SOCKET Socket = File->Object;
    TA_IP_ADDRESS Source;
    PLX_IOVEC Vectors;
    ULONG64 Total, Done = 0;
    BOOLEAN Truncated = FALSE;
    PUCHAR Bounce;
    LONG64 Result;

    if (Flags & LX_MSG_OOB)
        return -LX_EINVAL;
    if ((Socket->Type == LX_SOCK_STREAM) && (Socket->State != LX_INET_CONNECTED))
        return -LX_ENOTCONN;
    if (Socket->ShutRead)
        return 0;

    Result = LxSocketGatherIov(Header, KernelIov, &Vectors, &Total);
    if (Result != 0)
        return Result;

    Bounce = LxAllocPaged((SIZE_T)((Socket->Type == LX_SOCK_DGRAM) ? 65536 : min(max(Total, 1), LX_INET_CHUNK)));
    if (Bounce == NULL)
    {
        LxFree(Vectors);
        return -LX_ENOMEM;
    }

    RtlZeroMemory(&Source, sizeof(Source));
    do
    {
        ULONG Chunk = (Socket->Type == LX_SOCK_DGRAM) ? 65536 : (ULONG)min(Total - Done, LX_INET_CHUNK);
        LONG64 Received;

        Received = LxInetTransfer(File, Socket, FALSE, Bounce, Chunk,
                                  ((Done != 0) && !(Flags & LX_MSG_WAITALL)) ? (Flags | LX_MSG_DONTWAIT) : Flags,
                                  (Socket->Type == LX_SOCK_DGRAM) ? &Source : NULL, &Truncated);
        if (Received < 0)
        {
            Result = Received;
            break;
        }
        if (Socket->Type == LX_SOCK_DGRAM)
        {
            if ((ULONG64)Received > Total)
                Truncated = TRUE;
            Result = LxSocketCopyOut(Vectors, Header->IovLength, 0, Bounce, (ULONG)min((ULONG64)Received, Total));
            if (Result == 0)
                Done = (Flags & LX_MSG_TRUNC) ? (ULONG64)Received : min((ULONG64)Received, Total);
            break;
        }
        if (Received == 0)
            break;
        Result = LxSocketCopyOut(Vectors, Header->IovLength, Done, Bounce, (ULONG)Received);
        if (Result != 0)
            break;
        Done += (ULONG64)Received;
        if ((Flags & LX_MSG_PEEK) || !(Flags & LX_MSG_WAITALL))
            break;
    } while (Done < Total);

    LxFreePaged(Bounce);
    LxFree(Vectors);

    Header->Flags = Truncated ? LX_MSG_TRUNC : 0;
    if ((Done == 0) && (Result != 0))
    {
        Header->ControlLength = 0;
        return Result;
    }
    Result = LxInetDeliverPktinfo(Socket, Header);
    if (Result != 0)
        return Result;
    if ((Header->Name != 0) && (Header->NameLength > 0))
    {
        UCHAR Buffer[sizeof(LX_SOCKADDR_IN6)];
        ULONG Length;

        if (Socket->Type == LX_SOCK_DGRAM)
            Length = LxInetFormatAddress(Socket, ((PLX_SOCKADDR_IN)&Source)->Address,
                                         ((PLX_SOCKADDR_IN)&Source)->Port, FALSE, Buffer);
        else
            Length = LxInetFormatAddress(Socket, Socket->PeerAddress, Socket->PeerPort, Socket->PeerLoopbackV6, Buffer);
        Result = LxCopyToUser(Header->Name, Buffer, min((ULONG)Header->NameLength, Length));
        if (Result != 0)
            return Result;
        Header->NameLength = (LONG)Length;
    }
    else
    {
        Header->NameLength = 0;
    }
    return (LONG64)Done;
}

LONG64
LxInetShutdown(
    _In_ PLX_CALL Call,
    _In_ PLX_FILE File)
{
    PLX_INET_SOCKET Socket = File->Object;
    ULONG How = (ULONG)Call->Args[1];
    AFD_DISCONNECT_INFO Disconnect;
    NTSTATUS Status;

    if (How > LX_SHUT_RDWR)
        return -LX_EINVAL;
    if ((Socket->Type == LX_SOCK_STREAM) && (Socket->State != LX_INET_CONNECTED))
        return -LX_ENOTCONN;

    RtlZeroMemory(&Disconnect, sizeof(Disconnect));
    if ((How == LX_SHUT_RD) || (How == LX_SHUT_RDWR))
    {
        Socket->ShutRead = TRUE;
        Disconnect.DisconnectType |= AFD_DISCONNECT_RECV;
    }
    if ((How == LX_SHUT_WR) || (How == LX_SHUT_RDWR))
    {
        Socket->ShutWrite = TRUE;
        Disconnect.DisconnectType |= AFD_DISCONNECT_SEND;
    }
    if (Socket->Type == LX_SOCK_STREAM)
    {
        Disconnect.Timeout.QuadPart = -10000000LL;
        Status = LxAfdIoctl(Socket, IOCTL_AFD_DISCONNECT, &Disconnect, sizeof(Disconnect), NULL, 0, NULL);
        if (!NT_SUCCESS(Status) && (Status != STATUS_INVALID_CONNECTION))
            DPRINT1("Linux socket shutdown failed: 0x%08lx\n", Status);
    }
    LxPollWake(&Socket->Poll, LX_POLLIN | LX_POLLOUT | LX_POLLRDHUP | ((How == LX_SHUT_RDWR) ? LX_POLLHUP : 0));
    return 0;
}

static
PLX_INET_OPTION
LxInetFindOption(
    _In_ PLX_INET_SOCKET Socket,
    _In_ ULONG Level,
    _In_ ULONG Name)
{
    ULONG Index;

    for (Index = 0; Index < Socket->OptionCount; Index++)
    {
        if ((Socket->Options[Index].Level == Level) && (Socket->Options[Index].Name == Name))
            return &Socket->Options[Index];
    }
    return NULL;
}

static
BOOLEAN
LxInetKnownOption(
    _In_ PLX_INET_SOCKET Socket,
    _In_ ULONG Level,
    _In_ ULONG Name,
    _Out_ PLONG Default)
{
    *Default = 0;
    switch (Level)
    {
        case LX_SOL_SOCKET:
            switch (Name)
            {
                case LX_SO_RCVBUF:
                case LX_SO_SNDBUF:
                    *Default = LX_INET_BUFFER;
                    return TRUE;
                case LX_SO_RCVLOWAT:
                case LX_SO_SNDLOWAT:
                    *Default = 1;
                    return TRUE;
                case LX_SO_KEEPALIVE:
                case LX_SO_BROADCAST:
                case LX_SO_DEBUG:
                case LX_SO_PRIORITY:
                case LX_SO_REUSEPORT:
                case LX_SO_TIMESTAMP:
                case LX_SO_TIMESTAMPNS:
                case LX_SO_MARK:
                case LX_SO_BINDTOIFINDEX:
                case LX_SO_PASSCRED:
                case LX_SO_PASSSEC:
                    return TRUE;
            }
            return FALSE;

        case LX_IPPROTO_TCP:
            if (Socket->Type != LX_SOCK_STREAM)
                return FALSE;
            switch (Name)
            {
                case LX_TCP_MAXSEG:
                    *Default = 536;
                    return TRUE;
                case LX_TCP_KEEPIDLE:
                    *Default = 7200;
                    return TRUE;
                case LX_TCP_KEEPINTVL:
                    *Default = 75;
                    return TRUE;
                case LX_TCP_KEEPCNT:
                    *Default = 9;
                    return TRUE;
                case LX_TCP_SYNCNT:
                    *Default = 6;
                    return TRUE;
                case LX_TCP_LINGER2:
                    *Default = 60;
                    return TRUE;
                case LX_TCP_NODELAY:
                case LX_TCP_CORK:
                case LX_TCP_DEFER_ACCEPT:
                case LX_TCP_WINDOW_CLAMP:
                case LX_TCP_QUICKACK:
                case LX_TCP_USER_TIMEOUT:
                case LX_TCP_FASTOPEN:
                case LX_TCP_NOTSENT_LOWAT:
                case LX_TCP_FASTOPEN_CONNECT:
                    return TRUE;
            }
            return FALSE;

        case LX_IPPROTO_IP:
            switch (Name)
            {
                case LX_IP_TTL:
                    *Default = 64;
                    return TRUE;
                case LX_IP_MULTICAST_TTL:
                case LX_IP_MULTICAST_LOOP:
                    *Default = 1;
                    return TRUE;
                case LX_IP_TOS:
                case LX_IP_PKTINFO:
                case LX_IP_MTU_DISCOVER:
                case LX_IP_RECVERR:
                case LX_IP_RECVTTL:
                case LX_IP_RECVTOS:
                case LX_IP_FREEBIND:
                case LX_IP_BIND_ADDRESS_NO_PORT:
                case LX_IP_TRANSPARENT:
                case LX_IP_RECVFRAGSIZE:
                case LX_IP_MULTICAST_ALL:
                case LX_IP_UNICAST_IF:
                    return TRUE;
            }
            return FALSE;

        case LX_IPPROTO_IPV6:
            if (Socket->Family != LX_AF_INET6)
                return FALSE;
            switch (Name)
            {
                case LX_IPV6_UNICAST_HOPS:
                    *Default = 64;
                    return TRUE;
                case LX_IPV6_MULTICAST_HOPS:
                case LX_IPV6_MULTICAST_LOOP:
                    *Default = 1;
                    return TRUE;
                case LX_IPV6_V6ONLY:
                case LX_IPV6_MTU_DISCOVER:
                case LX_IPV6_RECVERR:
                case LX_IPV6_RECVPKTINFO:
                case LX_IPV6_TCLASS:
                case LX_IPV6_RECVHOPLIMIT:
                case LX_IPV6_RECVTCLASS:
                case LX_IPV6_UNICAST_IF:
                case LX_IPV6_RECVFRAGSIZE:
                case LX_IPV6_FREEBIND:
                    return TRUE;
            }
            return FALSE;
    }
    return FALSE;
}

LONG64
LxInetSetsockopt(
    _In_ PLX_CALL Call,
    _In_ PLX_FILE File)
{
    PLX_INET_SOCKET Socket = File->Object;
    ULONG Level = (ULONG)Call->Args[1], Name = (ULONG)Call->Args[2];
    PLX_INET_OPTION Option;
    LONG Value = 0, Default;
    LONG64 Result;

    if (Level == LX_SOL_SOCKET)
    {
        switch (Name)
        {
            case LX_SO_REUSEADDR:
                Result = LxSockoptInt(Call->Args[3], Call->Args[4], &Value);
                if (Result == 0)
                    Socket->ReuseAddress = (Value != 0);
                return Result;

            case LX_SO_RCVTIMEO_OLD:
            case LX_SO_RCVTIMEO_NEW:
            case LX_SO_SNDTIMEO_OLD:
            case LX_SO_SNDTIMEO_NEW:
            {
                LX_TIMEVAL Timeval;

                if (Call->Args[4] < sizeof(Timeval))
                    return -LX_EINVAL;
                Result = LxCopyFromUser(&Timeval, Call->Args[3], sizeof(Timeval));
                if (Result != 0)
                    return Result;
                if ((Name == LX_SO_RCVTIMEO_OLD) || (Name == LX_SO_RCVTIMEO_NEW))
                    Socket->RecvTimeout = Timeval.Seconds * 10000000LL + Timeval.Microseconds * 10;
                else
                    Socket->SendTimeout = Timeval.Seconds * 10000000LL + Timeval.Microseconds * 10;
                return 0;
            }

            case LX_SO_LINGER:
            case LX_SO_RCVBUFFORCE:
            case LX_SO_SNDBUFFORCE:
            case 25:
            case 26:
            case 27:
            case 44:
            case 50:
                return 0;
        }
    }
    if ((Level == LX_IPPROTO_IP) && ((Name == LX_IP_ADD_MEMBERSHIP) || (Name == LX_IP_DROP_MEMBERSHIP) ||
                                     (Name == LX_IP_MULTICAST_IF)))
        return 0;
    if ((Level == LX_IPPROTO_IPV6) && ((Name == LX_IPV6_JOIN_GROUP) || (Name == LX_IPV6_LEAVE_GROUP)))
        return 0;
    if ((Level == LX_IPPROTO_TCP) && (Name == LX_TCP_CONGESTION))
        return 0;

    if (!LxInetKnownOption(Socket, Level, Name, &Default))
    {
        DPRINT1("Linux socket option level %lu name %lu is not supported\n", Level, Name);
        return -LX_ENOPROTOOPT;
    }
    Result = LxSockoptInt(Call->Args[3], Call->Args[4], &Value);
    if (Result != 0)
        return Result;
    if ((Level == LX_SOL_SOCKET) && ((Name == LX_SO_RCVBUF) || (Name == LX_SO_SNDBUF)))
        Value = (LONG)max(min((LONG64)Value * 2, 64 * 1024 * 1024), 2304);

    LxInetLock(Socket);
    Option = LxInetFindOption(Socket, Level, Name);
    if ((Option == NULL) && (Socket->OptionCount < LX_INET_OPTIONS))
    {
        Option = &Socket->Options[Socket->OptionCount++];
        Option->Level = Level;
        Option->Name = Name;
    }
    if (Option != NULL)
        Option->Value = Value;
    LxInetUnlock(Socket);
    return 0;
}

LONG64
LxInetGetsockopt(
    _In_ PLX_CALL Call,
    _In_ PLX_FILE File)
{
    PLX_INET_SOCKET Socket = File->Object;
    ULONG Level = (ULONG)Call->Args[1], Name = (ULONG)Call->Args[2];
    PLX_INET_OPTION Option;
    LONG Value, Default;

    if (Level == LX_SOL_SOCKET)
    {
        switch (Name)
        {
            case LX_SO_TYPE:
                Value = (LONG)Socket->Type;
                return LxSockoptReturn(Call, &Value, sizeof(Value));
            case LX_SO_DOMAIN:
                Value = (LONG)Socket->Family;
                return LxSockoptReturn(Call, &Value, sizeof(Value));
            case LX_SO_PROTOCOL:
                Value = (LONG)Socket->Protocol;
                return LxSockoptReturn(Call, &Value, sizeof(Value));
            case LX_SO_ERROR:
                Value = InterlockedExchange(&Socket->Error, 0);
                return LxSockoptReturn(Call, &Value, sizeof(Value));
            case LX_SO_ACCEPTCONN:
                Value = (Socket->State == LX_INET_LISTENING);
                return LxSockoptReturn(Call, &Value, sizeof(Value));
            case LX_SO_REUSEADDR:
                Value = Socket->ReuseAddress;
                return LxSockoptReturn(Call, &Value, sizeof(Value));
            case LX_SO_LINGER:
            {
                LONG Linger[2] = { 0, 0 };

                return LxSockoptReturn(Call, Linger, sizeof(Linger));
            }
            case LX_SO_RCVTIMEO_OLD:
            case LX_SO_RCVTIMEO_NEW:
            case LX_SO_SNDTIMEO_OLD:
            case LX_SO_SNDTIMEO_NEW:
            {
                LX_TIMEVAL Timeval;
                LONG64 Timeout = ((Name == LX_SO_RCVTIMEO_OLD) || (Name == LX_SO_RCVTIMEO_NEW)) ?
                                 Socket->RecvTimeout : Socket->SendTimeout;

                Timeval.Seconds = Timeout / 10000000LL;
                Timeval.Microseconds = (Timeout % 10000000LL) / 10;
                return LxSockoptReturn(Call, &Timeval, sizeof(Timeval));
            }
        }
    }

    if (!LxInetKnownOption(Socket, Level, Name, &Default))
    {
        DPRINT1("Linux socket option level %lu name %lu cannot be read\n", Level, Name);
        return -LX_ENOPROTOOPT;
    }
    LxInetLock(Socket);
    Option = LxInetFindOption(Socket, Level, Name);
    Value = (Option != NULL) ? Option->Value : Default;
    LxInetUnlock(Socket);
    return LxSockoptReturn(Call, &Value, sizeof(Value));
}

static
LONG64
LxInetRead(
    _In_ PLX_FILE File,
    _In_ ULONG64 Buffer,
    _In_ ULONG64 Length,
    _Inout_opt_ PLONG64 Offset)
{
    LX_MSGHDR Header;
    LX_IOVEC Vector;

    UNREFERENCED_PARAMETER(Offset);
    RtlZeroMemory(&Header, sizeof(Header));
    Vector.Base = Buffer;
    Vector.Length = Length;
    Header.Iov = (ULONG64)(ULONG_PTR)&Vector;
    Header.IovLength = 1;
    return LxInetRecvmsg(File, &Header, TRUE, 0);
}

static
LONG64
LxInetWrite(
    _In_ PLX_FILE File,
    _In_ ULONG64 Buffer,
    _In_ ULONG64 Length,
    _Inout_opt_ PLONG64 Offset)
{
    LX_MSGHDR Header;
    LX_IOVEC Vector;

    UNREFERENCED_PARAMETER(Offset);
    RtlZeroMemory(&Header, sizeof(Header));
    Vector.Base = Buffer;
    Vector.Length = Length;
    Header.Iov = (ULONG64)(ULONG_PTR)&Vector;
    Header.IovLength = 1;
    return LxInetSendmsg(File, &Header, TRUE, 0);
}

static
LONG64
LxInetIoctl(
    _In_ PLX_FILE File,
    _In_ ULONG Request,
    _In_ ULONG64 Argument)
{
    PLX_INET_SOCKET Socket = File->Object;
    AFD_INFO Info;
    LONG Value = 0;

    switch (Request)
    {
        case LX_FIONREAD:
            RtlZeroMemory(&Info, sizeof(Info));
            Info.InformationClass = AFD_INFO_RECEIVE_CONTENT_SIZE;
            if (NT_SUCCESS(LxAfdIoctl(Socket, IOCTL_AFD_GET_INFO, &Info, sizeof(Info), &Info, sizeof(Info), NULL)))
                Value = (LONG)Info.Information.Ulong;
            return LxCopyToUser(Argument, &Value, sizeof(Value));

        case LX_TIOCOUTQ:
            return LxCopyToUser(Argument, &Value, sizeof(Value));

        case LX_FIONBIO:
        {
            LONG64 Result = LxCopyFromUser(&Value, Argument, sizeof(Value));

            if (Result != 0)
                return Result;
            if (Value)
                File->Flags |= LX_O_NONBLOCK;
            else
                File->Flags &= ~LX_O_NONBLOCK;
            return 0;
        }
    }
    return -LX_ENOTTY;
}

static
ULONG
LxInetPoll(
    _In_ PLX_FILE File,
    _In_opt_ PLX_POLL_ENTRY Entry)
{
    PLX_INET_SOCKET Socket = File->Object;
    ULONG Mask, Current;

    if (Entry != NULL)
        LxPollAdd(&Socket->Poll, Entry);

    switch (Socket->State)
    {
        case LX_INET_CREATED:
        case LX_INET_BOUND:
            if (Socket->Type == LX_SOCK_STREAM)
            {
                Mask = LX_POLLOUT | LX_POLLWRNORM | LX_POLLHUP;
                if (Socket->Error != 0)
                    Mask |= LX_POLLERR;
                return Mask;
            }
            break;
        case LX_INET_CONNECTING:
            if (Entry != NULL)
                LxInetArmWith(Socket, LxInetCurrentEvents(Socket), FALSE);
            return 0;
    }

    Current = LxInetCurrentEvents(Socket);
    Mask = LxInetMapEvents(Socket, Current);
    if (Socket->State == LX_INET_LISTENING)
        Mask &= LX_POLLIN | LX_POLLRDNORM | LX_POLLERR;
    if (Socket->ShutRead)
        Mask |= LX_POLLIN | LX_POLLRDNORM | LX_POLLRDHUP;
    if (Socket->ShutRead && Socket->ShutWrite)
        Mask |= LX_POLLHUP;
    if (Socket->Error != 0)
        Mask |= LX_POLLERR;
    if ((Entry != NULL) && !Socket->Closing)
        LxInetArmWith(Socket, Current, FALSE);
    return Mask;
}

static
LONG
LxInetStat(
    _In_ PLX_FILE File,
    _Out_ PLX_STAT Stat)
{
    RtlZeroMemory(Stat, sizeof(*Stat));
    Stat->Mode = LX_S_IFSOCK | 0777;
    Stat->Nlink = 1;
    Stat->Ino = File->Ino;
    Stat->BlkSize = 4096;
    return 0;
}

static
VOID
LxInetRelease(
    _In_ PLX_FILE File)
{
    PLX_INET_SOCKET Socket = File->Object;
    KIRQL OldIrql;
    HANDLE Handle;

    if (Socket == NULL)
        return;
    KeAcquireSpinLock(&Socket->SelectLock, &OldIrql);
    Socket->Closing = TRUE;
    Handle = Socket->Handle;
    KeReleaseSpinLock(&Socket->SelectLock, OldIrql);
    ZwClose(Handle);
    LxInetDereference(Socket);
}

const LX_SOCKET_OPS LxInetSocketOps =
{
    LxInetBind,
    LxInetListen,
    LxInetConnect,
    LxInetAccept,
    LxInetName,
    LxInetSendmsg,
    LxInetRecvmsg,
    LxInetShutdown,
    LxInetSetsockopt,
    LxInetGetsockopt
};
