/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Object directories
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

/* GLOBALS ********************************************************************/

POBJECT_TYPE ObpDirectoryObjectType = NULL;

/* PRIVATE FUNCTIONS **********************************************************/

static
BOOLEAN
ObpDirectoryIsCaseInsensitive(
    _In_ ULONG Attributes)
{
    return ((Attributes & OBJ_CASE_INSENSITIVE) != 0) || (ObpCaseInsensitive != 0);
}

/*
 * A LUID device map directory reaches the global names through the system map,
 * so a name it does not define is looked for there as well.
 */
static
POBJECT_DIRECTORY
ObpGetShadowDirectory(
    _In_ POBJECT_DIRECTORY Directory)
{
    PDEVICE_MAP DeviceMap = Directory->DeviceMap;

    if (DeviceMap == NULL) return NULL;
    if (DeviceMap == ObSystemDeviceMap) return NULL;

    return DeviceMap->GlobalDosDevicesDirectory;
}

/* Points the context at the directory it is about to search, moving the lock
 * if it is still holding a different one. */
static
VOID
ObpPointContextAt(
    _Inout_ POBP_LOOKUP_CONTEXT Context,
    _In_ POBJECT_DIRECTORY Directory)
{
    if (Context->DirectoryLocked && Context->Directory == Directory) return;

    if (Context->DirectoryLocked && Context->Directory != NULL)
        ObpReleaseDirectoryLock(Context->Directory, Context);

    ObpAcquireDirectoryLockShared(Directory, Context);
    Context->Directory = Directory;
    Context->DirectoryLocked = TRUE;
}

static
PVOID
ObpSearchDirectoryBucket(
    _In_ POBJECT_DIRECTORY Directory,
    _In_ PUNICODE_STRING Name,
    _In_ BOOLEAN CaseInsensitive,
    _Inout_ POBP_LOOKUP_CONTEXT Context)
{
    POBJECT_DIRECTORY_ENTRY Entry;
    POBJECT_HEADER_NAME_INFO NameInfo;
    POBJECT_HEADER Header;
    ULONG Hash;

    /* One entry carries one hash, so it is always the case-insensitive one:
     * a caller that wants an exact match gets it from the comparison, not
     * from landing in a different bucket */
    if (!NT_SUCCESS(RtlHashUnicodeString(Name,
                                         TRUE,
                                         HASH_STRING_ALGORITHM_X65599,
                                         &Hash)))
    {
        return NULL;
    }

    Context->HashValue = Hash;
    Context->HashIndex = (USHORT)(Hash % NUMBER_HASH_BUCKETS);

    for (Entry = Directory->HashBuckets[Context->HashIndex];
         Entry != NULL;
         Entry = Entry->ChainLink)
    {
        if (Entry->HashValue != Hash) continue;

        Header = OBJECT_TO_OBJECT_HEADER(Entry->Object);
        NameInfo = OBJECT_HEADER_TO_NAME_INFO(Header);
        if (NameInfo == NULL) continue;

        if (!RtlEqualUnicodeString(Name, &NameInfo->Name, CaseInsensitive))
            continue;

        /* The context keeps the entry it found, so a deletion that follows
         * unlinks that one and not another of the same name */
        ObpReferenceNameInfo(Header);
        ObReferenceObject(Entry->Object);
        Context->Object = Entry->Object;

        return Entry->Object;
    }

    return NULL;
}

/* PRIVATE NAMESPACE INTERFACE ************************************************/

/*
 * Finds one named entry of a directory. The caller either comes in holding the
 * directory exclusively, in which case the lock is left alone and a miss leaves
 * the context primed for an insertion, or comes in unlocked and the search
 * takes the lock shared and leaves it in the context.
 */
PVOID
NTAPI
ObpLookupEntryDirectory(
    _In_ POBJECT_DIRECTORY Directory,
    _In_ PUNICODE_STRING Name,
    _In_ ULONG Attributes,
    _In_ BOOLEAN SearchShadow,
    _In_ POBP_LOOKUP_CONTEXT Context)
{
    BOOLEAN CaseInsensitive = ObpDirectoryIsCaseInsensitive(Attributes);
    POBJECT_DIRECTORY Shadow;
    PVOID Object;

    ObpPointContextAt(Context, Directory);

    /* Whatever a previous search parked here is of no use to this one */
    ObpReleaseLookupContextObject(Context);

    Object = ObpSearchDirectoryBucket(Directory, Name, CaseInsensitive, Context);
    if (Object != NULL) return Object;

    if (!SearchShadow || ObpLUIDDeviceMapsEnabled == 0) return NULL;

    Shadow = ObpGetShadowDirectory(Directory);
    if (Shadow == NULL) return NULL;

    /* The shadow is searched under its own lock, and the context is left on it
     * because the caller has nothing more to do in the one that missed */
    ObpPointContextAt(Context, Shadow);

    return ObpSearchDirectoryBucket(Shadow, Name, CaseInsensitive, Context);
}

/*
 * Names an object in the directory the last lookup missed in, reusing the hash
 * and the bucket that lookup worked out.
 */
BOOLEAN
NTAPI
ObpInsertEntryDirectory(
    _In_ POBJECT_DIRECTORY Parent,
    _In_ POBP_LOOKUP_CONTEXT Context,
    _In_ POBJECT_HEADER ObjectHeader)
{
    POBJECT_HEADER_NAME_INFO NameInfo;
    POBJECT_DIRECTORY_ENTRY Entry;

    ASSERT(Context->DirectoryLocked);

    NameInfo = OBJECT_HEADER_TO_NAME_INFO(ObjectHeader);
    if (NameInfo == NULL) return FALSE;

    Entry = ExAllocatePoolWithTag(PagedPool, sizeof(*Entry), OB_DIR_TAG);
    if (Entry == NULL) return FALSE;

    Entry->Object = &ObjectHeader->Body;
    Entry->HashValue = Context->HashValue;
    Entry->ChainLink = Parent->HashBuckets[Context->HashIndex];
    Parent->HashBuckets[Context->HashIndex] = Entry;

    NameInfo->Directory = Parent;
    return TRUE;
}

/* Takes the entry the last lookup found back out of its directory. */
BOOLEAN
NTAPI
ObpDeleteEntryDirectory(
    _In_ POBP_LOOKUP_CONTEXT Context)
{
    POBJECT_DIRECTORY_ENTRY *Link;
    POBJECT_DIRECTORY_ENTRY Entry;

    ASSERT(Context->DirectoryLocked);

    if (Context->Directory == NULL) return FALSE;

    if (Context->Object == NULL) return FALSE;

    Link = &Context->Directory->HashBuckets[Context->HashIndex];
    while (*Link != NULL)
    {
        Entry = *Link;
        if (Entry->Object == Context->Object)
        {
            *Link = Entry->ChainLink;
            ExFreePoolWithTag(Entry, OB_DIR_TAG);
            return TRUE;
        }

        Link = &Entry->ChainLink;
    }

    return FALSE;
}

/* OBJECT TYPE METHODS ********************************************************/

VOID
NTAPI
ObpCloseDirectoryObject(
    _In_opt_ PEPROCESS Process,
    _In_ PVOID Object,
    _In_ ULONG_PTR ProcessHandleCount,
    _In_ ULONG_PTR SystemHandleCount)
{
    UNREFERENCED_PARAMETER(Process);
    UNREFERENCED_PARAMETER(Object);
    UNREFERENCED_PARAMETER(ProcessHandleCount);
    UNREFERENCED_PARAMETER(SystemHandleCount);
}

VOID
NTAPI
ObpDeleteDirectoryObject(
    _In_ PVOID Object)
{
    POBJECT_DIRECTORY Directory = Object;
    ULONG Index;

    if (Directory->NamespaceEntry)
        ObpRemovePrivateNamespace(Directory);

    ASSERT(Directory->DeviceMap == NULL);
    for (Index = 0; Index < NUMBER_HASH_BUCKETS; Index++)
    {
        ASSERT(Directory->HashBuckets[Index] == NULL);
    }
}

/* SYSTEM CALLS ***************************************************************/

NTSTATUS
NTAPI
NtCreateDirectoryObject(
    _Out_ PHANDLE DirectoryHandle,
    _In_ ACCESS_MASK DesiredAccess,
    _In_ POBJECT_ATTRIBUTES ObjectAttributes)
{
    KPROCESSOR_MODE PreviousMode = ExGetPreviousMode();
    POBJECT_DIRECTORY Directory;
    NTSTATUS Status;
    HANDLE Handle;

    PAGED_CODE();

    if (PreviousMode != KernelMode)
    {
        _SEH2_TRY
        {
            ProbeForWriteHandle(DirectoryHandle);
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            _SEH2_YIELD(return _SEH2_GetExceptionCode());
        }
        _SEH2_END;
    }

    Status = ObCreateObject(PreviousMode,
                            ObpDirectoryObjectType,
                            ObjectAttributes,
                            PreviousMode,
                            NULL,
                            sizeof(OBJECT_DIRECTORY),
                            0,
                            0,
                            (PVOID *)&Directory);
    if (!NT_SUCCESS(Status)) return Status;

    /* The buckets and the lock are the whole of a directory's own state */
    RtlZeroMemory(Directory, sizeof(OBJECT_DIRECTORY));
    ExInitializePushLock(&Directory->Lock);
    Directory->SessionId = MAXULONG;

    Status = ObInsertObject(Directory,
                            NULL,
                            DesiredAccess,
                            0,
                            NULL,
                            &Handle);
    if (!NT_SUCCESS(Status)) return Status;

    _SEH2_TRY
    {
        *DirectoryHandle = Handle;
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Status = _SEH2_GetExceptionCode();
    }
    _SEH2_END;

    return Status;
}

NTSTATUS
NTAPI
NtOpenDirectoryObject(
    _Out_ PHANDLE DirectoryHandle,
    _In_ ACCESS_MASK DesiredAccess,
    _In_ POBJECT_ATTRIBUTES ObjectAttributes)
{
    KPROCESSOR_MODE PreviousMode = ExGetPreviousMode();
    NTSTATUS Status;
    HANDLE Handle;

    PAGED_CODE();

    if (PreviousMode != KernelMode)
    {
        _SEH2_TRY
        {
            ProbeForWriteHandle(DirectoryHandle);
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            _SEH2_YIELD(return _SEH2_GetExceptionCode());
        }
        _SEH2_END;
    }

    Status = ObOpenObjectByName(ObjectAttributes,
                                ObpDirectoryObjectType,
                                PreviousMode,
                                NULL,
                                DesiredAccess,
                                NULL,
                                &Handle);

    /* The handle is reported whatever the outcome, so a caller that primed its
     * variable sees it cleared on failure */
    _SEH2_TRY
    {
        *DirectoryHandle = NT_SUCCESS(Status) ? Handle : NULL;
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Status = _SEH2_GetExceptionCode();
    }
    _SEH2_END;

    return Status;
}

typedef struct _OBP_DIRECTORY_SCAN
{
    POBJECT_DIRECTORY_INFORMATION Info;
    PUCHAR Strings;
    ULONG Left;
    ULONG Used;
    ULONG Needed;
    BOOLEAN Full;
} OBP_DIRECTORY_SCAN, *POBP_DIRECTORY_SCAN;

/*
 * Lays one entry into the caller's buffer: the descriptors grow from the front
 * and the two names from the back, with room kept for the empty descriptor
 * that ends the list.
 */
static
BOOLEAN
ObpReportDirectoryEntry(
    _Inout_ POBP_DIRECTORY_SCAN Scan,
    _In_ PVOID Object)
{
    POBJECT_HEADER Header = OBJECT_TO_OBJECT_HEADER(Object);
    POBJECT_HEADER_NAME_INFO NameInfo = OBJECT_HEADER_TO_NAME_INFO(Header);
    POBJECT_TYPE Type = ObpGetObjectTypeFromHeader(Header);
    ULONG Bytes;

    if (NameInfo == NULL) return TRUE;

    Bytes = 2 * sizeof(OBJECT_DIRECTORY_INFORMATION) +
            NameInfo->Name.Length + sizeof(UNICODE_NULL) +
            Type->Name.Length + sizeof(UNICODE_NULL);

    if (Bytes > Scan->Left)
    {
        Scan->Full = TRUE;
        if (Scan->Needed == 0) Scan->Needed = Scan->Used + Bytes;
        return FALSE;
    }

    Scan->Strings -= Type->Name.Length + sizeof(UNICODE_NULL);
    RtlCopyMemory(Scan->Strings, Type->Name.Buffer, Type->Name.Length);
    Scan->Info->TypeName.Buffer = (PWCHAR)Scan->Strings;
    Scan->Info->TypeName.Length = Type->Name.Length;
    Scan->Info->TypeName.MaximumLength = Type->Name.Length + sizeof(UNICODE_NULL);
    ((PWCHAR)Scan->Strings)[Type->Name.Length / sizeof(WCHAR)] = UNICODE_NULL;

    Scan->Strings -= NameInfo->Name.Length + sizeof(UNICODE_NULL);
    RtlCopyMemory(Scan->Strings, NameInfo->Name.Buffer, NameInfo->Name.Length);
    Scan->Info->Name.Buffer = (PWCHAR)Scan->Strings;
    Scan->Info->Name.Length = NameInfo->Name.Length;
    Scan->Info->Name.MaximumLength = NameInfo->Name.Length + sizeof(UNICODE_NULL);
    ((PWCHAR)Scan->Strings)[NameInfo->Name.Length / sizeof(WCHAR)] = UNICODE_NULL;

    Scan->Info++;
    Scan->Left -= Bytes;
    Scan->Used += Bytes;

    return TRUE;
}

NTSTATUS
NTAPI
NtQueryDirectoryObject(
    _In_ HANDLE DirectoryHandle,
    _Out_ PVOID Buffer,
    _In_ ULONG BufferLength,
    _In_ BOOLEAN ReturnSingleEntry,
    _In_ BOOLEAN RestartScan,
    _Inout_ PULONG Context,
    _Out_opt_ PULONG ReturnLength)
{
    KPROCESSOR_MODE PreviousMode = ExGetPreviousMode();
    POBJECT_DIRECTORY_ENTRY Entry;
    POBJECT_DIRECTORY Directory;
    OBP_LOOKUP_CONTEXT Lookup;
    OBP_DIRECTORY_SCAN Scan;
    ULONG Skip, Index, Bucket;
    NTSTATUS Status;

    PAGED_CODE();

    if (PreviousMode != KernelMode)
    {
        _SEH2_TRY
        {
            ProbeForWrite(Buffer, BufferLength, sizeof(ULONG));
            ProbeForWriteUlong(Context);
            if (ReturnLength != NULL) ProbeForWriteUlong(ReturnLength);
            Skip = RestartScan ? 0 : *Context;
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            _SEH2_YIELD(return _SEH2_GetExceptionCode());
        }
        _SEH2_END;
    }
    else
    {
        Skip = RestartScan ? 0 : *Context;
    }

    Status = ObReferenceObjectByHandle(DirectoryHandle,
                                       DIRECTORY_QUERY,
                                       ObpDirectoryObjectType,
                                       PreviousMode,
                                       (PVOID *)&Directory,
                                       NULL);
    if (!NT_SUCCESS(Status)) return Status;

    RtlZeroMemory(&Scan, sizeof(Scan));
    Scan.Info = Buffer;
    Scan.Strings = (PUCHAR)Buffer + BufferLength;
    Scan.Left = BufferLength;

    ObpInitializeLookupContext(&Lookup);
    ObpAcquireDirectoryLockShared(Directory, &Lookup);

    Index = 0;
    for (Bucket = 0; Bucket < NUMBER_HASH_BUCKETS && !Scan.Full; Bucket++)
    {
        for (Entry = Directory->HashBuckets[Bucket];
             Entry != NULL;
             Entry = Entry->ChainLink)
        {
            Index++;
            if (Index <= Skip) continue;

            if (!ObpReportDirectoryEntry(&Scan, Entry->Object))
            {
                Index--;
                break;
            }

            if (ReturnSingleEntry)
            {
                Scan.Full = TRUE;
                break;
            }
        }
    }

    _SEH2_TRY
    {
        /* An empty descriptor closes the list whenever anything was reported */
        if (Scan.Used != 0)
            RtlZeroMemory(Scan.Info, sizeof(OBJECT_DIRECTORY_INFORMATION));

        if (Scan.Used == 0)
        {
            Status = (Scan.Needed != 0) ? STATUS_BUFFER_TOO_SMALL :
                                          STATUS_NO_MORE_ENTRIES;
        }
        else if (ReturnSingleEntry || Index == Skip)
        {
            Status = STATUS_SUCCESS;
        }
        else
        {
            Status = Scan.Full ? STATUS_MORE_ENTRIES : STATUS_SUCCESS;
        }

        if (Scan.Used != 0) *Context = Index;

        if (ReturnLength != NULL)
        {
            *ReturnLength = (Scan.Needed != 0) ?
                            Scan.Needed :
                            Scan.Used + sizeof(OBJECT_DIRECTORY_INFORMATION);
        }
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Status = _SEH2_GetExceptionCode();
    }
    _SEH2_END;

    ObpReleaseDirectoryLock(Directory, &Lookup);
    ObDereferenceObject(Directory);

    return Status;
}

/* EOF */
