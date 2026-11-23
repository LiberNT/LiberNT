/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests Windows.ApplicationModel.Core.CoreApplication properties
 * COPYRIGHT:   Copyright 2026 Ostap Brehin <osbre@protonmail.com>
 */

#include "winrt_apitest.h"

#define WIDL_using_Windows_ApplicationModel_Core
#include <windows.applicationmodel.core.h>

static IMap_HSTRING_IInspectable *
GetProperties(ICoreApplication *Core)
{
    IMap_HSTRING_IInspectable *Map = NULL;
    IPropertySet *Properties = NULL;
    HRESULT hr;

    hr = ICoreApplication_get_Properties(Core, &Properties);
    ok(hr == S_OK, "Properties returned %#lx\n", hr);
    if (FAILED(hr))
        return NULL;
    ok(Properties != NULL, "Properties is NULL\n");
    hr = IPropertySet_QueryInterface(Properties, &IID_IMap_HSTRING_IInspectable, (void **)&Map);
    ok(hr == S_OK, "IMap returned %#lx\n", hr);
    IPropertySet_Release(Properties);
    return Map;
}

START_TEST(CoreApplication)
{
    ICoreApplication *Core = NULL;
    IMap_HSTRING_IInspectable *First = NULL, *Second = NULL;
    IInspectable *Found = NULL;
    HSTRING Key = CreateHString(L"LiberNT.CoreApplication.Test");
    boolean Replaced = TRUE, Has = FALSE;
    HRESULT hr;

    hr = RoInitialize(RO_INIT_MULTITHREADED);
    ok(hr == S_OK, "RoInitialize returned %#lx\n", hr);
    TraceActivatableClass(L"Windows.ApplicationModel.Core.CoreApplication");

    hr = GetFactory(L"Windows.ApplicationModel.Core.CoreApplication", &IID_ICoreApplication, (void **)&Core);
    ok(hr == S_OK, "RoGetActivationFactory returned %#lx\n", hr);
    if (FAILED(hr))
        goto done;

    hr = ICoreApplication_get_Properties(Core, NULL);
    ok(hr == E_POINTER, "Properties(NULL) returned %#lx\n", hr);

    First = GetProperties(Core);
    Second = GetProperties(Core);
    if (!First || !Second)
        goto done;

    hr = IMap_HSTRING_IInspectable_Insert(First, Key, (IInspectable *)Core, &Replaced);
    ok(hr == S_OK, "Insert returned %#lx\n", hr);
    ok(!Replaced, "Insert replaced an existing value\n");

    hr = IMap_HSTRING_IInspectable_HasKey(Second, Key, &Has);
    ok(hr == S_OK, "HasKey returned %#lx\n", hr);
    ok(Has, "a second Properties call does not see the inserted key\n");

    hr = IMap_HSTRING_IInspectable_Lookup(Second, Key, &Found);
    ok(hr == S_OK, "Lookup returned %#lx\n", hr);
    ok(Found == (IInspectable *)Core, "Lookup returned %p, expected %p\n", Found, Core);
    if (Found) IInspectable_Release(Found);

    hr = IMap_HSTRING_IInspectable_Remove(Second, Key);
    ok(hr == S_OK, "Remove returned %#lx\n", hr);
    Has = TRUE;
    hr = IMap_HSTRING_IInspectable_HasKey(First, Key, &Has);
    ok(hr == S_OK, "HasKey after Remove returned %#lx\n", hr);
    ok(!Has, "the removed key is still present\n");

done:
    WindowsDeleteString(Key);
    if (Second) IMap_HSTRING_IInspectable_Release(Second);
    if (First) IMap_HSTRING_IInspectable_Release(First);
    if (Core) ICoreApplication_Release(Core);
    RoUninitialize();
}
