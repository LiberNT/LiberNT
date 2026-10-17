/*
 * PROJECT:     LiberNT Settings
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Settings window, navigation, search and page rendering
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#define SECURITY_WIN32
#include "settings.h"
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
extern "C"
{
#include <netioapi.h>
}
#include <wlanapi.h>
#include <secext.h>
#include <algorithm>

using namespace ShellFrame;

#define IDC_SEARCH 100
#define IDC_SCROLL 101
#define IDC_ROW_FIRST 1000
#define BAR_ACTION (-2)
#define TIMER_REFRESH 1
#define TIMER_SETTING 2
#define WM_SETTINGS_CONTROL (WM_APP + 16)

enum class GlassTone
{
    Text,
    TextSecondary,
    TextDisabled,
    Hover,
    Selected,
    Stroke,
    Clear,
};

static COLORREF
GlassColor(const Theme &theme, GlassTone tone)
{
    static const COLORREF Tones[] =
    {
        RGB(255, 255, 255), RGB(0xC8, 0xC8, 0xC8), RGB(0x80, 0x80, 0x80), RGB(0x1C, 0x1C, 0x1C),
        RGB(0x30, 0x30, 0x30), RGB(0x60, 0x60, 0x60), RGB(0, 0, 0),
    };
    const Palette &c = theme.Colors();

    if (!theme.IsHighContrast())
        return Tones[(int)tone];
    switch (tone)
    {
        case GlassTone::Text:
        case GlassTone::TextSecondary:
        case GlassTone::Stroke:
            return c.Text;
        case GlassTone::TextDisabled:
            return c.TextDisabled;
        default:
            return c.Window;
    }
}

HINSTANCE Instance;

struct RowPlace
{
    int Group;
    int Row;
    RECT Box;
    RECT Control;
    RECT Bar;
    std::vector<RECT> Actions;
};

struct RowControl
{
    HWND Window;
    int Place;
    int Action;
    int Height;
    HICON Image;
};

struct SearchEntry
{
    PageId Page;
    std::wstring Title;
    std::wstring Detail;
    std::wstring Keywords;
    std::wstring Path;
    LPCWSTR Icon;
};

struct JobData
{
    std::function<void()> Work;
    std::function<void()> Done;
    HWND Window;
};

static void
GlassFill(Canvas &canvas, const RECT &rc, int radius, COLORREF color, COLORREF stroke)
{
    HDC dc = canvas.Dc();
    HBRUSH brush = CreateSolidBrush(color);
    HPEN pen = stroke != CLR_NONE ? CreatePen(PS_SOLID, 1, stroke) : NULL;
    HGDIOBJ oldBrush, oldPen;

    canvas.Graphics().Flush(Gdiplus::FlushIntentionSync);
    oldBrush = SelectObject(dc, brush);
    oldPen = SelectObject(dc, pen ? (HGDIOBJ)pen : GetStockObject(NULL_PEN));
    RoundRect(dc, rc.left, rc.top, rc.right + (pen ? 0 : 1), rc.bottom + (pen ? 0 : 1), radius * 2, radius * 2);
    SelectObject(dc, oldPen);
    SelectObject(dc, oldBrush);
    if (pen)
        DeleteObject(pen);
    DeleteObject(brush);
}

static DWORD WINAPI
JobThread(LPVOID parameter)
{
    JobData *job = (JobData *)parameter;

    CoInitializeEx(NULL, COINIT_MULTITHREADED);
    if (job->Work)
        job->Work();
    CoUninitialize();
    if (!PostMessageW(job->Window, WM_SETTINGS_JOB, 0, (LPARAM)job))
        delete job;
    return 0;
}

static VOID WINAPI
AddressChanged(PVOID context, PMIB_UNICASTIPADDRESS_ROW row, MIB_NOTIFICATION_TYPE type)
{
    PostMessageW((HWND)context, WM_SETTINGS_REFRESH, 0, 0);
}

static VOID WINAPI
InterfaceChanged(PVOID context, PMIB_IPINTERFACE_ROW row, MIB_NOTIFICATION_TYPE type)
{
    PostMessageW((HWND)context, WM_SETTINGS_REFRESH, 0, 0);
}

static VOID WINAPI
WlanChanged(PWLAN_NOTIFICATION_DATA data, PVOID context)
{
    if (!data || data->NotificationSource != WLAN_NOTIFICATION_SOURCE_ACM)
        return;
    if (data->NotificationCode == wlan_notification_acm_connection_attempt_fail &&
        data->dwDataSize >= sizeof(WLAN_CONNECTION_NOTIFICATION_DATA) && data->pData)
    {
        const WLAN_CONNECTION_NOTIFICATION_DATA *connection = (const WLAN_CONNECTION_NOTIFICATION_DATA *)data->pData;
        std::wstring *name = new std::wstring();

        for (ULONG i = 0; i < connection->dot11Ssid.uSSIDLength && i < DOT11_SSID_MAX_LENGTH; i++)
            name->push_back((WCHAR)connection->dot11Ssid.ucSSID[i]);
        if (name->empty())
            *name = connection->strProfileName;
        if (!PostMessageW((HWND)context, WM_SETTINGS_WIFI_FAILED, 0, (LPARAM)name))
            delete name;
    }
    switch (data->NotificationCode)
    {
        case wlan_notification_acm_connection_attempt_fail:
        case wlan_notification_acm_scan_complete:
        case wlan_notification_acm_scan_fail:
        case wlan_notification_acm_connection_complete:
        case wlan_notification_acm_disconnected:
        case wlan_notification_acm_interface_arrival:
        case wlan_notification_acm_interface_removal:
            PostMessageW((HWND)context, WM_SETTINGS_REFRESH, 0, 0);
            break;
        default:
            break;
    }
}

static void PageFromCommandLine(LPCWSTR commandLine, PageId *page, std::wstring *param);

static bool
Contains(const std::wstring &text, LPCWSTR query)
{
    return !text.empty() && StrStrIW(text.c_str(), query) != NULL;
}

class SettingsWindow : public FrameWindow, public SettingsHost
{
public:
    SettingsWindow() :
        m_Page(PageId::System),
        m_Scroll(0),
        m_ContentHeight(0),
        m_HotNav(-1),
        m_HotBack(FALSE),
        m_HotForward(FALSE),
        m_HotAccount(FALSE),
        m_HotCrumb(-1),
        m_TrackingClient(FALSE),
        m_Search(NULL),
        m_ScrollBar(NULL),
        m_CardBrush(NULL),
        m_CardColor(CLR_NONE),
        m_SearchFont(NULL),
        m_SmallIcon(NULL),
        m_BigIcon(NULL),
        m_IndexReady(false)
    {
        ULONG length = _countof(m_User);
        if (!GetUserNameExW(NameDisplay, m_User, &length) || !m_User[0])
        {
            DWORD size = _countof(m_User);
            if (!GetUserNameW(m_User, &size))
                m_User[0] = UNICODE_NULL;
        }
        m_Admin = IsAdmin();
        m_Theme.SetFollowSystemColors(TRUE);
    }

    void SetInitialPage(PageId page, const std::wstring &param)
    {
        m_Page = page;
        m_Param = param;
    }

    HWND Window() const override { return m_hwnd; }

    void Refresh() override
    {
        Rebuild(FALSE);
    }

    void Open(PageId page, const std::wstring &param) override
    {
        Navigate(page, param, TRUE);
    }

    void Back() override
    {
        GoBack();
    }

    void RunJob(std::function<void()> work, std::function<void()> done) override
    {
        JobData *job = new JobData;
        HANDLE thread;

        job->Work = work;
        job->Done = done;
        job->Window = m_hwnd;
        thread = CreateThread(NULL, 0, JobThread, job, 0, NULL);
        if (thread)
            CloseHandle(thread);
        else
        {
            if (job->Work)
                job->Work();
            if (job->Done)
                job->Done();
            delete job;
        }
    }

    void SetTimerRefresh(UINT milliseconds) override
    {
        SetTimer(m_hwnd, TIMER_REFRESH, milliseconds, NULL);
    }

protected:
    int CaptionHeight() const override { return m_Theme.Scale(48); }
    int GlassWidth() const override { return PanelLeft() + PanelRadius(); }
    int GlassTop() const override { return CaptionHeight() + PanelRadius(); }

    BOOL IsCaptionClient(POINT pt) override
    {
        RECT back = BackRect(), forward = ForwardRect();
        return PtInRect(&back, pt) || PtInRect(&forward, pt);
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

        PlaceWindow();
        m_Search = CreateWindowExW(WS_EX_CLIENTEDGE, WC_EDITW, L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
                                   0, 0, 0, 0, m_hwnd, (HMENU)IDC_SEARCH, Instance, NULL);
        m_ScrollBar = CreateWindowExW(0, WC_SCROLLBARW, L"", WS_CHILD | SBS_VERT, 0, 0, 0, 0, m_hwnd,
                                      (HMENU)IDC_SCROLL, Instance, NULL);
        SetWindowSubclass(m_Search, SearchProc, 0, (DWORD_PTR)this);
        StringCchCopyW(cue, _countof(cue), Str(IDS_FIND_SETTING).c_str());
        SendMessageW(m_Search, EM_SETCUEBANNER, FALSE, (LPARAM)cue);
        OnThemeChanged();
        NotifyUnicastIpAddressChange(AF_UNSPEC, AddressChanged, m_hwnd, FALSE, &m_AddressNotify);
        NotifyIpInterfaceChange(AF_UNSPEC, InterfaceChanged, m_hwnd, FALSE, &m_InterfaceNotify);
        {
            DWORD version = 0;
            if (WlanOpenHandle(2, NULL, &version, &m_Wlan) == ERROR_SUCCESS)
                WlanRegisterNotification(m_Wlan, WLAN_NOTIFICATION_SOURCE_ACM, TRUE, WlanChanged, m_hwnd, NULL, NULL);
            else
                m_Wlan = NULL;
        }
        Rebuild(TRUE);
    }

    void OnThemeChanged() override
    {
        LOGFONTW lf;

        if (m_SearchFont)
            DeleteObject(m_SearchFont);
        m_SearchFont = NULL;
        if (m_Search)
        {
            GetObjectW(m_Theme.Font(TextStyle::Body), sizeof(lf), &lf);
            m_SearchFont = CreateFontIndirectW(&lf);
            SendMessageW(m_Search, WM_SETFONT, (WPARAM)m_SearchFont, TRUE);
        }
    }

    void PlaceWindow()
    {
        MONITORINFO monitor = { sizeof(monitor) };
        RECT work, frame;
        int width, height;

        if (!GetMonitorInfoW(MonitorFromWindow(m_hwnd, MONITOR_DEFAULTTONEAREST), &monitor))
            return;
        work = monitor.rcWork;
        GetWindowRect(m_hwnd, &frame);
        width = min((int)(frame.right - frame.left), (int)(work.right - work.left) - m_Theme.Scale(24));
        height = min((int)(frame.bottom - frame.top), (int)(work.bottom - work.top) - m_Theme.Scale(24));
        SetWindowPos(m_hwnd, NULL, work.left + (work.right - work.left - width) / 2,
                     work.top + (work.bottom - work.top - height) / 2, width, height, SWP_NOZORDER | SWP_NOACTIVATE);
    }

    void OnLayout(const RECT &client) override
    {
        RECT box = SearchRect();

        UNREFERENCED_PARAMETER(client);
        if (m_Search)
        {
            TEXTMETRICW tm;
            HDC dc = GetDC(m_Search);
            HGDIOBJ old = SelectObject(dc, m_SearchFont ? m_SearchFont : m_Theme.Font(TextStyle::Body));
            int height;

            GetTextMetricsW(dc, &tm);
            SelectObject(dc, old);
            ReleaseDC(m_Search, dc);
            height = tm.tmHeight + 2 * GetSystemMetricsForDpi(SM_CYEDGE, m_Theme.Dpi()) + m_Theme.Scale(8);
            MoveWindow(m_Search, box.left, box.top + (box.bottom - box.top - height) / 2, box.right - box.left, height,
                       TRUE);
        }
        LayoutContent();
    }

    void OnPaint(Canvas &canvas, const RECT &client) override
    {
        const Palette &c = m_Theme.Colors();
        RECT glass = { 0, 0, GlassWidth(), client.bottom }, caption = { 0, 0, client.right, CaptionHeight() };

        canvas.Fill(client, c.Window);
        canvas.Fill(glass, GlassColor(m_Theme, GlassTone::Clear));
        canvas.Fill(caption, GlassColor(m_Theme, GlassTone::Clear));
        {
            Gdiplus::GraphicsPath panel(Gdiplus::FillModeAlternate);
            Gdiplus::SolidBrush fill(Gdiplus::Color(255, GetRValue(c.Window), GetGValue(c.Window), GetBValue(c.Window)));
            float left = (float)PanelLeft(), top = (float)CaptionHeight(), diameter = (float)(PanelRadius() * 2);

            panel.AddLine(left, (float)client.bottom, left, top + diameter / 2.0f);
            panel.AddArc(left, top, diameter, diameter, 180.0f, 90.0f);
            panel.AddLine(left + diameter / 2.0f, top, (float)client.right, top);
            panel.AddLine((float)client.right, top, (float)client.right, (float)client.bottom);
            panel.CloseFigure();
            canvas.Graphics().FillPath(&fill, &panel);
        }
        PaintCaption(canvas);
        PaintAccount(canvas);
        PaintNavigation(canvas, client);
        PaintContent(canvas, client);
#if SHELLFRAME_CUSTOM_CAPTION_BUTTONS
        PaintCaptionButtons(canvas, client, c.Window);
#endif
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
                SetHot(-1, FALSE, FALSE, FALSE, -1);
                break;
            case WM_LBUTTONUP:
                OnButtonUp(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
                return 0;
            case WM_XBUTTONUP:
                if (GET_XBUTTON_WPARAM(wParam) == XBUTTON1)
                    GoBack();
                else if (GET_XBUTTON_WPARAM(wParam) == XBUTTON2)
                    GoForward();
                return TRUE;
            case WM_APPCOMMAND:
                if (GET_APPCOMMAND_LPARAM(lParam) == APPCOMMAND_BROWSER_BACKWARD)
                {
                    GoBack();
                    return TRUE;
                }
                if (GET_APPCOMMAND_LPARAM(lParam) == APPCOMMAND_BROWSER_FORWARD)
                {
                    GoForward();
                    return TRUE;
                }
                break;
            case WM_MOUSEWHEEL:
            {
                POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
                ScreenToClient(m_hwnd, &pt);
                if (pt.x < PanelLeft())
                    m_NavScroll -= MulDiv(GET_WHEEL_DELTA_WPARAM(wParam), m_Theme.Scale(40), WHEEL_DELTA);
                else
                    m_Scroll -= MulDiv(GET_WHEEL_DELTA_WPARAM(wParam), m_Theme.Scale(64), WHEEL_DELTA);
                ClampScroll();
                Invalidate(NULL);
                return 0;
            }
            case WM_VSCROLL:
                if ((HWND)lParam == m_ScrollBar)
                {
                    OnScrollBar(LOWORD(wParam));
                    return 0;
                }
                break;
            case WM_HSCROLL:
            {
                int index = ControlIndex((HWND)lParam);
                if (index >= 0)
                {
                    OnSlider(index, LOWORD(wParam));
                    return 0;
                }
                break;
            }
            case WM_KEYDOWN:
                if (OnKey((UINT)wParam))
                    return 0;
                break;
            case WM_SYSKEYDOWN:
                if (wParam == VK_LEFT)
                {
                    GoBack();
                    return 0;
                }
                if (wParam == VK_RIGHT)
                {
                    GoForward();
                    return 0;
                }
                break;
            case WM_COMMAND:
            {
                int index = ControlIndex((HWND)lParam);
                if (LOWORD(wParam) == IDC_SEARCH && HIWORD(wParam) == EN_CHANGE)
                    OnSearchChanged();
                if (index >= 0 && HIWORD(wParam) == BN_CLICKED)
                    PostMessageW(m_hwnd, WM_SETTINGS_CONTROL, index, 0);
                if (index >= 0 && HIWORD(wParam) == CBN_SELCHANGE)
                    PostMessageW(m_hwnd, WM_SETTINGS_CONTROL, index, SendMessageW((HWND)lParam, CB_GETCURSEL, 0, 0));
                break;
            }
            case WM_CTLCOLORSTATIC:
            case WM_CTLCOLORBTN:
                if (ControlIndex((HWND)lParam) >= 0 || (HWND)lParam == m_ScrollBar)
                {
                    SetTextColor((HDC)wParam, m_Theme.Colors().Text);
                    SetBkColor((HDC)wParam, m_Theme.Colors().Card);
                    return (LRESULT)CardBrush();
                }
                break;
            case WM_PRINTCLIENT:
            {
                RECT box;
                if (GetClipBox((HDC)wParam, &box) != NULLREGION && InControls(box))
                {
                    FillRect((HDC)wParam, &box, CardBrush());
                    return 0;
                }
                break;
            }
            case WM_SETTINGS_CONTROL:
                ActivateControl((int)wParam, (int)lParam);
                return 0;
            case WM_TIMER:
                if (wParam == TIMER_REFRESH || wParam == TIMER_SETTING)
                {
                    KillTimer(m_hwnd, wParam);
                    Rebuild(FALSE);
                    return 0;
                }
                break;
            case WM_COPYDATA:
            {
                COPYDATASTRUCT *data = (COPYDATASTRUCT *)lParam;
                PageId page;
                std::wstring param;
                if (data && data->dwData == 1 && data->lpData && data->cbData >= sizeof(WCHAR))
                {
                    std::wstring command((LPCWSTR)data->lpData, data->cbData / sizeof(WCHAR));
                    PageFromCommandLine(command.c_str(), &page, &param);
                    if (StrStrIW(command.c_str(), L"ms-settings:"))
                        Navigate(page, param, TRUE);
                }
                return TRUE;
            }
            case WM_SETTINGS_WIFI_FAILED:
            {
                std::wstring *name = (std::wstring *)lParam;
                TASKDIALOGCONFIG config = { sizeof(config) };
                std::wstring title = Str(IDS_APP_TITLE), heading = Str(IDS_WIFI_CANT_CONNECT);

                config.hwndParent = m_hwnd;
                config.dwFlags = TDF_POSITION_RELATIVE_TO_WINDOW;
                config.dwCommonButtons = TDCBF_OK_BUTTON;
                config.pszWindowTitle = title.c_str();
                config.pszMainIcon = TD_ERROR_ICON;
                config.pszMainInstruction = heading.c_str();
                config.pszContent = name->c_str();
                TaskDialogIndirect(&config, NULL, NULL, NULL);
                delete name;
                return 0;
            }
            case WM_SETTINGS_JOB:
            {
                JobData *job = (JobData *)lParam;
                if (job->Done)
                    job->Done();
                delete job;
                return 0;
            }
            case WM_GETMINMAXINFO:
            {
                MINMAXINFO *info = (MINMAXINFO *)lParam;
                info->ptMinTrackSize.x = m_Theme.Scale(600);
                info->ptMinTrackSize.y = m_Theme.Scale(440);
                return 0;
            }
            case WM_SETTINGS_REFRESH:
            case WM_DISPLAYCHANGE:
            case WM_DEVICECHANGE:
            case WM_TIMECHANGE:
            case WM_POWERBROADCAST:
                SetTimer(m_hwnd, TIMER_SETTING, 400, NULL);
                break;
            case WM_THEMECHANGED:
            case WM_SETTINGCHANGE:
            {
                LRESULT result = FrameWindow::HandleMessage(msg, wParam, lParam);
                SetTimer(m_hwnd, TIMER_SETTING, 300, NULL);
                Invalidate(NULL);
                return result;
            }
            case WM_DESTROY:
                if (m_AddressNotify)
                    CancelMibChangeNotify2(m_AddressNotify);
                if (m_InterfaceNotify)
                    CancelMibChangeNotify2(m_InterfaceNotify);
                m_AddressNotify = m_InterfaceNotify = NULL;
                if (m_Wlan)
                {
                    WlanRegisterNotification(m_Wlan, WLAN_NOTIFICATION_SOURCE_NONE, TRUE, NULL, NULL, NULL, NULL);
                    WlanCloseHandle(m_Wlan, NULL);
                    m_Wlan = NULL;
                }
                if (m_SearchFont)
                    DeleteObject(m_SearchFont);
                m_SearchFont = NULL;
                DestroyControls();
                if (m_CardBrush)
                    DeleteObject(m_CardBrush);
                m_CardBrush = NULL;
                if (m_SmallIcon)
                    DestroyIcon(m_SmallIcon);
                if (m_BigIcon)
                    DestroyIcon(m_BigIcon);
                m_SmallIcon = m_BigIcon = NULL;
                FreeImages();
                PostQuitMessage(0);
                return 0;
        }
        return FrameWindow::HandleMessage(msg, wParam, lParam);
    }

private:
    static LRESULT CALLBACK SearchProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam, UINT_PTR id, DWORD_PTR data)
    {
        SettingsWindow *window = (SettingsWindow *)data;

        UNREFERENCED_PARAMETER(id);
        if (msg == WM_KEYDOWN && wParam == VK_ESCAPE)
        {
            SetWindowTextW(hwnd, L"");
            return 0;
        }
        if (msg == WM_KEYDOWN && wParam == VK_RETURN)
        {
            window->OpenFirstResult();
            return 0;
        }
        if (msg == WM_CHAR && (wParam == VK_RETURN || wParam == VK_ESCAPE))
            return 0;
        return DefSubclassProc(hwnd, msg, wParam, lParam);
    }

    RECT BackRect() const
    {
        RECT rc = { m_Theme.Scale(6), m_Theme.Scale(8), m_Theme.Scale(38), m_Theme.Scale(40) };
        return rc;
    }

    RECT ForwardRect() const
    {
        RECT rc = { m_Theme.Scale(40), m_Theme.Scale(8), m_Theme.Scale(72), m_Theme.Scale(40) };
        return rc;
    }

    RECT SearchRect() const
    {
        RECT rc = { m_Theme.Scale(16), m_Theme.Scale(56), PanelLeft() - m_Theme.Scale(16), m_Theme.Scale(88) };
        return rc;
    }

    RECT AccountRect() const
    {
        RECT rc = { m_Theme.Scale(12), m_Theme.Scale(100), PanelLeft() - m_Theme.Scale(12), m_Theme.Scale(160) };
        return rc;
    }

    int NavTop() const { return m_Theme.Scale(172); }

    RECT NavRect(int index) const
    {
        RECT rc;
        rc.left = m_Theme.Scale(12);
        rc.right = PanelLeft() - m_Theme.Scale(12);
        rc.top = NavTop() + index * m_Theme.Scale(40) - m_NavScroll;
        rc.bottom = rc.top + m_Theme.Scale(36);
        return rc;
    }

    int ContentLeft() const
    {
        RECT client;

        GetClientRect(m_hwnd, &client);
        return client.right < m_Theme.Scale(900) ? m_Theme.Scale(264) : m_Theme.Scale(328);
    }

    int PanelLeft() const { return ContentLeft() - m_Theme.Scale(16); }
    int PanelRadius() const { return m_Theme.Scale(8); }

    int ContentRight() const
    {
        RECT client;
        GetClientRect(m_hwnd, &client);
        return min((int)client.right - m_Theme.Scale(16) - GetSystemMetricsForDpi(SM_CXVSCROLL, m_Theme.Dpi()),
                   ContentLeft() + m_Theme.Scale(1000));
    }

    int ContentTop() const { return CaptionHeight() + m_Theme.Scale(4); }

    std::vector<PageId> Categories() const
    {
        std::vector<PageId> pages;
        UINT count;
        const PageInfo *table = PageTable(&count);

        for (UINT i = 0; i < count; i++)
        {
            if (table[i].Parent == PageId::None && table[i].Id != PageId::Search)
                pages.push_back(table[i].Id);
        }
        return pages;
    }

    PageId TopLevel(PageId page) const
    {
        const PageInfo *info = FindPage(page);

        while (info && info->Parent != PageId::None)
            info = FindPage(info->Parent);
        return info ? info->Id : PageId::None;
    }

    void FreeImages()
    {
        for (size_t g = 0; g < m_Groups.size(); g++)
        {
            for (size_t r = 0; r < m_Groups[g].Rows.size(); r++)
            {
                if (m_Groups[g].Rows[r].Image)
                    DestroyIcon(m_Groups[g].Rows[r].Image);
                m_Groups[g].Rows[r].Image = NULL;
            }
        }
    }

    void Rebuild(BOOL resetScroll)
    {
        const PageInfo *info = FindPage(m_Page);
        PageBuilder page(this, m_Param, false);
        int focusPlace = -1, focusAction = -1, focus = ControlIndex(GetFocus());

        if (focus >= 0)
        {
            focusPlace = m_Controls[focus].Place;
            focusAction = m_Controls[focus].Action;
        }
        DestroyControls();
        FreeImages();
        if (m_Page == PageId::Search)
            BuildSearch(page);
        else if (info && info->Build)
            info->Build(page);
        if (page.Title.empty() && info)
            page.Title = Str(info->Title);
        m_Title = page.Title;
        m_Groups.swap(page.Groups);
        if (resetScroll)
        {
            m_Scroll = 0;
            focusPlace = -1;
        }
        LayoutContent();
        CreateControls();
        for (size_t i = 0; i < m_Controls.size() && focusPlace >= 0; i++)
        {
            if (m_Controls[i].Place == focusPlace && m_Controls[i].Action == focusAction)
                SetFocus(m_Controls[i].Window);
        }
        Invalidate(NULL);
    }

    void Navigate(PageId page, const std::wstring &param, BOOL remember)
    {
        if (page == m_Page && param == m_Param)
            return;
        if (remember)
        {
            m_Back.push_back(std::make_pair(m_Page, m_Param));
            m_Forward.clear();
        }
        if (page != PageId::Search && m_Page == PageId::Search && m_Search)
        {
            m_Page = page;
            SetWindowTextW(m_Search, L"");
        }
        m_Page = page;
        m_Param = param;
        KillTimer(m_hwnd, TIMER_REFRESH);
        Rebuild(TRUE);
    }

    void GoBack()
    {
        if (m_Back.empty())
            return;
        m_Forward.push_back(std::make_pair(m_Page, m_Param));
        m_Page = m_Back.back().first;
        m_Param = m_Back.back().second;
        m_Back.pop_back();
        if (m_Page != PageId::Search && m_Search && GetWindowTextLengthW(m_Search))
        {
            PageId page = m_Page;
            SetWindowTextW(m_Search, L"");
            m_Page = page;
        }
        Rebuild(TRUE);
    }

    void GoForward()
    {
        if (m_Forward.empty())
            return;
        m_Back.push_back(std::make_pair(m_Page, m_Param));
        m_Page = m_Forward.back().first;
        m_Param = m_Forward.back().second;
        m_Forward.pop_back();
        Rebuild(TRUE);
    }

    std::vector<std::pair<PageId, std::wstring>> Breadcrumb() const
    {
        std::vector<std::pair<PageId, std::wstring>> parts;
        const PageInfo *info = FindPage(m_Page);

        if (!info || m_Page == PageId::Search)
            return parts;
        for (info = FindPage(info->Parent); info; info = FindPage(info->Parent))
            parts.insert(parts.begin(), std::make_pair(info->Id, Str(info->Title)));
        return parts;
    }

    int RowHeight(const SettingRow &row, int width)
    {
        switch (row.Kind)
        {
            case RowKind::Hero:
                return m_Theme.Scale(96);
            case RowKind::Text:
            {
                RECT rc = { 0, 0, width - m_Theme.Scale(32), 0 };
                HDC dc = GetDC(m_hwnd);
                HGDIOBJ old = SelectObject(dc, m_Theme.Font(TextStyle::Body));
                DrawTextW(dc, row.Title.c_str(), -1, &rc, DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX);
                SelectObject(dc, old);
                ReleaseDC(m_hwnd, dc);
                return rc.bottom + m_Theme.Scale(28);
            }
            default:
                if (row.Bar)
                    return m_Theme.Scale(84);
                return row.Detail.empty() ? m_Theme.Scale(52) : m_Theme.Scale(68);
        }
    }

    int MeasureWidth(const std::wstring &text, HFONT font)
    {
        SIZE size = { 0, 0 };
        HDC dc = GetDC(m_hwnd);
        HGDIOBJ old = SelectObject(dc, font);
        GetTextExtentPoint32W(dc, text.c_str(), (int)text.length(), &size);
        SelectObject(dc, old);
        ReleaseDC(m_hwnd, dc);
        return size.cx;
    }

    int HeaderBottom() const
    {
        return ContentTop() + m_Theme.Scale(72);
    }

    void LayoutContent()
    {
        int left = ContentLeft(), right = ContentRight(), y = HeaderBottom();

        if (!m_hwnd)
            return;
        m_Places.clear();
        m_GroupBoxes.clear();
        m_GroupTitles.clear();
        m_GroupIcons.clear();
        for (size_t g = 0; g < m_Groups.size(); g++)
        {
            SettingGroup &group = m_Groups[g];
            RECT box, title = { left, y, right, y };
            bool icons = false;

            for (size_t r = 0; r < group.Rows.size(); r++)
                icons = icons || ((group.Rows[r].Icon || group.Rows[r].Image) && group.Rows[r].Kind != RowKind::Hero);
            m_GroupIcons.push_back(icons);

            if (!group.Title.empty())
            {
                title.bottom = y + m_Theme.Scale(28);
                y = title.bottom + m_Theme.Scale(4);
            }
            m_GroupTitles.push_back(title);
            box.left = left;
            box.right = right;
            box.top = y;
            for (size_t r = 0; r < group.Rows.size(); r++)
            {
                SettingRow &row = group.Rows[r];
                RowPlace place;
                int height = RowHeight(row, right - left);

                place.Group = (int)g;
                place.Row = (int)r;
                SetRect(&place.Box, left, y, right, y + height);
                SetRectEmpty(&place.Control);
                SetRectEmpty(&place.Bar);
                LayoutControl(row, place);
                if (row.Bar)
                    SetRect(&place.Bar, left + m_Theme.Scale(icons ? 56 : 16), y + height - m_Theme.Scale(24),
                            right - m_Theme.Scale(16), y + height - m_Theme.Scale(12));
                m_Places.push_back(place);
                y += height;
            }
            box.bottom = y;
            m_GroupBoxes.push_back(box);
            if (!group.Footer.empty())
                y += m_Theme.Scale(30);
            y += m_Theme.Scale(24);
        }
        m_ContentHeight = y + m_Theme.Scale(16);
        ClampScroll();
    }

    int ControlWidth(int share, int preferred) const
    {
        return min(preferred, max(m_Theme.Scale(96), (ContentRight() - ContentLeft()) * share / 100));
    }

    void LayoutControl(const SettingRow &row, RowPlace &place)
    {
        RECT &rc = place.Box, &control = place.Control;
        int middle = (rc.top + rc.bottom) / 2, right = rc.right - m_Theme.Scale(16);

        switch (row.Kind)
        {
            case RowKind::Toggle:
                SetRect(&control, right - m_Theme.Scale(20), middle - m_Theme.Scale(10), right, middle + m_Theme.Scale(10));
                break;
            case RowKind::Choice:
            {
                int width = m_Theme.Scale(120);
                for (size_t i = 0; i < row.Options.size(); i++)
                    width = max(width, MeasureWidth(row.Options[i], m_Theme.Font(TextStyle::Body)) + m_Theme.Scale(40));
                width = min(width, ControlWidth(45, m_Theme.Scale(300)));
                SetRect(&control, right - width, middle - m_Theme.Scale(14), right, middle + m_Theme.Scale(14));
                break;
            }
            case RowKind::Slider:
                SetRect(&control, right - ControlWidth(35, m_Theme.Scale(220)), middle - m_Theme.Scale(14), right,
                        middle + m_Theme.Scale(14));
                break;
            case RowKind::Hero:
            case RowKind::Button:
            {
                std::vector<std::wstring> actions = row.Actions;
                if (!row.Action.empty())
                    actions.insert(actions.begin(), row.Action);
                for (size_t i = actions.size(); i-- > 0;)
                {
                    int width = max(m_Theme.Scale(96), MeasureWidth(actions[i], m_Theme.Font(TextStyle::Body)) +
                                                        m_Theme.Scale(32));
                    RECT button = { right - width, middle - m_Theme.Scale(14), right, middle + m_Theme.Scale(14) };
                    place.Actions.insert(place.Actions.begin(), button);
                    right -= width + m_Theme.Scale(8);
                }
                if (!place.Actions.empty())
                {
                    control = place.Actions.front();
                    control.right = place.Actions.back().right;
                }
                break;
            }
            case RowKind::Navigate:
            case RowKind::Link:
                SetRect(&control, rc.left + m_Theme.Scale(4), rc.top + m_Theme.Scale(4), rc.right - m_Theme.Scale(4),
                        rc.bottom - m_Theme.Scale(4));
                break;
            default:
                break;
        }
    }

    void ClampScroll()
    {
        RECT client;
        int view, navView, navHeight;

        GetClientRect(m_hwnd, &client);
        view = client.bottom - HeaderBottom() + m_Theme.Scale(0);
        m_Scroll = max(0, min(m_Scroll, max(0, m_ContentHeight - HeaderBottom() - view)));
        navView = client.bottom - NavTop() - m_Theme.Scale(8);
        navHeight = (int)Categories().size() * m_Theme.Scale(40);
        m_NavScroll = max(0, min(m_NavScroll, max(0, navHeight - navView)));
        if (m_ScrollBar)
        {
            SCROLLINFO info = { sizeof(info), SIF_RANGE | SIF_PAGE | SIF_POS };
            int width = GetSystemMetricsForDpi(SM_CXVSCROLL, m_Theme.Dpi());

            info.nMax = max(0, m_ContentHeight - HeaderBottom() - 1);
            info.nPage = (UINT)max(0, view);
            info.nPos = m_Scroll;
            SetScrollInfo(m_ScrollBar, SB_CTL, &info, TRUE);
            SetWindowPos(m_ScrollBar, NULL, client.right - width - m_Theme.Scale(2), HeaderBottom(), width,
                         max(0, (int)client.bottom - HeaderBottom() - m_Theme.Scale(4)),
                         SWP_NOZORDER | SWP_NOACTIVATE |
                         (m_ContentHeight - HeaderBottom() > view ? SWP_SHOWWINDOW : SWP_HIDEWINDOW));
        }
        PositionControls();
    }

    RECT ScreenBox(const RECT &rc) const
    {
        RECT out = rc;
        OffsetRect(&out, 0, -m_Scroll);
        return out;
    }

    void PaintCaption(Canvas &canvas)
    {
        RECT back = BackRect(), forward = ForwardRect(), title;
        int size = m_Theme.Scale(16);

        if (m_HotBack && !m_Back.empty())
            GlassFill(canvas, back, m_Theme.Scale(4), GlassColor(m_Theme, GlassTone::Hover), CLR_NONE);
        if (m_HotForward && !m_Forward.empty())
            GlassFill(canvas, forward, m_Theme.Scale(4), GlassColor(m_Theme, GlassTone::Hover), CLR_NONE);
        CenterIcon(canvas, back, size, L"arrow_left_16_regular", !m_Back.empty() ? GlassColor(m_Theme, GlassTone::Text) : GlassColor(m_Theme, GlassTone::TextDisabled));
        CenterIcon(canvas, forward, size, L"arrow_right_16_regular",
                   !m_Forward.empty() ? GlassColor(m_Theme, GlassTone::Text) : GlassColor(m_Theme, GlassTone::TextDisabled));
        title.left = m_Theme.Scale(82);
        title.top = 0;
        title.right = PanelLeft();
        title.bottom = CaptionHeight();
        canvas.Text(Str(IDS_APP_TITLE).c_str(), title, m_Theme.Font(TextStyle::Caption),
                    IsActive() ? GlassColor(m_Theme, GlassTone::Text) : GlassColor(m_Theme, GlassTone::TextSecondary), DT_SINGLELINE | DT_VCENTER | DT_LEFT);
    }

    void CenterIcon(Canvas &canvas, const RECT &box, int size, LPCWSTR name, COLORREF color)
    {
        RECT glyph;
        glyph.left = box.left + (box.right - box.left - size) / 2;
        glyph.top = box.top + (box.bottom - box.top - size) / 2;
        glyph.right = glyph.left + size;
        glyph.bottom = glyph.top + size;
        canvas.Icon(name, glyph, color);
    }

    void PaintAccount(Canvas &canvas)
    {
        RECT tile = AccountRect(), avatar, name, kind;

        if (m_HotAccount || TopLevel(m_Page) == PageId::Accounts)
            GlassFill(canvas, tile, m_Theme.Scale(6), m_HotAccount ? GlassColor(m_Theme, GlassTone::Hover) : GlassColor(m_Theme, GlassTone::Selected), CLR_NONE);
        SetRect(&avatar, tile.left + m_Theme.Scale(6), tile.top + m_Theme.Scale(6), tile.left + m_Theme.Scale(54),
                tile.top + m_Theme.Scale(54));
        GlassFill(canvas, avatar, m_Theme.Scale(24), GlassColor(m_Theme, GlassTone::Selected), CLR_NONE);
        CenterIcon(canvas, avatar, m_Theme.Scale(24), L"person_20_regular", GlassColor(m_Theme, GlassTone::Text));
        SetRect(&name, avatar.right + m_Theme.Scale(12), tile.top + m_Theme.Scale(10), tile.right - m_Theme.Scale(8),
                tile.top + m_Theme.Scale(32));
        canvas.Text(m_User, name, m_Theme.Font(TextStyle::BodyStrong), GlassColor(m_Theme, GlassTone::Text),
                    DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);
        kind = name;
        kind.top = name.bottom;
        kind.bottom = kind.top + m_Theme.Scale(18);
        canvas.Text(Str(m_Admin ? IDS_ACCOUNT_ADMIN_LOCAL : IDS_ACCOUNT_STANDARD_LOCAL).c_str(), kind,
                    m_Theme.Font(TextStyle::Caption), GlassColor(m_Theme, GlassTone::TextSecondary), DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);
    }

    void PaintNavigation(Canvas &canvas, const RECT &client)
    {
        const Palette &c = m_Theme.Colors();
        std::vector<PageId> pages = Categories();
        PageId current = TopLevel(m_Page);
        HRGN clip = CreateRectRgn(0, NavTop() - m_Theme.Scale(4), PanelLeft(), client.bottom);

        SelectClipRgn(canvas.Dc(), clip);
        canvas.Graphics().SetClip(Gdiplus::Rect(0, NavTop() - m_Theme.Scale(4), PanelLeft(),
                                                client.bottom - NavTop() + m_Theme.Scale(4)));
        for (int i = 0; i < (int)pages.size(); i++)
        {
            const PageInfo *info = FindPage(pages[i]);
            RECT rc = NavRect(i), glyph, label;
            int size = m_Theme.Scale(18);

            if (rc.bottom < NavTop() - m_Theme.Scale(4) || rc.top > client.bottom)
                continue;
            if (pages[i] == current)
            {
                RECT pill = { rc.left, rc.top + m_Theme.Scale(10), rc.left + m_Theme.Scale(3), rc.bottom - m_Theme.Scale(10) };
                GlassFill(canvas, rc, m_Theme.Scale(6), GlassColor(m_Theme, GlassTone::Selected), CLR_NONE);
                canvas.FillRound(pill, m_Theme.ScaleF(1.5f), c.Accent);
            }
            else if (i == m_HotNav)
            {
                GlassFill(canvas, rc, m_Theme.Scale(6), GlassColor(m_Theme, GlassTone::Hover), CLR_NONE);
            }
            glyph.left = rc.left + m_Theme.Scale(14);
            glyph.top = rc.top + (rc.bottom - rc.top - size) / 2;
            glyph.right = glyph.left + size;
            glyph.bottom = glyph.top + size;
            canvas.Icon(info->Icon, glyph, GlassColor(m_Theme, GlassTone::Text));
            label = rc;
            label.left = rc.left + m_Theme.Scale(46);
            canvas.Text(Str(info->Title).c_str(), label, m_Theme.Font(TextStyle::Body), GlassColor(m_Theme, GlassTone::Text),
                        DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);
        }
        canvas.Graphics().ResetClip();
        SelectClipRgn(canvas.Dc(), NULL);
        DeleteObject(clip);
    }

    void PaintHeader(Canvas &canvas)
    {
        const Palette &c = m_Theme.Colors();
        std::vector<std::pair<PageId, std::wstring>> crumbs = Breadcrumb();
        RECT rc = { ContentLeft(), ContentTop() + m_Theme.Scale(16), ContentRight(), ContentTop() + m_Theme.Scale(60) };
        HFONT font = m_Theme.Font(TextStyle::Title);

        m_CrumbRects.clear();
        for (size_t i = 0; i < crumbs.size(); i++)
        {
            int width = MeasureWidth(crumbs[i].second, font);
            RECT part = { rc.left, rc.top, rc.left + width, rc.bottom }, chevron;
            canvas.Text(crumbs[i].second.c_str(), part, font, (int)i == m_HotCrumb ? c.Text : c.TextSecondary,
                        DT_SINGLELINE | DT_VCENTER | DT_LEFT);
            m_CrumbRects.push_back(part);
            SetRect(&chevron, part.right + m_Theme.Scale(10), (rc.top + rc.bottom) / 2 - m_Theme.Scale(7),
                    part.right + m_Theme.Scale(24), (rc.top + rc.bottom) / 2 + m_Theme.Scale(7));
            canvas.Icon(L"chevron_right_16_regular", chevron, c.TextSecondary);
            rc.left = chevron.right + m_Theme.Scale(10);
        }
        canvas.Text(m_Title.c_str(), rc, font, c.Text, DT_SINGLELINE | DT_VCENTER | DT_LEFT | DT_END_ELLIPSIS);
    }

    void PaintContent(Canvas &canvas, const RECT &client)
    {
        const Palette &c = m_Theme.Colors();
        int top = HeaderBottom() - m_Theme.Scale(8);
        HRGN clip = CreateRectRgn(ContentLeft() - m_Theme.Scale(8), top, client.right, client.bottom);

        PaintHeader(canvas);
        SelectClipRgn(canvas.Dc(), clip);
        canvas.Graphics().SetClip(Gdiplus::Rect(ContentLeft() - m_Theme.Scale(8), top,
                                                client.right - ContentLeft() + m_Theme.Scale(8), client.bottom - top));
        for (size_t g = 0; g < m_Groups.size(); g++)
        {
            RECT title = ScreenBox(m_GroupTitles[g]), box = ScreenBox(m_GroupBoxes[g]);
            if (box.bottom < top || title.top > client.bottom)
                continue;
            if (!m_Groups[g].Title.empty())
                canvas.Text(m_Groups[g].Title.c_str(), title, m_Theme.Font(TextStyle::BodyStrong), c.Text,
                            DT_SINGLELINE | DT_BOTTOM | DT_LEFT | DT_END_ELLIPSIS);
            if (m_Groups[g].Rows.empty())
                continue;
            canvas.FillRound(box, m_Theme.ScaleF(8.0f), c.Card);
            canvas.FrameRound(box, m_Theme.ScaleF(8.0f), c.CardBorder);
            if (!m_Groups[g].Footer.empty())
            {
                RECT footer = { box.left + m_Theme.Scale(4), box.bottom + m_Theme.Scale(6), box.right, box.bottom + m_Theme.Scale(28) };
                canvas.Text(m_Groups[g].Footer.c_str(), footer, m_Theme.Font(TextStyle::Caption), c.TextSecondary,
                            DT_SINGLELINE | DT_VCENTER | DT_LEFT | DT_END_ELLIPSIS);
            }
        }
        for (size_t i = 0; i < m_Places.size(); i++)
        {
            RECT box = ScreenBox(m_Places[i].Box);
            if (box.bottom < top || box.top > client.bottom)
                continue;
            PaintRow(canvas, (int)i);
        }
        canvas.Graphics().ResetClip();
        SelectClipRgn(canvas.Dc(), NULL);
        DeleteObject(clip);
    }

    void PaintRow(Canvas &canvas, int place)
    {
        const Palette &c = m_Theme.Colors();
        RowPlace &where = m_Places[place];
        SettingRow &row = m_Groups[where.Group].Rows[where.Row];
        RECT rc = ScreenBox(where.Box), control = ScreenBox(where.Control), text;
        COLORREF title = row.Enabled ? c.Text : c.TextDisabled, detail = row.Enabled ? c.TextSecondary : c.TextDisabled;
        int textLeft = rc.left + m_Theme.Scale(16), textRight;

        if (where.Row > 0)
        {
            RECT line = { rc.left + m_Theme.Scale(16), rc.top, rc.right - m_Theme.Scale(16), rc.top + 1 };
            canvas.Fill(line, c.Divider);
        }
        if (row.Kind == RowKind::Navigate || row.Kind == RowKind::Link)
            return;
        if (row.Kind == RowKind::Hero)
        {
            PaintHero(canvas, place, row, rc);
            return;
        }
        if (row.Kind == RowKind::Text)
        {
            text = rc;
            InflateRect(&text, -m_Theme.Scale(16), -m_Theme.Scale(14));
            canvas.Text(row.Title.c_str(), text, m_Theme.Font(TextStyle::Body), detail, DT_WORDBREAK | DT_LEFT);
            return;
        }
        if (row.Image || row.Icon)
        {
            RECT glyph;
            int size = m_Theme.Scale(20);
            glyph.left = rc.left + m_Theme.Scale(18);
            glyph.top = rc.top + (rc.bottom - rc.top - size) / 2;
            glyph.right = glyph.left + size;
            glyph.bottom = glyph.top + size;
            if (row.Image)
            {
                canvas.Graphics().Flush(Gdiplus::FlushIntentionSync);
                DrawIconEx(canvas.Dc(), glyph.left - m_Theme.Scale(2), glyph.top - m_Theme.Scale(2), row.Image,
                           size + m_Theme.Scale(4), size + m_Theme.Scale(4), 0, NULL, DI_NORMAL);
            }
            else
                canvas.Icon(row.Icon, glyph, row.Enabled ? c.Glyph : c.TextDisabled);
            textLeft = rc.left + m_Theme.Scale(56);
        }
        if (m_GroupIcons[where.Group])
            textLeft = rc.left + m_Theme.Scale(56);
        textRight = !IsRectEmpty(&where.Control) ? control.left - m_Theme.Scale(16) : rc.right - m_Theme.Scale(16);
        if (row.Kind == RowKind::Info)
        {
            int valueWidth = row.Value.empty() ? 0 : min(MeasureWidth(row.Value, m_Theme.Font(TextStyle::Body)),
                                                          (rc.right - textLeft) / 2);
            RECT value = { textRight - valueWidth, rc.top, textRight, rc.bottom };
            value.right = rc.right - m_Theme.Scale(16);
            value.left = value.right - valueWidth;
            if (valueWidth)
            {
                canvas.Text(row.Value.c_str(), value, m_Theme.Font(TextStyle::Body), detail,
                            DT_SINGLELINE | DT_VCENTER | DT_RIGHT | DT_END_ELLIPSIS);
                textRight = value.left - m_Theme.Scale(16);
            }
        }
        if (row.Kind == RowKind::Slider)
        {
            std::wstring label = row.Label ? row.Label(row.Position) : row.Value;
            if (!label.empty())
            {
                int width = MeasureWidth(label, m_Theme.Font(TextStyle::Body));
                RECT value = { control.left - m_Theme.Scale(12) - width, rc.top, control.left - m_Theme.Scale(12), rc.bottom };
                canvas.Text(label.c_str(), value, m_Theme.Font(TextStyle::Body), detail,
                            DT_SINGLELINE | DT_VCENTER | DT_RIGHT);
                textRight = value.left - m_Theme.Scale(16);
            }
        }
        if (row.Detail.empty())
        {
            SetRect(&text, textLeft, rc.top, textRight, rc.bottom);
            canvas.Text(row.Title.c_str(), text, m_Theme.Font(TextStyle::Body), title,
                        DT_SINGLELINE | DT_VCENTER | DT_LEFT | DT_END_ELLIPSIS);
        }
        else
        {
            int middle = (rc.top + rc.bottom) / 2 - (row.Bar ? m_Theme.Scale(10) : 0);
            SetRect(&text, textLeft, middle - m_Theme.Scale(21), textRight, middle);
            canvas.Text(row.Title.c_str(), text, m_Theme.Font(TextStyle::Body), title,
                        DT_SINGLELINE | DT_BOTTOM | DT_LEFT | DT_END_ELLIPSIS);
            SetRect(&text, textLeft, middle + m_Theme.Scale(1), textRight, middle + m_Theme.Scale(20));
            canvas.Text(row.Detail.c_str(), text, m_Theme.Font(TextStyle::Caption), detail,
                        DT_SINGLELINE | DT_TOP | DT_LEFT | DT_END_ELLIPSIS);
        }
    }

    void PaintHero(Canvas &canvas, int place, SettingRow &row, const RECT &rc)
    {
        const Palette &c = m_Theme.Colors();
        RECT badge = { rc.left + m_Theme.Scale(16), rc.top + m_Theme.Scale(16), rc.left + m_Theme.Scale(80),
                       rc.bottom - m_Theme.Scale(16) }, text;
        int textRight = rc.right - m_Theme.Scale(16);

        if (row.Image)
        {
            canvas.Graphics().Flush(Gdiplus::FlushIntentionSync);
            DrawIconEx(canvas.Dc(), badge.left, badge.top, row.Image, badge.right - badge.left,
                       badge.bottom - badge.top, 0, NULL, DI_NORMAL);
        }
        else
        {
            canvas.FillEllipse(badge, c.Accent);
            CenterIcon(canvas, badge, m_Theme.Scale(28), row.Icon ? row.Icon : L"info_20_regular", c.AccentText);
        }
        if (!m_Places[place].Actions.empty())
            textRight = ScreenBox(m_Places[place].Actions.front()).left - m_Theme.Scale(16);
        SetRect(&text, badge.right + m_Theme.Scale(16), rc.top + m_Theme.Scale(20), textRight, rc.top + m_Theme.Scale(50));
        canvas.Text(row.Title.c_str(), text, m_Theme.Font(TextStyle::Subtitle), c.Text,
                    DT_SINGLELINE | DT_VCENTER | DT_LEFT | DT_END_ELLIPSIS);
        SetRect(&text, text.left, text.bottom, textRight, rc.bottom - m_Theme.Scale(16));
        canvas.Text(row.Detail.c_str(), text, m_Theme.Font(TextStyle::Body), c.TextSecondary,
                    DT_SINGLELINE | DT_TOP | DT_LEFT | DT_END_ELLIPSIS);
    }

    int NavFromPoint(int x, int y) const
    {
        POINT pt = { x, y };
        std::vector<PageId> pages = Categories();

        if (y < NavTop() - m_Theme.Scale(4))
            return -1;
        for (int i = 0; i < (int)pages.size(); i++)
        {
            RECT rc = NavRect(i);
            if (PtInRect(&rc, pt))
                return i;
        }
        return -1;
    }

    int CrumbFromPoint(int x, int y) const
    {
        POINT pt = { x, y };
        for (size_t i = 0; i < m_CrumbRects.size(); i++)
        {
            if (PtInRect(&m_CrumbRects[i], pt))
                return (int)i;
        }
        return -1;
    }

    void SetHot(int nav, BOOL back, BOOL forward, BOOL account, int crumb)
    {
        if (nav == m_HotNav && back == m_HotBack && forward == m_HotForward && account == m_HotAccount &&
            crumb == m_HotCrumb)
            return;
        m_HotNav = nav;
        m_HotBack = back;
        m_HotForward = forward;
        m_HotAccount = account;
        m_HotCrumb = crumb;
        Invalidate(NULL);
    }

    void OnMouseMove(int x, int y)
    {
        RECT back = BackRect(), forward = ForwardRect(), account = AccountRect();
        POINT pt = { x, y };

        if (!m_TrackingClient)
        {
            TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, m_hwnd, 0 };
            m_TrackingClient = TrackMouseEvent(&tme);
        }
        SetHot(NavFromPoint(x, y), PtInRect(&back, pt), PtInRect(&forward, pt), PtInRect(&account, pt),
               CrumbFromPoint(x, y));
    }

    void OnButtonUp(int x, int y)
    {
        RECT back = BackRect(), forward = ForwardRect(), account = AccountRect();
        POINT pt = { x, y };
        int nav = NavFromPoint(x, y), crumb = CrumbFromPoint(x, y);

        if (PtInRect(&back, pt))
        {
            GoBack();
            return;
        }
        if (PtInRect(&forward, pt))
        {
            GoForward();
            return;
        }
        if (PtInRect(&account, pt))
        {
            Navigate(PageId::Accounts, std::wstring(), TRUE);
            return;
        }
        if (nav >= 0)
        {
            Navigate(Categories()[nav], std::wstring(), TRUE);
            return;
        }
        if (crumb >= 0)
        {
            std::vector<std::pair<PageId, std::wstring>> crumbs = Breadcrumb();
            if (crumb < (int)crumbs.size())
                Navigate(crumbs[crumb].first, std::wstring(), TRUE);
        }
    }

    HBRUSH CardBrush()
    {
        COLORREF color = m_Theme.Colors().Card;

        if (!m_CardBrush || color != m_CardColor)
        {
            if (m_CardBrush)
                DeleteObject(m_CardBrush);
            m_CardBrush = CreateSolidBrush(color);
            m_CardColor = color;
        }
        return m_CardBrush;
    }

    int ControlIndex(HWND hwnd) const
    {
        for (size_t i = 0; hwnd && i < m_Controls.size(); i++)
        {
            if (m_Controls[i].Window == hwnd)
                return (int)i;
        }
        return -1;
    }

    RECT ControlRect(const RowControl &control) const
    {
        const RowPlace &where = m_Places[control.Place];
        RECT rc;

        if (control.Action >= 0)
            rc = where.Actions[control.Action];
        else if (control.Action == BAR_ACTION)
            rc = where.Bar;
        else
            rc = where.Control;
        if (control.Height)
        {
            rc.top = (rc.top + rc.bottom - control.Height) / 2;
            rc.bottom = rc.top + control.Height;
        }
        return rc;
    }

    BOOL InControls(const RECT &box) const
    {
        for (const RowControl &control : m_Controls)
        {
            RECT rc = ControlRect(control), overlap;

            OffsetRect(&rc, 0, -m_Scroll);
            if (IntersectRect(&overlap, &rc, &box))
                return TRUE;
        }
        return FALSE;
    }

    HWND AddControl(LPCWSTR className, LPCWSTR text, DWORD style, int place, int action)
    {
        HWND hwnd = CreateWindowExW(0, className, text, WS_CHILD | WS_CLIPSIBLINGS | style, 0, 0, 0, 0, m_hwnd,
                                    (HMENU)(INT_PTR)(IDC_ROW_FIRST + m_Controls.size()), Instance, NULL);
        RowControl control = { hwnd, place, action, 0, NULL };

        if (!hwnd)
            return NULL;
        SendMessageW(hwnd, WM_SETFONT, (WPARAM)m_Theme.Font(TextStyle::Body), FALSE);
        m_Controls.push_back(control);
        return hwnd;
    }

    void DestroyControls()
    {
        for (const RowControl &control : m_Controls)
        {
            DestroyWindow(control.Window);
            if (control.Image)
                DestroyIcon(control.Image);
        }
        m_Controls.clear();
    }

    void CreateControls()
    {
        const Palette &c = m_Theme.Colors();

        DestroyControls();
        for (size_t i = 0; i < m_Places.size(); i++)
        {
            const RowPlace &where = m_Places[i];
            const SettingRow &row = m_Groups[where.Group].Rows[where.Row];
            std::vector<std::wstring> actions = row.Actions;
            HWND hwnd = NULL;

            if (!row.Action.empty())
                actions.insert(actions.begin(), row.Action);
            for (size_t a = 0; a < where.Actions.size() && a < actions.size(); a++)
            {
                HWND button = AddControl(WC_BUTTONW, actions[a].c_str(), WS_TABSTOP |
                                         (row.Accent && a == 0 ? BS_DEFPUSHBUTTON : BS_PUSHBUTTON), (int)i, (int)a);
                if (button && !row.Enabled)
                    EnableWindow(button, FALSE);
            }
            switch (row.Kind)
            {
                case RowKind::Toggle:
                    hwnd = AddControl(WC_BUTTONW, L"", WS_TABSTOP | BS_CHECKBOX, (int)i, -1);
                    if (hwnd)
                        SendMessageW(hwnd, BM_SETCHECK, row.Checked ? BST_CHECKED : BST_UNCHECKED, 0);
                    break;
                case RowKind::Choice:
                    hwnd = AddControl(WC_COMBOBOXW, L"", WS_TABSTOP | WS_VSCROLL | CBS_DROPDOWNLIST, (int)i, -1);
                    if (hwnd)
                    {
                        RECT rc = where.Control, field;

                        for (const std::wstring &option : row.Options)
                            SendMessageW(hwnd, CB_ADDSTRING, 0, (LPARAM)option.c_str());
                        SendMessageW(hwnd, CB_SETCURSEL, row.Selected, 0);
                        SetWindowPos(hwnd, NULL, 0, 0, rc.right - rc.left, m_Theme.Scale(240), SWP_NOZORDER | SWP_NOMOVE);
                        GetWindowRect(hwnd, &field);
                        m_Controls.back().Height = field.bottom - field.top;
                    }
                    break;
                case RowKind::Slider:
                    hwnd = AddControl(TRACKBAR_CLASSW, row.Title.c_str(), WS_TABSTOP | TBS_HORZ | TBS_NOTICKS, (int)i, -1);
                    if (hwnd)
                    {
                        SendMessageW(hwnd, TBM_SETRANGEMIN, FALSE, row.Min);
                        SendMessageW(hwnd, TBM_SETRANGEMAX, FALSE, row.Max);
                        SendMessageW(hwnd, TBM_SETPAGESIZE, 0, max(1, (row.Max - row.Min) / 10));
                        SendMessageW(hwnd, TBM_SETPOS, TRUE, row.Position);
                    }
                    break;
                case RowKind::Navigate:
                case RowKind::Link:
                    hwnd = AddControl(WC_BUTTONW, row.Title.c_str(), WS_TABSTOP | BS_COMMANDLINK, (int)i, -1);
                    if (hwnd)
                    {
                        std::wstring note = row.Detail;
                        LPCWSTR icon = row.Icon ? row.Icon : (row.Kind == RowKind::Link ? L"open_16_regular" : NULL);

                        if (!row.Value.empty())
                            note = note.empty() ? row.Value : note + L" \x00B7 " + row.Value;
                        if (!note.empty())
                            SendMessageW(hwnd, BCM_SETNOTE, 0, (LPARAM)note.c_str());
                        if (row.Image)
                            SendMessageW(hwnd, BM_SETIMAGE, IMAGE_ICON, (LPARAM)row.Image);
                        else if (icon)
                        {
                            m_Controls.back().Image = CreateGlyphIcon(icon, m_Theme.Scale(20), c.Glyph);
                            SendMessageW(hwnd, BM_SETIMAGE, IMAGE_ICON, (LPARAM)m_Controls.back().Image);
                        }
                    }
                    break;
                default:
                    break;
            }
            if (hwnd && !row.Enabled)
                EnableWindow(hwnd, FALSE);
            if (row.Bar)
            {
                HWND bar = AddControl(PROGRESS_CLASSW, L"", 0, (int)i, BAR_ACTION);
                if (bar)
                {
                    SendMessageW(bar, PBM_SETRANGE32, 0, max(1, row.Max));
                    SendMessageW(bar, PBM_SETPOS, max(0, min(row.Max, row.Position)), 0);
                    if (row.Position * 10 >= row.Max * 9)
                        SendMessageW(bar, PBM_SETSTATE, PBST_ERROR, 0);
                    SetWindowPos(bar, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
                }
            }
        }
        PositionControls();
    }

    void PositionControls()
    {
        RECT client, view;
        HDWP defer;

        if (m_Controls.empty())
            return;
        GetClientRect(m_hwnd, &client);
        SetRect(&view, ContentLeft() - m_Theme.Scale(8), HeaderBottom() - m_Theme.Scale(8), client.right, client.bottom);
        defer = BeginDeferWindowPos((int)m_Controls.size());
        for (const RowControl &control : m_Controls)
        {
            RECT rc = ControlRect(control), visible;
            BOOL shown;
            int height;

            OffsetRect(&rc, 0, -m_Scroll);
            shown = IntersectRect(&visible, &rc, &view);
            if (shown)
            {
                OffsetRect(&visible, -rc.left, -rc.top);
                SetWindowRgn(control.Window, CreateRectRgnIndirect(&visible), FALSE);
            }
            height = control.Height ? m_Theme.Scale(240) : rc.bottom - rc.top;
            if (defer)
                defer = DeferWindowPos(defer, control.Window, control.Action == BAR_ACTION ? HWND_TOP : NULL, rc.left,
                                       rc.top, rc.right - rc.left, height,
                                       (control.Action == BAR_ACTION ? 0 : SWP_NOZORDER) | SWP_NOACTIVATE |
                                       (shown ? SWP_SHOWWINDOW : SWP_HIDEWINDOW));
        }
        if (defer)
            EndDeferWindowPos(defer);
    }

    void OnScrollBar(UINT code)
    {
        SCROLLINFO info = { sizeof(info), SIF_ALL };
        RECT client;
        int line = m_Theme.Scale(48);

        GetScrollInfo(m_ScrollBar, SB_CTL, &info);
        GetClientRect(m_hwnd, &client);
        switch (code)
        {
            case SB_LINEUP:
                m_Scroll -= line;
                break;
            case SB_LINEDOWN:
                m_Scroll += line;
                break;
            case SB_PAGEUP:
                m_Scroll -= (int)info.nPage;
                break;
            case SB_PAGEDOWN:
                m_Scroll += (int)info.nPage;
                break;
            case SB_THUMBTRACK:
            case SB_THUMBPOSITION:
                m_Scroll = info.nTrackPos;
                break;
            case SB_TOP:
                m_Scroll = 0;
                break;
            case SB_BOTTOM:
                m_Scroll = m_ContentHeight;
                break;
            default:
                return;
        }
        ClampScroll();
        Invalidate(NULL);
    }

    void OnSlider(int index, UINT code)
    {
        const RowControl &control = m_Controls[index];
        RowPlace &where = m_Places[control.Place];
        SettingRow &row = m_Groups[where.Group].Rows[where.Row];
        int value = (int)SendMessageW(control.Window, TBM_GETPOS, 0, 0);

        if (code == TB_ENDTRACK)
        {
            PostMessageW(m_hwnd, WM_SETTINGS_CONTROL, index, value);
            return;
        }
        if (value == row.Position)
            return;
        row.Position = value;
        if (row.Live && row.Change)
            row.Change(value);
        Invalidate(NULL);
    }

    void ActivateControl(int index, int value)
    {
        RowControl control;
        RowPlace where;

        if (index < 0 || index >= (int)m_Controls.size())
            return;
        control = m_Controls[index];
        where = m_Places[control.Place];
        {
            SettingRow &row = m_Groups[where.Group].Rows[where.Row];

            if (!row.Enabled)
                return;
            if (control.Action >= 0)
            {
                std::function<void(int)> invoke = row.Invoke;
                if (invoke)
                    invoke(control.Action);
                Rebuild(FALSE);
                return;
            }
            switch (row.Kind)
            {
                case RowKind::Navigate:
                    if (row.Invoke)
                    {
                        std::function<void(int)> invoke = row.Invoke;
                        invoke(0);
                    }
                    else if (row.Target != PageId::None)
                    {
                        PageId target = row.Target;
                        std::wstring param = row.Param;
                        Navigate(target, param, TRUE);
                    }
                    return;
                case RowKind::Link:
                    if (row.Invoke)
                    {
                        std::function<void(int)> invoke = row.Invoke;
                        invoke(0);
                    }
                    return;
                case RowKind::Toggle:
                    if (row.Change)
                    {
                        std::function<void(int)> change = row.Change;
                        change(row.Checked ? 0 : 1);
                    }
                    Rebuild(FALSE);
                    return;
                case RowKind::Choice:
                    if (value >= 0 && value != row.Selected && row.Change)
                    {
                        std::function<void(int)> change = row.Change;
                        change(value);
                    }
                    Rebuild(FALSE);
                    return;
                case RowKind::Slider:
                    if (!row.Live && row.Change)
                    {
                        std::function<void(int)> change = row.Change;
                        change(value);
                    }
                    Rebuild(FALSE);
                    return;
                default:
                    return;
            }
        }
    }

    BOOL OnKey(UINT key)
    {
        RECT client;
        int page;

        GetClientRect(m_hwnd, &client);
        page = max(m_Theme.Scale(64), (int)client.bottom - HeaderBottom() - m_Theme.Scale(48));
        switch (key)
        {
            case VK_BACK:
                GoBack();
                return TRUE;
            case VK_F3:
                SetFocus(m_Search);
                return TRUE;
            case 'F':
                if (GetKeyState(VK_CONTROL) < 0)
                {
                    SetFocus(m_Search);
                    SendMessageW(m_Search, EM_SETSEL, 0, -1);
                    return TRUE;
                }
                return FALSE;
            case VK_UP:
                m_Scroll -= m_Theme.Scale(48);
                break;
            case VK_DOWN:
                m_Scroll += m_Theme.Scale(48);
                break;
            case VK_PRIOR:
                m_Scroll -= page;
                break;
            case VK_NEXT:
                m_Scroll += page;
                break;
            case VK_HOME:
                m_Scroll = 0;
                break;
            case VK_END:
                m_Scroll = m_ContentHeight;
                break;
            default:
                return FALSE;
        }
        ClampScroll();
        Invalidate(NULL);
        return TRUE;
    }

    void BuildIndex()
    {
        UINT count;
        const PageInfo *table = PageTable(&count);

        if (m_IndexReady)
            return;
        for (UINT i = 0; i < count; i++)
        {
            PageBuilder page(this, std::wstring(), true);
            std::wstring path;
            const PageInfo *parent;
            SearchEntry entry;

            if (!table[i].Build || table[i].Id == PageId::Search)
                continue;
            table[i].Build(page);
            for (parent = FindPage(table[i].Parent); parent; parent = FindPage(parent->Parent))
                path = Str(parent->Title) + (path.empty() ? std::wstring() : L" \x203A " + path);
            entry.Page = table[i].Id;
            entry.Title = Str(table[i].Title);
            entry.Path = path;
            entry.Icon = table[i].Icon;
            m_Index.push_back(entry);
            for (size_t g = 0; g < page.Groups.size(); g++)
            {
                for (size_t r = 0; r < page.Groups[g].Rows.size(); r++)
                {
                    const SettingRow &row = page.Groups[g].Rows[r];
                    if (row.Kind == RowKind::Text || row.Kind == RowKind::Hero || row.Title.empty() || row.Image)
                        continue;
                    entry.Title = row.Title;
                    entry.Detail = row.Detail;
                    entry.Keywords = row.Keywords;
                    entry.Path = path.empty() ? Str(table[i].Title) : path + L" \x203A " + Str(table[i].Title);
                    entry.Icon = row.Icon ? row.Icon : table[i].Icon;
                    entry.Page = row.Kind == RowKind::Navigate && row.Target != PageId::None && row.Param.empty() ?
                                 row.Target : table[i].Id;
                    m_Index.push_back(entry);
                }
            }
        }
        m_IndexReady = true;
    }

    void BuildSearch(PageBuilder &page)
    {
        WCHAR query[128];
        std::vector<std::wstring> seen;
        int matches = 0;

        GetWindowTextW(m_Search, query, _countof(query));
        StrTrimW(query, L" ");
        page.Title = Str(IDS_SEARCH_RESULTS);
        page.Group();
        BuildIndex();
        for (size_t i = 0; i < m_Index.size() && matches < 60; i++)
        {
            const SearchEntry &entry = m_Index[i];
            std::wstring key = entry.Title + L"|" + entry.Path;
            if (!Contains(entry.Title, query) && !Contains(entry.Detail, query) && !Contains(entry.Keywords, query))
                continue;
            if (std::find(seen.begin(), seen.end(), key) != seen.end())
                continue;
            seen.push_back(key);
            page.Navigate(entry.Title, entry.Path, entry.Icon, entry.Page);
            matches++;
        }
        if (!matches)
            page.Text(Str(IDS_NO_RESULTS));
    }

    void OnSearchChanged()
    {
        WCHAR query[128];

        GetWindowTextW(m_Search, query, _countof(query));
        StrTrimW(query, L" ");
        if (!query[0])
        {
            if (m_Page == PageId::Search)
            {
                if (!m_Back.empty())
                    GoBack();
                else
                {
                    m_Page = PageId::System;
                    Rebuild(TRUE);
                }
            }
            return;
        }
        if (m_Page != PageId::Search)
        {
            m_Back.push_back(std::make_pair(m_Page, m_Param));
            m_Forward.clear();
            m_Page = PageId::Search;
            m_Param.clear();
        }
        Rebuild(TRUE);
    }

    void OpenFirstResult()
    {
        for (size_t i = 0; i < m_Places.size(); i++)
        {
            SettingRow &row = m_Groups[m_Places[i].Group].Rows[m_Places[i].Row];
            if (m_Page == PageId::Search && row.Kind == RowKind::Navigate)
            {
                PageId target = row.Target;
                Navigate(target, std::wstring(), TRUE);
                return;
            }
        }
    }

    PageId m_Page;
    std::wstring m_Param;
    std::wstring m_Title;
    std::vector<std::pair<PageId, std::wstring>> m_Back;
    std::vector<std::pair<PageId, std::wstring>> m_Forward;
    std::vector<SettingGroup> m_Groups;
    std::vector<RowPlace> m_Places;
    std::vector<RECT> m_GroupBoxes;
    std::vector<RECT> m_GroupTitles;
    std::vector<bool> m_GroupIcons;
    std::vector<RECT> m_CrumbRects;
    std::vector<SearchEntry> m_Index;
    int m_Scroll;
    int m_NavScroll = 0;
    int m_ContentHeight;
    int m_HotNav;
    BOOL m_HotBack;
    BOOL m_HotForward;
    BOOL m_HotAccount;
    int m_HotCrumb;
    BOOL m_TrackingClient;
    std::vector<RowControl> m_Controls;
    BOOL m_Admin = FALSE;
    HANDLE m_AddressNotify = NULL;
    HANDLE m_InterfaceNotify = NULL;
    HANDLE m_Wlan = NULL;
    HWND m_Search;
    HWND m_ScrollBar;
    HBRUSH m_CardBrush;
    COLORREF m_CardColor;
    HFONT m_SearchFont;
    HICON m_SmallIcon;
    HICON m_BigIcon;
    WCHAR m_User[UNLEN + 1];
    bool m_IndexReady;
};

static void
PageFromCommandLine(LPCWSTR commandLine, PageId *page, std::wstring *param)
{
    LPCWSTR uri = commandLine ? StrStrIW(commandLine, L"ms-settings:") : NULL;
    UINT count;
    const PageInfo *table = PageTable(&count);
    WCHAR name[96];
    size_t length = 0;

    *page = PageId::System;
    param->clear();
    if (!uri)
        return;
    uri += lstrlenW(L"ms-settings:");
    while (uri[length] && uri[length] != L'"' && uri[length] != L' ' && uri[length] != L'?' && length + 1 < _countof(name))
    {
        name[length] = uri[length];
        length++;
    }
    name[length] = UNICODE_NULL;
    if (!name[0])
        return;
    for (UINT i = 0; i < count; i++)
    {
        LPCWSTR list = table[i].Uri;
        while (list && *list)
        {
            LPCWSTR end = StrChrW(list, L';');
            size_t part = end ? (size_t)(end - list) : wcslen(list);
            if (part == length && !StrCmpNIW(list, name, (int)part))
            {
                *page = table[i].Id;
                return;
            }
            list = end ? end + 1 : NULL;
        }
    }
}

int WINAPI
wWinMain(HINSTANCE instance, HINSTANCE previous, LPWSTR commandLine, int showCmd)
{
    INITCOMMONCONTROLSEX icc = { sizeof(icc), ICC_STANDARD_CLASSES | ICC_DATE_CLASSES };
    SettingsWindow window;
    PageId page;
    std::wstring param;
    HWND existing;
    MSG msg;

    UNREFERENCED_PARAMETER(previous);
    Instance = instance;
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    existing = FindWindowW(L"LiberNTSettingsWindow", NULL);
    if (existing)
    {
        COPYDATASTRUCT data = { 1, (DWORD)((lstrlenW(commandLine) + 1) * sizeof(WCHAR)), commandLine };
        if (IsIconic(existing))
            ShowWindow(existing, SW_RESTORE);
        SetForegroundWindow(existing);
        SendMessageW(existing, WM_COPYDATA, 0, (LPARAM)&data);
        return 0;
    }
    PageFromCommandLine(commandLine, &page, &param);
    window.SetInitialPage(page, param);
    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    InitCommonControlsEx(&icc);
    if (!Startup())
        return 1;
    if (!window.Create(L"LiberNTSettingsWindow", Str(IDS_APP_TITLE).c_str(), NULL, 1100, 760, showCmd))
    {
        Shutdown();
        return 1;
    }
    while (GetMessageW(&msg, NULL, 0, 0))
    {
        if (msg.message == WM_KEYDOWN && msg.wParam == VK_TAB && IsDialogMessageW(window.Window(), &msg))
            continue;
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    Shutdown();
    CoUninitialize();
    return (int)msg.wParam;
}
