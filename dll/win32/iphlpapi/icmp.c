/*
 * PROJECT:         ReactOS IP Helper API
 * LICENSE:         LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:         ICMP functions
 * COPYRIGHT:       2016 Tim Crawford (crawfxrd@gmail.com)
 *                  2019 Victor Perevertkin (victor.perevertkin@reactos.org)
 */

#include "iphlpapi_private.h"

#include <tcpip_undoc.h>

typedef struct _ICMP_ASYNC_CONTEXT
{
    HANDLE Event;
    HANDLE Thread;
    PIO_APC_ROUTINE ApcRoutine;
    PVOID ApcContext;
    IO_STATUS_BLOCK IoStatusBlock;
} ICMP_ASYNC_CONTEXT, *PICMP_ASYNC_CONTEXT;

static
VOID
CALLBACK
IcmpApcRoutine(
    _In_ ULONG_PTR Parameter)
{
    PICMP_ASYNC_CONTEXT Context = (PICMP_ASYNC_CONTEXT)Parameter;

    Context->ApcRoutine(Context->ApcContext, &Context->IoStatusBlock, 0);
    HeapFree(GetProcessHeap(), 0, Context);
}

static
VOID
CALLBACK
IcmpCompletionRoutine(
    _In_ DWORD ErrorCode,
    _In_ DWORD BytesTransferred,
    _In_ LPOVERLAPPED Overlapped)
{
    PICMP_ASYNC_CONTEXT Context = (PICMP_ASYNC_CONTEXT)Overlapped;
    HANDLE Thread;
    BOOL Queued;

    if (!Context)
        return;

    if (Context->Event)
    {
        SetEvent(Context->Event);
    }
    else if (Context->ApcRoutine)
    {
        Thread = Context->Thread;
        Context->Thread = NULL;
        Queued = QueueUserAPC(IcmpApcRoutine, Thread, (ULONG_PTR)Context);
        CloseHandle(Thread);
        if (Queued)
            return;
    }

    HeapFree(GetProcessHeap(), 0, Context);
}

static
PICMP_ASYNC_CONTEXT
IcmpCreateAsyncContext(
    _In_opt_ HANDLE Event,
    _In_opt_ PIO_APC_ROUTINE ApcRoutine,
    _In_opt_ PVOID ApcContext)
{
    PICMP_ASYNC_CONTEXT Context;

    Context = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*Context));
    if (!Context)
        return NULL;

    Context->Event = Event;
    Context->ApcRoutine = ApcRoutine;
    Context->ApcContext = ApcContext;
    if (Event == NULL &&
        !DuplicateHandle(GetCurrentProcess(), GetCurrentThread(), GetCurrentProcess(),
                         &Context->Thread, 0, FALSE, DUPLICATE_SAME_ACCESS))
    {
        HeapFree(GetProcessHeap(), 0, Context);
        return NULL;
    }

    return Context;
}

static
VOID
IcmpFreeAsyncContext(
    _In_ PICMP_ASYNC_CONTEXT Context)
{
    if (Context->Thread)
        CloseHandle(Context->Thread);
    HeapFree(GetProcessHeap(), 0, Context);
}

HANDLE
WINAPI
Icmp6CreateFile(void)
{
    HANDLE IcmpFile;
    OBJECT_ATTRIBUTES ObjectAttributes;
    IO_STATUS_BLOCK IoStatusBlock;
    UNICODE_STRING DeviceName = RTL_CONSTANT_STRING(L"\\Device\\Ip6");
    NTSTATUS Status;

    InitializeObjectAttributes(
        &ObjectAttributes,
        &DeviceName,
        OBJ_CASE_INSENSITIVE,
        NULL,
        NULL);

    Status = NtCreateFile(
        &IcmpFile,
        GENERIC_EXECUTE,
        &ObjectAttributes,
        &IoStatusBlock,
        NULL,
        FILE_ATTRIBUTE_NORMAL,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        FILE_OPEN_IF,
        0,
        NULL,
        0);

    if (!NT_SUCCESS(Status))
    {
        SetLastError(RtlNtStatusToDosError(Status));
        return INVALID_HANDLE_VALUE;
    }

    if (!BindIoCompletionCallback(IcmpFile, IcmpCompletionRoutine, 0))
    {
        DWORD Error = GetLastError();

        NtClose(IcmpFile);
        SetLastError(Error);
        return INVALID_HANDLE_VALUE;
    }

    return IcmpFile;
}

DWORD
WINAPI
Icmp6ParseReplies(
    _In_ LPVOID ReplyBuffer,
    _In_ DWORD  ReplySize)
{
    PICMPV6_ECHO_REPLY pEcho;

    if (ReplyBuffer == NULL || ReplySize == 0)
        return 0;

    pEcho = (PICMPV6_ECHO_REPLY)ReplyBuffer;

    // XXX: MSDN also says IP_TTL_EXPIRED_TRANSIT.
    if (pEcho->Status == IP_SUCCESS)
    {
        return 1;
    }

    SetLastError(pEcho->Status);
    return 0;
}

DWORD
WINAPI
Icmp6SendEcho2(
    _In_     HANDLE                 IcmpHandle,
    _In_opt_ HANDLE                 Event,
    _In_opt_ PIO_APC_ROUTINE        ApcRoutine,
    _In_opt_ PVOID                  ApcContext,
    _In_     struct sockaddr_in6    *SourceAddress,
    _In_     struct sockaddr_in6    *DestinationAddress,
    _In_     LPVOID                 RequestData,
    _In_     WORD                   RequestSize,
    _In_opt_ PIP_OPTION_INFORMATION RequestOptions,
    _Out_    LPVOID                 ReplyBuffer,
    _In_     DWORD                  ReplySize,
    _In_     DWORD                  Timeout)
{
    PICMP_ASYNC_CONTEXT Context = NULL;
    IO_STATUS_BLOCK LocalIoStatusBlock;
    PIO_STATUS_BLOCK IoStatusBlock;
    HANDLE hEvent = NULL;
    PVOID InputBuffer;
    ULONG InputBufferLength;
    PICMPV6_ECHO_REQUEST Request;
    NTSTATUS Status;

    if (IcmpHandle == NULL || IcmpHandle == INVALID_HANDLE_VALUE || !ReplyBuffer)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return 0;
    }

    if (ReplySize < sizeof(ICMPV6_ECHO_REPLY))
    {
        SetLastError(IP_BUF_TOO_SMALL);
        return 0;
    }

    InputBufferLength = sizeof(ICMPV6_ECHO_REQUEST) + RequestSize;
    InputBuffer = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, InputBufferLength);
    if (InputBuffer == NULL)
    {
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return 0;
    }

    Request = (PICMPV6_ECHO_REQUEST)InputBuffer;

    Request->DestinationAddress.sin6_port = DestinationAddress->sin6_port;
    Request->DestinationAddress.sin6_flowinfo = DestinationAddress->sin6_flowinfo;
    CopyMemory(&(Request->DestinationAddress.sin6_addr), &(DestinationAddress->sin6_addr), sizeof(Request->DestinationAddress.sin6_addr));
    Request->DestinationAddress.sin6_scope_id = DestinationAddress->sin6_scope_id;

    Request->SourceAddress.sin6_port = SourceAddress->sin6_port;
    Request->SourceAddress.sin6_flowinfo = SourceAddress->sin6_flowinfo;
    CopyMemory(&(Request->SourceAddress.sin6_addr), &(SourceAddress->sin6_addr), sizeof(Request->SourceAddress.sin6_addr));
    Request->SourceAddress.sin6_scope_id = SourceAddress->sin6_scope_id;

    Request->Timeout = Timeout;
    if (RequestOptions != NULL)
    {
        Request->Ttl = RequestOptions->Ttl;
        Request->Flags = RequestOptions->Flags;
    }
    else
    {
        Request->Ttl = (UINT16)-1;
    }

    if (RequestSize > 0)
    {
        CopyMemory((PBYTE)InputBuffer + sizeof(ICMPV6_ECHO_REQUEST), RequestData, RequestSize);
    }

    if (Event != NULL || ApcRoutine != NULL)
    {
        Context = IcmpCreateAsyncContext(Event, ApcRoutine, ApcContext);
        if (!Context)
        {
            HeapFree(GetProcessHeap(), 0, InputBuffer);
            SetLastError(ERROR_NOT_ENOUGH_MEMORY);
            return 0;
        }
        IoStatusBlock = &Context->IoStatusBlock;
    }
    else
    {
        hEvent = CreateEventW(NULL, FALSE, FALSE, NULL);
        if (!hEvent)
        {
            HeapFree(GetProcessHeap(), 0, InputBuffer);
            return 0;
        }
        IoStatusBlock = &LocalIoStatusBlock;
    }

    Status = NtDeviceIoControlFile(
        IcmpHandle,
        hEvent,
        NULL,
        Context,
        IoStatusBlock,
        IOCTL_ICMPV6_ECHO_REQUEST,
        InputBuffer,
        InputBufferLength,
        ReplyBuffer,
        ReplySize);         // TODO: Determine how Windows calculates OutputBufferLength.

    HeapFree(GetProcessHeap(), 0, InputBuffer);

    if (Context != NULL)
    {
        if (Status != STATUS_PENDING)
            IcmpFreeAsyncContext(Context);
        SetLastError(RtlNtStatusToDosError(Status));
        return 0;
    }

    if (Status == STATUS_PENDING)
    {
        Status = NtWaitForSingleObject(hEvent, FALSE, NULL);

        if (NT_SUCCESS(Status))
        {
            Status = IoStatusBlock->Status;
        }
    }

    CloseHandle(hEvent);

    if (!NT_SUCCESS(Status) && IoStatusBlock->Information < sizeof(ICMPV6_ECHO_REPLY))
    {
        SetLastError(RtlNtStatusToDosError(Status));
        return 0;
    }

    Status = ((PICMPV6_ECHO_REPLY)ReplyBuffer)->Status;
    if (Status != IP_SUCCESS)
    {
        SetLastError(Status);
        return 0;
    }

    return 1;
}

BOOL
WINAPI
IcmpCloseHandle(
    _In_ HANDLE IcmpHandle)
{
    NTSTATUS Status;

    if (IcmpHandle == NULL || IcmpHandle == INVALID_HANDLE_VALUE)
    {
        SetLastError(ERROR_INVALID_HANDLE);
        return FALSE;
    }

    Status = NtClose(IcmpHandle);
    if (!NT_SUCCESS(Status))
    {
        SetLastError(RtlNtStatusToDosError(Status));
        return FALSE;
    }

    return TRUE;
}

HANDLE
WINAPI
IcmpCreateFile(void)
{
    HANDLE IcmpFile;
    OBJECT_ATTRIBUTES ObjectAttributes;
    IO_STATUS_BLOCK IoStatusBlock;
    UNICODE_STRING DeviceName = RTL_CONSTANT_STRING(L"\\Device\\Ip");
    NTSTATUS Status;

    InitializeObjectAttributes(
        &ObjectAttributes,
        &DeviceName,
        OBJ_CASE_INSENSITIVE,
        NULL,
        NULL);

    Status = NtCreateFile(
        &IcmpFile,
        GENERIC_EXECUTE,
        &ObjectAttributes,
        &IoStatusBlock,
        NULL,
        FILE_ATTRIBUTE_NORMAL,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        FILE_OPEN_IF,
        0,
        NULL,
        0);

    if (!NT_SUCCESS(Status))
    {
        SetLastError(RtlNtStatusToDosError(Status));
        return INVALID_HANDLE_VALUE;
    }

    if (!BindIoCompletionCallback(IcmpFile, IcmpCompletionRoutine, 0))
    {
        DWORD Error = GetLastError();

        NtClose(IcmpFile);
        SetLastError(Error);
        return INVALID_HANDLE_VALUE;
    }

    return IcmpFile;
}

DWORD
WINAPI
IcmpParseReplies(
    _In_ LPVOID ReplyBuffer,
    _In_ DWORD  ReplySize)
{
    PICMP_ECHO_REPLY pEcho;
    DWORD nReplies;

    if (ReplyBuffer == NULL || ReplySize == 0)
        return 0;

    // TODO: Handle ReplyBuffer having more than 1 ICMP_ECHO_REPLY.

    pEcho = (PICMP_ECHO_REPLY)ReplyBuffer;

    if (pEcho->Reserved == 0)
    {
        SetLastError(pEcho->Status);
    }

    nReplies = pEcho->Reserved;
    pEcho->Reserved = 0;

    return nReplies;
}

DWORD
WINAPI
IcmpSendEcho2(
    _In_     HANDLE                 IcmpHandle,
    _In_opt_ HANDLE                 Event,
    _In_opt_ PIO_APC_ROUTINE        ApcRoutine,
    _In_opt_ PVOID                  ApcContext,
    _In_     IPAddr                 DestinationAddress,
    _In_     LPVOID                 RequestData,
    _In_     WORD                   RequestSize,
    _In_opt_ PIP_OPTION_INFORMATION RequestOptions,
    _Out_    LPVOID                 ReplyBuffer,
    _In_     DWORD                  ReplySize,
    _In_     DWORD                  Timeout)
{
    PICMP_ASYNC_CONTEXT Context = NULL;
    IO_STATUS_BLOCK LocalIoStatusBlock;
    PIO_STATUS_BLOCK IoStatusBlock;
    HANDLE hEvent = NULL;
    PVOID InputBuffer;
    PICMP_ECHO_REQUEST Request;
    DWORD nReplies;
    NTSTATUS Status;

    if (ReplySize < sizeof(ICMP_ECHO_REPLY) + sizeof(IO_STATUS_BLOCK))
    {
        SetLastError(ERROR_INSUFFICIENT_BUFFER);
        return 0;
    }

    if (ReplySize < RequestSize + sizeof(ICMP_ECHO_REPLY))
    {
        SetLastError(IP_GENERAL_FAILURE);
        return 0;
    }

    InputBuffer = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, ReplySize);
    if (InputBuffer == NULL)
    {
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return 0;
    }

    Request = (PICMP_ECHO_REQUEST)InputBuffer;
    Request->Address = DestinationAddress;
    Request->Timeout = Timeout;
    Request->OptionsOffset = sizeof(ICMP_ECHO_REQUEST);
    Request->DataOffset = sizeof(ICMP_ECHO_REQUEST);

    if (RequestOptions != NULL)
    {
        Request->HasOptions = TRUE;
        Request->Ttl = RequestOptions->Ttl;
        Request->Tos = RequestOptions->Tos;
        Request->Flags = RequestOptions->Flags;

        if (RequestOptions->OptionsSize > 0)
        {
            Request->OptionsSize = RequestOptions->OptionsSize;
            Request->DataOffset += Request->OptionsSize;

            CopyMemory(
                (PUCHAR)InputBuffer + sizeof(ICMP_ECHO_REQUEST),
                RequestOptions->OptionsData,
                Request->OptionsSize);
        }
    }

    if (RequestSize > 0)
    {
        Request->DataSize = RequestSize;
        CopyMemory((PUCHAR)InputBuffer + Request->DataOffset, RequestData, RequestSize);
    }

    if (Event != NULL || ApcRoutine != NULL)
    {
        Context = IcmpCreateAsyncContext(Event, ApcRoutine, ApcContext);
        if (!Context)
        {
            HeapFree(GetProcessHeap(), 0, InputBuffer);
            SetLastError(ERROR_NOT_ENOUGH_MEMORY);
            return 0;
        }
        IoStatusBlock = &Context->IoStatusBlock;
    }
    else
    {
        hEvent = CreateEventW(NULL, FALSE, FALSE, NULL);
        if (!hEvent)
        {
            HeapFree(GetProcessHeap(), 0, InputBuffer);
            return 0;
        }
        IoStatusBlock = &LocalIoStatusBlock;
    }

    Status = NtDeviceIoControlFile(
        IcmpHandle,
        hEvent,
        NULL,
        Context,
        IoStatusBlock,
        IOCTL_ICMP_ECHO_REQUEST,
        InputBuffer,
        ReplySize,
        ReplyBuffer,
        ReplySize);         // TODO: Determine how Windows calculates OutputBufferLength.

    if (Context != NULL)
    {
        if (Status != STATUS_PENDING)
            IcmpFreeAsyncContext(Context);
        SetLastError(RtlNtStatusToDosError(Status));
        HeapFree(GetProcessHeap(), 0, InputBuffer);
        return 0;
    }

    // Otherwise handle it like IcmpSendEcho.
    if (Status == STATUS_PENDING)
    {
        Status = NtWaitForSingleObject(hEvent, FALSE, NULL);

        if (NT_SUCCESS(Status))
        {
            Status = IoStatusBlock->Status;
        }
    }

    CloseHandle(hEvent);
    HeapFree(GetProcessHeap(), 0, InputBuffer);

    if (!NT_SUCCESS(Status))
    {
        SetLastError(RtlNtStatusToDosError(Status));
        return 0;
    }

    Status = ((PICMP_ECHO_REPLY)ReplyBuffer)->Status;
    if (Status != IP_SUCCESS)
    {
        SetLastError(Status);
    }

    nReplies = ((PICMP_ECHO_REPLY)ReplyBuffer)->Reserved;
    ((PICMP_ECHO_REPLY)ReplyBuffer)->Reserved = 0;

    return nReplies;
}

#ifdef __REACTOS__
DWORD WINAPI IcmpSendEcho2Ex(HANDLE IcmpHandle, HANDLE Event, PIO_APC_ROUTINE ApcRoutine, PVOID ApcContext, IPAddr SourceAddress, IPAddr DestinationAddress, LPVOID RequestData, WORD RequestSize, PIP_OPTION_INFORMATION RequestOptions, LPVOID ReplyBuffer, DWORD ReplySize, DWORD Timeout)
{
    if (SourceAddress != INADDR_ANY)
    {
        SetLastError(ERROR_INVALID_NETNAME);
        return 0;
    }

    return IcmpSendEcho2(IcmpHandle, Event, ApcRoutine, ApcContext, DestinationAddress, RequestData, RequestSize, RequestOptions, ReplyBuffer, ReplySize, Timeout);
}
#endif

DWORD
WINAPI
IcmpSendEcho(
    _In_     HANDLE                 IcmpHandle,
    _In_     IPAddr                 DestinationAddress,
    _In_     LPVOID                 RequestData,
    _In_     WORD                   RequestSize,
    _In_opt_ PIP_OPTION_INFORMATION RequestOptions,
    _Out_    LPVOID                 ReplyBuffer,
    _In_     DWORD                  ReplySize,
    _In_     DWORD                  Timeout)
{
    HANDLE hEvent;
    IO_STATUS_BLOCK IoStatusBlock;
    PVOID InputBuffer;
    ULONG InputBufferLength;
    PICMP_ECHO_REQUEST Request;
    DWORD nReplies;
    NTSTATUS Status;

    if (Timeout == 0 || Timeout == (DWORD)-1)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return 0;
    }

    if (ReplySize < sizeof(ICMP_ECHO_REPLY))
    {
        SetLastError(ERROR_INSUFFICIENT_BUFFER);
        return 0;
    }

    if (ReplySize < RequestSize + sizeof(ICMP_ECHO_REPLY))
    {
        SetLastError(IP_GENERAL_FAILURE);
        return 0;
    }

    InputBufferLength = sizeof(ICMP_ECHO_REQUEST) + RequestSize;
    if (RequestOptions != NULL)
        InputBufferLength += RequestOptions->OptionsSize;

    if (InputBufferLength < ReplySize)
        InputBufferLength = ReplySize;

    InputBuffer = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, InputBufferLength);
    if (InputBuffer == NULL)
    {
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return 0;
    }

    Request = (PICMP_ECHO_REQUEST)InputBuffer;
    Request->Address = DestinationAddress;
    Request->Timeout = Timeout;
    Request->OptionsOffset = sizeof(ICMP_ECHO_REQUEST);
    Request->DataOffset = sizeof(ICMP_ECHO_REQUEST);

    if (RequestOptions != NULL)
    {
        Request->HasOptions = TRUE;
        Request->Ttl = RequestOptions->Ttl;
        Request->Tos = RequestOptions->Tos;
        Request->Flags = RequestOptions->Flags;

        if (RequestOptions->OptionsSize > 0)
        {
            Request->OptionsSize = RequestOptions->OptionsSize;
            Request->DataOffset += Request->OptionsSize;

            CopyMemory(
                (PUCHAR)InputBuffer + sizeof(ICMP_ECHO_REQUEST),
                RequestOptions->OptionsData,
                Request->OptionsSize);
        }
    }

    if (RequestSize > 0)
    {
        Request->DataSize = RequestSize;
        CopyMemory((PUCHAR)InputBuffer + Request->DataOffset, RequestData, RequestSize);
    }

    hEvent = CreateEventW(NULL, FALSE, FALSE, NULL);
    if (hEvent == NULL)
    {
        HeapFree(GetProcessHeap(), 0, InputBuffer);
        return 0;
    }

    Status = NtDeviceIoControlFile(
        IcmpHandle,
        hEvent,
        NULL,
        NULL,
        &IoStatusBlock,
        IOCTL_ICMP_ECHO_REQUEST,
        InputBuffer,
        InputBufferLength,
        ReplyBuffer,
        ReplySize);

    if (Status == STATUS_PENDING)
    {
        Status = NtWaitForSingleObject(hEvent, FALSE, NULL);

        if (NT_SUCCESS(Status))
        {
            Status = IoStatusBlock.Status;
        }
    }

    CloseHandle(hEvent);
    HeapFree(GetProcessHeap(), 0, InputBuffer);

    if (!NT_SUCCESS(Status))
    {
        SetLastError(RtlNtStatusToDosError(Status));
        return 0;
    }

    Status = ((PICMP_ECHO_REPLY)ReplyBuffer)->Status;
    if (Status != IP_SUCCESS)
    {
        SetLastError(Status);
    }

    nReplies = ((PICMP_ECHO_REPLY)ReplyBuffer)->Reserved;
    ((PICMP_ECHO_REPLY)ReplyBuffer)->Reserved = 0;

    return nReplies;
}
