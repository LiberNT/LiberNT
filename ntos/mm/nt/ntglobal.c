/*
 * PROJECT:     LiberNT NT Virtual Memory Subsystem
 * FILE:        ntoskrnl/nvs/nt/ntglobal.c
 * PURPOSE:     NT memory manager globals and supporting interfaces
 *
 * SPDX-FileCopyrightText: 2026 Ahmed ARIF
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include <mm/nt/mint.h>

MI_SYSTEM MiSystem;
MI_PROCESS_MANAGER MiProcessManager;
ULONG MmProductType;

#define MI_BOOT_ARENA_BYTES (1024 * 1024)

BOOLEAN MiPoolReady;

static UCHAR MiBootArena[MI_BOOT_ARENA_BYTES];
static volatile LONG MiBootArenaUsed;

PVOID
MiKmAllocate(
    _In_ SIZE_T Bytes)
{
    LONG Size, Offset;

    if (MiPoolReady)
        return ExAllocatePoolWithTag(NonPagedPool, Bytes, MI_CORE_TAG);

    if (Bytes > MI_BOOT_ARENA_BYTES)
        return NULL;

    Size = (LONG)ROUND_UP(Bytes, 16);
    for (;;)
    {
        Offset = InterlockedCompareExchange(&MiBootArenaUsed, 0, 0);
        ASSERT(Offset >= 0 && Offset <= MI_BOOT_ARENA_BYTES && (Offset & 15) == 0);
        if (Size > MI_BOOT_ARENA_BYTES - Offset)
            return NULL;

        if (InterlockedCompareExchange(&MiBootArenaUsed, Offset + Size, Offset) == Offset)
            return &MiBootArena[Offset];
    }
}

VOID
MiKmFree(
    _In_ PVOID Block)
{
    if ((PUCHAR)Block >= MiBootArena && (PUCHAR)Block < MiBootArena + MI_BOOT_ARENA_BYTES)
        return;

    ExFreePoolWithTag(Block, MI_CORE_TAG);
}

static KEVENT MiMemoryAvailableEvent;
static BOOLEAN MiMemoryEventReady;

/* Microsoft documents the levels a memory resource notification uses: the low
 * event signals at about 32 MB of available memory per 4 GB installed, to a
 * maximum of 64 MB, and the high event at three times that level. */
#define MI_LOW_MEMORY_SHARE         128
#define MI_LOW_MEMORY_CAP_BYTES     (64ULL * 1024 * 1024)
#define MI_HIGH_MEMORY_FACTOR       3

static PKEVENT MiLowMemoryCondition;
static PKEVENT MiHighMemoryCondition;
static ULONG64 MiLowMemoryPages;
static ULONG64 MiHighMemoryPages;

static
NTSTATUS
MiCreateConditionSd(
    _Outptr_ PSECURITY_DESCRIPTOR *SecurityDescriptor)
{
    PSECURITY_DESCRIPTOR Sd;
    NTSTATUS Status;
    ULONG AclSize;
    PACL Dacl;

    AclSize = sizeof(ACL) +
              sizeof(ACE) + RtlLengthSid(SeWorldSid) +
              sizeof(ACE) + RtlLengthSid(SeAliasAdminsSid) +
              sizeof(ACE) + RtlLengthSid(SeLocalSystemSid);

    Sd = ExAllocatePoolWithTag(PagedPool, sizeof(SECURITY_DESCRIPTOR) + AclSize, MI_CORE_TAG);
    if (Sd == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;

    Status = RtlCreateSecurityDescriptor(Sd, SECURITY_DESCRIPTOR_REVISION);
    if (NT_SUCCESS(Status))
    {
        Dacl = (PACL)((PUCHAR)Sd + sizeof(SECURITY_DESCRIPTOR));

        Status = RtlCreateAcl(Dacl, AclSize, ACL_REVISION);
        if (NT_SUCCESS(Status))
            Status = RtlAddAccessAllowedAce(Dacl, ACL_REVISION,
                                            EVENT_QUERY_STATE | SYNCHRONIZE | READ_CONTROL,
                                            SeWorldSid);
        if (NT_SUCCESS(Status))
            Status = RtlAddAccessAllowedAce(Dacl, ACL_REVISION, EVENT_ALL_ACCESS, SeAliasAdminsSid);
        if (NT_SUCCESS(Status))
            Status = RtlAddAccessAllowedAce(Dacl, ACL_REVISION, EVENT_ALL_ACCESS, SeLocalSystemSid);
        if (NT_SUCCESS(Status))
            Status = RtlSetDaclSecurityDescriptor(Sd, TRUE, Dacl, FALSE);
    }

    if (!NT_SUCCESS(Status))
    {
        ExFreePoolWithTag(Sd, MI_CORE_TAG);
        return Status;
    }

    *SecurityDescriptor = Sd;
    return STATUS_SUCCESS;
}

static
PKEVENT
MiCreateConditionEvent(
    _In_ PCWSTR Path,
    _In_ PSECURITY_DESCRIPTOR SecurityDescriptor)
{
    OBJECT_ATTRIBUTES ObjectAttributes;
    UNICODE_STRING Name;
    NTSTATUS Status;
    HANDLE Handle;
    PVOID Event;

    RtlInitUnicodeString(&Name, Path);
    InitializeObjectAttributes(&ObjectAttributes,
                               &Name,
                               OBJ_CASE_INSENSITIVE | OBJ_PERMANENT | OBJ_KERNEL_HANDLE,
                               NULL,
                               SecurityDescriptor);

    Status = ZwCreateEvent(&Handle, EVENT_ALL_ACCESS, &ObjectAttributes, NotificationEvent, FALSE);
    if (!NT_SUCCESS(Status))
        return NULL;

    Status = ObReferenceObjectByHandle(Handle, 0, ExEventObjectType, KernelMode, &Event, NULL);
    ZwClose(Handle);
    if (!NT_SUCCESS(Status))
        return NULL;

    return (PKEVENT)Event;
}

VOID
MiMemoryNotificationInitialize(VOID)
{
    PSECURITY_DESCRIPTOR Sd;
    ULONG64 Pages;

    Pages = (ULONG64)MmNumberOfPhysicalPages / MI_LOW_MEMORY_SHARE;
    if (Pages > (MI_LOW_MEMORY_CAP_BYTES >> PAGE_SHIFT))
        Pages = MI_LOW_MEMORY_CAP_BYTES >> PAGE_SHIFT;

    MiLowMemoryPages = Pages;
    MiHighMemoryPages = Pages * MI_HIGH_MEMORY_FACTOR;

    /* TODO: the LowMemoryThreshold and HighMemoryThreshold values of the
     * memory management key are not read yet; only the documented defaults
     * above are used */
    if (!NT_SUCCESS(MiCreateConditionSd(&Sd)))
        return;

    MiLowMemoryCondition = MiCreateConditionEvent(L"\\KernelObjects\\LowMemoryCondition", Sd);
    MiHighMemoryCondition = MiCreateConditionEvent(L"\\KernelObjects\\HighMemoryCondition", Sd);
    ExFreePoolWithTag(Sd, MI_CORE_TAG);
}

VOID
MiUpdateMemoryNotification(
    _In_ ULONG64 AvailablePages)
{
    if (MiLowMemoryCondition == NULL || MiHighMemoryCondition == NULL)
        return;

    if (AvailablePages <= MiLowMemoryPages)
    {
        KeClearEvent(MiHighMemoryCondition);
        KeSetEvent(MiLowMemoryCondition, 0, FALSE);
        return;
    }

    if (AvailablePages >= MiHighMemoryPages)
    {
        KeClearEvent(MiLowMemoryCondition);
        KeSetEvent(MiHighMemoryCondition, 0, FALSE);
        return;
    }

    KeClearEvent(MiLowMemoryCondition);
    KeClearEvent(MiHighMemoryCondition);
}


VOID
MiMemoryEventInitialize(VOID)
{
    ASSERT(!MiMemoryEventReady);
    KeInitializeEvent(&MiMemoryAvailableEvent, NotificationEvent, FALSE);
    MiMemoryEventReady = TRUE;
}

VOID
MiSignalMemoryAvailable(VOID)
{
    if (MiMemoryEventReady)
        KePulseEvent(&MiMemoryAvailableEvent, 0, FALSE);
}

NTSTATUS
MiWaitForMemory(
    _In_ NTSTATUS Status,
    _Inout_ PULONG Attempts)
{
    LARGE_INTEGER Timeout;
    ULONG64 Available;
    ULONG Frame;

    if (Status != STATUS_NO_MEMORY)
        return STATUS_SUCCESS;

    if (++*Attempts > 64 || KeGetCurrentIrql() > APC_LEVEL || PsGetCurrentThread()->MemoryMaker)
        return STATUS_NO_MEMORY;

    if (MiSystemPtesRefillTables(&MiSystem) != 0)
        return STATUS_SUCCESS;

    Available = MiPfnAvailablePages(&MiSystem.Pfn);
    if (Available > MiPfnListCount(&MiSystem.Pfn, MiPageStandby))
    {
        if (Available >= MiProcessManager.AvailableLow)
            return STATUS_SUCCESS;
    }
    else
    {
        Frame = MiPfnAllocatePage(&MiSystem.Pfn, 0);
        if (Frame != MI_FRAME_INVALID)
        {
            MiPfnFreePage(&MiSystem.Pfn, Frame);
            return STATUS_SUCCESS;
        }
    }

    if (!MiMemoryEventReady)
        return MiBalanceMemory(&MiSystem, &MiProcessManager) != 0 ? STATUS_SUCCESS : STATUS_NO_MEMORY;

    MiWakeBalanceSetManager();
    Timeout.QuadPart = -10 * 1000 * 10;
    KeWaitForSingleObject(&MiMemoryAvailableEvent, WrFreePage, KernelMode, FALSE, &Timeout);
    return STATUS_SUCCESS;
}
