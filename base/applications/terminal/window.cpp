/*
 * PROJECT:     LiberNT Terminal
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Terminal window: tab strip, sessions, actions and find bar
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "terminal.h"

#include <vssym32.h>

#define WINDOW_CLASS L"LiberNT.Terminal"
#define TIMER_SETTINGS 10
#define FIND_EDIT_ID 900
#define STRIP_HEIGHT 40

std::wstring
Tab::Title() const
{
    if (!CustomTitle.empty())
        return CustomTitle;
    if (!Term)
        return LoadStr(IDS_NEW_TAB);
    if (!Term->Settings().TabTitle.empty())
        return Term->Settings().TabTitle;
    if (!Term->Settings().SuppressApplicationTitle && !Term->Screen().Title().empty())
        return ToWide(Term->Screen().Title());
    return Term->Settings().Name;
}

TerminalWindow::TerminalWindow() :
    m_View(this)
{
}

TerminalWindow::~TerminalWindow()
{
    if (m_FindFont)
        DeleteObject(m_FindFont);
    if (m_TabFont)
        DeleteObject(m_TabFont);
}

void
TerminalWindow::UpdateTabFont()
{
    NONCLIENTMETRICSW metrics = { sizeof(metrics) };
    HFONT old = m_TabFont;

    if (!SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(metrics), &metrics, 0))
        return;
    metrics.lfMessageFont.lfHeight = MulDiv(metrics.lfMessageFont.lfHeight, (int)m_Theme.Dpi(), (int)GetDpiForSystem());
    m_TabFont = CreateFontIndirectW(&metrics.lfMessageFont);
    m_Band.SetFont(m_TabFont);
    if (old)
        DeleteObject(old);
}

bool
TerminalWindow::Open(int showCmd, const std::wstring &profileName, const std::wstring &commandLine,
                     const std::wstring &directory)
{
    const Profile *profile = profileName.empty() ? &g_Settings.Default() : g_Settings.FindProfile(profileName);
    HICON icon = (HICON)LoadImageW(g_Instance, MAKEINTRESOURCEW(IDI_TERMINAL), IMAGE_ICON, 0, 0, LR_DEFAULTSIZE | LR_SHARED);
    int width, height;

    if (!profile)
        profile = &g_Settings.Default();
    ApplyTheme();
    m_SettingsTime = SettingsStore::LastWrite();

    /* Size the window for the initial grid before the first tab exists. */
    {
        HDC hdc = GetDC(NULL);
        HFONT font = CreateFontW(-MulDiv(profile->FontSize, GetDeviceCaps(hdc, LOGPIXELSY), 72), 0, 0, 0, FW_NORMAL,
                                 FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS,
                                 CLEARTYPE_QUALITY, FIXED_PITCH | FF_MODERN, profile->FontFace.c_str());
        HGDIOBJ old = SelectObject(hdc, font);
        TEXTMETRICW metrics;
        SIZE extent;
        RECT frame = {};

        GetTextMetricsW(hdc, &metrics);
        GetTextExtentPoint32W(hdc, L"MMMMMMMMMM", 10, &extent);
        SelectObject(hdc, old);
        DeleteObject(font);
        frame.right = MulDiv((extent.cx * g_Settings.InitialCols) / 10, 96, GetDeviceCaps(hdc, LOGPIXELSX)) +
                      2 * profile->Padding + GetSystemMetricsForDpi(SM_CXVSCROLL, 96);
        frame.bottom = MulDiv(metrics.tmHeight * g_Settings.InitialRows, 96, GetDeviceCaps(hdc, LOGPIXELSY)) +
                       2 * profile->Padding + STRIP_HEIGHT;
        ReleaseDC(NULL, hdc);
        AdjustWindowRectExForDpi(&frame, WS_OVERLAPPEDWINDOW, FALSE, 0, 96);
        width = frame.right - frame.left;
        height = ClientCaption() ? frame.bottom : frame.bottom - frame.top;
    }

    if (!Create(WINDOW_CLASS, LoadStr(IDS_APP_TITLE).c_str(), icon, width, height, SW_HIDE))
        return false;
    if (!NewTab(*profile, commandLine, directory))
    {
        DestroyWindow(m_hwnd);
        return false;
    }
    ShowWindow(m_hwnd, showCmd);
    UpdateWindow(m_hwnd);
    SetFocus(m_View.Window());
    return true;
}

void
TerminalWindow::ApplyTheme()
{
    m_Theme.SetDarkPreference(g_Settings.Theme == ThemeMode::Dark ? 1 : g_Settings.Theme == ThemeMode::Light ? 0 : -1);
    if (m_hwnd)
        m_Theme.Update(m_hwnd);
}

COLORREF
TerminalWindow::SchemeBackground() const
{
    const Profile &profile = m_Active >= 0 && m_Active < (int)m_Tabs.size() && m_Tabs[m_Active]->Term ?
                             m_Tabs[m_Active]->Term->Settings() : g_Settings.Default();
    return g_Settings.SchemeFor(profile).Background;
}

void
TerminalWindow::OnCreate()
{
    ApplyTheme();
    m_Band.Create(m_hwnd, this, NULL, L"LiberNT.TerminalTabBand");
    m_Band.SetMenuButton(TRUE);
    UpdateTabFont();
    m_View.Create(m_hwnd);
    m_View.ApplyFont(m_Theme.Dpi());
    SetTimer(m_hwnd, TIMER_SETTINGS, 1500, NULL);
}

void
TerminalWindow::OnThemeChanged()
{
    ApplyTheme();
    UpdateTabFont();
    m_View.ApplyFont(m_Theme.Dpi());
    if (m_FindFont)
    {
        DeleteObject(m_FindFont);
        m_FindFont = NULL;
    }
    ResizeSessions();
}

int
TerminalWindow::StripHeight() const
{
    return m_Fullscreen ? 0 : m_Theme.Scale(STRIP_HEIGHT);
}

int
TerminalWindow::CaptionHeight() const
{
    return StripHeight();
}

void
TerminalWindow::OnLayout(const RECT &client)
{
    int top = StripHeight();

    if (m_View.Window())
        MoveWindow(m_View.Window(), 0, top, client.right, client.bottom > top ? client.bottom - top : 0, TRUE);
    LayoutTabs();
    LayoutFindBar();
    ResizeSessions();
}

void
TerminalWindow::ResizeSessions()
{
    RECT rc;
    int cols, rows;

    if (!m_View.Window())
        return;
    GetClientRect(m_View.Window(), &rc);
    if (rc.right <= 0 || rc.bottom <= 0)
        return;
    for (auto &tab : m_Tabs)
    {
        if (!tab->Term)
            continue;
        m_View.GridFor(tab->Term->Settings(), rc.right, rc.bottom, &cols, &rows);
        tab->Term->Resize(cols, rows);
    }
    m_View.Refresh(true);
}

void
TerminalWindow::LayoutTabs()
{
    RECT client;
    int band;

    if (!m_hwnd || !m_Band.Window())
        return;
    GetClientRect(m_hwnd, &client);
    band = client.right - CaptionButtonsWidth() - m_Theme.Scale(8);
    MoveWindow(m_Band.Window(), 0, 0, band > 0 ? band : 0, StripHeight(), TRUE);
    ShowWindow(m_Band.Window(), m_Fullscreen ? SW_HIDE : SW_SHOWNA);
    InvalidateTabs();
}

static bool
ProgramFileInfo(const std::wstring &commandLine, UINT flags, SHFILEINFOW *info)
{
    std::wstring program;
    wchar_t expanded[MAX_PATH * 2], found[MAX_PATH];
    size_t start = commandLine.find_first_not_of(L" \t"), end;

    if (start == std::wstring::npos)
        return false;
    if (commandLine[start] == L'"')
    {
        end = commandLine.find(L'"', start + 1);
        program = commandLine.substr(start + 1, end == std::wstring::npos ? std::wstring::npos : end - start - 1);
    }
    else
    {
        end = commandLine.find_first_of(L" \t", start);
        program = commandLine.substr(start, end == std::wstring::npos ? std::wstring::npos : end - start);
    }
    if (!ExpandEnvironmentStringsW(program.c_str(), expanded, ARRAYSIZE(expanded)))
        return false;
    if (SearchPathW(NULL, expanded, L".exe", ARRAYSIZE(found), found, NULL))
        StringCchCopyW(expanded, ARRAYSIZE(expanded), found);
    return SHGetFileInfoW(expanded, 0, info, sizeof(*info), flags) != 0;
}

static int
ProgramIconIndex(const std::wstring &commandLine)
{
    SHFILEINFOW info = {};

    if (!ProgramFileInfo(commandLine, SHGFI_SYSICONINDEX | SHGFI_SMALLICON, &info))
        return -1;
    return info.iIcon;
}

static HBITMAP
ProgramMenuBitmap(const std::wstring &commandLine, int size)
{
    SHFILEINFOW info = {};
    BITMAPINFO format = {};
    HBITMAP bitmap;
    void *bits;
    HDC dc;

    if (!ProgramFileInfo(commandLine, SHGFI_ICON | SHGFI_SMALLICON, &info) || !info.hIcon)
        return NULL;
    format.bmiHeader.biSize = sizeof(format.bmiHeader);
    format.bmiHeader.biWidth = size;
    format.bmiHeader.biHeight = -size;
    format.bmiHeader.biPlanes = 1;
    format.bmiHeader.biBitCount = 32;
    format.bmiHeader.biCompression = BI_RGB;
    dc = CreateCompatibleDC(NULL);
    bitmap = dc ? CreateDIBSection(dc, &format, DIB_RGB_COLORS, &bits, NULL, 0) : NULL;
    if (bitmap)
    {
        HGDIOBJ old = SelectObject(dc, bitmap);

        DrawIconEx(dc, 0, 0, info.hIcon, size, size, 0, NULL, DI_NORMAL);
        SelectObject(dc, old);
    }
    if (dc)
        DeleteDC(dc);
    DestroyIcon(info.hIcon);
    return bitmap;
}

void
TerminalWindow::InvalidateTabs()
{
    std::vector<ShellFrame::TabBandItem> items(m_Tabs.size());
    std::vector<std::wstring> titles(m_Tabs.size());

    if (!m_Band.Window())
        return;
    for (size_t i = 0; i < m_Tabs.size(); i++)
    {
        Tab &tab = *m_Tabs[i];

        if (tab.IconIndex < 0 && tab.Term)
            tab.IconIndex = ProgramIconIndex(tab.Term->Settings().CommandLine);
        titles[i] = tab.Title();
        items[i].Title = titles[i].c_str();
        items[i].IconIndex = tab.IconIndex;
        items[i].Attention = tab.Activity || tab.Bell;
    }
    m_Band.SetOpaque(ClientCaption() ? CLR_INVALID : m_Theme.Colors().TabStrip, SchemeBackground());
    m_Band.SetTabs(items.data(), (int)items.size(), m_Active);
}

void
TerminalWindow::OnPaint(ShellFrame::Canvas &canvas, const RECT &client)
{
    const ShellFrame::Palette &c = m_Theme.Colors();
    RECT strip = client;
    COLORREF content = SchemeBackground();

    strip.bottom = StripHeight();
    canvas.Fill(strip, ClientCaption() ? RGB(0, 0, 0) : c.TabStrip);
    if (strip.bottom < client.bottom)
    {
        RECT below = client;
        below.top = strip.bottom;
        canvas.Fill(below, content);
    }
}

void
TerminalWindow::OnTabSelect(int index)
{
    ActivateTab(index);
}

void
TerminalWindow::OnTabClose(int index)
{
    CloseTab(index);
}

void
TerminalWindow::OnTabMove(int from, int to)
{
    Tab *active = ActiveTab();
    std::unique_ptr<Tab> moved;

    if (from < 0 || to < 0 || from >= (int)m_Tabs.size() || to >= (int)m_Tabs.size() || from == to)
        return;
    moved = std::move(m_Tabs[from]);
    m_Tabs.erase(m_Tabs.begin() + from);
    m_Tabs.insert(m_Tabs.begin() + to, std::move(moved));
    for (size_t i = 0; i < m_Tabs.size(); i++)
    {
        if (m_Tabs[i].get() == active)
            m_Active = (int)i;
    }
    InvalidateTabs();
}

void
TerminalWindow::OnTabMenu(int index, POINT screen)
{
    ShowTabMenu(index, screen);
}

void
TerminalWindow::OnNewTab()
{
    PerformAction(Action::NewTab, 0);
}

void
TerminalWindow::OnNewTabMenu(POINT screen)
{
    ShowProfileMenu(screen);
}

bool
TerminalWindow::NewTab(const Profile &profile, const std::wstring &commandLine, const std::wstring &directory)
{
    std::unique_ptr<Tab> tab(new Tab());
    std::wstring error;
    RECT rc = { 0, 0, 800, 600 };
    int cols = g_Settings.InitialCols, rows = g_Settings.InitialRows;

    if (m_View.Window())
    {
        GetClientRect(m_View.Window(), &rc);
        if (rc.right > 0 && rc.bottom > 0)
            m_View.GridFor(profile, rc.right, rc.bottom, &cols, &rows);
    }
    tab->Term.reset(new Session(m_NextSessionId++, profile, cols, rows));
    if (!tab->Term->Start(m_hwnd, commandLine, directory, &error))
    {
        wchar_t message[600];

        StringCchPrintfW(message, ARRAYSIZE(message), LoadStr(IDS_START_FAILED).c_str(), error.c_str());
        tab->ErrorText = message;
        tab->Term->MarkExited(1);
    }
    m_Tabs.push_back(std::move(tab));
    ActivateTab((int)m_Tabs.size() - 1);
    return true;
}

void
TerminalWindow::ActivateTab(int index)
{
    if (index < 0 || index >= (int)m_Tabs.size())
        return;
    m_Active = index;
    m_Tabs[index]->Activity = false;
    m_Tabs[index]->Bell = false;
    m_View.SetTab(m_Tabs[index].get());
    m_View.ApplyFont(m_Theme.Dpi());
    {
        RECT rc;
        int cols, rows;

        GetClientRect(m_View.Window(), &rc);
        m_View.GridForSize(rc.right, rc.bottom, &cols, &rows);
        if (m_Tabs[index]->Term && rc.right > 0)
            m_Tabs[index]->Term->Resize(cols, rows);
    }
    if (m_FindVisible)
        UpdateFindStatus();
    UpdateWindowTitle();
    InvalidateTabs();
    Invalidate(NULL);
    m_View.Refresh(true);
    if (GetForegroundWindow() == m_hwnd)
        SetFocus(m_View.Window());
}

void
TerminalWindow::CloseTab(int index)
{
    if (index < 0 || index >= (int)m_Tabs.size())
        return;
    if (index == m_Active)
        m_View.SetTab(NULL);
    m_Tabs.erase(m_Tabs.begin() + index);
    if (m_Tabs.empty())
    {
        m_Active = -1;
        DestroyWindow(m_hwnd);
        return;
    }
    if (m_Active > index || m_Active >= (int)m_Tabs.size())
        m_Active--;
    if (m_Active < 0)
        m_Active = 0;
    ActivateTab(m_Active);
}

void
TerminalWindow::UpdateWindowTitle()
{
    Tab *tab = ActiveTab();
    std::wstring title = tab ? tab->Title() : LoadStr(IDS_APP_TITLE);

    SetWindowTextW(m_hwnd, title.c_str());
}

Tab *
TerminalWindow::TabFromSession(UINT id, int *index)
{
    for (size_t i = 0; i < m_Tabs.size(); i++)
    {
        if (m_Tabs[i]->Term && m_Tabs[i]->Term->Id() == id)
        {
            if (index)
                *index = (int)i;
            return m_Tabs[i].get();
        }
    }
    return NULL;
}

void
TerminalWindow::OnSessionOutput(UINT id)
{
    int index;
    Tab *tab = TabFromSession(id, &index);
    size_t scrolled;
    bool titleChanged, bell;
    std::vector<bool> dirty;

    if (!tab || !tab->Term->Pump())
        return;
    scrolled = tab->Term->Screen().TakeScrolled();
    titleChanged = tab->Term->Screen().TakeTitleChanged();
    bell = tab->Term->Screen().TakeBell();
    tab->Term->Screen().TakeDirty(dirty);
    if (index == m_Active)
    {
        m_View.OnOutput(scrolled);
        if (tab->SearchText.size() && m_FindVisible)
            UpdateFindStatus();
    }
    else if (!tab->Activity)
    {
        tab->Activity = true;
        InvalidateTabs();
    }
    if (bell)
    {
        if (tab->Term->Settings().BellSound)
            MessageBeep(MB_OK);
        if (g_Settings.ShowBellIndicator && index != m_Active)
            tab->Bell = true;
        if (GetForegroundWindow() != m_hwnd)
            FlashWindow(m_hwnd, TRUE);
        InvalidateTabs();
    }
    if (titleChanged)
    {
        InvalidateTabs();
        if (index == m_Active)
            UpdateWindowTitle();
    }
}

void
TerminalWindow::OnSessionExited(UINT id, DWORD code)
{
    int index;
    Tab *tab = TabFromSession(id, &index);
    CloseOnExit mode;

    if (!tab)
        return;
    tab->Term->Pump();
    tab->Term->MarkExited(code);
    mode = tab->Term->Settings().OnExit;
    if (mode == CloseOnExit::Always || (mode == CloseOnExit::Graceful && code == 0))
    {
        CloseTab(index);
        return;
    }
    {
        wchar_t message[256];
        std::wstring text;

        StringCchPrintfW(message, ARRAYSIZE(message), LoadStr(IDS_PROCESS_EXITED).c_str(), code);
        text = L"\r\n\r\n";
        text += message;
        tab->Term->Screen().Write(Vt::EncodeUtf8(FromWide(text)).c_str(), Vt::EncodeUtf8(FromWide(text)).size());
    }
    if (index == m_Active)
        m_View.OnOutput(tab->Term->Screen().TakeScrolled());
}

void
TerminalWindow::ShowProfileMenu(POINT pt)
{
    HMENU menu = CreatePopupMenu();
    std::vector<HBITMAP> bitmaps;
    UINT command;
    int visible = 0;

    for (size_t i = 0; i < g_Settings.Profiles.size(); i++)
    {
        const Profile &profile = g_Settings.Profiles[i];
        std::wstring label = profile.Name;
        std::wstring keys;
        MENUITEMINFOW item = { sizeof(item) };

        if (profile.Hidden)
            continue;
        visible++;
        keys = g_Settings.KeysFor(Action::NewTabProfile, visible);
        if (!keys.empty())
            label += L"\t" + keys;
        AppendMenuW(menu, MF_STRING, IDM_PROFILE_FIRST + i, label.c_str());
        item.fMask = MIIM_BITMAP;
        item.hbmpItem = ProgramMenuBitmap(profile.CommandLine, m_Theme.Scale(16));
        if (item.hbmpItem)
        {
            SetMenuItemInfoW(menu, IDM_PROFILE_FIRST + (UINT)i, FALSE, &item);
            bitmaps.push_back(item.hbmpItem);
        }
    }
    AppendMenuW(menu, MF_SEPARATOR, 0, NULL);
    AppendMenuW(menu, MF_STRING, IDM_SETTINGS, (LoadStr(IDS_SETTINGS) + L"\t" + g_Settings.KeysFor(Action::OpenSettings, 0)).c_str());
    AppendMenuW(menu, MF_STRING, IDM_SETTINGS_FILE, LoadStr(IDS_OPEN_SETTINGS_FILE).c_str());
    AppendMenuW(menu, MF_STRING, IDM_ABOUT, LoadStr(IDS_ABOUT).c_str());
    command = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_LEFTALIGN | TPM_TOPALIGN, pt.x, pt.y, 0, m_hwnd, NULL);
    DestroyMenu(menu);
    for (HBITMAP bitmap : bitmaps)
        DeleteObject(bitmap);
    if (command >= IDM_PROFILE_FIRST && command < IDM_PROFILE_FIRST + g_Settings.Profiles.size())
        NewTab(g_Settings.Profiles[command - IDM_PROFILE_FIRST], L"", L"");
    else if (command == IDM_SETTINGS)
        PerformAction(Action::OpenSettings, 0);
    else if (command == IDM_SETTINGS_FILE)
        PerformAction(Action::OpenSettingsFile, 0);
    else if (command == IDM_ABOUT)
        PerformAction(Action::About, 0);
}

void
TerminalWindow::ShowTabMenu(int index, POINT screen)
{
    HMENU menu = CreatePopupMenu();
    UINT command;

    AppendMenuW(menu, MF_STRING, IDM_TAB_RENAME, LoadStr(IDS_RENAME_TAB).c_str());
    AppendMenuW(menu, MF_STRING, IDM_TAB_DUPLICATE, LoadStr(IDS_DUPLICATE_TAB).c_str());
    AppendMenuW(menu, MF_SEPARATOR, 0, NULL);
    AppendMenuW(menu, MF_STRING, IDM_TAB_CLOSE, LoadStr(IDS_CLOSE_TAB).c_str());
    AppendMenuW(menu, MF_STRING | (m_Tabs.size() > 1 ? 0 : MF_GRAYED), IDM_TAB_CLOSE_OTHERS,
                LoadStr(IDS_CLOSE_OTHER_TABS).c_str());
    AppendMenuW(menu, MF_STRING | (index + 1 < (int)m_Tabs.size() ? 0 : MF_GRAYED), IDM_TAB_CLOSE_RIGHT,
                LoadStr(IDS_CLOSE_TABS_RIGHT).c_str());
    command = TrackPopupMenu(menu, TPM_RETURNCMD, screen.x, screen.y, 0, m_hwnd, NULL);
    DestroyMenu(menu);
    switch (command)
    {
        case IDM_TAB_RENAME:
            ActivateTab(index);
            PerformAction(Action::RenameTab, 0);
            break;
        case IDM_TAB_DUPLICATE:
            ActivateTab(index);
            PerformAction(Action::DuplicateTab, 0);
            break;
        case IDM_TAB_CLOSE:
            CloseTab(index);
            break;
        case IDM_TAB_CLOSE_OTHERS:
            for (int i = (int)m_Tabs.size() - 1; i >= 0; i--)
            {
                if (i != index)
                {
                    CloseTab(i);
                    if (i < index)
                        index--;
                }
            }
            break;
        case IDM_TAB_CLOSE_RIGHT:
            for (int i = (int)m_Tabs.size() - 1; i > index; i--)
                CloseTab(i);
            break;
    }
}

void
TerminalWindow::ToggleFullscreen()
{
    if (!m_Fullscreen)
    {
        MONITORINFO monitor = { sizeof(monitor) };

        GetWindowPlacement(m_hwnd, &m_Placement);
        m_SavedStyle = GetWindowLongPtrW(m_hwnd, GWL_STYLE);
        GetMonitorInfoW(MonitorFromWindow(m_hwnd, MONITOR_DEFAULTTONEAREST), &monitor);
        m_Fullscreen = true;
        SetWindowLongPtrW(m_hwnd, GWL_STYLE, (m_SavedStyle & ~WS_OVERLAPPEDWINDOW) | WS_POPUP);
        SetWindowPos(m_hwnd, HWND_TOP, monitor.rcMonitor.left, monitor.rcMonitor.top,
                     monitor.rcMonitor.right - monitor.rcMonitor.left, monitor.rcMonitor.bottom - monitor.rcMonitor.top,
                     SWP_FRAMECHANGED | SWP_NOOWNERZORDER);
    }
    else
    {
        m_Fullscreen = false;
        SetWindowLongPtrW(m_hwnd, GWL_STYLE, m_SavedStyle);
        SetWindowPlacement(m_hwnd, &m_Placement);
        SetWindowPos(m_hwnd, NULL, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
    }
}

bool
TerminalWindow::ConfirmClose()
{
    if (m_Tabs.size() <= 1 || !g_Settings.ConfirmCloseAllTabs)
        return true;
    return MessageBoxW(m_hwnd, LoadStr(IDS_CLOSE_ALL_TABS).c_str(), LoadStr(IDS_CLOSE_ALL_TABS_TITLE).c_str(),
                       MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2) == IDYES;
}

void
TerminalWindow::ApplySettings()
{
    for (auto &tab : m_Tabs)
    {
        if (!tab->Term)
            continue;
        if (const Profile *profile = g_Settings.FindProfile(tab->Term->Settings().Name))
            tab->Term->SetProfile(*profile);
    }
    ApplyTheme();
    m_View.ApplyFont(m_Theme.Dpi());
    {
        RECT client;
        GetClientRect(m_hwnd, &client);
        OnLayout(client);
    }
    m_SettingsTime = SettingsStore::LastWrite();
    UpdateWindowTitle();
    Invalidate(NULL);
}

void
TerminalWindow::PerformAction(Action command, int argument)
{
    Tab *tab = ActiveTab();

    switch (command)
    {
        case Action::NewTab:
            NewTab(g_Settings.Default(), L"", L"");
            break;
        case Action::NewTabProfile:
        {
            int visible = 0;

            for (const Profile &profile : g_Settings.Profiles)
            {
                if (profile.Hidden)
                    continue;
                if (++visible == argument)
                {
                    NewTab(profile, L"", L"");
                    break;
                }
            }
            break;
        }
        case Action::DuplicateTab:
            if (tab && tab->Term)
                NewTab(tab->Term->Settings(), tab->Term->CommandLine(), tab->Term->Directory());
            break;
        case Action::CloseTab:
            CloseTab(m_Active);
            break;
        case Action::CloseWindow:
            PostMessageW(m_hwnd, WM_CLOSE, 0, 0);
            break;
        case Action::NextTab:
            if (!m_Tabs.empty())
                ActivateTab((m_Active + 1) % (int)m_Tabs.size());
            break;
        case Action::PrevTab:
            if (!m_Tabs.empty())
                ActivateTab((m_Active + (int)m_Tabs.size() - 1) % (int)m_Tabs.size());
            break;
        case Action::SwitchToTab:
            if (argument == 9)
                ActivateTab((int)m_Tabs.size() - 1);
            else
                ActivateTab(argument - 1);
            break;
        case Action::Copy:
            m_View.Copy(true);
            break;
        case Action::Paste:
            m_View.Paste();
            break;
        case Action::SelectAll:
            m_View.SelectAll();
            break;
        case Action::Find:
            ShowFindBar(true);
            break;
        case Action::OpenSettings:
            ShowSettings(m_hwnd);
            break;
        case Action::OpenSettingsFile:
            ShellExecuteW(m_hwnd, L"open", L"notepad.exe", (L"\"" + SettingsStore::FilePath() + L"\"").c_str(), NULL,
                          SW_SHOWNORMAL);
            break;
        case Action::ZoomIn:
        case Action::ZoomOut:
        case Action::ZoomReset:
        {
            RECT client;

            m_View.SetZoom(command == Action::ZoomReset ? 0 : m_View.Zoom() + (command == Action::ZoomIn ? 1 : -1));
            GetClientRect(m_hwnd, &client);
            OnLayout(client);
            break;
        }
        case Action::ToggleFullscreen:
            ToggleFullscreen();
            break;
        case Action::ScrollUp:
            m_View.ScrollBy(-1);
            break;
        case Action::ScrollDown:
            m_View.ScrollBy(1);
            break;
        case Action::ScrollPageUp:
        case Action::ScrollPageDown:
            if (tab && tab->Term)
                m_View.ScrollBy(command == Action::ScrollPageUp ? -tab->Term->Screen().Rows() : tab->Term->Screen().Rows());
            break;
        case Action::ScrollToTop:
            m_View.ScrollToTop();
            break;
        case Action::ScrollToBottom:
            m_View.ScrollToBottom();
            break;
        case Action::ClearBuffer:
            if (tab && tab->Term)
            {
                tab->Term->Screen().ClearScrollback();
                tab->ScrollOffset = 0;
                tab->Select.Active = false;
                tab->Matches.clear();
                m_View.OnOutput(0);
            }
            break;
        case Action::RenameTab:
            if (tab)
            {
                std::wstring title = tab->Title();

                if (PromptText(m_hwnd, LoadStr(IDS_RENAME_TAB), title))
                {
                    tab->CustomTitle = title;
                    InvalidateTabs();
                    UpdateWindowTitle();
                }
            }
            break;
        case Action::About:
            ShowAbout(m_hwnd);
            break;
        case Action::None:
            break;
    }
}

void
TerminalWindow::ShowFindBar(bool show)
{
    m_FindVisible = show;
    if (show && !m_FindEdit)
    {
        m_FindEdit = CreateWindowExW(WS_EX_CLIENTEDGE, WC_EDITW, L"", WS_CHILD | ES_AUTOHSCROLL | WS_TABSTOP, 0, 0, 0, 0,
                                     m_hwnd, (HMENU)(UINT_PTR)FIND_EDIT_ID, g_Instance, NULL);
        m_FindStatus = CreateWindowExW(0, WC_STATICW, L"", WS_CHILD | SS_LEFT | SS_CENTERIMAGE, 0, 0, 0, 0, m_hwnd,
                                       NULL, g_Instance, NULL);
        SendMessageW(m_FindEdit, EM_SETCUEBANNER, TRUE, (LPARAM)LoadStr(IDS_FIND_HINT).c_str());
        SetWindowSubclass(m_FindEdit, FindEditProc, 1, (DWORD_PTR)this);
    }
    if (m_FindEdit)
    {
        if (!m_FindFont)
            m_FindFont = CreateFontW(-MulDiv(9, (int)m_Theme.Dpi(), 72), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                     DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                     DEFAULT_PITCH, L"Segoe UI");
        SendMessageW(m_FindEdit, WM_SETFONT, (WPARAM)m_FindFont, TRUE);
        SendMessageW(m_FindStatus, WM_SETFONT, (WPARAM)m_FindFont, TRUE);
        LayoutFindBar();
        ShowWindow(m_FindEdit, show ? SW_SHOW : SW_HIDE);
        ShowWindow(m_FindStatus, show ? SW_SHOW : SW_HIDE);
        if (show)
        {
            SendMessageW(m_FindEdit, EM_SETSEL, 0, -1);
            SetFocus(m_FindEdit);
        }
        else
        {
            m_View.ClearFind();
            SetFocus(m_View.Window());
        }
    }
}

void
TerminalWindow::LayoutFindBar()
{
    RECT client;
    int width = m_Theme.Scale(260), height = m_Theme.Scale(28), status = m_Theme.Scale(90);
    int top = StripHeight() + m_Theme.Scale(8);

    if (!m_FindEdit || !m_hwnd)
        return;
    GetClientRect(m_hwnd, &client);
    MoveWindow(m_FindEdit, client.right - width - status - m_Theme.Scale(28), top, width, height, TRUE);
    MoveWindow(m_FindStatus, client.right - status - m_Theme.Scale(24), top, status, height, TRUE);
    SetWindowPos(m_FindEdit, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
    SetWindowPos(m_FindStatus, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
}

void
TerminalWindow::UpdateFindStatus()
{
    Tab *tab = ActiveTab();
    wchar_t text[64];

    if (!m_FindStatus || !tab)
        return;
    if (tab->Matches.empty())
        StringCchCopyW(text, ARRAYSIZE(text), tab->SearchText.empty() ? L"" : LoadStr(IDS_FIND_NONE).c_str());
    else
        StringCchPrintfW(text, ARRAYSIZE(text), LoadStr(IDS_FIND_COUNT).c_str(), tab->CurrentMatch + 1,
                         (int)tab->Matches.size());
    SetWindowTextW(m_FindStatus, text);
}

LRESULT CALLBACK
TerminalWindow::FindEditProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam, UINT_PTR id, DWORD_PTR data)
{
    TerminalWindow *self = (TerminalWindow *)data;

    UNREFERENCED_PARAMETER(id);
    if (msg == WM_KEYDOWN)
    {
        if (wParam == VK_ESCAPE)
        {
            self->ShowFindBar(false);
            return 0;
        }
        if (wParam == VK_RETURN)
        {
            wchar_t text[256];

            GetWindowTextW(hwnd, text, ARRAYSIZE(text));
            self->m_View.Find(text, (GetKeyState(VK_SHIFT) & 0x8000) == 0, false);
            self->UpdateFindStatus();
            return 0;
        }
    }
    else if (msg == WM_CHAR && (wParam == L'\r' || wParam == 0x1B))
    {
        return 0;
    }
    return DefSubclassProc(hwnd, msg, wParam, lParam);
}

bool
TerminalWindow::PreTranslate(MSG *msg)
{
    if ((msg->message == WM_KEYDOWN || msg->message == WM_SYSKEYDOWN) && msg->hwnd == m_View.Window())
        return m_View.HandleKey(msg->message, msg->wParam, msg->lParam);
    if (msg->message == WM_KEYDOWN && msg->hwnd == m_FindEdit && (msg->wParam == VK_RETURN || msg->wParam == VK_ESCAPE))
    {
        SendMessageW(m_FindEdit, WM_KEYDOWN, msg->wParam, msg->lParam);
        return true;
    }
    return false;
}

LRESULT
TerminalWindow::HandleMessage(UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
        case WM_SESSION_OUTPUT:
            OnSessionOutput((UINT)wParam);
            return 0;

        case WM_SESSION_EXITED:
            OnSessionExited((UINT)wParam, (DWORD)lParam);
            return 0;

        case WM_SETFOCUS:
            if (m_View.Window())
                SetFocus(m_View.Window());
            return 0;

        case WM_ACTIVATE:
            if (LOWORD(wParam) != WA_INACTIVE && m_View.Window())
                SetFocus(m_View.Window());
            break;

        case WM_COMMAND:
            if (LOWORD(wParam) == FIND_EDIT_ID && HIWORD(wParam) == EN_CHANGE)
            {
                wchar_t text[256];

                GetWindowTextW(m_FindEdit, text, ARRAYSIZE(text));
                m_View.Find(text, true, true);
                UpdateFindStatus();
                return 0;
            }
            break;

        case WM_CTLCOLORSTATIC:
            if ((HWND)lParam == m_FindStatus)
            {
                HDC hdc = (HDC)wParam;
                SetBkColor(hdc, SchemeBackground());
                SetTextColor(hdc, g_Settings.SchemeFor(ActiveTab() && ActiveTab()->Term ? ActiveTab()->Term->Settings() :
                                                       g_Settings.Default()).Foreground);
                SetDCBrushColor(hdc, SchemeBackground());
                return (LRESULT)GetStockObject(DC_BRUSH);
            }
            break;

        case WM_TIMER:
            if (wParam == TIMER_SETTINGS)
            {
                FILETIME now = SettingsStore::LastWrite();

                if (CompareFileTime(&now, &m_SettingsTime) != 0)
                {
                    std::wstring error;

                    m_SettingsTime = now;
                    g_Settings = SettingsStore::Load(&error);
                    ApplySettings();
                }
                return 0;
            }
            break;

        case WM_CLOSE:
            if (!ConfirmClose())
                return 0;
            break;

        case WM_DESTROY:
            KillTimer(m_hwnd, TIMER_SETTINGS);
            m_View.SetTab(NULL);
            m_Tabs.clear();
            PostQuitMessage(0);
            break;
    }
    return FrameWindow::HandleMessage(msg, wParam, lParam);
}
