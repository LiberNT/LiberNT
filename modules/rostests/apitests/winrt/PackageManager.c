/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests Windows.Management.Deployment.PackageManager activation
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "winrt_apitest.h"

#define WIDL_using_Windows_Management_Deployment
#include <windows.management.deployment.h>

START_TEST(PackageManager)
{
    IActivationFactory *Factory;
    IInspectable *Instance;
    IPackageManager *Manager;
    IPackageManager2 *Manager2;
    IAgileObject *Agile;
    HRESULT hr;

    hr = RoInitialize(RO_INIT_MULTITHREADED);
    ok(SUCCEEDED(hr), "RoInitialize failed, hr %#lx\n", hr);

    TraceActivatableClass(RuntimeClass_Windows_Management_Deployment_PackageManager);
    hr = GetFactory(RuntimeClass_Windows_Management_Deployment_PackageManager, &IID_IActivationFactory, (void **)&Factory);
    ok(hr == S_OK, "RoGetActivationFactory failed, hr %#lx\n", hr);
    if (FAILED(hr))
    {
        RoUninitialize();
        return;
    }

    Instance = NULL;
    hr = IActivationFactory_ActivateInstance(Factory, &Instance);
    ok(hr == S_OK, "ActivateInstance returned hr %#lx\n", hr);
    ok(Instance != NULL, "ActivateInstance returned no instance\n");
    if (SUCCEEDED(hr) && Instance)
    {
        hr = IInspectable_QueryInterface(Instance, &IID_IPackageManager, (void **)&Manager);
        ok(hr == S_OK, "IPackageManager query failed, hr %#lx\n", hr);
        if (SUCCEEDED(hr))
            IPackageManager_Release(Manager);

        hr = IInspectable_QueryInterface(Instance, &IID_IPackageManager2, (void **)&Manager2);
        ok(hr == S_OK, "IPackageManager2 query failed, hr %#lx\n", hr);
        if (SUCCEEDED(hr))
            IPackageManager2_Release(Manager2);

        hr = IInspectable_QueryInterface(Instance, &IID_IAgileObject, (void **)&Agile);
        ok(hr == S_OK, "IAgileObject query failed, hr %#lx\n", hr);
        if (SUCCEEDED(hr))
            IAgileObject_Release(Agile);

        IInspectable_Release(Instance);
    }

    IActivationFactory_Release(Factory);
    RoUninitialize();
}
