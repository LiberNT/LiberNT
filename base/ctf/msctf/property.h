/*
 * PROJECT:     LiberNT CTF
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     ITfProperty implementation
 * COPYRIGHT:   Copyright 2026 mikuta0407 <mikuta0407@mikuta0407.net>
 */

#pragma once

class CRange;

struct PropertySpan
{
    CRange *range;
    VARIANT value;
};

class CProperty final : public ITfProperty
{
public:
    CProperty(ITfContext *context, REFGUID guid);

    REFGUID Guid() const { return m_guid; }
    void Detach();
    HRESULT ClearSpan(LONG start, LONG end);

    STDMETHODIMP QueryInterface(REFIID riid, void **ppvObj) override;
    STDMETHODIMP_(ULONG) AddRef() override;
    STDMETHODIMP_(ULONG) Release() override;

    STDMETHODIMP GetType(GUID *pguid) override;
    STDMETHODIMP EnumRanges(TfEditCookie ec, IEnumTfRanges **ppEnum, ITfRange *pTargetRange) override;
    STDMETHODIMP GetValue(TfEditCookie ec, ITfRange *pRange, VARIANT *pvarValue) override;
    STDMETHODIMP GetContext(ITfContext **ppContext) override;

    STDMETHODIMP FindRange(TfEditCookie ec, ITfRange *pRange, ITfRange **ppRange, TfAnchor aPos) override;
    STDMETHODIMP SetValueStore(TfEditCookie ec, ITfRange *pRange, ITfPropertyStore *pPropStore) override;
    STDMETHODIMP SetValue(TfEditCookie ec, ITfRange *pRange, const VARIANT *pvarValue) override;
    STDMETHODIMP Clear(TfEditCookie ec, ITfRange *pRange) override;

private:
    ~CProperty();
    HRESULT AddSpan(LONG start, LONG end, const VARIANT *value);
    void RemoveSpan(ULONG index);
    void Compact();

    LONG m_refs;
    ITfContext *m_context;
    GUID m_guid;
    PropertySpan *m_spans;
    ULONG m_count;
    ULONG m_capacity;
};

class CEnumRanges final : public IEnumTfRanges
{
public:
    CEnumRanges();
    HRESULT Add(ITfRange *range);

    STDMETHODIMP QueryInterface(REFIID riid, void **ppvObj) override;
    STDMETHODIMP_(ULONG) AddRef() override;
    STDMETHODIMP_(ULONG) Release() override;

    STDMETHODIMP Clone(IEnumTfRanges **ppEnum) override;
    STDMETHODIMP Next(ULONG ulCount, ITfRange **ppRange, ULONG *pcFetched) override;
    STDMETHODIMP Reset() override;
    STDMETHODIMP Skip(ULONG ulCount) override;

private:
    ~CEnumRanges();

    LONG m_refs;
    ITfRange **m_ranges;
    ULONG m_count;
    ULONG m_capacity;
    ULONG m_index;
};

class CAppProperty final : public ITfReadOnlyProperty
{
public:
    CAppProperty(ITfContext *context, REFGUID guid);

    STDMETHODIMP QueryInterface(REFIID riid, void **ppvObj) override;
    STDMETHODIMP_(ULONG) AddRef() override;
    STDMETHODIMP_(ULONG) Release() override;

    STDMETHODIMP GetType(GUID *pguid) override;
    STDMETHODIMP EnumRanges(TfEditCookie ec, IEnumTfRanges **ppEnum, ITfRange *pTargetRange) override;
    STDMETHODIMP GetValue(TfEditCookie ec, ITfRange *pRange, VARIANT *pvarValue) override;
    STDMETHODIMP GetContext(ITfContext **ppContext) override;

private:
    ~CAppProperty();

    LONG m_refs;
    ITfContext *m_context;
    GUID m_guid;
};
