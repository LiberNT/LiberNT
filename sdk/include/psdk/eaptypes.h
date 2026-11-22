#ifndef EAPTYPES_H
#define EAPTYPES_H

typedef struct _EAP_TYPE
{
    BYTE type;
    DWORD dwVendorId;
    DWORD dwVendorType;
} EAP_TYPE, *PEAP_TYPE;

typedef struct _EAP_METHOD_TYPE
{
    EAP_TYPE eapType;
    DWORD dwAuthorId;
} EAP_METHOD_TYPE, *PEAP_METHOD_TYPE;

#ifndef CERTIFICATE_HASH_LENGTH
#define CERTIFICATE_HASH_LENGTH 20
#endif
#ifndef EAPHOST_METHOD_API_VERSION
#define EAPHOST_METHOD_API_VERSION 1
#endif
#ifndef EAPHOST_PEER_API_VERSION
#define EAPHOST_PEER_API_VERSION 1
#endif
#ifndef EAP_CONFIG_INPUT_FIELD_PROPS_DEFAULT
#define EAP_CONFIG_INPUT_FIELD_PROPS_DEFAULT 0X00000000
#endif
#ifndef EAP_CONFIG_INPUT_FIELD_PROPS_NON_DISPLAYABLE
#define EAP_CONFIG_INPUT_FIELD_PROPS_NON_DISPLAYABLE 0X00000001
#endif
#ifndef EAP_CONFIG_INPUT_FIELD_PROPS_NON_PERSIST
#define EAP_CONFIG_INPUT_FIELD_PROPS_NON_PERSIST 0X00000002
#endif
#ifndef EAP_CREDENTIAL_VERSION
#define EAP_CREDENTIAL_VERSION 1
#endif
#ifndef EAP_FLAG_CONFG_READONLY
#define EAP_FLAG_CONFG_READONLY 0x00080000
#endif
#ifndef EAP_FLAG_DISABLE_SESSION_RESUMPTION
#define EAP_FLAG_DISABLE_SESSION_RESUMPTION 0x04000000
#endif
#ifndef EAP_FLAG_FULL_AUTH
#define EAP_FLAG_FULL_AUTH 0x00001000
#endif
#ifndef EAP_FLAG_GUEST_ACCESS
#define EAP_FLAG_GUEST_ACCESS 0x00000040
#endif
#ifndef EAP_FLAG_LOGON
#define EAP_FLAG_LOGON 0x00000004
#endif
#ifndef EAP_FLAG_MACHINE_AUTH
#define EAP_FLAG_MACHINE_AUTH 0x00000020
#endif
#ifndef EAP_FLAG_NON_INTERACTIVE
#define EAP_FLAG_NON_INTERACTIVE 0x00000002
#endif
#ifndef EAP_FLAG_ONLY_EAP_TLS
#define EAP_FLAG_ONLY_EAP_TLS 0x01000000
#endif
#ifndef EAP_FLAG_PREFER_ALT_CREDENTIALS
#define EAP_FLAG_PREFER_ALT_CREDENTIALS 0x00002000
#endif
#ifndef EAP_FLAG_PREVIEW
#define EAP_FLAG_PREVIEW 0x00000008
#endif
#ifndef EAP_FLAG_PRE_LOGON
#define EAP_FLAG_PRE_LOGON 0x00020000
#endif
#ifndef EAP_FLAG_RESUME_FROM_HIBERNATE
#define EAP_FLAG_RESUME_FROM_HIBERNATE 0x00000200
#endif
#ifndef EAP_FLAG_Reserved1
#define EAP_FLAG_Reserved1 0x00000001
#endif
#ifndef EAP_FLAG_Reserved2
#define EAP_FLAG_Reserved2 0x00000010
#endif
#ifndef EAP_FLAG_Reserved3
#define EAP_FLAG_Reserved3 0x00000080
#endif
#ifndef EAP_FLAG_Reserved4
#define EAP_FLAG_Reserved4 0x00000100
#endif
#ifndef EAP_FLAG_Reserved5
#define EAP_FLAG_Reserved5 0x00000400
#endif
#ifndef EAP_FLAG_Reserved6
#define EAP_FLAG_Reserved6 0x00000800
#endif
#ifndef EAP_FLAG_Reserved7
#define EAP_FLAG_Reserved7 0x00004000
#endif
#ifndef EAP_FLAG_Reserved8
#define EAP_FLAG_Reserved8 0x00100000
#endif
#ifndef EAP_FLAG_Reserved9
#define EAP_FLAG_Reserved9 0x00400000
#endif
#ifndef EAP_FLAG_SERVER_VALIDATION_REQUIRED
#define EAP_FLAG_SERVER_VALIDATION_REQUIRED 0x02000000
#endif
#ifndef EAP_FLAG_SUPRESS_UI
#define EAP_FLAG_SUPRESS_UI 0x00010000
#endif
#ifndef EAP_FLAG_USER_AUTH
#define EAP_FLAG_USER_AUTH 0x00040000
#endif
#ifndef EAP_FLAG_VPN
#define EAP_FLAG_VPN 0x00800000
#endif
#ifndef EAP_INTERACTIVE_UI_DATA_VERSION
#define EAP_INTERACTIVE_UI_DATA_VERSION 1
#endif
#ifndef EAP_PEER_FLAG_HEALTH_STATE_CHANGE
#define EAP_PEER_FLAG_HEALTH_STATE_CHANGE 0x00008000
#endif
#ifndef EAP_UI_INPUT_FIELD_PROPS_NON_PERSIST
#define EAP_UI_INPUT_FIELD_PROPS_NON_PERSIST 0X00000002
#endif
#ifndef EAP_UI_INPUT_FIELD_PROPS_READ_ONLY
#define EAP_UI_INPUT_FIELD_PROPS_READ_ONLY 0X00000004
#endif
#ifndef MAX_EAP_CONFIG_INPUT_FIELD_LENGTH
#define MAX_EAP_CONFIG_INPUT_FIELD_LENGTH 256
#endif
#ifndef MAX_EAP_CONFIG_INPUT_FIELD_VALUE_LENGTH
#define MAX_EAP_CONFIG_INPUT_FIELD_VALUE_LENGTH 1024
#endif
#ifndef eapPropCertifiedMethod
#define eapPropCertifiedMethod 0x00400000
#endif
#ifndef eapPropChannelBinding
#define eapPropChannelBinding 0x00010000
#endif
#ifndef eapPropCipherSuiteNegotiation
#define eapPropCipherSuiteNegotiation 0x00000001
#endif
#ifndef eapPropConfidentiality
#define eapPropConfidentiality 0x00000010
#endif
#ifndef eapPropCryptoBinding
#define eapPropCryptoBinding 0x00002000
#endif
#ifndef eapPropDictionaryAttackResistance
#define eapPropDictionaryAttackResistance 0x00000800
#endif
#ifndef eapPropFastReconnect
#define eapPropFastReconnect 0x00001000
#endif
#ifndef eapPropFragmentation
#define eapPropFragmentation 0x00008000
#endif
#ifndef eapPropHiddenMethod
#define eapPropHiddenMethod 0x00800000
#endif
#ifndef eapPropIdentityPrivacy
#define eapPropIdentityPrivacy 0x04000000
#endif
#ifndef eapPropIntegrity
#define eapPropIntegrity 0x00000004
#endif
#ifndef eapPropKeyDerivation
#define eapPropKeyDerivation 0x00000020
#endif
#ifndef eapPropKeyStrength1024
#define eapPropKeyStrength1024 0x00000400
#endif
#ifndef eapPropKeyStrength128
#define eapPropKeyStrength128 0x00000080
#endif
#ifndef eapPropKeyStrength256
#define eapPropKeyStrength256 0x00000100
#endif
#ifndef eapPropKeyStrength512
#define eapPropKeyStrength512 0x00000200
#endif
#ifndef eapPropKeyStrength64
#define eapPropKeyStrength64 0x00000040
#endif
#ifndef eapPropMachineAuth
#define eapPropMachineAuth 0x01000000
#endif
#ifndef eapPropMethodChaining
#define eapPropMethodChaining 0x08000000
#endif
#ifndef eapPropMppeEncryption
#define eapPropMppeEncryption 0x00080000
#endif
#ifndef eapPropMutualAuth
#define eapPropMutualAuth 0x00000002
#endif
#ifndef eapPropNap
#define eapPropNap 0x00020000
#endif
#ifndef eapPropReplayProtection
#define eapPropReplayProtection 0x00000008
#endif
#ifndef eapPropReserved
#define eapPropReserved 0x80000000
#endif
#ifndef eapPropSessionIndependence
#define eapPropSessionIndependence 0x00004000
#endif
#ifndef eapPropSharedStateEquivalence
#define eapPropSharedStateEquivalence 0x10000000
#endif
#ifndef eapPropStandalone
#define eapPropStandalone 0x00040000
#endif
#ifndef eapPropSupportsConfig
#define eapPropSupportsConfig 0x00200000
#endif
#ifndef eapPropTunnelMethod
#define eapPropTunnelMethod 0x00100000
#endif
#ifndef eapPropUserAuth
#define eapPropUserAuth 0x02000000
#endif

typedef enum _EAP_ATTRIBUTE_TYPE {
    eatMinimum = 0,
    eatUserName = 1,
    eatUserPassword = 2,
    eatMD5CHAPPassword = 3,
    eatNASIPAddress = 4,
    eatNASPort = 5,
    eatServiceType = 6,
    eatFramedProtocol = 7,
    eatFramedIPAddress = 8,
    eatFramedIPNetmask = 9,
    eatFramedRouting = 10,
    eatFilterId = 11,
    eatFramedMTU = 12,
    eatFramedCompression = 13,
    eatLoginIPHost = 14,
    eatLoginService = 15,
    eatLoginTCPPort = 16,
    eatUnassigned17 = 17,
    eatReplyMessage = 18,
    eatCallbackNumber = 19,
    eatCallbackId = 20,
    eatUnassigned21 = 21,
    eatFramedRoute = 22,
    eatFramedIPXNetwork = 23,
    eatState = 24,
    eatClass = 25,
    eatVendorSpecific = 26,
    eatSessionTimeout = 27,
    eatIdleTimeout = 28,
    eatTerminationAction = 29,
    eatCalledStationId = 30,
    eatCallingStationId = 31,
    eatNASIdentifier = 32,
    eatProxyState = 33,
    eatLoginLATService = 34,
    eatLoginLATNode = 35,
    eatLoginLATGroup = 36,
    eatFramedAppleTalkLink = 37,
    eatFramedAppleTalkNetwork = 38,
    eatFramedAppleTalkZone = 39,
    eatAcctStatusType = 40,
    eatAcctDelayTime = 41,
    eatAcctInputOctets = 42,
    eatAcctOutputOctets = 43,
    eatAcctSessionId = 44,
    eatAcctAuthentic = 45,
    eatAcctSessionTime = 46,
    eatAcctInputPackets = 47,
    eatAcctOutputPackets = 48,
    eatAcctTerminateCause = 49,
    eatAcctMultiSessionId = 50,
    eatAcctLinkCount = 51,
    eatAcctEventTimeStamp = 55,
    eatMD5CHAPChallenge = 60,
    eatNASPortType = 61,
    eatPortLimit = 62,
    eatLoginLATPort = 63,
    eatTunnelType = 64,
    eatTunnelMediumType = 65,
    eatTunnelClientEndpoint = 66,
    eatTunnelServerEndpoint = 67,
    eatARAPPassword = 70,
    eatARAPFeatures = 71,
    eatARAPZoneAccess = 72,
    eatARAPSecurity = 73,
    eatARAPSecurityData = 74,
    eatPasswordRetry = 75,
    eatPrompt = 76,
    eatConnectInfo = 77,
    eatConfigurationToken = 78,
    eatEAPMessage = 79,
    eatSignature = 80,
    eatARAPChallengeResponse = 84,
    eatAcctInterimInterval = 85,
    eatNASIPv6Address = 95,
    eatFramedInterfaceId = 96,
    eatFramedIPv6Prefix = 97,
    eatLoginIPv6Host = 98,
    eatFramedIPv6Route = 99,
    eatFramedIPv6Pool = 100,
    eatARAPGuestLogon = 0x1FA0,
    eatCertificateOID = 0x1FA1,
    eatEAPConfiguration = 0x1FA2,
    eatPEAPEmbeddedEAPTypeId = 0x1FA3,
    eatPEAPFastRoamedSession = 0x1FA4,
    eatFastRoamedSession = 0x1FA4,
    eatEAPTLV = 0x1FA6,
    eatCredentialsChanged = 0x1FA7,
    eatInnerEapMethodType = 0x1FA8,
    eatClearTextPassword = 0x1FAB,
    eatQuarantineSoH = 0x1FD6,
    eatCertificateThumbprint = 0x203A,
    eatPeerId = 0x2328,
    eatServerId = 0x2329,
    eatMethodId = 0x232A,
    eatEMSK = 0x232B,
    eatSessionId = 0x232C,
    eatReserved = (-1)
} EAP_ATTRIBUTE_TYPE, EapAttributeType;

typedef enum _EAP_CONFIG_INPUT_FIELD_TYPE {
    EapConfigInputUsername = 0,
    EapConfigInputPassword = 1,
    EapConfigInputNetworkUsername = 2,
    EapConfigInputNetworkPassword = 3,
    EapConfigInputPin = 4,
    EapConfigInputPSK = 5,
    EapConfigInputEdit = 6,
    EapConfigSmartCardUsername = 7,
    EapConfigSmartCardError = 8
} EAP_CONFIG_INPUT_FIELD_TYPE, *PEAP_CONFIG_INPUT_FIELD_TYPE;

typedef enum _EAP_INTERACTIVE_UI_DATA_TYPE {
    EapCredReq = 0,
    EapCredResp = 1,
    EapCredExpiryReq = 2,
    EapCredExpiryResp = 3,
    EapCredLogonReq = 4,
    EapCredLogonResp = 5
} EAP_INTERACTIVE_UI_DATA_TYPE;

typedef enum _EAP_METHOD_PROPERTY_TYPE {
    emptPropCipherSuiteNegotiation = 0,
    emptPropMutualAuth = 1,
    emptPropIntegrity = 2,
    emptPropReplayProtection = 3,
    emptPropConfidentiality = 4,
    emptPropKeyDerivation = 5,
    emptPropKeyStrength64 = 6,
    emptPropKeyStrength128 = 7,
    emptPropKeyStrength256 = 8,
    emptPropKeyStrength512 = 9,
    emptPropKeyStrength1024 = 10,
    emptPropDictionaryAttackResistance = 11,
    emptPropFastReconnect = 12,
    emptPropCryptoBinding = 13,
    emptPropSessionIndependence = 14,
    emptPropFragmentation = 15,
    emptPropChannelBinding = 16,
    emptPropNap = 17,
    emptPropStandalone = 18,
    emptPropMppeEncryption = 19,
    emptPropTunnelMethod = 20,
    emptPropSupportsConfig = 21,
    emptPropCertifiedMethod = 22,
    emptPropHiddenMethod = 23,
    emptPropMachineAuth = 24,
    emptPropUserAuth = 25,
    emptPropIdentityPrivacy = 26,
    emptPropMethodChaining = 27,
    emptPropSharedStateEquivalence = 28,
    emptLegacyMethodPropertyFlag = 31,
    emptPropVendorSpecific = 255
} EAP_METHOD_PROPERTY_TYPE;

typedef enum _EAP_METHOD_PROPERTY_VALUE_TYPE {
    empvtBool = 0,
    empvtDword = 1,
    empvtString = 2
} EAP_METHOD_PROPERTY_VALUE_TYPE;

typedef enum _EapCredentialType {
    EAP_EMPTY_CREDENTIAL = 0,
    EAP_USERNAME_PASSWORD_CREDENTIAL = 1,
    EAP_WINLOGON_CREDENTIAL = 2,
    EAP_CERTIFICATE_CREDENTIAL = 3,
    EAP_SIM_CREDENTIAL = 4
} EapCredentialType;

typedef DWORD EAP_SESSIONID;

typedef struct _EAP_ATTRIBUTE {
  EAP_ATTRIBUTE_TYPE eaType;
  DWORD dwLength;
  BYTE * pValue;
} EAP_ATTRIBUTE, EapAttribute;

typedef struct _EAP_ATTRIBUTES {
  DWORD dwNumberOfAttributes;
  EAP_ATTRIBUTE * pAttribs;
} EAP_ATTRIBUTES, EapAttributes;

typedef struct _EAP_CONFIG_INPUT_FIELD_DATA {
  DWORD dwSize;
  EAP_CONFIG_INPUT_FIELD_TYPE Type;
  DWORD dwFlagProps;
  LPWSTR pwszLabel;
  LPWSTR pwszData;
  DWORD dwMinDataLength;
  DWORD dwMaxDataLength;
} EAP_CONFIG_INPUT_FIELD_DATA, *PEAP_CONFIG_INPUT_FIELD_DATA;

typedef struct _EAP_CONFIG_INPUT_FIELD_ARRAY {
  DWORD dwVersion;
  DWORD dwNumberOfFields;
  EAP_CONFIG_INPUT_FIELD_DATA * pFields;
} EAP_CONFIG_INPUT_FIELD_ARRAY, *PEAP_CONFIG_INPUT_FIELD_ARRAY;

typedef struct _EAP_CRED_EXPIRY_REQ {
  EAP_CONFIG_INPUT_FIELD_ARRAY curCreds;
  EAP_CONFIG_INPUT_FIELD_ARRAY newCreds;
} EAP_CRED_EXPIRY_REQ, EAP_CRED_EXPIRY_RESP;

typedef struct _EAP_ERROR {
  DWORD dwWinError;
  EAP_METHOD_TYPE type;
  DWORD dwReasonCode;
  GUID rootCauseGuid;
  GUID repairGuid;
  GUID helpLinkGuid;
  LPWSTR pRootCauseString;
  LPWSTR pRepairString;
} EAP_ERROR;

typedef struct _EAP_METHOD_PROPERTY_VALUE_BOOL {
  DWORD length;
  BOOL value;
} EAP_METHOD_PROPERTY_VALUE_BOOL;

typedef struct _EAP_METHOD_PROPERTY_VALUE_DWORD {
  DWORD length;
  DWORD value;
} EAP_METHOD_PROPERTY_VALUE_DWORD;

typedef struct _EAP_METHOD_PROPERTY_VALUE_STRING {
  DWORD length;
  BYTE * value;
} EAP_METHOD_PROPERTY_VALUE_STRING;

typedef union _EAP_METHOD_PROPERTY_VALUE {
  EAP_METHOD_PROPERTY_VALUE_BOOL empvBool;
  EAP_METHOD_PROPERTY_VALUE_DWORD empvDword;
  EAP_METHOD_PROPERTY_VALUE_STRING empvString;
} EAP_METHOD_PROPERTY_VALUE;

typedef struct _EAP_METHOD_PROPERTY {
  EAP_METHOD_PROPERTY_TYPE eapMethodPropertyType;
  EAP_METHOD_PROPERTY_VALUE_TYPE eapMethodPropertyValueType;
  EAP_METHOD_PROPERTY_VALUE eapMethodPropertyValue;
} EAP_METHOD_PROPERTY;

typedef struct _EAP_METHOD_PROPERTY_ARRAY {
  DWORD dwNumberOfProperties;
  EAP_METHOD_PROPERTY * pMethodProperty;
} EAP_METHOD_PROPERTY_ARRAY;

typedef struct _EapCertificateCredential {
  BYTE certHash[20];
  LPWSTR password;
} EapCertificateCredential;

typedef struct _EAPHOST_IDENTITY_UI_PARAMS {
  EAP_METHOD_TYPE eapMethodType;
  DWORD dwFlags;
  DWORD dwSizeofConnectionData;
  BYTE * pConnectionData;
  DWORD dwSizeofUserData;
  BYTE * pUserData;
  DWORD dwSizeofUserDataOut;
  BYTE * pUserDataOut;
  LPWSTR pwszIdentity;
  DWORD dwError;
  EAP_ERROR * pEapError;
} EAPHOST_IDENTITY_UI_PARAMS;

typedef struct _EAPHOST_INTERACTIVE_UI_PARAMS {
  DWORD dwSizeofContextData;
  BYTE * pContextData;
  DWORD dwSizeofInteractiveUIData;
  BYTE * pInteractiveUIData;
  DWORD dwError;
  EAP_ERROR * pEapError;
} EAPHOST_INTERACTIVE_UI_PARAMS;

typedef struct _EapSimCredential {
  LPWSTR iccID;
} EapSimCredential;

typedef struct _EapUsernamePasswordCredential {
  LPWSTR username;
  LPWSTR password;
} EapUsernamePasswordCredential;

typedef union EapCredentialTypeData {
  EapUsernamePasswordCredential username_password;
  EapCertificateCredential certificate;
  EapSimCredential sim;
} EapCredentialTypeData;

typedef struct _EapCredential {
  EapCredentialType credType;
  EapCredentialTypeData credData;
} EapCredential;

#define EAP_UI_INPUT_FIELD_PROPS_DEFAULT 0
#define EAP_UI_INPUT_FIELD_PROPS_NON_DISPLAYABLE 1
#define EAP_VALUENAME_PROPERTIES L"Properties"

struct _EAP_METHOD_INFO
{
    EAP_METHOD_TYPE eaptype;
    LPWSTR pwszAuthorName;
    LPWSTR pwszFriendlyName;
    DWORD eapProperties;
    struct _EAP_METHOD_INFO *pInnerMethodInfo;
};

typedef EAP_CONFIG_INPUT_FIELD_ARRAY EAP_CRED_LOGON_REQ;

typedef EAP_CONFIG_INPUT_FIELD_ARRAY EAP_CRED_LOGON_RESP;

typedef EAP_CONFIG_INPUT_FIELD_ARRAY EAP_CRED_REQ;

typedef EAP_CONFIG_INPUT_FIELD_ARRAY EAP_CRED_RESP;

typedef struct _EAP_METHOD_INFO EAP_METHOD_INFO;

struct _EAP_METHOD_INFO_ARRAY
{
    DWORD dwNumberOfMethods;
    EAP_METHOD_INFO *pEapMethods;
};

typedef struct _EAP_METHOD_INFO_ARRAY EAP_METHOD_INFO_ARRAY;

#if (!(defined(__midl)))
union EAP_UI_DATA_FORMAT
{
    EAP_CRED_REQ *credData;
    EAP_CRED_EXPIRY_REQ *credExpiryData;
    EAP_CRED_LOGON_REQ *credLogonData;
};
#endif

struct _EAP_INTERACTIVE_UI_DATA
{
    DWORD dwVersion;
    DWORD dwSize;
    EAP_INTERACTIVE_UI_DATA_TYPE dwDataType;
    DWORD cbUiData;
    EAP_UI_DATA_FORMAT pbUiData;
};

typedef struct _EAP_INTERACTIVE_UI_DATA EAP_INTERACTIVE_UI_DATA;

struct _EAP_METHOD_INFO_ARRAY_EX
{
    DWORD dwNumberOfMethods;
    EAP_METHOD_INFO_EX *pEapMethods;
};

struct _EAP_METHOD_INFO_EX
{
    EAP_METHOD_TYPE eaptype;
    LPWSTR pwszAuthorName;
    LPWSTR pwszFriendlyName;
    DWORD eapProperties;
    struct _EAP_METHOD_INFO_ARRAY_EX *pInnerMethodInfoArray;
};

typedef struct _EAP_METHOD_INFO_ARRAY_EX EAP_METHOD_INFO_ARRAY_EX;

typedef struct _EAP_METHOD_INFO_EX EAP_METHOD_INFO_EX;

#endif
