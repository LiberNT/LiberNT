/*
 * PROJECT:     LiberNT kernel-mode tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     KdChangeOption, KdDisableDebugger and KdEnableDebugger follow the WDK contract
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <kmt_test.h>

static
NTSTATUS
SetBlockEnable(
    _In_ BOOLEAN Block)
{
    return KdChangeOption(KD_OPTION_SET_BLOCK_ENABLE, sizeof(Block), &Block, 0, NULL, NULL);
}

START_TEST(KdChangeOption)
{
    NTSTATUS Status;
    BOOLEAN Block = FALSE;
    UCHAR Out = 0;
    BOOLEAN DisabledWhileBlocked;

    Status = SetBlockEnable(FALSE);
    if (skip(Status != STATUS_DEBUGGER_INACTIVE, "The system was booted without debug controls\n"))
        return;
    ok_eq_hex(Status, STATUS_SUCCESS);

    Status = KdChangeOption((KD_OPTION)(KD_OPTION_SET_BLOCK_ENABLE + 1), sizeof(Block), &Block, 0, NULL, NULL);
    ok_eq_hex(Status, STATUS_INVALID_INFO_CLASS);
    Status = KdChangeOption((KD_OPTION)0x7FFFFFFF, sizeof(Block), &Block, 0, NULL, NULL);
    ok_eq_hex(Status, STATUS_INVALID_INFO_CLASS);

    Status = KdChangeOption(KD_OPTION_SET_BLOCK_ENABLE, 0, &Block, 0, NULL, NULL);
    ok_eq_hex(Status, STATUS_INVALID_PARAMETER);
    Status = KdChangeOption(KD_OPTION_SET_BLOCK_ENABLE, sizeof(ULONG), &Block, 0, NULL, NULL);
    ok_eq_hex(Status, STATUS_INVALID_PARAMETER);
    Status = KdChangeOption(KD_OPTION_SET_BLOCK_ENABLE, sizeof(Block), &Block, sizeof(Out), &Out, NULL);
    ok_eq_hex(Status, STATUS_INVALID_PARAMETER);
    Status = KdChangeOption(KD_OPTION_SET_BLOCK_ENABLE, sizeof(Block), &Block, sizeof(Out), NULL, NULL);
    ok_eq_hex(Status, STATUS_INVALID_PARAMETER);
    Status = KdChangeOption(KD_OPTION_SET_BLOCK_ENABLE, sizeof(Block), &Block, 0, &Out, NULL);
    ok_eq_hex(Status, STATUS_INVALID_PARAMETER);

    /* Without the high bit the block is whatever the caller last set */
    ok_eq_hex(SetBlockEnable(TRUE), STATUS_SUCCESS);
    Status = SetBlockEnable(FALSE);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (skip(NT_SUCCESS(Status), "The block cannot be lifted; a disable while blocked could not be undone\n"))
        return;

    ok_eq_hex(SetBlockEnable(TRUE), STATUS_SUCCESS);
    Status = KdDisableDebugger();
    ok_eq_hex(Status, STATUS_ACCESS_DENIED);
    DisabledWhileBlocked = NT_SUCCESS(Status);
    ok_eq_hex(KdEnableDebugger(), STATUS_ACCESS_DENIED);
    ok_eq_hex(SetBlockEnable(FALSE), STATUS_SUCCESS);
    if (DisabledWhileBlocked)
        KdEnableDebugger();

    if (!KD_DEBUGGER_ENABLED)
    {
        /* A debugger configured to wait, as with CRASHDEBUG, is turned on by one enable */
        ok_eq_hex(KdEnableDebugger(), STATUS_SUCCESS);
        ok_eq_bool(KD_DEBUGGER_ENABLED, TRUE);
        ok_eq_uint(SharedUserData->KdDebuggerEnabled & 1, 1);
        if (KD_DEBUGGER_ENABLED)
        {
            ok_eq_hex(KdDisableDebugger(), STATUS_SUCCESS);
            ok_eq_bool(KD_DEBUGGER_ENABLED, FALSE);
            ok_eq_uint(SharedUserData->KdDebuggerEnabled & 1, 0);
        }
        return;
    }

    /* The page user mode reads follows the switch */
    Status = KdDisableDebugger();
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok_eq_bool(KD_DEBUGGER_ENABLED, FALSE);
    ok_eq_uint(SharedUserData->KdDebuggerEnabled & 1, 0);
    if (NT_SUCCESS(Status))
    {
        ok_eq_hex(KdEnableDebugger(), STATUS_SUCCESS);
        ok_eq_bool(KD_DEBUGGER_ENABLED, TRUE);
        ok_eq_uint(SharedUserData->KdDebuggerEnabled & 1, 1);
    }
}

/* EOF */
