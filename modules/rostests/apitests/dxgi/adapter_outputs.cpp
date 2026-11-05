/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     DXGI enumerates the primary display output on the first adapter only
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arifing193@gmail.com>
 */

#include <apitest.h>
#include <initguid.h>
#include <dxgi1_6.h>

typedef HRESULT (WINAPI *CREATE_FACTORY1)(REFIID, void **);

static BOOL GetPrimaryDisplay(WCHAR *Name, UINT Count)
{
    DISPLAY_DEVICEW Device = {sizeof(Device)};

    for (DWORD i = 0; EnumDisplayDevicesW(NULL, i, &Device, 0); ++i)
    {
        if (Device.StateFlags & DISPLAY_DEVICE_PRIMARY_DEVICE)
        {
            lstrcpynW(Name, Device.DeviceName, Count);
            return TRUE;
        }
    }
    return FALSE;
}

static UINT CountPrimary(IDXGIAdapter1 *Adapter, UINT Index, const WCHAR *Primary)
{
    IDXGIOutput *Output;
    UINT Found = 0, o;
    HRESULT Hr;

    for (o = 0; SUCCEEDED(Hr = Adapter->EnumOutputs(o, &Output)); ++o)
    {
        DXGI_OUTPUT_DESC Desc = {};
        Output->GetDesc(&Desc);
        Found += !lstrcmpiW(Desc.DeviceName, Primary);
        Output->Release();
    }
    ok(Hr == DXGI_ERROR_NOT_FOUND, "adapter %u: output %u returned %#lx\n", Index, o, Hr);
    return Found;
}

START_TEST(adapter_outputs)
{
    HMODULE Dxgi = LoadLibraryW(L"dxgi.dll");
    CREATE_FACTORY1 Create = Dxgi ? reinterpret_cast<CREATE_FACTORY1>(GetProcAddress(Dxgi, "CreateDXGIFactory1")) : NULL;
    IDXGIFactory1 *Factory = NULL;
    IDXGIAdapter1 *Adapter;
    WCHAR Primary[32];
    UINT Holders = 0, Holder = ~0u;

    if (!Create || FAILED(Create(IID_IDXGIFactory1, reinterpret_cast<void **>(&Factory))))
    {
        skip("no DXGI factory\n");
        return;
    }
    if (!GetPrimaryDisplay(Primary, ARRAYSIZE(Primary)))
    {
        skip("no primary display\n");
        Factory->Release();
        return;
    }
    for (UINT i = 0; SUCCEEDED(Factory->EnumAdapters1(i, &Adapter)); ++i)
    {
        UINT Found = CountPrimary(Adapter, i, Primary);
        ok(Found <= 1, "adapter %u enumerates %ls %u times\n", i, Primary, Found);
        if (Found)
        {
            ++Holders;
            Holder = i;
        }
        Adapter->Release();
    }
    ok(Holders == 1 && Holder == 0, "%ls enumerated by %u adapters, last %u\n", Primary, Holders, Holder);
    Factory->Release();
    FreeLibrary(Dxgi);
}
