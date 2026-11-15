/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Zero whole pages with a string store
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>

VOID
FASTCALL
KeZeroPages(
    _Out_writes_bytes_(Size) PVOID Address,
    _In_ ULONG Size)
{
    ASSERT((Size % sizeof(ULONG)) == 0);

    __stosd((PULONG)Address, 0, Size / sizeof(ULONG));
}
