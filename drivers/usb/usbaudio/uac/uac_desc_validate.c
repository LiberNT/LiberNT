/*
 * PROJECT:     LiberNT
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Minimum data endpoint size for one isochronous alt setting
 * COPYRIGHT:   Copyright 2026 Zhou Ji Yang <yinglanxing@gmail.com>
 */
#include "../usbaudio.h"
#include "uac_desc_validate.h"

#define UAC_DESC_WMAX_SIZE_MASK 0x07FFUL
#define UAC_DESC_FS_FRAMES_PER_SECOND 1000UL
#define UAC_DESC_HS_FRAMES_PER_SECOND 8000UL
#define UAC_DESC_HS_INTERVAL_MAX 4

static
ULONG
UacDescServiceIntervals(
    _In_ BOOLEAN HighSpeed,
    _In_ UCHAR Interval)
{
    if (HighSpeed)
    {
        if (Interval < 1 || Interval > UAC_DESC_HS_INTERVAL_MAX)
            return 0;
        return UAC_DESC_HS_FRAMES_PER_SECOND >> (Interval - 1);
    }

    if (Interval != 1)
        return 0;
    return UAC_DESC_FS_FRAMES_PER_SECOND;
}

ULONG
UacDescMinDataWMaxPacket(
    _In_ BOOLEAN HighSpeed,
    _In_ UCHAR DataEpInterval,
    _In_ ULONG SampleRateHz,
    _In_ UCHAR Channels,
    _In_ UCHAR SubslotBytes)
{
    ULONG Intervals;
    ULONG CeilFrames;
    ULONG64 Minimum;

    if (SampleRateHz == 0 || Channels == 0 || SubslotBytes == 0)
        return 0;

    Intervals = UacDescServiceIntervals(HighSpeed, DataEpInterval);
    if (Intervals == 0)
        return 0;

    CeilFrames = (SampleRateHz + Intervals - 1) / Intervals;
    Minimum = ((ULONG64)CeilFrames + 1) * (ULONG64)Channels * (ULONG64)SubslotBytes;
    if (Minimum == 0 || Minimum > UAC_DESC_WMAX_SIZE_MASK)
        return 0;

    return (ULONG)Minimum;
}
