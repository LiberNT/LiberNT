/*
 * PROJECT:     LiberNT QEMU Standard VGA Driver
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Legacy-free QEMU standard VGA platform provider for the software GPU engine
 * COPYRIGHT:   Copyright 2026 Ahmed Arif <arif193@gmail.com>
 */

#include "softgpu.h"
#include "softgpu_2d_core.h"

#define QVGA_TAG                    'agVQ'

#define QVGA_MMIO_EDID              0x000
#define QVGA_MMIO_VGA_PORTS         0x400
#define QVGA_MMIO_DISPI             0x500
#define QVGA_MMIO_MINIMUM_SIZE      0x600
#define QVGA_VGA_PORT_BASE          0x3C0
#define QVGA_VGA_ATTRIBUTE          0x3C0
#define QVGA_VGA_INPUT_STATUS1      0x3DA
#define QVGA_AR_PALETTE_SOURCE      0x20

#define VBE_DISPI_INDEX_ID              0x00
#define VBE_DISPI_ID0                   0xB0C0
#define VBE_DISPI_ID5                   0xB0C5
#define VBE_DISPI_INDEX_XRES            0x01
#define VBE_DISPI_INDEX_YRES            0x02
#define VBE_DISPI_INDEX_BPP             0x03
#define VBE_DISPI_INDEX_ENABLE          0x04
#define VBE_DISPI_DISABLED              0x00
#define VBE_DISPI_ENABLED               0x01
#define VBE_DISPI_GETCAPS               0x02
#define VBE_DISPI_LFB_ENABLED           0x40
#define VBE_DISPI_INDEX_VIDEO_MEMORY_64K 0x0A

#define QVGA_BITS_PER_PIXEL         32
#define QVGA_DEFAULT_WIDTH          1024
#define QVGA_DEFAULT_HEIGHT         768
#define QVGA_MAX_WIDTH              1920
#define QVGA_MAX_HEIGHT             1200

typedef struct _QVGA_CONTEXT
{
    PUCHAR Mmio;
    SIZE_T MmioLength;
    PHYSICAL_ADDRESS FrameBuffer;
    ULONGLONG FrameBufferLength;
    ULONG MaxWidth;
    ULONG MaxHeight;
} QVGA_CONTEXT, *PQVGA_CONTEXT;

static const USHORT QVgaStandardModes[][2] =
{
    { 640, 480 }, { 800, 600 }, { 1024, 768 }, { 1152, 864 }, { 1280, 720 },
    { 1280, 800 }, { 1280, 1024 }, { 1366, 768 }, { 1440, 900 }, { 1600, 900 },
    { 1600, 1200 }, { 1680, 1050 }, { 1920, 1080 }, { 1920, 1200 },
};

static
VOID
QVgaWriteDispi(
    _In_ PQVGA_CONTEXT Context,
    _In_ USHORT Index,
    _In_ USHORT Value)
{
    WRITE_REGISTER_USHORT((PUSHORT)(Context->Mmio + QVGA_MMIO_DISPI + Index * sizeof(USHORT)), Value);
}

static
USHORT
QVgaReadDispi(
    _In_ PQVGA_CONTEXT Context,
    _In_ USHORT Index)
{
    return READ_REGISTER_USHORT((PUSHORT)(Context->Mmio + QVGA_MMIO_DISPI + Index * sizeof(USHORT)));
}

static
VOID
QVgaSetBlank(
    _In_ PQVGA_CONTEXT Context,
    _In_ BOOLEAN Blank)
{
    (VOID)READ_REGISTER_UCHAR(Context->Mmio + QVGA_MMIO_VGA_PORTS + (QVGA_VGA_INPUT_STATUS1 - QVGA_VGA_PORT_BASE));
    WRITE_REGISTER_UCHAR(Context->Mmio + QVGA_MMIO_VGA_PORTS + (QVGA_VGA_ATTRIBUTE - QVGA_VGA_PORT_BASE),
                         Blank ? 0 : QVGA_AR_PALETTE_SOURCE);
}

static
NTSTATUS
QVgaProgramMode(
    _In_ PQVGA_CONTEXT Context,
    _In_ ULONG Width,
    _In_ ULONG Height)
{
    ASSERT(Context->Mmio != NULL);
    ASSERT(Width != 0 && Width <= Context->MaxWidth);
    ASSERT(Height != 0 && Height <= Context->MaxHeight);
    ASSERT((ULONGLONG)Width * SOFTGPU_DISPLAY_BYTES_PER_PIXEL * Height <= Context->FrameBufferLength);

    QVgaWriteDispi(Context, VBE_DISPI_INDEX_ENABLE, VBE_DISPI_DISABLED);
    QVgaWriteDispi(Context, VBE_DISPI_INDEX_XRES, (USHORT)Width);
    QVgaWriteDispi(Context, VBE_DISPI_INDEX_YRES, (USHORT)Height);
    QVgaWriteDispi(Context, VBE_DISPI_INDEX_BPP, QVGA_BITS_PER_PIXEL);
    QVgaWriteDispi(Context, VBE_DISPI_INDEX_ENABLE, VBE_DISPI_ENABLED | VBE_DISPI_LFB_ENABLED);
    if (QVgaReadDispi(Context, VBE_DISPI_INDEX_XRES) != Width ||
        QVgaReadDispi(Context, VBE_DISPI_INDEX_YRES) != Height ||
        QVgaReadDispi(Context, VBE_DISPI_INDEX_BPP) != QVGA_BITS_PER_PIXEL)
    {
        DPRINT1("QEMUVGA: the device refused %lux%lu\n", Width, Height);
        return STATUS_GRAPHICS_INVALID_VIDEO_PRESENT_SOURCE_MODE;
    }

    QVgaSetBlank(Context, FALSE);
    return STATUS_SUCCESS;
}

static
NTSTATUS
QVgaSetMode(
    _Inout_ PSOFTGPU_DEVICE Device,
    _In_ const SOFTGPU_DISPLAY_MODE *Mode)
{
    PAGED_CODE();

    if (Device == NULL || Device->PlatformContext == NULL || Mode == NULL)
        return STATUS_INVALID_PARAMETER;
    ASSERT(Mode->Pitch == Mode->Width * SOFTGPU_DISPLAY_BYTES_PER_PIXEL);

    return QVgaProgramMode(Device->PlatformContext, Mode->Width, Mode->Height);
}

static
NTSTATUS
QVgaFindResources(
    _In_ PDXGK_INTERFACE DxgkInterface,
    _Inout_ PQVGA_CONTEXT Context,
    _Out_ PPHYSICAL_ADDRESS Mmio)
{
    PCM_PARTIAL_RESOURCE_DESCRIPTOR Descriptor;
    PCM_FULL_RESOURCE_DESCRIPTOR Full;
    DXGK_DEVICE_INFO DeviceInfo;
    ULONG FullIndex;
    ULONG Index;

    Mmio->QuadPart = 0;
    RtlZeroMemory(&DeviceInfo, sizeof(DeviceInfo));
    if (DxgkInterface->DxgkCbGetDeviceInformation == NULL ||
        !NT_SUCCESS(DxgkInterface->DxgkCbGetDeviceInformation(DxgkInterface->DeviceHandle, &DeviceInfo)) ||
        DeviceInfo.TranslatedResourceList == NULL)
    {
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    }

    Full = DeviceInfo.TranslatedResourceList->List;
    for (FullIndex = 0; FullIndex < DeviceInfo.TranslatedResourceList->Count; FullIndex++)
    {
        for (Index = 0; Index < Full->PartialResourceList.Count; Index++)
        {
            Descriptor = &Full->PartialResourceList.PartialDescriptors[Index];
            if (Descriptor->Type != CmResourceTypeMemory)
                continue;

            if (Descriptor->u.Memory.Length >= QVGA_MMIO_MINIMUM_SIZE &&
                Descriptor->u.Memory.Length < PAGE_SIZE * 16)
            {
                if (Mmio->QuadPart == 0)
                {
                    *Mmio = Descriptor->u.Memory.Start;
                    Context->MmioLength = Descriptor->u.Memory.Length;
                }
            }
            else if (Descriptor->u.Memory.Length > Context->FrameBufferLength)
            {
                Context->FrameBuffer = Descriptor->u.Memory.Start;
                Context->FrameBufferLength = Descriptor->u.Memory.Length;
            }
        }
        Full = (PCM_FULL_RESOURCE_DESCRIPTOR)&Full->PartialResourceList.PartialDescriptors[Full->PartialResourceList.Count];
    }

    if (Mmio->QuadPart == 0 || Context->FrameBufferLength == 0)
    {
        DPRINT1("QEMUVGA: no framebuffer or register BAR\n");
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    }
    return STATUS_SUCCESS;
}

static
BOOLEAN
QVgaReadEdid(
    _In_ PQVGA_CONTEXT Context,
    _Inout_ PSOFTGPU_PLATFORM_CONFIG Config,
    _Out_ PULONG PreferredWidth,
    _Out_ PULONG PreferredHeight)
{
    static const UCHAR Header[8] = { 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00 };
    const UCHAR *Timing;
    UCHAR Sum = 0;
    ULONG Index;

    *PreferredWidth = 0;
    *PreferredHeight = 0;
    READ_REGISTER_BUFFER_UCHAR(Context->Mmio + QVGA_MMIO_EDID, Config->Edid, SOFTGPU_EDID_SIZE);
    for (Index = 0; Index < SOFTGPU_EDID_SIZE; Index++)
        Sum += Config->Edid[Index];
    if (Sum != 0 || !RtlEqualMemory(Config->Edid, Header, sizeof(Header)))
    {
        RtlZeroMemory(Config->Edid, SOFTGPU_EDID_SIZE);
        return FALSE;
    }

    Config->EdidLength = SOFTGPU_EDID_SIZE;
    Timing = Config->Edid + 54;
    if (Timing[0] == 0 && Timing[1] == 0)
        return TRUE;

    *PreferredWidth = Timing[2] | ((ULONG)(Timing[4] & 0xF0) << 4);
    *PreferredHeight = Timing[5] | ((ULONG)(Timing[7] & 0xF0) << 4);
    return TRUE;
}

static
BOOLEAN
QVgaModeFits(
    _In_ PQVGA_CONTEXT Context,
    _In_ ULONG Width,
    _In_ ULONG Height)
{
    return Width >= 640 && Height >= 480 &&
           Width <= Context->MaxWidth && Height <= Context->MaxHeight &&
           (ULONGLONG)Width * SOFTGPU_DISPLAY_BYTES_PER_PIXEL * Height <= Context->FrameBufferLength;
}

static
VOID
QVgaAddMode(
    _Inout_ PSOFTGPU_PLATFORM_CONFIG Config,
    _In_ ULONG Width,
    _In_ ULONG Height)
{
    ULONG Index;

    for (Index = 0; Index < Config->ModeCount; Index++)
    {
        if (Config->Modes[Index].Width == Width && Config->Modes[Index].Height == Height)
            return;
    }
    if (Config->ModeCount >= SOFTGPU_MAX_DISPLAY_MODES)
        return;

    Config->Modes[Config->ModeCount].Width = Width;
    Config->Modes[Config->ModeCount].Height = Height;
    Config->Modes[Config->ModeCount].Pitch = Width * SOFTGPU_DISPLAY_BYTES_PER_PIXEL;
    Config->Modes[Config->ModeCount].PlatformMode = Config->ModeCount;
    Config->ModeCount++;
}

static
VOID
QVgaRelease(
    _In_ PQVGA_CONTEXT Context)
{
    if (Context->Mmio != NULL)
        MmUnmapIoSpace(Context->Mmio, Context->MmioLength);
    ExFreePoolWithTag(Context, QVGA_TAG);
}

static
NTSTATUS
QVgaOpen(
    _In_ PDXGK_INTERFACE DxgkInterface,
    _Out_ PQVGA_CONTEXT *Opened)
{
    PHYSICAL_ADDRESS Mmio;
    PQVGA_CONTEXT Context;
    ULONGLONG VideoMemory;
    USHORT Id;
    USHORT Enable;
    NTSTATUS Status;

    *Opened = NULL;
    Context = ExAllocatePoolZero(NonPagedPool, sizeof(*Context), QVGA_TAG);
    if (Context == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;

    Status = QVgaFindResources(DxgkInterface, Context, &Mmio);
    if (NT_SUCCESS(Status))
    {
        Context->Mmio = MmMapIoSpace(Mmio, Context->MmioLength, MmNonCached);
        if (Context->Mmio == NULL)
            Status = STATUS_INSUFFICIENT_RESOURCES;
    }
    if (!NT_SUCCESS(Status))
    {
        QVgaRelease(Context);
        return Status;
    }

    Id = QVgaReadDispi(Context, VBE_DISPI_INDEX_ID);
    if (Id < VBE_DISPI_ID0 || Id > VBE_DISPI_ID5)
    {
        DPRINT1("QEMUVGA: unknown register interface 0x%04x\n", Id);
        QVgaRelease(Context);
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    }

    Enable = QVgaReadDispi(Context, VBE_DISPI_INDEX_ENABLE);
    QVgaWriteDispi(Context, VBE_DISPI_INDEX_ENABLE, Enable | VBE_DISPI_GETCAPS);
    Context->MaxWidth = min((ULONG)QVgaReadDispi(Context, VBE_DISPI_INDEX_XRES), (ULONG)QVGA_MAX_WIDTH);
    Context->MaxHeight = min((ULONG)QVgaReadDispi(Context, VBE_DISPI_INDEX_YRES), (ULONG)QVGA_MAX_HEIGHT);
    QVgaWriteDispi(Context, VBE_DISPI_INDEX_ENABLE, Enable);

    VideoMemory = (ULONGLONG)QVgaReadDispi(Context, VBE_DISPI_INDEX_VIDEO_MEMORY_64K) * 0x10000;
    if (VideoMemory != 0)
        Context->FrameBufferLength = min(Context->FrameBufferLength, VideoMemory);

    DPRINT1("QEMUVGA: interface 0x%04x, %I64u KB at 0x%I64x, up to %lux%lu\n",
            Id, Context->FrameBufferLength / 1024, Context->FrameBuffer.QuadPart,
            Context->MaxWidth, Context->MaxHeight);
    *Opened = Context;
    return STATUS_SUCCESS;
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
    PQVGA_CONTEXT Context;
    SIZE_T SlabSize;
    ULONG PreferredWidth;
    ULONG PreferredHeight;
    ULONG Index;
    NTSTATUS Status;

    PAGED_CODE();

    if (Device == NULL || DxgkInterface == NULL || Config == NULL)
        return STATUS_INVALID_PARAMETER;

    RtlZeroMemory(Config, sizeof(*Config));
    Context = Device->PlatformContext;
    if (Context == NULL)
    {
        Status = QVgaOpen(DxgkInterface, &Context);
        if (!NT_SUCCESS(Status))
            return Status;
        Device->PlatformContext = Context;
    }

    QVgaReadEdid(Context, Config, &PreferredWidth, &PreferredHeight);
    if (!QVgaModeFits(Context, PreferredWidth, PreferredHeight))
    {
        PreferredWidth = QVGA_DEFAULT_WIDTH;
        PreferredHeight = QVGA_DEFAULT_HEIGHT;
    }
    if (!QVgaModeFits(Context, PreferredWidth, PreferredHeight))
        return STATUS_DEVICE_CONFIGURATION_ERROR;

    QVgaAddMode(Config, PreferredWidth, PreferredHeight);
    for (Index = 0; Index < RTL_NUMBER_OF(QVgaStandardModes); Index++)
    {
        if (QVgaModeFits(Context, QVgaStandardModes[Index][0], QVgaStandardModes[Index][1]))
            QVgaAddMode(Config, QVgaStandardModes[Index][0], QVgaStandardModes[Index][1]);
    }
    ASSERT(Config->ModeCount != 0);
    ASSERT(Config->Modes[0].Width == PreferredWidth);
    ASSERT(Config->Modes[0].Height == PreferredHeight);

    Status = QVgaProgramMode(Context, PreferredWidth, PreferredHeight);
    if (!NT_SUCCESS(Status))
        return Status;

    Config->Width = PreferredWidth;
    Config->Height = PreferredHeight;
    Config->Format = D3DDDIFMT_X8R8G8B8;
    Config->ScanoutPhysicalAddress = Context->FrameBuffer;
    Config->ScanoutPitch = PreferredWidth * SOFTGPU_DISPLAY_BYTES_PER_PIXEL;
    Config->ScanoutSize = Context->FrameBufferLength;
    Config->CurrentMode = 0;
    Config->SetMode = QVgaSetMode;

    for (Index = 0; Index < Config->ModeCount; Index++)
    {
        Status = SoftGpu2dComputeAllocationSlabSize(Config->Modes[Index].Width, Config->Modes[Index].Height,
                                                    SOFTGPU_2D_WORKING_SURFACE_COUNT, MAXULONG_PTR, &SlabSize);
        if (!NT_SUCCESS(Status))
            return Status;
        Config->Modes[Index].AllocationSlabSize = min(SlabSize, SOFTGPU_MAX_ALLOCATION_SLAB_SIZE);
    }
    Config->MinimumAllocationSlabSize = Config->Modes[0].AllocationSlabSize;
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
    UNREFERENCED_PARAMETER(Device);
    return STATUS_NOT_SUPPORTED;
}

NTSTATUS
SoftGpuPlatformStopScanout(
    _Inout_ PSOFTGPU_DEVICE Device)
{
    PQVGA_CONTEXT Context;

    if (Device == NULL || Device->PlatformContext == NULL)
        return STATUS_SUCCESS;

    Context = Device->PlatformContext;
    Device->PlatformContext = NULL;
    QVgaSetBlank(Context, TRUE);
    QVgaRelease(Context);
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
    UNREFERENCED_PARAMETER(Device);
    return STATUS_NOT_SUPPORTED;
}

VOID
SoftGpuPlatformScanoutWritten(
    _Inout_ PSOFTGPU_DEVICE Device,
    _In_ const RECT *Rect)
{
    UNREFERENCED_PARAMETER(Device);
    UNREFERENCED_PARAMETER(Rect);
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
    if (Device != NULL)
        InterlockedExchange(&Device->ScanoutVBlankAvailable, 0);
}

BOOLEAN
SoftGpuPlatformWaitForVerticalBlank(
    _Inout_ PSOFTGPU_DEVICE Device)
{
    UNREFERENCED_PARAMETER(Device);
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
SoftGpuPlatformSetDisplayPower(
    _Inout_ PSOFTGPU_DEVICE Device,
    _In_ BOOLEAN On)
{
    if (Device == NULL || Device->PlatformContext == NULL)
        return STATUS_NOT_SUPPORTED;

    QVgaSetBlank(Device->PlatformContext, !On);
    return STATUS_SUCCESS;
}

NTSTATUS
SoftGpuPlatformQueryDescriptor(
    _In_ PSOFTGPU_DEVICE Device,
    _Inout_ PDXGK_DEVICE_DESCRIPTOR DeviceDescriptor)
{
    ULONG Length;

    if (Device->EdidLength == 0)
        return STATUS_MONITOR_NO_DESCRIPTOR;
    if (DeviceDescriptor->DescriptorOffset >= Device->EdidLength)
        return STATUS_MONITOR_NO_MORE_DESCRIPTOR_DATA;
    if (DeviceDescriptor->DescriptorBuffer == NULL)
        return STATUS_INVALID_PARAMETER;

    Length = min(DeviceDescriptor->DescriptorLength, Device->EdidLength - DeviceDescriptor->DescriptorOffset);
    RtlCopyMemory(DeviceDescriptor->DescriptorBuffer, Device->Edid + DeviceDescriptor->DescriptorOffset, Length);
    DeviceDescriptor->DescriptorLength = Length;
    return STATUS_SUCCESS;
}
