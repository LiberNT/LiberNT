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

#endif
