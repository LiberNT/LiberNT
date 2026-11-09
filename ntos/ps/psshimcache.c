/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Remembering which images the compatibility engine has already
 *              looked at, so the loader asks about each one once
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#include "pspriv.h"
#define NDEBUG
#include <debug.h>

/*
 * How many images are remembered. Beyond this the one used longest ago is
 * dropped, which is what keeps the cache useful without bounding what a
 * caller may ask about.
 */
#define PSP_SHIM_CACHE_ENTRIES 512

/* GLOBALS ********************************************************************/

typedef struct _PSP_SHIM_CACHE_ENTRY
{
    LIST_ENTRY Links;
    UNICODE_STRING ImageName;
    LARGE_INTEGER LastWriteTime;
    LARGE_INTEGER FileSize;
} PSP_SHIM_CACHE_ENTRY, *PPSP_SHIM_CACHE_ENTRY;

static LIST_ENTRY PspShimCacheList;
static ULONG PspShimCacheCount;
static ERESOURCE PspShimCacheLock;
static BOOLEAN PspShimCacheReady;

/* PRIVATE FUNCTIONS **********************************************************/

static
VOID
PspShimCacheAcquire(
    _In_ BOOLEAN Exclusive)
{
    KeEnterCriticalRegion();

    if (Exclusive) ExAcquireResourceExclusiveLite(&PspShimCacheLock, TRUE);
    else ExAcquireResourceSharedLite(&PspShimCacheLock, TRUE);
}

static
VOID
PspShimCacheRelease(VOID)
{
    ExReleaseResourceLite(&PspShimCacheLock);
    KeLeaveCriticalRegion();
}

static
VOID
PspShimCacheFreeEntry(
    _In_ PPSP_SHIM_CACHE_ENTRY Entry)
{
    if (Entry->ImageName.Buffer != NULL)
        ExFreePoolWithTag(Entry->ImageName.Buffer, TAG_SHIM);

    ExFreePoolWithTag(Entry, TAG_SHIM);
}

/*
 * Finds an image in the cache by name. The caller holds the lock, so what is
 * returned is good only while it does.
 */
static
PPSP_SHIM_CACHE_ENTRY
PspShimCacheFind(
    _In_ PCUNICODE_STRING ImageName)
{
    PLIST_ENTRY Link;

    for (Link = PspShimCacheList.Flink;
         Link != &PspShimCacheList;
         Link = Link->Flink)
    {
        PPSP_SHIM_CACHE_ENTRY Entry = CONTAINING_RECORD(Link,
                                                        PSP_SHIM_CACHE_ENTRY,
                                                        Links);

        if (RtlEqualUnicodeString(&Entry->ImageName, ImageName, TRUE))
            return Entry;
    }

    return NULL;
}

/*
 * Reads what identifies a version of an image: when it was last written and
 * how long it is. An image whose either changed is a different image as far
 * as compatibility is concerned, so the cache entry for the old one no
 * longer answers for it.
 */
static
NTSTATUS
PspShimCacheQueryImage(
    _In_ HANDLE ImageHandle,
    _Out_ PLARGE_INTEGER LastWriteTime,
    _Out_ PLARGE_INTEGER FileSize)
{
    FILE_NETWORK_OPEN_INFORMATION Information;
    IO_STATUS_BLOCK IoStatusBlock;
    NTSTATUS Status;

    PAGED_CODE();

    LastWriteTime->QuadPart = 0;
    FileSize->QuadPart = 0;

    Status = ZwQueryInformationFile(ImageHandle,
                                    &IoStatusBlock,
                                    &Information,
                                    sizeof(Information),
                                    FileNetworkOpenInformation);
    if (!NT_SUCCESS(Status)) return Status;

    *LastWriteTime = Information.LastWriteTime;
    *FileSize = Information.EndOfFile;

    return STATUS_SUCCESS;
}

/*
 * Copies the name a caller passed from its own memory into the kernel's. A
 * name is read once here and never read again, so a caller cannot change it
 * under the cache after it has been checked.
 */
static
NTSTATUS
PspShimCaptureServiceData(
    _In_ PAPPHELP_CACHE_SERVICE_LOOKUP ServiceData,
    _In_ KPROCESSOR_MODE PreviousMode,
    _Out_ PUNICODE_STRING ImageName,
    _Out_ PHANDLE ImageHandle)
{
    APPHELP_CACHE_SERVICE_LOOKUP Safe;
    PWCHAR Buffer;

    PAGED_CODE();

    RtlInitEmptyUnicodeString(ImageName, NULL, 0);
    *ImageHandle = NULL;

    if (PreviousMode != KernelMode)
    {
        _SEH2_TRY
        {
            ProbeForRead(ServiceData, sizeof(*ServiceData), sizeof(ULONG_PTR));
            Safe = *ServiceData;
            ProbeForRead(Safe.ImageName.Buffer,
                         Safe.ImageName.Length,
                         sizeof(WCHAR));
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            _SEH2_YIELD(return _SEH2_GetExceptionCode());
        }
        _SEH2_END;
    }
    else
    {
        Safe = *ServiceData;
    }

    if (Safe.ImageName.Length == 0 ||
        (Safe.ImageName.Length % sizeof(WCHAR)) != 0)
    {
        return STATUS_INVALID_PARAMETER;
    }

    Buffer = ExAllocatePoolWithTag(PagedPool, Safe.ImageName.Length, TAG_SHIM);
    if (Buffer == NULL) return STATUS_INSUFFICIENT_RESOURCES;

    if (PreviousMode != KernelMode)
    {
        _SEH2_TRY
        {
            RtlCopyMemory(Buffer, Safe.ImageName.Buffer, Safe.ImageName.Length);
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            ExFreePoolWithTag(Buffer, TAG_SHIM);
            _SEH2_YIELD(return _SEH2_GetExceptionCode());
        }
        _SEH2_END;
    }
    else
    {
        RtlCopyMemory(Buffer, Safe.ImageName.Buffer, Safe.ImageName.Length);
    }

    ImageName->Buffer = Buffer;
    ImageName->Length = Safe.ImageName.Length;
    ImageName->MaximumLength = Safe.ImageName.Length;
    *ImageHandle = Safe.ImageHandle;

    return STATUS_SUCCESS;
}

/* PUBLIC FUNCTIONS ***********************************************************/

CODE_SEG("INIT")
NTSTATUS
NTAPI
ApphelpCacheInitialize(VOID)
{
    NTSTATUS Status;

    PAGED_CODE();

    InitializeListHead(&PspShimCacheList);
    PspShimCacheCount = 0;

    Status = ExInitializeResourceLite(&PspShimCacheLock);
    if (!NT_SUCCESS(Status)) return Status;

    PspShimCacheReady = TRUE;
    return STATUS_SUCCESS;
}

VOID
NTAPI
ApphelpCacheShutdown(VOID)
{
    PAGED_CODE();

    if (!PspShimCacheReady) return;

    PspShimCacheAcquire(TRUE);

    while (!IsListEmpty(&PspShimCacheList))
    {
        PLIST_ENTRY Link = RemoveHeadList(&PspShimCacheList);

        PspShimCacheFreeEntry(CONTAINING_RECORD(Link,
                                                PSP_SHIM_CACHE_ENTRY,
                                                Links));
    }

    PspShimCacheCount = 0;
    PspShimCacheRelease();
}

/* SYSTEM CALLS ***************************************************************/

/*
 * The one call the compatibility engine makes into the kernel. Looking an
 * image up is open to any caller, because the loader does it for every image
 * it maps; changing what the cache says is not, because it decides whether
 * an image is given compatibility treatment.
 */
NTSTATUS
NTAPI
NtApphelpCacheControl(
    _In_ APPHELPCACHESERVICECLASS Service,
    _In_opt_ PAPPHELP_CACHE_SERVICE_LOOKUP ServiceData)
{
    KPROCESSOR_MODE PreviousMode = ExGetPreviousMode();
    PPSP_SHIM_CACHE_ENTRY Entry;
    LARGE_INTEGER LastWriteTime;
    LARGE_INTEGER FileSize;
    UNICODE_STRING ImageName;
    HANDLE ImageHandle;
    NTSTATUS Status;

    PAGED_CODE();

    if (!PspShimCacheReady) return STATUS_NOT_SUPPORTED;

    if (Service != ApphelpCacheServiceLookup &&
        !SeSinglePrivilegeCheck(SeTcbPrivilege, PreviousMode))
    {
        return STATUS_PRIVILEGE_NOT_HELD;
    }

    if (Service == ApphelpCacheServiceFlush)
    {
        ApphelpCacheShutdown();
        return STATUS_SUCCESS;
    }

    if (Service == ApphelpCacheServiceDump)
    {
        PspShimCacheAcquire(FALSE);
        DbgPrint("Shim cache holds %lu image(s)\n", PspShimCacheCount);
        PspShimCacheRelease();
        return STATUS_SUCCESS;
    }

    if (Service != ApphelpCacheServiceLookup &&
        Service != ApphelpCacheServiceRemove &&
        Service != ApphelpCacheServiceUpdate)
    {
        return STATUS_INVALID_PARAMETER;
    }

    if (ServiceData == NULL) return STATUS_INVALID_PARAMETER;

    Status = PspShimCaptureServiceData(ServiceData,
                                       PreviousMode,
                                       &ImageName,
                                       &ImageHandle);
    if (!NT_SUCCESS(Status)) return Status;

    if (Service == ApphelpCacheServiceRemove)
    {
        PspShimCacheAcquire(TRUE);

        Entry = PspShimCacheFind(&ImageName);
        if (Entry != NULL)
        {
            RemoveEntryList(&Entry->Links);
            PspShimCacheCount--;
        }

        PspShimCacheRelease();

        if (Entry != NULL) PspShimCacheFreeEntry(Entry);

        ExFreePoolWithTag(ImageName.Buffer, TAG_SHIM);
        return (Entry != NULL) ? STATUS_SUCCESS : STATUS_NOT_FOUND;
    }

    /*
     * Both of the remaining services need to know which version of the image
     * is being asked about, and an image the caller cannot describe is one
     * the cache cannot answer for.
     */
    Status = PspShimCacheQueryImage(ImageHandle, &LastWriteTime, &FileSize);
    if (!NT_SUCCESS(Status))
    {
        ExFreePoolWithTag(ImageName.Buffer, TAG_SHIM);
        return Status;
    }

    if (Service == ApphelpCacheServiceLookup)
    {
        PspShimCacheAcquire(TRUE);

        Entry = PspShimCacheFind(&ImageName);
        if (Entry != NULL)
        {
            if (Entry->LastWriteTime.QuadPart != LastWriteTime.QuadPart ||
                Entry->FileSize.QuadPart != FileSize.QuadPart)
            {
                /* The file on disk is no longer the one that was cached */
                RemoveEntryList(&Entry->Links);
                PspShimCacheCount--;
                PspShimCacheRelease();

                PspShimCacheFreeEntry(Entry);
                ExFreePoolWithTag(ImageName.Buffer, TAG_SHIM);
                return STATUS_NOT_FOUND;
            }

            /* Using an entry moves it to the front, so the front is the
             * most recently used and the back is what is dropped first */
            RemoveEntryList(&Entry->Links);
            InsertHeadList(&PspShimCacheList, &Entry->Links);
        }

        PspShimCacheRelease();

        ExFreePoolWithTag(ImageName.Buffer, TAG_SHIM);
        return (Entry != NULL) ? STATUS_SUCCESS : STATUS_NOT_FOUND;
    }

    Entry = ExAllocatePoolWithTag(PagedPool, sizeof(*Entry), TAG_SHIM);
    if (Entry == NULL)
    {
        ExFreePoolWithTag(ImageName.Buffer, TAG_SHIM);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    Entry->ImageName = ImageName;
    Entry->LastWriteTime = LastWriteTime;
    Entry->FileSize = FileSize;

    PspShimCacheAcquire(TRUE);

    /* An image already known is replaced rather than listed twice */
    {
        PPSP_SHIM_CACHE_ENTRY Old = PspShimCacheFind(&ImageName);

        if (Old != NULL)
        {
            RemoveEntryList(&Old->Links);
            PspShimCacheCount--;
            PspShimCacheFreeEntry(Old);
        }
    }

    InsertHeadList(&PspShimCacheList, &Entry->Links);
    PspShimCacheCount++;

    while (PspShimCacheCount > PSP_SHIM_CACHE_ENTRIES)
    {
        PLIST_ENTRY Link = RemoveTailList(&PspShimCacheList);

        PspShimCacheCount--;
        PspShimCacheFreeEntry(CONTAINING_RECORD(Link,
                                                PSP_SHIM_CACHE_ENTRY,
                                                Links));
    }

    PspShimCacheRelease();

    return STATUS_SUCCESS;
}
