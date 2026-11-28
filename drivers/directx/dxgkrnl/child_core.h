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

BOOLEAN
DxgkChildPollsThroughDetectControl(
    _In_ BOOLEAN HasDisplayDetectControl,
    _In_ BOOLEAN HasQueryConnectionChange);

BOOLEAN
DxgkChildConnectionFromStatusQuery(
    _In_ DXGK_CHILD_DEVICE_HPD_AWARENESS HpdAwareness,
    _In_ BOOLEAN ReportsConnectionChanges);

typedef struct _DXGK_CHILD_CONNECTION_RECORD
{
    ULONG ChildUid;
    ULONG Sequence;
    BOOLEAN Observed;
    BOOLEAN Connected;
} DXGK_CHILD_CONNECTION_RECORD, *PDXGK_CHILD_CONNECTION_RECORD;

ULONG
DxgkChildCollectVideoOutputUids(
    _In_reads_(Count) const DXGK_CHILD_DESCRIPTOR *Children,
    _In_ ULONG Count,
    _Out_writes_to_(Capacity, return) PULONG Uids,
    _In_ ULONG Capacity);

ULONG
DxgkChildInitializeConnectionRecords(
    _In_reads_(Count) const DXGK_CHILD_DESCRIPTOR *Children,
    _In_ ULONG Count,
    _Out_writes_to_(Capacity, return) PDXGK_CHILD_CONNECTION_RECORD Records,
    _In_ ULONG Capacity);

PDXGK_CHILD_CONNECTION_RECORD
DxgkChildFindConnectionRecord(
    _In_reads_opt_(Count) PDXGK_CHILD_CONNECTION_RECORD Records,
    _In_ ULONG Count,
    _In_ ULONG ChildUid);

BOOLEAN
DxgkChildRecordConnection(
    _Inout_updates_opt_(Count) PDXGK_CHILD_CONNECTION_RECORD Records,
    _In_ ULONG Count,
    _In_ ULONG ChildUid,
    _In_ BOOLEAN Connected,
    _Out_ PBOOLEAN Changed);

BOOLEAN
DxgkChildResolveEnumeratedConnection(
    _In_ DXGK_CHILD_DEVICE_HPD_AWARENESS HpdAwareness,
    _In_ BOOLEAN QueryKnown,
    _In_ BOOLEAN QueryConnected,
    _In_opt_ const DXGK_CHILD_CONNECTION_RECORD *Record,
    _In_ ULONG SequenceBeforeQuery);
