/*
 * PROJECT:     LiberNT HTTP.sys driver
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     Core implementation of HTTP.sys protocol stack driver
 * COPYRIGHT:   Copyright 2019 Zebediah Figura
 *              Copyright 2026 Ostap Brehin <osbre@protonmail.com>
 */

#include "http_sys.h"

#define NDEBUG
#include <reactos/debug.h>

static HANDLE g_HttpDirectoryHandle;
static PDEVICE_OBJECT g_ReqQueueDeviceObject;

static FAST_MUTEX g_HttpGlobalLock;
static LIST_ENTRY g_RequestQueuesList;
static LIST_ENTRY g_ConnectionsList;
static LIST_ENTRY g_ListenersList;
static LIST_ENTRY g_ListenerCallbackList;
static KSPIN_LOCK g_ListenerCallbackLock;
static LONG g_ListenerCallbackId;

static HTTP_REQUEST_ID g_RequestIdCounter;
static WSK_REGISTRATION g_WskRegistration;
static const WSK_CLIENT_DISPATCH g_WskClientDispatch =
{
    .Version = MAKE_WSK_VERSION(1, 0),
};
static WSK_CLIENT_NPI g_WskClientNpi;
static WSK_PROVIDER_NPI g_WskProviderNpi;
static BOOLEAN g_WskRegistered;
static BOOLEAN g_WskCaptured;
static LONG g_ConnectionCount;
static ULONG g_MaxConnections = MAXULONG;
static LIST_ENTRY g_GroupConnectionsList;
static KSPIN_LOCK g_GroupConnectionsLock;
static llhttp_settings_t g_LlhttpSettings;
static KTIMER g_TimeoutTimer;
static KDPC g_TimeoutDpc;
static PIO_WORKITEM g_TimeoutWorkItem;
static LONG g_TimeoutWorkQueued;
static HANDLE g_ParametersKey;
static LONG g_IdleConnectionSeconds = HTTP_DEFAULT_TIMEOUT_SECONDS;
static LONG g_HeaderWaitSeconds = HTTP_DEFAULT_TIMEOUT_SECONDS;
static IO_CSQ g_DisconnectCsq;
static LIST_ENTRY g_DisconnectIrpQueue;
static KSPIN_LOCK g_DisconnectIrpLock;

typedef struct _HTTP_DISCONNECT_PEEK
{
    PHTTP_CONNECTION Connection;
    PHTTP_REQUEST_QUEUE Queue;
} HTTP_DISCONNECT_PEEK, *PHTTP_DISCONNECT_PEEK;

static VOID NTAPI HttpConnectionWorker(PDEVICE_OBJECT DeviceObject, PVOID Context);
static VOID HttpConnectionCloseSocket(PHTTP_CONNECTION Connection);
static VOID HttpConnectionReference(PHTTP_CONNECTION Connection);
static VOID HttpConnectionDereference(PHTTP_CONNECTION Connection);
static VOID HttpListenerDereference(PHTTP_LISTENER Listener);
static VOID HttpCloseListener(PHTTP_LISTENER Listener);
static VOID HttpRemoveListenerConnection(PHTTP_CONNECTION Connection);
static BOOLEAN HttpWantsBody(PHTTP_CONNECTION Connection);
static NTSTATUS HttpStartBodyReceive(PHTTP_CONNECTION Connection);
static VOID HttpRejectBody(PHTTP_CONNECTION Connection);

static VOID HttpReleaseWsk(VOID)
{
    if (g_WskCaptured)
    {
        WskReleaseProviderNPI(&g_WskRegistration);
        g_WskCaptured = FALSE;
    }

    if (g_WskRegistered)
    {
        WskDeregister(&g_WskRegistration);
        g_WskRegistered = FALSE;
    }
}

static NTSTATUS NTAPI HttpSyncCompletion(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp,
    _In_ PVOID Context)
{
    KeSetEvent((PKEVENT)Context, IO_NO_INCREMENT, FALSE);
    return STATUS_MORE_PROCESSING_REQUIRED;
}

static NTSTATUS HttpWaitForIrp(PIRP Irp, PKEVENT Event, NTSTATUS Status)
{
    if (Status == STATUS_PENDING)
    {
        KeWaitForSingleObject(Event, Executive, KernelMode, FALSE, NULL);
        Status = Irp->IoStatus.Status;
    }

    IoFreeIrp(Irp);
    return Status;
}

static NTSTATUS HttpCreateSocket(
    _In_ ADDRESS_FAMILY AddressFamily,
    _In_ USHORT SocketType,
    _In_ ULONG Protocol,
    _In_ ULONG Flags,
    _In_opt_ PVOID SocketContext,
    _In_opt_ const VOID *Dispatch,
    _Out_ PWSK_SOCKET *Socket)
{
    PIRP Irp;
    KEVENT Event;
    NTSTATUS Status;

    Irp = IoAllocateIrp(1, FALSE);
    if (!Irp)
        return STATUS_INSUFFICIENT_RESOURCES;

    KeInitializeEvent(&Event, NotificationEvent, FALSE);
    IoSetCompletionRoutine(Irp, HttpSyncCompletion, &Event, TRUE, TRUE, TRUE);
    Status = g_WskProviderNpi.Dispatch->WskSocket(
        g_WskProviderNpi.Client,
        AddressFamily,
        SocketType,
        Protocol,
        Flags,
        SocketContext,
        Dispatch,
        NULL,
        NULL,
        NULL,
        Irp);
    if (Status == STATUS_PENDING)
    {
        KeWaitForSingleObject(&Event, Executive, KernelMode, FALSE, NULL);
        Status = Irp->IoStatus.Status;
    }
    if (NT_SUCCESS(Status))
        *Socket = (PWSK_SOCKET)Irp->IoStatus.Information;

    IoFreeIrp(Irp);
    return Status;
}

static NTSTATUS HttpBindSocket(PWSK_SOCKET Socket, PSOCKADDR Address)
{
    PWSK_PROVIDER_LISTEN_DISPATCH Dispatch = (PWSK_PROVIDER_LISTEN_DISPATCH)Socket->Dispatch;
    PIRP Irp;
    KEVENT Event;
    NTSTATUS Status;

    Irp = IoAllocateIrp(1, FALSE);
    if (!Irp)
        return STATUS_INSUFFICIENT_RESOURCES;

    KeInitializeEvent(&Event, NotificationEvent, FALSE);
    IoSetCompletionRoutine(Irp, HttpSyncCompletion, &Event, TRUE, TRUE, TRUE);
    Status = Dispatch->WskBind(Socket, Address, 0, Irp);
    return HttpWaitForIrp(Irp, &Event, Status);
}

static NTSTATUS HttpEnableAccepts(PWSK_SOCKET Socket)
{
    PWSK_PROVIDER_LISTEN_DISPATCH Dispatch = (PWSK_PROVIDER_LISTEN_DISPATCH)Socket->Dispatch;
    WSK_EVENT_CALLBACK_CONTROL Control =
    {
        .NpiId = (PNPIID)&NPI_WSK_INTERFACE_ID,
        .EventMask = WSK_EVENT_ACCEPT,
    };
    PIRP Irp;
    KEVENT Event;
    NTSTATUS Status;

    Irp = IoAllocateIrp(1, FALSE);
    if (!Irp)
        return STATUS_INSUFFICIENT_RESOURCES;

    KeInitializeEvent(&Event, NotificationEvent, FALSE);
    IoSetCompletionRoutine(Irp, HttpSyncCompletion, &Event, TRUE, TRUE, TRUE);
    Status = Dispatch->WskControlSocket(
        Socket,
        WskSetOption,
        SO_WSK_EVENT_CALLBACK,
        SOL_SOCKET,
        sizeof(Control),
        &Control,
        0,
        NULL,
        NULL,
        Irp);
    return HttpWaitForIrp(Irp, &Event, Status);
}

static NTSTATUS HttpCloseSocket(PWSK_SOCKET Socket, PIRP Irp)
{
    PWSK_PROVIDER_BASIC_DISPATCH Dispatch;
    KEVENT Event;
    NTSTATUS Status;

    if (!Socket)
        return STATUS_SUCCESS;

    Dispatch = (PWSK_PROVIDER_BASIC_DISPATCH)Socket->Dispatch;
    IoReuseIrp(Irp, STATUS_UNSUCCESSFUL);
    KeInitializeEvent(&Event, NotificationEvent, FALSE);
    IoSetCompletionRoutine(Irp, HttpSyncCompletion, &Event, TRUE, TRUE, TRUE);
    Status = Dispatch->WskCloseSocket(Socket, Irp);
    if (Status == STATUS_PENDING)
    {
        KeWaitForSingleObject(&Event, Executive, KernelMode, FALSE, NULL);
        Status = Irp->IoStatus.Status;
    }
    return Status;
}

static LONG HttpCurrentSeconds(VOID)
{
    return (LONG)(KeQueryInterruptTime() / 10000000);
}

static VOID HttpSetDeadline(PHTTP_CONNECTION Connection, LONG Seconds)
{
    InterlockedExchange(&Connection->DeadlineSeconds, HttpCurrentSeconds() + Seconds);
}

static LONG HttpHeaderWaitSeconds(PHTTP_CONNECTION Connection)
{
    if (Connection->HeaderWaitSeconds)
        return Connection->HeaderWaitSeconds;
    return InterlockedCompareExchange(&g_HeaderWaitSeconds, 0, 0);
}

static VOID HttpReleaseGroupConnection(PHTTP_CONNECTION Connection)
{
    PHTTP_GROUP_CONNECTIONS Counter = Connection->GroupConnections;
    KIRQL Irql;

    if (!Counter)
        return;
    Connection->GroupConnections = NULL;
    KeAcquireSpinLock(&g_GroupConnectionsLock, &Irql);
    if (--Counter->Count)
        Counter = NULL;
    else
        RemoveEntryList(&Counter->Entry);
    KeReleaseSpinLock(&g_GroupConnectionsLock, Irql);
    if (Counter)
        ExFreePoolWithTag(Counter, TAG_HTTP);
}

static BOOLEAN HttpCountGroupConnection(PHTTP_CONNECTION Connection, PHTTP_REQUEST_QUEUE Queue,
                                        PHTTP_URL_ENTRY Url)
{
    PHTTP_GROUP_CONNECTIONS Counter = Connection->GroupConnections, New;
    PLIST_ENTRY Entry;
    BOOLEAN Counted = FALSE;
    KIRQL Irql;

    if (Counter && Counter->Queue == Queue && Counter->Group == Url->Group)
        return TRUE;

    New = ExAllocatePoolWithTag(NonPagedPoolNx, sizeof(*New), TAG_HTTP);
    if (!New)
        return FALSE;
    New->Queue = Queue;
    New->Group = Url->Group;
    New->Count = 0;

    KeAcquireSpinLock(&g_GroupConnectionsLock, &Irql);
    for (Entry = g_GroupConnectionsList.Flink; Entry != &g_GroupConnectionsList; Entry = Entry->Flink)
    {
        Counter = CONTAINING_RECORD(Entry, HTTP_GROUP_CONNECTIONS, Entry);
        if (Counter->Queue == Queue && Counter->Group == Url->Group)
            break;
    }
    if (Entry == &g_GroupConnectionsList)
    {
        Counter = NULL;
        if (Url->Config.max_connections)
        {
            InsertTailList(&g_GroupConnectionsList, &New->Entry);
            Counter = New;
            New = NULL;
        }
    }
    if (Counter && Counter->Count < Url->Config.max_connections)
    {
        ++Counter->Count;
        Counted = TRUE;
    }
    KeReleaseSpinLock(&g_GroupConnectionsLock, Irql);

    if (New)
        ExFreePoolWithTag(New, TAG_HTTP);
    if (!Counted)
        return FALSE;
    HttpReleaseGroupConnection(Connection);
    Connection->GroupConnections = Counter;
    return TRUE;
}

static VOID HttpFreeConnection(PHTTP_CONNECTION Connection)
{
    HttpReleaseGroupConnection(Connection);
    if (Connection->WorkItem)
        IoFreeWorkItem(Connection->WorkItem);
    if (Connection->ReceiveIrp)
        IoFreeIrp(Connection->ReceiveIrp);
    if (Connection->CloseIrp)
        IoFreeIrp(Connection->CloseIrp);
    if (Connection->Buffer)
        ExFreePoolWithTag(Connection->Buffer, TAG_HTTP);
    ExFreePoolWithTag(Connection, TAG_HTTP);
}

static NTSTATUS WSKAPI HttpAcceptEvent(
    _In_opt_ PVOID SocketContext,
    _In_ ULONG Flags,
    _In_ PSOCKADDR LocalAddress,
    _In_ PSOCKADDR RemoteAddress,
    _In_opt_ PWSK_SOCKET AcceptSocket,
    _Outptr_result_maybenull_ PVOID *AcceptSocketContext,
    _Outptr_result_maybenull_ const WSK_CLIENT_CONNECTION_DISPATCH **AcceptSocketDispatch)
{
    PHTTP_CONNECTION Connection;
    PHTTP_LISTENER Listener = NULL;
    KIRQL Irql;

    UNREFERENCED_PARAMETER(Flags);

    if (!SocketContext || !LocalAddress || !RemoteAddress || !AcceptSocket)
        return STATUS_REQUEST_NOT_ACCEPTED;

    if (LocalAddress->sa_family != AF_INET)
        return STATUS_REQUEST_NOT_ACCEPTED;

    KeAcquireSpinLock(&g_ListenerCallbackLock, &Irql);
    for (PLIST_ENTRY Entry = g_ListenerCallbackList.Flink;
         Entry != &g_ListenerCallbackList;
         Entry = Entry->Flink)
    {
        PHTTP_LISTENER Candidate =
            CONTAINING_RECORD(Entry, HTTP_LISTENER, CallbackEntry);
        if ((ULONG_PTR)Candidate->CallbackId == (ULONG_PTR)SocketContext &&
            Candidate->Port == RtlUshortByteSwap(((PSOCKADDR_IN)LocalAddress)->sin_port))
        {
            Listener = Candidate;
            InterlockedIncrement(&Listener->RefCount);
            break;
        }
    }
    KeReleaseSpinLock(&g_ListenerCallbackLock, Irql);
    if (!Listener)
        return STATUS_REQUEST_NOT_ACCEPTED;

    if ((ULONG)InterlockedIncrement(&g_ConnectionCount) > g_MaxConnections)
    {
        InterlockedDecrement(&g_ConnectionCount);
        HttpListenerDereference(Listener);
        return STATUS_REQUEST_NOT_ACCEPTED;
    }

    Connection = ExAllocatePoolZero(NonPagedPoolNx, sizeof(*Connection), TAG_HTTP);
    if (!Connection)
    {
        InterlockedDecrement(&g_ConnectionCount);
        HttpListenerDereference(Listener);
        return STATUS_REQUEST_NOT_ACCEPTED;
    }

    Connection->BufferCapacity = HTTP_MAX_HEADER_SIZE + 1;
    Connection->Buffer = ExAllocatePoolZero(NonPagedPoolNx, Connection->BufferCapacity, TAG_HTTP);
    Connection->WorkItem = IoAllocateWorkItem(g_ReqQueueDeviceObject);
    Connection->ReceiveIrp = IoAllocateIrp(1, FALSE);
    Connection->CloseIrp = IoAllocateIrp(1, FALSE);
    if (!Connection->Buffer || !Connection->WorkItem ||
        !Connection->ReceiveIrp || !Connection->CloseIrp)
    {
        HttpFreeConnection(Connection);
        InterlockedDecrement(&g_ConnectionCount);
        HttpListenerDereference(Listener);
        return STATUS_REQUEST_NOT_ACCEPTED;
    }

    Connection->Socket = AcceptSocket;
    Connection->Listener = Listener;
    Connection->RequestId = InterlockedIncrement64((LONG64 *)&g_RequestIdCounter);
    Connection->ConnectionId = Connection->RequestId;
    Connection->RefCount = 1;
    ExInitializeFastMutex(&Connection->ResponseMutex);
    ExInitializeFastMutex(&Connection->SocketMutex);
    memcpy(&Connection->LocalAddress, LocalAddress, sizeof(Connection->LocalAddress));
    memcpy(&Connection->RemoteAddress, RemoteAddress, sizeof(Connection->RemoteAddress));
    llhttp_init(&Connection->Parser, LLHTTP_REQUEST, &g_LlhttpSettings);
    llhttp_set_lenient_chunked_length(&Connection->Parser, 1);
    Connection->Parser.data = Connection;
    Connection->ErrorStatus = 400;
    HttpSetDeadline(Connection, InterlockedCompareExchange(&g_IdleConnectionSeconds, 0, 0));

    KeAcquireSpinLock(&Listener->PendingConnectionLock, &Irql);
    if (Listener->Closing)
    {
        KeReleaseSpinLock(&Listener->PendingConnectionLock, Irql);
        HttpFreeConnection(Connection);
        InterlockedDecrement(&g_ConnectionCount);
        HttpListenerDereference(Listener);
        return STATUS_REQUEST_NOT_ACCEPTED;
    }

    InsertTailList(&Listener->PendingConnections, &Connection->ListenerEntry);
    Connection->InListenerList = TRUE;
    HttpConnectionReference(Connection);
    KeReleaseSpinLock(&Listener->PendingConnectionLock, Irql);

    *AcceptSocketContext = Connection;
    *AcceptSocketDispatch = NULL;
    IoQueueWorkItem(Connection->WorkItem, HttpConnectionWorker, DelayedWorkQueue, Connection);
    return STATUS_SUCCESS;
}

static const WSK_CLIENT_LISTEN_DISPATCH g_HttpListenDispatch =
{
    .WskAcceptEvent = HttpAcceptEvent,
};

static NTSTATUS HttpCreateListener(USHORT Port, PHTTP_LISTENER *Listener)
{
    PHTTP_LISTENER NewListener;
    SOCKADDR_IN Address;
    NTSTATUS Status;

    NewListener = ExAllocatePoolZero(NonPagedPoolNx, sizeof(*NewListener), TAG_HTTP);
    if (!NewListener)
        return STATUS_INSUFFICIENT_RESOURCES;

    NewListener->CloseIrp = IoAllocateIrp(1, FALSE);
    if (!NewListener->CloseIrp)
    {
        ExFreePoolWithTag(NewListener, TAG_HTTP);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    InitializeListHead(&NewListener->PendingConnections);
    KeInitializeSpinLock(&NewListener->PendingConnectionLock);
    NewListener->RefCount = 1;
    NewListener->Port = Port;
    NewListener->CallbackId = (ULONG)InterlockedIncrement(&g_ListenerCallbackId);
    if (!NewListener->CallbackId)
        NewListener->CallbackId = (ULONG)InterlockedIncrement(&g_ListenerCallbackId);

    Status = HttpCreateSocket(AF_INET, SOCK_STREAM, IPPROTO_TCP,
                              WSK_FLAG_LISTEN_SOCKET,
                              (PVOID)(ULONG_PTR)NewListener->CallbackId,
                              &g_HttpListenDispatch, &NewListener->Socket);
    if (!NT_SUCCESS(Status))
        goto failed;

    RtlZeroMemory(&Address, sizeof(Address));
    Address.sin_family = AF_INET;
    Address.sin_port = RtlUshortByteSwap(Port);
    Address.sin_addr.s_addr = INADDR_ANY;
    Status = HttpBindSocket(NewListener->Socket, (PSOCKADDR)&Address);
    if (!NT_SUCCESS(Status))
        goto failed;

    {
        KIRQL Irql;
        KeAcquireSpinLock(&g_ListenerCallbackLock, &Irql);
        InsertTailList(&g_ListenerCallbackList, &NewListener->CallbackEntry);
        NewListener->InCallbackList = TRUE;
        KeReleaseSpinLock(&g_ListenerCallbackLock, Irql);
    }

    Status = HttpEnableAccepts(NewListener->Socket);
    if (!NT_SUCCESS(Status))
        goto failed;

    *Listener = NewListener;
    return STATUS_SUCCESS;

failed:
    HttpCloseListener(NewListener);
    return Status;
}

static PHTTP_LISTENER HttpFindListener(USHORT Port)
{
    PLIST_ENTRY Entry;

    for (Entry = g_ListenersList.Flink; Entry != &g_ListenersList; Entry = Entry->Flink)
    {
        PHTTP_LISTENER Listener = CONTAINING_RECORD(Entry, HTTP_LISTENER, Entry);
        if (Listener->Port == Port && !Listener->Closing)
            return Listener;
    }

    return NULL;
}

static NTSTATUS HttpWskTransfer(
    _In_ PHTTP_CONNECTION Connection,
    _Inout_updates_bytes_(Length) PVOID Buffer,
    _In_ ULONG Length,
    _In_ BOOLEAN Send,
    _Out_ PULONG Transferred)
{
    PWSK_PROVIDER_CONNECTION_DISPATCH Dispatch;
    PWSK_SOCKET Socket;
    PIRP Irp;
    PMDL Mdl;
    KEVENT Event;
    WSK_BUF WskBuffer;
    NTSTATUS Status;

    *Transferred = 0;
    Mdl = IoAllocateMdl(Buffer, Length, FALSE, FALSE, NULL);
    if (!Mdl)
        return STATUS_INSUFFICIENT_RESOURCES;
    MmBuildMdlForNonPagedPool(Mdl);

    Irp = IoAllocateIrp(1, FALSE);
    if (!Irp)
    {
        IoFreeMdl(Mdl);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    WskBuffer.Mdl = Mdl;
    WskBuffer.Offset = 0;
    WskBuffer.Length = Length;
    KeInitializeEvent(&Event, NotificationEvent, FALSE);
    IoSetCompletionRoutine(Irp, HttpSyncCompletion, &Event, TRUE, TRUE, TRUE);

    ExAcquireFastMutex(&Connection->SocketMutex);
    Socket = Connection->Socket;
    if (!Socket || InterlockedCompareExchange(&Connection->Shutdown, 0, 0))
    {
        ExReleaseFastMutex(&Connection->SocketMutex);
        IoFreeIrp(Irp);
        IoFreeMdl(Mdl);
        return STATUS_CONNECTION_DISCONNECTED;
    }

    Dispatch = (PWSK_PROVIDER_CONNECTION_DISPATCH)Socket->Dispatch;
    if (Send)
        Status = Dispatch->WskSend(Socket, &WskBuffer, 0, Irp);
    else
        Status = Dispatch->WskReceive(Socket, &WskBuffer, 0, Irp);
    ExReleaseFastMutex(&Connection->SocketMutex);

    if (Status == STATUS_PENDING)
    {
        KeWaitForSingleObject(&Event, Executive, KernelMode, FALSE, NULL);
        Status = Irp->IoStatus.Status;
    }
    if (NT_SUCCESS(Status))
        *Transferred = (ULONG)Irp->IoStatus.Information;

    IoFreeIrp(Irp);
    IoFreeMdl(Mdl);
    return Status;
}

static NTSTATUS HttpWskSendAll(PHTTP_CONNECTION Connection, const char *Buffer, ULONG Length)
{
    ULONG Sent, Total = 0;
    NTSTATUS Status;

    while (Total < Length)
    {
        Status = HttpWskTransfer(Connection, (PVOID)(Buffer + Total), Length - Total, TRUE, &Sent);
        if (!NT_SUCCESS(Status))
            return Status;
        if (!Sent)
            return STATUS_CONNECTION_DISCONNECTED;
        Total += Sent;
    }

    return STATUS_SUCCESS;
}

static const char *const g_HttpVerbs[] =
{
    "OPTIONS",
    "GET",
    "HEAD",
    "POST",
    "PUT",
    "DELETE",
    "TRACE",
    "CONNECT",
    "TRACK",
    "MOVE",
    "COPY",
    "PROPFIND",
    "PROPPATCH",
    "MKCOL",
    "LOCK",
    "UNLOCK",
    "SEARCH",
};

static const char *const g_HttpHeaders[] =
{
    "Cache-Control",
    "Connection",
    "Date",
    "Keep-Alive",
    "Pragma",
    "Trailer",
    "Transfer-Encoding",
    "Upgrade",
    "Via",
    "Warning",
    "Allow",
    "Content-Length",
    "Content-Type",
    "Content-Encoding",
    "Content-Language",
    "Content-Location",
    "Content-MD5",
    "Content-Range",
    "Expires",
    "Last-Modified",
    "Accept",
    "Accept-Charset",
    "Accept-Encoding",
    "Accept-Language",
    "Authorization",
    "Cookie",
    "Expect",
    "From",
    "Host",
    "If-Match",
    "If-Modified-Since",
    "If-None-Match",
    "If-Range",
    "If-Unmodified-Since",
    "Max-Forwards",
    "Proxy-Authorization",
    "Referer",
    "Range",
    "TE",
    "Translate",
    "User-Agent",
};

static HTTP_VERB parse_verb(const char *verb, int len)
{
    unsigned int i;
    for (i = 0; i < RTL_NUMBER_OF(g_HttpVerbs); ++i)
    {
        if ((int)strlen(g_HttpVerbs[i]) == len && !strncmp(verb, g_HttpVerbs[i], len))
            return HttpVerbOPTIONS + i;
    }
    return HttpVerbUnknown;
}

static HTTP_HEADER_ID parse_header_name(const char *header, int len)
{
    unsigned int i;
    for (i = 0; i < RTL_NUMBER_OF(g_HttpHeaders); ++i)
    {
        if ((int)strlen(g_HttpHeaders[i]) == len && !_strnicmp(header, g_HttpHeaders[i], len))
            return (HTTP_HEADER_ID)i;
    }
    return HttpHeaderRequestMaximum;
}

static int HttpLlhttpAppendSpan(
    _In_ PHTTP_CONNECTION Connection,
    _Inout_ PULONG Offset,
    _Inout_ PULONG CurrentLength,
    _In_ const char *Data,
    _In_ size_t Length)
{
    ULONG DataOffset = (ULONG)(Data - Connection->Buffer);

    if (!Length)
        return 0;
    if (!*CurrentLength)
        *Offset = DataOffset;
    else if (*Offset + *CurrentLength != DataOffset)
        return -1;
    *CurrentLength += (ULONG)Length;
    return 0;
}

static int HttpLlhttpOnMethod(llhttp_t *Parser, const char *Data, size_t Length)
{
    PHTTP_CONNECTION Connection = Parser->data;
    return HttpLlhttpAppendSpan(Connection, &Connection->MethodOffset,
                                &Connection->MethodLength, Data, Length);
}

static int HttpLlhttpOnUrl(llhttp_t *Parser, const char *Data, size_t Length)
{
    PHTTP_CONNECTION Connection = Parser->data;
    return HttpLlhttpAppendSpan(Connection, &Connection->UrlOffset,
                                &Connection->UrlLength, Data, Length);
}

static int HttpLlhttpOnHeaderField(llhttp_t *Parser, const char *Data, size_t Length)
{
    PHTTP_CONNECTION Connection = Parser->data;
    PHTTP_PARSED_HEADER Header = &Connection->Headers[Connection->HeaderCount];

    if (Connection->HeadersComplete)
        return 0;
    return HttpLlhttpAppendSpan(Connection, &Header->NameOffset, &Header->NameLength, Data, Length);
}

static int HttpLlhttpOnHeaderValue(llhttp_t *Parser, const char *Data, size_t Length)
{
    PHTTP_CONNECTION Connection = Parser->data;
    PHTTP_PARSED_HEADER Header = &Connection->Headers[Connection->HeaderCount];

    if (Connection->HeadersComplete)
        return 0;
    return HttpLlhttpAppendSpan(Connection, &Header->ValueOffset, &Header->ValueLength, Data, Length);
}

static int HttpLlhttpOnHeaderFieldComplete(llhttp_t *Parser)
{
    PHTTP_CONNECTION Connection = Parser->data;

    if (Connection->HeadersComplete)
        return 0;
    if (Connection->HeaderCount >= HTTP_MAX_HEADERS)
    {
        Connection->ErrorStatus = 431;
        return -1;
    }

    return 0;
}

static int HttpLlhttpOnHeaderValueComplete(llhttp_t *Parser)
{
    PHTTP_CONNECTION Connection = Parser->data;
    PHTTP_PARSED_HEADER Header = &Connection->Headers[Connection->HeaderCount];
    const char *Value = Connection->Buffer + Header->ValueOffset;

    if (Connection->HeadersComplete)
        return 0;
    while (Header->ValueLength &&
           (Value[Header->ValueLength - 1] == ' ' || Value[Header->ValueLength - 1] == '\t'))
        --Header->ValueLength;

    Header->Id = parse_header_name(Connection->Buffer + Header->NameOffset, (int)Header->NameLength);
    if (Header->Id == HttpHeaderHost)
    {
        if (Connection->HostSeen || !Header->ValueLength)
            return -1;
        Connection->HostSeen = TRUE;
        Connection->HostOffset = Header->ValueOffset;
        Connection->HostLength = Header->ValueLength;
    }

    ++Connection->HeaderCount;
    RtlZeroMemory(&Connection->Headers[Connection->HeaderCount], sizeof(*Header));
    return 0;
}

static int HttpLlhttpOnHeadersComplete(llhttp_t *Parser)
{
    PHTTP_CONNECTION Connection = Parser->data;

    if (Parser->http_major != 1 || Parser->http_minor > 1)
        return -1;
    if (Parser->http_minor == 1 && !Connection->HostSeen)
        return -1;
    if ((Parser->flags & F_TRANSFER_ENCODING) && !(Parser->flags & F_CHUNKED))
    {
        Connection->ErrorStatus = 501;
        return -1;
    }
    if (!Connection->MethodLength || Connection->MethodLength > MAXUSHORT ||
        !Connection->UrlLength || Connection->UrlLength > MAXUSHORT ||
        Connection->HostLength > MAXUSHORT)
        return -1;

    Connection->Version.MajorVersion = Parser->http_major;
    Connection->Version.MinorVersion = Parser->http_minor;
    Connection->KeepAlive = llhttp_should_keep_alive(Parser) &&
                            (~Parser->flags & (F_CHUNKED | F_CONTENT_LENGTH));
    Connection->HeadersComplete = TRUE;
    return HPE_PAUSED;
}

static int HttpLlhttpOnBody(llhttp_t *Parser, const char *Data, size_t Length)
{
    PHTTP_CONNECTION Connection = Parser->data;

    RtlMoveMemory(Connection->Buffer + Connection->BodyOffset + Connection->BodyLength, Data, Length);
    Connection->BodyLength += (ULONG)Length;
    return 0;
}

static int HttpLlhttpOnMessageComplete(llhttp_t *Parser)
{
    PHTTP_CONNECTION Connection = Parser->data;

    Connection->BodyComplete = TRUE;
    return HPE_PAUSED;
}

#define http_unknown_header http_unknown_header_64
#define http_data_chunk http_data_chunk_64
#define http_request http_request_64
#define complete_irp complete_irp_64
#define POINTER ULONGLONG
#include "request.h"
#undef http_unknown_header
#undef http_data_chunk
#undef http_request
#undef complete_irp
#undef POINTER

#define http_unknown_header http_unknown_header_32
#define http_data_chunk http_data_chunk_32
#define http_request http_request_32
#define complete_irp complete_irp_32
#define POINTER ULONG
#include "request.h"
#undef http_unknown_header
#undef http_data_chunk
#undef http_request
#undef complete_irp
#undef POINTER

static NTSTATUS complete_irp(PHTTP_CONNECTION conn, PIRP irp)
{
    const struct http_receive_request_params params
            = *(struct http_receive_request_params *)irp->AssociatedIrp.SystemBuffer;

    if (!conn->RequestId)
        conn->RequestId = InterlockedIncrement64((LONG64 *)&g_RequestIdCounter);

    if (params.bits == 32)
        return complete_irp_32(conn, irp);
    else
        return complete_irp_64(conn, irp);
}

static PHTTP_CONNECTION GetConnectionById(HTTP_REQUEST_ID RequestId)
{
    PLIST_ENTRY Entry;
    for (Entry = g_ConnectionsList.Flink; Entry != &g_ConnectionsList; Entry = Entry->Flink)
    {
        PHTTP_CONNECTION Conn = CONTAINING_RECORD(Entry, HTTP_CONNECTION, Entry);
        if (Conn->RequestId == RequestId)
            return Conn;
    }
    return NULL;
}

static VOID HttpConnectionReference(PHTTP_CONNECTION Connection)
{
    InterlockedIncrement(&Connection->RefCount);
}

static VOID HttpCompleteDisconnectWaits(PHTTP_CONNECTION Connection)
{
    HTTP_DISCONNECT_PEEK Peek = { Connection, NULL };
    PIRP Irp;

    while ((Irp = IoCsqRemoveNextIrp(&g_DisconnectCsq, &Peek)) != NULL)
    {
        Irp->IoStatus.Status = Connection->Cancelled ? STATUS_NETWORK_NAME_DELETED : STATUS_SUCCESS;
        Irp->IoStatus.Information = 0;
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
    }
}

static VOID HttpConnectionCloseSocket(PHTTP_CONNECTION Connection)
{
    PWSK_SOCKET Socket;
    NTSTATUS Status;

    ExAcquireFastMutex(&Connection->SocketMutex);
    InterlockedExchange(&Connection->Shutdown, TRUE);
    Socket = Connection->Socket;
    Connection->Socket = NULL;
    ExReleaseFastMutex(&Connection->SocketMutex);

    if (!Socket)
        return;

    Status = HttpCloseSocket(Socket, Connection->CloseIrp);
    if (!NT_SUCCESS(Status))
        DPRINT1("HttpConnectionCloseSocket failed with %lx\n", Status);
    HttpCompleteDisconnectWaits(Connection);
}

static NTSTATUS HttpDisconnectSocket(PWSK_SOCKET Socket, ULONG Flags, PIRP Irp)
{
    PWSK_PROVIDER_CONNECTION_DISPATCH Dispatch = (PWSK_PROVIDER_CONNECTION_DISPATCH)Socket->Dispatch;
    KEVENT Event;
    NTSTATUS Status;

    IoReuseIrp(Irp, STATUS_UNSUCCESSFUL);
    KeInitializeEvent(&Event, NotificationEvent, FALSE);
    IoSetCompletionRoutine(Irp, HttpSyncCompletion, &Event, TRUE, TRUE, TRUE);
    Status = Dispatch->WskDisconnect(Socket, NULL, Flags, Irp);
    if (Status == STATUS_PENDING)
    {
        KeWaitForSingleObject(&Event, Executive, KernelMode, FALSE, NULL);
        Status = Irp->IoStatus.Status;
    }
    return Status;
}

static VOID HttpConnectionResetSocket(PHTTP_CONNECTION Connection)
{
    PWSK_SOCKET Socket;
    NTSTATUS Status;

    ExAcquireFastMutex(&Connection->SocketMutex);
    InterlockedExchange(&Connection->Shutdown, TRUE);
    Socket = Connection->Socket;
    Connection->Socket = NULL;
    ExReleaseFastMutex(&Connection->SocketMutex);

    if (!Socket)
        return;

    Status = HttpDisconnectSocket(Socket, WSK_FLAG_ABORTIVE, Connection->CloseIrp);
    if (!NT_SUCCESS(Status))
        DPRINT1("abortive disconnect failed with %lx\n", Status);

    Status = HttpCloseSocket(Socket, Connection->CloseIrp);
    if (!NT_SUCCESS(Status))
        DPRINT1("HttpConnectionResetSocket failed with %lx\n", Status);
    HttpCompleteDisconnectWaits(Connection);
}

static VOID HttpConnectionDereference(PHTTP_CONNECTION Connection)
{
    PHTTP_LISTENER Listener;

    if (InterlockedDecrement(&Connection->RefCount) != 0)
        return;

    HttpConnectionCloseSocket(Connection);
    Listener = Connection->Listener;
    Connection->Listener = NULL;
    if (Listener)
        HttpListenerDereference(Listener);
    HttpFreeConnection(Connection);
    InterlockedDecrement(&g_ConnectionCount);
}

static VOID HttpListenerDereference(PHTTP_LISTENER Listener)
{
    if (InterlockedDecrement(&Listener->RefCount) == 0)
    {
        IoFreeIrp(Listener->CloseIrp);
        ExFreePoolWithTag(Listener, TAG_HTTP);
    }
}

static VOID HttpRemoveListenerConnection(PHTTP_CONNECTION Connection)
{
    PHTTP_LISTENER Listener = Connection->Listener;
    KIRQL Irql;
    BOOLEAN Removed = FALSE;

    if (!Listener)
        return;

    KeAcquireSpinLock(&Listener->PendingConnectionLock, &Irql);
    if (Connection->InListenerList)
    {
        RemoveEntryList(&Connection->ListenerEntry);
        Connection->InListenerList = FALSE;
        Removed = TRUE;
    }
    KeReleaseSpinLock(&Listener->PendingConnectionLock, Irql);

    if (Removed)
        HttpConnectionDereference(Connection);
}

static BOOLEAN HttpInsertListenerConnection(PHTTP_CONNECTION Connection)
{
    PHTTP_LISTENER Listener = Connection->Listener;
    BOOLEAN Inserted = FALSE;
    KIRQL Irql;

    KeAcquireSpinLock(&Listener->PendingConnectionLock, &Irql);
    if (!Listener->Closing && !InterlockedCompareExchange(&Connection->Shutdown, 0, 0))
    {
        InsertTailList(&Listener->PendingConnections, &Connection->ListenerEntry);
        Connection->InListenerList = TRUE;
        HttpConnectionReference(Connection);
        Inserted = TRUE;
    }
    KeReleaseSpinLock(&Listener->PendingConnectionLock, Irql);
    return Inserted;
}

static VOID HttpCloseListener(PHTTP_LISTENER Listener)
{
    LIST_ENTRY ConnectionCloseList;
    PLIST_ENTRY Entry;
    KIRQL Irql;
    NTSTATUS Status;

    InitializeListHead(&ConnectionCloseList);
    KeAcquireSpinLock(&g_ListenerCallbackLock, &Irql);
    if (Listener->InCallbackList)
    {
        RemoveEntryList(&Listener->CallbackEntry);
        Listener->InCallbackList = FALSE;
    }
    KeReleaseSpinLock(&g_ListenerCallbackLock, Irql);

    KeAcquireSpinLock(&Listener->PendingConnectionLock, &Irql);
    Listener->Closing = TRUE;
    while (!IsListEmpty(&Listener->PendingConnections))
    {
        Entry = RemoveHeadList(&Listener->PendingConnections);
        PHTTP_CONNECTION Connection =
            CONTAINING_RECORD(Entry, HTTP_CONNECTION, ListenerEntry);
        Connection->InListenerList = FALSE;
        InterlockedExchange(&Connection->Shutdown, TRUE);
        InsertTailList(&ConnectionCloseList, Entry);
    }
    KeReleaseSpinLock(&Listener->PendingConnectionLock, Irql);

    Status = HttpCloseSocket(Listener->Socket, Listener->CloseIrp);
    if (!NT_SUCCESS(Status))
        DPRINT1("HttpCloseListener failed with %lx\n", Status);
    Listener->Socket = NULL;

    while (!IsListEmpty(&ConnectionCloseList))
    {
        Entry = RemoveHeadList(&ConnectionCloseList);
        PHTTP_CONNECTION Connection =
            CONTAINING_RECORD(Entry, HTTP_CONNECTION, ListenerEntry);
        HttpConnectionCloseSocket(Connection);
        HttpConnectionDereference(Connection);
    }

    HttpListenerDereference(Listener);
}

static ULONG HttpRequestHostLength(PHTTP_CONNECTION Connection)
{
    ULONG Length = Connection->HostLength;
    ULONG i;

    if (!Length || !Connection->Host)
        return 0;
    if (Connection->Host[0] == '[')
    {
        for (i = 1; i < Length; ++i)
        {
            if (Connection->Host[i] == ']')
                return i + 1;
        }
        return 0;
    }

    for (i = 0; i < Length; ++i)
    {
        if (Connection->Host[i] == ':')
            return i;
    }
    return Length;
}

static BOOLEAN HttpUrlMatchesConnection(
    PHTTP_URL_ENTRY Url,
    PHTTP_CONNECTION Connection)
{
    ULONG HostLength;

    if (Connection->UrlLength < Url->PathLength ||
        _strnicmp(Connection->Url, Url->Path, Url->PathLength))
        return FALSE;
    if (Connection->UrlLength > Url->PathLength &&
        Url->Path[Url->PathLength - 1] != '/' &&
        Connection->Url[Url->PathLength] != '/' &&
        Connection->Url[Url->PathLength] != '?')
        return FALSE;

    if (Url->Category == HttpUrlCategoryStrongWildcard ||
        Url->Category == HttpUrlCategoryWeakWildcard)
        return TRUE;

    if (Url->Category == HttpUrlCategoryIPBound)
        return Url->BoundAddress.s_addr == Connection->LocalAddress.sin_addr.s_addr;

    HostLength = HttpRequestHostLength(Connection);
    return HostLength == strlen(Url->Host) &&
           !_strnicmp(Connection->Host, Url->Host, HostLength);
}

static ULONG HttpQueuedRequestCount(PHTTP_REQUEST_QUEUE Queue)
{
    PLIST_ENTRY Entry;
    ULONG Count = 0;

    for (Entry = g_ConnectionsList.Flink; Entry != &g_ConnectionsList; Entry = Entry->Flink)
    {
        PHTTP_CONNECTION Conn = CONTAINING_RECORD(Entry, HTTP_CONNECTION, Entry);
        if (Conn->Queue == Queue && Conn->Available)
            Count++;
    }
    return Count;
}

static BOOLEAN HttpQueueConnection(PHTTP_CONNECTION Connection)
{
    PLIST_ENTRY QueueEntry, UrlEntry;
    PHTTP_URL_ENTRY BestUrl = NULL;
    PHTTP_REQUEST_QUEUE BestQueue = NULL;
    PHTTP_REQUEST_QUEUE Queue;
    PIRP PendingIrp = NULL;
    UCHAR Reason = HttpUnavailableNone;
    NTSTATUS Status;

    Connection->ErrorStatus = 404;
    ExAcquireFastMutex(&g_HttpGlobalLock);
    for (QueueEntry = g_RequestQueuesList.Flink;
         QueueEntry != &g_RequestQueuesList;
         QueueEntry = QueueEntry->Flink)
    {
        Queue = CONTAINING_RECORD(QueueEntry, HTTP_REQUEST_QUEUE, Entry);
        ExAcquireFastMutex(&Queue->Mutex);
        if (Queue->State & HTTP_QUEUE_STATE_CLOSING)
        {
            ExReleaseFastMutex(&Queue->Mutex);
            continue;
        }
        ExReleaseFastMutex(&Queue->Mutex);

        for (UrlEntry = Queue->UrlList.Flink;
             UrlEntry != &Queue->UrlList;
             UrlEntry = UrlEntry->Flink)
        {
            PHTTP_URL_ENTRY Url = CONTAINING_RECORD(UrlEntry, HTTP_URL_ENTRY, Entry);
            if (Url->Port != RtlUshortByteSwap(Connection->LocalAddress.sin_port))
                continue;
            if (!HttpUrlMatchesConnection(Url, Connection))
                continue;
            if (!BestUrl || Url->Category < BestUrl->Category ||
                (Url->Category == BestUrl->Category &&
                 Url->PathLength > BestUrl->PathLength))
            {
                BestUrl = Url;
                BestQueue = Queue;
            }
        }
    }

    if (!BestUrl)
    {
        ExReleaseFastMutex(&g_HttpGlobalLock);
        return FALSE;
    }

    ExAcquireFastMutex(&BestQueue->Mutex);
    if (BestQueue->State & HTTP_QUEUE_STATE_CLOSING)
    {
        ExReleaseFastMutex(&BestQueue->Mutex);
        ExReleaseFastMutex(&g_HttpGlobalLock);
        return FALSE;
    }

    if (InterlockedCompareExchange(&Connection->Shutdown, 0, 0))
    {
        ExReleaseFastMutex(&BestQueue->Mutex);
        ExReleaseFastMutex(&g_HttpGlobalLock);
        return FALSE;
    }

    if (BestQueue->EnabledState != HttpEnabledStateActive)
        Reason = HttpUnavailableOffline;
    else if (BestUrl->Config.inactive)
        Reason = HttpUnavailableInactive;
    else if (HttpQueuedRequestCount(BestQueue) >= BestQueue->MaxQueueLength)
        Reason = HttpUnavailableQueueFull;
    else if (!HttpCountGroupConnection(Connection, BestQueue, BestUrl))
        Reason = HttpUnavailableConnectionLimit;
    if (Reason != HttpUnavailableNone)
    {
        Connection->ErrorStatus = 503;
        Connection->UnavailableReason = Reason;
        Connection->UnavailableVerbosity = (UCHAR)BestQueue->Verbosity;
        ExReleaseFastMutex(&BestQueue->Mutex);
        ExReleaseFastMutex(&g_HttpGlobalLock);
        return FALSE;
    }

    HttpRemoveListenerConnection(Connection);
    if (InterlockedCompareExchange(&Connection->Shutdown, 0, 0))
    {
        ExReleaseFastMutex(&BestQueue->Mutex);
        ExReleaseFastMutex(&g_HttpGlobalLock);
        return FALSE;
    }

    Connection->Queue = BestQueue;
    Connection->Context = BestUrl->Context;
    Connection->IdleSeconds = BestUrl->Config.idle_connection;
    Connection->HeaderWaitSeconds = BestUrl->Config.header_wait;
    Connection->EntityBodySeconds = BestUrl->Config.entity_body;
    /* TODO: DrainEntityBody, RequestQueue, MinSendRate and bandwidth limits are stored by httpapi only */
    Connection->Available = TRUE;
    InsertTailList(&g_ConnectionsList, &Connection->Entry);
    Connection->InConnectionList = TRUE;
    HttpConnectionReference(Connection);
    if (HttpWantsBody(Connection))
    {
        HttpConnectionReference(Connection);
        if (HttpStartBodyReceive(Connection) != STATUS_PENDING)
            HttpConnectionDereference(Connection);
    }

    PendingIrp = IoCsqRemoveNextIrp(&BestQueue->Csq, NULL);
    if (PendingIrp)
    {
        Status = complete_irp(Connection, PendingIrp);
        PendingIrp->IoStatus.Status = Status;
        if (!NT_SUCCESS(Status) && Status != STATUS_BUFFER_OVERFLOW)
            PendingIrp->IoStatus.Information = 0;
    }

    ExReleaseFastMutex(&BestQueue->Mutex);
    ExReleaseFastMutex(&g_HttpGlobalLock);

    if (PendingIrp)
        IoCompleteRequest(PendingIrp, IO_NO_INCREMENT);
    return TRUE;
}

static NTSTATUS NTAPI HttpReceiveComplete(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp,
    _In_ PVOID Context)
{
    PHTTP_CONNECTION Connection = Context;

    UNREFERENCED_PARAMETER(DeviceObject);

    Connection->ReceiveStatus = Irp->IoStatus.Status;
    Connection->ReceiveLength =
        NT_SUCCESS(Irp->IoStatus.Status) ? (ULONG)Irp->IoStatus.Information : 0;
    IoQueueWorkItem(Connection->WorkItem, HttpConnectionWorker, DelayedWorkQueue, Connection);
    return STATUS_MORE_PROCESSING_REQUIRED;
}

static NTSTATUS HttpStartReceive(PHTTP_CONNECTION Connection, ULONG Length)
{
    PWSK_PROVIDER_CONNECTION_DISPATCH Dispatch;
    PWSK_SOCKET Socket;

    Connection->ReceiveMdl = IoAllocateMdl(Connection->Buffer + Connection->BufferLength,
                                           Length, FALSE, FALSE, NULL);
    if (!Connection->ReceiveMdl)
        return STATUS_INSUFFICIENT_RESOURCES;
    MmBuildMdlForNonPagedPool(Connection->ReceiveMdl);

    Connection->ReceiveBuffer.Mdl = Connection->ReceiveMdl;
    Connection->ReceiveBuffer.Offset = 0;
    Connection->ReceiveBuffer.Length = Length;
    IoReuseIrp(Connection->ReceiveIrp, STATUS_UNSUCCESSFUL);
    IoSetCompletionRoutine(Connection->ReceiveIrp, HttpReceiveComplete, Connection, TRUE, TRUE, TRUE);

    ExAcquireFastMutex(&Connection->SocketMutex);
    Socket = Connection->Socket;
    if (!Socket || InterlockedCompareExchange(&Connection->Shutdown, 0, 0))
    {
        ExReleaseFastMutex(&Connection->SocketMutex);
        IoFreeMdl(Connection->ReceiveMdl);
        Connection->ReceiveMdl = NULL;
        return STATUS_CONNECTION_DISCONNECTED;
    }

    Connection->ReceivePending = TRUE;
    Connection->ReceiveOffset = Connection->BufferLength;
    Dispatch = (PWSK_PROVIDER_CONNECTION_DISPATCH)Socket->Dispatch;
    Dispatch->WskReceive(Socket, &Connection->ReceiveBuffer, 0, Connection->ReceiveIrp);
    ExReleaseFastMutex(&Connection->SocketMutex);
    return STATUS_PENDING;
}

static NTSTATUS HttpReceiveHeaders(PHTTP_CONNECTION Connection)
{
    if (Connection->BufferLength >= HTTP_MAX_HEADER_SIZE)
    {
        Connection->ErrorStatus = 431;
        return STATUS_BUFFER_TOO_SMALL;
    }

    return HttpStartReceive(Connection, Connection->BufferCapacity - Connection->BufferLength - 1);
}

static NTSTATUS HttpFinishRequest(PHTTP_CONNECTION Connection)
{
    const char *Url = Connection->Buffer + Connection->UrlOffset;

    if (!Connection->UrlLength || Url[0] != '/')
    {
        Connection->ErrorStatus = 501;
        return STATUS_NOT_SUPPORTED;
    }

    Connection->Verb = parse_verb(Connection->Buffer + Connection->MethodOffset,
                                  (int)Connection->MethodLength);
    Connection->UnknownVerbLength =
        Connection->Verb == HttpVerbUnknown ? Connection->MethodLength : 0;
    Connection->Url = Url;
    Connection->Host = Connection->HostSeen ? Connection->Buffer + Connection->HostOffset : "";
    return STATUS_SUCCESS;
}

static VOID HttpParseBody(PHTTP_CONNECTION Connection)
{
    ULONG Start = Connection->BodyOffset + Connection->BodyLength;
    ULONG Length = Connection->BufferLength - Start;
    ULONG Consumed = Length, End;
    llhttp_errno_t ParserStatus;

    if (Connection->BodyComplete || Connection->BodyFailed)
        return;

    ParserStatus = llhttp_execute(&Connection->Parser, Connection->Buffer + Start, Length);
    if (ParserStatus == HPE_PAUSED && Connection->BodyComplete)
        Consumed = (ULONG)(llhttp_get_error_pos(&Connection->Parser) - (Connection->Buffer + Start));
    else if (ParserStatus != HPE_OK)
        Connection->BodyFailed = TRUE;

    Connection->BodyReceived += Consumed;
    End = Connection->BodyOffset + Connection->BodyLength;
    RtlMoveMemory(Connection->Buffer + End, Connection->Buffer + Start + Consumed, Length - Consumed);
    Connection->BufferLength = End + Length - Consumed;
}

static NTSTATUS HttpParseHeaders(PHTTP_CONNECTION Connection, ULONG Offset)
{
    llhttp_errno_t ParserStatus;
    NTSTATUS Status;

    ParserStatus = llhttp_execute(&Connection->Parser,
                                  Connection->Buffer + Offset,
                                  Connection->BufferLength - Offset);
    if (ParserStatus == HPE_PAUSED && Connection->HeadersComplete)
    {
        Connection->HeaderLength =
            (ULONG)(llhttp_get_error_pos(&Connection->Parser) - Connection->Buffer);
        Status = HttpFinishRequest(Connection);
        if (!NT_SUCCESS(Status))
            return Status;

        Connection->BodyOffset = Connection->HeaderLength;
        llhttp_resume(&Connection->Parser);
        HttpParseBody(Connection);
        return Connection->BodyFailed ? STATUS_INVALID_PARAMETER : STATUS_SUCCESS;
    }
    if (ParserStatus != HPE_OK)
    {
        if (Connection->ErrorStatus == 431)
            return STATUS_BUFFER_TOO_SMALL;
        if (Connection->ErrorStatus == 501)
            return STATUS_NOT_SUPPORTED;
        return STATUS_INVALID_PARAMETER;
    }

    return HttpReceiveHeaders(Connection);
}

static NTSTATUS HttpReceiveCompleted(PHTTP_CONNECTION Connection)
{
    ULONG OldLength;

    IoFreeMdl(Connection->ReceiveMdl);
    Connection->ReceiveMdl = NULL;

    if (!NT_SUCCESS(Connection->ReceiveStatus))
        return Connection->ReceiveStatus;
    if (!Connection->ReceiveLength)
        return STATUS_CONNECTION_DISCONNECTED;

    OldLength = Connection->BufferLength;
    if (!OldLength)
        HttpSetDeadline(Connection, HttpHeaderWaitSeconds(Connection));
    Connection->BufferLength += Connection->ReceiveLength;
    return HttpParseHeaders(Connection, OldLength);
}

static VOID HttpResetRequest(PHTTP_CONNECTION Connection)
{
    llhttp_reset(&Connection->Parser);
    Connection->RequestId = InterlockedIncrement64((LONG64 *)&g_RequestIdCounter);
    Connection->Context = 0;
    Connection->Queue = NULL;
    Connection->ResponseStarted = FALSE;
    Connection->KeepAlive = FALSE;
    Connection->CloseAfterResponse = FALSE;
    Connection->Finished = FALSE;
    Connection->MethodOffset = 0;
    Connection->MethodLength = 0;
    Connection->UrlOffset = 0;
    Connection->UrlLength = 0;
    Connection->HeaderCount = 0;
    RtlZeroMemory(&Connection->Headers[0], sizeof(Connection->Headers[0]));
    Connection->HostOffset = 0;
    Connection->HostLength = 0;
    Connection->HeaderLength = 0;
    Connection->HostSeen = FALSE;
    Connection->HeadersComplete = FALSE;
    Connection->BodyComplete = FALSE;
    Connection->BodyFailed = FALSE;
    Connection->ErrorStatus = 400;
    Connection->Url = NULL;
    Connection->Host = NULL;
    Connection->UnknownVerbLength = 0;
    Connection->BodyOffset = 0;
    Connection->BodyLength = 0;
    Connection->BodyReceived = 0;
}

static VOID HttpRestartConnection(PHTTP_CONNECTION Connection)
{
    HttpResetRequest(Connection);
    if (Connection->BufferLength)
        HttpSetDeadline(Connection, HttpHeaderWaitSeconds(Connection));
    else
        HttpSetDeadline(Connection, Connection->IdleSeconds
                                    ? Connection->IdleSeconds
                                    : InterlockedCompareExchange(&g_IdleConnectionSeconds, 0, 0));
    IoQueueWorkItem(Connection->WorkItem, HttpConnectionWorker, DelayedWorkQueue, Connection);
}

static VOID HttpConnectionLinger(PHTTP_CONNECTION Connection)
{
    PWSK_SOCKET Socket;
    NTSTATUS Status = STATUS_CONNECTION_DISCONNECTED;

    Connection->Lingering = TRUE;
    Connection->BufferLength = 0;
    ExAcquireFastMutex(&Connection->SocketMutex);
    Socket = Connection->Socket;
    ExReleaseFastMutex(&Connection->SocketMutex);
    if (Socket)
        Status = HttpDisconnectSocket(Socket, 0, Connection->CloseIrp);

    HttpSetDeadline(Connection, Connection->IdleSeconds
                                ? Connection->IdleSeconds
                                : InterlockedCompareExchange(&g_IdleConnectionSeconds, 0, 0));
    if (NT_SUCCESS(Status) && HttpInsertListenerConnection(Connection))
    {
        HttpConnectionReference(Connection);
        if (HttpStartReceive(Connection, Connection->BufferCapacity - 1) == STATUS_PENDING)
            return;
        HttpConnectionDereference(Connection);
        HttpRemoveListenerConnection(Connection);
    }
    HttpConnectionCloseSocket(Connection);
}

static VOID HttpConsumeHeaders(PHTTP_CONNECTION Connection)
{
    if (!Connection->Available)
        return;

    RtlMoveMemory(Connection->Buffer, Connection->Buffer + Connection->HeaderLength,
                  Connection->BufferLength - Connection->HeaderLength);
    Connection->BufferLength -= Connection->HeaderLength;
    Connection->BodyOffset = 0;
    Connection->Available = FALSE;
}

static BOOLEAN HttpWantsBody(PHTTP_CONNECTION Connection)
{
    return !Connection->ReadingBody && !Connection->BodyComplete && !Connection->BodyFailed &&
           Connection->BufferLength + 1 < Connection->BufferCapacity;
}

static VOID HttpDiscardBody(PHTTP_CONNECTION Connection)
{
    ULONG Length = Connection->BodyLength;

    RtlMoveMemory(Connection->Buffer, Connection->Buffer + Length, Connection->BufferLength - Length);
    Connection->BufferLength -= Length;
    Connection->BodyLength = 0;
}

static BOOLEAN HttpCopyBody(PHTTP_CONNECTION Connection, PIRP Irp)
{
    PIO_STACK_LOCATION Stack = IoGetCurrentIrpStackLocation(Irp);
    ULONG OutputLen = Stack->Parameters.DeviceIoControl.OutputBufferLength;
    ULONG Copied = (ULONG)Irp->IoStatus.Information;
    ULONG Length = min(Connection->BodyLength, OutputLen - Copied);
    ULONG Flags = (ULONG)(ULONG_PTR)Irp->Tail.Overlay.DriverContext[1];

    memcpy((PCHAR)Irp->AssociatedIrp.SystemBuffer + Copied, Connection->Buffer, Length);
    RtlMoveMemory(Connection->Buffer, Connection->Buffer + Length, Connection->BufferLength - Length);
    Connection->BufferLength -= Length;
    Connection->BodyLength -= Length;
    Irp->IoStatus.Information = Copied + Length;

    if ((Connection->BodyComplete && !Connection->BodyLength) || Connection->BodyFailed ||
        Irp->IoStatus.Information == OutputLen)
        return TRUE;
    return Irp->IoStatus.Information &&
           !(Flags & HTTP_RECEIVE_REQUEST_ENTITY_BODY_FLAG_FILL_BUFFER);
}

static NTSTATUS HttpBodyStatus(PHTTP_CONNECTION Connection, PIRP Irp)
{
    if (Irp->IoStatus.Information)
        return STATUS_SUCCESS;
    return Connection->BodyFailed ? STATUS_INVALID_DEVICE_REQUEST : STATUS_END_OF_FILE;
}

static VOID HttpDeliverBody(PHTTP_CONNECTION Connection, PLIST_ENTRY CompleteList)
{
    PIRP Irp;

    while ((Irp = IoCsqRemoveNextIrp(&Connection->Queue->BodyCsq, Connection)) != NULL)
    {
        if (!HttpCopyBody(Connection, Irp))
        {
            IoCsqInsertIrpEx(&Connection->Queue->BodyCsq, Irp, NULL, Irp);
            return;
        }

        Irp->IoStatus.Status = HttpBodyStatus(Connection, Irp);
        InsertTailList(CompleteList, &Irp->Tail.Overlay.ListEntry);
    }
}

static VOID HttpFlushBodyIrps(PHTTP_CONNECTION Connection, PLIST_ENTRY CompleteList)
{
    PIRP Irp;

    while ((Irp = IoCsqRemoveNextIrp(&Connection->Queue->BodyCsq, Connection)) != NULL)
    {
        Irp->IoStatus.Status = Irp->IoStatus.Information ? STATUS_SUCCESS : STATUS_CONNECTION_INVALID;
        InsertTailList(CompleteList, &Irp->Tail.Overlay.ListEntry);
    }
}

static VOID HttpCompleteIrpList(PLIST_ENTRY CompleteList)
{
    while (!IsListEmpty(CompleteList))
    {
        PIRP Irp = CONTAINING_RECORD(RemoveHeadList(CompleteList), IRP, Tail.Overlay.ListEntry);
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
    }
}

static NTSTATUS HttpStartBodyReceive(PHTTP_CONNECTION Connection)
{
    ULONG Length = Connection->BufferCapacity - Connection->BufferLength - 1;
    NTSTATUS Status;

    Connection->ReadingBody = TRUE;
    HttpSetDeadline(Connection, Connection->EntityBodySeconds
                                ? Connection->EntityBodySeconds
                                : HTTP_ENTITY_BODY_SECONDS);
    Status = HttpStartReceive(Connection, Length);
    if (Status != STATUS_PENDING)
        Connection->ReadingBody = FALSE;
    return Status;
}

static VOID HttpBodyReceiveCompleted(PHTTP_CONNECTION Connection)
{
    LIST_ENTRY CompleteList;
    BOOLEAN Removed = FALSE, Restart = FALSE, Reject = FALSE;
    NTSTATUS Status = Connection->ReceiveStatus;

    InitializeListHead(&CompleteList);
    IoFreeMdl(Connection->ReceiveMdl);
    Connection->ReceiveMdl = NULL;
    if (NT_SUCCESS(Status) && !Connection->ReceiveLength)
        Status = STATUS_CONNECTION_DISCONNECTED;

    ExAcquireFastMutex(&g_HttpGlobalLock);
    Connection->ReceivePending = FALSE;
    Connection->ReadingBody = FALSE;
    if (NT_SUCCESS(Status))
    {
        RtlMoveMemory(Connection->Buffer + Connection->BufferLength,
                      Connection->Buffer + Connection->ReceiveOffset,
                      Connection->ReceiveLength);
        Connection->BufferLength += Connection->ReceiveLength;
        HttpParseBody(Connection);
        if (Connection->Finished)
            HttpDiscardBody(Connection);
        else if (Connection->InConnectionList)
            HttpDeliverBody(Connection, &CompleteList);

        if (Connection->BodyFailed)
        {
            if (Connection->Finished || !Connection->InConnectionList)
                Status = STATUS_INVALID_PARAMETER;
            else
                Reject = TRUE;
        }
        else if (HttpWantsBody(Connection))
            Status = HttpStartBodyReceive(Connection);
        else
            Restart = Connection->Finished;
    }

    if (!NT_SUCCESS(Status))
    {
        InterlockedExchange(&Connection->Shutdown, TRUE);
        if (Connection->InConnectionList)
        {
            HttpFlushBodyIrps(Connection, &CompleteList);
            RemoveEntryList(&Connection->Entry);
            Connection->InConnectionList = FALSE;
            Removed = TRUE;
        }
    }
    ExReleaseFastMutex(&g_HttpGlobalLock);

    HttpCompleteIrpList(&CompleteList);
    if (Status == STATUS_PENDING)
        return;

    if (Restart)
    {
        HttpRestartConnection(Connection);
        return;
    }

    if (Reject)
        HttpRejectBody(Connection);
    if (!NT_SUCCESS(Status))
    {
        HttpRemoveListenerConnection(Connection);
        HttpConnectionCloseSocket(Connection);
    }
    if (Removed)
        HttpConnectionDereference(Connection);
    HttpConnectionDereference(Connection);
}

static VOID HttpSendPage(PHTTP_CONNECTION Connection, USHORT StatusCode, const char *Reason,
                         const char *Heading, const char *Text)
{
    static const char Days[7][4] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
    static const char Months[12][4] =
        {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    char Body[512] = "", Response[1024];
    LARGE_INTEGER Now;
    TIME_FIELDS Time;

    KeQuerySystemTime(&Now);
    RtlTimeToTimeFields(&Now, &Time);
    if ((Heading && !NT_SUCCESS(RtlStringCbPrintfA(Body, sizeof(Body),
            "<!DOCTYPE HTML PUBLIC \"-//W3C//DTD HTML 4.01//EN\"\"http://www.w3.org/TR/html4/strict.dtd\">\r\n"
            "<HTML><HEAD><TITLE>%s</TITLE>\r\n"
            "<META HTTP-EQUIV=\"Content-Type\" Content=\"text/html; charset=us-ascii\"></HEAD>\r\n"
            "<BODY><h2>%s</h2>\r\n<hr><p>HTTP Error %u. %s</p>\r\n</BODY></HTML>\r\n",
            Reason, Heading, StatusCode, Text))) ||
        !NT_SUCCESS(RtlStringCbPrintfA(Response, sizeof(Response),
            "HTTP/1.1 %u %s\r\n%sServer: Microsoft-HTTPAPI/2.0\r\n"
            "Date: %s, %02u %s %u %02u:%02u:%02u GMT\r\nConnection: close\r\nContent-Length: %u\r\n\r\n%s",
            StatusCode, Reason, Heading ? "Content-Type: text/html; charset=us-ascii\r\n" : "",
            Days[Time.Weekday], Time.Day, Months[Time.Month - 1], Time.Year, Time.Hour, Time.Minute,
            Time.Second, (ULONG)strlen(Body), Body)))
    {
        HttpConnectionResetSocket(Connection);
        return;
    }
    HttpWskSendAll(Connection, Response, strlen(Response));
}

static VOID HttpSendUnavailable(PHTTP_CONNECTION Connection)
{
    const char *Heading = "Service Unavailable", *Text = "The service is unavailable.";

    if (Connection->UnavailableVerbosity == Http503ResponseVerbosityBasic)
    {
        HttpConnectionResetSocket(Connection);
        return;
    }
    if (Connection->UnavailableVerbosity == Http503ResponseVerbosityFull)
    {
        if (Connection->UnavailableReason == HttpUnavailableQueueFull)
        {
            Heading = "Application Request Queue Full";
            Text = "The application request queue is full.";
        }
        else if (Connection->UnavailableReason == HttpUnavailableOffline)
        {
            Heading = "Application Offline";
            Text = "The application is offline.";
        }
        else if (Connection->UnavailableReason == HttpUnavailableConnectionLimit)
        {
            Heading = "Forbidden - Too Many Users";
            Text = "There are too many outstanding connections.";
        }
    }

    HttpSendPage(Connection, 503, "Service Unavailable", Heading, Text);
}

static VOID HttpSendErrorResponse(PHTTP_CONNECTION Connection, USHORT StatusCode)
{
    switch (StatusCode)
    {
        case 400:
            HttpSendPage(Connection, 400, "Bad Request", NULL, NULL);
            break;
        case 404:
            HttpSendPage(Connection, 404, "Not Found", "Not Found", "The requested resource is not found.");
            break;
        case 431:
            HttpSendPage(Connection, 400, "Bad Request", "Bad Request - Request Too Long",
                         "The size of the request headers is too long.");
            break;
        case 503:
            HttpSendUnavailable(Connection);
            break;
        default:
            HttpSendPage(Connection, 501, "Not Implemented", NULL, NULL);
            break;
    }
}

static VOID HttpRejectBody(PHTTP_CONNECTION Connection)
{
    ExAcquireFastMutex(&Connection->ResponseMutex);
    if (!Connection->ResponseStarted)
    {
        Connection->ResponseStarted = TRUE;
        HttpSendErrorResponse(Connection, 400);
    }
    ExReleaseFastMutex(&Connection->ResponseMutex);
    HttpConnectionCloseSocket(Connection);
}

static VOID NTAPI HttpConnectionWorker(PDEVICE_OBJECT DeviceObject, PVOID Context)
{
    PHTTP_CONNECTION Connection = Context;
    NTSTATUS Status;

    UNREFERENCED_PARAMETER(DeviceObject);

    if (Connection->ReadingBody)
    {
        HttpBodyReceiveCompleted(Connection);
        return;
    }

    if (Connection->Lingering)
    {
        Connection->ReceivePending = FALSE;
        IoFreeMdl(Connection->ReceiveMdl);
        Connection->ReceiveMdl = NULL;
        if (NT_SUCCESS(Connection->ReceiveStatus) && Connection->ReceiveLength &&
            HttpStartReceive(Connection, Connection->BufferCapacity - 1) == STATUS_PENDING)
            return;
        HttpRemoveListenerConnection(Connection);
        HttpConnectionCloseSocket(Connection);
        HttpConnectionDereference(Connection);
        return;
    }

    if (Connection->ReceivePending)
    {
        Connection->ReceivePending = FALSE;
        Status = HttpReceiveCompleted(Connection);
    }
    else
    {
        Status = HttpParseHeaders(Connection, 0);
    }

    if (Status == STATUS_PENDING)
        return;

    if (NT_SUCCESS(Status))
    {
        if (HttpQueueConnection(Connection))
        {
            HttpConnectionDereference(Connection);
            return;
        }
    }

    HttpRemoveListenerConnection(Connection);
    HttpSendErrorResponse(Connection, Connection->ErrorStatus);
    HttpConnectionLinger(Connection);
    HttpConnectionDereference(Connection);
}

static BOOLEAN HttpQueryParameter(PCWSTR Name, PULONG Result)
{
    UCHAR Buffer[FIELD_OFFSET(KEY_VALUE_PARTIAL_INFORMATION, Data) + sizeof(ULONG)];
    PKEY_VALUE_PARTIAL_INFORMATION Value = (PKEY_VALUE_PARTIAL_INFORMATION)Buffer;
    UNICODE_STRING ValueName;
    ULONG Length;
    NTSTATUS Status;

    RtlInitUnicodeString(&ValueName, Name);
    Status = ZwQueryValueKey(g_ParametersKey, &ValueName, KeyValuePartialInformation,
                             Value, sizeof(Buffer), &Length);
    if (!NT_SUCCESS(Status) || Value->Type != REG_DWORD || Value->DataLength != sizeof(ULONG))
        return FALSE;

    *Result = *(PULONG)Value->Data;
    return TRUE;
}

static LONG HttpQueryTimeout(PCWSTR Name)
{
    ULONG Seconds;

    if (!HttpQueryParameter(Name, &Seconds) || !Seconds || Seconds > MAXUSHORT)
        return HTTP_DEFAULT_TIMEOUT_SECONDS;
    return (LONG)Seconds;
}

static VOID HttpLoadTimeouts(VOID)
{
    if (!g_ParametersKey)
        return;

    InterlockedExchange(&g_IdleConnectionSeconds, HttpQueryTimeout(L"IdleConnectionTimeout"));
    InterlockedExchange(&g_HeaderWaitSeconds, HttpQueryTimeout(L"HeaderWaitTimeout"));
}

static VOID HttpLoadMaxConnections(VOID)
{
    ULONG MaxConnections;

    if (g_ParametersKey && HttpQueryParameter(L"MaxConnections", &MaxConnections) &&
        MaxConnections >= HTTP_MIN_MAX_CONNECTIONS && MaxConnections <= HTTP_MAX_MAX_CONNECTIONS)
        g_MaxConnections = MaxConnections;
}

static VOID NTAPI HttpTimeoutWorker(PDEVICE_OBJECT DeviceObject, PVOID Context)
{
    LIST_ENTRY ExpiredList;
    PLIST_ENTRY ListenerEntry, Entry;
    LONG Now = HttpCurrentSeconds();
    KIRQL Irql;

    UNREFERENCED_PARAMETER(DeviceObject);
    UNREFERENCED_PARAMETER(Context);
    InitializeListHead(&ExpiredList);
    HttpLoadTimeouts();

    ExAcquireFastMutex(&g_HttpGlobalLock);
    for (ListenerEntry = g_ListenersList.Flink;
         ListenerEntry != &g_ListenersList;
         ListenerEntry = ListenerEntry->Flink)
    {
        PHTTP_LISTENER Listener = CONTAINING_RECORD(ListenerEntry, HTTP_LISTENER, Entry);

        KeAcquireSpinLock(&Listener->PendingConnectionLock, &Irql);
        for (Entry = Listener->PendingConnections.Flink;
             Entry != &Listener->PendingConnections;
             Entry = Entry->Flink)
        {
            PHTTP_CONNECTION Connection =
                CONTAINING_RECORD(Entry, HTTP_CONNECTION, ListenerEntry);
            if (Connection->TimedOut ||
                Now - InterlockedCompareExchange(&Connection->DeadlineSeconds, 0, 0) < 0)
                continue;
            Connection->TimedOut = TRUE;
            HttpConnectionReference(Connection);
            InsertTailList(&ExpiredList, &Connection->TimeoutEntry);
        }
        KeReleaseSpinLock(&Listener->PendingConnectionLock, Irql);
    }
    for (Entry = g_ConnectionsList.Flink; Entry != &g_ConnectionsList; Entry = Entry->Flink)
    {
        PHTTP_CONNECTION Connection = CONTAINING_RECORD(Entry, HTTP_CONNECTION, Entry);
        if (!Connection->ReadingBody || Connection->TimedOut ||
            Now - InterlockedCompareExchange(&Connection->DeadlineSeconds, 0, 0) < 0)
            continue;
        Connection->TimedOut = TRUE;
        HttpConnectionReference(Connection);
        InsertTailList(&ExpiredList, &Connection->TimeoutEntry);
    }
    ExReleaseFastMutex(&g_HttpGlobalLock);

    while (!IsListEmpty(&ExpiredList))
    {
        PHTTP_CONNECTION Connection =
            CONTAINING_RECORD(RemoveHeadList(&ExpiredList), HTTP_CONNECTION, TimeoutEntry);
        HttpConnectionResetSocket(Connection);
        HttpConnectionDereference(Connection);
    }

    InterlockedExchange(&g_TimeoutWorkQueued, FALSE);
}

static VOID NTAPI HttpTimeoutDpc(
    _In_ PKDPC Dpc,
    _In_opt_ PVOID DeferredContext,
    _In_opt_ PVOID SystemArgument1,
    _In_opt_ PVOID SystemArgument2)
{
    UNREFERENCED_PARAMETER(Dpc);
    UNREFERENCED_PARAMETER(DeferredContext);
    UNREFERENCED_PARAMETER(SystemArgument1);
    UNREFERENCED_PARAMETER(SystemArgument2);

    if (!InterlockedCompareExchange(&g_TimeoutWorkQueued, TRUE, FALSE))
        IoQueueWorkItem(g_TimeoutWorkItem, HttpTimeoutWorker, DelayedWorkQueue, NULL);
}

static BOOLEAN ParseUrlPrefix(
    _In_ const char *Url,
    _In_ size_t UrlLength,
    _Out_ HTTP_URL_CATEGORY *Category,
    _Out_ PUSHORT Port,
    _Out_ const char **Host,
    _Out_ size_t *HostLength,
    _Out_ const char **Path,
    _Out_ size_t *PathLength,
    _Out_ PIN_ADDR BoundAddress)
{
    const char *p, *end, *host_end, *port_end, *path;
    const char *terminator;
    const char *label_start;
    size_t host_len, path_len, port_len;
    ULONG port_num = 0;
    BOOLEAN numeric_host = TRUE;
    IN_ADDR ipv4;

    if (UrlLength <= 7 || memcmp(Url, "http://", 7) != 0)
        return FALSE;
    RtlZeroMemory(BoundAddress, sizeof(*BoundAddress));

    p = Url + 7;
    end = Url + UrlLength;

    if (*p == '[')
    {
        return FALSE;
    }
    else
    {
        host_end = memchr(p, ':', end - p);
        if (!host_end)
            return FALSE;
    }

    host_len = host_end - p;
    if (!host_len)
        return FALSE;

    if (*host_end != ':')
        return FALSE;

    p = host_end + 1;
    path = memchr(p, '/', end - p);
    port_end = path ? path : end;
    port_len = port_end - p;
    if (!port_len || port_len > 5 || (port_len > 1 && *p == '0'))
        return FALSE;
    for (; p < port_end; ++p)
    {
        if (*p < '0' || *p > '9')
            return FALSE;
        port_num = port_num * 10 + (*p - '0');
        if (port_num > 65535)
            return FALSE;
    }
    if (port_num == 0)
        return FALSE;

    *Port = (USHORT)port_num;

    if (!path)
        return FALSE;
    path_len = end - path;
    if (memchr(path, '?', path_len) || memchr(path, '#', path_len))
        return FALSE;

    if (host_len == 1 && Url[7] == '+')
        *Category = HttpUrlCategoryStrongWildcard;
    else if (host_len == 1 && Url[7] == '*')
        *Category = HttpUrlCategoryWeakWildcard;
    else
    {
        if (NT_SUCCESS(RtlIpv4StringToAddressA(Url + 7, TRUE, &terminator, &ipv4)) &&
            terminator == host_end)
        {
            *Category = HttpUrlCategoryIPBound;
            *BoundAddress = ipv4;
        }
        else
        {
            label_start = Url + 7;
            for (p = label_start; p <= host_end; ++p)
            {
                if (p < host_end && *p != '.')
                {
                    if (*p < '0' || *p > '9')
                        numeric_host = FALSE;
                    if (!((*p >= 'a' && *p <= 'z') ||
                          (*p >= 'A' && *p <= 'Z') ||
                          (*p >= '0' && *p <= '9') || *p == '-'))
                        return FALSE;
                    continue;
                }

                if (p == label_start || p - label_start > 63 ||
                    *label_start == '-' || p[-1] == '-')
                    return FALSE;
                label_start = p + 1;
            }
            if (numeric_host)
                return FALSE;
            *Category = HttpUrlCategoryExplicit;
        }
    }

    *Host = Url + 7;
    *HostLength = host_len;
    *Path = path;
    *PathLength = path_len;
    return TRUE;
}

static NTSTATUS HttpIoctlAddUrl(PHTTP_REQUEST_QUEUE Queue, PIRP Irp)
{
    PIO_STACK_LOCATION Stack = IoGetCurrentIrpStackLocation(Irp);
    const struct http_add_url_params *Params = Irp->AssociatedIrp.SystemBuffer;
    ULONG InputLen = Stack->Parameters.DeviceIoControl.InputBufferLength;
    HTTP_URL_CATEGORY Category;
    USHORT Port;
    const char *Host, *Path;
    size_t HostLen, PathLen, UrlLen, ParamsLen;
    IN_ADDR BoundAddress;
    SIZE_T AllocationSize;
    PLIST_ENTRY QueueEntry, UrlEntry;
    PHTTP_URL_ENTRY NewEntry;
    PHTTP_LISTENER Listener;
    NTSTATUS Status;

    if (InputLen < FIELD_OFFSET(struct http_add_url_params, url) + 1)
        return STATUS_INVALID_PARAMETER;

    ParamsLen = InputLen - FIELD_OFFSET(struct http_add_url_params, url);
    {
        const char *terminator = memchr(Params->url, '\0', ParamsLen);
        if (!terminator || terminator == Params->url)
            return STATUS_INVALID_PARAMETER;
        UrlLen = terminator - Params->url;
    }

    if (!strncmp(Params->url, "https://", 8))
        return STATUS_NOT_IMPLEMENTED;
    if (UrlLen > 7 && !memcmp(Params->url, "http://[", 8))
        return STATUS_NOT_SUPPORTED;

    if (!ParseUrlPrefix(Params->url, UrlLen, &Category, &Port, &Host, &HostLen, &Path, &PathLen,
                        &BoundAddress))
        return STATUS_INVALID_PARAMETER;

    if (UrlLen > ((SIZE_T)-1 - sizeof(*NewEntry) - 3) / 3)
        return STATUS_INVALID_PARAMETER;
    AllocationSize = sizeof(*NewEntry) + UrlLen + HostLen + PathLen + 3;

    NewEntry = ExAllocatePoolZero(NonPagedPoolNx, AllocationSize, TAG_HTTP);
    if (!NewEntry)
        return STATUS_NO_MEMORY;

    NewEntry->Url = (char *)(NewEntry + 1);
    NewEntry->Host = NewEntry->Url + UrlLen + 1;
    NewEntry->Path = NewEntry->Host + HostLen + 1;
    memcpy(NewEntry->Url, Params->url, UrlLen + 1);
    memcpy(NewEntry->Host, Host, HostLen);
    NewEntry->Host[HostLen] = '\0';
    memcpy(NewEntry->Path, Path, PathLen);
    NewEntry->Path[PathLen] = '\0';
    NewEntry->UrlLength = UrlLen;
    NewEntry->PathLength = PathLen;
    NewEntry->Context = Params->context;
    NewEntry->Group = Params->group;
    NewEntry->Config = Params->config;
    NewEntry->Category = Category;
    NewEntry->Port = Port;

    ExAcquireFastMutex(&g_HttpGlobalLock);

    ExAcquireFastMutex(&Queue->Mutex);
    if (Queue->State & HTTP_QUEUE_STATE_CLOSING)
    {
        ExReleaseFastMutex(&Queue->Mutex);
        ExReleaseFastMutex(&g_HttpGlobalLock);
        ExFreePoolWithTag(NewEntry, TAG_HTTP);
        return STATUS_FILE_CLOSED;
    }
    ExReleaseFastMutex(&Queue->Mutex);

    for (QueueEntry = g_RequestQueuesList.Flink; QueueEntry != &g_RequestQueuesList; QueueEntry = QueueEntry->Flink)
    {
        PHTTP_REQUEST_QUEUE Q = CONTAINING_RECORD(QueueEntry, HTTP_REQUEST_QUEUE, Entry);
        for (UrlEntry = Q->UrlList.Flink; UrlEntry != &Q->UrlList; UrlEntry = UrlEntry->Flink)
        {
            PHTTP_URL_ENTRY Existing = CONTAINING_RECORD(UrlEntry, HTTP_URL_ENTRY, Entry);
            if (Existing->Category == Category && Existing->Port == Port &&
                strlen(Existing->Host) == HostLen &&
                !_strnicmp(Existing->Host, Host, HostLen) &&
                strlen(Existing->Path) == PathLen &&
                !_strnicmp(Existing->Path, Path, PathLen))
            {
                ExReleaseFastMutex(&g_HttpGlobalLock);
                ExFreePoolWithTag(NewEntry, TAG_HTTP);
                return STATUS_OBJECT_NAME_COLLISION;
            }
        }
    }

    Listener = HttpFindListener(Port);
    if (!Listener)
    {
        Status = HttpCreateListener(Port, &Listener);
        if (!NT_SUCCESS(Status))
        {
            ExReleaseFastMutex(&g_HttpGlobalLock);
            ExFreePoolWithTag(NewEntry, TAG_HTTP);
            return Status;
        }
        InsertTailList(&g_ListenersList, &Listener->Entry);
    }

    NewEntry->Listener = Listener;
    NewEntry->BoundAddress = BoundAddress;
    ++Listener->UrlCount;
    InsertTailList(&Queue->UrlList, &NewEntry->Entry);

    ExReleaseFastMutex(&g_HttpGlobalLock);
    return STATUS_SUCCESS;
}

static NTSTATUS HttpIoctlRemoveUrl(PHTTP_REQUEST_QUEUE Queue, PIRP Irp)
{
    PIO_STACK_LOCATION Stack = IoGetCurrentIrpStackLocation(Irp);
    const char *Url = Irp->AssociatedIrp.SystemBuffer;
    ULONG InputLen = Stack->Parameters.DeviceIoControl.InputBufferLength;
    PLIST_ENTRY Entry;
    PHTTP_LISTENER Listener = NULL;

    if (!Url || !InputLen || !memchr(Url, '\0', InputLen))
        return STATUS_INVALID_PARAMETER;

    ExAcquireFastMutex(&g_HttpGlobalLock);

    for (Entry = Queue->UrlList.Flink; Entry != &Queue->UrlList; Entry = Entry->Flink)
    {
        PHTTP_URL_ENTRY UrlEntry = CONTAINING_RECORD(Entry, HTTP_URL_ENTRY, Entry);
        if (!_stricmp(UrlEntry->Url, Url))
        {
            RemoveEntryList(&UrlEntry->Entry);
            Listener = UrlEntry->Listener;
            if (--Listener->UrlCount == 0)
                RemoveEntryList(&Listener->Entry);
            ExFreePoolWithTag(UrlEntry, TAG_HTTP);
            ExReleaseFastMutex(&g_HttpGlobalLock);

            if (!Listener->UrlCount)
            {
                HttpCloseListener(Listener);
            }
            return STATUS_SUCCESS;
        }
    }

    ExReleaseFastMutex(&g_HttpGlobalLock);
    return STATUS_OBJECT_NAME_NOT_FOUND;
}

static NTSTATUS HttpIoctlSetUrlConfig(PHTTP_REQUEST_QUEUE Queue, PIRP Irp)
{
    PIO_STACK_LOCATION Stack = IoGetCurrentIrpStackLocation(Irp);
    const struct http_url_config_params *Params = Irp->AssociatedIrp.SystemBuffer;
    PLIST_ENTRY Entry;

    if (Stack->Parameters.DeviceIoControl.InputBufferLength < sizeof(*Params) || !Params->group)
        return STATUS_INVALID_PARAMETER;

    ExAcquireFastMutex(&g_HttpGlobalLock);
    for (Entry = Queue->UrlList.Flink; Entry != &Queue->UrlList; Entry = Entry->Flink)
    {
        PHTTP_URL_ENTRY UrlEntry = CONTAINING_RECORD(Entry, HTTP_URL_ENTRY, Entry);
        if (UrlEntry->Group == Params->group)
            UrlEntry->Config = Params->config;
    }
    ExReleaseFastMutex(&g_HttpGlobalLock);
    Irp->IoStatus.Information = 0;
    return STATUS_SUCCESS;
}

static VOID NTAPI HttpCsqInsertIrp(
    _In_ PIO_CSQ Csq,
    _Inout_ PIRP Irp)
{
    PHTTP_REQUEST_QUEUE Queue = CONTAINING_RECORD(Csq, HTTP_REQUEST_QUEUE, Csq);

    InsertTailList(&Queue->PendingIrpQueue, &Irp->Tail.Overlay.ListEntry);
}

static VOID NTAPI HttpCsqRemoveIrp(
    _In_ PIO_CSQ Csq,
    _Inout_ PIRP Irp)
{
    UNREFERENCED_PARAMETER(Csq);
    RemoveEntryList(&Irp->Tail.Overlay.ListEntry);
}

static PIRP NTAPI HttpCsqPeekNextIrp(
    _In_ PIO_CSQ Csq,
    _In_opt_ PIRP Irp,
    _In_opt_ PVOID PeekContext)
{
    PHTTP_REQUEST_QUEUE Queue = CONTAINING_RECORD(Csq, HTTP_REQUEST_QUEUE, Csq);
    PLIST_ENTRY Entry;

    UNREFERENCED_PARAMETER(PeekContext);
    Entry = Irp ? Irp->Tail.Overlay.ListEntry.Flink : Queue->PendingIrpQueue.Flink;
    if (Entry == &Queue->PendingIrpQueue)
        return NULL;
    return CONTAINING_RECORD(Entry, IRP, Tail.Overlay.ListEntry);
}

static VOID NTAPI HttpCsqAcquireLock(
    _In_ PIO_CSQ Csq,
    _Out_ PKIRQL Irql)
{
    PHTTP_REQUEST_QUEUE Queue = CONTAINING_RECORD(Csq, HTTP_REQUEST_QUEUE, Csq);

    KeAcquireSpinLock(&Queue->PendingIrpLock, Irql);
}

static VOID NTAPI HttpCsqReleaseLock(
    _In_ PIO_CSQ Csq,
    _In_ KIRQL Irql)
{
    PHTTP_REQUEST_QUEUE Queue = CONTAINING_RECORD(Csq, HTTP_REQUEST_QUEUE, Csq);

    KeReleaseSpinLock(&Queue->PendingIrpLock, Irql);
}

static VOID NTAPI HttpCsqCompleteCanceledIrp(
    _In_ PIO_CSQ Csq,
    _Inout_ PIRP Irp)
{
    UNREFERENCED_PARAMETER(Csq);
    Irp->IoStatus.Status = STATUS_CANCELLED;
    Irp->IoStatus.Information = 0;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
}

static NTSTATUS NTAPI HttpBodyCsqInsertIrp(
    _In_ PIO_CSQ Csq,
    _In_ PIRP Irp,
    _In_ PVOID InsertContext)
{
    PHTTP_REQUEST_QUEUE Queue = CONTAINING_RECORD(Csq, HTTP_REQUEST_QUEUE, BodyCsq);

    if (InsertContext)
        InsertHeadList(&Queue->PendingBodyIrpQueue, &Irp->Tail.Overlay.ListEntry);
    else
        InsertTailList(&Queue->PendingBodyIrpQueue, &Irp->Tail.Overlay.ListEntry);
    return STATUS_SUCCESS;
}

static PIRP NTAPI HttpBodyCsqPeekNextIrp(
    _In_ PIO_CSQ Csq,
    _In_opt_ PIRP Irp,
    _In_opt_ PVOID PeekContext)
{
    PHTTP_REQUEST_QUEUE Queue = CONTAINING_RECORD(Csq, HTTP_REQUEST_QUEUE, BodyCsq);
    PLIST_ENTRY Entry;

    for (Entry = Irp ? Irp->Tail.Overlay.ListEntry.Flink : Queue->PendingBodyIrpQueue.Flink;
         Entry != &Queue->PendingBodyIrpQueue;
         Entry = Entry->Flink)
    {
        PIRP Next = CONTAINING_RECORD(Entry, IRP, Tail.Overlay.ListEntry);
        if (!PeekContext || Next->Tail.Overlay.DriverContext[0] == PeekContext)
            return Next;
    }

    return NULL;
}

static VOID NTAPI HttpBodyCsqAcquireLock(
    _In_ PIO_CSQ Csq,
    _Out_ PKIRQL Irql)
{
    PHTTP_REQUEST_QUEUE Queue = CONTAINING_RECORD(Csq, HTTP_REQUEST_QUEUE, BodyCsq);

    KeAcquireSpinLock(&Queue->PendingBodyIrpLock, Irql);
}

static VOID NTAPI HttpBodyCsqReleaseLock(
    _In_ PIO_CSQ Csq,
    _In_ KIRQL Irql)
{
    PHTTP_REQUEST_QUEUE Queue = CONTAINING_RECORD(Csq, HTTP_REQUEST_QUEUE, BodyCsq);

    KeReleaseSpinLock(&Queue->PendingBodyIrpLock, Irql);
}

static VOID NTAPI HttpDisconnectCsqInsertIrp(
    _In_ PIO_CSQ Csq,
    _Inout_ PIRP Irp)
{
    UNREFERENCED_PARAMETER(Csq);
    InsertTailList(&g_DisconnectIrpQueue, &Irp->Tail.Overlay.ListEntry);
}

static PIRP NTAPI HttpDisconnectCsqPeekNextIrp(
    _In_ PIO_CSQ Csq,
    _In_opt_ PIRP Irp,
    _In_opt_ PVOID PeekContext)
{
    PHTTP_DISCONNECT_PEEK Peek = PeekContext;
    PLIST_ENTRY Entry;

    UNREFERENCED_PARAMETER(Csq);
    for (Entry = Irp ? Irp->Tail.Overlay.ListEntry.Flink : g_DisconnectIrpQueue.Flink;
         Entry != &g_DisconnectIrpQueue;
         Entry = Entry->Flink)
    {
        PIRP Next = CONTAINING_RECORD(Entry, IRP, Tail.Overlay.ListEntry);
        if (!Peek ||
            Next->Tail.Overlay.DriverContext[0] == Peek->Connection ||
            Next->Tail.Overlay.DriverContext[1] == Peek->Queue)
            return Next;
    }

    return NULL;
}

static VOID NTAPI HttpDisconnectCsqAcquireLock(
    _In_ PIO_CSQ Csq,
    _Out_ PKIRQL Irql)
{
    UNREFERENCED_PARAMETER(Csq);
    KeAcquireSpinLock(&g_DisconnectIrpLock, Irql);
}

static VOID NTAPI HttpDisconnectCsqReleaseLock(
    _In_ PIO_CSQ Csq,
    _In_ KIRQL Irql)
{
    UNREFERENCED_PARAMETER(Csq);
    KeReleaseSpinLock(&g_DisconnectIrpLock, Irql);
}

static NTSTATUS HttpInitializeRequestQueue(PHTTP_REQUEST_QUEUE Queue)
{
    NTSTATUS Status;

    KeInitializeSpinLock(&Queue->PendingIrpLock);
    KeInitializeSpinLock(&Queue->PendingBodyIrpLock);
    Status = IoCsqInitialize(&Queue->Csq,
                             HttpCsqInsertIrp,
                             HttpCsqRemoveIrp,
                             HttpCsqPeekNextIrp,
                             HttpCsqAcquireLock,
                             HttpCsqReleaseLock,
                             HttpCsqCompleteCanceledIrp);
    if (!NT_SUCCESS(Status))
        return Status;

    return IoCsqInitializeEx(&Queue->BodyCsq,
                             HttpBodyCsqInsertIrp,
                             HttpCsqRemoveIrp,
                             HttpBodyCsqPeekNextIrp,
                             HttpBodyCsqAcquireLock,
                             HttpBodyCsqReleaseLock,
                             HttpCsqCompleteCanceledIrp);
}

static NTSTATUS HttpReceiveRequest(PHTTP_REQUEST_QUEUE Queue, PIRP Irp)
{
    PIO_STACK_LOCATION Stack = IoGetCurrentIrpStackLocation(Irp);
    const struct http_receive_request_params *Params = Irp->AssociatedIrp.SystemBuffer;
    ULONG InputLen = Stack->Parameters.DeviceIoControl.InputBufferLength;
    PHTTP_CONNECTION Conn;
    NTSTATUS Status;

    if (InputLen < sizeof(struct http_receive_request_params))
        return STATUS_INVALID_PARAMETER;

    ExAcquireFastMutex(&Queue->Mutex);
    Status = (Queue->State & HTTP_QUEUE_STATE_SHUTDOWN) ? STATUS_INVALID_HANDLE : STATUS_SUCCESS;
    ExReleaseFastMutex(&Queue->Mutex);
    if (!NT_SUCCESS(Status))
        return Status;

    ExAcquireFastMutex(&g_HttpGlobalLock);

    if (Params->id != HTTP_NULL_ID)
    {
        Conn = GetConnectionById(Params->id);
        if (Conn && Conn->Available && Conn->Queue == Queue)
        {
            Status = complete_irp(Conn, Irp);
            ExReleaseFastMutex(&g_HttpGlobalLock);
            return Status;
        }

        ExReleaseFastMutex(&g_HttpGlobalLock);
        return STATUS_CONNECTION_INVALID;
    }

    for (PLIST_ENTRY Entry = g_ConnectionsList.Flink;
         Entry != &g_ConnectionsList;
         Entry = Entry->Flink)
    {
        Conn = CONTAINING_RECORD(Entry, HTTP_CONNECTION, Entry);
        if (Conn->Queue == Queue && Conn->Available)
        {
            Status = complete_irp(Conn, Irp);
            ExReleaseFastMutex(&g_HttpGlobalLock);
            return Status;
        }
    }

    ExAcquireFastMutex(&Queue->Mutex);
    if (Queue->State & (HTTP_QUEUE_STATE_CLOSING | HTTP_QUEUE_STATE_SHUTDOWN))
    {
        Status = (Queue->State & HTTP_QUEUE_STATE_CLOSING) ? STATUS_FILE_CLOSED : STATUS_INVALID_HANDLE;
        ExReleaseFastMutex(&Queue->Mutex);
        ExReleaseFastMutex(&g_HttpGlobalLock);
        return Status;
    }

    IoMarkIrpPending(Irp);
    IoCsqInsertIrp(&Queue->Csq, Irp, NULL);
    ExReleaseFastMutex(&Queue->Mutex);
    ExReleaseFastMutex(&g_HttpGlobalLock);

    return STATUS_PENDING;
}

static NTSTATUS HttpSendResponseHeaders(
    PHTTP_CONNECTION Connection,
    const char *Buffer,
    ULONG Length,
    PULONG Sent)
{
    const char *Header = NULL;
    PCHAR Response;
    ULONG StatusLength, HeaderLength;
    NTSTATUS Status;

    if (!Connection->KeepAlive)
        Connection->CloseAfterResponse = TRUE;

    if (Connection->CloseAfterResponse)
        Header = "Connection: close\r\n";
    else if (!Connection->Version.MinorVersion)
        Header = "Connection: keep-alive\r\n";

    for (StatusLength = 0; StatusLength + 1 < Length; ++StatusLength)
    {
        if (Buffer[StatusLength] == '\r' && Buffer[StatusLength + 1] == '\n')
            break;
    }
    *Sent = Length;
    if (!Header || StatusLength + 1 >= Length)
        return HttpWskSendAll(Connection, Buffer, Length);

    StatusLength += 2;
    HeaderLength = (ULONG)strlen(Header);
    Response = ExAllocatePoolWithTag(NonPagedPoolNx, Length + HeaderLength, TAG_HTTP);
    if (!Response)
        return STATUS_INSUFFICIENT_RESOURCES;

    memcpy(Response, Buffer, StatusLength);
    memcpy(Response + StatusLength, Header, HeaderLength);
    memcpy(Response + StatusLength + HeaderLength, Buffer + StatusLength, Length - StatusLength);
    *Sent = Length + HeaderLength;
    Status = HttpWskSendAll(Connection, Response, Length + HeaderLength);
    ExFreePoolWithTag(Response, TAG_HTTP);
    return Status;
}

static NTSTATUS HttpSendResponse(PHTTP_REQUEST_QUEUE Queue, PIRP Irp)
{
    PIO_STACK_LOCATION Stack = IoGetCurrentIrpStackLocation(Irp);
    const struct http_response *Response = Irp->AssociatedIrp.SystemBuffer;
    ULONG InputLen = Stack->Parameters.DeviceIoControl.InputBufferLength;
    ULONG ResponseHeaderSize = FIELD_OFFSET(struct http_response, buffer);
    LIST_ENTRY CompleteList;
    PHTTP_CONNECTION Conn;
    BOOLEAN Finished = FALSE, Removed = FALSE, Close = FALSE, Restart = FALSE, Drain = FALSE, Linger = FALSE;
    ULONG Sent = (ULONG)Response->len;
    NTSTATUS Status;

    if (InputLen < ResponseHeaderSize ||
        Response->len < 0 ||
        (ULONG)Response->len > InputLen - ResponseHeaderSize ||
        (Response->response_flags &
         ~(HTTP_SEND_RESPONSE_FLAG_MORE_DATA | HTTP_SEND_RESPONSE_FLAG_DISCONNECT)))
        return STATUS_INVALID_PARAMETER;

    InitializeListHead(&CompleteList);
    ExAcquireFastMutex(&g_HttpGlobalLock);
    Conn = GetConnectionById(Response->id);
    if (Conn && Conn->Queue == Queue && Conn->BodyFailed)
    {
        RemoveEntryList(&Conn->Entry);
        Conn->InConnectionList = FALSE;
        HttpFlushBodyIrps(Conn, &CompleteList);
        ExReleaseFastMutex(&g_HttpGlobalLock);
        HttpCompleteIrpList(&CompleteList);
        HttpConnectionDereference(Conn);
        return STATUS_INVALID_DEVICE_STATE;
    }
    if (!Conn || Conn->Queue != Queue ||
        InterlockedCompareExchange(&Conn->Shutdown, 0, 0))
    {
        ExReleaseFastMutex(&g_HttpGlobalLock);
        return STATUS_CONNECTION_INVALID;
    }

    HttpConnectionReference(Conn);
    ExReleaseFastMutex(&g_HttpGlobalLock);

    ExAcquireFastMutex(&Conn->ResponseMutex);
    if (Response->response_flags & HTTP_SEND_RESPONSE_FLAG_DISCONNECT)
        Conn->CloseAfterResponse = TRUE;
    if (InterlockedCompareExchange(&Conn->Shutdown, 0, 0))
    {
        Status = STATUS_CONNECTION_INVALID;
    }
    else if (Response->reset)
    {
        Status = STATUS_SUCCESS;
        Close = TRUE;
    }
    else
    {
        if (Conn->ResponseStarted)
            Status = HttpWskSendAll(Conn, Response->buffer, (ULONG)Response->len);
        else
            Status = HttpSendResponseHeaders(Conn, Response->buffer, (ULONG)Response->len, &Sent);
        if (NT_SUCCESS(Status))
        {
            Conn->ResponseStarted = TRUE;
            Irp->IoStatus.Information = Response->sent + Sent;
        }
        else
        {
            Close = TRUE;
        }
    }

    if (Close || !(Response->response_flags & HTTP_SEND_RESPONSE_FLAG_MORE_DATA))
        Finished = TRUE;

    if (Finished)
    {
        ExAcquireFastMutex(&g_HttpGlobalLock);
        if (Conn->InConnectionList)
        {
            RemoveEntryList(&Conn->Entry);
            Conn->InConnectionList = FALSE;
            Removed = TRUE;
            HttpFlushBodyIrps(Conn, &CompleteList);

            if (!Close && !Conn->CloseAfterResponse && HttpInsertListenerConnection(Conn))
            {
                Conn->Finished = TRUE;
                HttpConsumeHeaders(Conn);
                HttpDiscardBody(Conn);
                if (HttpWantsBody(Conn))
                {
                    HttpConnectionReference(Conn);
                    Drain = TRUE;
                    if (HttpStartBodyReceive(Conn) == STATUS_PENDING)
                        Drain = FALSE;
                    else
                        Close = TRUE;
                }
                else if (!Conn->ReadingBody)
                {
                    HttpConnectionReference(Conn);
                    Restart = TRUE;
                }
            }
            else if (!Close && Conn->CloseAfterResponse && !Conn->ReadingBody)
            {
                Linger = TRUE;
            }
            else
            {
                Close = TRUE;
            }

            if (Close)
                InterlockedExchange(&Conn->Shutdown, TRUE);
        }
        ExReleaseFastMutex(&g_HttpGlobalLock);
    }

    ExReleaseFastMutex(&Conn->ResponseMutex);
    HttpCompleteIrpList(&CompleteList);

    if (Restart)
        HttpRestartConnection(Conn);
    if (Close && Removed)
    {
        HttpRemoveListenerConnection(Conn);
        if (Response->reset)
            HttpConnectionResetSocket(Conn);
        else
            HttpConnectionCloseSocket(Conn);
    }
    if (Linger)
        HttpConnectionLinger(Conn);
    if (Drain)
        HttpConnectionDereference(Conn);
    if (Removed)
        HttpConnectionDereference(Conn);
    HttpConnectionDereference(Conn);
    return Status;
}

static NTSTATUS HttpReceiveBody(PHTTP_REQUEST_QUEUE Queue, PIRP Irp)
{
    PIO_STACK_LOCATION Stack = IoGetCurrentIrpStackLocation(Irp);
    const struct http_receive_body_params *Params = Irp->AssociatedIrp.SystemBuffer;
    ULONG InputLen = Stack->Parameters.DeviceIoControl.InputBufferLength;
    LIST_ENTRY CompleteList;
    PHTTP_CONNECTION Conn;
    BOOLEAN Failed = FALSE;
    NTSTATUS Status;
    ULONG Flags;

    if (InputLen < sizeof(struct http_receive_body_params))
        return STATUS_INVALID_PARAMETER;

    Flags = Params->flags;
    InitializeListHead(&CompleteList);
    ExAcquireFastMutex(&g_HttpGlobalLock);
    Conn = GetConnectionById(Params->id);
    if (!Conn || Conn->Queue != Queue ||
        (!Conn->BodyFailed && InterlockedCompareExchange(&Conn->Shutdown, 0, 0)))
    {
        ExReleaseFastMutex(&g_HttpGlobalLock);
        return STATUS_CONNECTION_INVALID;
    }

    HttpConsumeHeaders(Conn);
    Irp->IoStatus.Information = 0;
    Irp->Tail.Overlay.DriverContext[0] = Conn;
    Irp->Tail.Overlay.DriverContext[1] = (PVOID)(ULONG_PTR)Flags;
    if (HttpCopyBody(Conn, Irp))
    {
        Status = HttpBodyStatus(Conn, Irp);
        if (HttpWantsBody(Conn))
        {
            HttpConnectionReference(Conn);
            Failed = HttpStartBodyReceive(Conn) != STATUS_PENDING;
        }
        ExReleaseFastMutex(&g_HttpGlobalLock);
        if (Failed)
            HttpConnectionDereference(Conn);
        return Status;
    }

    ExAcquireFastMutex(&Queue->Mutex);
    if (Queue->State & HTTP_QUEUE_STATE_CLOSING)
    {
        ExReleaseFastMutex(&Queue->Mutex);
        ExReleaseFastMutex(&g_HttpGlobalLock);
        Irp->IoStatus.Information = 0;
        return STATUS_FILE_CLOSED;
    }
    IoCsqInsertIrp(&Queue->BodyCsq, Irp, NULL);
    ExReleaseFastMutex(&Queue->Mutex);

    if (HttpWantsBody(Conn))
    {
        HttpConnectionReference(Conn);
        Status = HttpStartBodyReceive(Conn);
        if (Status != STATUS_PENDING)
        {
            Failed = TRUE;
            HttpFlushBodyIrps(Conn, &CompleteList);
        }
    }
    ExReleaseFastMutex(&g_HttpGlobalLock);

    HttpCompleteIrpList(&CompleteList);
    if (Failed)
        HttpConnectionDereference(Conn);
    return STATUS_PENDING;
}

static NTSTATUS HttpSetQueueProperty(PHTTP_REQUEST_QUEUE Queue, PIRP Irp)
{
    PIO_STACK_LOCATION Stack = IoGetCurrentIrpStackLocation(Irp);
    const struct http_queue_property *Params = Irp->AssociatedIrp.SystemBuffer;
    NTSTATUS Status = STATUS_SUCCESS;

    if (Stack->Parameters.DeviceIoControl.InputBufferLength < sizeof(*Params))
        return STATUS_INVALID_PARAMETER;
    if (!Queue->Version2)
        return STATUS_NOT_SUPPORTED;

    ExAcquireFastMutex(&Queue->Mutex);
    switch (Params->property)
    {
        case HttpServerQueueLengthProperty:
            if (Params->value < HTTP_MIN_SERVER_QUEUE_LENGTH || Params->value > HTTP_MAX_SERVER_QUEUE_LENGTH)
                Status = STATUS_NOT_SUPPORTED;
            else
                Queue->MaxQueueLength = Params->value;
            break;
        case HttpServerStateProperty:
            if (Params->value > 5)
                Status = STATUS_NOT_SUPPORTED;
            else
                Queue->EnabledState = Params->value;
            break;
        case HttpServer503VerbosityProperty:
            if (Params->value > Http503ResponseVerbosityFull)
                Status = STATUS_NOT_SUPPORTED;
            else
                Queue->Verbosity = Params->value;
            break;
        default:
            Status = STATUS_INVALID_PARAMETER;
            break;
    }
    ExReleaseFastMutex(&Queue->Mutex);
    Irp->IoStatus.Information = 0;
    return Status;
}

static NTSTATUS HttpQueryQueueProperty(PHTTP_REQUEST_QUEUE Queue, PIRP Irp)
{
    PIO_STACK_LOCATION Stack = IoGetCurrentIrpStackLocation(Irp);
    PULONG Buffer = Irp->AssociatedIrp.SystemBuffer;
    ULONG Value;

    Irp->IoStatus.Information = 0;
    if (Stack->Parameters.DeviceIoControl.InputBufferLength < sizeof(ULONG) ||
        Stack->Parameters.DeviceIoControl.OutputBufferLength < sizeof(ULONG))
        return STATUS_INVALID_PARAMETER;
    if (!Queue->Version2)
        return STATUS_NOT_SUPPORTED;

    ExAcquireFastMutex(&Queue->Mutex);
    switch (*Buffer)
    {
        case HttpServerQueueLengthProperty:
            Value = Queue->MaxQueueLength;
            break;
        case HttpServerStateProperty:
            Value = Queue->EnabledState;
            break;
        case HttpServer503VerbosityProperty:
            Value = Queue->Verbosity;
            break;
        default:
            ExReleaseFastMutex(&Queue->Mutex);
            return STATUS_INVALID_PARAMETER;
    }
    ExReleaseFastMutex(&Queue->Mutex);

    *Buffer = Value;
    Irp->IoStatus.Information = sizeof(ULONG);
    return STATUS_SUCCESS;
}

C_ASSERT(FIELD_OFFSET(struct http_version_ea, name) == FIELD_OFFSET(FILE_FULL_EA_INFORMATION, EaName));

static NTSTATUS HttpShutdownQueue(PHTTP_REQUEST_QUEUE Queue)
{
    LIST_ENTRY CancelList;
    PIRP Irp;

    InitializeListHead(&CancelList);
    ExAcquireFastMutex(&Queue->Mutex);
    Queue->State |= HTTP_QUEUE_STATE_SHUTDOWN;
    while ((Irp = IoCsqRemoveNextIrp(&Queue->Csq, NULL)) != NULL)
    {
        Irp->IoStatus.Status = STATUS_CANCELLED;
        Irp->IoStatus.Information = 0;
        InsertTailList(&CancelList, &Irp->Tail.Overlay.ListEntry);
    }
    ExReleaseFastMutex(&Queue->Mutex);

    HttpCompleteIrpList(&CancelList);
    return STATUS_SUCCESS;
}

static NTSTATUS HttpCancelRequest(PHTTP_REQUEST_QUEUE Queue, PIRP Irp)
{
    PIO_STACK_LOCATION Stack = IoGetCurrentIrpStackLocation(Irp);
    LIST_ENTRY CompleteList;
    PHTTP_CONNECTION Conn;
    PIRP BodyIrp;

    if (Stack->Parameters.DeviceIoControl.InputBufferLength < sizeof(HTTP_REQUEST_ID))
        return STATUS_INVALID_PARAMETER;

    InitializeListHead(&CompleteList);
    ExAcquireFastMutex(&g_HttpGlobalLock);
    Conn = GetConnectionById(*(HTTP_REQUEST_ID *)Irp->AssociatedIrp.SystemBuffer);
    if (!Conn || Conn->Queue != Queue || InterlockedCompareExchange(&Conn->Shutdown, 0, 0))
    {
        ExReleaseFastMutex(&g_HttpGlobalLock);
        return STATUS_CONNECTION_INVALID;
    }

    RemoveEntryList(&Conn->Entry);
    Conn->InConnectionList = FALSE;
    Conn->Cancelled = TRUE;
    InterlockedExchange(&Conn->Shutdown, TRUE);
    while ((BodyIrp = IoCsqRemoveNextIrp(&Queue->BodyCsq, Conn)) != NULL)
    {
        BodyIrp->IoStatus.Status = STATUS_CANCELLED;
        InsertTailList(&CompleteList, &BodyIrp->Tail.Overlay.ListEntry);
    }
    ExReleaseFastMutex(&g_HttpGlobalLock);

    HttpCompleteIrpList(&CompleteList);
    HttpRemoveListenerConnection(Conn);
    HttpConnectionResetSocket(Conn);
    HttpConnectionDereference(Conn);
    return STATUS_SUCCESS;
}

static NTSTATUS HttpWaitDisconnect(PHTTP_REQUEST_QUEUE Queue, PIRP Irp)
{
    PIO_STACK_LOCATION Stack = IoGetCurrentIrpStackLocation(Irp);
    NTSTATUS Status = STATUS_CONNECTION_INVALID;
    HTTP_CONNECTION_ID Id;
    PLIST_ENTRY Entry;

    if (Stack->Parameters.DeviceIoControl.InputBufferLength < sizeof(Id))
        return STATUS_INVALID_PARAMETER;
    Id = *(HTTP_CONNECTION_ID *)Irp->AssociatedIrp.SystemBuffer;

    ExAcquireFastMutex(&g_HttpGlobalLock);
    for (Entry = g_ConnectionsList.Flink; Entry != &g_ConnectionsList; Entry = Entry->Flink)
    {
        PHTTP_CONNECTION Conn = CONTAINING_RECORD(Entry, HTTP_CONNECTION, Entry);

        if (Conn->ConnectionId != Id || Conn->Queue != Queue)
            continue;

        ExAcquireFastMutex(&Queue->Mutex);
        ExAcquireFastMutex(&Conn->SocketMutex);
        if (Conn->Socket && !(Queue->State & HTTP_QUEUE_STATE_CLOSING))
        {
            Irp->Tail.Overlay.DriverContext[0] = Conn;
            Irp->Tail.Overlay.DriverContext[1] = Queue;
            IoCsqInsertIrp(&g_DisconnectCsq, Irp, NULL);
            Status = STATUS_PENDING;
        }
        ExReleaseFastMutex(&Conn->SocketMutex);
        ExReleaseFastMutex(&Queue->Mutex);
        break;
    }
    ExReleaseFastMutex(&g_HttpGlobalLock);
    return Status;
}

static BOOLEAN HttpCreateIsVersion2(PIRP Irp)
{
    PIO_STACK_LOCATION Stack = IoGetCurrentIrpStackLocation(Irp);
    PFILE_FULL_EA_INFORMATION Ea = Irp->AssociatedIrp.SystemBuffer;
    ULONG Length = Stack->Parameters.Create.EaLength;
    HTTPAPI_VERSION Version;

    if (!Ea || Length < FIELD_OFFSET(FILE_FULL_EA_INFORMATION, EaName) ||
        Ea->EaNameLength != sizeof(HTTP_VERSION_EA_NAME) - 1 ||
        Ea->EaValueLength != sizeof(Version) ||
        Length < FIELD_OFFSET(FILE_FULL_EA_INFORMATION, EaName) + Ea->EaNameLength + 1 + Ea->EaValueLength ||
        memcmp(Ea->EaName, HTTP_VERSION_EA_NAME, Ea->EaNameLength))
        return FALSE;

    memcpy(&Version, Ea->EaName + Ea->EaNameLength + 1, sizeof(Version));
    return Version.HttpApiMajorVersion >= 2;
}

static NTSTATUS NTAPI HttpDispatchCreate(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    PIO_STACK_LOCATION Stack = IoGetCurrentIrpStackLocation(Irp);
    PHTTP_REQUEST_QUEUE Queue;

    UNREFERENCED_PARAMETER(DeviceObject);

    Queue = ExAllocatePoolZero(NonPagedPoolNx, sizeof(*Queue), TAG_HTTP);
    if (!Queue)
    {
        Irp->IoStatus.Status = STATUS_NO_MEMORY;
        Irp->IoStatus.Information = 0;
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
        return STATUS_NO_MEMORY;
    }

    InitializeListHead(&Queue->UrlList);
    InitializeListHead(&Queue->PendingIrpQueue);
    InitializeListHead(&Queue->PendingBodyIrpQueue);
    ExInitializeFastMutex(&Queue->Mutex);
    {
        NTSTATUS Status = HttpInitializeRequestQueue(Queue);
        if (!NT_SUCCESS(Status))
        {
            ExFreePoolWithTag(Queue, TAG_HTTP);
            Irp->IoStatus.Status = Status;
            Irp->IoStatus.Information = 0;
            IoCompleteRequest(Irp, IO_NO_INCREMENT);
            return Status;
        }
    }
    Queue->MaxQueueLength = 1000;
    Queue->EnabledState = HttpEnabledStateActive;
    Queue->Verbosity = Http503ResponseVerbosityBasic;
    Queue->Version2 = HttpCreateIsVersion2(Irp);

    ExAcquireFastMutex(&g_HttpGlobalLock);
    InsertTailList(&g_RequestQueuesList, &Queue->Entry);
    ExReleaseFastMutex(&g_HttpGlobalLock);

    Stack->FileObject->FsContext = Queue;

    Irp->IoStatus.Status = STATUS_SUCCESS;
    Irp->IoStatus.Information = 0;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return STATUS_SUCCESS;
}

static NTSTATUS NTAPI HttpDispatchCleanup(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    PIO_STACK_LOCATION Stack = IoGetCurrentIrpStackLocation(Irp);
    PHTTP_REQUEST_QUEUE Queue = Stack->FileObject ? Stack->FileObject->FsContext : NULL;
    LIST_ENTRY CancelList;
    PLIST_ENTRY Entry;

    UNREFERENCED_PARAMETER(DeviceObject);
    InitializeListHead(&CancelList);

    if (Queue)
    {
        HTTP_DISCONNECT_PEEK Peek = { NULL, Queue };
        PIRP PendingIrp;
        ExAcquireFastMutex(&Queue->Mutex);
        Queue->State |= HTTP_QUEUE_STATE_CLOSING;
        while ((PendingIrp = IoCsqRemoveNextIrp(&Queue->Csq, NULL)) != NULL)
            InsertTailList(&CancelList, &PendingIrp->Tail.Overlay.ListEntry);
        while ((PendingIrp = IoCsqRemoveNextIrp(&Queue->BodyCsq, NULL)) != NULL)
            InsertTailList(&CancelList, &PendingIrp->Tail.Overlay.ListEntry);
        while ((PendingIrp = IoCsqRemoveNextIrp(&g_DisconnectCsq, &Peek)) != NULL)
            InsertTailList(&CancelList, &PendingIrp->Tail.Overlay.ListEntry);
        ExReleaseFastMutex(&Queue->Mutex);

        while (!IsListEmpty(&CancelList))
        {
            Entry = RemoveHeadList(&CancelList);
            PendingIrp = CONTAINING_RECORD(Entry, IRP, Tail.Overlay.ListEntry);
            PendingIrp->IoStatus.Status = STATUS_CANCELLED;
            PendingIrp->IoStatus.Information = 0;
            IoCompleteRequest(PendingIrp, IO_NO_INCREMENT);
        }
    }

    Irp->IoStatus.Status = STATUS_SUCCESS;
    Irp->IoStatus.Information = 0;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return STATUS_SUCCESS;
}

static NTSTATUS NTAPI HttpDispatchClose(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    PIO_STACK_LOCATION Stack = IoGetCurrentIrpStackLocation(Irp);
    PHTTP_REQUEST_QUEUE Queue = Stack->FileObject ? Stack->FileObject->FsContext : NULL;
    LIST_ENTRY ListenerCloseList, ConnectionCloseList;
    PLIST_ENTRY Entry;

    UNREFERENCED_PARAMETER(DeviceObject);
    InitializeListHead(&ListenerCloseList);
    InitializeListHead(&ConnectionCloseList);

    if (Queue)
    {
        ExAcquireFastMutex(&g_HttpGlobalLock);
        RemoveEntryList(&Queue->Entry);

        while (!IsListEmpty(&Queue->UrlList))
        {
            Entry = RemoveHeadList(&Queue->UrlList);
            PHTTP_URL_ENTRY UrlEntry = CONTAINING_RECORD(Entry, HTTP_URL_ENTRY, Entry);
            PHTTP_LISTENER Listener = UrlEntry->Listener;
            if (--Listener->UrlCount == 0)
            {
                RemoveEntryList(&Listener->Entry);
                InsertTailList(&ListenerCloseList, &Listener->Entry);
            }
            ExFreePoolWithTag(UrlEntry, TAG_HTTP);
        }

        for (Entry = g_ConnectionsList.Flink; Entry != &g_ConnectionsList; )
        {
            PHTTP_CONNECTION Connection = CONTAINING_RECORD(Entry, HTTP_CONNECTION, Entry);
            Entry = Entry->Flink;
            if (Connection->Queue == Queue)
            {
                RemoveEntryList(&Connection->Entry);
                Connection->InConnectionList = FALSE;
                InterlockedExchange(&Connection->Shutdown, TRUE);
                InsertTailList(&ConnectionCloseList, &Connection->Entry);
            }
        }

        ExReleaseFastMutex(&g_HttpGlobalLock);

        while (!IsListEmpty(&ListenerCloseList))
        {
            Entry = RemoveHeadList(&ListenerCloseList);
            PHTTP_LISTENER Listener = CONTAINING_RECORD(Entry, HTTP_LISTENER, Entry);
            HttpCloseListener(Listener);
        }

        while (!IsListEmpty(&ConnectionCloseList))
        {
            Entry = RemoveHeadList(&ConnectionCloseList);
            PHTTP_CONNECTION Connection = CONTAINING_RECORD(Entry, HTTP_CONNECTION, Entry);
            if (Connection->Available)
                HttpConnectionResetSocket(Connection);
            else
                HttpConnectionCloseSocket(Connection);
            HttpConnectionDereference(Connection);
        }

        ExFreePoolWithTag(Queue, TAG_HTTP);
        Stack->FileObject->FsContext = NULL;
    }

    Irp->IoStatus.Status = STATUS_SUCCESS;
    Irp->IoStatus.Information = 0;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return STATUS_SUCCESS;
}

static NTSTATUS NTAPI HttpDispatchDeviceControl(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    PIO_STACK_LOCATION Stack = IoGetCurrentIrpStackLocation(Irp);
    PHTTP_REQUEST_QUEUE Queue = Stack->FileObject ? Stack->FileObject->FsContext : NULL;
    NTSTATUS Status;

    UNREFERENCED_PARAMETER(DeviceObject);

    if (!Queue)
    {
        Irp->IoStatus.Status = STATUS_INVALID_HANDLE;
        Irp->IoStatus.Information = 0;
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
        return STATUS_INVALID_HANDLE;
    }

    switch (Stack->Parameters.DeviceIoControl.IoControlCode)
    {
        case IOCTL_HTTP_ADD_URL:
            Status = HttpIoctlAddUrl(Queue, Irp);
            break;
        case IOCTL_HTTP_REMOVE_URL:
            Status = HttpIoctlRemoveUrl(Queue, Irp);
            break;
        case IOCTL_HTTP_RECEIVE_REQUEST:
            Status = HttpReceiveRequest(Queue, Irp);
            break;
        case IOCTL_HTTP_SEND_RESPONSE:
            Status = HttpSendResponse(Queue, Irp);
            break;
        case IOCTL_HTTP_RECEIVE_BODY:
            Status = HttpReceiveBody(Queue, Irp);
            break;
        case IOCTL_HTTP_SET_QUEUE_PROPERTY:
            Status = HttpSetQueueProperty(Queue, Irp);
            break;
        case IOCTL_HTTP_QUERY_QUEUE_PROPERTY:
            Status = HttpQueryQueueProperty(Queue, Irp);
            break;
        case IOCTL_HTTP_SET_URL_CONFIG:
            Status = HttpIoctlSetUrlConfig(Queue, Irp);
            break;
        case IOCTL_HTTP_SHUTDOWN_QUEUE:
            Status = HttpShutdownQueue(Queue);
            break;
        case IOCTL_HTTP_CANCEL_REQUEST:
            Status = HttpCancelRequest(Queue, Irp);
            break;
        case IOCTL_HTTP_WAIT_DISCONNECT:
            Status = HttpWaitDisconnect(Queue, Irp);
            break;
        default:
            Status = STATUS_INVALID_DEVICE_REQUEST;
            break;
    }

    if (Status != STATUS_PENDING)
    {
        Irp->IoStatus.Status = Status;
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
    }

    return Status;
}

VOID NTAPI HttpDriverUnload(_In_ PDRIVER_OBJECT DriverObject)
{
    LIST_ENTRY ListenerCloseList, ConnectionCloseList;
    PLIST_ENTRY Entry;

    UNREFERENCED_PARAMETER(DriverObject);
    InitializeListHead(&ListenerCloseList);
    InitializeListHead(&ConnectionCloseList);

    KeCancelTimer(&g_TimeoutTimer);
    KeFlushQueuedDpcs();

    ExAcquireFastMutex(&g_HttpGlobalLock);
    while (!IsListEmpty(&g_ListenersList))
    {
        Entry = RemoveHeadList(&g_ListenersList);
        InsertTailList(&ListenerCloseList, Entry);
    }
    while (!IsListEmpty(&g_ConnectionsList))
    {
        Entry = RemoveHeadList(&g_ConnectionsList);
        PHTTP_CONNECTION Connection =
            CONTAINING_RECORD(Entry, HTTP_CONNECTION, Entry);
        Connection->InConnectionList = FALSE;
        InterlockedExchange(&Connection->Shutdown, TRUE);
        InsertTailList(&ConnectionCloseList, Entry);
    }
    ExReleaseFastMutex(&g_HttpGlobalLock);

    while (!IsListEmpty(&ListenerCloseList))
    {
        PHTTP_LISTENER Listener =
            CONTAINING_RECORD(RemoveHeadList(&ListenerCloseList), HTTP_LISTENER, Entry);
        HttpCloseListener(Listener);
    }

    while (!IsListEmpty(&ConnectionCloseList))
    {
        PHTTP_CONNECTION Connection =
            CONTAINING_RECORD(RemoveHeadList(&ConnectionCloseList),
                              HTTP_CONNECTION,
                              Entry);
        HttpConnectionCloseSocket(Connection);
        HttpConnectionDereference(Connection);
    }

    while (InterlockedCompareExchange(&g_ConnectionCount, 0, 0) ||
           InterlockedCompareExchange(&g_TimeoutWorkQueued, 0, 0))
    {
        LARGE_INTEGER Interval;

        Interval.QuadPart = -10 * 1000 * 10;
        KeDelayExecutionThread(KernelMode, FALSE, &Interval);
    }

    IoFreeWorkItem(g_TimeoutWorkItem);
    g_TimeoutWorkItem = NULL;
    if (g_ParametersKey)
    {
        ZwClose(g_ParametersKey);
        g_ParametersKey = NULL;
    }
    IoDeleteDevice(g_ReqQueueDeviceObject);
    g_ReqQueueDeviceObject = NULL;

    HttpReleaseWsk();

    if (g_HttpDirectoryHandle)
    {
        ZwClose(g_HttpDirectoryHandle);
        g_HttpDirectoryHandle = NULL;
    }
}

NTSTATUS NTAPI DriverEntry(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PUNICODE_STRING RegistryPath)
{
    OBJECT_ATTRIBUTES Attr;
    UNICODE_STRING DirName = RTL_CONSTANT_STRING(L"\\Device\\Http");
    UNICODE_STRING DeviceName = RTL_CONSTANT_STRING(L"\\Device\\Http\\ReqQueue");
    NTSTATUS Status;


    ExInitializeFastMutex(&g_HttpGlobalLock);
    InitializeListHead(&g_RequestQueuesList);
    InitializeListHead(&g_ConnectionsList);
    InitializeListHead(&g_ListenersList);
    InitializeListHead(&g_ListenerCallbackList);
    KeInitializeSpinLock(&g_ListenerCallbackLock);
    InitializeListHead(&g_DisconnectIrpQueue);
    KeInitializeSpinLock(&g_DisconnectIrpLock);
    InitializeListHead(&g_GroupConnectionsList);
    KeInitializeSpinLock(&g_GroupConnectionsLock);
    IoCsqInitialize(&g_DisconnectCsq,
                    HttpDisconnectCsqInsertIrp,
                    HttpCsqRemoveIrp,
                    HttpDisconnectCsqPeekNextIrp,
                    HttpDisconnectCsqAcquireLock,
                    HttpDisconnectCsqReleaseLock,
                    HttpCsqCompleteCanceledIrp);
    g_RequestIdCounter = 1;
    g_WskClientNpi.ClientContext = NULL;
    g_WskClientNpi.Dispatch = &g_WskClientDispatch;

    Status = WskRegister(&g_WskClientNpi, &g_WskRegistration);
    if (!NT_SUCCESS(Status))
        return Status;
    g_WskRegistered = TRUE;

    Status = WskCaptureProviderNPI(&g_WskRegistration,
                                   WSK_INFINITE_WAIT,
                                   &g_WskProviderNpi);
    if (!NT_SUCCESS(Status))
    {
        HttpReleaseWsk();
        return Status;
    }
    g_WskCaptured = TRUE;

    llhttp_settings_init(&g_LlhttpSettings);
    g_LlhttpSettings.on_method = HttpLlhttpOnMethod;
    g_LlhttpSettings.on_url = HttpLlhttpOnUrl;
    g_LlhttpSettings.on_header_field = HttpLlhttpOnHeaderField;
    g_LlhttpSettings.on_header_value = HttpLlhttpOnHeaderValue;
    g_LlhttpSettings.on_header_field_complete = HttpLlhttpOnHeaderFieldComplete;
    g_LlhttpSettings.on_header_value_complete = HttpLlhttpOnHeaderValueComplete;
    g_LlhttpSettings.on_headers_complete = HttpLlhttpOnHeadersComplete;
    g_LlhttpSettings.on_body = HttpLlhttpOnBody;
    g_LlhttpSettings.on_message_complete = HttpLlhttpOnMessageComplete;

    InitializeObjectAttributes(&Attr, &DirName, OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE | OBJ_PERMANENT, NULL, NULL);
    Status = ZwCreateDirectoryObject(&g_HttpDirectoryHandle, DIRECTORY_ALL_ACCESS, &Attr);
    if (!NT_SUCCESS(Status) && Status != STATUS_OBJECT_NAME_COLLISION)
    {
        HttpReleaseWsk();
        return Status;
    }

    Status = IoCreateDevice(
        DriverObject,
        0,
        &DeviceName,
        FILE_DEVICE_NETWORK,
        FILE_DEVICE_SECURE_OPEN,
        FALSE,
        &g_ReqQueueDeviceObject);

    if (!NT_SUCCESS(Status))
    {
        if (g_HttpDirectoryHandle)
        {
            ZwClose(g_HttpDirectoryHandle);
            g_HttpDirectoryHandle = NULL;
        }
        HttpReleaseWsk();
        return Status;
    }

    g_TimeoutWorkItem = IoAllocateWorkItem(g_ReqQueueDeviceObject);
    if (!g_TimeoutWorkItem)
    {
        IoDeleteDevice(g_ReqQueueDeviceObject);
        g_ReqQueueDeviceObject = NULL;
        if (g_HttpDirectoryHandle)
        {
            ZwClose(g_HttpDirectoryHandle);
            g_HttpDirectoryHandle = NULL;
        }
        HttpReleaseWsk();
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    {
        UNICODE_STRING ParametersName = RTL_CONSTANT_STRING(L"Parameters");
        OBJECT_ATTRIBUTES KeyAttributes;
        HANDLE ServiceKey;

        InitializeObjectAttributes(&KeyAttributes, RegistryPath,
                                   OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, NULL, NULL);
        if (NT_SUCCESS(ZwOpenKey(&ServiceKey, KEY_READ | KEY_CREATE_SUB_KEY, &KeyAttributes)))
        {
            InitializeObjectAttributes(&KeyAttributes, &ParametersName,
                                       OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, ServiceKey, NULL);
            if (!NT_SUCCESS(ZwCreateKey(&g_ParametersKey, KEY_READ, &KeyAttributes, 0, NULL,
                                        REG_OPTION_NON_VOLATILE, NULL)))
                g_ParametersKey = NULL;
            ZwClose(ServiceKey);
        }
        HttpLoadTimeouts();
        HttpLoadMaxConnections();
    }

    KeInitializeTimerEx(&g_TimeoutTimer, NotificationTimer);
    KeInitializeDpc(&g_TimeoutDpc, HttpTimeoutDpc, NULL);
    {
        LARGE_INTEGER DueTime;

        DueTime.QuadPart = -10 * 1000 * 1000;
        KeSetTimerEx(&g_TimeoutTimer, DueTime, 1000, &g_TimeoutDpc);
    }

    DriverObject->MajorFunction[IRP_MJ_CREATE] = HttpDispatchCreate;
    DriverObject->MajorFunction[IRP_MJ_CLOSE] = HttpDispatchClose;
    DriverObject->MajorFunction[IRP_MJ_CLEANUP] = HttpDispatchCleanup;
    DriverObject->MajorFunction[IRP_MJ_DEVICE_CONTROL] = HttpDispatchDeviceControl;
    DriverObject->DriverUnload = HttpDriverUnload;

    return STATUS_SUCCESS;
}
