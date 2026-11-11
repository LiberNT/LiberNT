/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Steps a stack walk back one frame through the ebp chain
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

BOOLEAN
NTAPI
KiUnwindFrame(
    _Inout_ PCONTEXT Context,
    _In_ BOOLEAN CallerFrame)
{
    UNREFERENCED_PARAMETER(CallerFrame);

    return KiUnwindFrameRecord(Context);
}

/* EOF */
