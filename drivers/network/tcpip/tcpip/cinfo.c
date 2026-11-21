/*
 * COPYRIGHT:   See COPYING in the top level directory
 * PROJECT:     ReactOS TCP/IP protocol driver
 * FILE:        tcpip/cinfo.c
 * PURPOSE:     Per-socket connection information.
 * PROGRAMMER:  Jérôme Gardou
 */

#include "ntdef.h"
#include "precomp.h"

typedef struct tcp_keepalive {
  ULONG onoff;
  ULONG keepalivetime;
  ULONG keepaliveinterval;
} TCP_KEEPALIVE;

static TDI_STATUS SetConnectionInfoLocked(TDIObjectID *ID,
                                          PADDRESS_FILE AddressFile,
                                          PVOID Buffer,
                                          UINT BufferSize)
{
    NTSTATUS Status;
    PCONNECTION_ENDPOINT Connection = AddressFile->Connection;
    BOOLEAN Deferred = (Connection == NULL) || (Connection->SocketContext == NULL);

    ASSERT_TCPIP_OBJECT_LOCKED(AddressFile);
    ASSERT(ID->toi_type == INFO_TYPE_CONNECTION);
    switch (ID->toi_id)
    {
        case TCP_SOCKET_NODELAY:
        {
            BOOLEAN Set;
            if (BufferSize < sizeof(BOOLEAN))
                return TDI_INVALID_PARAMETER;
            Set = *(BOOLEAN*)Buffer;
            if (Deferred)
            {
                AddressFile->PendingNoDelay = Set;
                AddressFile->PendingNoDelaySet = TRUE;
                return TDI_SUCCESS;
            }
            return TCPSetNoDelay(Connection, Set);
        }
        case TCP_SOCKET_KEEPALIVE:
        {
            DWORD Set;
            if (BufferSize < sizeof(DWORD))
                return TDI_INVALID_PARAMETER;
            Set = *(DWORD*)Buffer;
            if (Deferred)
            {
                AddressFile->PendingKeepAlive = (Set != 0);
                AddressFile->PendingKeepAliveSet = TRUE;
                return TDI_SUCCESS;
            }
            return TCPSetKeepAlive(Connection, Set);
        }
        case TCP_SOCKET_KEEPALIVEVALS:
        {
            TCP_KEEPALIVE Set;
            if (BufferSize < sizeof(TCP_KEEPALIVE))
                return TDI_INVALID_PARAMETER;
            Set = *(TCP_KEEPALIVE*)Buffer;
            if (Deferred)
            {
                AddressFile->PendingKeepAlive = (Set.onoff != 0);
                AddressFile->PendingKeepAliveSet = TRUE;
                AddressFile->PendingKeepAliveTime = Set.keepalivetime;
                AddressFile->PendingKeepAliveInterval = Set.keepaliveinterval;
                AddressFile->PendingKeepAliveValuesSet = TRUE;
                return TDI_SUCCESS;
            }
            Status = TCPSetKeepAlive(Connection, Set.onoff);
            if (!NT_SUCCESS(Status))
                return Status;

            return TCPSetKeepAliveValues(Connection, Set.keepalivetime, Set.keepaliveinterval);
        }
        case TCP_SOCKET_KEEPALIVECOUNT:
        {
            ULONG Count;
            if (BufferSize < sizeof(ULONG))
                return TDI_INVALID_PARAMETER;
            Count = *(ULONG*)Buffer;
            if (Deferred)
            {
                AddressFile->PendingKeepAliveCount = Count;
                AddressFile->PendingKeepAliveCountSet = TRUE;
                return TDI_SUCCESS;
            }
            return TCPSetKeepAliveCount(Connection, Count);
        }
        default:
            DbgPrint("TCPIP: Unknown connection info ID: %u.\n", ID->toi_id);
    }

    return TDI_INVALID_PARAMETER;
}

TDI_STATUS SetConnectionInfo(TDIObjectID *ID,
                             PADDRESS_FILE AddressFile,
                             PVOID Buffer,
                             UINT BufferSize)
{
    TDI_STATUS Status;

    ASSERT(KeGetCurrentIrql() <= APC_LEVEL);

    LockObject(AddressFile);
    Status = SetConnectionInfoLocked(ID, AddressFile, Buffer, BufferSize);
    UnlockObject(AddressFile);

    return Status;
}

/* A request made on a connection file is for that connection, whichever address file it is associated with */
TDI_STATUS SetConnectionEndpointInfo(TDIObjectID *ID,
                                     PCONNECTION_ENDPOINT Connection,
                                     PVOID Buffer,
                                     UINT BufferSize)
{
    TDI_STATUS Status;
    TCP_KEEPALIVE Set;

    ASSERT(KeGetCurrentIrql() <= APC_LEVEL);
    ASSERT(ID->toi_type == INFO_TYPE_CONNECTION);

    if (Connection == NULL)
        return TDI_INVALID_PARAMETER;

    LockObject(Connection);

    if (Connection->SocketContext == NULL)
    {
        UnlockObject(Connection);
        return TDI_INVALID_STATE;
    }

    switch (ID->toi_id)
    {
        case TCP_SOCKET_NODELAY:
            Status = (BufferSize < sizeof(BOOLEAN)) ? TDI_INVALID_PARAMETER : TCPSetNoDelay(Connection, *(BOOLEAN *)Buffer);
            break;

        case TCP_SOCKET_KEEPALIVE:
            Status = (BufferSize < sizeof(DWORD)) ? TDI_INVALID_PARAMETER : TCPSetKeepAlive(Connection, *(DWORD *)Buffer != 0);
            break;

        case TCP_SOCKET_KEEPALIVEVALS:
            if (BufferSize < sizeof(TCP_KEEPALIVE))
            {
                Status = TDI_INVALID_PARAMETER;
                break;
            }
            Set = *(TCP_KEEPALIVE *)Buffer;
            Status = TCPSetKeepAlive(Connection, Set.onoff != 0);
            if (NT_SUCCESS(Status))
                Status = TCPSetKeepAliveValues(Connection, Set.keepalivetime, Set.keepaliveinterval);
            break;

        case TCP_SOCKET_KEEPALIVECOUNT:
            Status = (BufferSize < sizeof(ULONG)) ? TDI_INVALID_PARAMETER : TCPSetKeepAliveCount(Connection, *(ULONG *)Buffer);
            break;

        default:
            Status = TDI_INVALID_PARAMETER;
            break;
    }

    UnlockObject(Connection);
    return Status;
}
