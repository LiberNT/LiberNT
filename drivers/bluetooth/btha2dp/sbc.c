/*
 * PROJECT:     LiberNT Bluetooth A2DP audio driver
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Low Complexity Subband Coding (SBC) encoder, A2DP specification appendix B
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntdef.h>
#include "sbc.h"

#define SBC_PROTO_SHIFT     31
#define SBC_MATRIX_SHIFT    24
#define SBC_SAMPLE_SHIFT    15

typedef struct _SBC_BIT_WRITER
{
    PUCHAR Data;
    ULONG Bit;
} SBC_BIT_WRITER, *PSBC_BIT_WRITER;

static const LONG SbcProto8[10 * SBC_SUBBANDS] =
{
    0, 336243, 737138, 1191038, 1769354, 2447970, 3170548, 3830504,
    4320362, 4517704, 4283254, 3471542, 1937362, -383982, -3542770, -7510125,
    12153672, 17243030, 22459338, 27374475, 31466061, 34154783, 34834004, 32896036,
    27782384, 19021498, 6279423, -10556558, -31440036, -56070530, -83913220, -114218864,
    146026618, 178208410, 209541558, 238793071, 264708601, 286183152, 302265850, 312222319,
    315583606, 312222319, 302265850, 286183152, 264708601, 238793071, 209541558, 178208410,
    -146026618, -114218864, -83913220, -56070530, -31440036, -10556558, 6279423, 19021498,
    27782384, 32896036, 34834004, 34154783, 31466061, 27374475, 22459338, 17243030,
    -12153672, -7510125, -3542770, -383982, 1937362, 3471542, 4283254, 4517704,
    4320362, 3830504, 3170548, 2447970, 1769354, 1191038, 737138, 336243
};

static const LONG SbcMatrix8[SBC_SUBBANDS][2 * SBC_SUBBANDS] =
{
    {11863283, 13949745, 15500126, 16454846, 16777216, 16454846, 15500126, 13949745,
     11863283, 9320922, 6420363, 3273072, 0, -3273072, -6420363, -9320922},
    {-11863283, -3273072, 6420363, 13949745, 16777216, 13949745, 6420363, -3273072,
     -11863283, -16454846, -15500126, -9320922, 0, 9320922, 15500126, 16454846},
    {-11863283, -16454846, -6420363, 9320922, 16777216, 9320922, -6420363, -16454846,
     -11863283, 3273072, 15500126, 13949745, 0, -13949745, -15500126, -3273072},
    {11863283, -9320922, -15500126, 3273072, 16777216, 3273072, -15500126, -9320922,
     11863283, 13949745, -6420363, -16454846, 0, 16454846, 6420363, -13949745},
    {11863283, 9320922, -15500126, -3273072, 16777216, -3273072, -15500126, 9320922,
     11863283, -13949745, -6420363, 16454846, 0, -16454846, 6420363, 13949745},
    {-11863283, 16454846, -6420363, -9320922, 16777216, -9320922, -6420363, 16454846,
     -11863283, -3273072, 15500126, -13949745, 0, 13949745, -15500126, 3273072},
    {-11863283, 3273072, 6420363, -13949745, 16777216, -13949745, 6420363, 3273072,
     -11863283, 16454846, -15500126, 9320922, 0, -9320922, 15500126, -16454846},
    {11863283, -13949745, 15500126, -16454846, 16777216, -16454846, 15500126, -13949745,
     11863283, -9320922, 6420363, -3273072, 0, 3273072, -6420363, 9320922}
};

static const CHAR SbcOffset8[4][SBC_SUBBANDS] =
{
    {-2, 0, 0, 0, 0, 0, 0, 1},
    {-3, 0, 0, 0, 0, 0, 1, 2},
    {-4, 0, 0, 0, 0, 0, 1, 2},
    {-4, 0, 0, 0, 0, 0, 1, 2}
};

static
VOID
SbcPutBits(
    _Inout_ PSBC_BIT_WRITER Writer,
    _In_ ULONG Value,
    _In_ ULONG Count)
{
    while (Count--)
    {
        ULONG Byte = Writer->Bit >> 3;

        if (!(Writer->Bit & 7))
            Writer->Data[Byte] = 0;
        if ((Value >> Count) & 1)
            Writer->Data[Byte] |= (UCHAR)(0x80 >> (Writer->Bit & 7));
        Writer->Bit++;
    }
}

static
UCHAR
SbcCrc(
    _In_reads_bytes_((Bits + 7) / 8) const UCHAR *Data,
    _In_ ULONG Bits)
{
    UCHAR Crc = 0x0F;
    ULONG Index;

    for (Index = 0; Index < Bits; Index++)
    {
        UCHAR Bit = (Data[Index >> 3] >> (7 - (Index & 7))) & 1;
        UCHAR Feedback = (UCHAR)(((Crc >> 7) & 1) ^ Bit);

        Crc = (UCHAR)(Crc << 1);
        if (Feedback)
            Crc ^= 0x1D;
    }
    return Crc;
}

static
VOID
SbcAnalyze(
    _Inout_ PLONG History,
    _In_reads_(SBC_SUBBANDS) const LONG *Input,
    _Out_writes_(SBC_SUBBANDS) PLONGLONG Output)
{
    LONGLONG Partial[2 * SBC_SUBBANDS];
    ULONG Index, Tap;

    for (Index = 10 * SBC_SUBBANDS - 1; Index >= SBC_SUBBANDS; Index--)
        History[Index] = History[Index - SBC_SUBBANDS];
    for (Index = 0; Index < SBC_SUBBANDS; Index++)
        History[SBC_SUBBANDS - 1 - Index] = Input[Index];
    for (Index = 0; Index < 2 * SBC_SUBBANDS; Index++)
    {
        LONGLONG Sum = 0;

        for (Tap = 0; Tap < 5; Tap++)
            Sum += (LONGLONG)SbcProto8[Index + Tap * 2 * SBC_SUBBANDS] * History[Index + Tap * 2 * SBC_SUBBANDS];
        Partial[Index] = Sum >> (SBC_PROTO_SHIFT - SBC_SAMPLE_SHIFT);
    }
    for (Index = 0; Index < SBC_SUBBANDS; Index++)
    {
        LONGLONG Sum = 0;

        for (Tap = 0; Tap < 2 * SBC_SUBBANDS; Tap++)
            Sum += (LONGLONG)SbcMatrix8[Index][Tap] * Partial[Tap];
        Output[Index] = Sum >> SBC_MATRIX_SHIFT;
    }
}

static
UCHAR
SbcScaleFactor(
    _In_ ULONGLONG Maximum)
{
    UCHAR ScaleFactor = 0;

    while (ScaleFactor < 15 && Maximum >= (1ULL << (ScaleFactor + 1 + SBC_SAMPLE_SHIFT)))
        ScaleFactor++;
    return ScaleFactor;
}

static
VOID
SbcAllocate(
    _In_ const SBC_ENCODER *Encoder,
    _In_ const UCHAR ScaleFactors[SBC_MAX_CHANNELS][SBC_SUBBANDS],
    _Out_ UCHAR Bits[SBC_MAX_CHANNELS][SBC_SUBBANDS])
{
    LONG BitNeed[SBC_MAX_CHANNELS][SBC_SUBBANDS];
    ULONG Channel, Subband, First, Last, Pass;
    BOOLEAN Combined = Encoder->ChannelMode == SBC_MODE_STEREO || Encoder->ChannelMode == SBC_MODE_JOINT_STEREO;

    for (Channel = 0; Channel < Encoder->Channels; Channel++)
    {
        for (Subband = 0; Subband < SBC_SUBBANDS; Subband++)
        {
            if (Encoder->Allocation == SBC_ALLOCATION_SNR)
            {
                BitNeed[Channel][Subband] = ScaleFactors[Channel][Subband];
            }
            else if (!ScaleFactors[Channel][Subband])
            {
                BitNeed[Channel][Subband] = -5;
            }
            else
            {
                LONG Loudness = ScaleFactors[Channel][Subband] - SbcOffset8[Encoder->Frequency][Subband];

                BitNeed[Channel][Subband] = Loudness > 0 ? Loudness / 2 : Loudness;
            }
        }
    }

    for (Pass = 0; Pass < (Combined ? 1u : Encoder->Channels); Pass++)
    {
        LONG MaxBitNeed = 0, BitCount = 0, SliceCount = 0, BitSlice;

        First = Combined ? 0 : Pass;
        Last = Combined ? Encoder->Channels : Pass + 1;
        for (Channel = First; Channel < Last; Channel++)
        {
            for (Subband = 0; Subband < SBC_SUBBANDS; Subband++)
            {
                if (BitNeed[Channel][Subband] > MaxBitNeed)
                    MaxBitNeed = BitNeed[Channel][Subband];
            }
        }
        BitSlice = MaxBitNeed + 1;
        do
        {
            BitSlice--;
            BitCount += SliceCount;
            SliceCount = 0;
            for (Channel = First; Channel < Last; Channel++)
            {
                for (Subband = 0; Subband < SBC_SUBBANDS; Subband++)
                {
                    if (BitNeed[Channel][Subband] > BitSlice + 1 && BitNeed[Channel][Subband] < BitSlice + 16)
                        SliceCount++;
                    else if (BitNeed[Channel][Subband] == BitSlice + 1)
                        SliceCount += 2;
                }
            }
        } while (BitCount + SliceCount < Encoder->Bitpool);
        if (BitCount + SliceCount == Encoder->Bitpool)
        {
            BitCount += SliceCount;
            BitSlice--;
        }

        for (Channel = First; Channel < Last; Channel++)
        {
            for (Subband = 0; Subband < SBC_SUBBANDS; Subband++)
            {
                if (BitNeed[Channel][Subband] < BitSlice + 2)
                    Bits[Channel][Subband] = 0;
                else
                    Bits[Channel][Subband] = (UCHAR)min(BitNeed[Channel][Subband] - BitSlice, 16);
            }
        }

        Channel = First;
        Subband = 0;
        while (BitCount < Encoder->Bitpool && Subband < SBC_SUBBANDS)
        {
            if (Bits[Channel][Subband] >= 2 && Bits[Channel][Subband] < 16)
            {
                Bits[Channel][Subband]++;
                BitCount++;
            }
            else if (BitNeed[Channel][Subband] == BitSlice + 1 && Encoder->Bitpool > BitCount + 1)
            {
                Bits[Channel][Subband] = 2;
                BitCount += 2;
            }
            if (Combined && Channel == 0)
            {
                Channel = 1;
            }
            else
            {
                Channel = First;
                Subband++;
            }
        }
        Channel = First;
        Subband = 0;
        while (BitCount < Encoder->Bitpool && Subband < SBC_SUBBANDS)
        {
            if (Bits[Channel][Subband] < 16)
            {
                Bits[Channel][Subband]++;
                BitCount++;
            }
            if (Combined && Channel == 0)
            {
                Channel = 1;
            }
            else
            {
                Channel = First;
                Subband++;
            }
        }
    }
}

BOOLEAN
SbcInitialize(
    _Out_ PSBC_ENCODER Encoder,
    _In_ UCHAR Frequency,
    _In_ UCHAR Blocks,
    _In_ UCHAR ChannelMode,
    _In_ UCHAR Allocation,
    _In_ UCHAR Bitpool)
{
    ULONG Index;
    UCHAR Channels = ChannelMode == SBC_MODE_MONO ? 1 : 2;

    if (Frequency > SBC_FREQUENCY_48000 || (Blocks != 4 && Blocks != 8 && Blocks != 12 && Blocks != 16) ||
        ChannelMode > SBC_MODE_JOINT_STEREO || Allocation > SBC_ALLOCATION_SNR || Bitpool < 2 ||
        Bitpool > ((ChannelMode == SBC_MODE_MONO || ChannelMode == SBC_MODE_DUAL_CHANNEL) ? 16 : 32) * SBC_SUBBANDS)
    {
        return FALSE;
    }
    Encoder->Frequency = Frequency;
    Encoder->Blocks = Blocks;
    Encoder->ChannelMode = ChannelMode;
    Encoder->Allocation = Allocation;
    Encoder->Bitpool = Bitpool;
    Encoder->Channels = Channels;
    for (Index = 0; Index < 10 * SBC_SUBBANDS; Index++)
    {
        Encoder->History[0][Index] = 0;
        Encoder->History[1][Index] = 0;
    }
    return TRUE;
}

ULONG
SbcFrameLength(
    _In_ const SBC_ENCODER *Encoder)
{
    ULONG Bits = 4 * SBC_SUBBANDS * Encoder->Channels;

    if (Encoder->ChannelMode == SBC_MODE_MONO || Encoder->ChannelMode == SBC_MODE_DUAL_CHANNEL)
        Bits += Encoder->Blocks * Encoder->Channels * Encoder->Bitpool;
    else
        Bits += (Encoder->ChannelMode == SBC_MODE_JOINT_STEREO ? SBC_SUBBANDS : 0) + Encoder->Blocks * Encoder->Bitpool;
    return 4 + (Bits + 7) / 8;
}

ULONG
SbcSamplesPerFrame(
    _In_ const SBC_ENCODER *Encoder)
{
    return Encoder->Blocks * SBC_SUBBANDS;
}

ULONG
SbcEncodeFrame(
    _Inout_ PSBC_ENCODER Encoder,
    _In_reads_(SbcSamplesPerFrame(Encoder) * Encoder->Channels) const SHORT *Pcm,
    _Out_writes_bytes_(SBC_MAX_FRAME_LENGTH) PUCHAR Frame)
{
    LONGLONG Samples[SBC_MAX_BLOCKS][SBC_MAX_CHANNELS][SBC_SUBBANDS];
    UCHAR ScaleFactors[SBC_MAX_CHANNELS][SBC_SUBBANDS], Bits[SBC_MAX_CHANNELS][SBC_SUBBANDS];
    UCHAR Join = 0;
    ULONG Block, Channel, Subband, Length = SbcFrameLength(Encoder), CrcBits;
    SBC_BIT_WRITER Writer;

    for (Block = 0; Block < Encoder->Blocks; Block++)
    {
        for (Channel = 0; Channel < Encoder->Channels; Channel++)
        {
            LONG Input[SBC_SUBBANDS];

            for (Subband = 0; Subband < SBC_SUBBANDS; Subband++)
                Input[Subband] = Pcm[(Block * SBC_SUBBANDS + Subband) * Encoder->Channels + Channel];
            SbcAnalyze(Encoder->History[Channel], Input, Samples[Block][Channel]);
        }
    }

    for (Channel = 0; Channel < Encoder->Channels; Channel++)
    {
        for (Subband = 0; Subband < SBC_SUBBANDS; Subband++)
        {
            ULONGLONG Maximum = 0;

            for (Block = 0; Block < Encoder->Blocks; Block++)
            {
                LONGLONG Value = Samples[Block][Channel][Subband];
                ULONGLONG Magnitude = (ULONGLONG)(Value < 0 ? -Value : Value);

                if (Magnitude > Maximum)
                    Maximum = Magnitude;
            }
            ScaleFactors[Channel][Subband] = SbcScaleFactor(Maximum);
        }
    }

    if (Encoder->ChannelMode == SBC_MODE_JOINT_STEREO)
    {
        for (Subband = 0; Subband < SBC_SUBBANDS - 1; Subband++)
        {
            ULONGLONG MaxSum = 0, MaxDifference = 0;
            UCHAR SumFactor, DifferenceFactor;

            for (Block = 0; Block < Encoder->Blocks; Block++)
            {
                LONGLONG Sum = (Samples[Block][0][Subband] + Samples[Block][1][Subband]) >> 1;
                LONGLONG Difference = (Samples[Block][0][Subband] - Samples[Block][1][Subband]) >> 1;
                ULONGLONG SumMagnitude = (ULONGLONG)(Sum < 0 ? -Sum : Sum);
                ULONGLONG DifferenceMagnitude = (ULONGLONG)(Difference < 0 ? -Difference : Difference);

                if (SumMagnitude > MaxSum)
                    MaxSum = SumMagnitude;
                if (DifferenceMagnitude > MaxDifference)
                    MaxDifference = DifferenceMagnitude;
            }
            SumFactor = SbcScaleFactor(MaxSum);
            DifferenceFactor = SbcScaleFactor(MaxDifference);
            if ((ULONG)ScaleFactors[0][Subband] + ScaleFactors[1][Subband] > (ULONG)SumFactor + DifferenceFactor)
            {
                Join |= (UCHAR)(0x80 >> Subband);
                ScaleFactors[0][Subband] = SumFactor;
                ScaleFactors[1][Subband] = DifferenceFactor;
                for (Block = 0; Block < Encoder->Blocks; Block++)
                {
                    LONGLONG Left = Samples[Block][0][Subband], Right = Samples[Block][1][Subband];

                    Samples[Block][0][Subband] = (Left + Right) >> 1;
                    Samples[Block][1][Subband] = (Left - Right) >> 1;
                }
            }
        }
    }

    SbcAllocate(Encoder, ScaleFactors, Bits);

    Writer.Data = Frame;
    Writer.Bit = 0;
    SbcPutBits(&Writer, SBC_SYNCWORD, 8);
    SbcPutBits(&Writer, Encoder->Frequency, 2);
    SbcPutBits(&Writer, Encoder->Blocks / 4 - 1, 2);
    SbcPutBits(&Writer, Encoder->ChannelMode, 2);
    SbcPutBits(&Writer, Encoder->Allocation, 1);
    SbcPutBits(&Writer, 1, 1);
    SbcPutBits(&Writer, Encoder->Bitpool, 8);
    SbcPutBits(&Writer, 0, 8);
    if (Encoder->ChannelMode == SBC_MODE_JOINT_STEREO)
        SbcPutBits(&Writer, Join, SBC_SUBBANDS);
    for (Channel = 0; Channel < Encoder->Channels; Channel++)
    {
        for (Subband = 0; Subband < SBC_SUBBANDS; Subband++)
            SbcPutBits(&Writer, ScaleFactors[Channel][Subband], 4);
    }
    CrcBits = Writer.Bit - 32;
    {
        UCHAR CrcData[2 + (SBC_SUBBANDS + 4 * SBC_SUBBANDS * SBC_MAX_CHANNELS + 7) / 8];
        ULONG Index;

        CrcData[0] = Frame[1];
        CrcData[1] = Frame[2];
        for (Index = 0; Index < (CrcBits + 7) / 8; Index++)
            CrcData[2 + Index] = Frame[4 + Index];
        Frame[3] = SbcCrc(CrcData, 16 + CrcBits);
    }

    for (Block = 0; Block < Encoder->Blocks; Block++)
    {
        for (Channel = 0; Channel < Encoder->Channels; Channel++)
        {
            for (Subband = 0; Subband < SBC_SUBBANDS; Subband++)
            {
                UCHAR Count = Bits[Channel][Subband];
                ULONG Shift = ScaleFactors[Channel][Subband] + 1 + SBC_SAMPLE_SHIFT;
                LONGLONG Scale, Value;
                ULONGLONG Levels, Quantized;

                if (!Count)
                    continue;
                Scale = 1LL << Shift;
                Value = Samples[Block][Channel][Subband];
                if (Value >= Scale)
                    Value = Scale - 1;
                if (Value < -Scale)
                    Value = -Scale;
                Levels = (1ULL << Count) - 1;
                Quantized = ((ULONGLONG)(Value + Scale) * Levels) >> (Shift + 1);
                SbcPutBits(&Writer, (ULONG)Quantized, Count);
            }
        }
    }
    while (Writer.Bit < Length * 8)
        SbcPutBits(&Writer, 0, 1);
    return Length;
}
