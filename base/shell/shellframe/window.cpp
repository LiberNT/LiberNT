/*
 * PROJECT:     LiberNT Shell Frame
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     Top-level window with a client-drawn caption
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "shellframe.h"

namespace ShellFrame
{

FrameWindow::FrameWindow() :
    m_hwnd(NULL),
    m_HotButton(CaptionButton::None),
    m_PressedButton(CaptionButton::None),
    m_Active(TRUE),
    m_TrackingLeave(FALSE),
    m_GlassWidth(-1),
    m_GlassTop(-1)
{
}

FrameWindow::~FrameWindow()
{
}

HWND
FrameWindow::Create(LPCWSTR className, LPCWSTR title, HICON icon, int width, int height, int showCmd)
{
    WNDCLASSEXW wc = { sizeof(wc) };
    HINSTANCE instance = GetModuleHandleW(NULL);
    UINT dpi = GetDpiForSystem();

    if (!GetClassInfoExW(instance, className, &wc))
    {
        wc.cbSize = sizeof(wc);
        wc.style = CS_DBLCLKS;
        wc.lpfnWndProc = WindowProc;
        wc.hInstance = instance;
        wc.hIcon = icon;
        wc.hIconSm = icon;
        wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
        wc.lpszClassName = className;
        if (!RegisterClassExW(&wc))
            return NULL;
    }
    m_Theme.SetDpi(dpi);
    CreateWindowExW(0, className, title, WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, CW_USEDEFAULT, CW_USEDEFAULT,
                    MulDiv(width, dpi, 96), MulDiv(height, dpi, 96), NULL, NULL, instance, this);
    if (m_hwnd)
        ShowWindow(m_hwnd, showCmd);
    return m_hwnd;
}

LRESULT CALLBACK
FrameWindow::WindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    FrameWindow *self;

    if (msg == WM_NCCREATE)
    {
        self = (FrameWindow *)((CREATESTRUCTW *)lParam)->lpCreateParams;
        self->m_hwnd = hwnd;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)self);
    }
    else
    {
        self = (FrameWindow *)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    }
    if (!self)
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    if (msg == WM_NCDESTROY)
    {
        LRESULT result = self->HandleMessage(msg, wParam, lParam);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
        self->m_hwnd = NULL;
        return result;
    }
    return self->HandleMessage(msg, wParam, lParam);
}

LRESULT
FrameWindow::DefaultProc(UINT msg, WPARAM wParam, LPARAM lParam)
{
    return DefWindowProcW(m_hwnd, msg, wParam, lParam);
}

void
FrameWindow::Invalidate(const RECT *rc)
{
    InvalidateRect(m_hwnd, rc, FALSE);
}

void
FrameWindow::ApplyFrameAttributes()
{
#if SHELLFRAME_CUSTOM_CAPTION_BUTTONS
    MARGINS margins = { 0, 0, 0, 0 };
#else
    MARGINS margins = { m_Theme.IsHighContrast() ? 0 : GlassWidth(), 0, m_Theme.IsHighContrast() ? 0 : GlassTop(), 0 };
#endif
    DWORD corner = 2;

    m_GlassWidth = GlassWidth();
    m_GlassTop = GlassTop();
    DwmExtendFrameIntoClientArea(m_hwnd, &margins);
    DwmSetWindowAttribute(m_hwnd, DWMWA_WINDOW_CORNER_PREFERENCE, &corner, sizeof(corner));
}

int
FrameWindow::CaptionButtonsWidth() const
{
#if SHELLFRAME_CUSTOM_CAPTION_BUTTONS
    return m_Theme.Scale(46) * 3;
#else
    RECT bounds;

    if (SUCCEEDED(DwmGetWindowAttribute(m_hwnd, DWMWA_CAPTION_BUTTON_BOUNDS, &bounds, sizeof(bounds))) &&
        bounds.right > bounds.left)
        return bounds.right - bounds.left;
    return GetSystemMetricsForDpi(SM_CXSIZE, m_Theme.Dpi()) * 3;
#endif
}

#if !SHELLFRAME_CUSTOM_CAPTION_BUTTONS
void
FrameWindow::ClearCaptionButtons(Canvas &canvas)
{
    RECT bounds, window;
    POINT origin = { 0, 0 };

    if (FAILED(DwmGetWindowAttribute(m_hwnd, DWMWA_CAPTION_BUTTON_BOUNDS, &bounds, sizeof(bounds))) ||
        !GetWindowRect(m_hwnd, &window) || !ClientToScreen(m_hwnd, &origin))
        return;
    OffsetRect(&bounds, window.left - origin.x, window.top - origin.y);
    canvas.Fill(bounds, RGB(0, 0, 0));
}
#endif

#if SHELLFRAME_CUSTOM_CAPTION_BUTTONS
RECT
FrameWindow::CaptionButtonRect(CaptionButton button) const
{
    RECT client, rc;
    int width = m_Theme.Scale(46), index;

    GetClientRect(m_hwnd, &client);
    index = button == CaptionButton::Close ? 0 : button == CaptionButton::Maximize ? 1 : 2;
    rc.right = client.right - index * width;
    rc.left = rc.right - width;
    rc.top = 0;
    rc.bottom = CaptionButtonHeight();
    return rc;
}

CaptionButton
FrameWindow::ButtonFromHit(LRESULT hit) const
{
    switch (hit)
    {
        case HTMINBUTTON: return CaptionButton::Minimize;
        case HTMAXBUTTON: return CaptionButton::Maximize;
        case HTCLOSE: return CaptionButton::Close;
    }
    return CaptionButton::None;
}

void
FrameWindow::SetHotButton(CaptionButton button)
{
    RECT rc;

    if (button == m_HotButton)
        return;
    m_HotButton = button;
    GetClientRect(m_hwnd, &rc);
    rc.left = rc.right - CaptionButtonsWidth();
    rc.bottom = CaptionButtonHeight();
    Invalidate(&rc);
}
#endif

LRESULT
FrameWindow::HitTest(POINT screen)
{
    POINT pt = screen;
    RECT client;
    int border = GetSystemMetricsForDpi(SM_CYSIZEFRAME, m_Theme.Dpi()) +
                 GetSystemMetricsForDpi(SM_CXPADDEDBORDER, m_Theme.Dpi());
#if SHELLFRAME_CUSTOM_CAPTION_BUTTONS
    static const CaptionButton Buttons[] = { CaptionButton::Minimize, CaptionButton::Maximize, CaptionButton::Close };
    static const LRESULT Hits[] = { HTMINBUTTON, HTMAXBUTTON, HTCLOSE };
#endif

    ScreenToClient(m_hwnd, &pt);
    GetClientRect(m_hwnd, &client);
#if SHELLFRAME_CUSTOM_CAPTION_BUTTONS
    for (int i = 0; i < 3; i++)
    {
        RECT rc = CaptionButtonRect(Buttons[i]);
        if (PtInRect(&rc, pt))
            return Hits[i];
    }
#endif
    if (!IsZoomed(m_hwnd) && pt.y < border)
    {
        if (pt.x < border * 2)
            return HTTOPLEFT;
        if (pt.x >= client.right - border * 2)
            return HTTOPRIGHT;
        return HTTOP;
    }
    if (pt.y < CaptionHeight() && !IsCaptionClient(pt))
        return HTCAPTION;
    return HTCLIENT;
}

#if SHELLFRAME_CUSTOM_CAPTION_BUTTONS
void
FrameWindow::PaintCaptionButtons(Canvas &canvas, const RECT &client, COLORREF background)
{
    const Palette &c = m_Theme.Colors();
    static const CaptionButton Buttons[] = { CaptionButton::Minimize, CaptionButton::Maximize, CaptionButton::Close };

    UNREFERENCED_PARAMETER(client);
    for (int i = 0; i < 3; i++)
    {
        CaptionButton button = Buttons[i];
        RECT rc = CaptionButtonRect(button), glyph;
        COLORREF fill = background, ink = m_Active ? c.Text : c.TextDisabled;
        LPCWSTR icon;
        int size = m_Theme.Scale(16);

        if (button == m_HotButton || button == m_PressedButton)
        {
            if (button == CaptionButton::Close)
            {
                fill = c.CloseHover;
                ink = c.CloseText;
            }
            else
            {
                fill = button == m_PressedButton ? c.SubtlePressed : c.SubtleHover;
                ink = c.Text;
            }
        }
        canvas.Fill(rc, fill);
        icon = button == CaptionButton::Close ? L"dismiss_16_regular" :
               button == CaptionButton::Minimize ? L"subtract_16_regular" :
               IsZoomed(m_hwnd) ? L"square_multiple_16_regular" : L"square_16_regular";
        glyph.left = rc.left + (rc.right - rc.left - size) / 2;
        glyph.top = rc.top + (rc.bottom - rc.top - size) / 2;
        glyph.right = glyph.left + size;
        glyph.bottom = glyph.top + size;
        canvas.Icon(icon, glyph, ink);
    }
}
#endif

LRESULT
FrameWindow::HandleMessage(UINT msg, WPARAM wParam, LPARAM lParam)
{
#if !SHELLFRAME_CUSTOM_CAPTION_BUTTONS
    LRESULT result = 0;

    if (DwmDefWindowProc(m_hwnd, msg, wParam, lParam, &result))
        return result;
#endif
    switch (msg)
    {
        case WM_CREATE:
            m_Theme.Update(m_hwnd);
            ApplyFrameAttributes();
            SetWindowPos(m_hwnd, NULL, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
            OnCreate();
            return 0;

        case WM_NCCALCSIZE:
            if (wParam)
            {
                NCCALCSIZE_PARAMS *params = (NCCALCSIZE_PARAMS *)lParam;
                LONG top = params->rgrc[0].top;
                DefaultProc(msg, wParam, lParam);
                params->rgrc[0].top = top;
                if (IsZoomed(m_hwnd))
                {
                    params->rgrc[0].top += GetSystemMetricsForDpi(SM_CYSIZEFRAME, m_Theme.Dpi()) +
                                           GetSystemMetricsForDpi(SM_CXPADDEDBORDER, m_Theme.Dpi());
                }
                return 0;
            }
            break;

        case WM_NCHITTEST:
        {
            LRESULT hit = DefaultProc(msg, wParam, lParam);
            POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            if (hit != HTCLIENT && hit != HTCAPTION)
                return hit;
            return HitTest(pt);
        }

#if SHELLFRAME_CUSTOM_CAPTION_BUTTONS
        case WM_NCMOUSEMOVE:
        {
            CaptionButton button = ButtonFromHit(wParam);
            if (!m_TrackingLeave)
            {
                TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE | TME_NONCLIENT, m_hwnd, 0 };
                m_TrackingLeave = TrackMouseEvent(&tme);
            }
            SetHotButton(button);
            if (button != CaptionButton::None)
                return 0;
            break;
        }

        case WM_NCMOUSELEAVE:
            m_TrackingLeave = FALSE;
            m_PressedButton = CaptionButton::None;
            SetHotButton(CaptionButton::None);
            break;

        case WM_MOUSEMOVE:
            if (m_HotButton != CaptionButton::None)
                SetHotButton(CaptionButton::None);
            break;

        case WM_NCLBUTTONDOWN:
        case WM_NCLBUTTONDBLCLK:
            if (ButtonFromHit(wParam) != CaptionButton::None)
            {
                m_PressedButton = ButtonFromHit(wParam);
                SetHotButton(m_PressedButton);
                Invalidate(NULL);
                return 0;
            }
            break;

        case WM_NCLBUTTONUP:
        {
            CaptionButton button = ButtonFromHit(wParam);
            if (m_PressedButton != CaptionButton::None)
            {
                CaptionButton pressed = m_PressedButton;
                m_PressedButton = CaptionButton::None;
                Invalidate(NULL);
                if (button == pressed)
                {
                    WPARAM command = button == CaptionButton::Close ? SC_CLOSE :
                                     button == CaptionButton::Minimize ? SC_MINIMIZE :
                                     IsZoomed(m_hwnd) ? SC_RESTORE : SC_MAXIMIZE;
                    SetHotButton(CaptionButton::None);
                    PostMessageW(m_hwnd, WM_SYSCOMMAND, command, lParam);
                }
                return 0;
            }
            break;
        }

#endif
        case WM_ACTIVATE:
            m_Active = LOWORD(wParam) != WA_INACTIVE;
            Invalidate(NULL);
            break;

        case WM_SIZE:
        {
            RECT client;
            GetClientRect(m_hwnd, &client);
            OnLayout(client);
            if (GlassWidth() != m_GlassWidth || GlassTop() != m_GlassTop)
                ApplyFrameAttributes();
            Invalidate(NULL);
            break;
        }

        case WM_ERASEBKGND:
            return 1;

        case WM_PAINT:
        {
            PAINTSTRUCT ps;
            RECT client;
            HDC hdc = BeginPaint(m_hwnd, &ps);
            GetClientRect(m_hwnd, &client);
            {
                Canvas canvas(hdc, client);
                OnPaint(canvas, client);
#if !SHELLFRAME_CUSTOM_CAPTION_BUTTONS
                ClearCaptionButtons(canvas);
#endif
            }
            EndPaint(m_hwnd, &ps);
            return 0;
        }

        case WM_DPICHANGED:
        {
            RECT *suggested = (RECT *)lParam;
            m_Theme.SetDpi(HIWORD(wParam));
            ApplyFrameAttributes();
            OnThemeChanged();
            SetWindowPos(m_hwnd, NULL, suggested->left, suggested->top, suggested->right - suggested->left,
                         suggested->bottom - suggested->top, SWP_NOZORDER | SWP_NOACTIVATE);
            return 0;
        }

        case WM_SYSCOLORCHANGE:
        case WM_THEMECHANGED:
        case WM_SETTINGCHANGE:
            if (msg != WM_SETTINGCHANGE || wParam == SPI_SETHIGHCONTRAST ||
                (lParam && !lstrcmpiW((LPCWSTR)lParam, L"ImmersiveColorSet")))
            {
                RECT client;
                m_Theme.Update(m_hwnd);
                ApplyFrameAttributes();
                OnThemeChanged();
                GetClientRect(m_hwnd, &client);
                OnLayout(client);
                Invalidate(NULL);
            }
            break;
    }
    return DefaultProc(msg, wParam, lParam);
}

}
