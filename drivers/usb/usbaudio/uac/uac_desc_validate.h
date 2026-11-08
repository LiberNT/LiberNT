/*
 * PROJECT:     LiberNT
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Static checks for one isochronous audio alt-setting pair
 * COPYRIGHT:   Copyright 2026 Zhou Ji Yang <yinglanxing@gmail.com>
 */
#ifndef _USBAUDIO_UAC_DESC_VALIDATE_H_
#define _USBAUDIO_UAC_DESC_VALIDATE_H_

#define UAC_DESC_EP_XFER_MASK 0x03
#define UAC_DESC_EP_XFER_ISO 0x01
#define UAC_DESC_EP_SYNC_MASK 0x0C
#define UAC_DESC_EP_SYNC_ASYNC 0x04
#define UAC_DESC_EP_USAGE_MASK 0x30
#define UAC_DESC_EP_USAGE_FEEDBACK 0x10
#define UAC_DESC_EP_USAGE_IMPLICIT_FB 0x20

typedef enum _UAC_DESC_STATUS
{
    UacDescOk = 0,
    UacDescNull,
    UacDescGeometry,
    UacDescFbWMaxPacket,
    UacDescBInterval,
    UacDescSynchAddress,
    UacDescDataNotAsync,
    UacDescDataWMaxPacket
} UAC_DESC_STATUS;

typedef struct _UAC_DESC_FACTS
{
    BOOLEAN HighSpeed;

    UCHAR DataEpAddress;
    UCHAR DataEpAttributes;
    UCHAR DataEpInterval;
    USHORT DataEpWMaxPacket;
    UCHAR DataEpSynchAddress;

    UCHAR FeedbackEpAddress;
    UCHAR FeedbackEpInterval;
    USHORT FeedbackEpWMaxPacket;

    ULONG SampleRateHz;
    UCHAR Channels;
    UCHAR SubslotBytes;
} UAC_DESC_FACTS, *PUAC_DESC_FACTS;

UAC_DESC_STATUS
UacDescValidate(
    _In_ const UAC_DESC_FACTS *Facts);

UAC_DESC_STATUS
UacDescValidateIsoPair(
    _In_ const UAC_DESC_FACTS *Facts);

ULONG
UacDescMinDataWMaxPacket(
    _In_ BOOLEAN HighSpeed,
    _In_ UCHAR DataEpInterval,
    _In_ ULONG SampleRateHz,
    _In_ UCHAR Channels,
    _In_ UCHAR SubslotBytes);

#endif
