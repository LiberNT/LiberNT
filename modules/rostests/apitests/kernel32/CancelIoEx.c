/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test CancelIoEx on requests issued by another thread of the process
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"

typedef struct _HELD_READ_CONTEXT
{
    HANDLE Pipe;
    HANDLE Issued;
    HANDLE Release;
    OVERLAPPED Overlapped;
    CHAR Buffer[16];
    BOOL Result;
    DWORD Error;
} HELD_READ_CONTEXT;

static DWORD WINAPI
HeldReadThread(PVOID Parameter)
{
    HELD_READ_CONTEXT *Context = Parameter;

    Context->Result = ReadFile(Context->Pipe, Context->Buffer, sizeof(Context->Buffer), NULL, &Context->Overlapped);
    Context->Error = GetLastError();
    SetEvent(Context->Issued);
    WaitForSingleObject(Context->Release, 5000);
    return 0;
}

static VOID
TestCancelOtherThreadRead(BOOL ByOverlapped)
{
    static const WCHAR PipeName[] = L"\\\\.\\pipe\\CancelIoExHeldRead";
    HELD_READ_CONTEXT Context;
    HANDLE Server, Client, Thread;
    OVERLAPPED Other;
    DWORD Bytes, Result;
    BOOL Success;

    Server = CreateNamedPipeW(PipeName, PIPE_ACCESS_INBOUND | FILE_FLAG_OVERLAPPED,
                              PIPE_TYPE_BYTE | PIPE_WAIT, 1, 64, 64, 0, NULL);
    ok(Server != INVALID_HANDLE_VALUE, "CreateNamedPipeW failed with %lu\n", GetLastError());
    if (Server == INVALID_HANDLE_VALUE)
        return;

    Client = CreateFileW(PipeName, GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
    ok(Client != INVALID_HANDLE_VALUE, "CreateFileW failed with %lu\n", GetLastError());

    ZeroMemory(&Context, sizeof(Context));
    Context.Pipe = Server;
    Context.Issued = CreateEventW(NULL, TRUE, FALSE, NULL);
    Context.Release = CreateEventW(NULL, TRUE, FALSE, NULL);
    Context.Overlapped.hEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
    Thread = CreateThread(NULL, 0, HeldReadThread, &Context, 0, NULL);
    ok(Thread != NULL, "CreateThread failed with %lu\n", GetLastError());
    if (Thread != NULL)
    {
        Result = WaitForSingleObject(Context.Issued, 5000);
        ok(Result == WAIT_OBJECT_0, "The reading thread did not issue its read (%lu)\n", Result);
        ok(!Context.Result && Context.Error == ERROR_IO_PENDING,
           "ReadFile returned %d with %lu instead of a pending read\n", Context.Result, Context.Error);

        ZeroMemory(&Other, sizeof(Other));
        SetLastError(0xdeadbeef);
        Success = CancelIoEx(Server, &Other);
        ok(!Success && GetLastError() == ERROR_NOT_FOUND,
           "CancelIoEx with an OVERLAPPED that has no request returned %d with %lu\n", Success, GetLastError());

        Success = CancelIoEx(Server, ByOverlapped ? &Context.Overlapped : NULL);
        ok(Success, "CancelIoEx did not find the read of the other thread (%lu)\n", GetLastError());

        Result = WaitForSingleObject(Context.Overlapped.hEvent, 5000);
        ok(Result == WAIT_OBJECT_0, "The cancelled read did not complete (%lu)\n", Result);
        SetLastError(0xdeadbeef);
        Success = GetOverlappedResult(Server, &Context.Overlapped, &Bytes, FALSE);
        ok(!Success && GetLastError() == ERROR_OPERATION_ABORTED,
           "GetOverlappedResult returned %d with %lu\n", Success, GetLastError());

        SetEvent(Context.Release);
        Result = WaitForSingleObject(Thread, 5000);
        ok(Result == WAIT_OBJECT_0, "The reading thread did not exit (%lu)\n", Result);
        CloseHandle(Thread);
    }

    CloseHandle(Context.Overlapped.hEvent);
    CloseHandle(Context.Release);
    CloseHandle(Context.Issued);
    if (Client != INVALID_HANDLE_VALUE) CloseHandle(Client);
    CloseHandle(Server);
}

START_TEST(CancelIoEx)
{
    TestCancelOtherThreadRead(TRUE);
    TestCancelOtherThreadRead(FALSE);
}
