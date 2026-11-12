/*
 * PROJECT:     LiberNT kernel-mode tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     DbgPrint reports success for a print it delivered with no debugger enabled
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <kmt_test.h>

START_TEST(KdPrintStatus)
{
    BOOLEAN Disabled = FALSE;
    NTSTATUS Status;

    if (KD_DEBUGGER_ENABLED)
    {
        Status = KdDisableDebugger();
        if (skip(NT_SUCCESS(Status), "The kernel debugger cannot be disabled (0x%08lx)\n", Status))
            return;
        Disabled = TRUE;
    }

    Status = DbgPrint("KdPrintStatus: printed while no kernel debugger is enabled\n");

    if (Disabled)
        ok_eq_hex(KdEnableDebugger(), STATUS_SUCCESS);

    ok_eq_hex(Status, STATUS_SUCCESS);
}

/* EOF */
