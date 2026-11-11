/*
 * PROJECT:     LiberNT Text-to-Speech Engine
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Rule-based English formant synthesizer interface
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

typedef struct _SYNTH_PARAMS
{
    ULONG SampleRate;
    LONG Rate;
    ULONG Volume;
    LONG Pitch;
} SYNTH_PARAMS, *PSYNTH_PARAMS;

typedef HRESULT (*SYNTH_WRITE_ROUTINE)(
    _In_ PVOID Context,
    _In_reads_(Count) const SHORT *Samples,
    _In_ ULONG Count);

typedef HRESULT (*SYNTH_WORD_ROUTINE)(
    _In_ PVOID Context,
    _In_ ULONG TextOffset,
    _In_ ULONG TextLength,
    _In_ ULONGLONG SampleOffset);

typedef struct _SYNTH_OUTPUT
{
    SYNTH_WRITE_ROUTINE Write;
    SYNTH_WORD_ROUTINE Word;
    PVOID Context;
    ULONGLONG SamplesWritten;
} SYNTH_OUTPUT, *PSYNTH_OUTPUT;

HRESULT
SynthSpeakText(
    _In_reads_(Length) const WCHAR *Text,
    _In_ ULONG Length,
    _In_ BOOL SpellOut,
    _In_ const SYNTH_PARAMS *Params,
    _Inout_ PSYNTH_OUTPUT Output);

HRESULT
SynthSilence(
    _In_ ULONG Milliseconds,
    _In_ const SYNTH_PARAMS *Params,
    _Inout_ PSYNTH_OUTPUT Output);
