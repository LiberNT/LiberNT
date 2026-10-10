/*
 * PROJECT:     LiberNT explorerframe
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     Docking bar property bag
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

class CDockingBarPropertyBag :
    public CComCoClass<CDockingBarPropertyBag, &CLSID_CDockingBarPropertyBag>,
    public CComObjectRootEx<CComMultiThreadModelNoCS>,
    public IPropertyBag
{
    CComPtr<IPropertyBag> m_pBag;

public:
    HRESULT FinalConstruct();

    STDMETHOD(Read)(LPCOLESTR pszPropName, VARIANT *pVar, IErrorLog *pErrorLog) override;
    STDMETHOD(Write)(LPCOLESTR pszPropName, VARIANT *pVar) override;

    DECLARE_REGISTRY_RESOURCEID(IDR_DOCKINGBARPROPERTYBAG)
    DECLARE_NOT_AGGREGATABLE(CDockingBarPropertyBag)
    DECLARE_PROTECT_FINAL_CONSTRUCT()

    BEGIN_COM_MAP(CDockingBarPropertyBag)
        COM_INTERFACE_ENTRY_IID(IID_IPropertyBag, IPropertyBag)
    END_COM_MAP()
};
