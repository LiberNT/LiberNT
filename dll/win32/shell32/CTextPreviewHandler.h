/*
 * PROJECT:     LiberNT shell32
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     Preview handler for plain text files
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

class CTextPreviewHandler :
    public CComCoClass<CTextPreviewHandler, &CLSID_TextPreviewHandler>,
    public CComObjectRootEx<CComMultiThreadModelNoCS>,
    public IPreviewHandler,
    public IInitializeWithStream,
    public IObjectWithSite,
    public IOleWindow
{
private:
    CComPtr<IStream> m_Stream;
    CComPtr<IUnknown> m_Site;
    HWND m_hwndParent;
    HWND m_hwndPreview;
    RECT m_rc;

public:
    CTextPreviewHandler();
    ~CTextPreviewHandler();

    STDMETHODIMP SetWindow(HWND hwnd, const RECT *prc) override;
    STDMETHODIMP SetRect(const RECT *prc) override;
    STDMETHODIMP DoPreview() override;
    STDMETHODIMP Unload() override;
    STDMETHODIMP SetFocus() override;
    STDMETHODIMP QueryFocus(HWND *phwnd) override;
    STDMETHODIMP TranslateAccelerator(MSG *pmsg) override;

    STDMETHODIMP Initialize(IStream *pstream, DWORD grfMode) override;

    STDMETHODIMP SetSite(IUnknown *pUnkSite) override;
    STDMETHODIMP GetSite(REFIID riid, void **ppvSite) override;

    STDMETHODIMP GetWindow(HWND *phwnd) override;
    STDMETHODIMP ContextSensitiveHelp(BOOL fEnterMode) override;

    DECLARE_REGISTRY_RESOURCEID(IDR_TXTPREVIEWHANDLER)
    DECLARE_NOT_AGGREGATABLE(CTextPreviewHandler)
    DECLARE_PROTECT_FINAL_CONSTRUCT()

    BEGIN_COM_MAP(CTextPreviewHandler)
        COM_INTERFACE_ENTRY_IID(IID_IPreviewHandler, IPreviewHandler)
        COM_INTERFACE_ENTRY_IID(IID_IInitializeWithStream, IInitializeWithStream)
        COM_INTERFACE_ENTRY_IID(IID_IObjectWithSite, IObjectWithSite)
        COM_INTERFACE_ENTRY_IID(IID_IOleWindow, IOleWindow)
    END_COM_MAP()
};
