/*
 * COPYRIGHT:   See COPYING in the top level directory
 * PROJECT:     ReactOS Ancillary Function Driver DLL
 * FILE:        dll/win32/msafd/misc/stubs.c
 * PURPOSE:     Stubs
 * PROGRAMMERS: Casper S. Hornstrup (chorns@users.sourceforge.net)
 * REVISIONS:
 *   CSH 01/09-2000 Created
 */

#include <msafd.h>

INT
WSPAPI
WSPCancelBlockingCall(
    OUT LPINT lpErrno)
{
    UNIMPLEMENTED;

    return 0;
}


BOOL
WSPAPI
WSPGetQOSByName(
    IN      SOCKET s,
    IN OUT  LPWSABUF lpQOSName,
    OUT     LPQOS lpQOS,
    OUT     LPINT lpErrno)
{
    UNIMPLEMENTED;

    return FALSE;
}


SOCKET
WSPAPI
WSPJoinLeaf(
    IN  SOCKET s,
    IN  CONST SOCKADDR *name,
    IN  INT namelen,
    IN  LPWSABUF lpCallerData,
    OUT LPWSABUF lpCalleeData,
    IN  LPQOS lpSQOS,
    IN  LPQOS lpGQOS,
    IN  DWORD dwFlags,
    OUT LPINT lpErrno)
{
    UNIMPLEMENTED;

    return (SOCKET)0;
}

BOOL
WSPAPI
WSPAcceptEx(
    IN SOCKET sListenSocket,
    IN SOCKET sAcceptSocket,
    OUT PVOID lpOutputBuffer,
    IN DWORD dwReceiveDataLength,
    IN DWORD dwLocalAddressLength,
    IN DWORD dwRemoteAddressLength,
    OUT LPDWORD lpdwBytesReceived,
    IN OUT LPOVERLAPPED lpOverlapped)
{
    PSOCKET_INFORMATION Socket;
    AFD_SUPER_ACCEPT_INFO AcceptInfo;
    PIO_STATUS_BLOCK IOSB;
    NTSTATUS Status;

    Socket = GetSocketStructure(sListenSocket);
    if (!Socket)
    {
        SetLastError(WSAENOTSOCK);
        return FALSE;
    }

    if (!lpOverlapped || !lpOutputBuffer || !GetSocketStructure(sAcceptSocket))
    {
        SetLastError(WSAEINVAL);
        return FALSE;
    }

    AcceptInfo.AcceptHandle = (HANDLE)sAcceptSocket;
    AcceptInfo.ReceiveDataLength = dwReceiveDataLength;
    AcceptInfo.LocalAddressLength = dwLocalAddressLength;
    AcceptInfo.RemoteAddressLength = dwRemoteAddressLength;

    IOSB = (PIO_STATUS_BLOCK)lpOverlapped;
    IOSB->Status = STATUS_PENDING;

    Status = NtDeviceIoControlFile((HANDLE)sListenSocket,
                                   lpOverlapped->hEvent,
                                   NULL,
                                   lpOverlapped->hEvent ? NULL : lpOverlapped,
                                   IOSB,
                                   IOCTL_AFD_SUPER_ACCEPT,
                                   lpOutputBuffer,
                                   dwReceiveDataLength + dwLocalAddressLength + dwRemoteAddressLength,
                                   &AcceptInfo,
                                   sizeof(AcceptInfo));

    if (Status == STATUS_SUCCESS && lpdwBytesReceived)
        *lpdwBytesReceived = (DWORD)IOSB->Information;

    SetLastError(TranslateNtStatusError(Status));

    return Status == STATUS_SUCCESS;
}

BOOL
WSPAPI
WSPDisconnectEx(
  IN SOCKET hSocket,
  IN LPOVERLAPPED lpOverlapped,
  IN DWORD dwFlags,
  IN DWORD reserved)
{
    IO_STATUS_BLOCK DummyIOSB;
    PIO_STATUS_BLOCK IOSB = &DummyIOSB;
    AFD_DISCONNECT_INFO DisconnectInfo;
    PSOCKET_INFORMATION Socket;
    HANDLE Event = NULL;
    PVOID ApcContext = NULL;
    NTSTATUS Status;

    Socket = GetSocketStructure(hSocket);
    if (!Socket)
    {
        SetLastError(WSAENOTSOCK);
        return FALSE;
    }

    if (reserved || (dwFlags & ~TF_REUSE_SOCKET))
    {
        SetLastError(WSAEINVAL);
        return FALSE;
    }

    if (Socket->SharedData->State != SocketConnected)
    {
        SetLastError(WSAENOTCONN);
        return FALSE;
    }

    /* TODO: TF_REUSE_SOCKET does not yet return the socket to a state AcceptEx and ConnectEx accept */
    DisconnectInfo.DisconnectType = AFD_DISCONNECT_SEND;
    DisconnectInfo.Timeout.QuadPart = -1000000LL;

    if (lpOverlapped)
    {
        IOSB = (PIO_STATUS_BLOCK)lpOverlapped;
        Event = lpOverlapped->hEvent;
        if (!((ULONG_PTR)Event & 1))
            ApcContext = lpOverlapped;
    }
    else
    {
        Status = NtCreateEvent(&Event, EVENT_ALL_ACCESS, NULL, SynchronizationEvent, FALSE);
        if (!NT_SUCCESS(Status))
        {
            SetLastError(TranslateNtStatusError(Status));
            return FALSE;
        }
    }

    IOSB->Status = STATUS_PENDING;
    IOSB->Information = 0;
    Socket->SharedData->SendShutdown = TRUE;

    Status = NtDeviceIoControlFile((HANDLE)hSocket,
                                   Event,
                                   NULL,
                                   ApcContext,
                                   IOSB,
                                   IOCTL_AFD_DISCONNECT,
                                   &DisconnectInfo,
                                   sizeof(DisconnectInfo),
                                   NULL,
                                   0);

    if (!lpOverlapped)
    {
        if (Status == STATUS_PENDING)
        {
            MsafdWaitForAlert(Event);
            Status = IOSB->Status;
        }
        NtClose(Event);
    }
    else if (Status == STATUS_SUCCESS)
    {
        Status = STATUS_PENDING;
    }

    SetLastError(TranslateNtStatusError(Status));
    return Status == STATUS_SUCCESS;
}

VOID
WSPAPI
WSPGetAcceptExSockaddrs(
    IN PVOID lpOutputBuffer,
    IN DWORD dwReceiveDataLength,
    IN DWORD dwLocalAddressLength,
    IN DWORD dwRemoteAddressLength,
    OUT struct sockaddr **LocalSockaddr,
    OUT LPINT LocalSockaddrLength,
    OUT struct sockaddr **RemoteSockaddr,
    OUT LPINT RemoteSockaddrLength)
{
    PCHAR Buffer = (PCHAR)lpOutputBuffer + dwReceiveDataLength;

    *LocalSockaddrLength = *(PINT)Buffer;
    *LocalSockaddr = (struct sockaddr *)(Buffer + sizeof(INT));

    Buffer += dwLocalAddressLength;
    *RemoteSockaddrLength = *(PINT)Buffer;
    *RemoteSockaddr = (struct sockaddr *)(Buffer + sizeof(INT));
}

/* EOF */
