/*
 * PROJECT:     LiberNT kernel-mode tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Named pipe client computer name set by a kernel-mode pipe client
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <kmt_test.h>

#include "kernel32_test.h"

#define PIPE_LEAF L"kmtest_pipe_client_computer_name"

typedef BOOL (WINAPI *FN_GetNamedPipeClientComputerNameW)(HANDLE, LPWSTR, ULONG);
typedef BOOL (WINAPI *FN_GetNamedPipeClientComputerNameA)(HANDLE, LPSTR, ULONG);

static
DWORD
AttachRemoteClient(
    _In_opt_ PCWSTR ComputerName,
    _In_ USHORT ComputerNameLength)
{
    KMT_REMOTE_PIPE_CLIENT Request;
    DWORD Length = 0;

    RtlZeroMemory(&Request, sizeof(Request));
    wcscpy(Request.PipeName, L"\\Device\\NamedPipe\\" PIPE_LEAF);
    Request.ClientSession = (PVOID)(ULONG_PTR)0x5A5A;
    Request.ClientProcess = (PVOID)(ULONG_PTR)0x1234;
    Request.ComputerNameLength = ComputerNameLength;
    if (ComputerName)
    {
        RtlCopyMemory(Request.ComputerName, ComputerName,
                      min(wcslen(ComputerName) * sizeof(WCHAR), sizeof(Request.ComputerName)));
    }
    Request.WithComputerName = TRUE;
    return KmtSendBufferToDriver(IOCTL_ATTACH_REMOTE_PIPE_CLIENT, &Request, sizeof(Request), &Length);
}

static
VOID
DetachRemoteClient(VOID)
{
    DWORD Length = 0;
    DWORD Error;

    Error = KmtSendBufferToDriver(IOCTL_DETACH_REMOTE_PIPE_CLIENT, NULL, 0, &Length);
    ok(Error == ERROR_SUCCESS, "IOCTL_DETACH_REMOTE_PIPE_CLIENT failed with %lu\n", Error);
}

static
HANDLE
CreateServer(VOID)
{
    HANDLE Server;

    Server = CreateNamedPipeW(L"\\\\.\\pipe\\" PIPE_LEAF, PIPE_ACCESS_DUPLEX,
                              PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
                              1, 512, 512, 0, NULL);
    ok(Server != INVALID_HANDLE_VALUE, "CreateNamedPipeW failed with %lu\n", GetLastError());
    return Server;
}

static
VOID
TestRemoteName(
    _In_ FN_GetNamedPipeClientComputerNameW pGetNameW,
    _In_ FN_GetNamedPipeClientComputerNameA pGetNameA)
{
    WCHAR NameW[32];
    CHAR NameA[32];
    HANDLE Server;
    DWORD Error;
    BOOL Result;

    Server = CreateServer();
    if (Server == INVALID_HANDLE_VALUE)
        return;

    Error = AttachRemoteClient(L"REMOTEPC", sizeof(L"REMOTEPC") - sizeof(WCHAR));
    ok(Error == ERROR_SUCCESS, "IOCTL_ATTACH_REMOTE_PIPE_CLIENT failed with %lu\n", Error);
    if (Error != ERROR_SUCCESS)
    {
        CloseHandle(Server);
        return;
    }

    RtlFillMemory(NameW, sizeof(NameW), 0x55);
    SetLastError(0xdeadbeef);
    Result = pGetNameW(Server, NameW, sizeof(NameW));
    ok(Result == TRUE, "GetNamedPipeClientComputerNameW failed with %lu\n", GetLastError());
    ok(GetLastError() == 0xdeadbeef, "Last error changed to %lu\n", GetLastError());
    ok(wcscmp(NameW, L"REMOTEPC") == 0, "Got name '%ls'\n", NameW);

    RtlFillMemory(NameW, sizeof(NameW), 0x55);
    Result = pGetNameW(Server, NameW, sizeof(L"REMOTEPC"));
    ok(Result == TRUE, "GetNamedPipeClientComputerNameW with an exact buffer failed with %lu\n", GetLastError());
    ok(wcscmp(NameW, L"REMOTEPC") == 0, "Got name '%ls'\n", NameW);

    SetLastError(0xdeadbeef);
    Result = pGetNameW(Server, NameW, sizeof(L"REMOTEPC") - sizeof(WCHAR));
    ok(Result == FALSE, "GetNamedPipeClientComputerNameW succeeded with a short buffer\n");
    ok(GetLastError() == ERROR_INSUFFICIENT_BUFFER, "Last error %lu\n", GetLastError());

    RtlFillMemory(NameA, sizeof(NameA), 0x55);
    Result = pGetNameA(Server, NameA, sizeof(NameA));
    ok(Result == TRUE, "GetNamedPipeClientComputerNameA failed with %lu\n", GetLastError());
    ok(strcmp(NameA, "REMOTEPC") == 0, "Got name '%s'\n", NameA);

    RtlFillMemory(NameA, sizeof(NameA), 0x55);
    Result = pGetNameA(Server, NameA, sizeof("REMOTEPC"));
    ok(Result == TRUE, "GetNamedPipeClientComputerNameA with an exact buffer failed with %lu\n", GetLastError());
    ok(strcmp(NameA, "REMOTEPC") == 0, "Got name '%s'\n", NameA);

    SetLastError(0xdeadbeef);
    Result = pGetNameA(Server, NameA, sizeof("REMOTEPC") - 1);
    ok(Result == FALSE, "GetNamedPipeClientComputerNameA succeeded with a short buffer\n");
    ok(GetLastError() == ERROR_INSUFFICIENT_BUFFER, "Last error %lu\n", GetLastError());

    DetachRemoteClient();
    DisconnectNamedPipe(Server);
    CloseHandle(Server);
}

static
VOID
TestLongestName(
    _In_ FN_GetNamedPipeClientComputerNameW pGetNameW)
{
    static const WCHAR Longest[] = L"ABCDEFGHIJKLMNO";
    WCHAR NameW[32];
    HANDLE Server;
    DWORD Error;
    BOOL Result;

    Server = CreateServer();
    if (Server == INVALID_HANDLE_VALUE)
        return;

    Error = AttachRemoteClient(Longest, sizeof(Longest) - sizeof(WCHAR));
    ok(Error == ERROR_SUCCESS, "IOCTL_ATTACH_REMOTE_PIPE_CLIENT failed with %lu\n", Error);
    if (Error == ERROR_SUCCESS)
    {
        RtlFillMemory(NameW, sizeof(NameW), 0x55);
        Result = pGetNameW(Server, NameW, sizeof(NameW));
        ok(Result == TRUE, "GetNamedPipeClientComputerNameW failed with %lu\n", GetLastError());
        ok(wcscmp(NameW, Longest) == 0, "Got name '%ls'\n", NameW);
        DetachRemoteClient();
    }

    DisconnectNamedPipe(Server);
    CloseHandle(Server);
}

static
VOID
TestNameTooLong(VOID)
{
    HANDLE Server;
    DWORD Error;

    Server = CreateServer();
    if (Server == INVALID_HANDLE_VALUE)
        return;

    Error = AttachRemoteClient(L"ABCDEFGHIJKLMNOP", 16 * sizeof(WCHAR));
    ok(Error == ERROR_INVALID_PARAMETER, "IOCTL_ATTACH_REMOTE_PIPE_CLIENT returned %lu\n", Error);
    if (Error == ERROR_SUCCESS)
        DetachRemoteClient();

    DisconnectNamedPipe(Server);
    CloseHandle(Server);
}

START_TEST(PipeClientComputerName)
{
    FN_GetNamedPipeClientComputerNameW pGetNameW;
    FN_GetNamedPipeClientComputerNameA pGetNameA;
    HMODULE Kernel32 = GetModuleHandleW(L"kernel32.dll");
    DWORD Error;

    pGetNameW = (FN_GetNamedPipeClientComputerNameW)GetProcAddress(Kernel32, "GetNamedPipeClientComputerNameW");
    pGetNameA = (FN_GetNamedPipeClientComputerNameA)GetProcAddress(Kernel32, "GetNamedPipeClientComputerNameA");
    ok(pGetNameW != NULL, "GetNamedPipeClientComputerNameW is missing\n");
    ok(pGetNameA != NULL, "GetNamedPipeClientComputerNameA is missing\n");
    if (pGetNameW == NULL || pGetNameA == NULL)
        return;

    Error = KmtLoadAndOpenDriver(L"kernel32", FALSE);
    ok_eq_int(Error, ERROR_SUCCESS);
    if (Error)
        return;

    TestRemoteName(pGetNameW, pGetNameA);
    TestLongestName(pGetNameW);
    TestNameTooLong();

    KmtCloseDriver();
    KmtUnloadDriver();
}
