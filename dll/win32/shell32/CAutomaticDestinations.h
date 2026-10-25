/*
 * PROJECT:     LiberNT Windows Storage
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     Recent and frequent document lists of applications
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

HRESULT JumpListPath(LPCWSTR pszAppID, LPCWSTR pszFolder, LPCWSTR pszExtension, BOOL bCreate, CStringW &Path);

class CApplicationDocumentLists :
    public CComCoClass<CApplicationDocumentLists, &CLSID_ApplicationDocumentLists>,
    public CComObjectRootEx<CComMultiThreadModelNoCS>,
    public IApplicationDocumentLists
{
private:
    CStringW m_AppId;

public:
    STDMETHODIMP SetAppID(LPCWSTR pszAppID) override;
    STDMETHODIMP GetList(APPDOCLISTTYPE listtype, UINT cItemsDesired, REFIID riid, void **ppv) override;

    DECLARE_REGISTRY_RESOURCEID(IDR_APPLICATIONDOCUMENTLISTS)
    DECLARE_NOT_AGGREGATABLE(CApplicationDocumentLists)
    DECLARE_PROTECT_FINAL_CONSTRUCT()

    BEGIN_COM_MAP(CApplicationDocumentLists)
        COM_INTERFACE_ENTRY_IID(IID_IApplicationDocumentLists, IApplicationDocumentLists)
    END_COM_MAP()
};

class CApplicationDestinations :
    public CComCoClass<CApplicationDestinations, &CLSID_ApplicationDestinations>,
    public CComObjectRootEx<CComMultiThreadModelNoCS>,
    public IApplicationDestinations
{
private:
    CStringW m_AppId;

public:
    STDMETHODIMP SetAppID(LPCWSTR pszAppID) override;
    STDMETHODIMP RemoveDestination(IUnknown *punk) override;
    STDMETHODIMP RemoveAllDestinations() override;

    DECLARE_REGISTRY_RESOURCEID(IDR_APPLICATIONDESTINATIONS)
    DECLARE_NOT_AGGREGATABLE(CApplicationDestinations)
    DECLARE_PROTECT_FINAL_CONSTRUCT()

    BEGIN_COM_MAP(CApplicationDestinations)
        COM_INTERFACE_ENTRY_IID(IID_IApplicationDestinations, IApplicationDestinations)
    END_COM_MAP()
};
