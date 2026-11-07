/*
 * PROJECT:     LiberNT kernel-mode tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     POST display ownership handoff rollback state tests
 * COPYRIGHT:   Copyright 2026 Ahmed Arif <arif.ing@outlook.com>
 */

#include <kmt_test.h>
#include "postdisplay_core.h"
#include "adapter_map_core.h"

#define POST_TEST_MAX_BARS 3

typedef struct _POST_TEST_RESOURCES
{
    CM_RESOURCE_LIST List;
    CM_PARTIAL_RESOURCE_DESCRIPTOR More[POST_TEST_MAX_BARS - 1];
} POST_TEST_RESOURCES;

static
PCM_RESOURCE_LIST
DxgkPostDisplayTestBars(
    _Out_ POST_TEST_RESOURCES *Resources,
    _In_ ULONG Count,
    _In_reads_(Count) const ULONGLONG *Bases,
    _In_reads_(Count) const ULONG *Lengths)
{
    PCM_PARTIAL_RESOURCE_DESCRIPTOR Partial;
    ULONG Index;

    RtlZeroMemory(Resources, sizeof(*Resources));
    Resources->List.Count = 1;
    Resources->List.List[0].InterfaceType = PCIBus;
    Resources->List.List[0].PartialResourceList.Version = 1;
    Resources->List.List[0].PartialResourceList.Revision = 1;
    Resources->List.List[0].PartialResourceList.Count = Count;
    Partial = Resources->List.List[0].PartialResourceList.PartialDescriptors;
    for (Index = 0; Index < Count; Index++)
    {
        Partial[Index].Type = CmResourceTypeMemory;
        Partial[Index].ShareDisposition = CmResourceShareDeviceExclusive;
        Partial[Index].u.Memory.Start.QuadPart = (LONGLONG)Bases[Index];
        Partial[Index].u.Memory.Length = Lengths[Index];
    }
    return &Resources->List;
}

static
VOID
DxgkPostDisplayTestPostDevice(VOID)
{
    /* Comet Lake-H laptop: GOP 1920x1080 at 0xA0000000, pitch 7680, inside the iGPU aperture. */
    static const ULONGLONG IntelBases[] = { 0xC3000000, 0xA0000000 };
    static const ULONG IntelLengths[] = { 0x01000000, 0x10000000 };
    static const ULONGLONG NvidiaBases[] = { 0xC4000000, 0xB0000000, 0xC0000000 };
    static const ULONG NvidiaLengths[] = { 0x01000000, 0x10000000, 0x02000000 };
    POST_TEST_RESOURCES Resources;
    PHYSICAL_ADDRESS FrameBuffer;
    ULONGLONG FrameBufferLength = 7680ULL * 1080;

    FrameBuffer.QuadPart = 0xA0000000;
    ok_bool_true(DxgkAdapterIsPostDevice(DxgkPostDisplayTestBars(&Resources, 2, IntelBases, IntelLengths),
                                         FrameBuffer, FrameBufferLength, FALSE),
                 "the adapter whose BAR holds the firmware framebuffer is the POST device");
    ok_bool_false(DxgkAdapterIsPostDevice(DxgkPostDisplayTestBars(&Resources, 3, NvidiaBases, NvidiaLengths),
                                          FrameBuffer, FrameBufferLength, FALSE),
                  "a PCI adapter whose BARs miss the firmware framebuffer is not the POST device");
    ok_bool_true(DxgkAdapterIsPostDevice(DxgkPostDisplayTestBars(&Resources, 3, NvidiaBases, NvidiaLengths),
                                         FrameBuffer, FrameBufferLength, TRUE),
                 "an ACPI-enumerated display without a matching BAR is the POST device");
    ok_bool_true(DxgkAdapterIsPostDevice(NULL, FrameBuffer, FrameBufferLength, FALSE),
                 "an adapter without memory resources cannot be matched and stays a POST candidate");
    FrameBuffer.QuadPart = 0xAFFFF000;
    ok_bool_false(DxgkAdapterIsPostDevice(DxgkPostDisplayTestBars(&Resources, 2, IntelBases, IntelLengths),
                                          FrameBuffer, FrameBufferLength, FALSE),
                  "a framebuffer that runs past the end of the BAR is not inside it");
}

static
VOID
DxgkPostDisplayTestRestartableFailure(VOID)
{
    DXGK_POST_DISPLAY_HANDOFF_CORE Core = {0};
    DXGK_POST_DISPLAY_COMPLETION_ACTION Completion;

    ok_bool_true(DxgkPostDisplayCoreArm(&Core), "the stopped fallback arms one handoff transaction");
    ok_bool_false(DxgkPostDisplayCoreArm(&Core), "a claimant cannot replace its pending fallback");
    Completion = DxgkPostDisplayCoreComplete(&Core, STATUS_DEVICE_HARDWARE_ERROR, TRUE);
    ok_eq_long(Completion, DxgkPostDisplayCompletionRollback);
    Completion = DxgkPostDisplayCoreComplete(&Core, STATUS_DEVICE_HARDWARE_ERROR, TRUE);
    ok_eq_long(Completion, DxgkPostDisplayCompletionNone);
}

static
VOID
DxgkPostDisplayTestSuccessfulClaimant(VOID)
{
    DXGK_POST_DISPLAY_HANDOFF_CORE Core = {0};
    DXGK_POST_DISPLAY_COMPLETION_ACTION Completion;

    ok_bool_true(DxgkPostDisplayCoreArm(&Core), "a fallback can be retained for a new claimant");
    Completion = DxgkPostDisplayCoreComplete(&Core, STATUS_SUCCESS, TRUE);
    ok_eq_long(Completion, DxgkPostDisplayCompletionCommit);
    Completion = DxgkPostDisplayCoreComplete(&Core, STATUS_DEVICE_REMOVED, TRUE);
    ok_eq_long(Completion, DxgkPostDisplayCompletionNone);
    ok_bool_true(DxgkPostDisplayCoreArm(&Core), "a committed transaction leaves the state reusable");
}

static
VOID
DxgkPostDisplayTestNonRestartableFailure(VOID)
{
    DXGK_POST_DISPLAY_HANDOFF_CORE Core = {0};
    DXGK_POST_DISPLAY_COMPLETION_ACTION Completion;

    ok_bool_true(DxgkPostDisplayCoreArm(&Core), "the fallback is retained until claimant completion");
    Completion = DxgkPostDisplayCoreComplete(&Core, STATUS_DEVICE_HARDWARE_ERROR, FALSE);
    ok_eq_long(Completion, DxgkPostDisplayCompletionDefer);
    ok_bool_false(DxgkPostDisplayCoreArm(&Core), "a claimant that could not be stopped keeps its fallback");
    Completion = DxgkPostDisplayCoreComplete(&Core, STATUS_DEVICE_REMOVED, TRUE);
    ok_eq_long(Completion, DxgkPostDisplayCompletionRollback);
    Completion = DxgkPostDisplayCoreComplete(&Core, STATUS_DEVICE_REMOVED, TRUE);
    ok_eq_long(Completion, DxgkPostDisplayCompletionNone);
}

START_TEST(DxgkPostDisplayHandoff)
{
    DxgkPostDisplayTestRestartableFailure();
    DxgkPostDisplayTestSuccessfulClaimant();
    DxgkPostDisplayTestNonRestartableFailure();
    DxgkPostDisplayTestPostDevice();
}
