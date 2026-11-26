/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Registration of iexplore.exe that applications look up
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <apitest.h>
#include <shlwapi.h>
#include <strsafe.h>

static BOOL
ReadValue(HKEY Root, PCWSTR Key, PCWSTR Name, PWSTR Value, DWORD Characters)
{
    WCHAR Raw[MAX_PATH * 2];
    DWORD Size = sizeof(Raw), Type = 0;
    LSTATUS Error = RegGetValueW(Root, Key, Name, RRF_RT_REG_SZ | RRF_RT_REG_EXPAND_SZ | RRF_NOEXPAND, &Type, Raw,
                                 &Size);

    Value[0] = 0;
    if (Error != ERROR_SUCCESS)
        return FALSE;
    if (Type == REG_EXPAND_SZ)
        return ExpandEnvironmentStringsW(Raw, Value, Characters) != 0;
    return SUCCEEDED(StringCchCopyW(Value, Characters, Raw));
}

static BOOL
EndsWithNoCase(PCWSTR Text, PCWSTR Suffix)
{
    size_t TextLength = wcslen(Text), SuffixLength = wcslen(Suffix);

    return TextLength >= SuffixLength && !_wcsicmp(Text + TextLength - SuffixLength, Suffix);
}

static BOOL
CommandRunsBrowser(PCWSTR Command, PWSTR Program, DWORD Characters)
{
    PCWSTR Start = Command, End;

    if (*Start == L'"')
    {
        Start++;
        End = wcschr(Start, L'"');
    }
    else
    {
        End = StrStrIW(Start, L".exe");
        End = End ? End + 4 : NULL;
    }
    if (!End || (DWORD)(End - Start) >= Characters)
        return FALSE;
    CopyMemory(Program, Start, (End - Start) * sizeof(WCHAR));
    Program[End - Start] = 0;
    return EndsWithNoCase(Program, L"\\Internet Explorer\\iexplore.exe");
}

static void
CheckCommand(HKEY Root, PCWSTR Key)
{
    WCHAR Command[MAX_PATH * 2], Program[MAX_PATH];

    ok(ReadValue(Root, Key, NULL, Command, ARRAYSIZE(Command)), "%ls has no command\n", Key);
    ok(CommandRunsBrowser(Command, Program, ARRAYSIZE(Program)), "%ls runs \"%ls\"\n", Key, Command);
    ok(GetFileAttributesW(Program) != INVALID_FILE_ATTRIBUTES, "%ls: \"%ls\" is missing\n", Key, Program);
}

START_TEST(Registration)
{
    WCHAR Value[MAX_PATH * 2], Folder[MAX_PATH], Expected[MAX_PATH];
    DWORD Size = ARRAYSIZE(Value);
    HRESULT hr;

    ok(ExpandEnvironmentStringsW(L"%ProgramFiles%\\Internet Explorer\\iexplore.exe", Expected, ARRAYSIZE(Expected)),
       "ExpandEnvironmentStrings failed\n");
    ok(GetFileAttributesW(Expected) != INVALID_FILE_ATTRIBUTES, "\"%ls\" is missing\n", Expected);

    ok(ReadValue(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\App Paths\\IEXPLORE.EXE", NULL, Value,
                 ARRAYSIZE(Value)),
       "App Paths has no IEXPLORE.EXE\n");
    ok(!_wcsicmp(Value, Expected), "App Paths points to \"%ls\", expected \"%ls\"\n", Value, Expected);
    ok(ReadValue(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\App Paths\\IEXPLORE.EXE", L"Path",
                 Value, ARRAYSIZE(Value)),
       "App Paths has no Path for IEXPLORE.EXE\n");
    StringCchCopyW(Folder, ARRAYSIZE(Folder), Expected);
    PathRemoveFileSpecW(Folder);
    StringCchCatW(Folder, ARRAYSIZE(Folder), L";");
    ok(!_wcsicmp(Value, Folder), "App Paths Path is \"%ls\", expected \"%ls\"\n", Value, Folder);

    CheckCommand(HKEY_CLASSES_ROOT, L"IE.HTTP\\shell\\open\\command");
    CheckCommand(HKEY_CLASSES_ROOT, L"IE.HTTPS\\shell\\open\\command");
    CheckCommand(HKEY_CLASSES_ROOT, L"Applications\\iexplore.exe\\shell\\open\\command");
    CheckCommand(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Clients\\StartMenuInternet\\IEXPLORE.EXE\\shell\\open\\command");
    ok(ReadValue(HKEY_CLASSES_ROOT, L"IE.HTTP", L"URL Protocol", Value, ARRAYSIZE(Value)), "IE.HTTP is no protocol\n");

    hr = AssocQueryStringW(ASSOCF_NONE, ASSOCSTR_EXECUTABLE, L"http", L"open", Value, &Size);
    ok(hr == S_OK, "no program opens http addresses: 0x%lx\n", hr);
    if (hr == S_OK)
        ok(GetFileAttributesW(Value) != INVALID_FILE_ATTRIBUTES, "the http program \"%ls\" is missing\n", Value);
    Size = ARRAYSIZE(Value);
    hr = AssocQueryStringW(ASSOCF_NONE, ASSOCSTR_EXECUTABLE, L"https", L"open", Value, &Size);
    ok(hr == S_OK, "no program opens https addresses: 0x%lx\n", hr);
}
