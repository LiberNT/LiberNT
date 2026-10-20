/*
 * PROJECT:     LiberNT Drivers
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Localized text and error messages
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <initializer_list>
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

std::wstring LoadText(UINT id);
std::wstring FormatText(UINT id, std::initializer_list<std::wstring> args);
std::wstring ErrorText(DWORD error);
bool IsOfflineError(DWORD error);

}
