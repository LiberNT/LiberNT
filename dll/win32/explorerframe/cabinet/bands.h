/*
 * PROJECT:     LiberNT Explorer
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     File Explorer tab, address and search bands
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

#define CABINET_MAX_CRUMBS 32

struct CabinetPalette
{
    COLORREF Band;
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
};

void CabinetGetPalette(CabinetPalette *palette);
HIMAGELIST CabinetGlyphImages(const LPCWSTR *names, UINT count, int size, COLORREF color);
void CabinetFillRound(HDC dc, const RECT &rc, int radius, COLORREF color);

class ICabinetBandHost
{
public:
    virtual ~ICabinetBandHost() {}
    virtual void OnTabSelect(int index) = 0;
    virtual void OnTabClose(int index) = 0;
    virtual void OnTabMove(int from, int to) = 0;
    virtual void OnTabMenu(int index, POINT screen) = 0;
    virtual void OnNewTab() = 0;
    virtual void OnAddressNavigate(PCIDLIST_ABSOLUTE pidl) = 0;
    virtual void OnAddressText(LPCWSTR text) = 0;
    virtual void OnAddressCancel() = 0;
};

struct CabinetTabItem
{
    LPCWSTR Title;
    int IconIndex;
};

class CCabinetBand : public IOleWindow
{
public:
    CCabinetBand();
    virtual ~CCabinetBand();

    STDMETHODIMP QueryInterface(REFIID riid, void **ppv) override;
    STDMETHODIMP_(ULONG) AddRef() override;
    STDMETHODIMP_(ULONG) Release() override;
    STDMETHODIMP GetWindow(HWND *window) override;
    STDMETHODIMP ContextSensitiveHelp(BOOL enter) override;

    HWND Window() const { return m_hwnd; }

protected:
    static LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    virtual LRESULT HandleMessage(UINT msg, WPARAM wParam, LPARAM lParam) = 0;
    BOOL CreateBand(HWND parent, LPCWSTR className);
    int Scale(int value) const;

    LONG m_Refs;
    HWND m_hwnd;
    ICabinetBandHost *m_Host;
};

class CCabinetTabBand : public CCabinetBand
{
public:
    CCabinetTabBand();

    BOOL Create(HWND parent, ICabinetBandHost *host, HFONT font);
    void SetTabs(const CabinetTabItem *tabs, int count, int active);

protected:
    LRESULT HandleMessage(UINT msg, WPARAM wParam, LPARAM lParam) override;

private:
    struct Tab
    {
        WCHAR Title[MAX_PATH];
        int IconIndex;
    };

    int TabWidth() const;
    RECT TabRect(int index) const;
    RECT CloseRect(int index) const;
    RECT NewRect() const;
    int HitTab(POINT pt, BOOL *close) const;
    void Paint(HDC dc);
    void SetHot(int tab, BOOL close, BOOL add);

    Tab m_Tabs[32];
    int m_Count;
    int m_Active;
    int m_HotTab;
    BOOL m_HotClose;
    BOOL m_HotNew;
    BOOL m_Tracking;
    int m_DragTab;
    int m_PressedTab;
    BOOL m_PressedClose;
    BOOL m_PressedNew;
    HFONT m_Font;
};

class CCabinetAddressBand : public CCabinetBand
{
public:
    CCabinetAddressBand();
    ~CCabinetAddressBand();

    BOOL Create(HWND parent, ICabinetBandHost *host, HFONT font);
    void SetLocation(PCIDLIST_ABSOLUTE pidl);
    void BeginEdit();
    void EndEdit();
    BOOL IsEditing() const { return m_Editing; }
    HWND EditWindow() const { return m_Edit; }

protected:
    LRESULT HandleMessage(UINT msg, WPARAM wParam, LPARAM lParam) override;

private:
    static LRESULT CALLBACK EditProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam, UINT_PTR id, DWORD_PTR data);
    void FreeCrumbs();
    void Layout();
    void ShowChildren(int index);
    void ShowOverflow();
    RECT ButtonScreenRect(UINT id);

    HWND m_Toolbar;
    HWND m_Edit;
    HIMAGELIST m_Images;
    HFONT m_Font;
    PIDLIST_ABSOLUTE m_Crumbs[CABINET_MAX_CRUMBS];
    int m_CrumbCount;
    int m_FirstVisible;
    BOOL m_Editing;
    HBRUSH m_BoxBrush;
};

class CCabinetSearchBox : public CCabinetBand
{
public:
    CCabinetSearchBox();
    ~CCabinetSearchBox();

    BOOL Create(HWND parent, HFONT font);
    HWND EditWindow() const { return m_Edit; }

protected:
    LRESULT HandleMessage(UINT msg, WPARAM wParam, LPARAM lParam) override;

private:
    HWND m_Edit;
    HICON m_Glyph;
    HBRUSH m_BoxBrush;
};
