/*
 * PROJECT:     LiberNT Calculator
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Calculator window: keys, display, keyboard, menu and clipboard
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <windows.h>
#include <commctrl.h>
#include <shellapi.h>
#include <uxtheme.h>

#include "calcengine.h"
#include "calcres.h"

#define CALC_CLASS L"LiberNTCalculator"
#define CALC_SETTINGS_KEY L"Software\\LiberNT\\Calculator"
#define MAX_COLUMNS 5
#define MAX_ROWS 9
#define MEMORY_KEYS 5

typedef struct _CALC_LAYOUT
{
    int Columns;
    int Rows;
    int DisplayHeight;
    int Width;
    int Height;
    CALC_KEY Keys[MAX_ROWS][MAX_COLUMNS];
} CALC_LAYOUT;

typedef struct _CALC_LABEL
{
    CALC_KEY Key;
    const WCHAR *Text;
    const WCHAR *Plain;
} CALC_LABEL;

static const CALC_KEY MemoryRow[MEMORY_KEYS] =
{
    CALC_KEY_MEMORY_CLEAR, CALC_KEY_MEMORY_RECALL, CALC_KEY_MEMORY_ADD, CALC_KEY_MEMORY_SUBTRACT,
    CALC_KEY_MEMORY_STORE
};

static const CALC_LAYOUT Layouts[3] =
{
    {
        4, 6, 72, 300, 420,
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
        5, 9, 72, 400, 520,
        {
            {CALC_KEY_ANGLE, CALC_KEY_FORMAT, CALC_KEY_HYPERBOLIC, CALC_KEY_CLEAR_ENTRY, CALC_KEY_CLEAR},
            {CALC_KEY_INVERSE, CALC_KEY_PI, CALC_KEY_EULER, CALC_KEY_EXPONENT, CALC_KEY_BACK},
            {CALC_KEY_SIN, CALC_KEY_COS, CALC_KEY_TAN, CALC_KEY_FACTORIAL, CALC_KEY_MODULO},
            {CALC_KEY_SQUARE, CALC_KEY_RECIPROCAL, CALC_KEY_ABSOLUTE, CALC_KEY_OPEN, CALC_KEY_CLOSE},
            {CALC_KEY_SQUARE_ROOT, CALC_KEY_POWER, CALC_KEY_TEN_POWER, CALC_KEY_LOG, CALC_KEY_DIVIDE},
            {CALC_KEY_LN, CALC_KEY_7, CALC_KEY_8, CALC_KEY_9, CALC_KEY_MULTIPLY},
            {CALC_KEY_E_POWER, CALC_KEY_4, CALC_KEY_5, CALC_KEY_6, CALC_KEY_SUBTRACT},
            {CALC_KEY_ROOT, CALC_KEY_1, CALC_KEY_2, CALC_KEY_3, CALC_KEY_ADD},
            {CALC_KEY_CUBE, CALC_KEY_SIGN, CALC_KEY_0, CALC_KEY_POINT, CALC_KEY_EQUALS}
        }
    },
    {
        5, 8, 148, 400, 560,
        {
            {CALC_KEY_HEX, CALC_KEY_DEC, CALC_KEY_OCT, CALC_KEY_BIN, CALC_KEY_WORD},
            {CALC_KEY_AND, CALC_KEY_OR, CALC_KEY_XOR, CALC_KEY_NOT, CALC_KEY_CLEAR_ENTRY},
            {CALC_KEY_A, CALC_KEY_SHIFT_LEFT, CALC_KEY_SHIFT_RIGHT, CALC_KEY_CLEAR, CALC_KEY_BACK},
            {CALC_KEY_B, CALC_KEY_OPEN, CALC_KEY_CLOSE, CALC_KEY_MODULO, CALC_KEY_DIVIDE},
            {CALC_KEY_C, CALC_KEY_7, CALC_KEY_8, CALC_KEY_9, CALC_KEY_MULTIPLY},
            {CALC_KEY_D, CALC_KEY_4, CALC_KEY_5, CALC_KEY_6, CALC_KEY_SUBTRACT},
            {CALC_KEY_E, CALC_KEY_1, CALC_KEY_2, CALC_KEY_3, CALC_KEY_ADD},
            {CALC_KEY_F, CALC_KEY_SIGN, CALC_KEY_0, CALC_KEY_POINT, CALC_KEY_EQUALS}
        }
    }
};

static const CALC_LABEL Labels[] =
{
    {CALC_KEY_EXPONENT, L"exp", NULL},
    {CALC_KEY_SIGN, L"\x00B1", NULL},
    {CALC_KEY_BACK, L"\x2190", L"Bksp"},
    {CALC_KEY_CLEAR_ENTRY, L"CE", NULL},
    {CALC_KEY_CLEAR, L"C", NULL},
    {CALC_KEY_ADD, L"+", NULL},
    {CALC_KEY_SUBTRACT, L"\x2212", L"-"},
    {CALC_KEY_MULTIPLY, L"\x00D7", NULL},
    {CALC_KEY_DIVIDE, L"\x00F7", NULL},
    {CALC_KEY_MODULO, L"mod", NULL},
    {CALC_KEY_POWER, L"x^y", NULL},
    {CALC_KEY_ROOT, L"y\x221Ax", L"yroot"},
    {CALC_KEY_AND, L"AND", NULL},
    {CALC_KEY_OR, L"OR", NULL},
    {CALC_KEY_XOR, L"XOR", NULL},
    {CALC_KEY_SHIFT_LEFT, L"Lsh", NULL},
    {CALC_KEY_SHIFT_RIGHT, L"Rsh", NULL},
    {CALC_KEY_EQUALS, L"=", NULL},
    {CALC_KEY_PERCENT, L"%", NULL},
    {CALC_KEY_OPEN, L"(", NULL},
    {CALC_KEY_CLOSE, L")", NULL},
    {CALC_KEY_RECIPROCAL, L"1/x", NULL},
    {CALC_KEY_SQUARE, L"x\x00B2", NULL},
    {CALC_KEY_CUBE, L"x\x00B3", NULL},
    {CALC_KEY_SQUARE_ROOT, L"\x221Ax", L"sqrt"},
    {CALC_KEY_ABSOLUTE, L"|x|", NULL},
    {CALC_KEY_FACTORIAL, L"n!", NULL},
    {CALC_KEY_TEN_POWER, L"10^x", NULL},
    {CALC_KEY_E_POWER, L"e^x", NULL},
    {CALC_KEY_LOG, L"log", NULL},
    {CALC_KEY_LN, L"ln", NULL},
    {CALC_KEY_NOT, L"NOT", NULL},
    {CALC_KEY_PI, L"\x03C0", L"pi"},
    {CALC_KEY_EULER, L"e", NULL},
    {CALC_KEY_MEMORY_CLEAR, L"MC", NULL},
    {CALC_KEY_MEMORY_RECALL, L"MR", NULL},
    {CALC_KEY_MEMORY_ADD, L"M+", NULL},
    {CALC_KEY_MEMORY_SUBTRACT, L"M\x2212", L"M-"},
    {CALC_KEY_MEMORY_STORE, L"MS", NULL},
    {CALC_KEY_INVERSE, L"2nd", NULL},
    {CALC_KEY_HYPERBOLIC, L"hyp", NULL},
    {CALC_KEY_FORMAT, L"F-E", NULL},
    {CALC_KEY_HEX, L"HEX", NULL},
    {CALC_KEY_DEC, L"DEC", NULL},
    {CALC_KEY_OCT, L"OCT", NULL},
    {CALC_KEY_BIN, L"BIN", NULL}
};

static HINSTANCE Instance;
static HWND MainWindow;
static HWND KeyWindows[CALC_KEY_COUNT];
static CALC_ENGINE Engine;
static HFONT KeyFont, ValueFont, SmallFont;
static UINT Dpi = USER_DEFAULT_SCREEN_DPI;
static BOOL Grouping = TRUE;
static WCHAR DecimalMark[4] = L".";
static WCHAR GroupMark[4] = L",";
static RECT DisplayRect;

static int
Scale(int Value)
{
    return MulDiv(Value, Dpi, USER_DEFAULT_SCREEN_DPI);
}

static BOOL
IsToggleKey(CALC_KEY Key)
{
    return Key == CALC_KEY_INVERSE || Key == CALC_KEY_HYPERBOLIC || Key == CALC_KEY_FORMAT ||
           (Key >= CALC_KEY_HEX && Key <= CALC_KEY_BIN);
}

static BOOL
FontHasText(HDC Context, const WCHAR *Text)
{
    WORD Glyphs[8];
    int Length = lstrlenW(Text), Index;

    if (Length > (int)ARRAYSIZE(Glyphs))
        return FALSE;
    if (GetGlyphIndicesW(Context, Text, Length, Glyphs, GGI_MARK_NONEXISTING_GLYPHS) == GDI_ERROR)
        return FALSE;
    for (Index = 0; Index < Length; Index++)
    {
        if (Glyphs[Index] == 0xFFFF)
            return FALSE;
    }
    return TRUE;
}

static void
KeyLabel(HDC Context, CALC_KEY Key, WCHAR *Buffer, int Size)
{
    static const WCHAR *Angles[3] = {L"DEG", L"RAD", L"GRAD"};
    const WCHAR *Trigonometry[3] = {L"sin", L"cos", L"tan"};
    UINT Index;

    Buffer[0] = 0;
    if (Key >= CALC_KEY_0 && Key <= CALC_KEY_F)
    {
        Buffer[0] = L"0123456789ABCDEF"[Key - CALC_KEY_0];
        Buffer[1] = 0;
        return;
    }
    switch (Key)
    {
        case CALC_KEY_POINT:
            lstrcpynW(Buffer, DecimalMark, Size);
            return;
        case CALC_KEY_ANGLE:
            lstrcpynW(Buffer, Angles[Engine.Angle], Size);
            return;
        case CALC_KEY_WORD:
            lstrcpynW(Buffer,
                      Engine.WordBits == 64 ? L"QWORD" :
                      Engine.WordBits == 32 ? L"DWORD" : Engine.WordBits == 16 ? L"WORD" : L"BYTE",
                      Size);
            return;
        case CALC_KEY_SIN:
        case CALC_KEY_COS:
        case CALC_KEY_TAN:
            lstrcpynW(Buffer, Engine.Inverse ? L"a" : L"", Size);
            lstrcatW(Buffer, Trigonometry[Key - CALC_KEY_SIN]);
            if (Engine.Hyperbolic)
                lstrcatW(Buffer, L"h");
            return;
        default:
            break;
    }
    for (Index = 0; Index < ARRAYSIZE(Labels); Index++)
    {
        if (Labels[Index].Key != Key)
            continue;
        if (Labels[Index].Plain != NULL && !FontHasText(Context, Labels[Index].Text))
            lstrcpynW(Buffer, Labels[Index].Plain, Size);
        else
            lstrcpynW(Buffer, Labels[Index].Text, Size);
        return;
    }
}

static void
LoadMarks(void)
{
    if (GetLocaleInfoW(LOCALE_USER_DEFAULT, LOCALE_SDECIMAL, DecimalMark, ARRAYSIZE(DecimalMark)) == 0)
        lstrcpyW(DecimalMark, L".");
    if (GetLocaleInfoW(LOCALE_USER_DEFAULT, LOCALE_STHOUSAND, GroupMark, ARRAYSIZE(GroupMark)) == 0)
        lstrcpyW(GroupMark, L",");
}

static void
LoadSettings(void)
{
    DWORD Mode = CALC_MODE_STANDARD, Group = TRUE, Size = sizeof(DWORD);
    HKEY Key;

    if (RegOpenKeyExW(HKEY_CURRENT_USER, CALC_SETTINGS_KEY, 0, KEY_READ, &Key) != ERROR_SUCCESS)
        return;
    if (RegQueryValueExW(Key, L"Mode", NULL, NULL, (BYTE *)&Mode, &Size) == ERROR_SUCCESS &&
        Mode <= CALC_MODE_PROGRAMMER)
        CalcSetMode(&Engine, (CALC_MODE)Mode);
    Size = sizeof(DWORD);
    if (RegQueryValueExW(Key, L"DigitGrouping", NULL, NULL, (BYTE *)&Group, &Size) == ERROR_SUCCESS)
        Grouping = Group != 0;
    RegCloseKey(Key);
}

static void
SaveSettings(void)
{
    DWORD Mode = Engine.Mode, Group = Grouping;
    HKEY Key;

    if (RegCreateKeyExW(HKEY_CURRENT_USER, CALC_SETTINGS_KEY, 0, NULL, 0, KEY_WRITE, NULL, &Key, NULL) !=
        ERROR_SUCCESS)
        return;
    RegSetValueExW(Key, L"Mode", 0, REG_DWORD, (const BYTE *)&Mode, sizeof(Mode));
    RegSetValueExW(Key, L"DigitGrouping", 0, REG_DWORD, (const BYTE *)&Group, sizeof(Group));
    RegCloseKey(Key);
}

static void
GroupDigits(const WCHAR *Digits, int Count, int Width, const WCHAR *Mark, WCHAR *Output, int Size)
{
    int Used = lstrlenW(Output), Index;

    for (Index = 0; Index < Count && Used + 5 < Size; Index++)
    {
        if (Index != 0 && (Count - Index) % Width == 0)
        {
            lstrcpyW(Output + Used, Mark);
            Used += lstrlenW(Mark);
        }
        Output[Used++] = Digits[Index];
    }
    Output[Used] = 0;
}

static void
FormatText(const char *Text, int Base, BOOL Group, WCHAR *Output, int Size)
{
    WCHAR Wide[CALC_TEXT_LENGTH];
    const WCHAR *Cursor = Wide;
    int Whole = 0, Used;

    MultiByteToWideChar(CP_UTF8, 0, Text, -1, Wide, ARRAYSIZE(Wide));
    Output[0] = 0;
    if (*Cursor == L'-')
    {
        lstrcpyW(Output, L"-");
        Cursor++;
    }
    while ((Cursor[Whole] >= L'0' && Cursor[Whole] <= L'9') || (Cursor[Whole] >= L'A' && Cursor[Whole] <= L'F'))
        Whole++;
    if (!Group)
        GroupDigits(Cursor, Whole, Whole + 1, L"", Output, Size);
    else if (Base == 10)
        GroupDigits(Cursor, Whole, 3, GroupMark, Output, Size);
    else
        GroupDigits(Cursor, Whole, Base == 8 ? 3 : 4, L" ", Output, Size);
    Cursor += Whole;
    Used = lstrlenW(Output);
    for (; *Cursor != 0 && Used + 5 < Size; Cursor++)
    {
        if (*Cursor == L'.')
        {
            lstrcpyW(Output + Used, DecimalMark);
            Used += lstrlenW(DecimalMark);
        }
        else
        {
            Output[Used++] = *Cursor;
        }
    }
    Output[Used] = 0;
}

static void
DisplayText(BOOL Group, WCHAR *Output, int Size)
{
    static const UINT Messages[] = {0, IDS_DIVIDE_BY_ZERO, IDS_INVALID_INPUT, IDS_OVERFLOW, IDS_UNDEFINED};
    char Text[CALC_TEXT_LENGTH];

    if (Engine.Error != CALC_OK)
    {
        LoadStringW(Instance, Messages[Engine.Error], Output, Size);
        return;
    }
    CalcDisplay(&Engine, Text, sizeof(Text));
    FormatText(Text, Engine.Mode == CALC_MODE_PROGRAMMER ? Engine.Base : 10, Group, Output, Size);
}

static void
CreateFonts(void)
{
    NONCLIENTMETRICSW Metrics;
    LOGFONTW Font;

    if (KeyFont != NULL)
    {
        DeleteObject(KeyFont);
        DeleteObject(ValueFont);
        DeleteObject(SmallFont);
    }
    Metrics.cbSize = sizeof(Metrics);
    if (!SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(Metrics), &Metrics, 0))
    {
        ZeroMemory(&Metrics, sizeof(Metrics));
        lstrcpyW(Metrics.lfMessageFont.lfFaceName, L"MS Shell Dlg");
    }
    Font = Metrics.lfMessageFont;
    Font.lfHeight = -Scale(12);
    SmallFont = CreateFontIndirectW(&Font);
    Font.lfHeight = -Scale(15);
    KeyFont = CreateFontIndirectW(&Font);
    Font.lfHeight = -Scale(30);
    Font.lfWeight = FW_SEMIBOLD;
    ValueFont = CreateFontIndirectW(&Font);
}

static void
UpdateKeys(void)
{
    WCHAR Text[16], Current[16];
    HDC Context = GetDC(MainWindow);
    HFONT Previous = SelectObject(Context, KeyFont);
    int Key;

    for (Key = CALC_KEY_NONE + 1; Key < CALC_KEY_COUNT; Key++)
    {
        BOOL Checked;

        if (KeyWindows[Key] == NULL)
            continue;
        KeyLabel(Context, (CALC_KEY)Key, Text, ARRAYSIZE(Text));
        Current[0] = 0;
        GetWindowTextW(KeyWindows[Key], Current, ARRAYSIZE(Current));
        if (lstrcmpW(Current, Text) != 0)
            SetWindowTextW(KeyWindows[Key], Text);
        EnableWindow(KeyWindows[Key], CalcKeyEnabled(&Engine, (CALC_KEY)Key));
        if (!IsToggleKey((CALC_KEY)Key))
            continue;
        switch (Key)
        {
            case CALC_KEY_INVERSE:
                Checked = Engine.Inverse;
                break;
            case CALC_KEY_HYPERBOLIC:
                Checked = Engine.Hyperbolic;
                break;
            case CALC_KEY_FORMAT:
                Checked = Engine.Exponential;
                break;
            case CALC_KEY_HEX:
                Checked = Engine.Base == 16;
                break;
            case CALC_KEY_DEC:
                Checked = Engine.Base == 10;
                break;
            case CALC_KEY_OCT:
                Checked = Engine.Base == 8;
                break;
            default:
                Checked = Engine.Base == 2;
                break;
        }
        SendMessageW(KeyWindows[Key], BM_SETCHECK, Checked ? BST_CHECKED : BST_UNCHECKED, 0);
    }
    SelectObject(Context, Previous);
    ReleaseDC(MainWindow, Context);
    InvalidateRect(MainWindow, &DisplayRect, FALSE);
}

static void
PlaceKeys(void)
{
    const CALC_LAYOUT *Layout = &Layouts[Engine.Mode];
    BOOL Shown[CALC_KEY_COUNT] = {FALSE};
    RECT Client;
    int Margin = Scale(8), Gap = Scale(4);
    int Width, Height, Top, CellWidth, CellHeight, MemoryHeight, Row, Column, Key;

    GetClientRect(MainWindow, &Client);
    Width = Client.right - 2 * Margin;
    SetRect(&DisplayRect, Margin, Margin, Client.right - Margin, Margin + Scale(Layout->DisplayHeight));
    Top = DisplayRect.bottom + Gap;
    Height = Client.bottom - Margin - Top;
    CellHeight = (Height * 4 - Gap * 4 * Layout->Rows) / (Layout->Rows * 4 + 3);
    MemoryHeight = CellHeight * 3 / 4;
    CellWidth = (Width - Gap * (MEMORY_KEYS - 1)) / MEMORY_KEYS;
    for (Column = 0; Column < MEMORY_KEYS; Column++)
    {
        Key = MemoryRow[Column];
        Shown[Key] = TRUE;
        MoveWindow(KeyWindows[Key], Margin + Column * (CellWidth + Gap), Top, CellWidth, MemoryHeight, FALSE);
    }
    Top += MemoryHeight + Gap;
    CellWidth = (Width - Gap * (Layout->Columns - 1)) / Layout->Columns;
    for (Row = 0; Row < Layout->Rows; Row++)
    {
        for (Column = 0; Column < Layout->Columns; Column++)
        {
            Key = Layout->Keys[Row][Column];
            Shown[Key] = TRUE;
            MoveWindow(KeyWindows[Key], Margin + Column * (CellWidth + Gap), Top + Row * (CellHeight + Gap),
                       CellWidth, CellHeight, FALSE);
        }
    }
    for (Key = CALC_KEY_NONE + 1; Key < CALC_KEY_COUNT; Key++)
    {
        if (KeyWindows[Key] != NULL)
            ShowWindow(KeyWindows[Key], Shown[Key] ? SW_SHOWNA : SW_HIDE);
    }
    InvalidateRect(MainWindow, NULL, TRUE);
}

static void
CreateKeys(void)
{
    int Mode, Row, Column, Key;

    for (Mode = 0; Mode < 3; Mode++)
    {
        for (Row = 0; Row < Layouts[Mode].Rows + 1; Row++)
        {
            int Count = Row == Layouts[Mode].Rows ? MEMORY_KEYS : Layouts[Mode].Columns;

            for (Column = 0; Column < Count; Column++)
            {
                DWORD Style = WS_CHILD | BS_PUSHBUTTON;

                Key = Row == Layouts[Mode].Rows ? MemoryRow[Column] : Layouts[Mode].Keys[Row][Column];
                if (KeyWindows[Key] != NULL)
                    continue;
                if (IsToggleKey((CALC_KEY)Key))
                    Style = WS_CHILD | BS_CHECKBOX | BS_PUSHLIKE;
                KeyWindows[Key] = CreateWindowExW(0, WC_BUTTONW, L"", Style, 0, 0, 0, 0, MainWindow,
                                                  (HMENU)(INT_PTR)(IDC_KEY_FIRST + Key), Instance, NULL);
            }
        }
    }
}

static void
ApplyFonts(void)
{
    int Key;

    CreateFonts();
    for (Key = CALC_KEY_NONE + 1; Key < CALC_KEY_COUNT; Key++)
    {
        if (KeyWindows[Key] != NULL)
            SendMessageW(KeyWindows[Key], WM_SETFONT, (WPARAM)KeyFont, FALSE);
    }
}

static void
UpdateMenu(void)
{
    HMENU Menu = GetMenu(MainWindow);

    CheckMenuRadioItem(Menu, IDM_STANDARD, IDM_PROGRAMMER, IDM_STANDARD + Engine.Mode, MF_BYCOMMAND);
    CheckMenuItem(Menu, IDM_GROUPING, MF_BYCOMMAND | (Grouping ? MF_CHECKED : MF_UNCHECKED));
}

static void
SizeForMode(void)
{
    const CALC_LAYOUT *Layout = &Layouts[Engine.Mode];
    RECT Frame = {0, 0, Scale(Layout->Width), Scale(Layout->Height)};

    AdjustWindowRectEx(&Frame, (DWORD)GetWindowLongPtrW(MainWindow, GWL_STYLE), TRUE,
                       (DWORD)GetWindowLongPtrW(MainWindow, GWL_EXSTYLE));
    SetWindowPos(MainWindow, NULL, 0, 0, Frame.right - Frame.left, Frame.bottom - Frame.top,
                 SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    UpdateMenu();
    PlaceKeys();
    UpdateKeys();
}

static void
DrawFitted(HDC Context, const WCHAR *Text, RECT *Area)
{
    HFONT Fonts[3] = {ValueFont, KeyFont, SmallFont};
    HFONT Previous = SelectObject(Context, ValueFont);
    int Index;

    for (Index = 0; Index < 3; Index++)
    {
        SIZE Extent;

        SelectObject(Context, Fonts[Index]);
        if (!GetTextExtentPoint32W(Context, Text, lstrlenW(Text), &Extent) || Extent.cx <= Area->right - Area->left)
            break;
    }
    DrawTextW(Context, Text, -1, Area, DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    SelectObject(Context, Previous);
}

static void
PaintDisplay(HDC Target)
{
    static const int Bases[4] = {16, 10, 8, 2};
    static const WCHAR *BaseNames[4] = {L"HEX", L"DEC", L"OCT", L"BIN"};
    WCHAR Text[CALC_EXPRESSION_LENGTH];
    char Narrow[CALC_EXPRESSION_LENGTH];
    int Width = DisplayRect.right - DisplayRect.left, Height = DisplayRect.bottom - DisplayRect.top;
    HDC Context = CreateCompatibleDC(Target);
    HBITMAP Bitmap = CreateCompatibleBitmap(Target, Width, Height);
    HBITMAP PreviousBitmap = SelectObject(Context, Bitmap);
    HFONT PreviousFont = SelectObject(Context, SmallFont);
    RECT Area = {0, 0, Width, Height}, Line;
    int Pad = Scale(8), LineHeight = Scale(22), Index;

    FillRect(Context, &Area, GetSysColorBrush(COLOR_WINDOW));
    if (!IsAppThemed())
        DrawEdge(Context, &Area, EDGE_SUNKEN, BF_RECT);
    SetBkMode(Context, TRANSPARENT);
    SetTextColor(Context, GetSysColor(COLOR_GRAYTEXT));
    SetRect(&Line, Pad, Scale(4), Width - Pad, Scale(4) + LineHeight);
    if (Engine.HasMemory)
        DrawTextW(Context, L"M", 1, &Line, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    Line.left += Scale(20);
    CalcExpression(&Engine, Narrow, sizeof(Narrow));
    MultiByteToWideChar(CP_UTF8, 0, Narrow, -1, Text, ARRAYSIZE(Text));
    DrawTextW(Context, Text, -1, &Line, DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    SetTextColor(Context, GetSysColor(COLOR_WINDOWTEXT));
    SetRect(&Line, Pad, Scale(4) + LineHeight, Width - Pad, Scale(68));
    DisplayText(Grouping, Text, ARRAYSIZE(Text));
    DrawFitted(Context, Text, &Line);
    if (Engine.Mode == CALC_MODE_PROGRAMMER && Engine.Error == CALC_OK)
    {
        SelectObject(Context, SmallFont);
        for (Index = 0; Index < 4; Index++)
        {
            SetRect(&Line, Pad, Scale(70) + Index * Scale(18), Width - Pad, Scale(70) + (Index + 1) * Scale(18));
            SetTextColor(Context, GetSysColor(Engine.Base == Bases[Index] ? COLOR_WINDOWTEXT : COLOR_GRAYTEXT));
            DrawTextW(Context, BaseNames[Index], -1, &Line, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
            Line.left += Scale(40);
            CalcDisplayBase(&Engine, Bases[Index], Narrow, sizeof(Narrow));
            FormatText(Narrow, Bases[Index], TRUE, Text, ARRAYSIZE(Text));
            DrawTextW(Context, Text, -1, &Line, DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        }
    }
    BitBlt(Target, DisplayRect.left, DisplayRect.top, Width, Height, Context, 0, 0, SRCCOPY);
    SelectObject(Context, PreviousFont);
    SelectObject(Context, PreviousBitmap);
    DeleteObject(Bitmap);
    DeleteDC(Context);
}

static void
Press(CALC_KEY Key)
{
    if (Key == CALC_KEY_PERCENT && Engine.Mode != CALC_MODE_STANDARD)
        Key = CALC_KEY_MODULO;
    CalcPress(&Engine, Key);
    UpdateKeys();
}

static void
CopyValue(void)
{
    WCHAR Text[CALC_TEXT_LENGTH];
    HGLOBAL Memory;
    SIZE_T Bytes;

    DisplayText(FALSE, Text, ARRAYSIZE(Text));
    Bytes = (lstrlenW(Text) + 1) * sizeof(WCHAR);
    if (!OpenClipboard(MainWindow))
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

static void
PasteValue(void)
{
    char Narrow[CALC_TEXT_LENGTH];
    const WCHAR *Text;
    HANDLE Memory;
    int Used = 0;

    if (!OpenClipboard(MainWindow))
        return;
    Memory = GetClipboardData(CF_UNICODETEXT);
    Text = Memory != NULL ? GlobalLock(Memory) : NULL;
    if (Text != NULL)
    {
        for (; *Text != 0 && Used < (int)sizeof(Narrow) - 1; Text++)
        {
            if (*Text == L' ' || *Text == L'\t' || *Text == L'\r' || *Text == L'\n' || *Text == 0x00A0 ||
                (*Text == GroupMark[0] && *Text != DecimalMark[0] && Engine.Mode != CALC_MODE_PROGRAMMER))
                continue;
            if (*Text > 0x7E)
            {
                Used = 0;
                break;
            }
            Narrow[Used++] = *Text == DecimalMark[0] ? '.' : (char)*Text;
        }
        Narrow[Used] = 0;
        GlobalUnlock(Memory);
        if (Used == 0 || !CalcPaste(&Engine, Narrow))
            MessageBeep(MB_ICONWARNING);
    }
    CloseClipboard();
    UpdateKeys();
}

static CALC_KEY
KeyFromCharacter(WCHAR Character)
{
    if (Character >= L'0' && Character <= L'9')
        return (CALC_KEY)(CALC_KEY_0 + (Character - L'0'));
    if (Character == DecimalMark[0] || Character == L'.' || Character == L',')
        return CALC_KEY_POINT;
    if (Engine.Mode == CALC_MODE_PROGRAMMER)
    {
        if (Character >= L'a' && Character <= L'f')
            return (CALC_KEY)(CALC_KEY_A + (Character - L'a'));
        if (Character >= L'A' && Character <= L'F')
            return (CALC_KEY)(CALC_KEY_A + (Character - L'A'));
    }
    switch (Character)
    {
        case L'+':
            return CALC_KEY_ADD;
        case L'-':
            return CALC_KEY_SUBTRACT;
        case L'*':
            return CALC_KEY_MULTIPLY;
        case L'/':
            return CALC_KEY_DIVIDE;
        case L'=':
        case L'\r':
            return CALC_KEY_EQUALS;
        case L'\b':
            return CALC_KEY_BACK;
        case L'%':
            return CALC_KEY_PERCENT;
        case L'(':
            return CALC_KEY_OPEN;
        case L')':
            return CALC_KEY_CLOSE;
        case L'&':
            return CALC_KEY_AND;
        case L'|':
            return CALC_KEY_OR;
        case L'~':
            return CALC_KEY_NOT;
        case L'<':
            return CALC_KEY_SHIFT_LEFT;
        case L'>':
            return CALC_KEY_SHIFT_RIGHT;
        case L'^':
            return Engine.Mode == CALC_MODE_PROGRAMMER ? CALC_KEY_XOR : CALC_KEY_POWER;
        case L'@':
            return CALC_KEY_SQUARE_ROOT;
        case L'!':
            return CALC_KEY_FACTORIAL;
        case L'#':
            return CALC_KEY_CUBE;
        case L'r':
            return CALC_KEY_RECIPROCAL;
        case L'q':
            return CALC_KEY_SQUARE;
        case L'p':
            return CALC_KEY_PI;
        case L's':
            return CALC_KEY_SIN;
        case L'o':
            return CALC_KEY_COS;
        case L't':
            return CALC_KEY_TAN;
        case L'l':
            return CALC_KEY_LOG;
        case L'n':
            return CALC_KEY_LN;
        case L'x':
            return CALC_KEY_EXPONENT;
        case L'y':
            return CALC_KEY_POWER;
        case L'd':
            return CALC_KEY_MODULO;
        default:
            return CALC_KEY_NONE;
    }
}

static BOOL
OnKeyDown(WPARAM VirtualKey)
{
    BOOL Control = GetKeyState(VK_CONTROL) < 0;

    if (Control)
    {
        switch (VirtualKey)
        {
            case 'C':
                CopyValue();
                return TRUE;
            case 'V':
                PasteValue();
                return TRUE;
            case 'M':
                Press(CALC_KEY_MEMORY_STORE);
                return TRUE;
            case 'R':
                Press(CALC_KEY_MEMORY_RECALL);
                return TRUE;
            case 'L':
                Press(CALC_KEY_MEMORY_CLEAR);
                return TRUE;
            case 'P':
                Press(CALC_KEY_MEMORY_ADD);
                return TRUE;
            case 'Q':
                Press(CALC_KEY_MEMORY_SUBTRACT);
                return TRUE;
            default:
                return FALSE;
        }
    }
    switch (VirtualKey)
    {
        case VK_ESCAPE:
            Press(CALC_KEY_CLEAR);
            return TRUE;
        case VK_DELETE:
            Press(CALC_KEY_CLEAR_ENTRY);
            return TRUE;
        case VK_F9:
            Press(CALC_KEY_SIGN);
            return TRUE;
        case VK_F5:
        case VK_F6:
        case VK_F7:
        case VK_F8:
            if (Engine.Mode != CALC_MODE_PROGRAMMER)
                return FALSE;
            Press((CALC_KEY)(CALC_KEY_HEX + (VirtualKey - VK_F5)));
            return TRUE;
        default:
            return FALSE;
    }
}

static void
SetMode(CALC_MODE Mode)
{
    if (Mode == Engine.Mode)
        return;
    CalcSetMode(&Engine, Mode);
    SaveSettings();
    SizeForMode();
}

static void
OnCommand(UINT Command)
{
    WCHAR Title[64];

    if (Command >= IDC_KEY_FIRST && Command < IDC_KEY_FIRST + CALC_KEY_COUNT)
    {
        Press((CALC_KEY)(Command - IDC_KEY_FIRST));
        SetFocus(MainWindow);
        return;
    }
    switch (Command)
    {
        case IDM_STANDARD:
        case IDM_SCIENTIFIC:
        case IDM_PROGRAMMER:
            SetMode((CALC_MODE)(Command - IDM_STANDARD));
            break;
        case IDM_GROUPING:
            Grouping = !Grouping;
            SaveSettings();
            UpdateMenu();
            InvalidateRect(MainWindow, &DisplayRect, FALSE);
            break;
        case IDM_COPY:
            CopyValue();
            break;
        case IDM_PASTE:
            PasteValue();
            break;
        case IDM_ABOUT:
            LoadStringW(Instance, IDS_TITLE, Title, ARRAYSIZE(Title));
            ShellAboutW(MainWindow, Title, NULL, LoadIconW(Instance, MAKEINTRESOURCEW(IDI_CALC)));
            break;
        case IDM_EXIT:
            DestroyWindow(MainWindow);
            break;
        default:
            break;
    }
}

static LRESULT CALLBACK
WindowProcedure(HWND Window, UINT Message, WPARAM WParam, LPARAM LParam)
{
    switch (Message)
    {
        case WM_CREATE:
            MainWindow = Window;
            Dpi = GetDpiForWindow(Window);
            LoadMarks();
            CreateKeys();
            ApplyFonts();
            SizeForMode();
            return 0;
        case WM_SIZE:
            if (WParam != SIZE_MINIMIZED)
                PlaceKeys();
            return 0;
        case WM_GETMINMAXINFO:
        {
            MINMAXINFO *Info = (MINMAXINFO *)LParam;
            RECT Frame = {0, 0, Scale(Layouts[Engine.Mode].Width * 3 / 4), Scale(Layouts[Engine.Mode].Height * 3 / 4)};

            AdjustWindowRectEx(&Frame, (DWORD)GetWindowLongPtrW(Window, GWL_STYLE), TRUE,
                               (DWORD)GetWindowLongPtrW(Window, GWL_EXSTYLE));
            Info->ptMinTrackSize.x = Frame.right - Frame.left;
            Info->ptMinTrackSize.y = Frame.bottom - Frame.top;
            return 0;
        }
        case WM_PAINT:
        {
            PAINTSTRUCT Paint;
            HDC Context = BeginPaint(Window, &Paint);

            PaintDisplay(Context);
            EndPaint(Window, &Paint);
            return 0;
        }
        case WM_COMMAND:
            if (HIWORD(WParam) == BN_CLICKED || LParam == 0)
                OnCommand(LOWORD(WParam));
            return 0;
        case WM_CHAR:
        {
            CALC_KEY Key = KeyFromCharacter((WCHAR)WParam);

            if (Key != CALC_KEY_NONE)
                Press(Key);
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
        case WM_DPICHANGED:
        {
            const RECT *Suggested = (const RECT *)LParam;

            Dpi = HIWORD(WParam);
            ApplyFonts();
            SetWindowPos(Window, NULL, Suggested->left, Suggested->top, Suggested->right - Suggested->left,
                         Suggested->bottom - Suggested->top, SWP_NOZORDER | SWP_NOACTIVATE);
            PlaceKeys();
            UpdateKeys();
            return 0;
        }
        case WM_SETTINGCHANGE:
            LoadMarks();
            ApplyFonts();
            PlaceKeys();
            UpdateKeys();
            return 0;
        case WM_THEMECHANGED:
        case WM_SYSCOLORCHANGE:
            InvalidateRect(Window, NULL, TRUE);
            return 0;
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
        default:
            break;
    }
    return DefWindowProcW(Window, Message, WParam, LParam);
}

int WINAPI
wWinMain(HINSTANCE Module, HINSTANCE Previous, PWSTR CommandLine, int Show)
{
    WNDCLASSEXW Class = {sizeof(Class)};
    WCHAR Title[64];
    MSG Message;

    UNREFERENCED_PARAMETER(Previous);
    UNREFERENCED_PARAMETER(CommandLine);
    Instance = Module;
    InitCommonControls();
    CalcInit(&Engine);
    LoadSettings();
    Class.lpfnWndProc = WindowProcedure;
    Class.hInstance = Module;
    Class.hIcon = LoadIconW(Module, MAKEINTRESOURCEW(IDI_CALC));
    Class.hCursor = LoadCursorW(NULL, IDC_ARROW);
    Class.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    Class.lpszMenuName = MAKEINTRESOURCEW(IDR_MENU);
    Class.lpszClassName = CALC_CLASS;
    if (!RegisterClassExW(&Class))
        return 1;
    LoadStringW(Module, IDS_TITLE, Title, ARRAYSIZE(Title));
    if (CreateWindowExW(0, CALC_CLASS, Title, WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, CW_USEDEFAULT, CW_USEDEFAULT,
                        CW_USEDEFAULT, CW_USEDEFAULT, NULL, NULL, Module, NULL) == NULL)
        return 1;
    UpdateMenu();
    ShowWindow(MainWindow, Show);
    UpdateWindow(MainWindow);
    while (GetMessageW(&Message, NULL, 0, 0) > 0)
    {
        if (Message.message == WM_KEYDOWN || Message.message == WM_CHAR || Message.message == WM_SYSKEYDOWN ||
            Message.message == WM_SYSCHAR)
            Message.hwnd = MainWindow;
        TranslateMessage(&Message);
        DispatchMessageW(&Message);
    }
    return (int)Message.wParam;
}
