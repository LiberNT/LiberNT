/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Non-temporal prefetch hint, a no-op where no prefetch is wired up
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>

VOID
FASTCALL
RtlPrefetchMemoryNonTemporal(
    _In_ PVOID Source,
    _In_ SIZE_T Length)
{
    UNREFERENCED_PARAMETER(Source);
    UNREFERENCED_PARAMETER(Length);
}
