/*
 * PROJECT:     LiberNT Task Manager
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Mouse wheel scrolling distance
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

static inline int TmWheelScroll(int *remainder, int delta, int notchDistance)
{
    int total = *remainder - delta * notchDistance;
    int distance = total / WHEEL_DELTA;
    *remainder = total - distance * WHEEL_DELTA;
    return distance;
}
