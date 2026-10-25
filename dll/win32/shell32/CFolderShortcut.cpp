/*
 * PROJECT:     LiberNT shell32
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     Folder shortcut namespace object
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"

#define FOLDERSHORTCUT_LINK L"target.lnk"
#define FOLDERSHORTCUT_INI L"desktop.ini"

class CFolderShortcut :
    public CComCoClass<CFolderShortcut, &CLSID_FolderShortcut>,
    public CComObjectRootEx<CComMultiThreadModelNoCS>,
    public IShellFolder2,
    public IPersistFolder3,
    public IShellLinkA,
    public IShellLinkW,
    public IPersistFile,
    public IExtractIconW,
    public IQueryInfo,
    public IPersistStreamInit,
    public IPersistPropertyBag,
    public IBrowserFrameOptions,
    public IShellIconOverlay
{
    CComPtr<IShellLinkW> m_pLink;
    CComPtr<IShellFolder2> m_pTarget;
    CComHeapPtr<ITEMIDLIST> m_pidlRoot;
    CComHeapPtr<ITEMIDLIST> m_pidlTarget;
    WCHAR m_szFolderPath[MAX_PATH];

    HRESULT _GetTargetIDList(PIDLIST_ABSOLUTE *ppidl)
    {
        *ppidl = NULL;
        if (m_pidlTarget)
        {
            *ppidl = ILClone(m_pidlTarget);
            return *ppidl ? S_OK : E_OUTOFMEMORY;
        }
        return m_pLink->GetIDList(ppidl);
    }

    HRESULT _EnsureTarget()
    {
        CComHeapPtr<ITEMIDLIST> pidl;
        CComPtr<IShellFolder> psfDesktop;
        HRESULT hr;

        if (m_pTarget)
            return S_OK;
        hr = _GetTargetIDList(&pidl);
        if (FAILED(hr))
            return hr;
        if (!pidl)
            return E_FAIL;
        hr = SHGetDesktopFolder(&psfDesktop);
        if (FAILED(hr))
            return hr;
        if (ILIsEmpty(pidl))
            return psfDesktop->QueryInterface(IID_PPV_ARG(IShellFolder2, &m_pTarget));
        return psfDesktop->BindToObject(pidl, NULL, IID_PPV_ARG(IShellFolder2, &m_pTarget));
    }

    HRESULT _SetTargetIDList(PCIDLIST_ABSOLUTE pidl)
    {
        HRESULT hr = m_pLink->SetIDList(pidl);
        if (FAILED(hr))
            return hr;
        m_pidlTarget.Free();
        m_pTarget.Release();
        return S_OK;
    }

    HRESULT _GetLinkPersistFile(IPersistFile **ppf)
    {
        return m_pLink->QueryInterface(IID_PPV_ARG(IPersistFile, ppf));
    }

public:
    CFolderShortcut()
    {
        m_szFolderPath[0] = UNICODE_NULL;
    }

    HRESULT FinalConstruct()
    {
        return SHCoCreateInstance(NULL, &CLSID_ShellLink, NULL, IID_PPV_ARG(IShellLinkW, &m_pLink));
    }

    // IShellFolder
    STDMETHOD(ParseDisplayName)(HWND hwndOwner, LPBC pbc, LPOLESTR lpszDisplayName, ULONG *pchEaten, PIDLIST_RELATIVE *ppidl, ULONG *pdwAttributes) override
    {
        HRESULT hr = _EnsureTarget();
        return FAILED(hr) ? hr : m_pTarget->ParseDisplayName(hwndOwner, pbc, lpszDisplayName, pchEaten, ppidl, pdwAttributes);
    }

    STDMETHOD(EnumObjects)(HWND hwndOwner, SHCONTF dwFlags, LPENUMIDLIST *ppEnumIDList) override
    {
        HRESULT hr = _EnsureTarget();
        return FAILED(hr) ? hr : m_pTarget->EnumObjects(hwndOwner, dwFlags, ppEnumIDList);
    }

    STDMETHOD(BindToObject)(PCUIDLIST_RELATIVE pidl, LPBC pbcReserved, REFIID riid, LPVOID *ppvOut) override
    {
        HRESULT hr = _EnsureTarget();
        return FAILED(hr) ? hr : m_pTarget->BindToObject(pidl, pbcReserved, riid, ppvOut);
    }

    STDMETHOD(BindToStorage)(PCUIDLIST_RELATIVE pidl, LPBC pbcReserved, REFIID riid, LPVOID *ppvOut) override
    {
        HRESULT hr = _EnsureTarget();
        return FAILED(hr) ? hr : m_pTarget->BindToStorage(pidl, pbcReserved, riid, ppvOut);
    }

    STDMETHOD(CompareIDs)(LPARAM lParam, PCUIDLIST_RELATIVE pidl1, PCUIDLIST_RELATIVE pidl2) override
    {
        HRESULT hr = _EnsureTarget();
        return FAILED(hr) ? hr : m_pTarget->CompareIDs(lParam, pidl1, pidl2);
    }

    STDMETHOD(CreateViewObject)(HWND hwndOwner, REFIID riid, LPVOID *ppvOut) override
    {
        HRESULT hr = _EnsureTarget();
        return FAILED(hr) ? hr : m_pTarget->CreateViewObject(hwndOwner, riid, ppvOut);
    }

    STDMETHOD(GetAttributesOf)(UINT cidl, PCUITEMID_CHILD_ARRAY apidl, SFGAOF *rgfInOut) override
    {
        HRESULT hr = _EnsureTarget();
        return FAILED(hr) ? hr : m_pTarget->GetAttributesOf(cidl, apidl, rgfInOut);
    }

    STDMETHOD(GetUIObjectOf)(HWND hwndOwner, UINT cidl, PCUITEMID_CHILD_ARRAY apidl, REFIID riid, UINT *prgfInOut, LPVOID *ppvOut) override
    {
        HRESULT hr = _EnsureTarget();
        return FAILED(hr) ? hr : m_pTarget->GetUIObjectOf(hwndOwner, cidl, apidl, riid, prgfInOut, ppvOut);
    }

    STDMETHOD(GetDisplayNameOf)(PCUITEMID_CHILD pidl, SHGDNF uFlags, STRRET *lpName) override
    {
        HRESULT hr = _EnsureTarget();
        return FAILED(hr) ? hr : m_pTarget->GetDisplayNameOf(pidl, uFlags, lpName);
    }

    STDMETHOD(SetNameOf)(HWND hwndOwner, PCUITEMID_CHILD pidl, LPCOLESTR lpName, SHGDNF uFlags, PITEMID_CHILD *pPidlOut) override
    {
        HRESULT hr = _EnsureTarget();
        return FAILED(hr) ? hr : m_pTarget->SetNameOf(hwndOwner, pidl, lpName, uFlags, pPidlOut);
    }

    // IShellFolder2
    STDMETHOD(GetDefaultSearchGUID)(GUID *pguid) override
    {
        HRESULT hr = _EnsureTarget();
        return FAILED(hr) ? hr : m_pTarget->GetDefaultSearchGUID(pguid);
    }

    STDMETHOD(EnumSearches)(IEnumExtraSearch **ppenum) override
    {
        HRESULT hr = _EnsureTarget();
        return FAILED(hr) ? hr : m_pTarget->EnumSearches(ppenum);
    }

    STDMETHOD(GetDefaultColumn)(DWORD dwRes, ULONG *pSort, ULONG *pDisplay) override
    {
        HRESULT hr = _EnsureTarget();
        return FAILED(hr) ? hr : m_pTarget->GetDefaultColumn(dwRes, pSort, pDisplay);
    }

    STDMETHOD(GetDefaultColumnState)(UINT iColumn, SHCOLSTATEF *pcsFlags) override
    {
        HRESULT hr = _EnsureTarget();
        return FAILED(hr) ? hr : m_pTarget->GetDefaultColumnState(iColumn, pcsFlags);
    }

    STDMETHOD(GetDetailsEx)(PCUITEMID_CHILD pidl, const SHCOLUMNID *pscid, VARIANT *pv) override
    {
        HRESULT hr = _EnsureTarget();
        return FAILED(hr) ? hr : m_pTarget->GetDetailsEx(pidl, pscid, pv);
    }

    STDMETHOD(GetDetailsOf)(PCUITEMID_CHILD pidl, UINT iColumn, SHELLDETAILS *psd) override
    {
        HRESULT hr = _EnsureTarget();
        return FAILED(hr) ? hr : m_pTarget->GetDetailsOf(pidl, iColumn, psd);
    }

    STDMETHOD(MapColumnToSCID)(UINT iColumn, SHCOLUMNID *pscid) override
    {
        HRESULT hr = _EnsureTarget();
        return FAILED(hr) ? hr : m_pTarget->MapColumnToSCID(iColumn, pscid);
    }

    // IPersist
    STDMETHOD(GetClassID)(CLSID *pClassID) override
    {
        if (!pClassID)
            return E_POINTER;
        *pClassID = CLSID_FolderShortcut;
        return S_OK;
    }

    // IPersistFolder
    STDMETHOD(Initialize)(PCIDLIST_ABSOLUTE pidl) override
    {
        WCHAR szPath[MAX_PATH];

        if (!pidl)
            return E_INVALIDARG;
        m_pidlRoot.Free();
        m_pidlRoot.Attach(ILClone(pidl));
        if (!m_pidlRoot)
            return E_OUTOFMEMORY;
        if ((_ILGetFSType(ILFindLastID(pidl)) & PT_FS) && SHGetPathFromIDListW(pidl, szPath))
        {
            WCHAR szLink[MAX_PATH];
            StringCchCopyW(m_szFolderPath, _countof(m_szFolderPath), szPath);
            if (PathCombineW(szLink, szPath, FOLDERSHORTCUT_LINK) && PathFileExistsW(szLink))
            {
                CComPtr<IPersistFile> ppf;
                HRESULT hr = _GetLinkPersistFile(&ppf);
                if (SUCCEEDED(hr))
                    hr = ppf->Load(szLink, STGM_READ);
                if (FAILED(hr))
                    return hr;
                m_pidlTarget.Free();
                m_pTarget.Release();
            }
        }
        return S_OK;
    }

    // IPersistFolder2
    STDMETHOD(GetCurFolder)(PIDLIST_ABSOLUTE *ppidl) override
    {
        if (!ppidl)
            return E_POINTER;
        *ppidl = NULL;
        if (!m_pidlRoot)
            return S_FALSE;
        *ppidl = ILClone(m_pidlRoot);
        return *ppidl ? S_OK : E_OUTOFMEMORY;
    }

    // IPersistFolder3
    STDMETHOD(InitializeEx)(IBindCtx *pbc, PCIDLIST_ABSOLUTE pidlRoot, const PERSIST_FOLDER_TARGET_INFO *ppfti) override
    {
        HRESULT hr;

        if (!pidlRoot)
            return E_INVALIDARG;
        m_pidlRoot.Free();
        m_pidlRoot.Attach(ILClone(pidlRoot));
        if (!m_pidlRoot)
            return E_OUTOFMEMORY;
        if (!ppfti)
            return S_OK;
        if (ppfti->pidlTargetFolder)
            return _SetTargetIDList(ppfti->pidlTargetFolder);
        if (ppfti->szTargetParsingName[0])
        {
            CComHeapPtr<ITEMIDLIST> pidl;
            hr = SHParseDisplayName(ppfti->szTargetParsingName, NULL, &pidl, 0, NULL);
            return FAILED(hr) ? hr : _SetTargetIDList(pidl);
        }
        if (ppfti->csidl != -1)
        {
            CComHeapPtr<ITEMIDLIST> pidl;
            hr = SHGetSpecialFolderLocation(NULL, ppfti->csidl & ~CSIDL_FLAG_MASK, &pidl);
            return FAILED(hr) ? hr : _SetTargetIDList(pidl);
        }
        return S_OK;
    }

    STDMETHOD(GetFolderTargetInfo)(PERSIST_FOLDER_TARGET_INFO *ppfti) override
    {
        CComHeapPtr<ITEMIDLIST> pidl;
        HRESULT hr;

        if (!ppfti)
            return E_POINTER;
        ZeroMemory(ppfti, sizeof(*ppfti));
        ppfti->csidl = -1;
        hr = _GetTargetIDList(&pidl);
        if (FAILED(hr))
            return hr;
        ppfti->pidlTargetFolder = pidl.Detach();
        if (ppfti->pidlTargetFolder)
            SHGetPathFromIDListW(ppfti->pidlTargetFolder, ppfti->szTargetParsingName);
        ppfti->dwAttributes = FILE_ATTRIBUTE_DIRECTORY;
        return S_OK;
    }

    // IShellLinkA
    STDMETHOD(GetPath)(LPSTR pszFile, INT cchMaxPath, WIN32_FIND_DATAA *pfd, DWORD fFlags) override
    {
        CComPtr<IShellLinkA> psl;
        HRESULT hr = m_pLink->QueryInterface(IID_PPV_ARG(IShellLinkA, &psl));
        return FAILED(hr) ? hr : psl->GetPath(pszFile, cchMaxPath, pfd, fFlags);
    }

    STDMETHOD(GetDescription)(LPSTR pszName, INT cchMaxName) override
    {
        CComPtr<IShellLinkA> psl;
        HRESULT hr = m_pLink->QueryInterface(IID_PPV_ARG(IShellLinkA, &psl));
        return FAILED(hr) ? hr : psl->GetDescription(pszName, cchMaxName);
    }

    STDMETHOD(SetDescription)(LPCSTR pszName) override
    {
        CComPtr<IShellLinkA> psl;
        HRESULT hr = m_pLink->QueryInterface(IID_PPV_ARG(IShellLinkA, &psl));
        return FAILED(hr) ? hr : psl->SetDescription(pszName);
    }

    STDMETHOD(GetWorkingDirectory)(LPSTR pszDir, INT cchMaxPath) override
    {
        CComPtr<IShellLinkA> psl;
        HRESULT hr = m_pLink->QueryInterface(IID_PPV_ARG(IShellLinkA, &psl));
        return FAILED(hr) ? hr : psl->GetWorkingDirectory(pszDir, cchMaxPath);
    }

    STDMETHOD(SetWorkingDirectory)(LPCSTR pszDir) override
    {
        CComPtr<IShellLinkA> psl;
        HRESULT hr = m_pLink->QueryInterface(IID_PPV_ARG(IShellLinkA, &psl));
        return FAILED(hr) ? hr : psl->SetWorkingDirectory(pszDir);
    }

    STDMETHOD(GetArguments)(LPSTR pszArgs, INT cchMaxPath) override
    {
        CComPtr<IShellLinkA> psl;
        HRESULT hr = m_pLink->QueryInterface(IID_PPV_ARG(IShellLinkA, &psl));
        return FAILED(hr) ? hr : psl->GetArguments(pszArgs, cchMaxPath);
    }

    STDMETHOD(SetArguments)(LPCSTR pszArgs) override
    {
        CComPtr<IShellLinkA> psl;
        HRESULT hr = m_pLink->QueryInterface(IID_PPV_ARG(IShellLinkA, &psl));
        return FAILED(hr) ? hr : psl->SetArguments(pszArgs);
    }

    STDMETHOD(GetIconLocation)(LPSTR pszIconPath, INT cchIconPath, INT *piIcon) override
    {
        CComPtr<IShellLinkA> psl;
        HRESULT hr = m_pLink->QueryInterface(IID_PPV_ARG(IShellLinkA, &psl));
        return FAILED(hr) ? hr : psl->GetIconLocation(pszIconPath, cchIconPath, piIcon);
    }

    STDMETHOD(SetIconLocation)(LPCSTR pszIconPath, INT iIcon) override
    {
        CComPtr<IShellLinkA> psl;
        HRESULT hr = m_pLink->QueryInterface(IID_PPV_ARG(IShellLinkA, &psl));
        return FAILED(hr) ? hr : psl->SetIconLocation(pszIconPath, iIcon);
    }

    STDMETHOD(SetRelativePath)(LPCSTR pszPathRel, DWORD dwReserved) override
    {
        CComPtr<IShellLinkA> psl;
        HRESULT hr = m_pLink->QueryInterface(IID_PPV_ARG(IShellLinkA, &psl));
        return FAILED(hr) ? hr : psl->SetRelativePath(pszPathRel, dwReserved);
    }

    STDMETHOD(SetPath)(LPCSTR pszFile) override
    {
        CComPtr<IShellLinkA> psl;
        HRESULT hr = m_pLink->QueryInterface(IID_PPV_ARG(IShellLinkA, &psl));
        if (SUCCEEDED(hr))
            hr = psl->SetPath(pszFile);
        if (SUCCEEDED(hr))
        {
            m_pidlTarget.Free();
            m_pTarget.Release();
        }
        return hr;
    }

    // IShellLinkA and IShellLinkW
    STDMETHOD(GetIDList)(PIDLIST_ABSOLUTE *ppidl) override
    {
        if (!ppidl)
            return E_POINTER;
        return _GetTargetIDList(ppidl);
    }

    STDMETHOD(SetIDList)(PCIDLIST_ABSOLUTE pidl) override
    {
        return _SetTargetIDList(pidl);
    }

    STDMETHOD(GetHotkey)(WORD *pwHotkey) override
    {
        return m_pLink->GetHotkey(pwHotkey);
    }

    STDMETHOD(SetHotkey)(WORD wHotkey) override
    {
        return m_pLink->SetHotkey(wHotkey);
    }

    STDMETHOD(GetShowCmd)(INT *piShowCmd) override
    {
        return m_pLink->GetShowCmd(piShowCmd);
    }

    STDMETHOD(SetShowCmd)(INT iShowCmd) override
    {
        return m_pLink->SetShowCmd(iShowCmd);
    }

    STDMETHOD(Resolve)(HWND hwnd, DWORD fFlags) override
    {
        HRESULT hr = m_pLink->Resolve(hwnd, fFlags);
        if (SUCCEEDED(hr))
        {
            m_pidlTarget.Free();
            m_pTarget.Release();
        }
        return hr;
    }

    // IShellLinkW
    STDMETHOD(GetPath)(LPWSTR pszFile, INT cchMaxPath, WIN32_FIND_DATAW *pfd, DWORD fFlags) override
    {
        return m_pLink->GetPath(pszFile, cchMaxPath, pfd, fFlags);
    }

    STDMETHOD(GetDescription)(LPWSTR pszName, INT cchMaxName) override
    {
        return m_pLink->GetDescription(pszName, cchMaxName);
    }

    STDMETHOD(SetDescription)(LPCWSTR pszName) override
    {
        return m_pLink->SetDescription(pszName);
    }

    STDMETHOD(GetWorkingDirectory)(LPWSTR pszDir, INT cchMaxPath) override
    {
        return m_pLink->GetWorkingDirectory(pszDir, cchMaxPath);
    }

    STDMETHOD(SetWorkingDirectory)(LPCWSTR pszDir) override
    {
        return m_pLink->SetWorkingDirectory(pszDir);
    }

    STDMETHOD(GetArguments)(LPWSTR pszArgs, INT cchMaxPath) override
    {
        return m_pLink->GetArguments(pszArgs, cchMaxPath);
    }

    STDMETHOD(SetArguments)(LPCWSTR pszArgs) override
    {
        return m_pLink->SetArguments(pszArgs);
    }

    STDMETHOD(GetIconLocation)(LPWSTR pszIconPath, INT cchIconPath, INT *piIcon) override
    {
        return m_pLink->GetIconLocation(pszIconPath, cchIconPath, piIcon);
    }

    STDMETHOD(SetIconLocation)(LPCWSTR pszIconPath, INT iIcon) override
    {
        return m_pLink->SetIconLocation(pszIconPath, iIcon);
    }

    STDMETHOD(SetRelativePath)(LPCWSTR pszPathRel, DWORD dwReserved) override
    {
        return m_pLink->SetRelativePath(pszPathRel, dwReserved);
    }

    STDMETHOD(SetPath)(LPCWSTR pszFile) override
    {
        HRESULT hr = m_pLink->SetPath(pszFile);
        if (SUCCEEDED(hr))
        {
            m_pidlTarget.Free();
            m_pTarget.Release();
        }
        return hr;
    }

    // IPersistFile
    STDMETHOD(IsDirty)() override
    {
        CComPtr<IPersistFile> ppf;
        HRESULT hr = _GetLinkPersistFile(&ppf);
        return FAILED(hr) ? hr : ppf->IsDirty();
    }

    STDMETHOD(Load)(LPCOLESTR pszFileName, DWORD dwMode) override
    {
        CComPtr<IPersistFile> ppf;
        WCHAR szLink[MAX_PATH];
        HRESULT hr;

        if (!pszFileName)
            return E_INVALIDARG;
        if (!PathCombineW(szLink, pszFileName, FOLDERSHORTCUT_LINK))
            return E_INVALIDARG;
        hr = _GetLinkPersistFile(&ppf);
        if (SUCCEEDED(hr))
            hr = ppf->Load(szLink, dwMode);
        if (FAILED(hr))
            return hr;
        StringCchCopyW(m_szFolderPath, _countof(m_szFolderPath), pszFileName);
        m_pidlTarget.Free();
        m_pTarget.Release();
        return S_OK;
    }

    STDMETHOD(Save)(LPCOLESTR pszFileName, BOOL fRemember) override
    {
        CComPtr<IPersistFile> ppf;
        WCHAR szFolder[MAX_PATH], szLink[MAX_PATH], szIni[MAX_PATH], szClsid[40];
        HRESULT hr;

        if (pszFileName)
            StringCchCopyW(szFolder, _countof(szFolder), pszFileName);
        else if (m_szFolderPath[0])
            StringCchCopyW(szFolder, _countof(szFolder), m_szFolderPath);
        else
            return E_INVALIDARG;

        if (!CreateDirectoryW(szFolder, NULL) && GetLastError() != ERROR_ALREADY_EXISTS)
            return HRESULT_FROM_WIN32(GetLastError());
        if (!PathCombineW(szLink, szFolder, FOLDERSHORTCUT_LINK) || !PathCombineW(szIni, szFolder, FOLDERSHORTCUT_INI))
            return E_INVALIDARG;

        hr = _GetLinkPersistFile(&ppf);
        if (SUCCEEDED(hr))
            hr = ppf->Save(szLink, TRUE);
        if (FAILED(hr))
            return hr;

        StringFromGUID2(CLSID_FolderShortcut, szClsid, _countof(szClsid));
        if (!WritePrivateProfileStringW(L".ShellClassInfo", L"CLSID2", szClsid, szIni) ||
            !WritePrivateProfileStringW(L".ShellClassInfo", L"Flags", L"2", szIni))
        {
            return HRESULT_FROM_WIN32(GetLastError());
        }
        SetFileAttributesW(szIni, FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM);
        SetFileAttributesW(szFolder, GetFileAttributesW(szFolder) | FILE_ATTRIBUTE_READONLY);

        if (fRemember)
            StringCchCopyW(m_szFolderPath, _countof(m_szFolderPath), szFolder);
        SHChangeNotify(SHCNE_UPDATEDIR, SHCNF_PATHW, szFolder, NULL);
        return S_OK;
    }

    STDMETHOD(SaveCompleted)(LPCOLESTR pszFileName) override
    {
        return S_OK;
    }

    STDMETHOD(GetCurFile)(LPOLESTR *ppszFileName) override
    {
        if (!ppszFileName)
            return E_POINTER;
        *ppszFileName = NULL;
        if (!m_szFolderPath[0])
            return S_FALSE;
        return SHStrDupW(m_szFolderPath, ppszFileName);
    }

    // IExtractIconW
    STDMETHOD(GetIconLocation)(UINT uFlags, LPWSTR pszIconFile, UINT cchMax, INT *piIndex, UINT *pwFlags) override
    {
        CComPtr<IShellFolder> psfParent;
        CComPtr<IExtractIconW> pei;
        CComHeapPtr<ITEMIDLIST> pidl;
        PCUITEMID_CHILD pidlChild;
        HRESULT hr;

        hr = _GetTargetIDList(&pidl);
        if (FAILED(hr))
            return hr;
        if (!pidl)
            return E_FAIL;
        hr = SHBindToParent(pidl, IID_PPV_ARG(IShellFolder, &psfParent), &pidlChild);
        if (FAILED(hr))
            return hr;
        hr = psfParent->GetUIObjectOf(NULL, 1, &pidlChild, IID_NULL_PPV_ARG(IExtractIconW, &pei));
        if (FAILED(hr))
            return hr;
        return pei->GetIconLocation(uFlags | GIL_FORSHORTCUT, pszIconFile, cchMax, piIndex, pwFlags);
    }

    STDMETHOD(Extract)(LPCWSTR pszFile, UINT nIconIndex, HICON *phiconLarge, HICON *phiconSmall, UINT nIconSize) override
    {
        return SHDefExtractIconW(pszFile, nIconIndex, 0, phiconLarge, phiconSmall, nIconSize);
    }

    // IQueryInfo
    STDMETHOD(GetInfoTip)(DWORD dwFlags, WCHAR **ppwszTip) override
    {
        WCHAR szText[INFOTIPSIZE];
        CComHeapPtr<ITEMIDLIST> pidl;

        if (!ppwszTip)
            return E_POINTER;
        *ppwszTip = NULL;
        if (SUCCEEDED(m_pLink->GetDescription(szText, _countof(szText))) && szText[0])
            return SHStrDupW(szText, ppwszTip);
        if (SUCCEEDED(_GetTargetIDList(&pidl)) && pidl &&
            SUCCEEDED(SHGetNameFromIDList(pidl, SIGDN_DESKTOPABSOLUTEPARSING, ppwszTip)))
        {
            return S_OK;
        }
        return SHStrDupW(L"", ppwszTip);
    }

    STDMETHOD(GetInfoFlags)(DWORD *pdwFlags) override
    {
        if (!pdwFlags)
            return E_POINTER;
        *pdwFlags = 0;
        return S_OK;
    }

    // IPersistStreamInit
    STDMETHOD(Load)(LPSTREAM pStm) override
    {
        CComPtr<IPersistStream> pps;
        HRESULT hr = m_pLink->QueryInterface(IID_PPV_ARG(IPersistStream, &pps));
        if (SUCCEEDED(hr))
            hr = pps->Load(pStm);
        if (SUCCEEDED(hr))
        {
            m_pidlTarget.Free();
            m_pTarget.Release();
        }
        return hr;
    }

    STDMETHOD(Save)(LPSTREAM pStm, BOOL fClearDirty) override
    {
        CComPtr<IPersistStream> pps;
        HRESULT hr = m_pLink->QueryInterface(IID_PPV_ARG(IPersistStream, &pps));
        return FAILED(hr) ? hr : pps->Save(pStm, fClearDirty);
    }

    STDMETHOD(GetSizeMax)(ULARGE_INTEGER *pcbSize) override
    {
        CComPtr<IPersistStream> pps;
        HRESULT hr = m_pLink->QueryInterface(IID_PPV_ARG(IPersistStream, &pps));
        return FAILED(hr) ? hr : pps->GetSizeMax(pcbSize);
    }

    STDMETHOD(InitNew)() override
    {
        CComPtr<IShellLinkW> pLink;
        HRESULT hr = SHCoCreateInstance(NULL, &CLSID_ShellLink, NULL, IID_PPV_ARG(IShellLinkW, &pLink));
        if (FAILED(hr))
            return hr;
        m_pLink = pLink;
        m_pidlTarget.Free();
        m_pTarget.Release();
        return S_OK;
    }

    // IPersistPropertyBag
    STDMETHOD(Load)(IPropertyBag *pPropBag, IErrorLog *pErrorLog) override
    {
        WCHAR szValue[MAX_PATH], szPath[MAX_PATH];
        CComHeapPtr<ITEMIDLIST> pidl;
        DWORD dwCsidl;
        HRESULT hr;

        if (!pPropBag)
            return E_INVALIDARG;

        if (SUCCEEDED(SHPropertyBag_ReadStr(pPropBag, L"Target", szValue, _countof(szValue))) && szValue[0])
        {
            if (!ExpandEnvironmentStringsW(szValue, szPath, _countof(szPath)))
                return HRESULT_FROM_WIN32(GetLastError());
            hr = SHParseDisplayName(szPath, NULL, &pidl, 0, NULL);
        }
        else if (SUCCEEDED(SHPropertyBag_ReadStr(pPropBag, L"TargetKnownFolder", szValue, _countof(szValue))) && szValue[0])
        {
            KNOWNFOLDERID kfid;
            hr = CLSIDFromString(szValue, &kfid);
            if (SUCCEEDED(hr))
                hr = SHGetKnownFolderIDList(kfid, KF_FLAG_DEFAULT, NULL, &pidl);
        }
        else if (SUCCEEDED(SHPropertyBag_ReadDWORD(pPropBag, L"TargetSpecialFolder", &dwCsidl)))
        {
            hr = SHGetSpecialFolderLocation(NULL, (int)dwCsidl, &pidl);
        }
        else
        {
            return E_INVALIDARG;
        }
        if (FAILED(hr))
            return hr;
        return _SetTargetIDList(pidl);
    }

    STDMETHOD(Save)(IPropertyBag *pPropBag, BOOL fClearDirty, BOOL fSaveAllProperties) override
    {
        WCHAR szPath[MAX_PATH];
        CComHeapPtr<ITEMIDLIST> pidl;
        HRESULT hr;

        if (!pPropBag)
            return E_INVALIDARG;
        hr = _GetTargetIDList(&pidl);
        if (FAILED(hr))
            return hr;
        if (!pidl || !SHGetPathFromIDListW(pidl, szPath))
            return E_FAIL;
        return SHPropertyBag_WriteStr(pPropBag, L"Target", szPath);
    }

    // IBrowserFrameOptions
    STDMETHOD(GetFrameOptions)(BROWSERFRAMEOPTIONS dwMask, BROWSERFRAMEOPTIONS *pdwOptions) override
    {
        CComPtr<IBrowserFrameOptions> pbfo;

        if (!pdwOptions)
            return E_POINTER;
        *pdwOptions = BFO_NONE;
        if (SUCCEEDED(_EnsureTarget()) && SUCCEEDED(m_pTarget->QueryInterface(IID_PPV_ARG(IBrowserFrameOptions, &pbfo))))
            return pbfo->GetFrameOptions(dwMask, pdwOptions);
        return S_OK;
    }

    // IShellIconOverlay
    STDMETHOD(GetOverlayIndex)(PCUITEMID_CHILD pidl, int *pIndex) override
    {
        CComPtr<IShellIconOverlay> psio;
        HRESULT hr = _EnsureTarget();
        if (SUCCEEDED(hr))
            hr = m_pTarget->QueryInterface(IID_PPV_ARG(IShellIconOverlay, &psio));
        return FAILED(hr) ? hr : psio->GetOverlayIndex(pidl, pIndex);
    }

    STDMETHOD(GetOverlayIconIndex)(PCUITEMID_CHILD pidl, int *pIconIndex) override
    {
        CComPtr<IShellIconOverlay> psio;
        HRESULT hr = _EnsureTarget();
        if (SUCCEEDED(hr))
            hr = m_pTarget->QueryInterface(IID_PPV_ARG(IShellIconOverlay, &psio));
        return FAILED(hr) ? hr : psio->GetOverlayIconIndex(pidl, pIconIndex);
    }

    DECLARE_REGISTRY_RESOURCEID(IDR_FOLDERSHORTCUT)
    DECLARE_NOT_AGGREGATABLE(CFolderShortcut)
    DECLARE_PROTECT_FINAL_CONSTRUCT()

    BEGIN_COM_MAP(CFolderShortcut)
        COM_INTERFACE_ENTRY_IID(IID_IShellFolder2, IShellFolder2)
        COM_INTERFACE_ENTRY_IID(IID_IShellFolder, IShellFolder)
        COM_INTERFACE_ENTRY_IID(IID_IPersistFolder3, IPersistFolder3)
        COM_INTERFACE_ENTRY_IID(IID_IPersistFolder2, IPersistFolder2)
        COM_INTERFACE_ENTRY_IID(IID_IPersistFolder, IPersistFolder)
        COM_INTERFACE_ENTRY2_IID(IID_IPersist, IPersist, IPersistFolder3)
        COM_INTERFACE_ENTRY_IID(IID_IShellLinkA, IShellLinkA)
        COM_INTERFACE_ENTRY_IID(IID_IShellLinkW, IShellLinkW)
        COM_INTERFACE_ENTRY_IID(IID_IPersistFile, IPersistFile)
        COM_INTERFACE_ENTRY_IID(IID_IExtractIconW, IExtractIconW)
        COM_INTERFACE_ENTRY_IID(IID_IQueryInfo, IQueryInfo)
        COM_INTERFACE_ENTRY_IID(IID_IPersistStream, IPersistStreamInit)
        COM_INTERFACE_ENTRY_IID(IID_IPersistStreamInit, IPersistStreamInit)
        COM_INTERFACE_ENTRY_IID(IID_IPersistPropertyBag, IPersistPropertyBag)
        COM_INTERFACE_ENTRY_IID(IID_IBrowserFrameOptions, IBrowserFrameOptions)
        COM_INTERFACE_ENTRY_IID(IID_IShellIconOverlay, IShellIconOverlay)
    END_COM_MAP()
};

OBJECT_ENTRY_AUTO(CLSID_FolderShortcut, CFolderShortcut)
