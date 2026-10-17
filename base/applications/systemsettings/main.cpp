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
#define TIMER_REFRESH 1
#define TIMER_SETTING 2

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

enum class Part
{
    None,
    Row,
    Control,
    Action
};

struct RowPlace
{
    int Group;
    int Row;
    RECT Box;
    RECT Control;
    std::vector<RECT> Actions;
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
        m_HotPlace(-1),
        m_HotPart(Part::None),
        m_HotAction(-1),
        m_PressedPlace(-1),
        m_PressedPart(Part::None),
        m_PressedAction(-1),
        m_DragPlace(-1),
        m_HotBack(FALSE),
        m_HotForward(FALSE),
        m_HotAccount(FALSE),
        m_HotCrumb(-1),
        m_DragScroll(FALSE),
        m_DragOffset(0),
        m_TrackingClient(FALSE),
        m_Search(NULL),
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

        m_Search = CreateWindowExW(0, WC_EDITW, L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL, 0, 0, 0, 0,
                                   m_hwnd, (HMENU)IDC_SEARCH, Instance, NULL);
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
        PaintSearch(canvas);
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
                if (m_DragPlace < 0 && !m_DragScroll)
                    SetHot(-1, -1, Part::None, -1, FALSE, FALSE, FALSE, -1);
                break;
            case WM_LBUTTONDOWN:
                OnButtonDown(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
                return 0;
            case WM_LBUTTONUP:
                OnButtonUp(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
                return 0;
            case WM_CAPTURECHANGED:
                if (m_DragPlace >= 0 && (HWND)lParam != m_hwnd)
                    EndSliderDrag(FALSE);
                m_DragScroll = FALSE;
                break;
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
                if (LOWORD(wParam) == IDC_SEARCH && HIWORD(wParam) == EN_CHANGE)
                    OnSearchChanged();
                if (LOWORD(wParam) == IDC_SEARCH && (HIWORD(wParam) == EN_SETFOCUS || HIWORD(wParam) == EN_KILLFOCUS))
                    Invalidate(NULL);
                break;
            case WM_CTLCOLOREDIT:
                SetTextColor((HDC)wParam, GlassColor(m_Theme, GlassTone::Text));
                SetBkColor((HDC)wParam, GlassColor(m_Theme, GlassTone::Clear));
                SetDCBrushColor((HDC)wParam, GlassColor(m_Theme, GlassTone::Clear));
                return (LRESULT)GetStockObject(DC_BRUSH);
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
                info->ptMinTrackSize.x = m_Theme.Scale(760);
                info->ptMinTrackSize.y = m_Theme.Scale(560);
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
        RECT rc = { m_Theme.Scale(16), m_Theme.Scale(56), m_Theme.Scale(296), m_Theme.Scale(88) };
        return rc;
    }

    RECT AccountRect() const
    {
        RECT rc = { m_Theme.Scale(12), m_Theme.Scale(100), m_Theme.Scale(300), m_Theme.Scale(160) };
        return rc;
    }

    int NavTop() const { return m_Theme.Scale(172); }

    RECT NavRect(int index) const
    {
        RECT rc;
        rc.left = m_Theme.Scale(12);
        rc.right = m_Theme.Scale(300);
        rc.top = NavTop() + index * m_Theme.Scale(38) - m_NavScroll;
        rc.bottom = rc.top + m_Theme.Scale(36);
        return rc;
    }

    int ContentLeft() const { return m_Theme.Scale(328); }
    int PanelLeft() const { return ContentLeft() - m_Theme.Scale(16); }
    int PanelRadius() const { return m_Theme.Scale(8); }

    int ContentRight() const
    {
        RECT client;
        GetClientRect(m_hwnd, &client);
        return min((int)client.right - m_Theme.Scale(32), ContentLeft() + m_Theme.Scale(1000));
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
            m_HotPlace = -1;
            m_HotPart = Part::None;
        }
        m_PressedPlace = -1;
        m_PressedPart = Part::None;
        LayoutContent();
        if (m_HotPlace >= (int)m_Places.size())
            m_HotPlace = -1;
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
                LayoutControl(row, place);
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

    void LayoutControl(const SettingRow &row, RowPlace &place)
    {
        RECT &rc = place.Box, &control = place.Control;
        int middle = (rc.top + rc.bottom) / 2, right = rc.right - m_Theme.Scale(16);

        switch (row.Kind)
        {
            case RowKind::Toggle:
                SetRect(&control, right - m_Theme.Scale(40), middle - m_Theme.Scale(10), right, middle + m_Theme.Scale(10));
                break;
            case RowKind::Choice:
            {
                int width = m_Theme.Scale(120);
                for (size_t i = 0; i < row.Options.size(); i++)
                    width = max(width, MeasureWidth(row.Options[i], m_Theme.Font(TextStyle::Body)) + m_Theme.Scale(48));
                width = min(width, m_Theme.Scale(300));
                SetRect(&control, right - width, middle - m_Theme.Scale(16), right, middle + m_Theme.Scale(16));
                break;
            }
            case RowKind::Slider:
                SetRect(&control, right - m_Theme.Scale(220), middle - m_Theme.Scale(12), right, middle + m_Theme.Scale(12));
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
                    RECT button = { right - width, middle - m_Theme.Scale(16), right, middle + m_Theme.Scale(16) };
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
                SetRect(&control, right - m_Theme.Scale(16), middle - m_Theme.Scale(8), right, middle + m_Theme.Scale(8));
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
        navHeight = (int)Categories().size() * m_Theme.Scale(38);
        m_NavScroll = max(0, min(m_NavScroll, max(0, navHeight - navView)));
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
        title.right = m_Theme.Scale(300);
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

    void PaintSearch(Canvas &canvas)
    {
        const Palette &c = m_Theme.Colors();
        RECT box = SearchRect(), glyph;
        int size = m_Theme.Scale(16);

        GlassFill(canvas, box, m_Theme.Scale(4), GlassColor(m_Theme, GlassTone::Clear), GlassColor(m_Theme, GlassTone::Stroke));
        if (GetFocus() == m_Search)
        {
            RECT line = { box.left + 1, box.bottom - m_Theme.Scale(2), box.right - 1, box.bottom };
            canvas.Fill(line, c.Accent);
        }
        glyph.right = box.right - m_Theme.Scale(10);
        glyph.left = glyph.right - size;
        glyph.top = box.top + (box.bottom - box.top - size) / 2;
        glyph.bottom = glyph.top + size;
        canvas.Icon(L"search_16_regular", glyph, GlassColor(m_Theme, GlassTone::TextSecondary));
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
        PaintScrollBar(canvas, client);
    }

    RECT ScrollTrack() const
    {
        RECT client, rc;
        GetClientRect(m_hwnd, &client);
        SetRect(&rc, client.right - m_Theme.Scale(10), HeaderBottom(), client.right - m_Theme.Scale(4),
                client.bottom - m_Theme.Scale(8));
        return rc;
    }

    RECT ScrollThumb() const
    {
        RECT track = ScrollTrack(), thumb = track;
        int view = track.bottom - track.top, total = m_ContentHeight - HeaderBottom();
        int length, offset;

        if (total <= view || view <= 0)
        {
            SetRectEmpty(&thumb);
            return thumb;
        }
        length = max(m_Theme.Scale(32), MulDiv(view, view, total));
        offset = MulDiv(m_Scroll, view - length, max(1, total - view));
        thumb.top = track.top + offset;
        thumb.bottom = thumb.top + length;
        return thumb;
    }

    void PaintScrollBar(Canvas &canvas, const RECT &client)
    {
        const Palette &c = m_Theme.Colors();
        RECT thumb = ScrollThumb();

        UNREFERENCED_PARAMETER(client);
        if (IsRectEmpty(&thumb))
            return;
        if (!m_DragScroll && m_HotPart != Part::None)
            thumb.left += m_Theme.Scale(2);
        canvas.FillRound(thumb, (float)(thumb.right - thumb.left) / 2.0f, c.ControlStrongStroke);
    }

    void ClipRowHighlight(Canvas &canvas, int place, const RECT &rc, COLORREF color)
    {
        RECT box = ScreenBox(m_GroupBoxes[m_Places[place].Group]);
        Gdiplus::GraphicsPath path(Gdiplus::FillModeAlternate);
        Gdiplus::SolidBrush brush(Gdiplus::Color(255, GetRValue(color), GetGValue(color), GetBValue(color)));
        float r = m_Theme.ScaleF(8.0f), d = r * 2.0f;
        Gdiplus::RectF b((float)box.left + 1.0f, (float)box.top + 1.0f, (float)(box.right - box.left) - 2.0f,
                         (float)(box.bottom - box.top) - 2.0f);
        Gdiplus::Region region(Gdiplus::RectF((float)rc.left, (float)rc.top, (float)(rc.right - rc.left),
                                               (float)(rc.bottom - rc.top)));

        path.AddArc(b.X, b.Y, d, d, 180.0f, 90.0f);
        path.AddArc(b.X + b.Width - d, b.Y, d, d, 270.0f, 90.0f);
        path.AddArc(b.X + b.Width - d, b.Y + b.Height - d, d, d, 0.0f, 90.0f);
        path.AddArc(b.X, b.Y + b.Height - d, d, d, 90.0f, 90.0f);
        path.CloseFigure();
        region.Intersect(&path);
        canvas.Graphics().FillRegion(&brush, &region);
    }

    bool RowActivatable(const SettingRow &row) const
    {
        return row.Enabled && (row.Kind == RowKind::Navigate || row.Kind == RowKind::Link ||
                               row.Kind == RowKind::Toggle || row.Kind == RowKind::Choice);
    }

    void PaintRow(Canvas &canvas, int place)
    {
        const Palette &c = m_Theme.Colors();
        RowPlace &where = m_Places[place];
        SettingRow &row = m_Groups[where.Group].Rows[where.Row];
        RECT rc = ScreenBox(where.Box), control = ScreenBox(where.Control), text;
        COLORREF title = row.Enabled ? c.Text : c.TextDisabled, detail = row.Enabled ? c.TextSecondary : c.TextDisabled;
        int textLeft = rc.left + m_Theme.Scale(16), textRight;
        bool hot = place == m_HotPlace && RowActivatable(row);

        if (hot)
            ClipRowHighlight(canvas, place, rc, m_PressedPlace == place && m_PressedPart == Part::Row ? c.SubtlePressed : c.CardHover);
        if (where.Row > 0)
        {
            RECT line = { rc.left + m_Theme.Scale(16), rc.top, rc.right - m_Theme.Scale(16), rc.top + 1 };
            canvas.Fill(line, c.Divider);
        }
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
        if (row.Kind == RowKind::Info || row.Kind == RowKind::Navigate)
        {
            int valueWidth = row.Value.empty() ? 0 : min(MeasureWidth(row.Value, m_Theme.Font(TextStyle::Body)),
                                                          (rc.right - textLeft) / 2);
            RECT value = { textRight - valueWidth, rc.top, textRight, rc.bottom };
            if (row.Kind == RowKind::Info)
            {
                value.right = rc.right - m_Theme.Scale(16);
                value.left = value.right - valueWidth;
            }
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
            int middle = (rc.top + rc.bottom) / 2 - (row.Bar ? m_Theme.Scale(8) : 0);
            if (row.Bar)
            {
                RECT track = { textLeft, rc.bottom - m_Theme.Scale(20), textRight, rc.bottom - m_Theme.Scale(14) };
                RECT used = track;
                used.right = track.left + MulDiv(track.right - track.left, max(0, min(row.Max, row.Position)),
                                                 max(1, row.Max));
                canvas.FillRound(track, m_Theme.ScaleF(3.0f), c.ControlStrongStroke);
                if (used.right > used.left)
                    canvas.FillRound(used, m_Theme.ScaleF(3.0f), row.Position * 10 >= row.Max * 9 ? RGB(0xC4, 0x2B, 0x1C) :
                                                                                                     c.Accent);
            }
            SetRect(&text, textLeft, middle - m_Theme.Scale(21), textRight, middle);
            canvas.Text(row.Title.c_str(), text, m_Theme.Font(TextStyle::Body), title,
                        DT_SINGLELINE | DT_BOTTOM | DT_LEFT | DT_END_ELLIPSIS);
            SetRect(&text, textLeft, middle + m_Theme.Scale(1), textRight, middle + m_Theme.Scale(20));
            canvas.Text(row.Detail.c_str(), text, m_Theme.Font(TextStyle::Caption), detail,
                        DT_SINGLELINE | DT_TOP | DT_LEFT | DT_END_ELLIPSIS);
        }
        switch (row.Kind)
        {
            case RowKind::Toggle:
                PaintToggle(canvas, control, row.Checked, row.Enabled, hot);
                break;
            case RowKind::Choice:
                PaintChoice(canvas, place, row, control);
                break;
            case RowKind::Slider:
                PaintSlider(canvas, place, row, control);
                break;
            case RowKind::Button:
                PaintButtons(canvas, place, row);
                break;
            case RowKind::Navigate:
                canvas.Icon(L"chevron_right_16_regular", control, row.Enabled ? c.TextSecondary : c.TextDisabled);
                break;
            case RowKind::Link:
                canvas.Icon(L"open_16_regular", control, row.Enabled ? c.TextSecondary : c.TextDisabled);
                break;
            default:
                break;
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
        {
            textRight = ScreenBox(m_Places[place].Actions.front()).left - m_Theme.Scale(16);
            PaintButtons(canvas, place, row);
        }
        SetRect(&text, badge.right + m_Theme.Scale(16), rc.top + m_Theme.Scale(20), textRight, rc.top + m_Theme.Scale(50));
        canvas.Text(row.Title.c_str(), text, m_Theme.Font(TextStyle::Subtitle), c.Text,
                    DT_SINGLELINE | DT_VCENTER | DT_LEFT | DT_END_ELLIPSIS);
        SetRect(&text, text.left, text.bottom, textRight, rc.bottom - m_Theme.Scale(16));
        canvas.Text(row.Detail.c_str(), text, m_Theme.Font(TextStyle::Body), c.TextSecondary,
                    DT_SINGLELINE | DT_TOP | DT_LEFT | DT_END_ELLIPSIS);
    }

    void PaintToggle(Canvas &canvas, const RECT &rc, bool on, bool enabled, bool hot)
    {
        const Palette &c = m_Theme.Colors();
        int knob = m_Theme.Scale(hot ? 14 : 12), middle = (rc.top + rc.bottom) / 2;
        RECT dot;

        if (on)
        {
            canvas.FillRound(rc, (float)(rc.bottom - rc.top) / 2.0f, enabled ? c.Accent : c.TextDisabled);
            SetRect(&dot, rc.right - m_Theme.Scale(4) - knob, middle - knob / 2, rc.right - m_Theme.Scale(4),
                    middle - knob / 2 + knob);
            canvas.FillEllipse(dot, c.AccentText);
        }
        else
        {
            canvas.FillRound(rc, (float)(rc.bottom - rc.top) / 2.0f, c.ControlFill);
            canvas.FrameRound(rc, (float)(rc.bottom - rc.top) / 2.0f, enabled ? c.ControlStrongStroke : c.TextDisabled);
            SetRect(&dot, rc.left + m_Theme.Scale(4), middle - knob / 2, rc.left + m_Theme.Scale(4) + knob,
                    middle - knob / 2 + knob);
            canvas.FillEllipse(dot, enabled ? c.TextSecondary : c.TextDisabled);
        }
    }

    void PaintChoice(Canvas &canvas, int place, const SettingRow &row, const RECT &rc)
    {
        const Palette &c = m_Theme.Colors();
        RECT text = rc, chevron;
        bool hot = place == m_HotPlace && row.Enabled;

        canvas.FillRound(rc, m_Theme.ScaleF(4.0f), hot ? c.CardHover : c.ControlFill);
        canvas.FrameRound(rc, m_Theme.ScaleF(4.0f), c.ControlStroke);
        text.left += m_Theme.Scale(12);
        text.right -= m_Theme.Scale(34);
        if (row.Selected >= 0 && row.Selected < (int)row.Options.size())
            canvas.Text(row.Options[row.Selected].c_str(), text, m_Theme.Font(TextStyle::Body),
                        row.Enabled ? c.Text : c.TextDisabled, DT_SINGLELINE | DT_VCENTER | DT_LEFT | DT_END_ELLIPSIS);
        SetRect(&chevron, rc.right - m_Theme.Scale(24), (rc.top + rc.bottom) / 2 - m_Theme.Scale(6),
                rc.right - m_Theme.Scale(12), (rc.top + rc.bottom) / 2 + m_Theme.Scale(6));
        canvas.Icon(L"chevron_down_12_regular", chevron, row.Enabled ? c.TextSecondary : c.TextDisabled);
    }

    void PaintSlider(Canvas &canvas, int place, const SettingRow &row, const RECT &rc)
    {
        const Palette &c = m_Theme.Colors();
        int middle = (rc.top + rc.bottom) / 2, x = SliderX(row, rc);
        RECT track = { rc.left + m_Theme.Scale(10), middle - m_Theme.Scale(2), rc.right - m_Theme.Scale(10),
                       middle + m_Theme.Scale(2) }, fill = track, thumb, dot;
        int outer = m_Theme.Scale(10), inner = m_Theme.Scale(place == m_HotPlace || place == m_DragPlace ? 7 : 6);

        canvas.FillRound(track, m_Theme.ScaleF(2.0f), c.ControlStrongStroke);
        fill.right = x;
        if (fill.right > fill.left)
            canvas.FillRound(fill, m_Theme.ScaleF(2.0f), row.Enabled ? c.Accent : c.TextDisabled);
        SetRect(&thumb, x - outer, middle - outer, x + outer, middle + outer);
        canvas.FillEllipse(thumb, c.ControlFillFocus);
        canvas.FrameRound(thumb, (float)outer, c.ControlStroke);
        SetRect(&dot, x - inner, middle - inner, x + inner, middle + inner);
        canvas.FillEllipse(dot, row.Enabled ? c.Accent : c.TextDisabled);
    }

    int SliderX(const SettingRow &row, const RECT &rc) const
    {
        int left = rc.left + m_Theme.Scale(10), right = rc.right - m_Theme.Scale(10);
        int span = max(1, row.Max - row.Min);
        return left + MulDiv(max(row.Min, min(row.Max, row.Position)) - row.Min, right - left, span);
    }

    int SliderValue(const SettingRow &row, const RECT &rc, int x) const
    {
        int left = rc.left + m_Theme.Scale(10), right = rc.right - m_Theme.Scale(10);
        int value = row.Min + MulDiv(x - left, row.Max - row.Min, max(1, right - left));
        return max(row.Min, min(row.Max, value));
    }

    void PaintButtons(Canvas &canvas, int place, const SettingRow &row)
    {
        const Palette &c = m_Theme.Colors();
        std::vector<std::wstring> actions = row.Actions;

        if (!row.Action.empty())
            actions.insert(actions.begin(), row.Action);
        for (size_t i = 0; i < m_Places[place].Actions.size() && i < actions.size(); i++)
        {
            RECT rc = ScreenBox(m_Places[place].Actions[i]);
            bool hot = place == m_HotPlace && m_HotPart == Part::Action && m_HotAction == (int)i && row.Enabled;
            bool pressed = hot && m_PressedPlace == place && m_PressedAction == (int)i;
            bool accent = row.Accent && i == 0;
            COLORREF fill = accent ? c.Accent : (pressed ? c.SubtlePressed : (hot ? c.CardHover : c.ControlFill));

            if (!row.Enabled)
                fill = c.ControlFill;
            if (accent && hot && row.Enabled)
                fill = RGB(GetRValue(c.Accent) * 9 / 10, GetGValue(c.Accent) * 9 / 10, GetBValue(c.Accent) * 9 / 10);
            canvas.FillRound(rc, m_Theme.ScaleF(4.0f), fill);
            if (!accent)
                canvas.FrameRound(rc, m_Theme.ScaleF(4.0f), c.ControlStroke);
            canvas.Text(actions[i].c_str(), rc, m_Theme.Font(TextStyle::Body),
                        !row.Enabled ? c.TextDisabled : (accent ? c.AccentText : c.Text),
                        DT_SINGLELINE | DT_VCENTER | DT_CENTER);
        }
    }

    int PlaceFromPoint(int x, int y, Part *part, int *action) const
    {
        POINT pt = { x, y + m_Scroll };
        RECT client;

        GetClientRect(m_hwnd, &client);
        *part = Part::None;
        *action = -1;
        if (x < ContentLeft() - m_Theme.Scale(8) || y < HeaderBottom() - m_Theme.Scale(8))
            return -1;
        for (size_t i = 0; i < m_Places.size(); i++)
        {
            const RowPlace &place = m_Places[i];
            const SettingRow &row = m_Groups[place.Group].Rows[place.Row];
            if (!PtInRect(&place.Box, pt))
                continue;
            for (size_t a = 0; a < place.Actions.size(); a++)
            {
                if (PtInRect(&place.Actions[a], pt))
                {
                    *part = Part::Action;
                    *action = (int)a;
                    return (int)i;
                }
            }
            if (row.Kind == RowKind::Slider)
            {
                RECT hit = place.Control;
                InflateRect(&hit, m_Theme.Scale(4), m_Theme.Scale(8));
                *part = PtInRect(&hit, pt) ? Part::Control : Part::None;
                return *part == Part::None ? -1 : (int)i;
            }
            if (RowActivatable(row))
            {
                *part = Part::Row;
                return (int)i;
            }
            return -1;
        }
        return -1;
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

    void SetHot(int nav, int place, Part part, int action, BOOL back, BOOL forward, BOOL account, int crumb)
    {
        if (nav == m_HotNav && place == m_HotPlace && part == m_HotPart && action == m_HotAction && back == m_HotBack &&
            forward == m_HotForward && account == m_HotAccount && crumb == m_HotCrumb)
            return;
        m_HotNav = nav;
        m_HotPlace = place;
        m_HotPart = part;
        m_HotAction = action;
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
        Part part;
        int action, place;

        if (!m_TrackingClient)
        {
            TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, m_hwnd, 0 };
            m_TrackingClient = TrackMouseEvent(&tme);
        }
        if (m_DragScroll)
        {
            RECT track = ScrollTrack(), thumb = ScrollThumb();
            int view = track.bottom - track.top, total = m_ContentHeight - HeaderBottom();
            int length = thumb.bottom - thumb.top;
            m_Scroll = MulDiv(y - m_DragOffset - track.top, max(1, total - view), max(1, view - length));
            ClampScroll();
            Invalidate(NULL);
            return;
        }
        if (m_DragPlace >= 0)
        {
            RowPlace &where = m_Places[m_DragPlace];
            SettingRow &row = m_Groups[where.Group].Rows[where.Row];
            int value = SliderValue(row, ScreenBox(where.Control), x);
            if (value != row.Position)
            {
                row.Position = value;
                if (row.Live && row.Change)
                    row.Change(value);
                Invalidate(NULL);
            }
            return;
        }
        place = PlaceFromPoint(x, y, &part, &action);
        SetHot(NavFromPoint(x, y), place, part, action, PtInRect(&back, pt), PtInRect(&forward, pt),
               PtInRect(&account, pt), CrumbFromPoint(x, y));
    }

    void OnButtonDown(int x, int y)
    {
        RECT thumb = ScrollThumb();
        POINT pt = { x, y };
        Part part;
        int action;

        if (!IsRectEmpty(&thumb))
        {
            RECT hit = thumb;
            InflateRect(&hit, m_Theme.Scale(4), 0);
            if (PtInRect(&hit, pt))
            {
                m_DragScroll = TRUE;
                m_DragOffset = y - thumb.top;
                SetCapture(m_hwnd);
                return;
            }
        }
        m_PressedPlace = PlaceFromPoint(x, y, &part, &action);
        m_PressedPart = part;
        m_PressedAction = action;
        if (m_PressedPlace >= 0 && part == Part::Control)
        {
            RowPlace &where = m_Places[m_PressedPlace];
            SettingRow &row = m_Groups[where.Group].Rows[where.Row];
            if (row.Kind == RowKind::Slider && row.Enabled)
            {
                m_DragPlace = m_PressedPlace;
                m_DragStart = row.Position;
                row.Position = SliderValue(row, ScreenBox(where.Control), x);
                if (row.Live && row.Change)
                    row.Change(row.Position);
                SetCapture(m_hwnd);
            }
        }
        Invalidate(NULL);
    }

    void EndSliderDrag(BOOL commit)
    {
        int place = m_DragPlace;

        m_DragPlace = -1;
        if (place < 0 || place >= (int)m_Places.size())
            return;
        {
            RowPlace &where = m_Places[place];
            SettingRow &row = m_Groups[where.Group].Rows[where.Row];
            if (commit && row.Change && (!row.Live || row.Position != m_DragStart))
            {
                std::function<void(int)> change = row.Change;
                change(row.Position);
                Rebuild(FALSE);
            }
        }
    }

    void OnButtonUp(int x, int y)
    {
        RECT back = BackRect(), forward = ForwardRect(), account = AccountRect();
        POINT pt = { x, y };
        Part part;
        int action, nav = NavFromPoint(x, y), crumb = CrumbFromPoint(x, y);
        int place, pressed = m_PressedPlace, pressedAction = m_PressedAction;
        Part pressedPart = m_PressedPart;

        if (m_DragScroll)
        {
            m_DragScroll = FALSE;
            ReleaseCapture();
            return;
        }
        if (m_DragPlace >= 0)
        {
            ReleaseCapture();
            EndSliderDrag(TRUE);
            return;
        }
        place = PlaceFromPoint(x, y, &part, &action);
        m_PressedPlace = -1;
        m_PressedPart = Part::None;
        m_PressedAction = -1;
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
            return;
        }
        if (place >= 0 && place == pressed && part == pressedPart && action == pressedAction)
            Activate(place, part, action);
        else
            Invalidate(NULL);
    }

    void Activate(int place, Part part, int action)
    {
        RowPlace where = m_Places[place];
        SettingRow &row = m_Groups[where.Group].Rows[where.Row];

        if (!row.Enabled)
            return;
        if (part == Part::Action)
        {
            std::function<void(int)> invoke = row.Invoke;
            if (invoke)
                invoke(action);
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
            {
                RECT control = ScreenBox(where.Control);
                POINT pt = { control.left, control.bottom };
                HMENU menu = CreatePopupMenu();
                std::function<void(int)> change = row.Change;
                int selected = row.Selected;
                int chosen;

                for (size_t i = 0; i < row.Options.size(); i++)
                    AppendMenuW(menu, MF_STRING | ((int)i == selected ? MF_CHECKED : 0), i + 1, row.Options[i].c_str());
                ClientToScreen(m_hwnd, &pt);
                chosen = (int)TrackPopupMenuEx(menu, TPM_RETURNCMD | TPM_LEFTALIGN | TPM_TOPALIGN, pt.x, pt.y, m_hwnd, NULL);
                DestroyMenu(menu);
                if (chosen > 0 && chosen - 1 != selected && change)
                    change(chosen - 1);
                Rebuild(FALSE);
                return;
            }
            default:
                return;
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
    int m_HotPlace;
    Part m_HotPart;
    int m_HotAction;
    int m_PressedPlace;
    Part m_PressedPart;
    int m_PressedAction;
    int m_DragPlace;
    int m_DragStart = 0;
    BOOL m_HotBack;
    BOOL m_HotForward;
    BOOL m_HotAccount;
    int m_HotCrumb;
    BOOL m_DragScroll;
    int m_DragOffset;
    BOOL m_TrackingClient;
    BOOL m_Admin = FALSE;
    HANDLE m_AddressNotify = NULL;
    HANDLE m_InterfaceNotify = NULL;
    HANDLE m_Wlan = NULL;
    HWND m_Search;
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
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    Shutdown();
    CoUninitialize();
    return (int)msg.wParam;
}
