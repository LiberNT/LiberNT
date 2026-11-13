/*
 * PROJECT:         ReactOS HAL
 * LICENSE:         GPL - See COPYING in the top level directory
 * FILE:            hal/halx86/apic/halstall.h
 * PURPOSE:         TSC tick arithmetic for stall loops
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
    ULONG64 Limit = (ULONG64)~((ULONG64)0);
    ULONG64 Ticks = (ULONG64)ScaleFactor * MicroSeconds;

    /* Saturate at the maximum tick instead of wrapping. */
    return (StartTime > Limit - Ticks) ? Limit : StartTime + Ticks;
}

#endif /* _HALSTALL_H_ */
