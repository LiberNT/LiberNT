/*
 * PROJECT:     LiberNT Terminal
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Minimal JSON document model, reader and writer for the settings file
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace Json
{

class Value
{
public:
    enum class Type
    {
        Null,
        Bool,
        Number,
        String,
        Array,
        Object
    };

    Value() = default;
    explicit Value(bool value) : m_Type(Type::Bool), m_Bool(value) {}
    explicit Value(double value) : m_Type(Type::Number), m_Number(value) {}
    explicit Value(const std::basic_string<char32_t> &value) : m_Type(Type::String), m_String(value) {}
    static Value MakeArray() { Value v; v.m_Type = Type::Array; return v; }
    static Value MakeObject() { Value v; v.m_Type = Type::Object; return v; }

    Type GetType() const { return m_Type; }
    bool IsNull() const { return m_Type == Type::Null; }
    bool IsBool() const { return m_Type == Type::Bool; }
    bool IsNumber() const { return m_Type == Type::Number; }
    bool IsString() const { return m_Type == Type::String; }
    bool IsArray() const { return m_Type == Type::Array; }
    bool IsObject() const { return m_Type == Type::Object; }

    bool AsBool(bool fallback = false) const { return m_Type == Type::Bool ? m_Bool : fallback; }
    double AsNumber(double fallback = 0) const { return m_Type == Type::Number ? m_Number : fallback; }
    const std::basic_string<char32_t> &AsString() const { return m_String; }

    size_t Size() const { return m_Type == Type::Array ? m_Items.size() : m_Type == Type::Object ? m_Members.size() : 0; }
    const Value &operator[](size_t index) const;
    Value &Append(const Value &value);

    const Value *Find(const std::basic_string<char32_t> &key) const;
    Value &Set(const std::basic_string<char32_t> &key, const Value &value);
    void Remove(const std::basic_string<char32_t> &key);
    const std::vector<std::pair<std::basic_string<char32_t>, Value>> &Members() const { return m_Members; }

private:
    Type m_Type = Type::Null;
    bool m_Bool = false;
    double m_Number = 0;
    std::basic_string<char32_t> m_String;
    std::vector<Value> m_Items;
    std::vector<std::pair<std::basic_string<char32_t>, Value>> m_Members;
};

/* Parses UTF-8 text; comments and trailing commas are accepted. Returns false and the
   one-based line of the error when the text is not valid. */
bool Parse(const std::string &text, Value &out, int *errorLine);
std::string Write(const Value &value);

}
