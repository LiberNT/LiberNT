/*
 * PROJECT:     LiberNT Network Connections Shell
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     Connection user interface objects
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

EXTERN_C const IID IID_INetConnectionCommonUi;
EXTERN_C const IID IID_INetConnectionCommonUi2;
DEFINE_GUID(IID_INetConnectionUiUtilities, 0xFAEDCF5E, 0x31FE, 0x11D1, 0xAA, 0xD2, 0x00, 0x80, 0x5F, 0xC1, 0x27, 0x0E);

struct tagNETCON_CHOOSECONN;
struct tagCI_FILTER_INFO;

#define INTERFACE INetConnectionCommonUi
DECLARE_INTERFACE_(INetConnectionCommonUi, IUnknown)
{
    STDMETHOD(QueryInterface)(THIS_ REFIID riid, void **ppv) PURE;
    STDMETHOD_(ULONG, AddRef)(THIS) PURE;
    STDMETHOD_(ULONG, Release)(THIS) PURE;
    STDMETHOD(ChooseConnection)(THIS_ struct tagNETCON_CHOOSECONN *pChooseConn, INetConnection **ppCon) PURE;
    STDMETHOD(ShowConnectionProperties)(THIS_ HWND hwndParent, INetConnection *pCon) PURE;
    STDMETHOD(StartNewConnectionWizard)(THIS_ HWND hwndParent, INetConnection **ppCon) PURE;
};
#undef INTERFACE

#define INTERFACE INetConnectionCommonUi2
DECLARE_INTERFACE_(INetConnectionCommonUi2, INetConnectionCommonUi)
{
    STDMETHOD(QueryInterface)(THIS_ REFIID riid, void **ppv) PURE;
    STDMETHOD_(ULONG, AddRef)(THIS) PURE;
    STDMETHOD_(ULONG, Release)(THIS) PURE;
    STDMETHOD(ChooseConnection)(THIS_ struct tagNETCON_CHOOSECONN *pChooseConn, INetConnection **ppCon) PURE;
    STDMETHOD(ShowConnectionProperties)(THIS_ HWND hwndParent, INetConnection *pCon) PURE;
    STDMETHOD(StartNewConnectionWizard)(THIS_ HWND hwndParent, INetConnection **ppCon) PURE;
    STDMETHOD(SetPropertiesStartPage)(THIS_ DWORD dwPage) PURE;
};
#undef INTERFACE

#define INTERFACE INetConnectionUiUtilities
DECLARE_INTERFACE_(INetConnectionUiUtilities, IUnknown)
{
    STDMETHOD(QueryInterface)(THIS_ REFIID riid, void **ppv) PURE;
    STDMETHOD_(ULONG, AddRef)(THIS) PURE;
    STDMETHOD_(ULONG, Release)(THIS) PURE;
    STDMETHOD(QueryUserAndRemoveComponent)(THIS_ HWND hwndParent, INetCfg *pNetCfg, INetCfgComponent *pComponent) PURE;
    STDMETHOD(QueryUserForReboot)(THIS_ HWND hwndParent, LPCWSTR pszCaption, DWORD dwFlags) PURE;
    STDMETHOD(DisplayAddComponentDialog)(THIS_ HWND hwndParent, INetCfg *pNetCfg, struct tagCI_FILTER_INFO *pFilterInfo) PURE;
    STDMETHOD_(BOOL, UserHasPermission)(THIS_ DWORD dwPermission) PURE;
};
#undef INTERFACE

HICON
LoadNetConnectionIcon(INetConnection *pCon, DWORD dwSize);
