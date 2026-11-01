/*
 * PROJECT:     LiberNT Terminal
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Shared declarations of the terminal window, its tabs and its view
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <commdlg.h>
#include <shellapi.h>
#include <shlwapi.h>
#include <shlobj.h>
#include <strsafe.h>
#include <dwmapi.h>
#include <uxtheme.h>
#include <gdiplus.h>
#include <shellframe.h>

#include <memory>
#include <string>
#include <vector>

#include "resource.h"
#include "settings.h"
#include "session.h"
#include "vt.h"

template <class T>
class OwnedPtr
{
public:
    OwnedPtr() : m_Ptr(NULL) {}
    ~OwnedPtr() { delete m_Ptr; }
    OwnedPtr(const OwnedPtr &) = delete;
    OwnedPtr &operator=(const OwnedPtr &) = delete;
    T *get() const { return m_Ptr; }
    void reset(T *ptr = NULL) { T *old = m_Ptr; m_Ptr = ptr; delete old; }
    T *operator->() const { return m_Ptr; }
    explicit operator bool() const { return m_Ptr != NULL; }

private:
    T *m_Ptr;
};

extern HINSTANCE g_Instance;
extern Settings g_Settings;

std::wstring LoadStr(UINT id);
std::wstring ToWide(const std::basic_string<char32_t> &text);
std::basic_string<char32_t> FromWide(const std::wstring &text);
void BroadcastSettingsChanged();
void ShowSettings(HWND owner);
bool SettingsPreTranslate(MSG *msg);
void ShowAbout(HWND owner);
bool PromptText(HWND owner, const std::wstring &title, std::wstring &text);

struct TextPosition
{
    size_t Line = 0;
    int Col = 0;

    bool operator<(const TextPosition &other) const
    {
        return Line < other.Line || (Line == other.Line && Col < other.Col);
    }
    bool operator==(const TextPosition &other) const { return Line == other.Line && Col == other.Col; }
};

struct Selection
{
    bool Active = false;
    bool Block = false;
    TextPosition Anchor;
    TextPosition Caret;

    void Normalized(TextPosition &start, TextPosition &end) const
    {
        start = Caret < Anchor ? Caret : Anchor;
        end = Caret < Anchor ? Anchor : Caret;
    }
};

struct SearchMatch
{
    size_t Line;
    int Col;
    int Length;
};

struct Tab
{
    OwnedPtr<Session> Term;
    std::wstring CustomTitle;
    bool Bell = false;
    bool Activity = false;
    size_t ScrollOffset = 0;
    Selection Select;
    std::wstring SearchText;
    std::vector<SearchMatch> Matches;
    int CurrentMatch = -1;
    std::wstring ErrorText;
    int IconIndex = -1;

    std::wstring Title() const;
};

class TerminalWindow;

class TermView
{
public:
    explicit TermView(TerminalWindow *owner);
    ~TermView();

    HWND Create(HWND parent);
    HWND Window() const { return m_hwnd; }
    void SetTab(Tab *tab);
    void ApplyFont(UINT dpi);
    void GridForSize(int width, int height, int *cols, int *rows) const;
    void GridFor(const Profile &profile, int width, int height, int *cols, int *rows) const;
    SIZE CellSize() const { return m_Cell; }
    int Zoom() const { return m_Zoom; }
    void SetZoom(int zoom);
    void Refresh(bool all);
    void OnOutput(size_t scrolled);
    bool HandleKey(UINT msg, WPARAM wParam, LPARAM lParam);
    void Copy(bool clearSelection);
    void Paste();
    void SelectAll();
    void ScrollBy(long lines);
    void ScrollToBottom();
    void ScrollToTop();
    void Find(const std::wstring &text, bool up, bool restart);
    void ClearFind();
    std::wstring SelectedText() const;

private:
    static LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    LRESULT HandleMessage(UINT msg, WPARAM wParam, LPARAM lParam);
    void Paint(HDC hdc, const RECT &update);
    void DrawRow(HDC hdc, int row, const RECT &client);
    void DrawCursor(HDC hdc);
    void UpdateScrollBar();
    void ReleaseFonts();
    void SendText(const std::wstring &text);
    void SendBytes(const std::string &bytes);
    TextPosition HitTest(int x, int y, bool clampToGrid) const;
    void ExtendWord(TextPosition &start, TextPosition &end) const;
    bool IsSelected(size_t line, int col) const;
    bool IsMatch(size_t line, int col, bool *current) const;
    COLORREF ResolveColor(uint32_t color, bool foreground, bool bold) const;
    size_t FirstVisibleLine() const;
    void ShowContextMenu(POINT screen);

    TerminalWindow *m_Owner;
    HWND m_hwnd = NULL;
    Tab *m_Tab = NULL;
    HFONT m_Fonts[4] = {};
    SIZE m_Cell = { 8, 16 };
    int m_Ascent = 12;
    int m_Zoom = 0;
    UINT m_Dpi = 96;
    bool m_CursorOn = true;
    bool m_Focused = false;
    bool m_Selecting = false;
    int m_ClickCount = 0;
    DWORD m_LastClick = 0;
    POINT m_LastClickPoint = {};
    WCHAR m_HighSurrogate = 0;
    HBITMAP m_Buffer = NULL;
    SIZE m_BufferSize = {};
};

class TerminalWindow : public ShellFrame::FrameWindow, public ShellFrame::TabBandHost
{
public:
    TerminalWindow();
    ~TerminalWindow();

    bool Open(int showCmd, const std::wstring &profile, const std::wstring &commandLine, const std::wstring &directory);
    bool PreTranslate(MSG *msg);
    Tab *ActiveTab() { return m_Active >= 0 && m_Active < (int)m_Tabs.size() ? m_Tabs[m_Active] : NULL; }
    void PerformAction(Action command, int argument);
    void ApplySettings();
    void UpdateWindowTitle();
    void InvalidateTabs();
    void ShowFindBar(bool show);
    COLORREF SchemeBackground() const;

protected:
    LRESULT HandleMessage(UINT msg, WPARAM wParam, LPARAM lParam) override;
    void OnCreate() override;
    void OnLayout(const RECT &client) override;
    void OnPaint(ShellFrame::Canvas &canvas, const RECT &client) override;
    void OnThemeChanged() override;
    int CaptionHeight() const override;
    void OnTabSelect(int index) override;
    void OnTabClose(int index) override;
    void OnTabMove(int from, int to) override;
    void OnTabMenu(int index, POINT screen) override;
    void OnNewTab() override;
    void OnNewTabMenu(POINT screen) override;

private:
    bool NewTab(const Profile &profile, const std::wstring &commandLine, const std::wstring &directory);
    void CloseTab(int index);
    void ActivateTab(int index);
    void LayoutTabs();
    void UpdateTabFont();
    int StripHeight() const;
    void ResizeSessions();
    void ShowProfileMenu(POINT screen);
    void ShowTabMenu(int index, POINT screen);
    void ToggleFullscreen();
    Tab *TabFromSession(UINT id, int *index);
    void OnSessionOutput(UINT id);
    void OnSessionExited(UINT id, DWORD code);
    bool ConfirmClose();
    void ApplyTheme();
    void LayoutFindBar();
    void UpdateFindStatus();
    static LRESULT CALLBACK FindEditProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam, UINT_PTR id, DWORD_PTR data);

    TermView m_View;
    std::vector<Tab *> m_Tabs;
    ShellFrame::TabBand m_Band;
    HFONT m_TabFont = NULL;
    int m_Active = -1;
    UINT m_NextSessionId = 1;
    bool m_Fullscreen = false;
    WINDOWPLACEMENT m_Placement = { sizeof(WINDOWPLACEMENT) };
    LONG_PTR m_SavedStyle = 0;
    HWND m_FindEdit = NULL;
    HWND m_FindStatus = NULL;
    HFONT m_FindFont = NULL;
    bool m_FindVisible = false;
    FILETIME m_SettingsTime = {};
};
