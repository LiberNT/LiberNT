/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Task Manager CPU statistics layout tests
 * COPYRIGHT:   Copyright 2026 OpenAI
 */

#include <apitest.h>
#include "performance_layout.h"

START_TEST(performance_layout)
{
    int columns[] = { 50, 55, 105 };
    int width = TmCpuStatsLayout(columns, 95, 110, 72, 12);
    ok(columns[0] >= 50 + 6 && columns[1] >= 55 + 6 && columns[2] >= 105 + 6,
       "metric columns must fit the text and drawing inset\n");
    ok(width >= columns[0] + columns[1] + columns[2],
       "base speed block overlaps the handles column\n");
    ok(width - columns[0] >= 95 + 6, "speed text is clipped\n");
    ok(width >= 110 + 6, "uptime text is clipped\n");

    columns[0] = columns[1] = columns[2] = 20;
    width = TmCpuStatsLayout(columns, 300, 400, 72, 12);
    ok(width - columns[0] >= 300 + 6, "long speed text is clipped\n");
    ok(width >= 400 + 6, "long uptime text is clipped\n");
}
