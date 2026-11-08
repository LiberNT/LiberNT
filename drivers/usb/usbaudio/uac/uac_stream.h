/*
 * PROJECT:     LiberNT USB Audio driver
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Isochronous stream arithmetic that depends on the bus speed
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */
#ifndef _USBAUDIO_UAC_STREAM_H_
#define _USBAUDIO_UAC_STREAM_H_

static __inline
ULONG
UacPacketRate(
    _In_ BOOLEAN HighSpeed,
    _In_ UCHAR Interval)
{
    ULONG Shift = Interval ? (ULONG)Interval - 1 : 0;

    if (Shift > 3)
        Shift = 3;

    return (HighSpeed ? 8000UL : 1000UL) >> Shift;
}

/* Audio 2.0 feature units give each control two bits (D1..0 mute, D3..2 volume, ...), Audio 1.0 one bit */
static __inline
ULONG
UacFeatureUnit2Controls(
    _In_ ULONG Bitmap)
{
    ULONG Controls = 0;
    ULONG Index;

    for (Index = 0; Index < 16; Index++)
    {
        if ((Bitmap >> (Index * 2)) & 3)
            Controls |= 1UL << Index;
    }
    return Controls;
}

/* High-speed feedback is Q16.16 frames per microframe; some devices report it per 1 ms frame */
static __inline
ULONG
UacNormalizeFeedback(
    _In_ BOOLEAN HighSpeed,
    _In_ ULONG FeedbackQ16,
    _In_ ULONG NominalQ16)
{
    if (HighSpeed && NominalQ16 && FeedbackQ16 > NominalQ16 * 2 && FeedbackQ16 <= NominalQ16 * 16)
        return FeedbackQ16 >> 3;
    return FeedbackQ16;
}

#endif
