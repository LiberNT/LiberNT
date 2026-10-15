/*
 * COPYRIGHT:   See COPYING in the top level directory
 * PROJECT:     ReactOS WinSock 2 API
 * FILE:        dll/win32/ws2_32/src/select.c
 * PURPOSE:     Socket Select Support
 * PROGRAMMER:  Alex Ionescu (alex@relsoft.net)
 */

/* INCLUDES ******************************************************************/

#include <ws2_32.h>
#include <ndk/exfuncs.h>
#include <ndk/iofuncs.h>
#include <ndk/obfuncs.h>
#include <tdi.h>
#include <afd/shared.h>

#define NDEBUG
#include <debug.h>

/* FUNCTIONS *****************************************************************/

/*
 * @implemented
 */
INT
WSPAPI
__WSAFDIsSet(SOCKET s,
             LPFD_SET set)
{
    INT i = set->fd_count;
    INT Return = FALSE;

    /* Loop until a match is found */
    while (i--) if (set->fd_array[i] == s) Return = TRUE;

    /* Return */
    return Return;
}

/*
 * @implemented
 */
INT
WSAAPI
select(IN INT s,
       IN OUT LPFD_SET readfds,
       IN OUT LPFD_SET writefds,
       IN OUT LPFD_SET exceptfds,
       IN CONST struct timeval *timeout)
{
    PWSSOCKET Socket;
    INT Status;
    INT ErrorCode;
    SOCKET Handle;
    LPWSPSELECT WSPSelect;

    DPRINT("select: %lx %p %p %p %p\n", s, readfds, writefds, exceptfds, timeout);

    /* Check for WSAStartup */
    ErrorCode = WsQuickProlog();

    if (ErrorCode != ERROR_SUCCESS)
    {
        SetLastError(ErrorCode);
        return SOCKET_ERROR;
    }

    /* Use the first Socket from the first valid set */
    if (readfds && readfds->fd_count)
    {
        Handle = readfds->fd_array[0];
    }
    else if (writefds && writefds->fd_count)
    {
        Handle = writefds->fd_array[0];
    }
    else if (exceptfds && exceptfds->fd_count)
    {
        Handle = exceptfds->fd_array[0];
    }
    else
    {
        /* Invalid handles */
        SetLastError(WSAEINVAL);
        return SOCKET_ERROR;
    }

    /* Get the Socket Context */
    Socket = WsSockGetSocket(Handle);

    if (!Socket)
    {
        /* No Socket Context Found */
        SetLastError(WSAENOTSOCK);
        return SOCKET_ERROR;
    }

    /* Get the select procedure */
    WSPSelect = Socket->Provider->Service.lpWSPSelect;

    /* Make the call */
    Status = WSPSelect(s, readfds, writefds, exceptfds, (struct timeval *)timeout,
                       &ErrorCode);

    /* Deference the Socket Context */
    WsSockDereference(Socket);

    /* Return Provider Value */
    if (Status != SOCKET_ERROR)
        return Status;

    /* If everything seemed fine, then the WSP call failed itself */
    if (ErrorCode == NO_ERROR)
        ErrorCode = WSASYSCALLFAILURE;

    /* Return with an error */
    SetLastError(ErrorCode);
    return SOCKET_ERROR;
}

/*
 * @unimplemented
 */
INT
WSPAPI
WPUFDIsSet(IN SOCKET s,
           IN LPFD_SET set)
{
    UNIMPLEMENTED;
    return (SOCKET)0;
}

/*
 * @implemented
 */
INT
WSAAPI
WSAAsyncSelect(IN SOCKET s,
               IN HWND hWnd,
               IN UINT wMsg,
               IN LONG lEvent)
{
    PWSSOCKET Socket;
    INT Status;
    INT ErrorCode;
    DPRINT("WSAAsyncSelect: %lx, %lx, %lx, %lx\n", s, hWnd, wMsg, lEvent);

    /* Check for WSAStartup */
    if ((ErrorCode = WsQuickProlog()) == ERROR_SUCCESS)
    {
        /* Get the Socket Context */
        if ((Socket = WsSockGetSocket(s)))
        {
            /* Make the call */
            Status = Socket->Provider->Service.lpWSPAsyncSelect(s,
                                                                hWnd,
                                                                wMsg,
                                                                lEvent,
                                                                &ErrorCode);
            /* Deference the Socket Context */
            WsSockDereference(Socket);

            /* Return Provider Value */
            if (Status == ERROR_SUCCESS) return Status;

            /* If everything seemed fine, then the WSP call failed itself */
            if (ErrorCode == NO_ERROR) ErrorCode = WSASYSCALLFAILURE;
        }
        else
        {
            /* No Socket Context Found */
            ErrorCode = WSAENOTSOCK;
        }
    }

    /* Return with an Error */
    SetLastError(ErrorCode);
    return SOCKET_ERROR;
}

/*
 * @implemented
 */
INT
WSAAPI
WSAEventSelect(IN SOCKET s,
               IN WSAEVENT hEventObject,
               IN LONG lNetworkEvents)
{
    PWSSOCKET Socket;
    INT Status;
    INT ErrorCode;

    /* Check for WSAStartup */
    if ((ErrorCode = WsQuickProlog()) == ERROR_SUCCESS)
    {
        /* Get the Socket Context */
        if ((Socket = WsSockGetSocket(s)))
        {
            /* Make the call */
            Status = Socket->Provider->Service.lpWSPEventSelect(s,
                                                        hEventObject,
                                                        lNetworkEvents,
                                                        &ErrorCode);
            /* Deference the Socket Context */
            WsSockDereference(Socket);

            /* Return Provider Value */
            if (Status == ERROR_SUCCESS) return Status;
        }
        else
        {
            /* No Socket Context Found */
            ErrorCode = WSAENOTSOCK;
        }
    }

    /* Return with an Error */
    SetLastError(ErrorCode);
    return SOCKET_ERROR;
}

INT
WSAAPI
WSAPoll(IN OUT LPWSAPOLLFD fdArray,
        IN ULONG fds,
        IN INT timeout)
{
    PAFD_POLL_INFO PollInfo;
    IO_STATUS_BLOCK IoStatusBlock;
    PWSSOCKET Socket;
    SOCKET PollSocket = 0;
    HANDLE Event;
    NTSTATUS Status;
    SIZE_T PollSize;
    ULONG Index, Entry, Events;
    SHORT Revents;
    INT Result = 0;
    INT ErrorCode;

    ErrorCode = WsQuickProlog();
    if (ErrorCode != ERROR_SUCCESS)
    {
        SetLastError(ErrorCode);
        return SOCKET_ERROR;
    }

    if (fds == 0)
    {
        SetLastError(WSAEINVAL);
        return SOCKET_ERROR;
    }
    if (fdArray == NULL)
    {
        SetLastError(WSAEFAULT);
        return SOCKET_ERROR;
    }

    PollSize = FIELD_OFFSET(AFD_POLL_INFO, Handles) + (SIZE_T)fds * sizeof(AFD_HANDLE);
    PollInfo = HeapAlloc(WsSockHeap, HEAP_ZERO_MEMORY, PollSize);
    if (PollInfo == NULL)
    {
        SetLastError(WSAENOBUFS);
        return SOCKET_ERROR;
    }

    if (timeout >= 0)
        PollInfo->Timeout.QuadPart = (LONGLONG)timeout * -10000;
    else
        PollInfo->Timeout.QuadPart = MAXLONGLONG;

    for (Index = 0; Index < fds; Index++)
    {
        Socket = WsSockGetSocket(fdArray[Index].fd);
        if (Socket == NULL)
        {
            fdArray[Index].revents = POLLNVAL;
            continue;
        }
        WsSockDereference(Socket);

        Events = AFD_EVENT_DISCONNECT | AFD_EVENT_ABORT | AFD_EVENT_CONNECT_FAIL;
        if (fdArray[Index].events & POLLRDNORM)
            Events |= AFD_EVENT_ACCEPT | AFD_EVENT_RECEIVE;
        if (fdArray[Index].events & POLLRDBAND)
            Events |= AFD_EVENT_OOB_RECEIVE;
        if (fdArray[Index].events & POLLWRNORM)
            Events |= AFD_EVENT_SEND;

        PollSocket = fdArray[Index].fd;
        PollInfo->Handles[PollInfo->HandleCount].Handle = fdArray[Index].fd;
        PollInfo->Handles[PollInfo->HandleCount].Events = Events;
        PollInfo->HandleCount++;
        fdArray[Index].revents = 0;
    }

    if (PollInfo->HandleCount == 0)
    {
        HeapFree(WsSockHeap, 0, PollInfo);
        SetLastError(WSAENOTSOCK);
        return SOCKET_ERROR;
    }

    Status = NtCreateEvent(&Event, EVENT_ALL_ACCESS, NULL, SynchronizationEvent, FALSE);
    if (!NT_SUCCESS(Status))
    {
        HeapFree(WsSockHeap, 0, PollInfo);
        SetLastError(WSAENOBUFS);
        return SOCKET_ERROR;
    }

    PollSize = FIELD_OFFSET(AFD_POLL_INFO, Handles) + PollInfo->HandleCount * sizeof(AFD_HANDLE);
    Status = NtDeviceIoControlFile((HANDLE)PollSocket,
                                   Event,
                                   NULL,
                                   NULL,
                                   &IoStatusBlock,
                                   IOCTL_AFD_SELECT,
                                   PollInfo,
                                   (ULONG)PollSize,
                                   PollInfo,
                                   (ULONG)PollSize);
    if (Status == STATUS_PENDING)
    {
        while (WaitForSingleObjectEx(Event, INFINITE, TRUE) == WAIT_IO_COMPLETION);
        Status = IoStatusBlock.Status;
    }
    NtClose(Event);

    if (Status == STATUS_SUCCESS)
    {
        for (Index = 0; Index < fds; Index++)
        {
            for (Entry = 0; Entry < PollInfo->HandleCount; Entry++)
            {
                if (PollInfo->Handles[Entry].Handle != fdArray[Index].fd)
                    continue;

                Events = PollInfo->Handles[Entry].Events;
                Revents = 0;
                if (Events & (AFD_EVENT_ACCEPT | AFD_EVENT_RECEIVE))
                    Revents |= POLLRDNORM;
                if (Events & AFD_EVENT_OOB_RECEIVE)
                    Revents |= POLLRDBAND;
                if (Events & AFD_EVENT_SEND)
                    Revents |= POLLWRNORM;
                if (Events & (AFD_EVENT_ABORT | AFD_EVENT_DISCONNECT))
                    Revents |= POLLHUP;
                if (Events & (AFD_EVENT_ABORT | AFD_EVENT_CONNECT_FAIL))
                    Revents |= POLLERR;
                if (Events & AFD_EVENT_CLOSE)
                    Revents |= POLLNVAL;

                fdArray[Index].revents = Revents & (fdArray[Index].events | POLLHUP | POLLERR | POLLNVAL);
                if (fdArray[Index].revents)
                    Result++;
                break;
            }
        }
    }

    HeapFree(WsSockHeap, 0, PollInfo);

    if (Status != STATUS_SUCCESS && Status != STATUS_TIMEOUT)
    {
        SetLastError(Status == STATUS_CANCELLED ? WSA_OPERATION_ABORTED : WSAENOTSOCK);
        return SOCKET_ERROR;
    }

    return Result;
}
