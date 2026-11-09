/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Object security
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

/* PRIVATE DATA ***************************************************************/

/*
 * The header's descriptor field is an EX_FAST_REF: one reference belongs to the
 * object and MAX_FAST_REFS more are parked in the pointer for its readers.
 */
#define OBP_SD_FIELD_CHARGE (MAX_FAST_REFS + 1)

#define OBP_SI_SUPPORTED                        \
    (OWNER_SECURITY_INFORMATION |               \
     GROUP_SECURITY_INFORMATION |               \
     DACL_SECURITY_INFORMATION |                \
     SACL_SECURITY_INFORMATION |                \
     LABEL_SECURITY_INFORMATION |               \
     ATTRIBUTE_SECURITY_INFORMATION |           \
     SCOPE_SECURITY_INFORMATION |               \
     PROCESS_TRUST_LABEL_SECURITY_INFORMATION | \
     PROTECTED_DACL_SECURITY_INFORMATION |      \
     PROTECTED_SACL_SECURITY_INFORMATION |      \
     UNPROTECTED_DACL_SECURITY_INFORMATION |    \
     UNPROTECTED_SACL_SECURITY_INFORMATION)
/* TODO: BACKUP_SECURITY_INFORMATION, which the SDK headers in the tree do not
 * define, is rejected as an unsupported bit */

#define OBP_SI_WHOLE_DESCRIPTOR   \
    (OWNER_SECURITY_INFORMATION | \
     GROUP_SECURITY_INFORMATION | \
     DACL_SECURITY_INFORMATION |  \
     SACL_SECURITY_INFORMATION)

/* PRIVATE FUNCTIONS **********************************************************/

static
PEX_FAST_REF
ObpSecurityField(
    _In_ PVOID Object)
{
    return (PEX_FAST_REF)&OBJECT_TO_OBJECT_HEADER(Object)->SecurityDescriptor;
}

static
ACCESS_MASK
ObpAccessForQueryingSecurity(
    _In_ SECURITY_INFORMATION SecurityInformation)
{
    ACCESS_MASK Access = 0;

    if (SecurityInformation & ~SACL_SECURITY_INFORMATION) Access |= READ_CONTROL;
    if (SecurityInformation & SACL_SECURITY_INFORMATION)
        Access |= ACCESS_SYSTEM_SECURITY;

    return Access;
}

static
ACCESS_MASK
ObpAccessForSettingSecurity(
    _In_ SECURITY_INFORMATION SecurityInformation)
{
    ACCESS_MASK Access = 0;

    if (SecurityInformation & (OWNER_SECURITY_INFORMATION |
                               GROUP_SECURITY_INFORMATION |
                               LABEL_SECURITY_INFORMATION))
    {
        Access |= WRITE_OWNER;
    }

    if (SecurityInformation & (DACL_SECURITY_INFORMATION |
                               ATTRIBUTE_SECURITY_INFORMATION |
                               PROTECTED_DACL_SECURITY_INFORMATION |
                               UNPROTECTED_DACL_SECURITY_INFORMATION))
    {
        Access |= WRITE_DAC;
    }

    if (SecurityInformation & (SACL_SECURITY_INFORMATION |
                               SCOPE_SECURITY_INFORMATION |
                               PROCESS_TRUST_LABEL_SECURITY_INFORMATION |
                               PROTECTED_SACL_SECURITY_INFORMATION |
                               UNPROTECTED_SACL_SECURITY_INFORMATION))
    {
        Access |= ACCESS_SYSTEM_SECURITY;
    }

    return Access;
}

/* The descriptor has to carry every part the caller says it is setting. */
static
NTSTATUS
ObpCheckDescriptorCompleteness(
    _In_ SECURITY_INFORMATION SecurityInformation,
    _In_ PSECURITY_DESCRIPTOR Descriptor)
{
    if ((SecurityInformation & OWNER_SECURITY_INFORMATION) &&
        SepGetOwnerFromDescriptor(Descriptor) == NULL)
    {
        return STATUS_INVALID_OWNER;
    }

    if ((SecurityInformation & GROUP_SECURITY_INFORMATION) &&
        SepGetGroupFromDescriptor(Descriptor) == NULL)
    {
        return STATUS_INVALID_PRIMARY_GROUP;
    }

    return STATUS_SUCCESS;
}

static
NTSTATUS
ObpCallSecurityMethod(
    _In_ PVOID Object,
    _In_ SECURITY_OPERATION_CODE Operation,
    _In_opt_ PSECURITY_INFORMATION SecurityInformation,
    _In_opt_ PSECURITY_DESCRIPTOR Descriptor,
    _Inout_opt_ PULONG CapturedLength,
    _In_ KPROCESSOR_MODE AccessMode)
{
    POBJECT_HEADER Header = OBJECT_TO_OBJECT_HEADER(Object);
    POBJECT_TYPE Type = ObpGetObjectTypeFromHeader(Header);
    NTSTATUS Status;
    KIRQL CalloutIrql;

    ObpCalloutStart(&CalloutIrql);
    Status = Type->TypeInfo.SecurityProcedure(Object,
                                              Operation,
                                              SecurityInformation,
                                              Descriptor,
                                              CapturedLength,
                                              &Header->SecurityDescriptor,
                                              Type->TypeInfo.PoolType,
                                              &Type->TypeInfo.GenericMapping,
                                              AccessMode);
    ObpCalloutEnd(CalloutIrql, "Security", Type, Object);

    return Status;
}

/*
 * Runs one access check against the descriptor the object's type reports, so a
 * type with its own security method is asked rather than read.
 */
static
BOOLEAN
ObpAccessCheckObject(
    _In_ PVOID Object,
    _In_ ACCESS_MASK DesiredAccess,
    _In_ ACCESS_MASK PreviouslyGranted,
    _In_opt_ PACCESS_STATE AccessState,
    _In_ BOOLEAN LockHeld,
    _In_ KPROCESSOR_MODE AccessMode,
    _Out_ PACCESS_MASK GrantedAccess,
    _Out_ PNTSTATUS AccessStatus)
{
    POBJECT_TYPE Type = ObpGetObjectTypeFromHeader(OBJECT_TO_OBJECT_HEADER(Object));
    SECURITY_SUBJECT_CONTEXT LocalContext;
    PSECURITY_SUBJECT_CONTEXT Subject;
    PSECURITY_DESCRIPTOR Descriptor;
    PPRIVILEGE_SET Privileges = NULL;
    BOOLEAN Allocated, Granted;
    NTSTATUS Status;

    *GrantedAccess = 0;

    Status = ObGetObjectSecurity(Object, &Descriptor, &Allocated);
    if (!NT_SUCCESS(Status))
    {
        *AccessStatus = Status;
        return FALSE;
    }

    /* An object nobody secured is open to everyone, and a request for whatever
     * the object allows then means everything its mapping defines */
    if (Descriptor == NULL)
    {
        *GrantedAccess = DesiredAccess | PreviouslyGranted;
        if (*GrantedAccess & MAXIMUM_ALLOWED)
        {
            *GrantedAccess &= ~MAXIMUM_ALLOWED;
            *GrantedAccess |= Type->TypeInfo.GenericMapping.GenericAll;
        }

        *AccessStatus = STATUS_SUCCESS;
        ObReleaseObjectSecurity(Descriptor, Allocated);
        return TRUE;
    }

    if (AccessState != NULL)
    {
        Subject = &AccessState->SubjectSecurityContext;
    }
    else
    {
        SeCaptureSubjectContext(&LocalContext);
        Subject = &LocalContext;
    }

    if (!LockHeld) SeLockSubjectContext(Subject);

    Granted = SeAccessCheck(Descriptor,
                            Subject,
                            TRUE,
                            DesiredAccess,
                            PreviouslyGranted,
                            &Privileges,
                            &Type->TypeInfo.GenericMapping,
                            AccessMode,
                            GrantedAccess,
                            &Status);

    if (Privileges != NULL)
    {
        if (AccessState != NULL) SeAppendPrivileges(AccessState, Privileges);
        SeFreePrivileges(Privileges);
    }

    if (!LockHeld) SeUnlockSubjectContext(Subject);
    if (AccessState == NULL) SeReleaseSubjectContext(&LocalContext);

    ObReleaseObjectSecurity(Descriptor, Allocated);

    *AccessStatus = Status;
    return Granted;
}

/* Moves what was granted out of the access state's remaining request. */
static
VOID
ObpBankGrantedAccess(
    _Inout_ PACCESS_STATE AccessState,
    _In_ ACCESS_MASK GrantedAccess)
{
    AccessState->PreviouslyGrantedAccess |= GrantedAccess;
    AccessState->RemainingDesiredAccess &= ~(GrantedAccess | MAXIMUM_ALLOWED);
}

static
NTSTATUS
ObpBuildLabelledSacl(
    _In_opt_ PACL OldSacl,
    _In_ PSID LabelSid,
    _Out_ PACL Sacl,
    _In_ ULONG SaclLength)
{
    PACE_HEADER Ace;
    NTSTATUS Status;
    ULONG Index;

    Status = RtlCreateAcl(Sacl,
                          SaclLength,
                          OldSacl ? OldSacl->AclRevision : ACL_REVISION);
    if (!NT_SUCCESS(Status)) return Status;

    if (OldSacl != NULL)
    {
        for (Index = 0; Index < OldSacl->AceCount; Index++)
        {
            Status = RtlGetAce(OldSacl, Index, (PVOID *)&Ace);
            if (!NT_SUCCESS(Status)) return Status;
            if (Ace->AceType == SYSTEM_MANDATORY_LABEL_ACE_TYPE) continue;

            Status = RtlAddAce(Sacl, Sacl->AclRevision, MAXULONG, Ace, Ace->AceSize);
            if (!NT_SUCCESS(Status)) return Status;
        }
    }

    return RtlAddMandatoryAce(Sacl,
                              Sacl->AclRevision,
                              0,
                              SYSTEM_MANDATORY_LABEL_NO_WRITE_UP,
                              SYSTEM_MANDATORY_LABEL_ACE_TYPE,
                              LabelSid);
}

/*
 * A token carries its own integrity level, so the descriptor it is created with
 * gets a mandatory label for that level instead of an inherited one.
 */
static
NTSTATUS
ObpAssignTokenSecurity(
    _In_ PACCESS_STATE AccessState,
    _In_opt_ PSECURITY_DESCRIPTOR ParentDescriptor,
    _In_ PTOKEN Token,
    _Out_ PSECURITY_DESCRIPTOR *NewDescriptor,
    _In_ POBJECT_TYPE Type)
{
    PISECURITY_DESCRIPTOR Explicit = AccessState->SecurityDescriptor;
    SECURITY_DESCRIPTOR Labelled;
    PACL OldSacl = NULL, Sacl;
    PACE_HEADER Ace;
    ULONG Index, Length;
    NTSTATUS Status;
    PSID LabelSid;

    LabelSid = SeMediumMandatorySid;
    if (Token->IntegrityLevelIndex != 0 &&
        Token->IntegrityLevelIndex < Token->UserAndGroupCount)
    {
        LabelSid = Token->UserAndGroups[Token->IntegrityLevelIndex].Sid;
    }

    RtlCreateSecurityDescriptor(&Labelled, SECURITY_DESCRIPTOR_REVISION);
    if (Explicit != NULL)
    {
        Labelled.Sbz1 = Explicit->Sbz1;
        Labelled.Control = Explicit->Control & ~SE_SELF_RELATIVE;
        Labelled.Owner = SepGetOwnerFromDescriptor(Explicit);
        Labelled.Group = SepGetGroupFromDescriptor(Explicit);
        Labelled.Dacl = SepGetDaclFromDescriptor(Explicit);
        OldSacl = SepGetSaclFromDescriptor(Explicit);
    }

    Length = sizeof(ACL) +
             FIELD_OFFSET(SYSTEM_MANDATORY_LABEL_ACE, SidStart) +
             RtlLengthSid(LabelSid);

    if (OldSacl != NULL)
    {
        for (Index = 0; Index < OldSacl->AceCount; Index++)
        {
            Status = RtlGetAce(OldSacl, Index, (PVOID *)&Ace);
            if (!NT_SUCCESS(Status)) return Status;
            if (Ace->AceType != SYSTEM_MANDATORY_LABEL_ACE_TYPE)
                Length += Ace->AceSize;
        }
    }

    if (Length > MAXUSHORT) return STATUS_ALLOTTED_SPACE_EXCEEDED;

    Sacl = ExAllocatePoolWithTag(PagedPool, Length, TAG_ACL);
    if (Sacl == NULL) return STATUS_INSUFFICIENT_RESOURCES;

    Status = ObpBuildLabelledSacl(OldSacl, LabelSid, Sacl, Length);
    if (NT_SUCCESS(Status))
    {
        RtlSetSaclSecurityDescriptor(&Labelled, TRUE, Sacl, FALSE);
        Status = SeAssignSecurityEx(ParentDescriptor,
                                    &Labelled,
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
    BOOLEAN EffectiveOnly;
    PACCESS_TOKEN Token;
    TOKEN_TYPE TokenType;
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

            LabelRid = *RtlSubAuthoritySid(
                &((PSYSTEM_MANDATORY_LABEL_ACE)Ace)->SidStart, 0);
            break;
        }
    }

    Token = PsReferenceEffectiveToken(PsGetCurrentThread(),
                                      &TokenType,
                                      &EffectiveOnly,
                                      &Level);
    TokenRid = SepGetTokenIntegrityRid(Token);
    if (TokenType == TokenPrimary)
        PsDereferencePrimaryToken(Token);
    else
        PsDereferenceImpersonationToken(Token);

    return LabelRid <= TokenRid;
}

/* DEFAULT OBJECT SECURITY METHOD BACK ENDS ***********************************/

NTSTATUS
NTAPI
ObAssignObjectSecurityDescriptor(
    _In_ PVOID Object,
    _In_opt_ PSECURITY_DESCRIPTOR SecurityDescriptor,
    _In_ POOL_TYPE PoolType)
{
    PEX_FAST_REF Field = ObpSecurityField(Object);
    PSECURITY_DESCRIPTOR Cached;
    NTSTATUS Status;

    UNREFERENCED_PARAMETER(PoolType);

    if (SecurityDescriptor == NULL)
    {
        ExInitializeFastReference(Field, NULL);
        return STATUS_SUCCESS;
    }

    Status = ObLogSecurityDescriptor(SecurityDescriptor,
                                     &Cached,
                                     OBP_SD_FIELD_CHARGE);
    if (!NT_SUCCESS(Status)) return Status;

    /* The cache keeps its own copy, so the one we were handed is consumed */
    ExFreePoolWithTag(SecurityDescriptor, TAG_SD);
    ExInitializeFastReference(Field, Cached);

    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
ObDeassignSecurity(
    _Inout_ PSECURITY_DESCRIPTOR *SecurityDescriptor)
{
    PEX_FAST_REF Field = (PEX_FAST_REF)SecurityDescriptor;
    PSECURITY_DESCRIPTOR Cached;
    EX_FAST_REF Old;

    Old = ExSwapFastReference(Field, NULL);
    Cached = ExGetObjectFastReference(Old);

    /* The field owned whatever it still had parked plus the object's own one */
    if (Cached != NULL)
    {
        ObDereferenceSecurityDescriptor(Cached,
                                        ExGetCountFastReference(Old) + 1);
    }

    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
ObQuerySecurityDescriptorInfo(
    _In_ PVOID Object,
    _In_ PSECURITY_INFORMATION SecurityInformation,
    _Out_ PSECURITY_DESCRIPTOR SecurityDescriptor,
    _Inout_ PULONG Length,
    _In_ PSECURITY_DESCRIPTOR *OutputSecurityDescriptor)
{
    PSECURITY_DESCRIPTOR Cached;
    NTSTATUS Status;

    UNREFERENCED_PARAMETER(OutputSecurityDescriptor);

    PAGED_CODE();

    /* Work from a reference of our own so the object cannot be relabelled
     * between the size calculation and the copy */
    Cached = ObpReferenceSecurityDescriptor(OBJECT_TO_OBJECT_HEADER(Object));

    Status = SeQuerySecurityDescriptorInfo(SecurityInformation,
                                           SecurityDescriptor,
                                           Length,
                                           &Cached);

    if (Cached != NULL) ObDereferenceSecurityDescriptor(Cached, 1);

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
    PEX_FAST_REF Field = (PEX_FAST_REF)OutputSecurityDescriptor;
    POBJECT_HEADER Header = OBJECT_TO_OBJECT_HEADER(Object);
    PSECURITY_DESCRIPTOR Current = NULL, Modified, Cached;
    NTSTATUS Status;
    EX_FAST_REF Old;

    PAGED_CODE();

    for (;;)
    {
        Current = ObpReferenceSecurityDescriptor(Header);

        Modified = Current;
        Status = SeSetSecurityDescriptorInfoEx(Object,
                                               SecurityInformation,
                                               SecurityDescriptor,
                                               &Modified,
                                               0,
                                               PoolType,
                                               GenericMapping);
        if (!NT_SUCCESS(Status)) break;

        Status = ObLogSecurityDescriptor(Modified, &Cached, OBP_SD_FIELD_CHARGE);
        ExFreePoolWithTag(Modified, TAG_SD);
        if (!NT_SUCCESS(Status)) break;

        Old = ExCompareSwapFastReference(Field, Cached, Current);
        if (ExGetObjectFastReference(Old) == Current)
        {
            /* The field's share plus the reference this loop took */
            if (Current != NULL)
            {
                ObDereferenceSecurityDescriptor(
                    Current, ExGetCountFastReference(Old) + 2);
            }

            return STATUS_SUCCESS;
        }

        /* Another setter won the field; drop ours and read the winner's */
        ObDereferenceSecurityDescriptor(Cached, OBP_SD_FIELD_CHARGE);
        if (Current != NULL) ObDereferenceSecurityDescriptor(Current, 1);
    }

    if (Current != NULL) ObDereferenceSecurityDescriptor(Current, 1);
    return Status;
}

/* ACCESS CHECKS **************************************************************/

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
    ACCESS_MASK Granted;

    UNREFERENCED_PARAMETER(ComponentName);

    PAGED_CODE();

    /* TODO: no object creation audit alarm is raised */
    return ObpAccessCheckObject(Object,
                                CreateAccess,
                                0,
                                AccessState,
                                LockHeld,
                                AccessMode,
                                &Granted,
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
    PSECURITY_DESCRIPTOR Descriptor;
    ACCESS_MASK Granted;
    BOOLEAN Allocated;
    NTSTATUS Status;

    PAGED_CODE();

    *AccessStatus = STATUS_SUCCESS;

    /* The kernel walks the namespace on its own authority */
    if (AccessMode == KernelMode) return TRUE;

    if (AccessState != NULL)
    {
        Status = ObGetObjectSecurity(Object, &Descriptor, &Allocated);
        if (!NT_SUCCESS(Status))
        {
            *AccessStatus = Status;
            return FALSE;
        }

        /* A descriptor that grants traversal to everyone needs no subject */
        if (Descriptor != NULL &&
            SeFastTraverseCheck(Descriptor, AccessState, TraverseAccess, AccessMode))
        {
            ObReleaseObjectSecurity(Descriptor, Allocated);
            return TRUE;
        }

        ObReleaseObjectSecurity(Descriptor, Allocated);
    }

    return ObpAccessCheckObject(Object,
                                TraverseAccess,
                                0,
                                AccessState,
                                LockHeld,
                                AccessMode,
                                &Granted,
                                AccessStatus);
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
    ACCESS_MASK Granted;
    BOOLEAN Allowed;

    PAGED_CODE();

    Allowed = ObpAccessCheckObject(Object,
                                   AccessState->RemainingDesiredAccess,
                                   AccessState->PreviouslyGrantedAccess,
                                   AccessState,
                                   LockHeld,
                                   AccessMode,
                                   &Granted,
                                   AccessStatus);
    if (Allowed) ObpBankGrantedAccess(AccessState, Granted);

    /* TODO: no object access audit alarm is raised */
    return Allowed;
}

BOOLEAN
NTAPI
ObCheckObjectAccess(
    _In_ PVOID Object,
    _Inout_ PACCESS_STATE AccessState,
    _In_ BOOLEAN LockHeld,
    _In_ KPROCESSOR_MODE AccessMode,
    _Out_ PNTSTATUS AccessStatus)
{
    ACCESS_MASK Granted;
    BOOLEAN Allowed;

    PAGED_CODE();

    Allowed = ObpAccessCheckObject(Object,
                                   AccessState->RemainingDesiredAccess,
                                   AccessState->PreviouslyGrantedAccess,
                                   AccessState,
                                   LockHeld,
                                   AccessMode,
                                   &Granted,
                                   AccessStatus);
    if (Allowed) ObpBankGrantedAccess(AccessState, Granted);

    return Allowed;
}

/* PUBLIC FUNCTIONS ***********************************************************/

/* A token carries the integrity its own descriptor has to be labelled with, so
 * it is the one type that does not take its label from its parent. */
static
NTSTATUS
ObpBuildAssignedDescriptor(
    _In_ PACCESS_STATE AccessState,
    _In_opt_ PSECURITY_DESCRIPTOR ParentDescriptor,
    _In_ PVOID Object,
    _In_ POBJECT_TYPE Type,
    _Out_ PSECURITY_DESCRIPTOR *NewDescriptor)
{
    if (Type == SeTokenObjectType)
    {
        return ObpAssignTokenSecurity(AccessState,
                                      ParentDescriptor,
                                      Object,
                                      NewDescriptor,
                                      Type);
    }

    return SeAssignSecurity(ParentDescriptor,
                            AccessState->SecurityDescriptor,
                            NewDescriptor,
                            (Type == ObpDirectoryObjectType),
                            &AccessState->SubjectSecurityContext,
                            &Type->TypeInfo.GenericMapping,
                            PagedPool);
}

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

    Status = ObpBuildAssignedDescriptor(AccessState,
                                        SecurityDescriptor,
                                        Object,
                                        Type,
                                        &NewDescriptor);
    if (!NT_SUCCESS(Status)) return Status;

    /* A method that takes the descriptor owns it from here */
    Status = ObpCallSecurityMethod(Object,
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
    POBJECT_HEADER Header = OBJECT_TO_OBJECT_HEADER(Object);
    POBJECT_TYPE Type = ObpGetObjectTypeFromHeader(Header);
    SECURITY_INFORMATION Information = OBP_SI_WHOLE_DESCRIPTOR;
    PSECURITY_DESCRIPTOR Buffer;
    NTSTATUS Status;
    ULONG Length;

    PAGED_CODE();

    *SecurityDescriptor = NULL;
    *MemoryAllocated = FALSE;

    /* A type that keeps its descriptor in the cache can lend it out directly */
    if (Type->TypeInfo.SecurityProcedure == SeDefaultObjectMethod)
    {
        *SecurityDescriptor = ObpReferenceSecurityDescriptor(Header);
        return STATUS_SUCCESS;
    }

    /* Anything else builds its descriptor on demand, so ask for the size */
    Length = 0;
    Status = ObpCallSecurityMethod(Object,
                                   QuerySecurityDescriptor,
                                   &Information,
                                   NULL,
                                   &Length,
                                   KernelMode);
    if (Status != STATUS_BUFFER_TOO_SMALL) return Status;

    Buffer = ExAllocatePoolWithTag(PagedPool, Length, TAG_SEC_QUERY);
    if (Buffer == NULL) return STATUS_INSUFFICIENT_RESOURCES;

    Status = ObpCallSecurityMethod(Object,
                                   QuerySecurityDescriptor,
                                   &Information,
                                   Buffer,
                                   &Length,
                                   KernelMode);
    if (!NT_SUCCESS(Status))
    {
        ExFreePoolWithTag(Buffer, TAG_SEC_QUERY);
        return Status;
    }

    *SecurityDescriptor = Buffer;
    *MemoryAllocated = TRUE;
    return STATUS_SUCCESS;
}

VOID
NTAPI
ObReleaseObjectSecurity(
    _In_opt_ PSECURITY_DESCRIPTOR SecurityDescriptor,
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
    PAGED_CODE();

    return ObpCallSecurityMethod(Object,
                                 SetSecurityDescriptor,
                                 &SecurityInformation,
                                 SecurityDescriptor,
                                 NULL,
                                 KernelMode);
}

NTSTATUS
NTAPI
NtQuerySecurityObject(
    _In_ HANDLE Handle,
    _In_ SECURITY_INFORMATION SecurityInformation,
    _Out_writes_bytes_opt_(Length) PSECURITY_DESCRIPTOR SecurityDescriptor,
    _In_ ULONG Length,
    _Out_ PULONG LengthNeeded)
{
    KPROCESSOR_MODE PreviousMode = ExGetPreviousMode();
    ULONG Captured = Length;
    NTSTATUS Status;
    PVOID Object;

    PAGED_CODE();

    if (PreviousMode != KernelMode)
    {
        _SEH2_TRY
        {
            if (Length != 0) ProbeForWrite(SecurityDescriptor, Length, sizeof(ULONG));
            ProbeForWriteUlong(LengthNeeded);
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            _SEH2_YIELD(return _SEH2_GetExceptionCode());
        }
        _SEH2_END;
    }

    Status = ObReferenceObjectByHandle(Handle,
                                       ObpAccessForQueryingSecurity(SecurityInformation),
                                       NULL,
                                       PreviousMode,
                                       &Object,
                                       NULL);
    if (!NT_SUCCESS(Status)) return Status;

    Status = ObpCallSecurityMethod(Object,
                                   QuerySecurityDescriptor,
                                   &SecurityInformation,
                                   SecurityDescriptor,
                                   &Captured,
                                   PreviousMode);
    ObDereferenceObject(Object);

    _SEH2_TRY
    {
        *LengthNeeded = Captured;
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
    PSECURITY_DESCRIPTOR Captured;
    NTSTATUS Status;
    PVOID Object;

    PAGED_CODE();

    if (SecurityInformation == 0 ||
        (SecurityInformation & ~OBP_SI_SUPPORTED) != 0)
    {
        return STATUS_INVALID_PARAMETER;
    }

    Status = SeCaptureSecurityDescriptor(SecurityDescriptor,
                                         PreviousMode,
                                         PagedPool,
                                         FALSE,
                                         &Captured);
    if (!NT_SUCCESS(Status)) return Status;
    if (Captured == NULL) return STATUS_INVALID_SECURITY_DESCR;

    Status = ObpCheckDescriptorCompleteness(SecurityInformation, Captured);
    if (NT_SUCCESS(Status))
    {
        Status = ObReferenceObjectByHandle(Handle,
                                           ObpAccessForSettingSecurity(SecurityInformation),
                                           NULL,
                                           PreviousMode,
                                           &Object,
                                           NULL);
    }

    if (!NT_SUCCESS(Status))
    {
        SeReleaseSecurityDescriptor(Captured, PreviousMode, FALSE);
        return Status;
    }

    /* Raising a label above the caller's own integrity is a privileged act */
    if ((SecurityInformation & LABEL_SECURITY_INFORMATION) &&
        !ObpLabelWithinCallerIntegrity(Captured) &&
        !SeSinglePrivilegeCheck(SeRelabelPrivilege, PreviousMode))
    {
        Status = STATUS_PRIVILEGE_NOT_HELD;
    }
    else
    {
        Status = ObSetSecurityObjectByPointer(Object,
                                              SecurityInformation,
                                              Captured);
    }

    ObDereferenceObject(Object);
    SeReleaseSecurityDescriptor(Captured, PreviousMode, FALSE);

    return Status;
}

NTSTATUS
NTAPI
ObQueryObjectAuditingByHandle(
    _In_ HANDLE Handle,
    _Out_ PBOOLEAN GenerateOnClose)
{
    PHANDLE_TABLE_ENTRY Entry;
    PHANDLE_TABLE Table;

    PAGED_CODE();

    /* Only a driver can reach this export, so a kernel handle always belongs
     * to the kernel table whatever mode the current system call came from */
    if (ObpIsKernelHandle(Handle, KernelMode))
    {
        Table = ObpKernelHandleTable;
        Handle = ObKernelHandleToHandle(Handle);
    }
    else
    {
        Table = PsGetCurrentProcess()->ObjectTable;
    }

    KeEnterCriticalRegion();

    Entry = ExMapHandleToPointer(Table, Handle);
    if (Entry == NULL)
    {
        KeLeaveCriticalRegion();
        return STATUS_INVALID_HANDLE;
    }

    *GenerateOnClose = ((Entry->ObAttributes & OBJ_AUDIT_OBJECT_CLOSE) != 0);
    ExUnlockHandleTableEntry(Table, Entry);

    KeLeaveCriticalRegion();
    return STATUS_SUCCESS;
}

/* EOF */
