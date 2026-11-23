/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests Windows.Web.Http.HttpClient against a loopback HTTP server
 * COPYRIGHT:   Copyright 2026 Ostap Brehin <osbre@protonmail.com>
 */

#define COBJMACROS
#include <winsock2.h>
#include "winrt_apitest.h"

#define WIDL_using_Windows_Storage_Streams
#define WIDL_using_Windows_Web_Http
#define WIDL_using_Windows_Web_Http_Filters
#define WIDL_using_Windows_Web_Http_Headers
#include <windows.web.http.h>
#include <robuffer.h>

#define MAX_REQUESTS 16

struct server
{
    SOCKET Listen;
    USHORT Port;
    HANDLE Thread;
    CRITICAL_SECTION Lock;
    LONG Count;
    char Requests[MAX_REQUESTS][2048];
};

static struct server Server;

static const char HeadResponse[] =
    "HTTP/1.1 200 OK\r\n"
    "Content-Length: 10\r\n"
    "Content-Type: application/x-test\r\n"
    "X-Test-Version: 1.2.3\r\n"
    "x-lower: lower\r\n"
    "ETag: \"abc\"\r\n"
    "Last-Modified: Mon, 02 Nov 2026 10:00:00 GMT\r\n"
    "Connection: close\r\n"
    "\r\n";

static const char *
PickResponse(
    _In_ const char *Request,
    _Out_ const char **Body)
{
    *Body = "";
    if (!strncmp(Request, "HEAD /head ", 11))
        return HeadResponse;
    if (!strncmp(Request, "GET /head ", 10))
    {
        *Body = "0123456789";
        return HeadResponse;
    }
    if (!strncmp(Request, "GET /range ", 11))
    {
        *Body = "2345";
        return "HTTP/1.1 206 Partial Content\r\n"
               "Content-Range: bytes 2-5/10\r\n"
               "Content-Length: 4\r\n"
               "Accept-Ranges: bytes\r\n"
               "Connection: close\r\n"
               "\r\n";
    }
    if (!strncmp(Request, "HEAD /redirect ", 15) || !strncmp(Request, "GET /redirect ", 14))
        return "HTTP/1.1 302 Found\r\n"
               "Location: /head\r\n"
               "Content-Length: 0\r\n"
               "Connection: close\r\n"
               "\r\n";
    if (!strncmp(Request, "HEAD /retry ", 12))
        return "HTTP/1.1 503 Service Unavailable\r\n"
               "Retry-After: 120\r\n"
               "Content-Length: 0\r\n"
               "Connection: close\r\n"
               "\r\n";
    if (!strncmp(Request, "HEAD /dup ", 10))
        return "HTTP/1.1 200 OK\r\n"
               "X-Dup: a\r\n"
               "X-Dup: b\r\n"
               "Content-Length: 0\r\n"
               "Connection: close\r\n"
               "\r\n";
    return "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
}

static DWORD WINAPI
ServerThread(
    _In_ void *Context)
{
    for (;;)
    {
        char Request[2048];
        const char *Response, *Body;
        int Length = 0, Read;
        SOCKET Client = accept(Server.Listen, NULL, NULL);
        LONG Index;

        if (Client == INVALID_SOCKET)
            break;
        while (Length < (int)sizeof(Request) - 1)
        {
            Read = recv(Client, Request + Length, sizeof(Request) - 1 - Length, 0);
            if (Read <= 0)
                break;
            Length += Read;
            Request[Length] = 0;
            if (strstr(Request, "\r\n\r\n"))
                break;
        }
        Request[Length] = 0;
        EnterCriticalSection(&Server.Lock);
        Index = Server.Count;
        if (Index < MAX_REQUESTS)
        {
            strcpy(Server.Requests[Index], Request);
            Server.Count++;
        }
        LeaveCriticalSection(&Server.Lock);
        Response = PickResponse(Request, &Body);
        send(Client, Response, (int)strlen(Response), 0);
        if (*Body)
            send(Client, Body, (int)strlen(Body), 0);
        shutdown(Client, SD_SEND);
        while (recv(Client, Request, sizeof(Request), 0) > 0)
            ;
        closesocket(Client);
    }
    return 0;
}

static BOOL
StartServer(void)
{
    struct sockaddr_in Address = {0};
    int Size = sizeof(Address);
    WSADATA Data;

    if (WSAStartup(MAKEWORD(2, 2), &Data))
        return FALSE;
    InitializeCriticalSection(&Server.Lock);
    Server.Listen = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (Server.Listen == INVALID_SOCKET)
        return FALSE;
    Address.sin_family = AF_INET;
    Address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (bind(Server.Listen, (struct sockaddr *)&Address, sizeof(Address)) ||
        getsockname(Server.Listen, (struct sockaddr *)&Address, &Size) ||
        listen(Server.Listen, 8))
        return FALSE;
    Server.Port = ntohs(Address.sin_port);
    Server.Thread = CreateThread(NULL, 0, ServerThread, NULL, 0, NULL);
    return Server.Thread != NULL;
}

static void
StopServer(void)
{
    closesocket(Server.Listen);
    if (Server.Thread)
    {
        WaitForSingleObject(Server.Thread, 5000);
        CloseHandle(Server.Thread);
    }
    DeleteCriticalSection(&Server.Lock);
    WSACleanup();
}

static const char *
FindRequest(
    _In_ const char *Prefix)
{
    const char *Found = NULL;
    LONG i;

    EnterCriticalSection(&Server.Lock);
    for (i = 0; i < Server.Count; i++)
    {
        if (!strncmp(Server.Requests[i], Prefix, strlen(Prefix)))
            Found = Server.Requests[i];
    }
    LeaveCriticalSection(&Server.Lock);
    return Found;
}

static BOOL
RequestHasHeader(
    _In_opt_ const char *Request,
    _In_ const char *Line)
{
    size_t Length = strlen(Line);
    const char *Start;

    if (!Request)
        return FALSE;
    for (Start = strstr(Request, "\r\n"); Start; Start = strstr(Start + 2, "\r\n"))
    {
        if (!_strnicmp(Start + 2, Line, Length) && Start[2 + Length] == '\r')
            return TRUE;
    }
    return FALSE;
}

static IUriRuntimeClass *
MakeUri(
    _In_ const WCHAR *Path)
{
    IUriRuntimeClassFactory *Factory = NULL;
    IUriRuntimeClass *Uri = NULL;
    WCHAR Text[128];
    HSTRING String;
    HRESULT hr;

    swprintf(Text, ARRAYSIZE(Text), L"http://127.0.0.1:%u%ls", Server.Port, Path);
    hr = GetFactory(L"Windows.Foundation.Uri", &IID_IUriRuntimeClassFactory, (void **)&Factory);
    ok(hr == S_OK, "Uri factory returned %#lx\n", hr);
    if (FAILED(hr))
        return NULL;
    String = CreateHString(Text);
    hr = IUriRuntimeClassFactory_CreateUri(Factory, String, &Uri);
    ok(hr == S_OK, "CreateUri returned %#lx\n", hr);
    WindowsDeleteString(String);
    IUriRuntimeClassFactory_Release(Factory);
    return Uri;
}

static IMap_HSTRING_HSTRING *
GetMap(
    _In_ IUnknown *Collection)
{
    IMap_HSTRING_HSTRING *Map = NULL;
    HRESULT hr = IUnknown_QueryInterface(Collection, &IID_IMap_HSTRING_HSTRING, (void **)&Map);

    ok(hr == S_OK, "IMap<String, String> returned %#lx\n", hr);
    return Map;
}

static BOOL
MapHas(
    _In_ IMap_HSTRING_HSTRING *Map,
    _In_ const WCHAR *Key)
{
    HSTRING String = CreateHString(Key);
    boolean Found = FALSE;
    HRESULT hr = IMap_HSTRING_HSTRING_HasKey(Map, String, &Found);

    ok(hr == S_OK, "HasKey(%ls) returned %#lx\n", Key, hr);
    WindowsDeleteString(String);
    return Found;
}

static void
CheckLookup(
    _In_ IMap_HSTRING_HSTRING *Map,
    _In_ const WCHAR *Key,
    _In_ const WCHAR *Expected)
{
    HSTRING String = CreateHString(Key), Value = NULL;
    HRESULT hr = IMap_HSTRING_HSTRING_Lookup(Map, String, &Value);

    ok(hr == S_OK, "Lookup(%ls) returned %#lx\n", Key, hr);
    ok(!wcscmp(WindowsGetStringRawBuffer(Value, NULL), Expected), "Lookup(%ls) returned %ls, expected %ls\n",
       Key, WindowsGetStringRawBuffer(Value, NULL), Expected);
    WindowsDeleteString(Value);
    WindowsDeleteString(String);
}

static HRESULT
Send(
    _In_ IHttpClient *Client,
    _In_ IHttpMethod *Method,
    _In_ const WCHAR *Path,
    _In_opt_ const WCHAR *Range,
    _In_ HttpCompletionOption Option,
    _Out_opt_ IHttpRequestMessage **RequestOut,
    _Out_ IHttpResponseMessage **Response)
{
    IAsyncOperationWithProgress_HttpResponseMessage_HttpProgress *Operation = NULL;
    IHttpRequestMessageFactory *Factory = NULL;
    IHttpRequestMessage *Request = NULL;
    IHttpRequestHeaderCollection *Headers = NULL;
    IUriRuntimeClass *Uri = MakeUri(Path);
    AsyncStatus Status;
    HRESULT hr, Error;

    *Response = NULL;
    if (RequestOut)
        *RequestOut = NULL;
    if (!Uri)
        return E_FAIL;
    hr = GetFactory(L"Windows.Web.Http.HttpRequestMessage", &IID_IHttpRequestMessageFactory, (void **)&Factory);
    ok(hr == S_OK, "HttpRequestMessage factory returned %#lx\n", hr);
    if (FAILED(hr))
        goto done;
    hr = IHttpRequestMessageFactory_Create(Factory, Method, Uri, &Request);
    ok(hr == S_OK, "HttpRequestMessage.Create returned %#lx\n", hr);
    if (FAILED(hr))
        goto done;
    if (Range)
    {
        HSTRING Name = CreateHString(L"Range"), Value = CreateHString(Range);

        hr = IHttpRequestMessage_get_Headers(Request, &Headers);
        ok(hr == S_OK, "request Headers returned %#lx\n", hr);
        hr = IHttpRequestHeaderCollection_Append(Headers, Name, Value);
        ok(hr == S_OK, "Append(Range) returned %#lx\n", hr);
        WindowsDeleteString(Name);
        WindowsDeleteString(Value);
        IHttpRequestHeaderCollection_Release(Headers);
    }
    hr = IHttpClient_SendRequestWithOptionAsync(Client, Request, Option, &Operation);
    ok(hr == S_OK, "SendRequestAsync returned %#lx\n", hr);
    if (FAILED(hr))
        goto done;
    Status = WaitAsync((IUnknown *)Operation, 10000, &Error);
    if (Status != Completed)
    {
        hr = Error;
        goto done;
    }
    hr = IAsyncOperationWithProgress_HttpResponseMessage_HttpProgress_GetResults(Operation, Response);
    ok(hr == S_OK, "GetResults returned %#lx\n", hr);
    if (RequestOut)
    {
        *RequestOut = Request;
        Request = NULL;
    }

done:
    if (Operation) IAsyncOperationWithProgress_HttpResponseMessage_HttpProgress_Release(Operation);
    if (Request) IHttpRequestMessage_Release(Request);
    if (Factory) IHttpRequestMessageFactory_Release(Factory);
    IUriRuntimeClass_Release(Uri);
    return hr;
}

static void
CheckResponseUri(
    _In_ IHttpResponseMessage *Response,
    _In_ const WCHAR *Path)
{
    IHttpRequestMessage *Request = NULL;
    IUriRuntimeClass *Uri = NULL;
    HSTRING Text = NULL;
    WCHAR Expected[128];
    HRESULT hr;

    swprintf(Expected, ARRAYSIZE(Expected), L"http://127.0.0.1:%u%ls", Server.Port, Path);
    hr = IHttpResponseMessage_get_RequestMessage(Response, &Request);
    ok(hr == S_OK, "RequestMessage returned %#lx\n", hr);
    if (FAILED(hr) || !Request)
        return;
    hr = IHttpRequestMessage_get_RequestUri(Request, &Uri);
    ok(hr == S_OK, "RequestUri returned %#lx\n", hr);
    if (SUCCEEDED(hr) && Uri)
    {
        IUriRuntimeClass_get_AbsoluteUri(Uri, &Text);
        ok(!wcscmp(WindowsGetStringRawBuffer(Text, NULL), Expected), "RequestUri is %ls, expected %ls\n",
           WindowsGetStringRawBuffer(Text, NULL), Expected);
        WindowsDeleteString(Text);
        IUriRuntimeClass_Release(Uri);
    }
    IHttpRequestMessage_Release(Request);
}

static IMap_HSTRING_HSTRING *
GetResponseHeaders(
    _In_ IHttpResponseMessage *Response)
{
    IHttpResponseHeaderCollection *Headers = NULL;
    IMap_HSTRING_HSTRING *Map;
    HRESULT hr = IHttpResponseMessage_get_Headers(Response, &Headers);

    ok(hr == S_OK, "response Headers returned %#lx\n", hr);
    if (FAILED(hr))
        return NULL;
    Map = GetMap((IUnknown *)Headers);
    IHttpResponseHeaderCollection_Release(Headers);
    return Map;
}

static IHttpContent *
GetContent(
    _In_ IHttpResponseMessage *Response)
{
    IHttpContent *Content = NULL;
    HRESULT hr = IHttpResponseMessage_get_Content(Response, &Content);

    ok(hr == S_OK, "Content returned %#lx\n", hr);
    ok(Content != NULL, "Content is NULL\n");
    return Content;
}

static IMap_HSTRING_HSTRING *
GetContentHeaders(
    _In_ IHttpContent *Content)
{
    IHttpContentHeaderCollection *Headers = NULL;
    IMap_HSTRING_HSTRING *Map;
    HRESULT hr = IHttpContent_get_Headers(Content, &Headers);

    ok(hr == S_OK, "content Headers returned %#lx\n", hr);
    if (FAILED(hr))
        return NULL;
    Map = GetMap((IUnknown *)Headers);
    IHttpContentHeaderCollection_Release(Headers);
    return Map;
}

static void
CheckStatus(
    _In_ IHttpResponseMessage *Response,
    _In_ HttpStatusCode Expected)
{
    HttpStatusCode Status = 0;
    HRESULT hr = IHttpResponseMessage_get_StatusCode(Response, &Status);

    ok(hr == S_OK, "StatusCode returned %#lx\n", hr);
    ok(Status == Expected, "StatusCode is %d, expected %d\n", Status, Expected);
}

static void
TestHead(
    _In_ IHttpClient *Client,
    _In_ IHttpMethodStatics *Methods)
{
    IIterable_IKeyValuePair_HSTRING_HSTRING *Iterable = NULL;
    IIterator_IKeyValuePair_HSTRING_HSTRING *Iterator = NULL;
    IMap_HSTRING_HSTRING *Headers, *ContentHeaders;
    IHttpResponseMessage *Response = NULL;
    IHttpRequestMessage *Request = NULL, *Echo = NULL;
    IHttpRequestHeaderCollection *RequestHeaders;
    IHttpContent *Content;
    IHttpMethod *Head = NULL;
    HSTRING Reason = NULL;
    const char *Seen;
    boolean Has = FALSE, Success = FALSE;
    HttpVersion Version = 0;
    UINT32 Count = 0;
    BOOL FoundVersion = FALSE;
    HRESULT hr;

    hr = IHttpMethodStatics_get_Head(Methods, &Head);
    ok(hr == S_OK, "HttpMethod.Head returned %#lx\n", hr);
    hr = Send(Client, Head, L"/head", NULL, HttpCompletionOption_ResponseHeadersRead, &Request, &Response);
    ok(hr == S_OK, "HEAD /head returned %#lx\n", hr);
    IHttpMethod_Release(Head);
    if (FAILED(hr))
        return;

    Seen = FindRequest("HEAD /head HTTP/1.1\r\n");
    ok(Seen != NULL, "the server did not receive HEAD /head HTTP/1.1\n");
    ok(RequestHasHeader(Seen, "Connection: close"), "the request has no Connection: close\n");
    ok(RequestHasHeader(Seen, "User-Agent: LiberNT-Test/1.0"), "the request has no User-Agent\n");
    ok(RequestHasHeader(Seen, "Accept-Encoding: gzip, deflate"), "the request has no Accept-Encoding\n");

    CheckStatus(Response, HttpStatusCode_Ok);
    hr = IHttpResponseMessage_get_ReasonPhrase(Response, &Reason);
    ok(hr == S_OK, "ReasonPhrase returned %#lx\n", hr);
    ok(!wcscmp(WindowsGetStringRawBuffer(Reason, NULL), L"OK"), "ReasonPhrase is %ls\n", WindowsGetStringRawBuffer(Reason, NULL));
    WindowsDeleteString(Reason);
    hr = IHttpResponseMessage_get_IsSuccessStatusCode(Response, &Success);
    ok(hr == S_OK && Success, "IsSuccessStatusCode returned %#lx, %d\n", hr, Success);
    hr = IHttpResponseMessage_get_Version(Response, &Version);
    ok(hr == S_OK && Version == HttpVersion_Http11, "Version returned %#lx, %d\n", hr, Version);
    hr = IHttpResponseMessage_get_RequestMessage(Response, &Echo);
    ok(hr == S_OK, "RequestMessage returned %#lx\n", hr);
    ok(Echo == Request, "RequestMessage is %p, the sent request is %p\n", Echo, Request);
    if (Echo) IHttpRequestMessage_Release(Echo);
    hr = IHttpRequestMessage_get_Headers(Request, &RequestHeaders);
    ok(hr == S_OK, "request Headers returned %#lx\n", hr);
    if (SUCCEEDED(hr))
    {
        Headers = GetMap((IUnknown *)RequestHeaders);
        if (Headers)
        {
            CheckLookup(Headers, L"User-Agent", L"LiberNT-Test/1.0");
            IMap_HSTRING_HSTRING_Release(Headers);
        }
        IHttpRequestHeaderCollection_Release(RequestHeaders);
    }
    CheckResponseUri(Response, L"/head");

    Headers = GetResponseHeaders(Response);
    if (Headers)
    {
        ok(MapHas(Headers, L"X-Test-Version"), "X-Test-Version is missing\n");
        ok(MapHas(Headers, L"x-test-version"), "the header names are case sensitive\n");
        CheckLookup(Headers, L"X-TEST-VERSION", L"1.2.3");
        CheckLookup(Headers, L"ETag", L"\"abc\"");
        CheckLookup(Headers, L"X-Lower", L"lower");
        ok(!MapHas(Headers, L"Content-Length"), "Content-Length is a response header\n");
        ok(!MapHas(Headers, L"Content-Type"), "Content-Type is a response header\n");
        ok(!MapHas(Headers, L"Last-Modified"), "Last-Modified is a response header\n");
        hr = IMap_HSTRING_HSTRING_get_Size(Headers, &Count);
        ok(hr == S_OK && Count == 4, "Size returned %#lx, %u\n", hr, Count);

        hr = IMap_HSTRING_HSTRING_QueryInterface(Headers, &IID_IIterable_IKeyValuePair_HSTRING_HSTRING, (void **)&Iterable);
        ok(hr == S_OK, "IIterable returned %#lx\n", hr);
        if (SUCCEEDED(hr))
        {
            hr = IIterable_IKeyValuePair_HSTRING_HSTRING_First(Iterable, &Iterator);
            ok(hr == S_OK, "First returned %#lx\n", hr);
            while (SUCCEEDED(hr) && SUCCEEDED(IIterator_IKeyValuePair_HSTRING_HSTRING_get_HasCurrent(Iterator, &Has)) && Has)
            {
                IKeyValuePair_HSTRING_HSTRING *Pair = NULL;
                HSTRING Key = NULL, Value = NULL;

                hr = IIterator_IKeyValuePair_HSTRING_HSTRING_get_Current(Iterator, &Pair);
                ok(hr == S_OK, "Current returned %#lx\n", hr);
                if (FAILED(hr))
                    break;
                IKeyValuePair_HSTRING_HSTRING_get_Key(Pair, &Key);
                IKeyValuePair_HSTRING_HSTRING_get_Value(Pair, &Value);
                if (!wcscmp(WindowsGetStringRawBuffer(Key, NULL), L"X-Test-Version") &&
                    !wcscmp(WindowsGetStringRawBuffer(Value, NULL), L"1.2.3"))
                    FoundVersion = TRUE;
                WindowsDeleteString(Key);
                WindowsDeleteString(Value);
                IKeyValuePair_HSTRING_HSTRING_Release(Pair);
                hr = IIterator_IKeyValuePair_HSTRING_HSTRING_MoveNext(Iterator, &Has);
            }
            ok(FoundVersion, "the iteration does not return X-Test-Version: 1.2.3\n");
            if (Iterator) IIterator_IKeyValuePair_HSTRING_HSTRING_Release(Iterator);
            IIterable_IKeyValuePair_HSTRING_HSTRING_Release(Iterable);
        }
        IMap_HSTRING_HSTRING_Release(Headers);
    }

    Content = GetContent(Response);
    if (Content)
    {
        ContentHeaders = GetContentHeaders(Content);
        if (ContentHeaders)
        {
            CheckLookup(ContentHeaders, L"content-length", L"10");
            CheckLookup(ContentHeaders, L"Content-Type", L"application/x-test");
            CheckLookup(ContentHeaders, L"Last-Modified", L"Mon, 02 Nov 2026 10:00:00 GMT");
            ok(!MapHas(ContentHeaders, L"X-Test-Version"), "X-Test-Version is a content header\n");
            IMap_HSTRING_HSTRING_Release(ContentHeaders);
        }
        IHttpContent_Release(Content);
    }
    IHttpRequestMessage_Release(Request);
    IHttpResponseMessage_Release(Response);
}

static void
TestRange(
    _In_ IHttpClient *Client,
    _In_ IHttpMethodStatics *Methods)
{
    IAsyncOperationWithProgress_IBuffer_UINT64 *Read = NULL;
    IMap_HSTRING_HSTRING *Headers, *ContentHeaders;
    IHttpResponseMessage *Response = NULL;
    IBufferByteAccess *Bytes = NULL;
    IHttpContent *Content;
    IHttpMethod *Get = NULL;
    IBuffer *Buffer = NULL;
    const char *Seen;
    AsyncStatus Status;
    UINT32 Length = 0;
    BYTE *Data = NULL;
    HRESULT hr, Error;

    hr = IHttpMethodStatics_get_Get(Methods, &Get);
    ok(hr == S_OK, "HttpMethod.Get returned %#lx\n", hr);
    hr = Send(Client, Get, L"/range", L"bytes=2-5", HttpCompletionOption_ResponseHeadersRead, NULL, &Response);
    ok(hr == S_OK, "GET /range returned %#lx\n", hr);
    IHttpMethod_Release(Get);
    if (FAILED(hr))
        return;

    Seen = FindRequest("GET /range HTTP/1.1\r\n");
    ok(RequestHasHeader(Seen, "Range: bytes=2-5"), "the request has no Range header\n");
    CheckStatus(Response, HttpStatusCode_PartialContent);
    Headers = GetResponseHeaders(Response);
    if (Headers)
    {
        CheckLookup(Headers, L"Accept-Ranges", L"bytes");
        IMap_HSTRING_HSTRING_Release(Headers);
    }
    Content = GetContent(Response);
    if (!Content)
        goto done;
    ContentHeaders = GetContentHeaders(Content);
    if (ContentHeaders)
    {
        CheckLookup(ContentHeaders, L"Content-Range", L"bytes 2-5/10");
        IMap_HSTRING_HSTRING_Release(ContentHeaders);
    }
    hr = IHttpContent_ReadAsBufferAsync(Content, &Read);
    ok(hr == S_OK, "ReadAsBufferAsync returned %#lx\n", hr);
    if (SUCCEEDED(hr))
    {
        Status = WaitAsync((IUnknown *)Read, 10000, &Error);
        ok(Status == Completed, "ReadAsBufferAsync status %d, error %#lx\n", Status, Error);
        if (Status == Completed)
        {
            hr = IAsyncOperationWithProgress_IBuffer_UINT64_GetResults(Read, &Buffer);
            ok(hr == S_OK, "GetResults returned %#lx\n", hr);
        }
        IAsyncOperationWithProgress_IBuffer_UINT64_Release(Read);
    }
    if (Buffer)
    {
        hr = IBuffer_get_Length(Buffer, &Length);
        ok(hr == S_OK && Length == 4, "Length returned %#lx, %u\n", hr, Length);
        hr = IBuffer_QueryInterface(Buffer, &IID_IBufferByteAccess, (void **)&Bytes);
        ok(hr == S_OK, "IBufferByteAccess returned %#lx\n", hr);
        if (SUCCEEDED(hr))
        {
            IBufferByteAccess_Buffer(Bytes, &Data);
            ok(Length == 4 && !memcmp(Data, "2345", 4), "the body is %.*s\n", (int)Length, Data);
            IBufferByteAccess_Release(Bytes);
        }
        IBuffer_Release(Buffer);
    }
    IHttpContent_Release(Content);
done:
    IHttpResponseMessage_Release(Response);
}

static void
TestRedirect(
    _In_ IHttpClient *Client,
    _In_ IHttpMethodStatics *Methods)
{
    IHttpResponseMessage *Response = NULL;
    IHttpMethod *Head = NULL;
    HRESULT hr;

    hr = IHttpMethodStatics_get_Head(Methods, &Head);
    ok(hr == S_OK, "HttpMethod.Head returned %#lx\n", hr);
    hr = Send(Client, Head, L"/redirect", NULL, HttpCompletionOption_ResponseHeadersRead, NULL, &Response);
    ok(hr == S_OK, "HEAD /redirect returned %#lx\n", hr);
    IHttpMethod_Release(Head);
    if (FAILED(hr))
        return;
    CheckStatus(Response, HttpStatusCode_Ok);
    CheckResponseUri(Response, L"/head");
    ok(FindRequest("HEAD /redirect HTTP/1.1\r\n") != NULL, "the server did not receive HEAD /redirect\n");
    IHttpResponseMessage_Release(Response);
}

static void
TestRetryAfter(
    _In_ IHttpClient *Client,
    _In_ IHttpMethodStatics *Methods)
{
    IHttpDateOrDeltaHeaderValueStatics *Statics = NULL;
    IHttpResponseHeaderCollection *Headers = NULL;
    IHttpDateOrDeltaHeaderValue *Value = NULL;
    IReference_TimeSpan *Delta = NULL;
    IReference_DateTime *Date = NULL;
    IHttpResponseMessage *Response = NULL, *Ensured;
    IHttpMethod *Head = NULL;
    TimeSpan Span = {0};
    boolean Parsed = FALSE, Success = TRUE;
    HSTRING Text;
    HRESULT hr;

    hr = IHttpMethodStatics_get_Head(Methods, &Head);
    ok(hr == S_OK, "HttpMethod.Head returned %#lx\n", hr);
    hr = Send(Client, Head, L"/retry", NULL, HttpCompletionOption_ResponseHeadersRead, NULL, &Response);
    ok(hr == S_OK, "HEAD /retry returned %#lx\n", hr);
    IHttpMethod_Release(Head);
    if (FAILED(hr))
        return;
    CheckStatus(Response, HttpStatusCode_ServiceUnavailable);
    hr = IHttpResponseMessage_get_IsSuccessStatusCode(Response, &Success);
    ok(hr == S_OK && !Success, "IsSuccessStatusCode returned %#lx, %d\n", hr, Success);
    Ensured = (void *)0xdeadbeef;
    hr = IHttpResponseMessage_EnsureSuccessStatusCode(Response, &Ensured);
    ok(hr == MAKE_HRESULT(SEVERITY_ERROR, FACILITY_HTTP, 503), "EnsureSuccessStatusCode returned %#lx\n", hr);
    ok(Ensured == NULL, "EnsureSuccessStatusCode returned %p\n", Ensured);
    hr = IHttpResponseMessage_get_Headers(Response, &Headers);
    ok(hr == S_OK, "Headers returned %#lx\n", hr);
    if (SUCCEEDED(hr))
    {
        hr = IHttpResponseHeaderCollection_get_RetryAfter(Headers, &Value);
        ok(hr == S_OK && Value, "RetryAfter returned %#lx, %p\n", hr, Value);
        if (Value)
        {
            hr = IHttpDateOrDeltaHeaderValue_get_Delta(Value, &Delta);
            ok(hr == S_OK && Delta, "Delta returned %#lx, %p\n", hr, Delta);
            if (Delta)
            {
                IReference_TimeSpan_get_Value(Delta, &Span);
                ok(Span.Duration == 1200000000, "Delta is %I64d\n", Span.Duration);
                IReference_TimeSpan_Release(Delta);
            }
            hr = IHttpDateOrDeltaHeaderValue_get_Date(Value, &Date);
            ok(hr == S_OK && !Date, "Date returned %#lx, %p\n", hr, Date);
            if (Date) IReference_DateTime_Release(Date);
            IHttpDateOrDeltaHeaderValue_Release(Value);
        }
        IHttpResponseHeaderCollection_Release(Headers);
    }
    IHttpResponseMessage_Release(Response);

    hr = GetFactory(L"Windows.Web.Http.Headers.HttpDateOrDeltaHeaderValue", &IID_IHttpDateOrDeltaHeaderValueStatics, (void **)&Statics);
    ok(hr == S_OK, "HttpDateOrDeltaHeaderValue statics returned %#lx\n", hr);
    if (FAILED(hr))
        return;
    Text = CreateHString(L"30");
    Value = NULL;
    hr = IHttpDateOrDeltaHeaderValueStatics_TryParse(Statics, Text, &Value, &Parsed);
    ok(hr == S_OK && Parsed && Value, "TryParse(30) returned %#lx, %d, %p\n", hr, Parsed, Value);
    WindowsDeleteString(Text);
    if (Value)
    {
        Delta = NULL;
        IHttpDateOrDeltaHeaderValue_get_Delta(Value, &Delta);
        ok(Delta != NULL, "TryParse(30) has no delta\n");
        if (Delta)
        {
            IReference_TimeSpan_get_Value(Delta, &Span);
            ok(Span.Duration == 300000000, "Delta is %I64d\n", Span.Duration);
            IReference_TimeSpan_Release(Delta);
        }
        IHttpDateOrDeltaHeaderValue_Release(Value);
    }
    Text = CreateHString(L"Wed, 21 Oct 2015 07:28:00 GMT");
    Value = NULL;
    hr = IHttpDateOrDeltaHeaderValueStatics_TryParse(Statics, Text, &Value, &Parsed);
    ok(hr == S_OK && Parsed && Value, "TryParse(date) returned %#lx, %d, %p\n", hr, Parsed, Value);
    WindowsDeleteString(Text);
    if (Value)
    {
        DateTime Time = {0};

        Date = NULL;
        IHttpDateOrDeltaHeaderValue_get_Date(Value, &Date);
        ok(Date != NULL, "TryParse(date) has no date\n");
        if (Date)
        {
            IReference_DateTime_get_Value(Date, &Time);
            ok(Time.UniversalTime == 130898860800000000, "Date is %I64d\n", Time.UniversalTime);
            IReference_DateTime_Release(Date);
        }
        IHttpDateOrDeltaHeaderValue_Release(Value);
    }
    Text = CreateHString(L"soon");
    Value = (void *)0xdeadbeef;
    hr = IHttpDateOrDeltaHeaderValueStatics_TryParse(Statics, Text, &Value, &Parsed);
    ok(hr == S_OK && !Parsed, "TryParse(soon) returned %#lx, %d\n", hr, Parsed);
    ok(Value == NULL, "TryParse(soon) returned %p\n", Value);
    Value = (void *)0xdeadbeef;
    hr = IHttpDateOrDeltaHeaderValueStatics_Parse(Statics, Text, &Value);
    ok(hr == E_INVALIDARG, "Parse(soon) returned %#lx\n", hr);
    ok(Value == NULL, "Parse(soon) returned %p\n", Value);
    WindowsDeleteString(Text);
    IHttpDateOrDeltaHeaderValueStatics_Release(Statics);
}

static void
TestDuplicate(
    _In_ IHttpClient *Client,
    _In_ IHttpMethodStatics *Methods)
{
    IHttpResponseMessage *Response = NULL;
    IMap_HSTRING_HSTRING *Headers;
    IHttpMethod *Head = NULL;
    HSTRING Key, Value = NULL;
    HRESULT hr;

    hr = IHttpMethodStatics_get_Head(Methods, &Head);
    ok(hr == S_OK, "HttpMethod.Head returned %#lx\n", hr);
    hr = Send(Client, Head, L"/dup", NULL, HttpCompletionOption_ResponseHeadersRead, NULL, &Response);
    ok(hr == S_OK, "HEAD /dup returned %#lx\n", hr);
    IHttpMethod_Release(Head);
    if (FAILED(hr))
        return;
    Headers = GetResponseHeaders(Response);
    if (Headers)
    {
        Key = CreateHString(L"X-Dup");
        hr = IMap_HSTRING_HSTRING_Lookup(Headers, Key, &Value);
        ok(hr == S_OK, "Lookup(X-Dup) returned %#lx\n", hr);
        ok(!wcscmp(WindowsGetStringRawBuffer(Value, NULL), L"a, b"), "X-Dup is \"%ls\"\n", WindowsGetStringRawBuffer(Value, NULL));
        WindowsDeleteString(Value);
        WindowsDeleteString(Key);
        IMap_HSTRING_HSTRING_Release(Headers);
    }
    IHttpResponseMessage_Release(Response);
}

static void
TestRefused(
    _In_ IHttpClient *Client,
    _In_ IHttpMethodStatics *Methods)
{
    IHttpResponseMessage *Response = NULL;
    struct sockaddr_in Address = {0};
    IHttpMethod *Head = NULL;
    USHORT Saved = Server.Port;
    int Size = sizeof(Address);
    SOCKET Probe;
    HRESULT hr;

    Probe = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    Address.sin_family = AF_INET;
    Address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    bind(Probe, (struct sockaddr *)&Address, sizeof(Address));
    getsockname(Probe, (struct sockaddr *)&Address, &Size);
    closesocket(Probe);

    hr = IHttpMethodStatics_get_Head(Methods, &Head);
    ok(hr == S_OK, "HttpMethod.Head returned %#lx\n", hr);
    Server.Port = ntohs(Address.sin_port);
    hr = Send(Client, Head, L"/head", NULL, HttpCompletionOption_ResponseHeadersRead, NULL, &Response);
    Server.Port = Saved;
    ok(hr == HRESULT_FROM_WIN32(12029), "HEAD to a closed port returned %#lx\n", hr);
    ok(Response == NULL, "HEAD to a closed port returned a response\n");
    if (Response) IHttpResponseMessage_Release(Response);
    IHttpMethod_Release(Head);
}

static void
TestFilter(
    _Out_ IHttpFilter **FilterOut)
{
    IHttpBaseProtocolFilter *Filter = NULL;
    IHttpCacheControl *Cache = NULL;
    HttpCacheReadBehavior Read = -1;
    HttpCacheWriteBehavior Write = -1;
    IInspectable *Instance = NULL;
    boolean Value = FALSE;
    UINT32 Connections = 0;
    HSTRING Class;
    HRESULT hr;

    *FilterOut = NULL;
    Class = CreateHString(L"Windows.Web.Http.Filters.HttpBaseProtocolFilter");
    hr = RoActivateInstance(Class, &Instance);
    WindowsDeleteString(Class);
    ok(hr == S_OK, "RoActivateInstance(HttpBaseProtocolFilter) returned %#lx\n", hr);
    if (FAILED(hr))
        return;
    hr = IInspectable_QueryInterface(Instance, &IID_IHttpBaseProtocolFilter, (void **)&Filter);
    ok(hr == S_OK, "IHttpBaseProtocolFilter returned %#lx\n", hr);
    hr = IInspectable_QueryInterface(Instance, &IID_IHttpFilter, (void **)FilterOut);
    ok(hr == S_OK, "IHttpFilter returned %#lx\n", hr);
    IInspectable_Release(Instance);
    if (!Filter)
        return;

    hr = IHttpBaseProtocolFilter_get_AllowAutoRedirect(Filter, &Value);
    ok(hr == S_OK && Value, "AllowAutoRedirect returned %#lx, %d\n", hr, Value);
    hr = IHttpBaseProtocolFilter_get_AutomaticDecompression(Filter, &Value);
    ok(hr == S_OK && Value, "AutomaticDecompression returned %#lx, %d\n", hr, Value);
    hr = IHttpBaseProtocolFilter_get_UseProxy(Filter, &Value);
    ok(hr == S_OK && Value, "UseProxy returned %#lx, %d\n", hr, Value);
    hr = IHttpBaseProtocolFilter_get_AllowUI(Filter, &Value);
    ok(hr == S_OK && Value, "AllowUI returned %#lx, %d\n", hr, Value);
    hr = IHttpBaseProtocolFilter_get_MaxConnectionsPerServer(Filter, &Connections);
    ok(hr == S_OK && Connections == 6, "MaxConnectionsPerServer returned %#lx, %u\n", hr, Connections);

    hr = IHttpBaseProtocolFilter_get_CacheControl(Filter, &Cache);
    ok(hr == S_OK && Cache, "CacheControl returned %#lx, %p\n", hr, Cache);
    if (Cache)
    {
        hr = IHttpCacheControl_get_ReadBehavior(Cache, &Read);
        ok(hr == S_OK && Read == HttpCacheReadBehavior_Default, "ReadBehavior returned %#lx, %d\n", hr, Read);
        hr = IHttpCacheControl_get_WriteBehavior(Cache, &Write);
        ok(hr == S_OK && Write == HttpCacheWriteBehavior_Default, "WriteBehavior returned %#lx, %d\n", hr, Write);
        hr = IHttpCacheControl_put_ReadBehavior(Cache, HttpCacheReadBehavior_MostRecent);
        ok(hr == S_OK, "put_ReadBehavior returned %#lx\n", hr);
        IHttpCacheControl_Release(Cache);
        Cache = NULL;
        hr = IHttpBaseProtocolFilter_get_CacheControl(Filter, &Cache);
        ok(hr == S_OK && Cache, "CacheControl returned %#lx, %p\n", hr, Cache);
        if (Cache)
        {
            hr = IHttpCacheControl_get_ReadBehavior(Cache, &Read);
            ok(hr == S_OK && Read == HttpCacheReadBehavior_MostRecent, "ReadBehavior returned %#lx, %d\n", hr, Read);
            IHttpCacheControl_Release(Cache);
        }
    }
    IHttpBaseProtocolFilter_Release(Filter);
}

static void
SetDefaultHeaders(
    _In_ IHttpClient *Client)
{
    IVector_HttpConnectionOptionHeaderValue *Vector = NULL;
    IHttpConnectionOptionHeaderValueCollection *Connection = NULL;
    IHttpProductInfoHeaderValueCollection *UserAgent = NULL;
    IHttpRequestHeaderCollection *Headers = NULL;
    IMap_HSTRING_HSTRING *Map;
    HSTRING Name, Value;
    UINT32 Size = 1;
    HRESULT hr;

    hr = IHttpClient_get_DefaultRequestHeaders(Client, &Headers);
    ok(hr == S_OK, "DefaultRequestHeaders returned %#lx\n", hr);
    if (FAILED(hr))
        return;
    Map = GetMap((IUnknown *)Headers);
    if (Map)
    {
        IMap_HSTRING_HSTRING_get_Size(Map, &Size);
        ok(Size == 0, "the default request headers have %u entries\n", Size);
    }

    hr = IHttpRequestHeaderCollection_get_Connection(Headers, &Connection);
    ok(hr == S_OK, "Connection returned %#lx\n", hr);
    if (SUCCEEDED(hr))
    {
        hr = IHttpConnectionOptionHeaderValueCollection_QueryInterface(Connection, &IID_IVector_HttpConnectionOptionHeaderValue, (void **)&Vector);
        ok(hr == S_OK, "IVector<HttpConnectionOptionHeaderValue> returned %#lx\n", hr);
        if (SUCCEEDED(hr))
        {
            hr = IVector_HttpConnectionOptionHeaderValue_Clear(Vector);
            ok(hr == S_OK, "Clear returned %#lx\n", hr);
            IVector_HttpConnectionOptionHeaderValue_Release(Vector);
        }
        IHttpConnectionOptionHeaderValueCollection_Release(Connection);
    }

    Name = CreateHString(L"Connection");
    Value = CreateHString(L"close");
    hr = IHttpRequestHeaderCollection_Append(Headers, Name, Value);
    ok(hr == S_OK, "Append(Connection) returned %#lx\n", hr);
    WindowsDeleteString(Name);
    WindowsDeleteString(Value);

    hr = IHttpRequestHeaderCollection_get_UserAgent(Headers, &UserAgent);
    ok(hr == S_OK, "UserAgent returned %#lx\n", hr);
    if (SUCCEEDED(hr))
    {
        Value = CreateHString(L"LiberNT-Test/1.0");
        hr = IHttpProductInfoHeaderValueCollection_ParseAdd(UserAgent, Value);
        ok(hr == S_OK, "UserAgent.ParseAdd returned %#lx\n", hr);
        WindowsDeleteString(Value);
        IHttpProductInfoHeaderValueCollection_Release(UserAgent);
    }

    if (Map)
    {
        CheckLookup(Map, L"connection", L"close");
        CheckLookup(Map, L"User-Agent", L"LiberNT-Test/1.0");
        IMap_HSTRING_HSTRING_get_Size(Map, &Size);
        ok(Size == 2, "the default request headers have %u entries\n", Size);
        IMap_HSTRING_HSTRING_Release(Map);
    }
    IHttpRequestHeaderCollection_Release(Headers);
}

START_TEST(HttpClient)
{
    IHttpMethodStatics *Methods = NULL;
    IHttpClientFactory *Factory = NULL;
    IHttpClient *Client = NULL;
    IHttpFilter *Filter = NULL;
    IHttpMethod *Head = NULL;
    HSTRING Name = NULL;
    HRESULT hr;

    hr = RoInitialize(RO_INIT_MULTITHREADED);
    ok(hr == S_OK, "RoInitialize returned %#lx\n", hr);
    TraceActivatableClass(L"Windows.Web.Http.HttpClient");
    TraceActivatableClass(L"Windows.Web.Http.Filters.HttpBaseProtocolFilter");
    if (!StartServer())
    {
        skip("the loopback server did not start\n");
        RoUninitialize();
        return;
    }

    TestFilter(&Filter);
    if (!Filter)
        goto done;
    hr = GetFactory(L"Windows.Web.Http.HttpClient", &IID_IHttpClientFactory, (void **)&Factory);
    ok(hr == S_OK, "HttpClient factory returned %#lx\n", hr);
    if (FAILED(hr))
        goto done;
    hr = IHttpClientFactory_Create(Factory, Filter, &Client);
    ok(hr == S_OK, "HttpClient.Create returned %#lx\n", hr);
    if (FAILED(hr))
        goto done;
    hr = GetFactory(L"Windows.Web.Http.HttpMethod", &IID_IHttpMethodStatics, (void **)&Methods);
    ok(hr == S_OK, "HttpMethod statics returned %#lx\n", hr);
    if (FAILED(hr))
        goto done;
    hr = IHttpMethodStatics_get_Head(Methods, &Head);
    ok(hr == S_OK, "HttpMethod.Head returned %#lx\n", hr);
    if (SUCCEEDED(hr))
    {
        hr = IHttpMethod_get_Method(Head, &Name);
        ok(hr == S_OK && !wcscmp(WindowsGetStringRawBuffer(Name, NULL), L"HEAD"), "Method returned %#lx, %ls\n",
           hr, WindowsGetStringRawBuffer(Name, NULL));
        WindowsDeleteString(Name);
        IHttpMethod_Release(Head);
    }

    SetDefaultHeaders(Client);
    TestHead(Client, Methods);
    TestRange(Client, Methods);
    TestRedirect(Client, Methods);
    TestRetryAfter(Client, Methods);
    TestDuplicate(Client, Methods);
    TestRefused(Client, Methods);

done:
    if (Methods) IHttpMethodStatics_Release(Methods);
    if (Client) IHttpClient_Release(Client);
    if (Factory) IHttpClientFactory_Release(Factory);
    if (Filter) IHttpFilter_Release(Filter);
    StopServer();
    RoUninitialize();
}
