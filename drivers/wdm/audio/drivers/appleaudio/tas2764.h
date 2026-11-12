/*
 * PROJECT:     LiberNT Apple Silicon Audio Driver
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     TI TAS2764 / SN012776 speaker amplifier control
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

#include <reactos/drivers/applei2c.h>

#define TAS2764_DVC_MUTE            0xc9
#define TAS2764_DVC_MINIMUM         0xc8
#define TAS2764_DVC_STEP_MILLI_DB   500
#define TAS2764_VOLUME_UNKNOWN      0xff
#define TAS2764_NAME_LENGTH         32

typedef enum _TAS2764_MODE
{
    Tas2764Active,
    Tas2764Muted,
    Tas2764Shutdown
} TAS2764_MODE;

typedef struct _APPLE_AUDIO_AMP
{
    PAPPLE_I2C I2c;
    CHAR Name[TAS2764_NAME_LENGTH];
    ULONG NameLength;
    ULONG Node;
    ULONG Side;
    ULONG Model;
    ULONG IdleMask;
    UCHAR Address;
    UCHAR Page;
    UCHAR ImonSlot;
    UCHAR VmonSlot;
    UCHAR Volume;
    BOOLEAN Sn012776;
    BOOLEAN Tweeter;
    BOOLEAN IdleZero;
} APPLE_AUDIO_AMP, *PAPPLE_AUDIO_AMP;

LONG
Tas2764AmpGainMilliDb(
    _In_ ULONG Level);

NTSTATUS
Tas2764Reset(
    _Inout_ PAPPLE_AUDIO_AMP Amp);

NTSTATUS
Tas2764Initialize(
    _Inout_ PAPPLE_AUDIO_AMP Amp,
    _In_ ULONG AmpLevel);

NTSTATUS
Tas2764SetFormat(
    _Inout_ PAPPLE_AUDIO_AMP Amp,
    _In_ ULONG SampleBits);

NTSTATUS
Tas2764SetMode(
    _Inout_ PAPPLE_AUDIO_AMP Amp,
    _In_ TAS2764_MODE Mode);

NTSTATUS
Tas2764SetVolume(
    _Inout_ PAPPLE_AUDIO_AMP Amp,
    _In_ UCHAR Volume);

UCHAR
Tas2764PollFaults(
    _Inout_ PAPPLE_AUDIO_AMP Amp);

BOOLEAN
Tas2764ReadTemperature(
    _Inout_ PAPPLE_AUDIO_AMP Amp,
    _Out_ PLONG Celsius);
