/*
 * PROJECT:     LiberNT ieframe
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     Shell interfaces of the Internet shortcut object
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <stdio.h>

#include "ieframe.h"

#include "shlobj.h"
#include "shobjidl.h"
#include "intshcut.h"
#include "shellapi.h"
#include "shlwapi.h"
#include "shlguid.h"
#include "wininet.h"
#include "commctrl.h"
#include "prsht.h"

#include "wine/debug.h"

#include "resource.h"
#include "intshcut_ext.h"

WINE_DEFAULT_DEBUG_CHANNEL(ieframe);

#define INTSHCUT_SECTION L"InternetShortcut"

#define LINK_KEY(pid) { { 0x5cbf2787, 0x48cf, 0x4208, { 0xb9, 0x0e, 0xee, 0x5e, 0x5d, 0x42, 0x02, 0x94 } }, pid }
#define INTSHCUT_KEY(pid) { { 0x000214a0, 0x0000, 0x0000, { 0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46 } }, pid }

struct intshcut_prop
{
    PROPERTYKEY key;
    PROPVARIANT value;
};

struct store_key
{
    PROPERTYKEY key;
    PROPID propid;
    const WCHAR *name;
    VARTYPE vt;
};

static const struct store_key memory_keys[] =
{
    { LINK_KEY(2),  PID_IS_URL,        NULL, VT_LPWSTR },
    { LINK_KEY(37), PID_IS_SHOWCMD,    NULL, VT_I4 },
    { LINK_KEY(36), PID_IS_HOTKEY,     NULL, VT_UI2 },
    { LINK_KEY(26), PID_IS_ICONINDEX,  NULL, VT_I4 },
    { LINK_KEY(35), PID_IS_WORKINGDIR, NULL, VT_LPWSTR },
    { LINK_KEY(25), PID_IS_ICONFILE,   NULL, VT_LPWSTR },
};

static const struct store_key file_keys[] =
{
    { LINK_KEY(2),                       PID_IS_URL,        L"URL",              VT_LPWSTR },
    { INTSHCUT_KEY(PID_IS_URL),          PID_IS_URL,        L"URL",              VT_LPWSTR },
    { INTSHCUT_KEY(PID_IS_WORKINGDIR),   PID_IS_WORKINGDIR, L"WorkingDirectory", VT_LPWSTR },
    { INTSHCUT_KEY(PID_IS_HOTKEY),       PID_IS_HOTKEY,     L"HotKey",           VT_UI2 },
    { INTSHCUT_KEY(PID_IS_SHOWCMD),      PID_IS_SHOWCMD,    L"ShowCommand",      VT_I4 },
    { INTSHCUT_KEY(PID_IS_ICONINDEX),    PID_IS_ICONINDEX,  L"IconIndex",        VT_I4 },
    { INTSHCUT_KEY(PID_IS_ICONFILE),     PID_IS_ICONFILE,   L"IconFile",         VT_LPWSTR },
};

#define IMPL_FROM(type, name) \
static inline InternetShortcut *impl_from_##type(type *iface) \
{ \
    return CONTAINING_RECORD(iface, InternetShortcut, name); \
}

#define FORWARD_IUNKNOWN(type, prefix) \
static HRESULT WINAPI prefix##_QueryInterface(type *iface, REFIID riid, void **ppv) \
{ \
    InternetShortcut *This = impl_from_##type(iface); \
    return IPersistFile_QueryInterface(&This->IPersistFile_iface, riid, ppv); \
} \
static ULONG WINAPI prefix##_AddRef(type *iface) \
{ \
    InternetShortcut *This = impl_from_##type(iface); \
    return IPersistFile_AddRef(&This->IPersistFile_iface); \
} \
static ULONG WINAPI prefix##_Release(type *iface) \
{ \
    InternetShortcut *This = impl_from_##type(iface); \
    return IPersistFile_Release(&This->IPersistFile_iface); \
}

static HRESULT read_prop(InternetShortcut *This, PROPID propid, PROPVARIANT *pv)
{
    IPropertyStorage *stg;
    PROPSPEC ps;
    HRESULT hr;

    PropVariantInit(pv);
    hr = IPropertySetStorage_Open(This->property_set_storage, &FMTID_Intshcut,
                                  STGM_READ | STGM_SHARE_EXCLUSIVE, &stg);
    if (FAILED(hr))
        return hr;

    ps.ulKind = PRSPEC_PROPID;
    ps.propid = propid;
    hr = IPropertyStorage_ReadMultiple(stg, 1, &ps, pv);
    IPropertyStorage_Release(stg);
    return hr;
}

static HRESULT write_prop(InternetShortcut *This, PROPID propid, const PROPVARIANT *pv)
{
    IPropertyStorage *stg;
    PROPSPEC ps;
    HRESULT hr;

    hr = IPropertySetStorage_Open(This->property_set_storage, &FMTID_Intshcut,
                                  STGM_READWRITE | STGM_SHARE_EXCLUSIVE, &stg);
    if (FAILED(hr))
        return hr;

    ps.ulKind = PRSPEC_PROPID;
    ps.propid = propid;
    if (pv->vt == VT_EMPTY)
        hr = IPropertyStorage_DeleteMultiple(stg, 1, &ps);
    else
        hr = IPropertyStorage_WriteMultiple(stg, 1, &ps, pv, PID_FIRST_USABLE);
    IPropertyStorage_Release(stg);

    if (SUCCEEDED(hr))
        This->isDirty = TRUE;
    return hr;
}

static WCHAR *get_string_prop(InternetShortcut *This, PROPID propid)
{
    PROPVARIANT pv;
    WCHAR *ret = NULL;

    if (read_prop(This, propid, &pv) == S_OK && pv.vt == VT_LPWSTR && pv.pwszVal && *pv.pwszVal)
        ret = co_strdupW(pv.pwszVal);
    PropVariantClear(&pv);
    return ret;
}

static LONG get_int_prop(InternetShortcut *This, PROPID propid, LONG def)
{
    PROPVARIANT pv;
    LONG ret = def;

    if (read_prop(This, propid, &pv) == S_OK)
    {
        switch (pv.vt)
        {
            case VT_I4:  ret = pv.lVal; break;
            case VT_UI4: ret = pv.ulVal; break;
            case VT_I2:  ret = pv.iVal; break;
            case VT_UI2: ret = pv.uiVal; break;
        }
    }
    PropVariantClear(&pv);
    return ret;
}

static HRESULT set_string_prop(InternetShortcut *This, PROPID propid, LPCWSTR value)
{
    PROPVARIANT pv;

    PropVariantInit(&pv);
    if (value && *value)
    {
        pv.vt = VT_LPWSTR;
        pv.pwszVal = (LPWSTR)value;
    }
    return write_prop(This, propid, &pv);
}

static HRESULT set_int_prop(InternetShortcut *This, PROPID propid, VARTYPE vt, LONG value)
{
    PROPVARIANT pv;

    PropVariantInit(&pv);
    pv.vt = vt;
    if (vt == VT_UI2)
        pv.uiVal = (USHORT)value;
    else
        pv.lVal = value;
    return write_prop(This, propid, &pv);
}

static HRESULT set_url(InternetShortcut *This, LPCWSTR url)
{
    return This->IUniformResourceLocatorW_iface.lpVtbl->SetURL(&This->IUniformResourceLocatorW_iface, url, 0);
}

static HRESULT set_icon(InternetShortcut *This, LPCWSTR file, int index)
{
    HRESULT hr;

    hr = set_string_prop(This, PID_IS_ICONFILE, file);
    if (FAILED(hr))
        return hr;
    if (file && *file)
        return set_int_prop(This, PID_IS_ICONINDEX, VT_I4, index);
    return set_int_prop(This, PID_IS_ICONINDEX, VT_EMPTY, 0);
}

static void copy_to_w(LPCWSTR src, LPWSTR dst, int cch)
{
    if (dst && cch > 0)
        lstrcpynW(dst, src ? src : L"", cch);
}

static void copy_to_a(LPCWSTR src, LPSTR dst, int cch)
{
    if (!dst || cch <= 0)
        return;
    if (!WideCharToMultiByte(CP_ACP, 0, src ? src : L"", -1, dst, cch, NULL, NULL))
        dst[cch - 1] = 0;
}

static WCHAR *default_description(InternetShortcut *This)
{
    const WCHAR *start, *authority_end, *end, *seg, *colon, *at;
    WCHAR name[MAX_PATH], *ret;
    size_t len;

    if (!This->url)
    {
        if (!LoadStringW(ieframe_instance, IDS_INTSHCUT_NEWNAME, name, ARRAY_SIZE(name) - 4))
            return NULL;
        lstrcatW(name, L".url");
        return co_strdupW(name);
    }

    colon = wcschr(This->url, ':');
    start = colon ? colon + 1 : This->url;
    if (start[0] == '/' && start[1] == '/')
        start += 2;
    authority_end = start + wcscspn(start, L"/?#");
    end = authority_end + wcscspn(authority_end, L"?#");
    while (end > authority_end && end[-1] == '/')
        end--;
    seg = end;
    while (seg > authority_end && seg[-1] != '/')
        seg--;

    name[0] = 0;
    if (seg != end)
    {
        len = min((size_t)(end - seg), ARRAY_SIZE(name) - 5);
        memcpy(name, seg, len * sizeof(WCHAR));
        name[len] = 0;
        PathRemoveExtensionW(name);
    }

    if (!name[0])
    {
        for (at = start; at < authority_end; at++)
        {
            if (*at == '@')
                start = at + 1;
        }
        end = start + wcscspn(start, L":");
        if (end > authority_end)
            end = authority_end;
        len = min((size_t)(end - start), ARRAY_SIZE(name) - 5);
        memcpy(name, start, len * sizeof(WCHAR));
        name[len] = 0;
    }

    lstrcatW(name, L".url");
    ret = co_strdupW(name);
    return ret;
}

static WCHAR *get_description(InternetShortcut *This)
{
    if (This->description)
        return co_strdupW(This->description);
    return default_description(This);
}

static HRESULT get_default_icon(InternetShortcut *This, WCHAR **path, int *index)
{
    WCHAR scheme[INTERNET_MAX_SCHEME_LENGTH + 1];
    WCHAR exe[MAX_PATH];
    PARSEDURLW parsed;
    DWORD len;

    *path = NULL;
    *index = 0;

    parsed.cbSize = sizeof(parsed);
    if (This->url && ParseURLW(This->url, &parsed) == S_OK && parsed.cchProtocol < ARRAY_SIZE(scheme))
    {
        memcpy(scheme, parsed.pszProtocol, parsed.cchProtocol * sizeof(WCHAR));
        scheme[parsed.cchProtocol] = 0;
        len = ARRAY_SIZE(exe);
        if (SUCCEEDED(AssocQueryStringW(ASSOCF_NOTRUNCATE, ASSOCSTR_EXECUTABLE, scheme, L"open", exe, &len)))
        {
            *path = co_strdupW(exe);
            return *path ? S_OK : E_OUTOFMEMORY;
        }
    }

    len = sizeof(exe);
    if (RegGetValueW(HKEY_CLASSES_ROOT, L"InternetShortcut\\DefaultIcon", NULL,
                     RRF_RT_REG_SZ, NULL, exe, &len) != ERROR_SUCCESS)
        return S_FALSE;
    *index = PathParseIconLocationW(exe);
    if (PathFindFileNameW(exe) == exe)
    {
        WCHAR full[MAX_PATH];

        if (!GetSystemDirectoryW(full, ARRAY_SIZE(full)) || !PathAppendW(full, exe))
            return S_FALSE;
        lstrcpynW(exe, full, ARRAY_SIZE(exe));
    }
    *path = co_strdupW(exe);
    return *path ? S_OK : E_OUTOFMEMORY;
}

static HRESULT get_icon_location(InternetShortcut *This, WCHAR **path, int *index, UINT *flags)
{
    WCHAR url[INTERNET_MAX_URL_LENGTH];
    WCHAR *iconfile;
    DWORD len;

    iconfile = get_string_prop(This, PID_IS_ICONFILE);
    if (!iconfile)
    {
        if (flags)
            *flags = 0;
        return get_default_icon(This, path, index);
    }

    *index = get_int_prop(This, PID_IS_ICONINDEX, 0);
    if (flags)
        *flags = GIL_NOTFILENAME;

    len = ARRAY_SIZE(url);
    if (!PathIsURLW(iconfile) && SUCCEEDED(UrlCreateFromPathW(iconfile, url, &len, 0)))
    {
        CoTaskMemFree(iconfile);
        iconfile = co_strdupW(url);
        if (!iconfile)
            return E_OUTOFMEMORY;
    }
    *path = iconfile;
    return S_OK;
}

static HRESULT extract_icon(LPCWSTR file, UINT index, HICON *large_icon, HICON *small_icon, UINT size)
{
    WCHAR path[MAX_PATH];
    DWORD len = ARRAY_SIZE(path);

    if (UrlIsW(file, URLIS_FILEURL))
    {
        if (FAILED(PathCreateFromUrlW(file, path, &len, 0)))
            return S_FALSE;
        file = path;
    }
    else if (PathIsURLW(file))
    {
        return S_FALSE;
    }
    return SHDefExtractIconW(file, (int)index, 0, large_icon, small_icon, size);
}

static HRESULT append_line(WCHAR **buf, size_t *len, size_t *size, LPCWSTR key, LPCWSTR value)
{
    size_t need = wcslen(key) + wcslen(value) + 4;

    if (*len + need >= *size)
    {
        size_t new_size = max(*size * 2, *len + need + 64);
        WCHAR *tmp = realloc(*buf, new_size * sizeof(WCHAR));
        if (!tmp)
            return E_OUTOFMEMORY;
        *buf = tmp;
        *size = new_size;
    }
    *len += swprintf(*buf + *len, *size - *len, L"%s%s\r\n", key, value);
    return S_OK;
}

static HRESULT build_ini(InternetShortcut *This, char **out, DWORD *out_len)
{
    WCHAR *buf = NULL, *str, num[16];
    size_t len = 0, size = 0;
    LONG value;
    int chars;
    HRESULT hr;

    *out = NULL;
    *out_len = 0;

    hr = append_line(&buf, &len, &size, L"[" INTSHCUT_SECTION L"]", L"");
    if (SUCCEEDED(hr))
        hr = append_line(&buf, &len, &size, L"URL=", This->url ? This->url : L"");
    if (SUCCEEDED(hr) && (str = get_string_prop(This, PID_IS_WORKINGDIR)))
    {
        hr = append_line(&buf, &len, &size, L"WorkingDirectory=", str);
        CoTaskMemFree(str);
    }
    if (SUCCEEDED(hr) && (value = get_int_prop(This, PID_IS_HOTKEY, 0)))
    {
        swprintf(num, ARRAY_SIZE(num), L"%ld", value);
        hr = append_line(&buf, &len, &size, L"HotKey=", num);
    }
    if (SUCCEEDED(hr) && (value = get_int_prop(This, PID_IS_SHOWCMD, SW_SHOWNORMAL)) != SW_SHOWNORMAL)
    {
        swprintf(num, ARRAY_SIZE(num), L"%ld", value);
        hr = append_line(&buf, &len, &size, L"ShowCommand=", num);
    }
    if (SUCCEEDED(hr) && (str = get_string_prop(This, PID_IS_ICONFILE)))
    {
        swprintf(num, ARRAY_SIZE(num), L"%ld", get_int_prop(This, PID_IS_ICONINDEX, 0));
        hr = append_line(&buf, &len, &size, L"IconIndex=", num);
        if (SUCCEEDED(hr))
            hr = append_line(&buf, &len, &size, L"IconFile=", str);
        CoTaskMemFree(str);
    }
    if (FAILED(hr))
    {
        free(buf);
        return hr;
    }

    chars = WideCharToMultiByte(CP_ACP, 0, buf, (int)len, NULL, 0, NULL, NULL);
    *out = malloc(chars + 1);
    if (!*out)
    {
        free(buf);
        return E_OUTOFMEMORY;
    }
    WideCharToMultiByte(CP_ACP, 0, buf, (int)len, *out, chars, NULL, NULL);
    (*out)[chars] = 0;
    *out_len = chars;
    free(buf);
    return S_OK;
}

static HRESULT parse_ini(InternetShortcut *This, WCHAR *text)
{
    WCHAR *line, *next, *value, *url = NULL, *workdir = NULL, *iconfile = NULL;
    WCHAR *hotkey = NULL, *showcmd = NULL;
    LONG iconindex = 0;
    BOOL in_section = FALSE;
    HRESULT hr;

    for (line = text; line; line = next)
    {
        next = wcspbrk(line, L"\r\n");
        if (next)
        {
            *next++ = 0;
            next += wcsspn(next, L"\r\n");
        }
        line += wcsspn(line, L" \t");
        if (*line == '[')
        {
            in_section = !_wcsnicmp(line, L"[" INTSHCUT_SECTION L"]", ARRAY_SIZE(INTSHCUT_SECTION) + 1);
            continue;
        }
        if (!in_section || !(value = wcschr(line, '=')))
            continue;
        *value++ = 0;
        StrTrimW(line, L" \t");
        StrTrimW(value, L" \t");

        if (!wcsicmp(line, L"URL"))
            url = value;
        else if (!wcsicmp(line, L"WorkingDirectory"))
            workdir = value;
        else if (!wcsicmp(line, L"IconFile"))
            iconfile = value;
        else if (!wcsicmp(line, L"IconIndex"))
            iconindex = wcstol(value, NULL, 10);
        else if (!wcsicmp(line, L"HotKey"))
            hotkey = value;
        else if (!wcsicmp(line, L"ShowCommand"))
            showcmd = value;
    }

    if (!url || !*url)
        return E_FAIL;

    hr = set_url(This, url);
    if (SUCCEEDED(hr))
        hr = set_string_prop(This, PID_IS_WORKINGDIR, workdir);
    if (SUCCEEDED(hr))
        hr = set_int_prop(This, PID_IS_HOTKEY, (hotkey && *hotkey) ? VT_UI2 : VT_EMPTY,
                          hotkey ? wcstol(hotkey, NULL, 10) : 0);
    if (SUCCEEDED(hr))
        hr = set_int_prop(This, PID_IS_SHOWCMD, (showcmd && *showcmd) ? VT_I4 : VT_EMPTY,
                          showcmd ? wcstol(showcmd, NULL, 10) : 0);
    if (SUCCEEDED(hr))
        hr = set_icon(This, iconfile, iconindex);
    if (SUCCEEDED(hr))
        This->isDirty = FALSE;
    return hr;
}

static HRESULT invoke_default(InternetShortcut *This, HWND hwnd)
{
    URLINVOKECOMMANDINFOW info;

    memset(&info, 0, sizeof(info));
    info.dwcbSize = sizeof(info);
    info.dwFlags = IURL_INVOKECOMMAND_FL_USE_DEFAULT_VERB;
    info.hwndParent = hwnd;
    return This->IUniformResourceLocatorW_iface.lpVtbl->InvokeCommand(&This->IUniformResourceLocatorW_iface, &info);
}

void InternetShortcutExt_LoadFile(InternetShortcut *This, LPCWSTR file)
{
    WCHAR value[MAX_PATH];

    GetPrivateProfileStringW(INTSHCUT_SECTION, L"WorkingDirectory", L"", value, ARRAY_SIZE(value), file);
    set_string_prop(This, PID_IS_WORKINGDIR, value);
    if (GetPrivateProfileStringW(INTSHCUT_SECTION, L"HotKey", L"", value, ARRAY_SIZE(value), file))
        set_int_prop(This, PID_IS_HOTKEY, VT_UI2, wcstol(value, NULL, 10));
    else
        set_int_prop(This, PID_IS_HOTKEY, VT_EMPTY, 0);
    if (GetPrivateProfileStringW(INTSHCUT_SECTION, L"ShowCommand", L"", value, ARRAY_SIZE(value), file))
        set_int_prop(This, PID_IS_SHOWCMD, VT_I4, wcstol(value, NULL, 10));
    else
        set_int_prop(This, PID_IS_SHOWCMD, VT_EMPTY, 0);
    This->isDirty = FALSE;
    This->loaded = TRUE;
}

void InternetShortcutExt_SaveFile(InternetShortcut *This, LPCWSTR file)
{
    WCHAR *workdir, num[16];
    LONG value;

    if ((workdir = get_string_prop(This, PID_IS_WORKINGDIR)))
    {
        WritePrivateProfileStringW(INTSHCUT_SECTION, L"WorkingDirectory", workdir, file);
        CoTaskMemFree(workdir);
    }
    if ((value = get_int_prop(This, PID_IS_HOTKEY, 0)))
    {
        swprintf(num, ARRAY_SIZE(num), L"%ld", value);
        WritePrivateProfileStringW(INTSHCUT_SECTION, L"HotKey", num, file);
    }
    if ((value = get_int_prop(This, PID_IS_SHOWCMD, SW_SHOWNORMAL)) != SW_SHOWNORMAL)
    {
        swprintf(num, ARRAY_SIZE(num), L"%ld", value);
        WritePrivateProfileStringW(INTSHCUT_SECTION, L"ShowCommand", num, file);
    }
}

IMPL_FROM(IPersistStream, IPersistStream_iface)
FORWARD_IUNKNOWN(IPersistStream, PersistStream)

static HRESULT WINAPI PersistStream_GetClassID(IPersistStream *iface, CLSID *clsid)
{
    TRACE("(%p, %p)\n", iface, clsid);

    if (!clsid)
        return E_INVALIDARG;
    *clsid = CLSID_InternetShortcut;
    return S_OK;
}

static HRESULT WINAPI PersistStream_IsDirty(IPersistStream *iface)
{
    InternetShortcut *This = impl_from_IPersistStream(iface);

    TRACE("(%p)\n", iface);

    return This->isDirty ? S_OK : S_FALSE;
}

static HRESULT WINAPI PersistStream_Load(IPersistStream *iface, IStream *stm)
{
    InternetShortcut *This = impl_from_IPersistStream(iface);
    char *data = NULL, *tmp;
    ULONG size = 0, alloc = 0, read;
    WCHAR *text;
    int chars;
    HRESULT hr;

    TRACE("(%p, %p)\n", iface, stm);

    if (!stm)
        return E_INVALIDARG;
    if (This->loaded)
        return E_FAIL;
    if (This->url)
        return STG_E_ACCESSDENIED;

    for (;;)
    {
        if (alloc - size < 4096)
        {
            if (alloc >= 0x100000)
            {
                free(data);
                return E_FAIL;
            }
            alloc = alloc ? alloc * 2 : 4096;
            tmp = realloc(data, alloc);
            if (!tmp)
            {
                free(data);
                return E_OUTOFMEMORY;
            }
            data = tmp;
        }
        read = 0;
        hr = IStream_Read(stm, data + size, alloc - size, &read);
        if (FAILED(hr))
        {
            free(data);
            return hr;
        }
        size += read;
        if (hr == S_FALSE || !read)
            break;
    }

    while (size && !data[size - 1])
        size--;
    chars = size ? MultiByteToWideChar(CP_ACP, 0, data, size, NULL, 0) : 0;
    text = malloc((chars + 1) * sizeof(WCHAR));
    if (!text)
    {
        free(data);
        return E_OUTOFMEMORY;
    }
    if (chars)
        MultiByteToWideChar(CP_ACP, 0, data, size, text, chars);
    text[chars] = 0;
    free(data);

    hr = parse_ini(This, text);
    free(text);
    if (SUCCEEDED(hr))
        This->loaded = TRUE;
    return hr;
}

static HRESULT WINAPI PersistStream_Save(IPersistStream *iface, IStream *stm, BOOL clear_dirty)
{
    static const char header[] = "[" "InternetShortcut" "]\r\nURL=";
    InternetShortcut *This = impl_from_IPersistStream(iface);
    char *data;
    int url_len;
    DWORD len;
    HRESULT hr;

    TRACE("(%p, %p, %d)\n", iface, stm, clear_dirty);

    if (!stm)
        return E_INVALIDARG;
    if (!This->url)
        return E_OUTOFMEMORY;

    url_len = WideCharToMultiByte(CP_ACP, 0, This->url, -1, NULL, 0, NULL, NULL);
    len = sizeof(header) - 1 + url_len - 1 + 3;
    data = malloc(len);
    if (!data)
        return E_OUTOFMEMORY;
    memcpy(data, header, sizeof(header) - 1);
    WideCharToMultiByte(CP_ACP, 0, This->url, -1, data + sizeof(header) - 1, url_len, NULL, NULL);
    memcpy(data + len - 3, "\r\n", 3);
    hr = IStream_Write(stm, data, len, NULL);
    free(data);
    if (SUCCEEDED(hr) && clear_dirty)
        This->isDirty = FALSE;
    return hr;
}

static HRESULT WINAPI PersistStream_GetSizeMax(IPersistStream *iface, ULARGE_INTEGER *size)
{
    InternetShortcut *This = impl_from_IPersistStream(iface);
    char *data;
    DWORD len;
    HRESULT hr;

    TRACE("(%p, %p)\n", iface, size);

    if (!size)
        return E_POINTER;

    size->QuadPart = 0;
    if (!This->url)
        return S_OK;
    hr = build_ini(This, &data, &len);
    if (FAILED(hr))
        return hr;
    free(data);
    size->QuadPart = len;
    return S_OK;
}

static const IPersistStreamVtbl PersistStreamVtbl =
{
    PersistStream_QueryInterface,
    PersistStream_AddRef,
    PersistStream_Release,
    PersistStream_GetClassID,
    PersistStream_IsDirty,
    PersistStream_Load,
    PersistStream_Save,
    PersistStream_GetSizeMax,
};

IMPL_FROM(IShellLinkW, IShellLinkW_iface)
FORWARD_IUNKNOWN(IShellLinkW, ShellLinkW)

static HRESULT WINAPI ShellLinkW_GetPath(IShellLinkW *iface, LPWSTR file, int cch, WIN32_FIND_DATAW *fd, DWORD flags)
{
    InternetShortcut *This = impl_from_IShellLinkW(iface);

    TRACE("(%p, %p, %d, %p, 0x%lx)\n", iface, file, cch, fd, flags);

    if (fd)
        memset(fd, 0, sizeof(*fd));
    copy_to_w(This->url, file, cch);
    return This->url ? S_OK : S_FALSE;
}

static HRESULT WINAPI ShellLinkW_GetIDList(IShellLinkW *iface, PIDLIST_ABSOLUTE *pidl)
{
    InternetShortcut *This = impl_from_IShellLinkW(iface);

    TRACE("(%p, %p)\n", iface, pidl);

    if (!pidl)
        return E_INVALIDARG;
    *pidl = NULL;
    if (!This->url)
        return E_FAIL;
    return SHParseDisplayName(This->url, NULL, pidl, 0, NULL);
}

static HRESULT WINAPI ShellLinkW_SetIDList(IShellLinkW *iface, PCIDLIST_ABSOLUTE pidl)
{
    InternetShortcut *This = impl_from_IShellLinkW(iface);
    WCHAR path[MAX_PATH], url[INTERNET_MAX_URL_LENGTH];
    DWORD len = ARRAY_SIZE(url);
    HRESULT hr;

    TRACE("(%p, %p)\n", iface, pidl);

    if (!pidl || !SHGetPathFromIDListW(pidl, path))
        return E_INVALIDARG;
    hr = UrlCreateFromPathW(path, url, &len, 0);
    if (FAILED(hr))
        return hr;
    return set_url(This, url);
}

static HRESULT WINAPI ShellLinkW_GetDescription(IShellLinkW *iface, LPWSTR name, int cch)
{
    InternetShortcut *This = impl_from_IShellLinkW(iface);
    WCHAR *desc;

    TRACE("(%p, %p, %d)\n", iface, name, cch);

    desc = get_description(This);
    copy_to_w(desc, name, cch);
    CoTaskMemFree(desc);
    return S_OK;
}

static HRESULT WINAPI ShellLinkW_SetDescription(IShellLinkW *iface, LPCWSTR name)
{
    InternetShortcut *This = impl_from_IShellLinkW(iface);
    WCHAR *desc = NULL;

    TRACE("(%p, %s)\n", iface, debugstr_w(name));

    if (name && !(desc = co_strdupW(name)))
        return E_OUTOFMEMORY;
    CoTaskMemFree(This->description);
    This->description = desc;
    return S_OK;
}

static HRESULT WINAPI ShellLinkW_GetWorkingDirectory(IShellLinkW *iface, LPWSTR dir, int cch)
{
    InternetShortcut *This = impl_from_IShellLinkW(iface);
    WCHAR *workdir;

    TRACE("(%p, %p, %d)\n", iface, dir, cch);

    workdir = get_string_prop(This, PID_IS_WORKINGDIR);
    copy_to_w(workdir, dir, cch);
    CoTaskMemFree(workdir);
    return workdir ? S_OK : S_FALSE;
}

static HRESULT WINAPI ShellLinkW_SetWorkingDirectory(IShellLinkW *iface, LPCWSTR dir)
{
    InternetShortcut *This = impl_from_IShellLinkW(iface);

    TRACE("(%p, %s)\n", iface, debugstr_w(dir));

    return set_string_prop(This, PID_IS_WORKINGDIR, dir);
}

static HRESULT WINAPI ShellLinkW_GetArguments(IShellLinkW *iface, LPWSTR args, int cch)
{
    TRACE("(%p, %p, %d)\n", iface, args, cch);

    copy_to_w(NULL, args, cch);
    return E_NOTIMPL;
}

static HRESULT WINAPI ShellLinkW_SetArguments(IShellLinkW *iface, LPCWSTR args)
{
    TRACE("(%p, %s)\n", iface, debugstr_w(args));

    return E_NOTIMPL;
}

static HRESULT WINAPI ShellLinkW_GetHotkey(IShellLinkW *iface, WORD *hotkey)
{
    InternetShortcut *This = impl_from_IShellLinkW(iface);

    TRACE("(%p, %p)\n", iface, hotkey);

    if (!hotkey)
        return E_INVALIDARG;
    *hotkey = (WORD)get_int_prop(This, PID_IS_HOTKEY, 0);
    return S_OK;
}

static HRESULT WINAPI ShellLinkW_SetHotkey(IShellLinkW *iface, WORD hotkey)
{
    InternetShortcut *This = impl_from_IShellLinkW(iface);

    TRACE("(%p, 0x%x)\n", iface, hotkey);

    return set_int_prop(This, PID_IS_HOTKEY, VT_UI2, hotkey);
}

static HRESULT WINAPI ShellLinkW_GetShowCmd(IShellLinkW *iface, int *showcmd)
{
    InternetShortcut *This = impl_from_IShellLinkW(iface);

    TRACE("(%p, %p)\n", iface, showcmd);

    if (!showcmd)
        return E_INVALIDARG;
    *showcmd = get_int_prop(This, PID_IS_SHOWCMD, SW_SHOWNORMAL);
    return S_OK;
}

static HRESULT WINAPI ShellLinkW_SetShowCmd(IShellLinkW *iface, int showcmd)
{
    InternetShortcut *This = impl_from_IShellLinkW(iface);

    TRACE("(%p, %d)\n", iface, showcmd);

    return set_int_prop(This, PID_IS_SHOWCMD, VT_I4, showcmd);
}

static HRESULT WINAPI ShellLinkW_GetIconLocation(IShellLinkW *iface, LPWSTR path, int cch, int *index)
{
    InternetShortcut *This = impl_from_IShellLinkW(iface);
    WCHAR *icon;
    HRESULT hr;

    TRACE("(%p, %p, %d, %p)\n", iface, path, cch, index);

    if (!index)
        return E_INVALIDARG;
    hr = get_icon_location(This, &icon, index, NULL);
    if (FAILED(hr))
        return hr;
    copy_to_w(icon, path, cch);
    CoTaskMemFree(icon);
    return S_OK;
}

static HRESULT WINAPI ShellLinkW_SetIconLocation(IShellLinkW *iface, LPCWSTR path, int index)
{
    InternetShortcut *This = impl_from_IShellLinkW(iface);

    TRACE("(%p, %s, %d)\n", iface, debugstr_w(path), index);

    return set_icon(This, path, index);
}

static HRESULT WINAPI ShellLinkW_SetRelativePath(IShellLinkW *iface, LPCWSTR path, DWORD reserved)
{
    TRACE("(%p, %s, 0x%lx)\n", iface, debugstr_w(path), reserved);

    return E_NOTIMPL;
}

static HRESULT WINAPI ShellLinkW_Resolve(IShellLinkW *iface, HWND hwnd, DWORD flags)
{
    TRACE("(%p, %p, 0x%lx)\n", iface, hwnd, flags);

    return S_OK;
}

static HRESULT WINAPI ShellLinkW_SetPath(IShellLinkW *iface, LPCWSTR file)
{
    InternetShortcut *This = impl_from_IShellLinkW(iface);
    WCHAR url[INTERNET_MAX_URL_LENGTH];
    DWORD len = ARRAY_SIZE(url);

    TRACE("(%p, %s)\n", iface, debugstr_w(file));

    if (!file)
        return E_INVALIDARG;
    if (PathIsURLW(file) || FAILED(UrlCreateFromPathW(file, url, &len, 0)))
        return set_url(This, file);
    return set_url(This, url);
}

static const IShellLinkWVtbl ShellLinkWVtbl =
{
    ShellLinkW_QueryInterface,
    ShellLinkW_AddRef,
    ShellLinkW_Release,
    ShellLinkW_GetPath,
    ShellLinkW_GetIDList,
    ShellLinkW_SetIDList,
    ShellLinkW_GetDescription,
    ShellLinkW_SetDescription,
    ShellLinkW_GetWorkingDirectory,
    ShellLinkW_SetWorkingDirectory,
    ShellLinkW_GetArguments,
    ShellLinkW_SetArguments,
    ShellLinkW_GetHotkey,
    ShellLinkW_SetHotkey,
    ShellLinkW_GetShowCmd,
    ShellLinkW_SetShowCmd,
    ShellLinkW_GetIconLocation,
    ShellLinkW_SetIconLocation,
    ShellLinkW_SetRelativePath,
    ShellLinkW_Resolve,
    ShellLinkW_SetPath,
};

IMPL_FROM(IShellLinkA, IShellLinkA_iface)
FORWARD_IUNKNOWN(IShellLinkA, ShellLinkA)

typedef HRESULT (WINAPI *shelllinkw_setter)(IShellLinkW *iface, LPCWSTR value);

static HRESULT call_setter_a(InternetShortcut *This, shelllinkw_setter setter, LPCSTR value)
{
    WCHAR *valueW = NULL;
    HRESULT hr;

    if (value && !(valueW = co_strdupAtoW(value)))
        return E_OUTOFMEMORY;
    hr = setter(&This->IShellLinkW_iface, valueW);
    CoTaskMemFree(valueW);
    return hr;
}

static HRESULT WINAPI ShellLinkA_GetPath(IShellLinkA *iface, LPSTR file, int cch, WIN32_FIND_DATAA *fd, DWORD flags)
{
    InternetShortcut *This = impl_from_IShellLinkA(iface);

    TRACE("(%p, %p, %d, %p, 0x%lx)\n", iface, file, cch, fd, flags);

    if (fd)
        memset(fd, 0, sizeof(*fd));
    copy_to_a(This->url, file, cch);
    return This->url ? S_OK : S_FALSE;
}

static HRESULT WINAPI ShellLinkA_GetIDList(IShellLinkA *iface, PIDLIST_ABSOLUTE *pidl)
{
    InternetShortcut *This = impl_from_IShellLinkA(iface);
    return ShellLinkW_GetIDList(&This->IShellLinkW_iface, pidl);
}

static HRESULT WINAPI ShellLinkA_SetIDList(IShellLinkA *iface, PCIDLIST_ABSOLUTE pidl)
{
    InternetShortcut *This = impl_from_IShellLinkA(iface);
    return ShellLinkW_SetIDList(&This->IShellLinkW_iface, pidl);
}

static HRESULT WINAPI ShellLinkA_GetDescription(IShellLinkA *iface, LPSTR name, int cch)
{
    InternetShortcut *This = impl_from_IShellLinkA(iface);
    WCHAR *desc;

    TRACE("(%p, %p, %d)\n", iface, name, cch);

    desc = get_description(This);
    copy_to_a(desc, name, cch);
    CoTaskMemFree(desc);
    return S_OK;
}

static HRESULT WINAPI ShellLinkA_SetDescription(IShellLinkA *iface, LPCSTR name)
{
    InternetShortcut *This = impl_from_IShellLinkA(iface);
    return call_setter_a(This, ShellLinkW_SetDescription, name);
}

static HRESULT WINAPI ShellLinkA_GetWorkingDirectory(IShellLinkA *iface, LPSTR dir, int cch)
{
    InternetShortcut *This = impl_from_IShellLinkA(iface);
    WCHAR *workdir;

    TRACE("(%p, %p, %d)\n", iface, dir, cch);

    workdir = get_string_prop(This, PID_IS_WORKINGDIR);
    copy_to_a(workdir, dir, cch);
    CoTaskMemFree(workdir);
    return workdir ? S_OK : S_FALSE;
}

static HRESULT WINAPI ShellLinkA_SetWorkingDirectory(IShellLinkA *iface, LPCSTR dir)
{
    InternetShortcut *This = impl_from_IShellLinkA(iface);
    return call_setter_a(This, ShellLinkW_SetWorkingDirectory, dir);
}

static HRESULT WINAPI ShellLinkA_GetArguments(IShellLinkA *iface, LPSTR args, int cch)
{
    TRACE("(%p, %p, %d)\n", iface, args, cch);

    copy_to_a(NULL, args, cch);
    return E_NOTIMPL;
}

static HRESULT WINAPI ShellLinkA_SetArguments(IShellLinkA *iface, LPCSTR args)
{
    TRACE("(%p, %s)\n", iface, debugstr_a(args));

    return E_NOTIMPL;
}

static HRESULT WINAPI ShellLinkA_GetHotkey(IShellLinkA *iface, WORD *hotkey)
{
    InternetShortcut *This = impl_from_IShellLinkA(iface);
    return ShellLinkW_GetHotkey(&This->IShellLinkW_iface, hotkey);
}

static HRESULT WINAPI ShellLinkA_SetHotkey(IShellLinkA *iface, WORD hotkey)
{
    InternetShortcut *This = impl_from_IShellLinkA(iface);
    return ShellLinkW_SetHotkey(&This->IShellLinkW_iface, hotkey);
}

static HRESULT WINAPI ShellLinkA_GetShowCmd(IShellLinkA *iface, int *showcmd)
{
    InternetShortcut *This = impl_from_IShellLinkA(iface);
    return ShellLinkW_GetShowCmd(&This->IShellLinkW_iface, showcmd);
}

static HRESULT WINAPI ShellLinkA_SetShowCmd(IShellLinkA *iface, int showcmd)
{
    InternetShortcut *This = impl_from_IShellLinkA(iface);
    return ShellLinkW_SetShowCmd(&This->IShellLinkW_iface, showcmd);
}

static HRESULT WINAPI ShellLinkA_GetIconLocation(IShellLinkA *iface, LPSTR path, int cch, int *index)
{
    InternetShortcut *This = impl_from_IShellLinkA(iface);
    WCHAR *icon;
    HRESULT hr;

    TRACE("(%p, %p, %d, %p)\n", iface, path, cch, index);

    if (!index)
        return E_INVALIDARG;
    hr = get_icon_location(This, &icon, index, NULL);
    if (FAILED(hr))
        return hr;
    copy_to_a(icon, path, cch);
    CoTaskMemFree(icon);
    return S_OK;
}

static HRESULT WINAPI ShellLinkA_SetIconLocation(IShellLinkA *iface, LPCSTR path, int index)
{
    InternetShortcut *This = impl_from_IShellLinkA(iface);
    WCHAR *pathW = NULL;
    HRESULT hr;

    TRACE("(%p, %s, %d)\n", iface, debugstr_a(path), index);

    if (path && !(pathW = co_strdupAtoW(path)))
        return E_OUTOFMEMORY;
    hr = set_icon(This, pathW, index);
    CoTaskMemFree(pathW);
    return hr;
}

static HRESULT WINAPI ShellLinkA_SetRelativePath(IShellLinkA *iface, LPCSTR path, DWORD reserved)
{
    TRACE("(%p, %s, 0x%lx)\n", iface, debugstr_a(path), reserved);

    return E_NOTIMPL;
}

static HRESULT WINAPI ShellLinkA_Resolve(IShellLinkA *iface, HWND hwnd, DWORD flags)
{
    TRACE("(%p, %p, 0x%lx)\n", iface, hwnd, flags);

    return S_OK;
}

static HRESULT WINAPI ShellLinkA_SetPath(IShellLinkA *iface, LPCSTR file)
{
    InternetShortcut *This = impl_from_IShellLinkA(iface);
    return call_setter_a(This, ShellLinkW_SetPath, file);
}

static const IShellLinkAVtbl ShellLinkAVtbl =
{
    ShellLinkA_QueryInterface,
    ShellLinkA_AddRef,
    ShellLinkA_Release,
    ShellLinkA_GetPath,
    ShellLinkA_GetIDList,
    ShellLinkA_SetIDList,
    ShellLinkA_GetDescription,
    ShellLinkA_SetDescription,
    ShellLinkA_GetWorkingDirectory,
    ShellLinkA_SetWorkingDirectory,
    ShellLinkA_GetArguments,
    ShellLinkA_SetArguments,
    ShellLinkA_GetHotkey,
    ShellLinkA_SetHotkey,
    ShellLinkA_GetShowCmd,
    ShellLinkA_SetShowCmd,
    ShellLinkA_GetIconLocation,
    ShellLinkA_SetIconLocation,
    ShellLinkA_SetRelativePath,
    ShellLinkA_Resolve,
    ShellLinkA_SetPath,
};

IMPL_FROM(IExtractIconW, IExtractIconW_iface)
FORWARD_IUNKNOWN(IExtractIconW, ExtractIconW)

static HRESULT WINAPI ExtractIconW_GetIconLocation(IExtractIconW *iface, UINT flags, LPWSTR file, UINT cch, int *index, UINT *out_flags)
{
    InternetShortcut *This = impl_from_IExtractIconW(iface);
    WCHAR *icon;
    UINT gil;
    HRESULT hr;

    TRACE("(%p, 0x%x, %p, %u, %p, %p)\n", iface, flags, file, cch, index, out_flags);

    if (!index || !out_flags)
        return E_INVALIDARG;
    hr = get_icon_location(This, &icon, index, &gil);
    if (hr != S_OK)
        return hr;
    copy_to_w(icon, file, cch);
    CoTaskMemFree(icon);
    *out_flags = gil;
    return S_OK;
}

static HRESULT WINAPI ExtractIconW_Extract(IExtractIconW *iface, LPCWSTR file, UINT index, HICON *large_icon, HICON *small_icon, UINT size)
{
    TRACE("(%p, %s, %u, %p, %p, %u)\n", iface, debugstr_w(file), index, large_icon, small_icon, size);

    return extract_icon(file, index, large_icon, small_icon, size);
}

static const IExtractIconWVtbl ExtractIconWVtbl =
{
    ExtractIconW_QueryInterface,
    ExtractIconW_AddRef,
    ExtractIconW_Release,
    ExtractIconW_GetIconLocation,
    ExtractIconW_Extract,
};

IMPL_FROM(IExtractIconA, IExtractIconA_iface)
FORWARD_IUNKNOWN(IExtractIconA, ExtractIconA)

static HRESULT WINAPI ExtractIconA_GetIconLocation(IExtractIconA *iface, UINT flags, LPSTR file, UINT cch, int *index, UINT *out_flags)
{
    InternetShortcut *This = impl_from_IExtractIconA(iface);
    WCHAR *icon;
    UINT gil;
    HRESULT hr;

    TRACE("(%p, 0x%x, %p, %u, %p, %p)\n", iface, flags, file, cch, index, out_flags);

    if (!index || !out_flags)
        return E_INVALIDARG;
    hr = get_icon_location(This, &icon, index, &gil);
    if (hr != S_OK)
        return hr;
    copy_to_a(icon, file, cch);
    CoTaskMemFree(icon);
    *out_flags = gil;
    return S_OK;
}

static HRESULT WINAPI ExtractIconA_Extract(IExtractIconA *iface, LPCSTR file, UINT index, HICON *large_icon, HICON *small_icon, UINT size)
{
    WCHAR *fileW;
    HRESULT hr;

    TRACE("(%p, %s, %u, %p, %p, %u)\n", iface, debugstr_a(file), index, large_icon, small_icon, size);

    if (!file)
        return E_INVALIDARG;
    if (!(fileW = co_strdupAtoW(file)))
        return E_OUTOFMEMORY;
    hr = extract_icon(fileW, index, large_icon, small_icon, size);
    CoTaskMemFree(fileW);
    return hr;
}

static const IExtractIconAVtbl ExtractIconAVtbl =
{
    ExtractIconA_QueryInterface,
    ExtractIconA_AddRef,
    ExtractIconA_Release,
    ExtractIconA_GetIconLocation,
    ExtractIconA_Extract,
};

IMPL_FROM(IQueryInfo, IQueryInfo_iface)
FORWARD_IUNKNOWN(IQueryInfo, QueryInfo)

static HRESULT WINAPI QueryInfo_GetInfoTip(IQueryInfo *iface, DWORD flags, WCHAR **tip)
{
    InternetShortcut *This = impl_from_IQueryInfo(iface);

    TRACE("(%p, 0x%lx, %p)\n", iface, flags, tip);

    if (!tip)
        return E_INVALIDARG;
    *tip = NULL;
    if (!This->url)
        return S_FALSE;
    *tip = co_strdupW(This->url);
    return *tip ? S_OK : E_OUTOFMEMORY;
}

static HRESULT WINAPI QueryInfo_GetInfoFlags(IQueryInfo *iface, DWORD *flags)
{
    TRACE("(%p, %p)\n", iface, flags);

    if (flags)
        *flags = 0;
    return E_NOTIMPL;
}

static const IQueryInfoVtbl QueryInfoVtbl =
{
    QueryInfo_QueryInterface,
    QueryInfo_AddRef,
    QueryInfo_Release,
    QueryInfo_GetInfoTip,
    QueryInfo_GetInfoFlags,
};

static HRESULT set_referent(InternetShortcut *This, LPCWSTR referent)
{
    WCHAR guessed[INTERNET_MAX_URL_LENGTH], canonical[INTERNET_MAX_URL_LENGTH];
    DWORD len;
    HRESULT hr;

    if (!referent)
        return E_INVALIDARG;
    if (PathIsURLW(referent))
        return set_url(This, referent);
    if (!PathIsRelativeW(referent) || PathIsUNCW(referent))
        return S_FALSE;

    len = ARRAY_SIZE(guessed);
    hr = UrlApplySchemeW(referent, guessed, &len, URL_APPLY_GUESSSCHEME);
    if (hr != S_OK)
        return S_FALSE;
    len = ARRAY_SIZE(canonical);
    if (FAILED(UrlCanonicalizeW(guessed, canonical, &len, 0)))
        return set_url(This, guessed);
    return set_url(This, canonical);
}

static HRESULT get_new_name(LPWSTR name, int cch)
{
    if (!name || cch <= 0)
        return E_INVALIDARG;
    if (!LoadStringW(ieframe_instance, IDS_INTSHCUT_NEWNAME, name, cch))
        return HRESULT_FROM_WIN32(GetLastError());
    return S_OK;
}

IMPL_FROM(INewShortcutHookW, INewShortcutHookW_iface)
FORWARD_IUNKNOWN(INewShortcutHookW, NewShortcutHookW)

static HRESULT WINAPI NewShortcutHookW_SetReferent(INewShortcutHookW *iface, LPCWSTR referent, HWND hwnd)
{
    InternetShortcut *This = impl_from_INewShortcutHookW(iface);

    TRACE("(%p, %s, %p)\n", iface, debugstr_w(referent), hwnd);

    return set_referent(This, referent);
}

static HRESULT WINAPI NewShortcutHookW_GetReferent(INewShortcutHookW *iface, LPWSTR referent, int cch)
{
    InternetShortcut *This = impl_from_INewShortcutHookW(iface);

    TRACE("(%p, %p, %d)\n", iface, referent, cch);

    copy_to_w(This->url, referent, cch);
    return S_OK;
}

static HRESULT WINAPI NewShortcutHookW_SetFolder(INewShortcutHookW *iface, LPCWSTR folder)
{
    InternetShortcut *This = impl_from_INewShortcutHookW(iface);
    WCHAR *copy = NULL;

    TRACE("(%p, %s)\n", iface, debugstr_w(folder));

    if (folder && !(copy = co_strdupW(folder)))
        return E_OUTOFMEMORY;
    CoTaskMemFree(This->folder);
    This->folder = copy;
    return S_OK;
}

static HRESULT WINAPI NewShortcutHookW_GetFolder(INewShortcutHookW *iface, LPWSTR folder, int cch)
{
    InternetShortcut *This = impl_from_INewShortcutHookW(iface);

    TRACE("(%p, %p, %d)\n", iface, folder, cch);

    copy_to_w(This->folder, folder, cch);
    return This->folder ? S_OK : S_FALSE;
}

static HRESULT WINAPI NewShortcutHookW_GetName(INewShortcutHookW *iface, LPWSTR name, int cch)
{
    TRACE("(%p, %p, %d)\n", iface, name, cch);

    return get_new_name(name, cch);
}

static HRESULT WINAPI NewShortcutHookW_GetExtension(INewShortcutHookW *iface, LPWSTR ext, int cch)
{
    TRACE("(%p, %p, %d)\n", iface, ext, cch);

    if (!ext || cch <= 0)
        return E_INVALIDARG;
    copy_to_w(L".url", ext, cch);
    return S_OK;
}

static const INewShortcutHookWVtbl NewShortcutHookWVtbl =
{
    NewShortcutHookW_QueryInterface,
    NewShortcutHookW_AddRef,
    NewShortcutHookW_Release,
    NewShortcutHookW_SetReferent,
    NewShortcutHookW_GetReferent,
    NewShortcutHookW_SetFolder,
    NewShortcutHookW_GetFolder,
    NewShortcutHookW_GetName,
    NewShortcutHookW_GetExtension,
};

IMPL_FROM(INewShortcutHookA, INewShortcutHookA_iface)
FORWARD_IUNKNOWN(INewShortcutHookA, NewShortcutHookA)

static HRESULT WINAPI NewShortcutHookA_SetReferent(INewShortcutHookA *iface, LPCSTR referent, HWND hwnd)
{
    InternetShortcut *This = impl_from_INewShortcutHookA(iface);
    WCHAR *referentW;
    HRESULT hr;

    TRACE("(%p, %s, %p)\n", iface, debugstr_a(referent), hwnd);

    if (!referent)
        return E_INVALIDARG;
    if (!(referentW = co_strdupAtoW(referent)))
        return E_OUTOFMEMORY;
    hr = set_referent(This, referentW);
    CoTaskMemFree(referentW);
    return hr;
}

static HRESULT WINAPI NewShortcutHookA_GetReferent(INewShortcutHookA *iface, LPSTR referent, int cch)
{
    InternetShortcut *This = impl_from_INewShortcutHookA(iface);

    TRACE("(%p, %p, %d)\n", iface, referent, cch);

    copy_to_a(This->url, referent, cch);
    return S_OK;
}

static HRESULT WINAPI NewShortcutHookA_SetFolder(INewShortcutHookA *iface, LPCSTR folder)
{
    InternetShortcut *This = impl_from_INewShortcutHookA(iface);
    WCHAR *folderW = NULL;
    HRESULT hr;

    TRACE("(%p, %s)\n", iface, debugstr_a(folder));

    if (folder && !(folderW = co_strdupAtoW(folder)))
        return E_OUTOFMEMORY;
    hr = NewShortcutHookW_SetFolder(&This->INewShortcutHookW_iface, folderW);
    CoTaskMemFree(folderW);
    return hr;
}

static HRESULT WINAPI NewShortcutHookA_GetFolder(INewShortcutHookA *iface, LPSTR folder, int cch)
{
    InternetShortcut *This = impl_from_INewShortcutHookA(iface);

    TRACE("(%p, %p, %d)\n", iface, folder, cch);

    copy_to_a(This->folder, folder, cch);
    return This->folder ? S_OK : S_FALSE;
}

static HRESULT WINAPI NewShortcutHookA_GetName(INewShortcutHookA *iface, LPSTR name, int cch)
{
    WCHAR nameW[MAX_PATH];
    HRESULT hr;

    TRACE("(%p, %p, %d)\n", iface, name, cch);

    if (!name || cch <= 0)
        return E_INVALIDARG;
    hr = get_new_name(nameW, ARRAY_SIZE(nameW));
    if (SUCCEEDED(hr))
        copy_to_a(nameW, name, cch);
    return hr;
}

static HRESULT WINAPI NewShortcutHookA_GetExtension(INewShortcutHookA *iface, LPSTR ext, int cch)
{
    TRACE("(%p, %p, %d)\n", iface, ext, cch);

    if (!ext || cch <= 0)
        return E_INVALIDARG;
    lstrcpynA(ext, ".url", cch);
    return S_OK;
}

static const INewShortcutHookAVtbl NewShortcutHookAVtbl =
{
    NewShortcutHookA_QueryInterface,
    NewShortcutHookA_AddRef,
    NewShortcutHookA_Release,
    NewShortcutHookA_SetReferent,
    NewShortcutHookA_GetReferent,
    NewShortcutHookA_SetFolder,
    NewShortcutHookA_GetFolder,
    NewShortcutHookA_GetName,
    NewShortcutHookA_GetExtension,
};

IMPL_FROM(IShellExtInit, IShellExtInit_iface)
FORWARD_IUNKNOWN(IShellExtInit, ShellExtInit)

static HRESULT WINAPI ShellExtInit_Initialize(IShellExtInit *iface, PCIDLIST_ABSOLUTE folder, IDataObject *obj, HKEY progid)
{
    InternetShortcut *This = impl_from_IShellExtInit(iface);
    FORMATETC fmt = { CF_HDROP, NULL, DVASPECT_CONTENT, -1, TYMED_HGLOBAL };
    WCHAR path[MAX_PATH];
    STGMEDIUM medium;
    HRESULT hr;
    UINT len;

    TRACE("(%p, %p, %p, %p)\n", iface, folder, obj, progid);

    if (!obj)
        return E_INVALIDARG;

    hr = IDataObject_GetData(obj, &fmt, &medium);
    if (FAILED(hr))
        return hr;
    len = DragQueryFileW(medium.hGlobal, 0, path, ARRAY_SIZE(path));
    ReleaseStgMedium(&medium);
    if (!len)
        return E_FAIL;

    return IPersistFile_Load(&This->IPersistFile_iface, path, STGM_READ);
}

static const IShellExtInitVtbl ShellExtInitVtbl =
{
    ShellExtInit_QueryInterface,
    ShellExtInit_AddRef,
    ShellExtInit_Release,
    ShellExtInit_Initialize,
};

IMPL_FROM(IContextMenu2, IContextMenu2_iface)
FORWARD_IUNKNOWN(IContextMenu2, ContextMenu2)

static HRESULT WINAPI ContextMenu2_QueryContextMenu(IContextMenu2 *iface, HMENU menu, UINT index, UINT first, UINT last, UINT flags)
{
    WCHAR text[64];

    TRACE("(%p, %p, %u, %u, %u, 0x%x)\n", iface, menu, index, first, last, flags);

    if (first > last)
        return MAKE_HRESULT(SEVERITY_SUCCESS, 0, 0);
    if (!LoadStringW(ieframe_instance, IDS_INTSHCUT_OPEN, text, ARRAY_SIZE(text)))
        return HRESULT_FROM_WIN32(GetLastError());
    if (!InsertMenuW(menu, index, MF_BYPOSITION | MF_STRING, first, text))
        return HRESULT_FROM_WIN32(GetLastError());
    SetMenuDefaultItem(menu, first, FALSE);
    return MAKE_HRESULT(SEVERITY_SUCCESS, 0, 1);
}

static HRESULT WINAPI ContextMenu2_InvokeCommand(IContextMenu2 *iface, LPCMINVOKECOMMANDINFO info)
{
    InternetShortcut *This = impl_from_IContextMenu2(iface);
    const CMINVOKECOMMANDINFOEX *infoex = (const CMINVOKECOMMANDINFOEX *)info;

    TRACE("(%p, %p)\n", iface, info);

    if (!info)
        return E_INVALIDARG;

    if (info->cbSize >= sizeof(CMINVOKECOMMANDINFOEX) && (info->fMask & CMIC_MASK_UNICODE) &&
        !IS_INTRESOURCE(infoex->lpVerbW))
    {
        if (wcsicmp(infoex->lpVerbW, L"open"))
            return E_INVALIDARG;
    }
    else if (IS_INTRESOURCE(info->lpVerb))
    {
        if (LOWORD(info->lpVerb) != 0)
            return E_INVALIDARG;
    }
    else if (lstrcmpiA(info->lpVerb, "open"))
    {
        return E_INVALIDARG;
    }

    return invoke_default(This, info->hwnd);
}

static HRESULT WINAPI ContextMenu2_GetCommandString(IContextMenu2 *iface, UINT_PTR cmd, UINT type, UINT *reserved, LPSTR name, UINT cch)
{
    WCHAR help[128];

    TRACE("(%p, %Iu, 0x%x, %p, %p, %u)\n", iface, cmd, type, reserved, name, cch);

    if (cmd != 0)
        return E_INVALIDARG;

    switch (type)
    {
        case GCS_VALIDATEA:
        case GCS_VALIDATEW:
            return S_OK;
        case GCS_VERBA:
            if (!name || !cch)
                return E_INVALIDARG;
            lstrcpynA(name, "open", cch);
            return S_OK;
        case GCS_VERBW:
            if (!name || !cch)
                return E_INVALIDARG;
            lstrcpynW((LPWSTR)name, L"open", cch);
            return S_OK;
        case GCS_HELPTEXTA:
        case GCS_HELPTEXTW:
            if (!name || !cch)
                return E_INVALIDARG;
            if (!LoadStringW(ieframe_instance, IDS_INTSHCUT_OPEN_HELP, help, ARRAY_SIZE(help)))
                return HRESULT_FROM_WIN32(GetLastError());
            if (type == GCS_HELPTEXTW)
                lstrcpynW((LPWSTR)name, help, cch);
            else
                copy_to_a(help, name, cch);
            return S_OK;
    }
    return E_NOTIMPL;
}

static HRESULT WINAPI ContextMenu2_HandleMenuMsg(IContextMenu2 *iface, UINT msg, WPARAM wparam, LPARAM lparam)
{
    TRACE("(%p, 0x%x, %Ix, %Ix)\n", iface, msg, wparam, lparam);

    return E_NOTIMPL;
}

static const IContextMenu2Vtbl ContextMenu2Vtbl =
{
    ContextMenu2_QueryInterface,
    ContextMenu2_AddRef,
    ContextMenu2_Release,
    ContextMenu2_QueryContextMenu,
    ContextMenu2_InvokeCommand,
    ContextMenu2_GetCommandString,
    ContextMenu2_HandleMenuMsg,
};

enum intshcut_format
{
    FORMAT_FILECONTENTS,
    FORMAT_FILEDESCRIPTORW,
    FORMAT_FILEDESCRIPTORA,
    FORMAT_URLW,
    FORMAT_UNICODETEXT,
    FORMAT_URLA,
    FORMAT_TEXT,
    FORMAT_COUNT
};

static void get_formats(FORMATETC *fmt)
{
    static const WCHAR *names[] =
    {
        L"FileContents", L"FileGroupDescriptorW", L"FileGroupDescriptor",
        L"UniformResourceLocatorW", NULL, L"UniformResourceLocator", NULL
    };
    int i;

    for (i = 0; i < FORMAT_COUNT; i++)
    {
        fmt[i].cfFormat = names[i] ? RegisterClipboardFormatW(names[i]) : 0;
        fmt[i].ptd = NULL;
        fmt[i].dwAspect = DVASPECT_CONTENT;
        fmt[i].lindex = -1;
        fmt[i].tymed = TYMED_HGLOBAL;
    }
    fmt[FORMAT_UNICODETEXT].cfFormat = CF_UNICODETEXT;
    fmt[FORMAT_TEXT].cfFormat = CF_TEXT;
    fmt[FORMAT_FILECONTENTS].lindex = 0;
    fmt[FORMAT_FILECONTENTS].tymed = TYMED_HGLOBAL | TYMED_ISTREAM;
}

static int find_format(const FORMATETC *query)
{
    FORMATETC fmt[FORMAT_COUNT];
    int i;

    get_formats(fmt);
    for (i = 0; i < FORMAT_COUNT; i++)
    {
        if (fmt[i].cfFormat == query->cfFormat && (fmt[i].tymed & query->tymed))
            return i;
    }
    return -1;
}

static HGLOBAL alloc_global(const void *data, SIZE_T size)
{
    HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, size);
    void *ptr;

    if (mem && data && (ptr = GlobalLock(mem)))
    {
        memcpy(ptr, data, size);
        GlobalUnlock(mem);
    }
    return mem;
}

static HRESULT get_descriptor(InternetShortcut *This, BOOL unicode, HGLOBAL *out)
{
    WCHAR *desc;
    char *ini;
    DWORD ini_len = 0;
    HGLOBAL mem;
    HRESULT hr;

    if (This->url)
    {
        hr = build_ini(This, &ini, &ini_len);
        if (FAILED(hr))
            return hr;
        free(ini);
    }

    desc = get_description(This);
    if (unicode)
    {
        FILEGROUPDESCRIPTORW *fgd;

        mem = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, sizeof(*fgd));
        if (mem && (fgd = GlobalLock(mem)))
        {
            fgd->cItems = 1;
            fgd->fgd[0].dwFlags = FD_LINKUI | FD_FILESIZE;
            fgd->fgd[0].nFileSizeLow = ini_len;
            copy_to_w(desc, fgd->fgd[0].cFileName, ARRAY_SIZE(fgd->fgd[0].cFileName));
            GlobalUnlock(mem);
        }
    }
    else
    {
        FILEGROUPDESCRIPTORA *fgd;

        mem = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, sizeof(*fgd));
        if (mem && (fgd = GlobalLock(mem)))
        {
            fgd->cItems = 1;
            fgd->fgd[0].dwFlags = FD_LINKUI | FD_FILESIZE;
            fgd->fgd[0].nFileSizeLow = ini_len;
            copy_to_a(desc, fgd->fgd[0].cFileName, ARRAY_SIZE(fgd->fgd[0].cFileName));
            GlobalUnlock(mem);
        }
    }
    CoTaskMemFree(desc);

    *out = mem;
    return mem ? S_OK : E_OUTOFMEMORY;
}

IMPL_FROM(IDataObject, IDataObject_iface)
FORWARD_IUNKNOWN(IDataObject, DataObject)

static HRESULT WINAPI DataObject_GetData(IDataObject *iface, FORMATETC *query, STGMEDIUM *medium)
{
    InternetShortcut *This = impl_from_IDataObject(iface);
    HGLOBAL mem = NULL;
    char *ini, *urlA;
    DWORD len;
    HRESULT hr = S_OK;
    int format;

    TRACE("(%p, %p, %p)\n", iface, query, medium);

    if (!query || !medium)
        return E_INVALIDARG;

    format = find_format(query);
    if (format < 0)
        return DV_E_FORMATETC;

    switch (format)
    {
        case FORMAT_FILECONTENTS:
            if (This->url)
            {
                hr = build_ini(This, &ini, &len);
                if (FAILED(hr))
                    return hr;
                mem = alloc_global(ini, len);
                free(ini);
            }
            else
            {
                len = 1;
                mem = alloc_global(NULL, len);
            }
            if (mem && !(query->tymed & TYMED_HGLOBAL))
            {
                ULARGE_INTEGER size;

                hr = CreateStreamOnHGlobal(mem, TRUE, &medium->pstm);
                if (FAILED(hr))
                {
                    GlobalFree(mem);
                    return hr;
                }
                size.QuadPart = len;
                IStream_SetSize(medium->pstm, size);
                medium->tymed = TYMED_ISTREAM;
                medium->pUnkForRelease = NULL;
                return S_OK;
            }
            break;
        case FORMAT_FILEDESCRIPTORW:
        case FORMAT_FILEDESCRIPTORA:
            hr = get_descriptor(This, format == FORMAT_FILEDESCRIPTORW, &mem);
            if (FAILED(hr))
                return hr;
            break;
        case FORMAT_URLW:
        case FORMAT_UNICODETEXT:
            mem = alloc_global(This->url ? This->url : L"", (wcslen(This->url ? This->url : L"") + 1) * sizeof(WCHAR));
            if (!This->url)
                hr = S_FALSE;
            break;
        case FORMAT_URLA:
        case FORMAT_TEXT:
            urlA = co_strdupWtoA(This->url ? This->url : L"");
            if (!urlA)
                return E_OUTOFMEMORY;
            mem = alloc_global(urlA, strlen(urlA) + 1);
            CoTaskMemFree(urlA);
            if (!This->url)
                hr = S_FALSE;
            break;
    }

    if (!mem)
        return E_OUTOFMEMORY;
    medium->tymed = TYMED_HGLOBAL;
    medium->hGlobal = mem;
    medium->pUnkForRelease = NULL;
    return hr;
}

static HRESULT WINAPI DataObject_GetDataHere(IDataObject *iface, FORMATETC *query, STGMEDIUM *medium)
{
    TRACE("(%p, %p, %p)\n", iface, query, medium);

    return E_NOTIMPL;
}

static HRESULT WINAPI DataObject_QueryGetData(IDataObject *iface, FORMATETC *query)
{
    TRACE("(%p, %p)\n", iface, query);

    if (!query)
        return E_INVALIDARG;
    return find_format(query) >= 0 ? S_OK : DV_E_FORMATETC;
}

static HRESULT WINAPI DataObject_GetCanonicalFormatEtc(IDataObject *iface, FORMATETC *in, FORMATETC *out)
{
    TRACE("(%p, %p, %p)\n", iface, in, out);

    if (!in || !out)
        return E_INVALIDARG;
    *out = *in;
    out->ptd = NULL;
    return DATA_S_SAMEFORMATETC;
}

static HRESULT WINAPI DataObject_SetData(IDataObject *iface, FORMATETC *fmt, STGMEDIUM *medium, BOOL release)
{
    TRACE("(%p, %p, %p, %d)\n", iface, fmt, medium, release);

    return E_NOTIMPL;
}

static HRESULT WINAPI DataObject_EnumFormatEtc(IDataObject *iface, DWORD direction, IEnumFORMATETC **enumfmt)
{
    FORMATETC fmt[FORMAT_COUNT];

    TRACE("(%p, 0x%lx, %p)\n", iface, direction, enumfmt);

    if (!enumfmt)
        return E_INVALIDARG;
    *enumfmt = NULL;
    if (direction != DATADIR_GET)
        return E_NOTIMPL;

    get_formats(fmt);
    fmt[FORMAT_FILECONTENTS].tymed = TYMED_HGLOBAL;
    return SHCreateStdEnumFmtEtc(FORMAT_COUNT, fmt, enumfmt);
}

static HRESULT WINAPI DataObject_DAdvise(IDataObject *iface, FORMATETC *fmt, DWORD flags, IAdviseSink *sink, DWORD *connection)
{
    TRACE("(%p, %p, 0x%lx, %p, %p)\n", iface, fmt, flags, sink, connection);

    return OLE_E_ADVISENOTSUPPORTED;
}

static HRESULT WINAPI DataObject_DUnadvise(IDataObject *iface, DWORD connection)
{
    TRACE("(%p, %lu)\n", iface, connection);

    return OLE_E_ADVISENOTSUPPORTED;
}

static HRESULT WINAPI DataObject_EnumDAdvise(IDataObject *iface, IEnumSTATDATA **enumadvise)
{
    TRACE("(%p, %p)\n", iface, enumadvise);

    return OLE_E_ADVISENOTSUPPORTED;
}

static const IDataObjectVtbl DataObjectVtbl =
{
    DataObject_QueryInterface,
    DataObject_AddRef,
    DataObject_Release,
    DataObject_GetData,
    DataObject_GetDataHere,
    DataObject_QueryGetData,
    DataObject_GetCanonicalFormatEtc,
    DataObject_SetData,
    DataObject_EnumFormatEtc,
    DataObject_DAdvise,
    DataObject_DUnadvise,
    DataObject_EnumDAdvise,
};

IMPL_FROM(IObjectWithSite, IObjectWithSite_iface)
FORWARD_IUNKNOWN(IObjectWithSite, ObjectWithSite)

static HRESULT WINAPI ObjectWithSite_SetSite(IObjectWithSite *iface, IUnknown *site)
{
    InternetShortcut *This = impl_from_IObjectWithSite(iface);

    TRACE("(%p, %p)\n", iface, site);

    if (site)
        IUnknown_AddRef(site);
    if (This->site)
        IUnknown_Release(This->site);
    This->site = site;
    return S_OK;
}

static HRESULT WINAPI ObjectWithSite_GetSite(IObjectWithSite *iface, REFIID riid, void **site)
{
    InternetShortcut *This = impl_from_IObjectWithSite(iface);

    TRACE("(%p, %s, %p)\n", iface, debugstr_guid(riid), site);

    if (!site)
        return E_POINTER;
    *site = NULL;
    if (!This->site)
        return E_FAIL;
    return IUnknown_QueryInterface(This->site, riid, site);
}

static const IObjectWithSiteVtbl ObjectWithSiteVtbl =
{
    ObjectWithSite_QueryInterface,
    ObjectWithSite_AddRef,
    ObjectWithSite_Release,
    ObjectWithSite_SetSite,
    ObjectWithSite_GetSite,
};

IMPL_FROM(IServiceProvider, IServiceProvider_iface)
FORWARD_IUNKNOWN(IServiceProvider, ServiceProvider)

static HRESULT WINAPI ServiceProvider_QueryService(IServiceProvider *iface, REFGUID service, REFIID riid, void **ppv)
{
    InternetShortcut *This = impl_from_IServiceProvider(iface);

    TRACE("(%p, %s, %s, %p)\n", iface, debugstr_guid(service), debugstr_guid(riid), ppv);

    if (!ppv)
        return E_POINTER;
    *ppv = NULL;
    if (!This->site)
        return E_FAIL;
    return IUnknown_QueryService(This->site, service, riid, ppv);
}

static const IServiceProviderVtbl ServiceProviderVtbl =
{
    ServiceProvider_QueryInterface,
    ServiceProvider_AddRef,
    ServiceProvider_Release,
    ServiceProvider_QueryService,
};

IMPL_FROM(IOleCommandTarget, IOleCommandTarget_iface)
FORWARD_IUNKNOWN(IOleCommandTarget, OleCommandTarget)

static HRESULT WINAPI OleCommandTarget_QueryStatus(IOleCommandTarget *iface, const GUID *group, ULONG count, OLECMD *cmds, OLECMDTEXT *text)
{
    TRACE("(%p, %s, %lu, %p, %p)\n", iface, debugstr_guid(group), count, cmds, text);

    return E_NOTIMPL;
}

static HRESULT WINAPI OleCommandTarget_Exec(IOleCommandTarget *iface, const GUID *group, DWORD id, DWORD opt, VARIANT *in, VARIANT *out)
{
    TRACE("(%p, %s, %lu, %lu, %p, %p)\n", iface, debugstr_guid(group), id, opt, in, out);

    if (!group)
        return OLECMDERR_E_NOTSUPPORTED;
    return S_OK;
}

static const IOleCommandTargetVtbl OleCommandTargetVtbl =
{
    OleCommandTarget_QueryInterface,
    OleCommandTarget_AddRef,
    OleCommandTarget_Release,
    OleCommandTarget_QueryStatus,
    OleCommandTarget_Exec,
};

static void get_store_keys(InternetShortcut *This, const struct store_key **keys, DWORD *count)
{
    if (This->currentFile)
    {
        *keys = file_keys;
        *count = ARRAY_SIZE(file_keys);
    }
    else
    {
        *keys = memory_keys;
        *count = ARRAY_SIZE(memory_keys);
    }
}

static const struct store_key *find_store_key(InternetShortcut *This, REFPROPERTYKEY key)
{
    const struct store_key *keys;
    DWORD count, i;

    get_store_keys(This, &keys, &count);
    for (i = 0; i < count; i++)
    {
        if (IsEqualPropertyKey(keys[i].key, *key))
            return &keys[i];
    }
    return NULL;
}

static struct intshcut_prop *find_cached(InternetShortcut *This, REFPROPERTYKEY key)
{
    DWORD i;

    for (i = 0; i < This->prop_count; i++)
    {
        if (IsEqualPropertyKey(This->props[i].key, *key))
            return &This->props[i];
    }
    return NULL;
}

static HRESULT set_cached(InternetShortcut *This, REFPROPERTYKEY key, REFPROPVARIANT value)
{
    struct intshcut_prop *prop = find_cached(This, key), *props;
    PROPVARIANT copy;
    HRESULT hr;

    hr = PropVariantCopy(&copy, value);
    if (FAILED(hr))
        return hr;

    if (!prop)
    {
        props = realloc(This->props, (This->prop_count + 1) * sizeof(*props));
        if (!props)
        {
            PropVariantClear(&copy);
            return E_OUTOFMEMORY;
        }
        This->props = props;
        prop = &This->props[This->prop_count++];
        prop->key = *key;
        PropVariantInit(&prop->value);
    }
    PropVariantClear(&prop->value);
    prop->value = copy;
    return S_OK;
}

static void remove_cached(InternetShortcut *This, struct intshcut_prop *prop)
{
    PropVariantClear(&prop->value);
    This->prop_count--;
    memmove(prop, prop + 1, (This->props + This->prop_count - prop) * sizeof(*prop));
}

static BOOL read_file_key(InternetShortcut *This, const struct store_key *k, WCHAR *buf, DWORD cch)
{
    return GetPrivateProfileStringW(INTSHCUT_SECTION, k->name, L"", buf, cch, This->currentFile) > 0;
}

static BOOL store_key_present(InternetShortcut *This, const struct store_key *k)
{
    WCHAR buf[INTERNET_MAX_URL_LENGTH];
    PROPVARIANT pv;
    BOOL present;

    if (This->currentFile)
        return read_file_key(This, k, buf, ARRAY_SIZE(buf));
    if (k->propid == PID_IS_URL)
        return This->url != NULL;
    present = read_prop(This, k->propid, &pv) == S_OK && pv.vt != VT_EMPTY;
    PropVariantClear(&pv);
    return present;
}

static HRESULT get_store_value(InternetShortcut *This, const struct store_key *k, PROPVARIANT *value)
{
    WCHAR buf[INTERNET_MAX_URL_LENGTH];
    LONG number;

    PropVariantInit(value);
    if (This->currentFile)
    {
        if (!read_file_key(This, k, buf, ARRAY_SIZE(buf)))
            return S_OK;
        if (k->vt == VT_LPWSTR)
        {
            value->pwszVal = co_strdupW(buf);
            if (!value->pwszVal)
                return E_OUTOFMEMORY;
            value->vt = VT_LPWSTR;
            return S_OK;
        }
        number = wcstol(buf, NULL, 10);
    }
    else
    {
        if (!store_key_present(This, k))
            return S_OK;
        if (k->vt == VT_LPWSTR)
        {
            value->pwszVal = (k->propid == PID_IS_URL) ? co_strdupW(This->url) : get_string_prop(This, k->propid);
            if (value->pwszVal)
                value->vt = VT_LPWSTR;
            return S_OK;
        }
        number = get_int_prop(This, k->propid, 0);
    }

    value->vt = k->vt;
    if (k->vt == VT_UI2)
        value->uiVal = (USHORT)number;
    else
        value->lVal = number;
    return S_OK;
}

static HRESULT set_memory_value(InternetShortcut *This, const struct store_key *k, REFPROPVARIANT value)
{
    if (value->vt != VT_EMPTY && value->vt != k->vt)
        return E_INVALIDARG;

    switch (k->propid)
    {
        case PID_IS_URL:
            return set_url(This, value->vt == VT_EMPTY ? NULL : value->pwszVal);
        case PID_IS_WORKINGDIR:
        case PID_IS_ICONFILE:
            return set_string_prop(This, k->propid, value->vt == VT_EMPTY ? NULL : value->pwszVal);
        case PID_IS_HOTKEY:
            return set_int_prop(This, k->propid, value->vt == VT_EMPTY ? VT_EMPTY : VT_UI2, value->uiVal);
        default:
            return set_int_prop(This, k->propid, value->vt == VT_EMPTY ? VT_EMPTY : VT_I4, value->lVal);
    }
}

static BOOL get_store_key_at(InternetShortcut *This, DWORD index, PROPERTYKEY *key)
{
    const struct store_key *keys;
    DWORD count, i, n = 0;

    get_store_keys(This, &keys, &count);
    for (i = 0; i < count; i++)
    {
        if (!find_cached(This, &keys[i].key) && !store_key_present(This, &keys[i]))
            continue;
        if (n++ == index)
        {
            if (key)
                *key = keys[i].key;
            return TRUE;
        }
    }
    for (i = 0; i < This->prop_count; i++)
    {
        if (find_store_key(This, &This->props[i].key))
            continue;
        if (n++ == index)
        {
            if (key)
                *key = This->props[i].key;
            return TRUE;
        }
    }
    return FALSE;
}

IMPL_FROM(IPropertyStore, IPropertyStore_iface)
FORWARD_IUNKNOWN(IPropertyStore, PropertyStore)

static HRESULT WINAPI PropertyStore_GetCount(IPropertyStore *iface, DWORD *count)
{
    InternetShortcut *This = impl_from_IPropertyStore(iface);
    DWORD n = 0;

    TRACE("(%p, %p)\n", iface, count);

    if (!count)
        return E_POINTER;
    while (get_store_key_at(This, n, NULL))
        n++;
    *count = n;
    return S_OK;
}

static HRESULT WINAPI PropertyStore_GetAt(IPropertyStore *iface, DWORD index, PROPERTYKEY *key)
{
    InternetShortcut *This = impl_from_IPropertyStore(iface);

    TRACE("(%p, %lu, %p)\n", iface, index, key);

    if (!key)
        return E_POINTER;
    return get_store_key_at(This, index, key) ? S_OK : E_INVALIDARG;
}

static HRESULT WINAPI PropertyStore_GetValue(IPropertyStore *iface, REFPROPERTYKEY key, PROPVARIANT *value)
{
    InternetShortcut *This = impl_from_IPropertyStore(iface);
    const struct store_key *k;
    struct intshcut_prop *prop;

    TRACE("(%p, %p, %p)\n", iface, key, value);

    if (!key || !value)
        return E_POINTER;
    PropVariantInit(value);

    if ((prop = find_cached(This, key)))
        return PropVariantCopy(value, &prop->value);
    if ((k = find_store_key(This, key)))
        return get_store_value(This, k, value);
    return S_OK;
}

static HRESULT WINAPI PropertyStore_SetValue(IPropertyStore *iface, REFPROPERTYKEY key, REFPROPVARIANT value)
{
    InternetShortcut *This = impl_from_IPropertyStore(iface);
    const struct store_key *k;

    TRACE("(%p, %p, %p)\n", iface, key, value);

    if (!key || !value)
        return E_POINTER;

    k = find_store_key(This, key);
    if (k && !This->currentFile)
        return set_memory_value(This, k, value);
    return set_cached(This, key, value);
}

static HRESULT WINAPI PropertyStore_Commit(IPropertyStore *iface)
{
    InternetShortcut *This = impl_from_IPropertyStore(iface);
    const struct store_key *k;
    WCHAR num[16];
    const WCHAR *str;
    DWORD i = 0;

    TRACE("(%p)\n", iface);

    if (!This->currentFile)
        return S_OK;

    while (i < This->prop_count)
    {
        struct intshcut_prop *prop = &This->props[i];

        if (!(k = find_store_key(This, &prop->key)))
        {
            i++;
            continue;
        }

        switch (prop->value.vt)
        {
            case VT_EMPTY:
                str = NULL;
                break;
            case VT_LPWSTR:
                str = prop->value.pwszVal;
                break;
            case VT_UI2:
                swprintf(num, ARRAY_SIZE(num), L"%u", prop->value.uiVal);
                str = num;
                break;
            case VT_I4:
                swprintf(num, ARRAY_SIZE(num), L"%ld", prop->value.lVal);
                str = num;
                break;
            default:
                return E_INVALIDARG;
        }
        if (!WritePrivateProfileStringW(INTSHCUT_SECTION, k->name, str, This->currentFile))
            return HRESULT_FROM_WIN32(GetLastError());
        remove_cached(This, prop);
    }
    return S_OK;
}

static const IPropertyStoreVtbl PropertyStoreVtbl =
{
    PropertyStore_QueryInterface,
    PropertyStore_AddRef,
    PropertyStore_Release,
    PropertyStore_GetCount,
    PropertyStore_GetAt,
    PropertyStore_GetValue,
    PropertyStore_SetValue,
    PropertyStore_Commit,
};

IMPL_FROM(IInitializeWithFile, IInitializeWithFile_iface)
FORWARD_IUNKNOWN(IInitializeWithFile, InitializeWithFile)

static HRESULT WINAPI InitializeWithFile_Initialize(IInitializeWithFile *iface, LPCWSTR path, DWORD mode)
{
    InternetShortcut *This = impl_from_IInitializeWithFile(iface);

    TRACE("(%p, %s, 0x%lx)\n", iface, debugstr_w(path), mode);

    if (!path)
        return E_INVALIDARG;
    return IPersistFile_Load(&This->IPersistFile_iface, path, mode);
}

static const IInitializeWithFileVtbl InitializeWithFileVtbl =
{
    InitializeWithFile_QueryInterface,
    InitializeWithFile_AddRef,
    InitializeWithFile_Release,
    InitializeWithFile_Initialize,
};

IMPL_FROM(IInitializeWithBindCtx, IInitializeWithBindCtx_iface)
FORWARD_IUNKNOWN(IInitializeWithBindCtx, InitializeWithBindCtx)

static HRESULT WINAPI InitializeWithBindCtx_Initialize(IInitializeWithBindCtx *iface, IBindCtx *bindctx)
{
    TRACE("(%p, %p)\n", iface, bindctx);

    if (!bindctx)
        return E_INVALIDARG;
    return S_OK;
}

static const IInitializeWithBindCtxVtbl InitializeWithBindCtxVtbl =
{
    InitializeWithBindCtx_QueryInterface,
    InitializeWithBindCtx_AddRef,
    InitializeWithBindCtx_Release,
    InitializeWithBindCtx_Initialize,
};

struct intshcut_page
{
    InternetShortcut *shortcut;
    WCHAR icon[MAX_PATH];
    int icon_index;
    BOOL icon_changed;
    HICON hicon;
};

static void page_update_icon(HWND hwnd, struct intshcut_page *page)
{
    WCHAR *location = NULL;
    HICON large = NULL;
    int index = 0;

    if (page->icon_changed)
    {
        extract_icon(page->icon, page->icon_index, &large, NULL, GetSystemMetrics(SM_CXICON));
    }
    else if (SUCCEEDED(get_icon_location(page->shortcut, &location, &index, NULL)) && location)
    {
        extract_icon(location, index, &large, NULL, GetSystemMetrics(SM_CXICON));
        CoTaskMemFree(location);
    }

    SendDlgItemMessageW(hwnd, IDC_INTSHCUT_ICON, STM_SETICON, (WPARAM)large, 0);
    if (page->hicon)
        DestroyIcon(page->hicon);
    page->hicon = large;
}

static void page_init(HWND hwnd, struct intshcut_page *page)
{
    InternetShortcut *This = page->shortcut;
    WCHAR text[MAX_PATH], *name;
    int showcmd, sel, i;
    HWND combo;

    if (This->currentFile)
    {
        lstrcpynW(text, PathFindFileNameW(This->currentFile), ARRAY_SIZE(text));
        PathRemoveExtensionW(text);
        SetDlgItemTextW(hwnd, IDC_INTSHCUT_NAME, text);
    }
    else if ((name = get_description(This)))
    {
        SetDlgItemTextW(hwnd, IDC_INTSHCUT_NAME, name);
        CoTaskMemFree(name);
    }

    SetDlgItemTextW(hwnd, IDC_INTSHCUT_URL, This->url ? This->url : L"");
    SendDlgItemMessageW(hwnd, IDC_INTSHCUT_HOTKEY, HKM_SETHOTKEY, get_int_prop(This, PID_IS_HOTKEY, 0), 0);

    combo = GetDlgItem(hwnd, IDC_INTSHCUT_SHOWCMD);
    for (i = 0; i < 3; i++)
    {
        LoadStringW(ieframe_instance, IDS_INTSHCUT_RUN_NORMAL + i, text, ARRAY_SIZE(text));
        SendMessageW(combo, CB_ADDSTRING, 0, (LPARAM)text);
    }
    showcmd = get_int_prop(This, PID_IS_SHOWCMD, SW_SHOWNORMAL);
    sel = (showcmd == SW_SHOWMINNOACTIVE || showcmd == SW_SHOWMINIMIZED || showcmd == SW_MINIMIZE) ? 1 :
          (showcmd == SW_SHOWMAXIMIZED) ? 2 : 0;
    SendMessageW(combo, CB_SETCURSEL, sel, 0);

    page_update_icon(hwnd, page);
}

static void page_change_icon(HWND hwnd, struct intshcut_page *page)
{
    WCHAR path[MAX_PATH] = L"", *location = NULL;
    DWORD len = ARRAY_SIZE(path);
    int index = page->icon_index;

    if (page->icon_changed)
    {
        lstrcpynW(path, page->icon, ARRAY_SIZE(path));
    }
    else if (SUCCEEDED(get_icon_location(page->shortcut, &location, &index, NULL)) && location)
    {
        if (UrlIsW(location, URLIS_FILEURL))
            PathCreateFromUrlW(location, path, &len, 0);
        else
            lstrcpynW(path, location, ARRAY_SIZE(path));
        CoTaskMemFree(location);
    }

    if (!PickIconDlg(hwnd, path, ARRAY_SIZE(path), &index))
        return;

    lstrcpynW(page->icon, path, ARRAY_SIZE(page->icon));
    page->icon_index = index;
    page->icon_changed = TRUE;
    page_update_icon(hwnd, page);
    PropSheet_Changed(GetParent(hwnd), hwnd);
}

static void page_apply(HWND hwnd, struct intshcut_page *page)
{
    static const int showcmds[] = { SW_SHOWNORMAL, SW_SHOWMINNOACTIVE, SW_SHOWMAXIMIZED };
    InternetShortcut *This = page->shortcut;
    WCHAR *url;
    int len, sel;

    len = GetWindowTextLengthW(GetDlgItem(hwnd, IDC_INTSHCUT_URL));
    url = malloc((len + 1) * sizeof(WCHAR));
    if (url)
    {
        GetDlgItemTextW(hwnd, IDC_INTSHCUT_URL, url, len + 1);
        if (*url && (!This->url || wcscmp(url, This->url)))
            set_url(This, url);
        free(url);
    }

    set_int_prop(This, PID_IS_HOTKEY, VT_UI2,
                 (WORD)SendDlgItemMessageW(hwnd, IDC_INTSHCUT_HOTKEY, HKM_GETHOTKEY, 0, 0));
    sel = (int)SendDlgItemMessageW(hwnd, IDC_INTSHCUT_SHOWCMD, CB_GETCURSEL, 0, 0);
    if (sel >= 0 && sel < ARRAY_SIZE(showcmds))
        set_int_prop(This, PID_IS_SHOWCMD, VT_I4, showcmds[sel]);
    if (page->icon_changed)
        set_icon(This, page->icon, page->icon_index);

    if (This->currentFile)
        IPersistFile_Save(&This->IPersistFile_iface, This->currentFile, TRUE);
}

static INT_PTR CALLBACK intshcut_page_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    struct intshcut_page *page = (struct intshcut_page *)GetWindowLongPtrW(hwnd, DWLP_USER);

    switch (msg)
    {
        case WM_INITDIALOG:
            page = (struct intshcut_page *)((PROPSHEETPAGEW *)lparam)->lParam;
            SetWindowLongPtrW(hwnd, DWLP_USER, (LONG_PTR)page);
            page_init(hwnd, page);
            return TRUE;

        case WM_COMMAND:
            if (!page)
                break;
            switch (LOWORD(wparam))
            {
                case IDC_INTSHCUT_URL:
                case IDC_INTSHCUT_HOTKEY:
                    if (HIWORD(wparam) == EN_CHANGE)
                        PropSheet_Changed(GetParent(hwnd), hwnd);
                    break;
                case IDC_INTSHCUT_SHOWCMD:
                    if (HIWORD(wparam) == CBN_SELCHANGE)
                        PropSheet_Changed(GetParent(hwnd), hwnd);
                    break;
                case IDC_INTSHCUT_CHANGEICON:
                    if (HIWORD(wparam) == BN_CLICKED)
                        page_change_icon(hwnd, page);
                    break;
            }
            break;

        case WM_NOTIFY:
            if (page && ((NMHDR *)lparam)->code == PSN_APPLY)
            {
                page_apply(hwnd, page);
                SetWindowLongPtrW(hwnd, DWLP_MSGRESULT, PSNRET_NOERROR);
                return TRUE;
            }
            break;

        case WM_DESTROY:
            if (page && page->hicon)
            {
                DestroyIcon(page->hicon);
                page->hicon = NULL;
            }
            break;
    }
    return FALSE;
}

static UINT CALLBACK intshcut_page_callback(HWND hwnd, UINT msg, PROPSHEETPAGEW *psp)
{
    struct intshcut_page *page = (struct intshcut_page *)psp->lParam;

    if (msg == PSPCB_RELEASE)
    {
        IPersistFile_Release(&page->shortcut->IPersistFile_iface);
        free(page);
    }
    return 1;
}

IMPL_FROM(IShellPropSheetExt, IShellPropSheetExt_iface)
FORWARD_IUNKNOWN(IShellPropSheetExt, ShellPropSheetExt)

static HRESULT WINAPI ShellPropSheetExt_AddPages(IShellPropSheetExt *iface, LPFNSVADDPROPSHEETPAGE add_page, LPARAM lparam)
{
    InternetShortcut *This = impl_from_IShellPropSheetExt(iface);
    struct intshcut_page *page;
    PROPSHEETPAGEW psp;
    HPROPSHEETPAGE hpage;

    TRACE("(%p, %p, %Ix)\n", iface, add_page, lparam);

    if (!add_page)
        return E_INVALIDARG;
    if (!This->currentFile)
        return E_FAIL;

    page = calloc(1, sizeof(*page));
    if (!page)
        return E_OUTOFMEMORY;
    page->shortcut = This;

    memset(&psp, 0, sizeof(psp));
    psp.dwSize = sizeof(psp);
    psp.dwFlags = PSP_DEFAULT | PSP_USECALLBACK;
    psp.hInstance = ieframe_instance;
    psp.pszTemplate = MAKEINTRESOURCEW(IDD_INTSHCUT_PROP);
    psp.pfnDlgProc = intshcut_page_proc;
    psp.lParam = (LPARAM)page;
    psp.pfnCallback = intshcut_page_callback;

    hpage = CreatePropertySheetPageW(&psp);
    if (!hpage)
    {
        free(page);
        return E_OUTOFMEMORY;
    }
    IPersistFile_AddRef(&This->IPersistFile_iface);

    if (!add_page(hpage, lparam))
    {
        DestroyPropertySheetPage(hpage);
        return E_FAIL;
    }
    return MAKE_HRESULT(SEVERITY_SUCCESS, 0, 1);
}

static HRESULT WINAPI ShellPropSheetExt_ReplacePage(IShellPropSheetExt *iface, EXPPS page, LPFNSVADDPROPSHEETPAGE replace, LPARAM lparam)
{
    TRACE("(%p, %u, %p, %Ix)\n", iface, page, replace, lparam);

    return E_NOTIMPL;
}

static const IShellPropSheetExtVtbl ShellPropSheetExtVtbl =
{
    ShellPropSheetExt_QueryInterface,
    ShellPropSheetExt_AddRef,
    ShellPropSheetExt_Release,
    ShellPropSheetExt_AddPages,
    ShellPropSheetExt_ReplacePage,
};

void InternetShortcutExt_Init(InternetShortcut *This)
{
    This->IPersistStream_iface.lpVtbl = &PersistStreamVtbl;
    This->IShellLinkA_iface.lpVtbl = &ShellLinkAVtbl;
    This->IShellLinkW_iface.lpVtbl = &ShellLinkWVtbl;
    This->IExtractIconA_iface.lpVtbl = &ExtractIconAVtbl;
    This->IExtractIconW_iface.lpVtbl = &ExtractIconWVtbl;
    This->IQueryInfo_iface.lpVtbl = &QueryInfoVtbl;
    This->INewShortcutHookA_iface.lpVtbl = &NewShortcutHookAVtbl;
    This->INewShortcutHookW_iface.lpVtbl = &NewShortcutHookWVtbl;
    This->IShellExtInit_iface.lpVtbl = &ShellExtInitVtbl;
    This->IContextMenu2_iface.lpVtbl = &ContextMenu2Vtbl;
    This->IDataObject_iface.lpVtbl = &DataObjectVtbl;
    This->IObjectWithSite_iface.lpVtbl = &ObjectWithSiteVtbl;
    This->IServiceProvider_iface.lpVtbl = &ServiceProviderVtbl;
    This->IOleCommandTarget_iface.lpVtbl = &OleCommandTargetVtbl;
    This->IPropertyStore_iface.lpVtbl = &PropertyStoreVtbl;
    This->IInitializeWithFile_iface.lpVtbl = &InitializeWithFileVtbl;
    This->IInitializeWithBindCtx_iface.lpVtbl = &InitializeWithBindCtxVtbl;
    This->IShellPropSheetExt_iface.lpVtbl = &ShellPropSheetExtVtbl;
}

void InternetShortcutExt_Free(InternetShortcut *This)
{
    DWORD i;

    for (i = 0; i < This->prop_count; i++)
        PropVariantClear(&This->props[i].value);
    free(This->props);
    if (This->site)
        IUnknown_Release(This->site);
    CoTaskMemFree(This->description);
    CoTaskMemFree(This->folder);
}

HRESULT InternetShortcutExt_QueryInterface(InternetShortcut *This, REFIID riid, void **ppv)
{
    if (IsEqualGUID(&IID_IPersist, riid))
        *ppv = &This->IPersistFile_iface;
    else if (IsEqualGUID(&IID_IPersistStream, riid))
        *ppv = &This->IPersistStream_iface;
    else if (IsEqualGUID(&IID_IShellLinkA, riid))
        *ppv = &This->IShellLinkA_iface;
    else if (IsEqualGUID(&IID_IShellLinkW, riid))
        *ppv = &This->IShellLinkW_iface;
    else if (IsEqualGUID(&IID_IExtractIconA, riid))
        *ppv = &This->IExtractIconA_iface;
    else if (IsEqualGUID(&IID_IExtractIconW, riid))
        *ppv = &This->IExtractIconW_iface;
    else if (IsEqualGUID(&IID_IQueryInfo, riid))
        *ppv = &This->IQueryInfo_iface;
    else if (IsEqualGUID(&IID_INewShortcutHookA, riid))
        *ppv = &This->INewShortcutHookA_iface;
    else if (IsEqualGUID(&IID_INewShortcutHookW, riid))
        *ppv = &This->INewShortcutHookW_iface;
    else if (IsEqualGUID(&IID_IShellExtInit, riid))
        *ppv = &This->IShellExtInit_iface;
    else if (IsEqualGUID(&IID_IContextMenu, riid) || IsEqualGUID(&IID_IContextMenu2, riid))
        *ppv = &This->IContextMenu2_iface;
    else if (IsEqualGUID(&IID_IDataObject, riid))
        *ppv = &This->IDataObject_iface;
    else if (IsEqualGUID(&IID_IObjectWithSite, riid))
        *ppv = &This->IObjectWithSite_iface;
    else if (IsEqualGUID(&IID_IServiceProvider, riid))
        *ppv = &This->IServiceProvider_iface;
    else if (IsEqualGUID(&IID_IOleCommandTarget, riid))
        *ppv = &This->IOleCommandTarget_iface;
    else if (IsEqualGUID(&IID_IPropertyStore, riid))
        *ppv = &This->IPropertyStore_iface;
    else if (IsEqualGUID(&IID_IInitializeWithFile, riid))
        *ppv = &This->IInitializeWithFile_iface;
    else if (IsEqualGUID(&IID_IInitializeWithBindCtx, riid))
        *ppv = &This->IInitializeWithBindCtx_iface;
    else if (IsEqualGUID(&IID_IShellPropSheetExt, riid))
        *ppv = &This->IShellPropSheetExt_iface;
    else
        return E_NOINTERFACE;
    return S_OK;
}
