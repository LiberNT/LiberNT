/*
 * PROJECT:     LiberNT Apple Silicon Audio Driver
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Headphone jack polling policy around the codec interrupt
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#ifndef UNIT_TEST
#include <ntddk.h>
#endif
#include "jackpolicy.h"

VOID
AppleAudioJackPolicyInitialize(
    _Out_ PAPPLE_AUDIO_JACK_POLICY Policy)
{
    RtlZeroMemory(Policy, sizeof(*Policy));
    Policy->PollMs = APPLE_AUDIO_JACK_POLL_MS;
}

ULONG
AppleAudioJackPolicyUpdate(
    _Inout_ PAPPLE_AUDIO_JACK_POLICY Policy,
    _In_ LONG Interrupts,
    _In_ BOOLEAN Changed,
    _In_ BOOLEAN TipLatched)
{
    LONG Delta = Interrupts - Policy->InterruptsHandled;

    ASSERT(Delta >= 0);
    Policy->InterruptsHandled = Interrupts;
    if (Policy->Broken)
        return APPLE_AUDIO_JACK_NO_CHANGE;
    if (Changed || TipLatched)
        Policy->Spurious = 0;
    else if (Delta > 0)
        Policy->Spurious += (ULONG)Delta;
    if (Policy->Spurious > APPLE_AUDIO_JACK_SPURIOUS_MAX)
    {
        Policy->Broken = TRUE;
        Policy->PollMs = APPLE_AUDIO_JACK_POLL_MS;
        return APPLE_AUDIO_JACK_INTERRUPT_BROKEN;
    }
    if (Delta <= 0)
        return APPLE_AUDIO_JACK_NO_CHANGE;
    if (TipLatched && !Policy->CodecProven)
    {
        Policy->CodecProven = TRUE;
        Policy->DeliveryProven = TRUE;
        Policy->PollMs = 0;
        return APPLE_AUDIO_JACK_CODEC_PROVEN;
    }
    if (!Policy->DeliveryProven)
    {
        Policy->DeliveryProven = TRUE;
        if (!Policy->CodecProven)
            Policy->PollMs = APPLE_AUDIO_JACK_FALLBACK_POLL_MS;
        return APPLE_AUDIO_JACK_DELIVERY_PROVEN;
    }
    return APPLE_AUDIO_JACK_NO_CHANGE;
}
