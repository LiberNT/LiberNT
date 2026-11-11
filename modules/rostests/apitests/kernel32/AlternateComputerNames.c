/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test AddLocalAlternateComputerNameA/W
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"

typedef DWORD (WINAPI *PFN_ADD_ALT_NAME_W)(LPCWSTR, ULONG);
typedef DWORD (WINAPI *PFN_ADD_ALT_NAME_A)(LPCSTR, ULONG);

static const WCHAR DnscacheParameters[] = L"System\\CurrentControlSet\\Services\\Dnscache\\Parameters";
static const WCHAR AlternateNamesValue[] = L"AlternateComputerNames";

static PFN_ADD_ALT_NAME_W pAddLocalAlternateComputerNameW;
static PFN_ADD_ALT_NAME_A pAddLocalAlternateComputerNameA;

static
ULONG
CountListEntries(
    _In_ HKEY Key,
    _In_ PCWSTR Name,
    _Out_ PDWORD Type)
{
    WCHAR Buffer[4096];
    DWORD Size = sizeof(Buffer) - 2 * sizeof(WCHAR);
    ULONG Count = 0;
    PCWSTR Entry;
    LONG Error;

    *Type = REG_NONE;
    RtlZeroMemory(Buffer, sizeof(Buffer));
    Error = RegQueryValueExW(Key, AlternateNamesValue, NULL, Type, (LPBYTE)Buffer, &Size);
    if (Error != ERROR_SUCCESS)
        return 0;

    for (Entry = Buffer; *Entry != UNICODE_NULL; Entry += wcslen(Entry) + 1)
    {
        if (_wcsicmp(Entry, Name) == 0)
            Count++;
    }
    return Count;
}

static
VOID
TestInvalidParameters(VOID)
{
    WCHAR LongName[300];
    ULONG Index;

    SetLastError(0xdeadbeef);
    ok_long(pAddLocalAlternateComputerNameW(L"libernt-apitest-flags.example", 1), ERROR_INVALID_PARAMETER);
    ok_long(GetLastError(), 0xdeadbeef);
    ok_long(pAddLocalAlternateComputerNameW(L"libernt-apitest-flags.example", 0x80000000), ERROR_INVALID_PARAMETER);
    ok_long(pAddLocalAlternateComputerNameW(NULL, 0), ERROR_INVALID_PARAMETER);
    ok_long(pAddLocalAlternateComputerNameW(L"", 0), ERROR_INVALID_PARAMETER);
    ok_long(pAddLocalAlternateComputerNameW(L"bad name.example", 0), ERROR_INVALID_PARAMETER);
    ok_long(pAddLocalAlternateComputerNameW(L"bad..name.example", 0), ERROR_INVALID_PARAMETER);
    ok_long(pAddLocalAlternateComputerNameW(L"bad:name.example", 0), ERROR_INVALID_PARAMETER);
    ok_long(pAddLocalAlternateComputerNameW(L"bad*name.example", 0), ERROR_INVALID_PARAMETER);
    ok_long(pAddLocalAlternateComputerNameW(L"12345", 0), ERROR_INVALID_PARAMETER);

    for (Index = 0; Index < 64; Index++)
        LongName[Index] = L'a';
    wcscpy(&LongName[64], L".example");
    ok_long(pAddLocalAlternateComputerNameW(LongName, 0), ERROR_INVALID_PARAMETER);

    for (Index = 0; Index < 256; Index++)
        LongName[Index] = (Index % 32 == 31) ? L'.' : L'b';
    LongName[256] = UNICODE_NULL;
    ok_long(pAddLocalAlternateComputerNameW(LongName, 0), ERROR_INVALID_PARAMETER);

    if (pAddLocalAlternateComputerNameA)
    {
        ok_long(pAddLocalAlternateComputerNameA("libernt-apitest-flags.example", 2), ERROR_INVALID_PARAMETER);
        ok_long(pAddLocalAlternateComputerNameA(NULL, 0), ERROR_INVALID_PARAMETER);
        ok_long(pAddLocalAlternateComputerNameA("", 0), ERROR_INVALID_PARAMETER);
        ok_long(pAddLocalAlternateComputerNameA("bad name.example", 0), ERROR_INVALID_PARAMETER);
    }
}

static
VOID
TestAddNames(
    _In_ HKEY Key)
{
    DWORD Type;
    DWORD Error;

    SetLastError(0xdeadbeef);
    Error = pAddLocalAlternateComputerNameW(L"libernt-apitest-alt1.example.com", 0);
    if (Error == ERROR_ACCESS_DENIED)
    {
        skip("AddLocalAlternateComputerNameW needs administrator rights\n");
        return;
    }
    ok_long(Error, ERROR_SUCCESS);
    ok_long(GetLastError(), 0xdeadbeef);
    ok_long(CountListEntries(Key, L"libernt-apitest-alt1.example.com", &Type), 1);
    ok_long(Type, REG_MULTI_SZ);

    ok_long(pAddLocalAlternateComputerNameW(L"LIBERNT-APITEST-ALT1.example.com", 0), ERROR_SUCCESS);
    ok_long(CountListEntries(Key, L"libernt-apitest-alt1.example.com", &Type), 1);

    ok_long(pAddLocalAlternateComputerNameW(L"libernt-apitest-alt2.example.com", 0), ERROR_SUCCESS);
    ok_long(CountListEntries(Key, L"libernt-apitest-alt1.example.com", &Type), 1);
    ok_long(CountListEntries(Key, L"libernt-apitest-alt2.example.com", &Type), 1);
    ok_long(Type, REG_MULTI_SZ);

    ok_long(pAddLocalAlternateComputerNameW(L"libernt_apitest_alt3.example.com", 0), ERROR_SUCCESS);
    ok_long(CountListEntries(Key, L"libernt_apitest_alt3.example.com", &Type), 1);

    if (pAddLocalAlternateComputerNameA)
    {
        ok_long(pAddLocalAlternateComputerNameA("libernt-apitest-alt4.example.com", 0), ERROR_SUCCESS);
        ok_long(CountListEntries(Key, L"libernt-apitest-alt4.example.com", &Type), 1);
        ok_long(CountListEntries(Key, L"libernt-apitest-alt1.example.com", &Type), 1);
    }
}

START_TEST(AlternateComputerNames)
{
    HMODULE Kernel32 = GetModuleHandleW(L"kernel32.dll");
    BYTE Saved[8192];
    DWORD SavedSize = sizeof(Saved), SavedType = REG_NONE;
    LONG SavedError, Error;
    HKEY Key;

    pAddLocalAlternateComputerNameW = (PFN_ADD_ALT_NAME_W)GetProcAddress(Kernel32, "AddLocalAlternateComputerNameW");
    pAddLocalAlternateComputerNameA = (PFN_ADD_ALT_NAME_A)GetProcAddress(Kernel32, "AddLocalAlternateComputerNameA");
    if (!pAddLocalAlternateComputerNameW)
    {
        skip("AddLocalAlternateComputerNameW is not available\n");
        return;
    }
    ok(pAddLocalAlternateComputerNameA != NULL, "AddLocalAlternateComputerNameA is not exported\n");

    TestInvalidParameters();

    Error = RegOpenKeyExW(HKEY_LOCAL_MACHINE, DnscacheParameters, 0, KEY_QUERY_VALUE | KEY_SET_VALUE, &Key);
    if (Error != ERROR_SUCCESS)
    {
        skip("Cannot open the DNS client parameters key: %ld\n", Error);
        return;
    }

    SavedError = RegQueryValueExW(Key, AlternateNamesValue, NULL, &SavedType, Saved, &SavedSize);
    ok(SavedError == ERROR_SUCCESS || SavedError == ERROR_FILE_NOT_FOUND,
       "Unexpected error %ld reading the alternate names\n", SavedError);

    TestAddNames(Key);

    if (SavedError == ERROR_SUCCESS)
        Error = RegSetValueExW(Key, AlternateNamesValue, 0, SavedType, Saved, SavedSize);
    else
        Error = RegDeleteValueW(Key, AlternateNamesValue);
    ok(Error == ERROR_SUCCESS || (SavedError != ERROR_SUCCESS && Error == ERROR_FILE_NOT_FOUND),
       "Restoring the alternate names failed with %ld\n", Error);

    RegCloseKey(Key);
}
