/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Thread and job object property sets
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

typedef struct _PSP_PROPERTY_ENTRY
{
    LIST_ENTRY ListEntry;
    ULONG_PTR Key;
    PVOID Object;
} PSP_PROPERTY_ENTRY, *PPSP_PROPERTY_ENTRY;

static
KIRQL
PspAcquirePropertySet(
    _Inout_ PPS_PROPERTY_SET PropertySet)
{
    KIRQL OldIrql;

    KeAcquireSpinLock((PKSPIN_LOCK)&PropertySet->Lock, &OldIrql);
    if (PropertySet->ListHead.Flink == NULL)
        InitializeListHead(&PropertySet->ListHead);
    return OldIrql;
}

static
VOID
PspReleasePropertySet(
    _Inout_ PPS_PROPERTY_SET PropertySet,
    _In_ KIRQL OldIrql)
{
    KeReleaseSpinLock((PKSPIN_LOCK)&PropertySet->Lock, OldIrql);
}

static
PPSP_PROPERTY_ENTRY
PspFindProperty(
    _In_ PPS_PROPERTY_SET PropertySet,
    _In_ ULONG_PTR Key)
{
    PLIST_ENTRY ListEntry;
    PPSP_PROPERTY_ENTRY Entry;

    for (ListEntry = PropertySet->ListHead.Flink;
         ListEntry != &PropertySet->ListHead;
         ListEntry = ListEntry->Flink)
    {
        Entry = CONTAINING_RECORD(ListEntry, PSP_PROPERTY_ENTRY, ListEntry);
        if (Entry->Key == Key)
            return Entry;
    }

    return NULL;
}

static
PVOID
PspGetProperty(
    _Inout_ PPS_PROPERTY_SET PropertySet,
    _In_ ULONG_PTR Key)
{
    PPSP_PROPERTY_ENTRY Entry;
    PVOID Object = NULL;
    KIRQL OldIrql;

    OldIrql = PspAcquirePropertySet(PropertySet);
    Entry = PspFindProperty(PropertySet, Key);
    if (Entry)
    {
        Object = Entry->Object;
        ObReferenceObject(Object);
    }
    PspReleasePropertySet(PropertySet, OldIrql);

    return Object;
}

static
NTSTATUS
PspSetProperty(
    _Inout_ PPS_PROPERTY_SET PropertySet,
    _In_ ULONG_PTR Key,
    _In_opt_ PVOID Object)
{
    PPSP_PROPERTY_ENTRY Entry;
    KIRQL OldIrql;

    if (!Object)
    {
        OldIrql = PspAcquirePropertySet(PropertySet);
        Entry = PspFindProperty(PropertySet, Key);
        if (Entry)
            RemoveEntryList(&Entry->ListEntry);
        PspReleasePropertySet(PropertySet, OldIrql);

        if (Entry)
        {
            ObDereferenceObject(Entry->Object);
            ExFreePoolWithTag(Entry, TAG_PS_PROPERTY);
        }
        return STATUS_SUCCESS;
    }

    Entry = ExAllocatePoolWithTag(NonPagedPoolNx, sizeof(*Entry), TAG_PS_PROPERTY);
    if (!Entry)
        return STATUS_INSUFFICIENT_RESOURCES;

    Entry->Key = Key;
    Entry->Object = Object;

    OldIrql = PspAcquirePropertySet(PropertySet);
    if (PspFindProperty(PropertySet, Key))
    {
        PspReleasePropertySet(PropertySet, OldIrql);
        ExFreePoolWithTag(Entry, TAG_PS_PROPERTY);
        return STATUS_OBJECT_NAME_COLLISION;
    }

    ObReferenceObject(Object);
    InsertTailList(&PropertySet->ListHead, &Entry->ListEntry);
    PspReleasePropertySet(PropertySet, OldIrql);

    return STATUS_SUCCESS;
}

VOID
NTAPI
PspRundownPropertySet(
    _Inout_ PPS_PROPERTY_SET PropertySet)
{
    PPSP_PROPERTY_ENTRY Entry;
    PLIST_ENTRY ListEntry;
    LIST_ENTRY ListHead;
    KIRQL OldIrql;

    InitializeListHead(&ListHead);

    OldIrql = PspAcquirePropertySet(PropertySet);
    while (!IsListEmpty(&PropertySet->ListHead))
    {
        ListEntry = RemoveHeadList(&PropertySet->ListHead);
        InsertTailList(&ListHead, ListEntry);
    }
    PspReleasePropertySet(PropertySet, OldIrql);

    while (!IsListEmpty(&ListHead))
    {
        ListEntry = RemoveHeadList(&ListHead);
        Entry = CONTAINING_RECORD(ListEntry, PSP_PROPERTY_ENTRY, ListEntry);
        ObDereferenceObject(Entry->Object);
        ExFreePoolWithTag(Entry, TAG_PS_PROPERTY);
    }
}

_IRQL_requires_max_(DISPATCH_LEVEL)
PVOID
NTAPI
PsGetThreadProperty(
    _In_ PETHREAD Thread,
    _In_ ULONG_PTR Key,
    _In_ ULONG Flags)
{
    UNREFERENCED_PARAMETER(Flags);

    return PspGetProperty(&Thread->PropertySet, Key);
}

_IRQL_requires_max_(DISPATCH_LEVEL)
NTSTATUS
NTAPI
PsSetThreadProperty(
    _In_ PETHREAD Thread,
    _In_ ULONG_PTR Key,
    _In_ PVOID Object)
{
    return PspSetProperty(&Thread->PropertySet, Key, Object);
}

_IRQL_requires_max_(DISPATCH_LEVEL)
PVOID
NTAPI
PsGetJobProperty(
    _In_ PEJOB Job,
    _In_ ULONG_PTR Key)
{
    return PspGetProperty(&Job->PropertySet, Key);
}

_IRQL_requires_max_(DISPATCH_LEVEL)
NTSTATUS
NTAPI
PsSetJobProperty(
    _In_ PEJOB Job,
    _In_ ULONG_PTR Key,
    _In_ PVOID Object)
{
    return PspSetProperty(&Job->PropertySet, Key, Object);
}
