/*
 * PROJECT:     LiberNT Windows Runtime
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     CoreMessaging entry points and DispatcherQueue activation factories
 * COPYRIGHT:   Copyright 2026 Ostap Brehin <osbre@protonmail.com>
 */

#include "initguid.h"
#include "private.h"

WINE_DEFAULT_DEBUG_CHANNEL(coremessaging);

struct statics_factory
{
    IActivationFactory IActivationFactory_iface;
    union
    {
        IDispatcherQueueStatics IDispatcherQueueStatics_iface;
        IDispatcherQueueControllerStatics IDispatcherQueueControllerStatics_iface;
    };
    const IID *statics_iid;
    const WCHAR *class_name;
};

static inline struct statics_factory *impl_from_IActivationFactory(IActivationFactory *iface)
{
    return CONTAINING_RECORD(iface, struct statics_factory, IActivationFactory_iface);
}

static HRESULT WINAPI factory_QueryInterface(IActivationFactory *iface, REFIID iid, void **out)
{
    struct statics_factory *impl = impl_from_IActivationFactory(iface);

    TRACE("iface %p, iid %s, out %p.\n", iface, debugstr_guid(iid), out);

    if (!out) return E_POINTER;
    if (IsEqualGUID(iid, &IID_IUnknown) || IsEqualGUID(iid, &IID_IInspectable) ||
        IsEqualGUID(iid, &IID_IAgileObject) || IsEqualGUID(iid, &IID_IActivationFactory))
        *out = &impl->IActivationFactory_iface;
    else if (IsEqualGUID(iid, impl->statics_iid))
        *out = &impl->IDispatcherQueueStatics_iface;
    else
    {
        *out = NULL;
        WARN("%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid(iid));
        return E_NOINTERFACE;
    }
    IInspectable_AddRef((IInspectable *)*out);
    return S_OK;
}

static ULONG WINAPI factory_AddRef(IActivationFactory *iface)
{
    return 2;
}

static ULONG WINAPI factory_Release(IActivationFactory *iface)
{
    return 1;
}

static HRESULT WINAPI factory_GetIids(IActivationFactory *iface, ULONG *count, IID **iids)
{
    struct statics_factory *impl = impl_from_IActivationFactory(iface);

    if (!count || !iids) return E_POINTER;
    if (!(*iids = CoTaskMemAlloc(sizeof(**iids)))) return E_OUTOFMEMORY;
    **iids = *impl->statics_iid;
    *count = 1;
    return S_OK;
}

static HRESULT WINAPI factory_GetRuntimeClassName(IActivationFactory *iface, HSTRING *class_name)
{
    struct statics_factory *impl = impl_from_IActivationFactory(iface);

    if (!class_name) return E_POINTER;
    return WindowsCreateString(impl->class_name, wcslen(impl->class_name), class_name);
}

static HRESULT WINAPI factory_GetTrustLevel(IActivationFactory *iface, TrustLevel *trust_level)
{
    if (!trust_level) return E_POINTER;
    *trust_level = BaseTrust;
    return S_OK;
}

static HRESULT WINAPI factory_ActivateInstance(IActivationFactory *iface, IInspectable **instance)
{
    if (!instance) return E_POINTER;
    *instance = NULL;
    return E_NOTIMPL;
}

static const IActivationFactoryVtbl factory_vtbl =
{
    factory_QueryInterface,
    factory_AddRef,
    factory_Release,
    factory_GetIids,
    factory_GetRuntimeClassName,
    factory_GetTrustLevel,
    factory_ActivateInstance,
};

static inline struct statics_factory *impl_from_IDispatcherQueueStatics(IDispatcherQueueStatics *iface)
{
    return CONTAINING_RECORD(iface, struct statics_factory, IDispatcherQueueStatics_iface);
}

static HRESULT WINAPI queue_statics_QueryInterface(IDispatcherQueueStatics *iface, REFIID iid, void **out)
{
    return IActivationFactory_QueryInterface(&impl_from_IDispatcherQueueStatics(iface)->IActivationFactory_iface,
                                             iid, out);
}

static ULONG WINAPI queue_statics_AddRef(IDispatcherQueueStatics *iface)
{
    return 2;
}

static ULONG WINAPI queue_statics_Release(IDispatcherQueueStatics *iface)
{
    return 1;
}

static HRESULT WINAPI queue_statics_GetIids(IDispatcherQueueStatics *iface, ULONG *count, IID **iids)
{
    return factory_GetIids(&impl_from_IDispatcherQueueStatics(iface)->IActivationFactory_iface, count, iids);
}

static HRESULT WINAPI queue_statics_GetRuntimeClassName(IDispatcherQueueStatics *iface, HSTRING *class_name)
{
    return factory_GetRuntimeClassName(&impl_from_IDispatcherQueueStatics(iface)->IActivationFactory_iface,
                                       class_name);
}

static HRESULT WINAPI queue_statics_GetTrustLevel(IDispatcherQueueStatics *iface, TrustLevel *trust_level)
{
    return factory_GetTrustLevel(&impl_from_IDispatcherQueueStatics(iface)->IActivationFactory_iface,
                                 trust_level);
}

static HRESULT WINAPI queue_statics_GetForCurrentThread(IDispatcherQueueStatics *iface, IDispatcherQueue **result)
{
    TRACE("iface %p, result %p.\n", iface, result);

    if (!result) return E_POINTER;
    return dispatcher_queue_get_for_current_thread(result);
}

static const IDispatcherQueueStaticsVtbl queue_statics_vtbl =
{
    queue_statics_QueryInterface,
    queue_statics_AddRef,
    queue_statics_Release,
    queue_statics_GetIids,
    queue_statics_GetRuntimeClassName,
    queue_statics_GetTrustLevel,
    queue_statics_GetForCurrentThread,
};

static inline struct statics_factory *impl_from_IDispatcherQueueControllerStatics(
    IDispatcherQueueControllerStatics *iface)
{
    return CONTAINING_RECORD(iface, struct statics_factory, IDispatcherQueueControllerStatics_iface);
}

static HRESULT WINAPI controller_statics_QueryInterface(IDispatcherQueueControllerStatics *iface, REFIID iid,
                                                        void **out)
{
    return IActivationFactory_QueryInterface(
        &impl_from_IDispatcherQueueControllerStatics(iface)->IActivationFactory_iface, iid, out);
}

static ULONG WINAPI controller_statics_AddRef(IDispatcherQueueControllerStatics *iface)
{
    return 2;
}

static ULONG WINAPI controller_statics_Release(IDispatcherQueueControllerStatics *iface)
{
    return 1;
}

static HRESULT WINAPI controller_statics_GetIids(IDispatcherQueueControllerStatics *iface, ULONG *count,
                                                 IID **iids)
{
    return factory_GetIids(&impl_from_IDispatcherQueueControllerStatics(iface)->IActivationFactory_iface, count,
                           iids);
}

static HRESULT WINAPI controller_statics_GetRuntimeClassName(IDispatcherQueueControllerStatics *iface,
                                                             HSTRING *class_name)
{
    return factory_GetRuntimeClassName(&impl_from_IDispatcherQueueControllerStatics(iface)->IActivationFactory_iface,
                                       class_name);
}

static HRESULT WINAPI controller_statics_GetTrustLevel(IDispatcherQueueControllerStatics *iface,
                                                       TrustLevel *trust_level)
{
    return factory_GetTrustLevel(&impl_from_IDispatcherQueueControllerStatics(iface)->IActivationFactory_iface,
                                 trust_level);
}

static HRESULT WINAPI controller_statics_CreateOnDedicatedThread(IDispatcherQueueControllerStatics *iface,
                                                                 IDispatcherQueueController **result)
{
    TRACE("iface %p, result %p.\n", iface, result);

    if (!result) return E_POINTER;
    return dispatcher_queue_controller_create(DQTYPE_THREAD_DEDICATED, DQTAT_COM_STA, result);
}

static const IDispatcherQueueControllerStaticsVtbl controller_statics_vtbl =
{
    controller_statics_QueryInterface,
    controller_statics_AddRef,
    controller_statics_Release,
    controller_statics_GetIids,
    controller_statics_GetRuntimeClassName,
    controller_statics_GetTrustLevel,
    controller_statics_CreateOnDedicatedThread,
};

static struct statics_factory queue_statics =
{
    {&factory_vtbl},
    {.IDispatcherQueueStatics_iface = {&queue_statics_vtbl}},
    &IID_IDispatcherQueueStatics,
    RuntimeClass_Windows_System_DispatcherQueue,
};

static struct statics_factory controller_statics =
{
    {&factory_vtbl},
    {.IDispatcherQueueControllerStatics_iface = {&controller_statics_vtbl}},
    &IID_IDispatcherQueueControllerStatics,
    RuntimeClass_Windows_System_DispatcherQueueController,
};

IActivationFactory *dispatcher_queue_factory = &queue_statics.IActivationFactory_iface;
IActivationFactory *dispatcher_queue_controller_factory = &controller_statics.IActivationFactory_iface;

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, void *reserved)
{
    switch (reason)
    {
    case DLL_PROCESS_ATTACH:
        DisableThreadLibraryCalls(instance);
        return dispatcher_queue_process_attach(instance);
    case DLL_PROCESS_DETACH:
        if (!reserved) dispatcher_queue_process_detach();
        break;
    }
    return TRUE;
}

HRESULT WINAPI CreateDispatcherQueueController(DispatcherQueueOptions options,
                                               PDISPATCHERQUEUECONTROLLER *queue_controller)
{
    TRACE("size %lu, type %u, apartment %u, queue_controller %p.\n", options.dwSize, options.threadType,
          options.apartmentType, queue_controller);

    if (!queue_controller) return E_POINTER;
    if (options.dwSize != sizeof(options)) return E_INVALIDARG;

    switch (options.threadType)
    {
    case DQTYPE_THREAD_CURRENT:
        break;
    case DQTYPE_THREAD_DEDICATED:
        if (options.apartmentType != DQTAT_COM_STA && options.apartmentType != DQTAT_COM_ASTA)
            return E_INVALIDARG;
        break;
    default:
        return E_INVALIDARG;
    }
    return dispatcher_queue_controller_create(options.threadType, options.apartmentType, queue_controller);
}

HRESULT WINAPI DllGetClassObject(REFCLSID clsid, REFIID riid, void **out)
{
    FIXME("clsid %s, riid %s, out %p stub!\n", debugstr_guid(clsid), debugstr_guid(riid), out);
    return CLASS_E_CLASSNOTAVAILABLE;
}

HRESULT WINAPI DllGetActivationFactory(HSTRING classid, IActivationFactory **factory)
{
    const WCHAR *name = WindowsGetStringRawBuffer(classid, NULL);

    TRACE("classid %s, factory %p.\n", debugstr_hstring(classid), factory);

    if (!factory) return E_POINTER;
    *factory = NULL;

    if (!wcscmp(name, RuntimeClass_Windows_System_DispatcherQueue))
        IActivationFactory_QueryInterface(dispatcher_queue_factory, &IID_IActivationFactory, (void **)factory);
    else if (!wcscmp(name, RuntimeClass_Windows_System_DispatcherQueueController))
        IActivationFactory_QueryInterface(dispatcher_queue_controller_factory, &IID_IActivationFactory,
                                          (void **)factory);

    if (*factory) return S_OK;
    return CLASS_E_CLASSNOTAVAILABLE;
}
