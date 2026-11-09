/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Executive initialization hooks this architecture does not need
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

/* PUBLIC FUNCTIONS ***********************************************************/

CODE_SEG("INIT")
VOID
NTAPI
ExArchPostHalInitSystemPhase0(VOID)
{
}

CODE_SEG("INIT")
VOID
NTAPI
ExArchPostHalInitSystemPhase1(VOID)
{
}

/* EOF */
