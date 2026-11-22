/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test the AddReg FLG_ADDREG_32BITKEY and FLG_ADDREG_64BITKEY registry views
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <apitest.h>
#include <windows.h>
#include <setupapi.h>
#include <stdio.h>

static const WCHAR KeyName[] = L"Software\\LiberNTAddRegView";

static const char InfText[] =
    "[Version]\r\n"
    "Signature=\"$Windows NT$\"\r\n"
    "[Install]\r\n"
    "AddReg=Reg\r\n"
    "[Reg]\r\n"
    "HKLM,\"Software\\LiberNTAddRegView\",Native,,\"native\"\r\n"
    "HKLM,\"Software\\LiberNTAddRegView\",View32,0x00004000,\"x86\"\r\n"
    "HKLM,\"Software\\LiberNTAddRegView\",View64,0x00001000,\"x64\"\r\n";

static int
HasValue(REGSAM View, PCWSTR Name, PCWSTR Expected)
{
    WCHAR Data[32];
    DWORD Size = sizeof(Data);
    HKEY Key;
    LONG Error;

    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, KeyName, 0, KEY_QUERY_VALUE | View, &Key) != ERROR_SUCCESS)
        return 0;
    Error = RegQueryValueExW(Key, Name, NULL, NULL, (BYTE *)Data, &Size);
    RegCloseKey(Key);
    if (Error != ERROR_SUCCESS)
        return 0;
    return lstrcmpW(Data, Expected) ? 2 : 1;
}

static void
DeleteTestKeys(void)
{
    RegDeleteKeyExW(HKEY_LOCAL_MACHINE, KeyName, KEY_WOW64_32KEY, 0);
    RegDeleteKeyExW(HKEY_LOCAL_MACHINE, KeyName, KEY_WOW64_64KEY, 0);
}

START_TEST(AddRegRegistryView)
{
    WCHAR TempDir[MAX_PATH], InfPath[MAX_PATH];
    BOOL IsWow64 = FALSE, Is64BitOs;
    REGSAM NativeView;
    HANDLE File;
    DWORD Written;
    HINF Inf;
    BOOL Result;

    IsWow64Process(GetCurrentProcess(), &IsWow64);
    Is64BitOs = sizeof(void *) == 8 || IsWow64;
    NativeView = sizeof(void *) == 8 ? KEY_WOW64_64KEY : KEY_WOW64_32KEY;

    GetTempPathW(ARRAYSIZE(TempDir), TempDir);
    GetTempFileNameW(TempDir, L"arv", 0, InfPath);
    File = CreateFileW(InfPath, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    ok(File != INVALID_HANDLE_VALUE, "CreateFileW failed, error %lu\n", GetLastError());
    if (File == INVALID_HANDLE_VALUE)
        return;
    WriteFile(File, InfText, sizeof(InfText) - 1, &Written, NULL);
    CloseHandle(File);

    DeleteTestKeys();
    Inf = SetupOpenInfFileW(InfPath, NULL, INF_STYLE_WIN4, NULL);
    ok(Inf != INVALID_HANDLE_VALUE, "SetupOpenInfFileW failed, error %lu\n", GetLastError());
    if (Inf == INVALID_HANDLE_VALUE)
    {
        DeleteFileW(InfPath);
        return;
    }

    Result = SetupInstallFromInfSectionW(NULL, Inf, L"Install", SPINST_REGISTRY, NULL, NULL, 0,
                                         NULL, NULL, NULL, NULL);
    if (!Result && GetLastError() == ERROR_ACCESS_DENIED)
    {
        skip("HKLM is not writable\n");
    }
    else
    {
        ok(Result, "SetupInstallFromInfSectionW failed, error %lu\n", GetLastError());
        ok(HasValue(NativeView, L"Native", L"native") == 1, "Native value missing or wrong in the process view\n");
        ok(HasValue(KEY_WOW64_32KEY, L"View32", L"x86") == 1, "View32 value missing or wrong in the 32-bit view\n");
        ok(HasValue(KEY_WOW64_64KEY, L"View64", L"x64") == 1, "View64 value missing or wrong in the 64-bit view\n");
        if (Is64BitOs)
        {
            ok(HasValue(KEY_WOW64_64KEY, L"View32", L"x86") == 0, "View32 value also in the 64-bit view\n");
            ok(HasValue(KEY_WOW64_32KEY, L"View64", L"x64") == 0, "View64 value also in the 32-bit view\n");
        }
    }

    SetupCloseInfFile(Inf);
    DeleteTestKeys();
    DeleteFileW(InfPath);
}
