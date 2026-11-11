/*
 * PROJECT:     LiberNT Apple AOP Microphone Driver
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Audio service calls of the Apple Always-On Processor
 * COPYRIGHT:   Copyright (C) The Asahi Linux Contributors
 *              Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

#include <reactos/drivers/appleaop.h>

#ifdef __cplusplus
extern "C" {
#endif

#define AOP_AUDIO_FOURCC(a, b, c, d) (((ULONG)(UCHAR)(a) << 24) | ((ULONG)(UCHAR)(b) << 16) | \
                                      ((ULONG)(UCHAR)(c) << 8) | (ULONG)(UCHAR)(d))

#define AOP_AUDIO_POWER_OFF         AOP_AUDIO_FOURCC('i', 'd', 'l', 'e')
#define AOP_AUDIO_POWER_IDLE        AOP_AUDIO_FOURCC('p', 'w', '1', ' ')
#define AOP_AUDIO_POWER_ON          AOP_AUDIO_FOURCC('p', 'w', 'r', 'd')

NTSTATUS
AopAudioConfigure(
    _In_ PAPPLE_AOP_FUNCTION_INTERFACE Aop);

NTSTATUS
AopAudioSetPower(
    _In_ PAPPLE_AOP_FUNCTION_INTERFACE Aop,
    _In_ ULONG State,
    _In_ ULONG Unknown1,
    _In_ ULONG Cookie);

#ifdef __cplusplus
}
#endif
