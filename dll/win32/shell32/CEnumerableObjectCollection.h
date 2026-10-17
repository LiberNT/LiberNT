/*
 * PROJECT:     LiberNT shell32
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Object collection used to build jump list categories and tasks
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

class CEnumerableObjectCollection :
    public CComCoClass<CEnumerableObjectCollection, &CLSID_EnumerableObjectCollection>,
    public CComObjectRootEx<CComMultiThreadModelNoCS>,
    public IObjectCollection,
    public IEnumObjects
{
private:
    CSimpleArray<IUnknown *> m_Items;
    ULONG m_Position;
    CComAutoCriticalSection m_Lock;

public:
    CEnumerableObjectCollection() : m_Position(0) {}
    ~CEnumerableObjectCollection();

    STDMETHODIMP GetCount(UINT *pcObjects) override;
    STDMETHODIMP GetAt(UINT uiIndex, REFIID riid, void **ppv) override;
    STDMETHODIMP AddObject(IUnknown *punk) override;
    STDMETHODIMP AddFromArray(IObjectArray *poaSource) override;
    STDMETHODIMP RemoveObjectAt(UINT uiIndex) override;
    STDMETHODIMP Clear() override;

    STDMETHODIMP Next(ULONG celt, REFIID riid, void **rgelt, ULONG *pceltFetched) override;
    STDMETHODIMP Skip(ULONG celt) override;
    STDMETHODIMP Reset() override;
    STDMETHODIMP Clone(IEnumObjects **ppenum) override;

    DECLARE_REGISTRY_RESOURCEID(IDR_ENUMERABLEOBJECTCOLLECTION)
    DECLARE_NOT_AGGREGATABLE(CEnumerableObjectCollection)
    DECLARE_PROTECT_FINAL_CONSTRUCT()

    BEGIN_COM_MAP(CEnumerableObjectCollection)
        COM_INTERFACE_ENTRY_IID(IID_IObjectCollection, IObjectCollection)
        COM_INTERFACE_ENTRY_IID(IID_IObjectArray, IObjectArray)
        COM_INTERFACE_ENTRY_IID(IID_IEnumObjects, IEnumObjects)
    END_COM_MAP()
};
