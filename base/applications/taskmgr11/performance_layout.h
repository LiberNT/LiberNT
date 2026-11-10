/*
 * PROJECT:     LiberNT Task Manager
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     CPU statistics layout
 * COPYRIGHT:   Copyright 2026 OpenAI
 */

#pragma once

static inline int TmCpuStatsLayout(int columns[3], int speedWidth,
                                   int uptimeWidth, int minimumColumn,
                                   int spacing)
{
    int i, width;
    for (i = 0; i < 3; i++)
    {
        columns[i] += spacing;
        if (columns[i] < minimumColumn) columns[i] = minimumColumn;
    }
    width = columns[0] + columns[1] + columns[2];
    if (width < columns[0] + speedWidth + spacing)
        width = columns[0] + speedWidth + spacing;
    if (width < uptimeWidth + spacing)
        width = uptimeWidth + spacing;
    return width;
}
