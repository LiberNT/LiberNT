/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test DiUninstallDriverW
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
    "ClassGuid={4d36e97d-e325-11ce-bfc1-08002be10318}\r\nProvider=LiberNT\r\nDriverVer=01/01/2026,1.0.0.1\r\n"
    "[Manufacturer]\r\nLiberNT=Models,NTamd64,NTarm64,NTx86\r\n"
    "[Models.NTamd64]\r\nDevice=Install,ROOT\\LNTNEWDEV_UNINSTALL\r\n"
    "[Models.NTarm64]\r\nDevice=Install,ROOT\\LNTNEWDEV_UNINSTALL\r\n"
    "[Models.NTx86]\r\nDevice=Install,ROOT\\LNTNEWDEV_UNINSTALL\r\n"
    "[Install.NT]\r\n[Install.NT.Services]\r\nAddService=,2\r\n";

static
VOID
CheckUninstall(LPCWSTR Inf, DWORD Flags, DWORD Expected, BOOL ExpectedReboot, ULONG Line)
{
    BOOL Reboot = 7, Ret;

    SetLastError(0xdeadbeef);
    Ret = DiUninstallDriverW(NULL, Inf, Flags, &Reboot);
    ok_(__FILE__, Line)(!Ret, "DiUninstallDriverW succeeded\n");
    ok_(__FILE__, Line)(GetLastError() == Expected, "error %lu, expected %lu\n", GetLastError(), Expected);
    ok_(__FILE__, Line)(Reboot == ExpectedReboot, "reboot %d, expected %d\n", Reboot, ExpectedReboot);
}

static
BOOL
Publish(LPCWSTR Inf, LPWSTR Published, DWORD PublishedCch)
{
    PWSTR Component = NULL;

    SetLastError(0xdeadbeef);
    if (SetupCopyOEMInfW(Inf, NULL, SPOST_PATH, 0, Published, PublishedCch, NULL, &Component))
        return TRUE;

    if (GetLastError() == ERROR_NO_CATALOG_FOR_OEM_INF)
        skip("The unsigned test package cannot be published\n");
    else
        ok(FALSE, "SetupCopyOEMInfW failed with %lu\n", GetLastError());
    return FALSE;
}

START_TEST(UninstallDriver)
{
    WCHAR TempDir[MAX_PATH], Inf[MAX_PATH], Missing[MAX_PATH], Published[MAX_PATH], InBox[MAX_PATH];
    HANDLE File;
    DWORD Written;
    BOOL Wow64 = FALSE, Reboot, Ret;

    GetTempPathW(RTL_NUMBER_OF(TempDir), TempDir);
    swprintf(Inf, RTL_NUMBER_OF(Inf), L"%lslntuninst.inf", TempDir);
    swprintf(Missing, RTL_NUMBER_OF(Missing), L"%lslntuninst-missing.inf", TempDir);
    File = CreateFileW(Inf, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
    ok(File != INVALID_HANDLE_VALUE, "CreateFileW failed with %lu\n", GetLastError());
    if (File == INVALID_HANDLE_VALUE)
        return;
    ok(WriteFile(File, TestInf, sizeof(TestInf) - 1, &Written, NULL), "WriteFile failed with %lu\n", GetLastError());
    CloseHandle(File);

    IsWow64Process(GetCurrentProcess(), &Wow64);
    if (Wow64)
    {
        CheckUninstall(Inf, 0, ERROR_IN_WOW64, 7, __LINE__);
        DeleteFileW(Inf);
        return;
    }

    CheckUninstall(NULL, 0, ERROR_INVALID_PARAMETER, 7, __LINE__);
    CheckUninstall(Inf, 0x4, ERROR_INVALID_FLAGS, 7, __LINE__);
    CheckUninstall(Missing, 0, ERROR_FILE_NOT_FOUND, 7, __LINE__);
    CheckUninstall(Inf, 0, ERROR_NOT_FOUND, 7, __LINE__);
    CheckUninstall(Inf, DIURFLAG_RESERVED, ERROR_NOT_FOUND, 7, __LINE__);
    CheckUninstall(Inf, DIURFLAG_NO_REMOVE_INF, ERROR_NOT_FOUND, 7, __LINE__);

    GetWindowsDirectoryW(InBox, RTL_NUMBER_OF(InBox));
    wcscat(InBox, L"\\inf\\umpass.inf");
    CheckUninstall(InBox, DIURFLAG_NO_REMOVE_INF, ERROR_NOT_AN_INSTALLED_OEM_INF, 7, __LINE__);
    CheckUninstall(InBox, 0, ERROR_NOT_AN_INSTALLED_OEM_INF, 7, __LINE__);

    if (Publish(Inf, Published, RTL_NUMBER_OF(Published)))
    {
        Reboot = 7;
        SetLastError(0xdeadbeef);
        Ret = DiUninstallDriverW(NULL, Inf, DIURFLAG_NO_REMOVE_INF, &Reboot);
        ok(Ret, "DiUninstallDriverW failed with %lu\n", GetLastError());
        ok(Reboot == FALSE, "reboot %d\n", Reboot);
        ok(GetFileAttributesW(Published) != INVALID_FILE_ATTRIBUTES, "%ls was removed\n", Published);

        Reboot = 7;
        SetLastError(0xdeadbeef);
        Ret = DiUninstallDriverW(NULL, Inf, 0, &Reboot);
        ok(Ret, "DiUninstallDriverW failed with %lu\n", GetLastError());
        ok(Reboot == FALSE, "reboot %d\n", Reboot);
        ok(GetFileAttributesW(Published) == INVALID_FILE_ATTRIBUTES, "%ls is still there\n", Published);

        CheckUninstall(Inf, 0, ERROR_NOT_FOUND, 7, __LINE__);
    }

    if (Publish(Inf, Published, RTL_NUMBER_OF(Published)))
    {
        SetLastError(0xdeadbeef);
        Ret = DiUninstallDriverW(NULL, Published, 0, NULL);
        ok(Ret, "DiUninstallDriverW failed with %lu\n", GetLastError());
        ok(GetFileAttributesW(Published) == INVALID_FILE_ATTRIBUTES, "%ls is still there\n", Published);
        if (GetFileAttributesW(Published) != INVALID_FILE_ATTRIBUTES)
            SetupUninstallOEMInfW(wcsrchr(Published, L'\\') + 1, SUOI_FORCEDELETE, NULL);
    }

    DeleteFileW(Inf);
}
