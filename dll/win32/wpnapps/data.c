/*
 * PROJECT:     LiberNT Windows Runtime
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Windows.UI.Notifications.NotificationData
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "private.h"

struct notification_data
{
    INotificationData INotificationData_iface;
    LONG ref;
    IMap_HSTRING_HSTRING *values;
    LONG sequence;
};

static inline struct notification_data *impl_from_INotificationData(INotificationData *iface)
{
    return CONTAINING_RECORD(iface, struct notification_data, INotificationData_iface);
}

static HRESULT WINAPI data_QueryInterface(INotificationData *iface, REFIID iid, void **out)
{
    if (!out)
        return E_POINTER;
    if (IsEqualGUID(iid, &IID_IUnknown) || IsEqualGUID(iid, &IID_IInspectable) ||
        IsEqualGUID(iid, &IID_IAgileObject) || IsEqualGUID(iid, &IID_INotificationData))
    {
        *out = iface;
        INotificationData_AddRef(iface);
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI data_AddRef(INotificationData *iface)
{
    return InterlockedIncrement(&impl_from_INotificationData(iface)->ref);
}

static ULONG WINAPI data_Release(INotificationData *iface)
{
    struct notification_data *impl = impl_from_INotificationData(iface);
    ULONG ref = InterlockedDecrement(&impl->ref);

    if (!ref)
    {
        IMap_HSTRING_HSTRING_Release(impl->values);
        free(impl);
    }
    return ref;
}

static HRESULT WINAPI data_GetIids(INotificationData *iface, ULONG *count, IID **iids)
{
    return get_iids(&IID_INotificationData, count, iids);
}

static HRESULT WINAPI data_GetRuntimeClassName(INotificationData *iface, HSTRING *class_name)
{
    if (!class_name)
        return E_POINTER;
    return WindowsCreateString(RuntimeClass_Windows_UI_Notifications_NotificationData,
                               wcslen(RuntimeClass_Windows_UI_Notifications_NotificationData), class_name);
}

static HRESULT WINAPI data_GetTrustLevel(INotificationData *iface, TrustLevel *trust_level)
{
    if (!trust_level)
        return E_POINTER;
    *trust_level = BaseTrust;
    return S_OK;
}

static HRESULT WINAPI data_get_Values(INotificationData *iface, IMap_HSTRING_HSTRING **value)
{
    struct notification_data *impl = impl_from_INotificationData(iface);

    if (!value)
        return E_POINTER;
    *value = impl->values;
    IMap_HSTRING_HSTRING_AddRef(*value);
    return S_OK;
}

static HRESULT WINAPI data_get_SequenceNumber(INotificationData *iface, UINT32 *value)
{
    if (!value)
        return E_POINTER;
    *value = (UINT32)InterlockedCompareExchange(&impl_from_INotificationData(iface)->sequence, 0, 0);
    return S_OK;
}

static HRESULT WINAPI data_put_SequenceNumber(INotificationData *iface, UINT32 value)
{
    InterlockedExchange(&impl_from_INotificationData(iface)->sequence, (LONG)value);
    return S_OK;
}

static const INotificationDataVtbl data_vtbl =
{
    data_QueryInterface,
    data_AddRef,
    data_Release,
    data_GetIids,
    data_GetRuntimeClassName,
    data_GetTrustLevel,
    data_get_Values,
    data_get_SequenceNumber,
    data_put_SequenceNumber,
};

static HRESULT copy_values(IIterable_IKeyValuePair_HSTRING_HSTRING *source, IMap_HSTRING_HSTRING *target)
{
    IIterator_IKeyValuePair_HSTRING_HSTRING *iterator;
    IKeyValuePair_HSTRING_HSTRING *pair;
    boolean has_current = FALSE, replaced;
    HSTRING key, value;
    HRESULT hr;

    if (FAILED(hr = IIterable_IKeyValuePair_HSTRING_HSTRING_First(source, &iterator)))
        return hr;
    hr = IIterator_IKeyValuePair_HSTRING_HSTRING_get_HasCurrent(iterator, &has_current);
    while (SUCCEEDED(hr) && has_current)
    {
        if (FAILED(hr = IIterator_IKeyValuePair_HSTRING_HSTRING_get_Current(iterator, &pair)))
            break;
        key = value = NULL;
        if (SUCCEEDED(hr = IKeyValuePair_HSTRING_HSTRING_get_Key(pair, &key)) &&
            SUCCEEDED(hr = IKeyValuePair_HSTRING_HSTRING_get_Value(pair, &value)))
            hr = IMap_HSTRING_HSTRING_Insert(target, key, value, &replaced);
        WindowsDeleteString(key);
        WindowsDeleteString(value);
        IKeyValuePair_HSTRING_HSTRING_Release(pair);
        if (SUCCEEDED(hr))
            hr = IIterator_IKeyValuePair_HSTRING_HSTRING_MoveNext(iterator, &has_current);
    }
    IIterator_IKeyValuePair_HSTRING_HSTRING_Release(iterator);
    return hr;
}

HRESULT notification_data_create(IIterable_IKeyValuePair_HSTRING_HSTRING *values, UINT32 sequence,
                                 INotificationData **out)
{
    struct notification_data *impl;
    HRESULT hr;

    if (!out)
        return E_POINTER;
    *out = NULL;
    if (!(impl = calloc(1, sizeof(*impl))))
        return E_OUTOFMEMORY;
    if (FAILED(hr = string_map_create(&impl->values)))
    {
        free(impl);
        return hr;
    }
    if (values && FAILED(hr = copy_values(values, impl->values)))
    {
        IMap_HSTRING_HSTRING_Release(impl->values);
        free(impl);
        return hr;
    }
    impl->INotificationData_iface.lpVtbl = (INotificationDataVtbl *)&data_vtbl;
    impl->ref = 1;
    impl->sequence = (LONG)sequence;
    *out = &impl->INotificationData_iface;
    return S_OK;
}

struct data_factory
{
    IActivationFactory IActivationFactory_iface;
    INotificationDataFactory INotificationDataFactory_iface;
};

static inline struct data_factory *impl_from_IActivationFactory(IActivationFactory *iface)
{
    return CONTAINING_RECORD(iface, struct data_factory, IActivationFactory_iface);
}

static HRESULT WINAPI factory_QueryInterface(IActivationFactory *iface, REFIID iid, void **out)
{
    struct data_factory *impl = impl_from_IActivationFactory(iface);

    if (!out)
        return E_POINTER;
    if (IsEqualGUID(iid, &IID_IUnknown) || IsEqualGUID(iid, &IID_IInspectable) ||
        IsEqualGUID(iid, &IID_IAgileObject) || IsEqualGUID(iid, &IID_IActivationFactory))
        *out = &impl->IActivationFactory_iface;
    else if (IsEqualGUID(iid, &IID_INotificationDataFactory))
        *out = &impl->INotificationDataFactory_iface;
    else
    {
        *out = NULL;
        return E_NOINTERFACE;
    }
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
    return get_iids(&IID_IActivationFactory, count, iids);
}

static HRESULT WINAPI factory_GetRuntimeClassName(IActivationFactory *iface, HSTRING *class_name)
{
    return data_GetRuntimeClassName(NULL, class_name);
}

static HRESULT WINAPI factory_GetTrustLevel(IActivationFactory *iface, TrustLevel *trust_level)
{
    return data_GetTrustLevel(NULL, trust_level);
}

static HRESULT WINAPI factory_ActivateInstance(IActivationFactory *iface, IInspectable **instance)
{
    if (!instance)
        return E_POINTER;
    return notification_data_create(NULL, 0, (INotificationData **)instance);
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

static inline struct data_factory *impl_from_INotificationDataFactory(INotificationDataFactory *iface)
{
    return CONTAINING_RECORD(iface, struct data_factory, INotificationDataFactory_iface);
}

static HRESULT WINAPI data_factory_QueryInterface(INotificationDataFactory *iface, REFIID iid, void **out)
{
    return factory_QueryInterface(&impl_from_INotificationDataFactory(iface)->IActivationFactory_iface, iid, out);
}

static ULONG WINAPI data_factory_AddRef(INotificationDataFactory *iface)
{
    return 2;
}

static ULONG WINAPI data_factory_Release(INotificationDataFactory *iface)
{
    return 1;
}

static HRESULT WINAPI data_factory_GetIids(INotificationDataFactory *iface, ULONG *count, IID **iids)
{
    return get_iids(&IID_INotificationDataFactory, count, iids);
}

static HRESULT WINAPI data_factory_GetRuntimeClassName(INotificationDataFactory *iface, HSTRING *class_name)
{
    return data_GetRuntimeClassName(NULL, class_name);
}

static HRESULT WINAPI data_factory_GetTrustLevel(INotificationDataFactory *iface, TrustLevel *trust_level)
{
    return data_GetTrustLevel(NULL, trust_level);
}

static HRESULT WINAPI data_factory_CreateNotificationDataWithValuesAndSequenceNumber(
    INotificationDataFactory *iface, IIterable_IKeyValuePair_HSTRING_HSTRING *values, UINT32 sequence,
    INotificationData **out)
{
    return notification_data_create(values, sequence, out);
}

static HRESULT WINAPI data_factory_CreateNotificationDataWithValues(INotificationDataFactory *iface,
                                                                    IIterable_IKeyValuePair_HSTRING_HSTRING *values,
                                                                    INotificationData **out)
{
    return notification_data_create(values, 0, out);
}

static const INotificationDataFactoryVtbl data_factory_vtbl =
{
    data_factory_QueryInterface,
    data_factory_AddRef,
    data_factory_Release,
    data_factory_GetIids,
    data_factory_GetRuntimeClassName,
    data_factory_GetTrustLevel,
    data_factory_CreateNotificationDataWithValuesAndSequenceNumber,
    data_factory_CreateNotificationDataWithValues,
};

static struct data_factory data_statics =
{
    {(IActivationFactoryVtbl *)&factory_vtbl},
    {(INotificationDataFactoryVtbl *)&data_factory_vtbl},
};

IActivationFactory *notification_data_factory = &data_statics.IActivationFactory_iface;
