/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests how the compositor paces its attempts to bring Direct3D back
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */
#include <apitest.h>
#include <windows.h>
#include "../../../../base/system/dwm/gpuretry.h"

START_TEST(gpu_retry)
{
    ok_int(DwmGpuRetryDelay(0), 200);
    ok_int(DwmGpuRetryDelay(1), 400);
    ok_int(DwmGpuRetryDelay(5), 6400);
    ok_int(DwmGpuRetryDelay(6), 12800);
    ok_int(DwmGpuRetryDelay(7), 12800);
    ok_int(DwmGpuRetryDelay(~0UL), 12800);
    ok_int(DwmGpuRetryDue(1199, 1000, 0), FALSE);
    ok_int(DwmGpuRetryDue(1200, 1000, 0), TRUE);
    ok_int(DwmGpuRetryDue(1399, 1000, 1), FALSE);
    ok_int(DwmGpuRetryDue(1400, 1000, 1), TRUE);
    ok_int(DwmGpuRetryDue(100, 0xFFFFFFFFu - 50, 0), FALSE);
    ok_int(DwmGpuRetryDue(150, 0xFFFFFFFFu - 50, 0), TRUE);
}
