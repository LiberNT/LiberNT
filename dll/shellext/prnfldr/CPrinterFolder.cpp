/*
 * PROJECT:     LiberNT Printers Folder
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     Printers shell folder
 * COPYRIGHT:   Copyright 1997 Marcus Meissner
 *              Copyright 1998, 1999, 2002 Juergen Schmied
 *              Copyright 2005 Huw Davies
 *              Copyright 2009 Andrew Hill
 *              Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"

#define SHELL32_IDI_SHELL_PRINTER 17
#define SHELL32_IDI_SHELL_SHARE   29

#pragma pack(push, 1)
typedef struct _PRINTER_ITEM
{
    WORD cb;
    BYTE type;
    BYTE dummy;
    DWORD Attributes;
    WORD offsServer;
    WCHAR szName[1];
} PRINTER_ITEM, *PPRINTER_ITEM;
#pragma pack(pop)

#define PRINTER_ITEM_TYPE   0x00
#define PRINTER_ITEM_MARKER 0xFF

typedef struct _PRINTER_COLUMN
{
    UINT idsName;
    SHCOLSTATEF csFlags;
    int fmt;
    int cxChar;
} PRINTER_COLUMN;

static const PRINTER_COLUMN PrinterColumns[] =
{
    { IDS_COLUMN_NAME,      SHCOLSTATE_TYPE_STR | SHCOLSTATE_ONBYDEFAULT, LVCFMT_LEFT, 15 },
    { IDS_COLUMN_DOCUMENTS, SHCOLSTATE_TYPE_INT | SHCOLSTATE_ONBYDEFAULT, LVCFMT_LEFT, 15 },
    { IDS_COLUMN_STATUS,    SHCOLSTATE_TYPE_STR | SHCOLSTATE_ONBYDEFAULT, LVCFMT_LEFT, 15 },
    { IDS_COLUMN_COMMENTS,  SHCOLSTATE_TYPE_STR | SHCOLSTATE_ONBYDEFAULT, LVCFMT_LEFT, 15 },
    { IDS_COLUMN_LOCATION,  SHCOLSTATE_TYPE_STR | SHCOLSTATE_ONBYDEFAULT, LVCFMT_LEFT, 15 },
    { IDS_COLUMN_MODEL,     SHCOLSTATE_TYPE_STR | SHCOLSTATE_ONBYDEFAULT, LVCFMT_LEFT, 15 },
};

#define COLUMN_NAME 0

static const PRINTER_ITEM *GetPrinterItem(PCUITEMID_CHILD pidl)
{
    const PRINTER_ITEM *pItem = (const PRINTER_ITEM *)pidl;

    if (!pidl || pItem->cb < FIELD_OFFSET(PRINTER_ITEM, szName) + 2 * sizeof(WCHAR) ||
        pItem->type != PRINTER_ITEM_TYPE || pItem->dummy != PRINTER_ITEM_MARKER)
    {
        return NULL;
    }
    return pItem;
}

static PITEMID_CHILD CreatePrinterItem(LPCWSTR pszPrinter, LPCWSTR pszServer, DWORD Attributes)
{
    SIZE_T cchPrinter = pszPrinter ? wcslen(pszPrinter) : 0;
    SIZE_T cchServer = pszServer ? wcslen(pszServer) : 0;
    SIZE_T cbItem;
    PRINTER_ITEM *pItem;

    if (cchPrinter + cchServer > MAXWORD - 2)
        return NULL;

    cbItem = FIELD_OFFSET(PRINTER_ITEM, szName) + (cchPrinter + 1 + cchServer + 1) * sizeof(WCHAR);
    if (cbItem > MAXWORD - sizeof(WORD))
        return NULL;

    pItem = (PRINTER_ITEM *)SHAlloc(cbItem + sizeof(WORD));
    if (!pItem)
        return NULL;

    pItem->cb = (WORD)cbItem;
    pItem->type = PRINTER_ITEM_TYPE;
    pItem->dummy = PRINTER_ITEM_MARKER;
    pItem->Attributes = Attributes;
    pItem->offsServer = (WORD)(cchPrinter + 1);
    CopyMemory(pItem->szName, pszPrinter ? pszPrinter : L"", (cchPrinter + 1) * sizeof(WCHAR));
    CopyMemory(pItem->szName + pItem->offsServer, pszServer ? pszServer : L"", (cchServer + 1) * sizeof(WCHAR));
    *(WORD *)((BYTE *)pItem + cbItem) = 0;
    return (PITEMID_CHILD)pItem;
}

static HRESULT SetStrRet(LPSTRRET pStrRet, LPCWSTR pszText)
{
    pStrRet->uType = STRRET_WSTR;
    return SHStrDupW(pszText, &pStrRet->pOleStr);
}

class CPrintersEnum :
    public CComObjectRootEx<CComMultiThreadModelNoCS>,
    public IEnumIDList
{
    CSimpleArray<PITEMID_CHILD> m_Items;
    int m_iPos;

public:
    CPrintersEnum() : m_iPos(0) {}

    ~CPrintersEnum()
    {
        for (int i = 0; i < m_Items.GetSize(); i++)
            SHFree(m_Items[i]);
    }

    HRESULT AddItem(PCUITEMID_CHILD pidl)
    {
        PITEMID_CHILD pidlCopy = ILClone(pidl);

        if (!pidlCopy)
            return E_OUTOFMEMORY;
        if (!m_Items.Add(pidlCopy))
        {
            SHFree(pidlCopy);
            return E_OUTOFMEMORY;
        }
        return S_OK;
    }

    HRESULT Enumerate(LPCWSTR pszServer, DWORD dwFlags)
    {
        DWORD cbNeeded = 0, cReturned = 0;
        BYTE *pBuffer;
        DWORD dwLevel, dwEnumFlags;

        if (!(dwFlags & SHCONTF_NONFOLDERS))
            return S_OK;

        dwLevel = (pszServer && *pszServer) ? 2 : 4;
        dwEnumFlags = (pszServer && *pszServer) ? PRINTER_ENUM_NAME : PRINTER_ENUM_LOCAL;

        EnumPrintersW(dwEnumFlags, (LPWSTR)pszServer, dwLevel, NULL, 0, &cbNeeded, &cReturned);
        if (!cbNeeded)
            return S_OK;

        pBuffer = (BYTE *)HeapAlloc(GetProcessHeap(), 0, cbNeeded);
        if (!pBuffer)
            return E_OUTOFMEMORY;
        if (!EnumPrintersW(dwEnumFlags, (LPWSTR)pszServer, dwLevel, pBuffer, cbNeeded, &cbNeeded, &cReturned))
        {
            HeapFree(GetProcessHeap(), 0, pBuffer);
            return HRESULT_FROM_WIN32(GetLastError());
        }

        for (DWORD i = 0; i < cReturned; i++)
        {
            PITEMID_CHILD pidl;

            if (dwLevel == 4)
            {
                PRINTER_INFO_4W *pInfo = (PRINTER_INFO_4W *)pBuffer + i;
                pidl = CreatePrinterItem(pInfo->pPrinterName, pInfo->pServerName, pInfo->Attributes);
            }
            else
            {
                PRINTER_INFO_2W *pInfo = (PRINTER_INFO_2W *)pBuffer + i;
                pidl = CreatePrinterItem(pInfo->pPrinterName, pInfo->pServerName, pInfo->Attributes);
            }

            if (pidl && !m_Items.Add(pidl))
                SHFree(pidl);
        }

        HeapFree(GetProcessHeap(), 0, pBuffer);
        return S_OK;
    }

    STDMETHOD(Next)(ULONG celt, PITEMID_CHILD *rgelt, ULONG *pceltFetched) override
    {
        ULONG cFetched = 0;

        if (!rgelt || (celt > 1 && !pceltFetched))
            return E_INVALIDARG;

        while (cFetched < celt && m_iPos < m_Items.GetSize())
        {
            rgelt[cFetched] = ILClone(m_Items[m_iPos]);
            if (!rgelt[cFetched])
                break;
            cFetched++;
            m_iPos++;
        }
        if (pceltFetched)
            *pceltFetched = cFetched;
        return cFetched == celt ? S_OK : S_FALSE;
    }

    STDMETHOD(Skip)(ULONG celt) override
    {
        m_iPos += celt;
        if (m_iPos > m_Items.GetSize())
        {
            m_iPos = m_Items.GetSize();
            return S_FALSE;
        }
        return S_OK;
    }

    STDMETHOD(Reset)() override
    {
        m_iPos = 0;
        return S_OK;
    }

    STDMETHOD(Clone)(IEnumIDList **ppenum) override
    {
        CComObject<CPrintersEnum> *pEnum;
        HRESULT hr;

        if (!ppenum)
            return E_POINTER;
        *ppenum = NULL;
        hr = CComObject<CPrintersEnum>::CreateInstance(&pEnum);
        if (FAILED(hr))
            return hr;
        pEnum->AddRef();
        for (int i = 0; i < m_Items.GetSize() && SUCCEEDED(hr); i++)
            hr = pEnum->AddItem(m_Items[i]);
        pEnum->m_iPos = m_iPos;
        if (SUCCEEDED(hr))
            hr = pEnum->QueryInterface(IID_PPV_ARG(IEnumIDList, ppenum));
        pEnum->Release();
        return hr;
    }

    BEGIN_COM_MAP(CPrintersEnum)
        COM_INTERFACE_ENTRY_IID(IID_IEnumIDList, IEnumIDList)
    END_COM_MAP()
};

class CPrinterFolder :
    public CComCoClass<CPrinterFolder, &CLSID_Printers>,
    public CComObjectRootEx<CComMultiThreadModelNoCS>,
    public IShellFolder2,
    public IPersistFolder2,
    public IShellIconOverlay,
    public IRemoteComputer,
    public IContextMenuCB,
    public IResolveShellLink,
    public IFolderType,
    public IObjectWithBackReferences
{
    CComHeapPtr<ITEMIDLIST_ABSOLUTE> m_pidlRoot;
    CComHeapPtr<WCHAR> m_pszServer;

    HRESULT AssocCreate(REFIID riid, void **ppv);
    HRESULT CreateExtractIcon(REFIID riid, void **ppv);

public:
    STDMETHOD(ParseDisplayName)(HWND hwndOwner, LPBC pbc, LPOLESTR lpszDisplayName, DWORD *pchEaten,
                                PIDLIST_RELATIVE *ppidl, DWORD *pdwAttributes) override;
    STDMETHOD(EnumObjects)(HWND hwndOwner, DWORD dwFlags, LPENUMIDLIST *ppEnumIDList) override;
    STDMETHOD(BindToObject)(PCUIDLIST_RELATIVE pidl, LPBC pbcReserved, REFIID riid, LPVOID *ppvOut) override;
    STDMETHOD(BindToStorage)(PCUIDLIST_RELATIVE pidl, LPBC pbcReserved, REFIID riid, LPVOID *ppvOut) override;
    STDMETHOD(CompareIDs)(LPARAM lParam, PCUIDLIST_RELATIVE pidl1, PCUIDLIST_RELATIVE pidl2) override;
    STDMETHOD(CreateViewObject)(HWND hwndOwner, REFIID riid, LPVOID *ppvOut) override;
    STDMETHOD(GetAttributesOf)(UINT cidl, PCUITEMID_CHILD_ARRAY apidl, DWORD *rgfInOut) override;
    STDMETHOD(GetUIObjectOf)(HWND hwndOwner, UINT cidl, PCUITEMID_CHILD_ARRAY apidl, REFIID riid, UINT *prgfInOut,
                             LPVOID *ppvOut) override;
    STDMETHOD(GetDisplayNameOf)(PCUITEMID_CHILD pidl, DWORD dwFlags, LPSTRRET strRet) override;
    STDMETHOD(SetNameOf)(HWND hwndOwner, PCUITEMID_CHILD pidl, LPCOLESTR lpName, DWORD dwFlags,
                         PITEMID_CHILD *pPidlOut) override;

    STDMETHOD(GetDefaultSearchGUID)(GUID *pguid) override;
    STDMETHOD(EnumSearches)(IEnumExtraSearch **ppenum) override;
    STDMETHOD(GetDefaultColumn)(DWORD dwRes, ULONG *pSort, ULONG *pDisplay) override;
    STDMETHOD(GetDefaultColumnState)(UINT iColumn, SHCOLSTATEF *pcsFlags) override;
    STDMETHOD(GetDetailsEx)(PCUITEMID_CHILD pidl, const SHCOLUMNID *pscid, VARIANT *pv) override;
    STDMETHOD(GetDetailsOf)(PCUITEMID_CHILD pidl, UINT iColumn, SHELLDETAILS *psd) override;
    STDMETHOD(MapColumnToSCID)(UINT column, SHCOLUMNID *pscid) override;

    STDMETHOD(GetClassID)(CLSID *lpClassId) override;
    STDMETHOD(Initialize)(PCIDLIST_ABSOLUTE pidl) override;
    STDMETHOD(GetCurFolder)(PIDLIST_ABSOLUTE *pidl) override;

    STDMETHOD(GetOverlayIndex)(PCUITEMID_CHILD pidl, int *pIndex) override;
    STDMETHOD(GetOverlayIconIndex)(PCUITEMID_CHILD pidl, int *pIconIndex) override;

    STDMETHOD(Initialize)(LPCWSTR pszMachine, BOOL bEnumerating) override;

    STDMETHOD(CallBack)(IShellFolder *psf, HWND hwndOwner, IDataObject *pdtobj, UINT uMsg, WPARAM wParam,
                        LPARAM lParam) override;

    STDMETHOD(ResolveShellLink)(IUnknown *punkLink, HWND hwnd, DWORD fFlags) override;

    STDMETHOD(GetFolderType)(FOLDERTYPEID *pftid) override;

    STDMETHOD(RemoveBackReferences)() override;

    DECLARE_REGISTRY_RESOURCEID(IDR_PRINTERS)
    DECLARE_NOT_AGGREGATABLE(CPrinterFolder)
    DECLARE_PROTECT_FINAL_CONSTRUCT()

    BEGIN_COM_MAP(CPrinterFolder)
        COM_INTERFACE_ENTRY_IID(IID_IShellFolder2, IShellFolder2)
        COM_INTERFACE_ENTRY_IID(IID_IShellFolder, IShellFolder)
        COM_INTERFACE_ENTRY_IID(IID_IPersistFolder2, IPersistFolder2)
        COM_INTERFACE_ENTRY_IID(IID_IPersistFolder, IPersistFolder)
        COM_INTERFACE_ENTRY_IID(IID_IShellIconOverlay, IShellIconOverlay)
        COM_INTERFACE_ENTRY_IID(IID_IRemoteComputer, IRemoteComputer)
        COM_INTERFACE_ENTRY_IID(IID_IContextMenuCB, IContextMenuCB)
        COM_INTERFACE_ENTRY_IID(IID_IResolveShellLink, IResolveShellLink)
        COM_INTERFACE_ENTRY_IID(IID_IFolderType, IFolderType)
        COM_INTERFACE_ENTRY_IID(IID_IObjectWithBackReferences, IObjectWithBackReferences)
    END_COM_MAP()
};

HRESULT CPrinterFolder::AssocCreate(REFIID riid, void **ppv)
{
    CComPtr<IQueryAssociations> pAssoc;
    HRESULT hr;

    hr = ::AssocCreate(CLSID_QueryAssociations, IID_PPV_ARG(IQueryAssociations, &pAssoc));
    if (FAILED(hr))
        return hr;
    hr = pAssoc->Init(ASSOCF_NONE, L"Printers", NULL, NULL);
    if (FAILED(hr))
        return hr;
    return pAssoc->QueryInterface(riid, ppv);
}

HRESULT CPrinterFolder::CreateExtractIcon(REFIID riid, void **ppv)
{
    CComPtr<IDefaultExtractIconInit> pInit;
    HRESULT hr;

    hr = SHCreateDefaultExtractIcon(IID_PPV_ARG(IDefaultExtractIconInit, &pInit));
    if (FAILED(hr))
        return hr;
    hr = pInit->SetNormalIcon(L"shell32.dll", -SHELL32_IDI_SHELL_PRINTER);
    if (FAILED(hr))
        return hr;
    return pInit->QueryInterface(riid, ppv);
}

STDMETHODIMP CPrinterFolder::ParseDisplayName(HWND hwndOwner, LPBC pbc, LPOLESTR lpszDisplayName, DWORD *pchEaten,
                                              PIDLIST_RELATIVE *ppidl, DWORD *pdwAttributes)
{
    if (ppidl)
        *ppidl = NULL;
    if (pchEaten)
        *pchEaten = 0;
    return E_NOTIMPL;
}

STDMETHODIMP CPrinterFolder::EnumObjects(HWND hwndOwner, DWORD dwFlags, LPENUMIDLIST *ppEnumIDList)
{
    CComObject<CPrintersEnum> *pEnum;
    HRESULT hr;

    if (!ppEnumIDList)
        return E_POINTER;
    *ppEnumIDList = NULL;

    hr = CComObject<CPrintersEnum>::CreateInstance(&pEnum);
    if (FAILED(hr))
        return hr;
    pEnum->AddRef();
    hr = pEnum->Enumerate(m_pszServer, dwFlags);
    if (SUCCEEDED(hr))
        hr = pEnum->QueryInterface(IID_PPV_ARG(IEnumIDList, ppEnumIDList));
    pEnum->Release();
    return hr;
}

STDMETHODIMP CPrinterFolder::BindToObject(PCUIDLIST_RELATIVE pidl, LPBC pbcReserved, REFIID riid, LPVOID *ppvOut)
{
    if (ppvOut)
        *ppvOut = NULL;
    return E_NOTIMPL;
}

STDMETHODIMP CPrinterFolder::BindToStorage(PCUIDLIST_RELATIVE pidl, LPBC pbcReserved, REFIID riid, LPVOID *ppvOut)
{
    if (ppvOut)
        *ppvOut = NULL;
    return E_NOTIMPL;
}

STDMETHODIMP CPrinterFolder::CompareIDs(LPARAM lParam, PCUIDLIST_RELATIVE pidl1, PCUIDLIST_RELATIVE pidl2)
{
    SHELLDETAILS sd1, sd2;
    WCHAR szText1[MAX_PATH], szText2[MAX_PATH];
    UINT iColumn = LOWORD(lParam);
    HRESULT hr;
    int iResult;

    hr = GetDetailsOf(pidl1, iColumn, &sd1);
    if (FAILED(hr))
        return hr;
    hr = StrRetToBufW(&sd1.str, pidl1, szText1, _countof(szText1));
    if (FAILED(hr))
        return hr;
    hr = GetDetailsOf(pidl2, iColumn, &sd2);
    if (FAILED(hr))
        return hr;
    hr = StrRetToBufW(&sd2.str, pidl2, szText2, _countof(szText2));
    if (FAILED(hr))
        return hr;

    iResult = lstrcmpiW(szText1, szText2);
    return MAKE_HRESULT(SEVERITY_SUCCESS, 0, (USHORT)(SHORT)(iResult < 0 ? -1 : iResult > 0 ? 1 : 0));
}

STDMETHODIMP CPrinterFolder::CreateViewObject(HWND hwndOwner, REFIID riid, LPVOID *ppvOut)
{
    if (!ppvOut)
        return E_INVALIDARG;
    *ppvOut = NULL;

    if (IsEqualIID(riid, IID_IShellView))
    {
        SFV_CREATE sfvparams = { sizeof(SFV_CREATE), this };
        return SHCreateShellFolderView(&sfvparams, (IShellView **)ppvOut);
    }
    if (IsEqualIID(riid, IID_IDropTarget) || IsEqualIID(riid, IID_IContextMenu))
        return E_NOTIMPL;
    return E_INVALIDARG;
}

STDMETHODIMP CPrinterFolder::GetAttributesOf(UINT cidl, PCUITEMID_CHILD_ARRAY apidl, DWORD *rgfInOut)
{
    static const DWORD dwPrintersAttributes =
        SFGAO_HASPROPSHEET | SFGAO_STORAGEANCESTOR | SFGAO_FILESYSANCESTOR | SFGAO_FOLDER | SFGAO_CANRENAME | SFGAO_CANDELETE;

    if (!rgfInOut)
        return E_INVALIDARG;
    *rgfInOut &= dwPrintersAttributes;
    *rgfInOut &= ~SFGAO_VALIDATE;
    return S_OK;
}

STDMETHODIMP CPrinterFolder::GetUIObjectOf(HWND hwndOwner, UINT cidl, PCUITEMID_CHILD_ARRAY apidl, REFIID riid,
                                           UINT *prgfInOut, LPVOID *ppvOut)
{
    if (!ppvOut)
        return E_INVALIDARG;
    *ppvOut = NULL;

    if ((IsEqualIID(riid, IID_IExtractIconA) || IsEqualIID(riid, IID_IExtractIconW)) && cidl == 1)
        return CreateExtractIcon(riid, ppvOut);
    if (IsEqualIID(riid, IID_IQueryAssociations))
        return AssocCreate(riid, ppvOut);
    return E_NOINTERFACE;
}

STDMETHODIMP CPrinterFolder::GetDisplayNameOf(PCUITEMID_CHILD pidl, DWORD dwFlags, LPSTRRET strRet)
{
    const PRINTER_ITEM *pItem;

    if (!strRet)
        return E_INVALIDARG;
    pItem = GetPrinterItem(pidl);
    if (!pItem)
        return E_INVALIDARG;
    return SetStrRet(strRet, pItem->szName);
}

STDMETHODIMP CPrinterFolder::SetNameOf(HWND hwndOwner, PCUITEMID_CHILD pidl, LPCOLESTR lpName, DWORD dwFlags,
                                       PITEMID_CHILD *pPidlOut)
{
    if (pPidlOut)
        *pPidlOut = NULL;
    return E_FAIL;
}

STDMETHODIMP CPrinterFolder::GetDefaultSearchGUID(GUID *pguid)
{
    return E_NOTIMPL;
}

STDMETHODIMP CPrinterFolder::EnumSearches(IEnumExtraSearch **ppenum)
{
    if (ppenum)
        *ppenum = NULL;
    return E_NOTIMPL;
}

STDMETHODIMP CPrinterFolder::GetDefaultColumn(DWORD dwRes, ULONG *pSort, ULONG *pDisplay)
{
    if (pSort)
        *pSort = 0;
    if (pDisplay)
        *pDisplay = 0;
    return S_OK;
}

STDMETHODIMP CPrinterFolder::GetDefaultColumnState(UINT iColumn, SHCOLSTATEF *pcsFlags)
{
    if (!pcsFlags || iColumn >= _countof(PrinterColumns))
        return E_INVALIDARG;
    *pcsFlags = PrinterColumns[iColumn].csFlags;
    return S_OK;
}

STDMETHODIMP CPrinterFolder::GetDetailsEx(PCUITEMID_CHILD pidl, const SHCOLUMNID *pscid, VARIANT *pv)
{
    return E_NOTIMPL;
}

STDMETHODIMP CPrinterFolder::GetDetailsOf(PCUITEMID_CHILD pidl, UINT iColumn, SHELLDETAILS *psd)
{
    WCHAR szText[MAX_PATH];

    if (!psd)
        return E_FAIL;
    if (iColumn >= _countof(PrinterColumns))
        return E_NOTIMPL;

    psd->fmt = PrinterColumns[iColumn].fmt;
    psd->cxChar = PrinterColumns[iColumn].cxChar;
    if (!pidl)
    {
        if (!LoadStringW(prnfldr_hInstance, PrinterColumns[iColumn].idsName, szText, _countof(szText)))
            return HRESULT_FROM_WIN32(GetLastError());
        return SetStrRet(&psd->str, szText);
    }

    if (iColumn == COLUMN_NAME)
        return GetDisplayNameOf(pidl, SHGDN_NORMAL, &psd->str);

    return SetStrRet(&psd->str, L"");
}

STDMETHODIMP CPrinterFolder::MapColumnToSCID(UINT column, SHCOLUMNID *pscid)
{
    return E_NOTIMPL;
}

STDMETHODIMP CPrinterFolder::GetClassID(CLSID *lpClassId)
{
    if (!lpClassId)
        return E_POINTER;
    *lpClassId = CLSID_Printers;
    return S_OK;
}

STDMETHODIMP CPrinterFolder::Initialize(PCIDLIST_ABSOLUTE pidl)
{
    m_pidlRoot.Free();
    if (pidl)
    {
        m_pidlRoot.Attach(ILClone(pidl));
        if (!m_pidlRoot)
            return E_OUTOFMEMORY;
    }
    return S_OK;
}

STDMETHODIMP CPrinterFolder::GetCurFolder(PIDLIST_ABSOLUTE *pidl)
{
    if (!pidl)
        return E_POINTER;
    *pidl = NULL;
    if (!m_pidlRoot)
        return S_FALSE;
    *pidl = ILClone(m_pidlRoot);
    return *pidl ? S_OK : E_OUTOFMEMORY;
}

STDMETHODIMP CPrinterFolder::GetOverlayIndex(PCUITEMID_CHILD pidl, int *pIndex)
{
    const PRINTER_ITEM *pItem = GetPrinterItem(pidl);
    int iOverlay;

    if (!pIndex)
        return E_INVALIDARG;
    if (!pItem || !(pItem->Attributes & PRINTER_ATTRIBUTE_SHARED))
        return S_FALSE;
    if (*pIndex == (int)OI_ASYNC)
        return E_PENDING;

    iOverlay = SHGetIconOverlayIndexW(NULL, IDO_SHGIOI_SHARE);
    if (iOverlay < 0)
        return S_FALSE;
    *pIndex = iOverlay;
    return S_OK;
}

STDMETHODIMP CPrinterFolder::GetOverlayIconIndex(PCUITEMID_CHILD pidl, int *pIconIndex)
{
    const PRINTER_ITEM *pItem = GetPrinterItem(pidl);
    int iIcon;

    if (!pIconIndex)
        return E_INVALIDARG;
    if (!pItem || !(pItem->Attributes & PRINTER_ATTRIBUTE_SHARED))
        return S_FALSE;
    if (*pIconIndex == (int)OI_ASYNC)
        return E_PENDING;

    iIcon = Shell_GetCachedImageIndex(L"shell32.dll", -SHELL32_IDI_SHELL_SHARE, 0);
    if (iIcon < 0)
        return S_FALSE;
    *pIconIndex = iIcon;
    return S_OK;
}

STDMETHODIMP CPrinterFolder::Initialize(LPCWSTR pszMachine, BOOL bEnumerating)
{
    if (!pszMachine)
        return E_INVALIDARG;

    m_pszServer.Free();
    return SHStrDupW(pszMachine, &m_pszServer);
}

STDMETHODIMP CPrinterFolder::CallBack(IShellFolder *psf, HWND hwndOwner, IDataObject *pdtobj, UINT uMsg, WPARAM wParam,
                                      LPARAM lParam)
{
    switch (uMsg)
    {
        case DFM_MERGECONTEXTMENU:
        case DFM_INVOKECOMMAND:
        case DFM_GETHELPTEXT:
        case DFM_VALIDATECMD:
        case DFM_GETVERBW:
            return S_FALSE;
    }
    return E_NOTIMPL;
}

STDMETHODIMP CPrinterFolder::ResolveShellLink(IUnknown *punkLink, HWND hwnd, DWORD fFlags)
{
    return S_OK;
}

STDMETHODIMP CPrinterFolder::GetFolderType(FOLDERTYPEID *pftid)
{
    if (!pftid)
        return E_POINTER;
    *pftid = FOLDERTYPEID_Printers;
    return S_OK;
}

STDMETHODIMP CPrinterFolder::RemoveBackReferences()
{
    return S_OK;
}

OBJECT_ENTRY_AUTO(CLSID_Printers, CPrinterFolder)
