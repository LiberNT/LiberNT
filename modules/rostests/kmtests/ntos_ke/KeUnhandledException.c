/*
 * PROJECT:     LiberNT kernel-mode tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Stops the system with a fault nothing handles, so that the report of that stop can be checked
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <kmt_test.h>

static
DECLSPEC_NOINLINE
VOID
WriteUnmappedPage(VOID)
{
    *(volatile ULONG *)(ULONG_PTR)0x10 = 0;
}

START_TEST(KeUnhandledException)
{
    WriteUnmappedPage();
}
