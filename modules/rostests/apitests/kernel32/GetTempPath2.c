/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests for GetTempPath2A/W and kernelbase memory exports
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"

typedef DWORD (WINAPI *PFN_GETTEMPPATH2W)(DWORD, LPWSTR);
typedef DWORD (WINAPI *PFN_GETTEMPPATH2A)(DWORD, LPSTR);

static BOOL
IsLocalSystem(void)
{
    BYTE Buffer[sizeof(TOKEN_USER) + SECURITY_MAX_SID_SIZE];
    SID_IDENTIFIER_AUTHORITY NtAuthority = { SECURITY_NT_AUTHORITY };
    PSID SystemSid;
    HANDLE Token;
    DWORD Length;
    BOOL Result = FALSE;

    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &Token))
        return FALSE;
    if (GetTokenInformation(Token, TokenUser, Buffer, sizeof(Buffer), &Length) &&
        AllocateAndInitializeSid(&NtAuthority, 1, SECURITY_LOCAL_SYSTEM_RID, 0, 0, 0, 0, 0, 0, 0, &SystemSid))
    {
        Result = EqualSid(((PTOKEN_USER)Buffer)->User.Sid, SystemSid);
        FreeSid(SystemSid);
    }
    CloseHandle(Token);
    return Result;
}

static void
TestWide(PFN_GETTEMPPATH2W pGetTempPath2W)
{
    WCHAR Expected[MAX_PATH], Path[MAX_PATH], Small[4];
    DWORD Length, Ret;

    if (IsLocalSystem())
    {
        Length = GetSystemWindowsDirectoryW(Expected, MAX_PATH);
        ok(Length != 0, "GetSystemWindowsDirectoryW failed: %lu\n", GetLastError());
        if (Length && Expected[Length - 1] == L'\\')
            Expected[Length - 1] = UNICODE_NULL;
        wcscat(Expected, L"\\SystemTemp\\");
        Length = (DWORD)wcslen(Expected);
    }
    else
    {
        Length = GetTempPathW(MAX_PATH, Expected);
        ok(Length != 0, "GetTempPathW failed: %lu\n", GetLastError());
    }

    SetLastError(0xdeadbeef);
    Ret = pGetTempPath2W(MAX_PATH, Path);
    ok(Ret == Length, "Expected %lu, got %lu\n", Length, Ret);
    ok(!wcscmp(Path, Expected), "Expected %ls, got %ls\n", Expected, Path);
    ok(GetLastError() == ERROR_SUCCESS, "Expected ERROR_SUCCESS, got %lu\n", GetLastError());

    Small[0] = Small[1] = Small[2] = Small[3] = L'x';
    SetLastError(0xdeadbeef);
    Ret = pGetTempPath2W(3, Small);
    ok(Ret == Length + 1, "Expected %lu, got %lu\n", Length + 1, Ret);
    ok(Small[0] == 0 && Small[1] == 0 && Small[2] == 0, "The buffer was not cleared\n");
    ok(Small[3] == L'x', "The buffer was written past its size\n");
    ok(GetLastError() == ERROR_SUCCESS, "Expected ERROR_SUCCESS, got %lu\n", GetLastError());

    SetLastError(0xdeadbeef);
    Ret = pGetTempPath2W(0, NULL);
    ok(Ret == Length + 1, "Expected %lu, got %lu\n", Length + 1, Ret);
    ok(GetLastError() == ERROR_SUCCESS, "Expected ERROR_SUCCESS, got %lu\n", GetLastError());

    Ret = pGetTempPath2W(Length, Path);
    ok(Ret == Length + 1, "Expected %lu, got %lu\n", Length + 1, Ret);

    Ret = pGetTempPath2W(Length + 1, Path);
    ok(Ret == Length, "Expected %lu, got %lu\n", Length, Ret);
}

static void
TestAnsi(PFN_GETTEMPPATH2W pGetTempPath2W, PFN_GETTEMPPATH2A pGetTempPath2A)
{
    WCHAR PathW[MAX_PATH];
    CHAR Path[MAX_PATH], Expected[MAX_PATH], Small[4];
    DWORD Length, Ret;

    Length = pGetTempPath2W(MAX_PATH, PathW);
    WideCharToMultiByte(CP_ACP, 0, PathW, -1, Expected, MAX_PATH, NULL, NULL);

    SetLastError(0xdeadbeef);
    Ret = pGetTempPath2A(MAX_PATH, Path);
    ok(Ret == Length, "Expected %lu, got %lu\n", Length, Ret);
    ok(!strcmp(Path, Expected), "Expected %s, got %s\n", Expected, Path);
    ok(GetLastError() == ERROR_SUCCESS, "Expected ERROR_SUCCESS, got %lu\n", GetLastError());

    Small[0] = Small[1] = Small[2] = Small[3] = 'x';
    SetLastError(0xdeadbeef);
    Ret = pGetTempPath2A(3, Small);
    ok(Ret == Length + 1, "Expected %lu, got %lu\n", Length + 1, Ret);
    ok(Small[0] == 'x' && Small[3] == 'x', "The buffer was changed\n");
    ok(GetLastError() == ERROR_SUCCESS, "Expected ERROR_SUCCESS, got %lu\n", GetLastError());
}

static void
TestKernelBaseExports(void)
{
    static const char *Names[] =
    {
        "GetTempPath2A", "GetTempPath2W", "GetVolumeNameForVolumeMountPointW",
        "GlobalAlloc", "GlobalFree", "LocalAlloc", "LocalFree"
    };
    HMODULE hKernelBase = GetModuleHandleW(L"kernelbase.dll");
    ULONG i;

    ok(hKernelBase != NULL, "kernelbase.dll is not loaded\n");
    if (!hKernelBase)
        return;

    for (i = 0; i < ARRAYSIZE(Names); i++)
        ok(GetProcAddress(hKernelBase, Names[i]) != NULL, "kernelbase!%s is missing\n", Names[i]);
}

START_TEST(GetTempPath2)
{
    HMODULE hKernel32 = GetModuleHandleW(L"kernel32.dll");
    PFN_GETTEMPPATH2W pGetTempPath2W = (PFN_GETTEMPPATH2W)GetProcAddress(hKernel32, "GetTempPath2W");
    PFN_GETTEMPPATH2A pGetTempPath2A = (PFN_GETTEMPPATH2A)GetProcAddress(hKernel32, "GetTempPath2A");

    TestKernelBaseExports();

    if (!pGetTempPath2W || !pGetTempPath2A)
    {
        skip("GetTempPath2A/W are not exported\n");
        return;
    }

    TestWide(pGetTempPath2W);
    TestAnsi(pGetTempPath2W, pGetTempPath2A);
}
