/*
 * COPYRIGHT:   See COPYING in the top level directory
 * PROJECT:     ReactOS TCP/IP protocol driver
 * FILE:        transport/tcp/accept.c
 * PURPOSE:     Transmission Control Protocol Listen/Accept code
 * PROGRAMMERS: Art Yerkes (arty@users.sf.net)
 * REVISIONS:
 *   arty 12/21/2004 Created
 */

#include "precomp.h"

#include <lwip_glue/lwip_glue.h>

extern NPAGED_LOOKASIDE_LIST TdiBucketLookasideList;

NTSTATUS TCPCheckPeerForAccept(PVOID Context,
                               PTDI_REQUEST_KERNEL Request)
{
    struct tcp_pcb *newpcb = (struct tcp_pcb*)Context;
    NTSTATUS Status;
    PTDI_CONNECTION_INFORMATION WhoIsConnecting;
    ip_addr_t ipaddr;
    IP_ADDRESS Address;
    u16_t Port;

    if (Request->RequestFlags & TDI_QUERY_ACCEPT)
        DbgPrint("TDI_QUERY_ACCEPT NOT SUPPORTED!!!\n");

    WhoIsConnecting = (PTDI_CONNECTION_INFORMATION)Request->ReturnConnectionInformation;

    Status = TCPTranslateError(LibTCPGetPeerName(newpcb, &ipaddr, &Port));
    if (!NT_SUCCESS(Status))
        return Status;

    LibIPAddressFromLwip(&ipaddr, &Address);
    if (((PTRANSPORT_ADDRESS)WhoIsConnecting->RemoteAddress)->Address[0].AddressType == TDI_ADDRESS_TYPE_IP6)
        TCPMapToDualStack(&Address);
    return AddrBuildTransportAddress(&Address, WH2N(Port),
                                     (PTRANSPORT_ADDRESS)WhoIsConnecting->RemoteAddress,
                                     WhoIsConnecting->RemoteAddressLength);
}

/* This listen is on a socket we keep as internal.  That socket has the same
 * lifetime as the address file */
NTSTATUS TCPListen(PCONNECTION_ENDPOINT Connection, UINT Backlog)
{
    NTSTATUS Status = STATUS_SUCCESS;
    ip_addr_t AddressToBind;
    USHORT LocalPort;

    ASSERT(Connection);

    LockObject(Connection);

    ASSERT_KM_POINTER(Connection->AddressFile);

    TI_DbgPrint(DEBUG_TCP,("[IP, TCPListen] Called\n"));

    TI_DbgPrint(DEBUG_TCP, ("Connection->SocketContext %x\n",
        Connection->SocketContext));

    LibIPAddressToLwip(&Connection->AddressFile->Address, &AddressToBind);
    if (Connection->AddressFile->DualStack)
        ip_addr_copy(AddressToBind, *IP_ANY_TYPE);

    Status = TCPTranslateError(LibTCPBind(Connection,
                                          &AddressToBind,
                                          Connection->AddressFile->Port));

    if (NT_SUCCESS(Status))
    {
        /* Check if we had an unspecified port */
        if (!Connection->AddressFile->Port)
        {
            /* We did, so we need to copy back the port */
            Status = TCPGetSockPort(Connection, &LocalPort);
            if (NT_SUCCESS(Status))
            {
                /* Allocate the port in the port bitmap */
                UINT AllocatedPort = TCPAllocatePort(LocalPort);
                /* This should never fail unless all ports are in use */
                if (AllocatedPort == (UINT) -1)
                {
                    DbgPrint("ERR: No more ports available.\n");
                    UnlockObject(Connection);
                    return STATUS_TOO_MANY_ADDRESSES;
                }
                Connection->AddressFile->Port = AllocatedPort;
            }
        }
    }

    if (NT_SUCCESS(Status))
    {
        Connection->SocketContext = LibTCPListen(Connection, Backlog);
        if (!Connection->SocketContext)
            Status = STATUS_UNSUCCESSFUL;
    }

    UnlockObject(Connection);

    TI_DbgPrint(DEBUG_TCP,("[IP, TCPListen] Leaving. Status = %x\n", Status));

    return Status;
}

BOOLEAN TCPAbortListenForSocket
(   PCONNECTION_ENDPOINT Listener,
    PCONNECTION_ENDPOINT Connection)
{
    PLIST_ENTRY ListEntry;
    PTDI_BUCKET Bucket;
    BOOLEAN Found = FALSE;

    LockObject(Listener);

    ListEntry = Listener->ListenRequest.Flink;
    while (ListEntry != &Listener->ListenRequest)
    {
        Bucket = CONTAINING_RECORD(ListEntry, TDI_BUCKET, Entry);

        if (Bucket->AssociatedEndpoint == Connection)
        {
            DereferenceObject(Bucket->AssociatedEndpoint);
            RemoveEntryList( &Bucket->Entry );
            ExFreeToNPagedLookasideList(&TdiBucketLookasideList, Bucket);
            Found = TRUE;
            break;
        }

        ListEntry = ListEntry->Flink;
    }

    UnlockObject(Listener);

    return Found;
}

NTSTATUS TCPAccept ( PTDI_REQUEST Request,
                     PCONNECTION_ENDPOINT Listener,
                     PCONNECTION_ENDPOINT Connection,
                     PTCP_COMPLETION_ROUTINE Complete,
                     PVOID Context )
{
    NTSTATUS Status;
    PTDI_BUCKET Bucket;
    BOOLEAN DrainPending = FALSE;

    LockObject(Listener);

    Bucket = ExAllocateFromNPagedLookasideList(&TdiBucketLookasideList);

    if (Bucket)
    {
        Bucket->AssociatedEndpoint = Connection;
        ReferenceObject(Bucket->AssociatedEndpoint);

        Bucket->Request.RequestNotifyObject = Complete;
        Bucket->Request.RequestContext = Context;
        InsertTailList( &Listener->ListenRequest, &Bucket->Entry );
        if (!IsListEmpty(&Listener->PendingAccepts))
        {
            ReferenceObject(Listener);
            DrainPending = TRUE;
        }
        Status = STATUS_PENDING;
    }
    else
        Status = STATUS_NO_MEMORY;

    UnlockObject(Listener);

    if (DrainPending)
    {
        LibTCPDrainPendingAccept(Listener);
        DereferenceObject(Listener);
    }

    return Status;
}
