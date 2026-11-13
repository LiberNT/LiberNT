/*
 * PROJECT:     LiberNT kernel-mode tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Stops the system by running off the end of a kernel stack, so that the report of that stop can be checked
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <kmt_test.h>

static volatile ULONG Limit = MAXULONG;

static
DECLSPEC_NOINLINE
ULONG
Recurse(
    _In_ ULONG Level)
{
    volatile ULONG Frame[16];

    Frame[Level % RTL_NUMBER_OF(Frame)] = Level;
    if (Level == Limit)
        return 0;

    return Recurse(Level + 1) + Frame[Level % RTL_NUMBER_OF(Frame)];
}

START_TEST(KeKernelStackOverflow)
{
    Recurse(0);
}
