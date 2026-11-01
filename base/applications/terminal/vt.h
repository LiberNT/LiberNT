/*
 * PROJECT:     LiberNT Terminal
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Virtual terminal screen model and escape sequence parser
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

#include <stdint.h>
#include <stddef.h>
#include <deque>
#include <string>
#include <vector>

namespace Vt
{

/* A color is the default color, a palette index or a 24-bit RGB value. */
enum : uint32_t
{
    ColorDefault = 0,
    ColorIndexed = 0x01000000,
    ColorRgb = 0x02000000,
    ColorKindMask = 0xFF000000,
    ColorValueMask = 0x00FFFFFF
};

inline uint32_t IndexedColor(unsigned index) { return ColorIndexed | (index & 0xFF); }
inline uint32_t RgbColor(unsigned r, unsigned g, unsigned b) { return ColorRgb | ((r & 0xFF) << 16) | ((g & 0xFF) << 8) | (b & 0xFF); }

enum CellFlags : uint16_t
{
    CellBold = 0x0001,
    CellFaint = 0x0002,
    CellItalic = 0x0004,
    CellUnderline = 0x0008,
    CellBlink = 0x0010,
    CellInverse = 0x0020,
    CellHidden = 0x0040,
    CellStrike = 0x0080,
    CellDoubleUnderline = 0x0100,
    CellWide = 0x0200,
    CellWideTail = 0x0400
};

struct Attributes
{
    uint32_t Foreground = ColorDefault;
    uint32_t Background = ColorDefault;
    uint16_t Flags = 0;

    bool operator==(const Attributes &other) const
    {
        return Foreground == other.Foreground && Background == other.Background && Flags == other.Flags;
    }
    bool operator!=(const Attributes &other) const { return !(*this == other); }
};

struct Cell
{
    char32_t Ch = U' ';
    Attributes Attr;
};

struct Line
{
    std::vector<Cell> Cells;
    bool Wrapped = false;
};

enum class CursorShape
{
    Default,
    Block,
    Underline,
    Bar
};

class Screen
{
public:
    Screen(int cols, int rows, size_t scrollbackLimit);

    void Write(const char *data, size_t length);
    void Resize(int cols, int rows);
    void SetScrollbackLimit(size_t limit);
    void ClearScrollback();
    void Reset();

    int Cols() const { return m_Cols; }
    int Rows() const { return m_Rows; }
    size_t HistorySize() const { return m_Alternate ? 0 : m_History.size(); }

    /* Index 0 is the oldest history line; HistorySize() is the top of the screen. */
    const Line &LineAt(size_t index) const;
    size_t TotalLines() const { return HistorySize() + (size_t)m_Rows; }

    int CursorX() const { return m_CursorX >= m_Cols ? m_Cols - 1 : m_CursorX; }
    int CursorY() const { return m_CursorY; }
    bool CursorVisible() const { return m_CursorVisible; }
    CursorShape CursorStyle() const { return m_CursorShape; }
    bool CursorBlinking() const { return m_CursorBlink; }

    bool ApplicationCursorKeys() const { return m_AppCursorKeys; }
    bool ApplicationKeypad() const { return m_AppKeypad; }
    bool BracketedPaste() const { return m_BracketedPaste; }
    bool ReverseVideo() const { return m_ReverseVideo; }
    bool AlternateScreen() const { return m_Alternate; }
    int MouseMode() const { return m_MouseMode; }
    bool SgrMouse() const { return m_SgrMouse; }

    const std::basic_string<char32_t> &Title() const { return m_Title; }
    bool TakeTitleChanged();
    bool TakeBell();

    /* Bytes the terminal must send back to the application (replies to queries). */
    std::string TakeResponses();

    /* Lines touched since the last call, as screen rows. */
    bool TakeDirty(std::vector<bool> &rows);
    /* Number of lines pushed into the history since the last call. */
    size_t TakeScrolled();

    uint32_t PaletteOverride(unsigned index, bool *present) const;

private:
    enum class State
    {
        Ground,
        Escape,
        EscapeIntermediate,
        CsiEntry,
        CsiParam,
        CsiIntermediate,
        CsiIgnore,
        Osc,
        OscEscape,
        Dcs,
        DcsEscape,
        Ignore,
        IgnoreEscape
    };

    struct SavedCursor
    {
        int X = 0;
        int Y = 0;
        Attributes Attr;
        bool OriginMode = false;
        bool AutoWrap = true;
        bool G0Graphics = false;
        bool G1Graphics = false;
        bool ShiftOut = false;
        bool Valid = false;
    };

    void Feed(char32_t ch);
    void Print(char32_t ch);
    void Execute(char32_t ch);
    void EscDispatch(char32_t final);
    void CsiDispatch(char32_t final);
    void OscDispatch();
    void Sgr();
    void SetMode(bool privateMode, bool set);

    int Param(size_t index, int fallback) const;
    void ClearParams();

    Line BlankLine() const;
    Cell BlankCell() const;
    std::vector<Line> &Lines() { return m_Alternate ? m_AltLines : m_Main; }
    const std::vector<Line> &Lines() const { return m_Alternate ? m_AltLines : m_Main; }
    void MarkDirty(int row);
    void MarkAllDirty();
    void LineFeed();
    void ReverseIndex();
    void ScrollUp(int top, int bottom, int count);
    void ScrollDown(int top, int bottom, int count);
    void EraseInDisplay(int mode);
    void EraseInLine(int mode);
    void EraseCells(int row, int from, int to);
    void InsertCells(int count);
    void DeleteCells(int count);
    void InsertLines(int count);
    void DeleteLines(int count);
    void MoveCursor(int x, int y);
    void ClampCursor();
    void SaveCursor();
    void RestoreCursor();
    void SwitchAlternate(bool enable, bool saveCursor, bool clear);
    void Tab(int count);
    void BackTab(int count);
    void ResetTabs();
    void SoftReset();
    void AddResponse(const std::string &text);

    int m_Cols;
    int m_Rows;
    size_t m_ScrollbackLimit;
    std::deque<Line> m_History;
    std::vector<Line> m_Main;
    std::vector<Line> m_AltLines;
    bool m_Alternate = false;

    int m_CursorX = 0;
    int m_CursorY = 0;
    bool m_CursorVisible = true;
    CursorShape m_CursorShape = CursorShape::Default;
    bool m_CursorBlink = true;
    Attributes m_Attr;
    SavedCursor m_Saved;
    SavedCursor m_AltSaved;

    int m_Top = 0;
    int m_Bottom = 0;
    bool m_OriginMode = false;
    bool m_AutoWrap = true;
    bool m_InsertMode = false;
    bool m_NewLineMode = false;
    bool m_AppCursorKeys = false;
    bool m_AppKeypad = false;
    bool m_BracketedPaste = false;
    bool m_ReverseVideo = false;
    int m_MouseMode = 0;
    bool m_SgrMouse = false;
    bool m_G0Graphics = false;
    bool m_G1Graphics = false;
    bool m_ShiftOut = false;
    std::vector<bool> m_TabStops;
    char32_t m_LastPrinted = 0;

    State m_State = State::Ground;
    std::vector<int> m_Params;
    std::vector<bool> m_SubParam;
    bool m_ParamStarted = false;
    char32_t m_Private = 0;
    std::basic_string<char32_t> m_Intermediate;
    std::basic_string<char32_t> m_OscText;

    uint32_t m_Utf8Code = 0;
    int m_Utf8Remaining = 0;

    std::basic_string<char32_t> m_Title;
    bool m_TitleChanged = false;
    bool m_Bell = false;
    std::string m_Responses;
    std::vector<bool> m_Dirty;
    bool m_AllDirty = true;
    size_t m_Scrolled = 0;
    uint32_t m_Palette[256] = {};
    bool m_PaletteSet[256] = {};
};

int CharWidth(char32_t ch);
std::string EncodeUtf8(const std::basic_string<char32_t> &text);
std::basic_string<char32_t> DecodeUtf16(const char16_t *text, size_t length);
std::basic_string<char16_t> EncodeUtf16(const std::basic_string<char32_t> &text);

}
