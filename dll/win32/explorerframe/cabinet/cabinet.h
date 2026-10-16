/*
 * PROJECT:     LiberNT Explorer
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     File Explorer window declarations
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

#define COBJMACROS
#include <shellframe.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <shlwapi.h>
#include <shellapi.h>
#include <commctrl.h>
#include <strsafe.h>
#include <propkey.h>
#include "cabinet_res.h"

#ifndef IID_PPV_ARG
#define IID_PPV_ARG(Itype, ppType) IID_##Itype, reinterpret_cast<void **>((static_cast<Itype **>(ppType)))
#endif

#define CABINET_CLASS L"CabinetWClass"
#define CABINET_MAX_TABS 32
#define CABINET_MAX_HISTORY 64
#define CABINET_MAX_CRUMBS 32
#define CABINET_MAX_DETAILS 16

class CCabinetWindow;

struct CabinetTab
{
    IExplorerBrowser *Browser;
    IUnknown *Site;
    DWORD EventsCookie;
    IConnectionPoint *ViewEvents;
    DWORD ViewEventsCookie;
    PIDLIST_ABSOLUTE Pidl;
    PIDLIST_ABSOLUTE History[CABINET_MAX_HISTORY];
    int HistoryCount;
    int HistoryIndex;
    WCHAR Title[MAX_PATH];
    HICON Icon;
    int Width;
};

struct CabinetDetails
{
    WCHAR Name[MAX_PATH];
    WCHAR Type[128];
    HICON Icon;
    int Count;
    WCHAR Labels[CABINET_MAX_DETAILS][64];
    WCHAR Values[CABINET_MAX_DETAILS][256];
};

struct CabinetCrumb
{
    PIDLIST_ABSOLUTE Pidl;
    WCHAR Name[MAX_PATH];
    RECT Rect;
    RECT Chevron;
};

enum class CabinetCommand
{
    None = 0,
    Back,
    Forward,
    Up,
    Refresh,
    NewItem,
    Cut,
    Copy,
    Paste,
    Rename,
    Share,
    Delete,
    Sort,
    View,
    More,
    Details,
    ViewDetails,
    ViewTiles,
    NewTab,
    Count
};

HRESULT CabinetSite_Create(CCabinetWindow *window, IExplorerBrowser *browser, IUnknown **site);

class CCabinetWindow : public ShellFrame::FrameWindow
{
public:
    CCabinetWindow();
    ~CCabinetWindow();

    HRESULT Open(PCIDLIST_ABSOLUTE pidl, PCUITEMID_CHILD select, int showCmd);
    BOOL TranslateAcceleratorMessage(MSG *msg);

    void OnNavigationComplete(IExplorerBrowser *browser, PCIDLIST_ABSOLUTE pidl);
    void OnNavigationFailed(IExplorerBrowser *browser, PCIDLIST_ABSOLUTE pidl);
    void OnViewCreated(IExplorerBrowser *browser, IShellView *view);
    void OnSelectionChanged();
    HRESULT OnDefaultCommand(IShellView *view);
    void OnNavigationItem(IShellItem *item, BOOL newTab);
    void OnViewEvent(DISPID dispid);

protected:
    LRESULT HandleMessage(UINT msg, WPARAM wParam, LPARAM lParam) override;
    void OnCreate() override;
    void OnLayout(const RECT &client) override;
    void OnPaint(ShellFrame::Canvas &canvas, const RECT &client) override;
    void OnThemeChanged() override;
    BOOL IsCaptionClient(POINT pt) override;
    int CaptionHeight() const override;
    int CaptionButtonHeight() const override;

private:
    struct Layout
    {
        RECT Tabs;
        RECT NewTab;
        RECT AddressBand;
        RECT Address;
        RECT Search;
        RECT CommandBand;
        RECT Navigation;
        RECT View;
        RECT Status;
        RECT Details;
        RECT Buttons[(int)CabinetCommand::Count];
    };

    CabinetTab *ActiveTab() { return m_ActiveTab >= 0 ? &m_Tabs[m_ActiveTab] : NULL; }
    int AddTab(PCIDLIST_ABSOLUTE pidl, BOOL activate);
    void CloseTab(int index);
    void ActivateTab(int index);
    void PlaceBrowsers();
    void Navigate(PCIDLIST_ABSOLUTE pidl);
    void NavigateHistory(int delta);
    void NavigateUp();
    void Refresh();
    void UpdateTitle(CabinetTab *tab);
    void BuildCrumbs();
    void FreeCrumbs();
    void BeginAddressEdit();
    void EndAddressEdit(BOOL navigate);
    void UpdateStatus();
    void ComputeLayout(const RECT &client);
    IShellView *CurrentView();
    BOOL CommandEnabled(CabinetCommand command);
    void ExecuteCommand(CabinetCommand command);
    void InvokeSelectionVerb(LPCSTR verb);
    void InvokeBackgroundVerb(LPCSTR verb);
    void ShowNewMenu();
    void ShowSortMenu();
    void ShowViewMenu();
    void ShowMoreMenu();
    void ShowCrumbMenu(int index);
    void PaintTabs(ShellFrame::Canvas &canvas);
    void PaintAddressBand(ShellFrame::Canvas &canvas);
    void PaintCommandBand(ShellFrame::Canvas &canvas);
    void PaintStatus(ShellFrame::Canvas &canvas);
    void PaintButton(ShellFrame::Canvas &canvas, CabinetCommand command, LPCWSTR icon, LPCWSTR label, BOOL dropdown);
    int HitTab(POINT pt, BOOL *close);
    CabinetCommand HitCommand(POINT pt);
    int HitCrumb(POINT pt, BOOL *chevron);
    void SetHot(int tab, BOOL tabClose, CabinetCommand command, int crumb, BOOL crumbChevron);
    void LoadText(UINT id, LPWSTR buffer, int length);
    RECT MenuAnchor(CabinetCommand command);
    void CreateNavigationPane();
    void SyncNavigationPane();
    void ApplyChildColors();
    void Relayout();
    void WatchFolder();
    void UpdateDetails();
    void FreeDetails();
    void PaintDetails(ShellFrame::Canvas &canvas);
    void ShowTabMenu(int index, POINT screen);
    void StartSearch();
    void RunPendingSearch(CabinetTab *tab);
    void RefreshAllViews();
    void ShowCrumbOverflowMenu();
    void FocusView();
    void RestoreFocus();
    void UpdateTooltips();
    void CycleFocus();
    void SavePlacement();

    Layout m_Layout;
    CabinetTab m_Tabs[CABINET_MAX_TABS];
    int m_TabCount;
    int m_ActiveTab;
    INameSpaceTreeControl *m_Navigation;
    HWND m_NavigationWindow;
    HWND m_AddressEdit;
    HWND m_SearchEdit;
    HWND m_FocusSave;
    HWND m_Tooltip;
    HFONT m_EditFont;
    CabinetCrumb m_Crumbs[CABINET_MAX_CRUMBS];
    int m_CrumbCount;
    int m_CrumbFirst;
    RECT m_CrumbOverflow;
    HICON m_LocationIcon;
    int m_HotTab;
    BOOL m_HotTabClose;
    CabinetCommand m_HotCommand;
    CabinetCommand m_PressedCommand;
    int m_HotCrumb;
    BOOL m_HotCrumbChevron;
    BOOL m_TrackingClient;
    BOOL m_DetailsPane;
    WCHAR m_StatusText[128];
    HICON m_SmallIcon;
    HICON m_BigIcon;
    BOOL m_InHistoryNavigation;
    ULONG m_ChangeNotify;
    CabinetDetails m_Details;
    BOOL m_NavigationVisible;
    IExplorerBrowser *m_SearchBrowser;
    WCHAR m_SearchQuery[MAX_PATH];
    WCHAR m_SearchPaths[26][MAX_PATH];
    int m_SearchPathCount;
    BOOL m_SearchActive;
};

HRESULT CabinetNavigation_Create(CCabinetWindow *window, IUnknown **events);
HRESULT CabinetViewEvents_Create(CCabinetWindow *window, IUnknown **events);
HRESULT Cabinet_GetDefaultFolder(PIDLIST_ABSOLUTE *pidl);
HRESULT ShellFind_StartSearch(HWND hwndView, PCWSTR *rgszPaths, SIZE_T cPaths, PCWSTR pszQuery);
HRESULT Cabinet_RunWindow(PCIDLIST_ABSOLUTE pidl, PCUITEMID_CHILD select, int showCmd);
HRESULT Cabinet_OpenWindow(PCIDLIST_ABSOLUTE pidl, PCUITEMID_CHILD select, int showCmd);
