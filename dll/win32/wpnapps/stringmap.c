/*
 * PROJECT:     LiberNT Windows Runtime
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     IMap<String, String> used by notification data values
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "private.h"

struct string_pair
{
    HSTRING key;
    HSTRING value;
};

struct snapshot
{
    LONG ref;
    UINT32 count;
    struct string_pair pairs[1];
};

struct string_map
{
    IMap_HSTRING_HSTRING IMap_HSTRING_HSTRING_iface;
    IIterable_IKeyValuePair_HSTRING_HSTRING IIterable_iface;
    LONG ref;
    SRWLOCK lock;
    UINT32 count;
    UINT32 capacity;
    struct string_pair *pairs;
};

struct string_view
{
    IMapView_HSTRING_HSTRING IMapView_HSTRING_HSTRING_iface;
    IIterable_IKeyValuePair_HSTRING_HSTRING IIterable_iface;
    LONG ref;
    struct snapshot *snapshot;
};

struct string_iterator
{
    IIterator_IKeyValuePair_HSTRING_HSTRING IIterator_iface;
    LONG ref;
    struct snapshot *snapshot;
    UINT32 index;
};

struct string_kv
{
    IKeyValuePair_HSTRING_HSTRING IKeyValuePair_iface;
    LONG ref;
    HSTRING key;
    HSTRING value;
};

static HRESULT class_name(const WCHAR *name, HSTRING *out)
{
    if (!out)
        return E_POINTER;
    return WindowsCreateString(name, wcslen(name), out);
}

static HRESULT trust_level(TrustLevel *out)
{
    if (!out)
        return E_POINTER;
    *out = BaseTrust;
    return S_OK;
}

static void snapshot_release(struct snapshot *snapshot)
{
    UINT32 i;

    if (InterlockedDecrement(&snapshot->ref))
        return;
    for (i = 0; i < snapshot->count; i++)
    {
        WindowsDeleteString(snapshot->pairs[i].key);
        WindowsDeleteString(snapshot->pairs[i].value);
    }
    free(snapshot);
}

static BOOL find_pair(const struct string_pair *pairs, UINT32 count, HSTRING key, UINT32 *index)
{
    UINT32 low = 0, high = count, middle;
    INT32 order;

    while (low < high)
    {
        middle = low + (high - low) / 2;
        WindowsCompareStringOrdinal(pairs[middle].key, key, &order);
        if (!order)
        {
            *index = middle;
            return TRUE;
        }
        if (order < 0)
            low = middle + 1;
        else
            high = middle;
    }
    *index = low;
    return FALSE;
}

static HRESULT kv_create(HSTRING key, HSTRING value, IKeyValuePair_HSTRING_HSTRING **out);

static inline struct string_kv *impl_from_IKeyValuePair(IKeyValuePair_HSTRING_HSTRING *iface)
{
    return CONTAINING_RECORD(iface, struct string_kv, IKeyValuePair_iface);
}

static HRESULT WINAPI kv_QueryInterface(IKeyValuePair_HSTRING_HSTRING *iface, REFIID iid, void **out)
{
    if (!out)
        return E_POINTER;
    if (IsEqualGUID(iid, &IID_IUnknown) || IsEqualGUID(iid, &IID_IInspectable) || IsEqualGUID(iid, &IID_IAgileObject) ||
        IsEqualGUID(iid, &IID_IKeyValuePair_HSTRING_HSTRING))
    {
        *out = iface;
        IKeyValuePair_HSTRING_HSTRING_AddRef(iface);
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI kv_AddRef(IKeyValuePair_HSTRING_HSTRING *iface)
{
    return InterlockedIncrement(&impl_from_IKeyValuePair(iface)->ref);
}

static ULONG WINAPI kv_Release(IKeyValuePair_HSTRING_HSTRING *iface)
{
    struct string_kv *impl = impl_from_IKeyValuePair(iface);
    ULONG ref = InterlockedDecrement(&impl->ref);

    if (!ref)
    {
        WindowsDeleteString(impl->key);
        WindowsDeleteString(impl->value);
        free(impl);
    }
    return ref;
}

static HRESULT WINAPI kv_GetIids(IKeyValuePair_HSTRING_HSTRING *iface, ULONG *count, IID **iids)
{
    return get_iids(&IID_IKeyValuePair_HSTRING_HSTRING, count, iids);
}

static HRESULT WINAPI kv_GetRuntimeClassName(IKeyValuePair_HSTRING_HSTRING *iface, HSTRING *name)
{
    return class_name(L"Windows.Foundation.Collections.IKeyValuePair`2<String, String>", name);
}

static HRESULT WINAPI kv_GetTrustLevel(IKeyValuePair_HSTRING_HSTRING *iface, TrustLevel *level)
{
    return trust_level(level);
}

static HRESULT WINAPI kv_get_Key(IKeyValuePair_HSTRING_HSTRING *iface, HSTRING *key)
{
    if (!key)
        return E_POINTER;
    return WindowsDuplicateString(impl_from_IKeyValuePair(iface)->key, key);
}

static HRESULT WINAPI kv_get_Value(IKeyValuePair_HSTRING_HSTRING *iface, HSTRING *value)
{
    if (!value)
        return E_POINTER;
    return WindowsDuplicateString(impl_from_IKeyValuePair(iface)->value, value);
}

static const IKeyValuePair_HSTRING_HSTRINGVtbl kv_vtbl =
{
    kv_QueryInterface,
    kv_AddRef,
    kv_Release,
    kv_GetIids,
    kv_GetRuntimeClassName,
    kv_GetTrustLevel,
    kv_get_Key,
    kv_get_Value,
};

static HRESULT kv_create(HSTRING key, HSTRING value, IKeyValuePair_HSTRING_HSTRING **out)
{
    struct string_kv *impl;

    if (!(impl = calloc(1, sizeof(*impl))))
        return E_OUTOFMEMORY;
    impl->IKeyValuePair_iface.lpVtbl = (IKeyValuePair_HSTRING_HSTRINGVtbl *)&kv_vtbl;
    impl->ref = 1;
    WindowsDuplicateString(key, &impl->key);
    WindowsDuplicateString(value, &impl->value);
    *out = &impl->IKeyValuePair_iface;
    return S_OK;
}

static inline struct string_iterator *impl_from_IIterator(IIterator_IKeyValuePair_HSTRING_HSTRING *iface)
{
    return CONTAINING_RECORD(iface, struct string_iterator, IIterator_iface);
}

static HRESULT WINAPI iterator_QueryInterface(IIterator_IKeyValuePair_HSTRING_HSTRING *iface, REFIID iid, void **out)
{
    if (!out)
        return E_POINTER;
    if (IsEqualGUID(iid, &IID_IUnknown) || IsEqualGUID(iid, &IID_IInspectable) || IsEqualGUID(iid, &IID_IAgileObject) ||
        IsEqualGUID(iid, &IID_IIterator_IKeyValuePair_HSTRING_HSTRING))
    {
        *out = iface;
        IIterator_IKeyValuePair_HSTRING_HSTRING_AddRef(iface);
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI iterator_AddRef(IIterator_IKeyValuePair_HSTRING_HSTRING *iface)
{
    return InterlockedIncrement(&impl_from_IIterator(iface)->ref);
}

static ULONG WINAPI iterator_Release(IIterator_IKeyValuePair_HSTRING_HSTRING *iface)
{
    struct string_iterator *impl = impl_from_IIterator(iface);
    ULONG ref = InterlockedDecrement(&impl->ref);

    if (!ref)
    {
        snapshot_release(impl->snapshot);
        free(impl);
    }
    return ref;
}

static HRESULT WINAPI iterator_GetIids(IIterator_IKeyValuePair_HSTRING_HSTRING *iface, ULONG *count, IID **iids)
{
    return get_iids(&IID_IIterator_IKeyValuePair_HSTRING_HSTRING, count, iids);
}

static HRESULT WINAPI iterator_GetRuntimeClassName(IIterator_IKeyValuePair_HSTRING_HSTRING *iface, HSTRING *name)
{
    return class_name(L"Windows.Foundation.Collections.IIterator`1<Windows.Foundation.Collections.IKeyValuePair`2<String, String>>",
                      name);
}

static HRESULT WINAPI iterator_GetTrustLevel(IIterator_IKeyValuePair_HSTRING_HSTRING *iface, TrustLevel *level)
{
    return trust_level(level);
}

static HRESULT WINAPI iterator_get_Current(IIterator_IKeyValuePair_HSTRING_HSTRING *iface,
                                           IKeyValuePair_HSTRING_HSTRING **value)
{
    struct string_iterator *impl = impl_from_IIterator(iface);

    if (!value)
        return E_POINTER;
    *value = NULL;
    if (impl->index >= impl->snapshot->count)
        return E_BOUNDS;
    return kv_create(impl->snapshot->pairs[impl->index].key, impl->snapshot->pairs[impl->index].value, value);
}

static HRESULT WINAPI iterator_get_HasCurrent(IIterator_IKeyValuePair_HSTRING_HSTRING *iface, boolean *value)
{
    struct string_iterator *impl = impl_from_IIterator(iface);

    if (!value)
        return E_POINTER;
    *value = impl->index < impl->snapshot->count;
    return S_OK;
}

static HRESULT WINAPI iterator_MoveNext(IIterator_IKeyValuePair_HSTRING_HSTRING *iface, boolean *value)
{
    struct string_iterator *impl = impl_from_IIterator(iface);

    if (!value)
        return E_POINTER;
    if (impl->index < impl->snapshot->count)
        impl->index++;
    *value = impl->index < impl->snapshot->count;
    return S_OK;
}

static HRESULT WINAPI iterator_GetMany(IIterator_IKeyValuePair_HSTRING_HSTRING *iface, UINT32 size,
                                       IKeyValuePair_HSTRING_HSTRING **items, UINT32 *count)
{
    struct string_iterator *impl = impl_from_IIterator(iface);
    UINT32 i;
    HRESULT hr;

    if (!count || (size && !items))
        return E_POINTER;
    for (i = 0; i < size && impl->index < impl->snapshot->count; i++, impl->index++)
    {
        if (FAILED(hr = kv_create(impl->snapshot->pairs[impl->index].key, impl->snapshot->pairs[impl->index].value,
                                  &items[i])))
        {
            while (i)
            {
                i--;
                IKeyValuePair_HSTRING_HSTRING_Release(items[i]);
            }
            *count = 0;
            return hr;
        }
    }
    *count = i;
    return S_OK;
}

static const IIterator_IKeyValuePair_HSTRING_HSTRINGVtbl iterator_vtbl =
{
    iterator_QueryInterface,
    iterator_AddRef,
    iterator_Release,
    iterator_GetIids,
    iterator_GetRuntimeClassName,
    iterator_GetTrustLevel,
    iterator_get_Current,
    iterator_get_HasCurrent,
    iterator_MoveNext,
    iterator_GetMany,
};

static HRESULT iterator_create(struct snapshot *snapshot, IIterator_IKeyValuePair_HSTRING_HSTRING **out)
{
    struct string_iterator *impl;

    if (!out)
        return E_POINTER;
    if (!(impl = calloc(1, sizeof(*impl))))
        return E_OUTOFMEMORY;
    impl->IIterator_iface.lpVtbl = (IIterator_IKeyValuePair_HSTRING_HSTRINGVtbl *)&iterator_vtbl;
    impl->ref = 1;
    impl->snapshot = snapshot;
    InterlockedIncrement(&snapshot->ref);
    *out = &impl->IIterator_iface;
    return S_OK;
}

static inline struct string_view *impl_from_IMapView(IMapView_HSTRING_HSTRING *iface)
{
    return CONTAINING_RECORD(iface, struct string_view, IMapView_HSTRING_HSTRING_iface);
}

static HRESULT WINAPI view_QueryInterface(IMapView_HSTRING_HSTRING *iface, REFIID iid, void **out)
{
    struct string_view *impl = impl_from_IMapView(iface);

    if (!out)
        return E_POINTER;
    if (IsEqualGUID(iid, &IID_IUnknown) || IsEqualGUID(iid, &IID_IInspectable) || IsEqualGUID(iid, &IID_IAgileObject) ||
        IsEqualGUID(iid, &IID_IMapView_HSTRING_HSTRING))
        *out = &impl->IMapView_HSTRING_HSTRING_iface;
    else if (IsEqualGUID(iid, &IID_IIterable_IKeyValuePair_HSTRING_HSTRING))
        *out = &impl->IIterable_iface;
    else
    {
        *out = NULL;
        return E_NOINTERFACE;
    }
    IUnknown_AddRef((IUnknown *)*out);
    return S_OK;
}

static ULONG WINAPI view_AddRef(IMapView_HSTRING_HSTRING *iface)
{
    return InterlockedIncrement(&impl_from_IMapView(iface)->ref);
}

static ULONG WINAPI view_Release(IMapView_HSTRING_HSTRING *iface)
{
    struct string_view *impl = impl_from_IMapView(iface);
    ULONG ref = InterlockedDecrement(&impl->ref);

    if (!ref)
    {
        snapshot_release(impl->snapshot);
        free(impl);
    }
    return ref;
}

static HRESULT WINAPI view_GetIids(IMapView_HSTRING_HSTRING *iface, ULONG *count, IID **iids)
{
    return get_iids(&IID_IMapView_HSTRING_HSTRING, count, iids);
}

static HRESULT WINAPI view_GetRuntimeClassName(IMapView_HSTRING_HSTRING *iface, HSTRING *name)
{
    return class_name(L"Windows.Foundation.Collections.IMapView`2<String, String>", name);
}

static HRESULT WINAPI view_GetTrustLevel(IMapView_HSTRING_HSTRING *iface, TrustLevel *level)
{
    return trust_level(level);
}

static HRESULT WINAPI view_Lookup(IMapView_HSTRING_HSTRING *iface, HSTRING key, HSTRING *value)
{
    struct snapshot *snapshot = impl_from_IMapView(iface)->snapshot;
    UINT32 index;

    if (!value)
        return E_POINTER;
    *value = NULL;
    if (!find_pair(snapshot->pairs, snapshot->count, key, &index))
        return E_BOUNDS;
    return WindowsDuplicateString(snapshot->pairs[index].value, value);
}

static HRESULT WINAPI view_get_Size(IMapView_HSTRING_HSTRING *iface, UINT32 *size)
{
    if (!size)
        return E_POINTER;
    *size = impl_from_IMapView(iface)->snapshot->count;
    return S_OK;
}

static HRESULT WINAPI view_HasKey(IMapView_HSTRING_HSTRING *iface, HSTRING key, boolean *found)
{
    struct snapshot *snapshot = impl_from_IMapView(iface)->snapshot;
    UINT32 index;

    if (!found)
        return E_POINTER;
    *found = find_pair(snapshot->pairs, snapshot->count, key, &index);
    return S_OK;
}

static HRESULT WINAPI view_Split(IMapView_HSTRING_HSTRING *iface, IMapView_HSTRING_HSTRING **first,
                                 IMapView_HSTRING_HSTRING **second)
{
    if (!first || !second)
        return E_POINTER;
    *first = *second = NULL;
    return S_OK;
}

static const IMapView_HSTRING_HSTRINGVtbl view_vtbl =
{
    view_QueryInterface,
    view_AddRef,
    view_Release,
    view_GetIids,
    view_GetRuntimeClassName,
    view_GetTrustLevel,
    view_Lookup,
    view_get_Size,
    view_HasKey,
    view_Split,
};

static inline struct string_view *impl_from_view_IIterable(IIterable_IKeyValuePair_HSTRING_HSTRING *iface)
{
    return CONTAINING_RECORD(iface, struct string_view, IIterable_iface);
}

static HRESULT WINAPI view_iterable_QueryInterface(IIterable_IKeyValuePair_HSTRING_HSTRING *iface, REFIID iid, void **out)
{
    return view_QueryInterface(&impl_from_view_IIterable(iface)->IMapView_HSTRING_HSTRING_iface, iid, out);
}

static ULONG WINAPI view_iterable_AddRef(IIterable_IKeyValuePair_HSTRING_HSTRING *iface)
{
    return view_AddRef(&impl_from_view_IIterable(iface)->IMapView_HSTRING_HSTRING_iface);
}

static ULONG WINAPI view_iterable_Release(IIterable_IKeyValuePair_HSTRING_HSTRING *iface)
{
    return view_Release(&impl_from_view_IIterable(iface)->IMapView_HSTRING_HSTRING_iface);
}

static HRESULT WINAPI view_iterable_GetIids(IIterable_IKeyValuePair_HSTRING_HSTRING *iface, ULONG *count, IID **iids)
{
    return get_iids(&IID_IIterable_IKeyValuePair_HSTRING_HSTRING, count, iids);
}

static HRESULT WINAPI view_iterable_GetRuntimeClassName(IIterable_IKeyValuePair_HSTRING_HSTRING *iface, HSTRING *name)
{
    return view_GetRuntimeClassName(NULL, name);
}

static HRESULT WINAPI view_iterable_GetTrustLevel(IIterable_IKeyValuePair_HSTRING_HSTRING *iface, TrustLevel *level)
{
    return trust_level(level);
}

static HRESULT WINAPI view_iterable_First(IIterable_IKeyValuePair_HSTRING_HSTRING *iface,
                                          IIterator_IKeyValuePair_HSTRING_HSTRING **value)
{
    return iterator_create(impl_from_view_IIterable(iface)->snapshot, value);
}

static const IIterable_IKeyValuePair_HSTRING_HSTRINGVtbl view_iterable_vtbl =
{
    view_iterable_QueryInterface,
    view_iterable_AddRef,
    view_iterable_Release,
    view_iterable_GetIids,
    view_iterable_GetRuntimeClassName,
    view_iterable_GetTrustLevel,
    view_iterable_First,
};

static inline struct string_map *impl_from_IMap(IMap_HSTRING_HSTRING *iface)
{
    return CONTAINING_RECORD(iface, struct string_map, IMap_HSTRING_HSTRING_iface);
}

static HRESULT take_snapshot(struct string_map *impl, struct snapshot **out)
{
    struct snapshot *snapshot;
    UINT32 i;

    AcquireSRWLockShared(&impl->lock);
    if (!(snapshot = calloc(1, offsetof(struct snapshot, pairs[impl->count ? impl->count : 1]))))
    {
        ReleaseSRWLockShared(&impl->lock);
        return E_OUTOFMEMORY;
    }
    snapshot->ref = 1;
    snapshot->count = impl->count;
    for (i = 0; i < impl->count; i++)
    {
        WindowsDuplicateString(impl->pairs[i].key, &snapshot->pairs[i].key);
        WindowsDuplicateString(impl->pairs[i].value, &snapshot->pairs[i].value);
    }
    ReleaseSRWLockShared(&impl->lock);
    *out = snapshot;
    return S_OK;
}

static HRESULT WINAPI map_QueryInterface(IMap_HSTRING_HSTRING *iface, REFIID iid, void **out)
{
    struct string_map *impl = impl_from_IMap(iface);

    if (!out)
        return E_POINTER;
    if (IsEqualGUID(iid, &IID_IUnknown) || IsEqualGUID(iid, &IID_IInspectable) || IsEqualGUID(iid, &IID_IAgileObject) ||
        IsEqualGUID(iid, &IID_IMap_HSTRING_HSTRING))
        *out = &impl->IMap_HSTRING_HSTRING_iface;
    else if (IsEqualGUID(iid, &IID_IIterable_IKeyValuePair_HSTRING_HSTRING))
        *out = &impl->IIterable_iface;
    else
    {
        *out = NULL;
        return E_NOINTERFACE;
    }
    IUnknown_AddRef((IUnknown *)*out);
    return S_OK;
}

static ULONG WINAPI map_AddRef(IMap_HSTRING_HSTRING *iface)
{
    return InterlockedIncrement(&impl_from_IMap(iface)->ref);
}

static ULONG WINAPI map_Release(IMap_HSTRING_HSTRING *iface)
{
    struct string_map *impl = impl_from_IMap(iface);
    ULONG ref = InterlockedDecrement(&impl->ref);
    UINT32 i;

    if (!ref)
    {
        for (i = 0; i < impl->count; i++)
        {
            WindowsDeleteString(impl->pairs[i].key);
            WindowsDeleteString(impl->pairs[i].value);
        }
        free(impl->pairs);
        free(impl);
    }
    return ref;
}

static HRESULT WINAPI map_GetIids(IMap_HSTRING_HSTRING *iface, ULONG *count, IID **iids)
{
    return get_iids(&IID_IMap_HSTRING_HSTRING, count, iids);
}

static HRESULT WINAPI map_GetRuntimeClassName(IMap_HSTRING_HSTRING *iface, HSTRING *name)
{
    return class_name(L"Windows.Foundation.Collections.IMap`2<String, String>", name);
}

static HRESULT WINAPI map_GetTrustLevel(IMap_HSTRING_HSTRING *iface, TrustLevel *level)
{
    return trust_level(level);
}

static HRESULT WINAPI map_Lookup(IMap_HSTRING_HSTRING *iface, HSTRING key, HSTRING *value)
{
    struct string_map *impl = impl_from_IMap(iface);
    UINT32 index;
    HRESULT hr = E_BOUNDS;

    if (!value)
        return E_POINTER;
    *value = NULL;
    AcquireSRWLockShared(&impl->lock);
    if (find_pair(impl->pairs, impl->count, key, &index))
        hr = WindowsDuplicateString(impl->pairs[index].value, value);
    ReleaseSRWLockShared(&impl->lock);
    return hr;
}

static HRESULT WINAPI map_get_Size(IMap_HSTRING_HSTRING *iface, UINT32 *size)
{
    struct string_map *impl = impl_from_IMap(iface);

    if (!size)
        return E_POINTER;
    AcquireSRWLockShared(&impl->lock);
    *size = impl->count;
    ReleaseSRWLockShared(&impl->lock);
    return S_OK;
}

static HRESULT WINAPI map_HasKey(IMap_HSTRING_HSTRING *iface, HSTRING key, boolean *found)
{
    struct string_map *impl = impl_from_IMap(iface);
    UINT32 index;

    if (!found)
        return E_POINTER;
    AcquireSRWLockShared(&impl->lock);
    *found = find_pair(impl->pairs, impl->count, key, &index);
    ReleaseSRWLockShared(&impl->lock);
    return S_OK;
}

static HRESULT WINAPI map_GetView(IMap_HSTRING_HSTRING *iface, IMapView_HSTRING_HSTRING **out)
{
    struct string_map *impl = impl_from_IMap(iface);
    struct string_view *view;
    HRESULT hr;

    if (!out)
        return E_POINTER;
    *out = NULL;
    if (!(view = calloc(1, sizeof(*view))))
        return E_OUTOFMEMORY;
    if (FAILED(hr = take_snapshot(impl, &view->snapshot)))
    {
        free(view);
        return hr;
    }
    view->IMapView_HSTRING_HSTRING_iface.lpVtbl = (IMapView_HSTRING_HSTRINGVtbl *)&view_vtbl;
    view->IIterable_iface.lpVtbl = (IIterable_IKeyValuePair_HSTRING_HSTRINGVtbl *)&view_iterable_vtbl;
    view->ref = 1;
    *out = &view->IMapView_HSTRING_HSTRING_iface;
    return S_OK;
}

static HRESULT WINAPI map_Insert(IMap_HSTRING_HSTRING *iface, HSTRING key, HSTRING value, boolean *replaced)
{
    struct string_map *impl = impl_from_IMap(iface);
    struct string_pair *pairs;
    HSTRING key_copy, value_copy;
    UINT32 index, capacity;
    HRESULT hr;

    if (!replaced)
        return E_POINTER;
    if (FAILED(hr = WindowsDuplicateString(value, &value_copy)))
        return hr;
    AcquireSRWLockExclusive(&impl->lock);
    if ((*replaced = find_pair(impl->pairs, impl->count, key, &index)))
    {
        WindowsDeleteString(impl->pairs[index].value);
        impl->pairs[index].value = value_copy;
        ReleaseSRWLockExclusive(&impl->lock);
        return S_OK;
    }
    if (impl->count == impl->capacity)
    {
        capacity = max(4, impl->capacity * 2);
        if (!(pairs = realloc(impl->pairs, capacity * sizeof(*pairs))))
        {
            ReleaseSRWLockExclusive(&impl->lock);
            WindowsDeleteString(value_copy);
            return E_OUTOFMEMORY;
        }
        impl->pairs = pairs;
        impl->capacity = capacity;
    }
    if (FAILED(hr = WindowsDuplicateString(key, &key_copy)))
    {
        ReleaseSRWLockExclusive(&impl->lock);
        WindowsDeleteString(value_copy);
        return hr;
    }
    memmove(impl->pairs + index + 1, impl->pairs + index, (impl->count - index) * sizeof(*impl->pairs));
    impl->pairs[index].key = key_copy;
    impl->pairs[index].value = value_copy;
    impl->count++;
    ReleaseSRWLockExclusive(&impl->lock);
    return S_OK;
}

static HRESULT WINAPI map_Remove(IMap_HSTRING_HSTRING *iface, HSTRING key)
{
    struct string_map *impl = impl_from_IMap(iface);
    struct string_pair removed;
    UINT32 index;

    AcquireSRWLockExclusive(&impl->lock);
    if (!find_pair(impl->pairs, impl->count, key, &index))
    {
        ReleaseSRWLockExclusive(&impl->lock);
        return E_BOUNDS;
    }
    removed = impl->pairs[index];
    memmove(impl->pairs + index, impl->pairs + index + 1, (impl->count - index - 1) * sizeof(*impl->pairs));
    impl->count--;
    ReleaseSRWLockExclusive(&impl->lock);
    WindowsDeleteString(removed.key);
    WindowsDeleteString(removed.value);
    return S_OK;
}

static HRESULT WINAPI map_Clear(IMap_HSTRING_HSTRING *iface)
{
    struct string_map *impl = impl_from_IMap(iface);
    struct string_pair *pairs;
    UINT32 count, i;

    AcquireSRWLockExclusive(&impl->lock);
    pairs = impl->pairs;
    count = impl->count;
    impl->pairs = NULL;
    impl->count = impl->capacity = 0;
    ReleaseSRWLockExclusive(&impl->lock);
    for (i = 0; i < count; i++)
    {
        WindowsDeleteString(pairs[i].key);
        WindowsDeleteString(pairs[i].value);
    }
    free(pairs);
    return S_OK;
}

static const IMap_HSTRING_HSTRINGVtbl map_vtbl =
{
    map_QueryInterface,
    map_AddRef,
    map_Release,
    map_GetIids,
    map_GetRuntimeClassName,
    map_GetTrustLevel,
    map_Lookup,
    map_get_Size,
    map_HasKey,
    map_GetView,
    map_Insert,
    map_Remove,
    map_Clear,
};

static inline struct string_map *impl_from_map_IIterable(IIterable_IKeyValuePair_HSTRING_HSTRING *iface)
{
    return CONTAINING_RECORD(iface, struct string_map, IIterable_iface);
}

static HRESULT WINAPI map_iterable_QueryInterface(IIterable_IKeyValuePair_HSTRING_HSTRING *iface, REFIID iid, void **out)
{
    return map_QueryInterface(&impl_from_map_IIterable(iface)->IMap_HSTRING_HSTRING_iface, iid, out);
}

static ULONG WINAPI map_iterable_AddRef(IIterable_IKeyValuePair_HSTRING_HSTRING *iface)
{
    return map_AddRef(&impl_from_map_IIterable(iface)->IMap_HSTRING_HSTRING_iface);
}

static ULONG WINAPI map_iterable_Release(IIterable_IKeyValuePair_HSTRING_HSTRING *iface)
{
    return map_Release(&impl_from_map_IIterable(iface)->IMap_HSTRING_HSTRING_iface);
}

static HRESULT WINAPI map_iterable_GetIids(IIterable_IKeyValuePair_HSTRING_HSTRING *iface, ULONG *count, IID **iids)
{
    return get_iids(&IID_IIterable_IKeyValuePair_HSTRING_HSTRING, count, iids);
}

static HRESULT WINAPI map_iterable_GetRuntimeClassName(IIterable_IKeyValuePair_HSTRING_HSTRING *iface, HSTRING *name)
{
    return map_GetRuntimeClassName(NULL, name);
}

static HRESULT WINAPI map_iterable_GetTrustLevel(IIterable_IKeyValuePair_HSTRING_HSTRING *iface, TrustLevel *level)
{
    return trust_level(level);
}

static HRESULT WINAPI map_iterable_First(IIterable_IKeyValuePair_HSTRING_HSTRING *iface,
                                         IIterator_IKeyValuePair_HSTRING_HSTRING **value)
{
    struct snapshot *snapshot;
    HRESULT hr;

    if (!value)
        return E_POINTER;
    *value = NULL;
    if (FAILED(hr = take_snapshot(impl_from_map_IIterable(iface), &snapshot)))
        return hr;
    hr = iterator_create(snapshot, value);
    snapshot_release(snapshot);
    return hr;
}

static const IIterable_IKeyValuePair_HSTRING_HSTRINGVtbl map_iterable_vtbl =
{
    map_iterable_QueryInterface,
    map_iterable_AddRef,
    map_iterable_Release,
    map_iterable_GetIids,
    map_iterable_GetRuntimeClassName,
    map_iterable_GetTrustLevel,
    map_iterable_First,
};

HRESULT string_map_create(IMap_HSTRING_HSTRING **out)
{
    struct string_map *impl;

    if (!(impl = calloc(1, sizeof(*impl))))
        return E_OUTOFMEMORY;
    impl->IMap_HSTRING_HSTRING_iface.lpVtbl = (IMap_HSTRING_HSTRINGVtbl *)&map_vtbl;
    impl->IIterable_iface.lpVtbl = (IIterable_IKeyValuePair_HSTRING_HSTRINGVtbl *)&map_iterable_vtbl;
    impl->ref = 1;
    InitializeSRWLock(&impl->lock);
    *out = &impl->IMap_HSTRING_HSTRING_iface;
    return S_OK;
}
