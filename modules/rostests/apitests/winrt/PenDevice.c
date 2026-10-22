/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests Windows.Devices.Input.PenDevice activation and pointer lookup
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "winrt_apitest.h"

#define WIDL_using_Windows_Devices_Input
#include <windows.devices.input.h>

START_TEST(PenDevice)
{
    IActivationFactory *Factory;
    IPenDeviceStatics *Statics;
    IInspectable *Instance;
    IPenDevice *Device;
    HRESULT hr;

    hr = RoInitialize(RO_INIT_MULTITHREADED);
    ok(SUCCEEDED(hr), "RoInitialize failed, hr %#lx\n", hr);

    TraceActivatableClass(RuntimeClass_Windows_Devices_Input_PenDevice);
    hr = GetFactory(RuntimeClass_Windows_Devices_Input_PenDevice, &IID_IActivationFactory, (void **)&Factory);
    ok(hr == S_OK, "RoGetActivationFactory failed, hr %#lx\n", hr);
    if (FAILED(hr))
    {
        RoUninitialize();
        return;
    }

    Instance = (IInspectable *)(ULONG_PTR)0xdeadbeef;
    hr = IActivationFactory_ActivateInstance(Factory, &Instance);
    ok(hr == E_NOTIMPL, "ActivateInstance returned hr %#lx\n", hr);
    ok(Instance == NULL, "ActivateInstance returned instance %p\n", Instance);

    hr = IActivationFactory_QueryInterface(Factory, &IID_IPenDeviceStatics, (void **)&Statics);
    ok(hr == S_OK, "IPenDeviceStatics query failed, hr %#lx\n", hr);
    if (SUCCEEDED(hr))
    {
        Device = (IPenDevice *)(ULONG_PTR)0xdeadbeef;
        hr = IPenDeviceStatics_GetFromPointerId(Statics, 1, &Device);
        ok(hr == S_OK, "GetFromPointerId(1) returned hr %#lx\n", hr);
        ok(Device == NULL, "GetFromPointerId(1) returned device %p\n", Device);
        if (SUCCEEDED(hr) && Device)
            IPenDevice_Release(Device);

        Device = (IPenDevice *)(ULONG_PTR)0xdeadbeef;
        hr = IPenDeviceStatics_GetFromPointerId(Statics, 0x7fffffff, &Device);
        ok(hr == S_OK, "GetFromPointerId(0x7fffffff) returned hr %#lx\n", hr);
        ok(Device == NULL, "GetFromPointerId(0x7fffffff) returned device %p\n", Device);
        if (SUCCEEDED(hr) && Device)
            IPenDevice_Release(Device);


        IPenDeviceStatics_Release(Statics);
    }

    IActivationFactory_Release(Factory);
    RoUninitialize();
}
