/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Security reference monitor interface of the object manager
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

/* PRIVATE FUNCTIONS **********************************************************/

static
NTSTATUS
ObpCopyNonLabelAces(
    _Inout_ PACL Sacl,
    _In_ PACL Source)
{
    PACE_HEADER Ace;
    NTSTATUS Status;
    ULONG Index;

    for (Index = 0; Index < Source->AceCount; ++Index)
    {
        Status = RtlGetAce(Source, Index, (PVOID *)&Ace);
        if (!NT_SUCCESS(Status)) return Status;

        if (Ace->AceType == SYSTEM_MANDATORY_LABEL_ACE_TYPE) continue;

        Status = RtlAddAce(Sacl, Sacl->AclRevision, MAXULONG, Ace, Ace->AceSize);
        if (!NT_SUCCESS(Status)) return Status;
    }

    return STATUS_SUCCESS;
}

static
NTSTATUS
ObpMeasureLabelSacl(
    _In_opt_ PACL Source,
    _In_ PSID LabelSid,
    _Out_ PULONG SaclLength)
{
    PACE_HEADER Ace;
    NTSTATUS Status;
    ULONG Length;
    ULONG Index;

    Length = sizeof(ACL) +
             FIELD_OFFSET(SYSTEM_MANDATORY_LABEL_ACE, SidStart) +
             RtlLengthSid(LabelSid);

    if (Source != NULL)
    {
        for (Index = 0; Index < Source->AceCount; ++Index)
        {
            Status = RtlGetAce(Source, Index, (PVOID *)&Ace);
            if (!NT_SUCCESS(Status)) return Status;

            if (Ace->AceType != SYSTEM_MANDATORY_LABEL_ACE_TYPE)
                Length += Ace->AceSize;
        }
    }

    if (Length > MAXUSHORT) return STATUS_ALLOTTED_SPACE_EXCEEDED;

    *SaclLength = Length;
    return STATUS_SUCCESS;
}

static
NTSTATUS
ObpAssignTokenSecurity(
    _In_ PACCESS_STATE AccessState,
    _In_opt_ PSECURITY_DESCRIPTOR ParentDescriptor,
    _In_ PTOKEN Token,
    _Out_ PSECURITY_DESCRIPTOR *NewDescriptor,
    _In_ POBJECT_TYPE Type)
{
    PISECURITY_DESCRIPTOR ExplicitDescriptor = AccessState->SecurityDescriptor;
    SECURITY_DESCRIPTOR TokenDescriptor;
    PACL OldSacl = NULL, Sacl;
    PSID LabelSid;
    ULONG Length;
    NTSTATUS Status;

    /* A token is labelled with its own integrity level */
    LabelSid = SeMediumMandatorySid;
    if (Token->IntegrityLevelIndex != 0 &&
        Token->IntegrityLevelIndex < Token->UserAndGroupCount)
    {
        LabelSid = Token->UserAndGroups[Token->IntegrityLevelIndex].Sid;
    }

    RtlCreateSecurityDescriptor(&TokenDescriptor, SECURITY_DESCRIPTOR_REVISION);
    if (ExplicitDescriptor != NULL)
    {
        TokenDescriptor.Sbz1 = ExplicitDescriptor->Sbz1;
        TokenDescriptor.Control = ExplicitDescriptor->Control & ~SE_SELF_RELATIVE;
        TokenDescriptor.Owner = SepGetOwnerFromDescriptor(ExplicitDescriptor);
        TokenDescriptor.Group = SepGetGroupFromDescriptor(ExplicitDescriptor);
        TokenDescriptor.Dacl = SepGetDaclFromDescriptor(ExplicitDescriptor);
        OldSacl = SepGetSaclFromDescriptor(ExplicitDescriptor);
    }

    Status = ObpMeasureLabelSacl(OldSacl, LabelSid, &Length);
    if (!NT_SUCCESS(Status)) return Status;

    Sacl = ExAllocatePoolWithTag(PagedPool, Length, TAG_ACL);
    if (Sacl == NULL) return STATUS_INSUFFICIENT_RESOURCES;

    Status = RtlCreateAcl(Sacl, Length, OldSacl ? OldSacl->AclRevision : ACL_REVISION);

    if (NT_SUCCESS(Status) && OldSacl != NULL)
        Status = ObpCopyNonLabelAces(Sacl, OldSacl);

    if (NT_SUCCESS(Status))
    {
        Status = RtlAddMandatoryAce(Sacl,
                                    Sacl->AclRevision,
                                    0,
                                    SYSTEM_MANDATORY_LABEL_NO_WRITE_UP,
                                    SYSTEM_MANDATORY_LABEL_ACE_TYPE,
                                    LabelSid);
    }

    if (NT_SUCCESS(Status))
    {
        RtlSetSaclSecurityDescriptor(&TokenDescriptor, TRUE, Sacl, FALSE);
        Status = SeAssignSecurityEx(ParentDescriptor,
                                    &TokenDescriptor,
                                    NewDescriptor,
                                    NULL,
                                    FALSE,
                                    SEF_SACL_AUTO_INHERIT,
                                    &AccessState->SubjectSecurityContext,
                                    &Type->TypeInfo.GenericMapping,
                                    PagedPool);
    }

    ExFreePoolWithTag(Sacl, TAG_ACL);
    return Status;
}

static
BOOLEAN
ObpLabelWithinCallerIntegrity(
    _In_ PSECURITY_DESCRIPTOR SecurityDescriptor)
{
    ULONG LabelRid = SECURITY_MANDATORY_UNTRUSTED_RID;
    SECURITY_IMPERSONATION_LEVEL Level;
    PACCESS_TOKEN Token;
    TOKEN_TYPE TokenType;
    BOOLEAN EffectiveOnly;
    PACE_HEADER Ace;
    ULONG TokenRid;
    ULONG Index;
    PACL Sacl;

    Sacl = SepGetSaclFromDescriptor(SecurityDescriptor);
    if (Sacl != NULL)
    {
        for (Index = 0; Index < Sacl->AceCount; Index++)
        {
            if (!NT_SUCCESS(RtlGetAce(Sacl, Index, (PVOID *)&Ace))) break;
            if (Ace->AceType != SYSTEM_MANDATORY_LABEL_ACE_TYPE) continue;
            LabelRid = *RtlSubAuthoritySid(&((PSYSTEM_MANDATORY_LABEL_ACE)Ace)->SidStart, 0);
            break;
        }
    }

    Token = PsReferenceEffectiveToken(PsGetCurrentThread(), &TokenType, &EffectiveOnly, &Level);
    TokenRid = SepGetTokenIntegrityRid(Token);
    if (TokenType == TokenPrimary)
        PsDereferencePrimaryToken(Token);
    else
        PsDereferenceImpersonationToken(Token);

    return LabelRid <= TokenRid;
}

static
NTSTATUS
ObpCallSecurityProcedure(
    _In_ PVOID Object,
    _In_ POBJECT_TYPE ObjectType,
    _In_ SECURITY_OPERATION_CODE Operation,
    _In_opt_ PSECURITY_INFORMATION SecurityInformation,
    _In_opt_ PSECURITY_DESCRIPTOR SecurityDescriptor,
    _Inout_opt_ PULONG CapturedLength,
    _In_ KPROCESSOR_MODE AccessMode)
{
    POBJECT_HEADER ObjectHeader = OBJECT_TO_OBJECT_HEADER(Object);
    NTSTATUS Status;
    KIRQL CalloutIrql;

    ObpCalloutStart(&CalloutIrql);
    Status = ObjectType->TypeInfo.SecurityProcedure(Object,
                                                    Operation,
                                                    SecurityInformation,
                                                    SecurityDescriptor,
                                                    CapturedLength,
                                                    &ObjectHeader->SecurityDescriptor,
                                                    PagedPool,
                                                    &ObjectType->TypeInfo.GenericMapping,
                                                    AccessMode);
    ObpCalloutEnd(CalloutIrql, "Security", ObjectType, Object);

    return Status;
}

/*
 * Runs one SeAccessCheck against the object's own descriptor. The caller keeps
 * ownership of the access state; only ObCheckObjectAccess folds the result
 * back into it.
 */
static
BOOLEAN
ObpAccessCheckAgainstObject(
    _In_ PVOID Object,
    _In_ ACCESS_MASK DesiredAccess,
    _In_ ACCESS_MASK PreviouslyGranted,
    _Inout_ PACCESS_STATE AccessState,
    _In_ BOOLEAN LockHeld,
    _In_ KPROCESSOR_MODE AccessMode,
    _In_ BOOLEAN Audit,
    _In_ BOOLEAN RecordPrivileges,
    _In_opt_ PUNICODE_STRING ObjectName,
    _Out_ PACCESS_MASK GrantedAccess,
    _Out_ PNTSTATUS AccessStatus)
{
    POBJECT_HEADER ObjectHeader = OBJECT_TO_OBJECT_HEADER(Object);
    POBJECT_TYPE ObjectType = ObpGetObjectTypeFromHeader(ObjectHeader);
    PSECURITY_DESCRIPTOR SecurityDescriptor;
    PPRIVILEGE_SET Privileges = NULL;
    BOOLEAN SdAllocated;
    BOOLEAN Result;
    NTSTATUS Status;

    PAGED_CODE();

    /* The descriptor has to come from the object type: a registry key or a
     * token keeps it somewhere the object header does not point at */
    Status = ObGetObjectSecurity(Object, &SecurityDescriptor, &SdAllocated);
    if (!NT_SUCCESS(Status))
    {
        *AccessStatus = Status;
        return FALSE;
    }

    if (SecurityDescriptor == NULL)
    {
        /* Nothing protects the object, so the request is granted as asked for */
        if (DesiredAccess & MAXIMUM_ALLOWED)
        {
            *GrantedAccess = ObjectType->TypeInfo.GenericMapping.GenericAll |
                             (DesiredAccess & ~MAXIMUM_ALLOWED);
        }
        else
        {
            *GrantedAccess = DesiredAccess;
        }

        *GrantedAccess |= PreviouslyGranted;
        *AccessStatus = STATUS_SUCCESS;
        ObReleaseObjectSecurity(SecurityDescriptor, SdAllocated);
        return TRUE;
    }

    if (!LockHeld) SeLockSubjectContext(&AccessState->SubjectSecurityContext);

    Result = SeAccessCheck(SecurityDescriptor,
                           &AccessState->SubjectSecurityContext,
                           TRUE,
                           DesiredAccess,
                           PreviouslyGranted,
                           RecordPrivileges ? &Privileges : NULL,
                           &ObjectType->TypeInfo.GenericMapping,
                           AccessMode,
                           GrantedAccess,
                           AccessStatus);

    if (Privileges != NULL)
    {
        if (!NT_SUCCESS(SeAppendPrivileges(AccessState, Privileges)))
        {
            /* The privileges could not be recorded; they are still released */
            DPRINT1("OB: could not append privileges to the access state\n");
        }
        SeFreePrivileges(Privileges);
    }

    if (Audit)
    {
        SeOpenObjectAuditAlarm(&ObjectType->Name,
                               Object,
                               ObjectName,
                               SecurityDescriptor,
                               AccessState,
                               FALSE,
                               Result,
                               AccessMode,
                               &AccessState->GenerateOnClose);
    }

    if (!LockHeld) SeUnlockSubjectContext(&AccessState->SubjectSecurityContext);

    ObReleaseObjectSecurity(SecurityDescriptor, SdAllocated);
    return Result;
}

/* OBJECT SECURITY METHODS ****************************************************/

NTSTATUS
NTAPI
ObAssignObjectSecurityDescriptor(
    _In_ PVOID Object,
    _In_opt_ PSECURITY_DESCRIPTOR SecurityDescriptor,
    _In_ POOL_TYPE PoolType)
{
    POBJECT_HEADER ObjectHeader = OBJECT_TO_OBJECT_HEADER(Object);
    PEX_FAST_REF FastRef = (PEX_FAST_REF)&ObjectHeader->SecurityDescriptor;
    PSECURITY_DESCRIPTOR Cached;
    NTSTATUS Status;

    PAGED_CODE();
    UNREFERENCED_PARAMETER(PoolType);

    if (SecurityDescriptor == NULL)
    {
        ExInitializeFastReference(FastRef, NULL);
        return STATUS_SUCCESS;
    }

    /* One reference belongs to the object, the rest are handed to the pointer */
    Status = ObLogSecurityDescriptor(SecurityDescriptor, &Cached, MAX_FAST_REFS + 1);
    if (!NT_SUCCESS(Status)) return Status;

    /* The assign operation takes ownership of the descriptor it was given */
    ExFreePoolWithTag(SecurityDescriptor, TAG_SD);

    ASSERT(Cached != NULL);
    ExInitializeFastReference(FastRef, Cached);
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
ObDeassignSecurity(
    _Inout_ PSECURITY_DESCRIPTOR *SecurityDescriptor)
{
    EX_FAST_REF FastRef = *(PEX_FAST_REF)SecurityDescriptor;
    PSECURITY_DESCRIPTOR Cached = ExGetObjectFastReference(FastRef);

    PAGED_CODE();

    *SecurityDescriptor = NULL;

    /* Give back the object's own reference and every one still cached */
    if (Cached != NULL)
        ObDereferenceSecurityDescriptor(Cached, ExGetCountFastReference(FastRef) + 1);

    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
ObQuerySecurityDescriptorInfo(
    _In_ PVOID Object,
    _In_ PSECURITY_INFORMATION SecurityInformation,
    _Out_ PSECURITY_DESCRIPTOR SecurityDescriptor,
    _Inout_ PULONG Length,
    _Inout_ PSECURITY_DESCRIPTOR *OutputSecurityDescriptor)
{
    PSECURITY_DESCRIPTOR ObjectSd;
    NTSTATUS Status;

    PAGED_CODE();
    UNREFERENCED_PARAMETER(OutputSecurityDescriptor);

    ObjectSd = ObpReferenceSecurityDescriptor(OBJECT_TO_OBJECT_HEADER(Object));

    Status = SeQuerySecurityDescriptorInfo(SecurityInformation,
                                           SecurityDescriptor,
                                           Length,
                                           &ObjectSd);

    if (ObjectSd != NULL) ObDereferenceSecurityDescriptor(ObjectSd, 1);
    return Status;
}

NTSTATUS
NTAPI
ObSetSecurityDescriptorInfo(
    _In_ PVOID Object,
    _In_ PSECURITY_INFORMATION SecurityInformation,
    _Inout_ PSECURITY_DESCRIPTOR SecurityDescriptor,
    _Inout_ PSECURITY_DESCRIPTOR *OutputSecurityDescriptor,
    _In_ POOL_TYPE PoolType,
    _In_ PGENERIC_MAPPING GenericMapping)
{
    PEX_FAST_REF FastRef = (PEX_FAST_REF)OutputSecurityDescriptor;
    PSECURITY_DESCRIPTOR OldDescriptor, NewDescriptor, Cached;
    EX_FAST_REF OldValue;
    NTSTATUS Status;

    PAGED_CODE();

    for (;;)
    {
        OldValue = *FastRef;
        OldDescriptor = ExGetObjectFastReference(OldValue);

        /* Hold the current descriptor still while the SRM builds the new one */
        if (OldDescriptor != NULL) ObReferenceSecurityDescriptor(OldDescriptor, 1);

        NewDescriptor = OldDescriptor;
        Status = SeSetSecurityDescriptorInfo(Object,
                                             SecurityInformation,
                                             SecurityDescriptor,
                                             &NewDescriptor,
                                             PoolType,
                                             GenericMapping);
        if (!NT_SUCCESS(Status))
        {
            if (OldDescriptor != NULL) ObDereferenceSecurityDescriptor(OldDescriptor, 1);
            return Status;
        }

        Status = ObLogSecurityDescriptor(NewDescriptor, &Cached, MAX_FAST_REFS + 1);
        ExFreePool(NewDescriptor);
        if (!NT_SUCCESS(Status))
        {
            if (OldDescriptor != NULL) ObDereferenceSecurityDescriptor(OldDescriptor, 1);
            return Status;
        }

        OldValue = ExCompareSwapFastReference(FastRef, Cached, OldDescriptor);
        if (ExGetObjectFastReference(OldValue) == OldDescriptor)
        {
            /* The cached references the pointer held, the object's own one and
             * the one we took for the SRM above all go away together */
            if (OldDescriptor != NULL)
            {
                ObDereferenceSecurityDescriptor(OldDescriptor,
                                                ExGetCountFastReference(OldValue) + 2);
            }
            return STATUS_SUCCESS;
        }

        /* Somebody replaced the descriptor first; start over */
        if (OldDescriptor != NULL) ObDereferenceSecurityDescriptor(OldDescriptor, 1);
        ObDereferenceSecurityDescriptor(Cached, MAX_FAST_REFS + 1);
    }
}

/* ACCESS CHECKS **************************************************************/

BOOLEAN
NTAPI
ObCheckObjectAccess(
    _In_ PVOID Object,
    _Inout_ PACCESS_STATE AccessState,
    _In_ BOOLEAN LockHeld,
    _In_ KPROCESSOR_MODE AccessMode,
    _Out_ PNTSTATUS ReturnedStatus)
{
    ACCESS_MASK GrantedAccess = 0;
    BOOLEAN Result;

    PAGED_CODE();

    Result = ObpAccessCheckAgainstObject(Object,
                                         AccessState->RemainingDesiredAccess,
                                         AccessState->PreviouslyGrantedAccess,
                                         AccessState,
                                         LockHeld,
                                         AccessMode,
                                         TRUE,
                                         TRUE,
                                         &AccessState->ObjectName,
                                         &GrantedAccess,
                                         ReturnedStatus);
    if (!Result) return FALSE;

    AccessState->RemainingDesiredAccess &= ~GrantedAccess;
    AccessState->RemainingDesiredAccess &= ~MAXIMUM_ALLOWED;
    AccessState->PreviouslyGrantedAccess |= GrantedAccess;
    return TRUE;
}

BOOLEAN
NTAPI
ObCheckCreateObjectAccess(
    _In_ PVOID Object,
    _In_ ACCESS_MASK CreateAccess,
    _In_ PACCESS_STATE AccessState,
    _In_ PUNICODE_STRING ComponentName,
    _In_ BOOLEAN LockHeld,
    _In_ KPROCESSOR_MODE AccessMode,
    _Out_ PNTSTATUS AccessStatus)
{
    ACCESS_MASK GrantedAccess = 0;

    PAGED_CODE();

    return ObpAccessCheckAgainstObject(Object,
                                       CreateAccess,
                                       0,
                                       AccessState,
                                       LockHeld,
                                       AccessMode,
                                       TRUE,
                                       TRUE,
                                       ComponentName,
                                       &GrantedAccess,
                                       AccessStatus);
}

BOOLEAN
NTAPI
ObpCheckTraverseAccess(
    _In_ PVOID Object,
    _In_ ACCESS_MASK TraverseAccess,
    _In_opt_ PACCESS_STATE AccessState,
    _In_ BOOLEAN LockHeld,
    _In_ KPROCESSOR_MODE AccessMode,
    _Out_ PNTSTATUS AccessStatus)
{
    PSECURITY_DESCRIPTOR SecurityDescriptor;
    ACCESS_MASK GrantedAccess = 0;
    ACCESS_STATE LocalState;
    BOOLEAN SdAllocated;
    BOOLEAN Result;

    PAGED_CODE();

    *AccessStatus = STATUS_SUCCESS;

    /* A caller holding SeChangeNotifyPrivilege traverses without a check */
    if (!NT_SUCCESS(ObGetObjectSecurity(Object, &SecurityDescriptor, &SdAllocated)))
        return TRUE;
    if (SecurityDescriptor == NULL) return TRUE;

    if (AccessState != NULL)
    {
        Result = SeFastTraverseCheck(SecurityDescriptor,
                                     AccessState,
                                     TraverseAccess,
                                     AccessMode);
        ObReleaseObjectSecurity(SecurityDescriptor, SdAllocated);
        if (Result) return TRUE;

        return ObpAccessCheckAgainstObject(Object,
                                           TraverseAccess,
                                           0,
                                           AccessState,
                                           LockHeld,
                                           AccessMode,
                                           FALSE,
                                           TRUE,
                                           NULL,
                                           &GrantedAccess,
                                           AccessStatus);
    }

    ObReleaseObjectSecurity(SecurityDescriptor, SdAllocated);

    /* Without an access state there is nothing to carry privileges in, so run
     * the check against a throwaway one built from the current subject */
    RtlZeroMemory(&LocalState, sizeof(LocalState));
    SeCaptureSubjectContext(&LocalState.SubjectSecurityContext);
    LocalState.RemainingDesiredAccess = TraverseAccess;

    Result = ObpAccessCheckAgainstObject(Object,
                                         TraverseAccess,
                                         0,
                                         &LocalState,
                                         FALSE,
                                         AccessMode,
                                         FALSE,
                                         FALSE,
                                         NULL,
                                         &GrantedAccess,
                                         AccessStatus);

    SeReleaseSubjectContext(&LocalState.SubjectSecurityContext);
    return Result;
}

BOOLEAN
NTAPI
ObpCheckObjectReference(
    _In_ PVOID Object,
    _Inout_ PACCESS_STATE AccessState,
    _In_ BOOLEAN LockHeld,
    _In_ KPROCESSOR_MODE AccessMode,
    _Out_ PNTSTATUS AccessStatus)
{
    ACCESS_MASK GrantedAccess = 0;
    BOOLEAN Result;

    PAGED_CODE();

    Result = ObpAccessCheckAgainstObject(Object,
                                         AccessState->RemainingDesiredAccess,
                                         AccessState->PreviouslyGrantedAccess,
                                         AccessState,
                                         LockHeld,
                                         AccessMode,
                                         TRUE,
                                         TRUE,
                                         &AccessState->ObjectName,
                                         &GrantedAccess,
                                         AccessStatus);
    if (!Result) return FALSE;

    AccessState->RemainingDesiredAccess &= ~GrantedAccess;
    AccessState->PreviouslyGrantedAccess |= GrantedAccess;
    return TRUE;
}

/* PUBLIC FUNCTIONS ***********************************************************/

NTSTATUS
NTAPI
ObAssignSecurity(
    _In_ PACCESS_STATE AccessState,
    _In_opt_ PSECURITY_DESCRIPTOR SecurityDescriptor,
    _In_ PVOID Object,
    _In_ POBJECT_TYPE Type)
{
    PSECURITY_DESCRIPTOR NewDescriptor;
    NTSTATUS Status;

    PAGED_CODE();

    if (Type == SeTokenObjectType)
    {
        Status = ObpAssignTokenSecurity(AccessState,
                                        SecurityDescriptor,
                                        Object,
                                        &NewDescriptor,
                                        Type);
    }
    else
    {
        Status = SeAssignSecurity(SecurityDescriptor,
                                  AccessState->SecurityDescriptor,
                                  &NewDescriptor,
                                  (Type == ObpDirectoryObjectType),
                                  &AccessState->SubjectSecurityContext,
                                  &Type->TypeInfo.GenericMapping,
                                  PagedPool);
    }
    if (!NT_SUCCESS(Status)) return Status;

    /* The security method owns the descriptor from here on when it succeeds */
    Status = ObpCallSecurityProcedure(Object,
                                      Type,
                                      AssignSecurityDescriptor,
                                      NULL,
                                      NewDescriptor,
                                      NULL,
                                      KernelMode);
    if (!NT_SUCCESS(Status)) SeDeassignSecurity(&NewDescriptor);

    return Status;
}

NTSTATUS
NTAPI
ObGetObjectSecurity(
    _In_ PVOID Object,
    _Out_ PSECURITY_DESCRIPTOR *SecurityDescriptor,
    _Out_ PBOOLEAN MemoryAllocated)
{
    POBJECT_HEADER ObjectHeader = OBJECT_TO_OBJECT_HEADER(Object);
    POBJECT_TYPE ObjectType = ObpGetObjectTypeFromHeader(ObjectHeader);
    SECURITY_INFORMATION SecurityInformation;
    NTSTATUS Status;
    ULONG Length;

    PAGED_CODE();

    /* An object that keeps its descriptor in our cache is handed out directly */
    if (ObjectType->TypeInfo.SecurityProcedure == SeDefaultObjectMethod)
    {
        *SecurityDescriptor = ObpReferenceSecurityDescriptor(ObjectHeader);
        *MemoryAllocated = FALSE;
        return STATUS_SUCCESS;
    }

    SecurityInformation = OWNER_SECURITY_INFORMATION |
                          GROUP_SECURITY_INFORMATION |
                          DACL_SECURITY_INFORMATION |
                          SACL_SECURITY_INFORMATION;

    /* Ask the type how much room its descriptor needs */
    Length = 0;
    Status = ObpCallSecurityProcedure(Object,
                                      ObjectType,
                                      QuerySecurityDescriptor,
                                      &SecurityInformation,
                                      NULL,
                                      &Length,
                                      KernelMode);
    if (Status != STATUS_BUFFER_TOO_SMALL) return Status;

    *SecurityDescriptor = ExAllocatePoolWithTag(PagedPool, Length, TAG_SEC_QUERY);
    if (*SecurityDescriptor == NULL) return STATUS_INSUFFICIENT_RESOURCES;

    Status = ObpCallSecurityProcedure(Object,
                                      ObjectType,
                                      QuerySecurityDescriptor,
                                      &SecurityInformation,
                                      *SecurityDescriptor,
                                      &Length,
                                      KernelMode);
    if (!NT_SUCCESS(Status))
    {
        ExFreePoolWithTag(*SecurityDescriptor, TAG_SEC_QUERY);
        *SecurityDescriptor = NULL;
        return Status;
    }

    *MemoryAllocated = TRUE;
    return STATUS_SUCCESS;
}

VOID
NTAPI
ObReleaseObjectSecurity(
    _In_ PSECURITY_DESCRIPTOR SecurityDescriptor,
    _In_ BOOLEAN MemoryAllocated)
{
    PAGED_CODE();

    if (SecurityDescriptor == NULL) return;

    if (MemoryAllocated)
        ExFreePoolWithTag(SecurityDescriptor, TAG_SEC_QUERY);
    else
        ObDereferenceSecurityDescriptor(SecurityDescriptor, 1);
}

NTSTATUS
NTAPI
ObSetSecurityObjectByPointer(
    _In_ PVOID Object,
    _In_ SECURITY_INFORMATION SecurityInformation,
    _In_ PSECURITY_DESCRIPTOR SecurityDescriptor)
{
    POBJECT_HEADER ObjectHeader = OBJECT_TO_OBJECT_HEADER(Object);
    POBJECT_TYPE ObjectType = ObpGetObjectTypeFromHeader(ObjectHeader);

    PAGED_CODE();

    return ObpCallSecurityProcedure(Object,
                                    ObjectType,
                                    SetSecurityDescriptor,
                                    &SecurityInformation,
                                    SecurityDescriptor,
                                    NULL,
                                    KernelMode);
}

NTSTATUS
NTAPI
ObQueryObjectAuditingByHandle(
    _In_ HANDLE Handle,
    _Out_ PBOOLEAN GenerateOnClose)
{
    PHANDLE_TABLE HandleTable = PsGetCurrentProcess()->ObjectTable;
    PHANDLE_TABLE_ENTRY HandleEntry;

    PAGED_CODE();

    /* Only a driver can reach this export, so a kernel handle always belongs
     * to the kernel table whatever mode the current system call came from */
    if (ObpIsKernelHandle(Handle, KernelMode))
    {
        Handle = ObKernelHandleToHandle(Handle);
        HandleTable = ObpKernelHandleTable;
    }

    KeEnterCriticalRegion();

    HandleEntry = ExMapHandleToPointer(HandleTable, Handle);
    if (HandleEntry == NULL)
    {
        KeLeaveCriticalRegion();
        return STATUS_INVALID_HANDLE;
    }

    *GenerateOnClose = (((ULONG_PTR)HandleEntry->Object &
                         OBJ_AUDIT_OBJECT_CLOSE) != 0);

    ExUnlockHandleTableEntry(HandleTable, HandleEntry);
    KeLeaveCriticalRegion();
    return STATUS_SUCCESS;
}

/* SYSTEM CALLS ***************************************************************/

static
ACCESS_MASK
ObpAccessForQuerySecurity(
    _In_ SECURITY_INFORMATION SecurityInformation)
{
    ACCESS_MASK DesiredAccess = 0;

    if (SecurityInformation & (OWNER_SECURITY_INFORMATION |
                               GROUP_SECURITY_INFORMATION |
                               DACL_SECURITY_INFORMATION |
                               LABEL_SECURITY_INFORMATION))
    {
        DesiredAccess |= READ_CONTROL;
    }

    if (SecurityInformation & SACL_SECURITY_INFORMATION)
        DesiredAccess |= ACCESS_SYSTEM_SECURITY;

    return DesiredAccess;
}

static
ACCESS_MASK
ObpAccessForSetSecurity(
    _In_ SECURITY_INFORMATION SecurityInformation)
{
    ACCESS_MASK DesiredAccess = 0;

    if (SecurityInformation & (OWNER_SECURITY_INFORMATION |
                               GROUP_SECURITY_INFORMATION))
    {
        DesiredAccess |= WRITE_OWNER;
    }

    if (SecurityInformation & DACL_SECURITY_INFORMATION)
        DesiredAccess |= WRITE_DAC;

    if (SecurityInformation & SACL_SECURITY_INFORMATION)
        DesiredAccess |= ACCESS_SYSTEM_SECURITY;

    if (SecurityInformation & LABEL_SECURITY_INFORMATION)
        DesiredAccess |= WRITE_OWNER;

    return DesiredAccess;
}

NTSTATUS
NTAPI
NtQuerySecurityObject(
    _In_ HANDLE Handle,
    _In_ SECURITY_INFORMATION SecurityInformation,
    _Out_writes_bytes_opt_(Length) PSECURITY_DESCRIPTOR SecurityDescriptor,
    _In_ ULONG Length,
    _Out_ PULONG ResultLength)
{
    KPROCESSOR_MODE PreviousMode = ExGetPreviousMode();
    POBJECT_HEADER ObjectHeader;
    POBJECT_TYPE ObjectType;
    ACCESS_MASK DesiredAccess;
    NTSTATUS Status;
    PVOID Object;
    ULONG Size = Length;

    PAGED_CODE();

    if (PreviousMode != KernelMode)
    {
        _SEH2_TRY
        {
            ProbeForWriteUlong(ResultLength);
            if (Length != 0)
                ProbeForWrite(SecurityDescriptor, Length, sizeof(ULONG));
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            _SEH2_YIELD(return _SEH2_GetExceptionCode());
        }
        _SEH2_END;
    }

    DesiredAccess = ObpAccessForQuerySecurity(SecurityInformation);

    Status = ObReferenceObjectByHandle(Handle,
                                       DesiredAccess,
                                       NULL,
                                       PreviousMode,
                                       &Object,
                                       NULL);
    if (!NT_SUCCESS(Status)) return Status;

    ObjectHeader = OBJECT_TO_OBJECT_HEADER(Object);
    ObjectType = ObpGetObjectTypeFromHeader(ObjectHeader);

    Status = ObpCallSecurityProcedure(Object,
                                      ObjectType,
                                      QuerySecurityDescriptor,
                                      &SecurityInformation,
                                      SecurityDescriptor,
                                      &Size,
                                      PreviousMode);

    ObDereferenceObject(Object);

    _SEH2_TRY
    {
        *ResultLength = Size;
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
NtSetSecurityObject(
    _In_ HANDLE Handle,
    _In_ SECURITY_INFORMATION SecurityInformation,
    _In_ PSECURITY_DESCRIPTOR SecurityDescriptor)
{
    KPROCESSOR_MODE PreviousMode = ExGetPreviousMode();
    PSECURITY_DESCRIPTOR CapturedDescriptor;
    PISECURITY_DESCRIPTOR Captured;
    POBJECT_HEADER ObjectHeader;
    POBJECT_TYPE ObjectType;
    ACCESS_MASK DesiredAccess;
    NTSTATUS Status;
    PVOID Object;

    PAGED_CODE();

    if (SecurityInformation == 0) return STATUS_INVALID_PARAMETER;

    Status = SeCaptureSecurityDescriptor(SecurityDescriptor,
                                         PreviousMode,
                                         PagedPool,
                                         FALSE,
                                         &CapturedDescriptor);
    if (!NT_SUCCESS(Status)) return Status;
    if (CapturedDescriptor == NULL) return STATUS_INVALID_SECURITY_DESCR;

    /* Each class of information needs the part of the descriptor it names */
    Captured = CapturedDescriptor;
    if (((SecurityInformation & OWNER_SECURITY_INFORMATION) &&
         SepGetOwnerFromDescriptor(Captured) == NULL) ||
        ((SecurityInformation & GROUP_SECURITY_INFORMATION) &&
         SepGetGroupFromDescriptor(Captured) == NULL) ||
        ((SecurityInformation & DACL_SECURITY_INFORMATION) &&
         !(Captured->Control & SE_DACL_PRESENT)) ||
        ((SecurityInformation & SACL_SECURITY_INFORMATION) &&
         !(Captured->Control & SE_SACL_PRESENT)))
    {
        SeReleaseSecurityDescriptor(CapturedDescriptor, PreviousMode, FALSE);
        return STATUS_INVALID_SECURITY_DESCR;
    }

    /* A caller may not raise an object above its own integrity level */
    if ((SecurityInformation & LABEL_SECURITY_INFORMATION) &&
        PreviousMode != KernelMode &&
        !ObpLabelWithinCallerIntegrity(CapturedDescriptor))
    {
        SeReleaseSecurityDescriptor(CapturedDescriptor, PreviousMode, FALSE);
        return STATUS_ACCESS_DENIED;
    }

    DesiredAccess = ObpAccessForSetSecurity(SecurityInformation);

    Status = ObReferenceObjectByHandle(Handle,
                                       DesiredAccess,
                                       NULL,
                                       PreviousMode,
                                       &Object,
                                       NULL);
    if (NT_SUCCESS(Status))
    {
        ObjectHeader = OBJECT_TO_OBJECT_HEADER(Object);
        ObjectType = ObpGetObjectTypeFromHeader(ObjectHeader);

        Status = ObpCallSecurityProcedure(Object,
                                          ObjectType,
                                          SetSecurityDescriptor,
                                          &SecurityInformation,
                                          CapturedDescriptor,
                                          NULL,
                                          PreviousMode);
        ObDereferenceObject(Object);
    }

    SeReleaseSecurityDescriptor(CapturedDescriptor, PreviousMode, FALSE);
    return Status;
}

/* EOF */
