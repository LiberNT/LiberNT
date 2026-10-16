/*
 * PROJECT:     LiberNT Shell Frame
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     Double-buffered GDI and GDI+ drawing surface
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "shellframe.h"

namespace ShellFrame
{

static Gdiplus::Color
ToColor(COLORREF color)
{
    return Gdiplus::Color(255, GetRValue(color), GetGValue(color), GetBValue(color));
}

static void
AddRoundRect(Gdiplus::GraphicsPath &path, const Gdiplus::RectF &rc, float radius)
{
    float d = radius * 2.0f;

    if (radius <= 0.0f)
    {
        path.AddRectangle(rc);
        return;
    }
    path.AddArc(rc.X, rc.Y, d, d, 180.0f, 90.0f);
    path.AddArc(rc.X + rc.Width - d, rc.Y, d, d, 270.0f, 90.0f);
    path.AddArc(rc.X + rc.Width - d, rc.Y + rc.Height - d, d, d, 0.0f, 90.0f);
    path.AddArc(rc.X, rc.Y + rc.Height - d, d, d, 90.0f, 90.0f);
    path.CloseFigure();
}

Canvas::Canvas(HDC target, const RECT &bounds) :
    m_Target(target),
    m_Bounds(bounds),
    m_Dc(NULL),
    m_Bitmap(NULL),
    m_OldBitmap(NULL),
    m_Graphics(NULL)
{
    BITMAPINFO info = { };
    void *bits = NULL;
    int width = max(1, (int)(bounds.right - bounds.left));
    int height = max(1, (int)(bounds.bottom - bounds.top));

    info.bmiHeader.biSize = sizeof(info.bmiHeader);
    info.bmiHeader.biWidth = width;
    info.bmiHeader.biHeight = -height;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    m_Dc = CreateCompatibleDC(target);
    m_Bitmap = CreateDIBSection(target, &info, DIB_RGB_COLORS, &bits, NULL, 0);
    m_OldBitmap = SelectObject(m_Dc, m_Bitmap);
    SetBkMode(m_Dc, TRANSPARENT);
    m_Graphics = new Gdiplus::Graphics(m_Dc);
    m_Graphics->SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    m_Graphics->SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
}

Canvas::~Canvas()
{
    delete m_Graphics;
    BitBlt(m_Target, m_Bounds.left, m_Bounds.top, m_Bounds.right - m_Bounds.left, m_Bounds.bottom - m_Bounds.top,
           m_Dc, 0, 0, SRCCOPY);
    SelectObject(m_Dc, m_OldBitmap);
    DeleteObject(m_Bitmap);
    DeleteDC(m_Dc);
}

void
Canvas::Fill(const RECT &rc, COLORREF color)
{
    HBRUSH brush = CreateSolidBrush(color);
    m_Graphics->Flush(Gdiplus::FlushIntentionSync);
    FillRect(m_Dc, &rc, brush);
    DeleteObject(brush);
}

void
Canvas::FillRound(const RECT &rc, float radius, COLORREF color)
{
    Gdiplus::GraphicsPath path(Gdiplus::FillModeAlternate);
    Gdiplus::SolidBrush brush(ToColor(color));

    AddRoundRect(path, Gdiplus::RectF((float)rc.left, (float)rc.top, (float)(rc.right - rc.left),
                                      (float)(rc.bottom - rc.top)), radius);
    m_Graphics->FillPath(&brush, &path);
}

void
Canvas::FrameRound(const RECT &rc, float radius, COLORREF color, float width)
{
    Gdiplus::GraphicsPath path(Gdiplus::FillModeAlternate);
    Gdiplus::Pen pen(ToColor(color), width);
    float inset = width / 2.0f;

    AddRoundRect(path, Gdiplus::RectF(rc.left + inset, rc.top + inset, rc.right - rc.left - width,
                                      rc.bottom - rc.top - width), radius - inset);
    m_Graphics->DrawPath(&pen, &path);
}

void
Canvas::FillEllipse(const RECT &rc, COLORREF color)
{
    Gdiplus::SolidBrush brush(ToColor(color));
    m_Graphics->FillEllipse(&brush, (float)rc.left, (float)rc.top, (float)(rc.right - rc.left),
                            (float)(rc.bottom - rc.top));
}

void
Canvas::Line(int x1, int y1, int x2, int y2, COLORREF color)
{
    RECT rc;

    rc.left = min(x1, x2);
    rc.top = min(y1, y2);
    rc.right = max(x1, x2) + (x1 == x2 ? 1 : 0);
    rc.bottom = max(y1, y2) + (y1 == y2 ? 1 : 0);
    Fill(rc, color);
}

void
Canvas::Text(LPCWSTR text, const RECT &rc, HFONT font, COLORREF color, UINT format)
{
    RECT box = rc;
    HGDIOBJ old;

    m_Graphics->Flush(Gdiplus::FlushIntentionSync);
    old = SelectObject(m_Dc, font);
    SetTextColor(m_Dc, color);
    DrawTextW(m_Dc, text, -1, &box, format | DT_NOPREFIX);
    SelectObject(m_Dc, old);
}

SIZE
Canvas::MeasureText(LPCWSTR text, HFONT font)
{
    SIZE size = { 0, 0 };
    HGDIOBJ old = SelectObject(m_Dc, font);

    GetTextExtentPoint32W(m_Dc, text, lstrlenW(text), &size);
    SelectObject(m_Dc, old);
    return size;
}

void
Canvas::Icon(LPCWSTR name, const RECT &rc, COLORREF color)
{
    FillIcon(*m_Graphics, name, Gdiplus::RectF((float)rc.left, (float)rc.top, (float)(rc.right - rc.left),
                                                (float)(rc.bottom - rc.top)), color);
}

}
