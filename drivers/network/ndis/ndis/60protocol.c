/*
 * PROJECT:     LiberNT NDIS library
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     NDIS 6 protocol driver registration and per-adapter binding lifecycle
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "ndis6_internal.h"

#ifndef EXPORT
#define EXPORT NTAPI
#endif

/* ------------------------------------------------------------------ */
/*  Protocol driver registration and per-adapter binding lifecycle.   */
/* ------------------------------------------------------------------ */

LIST_ENTRY  g_Ndis6ProtocolDriverList;
KSPIN_LOCK  g_Ndis6ProtocolDriverListLock;
static KMUTEX g_Ndis6ProtocolLifecycleMutex;
static volatile LONG g_Ndis6ProtocolDriverListState;

#define NDIS6_PROTOCOL_DRIVER_TAG  'pDNn'  /* "nNDp" */
#define NDIS6_PROTOCOL_SNAPSHOT_TAG 'sPNn'

static VOID
Ndis6ProtocolDriverListInit(VOID)
{
    if (InterlockedCompareExchange(&g_Ndis6ProtocolDriverListState, 1, 0) == 0)
    {
        InitializeListHead(&g_Ndis6ProtocolDriverList);
        KeInitializeSpinLock(&g_Ndis6ProtocolDriverListLock);
        KeInitializeMutex(&g_Ndis6ProtocolLifecycleMutex, 0);
        InterlockedExchange(&g_Ndis6ProtocolDriverListState, 2);
        return;
    }

    while (InterlockedCompareExchange(&g_Ndis6ProtocolDriverListState, 2, 2) != 2)
        YieldProcessor();
}

static BOOLEAN
Ndis6ReferenceProtocolDriver(
    _In_ PNDIS6_PROTOCOL_DRIVER_BLOCK Block)
{
    if (Block == NULL || Block->Signature != NDIS6_PROTOCOL_DRIVER_SIGNATURE ||
        InterlockedCompareExchange(&Block->Closing, 0, 0) != 0)
        return FALSE;

    if (!ExAcquireRundownProtection(&Block->CallbackRundown))
        return FALSE;

    if (Block->Signature != NDIS6_PROTOCOL_DRIVER_SIGNATURE ||
        InterlockedCompareExchange(&Block->Closing, 0, 0) != 0)
    {
        ExReleaseRundownProtection(&Block->CallbackRundown);
        return FALSE;
    }

    return TRUE;
}

static VOID
Ndis6DereferenceProtocolDriver(
    _In_ PNDIS6_PROTOCOL_DRIVER_BLOCK Block)
{
    ExReleaseRundownProtection(&Block->CallbackRundown);
}

static BOOLEAN
Ndis6IsSupportedProtocolMinorVersion(
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

static NDIS_STATUS
Ndis6ValidateProtocolCharacteristics(
    _In_ PNDIS_PROTOCOL_DRIVER_CHARACTERISTICS Characteristics,
    _Out_ PULONG CopySize)
{
    ULONG AllowedFlags = 0;
    ULONG RequiredSize;
    UCHAR RequiredRevision;

    if (Characteristics->Header.Type != NDIS_OBJECT_TYPE_PROTOCOL_DRIVER_CHARACTERISTICS &&
        Characteristics->Header.Type != NDIS_OBJECT_TYPE_DEFAULT)
    {
        return NDIS_STATUS_BAD_CHARACTERISTICS;
    }

    if (Characteristics->MajorNdisVersion != 6 ||
        !Ndis6IsSupportedProtocolMinorVersion(Characteristics->MinorNdisVersion))
    {
        return NDIS_STATUS_BAD_VERSION;
    }

    RequiredRevision = (Characteristics->MinorNdisVersion >= 1)
        ? NDIS_PROTOCOL_DRIVER_CHARACTERISTICS_REVISION_2
        : NDIS_PROTOCOL_DRIVER_CHARACTERISTICS_REVISION_1;
    RequiredSize = RequiredRevision == NDIS_PROTOCOL_DRIVER_CHARACTERISTICS_REVISION_2
        ? NDIS_SIZEOF_PROTOCOL_DRIVER_CHARACTERISTICS_REVISION_2
        : NDIS_SIZEOF_PROTOCOL_DRIVER_CHARACTERISTICS_REVISION_1;
    if (Characteristics->Header.Revision != RequiredRevision)
        return NDIS_STATUS_BAD_VERSION;

    if (Characteristics->Header.Size < RequiredSize ||
        RequiredSize > sizeof(*Characteristics) ||
        Characteristics->BindAdapterHandlerEx == NULL ||
        Characteristics->UnbindAdapterHandlerEx == NULL)
    {
        return NDIS_STATUS_BAD_CHARACTERISTICS;
    }

    if (Characteristics->MinorNdisVersion >= 50)
    {
        AllowedFlags |= NDIS_PROTOCOL_DRIVER_SUPPORTS_CURRENT_MAC_ADDRESS_CHANGE |
                        NDIS_PROTOCOL_DRIVER_SUPPORTS_L2_MTU_SIZE_CHANGE;
    }
    if (Characteristics->MinorNdisVersion >= 89)
        AllowedFlags |= NDIS_PROTOCOL_DRIVER_UDP_RSC_NOT_SUPPORTED;
    if ((Characteristics->Flags & ~AllowedFlags) != 0)
        return NDIS_STATUS_BAD_CHARACTERISTICS;

    if ((Characteristics->Name.Length != 0 && Characteristics->Name.Buffer == NULL) ||
        Characteristics->Name.Length > Characteristics->Name.MaximumLength ||
        Characteristics->Name.Length > MAXUSHORT - sizeof(WCHAR) ||
        (Characteristics->Name.Length & (sizeof(WCHAR) - 1)) != 0)
    {
        return NDIS_STATUS_BAD_CHARACTERISTICS;
    }

    *CopySize = RequiredSize;
    return NDIS_STATUS_SUCCESS;
}

/* ============================================================================
 *  Build an NDIS_BIND_PARAMETERS from an adapter's cached general attrs.
 *  Used by the protocol bind fan-out below.
 * ============================================================================ */

static VOID
Ndis6BuildBindParameters(
    _In_  PLOGICAL_ADAPTER       Adapter,
    _In_  PNDIS6_PROTOCOL_DRIVER_BLOCK Block,
    _Out_ PNDIS_BIND_PARAMETERS  Params)
{
    PNDIS6_ADAPTER_EXT Ext = NDIS6_EXT(Adapter);

    RtlZeroMemory(Params, sizeof(*Params));
    Params->Header.Type = NDIS_OBJECT_TYPE_BIND_PARAMETERS;
    if (Block->Characteristics.MinorNdisVersion >= 30)
    {
        Params->Header.Revision = NDIS_BIND_PARAMETERS_REVISION_4;
        Params->Header.Size = NDIS_SIZEOF_BIND_PARAMETERS_REVISION_4;
    }
    else if (Block->Characteristics.MinorNdisVersion >= 20)
    {
        Params->Header.Revision = NDIS_BIND_PARAMETERS_REVISION_3;
        Params->Header.Size = NDIS_SIZEOF_BIND_PARAMETERS_REVISION_3;
    }
    else if (Block->Characteristics.MinorNdisVersion >= 1)
    {
        Params->Header.Revision = NDIS_BIND_PARAMETERS_REVISION_2;
        Params->Header.Size = NDIS_SIZEOF_BIND_PARAMETERS_REVISION_2;
    }
    else
    {
        Params->Header.Revision = NDIS_BIND_PARAMETERS_REVISION_1;
        Params->Header.Size = NDIS_SIZEOF_BIND_PARAMETERS_REVISION_1;
    }
    Params->AdapterName     = &Adapter->NdisMiniportBlock.MiniportName;
    Params->BoundAdapterName = &Adapter->NdisMiniportBlock.MiniportName;

    if (Ext != NULL)
    {
        Params->PhysicalDeviceObject = Ext->PhysicalDeviceObject;
        Params->BoundIfNetluid = Ext->NetLuid;
        Params->BoundIfIndex = Ext->IfIndex;
        Params->LowestIfNetluid = Ext->NetLuid;
        Params->LowestIfIndex = Ext->IfIndex;
        if (Ext->GeneralAttrsValid)
        {
            Params->MediaType            = Ext->GeneralAttrs.MediaType;
            Params->PhysicalMediumType   = Ext->GeneralAttrs.PhysicalMediumType;
            Params->MtuSize              = Ext->GeneralAttrs.MtuSize;
            Params->MaxXmitLinkSpeed     = Ext->GeneralAttrs.MaxXmitLinkSpeed;
            Params->XmitLinkSpeed        = Ext->GeneralAttrs.XmitLinkSpeed;
            Params->MaxRcvLinkSpeed      = Ext->GeneralAttrs.MaxRcvLinkSpeed;
            Params->RcvLinkSpeed         = Ext->GeneralAttrs.RcvLinkSpeed;
            Params->MediaConnectState    = Ext->GeneralAttrs.MediaConnectState;
            Params->MediaDuplexState     = Ext->GeneralAttrs.MediaDuplexState;
            Params->LookaheadSize        = Ext->GeneralAttrs.LookaheadSize;
            Params->PowerManagementCapabilities =
                Ext->GeneralAttrs.PowerManagementCapabilities;
            Params->MacOptions           = Ext->GeneralAttrs.MacOptions;
            Params->SupportedPacketFilters = Ext->GeneralAttrs.SupportedPacketFilters;
            Params->MaxMulticastListSize = Ext->GeneralAttrs.MaxMulticastListSize;
            Params->MacAddressLength     = Ext->GeneralAttrs.MacAddressLength;
            if (Ext->GeneralAttrs.MacAddressLength <= sizeof(Params->CurrentMacAddress))
            {
                RtlCopyMemory(Params->CurrentMacAddress, Ext->GeneralAttrs.CurrentMacAddress, Ext->GeneralAttrs.MacAddressLength);
            }
            Params->RcvScaleCapabilities =
                (PNDIS_RECEIVE_SCALE_CAPABILITIES)Ext->GeneralAttrs.RecvScaleCapabilities;
            Params->AccessType = Ext->GeneralAttrs.AccessType;
            Params->DirectionType = Ext->GeneralAttrs.DirectionType;
            Params->ConnectionType = Ext->GeneralAttrs.ConnectionType;
            Params->IfType = Ext->GeneralAttrs.IfType;
            Params->IfConnectorPresent = Ext->GeneralAttrs.IfConnectorPresent;
            Params->DataBackFillSize = Ext->GeneralAttrs.DataBackFillSize;
            Params->ContextBackFillSize = Ext->GeneralAttrs.ContextBackFillSize;
            Params->DefaultOffloadConfiguration = Ext->OffloadDefaultPtr;
            Params->PowerManagementCapabilitiesEx =
                Ext->GeneralAttrs.PowerManagementCapabilitiesEx;
        }
    }
}

typedef struct _NDIS6_BIND_OPERATION
{
    ULONG Signature;
    PLOGICAL_ADAPTER Adapter;
    KEVENT CompleteEvent;
    volatile LONG Completed;
    NDIS_STATUS CompletionStatus;
} NDIS6_BIND_OPERATION, *PNDIS6_BIND_OPERATION;

#define NDIS6_BIND_OPERATION_SIGNATURE 'bBNn'

VOID
EXPORT
NdisCompleteBindAdapterEx(
    _In_ NDIS_HANDLE BindAdapterContext,
    _In_ NDIS_STATUS Status)
{
    PNDIS6_BIND_OPERATION Operation = BindAdapterContext;

    if (Operation == NULL ||
        Operation->Signature != NDIS6_BIND_OPERATION_SIGNATURE)
    {
        return;
    }

    /* Claim completion before publishing the status. A second completion is
     * a protocol-driver contract violation and must not overwrite the first. */
    if (InterlockedCompareExchange(&Operation->Completed, -1, 0) == 0)
    {
        Operation->CompletionStatus = Status;
        InterlockedExchange(&Operation->Completed, 1);
        KeSetEvent(&Operation->CompleteEvent, IO_NO_INCREMENT, FALSE);
    }
}

PLOGICAL_ADAPTER
Ndis6AdapterFromBindingHandle(
    NDIS_HANDLE NdisBindingHandle)
{
    PNDIS6_PROTOCOL_BINDING Binding = NdisBindingHandle;

    if (Binding == NULL || Binding->Signature != NDIS6_PROTOCOL_BINDING_SIGNATURE)
        return NULL;

    return Binding->Adapter;
}

static const WCHAR Ndis6ServicesKey[] = L"\\Registry\\Machine\\System\\CurrentControlSet\\Services\\";
static const WCHAR Ndis6ParametersKey[] = L"\\Parameters";

NDIS_STATUS
Ndis6BuildProtocolSection(
    _In_ PNDIS6_PROTOCOL_DRIVER_BLOCK Block,
    _In_ PLOGICAL_ADAPTER Adapter,
    _Out_ PUNICODE_STRING ProtocolSection)
{
    static const UNICODE_STRING DevicePrefix = RTL_CONSTANT_STRING(L"\\Device\\");
    UNICODE_STRING AdapterKey;

    AdapterKey = Adapter->NdisMiniportBlock.MiniportName;
    if (RtlPrefixUnicodeString(&DevicePrefix, &AdapterKey, TRUE))
    {
        AdapterKey.Buffer += DevicePrefix.Length / sizeof(WCHAR);
        AdapterKey.Length -= DevicePrefix.Length;
        AdapterKey.MaximumLength = AdapterKey.Length;
    }

    ProtocolSection->Length = 0;
    ProtocolSection->MaximumLength = (USHORT)(sizeof(Ndis6ServicesKey) + AdapterKey.Length +
                                              sizeof(Ndis6ParametersKey) + sizeof(WCHAR) +
                                              Block->Characteristics.Name.Length);
    ProtocolSection->Buffer = ExAllocatePoolWithTag(PagedPool, ProtocolSection->MaximumLength, NDIS6_TAG);
    if (ProtocolSection->Buffer == NULL)
    {
        return NDIS_STATUS_RESOURCES;
    }

    RtlZeroMemory(ProtocolSection->Buffer, ProtocolSection->MaximumLength);
    RtlAppendUnicodeToString(ProtocolSection, Ndis6ServicesKey);
    RtlAppendUnicodeStringToString(ProtocolSection, &AdapterKey);
    RtlAppendUnicodeToString(ProtocolSection, Ndis6ParametersKey);
    RtlAppendUnicodeToString(ProtocolSection, L"\\");
    RtlAppendUnicodeStringToString(ProtocolSection, &Block->Characteristics.Name);
    return NDIS_STATUS_SUCCESS;
}

static NDIS_STATUS
Ndis6InvokeProtocolBind(
    _In_ PNDIS6_PROTOCOL_DRIVER_BLOCK Block,
    _In_ PLOGICAL_ADAPTER Adapter)
{
    NDIS6_BIND_OPERATION Operation;
    NDIS_BIND_PARAMETERS Params;
    NDIS_PNP_CAPABILITIES PnpCapabilities;
    UNICODE_STRING ProtocolSection;
    NDIS_STATUS Status;
    NTSTATUS WaitStatus;
    PLIST_ENTRY Entry;
    BOOLEAN Bound = FALSE;
    KIRQL OldIrql;

    if (Block == NULL || Adapter == NULL ||
        Block->Characteristics.BindAdapterHandlerEx == NULL)
    {
        return NDIS_STATUS_INVALID_PARAMETER;
    }

    KeAcquireSpinLock(&Block->BindingListLock, &OldIrql);
    for (Entry = Block->BindingList.Flink; Entry != &Block->BindingList; Entry = Entry->Flink)
    {
        if (CONTAINING_RECORD(Entry, NDIS6_PROTOCOL_BINDING, ListEntry)->Adapter == Adapter)
        {
            Bound = TRUE;
            break;
        }
    }
    KeReleaseSpinLock(&Block->BindingListLock, OldIrql);
    if (Bound)
    {
        return NDIS_STATUS_SUCCESS;
    }

    Status = Ndis6BuildProtocolSection(Block, Adapter, &ProtocolSection);
    if (Status != NDIS_STATUS_SUCCESS)
    {
        return Status;
    }

    RtlZeroMemory(&Operation, sizeof(Operation));
    Operation.Signature = NDIS6_BIND_OPERATION_SIGNATURE;
    Operation.Adapter = Adapter;
    Operation.CompletionStatus = NDIS_STATUS_PENDING;
    KeInitializeEvent(&Operation.CompleteEvent, NotificationEvent, FALSE);

    Ndis6BuildBindParameters(Adapter, Block, &Params);
    Params.ProtocolSection = &ProtocolSection;
    if (Params.PowerManagementCapabilities == NULL)
    {
        RtlZeroMemory(&PnpCapabilities, sizeof(PnpCapabilities));
        Params.PowerManagementCapabilities = &PnpCapabilities;
    }
    Status = Block->Characteristics.BindAdapterHandlerEx(Block->ProtocolDriverContext, (NDIS_HANDLE)&Operation, &Params);

    if (Status == NDIS_STATUS_PENDING)
    {
        WaitStatus = KeWaitForSingleObject(&Operation.CompleteEvent, Executive, KernelMode, FALSE, NULL);
        Status = NT_SUCCESS(WaitStatus)
            ? Operation.CompletionStatus
            : (NDIS_STATUS)WaitStatus;
    }

    Operation.Signature = 0;
    ExFreePoolWithTag(ProtocolSection.Buffer, NDIS6_TAG);
    return Status;
}

/* ============================================================================
 *  Ndis6BindProtocolToAllAdaptersLocked — when a protocol registers, walk the
 *  global LOGICAL_ADAPTER list and call its BindAdapterHandlerEx for every
 *  NDIS 6 adapter.
 * ============================================================================ */

static VOID
Ndis6BindProtocolToAllAdaptersLocked(
    _In_ PNDIS6_PROTOCOL_DRIVER_BLOCK Block)
{
    extern LIST_ENTRY AdapterListHead;
    extern KSPIN_LOCK AdapterListLock;
    PLIST_ENTRY entry;
    KIRQL OldIrql;
    PLOGICAL_ADAPTER *Snapshot = NULL;
    SIZE_T SnapshotCapacity = 0;
    SIZE_T SnapCount = 0;
    SIZE_T i;

    if (Block->Characteristics.BindAdapterHandlerEx == NULL)
        return;

    if (!Ndis6ReferenceProtocolDriver(Block))
        return;

    /* Count every NDIS 6 adapter in one list generation. Adapter teardown
     * waits on the protocol lifecycle mutex before it can free the object,
     * and adapters inserted after this count bind the protocol from their own
     * start path. */
    KeAcquireSpinLock(&AdapterListLock, &OldIrql);
    for (entry = AdapterListHead.Flink;
         entry != &AdapterListHead;
         entry = entry->Flink)
    {
        PLOGICAL_ADAPTER Adapter =
            CONTAINING_RECORD(entry, LOGICAL_ADAPTER, ListEntry);
        if (Adapter->IsNdis6 && NDIS6_EXT(Adapter) != NULL)
            SnapshotCapacity++;
    }
    KeReleaseSpinLock(&AdapterListLock, OldIrql);

    if (SnapshotCapacity == 0)
        goto Exit;

    if (SnapshotCapacity > MAXULONG_PTR / sizeof(*Snapshot))
    {
        DbgPrint("NDIS6: adapter snapshot size overflow during protocol bind\n");
        goto Exit;
    }

    Snapshot = ExAllocatePoolWithTag(NonPagedPool, SnapshotCapacity * sizeof(*Snapshot), NDIS6_PROTOCOL_SNAPSHOT_TAG);
    if (Snapshot == NULL)
    {
        DbgPrint("NDIS6: unable to allocate adapter snapshot during protocol bind\n");
        goto Exit;
    }

    KeAcquireSpinLock(&AdapterListLock, &OldIrql);
    for (entry = AdapterListHead.Flink;
         entry != &AdapterListHead && SnapCount < SnapshotCapacity;
         entry = entry->Flink)
    {
        PLOGICAL_ADAPTER Adapter =
            CONTAINING_RECORD(entry, LOGICAL_ADAPTER, ListEntry);
        if (Adapter->IsNdis6 && NDIS6_EXT(Adapter) != NULL &&
            Ndis6ReferenceAdapterLifecycle(Adapter))
        {
            Snapshot[SnapCount++] = Adapter;
        }
    }
    KeReleaseSpinLock(&AdapterListLock, OldIrql);

    for (i = 0; i < SnapCount; i++)
    {
        PNDIS6_ADAPTER_EXT Ext = NDIS6_EXT(Snapshot[i]);
        if (Snapshot[i]->IsNdis6 && Ext != NULL &&
            InterlockedCompareExchange(&Ext->ProtocolBindingsClosing, 0, 0) == 0)
        {
            (VOID)Ndis6InvokeProtocolBind(Block, Snapshot[i]);
            if (InterlockedCompareExchange((volatile LONG *)&Ext->PauseState, NDIS6_PAUSE_STATE_RUNNING, NDIS6_PAUSE_STATE_RUNNING) ==
                NDIS6_PAUSE_STATE_RUNNING)
            {
                Ndis6RestartProtocolBindings(Snapshot[i]);
            }
        }
        Ndis6DereferenceAdapterLifecycle(Snapshot[i]);
    }

    ExFreePoolWithTag(Snapshot, NDIS6_PROTOCOL_SNAPSHOT_TAG);

Exit:
    Ndis6DereferenceProtocolDriver(Block);
}

/* ============================================================================
 *  Ndis6BindAllProtocolsToAdapter — when a new adapter is created, walk
 *  the registered protocol list and call each one's BindAdapterHandlerEx.
 *  Called from Ndis6CreateLogicalAdapter (60adapter.c) at the end of adapter
 *  setup, after GeneralAttrs are populated.
 * ============================================================================ */

VOID
Ndis6BindAllProtocolsToAdapter(
    _In_ PLOGICAL_ADAPTER Adapter)
{
    PLIST_ENTRY entry;
    KIRQL OldIrql;
    PNDIS6_PROTOCOL_DRIVER_BLOCK *Snapshot = NULL;
    SIZE_T SnapshotCapacity = 0;
    SIZE_T SnapCount = 0;
    SIZE_T i;

    if (InterlockedCompareExchange(&g_Ndis6ProtocolDriverListState, 2, 2) != 2 ||
        Adapter == NULL || !Adapter->IsNdis6)
        return;

    (VOID)KeWaitForSingleObject(&g_Ndis6ProtocolLifecycleMutex, Executive, KernelMode, FALSE, NULL);

    if (!Adapter->IsNdis6 || NDIS6_EXT(Adapter) == NULL ||
        InterlockedCompareExchange(&NDIS6_EXT(Adapter)->ProtocolBindingsClosing, 0, 0) != 0)
    {
        KeReleaseMutex(&g_Ndis6ProtocolLifecycleMutex, FALSE);
        return;
    }

    KeAcquireSpinLock(&g_Ndis6ProtocolDriverListLock, &OldIrql);
    for (entry = g_Ndis6ProtocolDriverList.Flink;
         entry != &g_Ndis6ProtocolDriverList;
         entry = entry->Flink)
    {
        SnapshotCapacity++;
    }
    KeReleaseSpinLock(&g_Ndis6ProtocolDriverListLock, OldIrql);

    if (SnapshotCapacity == 0)
        goto Exit;

    if (SnapshotCapacity > MAXULONG_PTR / sizeof(*Snapshot))
    {
        DbgPrint("NDIS6: protocol snapshot size overflow during adapter bind\n");
        goto Exit;
    }

    Snapshot = ExAllocatePoolWithTag(NonPagedPool, SnapshotCapacity * sizeof(*Snapshot), NDIS6_PROTOCOL_SNAPSHOT_TAG);
    if (Snapshot == NULL)
    {
        DbgPrint("NDIS6: unable to allocate protocol snapshot during adapter bind\n");
        goto Exit;
    }

    KeAcquireSpinLock(&g_Ndis6ProtocolDriverListLock, &OldIrql);
    for (entry = g_Ndis6ProtocolDriverList.Flink;
         entry != &g_Ndis6ProtocolDriverList && SnapCount < SnapshotCapacity;
         entry = entry->Flink)
    {
        PNDIS6_PROTOCOL_DRIVER_BLOCK Block =
            CONTAINING_RECORD(entry, NDIS6_PROTOCOL_DRIVER_BLOCK, ListEntry);
        if (Block->Characteristics.BindAdapterHandlerEx != NULL &&
            Ndis6ReferenceProtocolDriver(Block))
        {
            Snapshot[SnapCount] = Block;
        }
        else
        {
            Snapshot[SnapCount] = NULL;
        }
        SnapCount++;
    }
    KeReleaseSpinLock(&g_Ndis6ProtocolDriverListLock, OldIrql);

    for (i = 0; i < SnapCount; i++)
    {
        if (Snapshot[i] == NULL)
            continue;
        (VOID)Ndis6InvokeProtocolBind(Snapshot[i], Adapter);
        Ndis6DereferenceProtocolDriver(Snapshot[i]);
    }

    ExFreePoolWithTag(Snapshot, NDIS6_PROTOCOL_SNAPSHOT_TAG);

Exit:
    KeReleaseMutex(&g_Ndis6ProtocolLifecycleMutex, FALSE);
}

static VOID
NTAPI
Ndis6ProtocolBindWorker(
    _In_ PVOID Context)
{
    PNDIS6_PROTOCOL_DRIVER_BLOCK Block = Context;
    KIRQL OldIrql;

    (VOID)KeWaitForSingleObject(&g_Ndis6ProtocolLifecycleMutex, Executive, KernelMode, FALSE, NULL);
    if (InterlockedCompareExchange(&Block->Closing, 0, 0) == 0)
    {
        KeAcquireSpinLock(&g_Ndis6ProtocolDriverListLock, &OldIrql);
        InsertTailList(&g_Ndis6ProtocolDriverList, &Block->ListEntry);
        KeReleaseSpinLock(&g_Ndis6ProtocolDriverListLock, OldIrql);

        Ndis6BindProtocolToAllAdaptersLocked(Block);
    }
    KeReleaseMutex(&g_Ndis6ProtocolLifecycleMutex, FALSE);

    KeSetEvent(&Block->BindWorkDone, IO_NO_INCREMENT, FALSE);
}

NDIS_STATUS
EXPORT
NdisRegisterProtocolDriver(
    _In_ NDIS_HANDLE ProtocolDriverContext,
    _In_ PNDIS_PROTOCOL_DRIVER_CHARACTERISTICS ProtocolDriverCharacteristics,
    _Out_ PNDIS_HANDLE NdisProtocolHandle)
{
    PNDIS6_PROTOCOL_DRIVER_BLOCK Block;
    NDIS_STATUS Status;
    ULONG CopySize;
    ULONG NameBytes;

    if (ProtocolDriverCharacteristics == NULL || NdisProtocolHandle == NULL)
        return NDIS_STATUS_INVALID_PARAMETER;

    *NdisProtocolHandle = NULL;

    Status = Ndis6ValidateProtocolCharacteristics(ProtocolDriverCharacteristics, &CopySize);
    if (Status != NDIS_STATUS_SUCCESS)
        return Status;

    Ndis6ProtocolDriverListInit();

    Block = (PNDIS6_PROTOCOL_DRIVER_BLOCK)ExAllocatePoolWithTag(NonPagedPool, sizeof(NDIS6_PROTOCOL_DRIVER_BLOCK), NDIS6_PROTOCOL_DRIVER_TAG);
    if (Block == NULL)
        return NDIS_STATUS_RESOURCES;

    RtlZeroMemory(Block, sizeof(*Block));
    Block->Signature = NDIS6_PROTOCOL_DRIVER_SIGNATURE;
    InitializeListHead(&Block->BindingList);
    KeInitializeSpinLock(&Block->BindingListLock);
    ExInitializeRundownProtection(&Block->CallbackRundown);
    Block->ProtocolDriverContext = ProtocolDriverContext;
    RtlCopyMemory(&Block->Characteristics, ProtocolDriverCharacteristics, CopySize);

    NameBytes = ProtocolDriverCharacteristics->Name.Length;
    if (NameBytes != 0)
    {
        Block->NameBuffer = ExAllocatePoolWithTag(PagedPool, NameBytes + sizeof(WCHAR), NDIS6_PROTOCOL_DRIVER_TAG);
        if (Block->NameBuffer == NULL)
        {
            ExFreePoolWithTag(Block, NDIS6_PROTOCOL_DRIVER_TAG);
            return NDIS_STATUS_RESOURCES;
        }

        RtlCopyMemory(Block->NameBuffer, ProtocolDriverCharacteristics->Name.Buffer, NameBytes);
        Block->NameBuffer[NameBytes / sizeof(WCHAR)] = UNICODE_NULL;
        Block->Characteristics.Name.Buffer = Block->NameBuffer;
        Block->Characteristics.Name.Length = (USHORT)NameBytes;
        Block->Characteristics.Name.MaximumLength =
            (USHORT)(NameBytes + sizeof(WCHAR));
    }

    if (Block->Characteristics.SetOptionsHandler != NULL)
    {
        *NdisProtocolHandle = (NDIS_HANDLE)Block;
        Status = Block->Characteristics.SetOptionsHandler((NDIS_HANDLE)Block, ProtocolDriverContext);
        if (Status != NDIS_STATUS_SUCCESS)
        {
            *NdisProtocolHandle = NULL;
            Block->Signature = 0;
            if (Block->NameBuffer != NULL)
                ExFreePoolWithTag(Block->NameBuffer, NDIS6_PROTOCOL_DRIVER_TAG);
            ExFreePoolWithTag(Block, NDIS6_PROTOCOL_DRIVER_TAG);
            return Status;
        }
    }

    *NdisProtocolHandle = (NDIS_HANDLE)Block;

    KeInitializeEvent(&Block->BindWorkDone, NotificationEvent, FALSE);
    ExInitializeWorkItem(&Block->BindWorkItem, Ndis6ProtocolBindWorker, Block);
    ExQueueWorkItem(&Block->BindWorkItem, DelayedWorkQueue);

    return NDIS_STATUS_SUCCESS;
}

typedef struct _NDIS6_UNBIND_OPERATION
{
    ULONG Signature;
    KEVENT CompleteEvent;
    volatile LONG Completed;
} NDIS6_UNBIND_OPERATION, *PNDIS6_UNBIND_OPERATION;

#define NDIS6_UNBIND_OPERATION_SIGNATURE 'bUNn'

VOID
EXPORT
NdisCompleteUnbindAdapterEx(
    _In_ NDIS_HANDLE UnbindContext)
{
    PNDIS6_UNBIND_OPERATION Operation = UnbindContext;

    if (Operation == NULL ||
        Operation->Signature != NDIS6_UNBIND_OPERATION_SIGNATURE)
    {
        return;
    }

    if (InterlockedCompareExchange(&Operation->Completed, 1, 0) == 0)
        KeSetEvent(&Operation->CompleteEvent, IO_NO_INCREMENT, FALSE);
}

typedef struct _NDIS6_NET_PNP_OPERATION
{
    ULONG Signature;
    PNDIS6_PROTOCOL_BINDING Binding;
    NET_PNP_EVENT_NOTIFICATION Notification;
    KEVENT CompleteEvent;
    volatile LONG Completed;
    NDIS_STATUS CompletionStatus;
} NDIS6_NET_PNP_OPERATION, *PNDIS6_NET_PNP_OPERATION;

#define NDIS6_NET_PNP_OPERATION_SIGNATURE 'pPNn'

VOID
EXPORT
NdisCompleteNetPnPEvent(
    _In_ NDIS_HANDLE NdisBindingHandle,
    _In_ PNET_PNP_EVENT_NOTIFICATION NetPnPEventNotification,
    _In_ NDIS_STATUS Status)
{
    PNDIS6_NET_PNP_OPERATION Operation;

    if (NdisBindingHandle == NULL || NetPnPEventNotification == NULL)
        return;

    Operation = CONTAINING_RECORD(NetPnPEventNotification, NDIS6_NET_PNP_OPERATION, Notification);
    if (Operation->Signature != NDIS6_NET_PNP_OPERATION_SIGNATURE ||
        Operation->Binding != (PNDIS6_PROTOCOL_BINDING)NdisBindingHandle)
    {
        return;
    }

    if (InterlockedCompareExchange(&Operation->Completed, -1, 0) == 0)
    {
        Operation->CompletionStatus = Status;
        InterlockedExchange(&Operation->Completed, 1);
        KeSetEvent(&Operation->CompleteEvent, IO_NO_INCREMENT, FALSE);
    }
}

static NDIS_STATUS
Ndis6CallProtocolNetPnPEvent(
    _In_ PNDIS6_PROTOCOL_BINDING Binding,
    _In_ PNET_PNP_EVENT_NOTIFICATION NetPnPEventNotification)
{
    PNDIS6_PROTOCOL_DRIVER_BLOCK Block = Binding->DriverBlock;
    NDIS6_NET_PNP_OPERATION Operation;
    NDIS_STATUS Status;
    NTSTATUS WaitStatus;

    if (Block == NULL || Block->Characteristics.NetPnPEventHandler == NULL)
        return NDIS_STATUS_SUCCESS;

    RtlZeroMemory(&Operation, sizeof(Operation));
    Operation.Signature = NDIS6_NET_PNP_OPERATION_SIGNATURE;
    Operation.Binding = Binding;
    Operation.CompletionStatus = NDIS_STATUS_PENDING;
    KeInitializeEvent(&Operation.CompleteEvent, NotificationEvent, FALSE);

    Operation.Notification.Header.Type = NDIS_OBJECT_TYPE_DEFAULT;
    if (Block->Characteristics.MinorNdisVersion >= 50)
    {
        Operation.Notification.Header.Revision =
            NET_PNP_EVENT_NOTIFICATION_REVISION_2;
        Operation.Notification.Header.Size =
            NDIS_SIZEOF_NET_PNP_EVENT_NOTIFICATION_REVISION_2;
    }
    else
    {
        Operation.Notification.Header.Revision =
            NET_PNP_EVENT_NOTIFICATION_REVISION_1;
        Operation.Notification.Header.Size =
            NDIS_SIZEOF_NET_PNP_EVENT_NOTIFICATION_REVISION_1;
    }
    Operation.Notification.PortNumber = NetPnPEventNotification->PortNumber;
    Operation.Notification.NetPnPEvent = NetPnPEventNotification->NetPnPEvent;
    Operation.Notification.Flags = NetPnPEventNotification->Flags;
    if (Operation.Notification.Header.Revision >= NET_PNP_EVENT_NOTIFICATION_REVISION_2 &&
        NetPnPEventNotification->Header.Revision >= NET_PNP_EVENT_NOTIFICATION_REVISION_2 &&
        NetPnPEventNotification->Header.Size >= NDIS_SIZEOF_NET_PNP_EVENT_NOTIFICATION_REVISION_2)
    {
        Operation.Notification.SwitchId = NetPnPEventNotification->SwitchId;
        Operation.Notification.VPortId = NetPnPEventNotification->VPortId;
    }

    Status = Block->Characteristics.NetPnPEventHandler(Binding->ProtocolBindingContext, &Operation.Notification);
    if (Status == NDIS_STATUS_PENDING)
    {
        WaitStatus = KeWaitForSingleObject(&Operation.CompleteEvent, Executive, KernelMode, FALSE, NULL);
        Status = NT_SUCCESS(WaitStatus)
            ? Operation.CompletionStatus
            : (NDIS_STATUS)WaitStatus;
    }

    Operation.Signature = 0;
    return Status;
}

static NDIS_STATUS
Ndis6CallProtocolNetPnPEventCode(
    _In_ PNDIS6_PROTOCOL_BINDING Binding,
    _In_ NET_PNP_EVENT_CODE EventCode,
    _In_opt_ PVOID EventBuffer,
    _In_ ULONG EventBufferLength)
{
    NET_PNP_EVENT_NOTIFICATION Notification;

    RtlZeroMemory(&Notification, sizeof(Notification));
    Notification.Header.Type = NDIS_OBJECT_TYPE_DEFAULT;
    Notification.Header.Revision = NET_PNP_EVENT_NOTIFICATION_REVISION_2;
    Notification.Header.Size =
        NDIS_SIZEOF_NET_PNP_EVENT_NOTIFICATION_REVISION_2;
    Notification.NetPnPEvent.NetEvent = EventCode;
    Notification.NetPnPEvent.Buffer = EventBuffer;
    Notification.NetPnPEvent.BufferLength = EventBufferLength;
    return Ndis6CallProtocolNetPnPEvent(Binding, &Notification);
}

static NDIS_STATUS
Ndis6SnapshotProtocolBindings(
    _In_ PNDIS6_ADAPTER_EXT Ext,
    _Outptr_result_buffer_(*SnapshotCount)
        PNDIS6_PROTOCOL_BINDING **SnapshotOut,
    _Out_ PSIZE_T SnapshotCount)
{
    PNDIS6_PROTOCOL_BINDING *Snapshot;
    SIZE_T Capacity = 0;
    SIZE_T Count = 0;
    PLIST_ENTRY Entry;
    KIRQL OldIrql;

    *SnapshotOut = NULL;
    *SnapshotCount = 0;

    KeAcquireSpinLock(&Ext->ProtocolBindingListLock, &OldIrql);
    for (Entry = Ext->ProtocolBindingList.Flink;
         Entry != &Ext->ProtocolBindingList;
         Entry = Entry->Flink)
    {
        Capacity++;
    }
    KeReleaseSpinLock(&Ext->ProtocolBindingListLock, OldIrql);

    if (Capacity == 0)
        return NDIS_STATUS_SUCCESS;

    if (Capacity > MAXULONG_PTR / sizeof(*Snapshot))
        return NDIS_STATUS_RESOURCES;

    Snapshot = ExAllocatePoolWithTag(NonPagedPool, Capacity * sizeof(*Snapshot), NDIS6_PROTOCOL_SNAPSHOT_TAG);
    if (Snapshot == NULL)
        return NDIS_STATUS_RESOURCES;

    KeAcquireSpinLock(&Ext->ProtocolBindingListLock, &OldIrql);
    for (Entry = Ext->ProtocolBindingList.Flink;
         Entry != &Ext->ProtocolBindingList && Count < Capacity;
         Entry = Entry->Flink)
    {
        PNDIS6_PROTOCOL_BINDING Binding =
            CONTAINING_RECORD(Entry, NDIS6_PROTOCOL_BINDING, AdapterLink);

        if (Ndis6ReferenceProtocolBinding(Binding))
            Snapshot[Count++] = Binding;
    }
    KeReleaseSpinLock(&Ext->ProtocolBindingListLock, OldIrql);

    if (Count == 0)
    {
        ExFreePoolWithTag(Snapshot, NDIS6_PROTOCOL_SNAPSHOT_TAG);
        return NDIS_STATUS_SUCCESS;
    }

    *SnapshotOut = Snapshot;
    *SnapshotCount = Count;
    return NDIS_STATUS_SUCCESS;
}

static VOID
Ndis6ReleaseProtocolBindingSnapshot(
    _In_reads_opt_(SnapshotCount) PNDIS6_PROTOCOL_BINDING *Snapshot,
    _In_ SIZE_T SnapshotCount)
{
    SIZE_T Index;

    for (Index = 0; Index < SnapshotCount; Index++)
        Ndis6DereferenceProtocolBinding(Snapshot[Index]);

    if (Snapshot != NULL)
        ExFreePoolWithTag(Snapshot, NDIS6_PROTOCOL_SNAPSHOT_TAG);
}

NDIS_STATUS
Ndis6FilterTerminalNetPnPEvent(
    _In_ PLOGICAL_ADAPTER Adapter,
    _In_ PNET_PNP_EVENT_NOTIFICATION NetPnPEventNotification)
{
    PNDIS6_ADAPTER_EXT Ext;
    PNDIS6_PROTOCOL_BINDING *Snapshot;
    SIZE_T SnapshotCount;
    SIZE_T Index;
    NDIS_STATUS OverallStatus = NDIS_STATUS_SUCCESS;
    NDIS_STATUS Status;

    if (Adapter == NULL || !Adapter->IsNdis6 ||
        NetPnPEventNotification == NULL)
        return NDIS_STATUS_INVALID_PARAMETER;

    Ext = NDIS6_EXT(Adapter);
    if (Ext == NULL)
        return NDIS_STATUS_INVALID_PARAMETER;

    if (InterlockedCompareExchange(&g_Ndis6ProtocolDriverListState, 2, 2) != 2)
        return NDIS_STATUS_SUCCESS;

    Status = Ndis6SnapshotProtocolBindings(Ext, &Snapshot, &SnapshotCount);
    if (Status != NDIS_STATUS_SUCCESS)
        return Status;

    /* Protocol callbacks may pend and may re-enter unrelated NDIS lifecycle
     * paths. Keep only per-binding rundown references while calling them;
     * global registration serialization must never cover driver callbacks. */
    for (Index = 0; Index < SnapshotCount; Index++)
    {
        Status = Ndis6CallProtocolNetPnPEvent(Snapshot[Index], NetPnPEventNotification);
        if (Status != NDIS_STATUS_SUCCESS)
            OverallStatus = Status;
    }

    Ndis6ReleaseProtocolBindingSnapshot(Snapshot, SnapshotCount);
    return OverallStatus;
}

static NDIS_STATUS
Ndis6NotifyProtocolBindingsEvent(
    _In_ PLOGICAL_ADAPTER Adapter,
    _In_ NET_PNP_EVENT_CODE EventCode,
    _In_opt_ PVOID EventBuffer,
    _In_ ULONG EventBufferLength)
{
    NET_PNP_EVENT_NOTIFICATION Notification;

    RtlZeroMemory(&Notification, sizeof(Notification));
    Notification.Header.Type = NDIS_OBJECT_TYPE_DEFAULT;
    Notification.Header.Revision = NET_PNP_EVENT_NOTIFICATION_REVISION_2;
    Notification.Header.Size =
        NDIS_SIZEOF_NET_PNP_EVENT_NOTIFICATION_REVISION_2;
    Notification.NetPnPEvent.NetEvent = EventCode;
    Notification.NetPnPEvent.Buffer = EventBuffer;
    Notification.NetPnPEvent.BufferLength = EventBufferLength;
    return Ndis6FilterDispatchNetPnPEvent(Adapter, &Notification);
}

NDIS_STATUS
Ndis6NotifyProtocolBindingsPower(
    _In_ PLOGICAL_ADAPTER Adapter,
    _In_ DEVICE_POWER_STATE DevicePowerState)
{
    NDIS_DEVICE_POWER_STATE NdisPowerState;

    if (DevicePowerState < PowerDeviceD0 ||
        DevicePowerState > PowerDeviceD3)
    {
        return NDIS_STATUS_INVALID_PARAMETER;
    }

    NdisPowerState = (NDIS_DEVICE_POWER_STATE)DevicePowerState;
    return Ndis6NotifyProtocolBindingsEvent(Adapter, NetEventSetPower, &NdisPowerState, sizeof(NdisPowerState));
}

static NDIS_STATUS
Ndis6PauseProtocolBinding(
    _In_ PNDIS6_PROTOCOL_BINDING Binding)
{
    NDIS_PROTOCOL_PAUSE_PARAMETERS PauseParameters;
    NDIS_STATUS Status;
    LONG State;

    State = InterlockedCompareExchange(&Binding->State, NDIS6_PROTOCOL_STATE_PAUSING, NDIS6_PROTOCOL_STATE_RUNNING);
    if (State == NDIS6_PROTOCOL_STATE_PAUSED)
        return NDIS_STATUS_SUCCESS;
    if (State != NDIS6_PROTOCOL_STATE_RUNNING)
        return NDIS_STATUS_INVALID_STATE;

    RtlZeroMemory(&PauseParameters, sizeof(PauseParameters));
    PauseParameters.Header.Type = NDIS_OBJECT_TYPE_DEFAULT;
    PauseParameters.Header.Revision =
        NDIS_PROTOCOL_PAUSE_PARAMETERS_REVISION_1;
    PauseParameters.Header.Size =
        NDIS_SIZEOF_PROTOCOL_PAUSE_PARAMETERS_REVISION_1;

    Status = Ndis6CallProtocolNetPnPEventCode(Binding, NetEventPause, &PauseParameters, sizeof(PauseParameters));
    InterlockedExchange(&Binding->State, Status == NDIS_STATUS_SUCCESS ? NDIS6_PROTOCOL_STATE_PAUSED : NDIS6_PROTOCOL_STATE_RUNNING);
    return Status;
}

NDIS_STATUS
Ndis6PauseProtocolBindings(
    _In_ PLOGICAL_ADAPTER Adapter)
{
    PNDIS6_ADAPTER_EXT Ext;
    PNDIS6_PROTOCOL_BINDING *Snapshot;
    SIZE_T SnapshotCount;
    SIZE_T Index;
    NDIS_STATUS OverallStatus = NDIS_STATUS_SUCCESS;
    NDIS_STATUS Status;

    if (Adapter == NULL || !Adapter->IsNdis6)
        return NDIS_STATUS_INVALID_PARAMETER;

    Ext = NDIS6_EXT(Adapter);
    if (Ext == NULL)
        return NDIS_STATUS_INVALID_PARAMETER;

    if (InterlockedCompareExchange(&g_Ndis6ProtocolDriverListState, 2, 2) != 2)
        return NDIS_STATUS_SUCCESS;

    Status = Ndis6SnapshotProtocolBindings(Ext, &Snapshot, &SnapshotCount);
    if (Status != NDIS_STATUS_SUCCESS)
        return Status;

    for (Index = 0; Index < SnapshotCount; Index++)
    {
        Status = Ndis6PauseProtocolBinding(Snapshot[Index]);
        if (Status != NDIS_STATUS_SUCCESS &&
            Status != NDIS_STATUS_INVALID_STATE)
        {
            OverallStatus = Status;
        }
    }

    Ndis6ReleaseProtocolBindingSnapshot(Snapshot, SnapshotCount);
    return OverallStatus;
}

VOID
Ndis6RestartProtocolBindings(
    _In_ PLOGICAL_ADAPTER Adapter)
{
    PNDIS6_ADAPTER_EXT Ext;
    PNDIS6_PROTOCOL_BINDING *Snapshot;
    SIZE_T SnapshotCount;
    SIZE_T Index;
    NDIS_STATUS SnapshotStatus;

    if (Adapter == NULL || !Adapter->IsNdis6)
        return;

    Ext = NDIS6_EXT(Adapter);
    if (Ext == NULL)
        return;

    if (InterlockedCompareExchange(&g_Ndis6ProtocolDriverListState, 2, 2) != 2)
        return;

    SnapshotStatus = Ndis6SnapshotProtocolBindings(Ext, &Snapshot, &SnapshotCount);
    if (SnapshotStatus != NDIS_STATUS_SUCCESS)
    {
        DbgPrint("NDIS6: unable to snapshot protocol bindings for restart\n");
        return;
    }

    for (Index = 0; Index < SnapshotCount; Index++)
    {
        PNDIS6_PROTOCOL_BINDING Binding = Snapshot[Index];

        if (InterlockedCompareExchange(&Binding->State, NDIS6_PROTOCOL_STATE_RESTARTING, NDIS6_PROTOCOL_STATE_PAUSED) ==
            NDIS6_PROTOCOL_STATE_PAUSED)
        {
            NDIS_PROTOCOL_RESTART_PARAMETERS RestartParameters;
            NDIS_STATUS Status;

            RtlZeroMemory(&RestartParameters, sizeof(RestartParameters));
            RestartParameters.Header.Type = NDIS_OBJECT_TYPE_DEFAULT;
            RestartParameters.Header.Revision =
                NDIS_PROTOCOL_RESTART_PARAMETERS_REVISION_1;
            RestartParameters.Header.Size =
                NDIS_SIZEOF_PROTOCOL_RESTART_PARAMETERS_REVISION_1;

            Status = Ndis6CallProtocolNetPnPEventCode(Binding, NetEventRestart, &RestartParameters, sizeof(RestartParameters));
            InterlockedExchange(&Binding->State, Status == NDIS_STATUS_SUCCESS ? NDIS6_PROTOCOL_STATE_RUNNING : NDIS6_PROTOCOL_STATE_PAUSED);
        }
    }

    Ndis6ReleaseProtocolBindingSnapshot(Snapshot, SnapshotCount);
}

static BOOLEAN
Ndis6ProtocolBindingStillOpen(
    _In_ PNDIS6_PROTOCOL_DRIVER_BLOCK Block,
    _In_ PNDIS6_PROTOCOL_BINDING Binding)
{
    PLIST_ENTRY Entry;
    KIRQL OldIrql;
    BOOLEAN Found = FALSE;

    KeAcquireSpinLock(&Block->BindingListLock, &OldIrql);
    for (Entry = Block->BindingList.Flink;
         Entry != &Block->BindingList;
         Entry = Entry->Flink)
    {
        if (CONTAINING_RECORD(Entry, NDIS6_PROTOCOL_BINDING, ListEntry) == Binding)
        {
            Found = TRUE;
            break;
        }
    }
    KeReleaseSpinLock(&Block->BindingListLock, OldIrql);
    return Found;
}

static VOID
Ndis6UnbindProtocolBinding(
    _In_ PNDIS6_PROTOCOL_BINDING Binding)
{
    PNDIS6_PROTOCOL_DRIVER_BLOCK Block;
    PROTOCOL_UNBIND_ADAPTER_EX_HANDLER UnbindHandler;
    NDIS_HANDLE ProtocolBindingContext;
    NDIS6_UNBIND_OPERATION Operation;
    NDIS_STATUS Status;

    Block = Binding->DriverBlock;
    if (Block == NULL)
        return;

    UnbindHandler = Block->Characteristics.UnbindAdapterHandlerEx;
    ProtocolBindingContext = Binding->ProtocolBindingContext;

    if (Ndis6ReferenceProtocolBinding(Binding))
    {
        (VOID)Ndis6PauseProtocolBinding(Binding);
        Ndis6DereferenceProtocolBinding(Binding);
    }

    RtlZeroMemory(&Operation, sizeof(Operation));
    Operation.Signature = NDIS6_UNBIND_OPERATION_SIGNATURE;
    KeInitializeEvent(&Operation.CompleteEvent, NotificationEvent, FALSE);

    Status = UnbindHandler((NDIS_HANDLE)&Operation, ProtocolBindingContext);
    if (Status == NDIS_STATUS_PENDING)
    {
        (VOID)KeWaitForSingleObject(&Operation.CompleteEvent, Executive, KernelMode, FALSE, NULL);
    }
    else if (Status != NDIS_STATUS_SUCCESS)
    {
        DbgPrint("NDIS6: ProtocolUnbindAdapterEx returned invalid status 0x%08lx\n", (ULONG)Status);
    }

    Operation.Signature = 0;

    /* ProtocolUnbindAdapterEx is required to close the binding and cannot
     * fail. Keep teardown memory-safe even when a defective protocol omits
     * NdisCloseAdapterEx. */
    if (Ndis6ProtocolBindingStillOpen(Block, Binding))
    {
        DbgPrint("NDIS6: protocol returned from unbind with an open binding; closing it\n");
        (VOID)NdisCloseAdapterEx((NDIS_HANDLE)Binding);
    }
}

VOID
Ndis6UnbindAllProtocolsFromAdapter(
    _In_ PLOGICAL_ADAPTER Adapter)
{
    PNDIS6_ADAPTER_EXT Ext;

    if (Adapter == NULL || !Adapter->IsNdis6)
        return;

    Ext = NDIS6_EXT(Adapter);
    if (Ext == NULL)
        return;

    InterlockedExchange(&Ext->ProtocolBindingsClosing, 1);

    if (InterlockedCompareExchange(&g_Ndis6ProtocolDriverListState, 2, 2) != 2)
        return;

    (VOID)KeWaitForSingleObject(&g_Ndis6ProtocolLifecycleMutex, Executive, KernelMode, FALSE, NULL);
    for (;;)
    {
        PNDIS6_PROTOCOL_BINDING Binding;
        KIRQL OldIrql;

        KeAcquireSpinLock(&Ext->ProtocolBindingListLock, &OldIrql);
        if (IsListEmpty(&Ext->ProtocolBindingList))
        {
            KeReleaseSpinLock(&Ext->ProtocolBindingListLock, OldIrql);
            break;
        }

        Binding = CONTAINING_RECORD(Ext->ProtocolBindingList.Flink, NDIS6_PROTOCOL_BINDING, AdapterLink);
        KeReleaseSpinLock(&Ext->ProtocolBindingListLock, OldIrql);
        Ndis6UnbindProtocolBinding(Binding);
    }
    KeReleaseMutex(&g_Ndis6ProtocolLifecycleMutex, FALSE);
}

VOID
EXPORT
NdisDeregisterProtocolDriver(
    _In_ NDIS_HANDLE NdisProtocolHandle)
{
    PNDIS6_PROTOCOL_DRIVER_BLOCK Block = (PNDIS6_PROTOCOL_DRIVER_BLOCK)NdisProtocolHandle;
    KIRQL OldIrql;

    if (Block == NULL || Block->Signature != NDIS6_PROTOCOL_DRIVER_SIGNATURE ||
        InterlockedCompareExchange(&Block->Closing, 1, 0) != 0)
    {
        return;
    }

    ASSERT(KeGetCurrentIrql() == PASSIVE_LEVEL);
    (VOID)KeWaitForSingleObject(&Block->BindWorkDone, Executive, KernelMode, FALSE, NULL);
    (VOID)KeWaitForSingleObject(&g_Ndis6ProtocolLifecycleMutex, Executive, KernelMode, FALSE, NULL);

    KeAcquireSpinLock(&g_Ndis6ProtocolDriverListLock, &OldIrql);
    if (Block->ListEntry.Flink != NULL && Block->ListEntry.Blink != NULL)
    {
        RemoveEntryList(&Block->ListEntry);
        Block->ListEntry.Flink = NULL;
        Block->ListEntry.Blink = NULL;
    }
    KeReleaseSpinLock(&g_Ndis6ProtocolDriverListLock, OldIrql);

    ExWaitForRundownProtectionRelease(&Block->CallbackRundown);

    for (;;)
    {
        PNDIS6_PROTOCOL_BINDING Binding;

        KeAcquireSpinLock(&Block->BindingListLock, &OldIrql);
        if (IsListEmpty(&Block->BindingList))
        {
            KeReleaseSpinLock(&Block->BindingListLock, OldIrql);
            break;
        }
        Binding = CONTAINING_RECORD(Block->BindingList.Flink, NDIS6_PROTOCOL_BINDING, ListEntry);
        KeReleaseSpinLock(&Block->BindingListLock, OldIrql);
        Ndis6UnbindProtocolBinding(Binding);
    }

    KeReleaseMutex(&g_Ndis6ProtocolLifecycleMutex, FALSE);

    Block->Signature = 0;
    if (Block->NameBuffer != NULL)
        ExFreePoolWithTag(Block->NameBuffer, NDIS6_PROTOCOL_DRIVER_TAG);
    ExFreePoolWithTag(Block, NDIS6_PROTOCOL_DRIVER_TAG);
}

/* ============================================================================
 *  NdisOpenAdapterEx / NdisCloseAdapterEx
 *
 *  Phase 9C: real per-binding context for native NDIS 6 protocols. The
 *  protocol calls NdisOpenAdapterEx from inside its BindAdapterHandlerEx
 *  callback to take a real binding. We allocate an NDIS6_PROTOCOL_BINDING
 *  with backptrs to the adapter and the protocol driver block, and hand
 *  it back as the binding handle. The protocol uses that handle for all
 *  subsequent operations on this binding (NdisOidRequest, etc.).
 *
 *  OpenParameters is an NDIS_OPEN_PARAMETERS struct containing the
 *  medium array, frame type array, and selected medium index pointer.
 *  We pick the first entry matching the miniport's registered medium.
 *
 *  Synchronous open only. PENDING completion would need a per-binding
 *  operation context that this bridge does not yet require.
 * ============================================================================ */

#define NDIS6_PROTOCOL_BINDING_TAG  'bPNn'  /* "nNPb" */

NDIS_STATUS
NTAPI
NdisOpenAdapterEx(
    _In_  NDIS_HANDLE  NdisProtocolHandle,
    _In_  NDIS_HANDLE  ProtocolBindingContext,
    _In_  PNDIS_OPEN_PARAMETERS OpenParameters,
    _In_  NDIS_HANDLE  BindContext,
    _Out_ PNDIS_HANDLE NdisBindingHandle)
{
    PNDIS6_PROTOCOL_DRIVER_BLOCK Block = (PNDIS6_PROTOCOL_DRIVER_BLOCK)NdisProtocolHandle;
    PNDIS6_PROTOCOL_BINDING      Binding;
    PNDIS6_BIND_OPERATION        BindOperation;
    PLOGICAL_ADAPTER             Adapter;
    PNDIS6_ADAPTER_EXT           Ext;
    PNDIS_OPEN_PARAMETERS        OpenParams = OpenParameters;

    if (Block == NULL || NdisBindingHandle == NULL || BindContext == NULL ||
        OpenParams == NULL)
        return NDIS_STATUS_INVALID_PARAMETER;

    *NdisBindingHandle = NULL;

    if (!Ndis6ReferenceProtocolDriver(Block))
        return NDIS_STATUS_CLOSING;

    if (OpenParams->Header.Type != NDIS_OBJECT_TYPE_OPEN_PARAMETERS ||
        OpenParams->Header.Revision != NDIS_OPEN_PARAMETERS_REVISION_1 ||
        OpenParams->Header.Size < NDIS_SIZEOF_OPEN_PARAMETERS_REVISION_1 ||
        OpenParams->Header.Size > sizeof(*OpenParams) ||
        OpenParams->MediumArray == NULL ||
        OpenParams->MediumArraySize == 0 ||
        OpenParams->SelectedMediumIndex == NULL)
    {
        Ndis6DereferenceProtocolDriver(Block);
        return NDIS_STATUS_INVALID_PARAMETER;
    }

    /* BindContext identifies one NDIS-owned bind operation. It is deliberately
     * not an adapter handle: protocols may retain it only until they complete
     * a pending bind through NdisCompleteBindAdapterEx. */
    BindOperation = (PNDIS6_BIND_OPERATION)BindContext;
    Adapter = (BindOperation->Signature == NDIS6_BIND_OPERATION_SIGNATURE)
        ? BindOperation->Adapter
        : NULL;
    if (Adapter == NULL || !Adapter->IsNdis6)
    {
        Ndis6DereferenceProtocolDriver(Block);
        return NDIS_STATUS_FAILURE;
    }

    Ext = NDIS6_EXT(Adapter);
    if (Ext == NULL)
    {
        Ndis6DereferenceProtocolDriver(Block);
        return NDIS_STATUS_FAILURE;
    }

    Binding = (PNDIS6_PROTOCOL_BINDING)ExAllocatePoolWithTag(
        NonPagedPool, sizeof(NDIS6_PROTOCOL_BINDING),
        NDIS6_PROTOCOL_BINDING_TAG);
    if (Binding == NULL)
    {
        Ndis6DereferenceProtocolDriver(Block);
        return NDIS_STATUS_RESOURCES;
    }

    RtlZeroMemory(Binding, sizeof(*Binding));
    Binding->Signature              = NDIS6_PROTOCOL_BINDING_SIGNATURE;
    Binding->DriverBlock            = Block;
    Binding->Adapter                = Adapter;
    Binding->ProtocolBindingContext = ProtocolBindingContext;
    /* D4: initialize per-binding pending-OID list. */
    InitializeListHead(&Binding->PendingOidRequests);
    KeInitializeSpinLock(&Binding->PendingOidRequestsLock);
    ExInitializeRundownProtection(&Binding->RundownRef);
    Binding->Closing = FALSE;
    Binding->State = NDIS6_PROTOCOL_STATE_PAUSED;

    /* D6: parse NDIS_OPEN_PARAMETERS and pick a medium. Walk the
     * MediumArray looking for NdisMedium802_3 (the only medium the
     * bridge currently bridges NDIS 5↔6). Report the matching index
     * via SelectedMediumIndex so the protocol knows which medium
     * won. If nothing matches, fail the open with NDIS_STATUS_UNSUPPORTED_MEDIA. */
    if (OpenParams != NULL)
    {
        NDIS_MEDIUM  AdapterMedium = NdisMedium802_3;
        UINT         i;
        BOOLEAN      Matched = FALSE;
        if (Ext != NULL && Ext->GeneralAttrsValid)
            AdapterMedium = Ext->GeneralAttrs.MediaType;

        if (OpenParams->MediumArray != NULL && OpenParams->MediumArraySize > 0)
        {
            for (i = 0; i < OpenParams->MediumArraySize; i++)
            {
                if (OpenParams->MediumArray[i] == AdapterMedium)
                {
                    if (OpenParams->SelectedMediumIndex != NULL)
                        *OpenParams->SelectedMediumIndex = i;
                    Matched = TRUE;
                    break;
                }
            }
            if (!Matched)
            {
                ExFreePoolWithTag(Binding, NDIS6_PROTOCOL_BINDING_TAG);
                Ndis6DereferenceProtocolDriver(Block);
                return NDIS_STATUS_UNSUPPORTED_MEDIA;
            }
        }
        /* FrameTypeArray: ignored — we deliver raw Ethernet II frames
         * to the protocol and let it decode. */
    }

    /* Link this binding into the adapter and protocol ownership lists before
     * returning the handle. Receive indications use one clone per binding, so
     * multiple native protocols can bind to the same adapter independently. */
    {
        KIRQL OldIrql;

        KIRQL DriverIrql;

        KeAcquireSpinLock(&Ext->ProtocolBindingListLock, &OldIrql);
        KeAcquireSpinLock(&Block->BindingListLock, &DriverIrql);
        if (InterlockedCompareExchange(&Block->Closing, 0, 0) != 0 ||
            InterlockedCompareExchange(&Ext->ProtocolBindingsClosing, 0, 0) != 0 ||
            !Adapter->IsNdis6 || NDIS6_EXT(Adapter) != Ext)
        {
            KeReleaseSpinLock(&Block->BindingListLock, DriverIrql);
            KeReleaseSpinLock(&Ext->ProtocolBindingListLock, OldIrql);
            ExFreePoolWithTag(Binding, NDIS6_PROTOCOL_BINDING_TAG);
            Ndis6DereferenceProtocolDriver(Block);
            return NDIS_STATUS_CLOSING;
        }
        InsertTailList(&Ext->ProtocolBindingList, &Binding->AdapterLink);
        InsertTailList(&Block->BindingList, &Binding->ListEntry);
        KeReleaseSpinLock(&Block->BindingListLock, DriverIrql);
        KeReleaseSpinLock(&Ext->ProtocolBindingListLock, OldIrql);
    }

    *NdisBindingHandle = (NDIS_HANDLE)Binding;

    /* The completion callback is reserved for an asynchronous open. This
     * implementation completes synchronously and therefore only returns the
     * status and binding handle. */
    Ndis6DereferenceProtocolDriver(Block);

    return NDIS_STATUS_SUCCESS;
}

NDIS_STATUS
NTAPI
NdisCloseAdapterEx(
    _In_ NDIS_HANDLE NdisBindingHandle)
{
    PNDIS6_PROTOCOL_BINDING Binding = (PNDIS6_PROTOCOL_BINDING)NdisBindingHandle;
    PNDIS6_PROTOCOL_DRIVER_BLOCK Block;

    if (Binding == NULL ||
        Binding->Signature != NDIS6_PROTOCOL_BINDING_SIGNATURE)
        return NDIS_STATUS_INVALID_PARAMETER;

    ASSERT(KeGetCurrentIrql() == PASSIVE_LEVEL);
    Block = Binding->DriverBlock;
    if (InterlockedCompareExchange(&Binding->Closing, TRUE, FALSE) != FALSE)
        return NDIS_STATUS_CLOSING;

    InterlockedExchange(&Binding->State, NDIS6_PROTOCOL_STATE_PAUSED);

    /* Unlink from both ownership lists before beginning rundown. Adapter and
     * driver list locks are always acquired in this order. */
    if (Binding->Adapter != NULL)
    {
        PNDIS6_ADAPTER_EXT Ext = NDIS6_EXT(Binding->Adapter);
        if (Ext != NULL)
        {
            KIRQL AdapterIrql;
            KIRQL DriverIrql = PASSIVE_LEVEL;

            KeAcquireSpinLock(&Ext->ProtocolBindingListLock, &AdapterIrql);
            if (Block != NULL)
                KeAcquireSpinLock(&Block->BindingListLock, &DriverIrql);
            if (Binding->AdapterLink.Flink != NULL &&
                Binding->AdapterLink.Blink != NULL)
            {
                RemoveEntryList(&Binding->AdapterLink);
                Binding->AdapterLink.Flink = NULL;
                Binding->AdapterLink.Blink = NULL;
            }
            if (Block != NULL &&
                Binding->ListEntry.Flink != NULL &&
                Binding->ListEntry.Blink != NULL)
            {
                RemoveEntryList(&Binding->ListEntry);
                Binding->ListEntry.Flink = NULL;
                Binding->ListEntry.Blink = NULL;
            }
            if (Block != NULL)
                KeReleaseSpinLock(&Block->BindingListLock, DriverIrql);
            KeReleaseSpinLock(&Ext->ProtocolBindingListLock, AdapterIrql);
        }
    }
    if (Block != NULL &&
        Binding->ListEntry.Flink != NULL &&
        Binding->ListEntry.Blink != NULL)
    {
        KIRQL DriverIrql;
        KeAcquireSpinLock(&Block->BindingListLock, &DriverIrql);
        if (Binding->ListEntry.Flink != NULL &&
            Binding->ListEntry.Blink != NULL)
        {
            RemoveEntryList(&Binding->ListEntry);
            Binding->ListEntry.Flink = NULL;
            Binding->ListEntry.Blink = NULL;
        }
        KeReleaseSpinLock(&Block->BindingListLock, DriverIrql);
    }

    ExWaitForRundownProtectionRelease(&Binding->RundownRef);

    if (!IsListEmpty(&Binding->PendingOidRequests))
    {
        DbgPrint("NDIS6: protocol closed a binding with pending OID requests\n");
    }

    /* The close completed synchronously; ProtocolCloseAdapterCompleteEx is
     * called only when NdisCloseAdapterEx returned NDIS_STATUS_PENDING. */
    Binding->Signature = 0;
    ExFreePoolWithTag(Binding, NDIS6_PROTOCOL_BINDING_TAG);
    return NDIS_STATUS_SUCCESS;
}

/* EOF */
