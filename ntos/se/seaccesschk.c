/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Deciding access against a security descriptor
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

/* PRIVATE DATA ***************************************************************/

/* What one walk of a list is deciding, and for whom */
typedef struct _SEP_CHECK_CONTEXT
{
    PISECURITY_DESCRIPTOR Descriptor;
    PTOKEN Token;
    PSID PrincipalSelfSid;
    PGENERIC_MAPPING GenericMapping;
    POBJECT_TYPE_LIST_INTERNAL ObjectTypeList;
    ULONG ObjectTypeListLength;
    ULONG SidSet;
    BOOLEAN MaximumAllowed;
} SEP_CHECK_CONTEXT, *PSEP_CHECK_CONTEXT;

/* PRIVATE FUNCTIONS **********************************************************/

static
BOOLEAN
SepIsObjectAce(
    _In_ UCHAR AceType)
{
    return (AceType == ACCESS_ALLOWED_OBJECT_ACE_TYPE ||
            AceType == ACCESS_DENIED_OBJECT_ACE_TYPE);
}

/*
 * An entry that names an object type speaks for that type and for everything
 * under it; one that names none speaks for the whole list.
 */
static
BOOLEAN
SepAceCoversEntry(
    _In_ PSEP_CHECK_CONTEXT Context,
    _In_ PACE Ace,
    _In_ ULONG Entry,
    _Out_ PBOOLEAN Known)
{
    PGUID Guid;
    ULONG Named;

    *Known = TRUE;

    if (!SepIsObjectAce(Ace->Header.AceType)) return TRUE;

    Guid = SepGetObjectTypeGuidFromAce(Ace,
                                       Ace->Header.AceType ==
                                       ACCESS_DENIED_OBJECT_ACE_TYPE);
    if (Guid == NULL) return TRUE;

    if (Context->ObjectTypeList == NULL)
    {
        /* Nothing was named to apply it to */
        *Known = FALSE;
        return FALSE;
    }

    if (!SepObjectTypeGuidInList(Context->ObjectTypeList,
                                 Context->ObjectTypeListLength,
                                 Guid,
                                 &Named))
    {
        *Known = FALSE;
        return FALSE;
    }

    if (Entry == Named) return TRUE;
    if (Entry < Named) return FALSE;

    /* Below the one it named, until the list climbs back up to its level */
    return (Context->ObjectTypeList[Entry].Level >
            Context->ObjectTypeList[Named].Level);
}

/*
 * Walks a list once for one entry of the object type list. Entries are read in
 * order and the walk stops as soon as the answer can no longer change: a
 * denial that touches what is still wanted settles it, and so does having
 * granted everything.
 */
static
VOID
SepWalkDacl(
    _In_ PSEP_CHECK_CONTEXT Context,
    _In_ PACL Dacl,
    _In_ ULONG Entry,
    _Inout_ PACCESS_CHECK_RIGHTS Rights)
{
    PACE Ace;
    ULONG Index;

    for (Index = 0; Index < Dacl->AceCount; Index++)
    {
        ACCESS_MASK Mask;
        BOOLEAN Known;
        PSID Sid;

        if (!NT_SUCCESS(RtlGetAce(Dacl, Index, (PVOID *)&Ace))) break;

        /* An entry kept only to be handed down decides nothing here */
        if (Ace->Header.AceFlags & INHERIT_ONLY_ACE) continue;

        if (!SepAceCoversEntry(Context, Ace, Entry, &Known) || !Known)
            continue;

        Sid = SepGetSidFromAce(Ace);
        Mask = ((PKNOWN_ACE)Ace)->Mask;
        RtlMapGenericMask(&Mask, Context->GenericMapping);

        switch (Ace->Header.AceType)
        {
            case ACCESS_DENIED_ACE_TYPE:
            case ACCESS_DENIED_OBJECT_ACE_TYPE:
                if (!SepSidInTokenEx(Context->Token,
                                     Context->PrincipalSelfSid,
                                     Sid,
                                     TRUE,
                                     Context->SidSet))
                {
                    break;
                }

                if (Context->MaximumAllowed)
                {
                    /*
                     * Asking for the most that is allowed reads the whole
                     * list, so a refusal only takes away what an entry before
                     * it had not already allowed.
                     */
                    Rights->DeniedAccessRights |=
                        (Mask & ~Rights->GrantedAccessRights);
                    break;
                }

                /* What is refused can no longer be asked for */
                Rights->DeniedAccessRights |= Mask;

                if (Mask & Rights->RemainingAccessRights)
                {
                    /* It touches what is still wanted, so that settles it */
                    Rights->RemainingAccessRights |= MAXIMUM_ALLOWED;
                    return;
                }
                break;

            case ACCESS_ALLOWED_ACE_TYPE:
            case ACCESS_ALLOWED_OBJECT_ACE_TYPE:
                if (!SepSidInTokenEx(Context->Token,
                                     Context->PrincipalSelfSid,
                                     Sid,
                                     FALSE,
                                     Context->SidSet))
                {
                    break;
                }

                if (Context->MaximumAllowed)
                {
                    /* Everything not already refused is added to the answer */
                    Rights->GrantedAccessRights |=
                        (Mask & ~Rights->DeniedAccessRights);
                    break;
                }

                /* Only what was asked for is handed over, never the whole
                 * of what an entry would allow */
                Rights->GrantedAccessRights |=
                    (Mask & Rights->RemainingAccessRights);
                Rights->RemainingAccessRights &= ~Mask;

                if (Rights->RemainingAccessRights == 0) return;
                break;

            default:
                /* TODO: a mandatory label in the list is not enforced */
                break;
        }
    }
}

/*
 * Decides one entry of the object type list. A descriptor with no list at all
 * allows everything; one with an empty list allows nothing.
 */
static
NTSTATUS
SepCheckEntry(
    _In_ PSEP_CHECK_CONTEXT Context,
    _In_ ULONG Entry,
    _In_ ACCESS_MASK Desired,
    _In_ ACCESS_MASK AlreadyGranted,
    _Out_ PACCESS_MASK GrantedAccess)
{
    ACCESS_CHECK_RIGHTS Rights;
    ACCESS_MASK Wanted = Desired;
    BOOLEAN DaclPresent = FALSE;
    BOOLEAN Defaulted;
    PACL Dacl = NULL;
    PSID Owner = NULL;

    *GrantedAccess = AlreadyGranted;

    RtlZeroMemory(&Rights, sizeof(Rights));

    if (!NT_SUCCESS(RtlGetDaclSecurityDescriptor(Context->Descriptor,
                                                 &DaclPresent,
                                                 &Dacl,
                                                 &Defaulted)))
    {
        return STATUS_INVALID_SECURITY_DESCR;
    }

    if (!DaclPresent || Dacl == NULL)
    {
        /* Nothing says who may do what, so everything asked for is allowed */
        if (Context->MaximumAllowed)
            *GrantedAccess = AlreadyGranted | Context->GenericMapping->GenericAll;
        else
            *GrantedAccess = AlreadyGranted | Wanted;

        return STATUS_SUCCESS;
    }

    /*
     * Whoever owns something may always read and change what it says about
     * who may use it, whatever its list says.
     */
    if (NT_SUCCESS(RtlGetOwnerSecurityDescriptor(Context->Descriptor,
                                                 &Owner,
                                                 &Defaulted)) &&
        Owner != NULL &&
        SepSidInTokenEx(Context->Token, Context->PrincipalSelfSid, Owner,
                        FALSE, Context->SidSet))
    {
        if (Context->MaximumAllowed)
        {
            Rights.GrantedAccessRights |= READ_CONTROL | WRITE_DAC;
        }
        else
        {
            Rights.GrantedAccessRights |= (Wanted & (READ_CONTROL | WRITE_DAC));
            Wanted &= ~(READ_CONTROL | WRITE_DAC);
        }
    }

    Rights.RemainingAccessRights = Wanted & ~MAXIMUM_ALLOWED;

    SepWalkDacl(Context, Dacl, Entry, &Rights);

    if (Context->MaximumAllowed)
    {
        *GrantedAccess = AlreadyGranted | Rights.GrantedAccessRights;

        return (*GrantedAccess != 0) ? STATUS_SUCCESS : STATUS_ACCESS_DENIED;
    }

    if (Rights.RemainingAccessRights != 0)
    {
        /* Something asked for was never allowed, so the whole ask fails,
         * while what was granted on the way is still reported */
        *GrantedAccess = AlreadyGranted | Rights.GrantedAccessRights;

        return STATUS_ACCESS_DENIED;
    }

    *GrantedAccess = AlreadyGranted | Rights.GrantedAccessRights;

    return STATUS_SUCCESS;
}

/*
 * Decides every entry, for the groups a token carries and then again for the
 * identifiers it was restricted to, because a restricted token may only do
 * what both answers allow.
 */
static
NTSTATUS
SepCheckList(
    _In_ PSEP_CHECK_CONTEXT Context,
    _In_ ACCESS_MASK Desired,
    _In_ ACCESS_MASK AlreadyGranted,
    _In_ ULONG Entries,
    _Out_writes_(Entries) PACCESS_MASK GrantedAccess,
    _Out_writes_(Entries) PNTSTATUS AccessStatus)
{
    BOOLEAN Restricted = (Context->Token != NULL &&
                          Context->Token->RestrictedSidCount != 0);
    ULONG Entry;

    for (Entry = 0; Entry < Entries; Entry++)
    {
        ACCESS_MASK Granted = 0;
        NTSTATUS Status;

        Context->SidSet = SEP_SID_SET_GROUPS;
        Status = SepCheckEntry(Context, Entry, Desired, AlreadyGranted,
                               &Granted);

        if (Restricted && NT_SUCCESS(Status))
        {
            ACCESS_MASK Second = 0;
            NTSTATUS Status2;

            Context->SidSet = SEP_SID_SET_RESTRICTED;
            Status2 = SepCheckEntry(Context, Entry, Desired, AlreadyGranted,
                                    &Second);

            if (!NT_SUCCESS(Status2)) Status = Status2;

            Granted &= Second;
        }

        GrantedAccess[Entry] = Granted;
        AccessStatus[Entry] = Status;
    }

    return STATUS_SUCCESS;
}

/* Prepares what both the kernel and the system call paths decide against */
static
NTSTATUS
SepPrepareCheck(
    _In_ PSEP_CHECK_CONTEXT Context,
    _Inout_ PACCESS_MASK Desired,
    _Inout_ PACCESS_MASK AlreadyGranted,
    _In_ PSECURITY_SUBJECT_CONTEXT SubjectContext,
    _Outptr_opt_result_maybenull_ PPRIVILEGE_SET *Privileges,
    _In_ KPROCESSOR_MODE AccessMode)
{
    BOOLEAN Defaulted;
    PSID Sid;

    if (Privileges != NULL) *Privileges = NULL;

    /* A descriptor that names nobody as its owner is not one */
    Sid = NULL;
    if (!NT_SUCCESS(RtlGetOwnerSecurityDescriptor(Context->Descriptor,
                                                  &Sid,
                                                  &Defaulted)) ||
        Sid == NULL)
    {
        return STATUS_INVALID_SECURITY_DESCR;
    }

    Sid = NULL;
    if (!NT_SUCCESS(RtlGetGroupSecurityDescriptor(Context->Descriptor,
                                                  &Sid,
                                                  &Defaulted)))
    {
        return STATUS_INVALID_SECURITY_DESCR;
    }

    RtlMapGenericMask(Desired, Context->GenericMapping);

    /* Two kinds of access are settled by privilege, not by the descriptor */
    return SePrivilegePolicyCheck(Desired,
                                  AlreadyGranted,
                                  SubjectContext,
                                  Context->Token,
                                  Privileges,
                                  AccessMode);
}

/* PUBLIC FUNCTIONS ***********************************************************/

BOOLEAN
NTAPI
SeAccessCheck(
    _In_ PSECURITY_DESCRIPTOR SecurityDescriptor,
    _In_ PSECURITY_SUBJECT_CONTEXT SubjectSecurityContext,
    _In_ BOOLEAN SubjectContextLocked,
    _In_ ACCESS_MASK DesiredAccess,
    _In_ ACCESS_MASK PreviouslyGrantedAccess,
    _Outptr_opt_ PPRIVILEGE_SET *Privileges,
    _In_ PGENERIC_MAPPING GenericMapping,
    _In_ KPROCESSOR_MODE AccessMode,
    _Out_ PACCESS_MASK GrantedAccess,
    _Out_ PNTSTATUS AccessStatus)
{
    SEP_CHECK_CONTEXT Context;
    ACCESS_MASK Granted = 0;
    ACCESS_MASK Already = PreviouslyGrantedAccess;
    ACCESS_MASK Wanted = DesiredAccess;
    NTSTATUS Entry = STATUS_SUCCESS;
    NTSTATUS Status;

    PAGED_CODE();

    if (Privileges != NULL) *Privileges = NULL;

    /* The kernel is trusted with whatever it asks of an object */
    if (AccessMode == KernelMode)
    {
        if (DesiredAccess & MAXIMUM_ALLOWED)
        {
            *GrantedAccess = GenericMapping->GenericAll |
                             (DesiredAccess & ~MAXIMUM_ALLOWED) |
                             PreviouslyGrantedAccess;
        }
        else
        {
            *GrantedAccess = DesiredAccess | PreviouslyGrantedAccess;
        }

        *AccessStatus = STATUS_SUCCESS;
        return TRUE;
    }

    if (DesiredAccess == 0 && PreviouslyGrantedAccess == 0)
    {
        /* Asking for nothing is not something that can be allowed */
        *GrantedAccess = 0;
        *AccessStatus = STATUS_ACCESS_DENIED;
        return FALSE;
    }

    if (SecurityDescriptor == NULL)
    {
        *GrantedAccess = 0;
        *AccessStatus = STATUS_ACCESS_DENIED;
        return FALSE;
    }

    if (!SubjectContextLocked) SeLockSubjectContext(SubjectSecurityContext);

    RtlZeroMemory(&Context, sizeof(Context));
    Context.Descriptor = SecurityDescriptor;
    Context.Token = (PTOKEN)SeQuerySubjectContextToken(SubjectSecurityContext);
    Context.GenericMapping = GenericMapping;
    Context.MaximumAllowed = (DesiredAccess & MAXIMUM_ALLOWED) != 0;

    Status = SepPrepareCheck(&Context, &Wanted, &Already,
                             SubjectSecurityContext, Privileges, AccessMode);
    if (NT_SUCCESS(Status))
    {
        Status = SepCheckList(&Context, Wanted, Already, 1, &Granted, &Entry);
        if (NT_SUCCESS(Status)) Status = Entry;
    }

    if (!SubjectContextLocked) SeUnlockSubjectContext(SubjectSecurityContext);

    if (!NT_SUCCESS(Status))
    {
        /* Nothing is handed back from an ask that failed */
        *GrantedAccess = 0;
        *AccessStatus = Status;

        if (Privileges != NULL && *Privileges != NULL)
        {
            SeFreePrivileges(*Privileges);
            *Privileges = NULL;
        }

        return FALSE;
    }

    *GrantedAccess = Granted;
    *AccessStatus = STATUS_SUCCESS;

    return TRUE;
}

/*
 * Answers whether a walk can be skipped: it can only when nothing in the list
 * refuses anything and everyone is already allowed what is wanted.
 */
BOOLEAN
NTAPI
SeFastTraverseCheck(
    _In_ PSECURITY_DESCRIPTOR SecurityDescriptor,
    _In_ PACCESS_STATE AccessState,
    _In_ ACCESS_MASK DesiredAccess,
    _In_ KPROCESSOR_MODE AccessMode)
{
    BOOLEAN Present = FALSE;
    BOOLEAN Defaulted;
    PACL Dacl = NULL;
    ULONG Index;
    PACE Ace;

    PAGED_CODE();

    UNREFERENCED_PARAMETER(AccessState);

    if (AccessMode == KernelMode) return TRUE;
    if (SecurityDescriptor == NULL) return FALSE;

    if (!NT_SUCCESS(RtlGetDaclSecurityDescriptor(SecurityDescriptor,
                                                 &Present,
                                                 &Dacl,
                                                 &Defaulted)))
    {
        return FALSE;
    }

    /* Nothing says who may do what, so anyone may */
    if (!Present || Dacl == NULL) return TRUE;

    for (Index = 0; Index < Dacl->AceCount; Index++)
    {
        if (!NT_SUCCESS(RtlGetAce(Dacl, Index, (PVOID *)&Ace))) return FALSE;

        if (Ace->Header.AceFlags & INHERIT_ONLY_ACE) continue;

        /* Anything that refuses has to be read in order, so no shortcut */
        if (Ace->Header.AceType != ACCESS_ALLOWED_ACE_TYPE) return FALSE;

        if (!RtlEqualSid(SepGetSidFromAce(Ace), SeWorldSid)) continue;

        if ((((PKNOWN_ACE)Ace)->Mask & DesiredAccess) == DesiredAccess)
            return TRUE;
    }

    return FALSE;
}

/* SYSTEM CALLS ***************************************************************/

/*
 * The three system calls differ only in what they are asked about, so they
 * all come through here. The order the arguments are refused in is part of
 * what callers see, so it does not change: what cannot be read at all, then
 * rights the caller should have mapped itself, then the token, then the
 * descriptor, then the list.
 */
NTSTATUS
SepAccessCheckByType(
    _In_opt_ PSECURITY_DESCRIPTOR SecurityDescriptor,
    _In_opt_ PSID PrincipalSelfSid,
    _In_opt_ HANDLE ClientToken,
    _In_opt_ PTOKEN EffectiveToken,
    _In_ ACCESS_MASK DesiredAccess,
    _In_opt_ POBJECT_TYPE_LIST ObjectTypeList,
    _In_ ULONG ObjectTypeListLength,
    _In_ PGENERIC_MAPPING GenericMapping,
    _Out_ PPRIVILEGE_SET PrivilegeSet,
    _Inout_ PULONG PrivilegeSetLength,
    _Out_ PACCESS_MASK GrantedAccess,
    _Out_ PNTSTATUS AccessStatus,
    _In_ BOOLEAN ResultList)
{
    KPROCESSOR_MODE PreviousMode = ExGetPreviousMode();
    POBJECT_TYPE_LIST_INTERNAL Captured = NULL;
    PSECURITY_DESCRIPTOR Descriptor = NULL;
    SECURITY_SUBJECT_CONTEXT SubjectContext;
    PPRIVILEGE_SET Used = NULL;
    PACCESS_MASK Granted = NULL;
    PNTSTATUS Results = NULL;
    GENERIC_MAPPING Mapping;
    NTSTATUS Status = STATUS_SUCCESS;
    ACCESS_MASK Already = 0;
    ACCESS_MASK Wanted = DesiredAccess;
    PSID SelfSid = NULL;
    SEP_CHECK_CONTEXT Context;
    ULONG Entries = 1;
    ULONG SetLength = 0;
    ULONG Needed;

    PAGED_CODE();

    RtlZeroMemory(&Context, sizeof(Context));

    if (ResultList && ObjectTypeListLength == 0)
        return STATUS_INVALID_PARAMETER;

    if (ResultList) Entries = ObjectTypeListLength;

    if (PreviousMode != KernelMode)
    {
        _SEH2_TRY
        {
            ProbeForRead(GenericMapping, sizeof(GENERIC_MAPPING), sizeof(ULONG));
            Mapping = *GenericMapping;

            ProbeForWriteUlong(PrivilegeSetLength);
            SetLength = *PrivilegeSetLength;

            if (SetLength != 0)
                ProbeForWrite(PrivilegeSet, SetLength, sizeof(ULONG));

            ProbeForWrite(GrantedAccess,
                          (SIZE_T)Entries * sizeof(ACCESS_MASK),
                          sizeof(ULONG));
            ProbeForWrite(AccessStatus,
                          (SIZE_T)Entries * sizeof(NTSTATUS),
                          sizeof(ULONG));
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            Status = _SEH2_GetExceptionCode();
        }
        _SEH2_END;

        if (!NT_SUCCESS(Status)) return Status;
    }
    else
    {
        Mapping = *GenericMapping;
        SetLength = *PrivilegeSetLength;
    }

    /* A caller has to map its own rights before asking about them */
    if (DesiredAccess & (GENERIC_READ | GENERIC_WRITE |
                         GENERIC_EXECUTE | GENERIC_ALL))
    {
        return STATUS_GENERIC_NOT_MAPPED;
    }

    SeCaptureSubjectContext(&SubjectContext);

    if (EffectiveToken != NULL)
    {
        /* A caller asking in its own name is answered for by what it acts
         * with, whichever kind of token that is */
        Context.Token = EffectiveToken;
        ObReferenceObject(Context.Token);
    }
    else
    {
        Status = ObReferenceObjectByHandle(ClientToken,
                                           TOKEN_QUERY,
                                           SeTokenObjectType,
                                           PreviousMode,
                                           (PVOID *)&Context.Token,
                                           NULL);
        if (NT_SUCCESS(Status))
        {
            /* Only a token taken from somebody else answers for them, and
             * only one taken at a level that may be acted on */
            if (Context.Token->TokenType != TokenImpersonation)
                Status = STATUS_NO_IMPERSONATION_TOKEN;
            else if (Context.Token->ImpersonationLevel < SecurityIdentification)
                Status = STATUS_BAD_IMPERSONATION_LEVEL;
        }
    }

    if (NT_SUCCESS(Status))
    {
        Status = SeCaptureObjectTypeList(ObjectTypeList,
                                         ObjectTypeListLength,
                                         PreviousMode,
                                         &Captured);
    }

    if (NT_SUCCESS(Status))
    {
        if (SecurityDescriptor == NULL)
        {
            Status = STATUS_INVALID_SECURITY_DESCR;
        }
        else
        {
            Status = SeCaptureSecurityDescriptor(SecurityDescriptor,
                                                 PreviousMode,
                                                 PagedPool,
                                                 FALSE,
                                                 &Descriptor);
            if (Status == STATUS_UNKNOWN_REVISION)
                Status = STATUS_REVISION_MISMATCH;
        }
    }

    if (NT_SUCCESS(Status) &&
        ((PISECURITY_DESCRIPTOR)Descriptor)->Revision !=
        SECURITY_DESCRIPTOR_REVISION)
    {
        Status = STATUS_REVISION_MISMATCH;
    }

    if (NT_SUCCESS(Status))
    {
        BOOLEAN Defaulted;
        PSID Named = NULL;

        if (!NT_SUCCESS(RtlGetOwnerSecurityDescriptor(Descriptor,
                                                      &Named,
                                                      &Defaulted)) ||
            Named == NULL)
        {
            Status = STATUS_INVALID_SECURITY_DESCR;
        }
        else
        {
            Named = NULL;
            if (!NT_SUCCESS(RtlGetGroupSecurityDescriptor(Descriptor,
                                                          &Named,
                                                          &Defaulted)) ||
                Named == NULL)
            {
                Status = STATUS_INVALID_SECURITY_DESCR;
            }
        }
    }

    /* What the caller says "self" means is read only once the rest is sound */
    if (NT_SUCCESS(Status) && PrincipalSelfSid != NULL)
    {
        Status = SepCaptureSid(PrincipalSelfSid, PreviousMode, PagedPool,
                               TRUE, &SelfSid);
    }

    if (NT_SUCCESS(Status))
    {
        Granted = ExAllocatePoolWithTag(PagedPool,
                                        (SIZE_T)Entries *
                                        (sizeof(ACCESS_MASK) + sizeof(NTSTATUS)),
                                        TAG_SE);
        if (Granted == NULL) Status = STATUS_INSUFFICIENT_RESOURCES;
        else Results = (PNTSTATUS)&Granted[Entries];
    }

    if (NT_SUCCESS(Status))
    {
        Context.Descriptor = Descriptor;
        Context.PrincipalSelfSid = SelfSid;
        Context.GenericMapping = &Mapping;
        Context.ObjectTypeList = Captured;
        Context.ObjectTypeListLength = ObjectTypeListLength;
        Context.MaximumAllowed = (DesiredAccess & MAXIMUM_ALLOWED) != 0;
        Context.SidSet = SEP_SID_SET_GROUPS;

        SeLockSubjectContext(&SubjectContext);

        Status = SepPrepareCheck(&Context, &Wanted, &Already,
                                 &SubjectContext, &Used, PreviousMode);
        if (NT_SUCCESS(Status))
        {
            Status = SepCheckList(&Context, Wanted, Already, Entries,
                                  Granted, Results);

            /*
             * Asking about one thing and being refused hands back nothing;
             * asking about a list hands back what each entry did get.
             */
            if (NT_SUCCESS(Status) && !ResultList && !NT_SUCCESS(Results[0]))
                Granted[0] = 0;
        }

        SeUnlockSubjectContext(&SubjectContext);
    }

    /* What privileges carried the answer is reported in the caller's own set */
    if (NT_SUCCESS(Status))
    {
        ULONG Count = (Used != NULL) ? Used->PrivilegeCount : 0;

        Needed = (ULONG)(FIELD_OFFSET(PRIVILEGE_SET, Privilege) +
                         (SIZE_T)Count * sizeof(LUID_AND_ATTRIBUTES));

        _SEH2_TRY
        {
            *PrivilegeSetLength = Needed;

            if (SetLength < Needed)
            {
                Status = STATUS_BUFFER_TOO_SMALL;
            }
            else
            {
                PrivilegeSet->PrivilegeCount = Count;
                PrivilegeSet->Control = (Used != NULL) ? Used->Control : 0;

                if (Count != 0)
                {
                    RtlCopyMemory(PrivilegeSet->Privilege,
                                  Used->Privilege,
                                  Count * sizeof(LUID_AND_ATTRIBUTES));
                }

                RtlCopyMemory(GrantedAccess,
                              Granted,
                              (SIZE_T)Entries * sizeof(ACCESS_MASK));
                RtlCopyMemory(AccessStatus,
                              Results,
                              (SIZE_T)Entries * sizeof(NTSTATUS));
            }
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            Status = _SEH2_GetExceptionCode();
        }
        _SEH2_END;
    }

    if (Used != NULL) SeFreePrivileges(Used);
    if (Granted != NULL) ExFreePoolWithTag(Granted, TAG_SE);
    if (Captured != NULL) SeReleaseObjectTypeList(Captured, PreviousMode);
    if (Descriptor != NULL)
        SeReleaseSecurityDescriptor(Descriptor, PreviousMode, FALSE);
    if (Context.Token != NULL) ObDereferenceObject(Context.Token);
    if (SelfSid != NULL) SepReleaseSid(SelfSid, PreviousMode, TRUE);

    SeReleaseSubjectContext(&SubjectContext);

    return Status;
}

NTSTATUS
NTAPI
NtAccessCheck(
    _In_ PSECURITY_DESCRIPTOR SecurityDescriptor,
    _In_ HANDLE ClientToken,
    _In_ ACCESS_MASK DesiredAccess,
    _In_ PGENERIC_MAPPING GenericMapping,
    _Out_ PPRIVILEGE_SET PrivilegeSet,
    _Inout_ PULONG PrivilegeSetLength,
    _Out_ PACCESS_MASK GrantedAccess,
    _Out_ PNTSTATUS AccessStatus)
{
    PAGED_CODE();

    return SepAccessCheckByType(SecurityDescriptor,
                                NULL,
                                ClientToken,
                                NULL,
                                DesiredAccess,
                                NULL,
                                0,
                                GenericMapping,
                                PrivilegeSet,
                                PrivilegeSetLength,
                                GrantedAccess,
                                AccessStatus,
                                FALSE);
}

NTSTATUS
NTAPI
NtAccessCheckByType(
    _In_ PSECURITY_DESCRIPTOR SecurityDescriptor,
    _In_opt_ PSID PrincipalSelfSid,
    _In_ HANDLE ClientToken,
    _In_ ACCESS_MASK DesiredAccess,
    _In_reads_opt_(ObjectTypeListLength) POBJECT_TYPE_LIST ObjectTypeList,
    _In_ ULONG ObjectTypeListLength,
    _In_ PGENERIC_MAPPING GenericMapping,
    _Out_ PPRIVILEGE_SET PrivilegeSet,
    _Inout_ PULONG PrivilegeSetLength,
    _Out_ PACCESS_MASK GrantedAccess,
    _Out_ PNTSTATUS AccessStatus)
{
    PAGED_CODE();

    return SepAccessCheckByType(SecurityDescriptor,
                                PrincipalSelfSid,
                                ClientToken,
                                NULL,
                                DesiredAccess,
                                ObjectTypeList,
                                ObjectTypeListLength,
                                GenericMapping,
                                PrivilegeSet,
                                PrivilegeSetLength,
                                GrantedAccess,
                                AccessStatus,
                                FALSE);
}

NTSTATUS
NTAPI
NtAccessCheckByTypeResultList(
    _In_ PSECURITY_DESCRIPTOR SecurityDescriptor,
    _In_opt_ PSID PrincipalSelfSid,
    _In_ HANDLE ClientToken,
    _In_ ACCESS_MASK DesiredAccess,
    _In_reads_(ObjectTypeListLength) POBJECT_TYPE_LIST ObjectTypeList,
    _In_ ULONG ObjectTypeListLength,
    _In_ PGENERIC_MAPPING GenericMapping,
    _Out_ PPRIVILEGE_SET PrivilegeSet,
    _Inout_ PULONG PrivilegeSetLength,
    _Out_writes_(ObjectTypeListLength) PACCESS_MASK GrantedAccess,
    _Out_writes_(ObjectTypeListLength) PNTSTATUS AccessStatus)
{
    PAGED_CODE();

    return SepAccessCheckByType(SecurityDescriptor,
                                PrincipalSelfSid,
                                ClientToken,
                                NULL,
                                DesiredAccess,
                                ObjectTypeList,
                                ObjectTypeListLength,
                                GenericMapping,
                                PrivilegeSet,
                                PrivilegeSetLength,
                                GrantedAccess,
                                AccessStatus,
                                TRUE);
}

/* EOF */
