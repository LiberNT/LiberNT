/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Referencing and dereferencing objects
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

/* PRIVATE FUNCTIONS **********************************************************/

BOOLEAN
FASTCALL
ObReferenceObjectSafe(
    _In_ PVOID Object)
{
    POBJECT_HEADER ObjectHeader = OBJECT_TO_OBJECT_HEADER(Object);
    LONG_PTR OldValue, NewValue;

    /* Take a reference only while the object still has one of its own */
    OldValue = ObjectHeader->PointerCount;
    while (OldValue != 0)
    {
        NewValue = InterlockedCompareExchangeSizeT(&ObjectHeader->PointerCount,
                                                   OldValue + 1,
                                                   OldValue);
        if (NewValue == OldValue) return TRUE;
        OldValue = NewValue;
    }

    return FALSE;
}

VOID
NTAPI
ObpDeferObjectDeletion(
    _In_ POBJECT_HEADER Header)
{
    PVOID Entry;

    /* Push the object on the reaper's list */
    do
    {
        Entry = ObpReaperList;
        Header->NextToFree = Entry;
    } while (InterlockedCompareExchangePointer(&ObpReaperList, Header, Entry) != Entry);

    /* The reaper only needs waking when it finds an empty list */
    if (Entry == NULL) ExQueueWorkItem(&ObpReaperWorkItem, CriticalWorkQueue);
}

LONG
FASTCALL
ObReferenceObjectEx(
    _In_ PVOID Object,
    _In_ LONG Count)
{
    POBJECT_HEADER Header = OBJECT_TO_OBJECT_HEADER(Object);

    return (LONG)(InterlockedExchangeAddSizeT(&Header->PointerCount, Count) + Count);
}

LONG
FASTCALL
ObDereferenceObjectEx(
    _In_ PVOID Object,
    _In_ LONG Count)
{
    POBJECT_HEADER Header = OBJECT_TO_OBJECT_HEADER(Object);
    LONG_PTR NewCount;

    NewCount = InterlockedExchangeAddSizeT(&Header->PointerCount, -Count) - Count;
    if (NewCount == 0) ObpDeferObjectDeletion(Header);

    return (LONG)NewCount;
}

VOID
FASTCALL
ObInitializeFastReference(
    _In_ PEX_FAST_REF FastRef,
    _In_opt_ PVOID Object)
{
    /* The pointer is handed a full batch of references to lend out */
    if (Object != NULL) ObReferenceObjectEx(Object, MAX_FAST_REFS);
    ExInitializeFastReference(FastRef, Object);
}

PVOID
FASTCALL
ObFastReferenceObjectLocked(
    _In_ PEX_FAST_REF FastRef)
{
    PVOID Object = ExGetObjectFastReference(*FastRef);

    if (Object != NULL) ObReferenceObject(Object);
    return Object;
}

PVOID
FASTCALL
ObFastReferenceObject(
    _In_ PEX_FAST_REF FastRef)
{
    EX_FAST_REF OldValue = ExAcquireFastReference(FastRef);
    PVOID Object = ExGetObjectFastReference(OldValue);
    ULONG_PTR Count = ExGetCountFastReference(OldValue);

    if (Count > 1) return Object;
    if (Count == 0) return NULL;

    /* That was the last cached reference, so refill the batch */
    ObReferenceObjectEx(Object, MAX_FAST_REFS);
    if (!ExInsertFastReference(FastRef, Object))
        ObDereferenceObjectEx(Object, MAX_FAST_REFS);

    return Object;
}

VOID
FASTCALL
ObFastDereferenceObject(
    _In_ PEX_FAST_REF FastRef,
    _In_ PVOID Object)
{
    /* Give the reference back to the pointer, or to the object itself */
    if (!ExReleaseFastReference(FastRef, Object)) ObDereferenceObject(Object);
}

PVOID
FASTCALL
ObFastReplaceObject(
    _In_ PEX_FAST_REF FastRef,
    _In_opt_ PVOID Object)
{
    EX_FAST_REF OldValue;
    PVOID OldObject;
    ULONG Count;

    if (Object != NULL) ObReferenceObjectEx(Object, MAX_FAST_REFS);

    OldValue = ExSwapFastReference(FastRef, Object);
    OldObject = ExGetObjectFastReference(OldValue);
    Count = ExGetCountFastReference(OldValue);

    if (OldObject != NULL && Count != 0) ObDereferenceObjectEx(OldObject, Count);

    return OldObject;
}

NTSTATUS
NTAPI
ObReferenceFileObjectForWrite(
    _In_ HANDLE Handle,
    _In_ KPROCESSOR_MODE AccessMode,
    _Out_ PFILE_OBJECT *FileObject,
    _Out_ POBJECT_HANDLE_INFORMATION HandleInformation)
{
    PHANDLE_TABLE_ENTRY HandleEntry;
    POBJECT_HEADER ObjectHeader;
    ACCESS_MASK GrantedAccess, DesiredAccess;
    PHANDLE_TABLE HandleTable;
    NTSTATUS Status;

    *FileObject = NULL;

    if (HandleToLong(Handle) < 0)
    {
        if (AccessMode != KernelMode ||
            Handle == NtCurrentProcess() ||
            Handle == NtCurrentThread())
        {
            return STATUS_INVALID_HANDLE;
        }

        Handle = ObKernelHandleToHandle(Handle);
        HandleTable = ObpKernelHandleTable;
    }
    else
    {
        HandleTable = PsGetCurrentProcess()->ObjectTable;
    }

    ASSERT(HandleTable != NULL);
    KeEnterCriticalRegion();

    HandleEntry = ExMapHandleToPointer(HandleTable, Handle);
    if (HandleEntry == NULL)
    {
        KeLeaveCriticalRegion();
        return STATUS_INVALID_HANDLE;
    }

    ObjectHeader = ObpGetHandleObject(HandleEntry);

    /* The file object decides which right a write actually needs */
    Status = IoComputeDesiredAccessFileObject((PFILE_OBJECT)&ObjectHeader->Body,
                                              &DesiredAccess);
    if (!NT_SUCCESS(Status))
    {
        ExUnlockHandleTableEntry(HandleTable, HandleEntry);
        KeLeaveCriticalRegion();
        return STATUS_OBJECT_TYPE_MISMATCH;
    }

    if (BooleanFlagOn(NtGlobalFlag, FLG_KERNEL_STACK_TRACE_DB))
    {
        /* TODO: translate the granted access for a traced handle */
        GrantedAccess = HandleEntry->GrantedAccess;
    }
    else
    {
        GrantedAccess = HandleEntry->GrantedAccess & ~ObpAccessProtectCloseBit;
    }

    HandleInformation->GrantedAccess = GrantedAccess;
    HandleInformation->HandleAttributes = HandleEntry->ObAttributes &
                                          (OBJ_INHERIT | OBJ_AUDIT_OBJECT_CLOSE);
    if (HandleEntry->GrantedAccess & ObpAccessProtectCloseBit)
        HandleInformation->HandleAttributes |= OBJ_PROTECT_CLOSE;

    if ((GrantedAccess & DesiredAccess) == 0)
    {
        ExUnlockHandleTableEntry(HandleTable, HandleEntry);
        KeLeaveCriticalRegion();
        return STATUS_ACCESS_DENIED;
    }

    /* TODO: audit the access when the handle asks for it */
    ObpReferenceObjectByHandleEntry(HandleTable, Handle, HandleEntry, ObjectHeader);
    ExUnlockHandleTableEntry(HandleTable, HandleEntry);
    KeLeaveCriticalRegion();

    *FileObject = (PFILE_OBJECT)&ObjectHeader->Body;
    ASSERT(*FileObject != NULL);
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

/* PUBLIC FUNCTIONS ***********************************************************/

LONG_PTR
FASTCALL
ObfReferenceObject(
    _In_ PVOID Object)
{
    ASSERT(Object);
    return InterlockedIncrementSizeT(&OBJECT_TO_OBJECT_HEADER(Object)->PointerCount);
}

LONG_PTR
FASTCALL
ObfDereferenceObject(
    _In_ PVOID Object)
{
    POBJECT_HEADER Header = OBJECT_TO_OBJECT_HEADER(Object);
    LONG_PTR NewCount;

    /* Every handle owns a pointer, so this can only mean a lost reference */
    if (Header->PointerCount < Header->HandleCount)
    {
        DPRINT1("Misbehaving object: %wZ\n", &ObpGetObjectTypeFromHeader(Header)->Name);
        return Header->PointerCount;
    }

    NewCount = InterlockedDecrementSizeT(&Header->PointerCount);
    if (NewCount != 0) return NewCount;

    ASSERT(Header->HandleCount == 0);

    /* The delete procedure may block, so it waits for a thread that can */
    if (!KeAreAllApcsDisabled())
        ObpDeleteObject(Object, FALSE);
    else
        ObpDeferObjectDeletion(Header);

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

VOID
NTAPI
ObDereferenceObjectDeferDelete(
    _In_ PVOID Object)
{
    POBJECT_HEADER Header = OBJECT_TO_OBJECT_HEADER(Object);

    if (InterlockedDecrementSizeT(&Header->PointerCount) == 0)
        ObpDeferObjectDeletion(Header);
}

#undef ObDereferenceObject
VOID
NTAPI
ObDereferenceObject(
    _In_ PVOID Object)
{
    ObfDereferenceObject(Object);
}

NTSTATUS
NTAPI
ObReferenceObjectByPointer(
    _In_ PVOID Object,
    _In_ ACCESS_MASK DesiredAccess,
    _In_opt_ POBJECT_TYPE ObjectType,
    _In_ KPROCESSOR_MODE AccessMode)
{
    POBJECT_HEADER Header = OBJECT_TO_OBJECT_HEADER(Object);

    UNREFERENCED_PARAMETER(DesiredAccess);

    /* A kernel caller may name any type it likes, except that a symbolic link
     * must never be mistaken for what it points at */
    if (ObpGetObjectTypeFromHeader(Header) != ObjectType &&
        (AccessMode != KernelMode || ObjectType == ObpSymbolicLinkObjectType))
    {
        return STATUS_OBJECT_TYPE_MISMATCH;
    }

    InterlockedIncrementSizeT(&Header->PointerCount);
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
ObReferenceObjectByName(
    _In_ PUNICODE_STRING ObjectPath,
    _In_ ULONG Attributes,
    _In_opt_ PACCESS_STATE PassedAccessState,
    _In_ ACCESS_MASK DesiredAccess,
    _In_ POBJECT_TYPE ObjectType,
    _In_ KPROCESSOR_MODE AccessMode,
    _Inout_opt_ PVOID ParseContext,
    _Out_ PVOID *ObjectPtr)
{
    OBP_LOOKUP_CONTEXT Context;
    UNICODE_STRING ObjectName;
    ACCESS_STATE AccessState;
    AUX_ACCESS_DATA AuxData;
    PVOID Object = NULL;
    NTSTATUS Status;

    PAGED_CODE();

    if (ObjectPath == NULL) return STATUS_OBJECT_NAME_INVALID;

    Status = ObpCaptureObjectName(&ObjectName, ObjectPath, AccessMode, TRUE);
    if (!NT_SUCCESS(Status)) return Status;

    if (ObjectName.Length == 0)
    {
        ObpFreeObjectNameBuffer(&ObjectName);
        return STATUS_OBJECT_NAME_INVALID;
    }

    if (PassedAccessState == NULL)
    {
        PassedAccessState = &AccessState;
        Status = SeCreateAccessState(&AccessState,
                                     &AuxData,
                                     DesiredAccess,
                                     &ObjectType->TypeInfo.GenericMapping);
        if (!NT_SUCCESS(Status))
        {
            ObpFreeObjectNameBuffer(&ObjectName);
            return Status;
        }
    }

    *ObjectPtr = NULL;
    Status = ObpLookupObjectName(NULL,
                                 &ObjectName,
                                 Attributes,
                                 ObjectType,
                                 AccessMode,
                                 ParseContext,
                                 NULL,
                                 NULL,
                                 PassedAccessState,
                                 &Context,
                                 &Object);
    ObpReleaseLookupContext(&Context);

    if (NT_SUCCESS(Status))
    {
        if (ObpCheckObjectReference(Object, PassedAccessState, FALSE, AccessMode, &Status))
            *ObjectPtr = Object;
    }

    if (PassedAccessState == &AccessState) SeDeleteAccessState(PassedAccessState);

    ObpFreeObjectNameBuffer(&ObjectName);
    return Status;
}

/*
 * NtCurrentProcess and NtCurrentThread never reach the handle table: they name
 * the caller itself, which always holds every right to itself.
 */
static
NTSTATUS
ObpReferenceCurrentObject(
    _In_ PVOID Object,
    _In_ ACCESS_MASK DesiredAccess,
    _In_ ACCESS_MASK GrantedAccess,
    _In_ KPROCESSOR_MODE AccessMode,
    _Out_ PVOID *ObjectPtr,
    _Out_opt_ POBJECT_HANDLE_INFORMATION HandleInformation)
{
    POBJECT_HEADER ObjectHeader;

    if (AccessMode != KernelMode && (~GrantedAccess & DesiredAccess) != 0)
        return STATUS_ACCESS_DENIED;

    if (HandleInformation != NULL)
    {
        HandleInformation->HandleAttributes = 0;
        HandleInformation->GrantedAccess = GrantedAccess;
    }

    ObjectHeader = OBJECT_TO_OBJECT_HEADER(Object);
    InterlockedExchangeAddSizeT(&ObjectHeader->PointerCount, 1);

    *ObjectPtr = Object;
    return STATUS_SUCCESS;
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
    PHANDLE_TABLE_ENTRY HandleEntry;
    POBJECT_HEADER ObjectHeader;
    PHANDLE_TABLE HandleTable;
    ACCESS_MASK GrantedAccess;
    ULONG Attributes;

    PAGED_CODE();

    *Object = NULL;

    if (HandleToLong(Handle) < 0)
    {
        if (Handle == NtCurrentProcess())
        {
            if (ObjectType != NULL && ObjectType != PsProcessType)
                return STATUS_OBJECT_TYPE_MISMATCH;

            return ObpReferenceCurrentObject(PsGetCurrentProcess(),
                                             DesiredAccess,
                                             PROCESS_ALL_ACCESS,
                                             AccessMode,
                                             Object,
                                             HandleInformation);
        }

        if (Handle == NtCurrentThread())
        {
            if (ObjectType != NULL && ObjectType != PsThreadType)
                return STATUS_OBJECT_TYPE_MISMATCH;

            return ObpReferenceCurrentObject(PsGetCurrentThread(),
                                             DesiredAccess,
                                             THREAD_ALL_ACCESS,
                                             AccessMode,
                                             Object,
                                             HandleInformation);
        }

        if (AccessMode != KernelMode)
        {
            /* A kernel handle is out of reach of a user-mode caller */
            ObpCheckInvalidHandleReference(Handle, AccessMode);
            return STATUS_INVALID_HANDLE;
        }

        Handle = ObKernelHandleToHandle(Handle);
        HandleTable = ObpKernelHandleTable;
    }
    else
    {
        HandleTable = PsGetCurrentProcess()->ObjectTable;
    }

    ASSERT(HandleTable != NULL);
    KeEnterCriticalRegion();

    HandleEntry = ExMapHandleToPointer(HandleTable, Handle);
    if (HandleEntry == NULL)
    {
        KeLeaveCriticalRegion();
        ObpCheckInvalidHandleReference(Handle, AccessMode);
        return STATUS_INVALID_HANDLE;
    }

    ObjectHeader = ObpGetHandleObject(HandleEntry);

    if (ObjectType != NULL && ObjectType != ObpGetObjectTypeFromHeader(ObjectHeader))
    {
        ExUnlockHandleTableEntry(HandleTable, HandleEntry);
        KeLeaveCriticalRegion();
        return STATUS_OBJECT_TYPE_MISMATCH;
    }

    GrantedAccess = HandleEntry->GrantedAccess & ~ObpAccessProtectCloseBit;

    if (AccessMode != KernelMode && (~GrantedAccess & DesiredAccess) != 0)
    {
        DPRINT("Rights not granted: %x\n", ~GrantedAccess & DesiredAccess);
        ExUnlockHandleTableEntry(HandleTable, HandleEntry);
        KeLeaveCriticalRegion();
        return STATUS_ACCESS_DENIED;
    }

    /* Consume a reference precharged to this handle */
    ObpReferenceObjectByHandleEntry(HandleTable, Handle, HandleEntry, ObjectHeader);

    if (HandleInformation != NULL)
    {
        Attributes = HandleEntry->ObAttributes & (OBJ_INHERIT | OBJ_AUDIT_OBJECT_CLOSE);
        if (HandleEntry->GrantedAccess & ObpAccessProtectCloseBit)
            Attributes |= OBJ_PROTECT_CLOSE;

        HandleInformation->HandleAttributes = Attributes;
        HandleInformation->GrantedAccess = GrantedAccess;
    }

    *Object = &ObjectHeader->Body;

    ExUnlockHandleTableEntry(HandleTable, HandleEntry);
    KeLeaveCriticalRegion();

    ASSERT(*Object != NULL);
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

/* EOF */
