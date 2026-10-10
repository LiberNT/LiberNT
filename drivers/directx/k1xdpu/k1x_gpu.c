/*
 * PROJECT:     LiberNT SpacemiT K1 display miniport
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     PowerVR GPU hosting, its escape interface and scanout buffers
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "softgpu.h"
#include "k1xdpu.h"

#define K1X_GPU_CORE_ID_PBVNC   0x0020U
#define K1X_GPU_LINUX_ENOMEM    12
#define K1X_GPU_DRAIN_POLL_MS   10
#define K1X_GPU_DRAIN_POLLS     100

static WORKER_THREAD_ROUTINE K1xScanoutFreeWorker;

static VOID
NTAPI
K1xScanoutFreeWorker(_In_ PVOID Parameter)
{
    PK1X_SCANOUT Buffer = Parameter;
    PK1XDPU_CONTEXT Context = Buffer->Context;

    MmFreeContiguousMemorySpecifyCache(Buffer->Address, Buffer->Size, MmWriteCombined);
    ExFreePoolWithTag(Buffer, K1XDPU_TAG);
    InterlockedDecrement(&Context->ScanoutCount);
}

VOID
K1xScanoutReference(_Inout_ PK1X_SCANOUT Buffer)
{
    InterlockedIncrement(&Buffer->References);
}

VOID
K1xScanoutDereference(_Inout_ PK1X_SCANOUT Buffer)
{
    if (InterlockedDecrement(&Buffer->References) != 0)
        return;
    Buffer->Magic = 0;
    if (KeGetCurrentIrql() == PASSIVE_LEVEL)
        K1xScanoutFreeWorker(Buffer);
    else
        ExQueueWorkItem(&Buffer->FreeItem, DelayedWorkQueue);
}

static void
K1xScanoutGemRelease(void *Parameter)
{
    K1xScanoutDereference(Parameter);
}

static void
K1xScanoutGemAcquire(void *Parameter)
{
    K1xScanoutReference(Parameter);
}

static NTSTATUS
K1xScanoutCreate(
    _Inout_ PK1XDPU_CONTEXT Context,
    _In_ PVOID File,
    _Inout_ POWERVR_SCANOUT_CREATE_ESCAPE *Create)
{
    PSOFTGPU_DEVICE Device = Context->Device;
    PHYSICAL_ADDRESS Low, High, Skip;
    ULONG64 Limit;
    PK1X_SCANOUT Buffer;
    uint32_t Handle;
    SIZE_T Size;
    int Error;

    if (!Context->PlanesReady)
        return STATUS_NOT_SUPPORTED;
    Limit = 2 * ROUND_TO_PAGES((ULONG64)Device->Width * Device->Height * sizeof(ULONG));
    if (!Create->Size || Create->Size > Limit)
        return STATUS_INVALID_PARAMETER;
    Size = ROUND_TO_PAGES((SIZE_T)Create->Size);

    if (InterlockedIncrement(&Context->ScanoutCount) > K1X_SCANOUT_LIMIT)
    {
        InterlockedDecrement(&Context->ScanoutCount);
        return STATUS_QUOTA_EXCEEDED;
    }
    Buffer = ExAllocatePoolZero(NonPagedPool, sizeof(*Buffer), K1XDPU_TAG);
    if (!Buffer)
    {
        InterlockedDecrement(&Context->ScanoutCount);
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    Low.QuadPart = 0;
    High.QuadPart = K1X_SCANOUT_HIGHEST_ADDRESS;
    Skip.QuadPart = 0;
    Buffer->Address = MmAllocateContiguousMemorySpecifyCache(Size, Low, High, Skip, MmWriteCombined);
    if (!Buffer->Address)
    {
        ExFreePoolWithTag(Buffer, K1XDPU_TAG);
        InterlockedDecrement(&Context->ScanoutCount);
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    RtlZeroMemory(Buffer->Address, Size);
    KeMemoryBarrier();
    Buffer->Magic = K1X_SCANOUT_MAGIC;
    Buffer->References = 1;
    Buffer->Context = Context;
    Buffer->Physical = MmGetPhysicalAddress(Buffer->Address);
    Buffer->Size = Size;
    ExInitializeWorkItem(&Buffer->FreeItem, K1xScanoutFreeWorker, Buffer);

    Error = pvr_glue_import(File, (uint64_t)Buffer->Physical.QuadPart, Size, K1xScanoutGemRelease, Buffer, &Handle);
    if (Error)
        return Error == -K1X_GPU_LINUX_ENOMEM ? STATUS_INSUFFICIENT_RESOURCES : STATUS_UNSUCCESSFUL;
    Create->Handle = Handle;
    Create->Size = Size;
    return STATUS_SUCCESS;
}

static NTSTATUS
K1xScanoutPresent(
    _Inout_ PK1XDPU_CONTEXT Context,
    _In_ PVOID Owner,
    _In_ PVOID File,
    _Inout_ POWERVR_SCANOUT_PRESENT_ESCAPE *Present)
{
    PSOFTGPU_DEVICE Device = Context->Device;
    PK1X_SCANOUT Buffer;
    PK1X_FLIP Flip;

    if (!Context->PlanesReady)
        return STATUS_NOT_SUPPORTED;
    if (Present->Handle == POWERVR_SCANOUT_HIDE || Present->Handle == POWERVR_SCANOUT_QUERY)
    {
        if (Present->Handle == POWERVR_SCANOUT_HIDE)
            K1xOverlayHide(Context, Owner);
        K1xOverlayBusy(Context, Owner, Present->Busy);
        return STATUS_SUCCESS;
    }

    if (!Present->Width || !Present->Height || Present->Left < 0 || Present->Top < 0 ||
        Present->Width > Device->Width || Present->Height > Device->Height ||
        (ULONG)Present->Left > Device->Width - Present->Width ||
        (ULONG)Present->Top > Device->Height - Present->Height ||
        (Present->Pitch & (sizeof(ULONG) - 1)) || Present->Pitch > K1X_CHANNEL_STRIDE_MAX ||
        Present->Pitch / sizeof(ULONG) < Present->Width ||
        Present->Width * sizeof(ULONG) > K1X_LINE_MEMORY_PAIR0_BYTES - K1X_CURSOR_SIZE * sizeof(ULONG))
    {
        return STATUS_INVALID_PARAMETER;
    }
    Buffer = pvr_glue_import_acquire(File, Present->Handle, K1xScanoutGemRelease, K1xScanoutGemAcquire);
    if (!Buffer)
        return STATUS_INVALID_HANDLE;
    if (Buffer->Magic != K1X_SCANOUT_MAGIC || Buffer->Context != Context ||
        (ULONG64)(Present->Height - 1) * Present->Pitch + (ULONG64)Present->Width * sizeof(ULONG) > Buffer->Size)
    {
        K1xScanoutDereference(Buffer);
        return STATUS_INVALID_PARAMETER;
    }

    Flip = ExAllocatePoolZero(NonPagedPool, sizeof(*Flip), K1XDPU_TAG);
    if (!Flip)
    {
        K1xScanoutDereference(Buffer);
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    Flip->Buffer = Buffer;
    Flip->Owner = Owner;
    Flip->Handle = Present->Handle;
    Flip->Left = Present->Left;
    Flip->Top = Present->Top;
    Flip->Width = Present->Width;
    Flip->Height = Present->Height;
    Flip->Pitch = Present->Pitch;
    Flip->Fence = pvr_glue_syncobj_fence(File, Present->SyncObject);
    if (!K1xOverlayQueue(Context, Flip))
    {
        K1xFlipDestroy(Flip);
        return STATUS_DEVICE_BUSY;
    }
    if (Present->Flags & POWERVR_SCANOUT_PRESENT_WAIT)
        K1xOverlayWait(Context, Flip);
    K1xOverlayBusy(Context, Owner, Present->Busy);
    return STATUS_SUCCESS;
}

static NTSTATUS
K1xGpuFile(
    _Inout_ PK1XDPU_CONTEXT Context,
    _In_ CONST DXGKARG_ESCAPE *Escape,
    _Out_ PSOFTGPU_KMD_DEVICE *Owner,
    _Out_ PVOID *File)
{
    PSOFTGPU_KMD_DEVICE KmdDevice = (PSOFTGPU_KMD_DEVICE)Escape->hDevice;
    PK1X_GPU_DEVICE GpuDevice;
    NTSTATUS Status = STATUS_SUCCESS;

    *Owner = NULL;
    *File = NULL;
    if (!KmdDevice || KmdDevice->Magic != SOFTGPU_KMD_DEVICE_MAGIC || KmdDevice->Adapter != Context->Device)
        return STATUS_INVALID_PARAMETER;
    if (!Context->GpuCore)
        return STATUS_DEVICE_NOT_READY;

    ExAcquireFastMutex(&Context->GpuMutex);
    GpuDevice = KmdDevice->PlatformDevice;
    if (!GpuDevice)
    {
        GpuDevice = ExAllocatePoolZero(NonPagedPool, sizeof(*GpuDevice), K1XDPU_TAG);
        if (!GpuDevice)
        {
            Status = STATUS_INSUFFICIENT_RESOURCES;
        }
        else if (pvr_glue_open(Context->GpuCore, &GpuDevice->File))
        {
            ExFreePoolWithTag(GpuDevice, K1XDPU_TAG);
            GpuDevice = NULL;
            Status = STATUS_INSUFFICIENT_RESOURCES;
        }
        else
        {
            GpuDevice->Magic = K1X_GPU_DEVICE_MAGIC;
            KmdDevice->PlatformDevice = GpuDevice;
        }
    }
    ExReleaseFastMutex(&Context->GpuMutex);
    if (!NT_SUCCESS(Status))
        return Status;
    *Owner = KmdDevice;
    *File = GpuDevice->File;
    return STATUS_SUCCESS;
}

NTSTATUS
K1xGpuEscape(
    _Inout_ PK1XDPU_CONTEXT Context,
    _In_ CONST DXGKARG_ESCAPE *Escape)
{
    POWERVR_ESCAPE_HEADER *Header;
    PSOFTGPU_KMD_DEVICE Owner;
    NTSTATUS Status;
    PVOID File;

    if (!Escape->pPrivateDriverData || Escape->PrivateDriverDataSize < sizeof(*Header))
        return STATUS_NOT_SUPPORTED;
    Header = Escape->pPrivateDriverData;
    if (Header->Magic != POWERVR_ESCAPE_MAGIC)
        return STATUS_NOT_SUPPORTED;
    if (Header->AbiVersion != POWERVR_ESCAPE_ABI_VERSION || Header->Size > Escape->PrivateDriverDataSize)
        return STATUS_INVALID_PARAMETER;

    if (Header->Command == POWERVR_ESCAPE_QUERY_INFO)
    {
        POWERVR_ESCAPE_INFO *Info = (POWERVR_ESCAPE_INFO *)Header;
        struct pvr_glue_info Core;

        if (Header->Size < sizeof(*Info))
            return STATUS_BUFFER_TOO_SMALL;
        Info->Branch = Context->GpuBranch;
        Info->Version = Context->GpuVersion;
        Info->NumberOfScalableUnits = Context->GpuScalableUnits;
        Info->Config = Context->GpuConfig;
        Info->FirmwareState = 0;
        Info->FirmwareVersion = 0;
        if (Context->GpuCore && !pvr_glue_query(Context->GpuCore, &Core))
        {
            Info->FirmwareState = Core.firmware_booted;
            Info->FirmwareVersion = (Core.firmware_major << 16) | (Core.firmware_minor & 0xFFFF);
        }
        Info->CoreClockHz = POWERVR_K1_CORE_CLOCK_HZ;
        Info->Caps = (Context->GpuCore && Context->PlanesReady) ? POWERVR_CAP_SCANOUT : 0;
        Info->ScreenWidth = Context->Device->Width;
        Info->ScreenHeight = Context->Device->Height;
        Info->Reserved = 0;
        Header->Status = STATUS_SUCCESS;
        return STATUS_SUCCESS;
    }

    Status = K1xGpuFile(Context, Escape, &Owner, &File);
    if (!NT_SUCCESS(Status))
        return Status;

    switch (Header->Command)
    {
        case POWERVR_ESCAPE_DRM_IOCTL:
        {
            POWERVR_DRM_IOCTL_ESCAPE *Ioctl = (POWERVR_DRM_IOCTL_ESCAPE *)Header;

            if (Header->Size < sizeof(*Ioctl))
                return STATUS_INVALID_PARAMETER;
            Header->Status = (LONG)pvr_glue_ioctl(File, Ioctl->Command, (PVOID)(ULONG_PTR)Ioctl->Argument,
                                                  Ioctl->ArgumentSize);
            return STATUS_SUCCESS;
        }

        case POWERVR_ESCAPE_SYNC_MERGE:
        {
            POWERVR_SYNC_MERGE_ESCAPE *Merge = (POWERVR_SYNC_MERGE_ESCAPE *)Header;

            if (Header->Size < sizeof(*Merge) || Merge->Count > POWERVR_SYNC_MERGE_COUNT)
                return STATUS_INVALID_PARAMETER;
            Header->Status = (LONG)pvr_glue_sync_merge(File, Merge->Destination, Merge->DestinationPoint, Merge->Count,
                                                       (const uint32_t *)Merge->Handles, Merge->Points);
            return STATUS_SUCCESS;
        }

        case POWERVR_ESCAPE_DRM_MMAP:
        case POWERVR_ESCAPE_DRM_MUNMAP:
        {
            POWERVR_DRM_MAP_ESCAPE *Map = (POWERVR_DRM_MAP_ESCAPE *)Header;
            uint64_t Address = 0;

            if (Header->Size < sizeof(*Map))
                return STATUS_INVALID_PARAMETER;
            if (Header->Command == POWERVR_ESCAPE_DRM_MMAP)
            {
                Header->Status = pvr_glue_mmap(File, Map->Offset, Map->Length, &Address);
                Map->Address = Address;
            }
            else
            {
                Header->Status = pvr_glue_munmap(File, Map->Address);
            }
            return STATUS_SUCCESS;
        }

        case POWERVR_ESCAPE_SCANOUT_CREATE:
            if (Header->Size < sizeof(POWERVR_SCANOUT_CREATE_ESCAPE))
                return STATUS_INVALID_PARAMETER;
            Header->Status = K1xScanoutCreate(Context, File, (POWERVR_SCANOUT_CREATE_ESCAPE *)Header);
            return STATUS_SUCCESS;

        case POWERVR_ESCAPE_SCANOUT_PRESENT:
            if (Header->Size < sizeof(POWERVR_SCANOUT_PRESENT_ESCAPE))
                return STATUS_INVALID_PARAMETER;
            Header->Status = K1xScanoutPresent(Context, Owner, File, (POWERVR_SCANOUT_PRESENT_ESCAPE *)Header);
            return STATUS_SUCCESS;

        default:
            Header->Status = STATUS_NOT_SUPPORTED;
            return STATUS_SUCCESS;
    }
}

NTSTATUS
K1xGpuStart(
    _Inout_ PK1XDPU_CONTEXT Context,
    _In_ const DXGK_DEVICE_INFO *Information)
{
    PCM_PARTIAL_RESOURCE_DESCRIPTOR Memory, Interrupt = NULL;
    struct pvr_glue_platform Platform;
    ULONG64 CoreId;
    NTSTATUS Status;
    int Error;

    PAGED_CODE();

    Memory = K1xFindResource(Information, CmResourceTypeMemory,
                             K1xFindName(Information->PhysicalDeviceObject, L"reg-names", "gpu"));
    if (K1xFindName(Information->PhysicalDeviceObject, L"interrupt-names", "gpu") == 0)
        Interrupt = K1xFindResource(Information, CmResourceTypeInterrupt, 0);
    if (!Memory || !Interrupt)
    {
        DPRINT1("K1XDPU: no GPU resources on the display node, 3D disabled\n");
        return STATUS_NOT_SUPPORTED;
    }

    Context->GpuRegistersPhysical = Memory->u.Memory.Start;
    Context->GpuRegistersLength = Memory->u.Memory.Length;
    Context->GpuRegisters = MmMapIoSpace(Context->GpuRegistersPhysical, Context->GpuRegistersLength, MmNonCached);
    if (!Context->GpuRegisters)
        return STATUS_INSUFFICIENT_RESOURCES;

    Status = PowerVrK1PowerUp(&Context->GpuPlatformRegisters);
    if (!NT_SUCCESS(Status))
        goto Fail;
    Context->GpuPowered = TRUE;

    KeMemoryBarrier();
    CoreId = *(volatile ULONG64 *)(Context->GpuRegisters + K1X_GPU_CORE_ID_PBVNC);
    KeMemoryBarrier();
    Context->GpuBranch = (USHORT)(CoreId >> 48);
    Context->GpuVersion = (USHORT)(CoreId >> 32);
    Context->GpuScalableUnits = (USHORT)(CoreId >> 16);
    Context->GpuConfig = (USHORT)CoreId;
    DPRINT1("K1XDPU: PowerVR GPU BVNC %u.%u.%u.%u\n", Context->GpuBranch, Context->GpuVersion,
            Context->GpuScalableUnits, Context->GpuConfig);

    if (pvr_glue_driver_init())
    {
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto Fail;
    }
    Context->GpuDriverInitialized = TRUE;

    RtlZeroMemory(&Platform, sizeof(Platform));
    Platform.registers_physical = (uint64_t)Context->GpuRegistersPhysical.QuadPart;
    Platform.registers_length = Context->GpuRegistersLength;
    Platform.interrupt_vector = Interrupt->u.Interrupt.Vector;
    Platform.interrupt_irql = (uint8_t)Interrupt->u.Interrupt.Level;
    Platform.interrupt_level_sensitive = !(Interrupt->Flags & CM_RESOURCE_INTERRUPT_LATCHED);
    Platform.core_clock_hz = POWERVR_K1_CORE_CLOCK_HZ;
    Platform.dma_limit = K1X_SCANOUT_HIGHEST_ADDRESS;
    Platform.compatible = PowerVrK1Compatible;
    Platform.compatible_count = POWERVR_K1_COMPATIBLE_COUNT;
    Error = pvr_glue_probe(&Platform, &Context->GpuCore);
    if (Error)
    {
        DPRINT1("K1XDPU: PowerVR core probe failed (%d)\n", Error);
        Status = STATUS_DEVICE_CONFIGURATION_ERROR;
        goto Fail;
    }
    return STATUS_SUCCESS;

Fail:
    K1xGpuStop(Context);
    return Status;
}

VOID
K1xGpuStop(_Inout_ PK1XDPU_CONTEXT Context)
{
    LARGE_INTEGER Interval;
    ULONG Poll;

    PAGED_CODE();

    if (Context->GpuCore)
    {
        pvr_glue_remove(Context->GpuCore);
        Context->GpuCore = NULL;
    }
    Interval.QuadPart = -10000LL * K1X_GPU_DRAIN_POLL_MS;
    for (Poll = 0; Poll < K1X_GPU_DRAIN_POLLS && InterlockedCompareExchange(&Context->ScanoutCount, 0, 0); ++Poll)
        KeDelayExecutionThread(KernelMode, FALSE, &Interval);
    if (Context->GpuDriverInitialized)
    {
        pvr_glue_driver_exit();
        Context->GpuDriverInitialized = FALSE;
    }
    if (Context->GpuPlatformRegisters)
    {
        PowerVrK1PowerDown(&Context->GpuPlatformRegisters);
        Context->GpuPowered = FALSE;
    }
    if (Context->GpuRegisters)
    {
        MmUnmapIoSpace(Context->GpuRegisters, Context->GpuRegistersLength);
        Context->GpuRegisters = NULL;
    }
}
