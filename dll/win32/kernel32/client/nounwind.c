/*
 * PROJECT:     LiberNT System Libraries
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Unhandled exception trace step for architectures without a frame walk
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <k32.h>

BOOL
BasepArchUnwindFrame(
    _Inout_ PCONTEXT Context,
    _Out_ PULONG_PTR ReturnAddress)
{
    UNREFERENCED_PARAMETER(Context);
    UNREFERENCED_PARAMETER(ReturnAddress);
    return FALSE;
}
