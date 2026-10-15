/*
 * COPYRIGHT:   See COPYING in the top level directory
 * PROJECT:     ReactOS TCP/IP protocol driver
 * FILE:        tcpip/ninfo.c
 * PURPOSE:     Network information
 * PROGRAMMERS: Art Yerkes
 * REVISIONS:
 *   CSH 01/08-2000 Created
 */

#include "precomp.h"

#include <tcpip_undoc.h>

#define IP_ROUTE_TYPE_ADD 3
#define IP_ROUTE_TYPE_DEL 2

/* Get IPRouteEntry s for each of the routes in the system */
TDI_STATUS InfoTdiQueryGetRouteTable( PIP_INTERFACE IF, PNDIS_BUFFER Buffer, PUINT BufferSize ) {
    TDI_STATUS Status;
    UINT RtCount;
    UINT Capacity = CountFIBs(IF);
    UINT Size;
    PFIB_ENTRY RCache, RCacheCur;
    PIPROUTE_ENTRY RouteEntries, RtCurrent;
    UINT i;

    TI_DbgPrint(DEBUG_INFO, ("Called, routes = %d\n",
				    Capacity));

    if (Capacity == 0)
        return InfoCopyOut(NULL, 0, NULL, BufferSize);

    for (;;) {
        if (Capacity > MAXULONG / sizeof(FIB_ENTRY))
            return TDI_NO_RESOURCES;

        RCache = ExAllocatePoolWithTag(NonPagedPool, sizeof(FIB_ENTRY) * Capacity, FIB_TAG);
        if (!RCache)
            return TDI_NO_RESOURCES;

        RtCount = CopyFIBs(IF, RCache, Capacity);
        if (RtCount <= Capacity)
            break;

        for (i = 0; i < Capacity; i++)
            NBDereferenceNeighbor(RCache[i].Router);

        ExFreePoolWithTag(RCache, FIB_TAG);
        Capacity = RtCount;
    }

    if (RtCount == 0) {
        ExFreePoolWithTag(RCache, FIB_TAG);
        return InfoCopyOut(NULL, 0, NULL, BufferSize);
    }

    if (RtCount > MAXULONG / sizeof(IPROUTE_ENTRY)) {
        for (i = 0; i < RtCount; i++)
            NBDereferenceNeighbor(RCache[i].Router);
        ExFreePoolWithTag(RCache, FIB_TAG);
        return TDI_NO_RESOURCES;
    }

    Size = sizeof(IPROUTE_ENTRY) * RtCount;
    RouteEntries = ExAllocatePoolWithTag(NonPagedPool, Size, ROUTE_ENTRY_TAG);
    if (!RouteEntries) {
        for (i = 0; i < RtCount; i++)
            NBDereferenceNeighbor(RCache[i].Router);
        ExFreePoolWithTag(RCache, FIB_TAG);
        return TDI_NO_RESOURCES;
    }

    RtlZeroMemory(RouteEntries, Size);
    RtCurrent = RouteEntries;
    RCacheCur = RCache;

    while( RtCurrent < RouteEntries + RtCount ) {
	ASSERT(RCacheCur->Router);

	RtlCopyMemory( &RtCurrent->Dest,
		       &RCacheCur->NetworkAddress.Address,
		       sizeof(RtCurrent->Dest) );
	RtlCopyMemory( &RtCurrent->Mask,
		       &RCacheCur->Netmask.Address,
		       sizeof(RtCurrent->Mask) );
	RtlCopyMemory( &RtCurrent->Gw,
		       &RCacheCur->Router->Address.Address,
		       sizeof(RtCurrent->Gw) );

	RtCurrent->Metric1 = RCacheCur->Metric;
	RtCurrent->Type = TDI_ADDRESS_TYPE_IP;

	TI_DbgPrint
	    (DEBUG_INFO,
	     ("%d: NA %08x NM %08x GW %08x MT %x\n",
	      RtCurrent - RouteEntries,
	      RtCurrent->Dest,
	      RtCurrent->Mask,
	      RtCurrent->Gw,
	      RtCurrent->Metric1 ));

	RtCurrent->Index = IF->Index;

	RtCurrent++; RCacheCur++;
    }

    Status = InfoCopyOut( (PCHAR)RouteEntries, Size, Buffer, BufferSize );

    for (i = 0; i < RtCount; i++)
        NBDereferenceNeighbor(RCache[i].Router);

    ExFreePoolWithTag( RouteEntries, ROUTE_ENTRY_TAG );
    ExFreePoolWithTag( RCache, FIB_TAG );

    TI_DbgPrint(DEBUG_INFO, ("Returning %08x\n", Status));

    return Status;
}

TDI_STATUS InfoTdiQueryGetAddrTable(TDIEntityID ID,
				    PNDIS_BUFFER Buffer,
				    PUINT BufferSize)
{
    KIRQL OldIrql;
    PIPADDR_ENTRY IPEntry;
    PIP_INTERFACE CurrentIF;
    UINT i;

    TI_DbgPrint(DEBUG_INFO, ("Called.\n"));


    TcpipAcquireSpinLock(&EntityListLock, &OldIrql);

    for (i = 0; i < EntityCount; i++)
    {
        if (EntityList[i].tei_entity == ID.tei_entity &&
            EntityList[i].tei_instance == ID.tei_instance)
            break;
    }

    if (i == EntityCount)
    {
        TcpipReleaseSpinLock(&EntityListLock, OldIrql);
        return TDI_INVALID_PARAMETER;
    }

    IPEntry = ExAllocatePoolWithTag(NonPagedPool, sizeof(IPADDR_ENTRY), IP_ADDRESS_TAG);
    if (!IPEntry)
    {
        TcpipReleaseSpinLock(&EntityListLock, OldIrql);
        return TDI_NO_RESOURCES;
    }

    CurrentIF = EntityList[i].context;

    IPEntry->Index = CurrentIF->Index;
    GetInterfaceIPv4Address(CurrentIF,
			    ADE_UNICAST,
			    &IPEntry->Addr);
    GetInterfaceIPv4Address(CurrentIF,
			    ADE_ADDRMASK,
			    &IPEntry->Mask);
    GetInterfaceIPv4Address(CurrentIF,
			    ADE_BROADCAST,
			    &IPEntry->BcastAddr);

    TcpipReleaseSpinLock(&EntityListLock, OldIrql);

    InfoCopyOut((PCHAR)IPEntry, sizeof(IPADDR_ENTRY),
		Buffer, BufferSize);

    ExFreePoolWithTag(IPEntry, IP_ADDRESS_TAG);

    return TDI_SUCCESS;
}

TDI_STATUS InfoTdiQueryGetIPSnmpInfo( TDIEntityID ID,
                                      PIP_INTERFACE IF,
				      PNDIS_BUFFER Buffer,
				      PUINT BufferSize ) {
    IPSNMPInfo SnmpInfo;
    UINT IfCount = CountInterfaces();
    UINT RouteCount = CountFIBs(IF);
    TDI_STATUS Status = TDI_INVALID_REQUEST;

    TI_DbgPrint(DEBUG_INFO, ("Called.\n"));

    RtlZeroMemory(&SnmpInfo, sizeof(SnmpInfo));

    SnmpInfo.ipsi_numif = IfCount;
    SnmpInfo.ipsi_numaddr = 1;
    SnmpInfo.ipsi_numroutes = RouteCount;

    Status = InfoCopyOut( (PCHAR)&SnmpInfo, sizeof(SnmpInfo),
			  Buffer, BufferSize );

    TI_DbgPrint(DEBUG_INFO, ("Returning %08x\n", Status));

    return Status;
}

TDI_STATUS InfoTdiQueryGetConnectionTcpTable(PADDRESS_FILE AddrFile,
				    PNDIS_BUFFER Buffer,
				    PUINT BufferSize,
                    TDI_TCPUDP_CLASS_INFO Class)
{
    SIZE_T Size;
    MIB_TCPROW_OWNER_MODULE TcpRow;
    TDI_STATUS Status = TDI_INVALID_REQUEST;

    TI_DbgPrint(DEBUG_INFO, ("Called.\n"));

    if (Class == TcpUdpClassOwnerPid)
    {
        Size = sizeof(MIB_TCPROW_OWNER_PID);
    }
    else if (Class == TcpUdpClassOwner)
    {
        Size = sizeof(MIB_TCPROW_OWNER_MODULE);
    }
    else
    {
        Size = sizeof(MIB_TCPROW);
    }

    TcpRow.dwOwningPid = HandleToUlong(AddrFile->ProcessId);
    TcpRow.liCreateTimestamp = AddrFile->CreationTime;

    if (AddrFile->Listener != NULL)
    {
        PADDRESS_FILE EndPoint;

        EndPoint = AddrFile->Listener->AddressFile;

        TcpRow.dwState = MIB_TCP_STATE_LISTEN;
        TcpRow.dwLocalAddr = AddrFile->Address.Address.IPv4Address;
        TcpRow.dwLocalPort = AddrFile->Port;
        TcpRow.dwRemoteAddr = EndPoint->Address.Address.IPv4Address;
        TcpRow.dwRemotePort = EndPoint->Port;

        Status = TDI_SUCCESS;
    }
    else if (AddrFile->Connection != NULL &&
             AddrFile->Connection->SocketContext != NULL)
    {
        TA_IP_ADDRESS EndPoint;

        Status = TCPGetSockAddress(AddrFile->Connection, (PTRANSPORT_ADDRESS)&EndPoint, sizeof(EndPoint), FALSE);
        if (NT_SUCCESS(Status))
        {
            ASSERT(EndPoint.TAAddressCount >= 1);
            ASSERT(EndPoint.Address[0].AddressLength == TDI_ADDRESS_LENGTH_IP);
            TcpRow.dwLocalAddr = EndPoint.Address[0].Address[0].in_addr;
            TcpRow.dwLocalPort = EndPoint.Address[0].Address[0].sin_port;

            Status = TCPGetSockAddress(AddrFile->Connection, (PTRANSPORT_ADDRESS)&EndPoint, sizeof(EndPoint), TRUE);
            if (NT_SUCCESS(Status))
            {
                ASSERT(EndPoint.TAAddressCount >= 1);
                ASSERT(EndPoint.Address[0].AddressLength == TDI_ADDRESS_LENGTH_IP);
                TcpRow.dwRemoteAddr = EndPoint.Address[0].Address[0].in_addr;
                TcpRow.dwRemotePort = EndPoint.Address[0].Address[0].sin_port;

                Status = TCPGetSocketStatus(AddrFile->Connection, &TcpRow.dwState);
                ASSERT(NT_SUCCESS(Status));
            }
        }
    }

    if (NT_SUCCESS(Status))
    {
        if (Class == TcpUdpClassOwner)
        {
            RtlZeroMemory(&TcpRow.OwningModuleInfo[0], sizeof(TcpRow.OwningModuleInfo));
            TcpRow.OwningModuleInfo[0] = (ULONG_PTR)AddrFile->SubProcessTag;
        }

        Status = InfoCopyOut( (PCHAR)&TcpRow, Size,
                              Buffer, BufferSize );
    }

    TI_DbgPrint(DEBUG_INFO, ("Returning %08x\n", Status));

    return Status;
}

TDI_STATUS InfoTdiSetConnectionTcp(PADDRESS_FILE AddrFile,
                                   PVOID Buffer,
                                   UINT BufferSize)
{
    PMIB_TCPROW Row = Buffer;
    PCONNECTION_ENDPOINT Connection;
    TA_IP_ADDRESS Local, Remote;
    TDI_STATUS Status = TDI_INVALID_REQUEST;

    if (!Buffer || BufferSize < sizeof(MIB_TCPROW))
        return TDI_INVALID_PARAMETER;
    if (Row->dwState != MIB_TCP_STATE_DELETE_TCB)
        return TDI_INVALID_PARAMETER;

    LockObject(AddrFile);
    Connection = AddrFile->Connection;
    if (Connection)
        ReferenceObject(Connection);
    UnlockObject(AddrFile);

    if (!Connection)
        return TDI_INVALID_REQUEST;

    if (Connection->SocketContext &&
        NT_SUCCESS(TCPGetSockAddress(Connection, (PTRANSPORT_ADDRESS)&Local, sizeof(Local), FALSE)) &&
        NT_SUCCESS(TCPGetSockAddress(Connection, (PTRANSPORT_ADDRESS)&Remote, sizeof(Remote), TRUE)) &&
        Local.Address[0].AddressType == TDI_ADDRESS_TYPE_IP &&
        Remote.Address[0].AddressType == TDI_ADDRESS_TYPE_IP &&
        Local.Address[0].Address[0].in_addr == Row->dwLocalAddr &&
        Local.Address[0].Address[0].sin_port == (USHORT)Row->dwLocalPort &&
        Remote.Address[0].Address[0].in_addr == Row->dwRemoteAddr &&
        Remote.Address[0].Address[0].sin_port == (USHORT)Row->dwRemotePort)
    {
        TCPDisconnect(Connection, TDI_DISCONNECT_ABORT, NULL, NULL, NULL, NULL, NULL);
        Status = TDI_SUCCESS;
    }

    DereferenceObject(Connection);
    return Status;
}

TDI_STATUS InfoTdiQueryGetConnectionUdpTable(PADDRESS_FILE AddrFile,
				    PNDIS_BUFFER Buffer,
				    PUINT BufferSize,
				    TDI_TCPUDP_CLASS_INFO Class)
{
    SIZE_T Size;
    MIB_UDPROW_OWNER_MODULE UdpRow;
    TDI_STATUS Status = TDI_INVALID_REQUEST;

    TI_DbgPrint(DEBUG_INFO, ("Called.\n"));

    if (Class == TcpUdpClassOwnerPid)
    {
        Size = sizeof(MIB_UDPROW_OWNER_PID);
    }
    else if (Class == TcpUdpClassOwner)
    {
        Size = sizeof(MIB_UDPROW_OWNER_MODULE);
    }
    else
    {
        Size = sizeof(MIB_UDPROW);
    }

    UdpRow.dwLocalAddr = AddrFile->Address.Address.IPv4Address;
    UdpRow.dwLocalPort = AddrFile->Port;
    UdpRow.dwOwningPid = HandleToUlong(AddrFile->ProcessId);
    UdpRow.liCreateTimestamp = AddrFile->CreationTime;
    UdpRow.dwFlags = 0; /* FIXME */
    if (Class == TcpUdpClassOwner)
    {
        RtlZeroMemory(&UdpRow.OwningModuleInfo[0], sizeof(UdpRow.OwningModuleInfo));
        UdpRow.OwningModuleInfo[0] = (ULONG_PTR)AddrFile->SubProcessTag;
    }

    Status = InfoCopyOut( (PCHAR)&UdpRow,
			  Size, Buffer, BufferSize );

    TI_DbgPrint(DEBUG_INFO, ("Returning %08x\n", Status));

    return Status;
}

TDI_STATUS InfoTdiSetRoute(PIP_INTERFACE IF, PVOID Buffer, UINT BufferSize)
{
    IP_ADDRESS Address, Netmask, Router;
    PIPROUTE_ENTRY Route = Buffer;

    if (!Buffer || BufferSize < sizeof(IPROUTE_ENTRY))
        return TDI_INVALID_PARAMETER;

    AddrInitIPv4( &Address, Route->Dest );
    AddrInitIPv4( &Netmask, Route->Mask );
    AddrInitIPv4( &Router,  Route->Gw );

    if (IF == Loopback)
    {
        DbgPrint("Failing attempt to add route to loopback adapter\n");
        return TDI_INVALID_PARAMETER;
    }

    if( Route->Type == IP_ROUTE_TYPE_ADD ) { /* Add the route */
        TI_DbgPrint(DEBUG_INFO,("Adding route (%s)\n", A2S(&Address)));
        IF->Metric = Route->Metric1;
	if (!RouterCreateRoute(&Address, &Netmask, &Router, IF))
	    return TDI_NO_RESOURCES;

        return TDI_SUCCESS;
     } else if( Route->Type == IP_ROUTE_TYPE_DEL ) {
	TI_DbgPrint(DEBUG_INFO,("Removing route (%s)\n", A2S(&Address)));
	if (NT_SUCCESS(RouterRemoveRoute( &Address, &Router )))
            return TDI_SUCCESS;
        else
            return TDI_INVALID_PARAMETER;
     }

     return TDI_INVALID_REQUEST;
}

static
VOID
InfoIp6CopyAddress(PCHAR TransportBuffer, PUCHAR Address, PUSHORT Port)
{
    PTA_IP6_ADDRESS Ta6 = (PTA_IP6_ADDRESS)TransportBuffer;

    RtlCopyMemory(Address, Ta6->Address[0].Address[0].sin6_addr, 16);
    *Port = Ta6->Address[0].Address[0].sin6_port;
}

static
NTSTATUS
InfoIp6QueryEndpoints(PUCHAR Buffer, ULONG Length, PULONG Count, PULONG Needed, PIP6_STATISTICS Stats)
{
    PADDRESS_FILE *Files;
    PLIST_ENTRY Entry;
    ULONG Total = 0, Taken = 0, i;
    KIRQL OldIrql;

    TcpipAcquireSpinLock(&AddressFileListLock, &OldIrql);
    for (Entry = AddressFileListHead.Flink; Entry != &AddressFileListHead; Entry = Entry->Flink)
    {
        PADDRESS_FILE AddrFile = CONTAINING_RECORD(Entry, ADDRESS_FILE, ListEntry);

        if (AddrFile->Address.Type == IP_ADDRESS_V6 &&
            (AddrFile->Protocol == IPPROTO_TCP || AddrFile->Protocol == IPPROTO_UDP))
            Total++;
    }
    TcpipReleaseSpinLock(&AddressFileListLock, OldIrql);

    Files = ExAllocatePoolWithTag(NonPagedPool, max(Total, 1) * sizeof(PADDRESS_FILE), ADDR_FILE_TAG);
    if (!Files)
        return STATUS_INSUFFICIENT_RESOURCES;

    TcpipAcquireSpinLock(&AddressFileListLock, &OldIrql);
    for (Entry = AddressFileListHead.Flink; Entry != &AddressFileListHead && Taken < Total; Entry = Entry->Flink)
    {
        PADDRESS_FILE AddrFile = CONTAINING_RECORD(Entry, ADDRESS_FILE, ListEntry);

        if (AddrFile->Address.Type == IP_ADDRESS_V6 &&
            (AddrFile->Protocol == IPPROTO_TCP || AddrFile->Protocol == IPPROTO_UDP))
        {
            ReferenceObject(AddrFile);
            Files[Taken++] = AddrFile;
        }
    }
    TcpipReleaseSpinLock(&AddressFileListLock, OldIrql);

    for (i = 0; i < Taken; i++)
    {
        PADDRESS_FILE AddrFile = Files[i];
        IP6_ENDPOINT_ROW Row;
        BOOLEAN Emit = TRUE;

        RtlZeroMemory(&Row, sizeof(Row));
        Row.Protocol = AddrFile->Protocol;
        RtlCopyMemory(Row.LocalAddress, AddrFile->Address.Address.IPv6Address, sizeof(Row.LocalAddress));
        Row.LocalPort = AddrFile->Port;
        Row.ProcessId = HandleToUlong(AddrFile->ProcessId);
        Row.CreateTime = AddrFile->CreationTime;
        Row.ModuleInfo = (ULONG_PTR)AddrFile->SubProcessTag;

        if (AddrFile->Protocol == IPPROTO_TCP)
        {
            if (AddrFile->Listener)
            {
                Row.State = MIB_TCP_STATE_LISTEN;
            }
            else if (AddrFile->Connection && AddrFile->Connection->SocketContext)
            {
                CHAR Local[sizeof(TA_IP6_ADDRESS)], Remote[sizeof(TA_IP6_ADDRESS)];

                if (NT_SUCCESS(TCPGetSockAddress(AddrFile->Connection, (PTRANSPORT_ADDRESS)Local, sizeof(Local), FALSE)) &&
                    NT_SUCCESS(TCPGetSockAddress(AddrFile->Connection, (PTRANSPORT_ADDRESS)Remote, sizeof(Remote), TRUE)) &&
                    ((PTA_IP6_ADDRESS)Local)->Address[0].AddressType == TDI_ADDRESS_TYPE_IP6 &&
                    NT_SUCCESS(TCPGetSocketStatus(AddrFile->Connection, &Row.State)))
                {
                    InfoIp6CopyAddress(Local, Row.LocalAddress, &Row.LocalPort);
                    InfoIp6CopyAddress(Remote, Row.RemoteAddress, &Row.RemotePort);
                }
                else
                {
                    Emit = FALSE;
                }
            }
            else
            {
                Emit = FALSE;
            }
        }

        if (Emit)
        {
            if (Stats)
            {
                if (Row.Protocol == IPPROTO_UDP)
                {
                    Stats->UdpNumAddresses++;
                }
                else
                {
                    Stats->TcpNumConnections++;
                    if (Row.State == MIB_TCP_STATE_ESTAB || Row.State == MIB_TCP_STATE_CLOSE_WAIT)
                        Stats->TcpCurrentEstablished++;
                }
            }
            else
            {
                ULONG Offset = *Needed;

                if (Buffer && Offset + sizeof(Row) <= Length)
                    RtlCopyMemory(Buffer + Offset, &Row, sizeof(Row));
                *Needed += sizeof(Row);
                (*Count)++;
            }
        }

        DereferenceObject(AddrFile);
    }

    ExFreePoolWithTag(Files, ADDR_FILE_TAG);
    return STATUS_SUCCESS;
}

NTSTATUS
InfoIp6QueryTable(ULONG TableId, PVOID Buffer, ULONG Length, PULONG Information)
{
    PIP6_TABLE_HEADER Header = Buffer;
    ULONG Count = 0, Needed = sizeof(IP6_TABLE_HEADER);
    NTSTATUS Status;

    *Information = 0;
    if (Length < sizeof(IP6_TABLE_HEADER))
        return STATUS_BUFFER_TOO_SMALL;

    switch (TableId)
    {
    case Ip6TableInterfaces:
    case Ip6TableAddresses:
    case Ip6TableRoutes:
    case Ip6TableNeighbors:
        Status = LibIP6QueryTable(TableId, Buffer, Length, &Count, &Needed, NULL);
        break;

    case Ip6TableStatistics:
    {
        IP6_STATISTICS Stats;
        ULONG Ignored, IgnoredNeeded;

        RtlZeroMemory(&Stats, sizeof(Stats));
        Status = LibIP6QueryTable(Ip6TableStatistics, NULL, 0, &Ignored, &IgnoredNeeded, &Stats);
        if (NT_SUCCESS(Status))
            Status = InfoIp6QueryEndpoints(NULL, 0, &Ignored, &IgnoredNeeded, &Stats);
        if (NT_SUCCESS(Status))
        {
            Count = 1;
            Needed = sizeof(IP6_TABLE_HEADER) + sizeof(Stats);
            if (Needed <= Length)
                RtlCopyMemory(Header + 1, &Stats, sizeof(Stats));
        }
        break;
    }

    case Ip6TableEndpoints:
        Status = InfoIp6QueryEndpoints(Buffer, Length, &Count, &Needed, NULL);
        break;

    default:
        return STATUS_INVALID_PARAMETER;
    }

    if (!NT_SUCCESS(Status))
        return Status;

    Header->Count = Count;
    Header->Needed = Needed;

    if (Needed > Length)
    {
        *Information = sizeof(IP6_TABLE_HEADER);
        return STATUS_BUFFER_OVERFLOW;
    }

    *Information = Needed;
    return STATUS_SUCCESS;
}
