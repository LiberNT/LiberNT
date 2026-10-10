/*
 * PROJECT:     LiberNT Universal CRT
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     strcmp and wcscmp for ARM64 with the UCRT result values
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <string.h>
#include <wchar.h>

int __ucrt_strcmp_simd(const char *first, const char *last);
int __ucrt_wcscmp_simd(const wchar_t *first, const wchar_t *last);

int __cdecl strcmp(const char *first, const char *last)
{
    int result = __ucrt_strcmp_simd(first, last);

    return (result > 0) - (result < 0);
}

int __cdecl wcscmp(const wchar_t *first, const wchar_t *last)
{
    int result = __ucrt_wcscmp_simd(first, last);

    return (result > 0) - (result < 0);
}
