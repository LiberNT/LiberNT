/*
 * PROJECT:     LiberNT NDIS library
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     NDIS 6 filter driver registration and per-adapter module lifecycle
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "ndis6_internal.h"

#ifndef EXPORT
#define EXPORT NTAPI
#endif

/* ------------------------------------------------------------------ */
/*  Filter driver registration and per-adapter module lifecycle.      */
/* ------------------------------------------------------------------ */

LIST_ENTRY  g_Ndis6FilterDriverList;
KSPIN_LOCK  g_Ndis6FilterDriverListLock;
static KMUTEX g_Ndis6FilterLifecycleMutex;
static volatile LONG g_Ndis6FilterDriverListState;

#define NDIS6_FILTER_DRIVER_TAG  'fDNn'  /* "nNDf" */
#define NDIS6_FILTER_NAME_TAG    'sFNn'
#define NDIS6_FILTER_MODULE_TAG  'mFNn'  /* "nNFm" */
#define NDIS6_FILTER_SNAPSHOT_TAG 'aFNn'

static VOID
Ndis6DetachFilterModule(
    _In_ PNDIS6_FILTER_MODULE Module);

static VOID
Ndis6AttachFilterDriverToAllAdapters(
    _In_ PNDIS6_FILTER_DRIVER_BLOCK Block);

VOID
Ndis6FilterDriverListInit(VOID)
{
    if (InterlockedCompareExchange(&g_Ndis6FilterDriverListState, 1, 0) == 0)
    {
        InitializeListHead(&g_Ndis6FilterDriverList);
        KeInitializeSpinLock(&g_Ndis6FilterDriverListLock);
        KeInitializeMutex(&g_Ndis6FilterLifecycleMutex, 0);
        InterlockedExchange(&g_Ndis6FilterDriverListState, 2);
        return;
    }

    while (InterlockedCompareExchange(&g_Ndis6FilterDriverListState, 2, 2) != 2)
        YieldProcessor();
}

static BOOLEAN
Ndis6IsSupportedFilterMinorVersion(
    _In_ UCHAR MinorVersion)
{
    switch (MinorVersion)
    {
        case 0:
        case 1:
        case 20:
        case 30:
        case 40:
        case 50:
        case 51:
        case 60:
        case 70:
        case 80:
        case 81:
        case 82:
        case 83:
        case 84:
        case 85:
        case 86:
        case 87:
        case 88:
        case 89:
            return TRUE;

        default:
            return FALSE;
    }
}

static BOOLEAN
Ndis6ValidateFilterName(
    _In_ PNDIS_STRING Name)
{
    return !((Name->Length != 0 && Name->Buffer == NULL) ||
             Name->Length > Name->MaximumLength ||
             Name->Length > MAXUSHORT - sizeof(WCHAR) ||
             (Name->Length & (sizeof(WCHAR) - 1)) != 0);
}

static NDIS_STATUS
Ndis6ValidateFilterCharacteristics(
    _In_ PNDIS_FILTER_DRIVER_CHARACTERISTICS Characteristics,
    _Out_ PULONG CopySize)
{
    ULONG AllowedFlags;
    ULONG RequiredSize;
    UCHAR RequiredRevision;

    if (Characteristics->Header.Type !=
        NDIS_OBJECT_TYPE_FILTER_DRIVER_CHARACTERISTICS)
    {
        return NDIS_STATUS_BAD_CHARACTERISTICS;
    }

    if (Characteristics->MajorNdisVersion != 6 ||
        !Ndis6IsSupportedFilterMinorVersion(Characteristics->MinorNdisVersion))
    {
        return NDIS_STATUS_BAD_VERSION;
    }

    RequiredRevision = Characteristics->MinorNdisVersion >= 80
        ? NDIS_FILTER_DRIVER_CHARACTERISTICS_REVISION_3
        : (Characteristics->MinorNdisVersion >= 1
            ? NDIS_FILTER_DRIVER_CHARACTERISTICS_REVISION_2
            : NDIS_FILTER_DRIVER_CHARACTERISTICS_REVISION_1);
    if (Characteristics->Header.Revision != RequiredRevision)
        return NDIS_STATUS_BAD_VERSION;

    switch (RequiredRevision)
    {
        case NDIS_FILTER_DRIVER_CHARACTERISTICS_REVISION_1:
            RequiredSize = NDIS_SIZEOF_FILTER_DRIVER_CHARACTERISTICS_REVISION_1;
            break;
        case NDIS_FILTER_DRIVER_CHARACTERISTICS_REVISION_2:
            RequiredSize = NDIS_SIZEOF_FILTER_DRIVER_CHARACTERISTICS_REVISION_2;
            break;
        case NDIS_FILTER_DRIVER_CHARACTERISTICS_REVISION_3:
            RequiredSize = NDIS_SIZEOF_FILTER_DRIVER_CHARACTERISTICS_REVISION_3;
            break;
        default:
            return NDIS_STATUS_BAD_VERSION;
    }

    if (Characteristics->Header.Size < RequiredSize ||
        RequiredSize > sizeof(*Characteristics) ||
        Characteristics->AttachHandler == NULL ||
        Characteristics->DetachHandler == NULL ||
        Characteristics->RestartHandler == NULL ||
        Characteristics->PauseHandler == NULL ||
        (Characteristics->OidRequestHandler != NULL &&
         Characteristics->OidRequestCompleteHandler == NULL) ||
        (RequiredRevision >= NDIS_FILTER_DRIVER_CHARACTERISTICS_REVISION_2 &&
         Characteristics->DirectOidRequestCompleteHandler != NULL &&
         Characteristics->DirectOidRequestHandler == NULL) ||
        ((Characteristics->ReceiveNetBufferListsHandler != NULL ||
          Characteristics->ReturnNetBufferListsHandler != NULL) &&
         Characteristics->StatusHandler == NULL) ||
        !Ndis6ValidateFilterName(&Characteristics->FriendlyName) ||
        !Ndis6ValidateFilterName(&Characteristics->UniqueName) ||
        !Ndis6ValidateFilterName(&Characteristics->ServiceName))
    {
        return NDIS_STATUS_BAD_CHARACTERISTICS;
    }

    AllowedFlags = NDIS_FILTER_DRIVER_MANDATORY;
    if (Characteristics->MinorNdisVersion >= 50)
    {
        AllowedFlags |= NDIS_FILTER_DRIVER_SUPPORTS_CURRENT_MAC_ADDRESS_CHANGE |
                        NDIS_FILTER_DRIVER_SUPPORTS_L2_MTU_SIZE_CHANGE;
    }
    if (Characteristics->MinorNdisVersion >= 89)
        AllowedFlags |= NDIS_FILTER_DRIVER_UDP_RSC_NOT_SUPPORTED;
    if ((Characteristics->Flags & ~AllowedFlags) != 0)
        return NDIS_STATUS_BAD_CHARACTERISTICS;

    *CopySize = RequiredSize;
    return NDIS_STATUS_SUCCESS;
}

static NDIS_STATUS
Ndis6CopyFilterName(
    _In_ PNDIS_STRING Source,
    _Out_ PNDIS_STRING Destination,
    _Out_ PWCHAR *OwnedBuffer)
{
    ULONG Bytes = Source->Length;

    *OwnedBuffer = NULL;
    RtlZeroMemory(Destination, sizeof(*Destination));
    if (Bytes == 0)
        return NDIS_STATUS_SUCCESS;

    *OwnedBuffer = ExAllocatePoolWithTag(PagedPool, Bytes + sizeof(WCHAR), NDIS6_FILTER_NAME_TAG);
    if (*OwnedBuffer == NULL)
        return NDIS_STATUS_RESOURCES;

    RtlCopyMemory(*OwnedBuffer, Source->Buffer, Bytes);
    (*OwnedBuffer)[Bytes / sizeof(WCHAR)] = UNICODE_NULL;
    Destination->Buffer = *OwnedBuffer;
    Destination->Length = (USHORT)Bytes;
    Destination->MaximumLength = (USHORT)(Bytes + sizeof(WCHAR));
    return NDIS_STATUS_SUCCESS;
}

static VOID
Ndis6FreeFilterDriverBlock(
    _In_ PNDIS6_FILTER_DRIVER_BLOCK Block)
{
    Block->Signature = 0;
    if (Block->FriendlyNameBuffer != NULL)
        ExFreePoolWithTag(Block->FriendlyNameBuffer, NDIS6_FILTER_NAME_TAG);
    if (Block->UniqueNameBuffer != NULL)
        ExFreePoolWithTag(Block->UniqueNameBuffer, NDIS6_FILTER_NAME_TAG);
    if (Block->ServiceNameBuffer != NULL)
        ExFreePoolWithTag(Block->ServiceNameBuffer, NDIS6_FILTER_NAME_TAG);
    ExFreePoolWithTag(Block, NDIS6_FILTER_DRIVER_TAG);
}

static BOOLEAN
Ndis6ReferenceFilterDriver(
    _In_ PNDIS6_FILTER_DRIVER_BLOCK Block)
{
    if (Block == NULL || Block->Signature != NDIS6_FILTER_DRIVER_SIGNATURE ||
        InterlockedCompareExchange(&Block->Closing, FALSE, FALSE) != FALSE)
        return FALSE;

    if (!ExAcquireRundownProtection(&Block->CallbackRundown))
        return FALSE;

    if (Block->Signature != NDIS6_FILTER_DRIVER_SIGNATURE ||
        InterlockedCompareExchange(&Block->Closing, FALSE, FALSE) != FALSE)
    {
        ExReleaseRundownProtection(&Block->CallbackRundown);
        return FALSE;
    }

    return TRUE;
}

static VOID
Ndis6DereferenceFilterDriver(
    _In_ PNDIS6_FILTER_DRIVER_BLOCK Block)
{
    ExReleaseRundownProtection(&Block->CallbackRundown);
}

NDIS_STATUS
EXPORT
NdisFRegisterFilterDriver(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ NDIS_HANDLE FilterDriverContext,
    _In_ PNDIS_FILTER_DRIVER_CHARACTERISTICS FilterDriverCharacteristics,
    _Out_ PNDIS_HANDLE NdisFilterDriverHandle)
{
    PNDIS6_FILTER_DRIVER_BLOCK Block;
    NDIS_STATUS Status;
    ULONG CopySize;
    KIRQL OldIrql;

    if (DriverObject == NULL || FilterDriverCharacteristics == NULL ||
        NdisFilterDriverHandle == NULL)
    {
        return NDIS_STATUS_INVALID_PARAMETER;
    }

    *NdisFilterDriverHandle = NULL;

    Status = Ndis6ValidateFilterCharacteristics(FilterDriverCharacteristics, &CopySize);
    if (Status != NDIS_STATUS_SUCCESS)
        return Status;

    Ndis6FilterDriverListInit();

    Block = (PNDIS6_FILTER_DRIVER_BLOCK)ExAllocatePoolWithTag(
        NonPagedPool, sizeof(NDIS6_FILTER_DRIVER_BLOCK),
        NDIS6_FILTER_DRIVER_TAG);
    if (Block == NULL)
        return NDIS_STATUS_RESOURCES;

    RtlZeroMemory(Block, sizeof(*Block));
    InitializeListHead(&Block->ListEntry);
    InitializeListHead(&Block->ModuleList);
    KeInitializeSpinLock(&Block->ModuleListLock);
    ExInitializeRundownProtection(&Block->CallbackRundown);
    Block->Signature           = NDIS6_FILTER_DRIVER_SIGNATURE;
    Block->DriverObject        = DriverObject;
    Block->FilterDriverContext = FilterDriverContext;
    RtlCopyMemory(&Block->Characteristics, FilterDriverCharacteristics, CopySize);

    Status = Ndis6CopyFilterName(&FilterDriverCharacteristics->FriendlyName, &Block->Characteristics.FriendlyName, &Block->FriendlyNameBuffer);
    if (Status == NDIS_STATUS_SUCCESS)
        Status = Ndis6CopyFilterName(&FilterDriverCharacteristics->UniqueName, &Block->Characteristics.UniqueName, &Block->UniqueNameBuffer);
    if (Status == NDIS_STATUS_SUCCESS)
        Status = Ndis6CopyFilterName(&FilterDriverCharacteristics->ServiceName, &Block->Characteristics.ServiceName, &Block->ServiceNameBuffer);
    if (Status != NDIS_STATUS_SUCCESS)
    {
        Ndis6FreeFilterDriverBlock(Block);
        return Status;
    }

    if (Block->Characteristics.SetOptionsHandler != NULL)
    {
        *NdisFilterDriverHandle = (NDIS_HANDLE)Block;
        Status = Block->Characteristics.SetOptionsHandler((NDIS_HANDLE)Block, FilterDriverContext);
        if (Status != NDIS_STATUS_SUCCESS)
        {
            *NdisFilterDriverHandle = NULL;
            Ndis6FreeFilterDriverBlock(Block);
            return Status;
        }
    }

    (VOID)KeWaitForSingleObject(&g_Ndis6FilterLifecycleMutex, Executive, KernelMode, FALSE, NULL);
    KeAcquireSpinLock(&g_Ndis6FilterDriverListLock, &OldIrql);
    InsertTailList(&g_Ndis6FilterDriverList, &Block->ListEntry);
    KeReleaseSpinLock(&g_Ndis6FilterDriverListLock, OldIrql);
    KeReleaseMutex(&g_Ndis6FilterLifecycleMutex, FALSE);

    Ndis6AttachFilterDriverToAllAdapters(Block);
    *NdisFilterDriverHandle = (NDIS_HANDLE)Block;
    return NDIS_STATUS_SUCCESS;
}

VOID
EXPORT
NdisFDeregisterFilterDriver(
    _In_ NDIS_HANDLE NdisFilterDriverHandle)
{
    PNDIS6_FILTER_DRIVER_BLOCK Block = (PNDIS6_FILTER_DRIVER_BLOCK)NdisFilterDriverHandle;
    KIRQL OldIrql;

    if (Block == NULL || Block->Signature != NDIS6_FILTER_DRIVER_SIGNATURE ||
        InterlockedCompareExchange(&Block->Closing, TRUE, FALSE) != FALSE)
        return;

    ASSERT(KeGetCurrentIrql() == PASSIVE_LEVEL);

    (VOID)KeWaitForSingleObject(&g_Ndis6FilterLifecycleMutex, Executive, KernelMode, FALSE, NULL);

    KeAcquireSpinLock(&g_Ndis6FilterDriverListLock, &OldIrql);
    if (Block->ListEntry.Flink != NULL &&
        Block->ListEntry.Blink != NULL &&
        Block->ListEntry.Flink != &Block->ListEntry)
    {
        RemoveEntryList(&Block->ListEntry);
        InitializeListHead(&Block->ListEntry);
    }
    KeReleaseSpinLock(&g_Ndis6FilterDriverListLock, OldIrql);

    ExWaitForRundownProtectionRelease(&Block->CallbackRundown);

    /* Windows does not release a filter-driver registration while modules
     * still point into its characteristics table. Quiesce each affected
     * stack, detach the module, and only then release the driver block. */
    for (;;)
    {
        PNDIS6_FILTER_MODULE Module = NULL;
        PLOGICAL_ADAPTER Adapter;
        PNDIS6_ADAPTER_EXT Ext;
        BOOLEAN WasRunning = FALSE;
        NDIS_STATUS Status;

        KeAcquireSpinLock(&Block->ModuleListLock, &OldIrql);
        if (!IsListEmpty(&Block->ModuleList))
        {
            Module = CONTAINING_RECORD(Block->ModuleList.Flink, NDIS6_FILTER_MODULE, DriverLink);
        }
        KeReleaseSpinLock(&Block->ModuleListLock, OldIrql);

        if (Module == NULL)
            break;

        Adapter = Module->Adapter;
        Ext = Adapter != NULL ? NDIS6_EXT(Adapter) : NULL;
        if (Ext != NULL && Ext->Initialized &&
            InterlockedCompareExchange(&Ext->ProtocolBindingsClosing, 0, 0) == 0 &&
            InterlockedCompareExchange((volatile LONG *)&Ext->PauseState, NDIS6_PAUSE_STATE_RUNNING, NDIS6_PAUSE_STATE_RUNNING) ==
                NDIS6_PAUSE_STATE_RUNNING)
        {
            WasRunning = TRUE;
            Status = Ndis6PauseDriverStack(Adapter);
        }
        else if (Adapter != NULL)
        {
            Status = Ndis6PauseFilterModules(Adapter);
        }
        else
        {
            Status = NDIS_STATUS_SUCCESS;
        }

        if (Status != NDIS_STATUS_SUCCESS)
        {
            DbgPrint("NDIS6: filter stack pause failed during deregistration 0x%08lx\n", (ULONG)Status);
        }

        Ndis6DetachFilterModule(Module);

        if (WasRunning && Ext != NULL && Ext->Initialized &&
            InterlockedCompareExchange(&Ext->ProtocolBindingsClosing, 0, 0) == 0)
        {
            Status = Ndis6RestartDriverStack(Adapter);
            if (Status != NDIS_STATUS_SUCCESS)
            {
                DbgPrint("NDIS6: filter stack restart failed during deregistration 0x%08lx\n", (ULONG)Status);
            }
        }
    }

    KeReleaseMutex(&g_Ndis6FilterLifecycleMutex, FALSE);

    Ndis6FreeFilterDriverBlock(Block);
}

/* ============================================================================
 *  Phase 7B filter attach/detach helpers
 *
 *  Ndis6AttachFiltersToAdapter — called from Ndis6CreateLogicalAdapter
 *  after MiniportInitializeEx populates GeneralAttrs. Walks the global
 *  filter driver list and calls each AttachHandler with a freshly built
 *  NDIS_FILTER_ATTACH_PARAMETERS, then stores the FilterModuleContext on
 *  the adapter's FilterModuleList.
 *
 *  Ndis6DetachFiltersFromAdapter — called from Ndis6DestroyLogicalAdapter
 *  on REMOVE. Walks the per-adapter filter module list and calls each
 *  filter's DetachHandler, then frees the modules.
 *
 * ============================================================================ */

static BOOLEAN
Ndis6FilterModuleExists(
    _In_ PNDIS6_FILTER_DRIVER_BLOCK Block,
    _In_ PLOGICAL_ADAPTER Adapter)
{
    PLIST_ENTRY Entry;
    KIRQL OldIrql;
    BOOLEAN Found = FALSE;

    KeAcquireSpinLock(&Block->ModuleListLock, &OldIrql);
    for (Entry = Block->ModuleList.Flink;
         Entry != &Block->ModuleList;
         Entry = Entry->Flink)
    {
        PNDIS6_FILTER_MODULE Module =
            CONTAINING_RECORD(Entry, NDIS6_FILTER_MODULE, DriverLink);
        if (Module->Adapter == Adapter &&
            InterlockedCompareExchange(&Module->Closing, FALSE, FALSE) == FALSE)
        {
            Found = TRUE;
            break;
        }
    }
    KeReleaseSpinLock(&Block->ModuleListLock, OldIrql);
    return Found;
}

static VOID
Ndis6BuildFilterAttachParameters(
    _In_ PLOGICAL_ADAPTER Adapter,
    _In_ PNDIS6_FILTER_DRIVER_BLOCK Block,
    _Out_ PNDIS_FILTER_ATTACH_PARAMETERS Params)
{
    PNDIS6_ADAPTER_EXT Ext = NDIS6_EXT(Adapter);

    RtlZeroMemory(Params, sizeof(*Params));
    Params->Header.Type = NDIS_OBJECT_TYPE_FILTER_ATTACH_PARAMETERS;
    if (Block->Characteristics.MinorNdisVersion >= 30)
    {
        Params->Header.Revision = NDIS_FILTER_ATTACH_PARAMETERS_REVISION_4;
        Params->Header.Size = NDIS_SIZEOF_FILTER_ATTACH_PARAMETERS_REVISION_4;
    }
    else if (Block->Characteristics.MinorNdisVersion >= 20)
    {
        Params->Header.Revision = NDIS_FILTER_ATTACH_PARAMETERS_REVISION_3;
        Params->Header.Size = NDIS_SIZEOF_FILTER_ATTACH_PARAMETERS_REVISION_3;
    }
    else if (Block->Characteristics.MinorNdisVersion >= 1)
    {
        Params->Header.Revision = NDIS_FILTER_ATTACH_PARAMETERS_REVISION_2;
        Params->Header.Size = NDIS_SIZEOF_FILTER_ATTACH_PARAMETERS_REVISION_2;
    }
    else
    {
        Params->Header.Revision = NDIS_FILTER_ATTACH_PARAMETERS_REVISION_1;
        Params->Header.Size = NDIS_SIZEOF_FILTER_ATTACH_PARAMETERS_REVISION_1;
    }

    Params->FilterModuleGuidName = &Block->Characteristics.UniqueName;
    Params->IfIndex = Ext->IfIndex;
    Params->NetLuid = Ext->NetLuid;
    Params->BaseMiniportIfIndex = Ext->IfIndex;
    Params->BaseMiniportInstanceName = &Adapter->NdisMiniportBlock.MiniportName;
    Params->BaseMiniportName = &Adapter->NdisMiniportBlock.MiniportName;
    Params->BaseMiniportNetLuid = Ext->NetLuid;
    Params->LowerIfIndex = Ext->IfIndex;
    Params->LowerIfNetLuid = Ext->NetLuid;
    Params->MiniportPhysicalDeviceObject = Ext->PhysicalDeviceObject;
    if (Ext->GeneralAttrsValid)
    {
        Params->MediaConnectState = Ext->GeneralAttrs.MediaConnectState;
        Params->MediaDuplexState =
            (NET_IF_MEDIA_DUPLEX_STATE)Ext->GeneralAttrs.MediaDuplexState;
        Params->XmitLinkSpeed = Ext->GeneralAttrs.XmitLinkSpeed;
        Params->RcvLinkSpeed = Ext->GeneralAttrs.RcvLinkSpeed;
        Params->MiniportMediaType = Ext->GeneralAttrs.MediaType;
        Params->MiniportPhysicalMediaType =
            Ext->GeneralAttrs.PhysicalMediumType;
        Params->DefaultOffloadConfiguration = Ext->OffloadDefaultPtr;
        Params->MacAddressLength = Ext->GeneralAttrs.MacAddressLength;
        if (Params->MacAddressLength <= sizeof(Params->CurrentMacAddress))
        {
            RtlCopyMemory(Params->CurrentMacAddress, Ext->GeneralAttrs.CurrentMacAddress, Params->MacAddressLength);
        }
        Params->BaseMiniportIfConnectorPresent =
            Ext->GeneralAttrs.IfConnectorPresent;
    }
}

/* g_Ndis6FilterLifecycleMutex serializes the driver's registration block
 * with deregistration while the callback builds and publishes this module. */
static NDIS_STATUS
Ndis6AttachFilterModuleLocked(
    _In_ PLOGICAL_ADAPTER Adapter,
    _In_ PNDIS6_FILTER_DRIVER_BLOCK Block)
{
    PNDIS6_ADAPTER_EXT Ext = NDIS6_EXT(Adapter);
    NDIS_FILTER_ATTACH_PARAMETERS Params;
    PNDIS6_FILTER_MODULE Module;
    NDIS_STATUS Status;
    BOOLEAN AttachSucceeded = FALSE;
    KIRQL OldIrql;

    if (Ext == NULL || Block == NULL)
        return NDIS_STATUS_INVALID_PARAMETER;

    if (Block->Characteristics.AttachHandler == NULL ||
        Ndis6FilterModuleExists(Block, Adapter))
    {
        return NDIS_STATUS_SUCCESS;
    }

    /* A closing block is being removed from the global list. Treat it as
     * absent instead of turning an unrelated mandatory attach into failure. */
    if (!Ndis6ReferenceFilterDriver(Block))
        return NDIS_STATUS_SUCCESS;

    Module = (PNDIS6_FILTER_MODULE)ExAllocatePoolWithTag(NonPagedPool, sizeof(NDIS6_FILTER_MODULE), NDIS6_FILTER_MODULE_TAG);
    if (Module == NULL)
    {
        Ndis6DereferenceFilterDriver(Block);
        return NDIS_STATUS_RESOURCES;
    }

    RtlZeroMemory(Module, sizeof(*Module));
    InitializeListHead(&Module->ListEntry);
    InitializeListHead(&Module->DriverLink);
    Module->DriverBlock = Block;
    Module->Adapter = Adapter;
    Module->Signature = NDIS6_FILTER_MODULE_SIGNATURE;
    Module->State = NDIS6_FILTER_STATE_PAUSED;
    ExInitializeRundownProtection(&Module->RundownRef);
    KeInitializeEvent(&Module->PauseEvent, NotificationEvent, FALSE);
    KeInitializeEvent(&Module->RestartEvent, NotificationEvent, FALSE);

    Ndis6BuildFilterAttachParameters(Adapter, Block, &Params);
    Status = Block->Characteristics.AttachHandler((NDIS_HANDLE)Module, Block->FilterDriverContext, &Params);

    if (Status == NDIS_STATUS_SUCCESS)
    {
        AttachSucceeded = TRUE;
        if (!Module->SetAttributesCalled)
        {
            Status = NDIS_STATUS_BAD_CHARACTERISTICS;
        }
        else if (Block->Characteristics.SetFilterModuleOptionsHandler != NULL)
        {
            Status = Block->Characteristics.SetFilterModuleOptionsHandler(Module->FilterModuleContext);
        }

        if (Status == NDIS_STATUS_SUCCESS &&
            InterlockedCompareExchange(&Ext->ProtocolBindingsClosing, 0, 0) != 0)
        {
            Status = NDIS_STATUS_CLOSING;
        }

        if (Status == NDIS_STATUS_SUCCESS)
        {
            KeAcquireSpinLock(&Ext->FilterModuleListLock, &OldIrql);
            InsertTailList(&Ext->FilterModuleList, &Module->ListEntry);
            KeReleaseSpinLock(&Ext->FilterModuleListLock, OldIrql);

            KeAcquireSpinLock(&Block->ModuleListLock, &OldIrql);
            InsertTailList(&Block->ModuleList, &Module->DriverLink);
            KeReleaseSpinLock(&Block->ModuleListLock, OldIrql);
        }
    }

    if (Status != NDIS_STATUS_SUCCESS)
    {
        if (AttachSucceeded && Block->Characteristics.DetachHandler != NULL)
            Block->Characteristics.DetachHandler(Module->FilterModuleContext);
        Module->Signature = 0;
        ExFreePoolWithTag(Module, NDIS6_FILTER_MODULE_TAG);
    }

    Ndis6DereferenceFilterDriver(Block);
    return Status;
}

NDIS_STATUS
Ndis6AttachFiltersToAdapter(
    _In_ PLOGICAL_ADAPTER Adapter)
{
    PNDIS6_ADAPTER_EXT Ext;
    PLIST_ENTRY        Entry;
    NDIS_STATUS        FailureStatus = NDIS_STATUS_SUCCESS;

    if (Adapter == NULL || !Adapter->IsNdis6)
        return NDIS_STATUS_INVALID_PARAMETER;

    if (InterlockedCompareExchange(&g_Ndis6FilterDriverListState, 2, 2) != 2)
        return NDIS_STATUS_SUCCESS;

    Ext = NDIS6_EXT(Adapter);
    if (Ext == NULL)
        return NDIS_STATUS_INVALID_PARAMETER;

    ASSERT(KeGetCurrentIrql() == PASSIVE_LEVEL);
    (VOID)KeWaitForSingleObject(&g_Ndis6FilterLifecycleMutex, Executive, KernelMode, FALSE, NULL);

    if (!Adapter->IsNdis6 || NDIS6_EXT(Adapter) != Ext ||
        InterlockedCompareExchange(&Ext->ProtocolBindingsClosing, 0, 0) != 0)
    {
        KeReleaseMutex(&g_Ndis6FilterLifecycleMutex, FALSE);
        return NDIS_STATUS_CLOSING;
    }

    /* The lifecycle mutex pins every driver block while callbacks run, so
     * this walk has no fixed snapshot limit and cannot observe a freed block. */
    for (Entry = g_Ndis6FilterDriverList.Flink;
         Entry != &g_Ndis6FilterDriverList;
         Entry = Entry->Flink)
    {
        PNDIS6_FILTER_DRIVER_BLOCK Block =
            CONTAINING_RECORD(Entry, NDIS6_FILTER_DRIVER_BLOCK, ListEntry);
        NDIS_STATUS Status = Ndis6AttachFilterModuleLocked(Adapter, Block);

        if (Status != NDIS_STATUS_SUCCESS &&
            (Block->Characteristics.Flags & NDIS_FILTER_DRIVER_MANDATORY))
        {
            FailureStatus = Status;
            break;
        }
    }

    KeReleaseMutex(&g_Ndis6FilterLifecycleMutex, FALSE);

    if (FailureStatus != NDIS_STATUS_SUCCESS)
        Ndis6DetachFiltersFromAdapter(Adapter);
    return FailureStatus;
}

/* Registering a filter can modify stacks that are already running. Windows
 * pauses each such stack, inserts the new module in the Paused state, and
 * restarts bottom-up. Adapter rundown keeps the snapshot valid after the
 * global adapter spin lock is dropped for driver callbacks. */
static VOID
Ndis6AttachFilterDriverToAllAdapters(
    _In_ PNDIS6_FILTER_DRIVER_BLOCK Block)
{
    extern LIST_ENTRY AdapterListHead;
    extern KSPIN_LOCK AdapterListLock;
    PLOGICAL_ADAPTER *Snapshot = NULL;
    SIZE_T SnapshotCapacity = 0;
    SIZE_T SnapCount = 0;
    SIZE_T Index;
    PLIST_ENTRY Entry;
    KIRQL OldIrql;

    KeAcquireSpinLock(&AdapterListLock, &OldIrql);
    for (Entry = AdapterListHead.Flink;
         Entry != &AdapterListHead;
         Entry = Entry->Flink)
    {
        PLOGICAL_ADAPTER Adapter =
            CONTAINING_RECORD(Entry, LOGICAL_ADAPTER, ListEntry);
        if (Adapter->IsNdis6 && NDIS6_EXT(Adapter) != NULL)
            SnapshotCapacity++;
    }
    KeReleaseSpinLock(&AdapterListLock, OldIrql);

    if (SnapshotCapacity == 0)
        return;

    if (SnapshotCapacity > MAXULONG_PTR / sizeof(*Snapshot))
    {
        DbgPrint("NDIS6: adapter snapshot size overflow during filter registration\n");
        return;
    }

    Snapshot = ExAllocatePoolWithTag(NonPagedPool, SnapshotCapacity * sizeof(*Snapshot), NDIS6_FILTER_SNAPSHOT_TAG);
    if (Snapshot == NULL)
    {
        DbgPrint("NDIS6: unable to allocate adapter snapshot during filter registration\n");
        return;
    }

    KeAcquireSpinLock(&AdapterListLock, &OldIrql);
    for (Entry = AdapterListHead.Flink;
         Entry != &AdapterListHead && SnapCount < SnapshotCapacity;
         Entry = Entry->Flink)
    {
        PLOGICAL_ADAPTER Adapter =
            CONTAINING_RECORD(Entry, LOGICAL_ADAPTER, ListEntry);
        if (Adapter->IsNdis6 && NDIS6_EXT(Adapter) != NULL &&
            Ndis6ReferenceAdapterLifecycle(Adapter))
        {
            Snapshot[SnapCount++] = Adapter;
        }
    }
    KeReleaseSpinLock(&AdapterListLock, OldIrql);

    for (Index = 0; Index < SnapCount; Index++)
    {
        PLOGICAL_ADAPTER Adapter = Snapshot[Index];
        PNDIS6_ADAPTER_EXT Ext = NDIS6_EXT(Adapter);
        NDIS_STATUS Status = NDIS_STATUS_CLOSING;
        NDIS_STATUS RestartStatus;
        BOOLEAN WasRunning = FALSE;

        (VOID)KeWaitForSingleObject(&g_Ndis6FilterLifecycleMutex, Executive, KernelMode, FALSE, NULL);

        if (Adapter->IsNdis6 && Ext != NULL && Ext->Initialized &&
            Block->Signature == NDIS6_FILTER_DRIVER_SIGNATURE &&
            InterlockedCompareExchange(&Block->Closing, 0, 0) == 0 &&
            InterlockedCompareExchange(&Ext->ProtocolBindingsClosing, 0, 0) == 0)
        {
            (VOID)KeWaitForSingleObject(&Ext->StackTransitionMutex, Executive, KernelMode, FALSE, NULL);

            if (Adapter->IsNdis6 && NDIS6_EXT(Adapter) == Ext &&
                Ext->Initialized &&
                InterlockedCompareExchange(&Ext->ProtocolBindingsClosing, 0, 0) == 0)
            {
                WasRunning = InterlockedCompareExchange((volatile LONG *)&Ext->PauseState, NDIS6_PAUSE_STATE_RUNNING, NDIS6_PAUSE_STATE_RUNNING) ==
                             NDIS6_PAUSE_STATE_RUNNING;

                if (WasRunning)
                    Status = Ndis6PauseDriverStackLocked(Adapter);
                else if (InterlockedCompareExchange((volatile LONG *)&Ext->PauseState, NDIS6_PAUSE_STATE_PAUSED, NDIS6_PAUSE_STATE_PAUSED) ==
                         NDIS6_PAUSE_STATE_PAUSED)
                    Status = NDIS_STATUS_SUCCESS;
                else
                    Status = NDIS_STATUS_INVALID_STATE;

                if (Status == NDIS_STATUS_SUCCESS &&
                    InterlockedCompareExchange(&Ext->ProtocolBindingsClosing, 0, 0) == 0)
                {
                    Status = Ndis6AttachFilterModuleLocked(Adapter, Block);
                }

                if (WasRunning &&
                    InterlockedCompareExchange(&Ext->ProtocolBindingsClosing, 0, 0) == 0)
                {
                    RestartStatus = Ndis6RestartDriverStackLocked(Adapter);
                    if (Status == NDIS_STATUS_SUCCESS)
                        Status = RestartStatus;
                }
            }

            KeReleaseMutex(&Ext->StackTransitionMutex, FALSE);
        }

        KeReleaseMutex(&g_Ndis6FilterLifecycleMutex, FALSE);

        if (Status != NDIS_STATUS_SUCCESS && Status != NDIS_STATUS_CLOSING)
        {
            DbgPrint("NDIS6: late filter attach failed 0x%08lx\n", (ULONG)Status);
        }

        Ndis6DereferenceAdapterLifecycle(Adapter);
    }

    ExFreePoolWithTag(Snapshot, NDIS6_FILTER_SNAPSHOT_TAG);
}

static VOID
Ndis6DetachFilterModule(
    _In_ PNDIS6_FILTER_MODULE Module)
{
    PNDIS6_FILTER_DRIVER_BLOCK Block;
    PLOGICAL_ADAPTER Adapter;
    PNDIS6_ADAPTER_EXT Ext;
    KIRQL OldIrql;

    if (Module == NULL || Module->Signature != NDIS6_FILTER_MODULE_SIGNATURE ||
        InterlockedCompareExchange(&Module->Closing, TRUE, FALSE) != FALSE)
    {
        return;
    }

    /* Mark closing before draining. Chain selection obtains rundown while
     * holding the adapter-list lock, so no new callback can enter after this
     * point. Keep the list links intact until existing callbacks have left. */
    ExWaitForRundownProtectionRelease(&Module->RundownRef);

    Adapter = Module->Adapter;
    Block = Module->DriverBlock;
    Ext = Adapter != NULL ? NDIS6_EXT(Adapter) : NULL;
    if (Ext != NULL)
    {
        KeAcquireSpinLock(&Ext->FilterModuleListLock, &OldIrql);
        if (Module->ListEntry.Flink != NULL &&
            Module->ListEntry.Blink != NULL &&
            Module->ListEntry.Flink != &Module->ListEntry)
        {
            RemoveEntryList(&Module->ListEntry);
            InitializeListHead(&Module->ListEntry);
        }
        KeReleaseSpinLock(&Ext->FilterModuleListLock, OldIrql);
    }

    if (Block != NULL)
    {
        KeAcquireSpinLock(&Block->ModuleListLock, &OldIrql);
        if (Module->DriverLink.Flink != NULL &&
            Module->DriverLink.Blink != NULL &&
            Module->DriverLink.Flink != &Module->DriverLink)
        {
            RemoveEntryList(&Module->DriverLink);
            InitializeListHead(&Module->DriverLink);
        }
        KeReleaseSpinLock(&Block->ModuleListLock, OldIrql);

        if (Block->Characteristics.DetachHandler != NULL)
            Block->Characteristics.DetachHandler(Module->FilterModuleContext);
    }

    Module->Adapter = NULL;
    Module->DriverBlock = NULL;
    Module->Signature = 0;
    ExFreePoolWithTag(Module, NDIS6_FILTER_MODULE_TAG);
}

VOID
Ndis6DetachFiltersFromAdapter(
    _In_ PLOGICAL_ADAPTER Adapter)
{
    PNDIS6_ADAPTER_EXT Ext;
    KIRQL              OldIrql;

    if (Adapter == NULL || !Adapter->IsNdis6)
        return;

    Ext = NDIS6_EXT(Adapter);
    if (Ext == NULL)
        return;

    ASSERT(KeGetCurrentIrql() == PASSIVE_LEVEL);
    if (InterlockedCompareExchange(&g_Ndis6FilterDriverListState, 2, 2) != 2)
        return;

    (VOID)KeWaitForSingleObject(&g_Ndis6FilterLifecycleMutex, Executive, KernelMode, FALSE, NULL);
    (VOID)Ndis6PauseFilterModules(Adapter);

    for (;;)
    {
        PNDIS6_FILTER_MODULE Module = NULL;

        KeAcquireSpinLock(&Ext->FilterModuleListLock, &OldIrql);
        if (!IsListEmpty(&Ext->FilterModuleList))
        {
            Module = CONTAINING_RECORD(Ext->FilterModuleList.Flink, NDIS6_FILTER_MODULE, ListEntry);
        }
        KeReleaseSpinLock(&Ext->FilterModuleListLock, OldIrql);

        if (Module == NULL)
            break;
        Ndis6DetachFilterModule(Module);
    }

    KeReleaseMutex(&g_Ndis6FilterLifecycleMutex, FALSE);
}

/* EOF */
