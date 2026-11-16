/*
 * PROJECT:     LiberNT kernel-mode tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     A failed assertion nobody can answer stops once instead of asking forever
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <kmt_test.h>

static
VOID
FailAssertion(
    _In_ NTSTATUS Expected)
{
    KmtStartSeh()
        RtlAssert((PVOID)"RtlAssertUnanswered", (PVOID)__FILE__, __LINE__, NULL);
    KmtEndSeh(Expected);
}

START_TEST(RtlAssertUnanswered)
{
    SYSTEM_FLAGS_INFORMATION Original;
    SYSTEM_FLAGS_INFORMATION Changed;
    BOOLEAN Disabled = FALSE;
    NTSTATUS Status;

    if (skip(KmtIsCheckedBuild, "Only a checked build is documented to stop at a failed assertion\n"))
        return;

    Status = ZwQuerySystemInformation(SystemFlagsInformation, &Original, sizeof(Original), NULL);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (skip(NT_SUCCESS(Status), "The global flags cannot be read\n"))
        return;

    if (KD_DEBUGGER_ENABLED)
    {
        Status = KdDisableDebugger();
        if (skip(NT_SUCCESS(Status), "The kernel debugger cannot be disabled (0x%08lx)\n", Status))
            return;
        Disabled = TRUE;
    }

    Changed.Flags = Original.Flags & ~FLG_DISABLE_DEBUG_PROMPTS;
    Status = ZwSetSystemInformation(SystemFlagsInformation, &Changed, sizeof(Changed));
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (NT_SUCCESS(Status))
        FailAssertion(STATUS_BREAKPOINT);

    Changed.Flags = Original.Flags | FLG_DISABLE_DEBUG_PROMPTS;
    Status = ZwSetSystemInformation(SystemFlagsInformation, &Changed, sizeof(Changed));
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (NT_SUCCESS(Status))
        FailAssertion(STATUS_ASSERTION_FAILURE);

    ok_eq_hex(ZwSetSystemInformation(SystemFlagsInformation, &Original, sizeof(Original)), STATUS_SUCCESS);

    if (Disabled)
        ok_eq_hex(KdEnableDebugger(), STATUS_SUCCESS);
}

/* EOF */
