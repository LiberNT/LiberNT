/*
 * PROJECT:     LiberNT Driver Installer
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Localized text and error messages
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "text.hpp"
#include "resource.h"

#include <wininet.h>
#include <setupapi.h>

#include <vector>

namespace lntdrv
{

std::wstring LoadText(UINT id)
{
    const WCHAR *text = NULL;
    int length = LoadStringW(GetModuleHandleW(NULL), id, (LPWSTR)&text, 0);

    return length > 0 && text ? std::wstring(text, length) : std::wstring();
}

namespace
{

std::wstring FormatArgs(UINT id, const std::wstring *args, size_t count)
{
    std::wstring pattern = LoadText(id), out;
    std::vector<DWORD_PTR> values;
    WCHAR *buffer = NULL;

    for (size_t i = 0; i < count; i++)
        values.push_back((DWORD_PTR)args[i].c_str());
    values.push_back(0);
    if (FormatMessageW(FORMAT_MESSAGE_FROM_STRING | FORMAT_MESSAGE_ARGUMENT_ARRAY | FORMAT_MESSAGE_ALLOCATE_BUFFER,
                       pattern.c_str(), 0, 0, (LPWSTR)&buffer, 0, (va_list *)values.data()) && buffer)
    {
        out = buffer;
        LocalFree(buffer);
    }
    return out.empty() ? pattern : out;
}

}

std::wstring FormatText(UINT id, const std::wstring &first)
{
    return FormatArgs(id, &first, 1);
}

std::wstring FormatText(UINT id, const std::wstring &first, const std::wstring &second)
{
    const std::wstring args[] = { first, second };

    return FormatArgs(id, args, _countof(args));
}

std::wstring FormatText(UINT id, const std::wstring &first, const std::wstring &second, const std::wstring &third)
{
    const std::wstring args[] = { first, second, third };

    return FormatArgs(id, args, _countof(args));
}

std::wstring FormatText(UINT id, const std::wstring &first, const std::wstring &second, const std::wstring &third,
                        const std::wstring &fourth)
{
    const std::wstring args[] = { first, second, third, fourth };

    return FormatArgs(id, args, _countof(args));
}

std::wstring NumberText(ULONGLONG value)
{
    WCHAR text[32];

    _snwprintf(text, _countof(text) - 1, L"%I64u", value);
    text[_countof(text) - 1] = L'\0';
    return text;
}

bool IsOfflineError(DWORD error)
{
    return error == ERROR_INTERNET_NAME_NOT_RESOLVED || error == ERROR_INTERNET_CANNOT_CONNECT ||
           error == ERROR_INTERNET_TIMEOUT || error == ERROR_INTERNET_CONNECTION_ABORTED ||
           error == ERROR_INTERNET_CONNECTION_RESET || error == ERROR_INTERNET_DISCONNECTED;
}

std::wstring ErrorText(DWORD error)
{
    WCHAR *text = NULL;
    std::wstring out;
    HMODULE module = NULL;
    WCHAR code[16];

    switch (error)
    {
        case ERROR_CRC:
            return LoadText(IDS_ERROR_CHECKSUM);
        case ERROR_BAD_NET_RESP:
            return LoadText(IDS_ERROR_SERVER);
        case ERROR_CANCELLED:
            return LoadText(IDS_ERROR_CANCELLED);
        case ERROR_NO_MORE_ITEMS:
            return LoadText(IDS_ERROR_NOT_BETTER);
        case ERROR_NO_COMPAT_DRIVERS:
            return LoadText(IDS_ERROR_NO_COMPAT);
        case ERROR_LNTDRV_CATALOG_FORMAT:
            return LoadText(IDS_ERROR_FORMAT);
        case ERROR_LNTDRV_PROGRAM_PACKAGE:
            return LoadText(IDS_ERROR_PROGRAM);
        case ERROR_LNTDRV_ALREADY_INSTALLED:
            return LoadText(IDS_ERROR_ALREADY);
        case ERROR_LNTDRV_NOT_NEWER:
            return LoadText(IDS_ERROR_NOT_NEWER);
        case ERROR_LNTDRV_NOT_COVERED:
            return LoadText(IDS_ERROR_NOT_COVERED);
        case ERROR_LNTDRV_NOT_IN_USE:
            return LoadText(IDS_ERROR_NOT_IN_USE);
        case ERROR_LNTDRV_NO_SIGNATURE:
            return LoadText(IDS_ERROR_NO_SIGNATURE);
        case ERROR_LNTDRV_OTHER_ARCH:
            return LoadText(IDS_ERROR_OTHER_ARCH);
        case ERROR_LNTDRV_OTHER_WINDOWS:
            return LoadText(IDS_NOTE_OTHER_WINDOWS);
    }
    if (IsOfflineError(error))
        return LoadText(IDS_ERROR_OFFLINE);
    if (error >= INTERNET_ERROR_BASE && error <= INTERNET_ERROR_LAST)
        module = GetModuleHandleW(L"wininet.dll");
    if (FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_IGNORE_INSERTS |
                       (module ? FORMAT_MESSAGE_FROM_HMODULE : FORMAT_MESSAGE_FROM_SYSTEM),
                       module, error, 0, (LPWSTR)&text, 0, NULL) && text)
    {
        out = text;
        LocalFree(text);
        while (!out.empty() && (out[out.size() - 1] == L'\n' || out[out.size() - 1] == L'\r' ||
                                out[out.size() - 1] == L' '))
            out.pop_back();
    }
    _snwprintf(code, _countof(code) - 1, L"0x%08lX", error);
    code[_countof(code) - 1] = L'\0';
    if ((error & 0xFFFF0000) == 0x800B0000 || (error & 0xFFFFF000) == 0x80096000)
        return FormatText(IDS_ERROR_SIGNATURE, out.empty() ? std::wstring(code) : out);
    if (out.empty())
        out = FormatText(IDS_ERROR_CODE, code);
    return out;
}

}
