/*
 * PROJECT:     ReactOS Management Console
 * LICENSE:     GPL-2.0+ (https://spdx.org/licenses/GPL-2.0+)
 * PURPOSE:     Single 'console' window
 * COPYRIGHT:   Copyright 2006-2007 Thomas Weidenmueller
 *              Copyright 2017 Mark Jansen (mark.jansen@reactos.org)
 *              Copyright 2026 Eric Kohl (eric.kohl@reactos.org)
 */

#pragma once

class CMainWnd;

class CConsoleWnd :
    public CWindowImpl<CConsoleWnd>,
    public IConsole2,
    public IConsoleNameSpace2
{
private:
    UINT m_ViewId;
    CMainWnd* m_MainWnd;
    CTreeView m_TreeView;
    CListView m_ListView;
    CStatusBar m_StatusBar;
    CWindow m_DescriptionBar;
    int m_iTreeViewWidth;
    int m_iActionsPaneWidth;
    int m_iSplitterWidth;
    int m_iSplitSide;
    int m_iStatusBarHeight;
    BOOL m_bTreeViewVisible;
    BOOL m_bActionsPaneVisible;
    BOOL m_bStatusBarVisible;
    BOOL m_bDescriptionBarVisible;

    HMENU m_hMenuTreeView;

    LISTVIEW_MODE m_ListViewMode;

    CSnapin *m_ViewRootNode;
    CSnapin *m_ViewSelectedNode;

public:
    BEGIN_MSG_MAP(CConsoleWnd)
        MESSAGE_HANDLER(WM_CREATE, OnCreate)
        MESSAGE_HANDLER(WM_DESTROY, OnDestroy)
        MESSAGE_HANDLER(WM_SIZE, OnSize)
        MESSAGE_HANDLER(WM_CONTEXTMENU, OnContextMenu)
        MESSAGE_HANDLER(WM_DRAWITEM, OnDrawItem)
        MESSAGE_HANDLER(WM_NOTIFY, OnNotify)
        MESSAGE_HANDLER(WM_ACTIVATE, OnActivate)

        COMMAND_ID_HANDLER(IDM_ACTION_NEW, OnActionNewWindow)
        COMMAND_ID_HANDLER(IDM_ACTION_RENAME, OnActionRename)
        COMMAND_ID_HANDLER(IDM_VIEW_LARGE_ICONS, OnViewMode)
        COMMAND_ID_HANDLER(IDM_VIEW_SMALL_ICONS, OnViewMode)
        COMMAND_ID_HANDLER(IDM_VIEW_LIST, OnViewMode)
        COMMAND_ID_HANDLER(IDM_VIEW_DETAILS, OnViewMode)
        COMMAND_ID_HANDLER(IDM_VIEW_CUSTOMIZE, OnViewCustomize)
    END_MSG_MAP()


    static CWndClassInfo& GetWndClassInfo()
    {
        static CWndClassInfo wc =
        {
            {
                /* cbSize= */sizeof(WNDCLASSEX),
                /* style= */CS_HREDRAW | CS_VREDRAW,
                /* lpfnWndProc= */StartWindowProc,
                /* cbClsExtra= */0,
                /* cbWndExtra= */sizeof(CConsoleWnd*),
                /* hInstance= */NULL,
                /* hIcon= */LoadIcon(_AtlBaseModule.GetModuleInstance(), MAKEINTRESOURCE(IDI_MAINAPP)),
                /* hCursor= */NULL,
                /* hbrBackground= */(HBRUSH)(COLOR_3DFACE + 1),
                /* lpszMenuName= */NULL,
                /* lpszClassName= */L"MMCChildFrm",
                /* hIconSm= */LoadIcon(_AtlBaseModule.GetModuleInstance(), MAKEINTRESOURCE(IDI_MAINAPP))
            },
            NULL, NULL, IDC_SIZEWE, TRUE, 0, L""
        };
        return wc;
    }

    static LPCWSTR GetWndClassName()
    {
        return GetWndClassInfo().m_wc.lpszClassName;
    }

private:
    VOID UpdateLayout();
    VOID AddTreeViewItemRecursive(HTREEITEM hParentTreeItem, CSnapin *Node);
    VOID UpdateTreeView();

public:
    CConsoleWnd(CMainWnd *MainWnd, CSnapin *RootNode, CSnapin *SelectedNode = NULL);
    ~CConsoleWnd();

    LRESULT OnCreate(UINT nMessage, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnDestroy(UINT nMessage, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnSize(UINT nMessage, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnContextMenu(UINT nMessage, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnDrawItem(UINT nMessage, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnNotify(UINT nMessage, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnActivate(UINT nMessage, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    LRESULT OnActionNewWindow(WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL& bHandled);
    LRESULT OnActionRename(WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL& bHandled);
    LRESULT OnViewMode(WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL& bHandled);
    LRESULT OnViewCustomize(WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL& bHandled);

    BOOL IsTreeViewVisible();
    VOID SetTreeViewVisible(BOOL bVisible);
    BOOL IsStatusBarVisible();
    VOID SetStatusBarVisible(BOOL bVisible);
    BOOL IsDescriptionBarVisible();
    VOID SetDescriptionBarVisible(BOOL bVisible);
    BOOL IsActionsPaneVisible();
    VOID SetActionsPaneVisible(BOOL bVisible);

    VOID UpdateView();
    VOID SetStatusBarText(LPWSTR pszStatusText);
    VOID SelectParent();

    LISTVIEW_MODE GetListViewMode();
    VOID SetListViewMode(LISTVIEW_MODE ListViewMode);

    VOID SaveView(MscFile *mscFile, IXMLDOMElement *pParentElement);

    // +IConsole
    STDMETHODIMP QueryInterface(REFIID riid, void **ppvObject) override;
    STDMETHODIMP_(ULONG) AddRef() override;
    STDMETHODIMP_(ULONG) Release() override;

    STDMETHODIMP SetHeader(LPHEADERCTRL pHeader) override;
    STDMETHODIMP SetToolbar(LPTOOLBAR pToolbar) override;
    STDMETHODIMP QueryResultView(LPUNKNOWN *pUnknown) override;
    STDMETHODIMP QueryScopeImageList(LPIMAGELIST *ppImageList) override;
    STDMETHODIMP QueryResultImageList(LPIMAGELIST *ppImageList) override;
    STDMETHODIMP UpdateAllViews(LPDATAOBJECT lpDataObject, LPARAM data, LONG_PTR hint) override;
    STDMETHODIMP MessageBox(LPCWSTR lpszText, LPCWSTR lpszTitle, UINT fuStyle, int *piRetval) override;
    STDMETHODIMP QueryConsoleVerb(LPCONSOLEVERB *ppConsoleVerb) override;
    STDMETHODIMP SelectScopeItem(HSCOPEITEM hScopeItem) override;
    STDMETHODIMP GetMainWindow(HWND *phwnd) override;
    STDMETHODIMP NewWindow(HSCOPEITEM hScopeItem, ULONG lOptions) override;
    // -IConsole

    // +IConsole2
    STDMETHODIMP Expand(HSCOPEITEM hItem, BOOL bExpand) override;
    STDMETHODIMP IsTaskpadViewPreferred() override;
    STDMETHODIMP SetStatusText(LPOLESTR pszStatusText) override;
    // -IConsole2

    // +IConsoleNameSpace
    STDMETHODIMP InsertItem(LPSCOPEDATAITEM item) override;
    STDMETHODIMP DeleteItem(HSCOPEITEM hItem, LONG fDeleteThis) override;
    STDMETHODIMP SetItem(LPSCOPEDATAITEM item) override;
    STDMETHODIMP GetItem(LPSCOPEDATAITEM item) override;
    STDMETHODIMP GetChildItem(HSCOPEITEM item, HSCOPEITEM *pItemChild, MMC_COOKIE *plCookie) override;
    STDMETHODIMP GetNextItem(HSCOPEITEM item, HSCOPEITEM *pItemNext, MMC_COOKIE *plCookie) override;
    STDMETHODIMP GetParentItem(HSCOPEITEM item, HSCOPEITEM *pItemParent, MMC_COOKIE *plCookie) override;
    // -IConsoleNameSpace

    // +IConsoleNameSpace2
    STDMETHODIMP Expand(HSCOPEITEM hItem) override;
    STDMETHODIMP AddExtension(HSCOPEITEM hItem, LPCLSID lpClsid) override;
    // -IConsoleNameSpace2
};

