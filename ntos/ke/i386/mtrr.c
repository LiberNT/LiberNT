/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     i386 memory type range register setup
 * COPYRIGHT:   Copyright Alex Ionescu <alex.ionescu@reactos.org>
 *              Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

CODE_SEG("INIT")
VOID
NTAPI
KiInitializeMTRR(IN BOOLEAN FinalCpu)
{
    /* FIXME: Support this */
    DPRINT("MTRR support detected but not yet taken advantage of\n");
}

CODE_SEG("INIT")
VOID
NTAPI
KiAmdK6InitializeMTRR(VOID)
{
    /* FIXME: Support this */
    DPRINT("AMD MTRR support detected but not yet taken advantage of\n");
}
