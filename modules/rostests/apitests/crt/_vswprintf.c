/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests for _vswprintf
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <apitest.h>

#define WIN32_NO_STATUS
#include <stdio.h>
#include <stdarg.h>

typedef int (__cdecl *PFN__vswprintf)(wchar_t *Buffer, const wchar_t *Format, va_list Args);

static PFN__vswprintf p_vswprintf;

static
int
__cdecl
call_vswprintf(
    _Out_ wchar_t *Buffer,
    _In_ const wchar_t *Format,
    ...)
{
    va_list Args;
    int Result;

    va_start(Args, Format);
    Result = p_vswprintf(Buffer, Format, Args);
    va_end(Args);
    return Result;
}

START_TEST(_vswprintf)
{
    wchar_t Buffer[32];
    int Result;

    p_vswprintf = (PFN__vswprintf)GetProcAddress(LoadLibraryA(TEST_DLL_NAME), "_vswprintf");
    if (p_vswprintf == NULL)
    {
        skip("_vswprintf is missing from %s\n", TEST_DLL_NAME);
        return;
    }

    RtlFillMemory(Buffer, sizeof(Buffer), 0xff);
    Result = call_vswprintf(Buffer, L"%s %d", L"number", 123);
    ok(Result == 10, "_vswprintf returned %d\n", Result);
    ok(wcsncmp(Buffer, L"number 123", 11) == 0, "Buffer is '%.10ls'\n", Buffer);
    ok(Buffer[11] == 0xffff, "_vswprintf wrote past the terminator: 0x%x\n", Buffer[11]);

    RtlFillMemory(Buffer, sizeof(Buffer), 0xff);
    Result = call_vswprintf(Buffer, L"%hs-%u%%", "narrow", 42u);
    ok(Result == 10, "_vswprintf returned %d\n", Result);
    ok(wcsncmp(Buffer, L"narrow-42%", 11) == 0, "Buffer is '%.10ls'\n", Buffer);

    RtlFillMemory(Buffer, sizeof(Buffer), 0xff);
    Result = call_vswprintf(Buffer, L"");
    ok(Result == 0, "_vswprintf returned %d\n", Result);
    ok(Buffer[0] == 0, "Buffer[0] is 0x%x\n", Buffer[0]);
    ok(Buffer[1] == 0xffff, "_vswprintf wrote past the terminator: 0x%x\n", Buffer[1]);
}
