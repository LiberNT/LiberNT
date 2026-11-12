/*
 * PROJECT:     LiberNT Text-to-Speech Engine
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Prosody and cascade/parallel formant synthesis
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "synthcore.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define FRAME_MS 5
#define BASE_PITCH 112.0
#define MAX_PHRASE_WORDS 64
#define OUTPUT_CHUNK 2048

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

typedef enum _KIND
{
    KIND_PAUSE,
    KIND_VOWEL,
    KIND_DIPHTHONG,
    KIND_APPROXIMANT,
    KIND_NASAL,
    KIND_FRICATIVE,
    KIND_STOP,
    KIND_AFFRICATE,
    KIND_ASPIRATE
} KIND;

typedef struct _PHONEME_DATA
{
    UCHAR Kind;
    UCHAR Voiced;
    USHORT Duration;
    USHORT F1, F2, F3;
    USHORT EndF1, EndF2, EndF3;
    USHORT FricFrequency, FricBandwidth;
    UCHAR FricAmplitude;
    UCHAR VoiceAmplitude;
} PHONEME_DATA;

static const PHONEME_DATA Phonemes[PH_COUNT] =
{
    [PH_PAUSE] = { KIND_PAUSE, 0, 0 },
    [PH_AA] = { KIND_VOWEL, 1, 140, 730, 1090, 2440, 0, 0, 0, 0, 0, 0, 100 },
    [PH_AE] = { KIND_VOWEL, 1, 140, 660, 1720, 2410, 0, 0, 0, 0, 0, 0, 100 },
    [PH_AH] = { KIND_VOWEL, 1, 90, 640, 1190, 2390, 0, 0, 0, 0, 0, 0, 100 },
    [PH_AO] = { KIND_VOWEL, 1, 140, 570, 840, 2410, 0, 0, 0, 0, 0, 0, 100 },
    [PH_AW] = { KIND_DIPHTHONG, 1, 180, 720, 1250, 2500, 450, 1000, 2300, 0, 0, 0, 100 },
    [PH_AY] = { KIND_DIPHTHONG, 1, 180, 700, 1220, 2600, 380, 2000, 2600, 0, 0, 0, 100 },
    [PH_EH] = { KIND_VOWEL, 1, 110, 530, 1840, 2480, 0, 0, 0, 0, 0, 0, 100 },
    [PH_ER] = { KIND_VOWEL, 1, 130, 490, 1350, 1690, 0, 0, 0, 0, 0, 0, 100 },
    [PH_EY] = { KIND_DIPHTHONG, 1, 160, 480, 1720, 2520, 330, 2200, 2700, 0, 0, 0, 100 },
    [PH_IH] = { KIND_VOWEL, 1, 90, 390, 1990, 2550, 0, 0, 0, 0, 0, 0, 100 },
    [PH_IY] = { KIND_VOWEL, 1, 130, 270, 2290, 3010, 0, 0, 0, 0, 0, 0, 100 },
    [PH_OW] = { KIND_DIPHTHONG, 1, 160, 540, 1000, 2400, 440, 900, 2300, 0, 0, 0, 100 },
    [PH_OY] = { KIND_DIPHTHONG, 1, 190, 550, 960, 2400, 400, 1900, 2600, 0, 0, 0, 100 },
    [PH_UH] = { KIND_VOWEL, 1, 100, 440, 1020, 2240, 0, 0, 0, 0, 0, 0, 100 },
    [PH_UW] = { KIND_VOWEL, 1, 130, 300, 870, 2240, 0, 0, 0, 0, 0, 0, 100 },
    [PH_B] = { KIND_STOP, 1, 75, 200, 900, 2100, 0, 0, 0, 1000, 2000, 40, 0 },
    [PH_CH] = { KIND_AFFRICATE, 0, 120, 300, 1800, 2600, 0, 0, 0, 2800, 1300, 75, 0 },
    [PH_D] = { KIND_STOP, 1, 75, 200, 1700, 2600, 0, 0, 0, 4000, 2000, 45, 0 },
    [PH_DH] = { KIND_FRICATIVE, 1, 60, 300, 1500, 2500, 0, 0, 0, 5000, 6000, 20, 55 },
    [PH_F] = { KIND_FRICATIVE, 0, 100, 300, 1100, 2200, 0, 0, 0, 4000, 6000, 35, 0 },
    [PH_G] = { KIND_STOP, 1, 75, 200, 1900, 2400, 0, 0, 0, 2000, 1500, 45, 0 },
    [PH_HH] = { KIND_ASPIRATE, 0, 70, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
    [PH_JH] = { KIND_AFFRICATE, 1, 100, 300, 1800, 2600, 0, 0, 0, 2800, 1300, 55, 40 },
    [PH_K] = { KIND_STOP, 0, 95, 250, 1900, 2400, 0, 0, 0, 2200, 1500, 60, 0 },
    [PH_L] = { KIND_APPROXIMANT, 1, 70, 310, 1050, 2880, 0, 0, 0, 0, 0, 0, 75 },
    [PH_M] = { KIND_NASAL, 1, 80, 280, 900, 2200, 0, 0, 0, 0, 0, 0, 60 },
    [PH_N] = { KIND_NASAL, 1, 75, 280, 1500, 2600, 0, 0, 0, 0, 0, 0, 60 },
    [PH_NG] = { KIND_NASAL, 1, 80, 280, 2200, 2800, 0, 0, 0, 0, 0, 0, 60 },
    [PH_P] = { KIND_STOP, 0, 95, 250, 900, 2100, 0, 0, 0, 1000, 2000, 50, 0 },
    [PH_R] = { KIND_APPROXIMANT, 1, 70, 310, 1060, 1380, 0, 0, 0, 0, 0, 0, 75 },
    [PH_S] = { KIND_FRICATIVE, 0, 120, 300, 1700, 2600, 0, 0, 0, 5500, 1500, 80, 0 },
    [PH_SH] = { KIND_FRICATIVE, 0, 120, 300, 1800, 2600, 0, 0, 0, 2800, 1200, 80, 0 },
    [PH_T] = { KIND_STOP, 0, 95, 250, 1700, 2600, 0, 0, 0, 4500, 2000, 60, 0 },
    [PH_TH] = { KIND_FRICATIVE, 0, 100, 300, 1400, 2500, 0, 0, 0, 5000, 6000, 30, 0 },
    [PH_V] = { KIND_FRICATIVE, 1, 80, 300, 1100, 2200, 0, 0, 0, 4000, 6000, 25, 55 },
    [PH_W] = { KIND_APPROXIMANT, 1, 70, 290, 610, 2150, 0, 0, 0, 0, 0, 0, 75 },
    [PH_Y] = { KIND_APPROXIMANT, 1, 70, 260, 2070, 3020, 0, 0, 0, 0, 0, 0, 75 },
    [PH_Z] = { KIND_FRICATIVE, 1, 100, 300, 1700, 2600, 0, 0, 0, 5500, 1500, 55, 50 },
    [PH_ZH] = { KIND_FRICATIVE, 1, 100, 300, 1800, 2600, 0, 0, 0, 2800, 1200, 55, 50 },
};

typedef struct _FRAME
{
    float F1, F2, F3;
    float B1, B2, B3;
    float Voice;
    float Aspiration;
    float Frication;
    float FricFrequency;
    float FricBandwidth;
    float Pitch;
} FRAME;

typedef struct _RESONATOR
{
    float A, B, C;
    float Y1, Y2;
} RESONATOR;

typedef struct _PHRASE_WORD
{
    ULONG TextOffset;
    ULONG TextLength;
    ULONG FirstPhoneme;
    ULONG PhonemeCount;
} PHRASE_WORD;

typedef struct _PHRASE
{
    PHRASE_WORD Words[MAX_PHRASE_WORDS];
    ULONG WordCount;
    UCHAR Phonemes[MAX_PHRASE_WORDS * MAX_WORD_PHONEMES];
    ULONG PhonemeCount;
    WCHAR Terminator;
} PHRASE;

typedef struct _SYNTH_STATE
{
    const SYNTH_PARAMS *Params;
    PSYNTH_OUTPUT Output;
    float SampleRate;
    float Speed;
    float PitchScale;
    float Volume;
    float LastScale;
    RESONATOR Cascade[5];
    RESONATOR Fric;
    float Phase;
    float LastOutput;
    ULONG Noise;
    FRAME Previous;
    float *Pending;
    ULONG PendingCount;
    ULONG PendingCapacity;
    BOOL OutOfMemory;
    SHORT Buffer[OUTPUT_CHUNK];
} SYNTH_STATE;

static void
SetResonator(
    _Inout_ RESONATOR *Resonator,
    _In_ float Frequency,
    _In_ float Bandwidth,
    _In_ float SampleRate)
{
    double R = exp(-M_PI * Bandwidth / SampleRate);

    if (Frequency >= SampleRate * 0.45f)
    {
        Resonator->A = 1.0f;
        Resonator->B = 0.0f;
        Resonator->C = 0.0f;
        return;
    }
    Resonator->C = (float)(-R * R);
    Resonator->B = (float)(2.0 * R * cos(2.0 * M_PI * Frequency / SampleRate));
    Resonator->A = 1.0f - Resonator->B - Resonator->C;
}

static float
RunResonator(
    _Inout_ RESONATOR *Resonator,
    _In_ float Input)
{
    float Output = Resonator->A * Input + Resonator->B * Resonator->Y1 + Resonator->C * Resonator->Y2;

    Resonator->Y2 = Resonator->Y1;
    Resonator->Y1 = Output;
    return Output;
}

static float
NextNoise(
    _Inout_ SYNTH_STATE *State)
{
    State->Noise = State->Noise * 1103515245u + 12345u;
    return ((float)((State->Noise >> 9) & 0xFFFF) / 32768.0f) - 1.0f;
}

static HRESULT
FlushPending(
    _Inout_ SYNTH_STATE *State,
    _In_ float Scale)
{
    HRESULT hr = S_OK;
    ULONG Done = 0, Count, i;

    while (Done < State->PendingCount && SUCCEEDED(hr))
    {
        Count = min(State->PendingCount - Done, OUTPUT_CHUNK);
        for (i = 0; i < Count; i++)
        {
            float Value = State->Pending[Done + i] * Scale;

            if (Value > 1.0f)
                Value = 1.0f;
            else if (Value < -1.0f)
                Value = -1.0f;
            State->Buffer[i] = (SHORT)(Value * 32000.0f);
        }
        hr = State->Output->Write(State->Output->Context, State->Buffer, Count);
        State->Output->SamplesWritten += Count;
        Done += Count;
    }
    State->PendingCount = 0;
    return hr;
}

static void
EmitSample(
    _Inout_ SYNTH_STATE *State,
    _In_ float Value)
{
    float *Grown;
    ULONG Capacity;

    if (State->PendingCount == State->PendingCapacity)
    {
        Capacity = State->PendingCapacity ? State->PendingCapacity * 2 : 65536;
        Grown = realloc(State->Pending, Capacity * sizeof(*Grown));
        if (!Grown)
        {
            State->OutOfMemory = TRUE;
            return;
        }
        State->Pending = Grown;
        State->PendingCapacity = Capacity;
    }
    State->Pending[State->PendingCount++] = Value;
}

static HRESULT
RenderFrame(
    _Inout_ SYNTH_STATE *State,
    _In_ const FRAME *Frame)
{
    ULONG Samples = (ULONG)(State->SampleRate * FRAME_MS / 1000.0f);
    const FRAME *Last = &State->Previous;
    float OpenQuotient = 0.6f;
    ULONG i;

    SetResonator(&State->Cascade[0], Frame->F1, Frame->B1, State->SampleRate);
    SetResonator(&State->Cascade[1], Frame->F2, Frame->B2, State->SampleRate);
    SetResonator(&State->Cascade[2], Frame->F3, Frame->B3, State->SampleRate);
    SetResonator(&State->Fric, Frame->FricFrequency, Frame->FricBandwidth, State->SampleRate);

    for (i = 0; i < Samples; i++)
    {
        float t = (float)i / (float)Samples;
        float Voice = Last->Voice + (Frame->Voice - Last->Voice) * t;
        float Aspiration = Last->Aspiration + (Frame->Aspiration - Last->Aspiration) * t;
        float Frication = Last->Frication + (Frame->Frication - Last->Frication) * t;
        float Pitch = Last->Pitch + (Frame->Pitch - Last->Pitch) * t;
        float Flow = 0.0f, Noise, Cascade, Parallel, Sum, x;
        ULONG k;

        State->Phase += Pitch / State->SampleRate;
        if (State->Phase >= 1.0f)
            State->Phase -= 1.0f;
        if (State->Phase < OpenQuotient)
        {
            x = State->Phase / OpenQuotient;
            Flow = 6.75f * (x * x - x * x * x);
        }

        Noise = NextNoise(State);
        Cascade = Voice * Flow + Aspiration * Noise * (0.4f + 0.6f * Flow);
        for (k = 0; k < ARRAYSIZE(State->Cascade); k++)
            Cascade = RunResonator(&State->Cascade[k], Cascade);

        if (Frame->FricBandwidth >= 4000.0f)
            Parallel = Frication * Noise * 0.25f;
        else
            Parallel = RunResonator(&State->Fric, Frication * Noise) * 0.5f;

        Sum = Cascade + Parallel;
        EmitSample(State, Sum - State->LastOutput);
        State->LastOutput = Sum;
    }

    State->Previous = *Frame;
    return State->OutOfMemory ? E_OUTOFMEMORY : S_OK;
}

static ULONG
FrameCount(
    _In_ const SYNTH_STATE *State,
    _In_ float Milliseconds)
{
    float Frames = Milliseconds / (FRAME_MS * State->Speed);

    return Frames < 1.0f ? 1 : (ULONG)(Frames + 0.5f);
}

static void
SetVoicedTarget(
    _Out_ FRAME *Frame,
    _In_ const PHONEME_DATA *Data,
    _In_ float Blend)
{
    float F1 = Data->F1, F2 = Data->F2, F3 = Data->F3;

    if (Data->Kind == KIND_DIPHTHONG)
    {
        F1 += (Data->EndF1 - F1) * Blend;
        F2 += (Data->EndF2 - F2) * Blend;
        F3 += (Data->EndF3 - F3) * Blend;
    }
    Frame->F1 = F1;
    Frame->F2 = F2;
    Frame->F3 = F3;
    Frame->B1 = Data->Kind == KIND_NASAL ? 100.0f : 60.0f;
    Frame->B2 = Data->Kind == KIND_NASAL ? 300.0f : 90.0f;
    Frame->B3 = Data->Kind == KIND_NASAL ? 400.0f : 150.0f;
}

static const PHONEME_DATA *
NextVoiced(
    _In_ const PHRASE *Phrase,
    _In_ ULONG Index)
{
    for (Index++; Index < Phrase->PhonemeCount; Index++)
    {
        const PHONEME_DATA *Data = &Phonemes[Phrase->Phonemes[Index]];

        if (Data->Kind == KIND_VOWEL || Data->Kind == KIND_DIPHTHONG || Data->Kind == KIND_APPROXIMANT)
            return Data;
    }
    return &Phonemes[PH_AH];
}

static float
PhrasePitch(
    _In_ const SYNTH_STATE *State,
    _In_ const PHRASE *Phrase,
    _In_ float Progress,
    _In_ BOOL Accent)
{
    float Pitch = (float)BASE_PITCH * (1.1f - 0.2f * Progress);

    if (Phrase->Terminator == L'?' && Progress > 0.7f)
        Pitch *= 1.0f + (Progress - 0.7f) * 1.3f;
    else if (Phrase->Terminator != L',' && Progress > 0.8f)
        Pitch *= 1.0f - (Progress - 0.8f) * 0.9f;
    if (Accent)
        Pitch *= 1.12f;
    return Pitch * State->PitchScale;
}

static HRESULT
RenderPhrase(
    _Inout_ SYNTH_STATE *State,
    _In_ const PHRASE *Phrase)
{
    ULONG WordStart[MAX_PHRASE_WORDS];
    ULONG Total = 0, Done = 0, w, p, f, Count, i;
    float Peak = 0.0f, Scale = 0.0f;
    HRESULT hr = S_OK;
    FRAME Target, Smooth;

    for (p = 0; p < Phrase->PhonemeCount; p++)
        Total += FrameCount(State, Phonemes[Phrase->Phonemes[p]].Duration);
    if (!Total)
        return S_OK;

    Smooth = State->Previous;
    for (w = 0; w < Phrase->WordCount && SUCCEEDED(hr); w++)
    {
        const PHRASE_WORD *Word = &Phrase->Words[w];
        BOOL Accented = FALSE;

        WordStart[w] = State->PendingCount;

        for (p = Word->FirstPhoneme; p < Word->FirstPhoneme + Word->PhonemeCount && SUCCEEDED(hr); p++)
        {
            const PHONEME_DATA *Data = &Phonemes[Phrase->Phonemes[p]];
            BOOL Vowel = Data->Kind == KIND_VOWEL || Data->Kind == KIND_DIPHTHONG;
            BOOL Accent = Vowel && !Accented;
            float Length = Data->Duration;

            if (Vowel && Accented)
                Length *= 0.75f;
            if (Vowel && p + 1 == Phrase->PhonemeCount)
                Length *= 1.4f;
            if (Vowel)
                Accented = TRUE;
            Count = FrameCount(State, Length);

            for (f = 0; f < Count && SUCCEEDED(hr); f++)
            {
                float Blend = (float)f / (float)Count;
                ULONG Closure = Count / 2, Burst = Closure + 2;

                memset(&Target, 0, sizeof(Target));
                Target.FricFrequency = Data->FricFrequency ? Data->FricFrequency : 3000.0f;
                Target.FricBandwidth = Data->FricBandwidth ? Data->FricBandwidth : 2000.0f;

                switch (Data->Kind)
                {
                    case KIND_VOWEL:
                    case KIND_DIPHTHONG:
                    case KIND_APPROXIMANT:
                    case KIND_NASAL:
                        SetVoicedTarget(&Target, Data, Blend);
                        Target.Voice = Data->VoiceAmplitude / 100.0f;
                        break;
                    case KIND_FRICATIVE:
                        SetVoicedTarget(&Target, Data, Blend);
                        Target.Voice = Data->VoiceAmplitude / 100.0f;
                        Target.Frication = Data->FricAmplitude / 100.0f;
                        break;
                    case KIND_ASPIRATE:
                        SetVoicedTarget(&Target, NextVoiced(Phrase, p), 0.0f);
                        Target.Aspiration = 0.6f;
                        break;
                    case KIND_STOP:
                    case KIND_AFFRICATE:
                        SetVoicedTarget(&Target, Data, 0.0f);
                        if (f < Closure)
                        {
                            Target.Voice = Data->Voiced ? 0.15f : 0.0f;
                        }
                        else if (Data->Kind == KIND_AFFRICATE)
                        {
                            Target.Voice = Data->VoiceAmplitude / 100.0f;
                            Target.Frication = Data->FricAmplitude / 100.0f;
                        }
                        else if (f < Burst)
                        {
                            Target.Frication = Data->FricAmplitude / 100.0f;
                        }
                        else if (!Data->Voiced)
                        {
                            SetVoicedTarget(&Target, NextVoiced(Phrase, p), 0.0f);
                            Target.Aspiration = 0.5f;
                        }
                        else
                        {
                            Target.Voice = 0.5f;
                        }
                        break;
                    default:
                        Target = Smooth;
                        Target.Voice = Target.Aspiration = Target.Frication = 0.0f;
                        break;
                }

                Target.Pitch = PhrasePitch(State, Phrase, (float)Done / (float)Total, Accent && f > Count / 4);
                Done++;

                if (Smooth.F1 == 0.0f)
                    Smooth = Target;
                Smooth.F1 += (Target.F1 - Smooth.F1) * 0.45f;
                Smooth.F2 += (Target.F2 - Smooth.F2) * 0.45f;
                Smooth.F3 += (Target.F3 - Smooth.F3) * 0.45f;
                Smooth.B1 = Target.B1;
                Smooth.B2 = Target.B2;
                Smooth.B3 = Target.B3;
                Smooth.Voice += (Target.Voice - Smooth.Voice) * 0.7f;
                Smooth.Aspiration += (Target.Aspiration - Smooth.Aspiration) * 0.8f;
                Smooth.Frication = Target.Frication;
                Smooth.FricFrequency = Target.FricFrequency;
                Smooth.FricBandwidth = Target.FricBandwidth;
                Smooth.Pitch += (Target.Pitch - Smooth.Pitch) * 0.5f;
                if (Smooth.Pitch < 40.0f)
                    Smooth.Pitch = Target.Pitch;

                hr = RenderFrame(State, &Smooth);
            }
        }
    }
    if (FAILED(hr))
        return hr;

    for (i = 0; i < State->PendingCount; i++)
        Peak = max(Peak, (float)fabs(State->Pending[i]));
    if (Peak > 1e-6f)
        Scale = 0.7f * State->Volume / Peak;
    State->LastScale = Scale;

    for (w = 0; w < Phrase->WordCount && SUCCEEDED(hr); w++)
    {
        if (State->Output->Word && Phrase->Words[w].TextLength)
            hr = State->Output->Word(State->Output->Context, Phrase->Words[w].TextOffset, Phrase->Words[w].TextLength,
                                     State->Output->SamplesWritten + WordStart[w]);
    }
    if (SUCCEEDED(hr))
        hr = FlushPending(State, Scale);
    return hr;
}

static HRESULT
RenderSilence(
    _Inout_ SYNTH_STATE *State,
    _In_ ULONG Milliseconds)
{
    FRAME Quiet = State->Previous;
    ULONG Frames = Milliseconds / FRAME_MS, f;
    HRESULT hr = S_OK;

    Quiet.Voice = Quiet.Aspiration = Quiet.Frication = 0.0f;
    if (Quiet.Pitch < 40.0f)
        Quiet.Pitch = (float)BASE_PITCH;
    if (Quiet.F1 == 0.0f)
    {
        SetVoicedTarget(&Quiet, &Phonemes[PH_AH], 0.0f);
        Quiet.FricFrequency = 3000.0f;
        Quiet.FricBandwidth = 2000.0f;
    }
    for (f = 0; f < Frames && SUCCEEDED(hr); f++)
        hr = RenderFrame(State, &Quiet);
    if (SUCCEEDED(hr))
        hr = FlushPending(State, State->LastScale);
    return hr;
}

static void
InitializeState(
    _Out_ SYNTH_STATE *State,
    _In_ const SYNTH_PARAMS *Params,
    _Inout_ PSYNTH_OUTPUT Output)
{
    LONG Rate = max(-10, min(10, Params->Rate));
    LONG Pitch = max(-10, min(10, Params->Pitch));

    memset(State, 0, sizeof(*State));
    State->Params = Params;
    State->Output = Output;
    State->SampleRate = (float)Params->SampleRate;
    State->Speed = (float)pow(3.0, Rate / 10.0);
    State->PitchScale = (float)pow(2.0, Pitch / 24.0);
    State->Volume = (float)min(Params->Volume, 100) / 100.0f;
    State->Noise = 0x2545F491u;
    SetResonator(&State->Cascade[3], 3300.0f, 250.0f, State->SampleRate);
    SetResonator(&State->Cascade[4], 3750.0f, 200.0f, State->SampleRate);
}

static BOOL
AddWordPhonemes(
    _Inout_ PHRASE *Phrase,
    _In_ ULONG TextOffset,
    _In_ ULONG TextLength,
    _In_reads_(Count) const UCHAR *Sounds,
    _In_ ULONG Count)
{
    PHRASE_WORD *Word;

    if (Phrase->WordCount == MAX_PHRASE_WORDS ||
        Phrase->PhonemeCount + Count > ARRAYSIZE(Phrase->Phonemes))
    {
        return FALSE;
    }

    Word = &Phrase->Words[Phrase->WordCount++];
    Word->TextOffset = TextOffset;
    Word->TextLength = TextLength;
    Word->FirstPhoneme = Phrase->PhonemeCount;
    Word->PhonemeCount = Count;
    memcpy(Phrase->Phonemes + Phrase->PhonemeCount, Sounds, Count);
    Phrase->PhonemeCount += Count;
    return TRUE;
}

static ULONG
SpeakableLetters(
    _In_reads_(Length) const WCHAR *Text,
    _In_ ULONG Length,
    _Out_writes_z_(MAX_WORD_LETTERS + 1) char *Letters)
{
    ULONG i, Count = 0;

    for (i = 0; i < Length && Count < MAX_WORD_LETTERS; i++)
    {
        WCHAR c = Text[i];

        if (c >= L'a' && c <= L'z')
            c -= L'a' - L'A';
        if ((c >= L'A' && c <= L'Z') || c == L'\'')
            Letters[Count++] = (char)c;
    }
    Letters[Count] = 0;
    return Count;
}

static ULONG
WordSounds(
    _In_reads_(Length) const WCHAR *Text,
    _In_ ULONG Length,
    _In_ BOOL SpellOut,
    _Out_writes_(MAX_WORD_PHONEMES) UCHAR *Sounds)
{
    char Letters[MAX_WORD_LETTERS + 1];
    char Padded[MAX_WORD_LETTERS + 3];
    BOOL HasVowel = FALSE;
    ULONG Count = 0, i, n;

    n = SpeakableLetters(Text, Length, Letters);
    for (i = 0; i < n; i++)
    {
        if (strchr("AEIOUY", Letters[i]))
            HasVowel = TRUE;
    }

    if (SpellOut || !HasVowel)
    {
        for (i = 0; i < n && Count < MAX_WORD_PHONEMES; i++)
            Count += LtsSpellLetter(Letters[i], Sounds + Count, MAX_WORD_PHONEMES - Count);
        return Count;
    }

    Padded[0] = ' ';
    memcpy(Padded + 1, Letters, n);
    Padded[n + 1] = ' ';
    Padded[n + 2] = 0;
    return LtsWordToPhonemes(Padded, Sounds, MAX_WORD_PHONEMES);
}

static BOOL
AddNumberWords(
    _Inout_ PHRASE *Phrase,
    _In_z_ const char *Words,
    _In_ ULONG Offset,
    _In_ ULONG Length)
{
    UCHAR Sounds[MAX_WORD_PHONEMES];
    WCHAR Wide[32];
    ULONG i = 0, Letters, Count;

    while (Words[i])
    {
        while (Words[i] == ' ')
            i++;
        for (Letters = 0; Words[i] && Words[i] != ' '; i++)
        {
            if (Letters < ARRAYSIZE(Wide))
                Wide[Letters++] = Words[i];
        }
        if (!Letters)
            break;
        Count = WordSounds(Wide, Letters, FALSE, Sounds);
        if (!AddWordPhonemes(Phrase, Offset, Length, Sounds, Count))
            return FALSE;
        Length = 0;
    }
    return TRUE;
}

static BOOL
AddNumber(
    _Inout_ PHRASE *Phrase,
    _In_reads_(Length) const WCHAR *Text,
    _In_ ULONG Offset,
    _In_ ULONG Length)
{
    ULONGLONG Value = 0;
    char Words[256];
    ULONG i;

    if (Length > 12)
    {
        for (i = 0; i < Length; i++)
        {
            LtsNumberToWords(Text[Offset + i] - L'0', Words, sizeof(Words));
            if (!AddNumberWords(Phrase, Words, Offset + i, 1))
                return FALSE;
        }
        return TRUE;
    }

    for (i = 0; i < Length; i++)
        Value = Value * 10 + (Text[Offset + i] - L'0');
    LtsNumberToWords(Value, Words, sizeof(Words));
    return AddNumberWords(Phrase, Words, Offset, Length);
}

static BOOL IsWordChar(WCHAR c) { return (c >= L'A' && c <= L'Z') || (c >= L'a' && c <= L'z') || c == L'\''; }
static BOOL IsDigitChar(WCHAR c) { return c >= L'0' && c <= L'9'; }
static BOOL IsBreakChar(WCHAR c) { return c == L'.' || c == L',' || c == L';' || c == L':' || c == L'!' || c == L'?'; }

static HRESULT
FinishPhrase(
    _Inout_ SYNTH_STATE *State,
    _Inout_ PHRASE *Phrase)
{
    HRESULT hr = S_OK;

    if (Phrase->WordCount)
    {
        hr = RenderPhrase(State, Phrase);
        if (SUCCEEDED(hr) && Phrase->Terminator)
            hr = RenderSilence(State, Phrase->Terminator == L',' ? 180 : 320);
    }
    Phrase->WordCount = 0;
    Phrase->PhonemeCount = 0;
    Phrase->Terminator = 0;
    return hr;
}

HRESULT
SynthSpeakText(
    _In_reads_(Length) const WCHAR *Text,
    _In_ ULONG Length,
    _In_ BOOL SpellOut,
    _In_ const SYNTH_PARAMS *Params,
    _Inout_ PSYNTH_OUTPUT Output)
{
    UCHAR Sounds[MAX_WORD_PHONEMES];
    SYNTH_STATE *State;
    PHRASE *Phrase;
    ULONG i = 0, Start, Count;
    HRESULT hr = S_OK;

    State = calloc(1, sizeof(*State));
    Phrase = calloc(1, sizeof(*Phrase));
    if (!State || !Phrase)
    {
        free(State);
        free(Phrase);
        return E_OUTOFMEMORY;
    }
    InitializeState(State, Params, Output);

    while (i < Length && SUCCEEDED(hr))
    {
        Start = i;
        if (IsWordChar(Text[i]))
        {
            while (i < Length && IsWordChar(Text[i]))
                i++;
            Count = WordSounds(Text + Start, i - Start, SpellOut, Sounds);
            if (Count && !AddWordPhonemes(Phrase, Start, i - Start, Sounds, Count))
            {
                hr = FinishPhrase(State, Phrase);
                if (SUCCEEDED(hr))
                    AddWordPhonemes(Phrase, Start, i - Start, Sounds, Count);
            }
        }
        else if (IsDigitChar(Text[i]))
        {
            while (i < Length && IsDigitChar(Text[i]))
                i++;
            if (!AddNumber(Phrase, Text, Start, i - Start))
            {
                hr = FinishPhrase(State, Phrase);
                if (SUCCEEDED(hr))
                    AddNumber(Phrase, Text, Start, i - Start);
            }
        }
        else
        {
            if (IsBreakChar(Text[i]) && Phrase->WordCount)
            {
                Phrase->Terminator = Text[i];
                hr = FinishPhrase(State, Phrase);
            }
            i++;
        }
    }

    if (SUCCEEDED(hr))
        hr = FinishPhrase(State, Phrase);

    free(State->Pending);
    free(Phrase);
    free(State);
    return hr;
}

HRESULT
SynthSilence(
    _In_ ULONG Milliseconds,
    _In_ const SYNTH_PARAMS *Params,
    _Inout_ PSYNTH_OUTPUT Output)
{
    SYNTH_STATE *State;
    HRESULT hr;

    State = calloc(1, sizeof(*State));
    if (!State)
        return E_OUTOFMEMORY;
    InitializeState(State, Params, Output);
    hr = RenderSilence(State, Milliseconds);
    free(State->Pending);
    free(State);
    return hr;
}
