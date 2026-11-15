/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test the signer score the driver database records for each driver package
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <apitest.h>
#include <windows.h>
#include <setupapi.h>
#include <stdio.h>

static const char TestInf[] =
    "[Version]\r\n"
    "Signature=\"$WINDOWS NT$\"\r\n"
    "Class=System\r\n"
    "ClassGuid={4d36e97d-e325-11ce-bfc1-08002be10318}\r\n"
    "Provider=%Provider%\r\n"
    "DriverVer=11/15/2026,1.0.0.0\r\n"
    "\r\n"
    "[Manufacturer]\r\n"
    "%Provider%=Models,NTamd64,NTx86,NTarm64\r\n"
    "\r\n"
    "[Models.NTamd64]\r\n"
    "%Device%=Install,ROOT\\LNTSIGNERSCORE\r\n"
    "[Models.NTx86]\r\n"
    "%Device%=Install,ROOT\\LNTSIGNERSCORE\r\n"
    "[Models.NTarm64]\r\n"
    "%Device%=Install,ROOT\\LNTSIGNERSCORE\r\n"
    "\r\n"
    "[Install]\r\n"
    "\r\n"
    "[Install.Services]\r\n"
    "AddService=,2\r\n"
    "\r\n"
    "[Strings]\r\n"
    "Provider=\"LiberNT Signer Score Test\"\r\n"
    "Device=\"LiberNT signer score test device\"\r\n";

static BOOL
ReadSignerScore(HKEY Packages, const WCHAR *Package, DWORD *Score)
{
    DWORD Size = sizeof(*Score), Type;
    HKEY Key;
    LONG Error;

    if (RegOpenKeyExW(Packages, Package, 0, KEY_READ, &Key) != ERROR_SUCCESS)
        return FALSE;
    Error = RegQueryValueExW(Key, L"SignerScore", NULL, &Type, (BYTE *)Score, &Size);
    RegCloseKey(Key);
    return Error == ERROR_SUCCESS && Type == REG_DWORD && Size == sizeof(*Score);
}

static VOID
CheckEveryPackage(VOID)
{
    WCHAR Name[256];
    DWORD Index, Length, Score, Checked = 0;
    HKEY Packages;

    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SYSTEM\\DriverDatabase\\DriverPackages", 0, KEY_READ, &Packages) != ERROR_SUCCESS)
    {
        skip("No driver database\n");
        return;
    }

    for (Index = 0; ; Index++)
    {
        Length = ARRAYSIZE(Name);
        if (RegEnumKeyExW(Packages, Index, Name, &Length, NULL, NULL, NULL, NULL) != ERROR_SUCCESS)
            break;
        ok(ReadSignerScore(Packages, Name, &Score), "%ls has no signer score\n", Name);
        Checked++;
    }

    RegCloseKey(Packages);
    ok(Checked > 0, "no driver package\n");
    trace("%lu driver packages checked\n", Checked);
}

static BOOL
ActivePackage(const WCHAR *PublishedName, WCHAR *Package, DWORD Chars)
{
    WCHAR Path[MAX_PATH];
    DWORD Size = (Chars - 1) * sizeof(WCHAR), Type;
    HKEY Key;
    LONG Error;

    ZeroMemory(Package, Chars * sizeof(WCHAR));
    _snwprintf(Path, ARRAYSIZE(Path) - 1, L"SYSTEM\\DriverDatabase\\DriverInfFiles\\%ls", PublishedName);
    Path[ARRAYSIZE(Path) - 1] = UNICODE_NULL;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, Path, 0, KEY_READ, &Key) != ERROR_SUCCESS)
        return FALSE;
    Error = RegQueryValueExW(Key, L"Active", NULL, &Type, (BYTE *)Package, &Size);
    RegCloseKey(Key);
    return Error == ERROR_SUCCESS && Type == REG_SZ;
}

/* Windows 11 stages no third-party INF that names no catalog */
static VOID
CheckUnsignedPackage(VOID)
{
    WCHAR Directory[MAX_PATH], InfPath[MAX_PATH], Published[MAX_PATH], Package[256];
    PWSTR Name = NULL;
    DWORD Written, Score;
    HANDLE File;
    HKEY Packages;
    BOOL Copied;

    GetTempPathW(ARRAYSIZE(Directory), Directory);
    wcscat(Directory, L"lntsignerscore");
    CreateDirectoryW(Directory, NULL);
    _snwprintf(InfPath, ARRAYSIZE(InfPath) - 1, L"%ls\\lntsignerscore.inf", Directory);
    InfPath[ARRAYSIZE(InfPath) - 1] = UNICODE_NULL;

    File = CreateFileW(InfPath, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    ok(File != INVALID_HANDLE_VALUE, "CreateFileW failed with %lu\n", GetLastError());
    if (File == INVALID_HANDLE_VALUE)
        return;
    WriteFile(File, TestInf, sizeof(TestInf) - 1, &Written, NULL);
    CloseHandle(File);

    SetLastError(0xdeadbeef);
    Copied = SetupCopyOEMInfW(InfPath, NULL, SPOST_PATH, 0, Published, ARRAYSIZE(Published), NULL, &Name);
    ok(!Copied, "An INF without a catalog was staged as %ls\n", Copied ? Name : L"");
    ok(Copied || GetLastError() == ERROR_NO_CATALOG_FOR_OEM_INF, "SetupCopyOEMInfW error 0x%lx, expected 0x%lx\n",
       GetLastError(), (DWORD)ERROR_NO_CATALOG_FOR_OEM_INF);
    if (Copied)
    {
        Score = 0;
        ok(ActivePackage(Name, Package, ARRAYSIZE(Package)), "%ls has no active driver package\n", Name);
        if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SYSTEM\\DriverDatabase\\DriverPackages", 0, KEY_READ, &Packages) == ERROR_SUCCESS)
        {
            ok(ReadSignerScore(Packages, Package, &Score), "%ls has no signer score\n", Package);
            RegCloseKey(Packages);
        }
        ok(SetupUninstallOEMInfW(Name, SUOI_FORCEDELETE, NULL), "SetupUninstallOEMInfW failed with %lu\n", GetLastError());
    }
    DeleteFileW(InfPath);
    RemoveDirectoryW(Directory);
}

START_TEST(DriverPackageSignerScore)
{
    CheckEveryPackage();
    CheckUnsignedPackage();
}
