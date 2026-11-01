/*
 * PROJECT:     LiberNT Terminal
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Terminal view: drawing, selection, clipboard, search and keyboard input
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "terminal.h"

#define VIEW_CLASS L"LiberNT.TerminalView"
#define TIMER_CURSOR 1
#define TIMER_AUTOSCROLL 2

namespace
{

bool KeyDown(int key)
{
    return (GetKeyState(key) & 0x8000) != 0;
}

COLORREF Blend(COLORREF a, COLORREF b)
{
    return RGB((GetRValue(a) + GetRValue(b)) / 2, (GetGValue(a) + GetGValue(b)) / 2, (GetBValue(a) + GetBValue(b)) / 2);
}

COLORREF CubeColor(unsigned index)
{
    static const BYTE Levels[6] = { 0, 95, 135, 175, 215, 255 };

    if (index >= 232)
    {
        BYTE gray = (BYTE)(8 + (index - 232) * 10);
        return RGB(gray, gray, gray);
    }
    index -= 16;
    return RGB(Levels[(index / 36) % 6], Levels[(index / 6) % 6], Levels[index % 6]);
}

std::string ToUtf8(const std::wstring &text)
{
    return Vt::EncodeUtf8(FromWide(text));
}

}

TermView::TermView(TerminalWindow *owner) :
    m_Owner(owner)
{
}

TermView::~TermView()
{
    ReleaseFonts();
    if (m_Buffer)
        DeleteObject(m_Buffer);
}

HWND
TermView::Create(HWND parent)
{
    WNDCLASSEXW wc = { sizeof(wc) };

    if (!GetClassInfoExW(g_Instance, VIEW_CLASS, &wc))
    {
        wc.cbSize = sizeof(wc);
        wc.style = CS_DBLCLKS;
        wc.lpfnWndProc = WindowProc;
        wc.hInstance = g_Instance;
        wc.hCursor = LoadCursorW(NULL, IDC_IBEAM);
        wc.lpszClassName = VIEW_CLASS;
        RegisterClassExW(&wc);
    }
    CreateWindowExW(0, VIEW_CLASS, L"", WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_CLIPSIBLINGS, 0, 0, 0, 0, parent,
                    NULL, g_Instance, this);
    return m_hwnd;
}

LRESULT CALLBACK
TermView::WindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    TermView *self;

    if (msg == WM_NCCREATE)
    {
        self = (TermView *)((CREATESTRUCTW *)lParam)->lpCreateParams;
        self->m_hwnd = hwnd;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)self);
    }
    else
    {
        self = (TermView *)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    }
    if (!self)
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    return self->HandleMessage(msg, wParam, lParam);
}

void
TermView::ReleaseFonts()
{
    for (HFONT &font : m_Fonts)
    {
        if (font)
            DeleteObject(font);
        font = NULL;
    }
}

void
TermView::SetZoom(int zoom)
{
    if (zoom < -8)
        zoom = -8;
    if (zoom > 40)
        zoom = 40;
    m_Zoom = zoom;
    ApplyFont(m_Dpi);
}

void
TermView::ApplyFont(UINT dpi)
{
    const Profile &profile = m_Tab && m_Tab->Term ? m_Tab->Term->Settings() : g_Settings.Default();
    int size = profile.FontSize + m_Zoom;
    TEXTMETRICW metrics;
    SIZE extent;
    HDC hdc;
    HGDIOBJ old;

    m_Dpi = dpi ? dpi : 96;
    if (size < 4)
        size = 4;
    ReleaseFonts();
    for (int i = 0; i < 4; i++)
    {
        int weight = (i & 1) || profile.FontBold ? FW_BOLD : FW_NORMAL;
        BOOL italic = (i & 2) != 0;

        m_Fonts[i] = CreateFontW(-MulDiv(size, (int)m_Dpi, 72), 0, 0, 0, weight, italic, FALSE, FALSE, DEFAULT_CHARSET,
                                 OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, FIXED_PITCH | FF_MODERN,
                                 profile.FontFace.c_str());
    }

    hdc = GetDC(m_hwnd);
    old = SelectObject(hdc, m_Fonts[0]);
    GetTextMetricsW(hdc, &metrics);
    GetTextExtentPoint32W(hdc, L"MMMMMMMMMM", 10, &extent);
    SelectObject(hdc, old);
    ReleaseDC(m_hwnd, hdc);

    m_Cell.cy = metrics.tmHeight > 0 ? metrics.tmHeight : 16;
    m_Cell.cx = (extent.cx + 5) / 10;
    if (m_Cell.cx < 1)
        m_Cell.cx = metrics.tmAveCharWidth > 0 ? metrics.tmAveCharWidth : 8;
    m_Ascent = metrics.tmAscent;
}

void
TermView::GridForSize(int width, int height, int *cols, int *rows) const
{
    const Profile &profile = m_Tab && m_Tab->Term ? m_Tab->Term->Settings() : g_Settings.Default();
    int padding = MulDiv(profile.Padding, (int)m_Dpi, 96);

    *cols = (width - 2 * padding) / (m_Cell.cx ? m_Cell.cx : 1);
    *rows = (height - 2 * padding) / (m_Cell.cy ? m_Cell.cy : 1);
    if (*cols < 2)
        *cols = 2;
    if (*rows < 1)
        *rows = 1;
}

void
TermView::GridFor(const Profile &profile, int width, int height, int *cols, int *rows) const
{
    int size = profile.FontSize + m_Zoom, padding = MulDiv(profile.Padding, (int)m_Dpi, 96);
    HDC hdc = GetDC(m_hwnd);
    HFONT font;
    HGDIOBJ old;
    TEXTMETRICW metrics;
    SIZE extent, cell;

    if (size < 4)
        size = 4;
    font = CreateFontW(-MulDiv(size, (int)m_Dpi, 72), 0, 0, 0, profile.FontBold ? FW_BOLD : FW_NORMAL, FALSE, FALSE,
                       FALSE, DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                       FIXED_PITCH | FF_MODERN, profile.FontFace.c_str());
    old = SelectObject(hdc, font);
    GetTextMetricsW(hdc, &metrics);
    GetTextExtentPoint32W(hdc, L"MMMMMMMMMM", 10, &extent);
    SelectObject(hdc, old);
    DeleteObject(font);
    ReleaseDC(m_hwnd, hdc);
    cell.cx = (extent.cx + 5) / 10 > 0 ? (extent.cx + 5) / 10 : 8;
    cell.cy = metrics.tmHeight > 0 ? metrics.tmHeight : 16;
    *cols = (width - 2 * padding) / cell.cx;
    *rows = (height - 2 * padding) / cell.cy;
    if (*cols < 2)
        *cols = 2;
    if (*rows < 1)
        *rows = 1;
}

void
TermView::SetTab(Tab *tab)
{
    m_Tab = tab;
    m_Selecting = false;
    ApplyFont(m_Dpi);
    UpdateScrollBar();
    Refresh(true);
}

size_t
TermView::FirstVisibleLine() const
{
    Vt::Screen &screen = m_Tab->Term->Screen();
    size_t history = screen.HistorySize();
    size_t offset = m_Tab->ScrollOffset > history ? history : m_Tab->ScrollOffset;

    return history - offset;
}

void
TermView::UpdateScrollBar()
{
    SCROLLINFO info = { sizeof(info) };

    if (!m_hwnd)
        return;
    info.fMask = SIF_RANGE | SIF_PAGE | SIF_POS | SIF_DISABLENOSCROLL;
    if (m_Tab && m_Tab->Term)
    {
        Vt::Screen &screen = m_Tab->Term->Screen();

        info.nMin = 0;
        info.nMax = (int)screen.TotalLines() - 1;
        info.nPage = (UINT)screen.Rows();
        info.nPos = (int)FirstVisibleLine();
    }
    SetScrollInfo(m_hwnd, SB_VERT, &info, TRUE);
}

void
TermView::Refresh(bool all)
{
    UNREFERENCED_PARAMETER(all);
    if (m_hwnd)
        InvalidateRect(m_hwnd, NULL, FALSE);
}

void
TermView::OnOutput(size_t scrolled)
{
    if (!m_Tab || !m_Tab->Term)
        return;
    if (m_Tab->ScrollOffset > 0)
    {
        m_Tab->ScrollOffset += scrolled;
        if (m_Tab->ScrollOffset > m_Tab->Term->Screen().HistorySize())
            m_Tab->ScrollOffset = m_Tab->Term->Screen().HistorySize();
    }
    m_CursorOn = true;
    if (m_Focused)
        SetTimer(m_hwnd, TIMER_CURSOR, GetCaretBlinkTime(), NULL);
    UpdateScrollBar();
    Refresh(false);
}

void
TermView::ScrollBy(long lines)
{
    long long offset;

    if (!m_Tab || !m_Tab->Term)
        return;
    offset = (long long)m_Tab->ScrollOffset - lines;
    if (offset < 0)
        offset = 0;
    if ((size_t)offset > m_Tab->Term->Screen().HistorySize())
        offset = (long long)m_Tab->Term->Screen().HistorySize();
    if ((size_t)offset == m_Tab->ScrollOffset)
        return;
    m_Tab->ScrollOffset = (size_t)offset;
    UpdateScrollBar();
    Refresh(true);
}

void
TermView::ScrollToBottom()
{
    if (m_Tab && m_Tab->ScrollOffset)
    {
        m_Tab->ScrollOffset = 0;
        UpdateScrollBar();
        Refresh(true);
    }
}

void
TermView::ScrollToTop()
{
    if (m_Tab && m_Tab->Term)
        ScrollBy(-(long)m_Tab->Term->Screen().HistorySize());
}

COLORREF
TermView::ResolveColor(uint32_t color, bool foreground, bool bold) const
{
    const ColorScheme &scheme = g_Settings.SchemeFor(m_Tab->Term->Settings());
    Vt::Screen &screen = m_Tab->Term->Screen();
    unsigned kind = color & Vt::ColorKindMask, value = color & Vt::ColorValueMask;
    bool present;
    uint32_t custom;

    if (kind == Vt::ColorDefault)
        return foreground ? scheme.Foreground : scheme.Background;
    if (kind == Vt::ColorRgb)
        return RGB((value >> 16) & 0xFF, (value >> 8) & 0xFF, value & 0xFF);
    if (foreground && bold && value < 8)
        value += 8;
    custom = screen.PaletteOverride(value, &present);
    if (present)
        return RGB((custom >> 16) & 0xFF, (custom >> 8) & 0xFF, custom & 0xFF);
    if (value < 16)
        return scheme.Table[value];
    return CubeColor(value);
}

bool
TermView::IsSelected(size_t line, int col) const
{
    TextPosition start, end;

    if (!m_Tab->Select.Active)
        return false;
    m_Tab->Select.Normalized(start, end);
    if (line < start.Line || line > end.Line)
        return false;
    if (m_Tab->Select.Block)
    {
        int left = m_Tab->Select.Anchor.Col < m_Tab->Select.Caret.Col ? m_Tab->Select.Anchor.Col : m_Tab->Select.Caret.Col;
        int right = m_Tab->Select.Anchor.Col < m_Tab->Select.Caret.Col ? m_Tab->Select.Caret.Col : m_Tab->Select.Anchor.Col;
        return col >= left && col <= right;
    }
    if (start.Line == end.Line)
        return col >= start.Col && col <= end.Col;
    if (line == start.Line)
        return col >= start.Col;
    if (line == end.Line)
        return col <= end.Col;
    return true;
}

bool
TermView::IsMatch(size_t line, int col, bool *current) const
{
    for (size_t i = 0; i < m_Tab->Matches.size(); i++)
    {
        const SearchMatch &match = m_Tab->Matches[i];

        if (match.Line == line && col >= match.Col && col < match.Col + match.Length)
        {
            *current = (int)i == m_Tab->CurrentMatch;
            return true;
        }
        if (match.Line > line)
            break;
    }
    return false;
}

void
TermView::DrawRow(HDC hdc, int row, const RECT &client)
{
    Vt::Screen &screen = m_Tab->Term->Screen();
    const ColorScheme &scheme = g_Settings.SchemeFor(m_Tab->Term->Settings());
    const Profile &profile = m_Tab->Term->Settings();
    int padding = MulDiv(profile.Padding, (int)m_Dpi, 96);
    size_t lineIndex = FirstVisibleLine() + (size_t)row;
    int y = padding + row * m_Cell.cy;
    std::wstring text;
    std::vector<int> advances;
    RECT rowRect = { client.left, y, client.right, y + m_Cell.cy };
    bool reverse = screen.ReverseVideo();

    if (lineIndex >= screen.TotalLines())
    {
        HBRUSH brush = CreateSolidBrush(reverse ? scheme.Foreground : scheme.Background);
        FillRect(hdc, &rowRect, brush);
        DeleteObject(brush);
        return;
    }

    const Vt::Line &line = screen.LineAt(lineIndex);
    int cols = (int)line.Cells.size();
    int col = 0;

    {
        RECT left = { client.left, y, padding, y + m_Cell.cy };
        RECT right = { padding + cols * m_Cell.cx, y, client.right, y + m_Cell.cy };
        HBRUSH brush = CreateSolidBrush(reverse ? scheme.Foreground : scheme.Background);
        FillRect(hdc, &left, brush);
        FillRect(hdc, &right, brush);
        DeleteObject(brush);
    }

    while (col < cols)
    {
        const Vt::Cell &first = line.Cells[col];
        bool selected = IsSelected(lineIndex, col);
        bool currentMatch = false;
        bool match = IsMatch(lineIndex, col, &currentMatch);
        int start = col;
        COLORREF fore, back;
        uint16_t flags = first.Attr.Flags;
        HFONT font;
        RECT runRect;

        text.clear();
        advances.clear();
        while (col < cols)
        {
            const Vt::Cell &cell = line.Cells[col];
            bool cellCurrent = false;

            if (cell.Attr != first.Attr || IsSelected(lineIndex, col) != selected ||
                IsMatch(lineIndex, col, &cellCurrent) != match || cellCurrent != currentMatch)
            {
                break;
            }
            if (cell.Attr.Flags & Vt::CellWideTail)
            {
                if (!advances.empty())
                    advances.back() += m_Cell.cx;
                else
                {
                    text += L' ';
                    advances.push_back(m_Cell.cx);
                }
                col++;
                continue;
            }
            {
                char32_t ch = cell.Ch ? cell.Ch : U' ';

                if (ch >= 0x10000)
                {
                    ch -= 0x10000;
                    text += (wchar_t)(0xD800 + (ch >> 10));
                    text += (wchar_t)(0xDC00 + (ch & 0x3FF));
                    advances.push_back(m_Cell.cx);
                    advances.push_back(0);
                }
                else
                {
                    text += (wchar_t)ch;
                    advances.push_back(m_Cell.cx);
                }
            }
            col++;
        }

        fore = ResolveColor(first.Attr.Foreground, true, (flags & Vt::CellBold) != 0);
        back = ResolveColor(first.Attr.Background, false, false);
        if (reverse)
        {
            if (first.Attr.Foreground == Vt::ColorDefault)
                fore = scheme.Background;
            if (first.Attr.Background == Vt::ColorDefault)
                back = scheme.Foreground;
        }
        if (flags & Vt::CellInverse)
        {
            COLORREF swap = fore;
            fore = back;
            back = swap;
        }
        if (flags & Vt::CellFaint)
            fore = Blend(fore, back);
        if (flags & Vt::CellHidden)
            fore = back;
        if (match)
        {
            back = currentMatch ? RGB(0xF2, 0x8C, 0x28) : RGB(0xE8, 0xC4, 0x4A);
            fore = RGB(0x10, 0x10, 0x10);
        }
        if (selected)
            back = scheme.Selection;

        font = m_Fonts[((flags & Vt::CellBold) ? 1 : 0) | ((flags & Vt::CellItalic) ? 2 : 0)];
        SelectObject(hdc, font);
        SetTextColor(hdc, fore);
        SetBkColor(hdc, back);
        runRect.left = padding + start * m_Cell.cx;
        runRect.right = padding + col * m_Cell.cx;
        runRect.top = y;
        runRect.bottom = y + m_Cell.cy;
        ExtTextOutW(hdc, runRect.left, y, ETO_OPAQUE | ETO_CLIPPED, &runRect, text.c_str(), (UINT)text.size(),
                    advances.data());

        if (flags & (Vt::CellUnderline | Vt::CellDoubleUnderline | Vt::CellStrike))
        {
            HPEN pen = CreatePen(PS_SOLID, MulDiv(1, (int)m_Dpi, 96), fore);
            HGDIOBJ oldPen = SelectObject(hdc, pen);

            if (flags & (Vt::CellUnderline | Vt::CellDoubleUnderline))
            {
                int underline = y + m_Ascent + MulDiv(2, (int)m_Dpi, 96);

                if (underline >= y + m_Cell.cy)
                    underline = y + m_Cell.cy - 1;
                MoveToEx(hdc, runRect.left, underline, NULL);
                LineTo(hdc, runRect.right, underline);
                if ((flags & Vt::CellDoubleUnderline) && underline - 2 > y)
                {
                    MoveToEx(hdc, runRect.left, underline - 2, NULL);
                    LineTo(hdc, runRect.right, underline - 2);
                }
            }
            if (flags & Vt::CellStrike)
            {
                int strike = y + m_Cell.cy / 2;
                MoveToEx(hdc, runRect.left, strike, NULL);
                LineTo(hdc, runRect.right, strike);
            }
            SelectObject(hdc, oldPen);
            DeleteObject(pen);
        }
    }
}

void
TermView::DrawCursor(HDC hdc)
{
    Vt::Screen &screen = m_Tab->Term->Screen();
    const ColorScheme &scheme = g_Settings.SchemeFor(m_Tab->Term->Settings());
    const Profile &profile = m_Tab->Term->Settings();
    int padding = MulDiv(profile.Padding, (int)m_Dpi, 96);
    size_t cursorLine = screen.HistorySize() + (size_t)screen.CursorY();
    size_t first = FirstVisibleLine();
    Vt::CursorShape shape = screen.CursorStyle() != Vt::CursorShape::Default ? screen.CursorStyle() : profile.Cursor;
    int thickness = MulDiv(2, (int)m_Dpi, 96);
    RECT rc;
    HBRUSH brush;

    if (!screen.CursorVisible() || cursorLine < first || cursorLine >= first + (size_t)screen.Rows() || m_Tab->Term->Exited())
        return;
    if (m_Focused && !m_CursorOn)
        return;

    rc.left = padding + screen.CursorX() * m_Cell.cx;
    rc.top = padding + (int)(cursorLine - first) * m_Cell.cy;
    rc.right = rc.left + m_Cell.cx;
    rc.bottom = rc.top + m_Cell.cy;
    if (screen.CursorX() < screen.Cols() &&
        (screen.LineAt(cursorLine).Cells[screen.CursorX()].Attr.Flags & Vt::CellWide))
    {
        rc.right += m_Cell.cx;
    }
    brush = CreateSolidBrush(scheme.Cursor);
    if (!m_Focused)
    {
        FrameRect(hdc, &rc, brush);
    }
    else if (shape == Vt::CursorShape::Bar)
    {
        rc.right = rc.left + thickness;
        FillRect(hdc, &rc, brush);
    }
    else if (shape == Vt::CursorShape::Underline)
    {
        rc.top = rc.bottom - thickness;
        FillRect(hdc, &rc, brush);
    }
    else
    {
        const Vt::Cell &cell = screen.LineAt(cursorLine).Cells[screen.CursorX() < screen.Cols() ? screen.CursorX() : 0];
        wchar_t ch = cell.Ch && cell.Ch < 0x10000 ? (wchar_t)cell.Ch : L' ';

        SelectObject(hdc, m_Fonts[(cell.Attr.Flags & Vt::CellBold) ? 1 : 0]);
        SetTextColor(hdc, scheme.Background);
        SetBkColor(hdc, scheme.Cursor);
        ExtTextOutW(hdc, rc.left, rc.top, ETO_OPAQUE | ETO_CLIPPED, &rc, &ch, 1, NULL);
    }
    DeleteObject(brush);
}

void
TermView::Paint(HDC hdc, const RECT &update)
{
    RECT client;
    HDC memory;
    HGDIOBJ oldBitmap;

    GetClientRect(m_hwnd, &client);
    if (client.right <= 0 || client.bottom <= 0)
        return;
    if (!m_Buffer || m_BufferSize.cx < client.right || m_BufferSize.cy < client.bottom)
    {
        if (m_Buffer)
            DeleteObject(m_Buffer);
        m_Buffer = CreateCompatibleBitmap(hdc, client.right, client.bottom);
        m_BufferSize.cx = client.right;
        m_BufferSize.cy = client.bottom;
    }
    memory = CreateCompatibleDC(hdc);
    oldBitmap = SelectObject(memory, m_Buffer);

    if (!m_Tab || !m_Tab->Term)
    {
        HBRUSH brush = CreateSolidBrush(m_Owner->SchemeBackground());
        FillRect(memory, &client, brush);
        DeleteObject(brush);
    }
    else
    {
        Vt::Screen &screen = m_Tab->Term->Screen();
        const ColorScheme &scheme = g_Settings.SchemeFor(m_Tab->Term->Settings());
        const Profile &profile = m_Tab->Term->Settings();
        int padding = MulDiv(profile.Padding, (int)m_Dpi, 96);
        int rowsHeight = padding + screen.Rows() * m_Cell.cy;
        HBRUSH brush = CreateSolidBrush(screen.ReverseVideo() ? scheme.Foreground : scheme.Background);
        RECT top = { 0, 0, client.right, padding };
        RECT bottom = { 0, rowsHeight, client.right, client.bottom };

        FillRect(memory, &top, brush);
        FillRect(memory, &bottom, brush);
        DeleteObject(brush);
        SetBkMode(memory, OPAQUE);
        for (int row = 0; row < screen.Rows(); row++)
        {
            int y = padding + row * m_Cell.cy;

            if (y + m_Cell.cy < update.top || y > update.bottom)
                continue;
            DrawRow(memory, row, client);
        }
        DrawCursor(memory);
        if (!m_Tab->ErrorText.empty())
        {
            RECT text = client;

            InflateRect(&text, -padding, -padding);
            SetTextColor(memory, scheme.Foreground);
            SetBkMode(memory, TRANSPARENT);
            SelectObject(memory, m_Fonts[0]);
            DrawTextW(memory, m_Tab->ErrorText.c_str(), -1, &text, DT_LEFT | DT_TOP | DT_WORDBREAK);
        }
    }

    BitBlt(hdc, update.left, update.top, update.right - update.left, update.bottom - update.top, memory, update.left,
           update.top, SRCCOPY);
    SelectObject(memory, oldBitmap);
    DeleteDC(memory);
}

TextPosition
TermView::HitTest(int x, int y, bool clampToGrid) const
{
    TextPosition position;
    Vt::Screen &screen = m_Tab->Term->Screen();
    const Profile &profile = m_Tab->Term->Settings();
    int padding = MulDiv(profile.Padding, (int)m_Dpi, 96);
    int row = (y - padding) >= 0 ? (y - padding) / m_Cell.cy : -1;
    int col = (x - padding) >= 0 ? (x - padding) / m_Cell.cx : 0;
    long long line = (long long)FirstVisibleLine() + row;

    UNREFERENCED_PARAMETER(clampToGrid);
    if (line < 0)
        line = 0;
    if (line >= (long long)screen.TotalLines())
        line = (long long)screen.TotalLines() - 1;
    if (col >= screen.Cols())
        col = screen.Cols() - 1;
    position.Line = (size_t)line;
    position.Col = col;
    return position;
}

void
TermView::ExtendWord(TextPosition &start, TextPosition &end) const
{
    Vt::Screen &screen = m_Tab->Term->Screen();
    const Vt::Line &line = screen.LineAt(start.Line);
    const std::wstring &delimiters = g_Settings.WordDelimiters;
    auto isWord = [&](int col)
    {
        char32_t ch = line.Cells[col].Ch;
        return ch && ch != U' ' && (ch > 0xFFFF || delimiters.find((wchar_t)ch) == std::wstring::npos);
    };

    if (!isWord(start.Col))
        return;
    while (start.Col > 0 && isWord(start.Col - 1))
        start.Col--;
    while (end.Col + 1 < (int)line.Cells.size() && isWord(end.Col + 1))
        end.Col++;
}

std::wstring
TermView::SelectedText() const
{
    std::wstring text;
    TextPosition start, end;

    if (!m_Tab || !m_Tab->Term || !m_Tab->Select.Active)
        return text;
    Vt::Screen &screen = m_Tab->Term->Screen();
    m_Tab->Select.Normalized(start, end);
    for (size_t lineIndex = start.Line; lineIndex <= end.Line && lineIndex < screen.TotalLines(); lineIndex++)
    {
        const Vt::Line &line = screen.LineAt(lineIndex);
        int from = 0, to = (int)line.Cells.size() - 1;
        std::basic_string<char32_t> part;

        if (m_Tab->Select.Block)
        {
            from = m_Tab->Select.Anchor.Col < m_Tab->Select.Caret.Col ? m_Tab->Select.Anchor.Col : m_Tab->Select.Caret.Col;
            to = m_Tab->Select.Anchor.Col < m_Tab->Select.Caret.Col ? m_Tab->Select.Caret.Col : m_Tab->Select.Anchor.Col;
        }
        else
        {
            if (lineIndex == start.Line)
                from = start.Col;
            if (lineIndex == end.Line)
                to = end.Col;
        }
        for (int col = from; col <= to && col < (int)line.Cells.size(); col++)
        {
            if (!(line.Cells[col].Attr.Flags & Vt::CellWideTail))
                part += line.Cells[col].Ch ? line.Cells[col].Ch : U' ';
        }
        if (m_Tab->Select.Block || !line.Wrapped || lineIndex == end.Line)
        {
            while (!part.empty() && part[part.size() - 1] == U' ')
                part.pop_back();
        }
        text += ToWide(part);
        if (lineIndex != end.Line && (m_Tab->Select.Block || !line.Wrapped))
            text += L"\r\n";
    }
    return text;
}

void
TermView::Copy(bool clearSelection)
{
    std::wstring text;
    HGLOBAL memory;

    if (!m_Tab || !m_Tab->Term || !m_Tab->Select.Active)
        return;
    text = SelectedText();
    if (OpenClipboard(m_hwnd))
    {
        EmptyClipboard();
        memory = GlobalAlloc(GMEM_MOVEABLE, (text.size() + 1) * sizeof(wchar_t));
        if (memory)
        {
            memcpy(GlobalLock(memory), text.c_str(), (text.size() + 1) * sizeof(wchar_t));
            GlobalUnlock(memory);
            if (!SetClipboardData(CF_UNICODETEXT, memory))
                GlobalFree(memory);
        }
        CloseClipboard();
    }
    if (clearSelection)
    {
        m_Tab->Select.Active = false;
        Refresh(true);
    }
}

void
TermView::Paste()
{
    std::wstring text, normalized;
    HANDLE data;

    if (!m_Tab || !m_Tab->Term || !OpenClipboard(m_hwnd))
        return;
    data = GetClipboardData(CF_UNICODETEXT);
    if (data)
    {
        const wchar_t *source = (const wchar_t *)GlobalLock(data);
        if (source)
            text = source;
        GlobalUnlock(data);
    }
    CloseClipboard();
    for (size_t i = 0; i < text.size(); i++)
    {
        if (text[i] == L'\r' && i + 1 < text.size() && text[i + 1] == L'\n')
            continue;
        normalized += text[i] == L'\n' ? L'\r' : text[i];
    }
    if (normalized.empty())
        return;
    ScrollToBottom();
    if (m_Tab->Term->Screen().BracketedPaste())
        SendBytes("\x1b[200~" + ToUtf8(normalized) + "\x1b[201~");
    else
        SendText(normalized);
}

void
TermView::SelectAll()
{
    if (!m_Tab || !m_Tab->Term)
        return;
    m_Tab->Select.Active = true;
    m_Tab->Select.Block = false;
    m_Tab->Select.Anchor = TextPosition();
    m_Tab->Select.Caret.Line = m_Tab->Term->Screen().TotalLines() - 1;
    m_Tab->Select.Caret.Col = m_Tab->Term->Screen().Cols() - 1;
    Refresh(true);
}

void
TermView::Find(const std::wstring &query, bool up, bool restart)
{
    if (!m_Tab || !m_Tab->Term)
        return;
    if (restart || query != m_Tab->SearchText)
    {
        Vt::Screen &screen = m_Tab->Term->Screen();
        std::basic_string<char32_t> needle = FromWide(query);

        m_Tab->SearchText = query;
        m_Tab->Matches.clear();
        m_Tab->CurrentMatch = -1;
        for (char32_t &ch : needle)
            ch = (char32_t)towlower((wint_t)ch);
        if (!needle.empty())
        {
            for (size_t lineIndex = 0; lineIndex < screen.TotalLines(); lineIndex++)
            {
                const Vt::Line &line = screen.LineAt(lineIndex);
                std::basic_string<char32_t> haystack;

                for (const Vt::Cell &cell : line.Cells)
                    haystack += (char32_t)towlower((wint_t)(cell.Ch ? cell.Ch : U' '));
                for (size_t found = haystack.find(needle); found != std::basic_string<char32_t>::npos;
                     found = haystack.find(needle, found + 1))
                {
                    m_Tab->Matches.push_back({ lineIndex, (int)found, (int)needle.size() });
                }
            }
        }
        if (!m_Tab->Matches.empty())
            m_Tab->CurrentMatch = (int)m_Tab->Matches.size() - 1;
    }
    else if (!m_Tab->Matches.empty())
    {
        int count = (int)m_Tab->Matches.size();
        m_Tab->CurrentMatch = (m_Tab->CurrentMatch + (up ? -1 : 1) + count) % count;
    }

    if (m_Tab->CurrentMatch >= 0)
    {
        Vt::Screen &screen = m_Tab->Term->Screen();
        size_t line = m_Tab->Matches[m_Tab->CurrentMatch].Line;
        size_t first = FirstVisibleLine();

        if (line < first || line >= first + (size_t)screen.Rows())
        {
            size_t history = screen.HistorySize();
            size_t target = line > (size_t)screen.Rows() / 2 ? line - (size_t)screen.Rows() / 2 : 0;

            if (target > history)
                target = history;
            m_Tab->ScrollOffset = history - target;
            UpdateScrollBar();
        }
    }
    Refresh(true);
}

void
TermView::ClearFind()
{
    if (!m_Tab)
        return;
    m_Tab->Matches.clear();
    m_Tab->CurrentMatch = -1;
    m_Tab->SearchText.clear();
    Refresh(true);
}

void
TermView::SendText(const std::wstring &text)
{
    SendBytes(ToUtf8(text));
}

void
TermView::SendBytes(const std::string &bytes)
{
    if (!m_Tab || !m_Tab->Term || bytes.empty())
        return;
    if (m_Tab->Term->Exited())
        return;
    if (m_Tab->Select.Active)
    {
        m_Tab->Select.Active = false;
        Refresh(true);
    }
    ScrollToBottom();
    m_Tab->Term->Write(bytes);
}

bool
TermView::HandleKey(UINT msg, WPARAM wParam, LPARAM lParam)
{
    bool ctrl = KeyDown(VK_CONTROL), shift = KeyDown(VK_SHIFT);
    bool alt = msg == WM_SYSKEYDOWN || KeyDown(VK_MENU);
    bool altGr = ctrl && alt && KeyDown(VK_RMENU);
    UINT key = (UINT)wParam;
    const KeyBinding *binding;
    char sequence[32];
    int modifier;

    UNREFERENCED_PARAMETER(lParam);
    if (!m_Tab || !m_Tab->Term)
        return false;

    if (!altGr && (binding = g_Settings.FindBinding(key, ctrl, shift, alt)) != NULL)
    {
        m_Owner->PerformAction(binding->Command, binding->Argument);
        return true;
    }

    if (m_Tab->Term->Exited())
    {
        if (key == VK_RETURN || key == VK_ESCAPE)
        {
            m_Owner->PerformAction(Action::CloseTab, 0);
            return true;
        }
        return false;
    }

    if (ctrl && !shift && !alt && key == 'C' && m_Tab->Select.Active)
    {
        Copy(true);
        return true;
    }
    if (ctrl && !shift && !alt && key == 'V')
    {
        Paste();
        return true;
    }

    modifier = 1 + (shift ? 1 : 0) + (alt ? 2 : 0) + (ctrl ? 4 : 0);
    sequence[0] = 0;
    switch (key)
    {
        case VK_UP:
        case VK_DOWN:
        case VK_RIGHT:
        case VK_LEFT:
        case VK_HOME:
        case VK_END:
        {
            char final = key == VK_UP ? 'A' : key == VK_DOWN ? 'B' : key == VK_RIGHT ? 'C' :
                         key == VK_LEFT ? 'D' : key == VK_HOME ? 'H' : 'F';

            if (modifier > 1)
                StringCbPrintfA(sequence, sizeof(sequence), "\x1b[1;%d%c", modifier, final);
            else
                StringCbPrintfA(sequence, sizeof(sequence), m_Tab->Term->Screen().ApplicationCursorKeys() ? "\x1bO%c" : "\x1b[%c", final);
            break;
        }
        case VK_INSERT:
        case VK_DELETE:
        case VK_PRIOR:
        case VK_NEXT:
        {
            int number = key == VK_INSERT ? 2 : key == VK_DELETE ? 3 : key == VK_PRIOR ? 5 : 6;

            if (modifier > 1)
                StringCbPrintfA(sequence, sizeof(sequence), "\x1b[%d;%d~", number, modifier);
            else
                StringCbPrintfA(sequence, sizeof(sequence), "\x1b[%d~", number);
            break;
        }
        case VK_F1:
        case VK_F2:
        case VK_F3:
        case VK_F4:
        {
            char final = (char)('P' + (key - VK_F1));

            if (modifier > 1)
                StringCbPrintfA(sequence, sizeof(sequence), "\x1b[1;%d%c", modifier, final);
            else
                StringCbPrintfA(sequence, sizeof(sequence), "\x1bO%c", final);
            break;
        }
        case VK_F5: case VK_F6: case VK_F7: case VK_F8: case VK_F9: case VK_F10: case VK_F11: case VK_F12:
        {
            static const int Numbers[] = { 15, 17, 18, 19, 20, 21, 23, 24 };
            int number = Numbers[key - VK_F5];

            if (modifier > 1)
                StringCbPrintfA(sequence, sizeof(sequence), "\x1b[%d;%d~", number, modifier);
            else
                StringCbPrintfA(sequence, sizeof(sequence), "\x1b[%d~", number);
            break;
        }
        case VK_BACK:
            if (altGr)
                return false;
            StringCbCopyA(sequence, sizeof(sequence), ctrl ? "\x08" : alt ? "\x1b\x7f" : "\x7f");
            break;
        case VK_TAB:
            if (shift && !ctrl)
            {
                StringCbCopyA(sequence, sizeof(sequence), "\x1b[Z");
                break;
            }
            return false;
        case VK_SPACE:
            if (ctrl && !altGr)
            {
                SendBytes(std::string(1, '\0'));
                return true;
            }
            return false;
        default:
            return false;
    }
    if (sequence[0])
    {
        SendBytes(sequence);
        return true;
    }
    return false;
}

void
TermView::ShowContextMenu(POINT screen)
{
    HMENU menu = CreatePopupMenu();
    UINT command;

    AppendMenuW(menu, MF_STRING | (m_Tab && m_Tab->Select.Active ? 0 : MF_GRAYED), IDM_COPY, LoadStr(IDS_COPY).c_str());
    AppendMenuW(menu, MF_STRING, IDM_PASTE, LoadStr(IDS_PASTE).c_str());
    AppendMenuW(menu, MF_STRING, IDM_SELECT_ALL, LoadStr(IDS_SELECT_ALL).c_str());
    AppendMenuW(menu, MF_SEPARATOR, 0, NULL);
    AppendMenuW(menu, MF_STRING, IDM_FIND, LoadStr(IDS_FIND).c_str());
    AppendMenuW(menu, MF_STRING, IDM_CLEAR_BUFFER, LoadStr(IDS_CLEAR_BUFFER).c_str());
    command = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, screen.x, screen.y, 0, m_hwnd, NULL);
    DestroyMenu(menu);
    switch (command)
    {
        case IDM_COPY: Copy(true); break;
        case IDM_PASTE: Paste(); break;
        case IDM_SELECT_ALL: SelectAll(); break;
        case IDM_FIND: m_Owner->PerformAction(Action::Find, 0); break;
        case IDM_CLEAR_BUFFER: m_Owner->PerformAction(Action::ClearBuffer, 0); break;
    }
}

LRESULT
TermView::HandleMessage(UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
        case WM_ERASEBKGND:
            return 1;

        case WM_PAINT:
        {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(m_hwnd, &ps);
            Paint(hdc, ps.rcPaint);
            EndPaint(m_hwnd, &ps);
            return 0;
        }

        case WM_SETFOCUS:
            m_Focused = true;
            m_CursorOn = true;
            SetTimer(m_hwnd, TIMER_CURSOR, GetCaretBlinkTime(), NULL);
            Refresh(false);
            return 0;

        case WM_KILLFOCUS:
            m_Focused = false;
            KillTimer(m_hwnd, TIMER_CURSOR);
            Refresh(false);
            return 0;

        case WM_TIMER:
            if (wParam == TIMER_CURSOR)
            {
                bool blink = m_Tab && m_Tab->Term && m_Tab->Term->Settings().CursorBlink &&
                             m_Tab->Term->Screen().CursorBlinking();
                m_CursorOn = blink ? !m_CursorOn : true;
                Refresh(false);
            }
            else if (wParam == TIMER_AUTOSCROLL && m_Selecting)
            {
                POINT pt;
                RECT client;

                GetCursorPos(&pt);
                ScreenToClient(m_hwnd, &pt);
                GetClientRect(m_hwnd, &client);
                if (pt.y < 0)
                    ScrollBy(-1);
                else if (pt.y >= client.bottom)
                    ScrollBy(1);
                if (m_Tab && m_Tab->Term)
                {
                    m_Tab->Select.Caret = HitTest(pt.x, pt.y, true);
                    Refresh(true);
                }
            }
            return 0;

        case WM_CHAR:
        case WM_SYSCHAR:
        {
            wchar_t ch = (wchar_t)wParam;
            std::wstring text;

            if (!m_Tab || !m_Tab->Term)
                return 0;
            if (msg == WM_SYSCHAR && ch == L' ')
                break;
            if (ch >= 0xD800 && ch <= 0xDBFF)
            {
                m_HighSurrogate = ch;
                return 0;
            }
            if (ch >= 0xDC00 && ch <= 0xDFFF && m_HighSurrogate)
            {
                text += m_HighSurrogate;
                m_HighSurrogate = 0;
            }
            text += ch;
            if (m_Tab->Term->Exited())
                return 0;
            if (msg == WM_SYSCHAR)
                text.insert(text.begin(), L'\x1b');
            SendText(text);
            return 0;
        }

        case WM_SYSKEYUP:
            if (wParam == VK_MENU || wParam == VK_F10)
                return 0;
            break;

        case WM_LBUTTONDOWN:
        case WM_LBUTTONDBLCLK:
        {
            POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            DWORD now = GetMessageTime();
            TextPosition position;

            SetFocus(m_hwnd);
            if (!m_Tab || !m_Tab->Term)
                return 0;
            if (msg == WM_LBUTTONDBLCLK)
                m_ClickCount = 2;
            else if (m_ClickCount == 2 && now - m_LastClick <= GetDoubleClickTime() &&
                     labs(pt.x - m_LastClickPoint.x) < GetSystemMetrics(SM_CXDOUBLECLK) &&
                     labs(pt.y - m_LastClickPoint.y) < GetSystemMetrics(SM_CYDOUBLECLK))
                m_ClickCount = 3;
            else
                m_ClickCount = 1;
            m_LastClick = now;
            m_LastClickPoint = pt;

            position = HitTest(pt.x, pt.y, true);
            if (m_ClickCount == 1 && (wParam & MK_SHIFT) && m_Tab->Select.Active)
            {
                m_Tab->Select.Caret = position;
            }
            else if (m_ClickCount == 1)
            {
                m_Tab->Select.Active = false;
                m_Tab->Select.Block = KeyDown(VK_MENU);
                m_Tab->Select.Anchor = position;
                m_Tab->Select.Caret = position;
            }
            else if (m_ClickCount == 2)
            {
                TextPosition start = position, end = position;
                ExtendWord(start, end);
                m_Tab->Select.Active = true;
                m_Tab->Select.Block = false;
                m_Tab->Select.Anchor = start;
                m_Tab->Select.Caret = end;
            }
            else
            {
                m_Tab->Select.Active = true;
                m_Tab->Select.Block = false;
                m_Tab->Select.Anchor.Line = position.Line;
                m_Tab->Select.Anchor.Col = 0;
                m_Tab->Select.Caret.Line = position.Line;
                m_Tab->Select.Caret.Col = m_Tab->Term->Screen().Cols() - 1;
            }
            m_Selecting = true;
            SetCapture(m_hwnd);
            SetTimer(m_hwnd, TIMER_AUTOSCROLL, 50, NULL);
            Refresh(true);
            return 0;
        }

        case WM_MOUSEMOVE:
            if (m_Selecting && m_Tab && m_Tab->Term && (wParam & MK_LBUTTON))
            {
                TextPosition position = HitTest(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam), true);

                if (!(position == m_Tab->Select.Caret) || !m_Tab->Select.Active)
                {
                    m_Tab->Select.Caret = position;
                    m_Tab->Select.Active = !(m_Tab->Select.Caret == m_Tab->Select.Anchor) || m_ClickCount > 1;
                    Refresh(true);
                }
            }
            return 0;

        case WM_LBUTTONUP:
            if (m_Selecting)
            {
                m_Selecting = false;
                KillTimer(m_hwnd, TIMER_AUTOSCROLL);
                ReleaseCapture();
                if (m_Tab && m_Tab->Select.Active && g_Settings.CopyOnSelect)
                    Copy(false);
            }
            return 0;

        case WM_RBUTTONUP:
        {
            POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };

            if (!m_Tab || !m_Tab->Term)
                return 0;
            if (wParam & MK_SHIFT)
            {
                ClientToScreen(m_hwnd, &pt);
                ShowContextMenu(pt);
            }
            else if (m_Tab->Select.Active)
            {
                Copy(true);
            }
            else
            {
                Paste();
            }
            return 0;
        }

        case WM_CONTEXTMENU:
            if (lParam == -1)
            {
                POINT pt = { 0, 0 };
                ClientToScreen(m_hwnd, &pt);
                ShowContextMenu(pt);
            }
            return 0;

        case WM_MOUSEWHEEL:
        {
            int delta = GET_WHEEL_DELTA_WPARAM(wParam);

            if (GET_KEYSTATE_WPARAM(wParam) & MK_CONTROL)
            {
                m_Owner->PerformAction(delta > 0 ? Action::ZoomIn : Action::ZoomOut, 0);
                return 0;
            }
            if (m_Tab && m_Tab->Term && m_Tab->Term->Screen().AlternateScreen())
            {
                int notches = abs(delta) / WHEEL_DELTA;
                const char *arrow = delta > 0 ? (m_Tab->Term->Screen().ApplicationCursorKeys() ? "\x1bOA" : "\x1b[A")
                                              : (m_Tab->Term->Screen().ApplicationCursorKeys() ? "\x1bOB" : "\x1b[B");
                for (int i = 0; i < notches * 3; i++)
                    m_Tab->Term->Write(arrow);
                return 0;
            }
            ScrollBy(-(long)(delta * 3 / WHEEL_DELTA));
            return 0;
        }

        case WM_VSCROLL:
        {
            SCROLLINFO info = { sizeof(info), SIF_ALL };
            long position;

            if (!m_Tab || !m_Tab->Term)
                return 0;
            GetScrollInfo(m_hwnd, SB_VERT, &info);
            position = info.nPos;
            switch (LOWORD(wParam))
            {
                case SB_LINEUP: position -= 1; break;
                case SB_LINEDOWN: position += 1; break;
                case SB_PAGEUP: position -= (long)info.nPage; break;
                case SB_PAGEDOWN: position += (long)info.nPage; break;
                case SB_THUMBTRACK:
                case SB_THUMBPOSITION: position = info.nTrackPos; break;
                case SB_TOP: position = 0; break;
                case SB_BOTTOM: position = info.nMax; break;
            }
            ScrollBy(position - (long)FirstVisibleLine());
            return 0;
        }

        case WM_GETDLGCODE:
            return DLGC_WANTALLKEYS | DLGC_WANTCHARS | DLGC_WANTARROWS | DLGC_WANTTAB;
    }
    return DefWindowProcW(m_hwnd, msg, wParam, lParam);
}
