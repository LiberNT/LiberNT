/*
 * PROJECT:     LiberNT WDDM DirectX Graphics Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     VidPN (Video Present Network) topology management
 * COPYRIGHT:   Copyright 2024-2026 LiberNT WDDM Team
 *
 * This file implements the VidPN manager subsystem for dxgkrnl.  It provides
 * the interface tables that miniport drivers (particularly DOD drivers like
 * KMDOD and viogpudo) use during mode enumeration and commitment:
 *
 *   DxgkCbQueryVidPnInterface    -> DXGK_VIDPN_INTERFACE
 *     pfnGetTopology             -> DXGK_VIDPNTOPOLOGY_INTERFACE
 *     pfnAcquireSourceModeSet    -> DXGK_VIDPNSOURCEMODESET_INTERFACE
 *     pfnAcquireTargetModeSet    -> DXGK_VIDPNTARGETMODESET_INTERFACE
 *
 *   DxgkCbQueryMonitorInterface  -> Monitor interface with
 *     pfnAcquireMonitorSourceModeSet -> DXGK_MONITORSOURCEMODESET_INTERFACE
 *
 * Multi-monitor support:
 *   - Up to DXGKP_MAX_SOURCES sources and DXGKP_MAX_TARGETS targets
 *   - Multiple paths in topology (clone, extend, single)
 *   - Per-source and per-target mode sets with pinning
 *   - VidPN cloning for miniport negotiation
 *   - Hot-plug detection and VidPN rebuild
 *   - VidPN source ownership tracking
 *
 * Memory ordering notes (x86-64):
 *   All VidPN operations run at PASSIVE_LEVEL under serialisation provided
 *   by the calling DDI path (IsSupportedVidPn, EnumVidPnCofuncModality,
 *   CommitVidPn are all PASSIVE_LEVEL and serialised by dxgkrnl's dispatch).
 *   No additional memory barriers are needed beyond the implicit TSO ordering
 *   provided by x86-64.
 */

#include "dxgkrnl_private.h"
#include "vidmm.h"
#include "vidpn.h"
#include "present.h"
#include "pnp.h"
#include "hotplug_work_core.h"
#include <ntddvdeo.h>
#include <reactos/dwmframe.h>
#include <reactos/rddm/rxgkioctl.h>

/* ========================================================================
 * Forward declarations for all interface functions
 * ====================================================================== */

/* Topology interface */
static NTSTATUS APIENTRY VidPnTopology_GetNumPaths(D3DKMDT_HVIDPNTOPOLOGY, SIZE_T*);
static NTSTATUS APIENTRY VidPnTopology_GetNumPathsFromSource(D3DKMDT_HVIDPNTOPOLOGY, D3DDDI_VIDEO_PRESENT_SOURCE_ID, SIZE_T*);
static NTSTATUS APIENTRY VidPnTopology_EnumPathTargetsFromSource(D3DKMDT_HVIDPNTOPOLOGY, D3DDDI_VIDEO_PRESENT_SOURCE_ID, D3DKMDT_VIDPN_PRESENT_PATH_INDEX, D3DDDI_VIDEO_PRESENT_TARGET_ID*);
static NTSTATUS APIENTRY VidPnTopology_GetPathSourceFromTarget(D3DKMDT_HVIDPNTOPOLOGY, D3DDDI_VIDEO_PRESENT_TARGET_ID, D3DDDI_VIDEO_PRESENT_SOURCE_ID*);
static NTSTATUS APIENTRY VidPnTopology_AcquirePathInfo(D3DKMDT_HVIDPNTOPOLOGY, D3DDDI_VIDEO_PRESENT_SOURCE_ID, D3DDDI_VIDEO_PRESENT_TARGET_ID, CONST D3DKMDT_VIDPN_PRESENT_PATH**);
static NTSTATUS APIENTRY VidPnTopology_AcquireFirstPathInfo(D3DKMDT_HVIDPNTOPOLOGY, CONST D3DKMDT_VIDPN_PRESENT_PATH**);
static NTSTATUS APIENTRY VidPnTopology_AcquireNextPathInfo(D3DKMDT_HVIDPNTOPOLOGY, CONST D3DKMDT_VIDPN_PRESENT_PATH*, CONST D3DKMDT_VIDPN_PRESENT_PATH**);
static NTSTATUS APIENTRY VidPnTopology_UpdatePathSupportInfo(D3DKMDT_HVIDPNTOPOLOGY, CONST D3DKMDT_VIDPN_PRESENT_PATH*);
static NTSTATUS APIENTRY VidPnTopology_ReleasePathInfo(D3DKMDT_HVIDPNTOPOLOGY, CONST D3DKMDT_VIDPN_PRESENT_PATH*);
static NTSTATUS APIENTRY VidPnTopology_CreateNewPathInfo(D3DKMDT_HVIDPNTOPOLOGY, D3DKMDT_VIDPN_PRESENT_PATH**);
static NTSTATUS APIENTRY VidPnTopology_AddPath(D3DKMDT_HVIDPNTOPOLOGY, D3DKMDT_VIDPN_PRESENT_PATH*);
static NTSTATUS APIENTRY VidPnTopology_RemovePath(D3DKMDT_HVIDPNTOPOLOGY, D3DDDI_VIDEO_PRESENT_SOURCE_ID, D3DDDI_VIDEO_PRESENT_TARGET_ID);

/* Source mode set interface */
static NTSTATUS APIENTRY VidPnSourceModeSet_GetNumModes(D3DKMDT_HVIDPNSOURCEMODESET, CONST SIZE_T*);
static NTSTATUS APIENTRY VidPnSourceModeSet_AcquireFirstModeInfo(D3DKMDT_HVIDPNSOURCEMODESET, CONST D3DKMDT_VIDPN_SOURCE_MODE**);
static NTSTATUS APIENTRY VidPnSourceModeSet_AcquireNextModeInfo(D3DKMDT_HVIDPNSOURCEMODESET, CONST D3DKMDT_VIDPN_SOURCE_MODE*, CONST D3DKMDT_VIDPN_SOURCE_MODE**);
static NTSTATUS APIENTRY VidPnSourceModeSet_AcquirePinnedModeInfo(D3DKMDT_HVIDPNSOURCEMODESET, CONST D3DKMDT_VIDPN_SOURCE_MODE**);
static NTSTATUS APIENTRY VidPnSourceModeSet_ReleaseModeInfo(D3DKMDT_HVIDPNSOURCEMODESET, CONST D3DKMDT_VIDPN_SOURCE_MODE*);
static NTSTATUS APIENTRY VidPnSourceModeSet_CreateNewModeInfo(D3DKMDT_HVIDPNSOURCEMODESET, D3DKMDT_VIDPN_SOURCE_MODE**);
static NTSTATUS APIENTRY VidPnSourceModeSet_AddMode(D3DKMDT_HVIDPNSOURCEMODESET, D3DKMDT_VIDPN_SOURCE_MODE*);
static NTSTATUS APIENTRY VidPnSourceModeSet_PinMode(D3DKMDT_HVIDPNSOURCEMODESET, D3DKMDT_VIDEO_PRESENT_SOURCE_MODE_ID);

/* Target mode set interface */
static NTSTATUS APIENTRY VidPnTargetModeSet_GetNumModes(D3DKMDT_HVIDPNTARGETMODESET, CONST SIZE_T*);
static NTSTATUS APIENTRY VidPnTargetModeSet_AcquireFirstModeInfo(D3DKMDT_HVIDPNTARGETMODESET, CONST D3DKMDT_VIDPN_TARGET_MODE**);
static NTSTATUS APIENTRY VidPnTargetModeSet_AcquireNextModeInfo(D3DKMDT_HVIDPNTARGETMODESET, CONST D3DKMDT_VIDPN_TARGET_MODE*, CONST D3DKMDT_VIDPN_TARGET_MODE**);
static NTSTATUS APIENTRY VidPnTargetModeSet_AcquirePinnedModeInfo(D3DKMDT_HVIDPNTARGETMODESET, CONST D3DKMDT_VIDPN_TARGET_MODE**);
static NTSTATUS APIENTRY VidPnTargetModeSet_ReleaseModeInfo(D3DKMDT_HVIDPNTARGETMODESET, CONST D3DKMDT_VIDPN_TARGET_MODE*);
static NTSTATUS APIENTRY VidPnTargetModeSet_CreateNewModeInfo(D3DKMDT_HVIDPNTARGETMODESET, D3DKMDT_VIDPN_TARGET_MODE**);
static NTSTATUS APIENTRY VidPnTargetModeSet_AddMode(D3DKMDT_HVIDPNTARGETMODESET, D3DKMDT_VIDPN_TARGET_MODE*);
static NTSTATUS APIENTRY VidPnTargetModeSet_PinMode(D3DKMDT_HVIDPNTARGETMODESET, D3DKMDT_VIDEO_PRESENT_TARGET_MODE_ID);

/* Monitor source mode set interface */
static NTSTATUS APIENTRY MonitorSourceModeSet_GetNumModes(D3DKMDT_HMONITORSOURCEMODESET, SIZE_T*);
static NTSTATUS APIENTRY MonitorSourceModeSet_AcquirePreferredModeInfo(D3DKMDT_HMONITORSOURCEMODESET, CONST D3DKMDT_MONITOR_SOURCE_MODE**);
static NTSTATUS APIENTRY MonitorSourceModeSet_AcquireFirstModeInfo(D3DKMDT_HMONITORSOURCEMODESET, CONST D3DKMDT_MONITOR_SOURCE_MODE**);
static NTSTATUS APIENTRY MonitorSourceModeSet_AcquireNextModeInfo(D3DKMDT_HMONITORSOURCEMODESET, CONST D3DKMDT_MONITOR_SOURCE_MODE*, CONST D3DKMDT_MONITOR_SOURCE_MODE**);
static NTSTATUS APIENTRY MonitorSourceModeSet_CreateNewModeInfo(D3DKMDT_HMONITORSOURCEMODESET, D3DKMDT_MONITOR_SOURCE_MODE**);
static NTSTATUS APIENTRY MonitorSourceModeSet_AddMode(D3DKMDT_HMONITORSOURCEMODESET, D3DKMDT_MONITOR_SOURCE_MODE*);
static NTSTATUS APIENTRY MonitorSourceModeSet_ReleaseModeInfo(D3DKMDT_HMONITORSOURCEMODESET, CONST D3DKMDT_MONITOR_SOURCE_MODE*);

/* Top-level VidPN interface */
static NTSTATUS APIENTRY VidPn_GetTopology(D3DKMDT_HVIDPN, D3DKMDT_HVIDPNTOPOLOGY*, CONST DXGK_VIDPNTOPOLOGY_INTERFACE**);
static NTSTATUS APIENTRY VidPn_AcquireSourceModeSet(D3DKMDT_HVIDPN, D3DDDI_VIDEO_PRESENT_SOURCE_ID, D3DKMDT_HVIDPNSOURCEMODESET*, CONST DXGK_VIDPNSOURCEMODESET_INTERFACE**);
static NTSTATUS APIENTRY VidPn_ReleaseSourceModeSet(D3DKMDT_HVIDPN, D3DKMDT_HVIDPNSOURCEMODESET);
static NTSTATUS APIENTRY VidPn_CreateNewSourceModeSet(D3DKMDT_HVIDPN, D3DDDI_VIDEO_PRESENT_SOURCE_ID, D3DKMDT_HVIDPNSOURCEMODESET*, CONST DXGK_VIDPNSOURCEMODESET_INTERFACE**);
static NTSTATUS APIENTRY VidPn_AssignSourceModeSet(D3DKMDT_HVIDPN, D3DDDI_VIDEO_PRESENT_SOURCE_ID, D3DKMDT_HVIDPNSOURCEMODESET);
static NTSTATUS APIENTRY VidPn_AssignMultisamplingMethodSet(D3DKMDT_HVIDPN, D3DDDI_VIDEO_PRESENT_SOURCE_ID, CONST SIZE_T, CONST D3DDDI_MULTISAMPLINGMETHOD*);
static NTSTATUS APIENTRY VidPn_AcquireTargetModeSet(D3DKMDT_HVIDPN, D3DDDI_VIDEO_PRESENT_TARGET_ID, D3DKMDT_HVIDPNTARGETMODESET*, CONST DXGK_VIDPNTARGETMODESET_INTERFACE**);
static NTSTATUS APIENTRY VidPn_ReleaseTargetModeSet(D3DKMDT_HVIDPN, D3DKMDT_HVIDPNTARGETMODESET);
static NTSTATUS APIENTRY VidPn_CreateNewTargetModeSet(D3DKMDT_HVIDPN, D3DDDI_VIDEO_PRESENT_TARGET_ID, D3DKMDT_HVIDPNTARGETMODESET*, CONST DXGK_VIDPNTARGETMODESET_INTERFACE**);
static NTSTATUS APIENTRY VidPn_AssignTargetModeSet(D3DKMDT_HVIDPN, D3DDDI_VIDEO_PRESENT_TARGET_ID, D3DKMDT_HVIDPNTARGETMODESET);

/* Monitor interface */
static NTSTATUS APIENTRY Monitor_AcquireMonitorSourceModeSet(D3DKMDT_ADAPTER, D3DDDI_VIDEO_PRESENT_TARGET_ID, D3DKMDT_HMONITORSOURCEMODESET*, CONST DXGK_MONITORSOURCEMODESET_INTERFACE**);
static NTSTATUS APIENTRY Monitor_ReleaseMonitorSourceModeSet(D3DKMDT_ADAPTER, D3DKMDT_HMONITORSOURCEMODESET);
static NTSTATUS APIENTRY Monitor_GetMonitorFrequencyRangeSet(D3DKMDT_ADAPTER, D3DDDI_VIDEO_PRESENT_TARGET_ID, D3DKMDT_HMONITORFREQUENCYRANGESET*, CONST DXGK_MONITORFREQUENCYRANGESET_INTERFACE**);
static NTSTATUS APIENTRY Monitor_GetMonitorDescriptorSet(D3DKMDT_ADAPTER, D3DDDI_VIDEO_PRESENT_TARGET_ID, D3DKMDT_HMONITORDESCRIPTORSET*, CONST DXGK_MONITORDESCRIPTORSET_INTERFACE**);
static NTSTATUS APIENTRY Monitor_GetAdditionalMonitorModeSet(D3DKMDT_ADAPTER, D3DDDI_VIDEO_PRESENT_TARGET_ID, UINT*, DXGK_TARGETMODE_DETAIL_TIMING**);
static NTSTATUS APIENTRY Monitor_ReleaseAdditionalMonitorModeSet(D3DKMDT_ADAPTER, D3DDDI_VIDEO_PRESENT_TARGET_ID, CONST DXGK_TARGETMODE_DETAIL_TIMING*);
static VOID DxgkpDestroySharedPrimaryLocked(PDXGKRNL_ADAPTER);

/* ========================================================================
 * Static interface tables
 * ====================================================================== */

static CONST DXGK_VIDPNTOPOLOGY_INTERFACE g_VidPnTopologyInterface =
{
    VidPnTopology_GetNumPaths,
    VidPnTopology_GetNumPathsFromSource,
    VidPnTopology_EnumPathTargetsFromSource,
    VidPnTopology_GetPathSourceFromTarget,
    VidPnTopology_AcquirePathInfo,
    VidPnTopology_AcquireFirstPathInfo,
    VidPnTopology_AcquireNextPathInfo,
    VidPnTopology_UpdatePathSupportInfo,
    VidPnTopology_ReleasePathInfo,
    VidPnTopology_CreateNewPathInfo,
    VidPnTopology_AddPath,
    VidPnTopology_RemovePath,
};

static CONST DXGK_VIDPNSOURCEMODESET_INTERFACE g_VidPnSourceModeSetInterface =
{
    VidPnSourceModeSet_GetNumModes,
    VidPnSourceModeSet_AcquireFirstModeInfo,
    VidPnSourceModeSet_AcquireNextModeInfo,
    VidPnSourceModeSet_AcquirePinnedModeInfo,
    VidPnSourceModeSet_ReleaseModeInfo,
    VidPnSourceModeSet_CreateNewModeInfo,
    VidPnSourceModeSet_AddMode,
    VidPnSourceModeSet_PinMode,
};

static CONST DXGK_VIDPNTARGETMODESET_INTERFACE g_VidPnTargetModeSetInterface =
{
    VidPnTargetModeSet_GetNumModes,
    VidPnTargetModeSet_AcquireFirstModeInfo,
    VidPnTargetModeSet_AcquireNextModeInfo,
    VidPnTargetModeSet_AcquirePinnedModeInfo,
    VidPnTargetModeSet_ReleaseModeInfo,
    VidPnTargetModeSet_CreateNewModeInfo,
    VidPnTargetModeSet_AddMode,
    VidPnTargetModeSet_PinMode,
};

static CONST DXGK_MONITORSOURCEMODESET_INTERFACE g_MonitorSourceModeSetInterface =
{
    MonitorSourceModeSet_GetNumModes,
    MonitorSourceModeSet_AcquirePreferredModeInfo,
    MonitorSourceModeSet_AcquireFirstModeInfo,
    MonitorSourceModeSet_AcquireNextModeInfo,
    MonitorSourceModeSet_CreateNewModeInfo,
    MonitorSourceModeSet_AddMode,
    MonitorSourceModeSet_ReleaseModeInfo,
};

static CONST DXGK_VIDPN_INTERFACE g_VidPnInterfaceV1 =
{
    DXGK_VIDPN_INTERFACE_VERSION_V1,
    VidPn_GetTopology,
    VidPn_AcquireSourceModeSet,
    VidPn_ReleaseSourceModeSet,
    VidPn_CreateNewSourceModeSet,
    VidPn_AssignSourceModeSet,
    VidPn_AssignMultisamplingMethodSet,
    VidPn_AcquireTargetModeSet,
    VidPn_ReleaseTargetModeSet,
    VidPn_CreateNewTargetModeSet,
    VidPn_AssignTargetModeSet,
};

#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_9)
/*
 * WDDM 2.9 added interface version V2 without changing the
 * DXGK_VIDPN_INTERFACE layout or its callback signatures.  Keep a distinct
 * table because a miniport is allowed to validate that Version matches the
 * version it requested.
 */
static CONST DXGK_VIDPN_INTERFACE g_VidPnInterfaceV2 =
{
    DXGK_VIDPN_INTERFACE_VERSION_V2,
    VidPn_GetTopology,
    VidPn_AcquireSourceModeSet,
    VidPn_ReleaseSourceModeSet,
    VidPn_CreateNewSourceModeSet,
    VidPn_AssignSourceModeSet,
    VidPn_AssignMultisamplingMethodSet,
    VidPn_AcquireTargetModeSet,
    VidPn_ReleaseTargetModeSet,
    VidPn_CreateNewTargetModeSet,
    VidPn_AssignTargetModeSet,
};
#endif

NTSTATUS
DxgkVidPnResolveTargetForSource(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _In_ D3DDDI_VIDEO_PRESENT_SOURCE_ID VidPnSourceId,
    _Out_ D3DDDI_VIDEO_PRESENT_TARGET_ID *VidPnTargetId)
{
    PDXGKP_VIDPN VidPn;
    SIZE_T Index;
    NTSTATUS Status = STATUS_GRAPHICS_SOURCE_NOT_IN_TOPOLOGY;

    if (Adapter == NULL || VidPnTargetId == NULL)
        return STATUS_INVALID_PARAMETER;

    *VidPnTargetId = D3DDDI_ID_UNINITIALIZED;
    (VOID)KeWaitForSingleObject(&Adapter->VidPnMutex, Executive, KernelMode, FALSE, NULL);
    VidPn = (PDXGKP_VIDPN)Adapter->VidPn;
    if (VidPn == NULL || VidPn->Signature != DXGKP_VIDPN_SIGNATURE || VidPnSourceId >= VidPn->NumSources)
    {
        Status = STATUS_GRAPHICS_INVALID_VIDEO_PRESENT_SOURCE;
    }
    else
    {
        for (Index = 0; Index < VidPn->NumPaths; ++Index)
        {
            if (VidPn->Paths[Index].VidPnSourceId != VidPnSourceId)
                continue;
            *VidPnTargetId = VidPn->Paths[Index].VidPnTargetId;
            Status = STATUS_SUCCESS;
            break;
        }
    }
    KeReleaseMutex(&Adapter->VidPnMutex, FALSE);
    return Status;
}

static CONST DXGK_MONITOR_INTERFACE g_MonitorInterfaceV1 =
{
    DXGK_MONITOR_INTERFACE_VERSION_V1,
    Monitor_AcquireMonitorSourceModeSet,
    Monitor_ReleaseMonitorSourceModeSet,
    Monitor_GetMonitorFrequencyRangeSet,
    Monitor_GetMonitorDescriptorSet,
};

static CONST DXGK_MONITOR_INTERFACE_V2 g_MonitorInterfaceV2 =
{
    DXGK_MONITOR_INTERFACE_VERSION_V2,
    Monitor_AcquireMonitorSourceModeSet,
    Monitor_ReleaseMonitorSourceModeSet,
    Monitor_GetMonitorFrequencyRangeSet,
    Monitor_GetMonitorDescriptorSet,
    Monitor_GetAdditionalMonitorModeSet,
    Monitor_ReleaseAdditionalMonitorModeSet,
};

/* ========================================================================
 * VidPN source ownership tracking
 *
 * A source identifier is only meaningful within one adapter.  Keep one
 * state object for each adapter that currently has at least one owner rather
 * than aliasing source zero from every adapter through one global array.
 * The state object does not need a separate adapter reference: every stored
 * owner is a live device, and each live device pins its adapter.  Empty state
 * objects are detached and freed before the last owner can disappear.
 * ====================================================================== */
typedef struct _DXGKP_SOURCE_OWNER_ADAPTER_STATE
{
    LIST_ENTRY Entry;
    PDXGKRNL_ADAPTER Adapter;
    DXGKP_VIDPN_SOURCE_OWNER Owners[DXGKP_MAX_SOURCES];
} DXGKP_SOURCE_OWNER_ADAPTER_STATE, *PDXGKP_SOURCE_OWNER_ADAPTER_STATE;

#define DXGKP_MAX_SOURCE_OWNER_OPERATIONS 4096U
#define DXGKP_STDALLOC_MAX_PRIVATE_SIZE   (64U * 1024U)
#define DXGKP_SOURCE_OWNER_FLAG_ALLOW_OUTPUT_DUPLICATION 0x00000001U
#define DXGKP_SOURCE_OWNER_FLAG_DISABLE_DWM_VIRTUAL_MODE 0x00000002U
#define DXGKP_SOURCE_OWNER_FLAG_USE_NT_HANDLES           0x00000004U
#define DXGKP_SOURCE_OWNER_FLAG_VALID_MASK               0x00000007U
#define TAG_DXGK_SOURCE_OWNER 'OxgD'

static LIST_ENTRY g_SourceOwnerAdapterList;
typedef struct _DXGKP_COMPOSITOR_SOURCE_OWNER
{
    LIST_ENTRY Entry;
    LUID AdapterLuid;
    D3DDDI_VIDEO_PRESENT_SOURCE_ID SourceId;
    PEPROCESS Process;
    PDXGKRNL_DEVICE Device;
    ULONGLONG Window;
    ULONGLONG Generation;
    UINT Width;
    UINT Height;
} DXGKP_COMPOSITOR_SOURCE_OWNER, *PDXGKP_COMPOSITOR_SOURCE_OWNER;

static LIST_ENTRY g_CompositorSourceOwnerList;
static FAST_MUTEX g_SourceOwnerMutex;
/* Programming holds this shared; ownership transitions take it exclusive.
 * A late CDD DMA retirement must check ownership at SetSourceAddress time,
 * and a new claim must drain any CDD programming admitted before it. */
static ERESOURCE g_SourceProgrammingResource;
static volatile LONG g_SourceOwnerState = 0;

static VOID
DxgkpEnsureSourceOwnerMutex(VOID)
{
    LONG State = InterlockedCompareExchange(&g_SourceOwnerState, 1, 0);

    if (State == 0)
    {
        ExInitializeFastMutex(&g_SourceOwnerMutex);
        InitializeListHead(&g_SourceOwnerAdapterList);
        InitializeListHead(&g_CompositorSourceOwnerList);
        ExInitializeResourceLite(&g_SourceProgrammingResource);
        InterlockedExchange(&g_SourceOwnerState, 2);
        return;
    }

    while (InterlockedCompareExchange(&g_SourceOwnerState, 2, 2) != 2)
        YieldProcessor();
}

static PDXGKP_SOURCE_OWNER_ADAPTER_STATE
DxgkpFindSourceOwnerAdapterLocked(
    _In_ PDXGKRNL_ADAPTER Adapter)
{
    PLIST_ENTRY Entry;

    for (Entry = g_SourceOwnerAdapterList.Flink; Entry != &g_SourceOwnerAdapterList; Entry = Entry->Flink)
    {
        PDXGKP_SOURCE_OWNER_ADAPTER_STATE State = CONTAINING_RECORD(Entry, DXGKP_SOURCE_OWNER_ADAPTER_STATE, Entry);

        if (State->Adapter == Adapter)
            return State;
    }

    return NULL;
}

static PDXGKP_COMPOSITOR_SOURCE_OWNER
DxgkpFindCompositorSourceOwnerLocked(
    _In_ CONST LUID *AdapterLuid,
    _In_ D3DDDI_VIDEO_PRESENT_SOURCE_ID SourceId)
{
    PLIST_ENTRY Entry;

    for (Entry = g_CompositorSourceOwnerList.Flink;
         Entry != &g_CompositorSourceOwnerList;
         Entry = Entry->Flink)
    {
        PDXGKP_COMPOSITOR_SOURCE_OWNER Owner =
            CONTAINING_RECORD(Entry, DXGKP_COMPOSITOR_SOURCE_OWNER, Entry);

        if (Owner->AdapterLuid.LowPart == AdapterLuid->LowPart &&
            Owner->AdapterLuid.HighPart == AdapterLuid->HighPart &&
            Owner->SourceId == SourceId)
            return Owner;
    }
    return NULL;
}

NTSTATUS
DxgkpSetCompositorSourceOwner(
    _In_ CONST RXGK_SETCOMPOSITORSOURCEOWNER_PACKET *Packet)
{
    PDXGKRNL_ADAPTER Adapters[16];
    PDXGKRNL_ADAPTER Adapter = NULL;
    PDXGKP_SOURCE_OWNER_ADAPTER_STATE State;
    PDXGKP_COMPOSITOR_SOURCE_OWNER Owner, NewOwner = NULL, RetiredOwner = NULL;
    PEPROCESS Process = PsGetCurrentProcess();
    LUID AdapterLuid;
    ULONG Count = 0, Index;
    BOOLEAN CddLocked = FALSE;
    NTSTATUS Status = STATUS_SUCCESS;

    PAGED_CODE();
    if (Packet == NULL || Packet->Size != sizeof(*Packet) ||
        Packet->Version != RXGK_WDDM_PACKET_VERSION_1 ||
        Packet->VidPnSourceId >= DXGKP_MAX_SOURCES ||
        Packet->ProcessId == 0 || Packet->Window == 0 || Packet->Generation == 0 ||
        (Packet->Action != RXGK_COMPOSITOR_SOURCE_CLAIM &&
         Packet->Action != RXGK_COMPOSITOR_SOURCE_RELEASE))
        return STATUS_INVALID_PARAMETER;

    AdapterLuid.LowPart = Packet->AdapterLuidLowPart;
    AdapterLuid.HighPart = Packet->AdapterLuidHighPart;
    if (Packet->Action == RXGK_COMPOSITOR_SOURCE_CLAIM)
    {
        if (Packet->ProcessId != (ULONGLONG)(ULONG_PTR)PsGetProcessId(Process))
            return STATUS_ACCESS_DENIED;
        if (Packet->Width == 0 || Packet->Height == 0 ||
            Packet->Width > MAXLONG || Packet->Height > MAXLONG)
            return STATUS_INVALID_PARAMETER;
        Count = DxgkReferenceStartedAdapters(Adapters, RTL_NUMBER_OF(Adapters));
        for (Index = 0; Index < Count; ++Index)
        {
            if (Adapters[Index]->AdapterLuid.LowPart == AdapterLuid.LowPart &&
                Adapters[Index]->AdapterLuid.HighPart == AdapterLuid.HighPart)
                Adapter = Adapters[Index];
        }
        if (Adapter == NULL || Packet->VidPnSourceId >= Adapter->NumberOfVideoPresentSources)
        {
            Status = STATUS_INVALID_PARAMETER;
            goto Cleanup;
        }
        NewOwner = ExAllocatePoolWithTag(NonPagedPool, sizeof(*NewOwner), TAG_DXGK_SOURCE_OWNER);
        if (NewOwner == NULL)
        {
            Status = STATUS_INSUFFICIENT_RESOURCES;
            goto Cleanup;
        }
        RtlZeroMemory(NewOwner, sizeof(*NewOwner));
        NewOwner->AdapterLuid = AdapterLuid;
        NewOwner->SourceId = Packet->VidPnSourceId;
        NewOwner->Process = Process;
        ObReferenceObject(Process);
        NewOwner->Window = Packet->Window;
        NewOwner->Generation = Packet->Generation;
        NewOwner->Width = Packet->Width;
        NewOwner->Height = Packet->Height;
    }
    else if (Packet->Width != 0 || Packet->Height != 0)
        return STATUS_INVALID_PARAMETER;

    /* Finish the old GDI desktop copy before the compositor can publish its
     * first frame. CDD checks ownership under this same mutex. */
    if (Packet->Action == RXGK_COMPOSITOR_SOURCE_CLAIM)
    {
        KeWaitForSingleObject(&Adapter->CddPresentMutex, Executive, KernelMode, FALSE, NULL);
        CddLocked = TRUE;
    }
    DxgkpEnsureSourceOwnerMutex();
    KeEnterCriticalRegion();
    ExAcquireResourceExclusiveLite(&g_SourceProgrammingResource, TRUE);
    ExAcquireFastMutex(&g_SourceOwnerMutex);
    Owner = DxgkpFindCompositorSourceOwnerLocked(&AdapterLuid, Packet->VidPnSourceId);
    if (Packet->Action == RXGK_COMPOSITOR_SOURCE_RELEASE)
    {
        /* Release may run after process exit or adapter rundown. Compare the
         * saved identity against the referenced process, never a reused PID. */
        if (Owner == NULL ||
            Packet->ProcessId != (ULONGLONG)(ULONG_PTR)PsGetProcessId(Owner->Process) ||
            Owner->Window != Packet->Window || Owner->Generation != Packet->Generation)
            Status = STATUS_NOT_FOUND;
        else
        {
            RemoveEntryList(&Owner->Entry);
            RetiredOwner = Owner;
        }
    }
    else if (PsGetProcessExitStatus(Process) != STATUS_PENDING)
        Status = STATUS_PROCESS_IS_TERMINATING;
    else if (Adapter->State != DxgkAdapterStateStarted ||
             InterlockedCompareExchange(&Adapter->RundownStarted, 0, 0) != 0)
        Status = STATUS_DELETE_PENDING;
    else if (Owner != NULL)
    {
        if (Owner->Process != Process || Owner->Window != Packet->Window ||
            Owner->Generation != Packet->Generation ||
            Owner->Width != Packet->Width || Owner->Height != Packet->Height)
            Status = STATUS_GRAPHICS_VIDPN_SOURCE_IN_USE;
    }
    else
    {
        State = DxgkpFindSourceOwnerAdapterLocked(Adapter);
        if (State != NULL && State->Owners[Packet->VidPnSourceId].OwnerDevice != NULL)
            Status = STATUS_GRAPHICS_VIDPN_SOURCE_IN_USE;
        else
        {
            InsertTailList(&g_CompositorSourceOwnerList, &NewOwner->Entry);
            NewOwner = NULL;
        }
    }
    ExReleaseFastMutex(&g_SourceOwnerMutex);
    ExReleaseResourceLite(&g_SourceProgrammingResource);
    KeLeaveCriticalRegion();

Cleanup:
    if (CddLocked)
        KeReleaseMutex(&Adapter->CddPresentMutex, FALSE);
    if (NewOwner != NULL)
    {
        ObDereferenceObject(NewOwner->Process);
        ExFreePoolWithTag(NewOwner, TAG_DXGK_SOURCE_OWNER);
    }
    if (RetiredOwner != NULL)
    {
        ObDereferenceObject(RetiredOwner->Process);
        ExFreePoolWithTag(RetiredOwner, TAG_DXGK_SOURCE_OWNER);
    }
    for (Index = 0; Index < Count; ++Index)
        DxgkDereferenceAdapter(Adapters[Index]);
    return Status;
}

ULONG64
DxgkVidPnGetCompositorGeneration(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _In_ D3DDDI_VIDEO_PRESENT_SOURCE_ID SourceId)
{
    PDXGKP_COMPOSITOR_SOURCE_OWNER Owner;
    ULONG64 Generation = 0;

    DxgkpEnsureSourceOwnerMutex();
    ExAcquireFastMutex(&g_SourceOwnerMutex);
    Owner = DxgkpFindCompositorSourceOwnerLocked(&Adapter->AdapterLuid, SourceId);
    if (Owner != NULL)
        Generation = Owner->Generation;
    ExReleaseFastMutex(&g_SourceOwnerMutex);
    return Generation;
}

ULONG64
DxgkVidPnCaptureCompositorGeneration(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _In_ PDXGKRNL_DEVICE Device,
    _In_ D3DDDI_VIDEO_PRESENT_SOURCE_ID SourceId,
    _In_ ULONG_PTR Window)
{
    PDXGKP_COMPOSITOR_SOURCE_OWNER Owner;
    ULONG64 Generation = 0;

    DxgkpEnsureSourceOwnerMutex();
    ExAcquireFastMutex(&g_SourceOwnerMutex);
    Owner = DxgkpFindCompositorSourceOwnerLocked(&Adapter->AdapterLuid, SourceId);
    if (Owner != NULL && Owner->Process == Device->OwnerProcess && Owner->Window == Window)
        Generation = Owner->Generation;
    ExReleaseFastMutex(&g_SourceOwnerMutex);
    return Generation;
}

NTSTATUS
DxgkVidPnAcquireScanoutLease(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _In_opt_ PDXGKRNL_DEVICE Device,
    _In_ D3DDDI_VIDEO_PRESENT_SOURCE_ID SourceId,
    _In_ ULONG_PTR Window,
    _In_ ULONG64 Generation,
    _Out_ PBOOLEAN Program)
{
    PDXGKP_COMPOSITOR_SOURCE_OWNER Compositor;
    PDXGKP_SOURCE_OWNER_ADAPTER_STATE State;
    PDXGKP_VIDPN_SOURCE_OWNER Owner = NULL;
    NTSTATUS Status = STATUS_SUCCESS;

    PAGED_CODE();
    *Program = FALSE;
    if (SourceId >= DXGKP_MAX_SOURCES || SourceId >= Adapter->NumberOfVideoPresentSources)
        return STATUS_INVALID_PARAMETER;
    DxgkpEnsureSourceOwnerMutex();
    KeEnterCriticalRegion();
    ExAcquireResourceSharedLite(&g_SourceProgrammingResource, TRUE);
    ExAcquireFastMutex(&g_SourceOwnerMutex);
    Compositor = DxgkpFindCompositorSourceOwnerLocked(&Adapter->AdapterLuid, SourceId);
    State = DxgkpFindSourceOwnerAdapterLocked(Adapter);
    if (State != NULL && State->Owners[SourceId].OwnerDevice != NULL &&
        State->Owners[SourceId].OwnerType != D3DKMT_VIDPNSOURCEOWNER_EMULATED)
        Owner = &State->Owners[SourceId];

    if (Device == NULL)
    {
        /* The CDD copy has completed. Suppressing its address update does not
         * discard pixels or fabricate completion of any GPU work. */
        *Program = Compositor == NULL && Owner == NULL;
    }
    else if (Device->Adapter != Adapter ||
             InterlockedCompareExchange(&Device->Destroying, 0, 0) != 0 ||
             InterlockedCompareExchange(&Device->ExecutionState, 0, 0) != D3DKMT_DEVICEEXECUTION_ACTIVE)
        Status = STATUS_DEVICE_REMOVED;
    else if (Generation != 0)
    {
        if (Compositor == NULL || Compositor->Generation != Generation ||
            Compositor->Window != Window || Compositor->Process != Device->OwnerProcess ||
            (Compositor->Device != NULL && Compositor->Device != Device))
            Status = STATUS_GRAPHICS_VIDPN_SOURCE_IN_USE;
        else if (Owner != NULL && Owner->OwnerDevice != Device)
        {
            /* An exclusive application temporarily occludes the registered
             * compositor. Its identity remains valid when that owner yields. */
            Status = Owner->OwnerType == D3DKMT_VIDPNSOURCEOWNER_EXCLUSIVE ||
                     Owner->OwnerType == D3DKMT_VIDPNSOURCEOWNER_EXCLUSIVEGDI
                         ? STATUS_GRAPHICS_PRESENT_OCCLUDED
                         : STATUS_GRAPHICS_VIDPN_SOURCE_IN_USE;
        }
        else
        {
            Compositor->Device = Device;
            *Program = TRUE;
        }
    }
    else if (Owner != NULL && Owner->OwnerDevice == Device)
        *Program = TRUE;
    else
        Status = STATUS_GRAPHICS_VIDPN_SOURCE_IN_USE;
    ExReleaseFastMutex(&g_SourceOwnerMutex);
    if (*Program)
    {
        /* Rendering/copies still retire normally for a detached desktop;
         * there is no target on which to program a primary address. */
        (VOID)KeWaitForSingleObject(&Adapter->VidPnMutex, Executive, KernelMode, FALSE, NULL);
        if (Adapter->HeadlessDesktop)
            *Program = FALSE;
        KeReleaseMutex(&Adapter->VidPnMutex, FALSE);
    }
    if (!*Program)
    {
        ExReleaseResourceLite(&g_SourceProgrammingResource);
        KeLeaveCriticalRegion();
    }
    return Status;
}

VOID
DxgkVidPnReleaseScanoutLease(VOID)
{
    ExReleaseResourceLite(&g_SourceProgrammingResource);
    KeLeaveCriticalRegion();
}

static BOOLEAN
DxgkpSourceOwnerStateIsEmpty(
    _In_reads_(DXGKP_MAX_SOURCES) CONST DXGKP_VIDPN_SOURCE_OWNER *Owners)
{
    ULONG Index;

    for (Index = 0; Index < DXGKP_MAX_SOURCES; ++Index)
    {
        if (Owners[Index].OwnerDevice != NULL)
            return FALSE;
    }

    return TRUE;
}

static NTSTATUS
DxgkpApplySourceOwnerOperation(
    _Inout_updates_(DXGKP_MAX_SOURCES) DXGKP_VIDPN_SOURCE_OWNER *Owners,
    _In_ PDXGKRNL_DEVICE OwnerDevice,
    _In_ D3DDDI_VIDEO_PRESENT_SOURCE_ID SourceId,
    _In_ D3DKMT_VIDPNSOURCEOWNER_TYPE RequestedType,
    _In_ UINT OwnerFlags)
{
    PDXGKP_VIDPN_SOURCE_OWNER Current = &Owners[SourceId];

    if (RequestedType == D3DKMT_VIDPNSOURCEOWNER_UNOWNED)
    {
        if (Current->OwnerDevice == OwnerDevice)
        {
            Current->OwnerDevice = NULL;
            Current->OwnerType = D3DKMT_VIDPNSOURCEOWNER_UNOWNED;
            Current->OwnerFlags = 0;
        }
        return STATUS_SUCCESS;
    }

    if (RequestedType == D3DKMT_VIDPNSOURCEOWNER_SHARED)
    {
        if (Current->OwnerDevice == OwnerDevice && (Current->OwnerType == D3DKMT_VIDPNSOURCEOWNER_EXCLUSIVE || Current->OwnerType == D3DKMT_VIDPNSOURCEOWNER_EXCLUSIVEGDI))
            return STATUS_INVALID_PARAMETER;
        return STATUS_GRAPHICS_VIDPN_SOURCE_IN_USE;
    }

    if (RequestedType == D3DKMT_VIDPNSOURCEOWNER_EXCLUSIVE || RequestedType == D3DKMT_VIDPNSOURCEOWNER_EXCLUSIVEGDI)
    {
        if (Current->OwnerDevice == NULL || Current->OwnerType == D3DKMT_VIDPNSOURCEOWNER_SHARED)
        {
            Current->OwnerDevice = OwnerDevice;
            Current->OwnerType = RequestedType;
            Current->OwnerFlags = OwnerFlags;
            return STATUS_SUCCESS;
        }
        if (Current->OwnerDevice == OwnerDevice && Current->OwnerType == RequestedType)
        {
            Current->OwnerFlags = OwnerFlags;
            return STATUS_SUCCESS;
        }
        if (Current->OwnerDevice == OwnerDevice)
            return STATUS_INVALID_PARAMETER;
        return STATUS_GRAPHICS_VIDPN_SOURCE_IN_USE;
    }

    if (Current->OwnerDevice == NULL)
    {
        Current->OwnerDevice = OwnerDevice;
        Current->OwnerType = D3DKMT_VIDPNSOURCEOWNER_EMULATED;
        Current->OwnerFlags = OwnerFlags;
        return STATUS_SUCCESS;
    }
    if (Current->OwnerDevice == OwnerDevice && Current->OwnerType == D3DKMT_VIDPNSOURCEOWNER_EMULATED)
    {
        Current->OwnerFlags = OwnerFlags;
        return STATUS_SUCCESS;
    }
    if (Current->OwnerDevice == OwnerDevice && (Current->OwnerType == D3DKMT_VIDPNSOURCEOWNER_EXCLUSIVE || Current->OwnerType == D3DKMT_VIDPNSOURCEOWNER_EXCLUSIVEGDI))
        return STATUS_INVALID_PARAMETER;
    return STATUS_GRAPHICS_VIDPN_SOURCE_IN_USE;
}

/* ========================================================================
 * DxgkpDeviceOwnsVidPnSource
 *
 * Returns TRUE if hDevice currently has real primary ownership of
 * VidPnSourceId (SHARED, EXCLUSIVE, or EXCLUSIVEGDI).  EMULATED ownership is
 * deliberately excluded: it reserves gamma control but has no real primary
 * ownership.  The present path uses this to decide whether a present may scan
 * out to the primary.
 * This mirrors Windows, where the desktop compositor owns the primary and an
 * ordinary app present is composited into the app's window rather than
 * overwriting the live desktop.  Without it, any process presenting an
 * arbitrary surface to source 0 would paint directly over the desktop.
 * ====================================================================== */
BOOLEAN
DxgkpDeviceOwnsVidPnSource(
    _In_ D3DKMT_HANDLE hDevice,
    _In_ D3DDDI_VIDEO_PRESENT_SOURCE_ID VidPnSourceId)
{
    PDXGKP_SOURCE_OWNER_ADAPTER_STATE State;
    PDXGKRNL_ADAPTER Adapter = NULL;
    PDXGKRNL_DEVICE Device;
    BOOLEAN Owns = FALSE;

    if (hDevice == 0 || VidPnSourceId >= DXGKP_MAX_SOURCES)
        return FALSE;

    Device = DxgkLookupDeviceByHandle(hDevice, &Adapter);
    if (Device == NULL)
        return FALSE;
    if (Adapter == NULL)
    {
        DxgkDereferenceDevice(Device);
        return FALSE;
    }

    DxgkpEnsureSourceOwnerMutex();
    ExAcquireFastMutex(&g_SourceOwnerMutex);
    if (InterlockedCompareExchange(&Device->Destroying, 0, 0) != 0 || InterlockedCompareExchange(&Device->ExecutionState, 0, 0) != D3DKMT_DEVICEEXECUTION_ACTIVE)
    {
        ExReleaseFastMutex(&g_SourceOwnerMutex);
        DxgkDereferenceDevice(Device);
        return FALSE;
    }
    State = DxgkpFindSourceOwnerAdapterLocked(Adapter);
    if (State != NULL && State->Owners[VidPnSourceId].OwnerDevice == Device)
        Owns = (BOOLEAN)(State->Owners[VidPnSourceId].OwnerType == D3DKMT_VIDPNSOURCEOWNER_SHARED || State->Owners[VidPnSourceId].OwnerType == D3DKMT_VIDPNSOURCEOWNER_EXCLUSIVE || State->Owners[VidPnSourceId].OwnerType == D3DKMT_VIDPNSOURCEOWNER_EXCLUSIVEGDI);
    ExReleaseFastMutex(&g_SourceOwnerMutex);
    DxgkDereferenceDevice(Device);

    return Owns;
}

VOID
DxgkVidPnCleanupDeviceOwners(
    _In_ PDXGKRNL_DEVICE Device)
{
    PDXGKP_SOURCE_OWNER_ADAPTER_STATE State;
    PDXGKP_SOURCE_OWNER_ADAPTER_STATE StateToFree = NULL;
    ULONG Index;

    if (Device == NULL || Device->Adapter == NULL || Device->Handle == 0)
        return;
    DxgkpEnsureSourceOwnerMutex();
    KeEnterCriticalRegion();
    ExAcquireResourceExclusiveLite(&g_SourceProgrammingResource, TRUE);
    ExAcquireFastMutex(&g_SourceOwnerMutex);
    {
        PLIST_ENTRY Entry;
        for (Entry = g_CompositorSourceOwnerList.Flink;
             Entry != &g_CompositorSourceOwnerList; Entry = Entry->Flink)
        {
            PDXGKP_COMPOSITOR_SOURCE_OWNER Owner =
                CONTAINING_RECORD(Entry, DXGKP_COMPOSITOR_SOURCE_OWNER, Entry);
            if (Owner->Device == Device)
                Owner->Device = NULL;
        }
    }
    State = DxgkpFindSourceOwnerAdapterLocked(Device->Adapter);
    if (State != NULL)
    {
        for (Index = 0; Index < DXGKP_MAX_SOURCES; ++Index)
        {
            if (State->Owners[Index].OwnerDevice == Device)
            {
                State->Owners[Index].OwnerDevice = NULL;
                State->Owners[Index].OwnerType = D3DKMT_VIDPNSOURCEOWNER_UNOWNED;
                State->Owners[Index].OwnerFlags = 0;
            }
        }
        if (DxgkpSourceOwnerStateIsEmpty(State->Owners))
        {
            RemoveEntryList(&State->Entry);
            StateToFree = State;
        }
    }
    ExReleaseFastMutex(&g_SourceOwnerMutex);
    ExReleaseResourceLite(&g_SourceProgrammingResource);
    KeLeaveCriticalRegion();
    if (StateToFree != NULL)
    {
        RtlZeroMemory(StateToFree->Owners, sizeof(StateToFree->Owners));
        ExFreePoolWithTag(StateToFree, TAG_DXGK_SOURCE_OWNER);
    }
}

NTSTATUS
DxgkVidPnReleaseProcessOwners(
    _In_ PEPROCESS Process)
{
    LIST_ENTRY FreeList;
    LIST_ENTRY CompositorFreeList;
    PLIST_ENTRY Entry;

    PAGED_CODE();
    if (Process == NULL)
        return STATUS_INVALID_PARAMETER;
    InitializeListHead(&FreeList);
    InitializeListHead(&CompositorFreeList);
    DxgkpEnsureSourceOwnerMutex();
    KeEnterCriticalRegion();
    ExAcquireResourceExclusiveLite(&g_SourceProgrammingResource, TRUE);
    ExAcquireFastMutex(&g_SourceOwnerMutex);
    Entry = g_CompositorSourceOwnerList.Flink;
    while (Entry != &g_CompositorSourceOwnerList)
    {
        PDXGKP_COMPOSITOR_SOURCE_OWNER Owner =
            CONTAINING_RECORD(Entry, DXGKP_COMPOSITOR_SOURCE_OWNER, Entry);
        PLIST_ENTRY Next = Entry->Flink;
        if (Owner->Process == Process)
        {
            RemoveEntryList(Entry);
            InsertTailList(&CompositorFreeList, Entry);
        }
        Entry = Next;
    }
    Entry = g_SourceOwnerAdapterList.Flink;
    while (Entry != &g_SourceOwnerAdapterList)
    {
        PDXGKP_SOURCE_OWNER_ADAPTER_STATE State = CONTAINING_RECORD(Entry, DXGKP_SOURCE_OWNER_ADAPTER_STATE, Entry);
        PLIST_ENTRY Next = Entry->Flink;
        ULONG Index;

        for (Index = 0; Index < DXGKP_MAX_SOURCES; ++Index)
        {
            if (State->Owners[Index].OwnerDevice != NULL && State->Owners[Index].OwnerDevice->OwnerProcess == Process)
            {
                State->Owners[Index].OwnerDevice = NULL;
                State->Owners[Index].OwnerType = D3DKMT_VIDPNSOURCEOWNER_UNOWNED;
                State->Owners[Index].OwnerFlags = 0;
            }
        }
        if (DxgkpSourceOwnerStateIsEmpty(State->Owners))
        {
            RemoveEntryList(&State->Entry);
            InsertTailList(&FreeList, &State->Entry);
        }
        Entry = Next;
    }
    ExReleaseFastMutex(&g_SourceOwnerMutex);
    ExReleaseResourceLite(&g_SourceProgrammingResource);
    KeLeaveCriticalRegion();
    while (!IsListEmpty(&CompositorFreeList))
    {
        PDXGKP_COMPOSITOR_SOURCE_OWNER Owner = CONTAINING_RECORD(
            RemoveHeadList(&CompositorFreeList), DXGKP_COMPOSITOR_SOURCE_OWNER, Entry);
        ObDereferenceObject(Owner->Process);
        ExFreePoolWithTag(Owner, TAG_DXGK_SOURCE_OWNER);
    }
    while (!IsListEmpty(&FreeList))
    {
        PDXGKP_SOURCE_OWNER_ADAPTER_STATE State = CONTAINING_RECORD(RemoveHeadList(&FreeList), DXGKP_SOURCE_OWNER_ADAPTER_STATE, Entry);

        RtlZeroMemory(State->Owners, sizeof(State->Owners));
        ExFreePoolWithTag(State, TAG_DXGK_SOURCE_OWNER);
    }
    return STATUS_SUCCESS;
}

#if (REACTOS_WDDM_TARGET_LEVEL >= 2000)
NTSTATUS
DxgkVidPnQueryExclusiveOwnership(
    _In_ HANDLE ProcessId,
    _In_ CONST LUID *QueryAdapterLuid,
    _In_ D3DDDI_VIDEO_PRESENT_SOURCE_ID QueryVidPnSourceId,
    _Out_ D3DDDI_VIDEO_PRESENT_SOURCE_ID *ResultVidPnSourceId,
    _Out_ LUID *ResultAdapterLuid,
    _Out_ D3DKMT_VIDPNSOURCEOWNER_TYPE *OwnerType)
{
    PLIST_ENTRY Entry;
    PDXGKP_VIDPN_SOURCE_OWNER Owner;
    PEPROCESS Process = NULL;
    NTSTATUS Status;

    PAGED_CODE();

    if (QueryAdapterLuid == NULL ||
        ResultVidPnSourceId == NULL ||
        ResultAdapterLuid == NULL ||
        OwnerType == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }

    *ResultVidPnSourceId = D3DDDI_ID_UNINITIALIZED;
    RtlZeroMemory(ResultAdapterLuid, sizeof(*ResultAdapterLuid));
    *OwnerType = D3DKMT_VIDPNSOURCEOWNER_UNOWNED;

    if (ProcessId == NULL)
        return STATUS_INVALID_HANDLE;

    Status = PsLookupProcessByProcessId(ProcessId, &Process);
    if (!NT_SUCCESS(Status))
        return Status;

    /*
     * QueryVidPnExclusiveOwnership does not turn a yieldable SHARED owner into
     * an exclusive one.  Native reports only EXCLUSIVE, EXCLUSIVEGDI, and
     * EMULATED here; no match is a successful query with the defaults above.
     *
     * Device teardown removes an owner while holding this same mutex before
     * the final device reference is released, so OwnerDevice and its adapter
     * remain stable for the duration of the lookup.
     */
    DxgkpEnsureSourceOwnerMutex();
    ExAcquireFastMutex(&g_SourceOwnerMutex);
    for (Entry = g_SourceOwnerAdapterList.Flink;
         Entry != &g_SourceOwnerAdapterList;
         Entry = Entry->Flink)
    {
        PDXGKP_SOURCE_OWNER_ADAPTER_STATE State =
            CONTAINING_RECORD(
                Entry,
                DXGKP_SOURCE_OWNER_ADAPTER_STATE,
                Entry);

        if (State->Adapter != NULL &&
            State->Adapter->AdapterLuid.LowPart ==
                QueryAdapterLuid->LowPart &&
            State->Adapter->AdapterLuid.HighPart ==
                QueryAdapterLuid->HighPart &&
            QueryVidPnSourceId < DXGKP_MAX_SOURCES)
        {
            Owner = &State->Owners[QueryVidPnSourceId];
            if (Owner->OwnerDevice != NULL &&
                Owner->OwnerDevice->OwnerProcess == Process &&
                InterlockedCompareExchange(
                    &Owner->OwnerDevice->Destroying, 0, 0) == 0 &&
                (Owner->OwnerType ==
                     D3DKMT_VIDPNSOURCEOWNER_EXCLUSIVE ||
                 Owner->OwnerType ==
                     D3DKMT_VIDPNSOURCEOWNER_EXCLUSIVEGDI ||
                 Owner->OwnerType ==
                     D3DKMT_VIDPNSOURCEOWNER_EMULATED))
            {
                *ResultVidPnSourceId = QueryVidPnSourceId;
                *ResultAdapterLuid = State->Adapter->AdapterLuid;
                *OwnerType = Owner->OwnerType;
            }
            break;
        }
    }
    ExReleaseFastMutex(&g_SourceOwnerMutex);
    ObDereferenceObject(Process);
    return STATUS_SUCCESS;
}
#endif

/* ========================================================================
 * Handle validation helpers
 * ====================================================================== */

static PDXGKP_VIDPN
DxgkpVidPnFromHandle(
    _In_ D3DKMDT_HVIDPN hVidPn)
{
    PDXGKP_VIDPN VidPn = (PDXGKP_VIDPN)hVidPn;

    if (VidPn == NULL)
        return NULL;

    if (VidPn->Signature != DXGKP_VIDPN_SIGNATURE)
    {
        DXGKRNL_ERR("DxgkpVidPnFromHandle: bad signature 0x%08lX (expected 0x%08lX) handle=%p\n",
                     VidPn->Signature, DXGKP_VIDPN_SIGNATURE, hVidPn);
        return NULL;
    }

    return VidPn;
}

BOOLEAN
DxgkVidPnReference(
    _In_ D3DKMDT_HVIDPN hVidPn)
{
    PDXGKP_VIDPN VidPn = (PDXGKP_VIDPN)hVidPn;
    LONG References;

    if (VidPn == NULL || VidPn->Signature != DXGKP_VIDPN_SIGNATURE)
        return FALSE;
    for (;;)
    {
        References = InterlockedCompareExchange(&VidPn->RefCount, 0, 0);
        if (References <= 0 || References == MAXLONG)
            return FALSE;
        if (InterlockedCompareExchange(&VidPn->RefCount, References + 1, References) == References)
            return TRUE;
    }
}

static PDXGKP_VIDPN
DxgkpTopologyFromHandle(
    _In_ D3DKMDT_HVIDPNTOPOLOGY hTopology)
{
    return DxgkpVidPnFromHandle((D3DKMDT_HVIDPN)hTopology);
}

static PDXGKP_VIDPN_SOURCE_MODESET
DxgkpSourceModeSetFromHandle(
    _In_ D3DKMDT_HVIDPNSOURCEMODESET hModeSet)
{
    return (PDXGKP_VIDPN_SOURCE_MODESET)hModeSet;
}

static PDXGKP_VIDPN_TARGET_MODESET
DxgkpTargetModeSetFromHandle(
    _In_ D3DKMDT_HVIDPNTARGETMODESET hModeSet)
{
    return (PDXGKP_VIDPN_TARGET_MODESET)hModeSet;
}

static PDXGKP_MONITOR_SOURCE_MODESET
DxgkpMonitorModeSetFromHandle(
    _In_ D3DKMDT_HMONITORSOURCEMODESET hModeSet)
{
    return (PDXGKP_MONITOR_SOURCE_MODESET)hModeSet;
}

/* ========================================================================
 * Default mode table
 * ====================================================================== */

typedef struct _DXGKP_DEFAULT_MODE
{
    UINT Width;
    UINT Height;
} DXGKP_DEFAULT_MODE;

static CONST DXGKP_DEFAULT_MODE g_DefaultModes[] =
{
    {  800,  600 },
    { 1024,  768 },
    { 1152,  864 },
    { 1280,  720 },
    { 1280,  800 },
    { 1280, 1024 },
    { 1366,  768 },
    { 1440,  900 },
    { 1600,  900 },
    { 1600, 1200 },
    { 1680, 1050 },
    { 1920, 1080 },
    { 1920, 1200 },
};

#define DXGKP_NUM_DEFAULT_MODES \
    (sizeof(g_DefaultModes) / sizeof(g_DefaultModes[0]))

/* ========================================================================
 * Mode normalization and comparison helpers
 * ====================================================================== */

static VOID
DxgkpNormalizeSourceMode(
    _Inout_ D3DKMDT_VIDPN_SOURCE_MODE *Mode)
{
    if ((Mode->Format.Graphics.VisibleRegionSize.cx == 0 ||
         Mode->Format.Graphics.VisibleRegionSize.cy == 0) &&
        Mode->Format.Graphics.PrimSurfSize.cx != 0 &&
        Mode->Format.Graphics.PrimSurfSize.cy != 0)
    {
        Mode->Format.Graphics.VisibleRegionSize = Mode->Format.Graphics.PrimSurfSize;
    }
}

static BOOLEAN
DxgkpAreEquivalentSourceModes(
    _In_ CONST D3DKMDT_VIDPN_SOURCE_MODE *Left,
    _In_ CONST D3DKMDT_VIDPN_SOURCE_MODE *Right)
{
    return (Left->Type == Right->Type &&
            Left->Format.Graphics.PrimSurfSize.cx == Right->Format.Graphics.PrimSurfSize.cx &&
            Left->Format.Graphics.PrimSurfSize.cy == Right->Format.Graphics.PrimSurfSize.cy &&
            Left->Format.Graphics.VisibleRegionSize.cx == Right->Format.Graphics.VisibleRegionSize.cx &&
            Left->Format.Graphics.VisibleRegionSize.cy == Right->Format.Graphics.VisibleRegionSize.cy &&
            Left->Format.Graphics.Stride == Right->Format.Graphics.Stride &&
            Left->Format.Graphics.PixelFormat == Right->Format.Graphics.PixelFormat &&
            Left->Format.Graphics.ColorBasis == Right->Format.Graphics.ColorBasis &&
            Left->Format.Graphics.PixelValueAccessMode == Right->Format.Graphics.PixelValueAccessMode);
}

static VOID
DxgkpNormalizeTargetMode(
    _Inout_ D3DKMDT_VIDPN_TARGET_MODE *Mode)
{
    if ((Mode->VideoSignalInfo.ActiveSize.cx == 0 ||
         Mode->VideoSignalInfo.ActiveSize.cy == 0) &&
        Mode->VideoSignalInfo.TotalSize.cx != 0 &&
        Mode->VideoSignalInfo.TotalSize.cy != 0)
    {
        Mode->VideoSignalInfo.ActiveSize = Mode->VideoSignalInfo.TotalSize;
    }

    if ((Mode->VideoSignalInfo.TotalSize.cx == 0 ||
         Mode->VideoSignalInfo.TotalSize.cy == 0) &&
        Mode->VideoSignalInfo.ActiveSize.cx != 0 &&
        Mode->VideoSignalInfo.ActiveSize.cy != 0)
    {
        Mode->VideoSignalInfo.TotalSize = Mode->VideoSignalInfo.ActiveSize;
    }
}

static BOOLEAN
DxgkpAreEquivalentTargetModes(
    _In_ CONST D3DKMDT_VIDPN_TARGET_MODE *Left,
    _In_ CONST D3DKMDT_VIDPN_TARGET_MODE *Right)
{
    return (Left->VideoSignalInfo.VideoStandard == Right->VideoSignalInfo.VideoStandard &&
            Left->VideoSignalInfo.TotalSize.cx == Right->VideoSignalInfo.TotalSize.cx &&
            Left->VideoSignalInfo.TotalSize.cy == Right->VideoSignalInfo.TotalSize.cy &&
            Left->VideoSignalInfo.ActiveSize.cx == Right->VideoSignalInfo.ActiveSize.cx &&
            Left->VideoSignalInfo.ActiveSize.cy == Right->VideoSignalInfo.ActiveSize.cy &&
            Left->VideoSignalInfo.VSyncFreq.Numerator == Right->VideoSignalInfo.VSyncFreq.Numerator &&
            Left->VideoSignalInfo.VSyncFreq.Denominator == Right->VideoSignalInfo.VSyncFreq.Denominator &&
            Left->VideoSignalInfo.HSyncFreq.Numerator == Right->VideoSignalInfo.HSyncFreq.Numerator &&
            Left->VideoSignalInfo.HSyncFreq.Denominator == Right->VideoSignalInfo.HSyncFreq.Denominator &&
            Left->VideoSignalInfo.PixelRate == Right->VideoSignalInfo.PixelRate &&
            Left->VideoSignalInfo.ScanLineOrdering == Right->VideoSignalInfo.ScanLineOrdering);
}

static BOOLEAN
DxgkpAreSameTargetModeValues(
    _In_ CONST D3DKMDT_VIDPN_TARGET_MODE *Left,
    _In_ CONST D3DKMDT_VIDPN_TARGET_MODE *Right)
{
    return (Left->VideoSignalInfo.TotalSize.cx == Right->VideoSignalInfo.TotalSize.cx &&
            Left->VideoSignalInfo.TotalSize.cy == Right->VideoSignalInfo.TotalSize.cy &&
            Left->VideoSignalInfo.ActiveSize.cx == Right->VideoSignalInfo.ActiveSize.cx &&
            Left->VideoSignalInfo.ActiveSize.cy == Right->VideoSignalInfo.ActiveSize.cy &&
            Left->VideoSignalInfo.VSyncFreq.Numerator == Right->VideoSignalInfo.VSyncFreq.Numerator &&
            Left->VideoSignalInfo.VSyncFreq.Denominator == Right->VideoSignalInfo.VSyncFreq.Denominator &&
            Left->VideoSignalInfo.HSyncFreq.Numerator == Right->VideoSignalInfo.HSyncFreq.Numerator &&
            Left->VideoSignalInfo.HSyncFreq.Denominator == Right->VideoSignalInfo.HSyncFreq.Denominator &&
            Left->VideoSignalInfo.PixelRate == Right->VideoSignalInfo.PixelRate &&
            Left->VideoSignalInfo.AdditionalSignalInfo.ScanLineOrdering == Right->VideoSignalInfo.AdditionalSignalInfo.ScanLineOrdering &&
            Left->VideoSignalInfo.AdditionalSignalInfo.VSyncFreqDivider == Right->VideoSignalInfo.AdditionalSignalInfo.VSyncFreqDivider &&
            Left->WireFormatAndPreference.Value == Right->WireFormatAndPreference.Value);
}

static VOID
DxgkpNormalizeMonitorMode(
    _Inout_ D3DKMDT_MONITOR_SOURCE_MODE *Mode)
{
    if ((Mode->VideoSignalInfo.ActiveSize.cx == 0 ||
         Mode->VideoSignalInfo.ActiveSize.cy == 0) &&
        Mode->VideoSignalInfo.TotalSize.cx != 0 &&
        Mode->VideoSignalInfo.TotalSize.cy != 0)
    {
        Mode->VideoSignalInfo.ActiveSize = Mode->VideoSignalInfo.TotalSize;
    }

    if ((Mode->VideoSignalInfo.TotalSize.cx == 0 ||
         Mode->VideoSignalInfo.TotalSize.cy == 0) &&
        Mode->VideoSignalInfo.ActiveSize.cx != 0 &&
        Mode->VideoSignalInfo.ActiveSize.cy != 0)
    {
        Mode->VideoSignalInfo.TotalSize = Mode->VideoSignalInfo.ActiveSize;
    }
}

static BOOLEAN
DxgkpAreEquivalentMonitorModes(
    _In_ CONST D3DKMDT_MONITOR_SOURCE_MODE *Left,
    _In_ CONST D3DKMDT_MONITOR_SOURCE_MODE *Right)
{
    return (Left->VideoSignalInfo.VideoStandard == Right->VideoSignalInfo.VideoStandard &&
            Left->VideoSignalInfo.TotalSize.cx == Right->VideoSignalInfo.TotalSize.cx &&
            Left->VideoSignalInfo.TotalSize.cy == Right->VideoSignalInfo.TotalSize.cy &&
            Left->VideoSignalInfo.ActiveSize.cx == Right->VideoSignalInfo.ActiveSize.cx &&
            Left->VideoSignalInfo.ActiveSize.cy == Right->VideoSignalInfo.ActiveSize.cy &&
            Left->VideoSignalInfo.VSyncFreq.Numerator == Right->VideoSignalInfo.VSyncFreq.Numerator &&
            Left->VideoSignalInfo.VSyncFreq.Denominator == Right->VideoSignalInfo.VSyncFreq.Denominator &&
            Left->VideoSignalInfo.HSyncFreq.Numerator == Right->VideoSignalInfo.HSyncFreq.Numerator &&
            Left->VideoSignalInfo.HSyncFreq.Denominator == Right->VideoSignalInfo.HSyncFreq.Denominator &&
            Left->VideoSignalInfo.PixelRate == Right->VideoSignalInfo.PixelRate &&
            Left->VideoSignalInfo.ScanLineOrdering == Right->VideoSignalInfo.ScanLineOrdering &&
            Left->ColorBasis == Right->ColorBasis &&
            Left->ColorCoeffDynamicRanges.FirstChannel == Right->ColorCoeffDynamicRanges.FirstChannel &&
            Left->ColorCoeffDynamicRanges.SecondChannel == Right->ColorCoeffDynamicRanges.SecondChannel &&
            Left->ColorCoeffDynamicRanges.ThirdChannel == Right->ColorCoeffDynamicRanges.ThirdChannel &&
            Left->ColorCoeffDynamicRanges.FourthChannel == Right->ColorCoeffDynamicRanges.FourthChannel &&
            Left->Origin == Right->Origin);
}

/* ========================================================================
 * Default mode population helpers
 * ====================================================================== */

static VOID
DxgkpPopulateDefaultSourceMode(
    _Out_ D3DKMDT_VIDPN_SOURCE_MODE *Mode,
    _In_  UINT                       Id,
    _In_  UINT                       Width,
    _In_  UINT                       Height)
{
    RtlZeroMemory(Mode, sizeof(*Mode));
    Mode->Id   = Id;
    Mode->Type = D3DKMDT_RMT_GRAPHICS;

    Mode->Format.Graphics.PrimSurfSize.cx      = Width;
    Mode->Format.Graphics.PrimSurfSize.cy      = Height;
    Mode->Format.Graphics.VisibleRegionSize.cx = Width;
    Mode->Format.Graphics.VisibleRegionSize.cy = Height;
    Mode->Format.Graphics.Stride               = Width * 4;
    Mode->Format.Graphics.PixelFormat          = D3DDDIFMT_A8R8G8B8;
    /* WDDM source modes use scRGB independently of their pixel format. */
    Mode->Format.Graphics.ColorBasis           = D3DKMDT_CB_SCRGB;
    Mode->Format.Graphics.PixelValueAccessMode = D3DKMDT_PVAM_DIRECT;
}

/*
 * Fill a video signal with VESA CVT reduced-blanking timings.
 *
 * These modes used to be written with TotalSize == ActiveSize and a pixel
 * rate of Width*Height*Refresh -- a raster with no blanking at all, which no
 * display pipeline can generate.  A full miniport validates the signal it is
 * asked to drive, and this Intel part refuses the whole VidPN over it, so
 * nothing was ever committed and the boot ended in VIDEO_DRIVER_INIT_FAILURE.
 * The synthetic list has to describe real rasters even when it is only a
 * starting point for DxgkDdiEnumVidPnCofuncModality to prune.
 *
 * CVT-RB v1: a fixed 160-pixel horizontal blanking, and just enough vertical
 * blanking lines to cover the 460 us the standard requires.  For 1920x1080 at
 * 60 Hz that gives 2080x1111 and 138.65 MHz, which is the published CVT-RB
 * timing for that mode.
 */
#define DXGKP_CVT_RB_H_BLANK        160u
#define DXGKP_CVT_RB_MIN_V_BLANK_US 460u

static VOID
DxgkpFillCvtReducedBlankingSignal(
    _Out_ D3DKMDT_VIDEO_SIGNAL_INFO *Signal,
    _In_  UINT                       Width,
    _In_  UINT                       Height,
    _In_  UINT                       RefreshHz)
{
    ULONGLONG TotalWidth;
    ULONGLONG TotalHeight;
    ULONGLONG BlankLines;
    ULONGLONG Denominator;
    ULONGLONG PixelRate;

    RtlZeroMemory(Signal, sizeof(*Signal));
    if (Width == 0 || Height == 0 || RefreshHz == 0)
        return;

    TotalWidth = (ULONGLONG)Width + DXGKP_CVT_RB_H_BLANK;

    /*
     * Lines of vertical blanking, rounded up:
     *     ceil(MinVBlankUs * Height * Refresh / (1000000 - MinVBlankUs * Refresh))
     * which is the CVT estimate  ceil(MinVBlank / HPeriodEst)  with the line
     * period left as a ratio so no rounding creeps in before the division.
     */
    Denominator = 1000000ULL - (ULONGLONG)DXGKP_CVT_RB_MIN_V_BLANK_US * RefreshHz;
    if ((LONGLONG)Denominator <= 0)
    {
        /* Refresh so high the blanking interval would not fit; fall back to
         * the CVT floor of front porch + sync + back porch. */
        BlankLines = 10;
    }
    else
    {
        ULONGLONG Numerator =
            (ULONGLONG)DXGKP_CVT_RB_MIN_V_BLANK_US * Height * RefreshHz;

        BlankLines = (Numerator + Denominator - 1) / Denominator;
        if (BlankLines < 10)
            BlankLines = 10;
    }
    TotalHeight = (ULONGLONG)Height + BlankLines;
    PixelRate = TotalWidth * TotalHeight * RefreshHz;

    Signal->VideoStandard      = D3DKMDT_VSS_OTHER;
    Signal->TotalSize.cx       = (LONG)TotalWidth;
    Signal->TotalSize.cy       = (LONG)TotalHeight;
    Signal->ActiveSize.cx      = (LONG)Width;
    Signal->ActiveSize.cy      = (LONG)Height;
    Signal->VSyncFreq.Numerator   = (UINT)RefreshHz;
    Signal->VSyncFreq.Denominator = 1;
    /* Exact rather than rounded: the line rate is the pixel rate over one
     * whole line, and both fit a rational. */
    Signal->HSyncFreq.Numerator   = (UINT)PixelRate;
    Signal->HSyncFreq.Denominator = (UINT)TotalWidth;
    Signal->PixelRate          = (SIZE_T)PixelRate;
    Signal->ScanLineOrdering   = D3DDDI_VSSLO_PROGRESSIVE;
}

static VOID
DxgkpPopulateDefaultTargetMode(
    _Out_ D3DKMDT_VIDPN_TARGET_MODE *Mode,
    _In_  UINT                       Id,
    _In_  UINT                       Width,
    _In_  UINT                       Height)
{
    RtlZeroMemory(Mode, sizeof(*Mode));
    Mode->Id = Id;
    DxgkpFillCvtReducedBlankingSignal(&Mode->VideoSignalInfo, Width, Height, 60);
    Mode->Preference = D3DKMDT_MP_NOTPREFERRED;
}

static VOID
DxgkpPopulateDefaultMonitorMode(
    _Out_ D3DKMDT_MONITOR_SOURCE_MODE *Mode,
    _In_  UINT                         Id,
    _In_  UINT                         Width,
    _In_  UINT                         Height)
{
    RtlZeroMemory(Mode, sizeof(*Mode));
    Mode->Id = Id;
    /* Same raster the target mode describes -- a monitor mode that claimed no
     * blanking would contradict the target it is meant to be driven by. */
    DxgkpFillCvtReducedBlankingSignal(&Mode->VideoSignalInfo, Width, Height, 60);

    Mode->ColorBasis = D3DKMDT_CB_SRGB;
    Mode->ColorCoeffDynamicRanges.FirstChannel  = 8;
    Mode->ColorCoeffDynamicRanges.SecondChannel = 8;
    Mode->ColorCoeffDynamicRanges.ThirdChannel  = 8;
    Mode->ColorCoeffDynamicRanges.FourthChannel = 0;
    Mode->Origin     = D3DKMDT_MCO_DRIVER;
    Mode->Preference = D3DKMDT_MP_PREFERRED;
}

static VOID
DxgkpPopulateDefaultPath(
    _Out_ D3DKMDT_VIDPN_PRESENT_PATH *Path,
    _In_  D3DDDI_VIDEO_PRESENT_SOURCE_ID SourceId,
    _In_  D3DDDI_VIDEO_PRESENT_TARGET_ID TargetId)
{
    RtlZeroMemory(Path, sizeof(*Path));
    Path->VidPnSourceId   = SourceId;
    Path->VidPnTargetId   = TargetId;
    Path->ImportanceOrdinal = D3DKMDT_VPPI_PRIMARY;

    Path->ContentTransformation.Scaling  = D3DKMDT_VPPS_IDENTITY;
    Path->ContentTransformation.ScalingSupport.Identity = 1;
    Path->ContentTransformation.ScalingSupport.Centered = 1;
    Path->ContentTransformation.ScalingSupport.Stretched = 1;

    Path->ContentTransformation.Rotation = D3DKMDT_VPPR_IDENTITY;
    Path->ContentTransformation.RotationSupport.Identity = 1;
    Path->ContentTransformation.RotationSupport.Rotate90 = 0;
    Path->ContentTransformation.RotationSupport.Rotate180 = 0;
    Path->ContentTransformation.RotationSupport.Rotate270 = 0;

    Path->GammaRamp.Type = D3DDDI_GAMMARAMP_DEFAULT;
    Path->GammaRamp.DataSize = 0;

    Path->VidPnTargetColorBasis = D3DKMDT_CB_SCRGB;
    Path->VidPnTargetColorCoeffDynamicRanges.FirstChannel  = 8;
    Path->VidPnTargetColorCoeffDynamicRanges.SecondChannel = 8;
    Path->VidPnTargetColorCoeffDynamicRanges.ThirdChannel  = 8;
    Path->VidPnTargetColorCoeffDynamicRanges.FourthChannel = 0;

    Path->Content = D3DKMDT_VPPC_GRAPHICS;

    /*
     * "No copy protection" is a value of its own, not the zero the rest of the
     * struct was cleared to.  D3DKMDT_VPPMT_UNINITIALIZED is what RtlZeroMemory
     * leaves behind, and it is the one field on this path that still said
     * nothing -- everything else names a real enumerator.  Declare the support
     * bit too, so what the path asks for is also what it advertises.
     */
    Path->CopyProtection.CopyProtectionType = D3DKMDT_VPPMT_NOPROTECTION;
    Path->CopyProtection.APSTriggerBits = 0;
    Path->CopyProtection.CopyProtectionSupport.NoProtection = 1;
}

/* ========================================================================
 * Mode set allocation/deallocation helpers
 * ====================================================================== */

static PDXGKP_VIDPN_SOURCE_MODESET
DxgkpAllocateSourceModeSet(
    _In_ PDXGKP_VIDPN VidPn,
    _In_ D3DDDI_VIDEO_PRESENT_SOURCE_ID SourceId)
{
    PDXGKP_VIDPN_SOURCE_MODESET ModeSet;

    ModeSet = (PDXGKP_VIDPN_SOURCE_MODESET)ExAllocatePoolWithTag(
        NonPagedPool, sizeof(DXGKP_VIDPN_SOURCE_MODESET), TAG_DXGK_MODESET);
    if (ModeSet == NULL)
        return NULL;

    RtlZeroMemory(ModeSet, sizeof(*ModeSet));
    ModeSet->Owner = VidPn;
    ModeSet->SourceId = SourceId;
    ModeSet->PinnedModeId = (UINT)-1;
    ModeSet->NextModeId = 0;

    return ModeSet;
}

/*
 * A VidPN target id is the miniport's child uid, which is whatever the driver
 * chose -- this Intel part reports 49 for its connected output while declaring
 * ten children.  The per-target arrays are positional, so an id is not an
 * index and must be looked up.  Treating the two as the same number rejected
 * every adapter whose uids are not 0..NumTargets-1.
 */
ULONG
DxgkVidPnTargetIndexFromId(
    _In_ PDXGKP_VIDPN VidPn,
    _In_ D3DDDI_VIDEO_PRESENT_TARGET_ID TargetId)
{
    ULONG Index;

    if (VidPn == NULL)
        return MAXULONG;
    for (Index = 0; Index < VidPn->NumTargets && Index < DXGKP_MAX_TARGETS; Index++)
    {
        if (VidPn->TargetModeSets[Index] != NULL &&
            VidPn->TargetModeSets[Index]->TargetId == TargetId)
        {
            return Index;
        }
    }
    return MAXULONG;
}


VOID
DxgkVidPnPublishVsyncTargetMap(
    _Inout_ PDXGKRNL_ADAPTER Adapter,
    _In_opt_ D3DKMDT_HVIDPN hVidPn)
{
    PDXGKP_VIDPN VidPn = (PDXGKP_VIDPN)hVidPn;
    ULONG Count = 0;
    SIZE_T Index;

    for (Index = 0;
         VidPn != NULL && Index < VidPn->NumPaths &&
         Count < RTL_NUMBER_OF(Adapter->VsyncTargetSourceMap);
         Index++)
    {
        D3DDDI_VIDEO_PRESENT_SOURCE_ID SourceId = VidPn->Paths[Index].VidPnSourceId;

        if (SourceId >= RTL_NUMBER_OF(Adapter->VsyncScanoutSequence))
            continue;
        InterlockedExchange64(&Adapter->VsyncTargetSourceMap[Count++],
                              (LONG64)(((ULONG64)VidPn->Paths[Index].VidPnTargetId << 32) |
                                       (ULONG64)(SourceId + 1)));
    }
    while (Count < RTL_NUMBER_OF(Adapter->VsyncTargetSourceMap))
        InterlockedExchange64(&Adapter->VsyncTargetSourceMap[Count++], 0);
}

ULONG
DxgkVidPnVsyncSourceFromTarget(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _In_ D3DDDI_VIDEO_PRESENT_TARGET_ID TargetId)
{
    ULONG Index;
    LONG64 Entry;

    for (Index = 0; Index < RTL_NUMBER_OF(Adapter->VsyncTargetSourceMap); Index++)
    {
        Entry = InterlockedCompareExchange64(&Adapter->VsyncTargetSourceMap[Index], 0, 0);
        if (Entry == 0)
            break;
        if ((ULONG)((ULONG64)Entry >> 32) == TargetId)
            return (ULONG)(Entry & 0xFFFFFFFF) - 1;
    }
    if (TargetId < RTL_NUMBER_OF(Adapter->VsyncScanoutSequence))
        return TargetId;
    return MAXULONG;
}

static PDXGKP_VIDPN_TARGET_MODESET
DxgkpAllocateTargetModeSet(
    _In_ PDXGKP_VIDPN VidPn,
    _In_ D3DDDI_VIDEO_PRESENT_TARGET_ID TargetId)
{
    PDXGKP_VIDPN_TARGET_MODESET ModeSet;

    ModeSet = (PDXGKP_VIDPN_TARGET_MODESET)ExAllocatePoolWithTag(
        NonPagedPool, sizeof(DXGKP_VIDPN_TARGET_MODESET), TAG_DXGK_MODESET);
    if (ModeSet == NULL)
        return NULL;

    RtlZeroMemory(ModeSet, sizeof(*ModeSet));
    ModeSet->Owner = VidPn;
    ModeSet->TargetId = TargetId;
    ModeSet->PinnedModeId = (UINT)-1;
    ModeSet->NextModeId = 0;

    return ModeSet;
}

static PDXGKP_MONITOR_SOURCE_MODESET
DxgkpAllocateMonitorModeSet(
    _In_ PDXGKP_VIDPN VidPn,
    _In_ D3DDDI_VIDEO_PRESENT_TARGET_ID TargetId)
{
    PDXGKP_MONITOR_SOURCE_MODESET ModeSet;

    ModeSet = (PDXGKP_MONITOR_SOURCE_MODESET)ExAllocatePoolWithTag(
        NonPagedPool, sizeof(DXGKP_MONITOR_SOURCE_MODESET), TAG_DXGK_MODESET);
    if (ModeSet == NULL)
        return NULL;

    RtlZeroMemory(ModeSet, sizeof(*ModeSet));
    ModeSet->Owner = VidPn;
    ModeSet->TargetId = TargetId;
    ModeSet->NextModeId = 0;

    return ModeSet;
}

static VOID
DxgkpPopulateDefaultModes(
    _In_ PDXGKP_VIDPN_SOURCE_MODESET SrcSet,
    _In_ PDXGKP_VIDPN_TARGET_MODESET TgtSet,
    _In_ PDXGKP_MONITOR_SOURCE_MODESET MonSet)
{
    UINT i;
    UINT NumModes = (UINT)DXGKP_NUM_DEFAULT_MODES;

    if (NumModes > DXGKP_MAX_MODES)
        NumModes = DXGKP_MAX_MODES;

    if (SrcSet != NULL)
    {
        for (i = 0; i < NumModes; i++)
        {
            DxgkpPopulateDefaultSourceMode(
                &SrcSet->Modes[i], i,
                g_DefaultModes[i].Width,
                g_DefaultModes[i].Height);
        }
        SrcSet->NumModes = NumModes;
        SrcSet->NextModeId = NumModes;
    }

    if (TgtSet != NULL)
    {
        for (i = 0; i < NumModes; i++)
        {
            DxgkpPopulateDefaultTargetMode(
                &TgtSet->Modes[i], i,
                g_DefaultModes[i].Width,
                g_DefaultModes[i].Height);
        }
        TgtSet->NumModes = NumModes;
        TgtSet->NextModeId = NumModes;
    }

    if (MonSet != NULL)
    {
        for (i = 0; i < NumModes; i++)
        {
            DxgkpPopulateDefaultMonitorMode(
                &MonSet->Modes[i], i,
                g_DefaultModes[i].Width,
                g_DefaultModes[i].Height);
        }
        if (NumModes > 0)
            MonSet->Modes[NumModes - 1].Preference = D3DKMDT_MP_PREFERRED;
        MonSet->NumModes = NumModes;
        MonSet->NextModeId = NumModes;
    }
}

/*
 * Make the pivot statement true before it is made.
 *
 * D3DKMDT_EPT_VIDPNSOURCE tells the miniport that the source mode set named
 * by the pivot is *fixed* -- that a mode is pinned in it -- and asks it to
 * enumerate everything cofunctional with that mode.  A driver that honours
 * the contract therefore leaves the pivot's source mode set exactly as it
 * found it.
 *
 * Naming the pivot without pinning anything turns that into a trap: the
 * driver skips the one mode set it was going to replace with a mode it can
 * actually scan out, and dxgkrnl is left pinning a mode out of its own
 * g_DefaultModes table -- a table of VESA sizes that cannot describe a panel
 * running an off-list raster.  On a Raspberry Pi 3 whose GOP comes up at
 * 720x480 the closest entry is 800x600; the miniport published 720x480 as its
 * only target timing, was handed an 800x600 source to go with it, and refused
 * the VidPN.  Nothing was ever committed, so the adapter kept a zero-sized
 * mode and win32k bugchecked with VIDEO_DRIVER_INIT_FAILURE.
 *
 * An already-pinned mode is the answer when there is one: DxgkpAddEdidPreferredModes
 * pins the monitor's own detailed timing, and that outranks anything inferred
 * here.  Otherwise the POST size is the authoritative one -- it is the raster
 * the display is lit at right now, and the same size the miniport read back
 * from DxgkCbAcquirePostDisplayOwnership -- so add it to the set if the
 * synthetic defaults do not already carry it, and pin it.
 *
 * Returns FALSE when no source mode could be pinned, which means the caller
 * has no business claiming a source pivot.
 */
BOOLEAN
DxgkVidPnEnsurePinnedSourceMode(
    _In_ PDXGKP_VIDPN VidPn,
    _In_ D3DDDI_VIDEO_PRESENT_SOURCE_ID SourceId,
    _In_ UINT FallbackWidth,
    _In_ UINT FallbackHeight)
{
    PDXGKP_VIDPN_SOURCE_MODESET SrcSet;
    D3DKMDT_VIDPN_SOURCE_MODE Mode;
    SIZE_T Index;

    if (VidPn == NULL || VidPn->Signature != DXGKP_VIDPN_SIGNATURE)
        return FALSE;
    if (SourceId >= VidPn->NumSources || SourceId >= DXGKP_MAX_SOURCES)
        return FALSE;

    SrcSet = VidPn->SourceModeSets[SourceId];
    if (SrcSet == NULL)
        return FALSE;

    /* A pin that resolves to a mode the set still holds is already the truth. */
    if (SrcSet->PinnedModeId != (UINT)-1)
    {
        for (Index = 0; Index < SrcSet->NumModes; Index++)
        {
            if (SrcSet->Modes[Index].Id == SrcSet->PinnedModeId)
                return TRUE;
        }
        SrcSet->PinnedModeId = (UINT)-1;
    }

    if (FallbackWidth == 0 || FallbackHeight == 0)
        return FALSE;

    DxgkpPopulateDefaultSourceMode(&Mode, SrcSet->NextModeId,
                                   FallbackWidth, FallbackHeight);

    for (Index = 0; Index < SrcSet->NumModes; Index++)
    {
        if (!DxgkpAreEquivalentSourceModes(&SrcSet->Modes[Index], &Mode))
            continue;
        SrcSet->PinnedModeId = SrcSet->Modes[Index].Id;
        return TRUE;
    }

    if (SrcSet->NumModes >= DXGKP_MAX_MODES)
        return FALSE;

    Mode.Id = SrcSet->NextModeId++;
    SrcSet->Modes[SrcSet->NumModes++] = Mode;
    SrcSet->PinnedModeId = Mode.Id;
    return TRUE;
}

/* ========================================================================
 * Pending-object lifetime (see DXGKP_PENDING_OBJECT in vidpn.h)
 *
 * These follow the VidPN's existing access model: the interface functions
 * that reach them run at PASSIVE_LEVEL, serialised per VidPN by the caller.
 * ====================================================================== */

static PVOID
DxgkpPendingCreate(
    _In_ PDXGKP_VIDPN       VidPn,
    _In_ DXGKP_PENDING_KIND Kind)
{
    PDXGKP_PENDING_OBJECT Object;

    Object = (PDXGKP_PENDING_OBJECT)ExAllocatePoolWithTag(
        NonPagedPool, sizeof(*Object), TAG_DXGK_VIDPN);
    if (Object == NULL)
        return NULL;

    RtlZeroMemory(Object, sizeof(*Object));
    Object->Kind = Kind;
    InsertTailList(&VidPn->PendingObjects, &Object->Link);
    return &Object->u;
}

/*
 * Frees the pending object the miniport knows as Pointer, if it is one.  A
 * pointer that is not a pending object of this kind -- such as a mode the
 * miniport acquired from a set -- is left alone, and the caller succeeds as
 * before: only objects this VidPN created have a lifetime to end.
 */
static VOID
DxgkpPendingConsume(
    _In_ PDXGKP_VIDPN       VidPn,
    _In_ CONST VOID        *Pointer,
    _In_ DXGKP_PENDING_KIND Kind)
{
    PLIST_ENTRY Entry;

    for (Entry = VidPn->PendingObjects.Flink;
         Entry != &VidPn->PendingObjects;
         Entry = Entry->Flink)
    {
        PDXGKP_PENDING_OBJECT Object =
            CONTAINING_RECORD(Entry, DXGKP_PENDING_OBJECT, Link);

        if ((CONST VOID *)&Object->u == Pointer && Object->Kind == Kind)
        {
            RemoveEntryList(&Object->Link);
            ExFreePoolWithTag(Object, TAG_DXGK_VIDPN);
            return;
        }
    }
}

static VOID
DxgkpPendingDrain(
    _In_ PDXGKP_VIDPN VidPn)
{
    while (!IsListEmpty(&VidPn->PendingObjects))
    {
        PLIST_ENTRY Entry = RemoveHeadList(&VidPn->PendingObjects);

        ExFreePoolWithTag(
            CONTAINING_RECORD(Entry, DXGKP_PENDING_OBJECT, Link),
            TAG_DXGK_VIDPN);
    }
}

/* ========================================================================
 * VidPN object lifecycle
 * ====================================================================== */

NTSTATUS
DxgkVidPnCreateForAdapter(
    _In_  PDXGKRNL_ADAPTER  Adapter,
    _Out_ D3DKMDT_HVIDPN   *phVidPn)
{
    PDXGKP_VIDPN VidPn = NULL;
    ULONG NumSources, NumTargets;
    ULONG i, ModePairCount, PathCount;
    BOOLEAN SeedDefaultTopology;

    PAGED_CODE();

    if (phVidPn == NULL)
        return STATUS_INVALID_PARAMETER;

    *phVidPn = NULL;

    DxgkpEnsureSourceOwnerMutex();

    VidPn = (PDXGKP_VIDPN)ExAllocatePoolWithTag(
        NonPagedPool, sizeof(DXGKP_VIDPN), TAG_DXGK_VIDPN);
    if (VidPn == NULL)
    {
        DXGKRNL_ERR("DxgkVidPnCreateForAdapter: failed to allocate VidPN\n");
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    RtlZeroMemory(VidPn, sizeof(*VidPn));
    InitializeListHead(&VidPn->PendingObjects);
    VidPn->Signature = DXGKP_VIDPN_SIGNATURE;
    VidPn->RefCount  = 1;
    VidPn->Adapter   = Adapter;

    D3DDDI_VIDEO_PRESENT_TARGET_ID TargetUids[DXGKP_MAX_TARGETS];

    /* Determine source/target counts from adapter. */
    NumSources = (Adapter->NumberOfVideoPresentSources > 0)
                 ? Adapter->NumberOfVideoPresentSources : 1;
    NumTargets = (Adapter->NumberOfChildren > 0)
                 ? Adapter->NumberOfChildren : 1;

    if (NumSources > DXGKP_MAX_SOURCES) NumSources = DXGKP_MAX_SOURCES;
    if (NumTargets > DXGKP_MAX_TARGETS) NumTargets = DXGKP_MAX_TARGETS;

    VidPn->NumSources = NumSources;
    VidPn->NumTargets = NumTargets;

    /*
     * Record which child uid each positional target stands for.  The miniport
     * identifies its outputs by uid, so the topology must carry uids, while
     * the arrays here stay positional; DxgkVidPnTargetIndexFromId bridges the two.
     */
    {
        KIRQL ChildIrql;
        PLIST_ENTRY ChildEntry;
        ULONG Slot = 0;

        for (Slot = 0; Slot < NumTargets; Slot++)
            TargetUids[Slot] = Slot;
        Slot = 0;
        KeAcquireSpinLock(&Adapter->ChildListLock, &ChildIrql);
        for (ChildEntry = Adapter->ChildListHead.Flink;
             ChildEntry != &Adapter->ChildListHead && Slot < NumTargets;
             ChildEntry = ChildEntry->Flink)
        {
            PDXGK_CHILD_PDO_EXTENSION Child =
                CONTAINING_RECORD(ChildEntry, DXGK_CHILD_PDO_EXTENSION, ListEntry);

            if (!Child->Present ||
                Child->Descriptor.ChildDeviceType != TypeVideoOutput)
            {
                continue;
            }
            TargetUids[Slot++] = Child->Descriptor.ChildUid;
        }
        KeReleaseSpinLock(&Adapter->ChildListLock, ChildIrql);
    }

    /* Allocate mode sets for each source and target. */
    for (i = 0; i < NumSources; i++)
    {
        VidPn->SourceModeSets[i] = DxgkpAllocateSourceModeSet(VidPn, i);
        if (VidPn->SourceModeSets[i] == NULL)
            goto Fail;
    }

    for (i = 0; i < NumTargets; i++)
    {
        VidPn->TargetModeSets[i] = DxgkpAllocateTargetModeSet(VidPn, TargetUids[i]);
        if (VidPn->TargetModeSets[i] == NULL)
            goto Fail;

        VidPn->MonitorModeSets[i] = DxgkpAllocateMonitorModeSet(VidPn, TargetUids[i]);
        if (VidPn->MonitorModeSets[i] == NULL)
            goto Fail;
    }

    /*
     * A full WDDM miniport's source and child counts describe capacities, not
     * source-to-target wiring.  Start it with the contractually valid empty
     * topology; child-status/hot-plug discovery will add only paths that are
     * actually connected.  Display-only and BasicDisplay adapters need their
     * pre-seeded path during early boot because they provide the desktop
     * framebuffer before child discovery runs.
     */
    SeedDefaultTopology =
        Adapter->MiniportContext != NULL &&
        (Adapter->MiniportContext->IsDisplayOnlyDriver ||
         Adapter->MiniportContext->IsBasicDisplayFallback);
    ModePairCount = (NumSources < NumTargets) ? NumSources : NumTargets;
    if (ModePairCount > DXGKP_MAX_PATHS)
        ModePairCount = DXGKP_MAX_PATHS;
    PathCount = SeedDefaultTopology ? ModePairCount : 0;

    for (i = 0; i < ModePairCount; i++)
    {
        if (SeedDefaultTopology)
            DxgkpPopulateDefaultPath(&VidPn->Paths[i], i, i);
        DxgkpPopulateDefaultModes(
            VidPn->SourceModeSets[i],
            VidPn->TargetModeSets[i],
            VidPn->MonitorModeSets[i]);
    }
    VidPn->NumPaths = PathCount;

    *phVidPn = (D3DKMDT_HVIDPN)VidPn;

    DXGKRNL_TRACE("DxgkVidPnCreateForAdapter: created VidPN %p for adapter %p "
                  "(%lu sources, %lu targets, %lu paths)\n",
                  VidPn, Adapter, NumSources, NumTargets, PathCount);

    return STATUS_SUCCESS;

Fail:
    /* Clean up partially allocated mode sets. */
    for (i = 0; i < DXGKP_MAX_SOURCES; i++)
    {
        if (VidPn->SourceModeSets[i] != NULL)
        {
            ExFreePoolWithTag(VidPn->SourceModeSets[i], TAG_DXGK_MODESET);
            VidPn->SourceModeSets[i] = NULL;
        }
    }
    for (i = 0; i < DXGKP_MAX_TARGETS; i++)
    {
        if (VidPn->TargetModeSets[i] != NULL)
        {
            ExFreePoolWithTag(VidPn->TargetModeSets[i], TAG_DXGK_MODESET);
            VidPn->TargetModeSets[i] = NULL;
        }
        if (VidPn->MonitorModeSets[i] != NULL)
        {
            ExFreePoolWithTag(VidPn->MonitorModeSets[i], TAG_DXGK_MODESET);
            VidPn->MonitorModeSets[i] = NULL;
        }
    }
    DxgkpPendingDrain(VidPn);
    ExFreePoolWithTag(VidPn, TAG_DXGK_VIDPN);
    return STATUS_INSUFFICIENT_RESOURCES;
}

NTSTATUS
DxgkVidPnClone(
    _In_  D3DKMDT_HVIDPN  hSourceVidPn,
    _Out_ D3DKMDT_HVIDPN *phClonedVidPn)
{
    PDXGKP_VIDPN Source, Clone;
    ULONG i;

    PAGED_CODE();

    if (phClonedVidPn == NULL)
        return STATUS_INVALID_PARAMETER;

    *phClonedVidPn = NULL;

    Source = DxgkpVidPnFromHandle(hSourceVidPn);
    if (Source == NULL)
        return STATUS_INVALID_PARAMETER;

    Clone = (PDXGKP_VIDPN)ExAllocatePoolWithTag(
        NonPagedPool, sizeof(DXGKP_VIDPN), TAG_DXGK_VIDPN);
    if (Clone == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;

    RtlZeroMemory(Clone, sizeof(*Clone));
    InitializeListHead(&Clone->PendingObjects);
    Clone->Signature  = DXGKP_VIDPN_SIGNATURE;
    Clone->RefCount   = 1;
    Clone->Adapter    = Source->Adapter;
    Clone->NumSources = Source->NumSources;
    Clone->NumTargets = Source->NumTargets;
    Clone->NumPaths   = Source->NumPaths;

    /* Copy paths. */
    RtlCopyMemory(Clone->Paths, Source->Paths,
                   Source->NumPaths * sizeof(D3DKMDT_VIDPN_PRESENT_PATH));

    /* Deep-copy source mode sets. */
    for (i = 0; i < Source->NumSources; i++)
    {
        if (Source->SourceModeSets[i] != NULL)
        {
            Clone->SourceModeSets[i] = DxgkpAllocateSourceModeSet(Clone, i);
            if (Clone->SourceModeSets[i] == NULL)
                goto CloneFail;
            RtlCopyMemory(Clone->SourceModeSets[i], Source->SourceModeSets[i],
                           sizeof(DXGKP_VIDPN_SOURCE_MODESET));
            Clone->SourceModeSets[i]->Owner = Clone;
            Clone->SourceModeSets[i]->References = 0;
        }
    }

    /* Deep-copy target mode sets and monitor mode sets. */
    for (i = 0; i < Source->NumTargets; i++)
    {
        if (Source->TargetModeSets[i] != NULL)
        {
            Clone->TargetModeSets[i] = DxgkpAllocateTargetModeSet(Clone, i);
            if (Clone->TargetModeSets[i] == NULL)
                goto CloneFail;
            RtlCopyMemory(Clone->TargetModeSets[i], Source->TargetModeSets[i],
                           sizeof(DXGKP_VIDPN_TARGET_MODESET));
            Clone->TargetModeSets[i]->Owner = Clone;
            Clone->TargetModeSets[i]->References = 0;
        }
        if (Source->MonitorModeSets[i] != NULL)
        {
            Clone->MonitorModeSets[i] = DxgkpAllocateMonitorModeSet(Clone, i);
            if (Clone->MonitorModeSets[i] == NULL)
                goto CloneFail;
            RtlCopyMemory(Clone->MonitorModeSets[i], Source->MonitorModeSets[i],
                           sizeof(DXGKP_MONITOR_SOURCE_MODESET));
            Clone->MonitorModeSets[i]->Owner = Clone;
        }
    }

    *phClonedVidPn = (D3DKMDT_HVIDPN)Clone;
    DXGKRNL_TRACE("DxgkVidPnClone: cloned %p -> %p\n", Source, Clone);
    return STATUS_SUCCESS;

CloneFail:
    DxgkVidPnDestroy((D3DKMDT_HVIDPN)Clone);
    return STATUS_INSUFFICIENT_RESOURCES;
}

VOID
DxgkVidPnDestroy(
    _In_ D3DKMDT_HVIDPN hVidPn)
{
    PDXGKP_VIDPN VidPn;
    ULONG i;

    if (hVidPn == NULL)
        return;

    VidPn = DxgkpVidPnFromHandle(hVidPn);
    if (VidPn == NULL)
        return;

    if (InterlockedDecrement(&VidPn->RefCount) != 0)
        return;

    DXGKRNL_TRACE("DxgkVidPnDestroy: freeing VidPN %p\n", VidPn);

    VidPn->Signature = 0;

    /* Objects the miniport created but never added or released. */
    DxgkpPendingDrain(VidPn);

    for (i = 0; i < DXGKP_MAX_SOURCES; i++)
    {
        if (VidPn->SourceModeSets[i] != NULL)
        {
            ExFreePoolWithTag(VidPn->SourceModeSets[i], TAG_DXGK_MODESET);
            VidPn->SourceModeSets[i] = NULL;
        }
    }
    for (i = 0; i < DXGKP_MAX_TARGETS; i++)
    {
        if (VidPn->TargetModeSets[i] != NULL)
        {
            ExFreePoolWithTag(VidPn->TargetModeSets[i], TAG_DXGK_MODESET);
            VidPn->TargetModeSets[i] = NULL;
        }
        if (VidPn->MonitorModeSets[i] != NULL)
        {
            ExFreePoolWithTag(VidPn->MonitorModeSets[i], TAG_DXGK_MODESET);
            VidPn->MonitorModeSets[i] = NULL;
        }
    }

    ExFreePoolWithTag(VidPn, TAG_DXGK_VIDPN);
}

/* ========================================================================
 * Hot-plug detection support
 * ====================================================================== */

/* A further connected output: the desktop is cloned to it. */
typedef struct _DXGKP_HOTPLUG_EXTRA_TARGET
{
    ULONG ChildUid;
    ULONG64 ChildStateGeneration;
    BOOLEAN EdidValid;
    UCHAR Edid[128];
} DXGKP_HOTPLUG_EXTRA_TARGET;

/*
 * The connected outputs a rebuild drives.  The chosen one (ChildUid,
 * TargetId) carries the desktop; the others are cloned from it, in the
 * order they were connected.
 */
typedef struct _DXGKP_HOTPLUG_MONITOR_SNAPSHOT
{
    BOOLEAN Connected;
    BOOLEAN EdidValid;
    ULONG ChildUid;
    ULONG64 ChildStateGeneration;
    LONG64 ChildEnumerationEpoch;
    D3DDDI_VIDEO_PRESENT_TARGET_ID TargetId;
    UCHAR Edid[128];
    UCHAR EdidExtensions[DXGKP_EDID_MAX_EXTENSIONS][128];
    UCHAR EdidExtensionCount;
    ULONG ExtraCount;
    DXGKP_HOTPLUG_EXTRA_TARGET Extra[DXGKP_MAX_PATHS - 1];
} DXGKP_HOTPLUG_MONITOR_SNAPSHOT, *PDXGKP_HOTPLUG_MONITOR_SNAPSHOT;

/*
 * Bind the positional target slots to the miniport's child uids.
 *
 * The VidPn is created inside DxgkAdapterStart, before PnP has asked the
 * miniport for its child relations, so DxgkVidPnCreateForAdapter can only
 * seed the slots with the placeholders 0..NumTargets-1.  The miniport names
 * its outputs by uid and the topology carries uids, so while the placeholders
 * stand every DxgkVidPnTargetIndexFromId lookup misses and no path can be
 * built for a real output.  This is where the real uids become known.
 *
 * The binding is stable: a slot already naming a live output keeps it, and
 * only outputs that own no slot take the free ones.  Re-keying the slots from
 * list order on every rebuild would move one output's mode sets onto another
 * whenever a child appeared or disappeared.
 */
static VOID
DxgkpBindVidPnTargetsToChildren(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _Inout_ PDXGKP_VIDPN VidPn)
{
    BOOLEAN SlotBound[DXGKP_MAX_TARGETS];
    ULONG GrownUids[DXGKP_MAX_TARGETS];
    ULONG GrownCount = 0;
    ULONG Grown;
    PLIST_ENTRY Entry;
    KIRQL OldIrql;
    ULONG NumTargets;
    ULONG Slot;
    LONG64 Epoch;

    PAGED_CODE();

    NumTargets = VidPn->NumTargets;
    if (NumTargets > DXGKP_MAX_TARGETS)
        NumTargets = DXGKP_MAX_TARGETS;
    for (Slot = 0; Slot < NumTargets; Slot++)
        SlotBound[Slot] = FALSE;

    /* VidPnMutex before ChildListLock, the order the rebuild already uses. */
    (VOID)KeWaitForSingleObject(&Adapter->VidPnMutex, Executive, KernelMode, FALSE, NULL);
    KeAcquireSpinLock(&Adapter->ChildListLock, &OldIrql);
    Epoch = Adapter->ChildEnumerationEpoch;
    if (!NT_SUCCESS(DxgkHotPlugWorkCoreValidateEnumerationLocked(&Adapter->ChildEnumerationEpoch, &Adapter->ChildRelationsEnumerated, Epoch)))
    {
        /* Mid-enumeration: the list is not the live set yet.  The rebuild
         * retries, and the binding runs again against the published list. */
        KeReleaseSpinLock(&Adapter->ChildListLock, OldIrql);
        KeReleaseMutex(&Adapter->VidPnMutex, FALSE);
        return;
    }

    /* Pass 1: every slot that already names a live output keeps it. */
    for (Entry = Adapter->ChildListHead.Flink; Entry != &Adapter->ChildListHead; Entry = Entry->Flink)
    {
        PDXGK_CHILD_PDO_EXTENSION Child = CONTAINING_RECORD(Entry, DXGK_CHILD_PDO_EXTENSION, ListEntry);
        ULONG Index;

        if (!Child->Present || Child->EnumerationEpoch != Epoch || Child->Descriptor.ChildDeviceType != TypeVideoOutput)
            continue;
        Index = DxgkVidPnTargetIndexFromId(VidPn, Child->Descriptor.ChildUid);
        if (Index != MAXULONG && Index < NumTargets)
            SlotBound[Index] = TRUE;
    }

    /* Pass 2: the outputs that own no slot take the ones still free. */
    Slot = 0;
    for (Entry = Adapter->ChildListHead.Flink; Entry != &Adapter->ChildListHead; Entry = Entry->Flink)
    {
        PDXGK_CHILD_PDO_EXTENSION Child = CONTAINING_RECORD(Entry, DXGK_CHILD_PDO_EXTENSION, ListEntry);

        if (!Child->Present || Child->EnumerationEpoch != Epoch || Child->Descriptor.ChildDeviceType != TypeVideoOutput)
            continue;
        if (DxgkVidPnTargetIndexFromId(VidPn, Child->Descriptor.ChildUid) != MAXULONG)
            continue;
        while (Slot < NumTargets && SlotBound[Slot])
            Slot++;
        if (Slot >= NumTargets && NumTargets < DXGKP_MAX_TARGETS && GrownCount < RTL_NUMBER_OF(GrownUids))
        {
            /* A target reported at run time (MST, tiled) owns no slot yet;
             * it gets one once the list lock is dropped. */
            GrownUids[GrownCount++] = Child->Descriptor.ChildUid;
            continue;
        }
        if (Slot >= NumTargets)
        {
            /* More outputs than this VidPn has slots for.  The remaining ones
             * stay unreachable; DxgkpSnapshotHotPlugMonitor refuses to drive
             * an output that owns no target rather than driving the wrong one. */
            DXGKRNL_WARN("DxgkpBindVidPnTargetsToChildren: child uid %lu has no free "
                         "target slot among the %lu this VidPN describes\n",
                         Child->Descriptor.ChildUid, VidPn->NumTargets);
            break;
        }
        if (VidPn->TargetModeSets[Slot] != NULL)
            VidPn->TargetModeSets[Slot]->TargetId = Child->Descriptor.ChildUid;
        if (VidPn->MonitorModeSets[Slot] != NULL)
            VidPn->MonitorModeSets[Slot]->TargetId = Child->Descriptor.ChildUid;
        SlotBound[Slot] = TRUE;
    }
    KeReleaseSpinLock(&Adapter->ChildListLock, OldIrql);

    /* New slots for run-time targets: mode sets the miniport fills through
     * cofunctional modality, as for any output. */
    for (Grown = 0; Grown < GrownCount && VidPn->NumTargets < DXGKP_MAX_TARGETS; Grown++)
    {
        ULONG NewSlot = VidPn->NumTargets;

        if (DxgkVidPnTargetIndexFromId(VidPn, GrownUids[Grown]) != MAXULONG)
            continue;
        VidPn->TargetModeSets[NewSlot] = DxgkpAllocateTargetModeSet(VidPn, GrownUids[Grown]);
        VidPn->MonitorModeSets[NewSlot] = DxgkpAllocateMonitorModeSet(VidPn, GrownUids[Grown]);
        if (VidPn->TargetModeSets[NewSlot] == NULL || VidPn->MonitorModeSets[NewSlot] == NULL)
        {
            if (VidPn->TargetModeSets[NewSlot] != NULL)
                ExFreePoolWithTag(VidPn->TargetModeSets[NewSlot], TAG_DXGK_MODESET);
            if (VidPn->MonitorModeSets[NewSlot] != NULL)
                ExFreePoolWithTag(VidPn->MonitorModeSets[NewSlot], TAG_DXGK_MODESET);
            VidPn->TargetModeSets[NewSlot] = NULL;
            VidPn->MonitorModeSets[NewSlot] = NULL;
            break;
        }
        VidPn->NumTargets++;
        DXGKRNL_TRACE("DxgkpBindVidPnTargetsToChildren: run-time target %lu takes slot %lu\n",
                      GrownUids[Grown], NewSlot);
    }
    KeReleaseMutex(&Adapter->VidPnMutex, FALSE);
}

static NTSTATUS
DxgkpSnapshotHotPlugMonitor(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _In_ PDXGKP_VIDPN VidPn,
    _In_ LONG64 ExpectedGeneration,
    _Out_ PDXGKP_HOTPLUG_MONITOR_SNAPSHOT Snapshot)
{
    PDXGK_CHILD_PDO_EXTENSION ConnectedChild = NULL;
    PLIST_ENTRY Entry;
    KIRQL OldIrql;
    ULONG ConnectedCount = 0;

    RtlZeroMemory(Snapshot, sizeof(*Snapshot));
    KeAcquireSpinLock(&Adapter->ChildListLock, &OldIrql);
    if (InterlockedCompareExchange64(&Adapter->HotPlugGeneration, 0, 0) != ExpectedGeneration)
    {
        KeReleaseSpinLock(&Adapter->ChildListLock, OldIrql);
        return STATUS_RETRY;
    }
    Snapshot->ChildEnumerationEpoch = Adapter->ChildEnumerationEpoch;
    if (!NT_SUCCESS(DxgkHotPlugWorkCoreValidateEnumerationLocked(&Adapter->ChildEnumerationEpoch, &Adapter->ChildRelationsEnumerated, Snapshot->ChildEnumerationEpoch)))
    {
        KeReleaseSpinLock(&Adapter->ChildListLock, OldIrql);
        return STATUS_DEVICE_NOT_READY;
    }
    for (Entry = Adapter->ChildListHead.Flink; Entry != &Adapter->ChildListHead; Entry = Entry->Flink)
    {
        PDXGK_CHILD_PDO_EXTENSION Child = CONTAINING_RECORD(Entry, DXGK_CHILD_PDO_EXTENSION, ListEntry);

        if (!Child->Present || Child->EnumerationEpoch != Snapshot->ChildEnumerationEpoch || Child->Descriptor.ChildDeviceType != TypeVideoOutput)
            continue;
        /* An output joined into a tiled target is driven through that one. */
        if (!Child->Connected || Child->Joined)
            continue;
        ConnectedCount++;
        /*
         * Prefer an output that reported an EDID: on a part with several
         * connectors the one with a monitor behind it is the one whose modes
         * the topology should be built from.  Otherwise keep the first
         * connected output rather than the last, so the choice is stable
         * across rebuilds instead of depending on child list order.
         */
        if (ConnectedChild == NULL ||
            (!ConnectedChild->EdidValid && Child->EdidValid) ||
            (ConnectedChild->EdidValid == Child->EdidValid &&
             Child->ConnectSequence > ConnectedChild->ConnectSequence))
        {
            ConnectedChild = Child;
        }
    }
    if (ConnectedCount >= 1)
    {
        Snapshot->Connected = TRUE;
        Snapshot->ChildUid = ConnectedChild->Descriptor.ChildUid;
        Snapshot->ChildStateGeneration = ConnectedChild->StateGeneration;
        Snapshot->EdidValid = ConnectedChild->EdidValid;
        if (Snapshot->EdidValid)
        {
            RtlCopyMemory(Snapshot->Edid, ConnectedChild->Edid, sizeof(Snapshot->Edid));
            RtlCopyMemory(Snapshot->EdidExtensions, ConnectedChild->EdidExtensions,
                          sizeof(Snapshot->EdidExtensions));
            Snapshot->EdidExtensionCount = ConnectedChild->EdidExtensionCount;
        }
        /* Every other connected output that owns a target slot shows a
         * clone of the desktop, earliest connected first. */
        while (Snapshot->ExtraCount < RTL_NUMBER_OF(Snapshot->Extra))
        {
            PDXGK_CHILD_PDO_EXTENSION Next = NULL;

            for (Entry = Adapter->ChildListHead.Flink; Entry != &Adapter->ChildListHead; Entry = Entry->Flink)
            {
                PDXGK_CHILD_PDO_EXTENSION Child = CONTAINING_RECORD(Entry, DXGK_CHILD_PDO_EXTENSION, ListEntry);
                ULONG Taken;

                if (Child == ConnectedChild || !Child->Present || !Child->Connected ||
                    Child->EnumerationEpoch != Snapshot->ChildEnumerationEpoch ||
                    Child->Descriptor.ChildDeviceType != TypeVideoOutput ||
                    Child->Joined ||
                    DxgkVidPnTargetIndexFromId(VidPn, Child->Descriptor.ChildUid) == MAXULONG)
                {
                    continue;
                }
                for (Taken = 0; Taken < Snapshot->ExtraCount; Taken++)
                {
                    if (Snapshot->Extra[Taken].ChildUid == Child->Descriptor.ChildUid)
                        break;
                }
                if (Taken != Snapshot->ExtraCount)
                    continue;
                if (Next == NULL || Child->ConnectSequence < Next->ConnectSequence)
                    Next = Child;
            }
            if (Next == NULL)
                break;
            Snapshot->Extra[Snapshot->ExtraCount].ChildUid = Next->Descriptor.ChildUid;
            Snapshot->Extra[Snapshot->ExtraCount].ChildStateGeneration = Next->StateGeneration;
            Snapshot->Extra[Snapshot->ExtraCount].EdidValid = Next->EdidValid;
            if (Next->EdidValid)
                RtlCopyMemory(Snapshot->Extra[Snapshot->ExtraCount].Edid, Next->Edid, 128);
            Snapshot->ExtraCount++;
        }
    }
    if (InterlockedCompareExchange64(&Adapter->HotPlugGeneration, 0, 0) != ExpectedGeneration)
    {
        KeReleaseSpinLock(&Adapter->ChildListLock, OldIrql);
        return STATUS_RETRY;
    }
    KeReleaseSpinLock(&Adapter->ChildListLock, OldIrql);
    if (ConnectedCount > 1 && Snapshot->ExtraCount + 1 < ConnectedCount)
    {
        /* Outputs beyond the topology's capacity, or without a target slot,
         * stay dark. */
        DXGKRNL_WARN("DxgkpSnapshotHotPlugMonitor: %lu connected outputs, driving "
                     "%lu (desktop on child uid %lu)\n",
                     ConnectedCount, Snapshot->ExtraCount + 1, Snapshot->ChildUid);
    }
    /* Nothing connected is a complete answer for any adapter; the single
     * source this implementation drives only matters once a path exists. */
    if (!Snapshot->Connected)
        return STATUS_SUCCESS;
    /*
     * The rebuild drives one connected child through video present source 0,
     * which is what DxgkpPopulateDefaultPath below builds.  How many sources
     * the adapter declares in total is irrelevant to that: every real display
     * adapter reports several (this Intel part reports three), and demanding
     * exactly one rejected every one of them with STATUS_NOT_SUPPORTED, so no
     * VidPN was ever committed and the adapter kept a zero-sized mode.
     */
    if (VidPn->NumSources == 0)
    {
        DXGKRNL_ERR("SNAPSHOT: vidpn %p describes no video present source; "
                    "the rebuild has nothing to attach target %lu to\n",
                    VidPn, Snapshot->ChildUid);
        return STATUS_NOT_SUPPORTED;
    }
    /*
     * The target id handed to the topology is the miniport's child uid; the
     * positional arrays are reached through DxgkVidPnTargetIndexFromId.  Accepting
     * only uids below NumTargets rejected this adapter outright, because it
     * numbers its connected output 49 while declaring ten children.
     */
    Snapshot->TargetId = Snapshot->ChildUid;
    if (DxgkVidPnTargetIndexFromId(VidPn, Snapshot->TargetId) == MAXULONG)
    {
        DXGKRNL_ERR("DxgkpSnapshotHotPlugMonitor: connected child uid %lu has no target "
                    "among the %lu this VidPn describes\n",
                    Snapshot->ChildUid, VidPn->NumTargets);
        return STATUS_NOT_SUPPORTED;
    }
    return STATUS_SUCCESS;
}

static BOOLEAN
DxgkpHotPlugSnapshotCurrentLocked(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _In_ PDXGKP_HOTPLUG_MONITOR_SNAPSHOT Snapshot,
    _In_ LONG64 ExpectedGeneration,
    _Outptr_result_maybenull_ PDXGK_CHILD_PDO_EXTENSION *MatchingChild)
{
    PLIST_ENTRY Entry;
    ULONG ConnectedCount = 0;

    *MatchingChild = NULL;
    if (InterlockedCompareExchange64(&Adapter->HotPlugGeneration, 0, 0) != ExpectedGeneration)
        return FALSE;
    if (!NT_SUCCESS(DxgkHotPlugWorkCoreValidateEnumerationLocked(&Adapter->ChildEnumerationEpoch, &Adapter->ChildRelationsEnumerated, Snapshot->ChildEnumerationEpoch)))
        return FALSE;
    for (Entry = Adapter->ChildListHead.Flink; Entry != &Adapter->ChildListHead; Entry = Entry->Flink)
    {
        PDXGK_CHILD_PDO_EXTENSION Child = CONTAINING_RECORD(Entry, DXGK_CHILD_PDO_EXTENSION, ListEntry);

        if (!Child->Present || Child->EnumerationEpoch != Snapshot->ChildEnumerationEpoch || Child->Descriptor.ChildDeviceType != TypeVideoOutput || !Child->Connected)
            continue;
        ConnectedCount++;
        if (Snapshot->Connected && Child->Descriptor.ChildUid == Snapshot->ChildUid && Child->StateGeneration == Snapshot->ChildStateGeneration)
            *MatchingChild = Child;
    }
    if (!Snapshot->Connected)
        return ConnectedCount == 0;
    /* The cloned outputs must still be there, unchanged, too. */
    {
        ULONG Extra;

        for (Extra = 0; Extra < Snapshot->ExtraCount; Extra++)
        {
            BOOLEAN Current = FALSE;

            for (Entry = Adapter->ChildListHead.Flink; Entry != &Adapter->ChildListHead; Entry = Entry->Flink)
            {
                PDXGK_CHILD_PDO_EXTENSION Child = CONTAINING_RECORD(Entry, DXGK_CHILD_PDO_EXTENSION, ListEntry);

                if (Child->Present && Child->Connected &&
                    Child->Descriptor.ChildUid == Snapshot->Extra[Extra].ChildUid &&
                    Child->StateGeneration == Snapshot->Extra[Extra].ChildStateGeneration)
                {
                    Current = TRUE;
                    break;
                }
            }
            if (!Current)
                return FALSE;
        }
    }
    /*
     * The snapshot is still current when the output it chose is still present,
     * still connected and unchanged.  Demanding that no *other* output be
     * connected contradicted DxgkpSnapshotHotPlugMonitor, which deliberately
     * drives one of several connected outputs rather than abandoning the
     * VidPn: on a part with two live connectors the snapshot succeeded, this
     * re-check then failed it, and the rebuild retried until it retired --
     * without ever committing a path.
     */
    return *MatchingChild != NULL;
}

static NTSTATUS
DxgkpRefreshHotPlugEdid(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _Inout_ PDXGKP_HOTPLUG_MONITOR_SNAPSHOT Snapshot)
{
    PDXGKDDI_QUERY_DEVICE_DESCRIPTOR QueryDeviceDescriptor;
    DXGK_DEVICE_DESCRIPTOR Descriptor;
    NTSTATUS Status = STATUS_UNSUCCESSFUL;

    if (!Snapshot->Connected || Snapshot->EdidValid)
        return STATUS_SUCCESS;
    QueryDeviceDescriptor = DXGK_CB(Adapter, DxgkDdiQueryDeviceDescriptor);
    if (QueryDeviceDescriptor == NULL)
        return STATUS_SUCCESS;
    RtlZeroMemory(&Descriptor, sizeof(Descriptor));
    Descriptor.DescriptorOffset = 0;
    Descriptor.DescriptorLength = sizeof(Snapshot->Edid);
    Descriptor.DescriptorBuffer = Snapshot->Edid;
    if (!DxgkAcquireKmdCall(Adapter))
        return STATUS_DELETE_PENDING;
    _SEH2_TRY
    {
        Status = QueryDeviceDescriptor(Adapter->MiniportDeviceContext, Snapshot->ChildUid, &Descriptor);
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Status = _SEH2_GetExceptionCode();
    }
    _SEH2_END;
    DxgkReleaseKmdCall(Adapter);
    if (NT_SUCCESS(Status))
    {
        Snapshot->EdidExtensionCount =
            DxgkPnpReadEdidExtensions(Adapter, Snapshot->ChildUid,
                                      Snapshot->Edid, Snapshot->EdidExtensions);
        Snapshot->EdidValid = TRUE;
        return STATUS_SUCCESS;
    }
    if (Status == STATUS_MONITOR_NO_DESCRIPTOR || Status == STATUS_NOT_SUPPORTED)
        return STATUS_SUCCESS;
    DXGKRNL_WARN("DxgkpRefreshHotPlugEdid: ChildUid %lu descriptor query failed 0x%08lX; retaining existing modes\n", Snapshot->ChildUid, Status);
    return STATUS_SUCCESS;
}

static VOID
DxgkpRefreshConnectedChildEdids(
    _In_ PDXGKRNL_ADAPTER Adapter)
{
    PDXGKDDI_QUERY_DEVICE_DESCRIPTOR QueryDeviceDescriptor;
    ULONG ChildUids[16];
    ULONG64 ChildGenerations[16];
    UCHAR Edid[128];
    UCHAR EdidExtensions[DXGKP_EDID_MAX_EXTENSIONS][128];
    UCHAR EdidExtensionCount;
    DXGK_DEVICE_DESCRIPTOR Descriptor;
    PLIST_ENTRY Entry;
    ULONG Count = 0;
    ULONG Index;
    KIRQL OldIrql;
    NTSTATUS Status;

    QueryDeviceDescriptor = DXGK_CB(Adapter, DxgkDdiQueryDeviceDescriptor);
    if (QueryDeviceDescriptor == NULL)
        return;
    KeAcquireSpinLock(&Adapter->ChildListLock, &OldIrql);
    for (Entry = Adapter->ChildListHead.Flink;
         Entry != &Adapter->ChildListHead && Count < RTL_NUMBER_OF(ChildUids);
         Entry = Entry->Flink)
    {
        PDXGK_CHILD_PDO_EXTENSION Child = CONTAINING_RECORD(Entry, DXGK_CHILD_PDO_EXTENSION, ListEntry);

        if (!Child->Present || Child->Descriptor.ChildDeviceType != TypeVideoOutput ||
            !Child->Connected || Child->EdidValid)
            continue;
        ChildUids[Count] = Child->Descriptor.ChildUid;
        ChildGenerations[Count] = Child->StateGeneration;
        Count++;
    }
    KeReleaseSpinLock(&Adapter->ChildListLock, OldIrql);

    for (Index = 0; Index < Count; Index++)
    {
        RtlZeroMemory(&Descriptor, sizeof(Descriptor));
        Descriptor.DescriptorOffset = 0;
        Descriptor.DescriptorLength = sizeof(Edid);
        Descriptor.DescriptorBuffer = Edid;
        if (!DxgkAcquireKmdCall(Adapter))
            return;
        _SEH2_TRY
        {
            Status = QueryDeviceDescriptor(Adapter->MiniportDeviceContext, ChildUids[Index], &Descriptor);
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            Status = _SEH2_GetExceptionCode();
        }
        _SEH2_END;
        DxgkReleaseKmdCall(Adapter);
        if (!NT_SUCCESS(Status))
            continue;
        EdidExtensionCount = DxgkPnpReadEdidExtensions(Adapter, ChildUids[Index],
                                                       Edid, EdidExtensions);
        KeAcquireSpinLock(&Adapter->ChildListLock, &OldIrql);
        for (Entry = Adapter->ChildListHead.Flink; Entry != &Adapter->ChildListHead; Entry = Entry->Flink)
        {
            PDXGK_CHILD_PDO_EXTENSION Child = CONTAINING_RECORD(Entry, DXGK_CHILD_PDO_EXTENSION, ListEntry);

            if (Child->Descriptor.ChildUid != ChildUids[Index])
                continue;
            if (Child->Connected && Child->StateGeneration == ChildGenerations[Index])
            {
                RtlCopyMemory(Child->Edid, Edid, sizeof(Child->Edid));
                RtlCopyMemory(Child->EdidExtensions, EdidExtensions,
                              sizeof(Child->EdidExtensions));
                Child->EdidExtensionCount = EdidExtensionCount;
                Child->EdidValid = TRUE;
            }
            break;
        }
        KeReleaseSpinLock(&Adapter->ChildListLock, OldIrql);
    }
}

static NTSTATUS DxgkpVidPnRebuildForHotPlugGeneration(_In_ PDXGKRNL_ADAPTER Adapter, _In_ LONG64 ExpectedGeneration);


/* Marks or clears a target's source as mid link-configuration. */
static VOID
DxgkpSetLinkConfiguring(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _In_ D3DDDI_VIDEO_PRESENT_TARGET_ID TargetId,
    _In_ BOOLEAN Configuring)
{
    ULONG SourceId = DxgkVidPnVsyncSourceFromTarget(Adapter, TargetId);

    if (SourceId >= 32)
        return;
    if (Configuring)
        InterlockedOr(&Adapter->LinkConfiguringSources, (LONG)(1UL << SourceId));
    else
        InterlockedAnd(&Adapter->LinkConfiguringSources, ~(LONG)(1UL << SourceId));
    InterlockedIncrement(&Adapter->LinkConfigGeneration);
}

static VOID
DxgkpDrainConnectionChanges(
    _In_ PDXGKRNL_ADAPTER Adapter)
{
    PDXGKDDI_QUERYCONNECTIONCHANGE QueryConnectionChange;
    DXGKARG_QUERYCONNECTIONCHANGE Args;
    BOOLEAN Changed;
    BOOLEAN AnyChanged = FALSE;
    BOOLEAN RearmVsync = FALSE;
    ULONG Count;
    NTSTATUS Status = STATUS_SUCCESS;

    PAGED_CODE();
    QueryConnectionChange = DXGK_CB_FULL(Adapter, DxgkDdiQueryConnectionChange);
    if (QueryConnectionChange == NULL)
        return;
    if (!DxgkBeginKmdTransaction(Adapter))
    {
        InterlockedExchange(&Adapter->ConnectorChangePending, 1);
        return;
    }
    for (Count = 0; Count < 256; Count++)
    {
        RtlZeroMemory(&Args, sizeof(Args));
        if (!DxgkAcquireKmdCall(Adapter))
        {
            InterlockedExchange(&Adapter->ConnectorChangePending, 1);
            break;
        }
        _SEH2_TRY
        {
            Status = QueryConnectionChange(Adapter->MiniportDeviceContext, &Args);
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            Status = _SEH2_GetExceptionCode();
        }
        _SEH2_END;
        DxgkReleaseKmdCall(Adapter);
        if (Status == STATUS_ALREADY_COMPLETE)
            break;
        if (!NT_SUCCESS(Status))
        {
            DXGKRNL_WARN("CONNECTOR_CHANGE: adapter %p query failed 0x%08lX\n", Adapter, Status);
            break;
        }
        DXGKRNL_INFO("CONNECTOR_CHANGE: adapter %p id=%I64u target=%u status=%u\n",
                     Adapter, Args.ConnectionChange.ConnectionChangeId,
                     (UINT)Args.ConnectionChange.TargetId, (UINT)Args.ConnectionChange.ConnectionStatus);
        switch (Args.ConnectionChange.ConnectionStatus)
        {
            case MonitorStatusConnected:
            case MonitorStatusDisconnected:
                break;

            case LinkConfigurationFailed:
                /* The rebuild this worker runs after draining re-enumerates
                 * co-functional timings and commits again, which is the
                 * documented response; a failure on that commit's own
                 * SetTimingsFromVidPn result is retried there, bounded. */
                DxgkpSetLinkConfiguring(Adapter, Args.ConnectionChange.TargetId, FALSE);
                DXGKRNL_WARN("CONNECTOR_CHANGE: adapter %p target %u link "
                             "training failed; the rebuild retries with "
                             "re-enumerated timings\n",
                             Adapter, (UINT)Args.ConnectionChange.TargetId);
                continue;

            case LinkConfigurationStarted:
            case LinkConfigurationSucceeded:
                /* Started stops scan-out on the target and loses its
                 * v-blanks until Succeeded resumes it; flips and v-blank
                 * waits there are held rather than timed out across the
                 * window (DxgkpWaitForMmioScanout, DxgkpWaitForVerticalBlank),
                 * and on success v-blank interrupts are turned back on. */
                DxgkpSetLinkConfiguring(Adapter, Args.ConnectionChange.TargetId,
                                        Args.ConnectionChange.ConnectionStatus ==
                                            LinkConfigurationStarted);
                if (Args.ConnectionChange.ConnectionStatus == LinkConfigurationSucceeded)
                    RearmVsync = TRUE;
                DXGKRNL_TRACE("CONNECTOR_CHANGE: adapter %p target %u link "
                              "configuration %s\n",
                              Adapter, (UINT)Args.ConnectionChange.TargetId,
                              Args.ConnectionChange.ConnectionStatus ==
                                  LinkConfigurationStarted ?
                                  "started" : "succeeded");
                continue;

            case TargetStatusConnected:
            case TargetStatusJoined:
            {
                BOOLEAN Join = Args.ConnectionChange.ConnectionStatus == TargetStatusJoined;

                /* A downstream (MST) or joined (tiled) target appears; its
                 * monitor is reported on it separately. */
                if (NT_SUCCESS(DxgkPnpAddDynamicTarget(
                                   Adapter,
                                   Args.ConnectionChange.TargetId,
                                   Join ? Args.ConnectionChange.TargetJoin.NewTargetId
                                        : Args.ConnectionChange.TargetConnect.NewTargetId,
                                   Join ? Args.ConnectionChange.TargetJoin.BaseTargetType
                                        : Args.ConnectionChange.TargetConnect.BaseTargetType,
                                   Join,
                                   &Changed)) && Changed)
                {
                    AnyChanged = TRUE;
                }
                continue;
            }

            case TargetStatusDisconnected:
                /* The target and everything reached through it are gone. */
                if (NT_SUCCESS(DxgkPnpRemoveTarget(Adapter, Args.ConnectionChange.TargetId, &Changed)) &&
                    Changed)
                {
                    AnyChanged = TRUE;
                }
                continue;

            default:
                DXGKRNL_WARN("CONNECTOR_CHANGE: adapter %p target %u status %u "
                             "not defined\n",
                             Adapter, (UINT)Args.ConnectionChange.TargetId,
                             (UINT)Args.ConnectionChange.ConnectionStatus);
                continue;
        }
        if (NT_SUCCESS(DxgkPnpIndicateChildConnection(Adapter,
                                                      Args.ConnectionChange.TargetId,
                                                      Args.ConnectionChange.ConnectionStatus == MonitorStatusConnected,
                                                      &Changed)) && Changed)
        {
            AnyChanged = TRUE;
        }
    }
    DxgkEndKmdTransaction(Adapter);
    if (RearmVsync)
    {
        Status = DxgkRearmVsyncInterrupt(Adapter);
        if (!NT_SUCCESS(Status) && Status != STATUS_NOT_SUPPORTED)
        {
            DXGKRNL_WARN("CONNECTOR_CHANGE: adapter %p v-blank interrupt not "
                         "re-armed after link configuration 0x%08lX\n",
                         Adapter, Status);
        }
    }
    if (AnyChanged)
        IoInvalidateDeviceRelations(Adapter->PhysicalDeviceObject, BusRelations);
}

static VOID
NTAPI
DxgkpHotPlugRebuildWorker(
    _In_ PVOID Context)
{
    PDXGKRNL_ADAPTER Adapter = Context;
    LONG64 ObservedGeneration;
    LONG64 RetryGeneration = 0;
    ULONG RetryCount = 0;
    KIRQL OldIrql;
    NTSTATUS Status;

    PAGED_CODE();
    for (;;)
    {
        if (InterlockedExchange(&Adapter->ConnectorChangePending, 0) != 0 &&
            Adapter->State == DxgkAdapterStateStarted)
        {
            DxgkpDrainConnectionChanges(Adapter);
        }
        KeAcquireSpinLock(&Adapter->ChildListLock, &OldIrql);
        if (Adapter->State != DxgkAdapterStateStarted || InterlockedCompareExchange(&Adapter->RundownStarted, 0, 0) != 0)
        {
            (VOID)DxgkHotPlugWorkCoreCompleteLocked(&Adapter->HotPlugGeneration, &Adapter->HotPlugWorkActive, Adapter->HotPlugGeneration, FALSE);
            KeReleaseSpinLock(&Adapter->ChildListLock, OldIrql);
            break;
        }
        ObservedGeneration = Adapter->HotPlugGeneration;
        if (RetryGeneration != ObservedGeneration)
        {
            RetryGeneration = ObservedGeneration;
            RetryCount = 0;
        }
        KeReleaseSpinLock(&Adapter->ChildListLock, OldIrql);
        Status = DxgkpVidPnRebuildForHotPlugGeneration(Adapter, ObservedGeneration);
        KeAcquireSpinLock(&Adapter->ChildListLock, &OldIrql);
        if (Adapter->State != DxgkAdapterStateStarted || InterlockedCompareExchange(&Adapter->RundownStarted, 0, 0) != 0)
        {
            (VOID)DxgkHotPlugWorkCoreCompleteLocked(&Adapter->HotPlugGeneration, &Adapter->HotPlugWorkActive, Adapter->HotPlugGeneration, FALSE);
            KeReleaseSpinLock(&Adapter->ChildListLock, OldIrql);
            break;
        }
        if (Adapter->HotPlugGeneration != ObservedGeneration)
        {
            RetryCount = 0;
            KeReleaseSpinLock(&Adapter->ChildListLock, OldIrql);
            continue;
        }
        /* The adapter is started and not running down (checked above), so a
         * refused miniport call means another thread still holds the KMD
         * exclusively -- the tail of the start that queued this rebuild --
         * which is a wait, not a removal. */
        if (Status == STATUS_DELETE_PENDING)
            Status = STATUS_DEVICE_BUSY;
        if (DxgkHotPlugWorkCoreShouldRetry(Status, RetryCount))
        {
            LARGE_INTEGER Delay;
            ULONG DelayMs = DxgkHotPlugWorkCoreRetryDelayMs(RetryCount);

            RetryCount++;
            KeReleaseSpinLock(&Adapter->ChildListLock, OldIrql);
            Delay.QuadPart = -(LONGLONG)DelayMs * 10000;
            KeDelayExecutionThread(KernelMode, FALSE, &Delay);
            continue;
        }
        if (!NT_SUCCESS(Status))
            DXGKRNL_ERR("DxgkpHotPlugRebuildWorker: adapter %p generation %I64d rebuild retired after %lu retries with 0x%08lX\n", Adapter, ObservedGeneration, RetryCount, Status);
        if (!DxgkHotPlugWorkCoreCompleteLocked(&Adapter->HotPlugGeneration, &Adapter->HotPlugWorkActive, ObservedGeneration, TRUE))
        {
            KeReleaseSpinLock(&Adapter->ChildListLock, OldIrql);
            continue;
        }
        KeReleaseSpinLock(&Adapter->ChildListLock, OldIrql);
        break;
    }
    ExReleaseRundownProtection(&Adapter->RundownRef);
}

VOID
DxgkVidPnInitializeHotPlugWorker(
    _In_ PDXGKRNL_ADAPTER Adapter)
{
    if (Adapter != NULL)
        ExInitializeWorkItem(&Adapter->HotPlugWorkItem, DxgkpHotPlugRebuildWorker, Adapter);
}

NTSTATUS
DxgkVidPnQueueHotPlugRebuild(
    _In_ PDXGKRNL_ADAPTER Adapter)
{
    KIRQL OldIrql;

    if (Adapter == NULL)
        return STATUS_INVALID_PARAMETER;
    if (Adapter->State != DxgkAdapterStateStarted || InterlockedCompareExchange(&Adapter->RundownStarted, 0, 0) != 0)
        return STATUS_DELETE_PENDING;
    KeAcquireSpinLock(&Adapter->ChildListLock, &OldIrql);
    (VOID)DxgkHotPlugWorkCorePublishLocked(&Adapter->HotPlugGeneration);
    KeReleaseSpinLock(&Adapter->ChildListLock, OldIrql);
    if (!ExAcquireRundownProtection(&Adapter->RundownRef))
        return STATUS_DELETE_PENDING;
    if (Adapter->State != DxgkAdapterStateStarted || InterlockedCompareExchange(&Adapter->RundownStarted, 0, 0) != 0)
    {
        ExReleaseRundownProtection(&Adapter->RundownRef);
        return STATUS_DELETE_PENDING;
    }
    KeAcquireSpinLock(&Adapter->ChildListLock, &OldIrql);
    if (Adapter->State != DxgkAdapterStateStarted || InterlockedCompareExchange(&Adapter->RundownStarted, 0, 0) != 0)
    {
        KeReleaseSpinLock(&Adapter->ChildListLock, OldIrql);
        ExReleaseRundownProtection(&Adapter->RundownRef);
        return STATUS_DELETE_PENDING;
    }
    if (!DxgkHotPlugWorkCoreTryActivateLocked(&Adapter->HotPlugWorkActive))
    {
        KeReleaseSpinLock(&Adapter->ChildListLock, OldIrql);
        ExReleaseRundownProtection(&Adapter->RundownRef);
        return STATUS_SUCCESS;
    }
    KeReleaseSpinLock(&Adapter->ChildListLock, OldIrql);
    ExQueueWorkItem(&Adapter->HotPlugWorkItem, DelayedWorkQueue);
    return STATUS_SUCCESS;
}

static BOOLEAN
DxgkpParseEdidPreferredSignal(
    _In_reads_bytes_(128) CONST UCHAR *Edid,
    _Out_ D3DKMDT_VIDEO_SIGNAL_INFO *Signal)
{
    CONST UCHAR *Timing = &Edid[54];
    ULONG Checksum = 0;
    ULONG HActive;
    ULONG HBlank;
    ULONG VActive;
    ULONG VBlank;
    ULONG HTotal;
    ULONG VTotal;
    ULONG PixelClock;
    ULONG Index;

    if (Edid[0] != 0x00 || Edid[1] != 0xff || Edid[2] != 0xff || Edid[3] != 0xff || Edid[4] != 0xff || Edid[5] != 0xff || Edid[6] != 0xff || Edid[7] != 0x00)
        return FALSE;
    for (Index = 0; Index < 128; ++Index)
        Checksum += Edid[Index];
    if ((Checksum & 0xff) != 0)
        return FALSE;
    PixelClock = ((ULONG)Timing[1] << 8 | Timing[0]) * 10000UL;
    HActive = Timing[2] | ((ULONG)(Timing[4] & 0xf0) << 4);
    HBlank = Timing[3] | ((ULONG)(Timing[4] & 0x0f) << 8);
    VActive = Timing[5] | ((ULONG)(Timing[7] & 0xf0) << 4);
    VBlank = Timing[6] | ((ULONG)(Timing[7] & 0x0f) << 8);
    HTotal = HActive + HBlank;
    VTotal = VActive + VBlank;
    if (PixelClock == 0 || HActive == 0 || VActive == 0 || HTotal <= HActive || VTotal <= VActive || HActive > 16384 || VActive > 16384)
        return FALSE;
    RtlZeroMemory(Signal, sizeof(*Signal));
    Signal->VideoStandard = D3DKMDT_VSS_OTHER;
    Signal->TotalSize.cx = HTotal;
    Signal->TotalSize.cy = VTotal;
    Signal->ActiveSize.cx = HActive;
    Signal->ActiveSize.cy = VActive;
    Signal->VSyncFreq.Numerator = PixelClock;
    Signal->VSyncFreq.Denominator = HTotal * VTotal;
    Signal->HSyncFreq.Numerator = PixelClock;
    Signal->HSyncFreq.Denominator = HTotal;
    Signal->PixelRate = PixelClock;
    Signal->ScanLineOrdering = (Timing[17] & 0x80) != 0 ? D3DDDI_VSSLO_INTERLACED_UPPERFIELDFIRST : D3DDDI_VSSLO_PROGRESSIVE;
    return TRUE;
}

static NTSTATUS
DxgkpAddEdidPreferredModes(
    _Inout_ PDXGKP_VIDPN VidPn,
    _In_ D3DDDI_VIDEO_PRESENT_TARGET_ID TargetId,
    _In_reads_bytes_(128) CONST UCHAR *Edid)
{
    PDXGKP_VIDPN_SOURCE_MODESET SourceSet = VidPn->SourceModeSets[0];
    ULONG TargetIndex = DxgkVidPnTargetIndexFromId(VidPn, TargetId);
    PDXGKP_VIDPN_TARGET_MODESET TargetSet =
        (TargetIndex != MAXULONG) ? VidPn->TargetModeSets[TargetIndex] : NULL;
    PDXGKP_MONITOR_SOURCE_MODESET MonitorSet =
        (TargetIndex != MAXULONG) ? VidPn->MonitorModeSets[TargetIndex] : NULL;
    D3DKMDT_VIDEO_SIGNAL_INFO Signal;
    D3DKMDT_VIDPN_SOURCE_MODE SourceMode;
    D3DKMDT_VIDPN_TARGET_MODE TargetMode;
    D3DKMDT_MONITOR_SOURCE_MODE MonitorMode;
    SIZE_T Index;
    BOOLEAN Found;

    if (SourceSet == NULL || TargetSet == NULL || MonitorSet == NULL)
        return STATUS_GRAPHICS_INVALID_VIDPN;
    if (!DxgkpParseEdidPreferredSignal(Edid, &Signal))
        return STATUS_SUCCESS;
    for (Index = 0; Index < TargetSet->NumModes; ++Index)
        TargetSet->Modes[Index].Preference = D3DKMDT_MP_NOTPREFERRED;
    for (Index = 0; Index < MonitorSet->NumModes; ++Index)
        MonitorSet->Modes[Index].Preference = D3DKMDT_MP_NOTPREFERRED;
    DxgkpPopulateDefaultSourceMode(&SourceMode, SourceSet->NextModeId, Signal.ActiveSize.cx, Signal.ActiveSize.cy);
    Found = FALSE;
    for (Index = 0; Index < SourceSet->NumModes; ++Index)
    {
        if (!DxgkpAreEquivalentSourceModes(&SourceSet->Modes[Index], &SourceMode))
            continue;
        SourceSet->PinnedModeId = SourceSet->Modes[Index].Id;
        Found = TRUE;
        break;
    }
    if (!Found)
    {
        if (SourceSet->NumModes >= DXGKP_MAX_MODES)
            return STATUS_GRAPHICS_RESOURCES_NOT_RELATED;
        SourceMode.Id = SourceSet->NextModeId++;
        SourceSet->Modes[SourceSet->NumModes++] = SourceMode;
        SourceSet->PinnedModeId = SourceMode.Id;
    }
    RtlZeroMemory(&TargetMode, sizeof(TargetMode));
    TargetMode.Id = TargetSet->NextModeId;
    TargetMode.VideoSignalInfo = Signal;
    TargetMode.Preference = D3DKMDT_MP_PREFERRED;
    Found = FALSE;
    for (Index = 0; Index < TargetSet->NumModes; ++Index)
    {
        if (!DxgkpAreEquivalentTargetModes(&TargetSet->Modes[Index], &TargetMode))
            continue;
        TargetSet->Modes[Index].Preference = D3DKMDT_MP_PREFERRED;
        TargetSet->PinnedModeId = TargetSet->Modes[Index].Id;
        Found = TRUE;
        break;
    }
    if (!Found)
    {
        if (TargetSet->NumModes >= DXGKP_MAX_MODES)
            return STATUS_GRAPHICS_RESOURCES_NOT_RELATED;
        TargetMode.Id = TargetSet->NextModeId++;
        TargetSet->Modes[TargetSet->NumModes++] = TargetMode;
        TargetSet->PinnedModeId = TargetMode.Id;
    }
    RtlZeroMemory(&MonitorMode, sizeof(MonitorMode));
    MonitorMode.Id = MonitorSet->NextModeId;
    MonitorMode.VideoSignalInfo = Signal;
    MonitorMode.ColorBasis = D3DKMDT_CB_SRGB;
    MonitorMode.ColorCoeffDynamicRanges.FirstChannel = 8;
    MonitorMode.ColorCoeffDynamicRanges.SecondChannel = 8;
    MonitorMode.ColorCoeffDynamicRanges.ThirdChannel = 8;
    MonitorMode.Origin = D3DKMDT_MCO_MONITORDESCRIPTOR;
    MonitorMode.Preference = D3DKMDT_MP_PREFERRED;
    Found = FALSE;
    for (Index = 0; Index < MonitorSet->NumModes; ++Index)
    {
        if (!DxgkpAreEquivalentMonitorModes(&MonitorSet->Modes[Index], &MonitorMode))
            continue;
        MonitorSet->Modes[Index].Preference = D3DKMDT_MP_PREFERRED;
        Found = TRUE;
        break;
    }
    if (!Found)
    {
        if (MonitorSet->NumModes >= DXGKP_MAX_MODES)
            return STATUS_GRAPHICS_RESOURCES_NOT_RELATED;
        MonitorMode.Id = MonitorSet->NextModeId++;
        MonitorSet->Modes[MonitorSet->NumModes++] = MonitorMode;
    }
    return STATUS_SUCCESS;
}

/*
 * Prepare the candidate for the miniport's recommendation.
 *
 * The topology is emptied and the monitor's modes are made available, but no
 * path is built here.  DxgkDdiRecommendFunctionalVidPn and
 * DxgkDdiRecommendVidPnTopology are DDIs the driver *populates*, and Windows
 * hands them a VidPN it has just created and left empty:
 * VIDPN_MGR::RecommendFunctionalVidPn calls VIDPN_MGR::CreateClientVidPn and
 * passes that fresh object as hRecommendedFunctionalVidPn, then validates what
 * came back with DMMVIDPN::IsFunctional.
 *
 * Seeding a path first made the normal driver answer impossible: asked to
 * recommend a configuration for the connected output, the driver adds a path
 * for that output, VidPnTopology_AddPath refuses it because the same target is
 * already in the topology, the driver fails its own DDI, and the rebuild dies
 * carrying the driver's error.  Nothing then ever adds a path, so the VidPN a
 * full miniport starts with -- the contractually empty one from
 * DxgkVidPnCreateForAdapter -- stays empty for the life of the adapter.
 *
 * DxgkpSeedDefaultHotPlugPath builds the OS's own path afterwards, and only if
 * the driver produced none.
 */
/*
 * DxgkpPrepareVidPnForRecommendation
 *
 * Windows passes a recommendation DDI a VidPN it has just created: no
 * topology and nothing pinned.  A clone of the committed VidPN still carries
 * the committed pins, and a driver that recommends a new configuration by
 * replacing a mode set is then refused with
 * STATUS_GRAPHICS_PINNED_MODE_MUST_REMAIN_IN_SET -- failing a recommendation
 * Windows accepts.  Clear the topology and every pin; the modes stay, as
 * hints the driver may use or replace.
 */
static VOID
DxgkpPrepareVidPnForRecommendation(
    _Inout_ PDXGKP_VIDPN VidPn)
{
    ULONG i;

    RtlZeroMemory(VidPn->Paths, sizeof(VidPn->Paths));
    VidPn->NumPaths = 0;
    for (i = 0; i < DXGKP_MAX_SOURCES; i++)
    {
        if (VidPn->SourceModeSets[i] != NULL)
            VidPn->SourceModeSets[i]->PinnedModeId = (UINT)-1;
    }
    for (i = 0; i < DXGKP_MAX_TARGETS; i++)
    {
        if (VidPn->TargetModeSets[i] != NULL)
            VidPn->TargetModeSets[i]->PinnedModeId = (UINT)-1;
    }
}

static NTSTATUS
DxgkpBuildHotPlugCandidate(
    _Inout_ PDXGKP_VIDPN VidPn,
    _In_ PDXGKP_HOTPLUG_MONITOR_SNAPSHOT Snapshot)
{
    DxgkpPrepareVidPnForRecommendation(VidPn);
    if (!Snapshot->Connected)
        return STATUS_SUCCESS;
    if (DxgkVidPnTargetIndexFromId(VidPn, Snapshot->TargetId) == MAXULONG ||
        VidPn->NumSources == 0)
        return STATUS_NOT_SUPPORTED;
    if (Snapshot->EdidValid)
    {
        NTSTATUS Status = DxgkpAddEdidPreferredModes(VidPn, Snapshot->TargetId, Snapshot->Edid);
        ULONG Extra;

        if (!NT_SUCCESS(Status))
            return Status;
        for (Extra = 0; Extra < Snapshot->ExtraCount; Extra++)
        {
            if (Snapshot->Extra[Extra].EdidValid)
                (VOID)DxgkpAddEdidPreferredModes(VidPn, Snapshot->Extra[Extra].ChildUid,
                                                 Snapshot->Extra[Extra].Edid);
        }
    }
    return STATUS_SUCCESS;
}

/*
 * Wire the connected output to a source when the miniport recommended nothing.
 *
 * Which source drives which target is the OS's decision in WDDM, not the
 * driver's: Windows builds the topology on its own side through
 * VIDPN_MGR::AddPathToVidPnTopology, whose callers are OS topology
 * constructors (BTL_TOPOLOGY_CONSTRUCTOR::_AddExternalPathsToTopology,
 * ::_AddSecondaryPathToTopology, CDS_JOURNAL::_ExtendTopology), and only
 * *asks* the driver for an opinion.
 * A driver that answers STATUS_GRAPHICS_NO_RECOMMENDED_FUNCTIONAL_VIDPN, or
 * that publishes neither recommendation DDI, has no opinion, and the caller's
 * own path stands.  Source 0 is the one this implementation drives; see
 * DxgkpSnapshotHotPlugMonitor.
 */
static NTSTATUS
DxgkpSeedDefaultHotPlugPath(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _Inout_ PDXGKP_VIDPN VidPn,
    _In_ PDXGKP_HOTPLUG_MONITOR_SNAPSHOT Snapshot)
{
    ULONG TargetIndex;

    if (!Snapshot->Connected || VidPn->NumPaths != 0)
        return STATUS_SUCCESS;
    TargetIndex = DxgkVidPnTargetIndexFromId(VidPn, Snapshot->TargetId);
    if (TargetIndex == MAXULONG || VidPn->NumSources == 0)
        return STATUS_NOT_SUPPORTED;

    DxgkpPopulateDefaultPath(&VidPn->Paths[0], 0, Snapshot->TargetId);
    VidPn->NumPaths = 1;
    /* The other connected outputs show the same source: a clone. */
    {
        ULONG Extra;

        for (Extra = 0; Extra < Snapshot->ExtraCount && VidPn->NumPaths < DXGKP_MAX_PATHS; Extra++)
        {
            ULONG ExtraIndex = DxgkVidPnTargetIndexFromId(VidPn, Snapshot->Extra[Extra].ChildUid);

            if (ExtraIndex == MAXULONG)
                continue;
            DxgkpPopulateDefaultPath(&VidPn->Paths[VidPn->NumPaths], 0, Snapshot->Extra[Extra].ChildUid);
            if (VidPn->TargetModeSets[ExtraIndex] != NULL)
                VidPn->TargetModeSets[ExtraIndex]->PinnedModeId = (UINT)-1;
            VidPn->NumPaths++;
        }
    }

    if (Adapter->MiniportContext != NULL &&
        !Adapter->MiniportContext->IsDisplayOnlyDriver &&
        !Adapter->MiniportContext->IsBasicDisplayFallback &&
        DXGK_CB(Adapter, DxgkDdiEnumVidPnCofuncModality) != NULL)
    {
        SIZE_T PathIndex;

        /*
         * This is an OS-created starting topology, not a functional VidPN
         * yet.  Leave its transformations unpinned so a full miniport can
         * publish the scaling and rotation combinations it actually
         * supports.  Pinning identity here made portrait-native panels
         * impossible to enumerate: their miniport could report Rotate90
         * support, but dxgkrnl would still hand identity to IsSupportedVidPn.
         *
         * The EDID modes added while constructing this candidate are monitor
         * constraints, not an OS desktop-mode decision.  Unpin the source and
         * target too, allowing a landscape source mode and portrait target
         * timing to be joined by the rotation selected after enumeration.
         */
        for (PathIndex = 0; PathIndex < VidPn->NumPaths; PathIndex++)
        {
            D3DKMDT_VIDPN_PRESENT_PATH *Path = &VidPn->Paths[PathIndex];

            Path->ContentTransformation.Scaling = D3DKMDT_VPPS_UNPINNED;
            RtlZeroMemory(&Path->ContentTransformation.ScalingSupport,
                          sizeof(Path->ContentTransformation.ScalingSupport));
            Path->ContentTransformation.Rotation = D3DKMDT_VPPR_UNPINNED;
            RtlZeroMemory(&Path->ContentTransformation.RotationSupport,
                          sizeof(Path->ContentTransformation.RotationSupport));
        }

        if (VidPn->SourceModeSets[0] != NULL)
            VidPn->SourceModeSets[0]->PinnedModeId = (UINT)-1;
        if (VidPn->TargetModeSets[TargetIndex] != NULL)
            VidPn->TargetModeSets[TargetIndex]->PinnedModeId = (UINT)-1;
    }

    return STATUS_SUCCESS;
}

/*
 * Check what the miniport built into our VidPN.
 *
 * Only the invariants the rest of dxgkrnl relies on: every path names a source
 * this adapter declares and a target that resolves to one of its mode-set
 * slots, and no target is driven twice.  How *many* paths there are is not one
 * of them.  Refusing anything past a single path threw away the answer of any
 * driver that recommended a real multi-output configuration -- this Intel part
 * declares three sources and ten outputs -- and left the topology empty, which
 * is the one state that cannot be committed at all.  Several paths sharing a
 * source is clone mode and is legal.
 */
static NTSTATUS
DxgkpValidateRecommendedTopology(
    _In_ PDXGKP_VIDPN VidPn)
{
    SIZE_T Index;
    SIZE_T Other;

    if (VidPn->Signature != DXGKP_VIDPN_SIGNATURE)
        return STATUS_GRAPHICS_INVALID_VIDPN;
    if (VidPn->NumPaths > DXGKP_MAX_PATHS)
        return STATUS_GRAPHICS_INVALID_VIDPN_TOPOLOGY;
    for (Index = 0; Index < VidPn->NumPaths; Index++)
    {
        if (VidPn->Paths[Index].VidPnSourceId >= VidPn->NumSources ||
            DxgkVidPnTargetIndexFromId(VidPn, VidPn->Paths[Index].VidPnTargetId) == MAXULONG)
        {
            DXGKRNL_ERR("DxgkpValidateRecommendedTopology: recommended path %Iu names "
                        "source %u target %u, which this VidPN (%lu sources, %lu "
                        "targets) does not describe\n",
                        Index,
                        VidPn->Paths[Index].VidPnSourceId,
                        VidPn->Paths[Index].VidPnTargetId,
                        VidPn->NumSources, VidPn->NumTargets);
            return STATUS_GRAPHICS_INVALID_VIDPN_TOPOLOGY;
        }
        for (Other = 0; Other < Index; Other++)
        {
            if (VidPn->Paths[Other].VidPnTargetId != VidPn->Paths[Index].VidPnTargetId)
                continue;
            DXGKRNL_ERR("DxgkpValidateRecommendedTopology: recommended paths %Iu and "
                        "%Iu both drive target %u\n",
                        Other, Index, VidPn->Paths[Index].VidPnTargetId);
            return STATUS_GRAPHICS_INVALID_VIDPN_TOPOLOGY;
        }
    }
    return STATUS_SUCCESS;
}

/*
 * Ask the miniport for a topology when no functional VidPn could be had.
 *
 * These are two different questions and Windows asks both.  A functional VidPn
 * is a complete configuration -- sources, targets, paths and modes.  A topology
 * is only the wiring: which source drives which target.  A driver that cannot
 * produce the former may still know the latter, and on a display-only or
 * headless adapter that is the usual case.
 *
 * The reason code tells the driver *why* it is being asked, which changes what
 * a sensible answer is: at initialization with no last-known-good configuration
 * to fall back on, a driver should offer something rather than nothing.
 *
 * A refusal is not a failure.  STATUS_GRAPHICS_NO_RECOMMENDED_VIDPN_TOPOLOGY is
 * the driver saying it has no opinion, and the caller's own default path stands.
 */
/*
 * Tell the miniport that an *already active* path's attributes changed.
 *
 * This is not the same event as committing a VidPn, and that is why it is a
 * separate DDI.  A commit says "this is the new configuration"; this says "the
 * configuration is unchanged, but the rotation or scaling on a path you are
 * already driving is different now".  A driver that only ever saw commits would
 * keep scanning out with the previous transformation, which is visible on
 * screen and reported by nothing.
 *
 * Only called when something actually differs -- announcing an unchanged path
 * would have a driver reprogram hardware for no reason on every commit.
 */
static VOID
DxgkpNotifyActivePathChanged(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _In_ CONST D3DKMDT_VIDPN_PRESENT_PATH *OldPath,
    _In_ CONST D3DKMDT_VIDPN_PRESENT_PATH *NewPath)
{
    PDXGKDDI_UPDATE_ACTIVE_VIDPN_PRESENT_PATH UpdatePath =
        DXGK_CB_FULL(Adapter, DxgkDdiUpdateActiveVidPnPresentPath);
    DXGKARG_UPDATEACTIVEVIDPNPRESENTPATH UpdateArgs;
    NTSTATUS Status = STATUS_UNSUCCESSFUL;

    if (UpdatePath == NULL)
        return;
    if (OldPath->VidPnSourceId != NewPath->VidPnSourceId ||
        OldPath->VidPnTargetId != NewPath->VidPnTargetId)
    {
        return;
    }
    if (OldPath->ContentTransformation.Scaling == NewPath->ContentTransformation.Scaling &&
        OldPath->ContentTransformation.Rotation == NewPath->ContentTransformation.Rotation &&
        OldPath->Content == NewPath->Content)
    {
        return;
    }

    RtlZeroMemory(&UpdateArgs, sizeof(UpdateArgs));
    UpdateArgs.VidPnPresentPathInfo = *NewPath;

    if (!DxgkAcquireKmdCall(Adapter))
        return;
    _SEH2_TRY
    {
        Status = UpdatePath(Adapter->MiniportDeviceContext, &UpdateArgs);
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Status = _SEH2_GetExceptionCode();
    }
    _SEH2_END;
    DxgkReleaseKmdCall(Adapter);

    /*
     * Reported, not propagated.  The configuration is already committed and the
     * driver is already scanning it out; failing the commit here would roll back
     * a display that is working, to fix an attribute that is merely stale.
     */
    if (!NT_SUCCESS(Status))
    {
        DXGKRNL_WARN("UpdateActiveVidPnPresentPath refused 0x%08lX for source %u target %u\n",
                     Status, NewPath->VidPnSourceId, NewPath->VidPnTargetId);
    }
}

static NTSTATUS
DxgkpRecommendTopologyFallback(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _In_ PDXGKP_VIDPN VidPn,
    _In_ PDXGKP_HOTPLUG_MONITOR_SNAPSHOT Snapshot,
    _In_ DXGK_RECOMMENDVIDPNTOPOLOGY_REASON Reason)
{
    PDXGKDDI_RECOMMEND_VIDPN_TOPOLOGY RecommendTopology =
        DXGK_CB_FULL(Adapter, DxgkDdiRecommendVidPnTopology);
    DXGKARG_RECOMMENDVIDPNTOPOLOGY TopologyArgs;
    NTSTATUS Status = STATUS_UNSUCCESSFUL;

    if (RecommendTopology == NULL || !Snapshot->Connected)
        return STATUS_SUCCESS;

    RtlZeroMemory(&TopologyArgs, sizeof(TopologyArgs));
    TopologyArgs.hVidPn = (D3DKMDT_HVIDPN)VidPn;
    TopologyArgs.VidPnSourceId = 0;
    TopologyArgs.RequestReason = Reason;
    TopologyArgs.hFallbackTopology = NULL;

    if (!DxgkAcquireKmdCall(Adapter))
        return STATUS_DELETE_PENDING;
    _SEH2_TRY
    {
        Status = RecommendTopology(Adapter->MiniportDeviceContext, &TopologyArgs);
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Status = _SEH2_GetExceptionCode();
    }
    _SEH2_END;
    DxgkReleaseKmdCall(Adapter);

    if (Status == STATUS_GRAPHICS_NO_RECOMMENDED_VIDPN_TOPOLOGY)
    {
        DXGKRNL_TRACE("RecommendVidPnTopology has no opinion for target %u\n", Snapshot->TargetId);
        return STATUS_SUCCESS;
    }
    if (!NT_SUCCESS(Status))
    {
        DXGKRNL_ERR("DxgkDdiRecommendVidPnTopology failed 0x%08lX for target %u "
                    "(reason %d); the rebuild has no topology to commit\n",
                    Status, Snapshot->TargetId, Reason);
        return Status;
    }

    /* The driver wrote into our VidPn.  Whatever it built has to still be a
     * VidPn this adapter can drive, or the negotiation continues on something
     * malformed and fails much later with a less useful status. */
    return DxgkpValidateRecommendedTopology(VidPn);
}

static NTSTATUS
DxgkpRecommendHotPlugCandidate(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _In_ PDXGKP_VIDPN VidPn,
    _In_ PDXGKP_HOTPLUG_MONITOR_SNAPSHOT Snapshot)
{
    PDXGKDDI_RECOMMEND_FUNCTIONAL_VIDPN RecommendFunctionalVidPn = DXGK_CB(Adapter, DxgkDdiRecommendFunctionalVidPn);
    DXGKARG_RECOMMENDFUNCTIONALVIDPN RecommendArgs;
    D3DDDI_VIDEO_PRESENT_TARGET_ID Targets[DXGKP_MAX_PATHS];
    ULONG TargetCount = 0;
    ULONG Extra;
    NTSTATUS Status = STATUS_UNSUCCESSFUL;

    /* The desktop output first, then the others in connection order. */
    if (Snapshot->Connected)
    {
        Targets[TargetCount++] = Snapshot->TargetId;
        for (Extra = 0; Extra < Snapshot->ExtraCount && TargetCount < RTL_NUMBER_OF(Targets); Extra++)
            Targets[TargetCount++] = Snapshot->Extra[Extra].ChildUid;
    }

    /*
     * Offer the driver its chance to add monitor modes first.  A monitor's mode
     * set starts from what its EDID advertises, and a driver often knows modes
     * the EDID does not list -- scaled, interlaced, or panel-native timings it
     * can drive.  Windows asks before building the functional VidPn for exactly
     * that reason, so a driver that only implements this DDI is not silently
     * limited to its EDID.  A refusal costs nothing: the EDID modes stand.
     */
    for (Extra = 0; Extra < TargetCount && DXGK_CB(Adapter, DxgkDdiRecommendMonitorModes) != NULL; Extra++)
    {
        ULONG Slot = DxgkVidPnTargetIndexFromId(VidPn, Targets[Extra]);
        DXGKARG_RECOMMENDMONITORMODES MonitorArgs;
        NTSTATUS MonitorStatus;

        if (Slot == MAXULONG || VidPn->MonitorModeSets[Slot] == NULL)
            continue;
        RtlZeroMemory(&MonitorArgs, sizeof(MonitorArgs));
        MonitorArgs.VideoPresentTargetId = Targets[Extra];
        MonitorArgs.hMonitorSourceModeSet = (D3DKMDT_HMONITORSOURCEMODESET)VidPn->MonitorModeSets[Slot];
        MonitorArgs.pMonitorSourceModeSetInterface = &g_MonitorSourceModeSetInterface;
        if (!DxgkAcquireKmdCall(Adapter))
            break;
        _SEH2_TRY
        {
            MonitorStatus = DXGK_CB(Adapter, DxgkDdiRecommendMonitorModes)(Adapter->MiniportDeviceContext, &MonitorArgs);
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            MonitorStatus = _SEH2_GetExceptionCode();
        }
        _SEH2_END;
        DxgkReleaseKmdCall(Adapter);
        if (!NT_SUCCESS(MonitorStatus))
            DXGKRNL_TRACE("RecommendMonitorModes declined 0x%08lX for target %u\n",
                          MonitorStatus, Targets[Extra]);
    }

    if (RecommendFunctionalVidPn == NULL)
        return DxgkpRecommendTopologyFallback(Adapter, VidPn, Snapshot,
                                              DXGK_RVT_INITIALIZATION_NOLKG);
    RtlZeroMemory(&RecommendArgs, sizeof(RecommendArgs));
    RecommendArgs.NumberOfVidPnTargets = TargetCount;
    RecommendArgs.pVidPnTargetPrioritizationVector = TargetCount != 0 ? Targets : NULL;
    RecommendArgs.hRecommendedFunctionalVidPn = (D3DKMDT_HVIDPN)VidPn;
    RecommendArgs.RequestReason = DXGK_RFVR_HOTKEY;
    if (!DxgkAcquireKmdCall(Adapter))
        return STATUS_DELETE_PENDING;
    _SEH2_TRY
    {
        Status = RecommendFunctionalVidPn(Adapter->MiniportDeviceContext, &RecommendArgs);
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Status = _SEH2_GetExceptionCode();
    }
    _SEH2_END;
    DxgkReleaseKmdCall(Adapter);
    if (Status == STATUS_GRAPHICS_NO_RECOMMENDED_FUNCTIONAL_VIDPN)
    {
        /* No complete configuration on offer.  The driver may still know the
         * wiring, which is a smaller question and often answerable when the
         * larger one is not. */
        return DxgkpRecommendTopologyFallback(Adapter, VidPn, Snapshot,
                                              DXGK_RVT_AUGMENTATION_NOLKG);
    }
    if (!NT_SUCCESS(Status))
    {
        DXGKRNL_ERR("DxgkDdiRecommendFunctionalVidPn failed 0x%08lX for target %u; "
                    "the rebuild has no topology to commit\n",
                    Status, Snapshot->TargetId);
        return Status;
    }
    return DxgkpValidateRecommendedTopology(VidPn);
}

/*
 * DxgkVidPnQueryFirmwareTiming
 *
 * Called at start, before the first commit, when firmware left a display
 * running.  Asks the driver with DXGK_RFVR_FIRMWARE to describe the topology
 * and timing it inherited, and records the target and exact timing.  The
 * first commit pins that timing out of the driver's own enumerated modes,
 * and only then asks the driver to preserve the inherited configuration.
 *
 * DXGK_RFVR_FIRMWARE carries no version gate in the WDK, so a driver that
 * does not know it is expected to refuse; any refusal simply leaves nothing
 * recorded, and the first commit programs the display afresh.
 */
VOID
DxgkVidPnQueryFirmwareTiming(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _In_ D3DKMDT_HVIDPN hVidPn)
{
    PDXGKDDI_RECOMMEND_FUNCTIONAL_VIDPN RecommendFunctionalVidPn;
    DXGKARG_RECOMMENDFUNCTIONALVIDPN RecommendArgs;
    D3DKMDT_HVIDPN Scratch = NULL;
    PDXGKP_VIDPN ScratchObject;
    PDXGKP_VIDPN_TARGET_MODESET TargetSet;
    D3DDDI_VIDEO_PRESENT_TARGET_ID TargetId;
    ULONG TargetIndex;
    NTSTATUS Status;
    SIZE_T i;

    PAGED_CODE();

    Adapter->FirmwareTimingValid = FALSE;

    if (Adapter->MiniportContext == NULL ||
        Adapter->PostDisplayWidth == 0 || Adapter->PostDisplayHeight == 0)
    {
        return;
    }
    RecommendFunctionalVidPn = DXGK_CB(Adapter, DxgkDdiRecommendFunctionalVidPn);
    if (RecommendFunctionalVidPn == NULL)
        return;

    Status = DxgkVidPnClone(hVidPn, &Scratch);
    if (!NT_SUCCESS(Status))
        return;
    ScratchObject = (PDXGKP_VIDPN)Scratch;
    DxgkpPrepareVidPnForRecommendation(ScratchObject);

    RtlZeroMemory(&RecommendArgs, sizeof(RecommendArgs));
    RecommendArgs.hRecommendedFunctionalVidPn = Scratch;
    RecommendArgs.RequestReason = DXGK_RFVR_FIRMWARE;

    Status = STATUS_DELETE_PENDING;
    if (DxgkAcquireKmdCall(Adapter))
    {
        _SEH2_TRY
        {
            Status = RecommendFunctionalVidPn(Adapter->MiniportDeviceContext,
                                              &RecommendArgs);
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            Status = _SEH2_GetExceptionCode();
        }
        _SEH2_END;
        DxgkReleaseKmdCall(Adapter);
    }

    if (NT_SUCCESS(Status))
        Status = DxgkpValidateRecommendedTopology(ScratchObject);
    if (!NT_SUCCESS(Status) || ScratchObject->NumPaths == 0)
    {
        DXGKRNL_TRACE("DxgkVidPnQueryFirmwareTiming: driver did not describe the "
                      "firmware configuration (0x%08lX); nothing to preserve\n",
                      Status);
        goto Done;
    }

    TargetId = ScratchObject->Paths[0].VidPnTargetId;
    TargetIndex = DxgkVidPnTargetIndexFromId(ScratchObject, TargetId);
    TargetSet = TargetIndex != MAXULONG ?
                    ScratchObject->TargetModeSets[TargetIndex] : NULL;
    if (TargetSet == NULL || TargetSet->PinnedModeId == (UINT)-1)
    {
        DXGKRNL_TRACE("DxgkVidPnQueryFirmwareTiming: firmware path to target %u "
                      "names no pinned timing; nothing to preserve\n", TargetId);
        goto Done;
    }
    for (i = 0; i < TargetSet->NumModes; i++)
    {
        if (TargetSet->Modes[i].Id == TargetSet->PinnedModeId)
        {
            Adapter->FirmwareTargetId = TargetId;
            Adapter->FirmwareTargetTiming = TargetSet->Modes[i].VideoSignalInfo;
            Adapter->FirmwareTimingValid = TRUE;
            DXGKRNL_TRACE("DxgkVidPnQueryFirmwareTiming: firmware drives target "
                          "%u at %ux%u\n", TargetId,
                          Adapter->FirmwareTargetTiming.ActiveSize.cx,
                          Adapter->FirmwareTargetTiming.ActiveSize.cy);
            break;
        }
    }

Done:
    DxgkVidPnDestroy(Scratch);
}

static NTSTATUS
DxgkpRecoverFailedHotPlugRollback(
    _In_ PDXGKRNL_ADAPTER Adapter)
{
    InterlockedExchange(&Adapter->TdrOwnershipUncertain, 1);
    DXGKRNL_ERR("DxgkpRecoverFailedHotPlugRollback: private TDR recovery is quarantined; failing adapter closed\n");
    DxgkBeginAdapterRundown(Adapter);
    return STATUS_NOT_SUPPORTED;
}

static NTSTATUS
DxgkpVidPnRebuildForHotPlugGeneration(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _In_ LONG64 ExpectedGeneration)
{
    PDXGKP_HOTPLUG_MONITOR_SNAPSHOT Snapshot = NULL;
    D3DKMDT_HVIDPN OldVidPn = NULL;
    D3DKMDT_HVIDPN Candidate = NULL;
    D3DKMDT_HVIDPN DetachedVidPn = NULL;
    PDXGKP_VIDPN CandidateObject;
    DXGKP_DISPLAY_COMMIT_RESULT CommitResult;
    DXGKP_DISPLAY_COMMIT_RESULT RollbackResult;
    ULONG OldCommittedWidth;
    ULONG OldCommittedHeight;
    D3DKMDT_VIDPN_PRESENT_PATH *OldPaths;
    D3DKMDT_VIDPN_PRESENT_PATH *NewPaths;
    ULONG ComparablePaths = 0;
    BOOLEAN KmdTransaction = FALSE;
    BOOLEAN RecoveryRequired = FALSE;
    PDXGK_CHILD_PDO_EXTENSION MatchingChild = NULL;
    KIRQL ChildOldIrql;
    BOOLEAN NotifyMonitorEvent = FALSE;
    NTSTATUS Status;

    PAGED_CODE();

    if (Adapter == NULL)
        return STATUS_INVALID_PARAMETER;
    if (KeGetCurrentIrql() != PASSIVE_LEVEL)
        return STATUS_INVALID_DEVICE_STATE;
    /* WDDM 2.3 replaced DxgkDdiCommitVidPn with DxgkDdiSetTimingsFromVidPn;
     * a miniport that implements either can take the rebuilt topology. */
    if (DXGK_CB(Adapter, DxgkDdiCommitVidPn) == NULL &&
        DXGK_CB_FULL(Adapter, DxgkDdiSetTimingsFromVidPn) == NULL)
    {
        return STATUS_NOT_SUPPORTED;
    }
    Snapshot = ExAllocatePoolWithTag(PagedPool, sizeof(*Snapshot), TAG_DXGK_VIDPN);
    if (Snapshot == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;
    OldPaths = ExAllocatePoolWithTag(NonPagedPool, 2 * DXGKP_MAX_PATHS * sizeof(*OldPaths), TAG_DXGK_VIDPN);
    if (OldPaths == NULL)
    {
        ExFreePoolWithTag(Snapshot, TAG_DXGK_VIDPN);
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    NewPaths = OldPaths + DXGKP_MAX_PATHS;
    (VOID)KeWaitForSingleObject(&Adapter->SharedPrimaryMutex, Executive, KernelMode, FALSE, NULL);
    DxgkpBeginSharedSurfaceMutationLocked(Adapter);
    if (!DxgkBeginKmdTransaction(Adapter))
    {
        Status = STATUS_DELETE_PENDING;
        goto Cleanup;
    }
    KmdTransaction = TRUE;
    (VOID)KeWaitForSingleObject(&Adapter->VidPnMutex, Executive, KernelMode, FALSE, NULL);
    OldVidPn = (D3DKMDT_HVIDPN)Adapter->VidPn;
    if (!DxgkVidPnReference(OldVidPn))
        OldVidPn = NULL;
    OldCommittedWidth = Adapter->CommittedWidth;
    OldCommittedHeight = Adapter->CommittedHeight;
    KeReleaseMutex(&Adapter->VidPnMutex, FALSE);
    if (OldVidPn == NULL)
    {
        Status = STATUS_INVALID_DEVICE_STATE;
        goto Cleanup;
    }
    DxgkpBindVidPnTargetsToChildren(Adapter, (PDXGKP_VIDPN)OldVidPn);
    DxgkpRefreshConnectedChildEdids(Adapter);
    Status = DxgkpSnapshotHotPlugMonitor(Adapter, (PDXGKP_VIDPN)OldVidPn, ExpectedGeneration, Snapshot);
    if (!NT_SUCCESS(Status))
        goto Cleanup;
    Status = DxgkpRefreshHotPlugEdid(Adapter, Snapshot);
    if (!NT_SUCCESS(Status))
        goto Cleanup;
    Status = DxgkVidPnClone(OldVidPn, &Candidate);
    if (!NT_SUCCESS(Status))
        goto Cleanup;
    CandidateObject = (PDXGKP_VIDPN)Candidate;
    Status = DxgkpBuildHotPlugCandidate(CandidateObject, Snapshot);
    if (!NT_SUCCESS(Status))
        goto Cleanup;
    Status = DxgkpRecommendHotPlugCandidate(Adapter, CandidateObject, Snapshot);
    if (!NT_SUCCESS(Status))
        goto Cleanup;
    /* Only now, once the driver has had the empty topology it is entitled to
     * and declined to fill it, does dxgkrnl wire the output itself. */
    Status = DxgkpSeedDefaultHotPlugPath(Adapter, CandidateObject, Snapshot);
    if (!NT_SUCCESS(Status))
        goto Cleanup;
    KeAcquireSpinLock(&Adapter->ChildListLock, &ChildOldIrql);
    if (!DxgkpHotPlugSnapshotCurrentLocked(Adapter, Snapshot, ExpectedGeneration, &MatchingChild))
    {
        KeReleaseSpinLock(&Adapter->ChildListLock, ChildOldIrql);
        /* Retire the stale snapshot before making a mode-set DDI call. The
         * worker retries STATUS_RETRY against the current generation. */
        Status = STATUS_RETRY;
        goto Cleanup;
    }
    KeReleaseSpinLock(&Adapter->ChildListLock, ChildOldIrql);
    Status = DxgkpDisplayCommitVidPnCandidate(Adapter, Candidate, &CommitResult);
    if (!NT_SUCCESS(Status) && CandidateObject->NumPaths > 1 &&
        Status != STATUS_DELETE_PENDING && Status != STATUS_DEVICE_REMOVED)
    {
        /* The miniport cannot drive every output this way (a clone needs
         * cofunctional timings on all of them).  The desktop output alone
         * is still a correct configuration. */
        DXGKRNL_WARN("DxgkVidPnRebuildForHotPlug: %Iu-path topology refused 0x%08lX; "
                     "driving the desktop output alone\n", CandidateObject->NumPaths, Status);
        CandidateObject->NumPaths = 1;
        Status = DxgkpDisplayCommitVidPnCandidate(Adapter, Candidate, &CommitResult);
    }
    if (!NT_SUCCESS(Status))
        goto Cleanup;
    (VOID)KeWaitForSingleObject(&Adapter->VidPnMutex, Executive, KernelMode, FALSE, NULL);
    KeAcquireSpinLock(&Adapter->ChildListLock, &ChildOldIrql);
    MatchingChild = NULL;
    if ((D3DKMDT_HVIDPN)Adapter->VidPn != OldVidPn || !DxgkpHotPlugSnapshotCurrentLocked(Adapter, Snapshot, ExpectedGeneration, &MatchingChild))
    {
        KeReleaseSpinLock(&Adapter->ChildListLock, ChildOldIrql);
        KeReleaseMutex(&Adapter->VidPnMutex, FALSE);
        Status = DxgkpDisplayCommitVidPnCandidate(Adapter, OldVidPn, &RollbackResult);
        if (NT_SUCCESS(Status))
            Status = STATUS_RETRY;
        else
        {
            NTSTATUS RollbackStatus = Status;

            (VOID)KeWaitForSingleObject(&Adapter->VidPnMutex, Executive, KernelMode, FALSE, NULL);
            Adapter->VidPnCommitted = FALSE;
            DxgkVidPnPublishVsyncTargetMap(Adapter, NULL);
            KeReleaseMutex(&Adapter->VidPnMutex, FALSE);
            InterlockedExchange(&Adapter->TdrOwnershipUncertain, 1);
            RecoveryRequired = TRUE;
            Status = RollbackStatus;
        }
        goto Cleanup;
    }
    DetachedVidPn = (D3DKMDT_HVIDPN)Adapter->VidPn;
    /* Capture the outgoing path before it is replaced, so the comparison below
     * has something to compare against. */
    {
        PDXGKP_VIDPN OutgoingVidPn = (PDXGKP_VIDPN)Adapter->VidPn;
        SIZE_T OldIndex, NewIndex;

        /* Paths that drive the same output from the same source before and
         * after; a changed transformation on one is announced below. */
        for (NewIndex = 0; OutgoingVidPn != NULL && CandidateObject != NULL &&
                           NewIndex < CandidateObject->NumPaths; NewIndex++)
        {
            for (OldIndex = 0; OldIndex < OutgoingVidPn->NumPaths; OldIndex++)
            {
                if (OutgoingVidPn->Paths[OldIndex].VidPnTargetId == CandidateObject->Paths[NewIndex].VidPnTargetId &&
                    OutgoingVidPn->Paths[OldIndex].VidPnSourceId == CandidateObject->Paths[NewIndex].VidPnSourceId)
                {
                    OldPaths[ComparablePaths] = OutgoingVidPn->Paths[OldIndex];
                    NewPaths[ComparablePaths] = CandidateObject->Paths[NewIndex];
                    ComparablePaths++;
                    break;
                }
            }
        }
    }
    Adapter->VidPn = Candidate;
    Adapter->CommittedWidth = CommitResult.CommittedWidth;
    Adapter->CommittedHeight = CommitResult.CommittedHeight;
    Adapter->VidPnCommitted = CommitResult.VidPnCommitted;
    Adapter->HeadlessDesktop = CommitResult.HeadlessDesktop;
    DxgkVidPnPublishVsyncTargetMap(Adapter, CommitResult.VidPnCommitted ? Candidate : NULL);
    if (Snapshot->Connected && Snapshot->EdidValid && MatchingChild != NULL)
    {
        RtlCopyMemory(MatchingChild->Edid, Snapshot->Edid, sizeof(MatchingChild->Edid));
        RtlCopyMemory(MatchingChild->EdidExtensions, Snapshot->EdidExtensions,
                      sizeof(MatchingChild->EdidExtensions));
        MatchingChild->EdidExtensionCount = Snapshot->EdidExtensionCount;
        MatchingChild->EdidValid = TRUE;
    }
    Candidate = NULL;
    KeReleaseSpinLock(&Adapter->ChildListLock, ChildOldIrql);
    KeReleaseMutex(&Adapter->VidPnMutex, FALSE);
    DxgkpDisplayPublishInitialMode(Adapter);
    DxgkVidPnDestroyDisplayModeCache(Adapter);
    NotifyMonitorEvent = CommitResult.VidPnCommitted &&
                         (Snapshot->Connected ||
                          CommitResult.CommittedWidth != OldCommittedWidth || CommitResult.CommittedHeight != OldCommittedHeight);
    if (CommitResult.CommittedWidth != OldCommittedWidth || CommitResult.CommittedHeight != OldCommittedHeight ||
        (!Snapshot->Connected && !CommitResult.HeadlessDesktop))
        DxgkpDestroySharedPrimaryLocked(Adapter);
    /* After the new VidPn is published and the VidPn mutex is dropped, but
     * while the KMD transaction still holds the miniport. */
    while (ComparablePaths != 0)
    {
        ComparablePaths--;
        DxgkpNotifyActivePathChanged(Adapter, &OldPaths[ComparablePaths], &NewPaths[ComparablePaths]);
    }
    DXGKRNL_TRACE("DxgkVidPnRebuildForHotPlug: atomically published %p replacing %p connected=%u target=%u mode=%ux%u\n", Adapter->VidPn, DetachedVidPn, Snapshot->Connected, Snapshot->TargetId, CommitResult.CommittedWidth, CommitResult.CommittedHeight);
    Status = STATUS_SUCCESS;

Cleanup:
    if (KmdTransaction)
    {
        DxgkEndKmdTransaction(Adapter);
        KmdTransaction = FALSE;
    }
    if (RecoveryRequired)
    {
        NTSTATUS RecoveryStatus = DxgkpRecoverFailedHotPlugRollback(Adapter);

        Status = NT_SUCCESS(RecoveryStatus) ? STATUS_RETRY : RecoveryStatus;
    }
    DxgkpEndSharedSurfaceMutationLocked(Adapter);
    KeReleaseMutex(&Adapter->SharedPrimaryMutex, FALSE);
    if (DetachedVidPn != NULL)
        DxgkVidPnDestroy(DetachedVidPn);
    if (OldVidPn != NULL)
        DxgkVidPnDestroy(OldVidPn);
    if (Candidate != NULL)
        DxgkVidPnDestroy(Candidate);
    if (NotifyMonitorEvent && NT_SUCCESS(Status))
        DxgkDisplayNotifyMonitorEvent(Adapter);
    ExFreePoolWithTag(OldPaths, TAG_DXGK_VIDPN);
    ExFreePoolWithTag(Snapshot, TAG_DXGK_VIDPN);
    return Status;
}

NTSTATUS
DxgkVidPnRebuildForHotPlug(
    _In_ PDXGKRNL_ADAPTER Adapter)
{
    LONG64 ExpectedGeneration;
    NTSTATUS Status;

    if (Adapter == NULL)
        return STATUS_INVALID_PARAMETER;
    ExpectedGeneration = InterlockedCompareExchange64(&Adapter->HotPlugGeneration, 0, 0);
    Status = DxgkpVidPnRebuildForHotPlugGeneration(Adapter, ExpectedGeneration);
    if (Status == STATUS_RETRY)
        Status = DxgkVidPnQueueHotPlugRebuild(Adapter);
    return Status;
}

/* ========================================================================
 * DxgkCbQueryVidPnInterface
 * ====================================================================== */

NTSTATUS
APIENTRY
DxgkCbQueryVidPnInterface(
    IN_CONST_D3DKMDT_HVIDPN hVidPn,
    IN_CONST_DXGK_VIDPN_INTERFACE_VERSION VidPnInterfaceVersion,
    DEREF_OUT_CONST_PPDXGK_VIDPN_INTERFACE ppVidPnInterface)
{
    PDXGKP_VIDPN VidPn;

    DXGKRNL_TRACE("DxgkCbQueryVidPnInterface: hVidPn=%p version=%d\n",
                  hVidPn, VidPnInterfaceVersion);

    if (ppVidPnInterface == NULL)
        return STATUS_INVALID_PARAMETER;

    *ppVidPnInterface = NULL;

    VidPn = DxgkpVidPnFromHandle(hVidPn);
    if (VidPn == NULL)
    {
        DXGKRNL_ERR("DxgkCbQueryVidPnInterface: invalid hVidPn %p\n", hVidPn);
        return STATUS_GRAPHICS_INVALID_VIDPN;
    }

    switch (VidPnInterfaceVersion)
    {
        case DXGK_VIDPN_INTERFACE_VERSION_V1:
            *ppVidPnInterface = &g_VidPnInterfaceV1;
            return STATUS_SUCCESS;

#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_9)
        case DXGK_VIDPN_INTERFACE_VERSION_V2:
            *ppVidPnInterface = &g_VidPnInterfaceV2;
            return STATUS_SUCCESS;
#endif

        default:
            DXGKRNL_WARN("DxgkCbQueryVidPnInterface: unsupported version %d\n",
                         VidPnInterfaceVersion);
            return STATUS_NOT_SUPPORTED;
    }
}

NTSTATUS
APIENTRY
DxgkCbQueryMonitorInterface(
    IN_CONST_HANDLE hAdapter,
    IN_CONST_DXGK_MONITOR_INTERFACE_VERSION MonitorInterfaceVersion,
    DEREF_OUT_CONST_PPDXGK_MONITOR_INTERFACE ppMonitorInterface)
{
    DXGKRNL_TRACE("DxgkCbQueryMonitorInterface: hAdapter=%p version=%u\n",
                  hAdapter, MonitorInterfaceVersion);

    if (ppMonitorInterface == NULL)
        return STATUS_INVALID_PARAMETER;

    *ppMonitorInterface = NULL;

    if (hAdapter == NULL)
    {
        DXGKRNL_ERR("DxgkCbQueryMonitorInterface: NULL hAdapter\n");
        return STATUS_INVALID_PARAMETER;
    }

    switch (MonitorInterfaceVersion)
    {
        case DXGK_MONITOR_INTERFACE_VERSION_V1:
            *ppMonitorInterface = &g_MonitorInterfaceV1;
            return STATUS_SUCCESS;

        case DXGK_MONITOR_INTERFACE_VERSION_V2:
            *ppMonitorInterface =
                (CONST DXGK_MONITOR_INTERFACE *)&g_MonitorInterfaceV2;
            return STATUS_SUCCESS;

        default:
            DXGKRNL_WARN("DxgkCbQueryMonitorInterface: unsupported version %u\n",
                         MonitorInterfaceVersion);
            return STATUS_NOT_SUPPORTED;
    }
}

/* ========================================================================
 * Top-level VidPN interface (DXGK_VIDPN_INTERFACE) implementations
 * ====================================================================== */

static NTSTATUS APIENTRY
VidPn_GetTopology(
    _In_  D3DKMDT_HVIDPN                              hVidPn,
    _Out_ D3DKMDT_HVIDPNTOPOLOGY*                     phVidPnTopology,
    _Out_ CONST DXGK_VIDPNTOPOLOGY_INTERFACE**        ppVidPnTopologyInterface)
{
    PDXGKP_VIDPN VidPn;

    if (phVidPnTopology == NULL || ppVidPnTopologyInterface == NULL)
        return STATUS_INVALID_PARAMETER;

    VidPn = DxgkpVidPnFromHandle(hVidPn);
    if (VidPn == NULL)
        return STATUS_INVALID_PARAMETER;

    *phVidPnTopology           = (D3DKMDT_HVIDPNTOPOLOGY)VidPn;
    *ppVidPnTopologyInterface  = &g_VidPnTopologyInterface;
    return STATUS_SUCCESS;
}

static NTSTATUS APIENTRY
VidPn_AcquireSourceModeSet(
    _In_  D3DKMDT_HVIDPN                              hVidPn,
    _In_  D3DDDI_VIDEO_PRESENT_SOURCE_ID               VidPnSourceId,
    _Out_ D3DKMDT_HVIDPNSOURCEMODESET*                phVidPnSourceModeSet,
    _Out_ CONST DXGK_VIDPNSOURCEMODESET_INTERFACE**   ppVidPnSourceModeSetInterface)
{
    PDXGKP_VIDPN VidPn;

    if (phVidPnSourceModeSet == NULL || ppVidPnSourceModeSetInterface == NULL)
        return STATUS_INVALID_PARAMETER;

    VidPn = DxgkpVidPnFromHandle(hVidPn);
    if (VidPn == NULL)
        return STATUS_INVALID_PARAMETER;

    if (VidPnSourceId >= VidPn->NumSources || VidPn->SourceModeSets[VidPnSourceId] == NULL)
    {
        DXGKRNL_WARN("VidPn_AcquireSourceModeSet: invalid source %u (max %lu)\n",
                     VidPnSourceId, VidPn->NumSources);
        return STATUS_GRAPHICS_INVALID_VIDEO_PRESENT_SOURCE;
    }

    InterlockedIncrement(&VidPn->SourceModeSets[VidPnSourceId]->References);
    *phVidPnSourceModeSet          = (D3DKMDT_HVIDPNSOURCEMODESET)VidPn->SourceModeSets[VidPnSourceId];
    *ppVidPnSourceModeSetInterface = &g_VidPnSourceModeSetInterface;
    return STATUS_SUCCESS;
}

static NTSTATUS APIENTRY
VidPn_ReleaseSourceModeSet(
    _In_ D3DKMDT_HVIDPN                               hVidPn,
    _In_ D3DKMDT_HVIDPNSOURCEMODESET                  hVidPnSourceModeSet)
{
    PDXGKP_VIDPN_SOURCE_MODESET ModeSet = DxgkpSourceModeSetFromHandle(hVidPnSourceModeSet);
    PDXGKP_VIDPN VidPn = DxgkpVidPnFromHandle(hVidPn);

    if (VidPn == NULL || ModeSet == NULL || ModeSet->Owner != VidPn)
        return STATUS_INVALID_PARAMETER;
    if (InterlockedDecrement(&ModeSet->References) > 0)
        return STATUS_SUCCESS;
    InterlockedExchange(&ModeSet->References, 0);
    if (ModeSet->SourceId >= DXGKP_MAX_SOURCES || VidPn->SourceModeSets[ModeSet->SourceId] != ModeSet)
        ExFreePoolWithTag(ModeSet, TAG_DXGK_MODESET);
    return STATUS_SUCCESS;
}

static NTSTATUS APIENTRY
VidPn_CreateNewSourceModeSet(
    _In_  D3DKMDT_HVIDPN                              hVidPn,
    _In_  D3DDDI_VIDEO_PRESENT_SOURCE_ID               VidPnSourceId,
    _Out_ D3DKMDT_HVIDPNSOURCEMODESET*                phVidPnSourceModeSet,
    _Out_ CONST DXGK_VIDPNSOURCEMODESET_INTERFACE**   ppVidPnSourceModeSetInterface)
{
    PDXGKP_VIDPN VidPn;
    PDXGKP_VIDPN_SOURCE_MODESET ModeSet;

    if (phVidPnSourceModeSet == NULL || ppVidPnSourceModeSetInterface == NULL)
        return STATUS_INVALID_PARAMETER;

    VidPn = DxgkpVidPnFromHandle(hVidPn);
    if (VidPn == NULL)
        return STATUS_INVALID_PARAMETER;

    if (VidPnSourceId >= VidPn->NumSources)
        return STATUS_GRAPHICS_INVALID_VIDEO_PRESENT_SOURCE;

    ModeSet = DxgkpAllocateSourceModeSet(VidPn, VidPnSourceId);
    if (ModeSet == NULL)
        return STATUS_NO_MEMORY;
    ModeSet->References = 1;

    *phVidPnSourceModeSet          = (D3DKMDT_HVIDPNSOURCEMODESET)ModeSet;
    *ppVidPnSourceModeSetInterface = &g_VidPnSourceModeSetInterface;
    return STATUS_SUCCESS;
}

/*
 * Pinned-mode preservation for mode-set replacement.
 *
 * When a miniport replaces a source or target mode set, the mode pinned in
 * the old set must still be offered by the new one; otherwise the
 * replacement would silently drop a constraint the VidPN was built around.
 * Assignment fails with STATUS_GRAPHICS_PINNED_MODE_MUST_REMAIN_IN_SET, and
 * the caller keeps ownership of the rejected set and releases it.
 *
 * Mode ids are per-set, so "the same mode" means the same format or timing,
 * never the same id.  Fields are compared one by one: these structures carry
 * padding (D3DKMDT_VIDEO_SIGNAL_INFO has 4 bytes before PixelRate on 64-bit)
 * that a byte compare would read.
 */
static BOOLEAN
DxgkpSourceModesMatch(
    _In_ CONST D3DKMDT_VIDPN_SOURCE_MODE *A,
    _In_ CONST D3DKMDT_VIDPN_SOURCE_MODE *B)
{
    if (A->Type != B->Type)
        return FALSE;

    if (A->Type == D3DKMDT_RMT_GRAPHICS)
    {
        CONST D3DKMDT_GRAPHICS_RENDERING_FORMAT *Ga = &A->Format.Graphics;
        CONST D3DKMDT_GRAPHICS_RENDERING_FORMAT *Gb = &B->Format.Graphics;

        return Ga->PrimSurfSize.cx      == Gb->PrimSurfSize.cx &&
               Ga->PrimSurfSize.cy      == Gb->PrimSurfSize.cy &&
               Ga->VisibleRegionSize.cx == Gb->VisibleRegionSize.cx &&
               Ga->VisibleRegionSize.cy == Gb->VisibleRegionSize.cy &&
               Ga->Stride               == Gb->Stride &&
               Ga->PixelFormat          == Gb->PixelFormat &&
               Ga->ColorBasis           == Gb->ColorBasis &&
               Ga->PixelValueAccessMode == Gb->PixelValueAccessMode;
    }

    /* Text modes are rare and carry no padding-sensitive members. */
    return RtlCompareMemory(&A->Format, &B->Format, sizeof(A->Format)) ==
           sizeof(A->Format);
}

static BOOLEAN
DxgkpTargetModesMatch(
    _In_ CONST D3DKMDT_VIDPN_TARGET_MODE *A,
    _In_ CONST D3DKMDT_VIDPN_TARGET_MODE *B)
{
    CONST D3DKMDT_VIDEO_SIGNAL_INFO *Sa = &A->VideoSignalInfo;
    CONST D3DKMDT_VIDEO_SIGNAL_INFO *Sb = &B->VideoSignalInfo;

    /* ScanLineOrdering is read as the whole union word, so on WDDM 1.3+ it
     * also covers VSyncFreqDivider -- a different divider is a different
     * effective refresh rate, hence a different mode. */
    return Sa->VideoStandard         == Sb->VideoStandard &&
           Sa->TotalSize.cx          == Sb->TotalSize.cx &&
           Sa->TotalSize.cy          == Sb->TotalSize.cy &&
           Sa->ActiveSize.cx         == Sb->ActiveSize.cx &&
           Sa->ActiveSize.cy         == Sb->ActiveSize.cy &&
           Sa->VSyncFreq.Numerator   == Sb->VSyncFreq.Numerator &&
           Sa->VSyncFreq.Denominator == Sb->VSyncFreq.Denominator &&
           Sa->HSyncFreq.Numerator   == Sb->HSyncFreq.Numerator &&
           Sa->HSyncFreq.Denominator == Sb->HSyncFreq.Denominator &&
           Sa->PixelRate             == Sb->PixelRate &&
           Sa->ScanLineOrdering      == Sb->ScanLineOrdering;
}

/*
 * Checks that the old set's pinned source mode survives in the new set, and
 * carries the pin across when the new set has none of its own.
 */
static NTSTATUS
DxgkpCarrySourcePin(
    _In_    CONST DXGKP_VIDPN_SOURCE_MODESET *Old,
    _Inout_ PDXGKP_VIDPN_SOURCE_MODESET       New)
{
    CONST D3DKMDT_VIDPN_SOURCE_MODE *Pinned = NULL;
    SIZE_T i;

    if (Old == NULL || Old->PinnedModeId == (UINT)-1)
        return STATUS_SUCCESS;

    for (i = 0; i < Old->NumModes; i++)
    {
        if (Old->Modes[i].Id == Old->PinnedModeId)
        {
            Pinned = &Old->Modes[i];
            break;
        }
    }
    /* A pin naming no mode in its own set constrains nothing. */
    if (Pinned == NULL)
        return STATUS_SUCCESS;

    for (i = 0; i < New->NumModes; i++)
    {
        if (DxgkpSourceModesMatch(&New->Modes[i], Pinned))
        {
            if (New->PinnedModeId == (UINT)-1)
                New->PinnedModeId = New->Modes[i].Id;
            return STATUS_SUCCESS;
        }
    }
    return STATUS_GRAPHICS_PINNED_MODE_MUST_REMAIN_IN_SET;
}

static NTSTATUS
DxgkpCarryTargetPin(
    _In_    CONST DXGKP_VIDPN_TARGET_MODESET *Old,
    _Inout_ PDXGKP_VIDPN_TARGET_MODESET       New)
{
    CONST D3DKMDT_VIDPN_TARGET_MODE *Pinned = NULL;
    SIZE_T i;

    if (Old == NULL || Old->PinnedModeId == (UINT)-1)
        return STATUS_SUCCESS;

    for (i = 0; i < Old->NumModes; i++)
    {
        if (Old->Modes[i].Id == Old->PinnedModeId)
        {
            Pinned = &Old->Modes[i];
            break;
        }
    }
    if (Pinned == NULL)
        return STATUS_SUCCESS;

    for (i = 0; i < New->NumModes; i++)
    {
        if (DxgkpTargetModesMatch(&New->Modes[i], Pinned))
        {
            if (New->PinnedModeId == (UINT)-1)
                New->PinnedModeId = New->Modes[i].Id;
            return STATUS_SUCCESS;
        }
    }
    return STATUS_GRAPHICS_PINNED_MODE_MUST_REMAIN_IN_SET;
}

static NTSTATUS APIENTRY
VidPn_AssignSourceModeSet(
    _In_ D3DKMDT_HVIDPN                               hVidPn,
    _In_ D3DDDI_VIDEO_PRESENT_SOURCE_ID                VidPnSourceId,
    _In_ D3DKMDT_HVIDPNSOURCEMODESET                  hVidPnSourceModeSet)
{
    PDXGKP_VIDPN VidPn;
    PDXGKP_VIDPN_SOURCE_MODESET ModeSet = DxgkpSourceModeSetFromHandle(hVidPnSourceModeSet);
    PDXGKP_VIDPN_SOURCE_MODESET Old;
    NTSTATUS Status;

    VidPn = DxgkpVidPnFromHandle(hVidPn);
    if (VidPn == NULL || ModeSet == NULL || ModeSet->Owner != VidPn)
        return STATUS_INVALID_PARAMETER;

    if (VidPnSourceId >= VidPn->NumSources || ModeSet->SourceId != VidPnSourceId)
        return STATUS_GRAPHICS_INVALID_VIDEO_PRESENT_SOURCE;

    Old = VidPn->SourceModeSets[VidPnSourceId];
    if (Old == ModeSet)
        return STATUS_SUCCESS;

    /* Reject before touching any state: on failure the caller still owns
     * the new set and releases it through pfnReleaseSourceModeSet. */
    Status = DxgkpCarrySourcePin(Old, ModeSet);
    if (!NT_SUCCESS(Status))
        return Status;

    VidPn->SourceModeSets[VidPnSourceId] = ModeSet;
    InterlockedExchange(&ModeSet->References, 0);
    if (Old != NULL && InterlockedCompareExchange(&Old->References, 0, 0) <= 0)
        ExFreePoolWithTag(Old, TAG_DXGK_MODESET);
    return STATUS_SUCCESS;
}

/*
 * VidPn_AssignMultisamplingMethodSet
 *
 * Records the multisampling methods a miniport reports as supported for one
 * VidPN source.  The set replaces any previously assigned set for that
 * source; a count of zero clears it, meaning the source supports no
 * multisampling at all.
 *
 * Takes NumMethods as its third argument, matching
 * DXGKDDI_VIDPN_ASSIGNMULTISAMPLINGMETHODSET in the Windows WDK.  Omitting it
 * left the method array unreadable, because the count is what bounds it.
 */
static NTSTATUS APIENTRY
VidPn_AssignMultisamplingMethodSet(
    _In_ D3DKMDT_HVIDPN                               hVidPn,
    _In_ D3DDDI_VIDEO_PRESENT_SOURCE_ID                VidPnSourceId,
    _In_ CONST SIZE_T                                  NumMethods,
    _In_reads_(NumMethods) CONST D3DDDI_MULTISAMPLINGMETHOD* pSupportedMethodSet)
{
    PDXGKP_VIDPN VidPn;
    SIZE_T       Index;

    VidPn = DxgkpVidPnFromHandle(hVidPn);
    if (VidPn == NULL)
        return STATUS_INVALID_PARAMETER;

    if (VidPnSourceId >= VidPn->NumSources)
        return STATUS_GRAPHICS_INVALID_VIDEO_PRESENT_SOURCE;

    /* A non-empty set must come with the array that holds it. */
    if (NumMethods != 0 && pSupportedMethodSet == NULL)
        return STATUS_INVALID_PARAMETER;

    if (NumMethods > DXGKP_MAX_MULTISAMPLING_METHODS)
    {
        DXGKRNL_WARN("VidPn_AssignMultisamplingMethodSet: source %lu reported "
                     "%Iu methods, only %u can be recorded\n",
                     VidPnSourceId, NumMethods,
                     DXGKP_MAX_MULTISAMPLING_METHODS);
        return STATUS_INVALID_PARAMETER;
    }

    /* Every method must describe at least one sample, and the quality range
     * starts at 0 and is inclusive, so a level count of 0 is meaningless. */
    for (Index = 0; Index < NumMethods; Index++)
    {
        if (pSupportedMethodSet[Index].NumSamples == 0 ||
            pSupportedMethodSet[Index].NumQualityLevels == 0)
        {
            DXGKRNL_WARN("VidPn_AssignMultisamplingMethodSet: source %lu "
                         "method %Iu invalid (samples=%u quality=%u)\n",
                         VidPnSourceId, Index,
                         pSupportedMethodSet[Index].NumSamples,
                         pSupportedMethodSet[Index].NumQualityLevels);
            return STATUS_INVALID_PARAMETER;
        }
    }

    if (NumMethods != 0)
    {
        RtlCopyMemory(VidPn->MultisamplingMethods[VidPnSourceId],
                      pSupportedMethodSet,
                      NumMethods * sizeof(D3DDDI_MULTISAMPLINGMETHOD));
    }
    RtlZeroMemory(&VidPn->MultisamplingMethods[VidPnSourceId][NumMethods],
                  (DXGKP_MAX_MULTISAMPLING_METHODS - NumMethods) *
                      sizeof(D3DDDI_MULTISAMPLINGMETHOD));

    VidPn->NumMultisamplingMethods[VidPnSourceId]      = NumMethods;
    VidPn->MultisamplingMethodsAssigned[VidPnSourceId] = TRUE;

    DXGKRNL_TRACE("VidPn_AssignMultisamplingMethodSet: source %lu recorded "
                  "%Iu method(s)\n", VidPnSourceId, NumMethods);
    return STATUS_SUCCESS;
}

static NTSTATUS APIENTRY
VidPn_AcquireTargetModeSet(
    _In_  D3DKMDT_HVIDPN                              hVidPn,
    _In_  D3DDDI_VIDEO_PRESENT_TARGET_ID               VidPnTargetId,
    _Out_ D3DKMDT_HVIDPNTARGETMODESET*                phVidPnTargetModeSet,
    _Out_ CONST DXGK_VIDPNTARGETMODESET_INTERFACE**   ppVidPnTargetModeSetInterface)
{
    PDXGKP_VIDPN VidPn;
    ULONG TargetIndex;

    if (phVidPnTargetModeSet == NULL || ppVidPnTargetModeSetInterface == NULL)
        return STATUS_INVALID_PARAMETER;

    VidPn = DxgkpVidPnFromHandle(hVidPn);
    if (VidPn == NULL)
        return STATUS_INVALID_PARAMETER;

    TargetIndex = DxgkVidPnTargetIndexFromId(VidPn, VidPnTargetId);
    if (TargetIndex == MAXULONG || VidPn->TargetModeSets[TargetIndex] == NULL)
    {
        DXGKRNL_WARN("VidPn_AcquireTargetModeSet: invalid target %u (max %lu)\n",
                     VidPnTargetId, VidPn->NumTargets);
        return STATUS_GRAPHICS_INVALID_VIDEO_PRESENT_TARGET;
    }

    InterlockedIncrement(&VidPn->TargetModeSets[TargetIndex]->References);
    *phVidPnTargetModeSet          = (D3DKMDT_HVIDPNTARGETMODESET)VidPn->TargetModeSets[TargetIndex];
    *ppVidPnTargetModeSetInterface = &g_VidPnTargetModeSetInterface;
    return STATUS_SUCCESS;
}

static NTSTATUS APIENTRY
VidPn_ReleaseTargetModeSet(
    _In_ D3DKMDT_HVIDPN                               hVidPn,
    _In_ D3DKMDT_HVIDPNTARGETMODESET                  hVidPnTargetModeSet)
{
    PDXGKP_VIDPN_TARGET_MODESET ModeSet = DxgkpTargetModeSetFromHandle(hVidPnTargetModeSet);
    PDXGKP_VIDPN VidPn = DxgkpVidPnFromHandle(hVidPn);
    ULONG TargetIndex;

    if (VidPn == NULL || ModeSet == NULL || ModeSet->Owner != VidPn)
        return STATUS_INVALID_PARAMETER;
    if (InterlockedDecrement(&ModeSet->References) > 0)
        return STATUS_SUCCESS;
    InterlockedExchange(&ModeSet->References, 0);
    TargetIndex = DxgkVidPnTargetIndexFromId(VidPn, ModeSet->TargetId);
    if (TargetIndex == MAXULONG || VidPn->TargetModeSets[TargetIndex] != ModeSet)
        ExFreePoolWithTag(ModeSet, TAG_DXGK_MODESET);
    return STATUS_SUCCESS;
}

static NTSTATUS APIENTRY
VidPn_CreateNewTargetModeSet(
    _In_  D3DKMDT_HVIDPN                              hVidPn,
    _In_  D3DDDI_VIDEO_PRESENT_TARGET_ID               VidPnTargetId,
    _Out_ D3DKMDT_HVIDPNTARGETMODESET*                phVidPnTargetModeSet,
    _Out_ CONST DXGK_VIDPNTARGETMODESET_INTERFACE**   ppVidPnTargetModeSetInterface)
{
    PDXGKP_VIDPN VidPn;
    PDXGKP_VIDPN_TARGET_MODESET ModeSet;

    if (phVidPnTargetModeSet == NULL || ppVidPnTargetModeSetInterface == NULL)
        return STATUS_INVALID_PARAMETER;

    VidPn = DxgkpVidPnFromHandle(hVidPn);
    if (VidPn == NULL)
        return STATUS_INVALID_PARAMETER;

    if (DxgkVidPnTargetIndexFromId(VidPn, VidPnTargetId) == MAXULONG)
        return STATUS_GRAPHICS_INVALID_VIDEO_PRESENT_TARGET;

    ModeSet = DxgkpAllocateTargetModeSet(VidPn, VidPnTargetId);
    if (ModeSet == NULL)
        return STATUS_NO_MEMORY;
    ModeSet->References = 1;

    *phVidPnTargetModeSet          = (D3DKMDT_HVIDPNTARGETMODESET)ModeSet;
    *ppVidPnTargetModeSetInterface = &g_VidPnTargetModeSetInterface;
    return STATUS_SUCCESS;
}

static NTSTATUS APIENTRY
VidPn_AssignTargetModeSet(
    _In_ D3DKMDT_HVIDPN                               hVidPn,
    _In_ D3DDDI_VIDEO_PRESENT_TARGET_ID                VidPnTargetId,
    _In_ D3DKMDT_HVIDPNTARGETMODESET                  hVidPnTargetModeSet)
{
    PDXGKP_VIDPN VidPn;
    PDXGKP_VIDPN_TARGET_MODESET ModeSet = DxgkpTargetModeSetFromHandle(hVidPnTargetModeSet);
    PDXGKP_VIDPN_TARGET_MODESET Old;
    ULONG TargetIndex;
    NTSTATUS Status;

    VidPn = DxgkpVidPnFromHandle(hVidPn);
    if (VidPn == NULL || ModeSet == NULL || ModeSet->Owner != VidPn)
        return STATUS_INVALID_PARAMETER;

    TargetIndex = DxgkVidPnTargetIndexFromId(VidPn, VidPnTargetId);
    if (TargetIndex == MAXULONG || ModeSet->TargetId != VidPnTargetId)
        return STATUS_GRAPHICS_INVALID_VIDEO_PRESENT_TARGET;

    Old = VidPn->TargetModeSets[TargetIndex];
    if (Old == ModeSet)
        return STATUS_SUCCESS;

    /* Reject before touching any state: on failure the caller still owns
     * the new set and releases it through pfnReleaseTargetModeSet. */
    Status = DxgkpCarryTargetPin(Old, ModeSet);
    if (!NT_SUCCESS(Status))
        return Status;

    VidPn->TargetModeSets[TargetIndex] = ModeSet;
    InterlockedExchange(&ModeSet->References, 0);
    if (Old != NULL && InterlockedCompareExchange(&Old->References, 0, 0) <= 0)
        ExFreePoolWithTag(Old, TAG_DXGK_MODESET);
    return STATUS_SUCCESS;
}

/* ========================================================================
 * Topology interface (DXGK_VIDPNTOPOLOGY_INTERFACE) implementations
 * ====================================================================== */

static NTSTATUS APIENTRY
VidPnTopology_GetNumPaths(
    _In_  D3DKMDT_HVIDPNTOPOLOGY hVidPnTopology,
    _Out_ SIZE_T*                pNumPaths)
{
    PDXGKP_VIDPN VidPn;

    if (pNumPaths == NULL)
        return STATUS_INVALID_PARAMETER;

    VidPn = DxgkpTopologyFromHandle(hVidPnTopology);
    if (VidPn == NULL)
        return STATUS_INVALID_PARAMETER;

    *pNumPaths = VidPn->NumPaths;
    return STATUS_SUCCESS;
}

static NTSTATUS APIENTRY
VidPnTopology_GetNumPathsFromSource(
    _In_  D3DKMDT_HVIDPNTOPOLOGY               hVidPnTopology,
    _In_  D3DDDI_VIDEO_PRESENT_SOURCE_ID        VidPnSourceId,
    _Out_ SIZE_T*                               pNumPathsFromSource)
{
    PDXGKP_VIDPN VidPn;
    SIZE_T Count = 0;
    SIZE_T i;

    if (pNumPathsFromSource == NULL)
        return STATUS_INVALID_PARAMETER;

    VidPn = DxgkpTopologyFromHandle(hVidPnTopology);
    if (VidPn == NULL)
        return STATUS_INVALID_PARAMETER;

    for (i = 0; i < VidPn->NumPaths; i++)
    {
        if (VidPn->Paths[i].VidPnSourceId == VidPnSourceId)
            Count++;
    }

    *pNumPathsFromSource = Count;
    return STATUS_SUCCESS;
}

static NTSTATUS APIENTRY
VidPnTopology_EnumPathTargetsFromSource(
    _In_  D3DKMDT_HVIDPNTOPOLOGY               hVidPnTopology,
    _In_  D3DDDI_VIDEO_PRESENT_SOURCE_ID        VidPnSourceId,
    _In_  D3DKMDT_VIDPN_PRESENT_PATH_INDEX      PathIndex,
    _Out_ D3DDDI_VIDEO_PRESENT_TARGET_ID*       pVidPnTargetId)
{
    PDXGKP_VIDPN VidPn;
    SIZE_T i, MatchIndex = 0;

    if (pVidPnTargetId == NULL)
        return STATUS_INVALID_PARAMETER;

    VidPn = DxgkpTopologyFromHandle(hVidPnTopology);
    if (VidPn == NULL)
        return STATUS_INVALID_PARAMETER;

    for (i = 0; i < VidPn->NumPaths; i++)
    {
        if (VidPn->Paths[i].VidPnSourceId == VidPnSourceId)
        {
            if (MatchIndex == PathIndex)
            {
                *pVidPnTargetId = VidPn->Paths[i].VidPnTargetId;
                return STATUS_SUCCESS;
            }
            MatchIndex++;
        }
    }

    return STATUS_GRAPHICS_INVALID_VIDEO_PRESENT_SOURCE;
}

static NTSTATUS APIENTRY
VidPnTopology_GetPathSourceFromTarget(
    _In_  D3DKMDT_HVIDPNTOPOLOGY               hVidPnTopology,
    _In_  D3DDDI_VIDEO_PRESENT_TARGET_ID        VidPnTargetId,
    _Out_ D3DDDI_VIDEO_PRESENT_SOURCE_ID*       pVidPnSourceId)
{
    PDXGKP_VIDPN VidPn;
    SIZE_T i;

    if (pVidPnSourceId == NULL)
        return STATUS_INVALID_PARAMETER;

    VidPn = DxgkpTopologyFromHandle(hVidPnTopology);
    if (VidPn == NULL)
        return STATUS_INVALID_PARAMETER;

    for (i = 0; i < VidPn->NumPaths; i++)
    {
        if (VidPn->Paths[i].VidPnTargetId == VidPnTargetId)
        {
            *pVidPnSourceId = VidPn->Paths[i].VidPnSourceId;
            return STATUS_SUCCESS;
        }
    }

    return STATUS_GRAPHICS_TARGET_NOT_IN_TOPOLOGY;
}

static NTSTATUS APIENTRY
VidPnTopology_AcquirePathInfo(
    _In_  D3DKMDT_HVIDPNTOPOLOGY               hVidPnTopology,
    _In_  D3DDDI_VIDEO_PRESENT_SOURCE_ID        VidPnSourceId,
    _In_  D3DDDI_VIDEO_PRESENT_TARGET_ID        VidPnTargetId,
    _Out_ CONST D3DKMDT_VIDPN_PRESENT_PATH**   ppVidPnPresentPathInfo)
{
    PDXGKP_VIDPN VidPn;
    SIZE_T i;

    if (ppVidPnPresentPathInfo == NULL)
        return STATUS_INVALID_PARAMETER;

    *ppVidPnPresentPathInfo = NULL;

    VidPn = DxgkpTopologyFromHandle(hVidPnTopology);
    if (VidPn == NULL)
        return STATUS_INVALID_PARAMETER;

    for (i = 0; i < VidPn->NumPaths; i++)
    {
        if (VidPn->Paths[i].VidPnSourceId == VidPnSourceId &&
            VidPn->Paths[i].VidPnTargetId == VidPnTargetId)
        {
            *ppVidPnPresentPathInfo = &VidPn->Paths[i];
            return STATUS_SUCCESS;
        }
    }

    return STATUS_GRAPHICS_INVALID_VIDPN_TOPOLOGY;
}

static NTSTATUS APIENTRY
VidPnTopology_AcquireFirstPathInfo(
    _In_  D3DKMDT_HVIDPNTOPOLOGY               hVidPnTopology,
    _Out_ CONST D3DKMDT_VIDPN_PRESENT_PATH**   ppFirstVidPnPresentPathInfo)
{
    PDXGKP_VIDPN VidPn;

    if (ppFirstVidPnPresentPathInfo == NULL)
        return STATUS_INVALID_PARAMETER;

    *ppFirstVidPnPresentPathInfo = NULL;

    VidPn = DxgkpTopologyFromHandle(hVidPnTopology);
    if (VidPn == NULL)
        return STATUS_INVALID_PARAMETER;

    if (VidPn->NumPaths == 0)
        return STATUS_GRAPHICS_DATASET_IS_EMPTY;

    *ppFirstVidPnPresentPathInfo = &VidPn->Paths[0];
    return STATUS_SUCCESS;
}

static NTSTATUS APIENTRY
VidPnTopology_AcquireNextPathInfo(
    _In_  D3DKMDT_HVIDPNTOPOLOGY               hVidPnTopology,
    _In_  CONST D3DKMDT_VIDPN_PRESENT_PATH*    pVidPnPresentPathInfo,
    _Out_ CONST D3DKMDT_VIDPN_PRESENT_PATH**   ppNextVidPnPresentPathInfo)
{
    PDXGKP_VIDPN VidPn;
    SIZE_T i;

    if (ppNextVidPnPresentPathInfo == NULL)
        return STATUS_INVALID_PARAMETER;

    *ppNextVidPnPresentPathInfo = NULL;

    VidPn = DxgkpTopologyFromHandle(hVidPnTopology);
    if (VidPn == NULL || pVidPnPresentPathInfo == NULL)
        return STATUS_INVALID_PARAMETER;

    /* Find current path by pointer. */
    for (i = 0; i < VidPn->NumPaths; i++)
    {
        if (&VidPn->Paths[i] == pVidPnPresentPathInfo)
        {
            if (i + 1 < VidPn->NumPaths)
            {
                *ppNextVidPnPresentPathInfo = &VidPn->Paths[i + 1];
                return STATUS_SUCCESS;
            }
            return STATUS_GRAPHICS_NO_MORE_ELEMENTS_IN_DATASET;
        }
    }

    /* Fallback: match by source+target IDs. */
    for (i = 0; i < VidPn->NumPaths; i++)
    {
        if (VidPn->Paths[i].VidPnSourceId == pVidPnPresentPathInfo->VidPnSourceId &&
            VidPn->Paths[i].VidPnTargetId == pVidPnPresentPathInfo->VidPnTargetId)
        {
            if (i + 1 < VidPn->NumPaths)
            {
                *ppNextVidPnPresentPathInfo = &VidPn->Paths[i + 1];
                return STATUS_SUCCESS;
            }
            return STATUS_GRAPHICS_NO_MORE_ELEMENTS_IN_DATASET;
        }
    }

    return STATUS_GRAPHICS_NO_MORE_ELEMENTS_IN_DATASET;
}

static NTSTATUS APIENTRY
VidPnTopology_UpdatePathSupportInfo(
    _In_ D3DKMDT_HVIDPNTOPOLOGY                hVidPnTopology,
    _In_ CONST D3DKMDT_VIDPN_PRESENT_PATH*     pVidPnPresentPathInfo)
{
    PDXGKP_VIDPN VidPn;
    SIZE_T i;

    VidPn = DxgkpTopologyFromHandle(hVidPnTopology);
    if (VidPn == NULL || pVidPnPresentPathInfo == NULL)
        return STATUS_INVALID_PARAMETER;

    /* Find the path and update its support info. */
    for (i = 0; i < VidPn->NumPaths; i++)
    {
        if (VidPn->Paths[i].VidPnSourceId == pVidPnPresentPathInfo->VidPnSourceId &&
            VidPn->Paths[i].VidPnTargetId == pVidPnPresentPathInfo->VidPnTargetId)
        {
            VidPn->Paths[i].ContentTransformation = pVidPnPresentPathInfo->ContentTransformation;
            VidPn->Paths[i].VidPnTargetColorBasis = pVidPnPresentPathInfo->VidPnTargetColorBasis;
            VidPn->Paths[i].VidPnTargetColorCoeffDynamicRanges =
                pVidPnPresentPathInfo->VidPnTargetColorCoeffDynamicRanges;
            VidPn->Paths[i].Content = pVidPnPresentPathInfo->Content;

            /* This call reports what the path supports, so take the copy
             * protection support level -- but not CopyProtectionType or the
             * APS trigger bits, which are the active controls and are not
             * the miniport's to change through a support update. */
            VidPn->Paths[i].CopyProtection.CopyProtectionSupport =
                pVidPnPresentPathInfo->CopyProtection.CopyProtectionSupport;
            return STATUS_SUCCESS;
        }
    }

    return STATUS_GRAPHICS_INVALID_VIDPN_TOPOLOGY;
}

static NTSTATUS APIENTRY
VidPnTopology_ReleasePathInfo(
    _In_ D3DKMDT_HVIDPNTOPOLOGY                hVidPnTopology,
    _In_ CONST D3DKMDT_VIDPN_PRESENT_PATH*     pVidPnPresentPathInfo)
{
    PDXGKP_VIDPN VidPn = DxgkpTopologyFromHandle(hVidPnTopology);

    if (VidPn != NULL && pVidPnPresentPathInfo != NULL)
        DxgkpPendingConsume(VidPn, pVidPnPresentPathInfo, DxgkpPendingPath);
    return STATUS_SUCCESS;
}

static NTSTATUS APIENTRY
VidPnTopology_CreateNewPathInfo(
    _In_  D3DKMDT_HVIDPNTOPOLOGY               hVidPnTopology,
    _Out_ D3DKMDT_VIDPN_PRESENT_PATH**         ppNewVidPnPresentPathInfo)
{
    PDXGKP_VIDPN VidPn;

    if (ppNewVidPnPresentPathInfo == NULL)
        return STATUS_INVALID_PARAMETER;

    *ppNewVidPnPresentPathInfo = NULL;

    VidPn = DxgkpTopologyFromHandle(hVidPnTopology);
    if (VidPn == NULL)
        return STATUS_INVALID_PARAMETER;

    {
        D3DKMDT_VIDPN_PRESENT_PATH *Path =
            DxgkpPendingCreate(VidPn, DxgkpPendingPath);
        if (Path == NULL)
            return STATUS_NO_MEMORY;
        *ppNewVidPnPresentPathInfo = Path;
    }

    return STATUS_SUCCESS;
}

static NTSTATUS APIENTRY
VidPnTopology_AddPath(
    _In_ D3DKMDT_HVIDPNTOPOLOGY                hVidPnTopology,
    _In_ D3DKMDT_VIDPN_PRESENT_PATH*           pVidPnPresentPath)
{
    PDXGKP_VIDPN VidPn;

    if (pVidPnPresentPath == NULL)
        return STATUS_INVALID_PARAMETER;

    VidPn = DxgkpTopologyFromHandle(hVidPnTopology);
    if (VidPn == NULL)
        return STATUS_INVALID_PARAMETER;

    if (VidPn->NumPaths >= DXGKP_MAX_PATHS)
    {
        DXGKRNL_WARN("VidPnTopology_AddPath: topology full (%Iu paths)\n",
                     VidPn->NumPaths);
        return STATUS_GRAPHICS_INVALID_VIDPN_TOPOLOGY;
    }

    /* Validate source/target IDs. */
    if (pVidPnPresentPath->VidPnSourceId >= VidPn->NumSources ||
        DxgkVidPnTargetIndexFromId(VidPn, pVidPnPresentPath->VidPnTargetId) == MAXULONG)
    {
        DXGKRNL_WARN("VidPnTopology_AddPath: src=%u tgt=%u out of range\n",
                     pVidPnPresentPath->VidPnSourceId,
                     pVidPnPresentPath->VidPnTargetId);
        return STATUS_GRAPHICS_INVALID_VIDPN_TOPOLOGY;
    }

    /* Check for duplicate target (each target can only appear once). */
    {
        SIZE_T i;
        for (i = 0; i < VidPn->NumPaths; i++)
        {
            if (VidPn->Paths[i].VidPnTargetId == pVidPnPresentPath->VidPnTargetId)
            {
                DXGKRNL_WARN("VidPnTopology_AddPath: target %u already in topology\n",
                             pVidPnPresentPath->VidPnTargetId);
                return STATUS_GRAPHICS_INVALID_VIDPN_TOPOLOGY;
            }
        }
    }

    RtlCopyMemory(&VidPn->Paths[VidPn->NumPaths], pVidPnPresentPath,
                   sizeof(D3DKMDT_VIDPN_PRESENT_PATH));
    VidPn->NumPaths++;

    /* The topology holds its own copy now, so a created path is consumed. */
    DxgkpPendingConsume(VidPn, pVidPnPresentPath, DxgkpPendingPath);

    DXGKRNL_TRACE("VidPnTopology_AddPath: added path src=%u tgt=%u (now %Iu paths)\n",
                  pVidPnPresentPath->VidPnSourceId,
                  pVidPnPresentPath->VidPnTargetId,
                  VidPn->NumPaths);
    return STATUS_SUCCESS;
}

static NTSTATUS APIENTRY
VidPnTopology_RemovePath(
    _In_ D3DKMDT_HVIDPNTOPOLOGY                hVidPnTopology,
    _In_ D3DDDI_VIDEO_PRESENT_SOURCE_ID         VidPnSourceId,
    _In_ D3DDDI_VIDEO_PRESENT_TARGET_ID         VidPnTargetId)
{
    PDXGKP_VIDPN VidPn;
    SIZE_T i;

    VidPn = DxgkpTopologyFromHandle(hVidPnTopology);
    if (VidPn == NULL)
        return STATUS_INVALID_PARAMETER;

    for (i = 0; i < VidPn->NumPaths; i++)
    {
        if (VidPn->Paths[i].VidPnSourceId == VidPnSourceId &&
            VidPn->Paths[i].VidPnTargetId == VidPnTargetId)
        {
            /* Shift remaining paths down. */
            if (i + 1 < VidPn->NumPaths)
            {
                RtlMoveMemory(&VidPn->Paths[i], &VidPn->Paths[i + 1],
                               (VidPn->NumPaths - i - 1) * sizeof(D3DKMDT_VIDPN_PRESENT_PATH));
            }
            VidPn->NumPaths--;
            DXGKRNL_TRACE("VidPnTopology_RemovePath: removed src=%u tgt=%u (now %Iu paths)\n",
                          VidPnSourceId, VidPnTargetId, VidPn->NumPaths);
            return STATUS_SUCCESS;
        }
    }

    return STATUS_GRAPHICS_INVALID_VIDPN_TOPOLOGY;
}

/* ========================================================================
 * Source mode set interface (DXGK_VIDPNSOURCEMODESET_INTERFACE)
 * ====================================================================== */

static NTSTATUS APIENTRY
VidPnSourceModeSet_GetNumModes(
    _In_  D3DKMDT_HVIDPNSOURCEMODESET           hVidPnSourceModeSet,
    _Out_ CONST SIZE_T*                         pNumModes)
{
    PDXGKP_VIDPN_SOURCE_MODESET ModeSet;

    if (pNumModes == NULL)
        return STATUS_INVALID_PARAMETER;

    ModeSet = DxgkpSourceModeSetFromHandle(hVidPnSourceModeSet);
    if (ModeSet == NULL)
        return STATUS_INVALID_PARAMETER;

    *(SIZE_T*)pNumModes = ModeSet->NumModes;
    return STATUS_SUCCESS;
}

static NTSTATUS APIENTRY
VidPnSourceModeSet_AcquireFirstModeInfo(
    _In_  D3DKMDT_HVIDPNSOURCEMODESET                  hVidPnSourceModeSet,
    _Out_ CONST D3DKMDT_VIDPN_SOURCE_MODE**            ppFirstVidPnSourceModeInfo)
{
    PDXGKP_VIDPN_SOURCE_MODESET ModeSet;

    if (ppFirstVidPnSourceModeInfo == NULL)
        return STATUS_INVALID_PARAMETER;

    *ppFirstVidPnSourceModeInfo = NULL;

    ModeSet = DxgkpSourceModeSetFromHandle(hVidPnSourceModeSet);
    if (ModeSet == NULL)
        return STATUS_INVALID_PARAMETER;

    if (ModeSet->NumModes == 0)
        return STATUS_GRAPHICS_DATASET_IS_EMPTY;

    *ppFirstVidPnSourceModeInfo = &ModeSet->Modes[0];
    return STATUS_SUCCESS;
}

static NTSTATUS APIENTRY
VidPnSourceModeSet_AcquireNextModeInfo(
    _In_  D3DKMDT_HVIDPNSOURCEMODESET                  hVidPnSourceModeSet,
    _In_  CONST D3DKMDT_VIDPN_SOURCE_MODE*             pVidPnSourceModeInfo,
    _Out_ CONST D3DKMDT_VIDPN_SOURCE_MODE**            ppNextVidPnSourceModeInfo)
{
    PDXGKP_VIDPN_SOURCE_MODESET ModeSet;
    SIZE_T i;

    if (ppNextVidPnSourceModeInfo == NULL)
        return STATUS_INVALID_PARAMETER;

    *ppNextVidPnSourceModeInfo = NULL;

    ModeSet = DxgkpSourceModeSetFromHandle(hVidPnSourceModeSet);
    if (ModeSet == NULL || pVidPnSourceModeInfo == NULL)
        return STATUS_INVALID_PARAMETER;

    /* Find by pointer first. */
    for (i = 0; i < ModeSet->NumModes; i++)
    {
        if (&ModeSet->Modes[i] == pVidPnSourceModeInfo)
        {
            if (i + 1 < ModeSet->NumModes)
            {
                *ppNextVidPnSourceModeInfo = &ModeSet->Modes[i + 1];
                return STATUS_SUCCESS;
            }
            return STATUS_GRAPHICS_NO_MORE_ELEMENTS_IN_DATASET;
        }
    }

    /* Fallback: match by ID. */
    for (i = 0; i < ModeSet->NumModes; i++)
    {
        if (ModeSet->Modes[i].Id == pVidPnSourceModeInfo->Id)
        {
            if (i + 1 < ModeSet->NumModes)
            {
                *ppNextVidPnSourceModeInfo = &ModeSet->Modes[i + 1];
                return STATUS_SUCCESS;
            }
            return STATUS_GRAPHICS_NO_MORE_ELEMENTS_IN_DATASET;
        }
    }

    return STATUS_GRAPHICS_NO_MORE_ELEMENTS_IN_DATASET;
}

static NTSTATUS APIENTRY
VidPnSourceModeSet_AcquirePinnedModeInfo(
    _In_  D3DKMDT_HVIDPNSOURCEMODESET                  hVidPnSourceModeSet,
    _Out_ CONST D3DKMDT_VIDPN_SOURCE_MODE**            ppPinnedVidPnSourceModeInfo)
{
    PDXGKP_VIDPN_SOURCE_MODESET ModeSet;
    SIZE_T i;

    if (ppPinnedVidPnSourceModeInfo == NULL)
        return STATUS_INVALID_PARAMETER;

    *ppPinnedVidPnSourceModeInfo = NULL;

    ModeSet = DxgkpSourceModeSetFromHandle(hVidPnSourceModeSet);
    if (ModeSet == NULL)
        return STATUS_GRAPHICS_INVALID_VIDPN_SOURCEMODESET;

    if (ModeSet->PinnedModeId == (UINT)-1)
        return STATUS_GRAPHICS_MODE_NOT_PINNED;

    for (i = 0; i < ModeSet->NumModes; i++)
    {
        if (ModeSet->Modes[i].Id == ModeSet->PinnedModeId)
        {
            *ppPinnedVidPnSourceModeInfo = &ModeSet->Modes[i];
            return STATUS_SUCCESS;
        }
    }

    return STATUS_GRAPHICS_MODE_NOT_PINNED;
}

static NTSTATUS APIENTRY
VidPnSourceModeSet_ReleaseModeInfo(
    _In_ D3DKMDT_HVIDPNSOURCEMODESET                   hVidPnSourceModeSet,
    _In_ CONST D3DKMDT_VIDPN_SOURCE_MODE*              pVidPnSourceModeInfo)
{
    PDXGKP_VIDPN_SOURCE_MODESET ModeSet =
        DxgkpSourceModeSetFromHandle(hVidPnSourceModeSet);

    if (ModeSet != NULL && ModeSet->Owner != NULL && pVidPnSourceModeInfo != NULL)
        DxgkpPendingConsume(ModeSet->Owner, pVidPnSourceModeInfo,
                            DxgkpPendingSourceMode);
    return STATUS_SUCCESS;
}

static NTSTATUS APIENTRY
VidPnSourceModeSet_CreateNewModeInfo(
    _In_  D3DKMDT_HVIDPNSOURCEMODESET                  hVidPnSourceModeSet,
    _Out_ D3DKMDT_VIDPN_SOURCE_MODE**                  ppNewVidPnSourceModeInfo)
{
    PDXGKP_VIDPN_SOURCE_MODESET ModeSet;

    if (ppNewVidPnSourceModeInfo == NULL)
        return STATUS_INVALID_PARAMETER;

    *ppNewVidPnSourceModeInfo = NULL;

    ModeSet = DxgkpSourceModeSetFromHandle(hVidPnSourceModeSet);
    if (ModeSet == NULL || ModeSet->Owner == NULL)
        return STATUS_INVALID_PARAMETER;

    {
        D3DKMDT_VIDPN_SOURCE_MODE *Mode =
            DxgkpPendingCreate(ModeSet->Owner, DxgkpPendingSourceMode);
        if (Mode == NULL)
            return STATUS_NO_MEMORY;
        Mode->Id = ModeSet->NextModeId++;
        *ppNewVidPnSourceModeInfo = Mode;
    }
    return STATUS_SUCCESS;
}

static NTSTATUS APIENTRY
VidPnSourceModeSet_AddMode(
    _In_ D3DKMDT_HVIDPNSOURCEMODESET                   hVidPnSourceModeSet,
    _In_ D3DKMDT_VIDPN_SOURCE_MODE*                    pVidPnSourceModeInfo)
{
    PDXGKP_VIDPN_SOURCE_MODESET ModeSet;
    D3DKMDT_VIDPN_SOURCE_MODE NewMode;
    SIZE_T i;

    if (pVidPnSourceModeInfo == NULL)
        return STATUS_INVALID_PARAMETER;

    ModeSet = DxgkpSourceModeSetFromHandle(hVidPnSourceModeSet);
    if (ModeSet == NULL)
        return STATUS_INVALID_PARAMETER;

    RtlCopyMemory(&NewMode, pVidPnSourceModeInfo, sizeof(NewMode));
    DxgkpNormalizeSourceMode(&NewMode);

    for (i = 0; i < ModeSet->NumModes; i++)
    {
        if (ModeSet->Modes[i].Id == NewMode.Id)
        {
            return DxgkpAreEquivalentSourceModes(&ModeSet->Modes[i], &NewMode) ?
                   STATUS_GRAPHICS_MODE_ALREADY_IN_MODESET :
                   STATUS_GRAPHICS_MODE_ID_MUST_BE_UNIQUE;
        }
    }

    for (i = 0; i < ModeSet->NumModes; i++)
    {
        if (DxgkpAreEquivalentSourceModes(&ModeSet->Modes[i], &NewMode))
        {
            return STATUS_GRAPHICS_MODE_ALREADY_IN_MODESET;
        }
    }

    if (ModeSet->NumModes >= DXGKP_MAX_MODES)
        return STATUS_GRAPHICS_RESOURCES_NOT_RELATED;

    RtlCopyMemory(&ModeSet->Modes[ModeSet->NumModes], &NewMode, sizeof(NewMode));
    if (NewMode.Id >= ModeSet->NextModeId)
        ModeSet->NextModeId = NewMode.Id + 1;
    ModeSet->NumModes++;

    /* The set holds its own copy now, so a created object is consumed.  On
     * failure it stays the miniport's, to release through ReleaseModeInfo. */
    if (ModeSet->Owner)
        DxgkpPendingConsume(ModeSet->Owner, pVidPnSourceModeInfo,
                            DxgkpPendingSourceMode);

    return STATUS_SUCCESS;
}

static NTSTATUS APIENTRY
VidPnSourceModeSet_PinMode(
    _In_ D3DKMDT_HVIDPNSOURCEMODESET                   hVidPnSourceModeSet,
    _In_ D3DKMDT_VIDEO_PRESENT_SOURCE_MODE_ID          VidPnSourceModeId)
{
    PDXGKP_VIDPN_SOURCE_MODESET ModeSet;
    SIZE_T i;

    ModeSet = DxgkpSourceModeSetFromHandle(hVidPnSourceModeSet);
    if (ModeSet == NULL)
        return STATUS_INVALID_PARAMETER;

    for (i = 0; i < ModeSet->NumModes; i++)
    {
        if (ModeSet->Modes[i].Id == VidPnSourceModeId)
        {
            ModeSet->PinnedModeId = VidPnSourceModeId;
            return STATUS_SUCCESS;
        }
    }

    return STATUS_GRAPHICS_INVALID_VIDEO_PRESENT_SOURCE_MODE;
}

/* ========================================================================
 * Target mode set interface (DXGK_VIDPNTARGETMODESET_INTERFACE)
 * ====================================================================== */

static NTSTATUS APIENTRY
VidPnTargetModeSet_GetNumModes(
    _In_  D3DKMDT_HVIDPNTARGETMODESET           hVidPnTargetModeSet,
    _Out_ CONST SIZE_T*                         pNumModes)
{
    PDXGKP_VIDPN_TARGET_MODESET ModeSet;

    if (pNumModes == NULL)
        return STATUS_INVALID_PARAMETER;

    ModeSet = DxgkpTargetModeSetFromHandle(hVidPnTargetModeSet);
    if (ModeSet == NULL)
        return STATUS_INVALID_PARAMETER;

    *(SIZE_T*)pNumModes = ModeSet->NumModes;
    return STATUS_SUCCESS;
}

static NTSTATUS APIENTRY
VidPnTargetModeSet_AcquireFirstModeInfo(
    _In_  D3DKMDT_HVIDPNTARGETMODESET                  hVidPnTargetModeSet,
    _Out_ CONST D3DKMDT_VIDPN_TARGET_MODE**            ppFirstVidPnTargetModeInfo)
{
    PDXGKP_VIDPN_TARGET_MODESET ModeSet;

    if (ppFirstVidPnTargetModeInfo == NULL)
        return STATUS_INVALID_PARAMETER;

    *ppFirstVidPnTargetModeInfo = NULL;

    ModeSet = DxgkpTargetModeSetFromHandle(hVidPnTargetModeSet);
    if (ModeSet == NULL)
        return STATUS_INVALID_PARAMETER;

    if (ModeSet->NumModes == 0)
        return STATUS_GRAPHICS_DATASET_IS_EMPTY;

    *ppFirstVidPnTargetModeInfo = &ModeSet->Modes[0];
    return STATUS_SUCCESS;
}

static NTSTATUS APIENTRY
VidPnTargetModeSet_AcquireNextModeInfo(
    _In_  D3DKMDT_HVIDPNTARGETMODESET                  hVidPnTargetModeSet,
    _In_  CONST D3DKMDT_VIDPN_TARGET_MODE*             pVidPnTargetModeInfo,
    _Out_ CONST D3DKMDT_VIDPN_TARGET_MODE**            ppNextVidPnTargetModeInfo)
{
    PDXGKP_VIDPN_TARGET_MODESET ModeSet;
    SIZE_T i;

    if (ppNextVidPnTargetModeInfo == NULL)
        return STATUS_INVALID_PARAMETER;

    *ppNextVidPnTargetModeInfo = NULL;

    ModeSet = DxgkpTargetModeSetFromHandle(hVidPnTargetModeSet);
    if (ModeSet == NULL || pVidPnTargetModeInfo == NULL)
        return STATUS_INVALID_PARAMETER;

    for (i = 0; i < ModeSet->NumModes; i++)
    {
        if (&ModeSet->Modes[i] == pVidPnTargetModeInfo)
        {
            if (i + 1 < ModeSet->NumModes)
            {
                *ppNextVidPnTargetModeInfo = &ModeSet->Modes[i + 1];
                return STATUS_SUCCESS;
            }
            return STATUS_GRAPHICS_NO_MORE_ELEMENTS_IN_DATASET;
        }
    }

    /* Fallback: match by ID. */
    if ((ULONG_PTR)pVidPnTargetModeInfo >= (ULONG_PTR)MmSystemRangeStart)
    {
        for (i = 0; i < ModeSet->NumModes; i++)
        {
            if (ModeSet->Modes[i].Id == pVidPnTargetModeInfo->Id)
            {
                if (i + 1 < ModeSet->NumModes)
                {
                    *ppNextVidPnTargetModeInfo = &ModeSet->Modes[i + 1];
                    return STATUS_SUCCESS;
                }
                return STATUS_GRAPHICS_NO_MORE_ELEMENTS_IN_DATASET;
            }
        }
    }

    return STATUS_GRAPHICS_NO_MORE_ELEMENTS_IN_DATASET;
}

static NTSTATUS APIENTRY
VidPnTargetModeSet_AcquirePinnedModeInfo(
    _In_  D3DKMDT_HVIDPNTARGETMODESET                  hVidPnTargetModeSet,
    _Out_ CONST D3DKMDT_VIDPN_TARGET_MODE**            ppPinnedVidPnTargetModeInfo)
{
    PDXGKP_VIDPN_TARGET_MODESET ModeSet;
    SIZE_T i;

    if (ppPinnedVidPnTargetModeInfo == NULL)
        return STATUS_INVALID_PARAMETER;

    *ppPinnedVidPnTargetModeInfo = NULL;

    ModeSet = DxgkpTargetModeSetFromHandle(hVidPnTargetModeSet);
    if (ModeSet == NULL)
        return STATUS_GRAPHICS_INVALID_VIDPN_TARGETMODESET;

    if (ModeSet->PinnedModeId == (UINT)-1)
        return STATUS_GRAPHICS_MODE_NOT_PINNED;

    for (i = 0; i < ModeSet->NumModes; i++)
    {
        if (ModeSet->Modes[i].Id == ModeSet->PinnedModeId)
        {
            *ppPinnedVidPnTargetModeInfo = &ModeSet->Modes[i];
            return STATUS_SUCCESS;
        }
    }

    return STATUS_GRAPHICS_MODE_NOT_PINNED;
}

static NTSTATUS APIENTRY
VidPnTargetModeSet_ReleaseModeInfo(
    _In_ D3DKMDT_HVIDPNTARGETMODESET                   hVidPnTargetModeSet,
    _In_ CONST D3DKMDT_VIDPN_TARGET_MODE*              pVidPnTargetModeInfo)
{
    PDXGKP_VIDPN_TARGET_MODESET ModeSet =
        DxgkpTargetModeSetFromHandle(hVidPnTargetModeSet);

    if (ModeSet != NULL && ModeSet->Owner != NULL && pVidPnTargetModeInfo != NULL)
        DxgkpPendingConsume(ModeSet->Owner, pVidPnTargetModeInfo,
                            DxgkpPendingTargetMode);
    return STATUS_SUCCESS;
}

static NTSTATUS APIENTRY
VidPnTargetModeSet_CreateNewModeInfo(
    _In_  D3DKMDT_HVIDPNTARGETMODESET                  hVidPnTargetModeSet,
    _Out_ D3DKMDT_VIDPN_TARGET_MODE**                  ppNewVidPnTargetModeInfo)
{
    PDXGKP_VIDPN_TARGET_MODESET ModeSet;

    if (ppNewVidPnTargetModeInfo == NULL)
        return STATUS_INVALID_PARAMETER;

    *ppNewVidPnTargetModeInfo = NULL;

    ModeSet = DxgkpTargetModeSetFromHandle(hVidPnTargetModeSet);
    if (ModeSet == NULL || ModeSet->Owner == NULL)
        return STATUS_INVALID_PARAMETER;

    {
        D3DKMDT_VIDPN_TARGET_MODE *Mode =
            DxgkpPendingCreate(ModeSet->Owner, DxgkpPendingTargetMode);
        if (Mode == NULL)
            return STATUS_NO_MEMORY;
        Mode->Id = ModeSet->NextModeId++;
        *ppNewVidPnTargetModeInfo = Mode;
    }
    return STATUS_SUCCESS;
}

static NTSTATUS APIENTRY
VidPnTargetModeSet_AddMode(
    _In_ D3DKMDT_HVIDPNTARGETMODESET                   hVidPnTargetModeSet,
    _In_ D3DKMDT_VIDPN_TARGET_MODE*                    pVidPnTargetModeInfo)
{
    PDXGKP_VIDPN_TARGET_MODESET ModeSet;
    D3DKMDT_VIDPN_TARGET_MODE NewMode;
    SIZE_T i;

    if (pVidPnTargetModeInfo == NULL)
        return STATUS_INVALID_PARAMETER;

    ModeSet = DxgkpTargetModeSetFromHandle(hVidPnTargetModeSet);
    if (ModeSet == NULL)
        return STATUS_INVALID_PARAMETER;

    RtlCopyMemory(&NewMode, pVidPnTargetModeInfo, sizeof(NewMode));
    DxgkpNormalizeTargetMode(&NewMode);

    for (i = 0; i < ModeSet->NumModes; i++)
    {
        if (ModeSet->Modes[i].Id == NewMode.Id)
        {
            return DxgkpAreSameTargetModeValues(&ModeSet->Modes[i], &NewMode) ?
                   STATUS_GRAPHICS_MODE_ALREADY_IN_MODESET :
                   STATUS_GRAPHICS_MODE_ID_MUST_BE_UNIQUE;
        }
    }

    for (i = 0; i < ModeSet->NumModes; i++)
    {
        if (DxgkpAreSameTargetModeValues(&ModeSet->Modes[i], &NewMode))
        {
            return STATUS_GRAPHICS_MODE_ALREADY_IN_MODESET;
        }
    }

    if (ModeSet->NumModes >= DXGKP_MAX_MODES)
        return STATUS_GRAPHICS_RESOURCES_NOT_RELATED;

    RtlCopyMemory(&ModeSet->Modes[ModeSet->NumModes], &NewMode, sizeof(NewMode));
    if (NewMode.Id >= ModeSet->NextModeId)
        ModeSet->NextModeId = NewMode.Id + 1;
    ModeSet->NumModes++;

    /* The set holds its own copy now, so a created object is consumed.  On
     * failure it stays the miniport's, to release through ReleaseModeInfo. */
    if (ModeSet->Owner)
        DxgkpPendingConsume(ModeSet->Owner, pVidPnTargetModeInfo,
                            DxgkpPendingTargetMode);

    return STATUS_SUCCESS;
}

static NTSTATUS APIENTRY
VidPnTargetModeSet_PinMode(
    _In_ D3DKMDT_HVIDPNTARGETMODESET                   hVidPnTargetModeSet,
    _In_ D3DKMDT_VIDEO_PRESENT_TARGET_MODE_ID          VidPnTargetModeId)
{
    PDXGKP_VIDPN_TARGET_MODESET ModeSet;
    SIZE_T i;

    ModeSet = DxgkpTargetModeSetFromHandle(hVidPnTargetModeSet);
    if (ModeSet == NULL)
        return STATUS_INVALID_PARAMETER;

    for (i = 0; i < ModeSet->NumModes; i++)
    {
        if (ModeSet->Modes[i].Id == VidPnTargetModeId)
        {
            ModeSet->PinnedModeId = VidPnTargetModeId;
            return STATUS_SUCCESS;
        }
    }

    return STATUS_GRAPHICS_INVALID_VIDEO_PRESENT_TARGET_MODE;
}

/* ========================================================================
 * Monitor source mode set interface (DXGK_MONITORSOURCEMODESET_INTERFACE)
 * ====================================================================== */

static NTSTATUS APIENTRY
MonitorSourceModeSet_GetNumModes(
    _In_  D3DKMDT_HMONITORSOURCEMODESET              hMonitorSourceModeSet,
    _Out_ SIZE_T*                                    pNumModes)
{
    PDXGKP_MONITOR_SOURCE_MODESET ModeSet;

    if (pNumModes == NULL)
        return STATUS_INVALID_PARAMETER;

    ModeSet = DxgkpMonitorModeSetFromHandle(hMonitorSourceModeSet);
    if (ModeSet == NULL)
        return STATUS_INVALID_PARAMETER;

    *pNumModes = ModeSet->NumModes;
    return STATUS_SUCCESS;
}

static NTSTATUS APIENTRY
MonitorSourceModeSet_AcquirePreferredModeInfo(
    _In_  D3DKMDT_HMONITORSOURCEMODESET              hMonitorSourceModeSet,
    _Out_ CONST D3DKMDT_MONITOR_SOURCE_MODE**        ppFirstMonitorSourceModeInfo)
{
    PDXGKP_MONITOR_SOURCE_MODESET ModeSet;
    SIZE_T i;

    if (ppFirstMonitorSourceModeInfo == NULL)
        return STATUS_INVALID_PARAMETER;

    *ppFirstMonitorSourceModeInfo = NULL;

    ModeSet = DxgkpMonitorModeSetFromHandle(hMonitorSourceModeSet);
    if (ModeSet == NULL)
        return STATUS_INVALID_PARAMETER;

    for (i = 0; i < ModeSet->NumModes; i++)
    {
        if (ModeSet->Modes[i].Preference == D3DKMDT_MP_PREFERRED)
        {
            *ppFirstMonitorSourceModeInfo = &ModeSet->Modes[i];
            return STATUS_SUCCESS;
        }
    }

    /*
     * No mode is marked preferred -- including when the set is empty.  Report
     * that rather than promoting some other mode: a caller would otherwise
     * treat an arbitrary resolution as the monitor's native one.
     *
     * STATUS_GRAPHICS_NO_PREFERRED_MODE is an informational status, so
     * NT_SUCCESS() is TRUE for it; the NULL output is what tells the caller
     * there is nothing to use.
     */
    return STATUS_GRAPHICS_NO_PREFERRED_MODE;
}

static NTSTATUS APIENTRY
MonitorSourceModeSet_AcquireFirstModeInfo(
    _In_  D3DKMDT_HMONITORSOURCEMODESET              hMonitorSourceModeSet,
    _Out_ CONST D3DKMDT_MONITOR_SOURCE_MODE**        ppFirstMonitorSourceModeInfo)
{
    PDXGKP_MONITOR_SOURCE_MODESET ModeSet;

    if (ppFirstMonitorSourceModeInfo == NULL)
        return STATUS_INVALID_PARAMETER;

    *ppFirstMonitorSourceModeInfo = NULL;

    ModeSet = DxgkpMonitorModeSetFromHandle(hMonitorSourceModeSet);
    if (ModeSet == NULL)
        return STATUS_INVALID_PARAMETER;

    if (ModeSet->NumModes == 0)
        return STATUS_GRAPHICS_DATASET_IS_EMPTY;

    *ppFirstMonitorSourceModeInfo = &ModeSet->Modes[0];
    return STATUS_SUCCESS;
}

static NTSTATUS APIENTRY
MonitorSourceModeSet_AcquireNextModeInfo(
    _In_  D3DKMDT_HMONITORSOURCEMODESET              hMonitorSourceModeSet,
    _In_  CONST D3DKMDT_MONITOR_SOURCE_MODE*         pMonitorSourceModeInfo,
    _Out_ CONST D3DKMDT_MONITOR_SOURCE_MODE**        ppNextMonitorSourceModeInfo)
{
    PDXGKP_MONITOR_SOURCE_MODESET ModeSet;
    SIZE_T i;

    if (ppNextMonitorSourceModeInfo == NULL)
        return STATUS_INVALID_PARAMETER;

    *ppNextMonitorSourceModeInfo = NULL;

    ModeSet = DxgkpMonitorModeSetFromHandle(hMonitorSourceModeSet);
    if (ModeSet == NULL || pMonitorSourceModeInfo == NULL)
        return STATUS_INVALID_PARAMETER;

    for (i = 0; i < ModeSet->NumModes; i++)
    {
        if (&ModeSet->Modes[i] == pMonitorSourceModeInfo)
        {
            if (i + 1 < ModeSet->NumModes)
            {
                *ppNextMonitorSourceModeInfo = &ModeSet->Modes[i + 1];
                return STATUS_SUCCESS;
            }
            return STATUS_GRAPHICS_NO_MORE_ELEMENTS_IN_DATASET;
        }
    }

    for (i = 0; i < ModeSet->NumModes; i++)
    {
        if (ModeSet->Modes[i].Id == pMonitorSourceModeInfo->Id)
        {
            if (i + 1 < ModeSet->NumModes)
            {
                *ppNextMonitorSourceModeInfo = &ModeSet->Modes[i + 1];
                return STATUS_SUCCESS;
            }
            return STATUS_GRAPHICS_NO_MORE_ELEMENTS_IN_DATASET;
        }
    }

    return STATUS_GRAPHICS_NO_MORE_ELEMENTS_IN_DATASET;
}

static NTSTATUS APIENTRY
MonitorSourceModeSet_CreateNewModeInfo(
    _In_  D3DKMDT_HMONITORSOURCEMODESET              hMonitorSourceModeSet,
    _Out_ D3DKMDT_MONITOR_SOURCE_MODE**              ppNewMonitorSourceModeInfo)
{
    PDXGKP_MONITOR_SOURCE_MODESET ModeSet;

    if (ppNewMonitorSourceModeInfo == NULL)
        return STATUS_INVALID_PARAMETER;

    *ppNewMonitorSourceModeInfo = NULL;

    ModeSet = DxgkpMonitorModeSetFromHandle(hMonitorSourceModeSet);
    if (ModeSet == NULL || ModeSet->Owner == NULL)
        return STATUS_INVALID_PARAMETER;

    {
        /* AddMode assigns the stored id, so this one is only a preview. */
        D3DKMDT_MONITOR_SOURCE_MODE *Mode =
            DxgkpPendingCreate(ModeSet->Owner, DxgkpPendingMonitorMode);
        if (Mode == NULL)
            return STATUS_NO_MEMORY;
        Mode->Id = ModeSet->NextModeId;
        *ppNewMonitorSourceModeInfo = Mode;
    }

    return STATUS_SUCCESS;
}

static NTSTATUS APIENTRY
MonitorSourceModeSet_AddMode(
    _In_ D3DKMDT_HMONITORSOURCEMODESET               hMonitorSourceModeSet,
    _In_ D3DKMDT_MONITOR_SOURCE_MODE*                pMonitorSourceModeInfo)
{
    PDXGKP_MONITOR_SOURCE_MODESET ModeSet;
    D3DKMDT_MONITOR_SOURCE_MODE NewMode;
    SIZE_T i;

    if (pMonitorSourceModeInfo == NULL)
        return STATUS_INVALID_PARAMETER;

    ModeSet = DxgkpMonitorModeSetFromHandle(hMonitorSourceModeSet);
    if (ModeSet == NULL)
        return STATUS_INVALID_PARAMETER;

    RtlCopyMemory(&NewMode, pMonitorSourceModeInfo, sizeof(NewMode));
    DxgkpNormalizeMonitorMode(&NewMode);

    for (i = 0; i < ModeSet->NumModes; i++)
    {
        if (DxgkpAreEquivalentMonitorModes(&ModeSet->Modes[i], &NewMode))
            return STATUS_GRAPHICS_MODE_ALREADY_IN_MODESET;
    }

    if (ModeSet->NumModes >= DXGKP_MAX_MODES)
        return STATUS_GRAPHICS_RESOURCES_NOT_RELATED;

    RtlCopyMemory(&ModeSet->Modes[ModeSet->NumModes], &NewMode, sizeof(NewMode));
    ModeSet->Modes[ModeSet->NumModes].Id = ModeSet->NextModeId;
    ModeSet->NextModeId++;
    ModeSet->NumModes++;

    /* The set holds its own copy now, so a created object is consumed.  On
     * failure it stays the miniport's, to release through ReleaseModeInfo. */
    if (ModeSet->Owner)
        DxgkpPendingConsume(ModeSet->Owner, pMonitorSourceModeInfo,
                            DxgkpPendingMonitorMode);

    return STATUS_SUCCESS;
}

static NTSTATUS APIENTRY
MonitorSourceModeSet_ReleaseModeInfo(
    _In_ D3DKMDT_HMONITORSOURCEMODESET               hMonitorSourceModeSet,
    _In_ CONST D3DKMDT_MONITOR_SOURCE_MODE*          pMonitorSourceModeInfo)
{
    PDXGKP_MONITOR_SOURCE_MODESET ModeSet =
        DxgkpMonitorModeSetFromHandle(hMonitorSourceModeSet);

    if (ModeSet != NULL && ModeSet->Owner != NULL && pMonitorSourceModeInfo != NULL)
        DxgkpPendingConsume(ModeSet->Owner, pMonitorSourceModeInfo,
                            DxgkpPendingMonitorMode);
    return STATUS_SUCCESS;
}

/* ========================================================================
 * Monitor interface implementations
 * ====================================================================== */

static NTSTATUS APIENTRY
Monitor_AcquireMonitorSourceModeSet(
    _In_  D3DKMDT_ADAPTER                              hAdapter,
    _In_  D3DDDI_VIDEO_PRESENT_TARGET_ID               VideoPresentTargetId,
    _Out_ D3DKMDT_HMONITORSOURCEMODESET*               phMonitorSourceModeSet,
    _Out_ CONST DXGK_MONITORSOURCEMODESET_INTERFACE**  ppMonitorSourceModeSetInterface)
{
    PDXGKRNL_ADAPTER Adapter;
    PDXGKP_VIDPN     VidPn;

    if (phMonitorSourceModeSet == NULL || ppMonitorSourceModeSetInterface == NULL)
        return STATUS_INVALID_PARAMETER;

    *phMonitorSourceModeSet = NULL;
    *ppMonitorSourceModeSetInterface = NULL;

    Adapter = (PDXGKRNL_ADAPTER)hAdapter;
    if (Adapter == NULL)
        return STATUS_INVALID_PARAMETER;

    (VOID)KeWaitForSingleObject(&Adapter->VidPnMutex, Executive, KernelMode, FALSE, NULL);
    VidPn = (PDXGKP_VIDPN)Adapter->VidPn;
    if (!DxgkVidPnReference((D3DKMDT_HVIDPN)VidPn))
    {
        KeReleaseMutex(&Adapter->VidPnMutex, FALSE);
        return STATUS_GRAPHICS_INVALID_VIDEO_PRESENT_TARGET;
    }
    KeReleaseMutex(&Adapter->VidPnMutex, FALSE);

    if (DxgkVidPnTargetIndexFromId(VidPn, VideoPresentTargetId) == MAXULONG ||
        DxgkVidPnTargetIndexFromId(VidPn, VideoPresentTargetId) == MAXULONG ||
        VidPn->MonitorModeSets[DxgkVidPnTargetIndexFromId(VidPn, VideoPresentTargetId)] == NULL)
    {
        DxgkVidPnDestroy((D3DKMDT_HVIDPN)VidPn);
        return STATUS_GRAPHICS_INVALID_VIDEO_PRESENT_TARGET;
    }

    *phMonitorSourceModeSet = (D3DKMDT_HMONITORSOURCEMODESET)VidPn->MonitorModeSets[DxgkVidPnTargetIndexFromId(VidPn, VideoPresentTargetId)];
    *ppMonitorSourceModeSetInterface = &g_MonitorSourceModeSetInterface;
    return STATUS_SUCCESS;
}

static NTSTATUS APIENTRY
Monitor_ReleaseMonitorSourceModeSet(
    _In_ D3DKMDT_ADAPTER                               hAdapter,
    _In_ D3DKMDT_HMONITORSOURCEMODESET                 hMonitorSourceModeSet)
{
    PDXGKP_MONITOR_SOURCE_MODESET ModeSet = DxgkpMonitorModeSetFromHandle(hMonitorSourceModeSet);

    if (ModeSet == NULL || ModeSet->Owner == NULL || ModeSet->Owner->Adapter != (PDXGKRNL_ADAPTER)hAdapter)
        return STATUS_INVALID_PARAMETER;
    DxgkVidPnDestroy((D3DKMDT_HVIDPN)ModeSet->Owner);
    return STATUS_SUCCESS;
}

/* ---- Monitor descriptor and frequency-range sets ----------------------
 *
 * Built from the child's cached EDID: the base block, then each extension
 * block -- a block map as BLOCKMAP, anything else (CTA-861, DisplayID) as
 * OTHER.  The sets live in the child extension (see pnp.h): the monitor
 * interface has no call to release them.
 */

/* Finds the present video-output child for a target id.  ChildListLock. */
static PDXGK_CHILD_PDO_EXTENSION
DxgkpMonitorChildLocked(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _In_ D3DDDI_VIDEO_PRESENT_TARGET_ID TargetId)
{
    PLIST_ENTRY Entry;

    for (Entry = Adapter->ChildListHead.Flink;
         Entry != &Adapter->ChildListHead;
         Entry = Entry->Flink)
    {
        PDXGK_CHILD_PDO_EXTENSION Child =
            CONTAINING_RECORD(Entry, DXGK_CHILD_PDO_EXTENSION, ListEntry);

        if (Child->Present &&
            Child->Descriptor.ChildDeviceType == TypeVideoOutput &&
            Child->Descriptor.ChildUid == TargetId)
        {
            return Child;
        }
    }
    return NULL;
}

/*
 * Locates the monitor on a target and checks one is attached.  On success
 * ChildListLock is held; the caller releases it with *OldIrql.
 */
static NTSTATUS
DxgkpLockMonitorChild(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _In_ D3DDDI_VIDEO_PRESENT_TARGET_ID TargetId,
    _Out_ PDXGK_CHILD_PDO_EXTENSION *OutChild,
    _Out_ PKIRQL OldIrql)
{
    PDXGK_CHILD_PDO_EXTENSION Child;

    *OutChild = NULL;
    if (Adapter == NULL)
        return STATUS_INVALID_PARAMETER;

    KeAcquireSpinLock(&Adapter->ChildListLock, OldIrql);
    Child = DxgkpMonitorChildLocked(Adapter, TargetId);
    if (Child == NULL)
    {
        KeReleaseSpinLock(&Adapter->ChildListLock, *OldIrql);
        return STATUS_GRAPHICS_INVALID_VIDEO_PRESENT_TARGET;
    }
    if (!Child->Connected)
    {
        KeReleaseSpinLock(&Adapter->ChildListLock, *OldIrql);
        return STATUS_GRAPHICS_MONITOR_NOT_CONNECTED;
    }
    *OutChild = Child;
    return STATUS_SUCCESS;
}

static PDXGKP_MONITOR_DESCRIPTOR_SET
DxgkpDescriptorSetFromHandle(
    _In_ D3DKMDT_HMONITORDESCRIPTORSET hSet)
{
    PDXGKP_MONITOR_DESCRIPTOR_SET Set = (PDXGKP_MONITOR_DESCRIPTOR_SET)hSet;

    return (Set != NULL && Set->Signature == DXGKP_DESCRIPTOR_SET_SIGNATURE) ? Set : NULL;
}

static PDXGKP_MONITOR_FREQUENCY_RANGE_SET
DxgkpFrequencyRangeSetFromHandle(
    _In_ D3DKMDT_HMONITORFREQUENCYRANGESET hSet)
{
    PDXGKP_MONITOR_FREQUENCY_RANGE_SET Set = (PDXGKP_MONITOR_FREQUENCY_RANGE_SET)hSet;

    return (Set != NULL && Set->Signature == DXGKP_FREQUENCY_RANGE_SET_SIGNATURE) ? Set : NULL;
}

static NTSTATUS APIENTRY
DescriptorSet_GetNumDescriptors(
    IN_CONST_D3DKMDT_HMONITORDESCRIPTORSET hMonitorDescriptorSet,
    OUT_PSIZE_T_CONST pNumMonitorDescriptors)
{
    PDXGKP_MONITOR_DESCRIPTOR_SET Set = DxgkpDescriptorSetFromHandle(hMonitorDescriptorSet);

    if (Set == NULL || pNumMonitorDescriptors == NULL)
        return STATUS_INVALID_PARAMETER;
    *pNumMonitorDescriptors = Set->NumDescriptors;
    return STATUS_SUCCESS;
}

static NTSTATUS APIENTRY
DescriptorSet_AcquireFirstDescriptorInfo(
    IN_CONST_D3DKMDT_HMONITORDESCRIPTORSET hMonitorDescriptorSet,
    DEREF_OUT_CONST_PPD3DKMDT_MONITOR_DESCRIPTOR ppFirstMonitorDescriptorInfo)
{
    PDXGKP_MONITOR_DESCRIPTOR_SET Set = DxgkpDescriptorSetFromHandle(hMonitorDescriptorSet);

    if (Set == NULL || ppFirstMonitorDescriptorInfo == NULL)
        return STATUS_INVALID_PARAMETER;
    *ppFirstMonitorDescriptorInfo = NULL;
    if (Set->NumDescriptors == 0)
        return STATUS_GRAPHICS_DATASET_IS_EMPTY;
    *ppFirstMonitorDescriptorInfo = &Set->Descriptors[0];
    return STATUS_SUCCESS;
}

static NTSTATUS APIENTRY
DescriptorSet_AcquireNextDescriptorInfo(
    IN_CONST_D3DKMDT_HMONITORDESCRIPTORSET hMonitorDescriptorSet,
    IN_CONST_PD3DKMDT_MONITOR_DESCRIPTOR_CONST pMonitorDescriptorInfo,
    DEREF_OUT_CONST_PPD3DKMDT_MONITOR_DESCRIPTOR ppNextMonitorDescriptorInfo)
{
    PDXGKP_MONITOR_DESCRIPTOR_SET Set = DxgkpDescriptorSetFromHandle(hMonitorDescriptorSet);
    SIZE_T Index;

    if (Set == NULL || pMonitorDescriptorInfo == NULL || ppNextMonitorDescriptorInfo == NULL)
        return STATUS_INVALID_PARAMETER;
    *ppNextMonitorDescriptorInfo = NULL;
    if (pMonitorDescriptorInfo < &Set->Descriptors[0] ||
        pMonitorDescriptorInfo >= &Set->Descriptors[Set->NumDescriptors])
    {
        return STATUS_INVALID_PARAMETER;
    }
    Index = (SIZE_T)(pMonitorDescriptorInfo - &Set->Descriptors[0]) + 1;
    if (Index >= Set->NumDescriptors)
        return STATUS_GRAPHICS_NO_MORE_ELEMENTS_IN_DATASET;
    *ppNextMonitorDescriptorInfo = &Set->Descriptors[Index];
    return STATUS_SUCCESS;
}

/* Descriptors belong to the set, which belongs to the monitor; releasing
 * one only ends the caller's use of it. */
static NTSTATUS APIENTRY
DescriptorSet_ReleaseDescriptorInfo(
    IN_CONST_D3DKMDT_HMONITORDESCRIPTORSET hMonitorDescriptorSet,
    IN_CONST_PD3DKMDT_MONITOR_DESCRIPTOR_CONST pMonitorDescriptorInfo)
{
    PDXGKP_MONITOR_DESCRIPTOR_SET Set = DxgkpDescriptorSetFromHandle(hMonitorDescriptorSet);

    if (Set == NULL || pMonitorDescriptorInfo == NULL ||
        pMonitorDescriptorInfo < &Set->Descriptors[0] ||
        pMonitorDescriptorInfo >= &Set->Descriptors[Set->NumDescriptors])
    {
        return STATUS_INVALID_PARAMETER;
    }
    return STATUS_SUCCESS;
}

static CONST DXGK_MONITORDESCRIPTORSET_INTERFACE g_MonitorDescriptorSetInterface =
{
    DescriptorSet_GetNumDescriptors,
    DescriptorSet_AcquireFirstDescriptorInfo,
    DescriptorSet_AcquireNextDescriptorInfo,
    DescriptorSet_ReleaseDescriptorInfo
};

static NTSTATUS APIENTRY
FrequencyRangeSet_GetNumFrequencyRanges(
    IN_CONST_D3DKMDT_HMONITORFREQUENCYRANGESET hMonitorFrequencyRangeSet,
    OUT_PSIZE_T_CONST pNumMonitorFrequencyRanges)
{
    PDXGKP_MONITOR_FREQUENCY_RANGE_SET Set = DxgkpFrequencyRangeSetFromHandle(hMonitorFrequencyRangeSet);

    if (Set == NULL || pNumMonitorFrequencyRanges == NULL)
        return STATUS_INVALID_PARAMETER;
    *pNumMonitorFrequencyRanges = Set->NumRanges;
    return STATUS_SUCCESS;
}

static NTSTATUS APIENTRY
FrequencyRangeSet_AcquireFirstFrequencyRangeInfo(
    IN_CONST_D3DKMDT_HMONITORFREQUENCYRANGESET hMonitorFrequencyRangeSet,
    DEREF_OUT_CONST_PPD3DKMDT_MONITOR_FREQUENCY_RANGE ppFirstMonitorFrequencyRangeInfo)
{
    PDXGKP_MONITOR_FREQUENCY_RANGE_SET Set = DxgkpFrequencyRangeSetFromHandle(hMonitorFrequencyRangeSet);

    if (Set == NULL || ppFirstMonitorFrequencyRangeInfo == NULL)
        return STATUS_INVALID_PARAMETER;
    *ppFirstMonitorFrequencyRangeInfo = NULL;
    if (Set->NumRanges == 0)
        return STATUS_GRAPHICS_DATASET_IS_EMPTY;
    *ppFirstMonitorFrequencyRangeInfo = &Set->Ranges[0];
    return STATUS_SUCCESS;
}

static NTSTATUS APIENTRY
FrequencyRangeSet_AcquireNextFrequencyRangeInfo(
    IN_CONST_D3DKMDT_HMONITORFREQUENCYRANGESET hMonitorFrequencyRangeSet,
    IN_CONST_PD3DKMDT_MONITOR_FREQUENCY_RANGE_CONST pMonitorFrequencyRangeInfo,
    DEREF_OUT_CONST_PPD3DKMDT_MONITOR_FREQUENCY_RANGE ppNextMonitorFrequencyRangeInfo)
{
    PDXGKP_MONITOR_FREQUENCY_RANGE_SET Set = DxgkpFrequencyRangeSetFromHandle(hMonitorFrequencyRangeSet);
    SIZE_T Index;

    if (Set == NULL || pMonitorFrequencyRangeInfo == NULL || ppNextMonitorFrequencyRangeInfo == NULL)
        return STATUS_INVALID_PARAMETER;
    *ppNextMonitorFrequencyRangeInfo = NULL;
    if (pMonitorFrequencyRangeInfo < &Set->Ranges[0] ||
        pMonitorFrequencyRangeInfo >= &Set->Ranges[Set->NumRanges])
    {
        return STATUS_INVALID_PARAMETER;
    }
    Index = (SIZE_T)(pMonitorFrequencyRangeInfo - &Set->Ranges[0]) + 1;
    if (Index >= Set->NumRanges)
        return STATUS_GRAPHICS_NO_MORE_ELEMENTS_IN_DATASET;
    *ppNextMonitorFrequencyRangeInfo = &Set->Ranges[Index];
    return STATUS_SUCCESS;
}

static NTSTATUS APIENTRY
FrequencyRangeSet_ReleaseFrequencyRangeInfo(
    IN_CONST_D3DKMDT_HMONITORFREQUENCYRANGESET hMonitorFrequencyRangeSet,
    IN_CONST_PD3DKMDT_MONITOR_FREQUENCY_RANGE_CONST pMonitorFrequencyRangeInfo)
{
    PDXGKP_MONITOR_FREQUENCY_RANGE_SET Set = DxgkpFrequencyRangeSetFromHandle(hMonitorFrequencyRangeSet);

    if (Set == NULL || pMonitorFrequencyRangeInfo == NULL ||
        pMonitorFrequencyRangeInfo < &Set->Ranges[0] ||
        pMonitorFrequencyRangeInfo >= &Set->Ranges[Set->NumRanges])
    {
        return STATUS_INVALID_PARAMETER;
    }
    return STATUS_SUCCESS;
}

static CONST DXGK_MONITORFREQUENCYRANGESET_INTERFACE g_MonitorFrequencyRangeSetInterface =
{
    FrequencyRangeSet_GetNumFrequencyRanges,
    FrequencyRangeSet_AcquireFirstFrequencyRangeInfo,
    FrequencyRangeSet_AcquireNextFrequencyRangeInfo,
    FrequencyRangeSet_ReleaseFrequencyRangeInfo
};

/*
 * Reads the Display Range Limits descriptor (tag 0xFD) from an EDID base
 * block.  EDID 1.4 byte 4 adds 255 to a rate when its offset bits say so:
 * bits 1:0 for vertical, 3:2 for horizontal (binary 10 = maximum only,
 * 11 = minimum and maximum).
 */
static BOOLEAN
DxgkpEdidRangeLimits(
    _In_reads_bytes_(128) CONST UCHAR *Edid,
    _Out_ D3DKMDT_MONITOR_FREQUENCY_RANGE *Range)
{
    static CONST ULONG DescriptorOffsets[] = { 54, 72, 90, 108 };
    ULONG i;

    RtlZeroMemory(Range, sizeof(*Range));
    for (i = 0; i < RTL_NUMBER_OF(DescriptorOffsets); i++)
    {
        CONST UCHAR *D = Edid + DescriptorOffsets[i];
        ULONG MinV, MaxV, MinH, MaxH;

        if (D[0] != 0 || D[1] != 0 || D[2] != 0 || D[3] != 0xFD)
            continue;

        MinV = D[5];
        MaxV = D[6];
        MinH = D[7];
        MaxH = D[8];
        if ((D[4] & 0x03) == 0x02 || (D[4] & 0x03) == 0x03)
            MaxV += 255;
        if ((D[4] & 0x03) == 0x03)
            MinV += 255;
        if ((D[4] & 0x0C) == 0x08 || (D[4] & 0x0C) == 0x0C)
            MaxH += 255;
        if ((D[4] & 0x0C) == 0x0C)
            MinH += 255;

        /* A range no monitor can have constrains nothing. */
        if (MinV == 0 || MinV > MaxV || MinH == 0 || MinH > MaxH || D[9] == 0)
            return FALSE;

        Range->Origin = D3DKMDT_MCO_MONITORDESCRIPTOR;
        Range->RangeLimits.MinVSyncFreq.Numerator = MinV;
        Range->RangeLimits.MinVSyncFreq.Denominator = 1;
        Range->RangeLimits.MaxVSyncFreq.Numerator = MaxV;
        Range->RangeLimits.MaxVSyncFreq.Denominator = 1;
        Range->RangeLimits.MinHSyncFreq.Numerator = MinH * 1000;
        Range->RangeLimits.MinHSyncFreq.Denominator = 1;
        Range->RangeLimits.MaxHSyncFreq.Numerator = MaxH * 1000;
        Range->RangeLimits.MaxHSyncFreq.Denominator = 1;
        Range->ConstraintType = D3DKMDT_MFRC_MAXPIXELRATE;
        Range->Constraint.MaxPixelRate = (SIZE_T)D[9] * 10000000;
        return TRUE;
    }
    return FALSE;
}

static NTSTATUS APIENTRY
Monitor_GetMonitorFrequencyRangeSet(
    _In_ D3DKMDT_ADAPTER hAdapter,
    _In_ D3DDDI_VIDEO_PRESENT_TARGET_ID VideoPresentTargetId,
    _Out_ D3DKMDT_HMONITORFREQUENCYRANGESET *phMonitorFrequencyRangeSet,
    _Out_ CONST DXGK_MONITORFREQUENCYRANGESET_INTERFACE
        **ppMonitorFrequencyRangeSetInterface)
{
    PDXGKRNL_ADAPTER Adapter = (PDXGKRNL_ADAPTER)hAdapter;
    PDXGK_CHILD_PDO_EXTENSION Child;
    PDXGKP_MONITOR_FREQUENCY_RANGE_SET Set;
    KIRQL OldIrql;
    NTSTATUS Status;

    if (phMonitorFrequencyRangeSet == NULL ||
        ppMonitorFrequencyRangeSetInterface == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }
    *phMonitorFrequencyRangeSet = NULL;
    *ppMonitorFrequencyRangeSetInterface = NULL;

    Status = DxgkpLockMonitorChild(Adapter, VideoPresentTargetId, &Child, &OldIrql);
    if (!NT_SUCCESS(Status))
        return Status;

    /* A monitor without a range descriptor -- or without an EDID -- has an
     * empty set, not a missing one. */
    Set = &Child->FrequencyRangeSet;
    Set->Signature = DXGKP_FREQUENCY_RANGE_SET_SIGNATURE;
    Set->NumRanges = (Child->EdidValid &&
                      DxgkpEdidRangeLimits(Child->Edid, &Set->Ranges[0])) ? 1 : 0;
    KeReleaseSpinLock(&Adapter->ChildListLock, OldIrql);

    *phMonitorFrequencyRangeSet = (D3DKMDT_HMONITORFREQUENCYRANGESET)Set;
    *ppMonitorFrequencyRangeSetInterface = &g_MonitorFrequencyRangeSetInterface;
    return STATUS_SUCCESS;
}

static NTSTATUS APIENTRY
Monitor_GetMonitorDescriptorSet(
    _In_ D3DKMDT_ADAPTER hAdapter,
    _In_ D3DDDI_VIDEO_PRESENT_TARGET_ID VideoPresentTargetId,
    _Out_ D3DKMDT_HMONITORDESCRIPTORSET *phMonitorDescriptorSet,
    _Out_ CONST DXGK_MONITORDESCRIPTORSET_INTERFACE
        **ppMonitorDescriptorSetInterface)
{
    PDXGKRNL_ADAPTER Adapter = (PDXGKRNL_ADAPTER)hAdapter;
    PDXGK_CHILD_PDO_EXTENSION Child;
    PDXGKP_MONITOR_DESCRIPTOR_SET Set;
    KIRQL OldIrql;
    NTSTATUS Status;
    UCHAR Block;

    if (phMonitorDescriptorSet == NULL ||
        ppMonitorDescriptorSetInterface == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }
    *phMonitorDescriptorSet = NULL;
    *ppMonitorDescriptorSetInterface = NULL;

    Status = DxgkpLockMonitorChild(Adapter, VideoPresentTargetId, &Child, &OldIrql);
    if (!NT_SUCCESS(Status))
        return Status;

    Set = &Child->DescriptorSet;
    RtlZeroMemory(Set, sizeof(*Set));
    Set->Signature = DXGKP_DESCRIPTOR_SET_SIGNATURE;
    if (Child->EdidValid)
    {
        RtlCopyMemory(Set->Data[0], Child->Edid, 128);
        Set->Descriptors[0].Id = 0;
        Set->Descriptors[0].Type = D3DKMDT_MDT_VESA_EDID_V1_BASEBLOCK;
        Set->Descriptors[0].DataSize = 128;
        Set->Descriptors[0].pData = Set->Data[0];
        Set->Descriptors[0].Origin = D3DKMDT_MCO_MONITORDESCRIPTOR;
        Set->NumDescriptors = 1;

        for (Block = 0;
             Block < Child->EdidExtensionCount && Block < DXGKP_EDID_MAX_EXTENSIONS;
             Block++)
        {
            SIZE_T Slot = Set->NumDescriptors;

            RtlCopyMemory(Set->Data[Slot], Child->EdidExtensions[Block], 128);
            Set->Descriptors[Slot].Id = (D3DKMDT_MONITOR_DESCRIPTOR_ID)Slot;
            Set->Descriptors[Slot].Type = Set->Data[Slot][0] == 0xF0 ?
                                              D3DKMDT_MDT_VESA_EDID_V1_BLOCKMAP :
                                              D3DKMDT_MDT_OTHER;
            Set->Descriptors[Slot].DataSize = 128;
            Set->Descriptors[Slot].pData = Set->Data[Slot];
            Set->Descriptors[Slot].Origin = D3DKMDT_MCO_MONITORDESCRIPTOR;
            Set->NumDescriptors++;
        }
    }
    KeReleaseSpinLock(&Adapter->ChildListLock, OldIrql);

    *phMonitorDescriptorSet = (D3DKMDT_HMONITORDESCRIPTORSET)Set;
    *ppMonitorDescriptorSetInterface = &g_MonitorDescriptorSetInterface;
    return STATUS_SUCCESS;
}

/*
 * Additional monitor modes.  Nothing in this stack sources modes beyond the
 * monitor's descriptor -- there is no override database -- so the set is
 * empty, which is an answer rather than a failure.
 */
static NTSTATUS APIENTRY
Monitor_GetAdditionalMonitorModeSet(
    _In_ D3DKMDT_ADAPTER hAdapter,
    _In_ D3DDDI_VIDEO_PRESENT_TARGET_ID VideoPresentTargetId,
    _Out_ UINT *pNumberModes,
    _Out_ DXGK_TARGETMODE_DETAIL_TIMING **ppAdditionalModesSet)
{
    PDXGKRNL_ADAPTER Adapter = (PDXGKRNL_ADAPTER)hAdapter;
    PDXGK_CHILD_PDO_EXTENSION Child;
    KIRQL OldIrql;
    NTSTATUS Status;

    if (pNumberModes == NULL || ppAdditionalModesSet == NULL)
        return STATUS_INVALID_PARAMETER;
    *pNumberModes = 0;
    *ppAdditionalModesSet = NULL;

    Status = DxgkpLockMonitorChild(Adapter, VideoPresentTargetId, &Child, &OldIrql);
    if (!NT_SUCCESS(Status))
        return Status;
    KeReleaseSpinLock(&Adapter->ChildListLock, OldIrql);
    return STATUS_SUCCESS;
}

static NTSTATUS APIENTRY
Monitor_ReleaseAdditionalMonitorModeSet(
    _In_ D3DKMDT_ADAPTER hAdapter,
    _In_ D3DDDI_VIDEO_PRESENT_TARGET_ID VideoPresentTargetId,
    _In_ CONST DXGK_TARGETMODE_DETAIL_TIMING *pAdditionalModesSet)
{
    UNREFERENCED_PARAMETER(hAdapter);
    UNREFERENCED_PARAMETER(VideoPresentTargetId);

    /* The only set ever handed out is the empty one, whose pointer is NULL. */
    return pAdditionalModesSet == NULL ? STATUS_SUCCESS : STATUS_INVALID_PARAMETER;
}

/* ========================================================================
 * D3DKMT API implementations
 * ====================================================================== */

/* SharedPrimaryMutex and shared-surface rundown are held by the caller. */
static VOID
DxgkpDestroySharedPrimaryAllocationLocked(
    _In_ PDXGKRNL_ADAPTER Adapter)
{
    PDXGKVMM_RESOURCE Resource = NULL;

    if (Adapter->SharedPrimaryAllocationHandle != NULL)
    {
        /*
         * Display-only adapters point Adapter->ShadowFb at this allocation's
         * CPU VA. Shared-surface rundown above drained every retained snapshot,
         * so clear the allocation-backed alias before destroying its mapping.
         */
        if (Adapter->MiniportContext != NULL &&
            Adapter->MiniportContext->IsDisplayOnlyDriver &&
            !Adapter->ShadowFbPoolOwned)
        {
            Adapter->ShadowFb = NULL;
            Adapter->ShadowFbPitch = 0;
            Adapter->ShadowFbSize = 0;
            Adapter->ShadowFbPoolOwned = FALSE;
        }

        if (Adapter->SharedPrimaryResourceHandle == 0)
            DxgkVidMmDestroyAllocation(Adapter, Adapter->SharedPrimaryAllocationHandle);
        Adapter->SharedPrimaryAllocationHandle = NULL;
    }

    if (Adapter->SharedPrimaryResourceHandle != 0)
    {
        if (NT_SUCCESS(DxgkVidMmReferenceResource(Adapter->SharedPrimaryResourceHandle, FALSE, NULL, &Resource)))
        {
            if (Resource->Adapter != Adapter)
            {
                DxgkVidMmDereferenceResource(Resource);
            }
            else
            {
                DxgkVidMmDereferenceResource(Resource);
                DxgkpVidMmDestroyResourceWrapper(Adapter, Resource);
            }
            Resource = NULL;
        }
        Adapter->SharedPrimaryResourceHandle = 0;
    }

    Adapter->SharedPrimaryGlobalShareHandle = 0;
    Adapter->SharedPrimaryVidPnSourceId = 0;
    Adapter->SharedPrimaryWidth = 0;
    Adapter->SharedPrimaryHeight = 0;
    Adapter->SharedPrimaryFormat = 0;
    Adapter->SharedPrimaryIsGopBacked = FALSE;
}

static VOID
DxgkpDestroySharedPrimaryLocked(
    _In_ PDXGKRNL_ADAPTER Adapter)
{
    PDXGKVMM_RESOURCE Resource = NULL;

    if (Adapter == NULL)
        return;

    DxgkpBeginSharedSurfaceMutationLocked(Adapter);

    DxgkpDestroySharedPrimaryAllocationLocked(Adapter);

    if (Adapter->SharedShadowAllocationHandle != NULL)
    {
        /*
         * Full-WDDM adapters reuse Adapter->ShadowFb as the live CPU VA for
         * the shadow allocation. Rundown makes clearing it here generation-safe.
         */
        if (Adapter->MiniportContext != NULL && !Adapter->MiniportContext->IsDisplayOnlyDriver && !Adapter->ShadowFbPoolOwned)
        {
            Adapter->ShadowFb = NULL;
            Adapter->ShadowFbPitch = 0;
            Adapter->ShadowFbSize = 0;
            Adapter->ShadowFbPoolOwned = FALSE;
        }

        if (Adapter->SharedShadowResourceHandle == 0)
            DxgkVidMmDestroyAllocation(Adapter, Adapter->SharedShadowAllocationHandle);
        Adapter->SharedShadowAllocationHandle = NULL;
    }

    if (Adapter->SharedShadowResourceHandle != 0)
    {
        if (NT_SUCCESS(DxgkVidMmReferenceResource(Adapter->SharedShadowResourceHandle, FALSE, NULL, &Resource)))
        {
            if (Resource->Adapter != Adapter)
            {
                DxgkVidMmDereferenceResource(Resource);
            }
            else
            {
                DxgkVidMmDereferenceResource(Resource);
                DxgkpVidMmDestroyResourceWrapper(Adapter, Resource);
            }
            Resource = NULL;
        }
        Adapter->SharedShadowResourceHandle = 0;
    }

    Adapter->SharedShadowGlobalShareHandle = 0;
    Adapter->SharedShadowWidth = 0;
    Adapter->SharedShadowHeight = 0;
    Adapter->SharedShadowPitch = 0;
    Adapter->SharedShadowFormat = 0;
    DxgkpEndSharedSurfaceMutationLocked(Adapter);
}

static NTSTATUS
DxgkpCreateGdiSurface(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _In_opt_ PDXGKRNL_DEVICE Device,
    _Inout_ PDXGK_REDIRECTION_SURFACE_CREATE Create,
    _In_ D3DKMDT_GDISURFACETYPE SurfaceType)
{
    DXGKARG_GETSTANDARDALLOCATIONDRIVERDATA QueryArgs;
    D3DKMDT_GDISURFACEDATA SurfaceData;
    DWM_DX_SHARED_SURFACE_INFO RuntimeInfo;
    DXGK_ALLOCATIONINFO AllocInfo;
    DXGK_CREATEALLOCATIONFLAGS CreateFlags;
    HANDLE AllocationHandle = NULL;
    HANDLE MiniportResourceHandle = NULL;
    PDXGKVMM_ALLOCATION Allocation = NULL;
    PDXGKVMM_RESOURCE Resource = NULL;
    PVOID AllocationPrivateData = NULL;
    PVOID ResourcePrivateData = NULL;
    UINT AllocationPrivateDataSize = 0;
    UINT ResourcePrivateDataSize = 0;
    PVOID CpuAddress = NULL;
    ULONGLONG RequiredBytes;
    NTSTATUS Status;

    if (Adapter == NULL || (Device != NULL && Device->Adapter != Adapter) ||
        Create == NULL ||
        Create->StructSize != sizeof(*Create) || Create->Flags != 0 ||
        Create->Width == 0 || Create->Height == 0 ||
        Create->Format != DWM_DX_FORMAT_B8G8R8A8_UNORM ||
        Create->Width > MAXULONG / sizeof(ULONG))
    {
        return STATUS_INVALID_PARAMETER;
    }

    RequiredBytes = (ULONGLONG)Create->Width * sizeof(ULONG) * Create->Height;
    if (RequiredBytes > MAXULONG)
        return STATUS_INTEGER_OVERFLOW;

    Create->Pitch = 0;
    Create->AllocationBytes = 0;
    Create->AllocationHandle = 0;
    Create->ResourceHandle = 0;
    Create->GlobalShare = 0;
    Create->CpuAddress = 0;

    if (Adapter->MiniportContext == NULL ||
        Adapter->MiniportContext->IsDisplayOnlyDriver ||
        DXGK_CB_FULL(Adapter, DxgkDdiGetStandardAllocationDriverData) == NULL)
    {
        return STATUS_NOT_SUPPORTED;
    }

    RtlZeroMemory(&SurfaceData, sizeof(SurfaceData));
    SurfaceData.Width = Create->Width;
    SurfaceData.Height = Create->Height;
    SurfaceData.Format = D3DDDIFMT_X8R8G8B8;
    SurfaceData.Type = SurfaceType;
    SurfaceData.Pitch = Create->Width * sizeof(ULONG);

    RtlZeroMemory(&QueryArgs, sizeof(QueryArgs));
    QueryArgs.StandardAllocationType = DXGK_STDALLOCATION_GDISURFACE;
    QueryArgs.pCreateGdiSurfaceData = &SurfaceData;

    if (!DxgkAcquireKmdCall(Adapter))
        return STATUS_DELETE_PENDING;
    Status = DXGK_CB_FULL(Adapter, DxgkDdiGetStandardAllocationDriverData)(
        Adapter->MiniportDeviceContext, &QueryArgs);
    DxgkReleaseKmdCall(Adapter);
    if (!NT_SUCCESS(Status))
        return Status;

    AllocationPrivateDataSize = QueryArgs.AllocationPrivateDriverDataSize;
    ResourcePrivateDataSize = QueryArgs.ResourcePrivateDriverDataSize;
    if (AllocationPrivateDataSize > DXGKP_STDALLOC_MAX_PRIVATE_SIZE ||
        ResourcePrivateDataSize > DXGKP_STDALLOC_MAX_PRIVATE_SIZE)
    {
        return STATUS_INVALID_PARAMETER;
    }

    if (AllocationPrivateDataSize != 0)
    {
        AllocationPrivateData = ExAllocatePoolWithTag(
            PagedPool, AllocationPrivateDataSize, TAG_DXGK_DISPLAY);
        if (AllocationPrivateData == NULL)
        {
            Status = STATUS_INSUFFICIENT_RESOURCES;
            goto Cleanup;
        }
    }
    if (ResourcePrivateDataSize != 0)
    {
        ResourcePrivateData = ExAllocatePoolWithTag(
            PagedPool, ResourcePrivateDataSize, TAG_DXGK_DISPLAY);
        if (ResourcePrivateData == NULL)
        {
            Status = STATUS_INSUFFICIENT_RESOURCES;
            goto Cleanup;
        }
    }

    QueryArgs.pAllocationPrivateDriverData = AllocationPrivateData;
    QueryArgs.AllocationPrivateDriverDataSize = AllocationPrivateDataSize;
    QueryArgs.pResourcePrivateDriverData = ResourcePrivateData;
    QueryArgs.ResourcePrivateDriverDataSize = ResourcePrivateDataSize;
    if (!DxgkAcquireKmdCall(Adapter))
    {
        Status = STATUS_DELETE_PENDING;
        goto Cleanup;
    }
    Status = DXGK_CB_FULL(Adapter, DxgkDdiGetStandardAllocationDriverData)(
        Adapter->MiniportDeviceContext, &QueryArgs);
    DxgkReleaseKmdCall(Adapter);
    if (!NT_SUCCESS(Status))
        goto Cleanup;
    if (QueryArgs.AllocationPrivateDriverDataSize != AllocationPrivateDataSize ||
        QueryArgs.ResourcePrivateDriverDataSize != ResourcePrivateDataSize ||
        SurfaceData.Pitch < Create->Width * sizeof(ULONG) ||
        SurfaceData.Pitch > MAXULONG / Create->Height)
    {
        Status = STATUS_INVALID_PARAMETER;
        goto Cleanup;
    }

    RtlZeroMemory(&AllocInfo, sizeof(AllocInfo));
    AllocInfo.pPrivateDriverData = AllocationPrivateData;
    AllocInfo.PrivateDriverDataSize = AllocationPrivateDataSize;
    AllocInfo.Size = (SIZE_T)SurfaceData.Pitch * SurfaceData.Height;

    RtlZeroMemory(&CreateFlags, sizeof(CreateFlags));
    CreateFlags.Resource = 1;
    Status = DxgkVidMmCreateAllocation(
        Adapter, Device, &AllocInfo, ResourcePrivateData,
        ResourcePrivateDataSize, NULL, CreateFlags, D3DDDI_ID_UNINITIALIZED,
        &AllocationHandle, &MiniportResourceHandle);
    if (!NT_SUCCESS(Status))
        goto Cleanup;

    Status = DxgkVidMmReferenceAllocation(
        AllocationHandle, Adapter, NULL, &Allocation);
    if (!NT_SUCCESS(Status))
        goto Cleanup;
    /*
     * CDD keeps CpuAddress in its SURFOBJ for the bitmap's lifetime.  Its
     * CPU mapping must therefore address the same backing that a shared GPU
     * consumer reads.  Evicting here maps the system copy permanently, and
     * later MakeResident moves only the GPU view: subsequent GDI writes
     * would never reach the imported texture.
     *
     * Publish GPU-resident backing for both CDD bitmaps and user-owned
     * redirection textures.  An ICD can CPU-map the latter before DWM's
     * first GPU submission.  Forcing eviction at creation leaves that map
     * pinning the system copy, so admission cannot move it back into a
     * segment until the ICD unlocks it (possibly only at destruction).
     * Normal residency policy still applies after publication; do not
     * introduce an unconditional eviction before the first producer access.
     */
    if (!Allocation->Resident)
    {
        Status = DxgkVidMmMakeResident(Allocation, Adapter);
        if (!NT_SUCCESS(Status))
            goto Cleanup;
    }

    RtlZeroMemory(&RuntimeInfo, sizeof(RuntimeInfo));
    RuntimeInfo.Magic = DWM_DX_SURFACE_INFO_MAGIC;
    RuntimeInfo.Version = DWM_DX_SURFACE_INFO_VERSION;
    RuntimeInfo.Width = Create->Width;
    RuntimeInfo.Height = Create->Height;
    RuntimeInfo.Pitch = SurfaceData.Pitch;
    RuntimeInfo.Format = Create->Format;
    Resource = DxgkVidMmCreateResourceWrapper(
        Adapter, Device, MiniportResourceHandle, 0, TRUE,
        &RuntimeInfo, sizeof(RuntimeInfo),
        ResourcePrivateData, ResourcePrivateDataSize);
    if (Resource == NULL)
    {
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto Cleanup;
    }

    Status = DxgkVidMmAttachAllocationToResource(Resource, Allocation);
    if (!NT_SUCCESS(Status))
        goto Cleanup;
    Allocation->MiniportResourceHandle = NULL;
    Allocation->DestroyMiniportResource = FALSE;

    /* CDD paints its kernel-owned redirection bitmap through CpuAddress.  A
     * user DWM/ICD resource is instead a GPU texture opened by global share;
     * production KMDs legitimately describe that texture as non-CPU-visible. */
    if (SurfaceType == D3DKMDT_GDISURFACE_STAGING_CPUVISIBLE)
    {
        Status = DxgkVidMmMapAllocationCpu(Allocation, &CpuAddress);
        if (!NT_SUCCESS(Status) || CpuAddress == NULL)
        {
            if (NT_SUCCESS(Status))
                Status = STATUS_UNSUCCESSFUL;
            goto Cleanup;
        }
    }

    Create->Pitch = SurfaceData.Pitch;
    Create->AllocationBytes = AllocInfo.Size;
    Create->AllocationHandle = (ULONGLONG)(ULONG_PTR)AllocationHandle;
    Create->ResourceHandle = Resource->Handle;
    Create->GlobalShare = Resource->GlobalShareHandle;
    Create->CpuAddress = (ULONGLONG)(ULONG_PTR)CpuAddress;
    Status = STATUS_SUCCESS;

Cleanup:
    if (Allocation != NULL)
        DxgkVidMmDereferenceAllocation(Allocation);
    if (!NT_SUCCESS(Status))
    {
        if (Resource != NULL)
        {
            (VOID)DxgkpVidMmDestroyResourceWrapper(Adapter, Resource);
            Resource = NULL;
            AllocationHandle = NULL;
        }
        else if (AllocationHandle != NULL)
        {
            (VOID)DxgkVidMmDestroyAllocation(Adapter, AllocationHandle);
        }
    }
    if (AllocationPrivateData != NULL)
        ExFreePoolWithTag(AllocationPrivateData, TAG_DXGK_DISPLAY);
    if (ResourcePrivateData != NULL)
        ExFreePoolWithTag(ResourcePrivateData, TAG_DXGK_DISPLAY);
    return Status;
}

NTSTATUS
DxgkCreateRedirectionSurface(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _In_opt_ PDXGKRNL_DEVICE Device,
    _Inout_ PDXGK_REDIRECTION_SURFACE_CREATE Create)
{
    NTSTATUS Status;

    /* CDD paints directly into its shared bitmap for the bitmap's lifetime.
     * Ask the miniport for the OS CPU-visible texture contract; TEXTURE is
     * explicitly GPU-only and cannot supply that permanent CPU mapping.
     * User-owned redirection textures are rendered through the UMD. */
    if (Device != NULL && !DxgkIsDisplayAdapter(Adapter))
    {
        Status = DxgkpCreateGdiSurface(Adapter, Device, Create, D3DKMDT_GDISURFACE_STAGING_CPUVISIBLE);
        if (NT_SUCCESS(Status))
            return Status;
    }
    return DxgkpCreateGdiSurface(Adapter, Device, Create,
                                Device == NULL ?
                                    D3DKMDT_GDISURFACE_STAGING_CPUVISIBLE :
                                    D3DKMDT_GDISURFACE_TEXTURE);
}

NTSTATUS
DxgkCreateCaptureSurface(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _In_ PDXGKRNL_DEVICE Device,
    _Inout_ PDXGK_REDIRECTION_SURFACE_CREATE Create)
{
    return DxgkpCreateGdiSurface(Adapter, Device, Create,
                                D3DKMDT_GDISURFACE_STAGING_CPUVISIBLE);
}

NTSTATUS
DxgkDestroyRedirectionSurface(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _In_ CONST DXGK_REDIRECTION_SURFACE_DESTROY *Destroy)
{
    PDXGKVMM_ALLOCATION Allocation = NULL;
    PDXGKVMM_RESOURCE Resource = NULL;
    NTSTATUS Status;

    if (Adapter == NULL || Destroy == NULL ||
        Destroy->StructSize != sizeof(*Destroy) || Destroy->Flags != 0 ||
        Destroy->AllocationHandle == 0 || Destroy->ResourceHandle == 0 ||
        Destroy->GlobalShare == 0)
    {
        return STATUS_INVALID_PARAMETER;
    }

    Status = DxgkVidMmReferenceResource(
        Destroy->ResourceHandle, FALSE, NULL, &Resource);
    if (!NT_SUCCESS(Status))
        return STATUS_INVALID_PARAMETER;
    Status = DxgkVidMmReferenceAllocation(
        (HANDLE)(ULONG_PTR)Destroy->AllocationHandle,
        Adapter, NULL, &Allocation);
    if (!NT_SUCCESS(Status))
    {
        DxgkVidMmDereferenceResource(Resource);
        return STATUS_INVALID_PARAMETER;
    }
    if (Resource->Adapter != Adapter ||
        Resource->GlobalShareHandle != Destroy->GlobalShare ||
        Resource->AllocationCount != 1 || Allocation->Resource != Resource)
    {
        Status = STATUS_INVALID_PARAMETER;
    }
    else
    {
        (VOID)KeWaitForSingleObject(&Allocation->ResidencyLock,
                                    Executive, KernelMode, FALSE, NULL);
        InterlockedExchangePointer(
            (PVOID volatile *)&Allocation->RedirectionSurfaceHandle,
            NULL);
        RtlZeroMemory(Allocation->RedirectionSubmittedFenceId,
                      sizeof(Allocation->RedirectionSubmittedFenceId));
        KeReleaseMutex(&Allocation->ResidencyLock, FALSE);
        Status = STATUS_SUCCESS;
    }
    DxgkVidMmDereferenceAllocation(Allocation);
    if (NT_SUCCESS(Status))
        Status = DxgkpVidMmDestroyResourceWrapper(Adapter, Resource);
    DxgkVidMmDereferenceResource(Resource);
    return Status;
}

static NTSTATUS
DxgkpEnsureSharedShadowSurfaceLocked(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _In_ D3DDDI_VIDEO_PRESENT_SOURCE_ID VidPnSourceId)
{
    DXGKARG_GETSTANDARDALLOCATIONDRIVERDATA QueryArgs;
    D3DKMDT_SHADOWSURFACEDATA SurfaceData;
    DXGK_ALLOCATIONINFO AllocInfo;
    DXGK_CREATEALLOCATIONFLAGS CreateFlags;
    HANDLE AllocationHandle = NULL;
    HANDLE MiniportResourceHandle = NULL;
    PDXGKVMM_ALLOCATION Allocation = NULL;
    PDXGKVMM_RESOURCE Resource = NULL;
    PVOID AllocationPrivateData = NULL;
    PVOID ResourcePrivateData = NULL;
    UINT AllocationPrivateDataSize = 0;
    UINT ResourcePrivateDataSize = 0;
    PVOID ShadowVa = NULL;
    BOOLEAN StartTimer = FALSE;
    NTSTATUS Status;

    if (Adapter == NULL)
        return STATUS_INVALID_PARAMETER;

    if (Adapter->SharedShadowAllocationHandle != NULL &&
        Adapter->SharedShadowWidth == Adapter->CommittedWidth &&
        Adapter->SharedShadowHeight == Adapter->CommittedHeight)
    {
        return STATUS_SUCCESS;
    }

    if (DXGK_CB_FULL(Adapter, DxgkDdiGetStandardAllocationDriverData) == NULL)
        return STATUS_NOT_SUPPORTED;

    RtlZeroMemory(&SurfaceData, sizeof(SurfaceData));
    SurfaceData.Width = Adapter->CommittedWidth;
    SurfaceData.Height = Adapter->CommittedHeight;
    SurfaceData.Format = D3DDDIFMT_X8R8G8B8;
    SurfaceData.Pitch = Adapter->CommittedWidth * 4;

    RtlZeroMemory(&QueryArgs, sizeof(QueryArgs));
    QueryArgs.StandardAllocationType = DXGK_STDALLOCATION_SHADOWSURFACE;
    QueryArgs.pCreateShadowSurfaceData = &SurfaceData;

    if (!DxgkAcquireKmdCall(Adapter))
        return STATUS_DELETE_PENDING;
    Status = DXGK_CB_FULL(Adapter, DxgkDdiGetStandardAllocationDriverData)(
        Adapter->MiniportDeviceContext,
        &QueryArgs);
    DxgkReleaseKmdCall(Adapter);
    if (!NT_SUCCESS(Status))
        return Status;

    AllocationPrivateDataSize = QueryArgs.AllocationPrivateDriverDataSize;
    ResourcePrivateDataSize = QueryArgs.ResourcePrivateDriverDataSize;
    if (AllocationPrivateDataSize > DXGKP_STDALLOC_MAX_PRIVATE_SIZE ||
        ResourcePrivateDataSize > DXGKP_STDALLOC_MAX_PRIVATE_SIZE)
    {
        return STATUS_INVALID_PARAMETER;
    }

    if (AllocationPrivateDataSize != 0)
    {
        AllocationPrivateData = ExAllocatePoolWithTag(PagedPool,
                                                      AllocationPrivateDataSize,
                                                      TAG_DXGK_DISPLAY);
        if (AllocationPrivateData == NULL)
        {
            Status = STATUS_INSUFFICIENT_RESOURCES;
            goto Cleanup;
        }
    }

    if (ResourcePrivateDataSize != 0)
    {
        ResourcePrivateData = ExAllocatePoolWithTag(PagedPool,
                                                    ResourcePrivateDataSize,
                                                    TAG_DXGK_DISPLAY);
        if (ResourcePrivateData == NULL)
        {
            Status = STATUS_INSUFFICIENT_RESOURCES;
            goto Cleanup;
        }
    }

    QueryArgs.pAllocationPrivateDriverData = AllocationPrivateData;
    QueryArgs.AllocationPrivateDriverDataSize = AllocationPrivateDataSize;
    QueryArgs.pResourcePrivateDriverData = ResourcePrivateData;
    QueryArgs.ResourcePrivateDriverDataSize = ResourcePrivateDataSize;

    if (!DxgkAcquireKmdCall(Adapter))
    {
        Status = STATUS_DELETE_PENDING;
        goto Cleanup;
    }
    Status = DXGK_CB_FULL(Adapter, DxgkDdiGetStandardAllocationDriverData)(
        Adapter->MiniportDeviceContext,
        &QueryArgs);
    DxgkReleaseKmdCall(Adapter);
    if (!NT_SUCCESS(Status))
        goto Cleanup;
    if (QueryArgs.AllocationPrivateDriverDataSize != AllocationPrivateDataSize ||
        QueryArgs.ResourcePrivateDriverDataSize != ResourcePrivateDataSize)
    {
        Status = STATUS_INVALID_PARAMETER;
        goto Cleanup;
    }

    RtlZeroMemory(&AllocInfo, sizeof(AllocInfo));
    AllocInfo.pPrivateDriverData = AllocationPrivateData;
    AllocInfo.PrivateDriverDataSize = AllocationPrivateDataSize;
    AllocInfo.Size = max((SIZE_T)SurfaceData.Pitch * SurfaceData.Height,
                         (SIZE_T)SurfaceData.Width * SurfaceData.Height * 4);

    RtlZeroMemory(&CreateFlags, sizeof(CreateFlags));
    CreateFlags.Resource = 1;

    DXGKRNL_TRACE("DxgkpEnsureSharedShadowSurface: calling CreateAllocation "
                  "Pitch=%u PrivDataSize=%u ResPrivDataSize=%u\n",
                  SurfaceData.Pitch,
                  AllocationPrivateDataSize,
                  ResourcePrivateDataSize);

    Status = DxgkVidMmCreateAllocation(Adapter, NULL, &AllocInfo, ResourcePrivateData, ResourcePrivateDataSize, NULL, CreateFlags, D3DDDI_ID_UNINITIALIZED, &AllocationHandle, &MiniportResourceHandle);
    if (!NT_SUCCESS(Status))
        goto Cleanup;

    Status = DxgkVidMmReferenceAllocation(AllocationHandle, Adapter, NULL, &Allocation);
    if (!NT_SUCCESS(Status))
        goto Cleanup;

    /*
     * CDD writes the shadow through its CPU mapping, then uses it as the GPU
     * blit source. Keep its placement resident: an aperture mapping exposes
     * the same cached backing pages to both CPU and GPU.
     */
    if (!Allocation->Resident)
    {
        Status = DxgkVidMmMakeResident(Allocation, Adapter);
        if (!NT_SUCCESS(Status))
            goto Cleanup;
    }

    Resource = DxgkVidMmCreateResourceWrapper(Adapter, NULL, MiniportResourceHandle, 0, TRUE, NULL, 0, ResourcePrivateData, ResourcePrivateDataSize);
    if (Resource == NULL)
    {
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto Cleanup;
    }

    Status = DxgkVidMmAttachAllocationToResource(Resource, Allocation);
    if (!NT_SUCCESS(Status))
        goto Cleanup;

    if (!Adapter->MiniportContext->IsDisplayOnlyDriver)
    {
        Status = DxgkVidMmMapAllocationCpu(Allocation, &ShadowVa);
        if (!NT_SUCCESS(Status))
            goto Cleanup;
        if (ShadowVa == NULL)
        {
            Status = STATUS_UNSUCCESSFUL;
            goto Cleanup;
        }
    }

    DxgkpBeginSharedSurfaceMutationLocked(Adapter);
    Adapter->SharedShadowResourceHandle = Resource->Handle;
    Adapter->SharedShadowGlobalShareHandle = Resource->GlobalShareHandle;
    Adapter->SharedShadowAllocationHandle = AllocationHandle;
    Adapter->SharedShadowWidth = SurfaceData.Width;
    Adapter->SharedShadowHeight = SurfaceData.Height;
    Adapter->SharedShadowPitch = SurfaceData.Pitch;
    Adapter->SharedShadowFormat = SurfaceData.Format;

    /*
     * For full WDDM adapters using the DOD present path:
     * Set ShadowFb to the shadow allocation's CPU VA so that the
     * present timer can copy CDD's draws to the display via
     * DxgkDdiPresentDisplayOnly.
     */
    if (ShadowVa != NULL)
    {
        Adapter->ShadowFb = ShadowVa;
        Adapter->ShadowFbPitch = SurfaceData.Pitch;
        Adapter->ShadowFbSize = AllocInfo.Size;
        Adapter->ShadowFbPoolOwned = FALSE;
        StartTimer = Adapter->VidPnCommitted;
        DXGKRNL_TRACE("DxgkpEnsureSharedShadowSurface: ShadowFb=%p pitch=%u (%Iu bytes) for DOD present path\n", ShadowVa, SurfaceData.Pitch, AllocInfo.Size);
    }
    DxgkpEndSharedSurfaceMutationLocked(Adapter);
    if (StartTimer)
        DxgkpStartPresentTimer(Adapter);

    DXGKRNL_TRACE("DxgkpEnsureSharedShadowSurface: created %ux%u pitch=%u "
                  "ResHandle=0x%X GlobalShare=0x%X AllocHandle=%p\n",
                  SurfaceData.Width,
                  SurfaceData.Height,
                  SurfaceData.Pitch,
                  Resource->Handle,
                  Resource->GlobalShareHandle,
                  AllocationHandle);

    Status = STATUS_SUCCESS;

Cleanup:
    if (Allocation != NULL)
    {
        DxgkVidMmDereferenceAllocation(Allocation);
        Allocation = NULL;
    }

    if (!NT_SUCCESS(Status))
    {
        DXGKRNL_WARN("DxgkpEnsureSharedShadowSurface: FAILED status=0x%08lX\n",
                     Status);
        if (AllocationHandle != NULL)
            DxgkVidMmDestroyAllocation(Adapter, AllocationHandle);
        if (Resource != NULL)
            DxgkpVidMmDestroyResourceWrapper(Adapter, Resource);
    }

    if (AllocationPrivateData != NULL)
        ExFreePoolWithTag(AllocationPrivateData, TAG_DXGK_DISPLAY);
    if (ResourcePrivateData != NULL)
        ExFreePoolWithTag(ResourcePrivateData, TAG_DXGK_DISPLAY);

    UNREFERENCED_PARAMETER(VidPnSourceId);
    return Status;
}

static NTSTATUS
DxgkpEnsureSharedPrimaryLocked(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _In_ D3DDDI_VIDEO_PRESENT_SOURCE_ID VidPnSourceId)
{
    DXGKARG_GETSTANDARDALLOCATIONDRIVERDATA QueryArgs;
    D3DKMDT_SHAREDPRIMARYSURFACEDATA SurfaceData;
    DXGK_ALLOCATIONINFO AllocInfo;
    DXGK_CREATEALLOCATIONFLAGS CreateFlags;
    HANDLE AllocationHandle = NULL;
    HANDLE MiniportResourceHandle = NULL;
    PDXGKVMM_ALLOCATION Allocation = NULL;
    PDXGKVMM_RESOURCE Resource = NULL;
    PVOID AllocationPrivateData = NULL;
    PVOID ResourcePrivateData = NULL;
    UINT AllocationPrivateDataSize = 0;
    UINT ResourcePrivateDataSize = 0;
    PVOID CpuVa = NULL;
    BOOLEAN StartTimer = FALSE;
    BOOLEAN PreserveShadow = FALSE;
    NTSTATUS Status;

    if (Adapter == NULL)
        return STATUS_INVALID_PARAMETER;

    /*
     * An invalid source is a pure query failure.  Reject it before touching
     * the adapter-owned primary/shadow pair: CDD may still have the current
     * source mapped as its GDI surface.
     */
    if (VidPnSourceId >= Adapter->NumberOfVideoPresentSources)
        return STATUS_INVALID_PARAMETER;

    DXGKRNL_TRACE("DxgkpEnsureSharedPrimary: Adapter=%p Committed=%ux%u "
                  "ExistingAlloc=%p VidPnSourceId=%u\n",
                  Adapter,
                  Adapter->CommittedWidth, Adapter->CommittedHeight,
                  Adapter->SharedPrimaryAllocationHandle,
                  VidPnSourceId);

    if (Adapter->CommittedWidth == 0 || Adapter->CommittedHeight == 0)
    {
        Status = DxgkpDisplayCommitVidPnWhileSharedPrimaryLocked(Adapter);
        if (!NT_SUCCESS(Status))
            return Status;
        /*
         * A commit over an empty topology succeeds without establishing a
         * mode, so success here does not mean there is a geometry to build a
         * primary from.  Asking the miniport for a 0x0 standard allocation
         * makes it answer with whatever it likes -- this Intel part returns
         * STATUS_UNSUCCESSFUL -- and that status is what win32k reports
         * instead of the reason there is no mode.  Say it plainly.
         */
        if (Adapter->CommittedWidth == 0 || Adapter->CommittedHeight == 0)
        {
            DXGKRNL_WARN("DxgkpEnsureSharedPrimary: no VidPN has been committed, "
                         "so the adapter has no mode to build a primary from\n");
            return STATUS_GRAPHICS_INVALID_VIDPN_TOPOLOGY;
        }
    }

    if (Adapter->SharedPrimaryAllocationHandle != NULL &&
        Adapter->SharedPrimaryWidth == Adapter->CommittedWidth &&
        Adapter->SharedPrimaryHeight == Adapter->CommittedHeight &&
        Adapter->SharedPrimaryVidPnSourceId == VidPnSourceId)
    {
        BOOLEAN ContentLost;

        Status = DxgkVidMmReferenceAllocation(
                     Adapter->SharedPrimaryAllocationHandle,
                     Adapter, NULL, &Allocation);
        if (!NT_SUCCESS(Status))
            return Status;
        (VOID)KeWaitForSingleObject(&Allocation->ResidencyLock,
                                    Executive, KernelMode, FALSE, NULL);
        ContentLost = Allocation->ContentLost;
        KeReleaseMutex(&Allocation->ResidencyLock, FALSE);
        DxgkVidMmDereferenceAllocation(Allocation);
        Allocation = NULL;
        if (!ContentLost)
            return STATUS_SUCCESS;

        /* A reset can invalidate a local-memory primary without changing
         * the mode. Do not publish that lost allocation to new callers.
         * CDD still maps the system-memory shadow: preserve its backing
         * while replacing only the primary and its sharing identity. */
        if (Adapter->MiniportContext->IsDisplayOnlyDriver)
            return STATUS_GRAPHICS_ALLOCATION_CONTENT_LOST;
        PreserveShadow = TRUE;
    }

    if (!PreserveShadow)
        DxgkpDestroySharedPrimaryLocked(Adapter);

    if (DXGK_CB_FULL(Adapter, DxgkDdiGetStandardAllocationDriverData) == NULL)
        return STATUS_NOT_SUPPORTED;

    RtlZeroMemory(&SurfaceData, sizeof(SurfaceData));
    SurfaceData.Width = Adapter->CommittedWidth;
    SurfaceData.Height = Adapter->CommittedHeight;
    SurfaceData.Format = D3DDDIFMT_X8R8G8B8;
    SurfaceData.RefreshRate.Numerator = 60;
    SurfaceData.RefreshRate.Denominator = 1;
    SurfaceData.VidPnSourceId = VidPnSourceId;

    RtlZeroMemory(&QueryArgs, sizeof(QueryArgs));
    QueryArgs.StandardAllocationType = DXGK_STDALLOCATION_SHAREDPRIMARYSURFACE;
    QueryArgs.pCreateSharedPrimarySurfaceData = &SurfaceData;

    if (!DxgkAcquireKmdCall(Adapter))
        return STATUS_DELETE_PENDING;
    Status = DXGK_CB_FULL(Adapter, DxgkDdiGetStandardAllocationDriverData)(
        Adapter->MiniportDeviceContext,
        &QueryArgs);
    DxgkReleaseKmdCall(Adapter);
    if (!NT_SUCCESS(Status))
        return Status;

    AllocationPrivateDataSize = QueryArgs.AllocationPrivateDriverDataSize;
    ResourcePrivateDataSize = QueryArgs.ResourcePrivateDriverDataSize;
    if (AllocationPrivateDataSize > DXGKP_STDALLOC_MAX_PRIVATE_SIZE ||
        ResourcePrivateDataSize > DXGKP_STDALLOC_MAX_PRIVATE_SIZE)
    {
        return STATUS_INVALID_PARAMETER;
    }

    if (AllocationPrivateDataSize != 0)
    {
        AllocationPrivateData = ExAllocatePoolWithTag(PagedPool,
                                                      AllocationPrivateDataSize,
                                                      TAG_DXGK_DISPLAY);
        if (AllocationPrivateData == NULL)
        {
            Status = STATUS_INSUFFICIENT_RESOURCES;
            goto Cleanup;
        }
    }

    if (ResourcePrivateDataSize != 0)
    {
        ResourcePrivateData = ExAllocatePoolWithTag(PagedPool,
                                                    ResourcePrivateDataSize,
                                                    TAG_DXGK_DISPLAY);
        if (ResourcePrivateData == NULL)
        {
            Status = STATUS_INSUFFICIENT_RESOURCES;
            goto Cleanup;
        }
    }

    QueryArgs.pAllocationPrivateDriverData = AllocationPrivateData;
    QueryArgs.AllocationPrivateDriverDataSize = AllocationPrivateDataSize;
    QueryArgs.pResourcePrivateDriverData = ResourcePrivateData;
    QueryArgs.ResourcePrivateDriverDataSize = ResourcePrivateDataSize;

    if (!DxgkAcquireKmdCall(Adapter))
    {
        Status = STATUS_DELETE_PENDING;
        goto Cleanup;
    }
    Status = DXGK_CB_FULL(Adapter, DxgkDdiGetStandardAllocationDriverData)(
        Adapter->MiniportDeviceContext,
        &QueryArgs);
    DxgkReleaseKmdCall(Adapter);
    if (!NT_SUCCESS(Status))
        goto Cleanup;
    if (QueryArgs.AllocationPrivateDriverDataSize != AllocationPrivateDataSize ||
        QueryArgs.ResourcePrivateDriverDataSize != ResourcePrivateDataSize)
    {
        Status = STATUS_INVALID_PARAMETER;
        goto Cleanup;
    }

    RtlZeroMemory(&AllocInfo, sizeof(AllocInfo));
    AllocInfo.pPrivateDriverData = AllocationPrivateData;
    AllocInfo.PrivateDriverDataSize = AllocationPrivateDataSize;
    /* Set the allocation size from the surface dimensions.
     * The shared primary is always 32bpp XRGB. */
    AllocInfo.Size = (SIZE_T)SurfaceData.Width * SurfaceData.Height * 4;

    RtlZeroMemory(&CreateFlags, sizeof(CreateFlags));
    CreateFlags.Resource = 1;

    DXGKRNL_TRACE("DxgkpEnsureSharedPrimary: calling CreateAllocation "
                  "PrivDataSize=%u ResPrivDataSize=%u\n",
                  AllocationPrivateDataSize, ResourcePrivateDataSize);

    Status = DxgkVidMmCreateAllocation(Adapter, NULL, &AllocInfo, ResourcePrivateData, ResourcePrivateDataSize, NULL, CreateFlags, VidPnSourceId, &AllocationHandle, &MiniportResourceHandle);
    if (!NT_SUCCESS(Status))
    {
        DXGKRNL_WARN("DxgkpEnsureSharedPrimary: CreateAllocation failed 0x%08lX\n",
                     Status);
        goto Cleanup;
    }

    Status = DxgkVidMmReferenceAllocation(AllocationHandle, Adapter, NULL, &Allocation);
    if (!NT_SUCCESS(Status))
        goto Cleanup;

    /*
     * The shared primary is the retirement destination and may also be
     * CPU-mapped by the display path. Pin it before any mapping is published;
     * allocation destruction drains this lifetime reference.
     */
    if (!Allocation->Resident)
    {
        Status = DxgkVidMmMakeResident(Allocation, Adapter);
        if (!NT_SUCCESS(Status))
            goto Cleanup;
    }
    Status = DxgkVidMmAcquireDeviceResidencyReference(Allocation, NULL);
    if (!NT_SUCCESS(Status))
        goto Cleanup;

    Resource = DxgkVidMmCreateResourceWrapper(Adapter, NULL, MiniportResourceHandle, 0, TRUE, NULL, 0, ResourcePrivateData, ResourcePrivateDataSize);
    if (Resource == NULL)
    {
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto Cleanup;
    }

    Status = DxgkVidMmAttachAllocationToResource(Resource, Allocation);
    if (!NT_SUCCESS(Status))
        goto Cleanup;

    if (Adapter->MiniportContext->IsDisplayOnlyDriver)
    {
        Status = DxgkVidMmMapAllocationCpu(Allocation, &CpuVa);
        if (!NT_SUCCESS(Status))
            goto Cleanup;
        if (CpuVa == NULL)
        {
            Status = STATUS_UNSUCCESSFUL;
            goto Cleanup;
        }
    }

    /*
     * Attach system memory backing to the GPU resource via aperture mapping.
     * This triggers BuildPagingBuffer(MAP_APERTURE_SEGMENT) → miniport's
     * MapApertureSegment → AttachBacking → GPU can read the pages.
     */
    Status = DxgkVidMmEnsureAllocationApertureMapped(Allocation);
    if (!NT_SUCCESS(Status))
    {
        DXGKRNL_WARN("DxgkpEnsureSharedPrimary: aperture map failed 0x%08lX\n",
                     Status);
        goto Cleanup;
    }

    DxgkpBeginSharedSurfaceMutationLocked(Adapter);
    if (PreserveShadow)
        DxgkpDestroySharedPrimaryAllocationLocked(Adapter);
    Adapter->SharedPrimaryResourceHandle = Resource->Handle;
    Adapter->SharedPrimaryGlobalShareHandle = Resource->GlobalShareHandle;
    Adapter->SharedPrimaryAllocationHandle = AllocationHandle;
    Adapter->SharedPrimaryVidPnSourceId = VidPnSourceId;
    Adapter->SharedPrimaryWidth = SurfaceData.Width;
    Adapter->SharedPrimaryHeight = SurfaceData.Height;
    Adapter->SharedPrimaryFormat = SurfaceData.Format;
    Adapter->SharedPrimaryIsGopBacked = FALSE;
    if (CpuVa != NULL)
    {
        Adapter->ShadowFb = CpuVa;
        Adapter->ShadowFbPitch = SurfaceData.Width * 4;
        Adapter->ShadowFbSize = AllocInfo.Size;
        Adapter->ShadowFbPoolOwned = FALSE;
        StartTimer = Adapter->VidPnCommitted;
        DXGKRNL_TRACE("DxgkpEnsureSharedPrimary: ShadowFb set to %p pitch=%u (%Iu bytes)\n", CpuVa, SurfaceData.Width * 4, AllocInfo.Size);
    }
    DxgkpEndSharedSurfaceMutationLocked(Adapter);
    if (StartTimer)
        DxgkpStartPresentTimer(Adapter);

    /*
     * Shared-primary creation only prepares the backing allocation.
     * Full-WDDM scanout programming still happens in DxgkSetDisplayMode
     * and via the present/retire refresh path.
     */

    DXGKRNL_TRACE("DxgkpEnsureSharedPrimary: created %ux%u "
                  "ResHandle=0x%X GlobalShare=0x%X AllocHandle=%p\n",
                  SurfaceData.Width, SurfaceData.Height,
                  Resource->Handle, Resource->GlobalShareHandle,
                  AllocationHandle);

    Status = STATUS_SUCCESS;

Cleanup:
    if (Allocation != NULL)
    {
        DxgkVidMmDereferenceAllocation(Allocation);
        Allocation = NULL;
    }

    if (!NT_SUCCESS(Status))
    {
        DXGKRNL_WARN("DxgkpEnsureSharedPrimary: FAILED status=0x%08lX\n", Status);
        if (!PreserveShadow &&
            (Adapter->SharedPrimaryAllocationHandle != NULL ||
             Adapter->SharedPrimaryResourceHandle != 0))
        {
            DxgkpDestroySharedPrimaryLocked(Adapter);
        }
        else
        {
            if (AllocationHandle != NULL)
                DxgkVidMmDestroyAllocation(Adapter, AllocationHandle);
            if (Resource != NULL)
                DxgkpVidMmDestroyResourceWrapper(Adapter, Resource);
        }
    }

    if (AllocationPrivateData != NULL)
        ExFreePoolWithTag(AllocationPrivateData, TAG_DXGK_DISPLAY);
    if (ResourcePrivateData != NULL)
        ExFreePoolWithTag(ResourcePrivateData, TAG_DXGK_DISPLAY);

    return Status;
}

NTSTATUS
DxgkpEnsureSharedDisplaySurfaces(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _In_ D3DDDI_VIDEO_PRESENT_SOURCE_ID VidPnSourceId)
{
    NTSTATUS Status;

    PAGED_CODE();

    if (Adapter == NULL || Adapter->MiniportContext == NULL ||
        Adapter->MiniportContext->IsDisplayOnlyDriver)
    {
        return STATUS_NOT_SUPPORTED;
    }

    (VOID)KeWaitForSingleObject(&Adapter->SharedPrimaryMutex,
                                Executive,
                                KernelMode,
                                FALSE,
                                NULL);
    Status = DxgkpEnsureSharedPrimaryLocked(Adapter, VidPnSourceId);
    if (NT_SUCCESS(Status))
    {
        Status = DxgkpEnsureSharedShadowSurfaceLocked(Adapter,
                                                      VidPnSourceId);
    }
    KeReleaseMutex(&Adapter->SharedPrimaryMutex, FALSE);
    return Status;
}

VOID
DxgkDestroySharedPrimary(
    _In_ PDXGKRNL_ADAPTER Adapter)
{
    if (Adapter == NULL)
        return;
    (VOID)KeWaitForSingleObject(&Adapter->SharedPrimaryMutex, Executive, KernelMode, FALSE, NULL);
    DxgkpDestroySharedPrimaryLocked(Adapter);
    KeReleaseMutex(&Adapter->SharedPrimaryMutex, FALSE);
}

NTSTATUS
NTAPI
DxgkSetDisplayMode(
    _In_ D3DKMT_SETDISPLAYMODE *pSetDisplayMode)
{
    PDXGKRNL_ADAPTER Adapter = NULL;
    PDXGKRNL_DEVICE Device = NULL;
    PDXGKP_VIDPN VidPn;
    PDXGKVMM_ALLOCATION Allocation = NULL;
    PDXGKARG_SETVIDPNSOURCEADDRESS SetSourceAddress = NULL;
    LARGE_INTEGER PrimaryAddress;
    D3DDDI_VIDEO_PRESENT_SOURCE_ID VidPnSourceId;
    NTSTATUS Status = STATUS_SUCCESS;

    PAGED_CODE();

    if (pSetDisplayMode == NULL)
        return STATUS_INVALID_PARAMETER;

    /* Look up the device to get the adapter. */
    Device = DxgkLookupDeviceByHandle(pSetDisplayMode->hDevice, &Adapter);
    if (Device == NULL || Adapter == NULL)
    {
        DXGKRNL_WARN("DxgkSetDisplayMode: invalid device handle 0x%X\n",
                     pSetDisplayMode->hDevice);
        return STATUS_INVALID_PARAMETER;
    }

    Status = DxgkVidMmReferenceAllocation((HANDLE)(ULONG_PTR)pSetDisplayMode->hPrimaryAllocation, Adapter, Device, &Allocation);
    if (!NT_SUCCESS(Status))
    {
        Status = STATUS_INVALID_HANDLE;
        goto Cleanup;
    }
    VidPnSourceId = Allocation->PrimaryVidPnSourceId;
    if (VidPnSourceId >= Adapter->NumberOfVideoPresentSources)
    {
        Status = STATUS_INVALID_PARAMETER;
        goto Cleanup;
    }

    (VOID)KeWaitForSingleObject(&Adapter->VidPnMutex, Executive, KernelMode, FALSE, NULL);
    VidPn = (PDXGKP_VIDPN)Adapter->VidPn;
    if (VidPn == NULL || VidPn->Signature != DXGKP_VIDPN_SIGNATURE)
    {
        KeReleaseMutex(&Adapter->VidPnMutex, FALSE);
        DXGKRNL_WARN("DxgkSetDisplayMode: no VidPN on adapter\n");
        Status = STATUS_UNSUCCESSFUL;
        goto Cleanup;
    }
    KeReleaseMutex(&Adapter->VidPnMutex, FALSE);

    (VOID)KeWaitForSingleObject(&Adapter->SharedPrimaryMutex, Executive, KernelMode, FALSE, NULL);
    Status = DxgkpEnsureSharedPrimaryLocked(Adapter, VidPnSourceId);
    KeReleaseMutex(&Adapter->SharedPrimaryMutex, FALSE);
    if (!NT_SUCCESS(Status))
        goto Cleanup;

    Status = DxgkVidMmEnsureAllocationApertureMapped(Allocation);
    if (!NT_SUCCESS(Status))
        goto Cleanup;

    if (DXGK_CB_FULL(Adapter, DxgkDdiSetVidPnSourceAddress) == NULL)
    {
        Status = STATUS_NOT_SUPPORTED;
        goto Cleanup;
    }

    PrimaryAddress = DxgkVidMmGetAllocationPrimaryAddress(Allocation);

    SetSourceAddress = ExAllocatePoolWithTag(NonPagedPool, sizeof(*SetSourceAddress), TAG_DXGK_VIDPN);
    if (SetSourceAddress == NULL)
    {
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto Cleanup;
    }

    RtlZeroMemory(SetSourceAddress, sizeof(*SetSourceAddress));
    SetSourceAddress->VidPnSourceId = VidPnSourceId;
    SetSourceAddress->hAllocation = Allocation->MiniportHandle;
    SetSourceAddress->PrimaryAddress = PrimaryAddress;
    SetSourceAddress->PrimarySegment = Allocation->SegmentId;
    SetSourceAddress->Flags.ModeChange = 1;

    DXGKRNL_TRACE("DxgkSetDisplayMode: device=0x%X alloc=0x%X seg=%u addr=0x%I64x\n",
                  pSetDisplayMode->hDevice,
                  pSetDisplayMode->hPrimaryAllocation,
                  Allocation->SegmentId,
                  PrimaryAddress.QuadPart);

    if (!DxgkAcquireKmdCall(Adapter))
    {
        Status = STATUS_DELETE_PENDING;
        goto Cleanup;
    }
    Status = DXGK_CB_FULL(Adapter, DxgkDdiSetVidPnSourceAddress)(Adapter->MiniportDeviceContext, SetSourceAddress);
    DxgkReleaseKmdCall(Adapter);
    ExFreePoolWithTag(SetSourceAddress, TAG_DXGK_VIDPN);
    SetSourceAddress = NULL;
    if (!NT_SUCCESS(Status))
        goto Cleanup;

    if (DXGK_CB(Adapter, DxgkDdiSetVidPnSourceVisibility) != NULL && DxgkAcquireKmdCall(Adapter))
    {
        DXGKARG_SETVIDPNSOURCEVISIBILITY Visibility;
        Visibility.VidPnSourceId = VidPnSourceId;
        Visibility.Visible = TRUE;
        DXGK_CB(Adapter, DxgkDdiSetVidPnSourceVisibility)(Adapter->MiniportDeviceContext, &Visibility);
        DxgkReleaseKmdCall(Adapter);
    }

Cleanup:
    if (SetSourceAddress != NULL)
        ExFreePoolWithTag(SetSourceAddress, TAG_DXGK_VIDPN);
    if (Allocation != NULL)
        DxgkVidMmDereferenceAllocation(Allocation);
    if (Device != NULL)
        DxgkDereferenceDevice(Device);
    return Status;
}

NTSTATUS
NTAPI
DxgkGetSharedPrimaryHandle(
    _Inout_ D3DKMT_GETSHAREDPRIMARYHANDLE *pGetSharedPrimaryHandle)
{
    PDXGKRNL_ADAPTER Adapter = NULL;
    NTSTATUS Status;

    PAGED_CODE();

    if (pGetSharedPrimaryHandle == NULL)
        return STATUS_INVALID_PARAMETER;

    DXGKRNL_TRACE("DxgkGetSharedPrimaryHandle: hAdapter=0x%X VidPnSourceId=%u\n",
                  pGetSharedPrimaryHandle->hAdapter,
                  pGetSharedPrimaryHandle->VidPnSourceId);

    Adapter = DxgkLookupAdapterByHandle(pGetSharedPrimaryHandle->hAdapter);
    if (Adapter == NULL)
    {
        DXGKRNL_WARN("DxgkGetSharedPrimaryHandle: invalid adapter handle 0x%X\n",
                     pGetSharedPrimaryHandle->hAdapter);
        return STATUS_INVALID_PARAMETER;
    }

    (VOID)KeWaitForSingleObject(&Adapter->SharedPrimaryMutex, Executive, KernelMode, FALSE, NULL);
    Status = DxgkpEnsureSharedPrimaryLocked(Adapter, pGetSharedPrimaryHandle->VidPnSourceId);
    if (!NT_SUCCESS(Status))
    {
        KeReleaseMutex(&Adapter->SharedPrimaryMutex, FALSE);
        goto Cleanup;
    }

    pGetSharedPrimaryHandle->hSharedPrimary = Adapter->SharedPrimaryGlobalShareHandle;

    DXGKRNL_TRACE("DxgkGetSharedPrimaryHandle: returning h=0x%X "
                  "(AllocHandle=%p ResHandle=0x%X Width=%u Height=%u)\n",
                  Adapter->SharedPrimaryGlobalShareHandle,
                  Adapter->SharedPrimaryAllocationHandle,
                  Adapter->SharedPrimaryResourceHandle,
                  Adapter->SharedPrimaryWidth,
                  Adapter->SharedPrimaryHeight);
    KeReleaseMutex(&Adapter->SharedPrimaryMutex, FALSE);

Cleanup:
    DxgkDereferenceAdapter(Adapter);
    return Status;
}

NTSTATUS
NTAPI
DxgkGetShadowSurface(
    _Inout_ DXGKMT_GETSHADOWSURFACE *pGetShadowSurface)
{
    PDXGKRNL_ADAPTER Adapter = NULL;
    NTSTATUS Status;

    PAGED_CODE();

    if (pGetShadowSurface == NULL)
        return STATUS_INVALID_PARAMETER;

    Adapter = DxgkLookupAdapterByHandle(pGetShadowSurface->hAdapter);
    if (Adapter == NULL)
        return STATUS_INVALID_PARAMETER;

    (VOID)KeWaitForSingleObject(&Adapter->SharedPrimaryMutex, Executive, KernelMode, FALSE, NULL);
    Status = DxgkpEnsureSharedPrimaryLocked(Adapter, pGetShadowSurface->VidPnSourceId);
    if (!NT_SUCCESS(Status))
    {
        KeReleaseMutex(&Adapter->SharedPrimaryMutex, FALSE);
        goto Cleanup;
    }

    Status = DxgkpEnsureSharedShadowSurfaceLocked(Adapter, pGetShadowSurface->VidPnSourceId);
    if (!NT_SUCCESS(Status))
    {
        KeReleaseMutex(&Adapter->SharedPrimaryMutex, FALSE);
        goto Cleanup;
    }

    pGetShadowSurface->hShadowSurface = Adapter->SharedShadowGlobalShareHandle;
    pGetShadowSurface->Width = Adapter->SharedShadowWidth;
    pGetShadowSurface->Height = Adapter->SharedShadowHeight;
    pGetShadowSurface->Pitch = Adapter->SharedShadowPitch;
    pGetShadowSurface->Format = Adapter->SharedShadowFormat;

    DXGKRNL_TRACE("DxgkGetShadowSurface: returning h=0x%X "
                  "(AllocHandle=%p ResHandle=0x%X Width=%u Height=%u Pitch=%u)\n",
                  Adapter->SharedShadowGlobalShareHandle,
                  Adapter->SharedShadowAllocationHandle,
                  Adapter->SharedShadowResourceHandle,
                  Adapter->SharedShadowWidth,
                  Adapter->SharedShadowHeight,
                  Adapter->SharedShadowPitch);
    KeReleaseMutex(&Adapter->SharedPrimaryMutex, FALSE);

Cleanup:
    DxgkDereferenceAdapter(Adapter);
    return Status;
}

NTSTATUS
NTAPI
DxgkQueryResourceInfo(
    _Inout_ D3DKMT_QUERYRESOURCEINFO *pQueryResourceInfo)
{
    PDXGKRNL_ADAPTER Adapter = NULL;
    PDXGKRNL_DEVICE Device = NULL;
    PDXGKVMM_RESOURCE Resource = NULL;
    PDXGKVMM_ALLOCATION *Allocations = NULL;
    UINT AllocationCount = 0;
    UINT TotalPrivateDriverDataSize = 0;
    UINT RuntimeCapacity;
    NTSTATUS Status = STATUS_SUCCESS;

    PAGED_CODE();

    if (pQueryResourceInfo == NULL)
        return STATUS_INVALID_PARAMETER;
    RuntimeCapacity = pQueryResourceInfo->PrivateRuntimeDataSize;

    Device = DxgkLookupDeviceByHandle(pQueryResourceInfo->hDevice, &Adapter);
    if (Device == NULL || Adapter == NULL)
        return STATUS_INVALID_PARAMETER;

    Status = DxgkVidMmReferenceResource(pQueryResourceInfo->hGlobalShare, TRUE, NULL, &Resource);
    if (!NT_SUCCESS(Status) || Resource->Adapter != Adapter)
    {
        Status = STATUS_INVALID_HANDLE;
        goto Cleanup;
    }

    Status = DxgkVidMmSnapshotResourceAllocations(Resource, Adapter, &Allocations, &AllocationCount, &TotalPrivateDriverDataSize);
    if (!NT_SUCCESS(Status))
        goto Cleanup;
    pQueryResourceInfo->PrivateRuntimeDataSize = Resource->PrivateRuntimeDataSize;
    pQueryResourceInfo->TotalPrivateDriverDataSize = TotalPrivateDriverDataSize;
    pQueryResourceInfo->ResourcePrivateDriverDataSize = Resource->ResourcePrivateDriverDataSize;
    pQueryResourceInfo->NumAllocations = AllocationCount;
    if (Resource->PrivateRuntimeDataSize != 0 && pQueryResourceInfo->pPrivateRuntimeData != NULL)
    {
        if (RuntimeCapacity < Resource->PrivateRuntimeDataSize)
            Status = STATUS_BUFFER_TOO_SMALL;
        else
        {
            _SEH2_TRY
            {
                RtlCopyMemory(pQueryResourceInfo->pPrivateRuntimeData, Resource->PrivateRuntimeData, Resource->PrivateRuntimeDataSize);
            }
            _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
            {
                Status = _SEH2_GetExceptionCode();
            }
            _SEH2_END;
        }
    }

Cleanup:
    if (Allocations != NULL)
        DxgkVidMmReleaseAllocationSnapshot(Allocations, AllocationCount);
    if (Resource != NULL)
        DxgkVidMmDereferenceResource(Resource);
    DxgkDereferenceDevice(Device);
    return Status;
}

NTSTATUS
NTAPI
DxgkOpenResource(
    _Inout_ D3DKMT_OPENRESOURCE *pOpenResource)
{
    PDXGKRNL_ADAPTER Adapter = NULL;
    PDXGKRNL_DEVICE Device = NULL;
    PDXGKVMM_RESOURCE Resource = NULL;
    PDXGKVMM_ALLOCATION *Allocations = NULL;
    PDXGKVMM_RESOURCE OpenedResource = NULL;
    PHANDLE OpenedAllocationHandles = NULL;
    PVOID RuntimeCopy = NULL;
    PVOID ResourcePrivateCopy = NULL;
    PVOID TotalPrivateCopy = NULL;
    UINT AllocationCount = 0;
    UINT TotalPrivateSize = 0;
    UINT AllocationCapacity;
    UINT RuntimeCapacity;
    UINT ResourcePrivateCapacity;
    UINT TotalPrivateCapacity;
    UINT Offset;
    UINT Index;
    BOOLEAN BufferTooSmall = FALSE;
    NTSTATUS Status = STATUS_SUCCESS;

    PAGED_CODE();

    if (pOpenResource == NULL)
        return STATUS_INVALID_PARAMETER;

    AllocationCapacity = pOpenResource->NumAllocations;
    RuntimeCapacity = pOpenResource->PrivateRuntimeDataSize;
    ResourcePrivateCapacity = pOpenResource->ResourcePrivateDriverDataSize;
    TotalPrivateCapacity = pOpenResource->TotalPrivateDriverDataBufferSize;

    Device = DxgkLookupDeviceByHandle(pOpenResource->hDevice, &Adapter);
    if (Device == NULL || Adapter == NULL)
        return STATUS_INVALID_PARAMETER;

    Status = DxgkVidMmReferenceResource(pOpenResource->hGlobalShare, TRUE, NULL, &Resource);
    if (!NT_SUCCESS(Status) || Resource->Adapter != Adapter)
    {
        Status = STATUS_INVALID_HANDLE;
        goto Cleanup;
    }

    Status = DxgkVidMmSnapshotResourceAllocations(Resource, Adapter, &Allocations, &AllocationCount, &TotalPrivateSize);
    if (!NT_SUCCESS(Status))
        goto Cleanup;

    pOpenResource->NumAllocations = AllocationCount;
    pOpenResource->PrivateRuntimeDataSize = Resource->PrivateRuntimeDataSize;
    pOpenResource->ResourcePrivateDriverDataSize = Resource->ResourcePrivateDriverDataSize;
    pOpenResource->TotalPrivateDriverDataBufferSize = TotalPrivateSize;
    if (AllocationCapacity < AllocationCount || pOpenResource->pOpenAllocationInfo == NULL)
        BufferTooSmall = TRUE;
    if (Resource->PrivateRuntimeDataSize != 0 && (pOpenResource->pPrivateRuntimeData == NULL || RuntimeCapacity < Resource->PrivateRuntimeDataSize))
        BufferTooSmall = TRUE;
    if (Resource->ResourcePrivateDriverDataSize != 0 && (pOpenResource->pResourcePrivateDriverData == NULL || ResourcePrivateCapacity < Resource->ResourcePrivateDriverDataSize))
        BufferTooSmall = TRUE;
    if (TotalPrivateSize != 0 && (pOpenResource->pTotalPrivateDriverDataBuffer == NULL || TotalPrivateCapacity < TotalPrivateSize))
        BufferTooSmall = TRUE;
    if (BufferTooSmall)
    {
        Status = STATUS_BUFFER_TOO_SMALL;
        goto Cleanup;
    }

    if ((SIZE_T)AllocationCount > MAXULONG_PTR / sizeof(*OpenedAllocationHandles))
    {
        Status = STATUS_INTEGER_OVERFLOW;
        goto Cleanup;
    }
    OpenedAllocationHandles = ExAllocatePoolWithTag(NonPagedPool, (SIZE_T)AllocationCount * sizeof(*OpenedAllocationHandles), TAG_VIDMM_RESOURCE);
    if (OpenedAllocationHandles == NULL)
    {
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto Cleanup;
    }
    RtlZeroMemory(OpenedAllocationHandles, (SIZE_T)AllocationCount * sizeof(*OpenedAllocationHandles));
    if (Resource->PrivateRuntimeDataSize != 0)
    {
        RuntimeCopy = ExAllocatePoolWithTag(NonPagedPool, Resource->PrivateRuntimeDataSize, TAG_VIDMM_RESOURCE);
        if (RuntimeCopy == NULL)
        {
            Status = STATUS_INSUFFICIENT_RESOURCES;
            goto Cleanup;
        }
        RtlCopyMemory(RuntimeCopy, Resource->PrivateRuntimeData, Resource->PrivateRuntimeDataSize);
    }
    if (Resource->ResourcePrivateDriverDataSize != 0)
    {
        ResourcePrivateCopy = ExAllocatePoolWithTag(NonPagedPool, Resource->ResourcePrivateDriverDataSize, TAG_VIDMM_RESOURCE);
        if (ResourcePrivateCopy == NULL)
        {
            Status = STATUS_INSUFFICIENT_RESOURCES;
            goto Cleanup;
        }
        RtlCopyMemory(ResourcePrivateCopy, Resource->ResourcePrivateDriverData, Resource->ResourcePrivateDriverDataSize);
    }
    if (TotalPrivateSize != 0)
    {
        TotalPrivateCopy = ExAllocatePoolWithTag(NonPagedPool, TotalPrivateSize, TAG_VIDMM_RESOURCE);
        if (TotalPrivateCopy == NULL)
        {
            Status = STATUS_INSUFFICIENT_RESOURCES;
            goto Cleanup;
        }
        Offset = 0;
        for (Index = 0; Index < AllocationCount; ++Index)
        {
            if (Allocations[Index]->PrivateDriverDataSize != 0)
                RtlCopyMemory((PUCHAR)TotalPrivateCopy + Offset, Allocations[Index]->PrivateDriverData, Allocations[Index]->PrivateDriverDataSize);
            Offset += Allocations[Index]->PrivateDriverDataSize;
        }
        ASSERT(Offset == TotalPrivateSize);
    }

    Status = DxgkVidMmCreateOpenResource(Device, Resource, Allocations, AllocationCount, ResourcePrivateCopy, Resource->ResourcePrivateDriverDataSize, TotalPrivateCopy, TotalPrivateSize, &OpenedResource, OpenedAllocationHandles);
    if (!NT_SUCCESS(Status))
        goto Cleanup;

    _SEH2_TRY
    {
        if (Resource->PrivateRuntimeDataSize != 0)
            RtlCopyMemory(pOpenResource->pPrivateRuntimeData, RuntimeCopy, Resource->PrivateRuntimeDataSize);
        if (Resource->ResourcePrivateDriverDataSize != 0)
            RtlCopyMemory(pOpenResource->pResourcePrivateDriverData, ResourcePrivateCopy, Resource->ResourcePrivateDriverDataSize);
        if (TotalPrivateSize != 0)
            RtlCopyMemory(pOpenResource->pTotalPrivateDriverDataBuffer, TotalPrivateCopy, TotalPrivateSize);
        Offset = 0;
        for (Index = 0; Index < AllocationCount; ++Index)
        {
            pOpenResource->pOpenAllocationInfo[Index].hAllocation = (D3DKMT_HANDLE)(ULONG_PTR)OpenedAllocationHandles[Index];
            pOpenResource->pOpenAllocationInfo[Index].pPrivateDriverData = Allocations[Index]->PrivateDriverDataSize != 0 ? (PVOID)((PUCHAR)pOpenResource->pTotalPrivateDriverDataBuffer + Offset) : NULL;
            pOpenResource->pOpenAllocationInfo[Index].PrivateDriverDataSize = Allocations[Index]->PrivateDriverDataSize;
            Offset += Allocations[Index]->PrivateDriverDataSize;
        }
        pOpenResource->hResource = OpenedResource->Handle;
        pOpenResource->NumAllocations = AllocationCount;
        pOpenResource->PrivateRuntimeDataSize = Resource->PrivateRuntimeDataSize;
        pOpenResource->ResourcePrivateDriverDataSize = Resource->ResourcePrivateDriverDataSize;
        pOpenResource->TotalPrivateDriverDataBufferSize = TotalPrivateSize;
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Status = _SEH2_GetExceptionCode();
    }
    _SEH2_END;

Cleanup:
    if (!NT_SUCCESS(Status) && OpenedResource != NULL)
        DxgkpVidMmDestroyResourceWrapper(Adapter, OpenedResource);
    if (TotalPrivateCopy != NULL)
        ExFreePoolWithTag(TotalPrivateCopy, TAG_VIDMM_RESOURCE);
    if (ResourcePrivateCopy != NULL)
        ExFreePoolWithTag(ResourcePrivateCopy, TAG_VIDMM_RESOURCE);
    if (RuntimeCopy != NULL)
        ExFreePoolWithTag(RuntimeCopy, TAG_VIDMM_RESOURCE);
    if (OpenedAllocationHandles != NULL)
        ExFreePoolWithTag(OpenedAllocationHandles, TAG_VIDMM_RESOURCE);
    if (Allocations != NULL)
        DxgkVidMmReleaseAllocationSnapshot(Allocations, AllocationCount);
    if (Resource != NULL)
        DxgkVidMmDereferenceResource(Resource);
    DxgkDereferenceDevice(Device);
    return Status;
}

static VOID
DxgkpInitializeDisplayMode(
    _Out_ D3DKMT_DISPLAYMODE *Mode,
    _In_ UINT Width,
    _In_ UINT Height)
{
    RtlZeroMemory(Mode, sizeof(*Mode));
    Mode->Width = Width;
    Mode->Height = Height;
    Mode->Format = D3DDDIFMT_X8R8G8B8;
    Mode->IntegerRefreshRate = 60;
    Mode->RefreshRate.Numerator = 60000;
    Mode->RefreshRate.Denominator = 1000;
    Mode->ScanLineOrdering = D3DDDI_VSSLO_PROGRESSIVE;
    Mode->DisplayOrientation = D3DDDI_ROTATION_IDENTITY;
    Mode->DisplayFixedOutput = 0;
}

NTSTATUS
DxgkVidPnQueryCurrentDisplayMode(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _Inout_ D3DKMT_CURRENTDISPLAYMODE *CurrentMode)
{
    D3DKMT_DISPLAYMODE Mode;
    D3DDDI_VIDEO_PRESENT_SOURCE_ID SourceId;
    PDXGKP_VIDPN VidPn;
    PDXGKP_VIDPN_SOURCE_MODESET SourceSet;
    PDXGKP_VIDPN_TARGET_MODESET TargetSet = NULL;
    CONST D3DKMDT_VIDPN_SOURCE_MODE *SourceMode = NULL;
    CONST D3DKMDT_VIDPN_TARGET_MODE *TargetMode = NULL;
    CONST D3DKMDT_VIDPN_PRESENT_PATH *Path = NULL;
    SIZE_T Index;
    NTSTATUS Status = STATUS_INVALID_PARAMETER;

    PAGED_CODE();

    if (Adapter == NULL || CurrentMode == NULL)
        return STATUS_INVALID_PARAMETER;

    SourceId = CurrentMode->VidPnSourceId;
    if (SourceId >= Adapter->NumberOfVideoPresentSources)
        return STATUS_INVALID_PARAMETER;

    (VOID)KeWaitForSingleObject(&Adapter->VidPnMutex,
                                Executive,
                                KernelMode,
                                FALSE,
                                NULL);
    VidPn = (PDXGKP_VIDPN)Adapter->VidPn;
    if ((!Adapter->VidPnCommitted && !Adapter->HeadlessDesktop) ||
        Adapter->CommittedWidth == 0 ||
        Adapter->CommittedHeight == 0 ||
        VidPn == NULL ||
        VidPn->Signature != DXGKP_VIDPN_SIGNATURE ||
        SourceId >= VidPn->NumSources)
    {
        goto Cleanup;
    }

    for (Index = 0; Index < VidPn->NumPaths; ++Index)
    {
        if (VidPn->Paths[Index].VidPnSourceId == SourceId)
        {
            Path = &VidPn->Paths[Index];
            break;
        }
    }
    if (Path == NULL && !(Adapter->HeadlessDesktop && VidPn->NumPaths == 0 && SourceId == 0))
        goto Cleanup;

    SourceSet = VidPn->SourceModeSets[SourceId];
    if (SourceSet == NULL || SourceSet->PinnedModeId == (UINT)-1)
        goto Cleanup;
    for (Index = 0; Index < SourceSet->NumModes; ++Index)
    {
        if (SourceSet->Modes[Index].Id == SourceSet->PinnedModeId)
        {
            SourceMode = &SourceSet->Modes[Index];
            break;
        }
    }
    if (SourceMode == NULL || SourceMode->Type != D3DKMDT_RMT_GRAPHICS)
        goto Cleanup;

    DxgkpInitializeDisplayMode(&Mode,
                               SourceMode->Format.Graphics.PrimSurfSize.cx,
                               SourceMode->Format.Graphics.PrimSurfSize.cy);
    if (Mode.Width == 0 || Mode.Height == 0)
    {
        Mode.Width = Adapter->CommittedWidth;
        Mode.Height = Adapter->CommittedHeight;
    }
    if (SourceMode->Format.Graphics.PixelFormat != D3DDDIFMT_UNKNOWN)
        Mode.Format = SourceMode->Format.Graphics.PixelFormat;

    if (Path != NULL && DxgkVidPnTargetIndexFromId(VidPn, Path->VidPnTargetId) != MAXULONG)
        TargetSet = (DxgkVidPnTargetIndexFromId(VidPn, Path->VidPnTargetId) != MAXULONG)
                        ? VidPn->TargetModeSets[DxgkVidPnTargetIndexFromId(VidPn, Path->VidPnTargetId)] : NULL;
    if (TargetSet != NULL && TargetSet->PinnedModeId != (UINT)-1)
    {
        for (Index = 0; Index < TargetSet->NumModes; ++Index)
        {
            if (TargetSet->Modes[Index].Id == TargetSet->PinnedModeId)
            {
                TargetMode = &TargetSet->Modes[Index];
                break;
            }
        }
    }
    if (TargetMode != NULL &&
        TargetMode->VideoSignalInfo.VSyncFreq.Numerator != 0 &&
        TargetMode->VideoSignalInfo.VSyncFreq.Denominator != 0)
    {
        Mode.RefreshRate = TargetMode->VideoSignalInfo.VSyncFreq;
        Mode.IntegerRefreshRate = (UINT)(((ULONGLONG)Mode.RefreshRate.Numerator +
                                          (Mode.RefreshRate.Denominator / 2)) /
                                         Mode.RefreshRate.Denominator);
        Mode.ScanLineOrdering = TargetMode->VideoSignalInfo.ScanLineOrdering;
    }

    switch (Path != NULL ? Path->ContentTransformation.Rotation : D3DKMDT_VPPR_IDENTITY)
    {
        case D3DKMDT_VPPR_ROTATE90:
            Mode.DisplayOrientation = D3DDDI_ROTATION_90;
            break;
        case D3DKMDT_VPPR_ROTATE180:
            Mode.DisplayOrientation = D3DDDI_ROTATION_180;
            break;
        case D3DKMDT_VPPR_ROTATE270:
            Mode.DisplayOrientation = D3DDDI_ROTATION_270;
            break;
        default:
            Mode.DisplayOrientation = D3DDDI_ROTATION_IDENTITY;
            break;
    }

    CurrentMode->DisplayMode = Mode;
    Status = STATUS_SUCCESS;

Cleanup:
    KeReleaseMutex(&Adapter->VidPnMutex, FALSE);
    return Status;
}

typedef struct _DXGKP_DISPLAY_MODE_ENTRY
{
    D3DKMDT_VIDPN_SOURCE_MODE Source;
    D3DKMDT_VIDEO_SIGNAL_INFO Target;
    D3DKMT_DISPLAYMODE Mode;
} DXGKP_DISPLAY_MODE_ENTRY;

/* The top two VIDEO_MODE bits belong to the caller, not the mode index. */
#define DXGKP_DISPLAY_MODE_INDEX_BITS 12
#define DXGKP_DISPLAY_MODE_INDEX_MASK ((1UL << DXGKP_DISPLAY_MODE_INDEX_BITS) - 1)
#define DXGKP_DISPLAY_MODE_GENERATION_MASK 0x3ffff

typedef struct _DXGKP_DISPLAY_MODE_CACHE
{
    DXGKP_DISPLAY_MODE_ENTRY *Entries;
    ULONG Count;
    ULONG Capacity;
    ULONG Generation;
    LONG64 HotPlugGeneration;
    D3DDDI_VIDEO_PRESENT_SOURCE_ID SourceId;
    D3DDDI_VIDEO_PRESENT_TARGET_ID TargetId;
    BOOLEAN CanSetMode;
} DXGKP_DISPLAY_MODE_CACHE;

static VOID
DxgkpFreeDisplayModeCache(
    _In_opt_ DXGKP_DISPLAY_MODE_CACHE *Cache)
{
    if (Cache != NULL)
    {
        if (Cache->Entries != NULL)
            ExFreePoolWithTag(Cache->Entries, TAG_DXGK_MODE);
        ExFreePoolWithTag(Cache, TAG_DXGK_MODESET);
    }
}

VOID
DxgkVidPnDestroyDisplayModeCache(
    _In_ PDXGKRNL_ADAPTER Adapter)
{
    (VOID)KeWaitForSingleObject(&Adapter->SharedPrimaryMutex, Executive, KernelMode, FALSE, NULL);
    DxgkpFreeDisplayModeCache(Adapter->DisplayModeCache);
    Adapter->DisplayModeCache = NULL;
    KeReleaseMutex(&Adapter->SharedPrimaryMutex, FALSE);
}

static VOID
DxgkpDisplayModeFromVidPnModes(
    _Out_ D3DKMT_DISPLAYMODE *Mode,
    _In_ CONST D3DKMDT_VIDPN_SOURCE_MODE *Source,
    _In_ CONST D3DKMDT_VIDEO_SIGNAL_INFO *Target)
{
    DxgkpInitializeDisplayMode(Mode, Source->Format.Graphics.PrimSurfSize.cx, Source->Format.Graphics.PrimSurfSize.cy);
    Mode->Format = Source->Format.Graphics.PixelFormat;
    if (Target->VSyncFreq.Numerator != 0 && Target->VSyncFreq.Denominator != 0)
    {
        Mode->RefreshRate = Target->VSyncFreq;
        Mode->IntegerRefreshRate = (UINT)(((ULONGLONG)Mode->RefreshRate.Numerator + Mode->RefreshRate.Denominator / 2) / Mode->RefreshRate.Denominator);
    }
    Mode->ScanLineOrdering = Target->ScanLineOrdering;
}

static BOOLEAN
DxgkpDisplayModesEqual(
    _In_ CONST D3DKMT_DISPLAYMODE *Left,
    _In_ CONST D3DKMT_DISPLAYMODE *Right)
{
    return Left->Width == Right->Width && Left->Height == Right->Height &&
           Left->Format == Right->Format && Left->ScanLineOrdering == Right->ScanLineOrdering &&
           (ULONGLONG)Left->RefreshRate.Numerator * Right->RefreshRate.Denominator ==
               (ULONGLONG)Right->RefreshRate.Numerator * Left->RefreshRate.Denominator;
}

static BOOLEAN
DxgkpDisplayModeSupportsGdi(
    _In_ CONST DXGKP_DISPLAY_MODE_ENTRY *Entry)
{
    return Entry->Mode.Format == D3DDDIFMT_X8R8G8B8 || Entry->Mode.Format == D3DDDIFMT_A8R8G8B8;
}

static NTSTATUS
DxgkpAppendDisplayMode(
    _Inout_ DXGKP_DISPLAY_MODE_CACHE *Cache,
    _In_ CONST D3DKMDT_VIDPN_SOURCE_MODE *Source,
    _In_ CONST D3DKMDT_VIDEO_SIGNAL_INFO *Target)
{
    D3DKMT_DISPLAYMODE Mode;
    DXGKP_DISPLAY_MODE_ENTRY *Entries;
    ULONG Index, Capacity;

    if (Source->Type != D3DKMDT_RMT_GRAPHICS ||
        Source->Format.Graphics.PrimSurfSize.cx == 0 ||
        Source->Format.Graphics.PrimSurfSize.cy == 0)
        return STATUS_SUCCESS;
    DxgkpDisplayModeFromVidPnModes(&Mode, Source, Target);
    for (Index = 0; Index < Cache->Count; ++Index)
    {
        if (DxgkpDisplayModesEqual(&Cache->Entries[Index].Mode, &Mode))
            return STATUS_SUCCESS;
    }
    if (Cache->Count == Cache->Capacity)
    {
        Capacity = Cache->Capacity == 0 ? 8 : Cache->Capacity * 2;
        if (Capacity > DXGKP_DISPLAY_MODE_INDEX_MASK + 1)
            return STATUS_INSUFFICIENT_RESOURCES;
        Entries = ExAllocatePoolWithTag(PagedPool, Capacity * sizeof(*Entries), TAG_DXGK_MODE);
        if (Entries == NULL)
            return STATUS_INSUFFICIENT_RESOURCES;
        if (Cache->Entries != NULL)
        {
            RtlCopyMemory(Entries, Cache->Entries, Cache->Count * sizeof(*Entries));
            ExFreePoolWithTag(Cache->Entries, TAG_DXGK_MODE);
        }
        Cache->Entries = Entries;
        Cache->Capacity = Capacity;
    }
    Cache->Entries[Cache->Count].Source = *Source;
    Cache->Entries[Cache->Count].Target = *Target;
    Cache->Entries[Cache->Count++].Mode = Mode;
    return STATUS_SUCCESS;
}

static NTSTATUS
DxgkpEnumerateDisplayModeCandidate(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _In_ PDXGKP_VIDPN Candidate,
    _In_ BOOLEAN SourcePivot)
{
    DXGKARG_ENUMVIDPNCOFUNCMODALITY Args;
    NTSTATUS Status = STATUS_UNSUCCESSFUL;

    RtlZeroMemory(&Args, sizeof(Args));
    Args.hConstrainingVidPn = (D3DKMDT_HVIDPN)Candidate;
    Args.EnumPivotType = SourcePivot ? D3DKMDT_EPT_VIDPNSOURCE : D3DKMDT_EPT_NOPIVOT;
    Args.EnumPivot.VidPnSourceId = Candidate->Paths[0].VidPnSourceId;
    Args.EnumPivot.VidPnTargetId = Candidate->Paths[0].VidPnTargetId;
    if (!DxgkAcquireKmdCall(Adapter))
        return STATUS_DELETE_PENDING;
    _SEH2_TRY
    {
        Status = DXGK_CB(Adapter, DxgkDdiEnumVidPnCofuncModality)(Adapter->MiniportDeviceContext, &Args);
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Status = _SEH2_GetExceptionCode();
    }
    _SEH2_END;
    DxgkReleaseKmdCall(Adapter);
    return Status;
}

/* SharedPrimaryMutex keeps the published mode indices stable across a normal
 * switch. A monitor change invalidates them rather than reusing an old index
 * for a different resolution. Enumeration never mutates the live VidPN. */
static NTSTATUS
DxgkpEnsureDisplayModeCacheLocked(
    _In_ PDXGKRNL_ADAPTER Adapter)
{
    DXGKP_DISPLAY_MODE_CACHE *Cache = NULL;
    PDXGKP_VIDPN Current = NULL, Candidate = NULL;
    PDXGKP_VIDPN_SOURCE_MODESET Sources;
    PDXGKP_VIDPN_TARGET_MODESET Targets;
    D3DKMDT_VIDPN_SOURCE_MODE *SourceModes = NULL;
    CONST D3DKMDT_VIDPN_SOURCE_MODE *PinnedSource = NULL;
    CONST D3DKMDT_VIDPN_TARGET_MODE *PinnedTarget = NULL;
    LONG64 Generation = InterlockedCompareExchange64(&Adapter->HotPlugGeneration, 0, 0);
    ULONG SourceId, TargetIndex;
    SIZE_T SourceCount, SourceIndex, TargetModeIndex;
    BOOLEAN Transaction = FALSE;
    NTSTATUS Status;

    if (Adapter->DisplayModeCache != NULL && Adapter->DisplayModeCache->HotPlugGeneration == Generation)
        return STATUS_SUCCESS;
    if (!DxgkBeginKmdTransaction(Adapter))
        return STATUS_DELETE_PENDING;
    Transaction = TRUE;
    (VOID)KeWaitForSingleObject(&Adapter->VidPnMutex, Executive, KernelMode, FALSE, NULL);
    Current = (PDXGKP_VIDPN)Adapter->VidPn;
    if (!DxgkVidPnReference((D3DKMDT_HVIDPN)Current))
        Current = NULL;
    KeReleaseMutex(&Adapter->VidPnMutex, FALSE);
    if (Current != NULL && Current->NumPaths == 0 && Adapter->HeadlessDesktop)
    {
        D3DKMDT_VIDEO_SIGNAL_INFO Timing;

        Sources = Current->SourceModeSets[0];
        if (Sources != NULL)
        {
            for (SourceIndex = 0; SourceIndex < Sources->NumModes; ++SourceIndex)
                if (Sources->Modes[SourceIndex].Id == Sources->PinnedModeId)
                    PinnedSource = &Sources->Modes[SourceIndex];
        }
        if (PinnedSource == NULL)
        {
            Status = STATUS_GRAPHICS_VIDPN_MODALITY_NOT_SUPPORTED;
            goto Cleanup;
        }
        Cache = ExAllocatePoolZero(PagedPool, sizeof(*Cache), TAG_DXGK_MODESET);
        if (Cache == NULL)
        {
            Status = STATUS_INSUFFICIENT_RESOURCES;
            goto Cleanup;
        }
        Cache->HotPlugGeneration = Generation;
        Cache->SourceId = 0;
        Cache->TargetId = D3DDDI_ID_UNINITIALIZED;
        RtlZeroMemory(&Timing, sizeof(Timing));
        Timing.ScanLineOrdering = D3DDDI_VSSLO_PROGRESSIVE;
        Status = DxgkpAppendDisplayMode(Cache, PinnedSource, &Timing);
        if (!NT_SUCCESS(Status))
            goto Cleanup;
        goto PublishCache;
    }
    if (Current == NULL || Current->NumPaths == 0)
    {
        Status = STATUS_GRAPHICS_INVALID_VIDPN_TOPOLOGY;
        goto Cleanup;
    }
    SourceId = Current->Paths[0].VidPnSourceId;
    TargetIndex = DxgkVidPnTargetIndexFromId(Current, Current->Paths[0].VidPnTargetId);
    if (SourceId >= DXGKP_MAX_SOURCES || TargetIndex == MAXULONG ||
        Current->SourceModeSets[SourceId] == NULL || Current->TargetModeSets[TargetIndex] == NULL)
    {
        Status = STATUS_GRAPHICS_INVALID_VIDPN_TOPOLOGY;
        goto Cleanup;
    }
    Cache = ExAllocatePoolZero(PagedPool, sizeof(*Cache), TAG_DXGK_MODESET);
    if (Cache == NULL)
    {
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto Cleanup;
    }
    Cache->HotPlugGeneration = Generation;
    Cache->SourceId = SourceId;
    Cache->TargetId = Current->Paths[0].VidPnTargetId;
    Sources = Current->SourceModeSets[SourceId];
    Targets = Current->TargetModeSets[TargetIndex];
    for (SourceIndex = 0; SourceIndex < Sources->NumModes; ++SourceIndex)
    {
        if (Sources->Modes[SourceIndex].Id == Sources->PinnedModeId)
            PinnedSource = &Sources->Modes[SourceIndex];
    }
    for (TargetModeIndex = 0; TargetModeIndex < Targets->NumModes; ++TargetModeIndex)
    {
        if (Targets->Modes[TargetModeIndex].Id == Targets->PinnedModeId)
            PinnedTarget = &Targets->Modes[TargetModeIndex];
    }
    /* Keep the committed mode first, including custom modes absent from a
     * later enumeration. It is also the only mode for fixed-mode adapters. */
    if (PinnedSource != NULL && PinnedTarget != NULL)
    {
        Status = DxgkpAppendDisplayMode(Cache, PinnedSource, &PinnedTarget->VideoSignalInfo);
        if (!NT_SUCCESS(Status))
            goto Cleanup;
    }
    Cache->CanSetMode = Current->NumPaths == 1 && Adapter->MiniportContext != NULL &&
                        !Adapter->MiniportContext->IsDisplayOnlyDriver &&
                        !Adapter->MiniportContext->IsBasicDisplayFallback &&
                        DXGK_CB(Adapter, DxgkDdiEnumVidPnCofuncModality) != NULL;
    if (Cache->CanSetMode)
    {
        Status = DxgkVidPnClone((D3DKMDT_HVIDPN)Current, (D3DKMDT_HVIDPN *)&Candidate);
        if (!NT_SUCCESS(Status))
            goto Cleanup;
        Sources = Candidate->SourceModeSets[SourceId];
        Targets = Candidate->TargetModeSets[TargetIndex];
        Sources->NumModes = Targets->NumModes = 0;
        Sources->PinnedModeId = Targets->PinnedModeId = (UINT)-1;
        Sources->NextModeId = Targets->NextModeId = 0;
        Status = DxgkpEnumerateDisplayModeCandidate(Adapter, Candidate, FALSE);
        if (!NT_SUCCESS(Status))
            goto Cleanup;
        Sources = Candidate->SourceModeSets[SourceId];
        SourceCount = Sources->NumModes;
        if (SourceCount != 0)
        {
            SourceModes = ExAllocatePoolWithTag(PagedPool, SourceCount * sizeof(*SourceModes), TAG_DXGK_MODE);
            if (SourceModes == NULL)
            {
                Status = STATUS_INSUFFICIENT_RESOURCES;
                goto Cleanup;
            }
            RtlCopyMemory(SourceModes, Sources->Modes, SourceCount * sizeof(*SourceModes));
        }
        for (SourceIndex = 0; SourceIndex < SourceCount; ++SourceIndex)
        {
            /* A fixed source pivot asks for the target timings that actually
             * drive it, rather than forming a source/target Cartesian product. */
            Sources = Candidate->SourceModeSets[SourceId];
            Sources->Modes[0] = SourceModes[SourceIndex];
            Sources->NumModes = 1;
            Sources->PinnedModeId = SourceModes[SourceIndex].Id;
            Sources->NextModeId = Sources->PinnedModeId + 1;
            Targets = Candidate->TargetModeSets[TargetIndex];
            Targets->NumModes = 0;
            Targets->PinnedModeId = (UINT)-1;
            Targets->NextModeId = 0;
            Status = DxgkpEnumerateDisplayModeCandidate(Adapter, Candidate, TRUE);
            if (!NT_SUCCESS(Status))
                goto Cleanup;
            Targets = Candidate->TargetModeSets[TargetIndex];
            for (TargetModeIndex = 0; TargetModeIndex < Targets->NumModes; ++TargetModeIndex)
            {
                Status = DxgkpAppendDisplayMode(Cache, &SourceModes[SourceIndex], &Targets->Modes[TargetModeIndex].VideoSignalInfo);
                if (!NT_SUCCESS(Status))
                    goto Cleanup;
            }
        }
    }
PublishCache:
    if (Cache->Count == 0)
    {
        Status = STATUS_GRAPHICS_NO_AVAILABLE_VIDPN_TARGET;
        goto Cleanup;
    }
    if (Generation != InterlockedCompareExchange64(&Adapter->HotPlugGeneration, 0, 0))
    {
        Status = STATUS_RETRY;
        goto Cleanup;
    }
    Adapter->DisplayModeGeneration = (Adapter->DisplayModeGeneration + 1) & DXGKP_DISPLAY_MODE_GENERATION_MASK;
    if (Adapter->DisplayModeGeneration == 0)
        Adapter->DisplayModeGeneration = 1;
    Cache->Generation = Adapter->DisplayModeGeneration;
    DxgkpFreeDisplayModeCache(Adapter->DisplayModeCache);
    Adapter->DisplayModeCache = Cache;
    Cache = NULL;
    Status = STATUS_SUCCESS;

Cleanup:
    if (SourceModes != NULL)
        ExFreePoolWithTag(SourceModes, TAG_DXGK_MODE);
    if (Candidate != NULL)
        DxgkVidPnDestroy((D3DKMDT_HVIDPN)Candidate);
    if (Current != NULL)
        DxgkVidPnDestroy((D3DKMDT_HVIDPN)Current);
    DxgkpFreeDisplayModeCache(Cache);
    if (Transaction)
        DxgkEndKmdTransaction(Adapter);
    return Status;
}

static VOID
DxgkpVideoModeFromDisplayMode(
    _Out_ PVIDEO_MODE_INFORMATION VideoMode,
    _In_ CONST DXGKP_DISPLAY_MODE_ENTRY *Entry,
    _In_ ULONG ModeIndex)
{
    RtlZeroMemory(VideoMode, sizeof(*VideoMode));
    VideoMode->Length = sizeof(*VideoMode);
    VideoMode->ModeIndex = ModeIndex;
    VideoMode->VisScreenWidth = Entry->Mode.Width;
    VideoMode->VisScreenHeight = Entry->Mode.Height;
    /* The CDD maps dxgkrnl's packed CPU shadow, not the miniport's pitch. */
    VideoMode->ScreenStride = Entry->Mode.Width * 4;
    VideoMode->NumberOfPlanes = 1;
    VideoMode->BitsPerPlane = 32;
    VideoMode->Frequency = Entry->Mode.IntegerRefreshRate;
    VideoMode->XMillimeter = 320;
    VideoMode->YMillimeter = 240;
    VideoMode->NumberRedBits = VideoMode->NumberGreenBits = VideoMode->NumberBlueBits = 8;
    VideoMode->RedMask = 0x00ff0000;
    VideoMode->GreenMask = 0x0000ff00;
    VideoMode->BlueMask = 0x000000ff;
    VideoMode->AttributeFlags = VIDEO_MODE_COLOR | VIDEO_MODE_GRAPHICS;
    if (Entry->Mode.ScanLineOrdering == D3DDDI_VSSLO_INTERLACED_UPPERFIELDFIRST ||
        Entry->Mode.ScanLineOrdering == D3DDDI_VSSLO_INTERLACED_LOWERFIELDFIRST)
        VideoMode->AttributeFlags |= VIDEO_MODE_INTERLACED;
    VideoMode->VideoMemoryBitmapWidth = Entry->Mode.Width;
    VideoMode->VideoMemoryBitmapHeight = Entry->Mode.Height;
    VideoMode->DriverSpecificAttributeFlags = 1; /* DXGK_DISP_DRIVERSPEC_SYSMEM_FB */
}

NTSTATUS
DxgkVidPnQueryVideoModes(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _Out_writes_opt_(Capacity) PVIDEO_MODE_INFORMATION Modes,
    _In_ ULONG Capacity,
    _Out_ PULONG Count,
    _In_ BOOLEAN CurrentOnly)
{
    DXGKP_DISPLAY_MODE_CACHE *Cache;
    D3DKMT_CURRENTDISPLAYMODE Current;
    ULONG Index, Required = 0, Output = 0;
    NTSTATUS Status;

    PAGED_CODE();
    if (Adapter == NULL || Count == NULL)
        return STATUS_INVALID_PARAMETER;
    *Count = 0;
    (VOID)KeWaitForSingleObject(&Adapter->SharedPrimaryMutex, Executive, KernelMode, FALSE, NULL);
    Status = DxgkpEnsureDisplayModeCacheLocked(Adapter);
    if (!NT_SUCCESS(Status))
        goto Cleanup;
    Cache = Adapter->DisplayModeCache;
    if (CurrentOnly)
    {
        RtlZeroMemory(&Current, sizeof(Current));
        Current.VidPnSourceId = Cache->SourceId;
        Status = DxgkVidPnQueryCurrentDisplayMode(Adapter, &Current);
        if (!NT_SUCCESS(Status))
            goto Cleanup;
    }
    for (Index = 0; Index < Cache->Count; ++Index)
    {
        if (DxgkpDisplayModeSupportsGdi(&Cache->Entries[Index]) &&
            (!CurrentOnly || DxgkpDisplayModesEqual(&Cache->Entries[Index].Mode, &Current.DisplayMode)))
            ++Required;
    }
    *Count = Required;
    if (Required == 0)
    {
        Status = STATUS_GRAPHICS_VIDPN_MODALITY_NOT_SUPPORTED;
        goto Cleanup;
    }
    if (Modes == NULL)
        goto Cleanup;
    if (Capacity < Required)
    {
        Status = STATUS_BUFFER_TOO_SMALL;
        goto Cleanup;
    }
    for (Index = 0; Index < Cache->Count; ++Index)
    {
        if (DxgkpDisplayModeSupportsGdi(&Cache->Entries[Index]) &&
            (!CurrentOnly || DxgkpDisplayModesEqual(&Cache->Entries[Index].Mode, &Current.DisplayMode)))
        {
            DxgkpVideoModeFromDisplayMode(&Modes[Output++], &Cache->Entries[Index],
                                          (Cache->Generation << DXGKP_DISPLAY_MODE_INDEX_BITS) | Index);
        }
    }
Cleanup:
    KeReleaseMutex(&Adapter->SharedPrimaryMutex, FALSE);
    return Status;
}

NTSTATUS
NTAPI
DxgkGetDisplayModeList(
    _Inout_ D3DKMT_GETDISPLAYMODELIST *pGetDisplayModeList)
{
    PDXGKRNL_ADAPTER Adapter = NULL;
    DXGKP_DISPLAY_MODE_CACHE *Cache;
    D3DKMT_CURRENTDISPLAYMODE Current;
    BOOLEAN Locked = FALSE;
    NTSTATUS Status = STATUS_SUCCESS;
    UINT NumModes, i;

    PAGED_CODE();

    if (pGetDisplayModeList == NULL)
        return STATUS_INVALID_PARAMETER;

    Adapter = DxgkLookupAdapterByHandle(pGetDisplayModeList->hAdapter);
    if (Adapter == NULL)
        return STATUS_INVALID_PARAMETER;

    if (pGetDisplayModeList->VidPnSourceId >= Adapter->NumberOfVideoPresentSources)
    {
        Status = STATUS_INVALID_PARAMETER;
        goto Cleanup;
    }

    (VOID)KeWaitForSingleObject(&Adapter->SharedPrimaryMutex, Executive, KernelMode, FALSE, NULL);
    Locked = TRUE;
    Status = DxgkpEnsureDisplayModeCacheLocked(Adapter);
    if (!NT_SUCCESS(Status))
        goto Cleanup;
    Cache = Adapter->DisplayModeCache;
    NumModes = Cache->Count;
    if (pGetDisplayModeList->VidPnSourceId != Cache->SourceId)
    {
        /* The legacy display device switches its primary path only. Other
         * active sources can still report their exact committed mode. */
        RtlZeroMemory(&Current, sizeof(Current));
        Current.VidPnSourceId = pGetDisplayModeList->VidPnSourceId;
        Status = DxgkVidPnQueryCurrentDisplayMode(Adapter, &Current);
        if (!NT_SUCCESS(Status))
            goto Cleanup;
        NumModes = 1;
    }

    /* Pass 1: caller wants the mode count only. */
    if (pGetDisplayModeList->pModeList == NULL || pGetDisplayModeList->ModeCount == 0)
    {
        pGetDisplayModeList->ModeCount = NumModes;
        goto Cleanup;
    }

    if (pGetDisplayModeList->ModeCount < NumModes)
    {
        pGetDisplayModeList->ModeCount = NumModes;
        Status = STATUS_BUFFER_TOO_SMALL;
        goto Cleanup;
    }

    _SEH2_TRY
    {
        for (i = 0; i < NumModes; i++)
        {
            pGetDisplayModeList->pModeList[i] = pGetDisplayModeList->VidPnSourceId == Cache->SourceId ?
                                               Cache->Entries[i].Mode : Current.DisplayMode;
        }
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Status = _SEH2_GetExceptionCode();
    }
    _SEH2_END;

    if (NT_SUCCESS(Status))
        pGetDisplayModeList->ModeCount = NumModes;

Cleanup:
    if (Locked)
        KeReleaseMutex(&Adapter->SharedPrimaryMutex, FALSE);
    DxgkDereferenceAdapter(Adapter);
    return Status;
}

NTSTATUS
DxgkVidPnSetVideoMode(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _In_ ULONG ModeIndex)
{
    DXGKP_DISPLAY_MODE_CACHE *Cache;
    CONST DXGKP_DISPLAY_MODE_ENTRY *Entry;
    PDXGKP_VIDPN Current = NULL, Candidate = NULL, Rollback = NULL, Detached = NULL;
    PDXGKP_VIDPN_SOURCE_MODESET Sources;
    PDXGKP_VIDPN_TARGET_MODESET Targets;
    D3DKMDT_VIDEO_SIGNAL_INFO OldTarget;
    D3DKMT_CURRENTDISPLAYMODE CurrentMode;
    DXGKP_DISPLAY_COMMIT_RESULT Result, RollbackResult;
    ULONG Index, TargetIndex, OldWidth, OldHeight;
    SIZE_T TargetModeIndex;
    BOOLEAN Transaction = FALSE, Mutation = FALSE, RecoveryRequired = FALSE;
    BOOLEAN HaveOldTarget = FALSE;
    KIRQL OldIrql;
    NTSTATUS Status, RollbackStatus;

    PAGED_CODE();
    if (Adapter == NULL)
        return STATUS_INVALID_PARAMETER;
    ModeIndex &= ~(VIDEO_MODE_NO_ZERO_MEMORY | VIDEO_MODE_MAP_MEM_LINEAR);
    (VOID)KeWaitForSingleObject(&Adapter->SharedPrimaryMutex, Executive, KernelMode, FALSE, NULL);
    if (ModeIndex == 0)
    {
        Status = (Adapter->VidPnCommitted || Adapter->HeadlessDesktop) ?
                     STATUS_SUCCESS : DxgkpDisplayCommitVidPnWhileSharedPrimaryLocked(Adapter);
        goto Cleanup;
    }
    Status = DxgkpEnsureDisplayModeCacheLocked(Adapter);
    if (!NT_SUCCESS(Status))
        goto Cleanup;
    Cache = Adapter->DisplayModeCache;
    Index = ModeIndex & DXGKP_DISPLAY_MODE_INDEX_MASK;
    if ((ModeIndex >> DXGKP_DISPLAY_MODE_INDEX_BITS) != Cache->Generation || Index >= Cache->Count)
    {
        Status = STATUS_INVALID_PARAMETER;
        goto Cleanup;
    }
    Entry = &Cache->Entries[Index];
    RtlZeroMemory(&CurrentMode, sizeof(CurrentMode));
    CurrentMode.VidPnSourceId = Cache->SourceId;
    Status = DxgkVidPnQueryCurrentDisplayMode(Adapter, &CurrentMode);
    if (NT_SUCCESS(Status) && DxgkpDisplayModesEqual(&CurrentMode.DisplayMode, &Entry->Mode))
        goto Cleanup;
    if (!Cache->CanSetMode || !DxgkpDisplayModeSupportsGdi(Entry))
    {
        Status = STATUS_GRAPHICS_VIDPN_MODALITY_NOT_SUPPORTED;
        goto Cleanup;
    }
    DxgkpBeginSharedSurfaceMutationLocked(Adapter);
    Mutation = TRUE;
    if (!DxgkBeginKmdTransaction(Adapter))
    {
        Status = STATUS_DELETE_PENDING;
        goto Cleanup;
    }
    Transaction = TRUE;
    (VOID)KeWaitForSingleObject(&Adapter->VidPnMutex, Executive, KernelMode, FALSE, NULL);
    Current = (PDXGKP_VIDPN)Adapter->VidPn;
    if (!DxgkVidPnReference((D3DKMDT_HVIDPN)Current))
        Current = NULL;
    OldWidth = Adapter->CommittedWidth;
    OldHeight = Adapter->CommittedHeight;
    KeReleaseMutex(&Adapter->VidPnMutex, FALSE);
    /* The mode is the desktop source's (Paths[0]); outputs cloned from it
     * are given timings for the new source mode by the commit. */
    if (Current == NULL || Current->NumPaths == 0 ||
        Current->Paths[0].VidPnSourceId != Cache->SourceId || Current->Paths[0].VidPnTargetId != Cache->TargetId)
    {
        Status = STATUS_RETRY;
        goto Cleanup;
    }
    TargetIndex = DxgkVidPnTargetIndexFromId(Current, Cache->TargetId);
    if (TargetIndex != MAXULONG && (Targets = Current->TargetModeSets[TargetIndex]) != NULL)
    {
        for (TargetModeIndex = 0; TargetModeIndex < Targets->NumModes; ++TargetModeIndex)
        {
            if (Targets->Modes[TargetModeIndex].Id == Targets->PinnedModeId)
            {
                OldTarget = Targets->Modes[TargetModeIndex].VideoSignalInfo;
                HaveOldTarget = TRUE;
                break;
            }
        }
    }
    if (!HaveOldTarget)
    {
        Status = STATUS_INVALID_DEVICE_STATE;
        goto Cleanup;
    }
    Status = DxgkVidPnClone((D3DKMDT_HVIDPN)Current, (D3DKMDT_HVIDPN *)&Candidate);
    if (!NT_SUCCESS(Status))
        goto Cleanup;
    /* Allocate the rollback object before hardware changes. The miniport is
     * allowed to replace mode sets, so rollback must not mutate the live one. */
    Status = DxgkVidPnClone((D3DKMDT_HVIDPN)Current, (D3DKMDT_HVIDPN *)&Rollback);
    if (!NT_SUCCESS(Status))
        goto Cleanup;
    Sources = Candidate->SourceModeSets[Cache->SourceId];
    Sources->Modes[0] = Entry->Source;
    Sources->NumModes = 1;
    Sources->PinnedModeId = Entry->Source.Id;
    Sources->NextModeId = Entry->Source.Id + 1;
    if (Cache->HotPlugGeneration != InterlockedCompareExchange64(&Adapter->HotPlugGeneration, 0, 0))
    {
        Status = STATUS_RETRY;
        goto Cleanup;
    }
    Status = DxgkpDisplayCommitVidPnCandidateWithTarget(Adapter, (D3DKMDT_HVIDPN)Candidate, &Entry->Target, &Result);
    if (NT_SUCCESS(Status) && (!Result.VidPnCommitted || Result.CommittedWidth != Entry->Mode.Width || Result.CommittedHeight != Entry->Mode.Height))
        Status = STATUS_GRAPHICS_VIDPN_MODALITY_NOT_SUPPORTED;
    if (NT_SUCCESS(Status))
    {
        (VOID)KeWaitForSingleObject(&Adapter->VidPnMutex, Executive, KernelMode, FALSE, NULL);
        KeAcquireSpinLock(&Adapter->ChildListLock, &OldIrql);
        if (Adapter->VidPn == (HANDLE)Current &&
            Cache->HotPlugGeneration == InterlockedCompareExchange64(&Adapter->HotPlugGeneration, 0, 0))
        {
            Detached = (PDXGKP_VIDPN)Adapter->VidPn;
            Adapter->VidPn = (HANDLE)Candidate;
            Adapter->CommittedWidth = Result.CommittedWidth;
            Adapter->CommittedHeight = Result.CommittedHeight;
            Adapter->VidPnCommitted = TRUE;
            Adapter->HeadlessDesktop = FALSE;
            DxgkVidPnPublishVsyncTargetMap(Adapter, (D3DKMDT_HVIDPN)Candidate);
            Candidate = NULL;
        }
        else
            Status = STATUS_RETRY;
        KeReleaseSpinLock(&Adapter->ChildListLock, OldIrql);
        KeReleaseMutex(&Adapter->VidPnMutex, FALSE);
    }
    if (!NT_SUCCESS(Status))
    {
        /* A failed commit can have changed hardware partially. Reapply the
         * old pinned source and exact timing before exposing its old state. */
        RollbackStatus = DxgkpDisplayCommitVidPnCandidateWithTarget(Adapter, (D3DKMDT_HVIDPN)Rollback, &OldTarget, &RollbackResult);
        if (!NT_SUCCESS(RollbackStatus) || !RollbackResult.VidPnCommitted ||
            RollbackResult.CommittedWidth != OldWidth || RollbackResult.CommittedHeight != OldHeight)
        {
            (VOID)KeWaitForSingleObject(&Adapter->VidPnMutex, Executive, KernelMode, FALSE, NULL);
            Adapter->VidPnCommitted = FALSE;
            DxgkVidPnPublishVsyncTargetMap(Adapter, NULL);
            KeReleaseMutex(&Adapter->VidPnMutex, FALSE);
            RecoveryRequired = TRUE;
            DXGKRNL_ERR("DxgkVidPnSetVideoMode: commit 0x%08lX, rollback 0x%08lX; failing adapter closed\n", Status, RollbackStatus);
        }
        goto Cleanup;
    }
    if (OldWidth != Result.CommittedWidth || OldHeight != Result.CommittedHeight)
        DxgkpDestroySharedPrimaryLocked(Adapter);
    DxgkpDisplayPublishInitialMode(Adapter);
    for (TargetModeIndex = 0;
         TargetModeIndex < Current->NumPaths &&
         TargetModeIndex < ((PDXGKP_VIDPN)Adapter->VidPn)->NumPaths;
         TargetModeIndex++)
    {
        DxgkpNotifyActivePathChanged(Adapter, &Current->Paths[TargetModeIndex],
                                     &((PDXGKP_VIDPN)Adapter->VidPn)->Paths[TargetModeIndex]);
    }

Cleanup:
    if (Transaction)
        DxgkEndKmdTransaction(Adapter);
    if (RecoveryRequired)
        Status = DxgkpRecoverFailedHotPlugRollback(Adapter);
    if (Mutation)
        DxgkpEndSharedSurfaceMutationLocked(Adapter);
    KeReleaseMutex(&Adapter->SharedPrimaryMutex, FALSE);
    if (Detached != NULL)
        DxgkVidPnDestroy((D3DKMDT_HVIDPN)Detached);
    if (Current != NULL)
        DxgkVidPnDestroy((D3DKMDT_HVIDPN)Current);
    if (Candidate != NULL)
        DxgkVidPnDestroy((D3DKMDT_HVIDPN)Candidate);
    if (Rollback != NULL)
        DxgkVidPnDestroy((D3DKMDT_HVIDPN)Rollback);
    return Status;
}

NTSTATUS
NTAPI
DxgkpSetVidPnSourceOwnerWithFlagsAndAccessMode(
    _In_ D3DKMT_SETVIDPNSOURCEOWNER *pSetVidPnSourceOwner,
    _In_ UINT OwnerFlags,
    _In_ KPROCESSOR_MODE EmbeddedBufferMode)
{
    PDXGKP_SOURCE_OWNER_ADAPTER_STATE State;
    PDXGKP_SOURCE_OWNER_ADAPTER_STATE StateToFree = NULL;
    PDXGKRNL_DEVICE Device = NULL;
    PDXGKRNL_ADAPTER Adapter = NULL;
    D3DKMT_VIDPNSOURCEOWNER_TYPE *Types = NULL;
    D3DDDI_VIDEO_PRESENT_SOURCE_ID *SourceIds = NULL;
    DXGKP_VIDPN_SOURCE_OWNER StagedOwners[DXGKP_MAX_SOURCES];
    BOOLEAN SeenSources[DXGKP_MAX_SOURCES];
    BOOLEAN HasExclusiveGdi = FALSE;
    BOOLEAN ReleaseAll;
    NTSTATUS Status = STATUS_SUCCESS;
    SIZE_T TypesSize = 0;
    SIZE_T SourceIdsSize = 0;
    ULONG i;

    PAGED_CODE();

    if (pSetVidPnSourceOwner == NULL)
        return STATUS_INVALID_PARAMETER;
    if (pSetVidPnSourceOwner->VidPnSourceCount > DXGKP_MAX_SOURCE_OWNER_OPERATIONS)
        return STATUS_INVALID_PARAMETER;
    if (pSetVidPnSourceOwner->VidPnSourceCount != 0 && (pSetVidPnSourceOwner->pType == NULL || pSetVidPnSourceOwner->pVidPnSourceId == NULL))
        return STATUS_INVALID_PARAMETER;
    /* A zero count is a release-all request; the arrays are then ignored, not
     * policed.  Windows 11 accepts it with non-NULL pointers still supplied. */
    if ((OwnerFlags & ~DXGKP_SOURCE_OWNER_FLAG_VALID_MASK) != 0)
        return STATUS_INVALID_PARAMETER;
    if ((OwnerFlags & DXGKP_SOURCE_OWNER_FLAG_ALLOW_OUTPUT_DUPLICATION) != 0)
        return STATUS_NOT_SUPPORTED;
    if ((OwnerFlags & DXGKP_SOURCE_OWNER_FLAG_DISABLE_DWM_VIRTUAL_MODE) != 0)
        return STATUS_NOT_SUPPORTED;
    if ((OwnerFlags & DXGKP_SOURCE_OWNER_FLAG_USE_NT_HANDLES) != 0)
        return STATUS_NOT_SUPPORTED;

    Device = DxgkLookupDeviceByHandle(pSetVidPnSourceOwner->hDevice, &Adapter);
    if (Device == NULL)
        return STATUS_INVALID_PARAMETER;
    if (Adapter == NULL)
    {
        DxgkDereferenceDevice(Device);
        return STATUS_INVALID_HANDLE;
    }
    if (InterlockedCompareExchange(&Device->ExecutionState, 0, 0) != D3DKMT_DEVICEEXECUTION_ACTIVE)
    {
        Status = STATUS_DEVICE_REMOVED;
        goto Cleanup;
    }

    ReleaseAll = pSetVidPnSourceOwner->VidPnSourceCount == 0;
    if (!ReleaseAll)
    {
        TypesSize = (SIZE_T)pSetVidPnSourceOwner->VidPnSourceCount * sizeof(*Types);
        SourceIdsSize = (SIZE_T)pSetVidPnSourceOwner->VidPnSourceCount * sizeof(*SourceIds);
        Status = DxgkpCaptureUserBuffer(pSetVidPnSourceOwner->pType, TypesSize, EmbeddedBufferMode, TAG_DXGK_CAPTURE, (PVOID *)&Types);
        if (NT_SUCCESS(Status))
            Status = DxgkpCaptureUserBuffer(pSetVidPnSourceOwner->pVidPnSourceId, SourceIdsSize, EmbeddedBufferMode, TAG_DXGK_CAPTURE, (PVOID *)&SourceIds);
        if (!NT_SUCCESS(Status))
            goto Cleanup;

        for (i = 0; i < pSetVidPnSourceOwner->VidPnSourceCount; ++i)
        {
            if (SourceIds[i] >= DXGKP_MAX_SOURCES || SourceIds[i] >= Adapter->NumberOfVideoPresentSources)
            {
                /* Windows 11 rejects an out-of-range source in this array with
                 * the generic status, not the graphics-specific one. */
                Status = STATUS_INVALID_PARAMETER;
                goto Cleanup;
            }
            if (Types[i] < D3DKMT_VIDPNSOURCEOWNER_UNOWNED || Types[i] > D3DKMT_VIDPNSOURCEOWNER_EMULATED)
            {
                Status = STATUS_INVALID_PARAMETER;
                goto Cleanup;
            }
            if (Types[i] == D3DKMT_VIDPNSOURCEOWNER_SHARED && Device->Flags.LegacyMode)
            {
                Status = STATUS_INVALID_PARAMETER;
                goto Cleanup;
            }
            if (Types[i] == D3DKMT_VIDPNSOURCEOWNER_EXCLUSIVEGDI)
                HasExclusiveGdi = TRUE;
        }

        if (HasExclusiveGdi)
        {
            if (!Device->Flags.LegacyMode || Adapter->NumberOfVideoPresentSources > DXGKP_MAX_SOURCES || pSetVidPnSourceOwner->VidPnSourceCount != Adapter->NumberOfVideoPresentSources)
            {
                Status = STATUS_INVALID_PARAMETER;
                goto Cleanup;
            }
            RtlZeroMemory(SeenSources, sizeof(SeenSources));
            for (i = 0; i < pSetVidPnSourceOwner->VidPnSourceCount; ++i)
            {
                if (Types[i] != D3DKMT_VIDPNSOURCEOWNER_EXCLUSIVEGDI || SeenSources[SourceIds[i]])
                {
                    Status = STATUS_INVALID_PARAMETER;
                    goto Cleanup;
                }
                SeenSources[SourceIds[i]] = TRUE;
            }
        }
    }

    DxgkpEnsureSourceOwnerMutex();
    KeEnterCriticalRegion();
    ExAcquireResourceExclusiveLite(&g_SourceProgrammingResource, TRUE);
    ExAcquireFastMutex(&g_SourceOwnerMutex);
    if (InterlockedCompareExchange(&Device->Destroying, 0, 0) != 0 || InterlockedCompareExchange(&Device->ExecutionState, 0, 0) != D3DKMT_DEVICEEXECUTION_ACTIVE)
    {
        Status = STATUS_DEVICE_REMOVED;
        goto Unlock;
    }
    State = DxgkpFindSourceOwnerAdapterLocked(Adapter);
    if (State != NULL)
        RtlCopyMemory(StagedOwners, State->Owners, sizeof(StagedOwners));
    else
        RtlZeroMemory(StagedOwners, sizeof(StagedOwners));

    if (ReleaseAll)
    {
        for (i = 0; i < DXGKP_MAX_SOURCES; ++i)
        {
            if (StagedOwners[i].OwnerDevice == Device)
            {
                StagedOwners[i].OwnerDevice = NULL;
                StagedOwners[i].OwnerType = D3DKMT_VIDPNSOURCEOWNER_UNOWNED;
                StagedOwners[i].OwnerFlags = 0;
            }
        }
    }
    else
    {
        for (i = 0; i < pSetVidPnSourceOwner->VidPnSourceCount; ++i)
        {
            Status = DxgkpApplySourceOwnerOperation(StagedOwners, Device, SourceIds[i], Types[i], OwnerFlags);
            if (!NT_SUCCESS(Status))
                goto Unlock;
        }
    }

    if (DxgkpSourceOwnerStateIsEmpty(StagedOwners))
    {
        if (State != NULL)
        {
            RemoveEntryList(&State->Entry);
            StateToFree = State;
        }
    }
    else
    {
        if (State == NULL)
        {
            State = ExAllocatePoolWithTag(NonPagedPool, sizeof(*State), TAG_DXGK_SOURCE_OWNER);
            if (State == NULL)
            {
                Status = STATUS_INSUFFICIENT_RESOURCES;
                goto Unlock;
            }
            RtlZeroMemory(State, sizeof(*State));
            State->Adapter = Adapter;
            InsertTailList(&g_SourceOwnerAdapterList, &State->Entry);
        }
        RtlCopyMemory(State->Owners, StagedOwners, sizeof(State->Owners));
    }

Unlock:
    ExReleaseFastMutex(&g_SourceOwnerMutex);
    ExReleaseResourceLite(&g_SourceProgrammingResource);
    KeLeaveCriticalRegion();
    if (StateToFree != NULL)
    {
        RtlZeroMemory(StateToFree->Owners, sizeof(StateToFree->Owners));
        ExFreePoolWithTag(StateToFree, TAG_DXGK_SOURCE_OWNER);
    }

Cleanup:
    if (SourceIds != NULL)
        ExFreePoolWithTag(SourceIds, TAG_DXGK_CAPTURE);
    if (Types != NULL)
        ExFreePoolWithTag(Types, TAG_DXGK_CAPTURE);
    DxgkDereferenceDevice(Device);
    return Status;
}

NTSTATUS
NTAPI
DxgkpSetVidPnSourceOwnerWithAccessMode(
    _In_ D3DKMT_SETVIDPNSOURCEOWNER *pSetVidPnSourceOwner,
    _In_ KPROCESSOR_MODE EmbeddedBufferMode)
{
    return DxgkpSetVidPnSourceOwnerWithFlagsAndAccessMode(pSetVidPnSourceOwner, 0, EmbeddedBufferMode);
}

NTSTATUS
NTAPI
DxgkSetVidPnSourceOwner(
    _In_ D3DKMT_SETVIDPNSOURCEOWNER *pSetVidPnSourceOwner)
{
    return DxgkpSetVidPnSourceOwnerWithAccessMode(pSetVidPnSourceOwner, KernelMode);
}

BOOLEAN
NTAPI
DxgkpIsAnyVidPnSourceExclusivelyOwned(VOID)
{
    PLIST_ENTRY Entry;
    PDXGKP_SOURCE_OWNER_ADAPTER_STATE State;
    BOOLEAN Owned = FALSE;
    ULONG i;

    PAGED_CODE();

    DxgkpEnsureSourceOwnerMutex();
    ExAcquireFastMutex(&g_SourceOwnerMutex);
    for (Entry = g_SourceOwnerAdapterList.Flink; Entry != &g_SourceOwnerAdapterList && !Owned; Entry = Entry->Flink)
    {
        State = CONTAINING_RECORD(Entry, DXGKP_SOURCE_OWNER_ADAPTER_STATE, Entry);
        for (i = 0; i < DXGKP_MAX_SOURCES; ++i)
        {
            if (State->Owners[i].OwnerDevice != NULL &&
                (State->Owners[i].OwnerType == D3DKMT_VIDPNSOURCEOWNER_EXCLUSIVE ||
                 State->Owners[i].OwnerType == D3DKMT_VIDPNSOURCEOWNER_EXCLUSIVEGDI))
            {
                Owned = TRUE;
                break;
            }
        }
    }
    ExReleaseFastMutex(&g_SourceOwnerMutex);
    return Owned;
}

NTSTATUS
NTAPI
DxgkCheckVidPnExclusiveOwnership(
    _In_ CONST D3DKMT_CHECKVIDPNEXCLUSIVEOWNERSHIP *pCheckVidPnExclusiveOwnership)
{
    PDXGKP_SOURCE_OWNER_ADAPTER_STATE State;
    PDXGKRNL_ADAPTER Adapter = NULL;
    NTSTATUS Status = STATUS_SUCCESS;

    PAGED_CODE();

    if (pCheckVidPnExclusiveOwnership == NULL)
        return STATUS_INVALID_PARAMETER;

    Adapter = DxgkLookupAdapterByHandle(pCheckVidPnExclusiveOwnership->hAdapter);
    if (Adapter == NULL)
        return STATUS_INVALID_PARAMETER;
    if (Adapter->State != DxgkAdapterStateStarted)
    {
        Status = STATUS_DEVICE_REMOVED;
        goto Cleanup;
    }

    if (pCheckVidPnExclusiveOwnership->VidPnSourceId >= DXGKP_MAX_SOURCES || pCheckVidPnExclusiveOwnership->VidPnSourceId >= Adapter->NumberOfVideoPresentSources)
    {
        Status = STATUS_GRAPHICS_INVALID_VIDEO_PRESENT_SOURCE;
        goto Cleanup;
    }

    DxgkpEnsureSourceOwnerMutex();

    ExAcquireFastMutex(&g_SourceOwnerMutex);
    State = DxgkpFindSourceOwnerAdapterLocked(Adapter);
    if (State != NULL && State->Owners[pCheckVidPnExclusiveOwnership->VidPnSourceId].OwnerDevice != NULL && (State->Owners[pCheckVidPnExclusiveOwnership->VidPnSourceId].OwnerType == D3DKMT_VIDPNSOURCEOWNER_EXCLUSIVE || State->Owners[pCheckVidPnExclusiveOwnership->VidPnSourceId].OwnerType == D3DKMT_VIDPNSOURCEOWNER_EXCLUSIVEGDI))
        Status = STATUS_GRAPHICS_PRESENT_OCCLUDED;
    ExReleaseFastMutex(&g_SourceOwnerMutex);

Cleanup:
    DxgkDereferenceAdapter(Adapter);
    return Status;
}
