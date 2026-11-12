/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     What the kernel asks of the debug providers when the KD protocol replaces them
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#include "kdpriv.h"
#include "../kd/kd.h"
#define NDEBUG
#include <debug.h>

NTSTATUS
NTAPI
KdpInitializeProviders(
    _In_ ULONG BootPhase)
{
    UNREFERENCED_PARAMETER(BootPhase);

    return STATUS_SUCCESS;
}

BOOLEAN
NTAPI
KdScreenIsBootLog(VOID)
{
    return FALSE;
}

VOID
NTAPI
KdSystemRootAvailable(VOID)
{
}

/* EOF */
