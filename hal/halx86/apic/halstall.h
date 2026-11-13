/*
 * PROJECT:     LiberNT HAL
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     TSC tick arithmetic for stall loops
 * COPYRIGHT:   Copyright 2026 LiberNT contributors
 */

#ifndef _HALSTALL_H_
#define _HALSTALL_H_

/* Pure arithmetic so host tests can include this without kernel headers. */
static __inline
ULONG64
HalpComputeStallEndTime(
    _In_ ULONG64 StartTime,
    _In_ ULONG ScaleFactor,
    _In_ ULONG MicroSeconds)
{
    ULONG64 Limit = ~(ULONG64)0;
    ULONG64 Ticks = (ULONG64)ScaleFactor * MicroSeconds;

    /* Saturate instead of wrapping when the start tick is already near the end. */
    return (StartTime > Limit - Ticks) ? Limit : StartTime + Ticks;
}

#endif /* _HALSTALL_H_ */
