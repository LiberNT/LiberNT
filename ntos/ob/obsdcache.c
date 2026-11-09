/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Object manager security descriptor cache
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

/* PRIVATE DATA ***************************************************************/

#define OBP_SD_POOL_TAG 'cSbO'

/*
 * Bucket count and hash are private to this file: no consumer can observe
 * either, so they are chosen for spread rather than to match anything.
 */
#define OBP_SD_BUCKETS 256u
#define OBP_SD_HASH_SEED 0x811C9DC5u
#define OBP_SD_HASH_PRIME 0x01000193u

static OB_SD_CACHE_LIST ObpSdBucket[OBP_SD_BUCKETS];

typedef struct _OBP_SD_KEY
{
    PSECURITY_DESCRIPTOR Descriptor;
    ULONG Length;
    ULONG Hash;
} OBP_SD_KEY, *POBP_SD_KEY;

/* PRIVATE FUNCTIONS **********************************************************/

static
ULONG
ObpSdHash(
    _In_reads_bytes_(Length) const VOID *Bytes,
    _In_ ULONG Length)
{
    const UCHAR *Cursor = Bytes;
    const UCHAR *End = Cursor + Length;
    ULONG Hash = OBP_SD_HASH_SEED;

    while (Cursor != End) Hash = (Hash ^ *Cursor++) * OBP_SD_HASH_PRIME;

    return Hash;
}

static
POB_SD_CACHE_LIST
ObpSdBucketOf(
    _In_ ULONG Hash)
{
    return &ObpSdBucket[Hash % OBP_SD_BUCKETS];
}

static
VOID
ObpSdLock(
    _Inout_ POB_SD_CACHE_LIST Bucket)
{
    ASSERT(KeGetCurrentIrql() <= APC_LEVEL);
    KeEnterCriticalRegion();
    ExAcquirePushLockExclusive(&Bucket->PushLock);
}

static
VOID
ObpSdUnlock(
    _Inout_ POB_SD_CACHE_LIST Bucket)
{
    ExReleasePushLock(&Bucket->PushLock);
    KeLeaveCriticalRegion();
}

/* Only a byte-for-byte equal descriptor may share an entry. */
static
PSECURITY_DESCRIPTOR_HEADER
ObpSdFind(
    _In_ POB_SD_CACHE_LIST Bucket,
    _In_ POBP_SD_KEY Key)
{
    PLIST_ENTRY Cursor = Bucket->Head.Flink;
    PSECURITY_DESCRIPTOR_HEADER Entry;

    while (Cursor != &Bucket->Head)
    {
        Entry = ObpGetHeaderForEntry(Cursor);
        Cursor = Cursor->Flink;

        if (Entry->FullHash != Key->Hash) continue;
        if (RtlLengthSecurityDescriptor(&Entry->SecurityDescriptor) != Key->Length) continue;
        if (RtlCompareMemory(&Entry->SecurityDescriptor,
                             Key->Descriptor,
                             Key->Length) != Key->Length)
        {
            continue;
        }

        return Entry;
    }

    return NULL;
}

static
PSECURITY_DESCRIPTOR_HEADER
ObpSdBuild(
    _In_ POBP_SD_KEY Key,
    _In_ ULONG Bias)
{
    SIZE_T Bytes = FIELD_OFFSET(SECURITY_DESCRIPTOR_HEADER, SecurityDescriptor) +
                   Key->Length;
    PSECURITY_DESCRIPTOR_HEADER Entry;

    Entry = ExAllocatePoolWithTag(PagedPool, Bytes, OBP_SD_POOL_TAG);
    if (Entry == NULL) return NULL;

    Entry->FullHash = Key->Hash;
    Entry->RefCount = Bias;
    RtlCopyMemory(&Entry->SecurityDescriptor, Key->Descriptor, Key->Length);

    return Entry;
}

/*
 * An entry has to stay readable for as long as anything holds it, so the copy
 * the cache keeps is always self-relative whatever form the caller passed.
 */
static
NTSTATUS
ObpSdMakeKey(
    _In_ PSECURITY_DESCRIPTOR Input,
    _Out_ POBP_SD_KEY Key,
    _Outptr_result_maybenull_ PSECURITY_DESCRIPTOR *Scratch)
{
    PSECURITY_DESCRIPTOR Flat;
    NTSTATUS Status;
    ULONG Bytes;

    *Scratch = NULL;

    if (((PISECURITY_DESCRIPTOR)Input)->Control & SE_SELF_RELATIVE)
    {
        Flat = Input;
    }
    else
    {
        Bytes = 0;
        Status = RtlAbsoluteToSelfRelativeSD(Input, NULL, &Bytes);
        if (Status != STATUS_BUFFER_TOO_SMALL)
            return NT_SUCCESS(Status) ? STATUS_INVALID_SECURITY_DESCR : Status;

        Flat = ExAllocatePoolWithTag(PagedPool, Bytes, OBP_SD_POOL_TAG);
        if (Flat == NULL) return STATUS_INSUFFICIENT_RESOURCES;

        Status = RtlAbsoluteToSelfRelativeSD(Input, Flat, &Bytes);
        if (!NT_SUCCESS(Status))
        {
            ExFreePoolWithTag(Flat, OBP_SD_POOL_TAG);
            return Status;
        }

        *Scratch = Flat;
    }

    Key->Descriptor = Flat;
    Key->Length = RtlLengthSecurityDescriptor(Flat);
    Key->Hash = ObpSdHash(Flat, Key->Length);

    return STATUS_SUCCESS;
}

CODE_SEG("INIT")
NTSTATUS
NTAPI
ObpInitSdCache(VOID)
{
    POB_SD_CACHE_LIST Bucket = ObpSdBucket;
    POB_SD_CACHE_LIST Last = ObpSdBucket + OBP_SD_BUCKETS;

    while (Bucket != Last)
    {
        InitializeListHead(&Bucket->Head);
        ExInitializePushLock(&Bucket->PushLock);
        Bucket++;
    }

    return STATUS_SUCCESS;
}

/*
 * The descriptor field of an object header is an EX_FAST_REF: it lends out the
 * references batched into its low bits before anyone has to touch the entry.
 */
PSECURITY_DESCRIPTOR
NTAPI
ObpReferenceSecurityDescriptor(
    _In_ POBJECT_HEADER ObjectHeader)
{
    PEX_FAST_REF Slot = (PEX_FAST_REF)&ObjectHeader->SecurityDescriptor;
    PSECURITY_DESCRIPTOR_HEADER Entry;
    PSECURITY_DESCRIPTOR Descriptor;
    EX_FAST_REF Taken;
    ULONG Spare;

    Taken = ExAcquireFastReference(Slot);
    Spare = ExGetCountFastReference(Taken);
    Descriptor = ExGetObjectFastReference(Taken);

    if (Spare == 0)
    {
        /* Nothing batched: hold the header still and charge one directly */
        ObpAcquireObjectLockShared(ObjectHeader);

        Descriptor = ExGetObjectFastReference(*Slot);
        if (Descriptor != NULL)
            InterlockedIncrement((PLONG)&ObpGetHeaderForSd(Descriptor)->RefCount);

        ObpReleaseObjectLock(ObjectHeader);
        return Descriptor;
    }

    ASSERT(Descriptor != NULL);
    Entry = ObpGetHeaderForSd(Descriptor);

    if (Spare == 1)
    {
        /* That was the last one; buy another batch for whoever comes next */
        InterlockedExchangeAdd((PLONG)&Entry->RefCount, MAX_FAST_REFS);

        if (!ExInsertFastReference(Slot, Descriptor))
        {
            /* Refilled or replaced meanwhile; ours is still outstanding, so
             * this can never be the reference that frees the entry */
            InterlockedExchangeAdd((PLONG)&Entry->RefCount, -(LONG)MAX_FAST_REFS);
        }
    }

    return Descriptor;
}

/* PUBLIC FUNCTIONS ***********************************************************/

NTSTATUS
NTAPI
ObLogSecurityDescriptor(
    _In_ PSECURITY_DESCRIPTOR InputSecurityDescriptor,
    _Out_ PSECURITY_DESCRIPTOR *OutputSecurityDescriptor,
    _In_ ULONG RefBias)
{
    PSECURITY_DESCRIPTOR_HEADER Entry, Spare = NULL;
    PSECURITY_DESCRIPTOR Scratch;
    POB_SD_CACHE_LIST Bucket;
    NTSTATUS Status;
    OBP_SD_KEY Key;

    PAGED_CODE();

    Status = ObpSdMakeKey(InputSecurityDescriptor, &Key, &Scratch);
    if (!NT_SUCCESS(Status)) return Status;

    Bucket = ObpSdBucketOf(Key.Hash);

    for (;;)
    {
        ObpSdLock(Bucket);

        Entry = ObpSdFind(Bucket, &Key);
        if (Entry != NULL)
        {
            InterlockedExchangeAdd((PLONG)&Entry->RefCount, RefBias);
        }
        else if (Spare != NULL)
        {
            InsertHeadList(&Bucket->Head, &Spare->Link);
            Entry = Spare;
            Spare = NULL;
        }

        ObpSdUnlock(Bucket);

        if (Entry != NULL) break;

        /* Build outside the lock, then look again: another thread may have
         * cached the very same descriptor while we were allocating */
        Spare = ObpSdBuild(&Key, RefBias);
        if (Spare == NULL)
        {
            if (Scratch != NULL) ExFreePoolWithTag(Scratch, OBP_SD_POOL_TAG);
            return STATUS_INSUFFICIENT_RESOURCES;
        }
    }

    if (Spare != NULL) ExFreePoolWithTag(Spare, OBP_SD_POOL_TAG);
    if (Scratch != NULL) ExFreePoolWithTag(Scratch, OBP_SD_POOL_TAG);

    *OutputSecurityDescriptor = &Entry->SecurityDescriptor;
    return STATUS_SUCCESS;
}

VOID
NTAPI
ObReferenceSecurityDescriptor(
    _In_ PSECURITY_DESCRIPTOR SecurityDescriptor,
    _In_ ULONG Count)
{
    PSECURITY_DESCRIPTOR_HEADER Entry = ObpGetHeaderForSd(SecurityDescriptor);

    ASSERT(Entry->RefCount != 0);
    InterlockedExchangeAdd((PLONG)&Entry->RefCount, Count);
}

VOID
NTAPI
ObDereferenceSecurityDescriptor(
    _Inout_ PSECURITY_DESCRIPTOR SecurityDescriptor,
    _In_ ULONG Count)
{
    PSECURITY_DESCRIPTOR_HEADER Entry = ObpGetHeaderForSd(SecurityDescriptor);
    POB_SD_CACHE_LIST Bucket = ObpSdBucketOf(Entry->FullHash);
    BOOLEAN Last;

    PAGED_CODE();
    ASSERT(Entry->RefCount >= Count);

    /* Under the bucket lock, so a log in flight cannot hand this entry out
     * between the count reaching zero and the unlink */
    ObpSdLock(Bucket);

    Last = (InterlockedExchangeAdd((PLONG)&Entry->RefCount,
                                   -(LONG)Count) == (LONG)Count);
    if (Last) RemoveEntryList(&Entry->Link);

    ObpSdUnlock(Bucket);

    if (Last) ExFreePoolWithTag(Entry, OBP_SD_POOL_TAG);
}

/* EOF */
