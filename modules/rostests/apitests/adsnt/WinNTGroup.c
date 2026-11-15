/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test that WinNT:// paths open the local computer's groups and users and change a group's members
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#define COBJMACROS
#include <apitest.h>
#include <windows.h>
#include <objbase.h>
#include <iads.h>
#include <adshlp.h>
#include <lm.h>
#include <sddl.h>
#include <stdio.h>

static BOOL
LookupName(PCWSTR SidString, WCHAR *Name, DWORD Size)
{
    WCHAR Domain[256];
    DWORD DomainSize = ARRAYSIZE(Domain);
    SID_NAME_USE Use;
    PSID Sid;
    BOOL Ret;

    if (!ConvertStringSidToSidW(SidString, &Sid))
        return FALSE;
    Ret = LookupAccountSidW(NULL, Sid, Name, &Size, Domain, &DomainSize, &Use);
    LocalFree(Sid);
    return Ret;
}

START_TEST(WinNTGroup)
{
    WCHAR Computer[MAX_COMPUTERNAME_LENGTH + 1], Group[256], GroupPath[512], UserPath[512];
    DWORD Size = ARRAYSIZE(Computer);
    USER_INFO_0 *Users = NULL;
    DWORD Read = 0, Total = 0;
    IADsGroup *AdsGroup = NULL;
    IADs *Ads = NULL;
    VARIANT_BOOL Member;
    BSTR Text, Path;
    HRESULT hr;

    ok(GetComputerNameW(Computer, &Size), "GetComputerNameW failed: %lu\n", GetLastError());
    ok(LookupName(L"S-1-5-32-559", Group, ARRAYSIZE(Group)), "No Performance Log Users group: %lu\n", GetLastError());
    ok(NetUserEnum(NULL, 0, FILTER_NORMAL_ACCOUNT, (LPBYTE *)&Users, MAX_PREFERRED_LENGTH, &Read, &Total, NULL) == NERR_Success &&
       Read != 0, "No user to add\n");
    if (!Read)
        return;

    _snwprintf(GroupPath, ARRAYSIZE(GroupPath) - 1, L"WinNT://%s/%s,group", Computer, Group);
    GroupPath[ARRAYSIZE(GroupPath) - 1] = 0;
    _snwprintf(UserPath, ARRAYSIZE(UserPath) - 1, L"WinNT://%s/%s", Computer, Users[0].usri0_name);
    UserPath[ARRAYSIZE(UserPath) - 1] = 0;

    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);

    hr = ADsGetObject(GroupPath, &IID_IADsGroup, (void **)&AdsGroup);
    ok(hr == S_OK, "ADsGetObject(%ls) returned %08lx\n", GroupPath, hr);
    if (SUCCEEDED(hr))
    {
        ok(IADsGroup_get_Name(AdsGroup, &Text) == S_OK && !wcscmp(Text, Group), "Name is %ls\n", Text);
        SysFreeString(Text);
        ok(IADsGroup_get_Class(AdsGroup, &Text) == S_OK && !wcscmp(Text, L"Group"), "Class is %ls\n", Text);
        SysFreeString(Text);

        Path = SysAllocString(UserPath);
        Member = VARIANT_TRUE;
        ok(IADsGroup_IsMember(AdsGroup, Path, &Member) == S_OK && Member == VARIANT_FALSE, "%ls is already a member\n", UserPath);
        hr = IADsGroup_Add(AdsGroup, Path);
        ok(hr == S_OK, "Add(%ls) returned %08lx\n", UserPath, hr);
        ok(IADsGroup_IsMember(AdsGroup, Path, &Member) == S_OK && Member == VARIANT_TRUE, "%ls was not added\n", UserPath);
        hr = IADsGroup_Add(AdsGroup, Path);
        ok(hr == HRESULT_FROM_WIN32(ERROR_MEMBER_IN_ALIAS), "Adding twice returned %08lx\n", hr);
        hr = IADsGroup_Remove(AdsGroup, Path);
        ok(hr == S_OK, "Remove(%ls) returned %08lx\n", UserPath, hr);
        ok(IADsGroup_IsMember(AdsGroup, Path, &Member) == S_OK && Member == VARIANT_FALSE, "%ls was not removed\n", UserPath);
        SysFreeString(Path);
        IADsGroup_Release(AdsGroup);
    }

    hr = ADsGetObject(UserPath, &IID_IADs, (void **)&Ads);
    ok(hr == S_OK, "ADsGetObject(%ls) returned %08lx\n", UserPath, hr);
    if (SUCCEEDED(hr))
    {
        ok(IADs_get_Class(Ads, &Text) == S_OK && !wcscmp(Text, L"User"), "Class is %ls\n", Text);
        SysFreeString(Text);
        IADs_Release(Ads);
    }

    CoUninitialize();
    NetApiBufferFree(Users);
}
