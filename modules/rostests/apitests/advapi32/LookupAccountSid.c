/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests LookupAccountSidW with application package and capability SIDs
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

START_TEST(LookupAccountSid)
{
    ULONG i;

    for (i = 0; i < ARRAYSIZE(Tests); i++)
        TestSid(&Tests[i]);
    for (i = 0; i < ARRAYSIZE(UnmappedSids); i++)
        TestUnmappedSid(UnmappedSids[i]);
}
