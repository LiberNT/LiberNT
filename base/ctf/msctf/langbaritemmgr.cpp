/*
 * PROJECT:     LiberNT CTF
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     ITfLangBarItemMgr implementation
 * COPYRIGHT:   Copyright 2026 mikuta0407 <mikuta0407@mikuta0407.net>
 */

#include "precomp.h"
#include "resource.h"
#include <windowsx.h>
#include <ime/indicml.h>
#include <inputindicator.h>

extern HINSTANCE g_hInst;


struct LangBarItemEntry
{
    ITfLangBarItem *item;
    TF_LANGBARITEMINFO info;
};

class CLangBarItemSinkProxy final : public ITfLangBarItemSink
{
public:
    CLangBarItemSinkProxy(ITfLangBarItemSink *pTarget) : m_cRefs(1), m_pTarget(pTarget) { m_pTarget->AddRef(); }

    STDMETHODIMP QueryInterface(REFIID riid, void **ppvObj) override
    {
        if (!ppvObj)
            return E_POINTER;
        if (riid == IID_IUnknown || riid == IID_ITfLangBarItemSink)
        {
            *ppvObj = static_cast<ITfLangBarItemSink *>(this);
            AddRef();
            return S_OK;
        }
        *ppvObj = NULL;
        return E_NOINTERFACE;
    }
    STDMETHODIMP_(ULONG) AddRef() override { return InterlockedIncrement(&m_cRefs); }
    STDMETHODIMP_(ULONG) Release() override
    {
        ULONG refs = InterlockedDecrement(&m_cRefs);
        if (!refs)
        {
            Disconnect();
            delete this;
        }
        return refs;
    }
    STDMETHODIMP OnUpdate(DWORD dwFlags) override
    {
        return m_pTarget ? m_pTarget->OnUpdate(dwFlags) : S_OK;
    }

    void Disconnect()
    {
        if (m_pTarget)
        {
            m_pTarget->Release();
            m_pTarget = NULL;
        }
    }

private:
    LONG m_cRefs;
    ITfLangBarItemSink *m_pTarget;
};

struct LangBarItemSinkEntry
{
    DWORD cookie;
    ITfLangBarItem *item;
    ITfSource *source;
    DWORD sourceCookie;
    CLangBarItemSinkProxy *proxy;
};

class CEnumLangBarItems final : public IEnumTfLangBarItems
{
public:
    CEnumLangBarItems() : m_refs(1), m_items(NULL), m_count(0), m_index(0) { }

    HRESULT Init(const LangBarItemEntry *entries, ULONG count)
    {
        if (!count)
            return S_OK;
        m_items = static_cast<ITfLangBarItem **>(cicMemAlloc(count * sizeof(*m_items)));
        if (!m_items)
            return E_OUTOFMEMORY;
        for (ULONG i = 0; i < count; ++i)
        {
            m_items[i] = entries[i].item;
            m_items[i]->AddRef();
        }
        m_count = count;
        return S_OK;
    }

    STDMETHODIMP QueryInterface(REFIID riid, void **ppvObj) override
    {
        if (!ppvObj)
            return E_POINTER;
        *ppvObj = NULL;
        if (riid != IID_IUnknown && riid != IID_IEnumTfLangBarItems)
            return E_NOINTERFACE;
        *ppvObj = static_cast<IEnumTfLangBarItems *>(this);
        AddRef();
        return S_OK;
    }
    STDMETHODIMP_(ULONG) AddRef() override { return InterlockedIncrement(&m_refs); }
    STDMETHODIMP_(ULONG) Release() override
    {
        ULONG refs = InterlockedDecrement(&m_refs);
        if (!refs)
            delete this;
        return refs;
    }

    STDMETHODIMP Clone(IEnumTfLangBarItems **ppEnum) override
    {
        if (!ppEnum)
            return E_INVALIDARG;
        *ppEnum = NULL;
        CEnumLangBarItems *pEnum = new(cicNoThrow) CEnumLangBarItems();
        if (!pEnum)
            return E_OUTOFMEMORY;
        if (m_count)
        {
            pEnum->m_items = static_cast<ITfLangBarItem **>(cicMemAlloc(m_count * sizeof(*m_items)));
            if (!pEnum->m_items)
            {
                pEnum->Release();
                return E_OUTOFMEMORY;
            }
            for (ULONG i = 0; i < m_count; ++i)
            {
                pEnum->m_items[i] = m_items[i];
                m_items[i]->AddRef();
            }
            pEnum->m_count = m_count;
        }
        pEnum->m_index = m_index;
        *ppEnum = pEnum;
        return S_OK;
    }
    STDMETHODIMP Next(ULONG ulCount, ITfLangBarItem **ppItem, ULONG *pcFetched) override
    {
        if (!ppItem || (ulCount > 1 && !pcFetched))
            return E_INVALIDARG;
        ULONG fetched = 0;
        while (fetched < ulCount && m_index < m_count)
        {
            ppItem[fetched] = m_items[m_index++];
            ppItem[fetched]->AddRef();
            ++fetched;
        }
        if (pcFetched)
            *pcFetched = fetched;
        return (fetched == ulCount) ? S_OK : S_FALSE;
    }
    STDMETHODIMP Reset() override
    {
        m_index = 0;
        return S_OK;
    }
    STDMETHODIMP Skip(ULONG ulCount) override
    {
        if (ulCount > m_count - m_index)
        {
            m_index = m_count;
            return S_FALSE;
        }
        m_index += ulCount;
        return S_OK;
    }

private:
    ~CEnumLangBarItems()
    {
        for (ULONG i = 0; i < m_count; ++i)
            m_items[i]->Release();
        cicMemFree(m_items);
    }

    LONG m_refs;
    ITfLangBarItem **m_items;
    ULONG m_count;
    ULONG m_index;
};

DEFINE_GUID(GUID_LBI_SYSTEM_HELP, 0xED9D5450, 0xEBE6, 0x4255, 0x82, 0x89, 0xF8, 0xA3, 0x1E, 0x68, 0x72, 0x28);
DEFINE_GUID(CLSID_SYSTEM_HELP_SERVICE, 0x5D997390, 0x8320, 0x400A, 0x88, 0xE5, 0x35, 0xB0, 0xC1, 0x20, 0xF9, 0x0B);
DEFINE_GUID(GUID_LBI_SYSTEM_CORRECTION, 0x190F67A2, 0x024F, 0x40D9, 0xAA, 0xBA, 0x43, 0x9F, 0x5C, 0x80, 0x8F, 0x67);

class CSystemLangBarItem final
    : public ITfLangBarItemButton
    , public ITfSource
    , public ITfSystemLangBarItem
    , public ITfSystemDeviceTypeLangBarItem
    , public ITfSystemLangBarItemText
{
public:
    CSystemLangBarItem(REFCLSID rclsidService, REFGUID rguidItem, DWORD dwStyle, ULONG ulSort, UINT uDescId,
                       BOOL fSystem)
        : m_refs(1), m_fSystem(fSystem), m_dwStatus(0), m_dwIconMode(0), m_hIcon(NULL), m_sinks(NULL), m_cSinks(0)
    {
        ZeroMemory(&m_info, sizeof(m_info));
        m_info.clsidService = rclsidService;
        m_info.guidItem = rguidItem;
        m_info.dwStyle = dwStyle;
        m_info.ulSort = ulSort;
        LoadStringW(g_hInst, uDescId, m_info.szDescription, _countof(m_info.szDescription));
        m_szText[0] = UNICODE_NULL;
        m_szTooltip[0] = UNICODE_NULL;
    }

    STDMETHODIMP QueryInterface(REFIID riid, void **ppvObj) override
    {
        if (!ppvObj)
            return E_POINTER;
        *ppvObj = NULL;
        if (riid == IID_IUnknown || riid == IID_ITfLangBarItem || riid == IID_ITfLangBarItemButton)
            *ppvObj = static_cast<ITfLangBarItemButton *>(this);
        else if (riid == IID_ITfSource)
            *ppvObj = static_cast<ITfSource *>(this);
        else if (m_fSystem && riid == IID_ITfSystemLangBarItem)
            *ppvObj = static_cast<ITfSystemLangBarItem *>(this);
        else if (m_fSystem && riid == IID_ITfSystemDeviceTypeLangBarItem)
            *ppvObj = static_cast<ITfSystemDeviceTypeLangBarItem *>(this);
        else if (m_fSystem && riid == IID_ITfSystemLangBarItemText)
            *ppvObj = static_cast<ITfSystemLangBarItemText *>(this);
        else
            return E_NOINTERFACE;
        AddRef();
        return S_OK;
    }
    STDMETHODIMP_(ULONG) AddRef() override { return InterlockedIncrement(&m_refs); }
    STDMETHODIMP_(ULONG) Release() override
    {
        ULONG refs = InterlockedDecrement(&m_refs);
        if (!refs)
            delete this;
        return refs;
    }

    STDMETHODIMP GetInfo(TF_LANGBARITEMINFO *pInfo) override
    {
        if (!pInfo)
            return E_INVALIDARG;
        *pInfo = m_info;
        return S_OK;
    }
    STDMETHODIMP GetStatus(DWORD *pdwStatus) override
    {
        if (!pdwStatus)
            return E_INVALIDARG;
        *pdwStatus = m_dwStatus;
        return S_OK;
    }
    STDMETHODIMP Show(BOOL fShow) override
    {
        m_dwStatus = fShow ? (m_dwStatus & ~TF_LBI_STATUS_HIDDEN) : (m_dwStatus | TF_LBI_STATUS_HIDDEN);
        return S_OK;
    }
    STDMETHODIMP GetTooltipString(BSTR *pbstrToolTip) override
    {
        return AllocString(m_szTooltip, pbstrToolTip);
    }

    STDMETHODIMP OnClick(TfLBIClick click, POINT pt, const RECT *prcArea) override { return S_OK; }
    STDMETHODIMP InitMenu(ITfMenu *pMenu) override
    {
        for (ULONG i = 0; i < m_cSinks; ++i)
            m_sinks[i].sink->InitMenu(pMenu);
        return S_OK;
    }
    STDMETHODIMP OnMenuSelect(UINT wID) override { return m_fSystem ? E_FAIL : S_OK; }
    STDMETHODIMP GetIcon(HICON *phIcon) override
    {
        if (!phIcon)
            return E_INVALIDARG;
        *phIcon = NULL;
        if (m_hIcon && !(m_dwIconMode & TF_DTLBI_USEPROFILEICON))
            *phIcon = CopyIcon(m_hIcon);
        return S_OK;
    }
    STDMETHODIMP GetText(BSTR *pbstrText) override
    {
        return AllocString(m_szText, pbstrText);
    }

    STDMETHODIMP AdviseSink(REFIID riid, IUnknown *punk, DWORD *pdwCookie) override
    {
        if (!punk || !pdwCookie)
            return E_INVALIDARG;
        if (!m_fSystem)
            return CONNECT_E_CANNOTCONNECT;
        *pdwCookie = (DWORD)-1;
        if (riid != IID_ITfSystemLangBarItemSink)
            return CONNECT_E_CANNOTCONNECT;
        ITfSystemLangBarItemSink *sink = NULL;
        HRESULT hr = punk->QueryInterface(IID_ITfSystemLangBarItemSink, (void **)&sink);
        if (FAILED(hr))
            return CONNECT_E_CANNOTCONNECT;
        SinkEntry *sinks = static_cast<SinkEntry *>(cicMemReAlloc(m_sinks, (m_cSinks + 1) * sizeof(*sinks)));
        if (!sinks)
        {
            sink->Release();
            return E_OUTOFMEMORY;
        }
        m_sinks = sinks;
        DWORD cookie = 1;
        while (FindSink(cookie) >= 0)
            ++cookie;
        m_sinks[m_cSinks].cookie = cookie;
        m_sinks[m_cSinks].sink = sink;
        *pdwCookie = cookie;
        ++m_cSinks;
        return S_OK;
    }
    STDMETHODIMP UnadviseSink(DWORD dwCookie) override
    {
        if (!m_fSystem)
            return E_FAIL;
        if (dwCookie == (DWORD)-1)
            return E_INVALIDARG;
        LONG i = FindSink(dwCookie);
        if (i < 0)
            return CONNECT_E_NOCONNECTION;
        m_sinks[i].sink->Release();
        MoveMemory(&m_sinks[i], &m_sinks[i + 1], (m_cSinks - i - 1) * sizeof(*m_sinks));
        --m_cSinks;
        return S_OK;
    }

    STDMETHODIMP SetIcon(HICON hIcon) override
    {
        HICON hCopy = NULL;
        if (hIcon)
        {
            hCopy = CopyIcon(hIcon);
            if (!hCopy)
                return E_FAIL;
        }
        if (m_hIcon)
            DestroyIcon(m_hIcon);
        m_hIcon = hCopy;
        return S_OK;
    }
    STDMETHODIMP SetTooltipString(WCHAR *pchToolTip, ULONG cch) override
    {
        return CopyText(m_szTooltip, _countof(m_szTooltip), pchToolTip, cch);
    }

    STDMETHODIMP SetIconMode(DWORD dwFlags) override
    {
        m_dwIconMode = dwFlags;
        return S_OK;
    }
    STDMETHODIMP GetIconMode(DWORD *pdwFlags) override
    {
        if (!pdwFlags)
            return E_INVALIDARG;
        *pdwFlags = m_dwIconMode;
        return S_OK;
    }

    STDMETHODIMP SetItemText(const WCHAR *pch, ULONG cch) override
    {
        return CopyText(m_szText, _countof(m_szText), pch, cch);
    }
    STDMETHODIMP GetItemText(BSTR *pbstrText) override
    {
        return AllocString(m_szText, pbstrText);
    }

private:
    struct SinkEntry
    {
        DWORD cookie;
        ITfSystemLangBarItemSink *sink;
    };

    ~CSystemLangBarItem()
    {
        if (m_hIcon)
            DestroyIcon(m_hIcon);
        for (ULONG i = 0; i < m_cSinks; ++i)
            m_sinks[i].sink->Release();
        cicMemFree(m_sinks);
    }

    LONG FindSink(DWORD dwCookie) const
    {
        for (ULONG i = 0; i < m_cSinks; ++i)
        {
            if (m_sinks[i].cookie == dwCookie)
                return (LONG)i;
        }
        return -1;
    }

    static HRESULT AllocString(LPCWSTR psz, BSTR *pbstr)
    {
        if (!pbstr)
            return E_INVALIDARG;
        *pbstr = SysAllocString(psz);
        return *pbstr ? S_OK : E_OUTOFMEMORY;
    }

    static HRESULT CopyText(LPWSTR pszDest, ULONG cchDest, const WCHAR *pch, ULONG cch)
    {
        if (!pch && cch)
            return E_INVALIDARG;
        pszDest[0] = UNICODE_NULL;
        if (cch)
            lstrcpynW(pszDest, pch, (INT)min(cch, cchDest));
        return S_OK;
    }

    LONG m_refs;
    BOOL m_fSystem;
    TF_LANGBARITEMINFO m_info;
    DWORD m_dwStatus;
    DWORD m_dwIconMode;
    HICON m_hIcon;
    WCHAR m_szText[TF_LBI_DESC_MAXLEN];
    WCHAR m_szTooltip[MAX_PATH];
    SinkEntry *m_sinks;
    ULONG m_cSinks;
};

class CTipInputModeIndicator final : public ITfLangBarItemSink
{
public:
    static CTipInputModeIndicator *Create(ITfLangBarItem *pItem)
    {
        CTipInputModeIndicator *pThis = new(cicNoThrow) CTipInputModeIndicator();
        if (!pThis)
            return NULL;
        if (FAILED(pItem->QueryInterface(IID_ITfLangBarItemButton, (void **)&pThis->m_pButton)) ||
            !RegisterWndClass())
        {
            pThis->Release();
            return NULL;
        }
        pThis->m_hwnd = CreateWindowExW(0, INPUTINDICATOR_TIP_WNDCLASS, NULL, 0, 0, 0, 0, 0, HWND_MESSAGE,
                                        NULL, g_hInst, pThis);
        if (!pThis->m_hwnd)
        {
            pThis->Release();
            return NULL;
        }
        pThis->m_dwThreadId = GetWindowThreadProcessId(pThis->m_hwnd, NULL);
        if (SUCCEEDED(pItem->QueryInterface(IID_ITfSource, (void **)&pThis->m_pSource)) &&
            FAILED(pThis->m_pSource->AdviseSink(IID_ITfLangBarItemSink, static_cast<ITfLangBarItemSink *>(pThis),
                                                &pThis->m_dwCookie)))
        {
            pThis->m_pSource->Release();
            pThis->m_pSource = NULL;
        }
        pThis->Publish();
        return pThis;
    }

    BOOL IsFor(ITfLangBarItem *pItem) const
    {
        ITfLangBarItemButton *pButton = NULL;
        BOOL fSame = FALSE;
        if (SUCCEEDED(pItem->QueryInterface(IID_ITfLangBarItemButton, (void **)&pButton)))
        {
            fSame = (pButton == m_pButton);
            pButton->Release();
        }
        return fSame;
    }

    void Close()
    {
        if (m_pSource)
        {
            m_pSource->UnadviseSink(m_dwCookie);
            m_pSource->Release();
            m_pSource = NULL;
        }
        if (m_pButton)
        {
            m_pButton->Release();
            m_pButton = NULL;
        }
        Publish();
        if (m_hwnd)
        {
            SetWindowLongPtrW(m_hwnd, GWLP_USERDATA, 0);
            if (m_dwThreadId == GetCurrentThreadId())
                DestroyWindow(m_hwnd);
            else
                PostMessageW(m_hwnd, WM_CLOSE, 0, 0);
            m_hwnd = NULL;
        }
    }

    STDMETHODIMP QueryInterface(REFIID riid, void **ppvObj) override
    {
        if (!ppvObj)
            return E_POINTER;
        *ppvObj = NULL;
        if (riid != IID_IUnknown && riid != IID_ITfLangBarItemSink)
            return E_NOINTERFACE;
        *ppvObj = static_cast<ITfLangBarItemSink *>(this);
        AddRef();
        return S_OK;
    }
    STDMETHODIMP_(ULONG) AddRef() override { return InterlockedIncrement(&m_refs); }
    STDMETHODIMP_(ULONG) Release() override
    {
        ULONG refs = InterlockedDecrement(&m_refs);
        if (!refs)
            delete this;
        return refs;
    }

    STDMETHODIMP OnUpdate(DWORD dwFlags) override
    {
        AddRef();
        Publish();
        Release();
        return S_OK;
    }

    static void UnregisterWndClass()
    {
        if (s_fRegistered)
            UnregisterClassW(INPUTINDICATOR_TIP_WNDCLASS, g_hInst);
    }

private:
    CTipInputModeIndicator() : m_refs(1), m_hwnd(NULL), m_dwThreadId(0), m_hIconSent(NULL), m_pButton(NULL)
        , m_pSource(NULL), m_dwCookie(0), m_click(TF_LBI_CLK_LEFT), m_ptClick(), m_dwClickTime(0) { }
    ~CTipInputModeIndicator()
    {
        Close();
    }

    static BOOL RegisterWndClass()
    {
        if (s_fRegistered)
            return TRUE;
        WNDCLASSW wc = { 0 };
        wc.lpfnWndProc = WndProc;
        wc.hInstance = g_hInst;
        wc.lpszClassName = INPUTINDICATOR_TIP_WNDCLASS;
        if (!RegisterClassW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
            return FALSE;
        s_fRegistered = TRUE;
        return TRUE;
    }

    void Publish()
    {
        HWND hwndIndicator = FindWindowW(INDICATOR_CLASSW, NULL);
        HICON hIcon = NULL;
        if (hwndIndicator && m_hwnd)
        {
            INPUTINDICATOR_TIPSTATE state = { sizeof(state), m_dwThreadId, (ULONG64)(LONG_PTR)m_hwnd };
            DWORD dwStatus = 0;
            if (m_pButton && SUCCEEDED(m_pButton->GetStatus(&dwStatus)) && !(dwStatus & TF_LBI_STATUS_HIDDEN) &&
                SUCCEEDED(m_pButton->GetIcon(&hIcon)) && hIcon)
            {
                BSTR bstrTooltip = NULL;
                state.hIcon = (ULONG64)(LONG_PTR)hIcon;
                if (SUCCEEDED(m_pButton->GetTooltipString(&bstrTooltip)) && bstrTooltip)
                {
                    StringCchCopyW(state.szTooltip, _countof(state.szTooltip), bstrTooltip);
                    SysFreeString(bstrTooltip);
                }
            }

            COPYDATASTRUCT cds = { INPUTINDICATOR_COPYDATA_ID, sizeof(state), &state };
            SendMessageTimeoutW(hwndIndicator, WM_COPYDATA, (WPARAM)m_hwnd, (LPARAM)&cds, SMTO_ABORTIFHUNG, 1000,
                                NULL);
        }
        if (m_hIconSent)
            DestroyIcon(m_hIconSent);
        m_hIconSent = hIcon;
    }

    void QueueClick(TfLBIClick click, POINT pt)
    {
        m_click = click;
        m_ptClick = pt;
        m_dwClickTime = GetTickCount();
        if (GetFocus())
        {
            Click(click, pt);
            return;
        }
        SetTimer(m_hwnd, CLICK_TIMER_ID, CLICK_TIMER_MS, NULL);
    }

    void OnClickTimer()
    {
        if (!GetFocus() && GetTickCount() - m_dwClickTime < CLICK_WAIT_MS)
            return;
        KillTimer(m_hwnd, CLICK_TIMER_ID);
        Click(m_click, m_ptClick);
    }

    void Click(TfLBIClick click, POINT pt)
    {
        if (!m_pButton)
            return;
        RECT rc = { pt.x, pt.y, pt.x + 1, pt.y + 1 };
        ITfLangBarItemButton *pButton = m_pButton;
        AddRef();
        pButton->AddRef();
        pButton->OnClick(click, pt, &rc);
        pButton->Release();
        Release();
    }

    static LRESULT CALLBACK WndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
    {
        if (uMsg == WM_NCCREATE)
        {
            LPCREATESTRUCTW pcs = (LPCREATESTRUCTW)lParam;
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)pcs->lpCreateParams);
        }
        CTipInputModeIndicator *pThis = (CTipInputModeIndicator *)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
        switch (uMsg)
        {
            case WM_INPUTINDICATOR_REFRESH:
                if (pThis)
                {
                    pThis->AddRef();
                    pThis->Publish();
                    pThis->Release();
                }
                return 0;
            case WM_INPUTINDICATOR_CLICK:
                if (pThis && (wParam == INPUTINDICATOR_CLICK_LEFT || wParam == INPUTINDICATOR_CLICK_RIGHT))
                {
                    POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
                    pThis->AddRef();
                    pThis->QueueClick((wParam == INPUTINDICATOR_CLICK_RIGHT) ? TF_LBI_CLK_RIGHT : TF_LBI_CLK_LEFT, pt);
                    pThis->Release();
                }
                return 0;
            case WM_TIMER:
                if (pThis && wParam == CLICK_TIMER_ID)
                {
                    pThis->AddRef();
                    pThis->OnClickTimer();
                    pThis->Release();
                }
                return 0;
            case WM_NCDESTROY:
                SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
                break;
        }
        return DefWindowProcW(hwnd, uMsg, wParam, lParam);
    }

    enum { CLICK_TIMER_ID = 1, CLICK_TIMER_MS = 20, CLICK_WAIT_MS = 500 };

    static BOOL s_fRegistered;

    LONG m_refs;
    HWND m_hwnd;
    DWORD m_dwThreadId;
    HICON m_hIconSent;
    ITfLangBarItemButton *m_pButton;
    ITfSource *m_pSource;
    DWORD m_dwCookie;
    TfLBIClick m_click;
    POINT m_ptClick;
    DWORD m_dwClickTime;
};

BOOL CTipInputModeIndicator::s_fRegistered = FALSE;

EXTERN_C VOID TipInputModeIndicator_Uninit(VOID)
{
    CTipInputModeIndicator::UnregisterWndClass();
}

class CLangBarItemMgr;

static SRWLOCK s_LangBarItemMgrLock = SRWLOCK_INIT;
static CLangBarItemMgr *s_pLangBarItemMgrs = NULL;

class CLangBarItemMgr final : public ITfLangBarItemMgr
{
public:
    CLangBarItemMgr()
        : m_refs(1), m_items(NULL), m_cItems(0), m_sinks(NULL), m_cSinks(0), m_nextCookie(1)
        , m_dwThreadId(GetCurrentThreadId()), m_pNext(NULL), m_pIndicator(NULL)
    {
    }

    STDMETHODIMP QueryInterface(REFIID riid, void **ppvObj) override
    {
        if (!ppvObj)
            return E_POINTER;
        *ppvObj = NULL;
        if (riid != IID_IUnknown && riid != IID_ITfLangBarItemMgr)
            return E_NOINTERFACE;
        *ppvObj = static_cast<ITfLangBarItemMgr *>(this);
        AddRef();
        return S_OK;
    }
    STDMETHODIMP_(ULONG) AddRef() override { return InterlockedIncrement(&m_refs); }
    STDMETHODIMP_(ULONG) Release() override
    {
        AcquireSRWLockExclusive(&s_LangBarItemMgrLock);
        ULONG refs = InterlockedDecrement(&m_refs);
        if (!refs)
        {
            for (CLangBarItemMgr **pp = &s_pLangBarItemMgrs; *pp; pp = &(*pp)->m_pNext)
            {
                if (*pp == this)
                {
                    *pp = m_pNext;
                    break;
                }
            }
        }
        ReleaseSRWLockExclusive(&s_LangBarItemMgrLock);
        if (!refs)
            delete this;
        return refs;
    }

    static CLangBarItemMgr *FindForCurrentThread()
    {
        CLangBarItemMgr *pMgr;
        AcquireSRWLockExclusive(&s_LangBarItemMgrLock);
        for (pMgr = s_pLangBarItemMgrs; pMgr; pMgr = pMgr->m_pNext)
        {
            if (pMgr->m_dwThreadId == GetCurrentThreadId())
            {
                pMgr->AddRef();
                break;
            }
        }
        ReleaseSRWLockExclusive(&s_LangBarItemMgrLock);
        return pMgr;
    }

    void Register()
    {
        AcquireSRWLockExclusive(&s_LangBarItemMgrLock);
        m_pNext = s_pLangBarItemMgrs;
        s_pLangBarItemMgrs = this;
        ReleaseSRWLockExclusive(&s_LangBarItemMgrLock);
    }

    STDMETHODIMP EnumItems(IEnumTfLangBarItems **ppEnum) override
    {
        if (!ppEnum)
            return E_INVALIDARG;
        *ppEnum = NULL;
        CEnumLangBarItems *pEnum = new(cicNoThrow) CEnumLangBarItems();
        if (!pEnum)
            return E_OUTOFMEMORY;
        HRESULT hr = pEnum->Init(m_items, m_cItems);
        if (FAILED(hr))
        {
            pEnum->Release();
            return hr;
        }
        *ppEnum = pEnum;
        return S_OK;
    }

    STDMETHODIMP GetItem(REFGUID rguid, ITfLangBarItem **ppItem) override
    {
        if (!ppItem)
            return E_INVALIDARG;
        *ppItem = NULL;
        LONG index = Find(rguid);
        if (index < 0)
            return S_FALSE;
        *ppItem = m_items[index].item;
        (*ppItem)->AddRef();
        return S_OK;
    }

    STDMETHODIMP AddItem(ITfLangBarItem *punk) override
    {
        if (!punk)
            return E_INVALIDARG;
        TF_LANGBARITEMINFO info;
        HRESULT hr = punk->GetInfo(&info);
        if (FAILED(hr))
            return hr;
        if (Find(info.guidItem) >= 0)
            return E_FAIL;
        LangBarItemEntry *items =
            static_cast<LangBarItemEntry *>(cicMemReAlloc(m_items, (m_cItems + 1) * sizeof(*items)));
        if (!items)
            return E_OUTOFMEMORY;
        m_items = items;
        m_items[m_cItems].item = punk;
        m_items[m_cItems].info = info;
        punk->AddRef();
        ++m_cItems;
        if (info.guidItem == GUID_LBI_INPUTMODE && !m_pIndicator)
        {
            CTipInputModeIndicator *pIndicator = CTipInputModeIndicator::Create(punk);
            if (pIndicator && (m_pIndicator || !HasItem(punk)))
            {
                pIndicator->Close();
                pIndicator->Release();
            }
            else
            {
                m_pIndicator = pIndicator;
            }
        }
        return S_OK;
    }

    STDMETHODIMP RemoveItem(ITfLangBarItem *punk) override
    {
        if (!punk)
            return E_INVALIDARG;
        for (ULONG i = 0; i < m_cItems; ++i)
        {
            if (m_items[i].item != punk)
                continue;
            if (m_pIndicator && m_pIndicator->IsFor(punk))
            {
                m_pIndicator->Close();
                m_pIndicator->Release();
                m_pIndicator = NULL;
            }
            ReleaseItemSinks(punk);
            m_items[i].item->Release();
            MoveMemory(&m_items[i], &m_items[i + 1], (m_cItems - i - 1) * sizeof(*m_items));
            --m_cItems;
            return S_OK;
        }
        return S_OK;
    }

    STDMETHODIMP AdviseItemSink(ITfLangBarItemSink *punk, DWORD *pdwCookie, REFGUID rguidItem) override
    {
        if (!punk || !pdwCookie)
            return E_INVALIDARG;
        *pdwCookie = 0;
        LONG index = Find(rguidItem);
        if (index < 0)
            return E_FAIL;
        ITfSource *source = NULL;
        HRESULT hr = m_items[index].item->QueryInterface(IID_ITfSource, (void **)&source);
        if (FAILED(hr))
            return hr;
        CLangBarItemSinkProxy *proxy = new(cicNoThrow) CLangBarItemSinkProxy(punk);
        if (!proxy)
        {
            source->Release();
            return E_OUTOFMEMORY;
        }
        DWORD sourceCookie = 0;
        hr = source->AdviseSink(IID_ITfLangBarItemSink, static_cast<ITfLangBarItemSink *>(proxy), &sourceCookie);
        if (FAILED(hr))
        {
            proxy->Release();
            source->Release();
            return hr;
        }
        LangBarItemSinkEntry *sinks =
            static_cast<LangBarItemSinkEntry *>(cicMemReAlloc(m_sinks, (m_cSinks + 1) * sizeof(*sinks)));
        if (!sinks)
        {
            source->UnadviseSink(sourceCookie);
            proxy->Release();
            source->Release();
            return E_OUTOFMEMORY;
        }
        m_sinks = sinks;
        m_sinks[m_cSinks].cookie = m_nextCookie++;
        m_sinks[m_cSinks].item = m_items[index].item;
        m_sinks[m_cSinks].proxy = proxy;
        m_sinks[m_cSinks].source = source;
        m_sinks[m_cSinks].sourceCookie = sourceCookie;
        *pdwCookie = m_sinks[m_cSinks].cookie;
        ++m_cSinks;
        return S_OK;
    }

    STDMETHODIMP UnadviseItemSink(DWORD dwCookie) override
    {
        for (ULONG i = 0; i < m_cSinks; ++i)
        {
            if (!dwCookie || m_sinks[i].cookie != dwCookie)
                continue;
            m_sinks[i].proxy->Disconnect();
            m_sinks[i].cookie = 0;
            return S_OK;
        }
        return CONNECT_E_NOCONNECTION;
    }

    void ReleaseSinkAt(ULONG i)
    {
        m_sinks[i].source->UnadviseSink(m_sinks[i].sourceCookie);
        m_sinks[i].source->Release();
        m_sinks[i].proxy->Release();
        MoveMemory(&m_sinks[i], &m_sinks[i + 1], (m_cSinks - i - 1) * sizeof(*m_sinks));
        --m_cSinks;
    }

    void ReleaseItemSinks(ITfLangBarItem *item)
    {
        for (ULONG i = m_cSinks; i > 0; --i)
        {
            if (m_sinks[i - 1].item == item)
                ReleaseSinkAt(i - 1);
        }
    }

    STDMETHODIMP GetItemFloatingRect(DWORD dwThreadId, REFGUID rguid, RECT *prc) override
    {
        if (!prc)
            return E_INVALIDARG;
        SetRectEmpty(prc);
        return E_FAIL;
    }

    STDMETHODIMP GetItemsStatus(ULONG ulCount, const GUID *prgguid, DWORD *pdwStatus) override
    {
        if (!prgguid || !pdwStatus)
            return E_INVALIDARG;
        for (ULONG i = 0; i < ulCount; ++i)
        {
            LONG index = Find(prgguid[i]);
            if (index < 0)
                return E_FAIL;
            HRESULT hr = m_items[index].item->GetStatus(&pdwStatus[i]);
            if (FAILED(hr))
                return hr;
        }
        return S_OK;
    }

    STDMETHODIMP GetItemNum(ULONG *pulCount) override
    {
        if (!pulCount)
            return E_INVALIDARG;
        *pulCount = m_cItems;
        return S_OK;
    }

    STDMETHODIMP GetItems(ULONG ulCount, ITfLangBarItem **ppItem, TF_LANGBARITEMINFO *pInfo,
                          DWORD *pdwStatus, ULONG *pcFetched) override
    {
        if (!ppItem || !pInfo || !pdwStatus)
            return E_INVALIDARG;
        ULONG fetched = 0;
        for (; fetched < ulCount && fetched < m_cItems; ++fetched)
        {
            ppItem[fetched] = m_items[fetched].item;
            ppItem[fetched]->AddRef();
            pInfo[fetched] = m_items[fetched].info;
            if (FAILED(m_items[fetched].item->GetStatus(&pdwStatus[fetched])))
                pdwStatus[fetched] = 0;
        }
        if (pcFetched)
            *pcFetched = fetched;
        return (fetched == ulCount) ? S_OK : S_FALSE;
    }

    STDMETHODIMP AdviseItemsSink(ULONG ulCount, ITfLangBarItemSink **ppunk, const GUID *pguidItem,
                                 DWORD *pdwCookie) override
    {
        if (!ppunk || !pguidItem || !pdwCookie)
            return E_INVALIDARG;
        for (ULONG i = 0; i < ulCount; ++i)
        {
            HRESULT hr = AdviseItemSink(ppunk[i], &pdwCookie[i], pguidItem[i]);
            if (FAILED(hr))
                pdwCookie[i] = 0;
        }
        return S_OK;
    }

    STDMETHODIMP UnadviseItemsSink(ULONG ulCount, DWORD *pdwCookie) override
    {
        if (!pdwCookie)
            return E_INVALIDARG;
        for (ULONG i = 0; i < ulCount; ++i)
            UnadviseItemSink(pdwCookie[i]);
        return S_OK;
    }

private:
    ~CLangBarItemMgr()
    {
        if (m_pIndicator)
        {
            m_pIndicator->Close();
            m_pIndicator->Release();
        }
        while (m_cSinks)
            ReleaseSinkAt(m_cSinks - 1);
        cicMemFree(m_sinks);
        for (ULONG i = 0; i < m_cItems; ++i)
            m_items[i].item->Release();
        cicMemFree(m_items);
    }

    BOOL HasItem(ITfLangBarItem *punk) const
    {
        for (ULONG i = 0; i < m_cItems; ++i)
        {
            if (m_items[i].item == punk)
                return TRUE;
        }
        return FALSE;
    }

    LONG Find(REFGUID guid) const
    {
        for (ULONG i = 0; i < m_cItems; ++i)
        {
            if (m_items[i].info.guidItem == guid)
                return (LONG)i;
        }
        return -1;
    }

    LONG m_refs;
    LangBarItemEntry *m_items;
    ULONG m_cItems;
    LangBarItemSinkEntry *m_sinks;
    ULONG m_cSinks;
    DWORD m_nextCookie;
    DWORD m_dwThreadId;
    CLangBarItemMgr *m_pNext;
    CTipInputModeIndicator *m_pIndicator;
};

EXTERN_C
HRESULT LangBarItemMgr_Constructor(ITfLangBarItemMgr **ppOut)
{
    if (!ppOut)
        return E_INVALIDARG;
    *ppOut = CLangBarItemMgr::FindForCurrentThread();
    if (*ppOut)
        return S_OK;
    CLangBarItemMgr *pMgr = new(cicNoThrow) CLangBarItemMgr();
    if (!pMgr)
        return E_OUTOFMEMORY;
    static const struct
    {
        const CLSID *pclsidService;
        const GUID *pguidItem;
        DWORD dwStyle;
        ULONG ulSort;
        UINT uDescId;
        BOOL fSystem;
    } s_SystemItems[] = {
        { &CLSID_SYSTEMLANGBARITEM, &GUID_LBI_CTRL, TF_LBI_STYLE_BTN_MENU, 0, IDS_LANGBAR_LANGUAGE, FALSE },
        { &CLSID_SYSTEMLANGBARITEM, &GUID_LBI_SYSTEM_CORRECTION, TF_LBI_STYLE_BTN_BUTTON | TF_LBI_STYLE_HIDDENBYDEFAULT,
          0, IDS_LANGBAR_CORRECTION, FALSE },
        { &CLSID_SYSTEMLANGBARITEM, &GUID_TFCAT_TIP_KEYBOARD, TF_LBI_STYLE_BTN_MENU, 0, IDS_LANGBAR_KEYBOARD, TRUE },
        { &CLSID_SYSTEM_HELP_SERVICE, &GUID_LBI_SYSTEM_HELP, TF_LBI_STYLE_BTN_MENU, (ULONG)-30, IDS_LANGBAR_HELP, TRUE },
    };
    for (UINT i = 0; i < _countof(s_SystemItems); ++i)
    {
        CSystemLangBarItem *pItem = new(cicNoThrow) CSystemLangBarItem(
            *s_SystemItems[i].pclsidService, *s_SystemItems[i].pguidItem, s_SystemItems[i].dwStyle,
            s_SystemItems[i].ulSort, s_SystemItems[i].uDescId, s_SystemItems[i].fSystem);
        if (!pItem)
            continue;
        pMgr->AddItem(static_cast<ITfLangBarItemButton *>(pItem));
        pItem->Release();
    }
    pMgr->Register();
    *ppOut = pMgr;
    return S_OK;
}
