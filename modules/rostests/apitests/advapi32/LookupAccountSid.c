/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests LookupAccountSidW and LookupAccountSidLocalW
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"
#include <sddl.h>

typedef struct
{
    PCWSTR Sid;
    PCWSTR Name;
} SID_TEST;

static const SID_TEST Tests[] =
{
    { L"S-1-15-2-1", L"ALL APPLICATION PACKAGES" },
    { L"S-1-15-2-2", L"ALL RESTRICTED APPLICATION PACKAGES" },
    { L"S-1-15-3-1", L"Your Internet connection" },
    { L"S-1-15-3-2", L"Your Internet connection, including incoming connections from the Internet" },
    { L"S-1-15-3-3", L"Your home or work networks" },
    { L"S-1-15-3-4", L"Your pictures library" },
    { L"S-1-15-3-5", L"Your videos library" },
    { L"S-1-15-3-6", L"Your music library" },
    { L"S-1-15-3-7", L"Your documents library" },
    { L"S-1-15-3-8", L"Your Windows credentials" },
    { L"S-1-15-3-9", L"Software and hardware certificates or a smart card" },
    { L"S-1-15-3-10", L"Removable storage" },
    { L"S-1-15-3-11", L"Your Appointments" },
    { L"S-1-15-3-12", L"Your Contacts" },
};

static const PCWSTR UnmappedSids[] =
{
    L"S-1-15-2-3",
    L"S-1-15-3-1024",
};

static void
TestSid(const SID_TEST *Test)
{
    WCHAR Name[256], Domain[256];
    DWORD cchName = ARRAYSIZE(Name), cchDomain = ARRAYSIZE(Domain);
    SID_NAME_USE Use = SidTypeUnknown;
    PSID Sid = NULL;
    BOOL Ret;

    ok(ConvertStringSidToSidW(Test->Sid, &Sid), "%s: ConvertStringSidToSidW failed: %lu\n",
       wine_dbgstr_w(Test->Sid), GetLastError());
    if (!Sid)
        return;

    SetLastError(0xdeadbeef);
    Ret = LookupAccountSidW(NULL, Sid, Name, &cchName, Domain, &cchDomain, &Use);
    ok(Ret, "%s: lookup failed with error %lu\n", wine_dbgstr_w(Test->Sid), GetLastError());
    if (Ret)
    {
        ok(!wcscmp(Name, Test->Name), "%s: name is %s\n", wine_dbgstr_w(Test->Sid), wine_dbgstr_w(Name));
        ok(!wcscmp(Domain, L"APPLICATION PACKAGE AUTHORITY"), "%s: domain is %s\n",
           wine_dbgstr_w(Test->Sid), wine_dbgstr_w(Domain));
        ok(Use == SidTypeWellKnownGroup, "%s: use is %d\n", wine_dbgstr_w(Test->Sid), Use);
    }
    LocalFree(Sid);
}

static void
TestUnmappedSid(PCWSTR SidString)
{
    WCHAR Name[256], Domain[256];
    DWORD cchName = ARRAYSIZE(Name), cchDomain = ARRAYSIZE(Domain);
    SID_NAME_USE Use;
    PSID Sid = NULL;
    BOOL Ret;

    ok(ConvertStringSidToSidW(SidString, &Sid), "%s: ConvertStringSidToSidW failed: %lu\n",
       wine_dbgstr_w(SidString), GetLastError());
    if (!Sid)
        return;

    SetLastError(0xdeadbeef);
    Ret = LookupAccountSidW(NULL, Sid, Name, &cchName, Domain, &cchDomain, &Use);
    ok(!Ret, "%s: lookup succeeded\n", wine_dbgstr_w(SidString));
    ok(GetLastError() == ERROR_NONE_MAPPED, "%s: error %lu\n", wine_dbgstr_w(SidString), GetLastError());
    LocalFree(Sid);
}

typedef BOOL (WINAPI *PFN_LOOKUPACCOUNTSIDLOCALW)(PSID, LPWSTR, LPDWORD, LPWSTR, LPDWORD, PSID_NAME_USE);

static VOID
TestLocalLookup(PFN_LOOKUPACCOUNTSIDLOCALW Function, PCWSTR SidString)
{
    WCHAR Name[256], Domain[256], LocalName[256], LocalDomain[256];
    DWORD cchName = ARRAYSIZE(Name), cchDomain = ARRAYSIZE(Domain);
    DWORD cchLocalName = ARRAYSIZE(LocalName), cchLocalDomain = ARRAYSIZE(LocalDomain);
    SID_NAME_USE Use = SidTypeUnknown, LocalUse = SidTypeUnknown;
    PSID Sid = NULL;
    BOOL Ret, LocalRet;

    ok(ConvertStringSidToSidW(SidString, &Sid), "%s: ConvertStringSidToSidW failed: %lu\n",
       wine_dbgstr_w(SidString), GetLastError());
    if (!Sid)
        return;

    Ret = LookupAccountSidW(NULL, Sid, Name, &cchName, Domain, &cchDomain, &Use);
    LocalRet = Function(Sid, LocalName, &cchLocalName, LocalDomain, &cchLocalDomain, &LocalUse);
    ok(Ret && LocalRet, "%s: lookup %d, local lookup %d, error %lu\n", wine_dbgstr_w(SidString), Ret, LocalRet, GetLastError());
    if (Ret && LocalRet)
    {
        ok(!wcscmp(Name, LocalName), "%s: local name %s\n", wine_dbgstr_w(SidString), wine_dbgstr_w(LocalName));
        ok(!wcscmp(Domain, LocalDomain), "%s: local domain %s\n", wine_dbgstr_w(SidString), wine_dbgstr_w(LocalDomain));
        ok(Use == LocalUse, "%s: local use %d, use %d\n", wine_dbgstr_w(SidString), LocalUse, Use);
        ok(cchLocalName == cchName && cchLocalDomain == cchDomain, "%s: local lengths %lu %lu\n",
           wine_dbgstr_w(SidString), cchLocalName, cchLocalDomain);
    }
    LocalFree(Sid);
}

static VOID
TestLookupAccountSidLocal(VOID)
{
    PFN_LOOKUPACCOUNTSIDLOCALW pLookupAccountSidLocalW;
    HMODULE Sechost;

    ok(GetProcAddress(GetModuleHandleW(L"advapi32.dll"), "LookupAccountSidLocalW") == NULL,
       "advapi32 exports LookupAccountSidLocalW\n");

    Sechost = LoadLibraryW(L"sechost.dll");
    ok(Sechost != NULL, "sechost.dll failed to load, error %lu\n", GetLastError());
    if (!Sechost)
        return;

    pLookupAccountSidLocalW = (PFN_LOOKUPACCOUNTSIDLOCALW)GetProcAddress(Sechost, "LookupAccountSidLocalW");
    ok(pLookupAccountSidLocalW != NULL, "sechost does not export LookupAccountSidLocalW\n");
    if (pLookupAccountSidLocalW)
    {
        TestLocalLookup(pLookupAccountSidLocalW, L"S-1-5-18");
        TestLocalLookup(pLookupAccountSidLocalW, L"S-1-15-2-1");
        TestLocalLookup(pLookupAccountSidLocalW, L"S-1-1-0");
    }
    FreeLibrary(Sechost);
}

START_TEST(LookupAccountSid)
{
    ULONG i;

    for (i = 0; i < ARRAYSIZE(Tests); i++)
        TestSid(&Tests[i]);
    for (i = 0; i < ARRAYSIZE(UnmappedSids); i++)
        TestUnmappedSid(UnmappedSids[i]);
    TestLookupAccountSidLocal();
}
