/*
 * PROJECT:     LiberNT System Libraries
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     x86 frame-pointer step of the unhandled exception trace
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <k32.h>

BOOL
BasepArchUnwindFrame(
    _Inout_ PCONTEXT Context,
    _Out_ PULONG_PTR ReturnAddress)
{
    PULONG Frame = (PULONG)Context->Ebp;

    if (Frame == NULL || Frame[1] == 0 || Frame[1] == 0xdeadbeef)
        return FALSE;

    *ReturnAddress = Frame[1];
    Context->Ebp = IsBadReadPtr((PVOID)Frame[0], sizeof(*Frame) * 2) ? 0 : Frame[0];
    return TRUE;
}
