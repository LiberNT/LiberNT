/*
 * PROJECT:     LiberNT KernelBase
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     AppInit DLL loader entry point
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntstatus.h>
#define WIN32_NO_STATUS
#include <windef.h>
#include <winbase.h>
#include <winreg.h>
#include <winternl.h>
#include <ddk/wdm.h>
#include <softpub.h>

#ifndef SHARED_GLOBAL_FLAGS_SECURE_BOOT_ENABLED
#define SHARED_GLOBAL_FLAGS_SECURE_BOOT_ENABLED (1UL << 7)
#endif

static const KUSER_SHARED_DATA *user_shared_data = (const KUSER_SHARED_DATA *)0x7ffe0000;

typedef LONG (WINAPI *PFN_WIN_VERIFY_TRUST)(HWND, GUID *, LPVOID);

static LONG AppInitDllsDone;

static
DWORD
AppInitQueryDword(
    _In_ HKEY Key,
    _In_ PCWSTR Name)
{
    DWORD Value = 0, Size = sizeof(Value), Type;

    if (RegQueryValueExW(Key, Name, NULL, &Type, (LPBYTE)&Value, &Size) != ERROR_SUCCESS ||
        Type != REG_DWORD || Size != sizeof(Value))
    {
        return 0;
    }
    return Value;
}

static
PWSTR
AppInitQueryList(
    _In_ HKEY Key)
{
    DWORD Size = 0, Type;
    PWSTR List;

    if (RegQueryValueExW(Key, L"AppInit_DLLs", NULL, &Type, NULL, &Size) != ERROR_SUCCESS ||
        Type != REG_SZ || Size < sizeof(WCHAR))
    {
        return NULL;
    }

    List = HeapAlloc(GetProcessHeap(), 0, Size + sizeof(WCHAR));
    if (List == NULL)
        return NULL;

    if (RegQueryValueExW(Key, L"AppInit_DLLs", NULL, &Type, (LPBYTE)List, &Size) != ERROR_SUCCESS ||
        Type != REG_SZ)
    {
        HeapFree(GetProcessHeap(), 0, List);
        return NULL;
    }
    List[Size / sizeof(WCHAR)] = UNICODE_NULL;
    return List;
}

static
BOOL
AppInitIsCodeSigned(
    _In_ PCWSTR Name,
    _Inout_ HMODULE *Wintrust)
{
    GUID Action = WINTRUST_ACTION_GENERIC_VERIFY_V2;
    WINTRUST_FILE_INFO File;
    WINTRUST_DATA Data;
    PFN_WIN_VERIFY_TRUST pWinVerifyTrust;
    WCHAR Path[MAX_PATH];

    if (!SearchPathW(NULL, Name, NULL, RTL_NUMBER_OF(Path), Path, NULL))
        return FALSE;

    if (*Wintrust == NULL)
        *Wintrust = LoadLibraryW(L"wintrust.dll");
    if (*Wintrust == NULL)
        return FALSE;
    pWinVerifyTrust = (PFN_WIN_VERIFY_TRUST)GetProcAddress(*Wintrust, "WinVerifyTrust");
    if (pWinVerifyTrust == NULL)
        return FALSE;

    RtlZeroMemory(&File, sizeof(File));
    File.cbStruct = sizeof(File);
    File.pcwszFilePath = Path;

    RtlZeroMemory(&Data, sizeof(Data));
    Data.cbStruct = sizeof(Data);
    Data.dwUIChoice = WTD_UI_NONE;
    Data.fdwRevocationChecks = WTD_REVOKE_NONE;
    Data.dwUnionChoice = WTD_CHOICE_FILE;
    Data.pFile = &File;
    Data.dwStateAction = WTD_STATEACTION_IGNORE;
    Data.dwProvFlags = WTD_CACHE_ONLY_URL_RETRIEVAL;

    return pWinVerifyTrust(INVALID_HANDLE_VALUE, &Action, &Data) == ERROR_SUCCESS;
}

VOID
WINAPI
LoadAppInitDlls(VOID)
{
    PROCESS_MITIGATION_EXTENSION_POINT_DISABLE_POLICY Policy;
    HMODULE Wintrust = NULL;
    DWORD Load, RequireSigned;
    PWSTR List, Entry;
    HKEY Key;

    if (InterlockedCompareExchange(&AppInitDllsDone, 1, 0) != 0)
        return;

    if (user_shared_data->SharedDataFlags & SHARED_GLOBAL_FLAGS_SECURE_BOOT_ENABLED)
        return;

    if (GetProcessMitigationPolicy(GetCurrentProcess(), ProcessExtensionPointDisablePolicy, &Policy, sizeof(Policy)) &&
        Policy.DisableExtensionPoints)
    {
        return;
    }

    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE,
                      L"Software\\Microsoft\\Windows NT\\CurrentVersion\\Windows",
                      0,
                      KEY_QUERY_VALUE,
                      &Key) != ERROR_SUCCESS)
    {
        return;
    }
    Load = AppInitQueryDword(Key, L"LoadAppInit_DLLs");
    RequireSigned = AppInitQueryDword(Key, L"RequireSignedAppInit_DLLs");
    List = (Load != 0) ? AppInitQueryList(Key) : NULL;
    RegCloseKey(Key);
    if (List == NULL)
        return;

    for (Entry = List; *Entry != UNICODE_NULL; )
    {
        PWSTR End = Entry;

        if (*Entry == L' ' || *Entry == L',')
        {
            Entry++;
            continue;
        }
        while (*End != UNICODE_NULL && *End != L' ' && *End != L',')
            End++;
        if (*End != UNICODE_NULL)
            *End++ = UNICODE_NULL;

        if (RequireSigned == 0 || AppInitIsCodeSigned(Entry, &Wintrust))
            LoadLibraryW(Entry);
        Entry = End;
    }

    HeapFree(GetProcessHeap(), 0, List);
}
