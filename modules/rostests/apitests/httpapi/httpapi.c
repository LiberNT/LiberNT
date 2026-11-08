/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     API tests for HTTP Server API and HTTP.sys
 * COPYRIGHT:   Copyright 2026 Ostap Brehin <osbre@protonmail.com>
 */

#include <stdarg.h>
#include <stdio.h>
#include <wchar.h>

#include <windef.h>
#include <winbase.h>
#include <winnt.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <http.h>

#include <wine/test.h>

static void test_initialize(void)
{
    HTTPAPI_VERSION invalid_ver = { 0, 0 };
    HTTPAPI_VERSION v1 = HTTPAPI_VERSION_1;
    HTTPAPI_VERSION v2 = HTTPAPI_VERSION_2;
    ULONG ret;

    ret = HttpInitialize(invalid_ver, HTTP_INITIALIZE_SERVER, NULL);
    ok(ret == ERROR_INVALID_DLL, "Expected ERROR_INVALID_DLL, got %lu\n", ret);

    ret = HttpInitialize(v1, 0, NULL);
    ok(ret == NO_ERROR, "HttpInitialize(v1, 0) failed with %lu\n", ret);

    ret = HttpTerminate(0, NULL);
    ok(ret == NO_ERROR, "HttpTerminate(0) failed with %lu\n", ret);

    ret = HttpInitialize(v1, HTTP_INITIALIZE_SERVER, (void *)1);
    ok(ret == ERROR_INVALID_PARAMETER, "Expected ERROR_INVALID_PARAMETER for reserved argument, got %lu\n", ret);

    ret = HttpInitialize(v1, HTTP_INITIALIZE_SERVER, NULL);
    ok(ret == NO_ERROR, "HttpInitialize(v1) failed with %lu\n", ret);

    ret = HttpTerminate(HTTP_INITIALIZE_SERVER, NULL);
    ok(ret == NO_ERROR, "HttpTerminate failed with %lu\n", ret);

    ret = HttpInitialize(v2, HTTP_INITIALIZE_SERVER, NULL);
    ok(ret == NO_ERROR, "HttpInitialize(v2) failed with %lu\n", ret);

    ret = HttpTerminate(HTTP_INITIALIZE_SERVER, NULL);
    ok(ret == NO_ERROR, "HttpTerminate failed with %lu\n", ret);
}

static void test_request_queue_v1(void)
{
    HANDLE queue1 = NULL, queue2 = NULL;
    HTTPAPI_VERSION v1 = HTTPAPI_VERSION_1;
    ULONG ret;

    ret = HttpInitialize(v1, HTTP_INITIALIZE_SERVER, NULL);
    ok(ret == NO_ERROR, "HttpInitialize failed with %lu\n", ret);

    ret = HttpCreateHttpHandle(NULL, 0);
    ok(ret == ERROR_INVALID_PARAMETER, "Expected ERROR_INVALID_PARAMETER, got %lu\n", ret);

    ret = HttpCreateHttpHandle(&queue1, 1);
    ok(ret == ERROR_INVALID_PARAMETER, "Expected ERROR_INVALID_PARAMETER for reserved argument, got %lu\n", ret);

    ret = HttpCreateHttpHandle(&queue1, 0);
    ok(ret == NO_ERROR, "HttpCreateHttpHandle failed with %lu\n", ret);
    ok(queue1 != NULL && queue1 != INVALID_HANDLE_VALUE, "Got invalid handle %p\n", queue1);

    ret = HttpCreateHttpHandle(&queue2, 0);
    ok(ret == NO_ERROR, "HttpCreateHttpHandle failed with %lu\n", ret);
    ok(queue2 != NULL && queue2 != queue1, "Expected distinct handles, got %p and %p\n", queue1, queue2);

    CloseHandle(queue2);
    CloseHandle(queue1);

    ret = HttpTerminate(HTTP_INITIALIZE_SERVER, NULL);
    ok(ret == NO_ERROR, "HttpTerminate failed with %lu\n", ret);
}

static void test_request_queue_v2(void)
{
    HTTPAPI_VERSION v2 = HTTPAPI_VERSION_2;
    HTTPAPI_VERSION v1 = HTTPAPI_VERSION_1;
    HANDLE queue = NULL;
    ULONG ret;

    ret = HttpInitialize(v2, HTTP_INITIALIZE_SERVER, NULL);
    ok(ret == NO_ERROR, "HttpInitialize failed with %lu\n", ret);

    ret = HttpCreateRequestQueue(v2, NULL, NULL, 0, NULL);
    ok(ret == ERROR_INVALID_PARAMETER, "Expected ERROR_INVALID_PARAMETER, got %lu\n", ret);

    ret = HttpCreateRequestQueue(v1, NULL, NULL, 0, &queue);
    ok(ret == NO_ERROR, "HttpCreateRequestQueue(v1) failed with %lu\n", ret);
    if (ret == NO_ERROR)
        HttpCloseRequestQueue(queue);

    ret = HttpCreateRequestQueue(v2, NULL, NULL, 0, &queue);
    ok(ret == NO_ERROR, "HttpCreateRequestQueue failed with %lu\n", ret);
    ok(queue != NULL && queue != INVALID_HANDLE_VALUE, "Got invalid handle %p\n", queue);

    ret = HttpCloseRequestQueue(queue);
    ok(ret == NO_ERROR, "HttpCloseRequestQueue failed with %lu\n", ret);

    ret = HttpTerminate(HTTP_INITIALIZE_SERVER, NULL);
    ok(ret == NO_ERROR, "HttpTerminate failed with %lu\n", ret);
}

static void test_server_session_and_url_group(void)
{
    HTTPAPI_VERSION v1 = HTTPAPI_VERSION_1;
    HTTPAPI_VERSION v2 = HTTPAPI_VERSION_2;
    HTTP_SERVER_SESSION_ID session_id = HTTP_NULL_ID;
    HTTP_URL_GROUP_ID group_id1 = HTTP_NULL_ID;
    HTTP_URL_GROUP_ID group_id2 = HTTP_NULL_ID;
    HTTP_BINDING_INFO binding;
    HANDLE queue = NULL;
    ULONG ret;

    ret = HttpInitialize(v2, HTTP_INITIALIZE_SERVER, NULL);
    ok(ret == NO_ERROR, "HttpInitialize failed with %lu\n", ret);

    ret = HttpCreateServerSession(v2, NULL, 0);
    ok(ret == ERROR_INVALID_PARAMETER, "Expected ERROR_INVALID_PARAMETER, got %lu\n", ret);

    ret = HttpCreateServerSession(v1, &session_id, 0);
    ok(ret == NO_ERROR, "HttpCreateServerSession(v1) failed with %lu\n", ret);
    if (ret == NO_ERROR)
    {
        ret = HttpCloseServerSession(session_id);
        ok(ret == NO_ERROR, "HttpCloseServerSession(v1) failed with %lu\n", ret);
    }

    ret = HttpCreateServerSession(v2, &session_id, 1);
    ok(ret == ERROR_INVALID_PARAMETER, "Expected ERROR_INVALID_PARAMETER for reserved argument, got %lu\n", ret);

    ret = HttpCreateServerSession(v2, &session_id, 0);
    ok(ret == NO_ERROR, "HttpCreateServerSession failed with %lu\n", ret);
    ok(session_id != HTTP_NULL_ID, "Got NULL session ID\n");

    ret = HttpCreateUrlGroup(session_id, NULL, 0);
    ok(ret == ERROR_INVALID_PARAMETER, "Expected ERROR_INVALID_PARAMETER, got %lu\n", ret);

    ret = HttpCreateUrlGroup(session_id, &group_id1, 1);
    ok(ret == ERROR_INVALID_PARAMETER, "Expected ERROR_INVALID_PARAMETER for reserved argument, got %lu\n", ret);

    ret = HttpCreateUrlGroup(0xdeadbeef, &group_id1, 0);
    ok(ret == ERROR_INVALID_PARAMETER, "Expected ERROR_INVALID_PARAMETER for fake session, got %lu\n", ret);

    ret = HttpCreateUrlGroup(session_id, &group_id1, 0);
    ok(ret == NO_ERROR, "HttpCreateUrlGroup failed with %lu\n", ret);
    ok(group_id1 != HTTP_NULL_ID, "Got NULL group ID\n");

    ret = HttpCreateUrlGroup(session_id, &group_id2, 0);
    ok(ret == NO_ERROR, "HttpCreateUrlGroup failed with %lu\n", ret);
    ok(group_id2 != HTTP_NULL_ID && group_id2 != group_id1, "Expected distinct group IDs\n");

    ret = HttpCreateRequestQueue(v2, NULL, NULL, 0, &queue);
    ok(ret == NO_ERROR, "HttpCreateRequestQueue failed with %lu\n", ret);

    memset(&binding, 0, sizeof(binding));
    binding.Flags.Present = 1;
    binding.RequestQueueHandle = queue;
    ret = HttpSetUrlGroupProperty(group_id1, HttpServerBindingProperty, &binding, sizeof(binding));
    ok(ret == NO_ERROR, "HttpSetUrlGroupProperty(HttpServerBindingProperty) failed with %lu\n", ret);

    ret = HttpSetUrlGroupProperty(0xdeadbeef, HttpServerBindingProperty, &binding, sizeof(binding));
    ok(ret == ERROR_INVALID_PARAMETER, "Expected ERROR_INVALID_PARAMETER for fake group, got %lu\n", ret);

    ret = HttpSetUrlGroupProperty(group_id1, HttpServerBindingProperty, NULL, sizeof(binding));
    ok(ret == ERROR_INVALID_PARAMETER, "Expected ERROR_INVALID_PARAMETER for null binding, got %lu\n", ret);

    ret = HttpAddUrlToUrlGroup(0xdeadbeef, L"http://localhost:59990/", 0, 0);
    ok(ret == ERROR_INVALID_PARAMETER, "Expected ERROR_INVALID_PARAMETER for fake group, got %lu\n", ret);

    ret = HttpAddUrlToUrlGroup(group_id2, L"http://localhost:59990/", 1, 0);
    ok(ret == NO_ERROR, "First HttpAddUrlToUrlGroup failed with %lu\n", ret);

    ret = HttpAddUrlToUrlGroup(group_id2, L"http://localhost:59991/", 2, 0);
    ok(ret == NO_ERROR, "Second HttpAddUrlToUrlGroup failed with %lu\n", ret);

    ret = HttpRemoveUrlFromUrlGroup(group_id2, L"http://localhost:59990/", 0);
    ok(ret == NO_ERROR, "HttpRemoveUrlFromUrlGroup failed with %lu\n", ret);

    ret = HttpRemoveUrlFromUrlGroup(group_id2, NULL, HTTP_URL_FLAG_REMOVE_ALL);
    ok(ret == NO_ERROR, "HttpRemoveUrlFromUrlGroup(REMOVE_ALL) failed with %lu\n", ret);

    ret = HttpRemoveUrlFromUrlGroup(group_id2, L"http://localhost:59991/", 0);
    ok(ret == ERROR_FILE_NOT_FOUND, "Expected ERROR_FILE_NOT_FOUND after REMOVE_ALL, got %lu\n", ret);

    ret = HttpCloseUrlGroup(group_id1);
    ok(ret == NO_ERROR, "HttpCloseUrlGroup failed with %lu\n", ret);

    ret = HttpCloseUrlGroup(0xdeadbeef);
    ok(ret == ERROR_INVALID_PARAMETER, "Expected ERROR_INVALID_PARAMETER for fake group, got %lu\n", ret);

    ret = HttpCloseServerSession(session_id);
    ok(ret == NO_ERROR, "HttpCloseServerSession failed with %lu\n", ret);

    ret = HttpCloseUrlGroup(group_id2);
    ok(ret == ERROR_INVALID_PARAMETER, "Expected session close to destroy its URL groups, got %lu\n", ret);

    HttpCloseRequestQueue(queue);

    ret = HttpTerminate(HTTP_INITIALIZE_SERVER, NULL);
    ok(ret == NO_ERROR, "HttpTerminate failed with %lu\n", ret);
}

static void test_url_syntax_and_registration(void)
{
    HTTPAPI_VERSION v1 = HTTPAPI_VERSION_1;
    HANDLE queue = NULL;
    ULONG ret;

    ret = HttpInitialize(v1, HTTP_INITIALIZE_SERVER, NULL);
    ok(ret == NO_ERROR, "HttpInitialize failed with %lu\n", ret);

    ret = HttpCreateHttpHandle(&queue, 0);
    ok(ret == NO_ERROR, "HttpCreateHttpHandle failed with %lu\n", ret);

    ret = HttpAddUrl(NULL, L"http://localhost:59990/", NULL);
    ok(ret == ERROR_INVALID_HANDLE || ret == ERROR_INVALID_PARAMETER, "Got %lu\n", ret);

    ret = HttpAddUrl(queue, L"ftp://localhost:59990/", NULL);
    ok(ret == ERROR_INVALID_PARAMETER, "Expected ERROR_INVALID_PARAMETER for ftp://, got %lu\n", ret);

    ret = HttpAddUrl(queue, L"http://", NULL);
    ok(ret == ERROR_INVALID_PARAMETER, "Expected ERROR_INVALID_PARAMETER for empty host, got %lu\n", ret);

    ret = HttpAddUrl(queue, L"localhost:59990/", NULL);
    ok(ret == ERROR_INVALID_PARAMETER, "Expected ERROR_INVALID_PARAMETER for missing scheme, got %lu\n", ret);

    ret = HttpAddUrl(queue, L"http://localhost/", NULL);
    ok(ret == ERROR_INVALID_PARAMETER, "Expected ERROR_INVALID_PARAMETER for missing port, got %lu\n", ret);

    ret = HttpAddUrl(queue, L"http://localhost:0/", NULL);
    ok(ret == ERROR_INVALID_PARAMETER, "Expected ERROR_INVALID_PARAMETER for port 0, got %lu\n", ret);

    ret = HttpAddUrl(queue, L"http://localhost:059990/", NULL);
    ok(ret == ERROR_INVALID_PARAMETER, "Expected ERROR_INVALID_PARAMETER for leading zero in port, got %lu\n", ret);

    ret = HttpAddUrl(queue, L"http://localhost:70000/", NULL);
    ok(ret == ERROR_INVALID_PARAMETER, "Expected ERROR_INVALID_PARAMETER for out-of-range port, got %lu\n", ret);

    ret = HttpAddUrl(queue, L"http://256.1.1.1:59990/", NULL);
    ok(ret == ERROR_INVALID_PARAMETER, "Expected ERROR_INVALID_PARAMETER for invalid IPv4 literal, got %lu\n", ret);

    ret = HttpAddUrl(queue, L"http://bad..host:59990/", NULL);
    ok(ret == ERROR_INVALID_PARAMETER, "Expected ERROR_INVALID_PARAMETER for malformed hostname, got %lu\n", ret);

    ret = HttpAddUrl(queue, L"http://localhost:59990", NULL);
    ok(ret == ERROR_INVALID_PARAMETER, "Expected ERROR_INVALID_PARAMETER for missing trailing slash, got %lu\n", ret);

    ret = HttpAddUrl(queue, L"http://localhost:59990/?query=1", NULL);
    ok(ret == ERROR_INVALID_PARAMETER, "Expected ERROR_INVALID_PARAMETER for query string in prefix, got %lu\n", ret);

    ret = HttpAddUrl(queue, L"http://localhost:59990/", NULL);
    if (ret == NO_ERROR)
    {
        ret = HttpAddUrl(queue, L"http://localhost:59990/", NULL);
        ok(ret == ERROR_ALREADY_EXISTS || ret == ERROR_SHARING_VIOLATION,
           "Expected collision error for duplicate prefix, got %lu\n", ret);

        ret = HttpRemoveUrl(queue, L"http://localhost:59990/");
        ok(ret == NO_ERROR, "HttpRemoveUrl failed with %lu\n", ret);

        ret = HttpRemoveUrl(queue, L"http://localhost:59990/");
        ok(ret == ERROR_FILE_NOT_FOUND, "Expected ERROR_FILE_NOT_FOUND on second remove, got %lu\n", ret);
    }
    else
    {
        ok(ret == ERROR_ACCESS_DENIED, "HttpAddUrl failed with unexpected error %lu\n", ret);
    }

    CloseHandle(queue);

    ret = HttpTerminate(HTTP_INITIALIZE_SERVER, NULL);
    ok(ret == NO_ERROR, "HttpTerminate failed with %lu\n", ret);
}

static void test_receive_request_cancellation(void)
{
    char DECLSPEC_ALIGN(8) req_buffer[2048];
    HTTP_REQUEST_V1 *req = (HTTP_REQUEST_V1 *)req_buffer;
    HTTPAPI_VERSION v1 = HTTPAPI_VERSION_1;
    OVERLAPPED ovl;
    HANDLE queue = NULL;
    DWORD bytes_ret = 0;
    BOOL bRet;
    ULONG ret;

    ret = HttpInitialize(v1, HTTP_INITIALIZE_SERVER, NULL);
    ok(ret == NO_ERROR, "HttpInitialize failed with %lu\n", ret);

    ret = HttpCreateHttpHandle(&queue, 0);
    ok(ret == NO_ERROR, "HttpCreateHttpHandle failed with %lu\n", ret);

    memset(req_buffer, 0, sizeof(req_buffer));
    memset(&ovl, 0, sizeof(ovl));
    ovl.hEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
    ok(ovl.hEvent != NULL, "CreateEventW failed with %lu\n", GetLastError());

    ret = HttpReceiveHttpRequest(NULL, HTTP_NULL_ID, 0, (HTTP_REQUEST *)req, sizeof(req_buffer), NULL, &ovl);
    ok(ret == ERROR_INVALID_HANDLE, "Expected ERROR_INVALID_HANDLE, got %lu\n", ret);

    ret = HttpReceiveHttpRequest(queue, 0xdeadbeef, 0, (HTTP_REQUEST *)req, sizeof(req_buffer), NULL, &ovl);
    ok(ret == ERROR_CONNECTION_INVALID, "Expected ERROR_CONNECTION_INVALID, got %lu\n", ret);

    ret = HttpReceiveHttpRequest(queue, HTTP_NULL_ID, 0, (HTTP_REQUEST *)req, sizeof(req_buffer), NULL, &ovl);
    ok(ret == ERROR_IO_PENDING, "Expected ERROR_IO_PENDING, got %lu\n", ret);

    bRet = CancelIoEx(queue, &ovl);
    ok(bRet || GetLastError() == ERROR_NOT_FOUND, "CancelIoEx failed with %lu\n", GetLastError());

    WaitForSingleObject(ovl.hEvent, 1000);

    bRet = GetOverlappedResult(queue, &ovl, &bytes_ret, FALSE);
    ok(!bRet, "Expected GetOverlappedResult to fail after cancellation\n");
    ok(GetLastError() == ERROR_OPERATION_ABORTED || GetLastError() == ERROR_IO_INCOMPLETE,
       "Got unexpected error %lu\n", GetLastError());

    CloseHandle(ovl.hEvent);
    CloseHandle(queue);

    ret = HttpTerminate(HTTP_INITIALIZE_SERVER, NULL);
    ok(ret == NO_ERROR, "HttpTerminate failed with %lu\n", ret);
}

enum
{
    HTTP_CANCEL_STRESS_COUNT = 16,
    HTTP_CANCEL_STRESS_ITERATIONS = 16
};

typedef struct _HTTP_CANCEL_STRESS_IO
{
    OVERLAPPED Overlapped;
    char DECLSPEC_ALIGN(8) Buffer[2048];
} HTTP_CANCEL_STRESS_IO;

typedef struct _HTTP_CANCEL_STRESS_CONTEXT
{
    HANDLE Queue;
    OVERLAPPED *Overlapped;
    BOOL Cancelled;
    DWORD Error;
} HTTP_CANCEL_STRESS_CONTEXT;

static DWORD WINAPI http_cancel_stress_thread(void *Parameter)
{
    HTTP_CANCEL_STRESS_CONTEXT *Context = Parameter;

    Context->Cancelled = CancelIoEx(Context->Queue, Context->Overlapped);
    Context->Error = Context->Cancelled ? NO_ERROR : GetLastError();
    return 0;
}

static void test_receive_request_cancellation_stress(void)
{
    HTTPAPI_VERSION Version = HTTPAPI_VERSION_2;
    ULONG Result;
    int Iteration;

    Result = HttpInitialize(Version, HTTP_INITIALIZE_SERVER, NULL);
    ok(Result == NO_ERROR, "HttpInitialize failed with %lu\n", Result);
    if (Result != NO_ERROR)
        return;

    for (Iteration = 0; Iteration < HTTP_CANCEL_STRESS_ITERATIONS; ++Iteration)
    {
        HTTP_CANCEL_STRESS_IO Operations[HTTP_CANCEL_STRESS_COUNT] = {0};
        HTTP_CANCEL_STRESS_CONTEXT Contexts[HTTP_CANCEL_STRESS_COUNT] = {0};
        HANDLE CancelThreads[HTTP_CANCEL_STRESS_COUNT] = {0};
        HANDLE Queue = NULL;
        ULONG PendingCount = 0;
        ULONG ThreadCount = 0;
        ULONG Index;
        BOOL CloseWithPending = (Iteration & 1) != 0;

        Result = HttpCreateRequestQueue(Version, NULL, NULL, 0, &Queue);
        ok(Result == NO_ERROR, "HttpCreateRequestQueue failed with %lu\n", Result);
        if (Result != NO_ERROR)
            break;

        for (Index = 0; Index < HTTP_CANCEL_STRESS_COUNT; ++Index)
        {
            Operations[Index].Overlapped.hEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
            ok(Operations[Index].Overlapped.hEvent != NULL,
               "CreateEventW for pending receive %lu failed with %lu\n", Index, GetLastError());
            if (!Operations[Index].Overlapped.hEvent)
                break;

            Result = HttpReceiveHttpRequest(Queue,
                                            HTTP_NULL_ID,
                                            0,
                                            (HTTP_REQUEST *)Operations[Index].Buffer,
                                            sizeof(Operations[Index].Buffer),
                                            NULL,
                                            &Operations[Index].Overlapped);
            ok(Result == ERROR_IO_PENDING, "Expected ERROR_IO_PENDING, got %lu\n", Result);
            if (Result != ERROR_IO_PENDING)
                break;

            ++PendingCount;
        }

        if (CloseWithPending)
        {
            Result = HttpCloseRequestQueue(Queue);
            ok(Result == NO_ERROR, "HttpCloseRequestQueue with pending receives failed with %lu\n",
               Result);
            if (Result != NO_ERROR)
            {
                BOOL Cancelled = CancelIoEx(Queue, NULL);
                DWORD Error = Cancelled ? NO_ERROR : GetLastError();
                ok(Cancelled || Error == ERROR_NOT_FOUND,
                   "CancelIoEx after queue-close failure failed with %lu\n", Error);
                if (!CloseHandle(Queue))
                {
                    Error = GetLastError();
                    ok(FALSE, "CloseHandle after queue-close failure failed with %lu\n", Error);
                }
            }
            Queue = NULL;
        }
        else
        {
            for (Index = 0; Index < PendingCount; ++Index)
            {
                Contexts[Index].Queue = Queue;
                Contexts[Index].Overlapped = &Operations[Index].Overlapped;
                CancelThreads[Index] = CreateThread(NULL,
                                                    0,
                                                    http_cancel_stress_thread,
                                                    &Contexts[Index],
                                                    0,
                                                    NULL);
                ok(CancelThreads[Index] != NULL,
                   "CreateThread for cancellation %lu failed with %lu\n", Index, GetLastError());
                if (!CancelThreads[Index])
                    break;
                ++ThreadCount;
            }

            for (Index = 0; Index < ThreadCount; ++Index)
            {
                DWORD WaitResult = WaitForSingleObject(CancelThreads[Index], 5000);
                ok(WaitResult == WAIT_OBJECT_0, "Cancellation thread %lu did not exit\n", Index);
                if (WaitResult != WAIT_OBJECT_0)
                    WaitForSingleObject(CancelThreads[Index], INFINITE);
                ok(Contexts[Index].Cancelled,
                   "CancelIoEx for receive %lu failed with %lu\n", Index, Contexts[Index].Error);
                CloseHandle(CancelThreads[Index]);
            }

            if (ThreadCount != PendingCount)
            {
                BOOL Cancelled = CancelIoEx(Queue, NULL);
                DWORD Error = Cancelled ? NO_ERROR : GetLastError();
                ok(Cancelled || Error == ERROR_NOT_FOUND,
                   "CancelIoEx fallback failed with %lu\n", Error);
            }
        }

        for (Index = 0; Index < PendingCount; ++Index)
        {
            DWORD WaitResult = WaitForSingleObject(Operations[Index].Overlapped.hEvent, 5000);
            ok(WaitResult == WAIT_OBJECT_0, "Pending receive %lu did not complete\n", Index);
            if (WaitResult != WAIT_OBJECT_0)
                WaitForSingleObject(Operations[Index].Overlapped.hEvent, INFINITE);
            if (!CloseWithPending)
            {
                DWORD BytesReturned;
                BOOL Completed = GetOverlappedResult(Queue,
                                                     &Operations[Index].Overlapped,
                                                     &BytesReturned,
                                                     FALSE);
                ok(!Completed, "Canceled receive %lu unexpectedly succeeded\n", Index);
                if (!Completed)
                    ok(GetLastError() == ERROR_OPERATION_ABORTED,
                       "Canceled receive %lu completed with %lu\n", Index, GetLastError());
            }
        }

        if (Queue)
        {
            Result = HttpCloseRequestQueue(Queue);
            ok(Result == NO_ERROR, "HttpCloseRequestQueue failed with %lu\n", Result);
        }

        for (Index = 0; Index < HTTP_CANCEL_STRESS_COUNT; ++Index)
        {
            if (Operations[Index].Overlapped.hEvent)
                CloseHandle(Operations[Index].Overlapped.hEvent);
        }
    }

    Result = HttpTerminate(HTTP_INITIALIZE_SERVER, NULL);
    ok(Result == NO_ERROR, "HttpTerminate failed with %lu\n", Result);
}

typedef struct _HTTP_LOOPBACK_CONTEXT
{
    const char *Request;
    const char *ExpectedResponse;
    USHORT Port;
    char Response[512];
    int ResponseLength;
    int Error;
} HTTP_LOOPBACK_CONTEXT;

static DWORD WINAPI http_loopback_client(void *Parameter)
{
    HTTP_LOOPBACK_CONTEXT *Context = Parameter;
    struct sockaddr_in Address;
    SOCKET Socket;
    char Buffer[512];
    int Length, Sent = 0, Timeout = 5000;
    int RequestLength = strlen(Context->Request);

    Socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (Socket == INVALID_SOCKET)
    {
        Context->Error = WSAGetLastError();
        return 0;
    }

    memset(&Address, 0, sizeof(Address));
    Address.sin_family = AF_INET;
    Address.sin_port = htons(Context->Port ? Context->Port : 59992);
    Address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (connect(Socket, (struct sockaddr *)&Address, sizeof(Address)) == SOCKET_ERROR)
    {
        Context->Error = WSAGetLastError();
        closesocket(Socket);
        return 0;
    }

    setsockopt(Socket, SOL_SOCKET, SO_RCVTIMEO, (const char *)&Timeout, sizeof(Timeout));
    Context->Error = 0;
    while (Sent < RequestLength)
    {
        Length = send(Socket, Context->Request + Sent, RequestLength - Sent, 0);
        if (Length == SOCKET_ERROR)
        {
            Context->Error = WSAGetLastError();
            closesocket(Socket);
            return 0;
        }
        Sent += Length;
    }

    while (!strstr(Context->Response, Context->ExpectedResponse) &&
           (Length = recv(Socket, Buffer, sizeof(Buffer), 0)) > 0)
    {
        int Remaining = (int)sizeof(Context->Response) - 1 - Context->ResponseLength;
        int CopyLength = min(Length, Remaining);
        if (CopyLength > 0)
        {
            memcpy(Context->Response + Context->ResponseLength, Buffer, CopyLength);
            Context->ResponseLength += CopyLength;
            Context->Response[Context->ResponseLength] = '\0';
        }
    }
    if (Length == SOCKET_ERROR)
        Context->Error = WSAGetLastError();

    closesocket(Socket);
    return 0;
}

static BOOL test_http_route_request(HANDLE Queue,
                                    HANDLE Event,
                                    const char *RequestText,
                                    USHORT Port,
                                    HTTP_URL_CONTEXT ExpectedContext,
                                    USHORT ExpectedMinorVersion,
                                    ULONG ReceiveFlags,
                                    ULONG RequestBufferLength,
                                    const char *ExpectedBody,
                                    const char *Description)
{
    char DECLSPEC_ALIGN(8) RequestBuffer[4096];
    char BodyBuffer[4096];
    HTTP_LOOPBACK_CONTEXT ClientContext = {0};
    HTTP_REQUEST *Request = (HTTP_REQUEST *)RequestBuffer;
    HTTP_RESPONSE Response = {0};
    OVERLAPPED Overlapped;
    HANDLE ClientThread;
    DWORD BytesReturned = 0, WaitResult;
    ULONG BodyBytes, BodyResult, Result;
    BOOL Success;

    ClientContext.Request = RequestText;
    ClientContext.ExpectedResponse = "HTTP/1.1 200";
    ClientContext.Port = Port;
    ClientThread = CreateThread(NULL, 0, http_loopback_client, &ClientContext, 0, NULL);
    ok(ClientThread != NULL, "%s: CreateThread failed with %lu\n", Description, GetLastError());
    if (!ClientThread)
        return FALSE;

    ResetEvent(Event);
    memset(RequestBuffer, 0, sizeof(RequestBuffer));
    memset(&Overlapped, 0, sizeof(Overlapped));
    Overlapped.hEvent = Event;
    Result = HttpReceiveHttpRequest(Queue,
                                    HTTP_NULL_ID,
                                    ReceiveFlags,
                                    Request,
                                    RequestBufferLength ? RequestBufferLength : sizeof(RequestBuffer),
                                    &BytesReturned,
                                    &Overlapped);
    if (Result == ERROR_IO_PENDING)
    {
        WaitResult = WaitForSingleObject(Event, 5000);
        ok(WaitResult == WAIT_OBJECT_0, "%s: request receive timed out\n", Description);
        if (WaitResult == WAIT_OBJECT_0)
        {
            if (!GetOverlappedResult(Queue, &Overlapped, &BytesReturned, FALSE))
                Result = GetLastError();
            else
                Result = NO_ERROR;
        }
        else
        {
            CancelIoEx(Queue, &Overlapped);
            WaitForSingleObject(Event, INFINITE);
            Result = ERROR_TIMEOUT;
        }
    }
    ok(Result == NO_ERROR, "%s: HttpReceiveHttpRequest failed with %lu\n", Description, Result);

    if (Result == NO_ERROR)
    {
        ok(Request->s.UrlContext == ExpectedContext,
           "%s: expected URL context 0x%I64x, got 0x%I64x\n",
           Description, (ULONGLONG)ExpectedContext, (ULONGLONG)Request->s.UrlContext);
        ok(Request->s.Version.MajorVersion == 1 &&
           Request->s.Version.MinorVersion == ExpectedMinorVersion,
           "%s: expected HTTP/1.%u, got HTTP/%u.%u\n",
           Description,
           ExpectedMinorVersion,
           Request->s.Version.MajorVersion,
           Request->s.Version.MinorVersion);

        if (ExpectedBody)
        {
            if (ReceiveFlags & HTTP_RECEIVE_REQUEST_FLAG_COPY_BODY)
            {
                ok(Request->s.EntityChunkCount == 1,
                   "%s: expected one copied entity chunk, got %u\n",
                   Description, Request->s.EntityChunkCount);
                if (Request->s.EntityChunkCount == 1)
                {
                    HTTP_DATA_CHUNK *Chunk = Request->s.pEntityChunks;
                    ok(Chunk->DataChunkType == HttpDataChunkFromMemory,
                       "%s: unexpected entity chunk type %u\n",
                       Description, Chunk->DataChunkType);
                    if (Chunk->DataChunkType == HttpDataChunkFromMemory)
                    {
                        ok(Chunk->FromMemory.pBuffer != NULL,
                           "%s: copied entity body buffer is null\n",
                           Description);
                        ok(Chunk->FromMemory.BufferLength <= strlen(ExpectedBody),
                           "%s: copied %lu bytes from a %lu-byte body\n",
                           Description,
                           Chunk->FromMemory.BufferLength,
                           (ULONG)strlen(ExpectedBody));
                        ok(Chunk->FromMemory.pBuffer != NULL &&
                           Chunk->FromMemory.BufferLength <= strlen(ExpectedBody) &&
                           !memcmp(Chunk->FromMemory.pBuffer,
                                   ExpectedBody,
                                   Chunk->FromMemory.BufferLength),
                           "%s: copied entity body prefix did not match\n",
                           Description);
                        ok(!!(Request->s.Flags & HTTP_REQUEST_FLAG_MORE_ENTITY_BODY_EXISTS) ==
                           (Chunk->FromMemory.BufferLength < strlen(ExpectedBody)),
                           "%s: incorrect MORE_ENTITY_BODY_EXISTS flag\n",
                           Description);
                        if (Chunk->FromMemory.BufferLength < strlen(ExpectedBody))
                        {
                            ULONG Remaining = (ULONG)strlen(ExpectedBody) -
                                              Chunk->FromMemory.BufferLength;
                            BodyBytes = 0;
                            BodyResult = HttpReceiveRequestEntityBody(
                                Queue,
                                Request->s.RequestId,
                                0,
                                BodyBuffer,
                                sizeof(BodyBuffer),
                                &BodyBytes,
                                NULL);
                            ok(BodyResult == NO_ERROR,
                               "%s: remaining HttpReceiveRequestEntityBody failed with %lu\n",
                               Description, BodyResult);
                            ok(BodyBytes == Remaining &&
                               !memcmp(BodyBuffer,
                                       ExpectedBody + Chunk->FromMemory.BufferLength,
                                       Remaining),
                               "%s: remaining body read returned %lu unexpected bytes\n",
                               Description, BodyBytes);
                        }
                    }
                }
            }
            else
            {
                memset(BodyBuffer, 0, sizeof(BodyBuffer));
                BodyBytes = 0;
                BodyResult = HttpReceiveRequestEntityBody(Queue,
                                                          Request->s.RequestId,
                                                          0,
                                                          BodyBuffer,
                                                          5,
                                                          &BodyBytes,
                                                          NULL);
                ok(BodyResult == NO_ERROR,
                   "%s: first HttpReceiveRequestEntityBody failed with %lu\n",
                   Description, BodyResult);
                ok(BodyBytes == 5 && !memcmp(BodyBuffer, ExpectedBody, 5),
                   "%s: first body read returned %lu unexpected bytes\n",
                   Description, BodyBytes);
                if (BodyResult == NO_ERROR)
                {
                    ULONG Remaining = (ULONG)strlen(ExpectedBody) - 5;
                    BodyBytes = 0;
                    BodyResult = HttpReceiveRequestEntityBody(Queue,
                                                              Request->s.RequestId,
                                                              HTTP_RECEIVE_REQUEST_ENTITY_BODY_FLAG_FILL_BUFFER,
                                                              BodyBuffer + 5,
                                                              sizeof(BodyBuffer) - 5,
                                                              &BodyBytes,
                                                              NULL);
                    ok(BodyResult == NO_ERROR,
                       "%s: second HttpReceiveRequestEntityBody failed with %lu\n",
                       Description, BodyResult);
                    ok(BodyBytes == Remaining &&
                       !memcmp(BodyBuffer + 5, ExpectedBody + 5, Remaining),
                       "%s: second body read returned %lu unexpected bytes\n",
                       Description, BodyBytes);
                    ok(!memcmp(BodyBuffer, ExpectedBody, strlen(ExpectedBody)),
                       "%s: received entity body did not match\n",
                       Description);

                    BodyResult = HttpReceiveRequestEntityBody(Queue,
                                                              Request->s.RequestId,
                                                              0,
                                                              BodyBuffer,
                                                              sizeof(BodyBuffer),
                                                              &BodyBytes,
                                                              NULL);
                    ok(BodyResult == ERROR_HANDLE_EOF,
                       "%s: expected ERROR_HANDLE_EOF after body, got %lu\n",
                       Description, BodyResult);
                }
            }
        }

        Response.s.StatusCode = 200;
        Response.s.pReason = "OK";
        Response.s.ReasonLength = 2;
        Result = HttpSendHttpResponse(Queue,
                                      Request->s.RequestId,
                                      0,
                                      &Response,
                                      NULL,
                                      NULL,
                                      NULL,
                                      0,
                                      NULL,
                                      NULL);
        ok(Result == NO_ERROR, "%s: HttpSendHttpResponse failed with %lu\n", Description, Result);
    }

    WaitResult = WaitForSingleObject(ClientThread, 5000);
    ok(WaitResult == WAIT_OBJECT_0, "%s: HTTP client timed out\n", Description);
    if (WaitResult != WAIT_OBJECT_0)
        WaitForSingleObject(ClientThread, INFINITE);
    ok(ClientContext.Error == 0, "%s: HTTP client failed with %d\n", Description, ClientContext.Error);
    ok(strstr(ClientContext.Response, "HTTP/1.1 200") != NULL,
       "%s: unexpected response: %s\n", Description, ClientContext.Response);
    Success = Result == NO_ERROR && WaitResult == WAIT_OBJECT_0 && ClientContext.Error == 0;
    CloseHandle(ClientThread);
    return Success;
}

typedef struct _HTTP_STALLED_CLIENT_CONTEXT
{
    HANDLE ReadyEvent;
    HANDLE ReleaseEvent;
    int Error;
} HTTP_STALLED_CLIENT_CONTEXT;

static DWORD WINAPI http_stalled_client(void *Parameter)
{
    static const char Request[] =
        "GET /stalled/ HTTP/1.1\r\nHost: 127.0.0.1:59993\r\nX-Stall:";
    HTTP_STALLED_CLIENT_CONTEXT *Context = Parameter;
    struct sockaddr_in Address;
    SOCKET Socket;
    int Sent = 0;

    Socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (Socket == INVALID_SOCKET)
    {
        Context->Error = WSAGetLastError();
        SetEvent(Context->ReadyEvent);
        return 0;
    }

    memset(&Address, 0, sizeof(Address));
    Address.sin_family = AF_INET;
    Address.sin_port = htons(59993);
    Address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (connect(Socket, (struct sockaddr *)&Address, sizeof(Address)) == SOCKET_ERROR)
    {
        Context->Error = WSAGetLastError();
        closesocket(Socket);
        SetEvent(Context->ReadyEvent);
        return 0;
    }

    while (Sent < sizeof(Request) - 1)
    {
        int Length = send(Socket, Request + Sent, sizeof(Request) - 1 - Sent, 0);
        if (Length == SOCKET_ERROR)
        {
            Context->Error = WSAGetLastError();
            closesocket(Socket);
            SetEvent(Context->ReadyEvent);
            return 0;
        }
        Sent += Length;
    }

    SetEvent(Context->ReadyEvent);
    WaitForSingleObject(Context->ReleaseEvent, INFINITE);
    closesocket(Socket);
    return 0;
}

static void test_stalled_connection_teardown(void)
{
    static const WCHAR Url[] = L"http://127.0.0.1:59993/stalled/";
    HTTPAPI_VERSION Version = HTTPAPI_VERSION_1;
    WSADATA WsaData;
    HANDLE Queue = NULL;
    ULONG Result;
    BOOL UrlRegistered = FALSE;
    int Iteration;

    Result = WSAStartup(MAKEWORD(2, 2), &WsaData);
    ok(Result == 0, "WSAStartup failed with %lu\n", Result);
    if (Result != 0)
        return;

    Result = HttpInitialize(Version, HTTP_INITIALIZE_SERVER, NULL);
    ok(Result == NO_ERROR, "HttpInitialize failed with %lu\n", Result);
    if (Result != NO_ERROR)
        goto cleanup_wsa;

    Result = HttpCreateHttpHandle(&Queue, 0);
    ok(Result == NO_ERROR, "HttpCreateHttpHandle failed with %lu\n", Result);
    if (Result != NO_ERROR)
        goto cleanup_http;

    for (Iteration = 0; Iteration < 4; ++Iteration)
    {
        HTTP_STALLED_CLIENT_CONTEXT Context = {0};
        HANDLE ClientThread = NULL;
        HANDLE ReadyEvent = NULL;
        HANDLE ReleaseEvent = NULL;
        DWORD WaitResult;
        BOOL ContinueTesting = TRUE;

        Result = HttpAddUrl(Queue, Url, NULL);
        ok(Result == NO_ERROR, "HttpAddUrl(stalled) failed with %lu\n", Result);
        if (Result != NO_ERROR)
            break;
        UrlRegistered = TRUE;

        ReadyEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
        ReleaseEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
        ok(ReadyEvent != NULL && ReleaseEvent != NULL,
           "CreateEventW for stalled connection failed with %lu\n", GetLastError());
        if (!ReadyEvent || !ReleaseEvent)
        {
            ContinueTesting = FALSE;
            goto cleanup_iteration;
        }

        Context.ReadyEvent = ReadyEvent;
        Context.ReleaseEvent = ReleaseEvent;
        ClientThread = CreateThread(NULL, 0, http_stalled_client, &Context, 0, NULL);
        ok(ClientThread != NULL, "CreateThread for stalled connection failed with %lu\n",
           GetLastError());
        if (!ClientThread)
        {
            ContinueTesting = FALSE;
            goto cleanup_iteration;
        }

        WaitResult = WaitForSingleObject(ReadyEvent, 5000);
        ok(WaitResult == WAIT_OBJECT_0, "Stalled HTTP client did not connect\n");
        if (WaitResult != WAIT_OBJECT_0 || Context.Error)
        {
            ok(Context.Error == 0, "Stalled HTTP client failed with %d\n", Context.Error);
            ContinueTesting = FALSE;
            goto cleanup_iteration;
        }

        Sleep(100);
        Result = HttpRemoveUrl(Queue, Url);
        ok(Result == NO_ERROR, "HttpRemoveUrl(stalled) failed with %lu\n", Result);
        if (Result == NO_ERROR)
            UrlRegistered = FALSE;
        else
            ContinueTesting = FALSE;

cleanup_iteration:
        if (ReleaseEvent)
            SetEvent(ReleaseEvent);
        if (ClientThread)
        {
            WaitResult = WaitForSingleObject(ClientThread, 5000);
            ok(WaitResult == WAIT_OBJECT_0, "Stalled HTTP client did not exit\n");
            if (WaitResult != WAIT_OBJECT_0)
            {
                WaitForSingleObject(ClientThread, INFINITE);
            }
            CloseHandle(ClientThread);
        }
        if (ReadyEvent)
            CloseHandle(ReadyEvent);
        if (ReleaseEvent)
            CloseHandle(ReleaseEvent);
        if (UrlRegistered)
        {
            HttpRemoveUrl(Queue, Url);
            UrlRegistered = FALSE;
        }
        if (!ContinueTesting)
            break;
    }

    if (UrlRegistered)
    {
        HttpRemoveUrl(Queue, Url);
        UrlRegistered = FALSE;
    }
    CloseHandle(Queue);
cleanup_http:
    HttpTerminate(HTTP_INITIALIZE_SERVER, NULL);
cleanup_wsa:
    WSACleanup();
}

static void test_stalled_clients_do_not_delay_requests(void)
{
    static const WCHAR Url[] = L"http://127.0.0.1:59995/busy/";
    static const char StalledRequest[] =
        "GET /busy/stalled HTTP/1.1\r\nHost: 127.0.0.1:59995\r\nX-Stall:";
    static const char RequestText[] =
        "GET /busy/request HTTP/1.1\r\nHost: 127.0.0.1:59995\r\n\r\n";
    SOCKET Sockets[32];
    HTTPAPI_VERSION Version = HTTPAPI_VERSION_1;
    struct sockaddr_in Address;
    WSADATA WsaData;
    HANDLE Queue = NULL, Event = NULL;
    ULONG Result, Connected = 0;
    ULONG Index;

    for (Index = 0; Index < ARRAYSIZE(Sockets); ++Index)
        Sockets[Index] = INVALID_SOCKET;

    Result = WSAStartup(MAKEWORD(2, 2), &WsaData);
    ok(Result == 0, "WSAStartup failed with %lu\n", Result);
    if (Result != 0)
        return;

    Result = HttpInitialize(Version, HTTP_INITIALIZE_SERVER, NULL);
    ok(Result == NO_ERROR, "HttpInitialize failed with %lu\n", Result);
    if (Result != NO_ERROR)
        goto cleanup_wsa;

    Result = HttpCreateHttpHandle(&Queue, 0);
    ok(Result == NO_ERROR, "HttpCreateHttpHandle failed with %lu\n", Result);
    if (Result != NO_ERROR)
        goto cleanup_http;

    Result = HttpAddUrl(Queue, Url, NULL);
    ok(Result == NO_ERROR, "HttpAddUrl(busy) failed with %lu\n", Result);
    if (Result != NO_ERROR)
        goto cleanup_queue;

    memset(&Address, 0, sizeof(Address));
    Address.sin_family = AF_INET;
    Address.sin_port = htons(59995);
    Address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    for (Index = 0; Index < ARRAYSIZE(Sockets); ++Index)
    {
        Sockets[Index] = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (Sockets[Index] == INVALID_SOCKET)
            break;
        if (connect(Sockets[Index], (struct sockaddr *)&Address, sizeof(Address)) == SOCKET_ERROR ||
            send(Sockets[Index], StalledRequest, sizeof(StalledRequest) - 1, 0) !=
                (int)sizeof(StalledRequest) - 1)
            break;
        ++Connected;
    }
    ok(Connected == ARRAYSIZE(Sockets), "only %lu of %lu stalled clients connected, error %d\n",
       Connected, (ULONG)ARRAYSIZE(Sockets), WSAGetLastError());

    Sleep(500);
    Event = CreateEventW(NULL, TRUE, FALSE, NULL);
    ok(Event != NULL, "CreateEventW failed with %lu\n", GetLastError());
    if (Event)
    {
        test_http_route_request(Queue,
                                Event,
                                RequestText,
                                59995,
                                0,
                                1,
                                0,
                                0,
                                NULL,
                                "request behind stalled clients");
        CloseHandle(Event);
    }

    for (Index = 0; Index < ARRAYSIZE(Sockets); ++Index)
    {
        if (Sockets[Index] != INVALID_SOCKET)
            closesocket(Sockets[Index]);
    }
    HttpRemoveUrl(Queue, Url);
cleanup_queue:
    CloseHandle(Queue);
cleanup_http:
    HttpTerminate(HTTP_INITIALIZE_SERVER, NULL);
cleanup_wsa:
    WSACleanup();
}

static void test_url_prefix_routing_precedence(void)
{
    static const char BodyText[] = "bounded body";
    static char LargeBodyText[4097];
    static char LargeBodyRequestText[4200];
    static const char ExplicitRequestText[] =
        "GET /routing/deep/resource/item/leaf HTTP/1.1\r\nHost: localhost:59994\r\n\r\n";
    static const char IpRequestText[] =
        "GET /routing/deep/resource/item/leaf HTTP/1.1\r\nHost: 127.0.0.1:59994\r\n\r\n";
    static const char Http10RequestText[] =
        "GET /routing/deep/resource/item/leaf HTTP/1.0\r\n\r\n";
    static const char BodyRequestText[] =
        "POST /routing/deep/resource/item/leaf HTTP/1.1\r\n"
        "Host: localhost:59994\r\n"
        "Content-Length: 12\r\n\r\n"
        "bounded body";
    HTTPAPI_VERSION Version = HTTPAPI_VERSION_2;
    HTTP_SERVER_SESSION_ID Session = HTTP_NULL_ID;
    HTTP_URL_GROUP_ID Group = HTTP_NULL_ID;
    HTTP_BINDING_INFO Binding;
    HANDLE Queue = NULL, Event = NULL;
    WSADATA WsaData;
    ULONG Result;
    BOOL UrlGroupCreated = FALSE;
    BOOL SessionCreated = FALSE;

    Result = WSAStartup(MAKEWORD(2, 2), &WsaData);
    ok(Result == 0, "WSAStartup failed with %lu\n", Result);
    if (Result != 0)
        return;

    Result = HttpInitialize(Version, HTTP_INITIALIZE_SERVER, NULL);
    ok(Result == NO_ERROR, "HttpInitialize failed with %lu\n", Result);
    if (Result != NO_ERROR)
        goto cleanup_wsa;

    Result = HttpCreateServerSession(Version, &Session, 0);
    ok(Result == NO_ERROR, "HttpCreateServerSession failed with %lu\n", Result);
    if (Result != NO_ERROR)
        goto cleanup_http;
    SessionCreated = TRUE;

    Result = HttpCreateUrlGroup(Session, &Group, 0);
    ok(Result == NO_ERROR, "HttpCreateUrlGroup failed with %lu\n", Result);
    if (Result != NO_ERROR)
        goto cleanup_session;
    UrlGroupCreated = TRUE;

    Result = HttpCreateRequestQueue(Version, NULL, NULL, 0, &Queue);
    ok(Result == NO_ERROR, "HttpCreateRequestQueue failed with %lu\n", Result);
    if (Result != NO_ERROR)
        goto cleanup_group;

    memset(&Binding, 0, sizeof(Binding));
    Binding.Flags.Present = 1;
    Binding.RequestQueueHandle = Queue;
    Result = HttpSetUrlGroupProperty(Group,
                                     HttpServerBindingProperty,
                                     &Binding,
                                     sizeof(Binding));
    ok(Result == NO_ERROR, "HttpSetUrlGroupProperty failed with %lu\n", Result);
    if (Result != NO_ERROR)
        goto cleanup_queue;

    Result = HttpAddUrlToUrlGroup(Group, L"http://localhost:59994/routing/", 0x1111, 0);
    ok(Result == NO_ERROR, "HttpAddUrlToUrlGroup(base) failed with %lu\n", Result);
    if (Result != NO_ERROR)
        goto cleanup_queue;

    Result = HttpAddUrlToUrlGroup(Group,
                                  L"http://localhost:59994/routing/deep/",
                                  0x2222,
                                  0);
    ok(Result == NO_ERROR, "HttpAddUrlToUrlGroup(deep) failed with %lu\n", Result);
    if (Result != NO_ERROR)
    {
        HttpRemoveUrlFromUrlGroup(Group, NULL, HTTP_URL_FLAG_REMOVE_ALL);
        goto cleanup_queue;
    }

    Result = HttpAddUrlToUrlGroup(Group,
                                  L"http://127.0.0.1:59994/routing/deep/resource/",
                                  0x4444,
                                  0);
    ok(Result == NO_ERROR, "HttpAddUrlToUrlGroup(IP-bound) failed with %lu\n", Result);
    if (Result != NO_ERROR)
        goto cleanup_urls;

    Result = HttpAddUrlToUrlGroup(Group,
                                  L"http://*:59994/routing/deep/resource/item/",
                                  0x5555,
                                  0);
    ok(Result == NO_ERROR, "HttpAddUrlToUrlGroup(weak wildcard) failed with %lu\n", Result);
    if (Result != NO_ERROR)
        goto cleanup_urls;

    for (ULONG i = 0; i < sizeof(LargeBodyText) - 1; ++i)
        LargeBodyText[i] = 'a' + i % 26;
    sprintf(LargeBodyRequestText,
            "POST /routing/deep/resource/item/leaf HTTP/1.1\r\n"
            "Host: localhost:59994\r\n"
            "Content-Length: %lu\r\n\r\n",
            (ULONG)sizeof(LargeBodyText) - 1);
    memcpy(LargeBodyRequestText + strlen(LargeBodyRequestText),
           LargeBodyText,
           sizeof(LargeBodyText) - 1);

    Event = CreateEventW(NULL, TRUE, FALSE, NULL);
    ok(Event != NULL, "CreateEventW failed with %lu\n", GetLastError());
    if (!Event)
        goto cleanup_urls;

    test_http_route_request(Queue,
                            Event,
                            ExplicitRequestText,
                            59994,
                            0x2222,
                            1,
                            0,
                            0,
                            NULL,
                            "longest explicit prefix");

    Result = HttpAddUrlToUrlGroup(Group, L"http://+:59994/routing/", 0x3333, 0);
    ok(Result == NO_ERROR, "HttpAddUrlToUrlGroup(strong wildcard) failed with %lu\n", Result);
    if (Result == NO_ERROR)
    {
        test_http_route_request(Queue,
                                Event,
                                ExplicitRequestText,
                                59994,
                                0x3333,
                                1,
                                0,
                                0,
                                NULL,
                                "strong wildcard priority");
        Result = HttpRemoveUrlFromUrlGroup(Group, L"http://+:59994/routing/", 0);
        ok(Result == NO_ERROR, "HttpRemoveUrlFromUrlGroup(strong wildcard) failed with %lu\n", Result);
    }

    Result = HttpRemoveUrlFromUrlGroup(Group, L"http://localhost:59994/routing/", 0);
    ok(Result == NO_ERROR, "HttpRemoveUrlFromUrlGroup(base explicit) failed with %lu\n", Result);
    Result = HttpRemoveUrlFromUrlGroup(Group, L"http://localhost:59994/routing/deep/", 0);
    ok(Result == NO_ERROR, "HttpRemoveUrlFromUrlGroup(deep explicit) failed with %lu\n", Result);
    test_http_route_request(Queue,
                            Event,
                            IpRequestText,
                            59994,
                            0x4444,
                            1,
                            0,
                            0,
                            NULL,
                            "IP-bound priority");

    Result = HttpRemoveUrlFromUrlGroup(Group,
                                       L"http://127.0.0.1:59994/routing/deep/resource/",
                                       0);
    ok(Result == NO_ERROR, "HttpRemoveUrlFromUrlGroup(IP-bound) failed with %lu\n", Result);
    test_http_route_request(Queue,
                            Event,
                            IpRequestText,
                            59994,
                            0x5555,
                            1,
                            0,
                            0,
                            NULL,
                            "weak wildcard fallback");
    test_http_route_request(Queue,
                            Event,
                            BodyRequestText,
                            59994,
                            0x5555,
                            1,
                            0,
                            0,
                            BodyText,
                            "Content-Length entity receive");
    test_http_route_request(Queue,
                            Event,
                            BodyRequestText,
                            59994,
                            0x5555,
                            1,
                            HTTP_RECEIVE_REQUEST_FLAG_COPY_BODY,
                            0,
                            BodyText,
                            "Content-Length copied entity");
    test_http_route_request(Queue,
                            Event,
                            LargeBodyRequestText,
                            59994,
                            0x5555,
                            1,
                            HTTP_RECEIVE_REQUEST_FLAG_COPY_BODY,
                            2048,
                            LargeBodyText,
                            "partial copied entity body");
    test_http_route_request(Queue,
                            Event,
                            Http10RequestText,
                            59994,
                            0x5555,
                            0,
                            0,
                            0,
                            NULL,
                            "HTTP/1.0 request without Host");

    CloseHandle(Event);
cleanup_urls:
    HttpRemoveUrlFromUrlGroup(Group, NULL, HTTP_URL_FLAG_REMOVE_ALL);
cleanup_queue:
    HttpCloseRequestQueue(Queue);
cleanup_group:
    if (UrlGroupCreated)
        HttpCloseUrlGroup(Group);
cleanup_session:
    if (SessionCreated)
        HttpCloseServerSession(Session);
cleanup_http:
    HttpTerminate(HTTP_INITIALIZE_SERVER, NULL);
cleanup_wsa:
    WSACleanup();
}

static void test_loopback_http_server(void)
{
    static const char body[] = "LiberNT HTTP works";
    static const char HttpRequestText[] =
        "GET /functional/ HTTP/1.1\r\nHost: localhost:59992\r\n\r\n";
    static const char UnmatchedRequest[] =
        "GET /not-registered/ HTTP/1.1\r\nHost: localhost:59992\r\n\r\n";
    static const char PrefixBoundaryRequest[] =
        "GET /shorter/ HTTP/1.1\r\nHost: localhost:59992\r\n\r\n";
    char DECLSPEC_ALIGN(8) RequestBuffer[4096];
    HTTP_LOOPBACK_CONTEXT ClientContext = {0}, UnmatchedContext = {0};
    HTTPAPI_VERSION Version = HTTPAPI_VERSION_1;
    HTTP_REQUEST *Request = (HTTP_REQUEST *)RequestBuffer;
    HTTP_RESPONSE Response;
    HTTP_DATA_CHUNK Chunk;
    OVERLAPPED Overlapped;
    HTTP_LOOPBACK_CONTEXT PrefixBoundaryContext = {0};
    HANDLE Queue = NULL, ClientThread = NULL;
    HANDLE Event = NULL;
    SOCKET RebindSocket = INVALID_SOCKET;
    DWORD BytesReturned = 0;
    WSADATA WsaData;
    ULONG Result;
    DWORD WaitResult;
    BOOL FunctionalUrlRegistered = FALSE;
    BOOL PrefixUrlRegistered = FALSE;

    Result = WSAStartup(MAKEWORD(2, 2), &WsaData);
    ok(Result == 0, "WSAStartup failed with %lu\n", Result);
    if (Result != 0)
        return;

    Result = HttpInitialize(Version, HTTP_INITIALIZE_SERVER, NULL);
    ok(Result == NO_ERROR, "HttpInitialize failed with %lu\n", Result);
    if (Result != NO_ERROR)
        goto cleanup_wsa;

    Result = HttpCreateHttpHandle(&Queue, 0);
    ok(Result == NO_ERROR, "HttpCreateHttpHandle failed with %lu\n", Result);
    if (Result != NO_ERROR)
        goto cleanup_http;

    Result = HttpAddUrl(Queue, L"http://127.0.0.1:59992/functional/", NULL);
    ok(Result == NO_ERROR, "HttpAddUrl failed with %lu\n", Result);
    if (Result != NO_ERROR)
        goto cleanup_queue;
    FunctionalUrlRegistered = TRUE;

    Result = HttpAddUrl(Queue, L"http://localhost:59992/short", NULL);
    ok(Result == NO_ERROR, "HttpAddUrl without a trailing path slash failed with %lu\n", Result);
    PrefixUrlRegistered = Result == NO_ERROR;

    ClientContext.Request = HttpRequestText;
    ClientContext.ExpectedResponse = "HTTP/1.1 200";
    ClientThread = CreateThread(NULL, 0, http_loopback_client, &ClientContext, 0, NULL);
    ok(ClientThread != NULL, "CreateThread failed with %lu\n", GetLastError());
    if (!ClientThread)
        goto cleanup_url;

    memset(RequestBuffer, 0, sizeof(RequestBuffer));
    memset(&Overlapped, 0, sizeof(Overlapped));
    Event = CreateEventW(NULL, TRUE, FALSE, NULL);
    ok(Event != NULL, "CreateEventW failed with %lu\n", GetLastError());
    if (!Event)
        goto cleanup_client;
    Overlapped.hEvent = Event;

    Result = HttpReceiveHttpRequest(Queue,
                                    HTTP_NULL_ID,
                                    0,
                                    Request,
                                    sizeof(RequestBuffer),
                                    &BytesReturned,
                                    &Overlapped);
    if (Result == ERROR_IO_PENDING)
    {
        WaitResult = WaitForSingleObject(Event, 5000);
        ok(WaitResult == WAIT_OBJECT_0, "Request receive timed out\n");
        if (WaitResult == WAIT_OBJECT_0)
        {
            if (!GetOverlappedResult(Queue, &Overlapped, &BytesReturned, FALSE))
                Result = GetLastError();
            else
                Result = NO_ERROR;
        }
        else
        {
            CancelIoEx(Queue, &Overlapped);
            Result = ERROR_TIMEOUT;
        }
    }
    ok(Result == NO_ERROR, "HttpReceiveHttpRequest failed with %lu\n", Result);

    if (Result == NO_ERROR)
    {
        memset(&Response, 0, sizeof(Response));
        memset(&Chunk, 0, sizeof(Chunk));
        Response.s.StatusCode = 200;
        Response.s.pReason = "OK";
        Response.s.ReasonLength = 2;
        Response.s.EntityChunkCount = 1;
        Response.s.pEntityChunks = &Chunk;
        Chunk.DataChunkType = HttpDataChunkFromMemory;
        Chunk.FromMemory.pBuffer = (PVOID)body;
        Chunk.FromMemory.BufferLength = sizeof(body) - 1;

        Result = HttpSendHttpResponse(Queue,
                                      Request->s.RequestId,
                                      0,
                                      &Response,
                                      NULL,
                                      NULL,
                                      NULL,
                                      0,
                                      NULL,
                                      NULL);
        ok(Result == NO_ERROR, "HttpSendHttpResponse failed with %lu\n", Result);
    }

    WaitResult = WaitForSingleObject(ClientThread, 5000);
    ok(WaitResult == WAIT_OBJECT_0, "HTTP client timed out\n");
    if (WaitResult == WAIT_OBJECT_0)
    {
        ok(ClientContext.Error == 0, "HTTP client failed with %d\n", ClientContext.Error);
        ok(strstr(ClientContext.Response, "HTTP/1.1 200") != NULL,
           "Unexpected HTTP response: %s\n", ClientContext.Response);
        ok(strstr(ClientContext.Response, body) != NULL,
           "HTTP response body was missing: %s\n", ClientContext.Response);

        UnmatchedContext.Request = UnmatchedRequest;
        UnmatchedContext.ExpectedResponse = "HTTP/1.1 404";
        http_loopback_client(&UnmatchedContext);
        ok(UnmatchedContext.Error == 0,
           "Unmatched HTTP client failed with %d\n", UnmatchedContext.Error);
        ok(strstr(UnmatchedContext.Response, UnmatchedContext.ExpectedResponse) != NULL,
           "Expected an unmatched URL response, got: %s\n", UnmatchedContext.Response);

        PrefixBoundaryContext.Request = PrefixBoundaryRequest;
        PrefixBoundaryContext.ExpectedResponse = "HTTP/1.1 404";
        http_loopback_client(&PrefixBoundaryContext);
        ok(PrefixBoundaryContext.Error == 0,
           "Prefix-boundary client failed with %d\n", PrefixBoundaryContext.Error);
        ok(strstr(PrefixBoundaryContext.Response, PrefixBoundaryContext.ExpectedResponse) != NULL,
           "Expected a non-matching URL prefix response, got: %s\n",
           PrefixBoundaryContext.Response);

        if (PrefixUrlRegistered)
        {
            Result = HttpRemoveUrl(Queue, L"http://localhost:59992/short");
            ok(Result == NO_ERROR, "HttpRemoveUrl(short) failed with %lu\n", Result);
            if (Result == NO_ERROR)
                PrefixUrlRegistered = FALSE;
        }

        Result = HttpRemoveUrl(Queue, L"http://127.0.0.1:59992/functional/");
        ok(Result == NO_ERROR, "HttpRemoveUrl(functional) failed with %lu\n", Result);
        if (Result == NO_ERROR)
            FunctionalUrlRegistered = FALSE;

        if (!PrefixUrlRegistered && !FunctionalUrlRegistered)
        {
            Result = HttpAddUrl(Queue, L"http://127.0.0.1:59992/functional/", NULL);
            ok(Result == NO_ERROR, "HttpAddUrl after listener teardown failed with %lu\n", Result);
            FunctionalUrlRegistered = Result == NO_ERROR;

            if (FunctionalUrlRegistered)
            {
                DWORD Attempts = 0;
                DWORD Retry;

                for (Retry = 0; Retry < 100; ++Retry)
                {
                    struct sockaddr_in Address;

                    ++Attempts;
                    RebindSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
                    if (RebindSocket == INVALID_SOCKET)
                    {
                        Result = WSAGetLastError();
                        break;
                    }

                    memset(&Address, 0, sizeof(Address));
                    Address.sin_family = AF_INET;
                    Address.sin_port = htons(59992);
                    Address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
                    if (connect(RebindSocket,
                                (struct sockaddr *)&Address,
                                sizeof(Address)) != SOCKET_ERROR)
                    {
                        Result = NO_ERROR;
                        break;
                    }

                    Result = WSAGetLastError();
                    closesocket(RebindSocket);
                    RebindSocket = INVALID_SOCKET;
                    if (Result != WSAECONNREFUSED)
                        break;
                    Sleep(50);
                }
                ok(Result == NO_ERROR,
                   "connect after listener rebind failed with %lu after %lu attempts\n",
                   Result, Attempts);
                if (RebindSocket != INVALID_SOCKET)
                {
                    closesocket(RebindSocket);
                    RebindSocket = INVALID_SOCKET;
                }
            }
        }
    }

    CloseHandle(Event);
cleanup_client:
    if (ClientThread)
    {
        WaitForSingleObject(ClientThread, 5000);
        CloseHandle(ClientThread);
    }
cleanup_url:
    if (FunctionalUrlRegistered)
        HttpRemoveUrl(Queue, L"http://127.0.0.1:59992/functional/");
    if (PrefixUrlRegistered)
        HttpRemoveUrl(Queue, L"http://localhost:59992/short");
cleanup_queue:
    if (RebindSocket != INVALID_SOCKET)
        closesocket(RebindSocket);
    CloseHandle(Queue);
cleanup_http:
    HttpTerminate(HTTP_INITIALIZE_SERVER, NULL);
cleanup_wsa:
    WSACleanup();
}

typedef struct _HTTP_KA_CLIENT
{
    SOCKET Socket;
    char Buffer[4096];
    int Length;
} HTTP_KA_CLIENT;

enum
{
    HTTP_KA_OPEN,
    HTTP_KA_CLOSED,
    HTTP_KA_RESET,
    HTTP_KA_DATA,
};

static BOOL http_ka_connect(HTTP_KA_CLIENT *Client, USHORT Port)
{
    struct sockaddr_in Address;

    Client->Length = 0;
    Client->Socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (Client->Socket == INVALID_SOCKET)
        return FALSE;

    memset(&Address, 0, sizeof(Address));
    Address.sin_family = AF_INET;
    Address.sin_port = htons(Port);
    Address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (connect(Client->Socket, (struct sockaddr *)&Address, sizeof(Address)) == SOCKET_ERROR)
    {
        closesocket(Client->Socket);
        Client->Socket = INVALID_SOCKET;
        return FALSE;
    }
    return TRUE;
}

static BOOL http_ka_send(HTTP_KA_CLIENT *Client, const char *Data)
{
    int Length = (int)strlen(Data);

    return send(Client->Socket, Data, Length, 0) == Length;
}

static int http_ka_wait(HTTP_KA_CLIENT *Client, int TimeoutMs)
{
    int Result, Error;

    if (Client->Length)
        return HTTP_KA_DATA;
    setsockopt(Client->Socket, SOL_SOCKET, SO_RCVTIMEO, (const char *)&TimeoutMs, sizeof(TimeoutMs));
    Result = recv(Client->Socket, Client->Buffer, sizeof(Client->Buffer) - 1, 0);
    if (Result > 0)
    {
        Client->Length = Result;
        Client->Buffer[Client->Length] = '\0';
        return HTTP_KA_DATA;
    }
    if (!Result)
        return HTTP_KA_CLOSED;
    Error = WSAGetLastError();
    return Error == WSAETIMEDOUT ? HTTP_KA_OPEN : HTTP_KA_RESET;
}

static BOOL http_ka_read_response(HTTP_KA_CLIENT *Client, char *Response, int ResponseSize, int TimeoutMs)
{
    const char *End, *Length;
    int Total, Received;

    setsockopt(Client->Socket, SOL_SOCKET, SO_RCVTIMEO, (const char *)&TimeoutMs, sizeof(TimeoutMs));
    for (;;)
    {
        Client->Buffer[Client->Length] = '\0';
        End = strstr(Client->Buffer, "\r\n\r\n");
        if (End)
        {
            Length = strstr(Client->Buffer, "Content-Length:");
            Total = (int)(End + 4 - Client->Buffer);
            if (Length && Length < End)
                Total += atoi(Length + 15);
            if (Client->Length >= Total && Total < ResponseSize)
            {
                memcpy(Response, Client->Buffer, Total);
                Response[Total] = '\0';
                memmove(Client->Buffer, Client->Buffer + Total, Client->Length - Total);
                Client->Length -= Total;
                return TRUE;
            }
        }
        if (Client->Length >= (int)sizeof(Client->Buffer) - 1)
            return FALSE;
        Received = recv(Client->Socket, Client->Buffer + Client->Length,
                        (int)sizeof(Client->Buffer) - 1 - Client->Length, 0);
        if (Received <= 0)
            return FALSE;
        Client->Length += Received;
    }
}

static ULONG http_ka_receive(HANDLE Queue, OVERLAPPED *Overlapped, ULONG Flags,
                             HTTP_REQUEST *Request, ULONG Size, DWORD TimeoutMs)
{
    DWORD BytesReturned;
    ULONG Result;

    ResetEvent(Overlapped->hEvent);
    memset(Request, 0, Size);
    Result = HttpReceiveHttpRequest(Queue, HTTP_NULL_ID, Flags, Request, Size, NULL, Overlapped);
    if (Result != ERROR_IO_PENDING)
        return Result;
    if (WaitForSingleObject(Overlapped->hEvent, TimeoutMs) != WAIT_OBJECT_0)
        return ERROR_IO_PENDING;
    if (!GetOverlappedResult(Queue, Overlapped, &BytesReturned, FALSE))
        return GetLastError();
    return NO_ERROR;
}

static void http_ka_cancel(HANDLE Queue, OVERLAPPED *Overlapped)
{
    DWORD BytesReturned;

    CancelIoEx(Queue, Overlapped);
    GetOverlappedResult(Queue, Overlapped, &BytesReturned, TRUE);
}

static ULONG http_ka_respond(HANDLE Queue, HTTP_REQUEST_ID RequestId, ULONG Flags, const char *Body)
{
    HTTP_RESPONSE Response = {0};
    HTTP_DATA_CHUNK Chunk;

    Response.s.StatusCode = 200;
    Response.s.pReason = "OK";
    Response.s.ReasonLength = 2;
    if (Body)
    {
        Chunk.DataChunkType = HttpDataChunkFromMemory;
        Chunk.FromMemory.pBuffer = (void *)Body;
        Chunk.FromMemory.BufferLength = (ULONG)strlen(Body);
        Response.s.EntityChunkCount = 1;
        Response.s.pEntityChunks = &Chunk;
    }
    return HttpSendHttpResponse(Queue, RequestId, Flags, &Response, NULL, NULL, NULL, 0, NULL, NULL);
}

static BOOL http_ka_path_is(HTTP_REQUEST *Request, const char *Path)
{
    return Request->s.RawUrlLength == strlen(Path) &&
           !memcmp(Request->s.pRawUrl, Path, Request->s.RawUrlLength);
}

static void http_ka_exchange(HANDLE Queue, OVERLAPPED *Overlapped, HTTP_KA_CLIENT *Client,
                             const char *RequestText, const char *Path, ULONG ResponseFlags,
                             const char *ExpectedConnection, int ExpectedState,
                             HTTP_CONNECTION_ID *ConnectionId, const char *Description)
{
    char DECLSPEC_ALIGN(8) RequestBuffer[4096];
    HTTP_REQUEST *Request = (HTTP_REQUEST *)RequestBuffer;
    char Response[1024], Header[64];
    ULONG Result;
    int State;

    ok(http_ka_send(Client, RequestText), "%s: send failed with %d\n", Description, WSAGetLastError());
    Result = http_ka_receive(Queue, Overlapped, 0, Request, sizeof(RequestBuffer), 5000);
    ok(Result == NO_ERROR, "%s: HttpReceiveHttpRequest returned %lu\n", Description, Result);
    if (Result == ERROR_IO_PENDING)
        http_ka_cancel(Queue, Overlapped);
    if (Result != NO_ERROR)
        return;

    ok(http_ka_path_is(Request, Path), "%s: received %.*s\n", Description,
       Request->s.RawUrlLength, Request->s.pRawUrl);
    if (ConnectionId)
    {
        if (*ConnectionId != HTTP_NULL_ID)
            ok(Request->s.ConnectionId == *ConnectionId, "%s: connection id changed\n", Description);
        *ConnectionId = Request->s.ConnectionId;
    }

    Result = http_ka_respond(Queue, Request->s.RequestId, ResponseFlags, "body");
    ok(Result == NO_ERROR, "%s: HttpSendHttpResponse returned %lu\n", Description, Result);
    ok(http_ka_read_response(Client, Response, sizeof(Response), 3000),
       "%s: no complete response\n", Description);
    ok(!strncmp(Response, "HTTP/1.1 200", 12), "%s: response %s\n", Description, Response);
    if (ExpectedConnection)
    {
        sprintf(Header, "\r\nConnection: %s\r\n", ExpectedConnection);
        ok(strstr(Response, Header) != NULL, "%s: no Connection: %s in %s\n",
           Description, ExpectedConnection, Response);
    }
    else
    {
        ok(strstr(Response, "\r\nConnection:") == NULL, "%s: unexpected Connection header in %s\n",
           Description, Response);
    }
    State = http_ka_wait(Client, 300);
    ok(State == ExpectedState, "%s: connection state %d after the response, expected %d\n",
       Description, State, ExpectedState);
}

static void test_keep_alive(void)
{
    static const WCHAR Url[] = L"http://127.0.0.1:59997/keep/";
    static const char Get[] = "GET /keep/a HTTP/1.1\r\nHost: 127.0.0.1:59997\r\n\r\n";
    static const char GetB[] = "GET /keep/b HTTP/1.1\r\nHost: 127.0.0.1:59997\r\n\r\n";
    static const char Pipelined[] =
        "GET /keep/p1 HTTP/1.1\r\nHost: 127.0.0.1:59997\r\n\r\n"
        "GET /keep/p2 HTTP/1.1\r\nHost: 127.0.0.1:59997\r\n\r\n";
    static const char Close[] =
        "GET /keep/close HTTP/1.1\r\nHost: 127.0.0.1:59997\r\nConnection: close\r\n\r\n";
    static const char Get10[] = "GET /keep/10 HTTP/1.0\r\n\r\n";
    static const char Get10KeepAlive[] =
        "GET /keep/10ka HTTP/1.0\r\nConnection: Keep-Alive\r\n\r\n";
    static const char UnreadBody[] =
        "POST /keep/unread HTTP/1.1\r\nHost: 127.0.0.1:59997\r\nContent-Length: 5\r\n\r\nhello";
    static const char PartialBody[] =
        "POST /keep/partial HTTP/1.1\r\nHost: 127.0.0.1:59997\r\nContent-Length: 10\r\n\r\n012";
    static const char Unrouted[] =
        "POST /elsewhere HTTP/1.1\r\nHost: 127.0.0.1:59997\r\nContent-Length: 10\r\n\r\n";
    char DECLSPEC_ALIGN(8) RequestBuffer[4096];
    char DECLSPEC_ALIGN(8) SecondBuffer[4096];
    HTTP_REQUEST *Request = (HTTP_REQUEST *)RequestBuffer;
    HTTP_REQUEST *Second = (HTTP_REQUEST *)SecondBuffer;
    HTTPAPI_VERSION Version = HTTPAPI_VERSION_1;
    HTTP_CONNECTION_ID ConnectionId;
    HTTP_REQUEST_ID FirstRequestId;
    HTTP_KA_CLIENT Client;
    OVERLAPPED Overlapped = {0}, SecondOverlapped = {0}, BodyOverlapped = {0};
    char Response[1024], Body[64];
    DWORD BytesReturned;
    WSADATA WsaData;
    HANDLE Queue = NULL;
    ULONG Result;
    int State;

    Result = WSAStartup(MAKEWORD(2, 2), &WsaData);
    ok(Result == 0, "WSAStartup failed with %lu\n", Result);
    if (Result != 0)
        return;

    Result = HttpInitialize(Version, HTTP_INITIALIZE_SERVER, NULL);
    ok(Result == NO_ERROR, "HttpInitialize failed with %lu\n", Result);
    if (Result != NO_ERROR)
        goto cleanup_wsa;

    Result = HttpCreateHttpHandle(&Queue, 0);
    ok(Result == NO_ERROR, "HttpCreateHttpHandle failed with %lu\n", Result);
    if (Result != NO_ERROR)
        goto cleanup_http;
    Result = HttpAddUrl(Queue, Url, NULL);
    ok(Result == NO_ERROR, "HttpAddUrl(keep) failed with %lu\n", Result);
    if (Result != NO_ERROR)
        goto cleanup_queue;

    Overlapped.hEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
    SecondOverlapped.hEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
    BodyOverlapped.hEvent = CreateEventW(NULL, TRUE, FALSE, NULL);

    if (http_ka_connect(&Client, 59997))
    {
        ConnectionId = HTTP_NULL_ID;
        http_ka_exchange(Queue, &Overlapped, &Client, Get, "/keep/a", 0, NULL, HTTP_KA_OPEN,
                         &ConnectionId, "first keep-alive request");
        http_ka_exchange(Queue, &Overlapped, &Client, GetB, "/keep/b", 0, NULL, HTTP_KA_OPEN,
                         &ConnectionId, "second keep-alive request");
        http_ka_exchange(Queue, &Overlapped, &Client, Get, "/keep/a",
                         HTTP_SEND_RESPONSE_FLAG_DISCONNECT, "close", HTTP_KA_CLOSED,
                         &ConnectionId, "response with DISCONNECT");
        closesocket(Client.Socket);
    }
    else
        ok(0, "keep-alive client failed to connect with %d\n", WSAGetLastError());

    if (http_ka_connect(&Client, 59997))
    {
        http_ka_exchange(Queue, &Overlapped, &Client, Close, "/keep/close", 0, "close", HTTP_KA_CLOSED,
                         NULL, "Connection: close");
        closesocket(Client.Socket);
    }
    if (http_ka_connect(&Client, 59997))
    {
        http_ka_exchange(Queue, &Overlapped, &Client, Get10, "/keep/10", 0, "close", HTTP_KA_CLOSED,
                         NULL, "HTTP/1.0");
        closesocket(Client.Socket);
    }
    if (http_ka_connect(&Client, 59997))
    {
        ConnectionId = HTTP_NULL_ID;
        http_ka_exchange(Queue, &Overlapped, &Client, Get10KeepAlive, "/keep/10ka", 0,
                         "keep-alive", HTTP_KA_OPEN, &ConnectionId, "HTTP/1.0 keep-alive");
        http_ka_exchange(Queue, &Overlapped, &Client, Get, "/keep/a", 0, NULL, HTTP_KA_OPEN,
                         &ConnectionId, "request after HTTP/1.0 keep-alive");
        closesocket(Client.Socket);
    }

    if (http_ka_connect(&Client, 59997))
    {
        ok(http_ka_send(&Client, Pipelined), "pipelined send failed\n");
        Result = http_ka_receive(Queue, &Overlapped, 0, Request, sizeof(RequestBuffer), 5000);
        ok(Result == NO_ERROR, "first pipelined request returned %lu\n", Result);
        if (Result == NO_ERROR)
        {
            ok(http_ka_path_is(Request, "/keep/p1"), "first pipelined request has the wrong path\n");
            FirstRequestId = Request->s.RequestId;
            ConnectionId = Request->s.ConnectionId;
            Result = http_ka_receive(Queue, &SecondOverlapped, 0, Second, sizeof(SecondBuffer), 300);
            ok(Result == ERROR_IO_PENDING,
               "second pipelined request before the first response returned %lu\n", Result);
            ok(http_ka_respond(Queue, FirstRequestId, 0, "one") == NO_ERROR,
               "first pipelined response failed\n");
            if (Result == ERROR_IO_PENDING)
            {
                Result = WaitForSingleObject(SecondOverlapped.hEvent, 5000) == WAIT_OBJECT_0 &&
                         GetOverlappedResult(Queue, &SecondOverlapped, &BytesReturned, FALSE)
                         ? NO_ERROR : ERROR_IO_PENDING;
                if (Result == ERROR_IO_PENDING)
                    http_ka_cancel(Queue, &SecondOverlapped);
            }
            ok(Result == NO_ERROR, "second pipelined request returned %lu\n", Result);
            if (Result == NO_ERROR)
            {
                ok(http_ka_path_is(Second, "/keep/p2"), "second pipelined request has the wrong path\n");
                ok(Second->s.ConnectionId == ConnectionId, "pipelined connection id changed\n");
                ok(Second->s.RequestId != FirstRequestId, "pipelined request id was reused\n");
                ok(http_ka_respond(Queue, Second->s.RequestId, 0, "two") == NO_ERROR,
                   "second pipelined response failed\n");
            }
            ok(http_ka_read_response(&Client, Response, sizeof(Response), 3000) &&
               strstr(Response, "\r\n\r\none"), "first pipelined response: %s\n", Response);
            ok(http_ka_read_response(&Client, Response, sizeof(Response), 3000) &&
               strstr(Response, "\r\n\r\ntwo"), "second pipelined response: %s\n", Response);
        }
        closesocket(Client.Socket);
    }

    if (http_ka_connect(&Client, 59997))
    {
        ConnectionId = HTTP_NULL_ID;
        http_ka_exchange(Queue, &Overlapped, &Client, UnreadBody, "/keep/unread", 0, NULL, HTTP_KA_OPEN,
                         &ConnectionId, "unread request body");
        http_ka_exchange(Queue, &Overlapped, &Client, Get, "/keep/a", 0, NULL, HTTP_KA_OPEN,
                         &ConnectionId, "request after an unread body");
        closesocket(Client.Socket);
    }

    if (http_ka_connect(&Client, 59997))
    {
        ok(http_ka_send(&Client, PartialBody), "partial body send failed\n");
        Result = http_ka_receive(Queue, &Overlapped, HTTP_RECEIVE_REQUEST_FLAG_COPY_BODY,
                                 Request, sizeof(RequestBuffer), 3000);
        ok(Result == NO_ERROR, "request with a partial body returned %lu\n", Result);
        if (Result == ERROR_IO_PENDING)
            http_ka_cancel(Queue, &Overlapped);
        if (Result == NO_ERROR)
        {
            ok(Request->s.Flags & HTTP_REQUEST_FLAG_MORE_ENTITY_BODY_EXISTS,
               "partial body: MORE_ENTITY_BODY_EXISTS not set\n");
            ok(Request->s.BytesReceived == sizeof(PartialBody) - 1, "partial body: BytesReceived %I64u\n",
               Request->s.BytesReceived);
            ok(Request->s.EntityChunkCount == 1, "partial body: %u entity chunks\n",
               Request->s.EntityChunkCount);
            if (Request->s.EntityChunkCount == 1)
                ok(Request->s.pEntityChunks->FromMemory.BufferLength == 3 &&
                   !memcmp(Request->s.pEntityChunks->FromMemory.pBuffer, "012", 3),
                   "partial body: copied %lu bytes\n",
                   Request->s.pEntityChunks->FromMemory.BufferLength);

            ResetEvent(BodyOverlapped.hEvent);
            BytesReturned = 0;
            memset(Body, 0, sizeof(Body));
            Result = HttpReceiveRequestEntityBody(Queue, Request->s.RequestId, 0, Body,
                                                  sizeof(Body), NULL, &BodyOverlapped);
            ok(Result == ERROR_IO_PENDING, "body read before the body arrived returned %lu\n", Result);
            if (Result == ERROR_IO_PENDING)
            {
                ok(WaitForSingleObject(BodyOverlapped.hEvent, 300) == WAIT_TIMEOUT,
                   "body read completed before the body arrived\n");
                ok(http_ka_send(&Client, "3456789"), "body send failed\n");
                Result = WaitForSingleObject(BodyOverlapped.hEvent, 3000) == WAIT_OBJECT_0 &&
                         GetOverlappedResult(Queue, &BodyOverlapped, &BytesReturned, FALSE)
                         ? NO_ERROR : ERROR_IO_PENDING;
                if (Result == ERROR_IO_PENDING)
                    http_ka_cancel(Queue, &BodyOverlapped);
                ok(Result == NO_ERROR, "pending body read did not complete\n");
                ok(BytesReturned == 7 && !memcmp(Body, "3456789", 7),
                   "pending body read returned %lu bytes: %s\n", BytesReturned, Body);
            }
            Result = HttpReceiveRequestEntityBody(Queue, Request->s.RequestId, 0, Body,
                                                  sizeof(Body), &BytesReturned, NULL);
            ok(Result == ERROR_HANDLE_EOF, "body read after the body returned %lu\n", Result);
            ok(http_ka_respond(Queue, Request->s.RequestId, 0, "body") == NO_ERROR,
               "partial body response failed\n");
            ok(http_ka_read_response(&Client, Response, sizeof(Response), 3000),
               "partial body: no response\n");
            State = http_ka_wait(&Client, 300);
            ok(State == HTTP_KA_OPEN, "partial body: connection state %d after the response\n", State);
        }
        closesocket(Client.Socket);
    }

    if (http_ka_connect(&Client, 59997))
    {
        ok(http_ka_send(&Client, Unrouted), "unrouted send failed\n");
        ok(http_ka_read_response(&Client, Response, sizeof(Response), 3000),
           "unrouted request without its body: no response\n");
        ok(!strncmp(Response, "HTTP/1.1 404", 12), "unrouted request: %s\n", Response);
        State = http_ka_wait(&Client, 1000);
        ok(State == HTTP_KA_CLOSED, "unrouted request: connection state %d after the 404\n", State);
        closesocket(Client.Socket);
    }

    CloseHandle(Overlapped.hEvent);
    CloseHandle(SecondOverlapped.hEvent);
    CloseHandle(BodyOverlapped.hEvent);
    HttpRemoveUrl(Queue, Url);
cleanup_queue:
    CloseHandle(Queue);
cleanup_http:
    HttpTerminate(HTTP_INITIALIZE_SERVER, NULL);
cleanup_wsa:
    WSACleanup();
}

static ULONG http_ch_read_body(HANDLE Queue, OVERLAPPED *Overlapped, HTTP_REQUEST_ID RequestId,
                               ULONG Flags, char *Body, ULONG Size, ULONG *Bytes, DWORD TimeoutMs)
{
    DWORD BytesReturned = 0;
    ULONG Result;

    ResetEvent(Overlapped->hEvent);
    memset(Body, 0, Size);
    *Bytes = 0;
    Result = HttpReceiveRequestEntityBody(Queue, RequestId, Flags, Body, Size - 1, NULL, Overlapped);
    if (Result == ERROR_IO_PENDING)
    {
        if (WaitForSingleObject(Overlapped->hEvent, TimeoutMs) != WAIT_OBJECT_0)
        {
            http_ka_cancel(Queue, Overlapped);
            return ERROR_IO_PENDING;
        }
        Result = GetOverlappedResult(Queue, Overlapped, &BytesReturned, FALSE) ? NO_ERROR : GetLastError();
    }
    else if (Result == NO_ERROR)
    {
        GetOverlappedResult(Queue, Overlapped, &BytesReturned, TRUE);
    }
    *Bytes = BytesReturned;
    return Result;
}

static ULONG http_ch_send_body(HANDLE Queue, HTTP_REQUEST_ID RequestId, ULONG Flags, const char *Body)
{
    HTTP_DATA_CHUNK Chunk;

    Chunk.DataChunkType = HttpDataChunkFromMemory;
    Chunk.FromMemory.pBuffer = (void *)Body;
    Chunk.FromMemory.BufferLength = Body ? (ULONG)strlen(Body) : 0;
    return HttpSendResponseEntityBody(Queue, RequestId, Flags, Body ? 1 : 0, Body ? &Chunk : NULL,
                                      NULL, NULL, 0, NULL, NULL);
}

static ULONG http_ch_respond(HANDLE Queue, HTTP_REQUEST_ID RequestId, ULONG Flags, const char *Body,
                             const char *TransferEncoding, const char *ContentLength)
{
    HTTP_RESPONSE Response = {0};
    HTTP_DATA_CHUNK Chunk;

    Response.s.StatusCode = 200;
    Response.s.pReason = "OK";
    Response.s.ReasonLength = 2;
    if (TransferEncoding)
    {
        Response.s.Headers.KnownHeaders[HttpHeaderTransferEncoding].pRawValue = TransferEncoding;
        Response.s.Headers.KnownHeaders[HttpHeaderTransferEncoding].RawValueLength =
            (USHORT)strlen(TransferEncoding);
    }
    if (ContentLength)
    {
        Response.s.Headers.KnownHeaders[HttpHeaderContentLength].pRawValue = ContentLength;
        Response.s.Headers.KnownHeaders[HttpHeaderContentLength].RawValueLength =
            (USHORT)strlen(ContentLength);
    }
    if (Body)
    {
        Chunk.DataChunkType = HttpDataChunkFromMemory;
        Chunk.FromMemory.pBuffer = (void *)Body;
        Chunk.FromMemory.BufferLength = (ULONG)strlen(Body);
        Response.s.EntityChunkCount = 1;
        Response.s.pEntityChunks = &Chunk;
    }
    return HttpSendHttpResponse(Queue, RequestId, Flags, &Response, NULL, NULL, NULL, 0, NULL, NULL);
}

static int http_ch_read_all(HTTP_KA_CLIENT *Client, char *Response, int ResponseSize, int TimeoutMs)
{
    int Received, Error, State = HTTP_KA_OPEN;

    setsockopt(Client->Socket, SOL_SOCKET, SO_RCVTIMEO, (const char *)&TimeoutMs, sizeof(TimeoutMs));
    while (Client->Length < (int)sizeof(Client->Buffer) - 1)
    {
        Received = recv(Client->Socket, Client->Buffer + Client->Length,
                        (int)sizeof(Client->Buffer) - 1 - Client->Length, 0);
        if (Received > 0)
        {
            Client->Length += Received;
            continue;
        }
        if (!Received)
            State = HTTP_KA_CLOSED;
        else
        {
            Error = WSAGetLastError();
            State = Error == WSAETIMEDOUT ? HTTP_KA_OPEN : HTTP_KA_RESET;
        }
        break;
    }

    Client->Buffer[Client->Length] = '\0';
    if (Client->Length < ResponseSize)
        memcpy(Response, Client->Buffer, Client->Length + 1);
    else
        Response[0] = '\0';
    Client->Length = 0;
    return State;
}

static const char *http_ch_body(const char *Response)
{
    const char *End = strstr(Response, "\r\n\r\n");

    return End ? End + 4 : "";
}

static void http_ch_check_rejected(HANDLE Queue, OVERLAPPED *Overlapped, const char *RequestText,
                                   const char *ExpectedStatus, const char *Description)
{
    char DECLSPEC_ALIGN(8) RequestBuffer[4096];
    HTTP_REQUEST *Request = (HTTP_REQUEST *)RequestBuffer;
    HTTP_KA_CLIENT Client;
    char Response[1024];
    ULONG Result;
    int State;

    if (!http_ka_connect(&Client, 59996))
    {
        ok(0, "%s: connect failed with %d\n", Description, WSAGetLastError());
        return;
    }
    ok(http_ka_send(&Client, RequestText), "%s: send failed\n", Description);
    Result = http_ka_receive(Queue, Overlapped, 0, Request, sizeof(RequestBuffer), 1000);
    ok(Result == ERROR_IO_PENDING, "%s: HttpReceiveHttpRequest returned %lu\n", Description, Result);
    if (Result == ERROR_IO_PENDING)
        http_ka_cancel(Queue, Overlapped);
    else if (Result == NO_ERROR)
        http_ka_respond(Queue, Request->s.RequestId, HTTP_SEND_RESPONSE_FLAG_DISCONNECT, NULL);

    State = http_ch_read_all(&Client, Response, sizeof(Response), 1000);
    ok(!strncmp(Response, ExpectedStatus, strlen(ExpectedStatus)), "%s: response %s\n",
       Description, Response);
    ok(strstr(Response, "\r\nConnection: close\r\n") != NULL, "%s: no Connection: close in %s\n",
       Description, Response);
    ok(State == HTTP_KA_CLOSED, "%s: connection state %d after the response\n", Description, State);
    closesocket(Client.Socket);
}

static void test_chunked(void)
{
    static const WCHAR Url[] = L"http://127.0.0.1:59996/chunk/";
    static const char Get[] = "GET /chunk/next HTTP/1.1\r\nHost: 127.0.0.1:59996\r\n\r\n";
    static const char Whole[] =
        "POST /chunk/whole HTTP/1.1\r\nHost: 127.0.0.1:59996\r\nTransfer-Encoding: chunked\r\n\r\n"
        "5\r\nhello\r\n6;name=value\r\n world\r\n0\r\nX-Trailer: t\r\n\r\n";
    static const char Split[] =
        "POST /chunk/split HTTP/1.1\r\nHost: 127.0.0.1:59996\r\nTransfer-Encoding: chunked\r\n\r\n4\r\nab";
    static const char Unread[] =
        "POST /chunk/unread HTTP/1.1\r\nHost: 127.0.0.1:59996\r\nTransfer-Encoding: chunked\r\n\r\n"
        "3\r\nabc\r\n0\r\n\r\n";
    static const char Malformed[] =
        "POST /chunk/bad HTTP/1.1\r\nHost: 127.0.0.1:59996\r\nTransfer-Encoding: chunked\r\n\r\nzz\r\n";
    static const char Gzip[] =
        "POST /chunk/gzip HTTP/1.1\r\nHost: 127.0.0.1:59996\r\nTransfer-Encoding: gzip\r\n\r\n";
    static const char GzipChunked[] =
        "POST /chunk/gzip HTTP/1.1\r\nHost: 127.0.0.1:59996\r\nTransfer-Encoding: gzip, chunked\r\n\r\n"
        "3\r\nabc\r\n0\r\n\r\n";
    static const char Both[] =
        "POST /chunk/both HTTP/1.1\r\nHost: 127.0.0.1:59996\r\nContent-Length: 3\r\n"
        "Transfer-Encoding: chunked\r\n\r\n5\r\nhello\r\n0\r\n\r\n";
    static const char Headers[] =
        "POST /chunk/late HTTP/1.1\r\nHost: 127.0.0.1:59996\r\nTransfer-Encoding: chunked\r\n\r\n";
    static const char Unrouted[] =
        "POST /elsewhere HTTP/1.1\r\nHost: 127.0.0.1:59996\r\nTransfer-Encoding: chunked\r\n\r\n";
    static const char Stream[] = "GET /chunk/stream HTTP/1.1\r\nHost: 127.0.0.1:59996\r\n\r\n";
    static const char Stream10[] = "GET /chunk/stream10 HTTP/1.0\r\n\r\n";
    char DECLSPEC_ALIGN(8) RequestBuffer[4096];
    HTTP_REQUEST *Request = (HTTP_REQUEST *)RequestBuffer;
    HTTPAPI_VERSION Version = HTTPAPI_VERSION_1;
    HTTP_CONNECTION_ID ConnectionId;
    HTTP_KA_CLIENT Client;
    OVERLAPPED Overlapped = {0}, BodyOverlapped = {0};
    char Response[2048], Body[64];
    const HTTP_KNOWN_HEADER *TransferEncoding;
    WSADATA WsaData;
    HANDLE Queue = NULL;
    ULONG Result, Bytes;
    int State;

    Result = WSAStartup(MAKEWORD(2, 2), &WsaData);
    ok(Result == 0, "WSAStartup failed with %lu\n", Result);
    if (Result != 0)
        return;

    Result = HttpInitialize(Version, HTTP_INITIALIZE_SERVER, NULL);
    ok(Result == NO_ERROR, "HttpInitialize failed with %lu\n", Result);
    if (Result != NO_ERROR)
        goto cleanup_wsa;

    Result = HttpCreateHttpHandle(&Queue, 0);
    ok(Result == NO_ERROR, "HttpCreateHttpHandle failed with %lu\n", Result);
    if (Result != NO_ERROR)
        goto cleanup_http;
    Result = HttpAddUrl(Queue, Url, NULL);
    ok(Result == NO_ERROR, "HttpAddUrl(chunk) failed with %lu\n", Result);
    if (Result != NO_ERROR)
        goto cleanup_queue;

    Overlapped.hEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
    BodyOverlapped.hEvent = CreateEventW(NULL, TRUE, FALSE, NULL);

    if (http_ka_connect(&Client, 59996))
    {
        ok(http_ka_send(&Client, Whole), "whole chunked send failed\n");
        Sleep(200);
        Result = http_ka_receive(Queue, &Overlapped, HTTP_RECEIVE_REQUEST_FLAG_COPY_BODY,
                                 Request, sizeof(RequestBuffer), 3000);
        ok(Result == NO_ERROR, "whole chunked request returned %lu\n", Result);
        if (Result == ERROR_IO_PENDING)
            http_ka_cancel(Queue, &Overlapped);
        if (Result == NO_ERROR)
        {
            ConnectionId = Request->s.ConnectionId;
            TransferEncoding = &Request->s.Headers.KnownHeaders[HttpHeaderTransferEncoding];
            ok(TransferEncoding->RawValueLength == 7 && !memcmp(TransferEncoding->pRawValue, "chunked", 7),
               "whole chunked: Transfer-Encoding %.*s\n",
               TransferEncoding->RawValueLength, TransferEncoding->pRawValue);
            ok(!Request->s.Headers.KnownHeaders[HttpHeaderContentLength].RawValueLength,
               "whole chunked: Content-Length %.*s\n",
               Request->s.Headers.KnownHeaders[HttpHeaderContentLength].RawValueLength,
               Request->s.Headers.KnownHeaders[HttpHeaderContentLength].pRawValue);
            ok(Request->s.Headers.TrailerCount == 0, "whole chunked: %u trailers\n",
               Request->s.Headers.TrailerCount);
            ok(Request->s.Headers.UnknownHeaderCount == 0, "whole chunked: %u unknown headers\n",
               Request->s.Headers.UnknownHeaderCount);
            ok(Request->s.EntityChunkCount == 1, "whole chunked: %u entity chunks\n",
               Request->s.EntityChunkCount);
            if (Request->s.EntityChunkCount == 1)
                ok(Request->s.pEntityChunks->FromMemory.BufferLength == 11 &&
                   !memcmp(Request->s.pEntityChunks->FromMemory.pBuffer, "hello world", 11),
                   "whole chunked: copied %lu bytes: %.*s\n",
                   Request->s.pEntityChunks->FromMemory.BufferLength,
                   (int)Request->s.pEntityChunks->FromMemory.BufferLength,
                   (const char *)Request->s.pEntityChunks->FromMemory.pBuffer);
            ok(!(Request->s.Flags & HTTP_REQUEST_FLAG_MORE_ENTITY_BODY_EXISTS),
               "whole chunked: flags %#lx\n", Request->s.Flags);
            ok(Request->s.BytesReceived == sizeof(Whole) - 1, "whole chunked: BytesReceived %I64u\n",
               Request->s.BytesReceived);

            Result = http_ch_read_body(Queue, &BodyOverlapped, Request->s.RequestId, 0,
                                       Body, sizeof(Body), &Bytes, 1000);
            ok(Result == ERROR_HANDLE_EOF, "whole chunked: body read returned %lu, %lu bytes: %s\n",
               Result, Bytes, Body);
            ok(http_ka_respond(Queue, Request->s.RequestId, 0, "done") == NO_ERROR,
               "whole chunked: response failed\n");
            ok(http_ka_read_response(&Client, Response, sizeof(Response), 3000),
               "whole chunked: no response\n");
            http_ka_exchange(Queue, &Overlapped, &Client, Get, "/chunk/next", 0, NULL, HTTP_KA_OPEN,
                             &ConnectionId, "request after a chunked body");
        }
        closesocket(Client.Socket);
    }
    else
        ok(0, "chunked client failed to connect with %d\n", WSAGetLastError());

    if (http_ka_connect(&Client, 59996))
    {
        ok(http_ka_send(&Client, Split), "split chunked send failed\n");
        Result = http_ka_receive(Queue, &Overlapped, 0, Request, sizeof(RequestBuffer), 3000);
        ok(Result == NO_ERROR, "split chunked request returned %lu\n", Result);
        if (Result == ERROR_IO_PENDING)
            http_ka_cancel(Queue, &Overlapped);
        if (Result == NO_ERROR)
        {
            ConnectionId = Request->s.ConnectionId;
            ok(Request->s.Flags & HTTP_REQUEST_FLAG_MORE_ENTITY_BODY_EXISTS,
               "split chunked: flags %#lx\n", Request->s.Flags);
            ok(Request->s.EntityChunkCount == 0, "split chunked: %u entity chunks\n",
               Request->s.EntityChunkCount);

            Result = http_ch_read_body(Queue, &BodyOverlapped, Request->s.RequestId, 0,
                                       Body, sizeof(Body), &Bytes, 1000);
            ok(Result == NO_ERROR && Bytes == 2 && !memcmp(Body, "ab", 2),
               "split chunked: first body read returned %lu, %lu bytes: %s\n", Result, Bytes, Body);

            ResetEvent(BodyOverlapped.hEvent);
            memset(Body, 0, sizeof(Body));
            Result = HttpReceiveRequestEntityBody(Queue, Request->s.RequestId,
                                                  HTTP_RECEIVE_REQUEST_ENTITY_BODY_FLAG_FILL_BUFFER,
                                                  Body, sizeof(Body) - 1, NULL, &BodyOverlapped);
            ok(Result == ERROR_IO_PENDING, "split chunked: pending body read returned %lu\n", Result);
            if (Result == ERROR_IO_PENDING)
            {
                DWORD BytesReturned = 0;

                ok(WaitForSingleObject(BodyOverlapped.hEvent, 300) == WAIT_TIMEOUT,
                   "split chunked: body read completed before the rest arrived\n");
                ok(http_ka_send(&Client, "cd\r\n3\r\nefg\r\n0\r\n\r\n"), "split chunked: send failed\n");
                Result = WaitForSingleObject(BodyOverlapped.hEvent, 3000) == WAIT_OBJECT_0 &&
                         GetOverlappedResult(Queue, &BodyOverlapped, &BytesReturned, FALSE)
                         ? NO_ERROR : ERROR_IO_PENDING;
                if (Result == ERROR_IO_PENDING)
                    http_ka_cancel(Queue, &BodyOverlapped);
                ok(Result == NO_ERROR && BytesReturned == 5 && !memcmp(Body, "cdefg", 5),
                   "split chunked: FILL_BUFFER read returned %lu, %lu bytes: %s\n",
                   Result, BytesReturned, Body);
            }
            Result = http_ch_read_body(Queue, &BodyOverlapped, Request->s.RequestId, 0,
                                       Body, sizeof(Body), &Bytes, 1000);
            ok(Result == ERROR_HANDLE_EOF, "split chunked: last body read returned %lu, %lu bytes\n",
               Result, Bytes);
            ok(http_ka_respond(Queue, Request->s.RequestId, 0, "done") == NO_ERROR,
               "split chunked: response failed\n");
            ok(http_ka_read_response(&Client, Response, sizeof(Response), 3000),
               "split chunked: no response\n");
            http_ka_exchange(Queue, &Overlapped, &Client, Get, "/chunk/next", 0, NULL, HTTP_KA_OPEN,
                             &ConnectionId, "request after a split chunked body");
        }
        closesocket(Client.Socket);
    }

    if (http_ka_connect(&Client, 59996))
    {
        ConnectionId = HTTP_NULL_ID;
        http_ka_exchange(Queue, &Overlapped, &Client, Unread, "/chunk/unread", 0, NULL, HTTP_KA_OPEN,
                         &ConnectionId, "unread chunked body");
        http_ka_exchange(Queue, &Overlapped, &Client, Get, "/chunk/next", 0, NULL, HTTP_KA_OPEN,
                         &ConnectionId, "request after an unread chunked body");
        closesocket(Client.Socket);
    }

    if (http_ka_connect(&Client, 59996))
    {
        ok(http_ka_send(&Client, Malformed), "malformed chunk send failed\n");
        Result = http_ka_receive(Queue, &Overlapped, 0, Request, sizeof(RequestBuffer), 1000);
        ok(Result == ERROR_IO_PENDING, "malformed chunk: HttpReceiveHttpRequest returned %lu\n", Result);
        if (Result == ERROR_IO_PENDING)
            http_ka_cancel(Queue, &Overlapped);
        State = http_ch_read_all(&Client, Response, sizeof(Response), 1000);
        ok(!strncmp(Response, "HTTP/1.1 400", 12), "malformed chunk: client got %s\n", Response);
        ok(strstr(Response, "\r\nConnection: close\r\n") != NULL, "malformed chunk: no Connection: close in %s\n",
           Response);
        ok(State == HTTP_KA_CLOSED, "malformed chunk: connection state %d\n", State);
        closesocket(Client.Socket);
    }

    if (http_ka_connect(&Client, 59996))
    {
        ok(http_ka_send(&Client, Headers), "late malformed chunk send failed\n");
        Result = http_ka_receive(Queue, &Overlapped, 0, Request, sizeof(RequestBuffer), 3000);
        ok(Result == NO_ERROR, "late malformed chunk: HttpReceiveHttpRequest returned %lu\n", Result);
        if (Result == ERROR_IO_PENDING)
            http_ka_cancel(Queue, &Overlapped);
        if (Result == NO_ERROR)
        {
            ok(Request->s.Flags & HTTP_REQUEST_FLAG_MORE_ENTITY_BODY_EXISTS,
               "late malformed chunk: flags %#lx\n", Request->s.Flags);
            ok(Request->s.BytesReceived == sizeof(Headers) - 1, "late malformed chunk: BytesReceived %I64u\n",
               Request->s.BytesReceived);
            ResetEvent(BodyOverlapped.hEvent);
            Result = HttpReceiveRequestEntityBody(Queue, Request->s.RequestId, 0, Body, sizeof(Body) - 1,
                                                  NULL, &BodyOverlapped);
            ok(Result == ERROR_IO_PENDING, "late malformed chunk: body read returned %lu\n", Result);
            ok(http_ka_send(&Client, "2\r\nab\r\nzz\r\n"), "late malformed chunk: send failed\n");
            if (Result == ERROR_IO_PENDING)
            {
                DWORD BytesReturned = 0;

                if (WaitForSingleObject(BodyOverlapped.hEvent, 3000) == WAIT_OBJECT_0)
                    Result = GetOverlappedResult(Queue, &BodyOverlapped, &BytesReturned, FALSE)
                             ? NO_ERROR : GetLastError();
                else
                    http_ka_cancel(Queue, &BodyOverlapped);
                ok(Result == NO_ERROR && BytesReturned == 2 && !memcmp(Body, "ab", 2),
                   "late malformed chunk: first body read returned %lu, %lu bytes\n", Result, BytesReturned);
            }
            Result = http_ch_read_body(Queue, &BodyOverlapped, Request->s.RequestId, 0,
                                       Body, sizeof(Body), &Bytes, 1000);
            ok(Result == ERROR_INVALID_FUNCTION, "late malformed chunk: body read returned %lu, %lu bytes\n",
               Result, Bytes);
            Result = http_ka_respond(Queue, Request->s.RequestId, 0, "done");
            ok(Result == ERROR_BAD_COMMAND, "late malformed chunk: response returned %lu\n", Result);
        }
        State = http_ch_read_all(&Client, Response, sizeof(Response), 1000);
        ok(!strncmp(Response, "HTTP/1.1 400", 12), "late malformed chunk: client got %s\n", Response);
        ok(State == HTTP_KA_CLOSED, "late malformed chunk: connection state %d\n", State);
        closesocket(Client.Socket);
    }

    if (http_ka_connect(&Client, 59996))
    {
        ok(http_ka_send(&Client, Both), "Content-Length with chunked send failed\n");
        Sleep(200);
        Result = http_ka_receive(Queue, &Overlapped, HTTP_RECEIVE_REQUEST_FLAG_COPY_BODY,
                                 Request, sizeof(RequestBuffer), 3000);
        ok(Result == NO_ERROR, "Content-Length with chunked: HttpReceiveHttpRequest returned %lu\n", Result);
        if (Result == ERROR_IO_PENDING)
            http_ka_cancel(Queue, &Overlapped);
        if (Result == NO_ERROR)
        {
            const HTTP_KNOWN_HEADER *ContentLength =
                &Request->s.Headers.KnownHeaders[HttpHeaderContentLength];

            ok(ContentLength->RawValueLength == 1 && ContentLength->pRawValue[0] == '3',
               "Content-Length with chunked: Content-Length %.*s\n",
               ContentLength->RawValueLength, ContentLength->pRawValue);
            ok(Request->s.EntityChunkCount == 1, "Content-Length with chunked: %u entity chunks\n",
               Request->s.EntityChunkCount);
            if (Request->s.EntityChunkCount == 1)
                ok(Request->s.pEntityChunks->FromMemory.BufferLength == 5 &&
                   !memcmp(Request->s.pEntityChunks->FromMemory.pBuffer, "hello", 5),
                   "Content-Length with chunked: copied %lu bytes: %.*s\n",
                   Request->s.pEntityChunks->FromMemory.BufferLength,
                   (int)Request->s.pEntityChunks->FromMemory.BufferLength,
                   (const char *)Request->s.pEntityChunks->FromMemory.pBuffer);
            ok(!(Request->s.Flags & HTTP_REQUEST_FLAG_MORE_ENTITY_BODY_EXISTS),
               "Content-Length with chunked: flags %#lx\n", Request->s.Flags);
            ok(http_ka_respond(Queue, Request->s.RequestId, 0, "done") == NO_ERROR,
               "Content-Length with chunked: response failed\n");
        }
        State = http_ch_read_all(&Client, Response, sizeof(Response), 1000);
        ok(!strncmp(Response, "HTTP/1.1 200", 12), "Content-Length with chunked: client got %s\n", Response);
        closesocket(Client.Socket);
    }

    http_ch_check_rejected(Queue, &Overlapped, Gzip, "HTTP/1.1 501", "Transfer-Encoding: gzip");

    if (http_ka_connect(&Client, 59996))
    {
        ok(http_ka_send(&Client, GzipChunked), "gzip, chunked send failed\n");
        Sleep(200);
        Result = http_ka_receive(Queue, &Overlapped, HTTP_RECEIVE_REQUEST_FLAG_COPY_BODY,
                                 Request, sizeof(RequestBuffer), 3000);
        ok(Result == NO_ERROR, "gzip, chunked: HttpReceiveHttpRequest returned %lu\n", Result);
        if (Result == ERROR_IO_PENDING)
            http_ka_cancel(Queue, &Overlapped);
        if (Result == NO_ERROR)
        {
            ConnectionId = Request->s.ConnectionId;
            TransferEncoding = &Request->s.Headers.KnownHeaders[HttpHeaderTransferEncoding];
            ok(TransferEncoding->RawValueLength == 13 && !memcmp(TransferEncoding->pRawValue, "gzip, chunked", 13),
               "gzip, chunked: Transfer-Encoding %.*s\n",
               TransferEncoding->RawValueLength, TransferEncoding->pRawValue);
            ok(Request->s.EntityChunkCount == 1, "gzip, chunked: %u entity chunks\n",
               Request->s.EntityChunkCount);
            if (Request->s.EntityChunkCount == 1)
                ok(Request->s.pEntityChunks->FromMemory.BufferLength == 3 &&
                   !memcmp(Request->s.pEntityChunks->FromMemory.pBuffer, "abc", 3),
                   "gzip, chunked: copied %lu bytes\n", Request->s.pEntityChunks->FromMemory.BufferLength);
            ok(http_ka_respond(Queue, Request->s.RequestId, 0, "done") == NO_ERROR,
               "gzip, chunked: response failed\n");
            ok(http_ka_read_response(&Client, Response, sizeof(Response), 3000), "gzip, chunked: no response\n");
            ok(strstr(Response, "\r\nConnection:") == NULL, "gzip, chunked: Connection header in %s\n", Response);
            http_ka_exchange(Queue, &Overlapped, &Client, Get, "/chunk/next", 0, NULL, HTTP_KA_OPEN,
                             &ConnectionId, "request after gzip, chunked");
        }
        closesocket(Client.Socket);
    }
    http_ch_check_rejected(Queue, &Overlapped, Unrouted, "HTTP/1.1 404", "unrouted chunked request");

    if (http_ka_connect(&Client, 59996))
    {
        ok(http_ka_send(&Client, Stream), "stream send failed\n");
        Result = http_ka_receive(Queue, &Overlapped, 0, Request, sizeof(RequestBuffer), 3000);
        ok(Result == NO_ERROR, "stream request returned %lu\n", Result);
        if (Result == ERROR_IO_PENDING)
            http_ka_cancel(Queue, &Overlapped);
        if (Result == NO_ERROR)
        {
            ConnectionId = Request->s.ConnectionId;
            ok(http_ch_respond(Queue, Request->s.RequestId, HTTP_SEND_RESPONSE_FLAG_MORE_DATA, "ab",
                               NULL, NULL) == NO_ERROR, "stream: first send failed\n");
            ok(http_ch_send_body(Queue, Request->s.RequestId, HTTP_SEND_RESPONSE_FLAG_MORE_DATA,
                                 "0123456789abcdefg") == NO_ERROR, "stream: second send failed\n");
            ok(http_ch_send_body(Queue, Request->s.RequestId, HTTP_SEND_RESPONSE_FLAG_MORE_DATA, "") ==
               NO_ERROR, "stream: empty send failed\n");
            ok(http_ch_send_body(Queue, Request->s.RequestId, 0, "z") == NO_ERROR,
               "stream: last send failed\n");
            State = http_ch_read_all(&Client, Response, sizeof(Response), 500);
            ok(!strncmp(Response, "HTTP/1.1 200", 12), "stream: response %s\n", Response);
            ok(strstr(Response, "\r\nTransfer-Encoding:") == NULL, "stream: Transfer-Encoding in %s\n", Response);
            ok(strstr(Response, "\r\nConnection:") == NULL, "stream: Connection header in %s\n", Response);
            ok(strstr(Response, "\r\nContent-Length:") == NULL, "stream: Content-Length in %s\n", Response);
            ok(!strcmp(http_ch_body(Response), "ab0123456789abcdefgz"), "stream: body %s\n",
               http_ch_body(Response));
            ok(State == HTTP_KA_OPEN, "stream: connection state %d after the response\n", State);
            http_ka_exchange(Queue, &Overlapped, &Client, Get, "/chunk/next", 0, NULL, HTTP_KA_OPEN,
                             &ConnectionId, "request after a response without a length");
        }
        closesocket(Client.Socket);
    }

    if (http_ka_connect(&Client, 59996))
    {
        ok(http_ka_send(&Client, Stream), "empty stream send failed\n");
        Result = http_ka_receive(Queue, &Overlapped, 0, Request, sizeof(RequestBuffer), 3000);
        ok(Result == NO_ERROR, "empty stream request returned %lu\n", Result);
        if (Result == ERROR_IO_PENDING)
            http_ka_cancel(Queue, &Overlapped);
        if (Result == NO_ERROR)
        {
            ok(http_ch_respond(Queue, Request->s.RequestId, HTTP_SEND_RESPONSE_FLAG_MORE_DATA, NULL,
                               NULL, NULL) == NO_ERROR, "empty stream: first send failed\n");
            ok(http_ch_send_body(Queue, Request->s.RequestId, 0, NULL) == NO_ERROR,
               "empty stream: last send failed\n");
            State = http_ch_read_all(&Client, Response, sizeof(Response), 500);
            ok(!strncmp(Response, "HTTP/1.1 200", 12), "empty stream: response %s\n", Response);
            ok(!strcmp(http_ch_body(Response), ""), "empty stream: body %s\n", http_ch_body(Response));
            ok(State == HTTP_KA_OPEN, "empty stream: connection state %d after the response\n", State);
        }
        closesocket(Client.Socket);
    }

    if (http_ka_connect(&Client, 59996))
    {
        ok(http_ka_send(&Client, Stream10), "HTTP/1.0 stream send failed\n");
        Result = http_ka_receive(Queue, &Overlapped, 0, Request, sizeof(RequestBuffer), 3000);
        ok(Result == NO_ERROR, "HTTP/1.0 stream request returned %lu\n", Result);
        if (Result == ERROR_IO_PENDING)
            http_ka_cancel(Queue, &Overlapped);
        if (Result == NO_ERROR)
        {
            ok(http_ch_respond(Queue, Request->s.RequestId, HTTP_SEND_RESPONSE_FLAG_MORE_DATA, "ab",
                               NULL, NULL) == NO_ERROR, "HTTP/1.0 stream: first send failed\n");
            ok(http_ch_send_body(Queue, Request->s.RequestId, 0, "cd") == NO_ERROR,
               "HTTP/1.0 stream: last send failed\n");
            State = http_ch_read_all(&Client, Response, sizeof(Response), 1000);
            ok(strstr(Response, "\r\nTransfer-Encoding:") == NULL,
               "HTTP/1.0 stream: Transfer-Encoding in %s\n", Response);
            ok(strstr(Response, "\r\nConnection: close\r\n") != NULL,
               "HTTP/1.0 stream: no Connection: close in %s\n", Response);
            ok(!strcmp(http_ch_body(Response), "abcd"), "HTTP/1.0 stream: body %s\n",
               http_ch_body(Response));
            ok(State == HTTP_KA_CLOSED, "HTTP/1.0 stream: connection state %d after the response\n", State);
        }
        closesocket(Client.Socket);
    }

    if (http_ka_connect(&Client, 59996))
    {
        ok(http_ka_send(&Client, Stream), "application chunked send failed\n");
        Result = http_ka_receive(Queue, &Overlapped, 0, Request, sizeof(RequestBuffer), 3000);
        ok(Result == NO_ERROR, "application chunked request returned %lu\n", Result);
        if (Result == ERROR_IO_PENDING)
            http_ka_cancel(Queue, &Overlapped);
        if (Result == NO_ERROR)
        {
            ok(http_ch_respond(Queue, Request->s.RequestId, HTTP_SEND_RESPONSE_FLAG_MORE_DATA,
                               "3\r\nabc\r\n", "chunked", NULL) == NO_ERROR,
               "application chunked: first send failed\n");
            ok(http_ch_send_body(Queue, Request->s.RequestId, 0, "0\r\n\r\n") == NO_ERROR,
               "application chunked: last send failed\n");
            State = http_ch_read_all(&Client, Response, sizeof(Response), 500);
            ok(strstr(Response, "\r\nTransfer-Encoding: chunked\r\n") != NULL,
               "application chunked: no Transfer-Encoding in %s\n", Response);
            ok(!strcmp(http_ch_body(Response), "3\r\nabc\r\n0\r\n\r\n"), "application chunked: body %s\n",
               http_ch_body(Response));
            ok(strstr(Response, "\r\nConnection:") == NULL, "application chunked: Connection header in %s\n",
               Response);
            ok(State == HTTP_KA_OPEN, "application chunked: connection state %d after the response\n", State);
        }
        closesocket(Client.Socket);
    }

    if (http_ka_connect(&Client, 59996))
    {
        ok(http_ka_send(&Client, Stream), "streamed length send failed\n");
        Result = http_ka_receive(Queue, &Overlapped, 0, Request, sizeof(RequestBuffer), 3000);
        ok(Result == NO_ERROR, "streamed length request returned %lu\n", Result);
        if (Result == ERROR_IO_PENDING)
            http_ka_cancel(Queue, &Overlapped);
        if (Result == NO_ERROR)
        {
            ok(http_ch_respond(Queue, Request->s.RequestId, HTTP_SEND_RESPONSE_FLAG_MORE_DATA, "ab",
                               NULL, "4") == NO_ERROR, "streamed length: first send failed\n");
            ok(http_ch_send_body(Queue, Request->s.RequestId, 0, "cd") == NO_ERROR,
               "streamed length: last send failed\n");
            State = http_ch_read_all(&Client, Response, sizeof(Response), 500);
            ok(!strcmp(http_ch_body(Response), "abcd"), "streamed length: body %s\n", http_ch_body(Response));
            ok(strstr(Response, "\r\nTransfer-Encoding:") == NULL,
               "streamed length: Transfer-Encoding in %s\n", Response);
            ok(State == HTTP_KA_OPEN, "streamed length: connection state %d after the response\n", State);
        }
        closesocket(Client.Socket);
    }

    CloseHandle(Overlapped.hEvent);
    CloseHandle(BodyOverlapped.hEvent);
    HttpRemoveUrl(Queue, Url);
cleanup_queue:
    CloseHandle(Queue);
cleanup_http:
    HttpTerminate(HTTP_INITIALIZE_SERVER, NULL);
cleanup_wsa:
    WSACleanup();
}

static ULONG http_set_timeout(HTTP_SERVICE_CONFIG_TIMEOUT_KEY Key, USHORT Seconds, ULONG Length)
{
    HTTP_SERVICE_CONFIG_TIMEOUT_SET Set;

    Set.KeyDesc = Key;
    Set.ParamDesc = Seconds;
    return HttpSetServiceConfiguration(NULL, HttpServiceConfigTimeout, &Set, Length, NULL);
}

static ULONG http_delete_timeout(HTTP_SERVICE_CONFIG_TIMEOUT_KEY Key)
{
    HTTP_SERVICE_CONFIG_TIMEOUT_SET Set;

    Set.KeyDesc = Key;
    Set.ParamDesc = 0;
    return HttpDeleteServiceConfiguration(NULL, HttpServiceConfigTimeout, &Set, sizeof(Set), NULL);
}

static void http_check_timeout(HTTP_SERVICE_CONFIG_TIMEOUT_KEY Key, USHORT Expected)
{
    HTTP_SERVICE_CONFIG_TIMEOUT_PARAM Seconds = 0;
    ULONG Returned = 0, Result;

    Result = HttpQueryServiceConfiguration(NULL, HttpServiceConfigTimeout, &Key, sizeof(Key),
                                           &Seconds, sizeof(Seconds), &Returned, NULL);
    ok(Result == NO_ERROR, "query timeout %u failed with %lu\n", Key, Result);
    ok(Seconds == Expected, "timeout %u is %u, expected %u\n", Key, Seconds, Expected);
    ok(Returned == sizeof(Seconds), "query timeout %u returned %lu bytes\n", Key, Returned);
}

static SOCKET http_timeout_client(const char *Data)
{
    struct sockaddr_in Address;
    SOCKET Socket;

    Socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (Socket == INVALID_SOCKET)
        return Socket;

    memset(&Address, 0, sizeof(Address));
    Address.sin_family = AF_INET;
    Address.sin_port = htons(59996);
    Address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (connect(Socket, (struct sockaddr *)&Address, sizeof(Address)) == SOCKET_ERROR ||
        (Data && send(Socket, Data, (int)strlen(Data), 0) != (int)strlen(Data)))
    {
        closesocket(Socket);
        return INVALID_SOCKET;
    }
    return Socket;
}

static void http_check_reset(SOCKET Socket, DWORD Start, const char *Description)
{
    char Buffer[64];
    int Timeout = 10000, Result, Error;
    DWORD Elapsed;

    setsockopt(Socket, SOL_SOCKET, SO_RCVTIMEO, (const char *)&Timeout, sizeof(Timeout));
    Result = recv(Socket, Buffer, sizeof(Buffer), 0);
    Error = Result == SOCKET_ERROR ? WSAGetLastError() : 0;
    Elapsed = GetTickCount() - Start;
    ok(Result == SOCKET_ERROR && Error == WSAECONNRESET,
       "%s: expected a reset, recv returned %d with error %d\n", Description, Result, Error);
    ok(Elapsed >= 1500 && Elapsed <= 9000, "%s: reset after %lu ms\n", Description, Elapsed);
}

static void http_check_open(SOCKET Socket, const char *Description)
{
    char Buffer[64];
    int Timeout = 500, Result, Error;

    setsockopt(Socket, SOL_SOCKET, SO_RCVTIMEO, (const char *)&Timeout, sizeof(Timeout));
    Result = recv(Socket, Buffer, sizeof(Buffer), 0);
    Error = Result == SOCKET_ERROR ? WSAGetLastError() : 0;
    ok(Result == SOCKET_ERROR && Error == WSAETIMEDOUT,
       "%s: expected an open connection, recv returned %d with error %d\n",
       Description, Result, Error);
}

static void test_service_config_timeout(void)
{
    static const WCHAR Url[] = L"http://127.0.0.1:59996/timeout/";
    static const char PartialRequest[] =
        "GET /timeout/stalled HTTP/1.1\r\nHost: 127.0.0.1:59996\r\nX-Stall:";
    HTTPAPI_VERSION Version = HTTPAPI_VERSION_1;
    HTTP_SERVICE_CONFIG_TIMEOUT_KEY Key;
    HTTP_SERVICE_CONFIG_TIMEOUT_SET Set;
    HTTP_SERVICE_CONFIG_TIMEOUT_PARAM Seconds;
    SOCKET Partial, Idle;
    HTTP_KA_CLIENT Client;
    WSADATA WsaData;
    HANDLE Queue = NULL;
    ULONG Result, Returned;
    DWORD Start;

    Result = WSAStartup(MAKEWORD(2, 2), &WsaData);
    ok(Result == 0, "WSAStartup failed with %lu\n", Result);
    if (Result != 0)
        return;

    Result = HttpInitialize(Version, HTTP_INITIALIZE_SERVER | HTTP_INITIALIZE_CONFIG, NULL);
    ok(Result == NO_ERROR, "HttpInitialize failed with %lu\n", Result);
    if (Result != NO_ERROR)
        goto cleanup_wsa;

    http_delete_timeout(HeaderWaitTimeout);
    http_delete_timeout(IdleConnectionTimeout);
    http_check_timeout(HeaderWaitTimeout, 120);
    http_check_timeout(IdleConnectionTimeout, 120);

    Key = (HTTP_SERVICE_CONFIG_TIMEOUT_KEY)5;
    Returned = 0xdead;
    Result = HttpQueryServiceConfiguration(NULL, HttpServiceConfigTimeout, &Key, sizeof(Key),
                                           &Seconds, sizeof(Seconds), &Returned, NULL);
    ok(Result == ERROR_INVALID_PARAMETER, "query of an invalid timeout returned %lu\n", Result);
    ok(Returned == 0, "query of an invalid timeout returned %lu bytes\n", Returned);

    Key = HeaderWaitTimeout;
    Returned = 0;
    Result = HttpQueryServiceConfiguration(NULL, HttpServiceConfigTimeout, &Key, sizeof(Key),
                                           &Seconds, 1, &Returned, NULL);
    ok(Result == ERROR_MORE_DATA, "query into a 1-byte buffer returned %lu\n", Result);
    ok(Returned == sizeof(Seconds), "query into a 1-byte buffer returned %lu bytes\n", Returned);

    Result = http_set_timeout(HeaderWaitTimeout, 0, sizeof(Set));
    ok(Result == ERROR_INVALID_PARAMETER, "setting a zero timeout returned %lu\n", Result);
    Result = http_set_timeout((HTTP_SERVICE_CONFIG_TIMEOUT_KEY)5, 8, sizeof(Set));
    ok(Result == ERROR_INVALID_PARAMETER, "setting an invalid timeout returned %lu\n", Result);
    Result = http_set_timeout(HeaderWaitTimeout, 8, sizeof(Set) - 1);
    ok(Result == ERROR_INSUFFICIENT_BUFFER, "setting with a short length returned %lu\n", Result);
    Set.KeyDesc = HeaderWaitTimeout;
    Set.ParamDesc = 8;
    Result = HttpSetServiceConfiguration((HANDLE)1, HttpServiceConfigTimeout, &Set, sizeof(Set), NULL);
    ok(Result == ERROR_INVALID_PARAMETER, "setting with a handle returned %lu\n", Result);

    Result = http_set_timeout(HeaderWaitTimeout, 7, sizeof(Set));
    ok(Result == NO_ERROR, "setting HeaderWaitTimeout failed with %lu\n", Result);
    http_check_timeout(HeaderWaitTimeout, 7);
    Result = http_set_timeout(HeaderWaitTimeout, 8, sizeof(Set));
    ok(Result == NO_ERROR, "replacing HeaderWaitTimeout failed with %lu\n", Result);
    http_check_timeout(HeaderWaitTimeout, 8);
    Result = http_delete_timeout(HeaderWaitTimeout);
    ok(Result == NO_ERROR, "deleting HeaderWaitTimeout failed with %lu\n", Result);
    http_check_timeout(HeaderWaitTimeout, 120);
    Result = http_delete_timeout(HeaderWaitTimeout);
    ok(Result == ERROR_FILE_NOT_FOUND, "deleting an unset timeout returned %lu\n", Result);

    Result = HttpCreateHttpHandle(&Queue, 0);
    ok(Result == NO_ERROR, "HttpCreateHttpHandle failed with %lu\n", Result);
    if (Result != NO_ERROR)
        goto cleanup_http;
    Result = HttpAddUrl(Queue, Url, NULL);
    ok(Result == NO_ERROR, "HttpAddUrl(timeout) failed with %lu\n", Result);
    if (Result != NO_ERROR)
        goto cleanup_queue;

    Result = http_set_timeout(HeaderWaitTimeout, 2, sizeof(Set));
    ok(Result == NO_ERROR, "setting HeaderWaitTimeout to 2 failed with %lu\n", Result);
    Sleep(1500);
    Partial = http_timeout_client(PartialRequest);
    Idle = http_timeout_client(NULL);
    Start = GetTickCount();
    ok(Partial != INVALID_SOCKET && Idle != INVALID_SOCKET,
       "header wait clients failed to connect, error %d\n", WSAGetLastError());
    if (Partial != INVALID_SOCKET)
    {
        http_check_reset(Partial, Start, "partial header");
        closesocket(Partial);
    }
    if (Idle != INVALID_SOCKET)
    {
        http_check_open(Idle, "idle connection under HeaderWaitTimeout");
        closesocket(Idle);
    }
    http_delete_timeout(HeaderWaitTimeout);

    Result = http_set_timeout(IdleConnectionTimeout, 2, sizeof(Set));
    ok(Result == NO_ERROR, "setting IdleConnectionTimeout to 2 failed with %lu\n", Result);
    Sleep(1500);
    Idle = http_timeout_client(NULL);
    Partial = http_timeout_client("G");
    Start = GetTickCount();
    ok(Partial != INVALID_SOCKET && Idle != INVALID_SOCKET,
       "idle clients failed to connect, error %d\n", WSAGetLastError());
    if (Idle != INVALID_SOCKET)
    {
        http_check_reset(Idle, Start, "idle connection");
        closesocket(Idle);
    }
    if (Partial != INVALID_SOCKET)
    {
        http_check_open(Partial, "started request under IdleConnectionTimeout");
        closesocket(Partial);
    }
    if (http_ka_connect(&Client, 59996))
    {
        OVERLAPPED Overlapped = {0};

        Overlapped.hEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
        http_ka_exchange(Queue, &Overlapped, &Client,
                         "GET /timeout/kept HTTP/1.1\r\nHost: 127.0.0.1:59996\r\n\r\n",
                         "/timeout/kept", 0, NULL, HTTP_KA_OPEN, NULL, "kept-alive connection");
        http_check_reset(Client.Socket, GetTickCount() - 1000, "kept-alive connection after a response");
        closesocket(Client.Socket);
        CloseHandle(Overlapped.hEvent);
    }
    http_delete_timeout(IdleConnectionTimeout);

    HttpRemoveUrl(Queue, Url);
cleanup_queue:
    CloseHandle(Queue);
cleanup_http:
    http_delete_timeout(HeaderWaitTimeout);
    http_delete_timeout(IdleConnectionTimeout);
    HttpTerminate(HTTP_INITIALIZE_SERVER | HTTP_INITIALIZE_CONFIG, NULL);
cleanup_wsa:
    WSACleanup();
}

#define HTTP_FC_PORT 59998
#define HTTP_FC_SMALL 100
#define HTTP_FC_BIG (1024 * 1024)

typedef struct _HTTP_FC_RESULT
{
    ULONG Result;
    ULONG BytesSent;
    ULONG FallbackResult;
    ULONG TailBytes;
    int Received;
    int State;
    const char *Body;
    int BodyLength;
    int ContentLength;
} HTTP_FC_RESULT;

static char *http_fc_data;
static WCHAR http_fc_small[MAX_PATH], http_fc_big[MAX_PATH], http_fc_empty[MAX_PATH];

static char http_fc_byte(ULONGLONG Offset)
{
    return (char)('a' + Offset % 26);
}

static BOOL http_fc_create(WCHAR *Path, const WCHAR *Name, ULONG Size)
{
    HANDLE File;
    char *Buffer;
    DWORD Written = 0;
    ULONG i;

    GetTempPathW(MAX_PATH, Path);
    wcscat(Path, Name);
    Buffer = malloc(Size + 1);
    if (!Buffer)
        return FALSE;
    for (i = 0; i < Size; ++i)
        Buffer[i] = http_fc_byte(i);
    File = CreateFileW(Path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (File != INVALID_HANDLE_VALUE)
    {
        WriteFile(File, Buffer, Size, &Written, NULL);
        CloseHandle(File);
    }
    free(Buffer);
    return File != INVALID_HANDLE_VALUE && Written == Size;
}

static HANDLE http_fc_open(const WCHAR *Path, DWORD Access, DWORD Flags)
{
    return CreateFileW(Path, Access, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL,
                       OPEN_EXISTING, Flags, NULL);
}

static void http_fc_file_chunk(HTTP_DATA_CHUNK *Chunk, HANDLE File, ULONGLONG Offset, ULONGLONG Length)
{
    Chunk->DataChunkType = HttpDataChunkFromFileHandle;
    Chunk->FromFileHandle.ByteRange.StartingOffset.QuadPart = Offset;
    Chunk->FromFileHandle.ByteRange.Length.QuadPart = Length;
    Chunk->FromFileHandle.FileHandle = File;
}

static void http_fc_memory_chunk(HTTP_DATA_CHUNK *Chunk, const char *Data)
{
    Chunk->DataChunkType = HttpDataChunkFromMemory;
    Chunk->FromMemory.pBuffer = (void *)Data;
    Chunk->FromMemory.BufferLength = (ULONG)strlen(Data);
}

typedef struct _HTTP_FC_READER
{
    SOCKET Socket;
    int Received;
    int State;
} HTTP_FC_READER;

static DWORD WINAPI http_fc_collect(void *Parameter)
{
    HTTP_FC_READER *Reader = Parameter;
    int TimeoutMs = 2000, Result;

    setsockopt(Reader->Socket, SOL_SOCKET, SO_RCVTIMEO, (const char *)&TimeoutMs, sizeof(TimeoutMs));
    for (;;)
    {
        Result = recv(Reader->Socket, http_fc_data + Reader->Received, HTTP_FC_BIG + 4096 - Reader->Received, 0);
        if (Result > 0)
        {
            Reader->Received += Result;
            if (Reader->Received < HTTP_FC_BIG + 4096)
                continue;
            Reader->State = HTTP_KA_DATA;
        }
        else if (!Result)
            Reader->State = HTTP_KA_CLOSED;
        else
            Reader->State = WSAGetLastError() == WSAETIMEDOUT ? HTTP_KA_OPEN : HTTP_KA_RESET;
        return 0;
    }
}

static void http_fc_serve(HANDLE Queue, OVERLAPPED *Overlapped, const char *Name, ULONG Flags,
                          HTTP_DATA_CHUNK *Chunks, USHORT ChunkCount, OVERLAPPED *SendOverlapped,
                          HTTP_DATA_CHUNK *Tail, USHORT TailCount, HTTP_FC_RESULT *Out)
{
    char DECLSPEC_ALIGN(8) RequestBuffer[4096];
    HTTP_REQUEST *Request = (HTTP_REQUEST *)RequestBuffer;
    HTTP_RESPONSE Response = {0};
    HTTP_KA_CLIENT Client;
    HTTP_FC_READER Reader = {0};
    HANDLE Thread;
    char Text[256];
    const char *End, *Length;
    DWORD Bytes;

    memset(Out, 0, sizeof(*Out));
    Out->Result = Out->FallbackResult = 0xdead;
    Out->BytesSent = Out->TailBytes = 0xdeadbeef;
    Out->ContentLength = -1;
    if (!http_ka_connect(&Client, HTTP_FC_PORT))
    {
        ok(0, "%s: connect failed with %d\n", Name, WSAGetLastError());
        return;
    }
    sprintf(Text, "GET /file/%s HTTP/1.1\r\nHost: 127.0.0.1:%d\r\nConnection: close\r\n\r\n",
            Name, HTTP_FC_PORT);
    ok(http_ka_send(&Client, Text), "%s: send failed\n", Name);
    Out->Result = http_ka_receive(Queue, Overlapped, 0, Request, sizeof(RequestBuffer), 3000);
    ok(Out->Result == NO_ERROR, "%s: HttpReceiveHttpRequest returned %lu\n", Name, Out->Result);
    if (Out->Result == ERROR_IO_PENDING)
        http_ka_cancel(Queue, Overlapped);
    if (Out->Result != NO_ERROR)
    {
        closesocket(Client.Socket);
        return;
    }

    Reader.Socket = Client.Socket;
    Thread = CreateThread(NULL, 0, http_fc_collect, &Reader, 0, NULL);
    Response.s.StatusCode = 200;
    Response.s.pReason = "OK";
    Response.s.ReasonLength = 2;
    Response.s.EntityChunkCount = ChunkCount;
    Response.s.pEntityChunks = Chunks;
    Out->Result = HttpSendHttpResponse(Queue, Request->s.RequestId, Flags, &Response, NULL,
                                       &Out->BytesSent, NULL, 0, SendOverlapped, NULL);
    if (SendOverlapped && (Out->Result == ERROR_IO_PENDING || Out->Result == NO_ERROR))
    {
        Out->Result = NO_ERROR;
        if (!GetOverlappedResult(Queue, SendOverlapped, &Bytes, TRUE))
            Out->Result = GetLastError();
        Out->BytesSent = Bytes;
    }
    if (Out->Result != NO_ERROR || (Flags & HTTP_SEND_RESPONSE_FLAG_MORE_DATA))
    {
        HTTP_DATA_CHUNK Fallback;

        http_fc_memory_chunk(&Fallback, "fallback");
        if (Flags & HTTP_SEND_RESPONSE_FLAG_MORE_DATA && Out->Result == NO_ERROR)
        {
            Out->FallbackResult = HttpSendResponseEntityBody(Queue, Request->s.RequestId, 0, TailCount, Tail,
                                                             &Out->TailBytes, NULL, 0, NULL, NULL);
        }
        else
        {
            Response.s.EntityChunkCount = 1;
            Response.s.pEntityChunks = &Fallback;
            Out->FallbackResult = HttpSendHttpResponse(Queue, Request->s.RequestId, 0, &Response, NULL,
                                                       NULL, NULL, 0, NULL, NULL);
        }
    }
    WaitForSingleObject(Thread, 10000);
    CloseHandle(Thread);
    closesocket(Client.Socket);
    Out->Received = Reader.Received;
    Out->State = Reader.State;

    http_fc_data[Out->Received] = '\0';
    End = strstr(http_fc_data, "\r\n\r\n");
    if (End)
    {
        Out->Body = End + 4;
        Out->BodyLength = Out->Received - (int)(Out->Body - http_fc_data);
        Length = strstr(http_fc_data, "\r\nContent-Length: ");
        if (Length && Length < End)
            Out->ContentLength = atoi(Length + 18);
    }
}

static BOOL http_fc_body_is(const HTTP_FC_RESULT *Out, const char *Prefix, ULONGLONG Offset,
                            int FileLength, const char *Suffix)
{
    int PrefixLength = Prefix ? (int)strlen(Prefix) : 0;
    int SuffixLength = Suffix ? (int)strlen(Suffix) : 0;
    int i;

    if (!Out->Body || Out->BodyLength != PrefixLength + FileLength + SuffixLength)
        return FALSE;
    if (PrefixLength && memcmp(Out->Body, Prefix, PrefixLength))
        return FALSE;
    for (i = 0; i < FileLength; ++i)
    {
        if (Out->Body[PrefixLength + i] != http_fc_byte(Offset + i))
            return FALSE;
    }
    return !SuffixLength || !memcmp(Out->Body + PrefixLength + FileLength, Suffix, SuffixLength);
}

static void http_fc_check(const HTTP_FC_RESULT *Out, const char *Name, ULONG Result,
                          int ContentLength, const char *Prefix, ULONGLONG Offset, int FileLength,
                          const char *Suffix)
{
    ok(Out->Result == Result, "%s: HttpSendHttpResponse returned %lu, expected %lu\n",
       Name, Out->Result, Result);
    ok(Out->ContentLength == ContentLength, "%s: Content-Length %d, expected %d\n",
       Name, Out->ContentLength, ContentLength);
    ok(http_fc_body_is(Out, Prefix, Offset, FileLength, Suffix), "%s: body of %d bytes [%.*s]\n",
       Name, Out->BodyLength, Out->BodyLength > 64 ? 64 : Out->BodyLength, Out->Body ? Out->Body : "");
    ok(Out->BytesSent == (ULONG)Out->Received, "%s: %lu bytes sent, %d received\n",
       Name, Out->BytesSent, Out->Received);
    ok(Out->State == HTTP_KA_CLOSED, "%s: connection state %d\n", Name, Out->State);
}

static void http_fc_check_reset(const HTTP_FC_RESULT *Out, const char *Name, ULONG Result)
{
    ok(Out->Result == Result, "%s: HttpSendHttpResponse returned %lu, expected %lu\n",
       Name, Out->Result, Result);
    ok(Out->BytesSent == 0, "%s: %lu bytes sent\n", Name, Out->BytesSent);
    ok(Out->Received == 0, "%s: %d bytes received\n", Name, Out->Received);
    ok(Out->State == HTTP_KA_RESET, "%s: connection state %d\n", Name, Out->State);
}

static void test_file_chunks(void)
{
    static const WCHAR Url[] = L"http://127.0.0.1:59998/file/";
    HTTPAPI_VERSION Version = HTTPAPI_VERSION_1;
    OVERLAPPED Overlapped = {0}, SendOverlapped = {0};
    HTTP_DATA_CHUNK Chunks[3], Tail[2];
    HTTP_FC_RESULT Out;
    LARGE_INTEGER Position, Zero = {{0}};
    WSADATA WsaData;
    HANDLE Queue = NULL, Small, Big, Empty, Async, NoRead;
    ULONG Result;

    http_fc_data = malloc(HTTP_FC_BIG + 4097);
    ok(http_fc_data != NULL, "out of memory\n");
    if (!http_fc_data)
        return;
    ok(http_fc_create(http_fc_small, L"httpfc_small.bin", HTTP_FC_SMALL) &&
       http_fc_create(http_fc_big, L"httpfc_big.bin", HTTP_FC_BIG) &&
       http_fc_create(http_fc_empty, L"httpfc_empty.bin", 0), "creating the files failed\n");

    Result = WSAStartup(MAKEWORD(2, 2), &WsaData);
    ok(Result == 0, "WSAStartup failed with %lu\n", Result);
    if (Result != 0)
        goto cleanup_files;
    Result = HttpInitialize(Version, HTTP_INITIALIZE_SERVER, NULL);
    ok(Result == NO_ERROR, "HttpInitialize failed with %lu\n", Result);
    if (Result != NO_ERROR)
        goto cleanup_wsa;
    Result = HttpCreateHttpHandle(&Queue, 0);
    ok(Result == NO_ERROR, "HttpCreateHttpHandle failed with %lu\n", Result);
    if (Result != NO_ERROR)
        goto cleanup_http;
    Result = HttpAddUrl(Queue, Url, NULL);
    ok(Result == NO_ERROR, "HttpAddUrl(file) failed with %lu\n", Result);
    if (Result != NO_ERROR)
        goto cleanup_queue;

    Overlapped.hEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
    SendOverlapped.hEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
    Small = http_fc_open(http_fc_small, GENERIC_READ, 0);
    Big = http_fc_open(http_fc_big, GENERIC_READ, 0);
    Empty = http_fc_open(http_fc_empty, GENERIC_READ, 0);
    Async = http_fc_open(http_fc_small, GENERIC_READ, FILE_FLAG_OVERLAPPED);
    NoRead = http_fc_open(http_fc_small, FILE_READ_ATTRIBUTES, 0);

    http_fc_file_chunk(&Chunks[0], Small, 0, HTTP_BYTE_RANGE_TO_EOF);
    http_fc_serve(Queue, &Overlapped, "whole", 0, Chunks, 1, NULL, NULL, 0, &Out);
    http_fc_check(&Out, "whole", NO_ERROR, HTTP_FC_SMALL, NULL, 0, HTTP_FC_SMALL, NULL);
    ok(SetFilePointerEx(Small, Zero, &Position, FILE_CURRENT) && Position.QuadPart == HTTP_FC_SMALL,
       "whole: file pointer at %I64d\n", Position.QuadPart);

    http_fc_file_chunk(&Chunks[0], Small, 10, 20);
    http_fc_serve(Queue, &Overlapped, "middle", 0, Chunks, 1, NULL, NULL, 0, &Out);
    http_fc_check(&Out, "middle", NO_ERROR, 20, NULL, 10, 20, NULL);
    ok(SetFilePointerEx(Small, Zero, &Position, FILE_CURRENT) && Position.QuadPart == 30,
       "middle: file pointer at %I64d\n", Position.QuadPart);

    http_fc_file_chunk(&Chunks[0], Small, 90, 20);
    http_fc_serve(Queue, &Overlapped, "straddle", 0, Chunks, 1, NULL, NULL, 0, &Out);
    http_fc_check_reset(&Out, "straddle", ERROR_FILE_INVALID);

    http_fc_file_chunk(&Chunks[0], Small, 200, 10);
    http_fc_serve(Queue, &Overlapped, "past", 0, Chunks, 1, NULL, NULL, 0, &Out);
    http_fc_check_reset(&Out, "past", ERROR_FILE_INVALID);

    http_fc_file_chunk(&Chunks[0], Small, 200, HTTP_BYTE_RANGE_TO_EOF);
    http_fc_serve(Queue, &Overlapped, "past-eof", 0, Chunks, 1, NULL, NULL, 0, &Out);
    http_fc_check_reset(&Out, "past-eof", ERROR_FILE_INVALID);

    http_fc_file_chunk(&Chunks[0], Small, HTTP_FC_SMALL, HTTP_BYTE_RANGE_TO_EOF);
    http_fc_serve(Queue, &Overlapped, "at-eof", 0, Chunks, 1, NULL, NULL, 0, &Out);
    http_fc_check(&Out, "at-eof", NO_ERROR, 0, NULL, 0, 0, NULL);

    http_fc_file_chunk(&Chunks[0], Small, 5, 0);
    http_fc_serve(Queue, &Overlapped, "zero", 0, Chunks, 1, NULL, NULL, 0, &Out);
    http_fc_check(&Out, "zero", NO_ERROR, 0, NULL, 0, 0, NULL);

    http_fc_file_chunk(&Chunks[0], Empty, 0, HTTP_BYTE_RANGE_TO_EOF);
    http_fc_serve(Queue, &Overlapped, "empty", 0, Chunks, 1, NULL, NULL, 0, &Out);
    http_fc_check(&Out, "empty", NO_ERROR, 0, NULL, 0, 0, NULL);

    http_fc_memory_chunk(&Chunks[0], "head:");
    http_fc_file_chunk(&Chunks[1], Small, 30, 10);
    http_fc_memory_chunk(&Chunks[2], ":tail");
    http_fc_serve(Queue, &Overlapped, "mixed", 0, Chunks, 3, NULL, NULL, 0, &Out);
    http_fc_check(&Out, "mixed", NO_ERROR, 20, "head:", 30, 10, ":tail");

    http_fc_file_chunk(&Chunks[0], Big, 0, HTTP_BYTE_RANGE_TO_EOF);
    http_fc_serve(Queue, &Overlapped, "big", 0, Chunks, 1, NULL, NULL, 0, &Out);
    http_fc_check(&Out, "big", NO_ERROR, HTTP_FC_BIG, NULL, 0, HTTP_FC_BIG, NULL);

    http_fc_file_chunk(&Chunks[0], Async, 0, HTTP_BYTE_RANGE_TO_EOF);
    http_fc_serve(Queue, &Overlapped, "async-file", 0, Chunks, 1, NULL, NULL, 0, &Out);
    http_fc_check(&Out, "async-file", NO_ERROR, HTTP_FC_SMALL, NULL, 0, HTTP_FC_SMALL, NULL);

    http_fc_file_chunk(&Chunks[0], Small, 0, 50);
    http_fc_serve(Queue, &Overlapped, "async-send", 0, Chunks, 1, &SendOverlapped, NULL, 0, &Out);
    http_fc_check(&Out, "async-send", NO_ERROR, 50, NULL, 0, 50, NULL);

    http_fc_file_chunk(&Chunks[0], INVALID_HANDLE_VALUE, 0, 10);
    http_fc_serve(Queue, &Overlapped, "invalid", 0, Chunks, 1, NULL, NULL, 0, &Out);
    http_fc_check_reset(&Out, "invalid", ERROR_INVALID_HANDLE);

    http_fc_file_chunk(&Chunks[0], NULL, 0, 10);
    http_fc_serve(Queue, &Overlapped, "null", 0, Chunks, 1, NULL, NULL, 0, &Out);
    http_fc_check_reset(&Out, "null", ERROR_INVALID_HANDLE);

    http_fc_file_chunk(&Chunks[0], NoRead, 0, 10);
    http_fc_serve(Queue, &Overlapped, "no-read", 0, Chunks, 1, NULL, NULL, 0, &Out);
    http_fc_check_reset(&Out, "no-read", ERROR_ACCESS_DENIED);

    http_fc_memory_chunk(&Chunks[0], "head:");
    http_fc_file_chunk(&Tail[0], Small, 40, 10);
    http_fc_memory_chunk(&Tail[1], ":tail");
    http_fc_serve(Queue, &Overlapped, "body", HTTP_SEND_RESPONSE_FLAG_MORE_DATA, Chunks, 1, NULL, Tail, 2, &Out);
    ok(Out.Result == NO_ERROR, "body: HttpSendHttpResponse returned %lu\n", Out.Result);
    ok(Out.FallbackResult == NO_ERROR, "body: HttpSendResponseEntityBody returned %lu\n", Out.FallbackResult);
    ok(Out.TailBytes == 15, "body: HttpSendResponseEntityBody sent %lu bytes\n", Out.TailBytes);
    ok(Out.ContentLength == -1, "body: Content-Length %d\n", Out.ContentLength);
    ok(http_fc_body_is(&Out, "head:", 40, 10, ":tail"), "body: body of %d bytes\n", Out.BodyLength);

    http_fc_file_chunk(&Tail[0], Small, 200, 10);
    http_fc_serve(Queue, &Overlapped, "body-past", HTTP_SEND_RESPONSE_FLAG_MORE_DATA, Chunks, 1, NULL, Tail, 1, &Out);
    ok(Out.Result == NO_ERROR, "body-past: HttpSendHttpResponse returned %lu\n", Out.Result);
    ok(Out.FallbackResult == ERROR_FILE_INVALID, "body-past: HttpSendResponseEntityBody returned %lu\n",
       Out.FallbackResult);
    ok(Out.TailBytes == 0, "body-past: HttpSendResponseEntityBody sent %lu bytes\n", Out.TailBytes);
    ok(Out.State == HTTP_KA_RESET, "body-past: connection state %d\n", Out.State);

    if (Small != INVALID_HANDLE_VALUE)
        CloseHandle(Small);
    if (Big != INVALID_HANDLE_VALUE)
        CloseHandle(Big);
    if (Empty != INVALID_HANDLE_VALUE)
        CloseHandle(Empty);
    if (Async != INVALID_HANDLE_VALUE)
        CloseHandle(Async);
    if (NoRead != INVALID_HANDLE_VALUE)
        CloseHandle(NoRead);
    CloseHandle(Overlapped.hEvent);
    CloseHandle(SendOverlapped.hEvent);
    HttpRemoveUrl(Queue, Url);
cleanup_queue:
    CloseHandle(Queue);
cleanup_http:
    HttpTerminate(HTTP_INITIALIZE_SERVER, NULL);
cleanup_wsa:
    WSACleanup();
cleanup_files:
    DeleteFileW(http_fc_small);
    DeleteFileW(http_fc_big);
    DeleteFileW(http_fc_empty);
    free(http_fc_data);
}

#define HTTP_PR_PORT 59989

static const char http_pr_get[] = "GET /prop/a HTTP/1.1\r\nHost: 127.0.0.1:59989\r\n\r\n";
static const char http_pr_post[] =
    "POST /prop/b HTTP/1.1\r\nHost: 127.0.0.1:59989\r\nContent-Length: 10\r\n\r\nhello";

static ULONG http_pr_query(HANDLE Queue, HTTP_SERVER_PROPERTY Property, ULONG *Value)
{
    ULONG Length;

    *Value = 0xdeadbeef;
    return HttpQueryRequestQueueProperty(Queue, Property, Value, sizeof(*Value), 0, &Length, NULL);
}

static ULONG http_pr_set(HANDLE Queue, HTTP_SERVER_PROPERTY Property, ULONG Value)
{
    return HttpSetRequestQueueProperty(Queue, Property, &Value, sizeof(Value), 0, NULL);
}

static BOOL http_pr_client(HTTP_KA_CLIENT *Client, const char *Text)
{
    if (!http_ka_connect(Client, HTTP_PR_PORT))
        return FALSE;
    if (Text && !http_ka_send(Client, Text))
    {
        closesocket(Client->Socket);
        return FALSE;
    }
    return TRUE;
}

static int http_pr_try(const char *Text, char *Response, int Size, int TimeoutMs)
{
    HTTP_KA_CLIENT Client;
    int State;

    Response[0] = '\0';
    if (!http_pr_client(&Client, Text))
        return -1;
    State = http_ch_read_all(&Client, Response, Size, TimeoutMs);
    closesocket(Client.Socket);
    return State;
}

static int http_pr_wait_end(HTTP_KA_CLIENT *Client, int TimeoutMs, DWORD *Elapsed)
{
    DWORD Start = GetTickCount();
    int State = http_ka_wait(Client, TimeoutMs);

    *Elapsed = GetTickCount() - Start;
    return State;
}

static ULONG http_pr_wait(HANDLE Queue, OVERLAPPED *Overlapped, DWORD TimeoutMs)
{
    DWORD Bytes;

    if (WaitForSingleObject(Overlapped->hEvent, TimeoutMs) != WAIT_OBJECT_0)
        return ERROR_IO_PENDING;
    return GetOverlappedResult(Queue, Overlapped, &Bytes, FALSE) ? NO_ERROR : GetLastError();
}

static ULONG http_pr_wait_disconnect(HANDLE Queue, HTTP_CONNECTION_ID Id, BOOL Ex, ULONG Reserved,
                                     OVERLAPPED *Overlapped)
{
    ResetEvent(Overlapped->hEvent);
    if (Ex)
        return HttpWaitForDisconnectEx(Queue, Id, Reserved, Overlapped);
    return HttpWaitForDisconnect(Queue, Id, Overlapped);
}

static void test_queue_property_validation(HANDLE Queue, HANDLE V1Queue)
{
    static const HTTP_SERVER_PROPERTY Properties[] =
        { HttpServerQueueLengthProperty, HttpServerStateProperty, HttpServer503VerbosityProperty };
    static const ULONG Defaults[] = { 1000, HttpEnabledStateActive, Http503ResponseVerbosityBasic };
    static const HTTP_SERVER_PROPERTY Unsupported[] =
        { HttpServerAuthenticationProperty, HttpServerLoggingProperty, HttpServerQosProperty,
          HttpServerTimeoutsProperty, HttpServerBindingProperty, HttpServerExtendedAuthenticationProperty,
          HttpServerChannelBindProperty, HttpServerProtectionLevelProperty, (HTTP_SERVER_PROPERTY)100 };
    ULONG Value[2], Length, Result, i;
    HANDLE Event = CreateEventW(NULL, TRUE, FALSE, NULL);

    for (i = 0; i < ARRAYSIZE(Properties); i++)
    {
        Value[0] = 0xdeadbeef;
        Length = 0xdeadbeef;
        Result = HttpQueryRequestQueueProperty(Queue, Properties[i], Value, sizeof(ULONG), 0, &Length, NULL);
        ok(Result == NO_ERROR && Value[0] == Defaults[i] && Length == sizeof(ULONG),
           "property %u: query returned %lu, value %lu, length %lu\n", Properties[i], Result, Value[0], Length);

        Value[0] = 0xdeadbeef;
        Length = 0xdeadbeef;
        Result = HttpQueryRequestQueueProperty(Queue, Properties[i], Value, sizeof(Value), 0, &Length, NULL);
        ok(Result == NO_ERROR && Value[0] == Defaults[i] && Length == sizeof(ULONG),
           "property %u: query with 8 bytes returned %lu, value %lu, length %lu\n",
           Properties[i], Result, Value[0], Length);

        Length = 0xdeadbeef;
        Result = HttpQueryRequestQueueProperty(Queue, Properties[i], Value, sizeof(ULONG) - 1, 0, &Length, NULL);
        ok(Result == ERROR_MORE_DATA && Length == sizeof(ULONG),
           "property %u: short query returned %lu, length %lu\n", Properties[i], Result, Length);

        Length = 0xdeadbeef;
        Result = HttpQueryRequestQueueProperty(Queue, Properties[i], NULL, 0, 0, &Length, NULL);
        ok(Result == ERROR_MORE_DATA && Length == sizeof(ULONG),
           "property %u: NULL query returned %lu, length %lu\n", Properties[i], Result, Length);

        Length = 0xdeadbeef;
        Result = HttpQueryRequestQueueProperty(Queue, Properties[i], NULL, sizeof(ULONG), 0, &Length, NULL);
        ok(Result == ERROR_NOACCESS, "property %u: NULL buffer with length returned %lu, length %lu\n",
           Properties[i], Result, Length);

        Value[0] = 0xdeadbeef;
        Result = HttpQueryRequestQueueProperty(Queue, Properties[i], Value, sizeof(ULONG), 0, NULL, NULL);
        ok(Result == NO_ERROR && Value[0] == Defaults[i], "property %u: query without length returned %lu, value %lu\n",
           Properties[i], Result, Value[0]);

        Result = HttpQueryRequestQueueProperty(Queue, Properties[i], NULL, 0, 0, NULL, NULL);
        ok(Result == ERROR_MORE_DATA, "property %u: NULL query without length returned %lu\n", Properties[i], Result);

        Result = HttpQueryRequestQueueProperty(Queue, Properties[i], Value, sizeof(ULONG), 1, &Length, NULL);
        ok(Result == ERROR_INVALID_PARAMETER, "property %u: query reserved1 returned %lu\n", Properties[i], Result);
        Result = HttpQueryRequestQueueProperty(Queue, Properties[i], Value, sizeof(ULONG), 0, &Length, Value);
        ok(Result == ERROR_INVALID_PARAMETER, "property %u: query reserved2 returned %lu\n", Properties[i], Result);

        Value[0] = Defaults[i];
        Value[1] = 0xdeadbeef;
        Result = HttpSetRequestQueueProperty(Queue, Properties[i], Value, 0, 0, NULL);
        ok(Result == ERROR_INVALID_PARAMETER, "property %u: set with length 0 returned %lu\n", Properties[i], Result);
        Result = HttpSetRequestQueueProperty(Queue, Properties[i], NULL, sizeof(ULONG), 0, NULL);
        ok(Result == ERROR_INVALID_PARAMETER, "property %u: set NULL returned %lu\n", Properties[i], Result);
        Result = HttpSetRequestQueueProperty(Queue, Properties[i], Value, sizeof(ULONG) - 1, 0, NULL);
        ok(Result == ERROR_INSUFFICIENT_BUFFER, "property %u: short set returned %lu\n", Properties[i], Result);
        Result = HttpSetRequestQueueProperty(Queue, Properties[i], Value, sizeof(Value), 0, NULL);
        ok(Result == NO_ERROR, "property %u: set with 8 bytes returned %lu\n", Properties[i], Result);
        Result = HttpSetRequestQueueProperty(Queue, Properties[i], Value, sizeof(ULONG), 1, NULL);
        ok(Result == ERROR_INVALID_PARAMETER, "property %u: set reserved1 returned %lu\n", Properties[i], Result);
        Result = HttpSetRequestQueueProperty(Queue, Properties[i], Value, sizeof(ULONG), 0, Value);
        ok(Result == ERROR_INVALID_PARAMETER, "property %u: set reserved2 returned %lu\n", Properties[i], Result);
        Result = HttpSetRequestQueueProperty(Queue, Properties[i], Value, sizeof(ULONG), 0, NULL);
        ok(Result == NO_ERROR, "property %u: set returned %lu\n", Properties[i], Result);

        Result = HttpQueryRequestQueueProperty(V1Queue, Properties[i], Value, sizeof(ULONG), 0, &Length, NULL);
        ok(Result == ERROR_NOT_SUPPORTED, "property %u: query on a v1 handle returned %lu\n", Properties[i], Result);
        Result = HttpSetRequestQueueProperty(V1Queue, Properties[i], Value, sizeof(ULONG), 0, NULL);
        ok(Result == ERROR_NOT_SUPPORTED, "property %u: set on a v1 handle returned %lu\n", Properties[i], Result);
        Result = HttpQueryRequestQueueProperty(NULL, Properties[i], Value, sizeof(ULONG), 0, &Length, NULL);
        ok(Result == ERROR_INVALID_HANDLE, "property %u: query on NULL returned %lu\n", Properties[i], Result);
        Result = HttpSetRequestQueueProperty(NULL, Properties[i], Value, sizeof(ULONG), 0, NULL);
        ok(Result == ERROR_INVALID_HANDLE, "property %u: set on NULL returned %lu\n", Properties[i], Result);
        Result = HttpQueryRequestQueueProperty(Event, Properties[i], Value, sizeof(ULONG), 0, &Length, NULL);
        ok(Result == ERROR_INVALID_HANDLE, "property %u: query on an event returned %lu\n", Properties[i], Result);
        Result = HttpSetRequestQueueProperty(Event, Properties[i], Value, sizeof(ULONG), 0, NULL);
        ok(Result == ERROR_INVALID_HANDLE, "property %u: set on an event returned %lu\n", Properties[i], Result);
    }

    for (i = 0; i < ARRAYSIZE(Unsupported); i++)
    {
        BYTE Buffer[64] = {0};

        Length = 0xdeadbeef;
        Result = HttpQueryRequestQueueProperty(Queue, Unsupported[i], Buffer, sizeof(Buffer), 0, &Length, NULL);
        ok(Result == ERROR_INVALID_PARAMETER, "property %u: query returned %lu, length %lu\n",
           Unsupported[i], Result, Length);
        Result = HttpSetRequestQueueProperty(Queue, Unsupported[i], Buffer, sizeof(Buffer), 0, NULL);
        ok(Result == ERROR_INVALID_PARAMETER, "property %u: set returned %lu\n", Unsupported[i], Result);
    }

    Result = http_pr_set(Queue, HttpServerQueueLengthProperty, 0);
    ok(Result == ERROR_NOT_SUPPORTED, "queue length 0 returned %lu\n", Result);
    Result = http_pr_set(Queue, HttpServerQueueLengthProperty, 0x80000000u);
    ok(Result == ERROR_NOT_SUPPORTED, "queue length 0x80000000 returned %lu\n", Result);
    Result = http_pr_set(Queue, HttpServerQueueLengthProperty, HTTP_MAX_SERVER_QUEUE_LENGTH);
    ok(Result == NO_ERROR, "queue length 0x7fffffff returned %lu\n", Result);
    Result = http_pr_query(Queue, HttpServerQueueLengthProperty, Value);
    ok(Result == NO_ERROR && Value[0] == HTTP_MAX_SERVER_QUEUE_LENGTH, "queue length query returned %lu, %lu\n",
       Result, Value[0]);
    Result = http_pr_set(Queue, HttpServerQueueLengthProperty, HTTP_MIN_SERVER_QUEUE_LENGTH);
    ok(Result == NO_ERROR, "queue length 1 returned %lu\n", Result);
    Result = http_pr_query(Queue, HttpServerQueueLengthProperty, Value);
    ok(Result == NO_ERROR && Value[0] == 1, "queue length query returned %lu, %lu\n", Result, Value[0]);
    Result = http_pr_set(Queue, HttpServerQueueLengthProperty, 1000);
    ok(Result == NO_ERROR, "queue length 1000 returned %lu\n", Result);

    Result = http_pr_set(Queue, HttpServer503VerbosityProperty, Http503ResponseVerbosityFull + 1);
    ok(Result == ERROR_NOT_SUPPORTED, "verbosity 3 returned %lu\n", Result);
    Result = http_pr_set(Queue, HttpServer503VerbosityProperty, Http503ResponseVerbosityFull);
    ok(Result == NO_ERROR, "verbosity full returned %lu\n", Result);
    Result = http_pr_query(Queue, HttpServer503VerbosityProperty, Value);
    ok(Result == NO_ERROR && Value[0] == Http503ResponseVerbosityFull, "verbosity query returned %lu, %lu\n",
       Result, Value[0]);
    Result = http_pr_set(Queue, HttpServer503VerbosityProperty, Http503ResponseVerbosityBasic);
    ok(Result == NO_ERROR, "verbosity basic returned %lu\n", Result);

    Result = http_pr_set(Queue, HttpServerStateProperty, HttpEnabledStateInactive + 1);
    ok(Result == NO_ERROR, "state 2 returned %lu\n", Result);
    Result = http_pr_query(Queue, HttpServerStateProperty, Value);
    ok(Result == NO_ERROR && Value[0] == 2, "state query after 2 returned %lu, %lu\n", Result, Value[0]);
    for (i = 3; i <= 5; i++)
    {
        Result = http_pr_set(Queue, HttpServerStateProperty, i);
        ok(Result == NO_ERROR, "state %lu returned %lu\n", i, Result);
    }
    Result = http_pr_set(Queue, HttpServerStateProperty, 6);
    ok(Result == ERROR_NOT_SUPPORTED, "state 6 returned %lu\n", Result);
    Result = http_pr_set(Queue, HttpServerStateProperty, 0xffffffff);
    ok(Result == ERROR_NOT_SUPPORTED, "state 0xffffffff returned %lu\n", Result);
    Result = http_pr_set(Queue, HttpServer503VerbosityProperty, 0xffffffff);
    ok(Result == ERROR_NOT_SUPPORTED, "verbosity 0xffffffff returned %lu\n", Result);
    Result = http_pr_query(Queue, HttpServerStateProperty, Value);
    ok(Result == NO_ERROR && Value[0] == 5, "state query after 6 returned %lu, %lu\n", Result, Value[0]);
    Result = http_pr_set(Queue, HttpServerStateProperty, HttpEnabledStateInactive);
    ok(Result == NO_ERROR, "state inactive returned %lu\n", Result);
    Result = http_pr_query(Queue, HttpServerStateProperty, Value);
    ok(Result == NO_ERROR && Value[0] == HttpEnabledStateInactive, "state query returned %lu, %lu\n",
       Result, Value[0]);
    Result = http_pr_set(Queue, HttpServerStateProperty, HttpEnabledStateActive);
    ok(Result == NO_ERROR, "state active returned %lu\n", Result);

    CloseHandle(Event);
}

typedef ULONG (WINAPI *HTTP_PR_SET)(HTTP_URL_GROUP_ID, HTTP_SERVER_PROPERTY, void *, ULONG);
typedef ULONG (WINAPI *HTTP_PR_QUERY)(HTTP_URL_GROUP_ID, HTTP_SERVER_PROPERTY, void *, ULONG, ULONG *);

static void test_object_property_validation(const char *Name, HTTP_URL_GROUP_ID Id, HTTP_PR_SET Set,
                                            HTTP_PR_QUERY Query, ULONG ConnectionLimitResult)
{
    static const HTTP_SERVER_PROPERTY Unsupported[] =
        { HttpServerQueueLengthProperty, HttpServer503VerbosityProperty, (HTTP_SERVER_PROPERTY)100 };
    HTTP_TIMEOUT_LIMIT_INFO Timeouts, Got;
    HTTP_STATE_INFO State, GotState;
    HTTP_QOS_SETTING_INFO Qos;
    HTTP_CONNECTION_LIMIT_INFO Connections;
    HTTP_BANDWIDTH_LIMIT_INFO Bandwidth;
    ULONG Length, Result, i;
    BYTE Buffer[64];

    ok(sizeof(HTTP_TIMEOUT_LIMIT_INFO) == 20, "sizeof(HTTP_TIMEOUT_LIMIT_INFO) is %u\n",
       (UINT)sizeof(HTTP_TIMEOUT_LIMIT_INFO));

    memset(&Got, 0xcc, sizeof(Got));
    Length = 0xdeadbeef;
    Result = Query(Id, HttpServerTimeoutsProperty, &Got, sizeof(Got), &Length);
    ok(Result == NO_ERROR && Length == sizeof(Got), "%s: default timeouts query returned %lu, length %lu\n",
       Name, Result, Length);
    ok(Got.Flags.Present == 0 && Got.EntityBody == 0 && Got.DrainEntityBody == 0 && Got.RequestQueue == 0 &&
       Got.IdleConnection == 0 && Got.HeaderWait == 0 && Got.MinSendRate == 0,
       "%s: default timeouts %u %u %u %u %u %u %lu\n", Name, Got.Flags.Present, Got.EntityBody,
       Got.DrainEntityBody, Got.RequestQueue, Got.IdleConnection, Got.HeaderWait, Got.MinSendRate);
    Length = 0xdeadbeef;
    Result = Query(Id, HttpServerTimeoutsProperty, &Got, sizeof(Got) - 1, &Length);
    ok(Result == ERROR_MORE_DATA && Length == sizeof(Got), "%s: short timeouts query returned %lu, length %lu\n",
       Name, Result, Length);
    Length = 0xdeadbeef;
    Result = Query(Id, HttpServerTimeoutsProperty, NULL, 0, &Length);
    ok(Result == ERROR_MORE_DATA && Length == sizeof(Got), "%s: NULL timeouts query returned %lu, length %lu\n",
       Name, Result, Length);
    Result = Query(Id, HttpServerTimeoutsProperty, NULL, sizeof(Got), &Length);
    ok(Result == ERROR_NOACCESS, "%s: NULL timeouts query with length returned %lu\n", Name, Result);
    Result = Query(Id, HttpServerTimeoutsProperty, &Got, sizeof(Got), NULL);
    ok(Result == NO_ERROR, "%s: timeouts query without length returned %lu\n", Name, Result);

    memset(&Timeouts, 0, sizeof(Timeouts));
    Timeouts.Flags.Present = 1;
    Timeouts.EntityBody = 10;
    Timeouts.DrainEntityBody = 11;
    Timeouts.RequestQueue = 12;
    Timeouts.IdleConnection = 13;
    Timeouts.HeaderWait = 14;
    Timeouts.MinSendRate = 300;
    Result = Set(Id, HttpServerTimeoutsProperty, &Timeouts, sizeof(Timeouts) - 1);
    ok(Result == ERROR_INSUFFICIENT_BUFFER, "%s: short timeouts set returned %lu\n", Name, Result);
    Result = Set(Id, HttpServerTimeoutsProperty, NULL, sizeof(Timeouts));
    ok(Result == ERROR_INVALID_PARAMETER, "%s: NULL timeouts set returned %lu\n", Name, Result);
    Result = Set(Id, HttpServerTimeoutsProperty, &Timeouts, sizeof(Timeouts));
    ok(Result == NO_ERROR, "%s: timeouts set returned %lu\n", Name, Result);
    memset(&Got, 0xcc, sizeof(Got));
    Result = Query(Id, HttpServerTimeoutsProperty, &Got, sizeof(Got), &Length);
    ok(Result == NO_ERROR && !memcmp(&Got, &Timeouts, sizeof(Got)),
       "%s: timeouts query returned %lu: %u %u %u %u %u %u %lu\n", Name, Result, Got.Flags.Present,
       Got.EntityBody, Got.DrainEntityBody, Got.RequestQueue, Got.IdleConnection, Got.HeaderWait,
       Got.MinSendRate);

    Timeouts.MinSendRate = 100;
    Result = Set(Id, HttpServerTimeoutsProperty, &Timeouts, sizeof(Timeouts));
    ok(Result == NO_ERROR, "%s: MinSendRate 100 returned %lu\n", Name, Result);
    Timeouts.MinSendRate = HTTP_LIMIT_INFINITE;
    Timeouts.IdleConnection = 0;
    Result = Set(Id, HttpServerTimeoutsProperty, &Timeouts, sizeof(Timeouts));
    ok(Result == NO_ERROR, "%s: zero idle timeout returned %lu\n", Name, Result);
    memset(&Got, 0xcc, sizeof(Got));
    Result = Query(Id, HttpServerTimeoutsProperty, &Got, sizeof(Got), &Length);
    ok(Result == NO_ERROR && Got.IdleConnection == 0 && Got.MinSendRate == HTTP_LIMIT_INFINITE && Got.HeaderWait == 14,
       "%s: timeouts query returned %lu: %u %u %u %u %u %u %lu\n", Name, Result, Got.Flags.Present,
       Got.EntityBody, Got.DrainEntityBody, Got.RequestQueue, Got.IdleConnection, Got.HeaderWait,
       Got.MinSendRate);
    *(ULONG *)&Timeouts.Flags = 2;
    Result = Set(Id, HttpServerTimeoutsProperty, &Timeouts, sizeof(Timeouts));
    ok(Result == NO_ERROR, "%s: flags 2 returned %lu\n", Name, Result);
    memset(&Got, 0xcc, sizeof(Got));
    Result = Query(Id, HttpServerTimeoutsProperty, &Got, sizeof(Got), &Length);
    ok(Result == NO_ERROR && *(ULONG *)&Got.Flags == 0 && Got.HeaderWait == 0 && Got.MinSendRate == 0,
       "%s: query after flags 2 returned %lu: %#lx %u %u %u %u %u %lu\n", Name, Result, *(ULONG *)&Got.Flags,
       Got.EntityBody, Got.DrainEntityBody, Got.RequestQueue, Got.IdleConnection, Got.HeaderWait,
       Got.MinSendRate);
    Timeouts.MinSendRate = 100;
    *(ULONG *)&Timeouts.Flags = 1;
    Result = Set(Id, HttpServerTimeoutsProperty, &Timeouts, sizeof(Timeouts));
    memset(&Got, 0xcc, sizeof(Got));
    Result = Query(Id, HttpServerTimeoutsProperty, &Got, sizeof(Got), &Length);
    ok(Result == NO_ERROR && Got.MinSendRate == 100, "%s: query after MinSendRate 100 returned %lu, %lu\n",
       Name, Result, Got.MinSendRate);
    Timeouts.Flags.Present = 0;
    Result = Set(Id, HttpServerTimeoutsProperty, &Timeouts, sizeof(Timeouts));
    ok(Result == NO_ERROR, "%s: timeouts not present returned %lu\n", Name, Result);
    memset(&Got, 0xcc, sizeof(Got));
    Result = Query(Id, HttpServerTimeoutsProperty, &Got, sizeof(Got), &Length);
    ok(Result == NO_ERROR && Got.Flags.Present == 0 && Got.HeaderWait == 0,
       "%s: cleared timeouts query returned %lu: %u %u %u %u %u %u %lu\n", Name, Result, Got.Flags.Present,
       Got.EntityBody, Got.DrainEntityBody, Got.RequestQueue, Got.IdleConnection, Got.HeaderWait,
       Got.MinSendRate);

    memset(&GotState, 0xcc, sizeof(GotState));
    Length = 0xdeadbeef;
    Result = Query(Id, HttpServerStateProperty, &GotState, sizeof(GotState), &Length);
    ok(Result == NO_ERROR && Length == sizeof(GotState) && GotState.Flags.Present == 1 &&
       GotState.State == HttpEnabledStateActive, "%s: default state query returned %lu, length %lu, %u %u\n",
       Name, Result, Length, GotState.Flags.Present, GotState.State);
    Length = 0xdeadbeef;
    Result = Query(Id, HttpServerStateProperty, &GotState, sizeof(GotState) - 1, &Length);
    ok(Result == ERROR_MORE_DATA && Length == sizeof(GotState), "%s: short state query returned %lu, length %lu\n",
       Name, Result, Length);
    State.Flags.Present = 1;
    State.State = HttpEnabledStateInactive + 1;
    Result = Set(Id, HttpServerStateProperty, &State, sizeof(State));
    ok(Result == ERROR_INVALID_PARAMETER, "%s: state 2 returned %lu\n", Name, Result);
    State.State = HttpEnabledStateInactive;
    Result = Set(Id, HttpServerStateProperty, &State, sizeof(State) - 1);
    ok(Result == ERROR_INSUFFICIENT_BUFFER, "%s: short state set returned %lu\n", Name, Result);
    Result = Set(Id, HttpServerStateProperty, &State, sizeof(State));
    ok(Result == NO_ERROR, "%s: inactive returned %lu\n", Name, Result);
    memset(&GotState, 0xcc, sizeof(GotState));
    Result = Query(Id, HttpServerStateProperty, &GotState, sizeof(GotState), &Length);
    ok(Result == NO_ERROR && GotState.Flags.Present == 1 && GotState.State == HttpEnabledStateInactive,
       "%s: state query returned %lu, %u %u\n", Name, Result, GotState.Flags.Present, GotState.State);
    State.Flags.Present = 0;
    Result = Set(Id, HttpServerStateProperty, &State, sizeof(State));
    ok(Result == NO_ERROR, "%s: state not present returned %lu\n", Name, Result);
    memset(&GotState, 0xcc, sizeof(GotState));
    Result = Query(Id, HttpServerStateProperty, &GotState, sizeof(GotState), &Length);
    ok(Result == NO_ERROR && GotState.Flags.Present == 1 && GotState.State == HttpEnabledStateInactive,
       "%s: state query after not present returned %lu, %u %u\n", Name, Result, GotState.Flags.Present,
       GotState.State);
    State.Flags.Present = 1;
    State.State = HttpEnabledStateActive;
    Result = Set(Id, HttpServerStateProperty, &State, sizeof(State));
    ok(Result == NO_ERROR, "%s: active returned %lu\n", Name, Result);
    memset(&GotState, 0xcc, sizeof(GotState));
    Result = Query(Id, HttpServerStateProperty, &GotState, sizeof(GotState), &Length);
    ok(Result == NO_ERROR && GotState.Flags.Present == 1 && GotState.State == HttpEnabledStateActive,
       "%s: final state query returned %lu, %u %u\n", Name, Result, GotState.Flags.Present, GotState.State);

    Connections.Flags.Present = 1;
    Connections.MaxConnections = 10;
    Qos.QosType = HttpQosSettingTypeConnectionLimit;
    Qos.QosSetting = &Connections;
    Result = Set(Id, HttpServerQosProperty, &Qos, sizeof(Qos));
    ok(Result == ConnectionLimitResult, "%s: connection limit returned %lu\n", Name, Result);
    Result = Set(Id, HttpServerQosProperty, &Qos, sizeof(Qos) - 1);
    ok(Result == ERROR_INSUFFICIENT_BUFFER, "%s: short QoS returned %lu\n", Name, Result);
    Connections.MaxConnections = HTTP_LIMIT_INFINITE;
    Result = Set(Id, HttpServerQosProperty, &Qos, sizeof(Qos));
    ok(Result == ConnectionLimitResult, "%s: infinite connection limit returned %lu\n", Name, Result);
    Connections.MaxConnections = 0;
    Result = Set(Id, HttpServerQosProperty, &Qos, sizeof(Qos));
    ok(Result == ConnectionLimitResult, "%s: connection limit 0 returned %lu\n", Name, Result);
    Connections.Flags.Present = 0;
    Result = Set(Id, HttpServerQosProperty, &Qos, sizeof(Qos));
    ok(Result == ConnectionLimitResult, "%s: connection limit not present returned %lu\n", Name, Result);
    Qos.QosSetting = NULL;
    Result = Set(Id, HttpServerQosProperty, &Qos, sizeof(Qos));
    ok(Result == ERROR_NOACCESS, "%s: NULL QoS setting returned %lu\n", Name, Result);
    Qos.QosType = HttpQosSettingTypeFlowRate + 1;
    Qos.QosSetting = &Connections;
    Result = Set(Id, HttpServerQosProperty, &Qos, sizeof(Qos));
    ok(Result == ERROR_INVALID_PARAMETER, "%s: QoS type 3 returned %lu\n", Name, Result);
    Bandwidth.Flags.Present = 1;
    Bandwidth.MaxBandwidth = HTTP_LIMIT_INFINITE;
    Qos.QosType = HttpQosSettingTypeBandwidth;
    Qos.QosSetting = &Bandwidth;
    Result = Set(Id, HttpServerQosProperty, &Qos, sizeof(Qos));
    ok(Result == NO_ERROR, "%s: infinite bandwidth returned %lu\n", Name, Result);
    Bandwidth.MaxBandwidth = 100;
    Result = Set(Id, HttpServerQosProperty, &Qos, sizeof(Qos));
    ok(Result == ERROR_INVALID_PARAMETER, "%s: bandwidth 100 returned %lu\n", Name, Result);
    Bandwidth.MaxBandwidth = HTTP_LIMIT_INFINITE;
    Result = Set(Id, HttpServerQosProperty, &Qos, sizeof(Qos));
    ok(Result == NO_ERROR, "%s: infinite bandwidth returned %lu\n", Name, Result);

    Length = 0xdeadbeef;
    Result = Query(Id, HttpServerLoggingProperty, Buffer, sizeof(Buffer), &Length);
    ok(Result == ERROR_INVALID_PARAMETER, "%s: logging query returned %lu, length %lu\n", Name, Result, Length);
    for (i = 0; i < ARRAYSIZE(Unsupported); i++)
    {
        memset(Buffer, 0, sizeof(Buffer));
        Result = Query(Id, Unsupported[i], Buffer, sizeof(Buffer), &Length);
        ok(Result == ERROR_INVALID_PARAMETER, "%s: property %u query returned %lu\n", Name, Unsupported[i], Result);
        Result = Set(Id, Unsupported[i], Buffer, sizeof(Buffer));
        ok(Result == ERROR_INVALID_PARAMETER, "%s: property %u set returned %lu\n", Name, Unsupported[i], Result);
    }

    Result = Query(0, HttpServerTimeoutsProperty, &Got, sizeof(Got), &Length);
    ok(Result == ERROR_INVALID_PARAMETER, "%s: query on id 0 returned %lu\n", Name, Result);
    Result = Set(0, HttpServerTimeoutsProperty, &Timeouts, sizeof(Timeouts));
    ok(Result == ERROR_INVALID_PARAMETER, "%s: set on id 0 returned %lu\n", Name, Result);
    Result = Query(Id + 0x100000, HttpServerStateProperty, &GotState, sizeof(GotState), &Length);
    ok(Result == ERROR_INVALID_PARAMETER, "%s: query on a bad id returned %lu\n", Name, Result);
}

static void test_state_present(const char *Name, HTTP_URL_GROUP_ID Id, HTTP_PR_SET Set, HTTP_PR_QUERY Query)
{
    static const struct { ULONG Present, State, GotPresent, GotState; } Steps[] =
    {
        {0, 1, 0, 1}, {0, 0, 0, 0}, {1, 1, 1, 1}, {0, 1, 1, 1}, {0, 0, 0, 0},
        {1, 0, 0, 0}, {0, 0, 0, 0}, {2, 1, 2, 1}, {3, 0, 3, 0},
    };
    HTTP_STATE_INFO State, Got;
    ULONG Length, Result, i;

    for (i = 0; i < ARRAYSIZE(Steps); i++)
    {
        *(ULONG *)&State.Flags = Steps[i].Present;
        State.State = Steps[i].State;
        Result = Set(Id, HttpServerStateProperty, &State, sizeof(State));
        memset(&Got, 0xcc, sizeof(Got));
        Query(Id, HttpServerStateProperty, &Got, sizeof(Got), &Length);
        ok(Result == NO_ERROR && *(ULONG *)&Got.Flags == Steps[i].GotPresent && Got.State == Steps[i].GotState,
           "%s: step %lu: set returned %lu, query %#lx %u\n", Name, i, Result, *(ULONG *)&Got.Flags, Got.State);
    }
    State.Flags.Present = 1;
    State.State = HttpEnabledStateActive;
    Set(Id, HttpServerStateProperty, &State, sizeof(State));
}

static ULONG WINAPI http_pr_set_session(HTTP_URL_GROUP_ID Id, HTTP_SERVER_PROPERTY Property, void *Value, ULONG Length)
{
    return HttpSetServerSessionProperty(Id, Property, Value, Length);
}

static ULONG WINAPI http_pr_query_session(HTTP_URL_GROUP_ID Id, HTTP_SERVER_PROPERTY Property, void *Value,
                                          ULONG Length, ULONG *ReturnLength)
{
    return HttpQueryServerSessionProperty(Id, Property, Value, Length, ReturnLength);
}

static void http_pr_check_body(int State, const char *Response, const char *Heading, const char *Text,
                               const char *Description)
{
    char Body[512], Length[32];

    sprintf(Body, "<!DOCTYPE HTML PUBLIC \"-//W3C//DTD HTML 4.01//EN\"\"http://www.w3.org/TR/html4/strict.dtd\">\r\n"
                  "<HTML><HEAD><TITLE>Service Unavailable</TITLE>\r\n"
                  "<META HTTP-EQUIV=\"Content-Type\" Content=\"text/html; charset=us-ascii\"></HEAD>\r\n"
                  "<BODY><h2>%s</h2>\r\n<hr><p>HTTP Error 503. %s</p>\r\n</BODY></HTML>\r\n", Heading, Text);
    sprintf(Length, "\r\nContent-Length: %u\r\n", (UINT)strlen(Body));
    ok(State == HTTP_KA_CLOSED, "%s: state %d\n", Description, State);
    ok(!strncmp(Response, "HTTP/1.1 503 Service Unavailable\r\n", 34), "%s: response %s\n", Description, Response);
    ok(strstr(Response, "\r\nContent-Type: text/html; charset=us-ascii\r\n") != NULL &&
       strstr(Response, "\r\nConnection: close\r\n") != NULL && strstr(Response, Length) != NULL,
       "%s: headers %s\n", Description, Response);
    ok(!strcmp(http_ch_body(Response), Body), "%s: body %s\n", Description, http_ch_body(Response));
}

static void test_queue_limit_and_state(HANDLE Queue)
{
    char DECLSPEC_ALIGN(8) RequestBuffer[4096];
    HTTP_REQUEST *Request = (HTTP_REQUEST *)RequestBuffer;
    OVERLAPPED Overlapped = {0};
    HTTP_KA_CLIENT First, Second;
    char Response[1024];
    HTTP_REQUEST_ID FirstId;
    ULONG Result;
    int ClientState;

    Overlapped.hEvent = CreateEventW(NULL, TRUE, FALSE, NULL);

    ok(http_pr_client(&First, NULL), "sanity client failed\n");
    http_ka_exchange(Queue, &Overlapped, &First, http_pr_get, "/prop/a", 0, NULL, HTTP_KA_OPEN, NULL, "sanity");
    closesocket(First.Socket);

    ok(http_pr_set(Queue, HttpServerQueueLengthProperty, 1) == NO_ERROR, "queue length 1 failed\n");
    ok(http_pr_client(&First, http_pr_get), "first client failed\n");
    Sleep(300);
    ClientState = http_ka_wait(&First, 1);
    ok(ClientState == HTTP_KA_OPEN, "first client state %d, %s\n", ClientState, First.Buffer);
    ClientState = http_pr_try(http_pr_get, Response, sizeof(Response), 1500);
    ok(ClientState == HTTP_KA_RESET && !Response[0], "full queue, basic: state %d, response %s\n",
       ClientState, Response);
    ok(http_pr_set(Queue, HttpServer503VerbosityProperty, Http503ResponseVerbosityLimited) == NO_ERROR,
       "verbosity limited failed\n");
    ClientState = http_pr_try(http_pr_get, Response, sizeof(Response), 1500);
    http_pr_check_body(ClientState, Response, "Service Unavailable", "The service is unavailable.",
                       "full queue, limited");
    ok(http_pr_set(Queue, HttpServer503VerbosityProperty, Http503ResponseVerbosityFull) == NO_ERROR,
       "verbosity full failed\n");
    ClientState = http_pr_try(http_pr_get, Response, sizeof(Response), 1500);
    http_pr_check_body(ClientState, Response, "Application Request Queue Full",
                       "The application request queue is full.", "full queue, full");

    Result = http_ka_receive(Queue, &Overlapped, 0, Request, sizeof(RequestBuffer), 2000);
    ok(Result == NO_ERROR, "receiving the queued request returned %lu\n", Result);
    FirstId = Request->s.RequestId;
    ok(http_pr_client(&Second, http_pr_get), "second client failed\n");
    Result = http_ka_receive(Queue, &Overlapped, 0, Request, sizeof(RequestBuffer), 2000);
    ok(Result == NO_ERROR, "receive with one request being served returned %lu\n", Result);
    if (Result == ERROR_IO_PENDING)
        http_ka_cancel(Queue, &Overlapped);
    else if (Result == NO_ERROR)
        http_ka_respond(Queue, Request->s.RequestId, 0, "second");
    http_ka_respond(Queue, FirstId, 0, "first");
    ok(http_ka_read_response(&First, Response, sizeof(Response), 2000) && !strncmp(Response, "HTTP/1.1 200", 12),
       "first response %s\n", Response);
    closesocket(First.Socket);
    closesocket(Second.Socket);
    ok(http_pr_set(Queue, HttpServerQueueLengthProperty, 1000) == NO_ERROR, "queue length 1000 failed\n");

    ok(http_pr_set(Queue, HttpServerStateProperty, HttpEnabledStateInactive) == NO_ERROR, "inactive failed\n");
    ResetEvent(Overlapped.hEvent);
    Result = HttpReceiveHttpRequest(Queue, HTTP_NULL_ID, 0, Request, sizeof(RequestBuffer), NULL, &Overlapped);
    ok(Result == ERROR_IO_PENDING, "receive on an inactive queue returned %lu\n", Result);
    ClientState = http_pr_try(http_pr_get, Response, sizeof(Response), 1500);
    http_pr_check_body(ClientState, Response, "Application Offline", "The application is offline.",
                       "inactive queue, full");
    Result = Result == ERROR_IO_PENDING ? http_pr_wait(Queue, &Overlapped, 300) : Result;
    ok(Result == ERROR_IO_PENDING, "receive on an inactive queue completed with %lu\n", Result);
    if (Result == ERROR_IO_PENDING)
        http_ka_cancel(Queue, &Overlapped);
    ok(http_pr_set(Queue, HttpServerStateProperty, HttpEnabledStateActive) == NO_ERROR, "active failed\n");
    Result = http_ka_receive(Queue, &Overlapped, 0, Request, sizeof(RequestBuffer), 300);
    ok(Result == ERROR_IO_PENDING, "receive after reactivating returned %lu\n", Result);
    if (Result == ERROR_IO_PENDING)
        http_ka_cancel(Queue, &Overlapped);

    ok(http_pr_set(Queue, HttpServer503VerbosityProperty, Http503ResponseVerbosityBasic) == NO_ERROR,
       "verbosity basic failed\n");

    CloseHandle(Overlapped.hEvent);
}

static void test_group_session_state(HANDLE Queue, HTTP_URL_GROUP_ID Group, HTTP_SERVER_SESSION_ID Session)
{
    char DECLSPEC_ALIGN(8) RequestBuffer[4096];
    HTTP_REQUEST *Request = (HTTP_REQUEST *)RequestBuffer;
    OVERLAPPED Overlapped = {0};
    HTTP_STATE_INFO State;
    char Response[1024];
    ULONG Result;
    int ClientState;

    Overlapped.hEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
    ok(http_pr_set(Queue, HttpServer503VerbosityProperty, Http503ResponseVerbosityFull) == NO_ERROR,
       "verbosity full failed\n");
    State.Flags.Present = 1;
    State.State = HttpEnabledStateInactive;
    ok(HttpSetUrlGroupProperty(Group, HttpServerStateProperty, &State, sizeof(State)) == NO_ERROR,
       "inactive group failed\n");
    ResetEvent(Overlapped.hEvent);
    HttpReceiveHttpRequest(Queue, HTTP_NULL_ID, 0, Request, sizeof(RequestBuffer), NULL, &Overlapped);
    ClientState = http_pr_try(http_pr_get, Response, sizeof(Response), 1500);
    http_pr_check_body(ClientState, Response, "Service Unavailable", "The service is unavailable.",
                       "inactive group");
    Result = http_pr_wait(Queue, &Overlapped, 100);
    ok(Result == ERROR_IO_PENDING, "receive with an inactive group returned %lu\n", Result);
    State.State = HttpEnabledStateActive;
    HttpSetUrlGroupProperty(Group, HttpServerStateProperty, &State, sizeof(State));

    State.State = HttpEnabledStateInactive;
    ok(HttpSetServerSessionProperty(Session, HttpServerStateProperty, &State, sizeof(State)) == NO_ERROR,
       "inactive session failed\n");
    ClientState = http_pr_try(http_pr_get, Response, sizeof(Response), 1500);
    http_pr_check_body(ClientState, Response, "Service Unavailable", "The service is unavailable.",
                       "inactive session");
    Result = http_pr_wait(Queue, &Overlapped, 100);
    ok(Result == ERROR_IO_PENDING, "receive with an inactive session returned %lu\n", Result);
    State.State = HttpEnabledStateActive;
    HttpSetServerSessionProperty(Session, HttpServerStateProperty, &State, sizeof(State));
    if (Result == ERROR_IO_PENDING)
        http_ka_cancel(Queue, &Overlapped);
    ok(http_pr_set(Queue, HttpServer503VerbosityProperty, Http503ResponseVerbosityBasic) == NO_ERROR,
       "verbosity basic failed\n");
    CloseHandle(Overlapped.hEvent);
}

static void test_group_timeouts(HANDLE Queue, HTTP_URL_GROUP_ID Group, HTTP_SERVER_SESSION_ID Session)
{
    OVERLAPPED Overlapped = {0};
    HTTP_TIMEOUT_LIMIT_INFO Timeouts = {{0}};
    HTTP_KA_CLIENT Client;
    DWORD Elapsed;
    int State;

    Overlapped.hEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
    Timeouts.Flags.Present = 1;
    Timeouts.IdleConnection = 2;
    ok(HttpSetUrlGroupProperty(Group, HttpServerTimeoutsProperty, &Timeouts, sizeof(Timeouts)) == NO_ERROR,
       "group idle timeout failed\n");
    ok(http_pr_client(&Client, NULL), "client failed\n");
    http_ka_exchange(Queue, &Overlapped, &Client, http_pr_get, "/prop/a", 0, NULL, HTTP_KA_OPEN, NULL,
                     "group idle timeout");
    http_ka_exchange(Queue, &Overlapped, &Client, http_pr_get, "/prop/a", 0, NULL, HTTP_KA_OPEN, NULL,
                     "group idle timeout, second request");
    State = http_pr_wait_end(&Client, 9000, &Elapsed);
    ok(State == HTTP_KA_RESET && Elapsed >= 1000 && Elapsed <= 9000,
       "group idle timeout: state %d after %lu ms\n", State, Elapsed);
    closesocket(Client.Socket);

    Timeouts.Flags.Present = 0;
    HttpSetUrlGroupProperty(Group, HttpServerTimeoutsProperty, &Timeouts, sizeof(Timeouts));
    Timeouts.Flags.Present = 1;
    ok(HttpSetServerSessionProperty(Session, HttpServerTimeoutsProperty, &Timeouts, sizeof(Timeouts)) == NO_ERROR,
       "session idle timeout failed\n");
    ok(http_pr_client(&Client, NULL), "client failed\n");
    http_ka_exchange(Queue, &Overlapped, &Client, http_pr_get, "/prop/a", 0, NULL, HTTP_KA_OPEN, NULL,
                     "session idle timeout");
    http_ka_exchange(Queue, &Overlapped, &Client, http_pr_get, "/prop/a", 0, NULL, HTTP_KA_OPEN, NULL,
                     "session idle timeout, second request");
    State = http_pr_wait_end(&Client, 9000, &Elapsed);
    ok(State == HTTP_KA_RESET && Elapsed >= 1000 && Elapsed <= 9000,
       "session idle timeout: state %d after %lu ms\n", State, Elapsed);
    closesocket(Client.Socket);
    Timeouts.Flags.Present = 0;
    HttpSetServerSessionProperty(Session, HttpServerTimeoutsProperty, &Timeouts, sizeof(Timeouts));
    CloseHandle(Overlapped.hEvent);
}

static void test_cancel_and_disconnect(HANDLE Queue)
{
    char DECLSPEC_ALIGN(8) RequestBuffer[4096];
    HTTP_REQUEST *Request = (HTTP_REQUEST *)RequestBuffer;
    OVERLAPPED Overlapped = {0}, Wait = {0}, Body = {0}, Cancel = {0};
    HTTP_KA_CLIENT Client;
    HTTP_REQUEST_ID Id;
    HTTP_CONNECTION_ID ConnectionId;
    char Data[64], Response[1024];
    DWORD Elapsed, Bytes;
    ULONG Result;
    int State;

    Overlapped.hEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
    Wait.hEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
    Body.hEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
    Cancel.hEvent = CreateEventW(NULL, TRUE, FALSE, NULL);

    Result = HttpCancelHttpRequest(Queue, HTTP_NULL_ID, NULL);
    ok(Result == ERROR_CONNECTION_INVALID, "cancel of the null id returned %lu\n", Result);
    Result = HttpCancelHttpRequest(Queue, 0x123456789ull, NULL);
    ok(Result == ERROR_CONNECTION_INVALID, "cancel of a bad id returned %lu\n", Result);
    Result = HttpCancelHttpRequest(NULL, 0x123456789ull, NULL);
    ok(Result == ERROR_INVALID_HANDLE, "cancel on NULL returned %lu\n", Result);
    Result = http_pr_wait_disconnect(Queue, HTTP_NULL_ID, FALSE, 0, &Wait);
    ok(Result == ERROR_CONNECTION_INVALID, "wait on the null id returned %lu\n", Result);
    if (Result == ERROR_IO_PENDING)
        http_ka_cancel(Queue, &Wait);
    Result = http_pr_wait_disconnect(Queue, 0x123456789ull, FALSE, 0, &Wait);
    ok(Result == ERROR_CONNECTION_INVALID, "wait on a bad id returned %lu\n", Result);
    if (Result == ERROR_IO_PENDING)
        http_ka_cancel(Queue, &Wait);
    Result = http_pr_wait_disconnect(NULL, 0x123456789ull, FALSE, 0, &Wait);
    ok(Result == ERROR_INVALID_HANDLE, "wait on NULL returned %lu\n", Result);

    ok(http_pr_client(&Client, http_pr_post), "cancel client failed\n");
    Result = http_ka_receive(Queue, &Overlapped, 0, Request, sizeof(RequestBuffer), 2000);
    ok(Result == NO_ERROR, "receive for cancel returned %lu\n", Result);
    Id = Request->s.RequestId;
    ConnectionId = Request->s.ConnectionId;
    Result = http_pr_wait_disconnect(Queue, ConnectionId, TRUE, 1, &Wait);
    ok(Result == ERROR_INVALID_PARAMETER, "wait with reserved 1 returned %lu\n", Result);
    if (Result == ERROR_IO_PENDING)
        http_ka_cancel(Queue, &Wait);
    Result = http_pr_wait_disconnect(Queue, ConnectionId, TRUE, 0, &Wait);
    ok(Result == ERROR_IO_PENDING, "wait for the cancelled connection returned %lu\n", Result);
    ResetEvent(Body.hEvent);
    Result = HttpReceiveRequestEntityBody(Queue, Id, 0, Data, sizeof(Data), NULL, &Body);
    ok(Result == NO_ERROR || Result == ERROR_IO_PENDING, "first body read returned %lu\n", Result);
    if (Result == NO_ERROR || Result == ERROR_IO_PENDING)
        GetOverlappedResult(Queue, &Body, &Bytes, TRUE);
    ResetEvent(Body.hEvent);
    Result = HttpReceiveRequestEntityBody(Queue, Id, 0, Data, sizeof(Data), NULL, &Body);
    ok(Result == ERROR_IO_PENDING, "second body read returned %lu\n", Result);
    ResetEvent(Cancel.hEvent);
    Result = HttpCancelHttpRequest(Queue, Id, &Cancel);
    ok(Result == ERROR_IO_PENDING || Result == NO_ERROR, "overlapped cancel returned %lu\n", Result);
    if (Result == ERROR_IO_PENDING || Result == NO_ERROR)
    {
        Result = http_pr_wait(Queue, &Cancel, 2000);
        ok(Result == NO_ERROR, "overlapped cancel completed with %lu\n", Result);
    }
    Result = http_pr_wait(Queue, &Body, 2000);
    ok(Result == ERROR_OPERATION_ABORTED, "pending body read after cancel completed with %lu\n", Result);
    if (Result == ERROR_IO_PENDING)
        http_ka_cancel(Queue, &Body);
    Result = http_pr_wait(Queue, &Wait, 2000);
    ok(Result == ERROR_NETNAME_DELETED, "disconnect wait after cancel completed with %lu\n", Result);
    if (Result == ERROR_IO_PENDING)
        http_ka_cancel(Queue, &Wait);
    State = http_pr_wait_end(&Client, 2000, &Elapsed);
    ok(State == HTTP_KA_RESET, "client after cancel: state %d after %lu ms\n", State, Elapsed);
    closesocket(Client.Socket);
    Result = http_ka_respond(Queue, Id, 0, "late");
    ok(Result == ERROR_CONNECTION_INVALID, "response after cancel returned %lu\n", Result);
    Result = HttpCancelHttpRequest(Queue, Id, NULL);
    ok(Result == ERROR_CONNECTION_INVALID, "second cancel returned %lu\n", Result);
    Result = http_pr_wait_disconnect(Queue, ConnectionId, FALSE, 0, &Wait);
    ok(Result == ERROR_CONNECTION_INVALID, "wait on a closed connection returned %lu\n", Result);
    if (Result == ERROR_IO_PENDING)
        http_ka_cancel(Queue, &Wait);

    ok(http_pr_client(&Client, http_pr_get), "wait client failed\n");
    Result = http_ka_receive(Queue, &Overlapped, 0, Request, sizeof(RequestBuffer), 2000);
    ok(Result == NO_ERROR, "receive for wait returned %lu\n", Result);
    Id = Request->s.RequestId;
    ConnectionId = Request->s.ConnectionId;
    Result = http_pr_wait_disconnect(Queue, ConnectionId, FALSE, 0, &Wait);
    ok(Result == ERROR_IO_PENDING, "wait returned %lu\n", Result);
    Result = http_pr_wait_disconnect(Queue, ConnectionId, TRUE, 0, &Cancel);
    ok(Result == ERROR_IO_PENDING, "second wait returned %lu\n", Result);
    Result = http_ka_respond(Queue, Id, 0, "body");
    ok(Result == NO_ERROR, "keep-alive response returned %lu\n", Result);
    ok(http_ka_read_response(&Client, Response, sizeof(Response), 2000), "no keep-alive response\n");
    Result = http_pr_wait(Queue, &Wait, 300);
    ok(Result == ERROR_IO_PENDING, "wait after a keep-alive response completed with %lu\n", Result);
    CancelIoEx(Queue, &Cancel);
    Result = http_pr_wait(Queue, &Cancel, 1000);
    ok(Result == ERROR_OPERATION_ABORTED, "cancelled wait completed with %lu\n", Result);
    Result = http_pr_wait(Queue, &Wait, 100);
    ok(Result == ERROR_IO_PENDING, "other wait after CancelIoEx completed with %lu\n", Result);
    shutdown(Client.Socket, SD_SEND);
    Result = http_pr_wait(Queue, &Wait, 2000);
    ok(Result == NO_ERROR, "wait after the client shut down sending completed with %lu\n", Result);
    if (Result == ERROR_IO_PENDING)
    {
        closesocket(Client.Socket);
        Result = http_pr_wait(Queue, &Wait, 2000);
        ok(Result == NO_ERROR, "wait after the client closed completed with %lu\n", Result);
        if (Result == ERROR_IO_PENDING)
            http_ka_cancel(Queue, &Wait);
    }
    else
        closesocket(Client.Socket);
    GetOverlappedResult(Queue, &Wait, &Bytes, FALSE);
    ok(Bytes == 0, "wait returned %lu bytes\n", Bytes);

    ok(http_pr_client(&Client, http_pr_get), "disconnect client failed\n");
    Result = http_ka_receive(Queue, &Overlapped, 0, Request, sizeof(RequestBuffer), 2000);
    ok(Result == NO_ERROR, "receive for disconnect returned %lu\n", Result);
    Result = http_pr_wait_disconnect(Queue, Request->s.ConnectionId, FALSE, 0, &Wait);
    ok(Result == ERROR_IO_PENDING, "wait before a disconnect response returned %lu\n", Result);
    Result = http_ka_respond(Queue, Request->s.RequestId, HTTP_SEND_RESPONSE_FLAG_DISCONNECT, "bye");
    ok(Result == NO_ERROR, "disconnect response returned %lu\n", Result);
    State = http_pr_wait_end(&Client, 1000, &Elapsed);
    ok(State == HTTP_KA_DATA, "client after a disconnect response: state %d\n", State);
    Client.Length = 0;
    State = http_pr_wait_end(&Client, 1000, &Elapsed);
    ok(State == HTTP_KA_CLOSED, "client after a disconnect response body: state %d\n", State);
    Result = http_pr_wait(Queue, &Wait, 300);
    ok(Result == ERROR_IO_PENDING, "wait after a disconnect response completed with %lu\n", Result);
    closesocket(Client.Socket);
    Result = http_pr_wait(Queue, &Wait, 2000);
    ok(Result == NO_ERROR, "wait after the client closed completed with %lu\n", Result);
    if (Result == ERROR_IO_PENDING)
        http_ka_cancel(Queue, &Wait);

    CloseHandle(Overlapped.hEvent);
    CloseHandle(Wait.hEvent);
    CloseHandle(Body.hEvent);
    CloseHandle(Cancel.hEvent);
}

static void test_shutdown(HANDLE Queue, HANDLE Idle)
{
    char DECLSPEC_ALIGN(8) RequestBuffer[4096];
    HTTP_REQUEST *Request = (HTTP_REQUEST *)RequestBuffer;
    OVERLAPPED Overlapped = {0}, Wait = {0};
    HTTP_KA_CLIENT Bound, Queued, Late;
    HTTP_REQUEST_ID Id;
    char Response[1024];
    DWORD Elapsed;
    ULONG Result, Value;
    int State;

    Overlapped.hEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
    Wait.hEvent = CreateEventW(NULL, TRUE, FALSE, NULL);

    Result = HttpShutdownRequestQueue(NULL);
    ok(Result == ERROR_INVALID_HANDLE, "shutdown of NULL returned %lu\n", Result);
    ResetEvent(Overlapped.hEvent);
    Result = HttpReceiveHttpRequest(Idle, HTTP_NULL_ID, 0, Request, sizeof(RequestBuffer), NULL, &Overlapped);
    ok(Result == ERROR_IO_PENDING, "receive on the idle queue returned %lu\n", Result);
    Result = HttpShutdownRequestQueue(Idle);
    ok(Result == NO_ERROR, "shutdown returned %lu\n", Result);
    Result = http_pr_wait(Idle, &Overlapped, 1000);
    ok(Result == ERROR_OPERATION_ABORTED, "pending receive after shutdown completed with %lu\n", Result);
    if (Result == ERROR_IO_PENDING)
        http_ka_cancel(Idle, &Overlapped);
    ResetEvent(Overlapped.hEvent);
    Result = HttpReceiveHttpRequest(Idle, HTTP_NULL_ID, 0, Request, sizeof(RequestBuffer), NULL, &Overlapped);
    ok(Result == ERROR_INVALID_HANDLE, "receive after shutdown returned %lu\n", Result);
    if (Result == ERROR_IO_PENDING)
        http_ka_cancel(Idle, &Overlapped);
    Result = http_pr_query(Idle, HttpServerQueueLengthProperty, &Value);
    ok(Result == NO_ERROR, "query after shutdown returned %lu, %lu\n", Result, Value);
    Result = http_pr_set(Idle, HttpServerQueueLengthProperty, 10);
    ok(Result == NO_ERROR, "set after shutdown returned %lu\n", Result);
    Result = HttpShutdownRequestQueue(Idle);
    ok(Result == NO_ERROR, "second shutdown returned %lu\n", Result);

    ok(http_pr_client(&Bound, http_pr_get), "bound client failed\n");
    Result = http_ka_receive(Queue, &Overlapped, 0, Request, sizeof(RequestBuffer), 2000);
    ok(Result == NO_ERROR, "receive before shutdown returned %lu\n", Result);
    Id = Request->s.RequestId;
    Result = http_pr_wait_disconnect(Queue, Request->s.ConnectionId, FALSE, 0, &Wait);
    ok(Result == ERROR_IO_PENDING, "wait before shutdown returned %lu\n", Result);
    ok(http_pr_client(&Queued, http_pr_get), "queued client failed\n");
    Sleep(300);
    Result = HttpShutdownRequestQueue(Queue);
    ok(Result == NO_ERROR, "shutdown returned %lu\n", Result);
    Result = http_pr_wait(Queue, &Wait, 500);
    ok(Result == ERROR_IO_PENDING, "disconnect wait after shutdown completed with %lu\n", Result);
    State = http_pr_wait_end(&Bound, 500, &Elapsed);
    ok(State == HTTP_KA_OPEN, "bound client after shutdown: state %d after %lu ms\n", State, Elapsed);
    Result = http_ka_respond(Queue, Id, 0, "late");
    ok(Result == NO_ERROR, "response after shutdown returned %lu\n", Result);
    ok(http_ka_read_response(&Bound, Response, sizeof(Response), 1000) && !strncmp(Response, "HTTP/1.1 200", 12),
       "bound client response after shutdown %s\n", Response);
    State = http_pr_wait_end(&Bound, 500, &Elapsed);
    ok(State == HTTP_KA_OPEN, "bound client after the response: state %d after %lu ms\n", State, Elapsed);
    State = http_pr_wait_end(&Queued, 500, &Elapsed);
    ok(State == HTTP_KA_OPEN, "queued client after shutdown: state %d after %lu ms\n", State, Elapsed);
    ok(http_pr_client(&Late, http_pr_get), "late client failed\n");
    State = http_pr_wait_end(&Late, 1000, &Elapsed);
    ok(State == HTTP_KA_OPEN, "new client after shutdown: state %d after %lu ms, %s\n", State, Elapsed,
       Late.Buffer);
    Result = http_ka_receive(Queue, &Overlapped, 0, Request, sizeof(RequestBuffer), 300);
    ok(Result == ERROR_INVALID_HANDLE, "receive after shutdown returned %lu\n", Result);
    if (Result == ERROR_IO_PENDING)
        http_ka_cancel(Queue, &Overlapped);
    closesocket(Bound.Socket);
    Result = http_pr_wait(Queue, &Wait, 1000);
    ok(Result == NO_ERROR, "disconnect wait after the bound client closed completed with %lu\n", Result);
    if (Result == ERROR_IO_PENDING)
        http_ka_cancel(Queue, &Wait);

    CloseHandle(Queue);
    State = http_pr_wait_end(&Queued, 1000, &Elapsed);
    ok(State == HTTP_KA_RESET, "queued client after close: state %d after %lu ms\n", State, Elapsed);
    closesocket(Queued.Socket);
    State = http_pr_wait_end(&Late, 1000, &Elapsed);
    ok(State == HTTP_KA_RESET, "new client after close: state %d after %lu ms\n", State, Elapsed);
    closesocket(Late.Socket);

    CloseHandle(Overlapped.hEvent);
    CloseHandle(Wait.hEvent);
}

static void test_properties(void)
{
    static const WCHAR Url[] = L"http://127.0.0.1:59989/prop/";
    HTTPAPI_VERSION Version = HTTPAPI_VERSION_2;
    HTTP_SERVER_SESSION_ID Session = 0;
    HTTP_URL_GROUP_ID Group = 0;
    HTTP_BINDING_INFO Binding;
    HANDLE Queue = NULL, V1Queue = NULL, Idle = NULL;
    WSADATA WsaData;
    ULONG Result;

    Result = WSAStartup(MAKEWORD(2, 2), &WsaData);
    ok(Result == 0, "WSAStartup failed with %lu\n", Result);
    if (Result)
        return;
    Result = HttpInitialize(Version, HTTP_INITIALIZE_SERVER, NULL);
    ok(Result == NO_ERROR, "HttpInitialize failed with %lu\n", Result);
    if (Result)
        goto cleanup_wsa;
    Result = HttpCreateServerSession(Version, &Session, 0);
    ok(Result == NO_ERROR, "HttpCreateServerSession failed with %lu\n", Result);
    Result = HttpCreateUrlGroup(Session, &Group, 0);
    ok(Result == NO_ERROR, "HttpCreateUrlGroup failed with %lu\n", Result);
    Result = HttpCreateRequestQueue(Version, NULL, NULL, 0, &Queue);
    ok(Result == NO_ERROR, "HttpCreateRequestQueue failed with %lu\n", Result);
    Result = HttpCreateRequestQueue(Version, NULL, NULL, 0, &Idle);
    ok(Result == NO_ERROR, "HttpCreateRequestQueue failed with %lu\n", Result);
    Result = HttpCreateHttpHandle(&V1Queue, 0);
    ok(Result == NO_ERROR, "HttpCreateHttpHandle failed with %lu\n", Result);
    if (!Queue || !Idle || !V1Queue || !Group)
        goto cleanup;
    Binding.Flags.Present = 1;
    Binding.RequestQueueHandle = Queue;
    Result = HttpSetUrlGroupProperty(Group, HttpServerBindingProperty, &Binding, sizeof(Binding));
    ok(Result == NO_ERROR, "binding failed with %lu\n", Result);
    Result = HttpAddUrlToUrlGroup(Group, Url, 0, 0);
    ok(Result == NO_ERROR, "HttpAddUrlToUrlGroup failed with %lu\n", Result);

    test_queue_property_validation(Queue, V1Queue);
    test_object_property_validation("group", Group, HttpSetUrlGroupProperty, HttpQueryUrlGroupProperty, NO_ERROR);
    test_object_property_validation("session", Session, http_pr_set_session, http_pr_query_session,
                                    ERROR_INVALID_PARAMETER);
    {
        HTTP_SERVER_SESSION_ID FreshSession = 0;
        HTTP_URL_GROUP_ID FreshGroup = 0;

        HttpCreateServerSession(Version, &FreshSession, 0);
        HttpCreateUrlGroup(FreshSession, &FreshGroup, 0);
        test_state_present("fresh group", FreshGroup, HttpSetUrlGroupProperty, HttpQueryUrlGroupProperty);
        test_state_present("fresh session", FreshSession, http_pr_set_session, http_pr_query_session);
        HttpCloseUrlGroup(FreshGroup);
        HttpCloseServerSession(FreshSession);
    }
    test_queue_limit_and_state(Queue);
    test_group_session_state(Queue, Group, Session);
    test_group_timeouts(Queue, Group, Session);
    test_cancel_and_disconnect(Queue);
    test_shutdown(Queue, Idle);
    Queue = NULL;

cleanup:
    if (Group)
        HttpCloseUrlGroup(Group);
    if (Session)
        HttpCloseServerSession(Session);
    if (Queue)
        HttpCloseRequestQueue(Queue);
    if (Idle)
        HttpCloseRequestQueue(Idle);
    if (V1Queue)
        CloseHandle(V1Queue);
    HttpTerminate(HTTP_INITIALIZE_SERVER, NULL);
cleanup_wsa:
    WSACleanup();
}

START_TEST(httpapi)
{
    test_initialize();
    test_request_queue_v1();
    test_request_queue_v2();
    test_server_session_and_url_group();
    test_url_syntax_and_registration();
    test_receive_request_cancellation();
    test_receive_request_cancellation_stress();
    test_url_prefix_routing_precedence();
    test_loopback_http_server();
    test_stalled_connection_teardown();
    test_stalled_clients_do_not_delay_requests();
    test_keep_alive();
    test_service_config_timeout();
}

START_TEST(chunked)
{
    test_chunked();
}

START_TEST(filechunk)
{
    test_file_chunks();
}

START_TEST(properties)
{
    test_properties();
}
