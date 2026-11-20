/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test that installing driver files that can only be replaced at the next restart asks for that restart
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <apitest.h>
#include <windows.h>
#include <setupapi.h>
#include <stdio.h>

static const GUID SystemClass = {0x4d36e97d, 0xe325, 0x11ce, {0xbf, 0xc1, 0x08, 0x00, 0x2b, 0xe1, 0x03, 0x18}};

static const char TestInf[] =
    "[Version]\r\n"
    "Signature=\"$WINDOWS NT$\"\r\n"
    "Class=System\r\n"
    "ClassGuid={4d36e97d-e325-11ce-bfc1-08002be10318}\r\n"
    "Provider=%Provider%\r\n"
    "DriverVer=11/20/2026,1.0.0.0\r\n"
    "\r\n"
    "[Manufacturer]\r\n"
    "%Provider%=Models,NTamd64,NTx86,NTarm64\r\n"
    "\r\n"
    "[Models.NTamd64]\r\n"
    "%Device%=Install,ROOT\\LNTNEEDREBOOT\r\n"
    "[Models.NTx86]\r\n"
    "%Device%=Install,ROOT\\LNTNEEDREBOOT\r\n"
    "[Models.NTarm64]\r\n"
    "%Device%=Install,ROOT\\LNTNEEDREBOOT\r\n"
    "\r\n"
    "[Install]\r\n"
    "CopyFiles=Files\r\n"
    "\r\n"
    "[Files]\r\n"
    "lntrb.bin\r\n"
    "\r\n"
    "[DestinationDirs]\r\n"
    "Files=10,Temp\\lntneedreboot\r\n"
    "\r\n"
    "[SourceDisksNames]\r\n"
    "1=%Disk%\r\n"
    "\r\n"
    "[SourceDisksFiles]\r\n"
    "lntrb.bin=1\r\n"
    "\r\n"
    "[Strings]\r\n"
    "Provider=\"LiberNT Need Reboot Test\"\r\n"
    "Device=\"LiberNT need reboot test device\"\r\n"
    "Disk=\"LiberNT need reboot test files\"\r\n";

static const char NewContent[] = "new driver file";
static const char OldContent[] = "old driver file";

static BOOL
WriteTestFile(const WCHAR *Path, const char *Content, DWORD Length)
{
    HANDLE File;
    DWORD Written = 0;
    BOOL Result;

    File = CreateFileW(Path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (File == INVALID_HANDLE_VALUE)
        return FALSE;
    Result = WriteFile(File, Content, Length, &Written, NULL) && Written == Length;
    CloseHandle(File);
    return Result;
}

static BOOL
FileHasContent(const WCHAR *Path, const char *Content, DWORD Length)
{
    char Buffer[64];
    HANDLE File;
    DWORD Read = 0;
    BOOL Result;

    File = CreateFileW(Path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING, 0,
                       NULL);
    if (File == INVALID_HANDLE_VALUE)
        return FALSE;
    Result = ReadFile(File, Buffer, sizeof(Buffer), &Read, NULL) && Read == Length && !memcmp(Buffer, Content, Length);
    CloseHandle(File);
    return Result;
}

/* Drop the delayed moves this test queued from the session manager's list */
static VOID
ForgetPendingMoves(const WCHAR *Directory)
{
    static const WCHAR SessionManager[] = L"SYSTEM\\CurrentControlSet\\Control\\Session Manager";
    WCHAR Prefix[MAX_PATH];
    WCHAR *Old, *New, *Source, *Target, *Out;
    DWORD Size = 0, Type, PrefixLength;
    HKEY Key;

    _snwprintf(Prefix, ARRAYSIZE(Prefix) - 1, L"\\??\\%ls\\", Directory);
    Prefix[ARRAYSIZE(Prefix) - 1] = UNICODE_NULL;
    PrefixLength = (DWORD)wcslen(Prefix);

    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, SessionManager, 0, KEY_QUERY_VALUE | KEY_SET_VALUE, &Key) != ERROR_SUCCESS)
        return;
    if (RegQueryValueExW(Key, L"PendingFileRenameOperations", NULL, &Type, NULL, &Size) != ERROR_SUCCESS ||
        Type != REG_MULTI_SZ || Size < 2 * sizeof(WCHAR))
    {
        RegCloseKey(Key);
        return;
    }

    Old = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, Size + 2 * sizeof(WCHAR));
    New = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, Size + 2 * sizeof(WCHAR));
    if (Old && New && RegQueryValueExW(Key, L"PendingFileRenameOperations", NULL, &Type, (BYTE *)Old, &Size) == ERROR_SUCCESS)
    {
        Out = New;
        for (Source = Old; *Source; Source = Target + wcslen(Target) + 1)
        {
            Target = Source + wcslen(Source) + 1;
            if (!_wcsnicmp(Source, Prefix, PrefixLength))
                continue;
            wcscpy(Out, Source);
            Out += wcslen(Source) + 1;
            wcscpy(Out, Target);
            Out += wcslen(Target) + 1;
        }
        *Out++ = UNICODE_NULL;
        if (Out - New <= 1)
            RegDeleteValueW(Key, L"PendingFileRenameOperations");
        else
            RegSetValueExW(Key, L"PendingFileRenameOperations", 0, REG_MULTI_SZ, (BYTE *)New, (DWORD)((Out - New) * sizeof(WCHAR)));
    }

    HeapFree(GetProcessHeap(), 0, New);
    HeapFree(GetProcessHeap(), 0, Old);
    RegCloseKey(Key);
}

static VOID
EmptyDirectory(const WCHAR *Directory)
{
    WIN32_FIND_DATAW Data;
    WCHAR Pattern[MAX_PATH], Path[MAX_PATH];
    HANDLE Find;

    _snwprintf(Pattern, ARRAYSIZE(Pattern) - 1, L"%ls\\*", Directory);
    Pattern[ARRAYSIZE(Pattern) - 1] = UNICODE_NULL;
    Find = FindFirstFileW(Pattern, &Data);
    if (Find == INVALID_HANDLE_VALUE)
        return;
    do
    {
        if (Data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
            continue;
        _snwprintf(Path, ARRAYSIZE(Path) - 1, L"%ls\\%ls", Directory, Data.cFileName);
        Path[ARRAYSIZE(Path) - 1] = UNICODE_NULL;
        DeleteFileW(Path);
    } while (FindNextFileW(Find, &Data));
    FindClose(Find);
}

static VOID
InstallFiles(const WCHAR *InfPath, const WCHAR *Target, BOOL HoldTarget)
{
    static const WCHAR HardwareIds[] = L"ROOT\\LNTNEEDREBOOT\0";
    SP_DEVINSTALL_PARAMS_W Params = { sizeof(Params) };
    SP_DEVINFO_DATA Device = { sizeof(Device) };
    HANDLE Held = INVALID_HANDLE_VALUE;
    HDEVINFO Devices;
    BOOL Success;

    ok(WriteTestFile(Target, OldContent, sizeof(OldContent) - 1), "Writing the old file failed with %lu\n", GetLastError());
    if (HoldTarget)
    {
        Held = CreateFileW(Target, GENERIC_READ, 0, NULL, OPEN_EXISTING, 0, NULL);
        ok(Held != INVALID_HANDLE_VALUE, "Holding the old file failed with %lu\n", GetLastError());
    }

    Devices = SetupDiCreateDeviceInfoList(&SystemClass, NULL);
    ok(Devices != INVALID_HANDLE_VALUE, "SetupDiCreateDeviceInfoList failed with %lu\n", GetLastError());
    if (Devices == INVALID_HANDLE_VALUE)
        goto Close;

    Success = SetupDiCreateDeviceInfoW(Devices, L"LntNeedReboot", &SystemClass, NULL, NULL, DICD_GENERATE_ID, &Device);
    ok(Success, "SetupDiCreateDeviceInfoW failed with %lu\n", GetLastError());
    if (!Success)
        goto Destroy;
    Success = SetupDiSetDeviceRegistryPropertyW(Devices, &Device, SPDRP_HARDWAREID, (const BYTE *)HardwareIds,
                                                sizeof(HardwareIds));
    ok(Success, "Setting the hardware ID failed with %lu\n", GetLastError());

    Success = SetupDiGetDeviceInstallParamsW(Devices, &Device, &Params);
    ok(Success, "SetupDiGetDeviceInstallParamsW failed with %lu\n", GetLastError());
    Params.Flags |= DI_ENUMSINGLEINF;
    wcscpy(Params.DriverPath, InfPath);
    Success = SetupDiSetDeviceInstallParamsW(Devices, &Device, &Params);
    ok(Success, "SetupDiSetDeviceInstallParamsW failed with %lu\n", GetLastError());

    Success = SetupDiBuildDriverInfoList(Devices, &Device, SPDIT_COMPATDRIVER);
    ok(Success, "SetupDiBuildDriverInfoList failed with %lu\n", GetLastError());
    Success = SetupDiCallClassInstaller(DIF_SELECTBESTCOMPATDRV, Devices, &Device);
    ok(Success, "Selecting the driver failed with %lu\n", GetLastError());
    if (!Success)
        goto Remove;

    Success = SetupDiInstallDriverFiles(Devices, &Device);
    ok(Success, "SetupDiInstallDriverFiles failed with %lu\n", GetLastError());

    Params.cbSize = sizeof(Params);
    Success = SetupDiGetDeviceInstallParamsW(Devices, &Device, &Params);
    ok(Success, "SetupDiGetDeviceInstallParamsW failed with %lu\n", GetLastError());
    ok((Params.Flags & DI_NOFILECOPY) != 0, "The files may be copied again: flags 0x%lx\n", Params.Flags);
    if (Held != INVALID_HANDLE_VALUE)
    {
        CloseHandle(Held);
        Held = INVALID_HANDLE_VALUE;
    }
    if (HoldTarget)
    {
        ok((Params.Flags & DI_NEEDREBOOT) != 0, "No restart was asked for a file in use: flags 0x%lx\n", Params.Flags);
        ok(FileHasContent(Target, OldContent, sizeof(OldContent) - 1), "The file in use was replaced at once\n");
    }
    else
    {
        ok(!(Params.Flags & DI_NEEDREBOOT), "A restart was asked for a free file: flags 0x%lx\n", Params.Flags);
        ok(FileHasContent(Target, NewContent, sizeof(NewContent) - 1), "The free file was not replaced\n");
    }

Remove:
    SetupDiDeleteDeviceInfo(Devices, &Device);
Destroy:
    SetupDiDestroyDeviceInfoList(Devices);
Close:
    if (Held != INVALID_HANDLE_VALUE)
        CloseHandle(Held);
}

START_TEST(DriverFilesNeedReboot)
{
    WCHAR Package[MAX_PATH], InfPath[MAX_PATH], Source[MAX_PATH], Windows[MAX_PATH], Destination[MAX_PATH],
          Target[MAX_PATH];

    GetTempPathW(ARRAYSIZE(Package), Package);
    wcscat(Package, L"lntneedrebootpkg");
    CreateDirectoryW(Package, NULL);
    _snwprintf(InfPath, ARRAYSIZE(InfPath) - 1, L"%ls\\lntneedreboot.inf", Package);
    InfPath[ARRAYSIZE(InfPath) - 1] = UNICODE_NULL;
    _snwprintf(Source, ARRAYSIZE(Source) - 1, L"%ls\\lntrb.bin", Package);
    Source[ARRAYSIZE(Source) - 1] = UNICODE_NULL;

    GetWindowsDirectoryW(Windows, ARRAYSIZE(Windows));
    _snwprintf(Destination, ARRAYSIZE(Destination) - 1, L"%ls\\Temp\\lntneedreboot", Windows);
    Destination[ARRAYSIZE(Destination) - 1] = UNICODE_NULL;
    _snwprintf(Target, ARRAYSIZE(Target) - 1, L"%ls\\lntrb.bin", Destination);
    Target[ARRAYSIZE(Target) - 1] = UNICODE_NULL;

    if (!CreateDirectoryW(Destination, NULL) && GetLastError() != ERROR_ALREADY_EXISTS)
    {
        skip("Cannot create %ls: %lu\n", Destination, GetLastError());
        return;
    }
    ok(WriteTestFile(InfPath, TestInf, sizeof(TestInf) - 1), "Writing the INF failed with %lu\n", GetLastError());
    ok(WriteTestFile(Source, NewContent, sizeof(NewContent) - 1), "Writing the new file failed with %lu\n", GetLastError());

    InstallFiles(InfPath, Target, FALSE);
    InstallFiles(InfPath, Target, TRUE);

    ForgetPendingMoves(Destination);
    EmptyDirectory(Destination);
    RemoveDirectoryW(Destination);
    DeleteFileW(Source);
    DeleteFileW(InfPath);
    RemoveDirectoryW(Package);
}
