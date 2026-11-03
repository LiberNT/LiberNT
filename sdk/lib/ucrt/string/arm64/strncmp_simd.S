/*
 * PROJECT:     LiberNT Universal CRT
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Arm strncmp entry point for the UCRT strncmp
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#define strncmp __ucrt_strncmp_simd
#include "../../../crt/string/arm64/strncmp.S"
