/*
 * PROJECT:     LiberNT Terminal
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Minimal JSON document model, reader and writer for the settings file
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "json.h"
#include "vt.h"

#include <stdio.h>
#include <stdlib.h>

namespace Json
{

namespace
{

class Reader
{
public:
    explicit Reader(const std::string &text) : m_Text(text) {}

    bool ParseDocument(Value &out)
    {
        SkipSpace();
        if (!ParseValue(out, 0))
            return false;
        SkipSpace();
        return m_Position == m_Text.size();
    }

    int Line() const
    {
        int line = 1;

        for (size_t i = 0; i < m_Position && i < m_Text.size(); i++)
        {
            if (m_Text[i] == '\n')
                line++;
        }
        return line;
    }

private:
    void SkipSpace()
    {
        while (m_Position < m_Text.size())
        {
            char ch = m_Text[m_Position];

            if (ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n')
            {
                m_Position++;
            }
            else if (ch == '/' && m_Position + 1 < m_Text.size() && m_Text[m_Position + 1] == '/')
            {
                while (m_Position < m_Text.size() && m_Text[m_Position] != '\n')
                    m_Position++;
            }
            else if (ch == '/' && m_Position + 1 < m_Text.size() && m_Text[m_Position + 1] == '*')
            {
                size_t end = m_Text.find("*/", m_Position + 2);
                m_Position = end == std::string::npos ? m_Text.size() : end + 2;
            }
            else
            {
                break;
            }
        }
    }

    bool Consume(char ch)
    {
        SkipSpace();
        if (m_Position < m_Text.size() && m_Text[m_Position] == ch)
        {
            m_Position++;
            return true;
        }
        return false;
    }

    bool ParseHex4(unsigned *value)
    {
        *value = 0;
        if (m_Position + 4 > m_Text.size())
            return false;
        for (int i = 0; i < 4; i++)
        {
            char ch = m_Text[m_Position++];
            unsigned digit;

            if (ch >= '0' && ch <= '9')
                digit = ch - '0';
            else if (ch >= 'a' && ch <= 'f')
                digit = ch - 'a' + 10;
            else if (ch >= 'A' && ch <= 'F')
                digit = ch - 'A' + 10;
            else
                return false;
            *value = *value * 16 + digit;
        }
        return true;
    }

    bool ParseString(std::basic_string<char32_t> &out)
    {
        std::string bytes;

        if (!Consume('"'))
            return false;
        while (m_Position < m_Text.size())
        {
            char ch = m_Text[m_Position++];

            if (ch == '"')
            {
                out = DecodeUtf8(bytes);
                return true;
            }
            if ((unsigned char)ch < 0x20)
                return false;
            if (ch != '\\')
            {
                bytes += ch;
                continue;
            }
            if (m_Position >= m_Text.size())
                return false;
            ch = m_Text[m_Position++];
            switch (ch)
            {
                case '"': bytes += '"'; break;
                case '\\': bytes += '\\'; break;
                case '/': bytes += '/'; break;
                case 'b': bytes += '\b'; break;
                case 'f': bytes += '\f'; break;
                case 'n': bytes += '\n'; break;
                case 'r': bytes += '\r'; break;
                case 't': bytes += '\t'; break;
                case 'u':
                {
                    unsigned code, low;

                    if (!ParseHex4(&code))
                        return false;
                    if (code >= 0xD800 && code <= 0xDBFF && m_Position + 6 <= m_Text.size() &&
                        m_Text[m_Position] == '\\' && m_Text[m_Position + 1] == 'u')
                    {
                        m_Position += 2;
                        if (!ParseHex4(&low) || low < 0xDC00 || low > 0xDFFF)
                            return false;
                        code = 0x10000 + ((code - 0xD800) << 10) + (low - 0xDC00);
                    }
                    bytes += Vt::EncodeUtf8(std::basic_string<char32_t>(1, (char32_t)code));
                    break;
                }
                default:
                    return false;
            }
        }
        return false;
    }

    static std::basic_string<char32_t> DecodeUtf8(const std::string &bytes)
    {
        std::basic_string<char32_t> out;

        for (size_t i = 0; i < bytes.size();)
        {
            unsigned char byte = (unsigned char)bytes[i];
            char32_t code;
            int extra;

            if (byte < 0x80)
            {
                code = byte;
                extra = 0;
            }
            else if ((byte & 0xE0) == 0xC0)
            {
                code = byte & 0x1F;
                extra = 1;
            }
            else if ((byte & 0xF0) == 0xE0)
            {
                code = byte & 0x0F;
                extra = 2;
            }
            else if ((byte & 0xF8) == 0xF0)
            {
                code = byte & 0x07;
                extra = 3;
            }
            else
            {
                out += 0xFFFD;
                i++;
                continue;
            }
            i++;
            for (int j = 0; j < extra; j++, i++)
            {
                if (i >= bytes.size() || ((unsigned char)bytes[i] & 0xC0) != 0x80)
                {
                    code = 0xFFFD;
                    break;
                }
                code = (code << 6) | ((unsigned char)bytes[i] & 0x3F);
            }
            out += code;
        }
        return out;
    }

    bool ParseNumber(Value &out)
    {
        size_t start = m_Position;
        std::string number;

        if (m_Position < m_Text.size() && (m_Text[m_Position] == '-' || m_Text[m_Position] == '+'))
            m_Position++;
        while (m_Position < m_Text.size())
        {
            char ch = m_Text[m_Position];

            if ((ch >= '0' && ch <= '9') || ch == '.' || ch == 'e' || ch == 'E' || ch == '-' || ch == '+')
                m_Position++;
            else
                break;
        }
        if (m_Position == start)
            return false;
        number = m_Text.substr(start, m_Position - start);
        out = Value(strtod(number.c_str(), NULL));
        return true;
    }

    bool ParseValue(Value &out, int depth)
    {
        if (depth > 64)
            return false;
        SkipSpace();
        if (m_Position >= m_Text.size())
            return false;

        char ch = m_Text[m_Position];
        if (ch == '{')
        {
            m_Position++;
            out = Value::MakeObject();
            if (Consume('}'))
                return true;
            for (;;)
            {
                std::basic_string<char32_t> key;
                Value member;

                SkipSpace();
                if (m_Position < m_Text.size() && m_Text[m_Position] == '}')
                {
                    m_Position++;
                    return true;
                }
                if (!ParseString(key) || !Consume(':') || !ParseValue(member, depth + 1))
                    return false;
                out.Set(key, member);
                if (Consume(','))
                    continue;
                return Consume('}');
            }
        }
        if (ch == '[')
        {
            m_Position++;
            out = Value::MakeArray();
            if (Consume(']'))
                return true;
            for (;;)
            {
                Value item;

                SkipSpace();
                if (m_Position < m_Text.size() && m_Text[m_Position] == ']')
                {
                    m_Position++;
                    return true;
                }
                if (!ParseValue(item, depth + 1))
                    return false;
                out.Append(item);
                if (Consume(','))
                    continue;
                return Consume(']');
            }
        }
        if (ch == '"')
        {
            std::basic_string<char32_t> text;

            if (!ParseString(text))
                return false;
            out = Value(text);
            return true;
        }
        if (m_Text.compare(m_Position, 4, "true") == 0)
        {
            m_Position += 4;
            out = Value(true);
            return true;
        }
        if (m_Text.compare(m_Position, 5, "false") == 0)
        {
            m_Position += 5;
            out = Value(false);
            return true;
        }
        if (m_Text.compare(m_Position, 4, "null") == 0)
        {
            m_Position += 4;
            out = Value();
            return true;
        }
        return ParseNumber(out);
    }

    const std::string &m_Text;
    size_t m_Position = 0;
};

void WriteString(std::string &out, const std::basic_string<char32_t> &text)
{
    out += '"';
    for (char32_t ch : text)
    {
        switch (ch)
        {
            case U'"': out += "\\\""; break;
            case U'\\': out += "\\\\"; break;
            case U'\n': out += "\\n"; break;
            case U'\r': out += "\\r"; break;
            case U'\t': out += "\\t"; break;
            default:
                if (ch < 0x20)
                {
                    char escape[8];
                    snprintf(escape, sizeof(escape), "\\u%04x", (unsigned)ch);
                    out += escape;
                }
                else
                {
                    out += Vt::EncodeUtf8(std::basic_string<char32_t>(1, ch));
                }
                break;
        }
    }
    out += '"';
}

void WriteValue(std::string &out, const Value &value, int indent)
{
    std::string pad((size_t)indent * 4, ' ');
    std::string inner((size_t)(indent + 1) * 4, ' ');

    switch (value.GetType())
    {
        case Value::Type::Null:
            out += "null";
            break;
        case Value::Type::Bool:
            out += value.AsBool() ? "true" : "false";
            break;
        case Value::Type::Number:
        {
            char number[64];
            double n = value.AsNumber();

            if (n == (double)(long long)n && n > -1e15 && n < 1e15)
                snprintf(number, sizeof(number), "%lld", (long long)n);
            else
                snprintf(number, sizeof(number), "%.6g", n);
            out += number;
            break;
        }
        case Value::Type::String:
            WriteString(out, value.AsString());
            break;
        case Value::Type::Array:
        {
            bool simple = true;

            for (size_t i = 0; i < value.Size(); i++)
            {
                if (value[i].IsArray() || value[i].IsObject())
                    simple = false;
            }
            if (value.Size() == 0)
            {
                out += "[]";
                break;
            }
            out += simple ? "[ " : "[\n";
            for (size_t i = 0; i < value.Size(); i++)
            {
                if (!simple)
                    out += inner;
                WriteValue(out, value[i], indent + 1);
                if (i + 1 < value.Size())
                    out += simple ? ", " : ",\n";
            }
            out += simple ? " ]" : "\n" + pad + "]";
            break;
        }
        case Value::Type::Object:
        {
            const auto &members = value.Members();

            if (members.empty())
            {
                out += "{}";
                break;
            }
            out += "{\n";
            for (size_t i = 0; i < members.size(); i++)
            {
                out += inner;
                WriteString(out, members[i].first);
                out += ": ";
                WriteValue(out, members[i].second, indent + 1);
                if (i + 1 < members.size())
                    out += ",";
                out += "\n";
            }
            out += pad + "}";
            break;
        }
    }
}

const Value NullValue;

}

const Value &
Value::operator[](size_t index) const
{
    if (m_Type == Type::Array && index < m_Items.size())
        return m_Items[index];
    if (m_Type == Type::Object && index < m_Members.size())
        return m_Members[index].second;
    return NullValue;
}

Value &
Value::Append(const Value &value)
{
    if (m_Type != Type::Array)
    {
        *this = MakeArray();
    }
    m_Items.push_back(value);
    return m_Items.back();
}

const Value *
Value::Find(const std::basic_string<char32_t> &key) const
{
    if (m_Type != Type::Object)
        return NULL;
    for (const auto &member : m_Members)
    {
        if (member.first == key)
            return &member.second;
    }
    return NULL;
}

Value &
Value::Set(const std::basic_string<char32_t> &key, const Value &value)
{
    if (m_Type != Type::Object)
        *this = MakeObject();
    for (auto &member : m_Members)
    {
        if (member.first == key)
        {
            member.second = value;
            return member.second;
        }
    }
    m_Members.push_back(std::make_pair(key, value));
    return m_Members.back().second;
}

void
Value::Remove(const std::basic_string<char32_t> &key)
{
    for (size_t i = 0; i < m_Members.size(); i++)
    {
        if (m_Members[i].first == key)
        {
            m_Members.erase(m_Members.begin() + i);
            return;
        }
    }
}

bool
Parse(const std::string &text, Value &out, int *errorLine)
{
    std::string body = text;

    if (body.size() >= 3 && (unsigned char)body[0] == 0xEF && (unsigned char)body[1] == 0xBB &&
        (unsigned char)body[2] == 0xBF)
    {
        body.erase(0, 3);
    }

    Reader reader(body);
    if (!reader.ParseDocument(out))
    {
        if (errorLine)
            *errorLine = reader.Line();
        return false;
    }
    return true;
}

std::string
Write(const Value &value)
{
    std::string out;

    WriteValue(out, value, 0);
    out += "\n";
    return out;
}

}
