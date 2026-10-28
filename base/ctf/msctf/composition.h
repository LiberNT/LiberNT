/*
 * PROJECT:     LiberNT CTF
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     ITfComposition implementation
 * COPYRIGHT:   Copyright 2026 mikuta0407 <mikuta0407@mikuta0407.net>
 */

#pragma once

class CRange;

DEFINE_GUID(IID_PRIV_CCOMPOSITION, 0x4b0f5ad8, 0x6f2a, 0x4e5c, 0x9d, 0x31, 0x2b, 0x8e, 0x5c, 0x47, 0x10, 0xa3);

class CComposition final
    : public ITfComposition
    , public ITfCompositionView
{
public:
    CComposition(ITfContext *context, CRange *range, ITfCompositionSink *sink, REFCLSID clsidOwner);

    static CComposition *FromView(IUnknown *punk);
    CRange *Range() const { return m_range; }
    ITfCompositionSink *Sink() const { return m_sink; }
    BOOL IsActive() const { return !m_ended; }
    void MarkEnded();

    STDMETHODIMP QueryInterface(REFIID riid, void **ppvObj) override;
    STDMETHODIMP_(ULONG) AddRef() override;
    STDMETHODIMP_(ULONG) Release() override;

    STDMETHODIMP GetRange(ITfRange **ppRange) override;
    STDMETHODIMP ShiftStart(TfEditCookie ecWrite, ITfRange *pNewStart) override;
    STDMETHODIMP ShiftEnd(TfEditCookie ecWrite, ITfRange *pNewEnd) override;
    STDMETHODIMP EndComposition(TfEditCookie ecWrite) override;

    STDMETHODIMP GetOwnerClsid(CLSID *pclsid) override;

private:
    ~CComposition();

    LONG m_refs;
    ITfContext *m_context;
    CRange *m_range;
    ITfCompositionSink *m_sink;
    CLSID m_clsidOwner;
    BOOL m_ended;
};

class CEnumCompositionView final : public IEnumITfCompositionView
{
public:
    CEnumCompositionView();
    HRESULT Add(ITfCompositionView *view);

    STDMETHODIMP QueryInterface(REFIID riid, void **ppvObj) override;
    STDMETHODIMP_(ULONG) AddRef() override;
    STDMETHODIMP_(ULONG) Release() override;

    STDMETHODIMP Clone(IEnumITfCompositionView **ppEnum) override;
    STDMETHODIMP Next(ULONG ulCount, ITfCompositionView **rgCompositionView, ULONG *pcFetched) override;
    STDMETHODIMP Reset() override;
    STDMETHODIMP Skip(ULONG ulCount) override;

private:
    ~CEnumCompositionView();

    LONG m_refs;
    ITfCompositionView **m_views;
    ULONG m_count;
    ULONG m_capacity;
    ULONG m_index;
};

HRESULT Context_OnCompositionUpdated(ITfContext *context, CComposition *composition);
HRESULT Context_OnCompositionEnded(ITfContext *context, CComposition *composition);
