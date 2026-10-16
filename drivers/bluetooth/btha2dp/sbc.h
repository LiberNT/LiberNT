/*
 * PROJECT:     LiberNT Bluetooth A2DP audio driver
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Low Complexity Subband Coding (SBC) encoder definitions
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

#define SBC_SYNCWORD                0x9C
#define SBC_SUBBANDS                8
#define SBC_MAX_BLOCKS              16
#define SBC_MAX_CHANNELS            2
#define SBC_MAX_FRAME_LENGTH        512

#define SBC_FREQUENCY_16000         0
#define SBC_FREQUENCY_32000         1
#define SBC_FREQUENCY_44100         2
#define SBC_FREQUENCY_48000         3

#define SBC_MODE_MONO               0
#define SBC_MODE_DUAL_CHANNEL       1
#define SBC_MODE_STEREO             2
#define SBC_MODE_JOINT_STEREO       3

#define SBC_ALLOCATION_LOUDNESS     0
#define SBC_ALLOCATION_SNR          1

typedef struct _SBC_ENCODER
{
    UCHAR Frequency;
    UCHAR Blocks;
    UCHAR ChannelMode;
    UCHAR Allocation;
    UCHAR Bitpool;
    UCHAR Channels;
    LONG History[SBC_MAX_CHANNELS][10 * SBC_SUBBANDS];
} SBC_ENCODER, *PSBC_ENCODER;

BOOLEAN
SbcInitialize(
    _Out_ PSBC_ENCODER Encoder,
    _In_ UCHAR Frequency,
    _In_ UCHAR Blocks,
    _In_ UCHAR ChannelMode,
    _In_ UCHAR Allocation,
    _In_ UCHAR Bitpool);

ULONG
SbcFrameLength(
    _In_ const SBC_ENCODER *Encoder);

ULONG
SbcSamplesPerFrame(
    _In_ const SBC_ENCODER *Encoder);

ULONG
SbcEncodeFrame(
    _Inout_ PSBC_ENCODER Encoder,
    _In_reads_(SbcSamplesPerFrame(Encoder) * Encoder->Channels) const SHORT *Pcm,
    _Out_writes_bytes_(SBC_MAX_FRAME_LENGTH) PUCHAR Frame);
