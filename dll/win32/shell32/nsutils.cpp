/*
 * PROJECT:     LiberNT Shell
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     Namespace helpers shared by shell32 and windows.storage
 * COPYRIGHT:   Copyright 1997 Marcus Meissner
 *              Copyright 1998, 1999, 2002 Juergen Schmied
 *              Copyright 2017-2024 Katayama Hirofumi MZ <katayama.hirofumi.mz@gmail.com>
 *              Copyright 2020 Mark Jansen (mark.jansen@reactos.org)
 */

#include "precomp.h"

WINE_DEFAULT_DEBUG_CHANNEL(shell);

HWND
CStubWindow32::FindStubWindow(UINT Type, LPCWSTR Path)
{
    for (HWND hWnd, hWndAfter = NULL;;)
    {
        hWnd = hWndAfter = FindWindowExW(NULL, hWndAfter, CSTUBWINDOW32_CLASSNAME, Path);
        if (!hWnd || !Path)
            return NULL;
        if (GetPropW(hWnd, GetTypePropName()) == ULongToHandle(Type))
            return hWnd;
    }
}

HRESULT
CStubWindow32::CreateStub(UINT Type, LPCWSTR Path, const POINT *pPt)
{
    if (HWND hWnd = FindStubWindow(Type, Path))
    {
        ::SwitchToThisWindow(::GetLastActivePopup(hWnd), TRUE);
        return HRESULT_FROM_WIN32(ERROR_ALREADY_EXISTS);
    }
    RECT rcPosition = { pPt ? pPt->x : CW_USEDEFAULT, pPt ? pPt->y : CW_USEDEFAULT, 0, 0 };
    DWORD Style = WS_DISABLED | WS_CLIPSIBLINGS | WS_CAPTION;
    DWORD ExStyle = WS_EX_WINDOWEDGE | WS_EX_APPWINDOW;
    if (!Create(NULL, rcPosition, Path, Style, ExStyle))
    {
        ERR("StubWindow32 creation failed\n");
        return E_FAIL;
    }
    ::SetPropW(*this, GetTypePropName(), ULongToHandle(Type));
    return S_OK;
}

BOOL PathIsDotOrDotDotW(_In_ LPCWSTR pszPath)
{
    if (pszPath[0] != L'.')
        return FALSE;
    return !pszPath[1] || (pszPath[1] == L'.' && !pszPath[2]);
}

#define PATH_VALID_ELEMENT ( \
    PATH_CHAR_CLASS_DOT | PATH_CHAR_CLASS_SEMICOLON | PATH_CHAR_CLASS_COMMA | \
    PATH_CHAR_CLASS_SPACE | PATH_CHAR_CLASS_OTHER_VALID \
)

BOOL PathIsValidElement(_In_ LPCWSTR pszPath)
{
    if (!*pszPath || PathIsDotOrDotDotW(pszPath))
        return FALSE;

    for (LPCWSTR pch = pszPath; *pch; ++pch)
    {
        if (!PathIsValidCharW(*pch, PATH_VALID_ELEMENT))
            return FALSE;
    }

    return TRUE;
}

BOOL PathIsDosDevice(_In_ LPCWSTR pszName)
{
    WCHAR szPath[MAX_PATH];
    StringCchCopyW(szPath, _countof(szPath), pszName);
    PathRemoveExtensionW(szPath);

    if (lstrcmpiW(szPath, L"NUL") == 0 || lstrcmpiW(szPath, L"PRN") == 0 ||
        lstrcmpiW(szPath, L"CON") == 0 || lstrcmpiW(szPath, L"AUX") == 0)
    {
        return TRUE;
    }

    if (_wcsnicmp(szPath, L"LPT", 3) == 0 || _wcsnicmp(szPath, L"COM", 3) == 0)
    {
        if ((L'0' <= szPath[3] && szPath[3] <= L'9') && szPath[4] == UNICODE_NULL)
            return TRUE;
    }

    return FALSE;
}

HRESULT SHILAppend(_Inout_ LPITEMIDLIST pidl, _Inout_ LPITEMIDLIST *ppidl)
{
    LPITEMIDLIST pidlOld = *ppidl;
    if (!pidlOld)
    {
        *ppidl = pidl;
        return S_OK;
    }

    HRESULT hr = SHILCombine(*ppidl, pidl, ppidl);
    ILFree(pidlOld);
    ILFree(pidl);
    return hr;
}

BOOL BindCtx_ContainsObject(_In_ IBindCtx *pBindCtx, _In_ LPCWSTR pszName)
{
    CComPtr<IUnknown> punk;
    if (!pBindCtx || FAILED(pBindCtx->GetObjectParam(const_cast<LPWSTR>(pszName), &punk)))
        return FALSE;
    return TRUE;
}

DWORD BindCtx_GetMode(_In_ IBindCtx *pbc, _In_ DWORD dwDefault)
{
    if (!pbc)
        return dwDefault;

    BIND_OPTS BindOpts = { sizeof(BindOpts) };
    HRESULT hr = pbc->GetBindOptions(&BindOpts);
    if (FAILED(hr))
        return dwDefault;

    return BindOpts.grfMode;
}

BOOL SHSkipJunctionBinding(_In_ IBindCtx *pbc, _In_ CLSID *pclsid)
{
    if (!pbc)
        return FALSE;

    BIND_OPTS BindOps = { sizeof(BindOps) };
    if (SUCCEEDED(pbc->GetBindOptions(&BindOps)) && BindOps.grfFlags == OLECONTF_LINKS)
        return TRUE;

    return pclsid && SHSkipJunction(pbc, pclsid);
}

HRESULT SHIsFileSysBindCtx(_In_ IBindCtx *pBindCtx, _Out_opt_ WIN32_FIND_DATAW *pFindData)
{
    CComPtr<IUnknown> punk;
    CComPtr<IFileSystemBindData> pBindData;

    if (!pBindCtx || FAILED(pBindCtx->GetObjectParam((LPWSTR)STR_FILE_SYS_BIND_DATA, &punk)))
        return S_FALSE;

    if (FAILED(punk->QueryInterface(IID_PPV_ARG(IFileSystemBindData, &pBindData))))
        return S_FALSE;

    if (pFindData)
        pBindData->GetFindData(pFindData);

    return S_OK;
}

BOOL Shell_FailForceReturn(_In_ HRESULT hr)
{
    DWORD code = HRESULT_CODE(hr);

    switch (code)
    {
        case ERROR_BAD_NETPATH:
        case ERROR_BAD_NET_NAME:
        case ERROR_CANCELLED:
            return TRUE;

        default:
            return (ERROR_FILE_NOT_FOUND <= code && code <= ERROR_PATH_NOT_FOUND);
    }
}

SHSTDAPI
SHBindToObject(
    _In_opt_ IShellFolder *pShellFolder,
    _In_ PCUIDLIST_RELATIVE pidl,
    _In_opt_ IBindCtx *pBindCtx,
    _In_ REFIID riid,
    _Out_ void **ppvObj)
{
    CComPtr<IShellFolder> psfDesktop;

    *ppvObj = NULL;

    if (!pShellFolder)
    {
        SHGetDesktopFolder(&psfDesktop);
        if (!psfDesktop)
            return E_FAIL;

        pShellFolder = psfDesktop;
    }

    HRESULT hr;
    if (_ILIsDesktop(pidl))
        hr = pShellFolder->QueryInterface(riid, ppvObj);
    else
        hr = pShellFolder->BindToObject(pidl, pBindCtx, riid, ppvObj);

    if (SUCCEEDED(hr) && !*ppvObj)
        hr = E_FAIL;

    return hr;
}

EXTERN_C HRESULT
SHELL_GetUIObjectOfAbsoluteItem(
    _In_opt_ HWND hWnd,
    _In_ PCIDLIST_ABSOLUTE pidl,
    _In_ REFIID riid, _Out_ void **ppvObj)
{
    if (!ppvObj)
        return E_INVALIDARG;
    *ppvObj = NULL;
    IShellFolder *psf;
    PCUITEMID_CHILD pidlChild;
    HRESULT hr = SHBindToParent(pidl, IID_PPV_ARG(IShellFolder, &psf), &pidlChild);
    if (SUCCEEDED(hr))
    {
        hr = psf->GetUIObjectOf(hWnd, 1, &pidlChild, riid, NULL, ppvObj);
        psf->Release();
        if (SUCCEEDED(hr))
        {
            if (*ppvObj)
                return hr;
            hr = E_FAIL;
        }
    }
    return hr;
}

HRESULT
SHELL_DisplayNameOf(
    _In_opt_ IShellFolder *psf,
    _In_ LPCITEMIDLIST pidl,
    _In_opt_ UINT Flags,
    _Out_ PWSTR *ppStr)
{
    HRESULT hr;
    CComPtr<IShellFolder> psfRoot;
    if (!psf)
    {
        PCUITEMID_CHILD pidlChild;
        hr = SHBindToParent(pidl, IID_PPV_ARG(IShellFolder, &psfRoot), &pidlChild);
        if (FAILED(hr))
            return hr;
        psf = psfRoot;
        pidl = pidlChild;
    }
    STRRET sr;
    hr = psf->GetDisplayNameOf((PCUITEMID_CHILD)pidl, Flags, &sr);
    return SUCCEEDED(hr) ? StrRetToStrW(&sr, pidl, ppStr) : hr;
}

/***********************************************************************
 *    DisplayNameOfW [SHELL32.757] (Vista+)
 */
EXTERN_C HRESULT WINAPI
DisplayNameOfW(
    _In_ IShellFolder *psf,
    _In_ LPCITEMIDLIST pidl,
    _In_ DWORD dwFlags,
    _Out_ LPWSTR pszBuf,
    _In_ UINT cchBuf)
{
    *pszBuf = UNICODE_NULL;
    STRRET sr;
    HRESULT hr = psf->GetDisplayNameOf(pidl, dwFlags, &sr);
    if (FAILED(hr))
        return hr;
    return StrRetToBufW(&sr, pidl, pszBuf, cchBuf);
}

DWORD
SHGetAttributes(_In_ IShellFolder *psf, _In_ LPCITEMIDLIST pidl, _In_ DWORD dwAttributes)
{
    LPCITEMIDLIST pidlLast = pidl;
    IShellFolder *release = NULL;

    if (!psf)
    {
        SHBindToParent(pidl, IID_PPV_ARG(IShellFolder, &psf), &pidlLast);
        if (!psf)
            return 0;
        release = psf;
    }

    DWORD oldAttrs = dwAttributes;
    if (FAILED(psf->GetAttributesOf(1, &pidlLast, &dwAttributes)))
        dwAttributes = 0;
    else
        dwAttributes &= oldAttrs;

    if ((dwAttributes & SFGAO_FOLDER) &&
        (dwAttributes & SFGAO_STREAM) &&
        !(dwAttributes & SFGAO_STORAGEANCESTOR) &&
        (oldAttrs & SFGAO_STORAGEANCESTOR) &&
        (SHGetObjectCompatFlags(psf, NULL) & 0x200))
    {
        dwAttributes &= ~(SFGAO_STREAM | SFGAO_STORAGEANCESTOR);
        dwAttributes |= SFGAO_STORAGEANCESTOR;
    }

    if (release)
        release->Release();
    return dwAttributes;
}

HRESULT SHELL_GetIDListTarget(_In_ LPCITEMIDLIST pidl, _Out_ PIDLIST_ABSOLUTE *ppidl)
{
    IShellLink *pSL;
    HRESULT hr = SHBindToObject(NULL, pidl, NULL, IID_PPV_ARG(IShellLink, &pSL));
    if (SUCCEEDED(hr))
    {
        hr = pSL->GetIDList(ppidl); // Note: Returns S_FALSE if no target pidl
        pSL->Release();
    }
    return hr;
}

HRESULT SHCoInitializeAnyApartment(VOID)
{
    HRESULT hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    if (FAILED(hr))
        hr = CoInitializeEx(NULL, COINIT_DISABLE_OLE1DDE);
    return hr;
}

HRESULT
SHGetNameAndFlagsW(
    _In_ LPCITEMIDLIST pidl,
    _In_ DWORD dwFlags,
    _Out_opt_ LPWSTR pszText,
    _In_ UINT cchBuf,
    _Inout_opt_ DWORD *pdwAttributes)
{
    if (pszText)
        *pszText = UNICODE_NULL;

    HRESULT hrCoInit = SHCoInitializeAnyApartment();

    CComPtr<IShellFolder> psfFolder;
    LPCITEMIDLIST ppidlLast;
    HRESULT hr = SHBindToParent(pidl, IID_PPV_ARG(IShellFolder, &psfFolder), &ppidlLast);
    if (SUCCEEDED(hr))
    {
        if (pszText)
            hr = DisplayNameOfW(psfFolder, ppidlLast, dwFlags, pszText, cchBuf);

        if (SUCCEEDED(hr))
        {
            if (pdwAttributes)
                *pdwAttributes = SHGetAttributes(psfFolder, ppidlLast, *pdwAttributes);
        }
    }

    if (SUCCEEDED(hrCoInit))
        CoUninitialize();

    return hr;
}

EXTERN_C HWND
BindCtx_GetUIWindow(_In_ IBindCtx *pBindCtx)
{
    HWND hWnd = NULL;

    CComPtr<IUnknown> punk;
    if (pBindCtx && SUCCEEDED(pBindCtx->GetObjectParam((LPWSTR)L"UI During Binding", &punk)))
        IUnknown_GetWindow(punk, &hWnd);

    return hWnd;
}

class CDummyOleWindow : public IOleWindow
{
protected:
    LONG m_cRefs;
    HWND m_hWnd;

public:
    CDummyOleWindow() : m_cRefs(1), m_hWnd(NULL) { }
    virtual ~CDummyOleWindow() { }

    // IUnknown methods
    STDMETHODIMP QueryInterface(REFIID riid, LPVOID *ppvObj) override
    {
        static const QITAB c_tab[] =
        {
            QITABENT(CDummyOleWindow, IOleWindow),
            { NULL }
        };
        return ::QISearch(this, c_tab, riid, ppvObj);
    }
    STDMETHODIMP_(ULONG) AddRef() override
    {
        return ++m_cRefs;
    }
    STDMETHODIMP_(ULONG) Release() override
    {
        if (--m_cRefs == 0)
        {
            delete this;
            return 0;
        }
        return m_cRefs;
    }

    // IOleWindow methods
    STDMETHODIMP GetWindow(HWND *phWnd) override
    {
        *phWnd = m_hWnd;
        if (!m_hWnd)
            return E_NOTIMPL;
        return S_OK;
    }
    STDMETHODIMP ContextSensitiveHelp(BOOL fEnterMode) override
    {
        return E_NOTIMPL;
    }
};

EXTERN_C HRESULT
BindCtx_RegisterObjectParam(
    _In_ IBindCtx *pBindCtx,
    _In_ LPOLESTR pszKey,
    _In_opt_ IUnknown *punk,
    _Out_ LPBC *ppbc)
{
    HRESULT hr = S_OK;
    CDummyOleWindow *pUnknown = NULL;

    *ppbc = pBindCtx;

    if (pBindCtx)
    {
        pBindCtx->AddRef();
    }
    else
    {
        hr = CreateBindCtx(0, ppbc);
        if (FAILED(hr))
            return hr;
    }

    if (!punk)
        punk = pUnknown = new CDummyOleWindow();

    hr = (*ppbc)->RegisterObjectParam(pszKey, punk);

    if (pUnknown)
        pUnknown->Release();

    if (FAILED(hr))
    {
        (*ppbc)->Release();
        *ppbc = NULL;
    }

    return hr;
}

HRESULT
SHELL_MapContextMenuVerbToCmdId(LPCMINVOKECOMMANDINFO pICI, const CMVERBMAP *pMap)
{
    LPCSTR pVerbA = pICI->lpVerb;
    CHAR buf[MAX_PATH];
    LPCMINVOKECOMMANDINFOEX pICIX = (LPCMINVOKECOMMANDINFOEX)pICI;
    if (IsUnicode(*pICIX) && !IS_INTRESOURCE(pICIX->lpVerbW))
    {
        if (SHUnicodeToAnsi(pICIX->lpVerbW, buf, _countof(buf)))
            pVerbA = buf;
    }

    if (IS_INTRESOURCE(pVerbA))
        return LOWORD(pVerbA);
    for (SIZE_T i = 0; pMap[i].Verb; ++i)
    {
        assert(SUCCEEDED((int)(pMap[i].CmdId))); // The id must be >= 0 and ideally in the 0..0x7fff range
        if (!lstrcmpiA(pMap[i].Verb, pVerbA) && pVerbA[0])
            return pMap[i].CmdId;
    }
    return E_FAIL;
}

static const CMVERBMAP*
FindVerbMapEntry(UINT_PTR CmdId, const CMVERBMAP *pMap)
{
    for (SIZE_T i = 0; pMap[i].Verb; ++i)
    {
        if (pMap[i].CmdId == CmdId)
            return &pMap[i];
    }
    return NULL;
}

HRESULT
SHELL_GetCommandStringImpl(SIZE_T CmdId, UINT uFlags, LPSTR Buf, UINT cchBuf, const CMVERBMAP *pMap)
{
    const CMVERBMAP* pEntry;
    switch (uFlags | GCS_UNICODE)
    {
        case GCS_VALIDATEW:
        case GCS_VERBW:
            pEntry = FindVerbMapEntry(CmdId, pMap);
            if ((uFlags | GCS_UNICODE) == GCS_VERBW)
            {
                if (!pEntry)
                    return E_INVALIDARG;
                else if (uFlags & GCS_UNICODE)
                    return SHAnsiToUnicode(pEntry->Verb, (LPWSTR)Buf, cchBuf) ? S_OK : E_FAIL;
                else
                    return StringCchCopyA(Buf, cchBuf, pEntry->Verb);
            }
            return pEntry ? S_OK : S_FALSE; // GCS_VALIDATE
    }
    return E_NOTIMPL;
}

typedef struct tagALIAS_MAPPING
{
    BYTE bFlagMask;      // The combination of ALIAS_USER_FOLDER and/or ALIAS_DESKTOP
    BYTE bCommonDesktop;
    WORD nCsidlSrc;      // CSIDL_... (source)
    WORD nCsidlDest;     // CSIDL_... (destination)
} ALIAS_MAPPING, *PALIAS_MAPPING;

//! PIDL alias table
static const ALIAS_MAPPING g_AliasTable[] =
{
    {
        ALIAS_USER_FOLDER,
        FALSE,
        CSIDL_PERSONAL | CSIDL_FLAG_NO_ALIAS,
        CSIDL_PERSONAL
    },
    {
        ALIAS_USER_FOLDER | ALIAS_DESKTOP,
        FALSE,
        CSIDL_COMMON_DOCUMENTS | CSIDL_FLAG_NO_ALIAS,
        CSIDL_COMMON_DOCUMENTS
    },
    {
        ALIAS_DESKTOP,
        FALSE,
        CSIDL_DESKTOPDIRECTORY,
        CSIDL_DESKTOP
    },
    {
        ALIAS_DESKTOP,
        TRUE,
        CSIDL_COMMON_DESKTOPDIRECTORY,
        CSIDL_DESKTOP
    }
};

//! Translate a PIDL to an "alias" PIDL.
EXTERN_C BOOL
SHELL32_ReparentAsAliasPidl(
    _In_opt_ HWND hwnd,
    _In_opt_ HANDLE hToken,
    _In_ LPCITEMIDLIST pidlTarget,
    _Out_ LPITEMIDLIST *ppidlNew,
    _In_ DWORD dwFlags)
{
    if (!pidlTarget || !ppidlNew)
        return FALSE;

    *ppidlNew = NULL;

    for (SIZE_T iEntry = 0; iEntry < _countof(g_AliasTable); ++iEntry)
    {
        const ALIAS_MAPPING *pEntry = &g_AliasTable[iEntry];

        if (!(dwFlags & pEntry->bFlagMask))
            continue;

        // Get the source root PIDL
        LPITEMIDLIST pidlSrcRoot = NULL;
        HRESULT hr = SHGetFolderLocation(hwnd, pEntry->nCsidlSrc, hToken, 0, &pidlSrcRoot);
        if (FAILED(hr))
            continue;

        // Check whether the input pidlTarget is under the source folder.
        // If it matches, ILFindChild returns the relative PIDL in the pidlTarget.
        LPCITEMIDLIST pidlRelative = ILFindChild(pidlSrcRoot, pidlTarget);
        if (!pidlRelative) // Not found?
        {
            ILFree(pidlSrcRoot);
            continue;
        }

        // Found. Get the destination root PIDL
        LPITEMIDLIST pidlDestRoot = NULL;
        hr = SHGetFolderLocation(hwnd, pEntry->nCsidlDest, hToken, 0, &pidlDestRoot);
        if (SUCCEEDED(hr))
        {
            // Create a new PIDL by combining the destination root PIDL and the relative PIDL
            *ppidlNew = ILCombine(pidlDestRoot, pidlRelative);
            if (*ppidlNew)
            {
                // Manipulate specific flags in the PIDL if necessary
                if (pEntry->bCommonDesktop && (*ppidlNew)->mkid.cb >= 3)
                {
                    (*ppidlNew)->mkid.abID[0] |= (PT_FS | PT_FS_COMMON_FLAG);
                }
            }
            ILFree(pidlDestRoot);
        }

        ILFree(pidlSrcRoot);
        break; // A match was found, so exit the loop
    }

    return (*ppidlNew != NULL);
}

//! Translate a PIDL to an "alias" PIDL.
EXTERN_C HRESULT
SHELL32_AliasTranslatePidl(
    _In_ LPCITEMIDLIST pidl,
    _Out_ LPITEMIDLIST *ppidlNew,
    _In_ DWORD dwFlags)
{
    return SHELL32_ReparentAsAliasPidl(NULL, NULL, pidl, ppidlNew, dwFlags) ? S_OK : E_FAIL;
}

static BOOL IsRealItem(const ITEMIDLIST &idl)
{
    // PIDLs created with SHSimpleIDListFromPath contain no data, otherwise, the item is "real"
    FileStruct &fsitem = ((PIDLDATA*)idl.mkid.abID)->u.file;
    return fsitem.dwFileSize | fsitem.uFileDate;
}

HRESULT CFSFolder::CompareSortFoldersFirst(LPCITEMIDLIST pidl1, LPCITEMIDLIST pidl2)
{
    BOOL bIsFolder1 = _ILIsFolder(pidl1), bIsFolder2 = _ILIsFolder(pidl2);
    // When sorting between a File and a Folder, the Folder gets sorted first
    if (bIsFolder1 != bIsFolder2)
    {
        // ...but only if neither of them were generated by SHSimpleIDListFromPath
        // because in that case we cannot tell if it's a file or a folder.
        if (pidl1 && IsRealItem(*pidl1) && pidl2 && IsRealItem(*pidl2))
            return MAKE_COMPARE_HRESULT(bIsFolder1 ? -1 : 1);
    }
    return MAKE_SCODE(SEVERITY_ERROR, FACILITY_SHELL, S_EQUAL);
}

HRESULT CFSFolder::FormatDateTime(const FILETIME &ft, LPWSTR Buf, UINT cchBuf)
{
    FILETIME lft;
    SYSTEMTIME time;
    FileTimeToLocalFileTime(&ft, &lft);
    FileTimeToSystemTime(&lft, &time);
    UINT ret = GetDateFormatW(LOCALE_USER_DEFAULT, DATE_SHORTDATE, &time, NULL, Buf, cchBuf);
    if (ret > 0 && ret < cchBuf)
    {
        /* Append space + time without seconds */
        Buf[ret-1] = ' ';
        GetTimeFormatW(LOCALE_USER_DEFAULT, TIME_NOSECONDS, &time, NULL, &Buf[ret], cchBuf - ret);
    }
    return ret ? S_OK : E_FAIL;
}

HRESULT CFSFolder::FormatSize(UINT64 size, LPWSTR Buf, UINT cchBuf)
{
    if (StrFormatKBSizeW(size, Buf, cchBuf))
        return S_OK;
    if (cchBuf)
        *Buf = UNICODE_NULL;
    return E_FAIL;
}

HRESULT CDrivesFolder::SetDriveLabel(HWND hwndOwner, PCWSTR DrivePath, PCWSTR Label)
{
    HRESULT hr = SetVolumeLabelW(DrivePath, *Label ? Label : NULL) ? S_OK : HResultFromWin32(GetLastError());
    if (SUCCEEDED(hr))
        SHChangeNotify(SHCNE_RENAMEFOLDER, SHCNF_PATHW, DrivePath, DrivePath); // DisplayName changed
    else if (hwndOwner)
        SHELL_ErrorBox(hwndOwner, hr);
    return hr;
}
