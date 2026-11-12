/*
 * PROJECT:     LiberNT kernel-mode tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     A print reaches the debug print callbacks with its own component and level
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <kmt_test.h>

#define MARKER "KMT_PRINT_COMPONENT_MARKER"

static volatile LONG Seen;
static ULONG SeenComponent;
static ULONG SeenLevel;

static
VOID
NTAPI
PrintCallback(
    _In_ PSTRING Output,
    _In_ ULONG ComponentId,
    _In_ ULONG Level)
{
    SIZE_T MarkerLength = sizeof(MARKER) - 1;
    USHORT Index;

    if (Output == NULL || Output->Buffer == NULL) return;

    for (Index = 0; Index + MarkerLength <= Output->Length; Index++)
    {
        if (RtlCompareMemory(Output->Buffer + Index, MARKER, MarkerLength) == MarkerLength)
        {
            SeenComponent = ComponentId;
            SeenLevel = Level;
            InterlockedExchange(&Seen, 1);
            return;
        }
    }
}

START_TEST(KdPrintComponent)
{
    NTSTATUS Status;

    Status = DbgSetDebugFilterState(DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL, TRUE);
    ok_eq_hex(Status, STATUS_SUCCESS);

    Status = DbgSetDebugPrintCallback(PrintCallback, TRUE);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status)) return;

    DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL, MARKER "\n");

    DbgSetDebugPrintCallback(PrintCallback, FALSE);

    ok_eq_long(Seen, 1L);
    ok_eq_ulong(SeenComponent, (ULONG)DPFLTR_IHVDRIVER_ID);
    ok_eq_ulong(SeenLevel, (ULONG)DPFLTR_INFO_LEVEL);
}
