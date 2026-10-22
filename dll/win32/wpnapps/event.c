/*
 * PROJECT:     LiberNT Windows Runtime
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Event registration lists and shared helpers of the toast platform
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "private.h"

struct event_handler
{
    struct list entry;
    INT64 token;
    IUnknown *handler;
};

static LONG64 next_event_token;

void event_list_init(struct event_list *list)
{
    InitializeSRWLock(&list->lock);
    list_init(&list->handlers);
}

void event_list_clear(struct event_list *list)
{
    struct event_handler *handler, *next;

    AcquireSRWLockExclusive(&list->lock);
    LIST_FOR_EACH_ENTRY_SAFE(handler, next, &list->handlers, struct event_handler, entry)
    {
        list_remove(&handler->entry);
        IUnknown_Release(handler->handler);
        free(handler);
    }
    ReleaseSRWLockExclusive(&list->lock);
}

HRESULT event_list_add(struct event_list *list, void *handler, REFIID iid, EventRegistrationToken *token)
{
    struct event_handler *entry;
    IUnknown *typed;
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
    entry->handler = typed;
    entry->token = InterlockedIncrement64(&next_event_token);
    AcquireSRWLockExclusive(&list->lock);
    list_add_tail(&list->handlers, &entry->entry);
    ReleaseSRWLockExclusive(&list->lock);
    token->value = entry->token;
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
    {
        IUnknown_Release(found->handler);
        free(found);
    }
    return S_OK;
}

UINT32 event_list_snapshot(struct event_list *list, IUnknown ***handlers)
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
            IUnknown_AddRef(handler->handler);
            (*handlers)[count++] = handler->handler;
        }
    }
    else
    {
        count = 0;
    }
    ReleaseSRWLockShared(&list->lock);
    return count;
}

void event_list_release_snapshot(IUnknown **handlers, UINT32 count)
{
    UINT32 i;

    for (i = 0; i < count; i++)
        IUnknown_Release(handlers[i]);
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
