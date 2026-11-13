/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF
 * PURPOSE:     LoongArch64 extended processor state services
 */

#include <ntoskrnl.h>

#define INVALID_EXTENDED_PROCESSOR_STATE 0x131

/* The loader enables EUEN.FPE only (handoff decision D-8), so the legacy FP
 * and SSE bits name the floating-point bank of CONTEXT and nothing else can
 * exist for a save record to carry. */
NTSTATUS
NTAPI
KeSaveExtendedProcessorState(
    _In_ ULONG64 Mask,
    _Out_ PXSTATE_SAVE XStateSave)
{
    ULONG64 OptionalMask = Mask & ~XSTATE_MASK_LEGACY;

    UNREFERENCED_PARAMETER(XStateSave);

    if (OptionalMask != 0)
    {
        KeBugCheckEx(INVALID_EXTENDED_PROCESSOR_STATE,
                     0,
                     0,
                     (ULONG)OptionalMask,
                     (ULONG)(OptionalMask >> 32));
    }

    return STATUS_SUCCESS;
}

VOID
NTAPI
KeRestoreExtendedProcessorState(
    _In_ PXSTATE_SAVE XStateSave)
{
    UNREFERENCED_PARAMETER(XStateSave);
}

ULONG64
NTAPI
RtlGetEnabledExtendedFeatures(
    _In_ ULONG64 FeatureMask)
{
    UNREFERENCED_PARAMETER(FeatureMask);
    return 0;
}
