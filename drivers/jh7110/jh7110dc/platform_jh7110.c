/*
 * PROJECT:     LiberNT StarFive JH7110 display miniport
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     JH7110 DC8200 and HDMI platform provider for the software GPU engine
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "softgpu.h"
#include "softgpu_2d_core.h"
#include "jh7110dc.h"

static VOID
JhStall(_In_ ULONG Microseconds)
{
    KeStallExecutionProcessor(Microseconds);
}

static VOID
JhSleep(_In_ ULONG Microseconds)
{
    LARGE_INTEGER Delay;

    Delay.QuadPart = -(LONGLONG)Microseconds * 10;
    KeDelayExecutionThread(KernelMode, FALSE, &Delay);
}

static VOID
JhFlushScanout(_In_ PJH7110DC_CONTEXT Context, _In_ SIZE_T Offset, _In_ SIZE_T Length)
{
    Jh7110DispFlushCache(Context->Ccache, Context->ScanoutPhysical.QuadPart + Offset, Length);
}

static VOID NTAPI
JhHotPlugThread(_In_ PVOID Parameter)
{
    PJH7110DC_CONTEXT Context = Parameter;
    LARGE_INTEGER Interval;
    BOOLEAN Connected;

    Interval.QuadPart = -(LONGLONG)JH_HDMI_HOTPLUG_POLL_MS * 10000;
    while (KeWaitForSingleObject(&Context->HotPlugStop, Executive, KernelMode, FALSE, &Interval) == STATUS_TIMEOUT)
    {
        Connected = Jh7110DispHotPlug(&Context->Display);
        if (Connected && !Context->HotPlug)
        {
            if (NT_SUCCESS(Jh7110DispStartHdmi(&Context->Display)))
                DPRINT1("JH7110DC: HDMI sink connected, link restarted\n");
        }
        Context->HotPlug = Connected;
    }
    PsTerminateSystemThread(STATUS_SUCCESS);
}

static VOID
JhStartHotPlugMonitor(_In_ PJH7110DC_CONTEXT Context)
{
    HANDLE Thread;

    KeInitializeEvent(&Context->HotPlugStop, NotificationEvent, FALSE);
    Context->HotPlug = Jh7110DispHotPlug(&Context->Display);
    if (!NT_SUCCESS(PsCreateSystemThread(&Thread, THREAD_ALL_ACCESS, NULL, NULL, NULL, JhHotPlugThread, Context)))
        return;
    if (!NT_SUCCESS(ObReferenceObjectByHandle(Thread, SYNCHRONIZE, *PsThreadType, KernelMode,
                                              &Context->HotPlugThread, NULL)))
    {
        Context->HotPlugThread = NULL;
    }
    ZwClose(Thread);
}

static VOID
JhStopHotPlugMonitor(_In_ PJH7110DC_CONTEXT Context)
{
    if (!Context->HotPlugThread)
        return;
    KeSetEvent(&Context->HotPlugStop, IO_NO_INCREMENT, FALSE);
    KeWaitForSingleObject(Context->HotPlugThread, Executive, KernelMode, FALSE, NULL);
    ObDereferenceObject(Context->HotPlugThread);
    Context->HotPlugThread = NULL;
}

static VOID
JhDisplayStop(_In_ PJH7110DC_CONTEXT Context)
{
    JhStopHotPlugMonitor(Context);
    if (!Context->Running)
        return;
    Jh7110DispStop(&Context->Display);
    Context->Running = FALSE;
}

static VOID
JhRelease(_In_ PJH7110DC_CONTEXT Context)
{
    PJH7110DISP Display = &Context->Display;

    JhDisplayStop(Context);
    if (Context->Scanout)
        MmFreeContiguousMemorySpecifyCache(Context->Scanout, Context->ScanoutSize, MmWriteCombined);
    if (Display->Hdmi)
        MmUnmapIoSpace(Display->Hdmi, JH_HDMI_SIZE);
    if (Display->Dc)
        MmUnmapIoSpace(Display->Dc, JH_DC_SIZE);
    if (Display->VoutCrg)
        MmUnmapIoSpace(Display->VoutCrg, JH_VOUTCRG_SIZE);
    if (Display->SysCrg)
        MmUnmapIoSpace(Display->SysCrg, JH_SYSCRG_SIZE);
    if (Display->Pmu)
        MmUnmapIoSpace(Display->Pmu, JH_PMU_SIZE);
    if (Context->Ccache)
        MmUnmapIoSpace(Context->Ccache, JH_CCACHE_SIZE);
    ExFreePoolWithTag(Context, JH7110DC_TAG);
}

static PUCHAR
JhMap(_In_ ULONGLONG Base, _In_ SIZE_T Size)
{
    PHYSICAL_ADDRESS Address;

    Address.QuadPart = Base;
    return MmMapIoSpace(Address, Size, MmNonCached);
}

static BOOLEAN
JhTakeOver(_Inout_ PJH7110DC_CONTEXT Context)
{
    PJH7110DISP Display = &Context->Display;
    const JH7110DISP_MODE *Mode = Display->Mode;
    PHYSICAL_ADDRESS Address;
    PVOID Firmware;
    SIZE_T Length;

    if (!Jh7110DispIsScanningOut(Display) ||
        Jh7110DispRead(Display->Dc, JH_DC_DISPLAY_H) != (Mode->Width | (Mode->HTotal << JH_DC_TOTAL_SHIFT)) ||
        Jh7110DispRead(Display->Dc, JH_DC_DISPLAY_V) != (Mode->Height | (Mode->VTotal << JH_DC_TOTAL_SHIFT)) ||
        Jh7110DispRead(Display->Dc, JH_DC_FB_STRIDE) != Context->Pitch)
    {
        return FALSE;
    }

    Address.QuadPart = Jh7110DispRead(Display->Dc, JH_DC_FB_ADDRESS);
    Length = (SIZE_T)Context->Pitch * Mode->Height;
    Firmware = MmMapIoSpace(Address, Length, MmWriteCombined);
    if (Firmware)
    {
        RtlCopyMemory(Context->Scanout, Firmware, Length);
        MmUnmapIoSpace(Firmware, Length);
    }
    KeMemoryBarrier();
    JhFlushScanout(Context, 0, Context->ScanoutSize);
    Jh7110DispSetFrameBuffer(Display, Context->ScanoutPhysical.LowPart, Context->Pitch);
    DPRINT1("JH7110DC: took over the running display from 0x%I64x\n", Address.QuadPart);
    return TRUE;
}

static NTSTATUS
JhStart(_Inout_ PJH7110DC_CONTEXT Context)
{
    PJH7110DISP Display = &Context->Display;
    PHYSICAL_ADDRESS Low, High, Boundary;
    NTSTATUS Status;

    Context->Ccache = JhMap(JH_CCACHE_BASE, JH_CCACHE_SIZE);
    Display->Pmu = JhMap(JH_PMU_BASE, JH_PMU_SIZE);
    Display->SysCrg = JhMap(JH_SYSCRG_BASE, JH_SYSCRG_SIZE);
    Display->VoutCrg = JhMap(JH_VOUTCRG_BASE, JH_VOUTCRG_SIZE);
    Display->Dc = JhMap(JH_DC_BASE, JH_DC_SIZE);
    Display->Hdmi = JhMap(JH_HDMI_BASE, JH_HDMI_SIZE);
    if (!Context->Ccache || !Display->Pmu || !Display->SysCrg || !Display->VoutCrg || !Display->Dc || !Display->Hdmi)
        return STATUS_INSUFFICIENT_RESOURCES;

    Context->Pitch = Display->Mode->Width * SOFTGPU_DISPLAY_BYTES_PER_PIXEL;
    Context->ScanoutSize = ROUND_TO_PAGES((SIZE_T)Context->Pitch * Display->Mode->Height);
    Low.QuadPart = 0;
    High.QuadPart = JH_DC_HIGHEST_ADDRESS;
    Boundary.QuadPart = 0;
    Context->Scanout = MmAllocateContiguousMemorySpecifyCache(Context->ScanoutSize, Low, High, Boundary,
                                                              MmWriteCombined);
    if (!Context->Scanout)
        return STATUS_INSUFFICIENT_RESOURCES;
    RtlZeroMemory(Context->Scanout, Context->ScanoutSize);
    Context->ScanoutPhysical = MmGetPhysicalAddress(Context->Scanout);

    if (!JhTakeOver(Context))
    {
        JhFlushScanout(Context, 0, Context->ScanoutSize);
        Status = Jh7110DispStart(Display, Context->ScanoutPhysical.LowPart, Context->Pitch);
        if (!NT_SUCCESS(Status))
            return Status;
    }
    Context->Running = TRUE;
    JhStartHotPlugMonitor(Context);

    DPRINT1("JH7110DC: HDMI %lux%lu DC8200 revision 0x%lx cid 0x%lx scanout 0x%I64x hotplug %lu\n",
            Display->Mode->Width, Display->Mode->Height, Jh7110DispRead(Display->Dc, JH_DC_HW_REVISION),
            Jh7110DispRead(Display->Dc, JH_DC_HW_CHIP_CID), Context->ScanoutPhysical.QuadPart,
            Jh7110DispHotPlug(Display));
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
    PJH7110DC_CONTEXT Context;
    SIZE_T WorkingSetSize;
    NTSTATUS Status;

    if (Device == NULL || DxgkInterface == NULL || Config == NULL)
        return STATUS_INVALID_PARAMETER;

    RtlZeroMemory(Config, sizeof(*Config));
    Context = Device->PlatformContext;
    if (!Context)
    {
        Context = ExAllocatePoolZero(NonPagedPool, sizeof(*Context), JH7110DC_TAG);
        if (!Context)
            return STATUS_INSUFFICIENT_RESOURCES;
        Context->Display.Mode = &Jh7110DispMode1080p60;
        Context->Display.Stall = JhStall;
        Context->Display.Sleep = JhSleep;
        Status = JhStart(Context);
        if (!NT_SUCCESS(Status))
        {
            DPRINT1("JH7110DC: display start failed 0x%08lx\n", Status);
            JhRelease(Context);
            return Status;
        }
        Device->PlatformContext = Context;
    }

    Config->Width = Context->Display.Mode->Width;
    Config->Height = Context->Display.Mode->Height;
    Config->Format = D3DDDIFMT_X8R8G8B8;
    Config->ScanoutPhysicalAddress = Context->ScanoutPhysical;
    Config->ScanoutPitch = Context->Pitch;
    Config->ScanoutSize = Context->ScanoutSize;

    Status = SoftGpu2dComputeAllocationSlabSize(Config->Width,
                                                Config->Height,
                                                JH7110DC_WORKING_SURFACE_COUNT,
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
    PJH7110DC_CONTEXT Context;

    if (Device == NULL || Device->PlatformContext == NULL)
        return STATUS_SUCCESS;
    Context = Device->PlatformContext;
    Device->PlatformContext = NULL;
    JhRelease(Context);
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
    PJH7110DC_CONTEXT Context;
    LONG Left, Top, Right, Bottom, Row;

    if (Device == NULL || Rect == NULL || Device->PlatformContext == NULL)
        return;
    Context = Device->PlatformContext;
    Left = max(Rect->left, 0);
    Top = max(Rect->top, 0);
    Right = min(Rect->right, (LONG)Context->Display.Mode->Width);
    Bottom = min(Rect->bottom, (LONG)Context->Display.Mode->Height);
    if (Left >= Right || Top >= Bottom)
        return;

    KeMemoryBarrier();
    if (Left == 0 && Right == (LONG)Context->Display.Mode->Width)
    {
        JhFlushScanout(Context, (SIZE_T)Top * Context->Pitch, (SIZE_T)(Bottom - Top) * Context->Pitch);
        return;
    }
    for (Row = Top; Row < Bottom; ++Row)
    {
        JhFlushScanout(Context,
                       (SIZE_T)Row * Context->Pitch + (SIZE_T)Left * SOFTGPU_DISPLAY_BYTES_PER_PIXEL,
                       (SIZE_T)(Right - Left) * SOFTGPU_DISPLAY_BYTES_PER_PIXEL);
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
SoftGpuPlatformQueryDescriptor(
    _In_ PSOFTGPU_DEVICE Device,
    _Inout_ PDXGK_DEVICE_DESCRIPTOR DeviceDescriptor)
{
    UNREFERENCED_PARAMETER(Device);
    UNREFERENCED_PARAMETER(DeviceDescriptor);
    return STATUS_MONITOR_NO_DESCRIPTOR;
}
