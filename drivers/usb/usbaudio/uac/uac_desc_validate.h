/*
 * PROJECT:     LiberNT
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Minimum data endpoint size for one isochronous alt setting
 * COPYRIGHT:   Copyright 2026 Zhou Ji Yang <yinglanxing@gmail.com>
 */
#ifndef _USBAUDIO_UAC_DESC_VALIDATE_H_
#define _USBAUDIO_UAC_DESC_VALIDATE_H_

ULONG
UacDescMinDataWMaxPacket(
    _In_ BOOLEAN HighSpeed,
    _In_ UCHAR DataEpInterval,
    _In_ ULONG SampleRateHz,
    _In_ UCHAR Channels,
    _In_ UCHAR SubslotBytes);

#endif
