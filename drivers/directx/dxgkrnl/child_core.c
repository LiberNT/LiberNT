/*
 * PROJECT:     LiberNT DirectX Graphics Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Testable normalization of the child descriptors a miniport reports
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

/* dxgkrnl's DDI version, also when the kmtest driver compiles this file */
#undef DXGKDDI_INTERFACE_VERSION
#define DXGKDDI_INTERFACE_VERSION 0x11007
#include "child_core.h"

VOID
DxgkChildNormalizeDescriptors(
    _Inout_updates_(Count) PDXGK_CHILD_DESCRIPTOR Children,
    _In_ ULONG Count)
{
    ULONG Index;

    for (Index = 0; Index < Count && Children[Index].ChildDeviceType != TypeUninitialized; Index++)
    {
        PDXGK_CHILD_CAPABILITIES Capabilities = &Children[Index].ChildCapabilities;

        if (Children[Index].ChildDeviceType != TypeIntegratedDisplay)
            continue;

        /* TODO: keep IntegratedDisplayChild.InterfaceTechnology and DescriptorLength for the integrated display descriptor query */
        Children[Index].ChildDeviceType = TypeVideoOutput;
        Capabilities->Type.VideoOutput.InterfaceTechnology = D3DKMDT_VOT_INTERNAL;
        Capabilities->Type.VideoOutput.MonitorOrientationAwareness = D3DKMDT_MOA_NONE;
        Capabilities->Type.VideoOutput.SupportsSdtvModes = FALSE;
    }
}

BOOLEAN
DxgkChildHasVideoOutput(
    _In_reads_(Count) const DXGK_CHILD_DESCRIPTOR *Children,
    _In_ ULONG Count)
{
    ULONG Index;

    ASSERT(Children != NULL || Count == 0);
    for (Index = 0; Index < Count && Children[Index].ChildDeviceType != TypeUninitialized; Index++)
    {
        if (Children[Index].ChildDeviceType == TypeVideoOutput)
            return TRUE;
    }
    return FALSE;
}

DXGKARG_DISPLAYDETECTCONTROL
DxgkChildMakeDisplayDetectControl(
    _In_ DXGK_DISPLAYDETECTCONTROLTYPE Type,
    _In_ D3DDDI_VIDEO_PRESENT_TARGET_ID TargetId,
    _In_ BOOLEAN NonDestructiveOnly)
{
    DXGKARG_DISPLAYDETECTCONTROL Args;

    ASSERT(Type >= DXGK_DDCT_POLLONE && Type <= DXGK_DDCT_DISABLEHPD);
    ASSERT((Type != DXGK_DDCT_ENABLEHPD && Type != DXGK_DDCT_DISABLEHPD) || TargetId == D3DDDI_ID_ALL);
    RtlZeroMemory(&Args, sizeof(Args));
    Args.TargetId = TargetId & 0xFFFFFF;
    Args.Type = Type;
    Args.NonDestructiveOnly = NonDestructiveOnly ? 1 : 0;
    return Args;
}
