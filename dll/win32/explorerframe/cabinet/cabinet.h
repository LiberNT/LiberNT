/*
 * PROJECT:     LiberNT Explorer
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     File Explorer window declarations
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

#define COBJMACROS
#include <windows.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <shlwapi.h>
#include <shellapi.h>
#include <commctrl.h>
#include <uxtheme.h>
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
    int IconIndex;
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
    Preview,
    Properties,
    NewTab,
    Go,
    Count
};

HRESULT CabinetSite_Create(CCabinetWindow *window, IExplorerBrowser *browser, IUnknown **site);

class CCabinetWindow
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

private:
    static LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    static LRESULT CALLBACK TabProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam, UINT_PTR id, DWORD_PTR data);
    LRESULT HandleMessage(UINT msg, WPARAM wParam, LPARAM lParam);
    LRESULT OnNotify(NMHDR *header);
    void OnCreate();
    void OnSize();
    int Scale(int value) const;

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
    void UpdateAddress();
    void NavigateToAddress();
    void UpdateStatus();
    void UpdateCommands();
    IShellView *CurrentView();
    BOOL CommandEnabled(CabinetCommand command);
    void ExecuteCommand(CabinetCommand command);
    void InvokeSelectionVerb(LPCSTR verb);
    void InvokeBackgroundVerb(LPCSTR verb);
    void ShowNewMenu();
    void ShowSortMenu();
    void ShowViewMenu();
    void ShowMoreMenu();
    void ShowChevronMenu(const NMREBARCHEVRON *chevron);
    RECT MenuAnchor(CabinetCommand command);
    void LoadText(UINT id, LPWSTR buffer, int length);
    void CreateBands();
    void CreateNavigationPane();
    void SyncNavigationPane();
    void WatchFolder();
    void UpdateDetails();
    void ShowTabMenu(int index, POINT screen);
    void StartSearch();
    void RunPendingSearch(CabinetTab *tab);
    void RefreshAllViews();
    void SetPane(BOOL details, BOOL preview);
    void ClearPreview();
    void UpdatePreview();
    void PlacePanes();
    void ShowPreviewMessage(UINT id);
    BOOL ShowPreviewHandler(IShellItem *item, PCWSTR path);
    BOOL ShowPreviewThumbnail(IShellItem *item);
    BOOL ShowPreviewImage(PCWSTR path);
    BOOL ShowPreviewText(PCWSTR path);
    void SetPreviewBitmap(HBITMAP bitmap);
    void SyncTabs();
    void MoveTab(int from, int to);
    void FocusView();
    void RestoreFocus();
    void CycleFocus();
    void SavePlacement();
    void UpdateMaterial();

    HWND m_hwnd;
    CabinetTab m_Tabs[CABINET_MAX_TABS];
    int m_TabCount;
    int m_ActiveTab;
    HWND m_Rebar;
    HWND m_TabControl;
    HWND m_TabTools;
    HWND m_NavBar;
    HWND m_Address;
    HWND m_AddressEdit;
    HWND m_GoBar;
    HWND m_SearchEdit;
    HWND m_CommandBar;
    HWND m_Status;
    HWND m_DetailsIcon;
    HWND m_DetailsName;
    HWND m_DetailsList;
    HWND m_PreviewHost;
    HWND m_PreviewText;
    HWND m_PreviewImage;
    HWND m_PreviewLabel;
    HIMAGELIST m_ToolbarImages;
    HIMAGELIST m_ToolbarHotImages;
    HIMAGELIST m_GoImages;
    HFONT m_Font;
    INameSpaceTreeControl *m_Navigation;
    HWND m_NavigationWindow;
    HWND m_FocusSave;
    RECT m_ViewRect;
    RECT m_PaneRect;
    int m_DragTab;
    BOOL m_DetailsPane;
    BOOL m_PreviewPane;
    IPreviewHandler *m_PreviewHandler;
    HBITMAP m_PreviewBitmap;
    PIDLIST_ABSOLUTE m_PreviewPidl;
    HICON m_DetailsIconHandle;
    BOOL m_InHistoryNavigation;
    ULONG m_ChangeNotify;
    BOOL m_NavigationVisible;
    IExplorerBrowser *m_SearchBrowser;
    WCHAR m_SearchQuery[MAX_PATH];
    WCHAR m_SearchPaths[26][MAX_PATH];
    int m_SearchPathCount;
    BOOL m_SearchActive;
    HBRUSH m_ContentBrush;
};

HIMAGELIST LoadToolbarImages(UINT bitmapId);
HRESULT CabinetNavigation_Create(CCabinetWindow *window, IUnknown **events);
HRESULT CabinetViewEvents_Create(CCabinetWindow *window, IUnknown **events);
HRESULT Cabinet_GetDefaultFolder(PIDLIST_ABSOLUTE *pidl);
HRESULT ShellFind_StartSearch(HWND hwndView, PCWSTR *rgszPaths, SIZE_T cPaths, PCWSTR pszQuery);
HRESULT Cabinet_RunWindow(PCIDLIST_ABSOLUTE pidl, PCUITEMID_CHILD select, int showCmd);
HRESULT Cabinet_OpenWindow(PCIDLIST_ABSOLUTE pidl, PCUITEMID_CHILD select, int showCmd);
