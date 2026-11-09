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

#define TAG_OB_SD_CACHE 'cSbO'

#define OBP_SD_CACHE_BUCKETS 256

static OB_SD_CACHE_LIST ObpSdCache[OBP_SD_CACHE_BUCKETS];

#define OBP_SD_HASH_BASIS  0x811C9DC5UL
#define OBP_SD_HASH_FACTOR 0x01000193UL

/* PRIVATE FUNCTIONS **********************************************************/

static
POB_SD_CACHE_LIST
ObpSdCacheBucket(
    _In_ ULONG FullHash)
{
    return &ObpSdCache[FullHash % OBP_SD_CACHE_BUCKETS];
}

static
VOID
ObpAcquireSdCacheBucket(
    _Inout_ POB_SD_CACHE_LIST Bucket)
{
    ASSERT(KeGetCurrentIrql() <= APC_LEVEL);
    KeEnterCriticalRegion();
    ExAcquirePushLockExclusive(&Bucket->PushLock);
}

static
VOID
ObpReleaseSdCacheBucket(
    _Inout_ POB_SD_CACHE_LIST Bucket)
{
    ExReleasePushLock(&Bucket->PushLock);
    KeLeaveCriticalRegion();
}

static
ULONG
ObpHashSecurityDescriptor(
    _In_reads_bytes_(Length) PVOID Buffer,
    _In_ ULONG Length)
{
    PUCHAR Bytes = Buffer;
    ULONG Hash = OBP_SD_HASH_BASIS;
    ULONG Index;

    for (Index = 0; Index < Length; Index++)
    {
        Hash = (Hash ^ Bytes[Index]) * OBP_SD_HASH_FACTOR;
    }

    return Hash;
}

static
PSECURITY_DESCRIPTOR_HEADER
ObpFindCachedSecurityDescriptor(
    _In_ POB_SD_CACHE_LIST Bucket,
    _In_ ULONG FullHash,
    _In_reads_bytes_(Length) PSECURITY_DESCRIPTOR SecurityDescriptor,
    _In_ ULONG Length)
{
    PLIST_ENTRY Entry;
    PSECURITY_DESCRIPTOR_HEADER SdHeader;

    for (Entry = Bucket->Head.Flink; Entry != &Bucket->Head; Entry = Entry->Flink)
    {
        SdHeader = ObpGetHeaderForEntry(Entry);

        if (SdHeader->FullHash != FullHash) continue;
        if (RtlLengthSecurityDescriptor(&SdHeader->SecurityDescriptor) != Length) continue;

        if (RtlCompareMemory(&SdHeader->SecurityDescriptor,
                             SecurityDescriptor,
                             Length) == Length)
        {
            return SdHeader;
        }
    }

    return NULL;
}

static
PSECURITY_DESCRIPTOR_HEADER
ObpAllocateCachedSecurityDescriptor(
    _In_reads_bytes_(Length) PSECURITY_DESCRIPTOR SecurityDescriptor,
    _In_ ULONG Length,
    _In_ ULONG FullHash,
    _In_ ULONG RefCount)
{
    PSECURITY_DESCRIPTOR_HEADER SdHeader;

    SdHeader = ExAllocatePoolWithTag(PagedPool,
                                     FIELD_OFFSET(SECURITY_DESCRIPTOR_HEADER,
                                                  SecurityDescriptor) + Length,
                                     TAG_OB_SD_CACHE);
    if (SdHeader == NULL) return NULL;

    SdHeader->RefCount = RefCount;
    SdHeader->FullHash = FullHash;
    RtlCopyMemory(&SdHeader->SecurityDescriptor, SecurityDescriptor, Length);

    return SdHeader;
}

CODE_SEG("INIT")
NTSTATUS
NTAPI
ObpInitSdCache(VOID)
{
    ULONG Index;

    for (Index = 0; Index < OBP_SD_CACHE_BUCKETS; Index++)
    {
        InitializeListHead(&ObpSdCache[Index].Head);
        ExInitializePushLock(&ObpSdCache[Index].PushLock);
    }

    return STATUS_SUCCESS;
}

PSECURITY_DESCRIPTOR
NTAPI
ObpReferenceSecurityDescriptor(
    _In_ POBJECT_HEADER ObjectHeader)
{
    PEX_FAST_REF FastRef = (PEX_FAST_REF)&ObjectHeader->SecurityDescriptor;
    PSECURITY_DESCRIPTOR SecurityDescriptor;
    PSECURITY_DESCRIPTOR_HEADER SdHeader;
    EX_FAST_REF OldRef;

    /* Try to take one of the references cached in the pointer itself */
    OldRef = ExAcquireFastReference(FastRef);
    SecurityDescriptor = ExGetObjectFastReference(OldRef);

    if (ExGetCountFastReference(OldRef) != 0)
    {
        ASSERT(SecurityDescriptor != NULL);
        SdHeader = ObpGetHeaderForSd(SecurityDescriptor);

        /* We took the last cached reference: charge a new batch and hand it to
         * the pointer so that the next caller stays on this path */
        if (ExGetCountFastReference(OldRef) == 1)
        {
            InterlockedExchangeAdd((PLONG)&SdHeader->RefCount, MAX_FAST_REFS);

            if (!ExInsertFastReference(FastRef, SecurityDescriptor))
            {
                /* The descriptor was refilled or replaced behind our back. Our
                 * own reference is still outstanding, so this cannot be last */
                InterlockedExchangeAdd((PLONG)&SdHeader->RefCount, -(LONG)MAX_FAST_REFS);
            }
        }

        return SecurityDescriptor;
    }

    /* The cache was empty. Serialize against whoever may be replacing the
     * descriptor and charge a reference of our own */
    ObpAcquireObjectLockShared(ObjectHeader);

    SecurityDescriptor = ExGetObjectFastReference(*FastRef);
    if (SecurityDescriptor != NULL)
    {
        SdHeader = ObpGetHeaderForSd(SecurityDescriptor);
        InterlockedIncrement((PLONG)&SdHeader->RefCount);
    }

    ObpReleaseObjectLock(ObjectHeader);

    return SecurityDescriptor;
}

/* PUBLIC FUNCTIONS ***********************************************************/

NTSTATUS
NTAPI
ObLogSecurityDescriptor(
    _In_ PSECURITY_DESCRIPTOR InputSecurityDescriptor,
    _Out_ PSECURITY_DESCRIPTOR *OutputSecurityDescriptor,
    _In_ ULONG RefBias)
{
    PSECURITY_DESCRIPTOR SelfRelative = InputSecurityDescriptor;
    PSECURITY_DESCRIPTOR Converted = NULL;
    PSECURITY_DESCRIPTOR_HEADER SdHeader;
    POB_SD_CACHE_LIST Bucket;
    NTSTATUS Status;
    ULONG Length;
    ULONG FullHash;

    PAGED_CODE();

    /* The cached copy outlives the caller's buffers, so it has to be
     * self-relative. Convert an absolute descriptor into a temporary one */
    if (!(((PISECURITY_DESCRIPTOR)InputSecurityDescriptor)->Control & SE_SELF_RELATIVE))
    {
        Length = 0;
        Status = RtlAbsoluteToSelfRelativeSD(InputSecurityDescriptor, NULL, &Length);
        if (Status != STATUS_BUFFER_TOO_SMALL)
        {
            return NT_SUCCESS(Status) ? STATUS_INVALID_SECURITY_DESCR : Status;
        }

        Converted = ExAllocatePoolWithTag(PagedPool, Length, TAG_OB_SD_CACHE);
        if (Converted == NULL) return STATUS_INSUFFICIENT_RESOURCES;

        Status = RtlAbsoluteToSelfRelativeSD(InputSecurityDescriptor, Converted, &Length);
        if (!NT_SUCCESS(Status))
        {
            ExFreePoolWithTag(Converted, TAG_OB_SD_CACHE);
            return Status;
        }

        SelfRelative = Converted;
    }

    Length = RtlLengthSecurityDescriptor(SelfRelative);
    FullHash = ObpHashSecurityDescriptor(SelfRelative, Length);
    Bucket = ObpSdCacheBucket(FullHash);

    /* Fast path: an identical descriptor is already cached */
    ObpAcquireSdCacheBucket(Bucket);
    SdHeader = ObpFindCachedSecurityDescriptor(Bucket, FullHash, SelfRelative, Length);
    if (SdHeader != NULL)
    {
        InterlockedExchangeAdd((PLONG)&SdHeader->RefCount, RefBias);
    }
    ObpReleaseSdCacheBucket(Bucket);

    if (SdHeader == NULL)
    {
        PSECURITY_DESCRIPTOR_HEADER NewHeader;

        /* Build the entry outside the bucket lock, then look again: another
         * thread may have cached the same descriptor meanwhile */
        NewHeader = ObpAllocateCachedSecurityDescriptor(SelfRelative,
                                                        Length,
                                                        FullHash,
                                                        RefBias);
        if (NewHeader == NULL)
        {
            if (Converted != NULL) ExFreePoolWithTag(Converted, TAG_OB_SD_CACHE);
            return STATUS_INSUFFICIENT_RESOURCES;
        }

        ObpAcquireSdCacheBucket(Bucket);
        SdHeader = ObpFindCachedSecurityDescriptor(Bucket, FullHash, SelfRelative, Length);
        if (SdHeader != NULL)
        {
            InterlockedExchangeAdd((PLONG)&SdHeader->RefCount, RefBias);
        }
        else
        {
            InsertHeadList(&Bucket->Head, &NewHeader->Link);
            SdHeader = NewHeader;
            NewHeader = NULL;
        }
        ObpReleaseSdCacheBucket(Bucket);

        if (NewHeader != NULL) ExFreePoolWithTag(NewHeader, TAG_OB_SD_CACHE);
    }

    if (Converted != NULL) ExFreePoolWithTag(Converted, TAG_OB_SD_CACHE);

    *OutputSecurityDescriptor = &SdHeader->SecurityDescriptor;
    return STATUS_SUCCESS;
}

VOID
NTAPI
ObReferenceSecurityDescriptor(
    _In_ PSECURITY_DESCRIPTOR SecurityDescriptor,
    _In_ ULONG Count)
{
    PSECURITY_DESCRIPTOR_HEADER SdHeader = ObpGetHeaderForSd(SecurityDescriptor);

    ASSERT(SdHeader->RefCount != 0);
    InterlockedExchangeAdd((PLONG)&SdHeader->RefCount, Count);
}

VOID
NTAPI
ObDereferenceSecurityDescriptor(
    _Inout_ PSECURITY_DESCRIPTOR SecurityDescriptor,
    _In_ ULONG Count)
{
    PSECURITY_DESCRIPTOR_HEADER SdHeader = ObpGetHeaderForSd(SecurityDescriptor);
    POB_SD_CACHE_LIST Bucket = ObpSdCacheBucket(SdHeader->FullHash);
    BOOLEAN Destroy;

    PAGED_CODE();
    ASSERT(SdHeader->RefCount >= Count);

    /* The bucket lock keeps a concurrent ObLogSecurityDescriptor from handing
     * this entry out while its last reference is going away */
    ObpAcquireSdCacheBucket(Bucket);
    Destroy = (InterlockedExchangeAdd((PLONG)&SdHeader->RefCount,
                                      -(LONG)Count) == (LONG)Count);
    if (Destroy) RemoveEntryList(&SdHeader->Link);
    ObpReleaseSdCacheBucket(Bucket);

    if (Destroy) ExFreePoolWithTag(SdHeader, TAG_OB_SD_CACHE);
}

/* EOF */
