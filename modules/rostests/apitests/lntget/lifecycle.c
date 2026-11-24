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
#include <tlhelp32.h>

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
ArpViewFind(HKEY Root, REGSAM View, PCWSTR DisplayName, BOOL Exact, PWSTR Version, DWORD VersionSize)
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
        if (RegGetValueW(Uninstall, SubKey, L"DisplayName", RRF_RT_REG_SZ, NULL, Name, &NameSize) != ERROR_SUCCESS)
            continue;
        Found = Exact ? wcscmp(Name, DisplayName) == 0 : wcsncmp(Name, DisplayName, wcslen(DisplayName)) == 0;
        if (Found && Version &&
            RegGetValueW(Uninstall, SubKey, L"DisplayVersion", RRF_RT_REG_SZ, NULL, Version, &VersionSize) != ERROR_SUCCESS)
            Version[0] = L'\0';
    }
    RegCloseKey(Uninstall);
    return Found;
}

static BOOL
ArpFind(PCWSTR DisplayName, BOOL Exact, PWSTR Version, DWORD VersionSize)
{
    return ArpViewFind(HKEY_LOCAL_MACHINE, KEY_WOW64_64KEY, DisplayName, Exact, Version, VersionSize) ||
           ArpViewFind(HKEY_LOCAL_MACHINE, KEY_WOW64_32KEY, DisplayName, Exact, Version, VersionSize) ||
           ArpViewFind(HKEY_CURRENT_USER, 0, DisplayName, Exact, Version, VersionSize);
}

static BOOL
ArpHasDisplayName(PCWSTR DisplayName)
{
    return ArpFind(DisplayName, FALSE, NULL, 0);
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

static BOOL
InstallerKept(PCWSTR Id)
{
    WCHAR Temp[MAX_PATH], Pattern[MAX_PATH];
    WIN32_FIND_DATAW Data;
    HANDLE Find;

    GetTempPathW(_countof(Temp), Temp);
    if (wcsstr(GetTool(), L"winget"))
        StringCchPrintfW(Pattern, _countof(Pattern), L"%sWinGet\\%s.*", Temp, Id);
    else
        StringCchPrintfW(Pattern, _countof(Pattern), L"%slntget\\%s", Temp, Id);
    Find = FindFirstFileW(Pattern, &Data);
    if (Find == INVALID_HANDLE_VALUE)
        return FALSE;
    FindClose(Find);
    return TRUE;
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
    ok(!InstallerKept(Package->Id), "the installer of %ls was kept after the install\n", Package->Id);

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

typedef struct _UPGRADE_PACKAGE
{
    PCWSTR Id;
    PCWSTR DisplayName;
    PCWSTR OldVersion;
    PCWSTR Directory;
    PCWSTR Alias;
} UPGRADE_PACKAGE;

static const UPGRADE_PACKAGE UpgradePackages[] =
{
    { L"jqlang.jq", L"jq", L"1.8.1", L"%LOCALAPPDATA%\\Microsoft\\WinGet\\Packages\\jqlang.jq_Microsoft.Winget.Source_8wekyb3d8bbwe",
      L"%LOCALAPPDATA%\\Microsoft\\WinGet\\Links\\jq.exe" },
    { L"sharkdp.fd", L"fd", L"10.4.2", L"%LOCALAPPDATA%\\Microsoft\\WinGet\\Packages\\sharkdp.fd_Microsoft.Winget.Source_8wekyb3d8bbwe",
      L"%LOCALAPPDATA%\\Microsoft\\WinGet\\Links\\fd.exe" },
};

static BOOL
InstalledVersion(const UPGRADE_PACKAGE *Package, PWSTR Version, DWORD VersionSize)
{
    Version[0] = L'\0';
    return ArpFind(Package->DisplayName, TRUE, Version, VersionSize);
}

static BOOL
InstallVersion(const UPGRADE_PACKAGE *Package, PCWSTR Version)
{
    WCHAR Arguments[512], Installed[64];
    DWORD ExitCode;

    StringCchPrintfW(Arguments, _countof(Arguments),
                     L"install --id %s -e --source winget --version %s --silent --accept-package-agreements --disable-interactivity",
                     Package->Id, Version);
    if (!RunTool(Arguments, &ExitCode))
        return FALSE;
    ok(ExitCode == 0, "install %ls %ls returned 0x%08lx:\n%s\n", Package->Id, Version, ExitCode, Output);
    ok(InstalledVersion(Package, Installed, sizeof(Installed)) && wcscmp(Installed, Version) == 0,
       "%ls is installed as version '%ls', not %ls\n", Package->Id, Installed, Version);
    return ExitCode == 0;
}

static void
UninstallPackage(PCWSTR Id)
{
    WCHAR Arguments[512];
    DWORD ExitCode;

    StringCchPrintfW(Arguments, _countof(Arguments), L"uninstall --id %s -e --source winget --silent --disable-interactivity", Id);
    if (RunTool(Arguments, &ExitCode))
        ok(ExitCode == 0, "uninstall %ls returned 0x%08lx:\n%s\n", Id, ExitCode, Output);
}

static BOOL
PackagesInstalled(void)
{
    WCHAR Version[64];
    ULONG i;

    for (i = 0; i < _countof(UpgradePackages); i++)
    {
        if (InstalledVersion(&UpgradePackages[i], Version, sizeof(Version)))
            return TRUE;
    }
    return FALSE;
}

static BOOL
SourceAvailable(PCWSTR Id)
{
    WCHAR Arguments[256];
    DWORD ExitCode;

    StringCchPrintfW(Arguments, _countof(Arguments), L"search --id %s -e --source winget", Id);
    if (!RunTool(Arguments, &ExitCode))
        return FALSE;
    if (ExitCode != 0)
        skip("the winget source is unavailable: 0x%08lx\n%s\n", ExitCode, Output);
    return ExitCode == 0;
}

static ULONG
LockDirectory(PCWSTR Directory, HANDLE *Handles, ULONG Capacity, ULONG Count)
{
    WCHAR Pattern[MAX_PATH], Path[MAX_PATH];
    WIN32_FIND_DATAW Data;
    HANDLE Find;

    StringCchPrintfW(Pattern, _countof(Pattern), L"%s\\*", Directory);
    Find = FindFirstFileW(Pattern, &Data);
    if (Find == INVALID_HANDLE_VALUE)
        return Count;
    do
    {
        if (!wcscmp(Data.cFileName, L".") || !wcscmp(Data.cFileName, L".."))
            continue;
        StringCchPrintfW(Path, _countof(Path), L"%s\\%s", Directory, Data.cFileName);
        if (Data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
            Count = LockDirectory(Path, Handles, Capacity, Count);
        else if (Count < Capacity)
        {
            Handles[Count] = CreateFileW(Path, GENERIC_READ, 0, NULL, OPEN_EXISTING, 0, NULL);
            if (Handles[Count] != INVALID_HANDLE_VALUE)
                Count++;
        }
    } while (FindNextFileW(Find, &Data));
    FindClose(Find);
    return Count;
}

START_TEST(upgrade)
{
    const UPGRADE_PACKAGE *Package = &UpgradePackages[0];
    WCHAR Arguments[512], Version[64];
    DWORD ExitCode;

    if (PackagesInstalled())
    {
        skip("an upgrade test package is already installed\n");
        return;
    }
    if (!SourceAvailable(Package->Id) || !InstallVersion(Package, Package->OldVersion))
        return;

    if (RunTool(L"upgrade --source winget", &ExitCode))
    {
        ok(ExitCode == 0, "upgrade list returned 0x%08lx:\n%s\n", ExitCode, Output);
        ok(OutputContains(Package->Id), "upgrade list does not name %ls:\n%s\n", Package->Id, Output);
    }

    StringCchPrintfW(Arguments, _countof(Arguments),
                     L"upgrade --id %s -e --source winget --silent --accept-package-agreements --disable-interactivity", Package->Id);
    if (RunTool(Arguments, &ExitCode))
        ok(ExitCode == 0, "upgrade returned 0x%08lx:\n%s\n", ExitCode, Output);
    ok(InstalledVersion(Package, Version, sizeof(Version)), "%ls is no longer installed\n", Package->Id);
    ok(wcscmp(Version, Package->OldVersion) != 0, "%ls is still version %ls\n", Package->Id, Version);

    StringCchPrintfW(Arguments, _countof(Arguments), L"upgrade --id %s -e --source winget", Package->Id);
    if (RunTool(Arguments, &ExitCode))
        ok(ExitCode != 0, "upgrade still finds an upgrade for %ls:\n%s\n", Package->Id, Output);

    if (RunTool(L"upgrade --all --version 1.0 --source winget", &ExitCode))
        ok(ExitCode != 0, "upgrade --all --version was accepted:\n%s\n", Output);

    UninstallPackage(Package->Id);
}

START_TEST(upgradeall)
{
    WCHAR Directory[MAX_PATH], Version[64];
    HANDLE Handles[64];
    ULONG Locked, Count, i, j;
    DWORD ExitCode;

    if (PackagesInstalled())
    {
        skip("an upgrade test package is already installed\n");
        return;
    }
    if (!SourceAvailable(UpgradePackages[0].Id))
        return;

    for (Locked = 0; Locked < _countof(UpgradePackages); Locked++)
    {
        for (i = 0; i < _countof(UpgradePackages); i++)
        {
            if (!InstallVersion(&UpgradePackages[i], UpgradePackages[i].OldVersion))
                return;
        }

        ExpandEnvironmentStringsW(UpgradePackages[Locked].Directory, Directory, _countof(Directory));
        Count = LockDirectory(Directory, Handles, _countof(Handles), 0);
        ok(Count != 0, "nothing to lock in %ls\n", Directory);

        if (RunTool(L"upgrade --all --source winget --silent --accept-package-agreements --disable-interactivity", &ExitCode))
            ok(ExitCode != 0, "upgrade --all succeeded with %ls locked:\n%s\n", UpgradePackages[Locked].Id, Output);

        for (j = 0; j < Count; j++)
            CloseHandle(Handles[j]);

        for (i = 0; i < _countof(UpgradePackages); i++)
        {
            ok(InstalledVersion(&UpgradePackages[i], Version, sizeof(Version)), "%ls is no longer installed\n", UpgradePackages[i].Id);
            if (i == Locked)
                ok(wcscmp(Version, UpgradePackages[i].OldVersion) == 0, "locked %ls changed to version %ls\n", UpgradePackages[i].Id, Version);
            else
                ok(wcscmp(Version, UpgradePackages[i].OldVersion) != 0, "%ls was not upgraded with %ls locked:\n%s\n",
                   UpgradePackages[i].Id, UpgradePackages[Locked].Id, Output);
        }

        for (i = 0; i < _countof(UpgradePackages); i++)
            UninstallPackage(UpgradePackages[i].Id);
    }
}

static ULONG
CountEntries(PCWSTR Directory, PWSTR Name, DWORD NameSize)
{
    WCHAR Pattern[MAX_PATH];
    WIN32_FIND_DATAW Data;
    HANDLE Find;
    ULONG Count = 0;

    StringCchPrintfW(Pattern, _countof(Pattern), L"%s\\*", Directory);
    Find = FindFirstFileW(Pattern, &Data);
    if (Find == INVALID_HANDLE_VALUE)
        return 0;
    do
    {
        if (!wcscmp(Data.cFileName, L".") || !wcscmp(Data.cFileName, L".."))
            continue;
        if (!Count)
            StringCchCopyW(Name, NameSize, Data.cFileName);
        Count++;
    } while (FindNextFileW(Find, &Data));
    FindClose(Find);
    return Count;
}

static BOOL
UserFileIntact(PCWSTR Path)
{
    char Buffer[32] = "";
    DWORD Read = 0;
    HANDLE File = CreateFileW(Path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);

    if (File == INVALID_HANDLE_VALUE)
        return FALSE;
    ReadFile(File, Buffer, sizeof(Buffer) - 1, &Read, NULL);
    CloseHandle(File);
    return Read == 8 && !memcmp(Buffer, "settings", 8);
}

START_TEST(portablefiles)
{
    WCHAR Directory[MAX_PATH], UserFile[MAX_PATH], Alias[MAX_PATH], Arguments[512], Version[64], Name[MAX_PATH];
    const UPGRADE_PACKAGE *Package;
    HANDLE File;
    DWORD ExitCode, Written;
    ULONG i;

    if (PackagesInstalled())
    {
        skip("an upgrade test package is already installed\n");
        return;
    }
    if (!SourceAvailable(UpgradePackages[0].Id))
        return;

    for (i = 0; i < _countof(UpgradePackages); i++)
    {
        Package = &UpgradePackages[i];
        if (!InstallVersion(Package, Package->OldVersion))
            return;
        ExpandEnvironmentStringsW(Package->Directory, Directory, _countof(Directory));
        ExpandEnvironmentStringsW(Package->Alias, Alias, _countof(Alias));
        StringCchPrintfW(UserFile, _countof(UserFile), L"%s\\lntget-user.ini", Directory);
        File = CreateFileW(UserFile, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
        ok(File != INVALID_HANDLE_VALUE, "cannot create %ls: %lu\n", UserFile, GetLastError());
        if (File == INVALID_HANDLE_VALUE)
            return;
        WriteFile(File, "settings", 8, &Written, NULL);
        CloseHandle(File);

        StringCchPrintfW(Arguments, _countof(Arguments),
                         L"upgrade --id %s -e --source winget --silent --accept-package-agreements --disable-interactivity", Package->Id);
        if (RunTool(Arguments, &ExitCode))
            ok(ExitCode == 0, "upgrade %ls returned 0x%08lx:\n%s\n", Package->Id, ExitCode, Output);
        ok(InstalledVersion(Package, Version, sizeof(Version)) && wcscmp(Version, Package->OldVersion) != 0,
           "%ls is version '%ls' after the upgrade\n", Package->Id, Version);
        ok(UserFileIntact(UserFile), "the upgrade of %ls removed or changed %ls\n", Package->Id, UserFile);
        ok(GetFileAttributesW(Alias) != INVALID_FILE_ATTRIBUTES, "%ls is missing after the upgrade\n", Alias);

        UninstallPackage(Package->Id);
        ok(!InstalledVersion(Package, Version, sizeof(Version)), "%ls is still installed\n", Package->Id);
        ok(GetFileAttributesW(Alias) == INVALID_FILE_ATTRIBUTES, "%ls remains after the uninstall\n", Alias);
        ok(UserFileIntact(UserFile), "the uninstall of %ls removed or changed %ls\n", Package->Id, UserFile);
        Name[0] = L'\0';
        ok(CountEntries(Directory, Name, _countof(Name)) == 1 && !_wcsicmp(Name, L"lntget-user.ini"),
           "%ls holds more than the user file after the uninstall (first entry '%ls')\n", Directory, Name);
        DeleteFileW(UserFile);
        RemoveDirectoryW(Directory);
    }
}

static void
CloseProcesses(PCWSTR ImageName)
{
    PROCESSENTRY32W Entry = { sizeof(Entry) };
    HANDLE Snapshot, Process;
    BOOL More;

    Snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (Snapshot == INVALID_HANDLE_VALUE)
        return;
    for (More = Process32FirstW(Snapshot, &Entry); More; More = Process32NextW(Snapshot, &Entry))
    {
        if (_wcsicmp(Entry.szExeFile, ImageName))
            continue;
        trace("closing %ls (%lu) before the uninstall\n", ImageName, Entry.th32ProcessID);
        Process = OpenProcess(PROCESS_TERMINATE | SYNCHRONIZE, FALSE, Entry.th32ProcessID);
        if (!Process)
            continue;
        TerminateProcess(Process, 0);
        WaitForSingleObject(Process, 5000);
        CloseHandle(Process);
    }
    CloseHandle(Snapshot);
}

START_TEST(dependency)
{
    static const PACKAGE Package = { L"bluemars.ClipX", L"nullsoft", NULL, L"ClipX", L"%ProgramFiles%\\ClipX\\clipx.exe" };
    WCHAR Arguments[512];
    SYSTEM_INFO System;
    PCWSTR Dependency;
    DWORD ExitCode;

    GetNativeSystemInfo(&System);
    if (System.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_AMD64 || System.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_ARM64)
        Dependency = L"Microsoft.VCRedist.2015+.x64";
    else if (System.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_INTEL)
        Dependency = L"Microsoft.VCRedist.2015+.x86";
    else
    {
        skip("%ls has no installer for this architecture\n", Package.Id);
        return;
    }
    if (ArpHasDisplayName(Package.DisplayName) || PackageFileExists(&Package))
    {
        skip("%ls is already installed\n", Package.Id);
        return;
    }
    if (!SourceAvailable(Package.Id))
        return;

    StringCchPrintfW(Arguments, _countof(Arguments),
                     L"install --id %s -e --source winget --version 2.0.0.1 --silent --accept-package-agreements --disable-interactivity",
                     Package.Id);
    if (!RunTool(Arguments, &ExitCode))
        return;
    ok(ExitCode == 0, "install returned 0x%08lx:\n%s\n", ExitCode, Output);
    ok(OutputContains(Dependency), "install output does not name the dependency %ls:\n%s\n", Dependency, Output);
    ok(ArpHasDisplayName(Package.DisplayName), "no installed application named %ls\n", Package.DisplayName);
    ok(PackageFileExists(&Package), "%ls is missing\n", Package.File);

    StringCchPrintfW(Arguments, _countof(Arguments), L"list --id %s -e --source winget", Dependency);
    if (RunTool(Arguments, &ExitCode))
        ok(ExitCode == 0, "the dependency %ls is not installed: 0x%08lx\n%s\n", Dependency, ExitCode, Output);
    if (!ArpHasDisplayName(Package.DisplayName))
        return;

    CloseProcesses(L"clipx.exe");
    UninstallPackage(Package.Id);
}

START_TEST(agreements)
{
    static const PCWSTR Id = L"Microsoft.RemoteHelp";
    WCHAR Arguments[512];
    DWORD ExitCode;

    if (ArpHasDisplayName(L"Remote Help"))
    {
        skip("%ls is already installed\n", Id);
        return;
    }
    if (!SourceAvailable(Id))
        return;

    StringCchPrintfW(Arguments, _countof(Arguments), L"show --id %s -e --source winget --version 5.2.1210.0", Id);
    if (RunTool(Arguments, &ExitCode))
    {
        ok(ExitCode == 0, "show returned 0x%08lx:\n%s\n", ExitCode, Output);
        ok(OutputContains(L"End User License Agreement (EULA)"), "show output does not name the agreement label:\n%s\n", Output);
        ok(OutputContains(L"https://learn.microsoft.com/en-us/legal/remote-help/microsoft-remote-help-license-terms"),
           "show output does not name the agreement URL:\n%s\n", Output);
    }

    StringCchPrintfW(Arguments, _countof(Arguments),
                     L"install --id %s -e --source winget --version 5.2.1210.0 --silent --disable-interactivity", Id);
    if (RunTool(Arguments, &ExitCode))
    {
        ok(ExitCode != 0, "install without accepting the agreements returned 0:\n%s\n", Output);
        ok(!ArpHasDisplayName(L"Remote Help"), "%ls was installed without accepting its agreements\n", Id);
    }
}
