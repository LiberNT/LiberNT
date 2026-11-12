/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     The fast resource, a reader/writer lock with a caller-held owner
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

/* PRIVATE DATA ***************************************************************/

/*
 * TODO: neither the fast resource nor its owner entry is published in any
 * header, documented, or named by the public symbols, so the shapes below are
 * this kernel's own. A caller that declares a fast resource from a Windows
 * header will not agree with them.
 */
typedef struct _EXP_FAST_RESOURCE
{
    EX_PUSH_LOCK Lock;
    volatile LONG Contention;
    volatile LONG SharedCount;
    volatile PVOID ExclusiveOwner;
} EXP_FAST_RESOURCE, *PEXP_FAST_RESOURCE;

typedef struct _EXP_FAST_OWNER_ENTRY
{
    PVOID Owner;
    LONG Depth;
    LONG Exclusive;
} EXP_FAST_OWNER_ENTRY, *PEXP_FAST_OWNER_ENTRY;

/* PUBLIC FUNCTIONS ***********************************************************/

VOID
NTAPI
ExpInitializeFastResource(
    _Out_ PVOID Resource)
{
    PEXP_FAST_RESOURCE FastResource = Resource;

    ExInitializePushLock(&FastResource->Lock);
    FastResource->Contention = 0;
    FastResource->SharedCount = 0;
    FastResource->ExclusiveOwner = NULL;
}

VOID
NTAPI
ExpInitializeFastOwnerEntry(
    _Out_ PVOID OwnerEntry)
{
    PEXP_FAST_OWNER_ENTRY Entry = OwnerEntry;

    Entry->Owner = NULL;
    Entry->Depth = 0;
    Entry->Exclusive = 0;
}

BOOLEAN
NTAPI
ExpAcquireFastResourceExclusive(
    _Inout_ PVOID Resource,
    _Inout_ PVOID OwnerEntry,
    _In_ ULONG Flags)
{
    PEXP_FAST_RESOURCE FastResource = Resource;
    PEXP_FAST_OWNER_ENTRY Entry = OwnerEntry;
    PVOID Owner = KeGetCurrentThread();

    UNREFERENCED_PARAMETER(Flags);

    if ((Entry->Depth != 0) && (Entry->Owner == Owner) && Entry->Exclusive)
    {
        Entry->Depth++;
        return TRUE;
    }

    if (FastResource->ExclusiveOwner != NULL)
    {
        InterlockedIncrement(&FastResource->Contention);
    }

    ExAcquirePushLockExclusive(&FastResource->Lock);

    FastResource->ExclusiveOwner = Owner;
    Entry->Owner = Owner;
    Entry->Depth = 1;
    Entry->Exclusive = 1;

    return TRUE;
}

BOOLEAN
NTAPI
ExpAcquireFastResourceShared(
    _Inout_ PVOID Resource,
    _Inout_ PVOID OwnerEntry,
    _In_ ULONG Flags)
{
    PEXP_FAST_RESOURCE FastResource = Resource;
    PEXP_FAST_OWNER_ENTRY Entry = OwnerEntry;
    PVOID Owner = KeGetCurrentThread();

    UNREFERENCED_PARAMETER(Flags);

    if ((Entry->Depth != 0) && (Entry->Owner == Owner))
    {
        Entry->Depth++;
        return TRUE;
    }

    ExAcquirePushLockShared(&FastResource->Lock);

    InterlockedIncrement(&FastResource->SharedCount);
    Entry->Owner = Owner;
    Entry->Depth = 1;
    Entry->Exclusive = 0;

    return TRUE;
}

VOID
NTAPI
ExpReleaseFastResource(
    _Inout_ PVOID Resource,
    _Inout_ PVOID OwnerEntry)
{
    PEXP_FAST_RESOURCE FastResource = Resource;
    PEXP_FAST_OWNER_ENTRY Entry = OwnerEntry;

    ASSERT(Entry->Depth > 0);

    if (--Entry->Depth != 0)
    {
        return;
    }

    Entry->Owner = NULL;

    if (Entry->Exclusive)
    {
        Entry->Exclusive = 0;
        FastResource->ExclusiveOwner = NULL;
        ExfReleasePushLockExclusive(&FastResource->Lock);
        return;
    }

    InterlockedDecrement(&FastResource->SharedCount);
    ExfReleasePushLockShared(&FastResource->Lock);
}

VOID
NTAPI
ExpDeleteFastResource(
    _Inout_ PVOID Resource)
{
    PEXP_FAST_RESOURCE FastResource = Resource;

    ASSERT(FastResource->ExclusiveOwner == NULL);
    ASSERT(FastResource->SharedCount == 0);

    ExInitializePushLock(&FastResource->Lock);
}

BOOLEAN
NTAPI
ExpIsFastResourceHeld(
    _In_ PVOID Resource)
{
    PEXP_FAST_RESOURCE FastResource = Resource;

    return (FastResource->ExclusiveOwner != NULL) ||
           (FastResource->SharedCount != 0);
}

BOOLEAN
NTAPI
ExpIsFastResourceHeldExclusive(
    _In_ PVOID Resource)
{
    PEXP_FAST_RESOURCE FastResource = Resource;

    return FastResource->ExclusiveOwner != NULL;
}

BOOLEAN
NTAPI
ExpIsFastResourceContended(
    _In_ PVOID Resource)
{
    PEXP_FAST_RESOURCE FastResource = Resource;

    return FastResource->Contention != 0;
}

/* EOF */
