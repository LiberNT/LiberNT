/*
 * PROJECT:     LiberNT Apple Silicon Audio Driver
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Headphone jack hardware: MCA, ADMAC, NCO and the CS42L84 codec
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#define APPLE_AUDIO_SAMPLE_RATE         48000
#define APPLE_AUDIO_CHANNELS            2
#define APPLE_AUDIO_VOLUME_MINIMUM      (-192)
#define APPLE_AUDIO_VOLUME_MAXIMUM      0
#define APPLE_AUDIO_VOLUME_DEFAULT      (-40)
#define APPLE_AUDIO_RENDER              0
#define APPLE_AUDIO_CAPTURE             1
#define APPLE_AUDIO_DIRECTIONS          2
#define APPLE_AUDIO_CAPTURE_CHANNELS    1

typedef struct _APPLE_AUDIO_HW *PAPPLE_AUDIO_HW;

NTSTATUS
NTAPI
AppleAudioHwQueryInterrupt(
    _In_ PDEVICE_OBJECT PhysicalDeviceObject,
    _Out_ PULONG Gsi,
    _Out_ PBOOLEAN EdgeTriggered);

NTSTATUS
NTAPI
AppleAudioHwOpen(
    _In_ PDEVICE_OBJECT PhysicalDeviceObject,
    _Out_ PAPPLE_AUDIO_HW *Hardware);

VOID
NTAPI
AppleAudioHwClose(
    _In_ PAPPLE_AUDIO_HW Hardware);

BOOLEAN
NTAPI
AppleAudioHwHasCapture(
    _In_ PAPPLE_AUDIO_HW Hardware);

NTSTATUS
NTAPI
AppleAudioHwPrepare(
    _In_ PAPPLE_AUDIO_HW Hardware,
    _In_ ULONG Direction,
    _In_ ULONG SampleBits);

VOID
NTAPI
AppleAudioHwUnprepare(
    _In_ PAPPLE_AUDIO_HW Hardware,
    _In_ ULONG Direction);

NTSTATUS
NTAPI
AppleAudioHwArm(
    _In_ PAPPLE_AUDIO_HW Hardware,
    _In_ ULONG Direction);

NTSTATUS
NTAPI
AppleAudioHwStartDma(
    _In_ PAPPLE_AUDIO_HW Hardware,
    _In_ ULONG Direction,
    _In_ ULONG64 Buffer,
    _In_ ULONG BufferLength,
    _In_ ULONG PeriodLength);

VOID
NTAPI
AppleAudioHwStopDma(
    _In_ PAPPLE_AUDIO_HW Hardware,
    _In_ ULONG Direction);

BOOLEAN
NTAPI
AppleAudioHwInterrupt(
    _In_ PAPPLE_AUDIO_HW Hardware,
    _Out_writes_(APPLE_AUDIO_DIRECTIONS) PULONG Periods);

ULONG
NTAPI
AppleAudioHwPosition(
    _In_ PAPPLE_AUDIO_HW Hardware,
    _In_ ULONG Direction);

NTSTATUS
NTAPI
AppleAudioHwSetVolume(
    _In_ PAPPLE_AUDIO_HW Hardware,
    _In_ LONG LeftHalfDecibels,
    _In_ LONG RightHalfDecibels,
    _In_ BOOLEAN Mute);

BOOLEAN
NTAPI
AppleAudioHwPollJack(
    _In_ PAPPLE_AUDIO_HW Hardware,
    _Out_ PBOOLEAN Connected,
    _Out_ PBOOLEAN Microphone);

#ifdef __cplusplus
}
#endif
