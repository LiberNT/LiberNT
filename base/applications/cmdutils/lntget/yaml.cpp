/*
 * PROJECT:     LiberNT package manager
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     YAML reader for WinGet manifests and version data
 * COPYRIGHT:   Copyright 2026 Ostap Brehin <osbre@protonmail.com>
 */

#define NOMINMAX
#include <windows.h>

#include "yaml.hpp"
#include "util.hpp"

#include <algorithm>
#include <cassert>

namespace lntget
{

namespace
{

constexpr unsigned MaximumYamlDepth = 100;

bool IsBlankCharacter(char character)
{
    return character == ' ' || character == '\t';
}

bool IsBlankLine(const std::string &line)
{
    for (const char character : line)
    {
        if (!IsBlankCharacter(character))
            return false;
    }
    return true;
}

bool IsCommentLine(const std::string &line)
{
    std::size_t index = 0;
    while (index < line.size() && IsBlankCharacter(line[index]))
        ++index;
    return index < line.size() && line[index] == '#';
}

std::size_t IndentOf(const std::string &line)
{
    std::size_t indent = 0;
    while (indent < line.size() && line[indent] == ' ')
        ++indent;
    return indent;
}

bool IsSequenceEntry(const std::string &line, std::size_t indent)
{
    return indent < line.size() && line[indent] == '-' && (indent + 1 == line.size() || IsBlankCharacter(line[indent + 1]));
}

std::string TrimRightBlanks(const std::string &text)
{
    std::size_t end = text.size();
    while (end > 0 && IsBlankCharacter(text[end - 1]))
        --end;
    return text.substr(0, end);
}

std::size_t PlainScalarEnd(const std::string &line, std::size_t start)
{
    for (std::size_t index = start + 1; index < line.size(); ++index)
    {
        if (line[index] == '#' && IsBlankCharacter(line[index - 1]))
            return index;
    }
    return line.size();
}

void AppendUtf8(std::string &text, uint32_t code)
{
    assert(code <= 0x10ffff);
    if (code < 0x80)
    {
        text.push_back(static_cast<char>(code));
    }
    else if (code < 0x800)
    {
        text.push_back(static_cast<char>(0xc0 | (code >> 6)));
        text.push_back(static_cast<char>(0x80 | (code & 0x3f)));
    }
    else if (code < 0x10000)
    {
        text.push_back(static_cast<char>(0xe0 | (code >> 12)));
        text.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3f)));
        text.push_back(static_cast<char>(0x80 | (code & 0x3f)));
    }
    else
    {
        text.push_back(static_cast<char>(0xf0 | (code >> 18)));
        text.push_back(static_cast<char>(0x80 | ((code >> 12) & 0x3f)));
        text.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3f)));
        text.push_back(static_cast<char>(0x80 | (code & 0x3f)));
    }
}

bool ReadHexDigits(const std::string &line, std::size_t &column, std::size_t digits, uint32_t &code)
{
    code = 0;
    for (std::size_t index = 0; index < digits; ++index, ++column)
    {
        if (column >= line.size())
            return false;
        const char digit = line[column];
        uint32_t value = 0;
        if (digit >= '0' && digit <= '9') value = static_cast<uint32_t>(digit - '0');
        else if (digit >= 'a' && digit <= 'f') value = static_cast<uint32_t>(digit - 'a' + 10);
        else if (digit >= 'A' && digit <= 'F') value = static_cast<uint32_t>(digit - 'A' + 10);
        else return false;
        code = code * 16 + value;
    }
    return true;
}

class YamlReader
{
public:
    explicit YamlReader(const std::string &text)
    {
        std::size_t offset = 0;
        while (offset <= text.size())
        {
            const std::size_t end = text.find('\n', offset);
            std::string line(text, offset, end == std::string::npos ? std::string::npos : end - offset);
            if (!line.empty() && line[line.size() - 1] == '\r')
                line.pop_back();
            lines_.push_back(line);
            if (end == std::string::npos)
                break;
            offset = end + 1;
        }
        if (!lines_.empty() && lines_[0].compare(0, 3, "\xEF\xBB\xBF") == 0)
            lines_[0].erase(0, 3);
    }

    Status Read(YamlNode &root)
    {
        root = YamlNode();
        while (line_ < lines_.size() && (IsBlankLine(lines_[line_]) || IsCommentLine(lines_[line_]) ||
                                         lines_[line_].compare(0, 1, "%") == 0))
            ++line_;
        if (line_ < lines_.size() && IsDocumentMarker(lines_[line_], "---"))
        {
            if (lines_[line_] != "---")
            {
                Fail("content after the document start marker is not supported");
                return error_;
            }
            ++line_;
        }
        end_ = line_;
        while (end_ < lines_.size() && !IsDocumentMarker(lines_[end_], "---") && !IsDocumentMarker(lines_[end_], "..."))
            ++end_;
        if (!ParseNode(-1, 0, root))
            return error_;
        SkipBlankLines();
        if (!AtEnd())
        {
            Fail("unexpected content");
            return error_;
        }
        return Status::Ok();
    }

private:
    std::vector<std::string> lines_;
    std::size_t line_ = 0;
    std::size_t end_ = 0;
    bool failed_ = false;
    Status error_;

    static bool IsDocumentMarker(const std::string &line, const char *marker)
    {
        return line.compare(0, 3, marker) == 0 && (line.size() == 3 || IsBlankCharacter(line[3]));
    }

    bool Fail(const std::string &message)
    {
        if (!failed_)
        {
            failed_ = true;
            error_ = Status::Fail(ERROR_BAD_FORMAT, "YAML line " + DecimalString(line_ + 1) + ": " + message);
        }
        return false;
    }

    bool AtEnd() const
    {
        return line_ >= end_;
    }

    void SkipBlankLines()
    {
        while (!AtEnd() && (IsBlankLine(lines_[line_]) || IsCommentLine(lines_[line_])))
            ++line_;
    }

    bool CheckIndentation(const std::string &line, std::size_t indent)
    {
        if (indent < line.size() && line[indent] == '\t')
            return Fail("a tab is used for indentation");
        return true;
    }

    bool FinishLine(std::size_t line, std::size_t column)
    {
        const std::string &text = lines_[line];
        while (column < text.size() && IsBlankCharacter(text[column]))
            ++column;
        if (column < text.size() && text[column] != '#')
            return Fail("unexpected text after a value");
        return true;
    }

    bool ReadQuoted(std::size_t &line, std::size_t &column, std::string &value, std::string &message) const
    {
        assert(line < end_ && column < lines_[line].size());
        const char quote = lines_[line][column];
        assert(quote == '"' || quote == '\'');
        ++column;
        value.clear();
        for (;;)
        {
            const std::string &text = lines_[line];
            std::size_t keep = value.size();
            bool escapedBreak = false;
            while (column < text.size())
            {
                const char character = text[column];
                if (character == quote)
                {
                    if (quote == '\'' && column + 1 < text.size() && text[column + 1] == '\'')
                    {
                        value.push_back('\'');
                        column += 2;
                        keep = value.size();
                        continue;
                    }
                    ++column;
                    return true;
                }
                if (quote == '"' && character == '\\')
                {
                    if (column + 1 == text.size())
                    {
                        escapedBreak = true;
                        column = text.size();
                        break;
                    }
                    const char escape = text[column + 1];
                    column += 2;
                    uint32_t code = 0;
                    std::size_t digits = 0;
                    switch (escape)
                    {
                        case '0': code = 0; break;
                        case 'a': code = 7; break;
                        case 'b': code = 8; break;
                        case 't': case '\t': code = 9; break;
                        case 'n': code = 10; break;
                        case 'v': code = 11; break;
                        case 'f': code = 12; break;
                        case 'r': code = 13; break;
                        case 'e': code = 27; break;
                        case ' ': case '"': case '/': case '\\': code = static_cast<unsigned char>(escape); break;
                        case 'N': code = 0x85; break;
                        case '_': code = 0xa0; break;
                        case 'L': code = 0x2028; break;
                        case 'P': code = 0x2029; break;
                        case 'x': digits = 2; break;
                        case 'u': digits = 4; break;
                        case 'U': digits = 8; break;
                        default:
                            message = std::string("invalid escape \\") + escape + " in a double-quoted scalar";
                            return false;
                    }
                    if (digits && !ReadHexDigits(text, column, digits, code))
                    {
                        message = "invalid hexadecimal escape in a double-quoted scalar";
                        return false;
                    }
                    if (code >= 0xd800 && code < 0xdc00 && digits == 4 && column + 1 < text.size() &&
                        text[column] == '\\' && text[column + 1] == 'u')
                    {
                        std::size_t lowColumn = column + 2;
                        uint32_t low = 0;
                        if (ReadHexDigits(text, lowColumn, 4, low) && low >= 0xdc00 && low < 0xe000)
                        {
                            code = 0x10000 + ((code - 0xd800) << 10) + (low - 0xdc00);
                            column = lowColumn;
                        }
                    }
                    if ((code >= 0xd800 && code < 0xe000) || code > 0x10ffff)
                    {
                        message = "invalid Unicode escape in a double-quoted scalar";
                        return false;
                    }
                    AppendUtf8(value, code);
                    keep = value.size();
                    continue;
                }
                value.push_back(character);
                ++column;
                if (!IsBlankCharacter(character))
                    keep = value.size();
            }
            if (!escapedBreak)
                value.resize(keep);
            ++line;
            std::size_t blanks = 0;
            while (line < end_ && IsBlankLine(lines_[line]))
            {
                ++blanks;
                ++line;
            }
            if (line >= end_)
            {
                message = "unterminated quoted scalar";
                return false;
            }
            if (escapedBreak || blanks)
                value.append(blanks, '\n');
            else
                value.push_back(' ');
            column = 0;
            while (column < lines_[line].size() && IsBlankCharacter(lines_[line][column]))
                ++column;
        }
    }

    bool FindKey(const std::string &line, std::size_t start, std::string &key, std::size_t &valueColumn) const
    {
        assert(start < line.size());
        if (line[start] == '"' || line[start] == '\'')
        {
            std::size_t keyLine = line_;
            std::size_t column = start;
            std::string decoded;
            std::string message;
            if (!ReadQuoted(keyLine, column, decoded, message) || keyLine != line_)
                return false;
            while (column < line.size() && IsBlankCharacter(line[column]))
                ++column;
            if (column >= line.size() || line[column] != ':' || (column + 1 < line.size() && !IsBlankCharacter(line[column + 1])))
                return false;
            key = decoded;
            valueColumn = column + 1;
            return true;
        }
        if (line[start] == '[' || line[start] == '{' || line[start] == '#')
            return false;
        for (std::size_t index = start; index < line.size(); ++index)
        {
            if (line[index] == '#' && index > start && IsBlankCharacter(line[index - 1]))
                return false;
            if (line[index] == ':' && (index + 1 == line.size() || IsBlankCharacter(line[index + 1])))
            {
                key = TrimRightBlanks(line.substr(start, index - start));
                valueColumn = index + 1;
                return !key.empty();
            }
        }
        return false;
    }

    bool ParseNode(long parentIndent, unsigned depth, YamlNode &node)
    {
        if (depth > MaximumYamlDepth)
            return Fail("nesting is too deep");
        SkipBlankLines();
        node = YamlNode();
        if (AtEnd())
            return true;
        const std::string &line = lines_[line_];
        const std::size_t indent = IndentOf(line);
        if (static_cast<long>(indent) <= parentIndent)
            return true;
        if (!CheckIndentation(line, indent))
            return false;
        if (IsSequenceEntry(line, indent))
            return ParseSequence(indent, depth, node);
        std::string key;
        std::size_t valueColumn = 0;
        if (FindKey(line, indent, key, valueColumn))
            return ParseMapping(indent, depth, node);
        return ParseValue(indent, parentIndent, depth, node);
    }

    bool ParseSequence(std::size_t indent, unsigned depth, YamlNode &node)
    {
        node.kind = YamlNode::Kind::Sequence;
        for (;;)
        {
            SkipBlankLines();
            if (AtEnd())
                return true;
            std::string &line = lines_[line_];
            const std::size_t lineIndent = IndentOf(line);
            if (lineIndent < indent)
                return true;
            if (lineIndent > indent)
                return Fail("unexpected indentation in a sequence");
            if (!CheckIndentation(line, lineIndent))
                return false;
            if (!IsSequenceEntry(line, lineIndent))
                return true;
            node.items.emplace_back();
            YamlNode &item = node.items.back();
            std::size_t column = lineIndent + 1;
            while (column < line.size() && IsBlankCharacter(line[column]))
                ++column;
            if (column == line.size() || line[column] == '#')
                ++line_;
            else
                line.replace(lineIndent, column - lineIndent, column - lineIndent, ' ');
            if (!ParseNode(static_cast<long>(indent), depth + 1, item))
                return false;
        }
    }

    bool ParseMapping(std::size_t indent, unsigned depth, YamlNode &node)
    {
        node.kind = YamlNode::Kind::Mapping;
        for (;;)
        {
            SkipBlankLines();
            if (AtEnd())
                return true;
            const std::string &line = lines_[line_];
            const std::size_t lineIndent = IndentOf(line);
            if (lineIndent < indent)
                return true;
            if (lineIndent > indent)
                return Fail("unexpected indentation in a mapping");
            if (!CheckIndentation(line, lineIndent))
                return false;
            std::string key;
            std::size_t column = 0;
            if (!FindKey(line, lineIndent, key, column))
                return Fail("expected a mapping key");
            for (const auto &member : node.members)
            {
                if (member.first == key)
                    return Fail("duplicate key " + key);
            }
            node.members.emplace_back(key, YamlNode());
            YamlNode &value = node.members.back().second;
            while (column < line.size() && IsBlankCharacter(line[column]))
                ++column;
            bool parsed = false;
            if (column == line.size() || line[column] == '#')
            {
                ++line_;
                SkipBlankLines();
                if (!AtEnd() && IndentOf(lines_[line_]) == indent && IsSequenceEntry(lines_[line_], indent))
                    parsed = ParseSequence(indent, depth + 1, value);
                else
                    parsed = ParseNode(static_cast<long>(indent), depth + 1, value);
            }
            else
            {
                parsed = ParseValue(column, static_cast<long>(indent), depth + 1, value);
            }
            if (!parsed)
                return false;
        }
    }

    bool ParseValue(std::size_t column, long parentIndent, unsigned depth, YamlNode &node)
    {
        if (depth > MaximumYamlDepth)
            return Fail("nesting is too deep");
        const std::string &line = lines_[line_];
        assert(column < line.size());
        const char first = line[column];
        if (first == '|' || first == '>')
            return ParseBlockScalar(column, parentIndent, node);
        if (first == '"' || first == '\'')
        {
            std::size_t endLine = line_;
            std::string message;
            node = YamlNode();
            node.kind = YamlNode::Kind::Scalar;
            if (!ReadQuoted(endLine, column, node.scalar, message))
                return Fail(message);
            line_ = endLine;
            if (!FinishLine(line_, column))
                return false;
            ++line_;
            return true;
        }
        if (first == '[' || first == '{')
        {
            std::size_t endLine = line_;
            if (!ParseFlow(endLine, column, depth, node))
                return false;
            line_ = endLine;
            if (!FinishLine(line_, column))
                return false;
            ++line_;
            return true;
        }
        if (first == '&' || first == '*')
            return Fail("anchors and aliases are not supported");
        if (first == '!')
            return Fail("tags are not supported");
        if (first == '@' || first == '`')
            return Fail("a plain scalar starts with a reserved character");
        return ParsePlain(column, parentIndent, node);
    }

    bool ParsePlain(std::size_t column, long parentIndent, YamlNode &node)
    {
        node = YamlNode();
        node.kind = YamlNode::Kind::Scalar;
        const std::string &first = lines_[line_];
        const std::size_t firstEnd = PlainScalarEnd(first, column);
        node.scalar = TrimRightBlanks(first.substr(column, firstEnd - column));
        ++line_;
        if (firstEnd < first.size())
            return true;
        std::size_t scan = line_;
        std::size_t blanks = 0;
        while (scan < end_)
        {
            const std::string &line = lines_[scan];
            if (IsBlankLine(line))
            {
                ++blanks;
                ++scan;
                continue;
            }
            if (static_cast<long>(IndentOf(line)) <= parentIndent)
                break;
            std::size_t start = 0;
            while (start < line.size() && IsBlankCharacter(line[start]))
                ++start;
            if (line[start] == '#')
                break;
            const std::size_t end = PlainScalarEnd(line, start);
            if (blanks)
                node.scalar.append(blanks, '\n');
            else
                node.scalar.push_back(' ');
            node.scalar += TrimRightBlanks(line.substr(start, end - start));
            blanks = 0;
            line_ = ++scan;
            if (end < line.size())
                break;
        }
        return true;
    }

    bool ParseBlockScalar(std::size_t column, long parentIndent, YamlNode &node)
    {
        const std::string &header = lines_[line_];
        const bool folded = header[column] == '>';
        std::size_t index = column + 1;
        char chomping = 0;
        std::size_t explicitIndent = 0;
        for (int indicator = 0; indicator < 2 && index < header.size(); ++indicator)
        {
            if (!chomping && (header[index] == '+' || header[index] == '-'))
                chomping = header[index++];
            else if (!explicitIndent && header[index] >= '1' && header[index] <= '9')
                explicitIndent = static_cast<std::size_t>(header[index++] - '0');
            else
                break;
        }
        if (index < header.size() && !IsBlankCharacter(header[index]))
            return Fail("invalid block scalar header");
        if (!FinishLine(line_, index))
            return false;
        ++line_;

        const std::size_t minimumIndent = static_cast<std::size_t>(parentIndent + 1);
        std::size_t contentIndent = (parentIndent < 0 ? 0 : static_cast<std::size_t>(parentIndent)) + explicitIndent;
        if (!explicitIndent)
        {
            contentIndent = minimumIndent;
            for (std::size_t scan = line_; scan < end_; ++scan)
            {
                if (!IsBlankLine(lines_[scan]))
                {
                    contentIndent = std::max(IndentOf(lines_[scan]), minimumIndent);
                    break;
                }
            }
        }

        std::vector<std::string> body;
        while (!AtEnd())
        {
            const std::string &line = lines_[line_];
            if (IsBlankLine(line))
            {
                body.push_back(line.size() > contentIndent ? line.substr(contentIndent) : std::string());
                ++line_;
                continue;
            }
            if (IndentOf(line) < contentIndent)
                break;
            body.push_back(line.substr(contentIndent));
            ++line_;
        }
        std::size_t contentLines = body.size();
        while (contentLines > 0 && IsBlankLine(body[contentLines - 1]))
            --contentLines;

        node = YamlNode();
        node.kind = YamlNode::Kind::Scalar;
        std::string &text = node.scalar;
        if (!folded)
        {
            for (std::size_t line = 0; line < contentLines; ++line)
            {
                text += body[line];
                text.push_back('\n');
            }
        }
        else
        {
            bool started = false;
            bool previousMoreIndented = false;
            std::size_t blanks = 0;
            for (std::size_t line = 0; line < contentLines; ++line)
            {
                const std::string &part = body[line];
                if (part.empty())
                {
                    ++blanks;
                    continue;
                }
                const bool moreIndented = IsBlankCharacter(part[0]);
                if (!started)
                    text.append(blanks, '\n');
                else if (moreIndented || previousMoreIndented)
                    text.append(blanks + 1, '\n');
                else if (blanks)
                    text.append(blanks, '\n');
                else
                    text.push_back(' ');
                text += part;
                started = true;
                blanks = 0;
                previousMoreIndented = moreIndented;
            }
            if (contentLines)
                text.push_back('\n');
        }
        if (chomping == '-')
        {
            while (!text.empty() && text[text.size() - 1] == '\n')
                text.pop_back();
        }
        else if (chomping == '+')
        {
            text.append(body.size() - contentLines, '\n');
        }
        else if (!contentLines)
        {
            text.clear();
        }
        return true;
    }

    bool SkipFlowSpace(std::size_t &line, std::size_t &column)
    {
        for (;;)
        {
            const std::string &text = lines_[line];
            while (column < text.size() && IsBlankCharacter(text[column]))
                ++column;
            if (column < text.size() && text[column] == '#' && (column == 0 || IsBlankCharacter(text[column - 1])))
                column = text.size();
            if (column < text.size())
                return true;
            if (++line >= end_)
            {
                line_ = end_ - 1;
                return Fail("unterminated flow collection");
            }
            column = 0;
        }
    }

    bool ParseFlowScalar(std::size_t &line, std::size_t &column, YamlNode &node)
    {
        node = YamlNode();
        node.kind = YamlNode::Kind::Scalar;
        const char first = lines_[line][column];
        if (first == '"' || first == '\'')
        {
            std::string message;
            if (!ReadQuoted(line, column, node.scalar, message))
            {
                line_ = line;
                return Fail(message);
            }
            return true;
        }
        if (first == '&' || first == '*' || first == '!' || first == '@' || first == '`')
        {
            line_ = line;
            return Fail("unsupported character in a flow collection");
        }
        for (;;)
        {
            const std::string &text = lines_[line];
            const std::size_t start = column;
            while (column < text.size())
            {
                const char character = text[column];
                if (character == ',' || character == '[' || character == ']' || character == '{' || character == '}')
                    break;
                if (character == ':' && (column + 1 == text.size() || IsBlankCharacter(text[column + 1]) ||
                                         text[column + 1] == ',' || text[column + 1] == ']' || text[column + 1] == '}'))
                    break;
                if (character == '#' && column > start && IsBlankCharacter(text[column - 1]))
                    break;
                ++column;
            }
            if (!node.scalar.empty() && column > start)
                node.scalar.push_back(' ');
            node.scalar += TrimRightBlanks(text.substr(start, column - start));
            if (column < text.size() && text[column] != '#')
                break;
            std::size_t nextLine = line;
            std::size_t nextColumn = text.size();
            if (!SkipFlowSpace(nextLine, nextColumn))
                return false;
            const char next = lines_[nextLine][nextColumn];
            if (next == ',' || next == ']' || next == '}' || next == ':' || next == '[' || next == '{')
            {
                line = nextLine;
                column = nextColumn;
                break;
            }
            line = nextLine;
            column = nextColumn;
        }
        if (node.scalar.empty())
        {
            line_ = line;
            return Fail("empty entry in a flow collection");
        }
        return true;
    }

    bool ParseFlowNode(std::size_t &line, std::size_t &column, unsigned depth, YamlNode &node)
    {
        const char first = lines_[line][column];
        if (first == '[' || first == '{')
            return ParseFlow(line, column, depth + 1, node);
        return ParseFlowScalar(line, column, node);
    }

    bool ParseFlow(std::size_t &line, std::size_t &column, unsigned depth, YamlNode &node)
    {
        if (depth > MaximumYamlDepth)
            return Fail("nesting is too deep");
        const char open = lines_[line][column];
        assert(open == '[' || open == '{');
        const char close = open == '[' ? ']' : '}';
        node = YamlNode();
        node.kind = open == '[' ? YamlNode::Kind::Sequence : YamlNode::Kind::Mapping;
        ++column;
        for (;;)
        {
            if (!SkipFlowSpace(line, column))
                return false;
            if (lines_[line][column] == close)
            {
                ++column;
                return true;
            }
            if (node.IsSequence())
            {
                node.items.emplace_back();
                if (!ParseFlowNode(line, column, depth, node.items.back()))
                    return false;
            }
            else
            {
                YamlNode key;
                if (!ParseFlowScalar(line, column, key))
                    return false;
                for (const auto &member : node.members)
                {
                    if (member.first == key.scalar)
                    {
                        line_ = line;
                        return Fail("duplicate key " + key.scalar);
                    }
                }
                node.members.emplace_back(key.scalar, YamlNode());
                if (!SkipFlowSpace(line, column))
                    return false;
                if (lines_[line][column] == ':')
                {
                    ++column;
                    if (!SkipFlowSpace(line, column))
                        return false;
                    const char next = lines_[line][column];
                    if (next != ',' && next != '}' && !ParseFlowNode(line, column, depth, node.members.back().second))
                        return false;
                }
            }
            if (!SkipFlowSpace(line, column))
                return false;
            const char separator = lines_[line][column];
            if (separator == ',')
            {
                ++column;
                continue;
            }
            if (separator == close)
            {
                ++column;
                return true;
            }
            line_ = line;
            return Fail("expected a comma in a flow collection");
        }
    }
};

} // namespace

const YamlNode *YamlNode::Find(const std::string &key) const
{
    for (const auto &member : members)
    {
        if (member.first == key)
            return &member.second;
    }
    return nullptr;
}

Status ParseYaml(const std::string &text, YamlNode &root)
{
    YamlReader reader(text);
    return reader.Read(root);
}

Status RunYamlSelfTests()
{
    static constexpr char Fixture[] =
        "\xEF\xBB\xBF# comment\r\n"
        "Plain: one two\r\n"
        "  three   # trailing comment\r\n"
        "Double: \"C:\\\\Program Files\\\\A \\\"B\\\" \\xA9 \\u7D05 \\U0001F600 \\uD83D\\uDE00\"\n"
        "Single: 'it''s # not a comment'\n"
        "Folded: \"first\n"
        "  second\n"
        "\n"
        "  third\"\n"
        "Literal: |\n"
        "  line 1\n"
        "\n"
        "   indented\n"
        "\n"
        "Strip: >-\n"
        "  a\n"
        "  b\n"
        "\n"
        "  c\n"
        "Keep: |+\n"
        "  k\n"
        "\n"
        "Flow: [a, 'b, c', \"d\"]\n"
        "Empty: []\n"
        "EmptyMap: {}\n"
        "Null:\n"
        "Compact:\n"
        "- x\n"
        "- Key: v\n"
        "  Other: w\n"
        "- - nested\n"
        "Nested:\n"
        "  Child: -dash value\n"
        "  'Quoted Key': url://a:b\n";
    YamlNode root;
    Status status = ParseYaml(Fixture, root);
    if (!status)
        return status;
    const auto text = [&root](const char *key) {
        const YamlNode *node = root.Find(key);
        return node ? node->Text() : std::string("<missing>");
    };
    if (!root.IsMapping() || text("Plain") != "one two three" ||
        text("Double") != "C:\\Program Files\\A \"B\" \xC2\xA9 \xE7\xB4\x85 \xF0\x9F\x98\x80 \xF0\x9F\x98\x80" ||
        text("Single") != "it's # not a comment" || text("Folded") != "first second\nthird" ||
        text("Literal") != "line 1\n\n indented\n" || text("Strip") != "a b\nc" || text("Keep") != "k\n\n")
        return Status::Fail(ERROR_ASSERTION_FAILURE, "YAML scalar self-test failed");
    const YamlNode *flow = root.Find("Flow");
    const YamlNode *empty = root.Find("Empty");
    const YamlNode *emptyMap = root.Find("EmptyMap");
    const YamlNode *null = root.Find("Null");
    if (!flow || !flow->IsSequence() || flow->items.size() != 3 || flow->items[1].Text() != "b, c" ||
        flow->items[2].Text() != "d" || !empty || !empty->IsSequence() || !empty->items.empty() || !emptyMap ||
        !emptyMap->IsMapping() || !null || !null->IsNull())
        return Status::Fail(ERROR_ASSERTION_FAILURE, "YAML flow collection self-test failed");
    const YamlNode *compact = root.Find("Compact");
    if (!compact || !compact->IsSequence() || compact->items.size() != 3 || compact->items[0].Text() != "x" ||
        !compact->items[1].IsMapping() || compact->items[1].members.size() != 2 || !compact->items[1].Find("Other") ||
        compact->items[1].Find("Other")->Text() != "w" || !compact->items[2].IsSequence() ||
        compact->items[2].items.size() != 1 || compact->items[2].items[0].Text() != "nested")
        return Status::Fail(ERROR_ASSERTION_FAILURE, "YAML block sequence self-test failed");
    const YamlNode *nested = root.Find("Nested");
    if (!nested || !nested->Find("Child") || nested->Find("Child")->Text() != "-dash value" || !nested->Find("Quoted Key") ||
        nested->Find("Quoted Key")->Text() != "url://a:b")
        return Status::Fail(ERROR_ASSERTION_FAILURE, "YAML nested mapping self-test failed");

    static const char *const Invalid[] = {
        "A: 1\nA: 2\n",
        "A:\n\tB: 1\n",
        "A: \"open\n",
        "A: [a, b\n",
        "A: \"\\q\"\n",
        "A: 1\n  - b\nB:\n   C: 1\n  D: 2\n",
        "A: *alias\n",
        "A: 'x' y\n",
    };
    for (const char *text : Invalid)
    {
        if (ParseYaml(text, root))
            return Status::Fail(ERROR_ASSERTION_FAILURE, std::string("YAML self-test accepted invalid input: ") + text);
    }
    std::string deep;
    for (unsigned level = 0; level <= MaximumYamlDepth + 1; ++level)
        deep += std::string(level * 2, ' ') + "A:\n";
    if (ParseYaml(deep, root))
        return Status::Fail(ERROR_ASSERTION_FAILURE, "YAML depth limit self-test failed");
    return Status::Ok();
}

} // namespace lntget
