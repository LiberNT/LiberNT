/*
 * PROJECT:     LiberNT Network Connections Shell
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     Connection user interface objects
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"
#include <initguid.h>
#include "connectionui.h"

#define NETCON_PERMISSION_COUNT 54

HICON
LoadNetConnectionIcon(INetConnection *pCon, DWORD dwSize)
{
    NETCON_PROPERTIES *pProperties = NULL;
    UINT uIcon = IDI_NET_IDLE;

    if (pCon && SUCCEEDED(pCon->GetProperties(&pProperties)) && pProperties)
    {
        if (pProperties->Status != NCS_CONNECTED && pProperties->Status != NCS_AUTHENTICATION_SUCCEEDED)
            uIcon = IDI_NET_OFF;
        NcFreeNetconProperties(pProperties);
    }
    return (HICON)LoadImageW(netshell_hInstance, MAKEINTRESOURCEW(uIcon), IMAGE_ICON, dwSize, dwSize, LR_DEFAULTCOLOR);
}

class CConnectionCommonUi :
    public CComCoClass<CConnectionCommonUi, &CLSID_ConnectionCommonUi>,
    public CComObjectRootEx<CComMultiThreadModel>,
    public INetConnectionCommonUi2
{
    DWORD m_dwStartPage;

public:
    CConnectionCommonUi() : m_dwStartPage(0) {}

    STDMETHOD(ChooseConnection)(struct tagNETCON_CHOOSECONN *pChooseConn, INetConnection **ppCon) override
    {
        return E_NOTIMPL;
    }

    STDMETHOD(ShowConnectionProperties)(HWND hwndParent, INetConnection *pCon) override
    {
        if (!pCon)
            return E_INVALIDARG;
        return ShowNetConnectionProperties(pCon, hwndParent, m_dwStartPage);
    }

    STDMETHOD(StartNewConnectionWizard)(HWND hwndParent, INetConnection **ppCon) override
    {
        return E_NOTIMPL;
    }

    STDMETHOD(SetPropertiesStartPage)(DWORD dwPage) override
    {
        m_dwStartPage = dwPage;
        return S_OK;
    }

    DECLARE_NO_REGISTRY()
    DECLARE_NOT_AGGREGATABLE(CConnectionCommonUi)
    DECLARE_PROTECT_FINAL_CONSTRUCT()

    BEGIN_COM_MAP(CConnectionCommonUi)
        COM_INTERFACE_ENTRY_IID(IID_INetConnectionCommonUi, INetConnectionCommonUi2)
        COM_INTERFACE_ENTRY_IID(IID_INetConnectionCommonUi2, INetConnectionCommonUi2)
    END_COM_MAP()
};

class CConnectionFolderEnum :
    public CComCoClass<CConnectionFolderEnum, &CLSID_ConnectionFolderEnum>,
    public CComObjectRootEx<CComMultiThreadModelNoCS>,
    public IEnumIDList
{
public:
    STDMETHOD(Next)(ULONG celt, PITEMID_CHILD *rgelt, ULONG *pceltFetched) override
    {
        if (pceltFetched)
            *pceltFetched = 0;
        return S_FALSE;
    }

    STDMETHOD(Skip)(ULONG celt) override
    {
        return S_OK;
    }

    STDMETHOD(Reset)() override
    {
        return S_OK;
    }

    STDMETHOD(Clone)(IEnumIDList **ppenum) override
    {
        if (ppenum)
            *ppenum = NULL;
        return E_NOTIMPL;
    }

    DECLARE_NO_REGISTRY()
    DECLARE_NOT_AGGREGATABLE(CConnectionFolderEnum)
    DECLARE_PROTECT_FINAL_CONSTRUCT()

    BEGIN_COM_MAP(CConnectionFolderEnum)
        COM_INTERFACE_ENTRY_IID(IID_IEnumIDList, IEnumIDList)
    END_COM_MAP()
};

class CNetConnectionUiUtilities :
    public CComCoClass<CNetConnectionUiUtilities, &CLSID_NetConnectionUiUtilities>,
    public CComObjectRootEx<CComMultiThreadModelNoCS>,
    public INetConnectionUiUtilities
{
public:
    STDMETHOD(QueryUserAndRemoveComponent)(HWND hwndParent, INetCfg *pNetCfg, INetCfgComponent *pComponent) override
    {
        if (!pNetCfg || !pComponent)
            return E_POINTER;
        FIXME("(%p %p %p) component removal prompt\n", hwndParent, pNetCfg, pComponent);
        return E_NOTIMPL;
    }

    STDMETHOD(QueryUserForReboot)(HWND hwndParent, LPCWSTR pszCaption, DWORD dwFlags) override
    {
        if (!dwFlags)
            return S_OK;
        FIXME("(%p %s %lx) reboot prompt\n", hwndParent, debugstr_w(pszCaption), dwFlags);
        return E_NOTIMPL;
    }

    STDMETHOD(DisplayAddComponentDialog)(HWND hwndParent, INetCfg *pNetCfg, struct tagCI_FILTER_INFO *pFilterInfo) override
    {
        if (!pNetCfg)
            return E_POINTER;
        FIXME("(%p %p %p) add component dialog\n", hwndParent, pNetCfg, pFilterInfo);
        return E_NOTIMPL;
    }

    STDMETHOD_(BOOL, UserHasPermission)(DWORD dwPermission) override
    {
        return dwPermission < NETCON_PERMISSION_COUNT && IsUserAnAdmin();
    }

    DECLARE_NO_REGISTRY()
    DECLARE_NOT_AGGREGATABLE(CNetConnectionUiUtilities)
    DECLARE_PROTECT_FINAL_CONSTRUCT()

    BEGIN_COM_MAP(CNetConnectionUiUtilities)
        COM_INTERFACE_ENTRY_IID(IID_INetConnectionUiUtilities, INetConnectionUiUtilities)
    END_COM_MAP()
};

class CNetConnectionTypeUi :
    public INetConnectionConnectUi,
    public INetConnectionPropertyUi2
{
protected:
    CComPtr<INetConnection> m_pCon;

public:
    STDMETHOD(SetConnection)(INetConnection *pCon) override
    {
        m_pCon = pCon;
        return S_OK;
    }

    STDMETHOD(Connect)(HWND hwndParent, DWORD dwFlags) override
    {
        if (!m_pCon)
            return E_UNEXPECTED;
        return m_pCon->Connect();
    }

    STDMETHOD(Disconnect)(HWND hwndParent, DWORD dwFlags) override
    {
        if (!m_pCon)
            return E_UNEXPECTED;
        return m_pCon->Disconnect();
    }

    STDMETHOD(AddPages)(HWND hwndParent, LPFNADDPROPSHEETPAGE pfnAddPage, LPARAM lParam) override
    {
        if (!m_pCon)
            return E_UNEXPECTED;
        FIXME("(%p %p %p) remote access connection pages\n", hwndParent, pfnAddPage, (void *)lParam);
        return E_NOTIMPL;
    }

    STDMETHOD(GetIcon)(DWORD dwSize, HICON *phIcon) override
    {
        if (!phIcon)
            return E_POINTER;
        *phIcon = LoadNetConnectionIcon(m_pCon, dwSize);
        return *phIcon ? S_OK : HRESULT_FROM_WIN32(GetLastError());
    }
};

template <class T, const CLSID *pclsid>
class CNetConnectionTypeUiT :
    public CComCoClass<T, pclsid>,
    public CComObjectRootEx<CComMultiThreadModelNoCS>,
    public CNetConnectionTypeUi
{
public:
    DECLARE_NO_REGISTRY()
    DECLARE_NOT_AGGREGATABLE(T)
    DECLARE_PROTECT_FINAL_CONSTRUCT()
};

class CDialupConnectionUi : public CNetConnectionTypeUiT<CDialupConnectionUi, &CLSID_DialupConnectionUi>
{
public:
    BEGIN_COM_MAP(CDialupConnectionUi)
        COM_INTERFACE_ENTRY_IID(IID_INetConnectionConnectUi, INetConnectionConnectUi)
        COM_INTERFACE_ENTRY_IID(IID_INetConnectionPropertyUi2, INetConnectionPropertyUi2)
    END_COM_MAP()
};

class CInboundConnectionUi : public CNetConnectionTypeUiT<CInboundConnectionUi, &CLSID_InboundConnectionUi>
{
public:
    BEGIN_COM_MAP(CInboundConnectionUi)
        COM_INTERFACE_ENTRY_IID(IID_INetConnectionPropertyUi2, INetConnectionPropertyUi2)
    END_COM_MAP()
};

class CInternetConnectionUi : public CNetConnectionTypeUiT<CInternetConnectionUi, &CLSID_InternetConnectionUi>
{
public:
    BEGIN_COM_MAP(CInternetConnectionUi)
        COM_INTERFACE_ENTRY_IID(IID_INetConnectionConnectUi, INetConnectionConnectUi)
        COM_INTERFACE_ENTRY_IID(IID_INetConnectionPropertyUi2, INetConnectionPropertyUi2)
    END_COM_MAP()
};

class CVpnConnectionUi : public CNetConnectionTypeUiT<CVpnConnectionUi, &CLSID_VpnConnectionUi>
{
public:
    BEGIN_COM_MAP(CVpnConnectionUi)
        COM_INTERFACE_ENTRY_IID(IID_INetConnectionConnectUi, INetConnectionConnectUi)
        COM_INTERFACE_ENTRY_IID(IID_INetConnectionPropertyUi2, INetConnectionPropertyUi2)
    END_COM_MAP()
};

class CPPPoEUi : public CNetConnectionTypeUiT<CPPPoEUi, &CLSID_PPPoEUi>
{
public:
    BEGIN_COM_MAP(CPPPoEUi)
        COM_INTERFACE_ENTRY_IID(IID_INetConnectionConnectUi, INetConnectionConnectUi)
        COM_INTERFACE_ENTRY_IID(IID_INetConnectionPropertyUi2, INetConnectionPropertyUi2)
    END_COM_MAP()
};

class CSharedAccessConnectionUi : public CNetConnectionTypeUiT<CSharedAccessConnectionUi, &CLSID_SharedAccessConnectionUi>
{
public:
    BEGIN_COM_MAP(CSharedAccessConnectionUi)
        COM_INTERFACE_ENTRY_IID(IID_INetConnectionConnectUi, INetConnectionConnectUi)
        COM_INTERFACE_ENTRY_IID(IID_INetConnectionPropertyUi, INetConnectionPropertyUi2)
        COM_INTERFACE_ENTRY_IID(IID_INetConnectionPropertyUi2, INetConnectionPropertyUi2)
    END_COM_MAP()
};

OBJECT_ENTRY_AUTO(CLSID_ConnectionCommonUi, CConnectionCommonUi)
OBJECT_ENTRY_AUTO(CLSID_ConnectionFolderEnum, CConnectionFolderEnum)
OBJECT_ENTRY_AUTO(CLSID_NetConnectionUiUtilities, CNetConnectionUiUtilities)
OBJECT_ENTRY_AUTO(CLSID_DialupConnectionUi, CDialupConnectionUi)
OBJECT_ENTRY_AUTO(CLSID_InboundConnectionUi, CInboundConnectionUi)
OBJECT_ENTRY_AUTO(CLSID_InternetConnectionUi, CInternetConnectionUi)
OBJECT_ENTRY_AUTO(CLSID_VpnConnectionUi, CVpnConnectionUi)
OBJECT_ENTRY_AUTO(CLSID_PPPoEUi, CPPPoEUi)
OBJECT_ENTRY_AUTO(CLSID_SharedAccessConnectionUi, CSharedAccessConnectionUi)
