/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test the UpdateDriverForPlugAndPlayDevicesW errors
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <apitest.h>

#include <winreg.h>
#include <winuser.h>
#include <setupapi.h>
#include <newdev.h>
#include <stdio.h>

static const char TestInf[] =
    "[Version]\r\nSignature=\"$WINDOWS NT$\"\r\nClass=System\r\n"
    "ClassGuid={4d36e97d-e325-11ce-bfc1-08002be10318}\r\nProvider=LiberNT\r\nDriverVer=01/01/2026,1.0.0.0\r\n"
    "[Manufacturer]\r\nLiberNT=Models,NTamd64,NTarm64,NTx86\r\n"
    "[Models.NTamd64]\r\nDevice=Install,ROOT\\LNTNEWDEV_MISSING\r\n"
    "[Models.NTarm64]\r\nDevice=Install,ROOT\\LNTNEWDEV_MISSING\r\n"
    "[Models.NTx86]\r\nDevice=Install,ROOT\\LNTNEWDEV_MISSING\r\n"
    "[Install.NT]\r\n[Install.NT.Services]\r\nAddService=,2\r\n";

static
VOID
CheckUpdate(LPCWSTR HardwareId, LPCWSTR Inf, DWORD Flags, DWORD Expected, BOOL ExpectedReboot, ULONG Line)
{
    BOOL Reboot = 7, Ret;

    SetLastError(0xdeadbeef);
    Ret = UpdateDriverForPlugAndPlayDevicesW(NULL, HardwareId, Inf, Flags, &Reboot);
    ok_(__FILE__, Line)(!Ret, "UpdateDriverForPlugAndPlayDevicesW succeeded\n");
    ok_(__FILE__, Line)(GetLastError() == Expected, "error %lu, expected %lu\n", GetLastError(), Expected);
    ok_(__FILE__, Line)(Reboot == ExpectedReboot, "reboot %d, expected %d\n", Reboot, ExpectedReboot);
}

START_TEST(UpdateDriver)
{
    WCHAR TempDir[MAX_PATH], Inf[MAX_PATH], Missing[MAX_PATH];
    HANDLE File;
    DWORD Written;
    BOOL Wow64 = FALSE;

    GetTempPathW(RTL_NUMBER_OF(TempDir), TempDir);
    swprintf(Inf, RTL_NUMBER_OF(Inf), L"%lslntnewdev.inf", TempDir);
    swprintf(Missing, RTL_NUMBER_OF(Missing), L"%lslntnewdev-missing.inf", TempDir);
    File = CreateFileW(Inf, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
    ok(File != INVALID_HANDLE_VALUE, "CreateFileW failed with %lu\n", GetLastError());
    if (File == INVALID_HANDLE_VALUE)
        return;
    ok(WriteFile(File, TestInf, sizeof(TestInf) - 1, &Written, NULL), "WriteFile failed with %lu\n", GetLastError());
    CloseHandle(File);

    IsWow64Process(GetCurrentProcess(), &Wow64);
    if (Wow64)
    {
        SetLastError(0xdeadbeef);
        ok(!UpdateDriverForPlugAndPlayDevicesW(NULL, L"ROOT\\LNTNEWDEV_MISSING", Inf, 0, NULL), "succeeded in WoW64\n");
        ok_err(ERROR_IN_WOW64);
    }
    else
    {
        CheckUpdate(L"ROOT\\LNTNEWDEV_MISSING", Inf, 0, ERROR_NO_SUCH_DEVINST, FALSE, __LINE__);
        CheckUpdate(L"ROOT\\LNTNEWDEV_MISSING", Inf, INSTALLFLAG_NONINTERACTIVE, ERROR_NO_SUCH_DEVINST, FALSE, __LINE__);
        CheckUpdate(L"ROOT\\LNTNEWDEV_MISSING", Inf, INSTALLFLAG_FORCE, ERROR_NO_SUCH_DEVINST, FALSE, __LINE__);
        CheckUpdate(L"ROOT\\LNTNEWDEV_MISSING", Missing, 0, ERROR_FILE_NOT_FOUND, 7, __LINE__);
        CheckUpdate(L"ROOT\\LNTNEWDEV_MISSING", Inf, 0x8, ERROR_INVALID_FLAGS, 7, __LINE__);
    }

    DeleteFileW(Inf);
}
