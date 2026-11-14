/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests synchronous and asynchronous GetAddrInfoExW, GetAddrInfoExCancel and GetAddrInfoExOverlappedResult
 * COPYRIGHT:   Copyright 2026 Ostap Brehin <osbre@protonmail.com>
 */

#include "ws2_32.h"

#define WAIT_MS 5000

typedef INT (WSAAPI *PGETADDRINFOEXW)(PCWSTR, PCWSTR, DWORD, LPGUID, const ADDRINFOEXW *, PADDRINFOEXW *,
                                      struct timeval *, LPOVERLAPPED, LPLOOKUPSERVICE_COMPLETION_ROUTINE, LPHANDLE);
typedef VOID (WSAAPI *PFREEADDRINFOEXW)(PADDRINFOEXW);
typedef INT (WSAAPI *PGETADDRINFOEXCANCEL)(LPHANDLE);
typedef INT (WSAAPI *PGETADDRINFOEXOVERLAPPEDRESULT)(LPOVERLAPPED);

static PGETADDRINFOEXW pGetAddrInfoExW;
static PFREEADDRINFOEXW pFreeAddrInfoExW;
static PGETADDRINFOEXCANCEL pGetAddrInfoExCancel;
static PGETADDRINFOEXOVERLAPPEDRESULT pGetAddrInfoExOverlappedResult;

static HANDLE CompletionEvent;
static DWORD CompletionError;
static DWORD CompletionBytes;
static LPWSAOVERLAPPED CompletionOverlapped;
static DWORD CompletionThreadId;
static LONG CompletionCount;

static
VOID
CALLBACK
CompletionRoutine(DWORD dwError, DWORD dwBytes, LPWSAOVERLAPPED lpOverlapped)
{
    CompletionError = dwError;
    CompletionBytes = dwBytes;
    CompletionOverlapped = lpOverlapped;
    CompletionThreadId = GetCurrentThreadId();
    InterlockedIncrement(&CompletionCount);
    SetEvent(CompletionEvent);
}

static
VOID
ResetCompletion(VOID)
{
    CompletionError = 0xdeadbeef;
    CompletionBytes = 0xdeadbeef;
    CompletionOverlapped = NULL;
    CompletionThreadId = 0;
    CompletionCount = 0;
    ResetEvent(CompletionEvent);
}

static
VOID
InitHints(PADDRINFOEXW Hints, INT Flags)
{
    ZeroMemory(Hints, sizeof(*Hints));
    Hints->ai_flags = Flags;
    Hints->ai_family = AF_INET;
    Hints->ai_socktype = SOCK_STREAM;
}

static
VOID
CheckIPv4Result(PADDRINFOEXW Result, PCSTR Address, USHORT Port)
{
    PSOCKADDR_IN SockAddr;

    ok(Result != NULL, "Result is NULL\n");
    if (!Result) return;

    ok_dec(Result->ai_family, AF_INET);
    ok_dec(Result->ai_socktype, SOCK_STREAM);
    ok_dec((INT)Result->ai_addrlen, sizeof(SOCKADDR_IN));
    ok(Result->ai_addr != NULL, "ai_addr is NULL\n");
    if (!Result->ai_addr) return;

    SockAddr = (PSOCKADDR_IN)Result->ai_addr;
    ok_dec(SockAddr->sin_family, AF_INET);
    ok_dec(ntohs(SockAddr->sin_port), Port);
    ok_hex(SockAddr->sin_addr.S_un.S_addr, inet_addr(Address));
}

static
VOID
TestSync(VOID)
{
    ADDRINFOEXW Hints;
    PADDRINFOEXW Result;
    HANDLE Handle;
    INT Error;

    InitHints(&Hints, AI_NUMERICHOST);
    Result = InvalidPointer;
    WSASetLastError(0xdeadbeef);
    Error = pGetAddrInfoExW(L"192.0.2.1", L"80", NS_DNS, NULL, &Hints, &Result, NULL, NULL, NULL, NULL);
    ok_dec(Error, 0);
    ok_dec(WSAGetLastError(), 0);
    CheckIPv4Result(Result, "192.0.2.1", 80);
    if (Result && Result != InvalidPointer) pFreeAddrInfoExW(Result);

    Result = InvalidPointer;
    Handle = InvalidPointer;
    Error = pGetAddrInfoExW(L"192.0.2.1", L"80", NS_DNS, NULL, &Hints, &Result, NULL, NULL, NULL, &Handle);
    ok_dec(Error, WSAEINVAL);
    ok_ptr(Handle, NULL);
    ok_ptr(Result, NULL);

    InitHints(&Hints, 0);
    Result = InvalidPointer;
    Error = pGetAddrInfoExW(L"localhost", L"443", NS_ALL, NULL, &Hints, &Result, NULL, NULL, NULL, NULL);
    ok_dec(Error, 0);
    CheckIPv4Result(Result, "127.0.0.1", 443);
    if (Result && Result != InvalidPointer) pFreeAddrInfoExW(Result);

    InitHints(&Hints, AI_NUMERICHOST);
    Result = InvalidPointer;
    Error = pGetAddrInfoExW(L"not-a-number", L"80", NS_DNS, NULL, &Hints, &Result, NULL, NULL, NULL, NULL);
    ok_dec(Error, WSAHOST_NOT_FOUND);
    ok_ptr(Result, NULL);
}

static
VOID
TestAsyncEvent(PCWSTR Name, INT Flags, PCSTR Address)
{
    ADDRINFOEXW Hints;
    PADDRINFOEXW Result;
    OVERLAPPED Overlapped;
    HANDLE Handle;
    INT Error;

    ZeroMemory(&Overlapped, sizeof(Overlapped));
    Overlapped.hEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
    ok(Overlapped.hEvent != NULL, "CreateEventW failed %lu\n", GetLastError());
    if (!Overlapped.hEvent) return;

    InitHints(&Hints, Flags);
    Result = InvalidPointer;
    Handle = InvalidPointer;
    WSASetLastError(0xdeadbeef);
    Error = pGetAddrInfoExW(Name, L"80", NS_DNS, NULL, &Hints, &Result, NULL, &Overlapped, NULL, &Handle);
    ok_dec(Error, WSA_IO_PENDING);
    ok_dec(WSAGetLastError(), WSA_IO_PENDING);
    ok(Handle != NULL && Handle != InvalidPointer, "Handle = %p\n", Handle);

    ok_dec(WaitForSingleObject(Overlapped.hEvent, WAIT_MS), WAIT_OBJECT_0);
    ok_dec(pGetAddrInfoExOverlappedResult(&Overlapped), 0);
    ok_hex((ULONG)Overlapped.Internal, 0);
    ok_ptr(Overlapped.Pointer, &Result);
    CheckIPv4Result(Result, Address, 80);
    if (Result && Result != InvalidPointer) pFreeAddrInfoExW(Result);

    if (Error == WSA_IO_PENDING)
        ok_dec(pGetAddrInfoExCancel(&Handle), WSA_INVALID_HANDLE);

    CloseHandle(Overlapped.hEvent);
}

static
VOID
TestAsyncImmediate(PCWSTR Name, INT Flags, INT ExpectedError)
{
    ADDRINFOEXW Hints;
    PADDRINFOEXW Result;
    OVERLAPPED Overlapped;
    HANDLE Handle;
    INT Error;

    ZeroMemory(&Overlapped, sizeof(Overlapped));
    Overlapped.hEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
    ok(Overlapped.hEvent != NULL, "CreateEventW failed %lu\n", GetLastError());
    if (!Overlapped.hEvent) return;

    InitHints(&Hints, Flags);
    Result = InvalidPointer;
    Handle = InvalidPointer;
    WSASetLastError(0xdeadbeef);
    Error = pGetAddrInfoExW(Name, L"80", NS_DNS, NULL, &Hints, &Result, NULL, &Overlapped, NULL, &Handle);
    ok_dec(Error, ExpectedError);
    ok_dec(WSAGetLastError(), ExpectedError);
    ok_ptr(Handle, NULL);
    ok_dec(WaitForSingleObject(Overlapped.hEvent, 0), WAIT_TIMEOUT);
    ok_hex((ULONG)Overlapped.Internal, WSAEINPROGRESS);
    ok_ptr(Overlapped.Pointer, NULL);
    if (ExpectedError == 0)
        CheckIPv4Result(Result, "192.0.2.1", 80);
    else
        ok_ptr(Result, NULL);
    if (Error == 0 && Result && Result != InvalidPointer) pFreeAddrInfoExW(Result);
    if (Error == WSA_IO_PENDING) WaitForSingleObject(Overlapped.hEvent, WAIT_MS);

    CloseHandle(Overlapped.hEvent);
}

static
VOID
TestAsyncCompletionRoutine(VOID)
{
    ADDRINFOEXW Hints;
    PADDRINFOEXW Result;
    OVERLAPPED Overlapped;
    HANDLE Handle;
    INT Error;

    ResetCompletion();
    ZeroMemory(&Overlapped, sizeof(Overlapped));
    InitHints(&Hints, 0);
    Result = InvalidPointer;
    Handle = InvalidPointer;
    Error = pGetAddrInfoExW(L"localhost", L"80", NS_DNS, NULL, &Hints, &Result, NULL, &Overlapped, CompletionRoutine, &Handle);
    ok_dec(Error, WSA_IO_PENDING);
    ok(Handle != NULL && Handle != InvalidPointer, "Handle = %p\n", Handle);

    ok_dec(WaitForSingleObject(CompletionEvent, WAIT_MS), WAIT_OBJECT_0);
    ok_dec(CompletionCount, 1);
    ok_dec(CompletionError, 0);
    ok_ptr(CompletionOverlapped, &Overlapped);
    ok(CompletionThreadId != 0 && CompletionThreadId != GetCurrentThreadId(),
       "Completion thread %lu, caller %lu\n", CompletionThreadId, GetCurrentThreadId());
    ok_dec(CompletionBytes, 0);
    ok_hex((ULONG)Overlapped.Internal, 0);
    ok_ptr(Overlapped.Pointer, &Result);
    ok_dec(pGetAddrInfoExOverlappedResult(&Overlapped), 0);
    CheckIPv4Result(Result, "127.0.0.1", 80);
    if (Result && Result != InvalidPointer) pFreeAddrInfoExW(Result);

    ok_dec(SleepEx(0, TRUE), 0);
    ok_dec(CompletionCount, 1);
}

static
VOID
TestCancel(VOID)
{
    ADDRINFOEXW Hints;
    PADDRINFOEXW Result;
    OVERLAPPED Overlapped;
    HANDLE Handle;
    INT Error, CancelError;

    ResetCompletion();
    ZeroMemory(&Overlapped, sizeof(Overlapped));
    InitHints(&Hints, 0);
    Result = InvalidPointer;
    Handle = NULL;
    Error = pGetAddrInfoExW(L"libernt-cancel.example.com", L"80", NS_DNS, NULL, &Hints, &Result, NULL, &Overlapped, CompletionRoutine, &Handle);
    ok_dec(Error, WSA_IO_PENDING);
    if (Error != WSA_IO_PENDING) return;

    CancelError = pGetAddrInfoExCancel(&Handle);
    if (CancelError == 0)
    {
        ok_dec(CompletionCount, 1);
        ok_dec(CompletionThreadId, GetCurrentThreadId());
        ok_dec(CompletionError, WSA_E_CANCELLED);
        ok_hex((ULONG)Overlapped.Internal, WSA_E_CANCELLED);
        ok_dec(pGetAddrInfoExOverlappedResult(&Overlapped), WSA_E_CANCELLED);
        ok_ptr(Result, NULL);
        ok_ptr(Overlapped.Pointer, &Result);
    }
    else
    {
        ok_dec(CancelError, WSA_INVALID_HANDLE);
        ok_dec(WaitForSingleObject(CompletionEvent, WAIT_MS), WAIT_OBJECT_0);
        trace("Query completed before the cancel with %lu\n", CompletionError);
        if (CompletionError == 0 && Result && Result != InvalidPointer) pFreeAddrInfoExW(Result);
    }

    ok_dec(pGetAddrInfoExCancel(&Handle), WSA_INVALID_HANDLE);
    Sleep(200);
    ok_dec(CompletionCount, 1);
}

static
VOID
TestTimeout(VOID)
{
    ADDRINFOEXW Hints;
    PADDRINFOEXW Result;
    OVERLAPPED Overlapped;
    struct timeval Timeout;
    HANDLE Handle;
    INT Error;

    InitHints(&Hints, 0);
    Timeout.tv_sec = 5;
    Timeout.tv_usec = 0;
    Result = InvalidPointer;
    Error = pGetAddrInfoExW(L"localhost", L"80", NS_DNS, NULL, &Hints, &Result, &Timeout, NULL, NULL, NULL);
    ok_dec(Error, WSAEINVAL);
    ok_ptr(Result, NULL);

    ResetCompletion();
    ZeroMemory(&Overlapped, sizeof(Overlapped));
    Result = InvalidPointer;
    Handle = NULL;
    Error = pGetAddrInfoExW(L"localhost", L"80", NS_DNS, NULL, &Hints, &Result, &Timeout, &Overlapped, CompletionRoutine, &Handle);
    ok_dec(Error, WSA_IO_PENDING);
    if (Error == WSA_IO_PENDING)
    {
        ok_dec(WaitForSingleObject(CompletionEvent, WAIT_MS), WAIT_OBJECT_0);
        ok_dec(CompletionError, 0);
        CheckIPv4Result(Result, "127.0.0.1", 80);
        if (CompletionError == 0 && Result && Result != InvalidPointer) pFreeAddrInfoExW(Result);
    }

    Timeout.tv_sec = 0;
    Timeout.tv_usec = 0;
    ResetCompletion();
    ZeroMemory(&Overlapped, sizeof(Overlapped));
    Result = InvalidPointer;
    Error = pGetAddrInfoExW(L"localhost", L"80", NS_DNS, NULL, &Hints, &Result, &Timeout, &Overlapped, CompletionRoutine, &Handle);
    ok_dec(Error, WSAEINVAL);
    ok_ptr(Result, NULL);
    if (Error == WSA_IO_PENDING) WaitForSingleObject(CompletionEvent, WAIT_MS);
}

static
VOID
TestInvalid(VOID)
{
    ADDRINFOEXW Hints;
    PADDRINFOEXW Result;
    OVERLAPPED Overlapped;
    SOCKADDR_IN SockAddr;
    HANDLE Handle;
    INT Error;

    ok_dec(pGetAddrInfoExOverlappedResult(NULL), WSAEINVAL);

    Handle = (HANDLE)(ULONG_PTR)0x12345678;
    ok_dec(pGetAddrInfoExCancel(&Handle), WSA_INVALID_HANDLE);
    Handle = NULL;
    ok_dec(pGetAddrInfoExCancel(&Handle), WSA_INVALID_HANDLE);
    ok_dec(pGetAddrInfoExCancel(NULL), WSA_INVALID_HANDLE);

    ResetCompletion();
    ZeroMemory(&Overlapped, sizeof(Overlapped));
    Overlapped.hEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
    InitHints(&Hints, 0);
    Result = InvalidPointer;
    Error = pGetAddrInfoExW(L"localhost", L"80", NS_DNS, NULL, &Hints, &Result, NULL, &Overlapped, CompletionRoutine, NULL);
    ok_dec(Error, WSAEINVAL);
    ok_ptr(Result, NULL);
    if (Error == WSA_IO_PENDING)
    {
        WaitForSingleObject(CompletionEvent, WAIT_MS);
        WaitForSingleObject(Overlapped.hEvent, WAIT_MS);
        if (Result && Result != InvalidPointer) pFreeAddrInfoExW(Result);
    }
    CloseHandle(Overlapped.hEvent);

    Result = InvalidPointer;
    Error = pGetAddrInfoExW(L"localhost", L"80", NS_DNS, NULL, &Hints, &Result, NULL, NULL, CompletionRoutine, NULL);
    ok_dec(Error, WSAEINVAL);
    ok_ptr(Result, NULL);
    if (Error == 0 && Result && Result != InvalidPointer) pFreeAddrInfoExW(Result);
    Sleep(200);
    ok_dec(CompletionCount, 0);

    InitHints(&Hints, 0);
    ZeroMemory(&SockAddr, sizeof(SockAddr));
    Hints.ai_addr = (PSOCKADDR)&SockAddr;
    Hints.ai_addrlen = sizeof(SockAddr);
    Result = InvalidPointer;
    Error = pGetAddrInfoExW(L"localhost", L"80", NS_DNS, NULL, &Hints, &Result, NULL, NULL, NULL, NULL);
    ok_dec(Error, WSANO_RECOVERY);
    if (Error == 0 && Result && Result != InvalidPointer) pFreeAddrInfoExW(Result);
}

START_TEST(GetAddrInfoEx)
{
    WSADATA WsaData;
    HMODULE Ws2_32;

    ok_dec(WSAStartup(MAKEWORD(2, 2), &WsaData), 0);

    Ws2_32 = GetModuleHandleW(L"ws2_32.dll");
    pGetAddrInfoExW = (PGETADDRINFOEXW)GetProcAddress(Ws2_32, "GetAddrInfoExW");
    pFreeAddrInfoExW = (PFREEADDRINFOEXW)GetProcAddress(Ws2_32, "FreeAddrInfoExW");
    pGetAddrInfoExCancel = (PGETADDRINFOEXCANCEL)GetProcAddress(Ws2_32, "GetAddrInfoExCancel");
    pGetAddrInfoExOverlappedResult = (PGETADDRINFOEXOVERLAPPEDRESULT)GetProcAddress(Ws2_32, "GetAddrInfoExOverlappedResult");
    ok(pGetAddrInfoExW != NULL, "GetAddrInfoExW missing\n");
    ok(pFreeAddrInfoExW != NULL, "FreeAddrInfoExW missing\n");
    ok(pGetAddrInfoExCancel != NULL, "GetAddrInfoExCancel missing\n");
    ok(pGetAddrInfoExOverlappedResult != NULL, "GetAddrInfoExOverlappedResult missing\n");
    if (!pGetAddrInfoExW || !pFreeAddrInfoExW || !pGetAddrInfoExCancel || !pGetAddrInfoExOverlappedResult)
    {
        skip("GetAddrInfoEx functions missing\n");
        WSACleanup();
        return;
    }

    CompletionEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
    ok(CompletionEvent != NULL, "CreateEventW failed %lu\n", GetLastError());
    if (!CompletionEvent)
    {
        WSACleanup();
        return;
    }

    TestSync();
    TestAsyncEvent(L"localhost", 0, "127.0.0.1");
    TestAsyncImmediate(L"192.0.2.1", 0, 0);
    TestAsyncImmediate(L"192.0.2.1", AI_NUMERICHOST, 0);
    TestAsyncImmediate(L"not-a-number", AI_NUMERICHOST, WSAHOST_NOT_FOUND);
    TestAsyncCompletionRoutine();
    TestCancel();
    TestTimeout();
    TestInvalid();

    CloseHandle(CompletionEvent);
    ok_dec(WSACleanup(), 0);
}
