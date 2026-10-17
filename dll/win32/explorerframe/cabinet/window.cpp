/*
 * PROJECT:     LiberNT Explorer
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     File Explorer window: tabs, bands, panes and status
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "cabinet.h"
#include "../resource.h"
#include <commoncontrols.h>
#include <propvarutil.h>
#include <exdisp.h>
#include <shdispid.h>
#include <shldisp.h>
#include <wincodec.h>
#include <windowsx.h>
#include <dwmapi.h>

#define IDC_CABINET_NAVBAR 103
#define IDC_CABINET_COMMANDS 107
#define IDC_CABINET_STATUS 108
#define IDC_CABINET_DETAILS 109
#define IDC_CABINET_DETAILSBAR 110
#define IDC_CABINET_VIEWBAR 111
#define IDC_COMMAND(command) (0x400 + (int)(command))
#define IDC_SEPARATOR(index) (0x500 + (index))
#define WM_CABINET_STATUS (WM_APP + 1)
#define WM_CABINET_CHANGED (WM_APP + 2)
#define WM_CABINET_FOCUSVIEW (WM_APP + 3)
#define IDT_CABINET_STATUS 1
#define IDT_CABINET_SEARCH 2

HINSTANCE
CabinetModule()
{
    static HINSTANCE module;
    HMODULE handle = NULL;

    if (!module &&
        GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           (LPCWSTR)CabinetModule, &handle))
        module = handle;
    return module;
}

#define g_hInstance CabinetModule()

static const CLSID CLSID_FindFolderShell = { 0xE17D4FC0, 0x5564, 0x11D1, { 0x83, 0xF2, 0x00, 0xA0, 0xC9, 0x0D, 0xC8, 0x49 } };

static void
FreeIdList(PIDLIST_ABSOLUTE *pidl)
{
    if (*pidl)
        ILFree(*pidl);
    *pidl = NULL;
}

static int
SystemIconIndex(PCIDLIST_ABSOLUTE pidl)
{
    SHFILEINFOW info;

    if (!pidl || !SHGetFileInfoW((LPCWSTR)pidl, 0, &info, sizeof(info), SHGFI_PIDL | SHGFI_SYSICONINDEX | SHGFI_SMALLICON))
        return -1;
    return info.iIcon;
}

static HICON
JumboIcon(PCIDLIST_ABSOLUTE pidl)
{
    static const int Lists[] = { SHIL_EXTRALARGE, SHIL_LARGE };
    SHFILEINFOW info;
    HICON icon = NULL;

    if (!SHGetFileInfoW((LPCWSTR)pidl, 0, &info, sizeof(info), SHGFI_PIDL | SHGFI_SYSICONINDEX))
        return NULL;
    for (UINT i = 0; i < _countof(Lists) && !icon; i++)
    {
        IImageList *list = NULL;
        if (SUCCEEDED(SHGetImageList(Lists[i], IID_IImageList, (void **)&list)))
        {
            list->GetIcon(info.iIcon, ILD_TRANSPARENT, &icon);
            list->Release();
        }
    }
    return icon;
}

CCabinetWindow::CCabinetWindow() :
    m_hwnd(NULL),
    m_TabCount(0),
    m_ActiveTab(-1),
    m_TabBand(NULL),
    m_AddressBand(NULL),
    m_SearchBox(NULL),
    m_NavBar(NULL),
    m_AddressEdit(NULL),
    m_SearchEdit(NULL),
    m_CommandBar(NULL),
    m_DetailsBar(NULL),
    m_ViewBar(NULL),
    m_Status(NULL),
    m_DetailsIcon(NULL),
    m_DetailsName(NULL),
    m_DetailsList(NULL),
    m_PreviewHost(NULL),
    m_PreviewText(NULL),
    m_PreviewImage(NULL),
    m_PreviewLabel(NULL),
    m_Font(NULL),
    m_Navigation(NULL),
    m_NavigationWindow(NULL),
    m_FocusSave(NULL),
    m_DetailsPane(FALSE),
    m_PreviewPane(FALSE),
    m_PreviewHandler(NULL),
    m_PreviewBitmap(NULL),
    m_PreviewPidl(NULL),
    m_DetailsIconHandle(NULL),
    m_InHistoryNavigation(FALSE),
    m_FocusViewOnNavigate(FALSE),
    m_ChangeNotify(0),
    m_NavigationVisible(TRUE),
    m_SearchBrowser(NULL),
    m_SearchPathCount(0),
    m_SearchActive(FALSE)
{
    m_SearchQuery[0] = UNICODE_NULL;
    ZeroMemory(m_Images, sizeof(m_Images));
    ZeroMemory(m_Tabs, sizeof(m_Tabs));
    SetRectEmpty(&m_ViewRect);
    SetRectEmpty(&m_PaneRect);
}

CCabinetWindow::~CCabinetWindow()
{
    ClearPreview();
    if (m_DetailsIconHandle)
        DestroyIcon(m_DetailsIconHandle);
    for (UINT i = 0; i < _countof(m_Images); i++)
    {
        if (m_Images[i])
            ImageList_Destroy(m_Images[i]);
    }
    if (m_TabBand)
        m_TabBand->Release();
    if (m_AddressBand)
        m_AddressBand->Release();
    if (m_SearchBox)
        m_SearchBox->Release();
    if (m_Font)
        DeleteObject(m_Font);
}

int
CCabinetWindow::Scale(int value) const
{
    return MulDiv(value, m_hwnd ? GetDpiForWindow(m_hwnd) : 96, 96);
}

void
CCabinetWindow::LoadText(UINT id, LPWSTR buffer, int length)
{
    if (!LoadStringW(g_hInstance, id, buffer, length))
        buffer[0] = UNICODE_NULL;
}

static HWND
CreateToolbar(HWND parent, UINT id, HIMAGELIST images, HIMAGELIST disabled, const TBBUTTON *buttons, UINT count,
              int gap)
{
    HWND toolbar = CreateWindowExW(TBSTYLE_EX_DOUBLEBUFFER, TOOLBARCLASSNAMEW, NULL,
                                   WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | TBSTYLE_FLAT | TBSTYLE_LIST |
                                   TBSTYLE_TOOLTIPS | TBSTYLE_TRANSPARENT | CCS_NODIVIDER | CCS_NOPARENTALIGN |
                                   CCS_NORESIZE | CCS_TOP,
                                   0, 0, 0, 0, parent, (HMENU)(UINT_PTR)id, g_hInstance, NULL);
    if (!toolbar)
        return NULL;
    SendMessageW(toolbar, TB_BUTTONSTRUCTSIZE, sizeof(TBBUTTON), 0);
    SendMessageW(toolbar, TB_SETMAXTEXTROWS, 1, 0);
    SendMessageW(toolbar, TB_SETEXTENDEDSTYLE, 0,
                 TBSTYLE_EX_MIXEDBUTTONS | TBSTYLE_EX_DRAWDDARROWS | TBSTYLE_EX_HIDECLIPPEDBUTTONS);
    SendMessageW(toolbar, TB_SETIMAGELIST, 0, (LPARAM)images);
    if (disabled)
        SendMessageW(toolbar, TB_SETDISABLEDIMAGELIST, 0, (LPARAM)disabled);
    SendMessageW(toolbar, TB_SETLISTGAP, gap, 0);
    SendMessageW(toolbar, TB_ADDBUTTONSW, count, (LPARAM)buttons);
    SendMessageW(toolbar, TB_AUTOSIZE, 0, 0);
    return toolbar;
}

static SIZE
ToolbarSize(HWND toolbar)
{
    SIZE size = { 0, 0 };

    SendMessageW(toolbar, TB_GETMAXSIZE, 0, (LPARAM)&size);
    return size;
}

void
CCabinetWindow::CreateBands()
{
    static const LPCWSTR NavGlyphs[] =
    {
        L"arrow_left_16_regular", L"arrow_right_16_regular", L"arrow_up_16_regular", L"arrow_clockwise_16_regular"
    };
    static const LPCWSTR CommandGlyphs[] =
    {
        L"add_circle_20_regular", L"cut_20_regular", L"copy_20_regular", L"clipboard_paste_20_regular",
        L"rename_20_regular", L"delete_20_regular", L"arrow_sort_20_regular", L"list_20_regular",
        L"more_horizontal_20_regular", L"panel_right_20_regular"
    };
    static const LPCWSTR ViewGlyphs[] = { L"text_bullet_list_ltr_16_regular", L"grid_16_regular" };
    CabinetPalette palette;
    WCHAR text[16][64];
    int glyph = Scale(16), command = Scale(20);

    CabinetGetPalette(&palette);
    m_Images[0] = CabinetGlyphImages(NavGlyphs, _countof(NavGlyphs), glyph, palette.Glyph);
    m_Images[1] = CabinetGlyphImages(NavGlyphs, _countof(NavGlyphs), glyph, palette.GlyphDisabled);
    m_Images[2] = CabinetGlyphImages(CommandGlyphs, _countof(CommandGlyphs), command, palette.Glyph);
    m_Images[3] = CabinetGlyphImages(CommandGlyphs, _countof(CommandGlyphs), command, palette.GlyphDisabled);
    m_Images[4] = CabinetGlyphImages(ViewGlyphs, _countof(ViewGlyphs), glyph, palette.Glyph);

    m_TabBand = new CCabinetTabBand();
    m_TabBand->Create(m_hwnd, this, m_Font);

    LoadText(IDS_CAB_BACK, text[0], _countof(text[0]));
    LoadText(IDS_CAB_FORWARD, text[1], _countof(text[1]));
    LoadText(IDS_CAB_UP, text[2], _countof(text[2]));
    LoadText(IDS_CAB_REFRESH, text[3], _countof(text[3]));
    {
        TBBUTTON buttons[] =
        {
            { 0, IDC_COMMAND(CabinetCommand::Back), 0, BTNS_BUTTON, { 0 }, 0, (INT_PTR)text[0] },
            { 1, IDC_COMMAND(CabinetCommand::Forward), 0, BTNS_BUTTON, { 0 }, 0, (INT_PTR)text[1] },
            { 2, IDC_COMMAND(CabinetCommand::Up), 0, BTNS_BUTTON, { 0 }, 0, (INT_PTR)text[2] },
            { 3, IDC_COMMAND(CabinetCommand::Refresh), TBSTATE_ENABLED, BTNS_BUTTON, { 0 }, 0, (INT_PTR)text[3] },
        };
        m_NavBar = CreateToolbar(m_hwnd, IDC_CABINET_NAVBAR, m_Images[0], m_Images[1], buttons, _countof(buttons),
                                 Scale(12));
    }

    m_AddressBand = new CCabinetAddressBand();
    if (m_AddressBand->Create(m_hwnd, this, m_Font))
        m_AddressEdit = m_AddressBand->EditWindow();
    m_SearchBox = new CCabinetSearchBox();
    if (m_SearchBox->Create(m_hwnd, m_Font))
        m_SearchEdit = m_SearchBox->EditWindow();

    LoadText(IDS_CAB_NEW, text[4], _countof(text[4]));
    LoadText(IDS_CAB_CUT, text[5], _countof(text[5]));
    LoadText(IDS_CAB_COPY, text[6], _countof(text[6]));
    LoadText(IDS_CAB_PASTE, text[7], _countof(text[7]));
    LoadText(IDS_CAB_RENAME, text[8], _countof(text[8]));
    LoadText(IDS_CAB_DELETE, text[9], _countof(text[9]));
    LoadText(IDS_CAB_SORT, text[10], _countof(text[10]));
    LoadText(IDS_CAB_VIEW, text[11], _countof(text[11]));
    LoadText(IDS_CAB_MORE, text[12], _countof(text[12]));
    LoadText(IDS_CAB_DETAILS, text[13], _countof(text[13]));
    LoadText(IDS_CAB_VIEW_DETAILS, text[14], _countof(text[14]));
    LoadText(IDS_CAB_VIEW_LARGE, text[15], _countof(text[15]));
    {
        TBBUTTON buttons[] =
        {
            { 0, IDC_COMMAND(CabinetCommand::NewItem), TBSTATE_ENABLED,
              BTNS_WHOLEDROPDOWN | BTNS_SHOWTEXT | BTNS_AUTOSIZE, { 0 }, 0, (INT_PTR)text[4] },
            { 0, IDC_SEPARATOR(0), 0, BTNS_SEP, { 0 }, 0, 0 },
            { 1, IDC_COMMAND(CabinetCommand::Cut), 0, BTNS_BUTTON, { 0 }, 0, (INT_PTR)text[5] },
            { 2, IDC_COMMAND(CabinetCommand::Copy), 0, BTNS_BUTTON, { 0 }, 0, (INT_PTR)text[6] },
            { 3, IDC_COMMAND(CabinetCommand::Paste), 0, BTNS_BUTTON, { 0 }, 0, (INT_PTR)text[7] },
            { 4, IDC_COMMAND(CabinetCommand::Rename), 0, BTNS_BUTTON, { 0 }, 0, (INT_PTR)text[8] },
            { 5, IDC_COMMAND(CabinetCommand::Delete), 0, BTNS_BUTTON, { 0 }, 0, (INT_PTR)text[9] },
            { 0, IDC_SEPARATOR(1), 0, BTNS_SEP, { 0 }, 0, 0 },
            { 6, IDC_COMMAND(CabinetCommand::Sort), TBSTATE_ENABLED,
              BTNS_WHOLEDROPDOWN | BTNS_SHOWTEXT | BTNS_AUTOSIZE, { 0 }, 0, (INT_PTR)text[10] },
            { 7, IDC_COMMAND(CabinetCommand::View), TBSTATE_ENABLED,
              BTNS_WHOLEDROPDOWN | BTNS_SHOWTEXT | BTNS_AUTOSIZE, { 0 }, 0, (INT_PTR)text[11] },
            { 0, IDC_SEPARATOR(2), 0, BTNS_SEP, { 0 }, 0, 0 },
            { 8, IDC_COMMAND(CabinetCommand::More), TBSTATE_ENABLED, BTNS_BUTTON, { 0 }, 0, (INT_PTR)text[12] },
        };
        m_CommandBar = CreateToolbar(m_hwnd, IDC_CABINET_COMMANDS, m_Images[2], m_Images[3], buttons,
                                     _countof(buttons), Scale(10));
    }
    {
        TBBUTTON buttons[] =
        {
            { 9, IDC_COMMAND(CabinetCommand::Details), TBSTATE_ENABLED, BTNS_CHECK | BTNS_SHOWTEXT | BTNS_AUTOSIZE,
              { 0 }, 0, (INT_PTR)text[13] },
        };
        m_DetailsBar = CreateToolbar(m_hwnd, IDC_CABINET_DETAILSBAR, m_Images[2], m_Images[3], buttons,
                                     _countof(buttons), Scale(10));
    }

    m_Status = CreateWindowExW(0, STATUSCLASSNAMEW, NULL, WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS,
                               0, 0, 0, 0, m_hwnd, (HMENU)IDC_CABINET_STATUS, g_hInstance, NULL);
    SendMessageW(m_Status, WM_SETFONT, (WPARAM)m_Font, FALSE);
    {
        TBBUTTON buttons[] =
        {
            { 0, IDC_COMMAND(CabinetCommand::ViewDetails), TBSTATE_ENABLED, BTNS_CHECK, { 0 }, 0, (INT_PTR)text[14] },
            { 1, IDC_COMMAND(CabinetCommand::ViewLarge), TBSTATE_ENABLED, BTNS_CHECK, { 0 }, 0, (INT_PTR)text[15] },
        };
        m_ViewBar = CreateToolbar(m_hwnd, IDC_CABINET_VIEWBAR, m_Images[4], NULL, buttons, _countof(buttons),
                                  Scale(4));
    }

    m_DetailsIcon = CreateWindowExW(0, WC_STATICW, NULL, WS_CHILD | SS_ICON | SS_CENTERIMAGE | SS_REALSIZEIMAGE,
                                    0, 0, 0, 0, m_hwnd, NULL, g_hInstance, NULL);
    m_DetailsName = CreateWindowExW(0, WC_STATICW, NULL, WS_CHILD | SS_CENTER | SS_NOPREFIX | SS_ENDELLIPSIS,
                                    0, 0, 0, 0, m_hwnd, NULL, g_hInstance, NULL);
    m_DetailsList = CreateWindowExW(0, WC_LISTVIEWW, NULL,
                                    WS_CHILD | LVS_REPORT | LVS_NOCOLUMNHEADER | LVS_SINGLESEL | LVS_NOSORTHEADER,
                                    0, 0, 0, 0, m_hwnd, (HMENU)IDC_CABINET_DETAILS, g_hInstance, NULL);
    SendMessageW(m_DetailsName, WM_SETFONT, (WPARAM)m_Font, FALSE);
    SendMessageW(m_DetailsList, WM_SETFONT, (WPARAM)m_Font, FALSE);
    SetWindowTheme(m_DetailsList, L"Explorer", NULL);
    ListView_SetExtendedListViewStyle(m_DetailsList, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);
    {
        LVCOLUMNW column = { LVCF_WIDTH };
        column.cx = Scale(110);
        ListView_InsertColumn(m_DetailsList, 0, &column);
        column.cx = Scale(190);
        ListView_InsertColumn(m_DetailsList, 1, &column);
    }
    m_PreviewHost = CreateWindowExW(0, WC_STATICW, NULL, WS_CHILD | WS_CLIPCHILDREN | WS_CLIPSIBLINGS, 0, 0, 0, 0,
                                    m_hwnd, NULL, g_hInstance, NULL);
    m_PreviewText = CreateWindowExW(0, WC_EDITW, L"", WS_CHILD | WS_VSCROLL | ES_MULTILINE | ES_READONLY |
                                    ES_AUTOVSCROLL, 0, 0, 0, 0, m_hwnd, NULL, g_hInstance, NULL);
    m_PreviewImage = CreateWindowExW(0, WC_STATICW, NULL, WS_CHILD | SS_BITMAP | SS_CENTERIMAGE, 0, 0, 0, 0, m_hwnd,
                                     NULL, g_hInstance, NULL);
    m_PreviewLabel = CreateWindowExW(0, WC_STATICW, NULL, WS_CHILD | SS_CENTER | SS_CENTERIMAGE | SS_NOPREFIX, 0, 0,
                                     0, 0, m_hwnd, NULL, g_hInstance, NULL);
    SendMessageW(m_PreviewText, WM_SETFONT, (WPARAM)m_Font, FALSE);
    SendMessageW(m_PreviewLabel, WM_SETFONT, (WPARAM)m_Font, FALSE);
}

void
CCabinetWindow::OnCreate()
{
    NONCLIENTMETRICSW metrics = { sizeof(metrics) };
    SHSTOCKICONINFO icon = { sizeof(icon) };

    if (SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(metrics), &metrics, 0))
        m_Font = CreateFontIndirectW(&metrics.lfMessageFont);
    if (SUCCEEDED(SHGetStockIconInfo(SIID_FOLDER, SHGSI_ICON | SHGSI_SMALLICON, &icon)))
        SendMessageW(m_hwnd, WM_SETICON, ICON_SMALL, (LPARAM)icon.hIcon);
    if (SUCCEEDED(SHGetStockIconInfo(SIID_FOLDER, SHGSI_ICON | SHGSI_LARGEICON, &icon)))
        SendMessageW(m_hwnd, WM_SETICON, ICON_BIG, (LPARAM)icon.hIcon);
    CreateBands();
    CreateNavigationPane();
    ExtendFrame();
}

void
CCabinetWindow::ExtendFrame()
{
    MARGINS margins = { 0, 0, TabStripHeight(), 0 };

    DwmExtendFrameIntoClientArea(m_hwnd, &margins);
}

int
CCabinetWindow::CaptionButtonsWidth()
{
    RECT bounds;

    if (SUCCEEDED(DwmGetWindowAttribute(m_hwnd, DWMWA_CAPTION_BUTTON_BOUNDS, &bounds, sizeof(bounds))) &&
        bounds.right > bounds.left)
        return bounds.right - bounds.left;
    return GetSystemMetricsForDpi(SM_CXSIZE, GetDpiForWindow(m_hwnd)) * 3;
}

void
CCabinetWindow::PaintBackground(HDC dc)
{
    CabinetPalette palette;
    RECT client;
    struct { int Top; int Bottom; COLORREF Color; } bands[5];

    CabinetGetPalette(&palette);
    GetClientRect(m_hwnd, &client);
    {
        RECT glass = { 0, 0, client.right, TabStripHeight() };
        FillRect(dc, &glass, (HBRUSH)GetStockObject(BLACK_BRUSH));
    }
    bands[0] = { TabStripHeight(), AddressBottom() - 1, palette.Band };
    bands[1] = { AddressBottom() - 1, AddressBottom(), palette.BandBottom };
    bands[2] = { AddressBottom(), CommandBottom() - 1, palette.Command };
    bands[3] = { CommandBottom() - 1, CommandBottom(), palette.CommandBottom };
    bands[4] = { CommandBottom(), (int)client.bottom, palette.Window };
    for (UINT i = 0; i < _countof(bands); i++)
    {
        RECT rc = { 0, bands[i].Top, client.right, bands[i].Bottom };
        HBRUSH brush = CreateSolidBrush(bands[i].Color);
        FillRect(dc, &rc, brush);
        DeleteObject(brush);
    }
}

void
CCabinetWindow::FitCommandBar(int width)
{
    static const UINT Order[] =
    {
        IDC_COMMAND(CabinetCommand::View), IDC_COMMAND(CabinetCommand::Sort), IDC_SEPARATOR(2),
        IDC_COMMAND(CabinetCommand::Delete), IDC_COMMAND(CabinetCommand::Rename), IDC_COMMAND(CabinetCommand::Paste),
        IDC_COMMAND(CabinetCommand::Copy), IDC_COMMAND(CabinetCommand::Cut), IDC_SEPARATOR(1),
        IDC_COMMAND(CabinetCommand::NewItem), IDC_SEPARATOR(0)
    };

    SendMessageW(m_CommandBar, WM_SETREDRAW, FALSE, 0);
    for (UINT i = 0; i < _countof(Order); i++)
        SendMessageW(m_CommandBar, TB_HIDEBUTTON, Order[i], MAKELONG(FALSE, 0));
    for (UINT i = 0; i < _countof(Order); i++)
    {
        SIZE size = { 0, 0 };
        SendMessageW(m_CommandBar, TB_GETMAXSIZE, 0, (LPARAM)&size);
        if (size.cx <= width)
            break;
        SendMessageW(m_CommandBar, TB_HIDEBUTTON, Order[i], MAKELONG(TRUE, 0));
    }
    SendMessageW(m_CommandBar, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(m_CommandBar, NULL, TRUE);
}

void
CCabinetWindow::OnSize()
{
    RECT client, status;
    SIZE size;
    int top, bottom, left, right, pane, search, row, statusHeight;

    if (!m_TabBand || !m_TabBand->Window())
        return;
    GetClientRect(m_hwnd, &client);
    MoveWindow(m_TabBand->Window(), 0, 0, max(0, (int)client.right - CaptionButtonsWidth() - Scale(8)), TabStripHeight(),
               TRUE);
    size = ToolbarSize(m_NavBar);
    MoveWindow(m_NavBar, Scale(3), TabStripHeight() + Scale(8) + (Scale(32) - size.cy) / 2, size.cx, size.cy, TRUE);
    left = Scale(3) + size.cx + Scale(6);
    search = min(Scale(300), max(Scale(140), (int)(client.right - Scale(214)) / 3));
    if (m_SearchBox->Window())
        MoveWindow(m_SearchBox->Window(), client.right - Scale(13) - search, TabStripHeight() + Scale(8), search,
                   Scale(32), TRUE);
    if (m_AddressBand->Window())
        MoveWindow(m_AddressBand->Window(), left, TabStripHeight() + Scale(8),
                   max(0, (int)client.right - Scale(13) - search - Scale(10) - left), Scale(32), TRUE);
    size = ToolbarSize(m_DetailsBar);
    row = AddressBottom() + (CommandBottom() - 1 - AddressBottom() - (int)size.cy) / 2;
    MoveWindow(m_DetailsBar, client.right - Scale(8) - size.cx, row, size.cx, size.cy, TRUE);
    right = max(0, (int)client.right - Scale(8) - (int)size.cx - Scale(16) - Scale(8));
    FitCommandBar(right);
    size = ToolbarSize(m_CommandBar);
    row = AddressBottom() + (CommandBottom() - 1 - AddressBottom() - (int)size.cy) / 2;
    MoveWindow(m_CommandBar, Scale(8), row, right, size.cy, TRUE);

    SendMessageW(m_Status, WM_SIZE, 0, 0);
    GetWindowRect(m_Status, &status);
    statusHeight = IsWindowVisible(m_Status) ? status.bottom - status.top : 0;
    {
        int parts[2] = { Scale(180), -1 };
        SendMessageW(m_Status, SB_SETPARTS, _countof(parts), (LPARAM)parts);
    }
    size = ToolbarSize(m_ViewBar);
    SetWindowPos(m_ViewBar, HWND_TOP, client.right - Scale(8) - size.cx,
                 client.bottom - statusHeight + (statusHeight - size.cy) / 2, size.cx, size.cy, SWP_NOACTIVATE);
    top = CommandBottom();
    bottom = client.bottom - statusHeight;
    left = 0;
    if (m_NavigationWindow)
    {
        ShowWindow(m_NavigationWindow, m_NavigationVisible ? SW_SHOW : SW_HIDE);
        if (m_NavigationVisible)
        {
            left = Scale(160);
            MoveWindow(m_NavigationWindow, Scale(2), top, Scale(154), max(0, bottom - top), TRUE);
        }
    }
    pane = m_DetailsPane || m_PreviewPane ? Scale(300) : 0;
    right = client.right - pane;
    SetRect(&m_ViewRect, left, top, max(left, right), bottom);
    SetRect(&m_PaneRect, max(left, right), top, client.right, bottom);
    PlaceBrowsers();
    PlacePanes();
    RedrawWindow(m_hwnd, NULL, NULL, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN);
}

void
CCabinetWindow::PlacePanes()
{
    RECT rc = m_PaneRect;
    int margin = Scale(10), icon = Scale(64);
    BOOL details = m_DetailsPane && !IsRectEmpty(&rc), preview = m_PreviewPane && !IsRectEmpty(&rc);

    InflateRect(&rc, -margin, -margin);
    ShowWindow(m_DetailsIcon, details ? SW_SHOW : SW_HIDE);
    ShowWindow(m_DetailsName, details ? SW_SHOW : SW_HIDE);
    ShowWindow(m_DetailsList, details ? SW_SHOW : SW_HIDE);
    if (details)
    {
        int width = rc.right - rc.left - GetSystemMetrics(SM_CXVSCROLL);
        ListView_SetColumnWidth(m_DetailsList, 0, width * 2 / 5);
        ListView_SetColumnWidth(m_DetailsList, 1, width - width * 2 / 5);
        MoveWindow(m_DetailsIcon, rc.left, rc.top, rc.right - rc.left, icon, TRUE);
        MoveWindow(m_DetailsName, rc.left, rc.top + icon + margin, rc.right - rc.left, Scale(20), TRUE);
        MoveWindow(m_DetailsList, rc.left, rc.top + icon + margin * 2 + Scale(20), rc.right - rc.left,
                   max(0, rc.bottom - (rc.top + icon + margin * 2 + Scale(20))), TRUE);
    }
    if (!preview)
    {
        ShowWindow(m_PreviewHost, SW_HIDE);
        ShowWindow(m_PreviewText, SW_HIDE);
        ShowWindow(m_PreviewImage, SW_HIDE);
        ShowWindow(m_PreviewLabel, SW_HIDE);
        return;
    }
    MoveWindow(m_PreviewHost, m_PaneRect.left, m_PaneRect.top, m_PaneRect.right - m_PaneRect.left,
               m_PaneRect.bottom - m_PaneRect.top, TRUE);
    MoveWindow(m_PreviewText, rc.left, rc.top, rc.right - rc.left, rc.bottom - rc.top, TRUE);
    MoveWindow(m_PreviewImage, rc.left, rc.top, rc.right - rc.left, rc.bottom - rc.top, TRUE);
    MoveWindow(m_PreviewLabel, rc.left, rc.top, rc.right - rc.left, rc.bottom - rc.top, TRUE);
    if (m_PreviewHandler)
    {
        RECT client;
        GetClientRect(m_PreviewHost, &client);
        m_PreviewHandler->SetRect(&client);
    }
}

void
CCabinetWindow::UpdateDetails()
{
    CabinetTab *tab = ActiveTab();
    IShellView *view;
    IFolderView *folderView = NULL;
    IShellItem2 *item = NULL;
    IPropertyDescriptionList *list = NULL;
    PIDLIST_ABSOLUTE pidl = NULL;
    int selected = 0;
    UINT count = 0;
    WCHAR name[MAX_PATH] = L"";

    ListView_DeleteAllItems(m_DetailsList);
    if (m_DetailsIconHandle)
        DestroyIcon(m_DetailsIconHandle);
    m_DetailsIconHandle = NULL;
    if (!m_DetailsPane || !tab || !tab->Pidl)
        return;
    view = CurrentView();
    if (view && SUCCEEDED(view->QueryInterface(IID_PPV_ARG(IFolderView, &folderView))))
        folderView->ItemCount(SVGIO_SELECTION, &selected);
    if (selected > 1)
    {
        WCHAR format[64];
        LoadText(IDS_CAB_SELECTED, format, _countof(format));
        StringCchPrintfW(name, _countof(name), format, selected);
        m_DetailsIconHandle = JumboIcon(tab->Pidl);
    }
    else
    {
        if (selected == 1)
        {
            IShellItemArray *items = NULL;
            if (SUCCEEDED(folderView->Items(SVGIO_SELECTION, IID_PPV_ARG(IShellItemArray, &items))))
            {
                IShellItem *first = NULL;
                if (SUCCEEDED(items->GetItemAt(0, &first)))
                {
                    first->QueryInterface(IID_PPV_ARG(IShellItem2, &item));
                    first->Release();
                }
                items->Release();
            }
        }
        if (!item)
            SHCreateItemFromIDList(tab->Pidl, IID_PPV_ARG(IShellItem2, &item));
        if (item && SUCCEEDED(SHGetIDListFromObject(item, &pidl)))
        {
            PWSTR display = NULL;
            m_DetailsIconHandle = JumboIcon(pidl);
            if (SUCCEEDED(item->GetDisplayName(SIGDN_NORMALDISPLAY, &display)))
            {
                StringCchCopyW(name, _countof(name), display);
                CoTaskMemFree(display);
            }
            ILFree(pidl);
        }
        if (item && SUCCEEDED(item->GetPropertyDescriptionList(PKEY_PropList_PreviewDetails,
                                                                IID_PPV_ARG(IPropertyDescriptionList, &list))))
        {
            list->GetCount(&count);
            for (UINT i = 0; i < count; i++)
            {
                IPropertyDescription *desc = NULL;
                PROPERTYKEY key;
                PROPVARIANT value;
                PWSTR label = NULL, text = NULL;

                if (FAILED(list->GetAt(i, IID_PPV_ARG(IPropertyDescription, &desc))))
                    continue;
                PropVariantInit(&value);
                if (SUCCEEDED(desc->GetPropertyKey(&key)) && SUCCEEDED(item->GetProperty(key, &value)) &&
                    value.vt != VT_EMPTY && SUCCEEDED(desc->FormatForDisplay(value, PDFF_DEFAULT, &text)) &&
                    text && text[0] && SUCCEEDED(desc->GetDisplayName(&label)))
                {
                    LVITEMW row = { LVIF_TEXT };
                    row.iItem = ListView_GetItemCount(m_DetailsList);
                    row.pszText = label;
                    row.iItem = ListView_InsertItem(m_DetailsList, &row);
                    ListView_SetItemText(m_DetailsList, row.iItem, 1, text);
                }
                CoTaskMemFree(label);
                CoTaskMemFree(text);
                PropVariantClear(&value);
                desc->Release();
            }
            list->Release();
        }
        if (item)
            item->Release();
    }
    if (folderView)
        folderView->Release();
    if (view)
        view->Release();
    SetWindowTextW(m_DetailsName, name);
    SendMessageW(m_DetailsIcon, STM_SETICON, (WPARAM)m_DetailsIconHandle, 0);
}

void
CCabinetWindow::SetPane(BOOL details, BOOL preview)
{
    m_DetailsPane = details;
    m_PreviewPane = preview;
    if (m_CommandBar)
    {
        SendMessageW(m_CommandBar, TB_CHECKBUTTON, IDC_COMMAND(CabinetCommand::Details), MAKELONG(details, 0));
        SendMessageW(m_CommandBar, TB_CHECKBUTTON, IDC_COMMAND(CabinetCommand::Preview), MAKELONG(preview, 0));
    }
    OnSize();
    UpdateDetails();
    UpdatePreview();
}

void
CCabinetWindow::ClearPreview()
{
    if (m_PreviewHandler)
    {
        m_PreviewHandler->Unload();
        m_PreviewHandler->Release();
        m_PreviewHandler = NULL;
    }
    if (m_PreviewHost)
        ShowWindow(m_PreviewHost, SW_HIDE);
    if (m_PreviewText)
    {
        ShowWindow(m_PreviewText, SW_HIDE);
        SetWindowTextW(m_PreviewText, L"");
    }
    if (m_PreviewLabel)
        ShowWindow(m_PreviewLabel, SW_HIDE);
    SetPreviewBitmap(NULL);
    FreeIdList(&m_PreviewPidl);
}

void
CCabinetWindow::SetPreviewBitmap(HBITMAP bitmap)
{
    HBITMAP old;

    if (!m_PreviewImage)
        return;
    old = (HBITMAP)SendMessageW(m_PreviewImage, STM_SETIMAGE, IMAGE_BITMAP, (LPARAM)bitmap);
    if (old && old != bitmap)
        DeleteObject(old);
    if (m_PreviewBitmap && m_PreviewBitmap != bitmap && m_PreviewBitmap != old)
        DeleteObject(m_PreviewBitmap);
    m_PreviewBitmap = bitmap;
    ShowWindow(m_PreviewImage, bitmap && m_PreviewPane ? SW_SHOW : SW_HIDE);
}

void
CCabinetWindow::ShowPreviewMessage(UINT id)
{
    WCHAR text[96];

    LoadText(id, text, _countof(text));
    SetWindowTextW(m_PreviewLabel, text);
    ShowWindow(m_PreviewLabel, m_PreviewPane ? SW_SHOW : SW_HIDE);
}

BOOL
CCabinetWindow::ShowPreviewHandler(IShellItem *item, PCWSTR path)
{
    WCHAR text[64];
    DWORD length = _countof(text);
    CLSID clsid;
    IPreviewHandler *handler = NULL;
    IInitializeWithStream *withStream = NULL;
    IInitializeWithItem *withItem = NULL;
    IInitializeWithFile *withFile = NULL;
    HRESULT hr = E_NOINTERFACE;
    RECT client;
    PCWSTR extension = path ? PathFindExtensionW(path) : NULL;

    if (!extension || !extension[0] ||
        FAILED(AssocQueryStringW(ASSOCF_INIT_DEFAULTTOSTAR, ASSOCSTR_SHELLEXTENSION, extension,
                                 L"{8895b1c6-b41f-4c1c-a562-0d564250836f}", text, &length)) ||
        FAILED(CLSIDFromString(text, &clsid)) ||
        FAILED(CoCreateInstance(clsid, NULL, CLSCTX_INPROC_SERVER | CLSCTX_LOCAL_SERVER,
                                IID_PPV_ARG(IPreviewHandler, &handler))))
        return FALSE;
    if (SUCCEEDED(handler->QueryInterface(IID_PPV_ARG(IInitializeWithStream, &withStream))))
    {
        IStream *stream = NULL;
        hr = SHCreateStreamOnFileEx(path, STGM_READ | STGM_SHARE_DENY_NONE, 0, FALSE, NULL, &stream);
        if (SUCCEEDED(hr))
        {
            hr = withStream->Initialize(stream, STGM_READ);
            stream->Release();
        }
        withStream->Release();
    }
    else if (SUCCEEDED(handler->QueryInterface(IID_PPV_ARG(IInitializeWithItem, &withItem))))
    {
        hr = withItem->Initialize(item, STGM_READ);
        withItem->Release();
    }
    else if (SUCCEEDED(handler->QueryInterface(IID_PPV_ARG(IInitializeWithFile, &withFile))))
    {
        hr = withFile->Initialize(path, STGM_READ);
        withFile->Release();
    }
    if (FAILED(hr))
    {
        handler->Release();
        return FALSE;
    }
    ShowWindow(m_PreviewHost, SW_SHOW);
    PlacePanes();
    GetClientRect(m_PreviewHost, &client);
    if (FAILED(handler->SetWindow(m_PreviewHost, &client)) || FAILED(handler->DoPreview()))
    {
        handler->Unload();
        handler->Release();
        ShowWindow(m_PreviewHost, SW_HIDE);
        return FALSE;
    }
    m_PreviewHandler = handler;
    return TRUE;
}

BOOL
CCabinetWindow::ShowPreviewThumbnail(IShellItem *item)
{
    IShellItemImageFactory *factory = NULL;
    SIZE size;
    HBITMAP bitmap = NULL;
    HRESULT hr;

    size.cx = max(m_PaneRect.right - m_PaneRect.left - Scale(20), Scale(64));
    size.cy = max(m_PaneRect.bottom - m_PaneRect.top - Scale(20), Scale(64));
    if (FAILED(item->QueryInterface(IID_PPV_ARG(IShellItemImageFactory, &factory))))
        return FALSE;
    hr = factory->GetImage(size, SIIGBF_THUMBNAILONLY | SIIGBF_BIGGERSIZEOK, &bitmap);
    factory->Release();
    if (FAILED(hr) || !bitmap)
        return FALSE;
    SetPreviewBitmap(bitmap);
    return TRUE;
}

BOOL
CCabinetWindow::ShowPreviewImage(PCWSTR path)
{
    PERCEIVED type = PERCEIVED_TYPE_UNSPECIFIED;
    PERCEIVEDFLAG flags = 0;
    PCWSTR extension = path ? PathFindExtensionW(path) : NULL;
    IWICImagingFactory *factory = NULL;
    IWICBitmapDecoder *decoder = NULL;
    IWICBitmapFrameDecode *frame = NULL;
    IWICBitmapScaler *scaler = NULL;
    IWICFormatConverter *converter = NULL;
    UINT width = 0, height = 0, maxWidth, maxHeight;
    HBITMAP bitmap = NULL;
    BOOL result = FALSE;

    if (!extension || !extension[0] || FAILED(AssocGetPerceivedType(extension, &type, &flags, NULL)) ||
        type != PERCEIVED_TYPE_IMAGE)
        return FALSE;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, NULL, CLSCTX_INPROC_SERVER,
                                IID_PPV_ARG(IWICImagingFactory, &factory))))
        return FALSE;
    if (SUCCEEDED(factory->CreateDecoderFromFilename(path, NULL, GENERIC_READ, WICDecodeMetadataCacheOnDemand,
                                                     &decoder)) &&
        SUCCEEDED(decoder->GetFrame(0, &frame)) && SUCCEEDED(frame->GetSize(&width, &height)) && width && height)
    {
        maxWidth = (UINT)max(m_PaneRect.right - m_PaneRect.left - Scale(20), Scale(64));
        maxHeight = (UINT)max(m_PaneRect.bottom - m_PaneRect.top - Scale(20), Scale(64));
        if (width > maxWidth)
        {
            height = (UINT)((ULONGLONG)height * maxWidth / width);
            width = maxWidth;
        }
        if (height > maxHeight)
        {
            width = (UINT)((ULONGLONG)width * maxHeight / height);
            height = maxHeight;
        }
        if (width && height && SUCCEEDED(factory->CreateBitmapScaler(&scaler)) &&
            SUCCEEDED(scaler->Initialize(frame, width, height, WICBitmapInterpolationModeFant)) &&
            SUCCEEDED(factory->CreateFormatConverter(&converter)) &&
            SUCCEEDED(converter->Initialize(scaler, GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone, NULL, 0,
                                            WICBitmapPaletteTypeCustom)))
        {
            BITMAPINFO info = { { sizeof(BITMAPINFOHEADER) } };
            BYTE *bits = NULL;
            info.bmiHeader.biWidth = (LONG)width;
            info.bmiHeader.biHeight = -(LONG)height;
            info.bmiHeader.biPlanes = 1;
            info.bmiHeader.biBitCount = 32;
            bitmap = CreateDIBSection(NULL, &info, DIB_RGB_COLORS, (void **)&bits, NULL, 0);
            if (bitmap && SUCCEEDED(converter->CopyPixels(NULL, width * 4, width * height * 4, bits)))
            {
                COLORREF back = GetSysColor(COLOR_WINDOW);
                for (UINT i = 0; i < width * height; i++)
                {
                    BYTE *pixel = bits + i * 4;
                    UINT inverse = 255 - pixel[3];
                    pixel[0] = (BYTE)(pixel[0] + GetBValue(back) * inverse / 255);
                    pixel[1] = (BYTE)(pixel[1] + GetGValue(back) * inverse / 255);
                    pixel[2] = (BYTE)(pixel[2] + GetRValue(back) * inverse / 255);
                    pixel[3] = 255;
                }
                SetPreviewBitmap(bitmap);
                bitmap = NULL;
                result = TRUE;
            }
        }
    }
    if (bitmap)
        DeleteObject(bitmap);
    if (converter)
        converter->Release();
    if (scaler)
        scaler->Release();
    if (frame)
        frame->Release();
    if (decoder)
        decoder->Release();
    factory->Release();
    return result;
}

BOOL
CCabinetWindow::ShowPreviewText(PCWSTR path)
{
    PERCEIVED type = PERCEIVED_TYPE_UNSPECIFIED;
    PERCEIVEDFLAG flags = 0;
    HANDLE file;
    BYTE *data;
    DWORD read = 0;
    PWSTR text = NULL, lines;
    int count = 0, offset = 0, out = 0;
    UINT codePage = CP_UTF8;
    PCWSTR extension = path ? PathFindExtensionW(path) : NULL;

    if (!extension || !extension[0] || FAILED(AssocGetPerceivedType(extension, &type, &flags, NULL)) ||
        type != PERCEIVED_TYPE_TEXT)
        return FALSE;
    file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL,
                       OPEN_EXISTING, 0, NULL);
    if (file == INVALID_HANDLE_VALUE)
        return FALSE;
    data = (BYTE *)HeapAlloc(GetProcessHeap(), 0, 65536);
    if (!data || !ReadFile(file, data, 65536, &read, NULL))
    {
        CloseHandle(file);
        if (data)
            HeapFree(GetProcessHeap(), 0, data);
        return FALSE;
    }
    CloseHandle(file);
    if (read >= 2 && data[0] == 0xFF && data[1] == 0xFE)
    {
        count = (int)(read - 2) / (int)sizeof(WCHAR);
        text = (PWSTR)HeapAlloc(GetProcessHeap(), 0, (count + 1) * sizeof(WCHAR));
        if (text)
        {
            CopyMemory(text, data + 2, count * sizeof(WCHAR));
            text[count] = UNICODE_NULL;
        }
    }
    else
    {
        if (read >= 3 && data[0] == 0xEF && data[1] == 0xBB && data[2] == 0xBF)
            offset = 3;
        else if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, (LPCCH)data, (int)read, NULL, 0))
            codePage = CP_ACP;
        count = MultiByteToWideChar(codePage, 0, (LPCCH)data + offset, (int)read - offset, NULL, 0);
        text = (PWSTR)HeapAlloc(GetProcessHeap(), 0, (count + 1) * sizeof(WCHAR));
        if (text)
        {
            MultiByteToWideChar(codePage, 0, (LPCCH)data + offset, (int)read - offset, text, count);
            text[count] = UNICODE_NULL;
        }
    }
    HeapFree(GetProcessHeap(), 0, data);
    if (!text)
        return FALSE;
    lines = (PWSTR)HeapAlloc(GetProcessHeap(), 0, (count * 2 + 1) * sizeof(WCHAR));
    if (!lines)
    {
        HeapFree(GetProcessHeap(), 0, text);
        return FALSE;
    }
    for (int i = 0; i < count && text[i]; i++)
    {
        if (text[i] == L'\n' && (i == 0 || text[i - 1] != L'\r'))
            lines[out++] = L'\r';
        lines[out++] = text[i];
    }
    lines[out] = UNICODE_NULL;
    HeapFree(GetProcessHeap(), 0, text);
    SetWindowTextW(m_PreviewText, lines);
    HeapFree(GetProcessHeap(), 0, lines);
    ShowWindow(m_PreviewText, SW_SHOW);
    return TRUE;
}

void
CCabinetWindow::UpdatePreview()
{
    IShellView *view = NULL;
    IFolderView *folderView = NULL;
    IShellItemArray *items = NULL;
    IShellItem *item = NULL;
    PIDLIST_ABSOLUTE pidl = NULL;
    DWORD count = 0;
    SFGAOF attributes = 0;
    PWSTR path = NULL;

    if (!m_PreviewPane)
    {
        ClearPreview();
        return;
    }
    view = CurrentView();
    if (view && SUCCEEDED(view->QueryInterface(IID_PPV_ARG(IFolderView, &folderView))) &&
        SUCCEEDED(folderView->Items(SVGIO_SELECTION, IID_PPV_ARG(IShellItemArray, &items))))
    {
        items->GetCount(&count);
        if (count == 1)
            items->GetItemAt(0, &item);
    }
    if (items)
        items->Release();
    if (folderView)
        folderView->Release();
    if (view)
        view->Release();
    if (item && FAILED(SHGetIDListFromObject(item, &pidl)))
        pidl = NULL;
    if (pidl && m_PreviewPidl && ILIsEqual(pidl, m_PreviewPidl))
    {
        ILFree(pidl);
        item->Release();
        return;
    }
    ClearPreview();
    if (!item || !pidl)
    {
        ShowPreviewMessage(count ? IDS_CAB_PREVIEW_NONE : IDS_CAB_PREVIEW_SELECT);
        if (pidl)
            ILFree(pidl);
        if (item)
            item->Release();
        return;
    }
    m_PreviewPidl = pidl;
    item->GetAttributes(SFGAO_FOLDER | SFGAO_STREAM, &attributes);
    if ((attributes & SFGAO_FOLDER) && !(attributes & SFGAO_STREAM))
        ShowPreviewMessage(IDS_CAB_PREVIEW_NONE);
    else
    {
        if (FAILED(item->GetDisplayName(SIGDN_FILESYSPATH, &path)))
            path = NULL;
        if (!ShowPreviewHandler(item, path) && !ShowPreviewThumbnail(item) && !ShowPreviewImage(path) &&
            !ShowPreviewText(path))
            ShowPreviewMessage(IDS_CAB_PREVIEW_NONE);
        if (path)
            CoTaskMemFree(path);
    }
    item->Release();
}

void
CCabinetWindow::SyncTabs()
{
    CabinetTabItem items[CABINET_MAX_TABS];

    if (!m_TabBand)
        return;
    for (int i = 0; i < m_TabCount; i++)
    {
        items[i].Title = m_Tabs[i].Title;
        items[i].IconIndex = m_Tabs[i].IconIndex;
    }
    m_TabBand->SetTabs(items, m_TabCount, m_ActiveTab);
}

void
CCabinetWindow::MoveTab(int from, int to)
{
    CabinetTab moved;

    if (from == to || from < 0 || to < 0 || from >= m_TabCount || to >= m_TabCount)
        return;
    moved = m_Tabs[from];
    if (from < to)
        MoveMemory(&m_Tabs[from], &m_Tabs[from + 1], (to - from) * sizeof(m_Tabs[0]));
    else
        MoveMemory(&m_Tabs[to + 1], &m_Tabs[to], (from - to) * sizeof(m_Tabs[0]));
    m_Tabs[to] = moved;
    if (m_ActiveTab == from)
        m_ActiveTab = to;
    else if (from < m_ActiveTab && m_ActiveTab <= to)
        m_ActiveTab--;
    else if (to <= m_ActiveTab && m_ActiveTab < from)
        m_ActiveTab++;
    SyncTabs();
}

void
CCabinetWindow::FocusView()
{
    IShellView *view = CurrentView();

    if (view)
    {
        HWND window = NULL;
        view->UIActivate(SVUIA_ACTIVATE_FOCUS);
        if (SUCCEEDED(view->GetWindow(&window)) && window)
        {
            HWND focus = GetFocus();
            if (focus != window && !IsChild(window, focus))
                SetFocus(window);
        }
        view->Release();
    }
}

void
CCabinetWindow::RestoreFocus()
{
    if (m_FocusSave && IsWindow(m_FocusSave) && IsChild(m_hwnd, m_FocusSave) && IsWindowVisible(m_FocusSave))
        SetFocus(m_FocusSave);
    else
        FocusView();
}

void
CCabinetWindow::CycleFocus()
{
    HWND focus = GetFocus(), view = NULL;
    IShellView *shellView = CurrentView();

    if (shellView)
    {
        shellView->GetWindow(&view);
        shellView->Release();
    }
    if (focus == m_AddressEdit)
    {
        m_AddressBand->EndEdit();
        SetFocus(m_SearchEdit);
    }
    else if (focus == m_SearchEdit && m_NavigationVisible && m_NavigationWindow)
        SetFocus(FindWindowExW(m_NavigationWindow, NULL, WC_TREEVIEWW, NULL) ?
                 FindWindowExW(m_NavigationWindow, NULL, WC_TREEVIEWW, NULL) : m_NavigationWindow);
    else if (view && (focus == m_SearchEdit || (m_NavigationWindow && (focus == m_NavigationWindow ||
                                                                       IsChild(m_NavigationWindow, focus)))))
        SetFocus(view);
    else
        EditAddress();
}

void
CCabinetWindow::SavePlacement()
{
    WINDOWPLACEMENT placement = { sizeof(placement) };
    HKEY key;

    if (!GetWindowPlacement(m_hwnd, &placement))
        return;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\LiberNT\\Explorer", 0, NULL, 0, KEY_SET_VALUE, NULL, &key,
                        NULL) == ERROR_SUCCESS)
    {
        DWORD pane = m_PreviewPane ? 2 : m_DetailsPane ? 1 : 0;
        RegSetValueExW(key, L"Placement", 0, REG_BINARY, (const BYTE *)&placement, sizeof(placement));
        RegSetValueExW(key, L"Pane", 0, REG_DWORD, (const BYTE *)&pane, sizeof(pane));
        RegCloseKey(key);
    }
}

void
CCabinetWindow::RefreshAllViews()
{
    for (int i = 0; i < m_TabCount; i++)
    {
        IShellView *view = NULL;
        if (m_Tabs[i].Browser && SUCCEEDED(m_Tabs[i].Browser->GetCurrentView(IID_PPV_ARG(IShellView, &view))))
        {
            view->Refresh();
            view->Release();
        }
    }
}

void
CCabinetWindow::StartSearch()
{
    static const WCHAR FindFolder[] = L"shell:::{E17D4FC0-5564-11D1-83F2-00A0C90DC849}";
    CabinetTab *tab = ActiveTab();
    PIDLIST_ABSOLUTE find = NULL;
    WCHAR query[MAX_PATH], path[MAX_PATH];

    if (!tab || !tab->Pidl)
        return;
    GetWindowTextW(m_SearchEdit, query, _countof(query));
    StrTrimW(query, L" \t");
    if (!query[0])
        return;
    if (!m_SearchActive || tab->Browser != m_SearchBrowser)
    {
        m_SearchPathCount = 0;
        if (SHGetPathFromIDListW(tab->Pidl, path))
        {
            StringCchCopyW(m_SearchPaths[m_SearchPathCount++], MAX_PATH, path);
        }
        else
        {
            WCHAR drives[128];
            DWORD length = GetLogicalDriveStringsW(_countof(drives), drives);
            for (PWSTR drive = drives; length && *drive && m_SearchPathCount < 26; drive += lstrlenW(drive) + 1)
            {
                UINT type = GetDriveTypeW(drive);
                if (type == DRIVE_FIXED || type == DRIVE_REMOVABLE)
                    StringCchCopyW(m_SearchPaths[m_SearchPathCount++], MAX_PATH, drive);
            }
        }
    }
    if (!m_SearchPathCount)
        return;
    StringCchCopyW(m_SearchQuery, _countof(m_SearchQuery), query);
    m_SearchBrowser = tab->Browser;
    if (m_SearchActive)
    {
        RunPendingSearch(tab);
        return;
    }
    if (SUCCEEDED(SHParseDisplayName(FindFolder, NULL, &find, 0, NULL)))
    {
        Navigate(find);
        ILFree(find);
    }
}

void
CCabinetWindow::RunPendingSearch(CabinetTab *tab)
{
    IShellView *view = NULL;
    HWND window = NULL;
    PCWSTR paths[26];

    if (!m_SearchQuery[0] || !tab || tab->Browser != m_SearchBrowser)
        return;
    if (SUCCEEDED(tab->Browser->GetCurrentView(IID_PPV_ARG(IShellView, &view))))
    {
        if (SUCCEEDED(view->GetWindow(&window)) && window)
        {
            for (int i = 0; i < m_SearchPathCount; i++)
                paths[i] = m_SearchPaths[i];
            if (SUCCEEDED(ShellFind_StartSearch(window, paths, m_SearchPathCount, m_SearchQuery)))
            {
                m_SearchActive = TRUE;
                SetTimer(m_hwnd, IDT_CABINET_SEARCH, 500, NULL);
            }
        }
        view->Release();
    }
    m_SearchQuery[0] = UNICODE_NULL;
}

void
CCabinetWindow::ShowTabMenu(int index, POINT screen)
{
    HMENU popup = CreatePopupMenu();
    WCHAR text[64];
    UINT command;

    LoadText(IDS_CAB_OPEN_NEW_TAB, text, _countof(text));
    AppendMenuW(popup, MF_STRING, 1, text);
    LoadText(IDS_CAB_OPEN_NEW_WINDOW, text, _countof(text));
    AppendMenuW(popup, MF_STRING, 2, text);
    AppendMenuW(popup, MF_SEPARATOR, 0, NULL);
    LoadText(IDS_CAB_CLOSE_TAB, text, _countof(text));
    AppendMenuW(popup, MF_STRING, 3, text);
    LoadText(IDS_CAB_CLOSE_OTHER_TABS, text, _countof(text));
    AppendMenuW(popup, MF_STRING | (m_TabCount > 1 ? 0 : MF_GRAYED), 4, text);
    command = TrackPopupMenuEx(popup, TPM_RETURNCMD, screen.x, screen.y, m_hwnd, NULL);
    DestroyMenu(popup);
    switch (command)
    {
        case 1:
            if (m_Tabs[index].Pidl)
                AddTab(m_Tabs[index].Pidl, TRUE);
            break;
        case 2:
            if (m_Tabs[index].Pidl)
                Cabinet_OpenWindow(m_Tabs[index].Pidl, NULL, SW_SHOWNORMAL);
            break;
        case 3:
            CloseTab(index);
            break;
        case 4:
            for (int i = m_TabCount - 1; i >= 0; i--)
            {
                if (i != index)
                {
                    CloseTab(i);
                    if (i < index)
                        index--;
                }
            }
            ActivateTab(index);
            break;
    }
}

HRESULT
CCabinetWindow::Open(PCIDLIST_ABSOLUTE pidl, PCUITEMID_CHILD select, int showCmd)
{
    WNDCLASSEXW wc = { sizeof(wc) };
    WINDOWPLACEMENT placement = { sizeof(placement) };
    DWORD size = sizeof(placement);

    if (!GetClassInfoExW(g_hInstance, CABINET_CLASS, &wc))
    {
        wc.cbSize = sizeof(wc);
        wc.style = CS_DBLCLKS;
        wc.lpfnWndProc = WindowProc;
        wc.hInstance = g_hInstance;
        wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
        wc.lpszClassName = CABINET_CLASS;
        if (!RegisterClassExW(&wc))
            return HRESULT_FROM_WIN32(GetLastError());
    }
    if (!CreateWindowExW(WS_EX_WINDOWEDGE, CABINET_CLASS, L"File Explorer", WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
                         CW_USEDEFAULT, CW_USEDEFAULT, 1000, 680, NULL, NULL, g_hInstance, this))
        return HRESULT_FROM_WIN32(GetLastError());
    if (RegGetValueW(HKEY_CURRENT_USER, L"Software\\LiberNT\\Explorer", L"Placement", RRF_RT_REG_BINARY, NULL,
                     &placement, &size) == ERROR_SUCCESS && size == sizeof(placement) &&
        placement.length == sizeof(placement))
    {
        if (showCmd == SW_SHOWNORMAL || showCmd == SW_SHOWDEFAULT || showCmd == SW_SHOW)
            placement.showCmd = placement.showCmd == SW_SHOWMAXIMIZED ? SW_SHOWMAXIMIZED : SW_SHOWNORMAL;
        else
            placement.showCmd = showCmd;
        placement.flags = 0;
        SetWindowPlacement(m_hwnd, &placement);
    }
    else
    {
        ShowWindow(m_hwnd, showCmd);
    }
    {
        DWORD pane = 0;
        size = sizeof(pane);
        if (RegGetValueW(HKEY_CURRENT_USER, L"Software\\LiberNT\\Explorer", L"Pane", RRF_RT_REG_DWORD, NULL, &pane,
                         &size) == ERROR_SUCCESS)
            SetPane(pane == 1, pane == 2);
    }
    if (AddTab(pidl, TRUE) < 0)
    {
        DestroyWindow(m_hwnd);
        return E_FAIL;
    }
    if (select)
    {
        IShellView *view = CurrentView();
        if (view)
        {
            view->SelectItem(select, SVSI_SELECT | SVSI_FOCUSED | SVSI_ENSUREVISIBLE | SVSI_DESELECTOTHERS);
            view->Release();
        }
    }
    return S_OK;
}

HRESULT
Cabinet_GetDefaultFolder(PIDLIST_ABSOLUTE *pidl)
{
    *pidl = NULL;
    if (SUCCEEDED(SHGetKnownFolderIDList(FOLDERID_ComputerFolder, 0, NULL, pidl)) && *pidl)
        return S_OK;
    return SHGetSpecialFolderLocation(NULL, CSIDL_DRIVES, pidl);
}

void
CCabinetWindow::WatchFolder()
{
    CabinetTab *tab = ActiveTab();
    SHChangeNotifyEntry entry;

    if (m_ChangeNotify)
        SHChangeNotifyDeregister(m_ChangeNotify);
    m_ChangeNotify = 0;
    if (!tab || !tab->Pidl)
        return;
    entry.pidl = tab->Pidl;
    entry.fRecursive = FALSE;
    m_ChangeNotify = SHChangeNotifyRegister(m_hwnd, SHCNRF_ShellLevel | SHCNRF_InterruptLevel,
                                            SHCNE_CREATE | SHCNE_DELETE | SHCNE_MKDIR | SHCNE_RMDIR |
                                            SHCNE_RENAMEITEM | SHCNE_RENAMEFOLDER | SHCNE_UPDATEDIR |
                                            SHCNE_UPDATEITEM, WM_CABINET_CHANGED, 1, &entry);
}

void
CCabinetWindow::CreateNavigationPane()
{
    static const KNOWNFOLDERID *QuickAccess[] =
    {
        &FOLDERID_Desktop, &FOLDERID_Downloads, &FOLDERID_Documents, &FOLDERID_Pictures, &FOLDERID_Music,
        &FOLDERID_Videos
    };
    static const KNOWNFOLDERID *Locations[] = { &FOLDERID_ComputerFolder, &FOLDERID_NetworkFolder };
    IUnknown *events = NULL;
    DWORD cookie = 0;
    IOleWindow *window = NULL;
    RECT rc = { 0, 0, 0, 0 };

    if (FAILED(CoCreateInstance(CLSID_NamespaceTreeControl, NULL, CLSCTX_INPROC_SERVER,
                                IID_PPV_ARG(INameSpaceTreeControl, &m_Navigation))))
        return;
    if (FAILED(m_Navigation->Initialize(m_hwnd, &rc, NSTCS_HASEXPANDOS | NSTCS_ROOTHASEXPANDO |
                                        NSTCS_SINGLECLICKEXPAND | NSTCS_FULLROWSELECT |
                                        NSTCS_SHOWSELECTIONALWAYS | NSTCS_AUTOHSCROLL)))
    {
        m_Navigation->Release();
        m_Navigation = NULL;
        return;
    }
    for (UINT i = 0; i < _countof(QuickAccess); i++)
    {
        IShellItem *item;
        if (SUCCEEDED(SHGetKnownFolderItem(*QuickAccess[i], KF_FLAG_DEFAULT, NULL, IID_PPV_ARG(IShellItem, &item))))
        {
            m_Navigation->AppendRoot(item, SHCONTF_FOLDERS, NSTCRS_VISIBLE, NULL);
            item->Release();
        }
    }
    for (UINT i = 0; i < _countof(Locations); i++)
    {
        IShellItem *item;
        if (SUCCEEDED(SHGetKnownFolderItem(*Locations[i], KF_FLAG_DEFAULT, NULL, IID_PPV_ARG(IShellItem, &item))))
        {
            m_Navigation->AppendRoot(item, SHCONTF_FOLDERS, NSTCRS_VISIBLE, NULL);
            item->Release();
        }
    }
    if (SUCCEEDED(CabinetNavigation_Create(this, &events)))
    {
        m_Navigation->TreeAdvise(events, &cookie);
        events->Release();
    }
    if (SUCCEEDED(m_Navigation->QueryInterface(IID_PPV_ARG(IOleWindow, &window))))
    {
        window->GetWindow(&m_NavigationWindow);
        window->Release();
    }
    if (m_NavigationWindow)
    {
        HWND tree = FindWindowExW(m_NavigationWindow, NULL, WC_TREEVIEWW, NULL);
        if (tree)
            TreeView_SetItemHeight(tree, Scale(32));
    }
}

int
CCabinetWindow::AddTab(PCIDLIST_ABSOLUTE pidl, BOOL activate)
{
    CabinetTab *tab;
    FOLDERSETTINGS settings = { (UINT)FVM_AUTO, FWF_NOWEBVIEW };
    IObjectWithSite *withSite = NULL;
    RECT rc = { 0, 0, 0, 0 };
    int index;

    if (m_TabCount >= CABINET_MAX_TABS)
        return -1;
    index = m_TabCount;
    tab = &m_Tabs[index];
    ZeroMemory(tab, sizeof(*tab));
    tab->IconIndex = -1;
    if (FAILED(CoCreateInstance(CLSID_ExplorerBrowser, NULL, CLSCTX_INPROC_SERVER,
                                IID_PPV_ARG(IExplorerBrowser, &tab->Browser))))
        return -1;
    tab->Browser->SetOptions((EXPLORER_BROWSER_OPTIONS)(EBO_NOBORDER | EBO_NOTRAVELLOG));
    if (FAILED(CabinetSite_Create(this, tab->Browser, &tab->Site)) ||
        FAILED(tab->Browser->Initialize(m_hwnd, &rc, &settings)))
    {
        if (tab->Site)
            tab->Site->Release();
        tab->Browser->Release();
        ZeroMemory(tab, sizeof(*tab));
        return -1;
    }
    if (SUCCEEDED(tab->Browser->QueryInterface(IID_PPV_ARG(IObjectWithSite, &withSite))))
    {
        withSite->SetSite(tab->Site);
        withSite->Release();
    }
    {
        IExplorerBrowserEvents *events = NULL;
        if (SUCCEEDED(tab->Site->QueryInterface(IID_PPV_ARG(IExplorerBrowserEvents, &events))))
        {
            tab->Browser->Advise(events, &tab->EventsCookie);
            events->Release();
        }
    }
    tab->HistoryIndex = -1;
    m_TabCount++;
    if (activate || m_ActiveTab < 0)
        m_ActiveTab = index;
    SyncTabs();
    PlaceBrowsers();
    if (FAILED(tab->Browser->BrowseToIDList(pidl, SBSP_ABSOLUTE)))
    {
        CloseTab(index);
        return -1;
    }
    return index;
}

void
CCabinetWindow::CloseTab(int index)
{
    CabinetTab *tab;

    if (index < 0 || index >= m_TabCount)
        return;
    tab = &m_Tabs[index];
    if (tab->ViewEvents)
    {
        tab->ViewEvents->Unadvise(tab->ViewEventsCookie);
        tab->ViewEvents->Release();
        tab->ViewEvents = NULL;
    }
    if (tab->Browser)
    {
        IObjectWithSite *withSite;
        tab->Browser->Unadvise(tab->EventsCookie);
        if (SUCCEEDED(tab->Browser->QueryInterface(IID_PPV_ARG(IObjectWithSite, &withSite))))
        {
            withSite->SetSite(NULL);
            withSite->Release();
        }
        tab->Browser->Destroy();
        tab->Browser->Release();
    }
    if (tab->Site)
        tab->Site->Release();
    for (int i = 0; i < tab->HistoryCount; i++)
        FreeIdList(&tab->History[i]);
    FreeIdList(&tab->Pidl);
    MoveMemory(&m_Tabs[index], &m_Tabs[index + 1], (m_TabCount - index - 1) * sizeof(CabinetTab));
    m_TabCount--;
    ZeroMemory(&m_Tabs[m_TabCount], sizeof(CabinetTab));
    if (!m_TabCount)
    {
        m_ActiveTab = -1;
        DestroyWindow(m_hwnd);
        return;
    }
    if (m_ActiveTab >= m_TabCount || m_ActiveTab > index)
        m_ActiveTab--;
    if (m_ActiveTab < 0)
        m_ActiveTab = 0;
    SyncTabs();
    ActivateTab(m_ActiveTab);
}

void
CCabinetWindow::ActivateTab(int index)
{
    if (index < 0 || index >= m_TabCount)
        return;
    m_ActiveTab = index;
    SyncTabs();
    PlaceBrowsers();
    UpdateTitle(ActiveTab());
    UpdateAddress();
    UpdateStatus();
    UpdateCommands();
    UpdateDetails();
    UpdatePreview();
    SyncNavigationPane();
    WatchFolder();
    if (GetActiveWindow() == m_hwnd)
        PostMessageW(m_hwnd, WM_CABINET_FOCUSVIEW, 0, 0);
}

void
CCabinetWindow::PlaceBrowsers()
{
    RECT empty = { 0, 0, 0, 0 };

    for (int i = 0; i < m_TabCount; i++)
    {
        if (m_Tabs[i].Browser)
            m_Tabs[i].Browser->SetRect(NULL, i == m_ActiveTab ? m_ViewRect : empty);
    }
}

IShellView *
CCabinetWindow::CurrentView()
{
    CabinetTab *tab = ActiveTab();
    IShellView *view = NULL;

    if (tab && tab->Browser)
        tab->Browser->GetCurrentView(IID_PPV_ARG(IShellView, &view));
    return view;
}

void
CCabinetWindow::Navigate(PCIDLIST_ABSOLUTE pidl)
{
    CabinetTab *tab = ActiveTab();

    if (tab && tab->Browser && pidl)
        tab->Browser->BrowseToIDList(pidl, SBSP_ABSOLUTE);
}

void
CCabinetWindow::NavigateHistory(int delta)
{
    CabinetTab *tab = ActiveTab();
    int target;

    if (!tab)
        return;
    target = tab->HistoryIndex + delta;
    if (target < 0 || target >= tab->HistoryCount)
        return;
    m_InHistoryNavigation = TRUE;
    if (SUCCEEDED(tab->Browser->BrowseToIDList(tab->History[target], SBSP_ABSOLUTE)))
        tab->HistoryIndex = target;
    m_InHistoryNavigation = FALSE;
    UpdateCommands();
}

void
CCabinetWindow::NavigateUp()
{
    CabinetTab *tab = ActiveTab();
    PIDLIST_ABSOLUTE parent;

    if (!tab || !tab->Pidl || ILIsEmpty(tab->Pidl))
        return;
    parent = ILClone(tab->Pidl);
    if (parent && ILRemoveLastID(parent))
        Navigate(parent);
    if (parent)
        ILFree(parent);
}

void
CCabinetWindow::Refresh()
{
    IShellView *view = CurrentView();

    if (view)
    {
        view->Refresh();
        view->Release();
    }
    UpdateStatus();
}

void
CCabinetWindow::OnNavigationComplete(IExplorerBrowser *browser, PCIDLIST_ABSOLUTE pidl)
{
    CabinetTab *tab = NULL;

    for (int i = 0; i < m_TabCount; i++)
    {
        if (m_Tabs[i].Browser == browser)
            tab = &m_Tabs[i];
    }
    if (!tab)
        return;
    FreeIdList(&tab->Pidl);
    tab->Pidl = ILClone(pidl);
    if (!m_InHistoryNavigation)
    {
        for (int i = tab->HistoryIndex + 1; i < tab->HistoryCount; i++)
            FreeIdList(&tab->History[i]);
        tab->HistoryCount = tab->HistoryIndex + 1;
        if (tab->HistoryCount == CABINET_MAX_HISTORY)
        {
            FreeIdList(&tab->History[0]);
            MoveMemory(&tab->History[0], &tab->History[1], (CABINET_MAX_HISTORY - 1) * sizeof(tab->History[0]));
            tab->HistoryCount--;
        }
        tab->History[tab->HistoryCount++] = ILClone(pidl);
        tab->HistoryIndex = tab->HistoryCount - 1;
    }
    UpdateTitle(tab);
    SyncTabs();
    if (tab->Browser == m_SearchBrowser)
    {
        CLSID clsid = CLSID_NULL;
        IShellView *view = NULL;
        IFolderView *folderView = NULL;
        IShellFolder *folder = NULL;
        IPersist *persist = NULL;
        if (SUCCEEDED(browser->GetCurrentView(IID_PPV_ARG(IShellView, &view))) &&
            SUCCEEDED(view->QueryInterface(IID_PPV_ARG(IFolderView, &folderView))) &&
            SUCCEEDED(folderView->GetFolder(IID_PPV_ARG(IShellFolder, &folder))) &&
            SUCCEEDED(folder->QueryInterface(IID_PPV_ARG(IPersist, &persist))))
            persist->GetClassID(&clsid);
        if (persist)
            persist->Release();
        if (folder)
            folder->Release();
        if (folderView)
            folderView->Release();
        if (view)
            view->Release();
        if (IsEqualCLSID(clsid, CLSID_FindFolderShell))
            RunPendingSearch(tab);
        else
        {
            m_SearchActive = FALSE;
            m_SearchBrowser = NULL;
            KillTimer(m_hwnd, IDT_CABINET_SEARCH);
        }
    }
    if (tab == ActiveTab())
    {
        if (!m_SearchActive)
            SetWindowTextW(m_SearchEdit, L"");
        UpdateAddress();
        UpdateStatus();
        UpdateCommands();
        SyncNavigationPane();
        WatchFolder();
        PostMessageW(m_hwnd, WM_CABINET_STATUS, 0, 0);
        if (GetActiveWindow() == m_hwnd)
        {
            HWND focus = GetFocus();
            if (m_FocusViewOnNavigate ||
                (focus != m_SearchEdit && focus != m_AddressEdit &&
                 !(m_NavigationWindow && (focus == m_NavigationWindow || IsChild(m_NavigationWindow, focus)))))
                PostMessageW(m_hwnd, WM_CABINET_FOCUSVIEW, 0, 0);
        }
        m_FocusViewOnNavigate = FALSE;
    }
}

void
CCabinetWindow::OnNavigationFailed(IExplorerBrowser *browser, PCIDLIST_ABSOLUTE pidl)
{
    UNREFERENCED_PARAMETER(browser);
    UNREFERENCED_PARAMETER(pidl);
    MessageBeep(MB_ICONWARNING);
}

void
CCabinetWindow::OnViewEvent(DISPID dispid)
{
    if (dispid == DISPID_FILELISTENUMDONE || dispid == DISPID_SELECTIONCHANGED)
        OnSelectionChanged();
}

void
CCabinetWindow::OnViewCreated(IExplorerBrowser *browser, IShellView *view)
{
    CabinetTab *tab = NULL;

    for (int i = 0; i < m_TabCount; i++)
    {
        if (m_Tabs[i].Browser == browser)
            tab = &m_Tabs[i];
    }
    if (tab && tab->ViewEvents)
    {
        tab->ViewEvents->Unadvise(tab->ViewEventsCookie);
        tab->ViewEvents->Release();
        tab->ViewEvents = NULL;
    }
    if (tab && view)
    {
        IDispatch *dispatch = NULL;
        if (SUCCEEDED(view->GetItemObject(SVGIO_BACKGROUND, IID_PPV_ARG(IDispatch, &dispatch))))
        {
            IConnectionPointContainer *container = NULL;
            if (SUCCEEDED(dispatch->QueryInterface(IID_PPV_ARG(IConnectionPointContainer, &container))))
            {
                if (SUCCEEDED(container->FindConnectionPoint(DIID_DShellFolderViewEvents, &tab->ViewEvents)))
                {
                    IUnknown *sink = NULL;
                    if (FAILED(CabinetViewEvents_Create(this, &sink)) ||
                        FAILED(tab->ViewEvents->Advise(sink, &tab->ViewEventsCookie)))
                    {
                        tab->ViewEvents->Release();
                        tab->ViewEvents = NULL;
                    }
                    if (sink)
                        sink->Release();
                }
                container->Release();
            }
            dispatch->Release();
        }
    }
}

void
CCabinetWindow::OnSelectionChanged()
{
    UpdateStatus();
    UpdateCommands();
    UpdateDetails();
    UpdatePreview();
}

HRESULT
CCabinetWindow::OnDefaultCommand(IShellView *view)
{
    UNREFERENCED_PARAMETER(view);
    return S_FALSE;
}

void
CCabinetWindow::OnNavigationItem(IShellItem *item, BOOL newTab)
{
    PIDLIST_ABSOLUTE pidl = NULL;

    if (FAILED(SHGetIDListFromObject(item, &pidl)))
        return;
    if (newTab)
        AddTab(pidl, FALSE);
    else
        Navigate(pidl);
    ILFree(pidl);
}

void
CCabinetWindow::UpdateTitle(CabinetTab *tab)
{
    PWSTR name = NULL;
    WCHAR title[MAX_PATH + 32];

    if (!tab || !tab->Pidl)
        return;
    if (SUCCEEDED(SHGetNameFromIDList(tab->Pidl, SIGDN_NORMALDISPLAY, &name)))
    {
        StringCchCopyW(tab->Title, _countof(tab->Title), name);
        CoTaskMemFree(name);
    }
    tab->IconIndex = SystemIconIndex(tab->Pidl);
    if (tab == ActiveTab())
    {
        WCHAR format[64], cue[MAX_PATH + 64];
        StringCchPrintfW(title, _countof(title), L"%s - File Explorer", tab->Title);
        SetWindowTextW(m_hwnd, title);
        LoadText(IDS_CAB_SEARCH_IN, format, _countof(format));
        StringCchPrintfW(cue, _countof(cue), format, tab->Title);
        SendMessageW(m_SearchEdit, EM_SETCUEBANNER, FALSE, (LPARAM)cue);
        InvalidateRect(m_SearchEdit, NULL, TRUE);
    }
}

void
CCabinetWindow::UpdateAddress()
{
    CabinetTab *tab = ActiveTab();

    if (m_AddressBand)
        m_AddressBand->SetLocation(tab ? tab->Pidl : NULL);
}

void
CCabinetWindow::EditAddress()
{
    if (m_AddressBand)
        m_AddressBand->BeginEdit();
}

void
CCabinetWindow::NavigateToAddress(LPCWSTR address)
{
    CabinetTab *tab = ActiveTab();
    WCHAR text[MAX_PATH * 2];
    PIDLIST_ABSOLUTE pidl = NULL;
    PWSTR current = NULL;

    StringCchCopyW(text, _countof(text), address);
    StrTrimW(text, L" \t");
    if (!text[0])
        return;
    if (tab && tab->Pidl && SUCCEEDED(SHGetNameFromIDList(tab->Pidl, SIGDN_DESKTOPABSOLUTEEDITING, &current)))
    {
        BOOL same = !lstrcmpiW(current, text);
        CoTaskMemFree(current);
        if (same)
        {
            m_AddressBand->EndEdit();
            FocusView();
            Refresh();
            return;
        }
    }
    if (SUCCEEDED(SHParseDisplayName(text, NULL, &pidl, 0, NULL)))
    {
        SFGAOF attributes = 0;
        IShellFolder *parent;
        PCUITEMID_CHILD child;
        if (SUCCEEDED(SHBindToParent(pidl, IID_PPV_ARG(IShellFolder, &parent), &child)))
        {
            attributes = SFGAO_FOLDER;
            parent->GetAttributesOf(1, &child, &attributes);
            parent->Release();
        }
        if (ILIsEmpty(pidl) || (attributes & SFGAO_FOLDER))
        {
            m_FocusViewOnNavigate = TRUE;
            Navigate(pidl);
        }
        else
        {
            m_AddressBand->EndEdit();
            FocusView();
            ShellExecuteW(m_hwnd, NULL, text, NULL, NULL, SW_SHOWNORMAL);
        }
        ILFree(pidl);
    }
    else if ((INT_PTR)ShellExecuteW(m_hwnd, NULL, text, NULL, NULL, SW_SHOWNORMAL) <= 32)
        MessageBeep(MB_ICONWARNING);
    else
    {
        m_AddressBand->EndEdit();
        FocusView();
    }
}

void
CCabinetWindow::OnTabSelect(int index)
{
    ActivateTab(index);
}

void
CCabinetWindow::OnTabClose(int index)
{
    CloseTab(index);
}

void
CCabinetWindow::OnTabMove(int from, int to)
{
    MoveTab(from, to);
}

void
CCabinetWindow::OnTabMenu(int index, POINT screen)
{
    ShowTabMenu(index, screen);
}

void
CCabinetWindow::OnNewTab()
{
    ExecuteCommand(CabinetCommand::NewTab);
}

void
CCabinetWindow::OnAddressNavigate(PCIDLIST_ABSOLUTE pidl)
{
    m_FocusViewOnNavigate = TRUE;
    Navigate(pidl);
}

void
CCabinetWindow::OnAddressText(LPCWSTR text)
{
    NavigateToAddress(text);
}

void
CCabinetWindow::OnAddressCancel()
{
    FocusView();
}

void
CCabinetWindow::UpdateViewToggles()
{
    IShellView *view = CurrentView();
    IFolderView2 *folderView = NULL;
    FOLDERVIEWMODE mode = FVM_AUTO;
    int size = 0;

    if (view && SUCCEEDED(view->QueryInterface(IID_PPV_ARG(IFolderView2, &folderView))))
    {
        folderView->GetViewModeAndIconSize(&mode, &size);
        folderView->Release();
    }
    if (view)
        view->Release();
    SendMessageW(m_ViewBar, TB_CHECKBUTTON, IDC_COMMAND(CabinetCommand::ViewDetails), MAKELONG(mode == FVM_DETAILS, 0));
    SendMessageW(m_ViewBar, TB_CHECKBUTTON, IDC_COMMAND(CabinetCommand::ViewLarge),
                 MAKELONG(mode == FVM_ICON && size >= 96, 0));
}

void
CCabinetWindow::UpdateStatus()
{
    IShellView *view = CurrentView();
    IFolderView *folderView = NULL;
    int count = 0, selected = 0;
    WCHAR format[64], items[64] = L"", selection[128] = L"";
    ULONGLONG selectedSize = 0;
    BOOL haveSize = FALSE;

    if (!view)
        return;
    if (SUCCEEDED(view->QueryInterface(IID_PPV_ARG(IFolderView, &folderView))))
    {
        IShellItemArray *array = NULL;
        folderView->ItemCount(SVGIO_ALLVIEW, &count);
        folderView->ItemCount(SVGIO_SELECTION, &selected);
        if (selected && selected <= 4096 &&
            SUCCEEDED(folderView->Items(SVGIO_SELECTION, IID_PPV_ARG(IShellItemArray, &array))))
        {
            DWORD itemCount = 0;
            array->GetCount(&itemCount);
            for (DWORD i = 0; i < itemCount; i++)
            {
                IShellItem *item = NULL;
                IShellItem2 *item2 = NULL;
                ULONGLONG size = 0;
                if (SUCCEEDED(array->GetItemAt(i, &item)) &&
                    SUCCEEDED(item->QueryInterface(IID_PPV_ARG(IShellItem2, &item2))) &&
                    SUCCEEDED(item2->GetUInt64(PKEY_Size, &size)))
                {
                    selectedSize += size;
                    haveSize = TRUE;
                }
                if (item2)
                    item2->Release();
                if (item)
                    item->Release();
            }
            array->Release();
        }
        folderView->Release();
    }
    view->Release();
    if (count == 1)
        LoadText(IDS_CAB_ITEM, items, _countof(items));
    else
    {
        LoadText(IDS_CAB_ITEMS, format, _countof(format));
        StringCchPrintfW(items, _countof(items), format, count);
    }
    if (selected)
    {
        if (selected == 1)
            LoadText(IDS_CAB_SELECTED_ONE, selection, _countof(selection));
        else
        {
            LoadText(IDS_CAB_SELECTED, format, _countof(format));
            StringCchPrintfW(selection, _countof(selection), format, selected);
        }
        if (haveSize)
        {
            WCHAR text[32];
            if (StrFormatByteSizeW((LONGLONG)selectedSize, text, _countof(text)))
            {
                StringCchCatW(selection, _countof(selection), L"    ");
                StringCchCatW(selection, _countof(selection), text);
            }
        }
    }
    SendMessageW(m_Status, SB_SETTEXTW, 0, (LPARAM)items);
    SendMessageW(m_Status, SB_SETTEXTW, 1, (LPARAM)selection);
    UpdateViewToggles();
}

void
CCabinetWindow::UpdateCommands()
{
    static const CabinetCommand Commands[] =
    {
        CabinetCommand::Back, CabinetCommand::Forward, CabinetCommand::Up, CabinetCommand::Cut,
        CabinetCommand::Copy, CabinetCommand::Paste, CabinetCommand::Rename, CabinetCommand::Delete
    };

    for (UINT i = 0; i < _countof(Commands); i++)
    {
        HWND bar = Commands[i] <= CabinetCommand::Up ? m_NavBar : m_CommandBar;
        SendMessageW(bar, TB_ENABLEBUTTON, IDC_COMMAND(Commands[i]), MAKELONG(CommandEnabled(Commands[i]), 0));
    }
}

BOOL
CCabinetWindow::CommandEnabled(CabinetCommand command)
{
    CabinetTab *tab = ActiveTab();
    IShellView *view;
    IFolderView *folderView;
    int selected = 0;

    switch (command)
    {
        case CabinetCommand::Back:
            return tab && tab->HistoryIndex > 0;
        case CabinetCommand::Forward:
            return tab && tab->HistoryIndex + 1 < tab->HistoryCount;
        case CabinetCommand::Up:
            return tab && tab->Pidl && !ILIsEmpty(tab->Pidl);
        case CabinetCommand::Cut:
        case CabinetCommand::Copy:
        case CabinetCommand::Rename:
        case CabinetCommand::Delete:
            view = CurrentView();
            if (view && SUCCEEDED(view->QueryInterface(IID_PPV_ARG(IFolderView, &folderView))))
            {
                folderView->ItemCount(SVGIO_SELECTION, &selected);
                folderView->Release();
            }
            if (view)
                view->Release();
            return command == CabinetCommand::Rename ? selected == 1 : selected > 0;
        case CabinetCommand::Paste:
            return IsClipboardFormatAvailable(CF_HDROP) ||
                   IsClipboardFormatAvailable(RegisterClipboardFormatW(CFSTR_SHELLIDLIST));
        default:
            return TRUE;
    }
}

void
CCabinetWindow::InvokeSelectionVerb(LPCSTR verb)
{
    IShellView *view = CurrentView();
    IContextMenu *menu = NULL;

    if (!view)
        return;
    if (SUCCEEDED(view->GetItemObject(SVGIO_SELECTION, IID_PPV_ARG(IContextMenu, &menu))))
    {
        CMINVOKECOMMANDINFOEX info = { sizeof(info) };
        HMENU popup = CreatePopupMenu();
        menu->QueryContextMenu(popup, 0, 1, 0x7FFF, CMF_NORMAL);
        info.fMask = CMIC_MASK_UNICODE;
        info.hwnd = m_hwnd;
        info.lpVerb = verb;
        info.nShow = SW_SHOWNORMAL;
        menu->InvokeCommand((LPCMINVOKECOMMANDINFO)&info);
        DestroyMenu(popup);
        menu->Release();
    }
    view->Release();
    UpdateStatus();
}

void
CCabinetWindow::InvokeBackgroundVerb(LPCSTR verb)
{
    IShellView *view = CurrentView();
    IContextMenu *menu = NULL;

    if (!view)
        return;
    if (SUCCEEDED(view->GetItemObject(SVGIO_BACKGROUND, IID_PPV_ARG(IContextMenu, &menu))))
    {
        CMINVOKECOMMANDINFOEX info = { sizeof(info) };
        HMENU popup = CreatePopupMenu();
        menu->QueryContextMenu(popup, 0, 1, 0x7FFF, CMF_NORMAL);
        info.fMask = CMIC_MASK_UNICODE;
        info.hwnd = m_hwnd;
        info.lpVerb = verb;
        info.nShow = SW_SHOWNORMAL;
        menu->InvokeCommand((LPCMINVOKECOMMANDINFO)&info);
        DestroyMenu(popup);
        menu->Release();
    }
    view->Release();
}

RECT
CCabinetWindow::MenuAnchor(CabinetCommand command)
{
    RECT rc = { 0, 0, 0, 0 };
    HWND bar = command <= CabinetCommand::Up ? m_NavBar : m_CommandBar;

    if (!SendMessageW(bar, TB_GETRECT, IDC_COMMAND(command), (LPARAM)&rc) || IsRectEmpty(&rc) ||
        (SendMessageW(bar, TB_GETSTATE, IDC_COMMAND(command), 0) & TBSTATE_HIDDEN))
    {
        bar = m_CommandBar;
        SetRectEmpty(&rc);
        SendMessageW(bar, TB_GETRECT, IDC_COMMAND(CabinetCommand::More), (LPARAM)&rc);
    }
    MapWindowPoints(bar, NULL, (POINT *)&rc, 2);
    return rc;
}

void
CCabinetWindow::ShowNewMenu()
{
    CabinetTab *tab = ActiveTab();
    IContextMenu *menu = NULL;
    IShellExtInit *init = NULL;
    HMENU popup;
    RECT anchor = MenuAnchor(CabinetCommand::NewItem);
    UINT command;

    if (!tab || !tab->Pidl)
        return;
    if (FAILED(CoCreateInstance(CLSID_NewMenu, NULL, CLSCTX_INPROC_SERVER, IID_PPV_ARG(IContextMenu, &menu))))
        return;
    if (SUCCEEDED(menu->QueryInterface(IID_PPV_ARG(IShellExtInit, &init))))
    {
        init->Initialize(tab->Pidl, NULL, NULL);
        init->Release();
    }
    {
        IObjectWithSite *withSite;
        IShellView *view = CurrentView();
        if (view && SUCCEEDED(menu->QueryInterface(IID_PPV_ARG(IObjectWithSite, &withSite))))
        {
            withSite->SetSite(view);
            withSite->Release();
        }
        if (view)
            view->Release();
    }
    popup = CreatePopupMenu();
    if (SUCCEEDED(menu->QueryContextMenu(popup, 0, 1, 0x7FFF, CMF_NORMAL)))
    {
        HMENU source = popup;
        if (GetMenuItemCount(popup) == 1 && GetSubMenu(popup, 0))
        {
            IContextMenu3 *menu3;
            MENUITEMINFOW mii = { sizeof(mii), MIIM_SUBMENU };
            GetMenuItemInfoW(popup, 0, TRUE, &mii);
            if (SUCCEEDED(menu->QueryInterface(IID_PPV_ARG(IContextMenu3, &menu3))))
            {
                menu3->HandleMenuMsg2(WM_INITMENUPOPUP, (WPARAM)mii.hSubMenu, 0, NULL);
                menu3->Release();
            }
            source = mii.hSubMenu;
        }
        command = TrackPopupMenuEx(source, TPM_RETURNCMD | TPM_LEFTALIGN | TPM_TOPALIGN, anchor.left, anchor.bottom,
                                   m_hwnd, NULL);
        if (command)
        {
            CMINVOKECOMMANDINFO info = { sizeof(info) };
            info.hwnd = m_hwnd;
            info.lpVerb = MAKEINTRESOURCEA(command - 1);
            info.nShow = SW_SHOWNORMAL;
            menu->InvokeCommand(&info);
        }
    }
    DestroyMenu(popup);
    menu->Release();
}

void
CCabinetWindow::ShowSortMenu()
{
    static const struct { UINT Text; const PROPERTYKEY *Key; } Columns[] =
    {
        { IDS_CAB_SORT_NAME, &PKEY_ItemNameDisplay },
        { IDS_CAB_SORT_DATE, &PKEY_DateModified },
        { IDS_CAB_SORT_TYPE, &PKEY_ItemTypeText },
        { IDS_CAB_SORT_SIZE, &PKEY_Size },
    };
    IShellView *view = CurrentView();
    IFolderView2 *folderView = NULL;
    SORTCOLUMN current = { PKEY_ItemNameDisplay, SORT_ASCENDING };
    HMENU popup;
    RECT anchor = MenuAnchor(CabinetCommand::Sort);
    WCHAR text[64];
    UINT command;

    if (!view)
        return;
    if (FAILED(view->QueryInterface(IID_PPV_ARG(IFolderView2, &folderView))))
    {
        view->Release();
        return;
    }
    folderView->GetSortColumns(&current, 1);
    popup = CreatePopupMenu();
    for (UINT i = 0; i < _countof(Columns); i++)
    {
        LoadText(Columns[i].Text, text, _countof(text));
        AppendMenuW(popup, MF_STRING | (IsEqualPropertyKey(current.propkey, *Columns[i].Key) ? MF_CHECKED : 0),
                    1 + i, text);
    }
    AppendMenuW(popup, MF_SEPARATOR, 0, NULL);
    LoadText(IDS_CAB_SORT_ASCENDING, text, _countof(text));
    AppendMenuW(popup, MF_STRING | (current.direction == SORT_ASCENDING ? MF_CHECKED : 0), 100, text);
    LoadText(IDS_CAB_SORT_DESCENDING, text, _countof(text));
    AppendMenuW(popup, MF_STRING | (current.direction == SORT_DESCENDING ? MF_CHECKED : 0), 101, text);
    command = TrackPopupMenuEx(popup, TPM_RETURNCMD | TPM_LEFTALIGN | TPM_TOPALIGN, anchor.left, anchor.bottom,
                               m_hwnd, NULL);
    DestroyMenu(popup);
    if (command >= 1 && command <= _countof(Columns))
        current.propkey = *Columns[command - 1].Key;
    else if (command == 100)
        current.direction = SORT_ASCENDING;
    else if (command == 101)
        current.direction = SORT_DESCENDING;
    if (command)
        folderView->SetSortColumns(&current, 1);
    folderView->Release();
    view->Release();
}

void
CCabinetWindow::ShowViewMenu()
{
    static const struct { UINT Text; FOLDERVIEWMODE Mode; int Size; } Modes[] =
    {
        { IDS_CAB_VIEW_XLARGE, FVM_ICON, 256 },
        { IDS_CAB_VIEW_LARGE, FVM_ICON, 96 },
        { IDS_CAB_VIEW_MEDIUM, FVM_ICON, 48 },
        { IDS_CAB_VIEW_SMALL, FVM_SMALLICON, 16 },
        { IDS_CAB_VIEW_LIST, FVM_LIST, 16 },
        { IDS_CAB_VIEW_DETAILS, FVM_DETAILS, 16 },
        { IDS_CAB_VIEW_TILES, FVM_TILE, 48 },
        { IDS_CAB_VIEW_CONTENT, FVM_CONTENT, 32 },
    };
    IShellView *view = CurrentView();
    IFolderView2 *folderView = NULL;
    FOLDERVIEWMODE mode = FVM_DETAILS;
    int size = 16;
    HMENU popup;
    RECT anchor = MenuAnchor(CabinetCommand::View);
    WCHAR text[64];
    UINT command;

    if (!view)
        return;
    if (FAILED(view->QueryInterface(IID_PPV_ARG(IFolderView2, &folderView))))
    {
        view->Release();
        return;
    }
    folderView->GetViewModeAndIconSize(&mode, &size);
    popup = CreatePopupMenu();
    for (UINT i = 0; i < _countof(Modes); i++)
    {
        BOOL checked = Modes[i].Mode == mode && (mode != FVM_ICON || Modes[i].Size == size);
        LoadText(Modes[i].Text, text, _countof(text));
        AppendMenuW(popup, MF_STRING | (checked ? MF_CHECKED : 0), 1 + i, text);
    }
    {
        HMENU show = CreatePopupMenu();
        SHELLSTATE state = { };
        DWORD flags = 0;
        SHGetSetSettings(&state, SSF_SHOWEXTENSIONS | SSF_SHOWALLOBJECTS, FALSE);
        folderView->GetCurrentFolderFlags(&flags);
        LoadText(IDS_CAB_SHOW_NAVIGATION, text, _countof(text));
        AppendMenuW(show, MF_STRING | (m_NavigationVisible ? MF_CHECKED : 0), 50, text);
        LoadText(IDS_CAB_SHOW_DETAILS, text, _countof(text));
        AppendMenuW(show, MF_STRING | (m_DetailsPane ? MF_CHECKED : 0), 51, text);
        LoadText(IDS_CAB_SHOW_PREVIEW, text, _countof(text));
        AppendMenuW(show, MF_STRING | (m_PreviewPane ? MF_CHECKED : 0), 55, text);
        AppendMenuW(show, MF_SEPARATOR, 0, NULL);
        LoadText(IDS_CAB_SHOW_CHECKBOXES, text, _countof(text));
        AppendMenuW(show, MF_STRING | ((flags & FWF_CHECKSELECT) ? MF_CHECKED : 0), 52, text);
        LoadText(IDS_CAB_SHOW_EXTENSIONS, text, _countof(text));
        AppendMenuW(show, MF_STRING | (state.fShowExtensions ? MF_CHECKED : 0), 53, text);
        LoadText(IDS_CAB_SHOW_HIDDEN, text, _countof(text));
        AppendMenuW(show, MF_STRING | (state.fShowAllObjects ? MF_CHECKED : 0), 54, text);
        AppendMenuW(popup, MF_SEPARATOR, 0, NULL);
        LoadText(IDS_CAB_SHOW, text, _countof(text));
        AppendMenuW(popup, MF_POPUP, (UINT_PTR)show, text);
    }
    command = TrackPopupMenuEx(popup, TPM_RETURNCMD | TPM_LEFTALIGN | TPM_TOPALIGN, anchor.left, anchor.bottom,
                               m_hwnd, NULL);
    DestroyMenu(popup);
    if (command >= 1 && command <= _countof(Modes))
    {
        folderView->SetViewModeAndIconSize(Modes[command - 1].Mode, Modes[command - 1].Size);
        UpdateViewToggles();
    }
    else if (command == 50)
    {
        m_NavigationVisible = !m_NavigationVisible;
        OnSize();
    }
    else if (command == 51)
        SetPane(!m_DetailsPane, FALSE);
    else if (command == 55)
        SetPane(FALSE, !m_PreviewPane);
    else if (command == 52)
    {
        DWORD current = 0;
        folderView->GetCurrentFolderFlags(&current);
        folderView->SetCurrentFolderFlags(FWF_CHECKSELECT, (current & FWF_CHECKSELECT) ? 0 : FWF_CHECKSELECT);
    }
    else if (command == 53 || command == 54)
    {
        SHELLSTATE state = { };
        DWORD mask = command == 53 ? SSF_SHOWEXTENSIONS : SSF_SHOWALLOBJECTS;
        SHGetSetSettings(&state, mask, FALSE);
        if (command == 53)
            state.fShowExtensions = !state.fShowExtensions;
        else
            state.fShowAllObjects = !state.fShowAllObjects;
        SHGetSetSettings(&state, mask, TRUE);
        RefreshAllViews();
    }
    folderView->Release();
    view->Release();
}

void
CCabinetWindow::ShowMoreMenu()
{
    IShellView *view = CurrentView();
    IFolderView2 *folderView = NULL;
    HMENU popup = CreatePopupMenu();
    RECT anchor = MenuAnchor(CabinetCommand::More);
    WCHAR text[64];
    UINT command;

    static const struct { CabinetCommand Command; UINT Text; } Hidden[] =
    {
        { CabinetCommand::NewItem, IDS_CAB_NEW }, { CabinetCommand::Cut, IDS_CAB_CUT },
        { CabinetCommand::Copy, IDS_CAB_COPY }, { CabinetCommand::Paste, IDS_CAB_PASTE },
        { CabinetCommand::Rename, IDS_CAB_RENAME }, { CabinetCommand::Delete, IDS_CAB_DELETE },
        { CabinetCommand::Sort, IDS_CAB_SORT }, { CabinetCommand::View, IDS_CAB_VIEW },
    };
    BOOL any = FALSE;

    for (UINT i = 0; i < _countof(Hidden); i++)
    {
        if (!SendMessageW(m_CommandBar, TB_ISBUTTONHIDDEN, IDC_COMMAND(Hidden[i].Command), 0))
            continue;
        LoadText(Hidden[i].Text, text, _countof(text));
        AppendMenuW(popup, MF_STRING | (CommandEnabled(Hidden[i].Command) ? 0 : MF_GRAYED),
                    100 + (UINT)Hidden[i].Command, text);
        any = TRUE;
    }
    if (any)
        AppendMenuW(popup, MF_SEPARATOR, 0, NULL);
    LoadText(IDS_CAB_SELECT_ALL, text, _countof(text));
    AppendMenuW(popup, MF_STRING, 1, text);
    LoadText(IDS_CAB_SELECT_NONE, text, _countof(text));
    AppendMenuW(popup, MF_STRING, 2, text);
    LoadText(IDS_CAB_INVERT_SELECTION, text, _countof(text));
    AppendMenuW(popup, MF_STRING, 3, text);
    AppendMenuW(popup, MF_SEPARATOR, 0, NULL);
    LoadText(IDS_CAB_PROPERTIES, text, _countof(text));
    AppendMenuW(popup, MF_STRING, 4, text);
    LoadText(IDS_CAB_OPTIONS, text, _countof(text));
    AppendMenuW(popup, MF_STRING, 5, text);
    command = TrackPopupMenuEx(popup, TPM_RETURNCMD | TPM_LEFTALIGN | TPM_TOPALIGN, anchor.left, anchor.bottom,
                               m_hwnd, NULL);
    DestroyMenu(popup);
    if (view)
        view->QueryInterface(IID_PPV_ARG(IFolderView2, &folderView));
    switch (command)
    {
        case 1:
            if (folderView)
            {
                int count = 0;
                folderView->ItemCount(SVGIO_ALLVIEW, &count);
                for (int i = 0; i < count; i++)
                    folderView->SelectItem(i, SVSI_SELECT);
            }
            break;
        case 2:
            if (view)
                view->SelectItem(NULL, SVSI_DESELECTOTHERS);
            break;
        case 3:
            if (folderView)
            {
                int count = 0;
                folderView->ItemCount(SVGIO_ALLVIEW, &count);
                for (int i = 0; i < count; i++)
                {
                    PITEMID_CHILD child = NULL;
                    if (SUCCEEDED(folderView->Item(i, &child)))
                    {
                        DWORD state = 0;
                        folderView->GetSelectionState(child, &state);
                        folderView->SelectItem(i, (state & SVSI_SELECT) ? SVSI_DESELECT : SVSI_SELECT);
                        ILFree(child);
                    }
                }
            }
            break;
        case 4:
            ExecuteCommand(CabinetCommand::Properties);
            break;
        case 5:
            ShellExecuteW(m_hwnd, NULL, L"rundll32.exe", L"shell32.dll,Options_RunDLL 0", NULL, SW_SHOWNORMAL);
            break;
    }
    if (folderView)
        folderView->Release();
    if (view)
        view->Release();
    if (command >= 100 && command < 100 + (UINT)CabinetCommand::Count)
        ExecuteCommand((CabinetCommand)(command - 100));
    UpdateStatus();
}

void
CCabinetWindow::SyncNavigationPane()
{
    CabinetTab *tab = ActiveTab();
    IShellItem *item = NULL;

    if (!m_Navigation || !tab || !tab->Pidl)
        return;
    if (SUCCEEDED(SHCreateItemFromIDList(tab->Pidl, IID_PPV_ARG(IShellItem, &item))))
    {
        m_Navigation->SetItemState(item, NSTCIS_SELECTED, NSTCIS_SELECTED);
        item->Release();
    }
}

void
CCabinetWindow::ExecuteCommand(CabinetCommand command)
{
    if (!CommandEnabled(command))
        return;
    switch (command)
    {
        case CabinetCommand::Back: NavigateHistory(-1); break;
        case CabinetCommand::Forward: NavigateHistory(1); break;
        case CabinetCommand::Up: NavigateUp(); break;
        case CabinetCommand::Refresh: Refresh(); break;
        case CabinetCommand::NewItem: ShowNewMenu(); break;
        case CabinetCommand::Cut: InvokeSelectionVerb("cut"); break;
        case CabinetCommand::Copy: InvokeSelectionVerb("copy"); break;
        case CabinetCommand::Paste: InvokeBackgroundVerb("paste"); break;
        case CabinetCommand::Rename:
        {
            IShellView *view = CurrentView();
            IFolderView2 *folderView;
            if (view && SUCCEEDED(view->QueryInterface(IID_PPV_ARG(IFolderView2, &folderView))))
            {
                if (FAILED(folderView->DoRename()))
                    InvokeSelectionVerb("rename");
                folderView->Release();
            }
            if (view)
                view->Release();
            break;
        }
        case CabinetCommand::Delete: InvokeSelectionVerb("delete"); break;
        case CabinetCommand::Sort: ShowSortMenu(); break;
        case CabinetCommand::View: ShowViewMenu(); break;
        case CabinetCommand::More: ShowMoreMenu(); break;
        case CabinetCommand::Details: SetPane(!m_DetailsPane, FALSE); break;
        case CabinetCommand::Preview: SetPane(FALSE, !m_PreviewPane); break;
        case CabinetCommand::Properties:
        {
            IShellView *view = CurrentView();
            IFolderView *folderView = NULL;
            int selected = 0;
            if (view && SUCCEEDED(view->QueryInterface(IID_PPV_ARG(IFolderView, &folderView))))
            {
                folderView->ItemCount(SVGIO_SELECTION, &selected);
                folderView->Release();
            }
            if (view)
                view->Release();
            if (selected)
                InvokeSelectionVerb("properties");
            else
                InvokeBackgroundVerb("properties");
            break;
        }
        case CabinetCommand::ViewDetails:
        case CabinetCommand::ViewLarge:
        {
            IShellView *view = CurrentView();
            IFolderView2 *folderView;
            if (view && SUCCEEDED(view->QueryInterface(IID_PPV_ARG(IFolderView2, &folderView))))
            {
                if (command == CabinetCommand::ViewDetails)
                    folderView->SetViewModeAndIconSize(FVM_DETAILS, 16);
                else
                    folderView->SetViewModeAndIconSize(FVM_ICON, 96);
                folderView->Release();
            }
            if (view)
                view->Release();
            UpdateViewToggles();
            break;
        }
        case CabinetCommand::NewTab:
        {
            PIDLIST_ABSOLUTE home = NULL;
            if (SUCCEEDED(Cabinet_GetDefaultFolder(&home)))
            {
                AddTab(home, TRUE);
                ILFree(home);
            }
            break;
        }
        default:
            break;
    }
}

BOOL
CCabinetWindow::TranslateAcceleratorMessage(MSG *msg)
{
    BOOL control = GetKeyState(VK_CONTROL) < 0, alt = GetKeyState(VK_MENU) < 0, shift = GetKeyState(VK_SHIFT) < 0;
    IShellView *view;

    if (!m_hwnd || (msg->hwnd != m_hwnd && !IsChild(m_hwnd, msg->hwnd)))
        return FALSE;
    if (msg->message == WM_KEYDOWN && msg->wParam == VK_F6)
    {
        CycleFocus();
        return TRUE;
    }
    if (msg->message == WM_KEYDOWN && msg->hwnd == m_SearchEdit)
    {
        if (msg->wParam == VK_ESCAPE)
        {
            SetWindowTextW(m_SearchEdit, L"");
            return TRUE;
        }
        if (msg->wParam == VK_RETURN)
        {
            StartSearch();
            return TRUE;
        }
    }
    if (msg->message == WM_KEYDOWN && (msg->hwnd == m_AddressEdit || msg->hwnd == m_SearchEdit) &&
        !(control && (msg->wParam == 'T' || msg->wParam == 'W' || msg->wParam == 'N' || msg->wParam == VK_TAB)))
        return FALSE;
    if (msg->message == WM_SYSKEYDOWN && alt)
    {
        switch (msg->wParam)
        {
            case VK_LEFT: NavigateHistory(-1); return TRUE;
            case VK_RIGHT: NavigateHistory(1); return TRUE;
            case VK_UP: NavigateUp(); return TRUE;
            case 'D':
                EditAddress();
                return TRUE;
            case 'P':
                if (shift)
                    SetPane(!m_DetailsPane, FALSE);
                else
                    SetPane(FALSE, !m_PreviewPane);
                return TRUE;
        }
    }
    if (msg->message == WM_KEYDOWN)
    {
        if (msg->wParam == VK_F5)
        {
            Refresh();
            return TRUE;
        }
        if (msg->wParam == VK_F4 || (control && msg->wParam == 'L'))
        {
            EditAddress();
            return TRUE;
        }
        if (control && (msg->wParam == 'F' || msg->wParam == 'E'))
        {
            SetFocus(m_SearchEdit);
            return TRUE;
        }
        if (control && msg->wParam == 'T')
        {
            ExecuteCommand(CabinetCommand::NewTab);
            return TRUE;
        }
        if (control && msg->wParam == 'W')
        {
            CloseTab(m_ActiveTab);
            return TRUE;
        }
        if (control && msg->wParam == VK_TAB && m_TabCount > 1)
        {
            ActivateTab((m_ActiveTab + (shift ? m_TabCount - 1 : 1)) % m_TabCount);
            return TRUE;
        }
        if (control && msg->wParam == 'N' && !shift)
        {
            CabinetTab *tab = ActiveTab();
            if (tab && tab->Pidl)
                Cabinet_OpenWindow(tab->Pidl, NULL, SW_SHOWNORMAL);
            return TRUE;
        }
        if (control && shift && msg->wParam == 'N')
        {
            InvokeBackgroundVerb(CMDSTR_NEWFOLDERA);
            return TRUE;
        }
        if (msg->wParam == VK_BACK)
        {
            NavigateHistory(-1);
            return TRUE;
        }
    }
    view = CurrentView();
    if (view)
    {
        HRESULT hr = view->TranslateAccelerator(msg);
        view->Release();
        if (hr == S_OK)
            return TRUE;
    }
    return FALSE;
}

LRESULT
CCabinetWindow::OnNotify(NMHDR *header)
{
    switch (header->code)
    {
        case NM_CUSTOMDRAW:
            if (header->hwndFrom == m_NavBar || header->hwndFrom == m_CommandBar ||
                header->hwndFrom == m_DetailsBar || header->hwndFrom == m_ViewBar)
            {
                CabinetPalette palette;
                CabinetGetPalette(&palette);
                return CabinetToolbarCustomDraw((NMTBCUSTOMDRAW *)header,
                                                header->hwndFrom == m_NavBar ? palette.Band : palette.Command);
            }
            break;
        case TBN_DROPDOWN:
        {
            NMTOOLBARW *toolbar = (NMTOOLBARW *)header;
            ExecuteCommand((CabinetCommand)(toolbar->iItem - IDC_COMMAND(CabinetCommand::None)));
            return TBDDRET_DEFAULT;
        }
    }
    return 0;
}

LRESULT CALLBACK
CCabinetWindow::WindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    CCabinetWindow *self;

    if (msg == WM_NCCREATE)
    {
        self = (CCabinetWindow *)((CREATESTRUCTW *)lParam)->lpCreateParams;
        self->m_hwnd = hwnd;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)self);
    }
    else
    {
        self = (CCabinetWindow *)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    }
    if (!self)
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    if (msg == WM_NCDESTROY)
    {
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
        self->m_hwnd = NULL;
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
    return self->HandleMessage(msg, wParam, lParam);
}

LRESULT
CCabinetWindow::HandleMessage(UINT msg, WPARAM wParam, LPARAM lParam)
{
    LRESULT result = 0;

    if (DwmDefWindowProc(m_hwnd, msg, wParam, lParam, &result))
        return result;
    switch (msg)
    {
        case WM_CREATE:
            OnCreate();
            SetWindowPos(m_hwnd, NULL, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
            return 0;
        case WM_NCCALCSIZE:
            if (wParam)
            {
                NCCALCSIZE_PARAMS *params = (NCCALCSIZE_PARAMS *)lParam;
                LONG top = params->rgrc[0].top;
                UINT dpi = GetDpiForWindow(m_hwnd);
                DefWindowProcW(m_hwnd, msg, wParam, lParam);
                params->rgrc[0].top = top;
                if (IsZoomed(m_hwnd))
                    params->rgrc[0].top += GetSystemMetricsForDpi(SM_CYSIZEFRAME, dpi) +
                                           GetSystemMetricsForDpi(SM_CXPADDEDBORDER, dpi);
                return 0;
            }
            break;
        case WM_NCHITTEST:
        {
            LRESULT hit = DefWindowProcW(m_hwnd, msg, wParam, lParam);
            POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            RECT client;
            UINT dpi = GetDpiForWindow(m_hwnd);
            int border = GetSystemMetricsForDpi(SM_CYSIZEFRAME, dpi) + GetSystemMetricsForDpi(SM_CXPADDEDBORDER, dpi);
            if (hit != HTCLIENT)
                return hit;
            ScreenToClient(m_hwnd, &pt);
            GetClientRect(m_hwnd, &client);
            if (!IsZoomed(m_hwnd) && pt.y < border)
            {
                if (pt.x < border * 2)
                    return HTTOPLEFT;
                if (pt.x >= client.right - border * 2)
                    return HTTOPRIGHT;
                return HTTOP;
            }
            if (pt.y < TabStripHeight())
                return HTCAPTION;
            return HTCLIENT;
        }
        case WM_DWMCOMPOSITIONCHANGED:
            ExtendFrame();
            break;
        case WM_SIZE:
            OnSize();
            return 0;
        case WM_ERASEBKGND:
            PaintBackground((HDC)wParam);
            return 1;
        case WM_NOTIFY:
            return OnNotify((NMHDR *)lParam);
        case WM_COMMAND:
            if (LOWORD(wParam) >= IDC_COMMAND(CabinetCommand::None) &&
                LOWORD(wParam) < IDC_COMMAND(CabinetCommand::Count) && HIWORD(wParam) == BN_CLICKED)
            {
                ExecuteCommand((CabinetCommand)(LOWORD(wParam) - IDC_COMMAND(CabinetCommand::None)));
                return 0;
            }
            break;
        case WM_CTLCOLORSTATIC:
            if ((HWND)lParam == m_PreviewText || (HWND)lParam == m_PreviewHost || (HWND)lParam == m_PreviewImage ||
                (HWND)lParam == m_PreviewLabel || (HWND)lParam == m_DetailsIcon || (HWND)lParam == m_DetailsName)
            {
                SetTextColor((HDC)wParam, GetSysColor(COLOR_WINDOWTEXT));
                SetBkMode((HDC)wParam, TRANSPARENT);
                return (LRESULT)GetSysColorBrush(COLOR_WINDOW);
            }
            break;
        case WM_CABINET_STATUS:
            UpdateStatus();
            UpdateCommands();
            UpdateDetails();
            UpdatePreview();
            return 0;
        case WM_CABINET_CHANGED:
            SetTimer(m_hwnd, IDT_CABINET_STATUS, 200, NULL);
            return 0;
        case WM_CABINET_FOCUSVIEW:
            FocusView();
            return 0;
        case WM_TIMER:
            if (wParam == IDT_CABINET_STATUS)
            {
                KillTimer(m_hwnd, IDT_CABINET_STATUS);
                UpdateStatus();
                return 0;
            }
            if (wParam == IDT_CABINET_SEARCH)
            {
                UpdateStatus();
                return 0;
            }
            break;
        case WM_XBUTTONUP:
            NavigateHistory(GET_XBUTTON_WPARAM(wParam) == XBUTTON1 ? -1 : 1);
            return TRUE;
        case WM_APPCOMMAND:
            switch (GET_APPCOMMAND_LPARAM(lParam))
            {
                case APPCOMMAND_BROWSER_BACKWARD:
                    NavigateHistory(-1);
                    return TRUE;
                case APPCOMMAND_BROWSER_FORWARD:
                    NavigateHistory(1);
                    return TRUE;
                case APPCOMMAND_BROWSER_REFRESH:
                    Refresh();
                    return TRUE;
                case APPCOMMAND_BROWSER_SEARCH:
                    SetFocus(m_SearchEdit);
                    return TRUE;
            }
            break;
        case WM_ACTIVATE:
            if (LOWORD(wParam) == WA_INACTIVE)
            {
                HWND focus = GetFocus();
                m_FocusSave = focus && IsChild(m_hwnd, focus) ? focus : NULL;
            }
            if (m_TabBand && m_TabBand->Window())
                InvalidateRect(m_TabBand->Window(), NULL, FALSE);
            break;
        case WM_SETFOCUS:
            RestoreFocus();
            return 0;
        case WM_THEMECHANGED:
        case WM_SYSCOLORCHANGE:
            InvalidateRect(m_hwnd, NULL, TRUE);
            break;
        case WM_CLOSE:
            SavePlacement();
            while (m_TabCount > 1)
                CloseTab(m_TabCount - 1);
            CloseTab(0);
            return 0;
        case WM_DESTROY:
            ClearPreview();
            if (m_ChangeNotify)
                SHChangeNotifyDeregister(m_ChangeNotify);
            m_ChangeNotify = 0;
            if (m_Navigation)
            {
                m_Navigation->Release();
                m_Navigation = NULL;
            }
            PostQuitMessage(0);
            break;
    }
    return DefWindowProcW(m_hwnd, msg, wParam, lParam);
}
