/*
 * PROJECT:     LiberNT Driver Installer
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Localized text and error messages
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <string>

namespace lntdrv
{

const DWORD ERROR_LNTDRV_CATALOG_FORMAT = 0x20000001;
const DWORD ERROR_LNTDRV_PROGRAM_PACKAGE = 0x20000002;
const DWORD ERROR_LNTDRV_ALREADY_INSTALLED = 0x20000003;
const DWORD ERROR_LNTDRV_NOT_NEWER = 0x20000004;
const DWORD ERROR_LNTDRV_NOT_COVERED = 0x20000005;
const DWORD ERROR_LNTDRV_NOT_IN_USE = 0x20000006;
const DWORD ERROR_LNTDRV_NO_SIGNATURE = 0x20000007;
const DWORD ERROR_LNTDRV_OTHER_ARCH = 0x20000008;
const DWORD ERROR_LNTDRV_OTHER_WINDOWS = 0x20000009;

std::wstring LoadText(UINT id);
std::wstring FormatText(UINT id, const std::wstring &first);
std::wstring FormatText(UINT id, const std::wstring &first, const std::wstring &second);
std::wstring FormatText(UINT id, const std::wstring &first, const std::wstring &second, const std::wstring &third);
std::wstring FormatText(UINT id, const std::wstring &first, const std::wstring &second, const std::wstring &third,
                        const std::wstring &fourth);
std::wstring NumberText(ULONGLONG value);
std::wstring ErrorText(DWORD error);
bool IsOfflineError(DWORD error);

}
