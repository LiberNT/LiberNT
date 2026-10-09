/*
 * PROJECT:     LiberNT Universal CRT
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     strnlen for ARM64
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <stddef.h>
#include <string.h>

size_t __cdecl strnlen(const char* string, size_t maximum_count)
{
    size_t index = 0;

    while (index < maximum_count && string[index] != '\0')
    {
        ++index;
    }

    return index;
}
