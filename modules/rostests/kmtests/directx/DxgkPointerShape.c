/*
 * PROJECT:     LiberNT kernel-mode tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     The color pointer image handed to DxgkDdiSetPointerShape
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <kmt_test.h>
#include "display_core.h"

#define TEST_POINTER_STAGING_WIDTH 8

static
VOID
FillStaged(
    _Out_writes_(TEST_POINTER_STAGING_WIDTH * Height) PULONG Pixels,
    _In_ ULONG Width,
    _In_ ULONG Height)
{
    ULONG Row;
    ULONG Column;

    for (Row = 0; Row < Height; Row++)
    {
        for (Column = 0; Column < TEST_POINTER_STAGING_WIDTH; Column++)
            Pixels[Row * TEST_POINTER_STAGING_WIDTH + Column] = Column < Width ? 0xFF000000 | (Row << 8) | Column : 0xDEADBEEF;
    }
}

static
BOOLEAN
RowsPacked(
    _In_reads_(Width * Height) const ULONG *Pixels,
    _In_ ULONG Width,
    _In_ ULONG Height)
{
    ULONG Row;
    ULONG Column;

    for (Row = 0; Row < Height; Row++)
    {
        for (Column = 0; Column < Width; Column++)
        {
            if (Pixels[Row * Width + Column] != (0xFF000000 | (Row << 8) | Column))
                return FALSE;
        }
    }
    return TRUE;
}

START_TEST(DxgkPointerShape)
{
    ULONG Pixels[TEST_POINTER_STAGING_WIDTH * 4];
    ULONG Pitch;

    FillStaged(Pixels, 3, 4);
    Pitch = DxgkDisplayCorePackPointerRows((PUCHAR)Pixels, 3, 4, TEST_POINTER_STAGING_WIDTH * sizeof(ULONG));
    ok_eq_ulong(Pitch, 3UL * sizeof(ULONG));
    ok_bool_true(RowsPacked(Pixels, 3, 4), "a pointer staged at a wider pitch reaches the miniport with its rows packed");

    FillStaged(Pixels, TEST_POINTER_STAGING_WIDTH, 4);
    Pitch = DxgkDisplayCorePackPointerRows((PUCHAR)Pixels, TEST_POINTER_STAGING_WIDTH, 4, TEST_POINTER_STAGING_WIDTH * sizeof(ULONG));
    ok_eq_ulong(Pitch, (ULONG)(TEST_POINTER_STAGING_WIDTH * sizeof(ULONG)));
    ok_bool_true(RowsPacked(Pixels, TEST_POINTER_STAGING_WIDTH, 4), "a pointer as wide as its staging rows is left as it is");

    FillStaged(Pixels, 2, 1);
    Pitch = DxgkDisplayCorePackPointerRows((PUCHAR)Pixels, 2, 1, TEST_POINTER_STAGING_WIDTH * sizeof(ULONG));
    ok_eq_ulong(Pitch, 2UL * sizeof(ULONG));
    ok_bool_true(RowsPacked(Pixels, 2, 1), "a one-row pointer keeps its row");
}
