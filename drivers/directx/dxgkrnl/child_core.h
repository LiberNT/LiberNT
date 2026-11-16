/*
 * PROJECT:     LiberNT DirectX Graphics Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Testable normalization of the child descriptors a miniport reports
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

#include <ntddk.h>
#include <dispmprt.h>

VOID
DxgkChildNormalizeDescriptors(
    _Inout_updates_(Count) PDXGK_CHILD_DESCRIPTOR Children,
    _In_ ULONG Count);

BOOLEAN
DxgkChildHasVideoOutput(
    _In_reads_(Count) const DXGK_CHILD_DESCRIPTOR *Children,
    _In_ ULONG Count);

DXGKARG_DISPLAYDETECTCONTROL
DxgkChildMakeDisplayDetectControl(
    _In_ DXGK_DISPLAYDETECTCONTROLTYPE Type,
    _In_ D3DDDI_VIDEO_PRESENT_TARGET_ID TargetId,
    _In_ BOOLEAN NonDestructiveOnly);
