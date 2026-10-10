/*
 * PROJECT:     LiberNT shdocvw
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     Attachment Services (IAttachmentExecute)
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

class CAttachmentServices :
    public CComCoClass<CAttachmentServices, &CLSID_AttachmentServices>,
    public CComObjectRootEx<CComMultiThreadModelNoCS>,
    public IAttachmentExecute
{
    CComHeapPtr<WCHAR> m_Title;
    CComHeapPtr<WCHAR> m_LocalPath;
    CComHeapPtr<WCHAR> m_FileName;
    CComHeapPtr<WCHAR> m_Source;
    CComHeapPtr<WCHAR> m_Referrer;
    GUID m_ClientGuid;
    BOOL m_HasClientGuid;
    HRESULT m_hrLatched;

    HRESULT Latch(HRESULT hr);
    HRESULT SetString(CComHeapPtr<WCHAR> &Target, LPCWSTR Value, BOOL Once);
    DWORD GetZone();
    BOOL IsDangerous();
    HRESULT EvaluatePolicy();
    HRESULT WriteZoneIdentifier(DWORD Zone);

public:
    CAttachmentServices();

    STDMETHOD(SetClientTitle)(LPCWSTR pszTitle) override;
    STDMETHOD(SetClientGuid)(REFGUID guid) override;
    STDMETHOD(SetLocalPath)(LPCWSTR pszLocalPath) override;
    STDMETHOD(SetFileName)(LPCWSTR pszFileName) override;
    STDMETHOD(SetSource)(LPCWSTR pszSource) override;
    STDMETHOD(SetReferrer)(LPCWSTR pszReferrer) override;
    STDMETHOD(CheckPolicy)() override;
    STDMETHOD(Prompt)(HWND hwnd, ATTACHMENT_PROMPT prompt, ATTACHMENT_ACTION *paction) override;
    STDMETHOD(Save)() override;
    STDMETHOD(Execute)(HWND hwnd, LPCWSTR pszVerb, HANDLE *phProcess) override;
    STDMETHOD(SaveWithUI)(HWND hwnd) override;
    STDMETHOD(ClearClientState)() override;

    DECLARE_REGISTRY_RESOURCEID(IDR_ATTACHMENTSERVICES)
    DECLARE_NOT_AGGREGATABLE(CAttachmentServices)

    BEGIN_COM_MAP(CAttachmentServices)
        COM_INTERFACE_ENTRY_IID(IID_IAttachmentExecute, IAttachmentExecute)
    END_COM_MAP()
};
