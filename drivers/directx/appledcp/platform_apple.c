/*
 * PROJECT:     LiberNT Apple Display Coprocessor Driver
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Apple silicon display coprocessor platform provider for the software GPU engine
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntifs.h>
#include "softgpu.h"
#include "softgpu_2d_core.h"
#include "appledcp.h"
#include <reactos/drivers/appleagx.h>

static PDRIVER_DISPATCH DcpDxgkPnp;
static DCP_INTERRUPT_RESOURCES DcpInterrupt;

typedef struct _APPLE_DCP_CONTEXT
{
    PAPPLE_DCP Dcp;
    PVOID SurfaceAllocation;
    SIZE_T SurfaceAllocationSize;
    PUCHAR Surface;
    PHYSICAL_ADDRESS SurfacePhysical;
    SIZE_T SurfaceSize;
    ULONG64 SurfaceIova;
    ULONG Width;
    ULONG Height;
    ULONG Pitch;
    BOOLEAN Accelerated;
    DCP_LAYER Layer;
    WORK_QUEUE_ITEM ScanoutWork;
    KEVENT ScanoutDone;
    volatile LONG ScanoutState;
} APPLE_DCP_CONTEXT, *PAPPLE_DCP_CONTEXT;

#define DCP_SCANOUT_PENDING     0
#define DCP_SCANOUT_QUEUED      1
#define DCP_SCANOUT_DONE        2
#define DCP_SCANOUT_CANCELLED   3

static
NTSTATUS
NTAPI
DcpPnpCompletion(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp,
    _In_ PVOID Context)
{
    UNREFERENCED_PARAMETER(DeviceObject);
    UNREFERENCED_PARAMETER(Irp);
    KeSetEvent((PKEVENT)Context, IO_NO_INCREMENT, FALSE);
    return STATUS_MORE_PROCESSING_REQUIRED;
}

static
NTSTATUS
DcpQueryMailboxInterrupt(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Out_ PULONG Gsi,
    _Out_ PBOOLEAN Edge)
{
    PDEVICE_OBJECT Pdo = IoGetDeviceAttachmentBaseRef(DeviceObject);
    FDT_DRIVER_TREE Tree;
    NTSTATUS Status;

    *Gsi = 0;
    *Edge = FALSE;
    Status = FdtDriverOpen(Pdo, &Tree);
    if (NT_SUCCESS(Status))
    {
        Status = AppleRtkitQueryInterrupt(&Tree, Tree.Node, Gsi, Edge);
        FdtDriverClose(&Tree);
    }
    ObDereferenceObject(Pdo);
    return Status;
}

static
NTSTATUS
DcpFilterRequirements(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    PDEVICE_OBJECT Lower = IoGetLowerDeviceObject(DeviceObject);
    BOOLEAN Edge;
    ULONG Gsi;
    KEVENT Event;
    NTSTATUS Status, FilterStatus;

    if (!Lower)
        return DcpDxgkPnp(DeviceObject, Irp);
    KeInitializeEvent(&Event, NotificationEvent, FALSE);
    IoCopyCurrentIrpStackLocationToNext(Irp);
    IoSetCompletionRoutine(Irp, DcpPnpCompletion, &Event, TRUE, TRUE, TRUE);
    Status = IoCallDriver(Lower, Irp);
    ObDereferenceObject(Lower);
    if (Status == STATUS_PENDING)
    {
        KeWaitForSingleObject(&Event, Executive, KernelMode, FALSE, NULL);
        Status = Irp->IoStatus.Status;
    }
    if (NT_SUCCESS(Status) || Status == STATUS_NOT_SUPPORTED)
    {
        FilterStatus = DcpQueryMailboxInterrupt(DeviceObject, &Gsi, &Edge);
        if (NT_SUCCESS(FilterStatus))
            FilterStatus = AppleAddInterruptRequirement(Irp, Gsi, Edge);
        DbgPrint("appledcp: requesting the mailbox interrupt %lu: %lx\n", Gsi, FilterStatus);
        if (NT_SUCCESS(FilterStatus))
            Status = STATUS_SUCCESS;
    }
    Irp->IoStatus.Status = Status;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return Status;
}

static
VOID
DcpCaptureInterrupt(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_opt_ PCM_RESOURCE_LIST Raw,
    _In_opt_ PCM_RESOURCE_LIST Translated)
{
    PCM_PARTIAL_RESOURCE_DESCRIPTOR RawPartial, Partial;
    BOOLEAN Edge;
    ULONG Gsi, Index;

    RtlZeroMemory(&DcpInterrupt, sizeof(DcpInterrupt));
    if (!Raw || !Translated || !Raw->Count || !Translated->Count ||
        Raw->List[0].PartialResourceList.Count != Translated->List[0].PartialResourceList.Count ||
        !NT_SUCCESS(DcpQueryMailboxInterrupt(DeviceObject, &Gsi, &Edge)))
    {
        return;
    }
    for (Index = 0; Index < Raw->List[0].PartialResourceList.Count; ++Index)
    {
        RawPartial = &Raw->List[0].PartialResourceList.PartialDescriptors[Index];
        Partial = &Translated->List[0].PartialResourceList.PartialDescriptors[Index];
        if (RawPartial->Type != CmResourceTypeInterrupt || Partial->Type != CmResourceTypeInterrupt ||
            RawPartial->u.Interrupt.Vector != Gsi)
        {
            continue;
        }
        DcpInterrupt.Gsi = Gsi;
        DcpInterrupt.Raw.Count = 1;
        DcpInterrupt.Raw.List[0] = Raw->List[0];
        DcpInterrupt.Raw.List[0].PartialResourceList.Count = 1;
        DcpInterrupt.Raw.List[0].PartialResourceList.PartialDescriptors[0] = *RawPartial;
        DcpInterrupt.Translated.Count = 1;
        DcpInterrupt.Translated.List[0] = Translated->List[0];
        DcpInterrupt.Translated.List[0].PartialResourceList.Count = 1;
        DcpInterrupt.Translated.List[0].PartialResourceList.PartialDescriptors[0] = *Partial;
        DcpInterrupt.Valid = TRUE;
        return;
    }
}

static
NTSTATUS
NTAPI
DcpPnp(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    PIO_STACK_LOCATION Stack = IoGetCurrentIrpStackLocation(Irp);

    ASSERT(KeGetCurrentIrql() == PASSIVE_LEVEL && DcpDxgkPnp != NULL);
    if (Stack->MinorFunction == IRP_MN_FILTER_RESOURCE_REQUIREMENTS)
        return DcpFilterRequirements(DeviceObject, Irp);
    if (Stack->MinorFunction == IRP_MN_START_DEVICE)
    {
        DcpCaptureInterrupt(DeviceObject, Stack->Parameters.StartDevice.AllocatedResources,
                            Stack->Parameters.StartDevice.AllocatedResourcesTranslated);
    }
    return DcpDxgkPnp(DeviceObject, Irp);
}

VOID
SoftGpuPlatformDriverEntry(
    _Inout_ PDRIVER_OBJECT DriverObject)
{
    ASSERT(DriverObject->MajorFunction[IRP_MJ_PNP] != NULL);
    DcpDxgkPnp = DriverObject->MajorFunction[IRP_MJ_PNP];
    DriverObject->MajorFunction[IRP_MJ_PNP] = DcpPnp;
}

static
VOID
DcpPlatformLogModes(
    _Inout_ PAPPLE_DCP Dcp,
    _Out_ PULONG NativeWidth,
    _Out_ PULONG NativeHeight)
{
    DCP_TIMING_MODE Timing[DCP_MAX_TIMING_MODES];
    DCP_COLOR_MODE Color[DCP_MAX_COLOR_MODES];
    ULONG TimingCount, ColorCount, Index;
    BOOLEAN Connected;

    if (NT_SUCCESS(DcpGetHotPlug(Dcp, &Connected, &TimingCount, &ColorCount)))
    {
        DbgPrint("appledcp: panel %s, %lu timing modes, %lu color modes\n", Connected ? "connected" : "absent",
                 TimingCount, ColorCount);
    }
    *NativeWidth = 0;
    *NativeHeight = 0;
    if (NT_SUCCESS(DcpGetTimingModes(Dcp, Timing, RTL_NUMBER_OF(Timing), &TimingCount)))
    {
        DcpNativeMode(Timing, min(TimingCount, (ULONG)RTL_NUMBER_OF(Timing)), NativeWidth, NativeHeight);
        for (Index = 0; Index < TimingCount; ++Index)
        {
            DbgPrint("appledcp: timing mode %lu: %lux%lu at %u.%02lu Hz%s\n", Index, Timing[Index].Width,
                     Timing[Index].Height, Timing[Index].FpsInteger, ((ULONG)Timing[Index].FpsFraction * 100) >> 16,
                     Timing[Index].Valid ? "" : " (not valid)");
        }
    }
    if (NT_SUCCESS(DcpGetColorModes(Dcp, Color, RTL_NUMBER_OF(Color), &ColorCount)))
    {
        for (Index = 0; Index < ColorCount; ++Index)
        {
            DbgPrint("appledcp: color mode %lu: colorimetry %lu, EOTF %lu, encoding %lu, %lu bpp%s\n", Index,
                     Color[Index].Colorimetry, Color[Index].Eotf, Color[Index].Encoding, Color[Index].Bpp,
                     Color[Index].Valid ? "" : " (not valid)");
        }
    }
}

/* The graphics processor is asked how far it has come. Rendering on it is offered only once its
 * firmware is ready; until then the software renderer keeps the desktop, which is what the panel
 * is showing today. */
static
BOOLEAN
DcpPlatformGraphicsReady(VOID)
{
    UNICODE_STRING Name = RTL_CONSTANT_STRING(APPLE_AGX_DEVICE_NAME);
    APPLE_AGX_REPORT Report = { 0 };
    IO_STATUS_BLOCK IoStatus;
    PDEVICE_OBJECT Device;
    PFILE_OBJECT File;
    KEVENT Event;
    PIRP Irp;
    NTSTATUS Status;

    Status = IoGetDeviceObjectPointer(&Name, FILE_READ_DATA, &File, &Device);
    if (!NT_SUCCESS(Status))
        return FALSE;
    KeInitializeEvent(&Event, NotificationEvent, FALSE);
    Irp = IoBuildDeviceIoControlRequest(IOCTL_APPLE_AGX_QUERY_STATE, Device, NULL, 0, &Report, sizeof(Report),
                                        FALSE, &Event, &IoStatus);
    if (Irp)
    {
        Status = IoCallDriver(Device, Irp);
        if (Status == STATUS_PENDING)
        {
            KeWaitForSingleObject(&Event, Executive, KernelMode, FALSE, NULL);
            Status = IoStatus.Status;
        }
    }
    else
    {
        Status = STATUS_INSUFFICIENT_RESOURCES;
    }
    ObDereferenceObject(File);
    if (!NT_SUCCESS(Status))
        return FALSE;
    DbgPrint("appledcp: the graphics processor is at stage %lu (%s%lu.%lu, %lu cores, %lu kHz)\n", Report.State,
             Report.State >= APPLE_AGX_STATE_IDENTIFIED ? "revision " : "", Report.Revision >> 8,
             Report.Revision & 0xff, Report.ActiveCores, Report.TopKilohertz);
    return Report.State == APPLE_AGX_STATE_READY;
}

static
VOID
DcpPlatformRelease(
    _In_ PAPPLE_DCP_CONTEXT Context)
{
    if (InterlockedCompareExchange(&Context->ScanoutState, DCP_SCANOUT_CANCELLED, DCP_SCANOUT_PENDING) ==
        DCP_SCANOUT_QUEUED)
    {
        KeWaitForSingleObject(&Context->ScanoutDone, Executive, KernelMode, FALSE, NULL);
    }
    if (Context->Dcp)
        DcpClose(Context->Dcp);
    if (Context->SurfaceAllocation && !Context->SurfaceIova)
        MmFreeContiguousMemorySpecifyCache(Context->SurfaceAllocation, Context->SurfaceAllocationSize, MmWriteCombined);
    ExFreePoolWithTag(Context, APPLE_DCP_TAG);
}

static
NTSTATUS
DcpPlatformStart(
    _Inout_ PAPPLE_DCP_CONTEXT Context,
    _In_ PDXGK_INTERFACE DxgkInterface)
{
    DXGK_DISPLAY_INFORMATION PostDisplay;
    DXGK_DEVICE_INFO Information;
    PHYSICAL_ADDRESS Low, High, Boundary;
    ULONGLONG VisibleLength;
    ULONG NativeWidth, NativeHeight;
    ULONG64 Physical;
    DCP_LAYER Layer;
    SIZE_T Skip;
    NTSTATUS Status;

    if (!DxgkInterface->DxgkCbGetDeviceInformation)
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    Status = DxgkInterface->DxgkCbGetDeviceInformation(DxgkInterface->DeviceHandle, &Information);
    if (!NT_SUCCESS(Status) || !Information.PhysicalDeviceObject)
        return NT_SUCCESS(Status) ? STATUS_DEVICE_CONFIGURATION_ERROR : Status;
    Status = DcpOpen(Information.PhysicalDeviceObject, &DcpInterrupt, &Context->Dcp);
    if (!NT_SUCCESS(Status))
        return Status;
    Status = DcpStartDisplayService(Context->Dcp);
    if (!NT_SUCCESS(Status))
        return Status;
    DcpPlatformLogModes(Context->Dcp, &NativeWidth, &NativeHeight);

    Status = SoftGpuAcquirePostDisplay(DxgkInterface, &PostDisplay, &VisibleLength);
    if (NT_SUCCESS(Status))
    {
        DbgPrint("appledcp: boot display %lux%lu, pitch %lu, format %lu at %I64x\n", PostDisplay.Width,
                 PostDisplay.Height, PostDisplay.Pitch, PostDisplay.ColorFormat, PostDisplay.PhysicAddress.QuadPart);
    }
    else
    {
        DbgPrint("appledcp: the boot display was not handed over (%lx), the panel's own mode is used\n", Status);
        RtlZeroMemory(&PostDisplay, sizeof(PostDisplay));
    }
    if (NativeWidth && NativeHeight)
    {
        Context->Width = NativeWidth;
        Context->Height = NativeHeight;
    }
    else if (PostDisplay.Width && PostDisplay.Height)
    {
        Context->Width = PostDisplay.Width;
        Context->Height = PostDisplay.Height;
    }
    else
    {
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    }
    Context->Pitch = Context->Width * SOFTGPU_DISPLAY_BYTES_PER_PIXEL;
    Context->SurfaceSize = ((SIZE_T)Context->Pitch * Context->Height + DCP_DART_PAGE_SIZE - 1) &
                           ~(SIZE_T)(DCP_DART_PAGE_SIZE - 1);
    Context->SurfaceAllocationSize = Context->SurfaceSize + DCP_DART_PAGE_SIZE - PAGE_SIZE;
    Low.QuadPart = 0;
    High.QuadPart = -1;
    Boundary.QuadPart = 0;
    Context->SurfaceAllocation = MmAllocateContiguousMemorySpecifyCache(Context->SurfaceAllocationSize, Low, High,
                                                                        Boundary, MmWriteCombined);
    if (!Context->SurfaceAllocation)
        return STATUS_INSUFFICIENT_RESOURCES;
    Physical = (ULONG64)MmGetPhysicalAddress(Context->SurfaceAllocation).QuadPart;
    Skip = (SIZE_T)(((Physical + DCP_DART_PAGE_SIZE - 1) & ~(ULONG64)(DCP_DART_PAGE_SIZE - 1)) - Physical);
    Context->Surface = (PUCHAR)Context->SurfaceAllocation + Skip;
    Context->SurfacePhysical.QuadPart = (LONGLONG)(Physical + Skip);
    RtlZeroMemory(Context->Surface, Context->SurfaceSize);
    KeMemoryBarrier();

    Status = DcpMapSurface(Context->Dcp, (ULONG64)Context->SurfacePhysical.QuadPart, Context->SurfaceSize,
                           &Context->SurfaceIova);
    if (!NT_SUCCESS(Status))
        return Status;

    RtlZeroMemory(&Layer, sizeof(Layer));
    Layer.Planes[0].Address = Context->SurfaceIova;
    Layer.Planes[0].Stride = Context->Pitch;
    Layer.Planes[0].AddressFormat = DCP_ADDRESS_FORMAT_PLANAR;
    Layer.PlaneCount = 1;
    Layer.Width = Context->Width;
    Layer.Height = Context->Height;
    Layer.SurfaceFormat = DCP_SURFACE_FORMAT_BGRA;
    Layer.Colorspace = DCP_COLORSPACE_SRGB;
    Layer.Eotf = DCP_EOTF_GAMMA_SDR;
    Context->Layer = Layer;
    Context->Accelerated = DcpPlatformGraphicsReady();
    DbgPrint("appledcp: %lux%lu BGRA surface at %I64x (device address %I64x), on the panel at the first frame\n",
             Context->Width, Context->Height, Context->SurfacePhysical.QuadPart, Context->SurfaceIova);
    return STATUS_SUCCESS;
}

static
VOID
NTAPI
DcpPlatformScanoutWork(
    _In_ PVOID Parameter)
{
    PAPPLE_DCP_CONTEXT Context = Parameter;
    NTSTATUS Status;

    Status = DcpSetSurface(Context->Dcp, &Context->Layer);
    if (!NT_SUCCESS(Status))
        Status = DcpSwapLayer(Context->Dcp, &Context->Layer);
    DbgPrint("appledcp: the panel shows the %lux%lu surface: %lx\n", Context->Width, Context->Height, Status);
    InterlockedExchange(&Context->ScanoutState, DCP_SCANOUT_DONE);
    KeSetEvent(&Context->ScanoutDone, IO_NO_INCREMENT, FALSE);
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
    PAPPLE_DCP_CONTEXT Context;
    SIZE_T WorkingSetSize;
    NTSTATUS Status;

    if (Device == NULL || DxgkInterface == NULL || Config == NULL)
        return STATUS_INVALID_PARAMETER;

    RtlZeroMemory(Config, sizeof(*Config));
    Context = Device->PlatformContext;
    if (!Context)
    {
        Context = ExAllocatePoolZero(NonPagedPool, sizeof(*Context), APPLE_DCP_TAG);
        if (!Context)
            return STATUS_INSUFFICIENT_RESOURCES;
        KeInitializeEvent(&Context->ScanoutDone, NotificationEvent, FALSE);
        ExInitializeWorkItem(&Context->ScanoutWork, DcpPlatformScanoutWork, Context);
        Status = DcpPlatformStart(Context, DxgkInterface);
        if (!NT_SUCCESS(Status))
        {
            DbgPrint("appledcp: the display coprocessor cannot drive the panel: %lx\n", Status);
            DcpPlatformRelease(Context);
            return Status;
        }
        Device->PlatformContext = Context;
    }

    Config->Width = Context->Width;
    Config->Height = Context->Height;
    Config->Format = D3DDDIFMT_X8R8G8B8;
    Config->ScanoutPhysicalAddress = Context->SurfacePhysical;
    Config->ScanoutPitch = Context->Pitch;
    Config->ScanoutSize = Context->SurfaceSize;

    Status = SoftGpu2dComputeAllocationSlabSize(Config->Width,
                                                Config->Height,
                                                APPLE_DCP_WORKING_SURFACE_COUNT,
                                                MAXULONG_PTR,
                                                &WorkingSetSize);
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
    PAPPLE_DCP_CONTEXT Context;

    if (Device == NULL || Device->PlatformContext == NULL)
        return STATUS_SUCCESS;
    Context = Device->PlatformContext;
    Device->PlatformContext = NULL;
    /* FIXME: the panel keeps scanning this surface after the adapter stops; the boot framebuffer is not restored. */
    DcpPlatformRelease(Context);
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
    PAPPLE_DCP_CONTEXT Context = Device->PlatformContext;

    UNREFERENCED_PARAMETER(Rect);
    KeMemoryBarrier();
    if (Context &&
        InterlockedCompareExchange(&Context->ScanoutState, DCP_SCANOUT_QUEUED, DCP_SCANOUT_PENDING) ==
            DCP_SCANOUT_PENDING)
    {
        ExQueueWorkItem(&Context->ScanoutWork, DelayedWorkQueue);
    }
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
    PAPPLE_DCP_CONTEXT Context = Device->PlatformContext;
    NTSTATUS Status;

    ASSERT(KeGetCurrentIrql() == PASSIVE_LEVEL);
    if (!Context || !Context->Dcp)
        return STATUS_DEVICE_NOT_READY;
    Status = DcpSetPower(Context->Dcp, On);
    if (NT_SUCCESS(Status) && On &&
        InterlockedCompareExchange(&Context->ScanoutState, DCP_SCANOUT_DONE, DCP_SCANOUT_DONE) == DCP_SCANOUT_DONE)
    {
        Status = DcpSwapLayer(Context->Dcp, &Context->Layer);
    }
    return Status;
}

NTSTATUS
SoftGpuPlatformQueryDescriptor(
    _In_ PSOFTGPU_DEVICE Device,
    _Inout_ PDXGK_DEVICE_DESCRIPTOR DeviceDescriptor)
{
    UNREFERENCED_PARAMETER(Device);
    UNREFERENCED_PARAMETER(DeviceDescriptor);
    return STATUS_MONITOR_NO_DESCRIPTOR;
}
