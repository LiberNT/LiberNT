/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test PackageFamilyNameFromId and the package information references
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"
#include <appmodel.h>

typedef LONG (WINAPI *PFN_FAMILY)(const PACKAGE_ID *, UINT32 *, PWSTR);
typedef LONG (WINAPI *PFN_OPEN)(PCWSTR, UINT32, PACKAGE_INFO_REFERENCE *);
typedef LONG (WINAPI *PFN_GET)(PACKAGE_INFO_REFERENCE, UINT32, UINT32 *, BYTE *, UINT32 *);
typedef LONG (WINAPI *PFN_CLOSE)(PACKAGE_INFO_REFERENCE);

static const WCHAR MicrosoftPublisher[] = L"CN=Microsoft Corporation, O=Microsoft Corporation, L=Redmond, S=Washington, C=US";

START_TEST(PackageInfo)
{
    HMODULE Kernel32 = GetModuleHandleW(L"kernel32.dll");
    PFN_FAMILY Family = (PFN_FAMILY)GetProcAddress(Kernel32, "PackageFamilyNameFromId");
    PFN_OPEN Open = (PFN_OPEN)GetProcAddress(Kernel32, "OpenPackageInfoByFullName");
    PFN_GET Get = (PFN_GET)GetProcAddress(Kernel32, "GetPackageInfo");
    PFN_CLOSE Close = (PFN_CLOSE)GetProcAddress(Kernel32, "ClosePackageInfo");
    PACKAGE_INFO_REFERENCE Reference;
    PACKAGE_ID Id = { 0 };
    WCHAR Name[128];
    UINT32 Length, Size, Count;

    ok(Family && Open && Get && Close, "kernel32 lacks a package export\n");
    if (!Family || !Open || !Get || !Close)
        return;

    Id.name = (PWSTR)L"Microsoft.WindowsCalculator";
    Id.publisher = (PWSTR)MicrosoftPublisher;
    Id.processorArchitecture = PROCESSOR_ARCHITECTURE_ARM64;
    Length = ARRAYSIZE(Name);
    ok_long(Family(&Id, &Length, Name), ERROR_SUCCESS);
    ok(!wcscmp(Name, L"Microsoft.WindowsCalculator_8wekyb3d8bbwe"), "family name %ls\n", Name);
    ok_int(Length, 42);

    Length = 0;
    ok_long(Family(&Id, &Length, NULL), ERROR_INSUFFICIENT_BUFFER);
    ok_int(Length, 42);
    Length = 5;
    ok_long(Family(&Id, &Length, Name), ERROR_INSUFFICIENT_BUFFER);
    ok_int(Length, 42);
    ok_long(Family(&Id, NULL, Name), ERROR_INVALID_PARAMETER);
    Length = ARRAYSIZE(Name);
    ok_long(Family(NULL, &Length, Name), ERROR_INVALID_PARAMETER);

    Id.publisher = (PWSTR)L"CN=LiberNT Test";
    Length = ARRAYSIZE(Name);
    ok_long(Family(&Id, &Length, Name), ERROR_SUCCESS);
    ok(!wcscmp(Name, L"Microsoft.WindowsCalculator_3m4fkjhr2ab88"), "family name %ls\n", Name);
    Id.publisher = NULL;
    Length = ARRAYSIZE(Name);
    ok_long(Family(&Id, &Length, Name), ERROR_INVALID_PARAMETER);

    Reference = (PACKAGE_INFO_REFERENCE)(ULONG_PTR)0xdead;
    ok_long(Open(L"LiberNT.NoSuchPackage_1.0.0.0_x64__8wekyb3d8bbwe", 0, &Reference), ERROR_NOT_FOUND);
    ok(Reference == (PACKAGE_INFO_REFERENCE)(ULONG_PTR)0xdead, "reference changed to %p\n", Reference);
    ok_long(Open(L"not a package name", 0, &Reference), ERROR_INVALID_PARAMETER);
    ok_long(Open(NULL, 0, &Reference), ERROR_INVALID_PARAMETER);
    ok_long(Open(L"LiberNT.NoSuchPackage_1.0.0.0_x64__8wekyb3d8bbwe", 0, NULL), ERROR_INVALID_PARAMETER);

    Size = 0;
    Count = 0xdead;
    ok_long(Get(NULL, PACKAGE_FILTER_HEAD, &Size, NULL, &Count), ERROR_INVALID_PARAMETER);
    ok_int(Size, 0);
    ok_int(Count, 0xdead);
    ok_long(Close(NULL), ERROR_INVALID_PARAMETER);
}
