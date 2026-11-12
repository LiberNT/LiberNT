/*
 * PROJECT:     LiberNT Apple Silicon Audio Driver
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Speaker voice coil and magnet temperature model ported from speakersafetyd 2.0.2
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 *              Copyright 2022 The Asahi Linux Contributors
 *
 * The model and the speaker parameters are ported from speakersafetyd 2.0.2
 * (src/types.rs, src/main.rs, conf/apple/j516.conf), which was released under
 * the MIT licence:
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

#include <ntddk.h>
#include <reactos/drivers/fdtdrv.h>
#include "protect.h"

#define PROTECT_ONE_Q62             (1ULL << 62)
#define PROTECT_CELSIUS(Milli)      ((((LONG64)(Milli)) << 32) / 1000)
#define PROTECT_SAMPLES_PER_MS      (APPLE_AUDIO_PROTECT_SAMPLE_RATE / 1000)
#define PROTECT_FULL_SCALE_SQUARED  (1ULL << 30)
#define PROTECT_NOMINAL_FLOOR_MDB   (-10)
#define PROTECT_NEGATIVE_FLOOR_MW   (-10)
#define PROTECT_REPORT_STEP_MDB     500

static const APPLE_AUDIO_SPEAKER_PROFILE AppleAudioJ516Speakers[] =
{
    { "Left Woofer 1", 1, 23000, 40000, 5600, 197250, 140000, 10000, 3600, 3750, 14000, 0, 1 },
    { "Right Woofer 1", 1, 23000, 40000, 5600, 197250, 140000, 10000, 3600, 3750, 14000, 2, 3 },
    { "Left Tweeter", 0, 45000, 50000, 1300, 73260, 140000, 10000, 3600, 3750, 14000, 4, 5 },
    { "Right Tweeter", 0, 45000, 50000, 1300, 73260, 140000, 10000, 3600, 3750, 14000, 6, 7 },
    { "Left Woofer 2", 1, 23000, 40000, 5600, 197250, 140000, 10000, 3600, 3750, 14000, 8, 9 },
    { "Right Woofer 2", 1, 23000, 40000, 5600, 197250, 140000, 10000, 3600, 3750, 14000, 10, 11 },
};

static const APPLE_AUDIO_BOARD_PROFILE AppleAudioBoards[] =
{
    { "apple,j516-macaudio", 50000, 5000, 20000, 12, RTL_NUMBER_OF(AppleAudioJ516Speakers), AppleAudioJ516Speakers },
};

static
ULONG64
ProtectMultiply(
    _In_ ULONG64 A,
    _In_ ULONG64 B,
    _In_ ULONG Shift)
{
    ULONG64 LowLow, Cross1, Cross2, High, Middle, Low;

    ASSERT(Shift > 0 && Shift < 64);
    LowLow = (A & MAXULONG) * (B & MAXULONG);
    Cross1 = (A >> 32) * (B & MAXULONG);
    Cross2 = (A & MAXULONG) * (B >> 32);
    High = (A >> 32) * (B >> 32);
    Middle = (LowLow >> 32) + (Cross1 & MAXULONG) + (Cross2 & MAXULONG);
    High += (Cross1 >> 32) + (Cross2 >> 32) + (Middle >> 32);
    Low = (Middle << 32) | (LowLow & MAXULONG);
    ASSERT((High >> Shift) == 0);
    return (High << (64 - Shift)) | (Low >> Shift);
}

static
LONG64
ProtectScale(
    _In_ LONG64 Value,
    _In_ ULONG64 Factor,
    _In_ ULONG Shift)
{
    ULONG64 Magnitude = (Value < 0) ? (ULONG64)(-Value) : (ULONG64)Value;
    LONG64 Scaled = (LONG64)ProtectMultiply(Magnitude, Factor, Shift);

    return (Value < 0) ? -Scaled : Scaled;
}

static
ULONG64
ProtectDecay(
    _In_ LONG64 Divisor,
    _In_ ULONG64 Samples)
{
    ULONG64 Base, Result = PROTECT_ONE_Q62;

    ASSERT(Divisor > 1);
    Base = PROTECT_ONE_Q62 - PROTECT_ONE_Q62 / (ULONG64)Divisor;
    while (Samples)
    {
        if (Samples & 1)
            Result = ProtectMultiply(Result, Base, 62);
        Base = ProtectMultiply(Base, Base, 62);
        Samples >>= 1;
    }
    return Result;
}

static
LONG
ProtectTenLog10MilliDb(
    _In_ ULONG64 Numerator,
    _In_ ULONG64 Denominator)
{
    ULONG64 Value;
    LONG64 Log2Q16 = 0;
    ULONG Bit;

    ASSERT(Numerator && Denominator && Numerator < (1ULL << 33));
    Value = (Numerator << 30) / Denominator;
    ASSERT(Value);
    while (Value >= (2ULL << 30))
    {
        Value >>= 1;
        Log2Q16 += 1 << 16;
    }
    while (Value < (1ULL << 30))
    {
        Value <<= 1;
        Log2Q16 -= 1 << 16;
    }
    for (Bit = 1 << 15; Bit; Bit >>= 1)
    {
        Value = (Value * Value) >> 30;
        if (Value >= (2ULL << 30))
        {
            Value >>= 1;
            Log2Q16 += Bit;
        }
    }
    return (LONG)((Log2Q16 * 30103) / 655360);
}

const APPLE_AUDIO_BOARD_PROFILE *
AppleAudioProtectionFindBoard(
    _In_reads_bytes_(Length) const VOID *Compatible,
    _In_ ULONG Length)
{
    ULONG Index;

    for (Index = 0; Index < RTL_NUMBER_OF(AppleAudioBoards); ++Index)
    {
        if (FdtStringListContains(Compatible, Length, AppleAudioBoards[Index].Compatible))
            return &AppleAudioBoards[Index];
    }
    return NULL;
}

NTSTATUS
AppleAudioProtectionAddSpeaker(
    _Inout_ PAPPLE_AUDIO_PROTECTION Protection,
    _In_reads_(NameLength) const CHAR *Name,
    _In_ ULONG NameLength,
    _In_ ULONG IsChannel,
    _In_ ULONG VsChannel,
    _In_ LONG AmpGainMilliDb,
    _In_ BOOLEAN ColdBoot,
    _Out_ PULONG Index)
{
    const APPLE_AUDIO_BOARD_PROFILE *Board = Protection->Board;
    const APPLE_AUDIO_SPEAKER_PROFILE *Profile = NULL;
    PAPPLE_AUDIO_SPEAKER_MODEL Model;
    ULONG Entry, TrSum;
    LONG64 Ambient;
    LONG MarginMilliDb;

    *Index = MAXULONG;
    if (!Board || Protection->SpeakerCount >= APPLE_AUDIO_PROTECT_MAX_SPEAKERS)
        return STATUS_INVALID_DEVICE_STATE;
    for (Entry = 0; Entry < Board->SpeakerCount; ++Entry)
    {
        if (strlen(Board->Speakers[Entry].Name) == NameLength &&
            !memcmp(Board->Speakers[Entry].Name, Name, NameLength))
        {
            Profile = &Board->Speakers[Entry];
            break;
        }
    }
    if (!Profile)
        return STATUS_NOT_FOUND;
    if (Profile->IsChannel != IsChannel || Profile->VsChannel != VsChannel || IsChannel >= Board->Channels ||
        VsChannel >= Board->Channels || Profile->Group >= APPLE_AUDIO_PROTECT_MAX_GROUPS ||
        Profile->TauMagnetMs <= Profile->TauCoilMs || Profile->TLimitMilliC <= Board->TAmbientMilliC + Board->TWindowMilliC)
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    for (Entry = 0; Entry < Protection->SpeakerCount; ++Entry)
    {
        if (Protection->Speaker[Entry].Profile == Profile)
            return STATUS_DEVICE_CONFIGURATION_ERROR;
    }

    Model = &Protection->Speaker[Protection->SpeakerCount];
    RtlZeroMemory(Model, sizeof(*Model));
    Model->Profile = Profile;
    Model->CoilDivisor = (LONG64)Profile->TauCoilMs * PROTECT_SAMPLES_PER_MS + 1;
    Model->MagnetDivisor = (LONG64)Profile->TauMagnetMs * PROTECT_SAMPLES_PER_MS + 1;
    Model->PowerScale = (ULONG64)Profile->VsScaleMilliV * Profile->IsScaleMilliA;
    Model->CoilRiseQ16 = (LONG64)((Model->PowerScale * Profile->TrCoilMilli * 4 * 65536) / 1000000000ULL);
    Model->MagnetRiseQ16 = (LONG64)((Model->PowerScale * Profile->TrMagnetMilli * 4 * 65536) / 1000000000ULL);

    Ambient = PROTECT_CELSIUS(Board->TAmbientMilliC);
    Model->TCoil = ColdBoot ? PROTECT_CELSIUS(Profile->TLimitMilliC - Board->TWindowMilliC - 1000) :
                   PROTECT_CELSIUS(Profile->TLimitMilliC);
    TrSum = Profile->TrMagnetMilli + Profile->TrCoilMilli;
    Model->TMagnet = Ambient + (LONG64)(((Model->TCoil - Ambient) / TrSum) * Profile->TrMagnetMilli);

    MarginMilliDb = ProtectTenLog10MilliDb((ULONG64)(Profile->TLimitMilliC - Board->TAmbientMilliC) * Profile->ZNominalMilliOhm,
                                           (ULONG64)TrSum * 2000);
    Model->MinGainMilliDb = min(MarginMilliDb - AmpGainMilliDb, 0);
    ASSERT(Model->MinGainMilliDb <= 0);

    Protection->GroupGainMilliDb[Profile->Group] = 0;
    *Index = Protection->SpeakerCount++;
    DbgPrint("appleaudio: speaker %s: group %lu, limit %lu.%lu C, amp gain %ld mdBV, worst-case ceiling %ld mdB, coil starts at %ld mC (%s)\n",
             Profile->Name, Profile->Group, Profile->TLimitMilliC / 1000, (Profile->TLimitMilliC % 1000) / 100,
             AmpGainMilliDb, Model->MinGainMilliDb, AppleAudioProtectionMilliCelsius(Model->TCoil),
             ColdBoot ? "cold boot" : "warm restart");
    return STATUS_SUCCESS;
}

VOID
AppleAudioProtectionSkip(
    _Inout_ PAPPLE_AUDIO_PROTECTION Protection,
    _In_ ULONG64 Samples)
{
    PAPPLE_AUDIO_SPEAKER_MODEL Model;
    LONG64 Ambient, Coil, Magnet;
    ULONG64 CoilDecay, MagnetDecay, Eta;
    ULONG Index;

    if (!Protection->Board || !Samples)
        return;
    Ambient = PROTECT_CELSIUS(Protection->Board->TAmbientMilliC);
    for (Index = 0; Index < Protection->SpeakerCount; ++Index)
    {
        Model = &Protection->Speaker[Index];
        ASSERT(Model->MagnetDivisor > Model->CoilDivisor);
        CoilDecay = ProtectDecay(Model->CoilDivisor, Samples);
        MagnetDecay = ProtectDecay(Model->MagnetDivisor, Samples);
        ASSERT(MagnetDecay >= CoilDecay);
        Eta = ((ULONG64)Model->MagnetDivisor << 32) / (ULONG64)(Model->MagnetDivisor - Model->CoilDivisor);
        Coil = Model->TCoil - Ambient;
        Magnet = Model->TMagnet - Ambient;
        Model->TCoil = Ambient + ProtectScale(Coil, CoilDecay, 62) +
                       ProtectScale(ProtectScale(Magnet, MagnetDecay - CoilDecay, 62), Eta, 32);
        Model->TMagnet = Ambient + ProtectScale(Magnet, MagnetDecay, 62);
    }
}

NTSTATUS
AppleAudioProtectionRun(
    _Inout_ PAPPLE_AUDIO_PROTECTION Protection,
    _In_reads_(FrameCount * Protection->Board->Channels) const SHORT *Frames,
    _In_ ULONG FrameCount)
{
    const APPLE_AUDIO_BOARD_PROFILE *Board = Protection->Board;
    const APPLE_AUDIO_SPEAKER_PROFILE *Profile;
    PAPPLE_AUDIO_SPEAKER_MODEL Model;
    LONG64 Ambient, Start, Window, Hysteresis, Ceiling, Product, PowerSum, Temperature, Target;
    LONG Gain, Group[APPLE_AUDIO_PROTECT_MAX_GROUPS];
    const SHORT *Frame;
    ULONG Index, Sample, Voltage, Current;
    NTSTATUS Status = STATUS_SUCCESS;

    if (!Board || !FrameCount)
        return STATUS_INVALID_PARAMETER;
    Ambient = PROTECT_CELSIUS(Board->TAmbientMilliC);
    Window = PROTECT_CELSIUS(Board->TWindowMilliC);
    Hysteresis = PROTECT_CELSIUS(Board->THysteresisMilliC);
    for (Index = 0; Index < APPLE_AUDIO_PROTECT_MAX_GROUPS; ++Index)
        Group[Index] = MAXLONG;

    for (Index = 0; Index < Protection->SpeakerCount; ++Index)
    {
        Model = &Protection->Speaker[Index];
        Profile = Model->Profile;
        PowerSum = 0;
        Model->PeakVoltage = 0;
        Model->PeakCurrent = 0;
        for (Sample = 0, Frame = Frames; Sample < FrameCount; ++Sample, Frame += Board->Channels)
        {
            Product = (LONG64)Frame[Profile->VsChannel] * Frame[Profile->IsChannel];
            Voltage = (ULONG)((Frame[Profile->VsChannel] < 0) ? -Frame[Profile->VsChannel] : Frame[Profile->VsChannel]);
            Current = (ULONG)((Frame[Profile->IsChannel] < 0) ? -Frame[Profile->IsChannel] : Frame[Profile->IsChannel]);
            Model->PeakVoltage = max(Model->PeakVoltage, Voltage);
            Model->PeakCurrent = max(Model->PeakCurrent, Current);
            Target = Model->TMagnet + (Product * Model->CoilRiseQ16) / 65536;
            Model->TCoil += (Target - Model->TCoil) / Model->CoilDivisor;
            Target = Ambient + (Product * Model->MagnetRiseQ16) / 65536;
            Model->TMagnet += (Target - Model->TMagnet) / Model->MagnetDivisor;
            PowerSum += Product;
        }

        Model->PowerMilliW = (LONG)(((PowerSum / (LONG64)FrameCount) * (LONG64)Model->PowerScale) /
                                    (LONG64)(PROTECT_FULL_SCALE_SQUARED * 1000));
        Ceiling = PROTECT_CELSIUS(Profile->TLimitMilliC + Profile->THeadroomMilliC);
        if (Model->TCoil > Ceiling || Model->TMagnet > Ceiling)
        {
            if (!Model->OverLimit)
                DbgPrint("appleaudio: speaker %s: temperature limit exceeded, coil %ld mC magnet %ld mC (limit %lu mC)\n",
                         Profile->Name, AppleAudioProtectionMilliCelsius(Model->TCoil),
                         AppleAudioProtectionMilliCelsius(Model->TMagnet), Profile->TLimitMilliC);
            Model->OverLimit = TRUE;
            Status = STATUS_DEVICE_HARDWARE_ERROR;
        }
        else
        {
            Model->OverLimit = FALSE;
        }
        if (Model->PowerMilliW < PROTECT_NEGATIVE_FLOOR_MW)
        {
            if (!Model->BadData)
                DbgPrint("appleaudio: speaker %s: negative power %ld mW, bad V/I sense data\n", Profile->Name,
                         Model->PowerMilliW);
            Model->BadData = TRUE;
            if (NT_SUCCESS(Status))
                Status = STATUS_DEVICE_DATA_ERROR;
        }
        else
        {
            Model->BadData = FALSE;
        }

        Model->TCoilHysteresis = min(max(Model->TCoilHysteresis, Model->TCoil), Model->TCoil + Hysteresis);
        Model->TMagnetHysteresis = min(max(Model->TMagnetHysteresis, Model->TMagnet), Model->TMagnet + Hysteresis);
        Temperature = max(Model->TCoilHysteresis, Model->TMagnetHysteresis);
        Start = PROTECT_CELSIUS(Profile->TLimitMilliC) - Window;
        Gain = 0;
        if (Temperature > Start)
            Gain = (LONG)(((LONG64)Model->MinGainMilliDb * (Temperature - Start)) / Window);
        if (Gain > PROTECT_NOMINAL_FLOOR_MDB)
            Gain = 0;
        Model->GainMilliDb = Gain;
        Group[Profile->Group] = min(Group[Profile->Group], Gain);
    }

    for (Index = 0; Index < APPLE_AUDIO_PROTECT_MAX_GROUPS; ++Index)
    {
        if (Group[Index] == MAXLONG)
            continue;
        if (Group[Index] / PROTECT_REPORT_STEP_MDB != Protection->GroupGainMilliDb[Index] / PROTECT_REPORT_STEP_MDB)
        {
            if (Group[Index])
                DbgPrint("appleaudio: speaker group %lu gain limited to %ld mdB\n", Index, Group[Index]);
            else
                DbgPrint("appleaudio: speaker group %lu gain nominal\n", Index);
        }
        Protection->GroupGainMilliDb[Index] = Group[Index];
    }
    return Status;
}

LONG
AppleAudioProtectionGain(
    _In_ PAPPLE_AUDIO_PROTECTION Protection,
    _In_ ULONG Index)
{
    ASSERT(Index < Protection->SpeakerCount);
    return Protection->GroupGainMilliDb[Protection->Speaker[Index].Profile->Group];
}

LONG
AppleAudioProtectionCeiling(
    _In_ PAPPLE_AUDIO_PROTECTION Protection,
    _In_ ULONG Index)
{
    LONG Ceiling = 0;
    ULONG Entry;

    ASSERT(Index < Protection->SpeakerCount);
    for (Entry = 0; Entry < Protection->SpeakerCount; ++Entry)
    {
        if (Protection->Speaker[Entry].Profile->Group == Protection->Speaker[Index].Profile->Group)
            Ceiling = min(Ceiling, Protection->Speaker[Entry].MinGainMilliDb);
    }
    return Ceiling;
}

LONG
AppleAudioProtectionMilliCelsius(
    _In_ LONG64 Temperature)
{
    return (LONG)((Temperature * 1000) / (1LL << 32));
}
