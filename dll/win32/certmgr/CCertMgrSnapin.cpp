/*
 * PROJECT:     LiberNT Certificates Snap-in
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     Certificates snap-in for the user certificate stores
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"
#include <atlstr.h>

WINE_DEFAULT_DEBUG_CHANNEL(certmgr);

#define CERTMGR_COLUMN_COUNT 5
#define CERTMGR_PRIVATE_FORMAT L"LiberNT Certificates Snap-in Cookie"

static const BYTE CertMgrDefaultState[36] =
{
    0x11, 0x27, 0x00, 0x00, 0x65, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00
};

typedef struct _CERTMGR_COOKIE_DATA
{
    MMC_COOKIE cookie;
    DATA_OBJECT_TYPES type;
} CERTMGR_COOKIE_DATA;

static CLIPFORMAT g_cfNodeType, g_cfSzNodeType, g_cfDisplayName, g_cfSnapinClsid, g_cfCookie;

static VOID CertMgr_RegisterFormats(VOID)
{
    if (g_cfCookie)
        return;
    g_cfNodeType = (CLIPFORMAT)RegisterClipboardFormatW(CCF_NODETYPE);
    g_cfSzNodeType = (CLIPFORMAT)RegisterClipboardFormatW(CCF_SZNODETYPE);
    g_cfDisplayName = (CLIPFORMAT)RegisterClipboardFormatW(CCF_DISPLAY_NAME);
    g_cfSnapinClsid = (CLIPFORMAT)RegisterClipboardFormatW(CCF_SNAPIN_CLASSID);
    g_cfCookie = (CLIPFORMAT)RegisterClipboardFormatW(CERTMGR_PRIVATE_FORMAT);
}

static HRESULT CertMgr_GetCookie(IDataObject *pDataObject, CERTMGR_COOKIE_DATA *pData)
{
    FORMATETC fmt = { 0, NULL, DVASPECT_CONTENT, -1, TYMED_HGLOBAL };
    STGMEDIUM medium = { TYMED_HGLOBAL };
    HRESULT hr;
    void *pv;

    if (!pDataObject)
        return E_POINTER;

    CertMgr_RegisterFormats();
    fmt.cfFormat = g_cfCookie;
    medium.hGlobal = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, sizeof(*pData));
    if (!medium.hGlobal)
        return E_OUTOFMEMORY;

    hr = pDataObject->GetDataHere(&fmt, &medium);
    if (SUCCEEDED(hr))
    {
        pv = GlobalLock(medium.hGlobal);
        if (pv)
        {
            CopyMemory(pData, pv, sizeof(*pData));
            GlobalUnlock(medium.hGlobal);
        }
        else
        {
            hr = E_UNEXPECTED;
        }
    }
    GlobalFree(medium.hGlobal);
    return hr;
}

class CStoreNode
{
public:
    CStringW m_CryptName;
    CStringW m_DisplayName;
    CStoreNode *m_pStore;
    BOOL m_bExpanded;

    CStoreNode(LPCWSTR pszCryptName) : m_CryptName(pszCryptName), m_pStore(NULL), m_bExpanded(FALSE)
    {
        LPCWSTR pszLocalized = CryptFindLocalizedName(pszCryptName);
        m_DisplayName = pszLocalized ? pszLocalized : pszCryptName;
    }

    CStoreNode(CStoreNode *pStore) : m_CryptName(pStore->m_CryptName), m_pStore(pStore), m_bExpanded(TRUE)
    {
        m_DisplayName.LoadString(certmgr_hInstance, IDS_FOLDER_CERTIFICATES);
    }
};

class CCertItem
{
public:
    PCCERT_CONTEXT m_pCert;
    CStringW m_Columns[CERTMGR_COLUMN_COUNT];

    CCertItem(PCCERT_CONTEXT pCert) : m_pCert(CertDuplicateCertificateContext(pCert))
    {
        WCHAR szBuffer[512];
        SYSTEMTIME stUtc, stLocal;
        DWORD cb = 0;

        CertGetNameStringW(pCert, CERT_NAME_SIMPLE_DISPLAY_TYPE, 0, NULL, szBuffer, _countof(szBuffer));
        m_Columns[0] = szBuffer;
        CertGetNameStringW(pCert, CERT_NAME_SIMPLE_DISPLAY_TYPE, CERT_NAME_ISSUER_FLAG, NULL, szBuffer, _countof(szBuffer));
        m_Columns[1] = szBuffer;

        if (FileTimeToSystemTime(&pCert->pCertInfo->NotAfter, &stUtc) &&
            SystemTimeToTzSpecificLocalTime(NULL, &stUtc, &stLocal) &&
            GetDateFormatW(LOCALE_USER_DEFAULT, DATE_SHORTDATE, &stLocal, NULL, szBuffer, _countof(szBuffer)))
        {
            m_Columns[2] = szBuffer;
        }

        if (CertGetEnhancedKeyUsage(pCert, 0, NULL, &cb) && cb)
        {
            PCERT_ENHKEY_USAGE pUsage = (PCERT_ENHKEY_USAGE)HeapAlloc(GetProcessHeap(), 0, cb);
            if (pUsage && CertGetEnhancedKeyUsage(pCert, 0, pUsage, &cb))
            {
                for (DWORD i = 0; i < pUsage->cUsageIdentifier; i++)
                {
                    PCCRYPT_OID_INFO pInfo = CryptFindOIDInfo(CRYPT_OID_INFO_OID_KEY, pUsage->rgpszUsageIdentifier[i],
                                                              CRYPT_ENHKEY_USAGE_OID_GROUP_ID);
                    if (!m_Columns[3].IsEmpty())
                        m_Columns[3] += L", ";
                    if (pInfo && pInfo->pwszName)
                        m_Columns[3] += pInfo->pwszName;
                    else
                        m_Columns[3] += CStringW(pUsage->rgpszUsageIdentifier[i]);
                }
                if (!pUsage->cUsageIdentifier && GetLastError() == (DWORD)CRYPT_E_NOT_FOUND)
                    m_Columns[3].LoadString(certmgr_hInstance, IDS_PURPOSES_ALL);
            }
            if (pUsage)
                HeapFree(GetProcessHeap(), 0, pUsage);
        }

        cb = sizeof(szBuffer);
        if (CertGetCertificateContextProperty(pCert, CERT_FRIENDLY_NAME_PROP_ID, szBuffer, &cb) && cb > sizeof(WCHAR))
            m_Columns[4] = szBuffer;
        else
            m_Columns[4].LoadString(certmgr_hInstance, IDS_FRIENDLY_NAME_NONE);
    }

    ~CCertItem()
    {
        if (m_pCert)
            CertFreeCertificateContext(m_pCert);
    }
};

class CCertMgrDataObject :
    public CComObjectRootEx<CComMultiThreadModelNoCS>,
    public IDataObject
{
public:
    CERTMGR_COOKIE_DATA m_Data;
    GUID m_NodeType;
    CStringW m_DisplayName;

    static HRESULT WriteToHGlobal(HGLOBAL hGlobal, const void *pv, ULONG cb)
    {
        CComPtr<IStream> pStream;
        HRESULT hr = CreateStreamOnHGlobal(hGlobal, FALSE, &pStream);

        if (FAILED(hr))
            return hr;
        return pStream->Write(pv, cb, NULL);
    }

    STDMETHOD(GetData)(FORMATETC *pformatetcIn, STGMEDIUM *pmedium) override
    {
        return E_NOTIMPL;
    }

    STDMETHOD(GetDataHere)(FORMATETC *pformatetc, STGMEDIUM *pmedium) override
    {
        WCHAR szGuid[40];

        if (!pformatetc || !pmedium)
            return E_POINTER;
        if (pmedium->tymed != TYMED_HGLOBAL || !pmedium->hGlobal)
            return DV_E_TYMED;

        CertMgr_RegisterFormats();
        if (pformatetc->cfFormat == g_cfCookie)
            return WriteToHGlobal(pmedium->hGlobal, &m_Data, sizeof(m_Data));
        if (pformatetc->cfFormat == g_cfNodeType)
            return WriteToHGlobal(pmedium->hGlobal, &m_NodeType, sizeof(m_NodeType));
        if (pformatetc->cfFormat == g_cfSzNodeType)
        {
            StringFromGUID2(m_NodeType, szGuid, _countof(szGuid));
            return WriteToHGlobal(pmedium->hGlobal, szGuid, (ULONG)((wcslen(szGuid) + 1) * sizeof(WCHAR)));
        }
        if (pformatetc->cfFormat == g_cfDisplayName)
            return WriteToHGlobal(pmedium->hGlobal, (LPCWSTR)m_DisplayName, (m_DisplayName.GetLength() + 1) * sizeof(WCHAR));
        if (pformatetc->cfFormat == g_cfSnapinClsid)
            return WriteToHGlobal(pmedium->hGlobal, &CLSID_CertificateManager, sizeof(CLSID));
        return DV_E_FORMATETC;
    }

    STDMETHOD(QueryGetData)(FORMATETC *pformatetc) override
    {
        return E_NOTIMPL;
    }

    STDMETHOD(GetCanonicalFormatEtc)(FORMATETC *pformatectIn, FORMATETC *pformatetcOut) override
    {
        return E_NOTIMPL;
    }

    STDMETHOD(SetData)(FORMATETC *pformatetc, STGMEDIUM *pmedium, BOOL fRelease) override
    {
        return E_NOTIMPL;
    }

    STDMETHOD(EnumFormatEtc)(DWORD dwDirection, IEnumFORMATETC **ppenumFormatEtc) override
    {
        return E_NOTIMPL;
    }

    STDMETHOD(DAdvise)(FORMATETC *pformatetc, DWORD advf, IAdviseSink *pAdvSink, DWORD *pdwConnection) override
    {
        return OLE_E_ADVISENOTSUPPORTED;
    }

    STDMETHOD(DUnadvise)(DWORD dwConnection) override
    {
        return OLE_E_ADVISENOTSUPPORTED;
    }

    STDMETHOD(EnumDAdvise)(IEnumSTATDATA **ppenumAdvise) override
    {
        return OLE_E_ADVISENOTSUPPORTED;
    }

    static HRESULT Create(MMC_COOKIE cookie, DATA_OBJECT_TYPES type, REFGUID nodeType, LPCWSTR pszDisplayName,
                          IDataObject **ppDataObject)
    {
        CComObject<CCertMgrDataObject> *pObject;
        HRESULT hr;

        if (!ppDataObject)
            return E_POINTER;
        *ppDataObject = NULL;
        hr = CComObject<CCertMgrDataObject>::CreateInstance(&pObject);
        if (FAILED(hr))
            return hr;
        pObject->m_Data.cookie = cookie;
        pObject->m_Data.type = type;
        pObject->m_NodeType = nodeType;
        pObject->m_DisplayName = pszDisplayName;
        return pObject->QueryInterface(IID_PPV_ARG(IDataObject, ppDataObject));
    }

    BEGIN_COM_MAP(CCertMgrDataObject)
        COM_INTERFACE_ENTRY_IID(IID_IDataObject, IDataObject)
    END_COM_MAP()
};

class CCertMgrSnapin;

class CCertMgrComponent :
    public CComObjectRootEx<CComMultiThreadModelNoCS>,
    public IComponent,
    public IResultDataCompare
{
    CComPtr<IConsole> m_pConsole;
    CComPtr<IResultData> m_pResultData;
    CComPtr<IHeaderCtrl> m_pHeaderCtrl;
    CSimpleArray<CCertItem *> m_Items;
    CStoreNode *m_pShownStore;

    VOID ClearItems()
    {
        for (int i = 0; i < m_Items.GetSize(); i++)
            delete m_Items[i];
        m_Items.RemoveAll();
    }

    HRESULT ShowStore(CStoreNode *pNode);

public:
    CCertMgrSnapin *m_pSnapin;

    CCertMgrComponent() : m_pShownStore(NULL), m_pSnapin(NULL) {}

    ~CCertMgrComponent()
    {
        ClearItems();
    }

    STDMETHOD(Initialize)(LPCONSOLE lpConsole) override;
    STDMETHOD(Notify)(LPDATAOBJECT lpDataObject, MMC_NOTIFY_TYPE event, LPARAM arg, LPARAM param) override;
    STDMETHOD(Destroy)(MMC_COOKIE cookie) override;
    STDMETHOD(QueryDataObject)(MMC_COOKIE cookie, DATA_OBJECT_TYPES type, LPDATAOBJECT *ppDataObject) override;
    STDMETHOD(GetResultViewType)(MMC_COOKIE cookie, LPOLESTR *ppViewType, long *pViewOptions) override;
    STDMETHOD(GetDisplayInfo)(RESULTDATAITEM *pResultDataItem) override;
    STDMETHOD(CompareObjects)(LPDATAOBJECT lpDataObjectA, LPDATAOBJECT lpDataObjectB) override;

    STDMETHOD(Compare)(LPARAM lUserParam, MMC_COOKIE cookieA, MMC_COOKIE cookieB, int *pnResult) override;

    BEGIN_COM_MAP(CCertMgrComponent)
        COM_INTERFACE_ENTRY_IID(IID_IComponent, IComponent)
        COM_INTERFACE_ENTRY_IID(IID_IResultDataCompare, IResultDataCompare)
    END_COM_MAP()
};

class CCertMgrSnapin :
    public CComCoClass<CCertMgrSnapin, &CLSID_CertificateManager>,
    public CComObjectRootEx<CComMultiThreadModel>,
    public IComponentData,
    public IExtendPropertySheet,
    public IPersistStream
{
    CComPtr<IConsole> m_pConsole;
    CComPtr<IConsoleNameSpace> m_pNameSpace;
    CSimpleArray<CStoreNode *> m_Stores;
    BYTE m_State[sizeof(CertMgrDefaultState)];
    BOOL m_bExpanded;
    CStringW m_RootName;

    static BOOL WINAPI EnumStoreCallback(const void *pvSystemStore, DWORD dwFlags, PCERT_SYSTEM_STORE_INFO pStoreInfo,
                                         void *pvReserved, void *pvArg)
    {
        CCertMgrSnapin *pThis = (CCertMgrSnapin *)pvArg;
        CStoreNode *pNode = new CStoreNode((LPCWSTR)pvSystemStore);

        if (pNode && !pThis->m_Stores.Add(pNode))
            delete pNode;
        return TRUE;
    }

    VOID ClearStores()
    {
        for (int i = 0; i < m_Stores.GetSize(); i++)
            delete m_Stores[i];
        m_Stores.RemoveAll();
        m_bExpanded = FALSE;
    }

    HRESULT InsertScopeItem(CStoreNode *pNode, HSCOPEITEM hParent);
    HRESULT ExpandRoot(HSCOPEITEM hParent);
    HRESULT ExpandStore(CStoreNode *pStore, HSCOPEITEM hParent);

public:
    CCertMgrSnapin() : m_bExpanded(FALSE)
    {
        CopyMemory(m_State, CertMgrDefaultState, sizeof(m_State));
        m_RootName.LoadString(certmgr_hInstance, IDS_ROOT_CURRENT_USER);
    }

    ~CCertMgrSnapin()
    {
        ClearStores();
    }

    BOOL IsStore(MMC_COOKIE cookie)
    {
        for (int i = 0; i < m_Stores.GetSize(); i++)
        {
            if ((MMC_COOKIE)m_Stores[i] == cookie)
                return TRUE;
        }
        return FALSE;
    }

    LPCWSTR GetNodeName(MMC_COOKIE cookie)
    {
        if (!cookie)
            return m_RootName;
        if (IsStore(cookie))
            return ((CStoreNode *)cookie)->m_DisplayName;
        return L"";
    }

    STDMETHOD(Initialize)(LPUNKNOWN pUnknown) override;
    STDMETHOD(CreateComponent)(LPCOMPONENT *ppComponent) override;
    STDMETHOD(Notify)(LPDATAOBJECT lpDataObject, MMC_NOTIFY_TYPE event, LPARAM arg, LPARAM param) override;
    STDMETHOD(Destroy)() override;
    STDMETHOD(QueryDataObject)(MMC_COOKIE cookie, DATA_OBJECT_TYPES type, LPDATAOBJECT *ppDataObject) override;
    STDMETHOD(GetDisplayInfo)(SCOPEDATAITEM *pScopeDataItem) override;
    STDMETHOD(CompareObjects)(LPDATAOBJECT lpDataObjectA, LPDATAOBJECT lpDataObjectB) override;

    STDMETHOD(CreatePropertyPages)(LPPROPERTYSHEETCALLBACK lpProvider, LONG_PTR handle, LPDATAOBJECT lpIDataObject) override;
    STDMETHOD(QueryPagesFor)(LPDATAOBJECT lpDataObject) override;

    STDMETHOD(GetClassID)(CLSID *pClassID) override;
    STDMETHOD(IsDirty)() override;
    STDMETHOD(Load)(IStream *pStm) override;
    STDMETHOD(Save)(IStream *pStm, BOOL fClearDirty) override;
    STDMETHOD(GetSizeMax)(ULARGE_INTEGER *pcbSize) override;

    DECLARE_REGISTRY_RESOURCEID(IDR_CERTMGR)
    DECLARE_NOT_AGGREGATABLE(CCertMgrSnapin)
    DECLARE_PROTECT_FINAL_CONSTRUCT()

    BEGIN_COM_MAP(CCertMgrSnapin)
        COM_INTERFACE_ENTRY_IID(IID_IComponentData, IComponentData)
        COM_INTERFACE_ENTRY_IID(IID_IExtendPropertySheet, IExtendPropertySheet)
        COM_INTERFACE_ENTRY_IID(IID_IPersistStream, IPersistStream)
    END_COM_MAP()
};

HRESULT CCertMgrSnapin::ExpandRoot(HSCOPEITEM hParent)
{
    HICON hIcon;
    HRESULT hr = S_OK;

    if (m_bExpanded)
        return S_OK;

    CertEnumSystemStore(CERT_SYSTEM_STORE_CURRENT_USER, NULL, this, EnumStoreCallback);
    m_bExpanded = TRUE;

    hIcon = (HICON)LoadImageW(certmgr_hInstance, MAKEINTRESOURCEW(IDI_CERTMGR), IMAGE_ICON, 16, 16, 0);
    if (hIcon)
    {
        CComPtr<IImageList> pImageList;
        if (SUCCEEDED(m_pConsole->QueryScopeImageList(&pImageList)))
            pImageList->ImageListSetIcon((LONG_PTR *)hIcon, 0);
        DestroyIcon(hIcon);
    }

    for (int i = 0; i < m_Stores.GetSize() && SUCCEEDED(hr); i++)
        hr = InsertScopeItem(m_Stores[i], hParent);
    return hr;
}

HRESULT CCertMgrSnapin::InsertScopeItem(CStoreNode *pNode, HSCOPEITEM hParent)
{
    SCOPEDATAITEM item = { 0 };

    item.mask = SDI_STR | SDI_IMAGE | SDI_STATE | SDI_PARAM | SDI_PARENT;
    item.displayname = MMC_CALLBACK;
    item.nImage = 0;
    item.nState = 0;
    item.lParam = (LPARAM)pNode;
    item.relativeID = hParent;
    return m_pNameSpace->InsertItem(&item);
}

HRESULT CCertMgrSnapin::ExpandStore(CStoreNode *pStore, HSCOPEITEM hParent)
{
    CStoreNode *pFolder;

    if (pStore->m_bExpanded)
        return S_OK;
    pStore->m_bExpanded = TRUE;

    pFolder = new CStoreNode(pStore);
    if (!pFolder)
        return E_OUTOFMEMORY;
    if (!m_Stores.Add(pFolder))
    {
        delete pFolder;
        return E_OUTOFMEMORY;
    }
    return InsertScopeItem(pFolder, hParent);
}

STDMETHODIMP CCertMgrSnapin::Initialize(LPUNKNOWN pUnknown)
{
    HRESULT hr;

    if (!pUnknown)
        return E_POINTER;

    hr = pUnknown->QueryInterface(IID_PPV_ARG(IConsole, &m_pConsole));
    if (FAILED(hr))
        return hr;
    hr = pUnknown->QueryInterface(IID_PPV_ARG(IConsoleNameSpace, &m_pNameSpace));
    if (FAILED(hr))
    {
        m_pConsole.Release();
        return hr;
    }
    return S_OK;
}

STDMETHODIMP CCertMgrSnapin::CreateComponent(LPCOMPONENT *ppComponent)
{
    CComObject<CCertMgrComponent> *pComponent;
    HRESULT hr;

    if (!ppComponent)
        return E_POINTER;
    *ppComponent = NULL;
    hr = CComObject<CCertMgrComponent>::CreateInstance(&pComponent);
    if (FAILED(hr))
        return hr;
    pComponent->m_pSnapin = this;
    return pComponent->QueryInterface(IID_PPV_ARG(IComponent, ppComponent));
}

STDMETHODIMP CCertMgrSnapin::Notify(LPDATAOBJECT lpDataObject, MMC_NOTIFY_TYPE event, LPARAM arg, LPARAM param)
{
    CERTMGR_COOKIE_DATA data;
    HRESULT hr;

    switch (event)
    {
        case MMCN_EXPAND:
            if (!arg)
                return S_OK;
            hr = CertMgr_GetCookie(lpDataObject, &data);
            if (FAILED(hr))
                return hr;
            if (data.cookie == 0)
                return ExpandRoot((HSCOPEITEM)param);
            if (IsStore(data.cookie) && !((CStoreNode *)data.cookie)->m_pStore)
                return ExpandStore((CStoreNode *)data.cookie, (HSCOPEITEM)param);
            return S_OK;

        case MMCN_REMOVE_CHILDREN:
            ClearStores();
            return S_OK;

        default:
            return S_FALSE;
    }
}

STDMETHODIMP CCertMgrSnapin::Destroy()
{
    ClearStores();
    m_pNameSpace.Release();
    m_pConsole.Release();
    return S_OK;
}

STDMETHODIMP CCertMgrSnapin::QueryDataObject(MMC_COOKIE cookie, DATA_OBJECT_TYPES type, LPDATAOBJECT *ppDataObject)
{
    return CCertMgrDataObject::Create(cookie, type, cookie ? NODEID_CertMgr_STORE : NODEID_CertMgr_ROOT,
                                      GetNodeName(cookie), ppDataObject);
}

STDMETHODIMP CCertMgrSnapin::GetDisplayInfo(SCOPEDATAITEM *pScopeDataItem)
{
    if (!pScopeDataItem)
        return E_POINTER;
    if (pScopeDataItem->mask & SDI_STR)
        pScopeDataItem->displayname = (LPOLESTR)GetNodeName(pScopeDataItem->lParam);
    return S_OK;
}

STDMETHODIMP CCertMgrSnapin::CompareObjects(LPDATAOBJECT lpDataObjectA, LPDATAOBJECT lpDataObjectB)
{
    CERTMGR_COOKIE_DATA a, b;
    HRESULT hr;

    hr = CertMgr_GetCookie(lpDataObjectA, &a);
    if (FAILED(hr))
        return hr;
    hr = CertMgr_GetCookie(lpDataObjectB, &b);
    if (FAILED(hr))
        return hr;
    return (a.cookie == b.cookie && a.type == b.type) ? S_OK : S_FALSE;
}

STDMETHODIMP CCertMgrSnapin::CreatePropertyPages(LPPROPERTYSHEETCALLBACK lpProvider, LONG_PTR handle,
                                                 LPDATAOBJECT lpIDataObject)
{
    return S_FALSE;
}

STDMETHODIMP CCertMgrSnapin::QueryPagesFor(LPDATAOBJECT lpDataObject)
{
    return S_FALSE;
}

STDMETHODIMP CCertMgrSnapin::GetClassID(CLSID *pClassID)
{
    if (!pClassID)
        return E_POINTER;
    *pClassID = CLSID_CertificateManager;
    return S_OK;
}

STDMETHODIMP CCertMgrSnapin::IsDirty()
{
    return S_FALSE;
}

STDMETHODIMP CCertMgrSnapin::Load(IStream *pStm)
{
    BYTE state[sizeof(m_State)];
    ULONG cbRead = 0;
    HRESULT hr;

    if (!pStm)
        return E_POINTER;
    hr = pStm->Read(state, sizeof(state), &cbRead);
    if (FAILED(hr))
        return hr;
    if (cbRead != sizeof(state))
        return STG_E_READFAULT;
    CopyMemory(m_State, state, sizeof(m_State));
    return S_OK;
}

STDMETHODIMP CCertMgrSnapin::Save(IStream *pStm, BOOL fClearDirty)
{
    if (!pStm)
        return E_POINTER;
    return pStm->Write(m_State, sizeof(m_State), NULL);
}

STDMETHODIMP CCertMgrSnapin::GetSizeMax(ULARGE_INTEGER *pcbSize)
{
    if (!pcbSize)
        return E_POINTER;
    pcbSize->QuadPart = 0;
    return S_OK;
}

HRESULT CCertMgrComponent::ShowStore(CStoreNode *pNode)
{
    static const UINT ColumnIds[CERTMGR_COLUMN_COUNT] =
    {
        IDS_COLUMN_ISSUED_TO, IDS_COLUMN_ISSUED_BY, IDS_COLUMN_EXPIRATION, IDS_COLUMN_PURPOSES, IDS_COLUMN_FRIENDLY_NAME
    };
    HCERTSTORE hStore;
    PCCERT_CONTEXT pCert = NULL;
    CStringW title;

    if (!pNode->m_pStore)
    {
        if (m_pHeaderCtrl)
        {
            title.LoadString(certmgr_hInstance, IDS_COLUMN_OBJECT_TYPE);
            m_pHeaderCtrl->InsertColumn(0, title, LVCFMT_LEFT, 450);
        }
        m_pShownStore = pNode;
        return S_OK;
    }

    if (m_pHeaderCtrl)
    {
        for (int i = 0; i < CERTMGR_COLUMN_COUNT; i++)
        {
            title.LoadString(certmgr_hInstance, ColumnIds[i]);
            m_pHeaderCtrl->InsertColumn(i, title, LVCFMT_LEFT, 150);
        }
    }

    hStore = CertOpenStore(CERT_STORE_PROV_SYSTEM_W, PKCS_7_ASN_ENCODING | X509_ASN_ENCODING, NULL,
                           CERT_SYSTEM_STORE_CURRENT_USER | CERT_STORE_READONLY_FLAG, (LPCWSTR)pNode->m_CryptName);
    if (!hStore)
    {
        m_pShownStore = pNode;
        return S_OK;
    }

    while ((pCert = CertEnumCertificatesInStore(hStore, pCert)) != NULL)
    {
        CCertItem *pItem = new CCertItem(pCert);
        RESULTDATAITEM item = { 0 };

        if (!pItem)
            continue;
        if (!m_Items.Add(pItem))
        {
            delete pItem;
            continue;
        }
        item.mask = RDI_STR | RDI_PARAM | RDI_IMAGE;
        item.str = MMC_CALLBACK;
        item.nImage = 0;
        item.lParam = (LPARAM)pItem;
        if (m_pResultData)
            m_pResultData->InsertItem(&item);
    }
    CertCloseStore(hStore, 0);
    m_pShownStore = pNode;
    return S_OK;
}

STDMETHODIMP CCertMgrComponent::Initialize(LPCONSOLE lpConsole)
{
    if (!lpConsole)
        return E_POINTER;
    m_pConsole = lpConsole;
    lpConsole->QueryInterface(IID_PPV_ARG(IResultData, &m_pResultData));
    lpConsole->QueryInterface(IID_PPV_ARG(IHeaderCtrl, &m_pHeaderCtrl));
    return S_OK;
}

STDMETHODIMP CCertMgrComponent::Notify(LPDATAOBJECT lpDataObject, MMC_NOTIFY_TYPE event, LPARAM arg, LPARAM param)
{
    CERTMGR_COOKIE_DATA data;
    HRESULT hr;

    switch (event)
    {
        case MMCN_SHOW:
            hr = CertMgr_GetCookie(lpDataObject, &data);
            if (FAILED(hr))
                return hr;
            if (!arg)
            {
                ClearItems();
                m_pShownStore = NULL;
                return S_OK;
            }
            if (!m_pSnapin || !m_pSnapin->IsStore(data.cookie))
                return S_OK;
            return ShowStore((CStoreNode *)data.cookie);

        case MMCN_ADD_IMAGES:
        {
            IImageList *pImageList = (IImageList *)arg;
            HICON hIcon = (HICON)LoadImageW(certmgr_hInstance, MAKEINTRESOURCEW(IDI_CERTMGR), IMAGE_ICON, 16, 16, 0);
            if (pImageList && hIcon)
                pImageList->ImageListSetIcon((LONG_PTR *)hIcon, 0);
            if (hIcon)
                DestroyIcon(hIcon);
            return S_OK;
        }

        case MMCN_DBLCLICK:
        {
            HWND hwndMain = NULL;
            hr = CertMgr_GetCookie(lpDataObject, &data);
            if (FAILED(hr) || data.type != CCT_RESULT || !data.cookie)
                return S_FALSE;
            if (m_pConsole)
                m_pConsole->GetMainWindow(&hwndMain);
            CryptUIDlgViewContext(CERT_STORE_CERTIFICATE_CONTEXT, (LPVOID)((CCertItem *)data.cookie)->m_pCert, hwndMain, NULL, 0, NULL);
            return S_OK;
        }

        default:
            return S_FALSE;
    }
}

STDMETHODIMP CCertMgrComponent::Destroy(MMC_COOKIE cookie)
{
    ClearItems();
    m_pHeaderCtrl.Release();
    m_pResultData.Release();
    m_pConsole.Release();
    return S_OK;
}

STDMETHODIMP CCertMgrComponent::QueryDataObject(MMC_COOKIE cookie, DATA_OBJECT_TYPES type, LPDATAOBJECT *ppDataObject)
{
    if (type == CCT_RESULT)
    {
        LPCWSTR pszName = cookie ? (LPCWSTR)((CCertItem *)cookie)->m_Columns[0] : L"";
        return CCertMgrDataObject::Create(cookie, type, NODEID_CertMgr_CERTIFICATE, pszName, ppDataObject);
    }
    if (!m_pSnapin)
        return E_UNEXPECTED;
    return m_pSnapin->QueryDataObject(cookie, type, ppDataObject);
}

STDMETHODIMP CCertMgrComponent::GetResultViewType(MMC_COOKIE cookie, LPOLESTR *ppViewType, long *pViewOptions)
{
    if (!ppViewType || !pViewOptions)
        return E_POINTER;
    *ppViewType = NULL;
    return S_FALSE;
}

STDMETHODIMP CCertMgrComponent::GetDisplayInfo(RESULTDATAITEM *pResultDataItem)
{
    if (!pResultDataItem)
        return E_POINTER;
    if (!(pResultDataItem->mask & RDI_STR))
        return S_OK;

    if (pResultDataItem->bScopeItem)
    {
        pResultDataItem->str = (LPOLESTR)(m_pSnapin ? m_pSnapin->GetNodeName(pResultDataItem->lParam) : L"");
        return S_OK;
    }

    if (pResultDataItem->nCol < 0 || pResultDataItem->nCol >= CERTMGR_COLUMN_COUNT || !pResultDataItem->lParam)
    {
        pResultDataItem->str = (LPOLESTR)L"";
        return S_OK;
    }
    pResultDataItem->str = (LPOLESTR)(LPCWSTR)((CCertItem *)pResultDataItem->lParam)->m_Columns[pResultDataItem->nCol];
    return S_OK;
}

STDMETHODIMP CCertMgrComponent::CompareObjects(LPDATAOBJECT lpDataObjectA, LPDATAOBJECT lpDataObjectB)
{
    CERTMGR_COOKIE_DATA a, b;
    HRESULT hr;

    hr = CertMgr_GetCookie(lpDataObjectA, &a);
    if (FAILED(hr))
        return hr;
    hr = CertMgr_GetCookie(lpDataObjectB, &b);
    if (FAILED(hr))
        return hr;
    return (a.cookie == b.cookie && a.type == b.type) ? S_OK : S_FALSE;
}

STDMETHODIMP CCertMgrComponent::Compare(LPARAM lUserParam, MMC_COOKIE cookieA, MMC_COOKIE cookieB, int *pnResult)
{
    int nCol;

    if (!pnResult || !cookieA || !cookieB)
        return E_POINTER;
    nCol = *pnResult;
    if (nCol < 0 || nCol >= CERTMGR_COLUMN_COUNT)
        return E_INVALIDARG;
    *pnResult = lstrcmpiW(((CCertItem *)cookieA)->m_Columns[nCol], ((CCertItem *)cookieB)->m_Columns[nCol]);
    return S_OK;
}

OBJECT_ENTRY_AUTO(CLSID_CertificateManager, CCertMgrSnapin)
