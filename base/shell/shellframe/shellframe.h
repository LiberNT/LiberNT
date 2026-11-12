/*
 * PROJECT:     LiberNT Shell Frame
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     Window frame, theme and drawing shared by Explorer and Settings
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

#include <windows.h>
#include <windowsx.h>
#include <dwmapi.h>
#include <commctrl.h>
#include <gdiplus.h>

#ifndef SHELLFRAME_CUSTOM_CAPTION_BUTTONS
#define SHELLFRAME_CUSTOM_CAPTION_BUTTONS 0
#endif

namespace ShellFrame
{

enum class TextStyle
{
    Caption,
    Body,
    BodyStrong,
    Subtitle,
    Title,
    Count
};

struct Palette
{
    COLORREF Window;
    COLORREF Text;
    COLORREF TextSecondary;
    COLORREF TextTertiary;
    COLORREF TextDisabled;
    COLORREF SubtleHover;
    COLORREF SubtlePressed;
    COLORREF Selected;
    COLORREF Card;
    COLORREF CardHover;
    COLORREF CardBorder;
    COLORREF Divider;
    COLORREF ControlFill;
    COLORREF ControlFillFocus;
    COLORREF ControlStroke;
    COLORREF ControlStrongStroke;
    COLORREF Accent;
    COLORREF AccentText;
    COLORREF Glyph;
    COLORREF CloseHover;
    COLORREF CloseText;
    COLORREF TabStrip;
    COLORREF TabSelected;
    COLORREF ToolbarBand;
    COLORREF Content;
};

class Theme
{
public:
    Theme();
    ~Theme();

    void Update(HWND hwnd);
    void SetDpi(UINT dpi);
    void SetFollowSystemColors(BOOL follow) { m_FollowSystem = follow; }
    void SetDarkPreference(int preference) { m_DarkPreference = preference; }
    BOOL UpdateMaterial(HWND hwnd);

    BOOL IsDark() const { return m_HighContrast ? m_HighContrastDark : m_Material ? m_MaterialDark : m_Dark; }
    BOOL HasMaterial() const { return m_Material && !m_HighContrast; }
    BOOL IsHighContrast() const { return m_HighContrast; }
    UINT Dpi() const { return m_Dpi; }
    int Scale(int value) const { return MulDiv(value, (int)m_Dpi, 96); }
    float ScaleF(float value) const { return value * (float)m_Dpi / 96.0f; }
    const Palette &Colors() const { return m_Colors; }
    HFONT Font(TextStyle style) const { return m_Fonts[(int)style]; }

private:
    void CreateFonts();
    void DestroyFonts();
    void BuildPalette();

    BOOL m_Dark;
    BOOL m_FollowSystem;
    int m_DarkPreference;
    BOOL m_HighContrast;
    BOOL m_HighContrastDark;
    BOOL m_Material;
    BOOL m_MaterialDark;
    COLORREF m_MaterialKey;
    UINT m_Dpi;
    Palette m_Colors;
    HFONT m_Fonts[(int)TextStyle::Count];
};

class Canvas
{
public:
    Canvas(HDC target, const RECT &bounds);
    ~Canvas();

    HDC Dc() const { return m_Dc; }
    Gdiplus::Graphics &Graphics() { return *m_Graphics; }

    void Fill(const RECT &rc, COLORREF color);
    void FillRound(const RECT &rc, float radius, COLORREF color);
    void FrameRound(const RECT &rc, float radius, COLORREF color, float width = 1.0f);
    void FillEllipse(const RECT &rc, COLORREF color);
    void Line(int x1, int y1, int x2, int y2, COLORREF color);
    void Text(LPCWSTR text, const RECT &rc, HFONT font, COLORREF color, UINT format);
    SIZE MeasureText(LPCWSTR text, HFONT font);
    void Icon(LPCWSTR name, const RECT &rc, COLORREF color);

private:
    HDC m_Target;
    RECT m_Bounds;
    HDC m_Dc;
    HBITMAP m_Bitmap;
    HGDIOBJ m_OldBitmap;
    Gdiplus::Graphics *m_Graphics;
};

BOOL Startup();
void Shutdown();
Gdiplus::GraphicsPath *IconPath(LPCWSTR name, float *viewBox);
HICON CreateGlyphIcon(LPCWSTR name, int size, COLORREF color);
void FillRoundSmooth(HDC hdc, const RECT &rc, float radius, COLORREF color);
void FillRoundGlass(HDC hdc, const RECT &rc, float radius, COLORREF color, BYTE opacity);
void FillIcon(Gdiplus::Graphics &graphics, LPCWSTR name, const Gdiplus::RectF &rc, COLORREF color);

class NavigationButtons
{
public:
    NavigationButtons();
    ~NavigationButtons();

    BOOL Create(HWND parent, const Theme &theme, UINT backId, UINT forwardId, LPCWSTR backTip, LPCWSTR forwardTip,
                COLORREF glyph, COLORREF disabled);
    void Destroy();
    void SetColors(COLORREF glyph, COLORREF disabled);
    void Place(int left, int centerY);
    void Enable(BOOL back, BOOL forward);
    BOOL OnNotify(const NMHDR *header, LRESULT *result, COLORREF background);
    HWND Window() const { return m_hwnd; }

private:
    HWND m_hwnd;
    HIMAGELIST m_Images;
    HIMAGELIST m_Disabled;
    UINT m_BackId;
    UINT m_ForwardId;
    int m_Button;
    int m_Glyph;
    int m_PadX;
    int m_PadY;
    float m_Radius;
};

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

enum class CaptionButton
{
    None,
    Minimize,
    Maximize,
    Close
};

class FrameWindow
{
public:
    FrameWindow();
    virtual ~FrameWindow();

    HWND Create(LPCWSTR className, LPCWSTR title, HICON icon, int width, int height, int showCmd);
    HWND Window() const { return m_hwnd; }
    Theme &CurrentTheme() { return m_Theme; }

protected:
    virtual LRESULT HandleMessage(UINT msg, WPARAM wParam, LPARAM lParam);
    virtual void OnCreate() {}
    virtual void OnLayout(const RECT &client) { UNREFERENCED_PARAMETER(client); }
    virtual void OnPaint(Canvas &canvas, const RECT &client) = 0;
    virtual void OnThemeChanged() {}
    virtual BOOL IsCaptionClient(POINT pt) { UNREFERENCED_PARAMETER(pt); return FALSE; }
    virtual int CaptionHeight() const = 0;
    virtual int GlassWidth() const { return 0; }
    virtual int GlassTop() const { return CaptionHeight(); }
    virtual int CaptionButtonHeight() const { return m_Theme.Scale(32); }
    virtual BOOL UsesClientCaption() const { return TRUE; }

    LRESULT DefaultProc(UINT msg, WPARAM wParam, LPARAM lParam);
    void Invalidate(const RECT *rc = NULL);
    const RECT &PaintRect() const { return m_PaintRect; }
    void PaintCaptionButtons(Canvas &canvas, const RECT &client, COLORREF background);
    RECT CaptionButtonRect(CaptionButton button) const;
    int CaptionButtonsWidth() const;
    BOOL ClientCaption() const;
    BOOL IsActive() const { return m_Active; }
    COLORREF Glass(GlassTone tone) const;
    COLORREF GlassTint(BYTE opacity) const;
    HFONT GlassFont(TextStyle style) const;
    void UpdateGlassFonts();

    HWND m_hwnd;
    Theme m_Theme;

private:
    static LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    LRESULT HitTest(POINT screen);
    CaptionButton ButtonFromHit(LRESULT hit) const;
    void SetHotButton(CaptionButton button);
    void ApplyFrameAttributes();
#if !SHELLFRAME_CUSTOM_CAPTION_BUTTONS
    void ClearCaptionButtons(Canvas &canvas);
#endif

    CaptionButton m_HotButton;
    CaptionButton m_PressedButton;
    BOOL m_Active;
    BOOL m_TrackingLeave;
    int m_GlassWidth;
    int m_GlassTop;
    RECT m_PaintRect;
    HFONT m_GlassFonts[(int)TextStyle::Count];
};

struct BandPalette
{
    COLORREF Band;
    COLORREF BandShade;
    COLORREF BandLine;
    COLORREF BandBottom;
    COLORREF Box;
    COLORREF BoxLine;
    COLORREF Command;
    COLORREF CommandBottom;
    COLORREF TabBorder;
    COLORREF Text;
    COLORREF Glyph;
    COLORREF GlyphDisabled;
    COLORREF Window;
    COLORREF Body;
    COLORREF PanelLine;
};

void GetBandPalette(BandPalette *palette);

struct TabBandItem
{
    LPCWSTR Title;
    int IconIndex;
    BOOL Attention;
};

class TabBandHost
{
public:
    virtual ~TabBandHost() {}
    virtual void OnTabSelect(int index) = 0;
    virtual void OnTabClose(int index) = 0;
    virtual void OnTabMove(int from, int to) = 0;
    virtual void OnTabMenu(int index, POINT screen) = 0;
    virtual void OnNewTab() = 0;
    virtual void OnNewTabMenu(POINT screen) { UNREFERENCED_PARAMETER(screen); }
};

class TabBand
{
public:
    TabBand();
    ~TabBand();

    BOOL Create(HWND parent, TabBandHost *host, HFONT font, LPCWSTR className);
    HWND Window() const { return m_hwnd; }
    void SetTabs(const TabBandItem *tabs, int count, int active);
    void SetShadeBottom(int bottom);
    void SetOpaque(COLORREF background, COLORREF activeFill);
    void SetMenuButton(BOOL show);
    void SetFont(HFONT font);

private:
    struct Tab
    {
        WCHAR Title[MAX_PATH];
        int IconIndex;
        BOOL Attention;
    };

    static LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    LRESULT HandleMessage(UINT msg, WPARAM wParam, LPARAM lParam);
    int Scale(int value) const;
    int TabWidth() const;
    BOOL InTab(int index, POINT pt) const;
    BOOL ShowsClose(int index) const;
    RECT TabRect(int index) const;
    RECT CloseRect(int index) const;
    RECT NewRect() const;
    RECT MenuRect() const;
    int HitTab(POINT pt, BOOL *close) const;
    void Paint(HDC dc);
    void SetHot(int tab, BOOL close, BOOL add, BOOL menu);

    HWND m_hwnd;
    TabBandHost *m_Host;
    Tab m_Tabs[32];
    int m_Count;
    int m_Active;
    int m_HotTab;
    BOOL m_HotClose;
    BOOL m_HotNew;
    BOOL m_HotMenu;
    BOOL m_Tracking;
    int m_DragTab;
    int m_PressedTab;
    BOOL m_PressedClose;
    BOOL m_PressedNew;
    BOOL m_PressedMenu;
    BOOL m_ShowMenu;
    HFONT m_Font;
    int m_ShadeBottom;
    COLORREF m_Background;
    COLORREF m_ActiveFill;
};

}
