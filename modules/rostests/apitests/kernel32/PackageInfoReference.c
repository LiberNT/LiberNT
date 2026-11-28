/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests package information references for packages that are not installed
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"
#include <appmodel.h>

typedef LONG (WINAPI *PFN_OPENPACKAGEINFOBYFULLNAME)(PCWSTR, const UINT32, PACKAGE_INFO_REFERENCE *);
typedef LONG (WINAPI *PFN_CLOSEPACKAGEINFO)(PACKAGE_INFO_REFERENCE);
typedef LONG (WINAPI *PFN_GETPACKAGEAPPLICATIONIDS)(PACKAGE_INFO_REFERENCE, UINT32 *, BYTE *, UINT32 *);

static const WCHAR MissingFullName[] = L"Microsoft.LiberNTMissing_1.0.0.0_x64__8wekyb3d8bbwe";
static PACKAGE_INFO_REFERENCE const Untouched = (PACKAGE_INFO_REFERENCE)(ULONG_PTR)0x1234;

static VOID
TestOpen(PFN_OPENPACKAGEINFOBYFULLNAME Open)
{
    PACKAGE_INFO_REFERENCE Reference;
    LONG Result;

    Reference = Untouched;
    SetLastError(0xdeadbeef);
    Result = Open(NULL, 0, &Reference);
    ok(Result == ERROR_INVALID_PARAMETER, "NULL name: result %ld\n", Result);
    ok(Reference == Untouched, "NULL name: reference %p\n", Reference);
    ok(GetLastError() == 0xdeadbeef, "NULL name: last error %lu\n", GetLastError());

    SetLastError(0xdeadbeef);
    Result = Open(MissingFullName, 0, NULL);
    ok(Result == ERROR_INVALID_PARAMETER, "NULL reference: result %ld\n", Result);
    ok(GetLastError() == 0xdeadbeef, "NULL reference: last error %lu\n", GetLastError());

    Reference = Untouched;
    Result = Open(MissingFullName, 0, &Reference);
    ok(Result == ERROR_NOT_FOUND, "missing package: result %ld\n", Result);
    ok(Reference == Untouched, "missing package: reference %p\n", Reference);

    Reference = Untouched;
    Result = Open(MissingFullName, 1, &Reference);
    ok(Result != ERROR_INVALID_PARAMETER && Result != ERROR_SUCCESS, "reserved flag: result %ld\n", Result);
    ok(Reference == Untouched, "reserved flag: reference %p\n", Reference);

    Reference = Untouched;
    Result = Open(L"notafullname", 0, &Reference);
    ok(Result == ERROR_INVALID_PARAMETER, "malformed name: result %ld\n", Result);
    ok(Reference == Untouched, "malformed name: reference %p\n", Reference);
}

static VOID
TestNullReference(PFN_CLOSEPACKAGEINFO Close, PFN_GETPACKAGEAPPLICATIONIDS GetIds)
{
    BYTE Buffer[64];
    UINT32 Length, Count;
    LONG Result;

    SetLastError(0xdeadbeef);
    Result = Close(NULL);
    ok(Result == ERROR_INVALID_PARAMETER, "close NULL: result %ld\n", Result);
    ok(GetLastError() == 0xdeadbeef, "close NULL: last error %lu\n", GetLastError());

    Length = 77;
    Count = 77;
    SetLastError(0xdeadbeef);
    Result = GetIds(NULL, &Length, NULL, &Count);
    ok(Result == ERROR_INVALID_PARAMETER, "ids NULL: result %ld\n", Result);
    ok(Length == 77, "ids NULL: length %u\n", Length);
    ok(Count == 77, "ids NULL: count %u\n", Count);
    ok(GetLastError() == 0xdeadbeef, "ids NULL: last error %lu\n", GetLastError());

    Length = sizeof(Buffer);
    Count = 77;
    Result = GetIds(NULL, &Length, Buffer, &Count);
    ok(Result == ERROR_INVALID_PARAMETER, "ids NULL with buffer: result %ld\n", Result);
    ok(Length == sizeof(Buffer), "ids NULL with buffer: length %u\n", Length);
    ok(Count == 77, "ids NULL with buffer: count %u\n", Count);

    Result = GetIds(NULL, NULL, NULL, NULL);
    ok(Result == ERROR_INVALID_PARAMETER, "ids all NULL: result %ld\n", Result);
}

START_TEST(PackageInfoReference)
{
    HMODULE Kernel32 = GetModuleHandleW(L"kernel32.dll");
    PFN_OPENPACKAGEINFOBYFULLNAME Open = (PFN_OPENPACKAGEINFOBYFULLNAME)GetProcAddress(Kernel32, "OpenPackageInfoByFullName");
    PFN_CLOSEPACKAGEINFO Close = (PFN_CLOSEPACKAGEINFO)GetProcAddress(Kernel32, "ClosePackageInfo");
    PFN_GETPACKAGEAPPLICATIONIDS GetIds = (PFN_GETPACKAGEAPPLICATIONIDS)GetProcAddress(Kernel32, "GetPackageApplicationIds");

    ok(Open != NULL, "OpenPackageInfoByFullName is not exported\n");
    ok(Close != NULL, "ClosePackageInfo is not exported\n");
    ok(GetIds != NULL, "GetPackageApplicationIds is not exported\n");
    if (!Open || !Close || !GetIds)
        return;

    TestOpen(Open);
    TestNullReference(Close, GetIds);
}
