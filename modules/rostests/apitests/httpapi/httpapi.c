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
    test_service_config_timeout();
}
