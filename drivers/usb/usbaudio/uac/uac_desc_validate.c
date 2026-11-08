/*
 * PROJECT:     LiberNT
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Static checks for one isochronous audio alt-setting pair
 * COPYRIGHT:   Copyright 2026 Zhou Ji Yang <yinglanxing@gmail.com>
 */
#include "../usbaudio.h"
#include "uac_desc_validate.h"

#define UAC_DESC_WMAX_SIZE_MASK 0x07FFUL
#define UAC_DESC_FS_FRAMES_PER_SECOND 1000UL
#define UAC_DESC_HS_FRAMES_PER_SECOND 8000UL
#define UAC_DESC_FS_FB_WMAX_PACKET 3
#define UAC_DESC_HS_FB_WMAX_PACKET 4
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

static
BOOLEAN
UacDescDataIsIsoAsync(
    _In_ UCHAR Attributes)
{
    return ((Attributes & UAC_DESC_EP_XFER_MASK) == UAC_DESC_EP_XFER_ISO) &&
           ((Attributes & UAC_DESC_EP_SYNC_MASK) == UAC_DESC_EP_SYNC_ASYNC);
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

UAC_DESC_STATUS
UacDescValidate(
    _In_ const UAC_DESC_FACTS *Facts)
{
    UCHAR ExpectedFeedbackSize;
    ULONG MinimumData;
    USHORT DataSize;

    if (Facts == NULL)
        return UacDescNull;

    if (Facts->SampleRateHz == 0 || Facts->Channels == 0 || Facts->SubslotBytes == 0)
        return UacDescGeometry;

    /* Full Speed explicit feedback is 3 bytes, High Speed is 4 */
    ExpectedFeedbackSize = Facts->HighSpeed ? UAC_DESC_HS_FB_WMAX_PACKET
                                            : UAC_DESC_FS_FB_WMAX_PACKET;
    if (Facts->FeedbackEpWMaxPacket != ExpectedFeedbackSize)
        return UacDescFbWMaxPacket;

    if (UacDescServiceIntervals(Facts->HighSpeed, Facts->DataEpInterval) == 0 ||
        UacDescServiceIntervals(Facts->HighSpeed, Facts->FeedbackEpInterval) == 0)
        return UacDescBInterval;

    if (Facts->DataEpSynchAddress == 0 ||
        Facts->FeedbackEpAddress == 0 ||
        Facts->DataEpSynchAddress != Facts->FeedbackEpAddress)
        return UacDescSynchAddress;

    if (!UacDescDataIsIsoAsync(Facts->DataEpAttributes))
        return UacDescDataNotAsync;

    MinimumData = UacDescMinDataWMaxPacket(Facts->HighSpeed,
                                           Facts->DataEpInterval,
                                           Facts->SampleRateHz,
                                           Facts->Channels,
                                           Facts->SubslotBytes);
    if (MinimumData == 0)
        return UacDescGeometry;

    DataSize = (USHORT)(Facts->DataEpWMaxPacket & UAC_DESC_WMAX_SIZE_MASK);
    if ((ULONG)DataSize < MinimumData)
        return UacDescDataWMaxPacket;

    return UacDescOk;
}

UAC_DESC_STATUS
UacDescValidateIsoPair(
    _In_ const UAC_DESC_FACTS *Facts)
{
    UAC_DESC_FACTS Paired;

    if (Facts == NULL)
        return UacDescNull;

    Paired = *Facts;
    if (Paired.DataEpSynchAddress == 0 && Paired.FeedbackEpAddress != 0)
        Paired.DataEpSynchAddress = Paired.FeedbackEpAddress;

    return UacDescValidate(&Paired);
}
