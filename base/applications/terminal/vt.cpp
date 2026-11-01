/*
 * PROJECT:     LiberNT Terminal
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Virtual terminal screen model and escape sequence parser
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "vt.h"

#include <stdio.h>
#include <string.h>

namespace Vt
{

namespace
{

struct Range
{
    char32_t First;
    char32_t Last;
};

const Range ZeroWidth[] =
{
    { 0x0300, 0x036F }, { 0x0483, 0x0489 }, { 0x0591, 0x05BD }, { 0x05BF, 0x05BF }, { 0x05C1, 0x05C2 },
    { 0x05C4, 0x05C5 }, { 0x05C7, 0x05C7 }, { 0x0610, 0x061A }, { 0x064B, 0x065F }, { 0x0670, 0x0670 },
    { 0x06D6, 0x06DC }, { 0x06DF, 0x06E4 }, { 0x06E7, 0x06E8 }, { 0x06EA, 0x06ED }, { 0x0711, 0x0711 },
    { 0x0730, 0x074A }, { 0x07A6, 0x07B0 }, { 0x0816, 0x082D }, { 0x0900, 0x0902 }, { 0x093A, 0x093A },
    { 0x093C, 0x093C }, { 0x0941, 0x0948 }, { 0x094D, 0x094D }, { 0x0951, 0x0957 }, { 0x0962, 0x0963 },
    { 0x0981, 0x0981 }, { 0x09BC, 0x09BC }, { 0x09C1, 0x09C4 }, { 0x09CD, 0x09CD }, { 0x0A01, 0x0A02 },
    { 0x0E31, 0x0E31 }, { 0x0E34, 0x0E3A }, { 0x0E47, 0x0E4E }, { 0x0EB1, 0x0EB1 }, { 0x0EB4, 0x0EBC },
    { 0x0EC8, 0x0ECD }, { 0x1AB0, 0x1AFF }, { 0x1DC0, 0x1DFF }, { 0x200B, 0x200F }, { 0x202A, 0x202E },
    { 0x2060, 0x2064 }, { 0x20D0, 0x20FF }, { 0xFE00, 0xFE0F }, { 0xFE20, 0xFE2F }, { 0xFEFF, 0xFEFF },
    { 0x1F3FB, 0x1F3FF }, { 0xE0001, 0xE007F }, { 0xE0100, 0xE01EF }
};

const Range Wide[] =
{
    { 0x1100, 0x115F }, { 0x231A, 0x231B }, { 0x2329, 0x232A }, { 0x23E9, 0x23EC }, { 0x23F0, 0x23F0 },
    { 0x23F3, 0x23F3 }, { 0x25FD, 0x25FE }, { 0x2614, 0x2615 }, { 0x2648, 0x2653 }, { 0x267F, 0x267F },
    { 0x2693, 0x2693 }, { 0x26A1, 0x26A1 }, { 0x26AA, 0x26AB }, { 0x26BD, 0x26BE }, { 0x26C4, 0x26C5 },
    { 0x26CE, 0x26CE }, { 0x26D4, 0x26D4 }, { 0x26EA, 0x26EA }, { 0x26F2, 0x26F3 }, { 0x26F5, 0x26F5 },
    { 0x26FA, 0x26FA }, { 0x26FD, 0x26FD }, { 0x2705, 0x2705 }, { 0x270A, 0x270B }, { 0x2728, 0x2728 },
    { 0x274C, 0x274C }, { 0x274E, 0x274E }, { 0x2753, 0x2755 }, { 0x2757, 0x2757 }, { 0x2795, 0x2797 },
    { 0x27B0, 0x27B0 }, { 0x27BF, 0x27BF }, { 0x2B1B, 0x2B1C }, { 0x2B50, 0x2B50 }, { 0x2B55, 0x2B55 },
    { 0x2E80, 0x303E }, { 0x3041, 0x33FF }, { 0x3400, 0x4DBF }, { 0x4E00, 0x9FFF }, { 0xA000, 0xA4CF },
    { 0xA960, 0xA97F }, { 0xAC00, 0xD7A3 }, { 0xF900, 0xFAFF }, { 0xFE10, 0xFE19 }, { 0xFE30, 0xFE6F },
    { 0xFF00, 0xFF60 }, { 0xFFE0, 0xFFE6 }, { 0x16FE0, 0x16FE4 }, { 0x17000, 0x18AFF }, { 0x1B000, 0x1B2FF },
    { 0x1F004, 0x1F004 }, { 0x1F0CF, 0x1F0CF }, { 0x1F18E, 0x1F18E }, { 0x1F191, 0x1F19A }, { 0x1F200, 0x1F202 },
    { 0x1F210, 0x1F23B }, { 0x1F240, 0x1F248 }, { 0x1F250, 0x1F251 }, { 0x1F260, 0x1F265 }, { 0x1F300, 0x1F320 },
    { 0x1F32D, 0x1F335 }, { 0x1F337, 0x1F37C }, { 0x1F37E, 0x1F393 }, { 0x1F3A0, 0x1F3CA }, { 0x1F3CF, 0x1F3D3 },
    { 0x1F3E0, 0x1F3F0 }, { 0x1F3F4, 0x1F3F4 }, { 0x1F3F8, 0x1F43E }, { 0x1F440, 0x1F440 }, { 0x1F442, 0x1F4FC },
    { 0x1F4FF, 0x1F53D }, { 0x1F54B, 0x1F54E }, { 0x1F550, 0x1F567 }, { 0x1F57A, 0x1F57A }, { 0x1F595, 0x1F596 },
    { 0x1F5A4, 0x1F5A4 }, { 0x1F5FB, 0x1F64F }, { 0x1F680, 0x1F6C5 }, { 0x1F6CC, 0x1F6CC }, { 0x1F6D0, 0x1F6D2 },
    { 0x1F6D5, 0x1F6D7 }, { 0x1F6EB, 0x1F6EC }, { 0x1F6F4, 0x1F6FC }, { 0x1F7E0, 0x1F7EB }, { 0x1F90C, 0x1F93A },
    { 0x1F93C, 0x1F945 }, { 0x1F947, 0x1F9FF }, { 0x1FA70, 0x1FAFF }, { 0x20000, 0x2FFFD }, { 0x30000, 0x3FFFD }
};

bool InRanges(const Range *ranges, size_t count, char32_t ch)
{
    size_t low = 0, high = count;

    while (low < high)
    {
        size_t middle = (low + high) / 2;

        if (ch < ranges[middle].First)
            high = middle;
        else if (ch > ranges[middle].Last)
            low = middle + 1;
        else
            return true;
    }
    return false;
}

/* DEC special graphics for 0x60-0x7E, the line drawing set. */
const char32_t DecGraphics[31] =
{
    0x25C6, 0x2592, 0x2409, 0x240C, 0x240D, 0x240A, 0x00B0, 0x00B1, 0x2424, 0x240B, 0x2518, 0x2510, 0x250C,
    0x2514, 0x253C, 0x23BA, 0x23BB, 0x2500, 0x23BC, 0x23BD, 0x251C, 0x2524, 0x2534, 0x252C, 0x2502, 0x2264,
    0x2265, 0x03C0, 0x2260, 0x00A3, 0x00B7
};

bool ParseHexColor(const std::basic_string<char32_t> &text, uint32_t *color)
{
    unsigned value[3] = { 0, 0, 0 };

    if (text.size() == 7 && text[0] == U'#')
    {
        for (size_t i = 0; i < 3; i++)
        {
            for (size_t j = 0; j < 2; j++)
            {
                char32_t ch = text[1 + i * 2 + j];
                unsigned digit;

                if (ch >= U'0' && ch <= U'9')
                    digit = ch - U'0';
                else if (ch >= U'a' && ch <= U'f')
                    digit = ch - U'a' + 10;
                else if (ch >= U'A' && ch <= U'F')
                    digit = ch - U'A' + 10;
                else
                    return false;
                value[i] = value[i] * 16 + digit;
            }
        }
        *color = RgbColor(value[0], value[1], value[2]);
        return true;
    }
    if (text.compare(0, 4, U"rgb:") == 0)
    {
        size_t position = 4;

        for (size_t i = 0; i < 3; i++)
        {
            unsigned digits = 0, component = 0;

            while (position < text.size() && text[position] != U'/')
            {
                char32_t ch = text[position++];
                unsigned digit;

                if (ch >= U'0' && ch <= U'9')
                    digit = ch - U'0';
                else if (ch >= U'a' && ch <= U'f')
                    digit = ch - U'a' + 10;
                else if (ch >= U'A' && ch <= U'F')
                    digit = ch - U'A' + 10;
                else
                    return false;
                component = component * 16 + digit;
                digits++;
            }
            if (digits == 0 || digits > 4)
                return false;
            value[i] = digits == 1 ? component * 17 : digits == 2 ? component : component >> ((digits - 2) * 4);
            if (i < 2)
            {
                if (position >= text.size())
                    return false;
                position++;
            }
        }
        *color = RgbColor(value[0], value[1], value[2]);
        return true;
    }
    return false;
}

}

int
CharWidth(char32_t ch)
{
    if (ch < 0x20 || (ch >= 0x7F && ch < 0xA0))
        return 0;
    if (ch < 0x300)
        return 1;
    if (InRanges(ZeroWidth, sizeof(ZeroWidth) / sizeof(ZeroWidth[0]), ch))
        return 0;
    if (InRanges(Wide, sizeof(Wide) / sizeof(Wide[0]), ch))
        return 2;
    return 1;
}

std::string
EncodeUtf8(const std::basic_string<char32_t> &text)
{
    std::string out;

    for (char32_t ch : text)
    {
        if (ch < 0x80)
        {
            out += (char)ch;
        }
        else if (ch < 0x800)
        {
            out += (char)(0xC0 | (ch >> 6));
            out += (char)(0x80 | (ch & 0x3F));
        }
        else if (ch < 0x10000)
        {
            out += (char)(0xE0 | (ch >> 12));
            out += (char)(0x80 | ((ch >> 6) & 0x3F));
            out += (char)(0x80 | (ch & 0x3F));
        }
        else
        {
            out += (char)(0xF0 | (ch >> 18));
            out += (char)(0x80 | ((ch >> 12) & 0x3F));
            out += (char)(0x80 | ((ch >> 6) & 0x3F));
            out += (char)(0x80 | (ch & 0x3F));
        }
    }
    return out;
}

std::basic_string<char32_t>
DecodeUtf16(const char16_t *text, size_t length)
{
    std::basic_string<char32_t> out;

    for (size_t i = 0; i < length; i++)
    {
        char32_t ch = text[i];

        if (ch >= 0xD800 && ch <= 0xDBFF && i + 1 < length && text[i + 1] >= 0xDC00 && text[i + 1] <= 0xDFFF)
        {
            ch = 0x10000 + ((ch - 0xD800) << 10) + (text[i + 1] - 0xDC00);
            i++;
        }
        else if (ch >= 0xD800 && ch <= 0xDFFF)
        {
            ch = 0xFFFD;
        }
        out += ch;
    }
    return out;
}

std::basic_string<char16_t>
EncodeUtf16(const std::basic_string<char32_t> &text)
{
    std::basic_string<char16_t> out;

    for (char32_t ch : text)
    {
        if (ch >= 0x10000)
        {
            ch -= 0x10000;
            out += (char16_t)(0xD800 + (ch >> 10));
            out += (char16_t)(0xDC00 + (ch & 0x3FF));
        }
        else
        {
            out += (char16_t)ch;
        }
    }
    return out;
}

Screen::Screen(int cols, int rows, size_t scrollbackLimit) :
    m_Cols(cols < 1 ? 1 : cols),
    m_Rows(rows < 1 ? 1 : rows),
    m_ScrollbackLimit(scrollbackLimit)
{
    m_Main.assign(m_Rows, BlankLine());
    m_Bottom = m_Rows - 1;
    m_Dirty.assign(m_Rows, true);
    ResetTabs();
}

Cell
Screen::BlankCell() const
{
    Cell cell;

    cell.Attr.Background = m_Attr.Background;
    return cell;
}

Line
Screen::BlankLine() const
{
    Line line;

    line.Cells.assign(m_Cols, BlankCell());
    return line;
}

void
Screen::MarkDirty(int row)
{
    if (row >= 0 && row < m_Rows)
        m_Dirty[row] = true;
}

void
Screen::MarkAllDirty()
{
    m_AllDirty = true;
}

bool
Screen::TakeDirty(std::vector<bool> &rows)
{
    bool any = m_AllDirty;

    rows.assign(m_Rows, m_AllDirty);
    for (int i = 0; i < m_Rows; i++)
    {
        if (m_Dirty[i])
        {
            rows[i] = true;
            any = true;
        }
        m_Dirty[i] = false;
    }
    m_AllDirty = false;
    return any;
}

size_t
Screen::TakeScrolled()
{
    size_t scrolled = m_Scrolled;

    m_Scrolled = 0;
    return scrolled;
}

bool
Screen::TakeTitleChanged()
{
    bool changed = m_TitleChanged;

    m_TitleChanged = false;
    return changed;
}

bool
Screen::TakeBell()
{
    bool bell = m_Bell;

    m_Bell = false;
    return bell;
}

std::string
Screen::TakeResponses()
{
    std::string responses;

    responses.swap(m_Responses);
    return responses;
}

void
Screen::AddResponse(const std::string &text)
{
    m_Responses += text;
}

uint32_t
Screen::PaletteOverride(unsigned index, bool *present) const
{
    *present = index < 256 && m_PaletteSet[index];
    return *present ? m_Palette[index] : 0;
}

const Line &
Screen::LineAt(size_t index) const
{
    size_t history = HistorySize();

    if (index < history)
        return m_History[index];
    index -= history;
    if (index >= Lines().size())
        index = Lines().size() - 1;
    return Lines()[index];
}

void
Screen::SetScrollbackLimit(size_t limit)
{
    m_ScrollbackLimit = limit;
    while (m_History.size() > m_ScrollbackLimit)
        m_History.pop_front();
}

void
Screen::ClearScrollback()
{
    m_History.clear();
    MarkAllDirty();
}

void
Screen::ResetTabs()
{
    m_TabStops.assign(m_Cols, false);
    for (int i = 8; i < m_Cols; i += 8)
        m_TabStops[i] = true;
}

void
Screen::Resize(int cols, int rows)
{
    if (cols < 1)
        cols = 1;
    if (rows < 1)
        rows = 1;
    if (cols == m_Cols && rows == m_Rows)
        return;

    for (int pass = 0; pass < 2; pass++)
    {
        std::vector<Line> &lines = pass == 0 ? m_Main : m_AltLines;
        bool active = (pass == 1) == m_Alternate;

        if (lines.empty())
            continue;
        for (Line &line : lines)
            line.Cells.resize(cols, Cell());
        if (pass == 0)
        {
            for (Line &line : m_History)
                line.Cells.resize(cols, Cell());
        }
        if (rows < (int)lines.size())
        {
            int excess = (int)lines.size() - rows;
            int below = (int)lines.size() - 1 - (active ? m_CursorY : (int)lines.size() - 1);
            int dropBottom = below < excess ? below : excess;
            int dropTop = excess - dropBottom;

            lines.resize(lines.size() - dropBottom);
            for (int i = 0; i < dropTop; i++)
            {
                if (pass == 0)
                {
                    m_History.push_back(lines.front());
                    if (m_History.size() > m_ScrollbackLimit)
                        m_History.pop_front();
                }
                lines.erase(lines.begin());
            }
            if (active)
                m_CursorY -= dropTop;
        }
        else
        {
            while ((int)lines.size() < rows)
            {
                if (pass == 0 && !m_History.empty() && active && (int)lines.size() < rows)
                {
                    lines.insert(lines.begin(), m_History.back());
                    m_History.pop_back();
                    m_CursorY++;
                }
                else
                {
                    Line line;
                    line.Cells.assign(cols, Cell());
                    lines.push_back(line);
                }
            }
        }
    }

    m_Cols = cols;
    m_Rows = rows;
    m_Top = 0;
    m_Bottom = m_Rows - 1;
    m_Dirty.assign(m_Rows, true);
    ResetTabs();
    ClampCursor();
    if (m_Saved.Y >= m_Rows)
        m_Saved.Y = m_Rows - 1;
    if (m_Saved.X >= m_Cols)
        m_Saved.X = m_Cols - 1;
    MarkAllDirty();
}

void
Screen::Reset()
{
    m_History.clear();
    m_Alternate = false;
    m_AltLines.clear();
    m_Attr = Attributes();
    m_Main.assign(m_Rows, BlankLine());
    m_CursorX = 0;
    m_CursorY = 0;
    m_Saved = SavedCursor();
    m_AltSaved = SavedCursor();
    m_Top = 0;
    m_Bottom = m_Rows - 1;
    m_OriginMode = false;
    m_AutoWrap = true;
    m_InsertMode = false;
    m_NewLineMode = false;
    m_AppCursorKeys = false;
    m_AppKeypad = false;
    m_BracketedPaste = false;
    m_ReverseVideo = false;
    m_MouseMode = 0;
    m_SgrMouse = false;
    m_CursorVisible = true;
    m_CursorShape = CursorShape::Default;
    m_CursorBlink = true;
    m_G0Graphics = false;
    m_G1Graphics = false;
    m_ShiftOut = false;
    memset(m_PaletteSet, 0, sizeof(m_PaletteSet));
    ResetTabs();
    MarkAllDirty();
}

void
Screen::SoftReset()
{
    m_CursorVisible = true;
    m_OriginMode = false;
    m_AutoWrap = true;
    m_InsertMode = false;
    m_AppCursorKeys = false;
    m_AppKeypad = false;
    m_Top = 0;
    m_Bottom = m_Rows - 1;
    m_Attr = Attributes();
    m_G0Graphics = false;
    m_G1Graphics = false;
    m_ShiftOut = false;
    m_Saved = SavedCursor();
}

void
Screen::Write(const char *data, size_t length)
{
    for (size_t i = 0; i < length; i++)
    {
        unsigned char byte = (unsigned char)data[i];

        if (m_Utf8Remaining > 0)
        {
            if ((byte & 0xC0) == 0x80)
            {
                m_Utf8Code = (m_Utf8Code << 6) | (byte & 0x3F);
                if (--m_Utf8Remaining == 0)
                    Feed(m_Utf8Code > 0x10FFFF ? 0xFFFD : m_Utf8Code);
                continue;
            }
            m_Utf8Remaining = 0;
            Feed(0xFFFD);
        }
        if (byte < 0x80)
        {
            Feed(byte);
        }
        else if ((byte & 0xE0) == 0xC0)
        {
            m_Utf8Code = byte & 0x1F;
            m_Utf8Remaining = 1;
        }
        else if ((byte & 0xF0) == 0xE0)
        {
            m_Utf8Code = byte & 0x0F;
            m_Utf8Remaining = 2;
        }
        else if ((byte & 0xF8) == 0xF0)
        {
            m_Utf8Code = byte & 0x07;
            m_Utf8Remaining = 3;
        }
        else
        {
            Feed(0xFFFD);
        }
    }
}

void
Screen::ClearParams()
{
    m_Params.clear();
    m_SubParam.clear();
    m_ParamStarted = false;
    m_Private = 0;
    m_Intermediate.clear();
}

int
Screen::Param(size_t index, int fallback) const
{
    if (index >= m_Params.size() || m_Params[index] < 0)
        return fallback;
    return m_Params[index];
}

void
Screen::Feed(char32_t ch)
{
    if (ch == 0x18 || ch == 0x1A)
    {
        m_State = State::Ground;
        return;
    }
    if (ch == 0x1B && m_State != State::Osc && m_State != State::Dcs && m_State != State::Ignore)
    {
        ClearParams();
        m_State = State::Escape;
        return;
    }

    switch (m_State)
    {
        case State::Ground:
            if (ch < 0x20 || ch == 0x7F)
                Execute(ch);
            else
                Print(ch);
            return;

        case State::Escape:
            if (ch < 0x20)
            {
                Execute(ch);
            }
            else if (ch >= 0x20 && ch <= 0x2F)
            {
                m_Intermediate += ch;
                m_State = State::EscapeIntermediate;
            }
            else if (ch == U'[')
            {
                ClearParams();
                m_State = State::CsiEntry;
            }
            else if (ch == U']')
            {
                m_OscText.clear();
                m_State = State::Osc;
            }
            else if (ch == U'P')
            {
                m_State = State::Dcs;
            }
            else if (ch == U'X' || ch == U'^' || ch == U'_')
            {
                m_State = State::Ignore;
            }
            else
            {
                EscDispatch(ch);
                m_State = State::Ground;
            }
            return;

        case State::EscapeIntermediate:
            if (ch < 0x20)
                Execute(ch);
            else if (ch <= 0x2F)
                m_Intermediate += ch;
            else
            {
                EscDispatch(ch);
                m_State = State::Ground;
            }
            return;

        case State::CsiEntry:
        case State::CsiParam:
            if (ch < 0x20)
            {
                Execute(ch);
                return;
            }
            if (ch >= U'0' && ch <= U'9')
            {
                if (!m_ParamStarted)
                {
                    m_Params.push_back(0);
                    m_SubParam.push_back(false);
                    m_ParamStarted = true;
                }
                int &value = m_Params.back();
                if (value < 0)
                    value = 0;
                if (value < 100000)
                    value = value * 10 + (int)(ch - U'0');
                m_State = State::CsiParam;
                return;
            }
            if (ch == U';' || ch == U':')
            {
                if (!m_ParamStarted)
                {
                    m_Params.push_back(-1);
                    m_SubParam.push_back(false);
                }
                m_Params.push_back(-1);
                m_SubParam.push_back(ch == U':');
                m_ParamStarted = true;
                m_State = State::CsiParam;
                return;
            }
            if (ch >= U'<' && ch <= U'?')
            {
                if (m_State == State::CsiEntry)
                    m_Private = ch;
                else
                    m_State = State::CsiIgnore;
                return;
            }
            if (ch >= 0x20 && ch <= 0x2F)
            {
                m_Intermediate += ch;
                m_State = State::CsiIntermediate;
                return;
            }
            if (ch >= 0x40 && ch <= 0x7E)
            {
                CsiDispatch(ch);
                m_State = State::Ground;
                return;
            }
            m_State = State::CsiIgnore;
            return;

        case State::CsiIntermediate:
            if (ch < 0x20)
                Execute(ch);
            else if (ch <= 0x2F)
                m_Intermediate += ch;
            else if (ch >= 0x40 && ch <= 0x7E)
            {
                CsiDispatch(ch);
                m_State = State::Ground;
            }
            else
                m_State = State::CsiIgnore;
            return;

        case State::CsiIgnore:
            if (ch < 0x20)
                Execute(ch);
            else if (ch >= 0x40 && ch <= 0x7E)
                m_State = State::Ground;
            return;

        case State::Osc:
            if (ch == 0x07)
            {
                OscDispatch();
                m_State = State::Ground;
            }
            else if (ch == 0x1B)
            {
                m_State = State::OscEscape;
            }
            else if (m_OscText.size() < 4096)
            {
                m_OscText += ch;
            }
            return;

        case State::OscEscape:
            OscDispatch();
            m_State = State::Ground;
            if (ch != U'\\')
                Feed(ch);
            return;

        case State::Dcs:
            if (ch == 0x1B)
                m_State = State::DcsEscape;
            return;

        case State::DcsEscape:
            m_State = ch == U'\\' ? State::Ground : State::Dcs;
            return;

        case State::Ignore:
            if (ch == 0x1B)
                m_State = State::IgnoreEscape;
            return;

        case State::IgnoreEscape:
            m_State = ch == U'\\' ? State::Ground : State::Ignore;
            return;
    }
}

void
Screen::Execute(char32_t ch)
{
    switch (ch)
    {
        case 0x07:
            m_Bell = true;
            break;
        case 0x08:
            if (m_CursorX >= m_Cols)
                m_CursorX = m_Cols - 1;
            if (m_CursorX > 0)
                m_CursorX--;
            break;
        case 0x09:
            Tab(1);
            break;
        case 0x0A:
        case 0x0B:
        case 0x0C:
            LineFeed();
            if (m_NewLineMode)
                m_CursorX = 0;
            break;
        case 0x0D:
            m_CursorX = 0;
            break;
        case 0x0E:
            m_ShiftOut = true;
            break;
        case 0x0F:
            m_ShiftOut = false;
            break;
        default:
            break;
    }
}

void
Screen::Print(char32_t ch)
{
    std::vector<Line> &lines = Lines();
    bool graphics = m_ShiftOut ? m_G1Graphics : m_G0Graphics;
    int width;

    if (graphics && ch >= 0x60 && ch <= 0x7E)
        ch = DecGraphics[ch - 0x60];
    width = CharWidth(ch);
    if (width == 0)
        return;
    if (width > m_Cols)
        width = 1;

    if (m_CursorX >= m_Cols || (width == 2 && m_CursorX == m_Cols - 1))
    {
        if (m_AutoWrap)
        {
            lines[m_CursorY].Wrapped = true;
            m_CursorX = 0;
            LineFeed();
        }
        else
        {
            m_CursorX = m_Cols - width;
        }
    }

    Line &line = lines[m_CursorY];
    if (m_InsertMode)
    {
        for (int i = m_Cols - 1; i >= m_CursorX + width; i--)
            line.Cells[i] = line.Cells[i - width];
    }

    /* Overwriting half of a wide character leaves its other half blank. */
    if (line.Cells[m_CursorX].Attr.Flags & CellWideTail && m_CursorX > 0)
    {
        line.Cells[m_CursorX - 1].Ch = U' ';
        line.Cells[m_CursorX - 1].Attr.Flags &= ~CellWide;
    }
    if (width == 1 && (line.Cells[m_CursorX].Attr.Flags & CellWide) && m_CursorX + 1 < m_Cols)
    {
        line.Cells[m_CursorX + 1].Ch = U' ';
        line.Cells[m_CursorX + 1].Attr.Flags &= ~CellWideTail;
    }

    line.Cells[m_CursorX].Ch = ch;
    line.Cells[m_CursorX].Attr = m_Attr;
    line.Cells[m_CursorX].Attr.Flags &= ~(CellWide | CellWideTail);
    if (width == 2)
    {
        line.Cells[m_CursorX].Attr.Flags |= CellWide;
        line.Cells[m_CursorX + 1].Ch = 0;
        line.Cells[m_CursorX + 1].Attr = m_Attr;
        line.Cells[m_CursorX + 1].Attr.Flags = (uint16_t)((m_Attr.Flags & ~CellWide) | CellWideTail);
    }
    MarkDirty(m_CursorY);
    m_LastPrinted = ch;
    m_CursorX += width;
    if (m_CursorX > m_Cols)
        m_CursorX = m_Cols;
}

void
Screen::LineFeed()
{
    if (m_CursorY == m_Bottom)
        ScrollUp(m_Top, m_Bottom, 1);
    else if (m_CursorY < m_Rows - 1)
        m_CursorY++;
}

void
Screen::ReverseIndex()
{
    if (m_CursorY == m_Top)
        ScrollDown(m_Top, m_Bottom, 1);
    else if (m_CursorY > 0)
        m_CursorY--;
}

void
Screen::ScrollUp(int top, int bottom, int count)
{
    std::vector<Line> &lines = Lines();

    if (count > bottom - top + 1)
        count = bottom - top + 1;
    for (int i = 0; i < count; i++)
    {
        if (top == 0 && !m_Alternate && m_ScrollbackLimit > 0)
        {
            m_History.push_back(lines[top]);
            if (m_History.size() > m_ScrollbackLimit)
                m_History.pop_front();
            else
                m_Scrolled++;
        }
        lines.erase(lines.begin() + top);
        lines.insert(lines.begin() + bottom, BlankLine());
    }
    for (int row = top; row <= bottom; row++)
        MarkDirty(row);
    if (top == 0)
        MarkAllDirty();
}

void
Screen::ScrollDown(int top, int bottom, int count)
{
    std::vector<Line> &lines = Lines();

    if (count > bottom - top + 1)
        count = bottom - top + 1;
    for (int i = 0; i < count; i++)
    {
        lines.erase(lines.begin() + bottom);
        lines.insert(lines.begin() + top, BlankLine());
    }
    for (int row = top; row <= bottom; row++)
        MarkDirty(row);
}

void
Screen::EraseCells(int row, int from, int to)
{
    Line &line = Lines()[row];

    if (from < 0)
        from = 0;
    if (to > m_Cols)
        to = m_Cols;
    if (from > 0 && (line.Cells[from].Attr.Flags & CellWideTail))
        from--;
    if (to < m_Cols && to > 0 && (line.Cells[to - 1].Attr.Flags & CellWide))
        to++;
    for (int i = from; i < to; i++)
        line.Cells[i] = BlankCell();
    if (to >= m_Cols)
        line.Wrapped = false;
    MarkDirty(row);
}

void
Screen::EraseInDisplay(int mode)
{
    int y = m_CursorY, x = m_CursorX >= m_Cols ? m_Cols - 1 : m_CursorX;

    switch (mode)
    {
        case 0:
            EraseCells(y, x, m_Cols);
            for (int row = y + 1; row < m_Rows; row++)
                EraseCells(row, 0, m_Cols);
            break;
        case 1:
            for (int row = 0; row < y; row++)
                EraseCells(row, 0, m_Cols);
            EraseCells(y, 0, x + 1);
            break;
        case 2:
            for (int row = 0; row < m_Rows; row++)
                EraseCells(row, 0, m_Cols);
            break;
        case 3:
            ClearScrollback();
            break;
    }
}

void
Screen::EraseInLine(int mode)
{
    int x = m_CursorX >= m_Cols ? m_Cols - 1 : m_CursorX;

    switch (mode)
    {
        case 0:
            EraseCells(m_CursorY, x, m_Cols);
            break;
        case 1:
            EraseCells(m_CursorY, 0, x + 1);
            break;
        case 2:
            EraseCells(m_CursorY, 0, m_Cols);
            break;
    }
}

void
Screen::InsertCells(int count)
{
    Line &line = Lines()[m_CursorY];
    int x = m_CursorX >= m_Cols ? m_Cols - 1 : m_CursorX;

    if (count > m_Cols - x)
        count = m_Cols - x;
    for (int i = m_Cols - 1; i >= x + count; i--)
        line.Cells[i] = line.Cells[i - count];
    for (int i = x; i < x + count; i++)
        line.Cells[i] = BlankCell();
    MarkDirty(m_CursorY);
}

void
Screen::DeleteCells(int count)
{
    Line &line = Lines()[m_CursorY];
    int x = m_CursorX >= m_Cols ? m_Cols - 1 : m_CursorX;

    if (count > m_Cols - x)
        count = m_Cols - x;
    for (int i = x; i < m_Cols - count; i++)
        line.Cells[i] = line.Cells[i + count];
    for (int i = m_Cols - count; i < m_Cols; i++)
        line.Cells[i] = BlankCell();
    MarkDirty(m_CursorY);
}

void
Screen::InsertLines(int count)
{
    if (m_CursorY < m_Top || m_CursorY > m_Bottom)
        return;
    ScrollDown(m_CursorY, m_Bottom, count);
    m_CursorX = 0;
}

void
Screen::DeleteLines(int count)
{
    std::vector<Line> &lines = Lines();

    if (m_CursorY < m_Top || m_CursorY > m_Bottom)
        return;
    if (count > m_Bottom - m_CursorY + 1)
        count = m_Bottom - m_CursorY + 1;
    for (int i = 0; i < count; i++)
    {
        lines.erase(lines.begin() + m_CursorY);
        lines.insert(lines.begin() + m_Bottom, BlankLine());
    }
    for (int row = m_CursorY; row <= m_Bottom; row++)
        MarkDirty(row);
    m_CursorX = 0;
}

void
Screen::ClampCursor()
{
    if (m_CursorX < 0)
        m_CursorX = 0;
    if (m_CursorX > m_Cols - 1)
        m_CursorX = m_Cols - 1;
    if (m_CursorY < 0)
        m_CursorY = 0;
    if (m_CursorY > m_Rows - 1)
        m_CursorY = m_Rows - 1;
}

void
Screen::MoveCursor(int x, int y)
{
    int top = m_OriginMode ? m_Top : 0;
    int bottom = m_OriginMode ? m_Bottom : m_Rows - 1;

    m_CursorX = x;
    m_CursorY = y;
    if (m_CursorY < top)
        m_CursorY = top;
    if (m_CursorY > bottom)
        m_CursorY = bottom;
    ClampCursor();
}

void
Screen::SaveCursor()
{
    SavedCursor &saved = m_Alternate ? m_AltSaved : m_Saved;

    saved.X = m_CursorX >= m_Cols ? m_Cols - 1 : m_CursorX;
    saved.Y = m_CursorY;
    saved.Attr = m_Attr;
    saved.OriginMode = m_OriginMode;
    saved.AutoWrap = m_AutoWrap;
    saved.G0Graphics = m_G0Graphics;
    saved.G1Graphics = m_G1Graphics;
    saved.ShiftOut = m_ShiftOut;
    saved.Valid = true;
}

void
Screen::RestoreCursor()
{
    SavedCursor &saved = m_Alternate ? m_AltSaved : m_Saved;

    if (!saved.Valid)
    {
        m_CursorX = 0;
        m_CursorY = 0;
        m_Attr = Attributes();
        m_OriginMode = false;
        return;
    }
    m_CursorX = saved.X;
    m_CursorY = saved.Y;
    m_Attr = saved.Attr;
    m_OriginMode = saved.OriginMode;
    m_AutoWrap = saved.AutoWrap;
    m_G0Graphics = saved.G0Graphics;
    m_G1Graphics = saved.G1Graphics;
    m_ShiftOut = saved.ShiftOut;
    ClampCursor();
}

void
Screen::SwitchAlternate(bool enable, bool saveCursor, bool clear)
{
    if (enable == m_Alternate)
    {
        if (enable && clear)
            EraseInDisplay(2);
        return;
    }
    if (enable)
    {
        if (saveCursor)
            SaveCursor();
        m_AltLines.assign(m_Rows, BlankLine());
        m_Alternate = true;
        m_AltSaved = m_Saved;
    }
    else
    {
        m_Alternate = false;
        m_AltLines.clear();
        if (saveCursor)
            RestoreCursor();
    }
    m_Top = 0;
    m_Bottom = m_Rows - 1;
    MarkAllDirty();
}

void
Screen::Tab(int count)
{
    while (count-- > 0)
    {
        int x = m_CursorX >= m_Cols ? m_Cols - 1 : m_CursorX;

        do
        {
            x++;
        }
        while (x < m_Cols - 1 && !m_TabStops[x]);
        m_CursorX = x >= m_Cols ? m_Cols - 1 : x;
    }
}

void
Screen::BackTab(int count)
{
    while (count-- > 0)
    {
        int x = m_CursorX >= m_Cols ? m_Cols - 1 : m_CursorX;

        do
        {
            x--;
        }
        while (x > 0 && !m_TabStops[x]);
        m_CursorX = x < 0 ? 0 : x;
    }
}

void
Screen::EscDispatch(char32_t final)
{
    if (!m_Intermediate.empty())
    {
        char32_t intermediate = m_Intermediate[0];

        if (intermediate == U'(' || intermediate == U')')
        {
            bool graphics = final == U'0';

            if (intermediate == U'(')
                m_G0Graphics = graphics;
            else
                m_G1Graphics = graphics;
        }
        else if (intermediate == U'#' && final == U'8')
        {
            for (int row = 0; row < m_Rows; row++)
            {
                for (Cell &cell : Lines()[row].Cells)
                {
                    cell.Ch = U'E';
                    cell.Attr = Attributes();
                }
                MarkDirty(row);
            }
        }
        return;
    }

    switch (final)
    {
        case U'7':
            SaveCursor();
            break;
        case U'8':
            RestoreCursor();
            break;
        case U'D':
            LineFeed();
            break;
        case U'E':
            LineFeed();
            m_CursorX = 0;
            break;
        case U'H':
            if (m_CursorX < m_Cols)
                m_TabStops[m_CursorX] = true;
            break;
        case U'M':
            ReverseIndex();
            break;
        case U'c':
            Reset();
            break;
        case U'=':
            m_AppKeypad = true;
            break;
        case U'>':
            m_AppKeypad = false;
            break;
        default:
            break;
    }
}

void
Screen::Sgr()
{
    if (m_Params.empty())
    {
        m_Attr.Foreground = ColorDefault;
        m_Attr.Background = ColorDefault;
        m_Attr.Flags = 0;
        return;
    }

    for (size_t i = 0; i < m_Params.size(); i++)
    {
        int value = m_Params[i] < 0 ? 0 : m_Params[i];

        if (i > 0 && m_SubParam[i])
            continue;
        switch (value)
        {
            case 0:
                m_Attr.Foreground = ColorDefault;
                m_Attr.Background = ColorDefault;
                m_Attr.Flags = 0;
                break;
            case 1: m_Attr.Flags |= CellBold; break;
            case 2: m_Attr.Flags |= CellFaint; break;
            case 3: m_Attr.Flags |= CellItalic; break;
            case 4:
                m_Attr.Flags &= ~CellDoubleUnderline;
                if (i + 1 < m_Params.size() && m_SubParam[i + 1] && m_Params[i + 1] == 0)
                    m_Attr.Flags &= ~CellUnderline;
                else
                    m_Attr.Flags |= CellUnderline;
                break;
            case 5:
            case 6: m_Attr.Flags |= CellBlink; break;
            case 7: m_Attr.Flags |= CellInverse; break;
            case 8: m_Attr.Flags |= CellHidden; break;
            case 9: m_Attr.Flags |= CellStrike; break;
            case 21: m_Attr.Flags |= CellDoubleUnderline; break;
            case 22: m_Attr.Flags &= ~(CellBold | CellFaint); break;
            case 23: m_Attr.Flags &= ~CellItalic; break;
            case 24: m_Attr.Flags &= ~(CellUnderline | CellDoubleUnderline); break;
            case 25: m_Attr.Flags &= ~CellBlink; break;
            case 27: m_Attr.Flags &= ~CellInverse; break;
            case 28: m_Attr.Flags &= ~CellHidden; break;
            case 29: m_Attr.Flags &= ~CellStrike; break;
            case 39: m_Attr.Foreground = ColorDefault; break;
            case 49: m_Attr.Background = ColorDefault; break;
            case 38:
            case 48:
            {
                uint32_t color = ColorDefault;
                size_t next = i + 1;
                bool colon = next < m_Params.size() && m_SubParam[next];
                int kind = next < m_Params.size() ? m_Params[next] : -1;

                if (kind == 5 && next + 1 < m_Params.size())
                {
                    color = IndexedColor((unsigned)(m_Params[next + 1] < 0 ? 0 : m_Params[next + 1]));
                    i = next + 1;
                }
                else if (kind == 2)
                {
                    size_t first = next + 1;

                    /* The colon form may carry a color space id before the components. */
                    if (colon && first + 3 < m_Params.size() && m_SubParam[first + 3])
                        first++;
                    if (first + 2 < m_Params.size())
                    {
                        color = RgbColor((unsigned)(m_Params[first] < 0 ? 0 : m_Params[first]),
                                         (unsigned)(m_Params[first + 1] < 0 ? 0 : m_Params[first + 1]),
                                         (unsigned)(m_Params[first + 2] < 0 ? 0 : m_Params[first + 2]));
                        i = first + 2;
                    }
                    else
                    {
                        i = m_Params.size();
                    }
                }
                else
                {
                    i = m_Params.size();
                }
                if (value == 38)
                    m_Attr.Foreground = color;
                else
                    m_Attr.Background = color;
                break;
            }
            default:
                if (value >= 30 && value <= 37)
                    m_Attr.Foreground = IndexedColor(value - 30);
                else if (value >= 40 && value <= 47)
                    m_Attr.Background = IndexedColor(value - 40);
                else if (value >= 90 && value <= 97)
                    m_Attr.Foreground = IndexedColor(value - 90 + 8);
                else if (value >= 100 && value <= 107)
                    m_Attr.Background = IndexedColor(value - 100 + 8);
                break;
        }
    }
}

void
Screen::SetMode(bool privateMode, bool set)
{
    for (size_t i = 0; i < m_Params.size(); i++)
    {
        int mode = m_Params[i];

        if (!privateMode)
        {
            if (mode == 4)
                m_InsertMode = set;
            else if (mode == 20)
                m_NewLineMode = set;
            continue;
        }
        switch (mode)
        {
            case 1:
                m_AppCursorKeys = set;
                break;
            case 3:
                EraseInDisplay(2);
                MoveCursor(0, 0);
                break;
            case 5:
                m_ReverseVideo = set;
                MarkAllDirty();
                break;
            case 6:
                m_OriginMode = set;
                MoveCursor(0, m_OriginMode ? m_Top : 0);
                break;
            case 7:
                m_AutoWrap = set;
                break;
            case 12:
                m_CursorBlink = set;
                break;
            case 25:
                m_CursorVisible = set;
                MarkDirty(m_CursorY);
                break;
            case 47:
            case 1047:
                SwitchAlternate(set, false, set && mode == 1047);
                break;
            case 1048:
                if (set)
                    SaveCursor();
                else
                    RestoreCursor();
                break;
            case 1049:
                SwitchAlternate(set, true, true);
                break;
            case 9:
            case 1000:
            case 1002:
            case 1003:
                m_MouseMode = set ? mode : 0;
                break;
            case 1006:
                m_SgrMouse = set;
                break;
            case 2004:
                m_BracketedPaste = set;
                break;
            default:
                break;
        }
    }
}

void
Screen::CsiDispatch(char32_t final)
{
    char buffer[64];
    int count = Param(0, 1);

    if (count == 0)
        count = 1;

    if (!m_Intermediate.empty())
    {
        if (m_Intermediate == U" " && final == U'q')
        {
            switch (Param(0, 0))
            {
                case 0: m_CursorShape = CursorShape::Default; m_CursorBlink = true; break;
                case 1: m_CursorShape = CursorShape::Block; m_CursorBlink = true; break;
                case 2: m_CursorShape = CursorShape::Block; m_CursorBlink = false; break;
                case 3: m_CursorShape = CursorShape::Underline; m_CursorBlink = true; break;
                case 4: m_CursorShape = CursorShape::Underline; m_CursorBlink = false; break;
                case 5: m_CursorShape = CursorShape::Bar; m_CursorBlink = true; break;
                case 6: m_CursorShape = CursorShape::Bar; m_CursorBlink = false; break;
            }
            MarkDirty(m_CursorY);
        }
        else if (m_Intermediate == U"!" && final == U'p')
        {
            SoftReset();
        }
        return;
    }

    if (m_Private == U'?')
    {
        if (final == U'h' || final == U'l')
            SetMode(true, final == U'h');
        else if (final == U'n' && Param(0, 0) == 6)
        {
            snprintf(buffer, sizeof(buffer), "\x1b[?%d;%dR", m_CursorY + 1, CursorX() + 1);
            AddResponse(buffer);
        }
        return;
    }
    if (m_Private == U'>')
    {
        if (final == U'c')
            AddResponse("\x1b[>0;10;1c");
        return;
    }
    if (m_Private)
        return;

    switch (final)
    {
        case U'@':
            InsertCells(count);
            break;
        case U'A':
        {
            int limit = m_CursorY >= m_Top ? m_Top : 0;

            m_CursorY -= count;
            if (m_CursorY < limit)
                m_CursorY = limit;
            if (m_CursorX >= m_Cols)
                m_CursorX = m_Cols - 1;
            break;
        }
        case U'B':
        case U'e':
        {
            int limit = m_CursorY <= m_Bottom ? m_Bottom : m_Rows - 1;

            m_CursorY += count;
            if (m_CursorY > limit)
                m_CursorY = limit;
            if (m_CursorX >= m_Cols)
                m_CursorX = m_Cols - 1;
            break;
        }
        case U'C':
        case U'a':
            if (m_CursorX >= m_Cols)
                m_CursorX = m_Cols - 1;
            m_CursorX += count;
            if (m_CursorX > m_Cols - 1)
                m_CursorX = m_Cols - 1;
            break;
        case U'D':
            if (m_CursorX >= m_Cols)
                m_CursorX = m_Cols - 1;
            m_CursorX -= count;
            if (m_CursorX < 0)
                m_CursorX = 0;
            break;
        case U'E':
            m_CursorY += count;
            m_CursorX = 0;
            if (m_CursorY > m_Bottom)
                m_CursorY = m_Bottom;
            break;
        case U'F':
            m_CursorY -= count;
            m_CursorX = 0;
            if (m_CursorY < m_Top)
                m_CursorY = m_Top;
            break;
        case U'G':
        case U'`':
            m_CursorX = Param(0, 1) - 1;
            ClampCursor();
            break;
        case U'H':
        case U'f':
        {
            int row = Param(0, 1), col = Param(1, 1);

            if (row == 0)
                row = 1;
            if (col == 0)
                col = 1;
            MoveCursor(col - 1, (m_OriginMode ? m_Top : 0) + row - 1);
            break;
        }
        case U'I':
            Tab(count);
            break;
        case U'J':
            EraseInDisplay(Param(0, 0));
            break;
        case U'K':
            EraseInLine(Param(0, 0));
            break;
        case U'L':
            InsertLines(count);
            break;
        case U'M':
            DeleteLines(count);
            break;
        case U'P':
            DeleteCells(count);
            break;
        case U'S':
            ScrollUp(m_Top, m_Bottom, count);
            break;
        case U'T':
            ScrollDown(m_Top, m_Bottom, count);
            break;
        case U'X':
        {
            int x = m_CursorX >= m_Cols ? m_Cols - 1 : m_CursorX;
            EraseCells(m_CursorY, x, x + count);
            break;
        }
        case U'Z':
            BackTab(count);
            break;
        case U'b':
            if (m_LastPrinted)
            {
                for (int i = 0; i < count && i < m_Cols * m_Rows; i++)
                    Print(m_LastPrinted);
            }
            break;
        case U'c':
            if (Param(0, 0) == 0)
                AddResponse("\x1b[?62;22c");
            break;
        case U'd':
            MoveCursor(m_CursorX >= m_Cols ? m_Cols - 1 : m_CursorX, (m_OriginMode ? m_Top : 0) + Param(0, 1) - 1);
            break;
        case U'g':
            if (Param(0, 0) == 0)
            {
                if (m_CursorX < m_Cols)
                    m_TabStops[m_CursorX] = false;
            }
            else if (Param(0, 0) == 3)
            {
                m_TabStops.assign(m_Cols, false);
            }
            break;
        case U'h':
        case U'l':
            SetMode(false, final == U'h');
            break;
        case U'm':
            Sgr();
            break;
        case U'n':
            if (Param(0, 0) == 5)
            {
                AddResponse("\x1b[0n");
            }
            else if (Param(0, 0) == 6)
            {
                snprintf(buffer, sizeof(buffer), "\x1b[%d;%dR", m_CursorY + 1 - (m_OriginMode ? m_Top : 0),
                         CursorX() + 1);
                AddResponse(buffer);
            }
            break;
        case U'r':
        {
            int top = Param(0, 1), bottom = Param(1, m_Rows);

            if (top == 0)
                top = 1;
            if (bottom == 0 || bottom > m_Rows)
                bottom = m_Rows;
            if (top < bottom)
            {
                m_Top = top - 1;
                m_Bottom = bottom - 1;
                MoveCursor(0, m_OriginMode ? m_Top : 0);
            }
            break;
        }
        case U's':
            SaveCursor();
            break;
        case U'u':
            RestoreCursor();
            break;
        case U't':
            if (Param(0, 0) == 18)
            {
                snprintf(buffer, sizeof(buffer), "\x1b[8;%d;%dt", m_Rows, m_Cols);
                AddResponse(buffer);
            }
            break;
        default:
            break;
    }
}

void
Screen::OscDispatch()
{
    size_t separator = m_OscText.find(U';');
    int command = 0;
    std::basic_string<char32_t> argument;

    for (size_t i = 0; i < m_OscText.size() && i < separator; i++)
    {
        char32_t ch = m_OscText[i];

        if (ch < U'0' || ch > U'9')
            return;
        command = command * 10 + (int)(ch - U'0');
    }
    if (separator != std::basic_string<char32_t>::npos)
        argument = m_OscText.substr(separator + 1);

    switch (command)
    {
        case 0:
        case 2:
            if (argument != m_Title)
            {
                m_Title = argument;
                m_TitleChanged = true;
            }
            break;
        case 4:
        {
            size_t position = 0;

            while (position < argument.size())
            {
                size_t indexEnd = argument.find(U';', position);
                size_t colorEnd;
                unsigned index = 0;
                uint32_t color;

                if (indexEnd == std::basic_string<char32_t>::npos)
                    break;
                for (size_t i = position; i < indexEnd; i++)
                    index = index * 10 + (unsigned)(argument[i] - U'0');
                colorEnd = argument.find(U';', indexEnd + 1);
                if (colorEnd == std::basic_string<char32_t>::npos)
                    colorEnd = argument.size();
                if (index < 256 && ParseHexColor(argument.substr(indexEnd + 1, colorEnd - indexEnd - 1), &color))
                {
                    m_Palette[index] = color;
                    m_PaletteSet[index] = true;
                    MarkAllDirty();
                }
                position = colorEnd + 1;
            }
            break;
        }
        case 104:
            memset(m_PaletteSet, 0, sizeof(m_PaletteSet));
            MarkAllDirty();
            break;
        default:
            break;
    }
}

}
