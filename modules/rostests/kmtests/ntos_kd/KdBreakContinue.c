/*
 * PROJECT:     LiberNT kernel-mode tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     DbgBreakPoint returns to its caller once the kernel debugger lets the machine go on
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <kmt_test.h>

START_TEST(KdBreakContinue)
{
    if (skip(KD_DEBUGGER_ENABLED, "The kernel debugger is not enabled\n"))
        return;

    /* Hidden test: something must answer this stop, such as KDBG with "cont" in its start-up file */
    DbgBreakPoint();
    ok(TRUE, "DbgBreakPoint returned\n");
}

/* EOF */
