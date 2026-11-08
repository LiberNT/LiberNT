/*
 * PROJECT:     LiberNT
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Host-side UAC explicit feedback consumer
 * COPYRIGHT:   Copyright 2026 Zhou Ji Yang <yinglanxing@gmail.com>
 */
#ifndef _USBAUDIO_UAC_FEEDBACK_H_
#define _USBAUDIO_UAC_FEEDBACK_H_

typedef struct _UAC_FEEDBACK_STATE
{
    ULONG NominalQ16;               /* nominal frames per service interval, Q16.16 */
    ULONG CurrentQ16;               /* last value accepted from the device */
    ULONG ServiceIntervalsPerSecond;
    ULONG FrameBytes;
    ULONG Accumulator;              /* Q16.16 remainder carried between packets */
    ULONG PacketFrames;             /* frames the next packet should carry */
    ULONG XrunCount;
    BOOLEAN Primed;                 /* a packet has been produced since (re)start */
    BOOLEAN Reprime;                /* a completion failed, refill the ring */
    ULONG LastUrbStatus;
} UAC_FEEDBACK_STATE, *PUAC_FEEDBACK_STATE;

VOID
UacFeedbackInit(
    _Out_ PUAC_FEEDBACK_STATE State,
    _In_ ULONG SampleRateHz,
    _In_ ULONG ServiceIntervalsPerSecond,
    _In_ ULONG FrameBytes);

VOID
UacFeedbackFeed(
    _Inout_ PUAC_FEEDBACK_STATE State,
    _In_ ULONG FeedbackQ16);

ULONG
UacFeedbackPacketFrames(
    _Inout_ PUAC_FEEDBACK_STATE State);

VOID
UacFeedbackXrun(
    _Inout_ PUAC_FEEDBACK_STATE State,
    _In_ ULONG Status);

BOOLEAN
UacFeedbackConsumeReprime(
    _Inout_ PUAC_FEEDBACK_STATE State);

VOID
UacFeedbackReset(
    _Inout_ PUAC_FEEDBACK_STATE State);

#endif
