/*
 * COPYRIGHT:   Copyright Eric Kohl
 *              Copyright 2022-2023 George Bișoc <george.bisoc@reactos.org>
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








BOOLEAN
NTAPI
    _In_ PSECURITY_DESCRIPTOR SecurityDescriptor,




BOOLEAN
NTAPI

VOID
NTAPI

VOID
NTAPI

NTAPI

NTAPI

NTAPI

NTSTATUS
NTAPI

VOID
NTAPI

NTAPI

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
    _In_ KPROCESSOR_MODE PreviousMode);

NTSTATUS
NTAPI

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

VOID
NTAPI
SeReleaseLuidAndAttributesArray(
    _In_ PLUID_AND_ATTRIBUTES Privilege,
    _In_ KPROCESSOR_MODE PreviousMode,
    _In_ BOOLEAN CaptureIfKernel);

CODE_SEG("INIT")
BOOLEAN
NTAPI

NTSTATUS
NTAPI
    _In_ KPROCESSOR_MODE AccessMode,
    _In_ POOL_TYPE PoolType,
    _In_ BOOLEAN CaptureIfKernel,

VOID
NTAPI
    _In_ KPROCESSOR_MODE AccessMode,
    _In_ BOOLEAN CaptureIfKernel);

NTAPI

NTAPI

NTAPI

NTSTATUS
NTAPI

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

CODE_SEG("INIT")
BOOLEAN
NTAPI

VOID



NTSTATUS

NTAPI

NTAPI

NTSTATUS

NTAPI

NTAPI

NTSTATUS

NTSTATUS
NTAPI

VOID
NTAPI

NTSTATUS

NTSTATUS
NTAPI

NTSTATUS
NTAPI

NTSTATUS
NTAPI

CODE_SEG("INIT")
NTAPI

NTAPI

NTAPI

NTAPI



NTAPI

NTSTATUS

NTAPI

NTAPI

VOID
NTAPI

VOID
NTAPI

VOID
NTAPI

VOID
NTAPI
    _In_ PEPROCESS Process,

NTSTATUS
NTAPI

NTAPI


BOOLEAN

NTSTATUS
SeCaptureObjectTypeList(
    _In_reads_opt_(ObjectTypeListLength) POBJECT_TYPE_LIST ObjectTypeList,
    _In_ ULONG ObjectTypeListLength,
    _In_ KPROCESSOR_MODE PreviousMode,
    _Out_ POBJECT_TYPE_LIST_INTERNAL *CapturedObjectTypeList);


NTSTATUS




