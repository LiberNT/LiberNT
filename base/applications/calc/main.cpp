/*
 * PROJECT:     LiberNT Calculator
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Calculator window: keypad, display, history, keyboard and clipboard
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <shellframe.h>
#include <shellapi.h>
#include <uxtheme.h>
#include <strsafe.h>

extern "C"
{
#include "calcengine.h"
}
#include "calcres.h"

using namespace ShellFrame;

#define CALC_CLASS L"LiberNTCalculator"
#define CALC_SETTINGS_KEY L"Software\\LiberNT\\Calculator"
#define MAX_CONTROLS 96
#define MAX_HISTORY 64
#define FLASH_TIMER 1
#define FALLBACK_FONTS 3

enum ControlKind
{
    KindDigit,
    KindOperator,
    KindEquals,
    KindSubtle,
    KindNavigation,
    KindHistory,
    KindBase,
    KindHistoryItem,
    KindHistoryClear
};

struct Control
{
    RECT Rect;
    ControlKind Kind;
    int Value;
};

struct Grid
{
    int Columns;
    int Rows;
    CALC_KEY Keys[7][5];
};

struct SubtleRow
{
    int Count;
    BOOL Spread;
    CALC_KEY Keys[6];
};

struct KeyText
{
    WCHAR Text[16];
    WCHAR Plain[16];
    LPCWSTR Icon;
};

struct HistoryEntry
{
    CALC_MODE Mode;
    WCHAR Expression[CALC_EXPRESSION_LENGTH];
    WCHAR Result[CALC_TEXT_LENGTH];
    char Raw[CALC_TEXT_LENGTH];
};

static const Grid Grids[3] =
{
    {
        4, 6,
        {
            {CALC_KEY_PERCENT, CALC_KEY_CLEAR_ENTRY, CALC_KEY_CLEAR, CALC_KEY_BACK},
            {CALC_KEY_RECIPROCAL, CALC_KEY_SQUARE, CALC_KEY_SQUARE_ROOT, CALC_KEY_DIVIDE},
            {CALC_KEY_7, CALC_KEY_8, CALC_KEY_9, CALC_KEY_MULTIPLY},
            {CALC_KEY_4, CALC_KEY_5, CALC_KEY_6, CALC_KEY_SUBTRACT},
            {CALC_KEY_1, CALC_KEY_2, CALC_KEY_3, CALC_KEY_ADD},
            {CALC_KEY_SIGN, CALC_KEY_0, CALC_KEY_POINT, CALC_KEY_EQUALS}
        }
    },
    {
        5, 7,
        {
            {CALC_KEY_INVERSE, CALC_KEY_PI, CALC_KEY_EULER, CALC_KEY_CLEAR, CALC_KEY_BACK},
            {CALC_KEY_SQUARE, CALC_KEY_RECIPROCAL, CALC_KEY_ABSOLUTE, CALC_KEY_EXPONENT, CALC_KEY_MODULO},
            {CALC_KEY_SQUARE_ROOT, CALC_KEY_OPEN, CALC_KEY_CLOSE, CALC_KEY_FACTORIAL, CALC_KEY_DIVIDE},
            {CALC_KEY_POWER, CALC_KEY_7, CALC_KEY_8, CALC_KEY_9, CALC_KEY_MULTIPLY},
            {CALC_KEY_TEN_POWER, CALC_KEY_4, CALC_KEY_5, CALC_KEY_6, CALC_KEY_SUBTRACT},
            {CALC_KEY_LOG, CALC_KEY_1, CALC_KEY_2, CALC_KEY_3, CALC_KEY_ADD},
            {CALC_KEY_LN, CALC_KEY_SIGN, CALC_KEY_0, CALC_KEY_POINT, CALC_KEY_EQUALS}
        }
    },
    {
        5, 6,
        {
            {CALC_KEY_A, CALC_KEY_SHIFT_LEFT, CALC_KEY_SHIFT_RIGHT, CALC_KEY_CLEAR, CALC_KEY_BACK},
            {CALC_KEY_B, CALC_KEY_OPEN, CALC_KEY_CLOSE, CALC_KEY_MODULO, CALC_KEY_DIVIDE},
            {CALC_KEY_C, CALC_KEY_7, CALC_KEY_8, CALC_KEY_9, CALC_KEY_MULTIPLY},
            {CALC_KEY_D, CALC_KEY_4, CALC_KEY_5, CALC_KEY_6, CALC_KEY_SUBTRACT},
            {CALC_KEY_E, CALC_KEY_1, CALC_KEY_2, CALC_KEY_3, CALC_KEY_ADD},
            {CALC_KEY_F, CALC_KEY_SIGN, CALC_KEY_0, CALC_KEY_POINT, CALC_KEY_EQUALS}
        }
    }
};

static const SubtleRow MemoryRow = {5, TRUE, {CALC_KEY_MEMORY_CLEAR, CALC_KEY_MEMORY_RECALL, CALC_KEY_MEMORY_ADD,
                                              CALC_KEY_MEMORY_SUBTRACT, CALC_KEY_MEMORY_STORE}};
static const SubtleRow AngleRow = {2, FALSE, {CALC_KEY_ANGLE, CALC_KEY_FORMAT}};
static const SubtleRow TrigonometryRow = {4, TRUE, {CALC_KEY_SIN, CALC_KEY_COS, CALC_KEY_TAN, CALC_KEY_HYPERBOLIC}};
static const SubtleRow BitwiseRow = {5, TRUE, {CALC_KEY_AND, CALC_KEY_OR, CALC_KEY_XOR, CALC_KEY_NOT, CALC_KEY_WORD}};

static const SubtleRow *const ModeRows[3][3] =
{
    {&MemoryRow, NULL, NULL},
    {&AngleRow, &MemoryRow, &TrigonometryRow},
    {&BitwiseRow, &MemoryRow, NULL}
};

static const SIZE ViewSize = {320, 500};

static HINSTANCE Instance;
static HistoryEntry History[MAX_HISTORY];
static int HistoryCount;

static COLORREF
Mix(COLORREF back, COLORREF fore, int alpha)
{
    return RGB((GetRValue(fore) * alpha + GetRValue(back) * (255 - alpha)) / 255,
               (GetGValue(fore) * alpha + GetGValue(back) * (255 - alpha)) / 255,
               (GetBValue(fore) * alpha + GetBValue(back) * (255 - alpha)) / 255);
}

static BOOL
IsDigitKey(CALC_KEY Key)
{
    return (Key >= CALC_KEY_0 && Key <= CALC_KEY_F) || Key == CALC_KEY_POINT || Key == CALC_KEY_SIGN;
}

static void
SetText(KeyText *Label, LPCWSTR Text, LPCWSTR Plain)
{
    StringCchCopyW(Label->Text, ARRAYSIZE(Label->Text), Text);
    StringCchCopyW(Label->Plain, ARRAYSIZE(Label->Plain), Plain ? Plain : Text);
    Label->Icon = NULL;
}

static BOOL
FontHasText(HDC Context, HFONT Font, LPCWSTR Text)
{
    WORD Glyphs[16];
    int Length = lstrlenW(Text);
    HGDIOBJ Previous;
    BOOL Result = TRUE;

    if (Length > (int)ARRAYSIZE(Glyphs))
        return FALSE;
    Previous = SelectObject(Context, Font);
    if (GetGlyphIndicesW(Context, Text, Length, Glyphs, GGI_MARK_NONEXISTING_GLYPHS) == GDI_ERROR)
        Result = FALSE;
    for (int Index = 0; Result && Index < Length; Index++)
        Result = Glyphs[Index] != 0xFFFF;
    SelectObject(Context, Previous);
    return Result;
}

static void
GroupDigits(const WCHAR *Digits, int Count, int Width, const WCHAR *Mark, WCHAR *Output, int Size)
{
    int Used = lstrlenW(Output);

    for (int Index = 0; Index < Count && Used + 5 < Size; Index++)
    {
        if (Index != 0 && (Count - Index) % Width == 0)
        {
            StringCchCopyW(Output + Used, Size - Used, Mark);
            Used += lstrlenW(Mark);
        }
        Output[Used++] = Digits[Index];
    }
    Output[Used] = 0;
}

class CalculatorWindow : public FrameWindow
{
public:
    CalculatorWindow() :
        m_ControlCount(0),
        m_Hot(-1),
        m_Pressed(-1),
        m_Flash(-1),
        m_Tracking(FALSE),
        m_HistoryOpen(FALSE),
        m_HistoryScroll(0),
        m_PanelTop(0),
        m_ValueLeft(0),
        m_SmallIcon(NULL),
        m_BigIcon(NULL),
        m_DigitFont(NULL),
        m_SmallFonts(),
        m_KeyFonts(),
        m_ValueFonts()
    {
        CalcInit(&m_Engine);
        StringCchCopyW(m_DecimalMark, ARRAYSIZE(m_DecimalMark), L".");
        StringCchCopyW(m_GroupMark, ARRAYSIZE(m_GroupMark), L",");
        SetRectEmpty(&m_Header);
        SetRectEmpty(&m_Display);
        SetRectEmpty(&m_Panel);
    }

    ~CalculatorWindow()
    {
        DestroyFonts();
        if (m_SmallIcon)
            DestroyIcon(m_SmallIcon);
        if (m_BigIcon)
            DestroyIcon(m_BigIcon);
    }

    void LoadSettings(WINDOWPLACEMENT *Placement, BOOL *HavePlacement)
    {
        DWORD Mode = CALC_MODE_STANDARD, Size = sizeof(Mode);
        HKEY Key;

        *HavePlacement = FALSE;
        if (RegOpenKeyExW(HKEY_CURRENT_USER, CALC_SETTINGS_KEY, 0, KEY_READ, &Key) != ERROR_SUCCESS)
            return;
        if (RegQueryValueExW(Key, L"Mode", NULL, NULL, (BYTE *)&Mode, &Size) == ERROR_SUCCESS &&
            Mode <= CALC_MODE_PROGRAMMER)
            CalcSetMode(&m_Engine, (CALC_MODE)Mode);
        Size = sizeof(*Placement);
        if (RegQueryValueExW(Key, L"Window", NULL, NULL, (BYTE *)Placement, &Size) == ERROR_SUCCESS &&
            Size == sizeof(*Placement) && Placement->length == sizeof(*Placement))
            *HavePlacement = TRUE;
        RegCloseKey(Key);
    }

    void FitView()
    {
        MONITORINFO Monitor = {sizeof(Monitor)};
        SIZE Frame = FrameExtra();
        RECT Window;
        int Width = S(ViewSize.cx) + Frame.cx, Height = S(ViewSize.cy) + Frame.cy;

        GetWindowRect(m_hwnd, &Window);
        if (GetMonitorInfoW(MonitorFromWindow(m_hwnd, MONITOR_DEFAULTTONEAREST), &Monitor))
        {
            const RECT &Work = Monitor.rcWork;

            Window.left = max(Work.left, min(Window.left, Work.right - Width));
            Window.top = max(Work.top, min(Window.top, Work.bottom - Height));
        }
        SetWindowPos(m_hwnd, NULL, Window.left, Window.top, Width, Height, SWP_NOZORDER | SWP_NOACTIVATE);
    }

protected:
    int CaptionHeight() const override { return m_Theme.Scale(32); }

    void OnCreate() override
    {
        LoadMarks();
        CreateFonts();
        UpdateIcons();
    }

    void OnThemeChanged() override
    {
        CreateFonts();
        UpdateIcons();
    }

    void OnLayout(const RECT &Client) override
    {
        Layout(Client);
    }

    void OnPaint(Canvas &Surface, const RECT &Client) override
    {
        const Palette &c = m_Theme.Colors();
        RECT Caption = {0, 0, Client.right, CaptionHeight()};
        RECT Body = {0, CaptionHeight(), Client.right, Client.bottom};
        RECT Title = {S(16), 0, Client.right, CaptionHeight()};
        WCHAR Text[64];

        Surface.Fill(Body, c.Window);
        Surface.Fill(Caption, Glass(GlassTone::Clear));
        LoadStringW(Instance, IDS_TITLE, Text, ARRAYSIZE(Text));
        Surface.Text(Text, Title, GlassFont(TextStyle::Caption),
                     IsActive() ? Glass(GlassTone::Text) : Glass(GlassTone::TextSecondary),
                     DT_SINGLELINE | DT_VCENTER | DT_LEFT);
        LoadStringW(Instance, IDS_STANDARD + m_Engine.Mode, Text, ARRAYSIZE(Text));
        Surface.Text(Text, m_Header, m_Theme.Font(TextStyle::Subtitle), c.Text, DT_SINGLELINE | DT_VCENTER | DT_LEFT);
        PaintDisplay(Surface);
        for (int Index = 0; Index < m_ControlCount; Index++)
        {
            if (m_Controls[Index].Kind != KindHistoryItem && m_Controls[Index].Kind != KindHistoryClear)
                PaintControl(Surface, Index);
        }
        if (m_HistoryOpen)
            PaintHistory(Surface);
#if SHELLFRAME_CUSTOM_CAPTION_BUTTONS
        PaintCaptionButtons(Surface, Client, Glass(GlassTone::Clear));
#endif
    }

    LRESULT HandleMessage(UINT Message, WPARAM WParam, LPARAM LParam) override
    {
        switch (Message)
        {
            case WM_MOUSEMOVE:
            {
                POINT Point = {GET_X_LPARAM(LParam), GET_Y_LPARAM(LParam)};

                if (!m_Tracking)
                {
                    TRACKMOUSEEVENT Track = {sizeof(Track), TME_LEAVE, m_hwnd, 0};
                    m_Tracking = TrackMouseEvent(&Track);
                }
                SetHot(HitControl(Point));
                break;
            }
            case WM_MOUSELEAVE:
                m_Tracking = FALSE;
                SetHot(-1);
                return 0;
            case WM_LBUTTONDOWN:
            case WM_LBUTTONDBLCLK:
            {
                POINT Point = {GET_X_LPARAM(LParam), GET_Y_LPARAM(LParam)};
                int Hit = HitControl(Point);

                if (m_HistoryOpen && !PtInRect(&m_Panel, Point) && (Hit < 0 || m_Controls[Hit].Kind != KindHistory))
                {
                    ShowHistory(FALSE);
                    return 0;
                }
                if (Hit >= 0 && ControlEnabled(Hit))
                {
                    m_Pressed = Hit;
                    SetCapture(m_hwnd);
                    Invalidate(&m_Controls[Hit].Rect);
                }
                return 0;
            }
            case WM_LBUTTONUP:
            {
                POINT Point = {GET_X_LPARAM(LParam), GET_Y_LPARAM(LParam)};
                int Pressed = m_Pressed;

                m_Pressed = -1;
                if (GetCapture() == m_hwnd)
                    ReleaseCapture();
                if (Pressed >= 0 && HitControl(Point) == Pressed)
                    Activate(Pressed);
                Invalidate(NULL);
                return 0;
            }
            case WM_CAPTURECHANGED:
                if (m_Pressed >= 0)
                {
                    m_Pressed = -1;
                    Invalidate(NULL);
                }
                break;
            case WM_RBUTTONUP:
            {
                POINT Point = {GET_X_LPARAM(LParam), GET_Y_LPARAM(LParam)};

                if (PtInRect(&m_Display, Point))
                {
                    ClientToScreen(m_hwnd, &Point);
                    ShowEditMenu(Point);
                    return 0;
                }
                break;
            }
            case WM_MOUSEWHEEL:
                if (m_HistoryOpen)
                {
                    m_HistoryScroll += GET_WHEEL_DELTA_WPARAM(WParam) > 0 ? -1 : 1;
                    RefreshLayout();
                    return 0;
                }
                break;
            case WM_CHAR:
            {
                CALC_KEY Key = GetKeyState(VK_CONTROL) < 0 ? CALC_KEY_NONE : KeyFromCharacter((WCHAR)WParam);

                if (Key != CALC_KEY_NONE)
                    PressFromKeyboard(Key);
                return 0;
            }
            case WM_KEYDOWN:
                if (OnKeyDown(WParam))
                    return 0;
                break;
            case WM_SYSKEYDOWN:
                if (WParam >= '1' && WParam <= '3')
                {
                    SetMode((CALC_MODE)(WParam - '1'));
                    return 0;
                }
                break;
            case WM_SYSCHAR:
                if (WParam >= '1' && WParam <= '3')
                    return 0;
                break;
            case WM_TIMER:
                if (WParam == FLASH_TIMER)
                {
                    KillTimer(m_hwnd, FLASH_TIMER);
                    m_Flash = -1;
                    Invalidate(NULL);
                    return 0;
                }
                break;
            case WM_GETMINMAXINFO:
            {
                MINMAXINFO *Info = (MINMAXINFO *)LParam;

                SIZE Frame = FrameExtra();

                Info->ptMinTrackSize.x = S(ViewSize.cx) + Frame.cx;
                Info->ptMinTrackSize.y = S(ViewSize.cy) + Frame.cy;
                return 0;
            }
            case WM_SETTINGCHANGE:
                if (LParam && !lstrcmpiW((LPCWSTR)LParam, L"intl"))
                {
                    LoadMarks();
                    Invalidate(NULL);
                }
                break;
            case WM_DESTROY:
                SaveSettings();
                PostQuitMessage(0);
                break;
        }
        return FrameWindow::HandleMessage(Message, WParam, LParam);
    }

private:
    int S(int Value) const { return m_Theme.Scale(Value); }

    SIZE FrameExtra() const
    {
        RECT Window, Client;
        SIZE Extra = {0, 0};

        if (m_hwnd && GetWindowRect(m_hwnd, &Window) && GetClientRect(m_hwnd, &Client))
        {
            Extra.cx = max(0L, (Window.right - Window.left) - Client.right);
            Extra.cy = max(0L, (Window.bottom - Window.top) - Client.bottom);
        }
        return Extra;
    }

    void MakeFont(HFONT *Font, int Height, int Weight, LPCWSTR Face)
    {
        if (*Font)
            DeleteObject(*Font);
        *Font = CreateFontW(-S(Height), 0, 0, 0, Weight, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                            CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, Face);
    }

    void CreateFonts()
    {
        static const LPCWSTR Faces[FALLBACK_FONTS] = {L"Segoe UI Variable Text", L"Arial", L"Verdana"};
        static const int ValueSizes[4] = {46, 36, 28, 20};

        MakeFont(&m_DigitFont, 20, FW_SEMIBOLD, L"Segoe UI Semibold");
        for (int Index = 0; Index < FALLBACK_FONTS; Index++)
        {
            MakeFont(&m_KeyFonts[Index], 16, FW_NORMAL, Faces[Index]);
            MakeFont(&m_SmallFonts[Index], 12, FW_NORMAL, Faces[Index]);
        }
        for (int Index = 0; Index < 4; Index++)
            MakeFont(&m_ValueFonts[Index], ValueSizes[Index], FW_SEMIBOLD, L"Segoe UI Semibold");
    }

    void DestroyFonts()
    {
        HFONT *Fonts[] = {&m_DigitFont, &m_KeyFonts[0], &m_KeyFonts[1], &m_KeyFonts[2], &m_SmallFonts[0],
                          &m_SmallFonts[1], &m_SmallFonts[2], &m_ValueFonts[0], &m_ValueFonts[1],
                          &m_ValueFonts[2], &m_ValueFonts[3]};

        for (HFONT *Font : Fonts)
        {
            if (*Font)
                DeleteObject(*Font);
            *Font = NULL;
        }
    }

    void UpdateIcons()
    {
        HICON Small = CreateGlyphIcon(L"calculator_20_regular", GetSystemMetricsForDpi(SM_CXSMICON, m_Theme.Dpi()),
                                      m_Theme.Colors().Accent);
        HICON Big = CreateGlyphIcon(L"calculator_24_regular", GetSystemMetricsForDpi(SM_CXICON, m_Theme.Dpi()),
                                    m_Theme.Colors().Accent);

        SendMessageW(m_hwnd, WM_SETICON, ICON_SMALL, (LPARAM)Small);
        SendMessageW(m_hwnd, WM_SETICON, ICON_BIG, (LPARAM)Big);
        if (m_SmallIcon)
            DestroyIcon(m_SmallIcon);
        if (m_BigIcon)
            DestroyIcon(m_BigIcon);
        m_SmallIcon = Small;
        m_BigIcon = Big;
    }

    void LoadMarks()
    {
        if (GetLocaleInfoW(LOCALE_USER_DEFAULT, LOCALE_SDECIMAL, m_DecimalMark, ARRAYSIZE(m_DecimalMark)) == 0)
            StringCchCopyW(m_DecimalMark, ARRAYSIZE(m_DecimalMark), L".");
        if (GetLocaleInfoW(LOCALE_USER_DEFAULT, LOCALE_STHOUSAND, m_GroupMark, ARRAYSIZE(m_GroupMark)) == 0)
            StringCchCopyW(m_GroupMark, ARRAYSIZE(m_GroupMark), L",");
    }

    void SaveSettings()
    {
        WINDOWPLACEMENT Placement = {sizeof(Placement)};
        DWORD Mode = m_Engine.Mode;
        HKEY Key;

        if (RegCreateKeyExW(HKEY_CURRENT_USER, CALC_SETTINGS_KEY, 0, NULL, 0, KEY_WRITE, NULL, &Key, NULL) !=
            ERROR_SUCCESS)
            return;
        RegSetValueExW(Key, L"Mode", 0, REG_DWORD, (const BYTE *)&Mode, sizeof(Mode));
        if (GetWindowPlacement(m_hwnd, &Placement))
            RegSetValueExW(Key, L"Window", 0, REG_BINARY, (const BYTE *)&Placement, sizeof(Placement));
        RegCloseKey(Key);
    }

    void FormatText(const char *Text, int Base, WCHAR *Output, int Size) const
    {
        WCHAR Wide[CALC_TEXT_LENGTH];
        const WCHAR *Cursor = Wide;
        int Whole = 0, Used;

        MultiByteToWideChar(CP_UTF8, 0, Text, -1, Wide, ARRAYSIZE(Wide));
        Output[0] = 0;
        if (*Cursor == L'-')
        {
            StringCchCopyW(Output, Size, L"-");
            Cursor++;
        }
        while ((Cursor[Whole] >= L'0' && Cursor[Whole] <= L'9') || (Cursor[Whole] >= L'A' && Cursor[Whole] <= L'F'))
            Whole++;
        if (Base == 10)
            GroupDigits(Cursor, Whole, 3, m_GroupMark, Output, Size);
        else
            GroupDigits(Cursor, Whole, Base == 8 ? 3 : 4, L" ", Output, Size);
        Cursor += Whole;
        Used = lstrlenW(Output);
        for (; *Cursor != 0 && Used + 5 < Size; Cursor++)
        {
            if (*Cursor == L'.')
            {
                StringCchCopyW(Output + Used, Size - Used, m_DecimalMark);
                Used += lstrlenW(m_DecimalMark);
            }
            else
            {
                Output[Used++] = *Cursor;
            }
        }
        Output[Used] = 0;
    }

    void DisplayText(WCHAR *Output, int Size) const
    {
        static const UINT Messages[] = {0, IDS_DIVIDE_BY_ZERO, IDS_INVALID_INPUT, IDS_OVERFLOW, IDS_UNDEFINED};
        char Text[CALC_TEXT_LENGTH];

        if (m_Engine.Error != CALC_OK)
        {
            LoadStringW(Instance, Messages[m_Engine.Error], Output, Size);
            return;
        }
        CalcDisplay(&m_Engine, Text, sizeof(Text));
        FormatText(Text, m_Engine.Mode == CALC_MODE_PROGRAMMER ? m_Engine.Base : 10, Output, Size);
    }

    CALC_KEY Effective(CALC_KEY Key) const
    {
        if (m_Engine.Mode == CALC_MODE_SCIENTIFIC && m_Engine.Inverse)
        {
            if (Key == CALC_KEY_SQUARE)
                return CALC_KEY_CUBE;
            if (Key == CALC_KEY_POWER)
                return CALC_KEY_ROOT;
            if (Key == CALC_KEY_LN)
                return CALC_KEY_E_POWER;
        }
        if (Key == CALC_KEY_CLEAR && m_Engine.Mode != CALC_MODE_STANDARD && m_Engine.Entering)
            return CALC_KEY_CLEAR_ENTRY;
        return Key;
    }

    BOOL KeyChecked(CALC_KEY Key) const
    {
        switch (Key)
        {
            case CALC_KEY_INVERSE:
                return m_Engine.Inverse;
            case CALC_KEY_HYPERBOLIC:
                return m_Engine.Hyperbolic;
            case CALC_KEY_FORMAT:
                return m_Engine.Exponential;
            default:
                return FALSE;
        }
    }

    BOOL KeyEnabled(CALC_KEY Key) const
    {
        if (!CalcKeyEnabled(&m_Engine, Key))
            return FALSE;
        if ((Key == CALC_KEY_MEMORY_CLEAR || Key == CALC_KEY_MEMORY_RECALL) && !m_Engine.HasMemory)
            return FALSE;
        if (m_Engine.Error == CALC_OK)
            return TRUE;
        return (Key >= CALC_KEY_0 && Key <= CALC_KEY_POINT) || Key == CALC_KEY_PI || Key == CALC_KEY_EULER ||
               Key == CALC_KEY_MEMORY_RECALL || Key == CALC_KEY_MEMORY_CLEAR || Key == CALC_KEY_CLEAR ||
               Key == CALC_KEY_CLEAR_ENTRY || Key == CALC_KEY_INVERSE || Key == CALC_KEY_HYPERBOLIC ||
               Key == CALC_KEY_ANGLE || Key == CALC_KEY_FORMAT;
    }

    void Label(CALC_KEY Key, KeyText *Label) const
    {
        static const LPCWSTR Angles[3] = {L"DEG", L"RAD", L"GRAD"};
        static const LPCWSTR Trigonometry[3] = {L"sin", L"cos", L"tan"};

        if (Key >= CALC_KEY_0 && Key <= CALC_KEY_F)
        {
            WCHAR Digit[2] = {L"0123456789ABCDEF"[Key - CALC_KEY_0], 0};
            SetText(Label, Digit, NULL);
            return;
        }
        switch (Key)
        {
            case CALC_KEY_POINT: SetText(Label, m_DecimalMark, NULL); return;
            case CALC_KEY_SIGN: SetText(Label, L"\x207A\x2215\x208B", L"+/-"); return;
            case CALC_KEY_BACK: SetText(Label, L"", NULL); Label->Icon = L"backspace_20_regular"; return;
            case CALC_KEY_CLEAR_ENTRY: SetText(Label, L"CE", NULL); return;
            case CALC_KEY_CLEAR: SetText(Label, L"C", NULL); return;
            case CALC_KEY_ADD: SetText(Label, L"+", NULL); return;
            case CALC_KEY_SUBTRACT: SetText(Label, L"\x2212", L"-"); return;
            case CALC_KEY_MULTIPLY: SetText(Label, L"\x00D7", L"*"); return;
            case CALC_KEY_DIVIDE: SetText(Label, L"\x00F7", L"/"); return;
            case CALC_KEY_EQUALS: SetText(Label, L"=", NULL); return;
            case CALC_KEY_PERCENT: SetText(Label, L"%", NULL); return;
            case CALC_KEY_MODULO:
                SetText(Label, m_Engine.Mode == CALC_MODE_PROGRAMMER ? L"%" : L"mod", NULL);
                return;
            case CALC_KEY_OPEN: SetText(Label, L"(", NULL); return;
            case CALC_KEY_CLOSE: SetText(Label, L")", NULL); return;
            case CALC_KEY_RECIPROCAL: SetText(Label, L"\x00B9\x2215\x2093", L"1/x"); return;
            case CALC_KEY_SQUARE: SetText(Label, L"x\x00B2", NULL); return;
            case CALC_KEY_CUBE: SetText(Label, L"x\x00B3", NULL); return;
            case CALC_KEY_SQUARE_ROOT: SetText(Label, L"\x00B2\x221Ax", L"sqrt"); return;
            case CALC_KEY_POWER: SetText(Label, L"x\x02B8", L"x^y"); return;
            case CALC_KEY_ROOT: SetText(Label, L"\x02B8\x221Ax", L"yroot"); return;
            case CALC_KEY_TEN_POWER: SetText(Label, L"10\x02E3", L"10^x"); return;
            case CALC_KEY_E_POWER: SetText(Label, L"e\x02E3", L"e^x"); return;
            case CALC_KEY_LOG: SetText(Label, L"log", NULL); return;
            case CALC_KEY_LN: SetText(Label, L"ln", NULL); return;
            case CALC_KEY_ABSOLUTE: SetText(Label, L"|x|", NULL); return;
            case CALC_KEY_FACTORIAL: SetText(Label, L"n!", NULL); return;
            case CALC_KEY_EXPONENT: SetText(Label, L"exp", NULL); return;
            case CALC_KEY_PI: SetText(Label, L"\x03C0", L"pi"); return;
            case CALC_KEY_EULER: SetText(Label, L"e", NULL); return;
            case CALC_KEY_INVERSE: SetText(Label, L"2\x207F\x1D48", L"2nd"); return;
            case CALC_KEY_HYPERBOLIC: SetText(Label, L"hyp", NULL); return;
            case CALC_KEY_ANGLE: SetText(Label, Angles[m_Engine.Angle], NULL); return;
            case CALC_KEY_FORMAT: SetText(Label, L"F-E", NULL); return;
            case CALC_KEY_AND: SetText(Label, L"AND", NULL); return;
            case CALC_KEY_OR: SetText(Label, L"OR", NULL); return;
            case CALC_KEY_XOR: SetText(Label, L"XOR", NULL); return;
            case CALC_KEY_NOT: SetText(Label, L"NOT", NULL); return;
            case CALC_KEY_SHIFT_LEFT: SetText(Label, L"<<", NULL); return;
            case CALC_KEY_SHIFT_RIGHT: SetText(Label, L">>", NULL); return;
            case CALC_KEY_MEMORY_CLEAR: SetText(Label, L"MC", NULL); return;
            case CALC_KEY_MEMORY_RECALL: SetText(Label, L"MR", NULL); return;
            case CALC_KEY_MEMORY_ADD: SetText(Label, L"M+", NULL); return;
            case CALC_KEY_MEMORY_SUBTRACT: SetText(Label, L"M\x2212", L"M-"); return;
            case CALC_KEY_MEMORY_STORE: SetText(Label, L"MS", NULL); return;
            case CALC_KEY_WORD:
                SetText(Label, m_Engine.WordBits == 64 ? L"QWORD" : m_Engine.WordBits == 32 ? L"DWORD" :
                               m_Engine.WordBits == 16 ? L"WORD" : L"BYTE", NULL);
                return;
            case CALC_KEY_SIN:
            case CALC_KEY_COS:
            case CALC_KEY_TAN:
            {
                WCHAR Text[16], Plain[16];

                StringCchPrintfW(Text, ARRAYSIZE(Text), L"%s%s%s", Trigonometry[Key - CALC_KEY_SIN],
                                 m_Engine.Hyperbolic ? L"h" : L"", m_Engine.Inverse ? L"\x207B\x00B9" : L"");
                StringCchPrintfW(Plain, ARRAYSIZE(Plain), L"%s%s%s", m_Engine.Inverse ? L"a" : L"",
                                 Trigonometry[Key - CALC_KEY_SIN], m_Engine.Hyperbolic ? L"h" : L"");
                SetText(Label, Text, Plain);
                return;
            }
            default:
                SetText(Label, L"", NULL);
                return;
        }
    }

    void AddControl(const RECT &Rect, ControlKind Kind, int Value)
    {
        if (m_ControlCount == MAX_CONTROLS)
            return;
        m_Controls[m_ControlCount].Rect = Rect;
        m_Controls[m_ControlCount].Kind = Kind;
        m_Controls[m_ControlCount].Value = Value;
        m_ControlCount++;
    }

    void Layout(const RECT &Client)
    {
        const Grid &Keys = Grids[m_Engine.Mode];
        int Margin = S(4), Gap = S(2), Top = CaptionHeight(), Width = Client.right - 2 * Margin;
        int Bottom = Client.bottom - Margin, RowHeight = S(34), CellWidth, CellHeight;
        RECT Rect;

        m_ControlCount = 0;
        SetRect(&Rect, Margin, Top + S(2), Margin + S(40), Top + S(38));
        AddControl(Rect, KindNavigation, 0);
        SetRect(&m_Header, Rect.right + S(8), Top, Client.right - S(48), Top + S(40));
        if (m_Engine.Mode != CALC_MODE_PROGRAMMER)
        {
            SetRect(&Rect, Client.right - Margin - S(40), Top + S(2), Client.right - Margin, Top + S(38));
            AddControl(Rect, KindHistory, 0);
        }
        Top += S(40);
        SetRect(&m_Display, S(12), Top, Client.right - S(12), Top + S(24) + S(64));
        m_ValueLeft = m_Display.left;
        if (m_Engine.Mode == CALC_MODE_PROGRAMMER)
        {
            int Column = (m_Display.right - Margin) * 11 / 20, Each = (m_Display.bottom - m_Display.top) / 4;

            for (int Index = 0; Index < 4; Index++)
            {
                SetRect(&Rect, Margin, m_Display.top + Index * Each, Margin + Column, m_Display.top + (Index + 1) * Each);
                AddControl(Rect, KindBase, Index);
            }
            m_ValueLeft = Margin + Column + S(8);
        }
        Top = m_Display.bottom + S(4);
        m_PanelTop = Top;
        for (int Row = 0; Row < 3 && ModeRows[m_Engine.Mode][Row] != NULL; Row++)
        {
            const SubtleRow *Line = ModeRows[m_Engine.Mode][Row];
            int Each = Line->Spread ? (Width - Gap * (Line->Count - 1)) / Line->Count : S(64);

            for (int Column = 0; Column < Line->Count; Column++)
            {
                SetRect(&Rect, Margin + Column * (Each + Gap), Top, Margin + Column * (Each + Gap) + Each, Top + RowHeight);
                AddControl(Rect, KindSubtle, Line->Keys[Column]);
            }
            Top += RowHeight + Gap;
        }
        CellWidth = (Width - Gap * (Keys.Columns - 1)) / Keys.Columns;
        CellHeight = max(S(24), (Bottom - Top - Gap * (Keys.Rows - 1)) / Keys.Rows);
        for (int Row = 0; Row < Keys.Rows; Row++)
        {
            for (int Column = 0; Column < Keys.Columns; Column++)
            {
                CALC_KEY Key = Keys.Keys[Row][Column];
                int Left = Margin + Column * (CellWidth + Gap), KeyTop = Top + Row * (CellHeight + Gap);

                SetRect(&Rect, Left, KeyTop, Column == Keys.Columns - 1 ? Client.right - Margin : Left + CellWidth,
                        Row == Keys.Rows - 1 ? Bottom : KeyTop + CellHeight);
                AddControl(Rect, Key == CALC_KEY_EQUALS ? KindEquals : IsDigitKey(Key) ? KindDigit : KindOperator,
                           Key);
            }
        }
        SetRect(&m_Panel, Margin, m_PanelTop, Client.right - Margin, Bottom);
        if (m_HistoryOpen)
            LayoutHistory();
    }

    void LayoutHistory()
    {
        int ItemHeight = S(64), Visible, Count = 0, Shown = 0, Top = m_Panel.top + S(8);
        int Bottom = m_Panel.bottom - S(48);
        RECT Rect;

        for (int Index = 0; Index < HistoryCount; Index++)
            Count += History[Index].Mode == m_Engine.Mode;
        Visible = max(1, (Bottom - Top) / ItemHeight);
        m_HistoryScroll = max(0, min(m_HistoryScroll, Count - Visible));
        for (int Index = HistoryCount - 1, Order = 0; Index >= 0 && Shown < Visible; Index--)
        {
            if (History[Index].Mode != m_Engine.Mode)
                continue;
            if (Order++ < m_HistoryScroll)
                continue;
            SetRect(&Rect, m_Panel.left + S(4), Top, m_Panel.right - S(4), Top + ItemHeight);
            AddControl(Rect, KindHistoryItem, Index);
            Top += ItemHeight;
            Shown++;
        }
        if (Count != 0)
        {
            SetRect(&Rect, m_Panel.right - S(44), m_Panel.bottom - S(44), m_Panel.right - S(4), m_Panel.bottom - S(4));
            AddControl(Rect, KindHistoryClear, 0);
        }
    }

    void RefreshLayout()
    {
        RECT Client;

        GetClientRect(m_hwnd, &Client);
        Layout(Client);
        m_Hot = -1;
        Invalidate(NULL);
    }

    int HitControl(POINT Point) const
    {
        for (int Index = m_ControlCount - 1; Index >= 0; Index--)
        {
            if (!PtInRect(&m_Controls[Index].Rect, Point))
                continue;
            if (m_HistoryOpen && PtInRect(&m_Panel, Point) && m_Controls[Index].Kind != KindHistoryItem &&
                m_Controls[Index].Kind != KindHistoryClear)
                return -1;
            return Index;
        }
        return -1;
    }

    BOOL ControlEnabled(int Index) const
    {
        const Control &Item = m_Controls[Index];

        if (Item.Kind == KindDigit || Item.Kind == KindOperator || Item.Kind == KindEquals || Item.Kind == KindSubtle)
            return KeyEnabled(Effective((CALC_KEY)Item.Value));
        if (Item.Kind == KindBase)
            return m_Engine.Error == CALC_OK;
        return TRUE;
    }

    void SetHot(int Hot)
    {
        if (Hot == m_Hot)
            return;
        m_Hot = Hot;
        Invalidate(NULL);
    }

    void Activate(int Index)
    {
        static const CALC_KEY Bases[4] = {CALC_KEY_HEX, CALC_KEY_DEC, CALC_KEY_OCT, CALC_KEY_BIN};
        const Control Item = m_Controls[Index];

        switch (Item.Kind)
        {
            case KindNavigation:
                ShowNavigation(Item.Rect);
                break;
            case KindHistory:
                ShowHistory(!m_HistoryOpen);
                break;
            case KindBase:
                Press(Bases[Item.Value]);
                break;
            case KindHistoryItem:
                if (CalcPaste(&m_Engine, History[Item.Value].Raw))
                    ShowHistory(FALSE);
                break;
            case KindHistoryClear:
            {
                int Kept = 0;

                for (int From = 0; From < HistoryCount; From++)
                {
                    if (History[From].Mode != m_Engine.Mode)
                        History[Kept++] = History[From];
                }
                HistoryCount = Kept;
                RefreshLayout();
                break;
            }
            default:
                Press(Effective((CALC_KEY)Item.Value));
                break;
        }
        Invalidate(NULL);
    }

    void AddHistory()
    {
        HistoryEntry *Entry;
        char Raw[CALC_TEXT_LENGTH];

        if (HistoryCount == MAX_HISTORY)
        {
            MoveMemory(&History[0], &History[1], sizeof(History[0]) * (MAX_HISTORY - 1));
            HistoryCount--;
        }
        Entry = &History[HistoryCount++];
        Entry->Mode = m_Engine.Mode;
        MultiByteToWideChar(CP_UTF8, 0, m_Engine.Expression, -1, Entry->Expression, ARRAYSIZE(Entry->Expression));
        DisplayText(Entry->Result, ARRAYSIZE(Entry->Result));
        CalcDisplay(&m_Engine, Raw, sizeof(Raw));
        StringCchCopyA(Entry->Raw, ARRAYSIZE(Entry->Raw), Raw);
    }

    void Press(CALC_KEY Key)
    {
        if (Key == CALC_KEY_PERCENT && m_Engine.Mode != CALC_MODE_STANDARD)
            Key = CALC_KEY_MODULO;
        CalcPress(&m_Engine, Key);
        if (Key == CALC_KEY_EQUALS && m_Engine.Error == CALC_OK && m_Engine.Mode != CALC_MODE_PROGRAMMER)
            AddHistory();
        if (m_HistoryOpen)
            RefreshLayout();
        Invalidate(NULL);
    }

    void PressFromKeyboard(CALC_KEY Key)
    {
        for (int Index = 0; Index < m_ControlCount; Index++)
        {
            const Control &Item = m_Controls[Index];

            if ((Item.Kind == KindDigit || Item.Kind == KindOperator || Item.Kind == KindEquals ||
                 Item.Kind == KindSubtle) && Effective((CALC_KEY)Item.Value) == Key)
            {
                m_Flash = Index;
                SetTimer(m_hwnd, FLASH_TIMER, 100, NULL);
                break;
            }
        }
        if (m_HistoryOpen)
            ShowHistory(FALSE);
        Press(Key);
    }

    void ShowHistory(BOOL Show)
    {
        m_HistoryOpen = Show;
        m_HistoryScroll = 0;
        RefreshLayout();
    }

    void SetMode(CALC_MODE Mode)
    {
        if (Mode == m_Engine.Mode)
            return;
        CalcSetMode(&m_Engine, Mode);
        m_HistoryOpen = FALSE;
        RefreshLayout();
    }

    void ShowNavigation(const RECT &Anchor)
    {
        HMENU Menu = CreatePopupMenu();
        POINT Point = {Anchor.left, Anchor.bottom};
        WCHAR Text[64];
        UINT Command;

        if (!Menu)
            return;
        for (UINT Index = 0; Index < 3; Index++)
        {
            LoadStringW(Instance, IDS_STANDARD + Index, Text, ARRAYSIZE(Text));
            AppendMenuW(Menu, MF_STRING, IDM_STANDARD + Index, Text);
        }
        AppendMenuW(Menu, MF_SEPARATOR, 0, NULL);
        LoadStringW(Instance, IDS_ABOUT, Text, ARRAYSIZE(Text));
        AppendMenuW(Menu, MF_STRING, IDM_ABOUT, Text);
        CheckMenuRadioItem(Menu, IDM_STANDARD, IDM_PROGRAMMER, IDM_STANDARD + m_Engine.Mode, MF_BYCOMMAND);
        ClientToScreen(m_hwnd, &Point);
        Command = (UINT)TrackPopupMenuEx(Menu, TPM_RETURNCMD | TPM_LEFTALIGN | TPM_TOPALIGN, Point.x, Point.y, m_hwnd,
                                         NULL);
        DestroyMenu(Menu);
        if (Command >= IDM_STANDARD && Command <= IDM_PROGRAMMER)
        {
            SetMode((CALC_MODE)(Command - IDM_STANDARD));
        }
        else if (Command == IDM_ABOUT)
        {
            LoadStringW(Instance, IDS_TITLE, Text, ARRAYSIZE(Text));
            ShellAboutW(m_hwnd, Text, NULL, m_BigIcon);
        }
    }

    void ShowEditMenu(POINT Point)
    {
        HMENU Menu = CreatePopupMenu();
        WCHAR Text[64];
        UINT Command;

        if (!Menu)
            return;
        LoadStringW(Instance, IDS_COPY, Text, ARRAYSIZE(Text));
        AppendMenuW(Menu, MF_STRING, IDM_COPY, Text);
        LoadStringW(Instance, IDS_PASTE, Text, ARRAYSIZE(Text));
        AppendMenuW(Menu, MF_STRING | (IsClipboardFormatAvailable(CF_UNICODETEXT) ? 0 : MF_GRAYED), IDM_PASTE, Text);
        Command = (UINT)TrackPopupMenuEx(Menu, TPM_RETURNCMD | TPM_LEFTALIGN | TPM_TOPALIGN, Point.x, Point.y, m_hwnd,
                                         NULL);
        DestroyMenu(Menu);
        if (Command == IDM_COPY)
            CopyValue();
        else if (Command == IDM_PASTE)
            PasteValue();
    }

    void CopyValue()
    {
        WCHAR Text[CALC_TEXT_LENGTH];
        char Raw[CALC_TEXT_LENGTH];
        HGLOBAL Memory;
        SIZE_T Bytes;

        if (m_Engine.Error != CALC_OK)
        {
            DisplayText(Text, ARRAYSIZE(Text));
        }
        else
        {
            CalcDisplay(&m_Engine, Raw, sizeof(Raw));
            MultiByteToWideChar(CP_UTF8, 0, Raw, -1, Text, ARRAYSIZE(Text));
            for (WCHAR *Cursor = Text; *Cursor; Cursor++)
            {
                if (*Cursor == L'.')
                    *Cursor = m_DecimalMark[0];
            }
        }
        Bytes = (lstrlenW(Text) + 1) * sizeof(WCHAR);
        if (!OpenClipboard(m_hwnd))
            return;
        EmptyClipboard();
        Memory = GlobalAlloc(GMEM_MOVEABLE, Bytes);
        if (Memory != NULL)
        {
            CopyMemory(GlobalLock(Memory), Text, Bytes);
            GlobalUnlock(Memory);
            if (SetClipboardData(CF_UNICODETEXT, Memory) == NULL)
                GlobalFree(Memory);
        }
        CloseClipboard();
    }

    void PasteValue()
    {
        char Narrow[CALC_TEXT_LENGTH];
        const WCHAR *Text;
        HANDLE Memory;
        int Used = 0;

        if (!OpenClipboard(m_hwnd))
            return;
        Memory = GetClipboardData(CF_UNICODETEXT);
        Text = Memory != NULL ? (const WCHAR *)GlobalLock(Memory) : NULL;
        if (Text != NULL)
        {
            for (; *Text != 0 && Used < (int)sizeof(Narrow) - 1; Text++)
            {
                if (*Text == L' ' || *Text == L'\t' || *Text == L'\r' || *Text == L'\n' || *Text == 0x00A0 ||
                    (*Text == m_GroupMark[0] && *Text != m_DecimalMark[0] && m_Engine.Mode != CALC_MODE_PROGRAMMER))
                    continue;
                if (*Text > 0x7E)
                {
                    Used = 0;
                    break;
                }
                Narrow[Used++] = *Text == m_DecimalMark[0] ? '.' : (char)*Text;
            }
            Narrow[Used] = 0;
            GlobalUnlock(Memory);
            if (Used == 0 || !CalcPaste(&m_Engine, Narrow))
                MessageBeep(MB_ICONWARNING);
        }
        CloseClipboard();
        Invalidate(NULL);
    }

    CALC_KEY KeyFromCharacter(WCHAR Character) const
    {
        if (Character >= L'0' && Character <= L'9')
            return (CALC_KEY)(CALC_KEY_0 + (Character - L'0'));
        if (Character == m_DecimalMark[0] || Character == L'.' || Character == L',')
            return CALC_KEY_POINT;
        if (m_Engine.Mode == CALC_MODE_PROGRAMMER)
        {
            if (Character >= L'a' && Character <= L'f')
                return (CALC_KEY)(CALC_KEY_A + (Character - L'a'));
            if (Character >= L'A' && Character <= L'F')
                return (CALC_KEY)(CALC_KEY_A + (Character - L'A'));
        }
        switch (Character)
        {
            case L'+': return CALC_KEY_ADD;
            case L'-': return CALC_KEY_SUBTRACT;
            case L'*': return CALC_KEY_MULTIPLY;
            case L'/': return CALC_KEY_DIVIDE;
            case L'=':
            case L'\r': return CALC_KEY_EQUALS;
            case L'\b': return CALC_KEY_BACK;
            case L'%': return CALC_KEY_PERCENT;
            case L'(': return CALC_KEY_OPEN;
            case L')': return CALC_KEY_CLOSE;
            case L'&': return CALC_KEY_AND;
            case L'|': return CALC_KEY_OR;
            case L'~': return CALC_KEY_NOT;
            case L'<': return CALC_KEY_SHIFT_LEFT;
            case L'>': return CALC_KEY_SHIFT_RIGHT;
            case L'^': return m_Engine.Mode == CALC_MODE_PROGRAMMER ? CALC_KEY_XOR : CALC_KEY_POWER;
            case L'@': return CALC_KEY_SQUARE_ROOT;
            case L'!': return CALC_KEY_FACTORIAL;
            case L'#': return CALC_KEY_CUBE;
            case L'r': return CALC_KEY_RECIPROCAL;
            case L'q': return CALC_KEY_SQUARE;
            case L'p': return CALC_KEY_PI;
            case L's': return CALC_KEY_SIN;
            case L'o': return CALC_KEY_COS;
            case L't': return CALC_KEY_TAN;
            case L'l': return CALC_KEY_LOG;
            case L'n': return CALC_KEY_LN;
            case L'x': return CALC_KEY_EXPONENT;
            case L'y': return CALC_KEY_POWER;
            case L'd': return CALC_KEY_MODULO;
            default: return CALC_KEY_NONE;
        }
    }

    BOOL OnKeyDown(WPARAM VirtualKey)
    {
        if (GetKeyState(VK_CONTROL) < 0)
        {
            switch (VirtualKey)
            {
                case 'C': CopyValue(); return TRUE;
                case 'V': PasteValue(); return TRUE;
                case 'H':
                    if (m_Engine.Mode != CALC_MODE_PROGRAMMER)
                        ShowHistory(!m_HistoryOpen);
                    return TRUE;
                case 'M': PressFromKeyboard(CALC_KEY_MEMORY_STORE); return TRUE;
                case 'R': PressFromKeyboard(CALC_KEY_MEMORY_RECALL); return TRUE;
                case 'L': PressFromKeyboard(CALC_KEY_MEMORY_CLEAR); return TRUE;
                case 'P': PressFromKeyboard(CALC_KEY_MEMORY_ADD); return TRUE;
                case 'Q': PressFromKeyboard(CALC_KEY_MEMORY_SUBTRACT); return TRUE;
                default: return FALSE;
            }
        }
        switch (VirtualKey)
        {
            case VK_ESCAPE:
                if (m_HistoryOpen)
                    ShowHistory(FALSE);
                else
                    PressFromKeyboard(CALC_KEY_CLEAR);
                return TRUE;
            case VK_DELETE:
                PressFromKeyboard(m_Engine.Mode == CALC_MODE_STANDARD ? CALC_KEY_CLEAR_ENTRY : Effective(CALC_KEY_CLEAR));
                return TRUE;
            case VK_F9:
                PressFromKeyboard(CALC_KEY_SIGN);
                return TRUE;
            case VK_F5:
            case VK_F6:
            case VK_F7:
            case VK_F8:
                if (m_Engine.Mode != CALC_MODE_PROGRAMMER)
                    return FALSE;
                Press((CALC_KEY)(CALC_KEY_HEX + (VirtualKey - VK_F5)));
                return TRUE;
            default:
                return FALSE;
        }
    }

    void DrawLabel(Canvas &Surface, const KeyText &Text, const RECT &Rect, HFONT First, HFONT *Fallbacks,
                   COLORREF Ink)
    {
        HFONT Font = First;
        LPCWSTR String = Text.Text;

        if (Text.Icon)
        {
            int Size = S(16);
            RECT Glyph = {Rect.left + (Rect.right - Rect.left - Size) / 2, Rect.top + (Rect.bottom - Rect.top - Size) / 2,
                          0, 0};

            Glyph.right = Glyph.left + Size;
            Glyph.bottom = Glyph.top + Size;
            Surface.Icon(Text.Icon, Glyph, Ink);
            return;
        }
        if (!FontHasText(Surface.Dc(), Font, String))
        {
            Font = NULL;
            for (int Index = 1; Index < FALLBACK_FONTS && Font == NULL; Index++)
            {
                if (FontHasText(Surface.Dc(), Fallbacks[Index], String))
                    Font = Fallbacks[Index];
            }
            if (Font == NULL)
            {
                Font = First;
                String = Text.Plain;
            }
        }
        Surface.Text(String, Rect, Font, Ink, DT_SINGLELINE | DT_CENTER | DT_VCENTER);
    }

    void PaintControl(Canvas &Surface, int Index)
    {
        const Palette &c = m_Theme.Colors();
        const Control &Item = m_Controls[Index];
        BOOL Dark = m_Theme.IsDark(), Enabled = ControlEnabled(Index);
        BOOL Hot = Enabled && (Index == m_Hot || Index == m_Flash), Pressed = Enabled && (Index == m_Pressed || Index == m_Flash);
        COLORREF White = RGB(255, 255, 255), Fill, Ink = c.Text;
        float Radius = m_Theme.ScaleF(4.0f);
        KeyText Text;
        CALC_KEY Key;
        BOOL Checked;
        RECT Frame;

        switch (Item.Kind)
        {
            case KindNavigation:
            case KindHistory:
            {
                RECT Glyph = Item.Rect;
                int Size = S(16);

                if (Hot || Index == m_Pressed || (Item.Kind == KindHistory && m_HistoryOpen))
                    Surface.FillRound(Item.Rect, Radius, Index == m_Pressed ? c.SubtlePressed : c.SubtleHover);
                Glyph.left += (Glyph.right - Glyph.left - Size) / 2;
                Glyph.top += (Glyph.bottom - Glyph.top - Size) / 2;
                Glyph.right = Glyph.left + Size;
                Glyph.bottom = Glyph.top + Size;
                Surface.Icon(Item.Kind == KindNavigation ? L"navigation_16_regular" : L"history_20_regular", Glyph, c.Glyph);
                return;
            }
            case KindBase:
                PaintBase(Surface, Index, Hot);
                return;
            default:
                break;
        }
        Key = Effective((CALC_KEY)Item.Value);
        Checked = KeyChecked(Key);
        Label(Key, &Text);
        if (Item.Kind == KindSubtle)
        {
            if (Pressed)
                Surface.FillRound(Item.Rect, Radius, c.SubtlePressed);
            else if (Hot)
                Surface.FillRound(Item.Rect, Radius, c.SubtleHover);
            Ink = !Enabled ? c.TextDisabled : Checked ? c.Accent : Pressed ? c.TextSecondary : c.Text;
            DrawLabel(Surface, Text, Item.Rect, m_SmallFonts[0], m_SmallFonts, Ink);
            return;
        }
        if (Item.Kind == KindEquals || Checked)
        {
            Fill = c.Accent;
            Ink = c.AccentText;
            if (Pressed)
            {
                Fill = Mix(Fill, c.Window, 0x40);
                Ink = Mix(Ink, Fill, 0x50);
            }
            else if (Hot)
            {
                Fill = Mix(Fill, c.Window, 0x1A);
            }
        }
        else
        {
            if (Item.Kind == KindDigit)
                Fill = Dark ? Mix(c.Window, White, 0x15) : Mix(c.Window, White, 0xB3);
            else
                Fill = Dark ? Mix(c.Window, White, 0x0D) : Mix(c.Window, RGB(249, 249, 249), 0x80);
            if (Pressed)
            {
                Fill = Mix(Fill, Dark ? RGB(0, 0, 0) : White, 0x30);
                Ink = c.TextSecondary;
            }
            else if (Hot)
            {
                Fill = Mix(Fill, Dark ? White : RGB(0, 0, 0), 0x08);
            }
            if (!Enabled)
                Ink = c.TextDisabled;
        }
        Surface.FillRound(Item.Rect, Radius, Fill);
        if (Item.Kind != KindEquals && !Checked)
        {
            Frame = Item.Rect;
            Frame.right--;
            Frame.bottom--;
            Surface.FrameRound(Frame, Radius, c.ControlStroke);
        }
        DrawLabel(Surface, Text, Item.Rect, Item.Kind == KindDigit && Key != CALC_KEY_SIGN ? m_DigitFont : m_KeyFonts[0],
                  m_KeyFonts, Ink);
    }

    void PaintBase(Canvas &Surface, int Index, BOOL Hot)
    {
        static const int Bases[4] = {16, 10, 8, 2};
        static const LPCWSTR Names[4] = {L"HEX", L"DEC", L"OCT", L"BIN"};
        const Palette &c = m_Theme.Colors();
        const Control &Item = m_Controls[Index];
        BOOL Selected = m_Engine.Base == Bases[Item.Value];
        RECT Name = Item.Rect, Value = Item.Rect, Bar;
        WCHAR Text[CALC_TEXT_LENGTH];
        char Narrow[CALC_TEXT_LENGTH];

        if (Hot)
            Surface.FillRound(Item.Rect, m_Theme.ScaleF(4.0f), Index == m_Pressed ? c.SubtlePressed : c.SubtleHover);
        if (Selected)
        {
            SetRect(&Bar, Item.Rect.left + S(2), Item.Rect.top + S(6), Item.Rect.left + S(5), Item.Rect.bottom - S(6));
            Surface.FillRound(Bar, m_Theme.ScaleF(1.5f), c.Accent);
        }
        Name.left += S(10);
        Surface.Text(Names[Item.Value], Name, m_Theme.Font(Selected ? TextStyle::BodyStrong : TextStyle::Body),
                     Selected ? c.Text : c.TextSecondary, DT_SINGLELINE | DT_VCENTER | DT_LEFT);
        Value.left += S(48);
        Value.right -= S(4);
        if (m_Engine.Error == CALC_OK)
        {
            CalcDisplayBase(&m_Engine, Bases[Item.Value], Narrow, sizeof(Narrow));
            FormatText(Narrow, Bases[Item.Value], Text, ARRAYSIZE(Text));
            Surface.Text(Text, Value, m_Theme.Font(TextStyle::Body), Selected ? c.Text : c.TextSecondary,
                         DT_SINGLELINE | DT_VCENTER | DT_LEFT | DT_END_ELLIPSIS);
        }
    }

    void PaintDisplay(Canvas &Surface)
    {
        const Palette &c = m_Theme.Colors();
        WCHAR Text[CALC_EXPRESSION_LENGTH];
        char Narrow[CALC_EXPRESSION_LENGTH];
        RECT Line = m_Display;
        HFONT Font = m_ValueFonts[3];

        Line.left = m_ValueLeft;
        Line.bottom = Line.top + S(24);
        CalcExpression(&m_Engine, Narrow, sizeof(Narrow));
        MultiByteToWideChar(CP_UTF8, 0, Narrow, -1, Text, ARRAYSIZE(Text));
        Surface.Text(Text, Line, m_Theme.Font(TextStyle::Body), c.TextSecondary, DT_SINGLELINE | DT_VCENTER | DT_RIGHT);
        Line.top = Line.bottom;
        Line.bottom = m_Display.bottom;
        DisplayText(Text, ARRAYSIZE(Text));
        for (int Index = 0; Index < 4; Index++)
        {
            if (Surface.MeasureText(Text, m_ValueFonts[Index]).cx <= Line.right - Line.left)
            {
                Font = m_ValueFonts[Index];
                break;
            }
        }
        Surface.Text(Text, Line, Font, c.Text, DT_SINGLELINE | DT_VCENTER | DT_RIGHT);
    }

    void PaintHistory(Canvas &Surface)
    {
        const Palette &c = m_Theme.Colors();
        float Radius = m_Theme.ScaleF(8.0f);
        RECT Frame = m_Panel, Empty;
        BOOL Any = FALSE;
        WCHAR Text[64];

        Surface.FillRound(m_Panel, Radius, m_Theme.IsDark() ? RGB(0x2C, 0x2C, 0x2C) : RGB(0xF9, 0xF9, 0xF9));
        Frame.right--;
        Frame.bottom--;
        Surface.FrameRound(Frame, Radius, c.CardBorder);
        for (int Index = 0; Index < m_ControlCount; Index++)
        {
            const Control &Item = m_Controls[Index];

            if (Item.Kind == KindHistoryItem)
            {
                const HistoryEntry &Entry = History[Item.Value];
                RECT Line = Item.Rect;

                Any = TRUE;
                if (Index == m_Hot)
                    Surface.FillRound(Item.Rect, m_Theme.ScaleF(4.0f), Index == m_Pressed ? c.SubtlePressed : c.SubtleHover);
                Line.right -= S(12);
                Line.bottom = Line.top + S(28);
                Surface.Text(Entry.Expression, Line, m_Theme.Font(TextStyle::Body), c.TextSecondary,
                             DT_SINGLELINE | DT_BOTTOM | DT_RIGHT);
                Line.top = Line.bottom;
                Line.bottom = Item.Rect.bottom - S(4);
                Surface.Text(Entry.Result, Line, m_Theme.Font(TextStyle::Subtitle), c.Text,
                             DT_SINGLELINE | DT_VCENTER | DT_RIGHT);
            }
            else if (Item.Kind == KindHistoryClear)
            {
                RECT Glyph = Item.Rect;
                int Size = S(16);

                if (Index == m_Hot)
                    Surface.FillRound(Item.Rect, m_Theme.ScaleF(4.0f), Index == m_Pressed ? c.SubtlePressed : c.SubtleHover);
                Glyph.left += (Glyph.right - Glyph.left - Size) / 2;
                Glyph.top += (Glyph.bottom - Glyph.top - Size) / 2;
                Glyph.right = Glyph.left + Size;
                Glyph.bottom = Glyph.top + Size;
                Surface.Icon(L"delete_20_regular", Glyph, c.Glyph);
            }
        }
        if (!Any)
        {
            SetRect(&Empty, m_Panel.left + S(16), m_Panel.top + S(12), m_Panel.right - S(16), m_Panel.top + S(40));
            LoadStringW(Instance, IDS_NO_HISTORY, Text, ARRAYSIZE(Text));
            Surface.Text(Text, Empty, m_Theme.Font(TextStyle::BodyStrong), c.Text, DT_SINGLELINE | DT_VCENTER | DT_LEFT);
        }
    }

    CALC_ENGINE m_Engine;
    Control m_Controls[MAX_CONTROLS];
    int m_ControlCount;
    int m_Hot;
    int m_Pressed;
    int m_Flash;
    BOOL m_Tracking;
    BOOL m_HistoryOpen;
    int m_HistoryScroll;
    int m_PanelTop;
    int m_ValueLeft;
    RECT m_Header;
    RECT m_Display;
    RECT m_Panel;
    WCHAR m_DecimalMark[4];
    WCHAR m_GroupMark[4];
    HICON m_SmallIcon;
    HICON m_BigIcon;
    HFONT m_DigitFont;
    HFONT m_SmallFonts[FALLBACK_FONTS];
    HFONT m_KeyFonts[FALLBACK_FONTS];
    HFONT m_ValueFonts[4];
};

int WINAPI
wWinMain(HINSTANCE Module, HINSTANCE Previous, PWSTR CommandLine, int Show)
{
    static CalculatorWindow Window;
    WINDOWPLACEMENT Placement = {sizeof(Placement)};
    BOOL HavePlacement;
    WCHAR Title[64];
    MSG Message;

    UNREFERENCED_PARAMETER(Previous);
    UNREFERENCED_PARAMETER(CommandLine);
    Instance = Module;
    if (!Startup())
        return 1;
    Window.LoadSettings(&Placement, &HavePlacement);
    LoadStringW(Module, IDS_TITLE, Title, ARRAYSIZE(Title));
    if (!Window.Create(CALC_CLASS, Title, LoadIconW(Module, MAKEINTRESOURCEW(IDI_CALC)),
                       ViewSize.cx, ViewSize.cy, SW_HIDE))
    {
        Shutdown();
        return 1;
    }
    if (HavePlacement)
    {
        Placement.showCmd = Placement.showCmd == SW_SHOWMAXIMIZED ? SW_SHOWMAXIMIZED : Show;
        if (Placement.showCmd == SW_SHOWMINIMIZED || Placement.showCmd == SW_MINIMIZE)
            Placement.showCmd = SW_SHOWNORMAL;
        SetWindowPlacement(Window.Window(), &Placement);
    }
    else
    {
        Window.FitView();
        ShowWindow(Window.Window(), Show);
    }
    while (GetMessageW(&Message, NULL, 0, 0) > 0)
    {
        TranslateMessage(&Message);
        DispatchMessageW(&Message);
    }
    Shutdown();
    return (int)Message.wParam;
}
