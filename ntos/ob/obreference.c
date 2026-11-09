/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Object references
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

/* PRIVATE FUNCTIONS **********************************************************/

/*
 * The reaper list is an intrusive stack through the header field the handle
 * count no longer needs, with (PVOID)1 standing for "the reaper is draining".
 */
static
VOID
ObpDeferObjectDeletion(
    _In_ POBJECT_HEADER ObjectHeader)
{
    PVOID Head;

    do
    {
        Head = ObpReaperList;
        ObjectHeader->NextToFree = Head;
    }
    while (InterlockedCompareExchangePointer(&ObpReaperList,
                                             ObjectHeader,
                                             Head) != Head);

    /* Only the thread that found the list empty has to wake the reaper */
    if (Head == NULL) ExQueueWorkItem(&ObpReaperWorkItem, CriticalWorkQueue);
}

/*
 * The delete procedure of a type may be pageable and may block, so an object
 * whose last reference goes away above APC level is handed to the reaper.
 */
static
VOID
ObpRetireObject(
    _In_ PVOID Object,
    _In_ BOOLEAN Defer)
{
    POBJECT_HEADER ObjectHeader = OBJECT_TO_OBJECT_HEADER(Object);

    if (Defer || KeGetCurrentIrql() > APC_LEVEL)
        ObpDeferObjectDeletion(ObjectHeader);
    else
        ObpDeleteObject(Object, FALSE);
}

static
LONG_PTR
ObpAdjustPointerCount(
    _In_ PVOID Object,
    _In_ LONG Count)
{
    POBJECT_HEADER ObjectHeader = OBJECT_TO_OBJECT_HEADER(Object);

    return InterlockedExchangeAddSizeT(&ObjectHeader->PointerCount,
                                       (LONG_PTR)Count) + Count;
}

/* PUBLIC FUNCTIONS ***********************************************************/

LONG
FASTCALL
ObReferenceObjectEx(
    _In_ PVOID Object,
    _In_ LONG Count)
{
    return (LONG)ObpAdjustPointerCount(Object, Count);
}

LONG
FASTCALL
ObDereferenceObjectEx(
    _In_ PVOID Object,
    _In_ LONG Count)
{
    LONG_PTR NewCount = ObpAdjustPointerCount(Object, -Count);

    ASSERT(NewCount >= 0);
    if (NewCount == 0) ObpRetireObject(Object, FALSE);

    return (LONG)NewCount;
}

/* Takes a reference only if the object still has one, so a caller racing the
 * last dereference is told to let go instead of resurrecting it. */
BOOLEAN
FASTCALL
ObReferenceObjectSafe(
    _In_ PVOID Object)
{
    POBJECT_HEADER ObjectHeader = OBJECT_TO_OBJECT_HEADER(Object);
    LONG_PTR Seen = ObjectHeader->PointerCount;
    LONG_PTR Was;

    while (Seen > 0)
    {
        Was = (LONG_PTR)InterlockedCompareExchangePointer(
            (PVOID *)&ObjectHeader->PointerCount,
            (PVOID)(Seen + 1),
            (PVOID)Seen);
        if (Was == Seen) return TRUE;
        Seen = Was;
    }

    return FALSE;
}

LONG_PTR
FASTCALL
ObfReferenceObject(
    _In_ PVOID Object)
{
    return ObpAdjustPointerCount(Object, 1);
}

LONG_PTR
FASTCALL
ObfDereferenceObject(
    _In_ PVOID Object)
{
    LONG_PTR NewCount = ObpAdjustPointerCount(Object, -1);

    ASSERT(NewCount >= 0);
    if (NewCount == 0) ObpRetireObject(Object, FALSE);

    return NewCount;
}

LONG_PTR
FASTCALL
ObfReferenceObjectWithTag(
    _In_ PVOID Object,
    _In_ ULONG Tag)
{
    UNREFERENCED_PARAMETER(Tag);
    return ObfReferenceObject(Object);
}

LONG_PTR
FASTCALL
ObfDereferenceObjectWithTag(
    _In_ PVOID Object,
    _In_ ULONG Tag)
{
    UNREFERENCED_PARAMETER(Tag);
    return ObfDereferenceObject(Object);
}

/* The published header turns the plain name into the fastcall one, so the
 * exported stdcall entry has to be defined without it */
#undef ObDereferenceObject

VOID
NTAPI
ObDereferenceObject(
    _In_ PVOID Object)
{
    ObfDereferenceObject(Object);
}

VOID
NTAPI
ObDereferenceObjectDeferDelete(
    _In_ PVOID Object)
{
    LONG_PTR NewCount = ObpAdjustPointerCount(Object, -1);

    ASSERT(NewCount >= 0);
    if (NewCount == 0) ObpRetireObject(Object, TRUE);
}

/* FAST REFERENCED OBJECT FIELDS **********************************************/

VOID
FASTCALL
ObInitializeFastReference(
    _In_ PEX_FAST_REF FastRef,
    _In_opt_ PVOID Object)
{
    /* The field is handed MAX_FAST_REFS references to lend out */
    if (Object != NULL) ObpAdjustPointerCount(Object, MAX_FAST_REFS);

    ExInitializeFastReference(FastRef, Object);
}

PVOID
FASTCALL
ObFastReferenceObject(
    _In_ PEX_FAST_REF FastRef)
{
    EX_FAST_REF Taken = ExAcquireFastReference(FastRef);
    PVOID Object = ExGetObjectFastReference(Taken);
    ULONG Cached = ExGetCountFastReference(Taken);

    /* Nothing was parked, so the caller has to go the slow way */
    if (Object == NULL || Cached == 0) return NULL;

    if (Cached == 1)
    {
        /* That was the last one; buy a batch for whoever comes next */
        ObpAdjustPointerCount(Object, MAX_FAST_REFS);
        if (!ExInsertFastReference(FastRef, Object))
            ObpAdjustPointerCount(Object, -MAX_FAST_REFS);
    }

    return Object;
}

PVOID
FASTCALL
ObFastReferenceObjectLocked(
    _In_ PEX_FAST_REF FastRef)
{
    PVOID Object = ExGetObjectFastReference(*FastRef);

    /* The caller serialises against replacement, so one plain reference does */
    if (Object != NULL) ObpAdjustPointerCount(Object, 1);

    return Object;
}

VOID
FASTCALL
ObFastDereferenceObject(
    _In_ PEX_FAST_REF FastRef,
    _In_ PVOID Object)
{
    /* Give the reference back to the field, or to the object if it is full */
    if (!ExReleaseFastReference(FastRef, Object)) ObDereferenceObject(Object);
}

PVOID
FASTCALL
ObFastReplaceObject(
    _In_ PEX_FAST_REF FastRef,
    _In_opt_ PVOID Object)
{
    EX_FAST_REF Old;
    PVOID OldObject;
    ULONG Cached;

    if (Object != NULL) ObpAdjustPointerCount(Object, MAX_FAST_REFS);

    Old = ExSwapFastReference(FastRef, Object);
    OldObject = ExGetObjectFastReference(Old);
    Cached = ExGetCountFastReference(Old);

    /* Hand back what nobody took out of the field, and the field's own one */
    if (OldObject != NULL && Cached != 0)
        ObpAdjustPointerCount(OldObject, -(LONG)Cached);

    return OldObject;
}

/* REFERENCING BY POINTER, NAME AND HANDLE ************************************/

NTSTATUS
NTAPI
ObReferenceObjectByPointer(
    _In_ PVOID Object,
    _In_ ACCESS_MASK DesiredAccess,
    _In_opt_ POBJECT_TYPE ObjectType,
    _In_ KPROCESSOR_MODE AccessMode)
{
    POBJECT_HEADER ObjectHeader = OBJECT_TO_OBJECT_HEADER(Object);

    UNREFERENCED_PARAMETER(DesiredAccess);
    UNREFERENCED_PARAMETER(AccessMode);

    /* The caller already has the object, so only its type is in question */
    if (ObjectType != NULL &&
        ObpGetObjectTypeFromHeader(ObjectHeader) != ObjectType)
    {
        return STATUS_OBJECT_TYPE_MISMATCH;
    }

    ObpAdjustPointerCount(Object, 1);
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
ObReferenceObjectByName(
    _In_ PUNICODE_STRING ObjectName,
    _In_ ULONG Attributes,
    _In_opt_ PACCESS_STATE PassedAccessState,
    _In_opt_ ACCESS_MASK DesiredAccess,
    _In_ POBJECT_TYPE ObjectType,
    _In_ KPROCESSOR_MODE AccessMode,
    _Inout_opt_ PVOID ParseContext,
    _Out_ PVOID *Object)
{
    PACCESS_STATE AccessState = PassedAccessState;
    ACCESS_STATE LocalAccessState;
    OBP_LOOKUP_CONTEXT LookupContext;
    AUX_ACCESS_DATA AuxData;
    UNICODE_STRING Captured;
    NTSTATUS Status;
    PVOID Found;

    PAGED_CODE();

    *Object = NULL;

    if (ObjectName == NULL || ObjectName->Length == 0)
        return STATUS_OBJECT_NAME_INVALID;

    /* The lookup walks the name as it goes, so it gets a copy of its own */
    Status = ObpCaptureObjectName(&Captured, ObjectName, AccessMode, TRUE);
    if (!NT_SUCCESS(Status)) return Status;

    if (AccessState == NULL)
    {
        AccessState = &LocalAccessState;
        Status = SeCreateAccessState(&LocalAccessState,
                                     &AuxData,
                                     DesiredAccess,
                                     &ObjectType->TypeInfo.GenericMapping);
        if (!NT_SUCCESS(Status))
        {
            ObpFreeObjectNameBuffer(&Captured);
            return Status;
        }
    }

    ObpInitializeLookupContext(&LookupContext);

    Status = ObpLookupObjectName(NULL,
                                 &Captured,
                                 Attributes,
                                 ObjectType,
                                 AccessMode,
                                 ParseContext,
                                 NULL,
                                 NULL,
                                 AccessState,
                                 &LookupContext,
                                 &Found);

    /* The lookup hands its own reference to us, so the context can let go */
    ObpReleaseLookupContext(&LookupContext);

    if (AccessState == &LocalAccessState) SeDeleteAccessState(&LocalAccessState);
    ObpFreeObjectNameBuffer(&Captured);

    if (!NT_SUCCESS(Status)) return Status;

    *Object = Found;
    return STATUS_SUCCESS;
}

BOOLEAN
NTAPI
ObpIsStrictHandleCheckingEnabled(VOID)
{
#if (NTDDI_VERSION >= NTDDI_LONGHORN)
    PHANDLE_TABLE HandleTable = PsGetCurrentProcess()->ObjectTable;
    return HandleTable && HandleTable->EnableHandleExceptions;
#else
    return FALSE;
#endif
}

VOID
NTAPI
ObpCheckInvalidHandleReference(
    _In_ HANDLE Handle,
    _In_ KPROCESSOR_MODE AccessMode)
{
    if (Handle && AccessMode == UserMode && !KeIsAttachedProcess() &&
        ObpIsStrictHandleCheckingEnabled())
    {
        /* Defer the exception until return to user mode. Keep returning the
         * failure status to kernel callers so they can release resources. */
        KeRaiseUserException(STATUS_INVALID_HANDLE);
    }
}

/*
 * A process and a thread can always reach themselves without a table entry, so
 * the two pseudo handles answer with full access and no attributes.
 */
static
NTSTATUS
ObpReferenceSelfHandle(
    _In_ HANDLE Handle,
    _In_opt_ POBJECT_TYPE ObjectType,
    _Out_ PVOID *Object,
    _Out_opt_ POBJECT_HANDLE_INFORMATION HandleInformation)
{
    POBJECT_TYPE Wanted;
    ACCESS_MASK Granted;
    PVOID Self;

    if (Handle == NtCurrentProcess())
    {
        Wanted = PsProcessType;
        Granted = PROCESS_ALL_ACCESS;
        Self = PsGetCurrentProcess();
    }
    else
    {
        Wanted = PsThreadType;
        Granted = THREAD_ALL_ACCESS;
        Self = PsGetCurrentThread();
    }

    if (ObjectType != NULL && ObjectType != Wanted)
        return STATUS_OBJECT_TYPE_MISMATCH;

    ObpAdjustPointerCount(Self, 1);

    if (HandleInformation != NULL)
    {
        HandleInformation->HandleAttributes = 0;
        HandleInformation->GrantedAccess = Granted;
    }

    *Object = Self;
    return STATUS_SUCCESS;
}

static
PHANDLE_TABLE
ObpTableForHandle(
    _Inout_ PHANDLE Handle,
    _In_ KPROCESSOR_MODE AccessMode)
{
    if (ObpIsKernelHandle(*Handle, AccessMode))
    {
        *Handle = ObKernelHandleToHandle(*Handle);
        return ObpKernelHandleTable;
    }

    return PsGetCurrentProcess()->ObjectTable;
}

NTSTATUS
NTAPI
ObReferenceObjectByHandle(
    _In_ HANDLE Handle,
    _In_ ACCESS_MASK DesiredAccess,
    _In_opt_ POBJECT_TYPE ObjectType,
    _In_ KPROCESSOR_MODE AccessMode,
    _Out_ PVOID *Object,
    _Out_opt_ POBJECT_HANDLE_INFORMATION HandleInformation)
{
    PHANDLE_TABLE_ENTRY Entry;
    POBJECT_HEADER ObjectHeader;
    PHANDLE_TABLE Table;
    ACCESS_MASK Granted;
    NTSTATUS Status;

    *Object = NULL;

    if (Handle == NtCurrentProcess() || Handle == NtCurrentThread())
    {
        Status = ObpReferenceSelfHandle(Handle,
                                        ObjectType,
                                        Object,
                                        HandleInformation);
        if (Status == STATUS_INVALID_HANDLE)
            ObpCheckInvalidHandleReference(Handle, AccessMode);

        return Status;
    }

    Table = ObpTableForHandle(&Handle, AccessMode);
    if (Table == NULL)
    {
        ObpCheckInvalidHandleReference(Handle, AccessMode);
        return STATUS_INVALID_HANDLE;
    }

    /* The entry stays locked while we read it, so the table cannot be swept
     * underneath, and that needs the critical region of its own */
    KeEnterCriticalRegion();

    Entry = ExMapHandleToPointer(Table, Handle);
    if (Entry == NULL)
    {
        KeLeaveCriticalRegion();
        ObpCheckInvalidHandleReference(Handle, AccessMode);
        return STATUS_INVALID_HANDLE;
    }

    ObjectHeader = ObpGetHandleObject(Entry);

    if (ObjectType != NULL &&
        ObpGetObjectTypeFromHeader(ObjectHeader) != ObjectType)
    {
        ExUnlockHandleTableEntry(Table, Entry);
        KeLeaveCriticalRegion();
        return STATUS_OBJECT_TYPE_MISMATCH;
    }

    Granted = Entry->GrantedAccess & ~ObpAccessProtectCloseBit;

    /* The kernel is trusted with whatever the handle names */
    if (AccessMode != KernelMode && (DesiredAccess & ~Granted) != 0)
    {
        ExUnlockHandleTableEntry(Table, Entry);
        KeLeaveCriticalRegion();
        return STATUS_ACCESS_DENIED;
    }

    InterlockedIncrementSizeT(&ObjectHeader->PointerCount);

    if (HandleInformation != NULL)
    {
        HandleInformation->HandleAttributes =
            (ULONG)(Entry->ObAttributes & OBJ_HANDLE_ATTRIBUTES);
        HandleInformation->GrantedAccess = Granted;
    }

    ExUnlockHandleTableEntry(Table, Entry);
    KeLeaveCriticalRegion();

    *Object = &ObjectHeader->Body;
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
ObReferenceObjectByHandleWithTag(
    _In_ HANDLE Handle,
    _In_ ACCESS_MASK DesiredAccess,
    _In_opt_ POBJECT_TYPE ObjectType,
    _In_ KPROCESSOR_MODE AccessMode,
    _In_ ULONG Tag,
    _Out_ PVOID *Object,
    _Out_opt_ POBJECT_HANDLE_INFORMATION HandleInformation)
{
    UNREFERENCED_PARAMETER(Tag);
    return ObReferenceObjectByHandle(Handle,
                                     DesiredAccess,
                                     ObjectType,
                                     AccessMode,
                                     Object,
                                     HandleInformation);
}

NTSTATUS
NTAPI
ObReferenceFileObjectForWrite(
    _In_ HANDLE Handle,
    _In_ KPROCESSOR_MODE AccessMode,
    _Out_ PFILE_OBJECT *FileObject,
    _Out_ POBJECT_HANDLE_INFORMATION HandleInformation)
{
    NTSTATUS Status;
    PVOID Object;

    /* Appending is a write as far as the handle is concerned, so a handle that
     * carries either right is accepted */
    Status = ObReferenceObjectByHandle(Handle,
                                       FILE_WRITE_DATA,
                                       IoFileObjectType,
                                       AccessMode,
                                       &Object,
                                       HandleInformation);
    if (Status == STATUS_ACCESS_DENIED)
    {
        Status = ObReferenceObjectByHandle(Handle,
                                           FILE_APPEND_DATA,
                                           IoFileObjectType,
                                           AccessMode,
                                           &Object,
                                           HandleInformation);
    }

    if (!NT_SUCCESS(Status)) return Status;

    *FileObject = Object;
    return STATUS_SUCCESS;
}

/* EOF */
