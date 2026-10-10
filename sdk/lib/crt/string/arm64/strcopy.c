/*
 * PROJECT:     LiberNT CRT library
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     String copy and append routines built on the ARM64 length and memory routines
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <string.h>
#include <wchar.h>

char *strcat(char *dst, const char *src)
{
    strcpy(dst + strlen(dst), src);
    return dst;
}

char *strncat(char *dst, const char *src, size_t n)
{
    char *end;
    size_t len;

    if (n == 0)
        return dst;
    end = dst + strlen(dst);
    len = strnlen(src, n);
    memcpy(end, src, len);
    end[len] = 0;
    return dst;
}

char *strncpy(char *dst, const char *src, size_t n)
{
    size_t len = strnlen(src, n);

    memcpy(dst, src, len);
    memset(dst + len, 0, n - len);
    return dst;
}

wchar_t *wcscpy(wchar_t *dst, const wchar_t *src)
{
    memcpy(dst, src, (wcslen(src) + 1) * sizeof(wchar_t));
    return dst;
}

wchar_t *wcscat(wchar_t *dst, const wchar_t *src)
{
    wcscpy(dst + wcslen(dst), src);
    return dst;
}

wchar_t *wcsncpy(wchar_t *dst, const wchar_t *src, size_t n)
{
    size_t len = wcsnlen(src, n);

    memcpy(dst, src, len * sizeof(wchar_t));
    memset(dst + len, 0, (n - len) * sizeof(wchar_t));
    return dst;
}

wchar_t *wcsncat(wchar_t *dst, const wchar_t *src, size_t n)
{
    wchar_t *end;
    size_t len;

    if (n == 0)
        return dst;
    end = dst + wcslen(dst);
    len = wcsnlen(src, n);
    memcpy(end, src, len * sizeof(wchar_t));
    end[len] = 0;
    return dst;
}
