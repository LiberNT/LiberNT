/*
 * PROJECT:     LiberNT Hyper-V Video Driver
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Hyper-V synthetic video platform provider for the software GPU engine
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "softgpu.h"
#include "softgpu_2d_core.h"
#include <vmbusapi.h>

#define HVVID_TAG                   'dvVL'
#define HVVID_RING_SIZE             (64 * 1024)
#define HVVID_PACKET_MAX            1024
#define HVVID_RESPONSE_MAX          512
#define HVVID_WAIT_MS               5000
#define HVVID_WORKING_SURFACE_COUNT 12UL
#define HVVID_MAX_RESOLUTIONS       64

#define PIPE_MESSAGE_DATA 1

#define SYNTHVID_VERSION_REQUEST                1
#define SYNTHVID_VERSION_RESPONSE               2
#define SYNTHVID_VRAM_LOCATION                  3
#define SYNTHVID_VRAM_LOCATION_ACK              4
#define SYNTHVID_SITUATION_UPDATE               5
#define SYNTHVID_SITUATION_UPDATE_ACK           6
#define SYNTHVID_POINTER_POSITION               7
#define SYNTHVID_POINTER_SHAPE                  8
#define SYNTHVID_FEATURE_CHANGE                 9
#define SYNTHVID_DIRT                           10
#define SYNTHVID_SUPPORTED_RESOLUTIONS_REQUEST  13
#define SYNTHVID_SUPPORTED_RESOLUTIONS_RESPONSE 14

#define SYNTHVID_VERSION(Major, Minor) ((ULONG)(Major) | ((ULONG)(Minor) << 16))
#define SYNTHVID_VERSION_WIN10 SYNTHVID_VERSION(3, 5)

#define SYNTHVID_POINTER_COMPLETE   0xFF
#define SYNTHVID_POINTER_SHAPE_SIZE 18
#define HVVID_POINTER_MESSAGE_MAX   (4 * sizeof(ULONG) + SYNTHVID_POINTER_SHAPE_SIZE + \
                                     SOFTGPU_POINTER_PIXEL_COUNT * sizeof(ULONG))

typedef struct _HVVID_CONTEXT
{
    PVMBUS_CHANNEL Channel;
    BOOLEAN Open;
    ULONG Version;
    PHYSICAL_ADDRESS Vram;
    ULONGLONG VramSize;
    ULONG Width;
    ULONG Height;
    ULONG Pitch;
    KEVENT ResponseEvent;
    volatile LONG ExpectedType;
    ULONG ResponseLength;
    UCHAR Response[HVVID_RESPONSE_MAX];
    UCHAR Packet[HVVID_PACKET_MAX];
    PSOFTGPU_DEVICE Device;
    WORK_QUEUE_ITEM PointerWorkItem;
    KEVENT PointerWorkIdle;
    volatile LONG PointerWorkQueued;
    volatile LONG PointerWanted;
    volatile LONG PointerResend;
    BOOLEAN Stopping;
    ULONG PointerGeneration;
    UCHAR PointerMessage[HVVID_POINTER_MESSAGE_MAX];
} HVVID_CONTEXT, *PHVVID_CONTEXT;

static const ULONG HvVidVersions[] =
{
    SYNTHVID_VERSION(3, 5),
    SYNTHVID_VERSION(3, 4),
    SYNTHVID_VERSION(3, 3),
    SYNTHVID_VERSION(3, 2),
    SYNTHVID_VERSION(3, 0),
};

static const USHORT HvVidFallbackModes[][2] =
{
    { 640, 480 }, { 800, 600 }, { 1024, 768 }, { 1152, 864 }, { 1280, 720 },
    { 1280, 800 }, { 1280, 1024 }, { 1440, 900 }, { 1600, 900 }, { 1600, 1200 },
    { 1680, 1050 }, { 1920, 1080 },
};

static
NTSTATUS
HvVidSend(
    _In_ PHVVID_CONTEXT Context,
    _In_ ULONG Type,
    _In_reads_bytes_(Length) const VOID *Payload,
    _In_ ULONG Length)
{
    UCHAR Message[96];
    ULONG Header[4];

    if (!Context->Open || Length > sizeof(Message) - sizeof(Header))
        return STATUS_INVALID_PARAMETER;

    Header[0] = PIPE_MESSAGE_DATA;
    Header[1] = 2 * sizeof(ULONG) + Length;
    Header[2] = Type;
    Header[3] = 2 * sizeof(ULONG) + Length;
    RtlCopyMemory(Message, Header, sizeof(Header));
    RtlCopyMemory(Message + sizeof(Header), Payload, Length);
    return VmbChannelSendPacket(Context->Channel, VMBUS_PACKET_TYPE_INBAND, 0, 0, Message, sizeof(Header) + Length);
}

static
VOID
HvVidSendDirt(
    _In_ PHVVID_CONTEXT Context,
    _In_ LONG Left,
    _In_ LONG Top,
    _In_ LONG Right,
    _In_ LONG Bottom);

static
VOID
HvVidQueuePointerWork(
    _Inout_ PHVVID_CONTEXT Context)
{
    if (!Context->Stopping && Context->Device != NULL &&
        InterlockedExchange(&Context->PointerWorkQueued, 1) == 0)
    {
        KeClearEvent(&Context->PointerWorkIdle);
        ExQueueWorkItem(&Context->PointerWorkItem, DelayedWorkQueue);
    }
}

static
NTSTATUS
HvVidRequest(
    _Inout_ PHVVID_CONTEXT Context,
    _In_ ULONG Type,
    _In_reads_bytes_(Length) const VOID *Payload,
    _In_ ULONG Length,
    _In_ ULONG ResponseType)
{
    LARGE_INTEGER Timeout;
    NTSTATUS Status;

    KeClearEvent(&Context->ResponseEvent);
    Context->ResponseLength = 0;
    InterlockedExchange(&Context->ExpectedType, (LONG)ResponseType);

    Status = HvVidSend(Context, Type, Payload, Length);
    if (NT_SUCCESS(Status))
    {
        Timeout.QuadPart = -10000LL * HVVID_WAIT_MS;
        if (KeWaitForSingleObject(&Context->ResponseEvent, Executive, KernelMode, FALSE, &Timeout) == STATUS_TIMEOUT)
            Status = STATUS_IO_TIMEOUT;
    }

    InterlockedExchange(&Context->ExpectedType, 0);
    return Status;
}

static
VOID
NTAPI
HvVidChannelCallback(
    _In_opt_ PVOID Parameter)
{
    PHVVID_CONTEXT Context = Parameter;
    PVMBUS_PACKET_DESCRIPTOR Descriptor;
    PUCHAR Payload;
    ULONG Header[4];
    ULONG Length;
    ULONG Offset;
    ULONG Available;
    NTSTATUS Status;

    if (Context == NULL)
        return;

    for (;;)
    {
        Status = VmbChannelReceivePacket(Context->Channel, Context->Packet, sizeof(Context->Packet), &Length);
        if (!NT_SUCCESS(Status))
        {
            if (Status != STATUS_NO_MORE_ENTRIES)
                DPRINT1("HYPERVVID: receive failed with 0x%lx (length %lu)\n", Status, Length);
            break;
        }

        Descriptor = (PVMBUS_PACKET_DESCRIPTOR)Context->Packet;
        Offset = (ULONG)Descriptor->DataOffset8 * 8;
        if (Descriptor->Type != VMBUS_PACKET_TYPE_INBAND || Offset + sizeof(Header) > Length)
            continue;

        Payload = Context->Packet + Offset;
        RtlCopyMemory(Header, Payload, sizeof(Header));
        if (Header[0] != PIPE_MESSAGE_DATA || Header[1] < 2 * sizeof(ULONG) ||
            Header[1] > Length - Offset - 2 * sizeof(ULONG))
        {
            continue;
        }

        Available = min(Header[3], Header[1] - 2 * sizeof(ULONG));
        Payload += sizeof(Header);

        if ((LONG)Header[2] == ReadNoFence(&Context->ExpectedType))
        {
            Context->ResponseLength = min(Available, (ULONG)sizeof(Context->Response));
            RtlCopyMemory(Context->Response, Payload, Context->ResponseLength);
            InterlockedExchange(&Context->ExpectedType, 0);
            KeSetEvent(&Context->ResponseEvent, IO_NO_INCREMENT, FALSE);
        }
        else if (Header[2] == SYNTHVID_FEATURE_CHANGE && Available >= 4)
        {
            InterlockedExchange(&Context->PointerWanted, Payload[1] != 0 || Payload[2] != 0);
            HvVidQueuePointerWork(Context);
            if (Payload[0] != 0 && Context->Width != 0 && Context->Height != 0)
                HvVidSendDirt(Context, 0, 0, (LONG)Context->Width, (LONG)Context->Height);
        }
    }
}

static
VOID
HvVidSendDirt(
    _In_ PHVVID_CONTEXT Context,
    _In_ LONG Left,
    _In_ LONG Top,
    _In_ LONG Right,
    _In_ LONG Bottom)
{
    UCHAR Dirt[2 + 4 * sizeof(LONG)];

    Dirt[0] = 0;
    Dirt[1] = 1;
    RtlCopyMemory(Dirt + 2, &Left, sizeof(LONG));
    RtlCopyMemory(Dirt + 6, &Top, sizeof(LONG));
    RtlCopyMemory(Dirt + 10, &Right, sizeof(LONG));
    RtlCopyMemory(Dirt + 14, &Bottom, sizeof(LONG));
    HvVidSend(Context, SYNTHVID_DIRT, Dirt, sizeof(Dirt));
}

static
VOID
NTAPI
HvVidPointerWorker(
    _In_ PVOID Parameter)
{
    PHVVID_CONTEXT Context = Parameter;
    BOOLEAN Wanted;

    do
    {
        InterlockedExchange(&Context->PointerWorkQueued, 2);
        Wanted = ReadNoFence(&Context->PointerWanted) != 0;
        InterlockedExchange(&Context->PointerResend, 1);
        if (!Context->Stopping)
            (VOID)SoftGpuPointerSetPlatformHardware(Context->Device, Wanted);
    } while (InterlockedCompareExchange(&Context->PointerWorkQueued, 0, 2) != 2);

    KeSetEvent(&Context->PointerWorkIdle, IO_NO_INCREMENT, FALSE);
}

static
NTSTATUS
HvVidSendPointerPosition(
    _In_ PHVVID_CONTEXT Context,
    _In_ BOOLEAN Visible,
    _In_ LONG X,
    _In_ LONG Y)
{
    UCHAR Position[2 + 2 * sizeof(LONG)];

    Position[0] = Visible ? 1 : 0;
    Position[1] = 0;
    RtlCopyMemory(Position + 2, &X, sizeof(LONG));
    RtlCopyMemory(Position + 6, &Y, sizeof(LONG));
    return HvVidSend(Context, SYNTHVID_POINTER_POSITION, Position, sizeof(Position));
}

static
NTSTATUS
HvVidSendPointerShape(
    _Inout_ PHVVID_CONTEXT Context,
    _In_ PSOFTGPU_DEVICE Device)
{
    ULONG RowBytes = Device->PointerWidth * sizeof(ULONG);
    ULONG Length = SYNTHVID_POINTER_SHAPE_SIZE + RowBytes * Device->PointerHeight;
    PUCHAR Shape;
    ULONG Header[4];
    ULONG Y;

    if (!Context->Open || Device->PointerWidth > SOFTGPU_POINTER_MAX_WIDTH ||
        Device->PointerHeight > SOFTGPU_POINTER_MAX_HEIGHT)
    {
        return STATUS_INVALID_PARAMETER;
    }

    Header[0] = PIPE_MESSAGE_DATA;
    Header[1] = 2 * sizeof(ULONG) + Length;
    Header[2] = SYNTHVID_POINTER_SHAPE;
    Header[3] = 2 * sizeof(ULONG) + Length;
    RtlCopyMemory(Context->PointerMessage, Header, sizeof(Header));

    Shape = Context->PointerMessage + sizeof(Header);
    Shape[0] = SYNTHVID_POINTER_COMPLETE;
    Shape[1] = 1;
    RtlCopyMemory(Shape + 2, &Device->PointerWidth, sizeof(ULONG));
    RtlCopyMemory(Shape + 6, &Device->PointerHeight, sizeof(ULONG));
    RtlCopyMemory(Shape + 10, &Device->PointerHotX, sizeof(ULONG));
    RtlCopyMemory(Shape + 14, &Device->PointerHotY, sizeof(ULONG));
    for (Y = 0; Y < Device->PointerHeight; Y++)
    {
        RtlCopyMemory(Shape + SYNTHVID_POINTER_SHAPE_SIZE + Y * RowBytes,
                      &Device->PointerPixels[Y * SOFTGPU_POINTER_MAX_WIDTH],
                      RowBytes);
    }

    return VmbChannelSendPacket(Context->Channel, VMBUS_PACKET_TYPE_INBAND, 0, 0,
                                Context->PointerMessage, sizeof(Header) + Length);
}

static
NTSTATUS
HvVidSetSituation(
    _Inout_ PHVVID_CONTEXT Context,
    _In_ ULONG Width,
    _In_ ULONG Height,
    _In_ ULONG Pitch)
{
    UCHAR Situation[27];
    ULONG64 UserContext = 0;
    ULONG Zero = 0;
    NTSTATUS Status;

    RtlZeroMemory(Situation, sizeof(Situation));
    RtlCopyMemory(Situation, &UserContext, sizeof(UserContext));
    Situation[8] = 1;
    Situation[9] = 1;
    RtlCopyMemory(Situation + 10, &Zero, sizeof(Zero));
    Situation[14] = 32;
    RtlCopyMemory(Situation + 15, &Width, sizeof(Width));
    RtlCopyMemory(Situation + 19, &Height, sizeof(Height));
    RtlCopyMemory(Situation + 23, &Pitch, sizeof(Pitch));

    Status = HvVidRequest(Context, SYNTHVID_SITUATION_UPDATE, Situation, sizeof(Situation),
                          SYNTHVID_SITUATION_UPDATE_ACK);
    if (Status == STATUS_IO_TIMEOUT)
        Status = STATUS_SUCCESS;
    if (!NT_SUCCESS(Status))
        return Status;

    Context->Width = Width;
    Context->Height = Height;
    Context->Pitch = Pitch;
    HvVidSendDirt(Context, 0, 0, (LONG)Width, (LONG)Height);
    return STATUS_SUCCESS;
}

static
NTSTATUS
HvVidSetMode(
    _Inout_ PSOFTGPU_DEVICE Device,
    _In_ const SOFTGPU_DISPLAY_MODE *Mode)
{
    PHVVID_CONTEXT Context = Device->PlatformContext;

    if (Context == NULL || Mode == NULL)
        return STATUS_INVALID_PARAMETER;

    return HvVidSetSituation(Context, Mode->Width, Mode->Height, Mode->Pitch);
}

static
VOID
HvVidRelease(
    _In_ PHVVID_CONTEXT Context)
{
    Context->Stopping = TRUE;
    KeWaitForSingleObject(&Context->PointerWorkIdle, Executive, KernelMode, FALSE, NULL);
    if (Context->Open)
    {
        Context->Open = FALSE;
        VmbChannelClose(Context->Channel);
    }

    ExFreePoolWithTag(Context, HVVID_TAG);
}

static
VOID
HvVidAddMode(
    _Inout_ PSOFTGPU_PLATFORM_CONFIG Config,
    _In_ ULONG Width,
    _In_ ULONG Height,
    _In_ ULONG Pitch)
{
    ULONG Index;

    if (Width < 640 || Height < 480 || Config->ModeCount >= SOFTGPU_MAX_DISPLAY_MODES ||
        (ULONGLONG)Pitch * Height > Config->ScanoutSize)
    {
        return;
    }

    for (Index = 0; Index < Config->ModeCount; Index++)
    {
        if (Config->Modes[Index].Width == Width && Config->Modes[Index].Height == Height)
            return;
    }

    Config->Modes[Config->ModeCount].Width = Width;
    Config->Modes[Config->ModeCount].Height = Height;
    Config->Modes[Config->ModeCount].Pitch = Pitch;
    Config->Modes[Config->ModeCount].PlatformMode = Config->ModeCount;
    Config->ModeCount++;
}

static
VOID
HvVidQueryModes(
    _Inout_ PHVVID_CONTEXT Context,
    _Inout_ PSOFTGPU_PLATFORM_CONFIG Config)
{
    UCHAR Request = HVVID_MAX_RESOLUTIONS;
    ULONG Count;
    ULONG Index;
    USHORT Width;
    USHORT Height;

    Config->ModeCount = 0;
    HvVidAddMode(Config, Config->Width, Config->Height, Config->ScanoutPitch);

    if (Context->Version >= SYNTHVID_VERSION_WIN10 &&
        NT_SUCCESS(HvVidRequest(Context, SYNTHVID_SUPPORTED_RESOLUTIONS_REQUEST, &Request, sizeof(Request),
                                SYNTHVID_SUPPORTED_RESOLUTIONS_RESPONSE)) &&
        Context->ResponseLength >= SOFTGPU_EDID_SIZE + 3)
    {
        RtlCopyMemory(Config->Edid, Context->Response, SOFTGPU_EDID_SIZE);
        Config->EdidLength = SOFTGPU_EDID_SIZE;
        Count = Context->Response[SOFTGPU_EDID_SIZE];
        for (Index = 0; Index < Count && SOFTGPU_EDID_SIZE + 3 + (Index + 1) * 4 <= Context->ResponseLength; Index++)
        {
            RtlCopyMemory(&Width, Context->Response + SOFTGPU_EDID_SIZE + 3 + Index * 4, sizeof(Width));
            RtlCopyMemory(&Height, Context->Response + SOFTGPU_EDID_SIZE + 3 + Index * 4 + 2, sizeof(Height));
            HvVidAddMode(Config, Width, Height, (ULONG)Width * SOFTGPU_DISPLAY_BYTES_PER_PIXEL);
        }
    }
    else
    {
        for (Index = 0; Index < RTL_NUMBER_OF(HvVidFallbackModes); Index++)
        {
            HvVidAddMode(Config, HvVidFallbackModes[Index][0], HvVidFallbackModes[Index][1],
                         HvVidFallbackModes[Index][0] * SOFTGPU_DISPLAY_BYTES_PER_PIXEL);
        }
    }

    Config->CurrentMode = 0;
    if (Config->ModeCount > 1)
        Config->SetMode = HvVidSetMode;
    else
        Config->ModeCount = 0;
}

static
NTSTATUS
HvVidStart(
    _Inout_ PHVVID_CONTEXT Context,
    _In_ const DXGK_DISPLAY_INFORMATION *PostDisplay)
{
    UCHAR VramLocation[17];
    ULONG64 Gpa;
    ULONG Index;
    NTSTATUS Status;

    Status = VmbChannelOpen(Context->Channel, HVVID_RING_SIZE, HvVidChannelCallback, Context);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("HYPERVVID: VmbChannelOpen failed with 0x%lx\n", Status);
        return Status;
    }
    Context->Open = TRUE;

    for (Index = 0; Index < RTL_NUMBER_OF(HvVidVersions); Index++)
    {
        Status = HvVidRequest(Context, SYNTHVID_VERSION_REQUEST, &HvVidVersions[Index], sizeof(ULONG),
                              SYNTHVID_VERSION_RESPONSE);
        if (!NT_SUCCESS(Status))
            return Status;

        if (Context->ResponseLength >= 5 && Context->Response[4] != 0)
        {
            Context->Version = HvVidVersions[Index];
            break;
        }
    }

    if (Context->Version == 0)
    {
        DPRINT1("HYPERVVID: host accepted no protocol version\n");
        return STATUS_REVISION_MISMATCH;
    }

    Gpa = (ULONG64)PostDisplay->PhysicAddress.QuadPart;
    RtlCopyMemory(VramLocation, &Gpa, sizeof(Gpa));
    VramLocation[8] = 1;
    RtlCopyMemory(VramLocation + 9, &Gpa, sizeof(Gpa));
    Status = HvVidRequest(Context, SYNTHVID_VRAM_LOCATION, VramLocation, sizeof(VramLocation),
                          SYNTHVID_VRAM_LOCATION_ACK);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("HYPERVVID: VRAM location not acknowledged (0x%lx)\n", Status);
        return Status;
    }

    return HvVidSetSituation(Context, PostDisplay->Width, PostDisplay->Height, PostDisplay->Pitch);
}

NTSTATUS
SoftGpuPlatformValidatePdo(
    _In_ PDEVICE_OBJECT PhysicalDeviceObject)
{
    if (PhysicalDeviceObject == NULL)
        return STATUS_INVALID_PARAMETER;

    return VmbChannelFromDevice(PhysicalDeviceObject) != NULL ? STATUS_SUCCESS : STATUS_NOT_SUPPORTED;
}

NTSTATUS
SoftGpuPlatformQueryStart(
    _In_ PSOFTGPU_DEVICE Device,
    _In_ PDXGK_INTERFACE DxgkInterface,
    _Out_ PSOFTGPU_PLATFORM_CONFIG Config)
{
    DXGK_DISPLAY_INFORMATION PostDisplay;
    ULONGLONG VisibleLength;
    PHVVID_CONTEXT Context;
    SIZE_T WorkingSetSize;
    ULONG SlabWidth;
    ULONG SlabHeight;
    ULONG Index;
    NTSTATUS Status;

    if (Device == NULL || DxgkInterface == NULL || Config == NULL || Device->PhysicalDeviceObject == NULL)
        return STATUS_INVALID_PARAMETER;

    RtlZeroMemory(Config, sizeof(*Config));

    Status = SoftGpuAcquirePostDisplay(DxgkInterface, &PostDisplay, &VisibleLength);
    if (!NT_SUCCESS(Status))
        return Status;
    if (PostDisplay.Width == 0 || PostDisplay.PhysicAddress.QuadPart == 0)
    {
        DPRINT1("HYPERVVID: no boot framebuffer to back the synthetic video surface\n");
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    }

    Context = Device->PlatformContext;
    if (Context == NULL)
    {
        Context = ExAllocatePoolZero(NonPagedPool, sizeof(*Context), HVVID_TAG);
        if (Context == NULL)
            return STATUS_INSUFFICIENT_RESOURCES;

        KeInitializeEvent(&Context->ResponseEvent, NotificationEvent, FALSE);
        KeInitializeEvent(&Context->PointerWorkIdle, NotificationEvent, TRUE);
        ExInitializeWorkItem(&Context->PointerWorkItem, HvVidPointerWorker, Context);
        Context->Channel = VmbChannelFromDevice(Device->PhysicalDeviceObject);
        Context->Vram = PostDisplay.PhysicAddress;
        Context->VramSize = max(VisibleLength, (ULONGLONG)VmbChannelGetMmioMegabytes(Context->Channel) << 20);

        Status = HvVidStart(Context, &PostDisplay);
        if (!NT_SUCCESS(Status))
        {
            HvVidRelease(Context);
            return Status;
        }

        Context->Device = Device;
        Device->PlatformContext = Context;
    }

    Device->PlatformSwitchablePointer = TRUE;
    if (ReadNoFence(&Context->PointerWanted) != 0)
        HvVidQueuePointerWork(Context);

    Config->Width = PostDisplay.Width;
    Config->Height = PostDisplay.Height;
    Config->Format = PostDisplay.ColorFormat;
    Config->ScanoutPhysicalAddress = Context->Vram;
    Config->ScanoutPitch = PostDisplay.Pitch;
    Config->ScanoutSize = Context->VramSize;
    HvVidQueryModes(Context, Config);

    SlabWidth = Config->Width;
    SlabHeight = Config->Height;
    for (Index = 0; Index < Config->ModeCount; Index++)
    {
        if ((ULONGLONG)Config->Modes[Index].Width * Config->Modes[Index].Height > (ULONGLONG)SlabWidth * SlabHeight)
        {
            SlabWidth = Config->Modes[Index].Width;
            SlabHeight = Config->Modes[Index].Height;
        }
    }

    Status = SoftGpu2dComputeAllocationSlabSize(SlabWidth, SlabHeight, HVVID_WORKING_SURFACE_COUNT,
                                                MAXULONG_PTR, &WorkingSetSize);
    if (!NT_SUCCESS(Status))
        return Status;
    Config->MinimumAllocationSlabSize = min(WorkingSetSize, SOFTGPU_MAX_ALLOCATION_SLAB_SIZE);
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
    PHVVID_CONTEXT Context;

    if (Device == NULL || Device->PlatformContext == NULL)
        return STATUS_SUCCESS;

    Context = Device->PlatformContext;
    Device->PlatformContext = NULL;
    HvVidRelease(Context);
    Device->PlatformHardwarePointer = FALSE;
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
    PHVVID_CONTEXT Context;
    NTSTATUS Status;

    if (Device == NULL || Device->PlatformContext == NULL)
        return STATUS_NOT_SUPPORTED;

    Context = Device->PlatformContext;
    if (InterlockedExchange(&Context->PointerResend, 0) != 0)
        Context->PointerGeneration = 0;

    if (Device->PointerShapeValid && Context->PointerGeneration != Device->PointerShapeGeneration)
    {
        Status = HvVidSendPointerShape(Context, Device);
        if (!NT_SUCCESS(Status))
            return Status;
        Context->PointerGeneration = Device->PointerShapeGeneration;
    }

    return HvVidSendPointerPosition(Context, Device->PointerVisible && Device->PointerShapeValid,
                                    Device->PointerX - (LONG)Device->PointerHotX,
                                    Device->PointerY - (LONG)Device->PointerHotY);
}

VOID
SoftGpuPlatformScanoutWritten(
    _Inout_ PSOFTGPU_DEVICE Device,
    _In_ const RECT *Rect)
{
    PHVVID_CONTEXT Context;
    LONG Left;
    LONG Top;
    LONG Right;
    LONG Bottom;

    if (Device == NULL || Rect == NULL || Device->PlatformContext == NULL)
        return;

    Context = Device->PlatformContext;
    Left = max(Rect->left, 0);
    Top = max(Rect->top, 0);
    Right = min(Rect->right, (LONG)Context->Width);
    Bottom = min(Rect->bottom, (LONG)Context->Height);
    if (Left >= Right || Top >= Bottom)
        return;

    KeMemoryBarrier();
    HvVidSendDirt(Context, Left, Top, Right, Bottom);
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
