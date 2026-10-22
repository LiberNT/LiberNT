/*
 * PROJECT:     LiberNT Windows Runtime
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Asynchronous operations and item views of the Store install control
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "private.h"

struct async_operation
{
    IAsyncOperation_IInspectable IAsyncOperation_iface;
    IAsyncInfo IAsyncInfo_iface;
    const IID *iid;
    LONG ref;
    SRWLOCK lock;
    AsyncStatus status;
    HRESULT error;
    BOOL is_boolean;
    boolean value;
    IAsyncOperationCompletedHandler_IInspectable *handler;
    BOOL handler_set;
    UINT32 id;
};

static LONG next_async_id;

static inline struct async_operation *impl_from_IAsyncOperation(IAsyncOperation_IInspectable *iface)
{
    return CONTAINING_RECORD(iface, struct async_operation, IAsyncOperation_iface);
}

static inline struct async_operation *impl_from_IAsyncInfo(IAsyncInfo *iface)
{
    return CONTAINING_RECORD(iface, struct async_operation, IAsyncInfo_iface);
}

static HRESULT WINAPI async_QueryInterface(IAsyncOperation_IInspectable *iface, REFIID iid, void **out)
{
    struct async_operation *impl = impl_from_IAsyncOperation(iface);

    if (!out)
        return E_POINTER;
    if (IsEqualGUID(iid, &IID_IUnknown) || IsEqualGUID(iid, &IID_IInspectable) ||
        IsEqualGUID(iid, &IID_IAgileObject) || IsEqualGUID(iid, impl->iid))
        *out = &impl->IAsyncOperation_iface;
    else if (IsEqualGUID(iid, &IID_IAsyncInfo))
        *out = &impl->IAsyncInfo_iface;
    else
    {
        *out = NULL;
        return E_NOINTERFACE;
    }
    IUnknown_AddRef((IUnknown *)*out);
    return S_OK;
}

static ULONG WINAPI async_AddRef(IAsyncOperation_IInspectable *iface)
{
    return InterlockedIncrement(&impl_from_IAsyncOperation(iface)->ref);
}

static ULONG WINAPI async_Release(IAsyncOperation_IInspectable *iface)
{
    struct async_operation *impl = impl_from_IAsyncOperation(iface);
    ULONG ref = InterlockedDecrement(&impl->ref);

    if (!ref)
    {
        if (impl->handler)
            IAsyncOperationCompletedHandler_IInspectable_Release(impl->handler);
        free(impl);
    }
    return ref;
}

static HRESULT WINAPI async_GetIids(IAsyncOperation_IInspectable *iface, ULONG *count, IID **iids)
{
    return get_iids(impl_from_IAsyncOperation(iface)->iid, count, iids);
}

static HRESULT WINAPI async_GetRuntimeClassName(IAsyncOperation_IInspectable *iface, HSTRING *class_name)
{
    if (!class_name)
        return E_POINTER;
    *class_name = NULL;
    return S_OK;
}

static HRESULT WINAPI async_GetTrustLevel(IAsyncOperation_IInspectable *iface, TrustLevel *level)
{
    return trust_level(level);
}

static HRESULT WINAPI async_put_Completed(IAsyncOperation_IInspectable *iface,
                                          IAsyncOperationCompletedHandler_IInspectable *handler)
{
    struct async_operation *impl = impl_from_IAsyncOperation(iface);
    AsyncStatus status;

    if (!handler)
        return E_INVALIDARG;
    AcquireSRWLockExclusive(&impl->lock);
    if (impl->handler_set)
    {
        ReleaseSRWLockExclusive(&impl->lock);
        return E_ILLEGAL_DELEGATE_ASSIGNMENT;
    }
    impl->handler_set = TRUE;
    status = impl->status;
    if (status == Started)
    {
        impl->handler = handler;
        IAsyncOperationCompletedHandler_IInspectable_AddRef(impl->handler);
    }
    ReleaseSRWLockExclusive(&impl->lock);
    if (status != Started)
        IAsyncOperationCompletedHandler_IInspectable_Invoke(handler, iface, status);
    return S_OK;
}

static HRESULT WINAPI async_get_Completed(IAsyncOperation_IInspectable *iface,
                                          IAsyncOperationCompletedHandler_IInspectable **handler)
{
    struct async_operation *impl = impl_from_IAsyncOperation(iface);

    if (!handler)
        return E_POINTER;
    AcquireSRWLockShared(&impl->lock);
    if ((*handler = impl->handler))
        IAsyncOperationCompletedHandler_IInspectable_AddRef(*handler);
    ReleaseSRWLockShared(&impl->lock);
    return S_OK;
}

static HRESULT WINAPI async_GetResults_object(IAsyncOperation_IInspectable *iface, IInspectable **results)
{
    struct async_operation *impl = impl_from_IAsyncOperation(iface);
    HRESULT hr;

    if (!results)
        return E_POINTER;
    *results = NULL;
    AcquireSRWLockShared(&impl->lock);
    if (impl->status == Started)
        hr = E_ILLEGAL_METHOD_CALL;
    else if (impl->status == Completed)
        hr = S_OK;
    else
        hr = impl->error;
    ReleaseSRWLockShared(&impl->lock);
    return hr;
}

static HRESULT WINAPI async_GetResults_boolean(IAsyncOperation_boolean *iface, boolean *results)
{
    struct async_operation *impl = impl_from_IAsyncOperation((IAsyncOperation_IInspectable *)iface);
    HRESULT hr;

    if (!results)
        return E_POINTER;
    AcquireSRWLockShared(&impl->lock);
    if (impl->status == Started)
        hr = E_ILLEGAL_METHOD_CALL;
    else if (impl->status == Completed)
    {
        *results = impl->value;
        hr = S_OK;
    }
    else
        hr = impl->error;
    ReleaseSRWLockShared(&impl->lock);
    return hr;
}

static const IAsyncOperation_IInspectableVtbl async_object_vtbl =
{
    async_QueryInterface,
    async_AddRef,
    async_Release,
    async_GetIids,
    async_GetRuntimeClassName,
    async_GetTrustLevel,
    async_put_Completed,
    async_get_Completed,
    async_GetResults_object,
};

static const IAsyncOperation_booleanVtbl async_boolean_vtbl =
{
    (void *)async_QueryInterface,
    (void *)async_AddRef,
    (void *)async_Release,
    (void *)async_GetIids,
    (void *)async_GetRuntimeClassName,
    (void *)async_GetTrustLevel,
    (void *)async_put_Completed,
    (void *)async_get_Completed,
    async_GetResults_boolean,
};

static HRESULT WINAPI info_QueryInterface(IAsyncInfo *iface, REFIID iid, void **out)
{
    return async_QueryInterface(&impl_from_IAsyncInfo(iface)->IAsyncOperation_iface, iid, out);
}

static ULONG WINAPI info_AddRef(IAsyncInfo *iface)
{
    return async_AddRef(&impl_from_IAsyncInfo(iface)->IAsyncOperation_iface);
}

static ULONG WINAPI info_Release(IAsyncInfo *iface)
{
    return async_Release(&impl_from_IAsyncInfo(iface)->IAsyncOperation_iface);
}

static HRESULT WINAPI info_GetIids(IAsyncInfo *iface, ULONG *count, IID **iids)
{
    return get_iids(&IID_IAsyncInfo, count, iids);
}

static HRESULT WINAPI info_GetRuntimeClassName(IAsyncInfo *iface, HSTRING *class_name)
{
    return async_GetRuntimeClassName(NULL, class_name);
}

static HRESULT WINAPI info_GetTrustLevel(IAsyncInfo *iface, TrustLevel *level)
{
    return trust_level(level);
}

static HRESULT WINAPI info_get_Id(IAsyncInfo *iface, UINT32 *id)
{
    if (!id)
        return E_POINTER;
    *id = impl_from_IAsyncInfo(iface)->id;
    return S_OK;
}

static HRESULT WINAPI info_get_Status(IAsyncInfo *iface, AsyncStatus *status)
{
    struct async_operation *impl = impl_from_IAsyncInfo(iface);

    if (!status)
        return E_POINTER;
    AcquireSRWLockShared(&impl->lock);
    *status = impl->status;
    ReleaseSRWLockShared(&impl->lock);
    return S_OK;
}

static HRESULT WINAPI info_get_ErrorCode(IAsyncInfo *iface, HRESULT *error)
{
    struct async_operation *impl = impl_from_IAsyncInfo(iface);

    if (!error)
        return E_POINTER;
    AcquireSRWLockShared(&impl->lock);
    *error = impl->status == Error ? impl->error : (impl->status == Canceled ? E_ABORT : S_OK);
    ReleaseSRWLockShared(&impl->lock);
    return S_OK;
}

static HRESULT WINAPI info_Cancel(IAsyncInfo *iface)
{
    struct async_operation *impl = impl_from_IAsyncInfo(iface);

    AcquireSRWLockExclusive(&impl->lock);
    if (impl->status == Started)
        impl->status = Canceled;
    ReleaseSRWLockExclusive(&impl->lock);
    return S_OK;
}

static HRESULT WINAPI info_Close(IAsyncInfo *iface)
{
    struct async_operation *impl = impl_from_IAsyncInfo(iface);
    HRESULT hr = S_OK;

    AcquireSRWLockShared(&impl->lock);
    if (impl->status == Started)
        hr = E_ILLEGAL_STATE_CHANGE;
    ReleaseSRWLockShared(&impl->lock);
    return hr;
}

static const IAsyncInfoVtbl info_vtbl =
{
    info_QueryInterface,
    info_AddRef,
    info_Release,
    info_GetIids,
    info_GetRuntimeClassName,
    info_GetTrustLevel,
    info_get_Id,
    info_get_Status,
    info_get_ErrorCode,
    info_Cancel,
    info_Close,
};

static void CALLBACK async_complete(PTP_CALLBACK_INSTANCE instance, void *context)
{
    struct async_operation *impl = context;
    IAsyncOperationCompletedHandler_IInspectable *handler;
    AsyncStatus status;

    AcquireSRWLockExclusive(&impl->lock);
    if (impl->status == Started)
        impl->status = FAILED(impl->error) ? Error : Completed;
    status = impl->status;
    handler = impl->handler;
    impl->handler = NULL;
    ReleaseSRWLockExclusive(&impl->lock);
    if (handler)
    {
        IAsyncOperationCompletedHandler_IInspectable_Invoke(handler, &impl->IAsyncOperation_iface, status);
        IAsyncOperationCompletedHandler_IInspectable_Release(handler);
    }
    IAsyncOperation_IInspectable_Release(&impl->IAsyncOperation_iface);
}

static HRESULT async_create(REFIID iid, BOOL is_boolean, HRESULT error, boolean value, void **out)
{
    struct async_operation *impl;

    *out = NULL;
    if (!(impl = calloc(1, sizeof(*impl))))
        return E_OUTOFMEMORY;
    impl->IAsyncOperation_iface.lpVtbl = is_boolean ? (IAsyncOperation_IInspectableVtbl *)&async_boolean_vtbl
                                                    : (IAsyncOperation_IInspectableVtbl *)&async_object_vtbl;
    impl->IAsyncInfo_iface.lpVtbl = (IAsyncInfoVtbl *)&info_vtbl;
    impl->iid = iid;
    impl->ref = 2;
    InitializeSRWLock(&impl->lock);
    impl->status = Started;
    impl->error = error;
    impl->is_boolean = is_boolean;
    impl->value = value;
    impl->id = InterlockedIncrement(&next_async_id);
    if (!TrySubmitThreadpoolCallback(async_complete, impl, NULL))
    {
        free(impl);
        return HRESULT_FROM_WIN32(GetLastError());
    }
    *out = &impl->IAsyncOperation_iface;
    return S_OK;
}

HRESULT async_boolean_create(HRESULT error, boolean value, IAsyncOperation_boolean **out)
{
    if (!out)
        return E_POINTER;
    return async_create(&IID_IAsyncOperation_boolean, TRUE, error, value, (void **)out);
}

HRESULT async_object_create(REFIID iid, HRESULT error, void **out)
{
    if (!out)
        return E_POINTER;
    return async_create(iid, FALSE, error, FALSE, out);
}

struct empty_view
{
    IVectorView_AppInstallItem IVectorView_iface;
    IIterable_AppInstallItem IIterable_iface;
    IIterator_AppInstallItem IIterator_iface;
    LONG ref;
};

static inline struct empty_view *impl_from_IVectorView(IVectorView_AppInstallItem *iface)
{
    return CONTAINING_RECORD(iface, struct empty_view, IVectorView_iface);
}

static HRESULT WINAPI view_QueryInterface(IVectorView_AppInstallItem *iface, REFIID iid, void **out)
{
    struct empty_view *impl = impl_from_IVectorView(iface);

    if (!out)
        return E_POINTER;
    if (IsEqualGUID(iid, &IID_IUnknown) || IsEqualGUID(iid, &IID_IInspectable) ||
        IsEqualGUID(iid, &IID_IAgileObject) || IsEqualGUID(iid, &IID_IVectorView_AppInstallItem))
        *out = &impl->IVectorView_iface;
    else if (IsEqualGUID(iid, &IID_IIterable_AppInstallItem))
        *out = &impl->IIterable_iface;
    else
    {
        *out = NULL;
        return E_NOINTERFACE;
    }
    IUnknown_AddRef((IUnknown *)*out);
    return S_OK;
}

static ULONG WINAPI view_AddRef(IVectorView_AppInstallItem *iface)
{
    return InterlockedIncrement(&impl_from_IVectorView(iface)->ref);
}

static ULONG WINAPI view_Release(IVectorView_AppInstallItem *iface)
{
    struct empty_view *impl = impl_from_IVectorView(iface);
    ULONG ref = InterlockedDecrement(&impl->ref);

    if (!ref)
        free(impl);
    return ref;
}

static HRESULT WINAPI view_GetIids(IVectorView_AppInstallItem *iface, ULONG *count, IID **iids)
{
    return get_iids(&IID_IVectorView_AppInstallItem, count, iids);
}

static HRESULT WINAPI view_GetRuntimeClassName(IVectorView_AppInstallItem *iface, HSTRING *class_name)
{
    if (!class_name)
        return E_POINTER;
    *class_name = NULL;
    return S_OK;
}

static HRESULT WINAPI view_GetTrustLevel(IVectorView_AppInstallItem *iface, TrustLevel *level)
{
    return trust_level(level);
}

static HRESULT WINAPI view_GetAt(IVectorView_AppInstallItem *iface, UINT32 index, IAppInstallItem **value)
{
    if (!value)
        return E_POINTER;
    *value = NULL;
    return E_BOUNDS;
}

static HRESULT WINAPI view_get_Size(IVectorView_AppInstallItem *iface, UINT32 *value)
{
    if (!value)
        return E_POINTER;
    *value = 0;
    return S_OK;
}

static HRESULT WINAPI view_IndexOf(IVectorView_AppInstallItem *iface, IAppInstallItem *element, UINT32 *index,
                                   boolean *found)
{
    if (!index || !found)
        return E_POINTER;
    *index = 0;
    *found = FALSE;
    return S_OK;
}

static HRESULT WINAPI view_GetMany(IVectorView_AppInstallItem *iface, UINT32 start_index, UINT32 items_size,
                                   IAppInstallItem **items, UINT32 *count)
{
    if (!count)
        return E_POINTER;
    *count = 0;
    return start_index ? E_BOUNDS : S_OK;
}

static const IVectorView_AppInstallItemVtbl view_vtbl =
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

static inline struct empty_view *impl_from_IIterable(IIterable_AppInstallItem *iface)
{
    return CONTAINING_RECORD(iface, struct empty_view, IIterable_iface);
}

static HRESULT WINAPI iterable_QueryInterface(IIterable_AppInstallItem *iface, REFIID iid, void **out)
{
    return view_QueryInterface(&impl_from_IIterable(iface)->IVectorView_iface, iid, out);
}

static ULONG WINAPI iterable_AddRef(IIterable_AppInstallItem *iface)
{
    return view_AddRef(&impl_from_IIterable(iface)->IVectorView_iface);
}

static ULONG WINAPI iterable_Release(IIterable_AppInstallItem *iface)
{
    return view_Release(&impl_from_IIterable(iface)->IVectorView_iface);
}

static HRESULT WINAPI iterable_GetIids(IIterable_AppInstallItem *iface, ULONG *count, IID **iids)
{
    return get_iids(&IID_IIterable_AppInstallItem, count, iids);
}

static HRESULT WINAPI iterable_GetRuntimeClassName(IIterable_AppInstallItem *iface, HSTRING *class_name)
{
    return view_GetRuntimeClassName(NULL, class_name);
}

static HRESULT WINAPI iterable_GetTrustLevel(IIterable_AppInstallItem *iface, TrustLevel *level)
{
    return trust_level(level);
}

static HRESULT WINAPI iterable_First(IIterable_AppInstallItem *iface, IIterator_AppInstallItem **value)
{
    struct empty_view *impl = impl_from_IIterable(iface);

    if (!value)
        return E_POINTER;
    view_AddRef(&impl->IVectorView_iface);
    *value = &impl->IIterator_iface;
    return S_OK;
}

static const IIterable_AppInstallItemVtbl iterable_vtbl =
{
    iterable_QueryInterface,
    iterable_AddRef,
    iterable_Release,
    iterable_GetIids,
    iterable_GetRuntimeClassName,
    iterable_GetTrustLevel,
    iterable_First,
};

static inline struct empty_view *impl_from_IIterator(IIterator_AppInstallItem *iface)
{
    return CONTAINING_RECORD(iface, struct empty_view, IIterator_iface);
}

static HRESULT WINAPI iterator_QueryInterface(IIterator_AppInstallItem *iface, REFIID iid, void **out)
{
    if (!out)
        return E_POINTER;
    if (IsEqualGUID(iid, &IID_IUnknown) || IsEqualGUID(iid, &IID_IInspectable) ||
        IsEqualGUID(iid, &IID_IAgileObject) || IsEqualGUID(iid, &IID_IIterator_AppInstallItem))
    {
        *out = iface;
        IIterator_AppInstallItem_AddRef(iface);
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI iterator_AddRef(IIterator_AppInstallItem *iface)
{
    return view_AddRef(&impl_from_IIterator(iface)->IVectorView_iface);
}

static ULONG WINAPI iterator_Release(IIterator_AppInstallItem *iface)
{
    return view_Release(&impl_from_IIterator(iface)->IVectorView_iface);
}

static HRESULT WINAPI iterator_GetIids(IIterator_AppInstallItem *iface, ULONG *count, IID **iids)
{
    return get_iids(&IID_IIterator_AppInstallItem, count, iids);
}

static HRESULT WINAPI iterator_GetRuntimeClassName(IIterator_AppInstallItem *iface, HSTRING *class_name)
{
    return view_GetRuntimeClassName(NULL, class_name);
}

static HRESULT WINAPI iterator_GetTrustLevel(IIterator_AppInstallItem *iface, TrustLevel *level)
{
    return trust_level(level);
}

static HRESULT WINAPI iterator_get_Current(IIterator_AppInstallItem *iface, IAppInstallItem **value)
{
    if (!value)
        return E_POINTER;
    *value = NULL;
    return E_BOUNDS;
}

static HRESULT WINAPI iterator_get_HasCurrent(IIterator_AppInstallItem *iface, boolean *value)
{
    if (!value)
        return E_POINTER;
    *value = FALSE;
    return S_OK;
}

static HRESULT WINAPI iterator_MoveNext(IIterator_AppInstallItem *iface, boolean *value)
{
    if (!value)
        return E_POINTER;
    *value = FALSE;
    return S_OK;
}

static HRESULT WINAPI iterator_GetMany(IIterator_AppInstallItem *iface, UINT32 items_size, IAppInstallItem **items,
                                       UINT32 *count)
{
    if (!count)
        return E_POINTER;
    *count = 0;
    return S_OK;
}

static const IIterator_AppInstallItemVtbl iterator_vtbl =
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

HRESULT empty_install_items(IVectorView_AppInstallItem **out)
{
    struct empty_view *impl;

    if (!out)
        return E_POINTER;
    if (!(impl = calloc(1, sizeof(*impl))))
        return E_OUTOFMEMORY;
    impl->IVectorView_iface.lpVtbl = (IVectorView_AppInstallItemVtbl *)&view_vtbl;
    impl->IIterable_iface.lpVtbl = (IIterable_AppInstallItemVtbl *)&iterable_vtbl;
    impl->IIterator_iface.lpVtbl = (IIterator_AppInstallItemVtbl *)&iterator_vtbl;
    impl->ref = 1;
    *out = &impl->IVectorView_iface;
    return S_OK;
}
