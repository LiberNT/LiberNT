/*
 * PROJECT:     LiberNT HTTP.sys driver
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Internal declarations for HTTP.sys
 * COPYRIGHT:   Copyright 2026 Ostap Brehin <osbre@protonmail.com>
 */

#ifndef _HTTP_SYS_H_
#define _HTTP_SYS_H_

#include <ntifs.h>
#include <ntstrsafe.h>
#include <windef.h>
#include <ws2def.h>
#include <ws2ipdef.h>
#include <wsk.h>
#include <http.h>
#include <wine/http.h>
#include "llhttp.h"

#define TAG_HTTP 'PTHH'
#define HTTP_QUEUE_STATE_CLOSING 0x00000001
#define HTTP_QUEUE_STATE_SHUTDOWN 0x00000002
#define HTTP_MAX_HEADER_SIZE (64 * 1024)
#define HTTP_MAX_CONNECTIONS 64
#define HTTP_MAX_HEADERS 128
#define HTTP_DEFAULT_TIMEOUT_SECONDS 120
#define HTTP_ENTITY_BODY_SECONDS 120

typedef struct _HTTP_LISTENER
{
    LIST_ENTRY Entry;
    LIST_ENTRY CallbackEntry;
    LIST_ENTRY PendingConnections;
    KSPIN_LOCK PendingConnectionLock;
    PWSK_SOCKET Socket;
    PIRP CloseIrp;
    USHORT Port;
    ULONG CallbackId;
    ULONG UrlCount;
    LONG RefCount;
    BOOLEAN Closing;
    BOOLEAN InCallbackList;
} HTTP_LISTENER, *PHTTP_LISTENER;

typedef enum _HTTP_URL_CATEGORY
{
    HttpUrlCategoryStrongWildcard = 0,
    HttpUrlCategoryExplicit = 1,
    HttpUrlCategoryIPBound = 2,
    HttpUrlCategoryWeakWildcard = 3,
    HttpUrlCategoryMaximum
} HTTP_URL_CATEGORY;

typedef struct _HTTP_URL_ENTRY
{
    LIST_ENTRY Entry;
    char *Url;
    size_t UrlLength;
    size_t PathLength;
    struct _HTTP_LISTENER *Listener;
    IN_ADDR BoundAddress;
    HTTP_URL_CONTEXT Context;
    HTTP_URL_GROUP_ID Group;
    struct http_url_config Config;
    HTTP_URL_CATEGORY Category;
    USHORT Port;
    char *Host;
    char *Path;
} HTTP_URL_ENTRY, *PHTTP_URL_ENTRY;

typedef struct _HTTP_REQUEST_QUEUE
{
    LIST_ENTRY Entry;
    LIST_ENTRY UrlList;
    LIST_ENTRY PendingIrpQueue;
    IO_CSQ Csq;
    KSPIN_LOCK PendingIrpLock;
    LIST_ENTRY PendingBodyIrpQueue;
    IO_CSQ BodyCsq;
    KSPIN_LOCK PendingBodyIrpLock;
    FAST_MUTEX Mutex;
    ULONG State;
    ULONG MaxQueueLength;
    ULONG EnabledState;
    ULONG Verbosity;
    BOOLEAN Version2;
} HTTP_REQUEST_QUEUE, *PHTTP_REQUEST_QUEUE;

typedef enum _HTTP_UNAVAILABLE_REASON
{
    HttpUnavailableNone,
    HttpUnavailableQueueFull,
    HttpUnavailableOffline,
    HttpUnavailableInactive,
} HTTP_UNAVAILABLE_REASON;

typedef struct _HTTP_PARSED_HEADER
{
    ULONG NameOffset;
    ULONG NameLength;
    ULONG ValueOffset;
    ULONG ValueLength;
    HTTP_HEADER_ID Id;
} HTTP_PARSED_HEADER, *PHTTP_PARSED_HEADER;

typedef struct _HTTP_CONNECTION
{
    LIST_ENTRY Entry;
    LIST_ENTRY ListenerEntry;
    LIST_ENTRY TimeoutEntry;
    HTTP_CONNECTION_ID ConnectionId;
    HTTP_REQUEST_ID RequestId;
    HTTP_URL_CONTEXT Context;
    PHTTP_REQUEST_QUEUE Queue;
    struct sockaddr_in LocalAddress;
    struct sockaddr_in RemoteAddress;
    PWSK_SOCKET Socket;
    PHTTP_LISTENER Listener;
    PIO_WORKITEM WorkItem;
    PIRP ReceiveIrp;
    PIRP CloseIrp;
    PMDL ReceiveMdl;
    WSK_BUF ReceiveBuffer;
    NTSTATUS ReceiveStatus;
    ULONG ReceiveOffset;
    ULONG ReceiveLength;
    LONG DeadlineSeconds;
    USHORT IdleSeconds;
    USHORT HeaderWaitSeconds;
    USHORT EntityBodySeconds;
    FAST_MUTEX ResponseMutex;
    FAST_MUTEX SocketMutex;
    char *Buffer;
    ULONG BufferLength;
    ULONG BufferCapacity;
    LONG RefCount;
    BOOLEAN Available;
    LONG Shutdown;
    BOOLEAN ResponseStarted;
    BOOLEAN InConnectionList;
    BOOLEAN InListenerList;
    BOOLEAN ReceivePending;
    BOOLEAN ReadingBody;
    BOOLEAN TimedOut;
    BOOLEAN KeepAlive;
    BOOLEAN CloseAfterResponse;
    BOOLEAN Finished;
    BOOLEAN Cancelled;
    BOOLEAN Lingering;

    llhttp_t Parser;
    ULONG MethodOffset;
    ULONG MethodLength;
    ULONG UrlOffset;
    ULONG HeaderCount;
    ULONG HostOffset;
    ULONG HeaderLength;
    BOOLEAN HostSeen;
    BOOLEAN HeadersComplete;
    BOOLEAN BodyComplete;
    BOOLEAN BodyFailed;
    USHORT ErrorStatus;
    UCHAR UnavailableReason;
    UCHAR UnavailableVerbosity;
    HTTP_PARSED_HEADER Headers[HTTP_MAX_HEADERS + 1];
    HTTP_VERB Verb;
    HTTP_VERSION Version;
    const char *Url;
    ULONG UrlLength;
    const char *Host;
    ULONG HostLength;
    ULONG UnknownVerbLength;
    ULONG BodyOffset;
    ULONG BodyLength;
    ULONGLONG BodyReceived;
} HTTP_CONNECTION, *PHTTP_CONNECTION;

#endif /* _HTTP_SYS_H_ */
