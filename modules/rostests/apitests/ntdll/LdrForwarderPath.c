/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test the search path of forwarders resolved by GetProcAddress
 * COPYRIGHT:   Copyright 2026 mikuta0407 <mikuta0407@mikuta0407.net>
 */

#include "precomp.h"
#include <strsafe.h>

#define RESULT_NOT_LOADED 0x20000

static WCHAR s_szDir[MAX_PATH];

static BOOL ExtractDll(PCWSTR pszName, UINT nId)
{
    WCHAR szPath[MAX_PATH];
    DWORD dwWritten;
    HRSRC hRsrc;
    HGLOBAL hGlobal;
    HANDLE hFile;
    BOOL bOk;

    hRsrc = FindResourceW(NULL, (PCWSTR)(ULONG_PTR)nId, (PCWSTR)(ULONG_PTR)10);
    if (!hRsrc || !(hGlobal = LoadResource(NULL, hRsrc)))
        return FALSE;
    StringCchPrintfW(szPath, _countof(szPath), L"%s\\%s", s_szDir, pszName);
    hFile = CreateFileW(szPath, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE)
        return FALSE;
    bOk = WriteFile(hFile, LockResource(hGlobal), SizeofResource(NULL, hRsrc), &dwWritten, NULL);
    CloseHandle(hFile);
    return bOk;
}

static DWORD RunChild(PCSTR pszExport, PCSTR pszFlags)
{
    WCHAR szExe[MAX_PATH], szCmd[MAX_PATH * 3];
    STARTUPINFOW si = { sizeof(si) };
    PROCESS_INFORMATION pi;
    DWORD dwExit = MAXDWORD;

    GetModuleFileNameW(NULL, szExe, _countof(szExe));
    StringCchPrintfW(szCmd, _countof(szCmd), L"\"%s\" LdrForwarderPath %S \"%s\" %S", szExe, pszExport, s_szDir, pszFlags);
    if (!CreateProcessW(NULL, szCmd, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi))
        return MAXDWORD;
    if (WaitForSingleObject(pi.hProcess, 30000) != WAIT_OBJECT_0)
    {
        TerminateProcess(pi.hProcess, MAXDWORD);
        WaitForSingleObject(pi.hProcess, 5000);
    }
    GetExitCodeProcess(pi.hProcess, &dwExit);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return dwExit;
}

static void Child(int argc, char **argv)
{
    WCHAR szPath[MAX_PATH];
    HMODULE hModule = NULL;
    FARPROC pfn;
    int i;

    StringCchPrintfW(szPath, _countof(szPath), L"%S\\ldrfwd_a.dll", argv[3]);
    for (i = 4; i < argc; i++)
    {
        hModule = LoadLibraryExW(szPath, NULL, strtoul(argv[i], NULL, 16));
        if (!hModule)
            ExitProcess(RESULT_NOT_LOADED | GetLastError());
    }
    if (!strcmp(argv[2], "Diamond"))
    {
        HMODULE Middle, Leaf;
        /* Resolving A.Bar adds A->B and A->C. Resolving B.Bar adds B->C. */
        pfn = GetProcAddress(hModule, "Bar");
        Middle = GetModuleHandleW(L"ldrfwd_b.dll");
        Leaf = GetModuleHandleW(L"ldrfwd_c.dll");
        if (!pfn || !Middle || !Leaf || GetProcAddress(Middle, "Bar") != pfn)
            ExitProcess(ERROR_PROC_NOT_FOUND);
        if (!FreeLibrary(Leaf) || GetModuleHandleW(L"ldrfwd_c.dll") != Leaf ||
            !FreeLibrary(hModule) || GetModuleHandleW(L"ldrfwd_b.dll") ||
            GetModuleHandleW(L"ldrfwd_c.dll"))
            ExitProcess(ERROR_INVALID_DATA);
        ExitProcess(0);
    }
    pfn = GetProcAddress(hModule, argv[2]);
    if (!pfn)
        ExitProcess(GetLastError());
    if (NtCurrentPeb()->OSMajorVersion >= 10)
    {
        PLDR_DATA_TABLE_ENTRY Entry, Owner;
        PLDR_DEPENDENCY_RECORD Edge;
        NTSTATUS Status = LdrFindEntryForAddress((PVOID)pfn, &Entry);
        if (!NT_SUCCESS(Status) || Entry->ProcessStaticImport ||
            Entry->LoadReason != LoadReasonDynamicForwarderDependency)
        {
            trace("GetProcAddress forwarder has unexpected loader state, status %#lx\n", Status);
            ExitProcess(ERROR_INVALID_DATA);
        }
        if (!Entry->DdagNode || !Entry->DdagNode->IncomingDependencies.Tail ||
            !NT_SUCCESS(LdrFindEntryForAddress(hModule, &Owner)))
            ExitProcess(ERROR_INVALID_DATA);
        Edge = CONTAINING_RECORD(Entry->DdagNode->IncomingDependencies.Tail,
                                 LDR_DEPENDENCY_RECORD, IncomingDependencyLink);
        if ((ULONG_PTR)Edge->IncomingDependencyNode != ((ULONG_PTR)Owner->DdagNode | 1))
        {
            trace("Wrong owner of the final forwarded export\n");
            ExitProcess(ERROR_INVALID_DATA);
        }
    }
    ExitProcess(((int (WINAPI *)(void))pfn)());
}

static void Cleanup(void)
{
    static const PCWSTR s_apszNames[] = { L"ldrfwd_a.dll", L"ldrfwd_b.dll", L"ldrfwd_c.dll" };
    WCHAR szPath[MAX_PATH];
    UINT i;

    for (i = 0; i < _countof(s_apszNames); i++)
    {
        StringCchPrintfW(szPath, _countof(szPath), L"%s\\%s", s_szDir, s_apszNames[i]);
        DeleteFileW(szPath);
    }
    RemoveDirectoryW(s_szDir);
}

START_TEST(LdrForwarderPath)
{
    char **argv;
    int argc;
    WCHAR szTemp[MAX_PATH];

    argc = winetest_get_mainargs(&argv);
    if (argc >= 5)
    {
        Child(argc, argv);
        return;
    }

    GetTempPathW(_countof(szTemp), szTemp);
    StringCchPrintfW(s_szDir, _countof(s_szDir), L"%sldrfwd%lu", szTemp, GetCurrentProcessId());
    CreateDirectoryW(s_szDir, NULL);
    if (!ExtractDll(L"ldrfwd_a.dll", 103) || !ExtractDll(L"ldrfwd_b.dll", 104) || !ExtractDll(L"ldrfwd_c.dll", 105))
    {
        skip("Cannot extract the test DLLs to %ls\n", s_szDir);
        Cleanup();
        return;
    }

    ok_long(RunChild("Foo", "8"), 42);
    ok_long(RunChild("Foo", "100"), 42);
    ok_long(RunChild("Foo", "0"), ERROR_PROC_NOT_FOUND);
    ok_long(RunChild("Foo", "800"), ERROR_PROC_NOT_FOUND);
    ok_long(RunChild("Foo", "1000"), ERROR_PROC_NOT_FOUND);

    ok_long(RunChild("Bar", "8"), 43);
    ok_long(RunChild("Bar", "100"), 43);
    if (NtCurrentPeb()->OSMajorVersion >= 10)
        ok_long(RunChild("Diamond", "8"), 0);
    ok_long(RunChild("Foo", "0 8"), ERROR_PROC_NOT_FOUND);
    ok_long(RunChild("Foo", "8 0"), 42);
    ok_long(RunChild("Foo", "800 100"), ERROR_PROC_NOT_FOUND);

    Cleanup();
}
