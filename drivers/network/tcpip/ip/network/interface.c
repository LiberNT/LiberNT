/*
 * COPYRIGHT:   See COPYING in the top level directory
 * PROJECT:     ReactOS TCP/IP protocol driver
 * FILE:        tcpip/interface.c
 * PURPOSE:     Convenient abstraction for getting and setting information
 *              in IP_INTERFACE.
 * PROGRAMMERS: Art Yerkes
 * REVISIONS:
 *   CSH 01/08-2000 Created
 */

#include "precomp.h"

#include <ntifs.h>
#include <ipifcons.h>

ULONG NextDefaultAdapter = 0;

static PIP_NTE NteFindAddress(
    PIP_INTERFACE Interface,
    IPv4_RAW_ADDRESS Address)
{
    PLIST_ENTRY Entry;

    for (Entry = NetTableListHead.Flink; Entry != &NetTableListHead; Entry = Entry->Flink)
    {
        PIP_NTE Nte = CONTAINING_RECORD(Entry, IP_NTE, ListEntry);

        if ((!Interface || Nte->Interface == Interface) &&
            Nte->Unicast.Address.IPv4Address == Address)
            return Nte;
    }

    return NULL;
}

static PIP_NTE NteFindPrefix(
    PIP_NTE Nte)
{
    IPv4_RAW_ADDRESS Mask = Nte->Netmask.Address.IPv4Address;
    PLIST_ENTRY Entry;

    for (Entry = NetTableListHead.Flink; Entry != &NetTableListHead; Entry = Entry->Flink)
    {
        PIP_NTE Other = CONTAINING_RECORD(Entry, IP_NTE, ListEntry);

        if (Other != Nte && Other->Interface == Nte->Interface &&
            Other->Netmask.Address.IPv4Address == Mask &&
            !((Other->Unicast.Address.IPv4Address ^ Nte->Unicast.Address.IPv4Address) & Mask))
            return Other;
    }

    return NULL;
}

static BOOLEAN NteIsSubnetBroadcast(
    IPv4_RAW_ADDRESS Address)
{
    PLIST_ENTRY Entry;

    for (Entry = NetTableListHead.Flink; Entry != &NetTableListHead; Entry = Entry->Flink)
    {
        PIP_NTE Nte = CONTAINING_RECORD(Entry, IP_NTE, ListEntry);

        if (Nte->Broadcast.Address.IPv4Address == Address)
            return TRUE;
    }

    return FALSE;
}

static VOID NteRefreshInterface(
    PIP_INTERFACE Interface)
{
    PLIST_ENTRY Entry;

    for (Entry = NetTableListHead.Flink; Entry != &NetTableListHead; Entry = Entry->Flink)
    {
        PIP_NTE Nte = CONTAINING_RECORD(Entry, IP_NTE, ListEntry);

        if (Nte->Interface == Interface)
        {
            Interface->Netmask = Nte->Netmask;
            Interface->Broadcast = Nte->Broadcast;
            Interface->Unicast = Nte->Unicast;
            return;
        }
    }

    AddrInitIPv4(&Interface->Unicast, 0);
    AddrInitIPv4(&Interface->Netmask, 0);
    AddrInitIPv4(&Interface->Broadcast, 0xFFFFFFFF);
}

NTSTATUS GetInterfaceIPv4Address( PIP_INTERFACE Interface,
				  ULONG TargetType,
				  PULONG Address ) {
    switch( TargetType ) {
    case ADE_UNICAST:
	*Address = Interface->Unicast.Address.IPv4Address;
	break;

    case ADE_ADDRMASK:
	*Address = Interface->Netmask.Address.IPv4Address;
	break;

    case ADE_BROADCAST:
	*Address = Interface->Broadcast.Address.IPv4Address;
	break;

    case ADE_POINTOPOINT:
	*Address = Interface->PointToPoint.Address.IPv4Address;
	break;

    default:
	return STATUS_UNSUCCESSFUL;
    }

    return STATUS_SUCCESS;
}

UINT CountInterfaces() {
    ULONG Count = 0;
    KIRQL OldIrql;
    IF_LIST_ITER(CurrentIF);

    TcpipAcquireSpinLock(&InterfaceListLock, &OldIrql);

    ForEachInterface(CurrentIF) {
	Count++;
    } EndFor(CurrentIF);

    TcpipReleaseSpinLock(&InterfaceListLock, OldIrql);

    return Count;
}

NTSTATUS GetInterfaceSpeed( PIP_INTERFACE Interface, PUINT Speed ) {
    PLAN_ADAPTER IF = (PLAN_ADAPTER)Interface->Context;

    *Speed = IF->Speed;

    return STATUS_SUCCESS;
}

NTSTATUS GetInterfaceName( PIP_INTERFACE Interface,
			   PCHAR NameBuffer,
			   UINT Len ) {
    ULONG ResultSize = 0;
    NTSTATUS Status =
	RtlUnicodeToMultiByteN( NameBuffer,
				Len,
				&ResultSize,
				Interface->Name.Buffer,
				Interface->Name.Length );

    if( NT_SUCCESS(Status) )
	NameBuffer[ResultSize] = 0;
    else
	NameBuffer[0] = 0;

    return Status;
}

PIP_INTERFACE GetMulticastInterface(ULONG Selector)
{
    ULONG HostSelector = DN2H(Selector);
    PIP_INTERFACE Interface = NULL;
    KIRQL OldIrql;
    PIP_NTE Nte;
    IF_LIST_ITER(CurrentIF);

    if (!Selector) return GetDefaultInterface();
    if (HostSelector >= 0x01000000) {
        TcpipAcquireSpinLock(&NetTableListLock, &OldIrql);
        Nte = NteFindAddress(NULL, Selector);
        if (Nte && IPReferenceInterface(Nte->Interface)) Interface = Nte->Interface;
        TcpipReleaseSpinLock(&NetTableListLock, OldIrql);
        return Interface;
    }
    TcpipAcquireSpinLock(&InterfaceListLock, &OldIrql);
    ForEachInterface(CurrentIF) {
        if (CurrentIF->Index == HostSelector) {
            if (IPReferenceInterface(CurrentIF)) Interface = CurrentIF;
            break;
        }
    } EndFor(CurrentIF);
    TcpipReleaseSpinLock(&InterfaceListLock, OldIrql);
    return Interface;
}

PIP_INTERFACE AddrLocateInterface(
    PIP_ADDRESS MatchAddress)
{
    KIRQL OldIrql;
    PIP_INTERFACE RetIF = NULL;
    PLIST_ENTRY Entry;
    IF_LIST_ITER(CurrentIF);

    TcpipAcquireSpinLock(&NetTableListLock, &OldIrql);

    for (Entry = NetTableListHead.Flink; Entry != &NetTableListHead; Entry = Entry->Flink) {
        PIP_NTE Nte = CONTAINING_RECORD(Entry, IP_NTE, ListEntry);

        if (AddrIsEqual(&Nte->Unicast, MatchAddress) ||
            AddrIsEqual(&Nte->Broadcast, MatchAddress)) {
            if (IPReferenceInterface(Nte->Interface))
                RetIF = Nte->Interface;
            break;
        }
    }

    TcpipReleaseSpinLock(&NetTableListLock, OldIrql);

    if (Entry != &NetTableListHead)
        return RetIF;

    TcpipAcquireSpinLock(&InterfaceListLock, &OldIrql);

    ForEachInterface(CurrentIF) {
	if( AddrIsEqual( &CurrentIF->Unicast, MatchAddress ) ||
            AddrIsEqual( &CurrentIF->Broadcast, MatchAddress ) ) {
            if (IPReferenceInterface(CurrentIF))
                RetIF = CurrentIF;
            break;
	}
    } EndFor(CurrentIF);

    TcpipReleaseSpinLock(&InterfaceListLock, OldIrql);

    if ((RetIF == NULL) && (MatchAddress->Type == IP_ADDRESS_V4) &&
        ((MatchAddress->Address.IPv4Address & 0xFF) == 127) &&
        (Loopback != NULL) && IPReferenceInterface(Loopback))
    {
        RetIF = Loopback;
    }

    return RetIF;
}

BOOLEAN HasPrefix(
    PIP_ADDRESS Address,
    PIP_ADDRESS Prefix,
    UINT Length)
/*
 * FUNCTION: Determines wether an address has an given prefix
 * ARGUMENTS:
 *     Address = Pointer to address to use
 *     Prefix  = Pointer to prefix to check for
 *     Length  = Length of prefix
 * RETURNS:
 *     TRUE if the address has the prefix, FALSE if not
 * NOTES:
 *     The two addresses must be of the same type
 */
{
    PUCHAR pAddress = (PUCHAR)&Address->Address;
    PUCHAR pPrefix  = (PUCHAR)&Prefix->Address;

    TI_DbgPrint(DEBUG_ROUTER, ("Called. Address (0x%X)  Prefix (0x%X)  Length (%d).\n", Address, Prefix, Length));

#if 0
    TI_DbgPrint(DEBUG_ROUTER, ("Address (%s)  Prefix (%s).\n",
        A2S(Address), A2S(Prefix)));
#endif

    /* Don't report matches for empty prefixes */
    if (Length == 0) {
        return FALSE;
    }

    /* Check that initial integral bytes match */
    while (Length > 8) {
        if (*pAddress++ != *pPrefix++)
            return FALSE;
        Length -= 8;
    }

    /* Check any remaining bits */
    if ((Length > 0) && ((*pAddress >> (8 - Length)) != (*pPrefix >> (8 - Length))))
        return FALSE;

    return TRUE;
}

PIP_INTERFACE GetDefaultInterface(VOID)
{
   KIRQL OldIrql;
   ULONG Index = 0;
   ULONG IfStatus;
   PIP_INTERFACE SelectedIF = NULL;

   IF_LIST_ITER(CurrentIF);

   TcpipAcquireSpinLock(&InterfaceListLock, &OldIrql);
   /* DHCP hack: Always return the adapter without an IP address */
   ForEachInterface(CurrentIF) {
      if (CurrentIF->Context && AddrIsUnspecified(&CurrentIF->Unicast)) {
          GetInterfaceConnectionStatus(CurrentIF, &IfStatus);
          if (IfStatus == MIB_IF_OPER_STATUS_OPERATIONAL && IPReferenceInterface(CurrentIF)) {
              SelectedIF = CurrentIF;
              goto Exit;
          }
      }
   } EndFor(CurrentIF);

   /* Try to continue from the next adapter */
   ForEachInterface(CurrentIF) {
      if (CurrentIF->Context && (Index++ == NextDefaultAdapter)) {
          GetInterfaceConnectionStatus(CurrentIF, &IfStatus);
          if (IfStatus == MIB_IF_OPER_STATUS_OPERATIONAL && IPReferenceInterface(CurrentIF)) {
              NextDefaultAdapter++;
              SelectedIF = CurrentIF;
              goto Exit;
          }
      }
   } EndFor(CurrentIF);

   /* No luck, so we'll choose the first adapter this time */
   Index = 0;
   ForEachInterface(CurrentIF) {
      if (CurrentIF->Context) {
          Index++;
          GetInterfaceConnectionStatus(CurrentIF, &IfStatus);
          if (IfStatus == MIB_IF_OPER_STATUS_OPERATIONAL && IPReferenceInterface(CurrentIF)) {
              NextDefaultAdapter = Index;
              SelectedIF = CurrentIF;
              goto Exit;
          }
      }
   } EndFor(CurrentIF);

   /* Even that didn't work, so we'll just go with loopback */
   NextDefaultAdapter = 0;

   /* There are no physical interfaces on the system
    * so we must pick the loopback interface */
   if (Loopback && IPReferenceInterface(Loopback))
       SelectedIF = Loopback;

Exit:
   TcpipReleaseSpinLock(&InterfaceListLock, OldIrql);
   return SelectedIF;
}

BOOLEAN
HasLoopbackPrefix(
    PIP_ADDRESS Address,
    PIP_ADDRESS Prefix)
{
    if (((Address->Address.IPv4Address & LOOPBACK_ADDRMASK_IPv4) == (LOOPBACK_ADDRESS_IPv4 & LOOPBACK_ADDRMASK_IPv4)) && 
        (Prefix->Address.IPv4Address == LOOPBACK_ADDRESS_IPv4))
        return TRUE;

    return FALSE;
}

PIP_INTERFACE FindOnLinkInterface(PIP_ADDRESS Address)
/*
 * FUNCTION: Checks all on-link prefixes to find out if an address is on-link
 * ARGUMENTS:
 *     Address = Pointer to address to check
 * RETURNS:
 *     Pointer to interface if address is on-link, NULL if not
 */
{
    KIRQL OldIrql;
    PIP_INTERFACE Interface = NULL;
    PLIST_ENTRY Entry;

    TI_DbgPrint(DEBUG_ROUTER, ("Called. Address (0x%X)\n", Address));
    TI_DbgPrint(DEBUG_ROUTER, ("Address (%s)\n", A2S(Address)));

    if (AddrIsUnspecified(Address))
        return GetDefaultInterface();

    TcpipAcquireSpinLock(&NetTableListLock, &OldIrql);

    for (Entry = NetTableListHead.Flink; Entry != &NetTableListHead; Entry = Entry->Flink) {
        PIP_NTE Nte = CONTAINING_RECORD(Entry, IP_NTE, ListEntry);

        if (HasLoopbackPrefix(Address, &Nte->Unicast) ||
            HasPrefix(Address, &Nte->Unicast, AddrCountPrefixBits(&Nte->Netmask)))
        {
            /* A disconnected interface keeps its address, but its subnet is
             * not reachable until the media connects again. */
            if (!IsInterfaceOperational(Nte->Interface) || !IPReferenceInterface(Nte->Interface))
                continue;
            Interface = Nte->Interface;
            break;
        }
    }

    TcpipReleaseSpinLock(&NetTableListLock, OldIrql);

    return Interface;
}

VOID GetInterfaceConnectionStatus(PIP_INTERFACE Interface, PULONG Result)
{
    PLAN_ADAPTER Adapter = Interface->Context;

    /* Loopback has no adapter context */
    if (Adapter == NULL || Adapter->State == LAN_STATE_STARTED) {
        *Result = MIB_IF_OPER_STATUS_OPERATIONAL;
    }
    else {
        *Result = MIB_IF_OPER_STATUS_DISCONNECTED;
    }
}

BOOLEAN IsInterfaceOperational(PIP_INTERFACE Interface)
{
    ULONG OperStatus;

    GetInterfaceConnectionStatus(Interface, &OperStatus);
    return OperStatus == MIB_IF_OPER_STATUS_OPERATIONAL;
}

UINT CountInterfaceAddresses(PIP_INTERFACE Interface)
{
    PLIST_ENTRY Entry;
    KIRQL OldIrql;
    UINT Count = 0;

    TcpipAcquireSpinLock(&NetTableListLock, &OldIrql);

    for (Entry = NetTableListHead.Flink; Entry != &NetTableListHead; Entry = Entry->Flink)
    {
        if (CONTAINING_RECORD(Entry, IP_NTE, ListEntry)->Interface == Interface)
            Count++;
    }

    TcpipReleaseSpinLock(&NetTableListLock, OldIrql);

    return Count;
}

UINT CopyInterfaceAddresses(PIP_INTERFACE Interface, PIPADDR_ENTRY Entries, UINT Count)
{
    PLIST_ENTRY Entry;
    KIRQL OldIrql;
    UINT Total = 0;

    TcpipAcquireSpinLock(&NetTableListLock, &OldIrql);

    for (Entry = NetTableListHead.Flink; Entry != &NetTableListHead; Entry = Entry->Flink)
    {
        PIP_NTE Nte = CONTAINING_RECORD(Entry, IP_NTE, ListEntry);

        if (Nte->Interface != Interface)
            continue;

        if (Total < Count)
        {
            UINT i = Total;

            while (i > 0 && DN2H(Entries[i - 1].Addr) > DN2H(Nte->Unicast.Address.IPv4Address))
            {
                Entries[i] = Entries[i - 1];
                i--;
            }

            RtlZeroMemory(&Entries[i], sizeof(Entries[i]));
            Entries[i].Addr = Nte->Unicast.Address.IPv4Address;
            Entries[i].Index = Interface->Index;
            Entries[i].Mask = Nte->Netmask.Address.IPv4Address;
            Entries[i].BcastAddr = Nte->Broadcast.Address.IPv4Address;
        }
        Total++;
    }

    TcpipReleaseSpinLock(&NetTableListLock, OldIrql);

    return Total;
}

BOOLEAN IsInterfaceAddress(PIP_INTERFACE Interface, PIP_ADDRESS Address)
{
    KIRQL OldIrql;
    BOOLEAN Found;

    if (Address->Type != IP_ADDRESS_V4)
        return FALSE;

    TcpipAcquireSpinLock(&NetTableListLock, &OldIrql);
    Found = NteFindAddress(Interface, Address->Address.IPv4Address) != NULL;
    TcpipReleaseSpinLock(&NetTableListLock, OldIrql);

    return Found;
}

BOOLEAN IsInterfaceBroadcast(PIP_INTERFACE Interface, PIP_ADDRESS Address)
{
    PLIST_ENTRY Entry;
    KIRQL OldIrql;
    BOOLEAN Found = FALSE;

    if (Address->Type != IP_ADDRESS_V4)
        return FALSE;

    if (Address->Address.IPv4Address == 0xFFFFFFFF)
        return TRUE;

    TcpipAcquireSpinLock(&NetTableListLock, &OldIrql);

    for (Entry = NetTableListHead.Flink; Entry != &NetTableListHead; Entry = Entry->Flink)
    {
        PIP_NTE Nte = CONTAINING_RECORD(Entry, IP_NTE, ListEntry);

        if (Nte->Interface == Interface && AddrIsEqual(&Nte->Broadcast, Address))
        {
            Found = TRUE;
            break;
        }
    }

    TcpipReleaseSpinLock(&NetTableListLock, OldIrql);

    return Found;
}

BOOLEAN IsInterfaceOnLink(PIP_INTERFACE Interface, PIP_ADDRESS Address)
{
    PLIST_ENTRY Entry;
    KIRQL OldIrql;
    BOOLEAN Found = FALSE;

    if (Address->Type != IP_ADDRESS_V4)
        return FALSE;

    TcpipAcquireSpinLock(&NetTableListLock, &OldIrql);

    for (Entry = NetTableListHead.Flink; Entry != &NetTableListHead; Entry = Entry->Flink)
    {
        PIP_NTE Nte = CONTAINING_RECORD(Entry, IP_NTE, ListEntry);

        if (Nte->Interface == Interface &&
            HasPrefix(Address, &Nte->Unicast, AddrCountPrefixBits(&Nte->Netmask)))
        {
            Found = TRUE;
            break;
        }
    }

    TcpipReleaseSpinLock(&NetTableListLock, OldIrql);

    return Found;
}

static UINT NteMatchLength(IPv4_RAW_ADDRESS Address1, IPv4_RAW_ADDRESS Address2)
{
    ULONG Difference = DN2H(Address1) ^ DN2H(Address2);
    UINT Length = 0;

    while (Length < 32 && !(Difference & (0x80000000 >> Length)))
        Length++;

    return Length;
}

BOOLEAN GetInterfaceSourceAddress(PIP_INTERFACE Interface, PIP_ADDRESS Destination,
                                  PIP_ADDRESS NextHop, PIP_ADDRESS Source)
{
    BOOLEAN OffLink, OnLink, BestOnLink = FALSE;
    UINT Match, BestMatch = 0;
    PIP_NTE Best = NULL;
    PLIST_ENTRY Entry;
    KIRQL OldIrql;

    OffLink = NextHop && NextHop->Type == IP_ADDRESS_V4 &&
              !AddrIsUnspecified(NextHop) && !AddrIsEqual(NextHop, Destination);

    TcpipAcquireSpinLock(&NetTableListLock, &OldIrql);

    for (Entry = NetTableListHead.Flink; Entry != &NetTableListHead; Entry = Entry->Flink)
    {
        PIP_NTE Nte = CONTAINING_RECORD(Entry, IP_NTE, ListEntry);

        if (Nte->Interface != Interface)
            continue;

        if (AddrIsEqual(&Nte->Unicast, Destination))
        {
            Best = Nte;
            break;
        }

        OnLink = HasPrefix(Destination, &Nte->Unicast, AddrCountPrefixBits(&Nte->Netmask));
        Match = OffLink ? NteMatchLength(Nte->Unicast.Address.IPv4Address, NextHop->Address.IPv4Address) : 0;

        if (!Best || (OnLink && !BestOnLink) || (OnLink == BestOnLink && Match > BestMatch))
        {
            Best = Nte;
            BestOnLink = OnLink;
            BestMatch = Match;
        }
    }

    if (Best)
        *Source = Best->Unicast;
    else
        *Source = Interface->Unicast;

    TcpipReleaseSpinLock(&NetTableListLock, OldIrql);

    return Best != NULL;
}

static NTSTATUS NteValidate(PIP_INTERFACE Interface, IPv4_RAW_ADDRESS Address, IPv4_RAW_ADDRESS Netmask)
{
    ULONG HostAddress = DN2H(Address);
    ULONG HostMask = DN2H(Netmask);

    if (~HostMask & (~HostMask + 1))
        return STATUS_INVALID_PARAMETER;

    if (HostAddress == 0 || HostAddress >= 0xE0000000)
        return STATUS_INVALID_PARAMETER;

    if ((HostAddress >> 24) == 127 && Interface != Loopback)
        return STATUS_INVALID_PARAMETER;

    return STATUS_SUCCESS;
}

static VOID NteRelease(PIP_NTE Nte, PIP_NTE Successor)
{
    PIP_INTERFACE Interface = Nte->Interface;
    PNEIGHBOR_CACHE_ENTRY NCE;
    IP_ADDRESS Network;

    TCPRemoveInterfaceAddress(Nte);

    AddrWidenAddress(&Network, &Nte->Unicast, &Nte->Netmask);

    NCE = NBLocateNeighbor(&Nte->Unicast, Interface);
    if (NCE)
    {
        if (Nte->RouteOwner)
            RouterRemoveRoute(&Network, &Nte->Unicast);
        NBRemoveNeighbor(NCE);
        NBDereferenceNeighbor(NCE);
    }

    if (Successor)
    {
        NCE = NBLocateNeighbor(&Successor->Unicast, Interface);
        if (NCE)
        {
            RouterAddRoute(&Network, &Successor->Netmask, NCE, 1);
            NBDereferenceNeighbor(NCE);
        }
    }

    ExFreePoolWithTag(Nte, NTE_TAG);
}

NTSTATUS IPAddInterfaceAddress(PIP_INTERFACE Interface, IPv4_RAW_ADDRESS Address,
                               IPv4_RAW_ADDRESS Netmask, PULONG Context, PULONG Instance)
{
    PNEIGHBOR_CACHE_ENTRY NCE;
    IP_ADDRESS Network;
    NTSTATUS Status;
    KIRQL OldIrql;
    PIP_NTE Nte;

    PAGED_CODE();

    Status = NteValidate(Interface, Address, Netmask);
    if (!NT_SUCCESS(Status))
        return Status;

    Nte = ExAllocatePoolZero(NonPagedPool, sizeof(IP_NTE), NTE_TAG);
    if (!Nte)
        return STATUS_INSUFFICIENT_RESOURCES;

    Nte->Interface = Interface;
    Nte->Context = Address;
    AddrInitIPv4(&Nte->Unicast, Address);
    AddrInitIPv4(&Nte->Netmask, Netmask);
    AddrInitIPv4(&Nte->Broadcast, DN2H(Netmask) >= 0xFFFFFFFE ? 0xFFFFFFFF : (Address | ~Netmask));

    ExAcquireFastMutex(&NetTableMutex);

    TcpipAcquireSpinLock(&NetTableListLock, &OldIrql);
    if (NteFindAddress(NULL, Address) || NteIsSubnetBroadcast(Address))
    {
        Status = STATUS_DUPLICATE_OBJECTID;
    }
    else if (Netmask != 0xFFFFFFFF && (Address & ~Netmask) == ~Netmask)
    {
        Status = STATUS_INVALID_PARAMETER;
    }
    else
    {
        Nte->RouteOwner = !NteFindPrefix(Nte);
        InsertTailList(&NetTableListHead, &Nte->ListEntry);
        NteRefreshInterface(Interface);
    }
    TcpipReleaseSpinLock(&NetTableListLock, OldIrql);

    if (!NT_SUCCESS(Status))
    {
        ExReleaseFastMutex(&NetTableMutex);
        ExFreePoolWithTag(Nte, NTE_TAG);
        return Status;
    }

    NCE = NBAddNeighbor(Interface, &Nte->Unicast, Interface->Address,
                        Interface->AddressLength, NUD_PERMANENT, 0);
    if (NCE)
    {
        if (Nte->RouteOwner)
        {
            AddrWidenAddress(&Network, &Nte->Unicast, &Nte->Netmask);
            if (!RouterAddRoute(&Network, &Nte->Netmask, NCE, 1))
                TI_DbgPrint(MIN_TRACE, ("Could not add route due to insufficient resources.\n"));
        }
        NBDereferenceNeighbor(NCE);
    }
    else
    {
        TI_DbgPrint(MIN_TRACE, ("Could not create NCE.\n"));
    }

    if (Interface != Loopback)
        ARPTransmit(&Nte->Unicast, NULL, Interface);

    TCPAddInterfaceAddress(Nte);

    if (Context)
        *Context = Nte->Context;
    if (Instance)
        *Instance = Nte->Instance;

    ExReleaseFastMutex(&NetTableMutex);

    return STATUS_SUCCESS;
}

NTSTATUS IPDeleteInterfaceAddress(ULONG Context)
{
    PIP_NTE Nte = NULL, Successor = NULL;
    PLIST_ENTRY Entry;
    KIRQL OldIrql;

    PAGED_CODE();

    ExAcquireFastMutex(&NetTableMutex);

    TcpipAcquireSpinLock(&NetTableListLock, &OldIrql);
    for (Entry = NetTableListHead.Flink; Entry != &NetTableListHead; Entry = Entry->Flink)
    {
        PIP_NTE Current = CONTAINING_RECORD(Entry, IP_NTE, ListEntry);

        if (Current->Context == Context)
        {
            Nte = Current;
            break;
        }
    }
    if (Nte)
    {
        RemoveEntryList(&Nte->ListEntry);
        NteRefreshInterface(Nte->Interface);
        if (Nte->RouteOwner && (Successor = NteFindPrefix(Nte)))
            Successor->RouteOwner = TRUE;
    }
    TcpipReleaseSpinLock(&NetTableListLock, OldIrql);

    if (Nte)
        NteRelease(Nte, Successor);

    ExReleaseFastMutex(&NetTableMutex);

    return Nte ? STATUS_SUCCESS : STATUS_NOT_FOUND;
}

VOID IPDeleteInterfaceAddresses(PIP_INTERFACE Interface)
{
    PLIST_ENTRY Entry;
    KIRQL OldIrql;
    PIP_NTE Nte;

    PAGED_CODE();

    ExAcquireFastMutex(&NetTableMutex);

    for (;;)
    {
        Nte = NULL;

        TcpipAcquireSpinLock(&NetTableListLock, &OldIrql);
        for (Entry = NetTableListHead.Flink; Entry != &NetTableListHead; Entry = Entry->Flink)
        {
            PIP_NTE Current = CONTAINING_RECORD(Entry, IP_NTE, ListEntry);

            if (Current->Interface == Interface)
            {
                Nte = Current;
                RemoveEntryList(&Nte->ListEntry);
                break;
            }
        }
        NteRefreshInterface(Interface);
        TcpipReleaseSpinLock(&NetTableListLock, OldIrql);

        if (!Nte)
            break;

        NteRelease(Nte, NULL);
    }

    ExReleaseFastMutex(&NetTableMutex);
}
