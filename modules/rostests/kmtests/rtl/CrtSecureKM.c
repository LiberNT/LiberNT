/*
 * PROJECT:     LiberNT kernel-mode tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test the kernel exports _wcsnset_s, _wcslwr_s and _ultow_s, results and buffer contents
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <kmt_test.h>

#define NDEBUG
#include <debug.h>

#define FILL 0x2323
#define TEST_EINVAL 22
#define TEST_ERANGE 34

typedef int (__cdecl *PFN_WCSNSET_S)(wchar_t *, size_t, wchar_t, size_t);
typedef int (__cdecl *PFN_WCSLWR_S)(wchar_t *, size_t);
typedef int (__cdecl *PFN_ULTOW_S)(unsigned long, wchar_t *, size_t, int);

static PFN_WCSNSET_S pWcsnsetS;
static PFN_WCSLWR_S pWcslwrS;
static PFN_ULTOW_S pUltowS;
static WCHAR Buffer[16];

static
VOID
Fill(
    _In_opt_ PCWSTR Text)
{
    ULONG Index;

    for (Index = 0; Index < RTL_NUMBER_OF(Buffer); Index++)
        Buffer[Index] = FILL;
    for (Index = 0; Text && Text[Index]; Index++)
        Buffer[Index] = Text[Index];
    if (Text)
        Buffer[Index] = 0;
}

#define check(r,e,...) check_(__LINE__,r,e,__VA_ARGS__, 0xFFFF)
static
VOID
check_(
    _In_ ULONG Line,
    _In_ int Result,
    _In_ int Expected,
    ...)
{
    va_list Args;
    ULONG Index, Value;

    KmtOk(Result == Expected, __FILE__, "line %lu: result %d, expected %d\n", Line, Result, Expected);
    va_start(Args, Expected);
    for (Index = 0; Index < RTL_NUMBER_OF(Buffer); Index++)
    {
        Value = va_arg(Args, ULONG);
        if (Value == 0xFFFF)
            break;
        KmtOk(Buffer[Index] == Value, __FILE__, "line %lu: Buffer[%lu] = 0x%04x, expected 0x%04lx\n", Line, Index,
              Buffer[Index], Value);
    }
    va_end(Args);
}

static
VOID
TestWcsnset(VOID)
{
    Fill(L"abcdef"); check(pWcsnsetS(Buffer, 10, L'x', 3), 0, 'x', 'x', 'x', 'd', 'e', 'f', 0, FILL);
    Fill(L"abcdef"); check(pWcsnsetS(Buffer, 10, L'x', 6), 0, 'x', 'x', 'x', 'x', 'x', 'x', 0, FILL);
    Fill(L"abcdef"); check(pWcsnsetS(Buffer, 10, L'x', 9), 0, 'x', 'x', 'x', 'x', 'x', 'x', 0, FILL);
    Fill(L"abcdef"); check(pWcsnsetS(Buffer, 10, L'x', 0), 0, 'a', 'b', 'c', 'd', 'e', 'f', 0, FILL);
    Fill(L"abcdef"); check(pWcsnsetS(Buffer, 7, L'x', 20), 0, 'x', 'x', 'x', 'x', 'x', 'x', 0, FILL);
    Fill(L"abc"); check(pWcsnsetS(Buffer, 4, 0, 2), 0, 0, 0, 'c', 0, FILL);
    Fill(L"abcdef"); check(pWcsnsetS(Buffer, 6, L'x', 2), TEST_EINVAL, 0, 'x', 'c', 'd', 'e', 'f', 0, FILL);
    Fill(L"abcdef"); check(pWcsnsetS(Buffer, 4, L'x', 2), TEST_EINVAL, 0, 'x', 'c', 'd', 'e', 'f', 0, FILL);
    Fill(L"abcdef"); check(pWcsnsetS(Buffer, 0, L'x', 2), TEST_EINVAL, 'a', 'b', 'c', 'd', 'e', 'f', 0, FILL);
    Fill(L"abcdef"); check(pWcsnsetS(NULL, 10, L'x', 2), TEST_EINVAL, 'a');
    Fill(L"abcdef"); check(pWcsnsetS(NULL, 0, L'x', 0), 0, 'a');
    Fill(L"abcdef"); check(pWcsnsetS(NULL, 0, L'x', 2), TEST_EINVAL, 'a');
    Fill(L"abcdef"); check(pWcsnsetS(Buffer, 0, L'x', 0), TEST_EINVAL, 'a', 'b', 'c', 'd', 'e', 'f', 0, FILL);
}

static
VOID
TestWcslwr(VOID)
{
    Fill(L"AbC1Z"); check(pWcslwrS(Buffer, 10), 0, 'a', 'b', 'c', '1', 'z', 0, FILL);
    Fill(L"\x00c0\x00c4\x0414\x0130Q"); check(pWcslwrS(Buffer, 10), 0, 0x00c0, 0x00c4, 0x0414, 0x0130, 'q', 0, FILL);
    Fill(L"ABC"); check(pWcslwrS(Buffer, 4), 0, 'a', 'b', 'c', 0, FILL);
    Fill(L"ABCDEF"); check(pWcslwrS(Buffer, 3), TEST_EINVAL, 0, 'B', 'C', 'D', 'E', 'F', 0, FILL);
    Fill(L"ABCDEF"); check(pWcslwrS(Buffer, 6), TEST_EINVAL, 0, 'B', 'C', 'D', 'E', 'F', 0, FILL);
    Fill(L"ABCDEF"); check(pWcslwrS(Buffer, 0), TEST_EINVAL, 0, 'B', 'C', 'D', 'E', 'F', 0, FILL);
    Fill(L"ABCDEF"); check(pWcslwrS(NULL, 4), TEST_EINVAL, 'A');
    Fill(L"ABCDEF"); check(pWcslwrS(NULL, 0), TEST_EINVAL, 'A');
}

static
VOID
TestUltow(VOID)
{
    Fill(NULL); check(pUltowS(255, Buffer, 10, 16), 0, 'f', 'f', 0, FILL);
    Fill(NULL); check(pUltowS(4294967295UL, Buffer, 11, 10), 0, '4', '2', '9', '4', '9', '6', '7', '2', '9', '5', 0, FILL);
    Fill(NULL); check(pUltowS(4294967295UL, Buffer, 10, 10), TEST_ERANGE, 0, '9', '2', '7', '6', '9', '4', '9', '2', '4', FILL);
    Fill(NULL); check(pUltowS(0, Buffer, 2, 10), 0, '0', 0, FILL);
    Fill(NULL); check(pUltowS(0, Buffer, 1, 10), TEST_ERANGE, 0, FILL);
    Fill(NULL); check(pUltowS(35, Buffer, 4, 36), 0, 'z', 0, FILL);
    Fill(NULL); check(pUltowS(5, Buffer, 12, 2), 0, '1', '0', '1', 0, FILL);
    Fill(NULL); check(pUltowS(5, Buffer, 12, 1), TEST_EINVAL, 0, FILL);
    Fill(NULL); check(pUltowS(5, Buffer, 12, 37), TEST_EINVAL, 0, FILL);
    Fill(NULL); check(pUltowS(5, Buffer, 0, 10), TEST_EINVAL, FILL);
    Fill(NULL); check(pUltowS(5, NULL, 12, 10), TEST_EINVAL, FILL);
    Fill(NULL); check(pUltowS(123, Buffer, 3, 10), TEST_ERANGE, 0, '2', '1', FILL);
}

START_TEST(CrtSecureKM)
{
    UNICODE_STRING Wcsnset = RTL_CONSTANT_STRING(L"_wcsnset_s");
    UNICODE_STRING Wcslwr = RTL_CONSTANT_STRING(L"_wcslwr_s");
    UNICODE_STRING Ultow = RTL_CONSTANT_STRING(L"_ultow_s");

    pWcsnsetS = (PFN_WCSNSET_S)MmGetSystemRoutineAddress(&Wcsnset);
    pWcslwrS = (PFN_WCSLWR_S)MmGetSystemRoutineAddress(&Wcslwr);
    pUltowS = (PFN_ULTOW_S)MmGetSystemRoutineAddress(&Ultow);
    ok(pWcsnsetS != NULL, "_wcsnset_s is not exported\n");
    ok(pWcslwrS != NULL, "_wcslwr_s is not exported\n");
    ok(pUltowS != NULL, "_ultow_s is not exported\n");

    if (pWcsnsetS)
        TestWcsnset();
    if (pWcslwrS)
        TestWcslwr();
    if (pUltowS)
        TestUltow();
}
