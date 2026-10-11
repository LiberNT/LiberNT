/*
 * PROJECT:     LiberNT Windows Storage
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Internet IShellFolder implementation
 * COPYRIGHT:   Copyright 2025 Whindmar Saksit <whindsaks@proton.me>
 */

#define NONAMELESSUNION
#define COBJMACROS

#include <stdarg.h>

#include "windef.h"
#include "winbase.h"
#include "winuser.h"
#include "ole2.h"

#include "shlobj.h"
#include "shobjidl.h"
#include "shellapi.h"
#include "shlwapi.h"
#include "shlguid.h"
#include "intshcut.h"

#include "wine/debug.h"

#include "objects.h"

WINE_DEFAULT_DEBUG_CHANNEL(storage);

extern int WINAPI SHAnsiToUnicodeCP(UINT CodePage, LPCSTR pszSrc, LPWSTR pwszDst, int cwchBuf);

#define MAX_URL_LENGTH 1024

#define PT_INTERNET_URL 0x61
#define IFUIF_UNICODE 0x80
typedef struct _IFURLITEM
{
    WORD cb;
    BYTE Type; // PT_INTERNET_URL
    BYTE Flags; // IFUIF_*
    UINT Unknown;
    WCHAR Url[ANYSIZE_ARRAY];
} IFURLITEM;

static int GetSchemeCharType(WCHAR c)
{
    if (('A' <= c && c <= 'Z') || ('a' <= c && c <= 'z'))
        return 0;
    return c == '+' || c == '-' || c == '.' ? 1 : -1;
}

static unsigned int GetSchemeLength(PCWSTR s)
{
    if (GetSchemeCharType(s[0]) != 0) // The first character MUST be A-Z, a-z.
        return 0;
    for (unsigned int i = 0;;)
    {
        if (s[++i] == ':')
            return ++i;
        if (GetSchemeCharType(s[i]) < 0)
            return 0;
    }
}

static LPITEMIDLIST CreateUrlItem(PCWSTR pszUrl)
{
    UINT cch = lstrlenW(pszUrl) + 1;
    UINT cb = FIELD_OFFSET(IFURLITEM, Url[cch]);
    IFURLITEM *pidl = SHAlloc(cb + sizeof(WORD));
    if (!pidl)
        return (LPITEMIDLIST)pidl;
    pidl->cb = cb;
    pidl->Type = PT_INTERNET_URL;
    pidl->Flags = IFUIF_UNICODE;
    pidl->Unknown = 0;
    CopyMemory(pidl->Url, pszUrl, cch * sizeof(*pszUrl));
    ILGetNext((LPITEMIDLIST)pidl)->mkid.cb = 0;
    return (LPITEMIDLIST)pidl;
}

static IFURLITEM* IsUrlItem(LPCITEMIDLIST pidl)
{
    IFURLITEM *p = (IFURLITEM*)pidl;
    if (p && p->cb > FIELD_OFFSET(IFURLITEM, Url) && p->Type == PT_INTERNET_URL)
        return p;
    return NULL;
}

static PWSTR GetUrl(IFURLITEM *pUrl, PWSTR Buffer)
{
    if (pUrl->Flags & IFUIF_UNICODE)
        return pUrl->Url;
    SHAnsiToUnicodeCP(CP_ACP, (PCSTR)pUrl->Url, Buffer, MAX_URL_LENGTH);
    return Buffer;
}

static HRESULT CreateUrlShortcut(PCWSTR Url, IUniformResourceLocatorW **ppv)
{
    HRESULT hr = CoCreateInstance(&CLSID_InternetShortcut, NULL, CLSCTX_INPROC_SERVER,
                                  &IID_IUniformResourceLocatorW, (void**)ppv);
    if (SUCCEEDED(hr))
        hr = (*ppv)->lpVtbl->SetURL(*ppv, Url, 0);
    return hr;
}

typedef struct _CInternetFolder
{
    IShellFolder2 IShellFolder2_iface;
    IPersistFolder2 IPersistFolder2_iface;
    IBrowserFrameOptions IBrowserFrameOptions_iface;
    IObjectWithBackReferences IObjectWithBackReferences_iface;
    LONG refCount;
    LPITEMIDLIST pidlRoot;
} CInternetFolder;

typedef struct _CInternetFolderEnum
{
    IEnumIDList IEnumIDList_iface;
    LONG refCount;
} CInternetFolderEnum;

static HRESULT WINAPI EnumIDList_QueryInterface(IEnumIDList *This, REFIID riid, PVOID *ppvObject)
{
    *ppvObject = NULL;
    if (!IsEqualGUID(&IID_IUnknown, riid) && !IsEqualGUID(&IID_IEnumIDList, riid))
        return E_NOINTERFACE;
    *ppvObject = This;
    IEnumIDList_AddRef(This);
    return S_OK;
}

static ULONG WINAPI EnumIDList_AddRef(IEnumIDList *This)
{
    return InterlockedIncrement(&CONTAINING_RECORD(This, CInternetFolderEnum, IEnumIDList_iface)->refCount);
}

static ULONG WINAPI EnumIDList_Release(IEnumIDList *This)
{
    CInternetFolderEnum *pEnum = CONTAINING_RECORD(This, CInternetFolderEnum, IEnumIDList_iface);
    const ULONG count = InterlockedDecrement(&pEnum->refCount);
    if (count == 0)
    {
        SHFree(pEnum);
        WSTORAGE_UnlockModule();
    }
    return count;
}

static HRESULT WINAPI EnumIDList_Next(IEnumIDList *This, ULONG celt, PITEMID_CHILD *rgelt, ULONG *pceltFetched)
{
    if (pceltFetched)
        *pceltFetched = 0;
    return S_FALSE;
}

static HRESULT WINAPI EnumIDList_Skip(IEnumIDList *This, ULONG celt)
{
    return S_FALSE;
}

static HRESULT WINAPI EnumIDList_Reset(IEnumIDList *This)
{
    return S_OK;
}

static HRESULT CreateEmptyEnum(IEnumIDList **ppenum);

static HRESULT WINAPI EnumIDList_Clone(IEnumIDList *This, IEnumIDList **ppenum)
{
    return CreateEmptyEnum(ppenum);
}

static const IEnumIDListVtbl EnumIDListVtbl = {
    EnumIDList_QueryInterface,
    EnumIDList_AddRef,
    EnumIDList_Release,
    EnumIDList_Next,
    EnumIDList_Skip,
    EnumIDList_Reset,
    EnumIDList_Clone
};

static HRESULT CreateEmptyEnum(IEnumIDList **ppenum)
{
    CInternetFolderEnum *pEnum = SHAlloc(sizeof(CInternetFolderEnum));
    *ppenum = NULL;
    if (!pEnum)
        return E_OUTOFMEMORY;
    pEnum->IEnumIDList_iface.lpVtbl = &EnumIDListVtbl;
    pEnum->refCount = 1;
    WSTORAGE_LockModule();
    *ppenum = &pEnum->IEnumIDList_iface;
    return S_OK;
}

static HRESULT Unknown_QueryInterface(CInternetFolder *This, REFIID riid, PVOID *ppvObject)
{
    TRACE("(%p, %s, %p)\n", This, debugstr_guid(riid), ppvObject);
    *ppvObject = NULL;
    if (IsEqualGUID(&IID_IUnknown, riid))
        *ppvObject = &This->IObjectWithBackReferences_iface;
    else if (IsEqualGUID(&IID_IShellFolder, riid) || IsEqualGUID(&IID_IShellFolder2, riid))
        *ppvObject = &This->IShellFolder2_iface;
    else if (IsEqualGUID(&IID_IPersist, riid) || IsEqualGUID(&IID_IPersistFolder, riid) ||
             IsEqualGUID(&IID_IPersistFolder2, riid))
        *ppvObject = &This->IPersistFolder2_iface;
    else if (IsEqualGUID(&IID_IBrowserFrameOptions, riid))
        *ppvObject = &This->IBrowserFrameOptions_iface;
    else if (IsEqualGUID(&IID_IObjectWithBackReferences, riid))
        *ppvObject = &This->IObjectWithBackReferences_iface;
    else
        return E_NOINTERFACE;
    IUnknown_AddRef((IUnknown*)*ppvObject);
    return S_OK;
}

static ULONG Unknown_AddRef(CInternetFolder *This)
{
    return InterlockedIncrement(&This->refCount);
}

static ULONG Unknown_Release(CInternetFolder *This)
{
    const ULONG count = InterlockedDecrement(&This->refCount);
    if (count == 0)
    {
        ILFree(This->pidlRoot);
        SHFree(This);
        WSTORAGE_UnlockModule();
    }
    return count;
}

static HRESULT WINAPI ShellFolder_QueryInterface(IShellFolder2 *This, REFIID riid, PVOID *ppvObject)
{
    return Unknown_QueryInterface(CONTAINING_RECORD(This, CInternetFolder, IShellFolder2_iface), riid, ppvObject);
}

static ULONG WINAPI ShellFolder_AddRef(IShellFolder2 *This)
{
    return Unknown_AddRef(CONTAINING_RECORD(This, CInternetFolder, IShellFolder2_iface));
}

static ULONG WINAPI ShellFolder_Release(IShellFolder2 *This)
{
    return Unknown_Release(CONTAINING_RECORD(This, CInternetFolder, IShellFolder2_iface));
}

static HRESULT WINAPI GetAttributesOf(IShellFolder2 *This, UINT cidl, PCUITEMID_CHILD_ARRAY apidl, SFGAOF *rgfInOut);

static HRESULT WINAPI ParseDisplayName(IShellFolder2 *This, HWND hwnd, IBindCtx *pbc, LPWSTR pszDisplayName, ULONG *pchEaten, PIDLIST_RELATIVE *ppidl, ULONG *pdwAttributes)
{
    UINT len;

    *ppidl = NULL;
    len = GetSchemeLength(pszDisplayName);
    if (len)
    {
        if (len + 1 == sizeof("shell:"))
        {
            WCHAR szBuf[100];
            lstrcpynW(szBuf, pszDisplayName, sizeof("shell:"));
            if (!lstrcmpiW(szBuf, L"shell:"))
                return E_FAIL;
        }

        if ((*ppidl = CreateUrlItem(pszDisplayName)) == NULL)
            return E_OUTOFMEMORY;
        if (pdwAttributes)
            GetAttributesOf(This, 1, (PCUITEMID_CHILD_ARRAY)ppidl, pdwAttributes);
        return S_OK;
    }
    return E_FAIL;
}

static HRESULT WINAPI ShellFolder_EnumObjects(IShellFolder2 *This, HWND hwndOwner, SHCONTF grfFlags, IEnumIDList **ppenumIDList)
{
    return CreateEmptyEnum(ppenumIDList);
}

static HRESULT WINAPI BindToObject(IShellFolder2 *This, PCUIDLIST_RELATIVE pidl, IBindCtx *pbc, REFIID riid, void **ppvOut)
{
    *ppvOut = NULL;
    return E_NOTIMPL;
}

static HRESULT WINAPI BindToStorage(IShellFolder2 *This, PCUIDLIST_RELATIVE pidl, IBindCtx *pbc, REFIID riid, void **ppvOut)
{
    *ppvOut = NULL;
    return E_NOTIMPL;
}

static HRESULT WINAPI CompareIDs(IShellFolder2 *This, LPARAM lParam, PCUIDLIST_RELATIVE pidl1, PCUIDLIST_RELATIVE pidl2)
{
    IFURLITEM *pUrl1 = IsUrlItem(pidl1), *pUrl2 = IsUrlItem(pidl2);
    if (pUrl1 && pUrl2)
    {
        WCHAR szUrl1[MAX_URL_LENGTH], *pszUrl1 = GetUrl(pUrl1, szUrl1);
        WCHAR szUrl2[MAX_URL_LENGTH], *pszUrl2 = GetUrl(pUrl2, szUrl2);
        int cmp = lstrcmpiW(pszUrl1, pszUrl2);
        return cmp < 0 ? 0xffff : cmp != 0;
    }
    return E_FAIL;
}

static HRESULT WINAPI CreateViewObject(IShellFolder2 *This, HWND hwndOwner,REFIID riid,void **ppv)
{
    *ppv = NULL;
    if (!hwndOwner)
        return E_INVALIDARG;
    return E_NOTIMPL;
}

static HRESULT WINAPI GetAttributesOf(IShellFolder2 *This, UINT cidl, PCUITEMID_CHILD_ARRAY apidl, SFGAOF *rgfInOut)
{
    UINT i;

    if (cidl == 0)
    {
        *rgfInOut &= SFGAO_FOLDER | SFGAO_CANLINK;
        return S_OK;
    }

    for (i = 0; i < cidl; ++i)
    {
        IFURLITEM *pUrl = IsUrlItem(apidl[i]);
        if (!pUrl)
            return E_FAIL;
        *rgfInOut &= SFGAO_CANLINK | SFGAO_BROWSABLE | SFGAO_STREAM;
    }
    return S_OK;
}

static HRESULT WINAPI GetUIObjectOf(IShellFolder2 *This, HWND hwndOwner, UINT cidl, PCUITEMID_CHILD_ARRAY apidl, REFIID riid, UINT *rgfReserved, void **ppv)
{
    IFURLITEM *pUrl;
    if (cidl == 1 && (pUrl = IsUrlItem(apidl[0])) != NULL)
    {
        if (IsEqualIID(riid, &IID_IExtractIconW) || IsEqualIID(riid, &IID_IExtractIconA) ||
            IsEqualIID(riid, &IID_IContextMenu) ||
            IsEqualIID(riid, &IID_IDataObject) ||
            IsEqualIID(riid, &IID_IQueryInfo))
        {
            IUniformResourceLocatorW *pUrlLnk;
            WCHAR szUrl[MAX_URL_LENGTH], *pszUrl = GetUrl(pUrl, szUrl);
            HRESULT hr = CreateUrlShortcut(pszUrl, &pUrlLnk);
            if (SUCCEEDED(hr))
            {
                hr = IUnknown_QueryInterface(pUrlLnk, riid, ppv);
                IUnknown_Release(pUrlLnk);
            }
            return hr;
        }
    }
    return E_NOINTERFACE;
}

static HRESULT WINAPI GetDisplayNameOf(IShellFolder2 *This, PCUITEMID_CHILD pidl, SHGDNF uFlags, STRRET *pSR)
{
    IFURLITEM *pUrl = IsUrlItem(pidl);
    if (pUrl)
    {
        WCHAR szUrl[MAX_URL_LENGTH], *pszUrl = GetUrl(pUrl, szUrl);
        pSR->uType = STRRET_WSTR;
        return SHStrDupW(pszUrl, &pSR->u.pOleStr);
    }
    return E_FAIL;
}

static HRESULT WINAPI SetNameOf(IShellFolder2 *This, HWND hwndOwner, PCUITEMID_CHILD pidl, LPCWSTR pszName, SHGDNF uFlags, PITEMID_CHILD*ppidlOut)
{
    if (ppidlOut)
        *ppidlOut = NULL;
    return E_NOTIMPL;
}

static HRESULT WINAPI GetDefaultSearchGUID(IShellFolder2 *This, GUID *pguid)
{
    return E_NOTIMPL;
}

static HRESULT WINAPI EnumSearches(IShellFolder2 *This, IEnumExtraSearch **ppenum)
{
    *ppenum = NULL;
    return E_NOTIMPL;
}

static HRESULT WINAPI GetDefaultColumn(IShellFolder2 *This, DWORD dwRes, ULONG *pSort, ULONG *pDisplay)
{
    return E_NOTIMPL;
}

static HRESULT WINAPI GetDefaultColumnState(IShellFolder2 *This, UINT iColumn, SHCOLSTATEF *pcsFlags)
{
    return E_NOTIMPL;
}

static HRESULT WINAPI GetDetailsEx(IShellFolder2 *This, PCUITEMID_CHILD pidl, const SHCOLUMNID *pscid, VARIANT *pv)
{
    return E_FAIL;
}

static HRESULT WINAPI GetDetailsOf(IShellFolder2 *This, PCUITEMID_CHILD pidl, UINT iColumn, SHELLDETAILS *psd)
{
    return E_NOTIMPL;
}

static HRESULT WINAPI MapColumnToSCID(IShellFolder2 *This, UINT iColumn, SHCOLUMNID *pscid)
{
    return E_NOTIMPL;
}

static HRESULT WINAPI PersistFolder_QueryInterface(IPersistFolder2 *This, REFIID riid, PVOID *ppvObject)
{
    return Unknown_QueryInterface(CONTAINING_RECORD(This, CInternetFolder, IPersistFolder2_iface), riid, ppvObject);
}

static ULONG WINAPI PersistFolder_AddRef(IPersistFolder2 *This)
{
    return Unknown_AddRef(CONTAINING_RECORD(This, CInternetFolder, IPersistFolder2_iface));
}

static ULONG WINAPI PersistFolder_Release(IPersistFolder2 *This)
{
    return Unknown_Release(CONTAINING_RECORD(This, CInternetFolder, IPersistFolder2_iface));
}

static HRESULT WINAPI PersistFolder_GetClassID(IPersistFolder2 *This, CLSID *pClassID)
{
    *pClassID = CLSID_Internet;
    return S_OK;
}

static HRESULT WINAPI PersistFolder_Initialize(IPersistFolder2 *This, PCIDLIST_ABSOLUTE pidl)
{
    CInternetFolder *pThis = CONTAINING_RECORD(This, CInternetFolder, IPersistFolder2_iface);
    LPITEMIDLIST pidlNew = NULL;

    if (pidl)
    {
        pidlNew = ILClone(pidl);
        if (!pidlNew)
            return E_OUTOFMEMORY;
    }
    ILFree(pThis->pidlRoot);
    pThis->pidlRoot = pidlNew;
    return S_OK;
}

static HRESULT WINAPI PersistFolder_GetCurFolder(IPersistFolder2 *This, PIDLIST_ABSOLUTE *ppidl)
{
    CInternetFolder *pThis = CONTAINING_RECORD(This, CInternetFolder, IPersistFolder2_iface);

    *ppidl = NULL;
    if (!pThis->pidlRoot)
        return E_INVALIDARG;
    *ppidl = ILClone(pThis->pidlRoot);
    return *ppidl ? S_OK : E_OUTOFMEMORY;
}

static HRESULT WINAPI FrameOptions_QueryInterface(IBrowserFrameOptions *This, REFIID riid, PVOID *ppvObject)
{
    return Unknown_QueryInterface(CONTAINING_RECORD(This, CInternetFolder, IBrowserFrameOptions_iface), riid, ppvObject);
}

static ULONG WINAPI FrameOptions_AddRef(IBrowserFrameOptions *This)
{
    return Unknown_AddRef(CONTAINING_RECORD(This, CInternetFolder, IBrowserFrameOptions_iface));
}

static ULONG WINAPI FrameOptions_Release(IBrowserFrameOptions *This)
{
    return Unknown_Release(CONTAINING_RECORD(This, CInternetFolder, IBrowserFrameOptions_iface));
}

static HRESULT WINAPI FrameOptions_GetFrameOptions(IBrowserFrameOptions *This, BROWSERFRAMEOPTIONS dwMask, BROWSERFRAMEOPTIONS *pdwOptions)
{
    if (!pdwOptions)
        return E_POINTER;
    *pdwOptions = 0x3ffaf & dwMask;
    return S_OK;
}

static HRESULT WINAPI BackReferences_QueryInterface(IObjectWithBackReferences *This, REFIID riid, PVOID *ppvObject)
{
    return Unknown_QueryInterface(CONTAINING_RECORD(This, CInternetFolder, IObjectWithBackReferences_iface), riid, ppvObject);
}

static ULONG WINAPI BackReferences_AddRef(IObjectWithBackReferences *This)
{
    return Unknown_AddRef(CONTAINING_RECORD(This, CInternetFolder, IObjectWithBackReferences_iface));
}

static ULONG WINAPI BackReferences_Release(IObjectWithBackReferences *This)
{
    return Unknown_Release(CONTAINING_RECORD(This, CInternetFolder, IObjectWithBackReferences_iface));
}

static HRESULT WINAPI BackReferences_RemoveBackReferences(IObjectWithBackReferences *This)
{
    return S_OK;
}

static const IShellFolder2Vtbl ShellFolderVtbl = {
    ShellFolder_QueryInterface,
    ShellFolder_AddRef,
    ShellFolder_Release,
    ParseDisplayName,
    ShellFolder_EnumObjects,
    BindToObject,
    BindToStorage,
    CompareIDs,
    CreateViewObject,
    GetAttributesOf,
    GetUIObjectOf,
    GetDisplayNameOf,
    SetNameOf,
    GetDefaultSearchGUID,
    EnumSearches,
    GetDefaultColumn,
    GetDefaultColumnState,
    GetDetailsEx,
    GetDetailsOf,
    MapColumnToSCID
};

static const IPersistFolder2Vtbl PersistFolderVtbl = {
    PersistFolder_QueryInterface,
    PersistFolder_AddRef,
    PersistFolder_Release,
    PersistFolder_GetClassID,
    PersistFolder_Initialize,
    PersistFolder_GetCurFolder
};

static const IBrowserFrameOptionsVtbl FrameOptionsVtbl = {
    FrameOptions_QueryInterface,
    FrameOptions_AddRef,
    FrameOptions_Release,
    FrameOptions_GetFrameOptions
};

static const IObjectWithBackReferencesVtbl BackReferencesVtbl = {
    BackReferences_QueryInterface,
    BackReferences_AddRef,
    BackReferences_Release,
    BackReferences_RemoveBackReferences
};

static CInternetFolder* CreateInstance(void)
{
    CInternetFolder *obj = SHAlloc(sizeof(CInternetFolder));
    if (obj)
    {
        obj->IShellFolder2_iface.lpVtbl = &ShellFolderVtbl;
        obj->IPersistFolder2_iface.lpVtbl = &PersistFolderVtbl;
        obj->IBrowserFrameOptions_iface.lpVtbl = &FrameOptionsVtbl;
        obj->IObjectWithBackReferences_iface.lpVtbl = &BackReferencesVtbl;
        obj->refCount = 1;
        obj->pidlRoot = NULL;
        WSTORAGE_LockModule();
    }
    return obj;
}

HRESULT WINAPI CInternetFolder_CreateInstance(IClassFactory *iface, IUnknown *outer, REFIID riid, void **ppv)
{
    CInternetFolder *pThis;

    *ppv = NULL;
    if (outer)
        return CLASS_E_NOAGGREGATION;

    if ((pThis = CreateInstance()) != NULL)
    {
        HRESULT hr = Unknown_QueryInterface(pThis, riid, ppv);
        Unknown_Release(pThis);
        return hr;
    }
    return E_OUTOFMEMORY;
}
