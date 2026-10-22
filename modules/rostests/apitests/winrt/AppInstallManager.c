/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests Windows.ApplicationModel.Store.Preview.InstallControl.AppInstallManager
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "winrt_apitest.h"

#define WIDL_using_Windows_ApplicationModel_Store_Preview_InstallControl
#include <windows.applicationmodel.store.preview.installcontrol.h>

static const WCHAR UnknownProduct[] = L"9ZZZZZZZZZZZ";

static void
CheckInterface(
    _In_ IAppInstallManager *Manager,
    _In_ const char *Name,
    _In_ REFIID Iid)
{
    IUnknown *Unknown;
    HRESULT hr = IAppInstallManager_QueryInterface(Manager, Iid, (void **)&Unknown);

    ok(hr == S_OK, "%s query returned hr %#lx\n", Name, hr);
    if (SUCCEEDED(hr))
        IUnknown_Release(Unknown);
}

static void
CheckStoreOperation(
    _In_ const char *Name,
    _In_ HRESULT hr,
    _In_ IAsyncOperation_boolean *Operation)
{
    AsyncStatus Status;
    HRESULT ErrorCode;

    ok(hr == S_OK, "%s returned hr %#lx\n", Name, hr);
    if (FAILED(hr))
        return;
    Status = WaitAsync((IUnknown *)Operation, 20000, &ErrorCode);
    ok(Status == Error && FAILED(ErrorCode), "%s ended with status %d error %#lx\n", Name, Status, ErrorCode);
    trace("%s error %#lx\n", Name, ErrorCode);
    IAsyncOperation_boolean_Release(Operation);
}

static void
TestOptions(void)
{
    IAppInstallOptions *Options;
    IAppUpdateOptions *Update;
    IInspectable *Instance;
    HSTRING Class, Value, Text;
    boolean Flag = 2;
    HRESULT hr;

    TraceActivatableClass(RuntimeClass_Windows_ApplicationModel_Store_Preview_InstallControl_AppInstallOptions);
    Class = CreateHString(RuntimeClass_Windows_ApplicationModel_Store_Preview_InstallControl_AppInstallOptions);
    hr = RoActivateInstance(Class, &Instance);
    WindowsDeleteString(Class);
    ok(hr == S_OK, "AppInstallOptions activation failed, hr %#lx\n", hr);
    if (SUCCEEDED(hr))
    {
        hr = IInspectable_QueryInterface(Instance, &IID_IAppInstallOptions, (void **)&Options);
        ok(hr == S_OK, "IAppInstallOptions query failed, hr %#lx\n", hr);
        if (SUCCEEDED(hr))
        {
            hr = IAppInstallOptions_get_Repair(Options, &Flag);
            ok(hr == S_OK && !Flag, "default Repair returned hr %#lx value %d\n", hr, Flag);
            Text = CreateHString(L"apitest-catalog");
            hr = IAppInstallOptions_put_CatalogId(Options, Text);
            ok(hr == S_OK, "put_CatalogId returned hr %#lx\n", hr);
            WindowsDeleteString(Text);
            hr = IAppInstallOptions_get_CatalogId(Options, &Value);
            ok(hr == S_OK && !wcscmp(WindowsGetStringRawBuffer(Value, NULL), L"apitest-catalog"),
               "get_CatalogId returned hr %#lx '%ls'\n", hr, WindowsGetStringRawBuffer(Value, NULL));
            WindowsDeleteString(Value);
            IAppInstallOptions_Release(Options);
        }
        IInspectable_Release(Instance);
    }

    Class = CreateHString(RuntimeClass_Windows_ApplicationModel_Store_Preview_InstallControl_AppUpdateOptions);
    hr = RoActivateInstance(Class, &Instance);
    WindowsDeleteString(Class);
    ok(hr == S_OK, "AppUpdateOptions activation failed, hr %#lx\n", hr);
    if (SUCCEEDED(hr))
    {
        hr = IInspectable_QueryInterface(Instance, &IID_IAppUpdateOptions, (void **)&Update);
        ok(hr == S_OK, "IAppUpdateOptions query failed, hr %#lx\n", hr);
        if (SUCCEEDED(hr))
        {
            Flag = 2;
            hr = IAppUpdateOptions_get_AllowForcedAppRestart(Update, &Flag);
            ok(hr == S_OK && !Flag, "default AllowForcedAppRestart returned hr %#lx value %d\n", hr, Flag);
            IAppUpdateOptions_Release(Update);
        }
        IInspectable_Release(Instance);
    }
}

START_TEST(AppInstallManager)
{
    IAsyncOperation_IVectorView_AppInstallItem *ItemsOperation;
    IAsyncOperation_boolean *BooleanOperation;
    IVectorView_AppInstallItem *Items;
    IAppInstallManager3 *Manager3;
    IAppInstallManager *Manager;
    IInspectable *Instance;
    AutoUpdateSetting Setting;
    AsyncStatus Status;
    HSTRING Class, Product, Value, Publisher;
    HRESULT hr, ErrorCode;
    UINT32 Size;

    hr = RoInitialize(RO_INIT_MULTITHREADED);
    ok(SUCCEEDED(hr), "RoInitialize failed, hr %#lx\n", hr);

    TraceActivatableClass(RuntimeClass_Windows_ApplicationModel_Store_Preview_InstallControl_AppInstallManager);
    Class = CreateHString(RuntimeClass_Windows_ApplicationModel_Store_Preview_InstallControl_AppInstallManager);
    hr = RoActivateInstance(Class, &Instance);
    WindowsDeleteString(Class);
    ok(hr == S_OK, "AppInstallManager activation failed, hr %#lx\n", hr);
    if (FAILED(hr))
    {
        RoUninitialize();
        return;
    }
    hr = IInspectable_QueryInterface(Instance, &IID_IAppInstallManager, (void **)&Manager);
    ok(hr == S_OK, "IAppInstallManager query failed, hr %#lx\n", hr);
    IInspectable_Release(Instance);
    if (FAILED(hr))
    {
        RoUninitialize();
        return;
    }

    CheckInterface(Manager, "IAppInstallManager2", &IID_IAppInstallManager2);
    CheckInterface(Manager, "IAppInstallManager3", &IID_IAppInstallManager3);
    CheckInterface(Manager, "IAppInstallManager4", &IID_IAppInstallManager4);
    CheckInterface(Manager, "IAppInstallManager5", &IID_IAppInstallManager5);
    CheckInterface(Manager, "IAppInstallManager6", &IID_IAppInstallManager6);
    CheckInterface(Manager, "IAppInstallManager7", &IID_IAppInstallManager7);

    hr = IAppInstallManager_get_AppInstallItems(Manager, &Items);
    ok(hr == S_OK, "get_AppInstallItems failed, hr %#lx\n", hr);
    if (SUCCEEDED(hr))
    {
        Size = 0xdeadbeef;
        IVectorView_AppInstallItem_get_Size(Items, &Size);
        ok(Size == 0, "AppInstallItems has %u items\n", Size);
        IVectorView_AppInstallItem_Release(Items);
    }

    hr = IAppInstallManager_get_AutoUpdateSetting(Manager, &Setting);
    ok(hr == S_OK && (UINT)Setting <= AutoUpdateSetting_EnabledByPolicy,
       "get_AutoUpdateSetting returned hr %#lx value %d\n", hr, Setting);
    Value = NULL;
    hr = IAppInstallManager_get_AcquisitionIdentity(Manager, &Value);
    ok(hr == S_OK && !WindowsGetStringLen(Value), "get_AcquisitionIdentity returned hr %#lx value '%ls'\n", hr,
       WindowsGetStringRawBuffer(Value, NULL));
    WindowsDeleteString(Value);

    Product = CreateHString(UnknownProduct);
    hr = IAppInstallManager_Cancel(Manager, Product);
    ok(hr == E_BOUNDS, "Cancel(unknown) returned hr %#lx\n", hr);

    hr = IAppInstallManager_GetIsAppAllowedToInstallAsync(Manager, Product, &BooleanOperation);
    CheckStoreOperation("GetIsAppAllowedToInstallAsync", hr, BooleanOperation);
    hr = IAppInstallManager_GetIsApplicableAsync(Manager, Product, NULL, &BooleanOperation);
    CheckStoreOperation("GetIsApplicableAsync", hr, BooleanOperation);

    BooleanOperation = (IAsyncOperation_boolean *)(ULONG_PTR)0xdeadbeef;
    hr = IAppInstallManager_IsStoreBlockedByPolicyAsync(Manager, NULL, NULL, &BooleanOperation);
    ok(hr == E_INVALIDARG, "IsStoreBlockedByPolicyAsync(NULL) returned hr %#lx\n", hr);
    Value = CreateHString(L"Microsoft.WindowsStore");
    Publisher = CreateHString(L"CN=Microsoft Corporation, O=Microsoft Corporation, L=Redmond, S=Washington, C=US");
    hr = IAppInstallManager_IsStoreBlockedByPolicyAsync(Manager, Value, Publisher, &BooleanOperation);
    WindowsDeleteString(Value);
    WindowsDeleteString(Publisher);
    ok(hr == S_OK, "IsStoreBlockedByPolicyAsync returned hr %#lx\n", hr);
    if (SUCCEEDED(hr))
    {
        boolean Blocked = 2;

        Status = WaitAsync((IUnknown *)BooleanOperation, 20000, &ErrorCode);
        if (Status == Completed)
            IAsyncOperation_boolean_GetResults(BooleanOperation, &Blocked);
        trace("IsStoreBlockedByPolicyAsync status %d error %#lx value %d\n", Status, ErrorCode, Blocked);
        IAsyncOperation_boolean_Release(BooleanOperation);
    }

    hr = IAppInstallManager_QueryInterface(Manager, &IID_IAppInstallManager3, (void **)&Manager3);
    if (SUCCEEDED(hr))
    {
        hr = IAppInstallManager3_StartProductInstallAsync(Manager3, Product, NULL, NULL, NULL, FALSE, FALSE, NULL, NULL,
                                                          &ItemsOperation);
        ok(hr == S_OK, "StartProductInstallAsync returned hr %#lx\n", hr);
        if (SUCCEEDED(hr))
        {
            Status = WaitAsync((IUnknown *)ItemsOperation, 20000, &ErrorCode);
            Size = 0xdeadbeef;
            if (Status == Completed &&
                SUCCEEDED(IAsyncOperation_IVectorView_AppInstallItem_GetResults(ItemsOperation, &Items)))
            {
                IVectorView_AppInstallItem_get_Size(Items, &Size);
                IVectorView_AppInstallItem_Release(Items);
            }
            ok(Status == Error && FAILED(ErrorCode), "StartProductInstallAsync ended with status %d error %#lx items %u\n",
               Status, ErrorCode, Size);
            IAsyncOperation_IVectorView_AppInstallItem_Release(ItemsOperation);
        }
        IAppInstallManager3_Release(Manager3);
    }

    WindowsDeleteString(Product);
    IAppInstallManager_Release(Manager);
    TestOptions();
    RoUninitialize();
}
