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

/* Windows answers DisplayDetectControl and QueryConnectionChange with DxgkDdiQueryChildStatus only for a miniport that lacks them */
BOOLEAN
DxgkChildConnectionFromStatusQuery(
    _In_ DXGK_CHILD_DEVICE_HPD_AWARENESS HpdAwareness,
    _In_ BOOLEAN ReportsConnectionChanges)
{
    if (HpdAwareness != HpdAwarenessPolled && HpdAwareness != HpdAwarenessInterruptible)
        return FALSE;
    return !ReportsConnectionChanges;
}

ULONG
DxgkChildCollectVideoOutputUids(
    _In_reads_(Count) const DXGK_CHILD_DESCRIPTOR *Children,
    _In_ ULONG Count,
    _Out_writes_to_(Capacity, return) PULONG Uids,
    _In_ ULONG Capacity)
{
    ULONG Index;
    ULONG Collected = 0;

    ASSERT(Children != NULL || Count == 0);
    ASSERT(Uids != NULL || Capacity == 0);
    for (Index = 0; Index < Count && Collected < Capacity && Children[Index].ChildDeviceType != TypeUninitialized; Index++)
    {
        ULONG Seen;

        if (Children[Index].ChildDeviceType != TypeVideoOutput)
            continue;
        for (Seen = 0; Seen < Collected && Uids[Seen] != Children[Index].ChildUid; Seen++)
            ;
        if (Seen < Collected)
            continue;
        Uids[Collected++] = Children[Index].ChildUid;
    }
    ASSERT(Collected <= Capacity);
    return Collected;
}

ULONG
DxgkChildInitializeConnectionRecords(
    _In_reads_(Count) const DXGK_CHILD_DESCRIPTOR *Children,
    _In_ ULONG Count,
    _Out_writes_to_(Capacity, return) PDXGK_CHILD_CONNECTION_RECORD Records,
    _In_ ULONG Capacity)
{
    ULONG Index;
    ULONG Collected = 0;

    ASSERT(Children != NULL || Count == 0);
    ASSERT(Records != NULL || Capacity == 0);
    for (Index = 0; Index < Count && Collected < Capacity && Children[Index].ChildDeviceType != TypeUninitialized; Index++)
    {
        if (Children[Index].ChildDeviceType != TypeVideoOutput ||
            DxgkChildFindConnectionRecord(Records, Collected, Children[Index].ChildUid) != NULL)
        {
            continue;
        }
        RtlZeroMemory(&Records[Collected], sizeof(Records[Collected]));
        Records[Collected].ChildUid = Children[Index].ChildUid;
        Collected++;
    }
    ASSERT(Collected <= Capacity);
    return Collected;
}

PDXGK_CHILD_CONNECTION_RECORD
DxgkChildFindConnectionRecord(
    _In_reads_opt_(Count) PDXGK_CHILD_CONNECTION_RECORD Records,
    _In_ ULONG Count,
    _In_ ULONG ChildUid)
{
    ULONG Index;

    ASSERT(Records != NULL || Count == 0);
    for (Index = 0; Index < Count; Index++)
    {
        if (Records[Index].ChildUid == ChildUid)
            return &Records[Index];
    }
    return NULL;
}

/* The OS knows its targets from DxgkDdiQueryChildRelations on, before BusRelations creates their child objects */
BOOLEAN
DxgkChildRecordConnection(
    _Inout_updates_opt_(Count) PDXGK_CHILD_CONNECTION_RECORD Records,
    _In_ ULONG Count,
    _In_ ULONG ChildUid,
    _In_ BOOLEAN Connected,
    _Out_ PBOOLEAN Changed)
{
    PDXGK_CHILD_CONNECTION_RECORD Record;
    BOOLEAN NewConnected = Connected ? TRUE : FALSE;

    *Changed = FALSE;
    Record = DxgkChildFindConnectionRecord(Records, Count, ChildUid);
    if (Record == NULL)
        return FALSE;
    *Changed = !Record->Observed || Record->Connected != NewConnected;
    Record->Observed = TRUE;
    Record->Connected = NewConnected;
    Record->Sequence++;
    return TRUE;
}

/* A change reported after the query began is newer than its answer; a failed query leaves the last change standing */
BOOLEAN
DxgkChildResolveEnumeratedConnection(
    _In_ DXGK_CHILD_DEVICE_HPD_AWARENESS HpdAwareness,
    _In_ BOOLEAN QueryKnown,
    _In_ BOOLEAN QueryConnected,
    _In_opt_ const DXGK_CHILD_CONNECTION_RECORD *Record,
    _In_ ULONG SequenceBeforeQuery)
{
    if (HpdAwareness == HpdAwarenessAlwaysConnected)
        return TRUE;
    if (Record != NULL && Record->Observed &&
        (!QueryKnown || Record->Sequence != SequenceBeforeQuery))
    {
        return Record->Connected;
    }
    return QueryKnown && QueryConnected;
}
