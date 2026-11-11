/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Who may touch a key, and where that answer is kept in the hive
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

#include "cmpriv.h"

/* PRIVATE DATA ***************************************************************/

static PSECURITY_DESCRIPTOR CmpRootSecurityDescriptor = NULL;

/* PRIVATE FUNCTIONS **********************************************************/

/*
 * Keys that say the same thing about who may touch them share one cell, so
 * the list of security cells is searched before a new one is made.
 */
static
HCELL_INDEX
CmpFindOrCreateSecurityCell(
    _In_ PHHIVE Hive,
    _In_ HSTORAGE_TYPE StorageType,
    _In_ HCELL_INDEX Anchor,
    _In_reads_bytes_(Length) PVOID Descriptor,
    _In_ ULONG Length)
{
    PCMHIVE CmHive = CONTAINING_RECORD(Hive, CMHIVE, Hive);
    PCM_KEY_SECURITY Security;
    PCM_KEY_SECURITY Neighbour;
    HCELL_INDEX NewCell;
    HCELL_INDEX Cell;

    Cell = CmpFindSecurityCellByDescriptor(CmHive, StorageType, Descriptor, Length);

    if (Cell != HCELL_NIL)
    {
        Security = (PCM_KEY_SECURITY)HvGetCell(Hive, Cell);

        if (Security != NULL)
        {
            if (!HvMarkCellDirty(Hive, Cell, FALSE))
            {
                HvReleaseCell(Hive, Cell);
                return HCELL_NIL;
            }

            Security->ReferenceCount++;
            HvReleaseCell(Hive, Cell);
            return Cell;
        }

        CmpInvalidateSecurityCacheEntry(CmHive, Cell);
    }

    if (Anchor != HCELL_NIL && HvGetCellType(Anchor) != (ULONG)StorageType)
    {
        Anchor = HCELL_NIL;
    }

    NewCell = HvAllocateCell(Hive,
                             FIELD_OFFSET(CM_KEY_SECURITY, Descriptor) + Length,
                             StorageType,
                             Anchor);
    if (NewCell == HCELL_NIL) return HCELL_NIL;

    Security = (PCM_KEY_SECURITY)HvGetCell(Hive, NewCell);
    if (Security == NULL)
    {
        HvFreeCell(Hive, NewCell);
        return HCELL_NIL;
    }

    Security->Signature = CM_KEY_SECURITY_SIGNATURE;
    Security->Reserved = 0;
    Security->ReferenceCount = 1;
    Security->DescriptorLength = Length;
    RtlCopyMemory(&Security->Descriptor, Descriptor, Length);

    if (Anchor == HCELL_NIL)
    {
        Security->Flink = NewCell;
        Security->Blink = NewCell;
        HvReleaseCell(Hive, NewCell);
        HvMarkCellDirty(Hive, NewCell, FALSE);
        CmpGetSecurityCacheEntry(CmHive, NewCell);
        return NewCell;
    }

    Neighbour = (PCM_KEY_SECURITY)HvGetCell(Hive, Anchor);
    if (Neighbour == NULL)
    {
        HvReleaseCell(Hive, NewCell);
        HvFreeCell(Hive, NewCell);
        return HCELL_NIL;
    }

    if (!HvMarkCellDirty(Hive, Anchor, FALSE))
    {
        HvReleaseCell(Hive, Anchor);
        HvReleaseCell(Hive, NewCell);
        HvFreeCell(Hive, NewCell);
        return HCELL_NIL;
    }

    Security->Flink = Neighbour->Flink;
    Security->Blink = Anchor;
    Neighbour->Flink = NewCell;
    HvReleaseCell(Hive, Anchor);

    Neighbour = (PCM_KEY_SECURITY)HvGetCell(Hive, Security->Flink);
    if (Neighbour != NULL)
    {
        if (HvMarkCellDirty(Hive, Security->Flink, FALSE))
        {
            Neighbour->Blink = NewCell;
        }

        HvReleaseCell(Hive, Security->Flink);
    }

    HvReleaseCell(Hive, NewCell);
    HvMarkCellDirty(Hive, NewCell, FALSE);
    CmpGetSecurityCacheEntry(CmHive, NewCell);
    return NewCell;
}

static
NTSTATUS
CmpAttachSecurityCell(
    _In_ PHHIVE Hive,
    _In_ HCELL_INDEX KeyCell,
    _In_reads_bytes_(Length) PVOID Descriptor,
    _In_ ULONG Length,
    _Out_ PHCELL_INDEX SecurityCell)
{
    PCM_KEY_NODE Node;
    HCELL_INDEX OldCell;
    HCELL_INDEX NewCell;

    *SecurityCell = HCELL_NIL;

    Node = (PCM_KEY_NODE)HvGetCell(Hive, KeyCell);
    if (Node == NULL) return STATUS_INSUFFICIENT_RESOURCES;

    if (!HvMarkCellDirty(Hive, KeyCell, FALSE))
    {
        HvReleaseCell(Hive, KeyCell);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    OldCell = Node->Security;

    NewCell = CmpFindOrCreateSecurityCell(Hive,
                                          HvGetCellType(KeyCell),
                                          CmpHiveSecurityAnchor(Hive),
                                          Descriptor,
                                          Length);
    if (NewCell == HCELL_NIL)
    {
        HvReleaseCell(Hive, KeyCell);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    Node->Security = NewCell;
    HvReleaseCell(Hive, KeyCell);

    if (OldCell != HCELL_NIL && OldCell != NewCell)
    {
        CmpDereferenceSecurityCell(Hive, OldCell);
    }

    *SecurityCell = NewCell;
    return STATUS_SUCCESS;
}

/* PUBLIC FUNCTIONS ***********************************************************/

/*
 * What a key says about access. The answer comes out of the hive's own cache,
 * so it stays readable for as long as the hive is loaded and the control
 * block can hold on to it.
 */
NTSTATUS
CmpGetKeySecurityDescriptor(
    _In_ PCM_KEY_CONTROL_BLOCK Kcb,
    _Out_ PSECURITY_DESCRIPTOR *Descriptor)
{
    PCM_KEY_SECURITY_CACHE Cached;
    PCMHIVE CmHive;
    PCM_KEY_NODE Node;
    HCELL_INDEX Cell;

    *Descriptor = NULL;

    if (Kcb->CachedSecurity != NULL)
    {
        *Descriptor = (PSECURITY_DESCRIPTOR)&Kcb->CachedSecurity->Descriptor;
        return STATUS_SUCCESS;
    }

    Node = (PCM_KEY_NODE)HvGetCell(Kcb->KeyHive, Kcb->KeyCell);
    if (Node == NULL) return STATUS_INSUFFICIENT_RESOURCES;

    Cell = Node->Security;
    HvReleaseCell(Kcb->KeyHive, Kcb->KeyCell);

    if (Cell == HCELL_NIL) return STATUS_SUCCESS;

    CmHive = CONTAINING_RECORD(Kcb->KeyHive, CMHIVE, Hive);

    Cached = CmpGetSecurityCacheEntry(CmHive, Cell);
    if (Cached == NULL) return STATUS_INSUFFICIENT_RESOURCES;

    Kcb->CachedSecurity = Cached;
    *Descriptor = (PSECURITY_DESCRIPTOR)&Cached->Descriptor;
    return STATUS_SUCCESS;
}

VOID
NTAPI
CmpAssignSecurityToKcb(
    _In_ PCM_KEY_CONTROL_BLOCK Kcb,
    _In_ HCELL_INDEX SecurityCell,
    _In_ BOOLEAN KeyLockHeld)
{
    PCMHIVE CmHive = CONTAINING_RECORD(Kcb->KeyHive, CMHIVE, Hive);

    UNREFERENCED_PARAMETER(KeyLockHeld);

    Kcb->CachedSecurity = CmpGetSecurityCacheEntry(CmHive, SecurityCell);
}

/*
 * Works out what the new key should say about access from what the caller
 * asked for and what its parent says, then writes that into the hive.
 */
NTSTATUS
CmpAssignSecurityDescriptorLocked(
    _In_ PCM_KEY_CONTROL_BLOCK Kcb,
    _In_opt_ PSECURITY_DESCRIPTOR SecurityDescriptor)
{
    PSECURITY_DESCRIPTOR Parent = NULL;
    PSECURITY_DESCRIPTOR Assigned = NULL;
    SECURITY_DESCRIPTOR_CONTROL ParentControl = 0;
    SECURITY_DESCRIPTOR_CONTROL Control = 0;
    SECURITY_SUBJECT_CONTEXT Subject;
    HCELL_INDEX SecurityCell;
    ULONG AutoInheritFlags = 0;
    NTSTATUS Status;
    ULONG Length;

    if (Kcb->ParentKcb != NULL)
    {
        Status = CmpGetKeySecurityDescriptor(Kcb->ParentKcb, &Parent);
        if (!NT_SUCCESS(Status)) return Status;
    }

    if (SecurityDescriptor == NULL && Parent == NULL)
    {
        SecurityDescriptor = CmpHiveRootSecurityDescriptor();
        if (SecurityDescriptor == NULL) return STATUS_INSUFFICIENT_RESOURCES;
    }

    /* A key inherits automatically, marking what it inherits, only from a parent whose list is auto-inherited */
    if (Parent != NULL) ParentControl = ((PISECURITY_DESCRIPTOR)Parent)->Control;
    if (SecurityDescriptor != NULL) Control = ((PISECURITY_DESCRIPTOR)SecurityDescriptor)->Control;
    if (!(Control & SE_DACL_PRESENT) && (ParentControl & SE_DACL_AUTO_INHERITED))
        AutoInheritFlags |= SEF_DACL_AUTO_INHERIT;
    if (!(Control & SE_SACL_PRESENT) && (ParentControl & SE_SACL_AUTO_INHERITED))
        AutoInheritFlags |= SEF_SACL_AUTO_INHERIT;

    SeCaptureSubjectContext(&Subject);

    Status = SeAssignSecurityEx(Parent,
                                SecurityDescriptor,
                                &Assigned,
                                NULL,
                                TRUE,
                                AutoInheritFlags,
                                &Subject,
                                &CmpKeyObjectType->TypeInfo.GenericMapping,
                                PagedPool);

    SeReleaseSubjectContext(&Subject);

    if (!NT_SUCCESS(Status)) return Status;

    Length = RtlLengthSecurityDescriptor(Assigned);

    Status = CmpAttachSecurityCell(Kcb->KeyHive,
                                   Kcb->KeyCell,
                                   Assigned,
                                   Length,
                                   &SecurityCell);

    SeDeassignSecurity(&Assigned);

    if (NT_SUCCESS(Status)) CmpAssignSecurityToKcb(Kcb, SecurityCell, TRUE);
    return Status;
}

NTSTATUS
CmpAssignSecurityDescriptor(
    _In_ PCM_KEY_CONTROL_BLOCK Kcb,
    _In_opt_ PSECURITY_DESCRIPTOR SecurityDescriptor)
{
    NTSTATUS Status;

    CmpLockHiveSecurity(Kcb->KeyHive);
    Status = CmpAssignSecurityDescriptorLocked(Kcb, SecurityDescriptor);
    CmpUnlockHiveSecurity(Kcb->KeyHive);
    return Status;
}

NTSTATUS
NTAPI
CmpQuerySecurityDescriptor(
    _In_ PCM_KEY_BODY KeyBody,
    _In_ SECURITY_INFORMATION SecurityInformation,
    _Out_writes_bytes_opt_(*Length) PSECURITY_DESCRIPTOR SecurityDescriptor,
    _Inout_ PULONG Length)
{
    PSECURITY_DESCRIPTOR Current = NULL;
    NTSTATUS Status;

    if (KeyBody->KeyControlBlock == NULL) return STATUS_KEY_DELETED;

    Status = CmpGetKeySecurityDescriptor(KeyBody->KeyControlBlock, &Current);
    if (!NT_SUCCESS(Status)) return Status;
    if (Current == NULL) return STATUS_NO_SECURITY_ON_OBJECT;

    return SeQuerySecurityDescriptorInfo(&SecurityInformation,
                                         SecurityDescriptor,
                                         Length,
                                         &Current);
}

NTSTATUS
NTAPI
CmpSetSecurityDescriptor(
    _In_ PCM_KEY_BODY KeyBody,
    _In_ PSECURITY_INFORMATION SecurityInformation,
    _In_ PSECURITY_DESCRIPTOR SecurityDescriptor,
    _In_ POOL_TYPE PoolType,
    _In_ PGENERIC_MAPPING GenericMapping)
{
    PCM_KEY_CONTROL_BLOCK Kcb = KeyBody->KeyControlBlock;
    PSECURITY_DESCRIPTOR Current = NULL;
    PSECURITY_DESCRIPTOR Working = NULL;
    HCELL_INDEX SecurityCell;
    NTSTATUS Status;
    ULONG Length;

    if (Kcb == NULL) return STATUS_KEY_DELETED;
    if (Kcb->Delete) return STATUS_KEY_DELETED;

    Status = CmpTransPrepareWrite(KeyBody);
    if (!NT_SUCCESS(Status)) return Status;
    if (Kcb->KeyHive->ReadOnly) return STATUS_ACCESS_DENIED;

    Status = CmpGetKeySecurityDescriptor(Kcb, &Current);
    if (!NT_SUCCESS(Status)) return Status;
    if (Current == NULL) return STATUS_NO_SECURITY_ON_OBJECT;

    Length = RtlLengthSecurityDescriptor(Current);

    Working = ExAllocatePoolZero(PoolType, Length, TAG_CMSD);
    if (Working == NULL) return STATUS_INSUFFICIENT_RESOURCES;

    RtlCopyMemory(Working, Current, Length);

    Status = SeSetSecurityDescriptorInfoEx(NULL,
                                           SecurityInformation,
                                           SecurityDescriptor,
                                           &Working,
                                           0,
                                           PoolType,
                                           GenericMapping);
    if (!NT_SUCCESS(Status))
    {
        ExFreePool(Working);
        return Status;
    }

    Length = RtlLengthSecurityDescriptor(Working);

    CmpLockHiveSecurity(Kcb->KeyHive);
    Status = CmpAttachSecurityCell(Kcb->KeyHive,
                                   Kcb->KeyCell,
                                   Working,
                                   Length,
                                   &SecurityCell);
    CmpUnlockHiveSecurity(Kcb->KeyHive);

    ExFreePool(Working);

    if (!NT_SUCCESS(Status)) return Status;

    CmpAssignSecurityToKcb(Kcb, SecurityCell, FALSE);
    CmpTouchKey(Kcb->KeyHive, Kcb->KeyCell);
    CmpReportNotify(Kcb,
                    Kcb->KeyHive,
                    Kcb->KeyCell,
                    REG_NOTIFY_CHANGE_SECURITY);

    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
CmpSecurityMethod(
    _In_ PVOID Object,
    _In_ SECURITY_OPERATION_CODE OperationType,
    _In_ PSECURITY_INFORMATION SecurityInformation,
    _In_ PSECURITY_DESCRIPTOR SecurityDescriptor,
    _Inout_ PULONG CapturedLength,
    _Inout_ PSECURITY_DESCRIPTOR *ObjectSecurityDescriptor,
    _In_ POOL_TYPE PoolType,
    _In_ PGENERIC_MAPPING GenericMapping,
    _In_ KPROCESSOR_MODE AccessMode)
{
    PCM_KEY_BODY KeyBody = Object;
    NTSTATUS Status;

    UNREFERENCED_PARAMETER(ObjectSecurityDescriptor);
    UNREFERENCED_PARAMETER(AccessMode);

    if (KeyBody->Type != CM_KEY_BODY_TYPE) return STATUS_OBJECT_TYPE_MISMATCH;

    switch (OperationType)
    {
        case SetSecurityDescriptor:
            CmpLockRegistryExclusive();
            Status = CmpSetSecurityDescriptor(KeyBody,
                                              SecurityInformation,
                                              SecurityDescriptor,
                                              PoolType,
                                              GenericMapping);
            CmpUnlockRegistry();
            return Status;

        case QuerySecurityDescriptor:
            CmpLockRegistry();
            Status = CmpQuerySecurityDescriptor(KeyBody,
                                                *SecurityInformation,
                                                SecurityDescriptor,
                                                CapturedLength);
            CmpUnlockRegistry();
            return Status;

        case AssignSecurityDescriptor:
            CmpLockRegistryExclusive();
            Status = CmpAssignSecurityDescriptor(KeyBody->KeyControlBlock,
                                                 SecurityDescriptor);
            CmpUnlockRegistry();
            return Status;

        case DeleteSecurityDescriptor:
            return STATUS_SUCCESS;

        default:
            return STATUS_INVALID_PARAMETER;
    }
}

/*
 * The descriptor a root key carries when nothing stored says otherwise. The
 * system owns it; the system and the administrators may do anything, and
 * everybody else, packaged applications holding the registry read capability
 * included, may read. Every entry is handed down to the keys made below.
 */
PSECURITY_DESCRIPTOR
NTAPI
CmpHiveRootSecurityDescriptor(VOID)
{
    static SID_IDENTIFIER_AUTHORITY PackageAuthority =
        SECURITY_APP_PACKAGE_AUTHORITY;
    static const ULONG RegistryReadCapability[] =
    {
        SECURITY_CAPABILITY_BASE_RID, SECURITY_CAPABILITY_APP_RID,
        1065365936UL, 1281604716UL, 3511738428UL, 1654721687UL,
        432734479UL, 3232135806UL, 4053264122UL, 3456934681UL
    };
    UCHAR CapabilityBuffer[FIELD_OFFSET(SID, SubAuthority) +
                           sizeof(RegistryReadCapability)];
    PSID CapabilitySid = (PSID)CapabilityBuffer;
    struct
    {
        PSID Sid;
        ACCESS_MASK Mask;
    } Entries[6];
    SECURITY_DESCRIPTOR Absolute;
    PSECURITY_DESCRIPTOR Relative;
    NTSTATUS Status;
    ULONG AclSize;
    ULONG Length;
    ULONG Index;
    PACL Dacl;

    if (CmpRootSecurityDescriptor != NULL) return CmpRootSecurityDescriptor;

    RtlInitializeSid(CapabilitySid,
                     &PackageAuthority,
                     RTL_NUMBER_OF(RegistryReadCapability));
    for (Index = 0; Index < RTL_NUMBER_OF(RegistryReadCapability); Index++)
    {
        *RtlSubAuthoritySid(CapabilitySid, Index) =
            RegistryReadCapability[Index];
    }

    Entries[0].Sid = SeLocalSystemSid;      Entries[0].Mask = KEY_ALL_ACCESS;
    Entries[1].Sid = SeAliasAdminsSid;      Entries[1].Mask = KEY_ALL_ACCESS;
    Entries[2].Sid = SeWorldSid;            Entries[2].Mask = KEY_READ;
    Entries[3].Sid = SeRestrictedSid;       Entries[3].Mask = KEY_READ;
    Entries[4].Sid = SeAllAppPackagesSid;   Entries[4].Mask = KEY_READ;
    Entries[5].Sid = CapabilitySid;         Entries[5].Mask = KEY_READ;

    AclSize = (ULONG)sizeof(ACL);
    for (Index = 0; Index < RTL_NUMBER_OF(Entries); Index++)
    {
        AclSize += (ULONG)(sizeof(ACCESS_ALLOWED_ACE) - sizeof(ULONG)) +
                   RtlLengthSid(Entries[Index].Sid);
    }

    Dacl = ExAllocatePoolZero(PagedPool, AclSize, TAG_CMSD);
    if (Dacl == NULL) return NULL;

    Status = RtlCreateAcl(Dacl, AclSize, ACL_REVISION);

    for (Index = 0;
         NT_SUCCESS(Status) && Index < RTL_NUMBER_OF(Entries);
         Index++)
    {
        Status = RtlAddAccessAllowedAceEx(Dacl,
                                          ACL_REVISION,
                                          CONTAINER_INHERIT_ACE,
                                          Entries[Index].Mask,
                                          Entries[Index].Sid);
    }

    if (NT_SUCCESS(Status))
    {
        Status = RtlCreateSecurityDescriptor(&Absolute,
                                             SECURITY_DESCRIPTOR_REVISION);
    }

    if (!NT_SUCCESS(Status))
    {
        ExFreePool(Dacl);
        return NULL;
    }

    RtlSetDaclSecurityDescriptor(&Absolute, TRUE, Dacl, FALSE);
    RtlSetOwnerSecurityDescriptor(&Absolute, SeLocalSystemSid, FALSE);
    RtlSetGroupSecurityDescriptor(&Absolute, SeLocalSystemSid, FALSE);

    Length = 0;
    Status = RtlAbsoluteToSelfRelativeSD(&Absolute, NULL, &Length);
    if (Status != STATUS_BUFFER_TOO_SMALL)
    {
        ExFreePool(Dacl);
        return NULL;
    }

    Relative = ExAllocatePoolZero(PagedPool, Length, TAG_CMSD);
    if (Relative == NULL)
    {
        ExFreePool(Dacl);
        return NULL;
    }

    Status = RtlAbsoluteToSelfRelativeSD(&Absolute, Relative, &Length);
    ExFreePool(Dacl);

    if (!NT_SUCCESS(Status))
    {
        ExFreePool(Relative);
        return NULL;
    }

    CmpRootSecurityDescriptor = Relative;
    return CmpRootSecurityDescriptor;
}

/*
 * An application hive belongs to the process that loaded it, so access to it
 * is already settled by who owns the hive rather than by its descriptors.
 */
NTSTATUS
NTAPI
CmpAppHiveAccessMode(
    _In_ PCMHIVE Hive,
    _In_ ACCESS_MASK DesiredAccess,
    _In_ KPROCESSOR_MODE AccessMode,
    _Out_ KPROCESSOR_MODE *EffectiveMode)
{
    UNREFERENCED_PARAMETER(DesiredAccess);

    *EffectiveMode = AccessMode;

    if ((Hive->Flags & CMHIVE_FLAG_APPLICATION_HIVE) == 0)
    {
        return STATUS_SUCCESS;
    }

    if ((Hive->Flags & CMHIVE_FLAG_PROCESS_PRIVATE) == 0)
    {
        return STATUS_SUCCESS;
    }

    if (CmpAppHiveOwner(Hive) != PsGetCurrentProcess())
    {
        return STATUS_ACCESS_DENIED;
    }

    *EffectiveMode = KernelMode;
    return STATUS_SUCCESS;
}

/* Carries the descriptor of one key across into another hive */
NTSTATUS
CmpCopyKeySecurity(
    _In_ PHHIVE SourceHive,
    _In_ HCELL_INDEX SourceCell,
    _In_ PHHIVE DestinationHive,
    _In_ HSTORAGE_TYPE StorageType,
    _Inout_ PHCELL_INDEX AnchorCell,
    _Out_ PHCELL_INDEX NewCell)
{
    PCM_KEY_SECURITY Security;
    PCM_KEY_NODE Node;
    HCELL_INDEX Cell;

    *NewCell = HCELL_NIL;

    Node = (PCM_KEY_NODE)HvGetCell(SourceHive, SourceCell);
    if (Node == NULL) return STATUS_INSUFFICIENT_RESOURCES;

    Cell = Node->Security;
    HvReleaseCell(SourceHive, SourceCell);

    if (Cell == HCELL_NIL) return STATUS_SUCCESS;

    Security = (PCM_KEY_SECURITY)HvGetCell(SourceHive, Cell);
    if (Security == NULL) return STATUS_INSUFFICIENT_RESOURCES;

    *NewCell = CmpFindOrCreateSecurityCell(DestinationHive,
                                           StorageType,
                                           *AnchorCell,
                                           &Security->Descriptor,
                                           Security->DescriptorLength);

    HvReleaseCell(SourceHive, Cell);

    if (*NewCell == HCELL_NIL) return STATUS_INSUFFICIENT_RESOURCES;

    if (*AnchorCell == HCELL_NIL) *AnchorCell = *NewCell;
    return STATUS_SUCCESS;
}
