/*
 * PROJECT:     LiberNT kernel-mode tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Stops the system with a call through a NULL pointer, so that the report of that stop can be checked
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <kmt_test.h>

typedef VOID (NTAPI *PKMT_NULL_ROUTINE)(VOID);

static PKMT_NULL_ROUTINE volatile NullRoutine;

static
DECLSPEC_NOINLINE
VOID
CallThroughNull(VOID)
{
    NullRoutine();
}

START_TEST(KeBadInstructionPointer)
{
    CallThroughNull();
}
