/*
 * PROJECT:     LiberNT Windows Runtime
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     CoreApplication activation and the process property set
 * COPYRIGHT:   Copyright 2026 Ostap Brehin <osbre@protonmail.com>
 */

#include <assert.h>
#include "private.h"
#include "roapi.h"
#include "wine/debug.h"

WINE_DEFAULT_DEBUG_CHANNEL(model);

struct core_application
{
    IActivationFactory IActivationFactory_iface;
    ICoreApplication ICoreApplication_iface;
    LONG ref;
};

static INIT_ONCE properties_once = INIT_ONCE_STATIC_INIT;
static IPropertySet *properties;

static struct core_application *impl_from_factory(IActivationFactory *iface)
{
    return CONTAINING_RECORD(iface, struct core_application, IActivationFactory_iface);
}

static HRESULT WINAPI factory_QueryInterface(IActivationFactory *iface, REFIID iid, void **out)
{
    struct core_application *impl = impl_from_factory(iface);
    if (!out) return E_POINTER;
    *out = NULL;
    if (IsEqualGUID(iid, &IID_IUnknown) || IsEqualGUID(iid, &IID_IInspectable) ||
        IsEqualGUID(iid, &IID_IActivationFactory) || IsEqualGUID(iid, &IID_IAgileObject))
        *out = &impl->IActivationFactory_iface;
    else if (IsEqualGUID(iid, &IID_ICoreApplication))
        *out = &impl->ICoreApplication_iface;
    else
        return E_NOINTERFACE;
    IUnknown_AddRef((IUnknown *)*out);
    return S_OK;
}

static ULONG WINAPI factory_AddRef(IActivationFactory *iface)
{
    return InterlockedIncrement(&impl_from_factory(iface)->ref);
}

static ULONG WINAPI factory_Release(IActivationFactory *iface)
{
    ULONG ref = InterlockedDecrement(&impl_from_factory(iface)->ref);
    assert(ref != 0);
    return ref;
}

static HRESULT WINAPI factory_GetIids(IActivationFactory *iface, ULONG *count, IID **iids)
{
    if (!count || !iids) return E_POINTER;
    *count = 0;
    *iids = CoTaskMemAlloc(sizeof(**iids));
    if (!*iids) return E_OUTOFMEMORY;
    **iids = IID_ICoreApplication;
    *count = 1;
    return S_OK;
}

static HRESULT WINAPI factory_GetRuntimeClassName(IActivationFactory *iface, HSTRING *name)
{
    const WCHAR *class_name = RuntimeClass_Windows_ApplicationModel_Core_CoreApplication;
    if (!name) return E_POINTER;
    return WindowsCreateString(class_name, wcslen(class_name), name);
}

static HRESULT WINAPI factory_GetTrustLevel(IActivationFactory *iface, TrustLevel *level)
{
    if (!level) return E_POINTER;
    *level = BaseTrust;
    return S_OK;
}

static HRESULT WINAPI factory_ActivateInstance(IActivationFactory *iface, IInspectable **out)
{
    if (!out) return E_POINTER;
    *out = NULL;
    return E_NOTIMPL;
}

static const IActivationFactoryVtbl factory_vtbl =
{
    factory_QueryInterface, factory_AddRef, factory_Release,
    factory_GetIids, factory_GetRuntimeClassName, factory_GetTrustLevel,
    factory_ActivateInstance
};

DEFINE_IINSPECTABLE_(core, ICoreApplication, struct core_application,
                     impl_from_core, ICoreApplication_iface, &impl->IActivationFactory_iface)

static HRESULT WINAPI core_get_Id(ICoreApplication *iface, HSTRING *value)
{
    if (!value) return E_POINTER;
    *value = NULL;
    /* TODO: Id needs the application identity of a packaged process */
    FIXME("iface %p, value %p stub!\n", iface, value);
    return E_NOTIMPL;
}

static HRESULT WINAPI core_add_Suspending(ICoreApplication *iface, __FIEventHandler_1_Windows__CApplicationModel__CSuspendingEventArgs *handler,
                                          EventRegistrationToken *token)
{
    /* TODO: Suspending needs the process lifetime manager */
    FIXME("iface %p, handler %p, token %p stub!\n", iface, handler, token);
    return E_NOTIMPL;
}

static HRESULT WINAPI core_remove_Suspending(ICoreApplication *iface, EventRegistrationToken token)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT WINAPI core_add_Resuming(ICoreApplication *iface, __FIEventHandler_1_IInspectable *handler,
                                        EventRegistrationToken *token)
{
    /* TODO: Resuming needs the process lifetime manager */
    FIXME("iface %p, handler %p, token %p stub!\n", iface, handler, token);
    return E_NOTIMPL;
}

static HRESULT WINAPI core_remove_Resuming(ICoreApplication *iface, EventRegistrationToken token)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static BOOL WINAPI create_properties(INIT_ONCE *once, void *param, void **context)
{
    static const WCHAR name[] = L"Windows.Foundation.Collections.PropertySet";
    IInspectable *inspectable = NULL;
    HSTRING class_name;
    HRESULT hr;

    hr = WindowsCreateString(name, ARRAY_SIZE(name) - 1, &class_name);
    if (SUCCEEDED(hr))
    {
        hr = RoActivateInstance(class_name, &inspectable);
        WindowsDeleteString(class_name);
    }
    if (SUCCEEDED(hr))
    {
        hr = IInspectable_QueryInterface(inspectable, &IID_IPropertySet, (void **)&properties);
        IInspectable_Release(inspectable);
    }
    *(HRESULT *)param = hr;
    assert(SUCCEEDED(hr) == (properties != NULL));
    return SUCCEEDED(hr);
}

static HRESULT WINAPI core_get_Properties(ICoreApplication *iface, IPropertySet **value)
{
    HRESULT hr = S_OK;

    if (!value) return E_POINTER;
    *value = NULL;
    if (!InitOnceExecuteOnce(&properties_once, create_properties, &hr, NULL))
        return FAILED(hr) ? hr : E_FAIL;
    assert(properties);
    IPropertySet_AddRef(properties);
    *value = properties;
    return S_OK;
}

static HRESULT WINAPI core_GetCurrentView(ICoreApplication *iface, ICoreApplicationView **value)
{
    if (!value) return E_POINTER;
    *value = NULL;
    /* TODO: GetCurrentView needs CoreApplicationView */
    FIXME("iface %p, value %p stub!\n", iface, value);
    return E_NOTIMPL;
}

static HRESULT WINAPI core_Run(ICoreApplication *iface, IFrameworkViewSource *source)
{
    /* TODO: Run needs CoreApplicationView and a CoreWindow dispatcher */
    FIXME("iface %p, source %p stub!\n", iface, source);
    return E_NOTIMPL;
}

static HRESULT WINAPI core_RunWithActivationFactories(ICoreApplication *iface, IGetActivationFactory *factory)
{
    /* TODO: RunWithActivationFactories needs CoreApplicationView and a CoreWindow dispatcher */
    FIXME("iface %p, factory %p stub!\n", iface, factory);
    return E_NOTIMPL;
}

static const ICoreApplicationVtbl core_vtbl =
{
    core_QueryInterface, core_AddRef, core_Release,
    core_GetIids, core_GetRuntimeClassName, core_GetTrustLevel,
    core_get_Id, core_add_Suspending, core_remove_Suspending,
    core_add_Resuming, core_remove_Resuming, core_get_Properties,
    core_GetCurrentView, core_Run, core_RunWithActivationFactories
};

static struct core_application core_application = {{&factory_vtbl}, {&core_vtbl}, 1};
IActivationFactory *core_application_factory = &core_application.IActivationFactory_iface;
