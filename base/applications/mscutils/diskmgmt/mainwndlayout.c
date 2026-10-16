/*
 * PROJECT:     LiberNT Disk Management
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Main-window layout and splitter helpers.
 */

#include "precomp.h"

VOID
DmMainWndGetCurrentLayoutPanes(
    _In_ PMAIN_WND_INFO Info,
    _Out_ PDM_LAYOUT_PANES Panes)
{
    RECT rcClient;
    RECT rcStatus;
    RECT rcToolbar;
    INT StatusHeight;
    INT ToolbarHeight;

    StatusHeight = 0;
    ToolbarHeight = 0;
    GetClientRect(Info->hMainWnd, &rcClient);

    /* Use the WS_VISIBLE style bit instead of IsWindowVisible().
       IsWindowVisible() walks the parent chain and returns FALSE during
       WM_CREATE because the top-level window hasn't been shown yet. */
    if (Info->hToolbar != NULL &&
        (GetWindowLongPtrW(Info->hToolbar, GWL_STYLE) & WS_VISIBLE))
    {
        SendMessageW(Info->hToolbar, TB_AUTOSIZE, 0, 0);
        GetWindowRect(Info->hToolbar, &rcToolbar);
        ToolbarHeight = rcToolbar.bottom - rcToolbar.top;
    }

    if (Info->hStatusBar != NULL && Info->bStatusBarVisible)
    {
        SendMessageW(Info->hStatusBar, WM_SIZE, 0, 0);
        GetWindowRect(Info->hStatusBar, &rcStatus);
        StatusHeight = rcStatus.bottom - rcStatus.top;
    }

    Info->LayoutMetrics.ToolbarVisible = (Info->hToolbar != NULL &&
                                         (GetWindowLongPtrW(Info->hToolbar, GWL_STYLE) & WS_VISIBLE));
    Info->LayoutMetrics.ToolbarHeight = ToolbarHeight;
    Info->LayoutMetrics.StatusBarVisible = Info->bStatusBarVisible;
    Info->LayoutMetrics.StatusBarHeight = StatusHeight;
    DmLayoutComputePanes(&rcClient,
                         &Info->LayoutMetrics,
                         Panes);
}

VOID
DmMainWndLayoutMainWindow(
    _In_ PMAIN_WND_INFO Info)
{
    DM_LAYOUT_PANES Panes;
    HDWP hdwp;

    DmMainWndGetCurrentLayoutPanes(Info, &Panes);

    if (Info->hMenu != NULL)
    {
        UINT View;

        for (View = DM_PANE_DISK_LIST; View <= DM_PANE_HIDDEN; View++)
        {
            if (View != DM_PANE_HIDDEN)
            {
                CheckMenuItem(Info->hMenu,
                              IDM_VIEW_TOP_DISK_LIST + View,
                              MF_BYCOMMAND | (View == Info->TopView ? MF_CHECKED : MF_UNCHECKED));
            }
            CheckMenuItem(Info->hMenu,
                          IDM_VIEW_BOTTOM_DISK_LIST + View,
                          MF_BYCOMMAND | (View == Info->BottomView ? MF_CHECKED : MF_UNCHECKED));
        }
    }

    hdwp = BeginDeferWindowPos(4);
    if (hdwp != NULL && Info->hToolbar != NULL)
    {
        hdwp = DeferWindowPos(hdwp,
                              Info->hToolbar,
                              NULL,
                              Panes.ToolbarRect.left,
                              Panes.ToolbarRect.top,
                              Panes.ToolbarRect.right - Panes.ToolbarRect.left,
                              Panes.ToolbarRect.bottom - Panes.ToolbarRect.top,
                              SWP_NOZORDER | SWP_NOACTIVATE);
    }
    {
        HWND Views[3] = { Info->hDiskListView, Info->hVolumeView, Info->hDiskView };
        RECT TopRect = Panes.TopRect;
        const RECT *Rect;
        UINT Index;

        if (Info->BottomView == DM_PANE_HIDDEN)
            TopRect.bottom = Panes.BottomRect.bottom;

        for (Index = 0; Index < ARRAYSIZE(Views) && hdwp != NULL; Index++)
        {
            if (Views[Index] == NULL)
                continue;

            if (Index == Info->TopView)
                Rect = &TopRect;
            else if (Index == Info->BottomView)
                Rect = &Panes.BottomRect;
            else
            {
                ShowWindow(Views[Index], SW_HIDE);
                continue;
            }

            hdwp = DeferWindowPos(hdwp,
                                  Views[Index],
                                  NULL,
                                  Rect->left,
                                  Rect->top,
                                  Rect->right - Rect->left,
                                  Rect->bottom - Rect->top,
                                  SWP_NOZORDER | SWP_NOACTIVATE | SWP_SHOWWINDOW);
        }
    }
    if (hdwp != NULL)
        EndDeferWindowPos(hdwp);

    DmMainWndUpdateDiskViewScrollInfo(Info);
}

VOID
DmMainWndSetPaneView(
    _In_ PMAIN_WND_INFO Info,
    _In_ BOOL Top,
    _In_ UINT View)
{
    if (Info == NULL || View > DM_PANE_HIDDEN || (Top && View == DM_PANE_HIDDEN))
        return;

    if (Top)
    {
        if (View == Info->BottomView)
            Info->BottomView = Info->TopView;
        Info->TopView = View;
    }
    else
    {
        if (View == Info->TopView)
        {
            if (Info->BottomView != DM_PANE_HIDDEN)
                Info->TopView = Info->BottomView;
            else
                Info->TopView = (View == DM_PANE_VOLUME_LIST) ? DM_PANE_GRAPHICAL : DM_PANE_VOLUME_LIST;
        }
        Info->BottomView = View;
    }

    DmMainWndLayoutMainWindow(Info);
    InvalidateRect(Info->hMainWnd, NULL, TRUE);
}

BOOL
DmMainWndBeginSplitterDrag(
    _In_ PMAIN_WND_INFO Info,
    _In_ const POINT *Point)
{
    DM_LAYOUT_PANES Panes;
    DM_SPLITTER_HIT Hit;
    RECT ClientRect;

    DmMainWndGetCurrentLayoutPanes(Info, &Panes);
    Hit = DmLayoutHitTestSplitter(Point, &Panes);
    if (Hit == DmSplitterHitNone)
        return FALSE;

    GetClientRect(Info->hMainWnd, &ClientRect);
    if (!DmSplitterBeginDrag(&Info->SplitterState,
                             &Info->LayoutMetrics,
                             &Panes,
                             Hit,
                             Point,
                             &ClientRect))
    {
        return FALSE;
    }

    SetCapture(Info->hMainWnd);
    DmSplitterSetCursor(Hit);
    return TRUE;
}

VOID
DmMainWndUpdateSplitterDrag(
    _In_ PMAIN_WND_INFO Info,
    _In_ const POINT *Point)
{
    DM_LAYOUT_METRICS Metrics;

    if (!DmSplitterUpdateDrag(&Info->SplitterState,
                              Point,
                              &Metrics,
                              NULL))
    {
        return;
    }

    Info->LayoutMetrics = Metrics;
    DmMainWndLayoutMainWindow(Info);
    InvalidateRect(Info->hMainWnd, NULL, TRUE);
    DmSplitterSetCursor(Info->SplitterState.Hit);
}

BOOL
DmMainWndHandleSplitterSetCursor(
    _In_ PMAIN_WND_INFO Info)
{
    DM_LAYOUT_PANES Panes;
    DM_SPLITTER_HIT Hit;
    POINT Point;

    if (DmSplitterIsDragging(&Info->SplitterState))
        return DmSplitterSetCursor(Info->SplitterState.Hit);

    GetCursorPos(&Point);
    ScreenToClient(Info->hMainWnd, &Point);
    DmMainWndGetCurrentLayoutPanes(Info, &Panes);
    Hit = DmLayoutHitTestSplitter(&Point, &Panes);
    if (Hit == DmSplitterHitNone)
        return FALSE;

    return DmSplitterSetCursor(Hit);
}
