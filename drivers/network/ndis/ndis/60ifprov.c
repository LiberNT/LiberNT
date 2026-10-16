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

        if (Entry->IfIndex == IfIndex)
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

        if (Entry->NetLuid.Value == NetLuid.Value)
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
    LIST_ENTRY Orphans;
    KIRQL OldIrql;

    PAGED_CODE();

    if (Provider == NULL || Provider->Signature != NDIS6_IF_PROVIDER_SIGNATURE)
        return;

    InitializeListHead(&Orphans);

    KeAcquireSpinLock(&Ndis6IfLock, &OldIrql);
    for (Link = Ndis6IfEntries.Flink; Link != &Ndis6IfEntries; Link = Next)
    {
        PNDIS6_IF_ENTRY Entry = CONTAINING_RECORD(Link, NDIS6_IF_ENTRY, Link);

        Next = Link->Flink;
        if (Entry->Provider == Provider)
        {
            RemoveEntryList(&Entry->Link);
            InsertTailList(&Orphans, &Entry->Link);
        }
    }
    RemoveEntryList(&Provider->Link);
    Provider->Signature = 0;
    KeReleaseSpinLock(&Ndis6IfLock, OldIrql);

    while (!IsListEmpty(&Orphans))
    {
        PNDIS6_IF_ENTRY Entry = CONTAINING_RECORD(RemoveHeadList(&Orphans), NDIS6_IF_ENTRY, Link);

        ExFreePoolWithTag(Entry, NDIS6_IF_PROVIDER_TAG);
    }

    ExFreePoolWithTag(Provider, NDIS6_IF_PROVIDER_TAG);
}

NDIS_STATUS
NTAPI
NdisIfAllocateNetLuidIndex(
    _In_ NET_IFTYPE ifType,
    _Out_ PUINT32 pNetLuidIndex)
{
    PNDIS6_LUID_INDEX Index;
    KIRQL OldIrql;

    PAGED_CODE();

    if (pNetLuidIndex == NULL)
        return NDIS_STATUS_INVALID_PARAMETER;

    Index = ExAllocatePoolWithTag(NonPagedPool, sizeof(*Index), NDIS6_IF_PROVIDER_TAG);
    if (Index == NULL)
        return NDIS_STATUS_RESOURCES;

    Index->IfType = ifType;
    Index->NetLuidIndex = (UINT32)Ndis6AllocateInterfaceIndex();

    KeAcquireSpinLock(&Ndis6IfLock, &OldIrql);
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
    PNDIS6_IF_ENTRY Entry;
    KIRQL OldIrql;

    PAGED_CODE();

    if (pfIndex == NULL)
        return NDIS_STATUS_INVALID_PARAMETER;

    *pfIndex = 0;

    if (Provider == NULL ||
        Provider->Signature != NDIS6_IF_PROVIDER_SIGNATURE ||
        pIfInfo == NULL ||
        NetLuid.Info.NetLuidIndex == 0)
    {
        return NDIS_STATUS_INVALID_PARAMETER;
    }

    if (Ndis6LookupProviderNetLuid(NetLuid, NULL))
        return NDIS_STATUS_INVALID_PARAMETER;

    Entry = ExAllocatePoolWithTag(NonPagedPool, sizeof(*Entry), NDIS6_IF_PROVIDER_TAG);
    if (Entry == NULL)
        return NDIS_STATUS_RESOURCES;

    Entry->IfIndex = Ndis6AllocateInterfaceIndex();
    Entry->NetLuid = NetLuid;
    Entry->Provider = Provider;
    Entry->ProviderIfContext = ProviderIfContext;

    KeAcquireSpinLock(&Ndis6IfLock, &OldIrql);
    InsertTailList(&Ndis6IfEntries, &Entry->Link);
    KeReleaseSpinLock(&Ndis6IfLock, OldIrql);

    *pfIndex = Entry->IfIndex;
    return NDIS_STATUS_SUCCESS;
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
        RemoveEntryList(&Entry->Link);
    KeReleaseSpinLock(&Ndis6IfLock, OldIrql);

    if (Entry != NULL)
        ExFreePoolWithTag(Entry, NDIS6_IF_PROVIDER_TAG);
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
