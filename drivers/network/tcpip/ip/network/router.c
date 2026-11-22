/*
 * COPYRIGHT:   See COPYING in the top level directory
 * PROJECT:     ReactOS TCP/IP protocol driver
 * FILE:        network/router.c
 * PURPOSE:     IP routing subsystem
 * PROGRAMMERS: Casper S. Hornstrup (chorns@users.sourceforge.net)
 * NOTES:
 *   This file holds authoritative routing information.
 *   Information queries on the route table should be handled here.
 *   This information should always override the route cache info.
 * REVISIONS:
 *   CSH 01/08-2000 Created
 */

#include "precomp.h"

#include <nldef.h>
#include <tcpip_undoc.h>

LIST_ENTRY FIBListHead;
KSPIN_LOCK FIBLock;

UINT
IPAutomaticMetric(
    _In_ PIP_INTERFACE Interface)
{
    static const struct { ULONG64 Speed; UINT Metric; } Wired[] =
    {
        {100000000000ULL, 5}, {40000000000ULL, 10}, {10000000000ULL, 15}, {2000000000ULL, 20},
        {200000000, 25}, {80000000, 35}, {20000000, 45}, {4000000, 55}, {500000, 65}
    }, Wireless[] =
    {
        {2000000000ULL, 25}, {500000000, 30}, {200000000, 35}, {150000000, 40}, {80000000, 45}, {50000000, 50},
        {20000000, 55}, {10000000, 60}, {4000000, 65}, {2000000, 70}, {500000, 75}, {200000, 80}
    };
    ULONG64 Speed = Interface->TransmitLinkSpeed;
    UINT i;

    if (Interface == Loopback)
        return 75;
    if (Interface->Wireless || Interface->HalfDuplex)
        Speed /= 2;
    if (Interface->Wireless)
    {
        for (i = 0; i < RTL_NUMBER_OF(Wireless); i++)
        {
            if (Speed >= Wireless[i].Speed)
                return Wireless[i].Metric;
        }
        return 85;
    }
    for (i = 0; i < RTL_NUMBER_OF(Wired); i++)
    {
        if (Speed >= Wired[i].Speed)
            return Wired[i].Metric;
    }
    return 75;
}

UINT
IPInterfaceMetric(
    _In_ PIP_INTERFACE Interface)
{
    return Interface->ManualMetric ? Interface->InterfaceMetric : IPAutomaticMetric(Interface);
}


VOID
RouterDumpRoutes(VOID)
{
    PLIST_ENTRY CurrentEntry;
    PLIST_ENTRY NextEntry;
    PFIB_ENTRY Current;
    PNEIGHBOR_CACHE_ENTRY NCE;

    TI_DbgPrint(DEBUG_ROUTER,("Dumping Routes\n"));

    CurrentEntry = FIBListHead.Flink;
    while (CurrentEntry != &FIBListHead) {
        NextEntry = CurrentEntry->Flink;
	Current = CONTAINING_RECORD(CurrentEntry, FIB_ENTRY, ListEntry);

        NCE   = Current->Router;

	TI_DbgPrint(DEBUG_ROUTER,("Examining FIBE %x\n", Current));
	TI_DbgPrint(DEBUG_ROUTER,("... NetworkAddress %s\n", A2S(&Current->NetworkAddress)));
	TI_DbgPrint(DEBUG_ROUTER,("... NCE->Address . %s\n", A2S(&NCE->Address)));

	CurrentEntry = NextEntry;
    }

    TI_DbgPrint(DEBUG_ROUTER,("Dumping Routes ... Done\n"));
}

VOID FreeFIB(
    PVOID Object)
/*
 * FUNCTION: Frees an forward information base object
 * ARGUMENTS:
 *     Object = Pointer to an forward information base structure
 */
{
    ExFreePoolWithTag(Object, FIB_TAG);
}


VOID DestroyFIBE(
    PFIB_ENTRY FIBE)
/*
 * FUNCTION: Destroys an forward information base entry
 * ARGUMENTS:
 *     FIBE = Pointer to FIB entry
 * NOTES:
 *     The forward information base lock must be held when called
 */
{
    TI_DbgPrint(DEBUG_ROUTER, ("Called. FIBE (0x%X).\n", FIBE));

    /* Unlink the FIB entry from the list */
    RemoveEntryList(&FIBE->ListEntry);

    NBDereferenceNeighbor(FIBE->Router);

    /* And free the FIB entry */
    FreeFIB(FIBE);
}


VOID DestroyFIBEs(
    VOID)
/*
 * FUNCTION: Destroys all forward information base entries
 * NOTES:
 *     The forward information base lock must be held when called
 */
{
    PLIST_ENTRY CurrentEntry;
    PLIST_ENTRY NextEntry;
    PFIB_ENTRY Current;

    /* Search the list and remove every FIB entry we find */
    CurrentEntry = FIBListHead.Flink;
    while (CurrentEntry != &FIBListHead) {
        NextEntry = CurrentEntry->Flink;
	Current = CONTAINING_RECORD(CurrentEntry, FIB_ENTRY, ListEntry);
        /* Destroy the FIB entry */
        DestroyFIBE(Current);
        CurrentEntry = NextEntry;
    }
}


UINT CountFIBs(PIP_INTERFACE IF) {
    UINT FibCount = 0;
    KIRQL OldIrql;
    PLIST_ENTRY CurrentEntry;
    PLIST_ENTRY NextEntry;
    PFIB_ENTRY Current;

    TcpipAcquireSpinLock(&FIBLock, &OldIrql);

    CurrentEntry = FIBListHead.Flink;
    while (CurrentEntry != &FIBListHead) {
        NextEntry = CurrentEntry->Flink;
        Current = CONTAINING_RECORD(CurrentEntry, FIB_ENTRY, ListEntry);
        if (Current->Router->Interface == IF)
	    FibCount++;
        CurrentEntry = NextEntry;
    }

    TcpipReleaseSpinLock(&FIBLock, OldIrql);

    return FibCount;
}


UINT CopyFIBs( PIP_INTERFACE IF, PFIB_ENTRY Target, UINT Capacity ) {
    UINT FibCount = 0;
    KIRQL OldIrql;
    PLIST_ENTRY CurrentEntry;
    PLIST_ENTRY NextEntry;
    PFIB_ENTRY Current;

    TcpipAcquireSpinLock(&FIBLock, &OldIrql);

    CurrentEntry = FIBListHead.Flink;
    while (CurrentEntry != &FIBListHead) {
        NextEntry = CurrentEntry->Flink;
	Current = CONTAINING_RECORD(CurrentEntry, FIB_ENTRY, ListEntry);
        if (Current->Router->Interface == IF)
        {
		    if (FibCount < Capacity)
		    {
		        Target[FibCount] = *Current;
		        NBReferenceNeighbor(Target[FibCount].Router);
		    }
		    FibCount++;
        }
        CurrentEntry = NextEntry;
    }

    TcpipReleaseSpinLock(&FIBLock, OldIrql);

    return FibCount;
}


UINT CommonPrefixLength(
    PIP_ADDRESS Address1,
    PIP_ADDRESS Address2)
/*
 * FUNCTION: Computes the length of the longest prefix common to two addresses
 * ARGUMENTS:
 *     Address1 = Pointer to first address
 *     Address2 = Pointer to second address
 * NOTES:
 *     The two addresses must be of the same type
 * RETURNS:
 *     Length of longest common prefix
 */
{
    PUCHAR Addr1, Addr2;
    UINT Size;
    UINT i, j;
    UINT Bitmask;

    TI_DbgPrint(DEBUG_ROUTER, ("Called. Address1 (0x%X)  Address2 (0x%X).\n", Address1, Address2));

    /*TI_DbgPrint(DEBUG_ROUTER, ("Target  (%s) \n", A2S(Address1)));*/
    /*TI_DbgPrint(DEBUG_ROUTER, ("Adapter (%s).\n", A2S(Address2)));*/

    if (Address1->Type == IP_ADDRESS_V4)
        Size = sizeof(IPv4_RAW_ADDRESS);
    else
        Size = sizeof(IPv6_RAW_ADDRESS);

    Addr1 = (PUCHAR)&Address1->Address.IPv4Address;
    Addr2 = (PUCHAR)&Address2->Address.IPv4Address;

    /* Find first non-matching byte */
    for (i = 0; i < Size && Addr1[i] == Addr2[i]; i++);
    if( i == Size ) return 8 * i;

    /* Find first non-matching bit */
    Bitmask = 0x80;
    for (j = 0; (Addr1[i] & Bitmask) == (Addr2[i] & Bitmask); j++)
        Bitmask >>= 1;

    TI_DbgPrint(DEBUG_ROUTER, ("Returning %d\n", 8 * i + j));

    return 8 * i + j;
}


PFIB_ENTRY RouterAddRoute(
    PIP_ADDRESS NetworkAddress,
    PIP_ADDRESS Netmask,
    PNEIGHBOR_CACHE_ENTRY Router,
    UINT Metric)
/*
 * FUNCTION: Adds a route to the Forward Information Base (FIB)
 * ARGUMENTS:
 *     NetworkAddress = Pointer to address of network
 *     Netmask        = Pointer to netmask of network
 *     Router         = Pointer to NCE of router to use
 *     Metric         = Cost of this route
 * RETURNS:
 *     Pointer to FIB entry if the route was added, NULL if not
 * NOTES:
 *     The FIB entry references the NetworkAddress, Netmask and
 *     the NCE of the router. The caller is responsible for providing
 *     these references
 */
{
    PFIB_ENTRY FIBE;

    TI_DbgPrint(DEBUG_ROUTER, ("Called. NetworkAddress (0x%X)  Netmask (0x%X) "
        "Router (0x%X)  Metric (%d).\n", NetworkAddress, Netmask, Router, Metric));

    TI_DbgPrint(DEBUG_ROUTER, ("NetworkAddress (%s)  Netmask (%s)  Router (%s).\n",
			       A2S(NetworkAddress),
			       A2S(Netmask),
			       A2S(&Router->Address)));

    FIBE = ExAllocatePoolWithTag(NonPagedPool, sizeof(FIB_ENTRY), FIB_TAG);
    if (!FIBE) {
        TI_DbgPrint(MIN_TRACE, ("Insufficient resources.\n"));
        return NULL;
    }

    RtlCopyMemory( &FIBE->NetworkAddress, NetworkAddress,
		   sizeof(FIBE->NetworkAddress) );
    RtlCopyMemory( &FIBE->Netmask, Netmask,
		   sizeof(FIBE->Netmask) );
    FIBE->Router         = Router;
    FIBE->Metric         = Metric;
    FIBE->Origin         = NlroManual;
    FIBE->Lifetime       = 0;
    FIBE->Updated        = KeQueryInterruptTime();
    NBReferenceNeighbor(Router);

    /* Add FIB to the forward information base */
    TcpipInterlockedInsertTailList(&FIBListHead, &FIBE->ListEntry, &FIBLock);

    TiSignalChange();
    return FIBE;
}


static PNEIGHBOR_CACHE_ENTRY RouterRenewNeighbor(PNEIGHBOR_CACHE_ENTRY Removed)
{
    KIRQL OldIrql;
    PLIST_ENTRY CurrentEntry;
    PFIB_ENTRY Current;
    PNEIGHBOR_CACHE_ENTRY NCE;

    NCE = NBFindOrCreateNeighbor(Removed->Interface, &Removed->Address, TRUE);
    if (!NCE)
        return Removed;

    TcpipAcquireSpinLock(&FIBLock, &OldIrql);

    for (CurrentEntry = FIBListHead.Flink;
         CurrentEntry != &FIBListHead;
         CurrentEntry = CurrentEntry->Flink)
    {
        Current = CONTAINING_RECORD(CurrentEntry, FIB_ENTRY, ListEntry);

        if (Current->Router == Removed)
        {
            NBReferenceNeighbor(NCE);
            Current->Router = NCE;
            NBDereferenceNeighbor(Removed);
        }
    }

    TcpipReleaseSpinLock(&FIBLock, OldIrql);

    NBDereferenceNeighbor(Removed);
    return NCE;
}

static PNEIGHBOR_CACHE_ENTRY RouterFindRoute(PIP_ADDRESS Destination, PIP_INTERFACE Interface)
/*
 * FUNCTION: Finds a router to use to get to Destination
 * ARGUMENTS:
 *     Destination = Pointer to destination address (NULL means don't care)
 * RETURNS:
 *     Pointer to NCE for router, NULL if none was found
 * NOTES:
 *     If found the NCE is referenced
 */
{
    KIRQL OldIrql;
    PLIST_ENTRY CurrentEntry;
    PLIST_ENTRY NextEntry;
    PFIB_ENTRY Current;
    UCHAR State;
    UINT Length, BestLength = 0, MaskLength, BestMetric = 0;
    PNEIGHBOR_CACHE_ENTRY NCE, BestNCE = NULL;

    TI_DbgPrint(DEBUG_ROUTER, ("Called. Destination (0x%X)\n", Destination));

    TI_DbgPrint(DEBUG_ROUTER, ("Destination (%s)\n", A2S(Destination)));

    TcpipAcquireSpinLock(&FIBLock, &OldIrql);

    CurrentEntry = FIBListHead.Flink;
    while (CurrentEntry != &FIBListHead) {
        NextEntry = CurrentEntry->Flink;
	    Current = CONTAINING_RECORD(CurrentEntry, FIB_ENTRY, ListEntry);

        NCE   = Current->Router;
        State = NCE->State;

	Length = CommonPrefixLength(Destination, &Current->NetworkAddress);
	MaskLength = AddrCountPrefixBits(&Current->Netmask);

	TI_DbgPrint(DEBUG_ROUTER,("This-Route: %s (Sharing %d bits)\n",
				  A2S(&NCE->Address), Length));

	/* Routes through a disconnected interface stay configured, but are
	 * inactive until its media connects again. */
	if(Length >= MaskLength &&
           (!BestNCE || MaskLength > BestLength ||
            (MaskLength == BestLength && Current->Metric + IPInterfaceMetric(NCE->Interface) < BestMetric)) &&
           ((!(State & NUD_STALE) && !(State & NUD_INCOMPLETE)) || !BestNCE) &&
           (!Interface || NCE->Interface == Interface) &&
           (MaskLength || !NCE->Interface->DisableDefaultRoutes) &&
           IsInterfaceOperational(NCE->Interface)) {
	    /* This seems to be a better router */
	    BestNCE    = NCE;
	    BestLength = MaskLength;
	    BestMetric = Current->Metric + IPInterfaceMetric(NCE->Interface);
	    TI_DbgPrint(DEBUG_ROUTER,("Route selected\n"));
	}

        CurrentEntry = NextEntry;
    }

    if (BestNCE)
        NBReferenceNeighbor(BestNCE);

    TcpipReleaseSpinLock(&FIBLock, OldIrql);

    if (BestNCE && BestNCE->Removed)
        BestNCE = RouterRenewNeighbor(BestNCE);

    if( BestNCE ) {
	TI_DbgPrint(DEBUG_ROUTER,("Routing to %s\n", A2S(&BestNCE->Address)));
    } else {
	TI_DbgPrint(DEBUG_ROUTER,("Packet won't be routed\n"));
    }

    return BestNCE;
}

PNEIGHBOR_CACHE_ENTRY RouterGetRoute(PIP_ADDRESS Destination)
{
    return RouterFindRoute(Destination, NULL);
}

PNEIGHBOR_CACHE_ENTRY RouteGetRouteOnInterface(PIP_ADDRESS Destination, PIP_INTERFACE Interface)
{
    PNEIGHBOR_CACHE_ENTRY NCE;

    if (Destination->Type != IP_ADDRESS_V4)
        return NULL;

    if (IsInterfaceBroadcast(Interface, Destination))
    {
        NCE = NBLocateNeighbor(Destination, Interface);
        if (!NCE)
            NCE = NBAddNeighbor(Interface, Destination, NULL,
                                Interface->AddressLength, NUD_PERMANENT, 0);
        return NCE;
    }

    if (IsInterfaceOnLink(Interface, Destination))
    {
        return NBFindOrCreateNeighbor(Interface, Destination, FALSE);
    }

    return RouterFindRoute(Destination, Interface);
}

PNEIGHBOR_CACHE_ENTRY RouteGetRouteFromSource(PIP_ADDRESS Destination, PIP_INTERFACE SourceInterface)
{
    PIP_INTERFACE Owner;

    if (SourceInterface->WeakHostSend || SourceInterface == Loopback)
        return RouteGetRouteToDestination(Destination);
    Owner = AddrLocateInterface(Destination);
    if (Owner)
    {
        IPDereferenceInterface(Owner);
        return RouteGetRouteToDestination(Destination);
    }
    return RouteGetRouteOnInterface(Destination, SourceInterface);
}

PNEIGHBOR_CACHE_ENTRY RouteGetMulticastRouteOnInterface(PIP_ADDRESS Destination, PIP_INTERFACE Interface)
{
    PNEIGHBOR_CACHE_ENTRY Neighbor;
    ULONG Address = DN2H(Destination->Address.IPv4Address);
    UCHAR LinkAddress[6] = {0x01, 0x00, 0x5e, (Address >> 16) & 0x7f,
                           (Address >> 8) & 0xff, Address & 0xff};

    ASSERT(Destination->Type == IP_ADDRESS_V4);
    ASSERT((Address & 0xf0000000) == 0xe0000000);
    Neighbor = NBLocateNeighbor(Destination, Interface);
    if (!Neighbor && (Interface->AddressLength == sizeof(LinkAddress) || Interface == Loopback))
        Neighbor = NBAddNeighbor(Interface, Destination, Interface == Loopback ? NULL : LinkAddress,
                                 Interface->AddressLength, NUD_PERMANENT, 0);
    return Neighbor;
}

PNEIGHBOR_CACHE_ENTRY RouteGetMulticastRoute(PIP_ADDRESS Destination, ULONG Selector)
{
    PIP_INTERFACE Interface = GetMulticastInterface(Selector);
    PNEIGHBOR_CACHE_ENTRY Neighbor;

    if (!Interface) return NULL;
    Neighbor = RouteGetMulticastRouteOnInterface(Destination, Interface);
    IPDereferenceInterface(Interface);
    return Neighbor;
}

PNEIGHBOR_CACHE_ENTRY RouteGetRouteToDestination(PIP_ADDRESS Destination)
/*
 * FUNCTION: Locates an RCN describing a route to a destination address
 * ARGUMENTS:
 *     Destination = Pointer to destination address to find route to
 *     RCN         = Address of pointer to an RCN
 * RETURNS:
 *     Status of operation
 * NOTES:
 *     The RCN is referenced for the caller. The caller is responsible
 *     for dereferencing it after use
 */
{
    PNEIGHBOR_CACHE_ENTRY NCE = NULL;
    PIP_INTERFACE Interface;

    TI_DbgPrint(DEBUG_RCACHE, ("Called. Destination (0x%X)\n", Destination));

    TI_DbgPrint(DEBUG_RCACHE, ("Destination (%s)\n", A2S(Destination)));

#if 0
    TI_DbgPrint(MIN_TRACE, ("Displaying tree (before).\n"));
    PrintTree(RouteCache);
#endif

    /* Check if the destination is on-link */
    Interface = FindOnLinkInterface(Destination);
    if (Interface) {
	/* The destination address is on-link. Check our neighbor cache */
	NCE = NBFindOrCreateNeighbor(Interface, Destination, FALSE);
        IPDereferenceInterface(Interface);
    } else {
	/* Destination is not on any subnets we're on. Find a router to use */
	NCE = RouterGetRoute(Destination);
    }

    if( NCE )
	TI_DbgPrint(DEBUG_ROUTER,("Interface->MTU: %d\n", NCE->Interface->MTU));

    return NCE;
}

VOID RouterRemoveRoutesForInterface(PIP_INTERFACE Interface)
{
    KIRQL OldIrql;
    PLIST_ENTRY CurrentEntry;
    PLIST_ENTRY NextEntry;
    PFIB_ENTRY Current;

    TcpipAcquireSpinLock(&FIBLock, &OldIrql);

    CurrentEntry = FIBListHead.Flink;
    while (CurrentEntry != &FIBListHead) {
        NextEntry = CurrentEntry->Flink;
        Current = CONTAINING_RECORD(CurrentEntry, FIB_ENTRY, ListEntry);

        if (Interface == Current->Router->Interface)
            DestroyFIBE(Current);

        CurrentEntry = NextEntry;
    }

    TcpipReleaseSpinLock(&FIBLock, OldIrql);
}

NTSTATUS RouterRemoveRoute(PIP_ADDRESS Target, PIP_ADDRESS Router)
/*
 * FUNCTION: Removes a route from the Forward Information Base (FIB)
 * ARGUMENTS:
 *     Target: The machine or network targeted by the route
 *     Router: The router used to pass the packet to the destination
 *
 * Searches the FIB and removes a route matching the indicated parameters.
 */
{
    KIRQL OldIrql;
    PLIST_ENTRY CurrentEntry;
    PLIST_ENTRY NextEntry;
    PFIB_ENTRY Current;
    BOOLEAN Found = FALSE;
    PNEIGHBOR_CACHE_ENTRY NCE;

    TI_DbgPrint(DEBUG_ROUTER, ("Called\n"));
    TI_DbgPrint(DEBUG_ROUTER, ("Deleting Route From: %s\n", A2S(Router)));
    TI_DbgPrint(DEBUG_ROUTER, ("                 To: %s\n", A2S(Target)));

    TcpipAcquireSpinLock(&FIBLock, &OldIrql);

    RouterDumpRoutes();

    CurrentEntry = FIBListHead.Flink;
    while (CurrentEntry != &FIBListHead) {
        NextEntry = CurrentEntry->Flink;
	Current = CONTAINING_RECORD(CurrentEntry, FIB_ENTRY, ListEntry);

        NCE   = Current->Router;

	if( AddrIsEqual( &Current->NetworkAddress, Target ) &&
	    AddrIsEqual( &NCE->Address, Router ) ) {
	    Found = TRUE;
	    break;
	}

	Current = NULL;
        CurrentEntry = NextEntry;
    }

    if( Found ) {
        TI_DbgPrint(DEBUG_ROUTER, ("Deleting route\n"));
        DestroyFIBE( Current );
    }

    RouterDumpRoutes();

    TcpipReleaseSpinLock(&FIBLock, OldIrql);

    TI_DbgPrint(DEBUG_ROUTER, ("Leaving\n"));

    if (!Found)
        return STATUS_UNSUCCESSFUL;
    TiSignalChange();
    return STATUS_SUCCESS;
}


PFIB_ENTRY RouterCreateRoute(
    PIP_ADDRESS NetworkAddress,
    PIP_ADDRESS Netmask,
    PIP_ADDRESS RouterAddress,
    PIP_INTERFACE Interface,
    UINT Metric)
/*
 * FUNCTION: Creates a route with IPv4 addresses as parameters
 * ARGUMENTS:
 *     NetworkAddress = Address of network
 *     Netmask        = Netmask of network
 *     RouterAddress  = Address of router to use
 *     Interface      = Network interface
 * RETURNS:
 *     Pointer to FIB entry if the route was created, NULL if not.
 *     The FIB entry references the NTE. The caller is responsible
 *     for providing this reference
 */
{
    KIRQL OldIrql;
    PLIST_ENTRY CurrentEntry;
    PLIST_ENTRY NextEntry;
    PFIB_ENTRY Current;
    PFIB_ENTRY FIBE;
    PNEIGHBOR_CACHE_ENTRY NCE;

    TcpipAcquireSpinLock(&FIBLock, &OldIrql);

    CurrentEntry = FIBListHead.Flink;
    while (CurrentEntry != &FIBListHead) {
        NextEntry = CurrentEntry->Flink;
        Current = CONTAINING_RECORD(CurrentEntry, FIB_ENTRY, ListEntry);

        NCE   = Current->Router;

        if(AddrIsEqual(NetworkAddress, &Current->NetworkAddress) &&
           AddrIsEqual(Netmask, &Current->Netmask) &&
           NCE->Interface == Interface)
        {
            TI_DbgPrint(DEBUG_ROUTER,("Attempting to add duplicate route to %s\n", A2S(NetworkAddress)));
            TcpipReleaseSpinLock(&FIBLock, OldIrql);
            return NULL;
        }

        CurrentEntry = NextEntry;
    }

    TcpipReleaseSpinLock(&FIBLock, OldIrql);

    /* The NCE references RouterAddress. The NCE is referenced for us */
    NCE = NBFindOrCreateNeighbor(Interface, RouterAddress, TRUE);

    if (!NCE) {
        /* Not enough free resources */
        return NULL;
    }

    FIBE = RouterAddRoute(NetworkAddress, Netmask, NCE, Metric);
    NBDereferenceNeighbor(NCE);
    return FIBE;
}


BOOLEAN RouterSetAdvertisedRoute(PIP_INTERFACE Interface, PIP_ADDRESS RouterAddress, UINT Metric, ULONG Lifetime)
{
    PNEIGHBOR_CACHE_ENTRY NCE;
    PLIST_ENTRY Entry;
    PFIB_ENTRY FIBE;
    IP_ADDRESS Default;
    KIRQL OldIrql;

    TcpipAcquireSpinLock(&FIBLock, &OldIrql);
    for (Entry = FIBListHead.Flink; Entry != &FIBListHead; Entry = Entry->Flink)
    {
        FIBE = CONTAINING_RECORD(Entry, FIB_ENTRY, ListEntry);
        if (FIBE->Origin == NlroRouterAdvertisement && FIBE->Router->Interface == Interface &&
            AddrIsEqual(&FIBE->Router->Address, RouterAddress))
        {
            FIBE->Metric = Metric;
            FIBE->Lifetime = Lifetime;
            FIBE->Updated = KeQueryInterruptTime();
            TcpipReleaseSpinLock(&FIBLock, OldIrql);
            return TRUE;
        }
    }
    TcpipReleaseSpinLock(&FIBLock, OldIrql);

    NCE = NBFindOrCreateNeighbor(Interface, RouterAddress, TRUE);
    if (!NCE)
        return FALSE;

    AddrInitIPv4(&Default, 0);
    FIBE = RouterAddRoute(&Default, &Default, NCE, Metric);
    NBDereferenceNeighbor(NCE);
    if (!FIBE)
        return FALSE;

    TcpipAcquireSpinLock(&FIBLock, &OldIrql);
    FIBE->Origin = NlroRouterAdvertisement;
    FIBE->Lifetime = Lifetime;
    TcpipReleaseSpinLock(&FIBLock, OldIrql);
    return TRUE;
}

VOID RouterRemoveAdvertisedRoutes(PIP_INTERFACE Interface, PIP_ADDRESS RouterAddress)
{
    PLIST_ENTRY Entry, Next;
    KIRQL OldIrql;

    TcpipAcquireSpinLock(&FIBLock, &OldIrql);
    for (Entry = FIBListHead.Flink; Entry != &FIBListHead; Entry = Next)
    {
        PFIB_ENTRY FIBE = CONTAINING_RECORD(Entry, FIB_ENTRY, ListEntry);

        Next = Entry->Flink;
        if (FIBE->Origin == NlroRouterAdvertisement && FIBE->Router->Interface == Interface &&
            (!RouterAddress || AddrIsEqual(&FIBE->Router->Address, RouterAddress)))
            DestroyFIBE(FIBE);
    }
    TcpipReleaseSpinLock(&FIBLock, OldIrql);
}

VOID RouterAgeAdvertisedRoutes(VOID)
{
    ULONGLONG Now = KeQueryInterruptTime();
    PLIST_ENTRY Entry, Next;
    KIRQL OldIrql;

    TcpipAcquireSpinLock(&FIBLock, &OldIrql);
    for (Entry = FIBListHead.Flink; Entry != &FIBListHead; Entry = Next)
    {
        PFIB_ENTRY FIBE = CONTAINING_RECORD(Entry, FIB_ENTRY, ListEntry);

        Next = Entry->Flink;
        if (FIBE->Origin == NlroRouterAdvertisement &&
            Now >= FIBE->Updated + (ULONGLONG)FIBE->Lifetime * 10000000)
            DestroyFIBE(FIBE);
    }
    TcpipReleaseSpinLock(&FIBLock, OldIrql);
}

NTSTATUS RouterQueryRouteV4(PIP4_ROUTE_INFO Info)
{
    ULONGLONG Now = KeQueryInterruptTime();
    NTSTATUS Status = STATUS_NOT_FOUND;
    PLIST_ENTRY Entry;
    KIRQL OldIrql;

    TcpipAcquireSpinLock(&FIBLock, &OldIrql);
    for (Entry = FIBListHead.Flink; Entry != &FIBListHead; Entry = Entry->Flink)
    {
        PFIB_ENTRY FIBE = CONTAINING_RECORD(Entry, FIB_ENTRY, ListEntry);
        ULONG Elapsed = (ULONG)((Now - FIBE->Updated) / 10000000);

        if (FIBE->Router->Interface->Index != Info->Index ||
            FIBE->NetworkAddress.Address.IPv4Address != Info->Destination ||
            FIBE->Netmask.Address.IPv4Address != Info->Mask ||
            FIBE->Router->Address.Address.IPv4Address != Info->NextHop)
            continue;

        Info->Origin = FIBE->Origin;
        Info->Age = Elapsed;
        if (FIBE->Origin == NlroRouterAdvertisement)
            Info->ValidLifetime = FIBE->Lifetime > Elapsed ? FIBE->Lifetime - Elapsed : 0;
        else
            Info->ValidLifetime = FIBE->Lifetime ? FIBE->Lifetime : MAXULONG;
        Status = STATUS_SUCCESS;
        break;
    }
    TcpipReleaseSpinLock(&FIBLock, OldIrql);

    return Status;
}


NTSTATUS RouterSetRouteV4(PIP4_ROUTE_INFO Info)
{
    NTSTATUS Status = STATUS_NOT_FOUND;
    PLIST_ENTRY Entry;
    KIRQL OldIrql;

    TcpipAcquireSpinLock(&FIBLock, &OldIrql);
    for (Entry = FIBListHead.Flink; Entry != &FIBListHead; Entry = Entry->Flink)
    {
        PFIB_ENTRY FIBE = CONTAINING_RECORD(Entry, FIB_ENTRY, ListEntry);

        if (FIBE->Router->Interface->Index != Info->Index ||
            FIBE->NetworkAddress.Address.IPv4Address != Info->Destination ||
            FIBE->Netmask.Address.IPv4Address != Info->Mask ||
            FIBE->Router->Address.Address.IPv4Address != Info->NextHop ||
            FIBE->Origin == NlroRouterAdvertisement)
            continue;

        FIBE->Lifetime = Info->ValidLifetime == MAXULONG ? 0 : Info->ValidLifetime;
        Status = STATUS_SUCCESS;
        break;
    }
    TcpipReleaseSpinLock(&FIBLock, OldIrql);

    return Status;
}


NTSTATUS RouterStartup(
    VOID)
/*
 * FUNCTION: Initializes the routing subsystem
 * RETURNS:
 *     Status of operation
 */
{
    TI_DbgPrint(DEBUG_ROUTER, ("Called.\n"));

    /* Initialize the Forward Information Base */
    InitializeListHead(&FIBListHead);
    TcpipInitializeSpinLock(&FIBLock);

    return STATUS_SUCCESS;
}


NTSTATUS RouterShutdown(
    VOID)
/*
 * FUNCTION: Shuts down the routing subsystem
 * RETURNS:
 *     Status of operation
 */
{
    KIRQL OldIrql;

    TI_DbgPrint(DEBUG_ROUTER, ("Called.\n"));

    /* Clear Forward Information Base */
    TcpipAcquireSpinLock(&FIBLock, &OldIrql);
    DestroyFIBEs();
    TcpipReleaseSpinLock(&FIBLock, OldIrql);

    return STATUS_SUCCESS;
}

/* EOF */
