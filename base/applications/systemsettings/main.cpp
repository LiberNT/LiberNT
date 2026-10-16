/*
 * PROJECT:     LiberNT Settings
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Settings window, navigation and pages
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <shellframe.h>
#include <lmcons.h>
#include <commctrl.h>
#include <shellapi.h>
#include <shlwapi.h>
#include <strsafe.h>
#include "resource.h"

using namespace ShellFrame;

struct SETTINGS_CARD
{
    UINT Title;
    UINT Description;
    LPCWSTR Icon;
    LPCWSTR Program;
    LPCWSTR Arguments;
};

struct SETTINGS_PAGE
{
    UINT Title;
    LPCWSTR Icon;
    const SETTINGS_CARD *Cards;
    UINT CardCount;
};

static const SETTINGS_CARD SystemCards[] =
{
    { IDS_CARD_DISPLAY, IDS_CARD_DISPLAY_DESC, L"desktop_20_regular", L"control.exe", L"desk.cpl" },
    { IDS_CARD_SOUND, IDS_CARD_SOUND_DESC, L"speaker_2_20_regular", L"control.exe", L"mmsys.cpl" },
    { IDS_CARD_POWER, IDS_CARD_POWER_DESC, L"power_20_regular", L"control.exe", L"powercfg.cpl" },
    { IDS_CARD_ABOUT, IDS_CARD_ABOUT_DESC, L"info_20_regular", L"control.exe", L"sysdm.cpl" },
};

static const SETTINGS_CARD DeviceCards[] =
{
    { IDS_CARD_BLUETOOTH, IDS_CARD_BLUETOOTH_DESC, L"bluetooth_20_regular", L"control.exe", L"bthprops.cpl" },
    { IDS_CARD_DEVICES, IDS_CARD_DEVICES_DESC, L"device_meeting_room_20_regular", L"devmgmt.exe", NULL },
    { IDS_CARD_MOUSE, IDS_CARD_MOUSE_DESC, L"cursor_20_regular", L"control.exe", L"main.cpl" },
    { IDS_CARD_KEYBOARD, IDS_CARD_KEYBOARD_DESC, L"keyboard_20_regular", L"control.exe", L"main.cpl,@1" },
    { IDS_CARD_PRINTERS, IDS_CARD_PRINTERS_DESC, L"print_20_regular", L"control.exe", L"printers" },
};

static const SETTINGS_CARD NetworkCards[] =
{
    { IDS_CARD_CONNECTIONS, IDS_CARD_CONNECTIONS_DESC, L"router_20_regular", L"control.exe", L"ncpa.cpl" },
    { IDS_CARD_INTERNET, IDS_CARD_INTERNET_DESC, L"globe_20_regular", L"control.exe", L"inetcpl.cpl" },
};

static const SETTINGS_CARD PersonalizeCards[] =
{
    { IDS_CARD_APPEARANCE, IDS_CARD_APPEARANCE_DESC, L"color_20_regular", L"control.exe", L"desk.cpl" },
};

static const SETTINGS_CARD AppCards[] =
{
    { IDS_CARD_INSTALLED, IDS_CARD_INSTALLED_DESC, L"apps_list_20_regular", L"control.exe", L"appwiz.cpl" },
};

static const SETTINGS_CARD AccountCards[] =
{
    { IDS_CARD_USERS, IDS_CARD_USERS_DESC, L"people_20_regular", L"control.exe", L"usrmgr.cpl" },
};

static const SETTINGS_CARD TimeCards[] =
{
    { IDS_CARD_DATETIME, IDS_CARD_DATETIME_DESC, L"calendar_clock_20_regular", L"control.exe", L"timedate.cpl" },
    { IDS_CARD_REGION, IDS_CARD_REGION_DESC, L"local_language_20_regular", L"control.exe", L"intl.cpl" },
};

static const SETTINGS_CARD GamingCards[] =
{
    { IDS_CARD_CONTROLLERS, IDS_CARD_CONTROLLERS_DESC, L"games_20_regular", L"control.exe", L"joy.cpl" },
};

static const SETTINGS_CARD AccessCards[] =
{
    { IDS_CARD_ACCESS, IDS_CARD_ACCESS_DESC, L"accessibility_20_regular", L"control.exe", L"access.cpl" },
};

static const SETTINGS_PAGE Pages[] =
{
    { IDS_PAGE_SYSTEM, L"desktop_20_regular", SystemCards, _countof(SystemCards) },
    { IDS_PAGE_DEVICES, L"bluetooth_20_regular", DeviceCards, _countof(DeviceCards) },
    { IDS_PAGE_NETWORK, L"wifi_1_20_regular", NetworkCards, _countof(NetworkCards) },
    { IDS_PAGE_PERSONALIZE, L"paint_brush_20_regular", PersonalizeCards, _countof(PersonalizeCards) },
    { IDS_PAGE_APPS, L"apps_20_regular", AppCards, _countof(AppCards) },
    { IDS_PAGE_ACCOUNTS, L"person_20_regular", AccountCards, _countof(AccountCards) },
    { IDS_PAGE_TIME, L"globe_clock_20_regular", TimeCards, _countof(TimeCards) },
    { IDS_PAGE_GAMING, L"games_20_regular", GamingCards, _countof(GamingCards) },
    { IDS_PAGE_ACCESSIBILITY, L"accessibility_20_regular", AccessCards, _countof(AccessCards) },
    { IDS_PAGE_PRIVACY, L"shield_20_regular", NULL, 0 },
    { IDS_PAGE_UPDATE, L"arrow_sync_20_regular", NULL, 0 },
};

#define SEARCH_PAGE ((int)_countof(Pages))
#define MAX_RESULTS 64
#define IDC_SEARCH 100

static HINSTANCE Instance;

static void
LoadText(UINT id, LPWSTR buffer, int length)
{
    if (!LoadStringW(Instance, id, buffer, length))
        buffer[0] = UNICODE_NULL;
}

class SettingsWindow : public FrameWindow
{
public:
    SettingsWindow() :
        m_Page(0),
        m_HistoryCount(0),
        m_HotNav(-1),
        m_HotCard(-1),
        m_PressedCard(-1),
        m_HotBack(FALSE),
        m_Scroll(0),
        m_Search(NULL),
        m_SearchFont(NULL),
        m_ResultCount(0)
    {
        DWORD length = _countof(m_User);
        if (!GetUserNameW(m_User, &length))
            m_User[0] = UNICODE_NULL;
    }

protected:
    int CaptionHeight() const override { return m_Theme.Scale(48); }

    BOOL IsCaptionClient(POINT pt) override
    {
        RECT rc = BackRect();
        return PtInRect(&rc, pt);
    }

    void OnCreate() override
    {
        WCHAR cue[64];

        m_SmallIcon = CreateGlyphIcon(L"settings_20_regular", GetSystemMetricsForDpi(SM_CXSMICON, m_Theme.Dpi()),
                                      m_Theme.Colors().Accent);
        m_BigIcon = CreateGlyphIcon(L"settings_24_regular", GetSystemMetricsForDpi(SM_CXICON, m_Theme.Dpi()),
                                    m_Theme.Colors().Accent);
        SendMessageW(m_hwnd, WM_SETICON, ICON_SMALL, (LPARAM)m_SmallIcon);
        SendMessageW(m_hwnd, WM_SETICON, ICON_BIG, (LPARAM)m_BigIcon);

        m_Search = CreateWindowExW(0, WC_EDITW, L"", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, 0, 0, 0, 0, m_hwnd,
                                   (HMENU)IDC_SEARCH, Instance, NULL);
        LoadText(IDS_FIND_SETTING, cue, _countof(cue));
        SendMessageW(m_Search, EM_SETCUEBANNER, FALSE, (LPARAM)cue);
        OnThemeChanged();
    }

    void OnThemeChanged() override
    {
        if (m_SearchFont)
            DeleteObject(m_SearchFont);
        m_SearchFont = NULL;
        if (m_Search)
        {
            LOGFONTW lf;
            GetObjectW(m_Theme.Font(TextStyle::Body), sizeof(lf), &lf);
            m_SearchFont = CreateFontIndirectW(&lf);
            SendMessageW(m_Search, WM_SETFONT, (WPARAM)m_SearchFont, TRUE);
        }
    }

    void OnLayout(const RECT &client) override
    {
        RECT box = SearchRect();

        UNREFERENCED_PARAMETER(client);
        if (m_Search)
        {
            int inset = m_Theme.Scale(11), height = m_Theme.Scale(20);
            MoveWindow(m_Search, box.left + inset, box.top + (box.bottom - box.top - height) / 2,
                       box.right - box.left - inset - m_Theme.Scale(36), height, TRUE);
        }
        ClampScroll();
    }

    void OnPaint(Canvas &canvas, const RECT &client) override
    {
        const Palette &c = m_Theme.Colors();

        canvas.Fill(client, c.Window);
        PaintCaption(canvas);
        PaintAccount(canvas);
        PaintSearch(canvas);
        PaintNavigation(canvas);
        PaintContent(canvas, client);
        PaintCaptionButtons(canvas, client, c.Window);
    }

    LRESULT HandleMessage(UINT msg, WPARAM wParam, LPARAM lParam) override
    {
        switch (msg)
        {
            case WM_MOUSEMOVE:
                OnMouseMove(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
                break;
            case WM_MOUSELEAVE:
                m_TrackingClient = FALSE;
                SetHot(-1, -1, FALSE);
                break;
            case WM_LBUTTONDOWN:
                m_PressedCard = CardFromPoint(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
                if (m_PressedCard >= 0)
                    Invalidate(NULL);
                return 0;
            case WM_LBUTTONUP:
                OnClick(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
                return 0;
            case WM_MOUSEWHEEL:
                m_Scroll -= MulDiv(GET_WHEEL_DELTA_WPARAM(wParam), m_Theme.Scale(48), WHEEL_DELTA);
                ClampScroll();
                Invalidate(NULL);
                return 0;
            case WM_COMMAND:
                if (LOWORD(wParam) == IDC_SEARCH && HIWORD(wParam) == EN_CHANGE)
                    OnSearchChanged();
                break;
            case WM_CTLCOLOREDIT:
            {
                const Palette &c = m_Theme.Colors();
                static HBRUSH brush;
                if (brush)
                    DeleteObject(brush);
                brush = CreateSolidBrush(GetFocus() == m_Search ? c.ControlFillFocus : c.ControlFill);
                SetTextColor((HDC)wParam, c.Text);
                SetBkColor((HDC)wParam, GetFocus() == m_Search ? c.ControlFillFocus : c.ControlFill);
                return (LRESULT)brush;
            }
            case WM_DESTROY:
                if (m_SearchFont)
                    DeleteObject(m_SearchFont);
                m_SearchFont = NULL;
                if (m_SmallIcon)
                    DestroyIcon(m_SmallIcon);
                if (m_BigIcon)
                    DestroyIcon(m_BigIcon);
                m_SmallIcon = m_BigIcon = NULL;
                PostQuitMessage(0);
                return 0;
        }
        LRESULT result = FrameWindow::HandleMessage(msg, wParam, lParam);
        if (msg == WM_COMMAND && HIWORD(wParam) == EN_SETFOCUS)
            Invalidate(NULL);
        if (msg == WM_COMMAND && HIWORD(wParam) == EN_KILLFOCUS)
            Invalidate(NULL);
        return result;
    }

private:
    RECT BackRect() const
    {
        RECT rc = { m_Theme.Scale(4), m_Theme.Scale(6), m_Theme.Scale(44), m_Theme.Scale(42) };
        return rc;
    }

    RECT SearchRect() const
    {
        RECT rc = { m_Theme.Scale(16), m_Theme.Scale(143), m_Theme.Scale(296), m_Theme.Scale(175) };
        return rc;
    }

    RECT NavRect(int index) const
    {
        RECT rc;
        rc.left = m_Theme.Scale(16);
        rc.right = m_Theme.Scale(296);
        rc.top = m_Theme.Scale(196) + index * m_Theme.Scale(40);
        rc.bottom = rc.top + m_Theme.Scale(36);
        return rc;
    }

    int ContentLeft() const { return m_Theme.Scale(324); }

    RECT CardRect(int index) const
    {
        RECT client, rc;
        GetClientRect(m_hwnd, &client);
        rc.left = ContentLeft();
        rc.right = max(rc.left + m_Theme.Scale(200), (int)client.right - m_Theme.Scale(24));
        rc.top = m_Theme.Scale(116) + index * m_Theme.Scale(72) - m_Scroll;
        rc.bottom = rc.top + m_Theme.Scale(68);
        return rc;
    }

    const SETTINGS_CARD *CardAt(int index) const
    {
        if (m_Page == SEARCH_PAGE)
            return index < m_ResultCount ? m_Results[index] : NULL;
        return index < (int)Pages[m_Page].CardCount ? &Pages[m_Page].Cards[index] : NULL;
    }

    int CardCount() const
    {
        return m_Page == SEARCH_PAGE ? m_ResultCount : (int)Pages[m_Page].CardCount;
    }

    void ClampScroll()
    {
        RECT client;
        int content, view;

        GetClientRect(m_hwnd, &client);
        content = m_Theme.Scale(116) + CardCount() * m_Theme.Scale(72) + m_Theme.Scale(24);
        view = client.bottom;
        m_Scroll = max(0, min(m_Scroll, max(0, content - view)));
    }

    void PaintCaption(Canvas &canvas)
    {
        const Palette &c = m_Theme.Colors();
        RECT back = BackRect(), glyph, title;
        WCHAR text[64];
        int size = m_Theme.Scale(16);

        if (m_HotBack && m_HistoryCount)
            canvas.FillRound(back, m_Theme.ScaleF(4.0f), c.SubtleHover);
        glyph.left = back.left + (back.right - back.left - size) / 2;
        glyph.top = back.top + (back.bottom - back.top - size) / 2;
        glyph.right = glyph.left + size;
        glyph.bottom = glyph.top + size;
        canvas.Icon(L"arrow_left_16_regular", glyph, m_HistoryCount ? c.Text : c.TextDisabled);
        LoadText(IDS_APP_TITLE, text, _countof(text));
        title.left = m_Theme.Scale(50);
        title.top = 0;
        title.right = m_Theme.Scale(400);
        title.bottom = CaptionHeight();
        canvas.Text(text, title, m_Theme.Font(TextStyle::Caption), IsActive() ? c.Text : c.TextDisabled,
                    DT_SINGLELINE | DT_VCENTER | DT_LEFT);
    }

    void PaintAccount(Canvas &canvas)
    {
        const Palette &c = m_Theme.Colors();
        RECT avatar = { m_Theme.Scale(16), m_Theme.Scale(58), m_Theme.Scale(76), m_Theme.Scale(118) };
        RECT glyph = avatar, name, kind;
        WCHAR text[64];

        canvas.FillEllipse(avatar, c.Selected);
        InflateRect(&glyph, -m_Theme.Scale(16), -m_Theme.Scale(16));
        canvas.Icon(L"person_20_regular", glyph, c.TextSecondary);
        name.left = m_Theme.Scale(88);
        name.top = m_Theme.Scale(70);
        name.right = m_Theme.Scale(296);
        name.bottom = m_Theme.Scale(90);
        canvas.Text(m_User, name, m_Theme.Font(TextStyle::BodyStrong), c.Text,
                    DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);
        kind = name;
        kind.top = m_Theme.Scale(90);
        kind.bottom = m_Theme.Scale(106);
        LoadText(IDS_LOCAL_ACCOUNT, text, _countof(text));
        canvas.Text(text, kind, m_Theme.Font(TextStyle::Caption), c.TextSecondary, DT_SINGLELINE | DT_VCENTER);
    }

    void PaintSearch(Canvas &canvas)
    {
        const Palette &c = m_Theme.Colors();
        RECT box = SearchRect(), glyph;
        BOOL focus = GetFocus() == m_Search;
        int size = m_Theme.Scale(16);

        canvas.FillRound(box, m_Theme.ScaleF(4.0f), focus ? c.ControlFillFocus : c.ControlFill);
        canvas.FrameRound(box, m_Theme.ScaleF(4.0f), c.ControlStroke);
        if (focus)
        {
            RECT line = { box.left + 1, box.bottom - m_Theme.Scale(2), box.right - 1, box.bottom };
            canvas.Fill(line, c.Accent);
        }
        else
        {
            RECT line = { box.left + m_Theme.Scale(2), box.bottom - 1, box.right - m_Theme.Scale(2), box.bottom };
            canvas.Fill(line, c.ControlStrongStroke);
        }
        glyph.right = box.right - m_Theme.Scale(10);
        glyph.left = glyph.right - size;
        glyph.top = box.top + (box.bottom - box.top - size) / 2;
        glyph.bottom = glyph.top + size;
        canvas.Icon(L"search_16_regular", glyph, c.TextSecondary);
    }

    void PaintNavigation(Canvas &canvas)
    {
        const Palette &c = m_Theme.Colors();
        WCHAR text[64];

        for (int i = 0; i < (int)_countof(Pages); i++)
        {
            RECT rc = NavRect(i), glyph, label;
            int size = m_Theme.Scale(18);

            if (i == m_Page)
            {
                RECT pill = { rc.left, rc.top + m_Theme.Scale(10), rc.left + m_Theme.Scale(3),
                              rc.bottom - m_Theme.Scale(10) };
                canvas.FillRound(rc, m_Theme.ScaleF(4.0f), c.Selected);
                canvas.FillRound(pill, m_Theme.ScaleF(1.5f), c.Accent);
            }
            else if (i == m_HotNav)
            {
                canvas.FillRound(rc, m_Theme.ScaleF(4.0f), c.SubtleHover);
            }
            glyph.left = rc.left + m_Theme.Scale(12);
            glyph.top = rc.top + (rc.bottom - rc.top - size) / 2;
            glyph.right = glyph.left + size;
            glyph.bottom = glyph.top + size;
            canvas.Icon(Pages[i].Icon, glyph, c.Glyph);
            label = rc;
            label.left = rc.left + m_Theme.Scale(44);
            LoadText(Pages[i].Title, text, _countof(text));
            canvas.Text(text, label, m_Theme.Font(TextStyle::Body), c.Text,
                        DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);
        }
    }

    void PaintContent(Canvas &canvas, const RECT &client)
    {
        const Palette &c = m_Theme.Colors();
        WCHAR text[128];
        RECT title = { ContentLeft(), m_Theme.Scale(52) - m_Scroll, client.right - m_Theme.Scale(24),
                       m_Theme.Scale(100) - m_Scroll };
        HRGN clip = CreateRectRgn(ContentLeft(), 0, client.right, client.bottom);

        SelectClipRgn(canvas.Dc(), clip);
        canvas.Graphics().SetClip(Gdiplus::Rect(ContentLeft(), 0, client.right - ContentLeft(), client.bottom));
        LoadText(m_Page == SEARCH_PAGE ? IDS_SEARCH_RESULTS : Pages[m_Page].Title, text, _countof(text));
        canvas.Text(text, title, m_Theme.Font(TextStyle::Title), c.Text, DT_SINGLELINE | DT_VCENTER);
        if (!CardCount())
        {
            RECT note = { ContentLeft(), m_Theme.Scale(116), client.right - m_Theme.Scale(24), m_Theme.Scale(140) };
            LoadText(m_Page == SEARCH_PAGE ? IDS_NO_RESULTS : IDS_EMPTY_PAGE, text, _countof(text));
            canvas.Text(text, note, m_Theme.Font(TextStyle::Body), c.TextSecondary, DT_SINGLELINE | DT_VCENTER);
        }
        for (int i = 0; i < CardCount(); i++)
            PaintCard(canvas, i);
        canvas.Graphics().ResetClip();
        SelectClipRgn(canvas.Dc(), NULL);
        DeleteObject(clip);
    }

    void PaintCard(Canvas &canvas, int index)
    {
        const Palette &c = m_Theme.Colors();
        const SETTINGS_CARD *card = CardAt(index);
        RECT rc = CardRect(index), glyph, title, detail, chevron;
        WCHAR text[160];
        int size = m_Theme.Scale(20), chevronSize = m_Theme.Scale(12);
        COLORREF fill = c.Card;

        if (index == m_PressedCard)
            fill = c.SubtlePressed;
        else if (index == m_HotCard)
            fill = c.CardHover;
        canvas.FillRound(rc, m_Theme.ScaleF(4.0f), fill);
        canvas.FrameRound(rc, m_Theme.ScaleF(4.0f), c.CardBorder);
        glyph.left = rc.left + m_Theme.Scale(18);
        glyph.top = rc.top + (rc.bottom - rc.top - size) / 2;
        glyph.right = glyph.left + size;
        glyph.bottom = glyph.top + size;
        canvas.Icon(card->Icon, glyph, c.Glyph);
        title.left = rc.left + m_Theme.Scale(58);
        title.right = rc.right - m_Theme.Scale(56);
        title.top = rc.top + m_Theme.Scale(14);
        title.bottom = rc.top + m_Theme.Scale(34);
        LoadText(card->Title, text, _countof(text));
        canvas.Text(text, title, m_Theme.Font(TextStyle::Body), c.Text, DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);
        detail = title;
        detail.top = title.bottom;
        detail.bottom = rc.top + m_Theme.Scale(52);
        LoadText(card->Description, text, _countof(text));
        canvas.Text(text, detail, m_Theme.Font(TextStyle::Caption), c.TextSecondary,
                    DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);
        chevron.right = rc.right - m_Theme.Scale(20);
        chevron.left = chevron.right - chevronSize;
        chevron.top = rc.top + (rc.bottom - rc.top - chevronSize) / 2;
        chevron.bottom = chevron.top + chevronSize;
        canvas.Icon(L"chevron_right_12_regular", chevron, c.Glyph);
    }

    int NavFromPoint(int x, int y) const
    {
        POINT pt = { x, y };
        for (int i = 0; i < (int)_countof(Pages); i++)
        {
            RECT rc = NavRect(i);
            if (PtInRect(&rc, pt))
                return i;
        }
        return -1;
    }

    int CardFromPoint(int x, int y) const
    {
        POINT pt = { x, y };
        if (x < ContentLeft())
            return -1;
        for (int i = 0; i < CardCount(); i++)
        {
            RECT rc = CardRect(i);
            if (PtInRect(&rc, pt))
                return i;
        }
        return -1;
    }

    void SetHot(int nav, int card, BOOL back)
    {
        if (nav == m_HotNav && card == m_HotCard && back == m_HotBack)
            return;
        m_HotNav = nav;
        m_HotCard = card;
        m_HotBack = back;
        Invalidate(NULL);
    }

    void OnMouseMove(int x, int y)
    {
        RECT back = BackRect();
        POINT pt = { x, y };

        if (!m_TrackingClient)
        {
            TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, m_hwnd, 0 };
            m_TrackingClient = TrackMouseEvent(&tme);
        }
        SetHot(NavFromPoint(x, y), CardFromPoint(x, y), PtInRect(&back, pt));
    }

    void Navigate(int page, BOOL remember)
    {
        if (page == m_Page)
            return;
        if (remember && m_HistoryCount < (int)_countof(m_History))
            m_History[m_HistoryCount++] = m_Page;
        m_Page = page;
        m_Scroll = 0;
        m_HotCard = -1;
        Invalidate(NULL);
    }

    void OnClick(int x, int y)
    {
        RECT back = BackRect();
        POINT pt = { x, y };
        int nav = NavFromPoint(x, y), card = CardFromPoint(x, y), pressed = m_PressedCard;

        m_PressedCard = -1;
        if (PtInRect(&back, pt) && m_HistoryCount)
        {
            m_Page = m_History[--m_HistoryCount];
            m_Scroll = 0;
            Invalidate(NULL);
            return;
        }
        if (nav >= 0)
        {
            SetWindowTextW(m_Search, L"");
            Navigate(nav, TRUE);
            return;
        }
        if (card >= 0 && card == pressed)
        {
            const SETTINGS_CARD *target = CardAt(card);
            ShellExecuteW(m_hwnd, NULL, target->Program, target->Arguments, NULL, SW_SHOWNORMAL);
        }
        Invalidate(NULL);
    }

    void OnSearchChanged()
    {
        WCHAR query[128], text[160];

        GetWindowTextW(m_Search, query, _countof(query));
        StrTrimW(query, L" ");
        if (!query[0])
        {
            if (m_Page == SEARCH_PAGE)
                m_Page = m_HistoryCount ? m_History[--m_HistoryCount] : 0;
            Invalidate(NULL);
            return;
        }
        m_ResultCount = 0;
        for (UINT p = 0; p < _countof(Pages); p++)
        {
            for (UINT i = 0; i < Pages[p].CardCount && m_ResultCount < MAX_RESULTS; i++)
            {
                const SETTINGS_CARD *card = &Pages[p].Cards[i];
                LoadText(card->Title, text, _countof(text));
                if (!StrStrIW(text, query))
                {
                    LoadText(card->Description, text, _countof(text));
                    if (!StrStrIW(text, query))
                        continue;
                }
                m_Results[m_ResultCount++] = card;
            }
        }
        if (m_Page != SEARCH_PAGE)
            Navigate(SEARCH_PAGE, TRUE);
        m_Scroll = 0;
        Invalidate(NULL);
    }

    int m_Page;
    int m_History[32];
    int m_HistoryCount;
    int m_HotNav;
    int m_HotCard;
    int m_PressedCard;
    BOOL m_HotBack;
    BOOL m_TrackingClient = FALSE;
    int m_Scroll;
    HWND m_Search;
    HFONT m_SearchFont;
    HICON m_SmallIcon = NULL;
    HICON m_BigIcon = NULL;
    WCHAR m_User[UNLEN + 1];
    const SETTINGS_CARD *m_Results[MAX_RESULTS];
    int m_ResultCount;
};

int WINAPI
wWinMain(HINSTANCE instance, HINSTANCE previous, LPWSTR commandLine, int showCmd)
{
    INITCOMMONCONTROLSEX icc = { sizeof(icc), ICC_STANDARD_CLASSES };
    SettingsWindow window;
    WCHAR title[64];
    MSG msg;

    UNREFERENCED_PARAMETER(previous);
    UNREFERENCED_PARAMETER(commandLine);
    Instance = instance;
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    InitCommonControlsEx(&icc);
    if (!Startup())
        return 1;
    LoadText(IDS_APP_TITLE, title, _countof(title));
    if (!window.Create(L"LiberNTSettingsWindow", title, NULL, 1000, 720, showCmd))
    {
        Shutdown();
        return 1;
    }
    while (GetMessageW(&msg, NULL, 0, 0))
    {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    Shutdown();
    return (int)msg.wParam;
}
