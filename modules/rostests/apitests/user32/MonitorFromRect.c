/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests MonitorFromRect with rectangles at the coordinate limits
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"

static BOOL CALLBACK
CountMonitor(
    _In_ HMONITOR Monitor,
    _In_ HDC Dc,
    _In_ LPRECT Rect,
    _In_ LPARAM Param)
{
    (*(UINT *)Param)++;
    return TRUE;
}

START_TEST(MonitorFromRect)
{
    static const RECT Rects[] =
    {
        { MINLONG, MINLONG, MINLONG + 10, MINLONG + 10 },
        { MAXLONG - 10, MAXLONG - 10, MAXLONG, MAXLONG },
        { MINLONG, MAXLONG - 10, MINLONG + 10, MAXLONG },
        { MAXLONG - 10, MINLONG, MAXLONG, MINLONG + 10 },
    };
    static const RECT Everything = { MINLONG, MINLONG, MAXLONG, MAXLONG };
    POINT Origin = { 0, 0 };
    HMONITOR Primary, Monitor;
    UINT Count = 0, i;

    ok(EnumDisplayMonitors(NULL, NULL, CountMonitor, (LPARAM)&Count), "EnumDisplayMonitors failed\n");
    Primary = MonitorFromPoint(Origin, MONITOR_DEFAULTTOPRIMARY);
    ok(Primary != NULL, "no primary monitor\n");
    if (Count != 1)
    {
        skip("%u monitors: the nearest monitor is checked with one monitor only\n", Count);
        return;
    }

    for (i = 0; i < ARRAYSIZE(Rects); i++)
    {
        Monitor = MonitorFromRect(&Rects[i], MONITOR_DEFAULTTONEAREST);
        ok(Monitor == Primary, "rect %u: nearest monitor %p, primary %p\n", i, Monitor, Primary);
        Monitor = MonitorFromRect(&Rects[i], MONITOR_DEFAULTTOPRIMARY);
        ok(Monitor == Primary, "rect %u: default monitor %p, primary %p\n", i, Monitor, Primary);
        Monitor = MonitorFromRect(&Rects[i], MONITOR_DEFAULTTONULL);
        ok(Monitor == NULL, "rect %u: monitor %p without a default\n", i, Monitor);
    }
    Monitor = MonitorFromRect(&Everything, MONITOR_DEFAULTTONULL);
    ok(Monitor == Primary, "whole plane: monitor %p, primary %p\n", Monitor, Primary);
}
