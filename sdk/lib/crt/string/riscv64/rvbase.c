/*
 * PROJECT:     LiberNT CRT
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     RISC-V64 RVA20 string and memory routines for kernel and native code
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <string.h>
#include "rvstring.h"

size_t __cdecl
strlen(const char *String)
{
    return RvStrlenRva20(String);
}

int __cdecl
strcmp(const char *First, const char *Second)
{
    return RvStrcmpRva20(First, Second);
}

char * __cdecl
strcpy(char *Destination, const char *Source)
{
    return RvStrcpyRva20(Destination, Source);
}

void * __cdecl
memcpy(void *Destination, const void *Source, size_t Length)
{
    return RvMemcpyRva20(Destination, Source, Length);
}

void * __cdecl
memmove(void *Destination, const void *Source, size_t Length)
{
    return RvMemmoveRva20(Destination, Source, Length);
}

void * __cdecl
memset(void *Destination, int Value, size_t Length)
{
    return RvMemsetRva20(Destination, Value, Length);
}

size_t __cdecl
strnlen(const char *String, size_t Count)
{
    return RvStrnlenRva20(String, Count);
}

char * __cdecl
strchr(const char *String, int Character)
{
    return RvStrchrRva20(String, Character);
}

char * __cdecl
strrchr(const char *String, int Character)
{
    return RvStrrchrRva20(String, Character);
}

size_t __cdecl
wcslen(const wchar_t *String)
{
    return RvWcslenRva20(String);
}

size_t __cdecl
wcsnlen(const wchar_t *String, size_t Count)
{
    return RvWcsnlenRva20(String, Count);
}

wchar_t * __cdecl
wcschr(const wchar_t *String, wchar_t Character)
{
    return RvWcschrRva20(String, Character);
}

wchar_t * __cdecl
wcsrchr(const wchar_t *String, wchar_t Character)
{
    return RvWcsrchrRva20(String, Character);
}

int __cdecl
wcscmp(const wchar_t *First, const wchar_t *Second)
{
    return RvWcscmpRva20(First, Second);
}

int __cdecl
wcsncmp(const wchar_t *First, const wchar_t *Second, size_t Count)
{
    return RvWcsncmpRva20(First, Second, Count);
}
