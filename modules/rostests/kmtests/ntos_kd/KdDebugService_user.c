/*
 * PROJECT:     LiberNT kernel-mode tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     A user-mode DbgPrompt the kernel debugger cannot serve reaches the caller as a breakpoint
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <kmt_test.h>
#include <ndk/rtlfuncs.h>

START_TEST(KdDebugService)
{
    NTSTATUS Code = STATUS_SUCCESS;

    /* The response buffer is in the never-mapped first page */
    _SEH2_TRY
    {
        DbgPrompt("KdDebugService: ", (PCH)(ULONG_PTR)0x1000, 16);
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Code = _SEH2_GetExceptionCode();
    }
    _SEH2_END;

    ok_eq_hex(Code, STATUS_BREAKPOINT);
}

/* EOF */
