/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test GetComPlusPackageInstallStatus and SetComPlusPackageInstallStatus
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"

typedef ULONG (WINAPI *PFN_GET_COMPLUS_STATUS)(VOID);
typedef BOOL (WINAPI *PFN_SET_COMPLUS_STATUS)(ULONG);

#define USER_SHARED_DATA_ADDRESS 0x7FFE0000
#define USER_SHARED_DATA_COMPLUS_PACKAGE 0x2E0

START_TEST(ComPlusPackageInstallStatus)
{
    HMODULE Kernel32 = GetModuleHandleW(L"kernel32.dll");
    PFN_GET_COMPLUS_STATUS pGetComPlusPackageInstallStatus;
    PFN_SET_COMPLUS_STATUS pSetComPlusPackageInstallStatus;
    ULONG Status, Shared, Queried, ReturnLength;
    NTSTATUS NtStatus;
    BOOL Ret;

    pGetComPlusPackageInstallStatus = (PFN_GET_COMPLUS_STATUS)GetProcAddress(Kernel32, "GetComPlusPackageInstallStatus");
    pSetComPlusPackageInstallStatus = (PFN_SET_COMPLUS_STATUS)GetProcAddress(Kernel32, "SetComPlusPackageInstallStatus");
    if (!pGetComPlusPackageInstallStatus || !pSetComPlusPackageInstallStatus)
    {
        skip("GetComPlusPackageInstallStatus or SetComPlusPackageInstallStatus is not available\n");
        return;
    }

    SetLastError(0xdeadbeef);
    Status = pGetComPlusPackageInstallStatus();
    ok_long(GetLastError(), 0xdeadbeef);
    ok(Status == 0 || Status == 1, "Unexpected COM+ package status %lu\n", Status);

    Shared = *(volatile ULONG *)(ULONG_PTR)(USER_SHARED_DATA_ADDRESS + USER_SHARED_DATA_COMPLUS_PACKAGE);
    ok_long(Status, Shared);

    SetLastError(0xdeadbeef);
    Ret = pSetComPlusPackageInstallStatus(2);
    ok_int(Ret, FALSE);
    ok_long(GetLastError(), ERROR_INVALID_PARAMETER);

    SetLastError(0xdeadbeef);
    Ret = pSetComPlusPackageInstallStatus(0x80000001);
    ok_int(Ret, FALSE);
    ok_long(GetLastError(), ERROR_INVALID_PARAMETER);

    SetLastError(0xdeadbeef);
    Ret = pSetComPlusPackageInstallStatus(Status);
    ok_int(Ret, TRUE);
    ok_long(GetLastError(), 0xdeadbeef);
    ok_long(pGetComPlusPackageInstallStatus(), Status);

    Ret = pSetComPlusPackageInstallStatus(!Status);
    ok_int(Ret, TRUE);
    ok_long(pGetComPlusPackageInstallStatus(), !Status);
    ok_long(*(volatile ULONG *)(ULONG_PTR)(USER_SHARED_DATA_ADDRESS + USER_SHARED_DATA_COMPLUS_PACKAGE), !Status);

    Queried = 0xdeadbeef;
    ReturnLength = 0;
    NtStatus = NtQuerySystemInformation(SystemComPlusPackage, &Queried, sizeof(Queried), &ReturnLength);
    ok_hex(NtStatus, STATUS_SUCCESS);
    ok_long(Queried, !Status);
    ok_long(ReturnLength, sizeof(Queried));

    Ret = pSetComPlusPackageInstallStatus(Status);
    ok_int(Ret, TRUE);
    ok_long(pGetComPlusPackageInstallStatus(), Shared);
}
