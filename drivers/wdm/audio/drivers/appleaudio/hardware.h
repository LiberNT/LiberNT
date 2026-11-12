/*
 * PROJECT:     LiberNT Apple Silicon Audio Driver
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Headphone jack and speaker hardware: MCA, ADMAC, NCO, CS42L84 and TAS2764
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
#define APPLE_AUDIO_SPEAKERS            2
#define APPLE_AUDIO_STREAMS             3
#define APPLE_AUDIO_SENSE               3
#define APPLE_AUDIO_DMA_CHANNELS        4
#define APPLE_AUDIO_ENDPOINT_HEADPHONES 0
#define APPLE_AUDIO_ENDPOINT_SPEAKERS   1
#define APPLE_AUDIO_ENDPOINTS           2
#define APPLE_AUDIO_SPEAKER_VOLUME_MINIMUM (-200)
#define APPLE_AUDIO_STREAM_CHANNELS(Stream) \
    ((Stream) == APPLE_AUDIO_CAPTURE ? APPLE_AUDIO_CAPTURE_CHANNELS : APPLE_AUDIO_CHANNELS)

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
    _Out_writes_(APPLE_AUDIO_DMA_CHANNELS) PULONG Periods);

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
    _Out_ PBOOLEAN Microphone,
    _Out_opt_ PBOOLEAN TipLatched);

NTSTATUS
NTAPI
AppleAudioHwAddJackRequirement(
    _In_ PDEVICE_OBJECT PhysicalDeviceObject,
    _Inout_ PIRP Irp);

BOOLEAN
NTAPI
AppleAudioHwIsDmaInterrupt(
    _In_ PAPPLE_AUDIO_HW Hardware,
    _In_ ULONG Gsi);

NTSTATUS
NTAPI
AppleAudioHwOpenJackInterrupt(
    _In_ PAPPLE_AUDIO_HW Hardware,
    _In_ ULONG Gsi);

VOID
NTAPI
AppleAudioHwCloseJackInterrupt(
    _In_ PAPPLE_AUDIO_HW Hardware);

BOOLEAN
NTAPI
AppleAudioHwJackInterrupt(
    _In_ PAPPLE_AUDIO_HW Hardware);

VOID
NTAPI
AppleAudioHwArmJackInterrupt(
    _In_ PAPPLE_AUDIO_HW Hardware,
    _In_ BOOLEAN Test);

BOOLEAN
NTAPI
AppleAudioHwHasSpeakers(
    _In_ PAPPLE_AUDIO_HW Hardware);

NTSTATUS
NTAPI
AppleAudioHwSetSpeakerVolume(
    _In_ PAPPLE_AUDIO_HW Hardware,
    _In_ LONG LeftHalfDecibels,
    _In_ LONG RightHalfDecibels,
    _In_ BOOLEAN Mute);

VOID
NTAPI
AppleAudioHwProtectSpeakers(
    _In_ PAPPLE_AUDIO_HW Hardware);

#ifdef __cplusplus
}
#endif
