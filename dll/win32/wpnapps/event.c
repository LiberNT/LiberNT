/*
 * PROJECT:     LiberNT Windows Runtime
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Event registration lists and shared helpers of the toast platform
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "private.h"
#include <combaseapi.h>
#include <ctxtcall.h>

struct event_handler
{
    struct list entry;
    INT64 token;
    LONG refs;
    IID iid;
    IAgileReference *reference;
    IContextCallback *context;
};

struct event_invocation
{
    struct event_handler *handler;
    event_invoke_func invoke;
    void *sender;
    void *args;
};

static LONG64 next_event_token;

static HRESULT WINAPI event_release_reference(ComCallData *data)
{
    IAgileReference *reference = InterlockedExchangePointer(&data->pUserDefined, NULL);

    if (reference)
        IAgileReference_Release(reference);
    return S_OK;
}

static void event_handler_release(struct event_handler *handler)
{
    ComCallData data = {0};
    HRESULT init = E_FAIL;

    if (!InterlockedDecrement(&handler->refs))
    {
        data.pUserDefined = handler->reference;
        if (handler->context)
        {
            init = CoInitializeEx(NULL, COINIT_MULTITHREADED);
            if (SUCCEEDED(init) || init == RPC_E_CHANGED_MODE)
                IContextCallback_ContextCallback(handler->context, event_release_reference, &data,
                                                 &IID_IContextCallback, 5, NULL);
        }
        if (data.pUserDefined)
            IAgileReference_Release((IAgileReference *)data.pUserDefined);
        if (SUCCEEDED(init))
            CoUninitialize();
        if (handler->context)
            IContextCallback_Release(handler->context);
        free(handler);
    }
}

void event_list_init(struct event_list *list)
{
    InitializeSRWLock(&list->lock);
    list_init(&list->handlers);
}

void event_list_clear(struct event_list *list)
{
    struct event_handler *handler;

    for (;;)
    {
        AcquireSRWLockExclusive(&list->lock);
        if (list_empty(&list->handlers))
        {
            ReleaseSRWLockExclusive(&list->lock);
            break;
        }
        handler = LIST_ENTRY(list_head(&list->handlers), struct event_handler, entry);
        list_remove(&handler->entry);
        ReleaseSRWLockExclusive(&list->lock);
        event_handler_release(handler);
    }
}

HRESULT event_list_add(struct event_list *list, void *handler, REFIID iid, EventRegistrationToken *token)
{
    struct event_handler *entry;
    IUnknown *typed, *agile;
    HRESULT hr;

    if (!token)
        return E_POINTER;
    if (!handler)
        return E_INVALIDARG;
    hr = IUnknown_QueryInterface((IUnknown *)handler, iid, (void **)&typed);
    if (FAILED(hr))
        return hr;
    if (!(entry = calloc(1, sizeof(*entry))))
    {
        IUnknown_Release(typed);
        return E_OUTOFMEMORY;
    }
    entry->refs = 1;
    entry->iid = *iid;
    hr = IUnknown_QueryInterface(typed, &IID_IAgileObject, (void **)&agile);
    if (SUCCEEDED(hr))
        IUnknown_Release(agile);
    else
        hr = CoGetObjectContext(&IID_IContextCallback, (void **)&entry->context);
    if (SUCCEEDED(hr))
        hr = RoGetAgileReference(AGILEREFERENCE_DEFAULT, &IID_IUnknown, typed, &entry->reference);
    IUnknown_Release(typed);
    if (FAILED(hr))
    {
        if (entry->context)
            IContextCallback_Release(entry->context);
        free(entry);
        return hr;
    }
    entry->token = InterlockedIncrement64(&next_event_token);
    AcquireSRWLockExclusive(&list->lock);
    list_add_tail(&list->handlers, &entry->entry);
    token->value = entry->token;
    ReleaseSRWLockExclusive(&list->lock);
    return S_OK;
}

HRESULT event_list_remove(struct event_list *list, EventRegistrationToken token)
{
    struct event_handler *handler, *found = NULL;

    AcquireSRWLockExclusive(&list->lock);
    LIST_FOR_EACH_ENTRY(handler, &list->handlers, struct event_handler, entry)
    {
        if (handler->token == token.value)
        {
            found = handler;
            list_remove(&handler->entry);
            break;
        }
    }
    ReleaseSRWLockExclusive(&list->lock);
    if (found)
        event_handler_release(found);
    return S_OK;
}

UINT32 event_list_snapshot(struct event_list *list, struct event_handler ***handlers)
{
    struct event_handler *handler;
    UINT32 count = 0;

    *handlers = NULL;
    AcquireSRWLockShared(&list->lock);
    count = list_count(&list->handlers);
    if (count && (*handlers = calloc(count, sizeof(**handlers))))
    {
        count = 0;
        LIST_FOR_EACH_ENTRY(handler, &list->handlers, struct event_handler, entry)
        {
            InterlockedIncrement(&handler->refs);
            (*handlers)[count++] = handler;
        }
    }
    else
        count = 0;
    ReleaseSRWLockShared(&list->lock);
    return count;
}

static HRESULT WINAPI event_invoke_in_context(ComCallData *data)
{
    struct event_invocation *invocation = data->pUserDefined;
    struct event_handler *entry = invocation->handler;
    IUnknown *identity, *typed;
    HRESULT hr;

    hr = IAgileReference_Resolve(entry->reference, &IID_IUnknown, (void **)&identity);
    if (FAILED(hr))
        return hr;
    hr = IUnknown_QueryInterface(identity, &entry->iid, (void **)&typed);
    IUnknown_Release(identity);
    if (FAILED(hr))
        return hr;
    hr = invocation->invoke(typed, invocation->sender, invocation->args);
    IUnknown_Release(typed);
    return hr;
}

void event_handler_invoke(struct event_handler *handler, event_invoke_func invoke, void *sender, void *args)
{
    struct event_invocation invocation = {handler, invoke, sender, args};
    ComCallData data = {0};

    data.pUserDefined = &invocation;
    if (handler->context)
        IContextCallback_ContextCallback(handler->context, event_invoke_in_context, &data,
                                         &IID_IContextCallback, 5, NULL);
    else
        event_invoke_in_context(&data);
}

void event_list_release_snapshot(struct event_handler **handlers, UINT32 count)
{
    UINT32 i;

    for (i = 0; i < count; i++)
        event_handler_release(handlers[i]);
    free(handlers);
}

HRESULT get_iids(REFIID iid, ULONG *count, IID **iids)
{
    if (!count || !iids)
        return E_POINTER;
    if (!(*iids = CoTaskMemAlloc(sizeof(**iids))))
        return E_OUTOFMEMORY;
    **iids = *iid;
    *count = 1;
    return S_OK;
}

HRESULT hstring_to_wide(HSTRING value, WCHAR **out)
{
    UINT32 length;
    const WCHAR *buffer = WindowsGetStringRawBuffer(value, &length);

    if (!(*out = malloc((length + 1) * sizeof(WCHAR))))
        return E_OUTOFMEMORY;
    memcpy(*out, buffer, length * sizeof(WCHAR));
    (*out)[length] = 0;
    return S_OK;
}

HRESULT xml_get_string(IXmlDocument *document, WCHAR **xml)
{
    IXmlNodeSerializer *serializer;
    HSTRING value;
    HRESULT hr;

    *xml = NULL;
    hr = IXmlDocument_QueryInterface(document, &IID_IXmlNodeSerializer, (void **)&serializer);
    if (FAILED(hr))
        return hr;
    hr = IXmlNodeSerializer_GetXml(serializer, &value);
    IXmlNodeSerializer_Release(serializer);
    if (FAILED(hr))
        return hr;
    hr = hstring_to_wide(value, xml);
    WindowsDeleteString(value);
    return hr;
}

HRESULT xml_load(const WCHAR *xml, IXmlDocument **document)
{
    IXmlDocumentIO *document_io;
    IInspectable *instance;
    HSTRING class_name, text;
    HRESULT hr;

    *document = NULL;
    hr = WindowsCreateString(RuntimeClass_Windows_Data_Xml_Dom_XmlDocument,
                             wcslen(RuntimeClass_Windows_Data_Xml_Dom_XmlDocument), &class_name);
    if (FAILED(hr))
        return hr;
    hr = RoActivateInstance(class_name, &instance);
    WindowsDeleteString(class_name);
    if (FAILED(hr))
        return hr;
    hr = IInspectable_QueryInterface(instance, &IID_IXmlDocumentIO, (void **)&document_io);
    if (SUCCEEDED(hr))
    {
        hr = WindowsCreateString(xml, wcslen(xml), &text);
        if (SUCCEEDED(hr))
        {
            hr = IXmlDocumentIO_LoadXml(document_io, text);
            WindowsDeleteString(text);
        }
        IXmlDocumentIO_Release(document_io);
    }
    if (SUCCEEDED(hr))
        hr = IInspectable_QueryInterface(instance, &IID_IXmlDocument, (void **)document);
    IInspectable_Release(instance);
    return hr;
}
