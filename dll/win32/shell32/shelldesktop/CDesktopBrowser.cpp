/*
 * Shell Desktop
 *
 * Copyright 2008 Thomas Bluemel
 * Copyright 2020 Katayama Hirofumi MZ
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 */

#include "shelldesktop.h"

// Support for multiple monitors is disabled till LVM_SETWORKAREAS gets implemented
#ifdef MULTIMONITOR_SUPPORT
#include <atlcoll.h>
#endif

#include <dbt.h>
#include <atlsimpcoll.h>
#include <shdocvw_undoc.h>

static const GUID GUID_DEVINTERFACE_WPD_SHELL =
    { 0x6ac27878, 0xa6fa, 0x4155, { 0xba, 0x85, 0xf9, 0x8f, 0x49, 0x1d, 0x4f, 0x33 } };

WINE_DEFAULT_DEBUG_CHANNEL(desktop);

static const WCHAR szProgmanClassName[]  = L"Progman";
static const WCHAR szProgmanWindowName[] = L"Program Manager";

static BOOL IsDesktopBrowserForwardShellViewCmd(WORD Cmd)
{
    // Note: The normal CShellBrowser forwards the entire FCIDM_SHVIEWFIRST..LAST range, we do not.
    // Note: Windows allows FCIDM_SHVIEW_SHOWINGROUPS but we don't support it nor does it make sense.
    return (FCIDM_SHVIEW_CREATELINK <= Cmd && Cmd <= FCIDM_SHVIEW_DESELECTALL) || 
           (FCIDM_SHVIEW_ARRANGE_AUTO <= Cmd && Cmd <= FCIDM_SHVIEW_ARRANGE_AUTOGRID) ||
           (Cmd == FCIDM_SHVIEW_REFRESH);
}

class CDesktopAutomation;

class CDesktopBrowser :
    public CWindowImpl<CDesktopBrowser, CWindow, CFrameWinTraits>,
    public CComObjectRootEx<CComMultiThreadModelNoCS>,
    public IShellBrowser,
    public IShellBrowserService,
    public IServiceProvider
{
private:
    HACCEL m_hAccel;
    HWND m_hWndShellView;
    CComPtr<IShellDesktopTray> m_Tray;
    CComPtr<IShellView>        m_ShellView;

    CComPtr<IOleWindow>        m_ChangeNotifyServer;
    HWND                       m_hwndChangeNotifyServer;
    DWORD m_dwDrives;
    HDEVNOTIFY m_hPortableDeviceNotify;
    LONG m_lShellWindowsCookie;
    CComObject<CDesktopAutomation> *m_pAutomation;
    CSimpleArray<CComBSTR> m_PropertyNames;
    CSimpleArray<CComVariant> m_PropertyValues;

    LRESULT _NotifyTray(UINT uMsg, WPARAM wParam, LPARAM lParam);
    HRESULT _Resize();
    HRESULT _GetWindowRect(RECT *prc);
    HRESULT _AllocEmpty(BSTR *pbstr);
    int _FindProperty(BSTR Property);

public:
    CDesktopBrowser();
    ~CDesktopBrowser();
    HRESULT Initialize(IShellDesktopTray *ShellDeskx);

    // *** IOleWindow methods ***
    STDMETHOD(GetWindow)(HWND *lphwnd) override;
    STDMETHOD(ContextSensitiveHelp)(BOOL fEnterMode) override;

    // *** IShellBrowser methods ***
    STDMETHOD(InsertMenusSB)(HMENU hmenuShared, LPOLEMENUGROUPWIDTHS lpMenuWidths) override;
    STDMETHOD(SetMenuSB)(HMENU hmenuShared, HOLEMENU holemenuRes, HWND hwndActiveObject) override;
    STDMETHOD(RemoveMenusSB)(HMENU hmenuShared) override;
    STDMETHOD(SetStatusTextSB)(LPCOLESTR pszStatusText) override;
    STDMETHOD(EnableModelessSB)(BOOL fEnable) override;
    STDMETHOD(TranslateAcceleratorSB)(MSG *pmsg, WORD wID) override;
    STDMETHOD(BrowseObject)(LPCITEMIDLIST pidl, UINT wFlags) override;
    STDMETHOD(GetViewStateStream)(DWORD grfMode, IStream **ppStrm) override;
    STDMETHOD(GetControlWindow)(UINT id, HWND *lphwnd) override;
    STDMETHOD(SendControlMsg)(UINT id, UINT uMsg, WPARAM wParam, LPARAM lParam, LRESULT *pret) override;
    STDMETHOD(QueryActiveShellView)(struct IShellView **ppshv) override;
    STDMETHOD(OnViewWindowActive)(struct IShellView *ppshv) override;
    STDMETHOD(SetToolbarItems)(LPTBBUTTON lpButtons, UINT nButtons, UINT uFlags) override;

    // *** IShellBrowserService methods ***
    STDMETHOD(Unknown)(PVOID p1, PVOID p2, PVOID p3) override;
    STDMETHOD(GetTravelLog)(ITravelLog **pptl) override;
    STDMETHOD(ShowControlWindow)(UINT id, BOOL fShow) override;
    STDMETHOD(IsControlWindowShown)(UINT id, BOOL *pfShown) override;
    STDMETHOD(DisplayParseError)(HRESULT hres, LPCWSTR pwszPath) override;
    STDMETHOD(SetNavigateState)(BNSTATE bnstate) override;
    STDMETHOD(GetNavigateState)(BNSTATE *pbnstate) override;
    STDMETHOD(UpdateWindowList)() override;
    STDMETHOD(UpdateBackForwardState)() override;
    STDMETHOD(SetFlags)(DWORD dwFlags, DWORD dwFlagMask) override;
    STDMETHOD(GetFlags)(DWORD *pdwFlags) override;
    STDMETHOD(GetPidl)(PIDLIST_ABSOLUTE *ppidl) override;
    STDMETHOD_(DWORD, GetBrowserIndex)() override;
    STDMETHOD(GetHistoryObject)(IOleObject **ppole, IStream **pstm, IBindCtx **ppbc) override;
    STDMETHOD(GetPalette)(HPALETTE *hpal) override;
    STDMETHOD(RegisterWindow)(BOOL fForceRegister, int swc) override;
    STDMETHOD(GetPropertyBag)(DWORD flags, REFIID riid, void **ppv) override;
    STDMETHOD(SetAsDefFolderSettings)() override;
    STDMETHOD(CreateViewWindow)(IShellItem *psi, IShellView *psvNew, IShellView *psvOld, RECT *prcView, HWND *phwnd) override;

    // *** IBrowserService2 methods (fake for now) ***
    inline void SetTopBrowser() const {}

    // *** IServiceProvider methods ***
    STDMETHOD(QueryService)(REFGUID guidService, REFIID riid, void **ppvObject) override;

    STDMETHOD(GoBack)();
    STDMETHOD(GoForward)();
    STDMETHOD(GoHome)();
    STDMETHOD(GoSearch)();
    STDMETHOD(Navigate)(BSTR URL, VARIANT *Flags, VARIANT *TargetFrameName, VARIANT *PostData, VARIANT *Headers);
    STDMETHOD(Refresh)();
    STDMETHOD(Refresh2)(VARIANT *Level);
    STDMETHOD(Stop)();
    STDMETHOD(get_Application)(IDispatch **ppDisp);
    STDMETHOD(get_Parent)(IDispatch **ppDisp);
    STDMETHOD(get_Container)(IDispatch **ppDisp);
    STDMETHOD(get_Document)(IDispatch **ppDisp);
    STDMETHOD(get_TopLevelContainer)(VARIANT_BOOL *pBool);
    STDMETHOD(get_Type)(BSTR *Type);
    STDMETHOD(get_Left)(LONG *pl);
    STDMETHOD(put_Left)(LONG Left);
    STDMETHOD(get_Top)(LONG *pl);
    STDMETHOD(put_Top)(LONG Top);
    STDMETHOD(get_Width)(LONG *pl);
    STDMETHOD(put_Width)(LONG Width);
    STDMETHOD(get_Height)(LONG *pl);
    STDMETHOD(put_Height)(LONG Height);
    STDMETHOD(get_LocationName)(BSTR *LocationName);
    STDMETHOD(get_LocationURL)(BSTR *LocationURL);
    STDMETHOD(get_Busy)(VARIANT_BOOL *pBool);
    STDMETHOD(Quit)();
    STDMETHOD(ClientToWindow)(int *pcx, int *pcy);
    STDMETHOD(PutProperty)(BSTR Property, VARIANT vtValue);
    STDMETHOD(GetProperty)(BSTR Property, VARIANT *pvtValue);
    STDMETHOD(get_Name)(BSTR *Name);
    STDMETHOD(get_HWND)(SHANDLE_PTR *pHWND);
    STDMETHOD(get_FullName)(BSTR *FullName);
    STDMETHOD(get_Path)(BSTR *Path);
    STDMETHOD(get_Visible)(VARIANT_BOOL *pBool);
    STDMETHOD(put_Visible)(VARIANT_BOOL Value);
    STDMETHOD(get_StatusBar)(VARIANT_BOOL *pBool);
    STDMETHOD(put_StatusBar)(VARIANT_BOOL Value);
    STDMETHOD(get_StatusText)(BSTR *StatusText);
    STDMETHOD(put_StatusText)(BSTR StatusText);
    STDMETHOD(get_ToolBar)(int *Value);
    STDMETHOD(put_ToolBar)(int Value);
    STDMETHOD(get_MenuBar)(VARIANT_BOOL *Value);
    STDMETHOD(put_MenuBar)(VARIANT_BOOL Value);
    STDMETHOD(get_FullScreen)(VARIANT_BOOL *pbFullScreen);
    STDMETHOD(put_FullScreen)(VARIANT_BOOL bFullScreen);
    STDMETHOD(Navigate2)(VARIANT *URL, VARIANT *Flags, VARIANT *TargetFrameName, VARIANT *PostData, VARIANT *Headers);
    STDMETHOD(QueryStatusWB)(OLECMDID cmdID, OLECMDF *pcmdf);
    STDMETHOD(ExecWB)(OLECMDID cmdID, OLECMDEXECOPT cmdexecopt, VARIANT *pvaIn, VARIANT *pvaOut);
    STDMETHOD(ShowBrowserBar)(VARIANT *pvaClsid, VARIANT *pvarShow, VARIANT *pvarSize);
    STDMETHOD(get_ReadyState)(READYSTATE *plReadyState);
    STDMETHOD(get_Offline)(VARIANT_BOOL *pbOffline);
    STDMETHOD(put_Offline)(VARIANT_BOOL bOffline);
    STDMETHOD(get_Silent)(VARIANT_BOOL *pbSilent);
    STDMETHOD(put_Silent)(VARIANT_BOOL bSilent);
    STDMETHOD(get_RegisterAsBrowser)(VARIANT_BOOL *pbRegister);
    STDMETHOD(put_RegisterAsBrowser)(VARIANT_BOOL bRegister);
    STDMETHOD(get_RegisterAsDropTarget)(VARIANT_BOOL *pbRegister);
    STDMETHOD(put_RegisterAsDropTarget)(VARIANT_BOOL bRegister);
    STDMETHOD(get_TheaterMode)(VARIANT_BOOL *pbRegister);
    STDMETHOD(put_TheaterMode)(VARIANT_BOOL bRegister);
    STDMETHOD(get_AddressBar)(VARIANT_BOOL *Value);
    STDMETHOD(put_AddressBar)(VARIANT_BOOL Value);
    STDMETHOD(get_Resizable)(VARIANT_BOOL *Value);
    STDMETHOD(put_Resizable)(VARIANT_BOOL Value);

    // message handlers
    LRESULT OnEraseBkgnd(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL &bHandled);
    LRESULT OnSize(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL &bHandled);
    LRESULT OnSettingChange(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL &bHandled);
    LRESULT OnClose(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL &bHandled);
    LRESULT OnOpenNewWindow(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL &bHandled);
    LRESULT OnCommand(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL &bHandled);
    LRESULT OnSetFocus(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL &bHandled);
    LRESULT OnGetChangeNotifyServer(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL &bHandled);
    LRESULT OnDeviceChange(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL &bHandled);
    LRESULT OnShowOptionsDlg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL &bHandled);
    LRESULT OnSaveState(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL &bHandled);
    LRESULT OnDestroy(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL &bHandled);

DECLARE_WND_CLASS_EX(szProgmanClassName, CS_DBLCLKS, COLOR_DESKTOP)

BEGIN_MSG_MAP(CBaseBar)
    MESSAGE_HANDLER(WM_ERASEBKGND, OnEraseBkgnd)
    MESSAGE_HANDLER(WM_SIZE, OnSize)
    MESSAGE_HANDLER(WM_SYSCOLORCHANGE, OnSettingChange)
    MESSAGE_HANDLER(WM_SETTINGCHANGE, OnSettingChange)
    MESSAGE_HANDLER(WM_CLOSE, OnClose)
    MESSAGE_HANDLER(WM_EXPLORER_OPEN_NEW_WINDOW, OnOpenNewWindow)
    MESSAGE_HANDLER(WM_COMMAND, OnCommand)
    MESSAGE_HANDLER(WM_SETFOCUS, OnSetFocus)
    MESSAGE_HANDLER(WM_DESKTOP_GET_CNOTIFY_SERVER, OnGetChangeNotifyServer)
    MESSAGE_HANDLER(WM_DEVICECHANGE, OnDeviceChange)
    MESSAGE_HANDLER(WM_PROGMAN_OPENSHELLSETTINGS, OnShowOptionsDlg)
    MESSAGE_HANDLER(WM_PROGMAN_SAVESTATE, OnSaveState)
    MESSAGE_HANDLER(WM_DESTROY, OnDestroy)
END_MSG_MAP()

BEGIN_COM_MAP(CDesktopBrowser)
    COM_INTERFACE_ENTRY_IID(IID_IOleWindow, IOleWindow)
    COM_INTERFACE_ENTRY_IID(IID_IShellBrowser, IShellBrowser)
    COM_INTERFACE_ENTRY_IID(IID_IShellBrowserService, IShellBrowserService)
    COM_INTERFACE_ENTRY_IID(IID_IServiceProvider, IServiceProvider)
END_COM_MAP()
};

class CDesktopAutomation :
    public CComObjectRootEx<CComMultiThreadModelNoCS>,
    public IDispatchImpl<IWebBrowser2, &IID_IWebBrowser2, &LIBID_SHDocVw, 1, 1>,
    public IServiceProvider,
    public IConnectionPointContainerImpl<CDesktopAutomation>,
    public IConnectionPointImpl<CDesktopAutomation, &DIID_DWebBrowserEvents2>
{
public:
    CDesktopBrowser *m_pBrowser;

    CDesktopAutomation() : m_pBrowser(NULL)
    {
    }

    STDMETHOD(QueryService)(REFGUID guidService, REFIID riid, void **ppvObject) override
    {
        if (!m_pBrowser)
            return E_FAIL;
        return m_pBrowser->QueryService(guidService, riid, ppvObject);
    }

    STDMETHOD(GoBack)() override { return m_pBrowser ? m_pBrowser->GoBack() : E_FAIL; }
    STDMETHOD(GoForward)() override { return m_pBrowser ? m_pBrowser->GoForward() : E_FAIL; }
    STDMETHOD(GoHome)() override { return m_pBrowser ? m_pBrowser->GoHome() : E_FAIL; }
    STDMETHOD(GoSearch)() override { return m_pBrowser ? m_pBrowser->GoSearch() : E_FAIL; }
    STDMETHOD(Navigate)(BSTR URL, VARIANT *Flags, VARIANT *TargetFrameName, VARIANT *PostData, VARIANT *Headers) override { return m_pBrowser ? m_pBrowser->Navigate(URL, Flags, TargetFrameName, PostData, Headers) : E_FAIL; }
    STDMETHOD(Refresh)() override { return m_pBrowser ? m_pBrowser->Refresh() : E_FAIL; }
    STDMETHOD(Refresh2)(VARIANT *Level) override { return m_pBrowser ? m_pBrowser->Refresh2(Level) : E_FAIL; }
    STDMETHOD(Stop)() override { return m_pBrowser ? m_pBrowser->Stop() : E_FAIL; }
    STDMETHOD(get_Application)(IDispatch **ppDisp) override { return m_pBrowser ? m_pBrowser->get_Application(ppDisp) : E_FAIL; }
    STDMETHOD(get_Parent)(IDispatch **ppDisp) override { return m_pBrowser ? m_pBrowser->get_Parent(ppDisp) : E_FAIL; }
    STDMETHOD(get_Container)(IDispatch **ppDisp) override { return m_pBrowser ? m_pBrowser->get_Container(ppDisp) : E_FAIL; }
    STDMETHOD(get_Document)(IDispatch **ppDisp) override { return m_pBrowser ? m_pBrowser->get_Document(ppDisp) : E_FAIL; }
    STDMETHOD(get_TopLevelContainer)(VARIANT_BOOL *pBool) override { return m_pBrowser ? m_pBrowser->get_TopLevelContainer(pBool) : E_FAIL; }
    STDMETHOD(get_Type)(BSTR *Type) override { return m_pBrowser ? m_pBrowser->get_Type(Type) : E_FAIL; }
    STDMETHOD(get_Left)(LONG *pl) override { return m_pBrowser ? m_pBrowser->get_Left(pl) : E_FAIL; }
    STDMETHOD(put_Left)(LONG Left) override { return m_pBrowser ? m_pBrowser->put_Left(Left) : E_FAIL; }
    STDMETHOD(get_Top)(LONG *pl) override { return m_pBrowser ? m_pBrowser->get_Top(pl) : E_FAIL; }
    STDMETHOD(put_Top)(LONG Top) override { return m_pBrowser ? m_pBrowser->put_Top(Top) : E_FAIL; }
    STDMETHOD(get_Width)(LONG *pl) override { return m_pBrowser ? m_pBrowser->get_Width(pl) : E_FAIL; }
    STDMETHOD(put_Width)(LONG Width) override { return m_pBrowser ? m_pBrowser->put_Width(Width) : E_FAIL; }
    STDMETHOD(get_Height)(LONG *pl) override { return m_pBrowser ? m_pBrowser->get_Height(pl) : E_FAIL; }
    STDMETHOD(put_Height)(LONG Height) override { return m_pBrowser ? m_pBrowser->put_Height(Height) : E_FAIL; }
    STDMETHOD(get_LocationName)(BSTR *LocationName) override { return m_pBrowser ? m_pBrowser->get_LocationName(LocationName) : E_FAIL; }
    STDMETHOD(get_LocationURL)(BSTR *LocationURL) override { return m_pBrowser ? m_pBrowser->get_LocationURL(LocationURL) : E_FAIL; }
    STDMETHOD(get_Busy)(VARIANT_BOOL *pBool) override { return m_pBrowser ? m_pBrowser->get_Busy(pBool) : E_FAIL; }
    STDMETHOD(Quit)() override { return m_pBrowser ? m_pBrowser->Quit() : E_FAIL; }
    STDMETHOD(ClientToWindow)(int *pcx, int *pcy) override { return m_pBrowser ? m_pBrowser->ClientToWindow(pcx, pcy) : E_FAIL; }
    STDMETHOD(PutProperty)(BSTR Property, VARIANT vtValue) override { return m_pBrowser ? m_pBrowser->PutProperty(Property, vtValue) : E_FAIL; }
    STDMETHOD(GetProperty)(BSTR Property, VARIANT *pvtValue) override { return m_pBrowser ? m_pBrowser->GetProperty(Property, pvtValue) : E_FAIL; }
    STDMETHOD(get_Name)(BSTR *Name) override { return m_pBrowser ? m_pBrowser->get_Name(Name) : E_FAIL; }
    STDMETHOD(get_HWND)(SHANDLE_PTR *pHWND) override { return m_pBrowser ? m_pBrowser->get_HWND(pHWND) : E_FAIL; }
    STDMETHOD(get_FullName)(BSTR *FullName) override { return m_pBrowser ? m_pBrowser->get_FullName(FullName) : E_FAIL; }
    STDMETHOD(get_Path)(BSTR *Path) override { return m_pBrowser ? m_pBrowser->get_Path(Path) : E_FAIL; }
    STDMETHOD(get_Visible)(VARIANT_BOOL *pBool) override { return m_pBrowser ? m_pBrowser->get_Visible(pBool) : E_FAIL; }
    STDMETHOD(put_Visible)(VARIANT_BOOL Value) override { return m_pBrowser ? m_pBrowser->put_Visible(Value) : E_FAIL; }
    STDMETHOD(get_StatusBar)(VARIANT_BOOL *pBool) override { return m_pBrowser ? m_pBrowser->get_StatusBar(pBool) : E_FAIL; }
    STDMETHOD(put_StatusBar)(VARIANT_BOOL Value) override { return m_pBrowser ? m_pBrowser->put_StatusBar(Value) : E_FAIL; }
    STDMETHOD(get_StatusText)(BSTR *StatusText) override { return m_pBrowser ? m_pBrowser->get_StatusText(StatusText) : E_FAIL; }
    STDMETHOD(put_StatusText)(BSTR StatusText) override { return m_pBrowser ? m_pBrowser->put_StatusText(StatusText) : E_FAIL; }
    STDMETHOD(get_ToolBar)(int *Value) override { return m_pBrowser ? m_pBrowser->get_ToolBar(Value) : E_FAIL; }
    STDMETHOD(put_ToolBar)(int Value) override { return m_pBrowser ? m_pBrowser->put_ToolBar(Value) : E_FAIL; }
    STDMETHOD(get_MenuBar)(VARIANT_BOOL *Value) override { return m_pBrowser ? m_pBrowser->get_MenuBar(Value) : E_FAIL; }
    STDMETHOD(put_MenuBar)(VARIANT_BOOL Value) override { return m_pBrowser ? m_pBrowser->put_MenuBar(Value) : E_FAIL; }
    STDMETHOD(get_FullScreen)(VARIANT_BOOL *pbFullScreen) override { return m_pBrowser ? m_pBrowser->get_FullScreen(pbFullScreen) : E_FAIL; }
    STDMETHOD(put_FullScreen)(VARIANT_BOOL bFullScreen) override { return m_pBrowser ? m_pBrowser->put_FullScreen(bFullScreen) : E_FAIL; }
    STDMETHOD(Navigate2)(VARIANT *URL, VARIANT *Flags, VARIANT *TargetFrameName, VARIANT *PostData, VARIANT *Headers) override { return m_pBrowser ? m_pBrowser->Navigate2(URL, Flags, TargetFrameName, PostData, Headers) : E_FAIL; }
    STDMETHOD(QueryStatusWB)(OLECMDID cmdID, OLECMDF *pcmdf) override { return m_pBrowser ? m_pBrowser->QueryStatusWB(cmdID, pcmdf) : E_FAIL; }
    STDMETHOD(ExecWB)(OLECMDID cmdID, OLECMDEXECOPT cmdexecopt, VARIANT *pvaIn, VARIANT *pvaOut) override { return m_pBrowser ? m_pBrowser->ExecWB(cmdID, cmdexecopt, pvaIn, pvaOut) : E_FAIL; }
    STDMETHOD(ShowBrowserBar)(VARIANT *pvaClsid, VARIANT *pvarShow, VARIANT *pvarSize) override { return m_pBrowser ? m_pBrowser->ShowBrowserBar(pvaClsid, pvarShow, pvarSize) : E_FAIL; }
    STDMETHOD(get_ReadyState)(READYSTATE *plReadyState) override { return m_pBrowser ? m_pBrowser->get_ReadyState(plReadyState) : E_FAIL; }
    STDMETHOD(get_Offline)(VARIANT_BOOL *pbOffline) override { return m_pBrowser ? m_pBrowser->get_Offline(pbOffline) : E_FAIL; }
    STDMETHOD(put_Offline)(VARIANT_BOOL bOffline) override { return m_pBrowser ? m_pBrowser->put_Offline(bOffline) : E_FAIL; }
    STDMETHOD(get_Silent)(VARIANT_BOOL *pbSilent) override { return m_pBrowser ? m_pBrowser->get_Silent(pbSilent) : E_FAIL; }
    STDMETHOD(put_Silent)(VARIANT_BOOL bSilent) override { return m_pBrowser ? m_pBrowser->put_Silent(bSilent) : E_FAIL; }
    STDMETHOD(get_RegisterAsBrowser)(VARIANT_BOOL *pbRegister) override { return m_pBrowser ? m_pBrowser->get_RegisterAsBrowser(pbRegister) : E_FAIL; }
    STDMETHOD(put_RegisterAsBrowser)(VARIANT_BOOL bRegister) override { return m_pBrowser ? m_pBrowser->put_RegisterAsBrowser(bRegister) : E_FAIL; }
    STDMETHOD(get_RegisterAsDropTarget)(VARIANT_BOOL *pbRegister) override { return m_pBrowser ? m_pBrowser->get_RegisterAsDropTarget(pbRegister) : E_FAIL; }
    STDMETHOD(put_RegisterAsDropTarget)(VARIANT_BOOL bRegister) override { return m_pBrowser ? m_pBrowser->put_RegisterAsDropTarget(bRegister) : E_FAIL; }
    STDMETHOD(get_TheaterMode)(VARIANT_BOOL *pbRegister) override { return m_pBrowser ? m_pBrowser->get_TheaterMode(pbRegister) : E_FAIL; }
    STDMETHOD(put_TheaterMode)(VARIANT_BOOL bRegister) override { return m_pBrowser ? m_pBrowser->put_TheaterMode(bRegister) : E_FAIL; }
    STDMETHOD(get_AddressBar)(VARIANT_BOOL *Value) override { return m_pBrowser ? m_pBrowser->get_AddressBar(Value) : E_FAIL; }
    STDMETHOD(put_AddressBar)(VARIANT_BOOL Value) override { return m_pBrowser ? m_pBrowser->put_AddressBar(Value) : E_FAIL; }
    STDMETHOD(get_Resizable)(VARIANT_BOOL *Value) override { return m_pBrowser ? m_pBrowser->get_Resizable(Value) : E_FAIL; }
    STDMETHOD(put_Resizable)(VARIANT_BOOL Value) override { return m_pBrowser ? m_pBrowser->put_Resizable(Value) : E_FAIL; }
BEGIN_COM_MAP(CDesktopAutomation)
    COM_INTERFACE_ENTRY_IID(IID_IDispatch, IDispatch)
    COM_INTERFACE_ENTRY_IID(IID_IWebBrowser, IWebBrowser)
    COM_INTERFACE_ENTRY_IID(IID_IWebBrowserApp, IWebBrowserApp)
    COM_INTERFACE_ENTRY_IID(IID_IWebBrowser2, IWebBrowser2)
    COM_INTERFACE_ENTRY_IID(IID_IServiceProvider, IServiceProvider)
    COM_INTERFACE_ENTRY_IID(IID_IConnectionPointContainer, IConnectionPointContainer)
END_COM_MAP()

BEGIN_CONNECTION_POINT_MAP(CDesktopAutomation)
    CONNECTION_POINT_ENTRY(DIID_DWebBrowserEvents2)
END_CONNECTION_POINT_MAP()
};

CDesktopBrowser::CDesktopBrowser():
    m_hAccel(NULL),
    m_hWndShellView(NULL),
    m_hwndChangeNotifyServer(NULL),
    m_dwDrives(::GetLogicalDrives()),
    m_hPortableDeviceNotify(NULL),
    m_lShellWindowsCookie(0),
    m_pAutomation(NULL)
{
    SetTopBrowser();
}

CDesktopBrowser::~CDesktopBrowser()
{
    if (m_pAutomation)
    {
        m_pAutomation->m_pBrowser = NULL;
        m_pAutomation->Release();
        m_pAutomation = NULL;
    }

    if (m_ShellView.p != NULL && m_hWndShellView != NULL)
    {
        m_ShellView->DestroyViewWindow();
    }

    if (m_hwndChangeNotifyServer)
    {
        ::DestroyWindow(m_hwndChangeNotifyServer);
    }
}

#ifdef MULTIMONITOR_SUPPORT
BOOL CALLBACK MonitorEnumProc(
  _In_ HMONITOR hMonitor,
  _In_ HDC      hdcMonitor,
  _In_ LPRECT   lprcMonitor,
  _In_ LPARAM   dwData
)
{
    CAtlList<RECT> *list = (CAtlList<RECT>*)dwData;
    MONITORINFO MonitorInfo;
    MonitorInfo.cbSize = sizeof(MonitorInfo);
    if (::GetMonitorInfoW(hMonitor, &MonitorInfo))
    {
        list->AddTail(MonitorInfo.rcWork);
    }

    return TRUE;
}
#endif

HRESULT CDesktopBrowser::_Resize()
{
    RECT rcNewSize;

#ifdef MULTIMONITOR_SUPPORT

    UINT cMonitors = GetSystemMetrics(SM_CMONITORS);
    if (cMonitors == 1)
    {
        SystemParametersInfoW(SPI_GETWORKAREA, 0, &rcNewSize, 0);
    }
    else
    {
        SetRect(&rcNewSize,
                GetSystemMetrics(SM_XVIRTUALSCREEN),
                GetSystemMetrics(SM_YVIRTUALSCREEN),
                GetSystemMetrics(SM_XVIRTUALSCREEN) + GetSystemMetrics(SM_CXVIRTUALSCREEN),
                GetSystemMetrics(SM_YVIRTUALSCREEN) + GetSystemMetrics(SM_CYVIRTUALSCREEN));
    }

    ::MoveWindow(m_hWnd, rcNewSize.left, rcNewSize.top, rcNewSize.right - rcNewSize.left, rcNewSize.bottom - rcNewSize.top, TRUE);
    ::MoveWindow(m_hWndShellView, 0, 0, rcNewSize.right - rcNewSize.left, rcNewSize.bottom - rcNewSize.top, TRUE);

    if (cMonitors != 1)
    {
        CAtlList<RECT> list;
        EnumDisplayMonitors(NULL, NULL, MonitorEnumProc, (LPARAM)&list);
        RECT* prcWorkAreas = new RECT[list.GetCount()];
        int i = 0;
        for (POSITION it = list.GetHeadPosition(); it; list.GetNext(it))
            prcWorkAreas[i++] = list.GetAt(it);

        HWND hwndListView = FindWindowExW(m_hWndShellView, NULL, WC_LISTVIEW, NULL);

        ::SendMessageW(hwndListView, LVM_SETWORKAREAS , i, (LPARAM)prcWorkAreas);
    }

#else
     SystemParametersInfoW(SPI_GETWORKAREA, 0, &rcNewSize, 0);
    ::MoveWindow(m_hWnd, rcNewSize.left, rcNewSize.top, rcNewSize.right - rcNewSize.left, rcNewSize.bottom - rcNewSize.top, TRUE);
    ::MoveWindow(m_hWndShellView, 0, 0, rcNewSize.right - rcNewSize.left, rcNewSize.bottom - rcNewSize.top, TRUE);

#endif
    return S_OK;
}

HRESULT CDesktopBrowser::Initialize(IShellDesktopTray *ShellDesk)
{
    CComPtr<IShellFolder> psfDesktop;
    HRESULT hRet;
    hRet = SHGetDesktopFolder(&psfDesktop);
    if (FAILED_UNEXPECTEDLY(hRet))
        return hRet;

    m_Tray = ShellDesk;

    Create(NULL, NULL, szProgmanWindowName, WS_POPUP | WS_CLIPSIBLINGS | WS_CLIPCHILDREN, WS_EX_TOOLWINDOW);
    if (!m_hWnd)
        return E_FAIL;

    DEV_BROADCAST_DEVICEINTERFACE_W Filter = { sizeof(Filter), DBT_DEVTYP_DEVICEINTERFACE };
    Filter.dbcc_classguid = GUID_DEVINTERFACE_WPD_SHELL;
    m_hPortableDeviceNotify = RegisterDeviceNotificationW(m_hWnd, &Filter, DEVICE_NOTIFY_WINDOW_HANDLE);

    hRet = psfDesktop->CreateViewObject(m_hWnd, IID_PPV_ARG(IShellView, &m_ShellView));
    if (FAILED_UNEXPECTEDLY(hRet))
        return hRet;

    m_Tray->RegisterDesktopWindow(m_hWnd);
    if (FAILED_UNEXPECTEDLY(hRet))
        return hRet;

    BOOL fHideIcons = SHELL_GetSetting(SSF_HIDEICONS, fHideIcons);
    FOLDERSETTINGS fs;
    RECT rcShellView = {0,0,0,0};
    fs.ViewMode = FVM_ICON;
    fs.fFlags = FWF_DESKTOP | FWF_NOCLIENTEDGE | FWF_NOSCROLL | FWF_TRANSPARENT |
                FWF_SNAPTOGRID | (fHideIcons ? FWF_NOICONS : 0);
    hRet = m_ShellView->CreateViewWindow(NULL, &fs, (IShellBrowser *)this, &rcShellView, &m_hWndShellView);
    if (FAILED_UNEXPECTEDLY(hRet))
        return hRet;

    _Resize();

    HWND hwndListView = FindWindowExW(m_hWndShellView, NULL, WC_LISTVIEW, NULL);

    m_hAccel = LoadAcceleratorsW(shell32_hInstance, MAKEINTRESOURCEW(IDA_DESKBROWSER));

#if 1
    /* A Windows8+ specific hack */
    ::ShowWindow(m_hWndShellView, SW_SHOW);
    ::ShowWindow(hwndListView, SW_SHOW);
#endif
    ShowWindow(SW_SHOW);
    UpdateWindow();

    if (!m_pAutomation && SUCCEEDED(CComObject<CDesktopAutomation>::CreateInstance(&m_pAutomation)))
    {
        m_pAutomation->AddRef();
        m_pAutomation->m_pBrowser = this;
    }

    CComPtr<IShellWindows> pShellWindows;
    pShellWindows.Attach(WinList_GetShellWindows(TRUE));
    if (pShellWindows && m_pAutomation)
        pShellWindows->Register(static_cast<IDispatch *>(static_cast<IWebBrowser2 *>(m_pAutomation)),
                                HandleToLong(m_hWnd), SWC_DESKTOP, &m_lShellWindowsCookie);

    return hRet;
}

HRESULT STDMETHODCALLTYPE CDesktopBrowser::GetWindow(HWND *lphwnd)
{
    if (lphwnd == NULL)
        return E_POINTER;
    *lphwnd = m_hWnd;
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CDesktopBrowser::ContextSensitiveHelp(BOOL fEnterMode)
{
    return E_NOTIMPL;
}

HRESULT STDMETHODCALLTYPE CDesktopBrowser::InsertMenusSB(HMENU hmenuShared, LPOLEMENUGROUPWIDTHS lpMenuWidths)
{
    return E_NOTIMPL;
}

HRESULT STDMETHODCALLTYPE CDesktopBrowser::SetMenuSB(HMENU hmenuShared, HOLEMENU holemenuRes, HWND hwndActiveObject)
{
    return E_NOTIMPL;
}

HRESULT STDMETHODCALLTYPE CDesktopBrowser::RemoveMenusSB(HMENU hmenuShared)
{
    return E_NOTIMPL;
}

HRESULT STDMETHODCALLTYPE CDesktopBrowser::SetStatusTextSB(LPCOLESTR lpszStatusText)
{
    return E_NOTIMPL;
}

HRESULT STDMETHODCALLTYPE CDesktopBrowser::EnableModelessSB(BOOL fEnable)
{
    return E_NOTIMPL;
}

HRESULT STDMETHODCALLTYPE CDesktopBrowser::TranslateAcceleratorSB(LPMSG lpmsg, WORD wID)
{
    if (!::TranslateAcceleratorW(m_hWnd, m_hAccel, lpmsg))
        return S_FALSE;
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CDesktopBrowser::BrowseObject(LPCITEMIDLIST pidl, UINT wFlags)
{
    /*
     * We should use IShellWindows interface here in order to attempt to
     * find an open shell window that shows the requested pidl and activate it
     */

    SHELLEXECUTEINFOW info = { sizeof(info) };
    info.fMask = SEE_MASK_IDLIST | SEE_MASK_FLAG_NO_UI;
    info.lpVerb = L"open";
    info.lpIDList = const_cast<LPITEMIDLIST>(pidl);
    info.nShow = SW_SHOWNORMAL;
    if (ShellExecuteExW(&info))
        return S_OK;

    DWORD dwFlags = ((wFlags & SBSP_EXPLOREMODE) != 0) ? SH_EXPLORER_CMDLINE_FLAG_E : 0;
    return SHOpenNewFrame(ILClone(pidl), NULL, 0, dwFlags);
}

HRESULT STDMETHODCALLTYPE CDesktopBrowser::GetViewStateStream(DWORD grfMode, IStream **ppStrm)
{
    return E_NOTIMPL;
}

HRESULT STDMETHODCALLTYPE CDesktopBrowser::GetControlWindow(UINT id, HWND *lphwnd)
{
    if (lphwnd == NULL)
        return E_POINTER;
    return E_NOTIMPL;
}

HRESULT STDMETHODCALLTYPE CDesktopBrowser::SendControlMsg(UINT id, UINT uMsg, WPARAM wParam, LPARAM lParam, LRESULT *pret)
{
    if (pret == NULL)
        return E_POINTER;
    return E_NOTIMPL;
}

HRESULT STDMETHODCALLTYPE CDesktopBrowser::QueryActiveShellView(IShellView **ppshv)
{
    if (ppshv == NULL)
        return E_POINTER;
    *ppshv = m_ShellView;
    if (*ppshv != NULL)
        (*ppshv)->AddRef();

    return S_OK;
}

HRESULT STDMETHODCALLTYPE CDesktopBrowser::OnViewWindowActive(IShellView *ppshv)
{
    return E_NOTIMPL;
}

HRESULT STDMETHODCALLTYPE CDesktopBrowser::SetToolbarItems(LPTBBUTTON lpButtons, UINT nButtons, UINT uFlags)
{
    return E_NOTIMPL;
}

HRESULT STDMETHODCALLTYPE CDesktopBrowser::Unknown(PVOID p1, PVOID p2, PVOID p3)
{
    return E_NOTIMPL;
}

HRESULT STDMETHODCALLTYPE CDesktopBrowser::GetTravelLog(ITravelLog **pptl)
{
    return E_NOTIMPL;
}

HRESULT STDMETHODCALLTYPE CDesktopBrowser::ShowControlWindow(UINT id, BOOL fShow)
{
    return E_NOTIMPL;
}

HRESULT STDMETHODCALLTYPE CDesktopBrowser::IsControlWindowShown(UINT id, BOOL *pfShown)
{
    return E_NOTIMPL;
}

HRESULT STDMETHODCALLTYPE CDesktopBrowser::DisplayParseError(HRESULT hres, LPCWSTR pwszPath)
{
    return E_NOTIMPL;
}

HRESULT STDMETHODCALLTYPE CDesktopBrowser::SetNavigateState(BNSTATE bnstate)
{
    return E_NOTIMPL;
}

HRESULT STDMETHODCALLTYPE CDesktopBrowser::GetNavigateState(BNSTATE *pbnstate)
{
    return E_NOTIMPL;
}

HRESULT STDMETHODCALLTYPE CDesktopBrowser::UpdateWindowList()
{
    return E_NOTIMPL;
}

HRESULT STDMETHODCALLTYPE CDesktopBrowser::UpdateBackForwardState()
{
    return E_NOTIMPL;
}

HRESULT STDMETHODCALLTYPE CDesktopBrowser::SetFlags(DWORD dwFlags, DWORD dwFlagMask)
{
    return E_NOTIMPL;
}

HRESULT STDMETHODCALLTYPE CDesktopBrowser::GetFlags(DWORD *pdwFlags)
{
    return E_NOTIMPL;
}

HRESULT STDMETHODCALLTYPE CDesktopBrowser::GetPidl(PIDLIST_ABSOLUTE *ppidl)
{
    return E_NOTIMPL;
}

DWORD STDMETHODCALLTYPE CDesktopBrowser::GetBrowserIndex()
{
    return 0;
}

HRESULT STDMETHODCALLTYPE CDesktopBrowser::GetHistoryObject(IOleObject **ppole, IStream **pstm, IBindCtx **ppbc)
{
    return E_NOTIMPL;
}

HRESULT STDMETHODCALLTYPE CDesktopBrowser::GetPalette(HPALETTE *hpal)
{
    return E_NOTIMPL;
}

HRESULT STDMETHODCALLTYPE CDesktopBrowser::RegisterWindow(BOOL fForceRegister, int swc)
{
    return E_NOTIMPL;
}

HRESULT STDMETHODCALLTYPE CDesktopBrowser::GetPropertyBag(DWORD flags, REFIID riid, void **ppv)
{
    ITEMIDLIST deskpidl = {};
    return SHGetViewStatePropertyBag(&deskpidl, L"Desktop", flags | SHGVSPB_ROAM, riid, ppv);
}

HRESULT STDMETHODCALLTYPE CDesktopBrowser::SetAsDefFolderSettings()
{
    return E_NOTIMPL;
}

HRESULT STDMETHODCALLTYPE CDesktopBrowser::CreateViewWindow(IShellItem *psi, IShellView *psvNew, IShellView *psvOld, RECT *prcView, HWND *phwnd)
{
    return E_NOTIMPL;
}

HRESULT STDMETHODCALLTYPE CDesktopBrowser::QueryService(REFGUID guidService, REFIID riid, PVOID *ppv)
{
    /* FIXME - handle guidService (SID_STopLevelBrowser for IShellBrowserService etc) */
    HRESULT hr = QueryInterface(riid, ppv);
    if (FAILED(hr) && m_pAutomation && !IsEqualGUID(guidService, SID_STopLevelBrowser))
        hr = m_pAutomation->QueryInterface(riid, ppv);
    return hr;
}

HRESULT CDesktopBrowser::_GetWindowRect(RECT *prc)
{
    if (!m_hWnd || !::GetWindowRect(m_hWnd, prc))
        return E_FAIL;
    return S_OK;
}

HRESULT CDesktopBrowser::_AllocEmpty(BSTR *pbstr)
{
    *pbstr = SysAllocString(L"");
    return *pbstr ? S_FALSE : E_OUTOFMEMORY;
}

HRESULT STDMETHODCALLTYPE CDesktopBrowser::GoBack()
{
    return E_NOTIMPL;
}

HRESULT STDMETHODCALLTYPE CDesktopBrowser::GoForward()
{
    return E_NOTIMPL;
}

HRESULT STDMETHODCALLTYPE CDesktopBrowser::GoHome()
{
    return E_NOTIMPL;
}

HRESULT STDMETHODCALLTYPE CDesktopBrowser::GoSearch()
{
    return E_NOTIMPL;
}

HRESULT STDMETHODCALLTYPE CDesktopBrowser::Navigate(BSTR URL, VARIANT *Flags, VARIANT *TargetFrameName,
                                                    VARIANT *PostData, VARIANT *Headers)
{
    return E_NOTIMPL;
}

HRESULT STDMETHODCALLTYPE CDesktopBrowser::Refresh()
{
    if (m_ShellView)
        m_ShellView->Refresh();
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CDesktopBrowser::Refresh2(VARIANT *Level)
{
    return E_NOTIMPL;
}

HRESULT STDMETHODCALLTYPE CDesktopBrowser::Stop()
{
    return E_NOTIMPL;
}

HRESULT STDMETHODCALLTYPE CDesktopBrowser::get_Application(IDispatch **ppDisp)
{
    if (!ppDisp)
        return E_POINTER;
    *ppDisp = NULL;
    if (!m_pAutomation)
        return E_FAIL;
    return m_pAutomation->QueryInterface(IID_PPV_ARG(IDispatch, ppDisp));
}

HRESULT STDMETHODCALLTYPE CDesktopBrowser::get_Parent(IDispatch **ppDisp)
{
    return get_Application(ppDisp);
}

HRESULT STDMETHODCALLTYPE CDesktopBrowser::get_Container(IDispatch **ppDisp)
{
    if (!ppDisp)
        return E_POINTER;
    *ppDisp = NULL;
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CDesktopBrowser::get_Document(IDispatch **ppDisp)
{
    if (!ppDisp)
        return E_POINTER;
    *ppDisp = NULL;
    if (!m_ShellView)
        return E_FAIL;
    return m_ShellView->GetItemObject(SVGIO_BACKGROUND, IID_PPV_ARG(IDispatch, ppDisp));
}

HRESULT STDMETHODCALLTYPE CDesktopBrowser::get_TopLevelContainer(VARIANT_BOOL *pBool)
{
    if (!pBool)
        return E_POINTER;
    *pBool = VARIANT_TRUE;
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CDesktopBrowser::get_Type(BSTR *Type)
{
    if (!Type)
        return E_POINTER;
    *Type = NULL;
    return E_NOINTERFACE;
}

HRESULT STDMETHODCALLTYPE CDesktopBrowser::get_Left(LONG *pl)
{
    RECT rc;
    if (!pl)
        return E_POINTER;
    HRESULT hr = _GetWindowRect(&rc);
    if (SUCCEEDED(hr))
        *pl = rc.left;
    return hr;
}

HRESULT STDMETHODCALLTYPE CDesktopBrowser::put_Left(LONG Left)
{
    return E_NOTIMPL;
}

HRESULT STDMETHODCALLTYPE CDesktopBrowser::get_Top(LONG *pl)
{
    RECT rc;
    if (!pl)
        return E_POINTER;
    HRESULT hr = _GetWindowRect(&rc);
    if (SUCCEEDED(hr))
        *pl = rc.top;
    return hr;
}

HRESULT STDMETHODCALLTYPE CDesktopBrowser::put_Top(LONG Top)
{
    return E_NOTIMPL;
}

HRESULT STDMETHODCALLTYPE CDesktopBrowser::get_Width(LONG *pl)
{
    RECT rc;
    if (!pl)
        return E_POINTER;
    HRESULT hr = _GetWindowRect(&rc);
    if (SUCCEEDED(hr))
        *pl = rc.right - rc.left;
    return hr;
}

HRESULT STDMETHODCALLTYPE CDesktopBrowser::put_Width(LONG Width)
{
    return E_NOTIMPL;
}

HRESULT STDMETHODCALLTYPE CDesktopBrowser::get_Height(LONG *pl)
{
    RECT rc;
    if (!pl)
        return E_POINTER;
    HRESULT hr = _GetWindowRect(&rc);
    if (SUCCEEDED(hr))
        *pl = rc.bottom - rc.top;
    return hr;
}

HRESULT STDMETHODCALLTYPE CDesktopBrowser::put_Height(LONG Height)
{
    return E_NOTIMPL;
}

HRESULT STDMETHODCALLTYPE CDesktopBrowser::get_LocationName(BSTR *LocationName)
{
    if (!LocationName)
        return E_POINTER;
    return _AllocEmpty(LocationName);
}

HRESULT STDMETHODCALLTYPE CDesktopBrowser::get_LocationURL(BSTR *LocationURL)
{
    if (!LocationURL)
        return E_POINTER;
    return _AllocEmpty(LocationURL);
}

HRESULT STDMETHODCALLTYPE CDesktopBrowser::get_Busy(VARIANT_BOOL *pBool)
{
    return E_NOTIMPL;
}

HRESULT STDMETHODCALLTYPE CDesktopBrowser::Quit()
{
    return E_NOTIMPL;
}

HRESULT STDMETHODCALLTYPE CDesktopBrowser::ClientToWindow(int *pcx, int *pcy)
{
    RECT rcWindow, rcClient;

    if (!pcx || !pcy)
        return E_POINTER;
    if (!m_hWndShellView || !::GetWindowRect(m_hWnd, &rcWindow) || !::GetClientRect(m_hWndShellView, &rcClient))
        return E_FAIL;
    *pcx += (rcWindow.right - rcWindow.left) - (rcClient.right - rcClient.left);
    *pcy += (rcWindow.bottom - rcWindow.top) - (rcClient.bottom - rcClient.top);
    return S_OK;
}

int CDesktopBrowser::_FindProperty(BSTR Property)
{
    for (int i = 0; i < m_PropertyNames.GetSize(); i++)
    {
        if (!wcscmp(m_PropertyNames[i] ? (BSTR)m_PropertyNames[i] : L"", Property ? Property : L""))
            return i;
    }
    return -1;
}

HRESULT STDMETHODCALLTYPE CDesktopBrowser::PutProperty(BSTR Property, VARIANT vtValue)
{
    int i = _FindProperty(Property);
    if (i < 0)
    {
        if (!m_PropertyNames.Add(CComBSTR(Property ? Property : L"")))
            return E_OUTOFMEMORY;
        if (!m_PropertyValues.Add(CComVariant()))
        {
            m_PropertyNames.RemoveAt(m_PropertyNames.GetSize() - 1);
            return E_OUTOFMEMORY;
        }
        i = m_PropertyValues.GetSize() - 1;
    }
    return m_PropertyValues[i].Copy(&vtValue);
}

HRESULT STDMETHODCALLTYPE CDesktopBrowser::GetProperty(BSTR Property, VARIANT *pvtValue)
{
    if (!pvtValue)
        return E_POINTER;
    VariantInit(pvtValue);
    int i = _FindProperty(Property);
    if (i < 0)
        return S_OK;
    return VariantCopy(pvtValue, &m_PropertyValues[i]);
}

HRESULT STDMETHODCALLTYPE CDesktopBrowser::get_Name(BSTR *Name)
{
    WCHAR szName[MAX_PATH];

    if (!Name)
        return E_POINTER;
    *Name = NULL;
    if (!LoadStringW(shell32_hInstance, IDS_FILE_EXPLORER, szName, _countof(szName)))
        return HRESULT_FROM_WIN32(GetLastError());
    *Name = SysAllocString(szName);
    return *Name ? S_OK : E_OUTOFMEMORY;
}

HRESULT STDMETHODCALLTYPE CDesktopBrowser::get_HWND(SHANDLE_PTR *pHWND)
{
    if (!pHWND)
        return E_POINTER;
    *pHWND = HandleToLong(m_hWnd);
    return *pHWND ? S_OK : E_UNEXPECTED;
}

HRESULT STDMETHODCALLTYPE CDesktopBrowser::get_FullName(BSTR *FullName)
{
    WCHAR szPath[MAX_PATH];

    if (!FullName)
        return E_POINTER;
    *FullName = NULL;
    if (!GetModuleFileNameW(NULL, szPath, _countof(szPath)))
        return HRESULT_FROM_WIN32(GetLastError());
    *FullName = SysAllocString(szPath);
    return *FullName ? S_OK : E_OUTOFMEMORY;
}

HRESULT STDMETHODCALLTYPE CDesktopBrowser::get_Path(BSTR *Path)
{
    WCHAR szPath[MAX_PATH];

    if (!Path)
        return E_POINTER;
    *Path = NULL;
    if (!GetModuleFileNameW(NULL, szPath, _countof(szPath)))
        return HRESULT_FROM_WIN32(GetLastError());
    PathRemoveFileSpecW(szPath);
    PathAddBackslashW(szPath);
    *Path = SysAllocString(szPath);
    return *Path ? S_OK : E_OUTOFMEMORY;
}

HRESULT STDMETHODCALLTYPE CDesktopBrowser::get_Visible(VARIANT_BOOL *pBool)
{
    if (!pBool)
        return E_POINTER;
    *pBool = VARIANT_FALSE;
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CDesktopBrowser::put_Visible(VARIANT_BOOL Value)
{
    return E_NOTIMPL;
}

HRESULT STDMETHODCALLTYPE CDesktopBrowser::get_StatusBar(VARIANT_BOOL *pBool)
{
    return E_NOTIMPL;
}

HRESULT STDMETHODCALLTYPE CDesktopBrowser::put_StatusBar(VARIANT_BOOL Value)
{
    return E_NOTIMPL;
}

HRESULT STDMETHODCALLTYPE CDesktopBrowser::get_StatusText(BSTR *StatusText)
{
    return E_NOTIMPL;
}

HRESULT STDMETHODCALLTYPE CDesktopBrowser::put_StatusText(BSTR StatusText)
{
    return E_NOTIMPL;
}

HRESULT STDMETHODCALLTYPE CDesktopBrowser::get_ToolBar(int *Value)
{
    if (!Value)
        return E_POINTER;
    *Value = 0;
    return S_FALSE;
}

HRESULT STDMETHODCALLTYPE CDesktopBrowser::put_ToolBar(int Value)
{
    return E_NOTIMPL;
}

HRESULT STDMETHODCALLTYPE CDesktopBrowser::get_MenuBar(VARIANT_BOOL *Value)
{
    return E_NOTIMPL;
}

HRESULT STDMETHODCALLTYPE CDesktopBrowser::put_MenuBar(VARIANT_BOOL Value)
{
    return E_NOTIMPL;
}

HRESULT STDMETHODCALLTYPE CDesktopBrowser::get_FullScreen(VARIANT_BOOL *pbFullScreen)
{
    if (!pbFullScreen)
        return E_POINTER;
    *pbFullScreen = VARIANT_FALSE;
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CDesktopBrowser::put_FullScreen(VARIANT_BOOL bFullScreen)
{
    return E_NOTIMPL;
}

HRESULT STDMETHODCALLTYPE CDesktopBrowser::Navigate2(VARIANT *URL, VARIANT *Flags, VARIANT *TargetFrameName,
                                                     VARIANT *PostData, VARIANT *Headers)
{
    return E_NOTIMPL;
}

HRESULT STDMETHODCALLTYPE CDesktopBrowser::QueryStatusWB(OLECMDID cmdID, OLECMDF *pcmdf)
{
    if (pcmdf)
        *pcmdf = (OLECMDF)0;
    return E_NOTIMPL;
}

HRESULT STDMETHODCALLTYPE CDesktopBrowser::ExecWB(OLECMDID cmdID, OLECMDEXECOPT cmdexecopt,
                                                  VARIANT *pvaIn, VARIANT *pvaOut)
{
    return E_NOTIMPL;
}

HRESULT STDMETHODCALLTYPE CDesktopBrowser::ShowBrowserBar(VARIANT *pvaClsid, VARIANT *pvarShow, VARIANT *pvarSize)
{
    return E_NOTIMPL;
}

HRESULT STDMETHODCALLTYPE CDesktopBrowser::get_ReadyState(READYSTATE *plReadyState)
{
    if (!plReadyState)
        return E_POINTER;
    *plReadyState = READYSTATE_UNINITIALIZED;
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CDesktopBrowser::get_Offline(VARIANT_BOOL *pbOffline)
{
    if (!pbOffline)
        return E_POINTER;
    *pbOffline = VARIANT_FALSE;
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CDesktopBrowser::put_Offline(VARIANT_BOOL bOffline)
{
    return E_NOTIMPL;
}

HRESULT STDMETHODCALLTYPE CDesktopBrowser::get_Silent(VARIANT_BOOL *pbSilent)
{
    if (!pbSilent)
        return E_POINTER;
    *pbSilent = VARIANT_FALSE;
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CDesktopBrowser::put_Silent(VARIANT_BOOL bSilent)
{
    return E_NOTIMPL;
}

HRESULT STDMETHODCALLTYPE CDesktopBrowser::get_RegisterAsBrowser(VARIANT_BOOL *pbRegister)
{
    if (!pbRegister)
        return E_POINTER;
    *pbRegister = VARIANT_FALSE;
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CDesktopBrowser::put_RegisterAsBrowser(VARIANT_BOOL bRegister)
{
    return E_NOTIMPL;
}

HRESULT STDMETHODCALLTYPE CDesktopBrowser::get_RegisterAsDropTarget(VARIANT_BOOL *pbRegister)
{
    if (!pbRegister)
        return E_POINTER;
    *pbRegister = VARIANT_FALSE;
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CDesktopBrowser::put_RegisterAsDropTarget(VARIANT_BOOL bRegister)
{
    return E_NOTIMPL;
}

HRESULT STDMETHODCALLTYPE CDesktopBrowser::get_TheaterMode(VARIANT_BOOL *pbRegister)
{
    if (!pbRegister)
        return E_POINTER;
    *pbRegister = VARIANT_FALSE;
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CDesktopBrowser::put_TheaterMode(VARIANT_BOOL bRegister)
{
    return E_NOTIMPL;
}

HRESULT STDMETHODCALLTYPE CDesktopBrowser::get_AddressBar(VARIANT_BOOL *Value)
{
    if (!Value)
        return E_POINTER;
    *Value = VARIANT_FALSE;
    return S_FALSE;
}

HRESULT STDMETHODCALLTYPE CDesktopBrowser::put_AddressBar(VARIANT_BOOL Value)
{
    return E_NOTIMPL;
}

HRESULT STDMETHODCALLTYPE CDesktopBrowser::get_Resizable(VARIANT_BOOL *Value)
{
    if (!Value)
        return E_POINTER;
    *Value = VARIANT_FALSE;
    return S_FALSE;
}

HRESULT STDMETHODCALLTYPE CDesktopBrowser::put_Resizable(VARIANT_BOOL Value)
{
    return E_NOTIMPL;
}

LRESULT CDesktopBrowser::_NotifyTray(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    HWND hWndTray;
    HRESULT hRet;

    hRet = m_Tray->GetTrayWindow(&hWndTray);
    if (SUCCEEDED(hRet))
        ::PostMessageW(hWndTray, uMsg, wParam, lParam);

    return 0;
}

LRESULT CDesktopBrowser::OnCommand(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL &bHandled)
{
    switch (LOWORD(wParam))
    {
        case FCIDM_DESKBROWSER_CLOSE:
            return _NotifyTray(TWM_DOEXITWINDOWS, 0, 0);
        case FCIDM_DESKBROWSER_FOCUS:
            if (GetKeyState(VK_SHIFT))
                return _NotifyTray(TWM_CYCLEFOCUS, 1, 0xFFFFFFFF);
            else
                return _NotifyTray(TWM_CYCLEFOCUS, 1, 1);
        case FCIDM_DESKBROWSER_SEARCH:
            SHFindFiles(NULL, NULL);
            break;
        case FCIDM_DESKBROWSER_REFRESH:
            if (m_ShellView)
                m_ShellView->Refresh();
            break;
        default:
            if (IsDesktopBrowserForwardShellViewCmd(LOWORD(wParam)) && m_hWndShellView)
                return SendMessageW(m_hWndShellView, uMsg, wParam, lParam);
            break;
    }
    return 0;
}


LRESULT CDesktopBrowser::OnEraseBkgnd(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL &bHandled)
{
    return (LRESULT)PaintDesktop((HDC)wParam);
}

LRESULT CDesktopBrowser::OnSize(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL &bHandled)
{
    if (wParam == SIZE_MINIMIZED)
    {
        /* Hey, we're the desktop!!! */
        ::ShowWindow(m_hWnd, SW_RESTORE);
    }

    ::InvalidateRect(m_hWndShellView, NULL, TRUE);

    return 0;
}

LRESULT CDesktopBrowser::OnSettingChange(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL &bHandled)
{
    if (uMsg == WM_SETTINGCHANGE /* == WM_WININICHANGE */ &&
        lstrcmpiW((LPCWSTR)lParam, L"Environment") == 0)
    {
        LPVOID lpEnvironment;
        RegenerateUserEnvironment(&lpEnvironment, TRUE);
    }
    SHSettingsChanged((LPCVOID)wParam, (PCWSTR)lParam); // Invalidate cached restrictions

    if (m_hWndShellView)
    {
        /* Forward the message */
        ::SendMessageW(m_hWndShellView, uMsg, wParam, lParam);
    }

    if (uMsg == WM_SETTINGCHANGE && wParam == SPI_SETWORKAREA && m_hWndShellView != NULL)
    {
        _Resize();
    }

    return 0;
}

LRESULT CDesktopBrowser::OnDestroy(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL &bHandled)
{
    if (m_lShellWindowsCookie)
    {
        WinList_Revoke(m_lShellWindowsCookie);
        m_lShellWindowsCookie = 0;
    }
    if (m_hPortableDeviceNotify)
    {
        UnregisterDeviceNotification(m_hPortableDeviceNotify);
        m_hPortableDeviceNotify = NULL;
    }
    bHandled = FALSE;
    return 0;
}

LRESULT CDesktopBrowser::OnClose(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL &bHandled)
{
    return _NotifyTray(TWM_DOEXITWINDOWS, 0, 0);
}

LRESULT CDesktopBrowser::OnOpenNewWindow(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL &bHandled)
{
    TRACE("Proxy Desktop message 1035 received.\n");
    SHOnCWMCommandLine((HANDLE)lParam);
    return 0;
}

LRESULT CDesktopBrowser::OnSetFocus(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL &bHandled)
{
    ::SetFocus(m_hWndShellView);
    return 0;
}

// Message WM_DESKTOP_GET_CNOTIFY_SERVER: Get or create the change notification server.
//   wParam: BOOL bCreate; The flag whether it creates or not.
//   lParam: Ignored.
//   return: The window handle of the server window.
LRESULT CDesktopBrowser::OnGetChangeNotifyServer(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL &bHandled)
{
    BOOL bCreate = (BOOL)wParam;
    if (bCreate && !::IsWindow(m_hwndChangeNotifyServer))
    {
        HRESULT hres = CChangeNotifyServer_CreateInstance(IID_PPV_ARG(IOleWindow, &m_ChangeNotifyServer));
        if (FAILED_UNEXPECTEDLY(hres))
            return NULL;

        hres = m_ChangeNotifyServer->GetWindow(&m_hwndChangeNotifyServer);
        if (FAILED_UNEXPECTEDLY(hres))
            return NULL;
    }
    return (LRESULT)m_hwndChangeNotifyServer;
}

// Detect DBT_DEVICEARRIVAL and DBT_DEVICEREMOVECOMPLETE
LRESULT CDesktopBrowser::OnDeviceChange(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL &bHandled)
{
    if (wParam != DBT_DEVICEARRIVAL && wParam != DBT_DEVICEREMOVECOMPLETE)
        return 0;

    PDEV_BROADCAST_HDR pHeader = (PDEV_BROADCAST_HDR)lParam;
    if (pHeader && pHeader->dbch_devicetype == DBT_DEVTYP_DEVICEINTERFACE &&
        IsEqualGUID(((PDEV_BROADCAST_DEVICEINTERFACE_W)pHeader)->dbcc_classguid, GUID_DEVINTERFACE_WPD_SHELL))
    {
        CComHeapPtr<ITEMIDLIST> pidlDrives;
        if (SUCCEEDED(SHGetSpecialFolderLocation(NULL, CSIDL_DRIVES, &pidlDrives)))
            SHChangeNotify(SHCNE_UPDATEDIR, SHCNF_IDLIST, pidlDrives, NULL);
        return 0;
    }

    DWORD dwDrives = ::GetLogicalDrives();
    for (INT iDrive = 0; iDrive <= 'Z' - 'A'; ++iDrive)
    {
        WCHAR szPath[MAX_PATH];
        DWORD dwBit = (1 << iDrive);
        if (!(m_dwDrives & dwBit) && (dwDrives & dwBit)) // The drive is added
        {
            PathBuildRootW(szPath, iDrive);
            SHChangeNotify(SHCNE_DRIVEADD, SHCNF_PATHW, szPath, NULL);
        }
        else if ((m_dwDrives & dwBit) && !(dwDrives & dwBit)) // The drive is removed
        {
            PathBuildRootW(szPath, iDrive);
            SHChangeNotify(SHCNE_DRIVEREMOVED, SHCNF_PATHW, szPath, NULL);
        }
    }

    m_dwDrives = dwDrives;
    return 0;
}

extern VOID WINAPI ShowFolderOptionsDialog(UINT Page, BOOL Async);

LRESULT CDesktopBrowser::OnShowOptionsDlg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL &bHandled)
{
    switch (wParam)
    {
        case 0:
#if (NTDDI_VERSION >= NTDDI_VISTA)
        case 2:
        case 7:
#endif
            ShowFolderOptionsDialog((UINT)(UINT_PTR)wParam, TRUE);
            break;
        case 1:
            _NotifyTray(WM_COMMAND, TRAYCMD_TASKBAR_PROPERTIES, 0);
            break;
    }
    return 0;
}

LRESULT CDesktopBrowser::OnSaveState(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL &bHandled)
{
    if (m_ShellView && !SHRestricted(REST_NOSAVESET))
        m_ShellView->SaveViewState();
    return 0;
}

HRESULT CDesktopBrowser_CreateInstance(IShellDesktopTray *Tray, REFIID riid, void **ppv)
{
    return ShellObjectCreatorInit<CDesktopBrowser, IShellDesktopTray*>(Tray, riid, ppv);
}

/*************************************************************************
 * SHCreateDesktop            [SHELL32.200]
 *
 */
HANDLE WINAPI SHCreateDesktop(IShellDesktopTray *Tray)
{
    if (Tray == NULL)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return NULL;
    }

    CComPtr<IShellBrowser> Browser;
    HRESULT hr = CDesktopBrowser_CreateInstance(Tray, IID_PPV_ARG(IShellBrowser, &Browser));
    if (FAILED_UNEXPECTEDLY(hr))
        return NULL;

    return static_cast<HANDLE>(Browser.Detach());
}

/*************************************************************************
 * SHCreateDesktop            [SHELL32.201]
 *
 */
BOOL WINAPI SHDesktopMessageLoop(HANDLE hDesktop)
{
    if (hDesktop == NULL)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    MSG Msg;
    BOOL bRet;

    CComPtr<IShellBrowser> browser;
    CComPtr<IShellView> shellView;

    browser.Attach(static_cast<IShellBrowser*>(hDesktop));
    HRESULT hr = browser->QueryActiveShellView(&shellView);
    if (FAILED_UNEXPECTEDLY(hr))
        return FALSE;

    while ((bRet = ::GetMessageW(&Msg, NULL, 0, 0)) != 0)
    {
        if (bRet != -1)
        {
            if (shellView->TranslateAcceleratorW(&Msg) != S_OK)
            {
                ::TranslateMessage(&Msg);
                ::DispatchMessageW(&Msg);
            }
        }
    }

    return TRUE;
}

/*************************************************************************
 *  SHIsTempDisplayMode [SHELL32.724]
 *
 * Is the current display settings temporary?
 */
EXTERN_C BOOL WINAPI SHIsTempDisplayMode(VOID)
{
    TRACE("\n");

    if (GetSystemMetrics(SM_REMOTESESSION) || GetSystemMetrics(SM_REMOTECONTROL))
        return FALSE;

    DEVMODEW DevMode;
    ZeroMemory(&DevMode, sizeof(DevMode));
    DevMode.dmSize = sizeof(DevMode);

    if (!EnumDisplaySettingsW(NULL, ENUM_REGISTRY_SETTINGS, &DevMode))
        return FALSE;

    if (!DevMode.dmPelsWidth || !DevMode.dmPelsHeight)
        return FALSE;

    HDC hDC = GetDC(NULL);
    DWORD cxWidth = GetDeviceCaps(hDC, HORZRES);
    DWORD cyHeight = GetDeviceCaps(hDC, VERTRES);
    ReleaseDC(NULL, hDC);

    return (cxWidth != DevMode.dmPelsWidth || cyHeight != DevMode.dmPelsHeight);
}
