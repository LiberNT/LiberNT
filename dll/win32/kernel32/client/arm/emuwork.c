/*
 * PROJECT:     LiberNT System Libraries
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Emulator work notifications on a host without WoW64
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <k32.h>

PVOID
WINAPI
BasepOpenEmulatorWork(
    _In_ HANDLE Process)
{
    UNREFERENCED_PARAMETER(Process);
    return NULL;
}

VOID
WINAPI
BasepCloseEmulatorWork(
    _In_opt_ PVOID WorkList)
{
    UNREFERENCED_PARAMETER(WorkList);
}

VOID
WINAPI
BasepPostEmulatorWork(
    _In_opt_ PVOID WorkList,
    _In_ ULONG Id,
    _In_ PVOID Address,
    _In_ SIZE_T Size,
    _In_ ULONG ArgumentCount,
    _In_ ULONG Argument0,
    _In_ ULONG Argument1)
{
    UNREFERENCED_PARAMETER(WorkList);
    UNREFERENCED_PARAMETER(Id);
    UNREFERENCED_PARAMETER(Address);
    UNREFERENCED_PARAMETER(Size);
    UNREFERENCED_PARAMETER(ArgumentCount);
    UNREFERENCED_PARAMETER(Argument0);
    UNREFERENCED_PARAMETER(Argument1);
}
