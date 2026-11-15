/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Security manager internals
 * COPYRIGHT:   Copyright Eric Kohl
 *              Copyright 2022-2023 George Bișoc <george.bisoc@reactos.org>
 *              Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

typedef struct _KNOWN_ACE
{
    ACE_HEADER Header;
    ACCESS_MASK Mask;
    ULONG SidStart;
} KNOWN_ACE, *PKNOWN_ACE;

typedef struct _KNOWN_OBJECT_ACE
{
    ACE_HEADER Header;
    ACCESS_MASK Mask;
    ULONG Flags;
    ULONG SidStart;
} KNOWN_OBJECT_ACE, *PKNOWN_OBJECT_ACE;

typedef struct _ACCESS_CHECK_RIGHTS
{
    ACCESS_MASK RemainingAccessRights;
    ACCESS_MASK GrantedAccessRights;
    ACCESS_MASK DeniedAccessRights;
} ACCESS_CHECK_RIGHTS, *PACCESS_CHECK_RIGHTS;

typedef struct _OBJECT_TYPE_LIST_INTERNAL
{
    GUID ObjectTypeGuid;
    USHORT Level;
    ACCESS_CHECK_RIGHTS ObjectAccessRights;
} OBJECT_TYPE_LIST_INTERNAL, *POBJECT_TYPE_LIST_INTERNAL;

#ifndef RTL_H

extern SID_IDENTIFIER_AUTHORITY SeNullSidAuthority;

extern SID_IDENTIFIER_AUTHORITY SeMandatoryLabelAuthority;

extern SID_IDENTIFIER_AUTHORITY SeAppPackageAuthority;

extern SID_IDENTIFIER_AUTHORITY SeWorldSidAuthority;

extern SID_IDENTIFIER_AUTHORITY SeLocalSidAuthority;

extern SID_IDENTIFIER_AUTHORITY SeCreatorSidAuthority;

extern SID_IDENTIFIER_AUTHORITY SeNtSidAuthority;

extern PSID SeNullSid;

extern PSID SeWorldSid;

extern PSID SeLocalSid;

extern PSID SeCreatorOwnerSid;

extern PSID SeOwnerRightsSid;

extern PSID SeCreatorGroupSid;

extern PSID SeCreatorOwnerServerSid;

extern PSID SeCreatorGroupServerSid;

extern PSID SeNtAuthoritySid;

extern PSID SeDialupSid;

extern PSID SeNetworkSid;

extern PSID SeBatchSid;

extern PSID SeInteractiveSid;

extern PSID SeServiceSid;

extern PSID SeAnonymousLogonSid;

extern PSID SePrincipalSelfSid;

extern PSID SeLocalSystemSid;

extern PSID SeUntrustedMandatorySid;

extern PSID SeLowMandatorySid;

extern PSID SeMediumMandatorySid;

extern PSID SeHighMandatorySid;

extern PSID SeSystemMandatorySid;

extern PSID SeAllAppPackagesSid;

extern PSID SeAllRestrictedAppPackagesSid;

extern PSID SeIUserSid;

extern PSID SeUserModeDriversSid;

extern PSID SeProcTrustWinTcbSid;

extern PSID SeTrustedInstallerSid;

extern PSID SeAuthenticatedUserSid;

extern PSID SeRestrictedCodeSid;

extern PSID SeAliasAdminsSid;

extern PSID SeAliasUsersSid;

extern PSID SeAliasGuestsSid;

extern PSID SeAliasPowerUsersSid;

extern PSID SeAliasAccountOpsSid;

extern PSID SeAliasSystemOpsSid;

extern PSID SeAliasPrintOpsSid;

extern PSID SeAliasBackupOpsSid;

extern PSID SeAuthenticatedUsersSid;

extern PSID SeRestrictedSid;

extern PSID SeAnonymousLogonSid;

extern PSID SeLocalServiceSid;

extern PSID SeNetworkServiceSid;

extern LUID SeSystemAuthenticationId;

extern LUID SeAnonymousAuthenticationId;

extern const LUID SeCreateTokenPrivilege;

extern const LUID SeAssignPrimaryTokenPrivilege;

extern const LUID SeLockMemoryPrivilege;

extern const LUID SeIncreaseQuotaPrivilege;

extern const LUID SeUnsolicitedInputPrivilege;

extern const LUID SeTcbPrivilege;

extern const LUID SeSecurityPrivilege;

extern const LUID SeTakeOwnershipPrivilege;

extern const LUID SeLoadDriverPrivilege;

extern const LUID SeSystemProfilePrivilege;

extern const LUID SeSystemtimePrivilege;

extern const LUID SeProfileSingleProcessPrivilege;

extern const LUID SeIncreaseBasePriorityPrivilege;

extern const LUID SeCreatePagefilePrivilege;

extern const LUID SeCreatePermanentPrivilege;

extern const LUID SeBackupPrivilege;

extern const LUID SeRestorePrivilege;

extern const LUID SeShutdownPrivilege;

extern const LUID SeDebugPrivilege;

extern const LUID SeAuditPrivilege;

extern const LUID SeSystemEnvironmentPrivilege;

extern const LUID SeChangeNotifyPrivilege;

extern const LUID SeRemoteShutdownPrivilege;

extern const LUID SeUndockPrivilege;

extern const LUID SeSyncAgentPrivilege;

extern const LUID SeEnableDelegationPrivilege;

extern const LUID SeManageVolumePrivilege;

extern const LUID SeImpersonatePrivilege;

extern const LUID SeCreateGlobalPrivilege;

extern const LUID SeTrustedCredmanPrivilege;

extern const LUID SeRelabelPrivilege;

extern const LUID SeIncreaseWorkingSetPrivilege;

extern const LUID SeTimeZonePrivilege;

extern const LUID SeCreateSymbolicLinkPrivilege;

extern const LUID SeDelegateSessionUserImpersonatePrivilege;

extern PACL SePublicDefaultUnrestrictedDacl;

extern PACL SePublicOpenDacl;

extern PACL SePublicOpenUnrestrictedDacl;

extern PACL SeSystemAnonymousLogonDacl;

extern PSECURITY_DESCRIPTOR SePublicDefaultSd;

extern PSECURITY_DESCRIPTOR SePublicDefaultUnrestrictedSd;

extern PSECURITY_DESCRIPTOR SePublicOpenSd;

extern PSECURITY_DESCRIPTOR SePublicOpenUnrestrictedSd;

extern PSECURITY_DESCRIPTOR SeSystemDefaultSd;

extern PTOKEN SeAnonymousLogonToken;

extern PTOKEN SeAnonymousLogonTokenNoEveryone;

#if DBG

VOID
SepDumpSdDebugInfo(
    _In_opt_ PISECURITY_DESCRIPTOR SecurityDescriptor);

VOID
SepDumpTokenDebugInfo(
   _In_opt_ PTOKEN Token);

VOID
SepDumpAccessRightsStats(
    _In_ PACCESS_CHECK_RIGHTS AccessRights);

VOID
SepDumpAccessAndStatusList(
    _In_ PACCESS_MASK GrantedAccessList,
    _In_ PNTSTATUS AccessStatusList,
    _In_ BOOLEAN IsResultList,
    _In_ POBJECT_TYPE_LIST_INTERNAL ObjectTypeList,
    _In_ ULONG ObjectTypeListLength);
#endif

#define SEP_SID_SET_GROUPS       0

#define SEP_SID_SET_RESTRICTED   1

#define SEP_SID_SET_CAPABILITIES 2

#define SEP_LOWBOX_LPAC 0x1

typedef struct _SEP_LOWBOX_INFO
{
    ULONG Flags;
    ULONG LowBoxNumber;
    ULONG SessionId;
    PSID PackageSid;
    ULONG CapabilityCount;
    PSID_AND_ATTRIBUTES Capabilities;
    ULONG HandleCount;
    PVOID *Handles;
} SEP_LOWBOX_INFO, *PSEP_LOWBOX_INFO;

extern ERESOURCE SepSubjectContextLock;

NTSTATUS
NTAPI
SeCreateAccessStateEx(
    _In_opt_ PETHREAD Thread,
    _In_ PEPROCESS Process,
    _Inout_ PACCESS_STATE AccessState,
    _In_ PAUX_ACCESS_DATA AuxData,
    _In_ ACCESS_MASK Access,
    _In_opt_ PGENERIC_MAPPING GenericMapping);

BOOLEAN
NTAPI
SeFastTraverseCheck(
    _In_ PSECURITY_DESCRIPTOR SecurityDescriptor,
    _In_ PACCESS_STATE AccessState,
    _In_ ACCESS_MASK DesiredAccess,
    _In_ KPROCESSOR_MODE AccessMode);

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
    _Out_opt_ PPRIVILEGE_SET PrivilegeSet,
    _Inout_opt_ PULONG PrivilegeSetLength,
    _Out_ PACCESS_MASK GrantedAccess,
    _Out_ PNTSTATUS AccessStatus,
    _In_ BOOLEAN ResultList,
    _In_ BOOLEAN ReturnPrivilegeSet);

NTSTATUS
NTAPI
SepCaptureAcl(
    _In_ PACL InputAcl,
    _In_ KPROCESSOR_MODE AccessMode,
    _In_ POOL_TYPE PoolType,
    _In_ BOOLEAN CaptureIfKernel,
    _Out_ PACL *CapturedAcl);

NTSTATUS
NTAPI
SepCreateImpersonationTokenDacl(
    _In_ PTOKEN Token,
    _In_ PTOKEN PrimaryToken,
    _Out_ PACL* Dacl);

CODE_SEG("INIT")
BOOLEAN
NTAPI
SepInitSystemDacls(VOID);

VOID
NTAPI
SepReleaseAcl(
    _In_ PACL CapturedAcl,
    _In_ KPROCESSOR_MODE AccessMode,
    _In_ BOOLEAN CaptureIfKernel);

NTSTATUS
SepSelectAcl(
    _In_opt_ PACL ExplicitAcl,
    _In_ BOOLEAN ExplicitPresent,
    _In_ BOOLEAN ExplicitDefaulted,
    _In_opt_ PACL ParentAcl,
    _In_opt_ PACL DefaultAcl,
    _Out_ PULONG AclLength,
    _In_ PSID Owner,
    _In_ PSID Group,
    _Out_ PBOOLEAN AclPresent,
    _Out_ PBOOLEAN IsInherited,
    _In_ BOOLEAN IsDirectoryObject,
    _In_opt_ GUID *ObjectType,
    _In_ PGENERIC_MAPPING GenericMapping,
    _Out_ PACL *SelectedAcl);

VOID
NTAPI
SeAuditProcessCreation(
    _In_ PEPROCESS Process);

VOID
NTAPI
SeAuditProcessExit(
    _In_ PEPROCESS Process);

VOID
NTAPI
SeCloseObjectAuditAlarm(
    _In_ PVOID Object,
    _In_ HANDLE Handle,
    _In_ BOOLEAN GenerateOnClose);

BOOLEAN
NTAPI
SeDetailedAuditingWithToken(
    _In_opt_ PTOKEN Token);

NTSTATUS
NTAPI
SeInitializeProcessAuditName(
    _In_ PFILE_OBJECT FileObject,
    _In_ BOOLEAN DoAudit,
    _Out_ POBJECT_NAME_INFORMATION *AuditInfo);

VOID
NTAPI
SePrivilegeObjectAuditAlarm(
    _In_ HANDLE Handle,
    _In_ PSECURITY_SUBJECT_CONTEXT SubjectContext,
    _In_ ACCESS_MASK DesiredAccess,
    _In_ PPRIVILEGE_SET Privileges,
    _In_ BOOLEAN AccessGranted,
    _In_ KPROCESSOR_MODE CurrentMode);

VOID
NTAPI
SePrivilegedServiceAuditAlarm(
    _In_opt_ PUNICODE_STRING ServiceName,
    _In_ PSECURITY_SUBJECT_CONTEXT SubjectContext,
    _In_ PPRIVILEGE_SET PrivilegeSet,
    _In_ BOOLEAN AccessGranted);

CODE_SEG("INIT")
BOOLEAN
NTAPI
SeInitSystem(VOID);

CODE_SEG("INIT")
BOOLEAN
NTAPI
SepInitializationPhase0(VOID);

CODE_SEG("INIT")
BOOLEAN
NTAPI
SepInitializationPhase1(VOID);

NTSTATUS
NTAPI
SeDefaultObjectMethod(
    _In_ PVOID Object,
    _In_ SECURITY_OPERATION_CODE OperationType,
    _In_ PSECURITY_INFORMATION SecurityInformation,
    _Inout_opt_ PSECURITY_DESCRIPTOR SecurityDescriptor,
    _Inout_opt_ PULONG ReturnLength,
    _Inout_opt_ PSECURITY_DESCRIPTOR *OldSecurityDescriptor,
    _In_ POOL_TYPE PoolType,
    _In_ PGENERIC_MAPPING GenericMapping,
    _In_ KPROCESSOR_MODE AccessMode);

VOID
NTAPI
SeQuerySecurityAccessMask(
    _In_ SECURITY_INFORMATION SecurityInformation,
    _Out_ PACCESS_MASK DesiredAccess);

VOID
NTAPI
SeSetSecurityAccessMask(
    _In_ SECURITY_INFORMATION SecurityInformation,
    _Out_ PACCESS_MASK DesiredAccess);

VOID
SeReleaseObjectTypeList(
    _In_ _Post_invalid_ POBJECT_TYPE_LIST_INTERNAL CapturedObjectTypeList,
    _In_ KPROCESSOR_MODE PreviousMode);

PGUID
SepGetObjectTypeGuidFromAce(
    _In_ PACE Ace,
    _In_ BOOLEAN IsAceDenied);

NTSTATUS
NTAPI
SeCaptureLuidAndAttributesArray(
    _In_ PLUID_AND_ATTRIBUTES Src,
    _In_ ULONG PrivilegeCount,
    _In_ KPROCESSOR_MODE PreviousMode,
    _In_ PLUID_AND_ATTRIBUTES AllocatedMem,
    _In_ ULONG AllocatedLength,
    _In_ POOL_TYPE PoolType,
    _In_ BOOLEAN CaptureIfKernel,
    _Out_ PLUID_AND_ATTRIBUTES* Dest,
    _Inout_ PULONG Length);

BOOLEAN
NTAPI
SeCheckAuditPrivilege(
    _In_ PSECURITY_SUBJECT_CONTEXT SubjectContext,
    _In_ KPROCESSOR_MODE PreviousMode);

BOOLEAN
NTAPI
SeCheckPrivilegedObject(
    _In_ LUID PrivilegeValue,
    _In_ HANDLE ObjectHandle,
    _In_ ACCESS_MASK DesiredAccess,
    _In_ KPROCESSOR_MODE PreviousMode);

NTSTATUS
NTAPI
SePrivilegePolicyCheck(
    _Inout_ PACCESS_MASK DesiredAccess,
    _Inout_ PACCESS_MASK GrantedAccess,
    _In_ PSECURITY_SUBJECT_CONTEXT SubjectContext,
    _In_ PTOKEN Token,
    _Out_opt_ PPRIVILEGE_SET *OutPrivilegeSet,
    _In_ KPROCESSOR_MODE PreviousMode);

VOID
NTAPI
SeReleaseLuidAndAttributesArray(
    _In_ PLUID_AND_ATTRIBUTES Privilege,
    _In_ KPROCESSOR_MODE PreviousMode,
    _In_ BOOLEAN CaptureIfKernel);

CODE_SEG("INIT")
VOID
NTAPI
SepInitPrivileges(VOID);

BOOLEAN
NTAPI
SepPrivilegeCheck(
    _In_ PTOKEN Token,
    _In_ PLUID_AND_ATTRIBUTES Privileges,
    _In_ ULONG PrivilegeCount,
    _In_ ULONG PrivilegeControl,
    _In_ KPROCESSOR_MODE PreviousMode);

NTSTATUS
NTAPI
SepCaptureSecurityQualityOfService(
    _In_opt_ POBJECT_ATTRIBUTES ObjectAttributes,
    _In_ KPROCESSOR_MODE AccessMode,
    _In_ POOL_TYPE PoolType,
    _In_ BOOLEAN CaptureIfKernel,
    _Out_ PSECURITY_QUALITY_OF_SERVICE *CapturedSecurityQualityOfService,
    _Out_ PBOOLEAN Present);

VOID
NTAPI
SepReleaseSecurityQualityOfService(
    _In_opt_ PSECURITY_QUALITY_OF_SERVICE CapturedSecurityQualityOfService,
    _In_ KPROCESSOR_MODE AccessMode,
    _In_ BOOLEAN CaptureIfKernel);

NTSTATUS
NTAPI
SeComputeQuotaInformationSize(
    _In_ PSECURITY_DESCRIPTOR SecurityDescriptor,
    _Out_ PULONG QuotaInfoSize);

NTSTATUS
NTAPI
SeSetWorldSecurityDescriptor(
    _In_ SECURITY_INFORMATION SecurityInformation,
    _In_ PISECURITY_DESCRIPTOR SecurityDescriptor,
    _In_ PULONG BufferLength);

CODE_SEG("INIT")
BOOLEAN
NTAPI
SepInitSystemSds(VOID);

NTSTATUS
NTAPI
SeConvertStringSecurityDescriptorToSecurityDescriptor(
    _In_ PCWSTR StringSecurityDescriptor,
    _In_ ULONG StringSecurityDescriptorRevision,
    _Outptr_ PSECURITY_DESCRIPTOR *SecurityDescriptor,
    _Out_opt_ PULONG SecurityDescriptorSize);

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
    _Out_ PULONG ResultLength);

VOID
NTAPI
SeReleaseSidAndAttributesArray(
    _In_ _Post_invalid_ PSID_AND_ATTRIBUTES CapturedSidAndAttributes,
    _In_ KPROCESSOR_MODE AccessMode,
    _In_ BOOLEAN CaptureIfKernel);

NTSTATUS
NTAPI
SepCaptureSid(
    _In_ PSID InputSid,
    _In_ KPROCESSOR_MODE AccessMode,
    _In_ POOL_TYPE PoolType,
    _In_ BOOLEAN CaptureIfKernel,
    _Out_ PSID *CapturedSid);

PSID
NTAPI
SepGetSidFromAce(
    _In_ PACE Ace);

CODE_SEG("INIT")
BOOLEAN
NTAPI
SepInitSecurityIDs(VOID);

VOID
NTAPI
SepReleaseSid(
    _In_ PSID CapturedSid,
    _In_ KPROCESSOR_MODE AccessMode,
    _In_ BOOLEAN CaptureIfKernel);

BOOLEAN
NTAPI
SepSidInToken(
    _In_ PACCESS_TOKEN _Token,
    _In_ PSID Sid);

BOOLEAN
NTAPI
SepSidInTokenEx(
    _In_ PACCESS_TOKEN _Token,
    _In_ PSID PrincipalSelfSid,
    _In_ PSID _Sid,
    _In_ BOOLEAN Deny,
    _In_ ULONG SidSet);

NTSTATUS
NTAPI
SeGetLogonIdDeviceMap(
    _In_ PLUID LogonId,
    _Out_ PDEVICE_MAP *DeviceMap);

BOOLEAN
NTAPI
SeRmInitPhase1(VOID);

NTSTATUS
NTAPI
SepRegQueryHelper(
    _In_ PCWSTR KeyName,
    _In_ PCWSTR ValueName,
    _In_ ULONG ValueType,
    _In_ ULONG DataLength,
    _Out_ PVOID ValueData);

NTSTATUS
SepRmDereferenceLogonSession(
    _Inout_ PLUID LogonLuid);

CODE_SEG("INIT")
BOOLEAN
NTAPI
SepRmInitializeSessions(VOID);

NTSTATUS
NTAPI
SepRmInsertLogonSessionIntoToken(
    _Inout_ PTOKEN Token);

NTSTATUS
SepRmReferenceLogonSession(
    _Inout_ PLUID LogonLuid);

NTSTATUS
NTAPI
SepRmRemoveLogonSessionFromToken(
    _Inout_ PTOKEN Token);

VOID
NTAPI
SeCaptureSubjectContextEx(
    _In_opt_ PETHREAD Thread,
    _In_ PEPROCESS Process,
    _Out_ PSECURITY_SUBJECT_CONTEXT SubjectContext);

NTSTATUS
NTAPI
SeCopyClientToken(
    _In_ PACCESS_TOKEN Token,
    _In_ SECURITY_IMPERSONATION_LEVEL Level,
    _In_ KPROCESSOR_MODE PreviousMode,
    _Out_ PACCESS_TOKEN* NewToken);

NTSTATUS
NTAPI
SeSubProcessToken(
    _In_ PTOKEN Parent,
    _Out_ PTOKEN *Token,
    _In_ BOOLEAN InUse,
    _In_ ULONG SessionId);

VOID
SepAcquireTokenLockExclusive(
    _Inout_ PTOKEN Token);

VOID
SepAcquireTokenLockShared(
    _Inout_ PTOKEN Token);

NTSTATUS
NTAPI
SepCopyImpersonationToken(
    _In_ PACCESS_TOKEN Token,
    _In_ BOOLEAN EffectiveOnly,
    _In_ SECURITY_IMPERSONATION_LEVEL Level,
    _In_ KPROCESSOR_MODE PreviousMode,
    _Out_ PACCESS_TOKEN* NewToken);

NTSTATUS
NTAPI
SepCopyPrimaryToken(
    _In_ PACCESS_TOKEN Token,
    _Out_ PACCESS_TOKEN* NewToken);

CODE_SEG("INIT")
PTOKEN
NTAPI
SepCreateSystemAnonymousLogonToken(VOID);

CODE_SEG("INIT")
PTOKEN
NTAPI
SepCreateSystemAnonymousLogonTokenNoEveryone(VOID);

CODE_SEG("INIT")
PTOKEN
NTAPI
SepCreateSystemProcessToken(VOID);

VOID
NTAPI
SepDeleteToken(
    _In_ PVOID ObjectBody);

VOID
SepDropTokenGroup(
    _Inout_ PTOKEN Token,
    _In_ ULONG Index);

VOID
SepDropTokenPrivilege(
    _Inout_ PTOKEN Token,
    _In_ ULONG Index);

ULONG
NTAPI
SepGetTokenIntegrityRid(
    _In_ PACCESS_TOKEN Token);

NTSTATUS
SepGrowChangeablePart(
    _Inout_ PTOKEN Token,
    _In_ ULONG Needed);

CODE_SEG("INIT")
BOOLEAN
NTAPI
SepInitializeTokenImplementation(VOID);

VOID
NTAPI
SepMarkLessPrivilegedAppContainer(
    _In_ PACCESS_TOKEN Token);

VOID
SepNoteTokenPrivilegeFlags(
    _Inout_ PTOKEN Token);

ULONG
NTAPI
SepQueryTokenFlags(
    _In_ PACCESS_TOKEN Token);

VOID
SepReleaseTokenLock(
    _Inout_ PTOKEN Token);

VOID
NTAPI
SepSetTokenFlags(
    _In_ PACCESS_TOKEN Token,
    _In_ ULONG Flags);

VOID
NTAPI
SeAssignPrimaryToken(
    _In_ PEPROCESS Process,
    _In_ PTOKEN Token);

VOID
NTAPI
SeDeassignPrimaryToken(
    _Inout_ PEPROCESS Process);

NTSTATUS
NTAPI
SeExchangePrimaryToken(
    _In_ PEPROCESS Process,
    _In_ PACCESS_TOKEN NewAccessToken,
    _Out_ PACCESS_TOKEN* OldAccessToken);

NTSTATUS
NTAPI
SeIsTokenChild(
    _In_ PTOKEN Token,
    _Out_ PBOOLEAN IsChild);

NTSTATUS
NTAPI
SeIsTokenSibling(
    _In_ PTOKEN Token,
    _Out_ PBOOLEAN IsSibling);

NTSTATUS
NTAPI
SeSetObjectMandatoryLabel(
    _In_ PVOID Object,
    _In_ ULONG Rid,
    _In_ ULONG Policy);

BOOLEAN
NTAPI
SeTokenCanImpersonate(
    _In_ PTOKEN ProcessToken,
    _In_ PTOKEN TokenToImpersonate,
    _In_ SECURITY_IMPERSONATION_LEVEL ImpersonationLevel);

BOOLEAN
NTAPI
SepTokenIsOwner(
    _In_ PACCESS_TOKEN _Token,
    _In_ PSECURITY_DESCRIPTOR SecurityDescriptor,
    _In_ BOOLEAN TokenLocked);

VOID
NTAPI
SeGetTokenControlInformation(
    _In_ PACCESS_TOKEN _Token,
    _Out_ PTOKEN_CONTROL TokenControl);

BOOLEAN
NTAPI
SeTokenIsInert(
    _In_ PTOKEN Token);

NTSTATUS
SeCaptureObjectTypeList(
    _In_reads_opt_(ObjectTypeListLength) POBJECT_TYPE_LIST ObjectTypeList,
    _In_ ULONG ObjectTypeListLength,
    _In_ KPROCESSOR_MODE PreviousMode,
    _Out_ POBJECT_TYPE_LIST_INTERNAL *CapturedObjectTypeList);

BOOLEAN
SepObjectTypeGuidInList(
    _In_reads_(ObjectTypeListLength) POBJECT_TYPE_LIST_INTERNAL ObjectTypeList,
    _In_ ULONG ObjectTypeListLength,
    _In_ PGUID ObjectTypeGuid,
    _Out_ PULONG ObjectIndex);

NTSTATUS
SepPropagateAcl(
    _Out_writes_bytes_opt_(AclLength) PACL AclDest,
    _Inout_ PULONG AclLength,
    _In_reads_bytes_(AclSource->AclSize) PACL AclSource,
    _In_ PSID Owner,
    _In_ PSID Group,
    _In_ BOOLEAN IsInherited,
    _In_ BOOLEAN MarkInherited,
    _In_ BOOLEAN IsDirectoryObject,
    _In_opt_ GUID *ObjectType,
    _In_ PGENERIC_MAPPING GenericMapping);

#endif

FORCEINLINE
PVOID
SepRelativeComponent(
    _In_ PISECURITY_DESCRIPTOR_RELATIVE Relative,
    _In_ ULONG Offset)
{
    return Offset != 0 ? (PVOID)((PUCHAR)Relative + Offset) : NULL;
}

FORCEINLINE
PSID
SepGetOwnerFromDescriptor(
    _Inout_ PSECURITY_DESCRIPTOR SecurityDescriptor)
{
    PISECURITY_DESCRIPTOR Absolute = SecurityDescriptor;
    PISECURITY_DESCRIPTOR_RELATIVE Relative = SecurityDescriptor;

    if (!(Absolute->Control & SE_SELF_RELATIVE))
        return Absolute->Owner;

    return SepRelativeComponent(Relative, Relative->Owner);
}

FORCEINLINE
PSID
SepGetGroupFromDescriptor(
    _Inout_ PSECURITY_DESCRIPTOR SecurityDescriptor)
{
    PISECURITY_DESCRIPTOR Absolute = SecurityDescriptor;
    PISECURITY_DESCRIPTOR_RELATIVE Relative = SecurityDescriptor;

    if (!(Absolute->Control & SE_SELF_RELATIVE))
        return Absolute->Group;

    return SepRelativeComponent(Relative, Relative->Group);
}

FORCEINLINE
PACL
SepGetDaclFromDescriptor(
    _Inout_ PSECURITY_DESCRIPTOR SecurityDescriptor)
{
    PISECURITY_DESCRIPTOR Absolute = SecurityDescriptor;
    PISECURITY_DESCRIPTOR_RELATIVE Relative = SecurityDescriptor;

    if (!(Absolute->Control & SE_DACL_PRESENT))
        return NULL;

    if (!(Absolute->Control & SE_SELF_RELATIVE))
        return Absolute->Dacl;

    return SepRelativeComponent(Relative, Relative->Dacl);
}

FORCEINLINE
PACL
SepGetSaclFromDescriptor(
    _Inout_ PSECURITY_DESCRIPTOR SecurityDescriptor)
{
    PISECURITY_DESCRIPTOR Absolute = SecurityDescriptor;
    PISECURITY_DESCRIPTOR_RELATIVE Relative = SecurityDescriptor;

    if (!(Absolute->Control & SE_SACL_PRESENT))
        return NULL;

    if (!(Absolute->Control & SE_SELF_RELATIVE))
        return Absolute->Sacl;

    return SepRelativeComponent(Relative, Relative->Sacl);
}
