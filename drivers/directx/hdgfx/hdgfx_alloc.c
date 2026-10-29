/*
 * PROJECT:     LiberNT Intel HD Graphics Driver
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Allocations and their per-device open bindings
 * COPYRIGHT:   Copyright 2026 Ostap Brehin <osbre@protonmail.com>
 */

#include "hdgfx.h"
#include "softgpu_2d_core.h"

#define HDGFX_ALLOCATION_SEGMENT_SET    (1UL << (SOFTGPU_SEGMENT_ID - 1))

static BOOLEAN
HdgfxDeviceValid(
    _In_opt_ PSOFTGPU_DEVICE Device)
{
    return Device != NULL && Device->Magic == SOFTGPU_DEVICE_MAGIC;
}

static BOOLEAN
HdgfxKmdDeviceValid(
    _In_opt_ PSOFTGPU_KMD_DEVICE KmdDevice)
{
    PSOFTGPU_DEVICE Device;

    if (KmdDevice == NULL || KmdDevice->Magic != SOFTGPU_KMD_DEVICE_MAGIC)
        return FALSE;
    Device = KmdDevice->Adapter;
    return HdgfxDeviceValid(Device) && KmdDevice->Process != NULL &&
           KmdDevice->Process->Magic == SOFTGPU_PROCESS_MAGIC && KmdDevice->Process->Adapter == Device;
}

static VOID
HdgfxFreeAllocation(
    _In_ PSOFTGPU_ALLOC Alloc)
{
    Alloc->Magic = 0xDEADA110UL;
    ExFreePoolWithTag(Alloc, HDGFX_TAG);
}

static VOID
HdgfxFreeOpen(
    _In_ PSOFTGPU_OPENALLOC Open)
{
    Open->Magic = 0xDEAD0A11UL;
    Open->Device = NULL;
    ExFreePoolWithTag(Open, HDGFX_TAG);
}

static VOID
HdgfxCopyGeometry(
    _In_ const SOFTGPU_ALLOCATION_PRIVATE_DATA *PrivateData,
    _Out_ PULONG Width,
    _Out_ PULONG Height,
    _Out_ PULONG Pitch,
    _Out_ PULONG StorageHeight,
    _Out_ D3DDDIFORMAT *Format,
    _Out_ PULONG PlaneCount,
    _Out_writes_(SOFTGPU_ALLOCATION_MAX_PLANES) PULONG PlaneOffsets,
    _Out_writes_(SOFTGPU_ALLOCATION_MAX_PLANES) PULONG PlanePitches)
{
    *Width = PrivateData->Width;
    *Height = PrivateData->Height;
    *Pitch = PrivateData->Pitch;
    *StorageHeight = PrivateData->StorageHeight;
    *Format = PrivateData->Format;
    *PlaneCount = PrivateData->PlaneCount;
    RtlCopyMemory(PlaneOffsets, PrivateData->PlaneOffsets, sizeof(PrivateData->PlaneOffsets));
    RtlCopyMemory(PlanePitches, PrivateData->PlanePitches, sizeof(PrivateData->PlanePitches));
}

NTSTATUS
APIENTRY
HdgfxDdiGetStandardAllocationDriverData(
    _In_ PVOID MiniportDeviceContext,
    _Inout_ PDXGKARG_GETSTANDARDALLOCATIONDRIVERDATA Data)
{
    PSOFTGPU_DEVICE Device = MiniportDeviceContext;
    SOFTGPU_ALLOCATION_PRIVATE_DATA PrivateData;
    UINT SuppliedSize;
    ULONG Width;
    ULONG Height;
    ULONG Pitch = 0;
    D3DDDIFORMAT Format = D3DDDIFMT_X8R8G8B8;
    UINT *PitchOut = NULL;

    if (!HdgfxDeviceValid(Device) || Data == NULL)
        return STATUS_INVALID_PARAMETER;

    switch (Data->StandardAllocationType)
    {
        case DXGK_STDALLOCATION_SHAREDPRIMARYSURFACE:
            if (Data->pCreateSharedPrimarySurfaceData == NULL ||
                Data->pCreateSharedPrimarySurfaceData->VidPnSourceId != 0)
            {
                return STATUS_INVALID_PARAMETER;
            }
            Width = Data->pCreateSharedPrimarySurfaceData->Width;
            Height = Data->pCreateSharedPrimarySurfaceData->Height;
            Format = Data->pCreateSharedPrimarySurfaceData->Format;
            break;

        case DXGK_STDALLOCATION_SHADOWSURFACE:
            if (Data->pCreateShadowSurfaceData == NULL)
                return STATUS_INVALID_PARAMETER;
            Width = Data->pCreateShadowSurfaceData->Width;
            Height = Data->pCreateShadowSurfaceData->Height;
            Format = Data->pCreateShadowSurfaceData->Format;
            PitchOut = &Data->pCreateShadowSurfaceData->Pitch;
            break;

        case DXGK_STDALLOCATION_STAGINGSURFACE:
            if (Data->pCreateStagingSurfaceData == NULL)
                return STATUS_INVALID_PARAMETER;
            Width = Data->pCreateStagingSurfaceData->Width;
            Height = Data->pCreateStagingSurfaceData->Height;
            PitchOut = &Data->pCreateStagingSurfaceData->Pitch;
            break;

        case DXGK_STDALLOCATION_GDISURFACE:
            if (Data->pCreateGdiSurfaceData == NULL)
                return STATUS_INVALID_PARAMETER;
            Width = Data->pCreateGdiSurfaceData->Width;
            Height = Data->pCreateGdiSurfaceData->Height;
            Format = Data->pCreateGdiSurfaceData->Format;
            PitchOut = &Data->pCreateGdiSurfaceData->Pitch;
            break;

        default:
            return STATUS_NOT_SUPPORTED;
    }

    if (PitchOut != NULL)
    {
        if (Width > MAXULONG / SOFTGPU_DISPLAY_BYTES_PER_PIXEL)
            return STATUS_INTEGER_OVERFLOW;
        *PitchOut = Width * SOFTGPU_DISPLAY_BYTES_PER_PIXEL;
        Pitch = *PitchOut;
    }
    if (Width == 0 || Height == 0 || Width > SOFTGPU_MAX_DISPLAY_WIDTH || Height > SOFTGPU_MAX_DISPLAY_HEIGHT ||
        (Format != D3DDDIFMT_X8R8G8B8 && Format != D3DDDIFMT_A8R8G8B8))
    {
        return STATUS_INVALID_PARAMETER;
    }
    if (Pitch == 0)
        Pitch = Width * SOFTGPU_DISPLAY_BYTES_PER_PIXEL;

    RtlZeroMemory(&PrivateData, sizeof(PrivateData));
    PrivateData.Width = Width;
    PrivateData.Height = Height;
    PrivateData.BitsPerPixel = SOFTGPU_DISPLAY_BITS_PER_PIXEL;
    PrivateData.Magic = SOFTGPU_ALLOCATION_PRIVATE_MAGIC;
    PrivateData.Version = SOFTGPU_ALLOCATION_PRIVATE_VERSION;
    PrivateData.Pitch = Pitch;
    PrivateData.Format = Format;
    PrivateData.StorageHeight = Height;
    PrivateData.PlaneCount = 1;
    PrivateData.PlanePitches[0] = Pitch;
    if (!SoftGpuAllocationPrivateDataValid(&PrivateData) || (ULONGLONG)Pitch * Height > Device->FrameBufferSize)
        return STATUS_GRAPHICS_NO_VIDEO_MEMORY;

    SuppliedSize = Data->AllocationPrivateDriverDataSize;
    Data->AllocationPrivateDriverDataSize = sizeof(PrivateData);
    Data->ResourcePrivateDriverDataSize = 0;
    if (Data->pAllocationPrivateDriverData == NULL)
        return STATUS_SUCCESS;
    if (SuppliedSize < sizeof(PrivateData))
        return STATUS_BUFFER_TOO_SMALL;
    RtlCopyMemory(Data->pAllocationPrivateDriverData, &PrivateData, sizeof(PrivateData));
    return STATUS_SUCCESS;
}

static NTSTATUS
HdgfxCreateOneAllocation(
    _In_ PSOFTGPU_DEVICE Device,
    _Inout_ DXGK_ALLOCATIONINFO *Info)
{
    SOFTGPU_ALLOCATION_PRIVATE_DATA PrivateData;
    ULONGLONG RequiredSize = 0;
    ULONGLONG Size;
    PSOFTGPU_ALLOC Alloc;

    if (Info->PrivateDriverDataSize != 0 && Info->pPrivateDriverData == NULL)
        return STATUS_INVALID_PARAMETER;
    if (Info->Size > Device->FrameBufferSize)
        return STATUS_GRAPHICS_NO_VIDEO_MEMORY;

    Alloc = ExAllocatePoolZero(NonPagedPool, sizeof(*Alloc), HDGFX_TAG);
    if (Alloc == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;
    Alloc->Magic = SOFTGPU_ALLOC_MAGIC;
    Alloc->Format = D3DDDIFMT_A8R8G8B8;

    if (Info->PrivateDriverDataSize >= SOFTGPU_ALLOCATION_PRIVATE_VERSION_1_SIZE &&
        ((const SOFTGPU_ALLOCATION_PRIVATE_DATA *)Info->pPrivateDriverData)->Magic ==
            SOFTGPU_ALLOCATION_PRIVATE_MAGIC)
    {
        if (!SoftGpuReadAllocationPrivateData(Info->pPrivateDriverData, Info->PrivateDriverDataSize, &PrivateData,
                                              &RequiredSize))
        {
            HdgfxFreeAllocation(Alloc);
            return STATUS_INVALID_PARAMETER;
        }
        HdgfxCopyGeometry(&PrivateData, &Alloc->Width, &Alloc->Height, &Alloc->Pitch, &Alloc->StorageHeight,
                          &Alloc->Format, &Alloc->PlaneCount, Alloc->PlaneOffsets, Alloc->PlanePitches);
    }

    Size = Info->Size != 0 ? Info->Size : PAGE_SIZE;
    if (RequiredSize > Size)
        Size = RequiredSize;
    Size = ROUND_TO_PAGES(Size);
    if (Size > Device->FrameBufferSize)
    {
        HdgfxFreeAllocation(Alloc);
        return STATUS_GRAPHICS_NO_VIDEO_MEMORY;
    }

    Alloc->Size = (SIZE_T)Size;
    Info->Size = Alloc->Size;
    Info->Alignment = PAGE_SIZE;
    Info->SupportedReadSegmentSet = HDGFX_ALLOCATION_SEGMENT_SET;
    Info->SupportedWriteSegmentSet = HDGFX_ALLOCATION_SEGMENT_SET;
    Info->EvictionSegmentSet = 0;
    Info->Flags.CpuVisible = 1;
    Info->Flags.AccessedPhysically = 1;
    Info->hAllocation = Alloc;
    return STATUS_SUCCESS;
}

NTSTATUS
APIENTRY
HdgfxDdiCreateAllocation(
    _In_ PVOID MiniportDeviceContext,
    _Inout_ PDXGKARG_CREATEALLOCATION CreateAllocation)
{
    PSOFTGPU_DEVICE Device = MiniportDeviceContext;
    NTSTATUS Status = STATUS_SUCCESS;
    ULONG Index;

    if (!HdgfxDeviceValid(Device) || CreateAllocation == NULL ||
        (CreateAllocation->NumAllocations != 0 && CreateAllocation->pAllocationInfo == NULL))
    {
        return STATUS_INVALID_PARAMETER;
    }
    if (Device->FrameBuffer == NULL || Device->FrameBufferSize == 0)
        return STATUS_DEVICE_NOT_READY;

    for (Index = 0; Index < CreateAllocation->NumAllocations; Index++)
        CreateAllocation->pAllocationInfo[Index].hAllocation = NULL;
    for (Index = 0; Index < CreateAllocation->NumAllocations; Index++)
    {
        Status = HdgfxCreateOneAllocation(Device, &CreateAllocation->pAllocationInfo[Index]);
        if (!NT_SUCCESS(Status))
            break;
    }
    if (NT_SUCCESS(Status))
        return STATUS_SUCCESS;

    while (Index-- > 0)
    {
        HdgfxFreeAllocation(CreateAllocation->pAllocationInfo[Index].hAllocation);
        CreateAllocation->pAllocationInfo[Index].hAllocation = NULL;
    }
    return Status;
}

NTSTATUS
APIENTRY
HdgfxDdiDestroyAllocation(
    _In_ PVOID MiniportDeviceContext,
    _In_ CONST DXGKARG_DESTROYALLOCATION *DestroyAllocation)
{
    PSOFTGPU_ALLOC Alloc;
    ULONG Index;

    if (!HdgfxDeviceValid(MiniportDeviceContext) || DestroyAllocation == NULL ||
        (DestroyAllocation->NumAllocations != 0 && DestroyAllocation->phAllocation == NULL))
    {
        return STATUS_INVALID_PARAMETER;
    }
    for (Index = 0; Index < DestroyAllocation->NumAllocations; Index++)
    {
        Alloc = DestroyAllocation->phAllocation[Index];
        if (Alloc != NULL && Alloc->Magic != SOFTGPU_ALLOC_MAGIC)
        {
            DPRINT1("DestroyAllocation %lu: bad allocation %p\n", Index, Alloc);
            return STATUS_INVALID_PARAMETER;
        }
    }
    for (Index = 0; Index < DestroyAllocation->NumAllocations; Index++)
    {
        Alloc = DestroyAllocation->phAllocation[Index];
        if (Alloc != NULL)
            HdgfxFreeAllocation(Alloc);
    }
    return STATUS_SUCCESS;
}

NTSTATUS
APIENTRY
HdgfxDdiValidateUpdateAllocationProperty(
    _In_ CONST HANDLE hAdapter,
    _In_ CONST DXGKARG_VALIDATEUPDATEALLOCPROPERTY *Validate)
{
    PSOFTGPU_DEVICE Device = hAdapter;
    PSOFTGPU_OPENALLOC Open;
    ULONG Preferred;

    if (!HdgfxDeviceValid(Device) || Validate == NULL || Validate->hAllocation == NULL ||
        (Validate->PropertyMaskValue & ~0x7UL) != 0 || (Validate->Flags.Value & ~0x1UL) != 0)
    {
        return STATUS_INVALID_PARAMETER;
    }
    Open = Validate->hAllocation;
    if (Open->Magic != SOFTGPU_OPENALLOC_MAGIC || Open->Device == NULL ||
        Open->Device->Magic != SOFTGPU_KMD_DEVICE_MAGIC || Open->Device->Adapter != Device)
    {
        return STATUS_INVALID_HANDLE;
    }
    if (Validate->SetAccessedPhysically && !Validate->Flags.AccessedPhysically)
        return STATUS_NOT_SUPPORTED;
    if (Validate->SetSupportedSegmentSet && Validate->SupportedSegmentSet != HDGFX_ALLOCATION_SEGMENT_SET)
        return STATUS_NOT_SUPPORTED;
    if (Validate->SetPreferredSegment)
    {
        for (Preferred = Validate->PreferredSegment.Value; Preferred != 0; Preferred >>= 6)
        {
            if ((Preferred & 0x1F) != 0 && (Preferred & 0x1F) != SOFTGPU_SEGMENT_ID)
                return STATUS_NOT_SUPPORTED;
        }
    }
    return STATUS_SUCCESS;
}

static NTSTATUS
HdgfxOpenOneAllocation(
    _In_ PSOFTGPU_KMD_DEVICE KmdDevice,
    _Inout_ DXGK_OPENALLOCATIONINFO *Info)
{
    SOFTGPU_ALLOCATION_PRIVATE_DATA PrivateData;
    ULONGLONG RequiredSize;
    PSOFTGPU_OPENALLOC Open;
    UINT LinearSize;

    if (Info->hAllocation == 0 || (Info->PrivateDriverDataSize != 0 && Info->pPrivateDriverData == NULL))
        return STATUS_INVALID_PARAMETER;

    Open = ExAllocatePoolZero(NonPagedPool, sizeof(*Open), HDGFX_TAG);
    if (Open == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;
    Open->Magic = SOFTGPU_OPENALLOC_MAGIC;
    Open->Device = KmdDevice;
    Open->hAllocation = Info->hAllocation;

    if (Info->PrivateDriverDataSize >= SOFTGPU_ALLOCATION_PRIVATE_VERSION_1_SIZE)
    {
        if (((const SOFTGPU_ALLOCATION_PRIVATE_DATA *)Info->pPrivateDriverData)->Magic ==
            SOFTGPU_ALLOCATION_PRIVATE_MAGIC)
        {
            if (!SoftGpuReadAllocationPrivateData(Info->pPrivateDriverData, Info->PrivateDriverDataSize,
                                                  &PrivateData, &RequiredSize))
            {
                HdgfxFreeOpen(Open);
                return STATUS_INVALID_PARAMETER;
            }
            Open->Size = (SIZE_T)RequiredSize;
            HdgfxCopyGeometry(&PrivateData, &Open->Width, &Open->Height, &Open->Pitch, &Open->StorageHeight,
                              &Open->Format, &Open->PlaneCount, Open->PlaneOffsets, Open->PlanePitches);
        }
    }
    else if (Info->PrivateDriverDataSize == sizeof(LinearSize))
    {
        RtlCopyMemory(&LinearSize, Info->pPrivateDriverData, sizeof(LinearSize));
        if (LinearSize == 0 || LinearSize > KmdDevice->Adapter->FrameBufferSize)
        {
            HdgfxFreeOpen(Open);
            return STATUS_INVALID_PARAMETER;
        }
        Open->Size = LinearSize;
    }
    else if (Info->PrivateDriverDataSize == 0)
    {
        Open->Size = PAGE_SIZE;
    }

    Info->hDeviceSpecificAllocation = Open;
    return STATUS_SUCCESS;
}

NTSTATUS
APIENTRY
HdgfxDdiOpenAllocation(
    _In_ PVOID hDevice,
    _In_ CONST DXGKARG_OPENALLOCATION *OpenAllocation)
{
    PSOFTGPU_KMD_DEVICE KmdDevice = hDevice;
    NTSTATUS Status = STATUS_SUCCESS;
    ULONG Index;

    if (KmdDevice == NULL || KmdDevice->Magic != SOFTGPU_KMD_DEVICE_MAGIC || OpenAllocation == NULL ||
        OpenAllocation->NumAllocations == 0 || OpenAllocation->pOpenAllocation == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }
    if (!HdgfxKmdDeviceValid(KmdDevice))
        return STATUS_INVALID_HANDLE;

    for (Index = 0; Index < OpenAllocation->NumAllocations; Index++)
        OpenAllocation->pOpenAllocation[Index].hDeviceSpecificAllocation = NULL;
    for (Index = 0; Index < OpenAllocation->NumAllocations; Index++)
    {
        Status = HdgfxOpenOneAllocation(KmdDevice, &OpenAllocation->pOpenAllocation[Index]);
        if (!NT_SUCCESS(Status))
            break;
    }
    if (NT_SUCCESS(Status))
        return STATUS_SUCCESS;

    while (Index-- > 0)
    {
        HdgfxFreeOpen(OpenAllocation->pOpenAllocation[Index].hDeviceSpecificAllocation);
        OpenAllocation->pOpenAllocation[Index].hDeviceSpecificAllocation = NULL;
    }
    return Status;
}

NTSTATUS
APIENTRY
HdgfxDdiCloseAllocation(
    _In_ PVOID hDevice,
    _In_ CONST DXGKARG_CLOSEALLOCATION *CloseAllocation)
{
    PSOFTGPU_KMD_DEVICE KmdDevice = hDevice;
    PSOFTGPU_OPENALLOC Open;
    ULONG Index;

    if (KmdDevice == NULL || KmdDevice->Magic != SOFTGPU_KMD_DEVICE_MAGIC || CloseAllocation == NULL ||
        CloseAllocation->NumAllocations == 0 || CloseAllocation->pOpenHandleList == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }
    if (!HdgfxKmdDeviceValid(KmdDevice))
        return STATUS_INVALID_HANDLE;

    for (Index = 0; Index < CloseAllocation->NumAllocations; Index++)
    {
        Open = CloseAllocation->pOpenHandleList[Index];
        if (Open == NULL || Open->Magic != SOFTGPU_OPENALLOC_MAGIC || Open->Device != KmdDevice)
        {
            DPRINT1("CloseAllocation %lu: bad binding %p\n", Index, Open);
            return STATUS_INVALID_PARAMETER;
        }
    }
    for (Index = 0; Index < CloseAllocation->NumAllocations; Index++)
        HdgfxFreeOpen(CloseAllocation->pOpenHandleList[Index]);
    return STATUS_SUCCESS;
}
