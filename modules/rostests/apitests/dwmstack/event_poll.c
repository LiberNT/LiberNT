/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests how the compositor reads an event query's GetData answer
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */
#include <apitest.h>
#include <windows.h>
#include "../../../../base/system/dwm/gpuquery.h"

START_TEST(event_poll)
{
    ok_int(DwmGpuEventPoll(S_FALSE, FALSE), DWM_GPU_EVENT_PENDING);
    ok_int(DwmGpuEventPoll(S_FALSE, TRUE), DWM_GPU_EVENT_PENDING);
    ok_int(DwmGpuEventPoll(S_OK, TRUE), DWM_GPU_EVENT_COMPLETE);
    ok_int(DwmGpuEventPoll(S_OK, FALSE), DWM_GPU_EVENT_REISSUE);
}
