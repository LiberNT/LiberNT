/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Well-known security identifiers and the capture of others
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

/* The start-up undoes its own work on failure, before anything declares it */
VOID
NTAPI
FreeInitializedSids(VOID);

/* GLOBALS ********************************************************************/

SID_IDENTIFIER_AUTHORITY SeNullSidAuthority = {SECURITY_NULL_SID_AUTHORITY};
SID_IDENTIFIER_AUTHORITY SeWorldSidAuthority = {SECURITY_WORLD_SID_AUTHORITY};
SID_IDENTIFIER_AUTHORITY SeLocalSidAuthority = {SECURITY_LOCAL_SID_AUTHORITY};
SID_IDENTIFIER_AUTHORITY SeCreatorSidAuthority = {SECURITY_CREATOR_SID_AUTHORITY};
SID_IDENTIFIER_AUTHORITY SeNtSidAuthority = {SECURITY_NT_AUTHORITY};
SID_IDENTIFIER_AUTHORITY SeMandatoryLabelAuthority = {SECURITY_MANDATORY_LABEL_AUTHORITY};
SID_IDENTIFIER_AUTHORITY SeAppPackageAuthority = {SECURITY_APP_PACKAGE_AUTHORITY};
SID_IDENTIFIER_AUTHORITY SeProcessTrustAuthority = {SECURITY_PROCESS_TRUST_AUTHORITY};

PSID SeNullSid = NULL;
PSID SeWorldSid = NULL;
PSID SeLocalSid = NULL;
PSID SeCreatorOwnerSid = NULL;
PSID SeCreatorGroupSid = NULL;
PSID SeCreatorOwnerServerSid = NULL;
PSID SeCreatorGroupServerSid = NULL;
PSID SeOwnerRightsSid = NULL;
PSID SeNtAuthoritySid = NULL;
PSID SeDialupSid = NULL;
PSID SeNetworkSid = NULL;
PSID SeBatchSid = NULL;
PSID SeInteractiveSid = NULL;
PSID SeServiceSid = NULL;
PSID SeAnonymousLogonSid = NULL;
PSID SePrincipalSelfSid = NULL;
PSID SeAuthenticatedUserSid = NULL;
PSID SeRestrictedCodeSid = NULL;
PSID SeLocalSystemSid = NULL;
PSID SeLocalServiceSid = NULL;
PSID SeNetworkServiceSid = NULL;
PSID SeIUserSid = NULL;
PSID SeAliasAdminsSid = NULL;
PSID SeAliasUsersSid = NULL;
PSID SeAliasGuestsSid = NULL;
PSID SeAliasPowerUsersSid = NULL;
PSID SeAliasAccountOpsSid = NULL;
PSID SeAliasSystemOpsSid = NULL;
PSID SeAliasPrintOpsSid = NULL;
PSID SeAliasBackupOpsSid = NULL;
PSID SeUntrustedMandatorySid = NULL;
PSID SeLowMandatorySid = NULL;
PSID SeMediumMandatorySid = NULL;
PSID SeHighMandatorySid = NULL;
PSID SeSystemMandatorySid = NULL;
PSID SeAllAppPackagesSid = NULL;
PSID SeAllRestrictedAppPackagesSid = NULL;
PSID SeUserModeDriversSid = NULL;
PSID SeProcTrustWinTcbSid = NULL;
PSID SeTrustedInstallerSid = NULL;

/* The two names the exported table uses for identifiers it shares */
PSID SeAuthenticatedUsersSid = NULL;
PSID SeRestrictedSid = NULL;


/* PRIVATE DATA ***************************************************************/

#define SEP_MAX_WELL_KNOWN_RIDS 8

/* One well-known identifier: where it is kept, and what it is made of */
typedef struct _SEP_WELL_KNOWN_SID
{
    PSID *Target;
    PSID_IDENTIFIER_AUTHORITY Authority;
    UCHAR Count;
    ULONG Rids[SEP_MAX_WELL_KNOWN_RIDS];
} SEP_WELL_KNOWN_SID;

/*
 * Every identifier is written out as the constants that define it rather than
 * as a number, so what each one is stays readable.
 */
static const SEP_WELL_KNOWN_SID SepWellKnownSids[] =
{
    {&SeNullSid,            &SeNullSidAuthority,    1, {SECURITY_NULL_RID}},
    {&SeWorldSid,           &SeWorldSidAuthority,   1, {SECURITY_WORLD_RID}},
    {&SeLocalSid,           &SeLocalSidAuthority,   1, {SECURITY_LOCAL_RID}},
    {&SeCreatorOwnerSid,    &SeCreatorSidAuthority, 1, {SECURITY_CREATOR_OWNER_RID}},
    {&SeCreatorGroupSid,    &SeCreatorSidAuthority, 1, {SECURITY_CREATOR_GROUP_RID}},
    {&SeCreatorOwnerServerSid, &SeCreatorSidAuthority, 1, {SECURITY_CREATOR_OWNER_SERVER_RID}},
    {&SeCreatorGroupServerSid, &SeCreatorSidAuthority, 1, {SECURITY_CREATOR_GROUP_SERVER_RID}},
    {&SeOwnerRightsSid,     &SeCreatorSidAuthority, 1, {SECURITY_CREATOR_OWNER_RIGHTS_RID}},

    /* The authority itself names no account, so it carries no identifier */
    {&SeNtAuthoritySid,     &SeNtSidAuthority,      0, {0}},

    {&SeDialupSid,          &SeNtSidAuthority,      1, {SECURITY_DIALUP_RID}},
    {&SeNetworkSid,         &SeNtSidAuthority,      1, {SECURITY_NETWORK_RID}},
    {&SeBatchSid,           &SeNtSidAuthority,      1, {SECURITY_BATCH_RID}},
    {&SeInteractiveSid,     &SeNtSidAuthority,      1, {SECURITY_INTERACTIVE_RID}},
    {&SeServiceSid,         &SeNtSidAuthority,      1, {SECURITY_SERVICE_RID}},
    {&SeAnonymousLogonSid,  &SeNtSidAuthority,      1, {SECURITY_ANONYMOUS_LOGON_RID}},
    {&SePrincipalSelfSid,   &SeNtSidAuthority,      1, {SECURITY_PRINCIPAL_SELF_RID}},
    {&SeAuthenticatedUserSid, &SeNtSidAuthority,    1, {SECURITY_AUTHENTICATED_USER_RID}},
    {&SeRestrictedCodeSid,  &SeNtSidAuthority,      1, {SECURITY_RESTRICTED_CODE_RID}},
    {&SeIUserSid,           &SeNtSidAuthority,      1, {SECURITY_IUSER_RID}},
    {&SeLocalSystemSid,     &SeNtSidAuthority,      1, {SECURITY_LOCAL_SYSTEM_RID}},
    {&SeLocalServiceSid,    &SeNtSidAuthority,      1, {SECURITY_LOCAL_SERVICE_RID}},
    {&SeNetworkServiceSid,  &SeNtSidAuthority,      1, {SECURITY_NETWORK_SERVICE_RID}},

    {&SeUserModeDriversSid, &SeNtSidAuthority,      6, {SECURITY_UMFD_BASE_RID, 0, 0, 0, 0, 0}},

    {&SeAliasAdminsSid,     &SeNtSidAuthority,      2, {SECURITY_BUILTIN_DOMAIN_RID, DOMAIN_ALIAS_RID_ADMINS}},
    {&SeAliasUsersSid,      &SeNtSidAuthority,      2, {SECURITY_BUILTIN_DOMAIN_RID, DOMAIN_ALIAS_RID_USERS}},
    {&SeAliasGuestsSid,     &SeNtSidAuthority,      2, {SECURITY_BUILTIN_DOMAIN_RID, DOMAIN_ALIAS_RID_GUESTS}},
    {&SeAliasPowerUsersSid, &SeNtSidAuthority,      2, {SECURITY_BUILTIN_DOMAIN_RID, DOMAIN_ALIAS_RID_POWER_USERS}},
    {&SeAliasAccountOpsSid, &SeNtSidAuthority,      2, {SECURITY_BUILTIN_DOMAIN_RID, DOMAIN_ALIAS_RID_ACCOUNT_OPS}},
    {&SeAliasSystemOpsSid,  &SeNtSidAuthority,      2, {SECURITY_BUILTIN_DOMAIN_RID, DOMAIN_ALIAS_RID_SYSTEM_OPS}},
    {&SeAliasPrintOpsSid,   &SeNtSidAuthority,      2, {SECURITY_BUILTIN_DOMAIN_RID, DOMAIN_ALIAS_RID_PRINT_OPS}},
    {&SeAliasBackupOpsSid,  &SeNtSidAuthority,      2, {SECURITY_BUILTIN_DOMAIN_RID, DOMAIN_ALIAS_RID_BACKUP_OPS}},

    {&SeTrustedInstallerSid, &SeNtSidAuthority,     6, {SECURITY_SERVICE_ID_BASE_RID,
                                                        SECURITY_TRUSTED_INSTALLER_RID1,
                                                        SECURITY_TRUSTED_INSTALLER_RID2,
                                                        SECURITY_TRUSTED_INSTALLER_RID3,
                                                        SECURITY_TRUSTED_INSTALLER_RID4,
                                                        SECURITY_TRUSTED_INSTALLER_RID5}},

    {&SeUntrustedMandatorySid, &SeMandatoryLabelAuthority, 1, {SECURITY_MANDATORY_UNTRUSTED_RID}},
    {&SeLowMandatorySid,    &SeMandatoryLabelAuthority, 1, {SECURITY_MANDATORY_LOW_RID}},
    {&SeMediumMandatorySid, &SeMandatoryLabelAuthority, 1, {SECURITY_MANDATORY_MEDIUM_RID}},
    {&SeHighMandatorySid,   &SeMandatoryLabelAuthority, 1, {SECURITY_MANDATORY_HIGH_RID}},
    {&SeSystemMandatorySid, &SeMandatoryLabelAuthority, 1, {SECURITY_MANDATORY_SYSTEM_RID}},

    {&SeAllAppPackagesSid,  &SeAppPackageAuthority, 2, {SECURITY_APP_PACKAGE_BASE_RID,
                                                        SECURITY_BUILTIN_PACKAGE_ANY_PACKAGE}},
    {&SeAllRestrictedAppPackagesSid, &SeAppPackageAuthority, 2,
                                                       {SECURITY_APP_PACKAGE_BASE_RID,
                                                        SECURITY_BUILTIN_PACKAGE_ANY_RESTRICTED_PACKAGE}},

    {&SeProcTrustWinTcbSid, &SeProcessTrustAuthority, 2, {SECURITY_PROCESS_PROTECTION_TYPE_FULL_RID,
                                                          SECURITY_PROCESS_PROTECTION_LEVEL_WINTCB_RID}},
};

/* PRIVATE FUNCTIONS **********************************************************/

static
PSID
SepBuildWellKnownSid(
    _In_ const SEP_WELL_KNOWN_SID *Spec)
{
    PSID Sid;
    UCHAR Index;

    Sid = ExAllocatePoolWithTag(NonPagedPool,
                                RtlLengthRequiredSid(Spec->Count),
                                TAG_SID);
    if (Sid == NULL) return NULL;

    RtlInitializeSid(Sid, Spec->Authority, Spec->Count);

    for (Index = 0; Index < Spec->Count; Index++)
        *RtlSubAuthoritySid(Sid, Index) = Spec->Rids[Index];

    return Sid;
}

CODE_SEG("INIT")
BOOLEAN
NTAPI
SepInitSecurityIDs(VOID)
{
    ULONG Index;

    for (Index = 0; Index < RTL_NUMBER_OF(SepWellKnownSids); Index++)
    {
        PSID Sid = SepBuildWellKnownSid(&SepWellKnownSids[Index]);

        if (Sid == NULL)
        {
            FreeInitializedSids();
            return FALSE;
        }

        *SepWellKnownSids[Index].Target = Sid;
    }

    /* The exported table names two of these differently */
    SeAuthenticatedUsersSid = SeAuthenticatedUserSid;
    SeRestrictedSid = SeRestrictedCodeSid;

    return TRUE;
}

VOID
NTAPI
FreeInitializedSids(VOID)
{
    ULONG Index;

    for (Index = 0; Index < RTL_NUMBER_OF(SepWellKnownSids); Index++)
    {
        PSID *Target = SepWellKnownSids[Index].Target;

        if (*Target == NULL) continue;

        ExFreePoolWithTag(*Target, TAG_SID);
        *Target = NULL;
    }

    /* These named the same identifiers, so they are gone with them */
    SeAuthenticatedUsersSid = NULL;
    SeRestrictedSid = NULL;
}

/* PUBLIC FUNCTIONS ***********************************************************/

/*
 * An ACE keeps its identifier after whatever else it carries, and an object
 * ACE only carries the two type names it says it has, so where the identifier
 * starts depends on the flags.
 */
PSID
SepGetSidFromAce(
    _In_ PACE Ace)
{
    PUCHAR Start;
    ULONG Flags;

    PAGED_CODE();

    switch (Ace->Header.AceType)
    {
        case ACCESS_ALLOWED_OBJECT_ACE_TYPE:
        case ACCESS_DENIED_OBJECT_ACE_TYPE:
        case SYSTEM_AUDIT_OBJECT_ACE_TYPE:
        case SYSTEM_ALARM_OBJECT_ACE_TYPE:
            break;

        default:
            return (PSID)&((PKNOWN_ACE)Ace)->SidStart;
    }

    Flags = ((PACCESS_ALLOWED_OBJECT_ACE)Ace)->Flags;
    Start = (PUCHAR)&((PACCESS_ALLOWED_OBJECT_ACE)Ace)->SidStart;

    if (!(Flags & ACE_INHERITED_OBJECT_TYPE_PRESENT)) Start -= sizeof(GUID);
    if (!(Flags & ACE_OBJECT_TYPE_PRESENT)) Start -= sizeof(GUID);

    return (PSID)Start;
}

/*
 * Answers whether a token carries an identifier, and whether it carries it in
 * a way that counts: a group that is turned off grants nothing, and one marked
 * for denial only is still read when a denial is being decided.
 */
BOOLEAN
NTAPI
SepSidInTokenEx(
    _In_ PACCESS_TOKEN _Token,
    _In_ PSID PrincipalSelfSid,
    _In_ PSID _Sid,
    _In_ BOOLEAN Deny,
    _In_ ULONG SidSet)
{
    PSID_AND_ATTRIBUTES Entries;
    PTOKEN Token = (PTOKEN)_Token;
    PSEP_LOWBOX_INFO LowBox;
    PSID Wanted = _Sid;
    ULONG Count;
    ULONG Index;

    PAGED_CODE();

    /* What "self" means is whatever the caller says it means here */
    if (PrincipalSelfSid != NULL &&
        SePrincipalSelfSid != NULL &&
        RtlEqualSid(SePrincipalSelfSid, _Sid))
    {
        Wanted = PrincipalSelfSid;
    }

    switch (SidSet)
    {
        case SEP_SID_SET_RESTRICTED:
            Entries = Token->RestrictedSids;
            Count = Token->RestrictedSidCount;
            break;

        case SEP_SID_SET_CAPABILITIES:
            LowBox = Token->LowBoxInfo;
            if (LowBox == NULL) return FALSE;
            Entries = LowBox->Capabilities;
            Count = LowBox->CapabilityCount;
            break;

        default:
            Entries = Token->UserAndGroups;
            Count = Token->UserAndGroupCount;
            break;
    }

    if (Entries == NULL) return FALSE;

    for (Index = 0; Index < Count; Index++)
    {
        if (!RtlEqualSid(Entries[Index].Sid, Wanted)) continue;

        /*
         * The first entry of the groups is the token's own user, which it
         * always carries; the others answer for how they are set.
         */
        if (SidSet == SEP_SID_SET_GROUPS && Index == 0) return TRUE;

        if (Entries[Index].Attributes & SE_GROUP_ENABLED) return TRUE;

        if (Deny && (Entries[Index].Attributes & SE_GROUP_USE_FOR_DENY_ONLY))
            return TRUE;

        return FALSE;
    }

    return FALSE;
}

BOOLEAN
NTAPI
SepSidInToken(
    _In_ PACCESS_TOKEN _Token,
    _In_ PSID Sid)
{
    PAGED_CODE();

    return SepSidInTokenEx(_Token, NULL, Sid, FALSE, SEP_SID_SET_GROUPS);
}

/*
 * Takes an identifier out of the caller's memory. Its length is in its own
 * first bytes, so the count is read and checked before anything is sized from
 * it, and the copy is validated once it can no longer change.
 */
NTSTATUS
NTAPI
SepCaptureSid(
    _In_ PSID InputSid,
    _In_ KPROCESSOR_MODE AccessMode,
    _In_ POOL_TYPE PoolType,
    _In_ BOOLEAN CaptureIfKernel,
    _Out_ PSID *CapturedSid)
{
    NTSTATUS Status = STATUS_SUCCESS;
    UCHAR Count = 0;
    PSID Copy;
    ULONG Length;

    PAGED_CODE();

    *CapturedSid = NULL;

    if (AccessMode == KernelMode && !CaptureIfKernel)
    {
        *CapturedSid = InputSid;
        return STATUS_SUCCESS;
    }

    if (AccessMode != KernelMode)
    {
        _SEH2_TRY
        {
            ProbeForRead(InputSid,
                         FIELD_OFFSET(SID, SubAuthority),
                         sizeof(ULONG));
            Count = ((PISID)InputSid)->SubAuthorityCount;
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
        Count = ((PISID)InputSid)->SubAuthorityCount;
    }

    if (Count > SID_MAX_SUB_AUTHORITIES) return STATUS_INVALID_SID;

    Length = RtlLengthRequiredSid(Count);

    Copy = ExAllocatePoolWithTag(PoolType, Length, TAG_SID);
    if (Copy == NULL) return STATUS_INSUFFICIENT_RESOURCES;

    _SEH2_TRY
    {
        if (AccessMode != KernelMode)
            ProbeForRead(InputSid, Length, sizeof(ULONG));

        RtlCopyMemory(Copy, InputSid, Length);
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Status = _SEH2_GetExceptionCode();
    }
    _SEH2_END;

    /* Only now can it no longer change underneath the check */
    if (NT_SUCCESS(Status) && !RtlValidSid(Copy)) Status = STATUS_INVALID_SID;

    if (!NT_SUCCESS(Status))
    {
        ExFreePoolWithTag(Copy, TAG_SID);
        return Status;
    }

    *CapturedSid = Copy;

    return STATUS_SUCCESS;
}

VOID
NTAPI
SepReleaseSid(
    _In_ PSID CapturedSid,
    _In_ KPROCESSOR_MODE AccessMode,
    _In_ BOOLEAN CaptureIfKernel)
{
    PAGED_CODE();

    if (CapturedSid == NULL) return;
    if (AccessMode == KernelMode && !CaptureIfKernel) return;

    ExFreePoolWithTag(CapturedSid, TAG_SID);
}

/*
 * Takes a whole array out of the caller's memory into one block: the entries
 * first, then the identifiers they name, so the copy is self-contained and the
 * pointers in it no longer reach into the caller.
 */
NTSTATUS
NTAPI
SeCaptureSidAndAttributesArray(
    _In_ PSID_AND_ATTRIBUTES SrcSidAndAttributes,
    _In_ ULONG AttributeCount,
    _In_ KPROCESSOR_MODE PreviousMode,
    _In_opt_ PVOID AllocatedMem,
    _In_ ULONG AllocatedLength,
    _In_ POOL_TYPE PoolType,
    _In_ BOOLEAN CaptureIfKernel,
    _Out_ PSID_AND_ATTRIBUTES *CapturedSidAndAttributes,
    _Out_ PULONG ResultLength)
{
    PSID_AND_ATTRIBUTES Entries;
    NTSTATUS Status = STATUS_SUCCESS;
    SIZE_T ArraySize;
    SIZE_T Total;
    PUCHAR Cursor;
    ULONG Index;
    BOOLEAN Owned = FALSE;

    PAGED_CODE();

    *CapturedSidAndAttributes = NULL;
    *ResultLength = 0;

    if (AttributeCount == 0) return STATUS_SUCCESS;

    if (PreviousMode == KernelMode && !CaptureIfKernel)
    {
        *CapturedSidAndAttributes = SrcSidAndAttributes;
        return STATUS_SUCCESS;
    }

    if (AttributeCount > MAXULONG / sizeof(SID_AND_ATTRIBUTES))
        return STATUS_INVALID_PARAMETER;

    ArraySize = (SIZE_T)AttributeCount * sizeof(SID_AND_ATTRIBUTES);
    Total = ArraySize;

    if (PreviousMode != KernelMode)
    {
        _SEH2_TRY
        {
            ProbeForRead(SrcSidAndAttributes,
                         ArraySize,
                         TYPE_ALIGNMENT(SID_AND_ATTRIBUTES));
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            Status = _SEH2_GetExceptionCode();
        }
        _SEH2_END;

        if (!NT_SUCCESS(Status)) return Status;
    }

    /* Work out the room the identifiers need before anything is allocated */
    _SEH2_TRY
    {
        for (Index = 0; Index < AttributeCount; Index++)
        {
            PISID Sid = SrcSidAndAttributes[Index].Sid;
            UCHAR Count;

            if (Sid == NULL)
            {
                Status = STATUS_INVALID_SID;
                break;
            }

            if (PreviousMode != KernelMode)
            {
                ProbeForRead(Sid,
                             FIELD_OFFSET(SID, SubAuthority),
                             sizeof(ULONG));
            }

            Count = Sid->SubAuthorityCount;
            if (Count > SID_MAX_SUB_AUTHORITIES)
            {
                Status = STATUS_INVALID_SID;
                break;
            }

            Total += ALIGN_UP(RtlLengthRequiredSid(Count), ULONG);
        }
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Status = _SEH2_GetExceptionCode();
    }
    _SEH2_END;

    if (!NT_SUCCESS(Status)) return Status;
    if (Total > MAXULONG) return STATUS_INVALID_PARAMETER;

    if (AllocatedMem != NULL)
    {
        if (AllocatedLength < Total) return STATUS_BUFFER_TOO_SMALL;
        Entries = AllocatedMem;
    }
    else
    {
        Entries = ExAllocatePoolWithTag(PoolType, Total, TAG_SID);
        if (Entries == NULL) return STATUS_INSUFFICIENT_RESOURCES;
        Owned = TRUE;
    }

    Cursor = (PUCHAR)Entries + ArraySize;

    _SEH2_TRY
    {
        for (Index = 0; Index < AttributeCount; Index++)
        {
            PISID Sid = SrcSidAndAttributes[Index].Sid;
            ULONG Length = RtlLengthRequiredSid(Sid->SubAuthorityCount);

            if (PreviousMode != KernelMode) ProbeForRead(Sid, Length, sizeof(ULONG));

            RtlCopyMemory(Cursor, Sid, Length);

            if (!RtlValidSid((PSID)Cursor))
            {
                Status = STATUS_INVALID_SID;
                break;
            }

            Entries[Index].Sid = (PSID)Cursor;
            Entries[Index].Attributes = SrcSidAndAttributes[Index].Attributes;

            Cursor += ALIGN_UP(Length, ULONG);
        }
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Status = _SEH2_GetExceptionCode();
    }
    _SEH2_END;

    if (!NT_SUCCESS(Status))
    {
        if (Owned) ExFreePoolWithTag(Entries, TAG_SID);
        return Status;
    }

    *CapturedSidAndAttributes = Entries;
    *ResultLength = (ULONG)Total;

    return STATUS_SUCCESS;
}

VOID
NTAPI
SeReleaseSidAndAttributesArray(
    _In_ _Post_invalid_ PSID_AND_ATTRIBUTES CapturedSidAndAttributes,
    _In_ KPROCESSOR_MODE AccessMode,
    _In_ BOOLEAN CaptureIfKernel)
{
    PAGED_CODE();

    if (CapturedSidAndAttributes == NULL) return;
    if (AccessMode == KernelMode && !CaptureIfKernel) return;

    ExFreePoolWithTag(CapturedSidAndAttributes, TAG_SID);
}

/* EOF */
