/*
 * PROJECT:     LiberNT Shell Frame
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     Light and dark palettes, accent color, DPI and fonts
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "shellframe.h"

namespace ShellFrame
{

static COLORREF
Blend(BYTE alpha, COLORREF fore, COLORREF back)
{
    return RGB((GetRValue(fore) * alpha + GetRValue(back) * (255 - alpha) + 127) / 255,
               (GetGValue(fore) * alpha + GetGValue(back) * (255 - alpha) + 127) / 255,
               (GetBValue(fore) * alpha + GetBValue(back) * (255 - alpha) + 127) / 255);
}

static BOOL
ReadDword(HKEY root, LPCWSTR key, LPCWSTR value, DWORD *data)
{
    DWORD size = sizeof(*data);
    return RegGetValueW(root, key, value, RRF_RT_REG_DWORD, NULL, data, &size) == ERROR_SUCCESS;
}

static COLORREF
AccentFromPalette(BOOL dark)
{
    BYTE palette[32];
    DWORD size = sizeof(palette);

    if (RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Accent",
                     L"AccentPalette", RRF_RT_REG_BINARY, NULL, palette, &size) == ERROR_SUCCESS &&
        size >= sizeof(palette))
    {
        const BYTE *entry = &palette[(dark ? 1 : 4) * 4];
        return RGB(entry[0], entry[1], entry[2]);
    }
    return dark ? RGB(0x4C, 0xC2, 0xFF) : RGB(0x00, 0x67, 0xC0);
}

Theme::Theme() :
    m_Dark(FALSE),
    m_Dpi(96)
{
    ZeroMemory(&m_Colors, sizeof(m_Colors));
    ZeroMemory(m_Fonts, sizeof(m_Fonts));
}

Theme::~Theme()
{
    DestroyFonts();
}

void
Theme::Update(HWND hwnd)
{
    DWORD light = 1;
    COLORREF window, white = RGB(255, 255, 255), black = RGB(0, 0, 0);

    ReadDword(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
              L"AppsUseLightTheme", &light);
    m_Dark = !light;

    Palette &c = m_Colors;
    if (m_Dark)
    {
        window = RGB(32, 32, 32);
        c.Window = window;
        c.Text = white;
        c.TextSecondary = Blend(0xC5, white, window);
        c.TextTertiary = Blend(0x8B, white, window);
        c.TextDisabled = Blend(0x5D, white, window);
        c.SubtleHover = Blend(0x0F, white, window);
        c.SubtlePressed = Blend(0x0A, white, window);
        c.Selected = Blend(0x0F, white, window);
        c.Card = Blend(0x0D, white, window);
        c.CardHover = Blend(0x15, white, window);
        c.CardBorder = Blend(0x19, black, window);
        c.Divider = Blend(0x15, white, window);
        c.ControlFill = Blend(0x0F, white, window);
        c.ControlFillFocus = RGB(0x1F, 0x1F, 0x1F);
        c.ControlStroke = Blend(0x12, white, window);
        c.ControlStrongStroke = Blend(0x8B, white, window);
        c.Glyph = white;
        c.TabStrip = RGB(32, 32, 32);
        c.TabSelected = RGB(43, 43, 43);
        c.ToolbarBand = RGB(43, 43, 43);
        c.Content = RGB(25, 25, 25);
    }
    else
    {
        window = RGB(243, 243, 243);
        c.Window = window;
        c.Text = Blend(0xE4, black, window);
        c.TextSecondary = Blend(0x9E, black, window);
        c.TextTertiary = Blend(0x72, black, window);
        c.TextDisabled = Blend(0x5C, black, window);
        c.SubtleHover = Blend(0x09, black, window);
        c.SubtlePressed = Blend(0x06, black, window);
        c.Selected = Blend(0x09, black, window);
        c.Card = Blend(0xB3, white, window);
        c.CardHover = Blend(0x80, RGB(249, 249, 249), window);
        c.CardBorder = Blend(0x0F, black, window);
        c.Divider = Blend(0x14, black, window);
        c.ControlFill = Blend(0xB3, white, window);
        c.ControlFillFocus = white;
        c.ControlStroke = Blend(0x0F, black, window);
        c.ControlStrongStroke = Blend(0x72, black, window);
        c.Glyph = Blend(0xE4, black, window);
        c.TabStrip = RGB(232, 232, 232);
        c.TabSelected = RGB(248, 248, 248);
        c.ToolbarBand = RGB(248, 248, 248);
        c.Content = white;
    }
    c.Accent = AccentFromPalette(m_Dark);
    c.AccentText = m_Dark ? black : white;
    c.CloseHover = RGB(0xC4, 0x2B, 0x1C);
    c.CloseText = white;

    if (hwnd)
    {
        BOOL dark = m_Dark;
        DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark));
        SetDpi(GetDpiForWindow(hwnd));
    }
    else
    {
        SetDpi(m_Dpi);
    }
}

void
Theme::SetDpi(UINT dpi)
{
    m_Dpi = dpi ? dpi : 96;
    DestroyFonts();
    CreateFonts();
}

void
Theme::CreateFonts()
{
    static const struct { int Height; int Weight; LPCWSTR Face; } Styles[] =
    {
        { 12, FW_NORMAL, L"Segoe UI Variable Text" },
        { 14, FW_NORMAL, L"Segoe UI Variable Text" },
        { 14, FW_SEMIBOLD, L"Segoe UI Semibold" },
        { 20, FW_SEMIBOLD, L"Segoe UI Semibold" },
        { 28, FW_SEMIBOLD, L"Segoe UI Semibold" },
    };

    for (int i = 0; i < (int)TextStyle::Count; i++)
    {
        m_Fonts[i] = CreateFontW(-Scale(Styles[i].Height), 0, 0, 0, Styles[i].Weight, FALSE, FALSE, FALSE,
                                 DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                 DEFAULT_PITCH | FF_SWISS, Styles[i].Face);
    }
}

void
Theme::DestroyFonts()
{
    for (int i = 0; i < (int)TextStyle::Count; i++)
    {
        if (m_Fonts[i])
            DeleteObject(m_Fonts[i]);
        m_Fonts[i] = NULL;
    }
}

}
