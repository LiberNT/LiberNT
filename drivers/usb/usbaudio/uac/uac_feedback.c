/*
 * PROJECT:     LiberNT
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Host-side UAC explicit feedback consumer
 * COPYRIGHT:   Copyright 2026 Zhou Ji Yang <yinglanxing@gmail.com>
 */
#include "../usbaudio.h"
#include "uac_feedback.h"

/* Accept only +/- one frame around nominal, as the class driver does */
#define UAC_FEEDBACK_MAX_DEVIATION (1UL << 16)

static
ULONG
UacFeedbackNominalQ16(
    _In_ ULONG SampleRateHz,
    _In_ ULONG ServiceIntervalsPerSecond)
{
    if (SampleRateHz == 0 || ServiceIntervalsPerSecond == 0)
        return 0;

    return (ULONG)(((ULONG64)SampleRateHz << 16) / (ULONG64)ServiceIntervalsPerSecond);
}

VOID
UacFeedbackInit(
    _Out_ PUAC_FEEDBACK_STATE State,
    _In_ ULONG SampleRateHz,
    _In_ ULONG ServiceIntervalsPerSecond)
{
    RtlZeroMemory(State, sizeof(*State));

    State->NominalQ16 = UacFeedbackNominalQ16(SampleRateHz, ServiceIntervalsPerSecond);
    State->CurrentQ16 = State->NominalQ16;
}

VOID
UacFeedbackFeed(
    _Inout_ PUAC_FEEDBACK_STATE State,
    _In_ ULONG FeedbackQ16)
{
    ULONG Delta;

    if (FeedbackQ16 == 0 || State->NominalQ16 == 0)
        return;

    Delta = (FeedbackQ16 > State->NominalQ16) ?
            (FeedbackQ16 - State->NominalQ16) :
            (State->NominalQ16 - FeedbackQ16);

    if (Delta > UAC_FEEDBACK_MAX_DEVIATION)
        return;

    State->CurrentQ16 = FeedbackQ16;
}
