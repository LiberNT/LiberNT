/*
 * PROJECT:     LiberNT Explorer
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     File Explorer tab, address and search bands
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "cabinet.h"
#include <shellframe.h>
#include <commoncontrols.h>

#define IDC_CRUMB_OVERFLOW 0x100
#define IDC_CRUMB(index) (0x200 + (index))
#define IDC_CRUMB_CHEVRON(index) (0x300 + (index))
#define IDC_CRUMB_ROOT 0x400
#define IDC_CRUMB_ROOT_CHEVRON 0x401

static COLORREF
BlendColor(COLORREF base, COLORREF ink, int amount)
{
    return RGB(GetRValue(base) + (GetRValue(ink) - GetRValue(base)) * amount / 255,
               GetGValue(base) + (GetGValue(ink) - GetGValue(base)) * amount / 255,
               GetBValue(base) + (GetBValue(ink) - GetBValue(base)) * amount / 255);
}

BOOL
CabinetComposedFrame(VOID)
{
    BOOL composed = FALSE;

    return IsAppThemed() && SUCCEEDED(DwmIsCompositionEnabled(&composed)) && composed;
}

void
CabinetGetPalette(CabinetPalette *palette)
{
    COLORREF window = GetSysColor(COLOR_WINDOW), text = GetSysColor(COLOR_WINDOWTEXT);

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
}

HIMAGELIST
CabinetGlyphImages(const LPCWSTR *names, UINT count, int size, COLORREF color)
{
    HIMAGELIST list = ImageList_Create(size, size, ILC_COLOR32, count, 0);

    if (!list)
        return NULL;
    for (UINT i = 0; i < count; i++)
    {
        HICON icon = ShellFrame::CreateGlyphIcon(names[i], size, color);
        ImageList_AddIcon(list, icon);
        if (icon)
            DestroyIcon(icon);
    }
    return list;
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

void
CabinetFillRound(HDC dc, const RECT &rc, int radius, COLORREF color)
{
    Gdiplus::Graphics graphics(dc);
    Gdiplus::GraphicsPath path(Gdiplus::FillModeAlternate);
    Gdiplus::SolidBrush brush(Gdiplus::Color(255, GetRValue(color), GetGValue(color), GetBValue(color)));

    graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
    AddRoundRect(path, Gdiplus::RectF((float)rc.left, (float)rc.top, (float)(rc.right - rc.left),
                                      (float)(rc.bottom - rc.top)), (float)radius);
    graphics.FillPath(&brush, &path);
}

LRESULT
CabinetToolbarCustomDraw(NMTBCUSTOMDRAW *draw, COLORREF base)
{
    CabinetPalette palette;
    UINT state = draw->nmcd.uItemState;
    int amount = 0;

    if (draw->nmcd.dwDrawStage == CDDS_PREPAINT)
        return CDRF_NOTIFYITEMDRAW;
    if (draw->nmcd.dwDrawStage != CDDS_ITEMPREPAINT)
        return CDRF_DODEFAULT;
    if (!(state & CDIS_DISABLED))
    {
        if (state & CDIS_SELECTED)
            amount = 0x10;
        else if ((state & CDIS_HOT) && (state & CDIS_CHECKED))
            amount = 0x24;
        else if (state & CDIS_HOT)
            amount = 0x18;
        else if (state & CDIS_CHECKED)
            amount = 0x14;
    }
    if (amount)
    {
        CabinetGetPalette(&palette);
        CabinetFillRound(draw->nmcd.hdc, draw->nmcd.rc, MulDiv(4, GetDpiForWindow(draw->nmcd.hdr.hwndFrom), 96),
                         BlendColor(base, palette.Text, amount));
    }
    return TBCDRF_NOBACKGROUND | TBCDRF_NOEDGES | TBCDRF_NOOFFSET;
}

static HICON
RotatedGlyphIcon(LPCWSTR name, int size, COLORREF color, float angle)
{
    Gdiplus::Bitmap bitmap(size, size, PixelFormat32bppARGB);
    HICON icon = NULL;

    {
        Gdiplus::Graphics graphics(&bitmap);
        graphics.Clear(Gdiplus::Color(0, 0, 0, 0));
        graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
        graphics.TranslateTransform(size / 2.0f, size / 2.0f);
        graphics.RotateTransform(angle);
        graphics.TranslateTransform(-size / 2.0f, -size / 2.0f);
        ShellFrame::FillIcon(graphics, name, Gdiplus::RectF(0.0f, 0.0f, (float)size, (float)size), color);
    }
    bitmap.GetHICON(&icon);
    return icon;
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

static int
SmallIconIndex(PCIDLIST_ABSOLUTE pidl)
{
    SHFILEINFOW info;

    if (!pidl || !SHGetFileInfoW((LPCWSTR)pidl, 0, &info, sizeof(info), SHGFI_PIDL | SHGFI_SYSICONINDEX | SHGFI_SMALLICON))
        return -1;
    return info.iIcon;
}

CCabinetBand::CCabinetBand() :
    m_Refs(1),
    m_hwnd(NULL),
    m_Host(NULL)
{
}

CCabinetBand::~CCabinetBand()
{
}

STDMETHODIMP
CCabinetBand::QueryInterface(REFIID riid, void **ppv)
{
    if (!ppv)
        return E_POINTER;
    if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_IOleWindow))
    {
        *ppv = static_cast<IOleWindow *>(this);
        AddRef();
        return S_OK;
    }
    *ppv = NULL;
    return E_NOINTERFACE;
}

STDMETHODIMP_(ULONG)
CCabinetBand::AddRef()
{
    return InterlockedIncrement(&m_Refs);
}

STDMETHODIMP_(ULONG)
CCabinetBand::Release()
{
    ULONG refs = InterlockedDecrement(&m_Refs);

    if (!refs)
        delete this;
    return refs;
}

STDMETHODIMP
CCabinetBand::GetWindow(HWND *window)
{
    if (!window)
        return E_POINTER;
    *window = m_hwnd;
    return m_hwnd ? S_OK : E_FAIL;
}

STDMETHODIMP
CCabinetBand::ContextSensitiveHelp(BOOL enter)
{
    UNREFERENCED_PARAMETER(enter);
    return E_NOTIMPL;
}

LRESULT CALLBACK
CCabinetBand::WindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    CCabinetBand *self;

    if (msg == WM_NCCREATE)
    {
        self = (CCabinetBand *)((CREATESTRUCTW *)lParam)->lpCreateParams;
        self->m_hwnd = hwnd;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)self);
    }
    else
    {
        self = (CCabinetBand *)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
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
CCabinetBand::CreateBand(HWND parent, LPCWSTR className)
{
    WNDCLASSEXW wc = { sizeof(wc) };
    HINSTANCE instance = CabinetModule();

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
CCabinetBand::Scale(int value) const
{
    return MulDiv(value, m_hwnd ? GetDpiForWindow(m_hwnd) : 96, 96);
}

CCabinetTabBand::CCabinetTabBand() :
    m_Count(0),
    m_Active(-1),
    m_HotTab(-1),
    m_HotClose(FALSE),
    m_HotNew(FALSE),
    m_Tracking(FALSE),
    m_DragTab(-1),
    m_PressedTab(-1),
    m_PressedClose(FALSE),
    m_PressedNew(FALSE),
    m_Font(NULL),
    m_ShadeBottom(0)
{
}

void
CCabinetTabBand::SetShadeBottom(int bottom)
{
    if (bottom == m_ShadeBottom)
        return;
    m_ShadeBottom = bottom;
    if (m_hwnd)
        InvalidateRect(m_hwnd, NULL, FALSE);
}

BOOL
CCabinetTabBand::Create(HWND parent, ICabinetBandHost *host, HFONT font)
{
    m_Host = host;
    m_Font = font;
    return CreateBand(parent, L"LiberNTExplorerTabBand");
}

void
CCabinetTabBand::SetTabs(const CabinetTabItem *tabs, int count, int active)
{
    m_Count = min(count, (int)_countof(m_Tabs));
    for (int i = 0; i < m_Count; i++)
    {
        StringCchCopyW(m_Tabs[i].Title, _countof(m_Tabs[i].Title), tabs[i].Title ? tabs[i].Title : L"");
        m_Tabs[i].IconIndex = tabs[i].IconIndex;
    }
    m_Active = active;
    if (m_HotTab >= m_Count)
        m_HotTab = -1;
    if (m_hwnd)
        InvalidateRect(m_hwnd, NULL, FALSE);
}

int
CCabinetTabBand::TabWidth() const
{
    RECT client;
    int available;

    GetClientRect(m_hwnd, &client);
    available = client.right - Scale(8) - Scale(52);
    if (m_Count <= 0)
        return Scale(240);
    return max(Scale(72), min(Scale(240), available / m_Count));
}

RECT
CCabinetTabBand::TabRect(int index) const
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
CCabinetTabBand::CloseRect(int index) const
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
CCabinetTabBand::NewRect() const
{
    RECT rc, tab = TabRect(0);
    int middle = (tab.top + tab.bottom) / 2;

    rc.left = Scale(8) + m_Count * TabWidth() + Scale(8);
    rc.right = rc.left + Scale(32);
    rc.top = middle - Scale(14);
    rc.bottom = middle + Scale(14);
    return rc;
}

BOOL
CCabinetTabBand::ShowsClose(int index) const
{
    return index == m_Active || index == m_HotTab || TabWidth() >= Scale(110);
}

BOOL
CCabinetTabBand::InTab(int index, POINT pt) const
{
    RECT rc = TabRect(index);

    return PtInRect(&rc, pt);
}

int
CCabinetTabBand::HitTab(POINT pt, BOOL *close) const
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
CCabinetTabBand::SetHot(int tab, BOOL close, BOOL add)
{
    if (tab == m_HotTab && close == m_HotClose && add == m_HotNew)
        return;
    m_HotTab = tab;
    m_HotClose = close;
    m_HotNew = add;
    InvalidateRect(m_hwnd, NULL, FALSE);
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

void
CCabinetTabBand::Paint(HDC dc)
{
    CabinetPalette palette;
    RECT client;
    BITMAPINFO info = { };
    void *bits = NULL;
    HBITMAP bitmap;
    BOOL active = GetActiveWindow() == GetAncestor(m_hwnd, GA_ROOT);
    BOOL composed = CabinetComposedFrame();
    COLORREF strip;
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
    CabinetGetPalette(&palette);
    strip = composed ? RGB(255, 255, 255) : palette.Text;
    if (!composed)
    {
        COLORREF face = GetSysColor(COLOR_3DFACE);
        ULONG pixel = 0xFF000000 | (GetRValue(face) << 16) | (GetGValue(face) << 8) | GetBValue(face);

        for (int i = 0; i < width * height; i++)
            ((ULONG *)bits)[i] = pixel;
    }
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
            RECT add = NewRect(), glyph;
            if (m_HotNew)
            {
                Gdiplus::GraphicsPath path(Gdiplus::FillModeAlternate);
                Gdiplus::SolidBrush fill(GlassColor(m_PressedNew ? 0x50 : 0x38, palette.Band));
                AddRoundRect(path, Gdiplus::RectF((float)add.left, (float)add.top, (float)(add.right - add.left),
                                                  (float)(add.bottom - add.top)), (float)Scale(4));
                graphics.FillPath(&fill, &path);
            }
            glyph.left = (add.left + add.right) / 2 - Scale(8);
            glyph.top = (add.top + add.bottom) / 2 - Scale(8);
            glyph.right = glyph.left + Scale(16);
            glyph.bottom = glyph.top + Scale(16);
            ShellFrame::FillIcon(graphics, L"add_16_regular",
                                 Gdiplus::RectF((float)glyph.left, (float)glyph.top, (float)(glyph.right - glyph.left),
                                                (float)(glyph.bottom - glyph.top)), strip);
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
                Gdiplus::LinearGradientBrush fill(Gdiplus::PointF(0.0f, 0.0f),
                                                  Gdiplus::PointF(0.0f, (float)max(m_ShadeBottom, height) + 1.0f),
                                                  GlassColor(255, palette.Band),
                                                  GlassColor(255, IsAppThemed() && m_ShadeBottom > 0 ?
                                                                     palette.BandShade : palette.Band));
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
                Gdiplus::SolidBrush brush(GlassColor(selected ? 255 : inkAlpha, selected ? palette.Text : strip));
                float iconLeft = left + (float)Scale(12), textLeft = iconLeft + (float)iconSize + (float)Scale(8);
                float textRight = ShowsClose(i) ? (float)close.left - (float)Scale(4) : right - (float)Scale(8);
                int iconTop = (rc.top + rc.bottom - iconSize) / 2;

                DrawSmallIcon(graphics, m_Tabs[i].IconIndex, (int)iconLeft, iconTop, iconSize);
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
                ShellFrame::FillIcon(graphics, L"dismiss_16_regular",
                                     Gdiplus::RectF((float)glyph.left, (float)glyph.top,
                                                    (float)(glyph.right - glyph.left),
                                                    (float)(glyph.bottom - glyph.top)),
                                     selected ? palette.Glyph : strip);
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
CCabinetTabBand::HandleMessage(UINT msg, WPARAM wParam, LPARAM lParam)
{
    POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
    BOOL close;
    int index;

    switch (msg)
    {
        case WM_NCHITTEST:
        {
            RECT add = NewRect();
            ScreenToClient(m_hwnd, &pt);
            if (HitTab(pt, &close) >= 0 || PtInRect(&add, pt))
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
            RECT add = NewRect();
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
            SetHot(index, close, index < 0 && PtInRect(&add, pt));
            return 0;
        }
        case WM_MOUSELEAVE:
            m_Tracking = FALSE;
            SetHot(-1, FALSE, FALSE);
            return 0;
        case WM_LBUTTONDOWN:
        case WM_LBUTTONDBLCLK:
        {
            RECT add = NewRect();
            index = HitTab(pt, &close);
            m_PressedTab = index;
            m_PressedClose = close;
            m_PressedNew = index < 0 && PtInRect(&add, pt);
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
            RECT add = NewRect();
            index = HitTab(pt, &close);
            if (GetCapture() == m_hwnd)
                ReleaseCapture();
            m_DragTab = -1;
            if (m_PressedClose && index == m_PressedTab && close)
                m_Host->OnTabClose(index);
            else if (m_PressedNew && index < 0 && PtInRect(&add, pt))
                m_Host->OnNewTab();
            m_PressedTab = -1;
            m_PressedClose = m_PressedNew = FALSE;
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

CCabinetAddressBand::CCabinetAddressBand() :
    m_Toolbar(NULL),
    m_Edit(NULL),
    m_Images(NULL),
    m_Font(NULL),
    m_CrumbCount(0),
    m_FirstVisible(0),
    m_Editing(FALSE),
    m_BoxBrush(NULL),
    m_DismissId(0),
    m_DismissTick(0)
{
    ZeroMemory(m_Crumbs, sizeof(m_Crumbs));
}

CCabinetAddressBand::~CCabinetAddressBand()
{
    FreeCrumbs();
    if (m_Images)
        ImageList_Destroy(m_Images);
    if (m_BoxBrush)
        DeleteObject(m_BoxBrush);
}

void
CCabinetAddressBand::RefreshGlyphs()
{
    CabinetPalette palette;
    static const LPCWSTR Glyphs[] = { L"chevron_right_16_regular" };
    HIMAGELIST old = m_Images;
    int size;

    CabinetGetPalette(&palette);
    size = GetSystemMetricsForDpi(SM_CXSMICON, GetDpiForWindow(m_hwnd));
    m_Images = CabinetGlyphImages(Glyphs, _countof(Glyphs), size, palette.Glyph);
    if (m_Images)
    {
        ImageList_SetImageCount(m_Images, 2);
        for (int frame = 1; frame <= 3; frame++)
        {
            HICON icon = RotatedGlyphIcon(Glyphs[0], size, palette.Glyph, 30.0f * frame);
            ImageList_AddIcon(m_Images, icon);
            if (icon)
                DestroyIcon(icon);
        }
        {
            HICON icon = ShellFrame::CreateGlyphIcon(L"more_horizontal_20_regular", size, palette.Glyph);
            ImageList_AddIcon(m_Images, icon);
            if (icon)
                DestroyIcon(icon);
        }
    }
    if (m_Toolbar)
    {
        SendMessageW(m_Toolbar, TB_SETIMAGELIST, 0, (LPARAM)m_Images);
        Layout();
    }
    if (old)
        ImageList_Destroy(old);
}

BOOL
CCabinetAddressBand::Create(HWND parent, ICabinetBandHost *host, HFONT font)
{
    m_Host = host;
    m_Font = font;
    if (!CreateBand(parent, L"LiberNTExplorerAddressBand"))
        return FALSE;
    RefreshGlyphs();
    m_Toolbar = CreateWindowExW(0, TOOLBARCLASSNAMEW, NULL,
                                WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | TBSTYLE_FLAT | TBSTYLE_LIST |
                                TBSTYLE_TRANSPARENT | CCS_NODIVIDER | CCS_NOPARENTALIGN | CCS_NORESIZE,
                                0, 0, 0, 0, m_hwnd, NULL, CabinetModule(), NULL);
    SendMessageW(m_Toolbar, TB_BUTTONSTRUCTSIZE, sizeof(TBBUTTON), 0);
    SendMessageW(m_Toolbar, TB_SETEXTENDEDSTYLE, 0, TBSTYLE_EX_MIXEDBUTTONS);
    SendMessageW(m_Toolbar, WM_SETFONT, (WPARAM)m_Font, FALSE);
    SendMessageW(m_Toolbar, TB_SETIMAGELIST, 0, (LPARAM)m_Images);
    m_Edit = CreateWindowExW(0, WC_EDITW, L"", WS_CHILD | ES_AUTOHSCROLL, 0, 0, 0, 0, m_hwnd, NULL,
                             CabinetModule(), NULL);
    SendMessageW(m_Edit, WM_SETFONT, (WPARAM)m_Font, FALSE);
    SHAutoComplete(m_Edit, SHACF_FILESYS_DIRS | SHACF_URLALL);
    SetWindowSubclass(m_Edit, EditProc, 1, (DWORD_PTR)this);
    return TRUE;
}

void
CCabinetAddressBand::FreeCrumbs()
{
    for (int i = 0; i < m_CrumbCount; i++)
    {
        if (m_Crumbs[i])
            ILFree(m_Crumbs[i]);
        m_Crumbs[i] = NULL;
    }
    m_CrumbCount = 0;
}

void
CCabinetAddressBand::SetLocation(PCIDLIST_ABSOLUTE pidl)
{
    PIDLIST_ABSOLUTE chain[CABINET_MAX_CRUMBS];
    PIDLIST_ABSOLUTE walk;
    int count = 0;

    FreeCrumbs();
    if (pidl)
    {
        walk = ILClone(pidl);
        while (walk && count < CABINET_MAX_CRUMBS)
        {
            if (ILIsEmpty(walk) && count)
                break;
            chain[count++] = ILClone(walk);
            if (ILIsEmpty(walk) || !ILRemoveLastID(walk))
                break;
        }
        if (walk)
            ILFree(walk);
        for (int i = count - 1; i >= 0; i--)
            m_Crumbs[m_CrumbCount++] = chain[i];
    }
    if (m_Editing)
        EndEdit();
    else
        Layout();
}

void
CCabinetAddressBand::Layout()
{
    RECT client;
    int count, available, padding = Scale(4), width;
    TBBUTTON button;
    HICON root;
    WCHAR names[CABINET_MAX_CRUMBS][MAX_PATH];

    if (!m_Toolbar)
        return;
    GetClientRect(m_hwnd, &client);
    if (m_Editing)
    {
        int height = Scale(20);
        ShowWindow(m_Toolbar, SW_HIDE);
        MoveWindow(m_Edit, Scale(12), (client.bottom - height) / 2, max(0, (int)client.right - Scale(24)), height,
                   TRUE);
        ShowWindow(m_Edit, SW_SHOW);
        return;
    }
    ShowWindow(m_Edit, SW_HIDE);
    SendMessageW(m_Toolbar, WM_SETREDRAW, FALSE, 0);
    count = (int)SendMessageW(m_Toolbar, TB_BUTTONCOUNT, 0, 0);
    while (count-- > 0)
        SendMessageW(m_Toolbar, TB_DELETEBUTTON, 0, 0);
    root = m_CrumbCount ? ImageList_GetIcon(SmallSystemImages(), SmallIconIndex(m_Crumbs[0]), ILD_TRANSPARENT) : NULL;
    if (m_Images)
        ImageList_ReplaceIcon(m_Images, 1, root);
    if (root)
        DestroyIcon(root);
    for (int i = 0; i < m_CrumbCount; i++)
    {
        PWSTR name = NULL;
        if (FAILED(SHGetNameFromIDList(m_Crumbs[i], SIGDN_NORMALDISPLAY, &name)))
            name = NULL;
        StringCchCopyW(names[i], MAX_PATH, name ? name : L"");
        CoTaskMemFree(name);
    }
    available = client.right - padding * 2;
    for (m_FirstVisible = 0; m_FirstVisible < m_CrumbCount; m_FirstVisible++)
    {
        count = (int)SendMessageW(m_Toolbar, TB_BUTTONCOUNT, 0, 0);
        while (count-- > 0)
            SendMessageW(m_Toolbar, TB_DELETEBUTTON, 0, 0);
        ZeroMemory(&button, sizeof(button));
        button.iBitmap = 1;
        button.idCommand = IDC_CRUMB_ROOT;
        button.fsState = TBSTATE_ENABLED;
        button.fsStyle = BTNS_BUTTON | BTNS_AUTOSIZE;
        SendMessageW(m_Toolbar, TB_ADDBUTTONSW, 1, (LPARAM)&button);
        button.iBitmap = 0;
        button.idCommand = IDC_CRUMB_ROOT_CHEVRON;
        SendMessageW(m_Toolbar, TB_ADDBUTTONSW, 1, (LPARAM)&button);
        if (m_FirstVisible)
        {
            button.iBitmap = 5;
            button.idCommand = IDC_CRUMB_OVERFLOW;
            button.fsStyle = BTNS_BUTTON | BTNS_AUTOSIZE;
            button.iString = 0;
            SendMessageW(m_Toolbar, TB_ADDBUTTONSW, 1, (LPARAM)&button);
        }
        for (int i = m_FirstVisible; i < m_CrumbCount; i++)
        {
            button.iBitmap = 0;
            button.idCommand = IDC_CRUMB_CHEVRON(i ? i - 1 : 0);
            button.fsStyle = BTNS_BUTTON | BTNS_AUTOSIZE;
            button.iString = 0;
            if (i)
                SendMessageW(m_Toolbar, TB_ADDBUTTONSW, 1, (LPARAM)&button);
            button.iBitmap = I_IMAGENONE;
            button.idCommand = IDC_CRUMB(i);
            button.fsStyle = BTNS_BUTTON | BTNS_SHOWTEXT | BTNS_NOPREFIX;
            button.iString = (INT_PTR)names[i];
            SendMessageW(m_Toolbar, TB_ADDBUTTONSW, 1, (LPARAM)&button);
            {
                TBBUTTONINFOW info = { sizeof(info), TBIF_SIZE };
                SIZE extent = { 0, 0 };
                HDC dc = GetDC(m_Toolbar);
                HGDIOBJ old = SelectObject(dc, m_Font);
                GetTextExtentPoint32W(dc, names[i], lstrlenW(names[i]), &extent);
                SelectObject(dc, old);
                ReleaseDC(m_Toolbar, dc);
                info.cx = (WORD)(extent.cx + Scale(16));
                SendMessageW(m_Toolbar, TB_SETBUTTONINFOW, IDC_CRUMB(i), (LPARAM)&info);
            }
        }
        if (m_CrumbCount)
        {
            button.iBitmap = 0;
            button.idCommand = IDC_CRUMB_CHEVRON(m_CrumbCount - 1);
            button.fsStyle = BTNS_BUTTON | BTNS_AUTOSIZE;
            button.iString = 0;
            SendMessageW(m_Toolbar, TB_ADDBUTTONSW, 1, (LPARAM)&button);
        }
        SendMessageW(m_Toolbar, TB_AUTOSIZE, 0, 0);
        {
            SIZE size = { 0, 0 };
            SendMessageW(m_Toolbar, TB_GETMAXSIZE, 0, (LPARAM)&size);
            width = size.cx;
        }
        if (width <= available || m_FirstVisible + 1 >= m_CrumbCount)
            break;
    }
    {
        SIZE size = { 0, 0 };
        SendMessageW(m_Toolbar, TB_GETMAXSIZE, 0, (LPARAM)&size);
        MoveWindow(m_Toolbar, padding, max(0, (int)(client.bottom - size.cy) / 2), min((int)size.cx, available),
                   size.cy, FALSE);
    }
    SendMessageW(m_Toolbar, WM_SETREDRAW, TRUE, 0);
    ShowWindow(m_Toolbar, SW_SHOW);
    InvalidateRect(m_hwnd, NULL, TRUE);
    InvalidateRect(m_Toolbar, NULL, TRUE);
}

void
CCabinetAddressBand::BeginEdit()
{
    PWSTR text = NULL;

    if (!m_Editing)
    {
        m_Editing = TRUE;
        if (m_CrumbCount && SUCCEEDED(SHGetNameFromIDList(m_Crumbs[m_CrumbCount - 1], SIGDN_DESKTOPABSOLUTEEDITING,
                                                          &text)))
        {
            SetWindowTextW(m_Edit, text);
            CoTaskMemFree(text);
        }
        Layout();
        InvalidateRect(m_hwnd, NULL, TRUE);
    }
    SetFocus(m_Edit);
    SendMessageW(m_Edit, EM_SETSEL, 0, -1);
}

void
CCabinetAddressBand::EndEdit()
{
    if (!m_Editing)
        return;
    m_Editing = FALSE;
    Layout();
    InvalidateRect(m_hwnd, NULL, TRUE);
}

RECT
CCabinetAddressBand::ButtonScreenRect(UINT id)
{
    RECT rc = { 0, 0, 0, 0 };

    SendMessageW(m_Toolbar, TB_GETRECT, id, (LPARAM)&rc);
    MapWindowPoints(m_Toolbar, NULL, (POINT *)&rc, 2);
    return rc;
}

void
CCabinetAddressBand::AnimateChevron(UINT id, BOOL open)
{
    static const int Frames[] = { 2, 3, 4 };

    SendMessageW(m_Toolbar, TB_PRESSBUTTON, id, MAKELONG(open, 0));
    for (int i = 0; i < (int)_countof(Frames); i++)
    {
        int frame = open ? Frames[i] : (i + 1 < (int)_countof(Frames) ? Frames[_countof(Frames) - 2 - i] : 0);
        SendMessageW(m_Toolbar, TB_CHANGEBITMAP, id, MAKELPARAM(frame, 0));
        UpdateWindow(m_Toolbar);
        if (i + 1 < (int)_countof(Frames))
            Sleep(16);
    }
}

void
CCabinetAddressBand::NoteMenuDismiss(UINT id)
{
    RECT rc = { 0, 0, 0, 0 };
    POINT pt;

    m_DismissId = 0;
    if (!m_Toolbar || !(GetAsyncKeyState(GetSystemMetrics(SM_SWAPBUTTON) ? VK_RBUTTON : VK_LBUTTON) & 0x8000))
        return;
    GetCursorPos(&pt);
    ScreenToClient(m_Toolbar, &pt);
    if (SendMessageW(m_Toolbar, TB_GETRECT, id, (LPARAM)&rc) && PtInRect(&rc, pt))
    {
        m_DismissId = id;
        m_DismissTick = GetTickCount();
    }
}

void
CCabinetAddressBand::ShowChildren(int index)
{
    IShellFolder *folder = NULL;
    IEnumIDList *items = NULL;
    PITEMID_CHILD children[256];
    PITEMID_CHILD child;
    PCUITEMID_CHILD next = NULL;
    int count = 0;
    HMENU popup;
    RECT anchor = ButtonScreenRect(index < 0 ? IDC_CRUMB_ROOT_CHEVRON : IDC_CRUMB_CHEVRON(index));
    PIDLIST_ABSOLUTE root = NULL;
    UINT command;

    if (index >= m_CrumbCount || !m_CrumbCount)
        return;
    if (index + 1 < m_CrumbCount)
        next = ILFindLastID(m_Crumbs[index + 1]);
    if (index < 0)
    {
        if (FAILED(SHGetFolderLocation(NULL, CSIDL_DESKTOP, NULL, 0, &root)) ||
            FAILED(SHGetDesktopFolder(&folder)))
        {
            if (root)
                ILFree(root);
            return;
        }
    }
    else if (FAILED(SHBindToObject(NULL, m_Crumbs[index], NULL, IID_PPV_ARG(IShellFolder, &folder))))
        return;
    if (folder->EnumObjects(m_hwnd, SHCONTF_FOLDERS, &items) == S_OK && items)
    {
        while (count < (int)_countof(children) && items->Next(1, &child, NULL) == S_OK)
        {
            int position = count;
            while (position > 0 && (short)HRESULT_CODE(folder->CompareIDs(0, children[position - 1], child)) > 0)
            {
                children[position] = children[position - 1];
                position--;
            }
            children[position] = child;
            count++;
        }
        items->Release();
    }
    popup = CreatePopupMenu();
    for (int i = 0; i < count; i++)
    {
        STRRET name;
        WCHAR text[MAX_PATH];
        if (FAILED(folder->GetDisplayNameOf(children[i], SHGDN_INFOLDER, &name)) ||
            FAILED(StrRetToBufW(&name, children[i], text, _countof(text))))
            text[0] = UNICODE_NULL;
        AppendMenuW(popup, MF_STRING, 1 + i, text);
        if (next && folder->CompareIDs(0, children[i], next) == 0)
            SetMenuDefaultItem(popup, 1 + i, FALSE);
    }
    if (count)
    {
        UINT id = index < 0 ? IDC_CRUMB_ROOT_CHEVRON : IDC_CRUMB_CHEVRON(index);
        AnimateChevron(id, TRUE);
        command = TrackPopupMenuEx(popup, TPM_RETURNCMD | TPM_LEFTALIGN | TPM_TOPALIGN, anchor.left, anchor.bottom,
                                   m_hwnd, NULL);
        NoteMenuDismiss(id);
        if (m_Toolbar)
            AnimateChevron(id, FALSE);
    }
    else
        command = 0;
    DestroyMenu(popup);
    if (command >= 1 && (int)command <= count)
    {
        PIDLIST_ABSOLUTE target = ILCombine(index < 0 ? root : m_Crumbs[index], children[command - 1]);
        if (target)
        {
            m_Host->OnAddressNavigate(target);
            ILFree(target);
        }
    }
    for (int i = 0; i < count; i++)
        ILFree(children[i]);
    if (root)
        ILFree(root);
    folder->Release();
}

void
CCabinetAddressBand::ShowOverflow()
{
    HMENU popup = CreatePopupMenu();
    RECT anchor = ButtonScreenRect(IDC_CRUMB_OVERFLOW);
    UINT command;

    for (int i = m_FirstVisible - 1; i >= 0; i--)
    {
        PWSTR name = NULL;
        if (SUCCEEDED(SHGetNameFromIDList(m_Crumbs[i], SIGDN_NORMALDISPLAY, &name)))
        {
            AppendMenuW(popup, MF_STRING, 1 + i, name);
            CoTaskMemFree(name);
        }
    }
    command = TrackPopupMenuEx(popup, TPM_RETURNCMD | TPM_LEFTALIGN | TPM_TOPALIGN, anchor.left, anchor.bottom,
                               m_hwnd, NULL);
    NoteMenuDismiss(IDC_CRUMB_OVERFLOW);
    DestroyMenu(popup);
    if (command >= 1 && (int)command <= m_FirstVisible)
    {
        PIDLIST_ABSOLUTE target = ILClone(m_Crumbs[command - 1]);
        if (target)
        {
            m_Host->OnAddressNavigate(target);
            ILFree(target);
        }
    }
}

LRESULT CALLBACK
CCabinetAddressBand::EditProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam, UINT_PTR id, DWORD_PTR data)
{
    CCabinetAddressBand *self = (CCabinetAddressBand *)data;

    UNREFERENCED_PARAMETER(id);
    switch (msg)
    {
        case WM_KEYDOWN:
            if (wParam == VK_RETURN)
            {
                WCHAR text[MAX_PATH * 2];
                GetWindowTextW(hwnd, text, _countof(text));
                self->m_Host->OnAddressText(text);
                return 0;
            }
            if (wParam == VK_ESCAPE)
            {
                self->EndEdit();
                self->m_Host->OnAddressCancel();
                return 0;
            }
            break;
        case WM_CHAR:
            if (wParam == L'\r' || wParam == 0x1B)
                return 0;
            break;
        case WM_GETDLGCODE:
            return DLGC_WANTALLKEYS | DefSubclassProc(hwnd, msg, wParam, lParam);
        case WM_KILLFOCUS:
        {
            LRESULT result = DefSubclassProc(hwnd, msg, wParam, lParam);
            HWND focus = (HWND)wParam;
            if (!focus || (focus != self->m_hwnd && !IsChild(self->m_hwnd, focus) &&
                           GetAncestor(focus, GA_ROOT) == GetAncestor(hwnd, GA_ROOT)))
                self->EndEdit();
            return result;
        }
    }
    return DefSubclassProc(hwnd, msg, wParam, lParam);
}

LRESULT
CCabinetAddressBand::HandleMessage(UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
        case WM_ERASEBKGND:
        {
            CabinetPalette palette;
            RECT client;
            HBRUSH brush;
            HDC dc = (HDC)wParam;
            CabinetGetPalette(&palette);
            GetClientRect(m_hwnd, &client);
            brush = CreateSolidBrush(palette.Band);
            FillRect(dc, &client, brush);
            DeleteObject(brush);
            CabinetFillRound(dc, client, Scale(4), m_Editing ? palette.Window : palette.Box);
            if (m_Editing)
            {
                RECT line = { client.left + Scale(1), client.bottom - Scale(2), client.right - Scale(1), client.bottom };
                FillRect(dc, &line, GetSysColorBrush(COLOR_HIGHLIGHT));
            }
            return 1;
        }
        case WM_SIZE:
            Layout();
            return 0;
        case WM_LBUTTONDOWN:
            BeginEdit();
            return 0;
        case WM_NOTIFY:
            if (((NMHDR *)lParam)->hwndFrom == m_Toolbar && ((NMHDR *)lParam)->code == NM_CUSTOMDRAW)
            {
                NMTBCUSTOMDRAW *draw = (NMTBCUSTOMDRAW *)lParam;
                CabinetPalette palette;
                LRESULT result;
                UINT id = (UINT)draw->nmcd.dwItemSpec;

                CabinetGetPalette(&palette);
                result = CabinetToolbarCustomDraw(draw, m_Editing ? palette.Window : palette.Box);
                if (draw->nmcd.dwDrawStage == CDDS_ITEMPREPAINT && id >= IDC_CRUMB(0) &&
                    id < IDC_CRUMB(CABINET_MAX_CRUMBS))
                {
                    WCHAR text[MAX_PATH];
                    RECT rc = draw->nmcd.rc;
                    HGDIOBJ old;
                    if (SendMessageW(m_Toolbar, TB_GETBUTTONTEXTW, id, (LPARAM)text) < 0)
                        text[0] = UNICODE_NULL;
                    old = SelectObject(draw->nmcd.hdc, m_Font);
                    SetBkMode(draw->nmcd.hdc, TRANSPARENT);
                    SetTextColor(draw->nmcd.hdc, palette.Text);
                    DrawTextW(draw->nmcd.hdc, text, -1, &rc,
                              DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
                    SelectObject(draw->nmcd.hdc, old);
                    return CDRF_SKIPDEFAULT;
                }
                return result;
            }
            break;
        case WM_CTLCOLOREDIT:
        {
            CabinetPalette palette;
            CabinetGetPalette(&palette);
            if (m_BoxBrush)
                DeleteObject(m_BoxBrush);
            m_BoxBrush = CreateSolidBrush(palette.Window);
            SetTextColor((HDC)wParam, palette.Text);
            SetBkColor((HDC)wParam, palette.Window);
            return (LRESULT)m_BoxBrush;
        }
        case WM_COMMAND:
        {
            UINT command = LOWORD(wParam);
            if ((HWND)lParam != m_Toolbar)
                break;
            if (m_DismissId && command == m_DismissId && GetTickCount() - m_DismissTick < 600)
            {
                m_DismissId = 0;
                return 0;
            }
            m_DismissId = 0;
            if (command == IDC_CRUMB_OVERFLOW)
                ShowOverflow();
            else if (command == IDC_CRUMB_ROOT)
                BeginEdit();
            else if (command == IDC_CRUMB_ROOT_CHEVRON)
                ShowChildren(-1);
            else if (command >= IDC_CRUMB(0) && command < IDC_CRUMB(CABINET_MAX_CRUMBS))
            {
                int index = command - IDC_CRUMB(0);
                if (index < m_CrumbCount)
                {
                    PIDLIST_ABSOLUTE target = ILClone(m_Crumbs[index]);
                    if (target)
                    {
                        m_Host->OnAddressNavigate(target);
                        ILFree(target);
                    }
                }
            }
            else if (command >= IDC_CRUMB_CHEVRON(0) && command < IDC_CRUMB_CHEVRON(CABINET_MAX_CRUMBS))
                ShowChildren(command - IDC_CRUMB_CHEVRON(0));
            return 0;
        }
        case WM_NCDESTROY:
            m_Toolbar = NULL;
            m_Edit = NULL;
            break;
    }
    return DefWindowProcW(m_hwnd, msg, wParam, lParam);
}

CCabinetSearchBox::CCabinetSearchBox() :
    m_Edit(NULL),
    m_Glyph(NULL),
    m_BoxBrush(NULL)
{
}

CCabinetSearchBox::~CCabinetSearchBox()
{
    if (m_Glyph)
        DestroyIcon(m_Glyph);
    if (m_BoxBrush)
        DeleteObject(m_BoxBrush);
}

void
CCabinetSearchBox::RefreshGlyphs()
{
    CabinetPalette palette;

    if (m_Glyph)
        DestroyIcon(m_Glyph);
    CabinetGetPalette(&palette);
    m_Glyph = ShellFrame::CreateGlyphIcon(L"search_16_regular", Scale(16), palette.Glyph);
    if (m_hwnd)
        InvalidateRect(m_hwnd, NULL, TRUE);
}

BOOL
CCabinetSearchBox::Create(HWND parent, HFONT font)
{
    if (!CreateBand(parent, L"LiberNTExplorerSearchBox"))
        return FALSE;
    RefreshGlyphs();
    m_Edit = CreateWindowExW(0, WC_EDITW, L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL, 0, 0, 0, 0,
                             m_hwnd, NULL, CabinetModule(), NULL);
    SendMessageW(m_Edit, WM_SETFONT, (WPARAM)font, FALSE);
    return m_Edit != NULL;
}

LRESULT
CCabinetSearchBox::HandleMessage(UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
        case WM_ERASEBKGND:
        {
            CabinetPalette palette;
            RECT client;
            HBRUSH brush;
            HDC dc = (HDC)wParam;
            BOOL focus = GetFocus() == m_Edit;
            CabinetGetPalette(&palette);
            GetClientRect(m_hwnd, &client);
            brush = CreateSolidBrush(palette.Band);
            FillRect(dc, &client, brush);
            DeleteObject(brush);
            CabinetFillRound(dc, client, Scale(4), focus ? palette.Window : palette.Box);
            if (focus)
            {
                RECT line = { client.left + Scale(1), client.bottom - Scale(2), client.right - Scale(1), client.bottom };
                FillRect(dc, &line, GetSysColorBrush(COLOR_HIGHLIGHT));
            }
            if (m_Glyph)
                DrawIconEx(dc, client.right - Scale(26), (client.bottom - Scale(16)) / 2, m_Glyph, Scale(16), Scale(16),
                           0, NULL, DI_NORMAL);
            return 1;
        }
        case WM_SIZE:
        {
            int height = Scale(20);
            if (m_Edit)
                MoveWindow(m_Edit, Scale(12), (HIWORD(lParam) - height) / 2, max(0, (int)LOWORD(lParam) - Scale(44)),
                           height, TRUE);
            return 0;
        }
        case WM_LBUTTONDOWN:
            SetFocus(m_Edit);
            return 0;
        case WM_COMMAND:
            if ((HWND)lParam == m_Edit && (HIWORD(wParam) == EN_SETFOCUS || HIWORD(wParam) == EN_KILLFOCUS))
            {
                InvalidateRect(m_hwnd, NULL, TRUE);
                InvalidateRect(m_Edit, NULL, TRUE);
            }
            return 0;
        case WM_CTLCOLOREDIT:
        {
            CabinetPalette palette;
            COLORREF back;
            CabinetGetPalette(&palette);
            back = GetFocus() == m_Edit ? palette.Window : palette.Box;
            if (m_BoxBrush)
                DeleteObject(m_BoxBrush);
            m_BoxBrush = CreateSolidBrush(back);
            SetTextColor((HDC)wParam, palette.Text);
            SetBkColor((HDC)wParam, back);
            return (LRESULT)m_BoxBrush;
        }
        case WM_NCDESTROY:
            m_Edit = NULL;
            break;
    }
    return DefWindowProcW(m_hwnd, msg, wParam, lParam);
}
