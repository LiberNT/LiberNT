/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test that a present audio render filter is offered as a render endpoint and a waveOut device
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#define COBJMACROS
#include <apitest.h>
#include <windows.h>
#include <objbase.h>
#include <mmsystem.h>
#include <mmdeviceapi.h>
#include <setupapi.h>
#include <ks.h>
#include <ksmedia.h>

static const GUID RenderCategory = { STATIC_KSCATEGORY_RENDER };
static const GUID AudioCategory = { STATIC_KSCATEGORY_AUDIO };

static UINT
CountRenderFilters(VOID)
{
    SP_DEVICE_INTERFACE_DATA Interface, Alias;
    HDEVINFO Set;
    DWORD Index;
    UINT Count = 0;

    Set = SetupDiGetClassDevsW(&RenderCategory, NULL, NULL, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    ok(Set != INVALID_HANDLE_VALUE, "SetupDiGetClassDevsW failed with %lu\n", GetLastError());
    if (Set == INVALID_HANDLE_VALUE)
        return 0;

    for (Index = 0; ; Index++)
    {
        Interface.cbSize = sizeof(Interface);
        if (!SetupDiEnumDeviceInterfaces(Set, NULL, &RenderCategory, Index, &Interface))
            break;
        Alias.cbSize = sizeof(Alias);
        if (SetupDiGetDeviceInterfaceAlias(Set, &Interface, &AudioCategory, &Alias))
            Count++;
    }

    SetupDiDestroyDeviceInfoList(Set);
    return Count;
}

static UINT
CountActiveRenderEndpoints(VOID)
{
    IMMDeviceEnumerator *Enumerator = NULL;
    IMMDeviceCollection *Collection = NULL;
    UINT Count = 0;
    HRESULT hr;

    hr = CoCreateInstance(&CLSID_MMDeviceEnumerator, NULL, CLSCTX_INPROC_SERVER,
                          &IID_IMMDeviceEnumerator, (void **)&Enumerator);
    ok(hr == S_OK, "CoCreateInstance returned 0x%08lx\n", hr);
    if (FAILED(hr))
        return 0;

    hr = IMMDeviceEnumerator_EnumAudioEndpoints(Enumerator, eRender, DEVICE_STATE_ACTIVE, &Collection);
    ok(hr == S_OK, "EnumAudioEndpoints returned 0x%08lx\n", hr);
    if (SUCCEEDED(hr))
    {
        hr = IMMDeviceCollection_GetCount(Collection, &Count);
        ok(hr == S_OK, "GetCount returned 0x%08lx\n", hr);
        IMMDeviceCollection_Release(Collection);
    }
    IMMDeviceEnumerator_Release(Enumerator);
    return Count;
}

START_TEST(renderendpoint)
{
    IMMDeviceEnumerator *Enumerator;
    IMMDevice *Device;
    UINT Filters, Endpoints;
    HRESULT hr;

    Filters = CountRenderFilters();
    trace("%u audio render filters\n", Filters);
    if (!Filters)
    {
        skip("No audio render filter is present\n");
        return;
    }

    hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    ok(hr == S_OK, "CoInitializeEx returned 0x%08lx\n", hr);
    if (FAILED(hr))
        return;

    Endpoints = CountActiveRenderEndpoints();
    ok(Endpoints > 0, "%u audio render filters but no active render endpoint\n", Filters);

    hr = CoCreateInstance(&CLSID_MMDeviceEnumerator, NULL, CLSCTX_INPROC_SERVER,
                          &IID_IMMDeviceEnumerator, (void **)&Enumerator);
    if (SUCCEEDED(hr))
    {
        Device = NULL;
        hr = IMMDeviceEnumerator_GetDefaultAudioEndpoint(Enumerator, eRender, eConsole, &Device);
        ok(hr == S_OK, "GetDefaultAudioEndpoint returned 0x%08lx\n", hr);
        if (Device)
            IMMDevice_Release(Device);
        IMMDeviceEnumerator_Release(Enumerator);
    }

    ok(waveOutGetNumDevs() > 0, "%u audio render filters but no waveOut device\n", Filters);

    CoUninitialize();
}
