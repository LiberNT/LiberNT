/*
 * PROJECT:   ReactOS Zip Shell Extension
 * LICENSE:   GPL-2.0+ (https://spdx.org/licenses/GPL-2.0+)
 * PURPOSE:   SendTo handler
 * COPYRIGHT: Copyright 2019 Mark Jansen (mark.jansen@reactos.org)
 *            Copyright 2019 Katayama Hirofumi MZ (katayama.hirofumi.mz@gmail.com)
 */

#ifndef CSENDTOZIP_HPP_
#define CSENDTOZIP_HPP_

class CSendToZip :
    public CComCoClass<CSendToZip, &CLSID_ZipFolderSendTo>,
    public CComObjectRootEx<CComMultiThreadModelNoCS>,
    public IDropTarget,
    public IPersistFile,
    public IObjectWithSite
{
    CComPtr<IDataObject> m_pDataObject;
    CComPtr<IUnknown> m_pUnkMarshaler;
    CComPtr<IUnknown> m_pSite;
    BOOL m_fCanDragDrop;

public:
    CSendToZip() : m_fCanDragDrop(FALSE)
    {
        InterlockedIncrement(&g_ModuleRefCnt);
    }

    virtual ~CSendToZip()
    {
        InterlockedDecrement(&g_ModuleRefCnt);
    }

    // *** IShellFolder2 methods ***
    STDMETHODIMP DragEnter(IDataObject *pDataObj, DWORD grfKeyState, POINTL pt, DWORD *pdwEffect);
    STDMETHODIMP DragOver(DWORD grfKeyState, POINTL pt, DWORD *pdwEffect);
    STDMETHODIMP DragLeave();
    STDMETHODIMP Drop(IDataObject *pDataObj, DWORD grfKeyState, POINTL pt, DWORD *pdwEffect);

    // *** IPersistFile methods ***
    STDMETHODIMP IsDirty()
    {
        return S_FALSE;
    }
    STDMETHODIMP Load(LPCOLESTR pszFileName, DWORD dwMode)
    {
        return S_OK;
    }
    STDMETHODIMP Save(LPCOLESTR pszFileName, BOOL fRemember)
    {
        return E_NOTIMPL;
    }
    STDMETHODIMP SaveCompleted(LPCOLESTR pszFileName)
    {
        return E_NOTIMPL;
    }
    STDMETHODIMP GetCurFile(LPOLESTR *ppszFileName)
    {
        return E_NOTIMPL;
    }

    // *** IPersist methods ***
    STDMETHODIMP GetClassID(CLSID *pclsid)
    {
        *pclsid = CLSID_ZipFolderSendTo;
        return S_OK;
    }

    // *** IObjectWithSite methods ***
    STDMETHODIMP SetSite(IUnknown *pUnkSite)
    {
        m_pSite = pUnkSite;
        return S_OK;
    }
    STDMETHODIMP GetSite(REFIID riid, void **ppvSite)
    {
        if (!ppvSite)
            return E_POINTER;
        *ppvSite = NULL;
        if (!m_pSite)
            return E_FAIL;
        return m_pSite->QueryInterface(riid, ppvSite);
    }

public:
    DECLARE_NO_REGISTRY()   // Handled manually
    DECLARE_NOT_AGGREGATABLE(CSendToZip)

    DECLARE_PROTECT_FINAL_CONSTRUCT()

    HRESULT FinalConstruct()
    {
        return CoCreateFreeThreadedMarshaler(static_cast<IDropTarget *>(this), &m_pUnkMarshaler);
    }

    static HRESULT WINAPI QueryMarshaler(void *pv, REFIID riid, LPVOID *ppv, DWORD_PTR dw)
    {
        return static_cast<CSendToZip *>(pv)->m_pUnkMarshaler->QueryInterface(riid, ppv);
    }

    BEGIN_COM_MAP(CSendToZip)
        COM_INTERFACE_ENTRY_IID(IID_IDropTarget, IDropTarget)
        COM_INTERFACE_ENTRY_IID(IID_IPersistFile, IPersistFile)
        COM_INTERFACE_ENTRY_IID(IID_IPersist, IPersist)
        COM_INTERFACE_ENTRY_IID(IID_IObjectWithSite, IObjectWithSite)
        COM_INTERFACE_ENTRY_FUNC(IID_IMarshal, 0, QueryMarshaler)
    END_COM_MAP()
};

#endif
