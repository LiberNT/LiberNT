/*
 * PROJECT:     LiberNT NT Virtual Memory Subsystem
 * LICENSE:     GPL-3.0-only (https://spdx.org/licenses/GPL-3.0-only)
 * PURPOSE:     Driver image paging controls and Driver Verifier queries
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <mm/nt/ntsysimg.h>
#define NDEBUG
#include <debug.h>

extern UCHAR MmDisablePagingExecutive;

MM_DRIVER_VERIFIER_DATA MmVerifierData;
PVOID MmTriageActionTaken;
PVOID KernelVerifier;

PVOID
NTAPI
MmPageEntireDriver(
    _In_ PVOID AddressWithinSection)
{
    PLDR_DATA_TABLE_ENTRY Entry;
    PVOID Base;

    PAGED_CODE();

    KeEnterCriticalRegion();
    ExAcquireResourceSharedLite(&PsLoadedModuleResource, TRUE);
    Entry = MiLookupDataTableEntry(AddressWithinSection);
    Base = Entry ? Entry->DllBase : NULL;
    ExReleaseResourceLite(&PsLoadedModuleResource);
    KeLeaveCriticalRegion();
    return Base;
}

VOID
NTAPI
MmResetDriverPaging(
    _In_ PVOID AddressWithinSection)
{
    PAGED_CODE();
    UNREFERENCED_PARAMETER(AddressWithinSection);
    ASSERT(MmDisablePagingExecutive);
}

PVOID
NTAPI
MmLockPageableDataSection(
    _In_ PVOID AddressWithinSection)
{
    PAGED_CODE();
    ASSERT(MmDisablePagingExecutive);
    return AddressWithinSection;
}

VOID
NTAPI
MmLockPageableSectionByHandle(
    _In_ PVOID ImageSectionHandle)
{
    PAGED_CODE();
    UNREFERENCED_PARAMETER(ImageSectionHandle);
    ASSERT(MmDisablePagingExecutive);
}

VOID
NTAPI
MmUnlockPageableImageSection(
    _In_ PVOID ImageSectionHandle)
{
    PAGED_CODE();
    UNREFERENCED_PARAMETER(ImageSectionHandle);
    ASSERT(MmDisablePagingExecutive);
}

ULONG
NTAPI
MmTrimAllSystemPageableMemory(
    _In_ ULONG PurgeTransitionList)
{
    UNREFERENCED_PARAMETER(PurgeTransitionList);
    UNIMPLEMENTED;
    return 0;
}

NTSTATUS
NTAPI
MmAddVerifierThunks(
    _In_reads_bytes_(ThunkBufferSize) PVOID ThunkBuffer,
    _In_ ULONG ThunkBufferSize)
{
    PAGED_CODE();
    UNREFERENCED_PARAMETER(ThunkBuffer);
    UNREFERENCED_PARAMETER(ThunkBufferSize);

    /* TODO: Driver Verifier is not implemented, so no driver can add thunks */
    return STATUS_NOT_SUPPORTED;
}

NTSTATUS
NTAPI
MmIsVerifierEnabled(
    _Out_ PULONG VerifierFlags)
{
    *VerifierFlags = 0;
    return STATUS_NOT_SUPPORTED;
}

LOGICAL
NTAPI
MmIsDriverVerifying(
    _In_ PDRIVER_OBJECT DriverObject)
{
    PLDR_DATA_TABLE_ENTRY Entry = (PLDR_DATA_TABLE_ENTRY)DriverObject->DriverSection;

    return (LOGICAL)(Entry != NULL && (Entry->Flags & LDRP_IMAGE_VERIFYING) != 0);
}

LOGICAL
NTAPI
MmIsDriverVerifyingByAddress(
    _In_ PVOID AddressWithinSection)
{
    UNREFERENCED_PARAMETER(AddressWithinSection);
    return FALSE;
}
