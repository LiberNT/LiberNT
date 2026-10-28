/*
 * PROJECT:     LiberNT CTF
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     Text store of contexts owned through ITfContextOwner
 * COPYRIGHT:   Copyright 2026 mikuta0407 <mikuta0407@mikuta0407.net>
 */

#pragma once

class CDefaultTextStore final : public ITextStoreACP
{
public:
    CDefaultTextStore();

    void SetOwner(ITfContextOwner *owner);
    BOOL HasOwner() const { return m_owner != NULL; }

    STDMETHODIMP QueryInterface(REFIID riid, void **ppvObj) override;
    STDMETHODIMP_(ULONG) AddRef() override;
    STDMETHODIMP_(ULONG) Release() override;

    STDMETHODIMP AdviseSink(REFIID riid, IUnknown *punk, DWORD dwMask) override;
    STDMETHODIMP UnadviseSink(IUnknown *punk) override;
    STDMETHODIMP RequestLock(DWORD dwLockFlags, HRESULT *phrSession) override;
    STDMETHODIMP GetStatus(TS_STATUS *pdcs) override;
    STDMETHODIMP QueryInsert(LONG acpTestStart, LONG acpTestEnd, ULONG cch,
                             LONG *pacpResultStart, LONG *pacpResultEnd) override;
    STDMETHODIMP GetSelection(ULONG ulIndex, ULONG ulCount, TS_SELECTION_ACP *pSelection,
                              ULONG *pcFetched) override;
    STDMETHODIMP SetSelection(ULONG ulCount, const TS_SELECTION_ACP *pSelection) override;
    STDMETHODIMP GetText(LONG acpStart, LONG acpEnd, WCHAR *pchPlain, ULONG cchPlainReq,
                         ULONG *pcchPlainRet, TS_RUNINFO *prgRunInfo, ULONG cRunInfoReq,
                         ULONG *pcRunInfoRet, LONG *pacpNext) override;
    STDMETHODIMP SetText(DWORD dwFlags, LONG acpStart, LONG acpEnd, const WCHAR *pchText,
                         ULONG cch, TS_TEXTCHANGE *pChange) override;
    STDMETHODIMP GetFormattedText(LONG acpStart, LONG acpEnd, IDataObject **ppDataObject) override;
    STDMETHODIMP GetEmbedded(LONG acpPos, REFGUID rguidService, REFIID riid, IUnknown **ppunk) override;
    STDMETHODIMP QueryInsertEmbedded(const GUID *pguidService, const FORMATETC *pFormatEtc,
                                     BOOL *pfInsertable) override;
    STDMETHODIMP InsertEmbedded(DWORD dwFlags, LONG acpStart, LONG acpEnd, IDataObject *pDataObject,
                                TS_TEXTCHANGE *pChange) override;
    STDMETHODIMP InsertTextAtSelection(DWORD dwFlags, const WCHAR *pchText, ULONG cch,
                                       LONG *pacpStart, LONG *pacpEnd, TS_TEXTCHANGE *pChange) override;
    STDMETHODIMP InsertEmbeddedAtSelection(DWORD dwFlags, IDataObject *pDataObject, LONG *pacpStart,
                                           LONG *pacpEnd, TS_TEXTCHANGE *pChange) override;
    STDMETHODIMP RequestSupportedAttrs(DWORD dwFlags, ULONG cFilterAttrs,
                                       const TS_ATTRID *paFilterAttrs) override;
    STDMETHODIMP RequestAttrsAtPosition(LONG acpPos, ULONG cFilterAttrs, const TS_ATTRID *paFilterAttrs,
                                        DWORD dwFlags) override;
    STDMETHODIMP RequestAttrsTransitioningAtPosition(LONG acpPos, ULONG cFilterAttrs,
                                                     const TS_ATTRID *paFilterAttrs,
                                                     DWORD dwFlags) override;
    STDMETHODIMP FindNextAttrTransition(LONG acpStart, LONG acpHalt, ULONG cFilterAttrs,
                                        const TS_ATTRID *paFilterAttrs, DWORD dwFlags, LONG *pacpNext,
                                        BOOL *pfFound, LONG *plFoundOffset) override;
    STDMETHODIMP RetrieveRequestedAttrs(ULONG ulCount, TS_ATTRVAL *paAttrVals, ULONG *pcFetched) override;
    STDMETHODIMP GetEndACP(LONG *pacp) override;
    STDMETHODIMP GetActiveView(TsViewCookie *pvcView) override;
    STDMETHODIMP GetACPFromPoint(TsViewCookie vcView, const POINT *ptScreen, DWORD dwFlags,
                                 LONG *pacp) override;
    STDMETHODIMP GetTextExt(TsViewCookie vcView, LONG acpStart, LONG acpEnd, RECT *prc,
                            BOOL *pfClipped) override;
    STDMETHODIMP GetScreenExt(TsViewCookie vcView, RECT *prc) override;
    STDMETHODIMP GetWnd(TsViewCookie vcView, HWND *phwnd) override;

private:
    ~CDefaultTextStore();
    HRESULT Replace(LONG start, LONG end, const WCHAR *text, ULONG cch, TS_TEXTCHANGE *change);
    void ClearRequestedAttrs();

    LONG m_refs;
    WCHAR *m_text;
    LONG m_length;
    LONG m_capacity;
    TS_SELECTION_ACP m_selection;
    ITextStoreACPSink *m_sink;
    ITfContextOwner *m_owner;
    DWORD m_lock;
    DWORD m_pendingLock;
    ULONG m_cAttrs;
    TS_ATTRVAL *m_attrs;
};
