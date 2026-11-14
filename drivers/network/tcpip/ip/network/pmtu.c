/*
 * PROJECT:     LiberNT TCP/IP protocol driver
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     IPv4 path cache and path MTU discovery (RFC 1191)
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif193@gmail.com>
 */

#include "precomp.h"

#include <icmp.h>
#include <lwip_glue/lwip_glue.h>
#include <tcpip_undoc.h>

#define PMTU_TAG        'uMPT'
#define PMTU_MINIMUM    68
#define PMTU_BUCKETS    64
#define PATH_IDLE_TIMEOUT 90000

typedef struct _PMTU_ENTRY
{
    LIST_ENTRY ListEntry;
    PIP_INTERFACE Interface;
    IPv4_RAW_ADDRESS Destination;
    IPv4_RAW_ADDRESS Source;
    IPv4_RAW_ADDRESS NextHop;
    ULONG Mtu;
    ULONGLONG Changed;
    ULONGLONG LastUsed;
    ULONGLONG LastConfirmed;
    ULONGLONG LinkSpeed;
} PMTU_ENTRY, *PPMTU_ENTRY;

static const ULONG PmtuPlateaus[] = { 65535, 32000, 17914, 8166, 4352, 2002, 1492, 1006, 508, 296, 68 };

static LIST_ENTRY PmtuEntries[PMTU_BUCKETS];
static KSPIN_LOCK PmtuLock;

static ULONG PmtuPlateauBelow(ULONG Length)
{
    ULONG i;

    for (i = 0; i < ARRAYSIZE(PmtuPlateaus); i++)
    {
        if (PmtuPlateaus[i] < Length)
            return PmtuPlateaus[i];
    }
    return PMTU_MINIMUM;
}

static ULONG PmtuPlateauAbove(ULONG Mtu)
{
    ULONG i, Above = PmtuPlateaus[0];

    for (i = 0; i < ARRAYSIZE(PmtuPlateaus); i++)
    {
        if (PmtuPlateaus[i] > Mtu)
            Above = PmtuPlateaus[i];
    }
    return Above;
}

static PLIST_ENTRY PmtuBucket(IPv4_RAW_ADDRESS Destination)
{
    ULONG Hash = Destination ^ (Destination >> 16);

    return &PmtuEntries[(Hash ^ (Hash >> 8)) % PMTU_BUCKETS];
}

static PPMTU_ENTRY PmtuFind(PIP_INTERFACE Interface, IPv4_RAW_ADDRESS Destination)
{
    PLIST_ENTRY Head = PmtuBucket(Destination), Entry;

    for (Entry = Head->Flink; Entry != Head; Entry = Entry->Flink)
    {
        PPMTU_ENTRY Pmtu = CONTAINING_RECORD(Entry, PMTU_ENTRY, ListEntry);

        if (Pmtu->Interface == Interface && Pmtu->Destination == Destination)
            return Pmtu;
    }
    return NULL;
}

static VOID PmtuFreeList(PLIST_ENTRY Free)
{
    while (!IsListEmpty(Free))
        ExFreePoolWithTag(CONTAINING_RECORD(RemoveHeadList(Free), PMTU_ENTRY, ListEntry), PMTU_TAG);
}

static BOOLEAN PathIsLocal(PIP_INTERFACE Interface, IPv4_RAW_ADDRESS Destination)
{
    IP_ADDRESS Address;

    if (Destination == 0xFFFFFFFF || Interface == Loopback)
        return TRUE;
    if ((DN2H(Destination) & 0xF0000000) == 0xE0000000)
        return DN2H(Destination) == 0xE0000001 || IGMPIsJoined(Interface->Index, Destination);
    AddrInitIPv4(&Address, Destination);
    return IsInterfaceAddress(Interface, &Address) || IsInterfaceBroadcast(Interface, &Address);
}

VOID PathTouch(PIP_INTERFACE Interface, IPv4_RAW_ADDRESS Source, IPv4_RAW_ADDRESS Destination,
               IPv4_RAW_ADDRESS NextHop)
{
    ULONGLONG Now = KeQueryInterruptTime();
    PPMTU_ENTRY Pmtu, Fresh = NULL;
    IP_ADDRESS Address;
    KIRQL OldIrql;

    if (Interface == Loopback && (DN2H(Destination) & 0xFF000000) != 0x7F000000)
    {
        PIP_INTERFACE Owner;
        BOOLEAN Unicast = FALSE;

        AddrInitIPv4(&Address, Destination);
        Owner = AddrLocateInterface(&Address);
        if (Owner)
        {
            Unicast = IsInterfaceAddress(Owner, &Address);
            if (Unicast)
            {
                Interface = Owner;
                NextHop = Destination;
            }
            IPDereferenceInterface(Owner);
        }
        if (!Unicast && (DN2H(Source) & 0xFF000000) != 0x7F000000)
            return;
    }

    TcpipAcquireSpinLock(&PmtuLock, &OldIrql);
    Pmtu = PmtuFind(Interface, Destination);
    if (Pmtu)
    {
        Pmtu->Source = Source;
        Pmtu->NextHop = NextHop;
        Pmtu->LastUsed = Now;
        TcpipReleaseSpinLock(&PmtuLock, OldIrql);
        return;
    }
    TcpipReleaseSpinLock(&PmtuLock, OldIrql);

    Fresh = ExAllocatePoolZero(NonPagedPool, sizeof(*Fresh), PMTU_TAG);
    if (!Fresh)
        return;
    Fresh->Interface = Interface;
    Fresh->Destination = Destination;
    Fresh->Source = Source;
    Fresh->NextHop = NextHop;
    Fresh->Mtu = Interface->MTU;
    Fresh->LastUsed = Now;
    Fresh->LinkSpeed = PathIsLocal(Interface, Destination) ? 0 : (ULONGLONG)Interface->Speed * 100;

    TcpipAcquireSpinLock(&PmtuLock, &OldIrql);
    Pmtu = PmtuFind(Interface, Destination);
    if (!Pmtu)
    {
        InsertTailList(PmtuBucket(Destination), &Fresh->ListEntry);
        Fresh = NULL;
    }
    TcpipReleaseSpinLock(&PmtuLock, OldIrql);

    if (Fresh)
        ExFreePoolWithTag(Fresh, PMTU_TAG);
}

VOID PathConfirmV4(IPv4_RAW_ADDRESS Destination)
{
    PathConfirm(Destination);
}

VOID PathConfirm(IPv4_RAW_ADDRESS Destination)
{
    ULONGLONG Now = KeQueryInterruptTime();
    PLIST_ENTRY Head = PmtuBucket(Destination), Entry;
    KIRQL OldIrql;

    TcpipAcquireSpinLock(&PmtuLock, &OldIrql);
    for (Entry = Head->Flink; Entry != Head; Entry = Entry->Flink)
    {
        PPMTU_ENTRY Pmtu = CONTAINING_RECORD(Entry, PMTU_ENTRY, ListEntry);

        if (Pmtu->Destination == Destination)
            Pmtu->LastConfirmed = Now;
    }
    TcpipReleaseSpinLock(&PmtuLock, OldIrql);
}

static VOID PathRemoveIf(PIP_INTERFACE Interface, IPv4_RAW_ADDRESS NextHop, ULONGLONG IdleBefore, BOOLEAN All)
{
    LIST_ENTRY Free;
    KIRQL OldIrql;
    ULONG i;

    InitializeListHead(&Free);
    TcpipAcquireSpinLock(&PmtuLock, &OldIrql);
    for (i = 0; i < PMTU_BUCKETS; i++)
    {
        PLIST_ENTRY Entry, Next;

        for (Entry = PmtuEntries[i].Flink; Entry != &PmtuEntries[i]; Entry = Next)
        {
            PPMTU_ENTRY Pmtu = CONTAINING_RECORD(Entry, PMTU_ENTRY, ListEntry);

            Next = Entry->Flink;
            if (All || (Interface && Pmtu->Interface == Interface && (!NextHop || Pmtu->NextHop == NextHop)) ||
                (IdleBefore && Pmtu->LastUsed < IdleBefore))
            {
                RemoveEntryList(&Pmtu->ListEntry);
                InsertTailList(&Free, &Pmtu->ListEntry);
            }
        }
    }
    TcpipReleaseSpinLock(&PmtuLock, OldIrql);
    PmtuFreeList(&Free);
}

VOID PathUnreachable(PIP_INTERFACE Interface, IPv4_RAW_ADDRESS NextHop)
{
    PathRemoveIf(Interface, NextHop, 0, FALSE);
}

VOID PathTimeout(VOID)
{
    ULONGLONG Now = KeQueryInterruptTime();

    if (Now > (ULONGLONG)PATH_IDLE_TIMEOUT * 10000)
        PathRemoveIf(NULL, 0, Now - (ULONGLONG)PATH_IDLE_TIMEOUT * 10000, FALSE);
}

VOID PathFlush(VOID)
{
    PathRemoveIf(NULL, 0, 0, TRUE);
}

NTSTATUS PathQuery(PIP_PATH_TABLE Table, ULONG Length, PULONG Information)
{
    ULONGLONG Now = KeQueryInterruptTime();
    ULONG Capacity = Length < FIELD_OFFSET(IP_PATH_TABLE, Rows) ? 0 :
                     (Length - FIELD_OFFSET(IP_PATH_TABLE, Rows)) / sizeof(IP_PATH_ROW);
    ULONG Count = 0, i;
    KIRQL OldIrql;

    if (Length < FIELD_OFFSET(IP_PATH_TABLE, Rows))
        return STATUS_BUFFER_TOO_SMALL;

    TcpipAcquireSpinLock(&PmtuLock, &OldIrql);
    for (i = 0; i < PMTU_BUCKETS; i++)
    {
        PLIST_ENTRY Entry;

        for (Entry = PmtuEntries[i].Flink; Entry != &PmtuEntries[i]; Entry = Entry->Flink)
        {
            PPMTU_ENTRY Pmtu = CONTAINING_RECORD(Entry, PMTU_ENTRY, ListEntry);
            PIP_PATH_ROW Row;
            ULONGLONG Elapsed;

            if (Count < Capacity)
            {
                Row = &Table->Rows[Count];
                RtlZeroMemory(Row, sizeof(*Row));
                Row->Index = Pmtu->Interface->Index;
                RtlCopyMemory(Row->Source, &Pmtu->Source, sizeof(ULONG));
                RtlCopyMemory(Row->Destination, &Pmtu->Destination, sizeof(ULONG));
                RtlCopyMemory(Row->NextHop, &Pmtu->NextHop, sizeof(ULONG));
                Row->PathMtu = min(Pmtu->Mtu, Pmtu->Interface->MTU);
                Elapsed = (Now - Pmtu->LastConfirmed) / 10000;
                Row->LastReachable = (ULONG)min(Elapsed - Elapsed % 500, MAXULONG);
                Row->IsReachable = TRUE;
                Row->LinkSpeed = Pmtu->LinkSpeed;
            }
            Count++;
        }
    }
    TcpipReleaseSpinLock(&PmtuLock, OldIrql);

    Table->NumEntries = Count;
    *Information = FIELD_OFFSET(IP_PATH_TABLE, Rows) + min(Count, Capacity) * sizeof(IP_PATH_ROW);
    return Count > Capacity ? STATUS_BUFFER_OVERFLOW : STATUS_SUCCESS;
}

BOOLEAN PmtuLower(PIP_INTERFACE Interface, IPv4_RAW_ADDRESS Destination, ULONG Mtu)
{
    PPMTU_ENTRY Pmtu, Fresh;
    BOOLEAN Lowered = FALSE;
    KIRQL OldIrql;

    Mtu = max(Mtu, PMTU_MINIMUM);
    if (Mtu >= Interface->MTU)
        return FALSE;

    Fresh = ExAllocatePoolZero(NonPagedPool, sizeof(*Fresh), PMTU_TAG);

    TcpipAcquireSpinLock(&PmtuLock, &OldIrql);
    Pmtu = PmtuFind(Interface, Destination);
    if (!Pmtu && Fresh)
    {
        Pmtu = Fresh;
        Fresh = NULL;
        Pmtu->Interface = Interface;
        Pmtu->Destination = Destination;
        Pmtu->Mtu = Interface->MTU;
        Pmtu->LastUsed = KeQueryInterruptTime();
        Pmtu->LinkSpeed = (ULONGLONG)Interface->Speed * 100;
        InsertTailList(PmtuBucket(Destination), &Pmtu->ListEntry);
    }
    if (Pmtu && Mtu < Pmtu->Mtu)
    {
        Pmtu->Mtu = Mtu;
        Pmtu->Changed = KeQueryInterruptTime();
        Lowered = TRUE;
    }
    TcpipReleaseSpinLock(&PmtuLock, OldIrql);

    if (Fresh)
        ExFreePoolWithTag(Fresh, PMTU_TAG);
    return Lowered;
}

ULONG PmtuQuery(PIP_INTERFACE Interface, IPv4_RAW_ADDRESS Destination, PBOOLEAN Raised)
{
    ULONGLONG Now = KeQueryInterruptTime();
    ULONG Mtu = Interface->MTU;
    PPMTU_ENTRY Pmtu;
    KIRQL OldIrql;

    if (Raised)
        *Raised = FALSE;

    TcpipAcquireSpinLock(&PmtuLock, &OldIrql);
    Pmtu = PmtuFind(Interface, Destination);
    if (Pmtu && Pmtu->Mtu < Interface->MTU)
    {
        if (Now - Pmtu->Changed >= (ULONGLONG)Interface->PathMtuDiscoveryTimeout * 10000)
        {
            Pmtu->Mtu = min(PmtuPlateauAbove(Pmtu->Mtu), Interface->MTU);
            Pmtu->Changed = Now;
            if (Raised)
                *Raised = TRUE;
        }
        Mtu = min(Pmtu->Mtu, Interface->MTU);
    }
    TcpipReleaseSpinLock(&PmtuLock, OldIrql);

    return Mtu;
}

ULONG PmtuForDestination(IPv4_RAW_ADDRESS Destination, ULONG Default)
{
    PNEIGHBOR_CACHE_ENTRY NCE;
    IP_ADDRESS Address;
    ULONG Mtu;

    AddrInitIPv4(&Address, Destination);
    NCE = RouteGetRouteToDestination(&Address);
    if (!NCE)
        return Default;
    Mtu = PmtuQuery(NCE->Interface, Destination, NULL);
    NBDereferenceNeighbor(NCE);
    return Mtu;
}

VOID PmtuFlushInterface(PIP_INTERFACE Interface)
{
    PathRemoveIf(Interface, 0, 0, FALSE);
}

VOID PmtuReceive(PIP_INTERFACE Interface, PIP_PACKET IPPacket)
{
    PUCHAR Icmp = IPPacket->Data;
    UINT DataSize = IPPacket->TotalSize - IPPacket->HeaderSize;
    PIPv4_HEADER Quoted;
    PUCHAR Transport;
    IP_ADDRESS Source;
    ULONG QuotedHeader, QuotedLength, Mtu;

    if (DataSize < sizeof(ICMP_HEADER) + sizeof(IPv4_HEADER) + 8)
        return;

    Quoted = (PIPv4_HEADER)(Icmp + sizeof(ICMP_HEADER));
    QuotedHeader = (Quoted->VerIHL & 0x0F) * 4;
    if ((Quoted->VerIHL >> 4) != 4 || QuotedHeader < sizeof(IPv4_HEADER) ||
        DataSize < sizeof(ICMP_HEADER) + QuotedHeader + 8 || Quoted->Protocol != IPPROTO_TCP ||
        !(WN2H(Quoted->FlagsFragOfs) & IPv4_DF_MASK))
        return;

    AddrInitIPv4(&Source, Quoted->SrcAddr);
    if (!IsInterfaceAddress(Interface, &Source))
        return;

    QuotedLength = WN2H(Quoted->TotalLength);
    Mtu = (Icmp[6] << 8) | Icmp[7];
    if (!Mtu)
        Mtu = PmtuPlateauBelow(QuotedLength);
    if (Mtu >= QuotedLength)
        return;

    Transport = (PUCHAR)Quoted + QuotedHeader;
    LibTCPPathMtuExceeded(Interface, Quoted->SrcAddr, Quoted->DstAddr,
                          (USHORT)((Transport[0] << 8) | Transport[1]),
                          (USHORT)((Transport[2] << 8) | Transport[3]),
                          ((ULONG)Transport[4] << 24) | ((ULONG)Transport[5] << 16) |
                          ((ULONG)Transport[6] << 8) | Transport[7],
                          max(Mtu, PMTU_MINIMUM));
}

VOID ICMPSendFragmentationNeeded(PIP_INTERFACE Interface, PIP_PACKET IPPacket, ULONG Mtu)
{
    ICMPSendDestinationUnreachable(Interface, IPPacket, IPPacket->Data, ICMP_CODE_DU_FRAG_DF_SET, Mtu);
}

VOID ICMPSendDestinationUnreachable(PIP_INTERFACE Interface, PIP_PACKET IPPacket, PVOID Transport, UCHAR Code, ULONG Mtu)
{
    UINT QuotedSize = IPPacket->HeaderSize + min(8, IPPacket->TotalSize - IPPacket->HeaderSize);
    UINT DataSize = sizeof(ICMP_HEADER) + QuotedSize;
    PNEIGHBOR_CACHE_ENTRY NCE;
    ADDRESS_FILE FakeAddrFile;
    IP_PACKET NewPacket;
    IP_ADDRESS Source;
    PUCHAR Message;

    NCE = RouteGetRouteToDestination(&IPPacket->SrcAddr);
    if (!NCE)
        return;

    Message = ExAllocatePoolZero(NonPagedPool, DataSize, PMTU_TAG);
    if (!Message)
    {
        NBDereferenceNeighbor(NCE);
        return;
    }
    Message[0] = ICMP_TYPE_DEST_UNREACH;
    Message[1] = Code;
    Message[6] = (UCHAR)(Mtu >> 8);
    Message[7] = (UCHAR)Mtu;
    RtlCopyMemory(Message + sizeof(ICMP_HEADER), IPPacket->Header, IPPacket->HeaderSize);
    RtlCopyMemory(Message + sizeof(ICMP_HEADER) + IPPacket->HeaderSize, Transport,
                  QuotedSize - IPPacket->HeaderSize);
    ((PICMP_HEADER)Message)->Checksum = (USHORT)IPv4Checksum(Message, DataSize, 0);

    GetInterfaceSourceAddress(Interface, &IPPacket->SrcAddr, &NCE->Address, &Source);
    RtlZeroMemory(&FakeAddrFile, sizeof(FakeAddrFile));
    FakeAddrFile.Protocol = IPPROTO_ICMP;
    FakeAddrFile.TTL = 128;
    if (NT_SUCCESS(BuildRawIpPacket(&FakeAddrFile, &NewPacket, &IPPacket->SrcAddr, 0, &Source, 0, (PCHAR)Message, DataSize)))
        IPSendDatagram(&NewPacket, NCE);

    ExFreePoolWithTag(Message, PMTU_TAG);
    NBDereferenceNeighbor(NCE);
}

VOID PmtuStartup(VOID)
{
    ULONG i;

    for (i = 0; i < PMTU_BUCKETS; i++)
        InitializeListHead(&PmtuEntries[i]);
    KeInitializeSpinLock(&PmtuLock);
}
