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

#endif
