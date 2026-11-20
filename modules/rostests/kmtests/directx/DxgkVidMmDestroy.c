/*
 * PROJECT:     LiberNT kernel-mode tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     dxgkrnl VidMm allocation destruction flag tests
 * COPYRIGHT:   Copyright 2026 Ahmed Arif <arif.ing@outlook.com>
 */

#include <kmt_test.h>
#include "vidmm_destroy_core.h"

static BOOLEAN DxgkVidMmDestroyTestWaits(_In_ UINT Value)
{
    D3DDDICB_DESTROYALLOCATION2FLAGS Flags;

    Flags.Value = Value;
    return DxgkVidMmCoreDestroyWaitsForQueuedWork(Flags);
}

START_TEST(DxgkVidMmDestroy)
{
    D3DDDICB_DESTROYALLOCATION2FLAGS Flags;

    Flags.Value = 0;
    Flags.AssumeNotInUse = 1;
    ok_eq_ulong(Flags.Value, 0x1UL);
    Flags.Value = 0;
    Flags.SynchronousDestroy = 1;
    ok_eq_ulong(Flags.Value, 0x2UL);

    ok_eq_bool(DxgkVidMmDestroyTestWaits(0), TRUE);
    ok_eq_bool(DxgkVidMmDestroyTestWaits(0x1), FALSE);
    ok_eq_bool(DxgkVidMmDestroyTestWaits(0x2), TRUE);
    ok_eq_bool(DxgkVidMmDestroyTestWaits(0x3), FALSE);
    ok_eq_bool(DxgkVidMmDestroyTestWaits(0x80000000), TRUE);
    ok_eq_bool(DxgkVidMmDestroyTestWaits(0x80000001), FALSE);
}
