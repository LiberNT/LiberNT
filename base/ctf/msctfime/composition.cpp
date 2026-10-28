/*
 * PROJECT:     LiberNT msctfime.ime
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     Reflecting text service compositions into input contexts
 * COPYRIGHT:   Copyright 2026 mikuta0407 <mikuta0407@mikuta0407.net>
 */

#include "msctfime.h"

class CicEditSink final : public ITfTextEditSink
{
public:
    CicEditSink(CicInputContext *pIC) : m_cRefs(1), m_pIC(pIC), m_pSource(NULL), m_dwCookie(TF_INVALID_COOKIE) { }

    HRESULT Advise(ITfContext *pContext)
    {
        HRESULT hr = pContext->QueryInterface(IID_ITfSource, (void **)&m_pSource);
        if (FAILED(hr))
            return hr;
        hr = m_pSource->AdviseSink(IID_ITfTextEditSink, static_cast<ITfTextEditSink *>(this), &m_dwCookie);
        if (FAILED(hr))
        {
            m_pSource->Release();
            m_pSource = NULL;
        }
        return hr;
    }

    void Unadvise()
    {
        if (m_pSource)
        {
            m_pSource->UnadviseSink(m_dwCookie);
            m_pSource->Release();
            m_pSource = NULL;
        }
        m_pIC = NULL;
    }

    STDMETHODIMP QueryInterface(REFIID riid, LPVOID *ppvObj) override
    {
        if (!ppvObj)
            return E_POINTER;
        *ppvObj = NULL;
        if (riid != IID_IUnknown && riid != IID_ITfTextEditSink)
            return E_NOINTERFACE;
        *ppvObj = static_cast<ITfTextEditSink *>(this);
        AddRef();
        return S_OK;
    }
    STDMETHODIMP_(ULONG) AddRef() override { return ::InterlockedIncrement(&m_cRefs); }
    STDMETHODIMP_(ULONG) Release() override
    {
        ULONG cRefs = ::InterlockedDecrement(&m_cRefs);
        if (!cRefs)
            delete this;
        return cRefs;
    }

    STDMETHODIMP OnEndEdit(ITfContext *pic, TfEditCookie ecReadOnly, ITfEditRecord *pEditRecord) override
    {
        if (!m_pIC)
            return S_OK;
        return m_pIC->OnEndEdit(ecReadOnly);
    }

private:
    LONG m_cRefs;
    CicInputContext *m_pIC;
    ITfSource *m_pSource;
    DWORD m_dwCookie;
};

typedef HRESULT (*FN_IC_EDIT_SESSION)(CicInputContext *pIC, TfEditCookie ec);

class CicFnEditSession final : public ITfEditSession
{
public:
    CicFnEditSession(CicInputContext *pIC, FN_IC_EDIT_SESSION fn) : m_cRefs(1), m_pIC(pIC), m_fn(fn)
    {
        m_pIC->AddRef();
    }

    STDMETHODIMP QueryInterface(REFIID riid, LPVOID *ppvObj) override
    {
        if (!ppvObj)
            return E_POINTER;
        *ppvObj = NULL;
        if (riid != IID_IUnknown && riid != IID_ITfEditSession)
            return E_NOINTERFACE;
        *ppvObj = static_cast<ITfEditSession *>(this);
        AddRef();
        return S_OK;
    }
    STDMETHODIMP_(ULONG) AddRef() override { return ::InterlockedIncrement(&m_cRefs); }
    STDMETHODIMP_(ULONG) Release() override
    {
        ULONG cRefs = ::InterlockedDecrement(&m_cRefs);
        if (!cRefs)
            delete this;
        return cRefs;
    }

    STDMETHODIMP DoEditSession(TfEditCookie ec) override
    {
        return m_fn(m_pIC, ec);
    }

private:
    ~CicFnEditSession()
    {
        m_pIC->Release();
    }

    LONG m_cRefs;
    CicInputContext *m_pIC;
    FN_IC_EDIT_SESSION m_fn;
};

static HRESULT
GetRangeExtent(_In_ ITfRange *pRange, _Out_ LONG *pStart, _Out_ LONG *pEnd)
{
    ITfRangeACP *pRangeACP = NULL;
    LONG acp = 0, cch = 0;
    HRESULT hr = pRange->QueryInterface(IID_ITfRangeACP, (void **)&pRangeACP);
    if (FAILED(hr))
        return hr;
    hr = pRangeACP->GetExtent(&acp, &cch);
    pRangeACP->Release();
    if (FAILED(hr))
        return hr;
    *pStart = acp;
    *pEnd = acp + cch;
    return S_OK;
}

static BYTE
ImmAttrFromDisplayAttribute(_In_ TF_DA_ATTR_INFO bAttr)
{
    switch (bAttr)
    {
        case TF_ATTR_TARGET_CONVERTED:      return ATTR_TARGET_CONVERTED;
        case TF_ATTR_CONVERTED:             return ATTR_CONVERTED;
        case TF_ATTR_TARGET_NOTCONVERTED:   return ATTR_TARGET_NOTCONVERTED;
        case TF_ATTR_INPUT_ERROR:           return ATTR_INPUT_ERROR;
        case TF_ATTR_FIXEDCONVERTED:        return ATTR_FIXEDCONVERTED;
        default:                            return ATTR_INPUT;
    }
}

static BOOL
GenerateImeMessages(_In_ HIMC hIMC, _In_reads_(cMsgs) const TRANSMSG *pMsgs, _In_ DWORD cMsgs)
{
    if (!cMsgs)
        return TRUE;

    CicIMCLock imcLock(hIMC);
    if (FAILED(imcLock.m_hr))
        return FALSE;

    DWORD cOld = imcLock.get().dwNumMsgBuf;
    HIMCC hMsgBuf = ImmReSizeIMCC(imcLock.get().hMsgBuf, (cOld + cMsgs) * sizeof(TRANSMSG));
    if (!hMsgBuf)
        return FALSE;
    imcLock.get().hMsgBuf = hMsgBuf;

    {
        CicIMCCLock<TRANSMSG> msgLock(hMsgBuf);
        if (FAILED(msgLock.m_hr))
            return FALSE;
        CopyMemory(&(&msgLock.get())[cOld], pMsgs, cMsgs * sizeof(TRANSMSG));
    }
    imcLock.get().dwNumMsgBuf = cOld + cMsgs;
    imcLock.unlock();

    return ImmGenerateMessage(hIMC);
}

static BOOL
StoreCompositionString(
    _In_ HIMC hIMC,
    _In_reads_(cchComp) LPCWSTR pszComp,
    _In_ DWORD cchComp,
    _In_reads_(cchComp) const BYTE *pAttr,
    _In_reads_(cClause) const DWORD *pClause,
    _In_ DWORD cClause,
    _In_ DWORD dwCursor,
    _In_reads_(cchResult) LPCWSTR pszResult,
    _In_ DWORD cchResult)
{
    SIZE_T cbNeeded = sizeof(COMPOSITIONSTRING);
    if (cchComp)
        cbNeeded += cClause * sizeof(DWORD) + cchComp * sizeof(WCHAR) + cchComp;
    if (cchResult)
        cbNeeded += 2 * sizeof(DWORD) + cchResult * sizeof(WCHAR);
    cbNeeded = (cbNeeded + sizeof(DWORD) - 1) & ~(sizeof(DWORD) - 1);

    CicIMCLock imcLock(hIMC);
    if (FAILED(imcLock.m_hr))
        return FALSE;

    HIMCC hCompStr = ImmReSizeIMCC(imcLock.get().hCompStr, (DWORD)cbNeeded);
    if (!hCompStr)
        return FALSE;
    imcLock.get().hCompStr = hCompStr;

    CicIMCCLock<COMPOSITIONSTRING> compLock(hCompStr);
    if (FAILED(compLock.m_hr))
        return FALSE;

    COMPOSITIONSTRING *pCS = &compLock.get();
    LPBYTE pb = (LPBYTE)pCS;
    ZeroMemory(pCS, cbNeeded);
    pCS->dwSize = (DWORD)cbNeeded;
    DWORD dwOffset = sizeof(*pCS);

    if (cchComp)
    {
        pCS->dwCompClauseLen = cClause * sizeof(DWORD);
        pCS->dwCompClauseOffset = dwOffset;
        CopyMemory(pb + dwOffset, pClause, pCS->dwCompClauseLen);
        dwOffset += pCS->dwCompClauseLen;

        pCS->dwCompStrLen = cchComp;
        pCS->dwCompStrOffset = dwOffset;
        CopyMemory(pb + dwOffset, pszComp, cchComp * sizeof(WCHAR));
        dwOffset += cchComp * sizeof(WCHAR);

        pCS->dwCompAttrLen = cchComp;
        pCS->dwCompAttrOffset = dwOffset;
        CopyMemory(pb + dwOffset, pAttr, cchComp);
        dwOffset += cchComp;

        pCS->dwCursorPos = dwCursor;
    }

    if (cchResult)
    {
        DWORD *pResultClause = (DWORD *)(pb + dwOffset);
        pCS->dwResultClauseLen = 2 * sizeof(DWORD);
        pCS->dwResultClauseOffset = dwOffset;
        pResultClause[0] = 0;
        pResultClause[1] = cchResult;
        dwOffset += pCS->dwResultClauseLen;

        pCS->dwResultStrLen = cchResult;
        pCS->dwResultStrOffset = dwOffset;
        CopyMemory(pb + dwOffset, pszResult, cchResult * sizeof(WCHAR));
    }

    return TRUE;
}

CicInputContext::~CicInputContext()
{
    DetachSinks();
    cicMemFree(m_pszLastComp);
    cicMemFree(m_pLastAttr);
}

HRESULT CicInputContext::AttachSinks()
{
    if (!m_pContext)
        return E_UNEXPECTED;

    if (!m_pInputContextOwner)
    {
        m_pInputContextOwner = new(cicNoThrow) CInputContextOwner(CicInputContext::OwnerCallback, this);
        if (!m_pInputContextOwner)
            return E_OUTOFMEMORY;
        HRESULT hr = m_pInputContextOwner->_Advise(m_pContext);
        if (FAILED(hr))
        {
            m_pInputContextOwner->Release();
            m_pInputContextOwner = NULL;
            return hr;
        }
    }

    if (!m_pEditSink)
    {
        m_pEditSink = new(cicNoThrow) CicEditSink(this);
        if (!m_pEditSink)
            return E_OUTOFMEMORY;
        HRESULT hr = m_pEditSink->Advise(m_pContext);
        if (FAILED(hr))
        {
            m_pEditSink->Release();
            m_pEditSink = NULL;
            return hr;
        }
    }

    if (!m_pCategoryMgr)
        TF_CreateCategoryMgr(&m_pCategoryMgr);
    if (!m_pDisplayAttributeMgr)
        TF_CreateDisplayAttributeMgr(&m_pDisplayAttributeMgr);

    return S_OK;
}

void CicInputContext::DetachSinks()
{
    if (m_pEditSink)
    {
        m_pEditSink->Unadvise();
        m_pEditSink->Release();
        m_pEditSink = NULL;
    }
    if (m_pInputContextOwner)
    {
        m_pInputContextOwner->_Unadvise();
        m_pInputContextOwner->Release();
        m_pInputContextOwner = NULL;
    }
    if (m_pDisplayAttributeMgr)
    {
        m_pDisplayAttributeMgr->Release();
        m_pDisplayAttributeMgr = NULL;
    }
    if (m_pCategoryMgr)
    {
        m_pCategoryMgr->Release();
        m_pCategoryMgr = NULL;
    }
}

static BYTE
LookupImmAttr(_In_opt_ ITfCategoryMgr *pCategoryMgr, _In_opt_ ITfDisplayAttributeMgr *pDisplayAttributeMgr,
              _In_ TfGuidAtom atom)
{
    GUID guid;
    if (!pCategoryMgr || !pDisplayAttributeMgr || FAILED(pCategoryMgr->GetGUID(atom, &guid)))
        return ATTR_INPUT;

    ITfDisplayAttributeInfo *pInfo = NULL;
    CLSID clsidOwner;
    if (FAILED(pDisplayAttributeMgr->GetDisplayAttributeInfo(guid, &pInfo, &clsidOwner)) || !pInfo)
        return ATTR_INPUT;

    TF_DISPLAYATTRIBUTE da;
    BYTE bAttr = ATTR_INPUT;
    if (SUCCEEDED(pInfo->GetAttributeInfo(&da)))
        bAttr = ImmAttrFromDisplayAttribute(da.bAttr);
    pInfo->Release();
    return bAttr;
}

HRESULT CicInputContext::OnEndEdit(_In_ TfEditCookie ec)
{
    if (!m_pContext)
        return S_OK;

    LONG compStart = -1, compEnd = -1;
    ITfContextComposition *pContextComposition = NULL;
    if (SUCCEEDED(m_pContext->QueryInterface(IID_ITfContextComposition, (void **)&pContextComposition)))
    {
        IEnumITfCompositionView *pEnum = NULL;
        if (SUCCEEDED(pContextComposition->EnumCompositions(&pEnum)) && pEnum)
        {
            ITfCompositionView *pView = NULL;
            if (pEnum->Next(1, &pView, NULL) == S_OK)
            {
                ITfRange *pRange = NULL;
                if (SUCCEEDED(pView->GetRange(&pRange)))
                {
                    GetRangeExtent(pRange, &compStart, &compEnd);
                    pRange->Release();
                }
                pView->Release();
            }
            pEnum->Release();
        }
        pContextComposition->Release();
    }

    ITfRange *pAll = NULL, *pEnd = NULL;
    HRESULT hr = m_pContext->GetStart(ec, &pAll);
    if (SUCCEEDED(hr))
        hr = m_pContext->GetEnd(ec, &pEnd);
    if (SUCCEEDED(hr))
        hr = pAll->ShiftEndToRange(ec, pEnd, TF_ANCHOR_END);
    LONG docStart = 0, docEnd = 0;
    if (SUCCEEDED(hr))
        hr = GetRangeExtent(pAll, &docStart, &docEnd);
    if (pEnd)
        pEnd->Release();
    if (FAILED(hr))
    {
        if (pAll)
            pAll->Release();
        return hr;
    }

    LPWSTR pszText = (LPWSTR)cicMemAllocClear((docEnd + 1) * sizeof(WCHAR));
    if (!pszText)
    {
        pAll->Release();
        return E_OUTOFMEMORY;
    }
    ULONG cchText = 0;
    if (docEnd)
        pAll->GetText(ec, 0, pszText, docEnd, &cchText);
    pAll->Release();
    docEnd = (LONG)cchText;

    BOOL bComposing = (compStart >= 0);
    if (bComposing)
    {
        compStart = min(max(compStart, 0), docEnd);
        compEnd = min(max(compEnd, compStart), docEnd);
    }

    LONG committedStart = min(m_lCommitted, docEnd);
    LONG committedEnd = bComposing ? max(committedStart, compStart) : docEnd;
    DWORD cchResult = committedEnd - committedStart;
    if (!bComposing && m_fCancelComposition)
    {
        cchResult = 0;
        m_fCancelComposition = FALSE;
    }
    m_lCommitted = committedEnd;

    DWORD cchComp = bComposing ? (DWORD)(compEnd - compStart) : 0;
    LPBYTE pAttr = (LPBYTE)cicMemAlloc(cchComp + 1);
    DWORD *pClause = (DWORD *)cicMemAlloc((cchComp + 2) * sizeof(DWORD));
    LPBYTE pBoundary = (LPBYTE)cicMemAllocClear(cchComp + 1);
    if (!pAttr || !pClause || !pBoundary)
    {
        cicMemFree(pszText);
        cicMemFree(pAttr);
        cicMemFree(pClause);
        cicMemFree(pBoundary);
        return E_OUTOFMEMORY;
    }
    FillMemory(pAttr, cchComp, ATTR_INPUT);

    DWORD dwCursor = cchComp;
    if (cchComp)
    {
        ITfProperty *pProp = NULL;
        if (SUCCEEDED(m_pContext->GetProperty(GUID_PROP_ATTRIBUTE, &pProp)))
        {
            IEnumTfRanges *pRanges = NULL;
            if (SUCCEEDED(pProp->EnumRanges(ec, &pRanges, NULL)) && pRanges)
            {
                ITfRange *pRange = NULL;
                while (pRanges->Next(1, &pRange, NULL) == S_OK)
                {
                    LONG start, end;
                    VARIANT var;
                    VariantInit(&var);
                    if (SUCCEEDED(GetRangeExtent(pRange, &start, &end)) &&
                        SUCCEEDED(pProp->GetValue(ec, pRange, &var)) && V_VT(&var) == VT_I4)
                    {
                        start = max(start, compStart) - compStart;
                        end = min(end, compEnd) - compStart;
                        if (start < end)
                        {
                            BYTE bAttr = LookupImmAttr(m_pCategoryMgr, m_pDisplayAttributeMgr, V_I4(&var));
                            FillMemory(pAttr + start, end - start, bAttr);
                            pBoundary[start] = pBoundary[end] = TRUE;
                        }
                    }
                    VariantClear(&var);
                    pRange->Release();
                }
                pRanges->Release();
            }
            pProp->Release();
        }

        TF_SELECTION sel;
        ULONG cFetched = 0;
        if (SUCCEEDED(m_pContext->GetSelection(ec, TF_DEFAULT_SELECTION, 1, &sel, &cFetched)) && cFetched)
        {
            LONG selStart, selEnd;
            if (SUCCEEDED(GetRangeExtent(sel.range, &selStart, &selEnd)))
            {
                LONG pos = (sel.style.ase == TF_AE_START) ? selStart : selEnd;
                dwCursor = (DWORD)(min(max(pos, compStart), compEnd) - compStart);
            }
            sel.range->Release();
        }
    }

    DWORD cClause = 0;
    pClause[cClause++] = 0;
    for (DWORD i = 1; i < cchComp; ++i)
    {
        if (pBoundary[i] || pAttr[i] != pAttr[i - 1])
            pClause[cClause++] = i;
    }
    if (cchComp)
        pClause[cClause++] = cchComp;

    BOOL bChanged = (cchResult != 0) || (cchComp != m_cchLastComp) || (dwCursor != m_dwLastCursor) ||
                    (cchComp && (memcmp(m_pszLastComp, pszText + compStart, cchComp * sizeof(WCHAR)) ||
                                 memcmp(m_pLastAttr, pAttr, cchComp)));
    BOOL bHadComp = m_fInComposition;

    if (bChanged)
    {
        StoreCompositionString(m_hIMC, pszText + compStart, cchComp, pAttr, pClause, cClause, dwCursor,
                               pszText + committedStart, cchResult);

        TRANSMSG msgs[3];
        DWORD cMsgs = 0;
        if (!bHadComp && (cchComp || cchResult))
        {
            msgs[cMsgs].message = WM_IME_STARTCOMPOSITION;
            msgs[cMsgs].wParam = 0;
            msgs[cMsgs].lParam = 0;
            ++cMsgs;
        }

        LPARAM lParam = 0;
        WPARAM wParam = 0;
        if (cchResult)
        {
            lParam |= GCS_RESULTSTR | GCS_RESULTCLAUSE;
            wParam = pszText[committedStart];
        }
        if (cchComp)
        {
            lParam |= GCS_COMPSTR | GCS_COMPATTR | GCS_COMPCLAUSE | GCS_CURSORPOS | GCS_DELTASTART;
            if (!cchResult && cchComp)
                wParam = pszText[compStart];
        }
        if (lParam)
        {
            msgs[cMsgs].message = WM_IME_COMPOSITION;
            msgs[cMsgs].wParam = wParam;
            msgs[cMsgs].lParam = lParam;
            ++cMsgs;
        }

        if ((bHadComp || cchResult) && !cchComp)
        {
            msgs[cMsgs].message = WM_IME_ENDCOMPOSITION;
            msgs[cMsgs].wParam = 0;
            msgs[cMsgs].lParam = 0;
            ++cMsgs;
        }

        m_fInComposition = (cchComp != 0);
        cicMemFree(m_pszLastComp);
        cicMemFree(m_pLastAttr);
        m_pszLastComp = NULL;
        m_pLastAttr = NULL;
        m_cchLastComp = 0;
        if (cchComp)
        {
            m_pszLastComp = (LPWSTR)cicMemAlloc(cchComp * sizeof(WCHAR));
            m_pLastAttr = (LPBYTE)cicMemAlloc(cchComp);
            if (m_pszLastComp && m_pLastAttr)
            {
                CopyMemory(m_pszLastComp, pszText + compStart, cchComp * sizeof(WCHAR));
                CopyMemory(m_pLastAttr, pAttr, cchComp);
                m_cchLastComp = cchComp;
            }
        }
        m_dwLastCursor = dwCursor;

        GenerateImeMessages(m_hIMC, msgs, cMsgs);
    }

    cicMemFree(pszText);
    cicMemFree(pAttr);
    cicMemFree(pClause);
    cicMemFree(pBoundary);

    if (m_lCommitted > 0 && !bComposing)
        RemoveCommittedText();

    return S_OK;
}

static HRESULT
RemoveCommittedSession(CicInputContext *pIC, TfEditCookie ec)
{
    return pIC->DoRemoveCommitted(ec);
}

static HRESULT
SyncCompositionSession(CicInputContext *pIC, TfEditCookie ec)
{
    return pIC->OnEndEdit(ec);
}

HRESULT CicInputContext::RemoveCommittedText()
{
    if (!m_pContext)
        return E_UNEXPECTED;
    CicFnEditSession *pSession = new(cicNoThrow) CicFnEditSession(this, RemoveCommittedSession);
    if (!pSession)
        return E_OUTOFMEMORY;
    HRESULT hrSession;
    HRESULT hr = m_pContext->RequestEditSession(m_clientId, pSession, TF_ES_ASYNC | TF_ES_READWRITE, &hrSession);
    pSession->Release();
    return hr;
}

HRESULT CicInputContext::DoRemoveCommitted(_In_ TfEditCookie ec)
{
    if (!m_pContext || m_lCommitted <= 0)
        return S_OK;

    LONG limit = m_lCommitted;
    ITfContextComposition *pContextComposition = NULL;
    if (SUCCEEDED(m_pContext->QueryInterface(IID_ITfContextComposition, (void **)&pContextComposition)))
    {
        IEnumITfCompositionView *pEnum = NULL;
        if (SUCCEEDED(pContextComposition->EnumCompositions(&pEnum)) && pEnum)
        {
            ITfCompositionView *pView = NULL;
            if (pEnum->Next(1, &pView, NULL) == S_OK)
            {
                ITfRange *pRange = NULL;
                LONG start, end;
                if (SUCCEEDED(pView->GetRange(&pRange)) && SUCCEEDED(GetRangeExtent(pRange, &start, &end)))
                    limit = min(limit, start);
                if (pRange)
                    pRange->Release();
                pView->Release();
            }
            pEnum->Release();
        }
        pContextComposition->Release();
    }
    if (limit <= 0)
        return S_OK;

    ITfRange *pRange = NULL;
    HRESULT hr = m_pContext->GetStart(ec, &pRange);
    if (FAILED(hr))
        return hr;
    LONG cch = 0;
    hr = pRange->ShiftEnd(ec, limit, &cch, NULL);
    if (SUCCEEDED(hr) && cch > 0)
    {
        hr = pRange->SetText(ec, 0, L"", 0);
        if (SUCCEEDED(hr))
            m_lCommitted = max(m_lCommitted - cch, 0);
    }
    pRange->Release();
    return hr;
}

HRESULT CicInputContext::SyncComposition()
{
    if (!m_pContext)
        return E_UNEXPECTED;
    CicFnEditSession *pSession = new(cicNoThrow) CicFnEditSession(this, SyncCompositionSession);
    if (!pSession)
        return E_OUTOFMEMORY;
    HRESULT hrSession;
    HRESULT hr = m_pContext->RequestEditSession(m_clientId, pSession, TF_ES_ASYNCDONTCARE | TF_ES_READ, &hrSession);
    pSession->Release();
    return hr;
}

HRESULT CicInputContext::TerminateComposition(_In_ BOOL bCancel)
{
    if (!m_pContext)
        return E_UNEXPECTED;
    if (!m_fInComposition)
        return S_OK;

    ITfContextOwnerCompositionServices *pServices = NULL;
    HRESULT hr = m_pContext->QueryInterface(IID_ITfContextOwnerCompositionServices, (void **)&pServices);
    if (FAILED(hr))
        return hr;
    m_fCancelComposition = bCancel;
    hr = pServices->TerminateComposition(NULL);
    pServices->Release();
    if (FAILED(hr))
        m_fCancelComposition = FALSE;
    SyncComposition();
    return hr;
}

HRESULT CALLBACK CicInputContext::OwnerCallback(UINT uType, LPVOID args, LPVOID param)
{
    return static_cast<CicInputContext *>(param)->OnOwnerCallback(uType, args);
}

HRESULT CicInputContext::GetTextExt(_Out_ RECT *prc, _Out_ BOOL *pfClipped)
{
    *pfClipped = FALSE;
    SetRectEmpty(prc);

    if (CCompositionWindow::GetVisibleRect(prc))
        return S_OK;

    CicIMCLock imcLock(m_hIMC);
    if (FAILED(imcLock.m_hr))
        return imcLock.m_hr;

    HWND hWnd = imcLock.get().hWnd;
    POINT pt = { 0, 0 };
    LONG height = 0;

    if (imcLock.get().lfFont.W.lfFaceName[0] || imcLock.get().lfFont.W.lfHeight)
    {
        HFONT hFont = CreateFontIndirectW(&imcLock.get().lfFont.W);
        HDC hDC = GetDC(hWnd);
        if (hFont && hDC)
        {
            TEXTMETRICW tm;
            HGDIOBJ hOldFont = SelectObject(hDC, hFont);
            if (GetTextMetricsW(hDC, &tm))
                height = tm.tmHeight;
            SelectObject(hDC, hOldFont);
        }
        if (hDC)
            ReleaseDC(hWnd, hDC);
        if (hFont)
            DeleteObject(hFont);
    }

    if (imcLock.get().cfCompForm.dwStyle & (CFS_POINT | CFS_FORCE_POSITION | CFS_RECT))
    {
        pt = imcLock.get().cfCompForm.ptCurrentPos;
    }
    else if (imcLock.get().cfCandForm[0].dwIndex == 0 &&
             (imcLock.get().cfCandForm[0].dwStyle & (CFS_CANDIDATEPOS | CFS_EXCLUDE)))
    {
        pt = imcLock.get().cfCandForm[0].ptCurrentPos;
    }
    else if (!GetCaretPos(&pt))
    {
        pt.x = pt.y = 0;
    }

    if (!height)
        height = GetSystemMetrics(SM_CYMENU);

    if (hWnd)
        ClientToScreen(hWnd, &pt);

    prc->left = pt.x;
    prc->top = pt.y;
    prc->right = pt.x + 1;
    prc->bottom = pt.y + height;
    return S_OK;
}

HRESULT CicInputContext::OnOwnerCallback(_In_ UINT uType, _Inout_ LPVOID args)
{
    switch (uType)
    {
        case 1:
        {
            TEXT_EXT_ARGS *pArgs = (TEXT_EXT_ARGS *)args;
            return GetTextExt(pArgs->prc, pArgs->pfClipped);
        }
        case 2:
        {
            RECT *prc = *(RECT **)args;
            CicIMCLock imcLock(m_hIMC);
            if (FAILED(imcLock.m_hr))
                return imcLock.m_hr;
            if (!imcLock.get().hWnd || !GetWindowRect(imcLock.get().hWnd, prc))
                SetRectEmpty(prc);
            return S_OK;
        }
        case 6:
        {
            TF_STATUS *pStatus = *(TF_STATUS **)args;
            ZeroMemory(pStatus, sizeof(*pStatus));
            return S_OK;
        }
        case 7:
        {
            HWND *phWnd = *(HWND **)args;
            CicIMCLock imcLock(m_hIMC);
            if (FAILED(imcLock.m_hr))
                return imcLock.m_hr;
            *phWnd = imcLock.get().hWnd;
            return S_OK;
        }
        default:
            return E_NOTIMPL;
    }
}
