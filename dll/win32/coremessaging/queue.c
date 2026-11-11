/*
 * PROJECT:     LiberNT Windows Runtime
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Windows.System.DispatcherQueue and DispatcherQueueController
 * COPYRIGHT:   Copyright 2026 Ostap Brehin <osbre@protonmail.com>
 */

#include <assert.h>

#include "private.h"

WINE_DEFAULT_DEBUG_CHANNEL(coremessaging);

#define WM_DISPATCHER_QUEUE_WORK (WM_USER + 0)
#define WM_DISPATCHER_QUEUE_SHUTDOWN (WM_USER + 1)

enum queue_state
{
    QUEUE_RUNNING,
    QUEUE_SHUT_DOWN,
};

enum queue_priority
{
    QUEUE_PRIORITY_HIGH,
    QUEUE_PRIORITY_NORMAL,
    QUEUE_PRIORITY_LOW,
    QUEUE_PRIORITY_COUNT,
};

struct queue_item
{
    struct list entry;
    IDispatcherQueueHandler *handler;
};

struct event_handler
{
    struct list entry;
    EventRegistrationToken token;
    IUnknown *handler;
};

struct shutdown_action
{
    IAsyncAction IAsyncAction_iface;
    IAsyncInfo IAsyncInfo_iface;
    struct weak_reference_source weak_reference_source;
    IUnknown *marshal;
    CRITICAL_SECTION cs;
    IAsyncActionCompletedHandler *handler;
    BOOL handler_set;
    AsyncStatus status;
};

struct dispatcher_queue
{
    IDispatcherQueue IDispatcherQueue_iface;
    IDispatcherQueue2 IDispatcherQueue2_iface;
    struct weak_reference_source weak_reference_source;
    IUnknown *marshal;
    CRITICAL_SECTION cs;
    struct list items[QUEUE_PRIORITY_COUNT];
    struct list starting_handlers;
    struct list completed_handlers;
    LONGLONG next_token;
    enum queue_state state;
    BOOL work_posted;
    BOOL shutdown_requested;
    BOOL dedicated;
    DWORD thread_id;
    HWND window;
    struct shutdown_action *shutdown;
};

struct dispatcher_queue_controller
{
    IDispatcherQueueController IDispatcherQueueController_iface;
    struct weak_reference_source weak_reference_source;
    IUnknown *marshal;
    struct dispatcher_queue *queue;
};

struct shutdown_starting_args
{
    IDispatcherQueueShutdownStartingEventArgs IDispatcherQueueShutdownStartingEventArgs_iface;
    LONG ref;
};

struct dedicated_thread_params
{
    DISPATCHERQUEUE_THREAD_APARTMENTTYPE apartment;
    HANDLE ready;
    HRESULT hr;
    struct dispatcher_queue *queue;
};

static const WCHAR queue_window_class[] = L"CoreMessagingDispatcherQueue";
static HINSTANCE coremessaging_instance;
static DWORD queue_tls = TLS_OUT_OF_INDEXES;
static INIT_ONCE queue_class_once = INIT_ONCE_STATIC_INIT;
static ATOM queue_class_atom;

static void queue_add_ref(struct dispatcher_queue *queue);
static ULONG queue_release(struct dispatcher_queue *queue);

static HRESULT add_handler(struct dispatcher_queue *queue, struct list *list, IUnknown *handler,
                           EventRegistrationToken *token)
{
    struct event_handler *entry;

    if (!handler || !token) return E_POINTER;
    if (!(entry = calloc(1, sizeof(*entry)))) return E_OUTOFMEMORY;

    IUnknown_AddRef(handler);
    entry->handler = handler;
    EnterCriticalSection(&queue->cs);
    entry->token.value = ++queue->next_token;
    list_add_tail(list, &entry->entry);
    LeaveCriticalSection(&queue->cs);
    *token = entry->token;
    return S_OK;
}

static HRESULT remove_handler(struct dispatcher_queue *queue, struct list *list, EventRegistrationToken token)
{
    struct event_handler *entry, *found = NULL;

    EnterCriticalSection(&queue->cs);
    LIST_FOR_EACH_ENTRY(entry, list, struct event_handler, entry)
    {
        if (entry->token.value == token.value)
        {
            list_remove(&entry->entry);
            found = entry;
            break;
        }
    }
    LeaveCriticalSection(&queue->cs);

    if (found)
    {
        IUnknown_Release(found->handler);
        free(found);
    }
    return S_OK;
}

static void free_handlers(struct list *list)
{
    struct event_handler *entry, *next;

    LIST_FOR_EACH_ENTRY_SAFE(entry, next, list, struct event_handler, entry)
    {
        list_remove(&entry->entry);
        IUnknown_Release(entry->handler);
        free(entry);
    }
}

static IUnknown **snapshot_handlers(struct dispatcher_queue *queue, struct list *list, UINT *count)
{
    struct event_handler *entry;
    IUnknown **handlers;
    UINT i = 0;

    EnterCriticalSection(&queue->cs);
    *count = list_count(list);
    if ((handlers = calloc(*count ? *count : 1, sizeof(*handlers))))
    {
        LIST_FOR_EACH_ENTRY(entry, list, struct event_handler, entry)
        {
            IUnknown_AddRef(entry->handler);
            handlers[i++] = entry->handler;
        }
    }
    else
    {
        *count = 0;
    }
    LeaveCriticalSection(&queue->cs);
    return handlers;
}

static inline struct shutdown_action *impl_from_IAsyncAction(IAsyncAction *iface)
{
    return CONTAINING_RECORD(iface, struct shutdown_action, IAsyncAction_iface);
}

static HRESULT WINAPI action_QueryInterface(IAsyncAction *iface, REFIID iid, void **out)
{
    struct shutdown_action *impl = impl_from_IAsyncAction(iface);

    TRACE("iface %p, iid %s, out %p.\n", iface, debugstr_guid(iid), out);

    if (!out) return E_POINTER;
    if (IsEqualGUID(iid, &IID_IUnknown) || IsEqualGUID(iid, &IID_IInspectable) ||
        IsEqualGUID(iid, &IID_IAgileObject) || IsEqualGUID(iid, &IID_IAsyncAction))
        *out = &impl->IAsyncAction_iface;
    else if (IsEqualGUID(iid, &IID_IAsyncInfo))
        *out = &impl->IAsyncInfo_iface;
    else if (IsEqualGUID(iid, &IID_IWeakReferenceSource))
        *out = &impl->weak_reference_source.IWeakReferenceSource_iface;
    else if (IsEqualGUID(iid, &IID_IMarshal))
        return IUnknown_QueryInterface(impl->marshal, iid, out);
    else
    {
        *out = NULL;
        WARN("%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid(iid));
        return E_NOINTERFACE;
    }
    IInspectable_AddRef((IInspectable *)*out);
    return S_OK;
}

static ULONG WINAPI action_AddRef(IAsyncAction *iface)
{
    struct shutdown_action *impl = impl_from_IAsyncAction(iface);
    ULONG ref = weak_reference_strong_add_ref(&impl->weak_reference_source);

    TRACE("iface %p, ref %lu.\n", iface, ref);
    return ref;
}

static ULONG WINAPI action_Release(IAsyncAction *iface)
{
    struct shutdown_action *impl = impl_from_IAsyncAction(iface);
    ULONG ref = weak_reference_strong_release(&impl->weak_reference_source);

    TRACE("iface %p, ref %lu.\n", iface, ref);

    if (!ref)
    {
        if (impl->handler) IAsyncActionCompletedHandler_Release(impl->handler);
        IUnknown_Release(impl->marshal);
        DeleteCriticalSection(&impl->cs);
        free(impl);
    }
    return ref;
}

static HRESULT WINAPI action_GetIids(IAsyncAction *iface, ULONG *iid_count, IID **iids)
{
    FIXME("iface %p, iid_count %p, iids %p stub!\n", iface, iid_count, iids);
    return E_NOTIMPL;
}

static HRESULT WINAPI action_GetRuntimeClassName(IAsyncAction *iface, HSTRING *class_name)
{
    static const WCHAR name[] = L"Windows.Foundation.IAsyncAction";

    if (!class_name) return E_POINTER;
    return WindowsCreateString(name, ARRAY_SIZE(name) - 1, class_name);
}

static HRESULT WINAPI action_GetTrustLevel(IAsyncAction *iface, TrustLevel *trust_level)
{
    if (!trust_level) return E_POINTER;
    *trust_level = BaseTrust;
    return S_OK;
}

static HRESULT WINAPI action_put_Completed(IAsyncAction *iface, IAsyncActionCompletedHandler *handler)
{
    struct shutdown_action *impl = impl_from_IAsyncAction(iface);
    AsyncStatus status;

    TRACE("iface %p, handler %p.\n", iface, handler);

    if (!handler) return E_POINTER;

    EnterCriticalSection(&impl->cs);
    if (impl->handler_set)
    {
        LeaveCriticalSection(&impl->cs);
        return E_ILLEGAL_DELEGATE_ASSIGNMENT;
    }
    impl->handler_set = TRUE;
    status = impl->status;
    if (status == Started)
    {
        IAsyncActionCompletedHandler_AddRef(handler);
        impl->handler = handler;
    }
    LeaveCriticalSection(&impl->cs);

    if (status != Started)
        IAsyncActionCompletedHandler_Invoke(handler, iface, status);
    return S_OK;
}

static HRESULT WINAPI action_get_Completed(IAsyncAction *iface, IAsyncActionCompletedHandler **handler)
{
    struct shutdown_action *impl = impl_from_IAsyncAction(iface);

    TRACE("iface %p, handler %p.\n", iface, handler);

    if (!handler) return E_POINTER;
    EnterCriticalSection(&impl->cs);
    if ((*handler = impl->handler)) IAsyncActionCompletedHandler_AddRef(*handler);
    LeaveCriticalSection(&impl->cs);
    return S_OK;
}

static HRESULT WINAPI action_GetResults(IAsyncAction *iface)
{
    struct shutdown_action *impl = impl_from_IAsyncAction(iface);
    HRESULT hr;

    TRACE("iface %p.\n", iface);

    EnterCriticalSection(&impl->cs);
    hr = impl->status == Completed ? S_OK : E_ILLEGAL_METHOD_CALL;
    LeaveCriticalSection(&impl->cs);
    return hr;
}

static const IAsyncActionVtbl action_vtbl =
{
    action_QueryInterface,
    action_AddRef,
    action_Release,
    action_GetIids,
    action_GetRuntimeClassName,
    action_GetTrustLevel,
    action_put_Completed,
    action_get_Completed,
    action_GetResults,
};

static inline struct shutdown_action *impl_from_IAsyncInfo(IAsyncInfo *iface)
{
    return CONTAINING_RECORD(iface, struct shutdown_action, IAsyncInfo_iface);
}

static HRESULT WINAPI action_info_QueryInterface(IAsyncInfo *iface, REFIID iid, void **out)
{
    return action_QueryInterface(&impl_from_IAsyncInfo(iface)->IAsyncAction_iface, iid, out);
}

static ULONG WINAPI action_info_AddRef(IAsyncInfo *iface)
{
    return action_AddRef(&impl_from_IAsyncInfo(iface)->IAsyncAction_iface);
}

static ULONG WINAPI action_info_Release(IAsyncInfo *iface)
{
    return action_Release(&impl_from_IAsyncInfo(iface)->IAsyncAction_iface);
}

static HRESULT WINAPI action_info_GetIids(IAsyncInfo *iface, ULONG *iid_count, IID **iids)
{
    return action_GetIids(&impl_from_IAsyncInfo(iface)->IAsyncAction_iface, iid_count, iids);
}

static HRESULT WINAPI action_info_GetRuntimeClassName(IAsyncInfo *iface, HSTRING *class_name)
{
    return action_GetRuntimeClassName(&impl_from_IAsyncInfo(iface)->IAsyncAction_iface, class_name);
}

static HRESULT WINAPI action_info_GetTrustLevel(IAsyncInfo *iface, TrustLevel *trust_level)
{
    return action_GetTrustLevel(&impl_from_IAsyncInfo(iface)->IAsyncAction_iface, trust_level);
}

static HRESULT WINAPI action_info_get_Id(IAsyncInfo *iface, UINT32 *id)
{
    if (!id) return E_POINTER;
    *id = 1;
    return S_OK;
}

static HRESULT WINAPI action_info_get_Status(IAsyncInfo *iface, AsyncStatus *status)
{
    struct shutdown_action *impl = impl_from_IAsyncInfo(iface);

    if (!status) return E_POINTER;
    EnterCriticalSection(&impl->cs);
    *status = impl->status;
    LeaveCriticalSection(&impl->cs);
    return S_OK;
}

static HRESULT WINAPI action_info_get_ErrorCode(IAsyncInfo *iface, HRESULT *error_code)
{
    if (!error_code) return E_POINTER;
    *error_code = S_OK;
    return S_OK;
}

static HRESULT WINAPI action_info_Cancel(IAsyncInfo *iface)
{
    FIXME("iface %p stub!\n", iface);
    return S_OK;
}

static HRESULT WINAPI action_info_Close(IAsyncInfo *iface)
{
    FIXME("iface %p stub!\n", iface);
    return S_OK;
}

static const IAsyncInfoVtbl action_info_vtbl =
{
    action_info_QueryInterface,
    action_info_AddRef,
    action_info_Release,
    action_info_GetIids,
    action_info_GetRuntimeClassName,
    action_info_GetTrustLevel,
    action_info_get_Id,
    action_info_get_Status,
    action_info_get_ErrorCode,
    action_info_Cancel,
    action_info_Close,
};

static HRESULT shutdown_action_create(struct shutdown_action **out)
{
    struct shutdown_action *impl;
    HRESULT hr;

    if (!(impl = calloc(1, sizeof(*impl)))) return E_OUTOFMEMORY;
    impl->IAsyncAction_iface.lpVtbl = &action_vtbl;
    impl->IAsyncInfo_iface.lpVtbl = &action_info_vtbl;
    impl->status = Started;

    if (FAILED(hr = weak_reference_source_init(&impl->weak_reference_source,
                                               (IUnknown *)&impl->IAsyncAction_iface)))
    {
        free(impl);
        return hr;
    }
    if (FAILED(hr = CoCreateFreeThreadedMarshaler((IUnknown *)&impl->IAsyncAction_iface, &impl->marshal)))
    {
        weak_reference_strong_release(&impl->weak_reference_source);
        free(impl);
        return hr;
    }
    InitializeCriticalSection(&impl->cs);
    *out = impl;
    return S_OK;
}

static void shutdown_action_complete(struct shutdown_action *impl)
{
    IAsyncActionCompletedHandler *handler;

    EnterCriticalSection(&impl->cs);
    impl->status = Completed;
    handler = impl->handler;
    impl->handler = NULL;
    LeaveCriticalSection(&impl->cs);

    if (handler)
    {
        IAsyncActionCompletedHandler_Invoke(handler, &impl->IAsyncAction_iface, Completed);
        IAsyncActionCompletedHandler_Release(handler);
    }
}

static inline struct shutdown_starting_args *impl_from_IDispatcherQueueShutdownStartingEventArgs(
    IDispatcherQueueShutdownStartingEventArgs *iface)
{
    return CONTAINING_RECORD(iface, struct shutdown_starting_args, IDispatcherQueueShutdownStartingEventArgs_iface);
}

static HRESULT WINAPI args_QueryInterface(IDispatcherQueueShutdownStartingEventArgs *iface, REFIID iid, void **out)
{
    TRACE("iface %p, iid %s, out %p.\n", iface, debugstr_guid(iid), out);

    if (!out) return E_POINTER;
    if (IsEqualGUID(iid, &IID_IUnknown) || IsEqualGUID(iid, &IID_IInspectable) ||
        IsEqualGUID(iid, &IID_IAgileObject) || IsEqualGUID(iid, &IID_IDispatcherQueueShutdownStartingEventArgs))
    {
        *out = iface;
        IInspectable_AddRef((IInspectable *)*out);
        return S_OK;
    }
    *out = NULL;
    WARN("%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid(iid));
    return E_NOINTERFACE;
}

static ULONG WINAPI args_AddRef(IDispatcherQueueShutdownStartingEventArgs *iface)
{
    return InterlockedIncrement(&impl_from_IDispatcherQueueShutdownStartingEventArgs(iface)->ref);
}

static ULONG WINAPI args_Release(IDispatcherQueueShutdownStartingEventArgs *iface)
{
    struct shutdown_starting_args *impl = impl_from_IDispatcherQueueShutdownStartingEventArgs(iface);
    ULONG ref = InterlockedDecrement(&impl->ref);

    if (!ref) free(impl);
    return ref;
}

static HRESULT WINAPI args_GetIids(IDispatcherQueueShutdownStartingEventArgs *iface, ULONG *iid_count, IID **iids)
{
    FIXME("iface %p, iid_count %p, iids %p stub!\n", iface, iid_count, iids);
    return E_NOTIMPL;
}

static HRESULT WINAPI args_GetRuntimeClassName(IDispatcherQueueShutdownStartingEventArgs *iface,
                                               HSTRING *class_name)
{
    if (!class_name) return E_POINTER;
    return WindowsCreateString(RuntimeClass_Windows_System_DispatcherQueueShutdownStartingEventArgs,
                               wcslen(RuntimeClass_Windows_System_DispatcherQueueShutdownStartingEventArgs),
                               class_name);
}

static HRESULT WINAPI args_GetTrustLevel(IDispatcherQueueShutdownStartingEventArgs *iface, TrustLevel *trust_level)
{
    if (!trust_level) return E_POINTER;
    *trust_level = BaseTrust;
    return S_OK;
}

static HRESULT WINAPI args_GetDeferral(IDispatcherQueueShutdownStartingEventArgs *iface, IDeferral **result)
{
    /* TODO: deferrals that hold the shutdown until they complete */
    FIXME("iface %p, result %p stub!\n", iface, result);
    if (!result) return E_POINTER;
    *result = NULL;
    return E_NOTIMPL;
}

static const IDispatcherQueueShutdownStartingEventArgsVtbl args_vtbl =
{
    args_QueryInterface,
    args_AddRef,
    args_Release,
    args_GetIids,
    args_GetRuntimeClassName,
    args_GetTrustLevel,
    args_GetDeferral,
};

static void queue_process_items(struct dispatcher_queue *queue)
{
    struct queue_item *item;
    struct list *entry;
    HRESULT hr;
    UINT i;

    assert(GetCurrentThreadId() == queue->thread_id);

    for (;;)
    {
        entry = NULL;
        EnterCriticalSection(&queue->cs);
        for (i = 0; i < QUEUE_PRIORITY_COUNT && !entry; i++)
            if ((entry = list_head(&queue->items[i]))) list_remove(entry);
        if (!entry) queue->work_posted = FALSE;
        LeaveCriticalSection(&queue->cs);
        if (!entry) break;

        item = LIST_ENTRY(entry, struct queue_item, entry);
        if (FAILED(hr = IDispatcherQueueHandler_Invoke(item->handler)))
            WARN("handler %p returned %#lx.\n", item->handler, hr);
        IDispatcherQueueHandler_Release(item->handler);
        free(item);
    }
}

static void queue_shutdown(struct dispatcher_queue *queue)
{
    struct shutdown_starting_args *args;
    struct shutdown_action *action;
    IUnknown **handlers;
    UINT count, i;
    HWND window;

    assert(GetCurrentThreadId() == queue->thread_id);
    assert(queue->state == QUEUE_RUNNING);
    assert(TlsGetValue(queue_tls) == queue);

    handlers = snapshot_handlers(queue, &queue->starting_handlers, &count);
    if (count && (args = calloc(1, sizeof(*args))))
    {
        args->IDispatcherQueueShutdownStartingEventArgs_iface.lpVtbl = &args_vtbl;
        args->ref = 1;
        for (i = 0; i < count; i++)
            ITypedEventHandler_DispatcherQueue_DispatcherQueueShutdownStartingEventArgs_Invoke(
                (void *)handlers[i], &queue->IDispatcherQueue_iface,
                &args->IDispatcherQueueShutdownStartingEventArgs_iface);
        IDispatcherQueueShutdownStartingEventArgs_Release(&args->IDispatcherQueueShutdownStartingEventArgs_iface);
    }
    for (i = 0; i < count; i++) IUnknown_Release(handlers[i]);
    free(handlers);

    EnterCriticalSection(&queue->cs);
    queue->state = QUEUE_SHUT_DOWN;
    LeaveCriticalSection(&queue->cs);
    queue_process_items(queue);

    TlsSetValue(queue_tls, NULL);
    EnterCriticalSection(&queue->cs);
    window = queue->window;
    queue->window = NULL;
    LeaveCriticalSection(&queue->cs);
    DestroyWindow(window);

    handlers = snapshot_handlers(queue, &queue->completed_handlers, &count);
    for (i = 0; i < count; i++)
    {
        ITypedEventHandler_DispatcherQueue_IInspectable_Invoke((void *)handlers[i], &queue->IDispatcherQueue_iface,
                                                               NULL);
        IUnknown_Release(handlers[i]);
    }
    free(handlers);

    EnterCriticalSection(&queue->cs);
    action = queue->shutdown;
    queue->shutdown = NULL;
    LeaveCriticalSection(&queue->cs);
    assert(action != NULL);

    shutdown_action_complete(action);
    IAsyncAction_Release(&action->IAsyncAction_iface);

    if (queue->dedicated) PostQuitMessage(0);
    queue_release(queue);
}

static LRESULT CALLBACK queue_window_proc(HWND window, UINT msg, WPARAM wparam, LPARAM lparam)
{
    struct dispatcher_queue *queue = (struct dispatcher_queue *)GetWindowLongPtrW(window, GWLP_USERDATA);

    switch (msg)
    {
    case WM_DISPATCHER_QUEUE_WORK:
        assert(queue != NULL);
        queue_process_items(queue);
        return 0;
    case WM_DISPATCHER_QUEUE_SHUTDOWN:
        assert(queue != NULL);
        queue_shutdown(queue);
        return 0;
    }
    return DefWindowProcW(window, msg, wparam, lparam);
}

static BOOL WINAPI register_queue_class(INIT_ONCE *once, void *param, void **context)
{
    WNDCLASSEXW class = {sizeof(class)};

    class.lpfnWndProc = queue_window_proc;
    class.hInstance = coremessaging_instance;
    class.lpszClassName = queue_window_class;
    queue_class_atom = RegisterClassExW(&class);
    return queue_class_atom != 0;
}

static inline struct dispatcher_queue *impl_from_IDispatcherQueue(IDispatcherQueue *iface)
{
    return CONTAINING_RECORD(iface, struct dispatcher_queue, IDispatcherQueue_iface);
}

static HRESULT WINAPI queue_QueryInterface(IDispatcherQueue *iface, REFIID iid, void **out)
{
    struct dispatcher_queue *impl = impl_from_IDispatcherQueue(iface);

    TRACE("iface %p, iid %s, out %p.\n", iface, debugstr_guid(iid), out);

    if (!out) return E_POINTER;
    if (IsEqualGUID(iid, &IID_IUnknown) || IsEqualGUID(iid, &IID_IInspectable) ||
        IsEqualGUID(iid, &IID_IAgileObject) || IsEqualGUID(iid, &IID_IDispatcherQueue))
        *out = &impl->IDispatcherQueue_iface;
    else if (IsEqualGUID(iid, &IID_IDispatcherQueue2))
        *out = &impl->IDispatcherQueue2_iface;
    /* TODO: IDispatcherQueue3 (EnsureSystemDispatcherQueue) */
    else if (IsEqualGUID(iid, &IID_IWeakReferenceSource))
        *out = &impl->weak_reference_source.IWeakReferenceSource_iface;
    else if (IsEqualGUID(iid, &IID_IMarshal))
        return IUnknown_QueryInterface(impl->marshal, iid, out);
    else
    {
        *out = NULL;
        WARN("%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid(iid));
        return E_NOINTERFACE;
    }
    IInspectable_AddRef((IInspectable *)*out);
    return S_OK;
}

static void queue_add_ref(struct dispatcher_queue *queue)
{
    weak_reference_strong_add_ref(&queue->weak_reference_source);
}

static ULONG queue_release(struct dispatcher_queue *queue)
{
    struct queue_item *item, *next;
    ULONG ref;
    UINT i;

    if ((ref = weak_reference_strong_release(&queue->weak_reference_source))) return ref;

    assert(!queue->window);
    assert(!queue->shutdown);
    for (i = 0; i < QUEUE_PRIORITY_COUNT; i++)
    {
        LIST_FOR_EACH_ENTRY_SAFE(item, next, &queue->items[i], struct queue_item, entry)
        {
            list_remove(&item->entry);
            IDispatcherQueueHandler_Release(item->handler);
            free(item);
        }
    }
    free_handlers(&queue->starting_handlers);
    free_handlers(&queue->completed_handlers);
    IUnknown_Release(queue->marshal);
    DeleteCriticalSection(&queue->cs);
    free(queue);
    return 0;
}

static ULONG WINAPI queue_AddRef(IDispatcherQueue *iface)
{
    struct dispatcher_queue *impl = impl_from_IDispatcherQueue(iface);
    ULONG ref = weak_reference_strong_add_ref(&impl->weak_reference_source);

    TRACE("iface %p, ref %lu.\n", iface, ref);
    return ref;
}

static ULONG WINAPI queue_Release(IDispatcherQueue *iface)
{
    struct dispatcher_queue *impl = impl_from_IDispatcherQueue(iface);
    ULONG ref = queue_release(impl);

    TRACE("iface %p, ref %lu.\n", iface, ref);
    return ref;
}

static HRESULT WINAPI queue_GetIids(IDispatcherQueue *iface, ULONG *iid_count, IID **iids)
{
    FIXME("iface %p, iid_count %p, iids %p stub!\n", iface, iid_count, iids);
    return E_NOTIMPL;
}

static HRESULT WINAPI queue_GetRuntimeClassName(IDispatcherQueue *iface, HSTRING *class_name)
{
    if (!class_name) return E_POINTER;
    return WindowsCreateString(RuntimeClass_Windows_System_DispatcherQueue,
                               wcslen(RuntimeClass_Windows_System_DispatcherQueue), class_name);
}

static HRESULT WINAPI queue_GetTrustLevel(IDispatcherQueue *iface, TrustLevel *trust_level)
{
    if (!trust_level) return E_POINTER;
    *trust_level = BaseTrust;
    return S_OK;
}

static HRESULT WINAPI queue_CreateTimer(IDispatcherQueue *iface, IDispatcherQueueTimer **result)
{
    /* TODO: DispatcherQueueTimer */
    FIXME("iface %p, result %p stub!\n", iface, result);
    if (!result) return E_POINTER;
    *result = NULL;
    return E_NOTIMPL;
}

static HRESULT WINAPI queue_TryEnqueueWithPriority(IDispatcherQueue *iface, DispatcherQueuePriority priority,
                                                   IDispatcherQueueHandler *callback, boolean *result)
{
    struct dispatcher_queue *impl = impl_from_IDispatcherQueue(iface);
    struct queue_item *item;
    enum queue_priority index;
    HWND window = NULL;

    TRACE("iface %p, priority %d, callback %p, result %p.\n", iface, priority, callback, result);

    if (!callback || !result) return E_POINTER;

    switch (priority)
    {
    case DispatcherQueuePriority_High: index = QUEUE_PRIORITY_HIGH; break;
    case DispatcherQueuePriority_Normal: index = QUEUE_PRIORITY_NORMAL; break;
    case DispatcherQueuePriority_Low: index = QUEUE_PRIORITY_LOW; break;
    default: return E_INVALIDARG;
    }

    if (!(item = calloc(1, sizeof(*item)))) return E_OUTOFMEMORY;
    IDispatcherQueueHandler_AddRef(callback);
    item->handler = callback;

    EnterCriticalSection(&impl->cs);
    if (impl->state != QUEUE_RUNNING)
    {
        LeaveCriticalSection(&impl->cs);
        IDispatcherQueueHandler_Release(callback);
        free(item);
        *result = FALSE;
        return S_OK;
    }
    list_add_tail(&impl->items[index], &item->entry);
    if (!impl->work_posted)
    {
        impl->work_posted = TRUE;
        window = impl->window;
    }
    LeaveCriticalSection(&impl->cs);

    if (window && !PostMessageW(window, WM_DISPATCHER_QUEUE_WORK, 0, 0))
        WARN("PostMessageW failed, error %lu.\n", GetLastError());
    *result = TRUE;
    return S_OK;
}

static HRESULT WINAPI queue_TryEnqueue(IDispatcherQueue *iface, IDispatcherQueueHandler *callback, boolean *result)
{
    return queue_TryEnqueueWithPriority(iface, DispatcherQueuePriority_Normal, callback, result);
}

static HRESULT WINAPI queue_add_ShutdownStarting(IDispatcherQueue *iface,
    ITypedEventHandler_DispatcherQueue_DispatcherQueueShutdownStartingEventArgs *handler,
    EventRegistrationToken *token)
{
    struct dispatcher_queue *impl = impl_from_IDispatcherQueue(iface);

    TRACE("iface %p, handler %p, token %p.\n", iface, handler, token);
    return add_handler(impl, &impl->starting_handlers, (IUnknown *)handler, token);
}

static HRESULT WINAPI queue_remove_ShutdownStarting(IDispatcherQueue *iface, EventRegistrationToken token)
{
    struct dispatcher_queue *impl = impl_from_IDispatcherQueue(iface);

    TRACE("iface %p, token %s.\n", iface, wine_dbgstr_longlong(token.value));
    return remove_handler(impl, &impl->starting_handlers, token);
}

static HRESULT WINAPI queue_add_ShutdownCompleted(IDispatcherQueue *iface,
    ITypedEventHandler_DispatcherQueue_IInspectable *handler, EventRegistrationToken *token)
{
    struct dispatcher_queue *impl = impl_from_IDispatcherQueue(iface);

    TRACE("iface %p, handler %p, token %p.\n", iface, handler, token);
    return add_handler(impl, &impl->completed_handlers, (IUnknown *)handler, token);
}

static HRESULT WINAPI queue_remove_ShutdownCompleted(IDispatcherQueue *iface, EventRegistrationToken token)
{
    struct dispatcher_queue *impl = impl_from_IDispatcherQueue(iface);

    TRACE("iface %p, token %s.\n", iface, wine_dbgstr_longlong(token.value));
    return remove_handler(impl, &impl->completed_handlers, token);
}

static const IDispatcherQueueVtbl queue_vtbl =
{
    queue_QueryInterface,
    queue_AddRef,
    queue_Release,
    queue_GetIids,
    queue_GetRuntimeClassName,
    queue_GetTrustLevel,
    queue_CreateTimer,
    queue_TryEnqueue,
    queue_TryEnqueueWithPriority,
    queue_add_ShutdownStarting,
    queue_remove_ShutdownStarting,
    queue_add_ShutdownCompleted,
    queue_remove_ShutdownCompleted,
};

static inline struct dispatcher_queue *impl_from_IDispatcherQueue2(IDispatcherQueue2 *iface)
{
    return CONTAINING_RECORD(iface, struct dispatcher_queue, IDispatcherQueue2_iface);
}

static HRESULT WINAPI queue2_QueryInterface(IDispatcherQueue2 *iface, REFIID iid, void **out)
{
    return queue_QueryInterface(&impl_from_IDispatcherQueue2(iface)->IDispatcherQueue_iface, iid, out);
}

static ULONG WINAPI queue2_AddRef(IDispatcherQueue2 *iface)
{
    return queue_AddRef(&impl_from_IDispatcherQueue2(iface)->IDispatcherQueue_iface);
}

static ULONG WINAPI queue2_Release(IDispatcherQueue2 *iface)
{
    return queue_Release(&impl_from_IDispatcherQueue2(iface)->IDispatcherQueue_iface);
}

static HRESULT WINAPI queue2_GetIids(IDispatcherQueue2 *iface, ULONG *iid_count, IID **iids)
{
    return queue_GetIids(&impl_from_IDispatcherQueue2(iface)->IDispatcherQueue_iface, iid_count, iids);
}

static HRESULT WINAPI queue2_GetRuntimeClassName(IDispatcherQueue2 *iface, HSTRING *class_name)
{
    return queue_GetRuntimeClassName(&impl_from_IDispatcherQueue2(iface)->IDispatcherQueue_iface, class_name);
}

static HRESULT WINAPI queue2_GetTrustLevel(IDispatcherQueue2 *iface, TrustLevel *trust_level)
{
    return queue_GetTrustLevel(&impl_from_IDispatcherQueue2(iface)->IDispatcherQueue_iface, trust_level);
}

static HRESULT WINAPI queue2_get_HasThreadAccess(IDispatcherQueue2 *iface, boolean *value)
{
    struct dispatcher_queue *impl = impl_from_IDispatcherQueue2(iface);

    TRACE("iface %p, value %p.\n", iface, value);

    if (!value) return E_POINTER;
    *value = GetCurrentThreadId() == impl->thread_id;
    return S_OK;
}

static const IDispatcherQueue2Vtbl queue2_vtbl =
{
    queue2_QueryInterface,
    queue2_AddRef,
    queue2_Release,
    queue2_GetIids,
    queue2_GetRuntimeClassName,
    queue2_GetTrustLevel,
    queue2_get_HasThreadAccess,
};

static HRESULT queue_create(BOOL dedicated, struct dispatcher_queue **out)
{
    struct dispatcher_queue *impl;
    HRESULT hr;
    UINT i;

    if (TlsGetValue(queue_tls)) return RPC_E_WRONG_THREAD;
    if (!InitOnceExecuteOnce(&queue_class_once, register_queue_class, NULL, NULL))
        return HRESULT_FROM_WIN32(GetLastError());

    if (!(impl = calloc(1, sizeof(*impl)))) return E_OUTOFMEMORY;
    impl->IDispatcherQueue_iface.lpVtbl = &queue_vtbl;
    impl->IDispatcherQueue2_iface.lpVtbl = &queue2_vtbl;
    for (i = 0; i < QUEUE_PRIORITY_COUNT; i++) list_init(&impl->items[i]);
    list_init(&impl->starting_handlers);
    list_init(&impl->completed_handlers);
    impl->state = QUEUE_RUNNING;
    impl->dedicated = dedicated;
    impl->thread_id = GetCurrentThreadId();

    if (FAILED(hr = weak_reference_source_init(&impl->weak_reference_source,
                                               (IUnknown *)&impl->IDispatcherQueue_iface)))
    {
        free(impl);
        return hr;
    }
    if (FAILED(hr = CoCreateFreeThreadedMarshaler((IUnknown *)&impl->IDispatcherQueue_iface, &impl->marshal)))
    {
        weak_reference_strong_release(&impl->weak_reference_source);
        free(impl);
        return hr;
    }
    InitializeCriticalSection(&impl->cs);

    impl->window = CreateWindowExW(0, queue_window_class, NULL, 0, 0, 0, 0, 0, HWND_MESSAGE, NULL,
                                   coremessaging_instance, NULL);
    if (!impl->window)
    {
        hr = HRESULT_FROM_WIN32(GetLastError());
        queue_release(impl);
        return hr;
    }
    SetWindowLongPtrW(impl->window, GWLP_USERDATA, (LONG_PTR)impl);

    TlsSetValue(queue_tls, impl);
    *out = impl;
    return S_OK;
}

static HRESULT queue_begin_shutdown(struct dispatcher_queue *queue, IAsyncAction **operation)
{
    struct shutdown_action *action;
    HRESULT hr;

    if (FAILED(hr = shutdown_action_create(&action))) return hr;

    EnterCriticalSection(&queue->cs);
    if (queue->shutdown_requested)
    {
        LeaveCriticalSection(&queue->cs);
        IAsyncAction_Release(&action->IAsyncAction_iface);
        return E_UNEXPECTED;
    }
    queue->shutdown_requested = TRUE;
    IAsyncAction_AddRef(&action->IAsyncAction_iface);
    queue->shutdown = action;
    LeaveCriticalSection(&queue->cs);

    if (!PostMessageW(queue->window, WM_DISPATCHER_QUEUE_SHUTDOWN, 0, 0))
        WARN("PostMessageW failed, error %lu.\n", GetLastError());
    *operation = &action->IAsyncAction_iface;
    return S_OK;
}

static inline struct dispatcher_queue_controller *impl_from_IDispatcherQueueController(
    IDispatcherQueueController *iface)
{
    return CONTAINING_RECORD(iface, struct dispatcher_queue_controller, IDispatcherQueueController_iface);
}

static HRESULT WINAPI controller_QueryInterface(IDispatcherQueueController *iface, REFIID iid, void **out)
{
    struct dispatcher_queue_controller *impl = impl_from_IDispatcherQueueController(iface);

    TRACE("iface %p, iid %s, out %p.\n", iface, debugstr_guid(iid), out);

    if (!out) return E_POINTER;
    if (IsEqualGUID(iid, &IID_IUnknown) || IsEqualGUID(iid, &IID_IInspectable) ||
        IsEqualGUID(iid, &IID_IAgileObject) || IsEqualGUID(iid, &IID_IDispatcherQueueController))
        *out = &impl->IDispatcherQueueController_iface;
    else if (IsEqualGUID(iid, &IID_IWeakReferenceSource))
        *out = &impl->weak_reference_source.IWeakReferenceSource_iface;
    else if (IsEqualGUID(iid, &IID_IMarshal))
        return IUnknown_QueryInterface(impl->marshal, iid, out);
    else
    {
        *out = NULL;
        WARN("%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid(iid));
        return E_NOINTERFACE;
    }
    IInspectable_AddRef((IInspectable *)*out);
    return S_OK;
}

static ULONG WINAPI controller_AddRef(IDispatcherQueueController *iface)
{
    struct dispatcher_queue_controller *impl = impl_from_IDispatcherQueueController(iface);
    ULONG ref = weak_reference_strong_add_ref(&impl->weak_reference_source);

    TRACE("iface %p, ref %lu.\n", iface, ref);
    return ref;
}

static ULONG WINAPI controller_Release(IDispatcherQueueController *iface)
{
    struct dispatcher_queue_controller *impl = impl_from_IDispatcherQueueController(iface);
    ULONG ref = weak_reference_strong_release(&impl->weak_reference_source);

    TRACE("iface %p, ref %lu.\n", iface, ref);

    if (!ref)
    {
        queue_release(impl->queue);
        IUnknown_Release(impl->marshal);
        free(impl);
    }
    return ref;
}

static HRESULT WINAPI controller_GetIids(IDispatcherQueueController *iface, ULONG *iid_count, IID **iids)
{
    FIXME("iface %p, iid_count %p, iids %p stub!\n", iface, iid_count, iids);
    return E_NOTIMPL;
}

static HRESULT WINAPI controller_GetRuntimeClassName(IDispatcherQueueController *iface, HSTRING *class_name)
{
    if (!class_name) return E_POINTER;
    return WindowsCreateString(RuntimeClass_Windows_System_DispatcherQueueController,
                               wcslen(RuntimeClass_Windows_System_DispatcherQueueController), class_name);
}

static HRESULT WINAPI controller_GetTrustLevel(IDispatcherQueueController *iface, TrustLevel *trust_level)
{
    if (!trust_level) return E_POINTER;
    *trust_level = BaseTrust;
    return S_OK;
}

static HRESULT WINAPI controller_get_DispatcherQueue(IDispatcherQueueController *iface, IDispatcherQueue **value)
{
    struct dispatcher_queue_controller *impl = impl_from_IDispatcherQueueController(iface);

    TRACE("iface %p, value %p.\n", iface, value);

    if (!value) return E_POINTER;
    queue_add_ref(impl->queue);
    *value = &impl->queue->IDispatcherQueue_iface;
    return S_OK;
}

static HRESULT WINAPI controller_ShutdownQueueAsync(IDispatcherQueueController *iface, IAsyncAction **operation)
{
    struct dispatcher_queue_controller *impl = impl_from_IDispatcherQueueController(iface);

    TRACE("iface %p, operation %p.\n", iface, operation);

    if (!operation) return E_POINTER;
    return queue_begin_shutdown(impl->queue, operation);
}

static const IDispatcherQueueControllerVtbl controller_vtbl =
{
    controller_QueryInterface,
    controller_AddRef,
    controller_Release,
    controller_GetIids,
    controller_GetRuntimeClassName,
    controller_GetTrustLevel,
    controller_get_DispatcherQueue,
    controller_ShutdownQueueAsync,
};

static DWORD WINAPI dedicated_thread_proc(void *param)
{
    struct dedicated_thread_params *params = param;
    struct dispatcher_queue *queue = NULL;
    BOOL asta = params->apartment == DQTAT_COM_ASTA;
    HRESULT hr;
    MSG msg;

    hr = asta ? RoInitialize(RO_INIT_SINGLETHREADED) : CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    if (SUCCEEDED(hr) && FAILED(hr = queue_create(TRUE, &queue)))
    {
        if (asta) RoUninitialize();
        else CoUninitialize();
    }

    params->hr = hr;
    if (SUCCEEDED(hr))
    {
        queue_add_ref(queue);
        params->queue = queue;
    }
    SetEvent(params->ready);

    if (SUCCEEDED(hr))
    {
        while (GetMessageW(&msg, NULL, 0, 0) > 0)
        {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        if (asta) RoUninitialize();
        else CoUninitialize();
    }
    FreeLibraryAndExitThread(coremessaging_instance, 0);
}

static HRESULT create_dedicated_queue(DISPATCHERQUEUE_THREAD_APARTMENTTYPE apartment,
                                      struct dispatcher_queue **out)
{
    struct dedicated_thread_params params = {apartment};
    HMODULE module;
    HANDLE thread;

    if (!(params.ready = CreateEventW(NULL, TRUE, FALSE, NULL))) return HRESULT_FROM_WIN32(GetLastError());
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS, (const WCHAR *)coremessaging_instance,
                            &module))
    {
        CloseHandle(params.ready);
        return HRESULT_FROM_WIN32(GetLastError());
    }
    if (!(thread = CreateThread(NULL, 0, dedicated_thread_proc, &params, 0, NULL)))
    {
        params.hr = HRESULT_FROM_WIN32(GetLastError());
        FreeLibrary(module);
        CloseHandle(params.ready);
        return params.hr;
    }
    WaitForSingleObject(params.ready, INFINITE);
    CloseHandle(thread);
    CloseHandle(params.ready);

    if (SUCCEEDED(params.hr)) *out = params.queue;
    return params.hr;
}

HRESULT dispatcher_queue_controller_create(DISPATCHERQUEUE_THREAD_TYPE type,
                                           DISPATCHERQUEUE_THREAD_APARTMENTTYPE apartment,
                                           IDispatcherQueueController **out)
{
    struct dispatcher_queue_controller *impl;
    struct dispatcher_queue *queue;
    HRESULT hr;

    assert(type == DQTYPE_THREAD_CURRENT || type == DQTYPE_THREAD_DEDICATED);

    if (!(impl = calloc(1, sizeof(*impl)))) return E_OUTOFMEMORY;
    impl->IDispatcherQueueController_iface.lpVtbl = &controller_vtbl;

    if (FAILED(hr = weak_reference_source_init(&impl->weak_reference_source,
                                               (IUnknown *)&impl->IDispatcherQueueController_iface)))
    {
        free(impl);
        return hr;
    }
    if (FAILED(hr = CoCreateFreeThreadedMarshaler((IUnknown *)&impl->IDispatcherQueueController_iface,
                                                  &impl->marshal)))
    {
        weak_reference_strong_release(&impl->weak_reference_source);
        free(impl);
        return hr;
    }

    if (type == DQTYPE_THREAD_CURRENT)
    {
        if (SUCCEEDED(hr = queue_create(FALSE, &queue))) queue_add_ref(queue);
    }
    else
    {
        hr = create_dedicated_queue(apartment, &queue);
    }
    if (FAILED(hr))
    {
        IUnknown_Release(impl->marshal);
        weak_reference_strong_release(&impl->weak_reference_source);
        free(impl);
        return hr;
    }

    impl->queue = queue;
    *out = &impl->IDispatcherQueueController_iface;
    TRACE("created controller %p, queue %p.\n", *out, queue);
    return S_OK;
}

HRESULT dispatcher_queue_get_for_current_thread(IDispatcherQueue **out)
{
    struct dispatcher_queue *queue = TlsGetValue(queue_tls);

    if (queue) queue_add_ref(queue);
    *out = queue ? &queue->IDispatcherQueue_iface : NULL;
    return S_OK;
}

BOOL dispatcher_queue_process_attach(HINSTANCE instance)
{
    coremessaging_instance = instance;
    queue_tls = TlsAlloc();
    return queue_tls != TLS_OUT_OF_INDEXES;
}

void dispatcher_queue_process_detach(void)
{
    if (queue_class_atom) UnregisterClassW(queue_window_class, coremessaging_instance);
    if (queue_tls != TLS_OUT_OF_INDEXES) TlsFree(queue_tls);
}
