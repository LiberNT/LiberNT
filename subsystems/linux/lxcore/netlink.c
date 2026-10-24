/*
 * PROJECT:     LiberNT Linux Subsystem
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Linux netlink sockets for routing, generic netlink and uevents
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "lxcore.h"
#include <tdiinfo.h>
#include <tcpioctl.h>

#define NDEBUG
#include <debug.h>

#define LX_NETLINK_ROUTE            0
#define LX_NETLINK_KOBJECT_UEVENT   15
#define LX_NETLINK_GENERIC          16

#define LX_SOL_NETLINK              270
#define LX_SO_ATTACH_FILTER         26
#define LX_SO_DETACH_FILTER         27
#define LX_SO_LOCK_FILTER           44
#define LX_SO_ATTACH_BPF            50

#define LX_NETLINK_ADD_MEMBERSHIP   1
#define LX_NETLINK_DROP_MEMBERSHIP  2
#define LX_NETLINK_PKTINFO          3
#define LX_NETLINK_BROADCAST_ERROR  4
#define LX_NETLINK_NO_ENOBUFS       5
#define LX_NETLINK_LISTEN_ALL_NSID  8
#define LX_NETLINK_LIST_MEMBERSHIPS 9
#define LX_NETLINK_CAP_ACK          10
#define LX_NETLINK_EXT_ACK          11
#define LX_NETLINK_GET_STRICT_CHK   12

#define LX_NLM_F_REQUEST            0x01
#define LX_NLM_F_MULTI              0x02
#define LX_NLM_F_ACK                0x04
#define LX_NLM_F_DUMP               0x300

#define LX_NLMSG_ERROR              2
#define LX_NLMSG_DONE               3
#define LX_NLMSG_MIN_TYPE           0x10

#define LX_RTM_NEWLINK              16
#define LX_RTM_GETLINK              18
#define LX_RTM_NEWADDR              20
#define LX_RTM_GETADDR              22
#define LX_RTM_NEWROUTE             24
#define LX_RTM_GETROUTE             26
#define LX_RTM_MAX                  123

#define LX_GENL_ID_CTRL             0x10
#define LX_CTRL_CMD_NEWFAMILY       1
#define LX_CTRL_CMD_GETFAMILY       3
#define LX_CTRL_ATTR_FAMILY_ID      1
#define LX_CTRL_ATTR_FAMILY_NAME    2
#define LX_CTRL_ATTR_VERSION        3
#define LX_CTRL_ATTR_HDRSIZE        4
#define LX_CTRL_ATTR_MAXATTR        5

#define LX_IFLA_ADDRESS             1
#define LX_IFLA_BROADCAST           2
#define LX_IFLA_IFNAME              3
#define LX_IFLA_MTU                 4
#define LX_IFLA_QDISC               6
#define LX_IFLA_STATS               7
#define LX_IFLA_TXQLEN              13
#define LX_IFLA_OPERSTATE           16
#define LX_IFLA_LINKMODE            17
#define LX_IFLA_GROUP               27
#define LX_IFLA_PROMISCUITY         30
#define LX_IFLA_NUM_TX_QUEUES       31
#define LX_IFLA_NUM_RX_QUEUES       32
#define LX_IFLA_CARRIER             33
#define LX_IFLA_CARRIER_CHANGES     35
#define LX_IFLA_MIN_MTU             50
#define LX_IFLA_MAX_MTU             51

#define LX_IFA_ADDRESS              1
#define LX_IFA_LOCAL                2
#define LX_IFA_LABEL                3
#define LX_IFA_BROADCAST            4
#define LX_IFA_CACHEINFO            6
#define LX_IFA_FLAGS                8
#define LX_IFA_F_PERMANENT          0x80

#define LX_RTA_DST                  1
#define LX_RTA_OIF                  4
#define LX_RTA_GATEWAY              5
#define LX_RTA_PRIORITY             6
#define LX_RTA_PREFSRC              7
#define LX_RTA_TABLE                15

#define LX_IFF_UP                   0x1
#define LX_IFF_BROADCAST            0x2
#define LX_IFF_LOOPBACK             0x8
#define LX_IFF_RUNNING              0x40
#define LX_IFF_MULTICAST            0x1000
#define LX_IFF_LOWER_UP             0x10000

#define LX_ARPHRD_ETHER             1
#define LX_ARPHRD_LOOPBACK          772

#define LX_IF_OPER_UNKNOWN          0
#define LX_IF_OPER_DOWN             2
#define LX_IF_OPER_UP               6

#define LX_RT_SCOPE_UNIVERSE        0
#define LX_RT_SCOPE_LINK            253
#define LX_RT_SCOPE_HOST            254
#define LX_RT_TABLE_MAIN            254
#define LX_RTN_UNICAST              1
#define LX_RTPROT_KERNEL            2
#define LX_RTPROT_BOOT              3

#define LX_NT_IF_TYPE_LOOPBACK      24
#define LX_NT_IF_OPER_OPERATIONAL   5

#define LX_NL_DATAGRAM              3776
#define LX_NL_HEADROOM              1024
#define LX_NL_MAX_LINKS             16
#define LX_NL_MAX_ADDRESSES         32
#define LX_NL_MAX_ROUTES            64
#define LX_NL_MAX_ENTITIES          256

#define LX_NL_ALIGN(Length)         (((Length) + 3) & ~3UL)

typedef struct _LX_NLMSGHDR
{
    ULONG Length;
    USHORT Type;
    USHORT Flags;
    ULONG Seq;
    ULONG Pid;
} LX_NLMSGHDR, *PLX_NLMSGHDR;

typedef struct _LX_SOCKADDR_NL
{
    USHORT Family;
    USHORT Pad;
    ULONG Pid;
    ULONG Groups;
} LX_SOCKADDR_NL, *PLX_SOCKADDR_NL;

typedef struct _LX_IFINFOMSG
{
    UCHAR Family;
    UCHAR Pad;
    USHORT Type;
    LONG Index;
    ULONG Flags;
    ULONG Change;
} LX_IFINFOMSG, *PLX_IFINFOMSG;

typedef struct _LX_IFADDRMSG
{
    UCHAR Family;
    UCHAR PrefixLength;
    UCHAR Flags;
    UCHAR Scope;
    ULONG Index;
} LX_IFADDRMSG, *PLX_IFADDRMSG;

typedef struct _LX_RTMSG
{
    UCHAR Family;
    UCHAR DstLength;
    UCHAR SrcLength;
    UCHAR Tos;
    UCHAR Table;
    UCHAR Protocol;
    UCHAR Scope;
    UCHAR Type;
    ULONG Flags;
} LX_RTMSG, *PLX_RTMSG;

typedef struct _LX_GENLMSGHDR
{
    UCHAR Command;
    UCHAR Version;
    USHORT Reserved;
} LX_GENLMSGHDR, *PLX_GENLMSGHDR;

typedef struct _LX_NL_CACHEINFO
{
    ULONG Preferred;
    ULONG Valid;
    ULONG Created;
    ULONG Updated;
} LX_NL_CACHEINFO, *PLX_NL_CACHEINFO;

typedef struct _LX_NT_ROUTE
{
    ULONG Dest;
    ULONG Index;
    ULONG Metric1;
    ULONG Metric2;
    ULONG Metric3;
    ULONG Metric4;
    ULONG Gateway;
    ULONG Type;
    ULONG Proto;
    ULONG Age;
    ULONG Mask;
    ULONG Metric5;
    ULONG Info;
} LX_NT_ROUTE, *PLX_NT_ROUTE;

typedef struct _LX_NL_LINK
{
    ULONG Index;
    ULONG NtIndex;
    CHAR Name[16];
    BOOLEAN Loopback;
    BOOLEAN Running;
    ULONG Mtu;
    ULONG MacLength;
    UCHAR Mac[8];
    ULONG RxPackets;
    ULONG TxPackets;
    ULONG RxBytes;
    ULONG TxBytes;
} LX_NL_LINK, *PLX_NL_LINK;

typedef struct _LX_NL_ADDRESS
{
    ULONG Index;
    ULONG Address;
    ULONG Mask;
} LX_NL_ADDRESS, *PLX_NL_ADDRESS;

typedef struct _LX_NL_ROUTE
{
    ULONG Index;
    ULONG Dest;
    ULONG Mask;
    ULONG Gateway;
    ULONG Metric;
    ULONG Source;
} LX_NL_ROUTE, *PLX_NL_ROUTE;

typedef struct _LX_NL_STATE
{
    ULONG LinkCount;
    ULONG AddressCount;
    ULONG RouteCount;
    LX_NL_LINK Links[LX_NL_MAX_LINKS];
    LX_NL_ADDRESS Addresses[LX_NL_MAX_ADDRESSES];
    LX_NL_ROUTE Routes[LX_NL_MAX_ROUTES];
} LX_NL_STATE, *PLX_NL_STATE;

typedef struct _LX_NL_MSG
{
    LIST_ENTRY Link;
    ULONG Length;
    UCHAR Data[1];
} LX_NL_MSG, *PLX_NL_MSG;

typedef struct _LX_NL_SOCKET
{
    LIST_ENTRY GlobalLink;
    PLX_INSTANCE Instance;
    ULONG Protocol;
    ULONG Type;
    ULONG PortId;
    ULONG Groups;
    BOOLEAN Bound;
    BOOLEAN PassCred;
    BOOLEAN PktInfo;
    BOOLEAN ExtAck;
    BOOLEAN StrictCheck;
    BOOLEAN NoEnobufs;
    BOOLEAN CapAck;
    BOOLEAN BroadcastError;
    BOOLEAN ListenAllNsid;
    KEVENT Lock;
    LIST_ENTRY Queue;
    ULONG QueueBytes;
    LX_POLL_HEAD Poll;
    LONG64 RecvTimeout;
    LONG RecvBuffer;
    LONG SendBuffer;
    ULONG64 Ino;
} LX_NL_SOCKET, *PLX_NL_SOCKET;

typedef struct _LX_NL_BUILDER
{
    PLX_NL_SOCKET Socket;
    PUCHAR Buffer;
    ULONG Used;
    ULONG Start;
} LX_NL_BUILDER, *PLX_NL_BUILDER;

static LIST_ENTRY LxNlSockets = { &LxNlSockets, &LxNlSockets };
static KSPIN_LOCK LxNlSocketsLock;
static ULONG LxNlRover = 0xFFFFEFFF;
static volatile LONG64 LxNlInode = 0x500000;

static LONG64 LxNlRead(_In_ PLX_FILE File, _In_ ULONG64 Buffer, _In_ ULONG64 Length, _Inout_opt_ PLONG64 Offset);
static LONG64 LxNlWrite(_In_ PLX_FILE File, _In_ ULONG64 Buffer, _In_ ULONG64 Length, _Inout_opt_ PLONG64 Offset);
static ULONG LxNlPoll(_In_ PLX_FILE File, _In_opt_ PLX_POLL_ENTRY Entry);
static LONG LxNlStat(_In_ PLX_FILE File, _Out_ PLX_STAT Stat);
static VOID LxNlRelease(_In_ PLX_FILE File);

static const LX_FILE_OPS LxNlOps =
{
    LxNlRead, LxNlWrite, NULL, LxNlPoll, LxNlStat, NULL, NULL, NULL, LxNlRelease
};

BOOLEAN
LxNetlinkIsFile(
    _In_ PLX_FILE File)
{
    return File->Ops == &LxNlOps;
}

static
NTSTATUS
LxNlTdiQuery(
    _In_ HANDLE Tcp,
    _In_ ULONG Entity,
    _In_ ULONG Instance,
    _In_ ULONG Class,
    _In_ ULONG Id,
    _Out_writes_bytes_(Length) PVOID Buffer,
    _In_ ULONG Length,
    _Out_ PULONG Returned)
{
    TCP_REQUEST_QUERY_INFORMATION_EX Request;
    IO_STATUS_BLOCK Iosb;
    NTSTATUS Status;

    RtlZeroMemory(&Request, sizeof(Request));
    Request.ID.toi_entity.tei_entity = Entity;
    Request.ID.toi_entity.tei_instance = Instance;
    Request.ID.toi_class = Class;
    Request.ID.toi_type = INFO_TYPE_PROVIDER;
    Request.ID.toi_id = Id;
    Status = ZwDeviceIoControlFile(Tcp, NULL, NULL, NULL, &Iosb, IOCTL_TCP_QUERY_INFORMATION_EX,
                                   &Request, sizeof(Request), Buffer, Length);
    *Returned = NT_SUCCESS(Status) ? (ULONG)Iosb.Information : 0;
    return Status;
}

static
PLX_NL_LINK
LxNlLinkByNtIndex(
    _In_ PLX_NL_STATE State,
    _In_ ULONG NtIndex)
{
    ULONG Index;

    for (Index = 0; Index < State->LinkCount; Index++)
    {
        if (State->Links[Index].NtIndex == NtIndex)
            return &State->Links[Index];
    }
    return NULL;
}

static
ULONG
LxNlPrefixLength(
    _In_ ULONG Mask)
{
    ULONG Host = RtlUlongByteSwap(Mask), Length = 0;

    while ((Length < 32) && (Host & (0x80000000UL >> Length)))
        Length++;
    return Length;
}

static
BOOLEAN
LxNlIsLocalAddress(
    _In_ PLX_NL_STATE State,
    _In_ ULONG Address)
{
    ULONG Index;

    for (Index = 0; Index < State->AddressCount; Index++)
    {
        if (State->Addresses[Index].Address == Address)
            return TRUE;
    }
    return FALSE;
}

static
ULONG
LxNlFirstAddress(
    _In_ PLX_NL_STATE State,
    _In_ ULONG LinkIndex)
{
    ULONG Index;

    for (Index = 0; Index < State->AddressCount; Index++)
    {
        if (State->Addresses[Index].Index == LinkIndex)
            return State->Addresses[Index].Address;
    }
    return 0;
}

static
VOID
LxNlSnapshot(
    _Out_ PLX_NL_STATE State)
{
    UNICODE_STRING Name = RTL_CONSTANT_STRING(L"\\Device\\Tcp");
    OBJECT_ATTRIBUTES ObjectAttributes;
    IO_STATUS_BLOCK Iosb;
    TDIEntityID *Entities = NULL;
    PUCHAR Buffer = NULL;
    ULONG EntityCount = 0, Returned, Index, Item, NextIndex = 2, Ether = 0;
    BOOLEAN HaveLoopback = FALSE;
    HANDLE Tcp = NULL;
    NTSTATUS Status;

    RtlZeroMemory(State, sizeof(*State));
    InitializeObjectAttributes(&ObjectAttributes, &Name, OBJ_KERNEL_HANDLE | OBJ_CASE_INSENSITIVE, NULL, NULL);
    Status = ZwCreateFile(&Tcp, GENERIC_READ | SYNCHRONIZE, &ObjectAttributes, &Iosb, NULL, FILE_ATTRIBUTE_NORMAL,
                          FILE_SHARE_READ | FILE_SHARE_WRITE, FILE_OPEN, FILE_SYNCHRONOUS_IO_NONALERT, NULL, 0);
    if (NT_SUCCESS(Status))
    {
        Entities = LxAllocPaged(LX_NL_MAX_ENTITIES * sizeof(TDIEntityID));
        Buffer = LxAllocPaged(LX_IO_CHUNK);
    }
    if ((Entities != NULL) && (Buffer != NULL) &&
        NT_SUCCESS(LxNlTdiQuery(Tcp, GENERIC_ENTITY, 0, INFO_CLASS_GENERIC, ENTITY_LIST_ID, Entities,
                                LX_NL_MAX_ENTITIES * sizeof(TDIEntityID), &Returned)))
    {
        EntityCount = Returned / sizeof(TDIEntityID);
    }

    for (Index = 0; Index < EntityCount; Index++)
    {
        IFEntry *Entry = (IFEntry *)Buffer;
        PLX_NL_LINK Link;

        if ((Entities[Index].tei_entity != IF_ENTITY) || (State->LinkCount >= LX_NL_MAX_LINKS))
            continue;
        if (!NT_SUCCESS(LxNlTdiQuery(Tcp, IF_ENTITY, Entities[Index].tei_instance, INFO_CLASS_PROTOCOL,
                                     IF_MIB_STATS_ID, Buffer, LX_IO_CHUNK, &Returned)) ||
            (Returned < FIELD_OFFSET(IFEntry, if_descr)))
        {
            continue;
        }
        if (LxNlLinkByNtIndex(State, Entry->if_index) != NULL)
            continue;

        Link = &State->Links[State->LinkCount++];
        Link->NtIndex = Entry->if_index;
        Link->Loopback = (Entry->if_type == LX_NT_IF_TYPE_LOOPBACK);
        Link->Running = Link->Loopback || (Entry->if_operstatus == LX_NT_IF_OPER_OPERATIONAL);
        Link->Mtu = Link->Loopback ? 65536 : Entry->if_mtu;
        Link->MacLength = min(Entry->if_physaddrlen, sizeof(Link->Mac));
        RtlCopyMemory(Link->Mac, Entry->if_physaddr, Link->MacLength);
        Link->RxPackets = Entry->if_inucastpkts + Entry->if_innucastpkts;
        Link->TxPackets = Entry->if_outucastpkts + Entry->if_outnucastpkts;
        Link->RxBytes = Entry->if_inoctets;
        Link->TxBytes = Entry->if_outoctets;
    }

    for (Index = 0; Index < State->LinkCount; Index++)
    {
        for (Item = Index + 1; Item < State->LinkCount; Item++)
        {
            if (State->Links[Item].NtIndex < State->Links[Index].NtIndex)
            {
                LX_NL_LINK Swap = State->Links[Index];

                State->Links[Index] = State->Links[Item];
                State->Links[Item] = Swap;
            }
        }
    }

    for (Index = 0; Index < State->LinkCount; Index++)
    {
        PLX_NL_LINK Link = &State->Links[Index];

        if (Link->Loopback && !HaveLoopback)
        {
            Link->Index = 1;
            RtlStringCbCopyA(Link->Name, sizeof(Link->Name), "lo");
            HaveLoopback = TRUE;
        }
        else
        {
            Link->Loopback = FALSE;
            Link->Index = NextIndex++;
            RtlStringCbPrintfA(Link->Name, sizeof(Link->Name), "eth%lu", Ether++);
        }
    }
    {
        if (!HaveLoopback && (State->LinkCount < LX_NL_MAX_LINKS))
        {
            PLX_NL_LINK Link = &State->Links[State->LinkCount++];

            Link->Index = 1;
            Link->NtIndex = MAXULONG;
            Link->Loopback = TRUE;
            Link->Running = TRUE;
            Link->Mtu = 65536;
            RtlStringCbCopyA(Link->Name, sizeof(Link->Name), "lo");
            State->Addresses[State->AddressCount].Index = 1;
            State->Addresses[State->AddressCount].Address = 0x0100007F;
            State->Addresses[State->AddressCount].Mask = 0x000000FF;
            State->AddressCount++;
        }
    }

    for (Index = 0; Index < EntityCount; Index++)
    {
        IPAddrEntry *Address = (IPAddrEntry *)Buffer;
        ULONG Count;

        if (Entities[Index].tei_entity != CL_NL_ENTITY)
            continue;
        if (!NT_SUCCESS(LxNlTdiQuery(Tcp, CL_NL_ENTITY, Entities[Index].tei_instance, INFO_CLASS_PROTOCOL,
                                     IP_MIB_ADDRTABLE_ENTRY_ID, Buffer, LX_IO_CHUNK, &Returned)))
        {
            continue;
        }
        Count = Returned / sizeof(IPAddrEntry);
        for (Item = 0; (Item < Count) && (State->AddressCount < LX_NL_MAX_ADDRESSES); Item++)
        {
            PLX_NL_LINK Link = LxNlLinkByNtIndex(State, Address[Item].iae_index);

            if ((Address[Item].iae_addr == 0) || (Link == NULL) || LxNlIsLocalAddress(State, Address[Item].iae_addr))
                continue;
            State->Addresses[State->AddressCount].Index = Link->Index;
            State->Addresses[State->AddressCount].Address = Address[Item].iae_addr;
            State->Addresses[State->AddressCount].Mask = Address[Item].iae_mask;
            State->AddressCount++;
        }

        if (!NT_SUCCESS(LxNlTdiQuery(Tcp, CL_NL_ENTITY, Entities[Index].tei_instance, INFO_CLASS_PROTOCOL,
                                     IP_MIB_ARPTABLE_ENTRY_ID, Buffer, LX_IO_CHUNK, &Returned)))
        {
            continue;
        }
        Count = Returned / sizeof(LX_NT_ROUTE);
        for (Item = 0; (Item < Count) && (State->RouteCount < LX_NL_MAX_ROUTES); Item++)
        {
            PLX_NT_ROUTE Route = &((PLX_NT_ROUTE)Buffer)[Item];
            PLX_NL_LINK Link = LxNlLinkByNtIndex(State, Route->Index);
            PLX_NL_ROUTE Out;
            ULONG Dest = Route->Dest & Route->Mask;

            if ((Link == NULL) || Link->Loopback)
                continue;
            if (((Dest & 0xFF) == 127) || ((Dest & 0xF0) == 0xE0) || (Dest == 0xFFFFFFFF))
                continue;
            if ((Route->Mask == 0xFFFFFFFF) && LxNlIsLocalAddress(State, Route->Dest))
                continue;
            if ((Route->Mask == 0xFFFFFFFF) && ((Route->Dest & 0xFF000000) == 0xFF000000))
                continue;

            Out = &State->Routes[State->RouteCount++];
            Out->Index = Link->Index;
            Out->Dest = Dest;
            Out->Mask = Route->Mask;
            Out->Gateway = ((Route->Gateway == 0) || LxNlIsLocalAddress(State, Route->Gateway)) ? 0 : Route->Gateway;
            Out->Metric = Route->Metric1;
            Out->Source = LxNlFirstAddress(State, Link->Index);
        }
    }

    LxFreePaged(Entities);
    LxFreePaged(Buffer);
    if (Tcp != NULL)
        ZwClose(Tcp);
}

static
VOID
LxNlQueue(
    _In_ PLX_NL_SOCKET Socket,
    _In_reads_bytes_(Length) PUCHAR Data,
    _In_ ULONG Length)
{
    PLX_NL_MSG Message = LxAlloc(FIELD_OFFSET(LX_NL_MSG, Data) + Length);

    if (Message == NULL)
        return;
    Message->Length = Length;
    RtlCopyMemory(Message->Data, Data, Length);
    KeWaitForSingleObject(&Socket->Lock, Executive, KernelMode, FALSE, NULL);
    InsertTailList(&Socket->Queue, &Message->Link);
    Socket->QueueBytes += Length;
    KeSetEvent(&Socket->Lock, IO_NO_INCREMENT, FALSE);
    LxPollWake(&Socket->Poll, LX_POLLIN | LX_POLLRDNORM);
}

static
VOID
LxNlFlush(
    _Inout_ PLX_NL_BUILDER Builder)
{
    if (Builder->Used != 0)
        LxNlQueue(Builder->Socket, Builder->Buffer, Builder->Used);
    Builder->Used = 0;
}

static
PVOID
LxNlBegin(
    _Inout_ PLX_NL_BUILDER Builder,
    _In_ PLX_NLMSGHDR Request,
    _In_ USHORT Type,
    _In_ USHORT Flags,
    _In_ ULONG PayloadLength)
{
    PLX_NLMSGHDR Header;

    if ((Builder->Used != 0) && (Builder->Used + LX_NL_HEADROOM > LX_NL_DATAGRAM))
        LxNlFlush(Builder);
    Builder->Start = Builder->Used;
    Header = (PLX_NLMSGHDR)(Builder->Buffer + Builder->Used);
    RtlZeroMemory(Header, sizeof(*Header) + LX_NL_ALIGN(PayloadLength));
    Header->Type = Type;
    Header->Flags = Flags;
    Header->Seq = Request->Seq;
    Header->Pid = Builder->Socket->PortId;
    Builder->Used += sizeof(*Header) + LX_NL_ALIGN(PayloadLength);
    return Header + 1;
}

static
VOID
LxNlAttr(
    _Inout_ PLX_NL_BUILDER Builder,
    _In_ USHORT Type,
    _In_reads_bytes_(Length) const VOID *Data,
    _In_ ULONG Length)
{
    PUSHORT Attribute = (PUSHORT)(Builder->Buffer + Builder->Used);

    if (Builder->Used + 4 + LX_NL_ALIGN(Length) > 2 * LX_NL_DATAGRAM)
        return;
    Attribute[0] = (USHORT)(4 + Length);
    Attribute[1] = Type;
    RtlZeroMemory((PUCHAR)(Attribute + 2), LX_NL_ALIGN(Length));
    RtlCopyMemory((PUCHAR)(Attribute + 2), Data, Length);
    Builder->Used += 4 + LX_NL_ALIGN(Length);
}

static
VOID
LxNlAttrU32(
    _Inout_ PLX_NL_BUILDER Builder,
    _In_ USHORT Type,
    _In_ ULONG Value)
{
    LxNlAttr(Builder, Type, &Value, sizeof(Value));
}

static
VOID
LxNlAttrU8(
    _Inout_ PLX_NL_BUILDER Builder,
    _In_ USHORT Type,
    _In_ UCHAR Value)
{
    LxNlAttr(Builder, Type, &Value, sizeof(Value));
}

static
VOID
LxNlAttrString(
    _Inout_ PLX_NL_BUILDER Builder,
    _In_ USHORT Type,
    _In_ PCSTR Value)
{
    LxNlAttr(Builder, Type, Value, (ULONG)strlen(Value) + 1);
}

static
VOID
LxNlEnd(
    _Inout_ PLX_NL_BUILDER Builder)
{
    ((PLX_NLMSGHDR)(Builder->Buffer + Builder->Start))->Length = Builder->Used - Builder->Start;
}

static
VOID
LxNlDone(
    _Inout_ PLX_NL_BUILDER Builder,
    _In_ PLX_NLMSGHDR Request)
{
    PLONG Error = LxNlBegin(Builder, Request, LX_NLMSG_DONE, LX_NLM_F_MULTI, sizeof(LONG));

    *Error = 0;
    LxNlEnd(Builder);
}

static
VOID
LxNlError(
    _Inout_ PLX_NL_BUILDER Builder,
    _In_ PLX_NLMSGHDR Request,
    _In_ LONG Error)
{
    PLONG Payload = LxNlBegin(Builder, Request, LX_NLMSG_ERROR, 0x100, sizeof(LONG) + sizeof(LX_NLMSGHDR));

    Payload[0] = Error;
    RtlCopyMemory(Payload + 1, Request, sizeof(LX_NLMSGHDR));
    LxNlEnd(Builder);
}

static
VOID
LxNlEmitLink(
    _Inout_ PLX_NL_BUILDER Builder,
    _In_ PLX_NLMSGHDR Request,
    _In_ PLX_NL_LINK Link,
    _In_ USHORT Flags)
{
    static const UCHAR Broadcast[6] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
    static const UCHAR Zero[6] = { 0 };
    PLX_IFINFOMSG Info = LxNlBegin(Builder, Request, LX_RTM_NEWLINK, Flags, sizeof(LX_IFINFOMSG));
    ULONG Stats[24];

    Info->Family = LX_AF_UNSPEC;
    Info->Type = Link->Loopback ? LX_ARPHRD_LOOPBACK : LX_ARPHRD_ETHER;
    Info->Index = (LONG)Link->Index;
    Info->Flags = LX_IFF_UP | (Link->Loopback ? LX_IFF_LOOPBACK : (LX_IFF_BROADCAST | LX_IFF_MULTICAST)) |
                  (Link->Running ? (LX_IFF_RUNNING | LX_IFF_LOWER_UP) : 0);

    LxNlAttrString(Builder, LX_IFLA_IFNAME, Link->Name);
    LxNlAttrU32(Builder, LX_IFLA_TXQLEN, Link->Loopback ? 1000 : 1000);
    LxNlAttrU8(Builder, LX_IFLA_OPERSTATE, Link->Loopback ? LX_IF_OPER_UNKNOWN :
               (Link->Running ? LX_IF_OPER_UP : LX_IF_OPER_DOWN));
    LxNlAttrU8(Builder, LX_IFLA_LINKMODE, 0);
    LxNlAttrU32(Builder, LX_IFLA_MTU, Link->Mtu);
    LxNlAttrU32(Builder, LX_IFLA_MIN_MTU, Link->Loopback ? 0 : 68);
    LxNlAttrU32(Builder, LX_IFLA_MAX_MTU, Link->Loopback ? 0 : 65535);
    LxNlAttrU32(Builder, LX_IFLA_GROUP, 0);
    LxNlAttrU32(Builder, LX_IFLA_PROMISCUITY, 0);
    LxNlAttrU32(Builder, LX_IFLA_NUM_TX_QUEUES, 1);
    LxNlAttrU32(Builder, LX_IFLA_NUM_RX_QUEUES, 1);
    LxNlAttrU8(Builder, LX_IFLA_CARRIER, Link->Running ? 1 : 0);
    LxNlAttrString(Builder, LX_IFLA_QDISC, Link->Loopback ? "noqueue" : "fq_codel");
    LxNlAttrU32(Builder, LX_IFLA_CARRIER_CHANGES, 0);
    LxNlAttr(Builder, LX_IFLA_ADDRESS, Link->Loopback ? Zero : Link->Mac, 6);
    LxNlAttr(Builder, LX_IFLA_BROADCAST, Link->Loopback ? Zero : Broadcast, 6);
    RtlZeroMemory(Stats, sizeof(Stats));
    Stats[0] = Link->RxPackets;
    Stats[1] = Link->TxPackets;
    Stats[2] = Link->RxBytes;
    Stats[3] = Link->TxBytes;
    LxNlAttr(Builder, LX_IFLA_STATS, Stats, sizeof(Stats));
    LxNlEnd(Builder);
}

static
VOID
LxNlEmitAddress(
    _Inout_ PLX_NL_BUILDER Builder,
    _In_ PLX_NLMSGHDR Request,
    _In_ PLX_NL_STATE State,
    _In_ PLX_NL_ADDRESS Address)
{
    PLX_IFADDRMSG Info = LxNlBegin(Builder, Request, LX_RTM_NEWADDR, LX_NLM_F_MULTI, sizeof(LX_IFADDRMSG));
    LX_NL_CACHEINFO Cache;
    PCSTR Label = "lo";
    ULONG Index, Broadcast;

    for (Index = 0; Index < State->LinkCount; Index++)
    {
        if (State->Links[Index].Index == Address->Index)
            Label = State->Links[Index].Name;
    }

    Info->Family = LX_AF_INET;
    Info->PrefixLength = (UCHAR)LxNlPrefixLength(Address->Mask);
    Info->Flags = LX_IFA_F_PERMANENT;
    Info->Scope = ((Address->Address & 0xFF) == 127) ? LX_RT_SCOPE_HOST : LX_RT_SCOPE_UNIVERSE;
    Info->Index = Address->Index;

    LxNlAttr(Builder, LX_IFA_ADDRESS, &Address->Address, sizeof(ULONG));
    LxNlAttr(Builder, LX_IFA_LOCAL, &Address->Address, sizeof(ULONG));
    if (((Address->Address & 0xFF) != 127) && (Address->Mask != 0xFFFFFFFF))
    {
        Broadcast = Address->Address | ~Address->Mask;
        LxNlAttr(Builder, LX_IFA_BROADCAST, &Broadcast, sizeof(ULONG));
    }
    LxNlAttrString(Builder, LX_IFA_LABEL, Label);
    LxNlAttrU32(Builder, LX_IFA_FLAGS, LX_IFA_F_PERMANENT);
    Cache.Preferred = MAXULONG;
    Cache.Valid = MAXULONG;
    Cache.Created = 0;
    Cache.Updated = 0;
    LxNlAttr(Builder, LX_IFA_CACHEINFO, &Cache, sizeof(Cache));
    LxNlEnd(Builder);
}

static
VOID
LxNlEmitLoopback6(
    _Inout_ PLX_NL_BUILDER Builder,
    _In_ PLX_NLMSGHDR Request)
{
    static const UCHAR Loopback[16] = { 0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,1 };
    PLX_IFADDRMSG Info = LxNlBegin(Builder, Request, LX_RTM_NEWADDR, LX_NLM_F_MULTI, sizeof(LX_IFADDRMSG));
    LX_NL_CACHEINFO Cache;

    Info->Family = LX_AF_INET6;
    Info->PrefixLength = 128;
    Info->Flags = LX_IFA_F_PERMANENT;
    Info->Scope = LX_RT_SCOPE_HOST;
    Info->Index = 1;
    LxNlAttr(Builder, LX_IFA_ADDRESS, Loopback, sizeof(Loopback));
    LxNlAttrU32(Builder, LX_IFA_FLAGS, LX_IFA_F_PERMANENT);
    Cache.Preferred = MAXULONG;
    Cache.Valid = MAXULONG;
    Cache.Created = 0;
    Cache.Updated = 0;
    LxNlAttr(Builder, LX_IFA_CACHEINFO, &Cache, sizeof(Cache));
    LxNlEnd(Builder);
}

static
VOID
LxNlEmitRoute(
    _Inout_ PLX_NL_BUILDER Builder,
    _In_ PLX_NLMSGHDR Request,
    _In_ PLX_NL_ROUTE Route)
{
    PLX_RTMSG Info = LxNlBegin(Builder, Request, LX_RTM_NEWROUTE, LX_NLM_F_MULTI, sizeof(LX_RTMSG));

    Info->Family = LX_AF_INET;
    Info->DstLength = (UCHAR)LxNlPrefixLength(Route->Mask);
    Info->Table = LX_RT_TABLE_MAIN;
    Info->Protocol = (Route->Gateway != 0) ? LX_RTPROT_BOOT : LX_RTPROT_KERNEL;
    Info->Scope = (Route->Gateway != 0) ? LX_RT_SCOPE_UNIVERSE : LX_RT_SCOPE_LINK;
    Info->Type = LX_RTN_UNICAST;

    LxNlAttrU32(Builder, LX_RTA_TABLE, LX_RT_TABLE_MAIN);
    if (Info->DstLength != 0)
        LxNlAttr(Builder, LX_RTA_DST, &Route->Dest, sizeof(ULONG));
    if (Route->Gateway != 0)
        LxNlAttr(Builder, LX_RTA_GATEWAY, &Route->Gateway, sizeof(ULONG));
    else if (Route->Source != 0)
        LxNlAttr(Builder, LX_RTA_PREFSRC, &Route->Source, sizeof(ULONG));
    if (Route->Metric != 0)
        LxNlAttrU32(Builder, LX_RTA_PRIORITY, Route->Metric);
    LxNlAttrU32(Builder, LX_RTA_OIF, Route->Index);
    LxNlEnd(Builder);
}

static
PCSTR
LxNlFindAttribute(
    _In_reads_bytes_(Length) PUCHAR Attributes,
    _In_ ULONG Length,
    _In_ USHORT Type,
    _Out_ PULONG DataLength)
{
    ULONG Offset = 0;

    while (Offset + 4 <= Length)
    {
        USHORT AttributeLength = *(PUSHORT)(Attributes + Offset);
        USHORT AttributeType = *(PUSHORT)(Attributes + Offset + 2) & 0x3FFF;

        if ((AttributeLength < 4) || (Offset + AttributeLength > Length))
            break;
        if (AttributeType == Type)
        {
            *DataLength = AttributeLength - 4;
            return (PCSTR)(Attributes + Offset + 4);
        }
        Offset += LX_NL_ALIGN(AttributeLength);
    }
    *DataLength = 0;
    return NULL;
}

static
ULONG
LxNlStringLength(
    _In_reads_bytes_(Length) PCSTR String,
    _In_ ULONG Length)
{
    ULONG Index = 0;

    while ((Index < Length) && (String[Index] != '\0'))
        Index++;
    return Index;
}

static
LONG
LxNlRoute(
    _Inout_ PLX_NL_BUILDER Builder,
    _In_ PLX_NLMSGHDR Request,
    _Out_ PBOOLEAN Replied)
{
    ULONG Payload = Request->Length - sizeof(LX_NLMSGHDR);
    PUCHAR Body = (PUCHAR)(Request + 1);
    UCHAR Family = (Payload >= 1) ? Body[0] : LX_AF_UNSPEC;
    BOOLEAN Dump = (Request->Flags & LX_NLM_F_DUMP) == LX_NLM_F_DUMP;
    PLX_NL_STATE State;
    ULONG Index;

    *Replied = FALSE;
    if ((Request->Type < LX_RTM_NEWLINK) || (Request->Type > LX_RTM_MAX))
        return -LX_EOPNOTSUPP;
    if ((Request->Type & 3) != 2)
        return -LX_EOPNOTSUPP;
    if ((Request->Type != LX_RTM_GETLINK) && (Request->Type != LX_RTM_GETADDR) && (Request->Type != LX_RTM_GETROUTE))
    {
        if (!Dump)
            return -LX_EOPNOTSUPP;
        LxNlDone(Builder, Request);
        *Replied = TRUE;
        return 0;
    }

    State = LxAllocPaged(sizeof(*State));
    if (State == NULL)
        return -LX_ENOMEM;
    LxNlSnapshot(State);

    if (Request->Type == LX_RTM_GETLINK)
    {
        if (Dump)
        {
            for (Index = 0; Index < State->LinkCount; Index++)
                LxNlEmitLink(Builder, Request, &State->Links[Index], LX_NLM_F_MULTI);
            LxNlDone(Builder, Request);
            *Replied = TRUE;
        }
        else
        {
            PLX_IFINFOMSG Info = (PLX_IFINFOMSG)Body;
            PCSTR Name = NULL;
            ULONG NameLength = 0;

            if (Payload >= sizeof(LX_IFINFOMSG))
                Name = LxNlFindAttribute(Body + sizeof(LX_IFINFOMSG), Payload - sizeof(LX_IFINFOMSG), LX_IFLA_IFNAME, &NameLength);
            for (Index = 0; Index < State->LinkCount; Index++)
            {
                PLX_NL_LINK Link = &State->Links[Index];

                if (((Payload >= sizeof(LX_IFINFOMSG)) && (Info->Index != 0) && ((ULONG)Info->Index == Link->Index)) ||
                    ((Name != NULL) && (LxNlStringLength(Name, NameLength) == strlen(Link->Name)) &&
                     (strncmp(Name, Link->Name, strlen(Link->Name)) == 0)))
                {
                    LxNlEmitLink(Builder, Request, Link, 0);
                    *Replied = TRUE;
                    break;
                }
            }
            if (!*Replied)
            {
                LxFreePaged(State);
                return -LX_ENODEV;
            }
        }
    }
    else if (Request->Type == LX_RTM_GETADDR)
    {
        if (!Dump)
        {
            LxFreePaged(State);
            return -LX_EOPNOTSUPP;
        }
        if ((Family == LX_AF_UNSPEC) || (Family == LX_AF_INET))
        {
            for (Index = 0; Index < State->AddressCount; Index++)
                LxNlEmitAddress(Builder, Request, State, &State->Addresses[Index]);
        }
        if ((Family == LX_AF_UNSPEC) || (Family == LX_AF_INET6))
            LxNlEmitLoopback6(Builder, Request);
        LxNlDone(Builder, Request);
        *Replied = TRUE;
    }
    else
    {
        if (!Dump)
        {
            LxFreePaged(State);
            return -LX_EOPNOTSUPP;
        }
        if ((Family == LX_AF_UNSPEC) || (Family == LX_AF_INET))
        {
            for (Index = 0; Index < State->RouteCount; Index++)
                LxNlEmitRoute(Builder, Request, &State->Routes[Index]);
        }
        LxNlDone(Builder, Request);
        *Replied = TRUE;
    }

    LxFreePaged(State);
    return 0;
}

static
VOID
LxNlEmitControlFamily(
    _Inout_ PLX_NL_BUILDER Builder,
    _In_ PLX_NLMSGHDR Request,
    _In_ USHORT Flags)
{
    PLX_GENLMSGHDR Header = LxNlBegin(Builder, Request, LX_GENL_ID_CTRL, Flags, sizeof(LX_GENLMSGHDR));
    USHORT Id = LX_GENL_ID_CTRL;

    Header->Command = LX_CTRL_CMD_NEWFAMILY;
    Header->Version = 2;
    LxNlAttrString(Builder, LX_CTRL_ATTR_FAMILY_NAME, "nlctrl");
    LxNlAttr(Builder, LX_CTRL_ATTR_FAMILY_ID, &Id, sizeof(Id));
    LxNlAttrU32(Builder, LX_CTRL_ATTR_VERSION, 2);
    LxNlAttrU32(Builder, LX_CTRL_ATTR_HDRSIZE, 0);
    LxNlAttrU32(Builder, LX_CTRL_ATTR_MAXATTR, 10);
    LxNlEnd(Builder);
}

static
LONG
LxNlGeneric(
    _Inout_ PLX_NL_BUILDER Builder,
    _In_ PLX_NLMSGHDR Request,
    _Out_ PBOOLEAN Replied)
{
    ULONG Payload = Request->Length - sizeof(LX_NLMSGHDR);
    PLX_GENLMSGHDR Header = (PLX_GENLMSGHDR)(Request + 1);
    PUCHAR Attributes = (PUCHAR)(Header + 1);
    PCSTR Name;
    PUSHORT Id;
    ULONG Length;

    *Replied = FALSE;
    if (Request->Type != LX_GENL_ID_CTRL)
        return -LX_ENOENT;
    if ((Payload < sizeof(LX_GENLMSGHDR)) || (Header->Command != LX_CTRL_CMD_GETFAMILY))
        return -LX_EOPNOTSUPP;

    if ((Request->Flags & LX_NLM_F_DUMP) == LX_NLM_F_DUMP)
    {
        LxNlEmitControlFamily(Builder, Request, LX_NLM_F_MULTI);
        LxNlDone(Builder, Request);
        *Replied = TRUE;
        return 0;
    }

    Name = LxNlFindAttribute(Attributes, Payload - sizeof(LX_GENLMSGHDR), LX_CTRL_ATTR_FAMILY_NAME, &Length);
    if ((Name != NULL) && (LxNlStringLength(Name, Length) == 6) && (strncmp(Name, "nlctrl", 6) == 0))
    {
        LxNlEmitControlFamily(Builder, Request, 0);
        *Replied = TRUE;
        return 0;
    }
    Id = (PUSHORT)LxNlFindAttribute(Attributes, Payload - sizeof(LX_GENLMSGHDR), LX_CTRL_ATTR_FAMILY_ID, &Length);
    if ((Id != NULL) && (Length >= sizeof(USHORT)) && (*Id == LX_GENL_ID_CTRL))
    {
        LxNlEmitControlFamily(Builder, Request, 0);
        *Replied = TRUE;
        return 0;
    }
    return -LX_ENOENT;
}

static
VOID
LxNlAutobind(
    _In_ PLX_NL_SOCKET Socket)
{
    PLX_THREAD Thread = LxCurrentThread();
    ULONG Candidate = (Thread != NULL) ? LxPidNr(Thread->Process, Thread->Process) : 0;
    PLIST_ENTRY Entry;
    KIRQL OldIrql;
    BOOLEAN Used;

    KeAcquireSpinLock(&LxNlSocketsLock, &OldIrql);
    if (Socket->Bound)
    {
        KeReleaseSpinLock(&LxNlSocketsLock, OldIrql);
        return;
    }
    for (;;)
    {
        Used = (Candidate == 0);
        for (Entry = LxNlSockets.Flink; !Used && (Entry != &LxNlSockets); Entry = Entry->Flink)
        {
            PLX_NL_SOCKET Other = CONTAINING_RECORD(Entry, LX_NL_SOCKET, GlobalLink);

            if ((Other != Socket) && Other->Bound && (Other->Protocol == Socket->Protocol) &&
                (Other->Instance == Socket->Instance) && (Other->PortId == Candidate))
            {
                Used = TRUE;
            }
        }
        if (!Used)
            break;
        Candidate = LxNlRover--;
        if (LxNlRover < 0x80000000)
            LxNlRover = 0xFFFFEFFF;
    }
    Socket->PortId = Candidate;
    Socket->Bound = TRUE;
    KeReleaseSpinLock(&LxNlSocketsLock, OldIrql);
}

static
LONG
LxNlBindPort(
    _In_ PLX_NL_SOCKET Socket,
    _In_ ULONG PortId)
{
    PLIST_ENTRY Entry;
    KIRQL OldIrql;
    LONG Error = 0;

    KeAcquireSpinLock(&LxNlSocketsLock, &OldIrql);
    if (Socket->Bound)
    {
        Error = (Socket->PortId == PortId) ? 0 : -LX_EINVAL;
    }
    else
    {
        for (Entry = LxNlSockets.Flink; Entry != &LxNlSockets; Entry = Entry->Flink)
        {
            PLX_NL_SOCKET Other = CONTAINING_RECORD(Entry, LX_NL_SOCKET, GlobalLink);

            if ((Other != Socket) && Other->Bound && (Other->Protocol == Socket->Protocol) &&
                (Other->Instance == Socket->Instance) && (Other->PortId == PortId))
            {
                Error = -LX_EADDRINUSE;
                break;
            }
        }
        if (Error == 0)
        {
            Socket->PortId = PortId;
            Socket->Bound = TRUE;
        }
    }
    KeReleaseSpinLock(&LxNlSocketsLock, OldIrql);
    return Error;
}

ULONG
LxNetlinkAddressIndex(
    _In_ ULONG Address)
{
    PLX_NL_STATE State;
    ULONG Index, Result = 0;

    if ((Address & 0xFF) == 127)
        return 1;
    State = LxAllocPaged(sizeof(*State));
    if (State == NULL)
        return 0;
    LxNlSnapshot(State);
    for (Index = 0; Index < State->AddressCount; Index++)
    {
        if (State->Addresses[Index].Address == Address)
        {
            Result = State->Addresses[Index].Index;
            break;
        }
    }
    LxFreePaged(State);
    return Result;
}

LONG64
LxNetlinkSocket(
    _In_ PLX_PROCESS Process,
    _In_ ULONG Type,
    _In_ ULONG Protocol,
    _In_ ULONG Flags)
{
    PLX_NL_SOCKET Socket;
    PLX_FILE File;
    KIRQL OldIrql;
    LONG Fd;

    if ((Type != LX_SOCK_RAW) && (Type != LX_SOCK_DGRAM))
        return -LX_ESOCKTNOSUPPORT;
    if ((Protocol != LX_NETLINK_ROUTE) && (Protocol != LX_NETLINK_KOBJECT_UEVENT) && (Protocol != LX_NETLINK_GENERIC))
        return -LX_EPROTONOSUPPORT;

    Socket = LxAlloc(sizeof(*Socket));
    if (Socket == NULL)
        return -LX_ENOMEM;
    LxInstanceReference(Process->Instance);
    Socket->Instance = Process->Instance;
    Socket->Protocol = Protocol;
    Socket->Type = Type;
    Socket->RecvBuffer = 212992;
    Socket->SendBuffer = 212992;
    Socket->Ino = (ULONG64)InterlockedIncrement64(&LxNlInode);
    KeInitializeEvent(&Socket->Lock, SynchronizationEvent, TRUE);
    InitializeListHead(&Socket->Queue);
    LxPollHeadInit(&Socket->Poll);

    File = LxFileAlloc(&LxNlOps, LX_O_RDWR | (Flags & LX_O_NONBLOCK), LX_S_IFSOCK);
    if (File == NULL)
    {
        LxInstanceDereference(Socket->Instance);
        LxFree(Socket);
        return -LX_ENOMEM;
    }
    File->Object = Socket;
    File->Ino = Socket->Ino;
    File->Path = LxStrPrintf("socket:[%llu]", Socket->Ino);

    KeAcquireSpinLock(&LxNlSocketsLock, &OldIrql);
    InsertTailList(&LxNlSockets, &Socket->GlobalLink);
    KeReleaseSpinLock(&LxNlSocketsLock, OldIrql);

    Fd = LxFdInstall(Process, File, 0, (Flags & LX_SOCK_CLOEXEC) != 0);
    LxFileDereference(File);
    return Fd;
}

static
LONG64
LxNlBind(
    _In_ PLX_CALL Call,
    _In_ PLX_FILE File)
{
    PLX_NL_SOCKET Socket = File->Object;
    LX_SOCKADDR_NL Address;
    LONG64 Result;

    if (Call->Args[2] < sizeof(Address))
        return -LX_EINVAL;
    Result = LxCopyFromUser(&Address, Call->Args[1], sizeof(Address));
    if (Result != 0)
        return Result;
    if (Address.Family != LX_AF_NETLINK)
        return -LX_EINVAL;

    Socket->Groups = Address.Groups;
    if (Address.Pid != 0)
        return LxNlBindPort(Socket, Address.Pid);
    LxNlAutobind(Socket);
    return 0;
}

static
LONG64
LxNlConnect(
    _In_ PLX_CALL Call,
    _In_ PLX_FILE File)
{
    PLX_NL_SOCKET Socket = File->Object;
    LX_SOCKADDR_NL Address;
    LONG64 Result;

    if (Call->Args[2] < sizeof(USHORT))
        return -LX_EINVAL;
    RtlZeroMemory(&Address, sizeof(Address));
    Result = LxCopyFromUser(&Address, Call->Args[1], (SIZE_T)min(Call->Args[2], sizeof(Address)));
    if (Result != 0)
        return Result;
    if (Address.Family == LX_AF_UNSPEC)
        return 0;
    if ((Address.Family != LX_AF_NETLINK) || (Call->Args[2] < sizeof(Address)))
        return -LX_EINVAL;
    LxNlAutobind(Socket);
    return 0;
}

static
LONG64
LxNlUnsupported(
    _In_ PLX_CALL Call,
    _In_ PLX_FILE File)
{
    UNREFERENCED_PARAMETER(Call);
    UNREFERENCED_PARAMETER(File);
    return -LX_EOPNOTSUPP;
}

static
LONG64
LxNlAccept(
    _In_ PLX_CALL Call,
    _In_ PLX_FILE File,
    _In_ ULONG Flags)
{
    UNREFERENCED_PARAMETER(Call);
    UNREFERENCED_PARAMETER(File);
    UNREFERENCED_PARAMETER(Flags);
    return -LX_EOPNOTSUPP;
}

static
LONG64
LxNlName(
    _In_ PLX_CALL Call,
    _In_ PLX_FILE File,
    _In_ BOOLEAN Peer)
{
    PLX_NL_SOCKET Socket = File->Object;
    LX_SOCKADDR_NL Address;

    RtlZeroMemory(&Address, sizeof(Address));
    Address.Family = LX_AF_NETLINK;
    if (!Peer)
    {
        LxNlAutobind(Socket);
        Address.Pid = Socket->PortId;
        Address.Groups = Socket->Groups;
    }
    {
        LONG Available;
        LONG64 Result = LxCopyFromUser(&Available, Call->Args[2], sizeof(Available));

        if (Result != 0)
            return Result;
        if (Available < 0)
            return -LX_EINVAL;
        Result = LxCopyToUser(Call->Args[1], &Address, min((ULONG)Available, sizeof(Address)));
        if (Result == 0)
        {
            Available = sizeof(Address);
            Result = LxCopyToUser(Call->Args[2], &Available, sizeof(Available));
        }
        return Result;
    }
}

static
LONG64
LxNlSendmsg(
    _In_ PLX_FILE File,
    _In_ PLX_MSGHDR Header,
    _In_ BOOLEAN KernelIov,
    _In_ ULONG Flags)
{
    PLX_NL_SOCKET Socket = File->Object;
    LX_NL_BUILDER Builder;
    PLX_IOVEC Vectors;
    ULONG64 Total;
    PUCHAR Data;
    ULONG Offset = 0;
    LONG64 Result;

    UNREFERENCED_PARAMETER(Flags);

    if ((Header->Name != 0) && (Header->NameLength >= (LONG)sizeof(LX_SOCKADDR_NL)))
    {
        LX_SOCKADDR_NL Address;

        Result = LxCopyFromUser(&Address, Header->Name, sizeof(Address));
        if (Result != 0)
            return Result;
        if (Address.Family != LX_AF_NETLINK)
            return -LX_EINVAL;
        if (Address.Pid != 0)
            return -LX_ECONNREFUSED;
    }

    Result = LxSocketGatherIov(Header, KernelIov, &Vectors, &Total);
    if (Result != 0)
        return Result;
    if (Total > (ULONG64)Socket->SendBuffer)
    {
        LxFree(Vectors);
        return -LX_EMSGSIZE;
    }
    Data = LxAllocPaged((SIZE_T)max(Total, 1));
    Builder.Buffer = LxAllocPaged(2 * LX_NL_DATAGRAM + LX_NL_HEADROOM);
    if ((Data == NULL) || (Builder.Buffer == NULL))
    {
        LxFreePaged(Data);
        LxFreePaged(Builder.Buffer);
        LxFree(Vectors);
        return -LX_ENOMEM;
    }
    Result = LxSocketCopyIn(Vectors, Header->IovLength, 0, Data, (ULONG)Total);
    LxFree(Vectors);
    if (Result != 0)
    {
        LxFreePaged(Data);
        LxFreePaged(Builder.Buffer);
        return Result;
    }

    LxNlAutobind(Socket);
    Builder.Socket = Socket;
    Builder.Used = 0;
    Builder.Start = 0;

    while ((Socket->Protocol != LX_NETLINK_KOBJECT_UEVENT) && (Offset + sizeof(LX_NLMSGHDR) <= Total))
    {
        PLX_NLMSGHDR Request = (PLX_NLMSGHDR)(Data + Offset);
        BOOLEAN Replied = FALSE;
        LONG Error = 0;

        if ((Request->Length < sizeof(LX_NLMSGHDR)) || (Request->Length > Total - Offset))
            break;
        if ((Request->Flags & LX_NLM_F_REQUEST) && (Request->Type >= LX_NLMSG_MIN_TYPE))
        {
            if (Socket->Protocol == LX_NETLINK_ROUTE)
                Error = LxNlRoute(&Builder, Request, &Replied);
            else
                Error = LxNlGeneric(&Builder, Request, &Replied);
            if ((Error != 0) || (Request->Flags & LX_NLM_F_ACK))
                LxNlError(&Builder, Request, Error);
        }
        Offset += LX_NL_ALIGN(Request->Length);
    }

    LxNlFlush(&Builder);
    LxFreePaged(Data);
    LxFreePaged(Builder.Buffer);
    return (LONG64)Total;
}

static
BOOLEAN
LxNlReady(
    _In_ PVOID Context)
{
    PLX_NL_SOCKET Socket = Context;

    return !IsListEmpty(&Socket->Queue);
}

static
LONG64
LxNlRecvmsg(
    _In_ PLX_FILE File,
    _Inout_ PLX_MSGHDR Header,
    _In_ BOOLEAN KernelIov,
    _In_ ULONG Flags)
{
    PLX_NL_SOCKET Socket = File->Object;
    PLX_NL_MSG Message;
    PLX_IOVEC Vectors;
    ULONG64 Total;
    ULONG Copy, Length;
    LONG64 Result;

    Result = LxSocketGatherIov(Header, KernelIov, &Vectors, &Total);
    if (Result != 0)
        return Result;

    for (;;)
    {
        KeWaitForSingleObject(&Socket->Lock, Executive, KernelMode, FALSE, NULL);
        if (!IsListEmpty(&Socket->Queue))
            break;
        KeSetEvent(&Socket->Lock, IO_NO_INCREMENT, FALSE);
        if ((File->Flags & LX_O_NONBLOCK) || (Flags & LX_MSG_DONTWAIT))
        {
            LxFree(Vectors);
            return -LX_EAGAIN;
        }
        if (Socket->RecvTimeout > 0)
        {
            LARGE_INTEGER Timeout;

            Timeout.QuadPart = -Socket->RecvTimeout;
            Result = LxWaitHead(&Socket->Poll, LxNlReady, Socket, &Timeout);
        }
        else
        {
            Result = LxWaitHead(&Socket->Poll, LxNlReady, Socket, NULL);
        }
        if (Result != 0)
        {
            LxFree(Vectors);
            return Result;
        }
    }

    Message = CONTAINING_RECORD(Socket->Queue.Flink, LX_NL_MSG, Link);
    Length = Message->Length;
    Copy = (ULONG)min(Total, (ULONG64)Length);
    Result = LxSocketCopyOut(Vectors, Header->IovLength, 0, Message->Data, Copy);
    if (!(Flags & LX_MSG_PEEK))
    {
        RemoveEntryList(&Message->Link);
        Socket->QueueBytes -= Length;
        LxFree(Message);
    }
    KeSetEvent(&Socket->Lock, IO_NO_INCREMENT, FALSE);
    LxFree(Vectors);
    Header->Flags = (Length > Total) ? LX_MSG_TRUNC : 0;
    Header->ControlLength = 0;
    if ((Result == 0) && (Header->Name != 0) && (Header->NameLength > 0))
    {
        LX_SOCKADDR_NL Address;

        RtlZeroMemory(&Address, sizeof(Address));
        Address.Family = LX_AF_NETLINK;
        Result = LxCopyToUser(Header->Name, &Address, min((ULONG)Header->NameLength, sizeof(Address)));
        Header->NameLength = sizeof(Address);
    }
    else
    {
        Header->NameLength = 0;
    }
    if (Result == 0)
        Result = (Flags & LX_MSG_TRUNC) ? (LONG64)Length : (LONG64)Copy;
    return Result;
}

static
LONG64
LxNlSetsockopt(
    _In_ PLX_CALL Call,
    _In_ PLX_FILE File)
{
    PLX_NL_SOCKET Socket = File->Object;
    ULONG Level = (ULONG)Call->Args[1], Name = (ULONG)Call->Args[2];
    LONG Value = 0;
    LONG64 Result;

    if (Level == LX_SOL_SOCKET)
    {
        switch (Name)
        {
            case LX_SO_ATTACH_FILTER:
            case LX_SO_DETACH_FILTER:
            case LX_SO_LOCK_FILTER:
            case LX_SO_ATTACH_BPF:
            case LX_SO_TIMESTAMP:
            case LX_SO_PRIORITY:
            case LX_SO_REUSEADDR:
            case LX_SO_REUSEPORT:
                return 0;
            case LX_SO_RCVTIMEO_OLD:
            case LX_SO_RCVTIMEO_NEW:
            {
                LX_TIMEVAL Timeval;

                if (Call->Args[4] < sizeof(Timeval))
                    return -LX_EINVAL;
                Result = LxCopyFromUser(&Timeval, Call->Args[3], sizeof(Timeval));
                if (Result == 0)
                    Socket->RecvTimeout = Timeval.Seconds * 10000000LL + Timeval.Microseconds * 10;
                return Result;
            }
            case LX_SO_SNDTIMEO_OLD:
            case LX_SO_SNDTIMEO_NEW:
                return 0;
        }
    }
    else if (Level != LX_SOL_NETLINK)
    {
        return -LX_ENOPROTOOPT;
    }

    Result = LxSockoptInt(Call->Args[3], Call->Args[4], &Value);
    if (Result != 0)
        return Result;

    if (Level == LX_SOL_SOCKET)
    {
        switch (Name)
        {
            case LX_SO_RCVBUF:
            case LX_SO_RCVBUFFORCE:
                Socket->RecvBuffer = (LONG)max(min((LONG64)Value * 2, 64 * 1024 * 1024), 2304);
                return 0;
            case LX_SO_SNDBUF:
            case LX_SO_SNDBUFFORCE:
                Socket->SendBuffer = (LONG)max(min((LONG64)Value * 2, 64 * 1024 * 1024), 4608);
                return 0;
            case LX_SO_PASSCRED:
                Socket->PassCred = (Value != 0);
                return 0;
        }
        DPRINT1("Linux netlink option level %lu name %lu is not supported\n", Level, Name);
        return -LX_ENOPROTOOPT;
    }

    switch (Name)
    {
        case LX_NETLINK_ADD_MEMBERSHIP:
        case LX_NETLINK_DROP_MEMBERSHIP:
            if (Value <= 0)
                return -LX_EINVAL;
            if (Value <= 32)
            {
                if (Name == LX_NETLINK_ADD_MEMBERSHIP)
                    Socket->Groups |= 1UL << (Value - 1);
                else
                    Socket->Groups &= ~(1UL << (Value - 1));
            }
            LxNlAutobind(Socket);
            return 0;
        case LX_NETLINK_PKTINFO:
            Socket->PktInfo = (Value != 0);
            return 0;
        case LX_NETLINK_BROADCAST_ERROR:
            Socket->BroadcastError = (Value != 0);
            return 0;
        case LX_NETLINK_NO_ENOBUFS:
            Socket->NoEnobufs = (Value != 0);
            return 0;
        case LX_NETLINK_LISTEN_ALL_NSID:
            Socket->ListenAllNsid = (Value != 0);
            return 0;
        case LX_NETLINK_CAP_ACK:
            Socket->CapAck = (Value != 0);
            return 0;
        case LX_NETLINK_EXT_ACK:
            Socket->ExtAck = (Value != 0);
            return 0;
        case LX_NETLINK_GET_STRICT_CHK:
            Socket->StrictCheck = (Value != 0);
            return 0;
    }
    DPRINT1("Linux netlink option level %lu name %lu is not supported\n", Level, Name);
    return -LX_ENOPROTOOPT;
}

static
LONG64
LxNlGetsockopt(
    _In_ PLX_CALL Call,
    _In_ PLX_FILE File)
{
    PLX_NL_SOCKET Socket = File->Object;
    ULONG Level = (ULONG)Call->Args[1], Name = (ULONG)Call->Args[2];
    LONG Value;

    if (Level == LX_SOL_SOCKET)
    {
        switch (Name)
        {
            case LX_SO_TYPE:
                Value = (LONG)Socket->Type;
                break;
            case LX_SO_DOMAIN:
                Value = LX_AF_NETLINK;
                break;
            case LX_SO_PROTOCOL:
                Value = (LONG)Socket->Protocol;
                break;
            case LX_SO_ERROR:
            case LX_SO_ACCEPTCONN:
                Value = 0;
                break;
            case LX_SO_RCVBUF:
                Value = Socket->RecvBuffer;
                break;
            case LX_SO_SNDBUF:
                Value = Socket->SendBuffer;
                break;
            case LX_SO_PASSCRED:
                Value = Socket->PassCred;
                break;
            default:
                return -LX_ENOPROTOOPT;
        }
        return LxSockoptReturn(Call, &Value, sizeof(Value));
    }
    if (Level != LX_SOL_NETLINK)
        return -LX_ENOPROTOOPT;

    switch (Name)
    {
        case LX_NETLINK_PKTINFO:
            Value = Socket->PktInfo;
            break;
        case LX_NETLINK_BROADCAST_ERROR:
            Value = Socket->BroadcastError;
            break;
        case LX_NETLINK_NO_ENOBUFS:
            Value = Socket->NoEnobufs;
            break;
        case LX_NETLINK_LISTEN_ALL_NSID:
            Value = Socket->ListenAllNsid;
            break;
        case LX_NETLINK_CAP_ACK:
            Value = Socket->CapAck;
            break;
        case LX_NETLINK_EXT_ACK:
            Value = Socket->ExtAck;
            break;
        case LX_NETLINK_GET_STRICT_CHK:
            Value = Socket->StrictCheck;
            break;
        case LX_NETLINK_LIST_MEMBERSHIPS:
            Value = (LONG)Socket->Groups;
            break;
        default:
            return -LX_ENOPROTOOPT;
    }
    return LxSockoptReturn(Call, &Value, sizeof(Value));
}

static
LONG64
LxNlRead(
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
    return LxNlRecvmsg(File, &Header, TRUE, 0);
}

static
LONG64
LxNlWrite(
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
    return LxNlSendmsg(File, &Header, TRUE, 0);
}

static
ULONG
LxNlPoll(
    _In_ PLX_FILE File,
    _In_opt_ PLX_POLL_ENTRY Entry)
{
    PLX_NL_SOCKET Socket = File->Object;
    ULONG Mask = LX_POLLOUT | LX_POLLWRNORM;

    if (Entry != NULL)
        LxPollAdd(&Socket->Poll, Entry);
    if (!IsListEmpty(&Socket->Queue))
        Mask |= LX_POLLIN | LX_POLLRDNORM;
    return Mask;
}

static
LONG
LxNlStat(
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
LxNlRelease(
    _In_ PLX_FILE File)
{
    PLX_NL_SOCKET Socket = File->Object;
    KIRQL OldIrql;

    if (Socket == NULL)
        return;
    KeAcquireSpinLock(&LxNlSocketsLock, &OldIrql);
    RemoveEntryList(&Socket->GlobalLink);
    KeReleaseSpinLock(&LxNlSocketsLock, OldIrql);
    while (!IsListEmpty(&Socket->Queue))
        LxFree(CONTAINING_RECORD(RemoveHeadList(&Socket->Queue), LX_NL_MSG, Link));
    LxInstanceDereference(Socket->Instance);
    LxFree(Socket);
}

const LX_SOCKET_OPS LxNetlinkSocketOps =
{
    LxNlBind,
    LxNlUnsupported,
    LxNlConnect,
    LxNlAccept,
    LxNlName,
    LxNlSendmsg,
    LxNlRecvmsg,
    LxNlUnsupported,
    LxNlSetsockopt,
    LxNlGetsockopt
};
