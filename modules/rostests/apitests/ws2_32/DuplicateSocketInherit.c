/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     A socket made from WSADuplicateSocket data can be handed to a child process
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "ws2_32.h"
#include <strsafe.h>

static int
ChildUseSocket(const char *Handle)
{
    WSADATA Data;
    SOCKET Socket = (SOCKET)(ULONG_PTR)_strtoui64(Handle, NULL, 16);
    struct sockaddr_in Address;
    int Length = sizeof(Address);

    WSAStartup(MAKEWORD(2, 2), &Data);
    if (getsockname(Socket, (struct sockaddr *)&Address, &Length) != 0)
        return 100 + (WSAGetLastError() % 100);
    return ntohs(Address.sin_port) ? 0 : 3;
}

START_TEST(DuplicateSocketInherit)
{
    STARTUPINFOEXW StartupInfo = { { sizeof(StartupInfo) } };
    struct sockaddr_in Address = { 0 };
    PROCESS_INFORMATION Process;
    WCHAR Module[MAX_PATH], CommandLine[MAX_PATH + 64];
    WSAPROTOCOL_INFOW ProtocolInfo;
    SOCKET Socket, Duplicate;
    HANDLE Handle;
    DWORD Flags = 0, ExitCode = 0;
    SIZE_T Size = 0;
    WSADATA Data;
    char **Argv;
    int Argc = winetest_get_mainargs(&Argv);
    BOOL Result;

    if (Argc >= 4 && !strcmp(Argv[2], "child"))
        exit(ChildUseSocket(Argv[3]));

    ok(WSAStartup(MAKEWORD(2, 2), &Data) == 0, "WSAStartup failed\n");
    Socket = WSASocketW(AF_INET, SOCK_STREAM, IPPROTO_TCP, NULL, 0, WSA_FLAG_OVERLAPPED);
    ok(Socket != INVALID_SOCKET, "WSASocketW failed %d\n", WSAGetLastError());
    if (Socket == INVALID_SOCKET)
        return;
    Address.sin_family = AF_INET;
    Address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    ok(bind(Socket, (struct sockaddr *)&Address, sizeof(Address)) == 0, "bind failed %d\n", WSAGetLastError());
    ok(GetHandleInformation((HANDLE)Socket, &Flags), "GetHandleInformation failed %lu\n", GetLastError());
    ok(Flags & HANDLE_FLAG_INHERIT, "A new socket is not inheritable (flags %lx)\n", Flags);

    ok(WSADuplicateSocketW(Socket, GetCurrentProcessId(), &ProtocolInfo) == 0,
       "WSADuplicateSocketW failed %d\n", WSAGetLastError());
    Duplicate = WSASocketW(FROM_PROTOCOL_INFO, FROM_PROTOCOL_INFO, FROM_PROTOCOL_INFO, &ProtocolInfo, 0, WSA_FLAG_OVERLAPPED);
    ok(Duplicate != INVALID_SOCKET, "WSASocketW(FROM_PROTOCOL_INFO) failed %d\n", WSAGetLastError());
    if (Duplicate == INVALID_SOCKET)
    {
        closesocket(Socket);
        return;
    }
    Flags = 0;
    ok(GetHandleInformation((HANDLE)Duplicate, &Flags), "GetHandleInformation failed %lu\n", GetLastError());
    ok(Flags & HANDLE_FLAG_INHERIT, "The socket made from the duplicate is not inheritable (flags %lx)\n", Flags);

    InitializeProcThreadAttributeList(NULL, 1, 0, &Size);
    StartupInfo.lpAttributeList = HeapAlloc(GetProcessHeap(), 0, Size);
    ok(InitializeProcThreadAttributeList(StartupInfo.lpAttributeList, 1, 0, &Size),
       "InitializeProcThreadAttributeList failed %lu\n", GetLastError());
    Handle = (HANDLE)Duplicate;
    ok(UpdateProcThreadAttribute(StartupInfo.lpAttributeList, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, &Handle, sizeof(Handle), NULL, NULL),
       "UpdateProcThreadAttribute failed %lu\n", GetLastError());
    GetModuleFileNameW(NULL, Module, _countof(Module));
    StringCchPrintfW(CommandLine, _countof(CommandLine), L"\"%s\" DuplicateSocketInherit child %Ix", Module, (ULONG_PTR)Duplicate);
    SetLastError(0xdeadbeef);
    Result = CreateProcessW(NULL, CommandLine, NULL, NULL, TRUE, EXTENDED_STARTUPINFO_PRESENT, NULL, NULL,
                            &StartupInfo.StartupInfo, &Process);
    ok(Result, "CreateProcessW with the socket in the handle list failed %lu\n", GetLastError());
    if (Result)
    {
        ok(WaitForSingleObject(Process.hProcess, 20000) == WAIT_OBJECT_0, "The child did not end\n");
        GetExitCodeProcess(Process.hProcess, &ExitCode);
        ok(ExitCode == 0, "The child could not use the inherited socket (exit code %lu)\n", ExitCode);
        CloseHandle(Process.hProcess);
        CloseHandle(Process.hThread);
    }
    DeleteProcThreadAttributeList(StartupInfo.lpAttributeList);
    HeapFree(GetProcessHeap(), 0, StartupInfo.lpAttributeList);
    closesocket(Duplicate);
    closesocket(Socket);
}
