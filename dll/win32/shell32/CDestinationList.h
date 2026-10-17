/*
 * PROJECT:     LiberNT shell32
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Custom jump list builder
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

class CDestinationList :
    public CComCoClass<CDestinationList, &CLSID_DestinationList>,
    public CComObjectRootEx<CComMultiThreadModelNoCS>,
    public ICustomDestinationList
{
private:
    CStringW m_AppId;
    BOOL m_Building;
    CComPtr<IStream> m_Pending;
    DWORD m_Sections;
    CComAutoCriticalSection m_Lock;

    HRESULT ResolveAppId(LPCWSTR pszAppID, CStringW &AppId);
    HRESULT ListPath(LPCWSTR pszAppID, BOOL Create, CStringW &Path);
    HRESULT WriteSection(DWORD Type, LPCWSTR Name, DWORD Category, IObjectArray *poa);

public:
    CDestinationList() : m_Building(FALSE), m_Sections(0) {}

    STDMETHODIMP SetAppID(LPCWSTR pszAppID) override;
    STDMETHODIMP BeginList(UINT *pcMinSlots, REFIID riid, void **ppv) override;
    STDMETHODIMP AppendCategory(LPCWSTR pszCategory, IObjectArray *poa) override;
    STDMETHODIMP AppendKnownCategory(KNOWNDESTCATEGORY category) override;
    STDMETHODIMP AddUserTasks(IObjectArray *poa) override;
    STDMETHODIMP CommitList() override;
    STDMETHODIMP GetRemovedDestinations(REFIID riid, void **ppv) override;
    STDMETHODIMP DeleteList(LPCWSTR pszAppID) override;
    STDMETHODIMP AbortList() override;

    DECLARE_REGISTRY_RESOURCEID(IDR_DESTINATIONLIST)
    DECLARE_NOT_AGGREGATABLE(CDestinationList)
    DECLARE_PROTECT_FINAL_CONSTRUCT()

    BEGIN_COM_MAP(CDestinationList)
        COM_INTERFACE_ENTRY_IID(IID_ICustomDestinationList, ICustomDestinationList)
    END_COM_MAP()
};
