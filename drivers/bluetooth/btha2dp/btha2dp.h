/*
 * PROJECT:     LiberNT Bluetooth A2DP audio driver
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Interface between the A2DP source engine and the audio miniports
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

#define BTHA2DP_TAG                 'dA2B'
#define BTHA2DP_SAMPLE_RATE         48000
#define BTHA2DP_CHANNELS            2
#define BTHA2DP_BITS_PER_SAMPLE     16
#define BTHA2DP_BLOCK_ALIGN         4
#define BTHA2DP_MAX_BUFFER_SIZE     (256 * 1024)
#define BTHA2DP_VOLUME_MINIMUM      (-96 * 0x10000)
#define BTHA2DP_VOLUME_MAXIMUM      0
#define BTHA2DP_VOLUME_STEP         0x8000

#ifdef __cplusplus
extern "C" {
#endif

typedef struct _A2DP_CONTEXT A2DP_CONTEXT, *PA2DP_CONTEXT;

NTSTATUS
A2dpCreate(
    _In_ PDEVICE_OBJECT PhysicalDevice,
    _Out_ PA2DP_CONTEXT *Context);

VOID
A2dpShutdown(
    _In_ PA2DP_CONTEXT Context);

VOID
A2dpDestroy(
    _In_ PA2DP_CONTEXT Context);

NTSTATUS
A2dpAttachBuffer(
    _In_ PA2DP_CONTEXT Context,
    _In_ PUCHAR Buffer,
    _In_ ULONG Size,
    _In_ ULONG NotificationCount);

VOID
A2dpDetachBuffer(
    _In_ PA2DP_CONTEXT Context);

VOID
A2dpSetRunning(
    _In_ PA2DP_CONTEXT Context,
    _In_ BOOLEAN Running,
    _In_ BOOLEAN Reset);

VOID
A2dpGetPosition(
    _In_ PA2DP_CONTEXT Context,
    _Out_ PULONGLONG PlayOffset,
    _Out_ PULONGLONG WriteOffset);

NTSTATUS
A2dpSetNotificationEvent(
    _In_ PA2DP_CONTEXT Context,
    _In_opt_ PKEVENT Event,
    _In_ BOOLEAN Register);

VOID
A2dpSetVolume(
    _In_ PA2DP_CONTEXT Context,
    _In_ ULONG Channel,
    _In_ LONG Level);

LONG
A2dpGetVolume(
    _In_ PA2DP_CONTEXT Context,
    _In_ ULONG Channel);

VOID
A2dpSetMute(
    _In_ PA2DP_CONTEXT Context,
    _In_ BOOLEAN Mute);

BOOLEAN
A2dpGetMute(
    _In_ PA2DP_CONTEXT Context);

BOOLEAN
A2dpIsConnected(
    _In_ PA2DP_CONTEXT Context);

#ifdef __cplusplus
}
#endif
