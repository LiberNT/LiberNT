/*
 * PROJECT:     LiberNT Universal CRT
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Arm strcmp entry point for the UCRT strcmp
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#define strcmp __ucrt_strcmp_simd
#include "../../../crt/string/arm64/strcmp.S"
