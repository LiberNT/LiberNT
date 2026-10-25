/*
 * standard IPropertyStore implementation
 *
 * Copyright 2012 Vincent Povirk for CodeWeavers
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 */

#define COBJMACROS

#include <stdarg.h>

#include "windef.h"
#include "winbase.h"
#include "objbase.h"
#include "rpcproxy.h"
#include "propsys.h"
#ifdef __REACTOS__
#include "propvarutil.h"
#endif
#include "wine/debug.h"
#include "wine/list.h"

#include "initguid.h"
#include "propsys_private.h"

DEFINE_GUID(FMTID_NamedProperties, 0xd5cdd505, 0x2e9c, 0x101b, 0x93, 0x97, 0x08, 0x00, 0x2b, 0x2c, 0xf9, 0xae);

WINE_DEFAULT_DEBUG_CHANNEL(propsys);

typedef struct {
    struct list entry;
    DWORD pid;
    PROPVARIANT propvar;
    PSC_STATE state;
} propstore_value;

typedef struct {
    struct list entry;
    GUID fmtid;
    struct list values; /* list of struct propstore_value */
    DWORD count;
} propstore_format;

typedef struct {
    IPropertyStoreCache IPropertyStoreCache_iface;
#ifdef __REACTOS__
    IPersistSerializedPropStorage IPersistSerializedPropStorage_iface;
    PERSIST_SPROPSTORE_FLAGS flags;
#endif
    LONG ref;
    CRITICAL_SECTION lock;
    struct list formats; /* list of struct propstore_format */
} PropertyStore;

static inline PropertyStore *impl_from_IPropertyStoreCache(IPropertyStoreCache *iface)
{
    return CONTAINING_RECORD(iface, PropertyStore, IPropertyStoreCache_iface);
}

#ifdef __REACTOS__
static inline PropertyStore *impl_from_IPersistSerializedPropStorage(IPersistSerializedPropStorage *iface)
{
    return CONTAINING_RECORD(iface, PropertyStore, IPersistSerializedPropStorage_iface);
}
#endif

static HRESULT WINAPI PropertyStore_QueryInterface(IPropertyStoreCache *iface, REFIID iid,
    void **ppv)
{
    PropertyStore *This = impl_from_IPropertyStoreCache(iface);
    TRACE("(%p,%s,%p)\n", iface, debugstr_guid(iid), ppv);

    if (!ppv) return E_INVALIDARG;

    if (IsEqualIID(&IID_IUnknown, iid) || IsEqualIID(&IID_IPropertyStore, iid) ||
        IsEqualIID(&IID_IPropertyStoreCache, iid))
    {
        *ppv = &This->IPropertyStoreCache_iface;
    }
#ifdef __REACTOS__
    else if (IsEqualIID(&IID_IPersistSerializedPropStorage, iid))
    {
        *ppv = &This->IPersistSerializedPropStorage_iface;
    }
#endif
    else
    {
        FIXME("No interface for %s\n", debugstr_guid(iid));
        *ppv = NULL;
        return E_NOINTERFACE;
    }

    IUnknown_AddRef((IUnknown*)*ppv);
    return S_OK;
}

static ULONG WINAPI PropertyStore_AddRef(IPropertyStoreCache *iface)
{
    PropertyStore *This = impl_from_IPropertyStoreCache(iface);
    ULONG ref = InterlockedIncrement(&This->ref);

    TRACE("(%p) refcount=%lu\n", iface, ref);

    return ref;
}

static void destroy_format(propstore_format *format)
{
    propstore_value *cursor, *cursor2;
    LIST_FOR_EACH_ENTRY_SAFE(cursor, cursor2, &format->values, propstore_value, entry)
    {
        PropVariantClear(&cursor->propvar);
        HeapFree(GetProcessHeap(), 0, cursor);
    }
    HeapFree(GetProcessHeap(), 0, format);
}

static ULONG WINAPI PropertyStore_Release(IPropertyStoreCache *iface)
{
    PropertyStore *This = impl_from_IPropertyStoreCache(iface);
    ULONG ref = InterlockedDecrement(&This->ref);

    TRACE("(%p) refcount=%lu\n", iface, ref);

    if (ref == 0)
    {
        propstore_format *cursor, *cursor2;
        This->lock.DebugInfo->Spare[0] = 0;
        DeleteCriticalSection(&This->lock);
        LIST_FOR_EACH_ENTRY_SAFE(cursor, cursor2, &This->formats, propstore_format, entry)
            destroy_format(cursor);
        HeapFree(GetProcessHeap(), 0, This);
    }

    return ref;
}

static HRESULT WINAPI PropertyStore_GetCount(IPropertyStoreCache *iface,
    DWORD *cProps)
{
    PropertyStore *This = impl_from_IPropertyStoreCache(iface);
    propstore_format *format;

    TRACE("%p,%p\n", iface, cProps);

    if (!cProps)
        return E_POINTER;

    *cProps = 0;

    EnterCriticalSection(&This->lock);

    LIST_FOR_EACH_ENTRY(format, &This->formats, propstore_format, entry)
        *cProps += format->count;

    LeaveCriticalSection(&This->lock);

    return S_OK;
}

static HRESULT WINAPI PropertyStore_GetAt(IPropertyStoreCache *iface,
    DWORD iProp, PROPERTYKEY *pkey)
{
    PropertyStore *This = impl_from_IPropertyStoreCache(iface);
    propstore_format *format=NULL, *format_candidate;
    propstore_value *value;
    HRESULT hr;

    TRACE("%p,%ld,%p\n", iface, iProp, pkey);

    if (!pkey)
        return E_POINTER;

    EnterCriticalSection(&This->lock);

    LIST_FOR_EACH_ENTRY(format_candidate, &This->formats, propstore_format, entry)
    {
        if (format_candidate->count > iProp)
        {
            format = format_candidate;
            pkey->fmtid = format->fmtid;
            break;
        }

        iProp -= format_candidate->count;
    }

    if (format)
    {
        LIST_FOR_EACH_ENTRY(value, &format->values, propstore_value, entry)
        {
            if (iProp == 0)
            {
                pkey->pid = value->pid;
                break;
            }

            iProp--;
        }

        hr = S_OK;
    }
    else
        hr = E_INVALIDARG;

    LeaveCriticalSection(&This->lock);

    return hr;
}

static HRESULT PropertyStore_LookupValue(PropertyStore *This, REFPROPERTYKEY key,
                                         BOOL insert, propstore_value **result)
{
    propstore_format *format=NULL, *format_candidate;
    propstore_value *value=NULL, *value_candidate;

    if (IsEqualGUID(&key->fmtid, &FMTID_NamedProperties))
    {
        /* This is used in the property store format [MS-PROPSTORE]
         * for named values and probably gets special treatment. */
        ERR("don't know how to handle FMTID_NamedProperties\n");
        return E_FAIL;
    }

    LIST_FOR_EACH_ENTRY(format_candidate, &This->formats, propstore_format, entry)
    {
        if (IsEqualGUID(&format_candidate->fmtid, &key->fmtid))
        {
            format = format_candidate;
            break;
        }
    }

    if (!format)
    {
        if (!insert)
            return TYPE_E_ELEMENTNOTFOUND;

        format = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*format));
        if (!format)
            return E_OUTOFMEMORY;

        format->fmtid = key->fmtid;
        list_init(&format->values);
        list_add_tail(&This->formats, &format->entry);
    }

    LIST_FOR_EACH_ENTRY(value_candidate, &format->values, propstore_value, entry)
    {
        if (value_candidate->pid == key->pid)
        {
            value = value_candidate;
            break;
        }
    }

    if (!value)
    {
        if (!insert)
            return TYPE_E_ELEMENTNOTFOUND;

        value = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*value));
        if (!value)
            return E_OUTOFMEMORY;

        value->pid = key->pid;
        list_add_tail(&format->values, &value->entry);
        format->count++;
    }

    *result = value;

    return S_OK;
}

static HRESULT WINAPI PropertyStore_GetValue(IPropertyStoreCache *iface,
    REFPROPERTYKEY key, PROPVARIANT *pv)
{
    PropertyStore *This = impl_from_IPropertyStoreCache(iface);
    propstore_value *value;
    HRESULT hr;

    TRACE("%p,%p,%p\n", iface, key, pv);

    if (!pv)
        return E_POINTER;

    EnterCriticalSection(&This->lock);

    hr = PropertyStore_LookupValue(This, key, FALSE, &value);

    if (SUCCEEDED(hr))
        hr = PropVariantCopy(pv, &value->propvar);
    else if (hr == TYPE_E_ELEMENTNOTFOUND)
    {
        PropVariantInit(pv);
        hr = S_OK;
    }

    LeaveCriticalSection(&This->lock);

    return hr;
}

static HRESULT WINAPI PropertyStore_SetValue(IPropertyStoreCache *iface,
    REFPROPERTYKEY key, REFPROPVARIANT propvar)
{
    PropertyStore *This = impl_from_IPropertyStoreCache(iface);
    propstore_value *value;
    HRESULT hr;
    PROPVARIANT temp;

    TRACE("%p,%p,%p\n", iface, key, propvar);

#ifdef __REACTOS__
    if (This->flags & FPSPS_READONLY)
        return STG_E_ACCESSDENIED;
#endif

    EnterCriticalSection(&This->lock);

    hr = PropertyStore_LookupValue(This, key, TRUE, &value);

    if (SUCCEEDED(hr))
        hr = PropVariantCopy(&temp, propvar);

    if (SUCCEEDED(hr))
    {
        PropVariantClear(&value->propvar);
        value->propvar = temp;
    }

    LeaveCriticalSection(&This->lock);

    return hr;
}

static HRESULT WINAPI PropertyStore_Commit(IPropertyStoreCache *iface)
{
    FIXME("%p: stub\n", iface);
    return S_OK;
}

static HRESULT WINAPI PropertyStore_GetState(IPropertyStoreCache *iface,
    REFPROPERTYKEY key, PSC_STATE *pstate)
{
    PropertyStore *This = impl_from_IPropertyStoreCache(iface);
    propstore_value *value;
    HRESULT hr;

    TRACE("%p,%p,%p\n", iface, key, pstate);

    EnterCriticalSection(&This->lock);

    hr = PropertyStore_LookupValue(This, key, FALSE, &value);

    if (SUCCEEDED(hr))
        *pstate = value->state;

    LeaveCriticalSection(&This->lock);

    if (FAILED(hr))
        *pstate = PSC_NORMAL;

    return hr;
}

static HRESULT WINAPI PropertyStore_GetValueAndState(IPropertyStoreCache *iface,
    REFPROPERTYKEY key, PROPVARIANT *ppropvar, PSC_STATE *pstate)
{
    PropertyStore *This = impl_from_IPropertyStoreCache(iface);
    propstore_value *value;
    HRESULT hr;

    TRACE("%p,%p,%p,%p\n", iface, key, ppropvar, pstate);

    EnterCriticalSection(&This->lock);

    hr = PropertyStore_LookupValue(This, key, FALSE, &value);

    if (SUCCEEDED(hr))
        hr = PropVariantCopy(ppropvar, &value->propvar);

    if (SUCCEEDED(hr))
        *pstate = value->state;

    LeaveCriticalSection(&This->lock);

    if (FAILED(hr))
    {
        PropVariantInit(ppropvar);
        *pstate = PSC_NORMAL;
    }

    return hr;
}

static HRESULT WINAPI PropertyStore_SetState(IPropertyStoreCache *iface,
    REFPROPERTYKEY key, PSC_STATE pstate)
{
    PropertyStore *This = impl_from_IPropertyStoreCache(iface);
    propstore_value *value;
    HRESULT hr;

    TRACE("%p,%p,%d\n", iface, key, pstate);

    EnterCriticalSection(&This->lock);

    hr = PropertyStore_LookupValue(This, key, FALSE, &value);

    if (SUCCEEDED(hr))
        value->state = pstate;

    LeaveCriticalSection(&This->lock);

    return hr;
}

static HRESULT WINAPI PropertyStore_SetValueAndState(IPropertyStoreCache *iface,
    REFPROPERTYKEY key, const PROPVARIANT *ppropvar, PSC_STATE state)
{
    PropertyStore *This = impl_from_IPropertyStoreCache(iface);
    propstore_value *value;
    HRESULT hr;
    PROPVARIANT temp;

    TRACE("%p,%p,%p,%d\n", iface, key, ppropvar, state);

#ifdef __REACTOS__
    if (This->flags & FPSPS_READONLY)
        return STG_E_ACCESSDENIED;
#endif

    EnterCriticalSection(&This->lock);

    hr = PropertyStore_LookupValue(This, key, TRUE, &value);

    if (SUCCEEDED(hr))
        hr = PropVariantCopy(&temp, ppropvar);

    if (SUCCEEDED(hr))
    {
        PropVariantClear(&value->propvar);
        value->propvar = temp;
        value->state = state;
    }

    LeaveCriticalSection(&This->lock);

    return hr;
}

static const IPropertyStoreCacheVtbl PropertyStore_Vtbl = {
    PropertyStore_QueryInterface,
    PropertyStore_AddRef,
    PropertyStore_Release,
    PropertyStore_GetCount,
    PropertyStore_GetAt,
    PropertyStore_GetValue,
    PropertyStore_SetValue,
    PropertyStore_Commit,
    PropertyStore_GetState,
    PropertyStore_GetValueAndState,
    PropertyStore_SetState,
    PropertyStore_SetValueAndState
};

#ifdef __REACTOS__
#define PROPSTORAGE_VERSION 0x53505331
#define PROPVALUE_HEADER_SIZE (2 * sizeof(DWORD) + sizeof(BYTE))

static HRESULT WINAPI PropertyStore_Serialized_QueryInterface(IPersistSerializedPropStorage *iface, REFIID iid, void **ppv)
{
    PropertyStore *This = impl_from_IPersistSerializedPropStorage(iface);
    return IPropertyStoreCache_QueryInterface(&This->IPropertyStoreCache_iface, iid, ppv);
}

static ULONG WINAPI PropertyStore_Serialized_AddRef(IPersistSerializedPropStorage *iface)
{
    PropertyStore *This = impl_from_IPersistSerializedPropStorage(iface);
    return IPropertyStoreCache_AddRef(&This->IPropertyStoreCache_iface);
}

static ULONG WINAPI PropertyStore_Serialized_Release(IPersistSerializedPropStorage *iface)
{
    PropertyStore *This = impl_from_IPersistSerializedPropStorage(iface);
    return IPropertyStoreCache_Release(&This->IPropertyStoreCache_iface);
}

static HRESULT WINAPI PropertyStore_Serialized_SetFlags(IPersistSerializedPropStorage *iface, PERSIST_SPROPSTORE_FLAGS flags)
{
    PropertyStore *This = impl_from_IPersistSerializedPropStorage(iface);

    TRACE("%p, %#x.\n", iface, flags);

    This->flags = flags;
    return S_OK;
}

static void PropertyStore_ClearFormats(struct list *formats)
{
    propstore_format *cursor, *cursor2;

    LIST_FOR_EACH_ENTRY_SAFE(cursor, cursor2, formats, propstore_format, entry)
    {
        list_remove(&cursor->entry);
        destroy_format(cursor);
    }
}

static HRESULT PropertyStore_AddParsedValue(struct list *formats, const FMTID *fmtid, DWORD pid, PROPVARIANT *propvar)
{
    propstore_format *format = NULL, *cursor;
    propstore_value *value;

    LIST_FOR_EACH_ENTRY(cursor, formats, propstore_format, entry)
    {
        if (IsEqualGUID(&cursor->fmtid, fmtid))
        {
            format = cursor;
            break;
        }
    }

    if (!format)
    {
        if (!(format = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*format))))
            return E_OUTOFMEMORY;
        format->fmtid = *fmtid;
        list_init(&format->values);
        list_add_tail(formats, &format->entry);
    }

    LIST_FOR_EACH_ENTRY(value, &format->values, propstore_value, entry)
    {
        if (value->pid == pid)
        {
            PropVariantClear(&value->propvar);
            value->propvar = *propvar;
            return S_OK;
        }
    }

    if (!(value = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*value))))
        return E_OUTOFMEMORY;
    value->pid = pid;
    value->propvar = *propvar;
    list_add_tail(&format->values, &value->entry);
    format->count++;

    return S_OK;
}

static HRESULT WINAPI PropertyStore_Serialized_SetPropertyStorage(IPersistSerializedPropStorage *iface,
    PCUSERIALIZEDPROPSTORAGE psps, DWORD cb)
{
    PropertyStore *This = impl_from_IPersistSerializedPropStorage(iface);
    const BYTE *ptr = (const BYTE *)psps, *end = ptr + cb;
    struct list formats = LIST_INIT(formats);
    HRESULT hr = S_OK;
    BOOL done = FALSE;

    TRACE("%p, %p, %lu.\n", iface, psps, cb);

    if (!psps && cb)
        return E_POINTER;

    while (cb && SUCCEEDED(hr) && !done)
    {
        const BYTE *value_ptr, *storage_end;
        DWORD storage_size, version;
        BOOL values_done = FALSE;
        FMTID fmtid;

        if (end - ptr < (LONG_PTR)sizeof(DWORD))
            break;
        memcpy(&storage_size, ptr, sizeof(storage_size));
        if (!storage_size)
        {
            done = TRUE;
            break;
        }
        if (storage_size < 2 * sizeof(DWORD) + sizeof(FMTID) || storage_size > (ULONG)(end - ptr))
            break;
        memcpy(&version, ptr + sizeof(DWORD), sizeof(version));
        if (version != PROPSTORAGE_VERSION)
            break;
        memcpy(&fmtid, ptr + 2 * sizeof(DWORD), sizeof(fmtid));

        value_ptr = ptr + 2 * sizeof(DWORD) + sizeof(FMTID);
        storage_end = ptr + storage_size;
        while (storage_end - value_ptr >= (LONG_PTR)sizeof(DWORD))
        {
            PROPVARIANT propvar;
            DWORD value_size, pid;

            memcpy(&value_size, value_ptr, sizeof(value_size));
            if (!value_size)
            {
                values_done = TRUE;
                break;
            }
            if (value_size <= PROPVALUE_HEADER_SIZE || value_size > (ULONG)(storage_end - value_ptr))
                break;
            memcpy(&pid, value_ptr + sizeof(DWORD), sizeof(pid));
            if (FAILED(hr = StgDeserializePropVariant((const SERIALIZEDPROPERTYVALUE *)(value_ptr + PROPVALUE_HEADER_SIZE),
                    value_size - PROPVALUE_HEADER_SIZE, &propvar)))
                break;
            if (FAILED(hr = PropertyStore_AddParsedValue(&formats, &fmtid, pid, &propvar)))
            {
                PropVariantClear(&propvar);
                break;
            }
            value_ptr += value_size;
        }
        if (!values_done)
            break;

        ptr = storage_end;
    }

    if (cb && SUCCEEDED(hr) && !done)
        hr = HRESULT_FROM_WIN32(ERROR_INVALID_DATA);

    if (FAILED(hr))
    {
        PropertyStore_ClearFormats(&formats);
        return hr;
    }

    EnterCriticalSection(&This->lock);
    PropertyStore_ClearFormats(&This->formats);
    list_move_tail(&This->formats, &formats);
    LeaveCriticalSection(&This->lock);

    return S_OK;
}

static HRESULT WINAPI PropertyStore_Serialized_GetPropertyStorage(IPersistSerializedPropStorage *iface,
    SERIALIZEDPROPSTORAGE **psps, DWORD *pcb)
{
    PropertyStore *This = impl_from_IPersistSerializedPropStorage(iface);
    SERIALIZEDPROPERTYVALUE *serialized;
    propstore_format *format;
    propstore_value *value;
    DWORD size, storage_size, version = PROPSTORAGE_VERSION;
    BYTE *buffer = NULL, *ptr;
    BOOL pass;
    HRESULT hr = S_OK;
    ULONG len;

    TRACE("%p, %p, %p.\n", iface, psps, pcb);

    if (!psps || !pcb)
        return E_POINTER;

    *psps = NULL;
    *pcb = 0;

    EnterCriticalSection(&This->lock);

    for (pass = FALSE, size = 0; SUCCEEDED(hr); pass = TRUE)
    {
        ptr = buffer;
        size = 0;

        LIST_FOR_EACH_ENTRY(format, &This->formats, propstore_format, entry)
        {
            BYTE *storage = ptr;

            if (!format->count)
                continue;

            storage_size = 2 * sizeof(DWORD) + sizeof(FMTID) + sizeof(DWORD);
            if (pass)
            {
                memcpy(ptr + sizeof(DWORD), &version, sizeof(version));
                memcpy(ptr + 2 * sizeof(DWORD), &format->fmtid, sizeof(FMTID));
                ptr += 2 * sizeof(DWORD) + sizeof(FMTID);
            }

            LIST_FOR_EACH_ENTRY(value, &format->values, propstore_value, entry)
            {
                DWORD value_size;

                if (FAILED(StgSerializePropVariant(&value->propvar, &serialized, &len)))
                    continue;

                value_size = PROPVALUE_HEADER_SIZE + len;
                storage_size += value_size;
                if (pass)
                {
                    memcpy(ptr, &value_size, sizeof(value_size));
                    memcpy(ptr + sizeof(DWORD), &value->pid, sizeof(value->pid));
                    ptr[2 * sizeof(DWORD)] = 0;
                    memcpy(ptr + PROPVALUE_HEADER_SIZE, serialized, len);
                    ptr += value_size;
                }
                CoTaskMemFree(serialized);
            }

            if (pass)
            {
                memset(ptr, 0, sizeof(DWORD));
                ptr += sizeof(DWORD);
                memcpy(storage, &storage_size, sizeof(storage_size));
            }
            size += storage_size;
        }

        if (!size)
            break;
        size += sizeof(DWORD);

        if (pass)
        {
            memset(ptr, 0, sizeof(DWORD));
            break;
        }

        if (!(buffer = CoTaskMemAlloc(size)))
            hr = E_OUTOFMEMORY;
    }

    LeaveCriticalSection(&This->lock);

    if (FAILED(hr))
        return hr;

    *psps = (SERIALIZEDPROPSTORAGE *)buffer;
    *pcb = size;

    return S_OK;
}

static const IPersistSerializedPropStorageVtbl PropertyStore_Serialized_Vtbl = {
    PropertyStore_Serialized_QueryInterface,
    PropertyStore_Serialized_AddRef,
    PropertyStore_Serialized_Release,
    PropertyStore_Serialized_SetFlags,
    PropertyStore_Serialized_SetPropertyStorage,
    PropertyStore_Serialized_GetPropertyStorage
};
#endif

HRESULT PropertyStore_CreateInstance(IUnknown *pUnkOuter, REFIID iid, void** ppv)
{
    PropertyStore *This;
    HRESULT ret;

    TRACE("(%p,%s,%p)\n", pUnkOuter, debugstr_guid(iid), ppv);

    *ppv = NULL;

    if (pUnkOuter) return CLASS_E_NOAGGREGATION;

    This = HeapAlloc(GetProcessHeap(), 0, sizeof(PropertyStore));
    if (!This) return E_OUTOFMEMORY;

    This->IPropertyStoreCache_iface.lpVtbl = &PropertyStore_Vtbl;
#ifdef __REACTOS__
    This->IPersistSerializedPropStorage_iface.lpVtbl = &PropertyStore_Serialized_Vtbl;
    This->flags = FPSPS_DEFAULT;
#endif
    This->ref = 1;
    InitializeCriticalSectionEx(&This->lock, 0, RTL_CRITICAL_SECTION_FLAG_FORCE_DEBUG_INFO);
    This->lock.DebugInfo->Spare[0] = (DWORD_PTR)(__FILE__ ": PropertyStore.lock");
    list_init(&This->formats);

    ret = IPropertyStoreCache_QueryInterface(&This->IPropertyStoreCache_iface, iid, ppv);
    IPropertyStoreCache_Release(&This->IPropertyStoreCache_iface);

    return ret;
}

#ifdef __REACTOS__
struct object_property_store
{
    IUnknown IUnknown_iface;
    LONG ref;
    IUnknown *object;
};

static inline struct object_property_store *impl_from_object_IUnknown(IUnknown *iface)
{
    return CONTAINING_RECORD(iface, struct object_property_store, IUnknown_iface);
}

static HRESULT WINAPI object_property_store_QueryInterface(IUnknown *iface, REFIID iid, void **ppv)
{
    struct object_property_store *store = impl_from_object_IUnknown(iface);

    TRACE("%p, %s, %p.\n", iface, debugstr_guid(iid), ppv);

    if (IsEqualIID(iid, &IID_IUnknown))
    {
        *ppv = iface;
        IUnknown_AddRef(iface);
        return S_OK;
    }

    return IUnknown_QueryInterface(store->object, iid, ppv);
}

static ULONG WINAPI object_property_store_AddRef(IUnknown *iface)
{
    struct object_property_store *store = impl_from_object_IUnknown(iface);
    return InterlockedIncrement(&store->ref);
}

static ULONG WINAPI object_property_store_Release(IUnknown *iface)
{
    struct object_property_store *store = impl_from_object_IUnknown(iface);
    ULONG ref = InterlockedDecrement(&store->ref);

    if (!ref)
    {
        IUnknown_Release(store->object);
        HeapFree(GetProcessHeap(), 0, store);
    }

    return ref;
}

static const IUnknownVtbl object_property_store_vtbl =
{
    object_property_store_QueryInterface,
    object_property_store_AddRef,
    object_property_store_Release
};
#endif

HRESULT WINAPI PSCreatePropertyStoreFromObject(IUnknown *obj, DWORD access, REFIID riid, void **ret)
{
    HRESULT hr;
#ifdef __REACTOS__
    struct object_property_store *store;
    IPropertyStore *propstore;
#endif

    TRACE("(%p, %ld, %s, %p)\n", obj, access, debugstr_guid(riid), ret);

    if (!obj || !ret)
        return E_POINTER;

    if (IsEqualIID(riid, &IID_IPropertyStore) && SUCCEEDED(hr = IUnknown_QueryInterface(obj, riid, ret)))
        return hr;

#ifdef __REACTOS__
    if (SUCCEEDED(IUnknown_QueryInterface(obj, &IID_IPropertyStore, (void **)&propstore)))
    {
        if (!(store = HeapAlloc(GetProcessHeap(), 0, sizeof(*store))))
        {
            IPropertyStore_Release(propstore);
            return E_OUTOFMEMORY;
        }
        store->IUnknown_iface.lpVtbl = &object_property_store_vtbl;
        store->ref = 1;
        store->object = (IUnknown *)propstore;
        hr = IUnknown_QueryInterface(&store->IUnknown_iface, riid, ret);
        IUnknown_Release(&store->IUnknown_iface);
        return hr;
    }
#endif

    FIXME("Unimplemented for %s.\n", debugstr_guid(riid));
    return E_NOTIMPL;
}
