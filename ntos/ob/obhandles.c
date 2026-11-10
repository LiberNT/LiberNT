/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Object handles
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

/* GLOBALS ********************************************************************/

PHANDLE_TABLE ObpKernelHandleTable = NULL;

/* The handle database grows in steps so a popular object does not reallocate
 * on every open. */
#define OBP_HANDLE_DATABASE_STEP 4

/* PROCESS HANDLE TABLES ******************************************************/

/*
 * A process's table may be torn down at any time, so it is only handed out
 * under the rundown protection that keeps the teardown waiting.
 */
PHANDLE_TABLE
NTAPI
ObReferenceProcessHandleTable(
    _In_ PEPROCESS Process)
{
    PHANDLE_TABLE HandleTable = NULL;

    KeEnterCriticalRegion();

    if (ExAcquireRundownProtection(&Process->RundownProtect))
    {
        HandleTable = Process->ObjectTable;
        if (HandleTable == NULL)
            ExReleaseRundownProtection(&Process->RundownProtect);
    }

    if (HandleTable == NULL) KeLeaveCriticalRegion();

    return HandleTable;
}

VOID
NTAPI
ObDereferenceProcessHandleTable(
    _In_ PEPROCESS Process)
{
    ExReleaseRundownProtection(&Process->RundownProtect);
    KeLeaveCriticalRegion();
}

ULONG
NTAPI
ObGetProcessHandleCount(
    _In_ PEPROCESS Process)
{
    PHANDLE_TABLE HandleTable;
    ULONG Count = 0;

    PAGED_CODE();

    HandleTable = ObReferenceProcessHandleTable(Process);
    if (HandleTable == NULL) return 0;

    Count = (ULONG)ExpGetHandleCount(HandleTable);
    ObDereferenceProcessHandleTable(Process);

    return Count;
}

BOOLEAN
NTAPI
ObIsKernelHandle(
    _In_ HANDLE Handle)
{
    /* A kernel handle is one the kernel table answers for, and the two
     * pseudo handles never are */
    return (((ULONG_PTR)Handle & KERNEL_HANDLE_FLAG) == KERNEL_HANDLE_FLAG) &&
           (Handle != NtCurrentProcess()) &&
           (Handle != NtCurrentThread());
}

/* ACCESS AND QUOTA ***********************************************************/

static
BOOLEAN
ObpSaclHasAuditAces(
    _In_ PISECURITY_DESCRIPTOR SecurityDescriptor);

/*
 * A handle may only carry rights its type defines, and a request for whatever
 * the object allows is answered with everything the mapping covers.
 */
static
NTSTATUS
ObpValidateAccessMask(
    _In_ POBJECT_TYPE ObjectType,
    _Inout_ PACCESS_STATE AccessState,
    _In_ BOOLEAN DropExcess)
{
    PISECURITY_DESCRIPTOR Descriptor;

    if (AccessState->RemainingDesiredAccess & MAXIMUM_ALLOWED)
    {
        AccessState->RemainingDesiredAccess &= ~MAXIMUM_ALLOWED;
        AccessState->RemainingDesiredAccess |=
            ObjectType->TypeInfo.GenericMapping.GenericAll;
    }

    RtlMapGenericMask(&AccessState->RemainingDesiredAccess,
                      &ObjectType->TypeInfo.GenericMapping);

    /* Creating an object settles for the bits its type defines; asking to
     * open one with any other bit is refused */
    if (!DropExcess &&
        (AccessState->RemainingDesiredAccess &
         ~(ObjectType->TypeInfo.ValidAccessMask | ACCESS_SYSTEM_SECURITY)))
    {
        return STATUS_ACCESS_DENIED;
    }

    /* A descriptor whose system ACL audits anything can only be acted on by a
     * caller allowed to see that ACL */
    Descriptor = AccessState->SecurityDescriptor;
    if (Descriptor != NULL &&
        (Descriptor->Control & SE_SACL_PRESENT) &&
        ObpSaclHasAuditAces(Descriptor) &&
        !(AccessState->PreviouslyGrantedAccess & ACCESS_SYSTEM_SECURITY))
    {
        AccessState->RemainingDesiredAccess |= ACCESS_SYSTEM_SECURITY;
    }

    return STATUS_SUCCESS;
}

/*
 * The create information is only needed until the object is named and secured.
 * Letting it go is what turns a new object into a live one.
 */
static
NTSTATUS
ObpChargeQuotaForObject(
    _Inout_ POBJECT_HEADER ObjectHeader,
    _In_ POBJECT_TYPE ObjectType,
    _Out_ PBOOLEAN NewObject)
{
    POBJECT_CREATE_INFORMATION ObjectCreateInfo = NULL;

    UNREFERENCED_PARAMETER(ObjectType);

    *NewObject = FALSE;

    if (ObjectHeader->Flags & OB_FLAG_CREATE_INFO)
    {
        ObjectCreateInfo = ObjectHeader->ObjectCreateInfo;
        ObjectHeader->Flags &= ~OB_FLAG_CREATE_INFO;
        ObjectHeader->QuotaBlockCharged = NULL;
        *NewObject = TRUE;
    }

    if (ObjectCreateInfo != NULL)
        ObpFreeObjectCreateInformation(ObjectCreateInfo);

    /* TODO: the pool charges the create information carried are not billed to
     * the process quota block */
    return STATUS_SUCCESS;
}

/* THE HANDLE DATABASE ********************************************************/

/*
 * A type that maintains a handle count needs to know how many handles each
 * process holds, which is one entry inline until a second process arrives.
 */
static
NTSTATUS
ObpIncrementHandleDataBase(
    _In_ POBJECT_HEADER ObjectHeader,
    _In_ PEPROCESS Process,
    _Out_ PULONG NewCount)
{
    POBJECT_HEADER_HANDLE_INFO HandleInfo;
    POBJECT_HANDLE_COUNT_DATABASE Database;
    POBJECT_HANDLE_COUNT_ENTRY Entry;
    POBJECT_HANDLE_COUNT_ENTRY Free = NULL;
    ULONG Index, Entries, Bytes;

    HandleInfo = OBJECT_HEADER_TO_HANDLE_INFO(ObjectHeader);
    if (HandleInfo == NULL) return STATUS_INSUFFICIENT_RESOURCES;

    /* One process is recorded in the header itself */
    if (ObjectHeader->Flags & OB_FLAG_SINGLE_PROCESS)
    {
        if (HandleInfo->SingleEntry.Process == Process ||
            HandleInfo->SingleEntry.HandleCount == 0)
        {
            HandleInfo->SingleEntry.Process = Process;
            HandleInfo->SingleEntry.HandleCount++;
            *NewCount = HandleInfo->SingleEntry.HandleCount;
            return STATUS_SUCCESS;
        }

        /* A second process needs a database of its own */
        Entries = OBP_HANDLE_DATABASE_STEP;
        Bytes = FIELD_OFFSET(OBJECT_HANDLE_COUNT_DATABASE, HandleCountEntries) +
                Entries * sizeof(OBJECT_HANDLE_COUNT_ENTRY);

        Database = ExAllocatePoolWithTag(PagedPool, Bytes, TAG_OB_HANDLE);
        if (Database == NULL) return STATUS_INSUFFICIENT_RESOURCES;

        RtlZeroMemory(Database, Bytes);
        Database->CountEntries = Entries;
        Database->HandleCountEntries[0] = HandleInfo->SingleEntry;
        Database->HandleCountEntries[1].Process = Process;
        Database->HandleCountEntries[1].HandleCount = 1;

        HandleInfo->HandleCountDatabase = Database;
        ObjectHeader->Flags &= ~OB_FLAG_SINGLE_PROCESS;

        *NewCount = 1;
        return STATUS_SUCCESS;
    }

    Database = HandleInfo->HandleCountDatabase;
    if (Database == NULL) return STATUS_INSUFFICIENT_RESOURCES;

    for (Index = 0; Index < Database->CountEntries; Index++)
    {
        Entry = &Database->HandleCountEntries[Index];

        if (Entry->Process == Process)
        {
            Entry->HandleCount++;
            *NewCount = Entry->HandleCount;
            return STATUS_SUCCESS;
        }

        if (Free == NULL && Entry->HandleCount == 0) Free = Entry;
    }

    if (Free == NULL)
    {
        /* Every entry is taken, so the database grows by one step */
        Entries = Database->CountEntries + OBP_HANDLE_DATABASE_STEP;
        Bytes = FIELD_OFFSET(OBJECT_HANDLE_COUNT_DATABASE, HandleCountEntries) +
                Entries * sizeof(OBJECT_HANDLE_COUNT_ENTRY);

        Database = ExAllocatePoolWithTag(PagedPool, Bytes, TAG_OB_HANDLE);
        if (Database == NULL) return STATUS_INSUFFICIENT_RESOURCES;

        RtlZeroMemory(Database, Bytes);
        RtlCopyMemory(Database,
                      HandleInfo->HandleCountDatabase,
                      FIELD_OFFSET(OBJECT_HANDLE_COUNT_DATABASE,
                                   HandleCountEntries) +
                      HandleInfo->HandleCountDatabase->CountEntries *
                      sizeof(OBJECT_HANDLE_COUNT_ENTRY));

        Free = &Database->HandleCountEntries[Database->CountEntries];
        Database->CountEntries = Entries;

        ExFreePoolWithTag(HandleInfo->HandleCountDatabase, TAG_OB_HANDLE);
        HandleInfo->HandleCountDatabase = Database;
    }

    Free->Process = Process;
    Free->HandleCount = 1;
    *NewCount = 1;

    return STATUS_SUCCESS;
}

/* Gives one handle back to the process that held it. */
static
ULONG
ObpDecrementHandleDataBase(
    _In_ POBJECT_HEADER ObjectHeader,
    _In_ PEPROCESS Process)
{
    POBJECT_HEADER_HANDLE_INFO HandleInfo;
    POBJECT_HANDLE_COUNT_DATABASE Database;
    POBJECT_HANDLE_COUNT_ENTRY Entry;
    ULONG Index;

    HandleInfo = OBJECT_HEADER_TO_HANDLE_INFO(ObjectHeader);
    if (HandleInfo == NULL) return 0;

    if (ObjectHeader->Flags & OB_FLAG_SINGLE_PROCESS)
    {
        if (HandleInfo->SingleEntry.Process != Process) return 0;
        if (HandleInfo->SingleEntry.HandleCount == 0) return 0;

        return HandleInfo->SingleEntry.HandleCount--;
    }

    Database = HandleInfo->HandleCountDatabase;
    if (Database == NULL) return 0;

    for (Index = 0; Index < Database->CountEntries; Index++)
    {
        Entry = &Database->HandleCountEntries[Index];
        if (Entry->Process != Process || Entry->HandleCount == 0) continue;

        Index = Entry->HandleCount--;
        if (Entry->HandleCount == 0) Entry->Process = NULL;

        return Index;
    }

    return 0;
}

static
VOID
ObpFreeHandleDataBase(
    _In_ POBJECT_HEADER ObjectHeader)
{
    POBJECT_HEADER_HANDLE_INFO HandleInfo =
        OBJECT_HEADER_TO_HANDLE_INFO(ObjectHeader);

    if (HandleInfo == NULL) return;
    if (ObjectHeader->Flags & OB_FLAG_SINGLE_PROCESS) return;

    if (HandleInfo->HandleCountDatabase != NULL)
    {
        ExFreePoolWithTag(HandleInfo->HandleCountDatabase, TAG_OB_HANDLE);
        HandleInfo->HandleCountDatabase = NULL;
    }
}

/* CARRIED-OVER NT10 HELPERS **************************************************/

typedef struct _OBP_GRANTED_ACCESS_DATA
{
    POBJECT_HEADER ObjectHeader;
    ACCESS_MASK GrantedAccess;
    BOOLEAN Found;
} OBP_GRANTED_ACCESS_DATA, *POBP_GRANTED_ACCESS_DATA;

static
VOID
ObpDecrementHandleCount(
    _In_ PVOID Object,
    _In_opt_ PEPROCESS Process,
    _In_ POBJECT_TYPE ObjectType);

typedef struct _OBP_INHERIT_HANDLE_CONTEXT
{
    PEPROCESS Process;
    PHANDLE_TABLE HandleTable;
    const HANDLE *HandleList;
    SIZE_T HandleCount;
} OBP_INHERIT_HANDLE_CONTEXT, *POBP_INHERIT_HANDLE_CONTEXT;

static
VOID
NTAPI
ObpChargeHandleCachedReferencesLocked(IN PHANDLE_TABLE HandleTable,
                                      IN HANDLE Handle,
                                      IN PHANDLE_TABLE_ENTRY HandleEntry,
                                      IN POBJECT_HEADER ObjectHeader)
{
#if (NTDDI_VERSION >= NTDDI_WIN8)
    PULONG CachedReferenceCount;

    CachedReferenceCount = ExGetHandleCachedReferenceCount(HandleTable, Handle, HandleEntry);
    ASSERT(CachedReferenceCount != NULL);
    if (CachedReferenceCount)
    {
        ASSERT(*CachedReferenceCount == 0);
        InterlockedExchangeAddSizeT(&ObjectHeader->PointerCount, OBP_HANDLE_REFERENCE_INCREMENT);
        *CachedReferenceCount = OBP_HANDLE_REFERENCE_INCREMENT;
    }
#else
    UNREFERENCED_PARAMETER(HandleTable);
    UNREFERENCED_PARAMETER(Handle);
    UNREFERENCED_PARAMETER(HandleEntry);
    UNREFERENCED_PARAMETER(ObjectHeader);
#endif
}

static
VOID
NTAPI
ObpChargeHandleCachedReferences(IN PHANDLE_TABLE HandleTable,
                                IN HANDLE Handle,
                                IN POBJECT_HEADER ObjectHeader)
{
#if (NTDDI_VERSION >= NTDDI_WIN8)
    PHANDLE_TABLE_ENTRY HandleEntry;

    PAGED_CODE();
    KeEnterCriticalRegion();
    HandleEntry = ExMapHandleToPointer(HandleTable, Handle);
    ASSERT(HandleEntry != NULL);
    if (HandleEntry)
    {
        ObpChargeHandleCachedReferencesLocked(HandleTable, Handle, HandleEntry, ObjectHeader);
        ExUnlockHandleTableEntry(HandleTable, HandleEntry);
    }
    KeLeaveCriticalRegion();
#else
    UNREFERENCED_PARAMETER(HandleTable);
    UNREFERENCED_PARAMETER(Handle);
    UNREFERENCED_PARAMETER(ObjectHeader);
#endif
}

VOID
NTAPI
ObpReferenceObjectByHandleEntry(IN PHANDLE_TABLE HandleTable,
                                IN HANDLE Handle,
                                IN PHANDLE_TABLE_ENTRY HandleEntry,
                                IN POBJECT_HEADER ObjectHeader)
{
#if (NTDDI_VERSION >= NTDDI_WIN8)
    PULONG CachedReferenceCount;

    CachedReferenceCount = ExGetHandleCachedReferenceCount(HandleTable, Handle, HandleEntry);
    ASSERT(CachedReferenceCount != NULL);
    if (CachedReferenceCount)
    {
        if (*CachedReferenceCount == 0)
        {
            InterlockedExchangeAddSizeT(&ObjectHeader->PointerCount, OBP_HANDLE_REFERENCE_INCREMENT);
            *CachedReferenceCount = OBP_HANDLE_REFERENCE_INCREMENT;
        }

        (*CachedReferenceCount)--;
        return;
    }
#else
    UNREFERENCED_PARAMETER(HandleTable);
    UNREFERENCED_PARAMETER(Handle);
    UNREFERENCED_PARAMETER(HandleEntry);
#endif

    InterlockedIncrementSizeT(&ObjectHeader->PointerCount);
}

static
VOID
NTAPI
ObpReleaseHandleCachedReferences(IN PHANDLE_TABLE HandleTable,
                                 IN HANDLE Handle,
                                 IN PHANDLE_TABLE_ENTRY HandleEntry,
                                 IN POBJECT_HEADER ObjectHeader)
{
#if (NTDDI_VERSION >= NTDDI_WIN8)
    PULONG CachedReferenceCount;
    ULONG RemainingReferences;

    CachedReferenceCount = ExGetHandleCachedReferenceCount(HandleTable, Handle, HandleEntry);
    ASSERT(CachedReferenceCount != NULL);
    if (CachedReferenceCount)
    {
        RemainingReferences = *CachedReferenceCount;
        *CachedReferenceCount = 0;
        if (RemainingReferences) InterlockedExchangeAddSizeT(&ObjectHeader->PointerCount, -(LONG_PTR)RemainingReferences);
    }
#else
    UNREFERENCED_PARAMETER(HandleTable);
    UNREFERENCED_PARAMETER(Handle);
    UNREFERENCED_PARAMETER(HandleEntry);
    UNREFERENCED_PARAMETER(ObjectHeader);
#endif
}

static
BOOLEAN
ObpSaclHasAuditAces(IN PISECURITY_DESCRIPTOR SecurityDescriptor)
{
    PACL Sacl;
    PACE_HEADER Ace;
    ULONG Index;

    Sacl = SepGetSaclFromDescriptor(SecurityDescriptor);
    if (!Sacl) return FALSE;
    for (Index = 0; Index < Sacl->AceCount; Index++)
    {
        if (!NT_SUCCESS(RtlGetAce(Sacl, Index, (PVOID*)&Ace))) break;
        if (Ace->AceType != SYSTEM_MANDATORY_LABEL_ACE_TYPE) return TRUE;
    }
    return FALSE;
}

/*++
* @name ObpRollbackHandleCount
*
*     Undo the bookkeeping performed before an object type open callback.
*
*--*/
static
VOID
ObpRollbackHandleCount(
    _In_ POBJECT_HEADER ObjectHeader,
    _In_ POBJECT_TYPE ObjectType,
    _In_ PEPROCESS Process)
{
    POBJECT_HEADER_HANDLE_INFO HandleInfo;
    POBJECT_HANDLE_COUNT_DATABASE HandleDatabase;
    POBJECT_HANDLE_COUNT_ENTRY HandleEntry = NULL;
    LONG NewCount;
    ULONG i;

    ObpAcquireObjectLock(ObjectHeader);
    NewCount = InterlockedDecrementSizeT(&ObjectHeader->HandleCount);
    if (!(NewCount) && (ObjectHeader->Flags & OB_FLAG_EXCLUSIVE)) OBJECT_HEADER_TO_PROCESS_INFO(ObjectHeader)->ExclusiveProcess = NULL;

    if (ObjectType->TypeInfo.MaintainHandleCount)
    {
        HandleInfo = OBJECT_HEADER_TO_HANDLE_INFO(ObjectHeader);
        if (ObjectHeader->Flags & OB_FLAG_SINGLE_PROCESS)
        {
            ASSERT(HandleInfo->SingleEntry.Process == Process);
            HandleEntry = &HandleInfo->SingleEntry;
        }
        else
        {
            HandleDatabase = HandleInfo->HandleCountDatabase;
            if (HandleDatabase)
            {
                HandleEntry = &HandleDatabase->HandleCountEntries[0];
                for (i = 0; i < HandleDatabase->CountEntries; i++, HandleEntry++)
                {
                    if (HandleEntry->HandleCount && HandleEntry->Process == Process) break;
                }
                if (i == HandleDatabase->CountEntries) HandleEntry = NULL;
            }
        }

        ASSERT(HandleEntry != NULL);
        ASSERT(!HandleEntry || HandleEntry->HandleCount != 0);
        if (HandleEntry && HandleEntry->HandleCount && !--HandleEntry->HandleCount) HandleEntry->Process = NULL;
    }

    ObpReleaseObjectLock(ObjectHeader);
}

static
BOOLEAN
NTAPI
ObpChargeInheritedHandleCallback(IN PHANDLE_TABLE_ENTRY HandleEntry,
                                 IN HANDLE Handle,
                                 IN PVOID Context)
{
    PHANDLE_TABLE HandleTable = Context;
    POBJECT_HEADER ObjectHeader;

    ObjectHeader = ObpGetHandleObject(HandleEntry);
    ObpChargeHandleCachedReferencesLocked(HandleTable, Handle, HandleEntry, ObjectHeader);
    return FALSE;
}

static
BOOLEAN
NTAPI
ObpFilterInheritedHandleCallback(
    IN PHANDLE_TABLE_ENTRY HandleEntry,
    IN HANDLE Handle,
    IN PVOID Context)
{
    POBP_INHERIT_HANDLE_CONTEXT FilterContext = Context;
    POBJECT_HEADER ObjectHeader;
    POBJECT_TYPE ObjectType;
    PVOID Object;
    SIZE_T Index;

    for (Index = 0; Index < FilterContext->HandleCount; Index++)
    {
        if (FilterContext->HandleList[Index] == Handle)
        {
            ExUnlockHandleTableEntry(FilterContext->HandleTable, HandleEntry);
            return FALSE;
        }
    }

    ObjectHeader = ObpGetHandleObject(HandleEntry);
    ObjectType = ObpGetObjectTypeFromHeader(ObjectHeader);
    Object = &ObjectHeader->Body;
    ExDestroyHandle(FilterContext->HandleTable, Handle, HandleEntry);
    ObpDecrementHandleCount(Object, FilterContext->Process, ObjectType);
    ObDereferenceObject(Object);
    return TRUE;
}

static
BOOLEAN
NTAPI
ObpEnumGrantedAccessProcedure(IN PHANDLE_TABLE_ENTRY HandleEntry,
                              IN HANDLE Handle,
                              IN PVOID Context)
{
    POBP_GRANTED_ACCESS_DATA Data = Context;

    if (ObpGetHandleObject(HandleEntry) == Data->ObjectHeader)
    {
        Data->GrantedAccess |= HandleEntry->GrantedAccess & ~ObpAccessProtectCloseBit;
        Data->Found = TRUE;
    }
    return FALSE;
}

BOOLEAN
NTAPI
ObpQueryProcessGrantedAccess(IN PEPROCESS Process,
                             IN PVOID Object,
                             OUT PACCESS_MASK GrantedAccess)
{
    OBP_GRANTED_ACCESS_DATA Data;
    PHANDLE_TABLE ObjectTable;

    PAGED_CODE();

    Data.ObjectHeader = OBJECT_TO_OBJECT_HEADER(Object);
    Data.GrantedAccess = 0;
    Data.Found = FALSE;

    ObjectTable = ObReferenceProcessHandleTable(Process);
    if (ObjectTable)
    {
        ExEnumHandleTable(ObjectTable, ObpEnumGrantedAccessProcedure, &Data, NULL);
        ObDereferenceProcessHandleTable(Process);
    }

    *GrantedAccess = Data.GrantedAccess;
    return Data.Found;
}

NTSTATUS
NTAPI
NtCompareObjects(IN HANDLE FirstObjectHandle,
                 IN HANDLE SecondObjectHandle)
{
    KPROCESSOR_MODE PreviousMode = ExGetPreviousMode();
    PVOID FirstObject, SecondObject;
    NTSTATUS Status;

    PAGED_CODE();

    Status = ObReferenceObjectByHandle(FirstObjectHandle, 0, NULL, PreviousMode, &FirstObject, NULL);
    if (!NT_SUCCESS(Status))
        return Status;

    Status = ObReferenceObjectByHandle(SecondObjectHandle, 0, NULL, PreviousMode, &SecondObject, NULL);
    if (NT_SUCCESS(Status))
    {
        Status = FirstObject == SecondObject ? STATUS_SUCCESS : STATUS_NOT_SAME_OBJECT;
        ObDereferenceObject(SecondObject);
    }

    ObDereferenceObject(FirstObject);
    return Status;
}

/* HANDLE COUNTS **************************************************************/

/*
 * Everything a new handle has to be allowed and accounted for happens here:
 * the exclusive rule, the access check, the counts, the per-process database
 * and finally the type's own say in the matter.
 */
static
NTSTATUS
ObpIncrementHandleCount(
    _In_ PVOID Object,
    _In_opt_ PACCESS_STATE AccessState,
    _In_ KPROCESSOR_MODE AccessMode,
    _In_ ULONG HandleAttributes,
    _In_opt_ PEPROCESS Process,
    _In_ OB_OPEN_REASON OpenReason,
    _In_ BOOLEAN CheckObjectAccess)
{
    POBJECT_HEADER Header = OBJECT_TO_OBJECT_HEADER(Object);
    POBJECT_TYPE ObjectType = ObpGetObjectTypeFromHeader(Header);
    POBJECT_HEADER_PROCESS_INFO ProcessInfo;
    PEPROCESS Self = PsGetCurrentProcess();
    NTSTATUS Status, AccessStatus;
    ULONG ProcessHandleCount = 0;
    ACCESS_MASK Granted = 0;
    KIRQL CalloutIrql;

    PAGED_CODE();

    /*
     * The access check comes first and without the object lock, because
     * reading what an object is secured with takes that same lock.
     */
    if (AccessState != NULL)
    {
        Status = ObpValidateAccessMask(ObjectType,
                                       AccessState,
                                       (BOOLEAN)(OpenReason == ObCreateHandle));
        if (!NT_SUCCESS(Status)) return Status;

        if (OpenReason == ObCreateHandle)
        {
            /*
             * Whoever creates an object is given the access it asked for; the
             * descriptor it supplied governs the opens that come after, so the
             * only thing filtered here is what the type does not define.
             */
            /* TODO: no object-creation audit alarm is raised */
            AccessState->PreviouslyGrantedAccess =
                (AccessState->RemainingDesiredAccess |
                 AccessState->PreviouslyGrantedAccess) &
                (ObjectType->TypeInfo.ValidAccessMask | ACCESS_SYSTEM_SECURITY);
            AccessState->RemainingDesiredAccess = 0;
        }
        else if (AccessMode != KernelMode && CheckObjectAccess &&
                 AccessState->RemainingDesiredAccess != 0)
        {
            /*
             * Only what is still outstanding is checked, and only for an
             * object the namespace walk resolved itself: a parse routine
             * answers for what it returns, and a zero mask is refused.
             */
            if (!ObCheckObjectAccess(Object,
                                     AccessState,
                                     FALSE,
                                     AccessMode,
                                     &AccessStatus))
            {
                return AccessStatus;
            }
        }
        else
        {
            /* The kernel, and whatever a parse routine allowed, is given
             * what it asked for */
            AccessState->PreviouslyGrantedAccess |=
                AccessState->RemainingDesiredAccess;
            AccessState->RemainingDesiredAccess = 0;
        }

        Granted = AccessState->PreviouslyGrantedAccess;
    }

    ObpAcquireObjectLock(Header);

    /* An exclusive object answers to one process only */
    if (Header->Flags & OB_FLAG_EXCLUSIVE)
    {
        ProcessInfo = OBJECT_HEADER_TO_PROCESS_INFO(Header);
        if (ProcessInfo == NULL ||
            (ProcessInfo->ExclusiveProcess != NULL &&
             ProcessInfo->ExclusiveProcess != Self))
        {
            ObpReleaseObjectLock(Header);
            return STATUS_ACCESS_DENIED;
        }
    }
    else if (HandleAttributes & OBJ_EXCLUSIVE)
    {
        ObpReleaseObjectLock(Header);
        return STATUS_INVALID_PARAMETER;
    }

    InterlockedIncrementSizeT(&Header->HandleCount);

    InterlockedIncrement((PLONG)&ObjectType->TotalNumberOfHandles);
    if (ObjectType->TotalNumberOfHandles > ObjectType->HighWaterNumberOfHandles)
        ObjectType->HighWaterNumberOfHandles = ObjectType->TotalNumberOfHandles;

    if (ObjectType->TypeInfo.MaintainHandleCount && Process != NULL)
    {
        Status = ObpIncrementHandleDataBase(Header,
                                            Process,
                                            &ProcessHandleCount);
        if (!NT_SUCCESS(Status))
        {
            ObpReleaseObjectLock(Header);
            ObpRollbackHandleCount(Header, ObjectType, Process);
            return Status;
        }
    }

    /* The first handle settles who owns an exclusive object */
    if (Header->Flags & OB_FLAG_EXCLUSIVE)
        OBJECT_HEADER_TO_PROCESS_INFO(Header)->ExclusiveProcess = Self;

    ObpReleaseObjectLock(Header);

    /* The type gets the last word, and may narrow what was granted */
    if (ObjectType->TypeInfo.OpenProcedure != NULL)
    {
        ObpCalloutStart(&CalloutIrql);
        Status = ObjectType->TypeInfo.OpenProcedure(OpenReason,
                                                    AccessMode,
                                                    Process,
                                                    Object,
                                                    &Granted,
                                                    ProcessHandleCount);
        ObpCalloutEnd(CalloutIrql, "Open", ObjectType, Object);

        if (!NT_SUCCESS(Status))
        {
            ObpRollbackHandleCount(Header, ObjectType, Process);
            return Status;
        }
    }

    if (AccessState != NULL) AccessState->PreviouslyGrantedAccess = Granted;

    return STATUS_SUCCESS;
}

static
NTSTATUS
ObpIncrementUnnamedHandleCount(
    _In_ PVOID Object,
    _Inout_ PACCESS_MASK DesiredAccess,
    _In_ KPROCESSOR_MODE AccessMode,
    _In_ ULONG HandleAttributes,
    _In_opt_ PEPROCESS Process,
    _In_ OB_OPEN_REASON OpenReason)
{
    POBJECT_HEADER Header = OBJECT_TO_OBJECT_HEADER(Object);
    POBJECT_TYPE ObjectType = ObpGetObjectTypeFromHeader(Header);
    ACCESS_STATE AccessState;
    AUX_ACCESS_DATA AuxData;
    NTSTATUS Status;

    PAGED_CODE();

    /* An object nobody named is opened against an access state of its own */
    Status = SeCreateAccessState(&AccessState,
                                 &AuxData,
                                 *DesiredAccess,
                                 &ObjectType->TypeInfo.GenericMapping);
    if (!NT_SUCCESS(Status)) return Status;

    Status = ObpIncrementHandleCount(Object,
                                     &AccessState,
                                     AccessMode,
                                     HandleAttributes,
                                     Process,
                                     OpenReason,
                                     TRUE);
    if (NT_SUCCESS(Status))
        *DesiredAccess = AccessState.PreviouslyGrantedAccess;

    SeDeleteAccessState(&AccessState);
    return Status;
}

/*
 * Gives one handle back. The type is told the counts the closing handle is
 * still part of, so the last handle reaches it as one, and the name goes with
 * that handle unless the object is permanent.
 */
static
VOID
ObpDecrementHandleCount(
    _In_ PVOID Object,
    _In_opt_ PEPROCESS Process,
    _In_ POBJECT_TYPE ObjectType)
{
    POBJECT_HEADER Header = OBJECT_TO_OBJECT_HEADER(Object);
    POBJECT_HEADER_PROCESS_INFO ProcessInfo;
    ULONG_PTR ProcessHandleCount = 0;
    LONG_PTR SystemHandleCount, Remaining;
    KIRQL CalloutIrql;

    ObpAcquireObjectLock(Header);

    SystemHandleCount = Header->HandleCount;
    Remaining = SystemHandleCount;
    if (SystemHandleCount > 0)
        Remaining = InterlockedDecrementSizeT(&Header->HandleCount);

    if (ObjectType->TypeInfo.MaintainHandleCount && Process != NULL)
        ProcessHandleCount = ObpDecrementHandleDataBase(Header, Process);

    /* The last handle releases the claim an exclusive object had */
    if ((Header->Flags & OB_FLAG_EXCLUSIVE) && Remaining == 0)
    {
        ProcessInfo = OBJECT_HEADER_TO_PROCESS_INFO(Header);
        if (ProcessInfo != NULL) ProcessInfo->ExclusiveProcess = NULL;
    }

    if (Remaining == 0) ObpFreeHandleDataBase(Header);

    ObpReleaseObjectLock(Header);

    InterlockedDecrement((PLONG)&ObjectType->TotalNumberOfHandles);

    if (ObjectType->TypeInfo.CloseProcedure != NULL)
    {
        ObpCalloutStart(&CalloutIrql);
        ObjectType->TypeInfo.CloseProcedure(Process,
                                            Object,
                                            ProcessHandleCount,
                                            (ULONG_PTR)SystemHandleCount);
        ObpCalloutEnd(CalloutIrql, "Close", ObjectType, Object);
    }

    ObpDeleteNameCheck(Object);
}

/* CREATING AND CLOSING HANDLES ***********************************************/

static
PHANDLE_TABLE
ObpTableForNewHandle(
    _In_ ULONG HandleAttributes,
    _In_ KPROCESSOR_MODE AccessMode,
    _Out_ PBOOLEAN KernelHandle)
{
    *KernelHandle = FALSE;

    /* Only the kernel may ask for a handle of its own */
    if ((HandleAttributes & OBJ_KERNEL_HANDLE) && AccessMode == KernelMode)
    {
        *KernelHandle = TRUE;
        return ObpKernelHandleTable;
    }

    return PsGetCurrentProcess()->ObjectTable;
}

static
NTSTATUS
ObpCreateHandleWorker(
    _In_ OB_OPEN_REASON OpenReason,
    _In_ PVOID Object,
    _Inout_ PACCESS_STATE AccessState,
    _In_ ULONG AdditionalReferences,
    _In_ ULONG HandleAttributes,
    _In_opt_ POBP_LOOKUP_CONTEXT Context,
    _In_ KPROCESSOR_MODE AccessMode,
    _Outptr_opt_ PVOID *ReturnedObject,
    _Out_ PHANDLE ReturnedHandle)
{
    POBJECT_HEADER Header = OBJECT_TO_OBJECT_HEADER(Object);
    POBJECT_TYPE FoundType = ObpGetObjectTypeFromHeader(Header);
    HANDLE_TABLE_ENTRY NewEntry;
    PHANDLE_TABLE Table;
    BOOLEAN KernelHandle;
    NTSTATUS Status;
    HANDLE Handle;

    PAGED_CODE();

    /* The walk parks the entry it resolved; anything else came from a parse */
    Status = ObpIncrementHandleCount(Object,
                                      AccessState,
                                      AccessMode,
                                      HandleAttributes,
                                      PsGetCurrentProcess(),
                                      OpenReason,
                                      (BOOLEAN)(Context == NULL ||
                                                Context->Object == Object));
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    /* The entry keeps the object with its attributes folded into the low bits
     * the allocation alignment leaves free */
    NewEntry.Object = Header;
    NewEntry.ObAttributes |= (HandleAttributes & OBJ_HANDLE_ATTRIBUTES);
    if (AccessState->GenerateOnClose)
        NewEntry.ObAttributes |= OBJ_AUDIT_OBJECT_CLOSE;
    NewEntry.GrantedAccess = AccessState->PreviouslyGrantedAccess;

    /* Only what the caller asked for is charged here: the batch a handle
     * lends out is charged with the handle itself, below */
    if (AdditionalReferences != 0)
    {
        InterlockedExchangeAddSizeT(&Header->PointerCount,
                                    AdditionalReferences);
    }

    Table = ObpTableForNewHandle(HandleAttributes, AccessMode, &KernelHandle);
    if (Table == NULL)
    {
        InterlockedExchangeAddSizeT(&Header->PointerCount,
                                    -(LONG_PTR)AdditionalReferences);
        ObpRollbackHandleCount(Header, FoundType, PsGetCurrentProcess());
        return STATUS_PROCESS_IS_TERMINATING;
    }

    Handle = ExCreateHandle(Table, &NewEntry);
    if (Handle == NULL)
    {
        InterlockedExchangeAddSizeT(&Header->PointerCount,
                                    -(LONG_PTR)AdditionalReferences);
        ObpRollbackHandleCount(Header, FoundType, PsGetCurrentProcess());
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    ObpChargeHandleCachedReferences(Table, Handle, Header);

    if (KernelHandle) Handle = ObMarkHandleAsKernelHandle(Handle);

    /* Whatever the lookup was holding has served its purpose */
    if (Context != NULL) ObpReleaseLookupContext(Context);

    if (ReturnedObject != NULL) *ReturnedObject = Object;
    *ReturnedHandle = Handle;

    return STATUS_SUCCESS;
}

/*
 * A registered callback is told before the handle is made, may narrow what it
 * will carry, and is told again with the handle's final access.
 */
static
NTSTATUS
ObpCreateHandle(
    _In_ OB_OPEN_REASON OpenReason,
    _In_ PVOID Object,
    _In_opt_ POBJECT_TYPE ObjectType,
    _Inout_ PACCESS_STATE AccessState,
    _In_ ULONG AdditionalReferences,
    _In_ ULONG HandleAttributes,
    _In_opt_ POBP_LOOKUP_CONTEXT Context,
    _In_ KPROCESSOR_MODE AccessMode,
    _Outptr_opt_ PVOID *ReturnedObject,
    _Out_ PHANDLE ReturnedHandle)
{
    POBJECT_TYPE FoundType =
        ObpGetObjectTypeFromHeader(OBJECT_TO_OBJECT_HEADER(Object));
    OBP_HANDLE_CALLBACK_STATE CallbackState;
    BOOLEAN KernelHandle;
    ACCESS_MASK Desired;
    NTSTATUS Status;

    PAGED_CODE();

    if (ObjectType != NULL && ObjectType != FoundType)
        return STATUS_OBJECT_TYPE_MISMATCH;

    Status = ObpBeginHandleCallbacks(&CallbackState,
                                     FoundType,
                                     OB_OPERATION_HANDLE_CREATE);
    if (!NT_SUCCESS(Status)) return Status;

    (VOID)ObpTableForNewHandle(HandleAttributes, AccessMode, &KernelHandle);

    Desired = AccessState->RemainingDesiredAccess |
              AccessState->PreviouslyGrantedAccess;
    ObpCallPreHandleCallbacks(&CallbackState,
                              Object,
                              KernelHandle,
                              &Desired,
                              NULL,
                              PsGetCurrentProcess());
    AccessState->RemainingDesiredAccess &= Desired;
    AccessState->PreviouslyGrantedAccess &= Desired;

    Status = ObpCreateHandleWorker(OpenReason,
                                   Object,
                                   AccessState,
                                   AdditionalReferences,
                                   HandleAttributes,
                                   Context,
                                   AccessMode,
                                   ReturnedObject,
                                   ReturnedHandle);

    ObpEndHandleCallbacks(&CallbackState,
                          Status,
                          NT_SUCCESS(Status) ?
                          AccessState->PreviouslyGrantedAccess : 0);

    return Status;
}

static
NTSTATUS
ObpCreateUnnamedHandleWorker(
    _In_ PVOID Object,
    _Inout_ PACCESS_MASK GrantedAccess,
    _In_ ULONG AdditionalReferences,
    _In_ ULONG HandleAttributes,
    _In_ KPROCESSOR_MODE AccessMode,
    _In_ OB_OPEN_REASON OpenReason,
    _Outptr_opt_ PVOID *ReturnedObject,
    _Out_ PHANDLE ReturnedHandle)
{
    POBJECT_HEADER Header = OBJECT_TO_OBJECT_HEADER(Object);
    ACCESS_MASK Granted = *GrantedAccess;
    HANDLE_TABLE_ENTRY NewEntry;
    PHANDLE_TABLE Table;
    BOOLEAN KernelHandle;
    NTSTATUS Status;
    HANDLE Handle;

    PAGED_CODE();

    Status = ObpIncrementUnnamedHandleCount(Object,
                                             &Granted,
                                             AccessMode,
                                             HandleAttributes,
                                             PsGetCurrentProcess(),
                                             OpenReason);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    *GrantedAccess = Granted;

    NewEntry.Object = Header;
    NewEntry.ObAttributes |= (HandleAttributes & OBJ_HANDLE_ATTRIBUTES);
    NewEntry.GrantedAccess = Granted;

    if (AdditionalReferences != 0)
    {
        InterlockedExchangeAddSizeT(&Header->PointerCount,
                                    AdditionalReferences);
    }

    Table = ObpTableForNewHandle(HandleAttributes, AccessMode, &KernelHandle);
    Handle = (Table != NULL) ? ExCreateHandle(Table, &NewEntry) : NULL;

    if (Handle == NULL)
    {
        InterlockedExchangeAddSizeT(&Header->PointerCount,
                                    -(LONG_PTR)AdditionalReferences);
        ObpRollbackHandleCount(Header,
                               ObpGetObjectTypeFromHeader(Header),
                               PsGetCurrentProcess());
        return (Table != NULL) ? STATUS_INSUFFICIENT_RESOURCES :
                                 STATUS_PROCESS_IS_TERMINATING;
    }

    ObpChargeHandleCachedReferences(Table, Handle, Header);

    if (KernelHandle) Handle = ObMarkHandleAsKernelHandle(Handle);

    if (ReturnedObject != NULL) *ReturnedObject = Object;
    *ReturnedHandle = Handle;

    return STATUS_SUCCESS;
}

static
NTSTATUS
ObpCreateUnnamedHandle(
    _In_ PVOID Object,
    _In_ ACCESS_MASK DesiredAccess,
    _In_ ULONG AdditionalReferences,
    _In_ ULONG HandleAttributes,
    _In_ KPROCESSOR_MODE AccessMode,
    _In_ OB_OPEN_REASON OpenReason,
    _Outptr_opt_ PVOID *ReturnedObject,
    _Out_ PHANDLE ReturnedHandle)
{
    POBJECT_TYPE ObjectType =
        ObpGetObjectTypeFromHeader(OBJECT_TO_OBJECT_HEADER(Object));
    OBP_HANDLE_CALLBACK_STATE CallbackState;
    ACCESS_MASK Granted = DesiredAccess;
    BOOLEAN KernelHandle;
    NTSTATUS Status;

    PAGED_CODE();

    Status = ObpBeginHandleCallbacks(&CallbackState,
                                     ObjectType,
                                     OB_OPERATION_HANDLE_CREATE);
    if (!NT_SUCCESS(Status)) return Status;

    (VOID)ObpTableForNewHandle(HandleAttributes, AccessMode, &KernelHandle);

    ObpCallPreHandleCallbacks(&CallbackState,
                              Object,
                              KernelHandle,
                              &Granted,
                              NULL,
                              PsGetCurrentProcess());

    Status = ObpCreateUnnamedHandleWorker(Object,
                                          &Granted,
                                          AdditionalReferences,
                                          HandleAttributes,
                                          AccessMode,
                                          OpenReason,
                                          ReturnedObject,
                                          ReturnedHandle);

    ObpEndHandleCallbacks(&CallbackState,
                          Status,
                          NT_SUCCESS(Status) ? Granted : 0);

    return Status;
}

/*
 * Takes one handle out of a table. The type may refuse, and a handle the
 * caller asked to be protected refuses on its own behalf.
 */
NTSTATUS
NTAPI
ObpCloseHandleTableEntry(
    _In_ PHANDLE_TABLE HandleTable,
    _In_ PHANDLE_TABLE_ENTRY HandleEntry,
    _In_ HANDLE Handle,
    _In_ KPROCESSOR_MODE AccessMode,
    _In_ BOOLEAN IgnoreHandleProtection)
{
    POBJECT_HEADER Header = ObpGetHandleObject(HandleEntry);
    POBJECT_TYPE ObjectType = ObpGetObjectTypeFromHeader(Header);
    PVOID Body = &Header->Body;
    KIRQL CalloutIrql;

    PAGED_CODE();

    if (ObjectType->TypeInfo.OkayToCloseProcedure != NULL)
    {
        BOOLEAN Okay;

        ObpCalloutStart(&CalloutIrql);
        Okay = ObjectType->TypeInfo.OkayToCloseProcedure(PsGetCurrentProcess(),
                                                         Body,
                                                         Handle,
                                                         AccessMode);
        ObpCalloutEnd(CalloutIrql, "NtClose", ObjectType, Body);

        if (!Okay)
        {
            ExUnlockHandleTableEntry(HandleTable, HandleEntry);
            return STATUS_HANDLE_NOT_CLOSABLE;
        }
    }

    if ((HandleEntry->GrantedAccess & ObpAccessProtectCloseBit) &&
        !IgnoreHandleProtection)
    {
        ExUnlockHandleTableEntry(HandleTable, HandleEntry);

        /* A debugger wants to see this where there is one to tell */
        if (AccessMode != KernelMode && PsGetCurrentProcess()->DebugPort != NULL)
            return KeRaiseUserException(STATUS_HANDLE_NOT_CLOSABLE);

        if (AccessMode != KernelMode) return STATUS_HANDLE_NOT_CLOSABLE;

        ASSERT(FALSE);
        return STATUS_HANDLE_NOT_CLOSABLE;
    }

    /* The batch of references the handle carried goes back while the entry
     * is still there to read them from */
    ObpReleaseHandleCachedReferences(HandleTable, Handle, HandleEntry, Header);

    /* The entry goes next, so nobody can find the handle again */
    ExDestroyHandle(HandleTable, Handle, HandleEntry);

    ObpDecrementHandleCount(Body, PsGetCurrentProcess(), ObjectType);

    /* The reference the open transferred to the handle goes back with it */
    ObDereferenceObject(Body);

    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
ObpCloseHandle(
    _In_ HANDLE Handle,
    _In_ KPROCESSOR_MODE AccessMode)
{
    PHANDLE_TABLE_ENTRY HandleEntry;
    BOOLEAN AttachedToProcess = FALSE;
    PHANDLE_TABLE HandleTable;
    KAPC_STATE ApcState;
    NTSTATUS Status;

    PAGED_CODE();

    /* Closing the current-process pseudo handle does nothing and succeeds */
    /* TODO: the current-thread pseudo handle is still refused, unverified */
    if (Handle == NtCurrentProcess()) return STATUS_SUCCESS;

    if (ObpIsKernelHandle(Handle, AccessMode))
    {
        HandleTable = ObpKernelHandleTable;
        Handle = ObKernelHandleToHandle(Handle);

        /* The kernel table belongs to the system process */
        if (PsGetCurrentProcess() != PsInitialSystemProcess)
        {
            KeStackAttachProcess(&PsInitialSystemProcess->Pcb, &ApcState);
            AttachedToProcess = TRUE;
        }
    }
    else
    {
        HandleTable = PsGetCurrentProcess()->ObjectTable;
    }

    KeEnterCriticalRegion();

    HandleEntry = ExMapHandleToPointer(HandleTable, Handle);
    if (HandleEntry != NULL)
    {
        Status = ObpCloseHandleTableEntry(HandleTable,
                                          HandleEntry,
                                          Handle,
                                          AccessMode,
                                          FALSE);
    }
    else
    {
        Status = STATUS_INVALID_HANDLE;
    }

    KeLeaveCriticalRegion();

    if (AttachedToProcess) KeUnstackDetachProcess(&ApcState);

    if (Status == STATUS_INVALID_HANDLE)
        ObpCheckInvalidHandleReference(Handle, AccessMode);

    return Status;
}

NTSTATUS
NTAPI
ObCloseHandle(
    _In_ HANDLE Handle,
    _In_ KPROCESSOR_MODE AccessMode)
{
    return ObpCloseHandle(Handle, AccessMode);
}

NTSTATUS
NTAPI
NtClose(
    _In_ HANDLE Handle)
{
    return ObpCloseHandle(Handle, ExGetPreviousMode());
}

/* HANDLE ATTRIBUTES **********************************************************/

static
BOOLEAN
NTAPI
ObpSetHandleAttributes(
    _Inout_ PHANDLE_TABLE_ENTRY HandleTableEntry,
    _In_ ULONG_PTR Context)
{
    POBP_SET_HANDLE_ATTRIBUTES_CONTEXT Request = (PVOID)Context;
    POBJECT_HEADER Header;

    if (Request->Information.Inherit)
    {
        /* A type that refuses inheritance refuses the whole change */
        Header = ObpGetHandleObject(HandleTableEntry);
        if (ObpGetObjectTypeFromHeader(Header)->TypeInfo.InvalidAttributes &
            OBJ_INHERIT)
        {
            return FALSE;
        }

        HandleTableEntry->ObAttributes |= OBJ_INHERIT;
    }
    else
    {
        HandleTableEntry->ObAttributes &= ~OBJ_INHERIT;
    }

    /* Protection from close has a bit of the granted access to itself */
    if (Request->Information.ProtectFromClose)
        HandleTableEntry->GrantedAccess |= ObpAccessProtectCloseBit;
    else
        HandleTableEntry->GrantedAccess &= ~ObpAccessProtectCloseBit;

    return TRUE;
}

NTSTATUS
NTAPI
ObSetHandleAttributes(
    _In_ HANDLE Handle,
    _In_ POBJECT_HANDLE_ATTRIBUTE_INFORMATION HandleFlags,
    _In_ KPROCESSOR_MODE PreviousMode)
{
    OBP_SET_HANDLE_ATTRIBUTES_CONTEXT Request;
    BOOLEAN AttachedToProcess = FALSE;
    PHANDLE_TABLE HandleTable;
    KAPC_STATE ApcState;
    BOOLEAN Result;

    PAGED_CODE();

    if (ObpIsKernelHandle(Handle, PreviousMode))
    {
        HandleTable = ObpKernelHandleTable;
        Handle = ObKernelHandleToHandle(Handle);

        if (PsGetCurrentProcess() != PsInitialSystemProcess)
        {
            KeStackAttachProcess(&PsInitialSystemProcess->Pcb, &ApcState);
            AttachedToProcess = TRUE;
        }
    }
    else
    {
        HandleTable = PsGetCurrentProcess()->ObjectTable;
    }

    Request.PreviousMode = PreviousMode;
    Request.Information = *HandleFlags;

    Result = ExChangeHandle(HandleTable,
                            Handle,
                            ObpSetHandleAttributes,
                            (ULONG_PTR)&Request);

    if (AttachedToProcess) KeUnstackDetachProcess(&ApcState);

    return Result ? STATUS_SUCCESS : STATUS_ACCESS_DENIED;
}

/* PROCESS HANDLE TABLE LIFETIME **********************************************/

static
BOOLEAN
NTAPI
ObpCloseHandleCallback(
    _In_ PHANDLE_TABLE_ENTRY HandleTableEntry,
    _In_ HANDLE Handle,
    _In_ PVOID Context)
{
    PHANDLE_TABLE HandleTable = Context;

    /* A table being swept ignores protection: the process is going away */
    ObpCloseHandleTableEntry(HandleTable,
                             HandleTableEntry,
                             Handle,
                             KernelMode,
                             TRUE);

    return TRUE;
}

static
BOOLEAN
NTAPI
ObpDuplicateHandleCallback(
    _In_ PEPROCESS Process,
    _In_ PHANDLE_TABLE HandleTable,
    _In_ PHANDLE_TABLE_ENTRY OldEntry,
    _In_ PHANDLE_TABLE_ENTRY HandleTableEntry)
{
    POBJECT_HEADER Header;
    ACCESS_STATE AccessState;
    NTSTATUS Status;
    BOOLEAN Inheritable;

    PAGED_CODE();

    /* The entry arrives locked, and goes back unlocked whatever we decide */
    Inheritable = ((HandleTableEntry->ObAttributes & OBJ_INHERIT) != 0);
    if (!Inheritable)
    {
        ExUnlockHandleTableEntry(HandleTable, OldEntry);
        return FALSE;
    }

    Header = ObpGetHandleObject(HandleTableEntry);

    /* The copy carries a base reference of its own; the batch it lends out is
     * charged by the walk that follows the duplication */
    InterlockedIncrementSizeT(&Header->PointerCount);

    ExUnlockHandleTableEntry(HandleTable, OldEntry);

    /* Nothing is being asked for beyond what the source handle already had,
     * so the state says so rather than leaving it to the stack */
    RtlZeroMemory(&AccessState, sizeof(AccessState));
    AccessState.PreviouslyGrantedAccess =
        HandleTableEntry->GrantedAccess & ~ObpAccessProtectCloseBit;

    Status = ObpIncrementHandleCount(&Header->Body,
                                     &AccessState,
                                     KernelMode,
                                     HandleTableEntry->ObAttributes &
                                     OBJ_HANDLE_ATTRIBUTES,
                                     Process,
                                     ObInheritHandle,
                                     FALSE);
    if (!NT_SUCCESS(Status))
    {
        ObDereferenceObject(&Header->Body);
        return FALSE;
    }

    HandleTableEntry->GrantedAccess = AccessState.PreviouslyGrantedAccess;
    return TRUE;
}

VOID
NTAPI
ObClearProcessHandleTable(
    _In_ PEPROCESS Process)
{
    PHANDLE_TABLE HandleTable;
    BOOLEAN Attached = FALSE;
    KAPC_STATE ApcState;

    PAGED_CODE();

    HandleTable = ObReferenceProcessHandleTable(Process);
    if (HandleTable == NULL) return;

    if (Process != PsGetCurrentProcess())
    {
        KeStackAttachProcess(&Process->Pcb, &ApcState);
        Attached = TRUE;
    }

    ExSweepHandleTable(HandleTable, ObpCloseHandleCallback, HandleTable);

    if (Attached) KeUnstackDetachProcess(&ApcState);

    ObDereferenceProcessHandleTable(Process);
}

NTSTATUS
NTAPI
ObInitProcess(
    _In_opt_ PEPROCESS Parent,
    _In_ PEPROCESS Process,
    _In_opt_ const HANDLE *HandleList,
    _In_ SIZE_T HandleCount)
{
    OBP_INHERIT_HANDLE_CONTEXT Filter;
    PHANDLE_TABLE ParentTable, NewTable;

    PAGED_CODE();

    /* Without a parent the process starts with an empty table */
    if (Parent == NULL)
    {
        NewTable = ExCreateHandleTable(Process, TRUE);
        if (NewTable == NULL) return STATUS_INSUFFICIENT_RESOURCES;

        Process->ObjectTable = NewTable;
        return STATUS_SUCCESS;
    }

    ParentTable = ObReferenceProcessHandleTable(Parent);
    if (ParentTable == NULL) return STATUS_PROCESS_IS_TERMINATING;

    /* Every inheritable handle comes across first. The parent's table stays
     * referenced until the new one is finished with, because the filter and
     * the charge below still walk entries. */
    NewTable = ExDupHandleTable(Process,
                                ParentTable,
                                ObpDuplicateHandleCallback,
                                OBJ_INHERIT);

    if (NewTable != NULL && HandleList != NULL)
    {
        BOOLEAN StrictFifo = NewTable->StrictFIFO;

        /* A caller that named the handles to inherit has the rest closed
         * again, in order, so the ones it named keep their values */
        Filter.Process = Process;
        Filter.HandleTable = NewTable;
        Filter.HandleList = HandleList;
        Filter.HandleCount = HandleCount;

        NewTable->StrictFIFO = TRUE;
        KeEnterCriticalRegion();
        ExSweepHandleTable(NewTable,
                           ObpFilterInheritedHandleCallback,
                           &Filter);
        KeLeaveCriticalRegion();
        NewTable->StrictFIFO = StrictFifo;
    }

    if (NewTable != NULL)
    {
        ExEnumHandleTable(NewTable,
                          ObpChargeInheritedHandleCallback,
                          NewTable,
                          NULL);
    }

    ObDereferenceProcessHandleTable(Parent);

    if (NewTable == NULL) return STATUS_INSUFFICIENT_RESOURCES;

    Process->ObjectTable = NewTable;
    return STATUS_SUCCESS;
}

VOID
NTAPI
ObKillProcess(
    _In_ PEPROCESS Process)
{
    PHANDLE_TABLE HandleTable = Process->ObjectTable;

    PAGED_CODE();

    /* Nothing may reach the table once rundown has completed */
    ExWaitForRundownProtectionRelease(&Process->RundownProtect);
    ExRundownCompleted(&Process->RundownProtect);

    if (HandleTable == NULL) return;

    /* Locking a table entry is only allowed with APCs disabled */
    KeEnterCriticalRegion();
    ExSweepHandleTable(HandleTable, ObpCloseHandleCallback, HandleTable);
    KeLeaveCriticalRegion();

    ExDestroyHandleTable(HandleTable, NULL);

    Process->ObjectTable = NULL;
}

/* FINDING A HANDLE ***********************************************************/

static
BOOLEAN
NTAPI
ObpEnumFindHandleProcedure(
    _In_ PHANDLE_TABLE_ENTRY HandleEntry,
    _In_ HANDLE Handle,
    _In_ PVOID Context)
{
    POBP_FIND_HANDLE_DATA Match = Context;
    POBJECT_HEADER Header = ObpGetHandleObject(HandleEntry);

    UNREFERENCED_PARAMETER(Handle);

    if (Match->ObjectHeader != NULL && Match->ObjectHeader != Header)
        return FALSE;

    if (Match->ObjectType != NULL &&
        Match->ObjectType != ObpGetObjectTypeFromHeader(Header))
    {
        return FALSE;
    }

    if (Match->HandleInformation != NULL)
    {
        if (Match->HandleInformation->GrantedAccess !=
            (HandleEntry->GrantedAccess & ~ObpAccessProtectCloseBit))
        {
            return FALSE;
        }

        if (Match->HandleInformation->HandleAttributes !=
            (ULONG)(HandleEntry->ObAttributes & OBJ_HANDLE_ATTRIBUTES))
        {
            return FALSE;
        }
    }

    return TRUE;
}

BOOLEAN
NTAPI
ObFindHandleForObject(
    _In_ PEPROCESS Process,
    _In_opt_ PVOID Object,
    _In_opt_ POBJECT_TYPE ObjectType,
    _In_opt_ POBJECT_HANDLE_INFORMATION HandleInformation,
    _Out_opt_ PHANDLE Handle)
{
    OBP_FIND_HANDLE_DATA Match;
    PHANDLE_TABLE HandleTable;
    BOOLEAN Found;

    PAGED_CODE();

    Match.ObjectHeader = (Object != NULL) ?
                         OBJECT_TO_OBJECT_HEADER(Object) : NULL;
    Match.ObjectType = ObjectType;
    Match.HandleInformation = HandleInformation;

    HandleTable = ObReferenceProcessHandleTable(Process);
    if (HandleTable == NULL) return FALSE;

    Found = ExEnumHandleTable(HandleTable,
                              ObpEnumFindHandleProcedure,
                              &Match,
                              Handle);

    ObDereferenceProcessHandleTable(Process);

    return Found;
}

/* OPENING AND INSERTING ******************************************************/

NTSTATUS
NTAPI
ObOpenObjectByPointer(
    _In_ PVOID Object,
    _In_ ULONG HandleAttributes,
    _In_opt_ PACCESS_STATE PassedAccessState,
    _In_ ACCESS_MASK DesiredAccess,
    _In_opt_ POBJECT_TYPE ObjectType,
    _In_ KPROCESSOR_MODE AccessMode,
    _Out_ PHANDLE Handle)
{
    POBJECT_HEADER Header = OBJECT_TO_OBJECT_HEADER(Object);
    PACCESS_STATE AccessState = PassedAccessState;
    POBJECT_TYPE FoundType;
    NTSTATUS Status;

    PAGED_CODE();

    FoundType = ObpGetObjectTypeFromHeader(Header);
    if (ObjectType != NULL && ObjectType != FoundType)
        return STATUS_OBJECT_TYPE_MISMATCH;

    /* The object has to survive the open whatever happens to the caller's
     * own reference */
    ObReferenceObject(Object);

    if (AccessState == NULL)
    {
        /* Nothing named this open, so it gets an access state of its own */
        Status = ObpCreateUnnamedHandle(Object,
                                        DesiredAccess,
                                        0,
                                        HandleAttributes,
                                        AccessMode,
                                        ObOpenHandle,
                                        NULL,
                                        Handle);
        if (!NT_SUCCESS(Status)) ObDereferenceObject(Object);

        return Status;
    }

    Status = ObpCreateHandle(ObOpenHandle,
                             Object,
                             ObjectType,
                             AccessState,
                             0,
                             HandleAttributes,
                             NULL,
                             AccessMode,
                             NULL,
                             Handle);

    /* The reference taken above is the handle's from here on */
    if (!NT_SUCCESS(Status)) ObDereferenceObject(Object);

    return Status;
}

NTSTATUS
NTAPI
ObOpenObjectByName(
    _In_ POBJECT_ATTRIBUTES ObjectAttributes,
    _In_opt_ POBJECT_TYPE ObjectType,
    _In_ KPROCESSOR_MODE AccessMode,
    _In_opt_ PACCESS_STATE PassedAccessState,
    _In_ ACCESS_MASK DesiredAccess,
    _Inout_opt_ PVOID ParseContext,
    _Out_ PHANDLE Handle)
{
    POB_TEMP_BUFFER TempBuffer;
    PACCESS_STATE AccessState = PassedAccessState;
    UNICODE_STRING ObjectName;
    NTSTATUS Status;
    PVOID Object;

    PAGED_CODE();

    *Handle = NULL;

    /*
     * The attributes that name the object are required; the type is not,
     * because a caller that will take whatever the name resolves to, as the
     * I/O manager does for a device or a file, names no type at all.
     */
    if (ObjectAttributes == NULL) return STATUS_INVALID_PARAMETER;

    /* The lookup needs a context, an access state and the create information
     * at once, so they travel in one allocation */
    TempBuffer = ExAllocatePoolWithTag(NonPagedPool,
                                       sizeof(*TempBuffer),
                                       TAG_OB_TEMP_STORAGE);
    if (TempBuffer == NULL) return STATUS_INSUFFICIENT_RESOURCES;

    Status = ObpCaptureObjectCreateInformation(ObjectAttributes,
                                               AccessMode,
                                               AccessMode,
                                               TRUE,
                                               &TempBuffer->ObjectCreateInfo,
                                               &ObjectName);
    if (!NT_SUCCESS(Status))
    {
        ExFreePoolWithTag(TempBuffer, TAG_OB_TEMP_STORAGE);
        return Status;
    }

    if (AccessState == NULL)
    {
        AccessState = &TempBuffer->LocalAccessState;
        Status = SeCreateAccessState(AccessState,
                                     &TempBuffer->AuxData,
                                     DesiredAccess,
                                     (ObjectType != NULL) ?
                                     &ObjectType->TypeInfo.GenericMapping :
                                     NULL);
        if (!NT_SUCCESS(Status))
        {
            if (ObjectName.Buffer != NULL)
                ObpFreeObjectNameBuffer(&ObjectName);

            ObpReleaseObjectCreateInformation(&TempBuffer->ObjectCreateInfo);
            ExFreePoolWithTag(TempBuffer, TAG_OB_TEMP_STORAGE);
            return Status;
        }
    }

    AccessState->SecurityDescriptor =
        TempBuffer->ObjectCreateInfo.SecurityDescriptor;

    ObpInitializeLookupContext(&TempBuffer->LookupContext);

    Status = ObpLookupObjectName(TempBuffer->ObjectCreateInfo.RootDirectory,
                                 &ObjectName,
                                 TempBuffer->ObjectCreateInfo.Attributes,
                                 ObjectType,
                                 AccessMode,
                                 ParseContext,
                                 TempBuffer->ObjectCreateInfo.SecurityQos,
                                 NULL,
                                 AccessState,
                                 &TempBuffer->LookupContext,
                                 &Object);
    if (NT_SUCCESS(Status))
    {
        Status = ObpCreateHandle(ObOpenHandle,
                                 Object,
                                 ObjectType,
                                 AccessState,
                                 0,
                                 TempBuffer->ObjectCreateInfo.Attributes,
                                 &TempBuffer->LookupContext,
                                 AccessMode,
                                 NULL,
                                 Handle);

        /* The reference the lookup gave us is the handle's from here on */
        if (!NT_SUCCESS(Status)) ObDereferenceObject(Object);

    }

    ObpReleaseLookupContext(&TempBuffer->LookupContext);

    if (AccessState == &TempBuffer->LocalAccessState)
        SeDeleteAccessState(AccessState);

    if (ObjectName.Buffer != NULL) ObpFreeObjectNameBuffer(&ObjectName);

    ObpReleaseObjectCreateInformation(&TempBuffer->ObjectCreateInfo);
    ExFreePoolWithTag(TempBuffer, TAG_OB_TEMP_STORAGE);

    return Status;
}

NTSTATUS
NTAPI
ObInsertObject(
    _In_ PVOID Object,
    _Inout_opt_ PACCESS_STATE PassedAccessState,
    _In_opt_ ACCESS_MASK DesiredAccess,
    _In_ ULONG ObjectPointerBias,
    _Outptr_opt_ PVOID *NewObject,
    _Out_opt_ PHANDLE Handle)
{
    POBJECT_HEADER Header = OBJECT_TO_OBJECT_HEADER(Object);
    POBJECT_TYPE ObjectType = ObpGetObjectTypeFromHeader(Header);
    POBJECT_CREATE_INFORMATION ObjectCreateInfo = Header->ObjectCreateInfo;
    POBJECT_HEADER_NAME_INFO ObjectNameInfo;
    PACCESS_STATE AccessState = PassedAccessState;
    PSECURITY_DESCRIPTOR ParentDescriptor = NULL;
    ACCESS_STATE LocalAccessState;
    OB_OPEN_REASON OpenReason = ObCreateHandle;
    OBP_LOOKUP_CONTEXT Context;
    PUNICODE_STRING ObjectName = NULL;
    AUX_ACCESS_DATA AuxData;
    BOOLEAN SdAllocated = FALSE;
    BOOLEAN IsNewObject;
    NTSTATUS Status, RealStatus, ChargeStatus;
    PVOID FoundObject = Object;

    PAGED_CODE();

    ObpInitializeLookupContext(&Context);

    ObjectNameInfo = ObpReferenceNameInfo(Header);
    if (ObjectNameInfo != NULL && ObjectNameInfo->Name.Buffer != NULL)
        ObjectName = &ObjectNameInfo->Name;

    if (AccessState == NULL)
    {
        AccessState = &LocalAccessState;
        Status = SeCreateAccessState(AccessState,
                                     &AuxData,
                                     DesiredAccess,
                                     &ObjectType->TypeInfo.GenericMapping);
        if (!NT_SUCCESS(Status))
        {
            ObpDereferenceNameInfo(ObjectNameInfo);
            ObDereferenceObject(Object);
            return Status;
        }
    }

    if (ObjectCreateInfo != NULL)
        AccessState->SecurityDescriptor = ObjectCreateInfo->SecurityDescriptor;

    /* A named object has to take its place in the namespace first, which is
     * also where a collision is discovered */
    if (ObjectName != NULL)
    {
        Status = ObpLookupObjectName(ObjectCreateInfo->RootDirectory,
                                     ObjectName,
                                     ObjectCreateInfo->Attributes,
                                     ObjectType,
                                     (Header->Flags & OB_FLAG_KERNEL_MODE) ?
                                     KernelMode : UserMode,
                                     ObjectCreateInfo->ParseContext,
                                     ObjectCreateInfo->SecurityQos,
                                     Object,
                                     AccessState,
                                     &Context,
                                     &FoundObject);

        if (NT_SUCCESS(Status) && FoundObject != Object)
        {
            POBJECT_TYPE FoundType =
                ObpGetObjectTypeFromHeader(OBJECT_TO_OBJECT_HEADER(FoundObject));

            /* The name was taken, which is only acceptable if the caller said
             * it would settle for opening what is there */
            OpenReason = ObOpenHandle;

            if (ObjectCreateInfo->Attributes & OBJ_OPENIF)
            {
                Status = (FoundType != ObjectType) ?
                         STATUS_OBJECT_TYPE_MISMATCH : STATUS_OBJECT_NAME_EXISTS;
            }
            else
            {
                if (FoundType == ObpSymbolicLinkObjectType)
                    ObDereferenceObject(FoundObject);

                Status = (FoundType != ObjectType) ?
                         STATUS_OBJECT_TYPE_MISMATCH :
                         STATUS_OBJECT_NAME_COLLISION;
            }
        }

        if (!NT_SUCCESS(Status))
        {
            ObpReleaseLookupContext(&Context);
            ObpDereferenceNameInfo(ObjectNameInfo);
            ObDereferenceObject(Object);

            if (AccessState == &LocalAccessState)
                SeDeleteAccessState(AccessState);

            return Status;
        }

        /* A symbolic link publishes its drive letter once it is named */
        if (ObjectType == ObpSymbolicLinkObjectType && FoundObject == Object)
            ObpCreateSymbolicLinkName(Object);
    }
    else
    {
        Status = STATUS_SUCCESS;
    }

    RealStatus = Status;

    /* A new object is secured from the directory it was named in */
    if (FoundObject == Object)
    {
        if (ObjectName != NULL ||
            ObjectType->TypeInfo.SecurityRequired ||
            (ObjectCreateInfo != NULL &&
             ObjectCreateInfo->SecurityDescriptor != NULL))
        {
            if (ObjectNameInfo != NULL && ObjectNameInfo->Directory != NULL)
            {
                ObGetObjectSecurity(ObjectNameInfo->Directory,
                                    &ParentDescriptor,
                                    &SdAllocated);
            }

            Status = ObAssignSecurity(AccessState,
                                      ParentDescriptor,
                                      Object,
                                      ObjectType);

            if (ParentDescriptor != NULL)
            {
                ObReleaseObjectSecurity(ParentDescriptor, SdAllocated);
            }
            else if (NT_SUCCESS(Status) && ObjectCreateInfo != NULL)
            {
                /* The descriptor was taken over, so the capture goes back */
                SeReleaseSecurityDescriptor(ObjectCreateInfo->SecurityDescriptor,
                                            ObjectCreateInfo->ProbeMode,
                                            TRUE);
                AccessState->SecurityDescriptor = NULL;
                ObjectCreateInfo->SecurityDescriptor = NULL;
            }

            if (!NT_SUCCESS(Status))
            {
                if (Context.DirectoryLocked) ObpDeleteEntryDirectory(&Context);

                ObpReleaseLookupContext(&Context);
                ObpDereferenceNameInfo(ObjectNameInfo);
                ObDereferenceObject(Object);

                if (AccessState == &LocalAccessState)
                    SeDeleteAccessState(AccessState);

                return Status;
            }
        }
    }

    /*
     * The create information has done its job. Clearing it is what marks the
     * object as no longer mid-creation, so it has to happen whether or not a
     * handle was asked for: a CID lookup rejects an object still flagged.
     */

    ObpAcquireObjectLock(Header);
    Header->ObjectCreateInfo = NULL;
    ChargeStatus = ObpChargeQuotaForObject(Header, ObjectType, &IsNewObject);
    ObpReleaseObjectLock(Header);

    if (Handle != NULL)
    {

        Status = ObpCreateHandle(OpenReason,
                                 FoundObject,
                                 NULL,
                                 AccessState,
                                 ObjectPointerBias + 1,
                                 (ObjectCreateInfo != NULL) ?
                                 ObjectCreateInfo->Attributes : 0,
                                 &Context,
                                 (Header->Flags & OB_FLAG_KERNEL_MODE) ?
                                 KernelMode : UserMode,
                                 NewObject,
                                 Handle);

        if (!NT_SUCCESS(Status))
        {
            /* The walk left the directory locked for exactly this undo, so
             * taking the name out again must not ask for that lock anew */
            if (Context.DirectoryLocked) ObpDeleteEntryDirectory(&Context);
            else if (ObjectName != NULL) ObpDeleteNameCheck(Object);

            *Handle = NULL;
            RealStatus = Status;
        }

        ObpDereferenceNameInfo(ObjectNameInfo);

        /* The reference that kept the object alive across all of this */
        ObDereferenceObject(Object);
    }
    else
    {
        RealStatus = ChargeStatus;

        if (!NT_SUCCESS(RealStatus)) ObDereferenceObject(Object);

        ObpDereferenceNameInfo(ObjectNameInfo);
    }

    if (ObjectCreateInfo != NULL)
        ObpFreeObjectCreateInformation(ObjectCreateInfo);

    ObpReleaseLookupContext(&Context);

    if (AccessState == &LocalAccessState) SeDeleteAccessState(AccessState);

    if (NewObject != NULL && NT_SUCCESS(RealStatus)) *NewObject = FoundObject;

    return RealStatus;
}

/* DUPLICATION ****************************************************************/

static
NTSTATUS
ObpReferenceProcessObjectByHandle(
    _In_ HANDLE Handle,
    _In_ PEPROCESS Process,
    _In_ PHANDLE_TABLE HandleTable,
    _In_ KPROCESSOR_MODE AccessMode,
    _Outptr_ PVOID *Object,
    _Out_ POBJECT_HANDLE_INFORMATION HandleInformation,
    _Out_ PACCESS_MASK TargetAccess)
{
    PHANDLE_TABLE_ENTRY HandleEntry;
    POBJECT_HEADER Header;

    PAGED_CODE();

    /* The two pseudo handles always name the caller's own process or thread */
    if (Handle == NtCurrentProcess() || Handle == NtCurrentThread())
    {
        return ObReferenceObjectByHandle(Handle,
                                         0,
                                         NULL,
                                         AccessMode,
                                         Object,
                                         HandleInformation);
    }

    UNREFERENCED_PARAMETER(Process);

    /* A kernel handle is kept in the kernel's own table, not the process's */
    if (ObpIsKernelHandle(Handle, AccessMode))
    {
        HandleTable = ObpKernelHandleTable;
        Handle = ObKernelHandleToHandle(Handle);
    }

    KeEnterCriticalRegion();

    HandleEntry = ExMapHandleToPointer(HandleTable, Handle);
    if (HandleEntry == NULL)
    {
        KeLeaveCriticalRegion();
        return STATUS_INVALID_HANDLE;
    }

    Header = ObpGetHandleObject(HandleEntry);

    *TargetAccess = HandleEntry->GrantedAccess & ~ObpAccessProtectCloseBit;
    HandleInformation->GrantedAccess = *TargetAccess;
    HandleInformation->HandleAttributes =
        (ULONG)(HandleEntry->ObAttributes & OBJ_HANDLE_ATTRIBUTES);

    InterlockedIncrementSizeT(&Header->PointerCount);

    ExUnlockHandleTableEntry(HandleTable, HandleEntry);
    KeLeaveCriticalRegion();

    *Object = &Header->Body;
    return STATUS_SUCCESS;
}

/*
 * Closing the source handle of a duplication reaches into the process that
 * holds it, unless it is a kernel handle, which the close resolves itself.
 */
/* TODO: a same-process close-source duplication always makes a second handle
 * instead of rewriting the source entry in place */
static
VOID
ObpCloseSourceHandle(
    _In_ PEPROCESS SourceProcess,
    _In_ HANDLE SourceHandle,
    _In_ KPROCESSOR_MODE PreviousMode)
{
    BOOLEAN Attached = FALSE;
    KAPC_STATE ApcState;

    PAGED_CODE();

    if (!ObpIsKernelHandle(SourceHandle, PreviousMode) &&
        SourceProcess != PsGetCurrentProcess())
    {
        KeStackAttachProcess(&SourceProcess->Pcb, &ApcState);
        Attached = TRUE;
    }

    (VOID)ObpCloseHandle(SourceHandle, PreviousMode);

    if (Attached) KeUnstackDetachProcess(&ApcState);
}

/*
 * Makes the entry a handle already has into the duplicate of itself: the
 * granted access and the attributes are replaced and the handle value stays.
 */
static
NTSTATUS
ObpReuseHandleForDuplicate(
    _In_ HANDLE Handle,
    _In_ ACCESS_MASK GrantedAccess,
    _In_ ULONG HandleAttributes,
    _In_ KPROCESSOR_MODE AccessMode)
{
    PHANDLE_TABLE HandleTable = PsGetCurrentProcess()->ObjectTable;
    PHANDLE_TABLE_ENTRY Entry;
    HANDLE TableHandle = Handle;

    PAGED_CODE();

    if (ObpIsKernelHandle(Handle, AccessMode))
    {
        HandleTable = ObpKernelHandleTable;
        TableHandle = ObKernelHandleToHandle(Handle);
    }

    KeEnterCriticalRegion();

    Entry = ExMapHandleToPointer(HandleTable, TableHandle);
    if (Entry == NULL)
    {
        KeLeaveCriticalRegion();
        return STATUS_INVALID_HANDLE;
    }

    Entry->GrantedAccess = GrantedAccess;
    Entry->ObAttributes &= ~OBJ_HANDLE_ATTRIBUTES;
    Entry->ObAttributes |= (HandleAttributes & OBJ_HANDLE_ATTRIBUTES);

    ExUnlockHandleTableEntry(HandleTable, Entry);
    KeLeaveCriticalRegion();

    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
ObDuplicateObject(
    _In_ PEPROCESS SourceProcess,
    _In_ HANDLE SourceHandle,
    _In_opt_ PEPROCESS TargetProcess,
    _Out_opt_ PHANDLE TargetHandle,
    _In_ ACCESS_MASK DesiredAccess,
    _In_ ULONG HandleAttributes,
    _In_ ULONG Options,
    _In_ KPROCESSOR_MODE PreviousMode)
{
    OBJECT_HANDLE_INFORMATION HandleInformation;
    PHANDLE_TABLE SourceTable, TargetTable = NULL;
    HANDLE_TABLE_ENTRY NewEntry;
    POBJECT_HEADER Header;
    POBJECT_TYPE ObjectType;
    ACCESS_MASK SourceAccess = 0, Granted, Desired;
    OBP_HANDLE_CALLBACK_STATE CallbackState;
    ACCESS_STATE AccessState;
    AUX_ACCESS_DATA AuxData;
    BOOLEAN Attached = FALSE;
    BOOLEAN KernelHandle = FALSE;
    KAPC_STATE ApcState;
    NTSTATUS Status;
    HANDLE NewHandle;
    PVOID Object;

    PAGED_CODE();

    SourceTable = ObReferenceProcessHandleTable(SourceProcess);
    if (SourceTable == NULL) return STATUS_PROCESS_IS_TERMINATING;

    Status = ObpReferenceProcessObjectByHandle(SourceHandle,
                                               SourceProcess,
                                               SourceTable,
                                               PreviousMode,
                                               &Object,
                                               &HandleInformation,
                                               &SourceAccess);
    ObDereferenceProcessHandleTable(SourceProcess);

    if (!NT_SUCCESS(Status)) return Status;

    Header = OBJECT_TO_OBJECT_HEADER(Object);
    ObjectType = ObpGetObjectTypeFromHeader(Header);

    /* A duplicate that keeps the source's rights needs no access check */
    Granted = (Options & DUPLICATE_SAME_ACCESS) ? SourceAccess : DesiredAccess;

    if (Options & DUPLICATE_SAME_ATTRIBUTES)
        HandleAttributes = HandleInformation.HandleAttributes;

    /* Without a target the duplicate exists only to close the source */
    if (TargetProcess == NULL || TargetHandle == NULL)
    {
        if (Options & DUPLICATE_CLOSE_SOURCE)
            ObpCloseSourceHandle(SourceProcess, SourceHandle, PreviousMode);

        ObDereferenceObject(Object);
        return STATUS_SUCCESS;
    }

    /* A handle the kernel asks for of its own goes in the kernel's table,
     * wherever the target process is */
    TargetTable = ObpTableForNewHandle(HandleAttributes,
                                       PreviousMode,
                                       &KernelHandle);

    /*
     * A duplicate that closes its source in the same table asks for no rights
     * the source did not have: the entry it already has becomes the duplicate,
     * so the handle value the caller gets back is the one it gave.
     */
    if ((Options & DUPLICATE_CLOSE_SOURCE) &&
        (SourceProcess == TargetProcess) &&
        ((Granted & ~SourceAccess) == 0) &&
        (ObpIsKernelHandle(SourceHandle, PreviousMode) == KernelHandle))
    {
        Status = ObpReuseHandleForDuplicate(SourceHandle,
                                            Granted,
                                            HandleAttributes,
                                            PreviousMode);
        if (NT_SUCCESS(Status))
        {
            *TargetHandle = SourceHandle;
            ObDereferenceObject(Object);
            return STATUS_SUCCESS;
        }
    }

    /* The source is closed first and whatever becomes of the duplicate, so its place in the table is free for the new handle */
    if (Options & DUPLICATE_CLOSE_SOURCE)
        ObpCloseSourceHandle(SourceProcess, SourceHandle, PreviousMode);
    if (!KernelHandle)
    {
        TargetTable = ObReferenceProcessHandleTable(TargetProcess);
        if (TargetTable == NULL)
        {
            ObDereferenceObject(Object);
            return STATUS_PROCESS_IS_TERMINATING;
        }
    }

    Status = SeCreateAccessState(&AccessState,
                                 &AuxData,
                                 Granted,
                                 &ObjectType->TypeInfo.GenericMapping);
    if (!NT_SUCCESS(Status))
    {
        if (!KernelHandle) ObDereferenceProcessHandleTable(TargetProcess);
        ObDereferenceObject(Object);
        return Status;
    }

    if (Options & DUPLICATE_SAME_ACCESS)
    {
        AccessState.PreviouslyGrantedAccess = SourceAccess;
        AccessState.RemainingDesiredAccess = 0;
    }

    Status = ObpBeginHandleCallbacks(&CallbackState,
                                     ObjectType,
                                     OB_OPERATION_HANDLE_DUPLICATE);
    if (!NT_SUCCESS(Status))
    {
        SeDeleteAccessState(&AccessState);
        if (!KernelHandle) ObDereferenceProcessHandleTable(TargetProcess);
        ObDereferenceObject(Object);
        return Status;
    }

    Desired = AccessState.RemainingDesiredAccess |
              AccessState.PreviouslyGrantedAccess;
    ObpCallPreHandleCallbacks(&CallbackState,
                              Object,
                              (BOOLEAN)((HandleAttributes & OBJ_KERNEL_HANDLE) != 0 &&
                                        TargetTable == ObpKernelHandleTable),
                              &Desired,
                              SourceProcess,
                              TargetProcess);
    AccessState.RemainingDesiredAccess &= Desired;
    AccessState.PreviouslyGrantedAccess &= Desired;

    Status = ObpIncrementHandleCount(Object,
                                      &AccessState,
                                      PreviousMode,
                                      HandleAttributes,
                                      TargetProcess,
                                      ObDuplicateHandle,
                                      TRUE);
    if (NT_SUCCESS(Status))
    {
        NewEntry.Object = Header;
        NewEntry.ObAttributes |= (HandleAttributes & OBJ_HANDLE_ATTRIBUTES);
        NewEntry.GrantedAccess = AccessState.PreviouslyGrantedAccess;

        /* The handle has to be made in the context the table belongs to */
        if (KernelHandle)
        {
            if (PsGetCurrentProcess() != PsInitialSystemProcess)
            {
                KeStackAttachProcess(&PsInitialSystemProcess->Pcb, &ApcState);
                Attached = TRUE;
            }
        }
        else if (TargetProcess != PsGetCurrentProcess())
        {
            KeStackAttachProcess(&TargetProcess->Pcb, &ApcState);
            Attached = TRUE;
        }

        NewHandle = ExCreateHandle(TargetTable, &NewEntry);

        if (Attached) KeUnstackDetachProcess(&ApcState);

        if (NewHandle != NULL)
        {
            ObpChargeHandleCachedReferences(TargetTable, NewHandle, Header);

            if ((HandleAttributes & OBJ_KERNEL_HANDLE) &&
                TargetTable == ObpKernelHandleTable)
            {
                NewHandle = ObMarkHandleAsKernelHandle(NewHandle);
            }

            *TargetHandle = NewHandle;
        }
        else
        {
            ObpRollbackHandleCount(Header, ObjectType, TargetProcess);
            Status = STATUS_INSUFFICIENT_RESOURCES;
        }
    }

    ObpEndHandleCallbacks(&CallbackState,
                          Status,
                          NT_SUCCESS(Status) ?
                          AccessState.PreviouslyGrantedAccess : 0);

    SeDeleteAccessState(&AccessState);
    if (!KernelHandle) ObDereferenceProcessHandleTable(TargetProcess);

    /* The reference the source handle lent us belongs to the new handle */
    if (!NT_SUCCESS(Status)) ObDereferenceObject(Object);

    return Status;
}

NTSTATUS
NTAPI
NtDuplicateObject(
    _In_ HANDLE SourceProcessHandle,
    _In_opt_ HANDLE SourceHandle,
    _In_opt_ HANDLE TargetProcessHandle,
    _Out_opt_ PHANDLE TargetHandle,
    _In_ ACCESS_MASK DesiredAccess,
    _In_ ULONG HandleAttributes,
    _In_ ULONG Options)
{
    KPROCESSOR_MODE PreviousMode = ExGetPreviousMode();
    PEPROCESS SourceProcess, TargetProcess = NULL;
    HANDLE Duplicate = NULL;
    NTSTATUS Status;

    PAGED_CODE();

    if (PreviousMode != KernelMode && TargetHandle != NULL)
    {
        _SEH2_TRY
        {
            ProbeForWriteHandle(TargetHandle);
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            _SEH2_YIELD(return _SEH2_GetExceptionCode());
        }
        _SEH2_END;
    }

    Status = ObReferenceObjectByHandle(SourceProcessHandle,
                                       PROCESS_DUP_HANDLE,
                                       PsProcessType,
                                       PreviousMode,
                                       (PVOID *)&SourceProcess,
                                       NULL);
    if (!NT_SUCCESS(Status)) return Status;

    if (TargetProcessHandle != NULL)
    {
        Status = ObReferenceObjectByHandle(TargetProcessHandle,
                                           PROCESS_DUP_HANDLE,
                                           PsProcessType,
                                           PreviousMode,
                                           (PVOID *)&TargetProcess,
                                           NULL);
        if (!NT_SUCCESS(Status))
        {
            ObDereferenceObject(SourceProcess);
            return Status;
        }
    }

    Status = ObDuplicateObject(SourceProcess,
                               SourceHandle,
                               TargetProcess,
                               (TargetProcess != NULL) ? &Duplicate : NULL,
                               DesiredAccess,
                               HandleAttributes,
                               Options,
                               PreviousMode);

    if (TargetProcess != NULL) ObDereferenceObject(TargetProcess);
    ObDereferenceObject(SourceProcess);

    if (TargetHandle != NULL)
    {
        _SEH2_TRY
        {
            *TargetHandle = NT_SUCCESS(Status) ? Duplicate : NULL;
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            Status = _SEH2_GetExceptionCode();
        }
        _SEH2_END;
    }

    return Status;
}

/* EOF */
