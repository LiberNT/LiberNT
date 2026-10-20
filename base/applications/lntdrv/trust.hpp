/*
 * PROJECT:     LiberNT Drivers
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Driver package signature and file hash verification
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <string>
#include <vector>

namespace lntdrv
{

DWORD HashFile(const std::wstring &path, LPCWSTR algorithm, std::vector<BYTE> &digest);
DWORD VerifyPackage(const std::wstring &directory, std::wstring &file);

}
