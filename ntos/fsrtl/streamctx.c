/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     What a filter hangs off a stream, a file and a file object
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#include "fsrtlp.h"
#define NDEBUG
#include <debug.h>

/* PRIVATE DATA ***************************************************************/

#define FSRTL_CONTEXT_TAG 'cRsF'

typedef struct _FSRTL_CONTEXT_HEAD
{
    LIST_ENTRY Contexts;
} FSRTL_CONTEXT_HEAD, *PFSRTL_CONTEXT_HEAD;

typedef struct _FSRTL_FILE_OBJECT_CONTEXTS
{
    LIST_ENTRY Links;
    PFILE_OBJECT FileObject;
    LIST_ENTRY Contexts;
} FSRTL_FILE_OBJECT_CONTEXTS, *PFSRTL_FILE_OBJECT_CONTEXTS;

static FAST_MUTEX FsRtlContextMutex;
static LIST_ENTRY FsRtlFileObjectContextList;

/* PRIVATE FUNCTIONS **********************************************************/

static
BOOLEAN
FsRtlpContextMatches(
    _In_ PVOID EntryOwnerId,
    _In_ PVOID EntryInstanceId,
    _In_opt_ PVOID OwnerId,
    _In_opt_ PVOID InstanceId)
{
    if ((OwnerId != NULL) && (EntryOwnerId != OwnerId)) return FALSE;
    if ((InstanceId != NULL) && (EntryInstanceId != InstanceId)) return FALSE;

    return TRUE;
}

static
PFSRTL_FILE_OBJECT_CONTEXTS
FsRtlpFindFileObjectContexts(
    _In_ PFILE_OBJECT FileObject)
{
    PLIST_ENTRY Entry;

    for (Entry = FsRtlFileObjectContextList.Flink;
         Entry != &FsRtlFileObjectContextList;
         Entry = Entry->Flink)
    {
        PFSRTL_FILE_OBJECT_CONTEXTS Bucket;

        Bucket = CONTAINING_RECORD(Entry,
                                   FSRTL_FILE_OBJECT_CONTEXTS,
                                   Links);

        if (Bucket->FileObject == FileObject) return Bucket;
    }

    return NULL;
}

static
VOID
FsRtlpFreeStreamContexts(
    _Inout_ PLIST_ENTRY Head)
{
    while (!IsListEmpty(Head))
    {
        PFSRTL_PER_STREAM_CONTEXT Context;

        Context = CONTAINING_RECORD(RemoveHeadList(Head),
                                    FSRTL_PER_STREAM_CONTEXT,
                                    Links);

        if (Context->FreeCallback != NULL) Context->FreeCallback(Context);
    }
}

static
VOID
FsRtlpFreeFileContexts(
    _Inout_ PLIST_ENTRY Head)
{
    while (!IsListEmpty(Head))
    {
        PFSRTL_PER_FILE_CONTEXT Context;

        Context = CONTAINING_RECORD(RemoveHeadList(Head),
                                    FSRTL_PER_FILE_CONTEXT,
                                    Links);

        if (Context->FreeCallback != NULL) Context->FreeCallback(Context);
    }
}

/* INIT FUNCTIONS *************************************************************/

CODE_SEG("INIT")
VOID
NTAPI
FsRtlInitializeContexts(VOID)
{
    ExInitializeFastMutex(&FsRtlContextMutex);
    InitializeListHead(&FsRtlFileObjectContextList);
}

/* PUBLIC FUNCTIONS ***********************************************************/

NTSTATUS
NTAPI
FsRtlInsertPerStreamContext(
    _In_ PFSRTL_ADVANCED_FCB_HEADER AdvancedHeader,
    _In_ PFSRTL_PER_STREAM_CONTEXT Context)
{
    PAGED_CODE();

    if (!FlagOn(AdvancedHeader->Flags2, FSRTL_FLAG2_SUPPORTS_FILTER_CONTEXTS))
    {
        return STATUS_INVALID_DEVICE_REQUEST;
    }

    ExAcquireFastMutex(AdvancedHeader->FastMutex);
    InsertHeadList(&AdvancedHeader->FilterContexts, &Context->Links);
    ExReleaseFastMutex(AdvancedHeader->FastMutex);

    return STATUS_SUCCESS;
}

PFSRTL_PER_STREAM_CONTEXT
NTAPI
FsRtlLookupPerStreamContextInternal(
    _In_ PFSRTL_ADVANCED_FCB_HEADER AdvancedHeader,
    _In_opt_ PVOID OwnerId,
    _In_opt_ PVOID InstanceId)
{
    PFSRTL_PER_STREAM_CONTEXT Found = NULL;
    PLIST_ENTRY Entry;

    PAGED_CODE();

    if (!FlagOn(AdvancedHeader->Flags2, FSRTL_FLAG2_SUPPORTS_FILTER_CONTEXTS))
    {
        return NULL;
    }

    ExAcquireFastMutex(AdvancedHeader->FastMutex);

    for (Entry = AdvancedHeader->FilterContexts.Flink;
         Entry != &AdvancedHeader->FilterContexts;
         Entry = Entry->Flink)
    {
        PFSRTL_PER_STREAM_CONTEXT Context;

        Context = CONTAINING_RECORD(Entry, FSRTL_PER_STREAM_CONTEXT, Links);

        if (FsRtlpContextMatches(Context->OwnerId,
                                 Context->InstanceId,
                                 OwnerId,
                                 InstanceId))
        {
            Found = Context;
            break;
        }
    }

    ExReleaseFastMutex(AdvancedHeader->FastMutex);

    return Found;
}

PFSRTL_PER_STREAM_CONTEXT
NTAPI
FsRtlRemovePerStreamContext(
    _In_ PFSRTL_ADVANCED_FCB_HEADER AdvancedHeader,
    _In_opt_ PVOID OwnerId,
    _In_opt_ PVOID InstanceId)
{
    PFSRTL_PER_STREAM_CONTEXT Found = NULL;
    PLIST_ENTRY Entry;

    PAGED_CODE();

    if (!FlagOn(AdvancedHeader->Flags2, FSRTL_FLAG2_SUPPORTS_FILTER_CONTEXTS))
    {
        return NULL;
    }

    ExAcquireFastMutex(AdvancedHeader->FastMutex);

    for (Entry = AdvancedHeader->FilterContexts.Flink;
         Entry != &AdvancedHeader->FilterContexts;
         Entry = Entry->Flink)
    {
        PFSRTL_PER_STREAM_CONTEXT Context;

        Context = CONTAINING_RECORD(Entry, FSRTL_PER_STREAM_CONTEXT, Links);

        if (FsRtlpContextMatches(Context->OwnerId,
                                 Context->InstanceId,
                                 OwnerId,
                                 InstanceId))
        {
            RemoveEntryList(&Context->Links);
            Found = Context;
            break;
        }
    }

    ExReleaseFastMutex(AdvancedHeader->FastMutex);

    return Found;
}

VOID
NTAPI
FsRtlTeardownPerStreamContexts(
    _In_ PFSRTL_ADVANCED_FCB_HEADER AdvancedHeader)
{
    LIST_ENTRY Detached;

    PAGED_CODE();

    if (!FlagOn(AdvancedHeader->Flags2, FSRTL_FLAG2_SUPPORTS_FILTER_CONTEXTS))
    {
        return;
    }

    InitializeListHead(&Detached);

    ExAcquireFastMutex(AdvancedHeader->FastMutex);

    if (!IsListEmpty(&AdvancedHeader->FilterContexts))
    {
        Detached = AdvancedHeader->FilterContexts;
        Detached.Flink->Blink = &Detached;
        Detached.Blink->Flink = &Detached;
        InitializeListHead(&AdvancedHeader->FilterContexts);
    }

    ExReleaseFastMutex(AdvancedHeader->FastMutex);

    FsRtlpFreeStreamContexts(&Detached);
}

NTSTATUS
NTAPI
FsRtlInsertPerFileContext(
    _In_ PVOID *PerFileContextPointer,
    _In_ PFSRTL_PER_FILE_CONTEXT Context)
{
    PFSRTL_CONTEXT_HEAD Head;

    PAGED_CODE();

    if (PerFileContextPointer == NULL) return STATUS_INVALID_PARAMETER;

    ExAcquireFastMutex(&FsRtlContextMutex);

    Head = (PFSRTL_CONTEXT_HEAD)*PerFileContextPointer;

    if (Head == NULL)
    {
        Head = ExAllocatePoolWithTag(NonPagedPool,
                                     sizeof(FSRTL_CONTEXT_HEAD),
                                     FSRTL_CONTEXT_TAG);

        if (Head == NULL)
        {
            ExReleaseFastMutex(&FsRtlContextMutex);
            return STATUS_INSUFFICIENT_RESOURCES;
        }

        InitializeListHead(&Head->Contexts);
        *PerFileContextPointer = Head;
    }

    InsertHeadList(&Head->Contexts, &Context->Links);

    ExReleaseFastMutex(&FsRtlContextMutex);

    return STATUS_SUCCESS;
}

PFSRTL_PER_FILE_CONTEXT
NTAPI
FsRtlLookupPerFileContext(
    _In_ PVOID *PerFileContextPointer,
    _In_opt_ PVOID OwnerId,
    _In_opt_ PVOID InstanceId)
{
    PFSRTL_PER_FILE_CONTEXT Found = NULL;
    PFSRTL_CONTEXT_HEAD Head;
    PLIST_ENTRY Entry;

    PAGED_CODE();

    if (PerFileContextPointer == NULL) return NULL;

    ExAcquireFastMutex(&FsRtlContextMutex);

    Head = (PFSRTL_CONTEXT_HEAD)*PerFileContextPointer;

    if (Head != NULL)
    {
        for (Entry = Head->Contexts.Flink;
             Entry != &Head->Contexts;
             Entry = Entry->Flink)
        {
            PFSRTL_PER_FILE_CONTEXT Context;

            Context = CONTAINING_RECORD(Entry, FSRTL_PER_FILE_CONTEXT, Links);

            if (FsRtlpContextMatches(Context->OwnerId,
                                     Context->InstanceId,
                                     OwnerId,
                                     InstanceId))
            {
                Found = Context;
                break;
            }
        }
    }

    ExReleaseFastMutex(&FsRtlContextMutex);

    return Found;
}

PFSRTL_PER_FILE_CONTEXT
NTAPI
FsRtlRemovePerFileContext(
    _In_ PVOID *PerFileContextPointer,
    _In_opt_ PVOID OwnerId,
    _In_opt_ PVOID InstanceId)
{
    PFSRTL_PER_FILE_CONTEXT Found = NULL;
    PFSRTL_CONTEXT_HEAD Head;
    PLIST_ENTRY Entry;

    PAGED_CODE();

    if (PerFileContextPointer == NULL) return NULL;

    ExAcquireFastMutex(&FsRtlContextMutex);

    Head = (PFSRTL_CONTEXT_HEAD)*PerFileContextPointer;

    if (Head != NULL)
    {
        for (Entry = Head->Contexts.Flink;
             Entry != &Head->Contexts;
             Entry = Entry->Flink)
        {
            PFSRTL_PER_FILE_CONTEXT Context;

            Context = CONTAINING_RECORD(Entry, FSRTL_PER_FILE_CONTEXT, Links);

            if (FsRtlpContextMatches(Context->OwnerId,
                                     Context->InstanceId,
                                     OwnerId,
                                     InstanceId))
            {
                RemoveEntryList(&Context->Links);
                Found = Context;
                break;
            }
        }
    }

    ExReleaseFastMutex(&FsRtlContextMutex);

    return Found;
}

VOID
NTAPI
FsRtlTeardownPerFileContexts(
    _In_ PVOID *PerFileContextPointer)
{
    PFSRTL_CONTEXT_HEAD Head;
    LIST_ENTRY Detached;

    PAGED_CODE();

    if (PerFileContextPointer == NULL) return;

    InitializeListHead(&Detached);

    ExAcquireFastMutex(&FsRtlContextMutex);

    Head = (PFSRTL_CONTEXT_HEAD)*PerFileContextPointer;
    *PerFileContextPointer = NULL;

    if ((Head != NULL) && !IsListEmpty(&Head->Contexts))
    {
        Detached = Head->Contexts;
        Detached.Flink->Blink = &Detached;
        Detached.Blink->Flink = &Detached;
    }

    ExReleaseFastMutex(&FsRtlContextMutex);

    FsRtlpFreeFileContexts(&Detached);

    if (Head != NULL) ExFreePoolWithTag(Head, FSRTL_CONTEXT_TAG);
}

NTSTATUS
NTAPI
FsRtlInsertPerFileObjectContext(
    _In_ PFILE_OBJECT FileObject,
    _In_ PFSRTL_PER_FILEOBJECT_CONTEXT Context)
{
    PFSRTL_FILE_OBJECT_CONTEXTS Bucket;

    PAGED_CODE();

    if (FileObject == NULL) return STATUS_INVALID_PARAMETER;

    ExAcquireFastMutex(&FsRtlContextMutex);

    Bucket = FsRtlpFindFileObjectContexts(FileObject);

    if (Bucket == NULL)
    {
        Bucket = ExAllocatePoolWithTag(NonPagedPool,
                                       sizeof(FSRTL_FILE_OBJECT_CONTEXTS),
                                       FSRTL_CONTEXT_TAG);

        if (Bucket == NULL)
        {
            ExReleaseFastMutex(&FsRtlContextMutex);
            return STATUS_INSUFFICIENT_RESOURCES;
        }

        Bucket->FileObject = FileObject;
        InitializeListHead(&Bucket->Contexts);
        InsertHeadList(&FsRtlFileObjectContextList, &Bucket->Links);
    }

    InsertHeadList(&Bucket->Contexts, &Context->Links);

    ExReleaseFastMutex(&FsRtlContextMutex);

    return STATUS_SUCCESS;
}

PFSRTL_PER_FILEOBJECT_CONTEXT
NTAPI
FsRtlLookupPerFileObjectContext(
    _In_ PFILE_OBJECT FileObject,
    _In_opt_ PVOID OwnerId,
    _In_opt_ PVOID InstanceId)
{
    PFSRTL_PER_FILEOBJECT_CONTEXT Found = NULL;
    PFSRTL_FILE_OBJECT_CONTEXTS Bucket;

    PAGED_CODE();

    if (FileObject == NULL) return NULL;

    ExAcquireFastMutex(&FsRtlContextMutex);

    Bucket = FsRtlpFindFileObjectContexts(FileObject);

    if (Bucket != NULL)
    {
        PLIST_ENTRY Entry;

        for (Entry = Bucket->Contexts.Flink;
             Entry != &Bucket->Contexts;
             Entry = Entry->Flink)
        {
            PFSRTL_PER_FILEOBJECT_CONTEXT Context;

            Context = CONTAINING_RECORD(Entry,
                                        FSRTL_PER_FILEOBJECT_CONTEXT,
                                        Links);

            if (FsRtlpContextMatches(Context->OwnerId,
                                     Context->InstanceId,
                                     OwnerId,
                                     InstanceId))
            {
                Found = Context;
                break;
            }
        }
    }

    ExReleaseFastMutex(&FsRtlContextMutex);

    return Found;
}

PFSRTL_PER_FILEOBJECT_CONTEXT
NTAPI
FsRtlRemovePerFileObjectContext(
    _In_ PFILE_OBJECT FileObject,
    _In_opt_ PVOID OwnerId,
    _In_opt_ PVOID InstanceId)
{
    PFSRTL_PER_FILEOBJECT_CONTEXT Found = NULL;
    PFSRTL_FILE_OBJECT_CONTEXTS Bucket;
    PFSRTL_FILE_OBJECT_CONTEXTS Empty = NULL;

    PAGED_CODE();

    if (FileObject == NULL) return NULL;

    ExAcquireFastMutex(&FsRtlContextMutex);

    Bucket = FsRtlpFindFileObjectContexts(FileObject);

    if (Bucket != NULL)
    {
        PLIST_ENTRY Entry;

        for (Entry = Bucket->Contexts.Flink;
             Entry != &Bucket->Contexts;
             Entry = Entry->Flink)
        {
            PFSRTL_PER_FILEOBJECT_CONTEXT Context;

            Context = CONTAINING_RECORD(Entry,
                                        FSRTL_PER_FILEOBJECT_CONTEXT,
                                        Links);

            if (FsRtlpContextMatches(Context->OwnerId,
                                     Context->InstanceId,
                                     OwnerId,
                                     InstanceId))
            {
                RemoveEntryList(&Context->Links);
                Found = Context;
                break;
            }
        }

        if (IsListEmpty(&Bucket->Contexts))
        {
            RemoveEntryList(&Bucket->Links);
            Empty = Bucket;
        }
    }

    ExReleaseFastMutex(&FsRtlContextMutex);

    if (Empty != NULL) ExFreePoolWithTag(Empty, FSRTL_CONTEXT_TAG);

    return Found;
}

VOID
NTAPI
FsRtlPTeardownPerFileObjectContexts(
    _In_ PFILE_OBJECT FileObject)
{
    PFSRTL_FILE_OBJECT_CONTEXTS Bucket;

    PAGED_CODE();

    if (FileObject == NULL) return;

    ExAcquireFastMutex(&FsRtlContextMutex);

    Bucket = FsRtlpFindFileObjectContexts(FileObject);
    if (Bucket != NULL) RemoveEntryList(&Bucket->Links);

    ExReleaseFastMutex(&FsRtlContextMutex);

    if (Bucket == NULL) return;

    /* TODO: the I/O manager does not call this when a file object is deleted, so a filter that forgets its contexts keeps the bucket alive */
    while (!IsListEmpty(&Bucket->Contexts))
    {
        RemoveHeadList(&Bucket->Contexts);
    }

    ExFreePoolWithTag(Bucket, FSRTL_CONTEXT_TAG);
}

LOGICAL
NTAPI
FsRtlIsPagingFile(
    _In_ PFILE_OBJECT FileObject)
{
    return MmIsFileObjectAPagingFile(FileObject);
}

/* EOF */
