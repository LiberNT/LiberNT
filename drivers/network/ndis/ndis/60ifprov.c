/*
 * PROJECT:     LiberNT NDIS library
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     NDIS interface provider and NET_LUID index routines
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "ndis6_internal.h"

#define NDIS6_IF_PROVIDER_TAG   'pIdN'
#define NDIS6_IF_PROVIDER_SIGNATURE 'vPfI'

typedef struct _NDIS6_IF_PROVIDER
{
    LIST_ENTRY Link;
    ULONG Signature;
    NDIS_IF_PROVIDER_CHARACTERISTICS Characteristics;
    NDIS_HANDLE Context;
} NDIS6_IF_PROVIDER, *PNDIS6_IF_PROVIDER;

typedef struct _NDIS6_IF_ENTRY
{
    LIST_ENTRY Link;
    NET_IFINDEX IfIndex;
    NET_LUID NetLuid;
    PNDIS6_IF_PROVIDER Provider;
    NDIS_HANDLE ProviderIfContext;
} NDIS6_IF_ENTRY, *PNDIS6_IF_ENTRY;

typedef struct _NDIS6_LUID_INDEX
{
    LIST_ENTRY Link;
    NET_IFTYPE IfType;
    UINT32 NetLuidIndex;
} NDIS6_LUID_INDEX, *PNDIS6_LUID_INDEX;

static LIST_ENTRY Ndis6IfProviders = { &Ndis6IfProviders, &Ndis6IfProviders };
static LIST_ENTRY Ndis6IfEntries = { &Ndis6IfEntries, &Ndis6IfEntries };
static LIST_ENTRY Ndis6LuidIndices = { &Ndis6LuidIndices, &Ndis6LuidIndices };
static KSPIN_LOCK Ndis6IfLock;

static
PNDIS6_IF_ENTRY
Ndis6FindIfEntryLocked(
    _In_ NET_IFINDEX IfIndex)
{
    PLIST_ENTRY Link;

    for (Link = Ndis6IfEntries.Flink; Link != &Ndis6IfEntries; Link = Link->Flink)
    {
        PNDIS6_IF_ENTRY Entry = CONTAINING_RECORD(Link, NDIS6_IF_ENTRY, Link);

        if (Entry->Provider != NULL && Entry->IfIndex == IfIndex)
            return Entry;
    }

    return NULL;
}

BOOLEAN
Ndis6LookupProviderInterface(
    _In_ NET_IFINDEX IfIndex,
    _Out_opt_ PNET_LUID NetLuid)
{
    PNDIS6_IF_ENTRY Entry;
    KIRQL OldIrql;
    BOOLEAN Found = FALSE;

    KeAcquireSpinLock(&Ndis6IfLock, &OldIrql);
    Entry = Ndis6FindIfEntryLocked(IfIndex);
    if (Entry != NULL)
    {
        if (NetLuid != NULL)
            *NetLuid = Entry->NetLuid;
        Found = TRUE;
    }
    KeReleaseSpinLock(&Ndis6IfLock, OldIrql);

    return Found;
}

BOOLEAN
Ndis6LookupProviderNetLuid(
    _In_ NET_LUID NetLuid,
    _Out_opt_ PNET_IFINDEX IfIndex)
{
    PLIST_ENTRY Link;
    KIRQL OldIrql;
    BOOLEAN Found = FALSE;

    KeAcquireSpinLock(&Ndis6IfLock, &OldIrql);
    for (Link = Ndis6IfEntries.Flink; Link != &Ndis6IfEntries; Link = Link->Flink)
    {
        PNDIS6_IF_ENTRY Entry = CONTAINING_RECORD(Link, NDIS6_IF_ENTRY, Link);

        if (Entry->Provider != NULL && Entry->NetLuid.Value == NetLuid.Value)
        {
            if (IfIndex != NULL)
                *IfIndex = Entry->IfIndex;
            Found = TRUE;
            break;
        }
    }
    KeReleaseSpinLock(&Ndis6IfLock, OldIrql);

    return Found;
}

static
BOOLEAN
Ndis6LuidIndexAllocatedLocked(
    _In_ NET_IFTYPE IfType,
    _In_ UINT32 NetLuidIndex)
{
    PLIST_ENTRY Link;

    for (Link = Ndis6LuidIndices.Flink; Link != &Ndis6LuidIndices; Link = Link->Flink)
    {
        PNDIS6_LUID_INDEX Index = CONTAINING_RECORD(Link, NDIS6_LUID_INDEX, Link);

        if (Index->IfType == IfType && Index->NetLuidIndex == NetLuidIndex)
            return TRUE;
    }

    return FALSE;
}

BOOLEAN
Ndis6IsNetLuidIndexAllocated(
    _In_ NET_IFTYPE IfType,
    _In_ UINT32 NetLuidIndex)
{
    KIRQL OldIrql;
    BOOLEAN Allocated;

    KeAcquireSpinLock(&Ndis6IfLock, &OldIrql);
    Allocated = Ndis6LuidIndexAllocatedLocked(IfType, NetLuidIndex);
    KeReleaseSpinLock(&Ndis6IfLock, OldIrql);

    return Allocated;
}

static
BOOLEAN
Ndis6AdapterUsesNetLuidIndexAtDpcLevel(
    _In_ NET_IFTYPE IfType,
    _In_ UINT32 NetLuidIndex)
{
    PLIST_ENTRY Link;
    BOOLEAN Found = FALSE;

    KeAcquireSpinLockAtDpcLevel(&AdapterListLock);
    for (Link = AdapterListHead.Flink; Link != &AdapterListHead; Link = Link->Flink)
    {
        PLOGICAL_ADAPTER Adapter = CONTAINING_RECORD(Link, LOGICAL_ADAPTER, ListEntry);
        PNDIS6_ADAPTER_EXT Ext;

        if (!Adapter->IsNdis6)
            continue;

        Ext = NDIS6_EXT(Adapter);
        if (Ext != NULL &&
            Ext->NetLuid.Info.IfType == IfType &&
            Ext->NetLuid.Info.NetLuidIndex == NetLuidIndex)
        {
            Found = TRUE;
            break;
        }
    }
    KeReleaseSpinLockFromDpcLevel(&AdapterListLock);

    return Found;
}

BOOLEAN
Ndis6LookupAdapterNetLuid(
    _In_ NET_LUID NetLuid,
    _Out_opt_ PNET_IFINDEX IfIndex)
{
    PLIST_ENTRY Link;
    KIRQL OldIrql;
    BOOLEAN Found = FALSE;

    KeAcquireSpinLock(&AdapterListLock, &OldIrql);
    for (Link = AdapterListHead.Flink; Link != &AdapterListHead; Link = Link->Flink)
    {
        PLOGICAL_ADAPTER Adapter = CONTAINING_RECORD(Link, LOGICAL_ADAPTER, ListEntry);
        PNDIS6_ADAPTER_EXT Ext;

        if (!Adapter->IsNdis6)
            continue;

        Ext = NDIS6_EXT(Adapter);
        if (Ext != NULL && Ext->NetLuid.Value == NetLuid.Value)
        {
            if (IfIndex != NULL)
                *IfIndex = Ext->IfIndex;
            Found = TRUE;
            break;
        }
    }
    KeReleaseSpinLock(&AdapterListLock, OldIrql);

    return Found;
}

static
BOOLEAN
Ndis6LookupAdapterInterface(
    _In_ NET_IFINDEX IfIndex,
    _Out_ PNET_LUID NetLuid)
{
    PLIST_ENTRY Link;
    KIRQL OldIrql;
    BOOLEAN Found = FALSE;

    KeAcquireSpinLock(&AdapterListLock, &OldIrql);
    for (Link = AdapterListHead.Flink; Link != &AdapterListHead; Link = Link->Flink)
    {
        PLOGICAL_ADAPTER Adapter = CONTAINING_RECORD(Link, LOGICAL_ADAPTER, ListEntry);
        PNDIS6_ADAPTER_EXT Ext;

        if (!Adapter->IsNdis6)
            continue;

        Ext = NDIS6_EXT(Adapter);
        if (Ext != NULL && Ext->IfIndex == IfIndex)
        {
            *NetLuid = Ext->NetLuid;
            Found = TRUE;
            break;
        }
    }
    KeReleaseSpinLock(&AdapterListLock, OldIrql);

    return Found;
}

NDIS_STATUS
NTAPI
NdisIfRegisterProvider(
    _In_ PNDIS_IF_PROVIDER_CHARACTERISTICS ProviderCharacteristics,
    _In_opt_ NDIS_HANDLE IfProviderContext,
    _Out_ PNDIS_HANDLE pNdisIfProviderHandle)
{
    PNDIS6_IF_PROVIDER Provider;
    KIRQL OldIrql;

    PAGED_CODE();

    if (pNdisIfProviderHandle == NULL)
        return NDIS_STATUS_INVALID_PARAMETER;

    *pNdisIfProviderHandle = NULL;

    if (ProviderCharacteristics == NULL ||
        ProviderCharacteristics->Header.Size < NDIS_SIZEOF_IF_PROVIDER_CHARACTERISTICS_REVISION_1)
    {
        return NDIS_STATUS_INVALID_PARAMETER;
    }

    Provider = ExAllocatePoolWithTag(NonPagedPool, sizeof(*Provider), NDIS6_IF_PROVIDER_TAG);
    if (Provider == NULL)
        return NDIS_STATUS_RESOURCES;

    RtlZeroMemory(Provider, sizeof(*Provider));
    Provider->Signature = NDIS6_IF_PROVIDER_SIGNATURE;
    RtlCopyMemory(&Provider->Characteristics,
                  ProviderCharacteristics,
                  NDIS_SIZEOF_IF_PROVIDER_CHARACTERISTICS_REVISION_1);
    Provider->Context = IfProviderContext;

    KeAcquireSpinLock(&Ndis6IfLock, &OldIrql);
    InsertTailList(&Ndis6IfProviders, &Provider->Link);
    KeReleaseSpinLock(&Ndis6IfLock, OldIrql);

    *pNdisIfProviderHandle = (NDIS_HANDLE)Provider;
    return NDIS_STATUS_SUCCESS;
}

VOID
NTAPI
NdisIfDeregisterProvider(
    _In_ NDIS_HANDLE NdisProviderHandle)
{
    PNDIS6_IF_PROVIDER Provider = (PNDIS6_IF_PROVIDER)NdisProviderHandle;
    PLIST_ENTRY Link, Next;
    KIRQL OldIrql;

    PAGED_CODE();

    if (Provider == NULL || Provider->Signature != NDIS6_IF_PROVIDER_SIGNATURE)
        return;

    KeAcquireSpinLock(&Ndis6IfLock, &OldIrql);
    for (Link = Ndis6IfEntries.Flink; Link != &Ndis6IfEntries; Link = Next)
    {
        PNDIS6_IF_ENTRY Entry = CONTAINING_RECORD(Link, NDIS6_IF_ENTRY, Link);

        Next = Link->Flink;
        if (Entry->Provider == Provider)
        {
            Entry->Provider = NULL;
            Entry->ProviderIfContext = NULL;
        }
    }
    RemoveEntryList(&Provider->Link);
    Provider->Signature = 0;
    KeReleaseSpinLock(&Ndis6IfLock, OldIrql);

    ExFreePoolWithTag(Provider, NDIS6_IF_PROVIDER_TAG);
}

NDIS_STATUS
NTAPI
NdisIfAllocateNetLuidIndex(
    _In_ NET_IFTYPE ifType,
    _Out_ PUINT32 pNetLuidIndex)
{
    PNDIS6_LUID_INDEX Index;
    UINT32 Value;
    KIRQL OldIrql;

    PAGED_CODE();

    if (pNetLuidIndex == NULL)
        return NDIS_STATUS_INVALID_PARAMETER;

    Index = ExAllocatePoolWithTag(NonPagedPool, sizeof(*Index), NDIS6_IF_PROVIDER_TAG);
    if (Index == NULL)
        return NDIS_STATUS_RESOURCES;

    Index->IfType = ifType;

    KeAcquireSpinLock(&Ndis6IfLock, &OldIrql);
    for (Value = 0; Value <= 0x00ffffff; Value++)
    {
        if (!Ndis6LuidIndexAllocatedLocked(ifType, Value) &&
            !Ndis6AdapterUsesNetLuidIndexAtDpcLevel(ifType, Value))
        {
            break;
        }
    }
    if (Value > 0x00ffffff)
    {
        KeReleaseSpinLock(&Ndis6IfLock, OldIrql);
        ExFreePoolWithTag(Index, NDIS6_IF_PROVIDER_TAG);
        return NDIS_STATUS_RESOURCES;
    }
    Index->NetLuidIndex = Value;
    InsertTailList(&Ndis6LuidIndices, &Index->Link);
    KeReleaseSpinLock(&Ndis6IfLock, OldIrql);

    *pNetLuidIndex = Index->NetLuidIndex;
    return NDIS_STATUS_SUCCESS;
}

NDIS_STATUS
NTAPI
NdisIfFreeNetLuidIndex(
    _In_ NET_IFTYPE ifType,
    _In_ UINT32 NetLuidIndex)
{
    PNDIS6_LUID_INDEX Found = NULL;
    PLIST_ENTRY Link;
    KIRQL OldIrql;

    PAGED_CODE();

    KeAcquireSpinLock(&Ndis6IfLock, &OldIrql);
    for (Link = Ndis6LuidIndices.Flink; Link != &Ndis6LuidIndices; Link = Link->Flink)
    {
        PNDIS6_LUID_INDEX Index = CONTAINING_RECORD(Link, NDIS6_LUID_INDEX, Link);

        if (Index->IfType == ifType && Index->NetLuidIndex == NetLuidIndex)
        {
            RemoveEntryList(&Index->Link);
            Found = Index;
            break;
        }
    }
    KeReleaseSpinLock(&Ndis6IfLock, OldIrql);

    if (Found == NULL)
        return NDIS_STATUS_INVALID_PARAMETER;

    ExFreePoolWithTag(Found, NDIS6_IF_PROVIDER_TAG);
    return NDIS_STATUS_SUCCESS;
}

NDIS_STATUS
NTAPI
NdisIfRegisterInterface(
    _In_ NDIS_HANDLE NdisProviderHandle,
    _In_ NET_LUID NetLuid,
    _In_ NDIS_HANDLE ProviderIfContext,
    _In_ PNET_IF_INFORMATION pIfInfo,
    _Out_ PNET_IFINDEX pfIndex)
{
    PNDIS6_IF_PROVIDER Provider = (PNDIS6_IF_PROVIDER)NdisProviderHandle;
    PNDIS6_IF_ENTRY Entry, NewEntry;
    PLIST_ENTRY Link;
    NDIS_STATUS Status = NDIS_STATUS_SUCCESS;
    KIRQL OldIrql;

    PAGED_CODE();

    if (pfIndex == NULL)
        return NDIS_STATUS_INVALID_PARAMETER;

    *pfIndex = 0;

    if (Provider == NULL ||
        Provider->Signature != NDIS6_IF_PROVIDER_SIGNATURE ||
        pIfInfo == NULL)
    {
        return NDIS_STATUS_INVALID_PARAMETER;
    }

    if (Ndis6LookupAdapterNetLuid(NetLuid, NULL))
        return (NDIS_STATUS)STATUS_DUPLICATE_OBJECTID;

    NewEntry = ExAllocatePoolWithTag(NonPagedPool, sizeof(*NewEntry), NDIS6_IF_PROVIDER_TAG);
    if (NewEntry == NULL)
        return NDIS_STATUS_RESOURCES;

    KeAcquireSpinLock(&Ndis6IfLock, &OldIrql);
    Entry = NULL;
    for (Link = Ndis6IfEntries.Flink; Link != &Ndis6IfEntries; Link = Link->Flink)
    {
        PNDIS6_IF_ENTRY Candidate = CONTAINING_RECORD(Link, NDIS6_IF_ENTRY, Link);

        if (Candidate->NetLuid.Value == NetLuid.Value)
        {
            Entry = Candidate;
            break;
        }
    }

    if (Entry != NULL && Entry->Provider != NULL)
    {
        Status = (NDIS_STATUS)STATUS_DUPLICATE_OBJECTID;
    }
    else
    {
        if (Entry == NULL)
        {
            Entry = NewEntry;
            NewEntry = NULL;
            Entry->IfIndex = Ndis6AllocateInterfaceIndex();
            Entry->NetLuid = NetLuid;
            InsertTailList(&Ndis6IfEntries, &Entry->Link);
        }
        Entry->Provider = Provider;
        Entry->ProviderIfContext = ProviderIfContext;
        *pfIndex = Entry->IfIndex;
    }
    KeReleaseSpinLock(&Ndis6IfLock, OldIrql);

    if (NewEntry != NULL)
        ExFreePoolWithTag(NewEntry, NDIS6_IF_PROVIDER_TAG);

    return Status;
}

VOID
NTAPI
NdisIfDeregisterInterface(
    _In_ NET_IFINDEX ifIndex)
{
    PNDIS6_IF_ENTRY Entry;
    KIRQL OldIrql;

    PAGED_CODE();

    KeAcquireSpinLock(&Ndis6IfLock, &OldIrql);
    Entry = Ndis6FindIfEntryLocked(ifIndex);
    if (Entry != NULL)
    {
        Entry->Provider = NULL;
        Entry->ProviderIfContext = NULL;
    }
    KeReleaseSpinLock(&Ndis6IfLock, OldIrql);
}

NDIS_STATUS
NTAPI
NdisIfGetNetLuidFromInterfaceIndex(
    _In_ NET_IFINDEX ifIndex,
    _Out_ PNET_LUID pNetLuid)
{
    PAGED_CODE();

    if (pNetLuid == NULL || ifIndex == 0)
        return NDIS_STATUS_INVALID_PARAMETER;

    if (Ndis6LookupProviderInterface(ifIndex, pNetLuid) ||
        Ndis6LookupAdapterInterface(ifIndex, pNetLuid))
    {
        return NDIS_STATUS_SUCCESS;
    }

    return NDIS_STATUS_INTERFACE_NOT_FOUND;
}

NDIS_STATUS
NTAPI
NdisIfQueryBindingIfIndex(
    _In_ NDIS_HANDLE NdisBindingHandle,
    _Out_ PNET_IFINDEX pBoundIfIndex,
    _Out_ PNET_LUID pBoundIfNetLuid,
    _Out_ PNET_IFINDEX pLowestIfIndex,
    _Out_ PNET_LUID pLowestIfNetLuid)
{
    PNDIS6_PROTOCOL_BINDING Binding = (PNDIS6_PROTOCOL_BINDING)NdisBindingHandle;
    PNDIS6_ADAPTER_EXT Ext;
    NDIS_STATUS Status = NDIS_STATUS_INVALID_PARAMETER;

    if (pBoundIfIndex == NULL || pBoundIfNetLuid == NULL ||
        pLowestIfIndex == NULL || pLowestIfNetLuid == NULL)
    {
        return NDIS_STATUS_INVALID_PARAMETER;
    }

    if (!Ndis6ReferenceProtocolBinding(Binding))
        return NDIS_STATUS_INVALID_PARAMETER;

    if (Binding->Adapter != NULL && Binding->Adapter->IsNdis6)
    {
        Ext = NDIS6_EXT(Binding->Adapter);
        if (Ext != NULL)
        {
            *pBoundIfIndex = Ext->IfIndex;
            *pBoundIfNetLuid = Ext->NetLuid;
            *pLowestIfIndex = Ext->IfIndex;
            *pLowestIfNetLuid = Ext->NetLuid;
            Status = NDIS_STATUS_SUCCESS;
        }
    }

    Ndis6DereferenceProtocolBinding(Binding);
    return Status;
}
