/*
 * PROJECT:     LiberNT explorerframe
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     Docking bar property bag
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"

HRESULT CDockingBarPropertyBag::FinalConstruct()
{
    return SHCreatePropertyBagOnMemory(STGM_READWRITE, IID_PPV_ARG(IPropertyBag, &m_pBag));
}

STDMETHODIMP CDockingBarPropertyBag::Read(LPCOLESTR pszPropName, VARIANT *pVar, IErrorLog *pErrorLog)
{
    return m_pBag->Read(pszPropName, pVar, pErrorLog);
}

STDMETHODIMP CDockingBarPropertyBag::Write(LPCOLESTR pszPropName, VARIANT *pVar)
{
    return m_pBag->Write(pszPropName, pVar);
}
