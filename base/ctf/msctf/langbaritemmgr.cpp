/*
 * PROJECT:     LiberNT CTF
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     ITfLangBarItemMgr implementation
 * COPYRIGHT:   Copyright 2026 mikuta0407 <mikuta0407@mikuta0407.net>
 */

#include "precomp.h"
#include "resource.h"

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

class CSystemHelpItem final
    : public ITfLangBarItem
    , public ITfSource
{
public:
    CSystemHelpItem() : m_refs(1), m_sinks(NULL), m_cSinks(0), m_nextCookie(1) { }

    STDMETHODIMP QueryInterface(REFIID riid, void **ppvObj) override
    {
        if (!ppvObj)
            return E_POINTER;
        *ppvObj = NULL;
        if (riid == IID_IUnknown || riid == IID_ITfLangBarItem)
            *ppvObj = static_cast<ITfLangBarItem *>(this);
        else if (riid == IID_ITfSource)
            *ppvObj = static_cast<ITfSource *>(this);
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
        ZeroMemory(pInfo, sizeof(*pInfo));
        pInfo->clsidService = CLSID_SYSTEM_HELP_SERVICE;
        pInfo->guidItem = GUID_LBI_SYSTEM_HELP;
        pInfo->dwStyle = TF_LBI_STYLE_BTN_MENU;
        pInfo->ulSort = (ULONG)-30;
        LoadStringW(g_hInst, IDS_LANGBAR_HELP, pInfo->szDescription, _countof(pInfo->szDescription));
        return S_OK;
    }
    STDMETHODIMP GetStatus(DWORD *pdwStatus) override
    {
        if (!pdwStatus)
            return E_INVALIDARG;
        *pdwStatus = 0;
        return S_OK;
    }
    STDMETHODIMP Show(BOOL fShow) override { return S_OK; }
    STDMETHODIMP GetTooltipString(BSTR *pbstrToolTip) override
    {
        if (!pbstrToolTip)
            return E_INVALIDARG;
        *pbstrToolTip = SysAllocString(L"");
        return *pbstrToolTip ? S_OK : E_OUTOFMEMORY;
    }

    STDMETHODIMP AdviseSink(REFIID riid, IUnknown *punk, DWORD *pdwCookie) override
    {
        if (!punk || !pdwCookie)
            return E_INVALIDARG;
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
        m_sinks[m_cSinks].cookie = m_nextCookie++;
        m_sinks[m_cSinks].sink = sink;
        *pdwCookie = m_sinks[m_cSinks].cookie;
        ++m_cSinks;
        return S_OK;
    }
    STDMETHODIMP UnadviseSink(DWORD dwCookie) override
    {
        for (ULONG i = 0; i < m_cSinks; ++i)
        {
            if (m_sinks[i].cookie != dwCookie)
                continue;
            m_sinks[i].sink->Release();
            MoveMemory(&m_sinks[i], &m_sinks[i + 1], (m_cSinks - i - 1) * sizeof(*m_sinks));
            --m_cSinks;
            return S_OK;
        }
        return CONNECT_E_NOCONNECTION;
    }

private:
    struct SinkEntry
    {
        DWORD cookie;
        ITfSystemLangBarItemSink *sink;
    };

    ~CSystemHelpItem()
    {
        for (ULONG i = 0; i < m_cSinks; ++i)
            m_sinks[i].sink->Release();
        cicMemFree(m_sinks);
    }

    LONG m_refs;
    SinkEntry *m_sinks;
    ULONG m_cSinks;
    DWORD m_nextCookie;
};

class CLangBarItemMgr;

static SRWLOCK s_LangBarItemMgrLock = SRWLOCK_INIT;
static CLangBarItemMgr *s_pLangBarItemMgrs = NULL;

class CLangBarItemMgr final : public ITfLangBarItemMgr
{
public:
    CLangBarItemMgr()
        : m_refs(1), m_items(NULL), m_cItems(0), m_sinks(NULL), m_cSinks(0), m_nextCookie(1)
        , m_dwThreadId(GetCurrentThreadId()), m_pNext(NULL)
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
        while (m_cSinks)
            ReleaseSinkAt(m_cSinks - 1);
        cicMemFree(m_sinks);
        for (ULONG i = 0; i < m_cItems; ++i)
            m_items[i].item->Release();
        cicMemFree(m_items);
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
    CSystemHelpItem *pHelp = new(cicNoThrow) CSystemHelpItem();
    if (pHelp)
    {
        pMgr->AddItem(pHelp);
        pHelp->Release();
    }
    pMgr->Register();
    *ppOut = pMgr;
    return S_OK;
}
