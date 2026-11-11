/*
 * Copyright 2016 Michael Müller
 * Copyright 2017 Andrey Gusev
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

#include "ntstatus.h"
#include "windows.h"
#include "appmodel.h"
#include "shlwapi.h"
#include "perflib.h"
#include "winternl.h"
#include "winperf.h"

#include "wine/debug.h"
#include "kernelbase.h"
#include "wine/list.h"

WINE_DEFAULT_DEBUG_CHANNEL(kernelbase);


BOOL is_wow64 = FALSE;

/***********************************************************************
 *           DllMain
 */
#ifndef __REACTOS__
BOOL WINAPI DllMain( HINSTANCE hinst, DWORD reason, LPVOID reserved )
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        DisableThreadLibraryCalls( hinst );
        IsWow64Process( GetCurrentProcess(), &is_wow64 );
        init_global_data();
        init_locale( hinst );
        init_startup_info( NtCurrentTeb()->Peb->ProcessParameters );
        init_console();
    }
    return TRUE;
}
#endif /* __REACTOS__ */


/***********************************************************************
 *           MulDiv   (kernelbase.@)
 */
INT WINAPI MulDiv( INT a, INT b, INT c )
{
    LONGLONG ret;

    if (!c) return -1;

    /* We want to deal with a positive divisor to simplify the logic. */
    if (c < 0)
    {
        a = -a;
        c = -c;
    }

    /* If the result is positive, we "add" to round. else, we subtract to round. */
    if ((a < 0 && b < 0) || (a >= 0 && b >= 0))
        ret = (((LONGLONG)a * b) + (c / 2)) / c;
    else
        ret = (((LONGLONG)a * b) - (c / 2)) / c;

    if (ret > 2147483647 || ret < -2147483647) return -1;
    return ret;
}

/***********************************************************************
 *          AppPolicyGetMediaFoundationCodecLoading (KERNELBASE.@)
 */

LONG WINAPI AppPolicyGetMediaFoundationCodecLoading(HANDLE token, AppPolicyMediaFoundationCodecLoading *policy)
{
    FIXME("%p, %p\n", token, policy);

    if(policy)
        *policy = AppPolicyMediaFoundationCodecLoading_All;

    return ERROR_SUCCESS;
}

/***********************************************************************
 *          AppPolicyGetProcessTerminationMethod (KERNELBASE.@)
 */
LONG WINAPI AppPolicyGetProcessTerminationMethod(HANDLE token, AppPolicyProcessTerminationMethod *policy)
{
    FIXME("%p, %p\n", token, policy);

    if(policy)
        *policy = AppPolicyProcessTerminationMethod_ExitProcess;

    return ERROR_SUCCESS;
}

/***********************************************************************
 *          AppPolicyGetThreadInitializationType (KERNELBASE.@)
 */
LONG WINAPI AppPolicyGetThreadInitializationType(HANDLE token, AppPolicyThreadInitializationType *policy)
{
    FIXME("%p, %p\n", token, policy);

    if(policy)
        *policy = AppPolicyThreadInitializationType_None;

    return ERROR_SUCCESS;
}

/***********************************************************************
 *          AppPolicyGetShowDeveloperDiagnostic (KERNELBASE.@)
 */
LONG WINAPI AppPolicyGetShowDeveloperDiagnostic(HANDLE token, AppPolicyShowDeveloperDiagnostic *policy)
{
    FIXME("%p, %p\n", token, policy);

    if(policy)
        *policy = AppPolicyShowDeveloperDiagnostic_ShowUI;

    return ERROR_SUCCESS;
}

/***********************************************************************
 *          AppPolicyGetWindowingModel (KERNELBASE.@)
 */
LONG WINAPI AppPolicyGetWindowingModel(HANDLE token, AppPolicyWindowingModel *policy)
{
    static int once;

    if(!once++)
        FIXME("%p, %p\n", token, policy);

    if(policy)
        *policy = AppPolicyWindowingModel_ClassicDesktop;

    return ERROR_SUCCESS;
}

struct counterset_template
{
    PERF_COUNTERSET_INFO counterset;
    PERF_COUNTER_INFO counter[1];
};

struct counterset_instance
{
    struct list entry;
    struct counterset_template *template;
    PERF_COUNTERSET_INSTANCE instance;
};

struct perf_provider
{
    GUID guid;
    PERFLIBREQUEST callback;
    PERF_MEM_ALLOC mem_alloc;
    PERF_MEM_FREE mem_free;
    void *mem_context;
    struct counterset_template **countersets;
    unsigned int counterset_count;

    struct list instance_list;
};

static struct perf_provider *perf_provider_from_handle(HANDLE prov)
{
    return (struct perf_provider *)prov;
}

static void *perf_alloc( struct perf_provider *prov, SIZE_T size )
{
    void *ptr;

    if (!prov->mem_alloc) return HeapAlloc( GetProcessHeap(), HEAP_ZERO_MEMORY, size );
    if ((ptr = prov->mem_alloc( size, prov->mem_context ))) memset( ptr, 0, size );
    return ptr;
}

static void perf_free( struct perf_provider *prov, void *ptr )
{
    if (!prov->mem_alloc) HeapFree( GetProcessHeap(), 0, ptr );
    else if (prov->mem_free) prov->mem_free( ptr, prov->mem_context );
}

static struct counterset_template *find_counterset( struct perf_provider *prov, const GUID *guid )
{
    unsigned int i;

    for (i = 0; i < prov->counterset_count; ++i)
        if (IsEqualGUID( guid, &prov->countersets[i]->counterset.CounterSetGuid )) return prov->countersets[i];
    return NULL;
}

static struct counterset_instance *find_instance( struct perf_provider *prov, struct counterset_template *template,
                                                  const WCHAR *name, ULONG id )
{
    struct counterset_instance *inst;

    LIST_FOR_EACH_ENTRY(inst, &prov->instance_list, struct counterset_instance, entry)
    {
        if (inst->template == template && inst->instance.InstanceId == id &&
            (!name || !wcscmp( (WCHAR *)((BYTE *)&inst->instance + inst->instance.InstanceNameOffset), name )))
            return inst;
    }
    return NULL;
}

/***********************************************************************
 *           PerfCreateInstance   (KERNELBASE.@)
 */
PERF_COUNTERSET_INSTANCE * WINAPI PerfCreateInstance( HANDLE handle, const GUID *guid,
                                                      const WCHAR *name, ULONG id )
{
    struct perf_provider *prov = perf_provider_from_handle( handle );
    struct counterset_template *template;
    struct counterset_instance *inst;
    ULONG size;

    TRACE( "handle %p, guid %s, name %s, id %lu.\n", handle, debugstr_guid(guid), debugstr_w(name), id );

    if (!prov || !guid || !name)
    {
        SetLastError( ERROR_INVALID_PARAMETER );
        return NULL;
    }

    if (!(template = find_counterset( prov, guid )))
    {
        SetLastError( ERROR_NOT_FOUND );
        return NULL;
    }

    if (find_instance( prov, template, NULL, id ))
    {
        SetLastError( ERROR_ALREADY_EXISTS );
        return NULL;
    }

    size = (sizeof(PERF_COUNTERSET_INSTANCE) + template->counterset.NumCounters * sizeof(UINT64)
            + (lstrlenW( name ) + 1) * sizeof(WCHAR) + 7) & ~7;
    if (!(inst = perf_alloc( prov, offsetof(struct counterset_instance, instance) + size )))
    {
        SetLastError( ERROR_OUTOFMEMORY );
        return NULL;
    }

    inst->template = template;
    inst->instance.CounterSetGuid = *guid;
    inst->instance.dwSize = size;
    inst->instance.InstanceId = id;
    inst->instance.InstanceNameOffset = sizeof(PERF_COUNTERSET_INSTANCE)
                                        + template->counterset.NumCounters * sizeof(UINT64);
    inst->instance.InstanceNameSize = (lstrlenW( name ) + 1) * sizeof(WCHAR);
    memcpy( (BYTE *)&inst->instance + inst->instance.InstanceNameOffset, name, inst->instance.InstanceNameSize );
    list_add_tail( &prov->instance_list, &inst->entry );

    return &inst->instance;
}

/***********************************************************************
 *           PerfDeleteInstance   (KERNELBASE.@)
 */
ULONG WINAPI PerfDeleteInstance(HANDLE provider, PERF_COUNTERSET_INSTANCE *block)
{
    struct perf_provider *prov = perf_provider_from_handle( provider );
    struct counterset_instance *inst;

    TRACE( "provider %p, block %p.\n", provider, block );

    if (!prov || !block) return ERROR_INVALID_PARAMETER;

    LIST_FOR_EACH_ENTRY(inst, &prov->instance_list, struct counterset_instance, entry)
    {
        if (&inst->instance != block) continue;
        list_remove( &inst->entry );
        perf_free( prov, inst );
        return ERROR_SUCCESS;
    }
    return ERROR_INVALID_PARAMETER;
}

/***********************************************************************
 *           PerfQueryInstance   (KERNELBASE.@)
 */
PERF_COUNTERSET_INSTANCE * WINAPI PerfQueryInstance( HANDLE handle, const GUID *guid, const WCHAR *name, ULONG id )
{
    struct perf_provider *prov = perf_provider_from_handle( handle );
    struct counterset_template *template;
    struct counterset_instance *inst;

    TRACE( "handle %p, guid %s, name %s, id %lu.\n", handle, debugstr_guid(guid), debugstr_w(name), id );

    if (!prov || !guid || !name)
    {
        SetLastError( ERROR_INVALID_PARAMETER );
        return NULL;
    }
    if (!(template = find_counterset( prov, guid )) || !(inst = find_instance( prov, template, name, id )))
    {
        SetLastError( ERROR_NOT_FOUND );
        return NULL;
    }
    return &inst->instance;
}

/***********************************************************************
 *           PerfSetCounterSetInfo   (KERNELBASE.@)
 */
ULONG WINAPI PerfSetCounterSetInfo( HANDLE handle, PERF_COUNTERSET_INFO *template, ULONG size )
{
    struct perf_provider *prov = perf_provider_from_handle( handle );
    struct counterset_template **new_array;
    struct counterset_template *new;
    unsigned int i;

    TRACE( "handle %p, template %p, size %lu.\n", handle, template, size );

    if (!prov || !template) return ERROR_INVALID_PARAMETER;
    if (!template->NumCounters) return ERROR_INVALID_PARAMETER;
    if (size < sizeof(*template) || (size - (sizeof(*template))) / sizeof(PERF_COUNTER_INFO) < template->NumCounters)
        return ERROR_INVALID_PARAMETER;

    if (find_counterset( prov, &template->CounterSetGuid )) return ERROR_ALREADY_EXISTS;

    size = offsetof( struct counterset_template, counter[template->NumCounters] );
    if (!(new = HeapAlloc( GetProcessHeap(), 0, size ))) return ERROR_OUTOFMEMORY;

    if (prov->counterset_count)
        new_array = HeapReAlloc( GetProcessHeap(), 0, prov->countersets,
                                 sizeof(*prov->countersets) * (prov->counterset_count + 1) );
    else
        new_array = HeapAlloc( GetProcessHeap(), 0, sizeof(*prov->countersets) );

    if (!new_array)
    {
        HeapFree( GetProcessHeap(), 0, new );
        return ERROR_OUTOFMEMORY;
    }
    memcpy( new, template, size );
    for (i = 0; i < template->NumCounters; ++i)
        new->counter[i].Offset = i * sizeof(UINT64);
    new_array[prov->counterset_count++] = new;
    prov->countersets = new_array;

    return STATUS_SUCCESS;
}

static PERF_COUNTER_INFO* get_performance_counter_info(PERF_COUNTERSET_INSTANCE *instance, ULONG counter_id)
{
    unsigned int i;
    struct counterset_template *template;
    struct counterset_instance *inst;

    inst = CONTAINING_RECORD(instance, struct counterset_instance, instance);
    template = inst->template;

    for (i = 0; i < template->counterset.NumCounters; ++i)
        if (template->counter[i].CounterId == counter_id) return  &template->counter[i];

    return NULL;
}

static ULONG get_counter_value_address( HANDLE provider, PERF_COUNTERSET_INSTANCE *instance, ULONG counterid,
                                        BOOL large, void **address )
{
    PERF_COUNTER_INFO *counter;

    if (!perf_provider_from_handle( provider ) || !instance) return ERROR_INVALID_PARAMETER;
    if (!(counter = get_performance_counter_info( instance, counterid ))) return ERROR_NOT_FOUND;
    if (counter->Attrib & PERF_ATTRIB_BY_REFERENCE) return ERROR_INVALID_PARAMETER;
    if (!(counter->Type & PERF_SIZE_LARGE) != !large) return ERROR_INVALID_PARAMETER;
    *address = (BYTE *)(instance + 1) + counter->Offset;
    return ERROR_SUCCESS;
}

/***********************************************************************
 *           PerfSetCounterRefValue   (KERNELBASE.@)
 */
ULONG WINAPI PerfSetCounterRefValue(HANDLE provider, PERF_COUNTERSET_INSTANCE *instance,
                                    ULONG counterid, void *address)
{
    struct perf_provider *prov = perf_provider_from_handle( provider );
    PERF_COUNTER_INFO* counter;

    TRACE( "provider %p, instance %p, counterid %lu, address %p.\n", provider, instance, counterid, address );

    if (!prov || !instance || !address) return ERROR_INVALID_PARAMETER;

    counter = get_performance_counter_info(instance, counterid);

    if (counter == NULL) return ERROR_NOT_FOUND;
    if (!(counter->Attrib & PERF_ATTRIB_BY_REFERENCE)) return ERROR_INVALID_PARAMETER;

    *(void **)((BYTE *)(instance + 1) + counter->Offset) = address;

    return STATUS_SUCCESS;
}

/***********************************************************************
 *           PerfSetULongCounterValue   (KERNELBASE.@)
 */
ULONG WINAPI PerfSetULongCounterValue(HANDLE provider, PERF_COUNTERSET_INSTANCE *instance,
                                      ULONG counterid, ULONG value)
{
    void *address;
    ULONG ret;

    TRACE( "provider %p, instance %p, counterid %lu, value %lu.\n", provider, instance, counterid, value );

    if ((ret = get_counter_value_address( provider, instance, counterid, FALSE, &address ))) return ret;
    InterlockedExchange( (LONG *)address, value );
    return ERROR_SUCCESS;
}

/***********************************************************************
 *           PerfSetULongLongCounterValue   (KERNELBASE.@)
 */
ULONG WINAPI PerfSetULongLongCounterValue(HANDLE provider, PERF_COUNTERSET_INSTANCE *instance,
                                          ULONG counterid, ULONGLONG value)
{
    void *address;
    ULONG ret;

    TRACE( "provider %p, instance %p, counterid %lu, value %I64u.\n", provider, instance, counterid, value );

    if ((ret = get_counter_value_address( provider, instance, counterid, TRUE, &address ))) return ret;
    InterlockedExchange64( (LONG64 *)address, value );
    return ERROR_SUCCESS;
}

/***********************************************************************
 *           PerfIncrementULongCounterValue   (KERNELBASE.@)
 */
ULONG WINAPI PerfIncrementULongCounterValue( HANDLE provider, PERF_COUNTERSET_INSTANCE *instance,
                                             ULONG counterid, ULONG value )
{
    void *address;
    ULONG ret;

    TRACE( "provider %p, instance %p, counterid %lu, value %lu.\n", provider, instance, counterid, value );

    if ((ret = get_counter_value_address( provider, instance, counterid, FALSE, &address ))) return ret;
    InterlockedExchangeAdd( (LONG *)address, value );
    return ERROR_SUCCESS;
}

/***********************************************************************
 *           PerfIncrementULongLongCounterValue   (KERNELBASE.@)
 */
ULONG WINAPI PerfIncrementULongLongCounterValue( HANDLE provider, PERF_COUNTERSET_INSTANCE *instance,
                                                 ULONG counterid, ULONGLONG value )
{
    void *address;
    ULONG ret;

    TRACE( "provider %p, instance %p, counterid %lu, value %I64u.\n", provider, instance, counterid, value );

    if ((ret = get_counter_value_address( provider, instance, counterid, TRUE, &address ))) return ret;
    InterlockedExchangeAdd64( (LONG64 *)address, value );
    return ERROR_SUCCESS;
}

/***********************************************************************
 *           PerfDecrementULongCounterValue   (KERNELBASE.@)
 */
ULONG WINAPI PerfDecrementULongCounterValue( HANDLE provider, PERF_COUNTERSET_INSTANCE *instance,
                                             ULONG counterid, ULONG value )
{
    void *address;
    ULONG ret;

    TRACE( "provider %p, instance %p, counterid %lu, value %lu.\n", provider, instance, counterid, value );

    if ((ret = get_counter_value_address( provider, instance, counterid, FALSE, &address ))) return ret;
    InterlockedExchangeAdd( (LONG *)address, -(LONG)value );
    return ERROR_SUCCESS;
}

/***********************************************************************
 *           PerfDecrementULongLongCounterValue   (KERNELBASE.@)
 */
ULONG WINAPI PerfDecrementULongLongCounterValue( HANDLE provider, PERF_COUNTERSET_INSTANCE *instance,
                                                 ULONG counterid, ULONGLONG value )
{
    void *address;
    ULONG ret;

    TRACE( "provider %p, instance %p, counterid %lu, value %I64u.\n", provider, instance, counterid, value );

    if ((ret = get_counter_value_address( provider, instance, counterid, TRUE, &address ))) return ret;
    InterlockedExchangeAdd64( (LONG64 *)address, -(LONG64)value );
    return ERROR_SUCCESS;
}

/***********************************************************************
 *           PerfStartProvider   (KERNELBASE.@)
 */
ULONG WINAPI PerfStartProvider( GUID *guid, PERFLIBREQUEST callback, HANDLE *provider )
{
    PERF_PROVIDER_CONTEXT ctx;

    TRACE( "guid %s, callback %p, provider %p.\n", debugstr_guid(guid), callback, provider );

    memset( &ctx, 0, sizeof(ctx) );
    ctx.ContextSize = sizeof(ctx);
    ctx.ControlCallback = callback;

    return PerfStartProviderEx( guid, &ctx, provider );
}

/***********************************************************************
 *           PerfStartProviderEx   (KERNELBASE.@)
 */
ULONG WINAPI PerfStartProviderEx( GUID *guid, PERF_PROVIDER_CONTEXT *context, HANDLE *provider )
{
    struct perf_provider *prov;

    TRACE( "guid %s, context %p, provider %p.\n", debugstr_guid(guid), context, provider );

    if (!guid || !provider) return ERROR_INVALID_PARAMETER;
    if (context && context->ContextSize < sizeof(*context)) return ERROR_INVALID_PARAMETER;

    if (!(prov = HeapAlloc( GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*prov) ))) return ERROR_OUTOFMEMORY;
    list_init( &prov->instance_list );
    memcpy( &prov->guid, guid, sizeof(prov->guid) );
    if (context)
    {
        /* TODO: call ControlCallback once a PERFLIB consumer can query this provider */
        prov->callback = context->ControlCallback;
        prov->mem_alloc = context->MemAllocRoutine;
        prov->mem_free = context->MemFreeRoutine;
        prov->mem_context = context->pMemContext;
    }
    *provider = prov;

    return STATUS_SUCCESS;
}

/***********************************************************************
 *           PerfStopProvider   (KERNELBASE.@)
 */
ULONG WINAPI PerfStopProvider(HANDLE handle)
{
    struct perf_provider *prov = perf_provider_from_handle( handle );
    struct counterset_instance *inst, *next;
    unsigned int i;

    TRACE( "handle %p.\n", handle );

    if (!prov) return ERROR_INVALID_PARAMETER;

    if (!list_empty( &prov->instance_list ))
        WARN( "Stopping provider with active counter instances.\n" );

    LIST_FOR_EACH_ENTRY_SAFE(inst, next, &prov->instance_list, struct counterset_instance, entry)
    {
        list_remove( &inst->entry );
        perf_free( prov, inst );
    }

    for (i = 0; i < prov->counterset_count; ++i)
        HeapFree( GetProcessHeap(), 0, prov->countersets[i] );
    HeapFree( GetProcessHeap(), 0, prov->countersets );
    HeapFree( GetProcessHeap(), 0, prov );
    return STATUS_SUCCESS;
}

/***********************************************************************
 *           QuirkIsEnabled   (KERNELBASE.@)
 */
BOOL WINAPI QuirkIsEnabled(void *arg)
{
    FIXME("(%p): stub\n", arg);
    return FALSE;
}

/***********************************************************************
 *          QuirkIsEnabled3 (KERNELBASE.@)
 */
BOOL WINAPI QuirkIsEnabled3(void *unk1, void *unk2)
{
    static int once;

    if (!once++)
        FIXME("(%p, %p) stub!\n", unk1, unk2);

    return FALSE;
}

HRESULT WINAPI QISearch(void *base, const QITAB *table, REFIID riid, void **obj)
{
    const QITAB *ptr;
    IUnknown *unk;

    TRACE("%p, %p, %s, %p\n", base, table, debugstr_guid(riid), obj);

    if (!obj)
        return E_POINTER;

    for (ptr = table; ptr->piid; ++ptr)
    {
        TRACE("trying (offset %ld) %s\n", ptr->dwOffset, debugstr_guid(ptr->piid));
        if (IsEqualIID(riid, ptr->piid))
        {
            unk = (IUnknown *)((BYTE *)base + ptr->dwOffset);
            TRACE("matched, returning (%p)\n", unk);
            *obj = unk;
            IUnknown_AddRef(unk);
            return S_OK;
        }
    }

    if (IsEqualIID(riid, &IID_IUnknown))
    {
        unk = (IUnknown *)((BYTE *)base + table->dwOffset);
        TRACE("returning first for IUnknown (%p)\n", unk);
        *obj = unk;
        IUnknown_AddRef(unk);
        return S_OK;
    }

    WARN("Not found %s.\n", debugstr_guid(riid));
    *obj = NULL;
    return E_NOINTERFACE;
}

HRESULT WINAPI GetAcceptLanguagesA(LPSTR langbuf, DWORD *buflen)
{
    DWORD buflenW, convlen;
    WCHAR *langbufW;
    HRESULT hr;

    TRACE("%p, %p, *%p: %ld\n", langbuf, buflen, buflen, buflen ? *buflen : -1);

    if (!langbuf || !buflen || !*buflen)
        return E_FAIL;

    buflenW = *buflen;
    langbufW = HeapAlloc(GetProcessHeap(), 0, sizeof(WCHAR) * buflenW);
    hr = GetAcceptLanguagesW(langbufW, &buflenW);

    if (hr == S_OK)
    {
        convlen = WideCharToMultiByte(CP_ACP, 0, langbufW, -1, langbuf, *buflen, NULL, NULL);
        convlen--;  /* do not count the terminating 0 */
    }
    else  /* copy partial string anyway */
    {
        convlen = WideCharToMultiByte(CP_ACP, 0, langbufW, *buflen, langbuf, *buflen, NULL, NULL);
        if (convlen < *buflen)
        {
            langbuf[convlen] = 0;
            convlen--;  /* do not count the terminating 0 */
        }
        else
        {
            convlen = *buflen;
        }
    }
    *buflen = buflenW ? convlen : 0;

    HeapFree(GetProcessHeap(), 0, langbufW);
    return hr;
}

static HRESULT lcid_to_rfc1766(LCID lcid, WCHAR *rfc1766, INT len)
{
    WCHAR buffer[6 /* MAX_RFC1766_NAME */];
    INT n = GetLocaleInfoW(lcid, LOCALE_SISO639LANGNAME, buffer, ARRAY_SIZE(buffer));
    INT i;

    if (n)
    {
        i = PRIMARYLANGID(lcid);
        if ((((i == LANG_ENGLISH) || (i == LANG_CHINESE) || (i == LANG_ARABIC)) &&
            (SUBLANGID(lcid) == SUBLANG_DEFAULT)) ||
            (SUBLANGID(lcid) > SUBLANG_DEFAULT)) {

            buffer[n - 1] = '-';
            i = GetLocaleInfoW(lcid, LOCALE_SISO3166CTRYNAME, buffer + n, ARRAY_SIZE(buffer) - n);
            if (!i)
                buffer[n - 1] = '\0';
        }
        else
            i = 0;

        LCMapStringW(LOCALE_USER_DEFAULT, LCMAP_LOWERCASE, buffer, n + i, rfc1766, len);
        return ((n + i) > len) ? E_INVALIDARG : S_OK;
    }
    return E_FAIL;
}

HRESULT WINAPI GetAcceptLanguagesW(WCHAR *langbuf, DWORD *buflen)
{
    DWORD mystrlen, mytype;
    WCHAR *mystr;
    LCID mylcid;
    HKEY mykey;
    LONG lres;
    DWORD len;

    TRACE("%p, %p, *%p: %ld\n", langbuf, buflen, buflen, buflen ? *buflen : -1);

    if (!langbuf || !buflen || !*buflen)
        return E_FAIL;

    mystrlen = (*buflen > 20) ? *buflen : 20 ;
    len = mystrlen * sizeof(WCHAR);
    mystr = HeapAlloc(GetProcessHeap(), 0, len);
    mystr[0] = 0;
    RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Internet Explorer\\International",
                  0, KEY_QUERY_VALUE, &mykey);
    lres = RegQueryValueExW(mykey, L"AcceptLanguage", 0, &mytype, (PBYTE)mystr, &len);
    RegCloseKey(mykey);
    len = lstrlenW(mystr);

    if (!lres && (*buflen > len))
    {
        lstrcpyW(langbuf, mystr);
        *buflen = len;
        HeapFree(GetProcessHeap(), 0, mystr);
        return S_OK;
    }

    /* Did not find a value in the registry or the user buffer is too small */
    mylcid = GetUserDefaultLCID();
    lcid_to_rfc1766(mylcid, mystr, mystrlen);
    len = lstrlenW(mystr);

    memcpy(langbuf, mystr, min(*buflen, len + 1)*sizeof(WCHAR));
    HeapFree(GetProcessHeap(), 0, mystr);

    if (*buflen > len)
    {
        *buflen = len;
        return S_OK;
    }

    *buflen = 0;
    return E_NOT_SUFFICIENT_BUFFER;
}

HRESULT WINAPI GetIntegratedDisplaySize( double *sz_inches )
{
    FIXME( "%p stub.\n", sz_inches );
    return E_NOTIMPL;
}
