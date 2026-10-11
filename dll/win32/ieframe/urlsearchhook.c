/*
 * PROJECT:     LiberNT ieframe
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     Default URL search hook (IURLSearchHook2)
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <stdio.h>

#include "ieframe.h"

#include "shobjidl.h"
#include "shlwapi.h"
#include "shlguid.h"
#include "wininet.h"

#include "wine/debug.h"

WINE_DEFAULT_DEBUG_CHANNEL(ieframe);

#define SEARCH_SCOPES L"Software\\Microsoft\\Internet Explorer\\SearchScopes"
#define SEARCH_TERMS L"{searchTerms}"

typedef struct
{
    IURLSearchHook2 IURLSearchHook2_iface;
    LONG ref;
} URLSearchHook;

static inline URLSearchHook *impl_from_IURLSearchHook2(IURLSearchHook2 *iface)
{
    return CONTAINING_RECORD(iface, URLSearchHook, IURLSearchHook2_iface);
}

static HRESULT get_search_template(WCHAR *buf, DWORD size)
{
    static const HKEY roots[] = { HKEY_CURRENT_USER, HKEY_LOCAL_MACHINE };
    WCHAR scope[64], key[ARRAY_SIZE(SEARCH_SCOPES) + ARRAY_SIZE(scope)];
    DWORD len;
    int i, j;

    for (i = 0; i < ARRAY_SIZE(roots); i++)
    {
        len = sizeof(scope);
        if (RegGetValueW(roots[i], SEARCH_SCOPES, L"DefaultScope", RRF_RT_REG_SZ, NULL, scope, &len) != ERROR_SUCCESS)
            continue;

        swprintf(key, ARRAY_SIZE(key), L"%s\\%s", SEARCH_SCOPES, scope);
        for (j = 0; j < ARRAY_SIZE(roots); j++)
        {
            len = size;
            if (RegGetValueW(roots[j], key, L"URL", RRF_RT_REG_SZ, NULL, buf, &len) == ERROR_SUCCESS)
                return S_OK;
        }
    }
    return E_FAIL;
}

static WCHAR *encode_terms(const WCHAR *terms)
{
    static const char unreserved[] = "*-._";
    WCHAR *ret, *out;
    char *utf8;
    const unsigned char *in;
    int len;

    len = WideCharToMultiByte(CP_UTF8, 0, terms, -1, NULL, 0, NULL, NULL);
    if (!len)
        return NULL;
    utf8 = malloc(len);
    ret = malloc((len * 3 + 1) * sizeof(WCHAR));
    if (!utf8 || !ret)
    {
        free(utf8);
        free(ret);
        return NULL;
    }
    WideCharToMultiByte(CP_UTF8, 0, terms, -1, utf8, len, NULL, NULL);

    out = ret;
    for (in = (const unsigned char *)utf8; *in; in++)
    {
        if (*in == ' ')
            *out++ = '+';
        else if ((*in >= 'a' && *in <= 'z') || (*in >= 'A' && *in <= 'Z') || (*in >= '0' && *in <= '9') ||
                 strchr(unreserved, *in))
            *out++ = *in;
        else
            out += swprintf(out, 4, L"%%%02X", *in);
    }
    *out = 0;
    free(utf8);
    return ret;
}

static HRESULT translate(LPWSTR url, DWORD cch)
{
    WCHAR template[INTERNET_MAX_URL_LENGTH], *encoded, *result, *pos;
    const WCHAR *terms;
    size_t prefix, total;
    HRESULT hr;

    if (!url || !cch)
        return E_INVALIDARG;

    terms = url;
    if (*terms == '?')
    {
        terms++;
        terms += wcsspn(terms, L" \t");
    }
    else if (!wcspbrk(terms, L" \t"))
    {
        return E_FAIL;
    }
    if (!*terms)
        return E_FAIL;

    hr = get_search_template(template, sizeof(template));
    if (FAILED(hr))
        return hr;
    pos = wcsstr(template, SEARCH_TERMS);
    if (!pos)
        return E_FAIL;

    encoded = encode_terms(terms);
    if (!encoded)
        return E_OUTOFMEMORY;

    prefix = pos - template;
    total = wcslen(template) - (ARRAY_SIZE(SEARCH_TERMS) - 1) + wcslen(encoded) + 1;
    result = malloc(total * sizeof(WCHAR));
    if (!result)
    {
        free(encoded);
        return E_OUTOFMEMORY;
    }
    memcpy(result, template, prefix * sizeof(WCHAR));
    lstrcpyW(result + prefix, encoded);
    lstrcatW(result, pos + ARRAY_SIZE(SEARCH_TERMS) - 1);

    lstrcpynW(url, result, cch);
    free(result);
    free(encoded);
    return S_OK;
}

static HRESULT WINAPI URLSearchHook_QueryInterface(IURLSearchHook2 *iface, REFIID riid, void **ppv)
{
    URLSearchHook *This = impl_from_IURLSearchHook2(iface);

    TRACE("(%p)->(%s %p)\n", This, debugstr_guid(riid), ppv);

    if (!ppv)
        return E_POINTER;
    if (IsEqualGUID(&IID_IUnknown, riid) || IsEqualGUID(&IID_IURLSearchHook, riid) ||
        IsEqualGUID(&IID_IURLSearchHook2, riid))
    {
        *ppv = &This->IURLSearchHook2_iface;
        IURLSearchHook2_AddRef(iface);
        return S_OK;
    }
    *ppv = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI URLSearchHook_AddRef(IURLSearchHook2 *iface)
{
    URLSearchHook *This = impl_from_IURLSearchHook2(iface);
    LONG ref = InterlockedIncrement(&This->ref);

    TRACE("(%p) ref=%ld\n", This, ref);

    return ref;
}

static ULONG WINAPI URLSearchHook_Release(IURLSearchHook2 *iface)
{
    URLSearchHook *This = impl_from_IURLSearchHook2(iface);
    LONG ref = InterlockedDecrement(&This->ref);

    TRACE("(%p) ref=%ld\n", This, ref);

    if (!ref)
    {
        free(This);
        unlock_module();
    }
    return ref;
}

static HRESULT WINAPI URLSearchHook_Translate(IURLSearchHook2 *iface, LPWSTR url, DWORD cch)
{
    TRACE("(%p)->(%s %lu)\n", iface, debugstr_w(url), cch);

    return translate(url, cch);
}

static HRESULT WINAPI URLSearchHook_TranslateWithSearchContext(IURLSearchHook2 *iface, LPWSTR url, DWORD cch,
                                                              ISearchContext *context)
{
    TRACE("(%p)->(%s %lu %p)\n", iface, debugstr_w(url), cch, context);

    return translate(url, cch);
}

static const IURLSearchHook2Vtbl URLSearchHookVtbl =
{
    URLSearchHook_QueryInterface,
    URLSearchHook_AddRef,
    URLSearchHook_Release,
    URLSearchHook_Translate,
    URLSearchHook_TranslateWithSearchContext,
};

HRESULT WINAPI URLSearchHook_Create(IClassFactory *iface, IUnknown *outer, REFIID riid, void **ppv)
{
    URLSearchHook *This;
    HRESULT hr;

    TRACE("(%p %s %p)\n", outer, debugstr_guid(riid), ppv);

    *ppv = NULL;
    if (outer)
        return CLASS_E_NOAGGREGATION;

    This = malloc(sizeof(*This));
    if (!This)
        return E_OUTOFMEMORY;
    This->IURLSearchHook2_iface.lpVtbl = &URLSearchHookVtbl;
    This->ref = 1;
    lock_module();

    hr = IURLSearchHook2_QueryInterface(&This->IURLSearchHook2_iface, riid, ppv);
    IURLSearchHook2_Release(&This->IURLSearchHook2_iface);
    return hr;
}
