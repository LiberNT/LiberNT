/*
 * PROJECT:     LiberNT WDDM DirectX Graphics Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Desktop duplication registrations (D3DKMTCreateOutputDupl and the frame entry points)
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arifing193@gmail.com>
 */

#include "dxgkrnl_private.h"
#include <ndk/obfuncs.h>

#define TAG_DXGK_OUTPUTDUPL 'DOxD'
#define DXGK_OUTPUTDUPL_REQUIRED_SURFACES 1

typedef struct _DXGKRNL_OUTPUTDUPL
{
    LIST_ENTRY ListEntry;
    PEPROCESS Process;
    LUID AdapterLuid;
    D3DDDI_VIDEO_PRESENT_SOURCE_ID VidPnSourceId;
    ULONG SurfaceCount;
    PVOID Surfaces[OUTPUTDUPL_CREATE_MAX_KEYEDMUTXES];
    ULONG FrameCount;
} DXGKRNL_OUTPUTDUPL, *PDXGKRNL_OUTPUTDUPL;

static LIST_ENTRY DxgkOutputDuplList = { &DxgkOutputDuplList, &DxgkOutputDuplList };
static KGUARDED_MUTEX DxgkOutputDuplLock;
static volatile LONG DxgkOutputDuplLockState;

static VOID
DxgkpOutputDuplLock(VOID)
{
    if (InterlockedCompareExchange(&DxgkOutputDuplLockState, 1, 0) == 0)
    {
        KeInitializeGuardedMutex(&DxgkOutputDuplLock);
        InterlockedExchange(&DxgkOutputDuplLockState, 2);
    }
    while (InterlockedCompareExchange(&DxgkOutputDuplLockState, 2, 2) != 2)
        YieldProcessor();
    KeAcquireGuardedMutex(&DxgkOutputDuplLock);
}

static VOID
DxgkpOutputDuplUnlock(VOID)
{
    KeReleaseGuardedMutex(&DxgkOutputDuplLock);
}

static VOID
DxgkpOutputDuplFree(_In_ PDXGKRNL_OUTPUTDUPL Dupl)
{
    ULONG Index;

    for (Index = 0; Index < Dupl->SurfaceCount; ++Index)
        ObDereferenceObject(Dupl->Surfaces[Index]);
    ObDereferenceObject(Dupl->Process);
    ExFreePoolWithTag(Dupl, TAG_DXGK_OUTPUTDUPL);
}

static PDXGKRNL_OUTPUTDUPL
DxgkpOutputDuplFindLocked(_In_ PEPROCESS Process, _In_ LUID AdapterLuid, _In_ D3DDDI_VIDEO_PRESENT_SOURCE_ID VidPnSourceId)
{
    PLIST_ENTRY Entry;

    for (Entry = DxgkOutputDuplList.Flink; Entry != &DxgkOutputDuplList; Entry = Entry->Flink)
    {
        PDXGKRNL_OUTPUTDUPL Dupl = CONTAINING_RECORD(Entry, DXGKRNL_OUTPUTDUPL, ListEntry);

        if (Dupl->Process == Process && Dupl->VidPnSourceId == VidPnSourceId &&
            Dupl->AdapterLuid.LowPart == AdapterLuid.LowPart && Dupl->AdapterLuid.HighPart == AdapterLuid.HighPart)
            return Dupl;
    }
    return NULL;
}

/* The caller's adapter and a source it drives, or STATUS_INVALID_PARAMETER. */
static NTSTATUS
DxgkpOutputDuplSource(_In_ D3DKMT_HANDLE hAdapter, _In_ D3DDDI_VIDEO_PRESENT_SOURCE_ID VidPnSourceId, _Out_ LUID *AdapterLuid)
{
    PDXGKRNL_ADAPTER Adapter;
    NTSTATUS Status;

    Status = DxgkReferenceAdapterByHandle(hAdapter, PsGetCurrentProcess(), &Adapter);
    if (!NT_SUCCESS(Status))
        return STATUS_INVALID_PARAMETER;
    *AdapterLuid = Adapter->AdapterLuid;
    Status = VidPnSourceId < Adapter->NumberOfVideoPresentSources ? STATUS_SUCCESS : STATUS_INVALID_PARAMETER;
    DxgkDereferenceAdapter(Adapter);
    return Status;
}

NTSTATUS
DxgkOutputDuplCreate(
    _In_ D3DKMT_HANDLE hAdapter,
    _In_ D3DDDI_VIDEO_PRESENT_SOURCE_ID VidPnSourceId,
    _In_ ULONG SurfaceCount,
    _In_reads_(SurfaceCount) CONST HANDLE *Surfaces,
    _Out_ PULONG RequiredSurfaceCount)
{
    PDXGKRNL_OUTPUTDUPL Dupl;
    POBJECT_TYPE Type = DxgkSharedResourceType();
    LUID AdapterLuid;
    NTSTATUS Status;
    ULONG Index;

    PAGED_CODE();
    *RequiredSurfaceCount = 0;
    Status = DxgkpOutputDuplSource(hAdapter, VidPnSourceId, &AdapterLuid);
    if (!NT_SUCCESS(Status))
        return Status;
    *RequiredSurfaceCount = DXGK_OUTPUTDUPL_REQUIRED_SURFACES;
    if (SurfaceCount == 0)
        return STATUS_SUCCESS;
    if (SurfaceCount > OUTPUTDUPL_CREATE_MAX_KEYEDMUTXES || Type == NULL)
        return STATUS_INVALID_PARAMETER;

    Dupl = ExAllocatePoolZero(PagedPool, sizeof(*Dupl), TAG_DXGK_OUTPUTDUPL);
    if (Dupl == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;
    Dupl->Process = PsGetCurrentProcess();
    ObReferenceObject(Dupl->Process);
    Dupl->AdapterLuid = AdapterLuid;
    Dupl->VidPnSourceId = VidPnSourceId;
    for (Index = 0; Index < SurfaceCount; ++Index)
    {
        Status = ObReferenceObjectByHandle(Surfaces[Index], 0, Type, UserMode, &Dupl->Surfaces[Index], NULL);
        if (!NT_SUCCESS(Status))
        {
            DxgkpOutputDuplFree(Dupl);
            return Status;
        }
        Dupl->SurfaceCount = Index + 1;
    }

    DxgkpOutputDuplLock();
    if (DxgkpOutputDuplFindLocked(Dupl->Process, AdapterLuid, VidPnSourceId) != NULL)
    {
        DxgkpOutputDuplUnlock();
        DxgkpOutputDuplFree(Dupl);
        return STATUS_ALREADY_REGISTERED;
    }
    InsertTailList(&DxgkOutputDuplList, &Dupl->ListEntry);
    DxgkpOutputDuplUnlock();
    return STATUS_SUCCESS;
}

NTSTATUS
DxgkOutputDuplDestroy(
    _In_ D3DKMT_HANDLE hAdapter,
    _In_ D3DDDI_VIDEO_PRESENT_SOURCE_ID VidPnSourceId)
{
    PDXGKRNL_OUTPUTDUPL Dupl;
    LUID AdapterLuid;
    NTSTATUS Status;

    PAGED_CODE();
    Status = DxgkpOutputDuplSource(hAdapter, VidPnSourceId, &AdapterLuid);
    if (!NT_SUCCESS(Status))
        return Status;
    DxgkpOutputDuplLock();
    Dupl = DxgkpOutputDuplFindLocked(PsGetCurrentProcess(), AdapterLuid, VidPnSourceId);
    if (Dupl != NULL)
        RemoveEntryList(&Dupl->ListEntry);
    DxgkpOutputDuplUnlock();
    if (Dupl != NULL)
        DxgkpOutputDuplFree(Dupl);
    return STATUS_SUCCESS;
}

/*
 * GetFrameInfo, GetMetaData, GetPointerShapeData and ReleaseFrame all need a
 * frame the compositor delivered to the caller's duplication; without one
 * Windows 11 answers STATUS_NOT_FOUND.
 */
NTSTATUS
DxgkOutputDuplFrameAccess(
    _In_ D3DKMT_HANDLE hAdapter,
    _In_ D3DDDI_VIDEO_PRESENT_SOURCE_ID VidPnSourceId)
{
    PDXGKRNL_OUTPUTDUPL Dupl;
    LUID AdapterLuid;
    NTSTATUS Status;

    PAGED_CODE();
    Status = DxgkpOutputDuplSource(hAdapter, VidPnSourceId, &AdapterLuid);
    if (!NT_SUCCESS(Status))
        return Status;
    DxgkpOutputDuplLock();
    Dupl = DxgkpOutputDuplFindLocked(PsGetCurrentProcess(), AdapterLuid, VidPnSourceId);
    /* TODO: frames presented by the compositor through D3DKMTOutputDuplPresent */
    Status = (Dupl == NULL || Dupl->FrameCount == 0) ? STATUS_NOT_FOUND : STATUS_SUCCESS;
    DxgkpOutputDuplUnlock();
    return Status;
}

VOID
DxgkOutputDuplProcessCleanup(
    _In_ PEPROCESS Process)
{
    LIST_ENTRY Released;
    PLIST_ENTRY Entry;

    if (InterlockedCompareExchange(&DxgkOutputDuplLockState, 0, 0) == 0)
        return;
    InitializeListHead(&Released);
    DxgkpOutputDuplLock();
    Entry = DxgkOutputDuplList.Flink;
    while (Entry != &DxgkOutputDuplList)
    {
        PDXGKRNL_OUTPUTDUPL Dupl = CONTAINING_RECORD(Entry, DXGKRNL_OUTPUTDUPL, ListEntry);

        Entry = Entry->Flink;
        if (Dupl->Process != Process)
            continue;
        RemoveEntryList(&Dupl->ListEntry);
        InsertTailList(&Released, &Dupl->ListEntry);
    }
    DxgkpOutputDuplUnlock();
    while (!IsListEmpty(&Released))
        DxgkpOutputDuplFree(CONTAINING_RECORD(RemoveHeadList(&Released), DXGKRNL_OUTPUTDUPL, ListEntry));
}
