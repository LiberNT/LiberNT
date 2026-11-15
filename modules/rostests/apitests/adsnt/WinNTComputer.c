/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test that the WinNT computer object is a container of the local groups and users
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

static BOOL LookupName(PCWSTR SidString, WCHAR *Name, DWORD Size)
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

static IDispatch *GetChild(IADsContainer *Container, PCWSTR Class, PCWSTR Name, HRESULT *Result)
{
    BSTR ClassText = Class ? SysAllocString(Class) : NULL, NameText = SysAllocString(Name);
    IDispatch *Child = NULL;

    *Result = IADsContainer_GetObject(Container, ClassText, NameText, &Child);
    SysFreeString(ClassText);
    SysFreeString(NameText);
    return Child;
}

START_TEST(WinNTComputer)
{
    WCHAR Computer[MAX_COMPUTERNAME_LENGTH + 1], Group[256], Path[512], UserPath[512];
    DWORD Size = ARRAYSIZE(Computer);
    USER_INFO_0 *Users = NULL;
    DWORD Read = 0, Total = 0;
    IADsContainer *Container = NULL, *NotContainer = NULL;
    IADsGroup *AdsGroup = NULL;
    IDispatch *Child;
    IADs *Ads = NULL;
    VARIANT_BOOL Member;
    BSTR Text;
    HRESULT hr;

    ok(GetComputerNameW(Computer, &Size), "GetComputerNameW failed: %lu\n", GetLastError());
    ok(LookupName(L"S-1-5-32-559", Group, ARRAYSIZE(Group)), "No Performance Log Users group: %lu\n", GetLastError());
    ok(NetUserEnum(NULL, 0, FILTER_NORMAL_ACCOUNT, (LPBYTE *)&Users, MAX_PREFERRED_LENGTH, &Read, &Total, NULL) == NERR_Success &&
       Read != 0, "No user\n");
    if (!Read)
        return;

    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);

    _snwprintf(Path, ARRAYSIZE(Path) - 1, L"WinNT://%s,computer", Computer);
    Path[ARRAYSIZE(Path) - 1] = 0;
    hr = ADsGetObject(Path, &IID_IADsContainer, (void **)&Container);
    ok(hr == S_OK, "ADsGetObject(%s, IADsContainer) returned 0x%08lx\n", wine_dbgstr_w(Path), hr);
    if (FAILED(hr))
        goto Done;

    Child = GetChild(Container, L"group", Group, &hr);
    ok(hr == S_OK && Child != NULL, "GetObject(group, %s) returned 0x%08lx\n", wine_dbgstr_w(Group), hr);
    if (Child)
    {
        hr = IDispatch_QueryInterface(Child, &IID_IADsGroup, (void **)&AdsGroup);
        ok(hr == S_OK, "QueryInterface(IADsGroup) returned 0x%08lx\n", hr);
        hr = IDispatch_QueryInterface(Child, &IID_IADsContainer, (void **)&NotContainer);
        ok(hr == E_NOINTERFACE && NotContainer == NULL, "a group answered IADsContainer: 0x%08lx\n", hr);
        IDispatch_Release(Child);
    }
    if (AdsGroup)
    {
        hr = IADsGroup_get_Name(AdsGroup, &Text);
        ok(hr == S_OK && Text && !lstrcmpiW(Text, Group), "get_Name returned 0x%08lx, %s\n", hr, wine_dbgstr_w(Text));
        SysFreeString(Text);

        _snwprintf(UserPath, ARRAYSIZE(UserPath) - 1, L"WinNT://%s/%s", Computer, Users[0].usri0_name);
        UserPath[ARRAYSIZE(UserPath) - 1] = 0;
        Text = SysAllocString(UserPath);
        hr = IADsGroup_Add(AdsGroup, Text);
        ok(hr == S_OK, "Add(%s) returned 0x%08lx\n", wine_dbgstr_w(UserPath), hr);
        Member = VARIANT_FALSE;
        hr = IADsGroup_IsMember(AdsGroup, Text, &Member);
        ok(hr == S_OK && Member == VARIANT_TRUE, "IsMember returned 0x%08lx, %d\n", hr, Member);
        hr = IADsGroup_Remove(AdsGroup, Text);
        ok(hr == S_OK, "Remove returned 0x%08lx\n", hr);
        SysFreeString(Text);
        IADsGroup_Release(AdsGroup);
    }

    Child = GetChild(Container, L"user", Users[0].usri0_name, &hr);
    ok(hr == S_OK && Child != NULL, "GetObject(user, %s) returned 0x%08lx\n", wine_dbgstr_w(Users[0].usri0_name), hr);
    if (Child)
    {
        hr = IDispatch_QueryInterface(Child, &IID_IADs, (void **)&Ads);
        ok(hr == S_OK, "QueryInterface(IADs) returned 0x%08lx\n", hr);
        if (SUCCEEDED(hr))
        {
            hr = IADs_get_Class(Ads, &Text);
            ok(hr == S_OK && Text && !wcscmp(Text, L"User"), "get_Class returned 0x%08lx, %s\n", hr, wine_dbgstr_w(Text));
            SysFreeString(Text);
            IADs_Release(Ads);
        }
        IDispatch_Release(Child);
    }

    Child = GetChild(Container, L"group", L"NoSuchGroup2026", &hr);
    ok(FAILED(hr) && Child == NULL, "GetObject of a missing group returned 0x%08lx\n", hr);
    if (Child)
        IDispatch_Release(Child);

    IADsContainer_Release(Container);

Done:
    NetApiBufferFree(Users);
    CoUninitialize();
}
