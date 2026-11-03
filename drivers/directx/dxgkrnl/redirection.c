/*
 * PROJECT:     LiberNT WDDM DirectX Graphics Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     CDD redirection surface synchronization
 * COPYRIGHT:   Copyright 2026 Ahmed Arif <arif.ing@outlook.com>
 */

#include "dxgkrnl_private.h"
#include "present.h"
#include "vidmm.h"
#include <reactos/dwmframe.h>

NTKERNELAPI VOID FASTCALL
ExfAcquirePushLockExclusive(_Inout_ PEX_PUSH_LOCK PushLock);
NTKERNELAPI VOID FASTCALL
ExfAcquirePushLockShared(_Inout_ PEX_PUSH_LOCK PushLock);
NTKERNELAPI VOID FASTCALL
ExfReleasePushLockExclusive(_Inout_ PEX_PUSH_LOCK PushLock);
NTKERNELAPI VOID FASTCALL
ExfReleasePushLockShared(_Inout_ PEX_PUSH_LOCK PushLock);

#define DXGKP_REDIRECTION_SYNC_MAX_SURFACES 1024u
#define DXGKP_REDIRECTION_SYNC_TIMEOUT_MS 2000u
#define DXGKP_WINDOW_BLT_ISSUE_TIMEOUT_MS 1000u
#define DXGKP_WINDOW_BLT_RETRY_MS 10u

typedef struct _DXGKP_REDIRECTION_SYNC_SNAPSHOT
{
    PDXGKVMM_ALLOCATION Allocation;
    ULONG Fences[DXGK_MAX_TRACKED_NODES];
    ULONGLONG CacheOffset;
    ULONGLONG CacheLength;
} DXGKP_REDIRECTION_SYNC_SNAPSHOT,
 *PDXGKP_REDIRECTION_SYNC_SNAPSHOT;

static EX_PUSH_LOCK DxgkpWin32kCddInterfaceLock;
static DXGKRNL_WIN32K_CDD_INTERFACE DxgkpWin32kCddInterface;
static KEVENT DxgkpWindowBltRetiredEvent;

static BOOLEAN
DxgkpRedirectionFenceReached(
    _In_ ULONG CompletedFenceId,
    _In_ ULONG SubmissionFenceId)
{
    return (LONG)(CompletedFenceId - SubmissionFenceId) >= 0;
}

VOID
DxgkRedirectionInitialize(VOID)
{
    RtlZeroMemory(&DxgkpWin32kCddInterfaceLock,
                  sizeof(DxgkpWin32kCddInterfaceLock));
    RtlZeroMemory(&DxgkpWin32kCddInterface,
                  sizeof(DxgkpWin32kCddInterface));
    KeInitializeEvent(&DxgkpWindowBltRetiredEvent, NotificationEvent, FALSE);
}

NTSTATUS
DxgkRegisterWin32kCddInterface(
    _In_ const DXGKRNL_WIN32K_CDD_INTERFACE *Interface)
{
    ULONG RequiredSize;

    if (Interface == NULL)
        return STATUS_INVALID_PARAMETER;

    if (Interface->Version == DXGKRNL_WIN32K_CDD_INTERFACE_VERSION_1)
    {
        RequiredSize = FIELD_OFFSET(DXGKRNL_WIN32K_CDD_INTERFACE,
                                    AdmitRedirectedBltPresent);
    }
    else if (Interface->Version ==
             DXGKRNL_WIN32K_CDD_INTERFACE_VERSION_2)
    {
        RequiredSize = FIELD_OFFSET(DXGKRNL_WIN32K_CDD_INTERFACE,
                                    CancelRedirectedBltPresent);
    }
    else if (Interface->Version ==
             DXGKRNL_WIN32K_CDD_INTERFACE_VERSION_3)
    {
        RequiredSize = FIELD_OFFSET(DXGKRNL_WIN32K_CDD_INTERFACE,
                                    DispatchNtGdi);
    }
    else if (Interface->Version ==
             DXGKRNL_WIN32K_CDD_INTERFACE_VERSION_4)
    {
        RequiredSize = FIELD_OFFSET(DXGKRNL_WIN32K_CDD_INTERFACE,
                                    QueryWindowPresentState);
    }
    else if (Interface->Version ==
             DXGKRNL_WIN32K_CDD_INTERFACE_VERSION_5)
    {
        RequiredSize = FIELD_OFFSET(DXGKRNL_WIN32K_CDD_INTERFACE,
                                    RefreshComposition);
    }
    else if (Interface->Version ==
             DXGKRNL_WIN32K_CDD_INTERFACE_VERSION_6)
    {
        RequiredSize = FIELD_OFFSET(DXGKRNL_WIN32K_CDD_INTERFACE,
                                    IssueWindowBltTarget);
    }
    else if (Interface->Version ==
             DXGKRNL_WIN32K_CDD_INTERFACE_VERSION_7)
    {
        RequiredSize = FIELD_OFFSET(DXGKRNL_WIN32K_CDD_INTERFACE,
                                    QueryRedirectedBltClient);
    }
    else if (Interface->Version ==
             DXGKRNL_WIN32K_CDD_INTERFACE_VERSION_8)
    {
        RequiredSize = sizeof(*Interface);
    }
    else
    {
        return STATUS_REVISION_MISMATCH;
    }
    if (Interface->Size != RequiredSize)
        return STATUS_INFO_LENGTH_MISMATCH;

    KeEnterCriticalRegion();
    ExfAcquirePushLockExclusive(&DxgkpWin32kCddInterfaceLock);
    RtlZeroMemory(&DxgkpWin32kCddInterface,
                  sizeof(DxgkpWin32kCddInterface));
    RtlCopyMemory(&DxgkpWin32kCddInterface, Interface, RequiredSize);
    ExfReleasePushLockExclusive(&DxgkpWin32kCddInterfaceLock);
    KeLeaveCriticalRegion();
    return STATUS_SUCCESS;
}

ULONG_PTR
DxgkDispatchWin32kNtGdi(
    _In_ ULONG NativeOrdinal,
    _In_ ULONG_PTR Argument0,
    _In_ ULONG_PTR Argument1,
    _In_ ULONG_PTR Argument2,
    _In_ ULONG_PTR Argument3,
    _In_ ULONG_PTR Argument4)
{
    PDXGKENG_DISPATCH_NTGDI Callback;

    KeEnterCriticalRegion();
    ExfAcquirePushLockShared(&DxgkpWin32kCddInterfaceLock);
    Callback = DxgkpWin32kCddInterface.DispatchNtGdi;
    ExfReleasePushLockShared(&DxgkpWin32kCddInterfaceLock);
    KeLeaveCriticalRegion();

    if (Callback == NULL)
        return (ULONG_PTR)STATUS_DEVICE_NOT_READY;
    return Callback(NativeOrdinal, Argument0, Argument1, Argument2, Argument3, Argument4);
}

NTSTATUS
DxgkAssociateRedirectionSurface(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _In_ const DXGK_REDIRECTION_SURFACE_ASSOCIATE *Associate)
{
    PDXGKVMM_ALLOCATION Allocation = NULL;
    PDXGKVMM_RESOURCE Resource = NULL;
    NTSTATUS Status;

    PAGED_CODE();

    if (Adapter == NULL || Associate == NULL ||
        Associate->StructSize != sizeof(*Associate) ||
        Associate->Flags != 0 || Associate->AllocationHandle == 0 ||
        Associate->ResourceHandle == 0 || Associate->GlobalShare == 0 ||
        Associate->SurfaceHandle == 0)
    {
        return STATUS_INVALID_PARAMETER;
    }

    Status = DxgkVidMmReferenceResource(
                 Associate->ResourceHandle, FALSE, NULL, &Resource);
    if (!NT_SUCCESS(Status))
        return STATUS_INVALID_PARAMETER;

    Status = DxgkVidMmReferenceAllocation(
                 (HANDLE)(ULONG_PTR)Associate->AllocationHandle,
                 Adapter, NULL, &Allocation);
    if (!NT_SUCCESS(Status))
    {
        DxgkVidMmDereferenceResource(Resource);
        return STATUS_INVALID_PARAMETER;
    }

    if (Resource->Adapter != Adapter ||
        Resource->GlobalShareHandle != Associate->GlobalShare ||
        Resource->AllocationCount != 1 || Allocation->Resource != Resource)
    {
        Status = STATUS_INVALID_PARAMETER;
    }
    else
    {
        (VOID)KeWaitForSingleObject(&Allocation->ResidencyLock,
                                    Executive, KernelMode, FALSE, NULL);
        if (Allocation->RedirectionSurfaceHandle != 0 &&
            Allocation->RedirectionSurfaceHandle !=
                (ULONG_PTR)Associate->SurfaceHandle)
        {
            Status = STATUS_OBJECT_NAME_COLLISION;
        }
        else
        {
            InterlockedExchangePointer(
                (PVOID volatile *)&Allocation->RedirectionSurfaceHandle,
                (PVOID)(ULONG_PTR)Associate->SurfaceHandle);
            Status = STATUS_SUCCESS;
        }
        KeReleaseMutex(&Allocation->ResidencyLock, FALSE);
    }

    DxgkVidMmDereferenceAllocation(Allocation);
    DxgkVidMmDereferenceResource(Resource);
    return Status;
}

VOID
DxgkPublishRedirectionPresent(
    _Inout_ PDXGKVMM_ALLOCATION Allocation,
    _In_ ULONG NodeOrdinal,
    _In_ ULONG SubmissionFenceId)
{
    if (Allocation == NULL || SubmissionFenceId == 0 ||
        NodeOrdinal >= DXGK_MAX_TRACKED_NODES ||
        InterlockedCompareExchangePointer(
            (PVOID volatile *)&Allocation->RedirectionSurfaceHandle,
            NULL, NULL) == NULL)
    {
        return;
    }

    (VOID)KeWaitForSingleObject(&Allocation->ResidencyLock,
                                Executive, KernelMode, FALSE, NULL);
    if (Allocation->RedirectionSurfaceHandle != 0)
        Allocation->RedirectionSubmittedFenceId[NodeOrdinal] =
            SubmissionFenceId;
    KeReleaseMutex(&Allocation->ResidencyLock, FALSE);
}

NTSTATUS
DxgkQueryWindowPresentState(_In_ ULONG_PTR WindowHandle)
{
    PDXGKENG_QUERY_WINDOW_PRESENT_STATE Callback;

    KeEnterCriticalRegion();
    ExfAcquirePushLockShared(&DxgkpWin32kCddInterfaceLock);
    Callback = DxgkpWin32kCddInterface.QueryWindowPresentState;
    ExfReleasePushLockShared(&DxgkpWin32kCddInterfaceLock);
    KeLeaveCriticalRegion();

    return Callback != NULL ? Callback(WindowHandle) : STATUS_NOT_SUPPORTED;
}

NTSTATUS
DxgkQueryRedirectedBltClient(
    _In_ const DXGKRNL_REDIRECTED_BLT_PRESENT *Present,
    _Out_ RECT *ClientRect)
{
    PDXGKENG_QUERY_REDIRECTED_BLT_CLIENT Callback;
    NTSTATUS Status;

    KeEnterCriticalRegion();
    ExfAcquirePushLockShared(&DxgkpWin32kCddInterfaceLock);
    Callback = DxgkpWin32kCddInterface.QueryRedirectedBltClient;
    Status = Callback != NULL ? Callback(Present, ClientRect) : STATUS_NOT_SUPPORTED;
    ExfReleasePushLockShared(&DxgkpWin32kCddInterfaceLock);
    KeLeaveCriticalRegion();
    return Status;
}

NTSTATUS
DxgkIssueWindowBltTarget(
    _In_ ULONG_PTR WindowHandle,
    _Out_ PDXGKRNL_WINDOW_BLT_TARGET Target)
{
    PDXGKENG_ISSUE_WINDOW_BLT_TARGET Callback;
    LARGE_INTEGER Interval;
    ULONGLONG Start100ns;
    NTSTATUS Status;

    KeEnterCriticalRegion();
    ExfAcquirePushLockShared(&DxgkpWin32kCddInterfaceLock);
    Callback = DxgkpWin32kCddInterface.IssueWindowBltTarget;
    ExfReleasePushLockShared(&DxgkpWin32kCddInterfaceLock);
    KeLeaveCriticalRegion();

    if (Callback == NULL)
        return STATUS_NOT_SUPPORTED;

    /* A window takes one update at a time; a Blt behind one still in flight waits for it to retire. */
    Start100ns = KeQueryInterruptTime();
    Interval.QuadPart = -(LONGLONG)DXGKP_WINDOW_BLT_RETRY_MS * 10000;
    for (;;)
    {
        KeClearEvent(&DxgkpWindowBltRetiredEvent);
        RtlZeroMemory(Target, sizeof(*Target));
        Target->Size = sizeof(*Target);
        Status = Callback(WindowHandle, Target);
        if (Status != STATUS_DEVICE_BUSY ||
            KeQueryInterruptTime() - Start100ns >= (ULONGLONG)DXGKP_WINDOW_BLT_ISSUE_TIMEOUT_MS * 10000)
        {
            return Status;
        }
        (VOID)KeWaitForSingleObject(&DxgkpWindowBltRetiredEvent, Executive, KernelMode, FALSE, &Interval);
    }
}

VOID
DxgkCancelWindowBltTarget(
    _In_ ULONG_PTR WindowHandle,
    _In_ ULONGLONG UpdateId)
{
    PDXGKENG_CANCEL_WINDOW_BLT_TARGET Callback;

    KeEnterCriticalRegion();
    ExfAcquirePushLockShared(&DxgkpWin32kCddInterfaceLock);
    Callback = DxgkpWin32kCddInterface.CancelWindowBltTarget;
    ExfReleasePushLockShared(&DxgkpWin32kCddInterfaceLock);
    KeLeaveCriticalRegion();

    if (Callback != NULL)
        (VOID)Callback(WindowHandle, UpdateId);
}

static WORK_QUEUE_ITEM DxgkpCompositionRefreshWorkItem;
static volatile LONG DxgkpCompositionRefreshQueued;

static VOID
DxgkpRefreshCompositionNow(VOID)
{
    PDXGKENG_REFRESH_COMPOSITION Callback;

    KeEnterCriticalRegion();
    ExfAcquirePushLockShared(&DxgkpWin32kCddInterfaceLock);
    Callback = DxgkpWin32kCddInterface.RefreshComposition;
    ExfReleasePushLockShared(&DxgkpWin32kCddInterfaceLock);
    KeLeaveCriticalRegion();

    if (Callback != NULL)
        Callback();
}

static VOID
NTAPI
DxgkpCompositionRefreshWorker(
    _In_ PVOID Parameter)
{
    UNREFERENCED_PARAMETER(Parameter);

    /* Re-arm first: a request made during the refresh queues another. */
    InterlockedExchange(&DxgkpCompositionRefreshQueued, 0);
    DxgkpRefreshCompositionNow();
}

/*
 * DxgkRequestCompositionRefresh
 *
 * Asks the compositor to compose and present a complete frame, as Windows
 * notifies DWM when a miniport withdraws an overlay configuration.  The
 * request is asynchronous and coalesced.  It is always deferred: a miniport
 * may make it from inside a mode set that win32k started under its USER
 * lock, which the refresh needs.
 *
 * IRQL: <= DISPATCH_LEVEL
 */
VOID
DxgkRequestCompositionRefresh(VOID)
{
    if (InterlockedCompareExchange(&DxgkpCompositionRefreshQueued, 1, 0) != 0)
        return;
    ExInitializeWorkItem(&DxgkpCompositionRefreshWorkItem,
                         DxgkpCompositionRefreshWorker, NULL);
    ExQueueWorkItem(&DxgkpCompositionRefreshWorkItem, DelayedWorkQueue);
}

NTSTATUS
DxgkAdmitRedirectedBltPresent(
    _In_ const DXGKRNL_REDIRECTED_BLT_PRESENT *Present)
{
    PDXGKENG_ADMIT_REDIRECTED_BLT_PRESENT Callback;
    NTSTATUS Status;

    if (Present == NULL || Present->Size != sizeof(*Present) ||
        Present->Flags != 0 || Present->SurfaceHandle == 0 ||
        Present->WindowHandle == 0 || Present->OwnerProcess == NULL ||
        Present->GlobalShare == 0 || Present->Reserved != 0 ||
        Present->UpdateId == 0)
    {
        return STATUS_INVALID_PARAMETER;
    }

    KeEnterCriticalRegion();
    ExfAcquirePushLockShared(&DxgkpWin32kCddInterfaceLock);
    Callback = DxgkpWin32kCddInterface.AdmitRedirectedBltPresent;
    Status = Callback != NULL ? Callback(Present) : STATUS_NOT_SUPPORTED;
    ExfReleasePushLockShared(&DxgkpWin32kCddInterfaceLock);
    KeLeaveCriticalRegion();
    return Status;
}

VOID
DxgkCancelRedirectedBltPresent(
    _In_ const DXGKRNL_REDIRECTED_BLT_PRESENT *Present)
{
    PDXGKENG_CANCEL_REDIRECTED_BLT_PRESENT Callback;

    if (Present == NULL)
        return;

    KeEnterCriticalRegion();
    ExfAcquirePushLockShared(&DxgkpWin32kCddInterfaceLock);
    Callback = DxgkpWin32kCddInterface.CancelRedirectedBltPresent;
    if (Callback != NULL)
        (VOID)Callback(Present);
    ExfReleasePushLockShared(&DxgkpWin32kCddInterfaceLock);
    KeLeaveCriticalRegion();
}

typedef struct _DXGKP_REDIRECTED_BLT_WORK
{
    WORK_QUEUE_ITEM Item;
    DXGKRNL_REDIRECTED_BLT_PRESENT Present;
    HANDLE Context;
    BOOLEAN Complete;
    PDXGKRNL_GDI_BLT_TEXTURE GdiBltTexture;
    UINT DirtyRectCount;
    RECT DirtyRects[DXGK_PRESENT_CORE_MAX_SUBRECTS];
} DXGKP_REDIRECTED_BLT_WORK, *PDXGKP_REDIRECTED_BLT_WORK;

static NTSTATUS
DxgkpCompleteRedirectedBltPresentWithHandle(
    _In_ const DXGKRNL_REDIRECTED_BLT_PRESENT *Present,
    _In_reads_(DirtyRectCount) const RECT *DirtyRects,
    _In_ UINT DirtyRectCount,
    _In_opt_ HANDLE ContextHandle);

static VOID
NTAPI
DxgkpRedirectedBltWorker(
    _In_ PVOID Parameter)
{
    PDXGKP_REDIRECTED_BLT_WORK Work = Parameter;
    NTSTATUS Status = Work->Complete ? STATUS_SUCCESS : STATUS_CANCELLED;

    if (NT_SUCCESS(Status) && Work->GdiBltTexture != NULL)
    {
        Status = Work->DirtyRectCount != 0 ?
            DxgkExecuteCddGdiBitBlt(Work->GdiBltTexture, Work->DirtyRects, Work->DirtyRectCount) :
            DxgkExecuteCddGdiBitBlt(Work->GdiBltTexture, &Work->Present.DestinationRect, 1);
    }
    if (NT_SUCCESS(Status))
        Status = DxgkpCompleteRedirectedBltPresentWithHandle(&Work->Present,
                                                             Work->DirtyRects,
                                                             Work->DirtyRectCount,
                                                             Work->Context);
    if (!NT_SUCCESS(Status))
        DxgkCancelRedirectedBltPresent(&Work->Present);
    KeSetEvent(&DxgkpWindowBltRetiredEvent, IO_NO_INCREMENT, FALSE);
    if (Work->GdiBltTexture != NULL)
        DxgkDereferenceGdiBltTexture(Work->GdiBltTexture);
    ObDereferenceObject(Work->Present.OwnerProcess);
    ExFreePoolWithTag(Work, TAG_DXGK_PRESENT);
}

PVOID
DxgkAllocateRedirectedBltWork(VOID)
{
    return ExAllocatePoolWithTag(NonPagedPool, sizeof(DXGKP_REDIRECTED_BLT_WORK), TAG_DXGK_PRESENT);
}

VOID
DxgkFreeRedirectedBltWork(
    _In_ PVOID Work)
{
    ExFreePoolWithTag(Work, TAG_DXGK_PRESENT);
}

VOID
DxgkQueueRedirectedBltPresent(
    _In_ PVOID Work,
    _In_ const DXGKRNL_REDIRECTED_BLT_PRESENT *Present,
    _In_ BOOLEAN Complete,
    _In_reads_opt_(DirtyRectCount) const RECT *DirtyRects,
    _In_ UINT DirtyRectCount,
    _In_opt_ struct _DXGKRNL_CONTEXT *Context,
    _In_opt_ PDXGKRNL_GDI_BLT_TEXTURE GdiBltTexture)
{
    PDXGKP_REDIRECTED_BLT_WORK Item = Work;

    ASSERT(Item != NULL && Present != NULL && Present->OwnerProcess != NULL);
    if (DirtyRects == NULL || DirtyRectCount > RTL_NUMBER_OF(Item->DirtyRects))
        DirtyRectCount = 0;
    Item->Present = *Present;
    Item->Context = Context != NULL ? (HANDLE)(ULONG_PTR)Context->Handle : NULL;
    Item->Complete = Complete;
    Item->GdiBltTexture = GdiBltTexture;
    Item->DirtyRectCount = DirtyRectCount;
    if (DirtyRectCount != 0)
        RtlCopyMemory(Item->DirtyRects, DirtyRects, DirtyRectCount * sizeof(RECT));
    ObReferenceObject(Item->Present.OwnerProcess);
    ExInitializeWorkItem(&Item->Item, DxgkpRedirectedBltWorker, Item);
    ExQueueWorkItem(&Item->Item, DelayedWorkQueue);
}

NTSTATUS
DxgkCompleteRedirectedBltPresent(
    _In_ const DXGKRNL_REDIRECTED_BLT_PRESENT *Present,
    _In_reads_(DirtyRectCount) const RECT *DirtyRects,
    _In_ UINT DirtyRectCount,
    _In_opt_ PDXGKRNL_CONTEXT Context)
{
    return DxgkpCompleteRedirectedBltPresentWithHandle(
               Present,
               DirtyRects,
               DirtyRectCount,
               Context != NULL ? (HANDLE)(ULONG_PTR)Context->Handle : NULL);
}

static NTSTATUS
DxgkpCompleteRedirectedBltPresentWithHandle(
    _In_ const DXGKRNL_REDIRECTED_BLT_PRESENT *Present,
    _In_reads_(DirtyRectCount) const RECT *DirtyRects,
    _In_ UINT DirtyRectCount,
    _In_opt_ HANDLE ContextHandle)
{
    PDXGKENG_COMPLETE_REDIRECTED_BLT_PRESENT Callback;
    NTSTATUS Status;

    if (Present == NULL ||
        (DirtyRectCount != 0 && DirtyRects == NULL))
    {
        return STATUS_INVALID_PARAMETER;
    }

    KeEnterCriticalRegion();
    ExfAcquirePushLockShared(&DxgkpWin32kCddInterfaceLock);
    Callback = DxgkpWin32kCddInterface.CompleteRedirectedBltPresent;
    Status = Callback != NULL
                 ? Callback(Present,
                            DirtyRects,
                            DirtyRectCount,
                            ContextHandle != NULL ? &ContextHandle : NULL,
                            ContextHandle != NULL ? 1 : 0)
                 : STATUS_NOT_SUPPORTED;
    ExfReleasePushLockShared(&DxgkpWin32kCddInterfaceLock);
    KeLeaveCriticalRegion();
    return Status;
}

static NTSTATUS
DxgkpCreateGdiBltTexture(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _In_ ULONG StagingShare,
    _Out_ PDXGKRNL_GDI_BLT_TEXTURE *OutTexture)
{
    DXGK_REDIRECTION_SURFACE_CREATE Create;
    const DWM_DX_SHARED_SURFACE_INFO *Info;
    PDXGKVMM_RESOURCE Resource = NULL;
    PDXGKVMM_ALLOCATION *Allocations = NULL;
    PDXGKRNL_GDI_BLT_TEXTURE Texture = NULL;
    UINT AllocationCount = 0;
    UINT PrivateDataSize = 0;
    NTSTATUS Status;

    PAGED_CODE();
    *OutTexture = NULL;
    Status = DxgkVidMmReferenceResource(StagingShare, TRUE, NULL, &Resource);
    if (!NT_SUCCESS(Status))
        return STATUS_NOT_FOUND;
    Info = Resource->PrivateRuntimeData;
    if (Resource->Adapter != Adapter || Resource->Device != NULL || Info == NULL ||
        Resource->PrivateRuntimeDataSize != sizeof(*Info) ||
        Info->Magic != DWM_DX_SURFACE_INFO_MAGIC ||
        Info->Format != DWM_DX_FORMAT_B8G8R8A8_UNORM ||
        Info->Width == 0 || Info->Height == 0 || Info->Pitch < Info->Width * sizeof(ULONG))
    {
        Status = STATUS_NOT_SUPPORTED;
    }
    else
    {
        Status = DxgkVidMmSnapshotResourceAllocations(Resource, Adapter, &Allocations,
                                                      &AllocationCount, &PrivateDataSize);
    }
    if (NT_SUCCESS(Status) &&
        (AllocationCount != 1 ||
         InterlockedCompareExchangePointer((PVOID volatile *)&Allocations[0]->RedirectionSurfaceHandle,
                                           NULL, NULL) == NULL))
    {
        Status = STATUS_NOT_SUPPORTED;
    }
    if (NT_SUCCESS(Status))
    {
        Texture = ExAllocatePoolZero(PagedPool, sizeof(*Texture), TAG_DXGK_DISPLAY);
        if (Texture == NULL)
            Status = STATUS_INSUFFICIENT_RESOURCES;
    }
    if (NT_SUCCESS(Status))
    {
        RtlZeroMemory(&Create, sizeof(Create));
        Create.StructSize = sizeof(Create);
        Create.Width = Info->Width;
        Create.Height = Info->Height;
        Create.Format = Info->Format;
        Status = DxgkCreateGdiTextureSurface(Adapter, &Create);
    }
    if (NT_SUCCESS(Status))
    {
        Texture->ReferenceCount = 1;
        Texture->Adapter = Adapter;
        Texture->StagingShare = StagingShare;
        Texture->StagingAllocation = Allocations[0]->Handle;
        Texture->StagingPitch = Info->Pitch;
        Texture->Width = Info->Width;
        Texture->Height = Info->Height;
        Texture->TextureAllocation = (D3DKMT_HANDLE)Create.AllocationHandle;
        Texture->TextureResource = Create.ResourceHandle;
        Texture->TextureShare = Create.GlobalShare;
        Texture->TexturePitch = Create.Pitch;
        *OutTexture = Texture;
        Texture = NULL;
    }
    if (Texture != NULL)
        ExFreePoolWithTag(Texture, TAG_DXGK_DISPLAY);
    DxgkVidMmReleaseAllocationSnapshot(Allocations, AllocationCount);
    DxgkVidMmDereferenceResource(Resource);
    return Status;
}

static VOID
DxgkpDestroyGdiBltTexture(
    _In_ PDXGKRNL_GDI_BLT_TEXTURE Texture)
{
    DXGK_REDIRECTION_SURFACE_DESTROY Destroy;

    PAGED_CODE();
    DxgkReleaseCddGdiBltBindings(Texture);
    if (Texture->TextureAllocation != 0)
    {
        RtlZeroMemory(&Destroy, sizeof(Destroy));
        Destroy.StructSize = sizeof(Destroy);
        Destroy.AllocationHandle = Texture->TextureAllocation;
        Destroy.ResourceHandle = Texture->TextureResource;
        Destroy.GlobalShare = Texture->TextureShare;
        (VOID)DxgkDestroyRedirectionSurface(Texture->Adapter, &Destroy);
    }
    ExFreePoolWithTag(Texture, TAG_DXGK_DISPLAY);
}

NTSTATUS
DxgkReferenceGdiBltTexture(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _In_ ULONG StagingShare,
    _Out_ PDXGKRNL_GDI_BLT_TEXTURE *OutTexture)
{
    PDXGKRNL_GDI_BLT_TEXTURE Texture = NULL;
    PLIST_ENTRY Link;
    NTSTATUS Status = STATUS_SUCCESS;

    PAGED_CODE();
    *OutTexture = NULL;
    if (Adapter == NULL || StagingShare == 0)
        return STATUS_INVALID_PARAMETER;
    if (!Adapter->GdiHardwareAcceleration)
        return STATUS_NOT_SUPPORTED;

    KeWaitForSingleObject(&Adapter->GdiBltTextureMutex, Executive, KernelMode, FALSE, NULL);
    for (Link = Adapter->GdiBltTextureList.Flink; Link != &Adapter->GdiBltTextureList; Link = Link->Flink)
    {
        PDXGKRNL_GDI_BLT_TEXTURE Candidate = CONTAINING_RECORD(Link, DXGKRNL_GDI_BLT_TEXTURE, Link);

        if (Candidate->StagingShare == StagingShare)
        {
            Texture = Candidate;
            break;
        }
    }
    if (Texture == NULL)
    {
        Status = DxgkpCreateGdiBltTexture(Adapter, StagingShare, &Texture);
        if (NT_SUCCESS(Status))
        {
            InsertTailList(&Adapter->GdiBltTextureList, &Texture->Link);
            Texture->Linked = TRUE;
        }
    }
    if (Texture != NULL)
        InterlockedIncrement(&Texture->ReferenceCount);
    KeReleaseMutex(&Adapter->GdiBltTextureMutex, FALSE);
    *OutTexture = Texture;
    return Status;
}

VOID
DxgkDereferenceGdiBltTexture(
    _In_ PDXGKRNL_GDI_BLT_TEXTURE Texture)
{
    LONG References = InterlockedDecrement(&Texture->ReferenceCount);

    ASSERT(References >= 0);
    if (References == 0)
        DxgkpDestroyGdiBltTexture(Texture);
}

VOID
DxgkRetireGdiBltTexture(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _In_ ULONG StagingShare)
{
    PDXGKRNL_GDI_BLT_TEXTURE Texture = NULL;
    PLIST_ENTRY Link;

    PAGED_CODE();
    if (Adapter == NULL || StagingShare == 0)
        return;
    KeWaitForSingleObject(&Adapter->GdiBltTextureMutex, Executive, KernelMode, FALSE, NULL);
    for (Link = Adapter->GdiBltTextureList.Flink; Link != &Adapter->GdiBltTextureList; Link = Link->Flink)
    {
        PDXGKRNL_GDI_BLT_TEXTURE Candidate = CONTAINING_RECORD(Link, DXGKRNL_GDI_BLT_TEXTURE, Link);

        if (Candidate->StagingShare == StagingShare)
        {
            RemoveEntryList(&Candidate->Link);
            Candidate->Linked = FALSE;
            Texture = Candidate;
            break;
        }
    }
    KeReleaseMutex(&Adapter->GdiBltTextureMutex, FALSE);
    if (Texture != NULL)
        DxgkDereferenceGdiBltTexture(Texture);
}

VOID
DxgkForgetGdiBltTextures(
    _In_ PDXGKRNL_ADAPTER Adapter)
{
    LIST_ENTRY Retired;

    PAGED_CODE();
    InitializeListHead(&Retired);
    KeWaitForSingleObject(&Adapter->GdiBltTextureMutex, Executive, KernelMode, FALSE, NULL);
    while (!IsListEmpty(&Adapter->GdiBltTextureList))
    {
        PLIST_ENTRY Link = RemoveHeadList(&Adapter->GdiBltTextureList);
        PDXGKRNL_GDI_BLT_TEXTURE Texture = CONTAINING_RECORD(Link, DXGKRNL_GDI_BLT_TEXTURE, Link);

        /* The adapter's handles are purged with its devices; nothing is left to close. */
        Texture->Linked = FALSE;
        Texture->CddDevice = 0;
        Texture->CddTextureBinding = 0;
        Texture->CddStagingBinding = 0;
        Texture->TextureAllocation = 0;
        InsertTailList(&Retired, Link);
    }
    KeReleaseMutex(&Adapter->GdiBltTextureMutex, FALSE);
    while (!IsListEmpty(&Retired))
    {
        PLIST_ENTRY Link = RemoveHeadList(&Retired);

        DxgkDereferenceGdiBltTexture(CONTAINING_RECORD(Link, DXGKRNL_GDI_BLT_TEXTURE, Link));
    }
}

static NTSTATUS
DxgkpWaitForRedirectionFence(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _In_ ULONG NodeOrdinal,
    _In_ ULONG SubmissionFenceId)
{
    LARGE_INTEGER Interval;
    ULONGLONG Start100ns;

    if (SubmissionFenceId == 0)
        return STATUS_SUCCESS;
    if (Adapter == NULL || NodeOrdinal >= DXGK_MAX_TRACKED_NODES)
        return STATUS_INVALID_PARAMETER;

    Start100ns = KeQueryInterruptTime();
    Interval.QuadPart = -10 * 10000;
    for (;;)
    {
        ULONG CompletedFenceId;

        KeClearEvent(&Adapter->FenceProgressEvent);
        DxgkRetireCompletedDmaBuffers(Adapter);
        CompletedFenceId = (ULONG)InterlockedCompareExchange(
            (volatile LONG *)&Adapter->NodeLastCompletedFenceId[NodeOrdinal],
            0, 0);
        if (CompletedFenceId != 0 &&
            DxgkpRedirectionFenceReached(CompletedFenceId,
                                         SubmissionFenceId))
        {
            return STATUS_SUCCESS;
        }
        if (InterlockedCompareExchange(&Adapter->SubmitDmaStopping, 0, 0) != 0)
            return STATUS_DEVICE_REMOVED;
        if (KeQueryInterruptTime() - Start100ns >=
            (ULONGLONG)DXGKP_REDIRECTION_SYNC_TIMEOUT_MS * 10000)
        {
            /* The caller must retain the outstanding fence and avoid
             * publishing a synchronized surface until GPU work retires. */
            return STATUS_IO_TIMEOUT;
        }
        (VOID)KeWaitForSingleObject(&Adapter->FenceProgressEvent, Executive, KernelMode, FALSE, &Interval);
    }
}

NTSTATUS
DxgkSynchronizeRedirectionSurfaces(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _Inout_ DXGK_REDIRECTION_SURFACES_SYNC *Sync,
    _In_ ULONG BufferLength)
{
    PDXGKP_REDIRECTION_SYNC_SNAPSHOT Snapshots = NULL;
    ULONG TargetFences[DXGK_MAX_TRACKED_NODES] = {0};
    SIZE_T HeaderSize;
    SIZE_T RequiredSize;
    ULONG Index;
    ULONG Node;
    NTSTATUS Status = STATUS_SUCCESS;

    PAGED_CODE();

    HeaderSize = FIELD_OFFSET(DXGK_REDIRECTION_SURFACES_SYNC, Surfaces);
    if (Adapter == NULL || Sync == NULL || BufferLength < HeaderSize ||
        Sync->Flags != 0 || Sync->Reserved != 0 || Sync->SurfaceCount == 0 ||
        Sync->SurfaceCount > DXGKP_REDIRECTION_SYNC_MAX_SURFACES ||
        Sync->SurfaceCount > (MAXULONG - HeaderSize) /
                             sizeof(Sync->Surfaces[0]))
    {
        return STATUS_INVALID_PARAMETER;
    }

    RequiredSize = HeaderSize +
                   (SIZE_T)Sync->SurfaceCount * sizeof(Sync->Surfaces[0]);
    if (Sync->StructSize != RequiredSize || BufferLength != RequiredSize)
        return STATUS_INFO_LENGTH_MISMATCH;
    Sync->FenceId = 0;

    Snapshots = ExAllocatePoolZero(
                    PagedPool,
                    (SIZE_T)Sync->SurfaceCount * sizeof(*Snapshots),
                    TAG_DXGK_DISPLAY);
    if (Snapshots == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;

    for (Index = 0; Index < Sync->SurfaceCount; ++Index)
    {
        const DXGK_REDIRECTION_SURFACE_SYNC_ENTRY *Entry =
            &Sync->Surfaces[Index];
        PDXGKVMM_RESOURCE Resource = NULL;
        PDXGKVMM_ALLOCATION Allocation = NULL;
        ULONGLONG BottomOffset;
        ULONGLONG TopOffset;

        if (Entry->AllocationHandle == 0 || Entry->ResourceHandle == 0 ||
            Entry->GlobalShare == 0 || Entry->Pitch == 0 ||
            Entry->DirtyRect.left < 0 || Entry->DirtyRect.top < 0 ||
            Entry->DirtyRect.right <= Entry->DirtyRect.left ||
            Entry->DirtyRect.bottom <= Entry->DirtyRect.top ||
            (ULONG)Entry->DirtyRect.right > Entry->Pitch / sizeof(ULONG))
        {
            Status = STATUS_INVALID_PARAMETER;
            break;
        }

        Status = DxgkVidMmReferenceResource(
                     Entry->ResourceHandle, FALSE, NULL, &Resource);
        if (!NT_SUCCESS(Status))
        {
            Status = STATUS_INVALID_PARAMETER;
            break;
        }
        Status = DxgkVidMmReferenceAllocation(
                     (HANDLE)(ULONG_PTR)Entry->AllocationHandle,
                     Adapter, NULL, &Allocation);
        if (!NT_SUCCESS(Status))
        {
            DxgkVidMmDereferenceResource(Resource);
            Status = STATUS_INVALID_PARAMETER;
            break;
        }

        TopOffset = (ULONGLONG)(ULONG)Entry->DirtyRect.top * Entry->Pitch;
        BottomOffset =
            (ULONGLONG)(ULONG)Entry->DirtyRect.bottom * Entry->Pitch;
        if (Resource->Adapter != Adapter ||
            Resource->GlobalShareHandle != Entry->GlobalShare ||
            Resource->AllocationCount != 1 || Allocation->Resource != Resource ||
            BottomOffset <= TopOffset || BottomOffset > Allocation->Size)
        {
            DxgkVidMmDereferenceAllocation(Allocation);
            DxgkVidMmDereferenceResource(Resource);
            Status = STATUS_INVALID_PARAMETER;
            break;
        }
        DxgkVidMmDereferenceResource(Resource);

        (VOID)KeWaitForSingleObject(&Allocation->ResidencyLock,
                                    Executive, KernelMode, FALSE, NULL);
        if (Allocation->RedirectionSurfaceHandle == 0)
        {
            KeReleaseMutex(&Allocation->ResidencyLock, FALSE);
            DxgkVidMmDereferenceAllocation(Allocation);
            Status = STATUS_INVALID_DEVICE_STATE;
            break;
        }
        for (Node = 0; Node < DXGK_MAX_TRACKED_NODES; ++Node)
        {
            ULONG Fence = Allocation->RedirectionSubmittedFenceId[Node];

            Snapshots[Index].Fences[Node] = Fence;
            if (Fence != 0 &&
                (TargetFences[Node] == 0 ||
                 !DxgkpRedirectionFenceReached(TargetFences[Node], Fence)))
            {
                TargetFences[Node] = Fence;
            }
        }
        KeReleaseMutex(&Allocation->ResidencyLock, FALSE);

        Snapshots[Index].Allocation = Allocation;
        Snapshots[Index].CacheOffset = TopOffset;
        Snapshots[Index].CacheLength = BottomOffset - TopOffset;
    }

    if (NT_SUCCESS(Status))
    {
        for (Node = 0; Node < DXGK_MAX_TRACKED_NODES; ++Node)
        {
            Status = DxgkpWaitForRedirectionFence(
                         Adapter, Node, TargetFences[Node]);
            if (!NT_SUCCESS(Status))
                break;
        }
    }

    if (NT_SUCCESS(Status))
    {
        for (Index = 0; Index < Sync->SurfaceCount; ++Index)
        {
            Status = DxgkVidMmWaitForTrackedSubmissions(
                         Snapshots[Index].Allocation, FALSE);
            if (!NT_SUCCESS(Status))
                break;
        }
    }

    if (NT_SUCCESS(Status))
    {
        for (Index = 0; Index < Sync->SurfaceCount; ++Index)
        {
            Status = DxgkVidMmInvalidateReferencedAllocationCache(
                         Snapshots[Index].Allocation,
                         Snapshots[Index].CacheOffset,
                         Snapshots[Index].CacheLength);
            if (!NT_SUCCESS(Status))
                break;
        }
    }

    if (NT_SUCCESS(Status))
    {
        for (Index = 0; Index < Sync->SurfaceCount; ++Index)
        {
            PDXGKVMM_ALLOCATION Allocation = Snapshots[Index].Allocation;

            (VOID)KeWaitForSingleObject(&Allocation->ResidencyLock,
                                        Executive, KernelMode, FALSE, NULL);
            for (Node = 0; Node < DXGK_MAX_TRACKED_NODES; ++Node)
            {
                if (Allocation->RedirectionSubmittedFenceId[Node] ==
                    Snapshots[Index].Fences[Node])
                {
                    Allocation->RedirectionSubmittedFenceId[Node] = 0;
                }
            }
            KeReleaseMutex(&Allocation->ResidencyLock, FALSE);
        }

        Sync->FenceId = (ULONGLONG)InterlockedIncrement64(
            &Adapter->NextRedirectionFenceId);
        if (Sync->FenceId == 0)
        {
            Sync->FenceId = (ULONGLONG)InterlockedIncrement64(
                &Adapter->NextRedirectionFenceId);
        }
    }

    for (Index = 0; Index < Sync->SurfaceCount; ++Index)
    {
        if (Snapshots[Index].Allocation != NULL)
            DxgkVidMmDereferenceAllocation(Snapshots[Index].Allocation);
    }
    ExFreePoolWithTag(Snapshots, TAG_DXGK_DISPLAY);
    return Status;
}
