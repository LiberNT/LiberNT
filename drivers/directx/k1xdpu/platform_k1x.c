/*
 * PROJECT:     LiberNT SpacemiT K1 display miniport
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     K1 DPU platform provider for the software GPU engine
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "softgpu.h"
#include "softgpu_2d_core.h"
#include "k1xdpu.h"

static NTSTATUS
K1xDdcFinish(_In_ PUCHAR Hdmi)
{
    ULONG Elapsed, Status;

    for (Elapsed = 0; Elapsed < K1X_DDC_TIMEOUT_US; Elapsed += K1X_DDC_POLL_US)
    {
        Status = K1xRead(Hdmi, K1X_HDMI_STATUS);
        if (Status & K1X_HDMI_STATUS_DDC_DONE)
        {
            K1xWrite(Hdmi, K1X_HDMI_STATUS, Status | K1X_HDMI_STATUS_DDC_DONE);
            return (Status & (K1X_HDMI_STATUS_DDC_NACK | K1X_HDMI_STATUS_DDC_LOST)) ?
                       STATUS_DEVICE_PROTOCOL_ERROR : STATUS_SUCCESS;
        }
        KeStallExecutionProcessor(K1X_DDC_POLL_US);
    }
    return STATUS_IO_TIMEOUT;
}

static NTSTATUS
K1xDdcReadEdid(_In_ PUCHAR Hdmi, _In_ UCHAR Offset, _Out_writes_bytes_(Length) PUCHAR Buffer, _In_ ULONG Length)
{
    ULONG Done = 0, Count, Index, Elapsed, Status;
    NTSTATUS Result;

    if (!(K1xRead(Hdmi, K1X_HDMI_STATUS) & K1X_HDMI_STATUS_HPD))
        return STATUS_DEVICE_NOT_CONNECTED;

    K1xWrite(Hdmi, K1X_HDMI_DDC_TX, Offset);
    K1xWrite(Hdmi, K1X_HDMI_DDC_COMMAND, K1X_EDID_ADDRESS << 1);
    Result = K1xDdcFinish(Hdmi);
    if (!NT_SUCCESS(Result))
        return Result;

    while (Done < Length)
    {
        Count = min(Length - Done, (ULONG)K1X_HDMI_DDC_CHUNK);
        K1xWrite(Hdmi, K1X_HDMI_DDC_COMMAND, ((Count - 1) << 8) | (K1X_EDID_ADDRESS << 1) | 1);
        for (Elapsed = 0; ; Elapsed += K1X_DDC_POLL_US)
        {
            Status = K1xRead(Hdmi, K1X_HDMI_STATUS);
            if (!(Status & K1X_HDMI_STATUS_HPD))
                return STATUS_DEVICE_NOT_CONNECTED;
            if (((Status & K1X_HDMI_STATUS_RX_COUNT_MASK) >> K1X_HDMI_STATUS_RX_COUNT_SHIFT) >= Count)
                break;
            if (Elapsed >= K1X_DDC_TIMEOUT_US)
                return STATUS_IO_TIMEOUT;
            KeStallExecutionProcessor(K1X_DDC_POLL_US);
        }
        for (Index = 0; Index < Count; ++Index)
            Buffer[Done + Index] = (UCHAR)K1xRead(Hdmi, K1X_HDMI_DDC_RX);
        Result = K1xDdcFinish(Hdmi);
        if (!NT_SUCCESS(Result))
            return Result;
        Done += Count;
    }
    return STATUS_SUCCESS;
}

static BOOLEAN
K1xEdidBlockValid(_In_reads_bytes_(K1X_EDID_BLOCK) const UCHAR *Block, _In_ BOOLEAN Base)
{
    static const UCHAR Header[8] = { 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00 };
    UCHAR Sum = 0;
    ULONG Index;

    if (Base && !RtlEqualMemory(Block, Header, sizeof(Header)))
        return FALSE;
    for (Index = 0; Index < K1X_EDID_BLOCK; ++Index)
        Sum = (UCHAR)(Sum + Block[Index]);
    return Sum == 0;
}

static VOID
K1xReadEdid(_Inout_ PK1XDPU_CONTEXT Context)
{
    const UCHAR *Timing;
    NTSTATUS Status;

    Context->EdidLength = 0;
    Status = K1xDdcReadEdid(Context->Hdmi, 0, Context->Edid, K1X_EDID_BLOCK);
    if (!NT_SUCCESS(Status) || !K1xEdidBlockValid(Context->Edid, TRUE))
    {
        DPRINT1("K1XDPU: EDID unavailable (0x%08lx)\n", Status);
        return;
    }
    Context->EdidLength = K1X_EDID_BLOCK;
    if (Context->Edid[126] &&
        NT_SUCCESS(K1xDdcReadEdid(Context->Hdmi, K1X_EDID_BLOCK, Context->Edid + K1X_EDID_BLOCK, K1X_EDID_BLOCK)) &&
        K1xEdidBlockValid(Context->Edid + K1X_EDID_BLOCK, FALSE))
    {
        Context->EdidLength = K1X_EDID_MAX;
    }

    Timing = Context->Edid + 54;
    DPRINT1("K1XDPU: EDID %c%c%c%04x, %lu block(s), preferred %ux%u at %lu kHz\n",
            ((Context->Edid[8] >> 2) & 0x1F) + '@',
            (((Context->Edid[8] & 3) << 3) | (Context->Edid[9] >> 5)) + '@',
            (Context->Edid[9] & 0x1F) + '@',
            Context->Edid[10] | (Context->Edid[11] << 8),
            Context->EdidLength / K1X_EDID_BLOCK,
            Timing[2] | ((Timing[4] & 0xF0) << 4),
            Timing[5] | ((Timing[7] & 0xF0) << 4),
            (ULONG)(Timing[0] | (Timing[1] << 8)) * 10);
}

static ULONG
K1xReadPowerStatus(VOID)
{
    PHYSICAL_ADDRESS Address;
    PUCHAR Apmu;
    ULONG Status;

    Address.QuadPart = K1X_APMU_BASE;
    Apmu = MmMapIoSpace(Address, K1X_APMU_SIZE, MmNonCached);
    if (!Apmu)
        return 0;
    Status = K1xRead(Apmu, K1X_APMU_POWER_STATUS);
    MmUnmapIoSpace(Apmu, K1X_APMU_SIZE);
    return Status;
}

ULONG
K1xFindName(
    _In_ PDEVICE_OBJECT PhysicalDeviceObject,
    _In_ PCWSTR Property,
    _In_z_ const CHAR *Wanted)
{
    UCHAR Storage[FIELD_OFFSET(KEY_VALUE_PARTIAL_INFORMATION, Data) + 64];
    PKEY_VALUE_PARTIAL_INFORMATION Information = (PKEY_VALUE_PARTIAL_INFORMATION)Storage;
    ULONG Result, Offset = 0, Index = 0, Found = MAXULONG;
    SIZE_T WantedLength = strlen(Wanted);
    UNICODE_STRING Name;
    HANDLE Key;

    RtlInitUnicodeString(&Name, Property);
    if (!NT_SUCCESS(IoOpenDeviceRegistryKey(PhysicalDeviceObject, PLUGPLAY_REGKEY_DEVICE, KEY_READ, &Key)))
        return MAXULONG;
    if (NT_SUCCESS(ZwQueryValueKey(Key, &Name, KeyValuePartialInformation, Information, sizeof(Storage), &Result)))
    {
        while (Offset < Information->DataLength)
        {
            const UCHAR *Entry = Information->Data + Offset;
            ULONG Length = 0;

            while (Offset + Length < Information->DataLength && Entry[Length])
                Length++;
            if (Length == WantedLength && RtlEqualMemory(Entry, Wanted, Length))
            {
                Found = Index;
                break;
            }
            Offset += Length + 1;
            Index++;
        }
    }
    ZwClose(Key);
    return Found;
}

PCM_PARTIAL_RESOURCE_DESCRIPTOR
K1xFindResource(
    _In_ const DXGK_DEVICE_INFO *Information,
    _In_ UCHAR Type,
    _In_ ULONG Ordinal)
{
    PCM_PARTIAL_RESOURCE_LIST List;
    ULONG Index, Seen = 0;

    if (!Information->TranslatedResourceList || !Information->TranslatedResourceList->Count)
        return NULL;
    List = &Information->TranslatedResourceList->List[0].PartialResourceList;
    for (Index = 0; Index < List->Count; ++Index)
    {
        if (List->PartialDescriptors[Index].Type != Type)
            continue;
        if (Seen++ == Ordinal)
            return &List->PartialDescriptors[Index];
    }
    return NULL;
}

static PUCHAR
K1xMapDpu(
    _In_ const DXGK_DEVICE_INFO *Information,
    _In_z_ const CHAR *Output)
{
    PCM_PARTIAL_RESOURCE_DESCRIPTOR Dpu;

    Dpu = K1xFindResource(Information, CmResourceTypeMemory,
                          K1xFindName(Information->PhysicalDeviceObject, L"reg-names", Output));
    if (!Dpu || Dpu->u.Memory.Length < K1X_DPU_SIZE)
        return NULL;
    return MmMapIoSpace(Dpu->u.Memory.Start, K1X_DPU_SIZE, MmNonCached);
}

static BOOLEAN
K1xPollVsync(_In_ PUCHAR Dpu)
{
    ULONG Elapsed;

    K1xWrite(Dpu, K1X_DPU_INT_ONLINE2_STATUS, K1X_DPU_INT_VSYNC);
    for (Elapsed = 0; Elapsed < K1X_VSYNC_TIMEOUT_MS * 1000; Elapsed += K1X_DDC_POLL_US)
    {
        if (K1xRead(Dpu, K1X_DPU_INT_ONLINE2_RAW) & K1X_DPU_INT_VSYNC)
            return TRUE;
        KeStallExecutionProcessor(K1X_DDC_POLL_US);
    }
    return FALSE;
}

NTSTATUS
SoftGpuPlatformValidatePdo(
    _In_ PDEVICE_OBJECT PhysicalDeviceObject)
{
    return PhysicalDeviceObject != NULL ? STATUS_SUCCESS : STATUS_INVALID_PARAMETER;
}

NTSTATUS
SoftGpuPlatformQueryStart(
    _In_ PSOFTGPU_DEVICE Device,
    _In_ PDXGK_INTERFACE DxgkInterface,
    _Out_ PSOFTGPU_PLATFORM_CONFIG Config)
{
    DXGK_DISPLAY_INFORMATION PostDisplayInfo;
    DXGK_DEVICE_INFO Information;
    ULONGLONG PostVisibleLength;
    PHYSICAL_ADDRESS Address;
    PK1XDPU_CONTEXT Context;
    SIZE_T WorkingSetSize;
    ULONG PowerStatus;
    NTSTATUS Status;

    if (Device == NULL || DxgkInterface == NULL || Config == NULL)
        return STATUS_INVALID_PARAMETER;

    RtlZeroMemory(Config, sizeof(*Config));
    Config->Width = SOFTGPU_DEFAULT_WIDTH;
    Config->Height = SOFTGPU_DEFAULT_HEIGHT;
    Config->Format = SOFTGPU_DEFAULT_FORMAT;

    Status = SoftGpuAcquirePostDisplay(DxgkInterface, &PostDisplayInfo, &PostVisibleLength);
    if (!NT_SUCCESS(Status))
        return Status;
    if (PostDisplayInfo.Width != 0)
    {
        Config->Width = PostDisplayInfo.Width;
        Config->Height = PostDisplayInfo.Height;
        Config->Format = PostDisplayInfo.ColorFormat;
        Config->ScanoutPhysicalAddress = PostDisplayInfo.PhysicAddress;
        Config->ScanoutPitch = PostDisplayInfo.Pitch;
        Config->ScanoutSize = PostVisibleLength;
    }

    Status = SoftGpu2dComputeAllocationSlabSize(Config->Width,
                                                Config->Height,
                                                K1XDPU_WORKING_SURFACE_COUNT,
                                                MAXULONG_PTR,
                                                &WorkingSetSize);
    if (!NT_SUCCESS(Status))
        return Status;
    Config->MinimumAllocationSlabSize = min(WorkingSetSize, SOFTGPU_MAX_ALLOCATION_SLAB_SIZE);

    if (Device->PlatformContext)
        return STATUS_SUCCESS;
    Context = ExAllocatePoolZero(NonPagedPool, sizeof(*Context), K1XDPU_TAG);
    if (!Context)
        return STATUS_INSUFFICIENT_RESOURCES;
    Context->Device = Device;
    ExInitializeFastMutex(&Context->GpuMutex);
    RtlCopyMemory(&Context->Dxgk, DxgkInterface, min((SIZE_T)DxgkInterface->Size, sizeof(Context->Dxgk)));

    PowerStatus = K1xReadPowerStatus();
    if (PowerStatus & K1X_APMU_POWER_HDMI)
        Context->Output = "hdmi";
    else if (PowerStatus & K1X_APMU_POWER_LCD)
        Context->Output = "dsi";
    if (Context->Output && DxgkInterface->DxgkCbGetDeviceInformation &&
        NT_SUCCESS(DxgkInterface->DxgkCbGetDeviceInformation(DxgkInterface->DeviceHandle, &Information)) &&
        Information.PhysicalDeviceObject)
    {
        Context->Dpu = K1xMapDpu(&Information, Context->Output);
    }
    if (PowerStatus & K1X_APMU_POWER_HDMI)
    {
        Address.QuadPart = K1X_HDMI_BASE;
        Context->Hdmi = MmMapIoSpace(Address, K1X_HDMI_SIZE, MmNonCached);
    }
    if (Context->Hdmi)
        K1xReadEdid(Context);
    Device->PlatformContext = Context;
    return STATUS_SUCCESS;
}

ULONG
SoftGpuPlatformDmaBufferPrivateDataSize(VOID)
{
    return 0;
}

ULONG
SoftGpuPlatformDmaBufferSegmentSet(VOID)
{
    return 0;
}

NTSTATUS
SoftGpuPlatformStartScanout(
    _Inout_ PSOFTGPU_DEVICE Device)
{
    PK1XDPU_CONTEXT Context;
    DXGK_DEVICE_INFO Information;

    /* FIXME: program the DPU (RDMA layer, compositor, output timing) and the
     * HDMI PHY/PLL for the committed mode instead of reusing U-Boot's setup. */
    if (Device == NULL || Device->PlatformContext == NULL)
        return STATUS_NOT_SUPPORTED;
    Context = Device->PlatformContext;
    if (!Context->Dxgk.DxgkCbGetDeviceInformation ||
        !NT_SUCCESS(Context->Dxgk.DxgkCbGetDeviceInformation(Context->Dxgk.DeviceHandle, &Information)) ||
        !Information.PhysicalDeviceObject)
    {
        return STATUS_NOT_SUPPORTED;
    }
    if (Context->Dpu && NT_SUCCESS(K1xPlanesStart(Context, &Information, Context->Output)))
        Device->PlatformHardwarePointer = TRUE;
    (VOID)K1xGpuStart(Context, &Information);
    return STATUS_NOT_SUPPORTED;
}

NTSTATUS
SoftGpuPlatformStopScanout(
    _Inout_ PSOFTGPU_DEVICE Device)
{
    PK1XDPU_CONTEXT Context;

    if (Device == NULL || Device->PlatformContext == NULL)
        return STATUS_SUCCESS;
    Context = Device->PlatformContext;
    InterlockedExchange(&Device->ScanoutVBlankAvailable, 0);
    K1xPlanesStop(Context);
    Device->PlatformHardwarePointer = FALSE;
    K1xGpuStop(Context);
    Device->PlatformContext = NULL;
    if (Context->Dpu)
        MmUnmapIoSpace(Context->Dpu, K1X_DPU_SIZE);
    if (Context->Hdmi)
        MmUnmapIoSpace(Context->Hdmi, K1X_HDMI_SIZE);
    ExFreePoolWithTag(Context, K1XDPU_TAG);
    return STATUS_SUCCESS;
}

NTSTATUS
SoftGpuPlatformSetPrimary(
    _Inout_ PSOFTGPU_DEVICE Device,
    _In_ PHYSICAL_ADDRESS PrimaryAddress,
    _In_ ULONG Pitch,
    _In_ ULONG Width,
    _In_ ULONG Height,
    _In_ BOOLEAN Visible)
{
    /* FIXME: direct scanout needs the DPU layer address and cache maintenance
     * for the non-coherent DPU reads. */
    UNREFERENCED_PARAMETER(Device);
    UNREFERENCED_PARAMETER(PrimaryAddress);
    UNREFERENCED_PARAMETER(Pitch);
    UNREFERENCED_PARAMETER(Width);
    UNREFERENCED_PARAMETER(Height);
    UNREFERENCED_PARAMETER(Visible);
    return STATUS_NOT_SUPPORTED;
}

NTSTATUS
SoftGpuPlatformPresentDisplayOnly(
    _Inout_ PSOFTGPU_DEVICE Device,
    _In_ const DXGKARG_PRESENT_DISPLAYONLY *PresentDisplayOnly)
{
    UNREFERENCED_PARAMETER(Device);
    UNREFERENCED_PARAMETER(PresentDisplayOnly);
    return STATUS_NOT_SUPPORTED;
}

NTSTATUS
SoftGpuPlatformUpdatePointer(
    _Inout_ PSOFTGPU_DEVICE Device)
{
    if (Device == NULL || Device->PlatformContext == NULL)
        return STATUS_DEVICE_NOT_READY;
    return K1xPlanesUpdatePointer(Device->PlatformContext);
}

VOID
SoftGpuPlatformFillNodeMetadata(
    _In_ ULONG NodeOrdinal,
    _Out_ DXGKARG_GETNODEMETADATA *GetNodeMetadata)
{
    static const WCHAR FriendlyName[] = L"3D";

    UNREFERENCED_PARAMETER(NodeOrdinal);
    GetNodeMetadata->EngineType = DXGK_ENGINE_TYPE_3D;
    RtlCopyMemory(GetNodeMetadata->FriendlyName, FriendlyName, sizeof(FriendlyName));
}

VOID
SoftGpuPlatformInitializeTiming(
    _Inout_ PSOFTGPU_DEVICE Device)
{
    PK1XDPU_CONTEXT Context;
    LARGE_INTEGER Start, End, Frequency;
    ULONGLONG PeriodUs;

    if (Device == NULL)
        return;
    InterlockedExchange(&Device->ScanoutVBlankAvailable, 0);
    Context = Device->PlatformContext;
    if (!Context || !Context->Dpu || Context->PlanesReady || !K1xPollVsync(Context->Dpu))
        return;
    Start = KeQueryPerformanceCounter(&Frequency);
    if (!K1xPollVsync(Context->Dpu))
        return;
    End = KeQueryPerformanceCounter(NULL);
    PeriodUs = (ULONGLONG)(End.QuadPart - Start.QuadPart) * 1000000ULL / (ULONGLONG)Frequency.QuadPart;
    DPRINT1("K1XDPU: vertical blank every %I64u us\n", PeriodUs);
    if (PeriodUs >= 5000 && PeriodUs <= K1X_VSYNC_TIMEOUT_MS * 1000)
        InterlockedExchange(&Device->ScanoutVBlankAvailable, 1);
}

BOOLEAN
SoftGpuPlatformWaitForVerticalBlank(
    _Inout_ PSOFTGPU_DEVICE Device)
{
    PK1XDPU_CONTEXT Context;

    if (Device == NULL || InterlockedCompareExchange(&Device->ScanoutVBlankAvailable, 0, 0) == 0)
        return FALSE;
    Context = Device->PlatformContext;
    if (Context && K1xPlanesWaitVsync(Context))
        return TRUE;
    InterlockedExchange(&Device->ScanoutVBlankAvailable, 0);
    return FALSE;
}

NTSTATUS
SoftGpuPlatformQueryScanLine(
    _In_ PSOFTGPU_DEVICE Device,
    _Inout_ PDXGKARG_GETSCANLINE GetScanLine)
{
    if (Device == NULL || GetScanLine == NULL || GetScanLine->VidPnTargetId != 0)
        return STATUS_INVALID_PARAMETER;
    return STATUS_NOT_SUPPORTED;
}

MEMORY_CACHING_TYPE
SoftGpuPlatformSegmentCacheType(VOID)
{
    return MmCached;
}

NTSTATUS
SoftGpuPlatformQueryDescriptor(
    _In_ PSOFTGPU_DEVICE Device,
    _Inout_ PDXGK_DEVICE_DESCRIPTOR DeviceDescriptor)
{
    PK1XDPU_CONTEXT Context = Device->PlatformContext;
    ULONG Length;

    if (!Context || !Context->EdidLength)
        return STATUS_MONITOR_NO_DESCRIPTOR;
    if (DeviceDescriptor->DescriptorOffset >= Context->EdidLength)
        return STATUS_MONITOR_NO_MORE_DESCRIPTOR_DATA;
    if (!DeviceDescriptor->DescriptorBuffer)
        return STATUS_INVALID_PARAMETER;
    Length = min(DeviceDescriptor->DescriptorLength, Context->EdidLength - DeviceDescriptor->DescriptorOffset);
    RtlCopyMemory(DeviceDescriptor->DescriptorBuffer, Context->Edid + DeviceDescriptor->DescriptorOffset, Length);
    DeviceDescriptor->DescriptorLength = Length;
    return STATUS_SUCCESS;
}

NTSTATUS
SoftGpuPlatformEscape(
    _Inout_ PSOFTGPU_DEVICE Device,
    _In_ CONST DXGKARG_ESCAPE *Escape)
{
    if (Device == NULL || Escape == NULL || Device->PlatformContext == NULL)
        return STATUS_NOT_SUPPORTED;
    return K1xGpuEscape(Device->PlatformContext, Escape);
}

VOID
SoftGpuPlatformDestroyDevice(
    _Inout_ PSOFTGPU_KMD_DEVICE KmdDevice)
{
    PK1X_GPU_DEVICE GpuDevice = KmdDevice->PlatformDevice;
    PK1XDPU_CONTEXT Context = KmdDevice->Adapter->PlatformContext;

    if (!GpuDevice)
        return;
    KmdDevice->PlatformDevice = NULL;
    if (Context)
        K1xOverlayHide(Context, KmdDevice);
    pvr_glue_close(GpuDevice->File);
    GpuDevice->Magic = 0;
    ExFreePoolWithTag(GpuDevice, K1XDPU_TAG);
}

NTSTATUS
SoftGpuPlatformOpenAllocation(
    _Inout_ PSOFTGPU_OPENALLOC OpenAllocation)
{
    UNREFERENCED_PARAMETER(OpenAllocation);
    return STATUS_SUCCESS;
}

VOID
SoftGpuPlatformCloseAllocation(
    _Inout_ PSOFTGPU_OPENALLOC OpenAllocation)
{
    UNREFERENCED_PARAMETER(OpenAllocation);
}

NTSTATUS
SoftGpuPlatformRender(
    _Inout_ PSOFTGPU_DEVICE Device,
    _In_ PSOFTGPU_KMD_DEVICE KmdDevice,
    _Inout_ PDXGKARG_RENDER Render)
{
    UNREFERENCED_PARAMETER(Device);
    UNREFERENCED_PARAMETER(KmdDevice);
    UNREFERENCED_PARAMETER(Render);
    return STATUS_NOT_SUPPORTED;
}

NTSTATUS
SoftGpuPlatformSubmitCommand(
    _Inout_ PSOFTGPU_DEVICE Device,
    _In_ const DXGKARG_SUBMITCOMMAND *SubmitCommand)
{
    UNREFERENCED_PARAMETER(Device);
    UNREFERENCED_PARAMETER(SubmitCommand);
    return STATUS_NOT_SUPPORTED;
}

BOOLEAN
SoftGpuPlatformInterruptRoutine(
    _Inout_ PSOFTGPU_DEVICE Device)
{
    PK1XDPU_CONTEXT Context = Device->PlatformContext;
    int QueueDpc = 0;
    BOOLEAN Handled;

    if (!Context || !Context->GpuCore)
        return FALSE;
    Handled = pvr_glue_isr(Context->GpuCore, &QueueDpc) != 0;
    if (QueueDpc)
        Context->Dxgk.DxgkCbQueueDpc(Context->Dxgk.DeviceHandle);
    return Handled;
}

VOID
SoftGpuPlatformDpcRoutine(
    _Inout_ PSOFTGPU_DEVICE Device)
{
    PK1XDPU_CONTEXT Context = Device->PlatformContext;

    if (Context && Context->GpuCore)
        pvr_glue_dpc(Context->GpuCore);
}
