/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test the documented DIFxAPI driver package status codes
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <apitest.h>
#include <winreg.h>
#include <winuser.h>
#include <setupapi.h>
#include <difxapi.h>
#include <strsafe.h>

typedef DWORD (WINAPI *PFN_PREINSTALL)(PCWSTR, DWORD);
typedef DWORD (WINAPI *PFN_INSTALL)(PCWSTR, DWORD, PCINSTALLERINFO_W, BOOL *);
typedef DWORD (WINAPI *PFN_GETPATH)(PCWSTR, PWSTR, DWORD *);

static PFN_PREINSTALL pDriverPackagePreinstallW;
static PFN_INSTALL pDriverPackageInstallW;
static PFN_INSTALL pDriverPackageUninstallW;
static PFN_GETPATH pDriverPackageGetPathW;

static const char UnsignedInf[] =
    "[Version]\r\nSignature=\"$WINDOWS NT$\"\r\nClass=System\r\n"
    "ClassGuid={4d36e97d-e325-11ce-bfc1-08002be10318}\r\nProvider=%Mfg%\r\nDriverVer=01/01/2026,1.0.0.0\r\n"
    "[Manufacturer]\r\n%Mfg%=Models,NTx86,NTamd64\r\n"
    "[Models.NTx86]\r\n%Dev%=Inst,ROOT\\LNTDIFXTEST\r\n[Models.NTamd64]\r\n%Dev%=Inst,ROOT\\LNTDIFXTEST\r\n"
    "[Inst]\r\n[Inst.Services]\r\nAddService=,2\r\n[Strings]\r\nMfg=\"LiberNT\"\r\nDev=\"DIFx test device\"\r\n";

static
BOOL
WriteUnsignedInf(PWSTR Path, DWORD PathSize)
{
    WCHAR Directory[MAX_PATH];
    HANDLE File;
    DWORD Written;
    BOOL Result;

    if (!GetTempPathW(RTL_NUMBER_OF(Directory), Directory) ||
        FAILED(StringCchPrintfW(Path, PathSize, L"%lslntdifxapitest.inf", Directory)))
    {
        return FALSE;
    }

    File = CreateFileW(Path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
    if (File == INVALID_HANDLE_VALUE)
        return FALSE;

    Result = WriteFile(File, UnsignedInf, sizeof(UnsignedInf) - 1, &Written, NULL);
    CloseHandle(File);
    return Result;
}

static
VOID
TestWow64(VOID)
{
    BOOL Reboot = TRUE;
    WCHAR Buffer[MAX_PATH];
    DWORD Chars = RTL_NUMBER_OF(Buffer);

    ok_hex(pDriverPackagePreinstallW(L"C:\\lntdifx-missing\\x.inf", DRIVER_PACKAGE_SILENT), ERROR_IN_WOW64);
    ok_hex(pDriverPackageInstallW(L"C:\\lntdifx-missing\\x.inf", DRIVER_PACKAGE_SILENT, NULL, &Reboot), ERROR_IN_WOW64);
    ok_hex(pDriverPackageUninstallW(L"C:\\lntdifx-missing\\x.inf", DRIVER_PACKAGE_SILENT, NULL, &Reboot), ERROR_IN_WOW64);
    ok_hex(pDriverPackageGetPathW(L"C:\\lntdifx-missing\\x.inf", Buffer, &Chars), ERROR_IN_WOW64);
}

static
VOID
TestNative(VOID)
{
    INSTALLERINFO_W EmptyInfo = { L"{6B1C1A5E-0000-4000-8000-4C4E54444958}", L"", L"DIFx test", L"LiberNT" };
    WCHAR Inf[MAX_PATH], SystemInf[MAX_PATH], Buffer[MAX_PATH];
    DWORD Chars;
    BOOL Reboot;

    ok_hex(pDriverPackagePreinstallW(NULL, DRIVER_PACKAGE_SILENT), ERROR_INVALID_PARAMETER);
    ok_hex(pDriverPackagePreinstallW(L"lntdifx-relative.inf", DRIVER_PACKAGE_SILENT), ERROR_INVALID_NAME);
    ok_hex(pDriverPackagePreinstallW(L"C:\\lntdifx-missing\\x.inf", DRIVER_PACKAGE_SILENT), ERROR_FILE_NOT_FOUND);

    Chars = RTL_NUMBER_OF(Buffer);
    ok_hex(pDriverPackageGetPathW(NULL, Buffer, &Chars), ERROR_INVALID_PARAMETER);

    if (GetWindowsDirectoryW(SystemInf, RTL_NUMBER_OF(SystemInf)) &&
        SUCCEEDED(StringCchCatW(SystemInf, RTL_NUMBER_OF(SystemInf), L"\\INF\\machine.inf")) &&
        GetFileAttributesW(SystemInf) != INVALID_FILE_ATTRIBUTES)
    {
        Chars = RTL_NUMBER_OF(Buffer);
        ok_hex(pDriverPackageGetPathW(SystemInf, Buffer, &Chars), ERROR_CANT_ACCESS_FILE);
        ok_hex(pDriverPackagePreinstallW(SystemInf, DRIVER_PACKAGE_SILENT), ERROR_CANT_ACCESS_FILE);
    }
    else
    {
        skip("machine.inf is not in the system INF directory\n");
    }

    if (!WriteUnsignedInf(Inf, RTL_NUMBER_OF(Inf)))
    {
        skip("Cannot write the test INF\n");
        return;
    }

    ok_hex(pDriverPackagePreinstallW(Inf, DRIVER_PACKAGE_SILENT), TRUST_E_NOSIGNATURE);
    Reboot = TRUE;
    ok_hex(pDriverPackageInstallW(Inf, DRIVER_PACKAGE_SILENT, &EmptyInfo, &Reboot), ERROR_INVALID_PARAMETER);
    ok_int(Reboot, FALSE);
    Chars = RTL_NUMBER_OF(Buffer);
    ok_hex(pDriverPackageGetPathW(Inf, Buffer, &Chars), ERROR_DRIVER_PACKAGE_NOT_IN_STORE);
    Reboot = TRUE;
    ok_hex(pDriverPackageUninstallW(Inf, DRIVER_PACKAGE_SILENT, NULL, &Reboot), ERROR_DRIVER_PACKAGE_NOT_IN_STORE);
    ok_int(Reboot, FALSE);

    DeleteFileW(Inf);
}

START_TEST(DriverPackage)
{
    HMODULE Difx;
    BOOL Wow64 = FALSE;

    Difx = LoadLibraryW(L"difxapi.dll");
    if (!Difx)
    {
        skip("difxapi.dll is not available\n");
        return;
    }

    pDriverPackagePreinstallW = (PFN_PREINSTALL)GetProcAddress(Difx, "DriverPackagePreinstallW");
    pDriverPackageInstallW = (PFN_INSTALL)GetProcAddress(Difx, "DriverPackageInstallW");
    pDriverPackageUninstallW = (PFN_INSTALL)GetProcAddress(Difx, "DriverPackageUninstallW");
    pDriverPackageGetPathW = (PFN_GETPATH)GetProcAddress(Difx, "DriverPackageGetPathW");
    ok(pDriverPackagePreinstallW && pDriverPackageInstallW && pDriverPackageUninstallW && pDriverPackageGetPathW,
       "difxapi.dll does not export the driver package functions\n");
    if (!pDriverPackagePreinstallW || !pDriverPackageInstallW || !pDriverPackageUninstallW || !pDriverPackageGetPathW)
        return;

    IsWow64Process(GetCurrentProcess(), &Wow64);
    if (Wow64)
        TestWow64();
    else
        TestNative();

    FreeLibrary(Difx);
}
