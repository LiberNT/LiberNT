/*
 * PROJECT:     LiberNT kernel-mode tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     POST display ownership handoff rollback state tests
 * COPYRIGHT:   Copyright 2026 Ahmed Arif <arif.ing@outlook.com>
 */

#include <kmt_test.h>
#undef DXGKDDI_INTERFACE_VERSION
#define DXGKDDI_INTERFACE_VERSION 0x11007
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

static
VOID
DxgkPostDisplayTestFirmwareScanout(VOID)
{
    ok_bool_true(DxgkPostDisplayCoreFirmwareScanout(TRUE, FALSE), "a display-only driver presents through the firmware framebuffer");
    ok_bool_true(DxgkPostDisplayCoreFirmwareScanout(TRUE, TRUE), "a committed display-only driver still scans out the firmware framebuffer");
    ok_bool_true(DxgkPostDisplayCoreFirmwareScanout(FALSE, FALSE), "a full miniport without a committed path leaves the firmware image on screen");
    ok_bool_false(DxgkPostDisplayCoreFirmwareScanout(FALSE, TRUE), "a full miniport with a committed path scans out its own primary");
}

static
VOID
DxgkPostDisplayTestApertureRunTakesBootWindow(VOID)
{
    const ULONGLONG Window = 0x70000000ULL;
    const ULONGLONG WindowBytes = 0x870000ULL;
    const ULONG Page = 0x10000;

    ok_bool_true(DxgkPostDisplayCoreApertureRunTakesBootWindow(Window, Page, 0, 1, Window, WindowBytes), "the first aperture page is the top of the boot screen");
    ok_bool_true(DxgkPostDisplayCoreApertureRunTakesBootWindow(Window, Page, 0, 9, Window, WindowBytes), "nine aperture pages cover the first rows of the boot screen");
    ok_bool_true(DxgkPostDisplayCoreApertureRunTakesBootWindow(Window, Page, 0x86, 1, Window, WindowBytes), "the last page that holds boot screen rows");
    ok_bool_false(DxgkPostDisplayCoreApertureRunTakesBootWindow(Window, Page, 0x87, 1, Window, WindowBytes), "the page after the boot screen");
    ok_bool_true(DxgkPostDisplayCoreApertureRunTakesBootWindow(Window, Page, 0x80, 0x100, Window, WindowBytes), "a run that starts inside the boot screen and ends after it");
    ok_bool_true(DxgkPostDisplayCoreApertureRunTakesBootWindow(Window - 0x100000, Page, 0, 0x200, Window, WindowBytes), "a run that covers the whole boot screen");
    ok_bool_false(DxgkPostDisplayCoreApertureRunTakesBootWindow(Window - 0x100000, Page, 0, 0x10, Window, WindowBytes), "a run that ends where the boot screen starts");
    ok_bool_true(DxgkPostDisplayCoreApertureRunTakesBootWindow(Window - 0x100000, Page, 0, 0x11, Window, WindowBytes), "a run whose last page is the top of the boot screen");
    ok_bool_false(DxgkPostDisplayCoreApertureRunTakesBootWindow(0xC0000000ULL, Page, 0, 0xE00, Window, WindowBytes), "an aperture on another address range");
    ok_bool_true(DxgkPostDisplayCoreApertureRunTakesBootWindow(Window, 0x1000, 0x86F, 1, Window, WindowBytes), "the last 4 KB page of the boot screen");
    ok_bool_false(DxgkPostDisplayCoreApertureRunTakesBootWindow(Window, 0x1000, 0x870, 1, Window, WindowBytes), "the 4 KB page after the boot screen");
    ok_bool_false(DxgkPostDisplayCoreApertureRunTakesBootWindow(Window, Page, 0, 0, Window, WindowBytes), "an empty run");
    ok_bool_false(DxgkPostDisplayCoreApertureRunTakesBootWindow(Window, Page, 0, 1, Window, 0), "no boot screen");
    ok_bool_false(DxgkPostDisplayCoreApertureRunTakesBootWindow(Window, 0, 0, 1, Window, WindowBytes), "an aperture without a page size");
}

static
VOID
DxgkPostDisplayTestUefiRangeSegment(VOID)
{
    ULONG Index = MAXULONG;

    ok_eq_uint(DxgkPostDisplayCoreUefiRangeSegment(1), 0);
    ok_eq_uint(DxgkPostDisplayCoreUefiRangeSegment(3), 2);
    ok_bool_true(DxgkPostDisplayCoreUefiRangeSegmentIndex(0, 3, &Index), "the miniport releases the first segment's firmware ranges as segment 0");
    ok_eq_ulong(Index, 0UL);
    ok_bool_true(DxgkPostDisplayCoreUefiRangeSegmentIndex(2, 3, &Index), "the last segment is still in range");
    ok_eq_ulong(Index, 2UL);
    ok_bool_false(DxgkPostDisplayCoreUefiRangeSegmentIndex(3, 3, &Index), "a position past the last segment is refused");
    ok_eq_ulong(Index, 0UL);
    ok_bool_false(DxgkPostDisplayCoreUefiRangeSegmentIndex(0, 0, &Index), "an adapter without segments has no firmware ranges to release");
    ok_bool_false(DxgkPostDisplayCoreUefiUnblockDeferred(PASSIVE_LEVEL), "a release at passive level is applied at once");
    ok_bool_false(DxgkPostDisplayCoreUefiUnblockDeferred(APC_LEVEL), "a release at APC level is applied at once");
    ok_bool_true(DxgkPostDisplayCoreUefiUnblockDeferred(DISPATCH_LEVEL), "a release from a DPC is handed to a worker");
}

static
D3DKMDT_WIRE_FORMAT_AND_PREFERENCE
DxgkPostDisplayTestWire(
    _In_ UINT Rgb,
    _In_ UINT YCbCr444,
    _In_ D3DKMDT_MODE_PREFERENCE Preference)
{
    D3DKMDT_WIRE_FORMAT_AND_PREFERENCE Format;

    Format.Value = 0;
    Format.Rgb = Rgb;
    Format.YCbCr444 = YCbCr444;
    Format.Preference = Preference;
    return Format;
}

static
VOID
DxgkPostDisplayTestFirmwareRequest(VOID)
{
    DXGKARG_RECOMMENDFUNCTIONALVIDPN Args;
    DXGK_POST_DISPLAY_FIRMWARE_DATA Data;
    D3DKMDT_HVIDPN VidPn = (D3DKMDT_HVIDPN)(ULONG_PTR)0x1234;
    ULONG Index;
    BOOLEAN Zeroed = TRUE;

    RtlFillMemory(&Args, sizeof(Args), 0xA5);
    RtlFillMemory(&Data, sizeof(Data), 0xA5);
    DxgkPostDisplayCoreFirmwareRequest(&Args, &Data, VidPn);
    ok_eq_uint(Args.RequestReason, DXGK_RFVR_FIRMWARE);
    ok_eq_pointer(Args.hRecommendedFunctionalVidPn, VidPn);
    ok_eq_pointer(Args.pPrivateDriverData, &Data);
    ok_eq_uint(Args.PrivateDriverDataSize, 12);
    ok_eq_uint(Args.NumberOfVidPnTargets, 0);
    ok_eq_pointer(Args.pVidPnTargetPrioritizationVector, NULL);
    for (Index = 0; Index < sizeof(Data); Index++)
    {
        if (((PUCHAR)&Data)[Index] != 0)
            Zeroed = FALSE;
    }
    ok_bool_true(Zeroed, "the firmware block the driver writes into starts zeroed");

    ok_bool_false(DxgkPostDisplayCoreFirmwareTopologyUsable(0), "no firmware path describes nothing");
    ok_bool_true(DxgkPostDisplayCoreFirmwareTopologyUsable(1), "one firmware path is taken");
    ok_bool_false(DxgkPostDisplayCoreFirmwareTopologyUsable(2), "a firmware clone is not taken");
}

static
VOID
DxgkPostDisplayTestFirmwareWireFormat(VOID)
{
    DXGK_POST_DISPLAY_FIRMWARE_DATA Data;
    D3DKMDT_WIRE_FORMAT_AND_PREFERENCE Format;
    D3DKMDT_WIRE_FORMAT_AND_PREFERENCE Supported;

    RtlZeroMemory(&Data, sizeof(Data));
    Data.WireFormat = DxgkPostDisplayTestWire(D3DKMDT_BITS_PER_COMPONENT_06, 0, D3DKMDT_MP_UNINITIALIZED);
    Format = DxgkPostDisplayCoreFirmwareWireFormat(&Data);
    ok_eq_uint(Format.Value, DxgkPostDisplayTestWire(D3DKMDT_BITS_PER_COMPONENT_06, 0, D3DKMDT_MP_UNINITIALIZED).Value);

    Data.WireFormat = DxgkPostDisplayTestWire(D3DKMDT_BITS_PER_COMPONENT_08, 0, D3DKMDT_MP_PREFERRED);
    Format = DxgkPostDisplayCoreFirmwareWireFormat(&Data);
    ok_eq_uint(Format.Value, DxgkPostDisplayTestWire(D3DKMDT_BITS_PER_COMPONENT_08, 0, D3DKMDT_MP_UNINITIALIZED).Value);

    Data.WireFormat = DxgkPostDisplayTestWire(D3DKMDT_BITS_PER_COMPONENT_08 | D3DKMDT_BITS_PER_COMPONENT_10, 0, D3DKMDT_MP_UNINITIALIZED);
    ok_eq_uint(DxgkPostDisplayCoreFirmwareWireFormat(&Data).Value, 0);
    Data.WireFormat = DxgkPostDisplayTestWire(D3DKMDT_BITS_PER_COMPONENT_08, D3DKMDT_BITS_PER_COMPONENT_08, D3DKMDT_MP_UNINITIALIZED);
    ok_eq_uint(DxgkPostDisplayCoreFirmwareWireFormat(&Data).Value, 0);
    Data.WireFormat.Value = 0;
    ok_eq_uint(DxgkPostDisplayCoreFirmwareWireFormat(&Data).Value, 0);

    Format = DxgkPostDisplayTestWire(D3DKMDT_BITS_PER_COMPONENT_06, 0, D3DKMDT_MP_UNINITIALIZED);
    Supported = DxgkPostDisplayTestWire(D3DKMDT_BITS_PER_COMPONENT_06 | D3DKMDT_BITS_PER_COMPONENT_08, 0, D3DKMDT_MP_PREFERRED);
    ok_bool_true(DxgkPostDisplayCoreFirmwareWireFormatUsable(Format, Supported), "the target mode offers the depth firmware drives");
    Supported = DxgkPostDisplayTestWire(D3DKMDT_BITS_PER_COMPONENT_08, 0, D3DKMDT_MP_UNINITIALIZED);
    ok_bool_false(DxgkPostDisplayCoreFirmwareWireFormatUsable(Format, Supported), "the target mode lacks the depth firmware drives");
    Format.Value = 0;
    ok_bool_false(DxgkPostDisplayCoreFirmwareWireFormatUsable(Format, Supported), "no firmware wire format");
}

START_TEST(DxgkPostDisplayHandoff)
{
    DxgkPostDisplayTestRestartableFailure();
    DxgkPostDisplayTestSuccessfulClaimant();
    DxgkPostDisplayTestNonRestartableFailure();
    DxgkPostDisplayTestPostDevice();
    DxgkPostDisplayTestFirmwareScanout();
    DxgkPostDisplayTestApertureRunTakesBootWindow();
    DxgkPostDisplayTestUefiRangeSegment();
    DxgkPostDisplayTestFirmwareRequest();
    DxgkPostDisplayTestFirmwareWireFormat();
}
