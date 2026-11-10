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
    ULONG NominalQ16;
    ULONG CurrentQ16;
} UAC_FEEDBACK_STATE, *PUAC_FEEDBACK_STATE;

VOID
UacFeedbackInit(
    _Out_ PUAC_FEEDBACK_STATE State,
    _In_ ULONG SampleRateHz,
    _In_ ULONG ServiceIntervalsPerSecond);

VOID
UacFeedbackFeed(
    _Inout_ PUAC_FEEDBACK_STATE State,
    _In_ ULONG FeedbackQ16);

#endif
