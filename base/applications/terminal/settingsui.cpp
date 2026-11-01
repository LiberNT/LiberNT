/*
 * PROJECT:     LiberNT Terminal
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Settings window: section tabs for profiles, colors, window options and keys
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "terminal.h"

#include <algorithm>

#define SETTINGS_CLASS L"LiberNT.TerminalSettings"
#define PAGE_CLASS L"LiberNT.TerminalSettingsPage"

namespace
{

enum ControlId
{
    IdName = 1000,
    IdDuplicateProfile,
    IdDeleteProfile,
    IdCommand,
    IdBrowse,
    IdDirectory,
    IdTabTitle,
    IdFont,
    IdFontSize,
    IdCursor,
    IdHistory,
    IdPadding,
    IdOnExit,
    IdSchemeName,
    IdDuplicateScheme,
    IdDeleteScheme,
    IdDefaultProfile,
    IdTheme,
    IdCols,
    IdRows,
    IdDelimiters,
    IdKeyList,
    IdKeys,
    IdAssign,
    IdRemove,
    IdReset,
    IdSave,
    IdCancel,
    IdOpenJson
};

enum class Check
{
    Bold,
    Blink,
    KeepTitle,
    BellSound,
    Hidden,
    CopyOnSelect,
    ConfirmClose,
    BellMark
};

struct TextItem
{
    UINT Text;
    RECT Bounds;
};

struct CheckItem
{
    Check Kind;
    UINT Text;
    RECT Bounds;
};

class SettingsWindow : public ShellFrame::FrameWindow
{
public:
    explicit SettingsWindow(HWND owner);
    ~SettingsWindow();

    HWND Frame() const { return m_hwnd; }
    void ShowSection(int section);
    int Section() const { return m_Section; }

protected:
    LRESULT HandleMessage(UINT msg, WPARAM wParam, LPARAM lParam) override;
    void OnCreate() override;
    void OnLayout(const RECT &client) override;
    void OnPaint(ShellFrame::Canvas &canvas, const RECT &client) override;
    void OnThemeChanged() override;
    int CaptionHeight() const override { return m_Theme.Scale(48); }
    int GlassWidth() const override { return PanelLeft() + m_Theme.Scale(8); }
    int GlassTop() const override { return CaptionHeight() + m_Theme.Scale(8); }

private:
    static LRESULT CALLBACK PageProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    LRESULT HandlePage(UINT msg, WPARAM wParam, LPARAM lParam);

    int S(int value) const { return m_Theme.Scale(value); }
    int PanelLeft() const { return m_Theme.Scale(200); }
    HWND AddControl(LPCWSTR className, DWORD style, int id, UINT text = 0);
    void CreateControls();
    void ApplyFonts();
    void LayoutFrame();
    int LayoutSection(int section, bool apply);
    void PaintPage(HDC hdc);
    void PaintPreview(ShellFrame::Canvas &canvas);
    void OnCommand(int id, int code);
    void OnClick(POINT pt);
    void OnHover(POINT pt);
    void SetHotRail(int index);

    Profile *CurrentProfile();
    ColorScheme *CurrentScheme();
    bool *CheckValue(Check kind);
    COLORREF *SwatchColor(int index);
    void LoadProfile();
    void StoreProfile();
    void LoadScheme();
    void LoadWindowOptions();
    void StoreWindowOptions();
    void FillProfileCombo();
    void FillKeyList();
    void InvalidatePreview();
    bool Save();

    HWND m_Owner;
    HWND m_Page = NULL;
    Settings m_Edit;
    int m_Profile = 0;
    int m_Scheme = 0;
    int m_Section = 0;
    int m_HotRail = -1;
    bool m_Loading = false;
    int m_HotChip = -1;
    int m_HotTile = -1;
    bool m_Tracking = false;
    bool m_TrackingFrame = false;
    std::vector<std::wstring> m_Fonts;
    std::vector<RECT> m_Rail;
    std::vector<TextItem> m_Labels;
    std::vector<TextItem> m_Notes;
    std::vector<CheckItem> m_Checks;
    std::vector<RECT> m_Chips;
    std::vector<RECT> m_Tiles;
    RECT m_Swatches[20] = {};
    RECT m_Preview = {};
    RECT m_Footer = {};
    COLORREF m_CustomColors[16] = {};
};

OwnedPtr<SettingsWindow> g_SettingsWindow;

const UINT SectionNames[] = { IDS_SET_SECTION_PROFILES, IDS_SET_SECTION_COLORS, IDS_SET_SECTION_WINDOW, IDS_SET_SECTION_KEYS };
const LPCWSTR SectionIcons[] = { L"window_console_20_regular", L"color_20_regular", L"window_multiple_20_regular",
                                 L"keyboard_20_regular" };

int ControlSection(int id)
{
    if (id >= IdName && id <= IdOnExit)
        return 0;
    if (id >= IdSchemeName && id <= IdDeleteScheme)
        return 1;
    if (id >= IdDefaultProfile && id <= IdDelimiters)
        return 2;
    if (id >= IdKeyList && id <= IdReset)
        return 3;
    return -1;
}

std::wstring ControlText(HWND hwnd)
{
    int length = GetWindowTextLengthW(hwnd);
    std::wstring text(length + 1, L'\0');

    GetWindowTextW(hwnd, &text[0], length + 1);
    text.resize(length);
    return text;
}

int ControlNumber(HWND hwnd, int fallback, int low, int high)
{
    std::wstring text = ControlText(hwnd);
    wchar_t *end = NULL;
    long value = wcstol(text.c_str(), &end, 10);

    if (text.empty() || (end && *end))
        return fallback;
    return value < low ? low : value > high ? high : (int)value;
}

std::wstring ComboText(HWND combo)
{
    int index = (int)SendMessageW(combo, CB_GETCURSEL, 0, 0);
    LRESULT length = index >= 0 ? SendMessageW(combo, CB_GETLBTEXTLEN, index, 0) : CB_ERR;
    std::wstring text;

    if (length == CB_ERR)
        return ControlText(combo);
    text.resize(length + 1);
    SendMessageW(combo, CB_GETLBTEXT, index, (LPARAM)&text[0]);
    text.resize(length);
    return text;
}

void FillIndexedCombo(HWND combo, const UINT *ids, int count, int selected)
{
    SendMessageW(combo, CB_RESETCONTENT, 0, 0);
    for (int i = 0; i < count; i++)
        SendMessageW(combo, CB_ADDSTRING, 0, (LPARAM)LoadStr(ids[i]).c_str());
    SendMessageW(combo, CB_SETCURSEL, selected, 0);
}

int CALLBACK FontCallback(const LOGFONTW *font, const TEXTMETRICW *metrics, DWORD type, LPARAM lParam)
{
    std::vector<std::wstring> *names = (std::vector<std::wstring> *)lParam;

    UNREFERENCED_PARAMETER(metrics);
    UNREFERENCED_PARAMETER(type);
    if ((font->lfPitchAndFamily & 0x03) == FIXED_PITCH && font->lfFaceName[0] != L'@')
    {
        std::wstring name = font->lfFaceName;
        bool known = false;

        for (const std::wstring &existing : *names)
            known = known || existing == name;
        if (!known)
            names->push_back(name);
    }
    return 1;
}

SettingsWindow::SettingsWindow(HWND owner) :
    m_Owner(owner),
    m_Edit(g_Settings)
{
    WNDCLASSEXW wc = { sizeof(wc) };
    HINSTANCE instance = GetModuleHandleW(NULL);
    HICON icon = (HICON)LoadImageW(g_Instance, MAKEINTRESOURCEW(IDI_TERMINAL), IMAGE_ICON, 0, 0, LR_DEFAULTSIZE | LR_SHARED);
    RECT area, ownerRect, frame;
    LONG_PTR style;
    int content = 0, width, height;

    if (!GetClassInfoExW(instance, PAGE_CLASS, &wc))
    {
        wc.cbSize = sizeof(wc);
        wc.style = CS_DBLCLKS;
        wc.lpfnWndProc = PageProc;
        wc.hInstance = instance;
        wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
        wc.lpszClassName = PAGE_CLASS;
        RegisterClassExW(&wc);
    }

    for (size_t i = 0; i < m_Edit.Profiles.size(); i++)
    {
        if (m_Edit.Profiles[i].Name == m_Edit.Default().Name)
            m_Profile = (int)i;
    }
    {
        LOGFONTW query = {};
        HDC hdc = GetDC(NULL);

        query.lfCharSet = DEFAULT_CHARSET;
        EnumFontFamiliesExW(hdc, &query, (FONTENUMPROCW)FontCallback, (LPARAM)&m_Fonts, 0);
        ReleaseDC(NULL, hdc);
    }

    m_Theme.SetDarkPreference(m_Edit.Theme == ThemeMode::Dark ? 1 : m_Edit.Theme == ThemeMode::Light ? 0 : -1);
    if (!Create(SETTINGS_CLASS, LoadStr(IDS_SETTINGS_WINDOW).c_str(), icon, 800, 600, SW_HIDE))
        return;
    SetWindowLongPtrW(m_hwnd, GWLP_HWNDPARENT, (LONG_PTR)owner);
    style = GetWindowLongPtrW(m_hwnd, GWL_STYLE) & ~(LONG_PTR)(WS_THICKFRAME | WS_MAXIMIZEBOX);
    SetWindowLongPtrW(m_hwnd, GWL_STYLE, style);
    for (int i = 0; i < (int)ARRAYSIZE(SectionNames); i++)
        content = std::max<LONG>(content, LayoutSection(i, false));
    frame.left = frame.top = 0;
    frame.right = PanelLeft() + S(24) + S(540) + S(24);
    frame.bottom = CaptionHeight() + S(8) + content + S(56);
    AdjustWindowRectExForDpi(&frame, (DWORD)style, FALSE, 0, m_Theme.Dpi());
    width = frame.right - frame.left;
    height = ClientCaption() ? frame.bottom : frame.bottom - frame.top;
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &area, 0);
    if (!owner || !GetWindowRect(owner, &ownerRect))
        ownerRect = area;
    SetWindowPos(m_hwnd, NULL, std::max<LONG>(area.left, (ownerRect.left + ownerRect.right - width) / 2),
                 std::max<LONG>(area.top, (ownerRect.top + ownerRect.bottom - height) / 2), width, height,
                 SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
    LayoutSection(m_Section, true);
    ShowWindow(m_hwnd, SW_SHOWNORMAL);
}

SettingsWindow::~SettingsWindow()
{
    if (m_hwnd)
    {
        HWND hwnd = m_hwnd;

        if (m_Page)
            SetWindowLongPtrW(m_Page, GWLP_USERDATA, 0);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
        m_hwnd = NULL;
        DestroyWindow(hwnd);
    }
}

LRESULT CALLBACK
SettingsWindow::PageProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    SettingsWindow *self;

    if (msg == WM_NCCREATE)
    {
        self = (SettingsWindow *)((CREATESTRUCTW *)lParam)->lpCreateParams;
        self->m_Page = hwnd;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)self);
    }
    self = (SettingsWindow *)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    return self ? self->HandlePage(msg, wParam, lParam) : DefWindowProcW(hwnd, msg, wParam, lParam);
}

HWND
SettingsWindow::AddControl(LPCWSTR className, DWORD style, int id, UINT text)
{
    bool footer = id == IdSave || id == IdCancel || id == IdOpenJson;
    DWORD exStyle = !lstrcmpW(className, WC_EDITW) || !lstrcmpW(className, WC_LISTVIEWW) ? WS_EX_CLIENTEDGE : 0;

    return CreateWindowExW(exStyle, className, text ? LoadStr(text).c_str() : L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | style,
                           0, 0, 0, 0, footer ? m_hwnd : m_Page, (HMENU)(INT_PTR)id, GetModuleHandleW(NULL), NULL);
}

void
SettingsWindow::CreateControls()
{
    static const UINT Cursors[] = { IDS_CURSOR_BAR, IDS_CURSOR_BLOCK, IDS_CURSOR_UNDERLINE };
    static const UINT Exits[] = { IDS_EXIT_GRACEFUL, IDS_EXIT_ALWAYS, IDS_EXIT_NEVER };
    static const UINT Themes[] = { IDS_THEME_SYSTEM, IDS_THEME_LIGHT, IDS_THEME_DARK };
    HWND list, fonts;
    LVCOLUMNW column = {};
    std::wstring action = LoadStr(IDS_COLUMN_ACTION), keys = LoadStr(IDS_COLUMN_KEYS);

    AddControl(WC_EDITW, ES_AUTOHSCROLL, IdName);
    AddControl(WC_BUTTONW, BS_PUSHBUTTON, IdDuplicateProfile, IDS_SET_DUPLICATE);
    AddControl(WC_BUTTONW, BS_PUSHBUTTON, IdDeleteProfile, IDS_SET_DELETE);
    AddControl(WC_EDITW, ES_AUTOHSCROLL, IdCommand);
    AddControl(WC_BUTTONW, BS_PUSHBUTTON, IdBrowse, IDS_SET_BROWSE);
    AddControl(WC_EDITW, ES_AUTOHSCROLL, IdDirectory);
    AddControl(WC_EDITW, ES_AUTOHSCROLL, IdTabTitle);
    fonts = AddControl(WC_COMBOBOXW, CBS_DROPDOWN | CBS_SORT | WS_VSCROLL, IdFont);
    for (const std::wstring &font : m_Fonts)
        SendMessageW(fonts, CB_ADDSTRING, 0, (LPARAM)font.c_str());
    AddControl(WC_EDITW, ES_NUMBER, IdFontSize);
    FillIndexedCombo(AddControl(WC_COMBOBOXW, CBS_DROPDOWNLIST, IdCursor), Cursors, 3, 0);
    AddControl(WC_EDITW, ES_NUMBER, IdHistory);
    AddControl(WC_EDITW, ES_NUMBER, IdPadding);
    FillIndexedCombo(AddControl(WC_COMBOBOXW, CBS_DROPDOWNLIST, IdOnExit), Exits, 3, 0);
    AddControl(WC_EDITW, ES_AUTOHSCROLL, IdSchemeName);
    AddControl(WC_BUTTONW, BS_PUSHBUTTON, IdDuplicateScheme, IDS_SET_DUPLICATE);
    AddControl(WC_BUTTONW, BS_PUSHBUTTON, IdDeleteScheme, IDS_SET_DELETE);
    AddControl(WC_COMBOBOXW, CBS_DROPDOWNLIST | WS_VSCROLL, IdDefaultProfile);
    FillIndexedCombo(AddControl(WC_COMBOBOXW, CBS_DROPDOWNLIST, IdTheme), Themes, 3, (int)m_Edit.Theme);
    AddControl(WC_EDITW, ES_NUMBER, IdCols);
    AddControl(WC_EDITW, ES_NUMBER, IdRows);
    AddControl(WC_EDITW, ES_AUTOHSCROLL, IdDelimiters);
    list = AddControl(WC_LISTVIEWW, LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS | LVS_NOSORTHEADER, IdKeyList);
    ListView_SetExtendedListViewStyle(list, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);
    column.mask = LVCF_TEXT | LVCF_WIDTH;
    column.cx = S(300);
    column.pszText = (LPWSTR)action.c_str();
    ListView_InsertColumn(list, 0, &column);
    column.cx = S(200);
    column.pszText = (LPWSTR)keys.c_str();
    ListView_InsertColumn(list, 1, &column);
    AddControl(WC_EDITW, ES_AUTOHSCROLL, IdKeys);
    AddControl(WC_BUTTONW, BS_PUSHBUTTON, IdAssign, IDS_SET_ASSIGN);
    AddControl(WC_BUTTONW, BS_PUSHBUTTON, IdRemove, IDS_SET_REMOVE);
    AddControl(WC_BUTTONW, BS_PUSHBUTTON, IdReset, IDS_SET_RESET);
    AddControl(WC_BUTTONW, BS_PUSHBUTTON, IdOpenJson, IDS_SET_OPEN_JSON);
    AddControl(WC_BUTTONW, BS_PUSHBUTTON, IdCancel, IDS_SET_CANCEL);
    AddControl(WC_BUTTONW, BS_DEFPUSHBUTTON, IdSave, IDS_SET_SAVE);

    ApplyFonts();
    FillProfileCombo();
    FillKeyList();
    LoadWindowOptions();
    LoadProfile();
}

void
SettingsWindow::ApplyFonts()
{
    HFONT font = m_Theme.Font(ShellFrame::TextStyle::Body);
    HWND parents[] = { m_Page, m_hwnd };

    for (HWND parent : parents)
    {
        for (HWND child = GetWindow(parent, GW_CHILD); child; child = GetWindow(child, GW_HWNDNEXT))
            SendMessageW(child, WM_SETFONT, (WPARAM)font, TRUE);
    }
}

Profile *
SettingsWindow::CurrentProfile()
{
    return m_Profile >= 0 && m_Profile < (int)m_Edit.Profiles.size() ? &m_Edit.Profiles[m_Profile] : NULL;
}

ColorScheme *
SettingsWindow::CurrentScheme()
{
    return m_Scheme >= 0 && m_Scheme < (int)m_Edit.Schemes.size() ? &m_Edit.Schemes[m_Scheme] : NULL;
}

bool *
SettingsWindow::CheckValue(Check kind)
{
    Profile *profile = CurrentProfile();

    switch (kind)
    {
        case Check::Bold: return profile ? &profile->FontBold : NULL;
        case Check::Blink: return profile ? &profile->CursorBlink : NULL;
        case Check::KeepTitle: return profile ? &profile->SuppressApplicationTitle : NULL;
        case Check::BellSound: return profile ? &profile->BellSound : NULL;
        case Check::Hidden: return profile ? &profile->Hidden : NULL;
        case Check::CopyOnSelect: return &m_Edit.CopyOnSelect;
        case Check::ConfirmClose: return &m_Edit.ConfirmCloseAllTabs;
        case Check::BellMark: return &m_Edit.ShowBellIndicator;
    }
    return NULL;
}

COLORREF *
SettingsWindow::SwatchColor(int index)
{
    ColorScheme *scheme = CurrentScheme();

    if (!scheme)
        return NULL;
    switch (index)
    {
        case 0: return &scheme->Foreground;
        case 1: return &scheme->Background;
        case 2: return &scheme->Cursor;
        case 3: return &scheme->Selection;
        default: return &scheme->Table[index - 4];
    }
}

void
SettingsWindow::LoadProfile()
{
    Profile *profile = CurrentProfile();
    int cursor;

    if (!profile)
        return;
    m_Loading = true;
    cursor = profile->Cursor == Vt::CursorShape::Block ? 1 : profile->Cursor == Vt::CursorShape::Underline ? 2 : 0;
    SetDlgItemTextW(m_Page, IdName, profile->Name.c_str());
    SetDlgItemTextW(m_Page, IdCommand, profile->CommandLine.c_str());
    SetDlgItemTextW(m_Page, IdDirectory, profile->StartingDirectory.c_str());
    SetDlgItemTextW(m_Page, IdTabTitle, profile->TabTitle.c_str());
    SetDlgItemTextW(m_Page, IdFont, profile->FontFace.c_str());
    SetDlgItemInt(m_Page, IdFontSize, profile->FontSize, FALSE);
    SendDlgItemMessageW(m_Page, IdCursor, CB_SETCURSEL, cursor, 0);
    SetDlgItemInt(m_Page, IdHistory, profile->ScrollbackLines, FALSE);
    SetDlgItemInt(m_Page, IdPadding, profile->Padding, FALSE);
    SendDlgItemMessageW(m_Page, IdOnExit, CB_SETCURSEL, (WPARAM)profile->OnExit, 0);
    EnableWindow(GetDlgItem(m_Page, IdDeleteProfile), m_Edit.Profiles.size() > 1);
    m_Scheme = 0;
    for (size_t i = 0; i < m_Edit.Schemes.size(); i++)
    {
        if (!_wcsicmp(m_Edit.Schemes[i].Name.c_str(), m_Edit.SchemeFor(*profile).Name.c_str()))
            m_Scheme = (int)i;
    }
    m_Loading = false;
    LoadScheme();
    InvalidateRect(m_Page, NULL, FALSE);
}

void
SettingsWindow::StoreProfile()
{
    Profile *profile = CurrentProfile();
    std::wstring oldName;
    int cursor;

    if (!profile || m_Loading)
        return;
    oldName = profile->Name;
    profile->Name = ControlText(GetDlgItem(m_Page, IdName));
    profile->CommandLine = ControlText(GetDlgItem(m_Page, IdCommand));
    profile->StartingDirectory = ControlText(GetDlgItem(m_Page, IdDirectory));
    profile->TabTitle = ControlText(GetDlgItem(m_Page, IdTabTitle));
    profile->FontFace = ControlText(GetDlgItem(m_Page, IdFont));
    profile->FontSize = ControlNumber(GetDlgItem(m_Page, IdFontSize), profile->FontSize, 6, 72);
    cursor = (int)SendDlgItemMessageW(m_Page, IdCursor, CB_GETCURSEL, 0, 0);
    profile->Cursor = cursor == 1 ? Vt::CursorShape::Block : cursor == 2 ? Vt::CursorShape::Underline : Vt::CursorShape::Bar;
    profile->ScrollbackLines = ControlNumber(GetDlgItem(m_Page, IdHistory), profile->ScrollbackLines, 0, 1000000);
    profile->Padding = ControlNumber(GetDlgItem(m_Page, IdPadding), profile->Padding, 0, 64);
    profile->OnExit = (CloseOnExit)SendDlgItemMessageW(m_Page, IdOnExit, CB_GETCURSEL, 0, 0);
    if (oldName != profile->Name)
    {
        if (!_wcsicmp(m_Edit.DefaultProfile.c_str(), oldName.c_str()))
            m_Edit.DefaultProfile = profile->Name;
        FillProfileCombo();
        LayoutSection(m_Section, true);
        InvalidateRect(m_Page, NULL, FALSE);
    }
    InvalidatePreview();
}

void
SettingsWindow::LoadScheme()
{
    ColorScheme *scheme = CurrentScheme();

    m_Loading = true;
    SetDlgItemTextW(m_Page, IdSchemeName, scheme ? scheme->Name.c_str() : L"");
    EnableWindow(GetDlgItem(m_Page, IdDeleteScheme), m_Edit.Schemes.size() > 1);
    m_Loading = false;
}

void
SettingsWindow::LoadWindowOptions()
{
    m_Loading = true;
    SendDlgItemMessageW(m_Page, IdTheme, CB_SETCURSEL, (WPARAM)m_Edit.Theme, 0);
    SetDlgItemInt(m_Page, IdCols, m_Edit.InitialCols, FALSE);
    SetDlgItemInt(m_Page, IdRows, m_Edit.InitialRows, FALSE);
    SetDlgItemTextW(m_Page, IdDelimiters, m_Edit.WordDelimiters.c_str());
    m_Loading = false;
}

void
SettingsWindow::StoreWindowOptions()
{
    if (m_Loading)
        return;
    m_Edit.DefaultProfile = ComboText(GetDlgItem(m_Page, IdDefaultProfile));
    m_Edit.Theme = (ThemeMode)SendDlgItemMessageW(m_Page, IdTheme, CB_GETCURSEL, 0, 0);
    m_Edit.InitialCols = ControlNumber(GetDlgItem(m_Page, IdCols), m_Edit.InitialCols, 20, 1000);
    m_Edit.InitialRows = ControlNumber(GetDlgItem(m_Page, IdRows), m_Edit.InitialRows, 5, 500);
    m_Edit.WordDelimiters = ControlText(GetDlgItem(m_Page, IdDelimiters));
}

void
SettingsWindow::FillProfileCombo()
{
    HWND combo = GetDlgItem(m_Page, IdDefaultProfile);
    bool loading = m_Loading;

    m_Loading = true;
    SendMessageW(combo, CB_RESETCONTENT, 0, 0);
    for (const Profile &profile : m_Edit.Profiles)
        SendMessageW(combo, CB_ADDSTRING, 0, (LPARAM)profile.Name.c_str());
    SendMessageW(combo, CB_SELECTSTRING, (WPARAM)-1, (LPARAM)m_Edit.Default().Name.c_str());
    m_Loading = loading;
}

void
SettingsWindow::FillKeyList()
{
    HWND list = GetDlgItem(m_Page, IdKeyList);

    ListView_DeleteAllItems(list);
    for (size_t i = 0; i < m_Edit.Bindings.size(); i++)
    {
        const KeyBinding &binding = m_Edit.Bindings[i];
        /* TODO: show readable action names instead of the settings.json identifiers */
        std::wstring name = SettingsStore::ActionName(binding.Command);
        std::wstring keys = SettingsStore::FormatKeys(binding);
        LVITEMW item = {};
        wchar_t number[16];

        if (binding.Argument)
        {
            StringCchPrintfW(number, ARRAYSIZE(number), L" %d", binding.Argument);
            name += number;
        }
        item.mask = LVIF_TEXT | LVIF_PARAM;
        item.iItem = (int)i;
        item.pszText = (LPWSTR)name.c_str();
        item.lParam = (LPARAM)i;
        ListView_InsertItem(list, &item);
        ListView_SetItemText(list, (int)i, 1, (LPWSTR)keys.c_str());
    }
}

void
SettingsWindow::InvalidatePreview()
{
    InvalidateRect(m_Page, &m_Preview, FALSE);
}

bool
SettingsWindow::Save()
{
    std::wstring error;

    StoreProfile();
    StoreWindowOptions();
    if (!SettingsStore::Save(m_Edit, &error))
    {
        MessageBoxW(m_hwnd, error.c_str(), LoadStr(IDS_SETTINGS_SAVE_FAILED).c_str(), MB_OK | MB_ICONERROR);
        return false;
    }
    g_Settings = m_Edit;
    BroadcastSettingsChanged();
    return true;
}

void
SettingsWindow::LayoutFrame()
{
    RECT client;
    int footer = S(56), button = S(96), height = S(30), panel = PanelLeft(), top;

    GetClientRect(m_hwnd, &client);
    m_Footer = { panel, client.bottom - footer, client.right, client.bottom };
    top = m_Footer.top + (footer - height) / 2;
    m_Rail.clear();
    for (int i = 0; i < (int)ARRAYSIZE(SectionNames); i++)
    {
        int itemTop = CaptionHeight() + S(4) + i * S(40);

        m_Rail.push_back({ S(12), itemTop, panel - S(12), itemTop + S(40) });
    }
    if (m_Page)
        MoveWindow(m_Page, panel, CaptionHeight() + S(8), client.right - panel,
                   std::max<LONG>(0, m_Footer.top - CaptionHeight() - S(8)), TRUE);
    MoveWindow(GetDlgItem(m_hwnd, IdOpenJson), panel + S(24), top, S(150), height, TRUE);
    MoveWindow(GetDlgItem(m_hwnd, IdCancel), client.right - S(24) - 2 * button - S(8), top, button, height, TRUE);
    MoveWindow(GetDlgItem(m_hwnd, IdSave), client.right - S(24) - button, top, button, height, TRUE);
    Invalidate(NULL);
}

int
SettingsWindow::LayoutSection(int section, bool apply)
{
    HDC hdc;
    HGDIOBJ oldFont;
    HDWP defer = apply ? BeginDeferWindowPos(32) : NULL;
    int margin = S(24), left = margin, width = S(540), label = S(176), row = S(28), gap = S(10);
    int button = S(96), x = left + label, field = width - label, right = left + width, y = margin;

    m_Labels.clear();
    m_Notes.clear();
    m_Checks.clear();
    m_Chips.clear();
    m_Tiles.clear();
    SetRectEmpty(&m_Preview);
    for (RECT &rc : m_Swatches)
        SetRectEmpty(&rc);
    auto place = [&](int id, int l, int t, int w, int h)
    {
        if (defer)
            defer = DeferWindowPos(defer, GetDlgItem(m_Page, id), NULL, l, t, w, h,
                                   SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOCOPYBITS | SWP_SHOWWINDOW);
    };
    auto labelRow = [&](UINT text)
    {
        m_Labels.push_back({ text, { left, y, x - S(12), y + row } });
    };
    auto check = [&](Check kind, UINT text, int l)
    {
        RECT rc = { l, y, l + S(320), y + row };
        m_Checks.push_back({ kind, text, rc });
    };

    if (section == 0 || section == 1)
    {
        m_Preview = { left, y, right, y + S(104) };
        y += S(104) + S(18);
    }

    switch (section)
    {
        case 0:
            hdc = GetDC(m_Page);
            oldFont = SelectObject(hdc, m_Theme.Font(ShellFrame::TextStyle::Body));
            {
                int chipLeft = left;

                for (size_t i = 0; i <= m_Edit.Profiles.size(); i++)
                {
                    std::wstring text = i < m_Edit.Profiles.size() ? m_Edit.Profiles[i].Name : LoadStr(IDS_SET_ADD);
                    SIZE extent = {};
                    int chip;

                    GetTextExtentPoint32W(hdc, text.c_str(), (int)text.length(), &extent);
                    chip = extent.cx + S(32) + (i == m_Edit.Profiles.size() ? S(20) : 0);
                    if (chipLeft > left && chipLeft + chip > right)
                    {
                        chipLeft = left;
                        y += S(32) + S(8);
                    }
                    m_Chips.push_back({ chipLeft, y, chipLeft + chip, y + S(32) });
                    chipLeft += chip + S(8);
                }
                y += S(32) + S(16);
            }
            SelectObject(hdc, oldFont);
            ReleaseDC(m_Page, hdc);

            labelRow(IDS_SET_NAME);
            place(IdName, x, y, field - 2 * (button + S(8)), row);
            place(IdDuplicateProfile, x + field - 2 * button - S(8), y, button, row);
            place(IdDeleteProfile, x + field - button, y, button, row);
            y += row + gap;
            labelRow(IDS_SET_COMMAND);
            place(IdCommand, x, y, field - button - S(8), row);
            place(IdBrowse, x + field - button, y, button, row);
            y += row + gap;
            labelRow(IDS_SET_DIRECTORY);
            place(IdDirectory, x, y, field, row);
            y += row + gap;
            labelRow(IDS_SET_TAB_TITLE);
            place(IdTabTitle, x, y, field, row);
            y += row + gap;
            labelRow(IDS_SET_FONT);
            place(IdFont, x, y, field - S(64) - S(8) - S(100), S(300));
            place(IdFontSize, x + field - S(64) - S(100), y, S(64), row);
            check(Check::Bold, IDS_SET_BOLD, x + field - S(92));
            m_Checks.back().Bounds.right = x + field;
            y += row + gap;
            labelRow(IDS_SET_CURSOR);
            place(IdCursor, x, y, S(180), S(200));
            check(Check::Blink, IDS_SET_BLINK, x + S(196));
            y += row + gap;
            labelRow(IDS_SET_HISTORY);
            place(IdHistory, x, y, S(100), row);
            m_Notes.push_back({ IDS_SET_PADDING, { x + S(120), y, x + S(220), y + row } });
            place(IdPadding, x + S(220), y, S(64), row);
            y += row + gap;
            labelRow(IDS_SET_ON_EXIT);
            place(IdOnExit, x, y, field, S(200));
            y += row + gap;
            check(Check::KeepTitle, IDS_SET_KEEP_TITLE, x);
            y += row;
            check(Check::BellSound, IDS_SET_BELL_SOUND, x);
            y += row;
            check(Check::Hidden, IDS_SET_HIDDEN, x);
            y += row;
            break;

        case 1:
        {
            static const UINT Names[] = { IDS_COLOR_FOREGROUND, IDS_COLOR_BACKGROUND, IDS_COLOR_CURSOR, IDS_COLOR_SELECTION };
            int tileWidth = S(164), tileHeight = S(72), tileLeft = left, swatch = S(26), step = S(32);

            for (size_t i = 0; i < m_Edit.Schemes.size(); i++)
            {
                if (tileLeft > left && tileLeft + tileWidth > right)
                {
                    tileLeft = left;
                    y += tileHeight + S(10);
                }
                m_Tiles.push_back({ tileLeft, y, tileLeft + tileWidth, y + tileHeight });
                tileLeft += tileWidth + S(14);
            }
            y += tileHeight + S(16);
            labelRow(IDS_SET_SCHEME_NAME);
            place(IdSchemeName, x, y, field - 2 * (button + S(8)), row);
            place(IdDuplicateScheme, x + field - 2 * button - S(8), y, button, row);
            place(IdDeleteScheme, x + field - button, y, button, row);
            y += row + gap;
            labelRow(IDS_SET_SCHEME_COLORS);
            for (int i = 0; i < 4; i++)
            {
                int l = x + i * S(92);

                m_Swatches[i] = { l, y, l + swatch, y + swatch };
                m_Notes.push_back({ Names[i], { l - S(20), y + swatch + S(2), l + swatch + S(60), y + swatch + S(22) } });
            }
            y += swatch + S(30);
            labelRow(IDS_SET_PALETTE);
            for (int i = 0; i < 16; i++)
            {
                int l = x + (i % 8) * step, t = y + (i / 8) * step;

                m_Swatches[4 + i] = { l, t, l + swatch, t + swatch };
            }
            y += step + swatch;
            break;
        }

        case 2:
            labelRow(IDS_SET_DEFAULT_PROFILE);
            place(IdDefaultProfile, x, y, field, S(240));
            y += row + gap;
            labelRow(IDS_SET_THEME);
            place(IdTheme, x, y, field, S(200));
            y += row + gap;
            labelRow(IDS_SET_WINDOW_SIZE);
            place(IdCols, x, y, S(64), row);
            m_Notes.push_back({ IDS_SET_COLUMNS, { x + S(72), y, x + S(150), y + row } });
            place(IdRows, x + S(150), y, S(64), row);
            m_Notes.push_back({ IDS_SET_ROWS, { x + S(222), y, x + S(300), y + row } });
            y += row + gap;
            labelRow(IDS_SET_WORD_DELIMITERS);
            place(IdDelimiters, x, y, field, row);
            y += row + gap;
            check(Check::CopyOnSelect, IDS_SET_COPY_ON_SELECT, x);
            y += row;
            check(Check::ConfirmClose, IDS_SET_CONFIRM_CLOSE, x);
            y += row;
            check(Check::BellMark, IDS_SET_BELL_MARK, x);
            y += row;
            break;

        case 3:
            place(IdKeyList, left, y, width, S(400));
            y += S(400) + gap;
            place(IdKeys, left, y, width - 3 * (button + S(8)), row);
            place(IdAssign, right - 3 * button - 2 * S(8), y, button, row);
            place(IdRemove, right - 2 * button - S(8), y, button, row);
            place(IdReset, right - button, y, button, row);
            y += row;
            break;
    }

    if (defer)
    {
        for (HWND child = GetWindow(m_Page, GW_CHILD); child; child = GetWindow(child, GW_HWNDNEXT))
        {
            if (ControlSection((int)GetWindowLongPtrW(child, GWLP_ID)) != section && defer)
                defer = DeferWindowPos(defer, child, NULL, 0, 0, 0, 0,
                                       SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOMOVE | SWP_NOSIZE | SWP_HIDEWINDOW);
        }
        if (defer)
            EndDeferWindowPos(defer);
    }
    for (TextItem &item : m_Notes)
        m_Labels.push_back(item);
    return y + margin;
}

void
SettingsWindow::ShowSection(int section)
{
    if (section < 0 || section >= (int)ARRAYSIZE(SectionNames))
        return;
    if (m_Page)
        SetFocus(m_Page);
    m_Section = section;
    LayoutSection(m_Section, true);
    RedrawWindow(m_Page, NULL, NULL, RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_UPDATENOW);
}

void
SettingsWindow::OnPaint(ShellFrame::Canvas &canvas, const RECT &client)
{
    const ShellFrame::Palette &c = m_Theme.Colors();
    RECT glass = { 0, 0, GlassWidth(), client.bottom }, caption = { 0, 0, client.right, CaptionHeight() };
    RECT title = { S(16), 0, PanelLeft(), CaptionHeight() };
    COLORREF ink = Glass(ShellFrame::GlassTone::Text);

    canvas.Fill(client, c.Window);
    canvas.Fill(glass, Glass(ShellFrame::GlassTone::Clear));
    canvas.Fill(caption, Glass(ShellFrame::GlassTone::Clear));
    {
        Gdiplus::GraphicsPath panel(Gdiplus::FillModeAlternate);
        Gdiplus::SolidBrush fill(Gdiplus::Color(255, GetRValue(c.Window), GetGValue(c.Window), GetBValue(c.Window)));
        float left = (float)PanelLeft(), top = (float)CaptionHeight(), diameter = (float)(S(8) * 2);

        panel.AddLine(left, (float)client.bottom, left, top + diameter / 2.0f);
        panel.AddArc(left, top, diameter, diameter, 180.0f, 90.0f);
        panel.AddLine(left + diameter / 2.0f, top, (float)client.right, top);
        panel.AddLine((float)client.right, top, (float)client.right, (float)client.bottom);
        panel.CloseFigure();
        canvas.Graphics().FillPath(&fill, &panel);
    }
    canvas.Text(LoadStr(IDS_SETTINGS_WINDOW).c_str(), title, GlassFont(ShellFrame::TextStyle::Caption),
                IsActive() ? ink : Glass(ShellFrame::GlassTone::TextSecondary), DT_SINGLELINE | DT_VCENTER | DT_LEFT | DT_NOPREFIX);
    for (size_t i = 0; i < m_Rail.size(); i++)
    {
        RECT pill = m_Rail[i], icon, label;
        bool selected = (int)i == m_Section, hot = (int)i == m_HotRail;
        int size = S(18);

        pill.top += S(2);
        pill.bottom -= S(2);
        pill.right -= S(4);
        if (selected || hot)
            canvas.FillRound(pill, m_Theme.ScaleF(6.0f), GlassTint(selected ? 26 : 13));
        icon.left = pill.left + S(14);
        icon.top = (pill.top + pill.bottom - size) / 2;
        icon.right = icon.left + size;
        icon.bottom = icon.top + size;
        canvas.Icon(SectionIcons[i], icon, ink);
        label = pill;
        label.left += S(46);
        canvas.Text(LoadStr(SectionNames[i]).c_str(), label, GlassFont(ShellFrame::TextStyle::Body), ink,
                    DT_SINGLELINE | DT_VCENTER | DT_LEFT | DT_END_ELLIPSIS | DT_NOPREFIX);
    }
    canvas.Fill(m_Footer, c.Card);
    canvas.Line(m_Footer.left, m_Footer.top, m_Footer.right, m_Footer.top, c.Divider);
}

void
SettingsWindow::PaintPreview(ShellFrame::Canvas &canvas)
{
    Profile *profile = CurrentProfile();
    const ColorScheme &scheme = profile ? m_Edit.SchemeFor(*profile) : m_Edit.Schemes.front();
    RECT rc = m_Preview, line;
    HDC hdc = canvas.Dc();
    HFONT font;
    HGDIOBJ old;
    TEXTMETRICW metrics;
    int padding, x, y, cell;

    if (!profile)
        return;
    canvas.FillRound(m_Preview, m_Theme.ScaleF(8), scheme.Background);
    canvas.Graphics().Flush(Gdiplus::FlushIntentionSync);
    font = CreateFontW(-MulDiv(profile->FontSize, (int)m_Theme.Dpi(), 72), 0, 0, 0, profile->FontBold ? FW_BOLD : FW_NORMAL,
                       FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                       FIXED_PITCH | FF_MODERN, profile->FontFace.c_str());
    old = SelectObject(hdc, font);
    GetTextMetricsW(hdc, &metrics);
    padding = MulDiv(profile->Padding, (int)m_Theme.Dpi(), 96) + S(8);
    cell = metrics.tmAveCharWidth;
    x = rc.left + padding;
    y = rc.top + padding;
    IntersectClipRect(hdc, rc.left, rc.top, rc.right, rc.bottom);
    SetBkMode(hdc, TRANSPARENT);

    SetTextColor(hdc, scheme.Foreground);
    TextOutW(hdc, x, y, L"C:\\Users\\you> dir /w", 20);
    y += metrics.tmHeight;
    for (int i = 0; i < 16; i++)
    {
        static const wchar_t Sample[] = L"Aa";
        int column = x + (i % 8) * cell * 3, top = y + (i / 8) * metrics.tmHeight;

        SetTextColor(hdc, scheme.Table[i]);
        TextOutW(hdc, column, top, Sample, 2);
    }
    y += 2 * metrics.tmHeight;
    line = { x, y, x + cell * 13, y + metrics.tmHeight };
    {
        HBRUSH brush = CreateSolidBrush(scheme.Selection);
        FillRect(hdc, &line, brush);
        DeleteObject(brush);
    }
    SetTextColor(hdc, scheme.Foreground);
    TextOutW(hdc, x, y, L"selected text", 13);
    y += metrics.tmHeight;
    TextOutW(hdc, x, y, L"C:\\Users\\you>", 13);
    {
        RECT cursor = { x + cell * 14, y, x + cell * 15, y + metrics.tmHeight };
        HBRUSH brush = CreateSolidBrush(scheme.Cursor);

        if (profile->Cursor == Vt::CursorShape::Bar)
            cursor.right = cursor.left + std::max<LONG>(2, S(2));
        else if (profile->Cursor == Vt::CursorShape::Underline)
            cursor.top = cursor.bottom - std::max<LONG>(2, S(2));
        FillRect(hdc, &cursor, brush);
        DeleteObject(brush);
    }
    SelectClipRgn(hdc, NULL);
    SelectObject(hdc, old);
    DeleteObject(font);
}

void
SettingsWindow::PaintPage(HDC hdc)
{
    RECT client;

    GetClientRect(m_Page, &client);
    {
        ShellFrame::Canvas canvas(hdc, client);
        const ShellFrame::Palette &c = m_Theme.Colors();
        HFONT body = m_Theme.Font(ShellFrame::TextStyle::Body);
        HFONT caption = m_Theme.Font(ShellFrame::TextStyle::Caption);
        Profile *profile = CurrentProfile();

        canvas.Fill(client, c.Window);
        PaintPreview(canvas);

        for (const TextItem &item : m_Labels)
        {
            bool note = item.Text == IDS_COLOR_FOREGROUND || item.Text == IDS_COLOR_BACKGROUND ||
                        item.Text == IDS_COLOR_CURSOR || item.Text == IDS_COLOR_SELECTION;

            canvas.Text(LoadStr(item.Text).c_str(), item.Bounds, note ? caption : body, c.TextSecondary,
                        DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX | (note ? DT_CENTER : 0));
        }

        for (size_t i = 0; i < m_Chips.size(); i++)
        {
            const RECT &rc = m_Chips[i];
            bool add = i == m_Edit.Profiles.size(), selected = (int)i == m_Profile;
            COLORREF fill = selected ? c.Accent : (int)i == m_HotChip ? c.CardHover : c.Card;
            COLORREF ink = selected ? c.AccentText : c.Text;
            RECT text = rc;

            canvas.FillRound(rc, m_Theme.ScaleF(16), fill);
            if (!selected)
                canvas.FrameRound(rc, m_Theme.ScaleF(16), c.CardBorder);
            if (add)
            {
                RECT glyph = { rc.left + S(12), rc.top + S(8), rc.left + S(28), rc.top + S(24) };

                canvas.Icon(L"add_16_regular", glyph, ink);
                text.left += S(20);
            }
            canvas.Text(add ? LoadStr(IDS_SET_ADD).c_str() : m_Edit.Profiles[i].Name.c_str(), text, body, ink,
                        DT_SINGLELINE | DT_VCENTER | DT_CENTER | DT_NOPREFIX | DT_END_ELLIPSIS);
        }

        for (size_t i = 0; i < m_Tiles.size() && i < m_Edit.Schemes.size(); i++)
        {
            const ColorScheme &scheme = m_Edit.Schemes[i];
            RECT rc = m_Tiles[i], sample = rc, name = rc;
            bool used = profile && !_wcsicmp(m_Edit.SchemeFor(*profile).Name.c_str(), scheme.Name.c_str());
            int dot = S(10);

            sample.bottom = rc.bottom - S(24);
            name.top = sample.bottom;
            canvas.FillRound(rc, m_Theme.ScaleF(8), (int)i == m_HotTile ? c.CardHover : c.Card);
            InflateRect(&sample, -S(4), -S(4));
            sample.bottom += S(4);
            canvas.FillRound(sample, m_Theme.ScaleF(5), scheme.Background);
            for (int color = 0; color < 16; color++)
            {
                RECT square;

                square.left = sample.left + S(8) + (color % 8) * (dot + S(3));
                square.top = sample.top + S(10) + (color / 8) * (dot + S(4));
                square.right = square.left + dot;
                square.bottom = square.top + dot;
                canvas.Fill(square, scheme.Table[color]);
            }
            canvas.Text(scheme.Name.c_str(), name, caption, c.Text,
                        DT_SINGLELINE | DT_VCENTER | DT_CENTER | DT_NOPREFIX | DT_END_ELLIPSIS);
            canvas.FrameRound(rc, m_Theme.ScaleF(8), used ? c.Accent : c.CardBorder, used ? m_Theme.ScaleF(2) : 1.0f);
        }

        for (int i = 0; i < 20; i++)
        {
            COLORREF *color = SwatchColor(i);

            if (!color || IsRectEmpty(&m_Swatches[i]))
                continue;
            canvas.FillRound(m_Swatches[i], m_Theme.ScaleF(4), *color);
            canvas.FrameRound(m_Swatches[i], m_Theme.ScaleF(4), c.ControlStrongStroke);
        }

        for (const CheckItem &item : m_Checks)
        {
            bool *value = CheckValue(item.Kind);
            RECT box = { item.Bounds.left, 0, item.Bounds.left + S(18), 0 }, text = item.Bounds;

            box.top = item.Bounds.top + (item.Bounds.bottom - item.Bounds.top - S(18)) / 2;
            box.bottom = box.top + S(18);
            if (value && *value)
            {
                RECT glyph = box;

                canvas.FillRound(box, m_Theme.ScaleF(4), c.Accent);
                InflateRect(&glyph, -S(2), -S(2));
                canvas.Icon(L"checkmark_16_regular", glyph, c.AccentText);
            }
            else
            {
                canvas.FillRound(box, m_Theme.ScaleF(4), c.ControlFill);
                canvas.FrameRound(box, m_Theme.ScaleF(4), c.ControlStrongStroke);
            }
            text.left = box.right + S(10);
            canvas.Text(LoadStr(item.Text).c_str(), text, body, c.Text, DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
        }
    }
}

void
SettingsWindow::OnClick(POINT pt)
{
    for (size_t i = 0; i < m_Chips.size(); i++)
    {
        if (!PtInRect(&m_Chips[i], pt))
            continue;
        StoreProfile();
        if (i == m_Edit.Profiles.size())
        {
            Profile profile = SettingsStore::Defaults().Profiles.front();

            profile.Name = LoadStr(IDS_PROFILE_NEW);
            m_Edit.Profiles.push_back(profile);
            FillProfileCombo();
            LayoutSection(m_Section, true);
        }
        m_Profile = (int)i;
        LoadProfile();
        return;
    }
    for (size_t i = 0; i < m_Tiles.size() && i < m_Edit.Schemes.size(); i++)
    {
        Profile *profile = CurrentProfile();

        if (!PtInRect(&m_Tiles[i], pt))
            continue;
        if (profile)
            profile->ColorScheme = m_Edit.Schemes[i].Name;
        m_Scheme = (int)i;
        LoadScheme();
        InvalidateRect(m_Page, NULL, FALSE);
        return;
    }
    for (int i = 0; i < 20; i++)
    {
        COLORREF *color = SwatchColor(i);
        CHOOSECOLORW choose = { sizeof(choose) };

        if (!color || !PtInRect(&m_Swatches[i], pt))
            continue;
        choose.hwndOwner = m_hwnd;
        choose.rgbResult = *color;
        choose.lpCustColors = m_CustomColors;
        choose.Flags = CC_RGBINIT | CC_FULLOPEN;
        if (ChooseColorW(&choose))
        {
            *color = choose.rgbResult;
            InvalidateRect(m_Page, NULL, FALSE);
        }
        return;
    }
    for (const CheckItem &item : m_Checks)
    {
        bool *value = CheckValue(item.Kind);

        if (!value || !PtInRect(&item.Bounds, pt))
            continue;
        *value = !*value;
        InvalidateRect(m_Page, &item.Bounds, FALSE);
        InvalidatePreview();
        return;
    }
}

void
SettingsWindow::OnHover(POINT pt)
{
    int chip = -1, tile = -1;

    for (size_t i = 0; i < m_Chips.size(); i++)
    {
        if (PtInRect(&m_Chips[i], pt))
            chip = (int)i;
    }
    for (size_t i = 0; i < m_Tiles.size(); i++)
    {
        if (PtInRect(&m_Tiles[i], pt))
            tile = (int)i;
    }
    if (chip != m_HotChip || tile != m_HotTile)
    {
        m_HotChip = chip;
        m_HotTile = tile;
        InvalidateRect(m_Page, NULL, FALSE);
    }
    if (!m_Tracking)
    {
        TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, m_Page, 0 };
        m_Tracking = TrackMouseEvent(&tme) != FALSE;
    }
}

void
SettingsWindow::OnCommand(int id, int code)
{
    switch (id)
    {
        case IdSave:
            if (Save())
                PostMessageW(m_hwnd, WM_CLOSE, 0, 0);
            return;

        case IDCANCEL:
        case IdCancel:
            PostMessageW(m_hwnd, WM_CLOSE, 0, 0);
            return;

        case IdOpenJson:
            ShellExecuteW(m_hwnd, L"open", L"notepad.exe", (L"\"" + SettingsStore::FilePath() + L"\"").c_str(), NULL,
                          SW_SHOWNORMAL);
            return;

        case IdDuplicateProfile:
            if (CurrentProfile())
            {
                Profile copy = *CurrentProfile();

                StoreProfile();
                copy.Name = LoadStr(IDS_PROFILE_NEW);
                copy.Hidden = false;
                m_Edit.Profiles.push_back(copy);
                m_Profile = (int)m_Edit.Profiles.size() - 1;
                FillProfileCombo();
                LayoutSection(m_Section, true);
                LoadProfile();
            }
            return;

        case IdDeleteProfile:
            if (m_Edit.Profiles.size() > 1 && CurrentProfile())
            {
                m_Edit.Profiles.erase(m_Edit.Profiles.begin() + m_Profile);
                if (m_Profile >= (int)m_Edit.Profiles.size())
                    m_Profile = (int)m_Edit.Profiles.size() - 1;
                FillProfileCombo();
                LayoutSection(m_Section, true);
                LoadProfile();
            }
            return;

        case IdBrowse:
        {
            wchar_t file[MAX_PATH] = L"";
            OPENFILENAMEW ofn = { sizeof(ofn) };

            ofn.hwndOwner = m_hwnd;
            ofn.lpstrFilter = L"Programs (*.exe;*.com;*.cmd;*.bat)\0*.exe;*.com;*.cmd;*.bat\0All files\0*.*\0";
            ofn.lpstrFile = file;
            ofn.nMaxFile = MAX_PATH;
            ofn.Flags = OFN_FILEMUSTEXIST | OFN_HIDEREADONLY;
            if (GetOpenFileNameW(&ofn))
            {
                std::wstring command = file;

                if (command.find(L' ') != std::wstring::npos)
                    command = L"\"" + command + L"\"";
                SetDlgItemTextW(m_Page, IdCommand, command.c_str());
            }
            return;
        }

        case IdDuplicateScheme:
            if (CurrentScheme())
            {
                ColorScheme copy = *CurrentScheme();

                copy.Name = LoadStr(IDS_SCHEME_NEW);
                m_Edit.Schemes.push_back(copy);
                m_Scheme = (int)m_Edit.Schemes.size() - 1;
                if (CurrentProfile())
                    CurrentProfile()->ColorScheme = copy.Name;
                LayoutSection(m_Section, true);
                LoadScheme();
                InvalidateRect(m_Page, NULL, FALSE);
            }
            return;

        case IdDeleteScheme:
            if (m_Edit.Schemes.size() > 1 && CurrentScheme())
            {
                std::wstring name = CurrentScheme()->Name;

                m_Edit.Schemes.erase(m_Edit.Schemes.begin() + m_Scheme);
                for (Profile &profile : m_Edit.Profiles)
                {
                    if (!_wcsicmp(profile.ColorScheme.c_str(), name.c_str()))
                        profile.ColorScheme = m_Edit.Schemes.front().Name;
                }
                m_Scheme = 0;
                LayoutSection(m_Section, true);
                LoadScheme();
                InvalidateRect(m_Page, NULL, FALSE);
            }
            return;

        case IdSchemeName:
            if (code == EN_CHANGE && !m_Loading && CurrentScheme())
            {
                std::wstring oldName = CurrentScheme()->Name, name = ControlText(GetDlgItem(m_Page, IdSchemeName));

                CurrentScheme()->Name = name;
                for (Profile &profile : m_Edit.Profiles)
                {
                    if (!_wcsicmp(profile.ColorScheme.c_str(), oldName.c_str()))
                        profile.ColorScheme = name;
                }
                InvalidateRect(m_Page, NULL, FALSE);
            }
            return;

        case IdAssign:
        case IdRemove:
        case IdReset:
        {
            HWND list = GetDlgItem(m_Page, IdKeyList);
            int index = ListView_GetNextItem(list, -1, LVNI_SELECTED);

            if (id == IdReset)
            {
                m_Edit.Bindings = SettingsStore::Defaults().Bindings;
                FillKeyList();
            }
            else if (index >= 0 && index < (int)m_Edit.Bindings.size() && id == IdRemove)
            {
                m_Edit.Bindings.erase(m_Edit.Bindings.begin() + index);
                FillKeyList();
            }
            else if (index >= 0 && index < (int)m_Edit.Bindings.size())
            {
                KeyBinding parsed;

                if (!SettingsStore::ParseKeys(ControlText(GetDlgItem(m_Page, IdKeys)), parsed))
                {
                    MessageBeep(MB_ICONWARNING);
                    return;
                }
                parsed.Command = m_Edit.Bindings[index].Command;
                parsed.Argument = m_Edit.Bindings[index].Argument;
                m_Edit.Bindings[index] = parsed;
                FillKeyList();
                ListView_SetItemState(list, index, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
            }
            return;
        }

        case IdDefaultProfile:
        case IdTheme:
            if (code == CBN_SELCHANGE)
                StoreWindowOptions();
            return;

        case IdCols:
        case IdRows:
        case IdDelimiters:
            if (code == EN_CHANGE)
                StoreWindowOptions();
            return;

        case IdKeys:
            return;
    }
    if (code == EN_CHANGE || code == CBN_SELCHANGE || code == CBN_EDITCHANGE)
        StoreProfile();
}

void
SettingsWindow::OnCreate()
{
    UpdateGlassFonts();
    CreateWindowExW(WS_EX_CONTROLPARENT, PAGE_CLASS, NULL, WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN, 0, 0, 0, 0, m_hwnd, NULL,
                    GetModuleHandleW(NULL), this);
    CreateControls();
}

void
SettingsWindow::OnLayout(const RECT &client)
{
    UNREFERENCED_PARAMETER(client);
    LayoutFrame();
}

void
SettingsWindow::OnThemeChanged()
{
    UpdateGlassFonts();
    ApplyFonts();
    LayoutFrame();
    LayoutSection(m_Section, true);
}

void
SettingsWindow::SetHotRail(int index)
{
    if (index == m_HotRail)
        return;
    m_HotRail = index;
    Invalidate(NULL);
}

LRESULT
SettingsWindow::HandleMessage(UINT msg, WPARAM wParam, LPARAM lParam)
{
    POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };

    switch (msg)
    {
        case WM_COMMAND:
            OnCommand(LOWORD(wParam), HIWORD(wParam));
            return 0;

        case WM_LBUTTONDOWN:
            for (size_t i = 0; i < m_Rail.size(); i++)
            {
                if (PtInRect(&m_Rail[i], pt))
                    ShowSection((int)i);
            }
            return 0;

        case WM_MOUSEMOVE:
        {
            int hot = -1;

            for (size_t i = 0; i < m_Rail.size(); i++)
            {
                if (PtInRect(&m_Rail[i], pt))
                    hot = (int)i;
            }
            if (!m_TrackingFrame)
            {
                TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, m_hwnd, 0 };
                m_TrackingFrame = TrackMouseEvent(&tme) != FALSE;
            }
            SetHotRail(hot);
            break;
        }

        case WM_MOUSELEAVE:
            m_TrackingFrame = false;
            SetHotRail(-1);
            break;

        case WM_CLOSE:
            DestroyWindow(m_hwnd);
            return 0;

        case WM_DESTROY:
            if (m_Owner && IsWindow(m_Owner))
                SetForegroundWindow(m_Owner);
            break;
    }
    return FrameWindow::HandleMessage(msg, wParam, lParam);
}

LRESULT
SettingsWindow::HandlePage(UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
        case WM_SIZE:
            LayoutSection(m_Section, true);
            InvalidateRect(m_Page, NULL, FALSE);
            return 0;

        case WM_ERASEBKGND:
            return 1;

        case WM_PAINT:
        {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(m_Page, &ps);

            PaintPage(hdc);
            EndPaint(m_Page, &ps);
            return 0;
        }

        case WM_LBUTTONDOWN:
        {
            POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };

            SetFocus(m_Page);
            OnClick(pt);
            return 0;
        }

        case WM_MOUSEMOVE:
        {
            POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };

            OnHover(pt);
            return 0;
        }

        case WM_MOUSELEAVE:
            m_Tracking = false;
            if (m_HotChip >= 0 || m_HotTile >= 0)
            {
                m_HotChip = m_HotTile = -1;
                InvalidateRect(m_Page, NULL, FALSE);
            }
            return 0;

        case WM_COMMAND:
            OnCommand(LOWORD(wParam), HIWORD(wParam));
            return 0;

        case WM_NOTIFY:
        {
            NMHDR *header = (NMHDR *)lParam;

            if (header->idFrom == IdKeyList && header->code == LVN_ITEMCHANGED)
            {
                int index = ListView_GetNextItem(header->hwndFrom, -1, LVNI_SELECTED);

                if (index >= 0 && index < (int)m_Edit.Bindings.size())
                    SetDlgItemTextW(m_Page, IdKeys, SettingsStore::FormatKeys(m_Edit.Bindings[index]).c_str());
            }
            return 0;
        }

        case WM_NCDESTROY:
            SetWindowLongPtrW(m_Page, GWLP_USERDATA, 0);
            m_Page = NULL;
            return 0;
    }
    return DefWindowProcW(m_Page, msg, wParam, lParam);
}

}

void
ShowSettings(HWND owner)
{
    if (g_SettingsWindow && g_SettingsWindow->Frame())
    {
        ShowWindow(g_SettingsWindow->Frame(), SW_SHOWNORMAL);
        SetForegroundWindow(g_SettingsWindow->Frame());
        return;
    }
    g_SettingsWindow.reset(new SettingsWindow(owner));
    if (!g_SettingsWindow->Frame())
        g_SettingsWindow.reset();
}

bool
SettingsPreTranslate(MSG *msg)
{
    HWND frame = g_SettingsWindow ? g_SettingsWindow->Frame() : NULL;

    if (!frame)
        return false;
    if (msg->message == WM_KEYDOWN && msg->wParam == VK_TAB && GetKeyState(VK_CONTROL) < 0 &&
        (msg->hwnd == frame || IsChild(frame, msg->hwnd)))
    {
        int count = (int)ARRAYSIZE(SectionNames), step = GetKeyState(VK_SHIFT) < 0 ? count - 1 : 1;

        g_SettingsWindow->ShowSection((g_SettingsWindow->Section() + step) % count);
        return true;
    }
    return IsDialogMessageW(frame, msg) != FALSE;
}
