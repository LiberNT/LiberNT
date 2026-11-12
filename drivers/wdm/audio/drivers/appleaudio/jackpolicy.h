/*
 * PROJECT:     LiberNT Apple Silicon Audio Driver
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Headphone jack polling policy around the codec interrupt
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#define APPLE_AUDIO_JACK_POLL_MS            500
#define APPLE_AUDIO_JACK_FALLBACK_POLL_MS   4000
#define APPLE_AUDIO_JACK_SPURIOUS_MAX       32

#define APPLE_AUDIO_JACK_NO_CHANGE          0
#define APPLE_AUDIO_JACK_DELIVERY_PROVEN    1
#define APPLE_AUDIO_JACK_CODEC_PROVEN       2
#define APPLE_AUDIO_JACK_INTERRUPT_BROKEN   3

typedef struct _APPLE_AUDIO_JACK_POLICY
{
    LONG InterruptsHandled;
    ULONG Spurious;
    ULONG PollMs;
    BOOLEAN DeliveryProven;
    BOOLEAN CodecProven;
    BOOLEAN Broken;
} APPLE_AUDIO_JACK_POLICY, *PAPPLE_AUDIO_JACK_POLICY;

VOID
AppleAudioJackPolicyInitialize(
    _Out_ PAPPLE_AUDIO_JACK_POLICY Policy);

ULONG
AppleAudioJackPolicyUpdate(
    _Inout_ PAPPLE_AUDIO_JACK_POLICY Policy,
    _In_ LONG Interrupts,
    _In_ BOOLEAN Changed,
    _In_ BOOLEAN TipLatched);

#ifdef __cplusplus
}
#endif
