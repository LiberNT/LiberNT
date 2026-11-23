/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test that the Remote Desktop server does not keep the user's password once the user is signed in
 * COPYRIGHT:   Copyright 2026 mikuta0407 <mikuta0407@mikuta0407.net>
 */

#include <apitest.h>
#include <windows.h>
#include <lm.h>
#include <tlhelp32.h>
#include <stdio.h>
#include "rdpclient.h"

static BOOL
Contains(const BYTE *Data, SIZE_T Size, const void *Pattern, SIZE_T Length)
{
    SIZE_T i;

    for (i = 0; i + Length <= Size; i++)
    {
        if (Data[i] == *(const BYTE *)Pattern && !memcmp(Data + i, Pattern, Length))
            return TRUE;
    }
    return FALSE;
}

static ULONG
CopiesInProcess(DWORD ProcessId, const char *Password, const WCHAR *WidePassword)
{
    MEMORY_BASIC_INFORMATION Info;
    SIZE_T Read, Length = strlen(Password);
    ULONG Copies = 0;
    PBYTE Address = NULL, Buffer;
    HANDLE Process;

    Process = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, ProcessId);
    ok(Process != NULL, "Cannot open rdpserver.exe %lu: %lu\n", ProcessId, GetLastError());
    if (!Process)
        return 0;
    while (VirtualQueryEx(Process, Address, &Info, sizeof(Info)) == sizeof(Info))
    {
        if (Info.State == MEM_COMMIT && Info.Type == MEM_PRIVATE && !(Info.Protect & (PAGE_GUARD | PAGE_NOACCESS)))
        {
            Buffer = HeapAlloc(GetProcessHeap(), 0, Info.RegionSize);
            if (Buffer && ReadProcessMemory(Process, Info.BaseAddress, Buffer, Info.RegionSize, &Read))
            {
                if (Contains(Buffer, Read, Password, Length))
                {
                    trace("process %lu holds the password at %p (%Iu bytes)\n", ProcessId, Info.BaseAddress, Info.RegionSize);
                    Copies++;
                }
                if (Contains(Buffer, Read, WidePassword, Length * sizeof(WCHAR)))
                {
                    trace("process %lu holds the UTF-16 password at %p (%Iu bytes)\n", ProcessId, Info.BaseAddress, Info.RegionSize);
                    Copies++;
                }
            }
            HeapFree(GetProcessHeap(), 0, Buffer);
        }
        Address = (PBYTE)Info.BaseAddress + Info.RegionSize;
        if (Address < (PBYTE)Info.BaseAddress)
            break;
    }
    CloseHandle(Process);
    return Copies;
}

static ULONG
CopiesInServers(ULONG *Servers)
{
    PROCESSENTRY32W Entry = { sizeof(Entry) };
    WCHAR WidePassword[32];
    ULONG Copies = 0;
    HANDLE Snapshot;

    *Servers = 0;
    MultiByteToWideChar(CP_ACP, 0, RdpTestPassword(), -1, WidePassword, ARRAYSIZE(WidePassword));
    Snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (Snapshot == INVALID_HANDLE_VALUE)
        return 0;
    if (Process32FirstW(Snapshot, &Entry))
    {
        do
        {
            if (lstrcmpiW(Entry.szExeFile, L"rdpserver.exe"))
                continue;
            ++*Servers;
            Copies += CopiesInProcess(Entry.th32ProcessID, RdpTestPassword(), WidePassword);
        } while (Process32NextW(Snapshot, &Entry));
    }
    CloseHandle(Snapshot);
    return Copies;
}

START_TEST(PasswordMemory)
{
    RDP_TEST_SETUP Setup;
    RDP_CLIENT Client;
    WCHAR User[16];
    char UserA[16];
    ULONG Copies, Servers;

    _snwprintf(User, ARRAYSIZE(User), L"rdppw%06lx", GetTickCount() & 0xffffff);
    _snprintf(UserA, sizeof(UserA), "%ls", User);
    if (!RdpTestEnableDebugPrivilege())
    {
        skip("Reading the server's memory needs SeDebugPrivilege\n");
        return;
    }
    if (!RdpTestStart(&Setup))
        return;
    if (RdpTestAddUser(User))
    {
        RdpClientStart(&Client, UserA, UserA);
        ok(RdpClientWaitConnected(&Client), "%s could not connect: 0x%08x\n", UserA, Client.Error);
        if (Client.Alive)
        {
            Sleep(3000);
            Copies = CopiesInServers(&Servers);
            ok(Servers >= 2, "Only %lu rdpserver.exe processes run\n", Servers);
            ok(Copies == 0, "%lu regions of the Remote Desktop servers still hold the signed-in user's password\n",
               Copies);
        }
        RdpClientStop(&Client);
    }
    NetUserDel(NULL, User);
    RdpTestEnd(&Setup);
}
