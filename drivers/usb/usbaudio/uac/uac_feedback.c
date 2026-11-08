/*
 * PROJECT:     LiberNT
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Host-side UAC explicit feedback consumer
 * COPYRIGHT:   Copyright 2026 Zhou Ji Yang <yinglanxing@gmail.com>
 */
#include "../usbaudio.h"
#include "uac_feedback.h"
#include "uac_play_format.h"

/* Accept only +/- one frame around nominal, as the class driver does */
#define UAC_FEEDBACK_MAX_DEVIATION (1UL << 16)

VOID
UacFeedbackInit(
    _Out_ PUAC_FEEDBACK_STATE State,
    _In_ ULONG SampleRateHz,
    _In_ ULONG ServiceIntervalsPerSecond,
    _In_ ULONG FrameBytes)
{
    RtlZeroMemory(State, sizeof(*State));

    State->ServiceIntervalsPerSecond = ServiceIntervalsPerSecond;
    State->FrameBytes = FrameBytes;
    State->NominalQ16 = UacFeedbackNominalQ16(SampleRateHz, ServiceIntervalsPerSecond);
    State->CurrentQ16 = State->NominalQ16;
    State->PacketFrames = State->NominalQ16 >> 16;
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

ULONG
UacFeedbackPacketFrames(
    _Inout_ PUAC_FEEDBACK_STATE State)
{
    ULONG Frames;

    State->Accumulator += State->CurrentQ16;
    Frames = State->Accumulator >> 16;
    State->Accumulator &= 0xFFFF;
    State->PacketFrames = Frames;
    State->Primed = TRUE;
    return Frames;
}

VOID
UacFeedbackXrun(
    _Inout_ PUAC_FEEDBACK_STATE State,
    _In_ ULONG Status)
{
    State->XrunCount++;
    State->LastUrbStatus = Status;
    State->Reprime = TRUE;
}

BOOLEAN
UacFeedbackConsumeReprime(
    _Inout_ PUAC_FEEDBACK_STATE State)
{
    BOOLEAN Reprime = State->Reprime;

    State->Reprime = FALSE;
    return Reprime;
}

VOID
UacFeedbackReset(
    _Inout_ PUAC_FEEDBACK_STATE State)
{
    State->Accumulator = 0;
    State->CurrentQ16 = State->NominalQ16;
    State->Primed = FALSE;
    State->Reprime = FALSE;
}
