/*
 * PROJECT:     LiberNT Shell Frame
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     Tab band shared by File Explorer and the terminal
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "shellframe.h"

#include <shlobj.h>
#include <shellapi.h>
#include <strsafe.h>
#include <uxtheme.h>
#include <commoncontrols.h>
#include <reactos/dwmframe.h>

namespace ShellFrame
{

static COLORREF
BlendColor(COLORREF base, COLORREF ink, int amount)
{
    return RGB(GetRValue(base) + (GetRValue(ink) - GetRValue(base)) * amount / 255,
               GetGValue(base) + (GetGValue(ink) - GetGValue(base)) * amount / 255,
               GetBValue(base) + (GetBValue(ink) - GetBValue(base)) * amount / 255);
}

static BOOL
IsDarkColor(COLORREF color)
{
    return GetRValue(color) * 299 + GetGValue(color) * 587 + GetBValue(color) * 114 < 128000;
}

void
GetBandPalette(BandPalette *palette)
{
    COLORREF window = GetSysColor(COLOR_WINDOW), text = GetSysColor(COLOR_WINDOWTEXT);
    COLORREF base = RGB(0xF3, 0xF3, 0xF3), black = RGB(0, 0, 0);
    HIGHCONTRASTW contrast = { sizeof(contrast) };

    palette->Window = window;
    palette->Band = BlendColor(window, text, 7);
    palette->BandShade = BlendColor(palette->Band, RGB(0, 0, 0), 14);
    palette->BandLine = BlendColor(window, text, 15);
    palette->BandBottom = BlendColor(window, text, 34);
    palette->Box = BlendColor(window, text, 2);
    palette->BoxLine = BlendColor(window, text, 41);
    palette->Command = window;
    palette->CommandBottom = BlendColor(window, text, 41);
    palette->TabBorder = BlendColor(window, text, 37);
    palette->Text = text;
    palette->Glyph = BlendColor(window, text, 229);
    palette->GlyphDisabled = BlendColor(window, text, 84);
    palette->Body = window;
    palette->PanelLine = window;
    if (IsDarkColor(window) ||
        (SystemParametersInfoW(SPI_GETHIGHCONTRAST, sizeof(contrast), &contrast, 0) &&
         (contrast.dwFlags & HCF_HIGHCONTRASTON)))
        return;
    palette->Band = palette->BandShade = palette->Command = palette->CommandBottom = palette->Body = base;
    palette->BandLine = palette->BandBottom = BlendColor(base, black, 0x14);
    palette->Box = BlendColor(base, RGB(255, 255, 255), 0xB3);
    palette->PanelLine = BlendColor(base, black, 0x0F);
}

static void
AddRoundRect(Gdiplus::GraphicsPath &path, const Gdiplus::RectF &rc, float radius)
{
    float d = radius * 2.0f;

    path.AddArc(rc.X, rc.Y, d, d, 180.0f, 90.0f);
    path.AddArc(rc.X + rc.Width - d, rc.Y, d, d, 270.0f, 90.0f);
    path.AddArc(rc.X + rc.Width - d, rc.Y + rc.Height - d, d, d, 0.0f, 90.0f);
    path.AddArc(rc.X, rc.Y + rc.Height - d, d, d, 90.0f, 90.0f);
    path.CloseFigure();
}

static HIMAGELIST
SmallSystemImages()
{
    IImageList *images = NULL;

    if (FAILED(SHGetImageList(SHIL_SMALL, IID_IImageList, (void **)&images)))
        return NULL;
    images->Release();
    return (HIMAGELIST)images;
}

static Gdiplus::Color
GlassColor(BYTE alpha, COLORREF color)
{
    return Gdiplus::Color(alpha, GetRValue(color), GetGValue(color), GetBValue(color));
}

static void
DrawSmallIcon(Gdiplus::Graphics &graphics, int index, int x, int y, int size)
{
    HICON icon = index >= 0 ? ImageList_GetIcon(SmallSystemImages(), index, ILD_TRANSPARENT) : NULL;

    if (!icon)
        return;
    {
        Gdiplus::Bitmap *bitmap = Gdiplus::Bitmap::FromHICON(icon);
        if (bitmap)
        {
            graphics.DrawImage(bitmap, x, y, size, size);
            delete bitmap;
        }
    }
    DestroyIcon(icon);
}

static void
AddCorner(Gdiplus::GraphicsPath &path, float x0, float y0, float cx, float cy, float x1, float y1)
{
    path.AddBezier(x0, y0, x0 + (cx - x0) * 2.0f / 3.0f, y0 + (cy - y0) * 2.0f / 3.0f,
                   x1 + (cx - x1) * 2.0f / 3.0f, y1 + (cy - y1) * 2.0f / 3.0f, x1, y1);
}

static void
TabOutline(Gdiplus::GraphicsPath &path, float left, float top, float right, float bottom, float radius, float foot,
           BOOL close)
{
    if (foot > 0.0f)
        AddCorner(path, left - foot, bottom, left, bottom, left, bottom - foot);
    path.AddLine(left, bottom - foot, left, top + radius);
    AddCorner(path, left, top + radius, left, top, left + radius, top);
    path.AddLine(left + radius, top, right - radius, top);
    AddCorner(path, right - radius, top, right, top, right, top + radius);
    path.AddLine(right, top + radius, right, bottom - foot);
    if (foot > 0.0f)
        AddCorner(path, right, bottom - foot, right, bottom, right + foot, bottom);
    if (close)
        path.CloseFigure();
}

static void
FillButton(Gdiplus::Graphics &graphics, const RECT &rc, BYTE alpha, COLORREF color, int radius)
{
    Gdiplus::GraphicsPath path(Gdiplus::FillModeAlternate);
    Gdiplus::SolidBrush fill(GlassColor(alpha, color));

    AddRoundRect(path, Gdiplus::RectF((float)rc.left, (float)rc.top, (float)(rc.right - rc.left),
                                      (float)(rc.bottom - rc.top)), (float)radius);
    graphics.FillPath(&fill, &path);
}

static void
FillGlyph(Gdiplus::Graphics &graphics, LPCWSTR name, const RECT &button, int size, COLORREF color)
{
    float left = (float)((button.left + button.right - size) / 2), top = (float)((button.top + button.bottom - size) / 2);

    FillIcon(graphics, name, Gdiplus::RectF(left, top, (float)size, (float)size), color);
}

TabBand::TabBand() :
    m_hwnd(NULL),
    m_Host(NULL),
    m_Count(0),
    m_Active(-1),
    m_HotTab(-1),
    m_HotClose(FALSE),
    m_HotNew(FALSE),
    m_HotMenu(FALSE),
    m_Tracking(FALSE),
    m_DragTab(-1),
    m_PressedTab(-1),
    m_PressedClose(FALSE),
    m_PressedNew(FALSE),
    m_PressedMenu(FALSE),
    m_ShowMenu(FALSE),
    m_Font(NULL),
    m_ShadeBottom(0),
    m_Background(CLR_INVALID),
    m_ActiveFill(CLR_INVALID)
{
}

TabBand::~TabBand()
{
    if (m_hwnd)
        SetWindowLongPtrW(m_hwnd, GWLP_USERDATA, 0);
}

LRESULT CALLBACK
TabBand::WindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    TabBand *self;

    if (msg == WM_NCCREATE)
    {
        self = (TabBand *)((CREATESTRUCTW *)lParam)->lpCreateParams;
        self->m_hwnd = hwnd;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)self);
    }
    else
    {
        self = (TabBand *)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
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

BOOL
TabBand::Create(HWND parent, TabBandHost *host, HFONT font, LPCWSTR className)
{
    WNDCLASSEXW wc = { sizeof(wc) };
    HINSTANCE instance = NULL;

    m_Host = host;
    m_Font = font;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       (LPCWSTR)&TabBand::WindowProc, &instance);
    if (!GetClassInfoExW(instance, className, &wc))
    {
        wc.cbSize = sizeof(wc);
        wc.style = CS_DBLCLKS;
        wc.lpfnWndProc = WindowProc;
        wc.hInstance = instance;
        wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
        wc.lpszClassName = className;
        if (!RegisterClassExW(&wc))
            return FALSE;
    }
    return CreateWindowExW(0, className, NULL, WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | WS_CLIPSIBLINGS,
                           0, 0, 0, 0, parent, NULL, instance, this) != NULL;
}

int
TabBand::Scale(int value) const
{
    return MulDiv(value, m_hwnd ? GetDpiForWindow(m_hwnd) : GetDpiForSystem(), 96);
}

void
TabBand::SetShadeBottom(int bottom)
{
    if (bottom == m_ShadeBottom)
        return;
    m_ShadeBottom = bottom;
    if (m_hwnd)
        InvalidateRect(m_hwnd, NULL, FALSE);
}

void
TabBand::SetOpaque(COLORREF background, COLORREF activeFill)
{
    if (background == m_Background && activeFill == m_ActiveFill)
        return;
    m_Background = background;
    m_ActiveFill = activeFill;
    if (m_hwnd)
        InvalidateRect(m_hwnd, NULL, FALSE);
}

void
TabBand::SetMenuButton(BOOL show)
{
    m_ShowMenu = show;
    if (m_hwnd)
        InvalidateRect(m_hwnd, NULL, FALSE);
}

void
TabBand::SetFont(HFONT font)
{
    m_Font = font;
    if (m_hwnd)
        InvalidateRect(m_hwnd, NULL, FALSE);
}

void
TabBand::SetTabs(const TabBandItem *tabs, int count, int active)
{
    m_Count = min(count, (int)_countof(m_Tabs));
    for (int i = 0; i < m_Count; i++)
    {
        StringCchCopyW(m_Tabs[i].Title, _countof(m_Tabs[i].Title), tabs[i].Title ? tabs[i].Title : L"");
        m_Tabs[i].IconIndex = tabs[i].IconIndex;
        m_Tabs[i].Attention = tabs[i].Attention;
    }
    m_Active = active;
    if (m_HotTab >= m_Count)
        m_HotTab = -1;
    if (m_hwnd)
        InvalidateRect(m_hwnd, NULL, FALSE);
}

int
TabBand::TabWidth() const
{
    RECT client;
    int available;

    GetClientRect(m_hwnd, &client);
    available = client.right - Scale(8) - Scale(52) - (m_ShowMenu ? Scale(32) : 0);
    if (m_Count <= 0)
        return Scale(240);
    return max(Scale(72), min(Scale(240), available / m_Count));
}

RECT
TabBand::TabRect(int index) const
{
    RECT rc, client;
    int width = TabWidth();

    GetClientRect(m_hwnd, &client);
    rc.left = Scale(8) + index * width;
    rc.right = rc.left + width;
    rc.top = Scale(8);
    rc.bottom = client.bottom;
    return rc;
}

RECT
TabBand::CloseRect(int index) const
{
    RECT tab = TabRect(index), rc;
    int middle = (tab.top + tab.bottom) / 2;

    rc.right = tab.right - Scale(8);
    rc.left = rc.right - Scale(24);
    rc.top = middle - Scale(12);
    rc.bottom = middle + Scale(12);
    return rc;
}

RECT
TabBand::NewRect() const
{
    RECT rc, tab = TabRect(0);
    int middle = (tab.top + tab.bottom) / 2;

    rc.left = Scale(8) + m_Count * TabWidth() + Scale(8);
    rc.right = rc.left + Scale(32);
    rc.top = middle - Scale(14);
    rc.bottom = middle + Scale(14);
    return rc;
}

RECT
TabBand::MenuRect() const
{
    RECT rc = NewRect();

    if (!m_ShowMenu)
    {
        SetRectEmpty(&rc);
        return rc;
    }
    rc.left = rc.right + Scale(2);
    rc.right = rc.left + Scale(28);
    return rc;
}

BOOL
TabBand::ShowsClose(int index) const
{
    return index == m_Active || index == m_HotTab || TabWidth() >= Scale(110);
}

BOOL
TabBand::InTab(int index, POINT pt) const
{
    RECT rc = TabRect(index);

    return PtInRect(&rc, pt);
}

int
TabBand::HitTab(POINT pt, BOOL *close) const
{
    *close = FALSE;
    for (int order = -1; order < m_Count; order++)
    {
        int i = order < 0 ? m_Active : order;
        if (i < 0 || i >= m_Count || (order >= 0 && i == m_Active))
            continue;
        if (InTab(i, pt))
        {
            RECT button = CloseRect(i);
            *close = ShowsClose(i) && PtInRect(&button, pt);
            return i;
        }
    }
    return -1;
}

void
TabBand::SetHot(int tab, BOOL close, BOOL add, BOOL menu)
{
    if (tab == m_HotTab && close == m_HotClose && add == m_HotNew && menu == m_HotMenu)
        return;
    m_HotTab = tab;
    m_HotClose = close;
    m_HotNew = add;
    m_HotMenu = menu;
    InvalidateRect(m_hwnd, NULL, FALSE);
}

void
TabBand::Paint(HDC dc)
{
    BandPalette palette;
    RECT client;
    BITMAPINFO info = { };
    void *bits = NULL;
    HBITMAP bitmap;
    BOOL active = GetActiveWindow() == GetAncestor(m_hwnd, GA_ROOT);
    BOOL opaque = m_Background != CLR_INVALID;
    BOOL composed = FALSE;
    COLORREF strip, selectedInk, selectedGlyph;
    int width, height;

    GetClientRect(m_hwnd, &client);
    width = max(1, (int)client.right);
    height = max(1, (int)client.bottom);
    info.bmiHeader.biSize = sizeof(info.bmiHeader);
    info.bmiHeader.biWidth = width;
    info.bmiHeader.biHeight = -height;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    bitmap = CreateDIBSection(dc, &info, DIB_RGB_COLORS, &bits, NULL, 0);
    if (!bitmap)
        return;
    ZeroMemory(bits, width * height * 4);
    GetBandPalette(&palette);
    composed = !opaque && IsAppThemed() && SUCCEEDED(DwmIsCompositionEnabled(&composed)) && composed;
    strip = palette.Text;
    selectedInk = palette.Text;
    selectedGlyph = palette.Glyph;
    if (composed)
    {
        ULONG_PTR key = (ULONG_PTR)GetPropW(GetAncestor(m_hwnd, GA_ROOT), DWM_PROP_BACKDROP_COLOR);
        COLORREF frame = key ? (COLORREF)(key - 1) : RGB(0, 0, 0);

        strip = IsDarkColor(frame) ? RGB(255, 255, 255) : RGB(0, 0, 0);
    }
    if (!composed)
    {
        COLORREF face = opaque ? m_Background : GetSysColor(COLOR_3DFACE);
        ULONG pixel = 0xFF000000 | (GetRValue(face) << 16) | (GetGValue(face) << 8) | GetBValue(face);

        for (int i = 0; i < width * height; i++)
            ((ULONG *)bits)[i] = pixel;
        if (opaque)
            strip = IsDarkColor(m_Background) ? RGB(255, 255, 255) : RGB(0, 0, 0);
    }
    if (m_ActiveFill != CLR_INVALID)
        selectedInk = selectedGlyph = IsDarkColor(m_ActiveFill) ? RGB(0xF2, 0xF2, 0xF2) : RGB(0x1A, 0x1A, 0x1A);
    {
        Gdiplus::Bitmap surface(width, height, width * 4, PixelFormat32bppPARGB, (BYTE *)bits);
        Gdiplus::Graphics graphics(&surface);
        LOGFONTW logFont;
        Gdiplus::StringFormat format;
        int iconSize = GetSystemMetricsForDpi(SM_CXSMICON, GetDpiForWindow(m_hwnd));
        float radius = (float)Scale(8), foot = (float)Scale(8);
        BYTE inkAlpha = active ? 0xE0 : 0x90;

        graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
        graphics.SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAliasGridFit);
        format.SetFormatFlags(Gdiplus::StringFormatFlagsNoWrap);
        format.SetTrimming(Gdiplus::StringTrimmingEllipsisCharacter);
        format.SetLineAlignment(Gdiplus::StringAlignmentCenter);
        GetObjectW(m_Font ? m_Font : GetStockObject(DEFAULT_GUI_FONT), sizeof(logFont), &logFont);
        Gdiplus::Font font(dc, &logFont);

        for (int i = 0; i <= m_Count; i++)
        {
            RECT rc = i < m_Count ? TabRect(i) : NewRect();
            BOOL quiet = i < m_Count ? (i != m_Active && i != m_HotTab) : !m_HotNew;
            BOOL previousQuiet = i > 0 && i - 1 != m_Active && i - 1 != m_HotTab;
            float x = (float)(i < m_Count ? rc.left : rc.left - Scale(8)), middle;

            if (!i || !quiet || !previousQuiet)
                continue;
            middle = (float)(TabRect(0).top + TabRect(0).bottom) / 2.0f;
            {
                Gdiplus::Pen line(GlassColor(0x60, palette.Text), 1.0f);
                graphics.DrawLine(&line, x, middle - (float)Scale(8), x, middle + (float)Scale(8));
            }
        }
        {
            RECT add = NewRect();

            if (m_HotNew)
                FillButton(graphics, add, m_PressedNew ? 0x50 : 0x38, palette.Band, Scale(4));
            FillGlyph(graphics, L"add_16_regular", add, Scale(16), strip);
            if (m_ShowMenu)
            {
                RECT menu = MenuRect();

                if (m_HotMenu)
                    FillButton(graphics, menu, m_PressedMenu ? 0x50 : 0x38, palette.Band, Scale(4));
                FillGlyph(graphics, L"chevron_down_12_regular", menu, Scale(12), strip);
            }
        }
        for (int order = m_Count - 1; order >= -1; order--)
        {
            int i = order < 0 ? m_Active : order;
            RECT rc, close;
            BOOL selected, hot;
            float left, top, right, bottom;

            if (i < 0 || i >= m_Count || (order >= 0 && i == m_Active))
                continue;
            rc = TabRect(i);
            close = CloseRect(i);
            selected = i == m_Active;
            hot = i == m_HotTab;
            left = (float)rc.left;
            top = (float)rc.top;
            right = (float)rc.right;
            bottom = (float)rc.bottom;
            if (selected)
            {
                Gdiplus::GraphicsPath fillPath(Gdiplus::FillModeAlternate);
                Gdiplus::GraphicsPath edgePath(Gdiplus::FillModeAlternate);
                COLORREF upper = m_ActiveFill != CLR_INVALID ? m_ActiveFill : palette.Band;
                COLORREF lower = m_ActiveFill != CLR_INVALID ? m_ActiveFill :
                                 IsAppThemed() && m_ShadeBottom > 0 ? palette.BandShade : palette.Band;
                Gdiplus::LinearGradientBrush fill(Gdiplus::PointF(0.0f, 0.0f),
                                                  Gdiplus::PointF(0.0f, (float)max(m_ShadeBottom, height) + 1.0f),
                                                  GlassColor(255, upper), GlassColor(255, lower));
                Gdiplus::Pen border(GlassColor(255, palette.TabBorder), 1.0f);

                TabOutline(fillPath, left, top + 0.5f, right, bottom + 1.0f, radius, foot, TRUE);
                TabOutline(edgePath, left, top + 0.5f, right, bottom + 1.0f, radius, foot, FALSE);
                graphics.FillPath(&fill, &fillPath);
                graphics.DrawPath(&border, &edgePath);
            }
            else if (hot)
            {
                Gdiplus::GraphicsPath path(Gdiplus::FillModeAlternate);
                Gdiplus::SolidBrush fill(GlassColor(m_PressedTab == i ? 0x60 : 0x40, palette.Band));
                AddRoundRect(path, Gdiplus::RectF(left + (float)Scale(2), top + (float)Scale(2),
                                                  right - left - (float)Scale(4), bottom - top - (float)Scale(6)),
                             radius - 2.0f);
                graphics.FillPath(&fill, &path);
            }
            {
                Gdiplus::SolidBrush brush(GlassColor(selected ? 255 : inkAlpha, selected ? selectedInk : strip));
                float iconLeft = left + (float)Scale(12), textLeft = iconLeft + (float)iconSize + (float)Scale(8);
                float textRight = ShowsClose(i) ? (float)close.left - (float)Scale(4) : right - (float)Scale(8);
                int iconTop = (rc.top + rc.bottom - iconSize) / 2;

                DrawSmallIcon(graphics, m_Tabs[i].IconIndex, (int)iconLeft, iconTop, iconSize);
                if (m_Tabs[i].Attention && !selected)
                {
                    Gdiplus::SolidBrush dot(GlassColor(255, GetSysColor(COLOR_HIGHLIGHT)));
                    float size = (float)Scale(6);

                    graphics.FillEllipse(&dot, iconLeft + (float)iconSize - size / 2.0f, (float)iconTop - size / 2.0f,
                                         size, size);
                }
                if (textRight > textLeft)
                {
                    Gdiplus::RectF text(textLeft, top, textRight - textLeft, bottom - top);
                    graphics.DrawString(m_Tabs[i].Title, -1, &font, text, &format, &brush);
                }
            }
            if (ShowsClose(i))
            {
                RECT glyph = close;
                if (hot && m_HotClose)
                {
                    Gdiplus::GraphicsPath path(Gdiplus::FillModeAlternate);
                    Gdiplus::SolidBrush fill(GlassColor(selected ? 255 : 0x60, selected ? palette.BandBottom : palette.Band));
                    AddRoundRect(path, Gdiplus::RectF((float)close.left, (float)close.top,
                                                      (float)(close.right - close.left),
                                                      (float)(close.bottom - close.top)), (float)Scale(4));
                    graphics.FillPath(&fill, &path);
                }
                InflateRect(&glyph, -Scale(4), -Scale(4));
                FillIcon(graphics, L"dismiss_16_regular",
                         Gdiplus::RectF((float)glyph.left, (float)glyph.top, (float)(glyph.right - glyph.left),
                                        (float)(glyph.bottom - glyph.top)),
                         selected ? selectedGlyph : strip);
            }
        }
    }
    {
        HDC memory = CreateCompatibleDC(dc);
        HGDIOBJ old = SelectObject(memory, bitmap);
        BitBlt(dc, 0, 0, width, height, memory, 0, 0, SRCCOPY);
        SelectObject(memory, old);
        DeleteDC(memory);
    }
    DeleteObject(bitmap);
}

LRESULT
TabBand::HandleMessage(UINT msg, WPARAM wParam, LPARAM lParam)
{
    POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
    BOOL close;
    int index;

    switch (msg)
    {
        case WM_NCHITTEST:
        {
            RECT add = NewRect(), menu = MenuRect();
            ScreenToClient(m_hwnd, &pt);
            if (HitTab(pt, &close) >= 0 || PtInRect(&add, pt) || PtInRect(&menu, pt) || m_Background != CLR_INVALID)
                return HTCLIENT;
            return HTTRANSPARENT;
        }
        case WM_ERASEBKGND:
            return 1;
        case WM_PAINT:
        {
            PAINTSTRUCT ps;
            HDC dc = BeginPaint(m_hwnd, &ps);
            Paint(dc);
            EndPaint(m_hwnd, &ps);
            return 0;
        }
        case WM_SIZE:
            InvalidateRect(m_hwnd, NULL, FALSE);
            return 0;
        case WM_MOUSEMOVE:
        {
            RECT add = NewRect(), menu = MenuRect();
            if (!m_Tracking)
            {
                TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, m_hwnd, 0 };
                m_Tracking = TrackMouseEvent(&tme);
            }
            index = HitTab(pt, &close);
            if (m_DragTab >= 0 && (wParam & MK_LBUTTON) && GetCapture() == m_hwnd)
            {
                int target = max(0, min(m_Count - 1, (int)((pt.x - Scale(8)) / max(1, TabWidth()))));
                if (target != m_DragTab)
                {
                    int from = m_DragTab;
                    m_DragTab = target;
                    m_PressedTab = -1;
                    m_Host->OnTabMove(from, target);
                }
            }
            SetHot(index, close, index < 0 && PtInRect(&add, pt), index < 0 && PtInRect(&menu, pt));
            return 0;
        }
        case WM_MOUSELEAVE:
            m_Tracking = FALSE;
            SetHot(-1, FALSE, FALSE, FALSE);
            return 0;
        case WM_LBUTTONDOWN:
        case WM_LBUTTONDBLCLK:
        {
            RECT add = NewRect(), menu = MenuRect();
            index = HitTab(pt, &close);
            m_PressedTab = index;
            m_PressedClose = close;
            m_PressedNew = index < 0 && PtInRect(&add, pt);
            m_PressedMenu = index < 0 && PtInRect(&menu, pt);
            if (index >= 0 && !close)
            {
                if (index != m_Active)
                    m_Host->OnTabSelect(index);
                m_DragTab = index;
            }
            SetCapture(m_hwnd);
            return 0;
        }
        case WM_LBUTTONUP:
        {
            RECT add = NewRect(), menu = MenuRect();
            index = HitTab(pt, &close);
            if (GetCapture() == m_hwnd)
                ReleaseCapture();
            m_DragTab = -1;
            if (m_PressedClose && index == m_PressedTab && close)
                m_Host->OnTabClose(index);
            else if (m_PressedNew && index < 0 && PtInRect(&add, pt))
                m_Host->OnNewTab();
            else if (m_PressedMenu && index < 0 && PtInRect(&menu, pt))
            {
                POINT corner = { menu.left, menu.bottom };

                ClientToScreen(m_hwnd, &corner);
                m_PressedMenu = FALSE;
                m_Host->OnNewTabMenu(corner);
            }
            m_PressedTab = -1;
            m_PressedClose = m_PressedNew = m_PressedMenu = FALSE;
            return 0;
        }
        case WM_CAPTURECHANGED:
            m_DragTab = -1;
            return 0;
        case WM_MBUTTONUP:
            index = HitTab(pt, &close);
            if (index >= 0)
                m_Host->OnTabClose(index);
            return 0;
        case WM_RBUTTONUP:
            index = HitTab(pt, &close);
            if (index >= 0)
            {
                ClientToScreen(m_hwnd, &pt);
                m_Host->OnTabMenu(index, pt);
            }
            return 0;
    }
    return DefWindowProcW(m_hwnd, msg, wParam, lParam);
}

}
