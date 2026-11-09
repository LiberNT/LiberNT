/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Object manager directories
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

/* GLOBALS ********************************************************************/

POBJECT_TYPE ObpDirectoryObjectType = NULL;

/* PRIVATE FUNCTIONS **********************************************************/

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

    if (Directory->NamespaceEntry) ObpRemovePrivateNamespace(Directory);

    ASSERT(Directory->DeviceMap == NULL);
    for (Index = 0; Index < NUMBER_HASH_BUCKETS; Index++)
    {
        ASSERT(Directory->HashBuckets[Index] == NULL);
    }
}

/*
 * The namespace hash is case blind and its bucket order is what an enumeration
 * of a directory reports, so it is part of the directory's observable shape.
 */
static
ULONG
ObpDirectoryHash(
    _In_ PCUNICODE_STRING Name)
{
    PWSTR Buffer = Name->Buffer;
    LONG Remaining = Name->Length / sizeof(WCHAR);
    ULONG HashValue = 0;
    WCHAR CurrentChar;

    while (Remaining-- > 0)
    {
        CurrentChar = *Buffer++;

        HashValue += (HashValue << 1) + (HashValue >> 1);

        if (CurrentChar < 'a') HashValue += CurrentChar;
        else if (CurrentChar > 'z') HashValue += RtlUpcaseUnicodeChar(CurrentChar);
        else HashValue += (CurrentChar - ('a' - 'A'));
    }

    return HashValue;
}

BOOLEAN
NTAPI
ObpInsertEntryDirectory(
    _In_ POBJECT_DIRECTORY Parent,
    _In_ POBP_LOOKUP_CONTEXT Context,
    _In_ POBJECT_HEADER ObjectHeader)
{
    POBJECT_DIRECTORY_ENTRY *Bucket;
    POBJECT_DIRECTORY_ENTRY NewEntry;

    ASSERT(ObjectHeader->InfoMask & OBP_NAME_INFO_MASK);

    /* The context must be the one a failed lookup left behind */
    if (Context->Object != NULL ||
        !Context->DirectoryLocked ||
        Parent != Context->Directory)
    {
        DPRINT1("OB: ObpInsertEntryDirectory - invalid context %p %u\n",
                Context, Context->DirectoryLocked);
        ASSERT(FALSE);
        return FALSE;
    }

    NewEntry = ExAllocatePoolWithTag(PagedPool,
                                     sizeof(OBJECT_DIRECTORY_ENTRY),
                                     OB_DIR_TAG);
    if (NewEntry == NULL) return FALSE;

    NewEntry->HashValue = Context->HashValue;
    NewEntry->Object = &ObjectHeader->Body;

    Bucket = &Parent->HashBuckets[Context->HashIndex];
    NewEntry->ChainLink = *Bucket;
    *Bucket = NewEntry;

    OBJECT_HEADER_TO_NAME_INFO(ObjectHeader)->Directory = Parent;
    return TRUE;
}

POBJECT_DIRECTORY
NTAPI
ObpGetShadowDirectory(
    _In_ POBJECT_DIRECTORY Directory)
{
    PDEVICE_MAP SystemDeviceMap = ObSystemDeviceMap;
    POBJECT_DIRECTORY GlobalDosDirectory;

    if (*(PDEVICE_MAP volatile *)&Directory->DeviceMap == NULL ||
        SystemDeviceMap == NULL)
    {
        return NULL;
    }

    GlobalDosDirectory = SystemDeviceMap->DosDevicesDirectory;
    return (Directory != GlobalDosDirectory) ? GlobalDosDirectory : NULL;
}

/*
 * Walks one directory's bucket. On a hit the directory is still locked, so the
 * caller can reference what it found; on a miss the lock this call took is
 * already gone.
 */
static
PVOID
ObpSearchDirectoryBucket(
    _In_ POBJECT_DIRECTORY Directory,
    _In_ PUNICODE_STRING Name,
    _In_ ULONG HashValue,
    _In_ ULONG HashIndex,
    _In_ BOOLEAN CaseInsensitive,
    _In_ POBP_LOOKUP_CONTEXT Context)
{
    POBJECT_DIRECTORY_ENTRY *Bucket = &Directory->HashBuckets[HashIndex];
    POBJECT_DIRECTORY_ENTRY *Link = Bucket;
    POBJECT_HEADER_NAME_INFO HeaderNameInfo;
    POBJECT_DIRECTORY_ENTRY Entry;
    POBJECT_HEADER ObjectHeader;

    if (!Context->DirectoryLocked) ObpAcquireDirectoryLockShared(Directory, Context);

    while ((Entry = *Link) != NULL)
    {
        if (Entry->HashValue == HashValue)
        {
            ObjectHeader = OBJECT_TO_OBJECT_HEADER(Entry->Object);
            ASSERT(ObjectHeader->InfoMask & OBP_NAME_INFO_MASK);
            HeaderNameInfo = OBJECT_HEADER_TO_NAME_INFO(ObjectHeader);

            if (Name->Length == HeaderNameInfo->Name.Length &&
                RtlEqualUnicodeString(Name, &HeaderNameInfo->Name, CaseInsensitive))
            {
                break;
            }
        }

        Link = &Entry->ChainLink;
    }

    if (Entry == NULL)
    {
        if (!Context->DirectoryLocked) ObpReleaseDirectoryLock(Directory, Context);
        return NULL;
    }

    /* Move the hit to the front so the next lookup of the same name is cheap */
    if (Link != Bucket &&
        (Context->DirectoryLocked ||
         ExConvertPushLockSharedToExclusive(&Directory->Lock)))
    {
        *Link = Entry->ChainLink;
        Entry->ChainLink = *Bucket;
        *Bucket = Entry;
    }

    return Entry->Object;
}

PVOID
NTAPI
ObpLookupEntryDirectory(
    _In_ POBJECT_DIRECTORY Directory,
    _In_ PUNICODE_STRING Name,
    _In_ ULONG Attributes,
    _In_ BOOLEAN SearchShadow,
    _In_ POBP_LOOKUP_CONTEXT Context)
{
    BOOLEAN CaseInsensitive = ((Attributes & OBJ_CASE_INSENSITIVE) != 0);
    POBJECT_DIRECTORY ShadowDirectory;
    PVOID FoundObject = NULL;
    ULONG HashValue;
    ULONG HashIndex;

    PAGED_CODE();

    if (ObpLUIDDeviceMapsEnabled == 0) SearchShadow = FALSE;

    if (Directory != NULL && Name != NULL &&
        Name->Buffer != NULL && Name->Length != 0)
    {
        HashValue = ObpDirectoryHash(Name);
        HashIndex = HashValue % NUMBER_HASH_BUCKETS;

        Context->HashValue = HashValue;
        Context->HashIndex = (USHORT)HashIndex;

        for (;;)
        {
            FoundObject = ObpSearchDirectoryBucket(Directory,
                                                   Name,
                                                   HashValue,
                                                   HashIndex,
                                                   CaseInsensitive,
                                                   Context);
            if (FoundObject != NULL) break;

            /* A LUID mapped DOS directory falls back to the global one */
            if (!SearchShadow || Directory->DeviceMap == NULL) break;

            ShadowDirectory = ObpGetShadowDirectory(Directory);
            if (ShadowDirectory == NULL) break;

            Directory = ShadowDirectory;
        }
    }

    if (FoundObject != NULL)
    {
        ObpReferenceNameInfo(OBJECT_TO_OBJECT_HEADER(FoundObject));
        ObReferenceObject(FoundObject);

        if (!Context->DirectoryLocked) ObpReleaseDirectoryLock(Directory, Context);
    }

    ObpReleaseLookupContextObject(Context);
    Context->Object = FoundObject;

    return FoundObject;
}

BOOLEAN
NTAPI
ObpDeleteEntryDirectory(
    _In_ POBP_LOOKUP_CONTEXT Context)
{
    POBJECT_DIRECTORY Directory = Context->Directory;
    POBJECT_DIRECTORY_ENTRY *Bucket;
    POBJECT_DIRECTORY_ENTRY Entry;

    if (Directory == NULL) return FALSE;

    /* The lookup that filled the context left the entry at the front */
    Bucket = &Directory->HashBuckets[Context->HashIndex];
    Entry = *Bucket;

    *Bucket = Entry->ChainLink;
    Entry->ChainLink = NULL;

    ExFreePoolWithTag(Entry, OB_DIR_TAG);
    return TRUE;
}

/* SYSTEM CALLS ***************************************************************/

NTSTATUS
NTAPI
NtOpenDirectoryObject(
    _Out_ PHANDLE DirectoryHandle,
    _In_ ACCESS_MASK DesiredAccess,
    _In_ POBJECT_ATTRIBUTES ObjectAttributes)
{
    KPROCESSOR_MODE PreviousMode = ExGetPreviousMode();
    HANDLE Directory;
    NTSTATUS Status;

    PAGED_CODE();

    if (PreviousMode != KernelMode)
    {
        _SEH2_TRY
        {
            ProbeForWriteHandle(DirectoryHandle);
            *DirectoryHandle = NULL;
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
                                &Directory);
    if (!NT_SUCCESS(Status)) return Status;

    _SEH2_TRY
    {
        *DirectoryHandle = Directory;
    }
    _SEH2_EXCEPT(ExSystemExceptionFilter())
    {
        Status = _SEH2_GetExceptionCode();
    }
    _SEH2_END;

    return Status;
}

/*
 * Copies the names gathered in the scratch buffer behind the entry array and
 * repoints every string at where it will land in the caller's buffer.
 */
static
VOID
ObpPackDirectoryNames(
    _In_ PVOID LocalBuffer,
    _In_ PVOID UserBuffer,
    _In_ POBJECT_DIRECTORY_INFORMATION Entries,
    _In_ ULONG Count)
{
    POBJECT_DIRECTORY_INFORMATION Entry = LocalBuffer;
    PWSTR Target = (PWSTR)Entries;

    while (Count-- != 0)
    {
        RtlCopyMemory(Target, Entry->Name.Buffer, Entry->Name.Length);
        Entry->Name.Buffer = (PWSTR)((ULONG_PTR)UserBuffer +
                                     ((ULONG_PTR)Target - (ULONG_PTR)LocalBuffer));
        Target = (PWSTR)((ULONG_PTR)Target + Entry->Name.Length);
        *Target++ = UNICODE_NULL;

        RtlCopyMemory(Target, Entry->TypeName.Buffer, Entry->TypeName.Length);
        Entry->TypeName.Buffer = (PWSTR)((ULONG_PTR)UserBuffer +
                                         ((ULONG_PTR)Target - (ULONG_PTR)LocalBuffer));
        Target = (PWSTR)((ULONG_PTR)Target + Entry->TypeName.Length);
        *Target++ = UNICODE_NULL;

        Entry++;
    }
}

NTSTATUS
NTAPI
NtQueryDirectoryObject(
    _In_ HANDLE DirectoryHandle,
    _Out_writes_bytes_opt_(BufferLength) PVOID Buffer,
    _In_ ULONG BufferLength,
    _In_ BOOLEAN ReturnSingleEntry,
    _In_ BOOLEAN RestartScan,
    _Inout_ PULONG Context,
    _Out_opt_ PULONG ReturnLength)
{
    KPROCESSOR_MODE PreviousMode = ExGetPreviousMode();
    POBJECT_DIRECTORY_INFORMATION DirectoryInfo;
    POBJECT_HEADER_NAME_INFO ObjectNameInfo;
    POBJECT_DIRECTORY_ENTRY Entry;
    POBJECT_DIRECTORY Directory;
    OBP_LOOKUP_CONTEXT LookupContext;
    POBJECT_HEADER ObjectHeader;
    POBJECT_TYPE ObjectType;
    ULONG SkipEntries = 0;
    ULONG Length, TotalLength;
    ULONG Count, CurrentEntry;
    UNICODE_STRING Name;
    PVOID LocalBuffer;
    NTSTATUS Status;
    BOOLEAN Done;
    ULONG Hash;

    PAGED_CODE();

    ObpInitializeLookupContext(&LookupContext);

    if (PreviousMode != KernelMode)
    {
        _SEH2_TRY
        {
            ProbeForWrite(Buffer, BufferLength, sizeof(WCHAR));
            ProbeForWriteUlong(Context);
            if (!RestartScan) SkipEntries = *Context;
            if (ReturnLength) ProbeForWriteUlong(ReturnLength);
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            _SEH2_YIELD(return _SEH2_GetExceptionCode());
        }
        _SEH2_END;
    }
    else if (!RestartScan)
    {
        SkipEntries = *Context;
    }

    /* The entries are gathered privately: the directory lock may not be held
     * across a fault on the caller's buffer */
    LocalBuffer = ExAllocatePoolWithTag(PagedPool,
                                        sizeof(OBJECT_DIRECTORY_INFORMATION) +
                                        BufferLength,
                                        OB_NAME_TAG);
    if (LocalBuffer == NULL) return STATUS_INSUFFICIENT_RESOURCES;
    RtlZeroMemory(LocalBuffer, BufferLength);

    Status = ObReferenceObjectByHandle(DirectoryHandle,
                                       DIRECTORY_QUERY,
                                       ObpDirectoryObjectType,
                                       PreviousMode,
                                       (PVOID *)&Directory,
                                       NULL);
    if (!NT_SUCCESS(Status))
    {
        ExFreePoolWithTag(LocalBuffer, OB_NAME_TAG);
        return Status;
    }

    ObpAcquireDirectoryLockShared(Directory, &LookupContext);

    DirectoryInfo = LocalBuffer;
    TotalLength = sizeof(OBJECT_DIRECTORY_INFORMATION);
    Count = 0;
    CurrentEntry = 0;
    Done = FALSE;
    Status = STATUS_NO_MORE_ENTRIES;

    for (Hash = 0; Hash < NUMBER_HASH_BUCKETS && !Done; Hash++)
    {
        for (Entry = Directory->HashBuckets[Hash]; Entry != NULL; Entry = Entry->ChainLink)
        {
            if (SkipEntries != CurrentEntry++) continue;

            ObjectHeader = OBJECT_TO_OBJECT_HEADER(Entry->Object);
            ObjectNameInfo = OBJECT_HEADER_TO_NAME_INFO(ObjectHeader);
            ObjectType = ObpGetObjectTypeFromHeader(ObjectHeader);

            if (ObjectNameInfo != NULL)
                Name = ObjectNameInfo->Name;
            else
                RtlInitEmptyUnicodeString(&Name, NULL, 0);

            Length = sizeof(OBJECT_DIRECTORY_INFORMATION) +
                     Name.Length + sizeof(UNICODE_NULL) +
                     ObjectType->Name.Length + sizeof(UNICODE_NULL);

            if ((TotalLength + Length) > BufferLength)
            {
                if (ReturnSingleEntry)
                {
                    TotalLength += Length;
                    Status = STATUS_BUFFER_TOO_SMALL;
                }
                else
                {
                    Status = STATUS_MORE_ENTRIES;
                }

                CurrentEntry--;
                Done = TRUE;
                break;
            }

            DirectoryInfo->Name.Length = Name.Length;
            DirectoryInfo->Name.MaximumLength = Name.Length + sizeof(UNICODE_NULL);
            DirectoryInfo->Name.Buffer = Name.Buffer;
            DirectoryInfo->TypeName.Length = ObjectType->Name.Length;
            DirectoryInfo->TypeName.MaximumLength = ObjectType->Name.Length +
                                                    sizeof(UNICODE_NULL);
            DirectoryInfo->TypeName.Buffer = ObjectType->Name.Buffer;

            Status = STATUS_SUCCESS;

            TotalLength += Length;
            DirectoryInfo++;
            Count++;

            if (ReturnSingleEntry)
            {
                Done = TRUE;
                break;
            }

            SkipEntries++;
        }
    }

    if (NT_SUCCESS(Status))
    {
        /* The array is closed by an empty entry, and the names follow it */
        RtlZeroMemory(DirectoryInfo, sizeof(OBJECT_DIRECTORY_INFORMATION));
        DirectoryInfo++;

        ObpPackDirectoryNames(LocalBuffer, Buffer, DirectoryInfo, Count);

        *Context = CurrentEntry;
    }

    _SEH2_TRY
    {
        RtlCopyMemory(Buffer,
                      LocalBuffer,
                      (TotalLength <= BufferLength) ? TotalLength : BufferLength);

        if (ReturnLength) *ReturnLength = TotalLength;
    }
    _SEH2_EXCEPT(ExSystemExceptionFilter())
    {
        Status = _SEH2_GetExceptionCode();
    }
    _SEH2_END;

    ObpReleaseDirectoryLock(Directory, &LookupContext);

    ObDereferenceObject(Directory);
    ExFreePoolWithTag(LocalBuffer, OB_NAME_TAG);

    return Status;
}

NTSTATUS
NTAPI
NtCreateDirectoryObject(
    _Out_ PHANDLE DirectoryHandle,
    _In_ ACCESS_MASK DesiredAccess,
    _In_ POBJECT_ATTRIBUTES ObjectAttributes)
{
    KPROCESSOR_MODE PreviousMode = ExGetPreviousMode();
    POBJECT_DIRECTORY Directory;
    HANDLE NewHandle = NULL;
    NTSTATUS Status;

    PAGED_CODE();

    if (PreviousMode != KernelMode)
    {
        _SEH2_TRY
        {
            ProbeForWriteHandle(DirectoryHandle);
            *DirectoryHandle = NULL;
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

    RtlZeroMemory(Directory, sizeof(OBJECT_DIRECTORY));
    ExInitializePushLock(&Directory->Lock);
    Directory->SessionId = -1;

    Status = ObInsertObject((PVOID)Directory,
                            NULL,
                            DesiredAccess,
                            0,
                            NULL,
                            &NewHandle);

    _SEH2_TRY
    {
        *DirectoryHandle = NewHandle;
    }
    _SEH2_EXCEPT(ExSystemExceptionFilter())
    {
        Status = _SEH2_GetExceptionCode();
    }
    _SEH2_END;

    return Status;
}

/* EOF */
