/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests the package lifecycle of lntget, or of winget through LNTGET_TOOL
 * COPYRIGHT:   Copyright 2026 Ostap Brehin <osbre@protonmail.com>
 */

#include <apitest.h>
#include <windows.h>
#include <strsafe.h>
#include <string.h>

#define TOOL_TIMEOUT (15 * 60 * 1000)
#define REMOVAL_TIMEOUT (20 * 1000)

typedef struct _PACKAGE
{
    PCWSTR Id;
    PCWSTR InstallerType;
    PCWSTR Scope;
    PCWSTR DisplayName;
    PCWSTR File;
} PACKAGE;

static const PACKAGE ExePackage = { L"7zip.7zip", L"exe", NULL, L"7-Zip", L"%ProgramFiles%\\7-Zip\\7z.exe" };
static const PACKAGE InnoPackage = { L"WinSCP.WinSCP", L"inno", L"machine", L"WinSCP", L"%ProgramFiles(x86)%\\WinSCP\\WinSCP.exe" };
static const PACKAGE NullsoftPackage = { L"Notepad++.Notepad++", L"nullsoft", NULL, L"Notepad++", L"%ProgramFiles%\\Notepad++\\notepad++.exe" };
static const PACKAGE PortablePackage = { L"jqlang.jq", L"portable", NULL, L"jq", L"%LOCALAPPDATA%\\Microsoft\\WinGet\\Links\\jq.exe" };
static const PACKAGE WixPackage = { L"7zip.7zip", L"wix", NULL, L"7-Zip", L"%ProgramFiles%\\7-Zip\\7z.exe" };

static char Output[64 * 1024];

static PCWSTR
GetTool(void)
{
    static WCHAR Tool[MAX_PATH];

    if (!Tool[0] && !GetEnvironmentVariableW(L"LNTGET_TOOL", Tool, _countof(Tool)))
        wcscpy(Tool, L"lntget.exe");
    return Tool;
}

static BOOL
RunTool(PCWSTR Arguments, DWORD *ExitCode)
{
    SECURITY_ATTRIBUTES Inherit = { sizeof(Inherit), NULL, TRUE };
    WCHAR OutputPath[MAX_PATH], Directory[MAX_PATH], CommandLine[2048];
    STARTUPINFOW Startup = { sizeof(Startup) };
    PROCESS_INFORMATION Process;
    HANDLE File, Null;
    DWORD Wait, Read = 0;
    BOOL Started;

    Output[0] = '\0';
    *ExitCode = 0xFFFFFFFF;
    GetTempPathW(_countof(Directory), Directory);
    if (!GetTempFileNameW(Directory, L"lng", 0, OutputPath))
        return FALSE;
    File = CreateFileW(OutputPath, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, &Inherit,
                       CREATE_ALWAYS, FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE, NULL);
    if (File == INVALID_HANDLE_VALUE)
        return FALSE;
    Null = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &Inherit, OPEN_EXISTING, 0, NULL);

    StringCchPrintfW(CommandLine, _countof(CommandLine), L"\"%s\" %s", GetTool(), Arguments);
    Startup.dwFlags = STARTF_USESTDHANDLES;
    Startup.hStdInput = Null;
    Startup.hStdOutput = File;
    Startup.hStdError = File;
    Started = CreateProcessW(NULL, CommandLine, NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL, NULL, &Startup, &Process);
    ok(Started, "CreateProcess(%ls) failed with %lu\n", CommandLine, GetLastError());
    if (Started)
    {
        Wait = WaitForSingleObject(Process.hProcess, TOOL_TIMEOUT);
        ok(Wait == WAIT_OBJECT_0, "%ls did not finish: %lu\n", CommandLine, Wait);
        if (Wait != WAIT_OBJECT_0)
            TerminateProcess(Process.hProcess, ERROR_TIMEOUT);
        GetExitCodeProcess(Process.hProcess, ExitCode);
        CloseHandle(Process.hThread);
        CloseHandle(Process.hProcess);

        SetFilePointer(File, 0, NULL, FILE_BEGIN);
        ReadFile(File, Output, sizeof(Output) - 1, &Read, NULL);
        Output[Read] = '\0';
        trace("%ls -> 0x%08lx\n", CommandLine, *ExitCode);
    }
    if (Null != INVALID_HANDLE_VALUE)
        CloseHandle(Null);
    CloseHandle(File);
    return Started;
}

static BOOL
ArpViewHasDisplayName(HKEY Root, REGSAM View, PCWSTR DisplayName)
{
    WCHAR SubKey[256], Name[512];
    DWORD Index, SubKeyLength, NameSize;
    HKEY Uninstall;
    BOOL Found = FALSE;

    if (RegOpenKeyExW(Root, L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall", 0,
                      KEY_READ | View, &Uninstall) != ERROR_SUCCESS)
        return FALSE;
    for (Index = 0; !Found; Index++)
    {
        SubKeyLength = _countof(SubKey);
        if (RegEnumKeyExW(Uninstall, Index, SubKey, &SubKeyLength, NULL, NULL, NULL, NULL) != ERROR_SUCCESS)
            break;
        NameSize = sizeof(Name);
        if (RegGetValueW(Uninstall, SubKey, L"DisplayName", RRF_RT_REG_SZ, NULL, Name, &NameSize) == ERROR_SUCCESS)
            Found = wcsncmp(Name, DisplayName, wcslen(DisplayName)) == 0;
    }
    RegCloseKey(Uninstall);
    return Found;
}

static BOOL
ArpHasDisplayName(PCWSTR DisplayName)
{
    return ArpViewHasDisplayName(HKEY_LOCAL_MACHINE, KEY_WOW64_64KEY, DisplayName) ||
           ArpViewHasDisplayName(HKEY_LOCAL_MACHINE, KEY_WOW64_32KEY, DisplayName) ||
           ArpViewHasDisplayName(HKEY_CURRENT_USER, 0, DisplayName);
}

static BOOL
PackageFileExists(const PACKAGE *Package)
{
    WCHAR Path[MAX_PATH];

    if (!ExpandEnvironmentStringsW(Package->File, Path, _countof(Path)))
        return FALSE;
    return GetFileAttributesW(Path) != INVALID_FILE_ATTRIBUTES;
}

static BOOL
WaitForRemoval(const PACKAGE *Package)
{
    DWORD Start = GetTickCount();

    while (ArpHasDisplayName(Package->DisplayName) || PackageFileExists(Package))
    {
        if (GetTickCount() - Start > REMOVAL_TIMEOUT)
            return FALSE;
        Sleep(500);
    }
    return TRUE;
}

static BOOL
OutputContains(PCWSTR Text)
{
    char Narrow[256];

    WideCharToMultiByte(CP_UTF8, 0, Text, -1, Narrow, sizeof(Narrow), NULL, NULL);
    return strstr(Output, Narrow) != NULL;
}

static void
TestPackage(const PACKAGE *Package)
{
    WCHAR Arguments[512];
    DWORD ExitCode;

    if (ArpHasDisplayName(Package->DisplayName) || PackageFileExists(Package))
    {
        skip("%ls is already installed\n", Package->Id);
        return;
    }

    StringCchPrintfW(Arguments, _countof(Arguments), L"search --id %s -e --source winget", Package->Id);
    if (!RunTool(Arguments, &ExitCode))
        return;
    if (ExitCode != 0)
    {
        skip("the winget source is unavailable: 0x%08lx\n%s\n", ExitCode, Output);
        return;
    }
    ok(OutputContains(Package->Id), "search output does not name %ls:\n%s\n", Package->Id, Output);

    StringCchPrintfW(Arguments, _countof(Arguments), L"install --id %s -e --source winget --installer-type %s%s%s --silent --accept-package-agreements --disable-interactivity",
             Package->Id, Package->InstallerType, Package->Scope ? L" --scope " : L"", Package->Scope ? Package->Scope : L"");
    if (!RunTool(Arguments, &ExitCode))
        return;
    ok(ExitCode == 0, "install returned 0x%08lx:\n%s\n", ExitCode, Output);
    ok(ArpHasDisplayName(Package->DisplayName), "no installed application named %ls\n", Package->DisplayName);
    ok(PackageFileExists(Package), "%ls is missing\n", Package->File);
    if (ExitCode != 0)
        return;

    StringCchPrintfW(Arguments, _countof(Arguments), L"list --id %s -e --source winget", Package->Id);
    if (RunTool(Arguments, &ExitCode))
    {
        ok(ExitCode == 0, "list returned 0x%08lx:\n%s\n", ExitCode, Output);
        ok(OutputContains(Package->Id), "list output does not name %ls:\n%s\n", Package->Id, Output);
    }

    StringCchPrintfW(Arguments, _countof(Arguments), L"uninstall --id %s -e --source winget --silent --disable-interactivity", Package->Id);
    if (!RunTool(Arguments, &ExitCode))
        return;
    ok(ExitCode == 0, "uninstall returned 0x%08lx:\n%s\n", ExitCode, Output);
    ok(WaitForRemoval(Package), "%ls is still installed after uninstall\n", Package->Id);

    StringCchPrintfW(Arguments, _countof(Arguments), L"list --id %s -e --source winget", Package->Id);
    if (RunTool(Arguments, &ExitCode))
        ok(ExitCode != 0, "list still finds %ls:\n%s\n", Package->Id, Output);
}

START_TEST(exe)
{
    TestPackage(&ExePackage);
}

START_TEST(inno)
{
    TestPackage(&InnoPackage);
}

START_TEST(nullsoft)
{
    TestPackage(&NullsoftPackage);
}

START_TEST(portable)
{
    TestPackage(&PortablePackage);
}

START_TEST(wix)
{
    TestPackage(&WixPackage);
}
