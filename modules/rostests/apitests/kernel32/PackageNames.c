/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests package full name, family name and application user model ID parsing
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"

typedef LONG (WINAPI *PFN_PACKAGEFAMILYNAMEFROMFULLNAME)(PCWSTR, UINT32 *, PWSTR);
typedef LONG (WINAPI *PFN_PARSEAPPLICATIONUSERMODELID)(PCWSTR, UINT32 *, PWSTR, UINT32 *, PWSTR);
typedef LONG (WINAPI *PFN_GETPACKAGEFULLNAME)(HANDLE, UINT32 *, PWSTR);

static const WCHAR NotepadFullName[] = L"Microsoft.WindowsNotepad_11.2507.26.0_x64__8wekyb3d8bbwe";
static const WCHAR NotepadFamilyName[] = L"Microsoft.WindowsNotepad_8wekyb3d8bbwe";

static VOID
TestFamilyName(PFN_PACKAGEFAMILYNAMEFROMFULLNAME Function, PCWSTR FullName, LONG Expected, PCWSTR ExpectedFamily)
{
    WCHAR Family[128];
    UINT32 Length = _countof(Family);
    LONG Result;

    wcscpy(Family, L"unchanged");
    Result = Function(FullName, &Length, Family);
    ok(Result == Expected, "%s: result %ld\n", wine_dbgstr_w(FullName), Result);
    if (Expected == ERROR_SUCCESS)
    {
        ok(!wcscmp(Family, ExpectedFamily), "%s: family %s\n", wine_dbgstr_w(FullName), wine_dbgstr_w(Family));
        ok(Length == wcslen(ExpectedFamily) + 1, "%s: length %u\n", wine_dbgstr_w(FullName), Length);
    }
    else
    {
        ok(Length == _countof(Family), "%s: length %u\n", wine_dbgstr_w(FullName), Length);
        ok(!wcscmp(Family, L"unchanged"), "%s: family %s\n", wine_dbgstr_w(FullName), wine_dbgstr_w(Family));
    }
}

static VOID
TestPackageFamilyNameFromFullName(PFN_PACKAGEFAMILYNAMEFROMFULLNAME Function)
{
    WCHAR Family[8];
    UINT32 Length;
    LONG Result;

    Length = 0;
    Result = Function(NotepadFullName, &Length, NULL);
    ok(Result == ERROR_INSUFFICIENT_BUFFER, "size: result %ld\n", Result);
    ok(Length == _countof(NotepadFamilyName), "size: length %u\n", Length);

    Length = _countof(Family);
    Result = Function(NotepadFullName, &Length, Family);
    ok(Result == ERROR_INSUFFICIENT_BUFFER, "small: result %ld\n", Result);
    ok(Length == _countof(NotepadFamilyName), "small: length %u\n", Length);

    TestFamilyName(Function, NotepadFullName, ERROR_SUCCESS, NotepadFamilyName);
    TestFamilyName(Function, L"Microsoft.WindowsNotepad_11.2507.26.0_X64__8wekyb3d8bbwe", ERROR_SUCCESS, NotepadFamilyName);
    TestFamilyName(Function, L"Abc_1.0.0.0_neutral_~_8wekyb3d8bbwe", ERROR_SUCCESS, L"Abc_8wekyb3d8bbwe");
    TestFamilyName(Function, L"bogus", ERROR_INVALID_PARAMETER, NULL);
    TestFamilyName(Function, L"A_1.0.0.0_neutral_~_8wekyb3d8bbwe", ERROR_INVALID_PARAMETER, NULL);
    TestFamilyName(Function, L"Abc_1.0.0_neutral__8wekyb3d8bbwe", ERROR_INVALID_PARAMETER, NULL);
    TestFamilyName(Function, L"Abc_1.0.0.0_mips__8wekyb3d8bbwe", ERROR_INVALID_PARAMETER, NULL);
    TestFamilyName(Function, L"Abc_1.0.0.0_neutral__8wekyb3d8bbw", ERROR_INVALID_PARAMETER, NULL);
    TestFamilyName(Function, NotepadFamilyName, ERROR_INVALID_PARAMETER, NULL);
}

static VOID
TestParseApplicationUserModelId(PFN_PARSEAPPLICATIONUSERMODELID Function)
{
    WCHAR Family[128], Application[64];
    UINT32 FamilyLength, ApplicationLength;
    LONG Result;

    FamilyLength = _countof(Family);
    ApplicationLength = _countof(Application);
    Result = Function(L"Microsoft.WindowsNotepad_8wekyb3d8bbwe!App", &FamilyLength, Family, &ApplicationLength, Application);
    ok(Result == ERROR_SUCCESS, "parse: result %ld\n", Result);
    ok(FamilyLength == _countof(NotepadFamilyName), "parse: family length %u\n", FamilyLength);
    ok(ApplicationLength == 4, "parse: application length %u\n", ApplicationLength);
    if (Result == ERROR_SUCCESS)
    {
        ok(!wcscmp(Family, NotepadFamilyName), "parse: family %s\n", wine_dbgstr_w(Family));
        ok(!wcscmp(Application, L"App"), "parse: application %s\n", wine_dbgstr_w(Application));
    }

    FamilyLength = 0;
    ApplicationLength = 0;
    Result = Function(L"Microsoft.WindowsNotepad_8wekyb3d8bbwe!App", &FamilyLength, NULL, &ApplicationLength, NULL);
    ok(Result == ERROR_INSUFFICIENT_BUFFER, "size: result %ld\n", Result);
    ok(FamilyLength == _countof(NotepadFamilyName), "size: family length %u\n", FamilyLength);
    ok(ApplicationLength == 4, "size: application length %u\n", ApplicationLength);

    FamilyLength = _countof(Family);
    ApplicationLength = _countof(Application);
    Result = Function(L"noexclam", &FamilyLength, Family, &ApplicationLength, Application);
    ok(Result == ERROR_INVALID_PARAMETER, "no separator: result %ld\n", Result);

    FamilyLength = _countof(Family);
    ApplicationLength = _countof(Application);
    Result = Function(L"Microsoft.WindowsNotepad_8wekyb3d8bbwe!", &FamilyLength, Family, &ApplicationLength, Application);
    ok(Result == ERROR_INVALID_PARAMETER, "no application: result %ld\n", Result);

    FamilyLength = _countof(Family);
    ApplicationLength = _countof(Application);
    Result = Function(L"bad!App", &FamilyLength, Family, &ApplicationLength, Application);
    ok(Result == ERROR_INVALID_PARAMETER, "bad family: result %ld\n", Result);
}

START_TEST(PackageNames)
{
    HMODULE Kernel32 = GetModuleHandleW(L"kernel32.dll");
    PFN_PACKAGEFAMILYNAMEFROMFULLNAME pPackageFamilyNameFromFullName;
    PFN_PARSEAPPLICATIONUSERMODELID pParseApplicationUserModelId;
    PFN_GETPACKAGEFULLNAME pGetPackageFullName;
    UINT32 Length;
    LONG Result;

    pPackageFamilyNameFromFullName = (PFN_PACKAGEFAMILYNAMEFROMFULLNAME)GetProcAddress(Kernel32, "PackageFamilyNameFromFullName");
    pParseApplicationUserModelId = (PFN_PARSEAPPLICATIONUSERMODELID)GetProcAddress(Kernel32, "ParseApplicationUserModelId");
    pGetPackageFullName = (PFN_GETPACKAGEFULLNAME)GetProcAddress(Kernel32, "GetPackageFullName");

    ok(pPackageFamilyNameFromFullName != NULL, "PackageFamilyNameFromFullName is missing\n");
    ok(pParseApplicationUserModelId != NULL, "ParseApplicationUserModelId is missing\n");
    ok(pGetPackageFullName != NULL, "GetPackageFullName is missing\n");

    if (pPackageFamilyNameFromFullName)
        TestPackageFamilyNameFromFullName(pPackageFamilyNameFromFullName);
    if (pParseApplicationUserModelId)
        TestParseApplicationUserModelId(pParseApplicationUserModelId);
    if (pGetPackageFullName)
    {
        Length = 0;
        Result = pGetPackageFullName(GetCurrentProcess(), &Length, NULL);
        ok(Result == APPMODEL_ERROR_NO_PACKAGE, "GetPackageFullName result %ld\n", Result);
        ok(Length == 0, "GetPackageFullName length %u\n", Length);
    }
}
