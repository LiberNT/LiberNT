/*
 * PROJECT:     LiberNT Apple Silicon Audio Driver
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Speaker voice coil and magnet temperature model ported from speakersafetyd 2.0.2
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 *              Copyright 2022 The Asahi Linux Contributors
 *
 * The model and the speaker parameters are ported from speakersafetyd 2.0.2,
 * which was released under the MIT licence:
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of
 * this software and associated documentation files (the "Software"), to deal in
 * the Software without restriction, including without limitation the rights to
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
 * of the Software, and to permit persons to whom the Software is furnished to do
 * so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
 * FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 * COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#pragma once

#define APPLE_AUDIO_PROTECT_MAX_SPEAKERS    8
#define APPLE_AUDIO_PROTECT_MAX_GROUPS      4
#define APPLE_AUDIO_PROTECT_SAMPLE_RATE     48000

typedef struct _APPLE_AUDIO_SPEAKER_PROFILE
{
    PCSTR Name;
    ULONG Group;
    ULONG TrCoilMilli;
    ULONG TrMagnetMilli;
    ULONG TauCoilMs;
    ULONG TauMagnetMs;
    ULONG TLimitMilliC;
    ULONG THeadroomMilliC;
    ULONG ZNominalMilliOhm;
    ULONG IsScaleMilliA;
    ULONG VsScaleMilliV;
    ULONG IsChannel;
    ULONG VsChannel;
} APPLE_AUDIO_SPEAKER_PROFILE;

typedef struct _APPLE_AUDIO_BOARD_PROFILE
{
    PCSTR Compatible;
    ULONG TAmbientMilliC;
    ULONG THysteresisMilliC;
    ULONG TWindowMilliC;
    ULONG Channels;
    ULONG SpeakerCount;
    const APPLE_AUDIO_SPEAKER_PROFILE *Speakers;
} APPLE_AUDIO_BOARD_PROFILE;

typedef struct _APPLE_AUDIO_SPEAKER_MODEL
{
    const APPLE_AUDIO_SPEAKER_PROFILE *Profile;
    LONG64 TCoil;
    LONG64 TMagnet;
    LONG64 TCoilHysteresis;
    LONG64 TMagnetHysteresis;
    LONG64 CoilDivisor;
    LONG64 MagnetDivisor;
    LONG64 CoilRiseQ16;
    LONG64 MagnetRiseQ16;
    ULONG64 PowerScale;
    LONG MinGainMilliDb;
    LONG GainMilliDb;
    LONG PowerMilliW;
    ULONG PeakVoltage;
    ULONG PeakCurrent;
    BOOLEAN OverLimit;
    BOOLEAN BadData;
} APPLE_AUDIO_SPEAKER_MODEL, *PAPPLE_AUDIO_SPEAKER_MODEL;

typedef struct _APPLE_AUDIO_PROTECTION
{
    const APPLE_AUDIO_BOARD_PROFILE *Board;
    APPLE_AUDIO_SPEAKER_MODEL Speaker[APPLE_AUDIO_PROTECT_MAX_SPEAKERS];
    ULONG SpeakerCount;
    LONG GroupGainMilliDb[APPLE_AUDIO_PROTECT_MAX_GROUPS];
} APPLE_AUDIO_PROTECTION, *PAPPLE_AUDIO_PROTECTION;

const APPLE_AUDIO_BOARD_PROFILE *
AppleAudioProtectionFindBoard(
    _In_reads_bytes_(Length) const VOID *Compatible,
    _In_ ULONG Length);

NTSTATUS
AppleAudioProtectionAddSpeaker(
    _Inout_ PAPPLE_AUDIO_PROTECTION Protection,
    _In_reads_(NameLength) const CHAR *Name,
    _In_ ULONG NameLength,
    _In_ ULONG IsChannel,
    _In_ ULONG VsChannel,
    _In_ LONG AmpGainMilliDb,
    _In_ BOOLEAN ColdBoot,
    _Out_ PULONG Index);

VOID
AppleAudioProtectionSkip(
    _Inout_ PAPPLE_AUDIO_PROTECTION Protection,
    _In_ ULONG64 Samples);

NTSTATUS
AppleAudioProtectionRun(
    _Inout_ PAPPLE_AUDIO_PROTECTION Protection,
    _In_reads_(FrameCount * Protection->Board->Channels) const SHORT *Frames,
    _In_ ULONG FrameCount);

LONG
AppleAudioProtectionGain(
    _In_ PAPPLE_AUDIO_PROTECTION Protection,
    _In_ ULONG Index);

LONG
AppleAudioProtectionCeiling(
    _In_ PAPPLE_AUDIO_PROTECTION Protection,
    _In_ ULONG Index);

LONG
AppleAudioProtectionMilliCelsius(
    _In_ LONG64 Temperature);
