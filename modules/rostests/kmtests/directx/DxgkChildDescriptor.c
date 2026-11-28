/*
 * PROJECT:     LiberNT kernel-mode tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Normalization of the child descriptors a display miniport reports
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <kmt_test.h>
#include <windef.h>
#undef DXGKDDI_INTERFACE_VERSION
#define DXGKDDI_INTERFACE_VERSION 0x11007
#include "child_core.h"

START_TEST(DxgkChildDescriptor)
{
    DXGK_CHILD_DESCRIPTOR Children[4];

    RtlZeroMemory(Children, sizeof(Children));
    /* Comet Lake-H laptop iGPU: integrated eDP panel and an HDMI output. */
    Children[0].ChildDeviceType = TypeIntegratedDisplay;
    Children[0].ChildCapabilities.Type.IntegratedDisplayChild.InterfaceTechnology = D3DKMDT_VOT_DISPLAYPORT_EMBEDDED;
    Children[0].ChildCapabilities.Type.IntegratedDisplayChild.DescriptorLength = 128;
    Children[0].ChildCapabilities.HpdAwareness = HpdAwarenessInterruptible;
    Children[0].AcpiUid = 0x400;
    Children[0].ChildUid = 0x40F04;
    Children[1].ChildDeviceType = TypeVideoOutput;
    Children[1].ChildCapabilities.Type.VideoOutput.InterfaceTechnology = D3DKMDT_VOT_HDMI;
    Children[1].ChildCapabilities.Type.VideoOutput.MonitorOrientationAwareness = D3DKMDT_MOA_NONE;
    Children[1].ChildCapabilities.HpdAwareness = HpdAwarenessInterruptible;
    Children[1].ChildUid = 0x31003;
    Children[3].ChildDeviceType = TypeIntegratedDisplay;

    DxgkChildNormalizeDescriptors(Children, RTL_NUMBER_OF(Children));

    ok_eq_long(Children[0].ChildDeviceType, TypeVideoOutput);
    ok_eq_long(Children[0].ChildCapabilities.Type.VideoOutput.InterfaceTechnology, D3DKMDT_VOT_INTERNAL);
    ok_eq_long(Children[0].ChildCapabilities.Type.VideoOutput.MonitorOrientationAwareness, D3DKMDT_MOA_NONE);
    ok_eq_bool(Children[0].ChildCapabilities.Type.VideoOutput.SupportsSdtvModes, FALSE);
    ok_eq_long(Children[0].ChildCapabilities.HpdAwareness, HpdAwarenessInterruptible);
    ok_eq_ulong(Children[0].AcpiUid, 0x400UL);
    ok_eq_ulong(Children[0].ChildUid, 0x40F04UL);

    ok_eq_long(Children[1].ChildDeviceType, TypeVideoOutput);
    ok_eq_long(Children[1].ChildCapabilities.Type.VideoOutput.InterfaceTechnology, D3DKMDT_VOT_HDMI);
    ok_eq_ulong(Children[1].ChildUid, 0x31003UL);

    ok_eq_long(Children[3].ChildDeviceType, TypeIntegratedDisplay);
}

START_TEST(DxgkDisplayDetectControl)
{
    DXGK_CHILD_DESCRIPTOR Children[3];
    DXGKARG_DISPLAYDETECTCONTROL Args;
    UINT Raw;

    C_ASSERT(sizeof(DXGKARG_DISPLAYDETECTCONTROL) == sizeof(UINT));

    Args = DxgkChildMakeDisplayDetectControl(DXGK_DDCT_ENABLEHPD, D3DDDI_ID_ALL, FALSE);
    RtlCopyMemory(&Raw, &Args, sizeof(Raw));
    ok_eq_hex(Raw, 0x03FFFFFDU);
    Args = DxgkChildMakeDisplayDetectControl(DXGK_DDCT_DISABLEHPD, D3DDDI_ID_ALL, FALSE);
    RtlCopyMemory(&Raw, &Args, sizeof(Raw));
    ok_eq_hex(Raw, 0x04FFFFFDU);
    Args = DxgkChildMakeDisplayDetectControl(DXGK_DDCT_POLLONE, 0x104, TRUE);
    RtlCopyMemory(&Raw, &Args, sizeof(Raw));
    ok_eq_hex(Raw, 0x11000104U);

    RtlZeroMemory(Children, sizeof(Children));
    ok_eq_bool(DxgkChildHasVideoOutput(Children, RTL_NUMBER_OF(Children)), FALSE);
    Children[0].ChildDeviceType = TypeOther;
    Children[2].ChildDeviceType = TypeVideoOutput;
    ok_eq_bool(DxgkChildHasVideoOutput(Children, RTL_NUMBER_OF(Children)), FALSE);
    Children[1].ChildDeviceType = TypeVideoOutput;
    ok_eq_bool(DxgkChildHasVideoOutput(Children, RTL_NUMBER_OF(Children)), TRUE);
    ok_eq_bool(DxgkChildHasVideoOutput(Children, 1), FALSE);
}

START_TEST(DxgkChildConnectionRecord)
{
    DXGK_CHILD_DESCRIPTOR Children[7];
    DXGK_CHILD_CONNECTION_RECORD Records[6];
    PDXGK_CHILD_CONNECTION_RECORD Panel;
    ULONG Uids[6];
    ULONG Count;
    ULONG Sequence;
    BOOLEAN Changed;

    ok_eq_bool(DxgkChildConnectionFromStatusQuery(HpdAwarenessInterruptible, TRUE), FALSE);
    ok_eq_bool(DxgkChildConnectionFromStatusQuery(HpdAwarenessPolled, TRUE), FALSE);
    ok_eq_bool(DxgkChildConnectionFromStatusQuery(HpdAwarenessInterruptible, FALSE), TRUE);
    ok_eq_bool(DxgkChildConnectionFromStatusQuery(HpdAwarenessPolled, FALSE), TRUE);
    ok_eq_bool(DxgkChildConnectionFromStatusQuery(HpdAwarenessAlwaysConnected, FALSE), FALSE);
    ok_eq_bool(DxgkChildConnectionFromStatusQuery(HpdAwarenessNone, FALSE), FALSE);
    ok_eq_bool(DxgkChildConnectionFromStatusQuery(HpdAwarenessUninitialized, FALSE), FALSE);
    ok_eq_bool(DxgkChildPollsThroughDetectControl(TRUE, TRUE), TRUE);
    ok_eq_bool(DxgkChildPollsThroughDetectControl(TRUE, FALSE), FALSE);
    ok_eq_bool(DxgkChildPollsThroughDetectControl(FALSE, TRUE), FALSE);
    ok_eq_bool(DxgkChildPollsThroughDetectControl(FALSE, FALSE), FALSE);

    /* GTX 1070 laptop: HDMI 0x1100, internal panel 0x1101, DisplayPort 0x1102. */
    RtlZeroMemory(Children, sizeof(Children));
    Children[0].ChildDeviceType = TypeVideoOutput;
    Children[0].ChildCapabilities.Type.VideoOutput.InterfaceTechnology = D3DKMDT_VOT_HDMI;
    Children[0].ChildCapabilities.HpdAwareness = HpdAwarenessInterruptible;
    Children[0].ChildUid = 0x1100;
    Children[1].ChildDeviceType = TypeOther;
    Children[1].ChildUid = 0x2000;
    Children[2].ChildDeviceType = TypeVideoOutput;
    Children[2].ChildCapabilities.Type.VideoOutput.InterfaceTechnology = D3DKMDT_VOT_INTERNAL;
    Children[2].ChildCapabilities.HpdAwareness = HpdAwarenessInterruptible;
    Children[2].ChildUid = 0x1101;
    Children[3] = Children[0];
    Children[4].ChildDeviceType = TypeVideoOutput;
    Children[4].ChildCapabilities.Type.VideoOutput.InterfaceTechnology = D3DKMDT_VOT_DISPLAYPORT_EXTERNAL;
    Children[4].ChildCapabilities.HpdAwareness = HpdAwarenessInterruptible;
    Children[4].ChildUid = 0x1102;
    Children[6].ChildDeviceType = TypeVideoOutput;
    Children[6].ChildUid = 0x1103;

    RtlFillMemory(Uids, sizeof(Uids), 0xA5);
    Count = DxgkChildCollectVideoOutputUids(Children, RTL_NUMBER_OF(Children), Uids, RTL_NUMBER_OF(Uids));
    ok_eq_ulong(Count, 3UL);
    ok_eq_hex(Uids[0], 0x1100UL);
    ok_eq_hex(Uids[1], 0x1101UL);
    ok_eq_hex(Uids[2], 0x1102UL);
    ok_eq_hex(Uids[3], 0xA5A5A5A5UL);
    Count = DxgkChildCollectVideoOutputUids(Children, RTL_NUMBER_OF(Children), Uids, 2);
    ok_eq_ulong(Count, 2UL);
    ok_eq_ulong(DxgkChildCollectVideoOutputUids(Children, 1, Uids, RTL_NUMBER_OF(Uids)), 1UL);
    ok_eq_ulong(DxgkChildCollectVideoOutputUids(NULL, 0, Uids, RTL_NUMBER_OF(Uids)), 0UL);

    RtlFillMemory(Records, sizeof(Records), 0xA5);
    Count = DxgkChildInitializeConnectionRecords(Children, RTL_NUMBER_OF(Children), Records, RTL_NUMBER_OF(Records));
    ok_eq_ulong(Count, 3UL);
    ok_eq_hex(Records[1].ChildUid, 0x1101UL);
    ok_eq_bool(Records[1].Observed, FALSE);
    ok_eq_bool(Records[1].Connected, FALSE);
    ok_eq_ulong(Records[1].Sequence, 0UL);
    ok_eq_pointer(DxgkChildFindConnectionRecord(Records, Count, 0x2000), NULL);
    ok_eq_pointer(DxgkChildFindConnectionRecord(Records, Count, 0x1103), NULL);
    ok_eq_pointer(DxgkChildFindConnectionRecord(NULL, 0, 0x1101), NULL);
    Panel = DxgkChildFindConnectionRecord(Records, Count, 0x1101);
    ok_eq_pointer(Panel, &Records[1]);
    if (Panel == NULL)
        return;

    Changed = TRUE;
    ok_eq_bool(DxgkChildRecordConnection(Records, Count, 0x1400, TRUE, &Changed), FALSE);
    ok_eq_bool(Changed, FALSE);
    ok_eq_bool(DxgkChildRecordConnection(Records, Count, 0x2000, TRUE, &Changed), FALSE);

    /* ENABLEHPD at start: the panel is reported connected before BusRelations creates its child object. */
    ok_eq_bool(DxgkChildRecordConnection(Records, Count, 0x1101, 0x80, &Changed), TRUE);
    ok_eq_bool(Changed, TRUE);
    ok_eq_bool(Panel->Observed, TRUE);
    ok_eq_bool(Panel->Connected, TRUE);
    ok_eq_ulong(Panel->Sequence, 1UL);
    ok_eq_bool(DxgkChildRecordConnection(Records, Count, 0x1101, TRUE, &Changed), TRUE);
    ok_eq_bool(Changed, FALSE);
    ok_eq_ulong(Panel->Sequence, 2UL);
    ok_eq_bool(Records[0].Observed, FALSE);

    /* BusRelations then queries the panel, and the miniport fails that query. */
    Sequence = Panel->Sequence;
    ok_eq_bool(DxgkChildResolveEnumeratedConnection(HpdAwarenessInterruptible, FALSE, FALSE, Panel, Sequence), TRUE);
    ok_eq_bool(DxgkChildResolveEnumeratedConnection(HpdAwarenessInterruptible, TRUE, FALSE, Panel, Sequence), FALSE);
    ok_eq_bool(DxgkChildResolveEnumeratedConnection(HpdAwarenessInterruptible, TRUE, TRUE, Panel, Sequence), TRUE);
    ok_eq_bool(DxgkChildResolveEnumeratedConnection(HpdAwarenessInterruptible, TRUE, FALSE, Panel, Sequence - 1), TRUE);
    ok_eq_bool(DxgkChildRecordConnection(Records, Count, 0x1101, FALSE, &Changed), TRUE);
    ok_eq_bool(Changed, TRUE);
    ok_eq_bool(DxgkChildResolveEnumeratedConnection(HpdAwarenessInterruptible, TRUE, TRUE, Panel, Sequence), FALSE);
    ok_eq_bool(DxgkChildResolveEnumeratedConnection(HpdAwarenessAlwaysConnected, FALSE, FALSE, Panel, Sequence), TRUE);
    ok_eq_bool(DxgkChildResolveEnumeratedConnection(HpdAwarenessPolled, FALSE, FALSE, NULL, 0), FALSE);
    ok_eq_bool(DxgkChildResolveEnumeratedConnection(HpdAwarenessPolled, TRUE, TRUE, NULL, 0), TRUE);
    ok_eq_bool(DxgkChildResolveEnumeratedConnection(HpdAwarenessInterruptible, FALSE, FALSE, &Records[0], Records[0].Sequence), FALSE);
    Panel->Observed = FALSE;
    ok_eq_bool(DxgkChildResolveEnumeratedConnection(HpdAwarenessInterruptible, FALSE, FALSE, Panel, Sequence), FALSE);
}
