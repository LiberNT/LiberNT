/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests setjmp and longjmp with a jump buffer that is only 8-byte aligned
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <apitest.h>
#include <setjmp.h>
#include <pseh/pseh2.h>

static DECLSPEC_ALIGN(16) unsigned char JumpStore[sizeof(jmp_buf) + 32];

static DECLSPEC_NOINLINE int JumpThrough(_JBTYPE *Buffer)
{
    volatile int Stage = 0;

    if (setjmp(Buffer) == 0)
    {
        Stage = 1;
        longjmp(Buffer, 7);
    }
    if (Stage == 1)
        Stage = 2;
    return Stage;
}

static void TestJump(ULONG Offset)
{
    volatile int Stage = 0;
    volatile DWORD Code = 0;

    _SEH2_TRY
    {
        Stage = JumpThrough((_JBTYPE *)(JumpStore + Offset));
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Code = _SEH2_GetExceptionCode();
    }
    _SEH2_END;

    ok(Code == 0, "Offset %lu: exception 0x%08lx\n", Offset, Code);
    ok(Stage == 2, "Offset %lu: stage %d\n", Offset, Stage);
}

START_TEST(setjmp_align)
{
    TestJump(0);
    TestJump(8);
}
