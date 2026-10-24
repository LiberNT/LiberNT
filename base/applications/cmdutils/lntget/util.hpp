/*
 * PROJECT:     ReactOS package manager
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     lntget utility declarations
 * COPYRIGHT:   Copyright 2026 Ahmed Arif
 */

#pragma once

#include "model.hpp"

namespace lntget
{

std::string Utf8FromWide(const std::wstring &value);
std::wstring WideFromUtf8(const std::string &value);
std::string Trim(const std::string &value);
std::string UnquoteYaml(const std::string &value);
std::string AsciiLower(const std::string &value);
bool AsciiEquals(const std::string &left, const std::string &right);
bool AsciiContains(const std::string &value, const std::string &needle);
bool AsciiStartsWith(const std::string &value, const std::string &prefix);
std::wstring UrlEncodePath(const std::string &path);
std::wstring QuoteCommandArgument(const std::wstring &value);
std::wstring JoinPath(const std::wstring &left, const std::wstring &right);
std::wstring ParentPath(const std::wstring &path);
std::wstring FileNameFromUrl(const std::string &url);
std::wstring LocalCacheDirectory();
std::wstring TemporaryDirectory();
Status EnsureDirectory(const std::wstring &path);
Status AtomicReplaceFile(const std::wstring &source, const std::wstring &destination, const std::string &description);
Status ReadFileBytes(const std::wstring &path, std::vector<uint8_t> &bytes,
                     std::size_t maximumSize = static_cast<std::size_t>(-1));
Status WriteFileBytes(const std::wstring &path, const std::vector<uint8_t> &bytes);
std::string WindowsErrorMessage(DWORD error);
std::string HexLower(const uint8_t *data, std::size_t size);
bool ParseHex256(const std::string &text, uint8_t (&value)[32]);
std::string DecimalString(unsigned long long value);
std::wstring DecimalWideString(unsigned long long value);

} // namespace lntget
