/*
 * PROJECT:     LiberNT Bluetooth stack
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     TDI transport providing AF_BTH RFCOMM stream sockets
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "bthmini.h"
#include <tdikrnl.h>
#include <reactos/tdibth.h>
#include <ws2bth.h>
#include <wdmsec.h>

#define NDEBUG
#include <debug.h>

#define BTHTDI_TAG              'dTtB'
#define BTHTDI_RING_SIZE        0x8000
#define BTHTDI_TX_LIMIT         0x8000
#define BTHTDI_BACKLOG          8
#define BTHTDI_SYNC_TIMEOUT_MS  10000
#define BTHTDI_MAX_RADIOS       4
#define BTHTDI_MAX_KEYED        64

typedef enum _BTHTDI_FILE_TYPE
{
    BthTdiFileControl,
    BthTdiFileAddress,
    BthTdiFileConnection
} BTHTDI_FILE_TYPE;

typedef enum _BTHTDI_STATE
{
    BthTdiIdle,
    BthTdiConnecting,
    BthTdiConnected,
    BthTdiClosed
} BTHTDI_STATE;

typedef struct _BTHTDI_ADDRESS
{
    LONG RefCount;
    LIST_ENTRY Link;
    ULONG Port;
    UCHAR Channel;
    BOOLEAN Closing;
    BOOLEAN Listening;
    BTH_ADDR LocalRadio;
    PBTHMINI_RFCOMM_SERVER Servers[BTHTDI_MAX_RADIOS];
    LIST_ENTRY ListenIrps;
    LIST_ENTRY Pending;
    ULONG PendingCount;
} BTHTDI_ADDRESS, *PBTHTDI_ADDRESS;

typedef struct _BTHTDI_CONNECTION
{
    LONG RefCount;
    LIST_ENTRY Link;
    PBTHTDI_ADDRESS Address;
    PBTHMINI_DEVICE Dev;
    BOOLEAN Internal;
    BOOLEAN Cleanup;
    BOOLEAN RemoteClosed;
    BTHTDI_STATE State;
    PBTHMINI_DLC Dlc;
    BTH_ADDR RemoteAddress;
    ULONG RemotePort;
    GUID ServiceClass;
    LIST_ENTRY ConnectIrps;
    PIRP AcceptedListen;
    LIST_ENTRY Receives;
    LIST_ENTRY Sends;
    LIST_ENTRY ActiveLink;
    PUCHAR Ring;
    ULONG RingHead;
    ULONG RingCount;
} BTHTDI_CONNECTION, *PBTHTDI_CONNECTION;

typedef enum _BTHTDI_SYNC_OPERATION
{
    BthTdiSyncBind,
    BthTdiSyncCleanupAddress,
    BthTdiSyncCleanupConnection
} BTHTDI_SYNC_OPERATION;

typedef struct _BTHTDI_SYNC
{
    LIST_ENTRY Link;
    BTHTDI_SYNC_OPERATION Operation;
    PVOID Object;
    KEVENT Done;
    NTSTATUS Status;
} BTHTDI_SYNC, *PBTHTDI_SYNC;

typedef struct _BTHTDI_KEYED
{
    PBTHMINI_DEVICE Dev;
    BTH_ADDR Address;
} BTHTDI_KEYED;

static const GUID BthTdiNullGuid;
static WDFDEVICE BthTdiDevice;
static KSPIN_LOCK BthTdiLock;
static PBTHMINI_DEVICE BthTdiRadio;
static PBTHMINI_DEVICE BthTdiRadios[BTHTDI_MAX_RADIOS];
static BTHTDI_KEYED BthTdiKeyed[BTHTDI_MAX_KEYED];
static LIST_ENTRY BthTdiAddresses;

static BTHMINI_DLC_EVENT BthTdiDlcOpened;
static BTHMINI_DLC_RECEIVE BthTdiDlcReceive;
static BTHMINI_DLC_EVENT BthTdiDlcClosed;
static BTHMINI_DLC_ROOM BthTdiDlcRoom;
static BTHMINI_RFCOMM_ACCEPT BthTdiAccept;
static BTHMINI_SDP_CALLBACK BthTdiSdpDone;

static const BTHMINI_DLC_CALLBACKS BthTdiDlcCallbacks =
{
    BthTdiDlcOpened,
    BthTdiDlcReceive,
    BthTdiDlcClosed,
    BthTdiDlcRoom
};

static
VOID
BthTdiReferenceAddress(
    _In_ PBTHTDI_ADDRESS Address)
{
    InterlockedIncrement(&Address->RefCount);
}

static
VOID
BthTdiDereferenceAddress(
    _In_ PBTHTDI_ADDRESS Address)
{
    if (InterlockedDecrement(&Address->RefCount) == 0)
        ExFreePoolWithTag(Address, BTHTDI_TAG);
}

static
VOID
BthTdiReferenceConnection(
    _In_ PBTHTDI_CONNECTION Connection)
{
    InterlockedIncrement(&Connection->RefCount);
}

static
VOID
BthTdiDereferenceConnection(
    _In_ PBTHTDI_CONNECTION Connection)
{
    if (InterlockedDecrement(&Connection->RefCount) != 0)
        return;
    if (Connection->Address)
        BthTdiDereferenceAddress(Connection->Address);
    if (Connection->Ring)
        ExFreePoolWithTag(Connection->Ring, BTHTDI_TAG);
    ExFreePoolWithTag(Connection, BTHTDI_TAG);
}

static
PBTHTDI_CONNECTION
BthTdiAllocateConnection(VOID)
{
    PBTHTDI_CONNECTION Connection = ExAllocatePoolZero(NonPagedPool, sizeof(*Connection), BTHTDI_TAG);

    if (!Connection)
        return NULL;
    Connection->Ring = ExAllocatePoolWithTag(NonPagedPool, BTHTDI_RING_SIZE, BTHTDI_TAG);
    if (!Connection->Ring)
    {
        ExFreePoolWithTag(Connection, BTHTDI_TAG);
        return NULL;
    }
    Connection->RefCount = 1;
    InitializeListHead(&Connection->Link);
    InitializeListHead(&Connection->ConnectIrps);
    InitializeListHead(&Connection->Receives);
    InitializeListHead(&Connection->Sends);
    InitializeListHead(&Connection->ActiveLink);
    return Connection;
}

static
LONG
BthTdiRadioIndexLocked(
    _In_opt_ PBTHMINI_DEVICE Dev)
{
    ULONG Index;

    for (Index = 0; Dev && Index < BTHTDI_MAX_RADIOS; Index++)
    {
        if (BthTdiRadios[Index] == Dev)
            return (LONG)Index;
    }
    return -1;
}

static
PBTHMINI_DEVICE
BthTdiRadioByAddressLocked(
    _In_ BTH_ADDR Address)
{
    ULONG Index;

    for (Index = 0; Index < BTHTDI_MAX_RADIOS; Index++)
    {
        if (BthTdiRadios[Index] && BthTdiRadios[Index]->Address == Address)
            return BthTdiRadios[Index];
    }
    return NULL;
}

static
PBTHMINI_DEVICE
BthTdiRadioForRemoteLocked(
    _In_ BTH_ADDR Address)
{
    ULONG Index;

    for (Index = 0; Index < BTHTDI_MAX_KEYED; Index++)
    {
        if (BthTdiKeyed[Index].Dev && BthTdiKeyed[Index].Address == Address &&
            BthTdiRadioIndexLocked(BthTdiKeyed[Index].Dev) >= 0)
        {
            return BthTdiKeyed[Index].Dev;
        }
    }
    return NULL;
}

static
ULONG
BthTdiSnapshotRadios(
    _Out_writes_(BTHTDI_MAX_RADIOS) PBTHMINI_DEVICE *Radios)
{
    ULONG Index, Count = 0;
    KIRQL OldIrql;

    KeAcquireSpinLock(&BthTdiLock, &OldIrql);
    for (Index = 0; Index < BTHTDI_MAX_RADIOS; Index++)
    {
        if (BthTdiRadios[Index])
            Radios[Count++] = BthTdiRadios[Index];
    }
    KeReleaseSpinLock(&BthTdiLock, OldIrql);
    return Count;
}

VOID
BthTdiRemoteKeyed(
    _In_ PBTHMINI_DEVICE Dev,
    _In_ BTH_ADDR Address,
    _In_ BOOLEAN Keyed)
{
    LONG Free = -1;
    ULONG Index;
    KIRQL OldIrql;

    KeAcquireSpinLock(&BthTdiLock, &OldIrql);
    for (Index = 0; Index < BTHTDI_MAX_KEYED; Index++)
    {
        if (BthTdiKeyed[Index].Dev == Dev && BthTdiKeyed[Index].Address == Address)
        {
            if (!Keyed)
                BthTdiKeyed[Index].Dev = NULL;
            KeReleaseSpinLock(&BthTdiLock, OldIrql);
            return;
        }
        if (!BthTdiKeyed[Index].Dev && Free < 0)
            Free = (LONG)Index;
    }
    if (Keyed && Free >= 0)
    {
        BthTdiKeyed[Free].Dev = Dev;
        BthTdiKeyed[Free].Address = Address;
    }
    KeReleaseSpinLock(&BthTdiLock, OldIrql);
}

static
VOID
NTAPI
BthTdiCancel(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    KIRQL OldIrql;

    UNREFERENCED_PARAMETER(DeviceObject);
    IoReleaseCancelSpinLock(Irp->CancelIrql);
    KeAcquireSpinLock(&BthTdiLock, &OldIrql);
    RemoveEntryList(&Irp->Tail.Overlay.ListEntry);
    InitializeListHead(&Irp->Tail.Overlay.ListEntry);
    KeReleaseSpinLock(&BthTdiLock, OldIrql);
    Irp->IoStatus.Status = STATUS_CANCELLED;
    Irp->IoStatus.Information = 0;
    IoCompleteRequest(Irp, IO_NETWORK_INCREMENT);
}

static
BOOLEAN
BthTdiParkLocked(
    _In_ PLIST_ENTRY List,
    _Inout_ PIRP Irp)
{
    InsertTailList(List, &Irp->Tail.Overlay.ListEntry);
    IoSetCancelRoutine(Irp, BthTdiCancel);
    if (Irp->Cancel && IoSetCancelRoutine(Irp, NULL))
    {
        RemoveEntryList(&Irp->Tail.Overlay.ListEntry);
        InitializeListHead(&Irp->Tail.Overlay.ListEntry);
        return FALSE;
    }
    return TRUE;
}

static
PIRP
BthTdiTakeLocked(
    _In_ PLIST_ENTRY List)
{
    while (!IsListEmpty(List))
    {
        PIRP Irp = CONTAINING_RECORD(RemoveHeadList(List), IRP, Tail.Overlay.ListEntry);

        InitializeListHead(&Irp->Tail.Overlay.ListEntry);
        if (IoSetCancelRoutine(Irp, NULL))
            return Irp;
    }
    return NULL;
}

static
PIRP
BthTdiTake(
    _In_ PLIST_ENTRY List)
{
    KIRQL OldIrql;
    PIRP Irp;

    KeAcquireSpinLock(&BthTdiLock, &OldIrql);
    Irp = BthTdiTakeLocked(List);
    KeReleaseSpinLock(&BthTdiLock, OldIrql);
    return Irp;
}

static
VOID
BthTdiPark(
    _In_ PLIST_ENTRY List,
    _Inout_ PIRP Irp)
{
    KIRQL OldIrql;
    BOOLEAN Parked;

    KeAcquireSpinLock(&BthTdiLock, &OldIrql);
    Parked = BthTdiParkLocked(List, Irp);
    KeReleaseSpinLock(&BthTdiLock, OldIrql);
    if (!Parked)
    {
        Irp->IoStatus.Status = STATUS_CANCELLED;
        Irp->IoStatus.Information = 0;
        IoCompleteRequest(Irp, IO_NETWORK_INCREMENT);
    }
}

static
VOID
BthTdiComplete(
    _Inout_ PIRP Irp,
    _In_ NTSTATUS Status,
    _In_ ULONG_PTR Information)
{
    Irp->IoStatus.Status = Status;
    Irp->IoStatus.Information = NT_SUCCESS(Status) ? Information : 0;
    IoCompleteRequest(Irp, IO_NETWORK_INCREMENT);
}

static
VOID
BthTdiFailList(
    _In_ PLIST_ENTRY List,
    _In_ NTSTATUS Status)
{
    PIRP Irp;

    while ((Irp = BthTdiTake(List)) != NULL)
        BthTdiComplete(Irp, Status, 0);
}

static
PTA_ADDRESS
BthTdiFindAddress(
    _In_opt_ PTRANSPORT_ADDRESS Address,
    _In_ ULONG Length)
{
    PTA_ADDRESS Entry;

    if (!Address || Length < FIELD_OFFSET(TRANSPORT_ADDRESS, Address[0].Address) || Address->TAAddressCount < 1)
        return NULL;
    Entry = &Address->Address[0];
    if (Entry->AddressType != TDI_ADDRESS_TYPE_BTH || Entry->AddressLength < TDI_ADDRESS_LENGTH_BTH ||
        Length < FIELD_OFFSET(TRANSPORT_ADDRESS, Address[0].Address) + TDI_ADDRESS_LENGTH_BTH)
        return NULL;
    return Entry;
}

static
ULONG
BthTdiFillAddress(
    _Out_writes_bytes_(Length) PTRANSPORT_ADDRESS Address,
    _In_ ULONG Length,
    _In_ BTH_ADDR BtAddress,
    _In_ ULONG Port)
{
    PTDI_ADDRESS_BTH Bth;
    ULONG Required = FIELD_OFFSET(TRANSPORT_ADDRESS, Address[0].Address) + TDI_ADDRESS_LENGTH_BTH;

    if (Length < Required)
        return 0;
    Address->TAAddressCount = 1;
    Address->Address[0].AddressType = TDI_ADDRESS_TYPE_BTH;
    Address->Address[0].AddressLength = TDI_ADDRESS_LENGTH_BTH;
    Bth = (PTDI_ADDRESS_BTH)Address->Address[0].Address;
    RtlZeroMemory(Bth, sizeof(*Bth));
    Bth->btAddr = BtAddress;
    Bth->port = Port;
    return Required;
}

static
VOID
BthTdiFillReturnInfo(
    _In_opt_ PTDI_CONNECTION_INFORMATION Info,
    _In_ PBTHTDI_CONNECTION Connection)
{
    if (Info && Info->RemoteAddress)
        BthTdiFillAddress(Info->RemoteAddress, Info->RemoteAddressLength, Connection->RemoteAddress,
                          Connection->RemotePort);
}

static
VOID
BthTdiFailListens(
    _In_ PBTHTDI_ADDRESS Address,
    _In_ PBTHTDI_CONNECTION Connection)
{
    LIST_ENTRY Failed;
    PLIST_ENTRY Entry;
    KIRQL OldIrql;

    InitializeListHead(&Failed);
    KeAcquireSpinLock(&BthTdiLock, &OldIrql);
    for (Entry = Address->ListenIrps.Flink; Entry != &Address->ListenIrps;)
    {
        PIRP Irp = CONTAINING_RECORD(Entry, IRP, Tail.Overlay.ListEntry);

        Entry = Entry->Flink;
        if (Irp->Tail.Overlay.DriverContext[0] != Connection || !IoSetCancelRoutine(Irp, NULL))
            continue;
        RemoveEntryList(&Irp->Tail.Overlay.ListEntry);
        InsertTailList(&Failed, &Irp->Tail.Overlay.ListEntry);
    }
    KeReleaseSpinLock(&BthTdiLock, OldIrql);
    while (!IsListEmpty(&Failed))
        BthTdiComplete(CONTAINING_RECORD(RemoveHeadList(&Failed), IRP, Tail.Overlay.ListEntry), STATUS_CANCELLED, 0);
}

static
VOID
BthTdiAttachDlc(
    _In_ PBTHMINI_DEVICE Dev,
    _Inout_ PBTHTDI_CONNECTION Connection,
    _In_ PBTHMINI_DLC Dlc)
{
    Dlc->Callbacks = &BthTdiDlcCallbacks;
    Dlc->Context = Connection;
    Connection->Dev = Dev;
    Connection->Dlc = Dlc;
    Connection->RemoteAddress = Dlc->Session->Remote->Address;
    Connection->RemotePort = Dlc->Dlci >> 1;
    Connection->State = BthTdiConnected;
    if (IsListEmpty(&Connection->ActiveLink))
        InsertTailList(&Dev->TdiActive, &Connection->ActiveLink);
    BthTdiReferenceConnection(Connection);
}

static
ULONG
BthTdiRingCopy(
    _Inout_ PBTHTDI_CONNECTION Connection,
    _In_opt_ PMDL Mdl,
    _In_ ULONG Length,
    _In_ BOOLEAN Peek)
{
    ULONG Copied = 0, Head = Connection->RingHead, Available = Connection->RingCount;

    for (; Mdl && Copied < Length && Available; Mdl = Mdl->Next)
    {
        PUCHAR Buffer = MmGetSystemAddressForMdlSafe(Mdl, NormalPagePriority);
        ULONG Room = min(MmGetMdlByteCount(Mdl), Length - Copied), Offset = 0;

        if (!Buffer)
            break;
        while (Offset < Room && Available)
        {
            ULONG Chunk = min(min(Room - Offset, Available), BTHTDI_RING_SIZE - Head);

            RtlCopyMemory(Buffer + Offset, Connection->Ring + Head, Chunk);
            Head = (Head + Chunk) % BTHTDI_RING_SIZE;
            Available -= Chunk;
            Offset += Chunk;
        }
        Copied += Offset;
    }
    if (!Peek)
    {
        Connection->RingHead = Head;
        Connection->RingCount = Available;
    }
    return Copied;
}

static
VOID
BthTdiServeReceives(
    _In_opt_ PBTHMINI_DEVICE Dev,
    _Inout_ PBTHTDI_CONNECTION Connection)
{
    BOOLEAN Consumed = FALSE;
    PIRP Irp;

    while ((Connection->RingCount || Connection->RemoteClosed || Connection->State == BthTdiClosed) &&
           (Irp = BthTdiTake(&Connection->Receives)) != NULL)
    {
        PIO_STACK_LOCATION IoStack = IoGetCurrentIrpStackLocation(Irp);
        PTDI_REQUEST_KERNEL_RECEIVE Receive = (PTDI_REQUEST_KERNEL_RECEIVE)&IoStack->Parameters;
        ULONG Copied = BthTdiRingCopy(Connection, Irp->MdlAddress, Receive->ReceiveLength,
                                      (Receive->ReceiveFlags & TDI_RECEIVE_PEEK) != 0);

        Consumed |= Copied != 0;
        BthTdiComplete(Irp, STATUS_SUCCESS, Copied);
    }
    if (Consumed && Dev && Connection->Dlc)
        BthRfcommGrantCredits(Dev, Connection->Dlc);
}

static
VOID
BthTdiServeSends(
    _Inout_ PBTHTDI_CONNECTION Connection)
{
    PIRP Irp;

    if (Connection->Dlc && BthRfcommTxQueued(Connection->Dlc) > BTHTDI_TX_LIMIT)
        return;
    while ((Irp = BthTdiTake(&Connection->Sends)) != NULL)
    {
        if (Connection->Dlc)
            BthTdiComplete(Irp, STATUS_SUCCESS, Irp->IoStatus.Information);
        else
            BthTdiComplete(Irp, STATUS_CONNECTION_RESET, 0);
    }
}

static
VOID
BthTdiConnectionDown(
    _In_opt_ PBTHMINI_DEVICE Dev,
    _Inout_ PBTHTDI_CONNECTION Connection,
    _In_ NTSTATUS Status)
{
    Connection->Dlc = NULL;
    Connection->RemoteClosed = TRUE;
    if (Connection->State == BthTdiConnected)
        Connection->State = BthTdiClosed;
    if (!IsListEmpty(&Connection->ActiveLink))
    {
        RemoveEntryList(&Connection->ActiveLink);
        InitializeListHead(&Connection->ActiveLink);
    }
    BthTdiFailList(&Connection->ConnectIrps, Status);
    BthTdiServeReceives(Dev, Connection);
    BthTdiServeSends(Connection);
}

static
VOID
BthTdiCompleteListen(
    _Inout_ PIRP Irp,
    _In_ PBTHTDI_CONNECTION Connection)
{
    PIO_STACK_LOCATION IoStack = IoGetCurrentIrpStackLocation(Irp);
    PTDI_REQUEST_KERNEL_LISTEN Listen = (PTDI_REQUEST_KERNEL_LISTEN)&IoStack->Parameters;

    BthTdiFillReturnInfo(Listen->ReturnConnectionInformation, Connection);
    BthTdiComplete(Irp, STATUS_SUCCESS, 0);
}

static
NTSTATUS
BthTdiConnectStatus(
    _In_ NTSTATUS Status)
{
    switch (Status)
    {
        case STATUS_IO_TIMEOUT:
        case STATUS_ACCESS_DENIED:
        case STATUS_INSUFFICIENT_RESOURCES:
        case STATUS_CANCELLED:
        case STATUS_INVALID_ADDRESS:
            return Status;
        case STATUS_CONNECTION_REFUSED:
        case STATUS_CONNECTION_ABORTED:
            return STATUS_HOST_DOWN;
        default:
            return STATUS_NETWORK_UNREACHABLE;
    }
}

static
VOID
BthTdiDlcOpened(
    _In_ PBTHMINI_DEVICE Dev,
    _In_ PBTHMINI_DLC Dlc,
    _In_ NTSTATUS Status)
{
    PBTHTDI_CONNECTION Connection = Dlc->Context;
    PIRP Irp;

    if (!NT_SUCCESS(Status))
    {
        DPRINT1("BTHMINI: RFCOMM socket connect to %012I64x channel %lu failed 0x%08lx\n",
                Connection->RemoteAddress, Connection->RemotePort, Status);
        Connection->Dlc = NULL;
        Connection->State = BthTdiIdle;
        BthTdiFailList(&Connection->ConnectIrps, BthTdiConnectStatus(Status));
        BthTdiDereferenceConnection(Connection);
        return;
    }
    Connection->State = BthTdiConnected;
    Connection->Dev = Dev;
    if (IsListEmpty(&Connection->ActiveLink))
        InsertTailList(&Dev->TdiActive, &Connection->ActiveLink);
    Irp = Connection->AcceptedListen;
    Connection->AcceptedListen = NULL;
    if (Connection->Cleanup)
    {
        if (Irp)
            BthTdiComplete(Irp, STATUS_CANCELLED, 0);
        BthRfcommDisconnect(Dev, Dlc);
        return;
    }
    if (Irp)
        BthTdiCompleteListen(Irp, Connection);
    while ((Irp = BthTdiTake(&Connection->ConnectIrps)) != NULL)
    {
        PIO_STACK_LOCATION IoStack = IoGetCurrentIrpStackLocation(Irp);
        PTDI_REQUEST_KERNEL_CONNECT Connect = (PTDI_REQUEST_KERNEL_CONNECT)&IoStack->Parameters;

        BthTdiFillReturnInfo(Connect->ReturnConnectionInformation, Connection);
        BthTdiComplete(Irp, STATUS_SUCCESS, 0);
    }
}

static
VOID
BthTdiDlcReceive(
    _In_ PBTHMINI_DEVICE Dev,
    _In_ PBTHMINI_DLC Dlc,
    _In_reads_bytes_(Length) const UCHAR *Data,
    _In_ ULONG Length)
{
    PBTHTDI_CONNECTION Connection = Dlc->Context;
    ULONG Offset = 0;

    if (Connection->Cleanup)
        return;
    if (Length > BTHTDI_RING_SIZE - Connection->RingCount)
    {
        DPRINT1("BTHMINI: RFCOMM socket receive ring full, %lu bytes dropped\n",
                Length - (BTHTDI_RING_SIZE - Connection->RingCount));
        Length = BTHTDI_RING_SIZE - Connection->RingCount;
    }
    while (Offset < Length)
    {
        ULONG Tail = (Connection->RingHead + Connection->RingCount) % BTHTDI_RING_SIZE;
        ULONG Chunk = min(Length - Offset, BTHTDI_RING_SIZE - Tail);

        RtlCopyMemory(Connection->Ring + Tail, Data + Offset, Chunk);
        Connection->RingCount += Chunk;
        Offset += Chunk;
    }
    BthTdiServeReceives(Dev, Connection);
}

static
VOID
BthTdiDlcClosed(
    _In_ PBTHMINI_DEVICE Dev,
    _In_ PBTHMINI_DLC Dlc,
    _In_ NTSTATUS Status)
{
    PBTHTDI_CONNECTION Connection = Dlc->Context;

    BOOLEAN Unlinked = FALSE;
    KIRQL OldIrql;

    BthTdiConnectionDown(Dev, Connection, Status);
    KeAcquireSpinLock(&BthTdiLock, &OldIrql);
    if (Connection->Internal && Connection->Address && !IsListEmpty(&Connection->Link))
    {
        RemoveEntryList(&Connection->Link);
        InitializeListHead(&Connection->Link);
        Connection->Address->PendingCount--;
        Unlinked = TRUE;
    }
    KeReleaseSpinLock(&BthTdiLock, OldIrql);
    if (Unlinked)
        BthTdiDereferenceConnection(Connection);
    BthTdiDereferenceConnection(Connection);
}

static
ULONG
BthTdiDlcRoom(
    _In_ PBTHMINI_DLC Dlc)
{
    PBTHTDI_CONNECTION Connection = Dlc->Context;

    return BTHTDI_RING_SIZE - Connection->RingCount;
}

static
BOOLEAN
BthTdiAccept(
    _In_ PBTHMINI_DEVICE Dev,
    _In_opt_ PVOID Context,
    _In_ PBTHMINI_DLC Dlc)
{
    PBTHTDI_ADDRESS Address = Context;
    PBTHTDI_CONNECTION Connection;
    BOOLEAN Full;
    KIRQL OldIrql;
    PIRP Irp;

    if (Address->Closing || !Address->Listening)
        return FALSE;
    Irp = BthTdiTake(&Address->ListenIrps);
    if (Irp)
    {
        Connection = Irp->Tail.Overlay.DriverContext[0];
        BthTdiAttachDlc(Dev, Connection, Dlc);
        Connection->AcceptedListen = Irp;
        return TRUE;
    }
    KeAcquireSpinLock(&BthTdiLock, &OldIrql);
    Full = Address->PendingCount >= BTHTDI_BACKLOG;
    KeReleaseSpinLock(&BthTdiLock, OldIrql);
    if (Full)
        return FALSE;
    Connection = BthTdiAllocateConnection();
    if (!Connection)
        return FALSE;
    Connection->Internal = TRUE;
    BthTdiReferenceAddress(Address);
    Connection->Address = Address;
    BthTdiAttachDlc(Dev, Connection, Dlc);
    KeAcquireSpinLock(&BthTdiLock, &OldIrql);
    InsertTailList(&Address->Pending, &Connection->Link);
    Address->PendingCount++;
    KeReleaseSpinLock(&BthTdiLock, OldIrql);
    return TRUE;
}

static
VOID
BthTdiAdopt(
    _In_ PBTHMINI_DEVICE Dev,
    _Inout_ PBTHTDI_CONNECTION Connection,
    _Inout_ PBTHTDI_CONNECTION Pending)
{
    PUCHAR Ring = Connection->Ring;

    Connection->Ring = Pending->Ring;
    Connection->RingHead = Pending->RingHead;
    Connection->RingCount = Pending->RingCount;
    Pending->Ring = Ring;
    Pending->RingCount = 0;
    Connection->RemoteAddress = Pending->RemoteAddress;
    Connection->RemotePort = Pending->RemotePort;
    Connection->RemoteClosed = Pending->RemoteClosed;
    Connection->State = Pending->State;
    if (Pending->Dlc)
    {
        Connection->Dlc = Pending->Dlc;
        Connection->Dlc->Context = Connection;
        Pending->Dlc = NULL;
        Connection->Dev = Dev;
        BthTdiReferenceConnection(Connection);
        BthTdiDereferenceConnection(Pending);
        if (IsListEmpty(&Connection->ActiveLink))
            InsertTailList(&Dev->TdiActive, &Connection->ActiveLink);
        BthRfcommGrantCredits(Dev, Connection->Dlc);
    }
    if (!IsListEmpty(&Pending->ActiveLink))
    {
        RemoveEntryList(&Pending->ActiveLink);
        InitializeListHead(&Pending->ActiveLink);
    }
}

static
NTSTATUS
BthTdiRegister(
    _In_ PBTHMINI_DEVICE Dev,
    _Inout_ PBTHTDI_ADDRESS Address,
    _In_ LONG Slot)
{
    NTSTATUS Status;

    if (Slot < 0)
        return STATUS_DEVICE_NOT_CONNECTED;
    if (Address->Servers[Slot])
        return STATUS_SUCCESS;
    if (Address->LocalRadio && Address->LocalRadio != Dev->Address)
        return STATUS_INVALID_ADDRESS;
    Status = BthRfcommRegisterServer(Dev, Address->Channel, BthTdiAccept, Address, &Address->Servers[Slot]);
    if (NT_SUCCESS(Status))
    {
        Address->Channel = Address->Servers[Slot]->Channel;
        DPRINT1("BTHMINI: RFCOMM socket bound to channel %u on radio %012I64x\n", Address->Channel, Dev->Address);
    }
    return Status;
}

static
VOID
BthTdiUnregister(
    _In_opt_ PBTHMINI_DEVICE Dev,
    _Inout_ PBTHTDI_ADDRESS Address,
    _In_ LONG Slot)
{
    if (Slot < 0)
        return;
    if (Dev && Address->Servers[Slot])
        BthRfcommUnregisterServer(Dev, Address->Servers[Slot]);
    Address->Servers[Slot] = NULL;
}

static
BOOLEAN
BthTdiRegistered(
    _In_ PBTHTDI_ADDRESS Address)
{
    ULONG Index;

    for (Index = 0; Index < BTHTDI_MAX_RADIOS; Index++)
    {
        if (Address->Servers[Index])
            return TRUE;
    }
    return FALSE;
}

static
NTSTATUS
BthTdiRunSync(
    _In_opt_ PBTHMINI_DEVICE Dev,
    _In_ PBTHTDI_SYNC Sync)
{
    switch (Sync->Operation)
    {
        case BthTdiSyncBind:
        {
            PBTHTDI_ADDRESS Address = Sync->Object;
            KIRQL OldIrql;
            NTSTATUS Status;
            LONG Slot;

            if (!Dev)
                return STATUS_DEVICE_NOT_CONNECTED;
            KeAcquireSpinLock(&BthTdiLock, &OldIrql);
            Slot = BthTdiRadioIndexLocked(Dev);
            KeReleaseSpinLock(&BthTdiLock, OldIrql);
            Status = BthTdiRegister(Dev, Address, Slot);
            if (NT_SUCCESS(Status))
            {
                KeAcquireSpinLock(&BthTdiLock, &OldIrql);
                if (IsListEmpty(&Address->Link))
                    InsertTailList(&BthTdiAddresses, &Address->Link);
                KeReleaseSpinLock(&BthTdiLock, OldIrql);
            }
            return Status;
        }

        case BthTdiSyncCleanupAddress:
        {
            PBTHTDI_ADDRESS Address = Sync->Object;
            LIST_ENTRY Owned;
            PLIST_ENTRY Entry;
            KIRQL OldIrql;
            LONG Slot;

            if (!Dev)
                return STATUS_SUCCESS;
            InitializeListHead(&Owned);
            KeAcquireSpinLock(&BthTdiLock, &OldIrql);
            Slot = BthTdiRadioIndexLocked(Dev);
            for (Entry = Address->Pending.Flink; Entry != &Address->Pending;)
            {
                PBTHTDI_CONNECTION Pending = CONTAINING_RECORD(Entry, BTHTDI_CONNECTION, Link);

                Entry = Entry->Flink;
                if (Pending->Dev != Dev)
                    continue;
                RemoveEntryList(&Pending->Link);
                InsertTailList(&Owned, &Pending->Link);
                Address->PendingCount--;
            }
            KeReleaseSpinLock(&BthTdiLock, OldIrql);
            BthTdiUnregister(Dev, Address, Slot);
            while (!IsListEmpty(&Owned))
            {
                PBTHTDI_CONNECTION Pending = CONTAINING_RECORD(RemoveHeadList(&Owned), BTHTDI_CONNECTION, Link);

                InitializeListHead(&Pending->Link);
                Pending->Cleanup = TRUE;
                if (Pending->Dlc)
                    BthRfcommDisconnect(Dev, Pending->Dlc);
                BthTdiDereferenceConnection(Pending);
            }
            return STATUS_SUCCESS;
        }

        case BthTdiSyncCleanupConnection:
        {
            PBTHTDI_CONNECTION Connection = Sync->Object;

            Connection->Cleanup = TRUE;
            if (Connection->Address)
                BthTdiFailListens(Connection->Address, Connection);
            BthTdiFailList(&Connection->ConnectIrps, STATUS_CANCELLED);
            BthTdiFailList(&Connection->Receives, STATUS_CANCELLED);
            BthTdiFailList(&Connection->Sends, STATUS_CANCELLED);
            if (Dev && Connection->Dlc && Connection->Dev == Dev)
                BthRfcommDisconnect(Dev, Connection->Dlc);
            if (Connection->State == BthTdiConnected)
                Connection->State = BthTdiClosed;
            return STATUS_SUCCESS;
        }
    }
    return STATUS_NOT_SUPPORTED;
}

static
NTSTATUS
BthTdiSyncOn(
    _In_opt_ PBTHMINI_DEVICE Dev,
    _In_ BTHTDI_SYNC_OPERATION Operation,
    _In_ PVOID Object)
{
    LARGE_INTEGER Timeout;
    BTHTDI_SYNC Sync;
    KIRQL OldIrql;

    RtlZeroMemory(&Sync, sizeof(Sync));
    Sync.Operation = Operation;
    Sync.Object = Object;
    KeInitializeEvent(&Sync.Done, NotificationEvent, FALSE);
    KeAcquireSpinLock(&BthTdiLock, &OldIrql);
    if (BthTdiRadioIndexLocked(Dev) < 0)
        Dev = NULL;
    if (Dev && Dev->Thread == KeGetCurrentThread())
    {
        KeReleaseSpinLock(&BthTdiLock, OldIrql);
        return BthTdiRunSync(Dev, &Sync);
    }
    if (Dev)
        InsertTailList(&Dev->TdiSyncs, &Sync.Link);
    KeReleaseSpinLock(&BthTdiLock, OldIrql);
    if (!Dev)
        return BthTdiRunSync(NULL, &Sync);
    KeSetEvent(&Dev->WorkEvent, IO_NO_INCREMENT, FALSE);
    Timeout.QuadPart = -(LONGLONG)BTHTDI_SYNC_TIMEOUT_MS * 10000;
    if (KeWaitForSingleObject(&Sync.Done, Executive, KernelMode, FALSE, &Timeout) == STATUS_TIMEOUT)
    {
        KeAcquireSpinLock(&BthTdiLock, &OldIrql);
        if (!IsListEmpty(&Sync.Link) && Sync.Link.Flink)
        {
            RemoveEntryList(&Sync.Link);
            KeReleaseSpinLock(&BthTdiLock, OldIrql);
            return STATUS_IO_TIMEOUT;
        }
        KeReleaseSpinLock(&BthTdiLock, OldIrql);
        KeWaitForSingleObject(&Sync.Done, Executive, KernelMode, FALSE, NULL);
    }
    return Sync.Status;
}

static
NTSTATUS
BthTdiBind(
    _Inout_ PBTHTDI_ADDRESS Address)
{
    PBTHMINI_DEVICE Radios[BTHTDI_MAX_RADIOS];
    NTSTATUS Status = STATUS_DEVICE_NOT_CONNECTED, Result;
    ULONG Count, Index;

    Count = BthTdiSnapshotRadios(Radios);
    for (Index = 0; Index < Count; Index++)
    {
        Result = BthTdiSyncOn(Radios[Index], BthTdiSyncBind, Address);
        if (NT_SUCCESS(Result))
            Status = STATUS_SUCCESS;
        else if (!NT_SUCCESS(Status) && Result != STATUS_INVALID_ADDRESS)
            Status = Result;
        else if (Result != STATUS_INVALID_ADDRESS)
            DPRINT1("BTHMINI: RFCOMM socket channel %u not bound on radio %012I64x 0x%08lx\n",
                    Address->Channel, Radios[Index]->Address, Result);
    }
    return Status;
}

static
VOID
BthTdiCleanupAddress(
    _Inout_ PBTHTDI_ADDRESS Address)
{
    PBTHMINI_DEVICE Radios[BTHTDI_MAX_RADIOS];
    LIST_ENTRY Left;
    ULONG Count, Index;
    KIRQL OldIrql;

    Address->Closing = TRUE;
    Count = BthTdiSnapshotRadios(Radios);
    for (Index = 0; Index < Count; Index++)
        BthTdiSyncOn(Radios[Index], BthTdiSyncCleanupAddress, Address);

    InitializeListHead(&Left);
    KeAcquireSpinLock(&BthTdiLock, &OldIrql);
    if (!IsListEmpty(&Address->Link))
    {
        RemoveEntryList(&Address->Link);
        InitializeListHead(&Address->Link);
    }
    while (!IsListEmpty(&Address->Pending))
        InsertTailList(&Left, RemoveHeadList(&Address->Pending));
    Address->PendingCount = 0;
    KeReleaseSpinLock(&BthTdiLock, OldIrql);
    BthTdiFailList(&Address->ListenIrps, STATUS_CANCELLED);
    while (!IsListEmpty(&Left))
    {
        PBTHTDI_CONNECTION Pending = CONTAINING_RECORD(RemoveHeadList(&Left), BTHTDI_CONNECTION, Link);

        InitializeListHead(&Pending->Link);
        Pending->Cleanup = TRUE;
        BthTdiDereferenceConnection(Pending);
    }
}

static
VOID
BthTdiCleanupConnection(
    _Inout_ PBTHTDI_CONNECTION Connection)
{
    PBTHMINI_DEVICE Radios[BTHTDI_MAX_RADIOS];
    ULONG Count, Index;

    if (Connection->Dev)
    {
        BthTdiSyncOn(Connection->Dev, BthTdiSyncCleanupConnection, Connection);
        return;
    }
    Count = BthTdiSnapshotRadios(Radios);
    if (!Count)
        BthTdiSyncOn(NULL, BthTdiSyncCleanupConnection, Connection);
    for (Index = 0; Index < Count; Index++)
        BthTdiSyncOn(Radios[Index], BthTdiSyncCleanupConnection, Connection);
}

static
VOID
BthTdiProcessConnect(
    _In_ PBTHMINI_DEVICE Dev,
    _Inout_ PBTHTDI_CONNECTION Connection,
    _Inout_ PIRP Irp)
{
    PIO_STACK_LOCATION IoStack = IoGetCurrentIrpStackLocation(Irp);
    PTDI_REQUEST_KERNEL_CONNECT Connect = (PTDI_REQUEST_KERNEL_CONNECT)&IoStack->Parameters;
    PTDI_CONNECTION_INFORMATION Info = Connect->RequestConnectionInformation;
    PTDI_ADDRESS_BTH Bth;
    PBTHMINI_REMOTE Remote;
    PTA_ADDRESS Address;
    NTSTATUS Status;
    UCHAR Uuid[16];

    if (Connection->State != BthTdiIdle || Connection->Cleanup)
    {
        BthTdiComplete(Irp, STATUS_CONNECTION_ACTIVE, 0);
        return;
    }
    Address = Info ? BthTdiFindAddress(Info->RemoteAddress, Info->RemoteAddressLength) : NULL;
    if (!Address)
    {
        BthTdiComplete(Irp, STATUS_INVALID_ADDRESS, 0);
        return;
    }
    Bth = (PTDI_ADDRESS_BTH)Address->Address;
    if (!Bth->btAddr || Bth->btAddr == Dev->Address ||
        ((Bth->port == 0 || Bth->port == BT_PORT_ANY) && IsEqualGUID(&Bth->serviceClassId, &BthTdiNullGuid)) ||
        (Bth->port > 30 && Bth->port != BT_PORT_ANY))
    {
        BthTdiComplete(Irp, STATUS_INVALID_ADDRESS, 0);
        return;
    }
    Remote = BthRadioGetRemote(Dev, Bth->btAddr);
    if (!Remote)
    {
        BthTdiComplete(Irp, STATUS_INSUFFICIENT_RESOURCES, 0);
        return;
    }
    Connection->Dev = Dev;
    Connection->RemoteAddress = Bth->btAddr;
    Connection->RemotePort = (Bth->port && Bth->port != BT_PORT_ANY) ? Bth->port : 0;
    Connection->ServiceClass = Bth->serviceClassId;
    Connection->State = BthTdiConnecting;
    BthTdiReferenceConnection(Connection);
    if (Connection->RemotePort)
    {
        Status = BthRfcommConnect(Dev, Remote, (UCHAR)Connection->RemotePort, &BthTdiDlcCallbacks, Connection,
                                  &Connection->Dlc);
    }
    else
    {
        Uuid[0] = (UCHAR)(Bth->serviceClassId.Data1 >> 24);
        Uuid[1] = (UCHAR)(Bth->serviceClassId.Data1 >> 16);
        Uuid[2] = (UCHAR)(Bth->serviceClassId.Data1 >> 8);
        Uuid[3] = (UCHAR)Bth->serviceClassId.Data1;
        Uuid[4] = (UCHAR)(Bth->serviceClassId.Data2 >> 8);
        Uuid[5] = (UCHAR)Bth->serviceClassId.Data2;
        Uuid[6] = (UCHAR)(Bth->serviceClassId.Data3 >> 8);
        Uuid[7] = (UCHAR)Bth->serviceClassId.Data3;
        RtlCopyMemory(Uuid + 8, Bth->serviceClassId.Data4, 8);
        Status = BthSdpQueryRfcommChannelUuid(Dev, Remote, Uuid, BthTdiSdpDone, Connection);
    }
    if (!NT_SUCCESS(Status))
    {
        Connection->Dlc = NULL;
        Connection->State = BthTdiIdle;
        BthTdiDereferenceConnection(Connection);
        BthTdiComplete(Irp, BthTdiConnectStatus(Status), 0);
        return;
    }
    BthTdiPark(&Connection->ConnectIrps, Irp);
}

static
VOID
BthTdiSdpDone(
    _In_ PBTHMINI_DEVICE Dev,
    _In_ PBTHMINI_REMOTE Remote,
    _In_opt_ PVOID Context,
    _In_ NTSTATUS Status,
    _In_ UCHAR Channel)
{
    PBTHTDI_CONNECTION Connection = Context;

    if (Connection->Cleanup || Connection->State != BthTdiConnecting)
    {
        BthTdiDereferenceConnection(Connection);
        return;
    }
    if (NT_SUCCESS(Status) && Channel)
    {
        Connection->RemotePort = Channel;
        Status = BthRfcommConnect(Dev, Remote, Channel, &BthTdiDlcCallbacks, Connection, &Connection->Dlc);
        if (NT_SUCCESS(Status))
            return;
    }
    else if (Status == STATUS_NOT_FOUND || NT_SUCCESS(Status))
    {
        Status = STATUS_INVALID_ADDRESS;
    }
    DPRINT1("BTHMINI: RFCOMM socket service lookup on %012I64x failed 0x%08lx\n", Connection->RemoteAddress, Status);
    Connection->Dlc = NULL;
    Connection->State = BthTdiIdle;
    BthTdiFailList(&Connection->ConnectIrps, BthTdiConnectStatus(Status));
    BthTdiDereferenceConnection(Connection);
}

static
VOID
BthTdiProcessListen(
    _In_ PBTHMINI_DEVICE Dev,
    _Inout_ PBTHTDI_CONNECTION Connection,
    _Inout_ PIRP Irp)
{
    PBTHTDI_ADDRESS Address = Connection->Address;
    PBTHTDI_CONNECTION Pending = NULL;
    PBTHMINI_DEVICE Owner;
    NTSTATUS Status;
    KIRQL OldIrql;
    LONG Slot;

    if (!Address || Connection->State != BthTdiIdle || Connection->Cleanup)
    {
        BthTdiComplete(Irp, Address ? STATUS_CONNECTION_ACTIVE : STATUS_INVALID_ADDRESS, 0);
        return;
    }
    KeAcquireSpinLock(&BthTdiLock, &OldIrql);
    Slot = BthTdiRadioIndexLocked(Dev);
    KeReleaseSpinLock(&BthTdiLock, OldIrql);
    if (!BthTdiRegistered(Address) || (Slot >= 0 && !Address->Servers[Slot]))
    {
        Status = BthTdiRegister(Dev, Address, Slot);
        if (!NT_SUCCESS(Status) && !BthTdiRegistered(Address))
        {
            BthTdiComplete(Irp, Status, 0);
            return;
        }
        KeAcquireSpinLock(&BthTdiLock, &OldIrql);
        if (IsListEmpty(&Address->Link))
            InsertTailList(&BthTdiAddresses, &Address->Link);
        KeReleaseSpinLock(&BthTdiLock, OldIrql);
    }
    Address->Listening = TRUE;
    KeAcquireSpinLock(&BthTdiLock, &OldIrql);
    if (!IsListEmpty(&Address->Pending))
    {
        Pending = CONTAINING_RECORD(Address->Pending.Flink, BTHTDI_CONNECTION, Link);
        Owner = Pending->Dev;
        if (Owner && Owner != Dev && BthTdiRadioIndexLocked(Owner) >= 0)
        {
            BOOLEAN Parked = BthTdiParkLocked(&Owner->TdiIrps, Irp);

            KeReleaseSpinLock(&BthTdiLock, OldIrql);
            if (Parked)
                KeSetEvent(&Owner->WorkEvent, IO_NO_INCREMENT, FALSE);
            else
                BthTdiComplete(Irp, STATUS_CANCELLED, 0);
            return;
        }
        RemoveEntryList(&Pending->Link);
        InitializeListHead(&Pending->Link);
        Address->PendingCount--;
    }
    KeReleaseSpinLock(&BthTdiLock, OldIrql);
    if (Pending)
    {
        BthTdiAdopt(Dev, Connection, Pending);
        BthTdiDereferenceConnection(Pending);
        BthTdiCompleteListen(Irp, Connection);
        return;
    }
    Irp->Tail.Overlay.DriverContext[0] = Connection;
    BthTdiPark(&Address->ListenIrps, Irp);
}

static
VOID
BthTdiProcessSend(
    _In_ PBTHMINI_DEVICE Dev,
    _Inout_ PBTHTDI_CONNECTION Connection,
    _Inout_ PIRP Irp)
{
    PIO_STACK_LOCATION IoStack = IoGetCurrentIrpStackLocation(Irp);
    PTDI_REQUEST_KERNEL_SEND Send = (PTDI_REQUEST_KERNEL_SEND)&IoStack->Parameters;
    ULONG Sent = 0;
    PMDL Mdl;

    if (!Connection->Dlc || Connection->State != BthTdiConnected)
    {
        BthTdiComplete(Irp, Connection->RemoteClosed ? STATUS_REMOTE_DISCONNECT : STATUS_INVALID_CONNECTION, 0);
        return;
    }
    for (Mdl = Irp->MdlAddress; Mdl && Sent < Send->SendLength; Mdl = Mdl->Next)
    {
        PUCHAR Buffer = MmGetSystemAddressForMdlSafe(Mdl, NormalPagePriority);
        ULONG Chunk = min(MmGetMdlByteCount(Mdl), Send->SendLength - Sent);
        NTSTATUS Status;

        if (!Buffer)
        {
            BthTdiComplete(Irp, STATUS_INSUFFICIENT_RESOURCES, 0);
            return;
        }
        Status = BthRfcommSend(Dev, Connection->Dlc, Buffer, Chunk);
        if (!NT_SUCCESS(Status))
        {
            BthTdiComplete(Irp, Status, 0);
            return;
        }
        Sent += Chunk;
    }
    if (BthRfcommTxQueued(Connection->Dlc) <= BTHTDI_TX_LIMIT)
    {
        BthTdiComplete(Irp, STATUS_SUCCESS, Sent);
        return;
    }
    Irp->IoStatus.Information = Sent;
    BthTdiPark(&Connection->Sends, Irp);
}

static
VOID
BthTdiProcessIrp(
    _In_ PBTHMINI_DEVICE Dev,
    _Inout_ PIRP Irp)
{
    PIO_STACK_LOCATION IoStack = IoGetCurrentIrpStackLocation(Irp);
    PBTHTDI_CONNECTION Connection = IoStack->FileObject->FsContext;

    switch (IoStack->MinorFunction)
    {
        case TDI_CONNECT:
            BthTdiProcessConnect(Dev, Connection, Irp);
            break;

        case TDI_LISTEN:
            BthTdiProcessListen(Dev, Connection, Irp);
            break;

        case TDI_SEND:
            BthTdiProcessSend(Dev, Connection, Irp);
            break;

        case TDI_RECEIVE:
            if (Connection->State == BthTdiIdle || Connection->State == BthTdiConnecting)
            {
                BthTdiComplete(Irp, STATUS_INVALID_CONNECTION, 0);
                break;
            }
            BthTdiPark(&Connection->Receives, Irp);
            BthTdiServeReceives(Dev, Connection);
            break;

        case TDI_DISCONNECT:
            if (Connection->Dlc)
                BthRfcommDisconnect(Dev, Connection->Dlc);
            if (Connection->State == BthTdiConnected)
                Connection->State = BthTdiClosed;
            BthTdiServeReceives(Dev, Connection);
            BthTdiComplete(Irp, STATUS_SUCCESS, 0);
            break;

        default:
            BthTdiComplete(Irp, STATUS_NOT_IMPLEMENTED, 0);
            break;
    }
}

VOID
BthTdiProcess(
    _In_ PBTHMINI_DEVICE Dev)
{
    PLIST_ENTRY Entry;
    KIRQL OldIrql;
    PIRP Irp;

    for (;;)
    {
        PBTHTDI_SYNC Sync = NULL;

        KeAcquireSpinLock(&BthTdiLock, &OldIrql);
        if (!IsListEmpty(&Dev->TdiSyncs))
        {
            Sync = CONTAINING_RECORD(RemoveHeadList(&Dev->TdiSyncs), BTHTDI_SYNC, Link);
            InitializeListHead(&Sync->Link);
        }
        KeReleaseSpinLock(&BthTdiLock, OldIrql);
        if (!Sync)
            break;
        Sync->Status = BthTdiRunSync(Dev, Sync);
        KeSetEvent(&Sync->Done, IO_NO_INCREMENT, FALSE);
    }
    while ((Irp = BthTdiTake(&Dev->TdiIrps)) != NULL)
        BthTdiProcessIrp(Dev, Irp);
    for (Entry = Dev->TdiActive.Flink; Entry != &Dev->TdiActive;)
    {
        PBTHTDI_CONNECTION Connection = CONTAINING_RECORD(Entry, BTHTDI_CONNECTION, ActiveLink);

        Entry = Entry->Flink;
        BthTdiServeSends(Connection);
    }
}

VOID
BthTdiRadioReady(
    _In_ PBTHMINI_DEVICE Dev)
{
    PLIST_ENTRY Entry;
    KIRQL OldIrql;
    LONG Slot = -1;
    ULONG Index;

    KeAcquireSpinLock(&BthTdiLock, &OldIrql);
    if (BthTdiRadioIndexLocked(Dev) >= 0)
    {
        KeReleaseSpinLock(&BthTdiLock, OldIrql);
        return;
    }
    InitializeListHead(&Dev->TdiIrps);
    InitializeListHead(&Dev->TdiSyncs);
    InitializeListHead(&Dev->TdiActive);
    for (Index = 0; Index < BTHTDI_MAX_RADIOS && Slot < 0; Index++)
    {
        if (!BthTdiRadios[Index])
            Slot = (LONG)Index;
    }
    if (Slot < 0)
    {
        KeReleaseSpinLock(&BthTdiLock, OldIrql);
        DPRINT1("BTHMINI: radio %012I64x has no RFCOMM socket slot\n", Dev->Address);
        return;
    }
    BthTdiRadios[Slot] = Dev;
    if (!BthTdiRadio)
        BthTdiRadio = Dev;
    for (Entry = BthTdiAddresses.Flink; Entry != &BthTdiAddresses; Entry = Entry->Flink)
    {
        PBTHTDI_ADDRESS Address = CONTAINING_RECORD(Entry, BTHTDI_ADDRESS, Link);
        NTSTATUS Status;

        if (!Address->Channel || Address->Closing)
            continue;
        Status = BthTdiRegister(Dev, Address, Slot);
        if (!NT_SUCCESS(Status) && Status != STATUS_INVALID_ADDRESS)
            DPRINT1("BTHMINI: RFCOMM socket channel %u not restored\n", Address->Channel);
    }
    KeReleaseSpinLock(&BthTdiLock, OldIrql);
}

VOID
BthTdiRadioStopping(
    _In_ PBTHMINI_DEVICE Dev)
{
    PLIST_ENTRY Entry;
    KIRQL OldIrql;
    LONG Slot;
    ULONG Index;
    PIRP Irp;

    KeAcquireSpinLock(&BthTdiLock, &OldIrql);
    Slot = BthTdiRadioIndexLocked(Dev);
    if (Slot < 0)
    {
        KeReleaseSpinLock(&BthTdiLock, OldIrql);
        return;
    }
    for (Entry = BthTdiAddresses.Flink; Entry != &BthTdiAddresses; Entry = Entry->Flink)
        BthTdiUnregister(Dev, CONTAINING_RECORD(Entry, BTHTDI_ADDRESS, Link), Slot);
    BthTdiRadios[Slot] = NULL;
    if (BthTdiRadio == Dev)
    {
        BthTdiRadio = NULL;
        for (Index = 0; Index < BTHTDI_MAX_RADIOS && !BthTdiRadio; Index++)
            BthTdiRadio = BthTdiRadios[Index];
    }
    for (Index = 0; Index < BTHTDI_MAX_KEYED; Index++)
    {
        if (BthTdiKeyed[Index].Dev == Dev)
            BthTdiKeyed[Index].Dev = NULL;
    }
    KeReleaseSpinLock(&BthTdiLock, OldIrql);

    for (;;)
    {
        PBTHTDI_SYNC Sync = NULL;

        KeAcquireSpinLock(&BthTdiLock, &OldIrql);
        if (!IsListEmpty(&Dev->TdiSyncs))
        {
            Sync = CONTAINING_RECORD(RemoveHeadList(&Dev->TdiSyncs), BTHTDI_SYNC, Link);
            InitializeListHead(&Sync->Link);
        }
        KeReleaseSpinLock(&BthTdiLock, OldIrql);
        if (!Sync)
            break;
        Sync->Status = BthTdiRunSync(NULL, Sync);
        KeSetEvent(&Sync->Done, IO_NO_INCREMENT, FALSE);
    }
    while ((Irp = BthTdiTake(&Dev->TdiIrps)) != NULL)
        BthTdiComplete(Irp, STATUS_DEVICE_NOT_CONNECTED, 0);
    while (!IsListEmpty(&Dev->TdiActive))
    {
        PBTHTDI_CONNECTION Connection = CONTAINING_RECORD(RemoveHeadList(&Dev->TdiActive), BTHTDI_CONNECTION, ActiveLink);

        InitializeListHead(&Connection->ActiveLink);
        BthTdiConnectionDown(NULL, Connection, STATUS_DEVICE_NOT_CONNECTED);
    }
}

static
NTSTATUS
BthTdiCreate(
    _Inout_ PIRP Irp)
{
    PIO_STACK_LOCATION IoStack = IoGetCurrentIrpStackLocation(Irp);
    PFILE_FULL_EA_INFORMATION Ea = Irp->AssociatedIrp.SystemBuffer;
    PFILE_OBJECT FileObject = IoStack->FileObject;
    NTSTATUS Status = STATUS_SUCCESS;

    FileObject->FsContext = NULL;
    FileObject->FsContext2 = (PVOID)(ULONG_PTR)BthTdiFileControl;
    if (!Ea)
        return STATUS_SUCCESS;
    if (Ea->EaNameLength == TDI_TRANSPORT_ADDRESS_LENGTH &&
        RtlEqualMemory(Ea->EaName, TdiTransportAddress, TDI_TRANSPORT_ADDRESS_LENGTH))
    {
        PTA_ADDRESS Entry = BthTdiFindAddress((PTRANSPORT_ADDRESS)(Ea->EaName + Ea->EaNameLength + 1),
                                              Ea->EaValueLength);
        PTDI_ADDRESS_BTH Bth;
        PBTHTDI_ADDRESS Address;

        if (!Entry)
            return STATUS_INVALID_ADDRESS;
        Bth = (PTDI_ADDRESS_BTH)Entry->Address;
        if (Bth->port != 0 && Bth->port != BT_PORT_ANY && Bth->port > 30)
            return STATUS_INVALID_ADDRESS;
        Address = ExAllocatePoolZero(NonPagedPool, sizeof(*Address), BTHTDI_TAG);
        if (!Address)
            return STATUS_INSUFFICIENT_RESOURCES;
        Address->RefCount = 1;
        Address->Port = Bth->port;
        Address->LocalRadio = Bth->btAddr;
        Address->Channel = (Bth->port && Bth->port != BT_PORT_ANY) ? (UCHAR)Bth->port : 0;
        InitializeListHead(&Address->Link);
        InitializeListHead(&Address->ListenIrps);
        InitializeListHead(&Address->Pending);
        if (Bth->port)
        {
            Status = BthTdiBind(Address);
            if (!NT_SUCCESS(Status))
            {
                ExFreePoolWithTag(Address, BTHTDI_TAG);
                return Status;
            }
        }
        FileObject->FsContext = Address;
        FileObject->FsContext2 = (PVOID)(ULONG_PTR)BthTdiFileAddress;
    }
    else if (Ea->EaNameLength == TDI_CONNECTION_CONTEXT_LENGTH &&
             RtlEqualMemory(Ea->EaName, TdiConnectionContext, TDI_CONNECTION_CONTEXT_LENGTH))
    {
        PBTHTDI_CONNECTION Connection = BthTdiAllocateConnection();

        if (!Connection)
            return STATUS_INSUFFICIENT_RESOURCES;
        FileObject->FsContext = Connection;
        FileObject->FsContext2 = (PVOID)(ULONG_PTR)BthTdiFileConnection;
    }
    return Status;
}

static
NTSTATUS
BthTdiAssociate(
    _Inout_ PBTHTDI_CONNECTION Connection,
    _In_ PIRP Irp)
{
    PIO_STACK_LOCATION IoStack = IoGetCurrentIrpStackLocation(Irp);
    PTDI_REQUEST_KERNEL_ASSOCIATE Associate = (PTDI_REQUEST_KERNEL_ASSOCIATE)&IoStack->Parameters;
    PFILE_OBJECT AddressFile;
    NTSTATUS Status;

    if (Connection->Address)
        return STATUS_INVALID_PARAMETER;
    Status = ObReferenceObjectByHandle(Associate->AddressHandle, 0, *IoFileObjectType, KernelMode,
                                       (PVOID *)&AddressFile, NULL);
    if (!NT_SUCCESS(Status))
        return Status;
    if (AddressFile->DeviceObject != WdfDeviceWdmGetDeviceObject(BthTdiDevice) ||
        (ULONG_PTR)AddressFile->FsContext2 != BthTdiFileAddress || !AddressFile->FsContext)
    {
        ObDereferenceObject(AddressFile);
        return STATUS_INVALID_PARAMETER;
    }
    BthTdiReferenceAddress(AddressFile->FsContext);
    Connection->Address = AddressFile->FsContext;
    ObDereferenceObject(AddressFile);
    return STATUS_SUCCESS;
}

static
NTSTATUS
BthTdiQueryInformation(
    _In_ PFILE_OBJECT FileObject,
    _In_ PIRP Irp,
    _Out_ PULONG_PTR Information)
{
    PIO_STACK_LOCATION IoStack = IoGetCurrentIrpStackLocation(Irp);
    PTDI_REQUEST_KERNEL_QUERY_INFORMATION Query = (PTDI_REQUEST_KERNEL_QUERY_INFORMATION)&IoStack->Parameters;
    PBTHMINI_DEVICE Radio = BthTdiRadio;

    if ((ULONG_PTR)FileObject->FsContext2 == BthTdiFileConnection && FileObject->FsContext &&
        ((PBTHTDI_CONNECTION)FileObject->FsContext)->Dev)
    {
        Radio = ((PBTHTDI_CONNECTION)FileObject->FsContext)->Dev;
    }
    PVOID Buffer;
    ULONG Length;

    *Information = 0;
    if (!Irp->MdlAddress)
        return STATUS_INVALID_PARAMETER;
    Buffer = MmGetSystemAddressForMdlSafe(Irp->MdlAddress, NormalPagePriority);
    if (!Buffer)
        return STATUS_INSUFFICIENT_RESOURCES;
    Length = MmGetMdlByteCount(Irp->MdlAddress);

    switch (Query->QueryType)
    {
        case TDI_QUERY_MAX_DATAGRAM_INFO:
            if (Length < sizeof(TDI_MAX_DATAGRAM_INFO))
                return STATUS_BUFFER_TOO_SMALL;
            ((PTDI_MAX_DATAGRAM_INFO)Buffer)->MaxDatagramSize = BTHTDI_RING_SIZE;
            *Information = sizeof(TDI_MAX_DATAGRAM_INFO);
            return STATUS_SUCCESS;

        case TDI_QUERY_ADDRESS_INFO:
        {
            PTDI_ADDRESS_INFO Info = Buffer;
            PBTHTDI_ADDRESS Address = NULL;
            ULONG Used;

            if ((ULONG_PTR)FileObject->FsContext2 == BthTdiFileAddress)
                Address = FileObject->FsContext;
            else if ((ULONG_PTR)FileObject->FsContext2 == BthTdiFileConnection && FileObject->FsContext)
                Address = ((PBTHTDI_CONNECTION)FileObject->FsContext)->Address;
            if (Length < FIELD_OFFSET(TDI_ADDRESS_INFO, Address))
                return STATUS_BUFFER_TOO_SMALL;
            Info->ActivityCount = 1;
            Used = BthTdiFillAddress(&Info->Address, Length - FIELD_OFFSET(TDI_ADDRESS_INFO, Address),
                                     Radio ? Radio->Address : 0, Address ? Address->Channel : 0);
            if (!Used)
                return STATUS_BUFFER_TOO_SMALL;
            *Information = FIELD_OFFSET(TDI_ADDRESS_INFO, Address) + Used;
            return STATUS_SUCCESS;
        }

        case TDI_QUERY_CONNECTION_INFO:
            if (Length < sizeof(TDI_CONNECTION_INFO))
                return STATUS_BUFFER_TOO_SMALL;
            RtlZeroMemory(Buffer, sizeof(TDI_CONNECTION_INFO));
            *Information = sizeof(TDI_CONNECTION_INFO);
            return STATUS_SUCCESS;

        default:
            return STATUS_NOT_IMPLEMENTED;
    }
}

static
PBTHMINI_DEVICE
BthTdiRouteLocked(
    _In_ PBTHTDI_CONNECTION Connection,
    _In_ PIRP Irp)
{
    PIO_STACK_LOCATION IoStack = IoGetCurrentIrpStackLocation(Irp);
    PBTHTDI_ADDRESS Address = Connection->Address;
    PBTHMINI_DEVICE Dev;

    if (BthTdiRadioIndexLocked(Connection->Dev) >= 0)
        return Connection->Dev;
    if (IoStack->MinorFunction == TDI_CONNECT)
    {
        PTDI_REQUEST_KERNEL_CONNECT Connect = (PTDI_REQUEST_KERNEL_CONNECT)&IoStack->Parameters;
        PTDI_CONNECTION_INFORMATION Info = Connect->RequestConnectionInformation;
        PTA_ADDRESS Remote = Info ? BthTdiFindAddress(Info->RemoteAddress, Info->RemoteAddressLength) : NULL;

        if (Address && Address->LocalRadio)
        {
            Dev = BthTdiRadioByAddressLocked(Address->LocalRadio);
            if (Dev)
                return Dev;
        }
        if (Remote)
        {
            Dev = BthTdiRadioForRemoteLocked(((PTDI_ADDRESS_BTH)Remote->Address)->btAddr);
            if (Dev)
                return Dev;
        }
    }
    if (IoStack->MinorFunction == TDI_LISTEN && Address && !IsListEmpty(&Address->Pending))
    {
        PBTHTDI_CONNECTION Pending = CONTAINING_RECORD(Address->Pending.Flink, BTHTDI_CONNECTION, Link);

        if (BthTdiRadioIndexLocked(Pending->Dev) >= 0)
            return Pending->Dev;
    }
    return BthTdiRadio;
}

static
NTSTATUS
BthTdiInternalControl(
    _Inout_ PIRP Irp)
{
    PIO_STACK_LOCATION IoStack = IoGetCurrentIrpStackLocation(Irp);
    PFILE_OBJECT FileObject = IoStack->FileObject;
    PBTHTDI_CONNECTION Connection = NULL;
    ULONG_PTR Information = 0;
    PBTHMINI_DEVICE Dev;
    NTSTATUS Status;
    KIRQL OldIrql;

    if ((ULONG_PTR)FileObject->FsContext2 == BthTdiFileConnection)
        Connection = FileObject->FsContext;

    switch (IoStack->MinorFunction)
    {
        case TDI_ASSOCIATE_ADDRESS:
            Status = Connection ? BthTdiAssociate(Connection, Irp) : STATUS_INVALID_DEVICE_REQUEST;
            break;

        case TDI_DISASSOCIATE_ADDRESS:
        case TDI_ACCEPT:
        case TDI_SET_INFORMATION:
        case TDI_SET_EVENT_HANDLER:
        case TDI_ACTION:
            Status = STATUS_SUCCESS;
            break;

        case TDI_QUERY_INFORMATION:
            Status = BthTdiQueryInformation(FileObject, Irp, &Information);
            break;

        case TDI_CONNECT:
        case TDI_LISTEN:
        case TDI_SEND:
        case TDI_RECEIVE:
        case TDI_DISCONNECT:
            if (!Connection)
            {
                Status = STATUS_INVALID_DEVICE_REQUEST;
                break;
            }
            if (IoStack->MinorFunction == TDI_LISTEN && Connection->Address && !Connection->Address->Closing &&
                KeGetCurrentIrql() == PASSIVE_LEVEL)
            {
                BthTdiBind(Connection->Address);
            }
            IoMarkIrpPending(Irp);
            KeAcquireSpinLock(&BthTdiLock, &OldIrql);
            Dev = BthTdiRouteLocked(Connection, Irp);
            Status = !Dev ? STATUS_DEVICE_NOT_CONNECTED :
                     BthTdiParkLocked(&Dev->TdiIrps, Irp) ? STATUS_PENDING : STATUS_CANCELLED;
            KeReleaseSpinLock(&BthTdiLock, OldIrql);
            if (Status == STATUS_PENDING)
                KeSetEvent(&Dev->WorkEvent, IO_NO_INCREMENT, FALSE);
            else
                BthTdiComplete(Irp, Status, 0);
            return STATUS_PENDING;

        default:
            Status = STATUS_NOT_IMPLEMENTED;
            break;
    }
    BthTdiComplete(Irp, Status, Information);
    return Status;
}

static
NTSTATUS
NTAPI
BthTdiPreprocess(
    _In_ WDFDEVICE Device,
    _Inout_ PIRP Irp)
{
    PIO_STACK_LOCATION IoStack = IoGetCurrentIrpStackLocation(Irp);
    PFILE_OBJECT FileObject = IoStack->FileObject;
    NTSTATUS Status = STATUS_SUCCESS;

    UNREFERENCED_PARAMETER(Device);
    switch (IoStack->MajorFunction)
    {
        case IRP_MJ_CREATE:
            Status = BthTdiCreate(Irp);
            break;

        case IRP_MJ_CLEANUP:
            if ((ULONG_PTR)FileObject->FsContext2 == BthTdiFileAddress && FileObject->FsContext)
                BthTdiCleanupAddress(FileObject->FsContext);
            else if ((ULONG_PTR)FileObject->FsContext2 == BthTdiFileConnection && FileObject->FsContext)
                BthTdiCleanupConnection(FileObject->FsContext);
            break;

        case IRP_MJ_CLOSE:
            if ((ULONG_PTR)FileObject->FsContext2 == BthTdiFileAddress && FileObject->FsContext)
                BthTdiDereferenceAddress(FileObject->FsContext);
            else if ((ULONG_PTR)FileObject->FsContext2 == BthTdiFileConnection && FileObject->FsContext)
                BthTdiDereferenceConnection(FileObject->FsContext);
            FileObject->FsContext = NULL;
            break;

        case IRP_MJ_INTERNAL_DEVICE_CONTROL:
            return BthTdiInternalControl(Irp);

        default:
            Status = STATUS_INVALID_DEVICE_REQUEST;
            break;
    }
    BthTdiComplete(Irp, Status, 0);
    return Status;
}

NTSTATUS
BthTdiInitialize(
    _In_ WDFDRIVER Driver)
{
    DECLARE_CONST_UNICODE_STRING(Name, BTHTDI_DEVICE_NAME);
    static const UCHAR Majors[] =
    {
        IRP_MJ_CREATE, IRP_MJ_CLEANUP, IRP_MJ_CLOSE, IRP_MJ_INTERNAL_DEVICE_CONTROL, IRP_MJ_DEVICE_CONTROL
    };
    PWDFDEVICE_INIT Init;
    NTSTATUS Status;
    ULONG Index;

    KeInitializeSpinLock(&BthTdiLock);
    InitializeListHead(&BthTdiAddresses);
    Init = WdfControlDeviceInitAllocate(Driver, &SDDL_DEVOBJ_SYS_ALL_ADM_RWX_WORLD_RW_RES_R);
    if (!Init)
        return STATUS_INSUFFICIENT_RESOURCES;
    WdfDeviceInitSetDeviceType(Init, FILE_DEVICE_NETWORK);
    Status = WdfDeviceInitAssignName(Init, &Name);
    for (Index = 0; NT_SUCCESS(Status) && Index < RTL_NUMBER_OF(Majors); Index++)
        Status = WdfDeviceInitAssignWdmIrpPreprocessCallback(Init, BthTdiPreprocess, Majors[Index], NULL, 0);
    if (!NT_SUCCESS(Status))
    {
        WdfDeviceInitFree(Init);
        return Status;
    }
    Status = WdfDeviceCreate(&Init, WDF_NO_OBJECT_ATTRIBUTES, &BthTdiDevice);
    if (!NT_SUCCESS(Status))
    {
        WdfDeviceInitFree(Init);
        return Status;
    }
    WdfControlFinishInitializing(BthTdiDevice);
    return STATUS_SUCCESS;
}

VOID
BthTdiUnload(VOID)
{
    if (BthTdiDevice)
        WdfObjectDelete(BthTdiDevice);
    BthTdiDevice = NULL;
}
