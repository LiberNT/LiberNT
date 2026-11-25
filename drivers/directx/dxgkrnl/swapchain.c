/*
 * PROJECT:     LiberNT WDDM DirectX Graphics Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     D3DKMT swap chain objects shared through NT handles
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF
 */

#include "dxgkrnl_private.h"
#include <ndk/obfuncs.h>
#include <reactos/rddm/rxgkioctl.h>

C_ASSERT(sizeof(RXGK_SWAPCHAIN_PACKET) == RXGK_SWAPCHAIN_PACKET_V1_SIZE);

typedef struct _DXGKRNL_SWAPCHAIN_OBJECT
{
    FAST_MUTEX MetadataLock;
    volatile LONG ProducerRegistered;
    volatile LONG ConsumerRegistered;
    volatile LONG Abandoned;
    ULONG SurfaceCount;
    PVOID Surfaces[RXGK_SWAPCHAIN_MAX_SURFACES];
    ULONG MetadataSize;
    PUCHAR Metadata;
} DXGKRNL_SWAPCHAIN_OBJECT, *PDXGKRNL_SWAPCHAIN_OBJECT;

static POBJECT_TYPE DxgkSwapChainObjectType;

static VOID
DxgkpSwapChainDereferenceSurfaces(
    _In_reads_(Count) PVOID *Surfaces,
    _In_ ULONG Count)
{
    ULONG Index;

    ASSERT(Count <= RXGK_SWAPCHAIN_MAX_SURFACES);
    for (Index = 0; Index < Count; ++Index)
    {
        ASSERT(Surfaces[Index] != NULL);
        ObDereferenceObject(Surfaces[Index]);
    }
}

static VOID
NTAPI
DxgkpDeleteSwapChainObject(
    _In_ PVOID Object)
{
    PDXGKRNL_SWAPCHAIN_OBJECT SwapChain = Object;

    ASSERT(SwapChain->MetadataSize <= RXGK_SWAPCHAIN_MAX_METADATA);
    ASSERT((SwapChain->Metadata == NULL) == (SwapChain->MetadataSize == 0));
    DxgkpSwapChainDereferenceSurfaces(SwapChain->Surfaces, SwapChain->SurfaceCount);
    if (SwapChain->Metadata != NULL)
        ExFreePoolWithTag(SwapChain->Metadata, TAG_DXGK_SWAPCHAIN);
}

NTSTATUS
DxgkSwapChainInitialize(VOID)
{
    OBJECT_TYPE_INITIALIZER TypeInitializer;
    UNICODE_STRING TypeName;

    PAGED_CODE();
    if (DxgkSwapChainObjectType != NULL)
        return STATUS_SUCCESS;
    RtlZeroMemory(&TypeInitializer, sizeof(TypeInitializer));
    RtlInitUnicodeString(&TypeName, L"DxgkSharedSwapChainObject");
    TypeInitializer.Length = sizeof(TypeInitializer);
    TypeInitializer.InvalidAttributes = OBJ_OPENLINK;
    TypeInitializer.GenericMapping.GenericRead = READ_CONTROL;
    TypeInitializer.GenericMapping.GenericWrite = READ_CONTROL;
    TypeInitializer.GenericMapping.GenericExecute = READ_CONTROL;
    TypeInitializer.GenericMapping.GenericAll = STANDARD_RIGHTS_ALL;
    TypeInitializer.ValidAccessMask = STANDARD_RIGHTS_ALL;
    TypeInitializer.PoolType = NonPagedPool;
    TypeInitializer.DefaultNonPagedPoolCharge = sizeof(DXGKRNL_SWAPCHAIN_OBJECT);
    TypeInitializer.UseDefaultObject = TRUE;
    TypeInitializer.SecurityRequired = TRUE;
    TypeInitializer.DeleteProcedure = DxgkpDeleteSwapChainObject;
    return ObCreateObjectType(&TypeName, &TypeInitializer, NULL, &DxgkSwapChainObjectType);
}

static NTSTATUS
DxgkpReferenceSwapChain(
    _In_ ULONGLONG Handle,
    _In_ ACCESS_MASK DesiredAccess,
    _Out_ PDXGKRNL_SWAPCHAIN_OBJECT *SwapChain)
{
    *SwapChain = NULL;
    if (Handle > (ULONGLONG)(ULONG_PTR)-1)
        return STATUS_INVALID_HANDLE;
    return ObReferenceObjectByHandle((HANDLE)(ULONG_PTR)Handle, DesiredAccess, DxgkSwapChainObjectType,
                                     ExGetPreviousMode(), (PVOID *)SwapChain, NULL);
}

static NTSTATUS
DxgkpSwapChainCheckDevice(
    _In_ D3DKMT_HANDLE hDevice)
{
    PDXGKRNL_ADAPTER Adapter;
    PDXGKRNL_DEVICE Device;

    if (!NT_SUCCESS(DxgkReferenceOwnedDeviceByHandle(hDevice, PsGetCurrentProcess(), &Adapter, &Device)))
        return STATUS_INVALID_PARAMETER;
    ASSERT(Device != NULL && Device->Adapter == Adapter);
    DxgkDereferenceDevice(Device);
    return STATUS_SUCCESS;
}

static NTSTATUS
DxgkpSwapChainCheckEvent(
    _In_ CONST RXGK_SWAPCHAIN_PACKET *Packet)
{
    PVOID Event;
    NTSTATUS Status;

    if ((Packet->Flags & RXGK_SWAPCHAIN_FLAG_EVENT) == 0)
        return STATUS_SUCCESS;
    if ((Packet->Flags & RXGK_SWAPCHAIN_FLAG_FAIL_IF_BUSY) != 0)
        return STATUS_NOT_IMPLEMENTED;
    if (Packet->Data > (ULONGLONG)(ULONG_PTR)-1)
        return STATUS_INVALID_HANDLE;
    Status = ObReferenceObjectByHandle((HANDLE)(ULONG_PTR)Packet->Data, 0, *ExEventObjectType, ExGetPreviousMode(), &Event, NULL);
    if (!NT_SUCCESS(Status))
        return Status;
    /* FIXME: the event is not kept; the missing buffer queue never signals it */
    ObDereferenceObject(Event);
    return STATUS_SUCCESS;
}

static NTSTATUS
DxgkpSwapChainCreate(
    _Inout_ PRXGK_SWAPCHAIN_PACKET Packet)
{
    PVOID Surfaces[RXGK_SWAPCHAIN_MAX_SURFACES];
    PDXGKRNL_SWAPCHAIN_OBJECT SwapChain;
    HANDLE Handle;
    ULONG Index;
    NTSTATUS Status;

    if (Packet->SurfaceCount == 0 || Packet->SurfaceCount > RXGK_SWAPCHAIN_MAX_SURFACES)
        return STATUS_INVALID_PARAMETER;
    Status = DxgkpSwapChainCheckDevice(Packet->hDevice);
    if (!NT_SUCCESS(Status))
        return Status;
    for (Index = 0; Index < Packet->SurfaceCount; ++Index)
    {
        if (Packet->Surfaces[Index] > (ULONGLONG)(ULONG_PTR)-1)
            Status = STATUS_INVALID_HANDLE;
        else
            Status = ObReferenceObjectByHandle((HANDLE)(ULONG_PTR)Packet->Surfaces[Index], 0, DxgkSharedResourceType(),
                                               ExGetPreviousMode(), &Surfaces[Index], NULL);
        if (!NT_SUCCESS(Status))
        {
            DxgkpSwapChainDereferenceSurfaces(Surfaces, Index);
            return Status;
        }
    }
    Status = DxgkpSwapChainCheckEvent(Packet);
    if (!NT_SUCCESS(Status))
    {
        DxgkpSwapChainDereferenceSurfaces(Surfaces, Packet->SurfaceCount);
        return Status;
    }

    Status = ObCreateObject(ExGetPreviousMode(), DxgkSwapChainObjectType,
                            (POBJECT_ATTRIBUTES)(ULONG_PTR)Packet->ObjectAttributes, KernelMode, NULL,
                            sizeof(*SwapChain), 0, 0, (PVOID *)&SwapChain);
    if (!NT_SUCCESS(Status))
    {
        DxgkpSwapChainDereferenceSurfaces(Surfaces, Packet->SurfaceCount);
        return Status;
    }
    RtlZeroMemory(SwapChain, sizeof(*SwapChain));
    ExInitializeFastMutex(&SwapChain->MetadataLock);
    SwapChain->SurfaceCount = Packet->SurfaceCount;
    RtlCopyMemory(SwapChain->Surfaces, Surfaces, Packet->SurfaceCount * sizeof(Surfaces[0]));
    if ((Packet->Flags & RXGK_SWAPCHAIN_FLAG_PRODUCER) != 0)
        SwapChain->ProducerRegistered = 1;
    else
        SwapChain->ConsumerRegistered = 1;
    Status = ObInsertObject(SwapChain, NULL, Packet->DesiredAccess, 0, NULL, &Handle);
    if (!NT_SUCCESS(Status))
        return Status;
    Packet->SwapChain = (ULONGLONG)(ULONG_PTR)Handle;
    return STATUS_SUCCESS;
}

static NTSTATUS
DxgkpSwapChainOpenSurfaces(
    _In_ PDXGKRNL_SWAPCHAIN_OBJECT SwapChain,
    _In_ ACCESS_MASK DesiredAccess,
    _Out_writes_(RXGK_SWAPCHAIN_MAX_SURFACES) PULONGLONG Handles)
{
    HANDLE Handle;
    ULONG Index;
    NTSTATUS Status;

    ASSERT(SwapChain->SurfaceCount != 0 && SwapChain->SurfaceCount <= RXGK_SWAPCHAIN_MAX_SURFACES);
    for (Index = 0; Index < SwapChain->SurfaceCount; ++Index)
    {
        Status = ObOpenObjectByPointer(SwapChain->Surfaces[Index], 0, NULL, DesiredAccess, DxgkSharedResourceType(),
                                       ExGetPreviousMode(), &Handle);
        if (!NT_SUCCESS(Status))
        {
            while (Index-- > 0)
                ZwClose((HANDLE)(ULONG_PTR)Handles[Index]);
            return Status;
        }
        Handles[Index] = (ULONGLONG)(ULONG_PTR)Handle;
    }
    return STATUS_SUCCESS;
}

static NTSTATUS
DxgkpSwapChainRegister(
    _Inout_ PRXGK_SWAPCHAIN_PACKET Packet,
    _In_ PDXGKRNL_SWAPCHAIN_OBJECT SwapChain)
{
    volatile LONG *Role;
    LONG Previous;
    NTSTATUS Status;

    if (Packet->SurfaceCount == 0 && (Packet->Flags & RXGK_SWAPCHAIN_FLAG_NO_BUFFER) != 0)
    {
        Packet->SurfaceCount = SwapChain->SurfaceCount;
        return STATUS_BUFFER_TOO_SMALL;
    }
    if (Packet->SurfaceCount != SwapChain->SurfaceCount || (Packet->Flags & RXGK_SWAPCHAIN_FLAG_NO_BUFFER) != 0)
        return STATUS_INVALID_PARAMETER;
    Role = (Packet->Flags & RXGK_SWAPCHAIN_FLAG_PRODUCER) != 0 ? &SwapChain->ProducerRegistered : &SwapChain->ConsumerRegistered;
    if (InterlockedCompareExchange(Role, 0, 0) != 0)
        return STATUS_ALREADY_REGISTERED;
    Status = DxgkpSwapChainCheckEvent(Packet);
    if (!NT_SUCCESS(Status))
        return Status;
    Previous = InterlockedCompareExchange(Role, 1, 0);
    if (Previous != 0)
    {
        ASSERT(Previous == 1);
        return STATUS_ALREADY_REGISTERED;
    }
    Status = DxgkpSwapChainOpenSurfaces(SwapChain, Packet->DesiredAccessTextures, Packet->Surfaces);
    if (!NT_SUCCESS(Status))
    {
        Previous = InterlockedExchange(Role, 0);
        ASSERT(Previous == 1);
    }
    return Status;
}

static NTSTATUS
DxgkpSwapChainOpen(
    _Inout_ PRXGK_SWAPCHAIN_PACKET Packet)
{
    PDXGKRNL_SWAPCHAIN_OBJECT SwapChain;
    ULONGLONG Handle = Packet->SwapChain;
    HANDLE Opened = NULL;
    NTSTATUS Status;

    Status = DxgkpSwapChainCheckDevice(Packet->hDevice);
    if (!NT_SUCCESS(Status))
        return Status;
    if ((Packet->Flags & RXGK_SWAPCHAIN_FLAG_BY_NAME) != 0)
    {
        ASSERT(Packet->SwapChain == 0);
        Status = ObOpenObjectByName((POBJECT_ATTRIBUTES)(ULONG_PTR)Packet->ObjectAttributes, DxgkSwapChainObjectType,
                                    ExGetPreviousMode(), NULL, Packet->DesiredAccess, NULL, &Opened);
        if (!NT_SUCCESS(Status))
            return Status;
        Handle = (ULONGLONG)(ULONG_PTR)Opened;
    }
    Status = DxgkpReferenceSwapChain(Handle, READ_CONTROL, &SwapChain);
    if (NT_SUCCESS(Status))
    {
        Status = DxgkpSwapChainRegister(Packet, SwapChain);
        ObDereferenceObject(SwapChain);
    }
    if (Opened != NULL)
    {
        if (NT_SUCCESS(Status))
            Packet->SwapChain = (ULONGLONG)(ULONG_PTR)Opened;
        else
            ZwClose(Opened);
    }
    return Status;
}

static NTSTATUS
DxgkpSwapChainSetMetadataLocked(
    _Inout_ PDXGKRNL_SWAPCHAIN_OBJECT SwapChain,
    _In_reads_bytes_(Size) CONST UCHAR *Data,
    _In_ ULONG Size)
{
    PUCHAR Grown;

    ASSERT(Size != 0 && Size <= RXGK_SWAPCHAIN_MAX_METADATA);
    ASSERT(Data != NULL);
    if (Size > SwapChain->MetadataSize)
    {
        Grown = ExAllocatePoolWithTag(PagedPool, Size, TAG_DXGK_SWAPCHAIN);
        if (Grown == NULL)
            return STATUS_INSUFFICIENT_RESOURCES;
        if (SwapChain->Metadata != NULL)
        {
            RtlCopyMemory(Grown, SwapChain->Metadata, SwapChain->MetadataSize);
            ExFreePoolWithTag(SwapChain->Metadata, TAG_DXGK_SWAPCHAIN);
        }
        SwapChain->Metadata = Grown;
        SwapChain->MetadataSize = Size;
    }
    RtlCopyMemory(SwapChain->Metadata, Data, Size);
    return STATUS_SUCCESS;
}

static NTSTATUS
DxgkpSwapChainMetadataLocked(
    _Inout_ PRXGK_SWAPCHAIN_PACKET Packet,
    _Inout_ PDXGKRNL_SWAPCHAIN_OBJECT SwapChain)
{
    PUCHAR Data = (PUCHAR)(ULONG_PTR)Packet->Data;
    ULONG Stored = SwapChain->MetadataSize;

    if (InterlockedCompareExchange(&SwapChain->Abandoned, 0, 0) != 0)
        return STATUS_DELETE_PENDING;
    /* FIXME: frame metadata belongs to an acquired buffer, and the missing buffer queue never hands one out */
    if ((Packet->Flags & RXGK_SWAPCHAIN_FLAG_GLOBAL) == 0)
        return STATUS_INVALID_PARAMETER;
    if ((Packet->Flags & RXGK_SWAPCHAIN_FLAG_SET) != 0)
    {
        if (Packet->DataSize == 0)
            return STATUS_INVALID_PARAMETER;
        if (Packet->DataSize > RXGK_SWAPCHAIN_MAX_METADATA)
        {
            InterlockedExchange(&SwapChain->Abandoned, 1);
            return STATUS_INVALID_PARAMETER;
        }
        return DxgkpSwapChainSetMetadataLocked(SwapChain, Data, Packet->DataSize);
    }
    if (Packet->DataSize == 0 && (Packet->Flags & RXGK_SWAPCHAIN_FLAG_NO_BUFFER) != 0)
    {
        Packet->DataSize = Stored;
        return STATUS_SUCCESS;
    }
    if (Packet->DataSize == 0 || (Packet->Flags & RXGK_SWAPCHAIN_FLAG_NO_BUFFER) != 0 || Stored == 0)
        return STATUS_INVALID_PARAMETER;
    ASSERT(Data != NULL && Packet->DataSize <= RXGK_SWAPCHAIN_MAX_METADATA);
    RtlCopyMemory(Data, SwapChain->Metadata, min(Packet->DataSize, Stored));
    if (Packet->DataSize < Stored)
        return STATUS_BUFFER_OVERFLOW;
    Packet->DataSize = Stored;
    return STATUS_SUCCESS;
}

static NTSTATUS
DxgkpSwapChainMetadata(
    _Inout_ PRXGK_SWAPCHAIN_PACKET Packet)
{
    PDXGKRNL_SWAPCHAIN_OBJECT SwapChain;
    NTSTATUS Status;

    Status = DxgkpReferenceSwapChain(Packet->SwapChain, READ_CONTROL, &SwapChain);
    if (!NT_SUCCESS(Status))
        return Status;
    ExAcquireFastMutex(&SwapChain->MetadataLock);
    Status = DxgkpSwapChainMetadataLocked(Packet, SwapChain);
    ExReleaseFastMutex(&SwapChain->MetadataLock);
    ObDereferenceObject(SwapChain);
    return Status;
}

static NTSTATUS
DxgkpSwapChainSimple(
    _Inout_ PRXGK_SWAPCHAIN_PACKET Packet)
{
    PDXGKRNL_SWAPCHAIN_OBJECT SwapChain;
    BOOLEAN Abandoned;
    NTSTATUS Status;

    Status = DxgkpReferenceSwapChain(Packet->SwapChain, Packet->Operation == RxgkSwapChainAbandon ? READ_CONTROL : 0, &SwapChain);
    if (!NT_SUCCESS(Status))
        return Status;
    if (Packet->Operation == RxgkSwapChainAbandon)
        InterlockedExchange(&SwapChain->Abandoned, 1);
    Abandoned = InterlockedCompareExchange(&SwapChain->Abandoned, 0, 0) != 0;
    ObDereferenceObject(SwapChain);
    switch (Packet->Operation)
    {
        case RxgkSwapChainAbandon:
            return STATUS_SUCCESS;
        case RxgkSwapChainNonSequential:
            /* TODO: non-sequential swap chains; every chain made here is sequential, and Windows 11 refuses these on one */
            return Abandoned ? STATUS_DELETE_PENDING : STATUS_NOT_SUPPORTED;
        default:
            ASSERT(Packet->Operation == RxgkSwapChainAcquire || Packet->Operation == RxgkSwapChainRelease);
            /* FIXME: the producer/consumer buffer queue is not implemented; Windows 11 can hand a buffer out on acquire */
            return Abandoned ? STATUS_DELETE_PENDING : STATUS_INVALID_PARAMETER;
    }
}

NTSTATUS
DxgkSwapChainDispatch(
    _Inout_ PRXGK_SWAPCHAIN_PACKET Packet)
{
    PAGED_CODE();
    if (Packet == NULL ||
        Packet->Size != RXGK_SWAPCHAIN_PACKET_V1_SIZE ||
        Packet->Version != RXGK_WDDM_PACKET_VERSION_1 ||
        (Packet->Flags & ~RXGK_SWAPCHAIN_VALID_FLAGS) != 0 ||
        Packet->SurfaceCount > RXGK_SWAPCHAIN_MAX_SURFACES)
    {
        return STATUS_INVALID_PARAMETER;
    }
    if (DxgkSwapChainObjectType == NULL)
        return STATUS_DEVICE_NOT_READY;
    switch (Packet->Operation)
    {
        case RxgkSwapChainCreate:
            Packet->Result = DxgkpSwapChainCreate(Packet);
            return STATUS_SUCCESS;
        case RxgkSwapChainOpen:
            Packet->Result = DxgkpSwapChainOpen(Packet);
            return STATUS_SUCCESS;
        case RxgkSwapChainMetadata:
            Packet->Result = DxgkpSwapChainMetadata(Packet);
            return STATUS_SUCCESS;
        case RxgkSwapChainAbandon:
        case RxgkSwapChainAcquire:
        case RxgkSwapChainRelease:
        case RxgkSwapChainNonSequential:
            Packet->Result = DxgkpSwapChainSimple(Packet);
            return STATUS_SUCCESS;
        default:
            return STATUS_INVALID_PARAMETER;
    }
}
