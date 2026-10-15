/*
 * Implementation of the Fusion API
 *
 * Copyright 2008 James Hawkins
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

#include <stdarg.h>

#define COBJMACROS

#include "windef.h"
#include "winbase.h"
#include "winuser.h"
#include "ole2.h"
#include "fusion.h"
#include "wine/debug.h"

WINE_DEFAULT_DEBUG_CHANNEL(fusion);


/******************************************************************
 *  InitializeFusion   (FUSION.@)
 */
HRESULT WINAPI InitializeFusion(void)
{
    FIXME("\n");
    return S_OK;
}

/******************************************************************
 *  ClearDownloadCache   (FUSION.@)
 */
HRESULT WINAPI ClearDownloadCache(void)
{
    FIXME("stub!\n");
    return E_NOTIMPL;
}

/******************************************************************
 *  CopyPDBs   (FUSION.@)
 */
HRESULT WINAPI CopyPDBs(void *unknown)
{
    FIXME("(%p) stub!\n", unknown);
    return E_NOTIMPL;
}

/******************************************************************
 *  CreateInstallReferenceEnum   (FUSION.@)
 */
HRESULT WINAPI CreateInstallReferenceEnum(IInstallReferenceEnum **ppRefEnum,
                                          IAssemblyName *pName, DWORD dwFlags,
                                          LPVOID pvReserved)
{
    FIXME("(%p, %p, %08lx, %p) stub!\n", ppRefEnum, pName, dwFlags, pvReserved);
    return E_NOTIMPL;
}

/******************************************************************
 *  CreateApplicationContext   (FUSION.@)
 */
HRESULT WINAPI CreateApplicationContext(IAssemblyName *name, void *ctx)
{
    FIXME("%p, %p\n", name, ctx);
    return E_NOTIMPL;
}

static HRESULT (WINAPI *pGetCORVersion)(LPWSTR pbuffer, DWORD cchBuffer,
                                        DWORD *dwLength);

static HRESULT get_corversion(LPWSTR version, DWORD size)
{
    HMODULE hmscoree;
    HRESULT hr;
    DWORD len;

    hmscoree = LoadLibraryA("mscoree.dll");
    if (!hmscoree)
        return E_FAIL;

    pGetCORVersion = (void *)GetProcAddress(hmscoree, "GetCORVersion");
    if (!pGetCORVersion)
        hr = E_FAIL;
    else
        hr = pGetCORVersion(version, size, &len);

    FreeLibrary(hmscoree);
    return hr;
}

#ifdef __REACTOS__
static BOOL get_module_clr_version(LPWSTR version, DWORD size)
{
    WCHAR path[MAX_PATH], *file, *dir;
    HMODULE module;

    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            (LPCWSTR)get_module_clr_version, &module) ||
        !GetModuleFileNameW(module, path, ARRAY_SIZE(path)) ||
        !(file = wcsrchr(path, '\\')))
        return FALSE;

    *file = 0;
    if (!(dir = wcsrchr(path, '\\')) || (dir[1] != 'v' && dir[1] != 'V') || dir[2] < '0' || dir[2] > '9' ||
        lstrlenW(dir + 1) >= size)
        return FALSE;

    lstrcpyW(version, dir + 1);
    return TRUE;
}

#endif
/******************************************************************
 *  GetCachePath   (FUSION.@)
 */
HRESULT WINAPI GetCachePath(ASM_CACHE_FLAGS dwCacheFlags, LPWSTR pwzCachePath,
                            PDWORD pcchPath)
{
    static const WCHAR zapfmt[] = L"%s\\assembly\\NativeImages_%s_%s";
    SYSTEM_INFO info;
    WCHAR path[MAX_PATH], windir[MAX_PATH], version[MAX_PATH];
    DWORD len;
    HRESULT hr = S_OK;

    TRACE("(%08x, %p, %p)\n", dwCacheFlags, pwzCachePath, pcchPath);

    if (!pcchPath)
        return E_INVALIDARG;

    len = GetWindowsDirectoryW(windir, MAX_PATH);
    lstrcpyW(path, windir);

    switch (dwCacheFlags)
    {
        case ASM_CACHE_ZAP:
        {
#ifdef __REACTOS__
            hr = get_module_clr_version(version, MAX_PATH) ? S_OK : get_corversion(version, MAX_PATH);
#else
            hr = get_corversion(version, MAX_PATH);
#endif
            if (FAILED(hr))
                return hr;

            GetSystemInfo(&info);
            len = swprintf(path, ARRAY_SIZE(path), zapfmt, windir, version,
                           info.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_ARM64 ? L"ARM64" :
                           sizeof(void *) == sizeof(ULONGLONG) ? L"64" : L"32");
            break;
        }
        case ASM_CACHE_GAC:
        {
            lstrcpyW(path + len, L"\\assembly");
            len += ARRAY_SIZE(L"\\assembly") - 1;
            lstrcpyW(path + len, L"\\GAC");
            len += ARRAY_SIZE(L"\\GAC") - 1;
            break;
        }
        case ASM_CACHE_DOWNLOAD:
        {
            FIXME("Download cache not implemented\n");
            return E_FAIL;
        }
        case ASM_CACHE_ROOT:
            lstrcpyW(path + len, L"\\assembly");
            len += ARRAY_SIZE(L"\\assembly") - 1;
            break;
        case ASM_CACHE_ROOT_EX:
            lstrcpyW(path + len, L"\\Microsoft.NET");
            len += ARRAY_SIZE(L"\\Microsoft.NET") - 1;
            lstrcpyW(path + len, L"\\assembly");
            len += ARRAY_SIZE(L"\\assembly") - 1;
            break;
        default:
            return E_INVALIDARG;
    }

    len++;
    if (*pcchPath <= len || !pwzCachePath)
        hr = E_NOT_SUFFICIENT_BUFFER;
    else if (pwzCachePath)
        lstrcpyW(pwzCachePath, path);

    *pcchPath = len;
    return hr;
}
