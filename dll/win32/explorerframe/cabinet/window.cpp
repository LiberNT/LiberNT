/*
 * PROJECT:     LiberNT Explorer
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     File Explorer window: tabs, address bar, command bar, panes and status
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "cabinet.h"
#include <commoncontrols.h>
#include <propvarutil.h>
#include <exdisp.h>
#include <shdispid.h>
#include <shldisp.h>

using namespace ShellFrame;

#define IDC_ADDRESS_EDIT 1001
#define IDC_SEARCH_EDIT 1002
#define WM_CABINET_STATUS (WM_APP + 1)
#define WM_CABINET_CHANGED (WM_APP + 2)
#define IDT_CABINET_STATUS 1
#define IDT_CABINET_SEARCH 2

static HINSTANCE
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

static RECT
MakeRect(int left, int top, int right, int bottom)
{
    RECT rc = { left, top, right, bottom };
    return rc;
}

static RECT
CenterRect(const RECT &outer, int width, int height)
{
    RECT rc;
    rc.left = outer.left + (outer.right - outer.left - width) / 2;
    rc.top = outer.top + (outer.bottom - outer.top - height) / 2;
    rc.right = rc.left + width;
    rc.bottom = rc.top + height;
    return rc;
}

CCabinetWindow::CCabinetWindow() :
    m_TabCount(0),
    m_ActiveTab(-1),
    m_Navigation(NULL),
    m_NavigationWindow(NULL),
    m_AddressEdit(NULL),
    m_SearchEdit(NULL),
    m_FocusSave(NULL),
    m_Tooltip(NULL),
    m_EditFont(NULL),
    m_CrumbCount(0),
    m_CrumbFirst(0),
    m_LocationIcon(NULL),
    m_HotTab(-1),
    m_HotTabClose(FALSE),
    m_HotCommand(CabinetCommand::None),
    m_PressedCommand(CabinetCommand::None),
    m_HotCrumb(-1),
    m_HotCrumbChevron(FALSE),
    m_TrackingClient(FALSE),
    m_DetailsPane(FALSE),
    m_SmallIcon(NULL),
    m_BigIcon(NULL),
    m_InHistoryNavigation(FALSE),
    m_ChangeNotify(0),
    m_NavigationVisible(TRUE),
    m_SearchBrowser(NULL),
    m_SearchPathCount(0),
    m_SearchActive(FALSE)
{
    m_SearchQuery[0] = UNICODE_NULL;
    ZeroMemory(&m_Layout, sizeof(m_Layout));
    ZeroMemory(m_Tabs, sizeof(m_Tabs));
    ZeroMemory(m_Crumbs, sizeof(m_Crumbs));
    ZeroMemory(&m_Details, sizeof(m_Details));
    SetRectEmpty(&m_CrumbOverflow);
    m_StatusText[0] = UNICODE_NULL;
}

CCabinetWindow::~CCabinetWindow()
{
    FreeCrumbs();
    FreeDetails();
}

void
CCabinetWindow::FreeDetails()
{
    if (m_Details.Icon)
        DestroyIcon(m_Details.Icon);
    ZeroMemory(&m_Details, sizeof(m_Details));
}

static HICON
JumboIcon(PCIDLIST_ABSOLUTE pidl)
{
    static const int Lists[] = { SHIL_JUMBO, SHIL_EXTRALARGE, SHIL_LARGE };
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
    if (!icon && SHGetFileInfoW((LPCWSTR)pidl, 0, &info, sizeof(info), SHGFI_PIDL | SHGFI_ICON | SHGFI_LARGEICON))
        icon = info.hIcon;
    return icon;
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

    FreeDetails();
    if (!m_DetailsPane || !tab || !tab->Pidl)
        return;
    view = CurrentView();
    if (view && SUCCEEDED(view->QueryInterface(IID_PPV_ARG(IFolderView, &folderView))))
        folderView->ItemCount(SVGIO_SELECTION, &selected);
    if (selected > 1)
    {
        WCHAR format[64];
        LoadText(IDS_CAB_SELECTED, format, _countof(format));
        StringCchPrintfW(m_Details.Name, _countof(m_Details.Name), format, selected);
        m_Details.Icon = JumboIcon(tab->Pidl);
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
            PWSTR name = NULL;
            m_Details.Icon = JumboIcon(pidl);
            if (SUCCEEDED(item->GetDisplayName(SIGDN_NORMALDISPLAY, &name)))
            {
                StringCchCopyW(m_Details.Name, _countof(m_Details.Name), name);
                CoTaskMemFree(name);
            }
            if (SUCCEEDED(item->GetString(PKEY_ItemTypeText, &name)))
            {
                StringCchCopyW(m_Details.Type, _countof(m_Details.Type), name);
                CoTaskMemFree(name);
            }
            ILFree(pidl);
        }
        if (item && SUCCEEDED(item->GetPropertyDescriptionList(PKEY_PropList_PreviewDetails,
                                                                IID_PPV_ARG(IPropertyDescriptionList, &list))))
        {
            list->GetCount(&count);
            for (UINT i = 0; i < count && m_Details.Count < CABINET_MAX_DETAILS; i++)
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
                    StringCchCopyW(m_Details.Labels[m_Details.Count], _countof(m_Details.Labels[0]), label);
                    StringCchCopyW(m_Details.Values[m_Details.Count], _countof(m_Details.Values[0]), text);
                    m_Details.Count++;
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
    InvalidateRect(m_hwnd, &m_Layout.Details, FALSE);
}

void
CCabinetWindow::PaintDetails(Canvas &canvas)
{
    const Palette &c = m_Theme.Colors();
    RECT pane = m_Layout.Details, rc;
    int x = pane.left + m_Theme.Scale(20), right = pane.right - m_Theme.Scale(20), y, size = m_Theme.Scale(96);

    if (!m_DetailsPane)
        return;
    canvas.Fill(pane, c.Content);
    canvas.Line(pane.left, pane.top, pane.left, pane.bottom, c.Divider);
    y = pane.top + m_Theme.Scale(24);
    if (m_Details.Icon)
        DrawIconEx(canvas.Dc(), pane.left + (pane.right - pane.left - size) / 2, y, m_Details.Icon, size, size, 0, NULL,
                   DI_NORMAL);
    y += size + m_Theme.Scale(16);
    rc = MakeRect(x, y, right, y + m_Theme.Scale(56));
    canvas.Text(m_Details.Name, rc, m_Theme.Font(TextStyle::Subtitle), c.Text, DT_WORDBREAK | DT_END_ELLIPSIS);
    y += m_Theme.Scale(60);
    if (m_Details.Type[0])
    {
        rc = MakeRect(x, y, right, y + m_Theme.Scale(18));
        canvas.Text(m_Details.Type, rc, m_Theme.Font(TextStyle::Caption), c.TextSecondary,
                    DT_SINGLELINE | DT_END_ELLIPSIS);
        y += m_Theme.Scale(30);
    }
    for (int i = 0; i < m_Details.Count && y < pane.bottom - m_Theme.Scale(40); i++)
    {
        rc = MakeRect(x, y, right, y + m_Theme.Scale(16));
        canvas.Text(m_Details.Labels[i], rc, m_Theme.Font(TextStyle::Caption), c.TextSecondary,
                    DT_SINGLELINE | DT_END_ELLIPSIS);
        y += m_Theme.Scale(17);
        rc = MakeRect(x, y, right, y + m_Theme.Scale(20));
        canvas.Text(m_Details.Values[i], rc, m_Theme.Font(TextStyle::Body), c.Text, DT_SINGLELINE | DT_END_ELLIPSIS);
        y += m_Theme.Scale(30);
    }
}

void
CCabinetWindow::ShowCrumbOverflowMenu()
{
    HMENU popup = CreatePopupMenu();
    POINT pt = { m_CrumbOverflow.left, m_Layout.Address.bottom };
    UINT command;

    for (int i = m_CrumbFirst - 1; i >= 0; i--)
        AppendMenuW(popup, MF_STRING, i + 1, m_Crumbs[i].Name);
    ClientToScreen(m_hwnd, &pt);
    command = TrackPopupMenuEx(popup, TPM_RETURNCMD | TPM_LEFTALIGN | TPM_TOPALIGN, pt.x, pt.y, m_hwnd, NULL);
    DestroyMenu(popup);
    if (command >= 1 && (int)command <= m_CrumbFirst)
        Navigate(m_Crumbs[command - 1].Pidl);
}

void
CCabinetWindow::FocusView()
{
    IShellView *view = CurrentView();

    if (view)
    {
        view->UIActivate(SVUIA_ACTIVATE_FOCUS);
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
CCabinetWindow::UpdateTooltips()
{
    static const struct { CabinetCommand Command; UINT Label; } Tools[] =
    {
        { CabinetCommand::Back, IDS_CAB_BACK },
        { CabinetCommand::Forward, IDS_CAB_FORWARD },
        { CabinetCommand::Up, IDS_CAB_UP },
        { CabinetCommand::Refresh, IDS_CAB_REFRESH },
        { CabinetCommand::Cut, IDS_CAB_CUT },
        { CabinetCommand::Copy, IDS_CAB_COPY },
        { CabinetCommand::Paste, IDS_CAB_PASTE },
        { CabinetCommand::Rename, IDS_CAB_RENAME },
        { CabinetCommand::Share, IDS_CAB_SHARE },
        { CabinetCommand::Delete, IDS_CAB_DELETE },
        { CabinetCommand::More, IDS_CAB_MORE },
        { CabinetCommand::NewTab, IDS_CAB_NEW_TAB },
        { CabinetCommand::ViewDetails, IDS_CAB_VIEW_DETAILS },
        { CabinetCommand::ViewTiles, IDS_CAB_VIEW_LARGE },
    };

    if (!m_Tooltip)
        return;
    for (UINT i = 0; i < _countof(Tools); i++)
    {
        WCHAR text[64];
        TTTOOLINFOW info = { sizeof(info) };
        info.uFlags = TTF_SUBCLASS;
        info.hwnd = m_hwnd;
        info.uId = (UINT_PTR)Tools[i].Command;
        info.rect = m_Layout.Buttons[(int)Tools[i].Command];
        info.hinst = NULL;
        LoadText(Tools[i].Label, text, _countof(text));
        info.lpszText = text;
        SendMessageW(m_Tooltip, TTM_DELTOOLW, 0, (LPARAM)&info);
        if (!IsRectEmpty(&info.rect))
            SendMessageW(m_Tooltip, TTM_ADDTOOLW, 0, (LPARAM)&info);
    }
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
    if (m_NavigationVisible && m_NavigationWindow && (focus == m_SearchEdit || !focus))
        SetFocus(FindWindowExW(m_NavigationWindow, NULL, WC_TREEVIEWW, NULL) ?
                 FindWindowExW(m_NavigationWindow, NULL, WC_TREEVIEWW, NULL) : m_NavigationWindow);
    else if (view && (!m_NavigationWindow || focus == m_NavigationWindow || IsChild(m_NavigationWindow, focus) ||
                      focus == m_SearchEdit))
        SetFocus(view);
    else
        SetFocus(m_SearchEdit);
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
        RegSetValueExW(key, L"Placement", 0, REG_BINARY, (const BYTE *)&placement, sizeof(placement));
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

void
CCabinetWindow::LoadText(UINT id, LPWSTR buffer, int length)
{
    if (!LoadStringW(g_hInstance, id, buffer, length))
        buffer[0] = UNICODE_NULL;
}

int
CCabinetWindow::CaptionHeight() const
{
    return m_Theme.Scale(40);
}

int
CCabinetWindow::CaptionButtonHeight() const
{
    return m_Theme.Scale(30);
}

HRESULT
CCabinetWindow::Open(PCIDLIST_ABSOLUTE pidl, PCUITEMID_CHILD select, int showCmd)
{
    WINDOWPLACEMENT placement = { sizeof(placement) };
    DWORD size = sizeof(placement);

    if (!Create(CABINET_CLASS, L"File Explorer", NULL, 1100, 700, SW_HIDE))
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

void
CCabinetWindow::OnCreate()
{
    m_SmallIcon = CreateGlyphIcon(L"folder_20_regular", GetSystemMetricsForDpi(SM_CXSMICON, m_Theme.Dpi()),
                                  RGB(0xE8, 0xA3, 0x17));
    m_BigIcon = CreateGlyphIcon(L"folder_20_regular", GetSystemMetricsForDpi(SM_CXICON, m_Theme.Dpi()),
                                RGB(0xE8, 0xA3, 0x17));
    SendMessageW(m_hwnd, WM_SETICON, ICON_SMALL, (LPARAM)m_SmallIcon);
    SendMessageW(m_hwnd, WM_SETICON, ICON_BIG, (LPARAM)m_BigIcon);
    m_AddressEdit = CreateWindowExW(0, WC_EDITW, L"", WS_CHILD | ES_AUTOHSCROLL, 0, 0, 0, 0, m_hwnd,
                                    (HMENU)IDC_ADDRESS_EDIT, g_hInstance, NULL);
    m_SearchEdit = CreateWindowExW(0, WC_EDITW, L"", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, 0, 0, 0, 0, m_hwnd,
                                   (HMENU)IDC_SEARCH_EDIT, g_hInstance, NULL);
    m_Tooltip = CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASSW, NULL, WS_POPUP | TTS_NOPREFIX | TTS_ALWAYSTIP, 0, 0, 0,
                                0, m_hwnd, NULL, g_hInstance, NULL);
    SHAutoComplete(m_AddressEdit, SHACF_FILESYS_DIRS | SHACF_URLALL);
    OnThemeChanged();
    CreateNavigationPane();
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
CCabinetWindow::Relayout()
{
    RECT client;

    GetClientRect(m_hwnd, &client);
    OnLayout(client);
    Invalidate(NULL);
}

void
CCabinetWindow::ApplyChildColors()
{
    const Palette &c = m_Theme.Colors();
    HWND tree = m_NavigationWindow ? FindWindowExW(m_NavigationWindow, NULL, WC_TREEVIEWW, NULL) : NULL;

    if (!tree && m_NavigationWindow)
    {
        WCHAR name[32];
        if (GetClassNameW(m_NavigationWindow, name, _countof(name)) && !lstrcmpiW(name, WC_TREEVIEWW))
            tree = m_NavigationWindow;
    }
    if (tree)
    {
        TreeView_SetItemHeight(tree, m_Theme.Scale(28));
        TreeView_SetBkColor(tree, c.Content);
        TreeView_SetTextColor(tree, c.Text);
        InvalidateRect(tree, NULL, TRUE);
    }
    if (m_AddressEdit)
        InvalidateRect(m_AddressEdit, NULL, TRUE);
    if (m_SearchEdit)
        InvalidateRect(m_SearchEdit, NULL, TRUE);
}

void
CCabinetWindow::OnThemeChanged()
{
    LOGFONTW lf;

    if (m_EditFont)
        DeleteObject(m_EditFont);
    GetObjectW(m_Theme.Font(TextStyle::Body), sizeof(lf), &lf);
    m_EditFont = CreateFontIndirectW(&lf);
    if (m_AddressEdit)
        SendMessageW(m_AddressEdit, WM_SETFONT, (WPARAM)m_EditFont, TRUE);
    if (m_SearchEdit)
        SendMessageW(m_SearchEdit, WM_SETFONT, (WPARAM)m_EditFont, TRUE);
    if (m_NavigationWindow)
        SetWindowTheme(m_NavigationWindow, m_Theme.IsDark() ? L"DarkMode_Explorer" : L"Explorer", NULL);
    m_Theme.UpdateMaterial(m_hwnd);
    ApplyChildColors();
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
    RECT rc = MakeRect(0, 0, 0, 0);

    if (FAILED(CoCreateInstance(CLSID_NamespaceTreeControl, NULL, CLSCTX_INPROC_SERVER,
                                IID_PPV_ARG(INameSpaceTreeControl, &m_Navigation))))
        return;
    if (FAILED(m_Navigation->Initialize(m_hwnd, &rc, NSTCS_HASEXPANDOS | NSTCS_SINGLECLICKEXPAND |
                                        NSTCS_FULLROWSELECT | NSTCS_SHOWSELECTIONALWAYS | NSTCS_NOINFOTIP |
                                        NSTCS_AUTOHSCROLL)))
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
    OnThemeChanged();
}

int
CCabinetWindow::AddTab(PCIDLIST_ABSOLUTE pidl, BOOL activate)
{
    CabinetTab *tab;
    FOLDERSETTINGS settings = { FVM_DETAILS, FWF_NOWEBVIEW };
    IObjectWithSite *withSite = NULL;
    RECT rc = MakeRect(0, 0, 0, 0);
    int index;

    if (m_TabCount >= CABINET_MAX_TABS)
        return -1;
    index = m_TabCount;
    tab = &m_Tabs[index];
    ZeroMemory(tab, sizeof(*tab));
    if (FAILED(CoCreateInstance(CLSID_ExplorerBrowser, NULL, CLSCTX_INPROC_SERVER,
                                IID_PPV_ARG(IExplorerBrowser, &tab->Browser))))
        return -1;
    if (FAILED(CabinetSite_Create(this, tab->Browser, &tab->Site)) ||
        FAILED(tab->Browser->Initialize(m_hwnd, &rc, &settings)))
    {
        if (tab->Site)
            tab->Site->Release();
        tab->Browser->Release();
        ZeroMemory(tab, sizeof(*tab));
        return -1;
    }
    tab->Browser->SetOptions((EXPLORER_BROWSER_OPTIONS)(EBO_NOBORDER | EBO_NOTRAVELLOG));
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
    Relayout();
    if (FAILED(tab->Browser->BrowseToIDList(pidl, SBSP_ABSOLUTE)))
    {
        CloseTab(index);
        return -1;
    }
    Invalidate(NULL);
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
    if (tab->Icon)
        DestroyIcon(tab->Icon);
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
    Relayout();
    ActivateTab(m_ActiveTab);
}

void
CCabinetWindow::ActivateTab(int index)
{
    if (index < 0 || index >= m_TabCount)
        return;
    m_ActiveTab = index;
    PlaceBrowsers();
    UpdateTitle(ActiveTab());
    BuildCrumbs();
    UpdateStatus();
    SyncNavigationPane();
    WatchFolder();
    Invalidate(NULL);
}

void
CCabinetWindow::PlaceBrowsers()
{
    RECT empty = MakeRect(0, 0, 0, 0);

    for (int i = 0; i < m_TabCount; i++)
    {
        if (m_Tabs[i].Browser)
            m_Tabs[i].Browser->SetRect(NULL, i == m_ActiveTab ? m_Layout.View : empty);
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
    Invalidate(NULL);
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
        if (m_AddressEdit && IsWindowVisible(m_AddressEdit))
            EndAddressEdit(FALSE);
        if (!m_SearchActive)
            SetWindowTextW(m_SearchEdit, L"");
        BuildCrumbs();
        UpdateStatus();
        SyncNavigationPane();
        WatchFolder();
        PostMessageW(m_hwnd, WM_CABINET_STATUS, 0, 0);
        if (GetActiveWindow() == m_hwnd)
        {
            HWND focus = GetFocus();
            if (focus != m_SearchEdit && focus != m_AddressEdit &&
                !(m_NavigationWindow && (focus == m_NavigationWindow || IsChild(m_NavigationWindow, focus))))
                FocusView();
        }
    }
    Invalidate(NULL);
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
    {
        UpdateStatus();
        UpdateDetails();
        InvalidateRect(m_hwnd, &m_Layout.CommandBand, FALSE);
    }
}

void
CCabinetWindow::OnViewCreated(IExplorerBrowser *browser, IShellView *view)
{
    HWND viewWindow = NULL;
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
    if (view && SUCCEEDED(view->GetWindow(&viewWindow)) && viewWindow)
    {
        HWND list = FindWindowExW(viewWindow, NULL, WC_LISTVIEWW, NULL);
        if (list)
            SetWindowTheme(list, m_Theme.IsDark() ? L"DarkMode_ItemsView" : L"ItemsView", NULL);
    }
}

void
CCabinetWindow::OnSelectionChanged()
{
    UpdateStatus();
    UpdateDetails();
    InvalidateRect(m_hwnd, &m_Layout.CommandBand, FALSE);
    InvalidateRect(m_hwnd, &m_Layout.Status, FALSE);
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
    {
        AddTab(pidl, FALSE);
    }
    else
    {
        Navigate(pidl);
    }
    ILFree(pidl);
}

void
CCabinetWindow::UpdateTitle(CabinetTab *tab)
{
    PWSTR name = NULL;
    SHFILEINFOW info;
    WCHAR title[MAX_PATH + 32];

    if (!tab || !tab->Pidl)
        return;
    if (SUCCEEDED(SHGetNameFromIDList(tab->Pidl, SIGDN_NORMALDISPLAY, &name)))
    {
        StringCchCopyW(tab->Title, _countof(tab->Title), name);
        CoTaskMemFree(name);
    }
    if (tab->Icon)
        DestroyIcon(tab->Icon);
    tab->Icon = NULL;
    if (SHGetFileInfoW((LPCWSTR)tab->Pidl, 0, &info, sizeof(info), SHGFI_PIDL | SHGFI_ICON | SHGFI_SMALLICON))
        tab->Icon = info.hIcon;
    if (tab == ActiveTab())
    {
        StringCchPrintfW(title, _countof(title), L"%s - File Explorer", tab->Title);
        SetWindowTextW(m_hwnd, title);
        if (m_SearchEdit)
        {
            WCHAR format[64], cue[MAX_PATH + 64];
            LoadText(IDS_CAB_SEARCH_IN, format, _countof(format));
            StringCchPrintfW(cue, _countof(cue), format, tab->Title);
            SendMessageW(m_SearchEdit, EM_SETCUEBANNER, FALSE, (LPARAM)cue);
        }
    }
}

void
CCabinetWindow::FreeCrumbs()
{
    for (int i = 0; i < m_CrumbCount; i++)
        FreeIdList(&m_Crumbs[i].Pidl);
    m_CrumbCount = 0;
    if (m_LocationIcon)
        DestroyIcon(m_LocationIcon);
    m_LocationIcon = NULL;
}

void
CCabinetWindow::BuildCrumbs()
{
    CabinetTab *tab = ActiveTab();
    PIDLIST_ABSOLUTE chain[CABINET_MAX_CRUMBS];
    int count = 0;
    PIDLIST_ABSOLUTE walk;
    SHFILEINFOW info;

    FreeCrumbs();
    if (!tab || !tab->Pidl)
        return;
    walk = ILClone(tab->Pidl);
    while (walk && !ILIsEmpty(walk) && count < CABINET_MAX_CRUMBS)
    {
        chain[count++] = ILClone(walk);
        if (!ILRemoveLastID(walk))
            break;
    }
    if (walk)
        ILFree(walk);
    for (int i = count - 1; i >= 0; i--)
    {
        CabinetCrumb *crumb = &m_Crumbs[m_CrumbCount];
        PWSTR name = NULL;

        crumb->Pidl = chain[i];
        if (SUCCEEDED(SHGetNameFromIDList(crumb->Pidl, SIGDN_NORMALDISPLAY, &name)))
        {
            StringCchCopyW(crumb->Name, _countof(crumb->Name), name);
            CoTaskMemFree(name);
        }
        m_CrumbCount++;
    }
    if (!m_CrumbCount)
    {
        PWSTR name = NULL;
        m_Crumbs[0].Pidl = ILClone(tab->Pidl);
        if (SUCCEEDED(SHGetNameFromIDList(tab->Pidl, SIGDN_NORMALDISPLAY, &name)))
        {
            StringCchCopyW(m_Crumbs[0].Name, _countof(m_Crumbs[0].Name), name);
            CoTaskMemFree(name);
        }
        m_CrumbCount = 1;
    }
    if (SHGetFileInfoW((LPCWSTR)m_Crumbs[0].Pidl, 0, &info, sizeof(info), SHGFI_PIDL | SHGFI_ICON | SHGFI_SMALLICON))
        m_LocationIcon = info.hIcon;
}

void
CCabinetWindow::BeginAddressEdit()
{
    CabinetTab *tab = ActiveTab();
    PWSTR path = NULL;
    RECT rc = m_Layout.Address;
    int height = m_Theme.Scale(20);

    if (!tab || !tab->Pidl)
        return;
    if (FAILED(SHGetNameFromIDList(tab->Pidl, SIGDN_DESKTOPABSOLUTEEDITING, &path)))
        return;
    SetWindowTextW(m_AddressEdit, path);
    CoTaskMemFree(path);
    MoveWindow(m_AddressEdit, rc.left + m_Theme.Scale(10), rc.top + (rc.bottom - rc.top - height) / 2,
               rc.right - rc.left - m_Theme.Scale(20), height, TRUE);
    ShowWindow(m_AddressEdit, SW_SHOW);
    SetFocus(m_AddressEdit);
    SendMessageW(m_AddressEdit, EM_SETSEL, 0, -1);
    InvalidateRect(m_hwnd, &m_Layout.Address, FALSE);
}

void
CCabinetWindow::EndAddressEdit(BOOL navigate)
{
    WCHAR text[MAX_PATH * 2];
    PIDLIST_ABSOLUTE pidl = NULL;

    if (!IsWindowVisible(m_AddressEdit))
        return;
    GetWindowTextW(m_AddressEdit, text, _countof(text));
    ShowWindow(m_AddressEdit, SW_HIDE);
    InvalidateRect(m_hwnd, &m_Layout.Address, FALSE);
    if (!navigate)
    {
        IShellView *view = CurrentView();
        HWND viewWindow = NULL;
        if (view && SUCCEEDED(view->GetWindow(&viewWindow)))
            SetFocus(viewWindow);
        if (view)
            view->Release();
        return;
    }
    StrTrimW(text, L" \t");
    if (!text[0])
        return;
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
            Navigate(pidl);
        else
            ShellExecuteW(m_hwnd, NULL, text, NULL, NULL, SW_SHOWNORMAL);
        ILFree(pidl);
    }
    else if ((INT_PTR)ShellExecuteW(m_hwnd, NULL, text, NULL, NULL, SW_SHOWNORMAL) <= 32)
    {
        MessageBeep(MB_ICONWARNING);
    }
}

void
CCabinetWindow::UpdateStatus()
{
    IShellView *view = CurrentView();
    IFolderView *folderView = NULL;
    int count = 0, selected = 0;
    WCHAR format[64];

    ULONGLONG selectedSize = 0;
    BOOL haveSize = FALSE;

    m_StatusText[0] = UNICODE_NULL;
    if (!view)
        return;
    if (SUCCEEDED(view->QueryInterface(IID_PPV_ARG(IFolderView, &folderView))))
    {
        IShellItemArray *items = NULL;
        folderView->ItemCount(SVGIO_ALLVIEW, &count);
        folderView->ItemCount(SVGIO_SELECTION, &selected);
        if (selected && selected <= 4096 &&
            SUCCEEDED(folderView->Items(SVGIO_SELECTION, IID_PPV_ARG(IShellItemArray, &items))))
        {
            DWORD itemCount = 0;
            items->GetCount(&itemCount);
            for (DWORD i = 0; i < itemCount; i++)
            {
                IShellItem *item = NULL;
                IShellItem2 *item2 = NULL;
                ULONGLONG size = 0;
                if (SUCCEEDED(items->GetItemAt(i, &item)) &&
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
            items->Release();
        }
        folderView->Release();
    }
    view->Release();
    if (count == 1)
        LoadText(IDS_CAB_ITEM, m_StatusText, _countof(m_StatusText));
    else
    {
        LoadText(IDS_CAB_ITEMS, format, _countof(format));
        StringCchPrintfW(m_StatusText, _countof(m_StatusText), format, count);
    }
    if (selected)
    {
        WCHAR part[64];
        if (selected == 1)
            LoadText(IDS_CAB_SELECTED_ONE, part, _countof(part));
        else
        {
            LoadText(IDS_CAB_SELECTED, format, _countof(format));
            StringCchPrintfW(part, _countof(part), format, selected);
        }
        StringCchCatW(m_StatusText, _countof(m_StatusText), L"    ");
        StringCchCatW(m_StatusText, _countof(m_StatusText), part);
        if (selectedSize > 0 || haveSize)
        {
            WCHAR text[32];
            if (StrFormatByteSizeW((LONGLONG)selectedSize, text, _countof(text)))
            {
                StringCchCatW(m_StatusText, _countof(m_StatusText), L"  ");
                StringCchCatW(m_StatusText, _countof(m_StatusText), text);
            }
        }
    }
    InvalidateRect(m_hwnd, &m_Layout.Status, FALSE);
}

void
CCabinetWindow::ComputeLayout(const RECT &client)
{
    Layout &l = m_Layout;
    int s40 = m_Theme.Scale(40), x;
    int tabRight = client.right - CaptionButtonsWidth() - m_Theme.Scale(48);
    int tabWidth = m_Theme.Scale(240);
    static const CabinetCommand NavButtons[] = { CabinetCommand::Back, CabinetCommand::Forward, CabinetCommand::Up,
                                                 CabinetCommand::Refresh };

    if (m_TabCount)
    {
        int available = tabRight - m_Theme.Scale(8);
        tabWidth = max(m_Theme.Scale(72), min(tabWidth, available / m_TabCount));
    }
    for (int i = 0; i < m_TabCount; i++)
        m_Tabs[i].Width = tabWidth;
    l.Tabs = MakeRect(m_Theme.Scale(8), m_Theme.Scale(8), m_Theme.Scale(8) + tabWidth * m_TabCount, CaptionHeight());
    l.NewTab = MakeRect(l.Tabs.right + m_Theme.Scale(4), m_Theme.Scale(8), l.Tabs.right + m_Theme.Scale(40),
                        CaptionHeight() - m_Theme.Scale(4));
    l.Buttons[(int)CabinetCommand::NewTab] = l.NewTab;

    l.AddressBand = MakeRect(0, CaptionHeight(), client.right, CaptionHeight() + m_Theme.Scale(48));
    x = m_Theme.Scale(6);
    for (UINT i = 0; i < _countof(NavButtons); i++)
    {
        l.Buttons[(int)NavButtons[i]] = MakeRect(x, l.AddressBand.top + m_Theme.Scale(8), x + s40,
                                                l.AddressBand.top + m_Theme.Scale(40));
        x += m_Theme.Scale(48);
    }
    l.Search = MakeRect(max(x + m_Theme.Scale(120), (int)client.right - m_Theme.Scale(10) - m_Theme.Scale(304)),
                        l.AddressBand.top + m_Theme.Scale(8), client.right - m_Theme.Scale(10),
                        l.AddressBand.top + m_Theme.Scale(40));
    l.Address = MakeRect(x + m_Theme.Scale(8), l.Search.top, l.Search.left - m_Theme.Scale(8), l.Search.bottom);

    l.CommandBand = MakeRect(0, l.AddressBand.bottom, client.right, l.AddressBand.bottom + m_Theme.Scale(55));
    l.Navigation = MakeRect(0, l.CommandBand.bottom, m_NavigationVisible ? m_Theme.Scale(200) : 0,
                            client.bottom - m_Theme.Scale(26));
    l.Details = MakeRect(client.right - (m_DetailsPane ? m_Theme.Scale(320) : 0), l.CommandBand.bottom, client.right,
                         l.Navigation.bottom);
    l.View = MakeRect(l.Navigation.right + (m_NavigationVisible ? 1 : 0), l.CommandBand.bottom, l.Details.left,
                      l.Navigation.bottom);
    l.Status = MakeRect(0, l.Navigation.bottom, client.right, client.bottom);
    l.Buttons[(int)CabinetCommand::ViewTiles] = MakeRect(client.right - m_Theme.Scale(30), l.Status.top + 1,
                                                        client.right - m_Theme.Scale(6), l.Status.bottom - 1);
    l.Buttons[(int)CabinetCommand::ViewDetails] = MakeRect(client.right - m_Theme.Scale(56), l.Status.top + 1,
                                                          client.right - m_Theme.Scale(32), l.Status.bottom - 1);
}

void
CCabinetWindow::OnLayout(const RECT &client)
{
    HDC dc = GetDC(m_hwnd);
    int x, height = m_Theme.Scale(20), widths[10];
    UINT visible;
    struct { CabinetCommand Command; UINT Label; BOOL Dropdown; } Buttons[] =
    {
        { CabinetCommand::NewItem, IDS_CAB_NEW, TRUE },
        { CabinetCommand::Cut, 0, FALSE },
        { CabinetCommand::Copy, 0, FALSE },
        { CabinetCommand::Paste, 0, FALSE },
        { CabinetCommand::Rename, 0, FALSE },
        { CabinetCommand::Share, 0, FALSE },
        { CabinetCommand::Delete, 0, FALSE },
        { CabinetCommand::Sort, IDS_CAB_SORT, TRUE },
        { CabinetCommand::View, IDS_CAB_VIEW, TRUE },
        { CabinetCommand::More, 0, FALSE },
    };

    ComputeLayout(client);
    {
        WCHAR text[64];
        SIZE size = { 0, 0 };
        HGDIOBJ old = SelectObject(dc, m_Theme.Font(TextStyle::Body));
        LoadText(IDS_CAB_DETAILS, text, _countof(text));
        GetTextExtentPoint32W(dc, text, lstrlenW(text), &size);
        SelectObject(dc, old);
        m_Layout.Buttons[(int)CabinetCommand::Details] =
            MakeRect(client.right - m_Theme.Scale(16 + 12 + 16 + 8 + 12) - size.cx,
                     m_Layout.CommandBand.top + m_Theme.Scale(7), client.right - m_Theme.Scale(16),
                     m_Layout.CommandBand.top + m_Theme.Scale(47));
    }
    for (UINT i = 0; i < _countof(Buttons); i++)
    {
        widths[i] = m_Theme.Scale(40);
        if (Buttons[i].Label)
        {
            WCHAR text[64];
            SIZE size = { 0, 0 };
            HGDIOBJ old = SelectObject(dc, m_Theme.Font(TextStyle::Body));
            LoadText(Buttons[i].Label, text, _countof(text));
            GetTextExtentPoint32W(dc, text, lstrlenW(text), &size);
            SelectObject(dc, old);
            widths[i] = m_Theme.Scale(12 + 16 + 8) + size.cx + m_Theme.Scale(Buttons[i].Dropdown ? 6 + 12 + 10 : 12);
        }
    }
    for (visible = _countof(Buttons);; visible--)
    {
        x = m_Theme.Scale(12);
        for (UINT i = 0; i < _countof(Buttons); i++)
        {
            RECT *rc = &m_Layout.Buttons[(int)Buttons[i].Command];
            if (Buttons[i].Command != CabinetCommand::More && i + 1 >= visible)
            {
                SetRectEmpty(rc);
                continue;
            }
            if (Buttons[i].Command == CabinetCommand::Cut || Buttons[i].Command == CabinetCommand::Sort ||
                Buttons[i].Command == CabinetCommand::More)
                x += m_Theme.Scale(17);
            *rc = MakeRect(x, m_Layout.CommandBand.top + m_Theme.Scale(7), x + widths[i],
                           m_Layout.CommandBand.top + m_Theme.Scale(47));
            x += widths[i] + m_Theme.Scale(8);
        }
        if (x <= m_Layout.Buttons[(int)CabinetCommand::Details].left || visible <= 1)
            break;
    }
    ReleaseDC(m_hwnd, dc);
    if (m_SearchEdit)
    {
        RECT rc = m_Layout.Search;
        MoveWindow(m_SearchEdit, rc.left + m_Theme.Scale(10), rc.top + (rc.bottom - rc.top - height) / 2,
                   rc.right - rc.left - m_Theme.Scale(40), height, TRUE);
    }
    if (m_AddressEdit && IsWindowVisible(m_AddressEdit))
    {
        RECT rc = m_Layout.Address;
        MoveWindow(m_AddressEdit, rc.left + m_Theme.Scale(10), rc.top + (rc.bottom - rc.top - height) / 2,
                   rc.right - rc.left - m_Theme.Scale(20), height, TRUE);
    }
    if (m_Navigation)
    {
        RECT rc = m_Layout.Navigation;
        rc.top += m_Theme.Scale(4);
        if (m_NavigationWindow)
        {
            ShowWindow(m_NavigationWindow, m_NavigationVisible ? SW_SHOW : SW_HIDE);
            MoveWindow(m_NavigationWindow, rc.left, rc.top, rc.right - rc.left, rc.bottom - rc.top, TRUE);
        }
    }
    PlaceBrowsers();
    UpdateTooltips();
}

BOOL
CCabinetWindow::IsCaptionClient(POINT pt)
{
    BOOL close;
    return HitTab(pt, &close) >= 0 || PtInRect(&m_Layout.NewTab, pt);
}

void
CCabinetWindow::PaintButton(Canvas &canvas, CabinetCommand command, LPCWSTR icon, LPCWSTR label, BOOL dropdown)
{
    const Palette &c = m_Theme.Colors();
    RECT rc = m_Layout.Buttons[(int)command], glyph;
    BOOL enabled = CommandEnabled(command);

    if (IsRectEmpty(&rc))
        return;
    COLORREF ink = enabled ? c.Text : c.TextDisabled;
    int size = m_Theme.Scale(command >= CabinetCommand::NewItem && command <= CabinetCommand::Details ? 20 : 16);
    int x;

    if (enabled && (command == m_HotCommand || command == m_PressedCommand))
        canvas.FillRound(rc, m_Theme.ScaleF(4.0f), command == m_PressedCommand ? c.SubtlePressed : c.SubtleHover);
    if (command == CabinetCommand::Details && m_DetailsPane)
        canvas.FillRound(rc, m_Theme.ScaleF(4.0f), c.Selected);
    if (!label)
    {
        glyph = CenterRect(rc, size, size);
        canvas.Icon(icon, glyph, ink);
        return;
    }
    x = rc.left + m_Theme.Scale(12);
    glyph = MakeRect(x, rc.top + (rc.bottom - rc.top - size) / 2, x + size, rc.top + (rc.bottom - rc.top - size) / 2 + size);
    canvas.Icon(icon, glyph, command == CabinetCommand::NewItem && enabled ? c.Accent : ink);
    x = glyph.right + m_Theme.Scale(8);
    {
        RECT text = MakeRect(x, rc.top, rc.right, rc.bottom);
        canvas.Text(label, text, m_Theme.Font(TextStyle::Body), ink, DT_SINGLELINE | DT_VCENTER | DT_LEFT);
    }
    if (dropdown)
    {
        int chevron = m_Theme.Scale(12);
        RECT arrow = MakeRect(rc.right - m_Theme.Scale(10) - chevron, rc.top + (rc.bottom - rc.top - chevron) / 2,
                              rc.right - m_Theme.Scale(10), rc.top + (rc.bottom - rc.top - chevron) / 2 + chevron);
        canvas.Icon(L"chevron_down_12_regular", arrow, ink);
    }
}

void
CCabinetWindow::PaintTabs(Canvas &canvas)
{
    const Palette &c = m_Theme.Colors();
    int x = m_Layout.Tabs.left;

    for (int i = 0; i < m_TabCount; i++)
    {
        CabinetTab *tab = &m_Tabs[i];
        RECT rc = MakeRect(x, m_Layout.Tabs.top, x + tab->Width - m_Theme.Scale(2), CaptionHeight() + m_Theme.Scale(8));
        RECT text, close, closeHot;
        int iconSize = GetSystemMetricsForDpi(SM_CXSMICON, m_Theme.Dpi());

        if (i == m_ActiveTab)
            canvas.FillRound(rc, m_Theme.ScaleF(8.0f), c.ToolbarBand);
        else if (i == m_HotTab)
        {
            RECT hot = rc;
            hot.bottom = CaptionHeight() - m_Theme.Scale(4);
            canvas.FillRound(hot, m_Theme.ScaleF(6.0f), c.SubtleHover);
        }
        if (tab->Icon)
        {
            DrawIconEx(canvas.Dc(), rc.left + m_Theme.Scale(12), rc.top + (CaptionHeight() - rc.top - iconSize) / 2,
                       tab->Icon, iconSize, iconSize, 0, NULL, DI_NORMAL);
        }
        close = MakeRect(rc.right - m_Theme.Scale(32), rc.top + m_Theme.Scale(4), rc.right - m_Theme.Scale(8),
                         CaptionHeight() - m_Theme.Scale(4));
        closeHot = close;
        if (i == m_HotTab && m_HotTabClose)
            canvas.FillRound(closeHot, m_Theme.ScaleF(4.0f), c.SubtleHover);
        if (i == m_ActiveTab || i == m_HotTab)
            canvas.Icon(L"dismiss_16_regular", CenterRect(close, m_Theme.Scale(12), m_Theme.Scale(12)), c.Text);
        text = MakeRect(rc.left + m_Theme.Scale(36), rc.top, close.left - m_Theme.Scale(4), CaptionHeight());
        canvas.Text(tab->Title, text, m_Theme.Font(TextStyle::Caption), i == m_ActiveTab ? c.Text : c.TextSecondary,
                    DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);
        x += tab->Width;
    }
    if (m_HotCommand == CabinetCommand::NewTab)
        canvas.FillRound(m_Layout.NewTab, m_Theme.ScaleF(4.0f), c.SubtleHover);
    canvas.Icon(L"add_16_regular", CenterRect(m_Layout.NewTab, m_Theme.Scale(16), m_Theme.Scale(16)), c.Text);
}

void
CCabinetWindow::PaintAddressBand(Canvas &canvas)
{
    const Palette &c = m_Theme.Colors();
    static const struct { CabinetCommand Command; LPCWSTR Icon; } Nav[] =
    {
        { CabinetCommand::Back, L"arrow_left_16_regular" },
        { CabinetCommand::Forward, L"arrow_right_16_regular" },
        { CabinetCommand::Up, L"arrow_up_16_regular" },
        { CabinetCommand::Refresh, L"arrow_clockwise_16_regular" },
    };
    RECT address = m_Layout.Address, search = m_Layout.Search;
    int iconSize = GetSystemMetricsForDpi(SM_CXSMICON, m_Theme.Dpi()), x;
    HRGN clip;

    canvas.Fill(m_Layout.AddressBand, c.ToolbarBand);
    for (UINT i = 0; i < _countof(Nav); i++)
        PaintButton(canvas, Nav[i].Command, Nav[i].Icon, NULL, FALSE);

    canvas.FillRound(address, m_Theme.ScaleF(4.0f), IsWindowVisible(m_AddressEdit) ? c.ControlFillFocus : c.ControlFill);
    canvas.FrameRound(address, m_Theme.ScaleF(4.0f), c.ControlStroke);
    if (IsWindowVisible(m_AddressEdit))
    {
        RECT line = MakeRect(address.left + 1, address.bottom - m_Theme.Scale(2), address.right - 1, address.bottom);
        canvas.Fill(line, c.Accent);
    }
    else
    {
        clip = CreateRectRgn(address.left + 1, address.top, address.right - 1, address.bottom);
        SelectClipRgn(canvas.Dc(), clip);
        x = address.left + m_Theme.Scale(10);
        if (m_LocationIcon)
        {
            DrawIconEx(canvas.Dc(), x, address.top + (address.bottom - address.top - iconSize) / 2, m_LocationIcon,
                       iconSize, iconSize, 0, NULL, DI_NORMAL);
            x += iconSize + m_Theme.Scale(6);
        }
        {
            int total = 0, available = address.right - m_Theme.Scale(8) - x, overflow = m_Theme.Scale(28);
            int widths[CABINET_MAX_CRUMBS];
            for (int i = 0; i < m_CrumbCount; i++)
            {
                SIZE size = canvas.MeasureText(m_Crumbs[i].Name, m_Theme.Font(TextStyle::Body));
                widths[i] = m_Theme.Scale(20) + size.cx + m_Theme.Scale(12);
                total += widths[i];
            }
            m_CrumbFirst = 0;
            while (m_CrumbFirst < m_CrumbCount - 1 && total + (m_CrumbFirst ? overflow : 0) > available)
                total -= widths[m_CrumbFirst++];
            SetRectEmpty(&m_CrumbOverflow);
            if (m_CrumbFirst)
            {
                m_CrumbOverflow = MakeRect(x, address.top + m_Theme.Scale(4), x + overflow, address.bottom - m_Theme.Scale(4));
                if (m_HotCrumb == -2)
                    canvas.FillRound(m_CrumbOverflow, m_Theme.ScaleF(4.0f), c.SubtleHover);
                canvas.Icon(L"more_horizontal_20_regular", CenterRect(m_CrumbOverflow, m_Theme.Scale(16), m_Theme.Scale(16)),
                            c.TextSecondary);
                x = m_CrumbOverflow.right;
            }
            for (int i = 0; i < m_CrumbFirst; i++)
            {
                SetRectEmpty(&m_Crumbs[i].Rect);
                SetRectEmpty(&m_Crumbs[i].Chevron);
            }
        }
        for (int i = m_CrumbFirst; i < m_CrumbCount; i++)
        {
            CabinetCrumb *crumb = &m_Crumbs[i];
            SIZE size = canvas.MeasureText(crumb->Name, m_Theme.Font(TextStyle::Body));
            int chevron = m_Theme.Scale(16), pad = m_Theme.Scale(6);

            crumb->Chevron = MakeRect(x, address.top + m_Theme.Scale(4), x + chevron + m_Theme.Scale(4),
                                      address.bottom - m_Theme.Scale(4));
            if (i == m_HotCrumb && m_HotCrumbChevron)
                canvas.FillRound(crumb->Chevron, m_Theme.ScaleF(4.0f), c.SubtleHover);
            canvas.Icon(L"chevron_right_12_regular", CenterRect(crumb->Chevron, m_Theme.Scale(12), m_Theme.Scale(12)),
                        c.TextSecondary);
            x = crumb->Chevron.right;
            crumb->Rect = MakeRect(x, address.top + m_Theme.Scale(4), x + size.cx + pad * 2,
                                   address.bottom - m_Theme.Scale(4));
            if (i == m_HotCrumb && !m_HotCrumbChevron)
                canvas.FillRound(crumb->Rect, m_Theme.ScaleF(4.0f), c.SubtleHover);
            canvas.Text(crumb->Name, crumb->Rect, m_Theme.Font(TextStyle::Body), c.Text,
                        DT_SINGLELINE | DT_VCENTER | DT_CENTER);
            x = crumb->Rect.right;
        }
        SelectClipRgn(canvas.Dc(), NULL);
        DeleteObject(clip);
    }

    canvas.FillRound(search, m_Theme.ScaleF(4.0f), GetFocus() == m_SearchEdit ? c.ControlFillFocus : c.ControlFill);
    canvas.FrameRound(search, m_Theme.ScaleF(4.0f), c.ControlStroke);
    if (GetFocus() == m_SearchEdit)
    {
        RECT line = MakeRect(search.left + 1, search.bottom - m_Theme.Scale(2), search.right - 1, search.bottom);
        canvas.Fill(line, c.Accent);
    }
    {
        RECT glyph = MakeRect(search.right - m_Theme.Scale(28), search.top, search.right - m_Theme.Scale(10),
                              search.bottom);
        canvas.Icon(L"search_16_regular", CenterRect(glyph, m_Theme.Scale(16), m_Theme.Scale(16)), c.TextSecondary);
    }
}

void
CCabinetWindow::PaintCommandBand(Canvas &canvas)
{
    const Palette &c = m_Theme.Colors();
    WCHAR text[64];
    RECT band = m_Layout.CommandBand;
    static const CabinetCommand Separators[] = { CabinetCommand::Cut, CabinetCommand::Sort, CabinetCommand::More };

    canvas.Fill(band, c.Content);
    canvas.Line(band.left, band.top, band.right, band.top, c.Divider);
    canvas.Line(band.left, band.bottom - 1, band.right, band.bottom - 1, c.Divider);
    for (UINT i = 0; i < _countof(Separators); i++)
    {
        RECT rc = m_Layout.Buttons[(int)Separators[i]];
        int x = rc.left - m_Theme.Scale(13);
        if (IsRectEmpty(&rc))
            continue;
        canvas.Line(x, band.top + m_Theme.Scale(14), x, band.bottom - m_Theme.Scale(14), c.Divider);
    }
    LoadText(IDS_CAB_NEW, text, _countof(text));
    PaintButton(canvas, CabinetCommand::NewItem, L"add_circle_20_regular", text, TRUE);
    PaintButton(canvas, CabinetCommand::Cut, L"cut_20_regular", NULL, FALSE);
    PaintButton(canvas, CabinetCommand::Copy, L"copy_20_regular", NULL, FALSE);
    PaintButton(canvas, CabinetCommand::Paste, L"clipboard_paste_20_regular", NULL, FALSE);
    PaintButton(canvas, CabinetCommand::Rename, L"rename_20_regular", NULL, FALSE);
    PaintButton(canvas, CabinetCommand::Share, L"share_20_regular", NULL, FALSE);
    PaintButton(canvas, CabinetCommand::Delete, L"delete_20_regular", NULL, FALSE);
    LoadText(IDS_CAB_SORT, text, _countof(text));
    PaintButton(canvas, CabinetCommand::Sort, L"arrow_sort_20_regular", text, TRUE);
    LoadText(IDS_CAB_VIEW, text, _countof(text));
    PaintButton(canvas, CabinetCommand::View, L"text_bullet_list_ltr_20_regular", text, TRUE);
    PaintButton(canvas, CabinetCommand::More, L"more_horizontal_20_regular", NULL, FALSE);
    LoadText(IDS_CAB_DETAILS, text, _countof(text));
    PaintButton(canvas, CabinetCommand::Details, L"panel_right_20_regular", text, FALSE);
}

void
CCabinetWindow::PaintStatus(Canvas &canvas)
{
    const Palette &c = m_Theme.Colors();
    RECT text = m_Layout.Status;
    FOLDERVIEWMODE mode = FVM_DETAILS;
    IShellView *view = CurrentView();

    if (view)
    {
        IFolderView *folderView;
        if (SUCCEEDED(view->QueryInterface(IID_PPV_ARG(IFolderView, &folderView))))
        {
            UINT current = FVM_DETAILS;
            folderView->GetCurrentViewMode(&current);
            mode = (FOLDERVIEWMODE)current;
            folderView->Release();
        }
        view->Release();
    }
    canvas.Fill(m_Layout.Status, c.Content);
    text.left += m_Theme.Scale(14);
    canvas.Text(m_StatusText, text, m_Theme.Font(TextStyle::Caption), c.Text, DT_SINGLELINE | DT_VCENTER);
    if (mode == FVM_DETAILS)
        canvas.FillRound(m_Layout.Buttons[(int)CabinetCommand::ViewDetails], m_Theme.ScaleF(3.0f), c.Selected);
    else if (mode == FVM_TILE || mode == FVM_ICON)
        canvas.FillRound(m_Layout.Buttons[(int)CabinetCommand::ViewTiles], m_Theme.ScaleF(3.0f), c.Selected);
    canvas.Icon(L"text_bullet_list_ltr_16_regular",
                CenterRect(m_Layout.Buttons[(int)CabinetCommand::ViewDetails], m_Theme.Scale(16), m_Theme.Scale(16)),
                c.Text);
    canvas.Icon(L"grid_16_regular",
                CenterRect(m_Layout.Buttons[(int)CabinetCommand::ViewTiles], m_Theme.Scale(16), m_Theme.Scale(16)),
                c.Text);
}

void
CCabinetWindow::OnPaint(Canvas &canvas, const RECT &client)
{
    if (m_Theme.UpdateMaterial(m_hwnd))
        ApplyChildColors();
    const Palette &c = m_Theme.Colors();
    RECT strip = MakeRect(0, 0, client.right, CaptionHeight());

    canvas.Fill(strip, c.TabStrip);
    PaintTabs(canvas);
#if SHELLFRAME_CUSTOM_CAPTION_BUTTONS
    PaintCaptionButtons(canvas, client, c.TabStrip);
#endif
    PaintAddressBand(canvas);
    PaintCommandBand(canvas);
    if (m_NavigationVisible)
    {
        canvas.Fill(m_Layout.Navigation, c.Content);
        canvas.Line(m_Layout.Navigation.right, m_Layout.Navigation.top, m_Layout.Navigation.right,
                    m_Layout.Navigation.bottom, c.Divider);
    }
    if (m_ActiveTab < 0)
        canvas.Fill(m_Layout.View, c.Content);
    PaintDetails(canvas);
    PaintStatus(canvas);
}

int
CCabinetWindow::HitTab(POINT pt, BOOL *close)
{
    int x = m_Layout.Tabs.left;

    *close = FALSE;
    if (pt.y < m_Layout.Tabs.top || pt.y >= CaptionHeight())
        return -1;
    for (int i = 0; i < m_TabCount; i++)
    {
        if (pt.x >= x && pt.x < x + m_Tabs[i].Width)
        {
            *close = pt.x >= x + m_Tabs[i].Width - m_Theme.Scale(34);
            return i;
        }
        x += m_Tabs[i].Width;
    }
    return -1;
}

CabinetCommand
CCabinetWindow::HitCommand(POINT pt)
{
    for (int i = (int)CabinetCommand::Back; i < (int)CabinetCommand::Count; i++)
    {
        if (PtInRect(&m_Layout.Buttons[i], pt))
            return (CabinetCommand)i;
    }
    return CabinetCommand::None;
}

int
CCabinetWindow::HitCrumb(POINT pt, BOOL *chevron)
{
    *chevron = FALSE;
    if (IsWindowVisible(m_AddressEdit) || !PtInRect(&m_Layout.Address, pt))
        return -1;
    if (PtInRect(&m_CrumbOverflow, pt))
        return -2;
    for (int i = m_CrumbFirst; i < m_CrumbCount; i++)
    {
        if (PtInRect(&m_Crumbs[i].Chevron, pt))
        {
            *chevron = TRUE;
            return i;
        }
        if (PtInRect(&m_Crumbs[i].Rect, pt))
            return i;
    }
    return -1;
}

void
CCabinetWindow::SetHot(int tab, BOOL tabClose, CabinetCommand command, int crumb, BOOL crumbChevron)
{
    if (tab == m_HotTab && tabClose == m_HotTabClose && command == m_HotCommand && crumb == m_HotCrumb &&
        crumbChevron == m_HotCrumbChevron)
        return;
    m_HotTab = tab;
    m_HotTabClose = tabClose;
    m_HotCommand = command;
    m_HotCrumb = crumb;
    m_HotCrumbChevron = crumbChevron;
    Invalidate(NULL);
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
        case CabinetCommand::Share:
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
    RECT rc = m_Layout.Buttons[(int)(IsRectEmpty(&m_Layout.Buttons[(int)command]) ? CabinetCommand::More : command)];
    MapWindowPoints(m_hwnd, NULL, (POINT *)&rc, 2);
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
        folderView->SetViewModeAndIconSize(Modes[command - 1].Mode, Modes[command - 1].Size);
    else if (command == 50)
    {
        m_NavigationVisible = !m_NavigationVisible;
        Relayout();
    }
    else if (command == 51)
        ExecuteCommand(CabinetCommand::Details);
    else if (command == 52)
    {
        DWORD flags = 0;
        folderView->GetCurrentFolderFlags(&flags);
        folderView->SetCurrentFolderFlags(FWF_CHECKSELECT, (flags & FWF_CHECKSELECT) ? 0 : FWF_CHECKSELECT);
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
    InvalidateRect(m_hwnd, &m_Layout.Status, FALSE);
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
    static const struct { CabinetCommand Command; UINT Label; } Overflow[] =
    {
        { CabinetCommand::NewItem, IDS_CAB_NEW },
        { CabinetCommand::Cut, IDS_CAB_CUT },
        { CabinetCommand::Copy, IDS_CAB_COPY },
        { CabinetCommand::Paste, IDS_CAB_PASTE },
        { CabinetCommand::Rename, IDS_CAB_RENAME },
        { CabinetCommand::Share, IDS_CAB_SHARE },
        { CabinetCommand::Delete, IDS_CAB_DELETE },
        { CabinetCommand::Sort, IDS_CAB_SORT },
        { CabinetCommand::View, IDS_CAB_VIEW },
    };
    BOOL overflow = FALSE;

    for (UINT i = 0; i < _countof(Overflow); i++)
    {
        if (!IsRectEmpty(&m_Layout.Buttons[(int)Overflow[i].Command]))
            continue;
        LoadText(Overflow[i].Label, text, _countof(text));
        AppendMenuW(popup, MF_STRING | (CommandEnabled(Overflow[i].Command) ? 0 : MF_GRAYED),
                    100 + (UINT)Overflow[i].Command, text);
        overflow = TRUE;
    }
    if (overflow)
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
    command = TrackPopupMenuEx(popup, TPM_RETURNCMD | TPM_RIGHTALIGN | TPM_TOPALIGN, anchor.right, anchor.bottom,
                               m_hwnd, NULL);
    DestroyMenu(popup);
    if (command >= 100 && command < 100 + (UINT)CabinetCommand::Count)
    {
        if (view)
            view->Release();
        ExecuteCommand((CabinetCommand)(command - 100));
        return;
    }
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
        {
            int selected = 0;
            if (folderView)
                folderView->ItemCount(SVGIO_SELECTION, &selected);
            if (selected)
                InvokeSelectionVerb("properties");
            else
                InvokeBackgroundVerb("properties");
            break;
        }
        case 5:
            ShellExecuteW(m_hwnd, NULL, L"rundll32.exe", L"shell32.dll,Options_RunDLL 0", NULL, SW_SHOWNORMAL);
            break;
    }
    if (folderView)
        folderView->Release();
    if (view)
        view->Release();
    UpdateStatus();
}

void
CCabinetWindow::ShowCrumbMenu(int index)
{
    IShellFolder *folder = NULL;
    IEnumIDList *items = NULL;
    HMENU popup;
    PITEMID_CHILD child;
    PITEMID_CHILD children[256];
    UINT count = 0, command;
    POINT pt;

    if (index < 0 || index >= m_CrumbCount)
        return;
    if (FAILED(SHBindToObject(NULL, index ? m_Crumbs[index - 1].Pidl : NULL, NULL, IID_PPV_ARG(IShellFolder, &folder))))
    {
        if (index)
            return;
        if (FAILED(SHGetDesktopFolder(&folder)))
            return;
    }
    popup = CreatePopupMenu();
    if (SUCCEEDED(folder->EnumObjects(m_hwnd, SHCONTF_FOLDERS, &items)) && items)
    {
        while (count < _countof(children) && items->Next(1, &child, NULL) == S_OK)
        {
            STRRET str;
            WCHAR name[MAX_PATH];
            if (SUCCEEDED(folder->GetDisplayNameOf(child, SHGDN_INFOLDER, &str)) &&
                SUCCEEDED(StrRetToBufW(&str, child, name, _countof(name))))
            {
                AppendMenuW(popup, MF_STRING, count + 1, name);
                children[count++] = child;
            }
            else
            {
                ILFree(child);
            }
        }
        items->Release();
    }
    pt.x = m_Crumbs[index].Chevron.left;
    pt.y = m_Layout.Address.bottom;
    ClientToScreen(m_hwnd, &pt);
    command = count ? TrackPopupMenuEx(popup, TPM_RETURNCMD | TPM_LEFTALIGN | TPM_TOPALIGN, pt.x, pt.y, m_hwnd, NULL) : 0;
    DestroyMenu(popup);
    if (command >= 1 && command <= count)
    {
        PIDLIST_ABSOLUTE target = ILCombine(index ? m_Crumbs[index - 1].Pidl : NULL, children[command - 1]);
        if (target)
        {
            Navigate(target);
            ILFree(target);
        }
    }
    for (UINT i = 0; i < count; i++)
        ILFree(children[i]);
    folder->Release();
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
        case CabinetCommand::Share: InvokeSelectionVerb("sendto"); break;
        case CabinetCommand::Delete: InvokeSelectionVerb("delete"); break;
        case CabinetCommand::Sort: ShowSortMenu(); break;
        case CabinetCommand::View: ShowViewMenu(); break;
        case CabinetCommand::More: ShowMoreMenu(); break;
        case CabinetCommand::Details:
            m_DetailsPane = !m_DetailsPane;
            Relayout();
            UpdateDetails();
            break;
        case CabinetCommand::ViewDetails:
        case CabinetCommand::ViewTiles:
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
            InvalidateRect(m_hwnd, &m_Layout.Status, FALSE);
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
    Invalidate(NULL);
}

BOOL
CCabinetWindow::TranslateAcceleratorMessage(MSG *msg)
{
    BOOL control = GetKeyState(VK_CONTROL) < 0, alt = GetKeyState(VK_MENU) < 0, shift = GetKeyState(VK_SHIFT) < 0;
    IShellView *view;

    if (!m_hwnd || (msg->hwnd != m_hwnd && !IsChild(m_hwnd, msg->hwnd)))
        return FALSE;
    if (msg->message == WM_KEYDOWN && msg->wParam == VK_F6 && (msg->hwnd == m_AddressEdit || msg->hwnd == m_SearchEdit))
    {
        CycleFocus();
        return TRUE;
    }
    if (msg->message == WM_KEYDOWN && msg->hwnd == m_AddressEdit)
    {
        if (msg->wParam == VK_RETURN || msg->wParam == VK_ESCAPE)
        {
            EndAddressEdit(msg->wParam == VK_RETURN);
            return TRUE;
        }
        return FALSE;
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
        return FALSE;
    }
    if (msg->message == WM_SYSKEYDOWN && alt)
    {
        switch (msg->wParam)
        {
            case VK_LEFT: NavigateHistory(-1); return TRUE;
            case VK_RIGHT: NavigateHistory(1); return TRUE;
            case VK_UP: NavigateUp(); return TRUE;
            case 'D': BeginAddressEdit(); return TRUE;
        }
    }
    if (msg->message == WM_KEYDOWN)
    {
        if (msg->wParam == VK_F5)
        {
            Refresh();
            return TRUE;
        }
        if (msg->wParam == VK_F6)
        {
            CycleFocus();
            return TRUE;
        }
        if (msg->wParam == VK_F4 || (control && msg->wParam == 'L'))
        {
            BeginAddressEdit();
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
        if (msg->wParam == VK_BACK && msg->hwnd != m_SearchEdit)
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
CCabinetWindow::HandleMessage(UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
        case WM_MOUSEMOVE:
        {
            POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            BOOL close, chevron;
            int tab = HitTab(pt, &close), crumb = HitCrumb(pt, &chevron);
            if (!m_TrackingClient)
            {
                TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, m_hwnd, 0 };
                m_TrackingClient = TrackMouseEvent(&tme);
            }
            SetHot(tab, close, HitCommand(pt), crumb, chevron);
            break;
        }
        case WM_MOUSELEAVE:
            m_TrackingClient = FALSE;
            m_PressedCommand = CabinetCommand::None;
            SetHot(-1, FALSE, CabinetCommand::None, -1, FALSE);
            break;
        case WM_LBUTTONDOWN:
        {
            POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            BOOL close, chevron;
            int tab = HitTab(pt, &close), crumb;
            if (tab >= 0)
            {
                if (close)
                    CloseTab(tab);
                else
                    ActivateTab(tab);
                return 0;
            }
            crumb = HitCrumb(pt, &chevron);
            if (crumb == -2)
            {
                ShowCrumbOverflowMenu();
                return 0;
            }
            if (crumb >= 0)
            {
                if (chevron)
                    ShowCrumbMenu(crumb);
                else
                    Navigate(m_Crumbs[crumb].Pidl);
                return 0;
            }
            if (PtInRect(&m_Layout.Address, pt))
            {
                BeginAddressEdit();
                return 0;
            }
            m_PressedCommand = HitCommand(pt);
            if (m_PressedCommand != CabinetCommand::None)
                Invalidate(NULL);
            return 0;
        }
        case WM_LBUTTONUP:
        {
            POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            CabinetCommand command = HitCommand(pt), pressed = m_PressedCommand;
            m_PressedCommand = CabinetCommand::None;
            if (command != CabinetCommand::None && command == pressed)
                ExecuteCommand(command);
            Invalidate(NULL);
            return 0;
        }
        case WM_MBUTTONUP:
        {
            POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            BOOL close;
            int tab = HitTab(pt, &close);
            if (tab >= 0)
                CloseTab(tab);
            return 0;
        }
        case WM_CABINET_STATUS:
            UpdateStatus();
            UpdateDetails();
            return 0;
        case WM_RBUTTONUP:
        {
            POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            BOOL close;
            int tab = HitTab(pt, &close);
            if (tab >= 0)
            {
                ClientToScreen(m_hwnd, &pt);
                ShowTabMenu(tab, pt);
                return 0;
            }
            break;
        }
        case WM_CABINET_CHANGED:
            SetTimer(m_hwnd, IDT_CABINET_STATUS, 200, NULL);
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
        case WM_COMMAND:
            if (LOWORD(wParam) == IDC_ADDRESS_EDIT && HIWORD(wParam) == EN_KILLFOCUS)
                EndAddressEdit(FALSE);
            if ((LOWORD(wParam) == IDC_SEARCH_EDIT) && (HIWORD(wParam) == EN_SETFOCUS || HIWORD(wParam) == EN_KILLFOCUS))
                InvalidateRect(m_hwnd, &m_Layout.Search, FALSE);
            break;
        case WM_CTLCOLOREDIT:
        {
            const Palette &c = m_Theme.Colors();
            static HBRUSH focusBrush, normalBrush;
            static COLORREF focusColor, normalColor;
            BOOL focus = (HWND)lParam == m_AddressEdit || GetFocus() == (HWND)lParam;
            if (!focusBrush || focusColor != c.ControlFillFocus)
            {
                if (focusBrush)
                    DeleteObject(focusBrush);
                focusColor = c.ControlFillFocus;
                focusBrush = CreateSolidBrush(focusColor);
            }
            if (!normalBrush || normalColor != c.ControlFill)
            {
                if (normalBrush)
                    DeleteObject(normalBrush);
                normalColor = c.ControlFill;
                normalBrush = CreateSolidBrush(normalColor);
            }
            SetTextColor((HDC)wParam, c.Text);
            SetBkColor((HDC)wParam, focus ? focusColor : normalColor);
            return (LRESULT)(focus ? focusBrush : normalBrush);
        }
        case WM_ACTIVATE:
            if (LOWORD(wParam) == WA_INACTIVE)
            {
                HWND focus = GetFocus();
                m_FocusSave = focus && IsChild(m_hwnd, focus) ? focus : NULL;
            }
            break;
        case WM_SETFOCUS:
            RestoreFocus();
            return 0;
        case WM_CLOSE:
            SavePlacement();
            while (m_TabCount > 1)
                CloseTab(m_TabCount - 1);
            CloseTab(0);
            return 0;
        case WM_DESTROY:
            if (m_ChangeNotify)
                SHChangeNotifyDeregister(m_ChangeNotify);
            m_ChangeNotify = 0;
            if (m_Navigation)
            {
                m_Navigation->Release();
                m_Navigation = NULL;
            }
            if (m_EditFont)
                DeleteObject(m_EditFont);
            m_EditFont = NULL;
            if (m_SmallIcon)
                DestroyIcon(m_SmallIcon);
            if (m_BigIcon)
                DestroyIcon(m_BigIcon);
            m_SmallIcon = m_BigIcon = NULL;
            PostQuitMessage(0);
            break;
    }
    return FrameWindow::HandleMessage(msg, wParam, lParam);
}
