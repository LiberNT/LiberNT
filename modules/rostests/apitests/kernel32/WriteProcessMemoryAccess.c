/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests the access rights WriteProcessMemory and VirtualQueryEx need, and page protection handling
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"
#include <psapi.h>

static void TestProtection(HANDLE Process, DWORD Protect, BOOL ExpectWritten, PCSTR What)
{
    static const char Data[] = "lntget";
    char Buffer[sizeof(Data)] = { 0 };
    SIZE_T Written = 0, Read = 0;
    MEMORY_BASIC_INFORMATION Info;
    PVOID Remote;
    BOOL Result;

    Remote = VirtualAllocEx(Process, NULL, 4096, MEM_COMMIT | MEM_RESERVE, Protect);
    ok(Remote != NULL, "%s: VirtualAllocEx(0x%lx) failed with %lu\n", What, Protect, GetLastError());
    if (!Remote)
        return;
    SetLastError(0xdeadbeef);
    Result = WriteProcessMemory(Process, Remote, Data, sizeof(Data), &Written);
    ok(Result == ExpectWritten, "%s: WriteProcessMemory(0x%lx) returned %d error %lu\n", What, Protect, Result, GetLastError());
    if (ExpectWritten)
    {
        ok(Written == sizeof(Data), "%s: wrote %Iu bytes\n", What, (size_t)Written);
        ok(ReadProcessMemory(Process, Remote, Buffer, sizeof(Buffer), &Read) && !memcmp(Buffer, Data, sizeof(Data)),
           "%s: the written bytes did not arrive\n", What);
    }
    if (VirtualQueryEx(Process, Remote, &Info, sizeof(Info)))
        ok(Info.Protect == Protect, "%s: protection changed from 0x%lx to 0x%lx\n", What, Protect, Info.Protect);
    VirtualFreeEx(Process, Remote, 0, MEM_RELEASE);
}

static void TestQueryAccess(DWORD ProcessId, PVOID Remote, DWORD Access, BOOL Granted, PCSTR What)
{
    MEMORY_BASIC_INFORMATION Info;
    WCHAR Name[MAX_PATH];
    HANDLE Process;
    SIZE_T Size;

    Process = OpenProcess(Access, FALSE, ProcessId);
    ok(Process != NULL, "%s: OpenProcess failed with %lu\n", What, GetLastError());
    if (!Process)
        return;
    SetLastError(0xdeadbeef);
    Size = VirtualQueryEx(Process, Remote, &Info, sizeof(Info));
    if (Granted)
    {
        ok(Size == sizeof(Info), "%s: VirtualQueryEx returned %Iu error %lu\n", What, (size_t)Size, GetLastError());
        ok(Size != sizeof(Info) || Info.Protect == PAGE_READWRITE, "%s: protection 0x%lx\n", What, Info.Protect);
    }
    else
    {
        ok(Size == 0 && GetLastError() == ERROR_ACCESS_DENIED, "%s: VirtualQueryEx returned %Iu error %lu\n", What, (size_t)Size, GetLastError());
    }
    SetLastError(0xdeadbeef);
    ok(K32GetMappedFileNameW(Process, Remote, Name, MAX_PATH) == 0, "%s: private memory has a mapped file name\n", What);
    ok(GetLastError() == (Granted ? ERROR_UNEXP_NET_ERR : ERROR_ACCESS_DENIED), "%s: K32GetMappedFileNameW error %lu\n",
       What, GetLastError());
    CloseHandle(Process);
}

START_TEST(WriteProcessMemoryAccess)
{
    STARTUPINFOW Startup = { sizeof(Startup) };
    PROCESS_INFORMATION Child;
    WCHAR Command[MAX_PATH];
    HANDLE Narrow;

    GetSystemDirectoryW(Command, MAX_PATH);
    StringCchCatW(Command, MAX_PATH, L"\\cmd.exe /c exit");
    ok(CreateProcessW(NULL, Command, NULL, NULL, FALSE, CREATE_SUSPENDED | CREATE_NO_WINDOW, NULL, NULL, &Startup, &Child),
       "CreateProcessW failed with %lu\n", GetLastError());
    if (!Child.hProcess)
        return;

    {
        PVOID Remote = VirtualAllocEx(Child.hProcess, NULL, 4096, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);

        ok(Remote != NULL, "VirtualAllocEx failed with %lu\n", GetLastError());
        TestQueryAccess(Child.dwProcessId, Remote, PROCESS_VM_READ, FALSE, "VM_READ");
        TestQueryAccess(Child.dwProcessId, Remote, PROCESS_VM_WRITE, FALSE, "VM_WRITE");
        TestQueryAccess(Child.dwProcessId, Remote, PROCESS_VM_OPERATION, FALSE, "VM_OPERATION");
        TestQueryAccess(Child.dwProcessId, Remote, SYNCHRONIZE, FALSE, "SYNCHRONIZE");
        TestQueryAccess(Child.dwProcessId, Remote, PROCESS_VM_OPERATION | PROCESS_VM_READ, FALSE, "VM_OPERATION|VM_READ");
        TestQueryAccess(Child.dwProcessId, Remote, PROCESS_VM_WRITE | PROCESS_VM_READ, FALSE, "VM_WRITE|VM_READ");
        TestQueryAccess(Child.dwProcessId, Remote, PROCESS_QUERY_LIMITED_INFORMATION, TRUE, "QUERY_LIMITED_INFORMATION");
        TestQueryAccess(Child.dwProcessId, Remote, PROCESS_QUERY_INFORMATION, TRUE, "QUERY_INFORMATION");
        TestQueryAccess(Child.dwProcessId, Remote, PROCESS_VM_OPERATION | PROCESS_VM_WRITE, TRUE, "VM_OPERATION|VM_WRITE");
        VirtualFreeEx(Child.hProcess, Remote, 0, MEM_RELEASE);
    }

    Narrow = OpenProcess(PROCESS_VM_OPERATION | PROCESS_VM_WRITE | PROCESS_VM_READ, FALSE, Child.dwProcessId);
    ok(Narrow != NULL, "OpenProcess failed with %lu\n", GetLastError());
    if (Narrow)
    {
        TestProtection(Narrow, PAGE_READWRITE, TRUE, "narrow handle");
        TestProtection(Narrow, PAGE_EXECUTE_READWRITE, TRUE, "narrow handle");
        TestProtection(Narrow, PAGE_READONLY, FALSE, "narrow handle");
        TestProtection(Narrow, PAGE_EXECUTE_READ, TRUE, "narrow handle");
        CloseHandle(Narrow);
    }
    TestProtection(Child.hProcess, PAGE_READWRITE, TRUE, "full handle");
    TestProtection(Child.hProcess, PAGE_READONLY, FALSE, "full handle");
    TestProtection(Child.hProcess, PAGE_EXECUTE_READ, TRUE, "full handle");

    TerminateProcess(Child.hProcess, 0);
    CloseHandle(Child.hThread);
    CloseHandle(Child.hProcess);
}
