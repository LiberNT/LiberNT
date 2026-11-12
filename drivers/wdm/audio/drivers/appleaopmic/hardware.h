/*
 * PROJECT:     LiberNT Apple AOP Microphone Driver
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Device tree, DART and ADMAC access of the built-in microphones
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

typedef struct _AOP_MIC_HW AOP_MIC_HW, *PAOP_MIC_HW;

NTSTATUS
AopMicHwOpen(
    _Out_ PAOP_MIC_HW *Hardware);

VOID
AopMicHwClose(
    _In_opt_ PAOP_MIC_HW Hardware);

NTSTATUS
AopMicHwOpenChannel(
    _Inout_ PAOP_MIC_HW Hardware,
    _In_ ULONG WordBytes,
    _In_ ULONG FrameWords);

VOID
AopMicHwCloseChannel(
    _Inout_ PAOP_MIC_HW Hardware);

NTSTATUS
AopMicHwStart(
    _Inout_ PAOP_MIC_HW Hardware,
    _In_ ULONG64 Buffer,
    _In_ ULONG BufferLength,
    _In_ ULONG PeriodLength);

VOID
AopMicHwStop(
    _Inout_ PAOP_MIC_HW Hardware);

ULONG
AopMicHwService(
    _Inout_ PAOP_MIC_HW Hardware);

ULONG
AopMicHwPosition(
    _In_ PAOP_MIC_HW Hardware);

BOOLEAN
AopMicHwInterrupt(
    _Inout_ PAOP_MIC_HW Hardware,
    _Out_ PULONG Reports);

NTSTATUS
AopMicHwFilterRequirements(
    _Inout_ PIRP Irp);

#ifdef __cplusplus
}
#endif
