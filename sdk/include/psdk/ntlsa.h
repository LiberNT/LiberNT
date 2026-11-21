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

typedef enum _LSA_CREDENTIAL_KEY_SOURCE_TYPE {
    eFromPrecomputed = 1,
    eFromClearPassword = 2,
    eFromNtOwf = 3
} LSA_CREDENTIAL_KEY_SOURCE_TYPE, *PLSA_CREDENTIAL_KEY_SOURCE_TYPE;

typedef ULONG POLICY_SYSTEM_ACCESS_MODE;
typedef ULONG * PPOLICY_SYSTEM_ACCESS_MODE;

#endif /* _NTLSA_ */
