/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test the GPU load shown while the DirectX Diagnostic Tool's rendering tests run
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <apitest.h>
#include "precomp.h"

#define SAMPLE_MS 600

START_TEST(D3DStats)
{
    CHAR Text[D3DTEST_STATS_LINES][D3DTEST_STATS_LINE_LENGTH];
    D3DTEST_RESULT Result;
    UINT Lines;

    ZeroMemory(&Result, sizeof(Result));
    D3DTestStatsBegin(&Result.Stats);
    ok(Result.Stats.GpuCount >= 1, "No adapter reported engine statistics\n");
    Sleep(SAMPLE_MS);
    D3DTestStatsSample(&Result.Stats, SAMPLE_MS, 30);
    ok(Result.Stats.GpuLoad >= 0 && Result.Stats.GpuLoad <= 100, "GPU load %ld\n", Result.Stats.GpuLoad);
    Lines = D3DTestStatsText(&Result, Text);
    ok(Lines == D3DTEST_STATS_LINES, "%u lines\n", Lines);
    ok(strncmp(Text[3], "GPU ", 4) == 0 && strcmp(Text[3], "GPU N/A") != 0, "GPU line \"%s\"\n", Text[3]);
}
