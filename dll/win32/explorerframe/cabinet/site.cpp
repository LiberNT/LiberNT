/*
 * PROJECT:     LiberNT Explorer
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     Explorer browser site, view callbacks and navigation events
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "cabinet.h"
#include <exdisp.h>
#include <shldisp.h>

class CCabinetSite : public IServiceProvider, public ICommDlgBrowser, public IExplorerBrowserEvents
{
public:
    CCabinetSite(CCabinetWindow *window, IExplorerBrowser *browser) :
        m_Ref(1),
        m_Window(window),
        m_Browser(browser)
    {
    }

    STDMETHODIMP QueryInterface(REFIID riid, void **ppv) override
    {
        if (!ppv)
            return E_POINTER;
        if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_IServiceProvider))
            *ppv = static_cast<IServiceProvider *>(this);
        else if (IsEqualIID(riid, IID_ICommDlgBrowser))
            *ppv = static_cast<ICommDlgBrowser *>(this);
        else if (IsEqualIID(riid, IID_IExplorerBrowserEvents))
            *ppv = static_cast<IExplorerBrowserEvents *>(this);
        else
        {
            *ppv = NULL;
            return E_NOINTERFACE;
        }
        AddRef();
        return S_OK;
    }

    STDMETHODIMP_(ULONG) AddRef() override
    {
        return InterlockedIncrement(&m_Ref);
    }

    STDMETHODIMP_(ULONG) Release() override
    {
        ULONG ref = InterlockedDecrement(&m_Ref);
        if (!ref)
            delete this;
        return ref;
    }

    STDMETHODIMP QueryService(REFGUID service, REFIID riid, void **ppv) override
    {
        if (!ppv)
            return E_POINTER;
        *ppv = NULL;
        if (IsEqualGUID(service, SID_SExplorerBrowserFrame))
            return QueryInterface(riid, ppv);
        return E_NOINTERFACE;
    }

    STDMETHODIMP OnDefaultCommand(IShellView *view) override
    {
        return m_Window ? m_Window->OnDefaultCommand(view) : S_FALSE;
    }

    STDMETHODIMP OnStateChange(IShellView *view, ULONG change) override
    {
        UNREFERENCED_PARAMETER(view);
        if (m_Window && change == CDBOSC_SELCHANGE)
            m_Window->OnSelectionChanged();
        return S_OK;
    }

    STDMETHODIMP IncludeObject(IShellView *view, PCUITEMID_CHILD pidl) override
    {
        UNREFERENCED_PARAMETER(view);
        UNREFERENCED_PARAMETER(pidl);
        return S_OK;
    }

    STDMETHODIMP OnNavigationPending(PCIDLIST_ABSOLUTE pidl) override
    {
        UNREFERENCED_PARAMETER(pidl);
        return S_OK;
    }

    STDMETHODIMP OnViewCreated(IShellView *view) override
    {
        if (m_Window)
            m_Window->OnViewCreated(m_Browser, view);
        return S_OK;
    }

    STDMETHODIMP OnNavigationComplete(PCIDLIST_ABSOLUTE pidl) override
    {
        if (m_Window)
            m_Window->OnNavigationComplete(m_Browser, pidl);
        return S_OK;
    }

    STDMETHODIMP OnNavigationFailed(PCIDLIST_ABSOLUTE pidl) override
    {
        if (m_Window)
            m_Window->OnNavigationFailed(m_Browser, pidl);
        return S_OK;
    }

private:
    virtual ~CCabinetSite() {}

    LONG m_Ref;
    CCabinetWindow *m_Window;
    IExplorerBrowser *m_Browser;
};

HRESULT
CabinetSite_Create(CCabinetWindow *window, IExplorerBrowser *browser, IUnknown **site)
{
    CCabinetSite *object = new CCabinetSite(window, browser);

    if (!object)
        return E_OUTOFMEMORY;
    *site = static_cast<IServiceProvider *>(object);
    return S_OK;
}

class CCabinetNavigation : public INameSpaceTreeControlEvents
{
public:
    CCabinetNavigation(CCabinetWindow *window) :
        m_Ref(1),
        m_Window(window)
    {
    }

    STDMETHODIMP QueryInterface(REFIID riid, void **ppv) override
    {
        if (!ppv)
            return E_POINTER;
        if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_INameSpaceTreeControlEvents))
        {
            *ppv = static_cast<INameSpaceTreeControlEvents *>(this);
            AddRef();
            return S_OK;
        }
        *ppv = NULL;
        return E_NOINTERFACE;
    }

    STDMETHODIMP_(ULONG) AddRef() override
    {
        return InterlockedIncrement(&m_Ref);
    }

    STDMETHODIMP_(ULONG) Release() override
    {
        ULONG ref = InterlockedDecrement(&m_Ref);
        if (!ref)
            delete this;
        return ref;
    }

    STDMETHODIMP OnItemClick(IShellItem *item, NSTCEHITTEST hit, NSTCECLICKTYPE click) override
    {
        if (!item || !(hit & (NSTCEHT_ONITEMICON | NSTCEHT_ONITEMLABEL | NSTCEHT_ONITEMRIGHT | NSTCEHT_ONITEMIDENT)))
            return S_FALSE;
        if ((click & NSTCECT_BUTTON) == NSTCECT_LBUTTON)
            m_Window->OnNavigationItem(item, FALSE);
        else if ((click & NSTCECT_BUTTON) == NSTCECT_MBUTTON)
            m_Window->OnNavigationItem(item, TRUE);
        return S_FALSE;
    }

    STDMETHODIMP OnPropertyItemCommit(IShellItem *item) override { UNREFERENCED_PARAMETER(item); return S_FALSE; }
    STDMETHODIMP OnItemStateChanging(IShellItem *item, NSTCITEMSTATE mask, NSTCITEMSTATE state) override
    {
        UNREFERENCED_PARAMETER(item);
        UNREFERENCED_PARAMETER(mask);
        UNREFERENCED_PARAMETER(state);
        return S_OK;
    }
    STDMETHODIMP OnItemStateChanged(IShellItem *item, NSTCITEMSTATE mask, NSTCITEMSTATE state) override
    {
        UNREFERENCED_PARAMETER(item);
        UNREFERENCED_PARAMETER(mask);
        UNREFERENCED_PARAMETER(state);
        return S_OK;
    }
    STDMETHODIMP OnSelectionChanged(IShellItemArray *selection) override
    {
        UNREFERENCED_PARAMETER(selection);
        return S_OK;
    }
    STDMETHODIMP OnKeyboardInput(UINT msg, WPARAM wParam, LPARAM lParam) override
    {
        UNREFERENCED_PARAMETER(msg);
        UNREFERENCED_PARAMETER(wParam);
        UNREFERENCED_PARAMETER(lParam);
        return S_FALSE;
    }
    STDMETHODIMP OnBeforeExpand(IShellItem *item) override { UNREFERENCED_PARAMETER(item); return S_OK; }
    STDMETHODIMP OnAfterExpand(IShellItem *item) override { UNREFERENCED_PARAMETER(item); return S_OK; }
    STDMETHODIMP OnBeginLabelEdit(IShellItem *item) override { UNREFERENCED_PARAMETER(item); return S_OK; }
    STDMETHODIMP OnEndLabelEdit(IShellItem *item) override { UNREFERENCED_PARAMETER(item); return S_OK; }
    STDMETHODIMP OnGetToolTip(IShellItem *item, LPWSTR tip, int length) override
    {
        UNREFERENCED_PARAMETER(item);
        UNREFERENCED_PARAMETER(tip);
        UNREFERENCED_PARAMETER(length);
        return E_NOTIMPL;
    }
    STDMETHODIMP OnBeforeItemDelete(IShellItem *item) override { UNREFERENCED_PARAMETER(item); return S_OK; }
    STDMETHODIMP OnItemAdded(IShellItem *item, BOOL root) override
    {
        UNREFERENCED_PARAMETER(item);
        UNREFERENCED_PARAMETER(root);
        return S_OK;
    }
    STDMETHODIMP OnItemDeleted(IShellItem *item, BOOL root) override
    {
        UNREFERENCED_PARAMETER(item);
        UNREFERENCED_PARAMETER(root);
        return S_OK;
    }
    STDMETHODIMP OnBeforeContextMenu(IShellItem *item, REFIID riid, void **ppv) override
    {
        UNREFERENCED_PARAMETER(item);
        UNREFERENCED_PARAMETER(riid);
        *ppv = NULL;
        return E_NOTIMPL;
    }
    STDMETHODIMP OnAfterContextMenu(IShellItem *item, IContextMenu *menu, REFIID riid, void **ppv) override
    {
        UNREFERENCED_PARAMETER(item);
        return menu->QueryInterface(riid, ppv);
    }
    STDMETHODIMP OnBeforeStateImageChange(IShellItem *item) override { UNREFERENCED_PARAMETER(item); return S_OK; }
    STDMETHODIMP OnGetDefaultIconIndex(IShellItem *item, int *defaultIcon, int *openIcon) override
    {
        UNREFERENCED_PARAMETER(item);
        UNREFERENCED_PARAMETER(defaultIcon);
        UNREFERENCED_PARAMETER(openIcon);
        return E_NOTIMPL;
    }

private:
    virtual ~CCabinetNavigation() {}

    LONG m_Ref;
    CCabinetWindow *m_Window;
};

HRESULT
CabinetNavigation_Create(CCabinetWindow *window, IUnknown **events)
{
    CCabinetNavigation *object = new CCabinetNavigation(window);

    if (!object)
        return E_OUTOFMEMORY;
    *events = static_cast<INameSpaceTreeControlEvents *>(object);
    return S_OK;
}

class CCabinetViewEvents : public IDispatch
{
public:
    CCabinetViewEvents(CCabinetWindow *window) :
        m_Ref(1),
        m_Window(window)
    {
    }

    STDMETHODIMP QueryInterface(REFIID riid, void **ppv) override
    {
        if (!ppv)
            return E_POINTER;
        if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_IDispatch) ||
            IsEqualIID(riid, DIID_DShellFolderViewEvents))
        {
            *ppv = static_cast<IDispatch *>(this);
            AddRef();
            return S_OK;
        }
        *ppv = NULL;
        return E_NOINTERFACE;
    }

    STDMETHODIMP_(ULONG) AddRef() override
    {
        return InterlockedIncrement(&m_Ref);
    }

    STDMETHODIMP_(ULONG) Release() override
    {
        ULONG ref = InterlockedDecrement(&m_Ref);
        if (!ref)
            delete this;
        return ref;
    }

    STDMETHODIMP GetTypeInfoCount(UINT *count) override
    {
        *count = 0;
        return S_OK;
    }

    STDMETHODIMP GetTypeInfo(UINT index, LCID lcid, ITypeInfo **info) override
    {
        UNREFERENCED_PARAMETER(index);
        UNREFERENCED_PARAMETER(lcid);
        *info = NULL;
        return E_NOTIMPL;
    }

    STDMETHODIMP GetIDsOfNames(REFIID riid, LPOLESTR *names, UINT count, LCID lcid, DISPID *ids) override
    {
        UNREFERENCED_PARAMETER(riid);
        UNREFERENCED_PARAMETER(names);
        UNREFERENCED_PARAMETER(count);
        UNREFERENCED_PARAMETER(lcid);
        UNREFERENCED_PARAMETER(ids);
        return E_NOTIMPL;
    }

    STDMETHODIMP Invoke(DISPID dispid, REFIID riid, LCID lcid, WORD flags, DISPPARAMS *params, VARIANT *result,
                        EXCEPINFO *exception, UINT *argument) override
    {
        UNREFERENCED_PARAMETER(riid);
        UNREFERENCED_PARAMETER(lcid);
        UNREFERENCED_PARAMETER(flags);
        UNREFERENCED_PARAMETER(params);
        UNREFERENCED_PARAMETER(result);
        UNREFERENCED_PARAMETER(exception);
        UNREFERENCED_PARAMETER(argument);
        if (m_Window)
            m_Window->OnViewEvent(dispid);
        return S_OK;
    }

private:
    virtual ~CCabinetViewEvents() {}

    LONG m_Ref;
    CCabinetWindow *m_Window;
};

HRESULT
CabinetViewEvents_Create(CCabinetWindow *window, IUnknown **events)
{
    CCabinetViewEvents *object = new CCabinetViewEvents(window);

    if (!object)
        return E_OUTOFMEMORY;
    *events = static_cast<IDispatch *>(object);
    return S_OK;
}
