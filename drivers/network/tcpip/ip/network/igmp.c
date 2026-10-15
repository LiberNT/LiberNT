/*
 * PROJECT:     LiberNT TCP/IP protocol driver
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     IPv4 multicast group membership and IGMPv3 host reports
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"

#include <igmp.h>

#define IGMP_TAG 'pmgI'

#define IGMP_TYPE_QUERY              0x11
#define IGMP_TYPE_V3_REPORT          0x22
#define IGMP_RECORD_MODE_IS_EXCLUDE  2
#define IGMP_RECORD_TO_INCLUDE       3
#define IGMP_RECORD_TO_EXCLUDE       4
#define IGMP_ROBUSTNESS              2
#define IGMP_MAX_RECORDS             32

#pragma pack(push, 1)
typedef struct _IGMP_V3_RECORD
{
    UCHAR Type;
    UCHAR AuxLength;
    USHORT Sources;
    IPv4_RAW_ADDRESS Group;
} IGMP_V3_RECORD;

typedef struct _IGMP_V3_REPORT
{
    UCHAR Type;
    UCHAR Reserved;
    USHORT Checksum;
    USHORT Reserved2;
    USHORT Records;
} IGMP_V3_REPORT;

typedef struct _IGMP_QUERY
{
    UCHAR Type;
    UCHAR MaxResponse;
    USHORT Checksum;
    IPv4_RAW_ADDRESS Group;
} IGMP_QUERY;
#pragma pack(pop)

typedef struct _IGMP_GROUP
{
    LIST_ENTRY ListEntry;
    ULONG InterfaceIndex;
    IPv4_RAW_ADDRESS Group;
    ULONG References;
    UCHAR PendingType;
    UCHAR PendingReports;
    BOOLEAN QueryPending;
} IGMP_GROUP, *PIGMP_GROUP;

typedef struct _IGMP_MEMBERSHIP
{
    LIST_ENTRY ListEntry;
    PVOID Owner;
    ULONG InterfaceIndex;
    IPv4_RAW_ADDRESS Group;
} IGMP_MEMBERSHIP, *PIGMP_MEMBERSHIP;

typedef struct _IGMP_SEND
{
    ULONG InterfaceIndex;
    ULONG Count;
    IGMP_V3_RECORD Records[IGMP_MAX_RECORDS];
} IGMP_SEND, *PIGMP_SEND;

static LIST_ENTRY IGMPGroups;
static LIST_ENTRY IGMPMemberships;
static KSPIN_LOCK IGMPLock;

static VOID IGMPMulticastMac(IPv4_RAW_ADDRESS Group, PUCHAR Mac)
{
    ULONG Address = DN2H(Group);

    Mac[0] = 0x01;
    Mac[1] = 0x00;
    Mac[2] = 0x5e;
    Mac[3] = (UCHAR)((Address >> 16) & 0x7f);
    Mac[4] = (UCHAR)(Address >> 8);
    Mac[5] = (UCHAR)Address;
}

static PIGMP_GROUP IGMPFindGroup(ULONG InterfaceIndex, IPv4_RAW_ADDRESS Group)
{
    PLIST_ENTRY Entry;

    for (Entry = IGMPGroups.Flink; Entry != &IGMPGroups; Entry = Entry->Flink)
    {
        PIGMP_GROUP Current = CONTAINING_RECORD(Entry, IGMP_GROUP, ListEntry);

        if (Current->InterfaceIndex == InterfaceIndex && Current->Group == Group)
            return Current;
    }
    return NULL;
}

static VOID IGMPTransmitReport(PIGMP_SEND Send)
{
    IP_PACKET Packet;
    IP_ADDRESS Destination;
    PNEIGHBOR_CACHE_ENTRY NCE;
    PIP_INTERFACE Interface;
    PIPv4_HEADER Header;
    IGMP_V3_REPORT *Report;
    UINT DataSize = sizeof(IGMP_V3_REPORT) + Send->Count * sizeof(IGMP_V3_RECORD);
    UINT HeaderSize = sizeof(IPv4_HEADER) + 4;
    PUCHAR Options;
    UINT Contiguous;

    if (!Send->Count)
        return;

    Interface = GetMulticastInterface(DH2N(Send->InterfaceIndex));
    if (!Interface)
        return;

    AddrInitIPv4(&Destination, DH2N(0xe0000016));
    NCE = RouteGetMulticastRoute(&Destination, DH2N(Send->InterfaceIndex));
    if (!NCE)
    {
        IPDereferenceInterface(Interface);
        return;
    }

    IPInitializePacket(&Packet, IP_ADDRESS_V4);
    Packet.TotalSize = HeaderSize + DataSize;
    if (!NT_SUCCESS(AllocatePacketWithBuffer(&Packet.NdisPacket, NULL, Packet.TotalSize)))
    {
        NBDereferenceNeighbor(NCE);
        IPDereferenceInterface(Interface);
        return;
    }

    GetDataPtr(Packet.NdisPacket, 0, (PCHAR *)&Packet.Header, &Contiguous);
    Packet.MappedHeader = TRUE;
    Packet.HeaderSize = HeaderSize;
    Packet.Data = (PCHAR)Packet.Header + HeaderSize;
    Packet.Flags |= IP_PACKET_FLAG_RAW;
    Packet.DstAddr = Destination;
    Packet.SrcAddr = Interface->Unicast;

    Header = Packet.Header;
    Header->VerIHL = 0x40 | (HeaderSize / 4);
    Header->Tos = 0xc0;
    Header->TotalLength = WH2N((USHORT)Packet.TotalSize);
    Header->Id = (USHORT)Random();
    Header->FlagsFragOfs = 0;
    Header->Ttl = 1;
    Header->Protocol = IPPROTO_IGMP;
    Header->Checksum = 0;
    Header->SrcAddr = Interface->Unicast.Address.IPv4Address;
    Header->DstAddr = Destination.Address.IPv4Address;
    Options = (PUCHAR)(Header + 1);
    Options[0] = 0x94;
    Options[1] = 0x04;
    Options[2] = 0;
    Options[3] = 0;

    Report = Packet.Data;
    Report->Type = IGMP_TYPE_V3_REPORT;
    Report->Reserved = 0;
    Report->Checksum = 0;
    Report->Reserved2 = 0;
    Report->Records = WH2N((USHORT)Send->Count);
    RtlCopyMemory(Report + 1, Send->Records, Send->Count * sizeof(IGMP_V3_RECORD));
    Report->Checksum = (USHORT)IPv4Checksum(Report, DataSize, 0);

    IPSendDatagram(&Packet, NCE);
    NBDereferenceNeighbor(NCE);
    IPDereferenceInterface(Interface);
}

static VOID IGMPReportWorker(PVOID Context)
{
    IGMPTransmitReport(Context);
    ExFreePoolWithTag(Context, IGMP_TAG);
}

static VOID IGMPSendReport(PIGMP_SEND Send)
{
    PIGMP_SEND Copy;

    if (!Send->Count)
        return;
    if (KeGetCurrentIrql() == PASSIVE_LEVEL)
    {
        IGMPTransmitReport(Send);
        return;
    }
    Copy = ExAllocatePoolWithTag(NonPagedPool, sizeof(*Copy), IGMP_TAG);
    if (!Copy)
        return;
    RtlCopyMemory(Copy, Send, sizeof(*Copy));
    if (!ChewCreate(IGMPReportWorker, Copy))
        ExFreePoolWithTag(Copy, IGMP_TAG);
}

static VOID IGMPAddRecord(PIGMP_SEND Send, UCHAR Type, IPv4_RAW_ADDRESS Group)
{
    if (Send->Count >= IGMP_MAX_RECORDS)
        return;
    Send->Records[Send->Count].Type = Type;
    Send->Records[Send->Count].AuxLength = 0;
    Send->Records[Send->Count].Sources = 0;
    Send->Records[Send->Count].Group = Group;
    Send->Count++;
}

typedef struct _IGMP_FILTER_WORK
{
    ULONG InterfaceIndex;
    IPv4_RAW_ADDRESS Group;
    BOOLEAN Add;
} IGMP_FILTER_WORK, *PIGMP_FILTER_WORK;

static VOID IGMPFilterWorker(PVOID Context)
{
    PIGMP_FILTER_WORK Work = Context;
    PIP_INTERFACE Interface = GetMulticastInterface(DH2N(Work->InterfaceIndex));
    UCHAR Mac[6];

    if (Interface)
    {
        if (Interface->Context && Interface->AddressLength == sizeof(Mac))
        {
            IGMPMulticastMac(Work->Group, Mac);
            LANUpdateMulticastList(Interface->Context, Mac, Work->Add);
        }
        IPDereferenceInterface(Interface);
    }
    ExFreePoolWithTag(Work, IGMP_TAG);
}

static VOID IGMPUpdateFilter(ULONG InterfaceIndex, IPv4_RAW_ADDRESS Group, BOOLEAN Add)
{
    PIGMP_FILTER_WORK Work = ExAllocatePoolWithTag(NonPagedPool, sizeof(*Work), IGMP_TAG);

    if (!Work)
        return;
    Work->InterfaceIndex = InterfaceIndex;
    Work->Group = Group;
    Work->Add = Add;
    if (KeGetCurrentIrql() == PASSIVE_LEVEL)
        IGMPFilterWorker(Work);
    else if (!ChewCreate(IGMPFilterWorker, Work))
        ExFreePoolWithTag(Work, IGMP_TAG);
}

static NTSTATUS IGMPReferenceGroup(ULONG InterfaceIndex, IPv4_RAW_ADDRESS Group, PIGMP_SEND Send)
{
    PIGMP_GROUP GroupEntry;
    KIRQL OldIrql;
    BOOLEAN First = FALSE;

    TcpipAcquireSpinLock(&IGMPLock, &OldIrql);
    GroupEntry = IGMPFindGroup(InterfaceIndex, Group);
    if (!GroupEntry)
    {
        GroupEntry = ExAllocatePoolWithTag(NonPagedPool, sizeof(*GroupEntry), IGMP_TAG);
        if (!GroupEntry)
        {
            TcpipReleaseSpinLock(&IGMPLock, OldIrql);
            return STATUS_INSUFFICIENT_RESOURCES;
        }
        RtlZeroMemory(GroupEntry, sizeof(*GroupEntry));
        GroupEntry->InterfaceIndex = InterfaceIndex;
        GroupEntry->Group = Group;
        InsertTailList(&IGMPGroups, &GroupEntry->ListEntry);
    }
    if (GroupEntry->References++ == 0)
    {
        First = TRUE;
        GroupEntry->PendingType = IGMP_RECORD_TO_EXCLUDE;
        GroupEntry->PendingReports = IGMP_ROBUSTNESS - 1;
        Send->InterfaceIndex = InterfaceIndex;
        IGMPAddRecord(Send, IGMP_RECORD_TO_EXCLUDE, Group);
    }
    TcpipReleaseSpinLock(&IGMPLock, OldIrql);

    if (First)
        IGMPUpdateFilter(InterfaceIndex, Group, TRUE);
    return STATUS_SUCCESS;
}

static VOID IGMPDereferenceGroup(ULONG InterfaceIndex, IPv4_RAW_ADDRESS Group, PIGMP_SEND Send)
{
    PIGMP_GROUP GroupEntry;
    KIRQL OldIrql;
    BOOLEAN Last = FALSE;

    TcpipAcquireSpinLock(&IGMPLock, &OldIrql);
    GroupEntry = IGMPFindGroup(InterfaceIndex, Group);
    if (GroupEntry && GroupEntry->References && --GroupEntry->References == 0)
    {
        Last = TRUE;
        GroupEntry->PendingType = IGMP_RECORD_TO_INCLUDE;
        GroupEntry->PendingReports = IGMP_ROBUSTNESS - 1;
        GroupEntry->QueryPending = FALSE;
        Send->InterfaceIndex = InterfaceIndex;
        IGMPAddRecord(Send, IGMP_RECORD_TO_INCLUDE, Group);
    }
    TcpipReleaseSpinLock(&IGMPLock, OldIrql);

    if (Last)
        IGMPUpdateFilter(InterfaceIndex, Group, FALSE);
}

NTSTATUS IGMPJoinGroup(PVOID Owner, ULONG InterfaceIndex, IPv4_RAW_ADDRESS Group)
{
    PIGMP_MEMBERSHIP Membership;
    PLIST_ENTRY Entry;
    IGMP_SEND Send;
    NTSTATUS Status;
    KIRQL OldIrql;

    if ((DN2H(Group) & 0xf0000000) != 0xe0000000)
        return STATUS_INVALID_PARAMETER;

    Membership = ExAllocatePoolWithTag(NonPagedPool, sizeof(*Membership), IGMP_TAG);
    if (!Membership)
        return STATUS_INSUFFICIENT_RESOURCES;
    Membership->Owner = Owner;
    Membership->InterfaceIndex = InterfaceIndex;
    Membership->Group = Group;

    TcpipAcquireSpinLock(&IGMPLock, &OldIrql);
    for (Entry = IGMPMemberships.Flink; Entry != &IGMPMemberships; Entry = Entry->Flink)
    {
        PIGMP_MEMBERSHIP Current = CONTAINING_RECORD(Entry, IGMP_MEMBERSHIP, ListEntry);

        if (Current->Owner == Owner && Current->InterfaceIndex == InterfaceIndex && Current->Group == Group)
        {
            TcpipReleaseSpinLock(&IGMPLock, OldIrql);
            ExFreePoolWithTag(Membership, IGMP_TAG);
            return STATUS_ADDRESS_ALREADY_EXISTS;
        }
    }
    InsertTailList(&IGMPMemberships, &Membership->ListEntry);
    TcpipReleaseSpinLock(&IGMPLock, OldIrql);

    RtlZeroMemory(&Send, sizeof(Send));
    Status = IGMPReferenceGroup(InterfaceIndex, Group, &Send);
    if (!NT_SUCCESS(Status))
    {
        TcpipAcquireSpinLock(&IGMPLock, &OldIrql);
        RemoveEntryList(&Membership->ListEntry);
        TcpipReleaseSpinLock(&IGMPLock, OldIrql);
        ExFreePoolWithTag(Membership, IGMP_TAG);
        return Status;
    }
    IGMPSendReport(&Send);
    return STATUS_SUCCESS;
}

NTSTATUS IGMPLeaveGroup(PVOID Owner, ULONG InterfaceIndex, IPv4_RAW_ADDRESS Group)
{
    PIGMP_MEMBERSHIP Membership = NULL;
    PLIST_ENTRY Entry;
    IGMP_SEND Send;
    KIRQL OldIrql;

    TcpipAcquireSpinLock(&IGMPLock, &OldIrql);
    for (Entry = IGMPMemberships.Flink; Entry != &IGMPMemberships; Entry = Entry->Flink)
    {
        PIGMP_MEMBERSHIP Current = CONTAINING_RECORD(Entry, IGMP_MEMBERSHIP, ListEntry);

        if (Current->Owner == Owner && Current->InterfaceIndex == InterfaceIndex && Current->Group == Group)
        {
            Membership = Current;
            RemoveEntryList(&Membership->ListEntry);
            break;
        }
    }
    TcpipReleaseSpinLock(&IGMPLock, OldIrql);

    if (!Membership)
        return STATUS_INVALID_ADDRESS;
    ExFreePoolWithTag(Membership, IGMP_TAG);

    RtlZeroMemory(&Send, sizeof(Send));
    IGMPDereferenceGroup(InterfaceIndex, Group, &Send);
    IGMPSendReport(&Send);
    return STATUS_SUCCESS;
}

VOID IGMPLeaveAllGroups(PVOID Owner)
{
    PIGMP_MEMBERSHIP Membership;
    PLIST_ENTRY Entry;
    IGMP_SEND Send;
    KIRQL OldIrql;

    for (;;)
    {
        Membership = NULL;
        TcpipAcquireSpinLock(&IGMPLock, &OldIrql);
        for (Entry = IGMPMemberships.Flink; Entry != &IGMPMemberships; Entry = Entry->Flink)
        {
            PIGMP_MEMBERSHIP Current = CONTAINING_RECORD(Entry, IGMP_MEMBERSHIP, ListEntry);

            if (Current->Owner == Owner)
            {
                Membership = Current;
                RemoveEntryList(&Membership->ListEntry);
                break;
            }
        }
        TcpipReleaseSpinLock(&IGMPLock, OldIrql);
        if (!Membership)
            break;

        RtlZeroMemory(&Send, sizeof(Send));
        IGMPDereferenceGroup(Membership->InterfaceIndex, Membership->Group, &Send);
        IGMPSendReport(&Send);
        ExFreePoolWithTag(Membership, IGMP_TAG);
    }
}

BOOLEAN IGMPIsMember(PVOID Owner, ULONG InterfaceIndex, IPv4_RAW_ADDRESS Group)
{
    PLIST_ENTRY Entry;
    BOOLEAN Member = FALSE;
    KIRQL OldIrql;

    TcpipAcquireSpinLock(&IGMPLock, &OldIrql);
    for (Entry = IGMPMemberships.Flink; Entry != &IGMPMemberships; Entry = Entry->Flink)
    {
        PIGMP_MEMBERSHIP Current = CONTAINING_RECORD(Entry, IGMP_MEMBERSHIP, ListEntry);

        if (Current->Owner == Owner && Current->InterfaceIndex == InterfaceIndex && Current->Group == Group)
        {
            Member = TRUE;
            break;
        }
    }
    TcpipReleaseSpinLock(&IGMPLock, OldIrql);
    return Member;
}

VOID IGMPTimeout(VOID)
{
    IGMP_SEND Send[4];
    PLIST_ENTRY Entry, Next;
    KIRQL OldIrql;
    ULONG i, Used = 0;

    RtlZeroMemory(Send, sizeof(Send));
    TcpipAcquireSpinLock(&IGMPLock, &OldIrql);
    for (Entry = IGMPGroups.Flink; Entry != &IGMPGroups; Entry = Next)
    {
        PIGMP_GROUP Group = CONTAINING_RECORD(Entry, IGMP_GROUP, ListEntry);
        PIGMP_SEND Target = NULL;

        Next = Entry->Flink;
        for (i = 0; i < Used; i++)
        {
            if (Send[i].InterfaceIndex == Group->InterfaceIndex)
                Target = &Send[i];
        }
        if (!Target && Used < ARRAYSIZE(Send) && (Group->PendingReports || Group->QueryPending))
        {
            Target = &Send[Used++];
            Target->InterfaceIndex = Group->InterfaceIndex;
        }

        if (Group->PendingReports && Target)
        {
            IGMPAddRecord(Target, Group->PendingType, Group->Group);
            Group->PendingReports--;
        }
        else if (Group->QueryPending && Group->References && Target)
        {
            IGMPAddRecord(Target, IGMP_RECORD_MODE_IS_EXCLUDE, Group->Group);
        }
        Group->QueryPending = FALSE;

        if (!Group->References && !Group->PendingReports)
        {
            RemoveEntryList(&Group->ListEntry);
            ExFreePoolWithTag(Group, IGMP_TAG);
        }
    }
    TcpipReleaseSpinLock(&IGMPLock, OldIrql);

    for (i = 0; i < Used; i++)
        IGMPSendReport(&Send[i]);
}

static VOID IGMPReceive(PIP_INTERFACE Interface, PIP_PACKET IPPacket)
{
    IGMP_QUERY *Query = IPPacket->Data;
    UINT DataSize = IPPacket->TotalSize - IPPacket->HeaderSize;
    PLIST_ENTRY Entry;
    KIRQL OldIrql;

    if (DataSize < sizeof(IGMP_QUERY) || !IPv4CorrectChecksum(IPPacket->Data, DataSize))
        return;

    RawIpReceive(Interface, IPPacket);

    if (Query->Type != IGMP_TYPE_QUERY)
        return;

    TcpipAcquireSpinLock(&IGMPLock, &OldIrql);
    for (Entry = IGMPGroups.Flink; Entry != &IGMPGroups; Entry = Entry->Flink)
    {
        PIGMP_GROUP Group = CONTAINING_RECORD(Entry, IGMP_GROUP, ListEntry);

        if (Group->InterfaceIndex == Interface->Index && Group->References &&
            (!Query->Group || Query->Group == Group->Group))
        {
            Group->QueryPending = TRUE;
        }
    }
    TcpipReleaseSpinLock(&IGMPLock, OldIrql);
}

NTSTATUS IGMPStartup(VOID)
{
    InitializeListHead(&IGMPGroups);
    InitializeListHead(&IGMPMemberships);
    KeInitializeSpinLock(&IGMPLock);
    IPRegisterProtocol(IPPROTO_IGMP, IGMPReceive);
    return STATUS_SUCCESS;
}

NTSTATUS IGMPShutdown(VOID)
{
    IPRegisterProtocol(IPPROTO_IGMP, NULL);
    return STATUS_SUCCESS;
}
