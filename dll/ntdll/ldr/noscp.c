/*
 * PROJECT:     LiberNT NT Library
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Loader hooks for architectures without AMD64 supplemental unwind tables
 * COPYRIGHT:   Copyright 2026 LiberNT contributors
 */

#include <ntdll.h>

NTSTATUS
LdrpArchRegisterScp(PLDR_DATA_TABLE_ENTRY Entry)
{
    UNREFERENCED_PARAMETER(Entry);
    return STATUS_SUCCESS;
}

VOID
LdrpArchUnregisterScp(PLDR_DATA_TABLE_ENTRY Entry)
{
    UNREFERENCED_PARAMETER(Entry);
}
