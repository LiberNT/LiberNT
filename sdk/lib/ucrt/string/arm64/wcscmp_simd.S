/*
 * PROJECT:     LiberNT Universal CRT
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     wcscmp entry point for the UCRT wcscmp, and wcsncmp
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#define wcscmp __ucrt_wcscmp_simd
#include "../../../crt/string/arm64/wcscmp.S"
