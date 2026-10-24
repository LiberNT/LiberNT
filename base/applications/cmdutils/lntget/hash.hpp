/*
 * PROJECT:     ReactOS package manager
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     SHA-256 verification for downloaded installers
 * COPYRIGHT:   Copyright 2026 Ahmed Arif
 */

#pragma once

#include "model.hpp"

namespace lntget
{

Status HashBytesSha256(const uint8_t *bytes, std::size_t size, uint8_t (&digest)[32]);
Status HashFileSha256(const std::wstring &path, uint8_t (&digest)[32]);
Status VerifyFileSha256(const std::wstring &path, const std::string &expectedHex);
Status RunHashSelfTests();

} // namespace lntget
