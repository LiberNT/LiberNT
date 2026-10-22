/*
 * PROJECT:     LiberNT Windows Runtime
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Read-only vector views returned by the toast platform
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "private.h"

struct vector_view
{
    IVectorView_IInspectable IVectorView_iface;
    IIterable_IInspectable IIterable_iface;
    LONG ref;
    const IID *view_iid;
    const IID *iterable_iid;
    const IID *iterator_iid;
    UINT32 size;
    IInspectable *items[1];
};

struct vector_iterator
{
    IIterator_IInspectable IIterator_iface;
    LONG ref;
    struct vector_view *view;
    UINT32 index;
};

static inline struct vector_view *impl_from_IVectorView(IVectorView_IInspectable *iface)
{
    return CONTAINING_RECORD(iface, struct vector_view, IVectorView_iface);
}

static inline struct vector_view *impl_from_IIterable(IIterable_IInspectable *iface)
{
    return CONTAINING_RECORD(iface, struct vector_view, IIterable_iface);
}

static inline struct vector_iterator *impl_from_IIterator(IIterator_IInspectable *iface)
{
    return CONTAINING_RECORD(iface, struct vector_iterator, IIterator_iface);
}

static HRESULT WINAPI iterator_QueryInterface(IIterator_IInspectable *iface, REFIID iid, void **out)
{
    struct vector_iterator *impl = impl_from_IIterator(iface);

    if (!out)
        return E_POINTER;
    if (IsEqualGUID(iid, &IID_IUnknown) || IsEqualGUID(iid, &IID_IInspectable) ||
        IsEqualGUID(iid, &IID_IAgileObject) || IsEqualGUID(iid, impl->view->iterator_iid))
    {
        *out = iface;
        IIterator_IInspectable_AddRef(iface);
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI iterator_AddRef(IIterator_IInspectable *iface)
{
    return InterlockedIncrement(&impl_from_IIterator(iface)->ref);
}

static ULONG WINAPI iterator_Release(IIterator_IInspectable *iface)
{
    struct vector_iterator *impl = impl_from_IIterator(iface);
    ULONG ref = InterlockedDecrement(&impl->ref);

    if (!ref)
    {
        IVectorView_IInspectable_Release(&impl->view->IVectorView_iface);
        free(impl);
    }
    return ref;
}

static HRESULT WINAPI iterator_GetIids(IIterator_IInspectable *iface, ULONG *count, IID **iids)
{
    return get_iids(impl_from_IIterator(iface)->view->iterator_iid, count, iids);
}

static HRESULT WINAPI iterator_GetRuntimeClassName(IIterator_IInspectable *iface, HSTRING *class_name)
{
    if (!class_name)
        return E_POINTER;
    *class_name = NULL;
    return S_OK;
}

static HRESULT WINAPI iterator_GetTrustLevel(IIterator_IInspectable *iface, TrustLevel *trust_level)
{
    if (!trust_level)
        return E_POINTER;
    *trust_level = BaseTrust;
    return S_OK;
}

static HRESULT WINAPI iterator_get_Current(IIterator_IInspectable *iface, IInspectable **value)
{
    struct vector_iterator *impl = impl_from_IIterator(iface);

    if (!value)
        return E_POINTER;
    *value = NULL;
    if (impl->index >= impl->view->size)
        return E_BOUNDS;
    *value = impl->view->items[impl->index];
    IInspectable_AddRef(*value);
    return S_OK;
}

static HRESULT WINAPI iterator_get_HasCurrent(IIterator_IInspectable *iface, boolean *value)
{
    struct vector_iterator *impl = impl_from_IIterator(iface);

    if (!value)
        return E_POINTER;
    *value = impl->index < impl->view->size;
    return S_OK;
}

static HRESULT WINAPI iterator_MoveNext(IIterator_IInspectable *iface, boolean *value)
{
    struct vector_iterator *impl = impl_from_IIterator(iface);

    if (!value)
        return E_POINTER;
    if (impl->index < impl->view->size)
        impl->index++;
    *value = impl->index < impl->view->size;
    return S_OK;
}

static HRESULT WINAPI iterator_GetMany(IIterator_IInspectable *iface, UINT32 items_size,
                                       IInspectable **items, UINT32 *count)
{
    struct vector_iterator *impl = impl_from_IIterator(iface);
    UINT32 i;

    if (!count || (items_size && !items))
        return E_POINTER;
    for (i = 0; i < items_size && impl->index < impl->view->size; i++, impl->index++)
    {
        items[i] = impl->view->items[impl->index];
        IInspectable_AddRef(items[i]);
    }
    *count = i;
    return S_OK;
}

static const IIterator_IInspectableVtbl iterator_vtbl =
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

static HRESULT WINAPI view_QueryInterface(IVectorView_IInspectable *iface, REFIID iid, void **out)
{
    struct vector_view *impl = impl_from_IVectorView(iface);

    if (!out)
        return E_POINTER;
    if (IsEqualGUID(iid, &IID_IUnknown) || IsEqualGUID(iid, &IID_IInspectable) ||
        IsEqualGUID(iid, &IID_IAgileObject) || IsEqualGUID(iid, impl->view_iid))
        *out = &impl->IVectorView_iface;
    else if (IsEqualGUID(iid, impl->iterable_iid))
        *out = &impl->IIterable_iface;
    else
    {
        *out = NULL;
        return E_NOINTERFACE;
    }
    IUnknown_AddRef((IUnknown *)*out);
    return S_OK;
}

static ULONG WINAPI view_AddRef(IVectorView_IInspectable *iface)
{
    return InterlockedIncrement(&impl_from_IVectorView(iface)->ref);
}

static ULONG WINAPI view_Release(IVectorView_IInspectable *iface)
{
    struct vector_view *impl = impl_from_IVectorView(iface);
    ULONG ref = InterlockedDecrement(&impl->ref);
    UINT32 i;

    if (!ref)
    {
        for (i = 0; i < impl->size; i++)
            IInspectable_Release(impl->items[i]);
        free(impl);
    }
    return ref;
}

static HRESULT WINAPI view_GetIids(IVectorView_IInspectable *iface, ULONG *count, IID **iids)
{
    return get_iids(impl_from_IVectorView(iface)->view_iid, count, iids);
}

static HRESULT WINAPI view_GetRuntimeClassName(IVectorView_IInspectable *iface, HSTRING *class_name)
{
    if (!class_name)
        return E_POINTER;
    *class_name = NULL;
    return S_OK;
}

static HRESULT WINAPI view_GetTrustLevel(IVectorView_IInspectable *iface, TrustLevel *trust_level)
{
    if (!trust_level)
        return E_POINTER;
    *trust_level = BaseTrust;
    return S_OK;
}

static HRESULT WINAPI view_GetAt(IVectorView_IInspectable *iface, UINT32 index, IInspectable **value)
{
    struct vector_view *impl = impl_from_IVectorView(iface);

    if (!value)
        return E_POINTER;
    *value = NULL;
    if (index >= impl->size)
        return E_BOUNDS;
    *value = impl->items[index];
    IInspectable_AddRef(*value);
    return S_OK;
}

static HRESULT WINAPI view_get_Size(IVectorView_IInspectable *iface, UINT32 *value)
{
    if (!value)
        return E_POINTER;
    *value = impl_from_IVectorView(iface)->size;
    return S_OK;
}

static HRESULT WINAPI view_IndexOf(IVectorView_IInspectable *iface, IInspectable *element,
                                   UINT32 *index, boolean *found)
{
    struct vector_view *impl = impl_from_IVectorView(iface);
    UINT32 i;

    if (!index || !found)
        return E_POINTER;
    for (i = 0; i < impl->size; i++)
    {
        if (impl->items[i] == element)
            break;
    }
    *found = i < impl->size;
    *index = *found ? i : 0;
    return S_OK;
}

static HRESULT WINAPI view_GetMany(IVectorView_IInspectable *iface, UINT32 start_index,
                                   UINT32 items_size, IInspectable **items, UINT32 *count)
{
    struct vector_view *impl = impl_from_IVectorView(iface);
    UINT32 i;

    if (!count || (items_size && !items))
        return E_POINTER;
    *count = 0;
    if (start_index > impl->size)
        return E_BOUNDS;
    for (i = 0; i < items_size && start_index + i < impl->size; i++)
    {
        items[i] = impl->items[start_index + i];
        IInspectable_AddRef(items[i]);
    }
    *count = i;
    return S_OK;
}

static const IVectorView_IInspectableVtbl view_vtbl =
{
    view_QueryInterface,
    view_AddRef,
    view_Release,
    view_GetIids,
    view_GetRuntimeClassName,
    view_GetTrustLevel,
    view_GetAt,
    view_get_Size,
    view_IndexOf,
    view_GetMany,
};

static HRESULT WINAPI iterable_QueryInterface(IIterable_IInspectable *iface, REFIID iid, void **out)
{
    return view_QueryInterface(&impl_from_IIterable(iface)->IVectorView_iface, iid, out);
}

static ULONG WINAPI iterable_AddRef(IIterable_IInspectable *iface)
{
    return view_AddRef(&impl_from_IIterable(iface)->IVectorView_iface);
}

static ULONG WINAPI iterable_Release(IIterable_IInspectable *iface)
{
    return view_Release(&impl_from_IIterable(iface)->IVectorView_iface);
}

static HRESULT WINAPI iterable_GetIids(IIterable_IInspectable *iface, ULONG *count, IID **iids)
{
    return get_iids(impl_from_IIterable(iface)->iterable_iid, count, iids);
}

static HRESULT WINAPI iterable_GetRuntimeClassName(IIterable_IInspectable *iface, HSTRING *class_name)
{
    return view_GetRuntimeClassName(&impl_from_IIterable(iface)->IVectorView_iface, class_name);
}

static HRESULT WINAPI iterable_GetTrustLevel(IIterable_IInspectable *iface, TrustLevel *trust_level)
{
    return view_GetTrustLevel(&impl_from_IIterable(iface)->IVectorView_iface, trust_level);
}

static HRESULT WINAPI iterable_First(IIterable_IInspectable *iface, IIterator_IInspectable **value)
{
    struct vector_view *impl = impl_from_IIterable(iface);
    struct vector_iterator *iterator;

    if (!value)
        return E_POINTER;
    if (!(iterator = calloc(1, sizeof(*iterator))))
        return E_OUTOFMEMORY;
    iterator->IIterator_iface.lpVtbl = (IIterator_IInspectableVtbl *)&iterator_vtbl;
    iterator->ref = 1;
    iterator->view = impl;
    IVectorView_IInspectable_AddRef(&impl->IVectorView_iface);
    *value = &iterator->IIterator_iface;
    return S_OK;
}

static const IIterable_IInspectableVtbl iterable_vtbl =
{
    iterable_QueryInterface,
    iterable_AddRef,
    iterable_Release,
    iterable_GetIids,
    iterable_GetRuntimeClassName,
    iterable_GetTrustLevel,
    iterable_First,
};

HRESULT vector_view_create(REFIID view_iid, REFIID iterable_iid, REFIID iterator_iid,
                           IInspectable **items, UINT32 count, void **out)
{
    struct vector_view *impl;
    UINT32 i;

    *out = NULL;
    impl = calloc(1, FIELD_OFFSET(struct vector_view, items[count ? count : 1]));
    if (!impl)
        return E_OUTOFMEMORY;
    impl->IVectorView_iface.lpVtbl = (IVectorView_IInspectableVtbl *)&view_vtbl;
    impl->IIterable_iface.lpVtbl = (IIterable_IInspectableVtbl *)&iterable_vtbl;
    impl->ref = 1;
    impl->view_iid = view_iid;
    impl->iterable_iid = iterable_iid;
    impl->iterator_iid = iterator_iid;
    impl->size = count;
    for (i = 0; i < count; i++)
    {
        impl->items[i] = items[i];
        IInspectable_AddRef(impl->items[i]);
    }
    *out = &impl->IVectorView_iface;
    return S_OK;
}
