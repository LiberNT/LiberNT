/*
 * ntlsa.h
 *
 * This file is part of the ReactOS PSDK package.
 *
 * Contributors:
 *   Created by Eric Kohl.
 *
 * THIS SOFTWARE IS NOT COPYRIGHTED
 *
 * This source code is offered for use in the public domain. You may
 * use, modify or distribute it freely.
 *
 * This code is distributed in the hope that it will be useful but
 * WITHOUT ANY WARRANTY. ALL WARRANTIES, EXPRESS OR IMPLIED ARE HEREBY
 * DISCLAIMED. This includes but is not limited to warranties of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 */

#ifndef _NTLSA_
#define _NTLSA_

#ifdef __cplusplus
extern "C" {
#endif

#define ACCOUNT_VIEW 1
#define ACCOUNT_ADJUST_PRIVILEGES 2
#define ACCOUNT_ADJUST_QUOTAS 4
#define ACCOUNT_ADJUST_SYSTEM_ACCESS 8

#define ACCOUNT_ALL_ACCESS (STANDARD_RIGHTS_REQUIRED | 15)
#define ACCOUNT_READ       (STANDARD_RIGHTS_READ | 1)
#define ACCOUNT_WRITE      (STANDARD_RIGHTS_WRITE | 14)
#define ACCOUNT_EXECUTE    (STANDARD_RIGHTS_EXECUTE)

#define SECRET_SET_VALUE 1
#define SECRET_QUERY_VALUE 2

#define SECRET_ALL_ACCESS  (STANDARD_RIGHTS_REQUIRED | 3)
#define SECRET_READ        (STANDARD_RIGHTS_READ | 2)
#define SECRET_WRITE       (STANDARD_RIGHTS_WRITE | 1)
#define SECRET_EXECUTE     (STANDARD_RIGHTS_EXECUTE)


/* System Access Flags */
#define SECURITY_ACCESS_INTERACTIVE_LOGON             0x00000001
#define SECURITY_ACCESS_NETWORK_LOGON                 0x00000002
#define SECURITY_ACCESS_BATCH_LOGON                   0x00000004
#define SECURITY_ACCESS_SERVICE_LOGON                 0x00000010
#define SECURITY_ACCESS_PROXY_LOGON                   0x00000020
#define SECURITY_ACCESS_DENY_INTERACTIVE_LOGON        0x00000040
#define SECURITY_ACCESS_DENY_NETWORK_LOGON            0x00000080
#define SECURITY_ACCESS_DENY_BATCH_LOGON              0x00000100
#define SECURITY_ACCESS_DENY_SERVICE_LOGON            0x00000200
#define SECURITY_ACCESS_REMOTE_INTERACTIVE_LOGON      0x00000400
#define SECURITY_ACCESS_DENY_REMOTE_INTERACTIVE_LOGON 0x00000800

#ifdef _NTIFS_INCLUDED_ // HACK to avoid redefinition from ntsecapi.h
typedef enum _POLICY_AUDIT_EVENT_TYPE
{
    AuditCategorySystem,
    AuditCategoryLogon,
    AuditCategoryObjectAccess,
    AuditCategoryPrivilegeUse,
    AuditCategoryDetailedTracking,
    AuditCategoryPolicyChange,
    AuditCategoryAccountManagement,
    AuditCategoryDirectoryServiceAccess,
    AuditCategoryAccountLogon
} POLICY_AUDIT_EVENT_TYPE, *PPOLICY_AUDIT_EVENT_TYPE;
#endif

#ifdef __cplusplus
}
#endif


#ifndef LSA_CALL_LICENSE_SERVER
#define LSA_CALL_LICENSE_SERVER 0x80000000
#endif
#ifndef LSA_GLOBAL_SECRET_PREFIX_LENGTH
#define LSA_GLOBAL_SECRET_PREFIX_LENGTH 2
#endif
#ifndef LSA_LOCAL_SECRET_PREFIX_LENGTH
#define LSA_LOCAL_SECRET_PREFIX_LENGTH 2
#endif
#ifndef LSA_LOOKUP_DISALLOW_CONNECTED_ACCOUNT_INTERNET_SID
#define LSA_LOOKUP_DISALLOW_CONNECTED_ACCOUNT_INTERNET_SID 0x80000000
#endif
#ifndef LSA_LOOKUP_ISOLATED_AS_LOCAL
#define LSA_LOOKUP_ISOLATED_AS_LOCAL 0x80000000
#endif
#ifndef LSA_LOOKUP_PREFER_EXTERNAL_SID
#define LSA_LOOKUP_PREFER_EXTERNAL_SID 0x40000000
#endif
#ifndef LSA_LOOKUP_PREFER_INTERNET_NAMES
#define LSA_LOOKUP_PREFER_INTERNET_NAMES 0x40000000
#endif
#ifndef LSA_MAXIMUM_ENUMERATION_LENGTH
#define LSA_MAXIMUM_ENUMERATION_LENGTH 32000
#endif
#ifndef LSA_MAXIMUM_SID_COUNT
#define LSA_MAXIMUM_SID_COUNT 0x00000100L
#endif
#ifndef LSA_SECRET_MAXIMUM_COUNT
#define LSA_SECRET_MAXIMUM_COUNT 0x00001000L
#endif
#ifndef LSA_SECRET_MAXIMUM_LENGTH
#define LSA_SECRET_MAXIMUM_LENGTH 0x00000200L
#endif
#ifndef MAX_USER_RECORDS
#define MAX_USER_RECORDS 1000
#endif
#ifndef NEGOTIATE_ALLOW_NTLM
#define NEGOTIATE_ALLOW_NTLM 0x10000000
#endif
#ifndef NEGOTIATE_NEG_NTLM
#define NEGOTIATE_NEG_NTLM 0x20000000
#endif
#ifndef POLICY_MODE_COUNT
#define POLICY_MODE_COUNT 11
#endif
#ifndef SE_ADT_POLICY_AUDIT_EVENT_TYPE_EX_BEGIN
#define SE_ADT_POLICY_AUDIT_EVENT_TYPE_EX_BEGIN 100
#endif
#ifndef TRUSTED_QUERY_AUTH
#define TRUSTED_QUERY_AUTH 0x00000040L
#endif
#ifndef TRUSTED_QUERY_CONTROLLERS
#define TRUSTED_QUERY_CONTROLLERS 0x00000002L
#endif
#ifndef TRUSTED_QUERY_DOMAIN_NAME
#define TRUSTED_QUERY_DOMAIN_NAME 0x00000001L
#endif
#ifndef TRUSTED_QUERY_POSIX
#define TRUSTED_QUERY_POSIX 0x00000008L
#endif
#ifndef TRUSTED_SET_AUTH
#define TRUSTED_SET_AUTH 0x00000020L
#endif
#ifndef TRUSTED_SET_CONTROLLERS
#define TRUSTED_SET_CONTROLLERS 0x00000004L
#endif
#ifndef TRUSTED_SET_POSIX
#define TRUSTED_SET_POSIX 0x00000010L
#endif
#ifndef versionbyte
#define versionbyte 0x01
#endif
#ifndef versionbyte_length
#define versionbyte_length 1
#endif

typedef enum _LSA_CREDENTIAL_KEY_SOURCE_TYPE {
    eFromPrecomputed = 1,
    eFromClearPassword = 2,
    eFromNtOwf = 3
} LSA_CREDENTIAL_KEY_SOURCE_TYPE, *PLSA_CREDENTIAL_KEY_SOURCE_TYPE;

typedef enum _POLICY_AUDIT_EVENT_TYPE_EX {
    iSystem_SecurityStateChange = 100,
    iSystem_SecuritySubsystemExtension = 101,
    iSystem_Integrity = 102,
    iSystem_IPSecDriverEvents = 103,
    iSystem_Others = 104,
    iLogon_Logon = 105,
    iLogon_Logoff = 106,
    iLogon_AccountLockout = 107,
    iLogon_IPSecMainMode = 108,
    iLogon_SpecialLogon = 109,
    iLogon_IPSecQuickMode = 110,
    iLogon_IPSecUsermode = 111,
    iLogon_Others = 112,
    iLogon_NPS = 113,
    iLogon_Claims = 114,
    iLogon_Groups = 115,
    iLogon_AccessRights = 116,
    iObjectAccess_FileSystem = 117,
    iObjectAccess_Registry = 118,
    iObjectAccess_Kernel = 119,
    iObjectAccess_Sam = 120,
    iObjectAccess_Other = 121,
    iObjectAccess_CertificationAuthority = 122,
    iObjectAccess_ApplicationGenerated = 123,
    iObjectAccess_HandleBasedAudits = 124,
    iObjectAccess_Share = 125,
    iObjectAccess_FirewallPacketDrops = 126,
    iObjectAccess_FirewallConnection = 127,
    iObjectAccess_DetailedFileShare = 128,
    iObjectAccess_RemovableStorage = 129,
    iObjectAccess_CbacStaging = 130,
    iPrivilegeUse_Sensitive = 131,
    iPrivilegeUse_NonSensitive = 132,
    iPrivilegeUse_Others = 133,
    iDetailedTracking_ProcessCreation = 134,
    iDetailedTracking_ProcessTermination = 135,
    iDetailedTracking_DpapiActivity = 136,
    iDetailedTracking_RpcCall = 137,
    iDetailedTracking_PnpActivity = 138,
    iDetailedTracking_TokenRightAdjusted = 139,
    iPolicyChange_AuditPolicy = 140,
    iPolicyChange_AuthenticationPolicy = 141,
    iPolicyChange_AuthorizationPolicy = 142,
    iPolicyChange_MpsscvRulePolicy = 143,
    iPolicyChange_WfpIPSecPolicy = 144,
    iPolicyChange_Others = 145,
    iAccountManagement_UserAccount = 146,
    iAccountManagement_ComputerAccount = 147,
    iAccountManagement_SecurityGroup = 148,
    iAccountManagement_DistributionGroup = 149,
    iAccountManagement_ApplicationGroup = 150,
    iAccountManagement_Others = 151,
    iDSAccess_DSAccess = 152,
    iDSAccess_AdAuditChanges = 153,
    iDS_Replication = 154,
    iDS_DetailedReplication = 155,
    iAccountLogon_CredentialValidation = 156,
    iAccountLogon_Kerberos = 157,
    iAccountLogon_Others = 158,
    iAccountLogon_KerbCredentialValidation = 159,
    iUnknownSubCategory = 0x3E7
} POLICY_AUDIT_EVENT_TYPE_EX, *PPOLICY_AUDIT_EVENT_TYPE_EX;

typedef ULONG POLICY_SYSTEM_ACCESS_MODE;
typedef ULONG * PPOLICY_SYSTEM_ACCESS_MODE;

typedef struct _LSA_ADT_SID_LIST_ENTRY {
  ULONG Flags;
  PSID Sid;
} LSA_ADT_SID_LIST_ENTRY, *PLSA_ADT_SID_LIST_ENTRY;

typedef struct _LSA_ADT_SID_LIST {
  ULONG cSids;
  PLSA_ADT_SID_LIST_ENTRY Sids;
} LSA_ADT_SID_LIST, *PLSA_ADT_SID_LIST;

typedef struct _LSA_AGENT_LOGON_CREDENTIAL {
  ULONG Size;
  PVOID Credential;
} LSA_AGENT_LOGON_CREDENTIAL, *PLSA_AGENT_LOGON_CREDENTIAL;

typedef struct _LSA_LOCAL_ACCESSRIGHT_ASSIGNMENT {
  PSID AccountSid;
  ULONG LocalSystemAccess;
} LSA_LOCAL_ACCESSRIGHT_ASSIGNMENT, *PLSA_LOCAL_ACCESSRIGHT_ASSIGNMENT;

typedef struct _LSA_LOCAL_ACCESSRIGHT_ASSIGNMENTS {
  ULONG cLocalAccessRightAssignments;
  PLSA_LOCAL_ACCESSRIGHT_ASSIGNMENT pLocalAccessRightAssignments;
} LSA_LOCAL_ACCESSRIGHT_ASSIGNMENTS, *PLSA_LOCAL_ACCESSRIGHT_ASSIGNMENTS;

typedef struct _NEGOTIATE_CALLER_NAME_RESPONSE_WOW {
  ULONG MessageType;
  ULONG CallerName;
} NEGOTIATE_CALLER_NAME_RESPONSE_WOW, *PNEGOTIATE_CALLER_NAME_RESPONSE_WOW;

typedef struct _NEGOTIATE_PACKAGE_PREFIX_WOW {
  ULONG PackageId;
  ULONG PackageDataA;
  ULONG PackageDataW;
  ULONG PrefixLen;
  UCHAR Prefix[32];
} NEGOTIATE_PACKAGE_PREFIX_WOW, *PNEGOTIATE_PACKAGE_PREFIX_WOW;

#define LSA_ADT_LEGACY_SECURITY_SOURCE_NAME L"Security"
#define LSA_ADT_SECURITY_SOURCE_NAME 0
#define LSA_AP_NAME_CALL_PACKAGE "LsaApCallPackage\0"
#define LSA_AP_NAME_CALL_PACKAGE_PASSTHROUGH "LsaApCallPackagePassthrough\0"
#define LSA_AP_NAME_CALL_PACKAGE_UNTRUSTED "LsaApCallPackageUntrusted\0"
#define LSA_AP_NAME_INITIALIZE_PACKAGE 0
#define LSA_AP_NAME_LOGON_TERMINATED "LsaApLogonTerminated\0"
#define LSA_AP_NAME_LOGON_USER "LsaApLogonUser\0"
#define LSA_AP_NAME_LOGON_USER_EX "LsaApLogonUserEx\0"
#define LSA_GLOBAL_SECRET_PREFIX 0
#define LSA_LOCAL_SECRET_PREFIX 0
#define LSA_MACHINE_SECRET_PREFIX 0
#define LSA_MACHINE_SECRET_PREFIX_LENGTH ((sizeof (LSA_MACHINE_SECRET_PREFIX) - sizeof (WCHAR)) / sizeof (WCHAR))
#define LSAD_AES256_ENC_KEY_STRING "Microsoft LSAD encryption key AEAD-AES-256-CBC-HMAC-SHA512 16"
#define LSAD_AES256_ENC_KEY_STRING_LENGTH sizeof (LSAD_AES256_ENC_KEY_STRING)
#define LSAD_AES256_MAC_KEY_STRING "Microsoft LSAD MAC key AEAD-AES-256-CBC-HMAC-SHA512 16"
#define LSAD_AES256_MAC_KEY_STRING_LENGTH 0x37
#define LSAD_AES_256_ALG 0
#define MAX_LOCAL_ACCESSRIGHT_ASSIGNMENTS 0x20000
#define POLICY_MODE_ALL 0xFF7
#define POLICY_MODE_ALL_NT4 0x37
#define POLICY_MODE_BATCH 4
#define POLICY_MODE_DENY_BATCH 0x100
#define POLICY_MODE_DENY_INTERACTIVE 0x40
#define POLICY_MODE_DENY_NETWORK 0x80
#define POLICY_MODE_DENY_REMOTE_INTERACTIVE 0x800
#define POLICY_MODE_DENY_SERVICE 0x200
#define POLICY_MODE_INTERACTIVE 1
#define POLICY_MODE_NETWORK 2
#define POLICY_MODE_PROXY 0x20
#define POLICY_MODE_REMOTE_INTERACTIVE 0x400
#define POLICY_MODE_SERVICE 0x10
#define TRUSTED_ALL_ACCESS 0xF007F
#define TRUSTED_EXECUTE 0x2000A
#define TRUSTED_READ 0x20001
#define TRUSTED_WRITE 0x20034

struct _LSA_ADT_STRING_LIST_ENTRY
{
    ULONG Flags;
    int String;
};

struct _LSA_AGENT_ACCOUNT_INFO
{
    PSID AgentSid;
    int OwningAppPackage;
    int AgentName;
    int UserName;
};

struct _LSA_DISPATCH_TABLE
{
    PLSA_CREATE_LOGON_SESSION CreateLogonSession;
    PLSA_DELETE_LOGON_SESSION DeleteLogonSession;
    PLSA_ADD_CREDENTIAL AddCredential;
    PLSA_GET_CREDENTIALS GetCredentials;
    PLSA_DELETE_CREDENTIAL DeleteCredential;
    PLSA_ALLOCATE_LSA_HEAP AllocateLsaHeap;
    PLSA_FREE_LSA_HEAP FreeLsaHeap;
    PLSA_ALLOCATE_CLIENT_BUFFER AllocateClientBuffer;
    PLSA_FREE_CLIENT_BUFFER FreeClientBuffer;
    PLSA_COPY_TO_CLIENT_BUFFER CopyToClientBuffer;
    PLSA_COPY_FROM_CLIENT_BUFFER CopyFromClientBuffer;
};

struct _LSA_TOKEN_INFORMATION_V3
{
    LARGE_INTEGER ExpirationTime;
    TOKEN_USER User;
    PTOKEN_GROUPS Groups;
    TOKEN_PRIMARY_GROUP PrimaryGroup;
    PTOKEN_PRIVILEGES Privileges;
    TOKEN_OWNER Owner;
    TOKEN_DEFAULT_DACL DefaultDacl;
    TOKEN_USER_CLAIMS UserClaims;
    TOKEN_DEVICE_CLAIMS DeviceClaims;
    PTOKEN_GROUPS DeviceGroups;
};

struct _NEGOTIATE_PACKAGE_NAMES
{
    ULONG NamesCount;
    int Names[1];
};

typedef NTSTATUS WINAPI LSA_ADD_CREDENTIAL(PLUID, ULONG, PLSA_STRING, PLSA_STRING);

typedef struct _LSA_ADT_STRING_LIST_ENTRY LSA_ADT_STRING_LIST_ENTRY;

typedef struct _LSA_AGENT_ACCOUNT_INFO LSA_AGENT_ACCOUNT_INFO;

struct _LSA_AGENT_ACCOUNT_LIST
{
    ULONG Count;
    LSA_AGENT_ACCOUNT_INFO *Agents;
};

typedef struct _LSA_AGENT_ACCOUNT_LIST LSA_AGENT_ACCOUNT_LIST;

typedef NTSTATUS WINAPI LSA_ALLOCATE_CLIENT_BUFFER(PLSA_CLIENT_REQUEST, ULONG, PVOID *);

typedef PVOID WINAPI LSA_ALLOCATE_LSA_HEAP(ULONG);

typedef PVOID WINAPI LSA_ALLOCATE_PRIVATE_HEAP(SIZE_T);

typedef NTSTATUS WINAPI LSA_AP_CALL_PACKAGE(PLSA_CLIENT_REQUEST, PVOID, PVOID, ULONG, PVOID *, PULONG, PNTSTATUS);

typedef NTSTATUS WINAPI LSA_AP_CALL_PACKAGE_PASSTHROUGH(PLSA_CLIENT_REQUEST, PVOID, PVOID, ULONG, PVOID *, PULONG, PNTSTATUS);

typedef LSA_AP_CALL_PACKAGE LSA_AP_CALL_PACKAGE_UNTRUSTED;

typedef NTSTATUS WINAPI LSA_AP_INITIALIZE_PACKAGE(ULONG, PLSA_DISPATCH_TABLE, PLSA_STRING, PLSA_STRING, PLSA_STRING *);

typedef void WINAPI LSA_AP_LOGON_TERMINATED(PLUID);

typedef NTSTATUS WINAPI LSA_AP_LOGON_USER(PLSA_CLIENT_REQUEST, SECURITY_LOGON_TYPE, PVOID, PVOID, ULONG, PVOID *, PULONG, PLUID, PNTSTATUS, PLSA_TOKEN_INFORMATION_TYPE, PVOID *, PLSA_UNICODE_STRING *, PLSA_UNICODE_STRING *);

typedef NTSTATUS WINAPI LSA_AP_LOGON_USER_EX(PLSA_CLIENT_REQUEST, SECURITY_LOGON_TYPE, PVOID, PVOID, ULONG, PVOID *, PULONG, PLUID, PNTSTATUS, PLSA_TOKEN_INFORMATION_TYPE, PVOID *, int *, int *, int *);

typedef NTSTATUS WINAPI LSA_COPY_FROM_CLIENT_BUFFER(PLSA_CLIENT_REQUEST, ULONG, PVOID, PVOID);

typedef NTSTATUS WINAPI LSA_COPY_TO_CLIENT_BUFFER(PLSA_CLIENT_REQUEST, ULONG, PVOID, PVOID);

typedef NTSTATUS WINAPI LSA_CREATE_LOGON_SESSION(PLUID);

typedef NTSTATUS WINAPI LSA_DELETE_CREDENTIAL(PLUID, ULONG, PLSA_STRING);

typedef NTSTATUS WINAPI LSA_DELETE_LOGON_SESSION(PLUID);

typedef NTSTATUS WINAPI LSA_FREE_CLIENT_BUFFER(PLSA_CLIENT_REQUEST, PVOID);

typedef void WINAPI LSA_FREE_LSA_HEAP(PVOID);

typedef void WINAPI LSA_FREE_PRIVATE_HEAP(PVOID);

typedef NTSTATUS WINAPI LSA_GET_CREDENTIALS(PLUID, ULONG, PULONG, BOOLEAN, PLSA_STRING, PULONG, PLSA_STRING);

typedef struct _LSA_TOKEN_INFORMATION_V3 LSA_TOKEN_INFORMATION_V3;

typedef struct _NEGOTIATE_PACKAGE_NAMES NEGOTIATE_PACKAGE_NAMES;

typedef NTSTATUS (WINAPI *PFN_LOOKUP_AUTH_PKG)(HANDLE, PLSA_STRING, PULONG);

typedef NTSTATUS (WINAPI *PFN_LSA_CALL_AUTH_PKG)(HANDLE, ULONG, PVOID, ULONG, PVOID *, PULONG, PNTSTATUS);

typedef NTSTATUS (WINAPI *PFN_LSA_DEREGISTER_PROC)(HANDLE);

typedef NTSTATUS (WINAPI *PFN_LSA_FREE_BUFFER)(PVOID);

typedef NTSTATUS (WINAPI *PFN_LSA_LOGON_SMA)(LUID, PHANDLE);

typedef NTSTATUS (WINAPI *PFN_LSA_LOGON_USER)(HANDLE, PLSA_STRING, SECURITY_LOGON_TYPE, ULONG, PVOID, ULONG, PTOKEN_GROUPS, PTOKEN_SOURCE, PVOID *, PULONG, PLUID, PHANDLE, PQUOTA_LIMITS, PNTSTATUS);

typedef NTSTATUS (WINAPI *PFN_LSA_REGISTER_PROC)(PLSA_STRING, PHANDLE, PLSA_OPERATIONAL_MODE);

struct _LSA_AUTH_CALLBACKS
{
    PFN_LSA_CALL_AUTH_PKG LsaCallAuthPkgFn;
    PFN_LSA_DEREGISTER_PROC LsaDeregisterProcFn;
    PFN_LSA_FREE_BUFFER LsaFreeReturnBufferFn;
    PFN_LSA_LOGON_USER LsaLogonUserFn;
    PFN_LOOKUP_AUTH_PKG LsaLookupAuthPkgFn;
    PFN_LSA_REGISTER_PROC LsaRegisterProcFn;
    PFN_LSA_LOGON_SMA LsaLogonSystemManagedAdminFn;
};

typedef struct _LSA_AUTH_CALLBACKS LSA_AUTH_CALLBACKS;

typedef const LSA_AUTH_CALLBACKS *PCLSA_AUTH_CALLBACKS;

typedef struct _LSA_ADT_STRING_LIST_ENTRY *PLSA_ADT_STRING_LIST_ENTRY;

struct _LSA_ADT_STRING_LIST
{
    ULONG cStrings;
    PLSA_ADT_STRING_LIST_ENTRY Strings;
};

typedef struct _LSA_ADT_STRING_LIST LSA_ADT_STRING_LIST;

typedef struct _LSA_ADT_STRING_LIST *PLSA_ADT_STRING_LIST;

typedef struct _LSA_AGENT_ACCOUNT_INFO *PLSA_AGENT_ACCOUNT_INFO;

typedef struct _LSA_AGENT_ACCOUNT_LIST *PLSA_AGENT_ACCOUNT_LIST;

typedef LSA_ALLOCATE_PRIVATE_HEAP *PLSA_ALLOCATE_PRIVATE_HEAP;

typedef struct _LSA_AUTH_CALLBACKS *PLSA_AUTH_CALLBACKS;

typedef LSA_FREE_PRIVATE_HEAP *PLSA_FREE_PRIVATE_HEAP;

typedef struct _LSA_TOKEN_INFORMATION_V3 *PLSA_TOKEN_INFORMATION_V3;

typedef struct _NEGOTIATE_PACKAGE_NAMES *PNEGOTIATE_PACKAGE_NAMES;

#ifdef __cplusplus
extern "C" {
#endif

/* TODO: LiberNT does not implement the functions below yet */
NTSTATUS WINAPI LsaChangePassword(PLSA_UNICODE_STRING ServerName, PLSA_UNICODE_STRING DomainName, PLSA_UNICODE_STRING AccountName, PLSA_UNICODE_STRING OldPassword, PLSA_UNICODE_STRING NewPassword);
NTSTATUS WINAPI LsaCreateAgentAccount(PSID OwningUser, int OwningAppPackage, int AgentName);
NTSTATUS WINAPI LsaDeleteAgentAccount(PSID OwningUser, int OwningAppPackage, int AgentName);
NTSTATUS WINAPI LsaEnumerateAgentAccounts(PSID OwningUser, PLSA_AGENT_ACCOUNT_LIST AgentList);
NTSTATUS WINAPI LsaGetAgentOwner(PSID AgentSid, PSID *OwnerSid);
NTSTATUS LsaInvokeTrustScanner(LSA_HANDLE PolicyHandle, LPWSTR DomainName, ULONG Flags, LPWSTR CompletionEvent);
NTSTATUS WINAPI LsaIsCredentialGuardRunning(PBOOLEAN IsCredentialGuardRunning);
NTSTATUS WINAPI LsaPurgeLocalSystemAccessTable(void);
NTSTATUS WINAPI LsaQueryLocalSystemAccess(PSID AccountSid, PULONG SystemAccessLocal);
NTSTATUS WINAPI LsaQueryLocalSystemAccessAll(PLSA_LOCAL_ACCESSRIGHT_ASSIGNMENTS *ppLocalAccessRightAssignments);
NTSTATUS WINAPI LsaRetrieveAgentLogonCredential(PSID OwningUser, int OwningAppPackage, int AgentName, PLSA_AGENT_LOGON_CREDENTIAL Credential);
NTSTATUS WINAPI LsaRetrieveCloudAgentLogonCredential(PSID OwningUser, int OwningAppPackage, int AgentName, PVOID WamToken, ULONG WamTokenSize, PLSA_AGENT_LOGON_CREDENTIAL Credential);
NTSTATUS WINAPI LsaSetLocalSystemAccess(PLSA_LOCAL_ACCESSRIGHT_ASSIGNMENTS pLocalAccessRightAssignments);
NTSTATUS WINAPI LsaSetPolicyReplicationHandle(PLSA_HANDLE PolicyHandle);
NTSTATUS SeciIsProtectedUser(PBOOLEAN ProtectedUser);

#ifdef __cplusplus
}
#endif

struct _LSA_TOKEN_INFORMATION_NULL
{
    LARGE_INTEGER ExpirationTime;
    PTOKEN_GROUPS Groups;
};

struct _LSA_TOKEN_INFORMATION_V1
{
    LARGE_INTEGER ExpirationTime;
    TOKEN_USER User;
    PTOKEN_GROUPS Groups;
    TOKEN_PRIMARY_GROUP PrimaryGroup;
    PTOKEN_PRIVILEGES Privileges;
    TOKEN_OWNER Owner;
    TOKEN_DEFAULT_DACL DefaultDacl;
};

struct _LSA_USER_REGISTRATION_INFO
{
    LSA_UNICODE_STRING Sid;
    LSA_UNICODE_STRING DeviceId;
    LSA_UNICODE_STRING Username;
    LSA_UNICODE_STRING Thumbprint;
    LARGE_INTEGER RegistrationTime;
};

typedef struct _LSA_REGISTRATION_INFO LSA_REGISTRATION_INFO;

typedef struct _LSA_TOKEN_INFORMATION_NULL LSA_TOKEN_INFORMATION_NULL;

typedef struct _LSA_TOKEN_INFORMATION_V1 LSA_TOKEN_INFORMATION_V1;

typedef LSA_TOKEN_INFORMATION_V1 LSA_TOKEN_INFORMATION_V2;

typedef struct _LSA_USER_REGISTRATION_INFO LSA_USER_REGISTRATION_INFO;

typedef struct _LSA_REGISTRATION_INFO *PLSA_REGISTRATION_INFO;

typedef struct _LSA_TOKEN_INFORMATION_NULL *PLSA_TOKEN_INFORMATION_NULL;

typedef struct _LSA_TOKEN_INFORMATION_V1 *PLSA_TOKEN_INFORMATION_V1;

typedef LSA_TOKEN_INFORMATION_V1 *PLSA_TOKEN_INFORMATION_V2;

typedef struct _LSA_USER_REGISTRATION_INFO *PLSA_USER_REGISTRATION_INFO;

struct _LSA_REGISTRATION_INFO
{
    ULONG RegisteredCount;
    PLSA_USER_REGISTRATION_INFO *UserRegistrationInfo;
};

#ifdef __cplusplus
extern "C" {
#endif

NTSTATUS WINAPI LsaQueryInfoTrustedDomain(LSA_HANDLE TrustedDomainHandle, TRUSTED_INFORMATION_CLASS InformationClass, PVOID *Buffer);

/* TODO: LiberNT does not implement the functions below yet */
NTSTATUS WINAPI LsaGetDeviceRegistrationInfo(PLSA_REGISTRATION_INFO *RegistrationInfo);

#ifdef __cplusplus
}
#endif

#endif /* _NTLSA_ */
