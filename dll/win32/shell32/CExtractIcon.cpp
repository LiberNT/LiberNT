/*
 * COPYRIGHT:       See COPYING in the top level directory
 * PROJECT:         ReactOS Registry namespace extension
 * FILE:            dll/win32/shell32/extracticon.c
 * PURPOSE:         Icon extraction
 *
 * PROGRAMMERS:     Hervé Poussineau (hpoussin@reactos.org)
 *                  Ahmed ARIF (arif.ing@outlook.com)
 */

#include "precomp.h"

WINE_DEFAULT_DEBUG_CHANNEL(shell);

struct IconLocation
{
    LPWSTR file;
    UINT index;
};

class CExtractIcon :
    public CComObjectRootEx<CComMultiThreadModelNoCS>,
    public IDefaultExtractIconInit,
    public IExtractIconW,
    public IExtractIconA,
    public IPersistFile
{
private:
    UINT flags;
    struct IconLocation defaultIcon;
    struct IconLocation normalIcon;
    struct IconLocation openIcon;
    struct IconLocation shortcutIcon;
public:
    CExtractIcon();
    ~CExtractIcon();

    // IDefaultExtractIconInit
    STDMETHOD(SetDefaultIcon)(LPCWSTR pszFile, int iIcon) override;
    STDMETHOD(SetFlags)(UINT uFlags) override;
    STDMETHOD(SetKey)(HKEY hkey) override;
    STDMETHOD(SetNormalIcon)(LPCWSTR pszFile, int iIcon) override;
    STDMETHOD(SetOpenIcon)(LPCWSTR pszFile, int iIcon) override;
    STDMETHOD(SetShortcutIcon)(LPCWSTR pszFile, int iIcon) override;

    // IExtractIconW
    STDMETHOD(GetIconLocation)(UINT uFlags, LPWSTR szIconFile, UINT cchMax, int *piIndex, UINT *pwFlags) override;
    STDMETHOD(Extract)(LPCWSTR pszFile, UINT nIconIndex, HICON *phiconLarge, HICON *phiconSmall, UINT nIconSize) override;

    // IExtractIconA
    STDMETHOD(GetIconLocation)(UINT uFlags, LPSTR szIconFile, UINT cchMax, int *piIndex, UINT *pwFlags) override;
    STDMETHOD(Extract)(LPCSTR pszFile, UINT nIconIndex, HICON *phiconLarge, HICON *phiconSmall, UINT nIconSize) override;

    // IPersist
    STDMETHOD(GetClassID)(CLSID *pClassID) override;
    STDMETHOD(IsDirty)() override;

    // IPersistFile
    STDMETHOD(Load)(LPCOLESTR pszFileName, DWORD dwMode) override;
    STDMETHOD(Save)(LPCOLESTR pszFileName, BOOL fRemember) override;
    STDMETHOD(SaveCompleted)(LPCOLESTR pszFileName) override;
    STDMETHOD(GetCurFile)(LPOLESTR *ppszFileName) override;

BEGIN_COM_MAP(CExtractIcon)
    COM_INTERFACE_ENTRY_IID(IID_IDefaultExtractIconInit, IDefaultExtractIconInit)
    COM_INTERFACE_ENTRY_IID(IID_IExtractIconW, IExtractIconW)
    COM_INTERFACE_ENTRY_IID(IID_IExtractIconA, IExtractIconA)
    COM_INTERFACE_ENTRY_IID(IID_IPersist, IPersist)
    COM_INTERFACE_ENTRY_IID(IID_IPersistFile, IPersistFile)
END_COM_MAP()
};

VOID DuplicateString(
    LPCWSTR Source,
    LPWSTR *Destination)
{
    SIZE_T cb;

    if (*Destination)
        CoTaskMemFree(*Destination);

    cb = (wcslen(Source) + 1) * sizeof(WCHAR);
    *Destination = (LPWSTR)CoTaskMemAlloc(cb);
    if (!*Destination)
        return;
    CopyMemory(*Destination, Source, cb);
}

CExtractIcon::CExtractIcon()
{
    flags = 0;
    memset(&defaultIcon, 0, sizeof(defaultIcon));
    memset(&normalIcon, 0, sizeof(normalIcon));
    memset(&openIcon, 0, sizeof(openIcon));
    memset(&shortcutIcon, 0, sizeof(shortcutIcon));
}

CExtractIcon::~CExtractIcon()
{
    if (defaultIcon.file) CoTaskMemFree(defaultIcon.file);
    if (normalIcon.file) CoTaskMemFree(normalIcon.file);
    if (openIcon.file) CoTaskMemFree(openIcon.file);
    if (shortcutIcon.file) CoTaskMemFree(shortcutIcon.file);
}

HRESULT STDMETHODCALLTYPE CExtractIcon::SetDefaultIcon(
    LPCWSTR pszFile,
    int iIcon)
{
    TRACE("(%p, %s, %d)\n", this, debugstr_w(pszFile), iIcon);

    DuplicateString(pszFile, &defaultIcon.file);
    if (!defaultIcon.file)
        return E_OUTOFMEMORY;
    defaultIcon.index = iIcon;
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CExtractIcon::SetFlags(
    UINT uFlags)
{
    TRACE("(%p, 0x%x)\n", this, uFlags);

    flags = uFlags;
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CExtractIcon::SetKey(
    HKEY hkey)
{
    FIXME("(%p, %p)\n", this, hkey);
    UNIMPLEMENTED;
    return E_NOTIMPL;
}

HRESULT STDMETHODCALLTYPE CExtractIcon::SetNormalIcon(
    LPCWSTR pszFile,
    int iIcon)
{
    TRACE("(%p, %s, %d)\n", this, debugstr_w(pszFile), iIcon);

    DuplicateString(pszFile, &normalIcon.file);
    if (!normalIcon.file)
        return E_OUTOFMEMORY;
    normalIcon.index = iIcon;
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CExtractIcon::SetOpenIcon(
    LPCWSTR pszFile,
    int iIcon)
{
    TRACE("(%p, %s, %d)\n", this, debugstr_w(pszFile), iIcon);

    DuplicateString(pszFile, &openIcon.file);
    if (!openIcon.file)
        return E_OUTOFMEMORY;
    openIcon.index = iIcon;
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CExtractIcon::SetShortcutIcon(
    LPCWSTR pszFile,
    int iIcon)
{
    TRACE("(%p, %s, %d)\n", this, debugstr_w(pszFile), iIcon);

    DuplicateString(pszFile, &shortcutIcon.file);
    if (!shortcutIcon.file)
        return E_OUTOFMEMORY;
    shortcutIcon.index = iIcon;
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CExtractIcon::GetIconLocation(
    UINT uFlags,
    LPWSTR szIconFile,
    UINT cchMax,
    int *piIndex,
    UINT *pwFlags)
{
    const struct IconLocation *icon = NULL;
    SIZE_T cb;

    TRACE("(%p, 0x%x, %s, 0x%x, %p, %p)\n", this, uFlags, debugstr_w(szIconFile), cchMax, piIndex, pwFlags);

    if (!piIndex || !pwFlags)
        return E_POINTER;

    if (uFlags & GIL_DEFAULTICON)
        icon = defaultIcon.file ? &defaultIcon : &normalIcon;
    else if (uFlags & GIL_FORSHORTCUT)
        icon = shortcutIcon.file ? &shortcutIcon : &normalIcon;
    else if (uFlags & GIL_OPENICON)
        icon = openIcon.file ? &openIcon : &normalIcon;
    else
        icon = &normalIcon;

    if (!icon->file)
        return E_FAIL;

    cb = wcslen(icon->file) + 1;
    if (cchMax < (UINT)cb)
        return E_FAIL;
    CopyMemory(szIconFile, icon->file, cb * sizeof(WCHAR));
    *piIndex = icon->index;
    *pwFlags = flags;
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CExtractIcon::Extract(
    LPCWSTR pszFile,
    UINT nIconIndex,
    HICON *phiconLarge,
    HICON *phiconSmall,
    UINT nIconSize)
{
    TRACE("(%p, %s, %u, %p, %p, %u)\n", this, debugstr_w(pszFile), nIconIndex, phiconLarge, phiconSmall, nIconSize);

    /* Nothing to do, ExtractIconW::GetIconLocation should be enough */
    return S_FALSE;
}

HRESULT STDMETHODCALLTYPE CExtractIcon::GetIconLocation(
    UINT uFlags,
    LPSTR szIconFile,
    UINT cchMax,
    int *piIndex,
    UINT *pwFlags)
{
    LPWSTR szIconFileW = NULL;
    HRESULT hr;

    if (cchMax > 0)
    {
        szIconFileW = (LPWSTR)CoTaskMemAlloc(cchMax * sizeof(WCHAR));
        if (!szIconFileW)
            return E_OUTOFMEMORY;
    }

    hr = GetIconLocation(uFlags, szIconFileW, cchMax, piIndex, pwFlags);
    if (SUCCEEDED(hr) && cchMax > 0)
        if (0 == WideCharToMultiByte(CP_ACP, 0, szIconFileW, cchMax, szIconFile, cchMax, NULL, NULL))
            hr = E_FAIL;

    if (szIconFileW)
        CoTaskMemFree(szIconFileW);
    return hr;
}

HRESULT STDMETHODCALLTYPE CExtractIcon::Extract(
    LPCSTR pszFile,
    UINT nIconIndex,
    HICON *phiconLarge,
    HICON *phiconSmall,
    UINT nIconSize)
{
    LPWSTR pszFileW = NULL;
    HRESULT hr;

    if (pszFile)
    {
        int nLength;

        nLength = MultiByteToWideChar(CP_ACP, 0, pszFile, -1, NULL, 0);
        if (nLength == 0)
            return E_FAIL;
        pszFileW = (LPWSTR)CoTaskMemAlloc(nLength * sizeof(WCHAR));
        if (!pszFileW)
            return E_OUTOFMEMORY;
        if (!MultiByteToWideChar(CP_ACP, 0, pszFile, nLength, pszFileW, nLength))
        {
            CoTaskMemFree(pszFileW);
            return E_FAIL;
        }
    }

    hr = Extract(pszFileW, nIconIndex, phiconLarge, phiconSmall, nIconSize);

    if (pszFileW)
        CoTaskMemFree(pszFileW);
    return hr;
}

HRESULT STDMETHODCALLTYPE CExtractIcon::GetClassID(
    CLSID *pClassID)
{
    TRACE("(%p, %p)\n", this, pClassID);

    if (!pClassID)
        return E_POINTER;

    *pClassID = GUID_NULL;
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CExtractIcon::IsDirty()
{
    FIXME("(%p)\n", this);
    UNIMPLEMENTED;
    return E_NOTIMPL;
}

HRESULT STDMETHODCALLTYPE CExtractIcon::Load(
    LPCOLESTR pszFileName,
    DWORD dwMode)
{
    FIXME("(%p, %s, %u)\n", this, debugstr_w(pszFileName), dwMode);
    UNIMPLEMENTED;
    return E_NOTIMPL;
}

HRESULT STDMETHODCALLTYPE CExtractIcon::Save(
    LPCOLESTR pszFileName,
    BOOL fRemember)
{
    FIXME("(%p, %s, %d)\n", this, debugstr_w(pszFileName), fRemember);
    UNIMPLEMENTED;
    return E_NOTIMPL;
}

HRESULT STDMETHODCALLTYPE CExtractIcon::SaveCompleted(
    LPCOLESTR pszFileName)
{
    FIXME("(%p, %s)\n", this, debugstr_w(pszFileName));
    UNIMPLEMENTED;
    return E_NOTIMPL;
}

HRESULT STDMETHODCALLTYPE CExtractIcon::GetCurFile(
    LPOLESTR *ppszFileName)
{
    FIXME("(%p, %p)\n", this, ppszFileName);
    UNIMPLEMENTED;
    return E_NOTIMPL;
}

HRESULT WINAPI SHCreateDefaultExtractIcon(REFIID riid, void **ppv)
{
    return ShellObjectCreator<CExtractIcon>(riid, ppv);
}

class CFileExtractIcon :
    public CComObjectRootEx<CComMultiThreadModelNoCS>,
    public IExtractIconW,
    public IExtractIconA
{
private:
    CComHeapPtr<WCHAR> m_pszFile;
    DWORD m_dwFileAttributes;

    HRESULT ExtractFromSystemImageList(UINT nIconIndex, HICON *phiconLarge, HICON *phiconSmall)
    {
        HIMAGELIST himlLarge, himlSmall;

        if (!Shell_GetImageLists(&himlLarge, &himlSmall))
            return E_FAIL;
        if (phiconLarge)
            *phiconLarge = ImageList_GetIcon(himlLarge, nIconIndex, ILD_NORMAL);
        if (phiconSmall)
            *phiconSmall = ImageList_GetIcon(himlSmall, nIconIndex, ILD_NORMAL);
        return S_OK;
    }

public:
    CFileExtractIcon() : m_dwFileAttributes(0)
    {
    }

    HRESULT Initialize(LPCWSTR pszFile, DWORD dwFileAttributes)
    {
        HRESULT hr = SHStrDupW(pszFile, &m_pszFile);
        m_dwFileAttributes = dwFileAttributes;
        return hr;
    }

    STDMETHOD(GetIconLocation)(UINT uFlags, LPWSTR szIconFile, UINT cchMax, int *piIndex, UINT *pwFlags) override
    {
        SHFILEINFOW shfi;

        if (uFlags & GIL_DEFAULTICON)
            return S_FALSE;
        if (!szIconFile || !cchMax || !piIndex || !pwFlags)
            return E_INVALIDARG;
        if (!SHGetFileInfoW(m_pszFile, m_dwFileAttributes, &shfi, sizeof(shfi), SHGFI_USEFILEATTRIBUTES | SHGFI_SYSICONINDEX))
            return E_FAIL;
        HRESULT hr = StringCchCopyW(szIconFile, cchMax, L"*");
        if (FAILED(hr))
            return hr;
        *piIndex = shfi.iIcon;
        *pwFlags = GIL_NOTFILENAME | GIL_PERCLASS;
        return S_OK;
    }

    STDMETHOD(Extract)(LPCWSTR pszFile, UINT nIconIndex, HICON *phiconLarge, HICON *phiconSmall, UINT nIconSize) override
    {
        if (pszFile && !wcscmp(pszFile, L"*"))
            return ExtractFromSystemImageList(nIconIndex, phiconLarge, phiconSmall);
        return SHDefExtractIconW(pszFile, nIconIndex, 0, phiconLarge, phiconSmall, nIconSize);
    }

    STDMETHOD(GetIconLocation)(UINT uFlags, LPSTR szIconFile, UINT cchMax, int *piIndex, UINT *pwFlags) override
    {
        WCHAR szIconFileW[MAX_PATH];

        if (!szIconFile || !cchMax)
            return E_INVALIDARG;
        HRESULT hr = GetIconLocation(uFlags, szIconFileW, _countof(szIconFileW), piIndex, pwFlags);
        if (hr != S_OK)
            return hr;
        if (!WideCharToMultiByte(CP_ACP, 0, szIconFileW, -1, szIconFile, cchMax, NULL, NULL))
            return E_FAIL;
        return S_OK;
    }

    STDMETHOD(Extract)(LPCSTR pszFile, UINT nIconIndex, HICON *phiconLarge, HICON *phiconSmall, UINT nIconSize) override
    {
        if (pszFile && !strcmp(pszFile, "*"))
            return ExtractFromSystemImageList(nIconIndex, phiconLarge, phiconSmall);
        return SHDefExtractIconA(pszFile, nIconIndex, 0, phiconLarge, phiconSmall, nIconSize);
    }

BEGIN_COM_MAP(CFileExtractIcon)
    COM_INTERFACE_ENTRY_IID(IID_IExtractIconW, IExtractIconW)
    COM_INTERFACE_ENTRY_IID(IID_IExtractIconA, IExtractIconA)
END_COM_MAP()
};

EXTERN_C
HRESULT
WINAPI
SHCreateFileExtractIconW(
    _In_ LPCWSTR pszFile,
    _In_ DWORD dwFileAttributes,
    _In_ REFIID riid,
    _Outptr_ void **ppv)
{
    if (!ppv)
        return E_POINTER;
    *ppv = NULL;
    if (!pszFile)
        return E_INVALIDARG;
    return ShellObjectCreatorInit<CFileExtractIcon>(pszFile, dwFileAttributes, riid, ppv);
}
