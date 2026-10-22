/*
 * PROJECT:     LiberNT Windows Runtime
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Activation entry points and shared helpers of the Store install control
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <initguid.h>
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
    if (FAILED(hr = IUnknown_QueryInterface((IUnknown *)handler, iid, (void **)&typed)))
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

HRESULT class_name_string(const WCHAR *name, HSTRING *class_name)
{
    if (!class_name)
        return E_POINTER;
    return WindowsCreateString(name, wcslen(name), class_name);
}

HRESULT trust_level(TrustLevel *level)
{
    if (!level)
        return E_POINTER;
    *level = BaseTrust;
    return S_OK;
}

HRESULT WINAPI DllGetClassObject(REFCLSID clsid, REFIID riid, void **out)
{
    if (out)
        *out = NULL;
    return CLASS_E_CLASSNOTAVAILABLE;
}

HRESULT WINAPI DllGetActivationFactory(HSTRING classid, IActivationFactory **factory)
{
    const WCHAR *name = WindowsGetStringRawBuffer(classid, NULL);
    IActivationFactory *source = NULL;

    if (!factory)
        return E_POINTER;
    *factory = NULL;
    if (!wcscmp(name, RuntimeClass_Windows_ApplicationModel_Store_Preview_InstallControl_AppInstallManager))
        source = app_install_manager_factory;
    else if (!wcscmp(name, RuntimeClass_Windows_ApplicationModel_Store_Preview_InstallControl_AppInstallOptions))
        source = app_install_options_factory;
    else if (!wcscmp(name, RuntimeClass_Windows_ApplicationModel_Store_Preview_InstallControl_AppUpdateOptions))
        source = app_update_options_factory;
    if (!source)
        return CLASS_E_CLASSNOTAVAILABLE;
    IActivationFactory_AddRef(source);
    *factory = source;
    return S_OK;
}
