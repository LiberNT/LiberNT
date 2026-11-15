/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test that WinNT:// display names bind through the WinNT provider object, as GetObject does
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#define COBJMACROS
#include <apitest.h>
#include <windows.h>
#include <objbase.h>
#include <iads.h>
#include <sddl.h>
#include <stdio.h>

static const CLSID ClsidWinNTProvider = { 0x8b20cd60, 0x0f29, 0x11cf, { 0xab, 0xc4, 0x02, 0x60, 0x8c, 0x9e, 0x75, 0x53 } };

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

START_TEST(WinNTMoniker)
{
    WCHAR Computer[MAX_COMPUTERNAME_LENGTH + 1], Group[256], Path[512];
    DWORD Size = ARRAYSIZE(Computer);
    IADsGroup *AdsGroup = NULL;
    IMoniker *Moniker = NULL;
    IBindCtx *BindCtx = NULL;
    ULONG Eaten = 0;
    CLSID Clsid;
    BSTR Text;
    HRESULT hr;

    ok(GetComputerNameW(Computer, &Size), "GetComputerNameW failed: %lu\n", GetLastError());
    ok(LookupName(L"S-1-5-32-544", Group, ARRAYSIZE(Group)), "No Administrators group: %lu\n", GetLastError());
    _snwprintf(Path, ARRAYSIZE(Path) - 1, L"WinNT://%s/%s,group", Computer, Group);
    Path[ARRAYSIZE(Path) - 1] = 0;

    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);

    hr = CLSIDFromProgID(L"WinNT", &Clsid);
    ok(hr == S_OK, "CLSIDFromProgID(WinNT) returned 0x%08lx\n", hr);
    ok(IsEqualCLSID(&Clsid, &ClsidWinNTProvider), "WinNT is %s\n", wine_dbgstr_guid(&Clsid));

    hr = CoGetObject(Path, NULL, &IID_IADsGroup, (void **)&AdsGroup);
    ok(hr == S_OK, "CoGetObject(%s) returned 0x%08lx\n", wine_dbgstr_w(Path), hr);
    if (SUCCEEDED(hr))
    {
        hr = IADsGroup_get_Name(AdsGroup, &Text);
        ok(hr == S_OK && Text && !lstrcmpiW(Text, Group), "get_Name returned 0x%08lx, %s\n", hr, wine_dbgstr_w(Text));
        SysFreeString(Text);
        hr = IADsGroup_get_Class(AdsGroup, &Text);
        ok(hr == S_OK && Text && !wcscmp(Text, L"Group"), "get_Class returned 0x%08lx, %s\n", hr, wine_dbgstr_w(Text));
        SysFreeString(Text);
        IADsGroup_Release(AdsGroup);
    }

    hr = CreateBindCtx(0, &BindCtx);
    ok(hr == S_OK, "CreateBindCtx returned 0x%08lx\n", hr);
    if (SUCCEEDED(hr))
    {
        hr = MkParseDisplayName(BindCtx, Path, &Eaten, &Moniker);
        ok(hr == S_OK, "MkParseDisplayName returned 0x%08lx\n", hr);
        ok(Eaten == wcslen(Path), "MkParseDisplayName ate %lu of %u characters\n", Eaten, (unsigned)wcslen(Path));
        if (SUCCEEDED(hr))
        {
            hr = IMoniker_BindToObject(Moniker, BindCtx, NULL, &IID_IADsGroup, (void **)&AdsGroup);
            ok(hr == S_OK, "BindToObject returned 0x%08lx\n", hr);
            if (SUCCEEDED(hr))
                IADsGroup_Release(AdsGroup);
            IMoniker_Release(Moniker);
        }
        IBindCtx_Release(BindCtx);
    }

    _snwprintf(Path, ARRAYSIZE(Path) - 1, L"WinNT://%s/NoSuchGroup2026,group", Computer);
    Path[ARRAYSIZE(Path) - 1] = 0;
    AdsGroup = NULL;
    hr = CoGetObject(Path, NULL, &IID_IADsGroup, (void **)&AdsGroup);
    ok(FAILED(hr) && AdsGroup == NULL, "CoGetObject of a missing group returned 0x%08lx\n", hr);

    CoUninitialize();
}
