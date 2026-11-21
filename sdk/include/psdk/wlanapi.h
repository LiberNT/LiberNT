#ifndef _WLANAPI_H
#define _WLANAPI_H

#include <l2cmn.h>
#include <windot11.h>
#include <eaptypes.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Defines */
#define WLAN_API_VERSION_1_0 0x00000001
#define WLAN_API_VERSION_2_0 0x00000002

#define WLAN_MAX_PHY_INDEX 64
#define WLAN_MAX_PHY_TYPE_NUMBER              8
#define WLAN_MAX_NAME_LENGTH 256

#define WLAN_AVAILABLE_NETWORK_INCLUDE_ALL_ADHOC_PROFILES         0x00000001
#define WLAN_AVAILABLE_NETWORK_INCLUDE_ALL_MANUAL_HIDDEN_PROFILES 0x00000002

#define WLAN_AVAILABLE_NETWORK_CONNECTED                          0x00000001
#define WLAN_AVAILABLE_NETWORK_HAS_PROFILE                        0x00000002
#define WLAN_AVAILABLE_NETWORK_CONSOLE_USER_PROFILE               0x00000004
#define WLAN_AVAILABLE_NETWORK_INTERWORKING_SUPPORTED             0x00000008
#define WLAN_AVAILABLE_NETWORK_HOTSPOT2_ENABLED                   0x00000010
#define WLAN_AVAILABLE_NETWORK_ANQP_SUPPORTED                     0x00000020
#define WLAN_AVAILABLE_NETWORK_HOTSPOT2_DOMAIN                    0x00000040
#define WLAN_AVAILABLE_NETWORK_HOTSPOT2_ROAMING                   0x00000080
#define WLAN_AVAILABLE_NETWORK_AUTO_CONNECT_FAILED                0x00000100

#define WLAN_READ_ACCESS    (STANDARD_RIGHTS_READ | FILE_READ_DATA)
#define WLAN_EXECUTE_ACCESS (STANDARD_RIGHTS_EXECUTE | FILE_EXECUTE | WLAN_READ_ACCESS)
#define WLAN_WRITE_ACCESS   (STANDARD_RIGHTS_WRITE | FILE_WRITE_DATA | DELETE | WRITE_DAC | WLAN_READ_ACCESS | WLAN_EXECUTE_ACCESS)

/* Enumerations */

#if defined(__midl) || defined(__WIDL__)
typedef [v1_enum] enum _DOT11_RADIO_STATE {
#else
typedef enum _DOT11_RADIO_STATE {
#endif
    dot11_radio_state_unknown = 0,
    dot11_radio_state_on,
    dot11_radio_state_off
} DOT11_RADIO_STATE; /* HACK: WIDL is broken, *PDOT11_RADIO_STATE; */

#if defined(__midl) || defined(__WIDL__)
typedef [v1_enum] enum _WLAN_OPCODE_VALUE_TYPE {
#else
typedef enum _WLAN_OPCODE_VALUE_TYPE {
#endif
    wlan_opcode_value_type_query_only = 0,
    wlan_opcode_value_type_set_by_group_policy,
    wlan_opcode_value_type_set_by_user,
    wlan_opcode_value_type_invalid
} WLAN_OPCODE_VALUE_TYPE; /* HACK: WIDL is broken    , *PWLAN_OPCODE_VALUE_TYPE; */

typedef enum _WLAN_SECURABLE_OBJECT {
    wlan_secure_permit_list = 0,
    wlan_secure_deny_list,
    wlan_secure_ac_enabled,
    wlan_secure_bc_scan_enabled,
    wlan_secure_bss_type,
    wlan_secure_show_denied,
    wlan_secure_interface_properties,
    wlan_secure_ihv_control,
    wlan_secure_all_user_profiles_order,
    wlan_secure_add_new_all_user_profiles,
    wlan_secure_add_new_per_user_profiles,
    wlan_secure_media_streaming_mode_enabled,
    wlan_secure_current_operation_mode,
    wlan_secure_get_plaintext_key,
    wlan_secure_hosted_network_elevated_access,
    wlan_secure_virtual_station_extensibility,
    wlan_secure_wfd_elevated_access,
    WLAN_SECURABLE_OBJECT_COUNT
} WLAN_SECURABLE_OBJECT, *PWLAN_SECURABLE_OBJECT;

typedef enum _WLAN_CONNECTION_MODE {
    wlan_connection_mode_profile = 0,
    wlan_connection_mode_temporary_profile,
    wlan_connection_mode_discovery_secure,
    wlan_connection_mode_discovery_unsecure,
    wlan_connection_mode_auto,
    wlan_connection_mode_invalid
} WLAN_CONNECTION_MODE, *PWLAN_CONNECTION_MODE;

#if defined(__midl) || defined(__WIDL__)
typedef [v1_enum] enum _WLAN_IHV_CONTROL_TYPE {
#else
typedef enum _WLAN_IHV_CONTROL_TYPE {
#endif
    wlan_ihv_control_type_service,
    wlan_ihv_control_type_driver
} WLAN_IHV_CONTROL_TYPE;  /* HACK , *PWLAN_IHV_CONTROL_TYPE; */

#if defined(__midl) || defined(__WIDL__)
typedef [v1_enum] enum _WLAN_INTF_OPCODE {
#else
typedef enum _WLAN_INTF_OPCODE {
#endif
    wlan_intf_opcode_autoconf_start                              = 0x000000000,
    wlan_intf_opcode_autoconf_enabled,
    wlan_intf_opcode_background_scan_enabled,
    wlan_intf_opcode_media_streaming_mode,
    wlan_intf_opcode_radio_state,
    wlan_intf_opcode_bss_type,
    wlan_intf_opcode_interface_state,
    wlan_intf_opcode_current_connection,
    wlan_intf_opcode_channel_number,
    wlan_intf_opcode_supported_infrastructure_auth_cipher_pairs,
    wlan_intf_opcode_supported_adhoc_auth_cipher_pairs,
    wlan_intf_opcode_supported_country_or_region_string_list,
    wlan_intf_opcode_current_operation_mode,
    wlan_intf_opcode_supported_safe_mode,
    wlan_intf_opcode_certified_safe_mode,
    wlan_intf_opcode_hosted_network_capable,
    wlan_intf_opcode_management_frame_protection_capable,
    wlan_intf_opcode_autoconf_end                                = 0x0fffffff,
    wlan_intf_opcode_msm_start                                   = 0x10000100,
    wlan_intf_opcode_statistics,
    wlan_intf_opcode_rssi,
    wlan_intf_opcode_msm_end                                     = 0x1fffffff,
    wlan_intf_opcode_security_start                              = 0x20010000,
    wlan_intf_opcode_security_end                                = 0x2fffffff,
    wlan_intf_opcode_ihv_start                                   = 0x30000000,
    wlan_intf_opcode_ihv_end                                     = 0x3fffffff,
    wlan_intf_opcode_secondary_sta_interfaces = 0x11,
    wlan_intf_opcode_secondary_sta_synchronized_connections = 0x12,
    wlan_intf_opcode_realtime_connection_quality = 0x13,
    wlan_intf_opcode_qos_info = 0x14,
} WLAN_INTF_OPCODE; /* HACK: WIDL is broken    , *PWLAN_INTF_OPCODE; */

#if defined(__midl) || defined(__WIDL__)
typedef [v1_enum] enum _WLAN_INTERFACE_STATE {
#else
typedef enum _WLAN_INTERFACE_STATE {
#endif
    wlan_interface_state_not_ready = 0,
    wlan_interface_state_connected,
    wlan_interface_state_ad_hoc_network_formed,
    wlan_interface_state_disconnecting,
    wlan_interface_state_disconnected,
    wlan_interface_state_associating,
    wlan_interface_state_discovering,
    wlan_interface_state_authenticating
} WLAN_INTERFACE_STATE;

typedef enum _WLAN_INTERFACE_TYPE {
    wlan_interface_type_emulated_802_11 = 0,
    wlan_interface_type_native_802_11,
    wlan_interface_type_invalid
} WLAN_INTERFACE_TYPE, *PWLAN_INTERFACE_TYPE;

/* Types */
typedef DWORD WLAN_REASON_CODE, *PWLAN_REASON_CODE;
typedef ULONG WLAN_SIGNAL_QUALITY, *PWLAN_SIGNAL_QUALITY;

typedef struct _DOT11_NETWORK {
    DOT11_SSID dot11Ssid;
    DOT11_BSS_TYPE dot11BssType;
} DOT11_NETWORK, *PDOT11_NETWORK;

typedef struct _DOT11_NETWORK_LIST {
    DWORD dwNumberOfItems;
    DWORD dwIndex;
#if defined(__midl) || defined(__WIDL__)
    [size_is(dwNumberOfItems)] DOT11_NETWORK Network[];
#else
    DOT11_NETWORK Network[1];
#endif
} DOT11_NETWORK_LIST, *PDOT11_NETWORK_LIST;

typedef struct _WLAN_INTERFACE_INFO {
    GUID InterfaceGuid;
    WCHAR strInterfaceDescription[256];
    WLAN_INTERFACE_STATE isState;
} WLAN_INTERFACE_INFO, *PWLAN_INTERFACE_INFO;

typedef struct _WLAN_INTERFACE_INFO_LIST {
    DWORD dwNumberOfItems;
    DWORD dwIndex;
#if defined(__midl) || defined(__WIDL__)
    [unique, size_is(dwNumberOfItems)] WLAN_INTERFACE_INFO InterfaceInfo[*];
#else
    WLAN_INTERFACE_INFO InterfaceInfo[1];
#endif
} WLAN_INTERFACE_INFO_LIST, *PWLAN_INTERFACE_INFO_LIST;

typedef struct _WLAN_INTERFACE_CAPABILITY {
    WLAN_INTERFACE_TYPE interfaceType;
    BOOL bDot11DSupported;
    DWORD dwMaxDesiredSsidListSize;
    DWORD dwMaxDesiredBssidListSize;
    DWORD dwNumberOfSupportedPhys;
    DOT11_PHY_TYPE dot11PhyTypes[WLAN_MAX_PHY_INDEX];
} WLAN_INTERFACE_CAPABILITY, *PWLAN_INTERFACE_CAPABILITY;

typedef struct _WLAN_PHY_RADIO_STATE {
    DWORD dwPhyIndex;
    DOT11_RADIO_STATE dot11SoftwareRadioState;
    DOT11_RADIO_STATE dot11HardwareRadioState;
} WLAN_PHY_RADIO_STATE, *PWLAN_PHY_RADIO_STATE;

typedef struct _WLAN_RADIO_STATE {
    DWORD dwNumberOfPhys;
    WLAN_PHY_RADIO_STATE PhyRadioState[WLAN_MAX_PHY_INDEX];
} WLAN_RADIO_STATE, *PWLAN_RADIO_STATE;

typedef struct _WLAN_RAW_DATA {
    DWORD dwDataSize;
#if defined(__midl) || defined(__WIDL__)
    [size_is(dwDataSize)] BYTE DataBlob[];
#else
    BYTE DataBlob[1];
#endif
} WLAN_RAW_DATA, *PWLAN_RAW_DATA;

typedef struct _WLAN_PROFILE_INFO {
    WCHAR strProfileName[256];
    DWORD dwFlags;
} WLAN_PROFILE_INFO, *PWLAN_PROFILE_INFO;

typedef struct _WLAN_PROFILE_INFO_LIST {
    DWORD dwNumberOfItems;
    DWORD dwIndex;
#if defined(__midl) || defined(__WIDL__)
    [size_is(dwNumberOfItems)] WLAN_PROFILE_INFO ProfileInfo[];
#else
    WLAN_PROFILE_INFO ProfileInfo[1];
#endif
} WLAN_PROFILE_INFO_LIST, *PWLAN_PROFILE_INFO_LIST;

typedef struct _WLAN_AVAILABLE_NETWORK {
    WCHAR strProfileName[WLAN_MAX_NAME_LENGTH];
    DOT11_SSID dot11Ssid;
    DOT11_BSS_TYPE dot11BssType;
    ULONG uNumberOfBssids;
    BOOL bNetworkConnectable;
    WLAN_REASON_CODE wlanNotConnectableReason;
    ULONG uNumberOfPhyTypes;
    DOT11_PHY_TYPE dot11PhyTypes[8];
    BOOL bMorePhyTypes;
    WLAN_SIGNAL_QUALITY wlanSignalQuality;
    BOOL bSecurityEnabled;
    DOT11_AUTH_ALGORITHM dot11DefaultAuthAlgorithm;
    DOT11_CIPHER_ALGORITHM dot11DefaultCipherAlgorithm;
    DWORD dwFlags;
    DWORD dwReserved;
} WLAN_AVAILABLE_NETWORK, *PWLAN_AVAILABLE_NETWORK;

typedef struct _WLAN_AVAILABLE_NETWORK_LIST {
    DWORD dwNumberOfItems;
    DWORD dwIndex;
#if defined(__midl) || defined(__WIDL__)
    [size_is(dwNumberOfItems)] WLAN_AVAILABLE_NETWORK Network[*];
#else
    WLAN_AVAILABLE_NETWORK Network[1];
#endif
} WLAN_AVAILABLE_NETWORK_LIST ,*PWLAN_AVAILABLE_NETWORK_LIST;

typedef struct _WLAN_RATE_SET {
    ULONG uRateSetLength;
    USHORT usRateSet[DOT11_RATE_SET_MAX_LENGTH];
} WLAN_RATE_SET, *PWLAN_RATE_SET;

typedef struct _WLAN_BSS_ENTRY {
    DOT11_SSID dot11Ssid;
    ULONG uPhyId;
    DOT11_MAC_ADDRESS dot11Bssid;
    DOT11_BSS_TYPE dot11BssType;
    DOT11_PHY_TYPE dot11BssPhyType;
    LONG lRssi;
    ULONG uLinkQuality;
    BOOLEAN bInRegDomain;
    USHORT usBeaconPeriod;
    ULONGLONG ullTimestamp;
    ULONGLONG ullHostTimestamp;
    USHORT usCapabilityInformation;
    ULONG  ulChCenterFrequency;
    WLAN_RATE_SET wlanRateSet;
    ULONG ulIeOffset;
    ULONG ulIeSize;
} WLAN_BSS_ENTRY, *PWLAN_BSS_ENTRY;

typedef struct _WLAN_BSS_LIST {
    DWORD dwTotalSize;
    DWORD dwNumberOfItems;
    WLAN_BSS_ENTRY wlanBssEntries[1];
} WLAN_BSS_LIST, *PWLAN_BSS_LIST;

typedef struct _WLAN_ASSOCIATION_ATTRIBUTES {
    DOT11_SSID dot11Ssid;
    DOT11_BSS_TYPE dot11BssType;
    DOT11_MAC_ADDRESS dot11Bssid;
    DOT11_PHY_TYPE dot11PhyType;
    ULONG uDot11PhyIndex;
    WLAN_SIGNAL_QUALITY wlanSignalQuality;
    ULONG ulRxRate;
    ULONG ulTxRate;
} WLAN_ASSOCIATION_ATTRIBUTES, *PWLAN_ASSOCIATION_ATTRIBUTES;

typedef struct _WLAN_SECURITY_ATTRIBUTES {
    BOOL bSecurityEnabled;
    BOOL bOneXEnabled;
    DOT11_AUTH_ALGORITHM dot11AuthAlgorithm;
    DOT11_CIPHER_ALGORITHM dot11CipherAlgorithm;
} WLAN_SECURITY_ATTRIBUTES, *PWLAN_SECURITY_ATTRIBUTES;

typedef struct _WLAN_CONNECTION_ATTRIBUTES {
    WLAN_INTERFACE_STATE isState;
    WLAN_CONNECTION_MODE wlanConnectionMode;
    WCHAR strProfileName[WLAN_MAX_NAME_LENGTH];
    WLAN_ASSOCIATION_ATTRIBUTES wlanAssociationAttributes;
    WLAN_SECURITY_ATTRIBUTES wlanSecurityAttributes;
} WLAN_CONNECTION_ATTRIBUTES, *PWLAN_CONNECTION_ATTRIBUTES;

typedef struct _WLAN_AUTH_CIPHER_PAIR_LIST {
    DWORD dwNumberOfItems;
    DOT11_AUTH_CIPHER_PAIR pAuthCipherPairList[1];
} WLAN_AUTH_CIPHER_PAIR_LIST, *PWLAN_AUTH_CIPHER_PAIR_LIST;

typedef struct _WLAN_CONNECTION_PARAMETERS {
    WLAN_CONNECTION_MODE wlanConnectionMode;
#if defined(__midl) || defined(__WIDL__)
    [string] LPCWSTR strProfile;
#else
    LPCWSTR strProfile;
#endif
    PDOT11_SSID pDot11Ssid;
    PDOT11_BSSID_LIST pDesiredBssidList;
    DOT11_BSS_TYPE dot11BssType;
    DWORD dwFlags;
} WLAN_CONNECTION_PARAMETERS, *PWLAN_CONNECTION_PARAMETERS;

typedef L2_NOTIFICATION_DATA WLAN_NOTIFICATION_DATA, *PWLAN_NOTIFICATION_DATA;

typedef void (__stdcall *WLAN_NOTIFICATION_CALLBACK) (PWLAN_NOTIFICATION_DATA, PVOID);

/* flags for connection notifications */
#define WLAN_CONNECTION_NOTIFICATION_ADHOC_NETWORK_FORMED    0x00000001
#define WLAN_CONNECTION_NOTIFICATION_CONSOLE_USER_PROFILE    0x00000004

typedef struct _WLAN_CONNECTION_NOTIFICATION_DATA {
    WLAN_CONNECTION_MODE wlanConnectionMode;
    WCHAR strProfileName[WLAN_MAX_NAME_LENGTH];
    DOT11_SSID dot11Ssid;
    DOT11_BSS_TYPE dot11BssType;
    BOOL bSecurityEnabled;
    WLAN_REASON_CODE wlanReasonCode;
    DWORD dwFlags;
    WCHAR strProfileXml[1];
} WLAN_CONNECTION_NOTIFICATION_DATA, *PWLAN_CONNECTION_NOTIFICATION_DATA;

typedef struct _WLAN_MSM_NOTIFICATION_DATA {
    WLAN_CONNECTION_MODE wlanConnectionMode;
    WCHAR strProfileName[WLAN_MAX_NAME_LENGTH];
    DOT11_SSID dot11Ssid;
    DOT11_BSS_TYPE dot11BssType;
    DOT11_MAC_ADDRESS dot11MacAddr;
    BOOL bSecurityEnabled;
    BOOL bFirstPeer;
    BOOL bLastPeer;
    WLAN_REASON_CODE wlanReasonCode;
} WLAN_MSM_NOTIFICATION_DATA, *PWLAN_MSM_NOTIFICATION_DATA;

/* Notification source masks; values match L2_NOTIFICATION_SOURCE_*. */
#define WLAN_NOTIFICATION_SOURCE_NONE         0x00000000
#define WLAN_NOTIFICATION_SOURCE_ALL          0x0000FFFF
#define WLAN_NOTIFICATION_SOURCE_ACM          0x00000008
#define WLAN_NOTIFICATION_SOURCE_MSM          0x00000010
#define WLAN_NOTIFICATION_SOURCE_SECURITY     0x00000020
#define WLAN_NOTIFICATION_SOURCE_IHV          0x00000040
#define WLAN_NOTIFICATION_SOURCE_HNWK         0x00000080
#define WLAN_NOTIFICATION_SOURCE_ONEX         0x00000004

/* == L2_REASON_CODE_SUCCESS */
#define WLAN_REASON_CODE_SUCCESS              0
#define WLAN_REASON_CODE_UNKNOWN              L2_REASON_CODE_UNKNOWN
#define WLAN_REASON_CODE_RANGE_SIZE           L2_REASON_CODE_GROUP_SIZE
#define WLAN_REASON_CODE_BASE                 L2_REASON_CODE_DOT11_AC_BASE
#define WLAN_REASON_CODE_AC_BASE              L2_REASON_CODE_DOT11_AC_BASE
#define WLAN_REASON_CODE_AC_CONNECT_BASE      (WLAN_REASON_CODE_AC_BASE + WLAN_REASON_CODE_RANGE_SIZE / 2)
#define WLAN_REASON_CODE_MSM_BASE             L2_REASON_CODE_DOT11_MSM_BASE
#define WLAN_REASON_CODE_MSM_CONNECT_BASE     (WLAN_REASON_CODE_MSM_BASE + WLAN_REASON_CODE_RANGE_SIZE / 2)
#define WLAN_REASON_CODE_CONNECT_CALL_FAIL    (WLAN_REASON_CODE_AC_CONNECT_BASE + 9)
#define WLAN_REASON_CODE_NETWORK_NOT_AVAILABLE (WLAN_REASON_CODE_AC_CONNECT_BASE + 11)
#define WLAN_REASON_CODE_ASSOCIATION_FAILURE  (WLAN_REASON_CODE_MSM_CONNECT_BASE + 2)
#define WLAN_REASON_CODE_MSMSEC_BASE          L2_REASON_CODE_DOT11_SECURITY_BASE
#define WLAN_REASON_CODE_MSMSEC_CONNECT_BASE  (WLAN_REASON_CODE_MSMSEC_BASE + WLAN_REASON_CODE_RANGE_SIZE / 2)
#define WLAN_REASON_CODE_MSMSEC_KEY_START_TIMEOUT   (WLAN_REASON_CODE_MSMSEC_CONNECT_BASE + 4)
#define WLAN_REASON_CODE_MSMSEC_KEY_SUCCESS_TIMEOUT (WLAN_REASON_CODE_MSMSEC_CONNECT_BASE + 5)

/* ACM notification codes (begin at L2_NOTIFICATION_CODE_PUBLIC_BEGIN == 0). */
#if defined(__midl) || defined(__WIDL__)
typedef [v1_enum] enum _WLAN_NOTIFICATION_ACM {
#else
typedef enum _WLAN_NOTIFICATION_ACM {
#endif
    wlan_notification_acm_start = 0,
    wlan_notification_acm_autoconf_enabled,
    wlan_notification_acm_autoconf_disabled,
    wlan_notification_acm_background_scan_enabled,
    wlan_notification_acm_background_scan_disabled,
    wlan_notification_acm_bss_type_change,
    wlan_notification_acm_power_setting_change,
    wlan_notification_acm_scan_complete,
    wlan_notification_acm_scan_fail,
    wlan_notification_acm_connection_start,
    wlan_notification_acm_connection_complete,
    wlan_notification_acm_connection_attempt_fail,
    wlan_notification_acm_filter_list_change,
    wlan_notification_acm_interface_arrival,
    wlan_notification_acm_interface_removal,
    wlan_notification_acm_profile_change,
    wlan_notification_acm_profile_name_change,
    wlan_notification_acm_profiles_exhausted,
    wlan_notification_acm_network_not_available,
    wlan_notification_acm_network_available,
    wlan_notification_acm_disconnecting,
    wlan_notification_acm_disconnected,
    wlan_notification_acm_adhoc_network_state_change,
    wlan_notification_acm_profile_unblocked,
    wlan_notification_acm_screen_power_change,
    wlan_notification_acm_profile_blocked,
    wlan_notification_acm_scan_list_refresh,
    wlan_notification_acm_operational_state_change,
    wlan_notification_acm_end
} WLAN_NOTIFICATION_ACM;
typedef WLAN_NOTIFICATION_ACM *PWLAN_NOTIFICATION_ACM;

typedef enum _WLAN_NOTIFICATION_MSM {
    wlan_notification_msm_start = 0,
    wlan_notification_msm_associating,
    wlan_notification_msm_associated,
    wlan_notification_msm_authenticating,
    wlan_notification_msm_connected,
    wlan_notification_msm_roaming_start,
    wlan_notification_msm_roaming_end,
    wlan_notification_msm_radio_state_change,
    wlan_notification_msm_signal_quality_change,
    wlan_notification_msm_disassociating,
    wlan_notification_msm_disconnected,
    wlan_notification_msm_peer_join,
    wlan_notification_msm_peer_leave,
    wlan_notification_msm_adapter_removal,
    wlan_notification_msm_adapter_operation_mode_change,
    wlan_notification_msm_link_degraded,
    wlan_notification_msm_link_improved,
    wlan_notification_msm_end
} WLAN_NOTIFICATION_MSM, *PWLAN_NOTIFICATION_MSM;

/* Functions */
#if !defined(__midl) && !defined(__WIDL__)
PVOID WINAPI WlanAllocateMemory(DWORD dwSize);
VOID WINAPI WlanFreeMemory(PVOID pMemory);
DWORD WINAPI WlanOpenHandle(IN DWORD dwClientVersion, PVOID pReserved, OUT DWORD *pdwNegotiatedVersion, OUT HANDLE *phClientHandle);
DWORD WINAPI WlanCloseHandle(IN HANDLE hClientHandle, PVOID pReserved);
DWORD WINAPI WlanConnect(IN HANDLE hClientHandle, IN const GUID *pInterfaceGuid, IN const PWLAN_CONNECTION_PARAMETERS pConnectionParameters, PVOID pReserved);
DWORD WINAPI WlanDisconnect(IN HANDLE hClientHandle, IN const GUID *pInterfaceGuid, PVOID pReserved);
DWORD WINAPI WlanEnumInterfaces(IN HANDLE hClientHandle, PVOID pReserved, OUT PWLAN_INTERFACE_INFO_LIST *ppInterfaceList);
DWORD WINAPI WlanGetAvailableNetworkList(IN HANDLE hClientHandle, IN const GUID *pInterfaceGuid, IN DWORD dwFlags, PVOID pReserved, OUT PWLAN_AVAILABLE_NETWORK_LIST *ppAvailableNetworkList);
DWORD WINAPI WlanGetNetworkBssList(IN HANDLE hClientHandle, IN const GUID *pInterfaceGuid, IN const PDOT11_SSID pDot11Ssid, IN DOT11_BSS_TYPE dot11BssType, IN BOOL bSecurityEnabled, PVOID pReserved, OUT PWLAN_BSS_LIST *ppWlanBssList);
DWORD WINAPI WlanGetInterfaceCapability(IN HANDLE hClientHandle, IN const GUID *pInterfaceGuid, PVOID pReserved, OUT PWLAN_INTERFACE_CAPABILITY *ppCapability);
DWORD WINAPI WlanDeleteProfile(IN HANDLE hClientHandle, IN const GUID *pInterfaceGuid, IN LPCWSTR strProfileName, PVOID pReserved);
DWORD WINAPI WlanGetProfile(IN HANDLE hClientHandle, IN const GUID *pInterfaceGuid, IN LPCWSTR strProfileName, PVOID pReserved, OUT LPWSTR *pstrProfileXml, DWORD *pdwFlags, PDWORD pdwGrantedAccess);
DWORD WINAPI WlanGetProfileCustomUserData(IN HANDLE hClientHandle, IN const GUID *pInterfaceGuid, IN LPCWSTR strProfileName, PVOID pReserved, OUT DWORD *pdwDataSize, OUT PBYTE *ppData);
DWORD WINAPI WlanGetProfileList(IN HANDLE hClientHandle, IN const GUID *pInterfaceGuid, PVOID pReserved, OUT PWLAN_PROFILE_INFO_LIST *ppProfileList);
DWORD WINAPI WlanIhvControl(IN HANDLE hClientHandle, IN const GUID *pInterfaceGuid, IN WLAN_IHV_CONTROL_TYPE Type, IN DWORD dwInBufferSize, IN PVOID pInBuffer, IN DWORD dwOutBufferSize, PVOID pOutBuffer, OUT PDWORD pdwBytesReturned);
DWORD WINAPI WlanQueryInterface(IN HANDLE hClientHandle, IN const GUID *pInterfaceGuid, IN WLAN_INTF_OPCODE OpCode, PVOID pReserved, OUT PDWORD pdwDataSize, OUT PVOID *ppData, WLAN_OPCODE_VALUE_TYPE *pWlanOpcodeValueType);
DWORD WINAPI WlanSetInterface(IN HANDLE hClientHandle, IN const GUID *pInterfaceGuid, IN WLAN_INTF_OPCODE OpCode, IN DWORD dwDataSize, IN const VOID *pData, PVOID pReserved);
DWORD WINAPI WlanReasonCodeToString(IN DWORD dwReasonCode, IN DWORD dwBufferSize, IN PWCHAR pStringBuffer, PVOID pReserved);
typedef struct _WLAN_AVAILABLE_NETWORK_V2 {
    WCHAR strProfileName[WLAN_MAX_NAME_LENGTH];
    DOT11_SSID dot11Ssid;
    DOT11_BSS_TYPE dot11BssType;
    ULONG uNumberOfBssids;
    BOOL bNetworkConnectable;
    WLAN_REASON_CODE wlanNotConnectableReason;
    ULONG uNumberOfPhyTypes;
    DOT11_PHY_TYPE dot11PhyTypes[WLAN_MAX_PHY_TYPE_NUMBER];
    BOOL bMorePhyTypes;
    WLAN_SIGNAL_QUALITY wlanSignalQuality;
    BOOL bSecurityEnabled;
    DOT11_AUTH_ALGORITHM dot11DefaultAuthAlgorithm;
    DOT11_CIPHER_ALGORITHM dot11DefaultCipherAlgorithm;
    DWORD dwFlags;
    DOT11_ACCESSNETWORKOPTIONS AccessNetworkOptions;
    DOT11_HESSID dot11HESSID;
    DOT11_VENUEINFO VenueInfo;
    DWORD dwReserved;
} WLAN_AVAILABLE_NETWORK_V2, *PWLAN_AVAILABLE_NETWORK_V2;

typedef struct _WLAN_AVAILABLE_NETWORK_LIST_V2 {
    DWORD dwNumberOfItems;
    DWORD dwIndex;
    WLAN_AVAILABLE_NETWORK_V2 Network[1];
} WLAN_AVAILABLE_NETWORK_LIST_V2, *PWLAN_AVAILABLE_NETWORK_LIST_V2;

typedef struct _WLAN_CONNECTION_PARAMETERS_V2 {
    WLAN_CONNECTION_MODE wlanConnectionMode;
    LPCWSTR strProfile;
    PDOT11_SSID pDot11Ssid;
    PDOT11_HESSID pDot11Hessid;
    PDOT11_BSSID_LIST pDesiredBssidList;
    DOT11_BSS_TYPE dot11BssType;
    DWORD dwFlags;
    PDOT11_ACCESSNETWORKOPTIONS pDot11AccessNetworkOptions;
} WLAN_CONNECTION_PARAMETERS_V2, *PWLAN_CONNECTION_PARAMETERS_V2;

DWORD WINAPI WlanConnect2(IN HANDLE hClientHandle, IN const GUID *pInterfaceGuid, IN const PWLAN_CONNECTION_PARAMETERS_V2 pConnectionParameters, PVOID pReserved);
DWORD WINAPI WlanGetAvailableNetworkList2(IN HANDLE hClientHandle, IN const GUID *pInterfaceGuid, IN DWORD dwFlags, PVOID pReserved, OUT PWLAN_AVAILABLE_NETWORK_LIST_V2 *ppAvailableNetworkList);

DWORD WINAPI WlanRegisterNotification(IN HANDLE hClientHandle,IN DWORD dwNotifSource, IN BOOL bIgnoreDuplicate, WLAN_NOTIFICATION_CALLBACK funcCallback, PVOID pCallbackContext, PVOID pReserved, PDWORD pdwPrevNotifSource);
DWORD WINAPI WlanRenameProfile(IN HANDLE hClientHandle, IN const GUID *pInterfaceGuid, IN LPCWSTR strOldProfileName, IN LPCWSTR strNewProfileName, PVOID pReserved);
DWORD WINAPI WlanSetProfile(IN HANDLE hClientHandle, IN const GUID *pInterfaceGuid, IN DWORD dwFlags, IN LPCWSTR strProfileXml, LPCWSTR strAllUserProfileSecurity, IN BOOL bOverwrite, PVOID pReserved, OUT DWORD *pdwReasonCode);
DWORD WINAPI WlanSetProfileCustomUserData(IN HANDLE hClientHandle, IN const GUID *pInterfaceGuid, IN LPCWSTR strProfileName, IN DWORD dwDataSize, IN const PBYTE pData, PVOID pReserved);
DWORD WINAPI WlanSetProfileEapUserData(IN HANDLE hClientHandle, IN const GUID *pInterfaceGuid, IN LPCWSTR strProfileName, IN EAP_METHOD_TYPE eapType, IN DWORD dwFlags, IN DWORD dwEapUserDataSize, IN const LPBYTE pbEapUserData, PVOID pReserved);
DWORD WINAPI WlanSetProfileList(IN HANDLE hClientHandle, IN const GUID *pInterfaceGuid, DWORD dwItems, IN LPCWSTR *strProfileNames, PVOID pReserved);
DWORD WINAPI WlanSetSecuritySettings(IN HANDLE hClientHandle, IN WLAN_SECURABLE_OBJECT SecurableObject, IN LPCWSTR strModifiedSDDL);
DWORD WINAPI WlanGetSecuritySettings(IN HANDLE hClientHandle, IN WLAN_SECURABLE_OBJECT SecurableObject, OUT WLAN_OPCODE_VALUE_TYPE *pValueType OPTIONAL, OUT LPWSTR *pstrCurrentSDDL, OUT PDWORD pdwGrantedAccess);
DWORD WINAPI WlanScan(IN HANDLE hClientHandle, IN const GUID *pInterfaceGuid, IN PDOT11_SSID pDot11Ssid, IN PWLAN_RAW_DATA pIeData, PVOID pReserved);
#endif

#ifdef __cplusplus
}
#endif


#ifndef DOT11_PSD_IE_MAX_DATA_SIZE
#define DOT11_PSD_IE_MAX_DATA_SIZE 240
#endif
#ifndef DOT11_PSD_IE_MAX_ENTRY_NUMBER
#define DOT11_PSD_IE_MAX_ENTRY_NUMBER 5
#endif
#ifndef WFD_API_VERSION_1_0
#define WFD_API_VERSION_1_0 0x00000001
#endif
#ifndef WLAN_CONNECTION_ADHOC_JOIN_ONLY
#define WLAN_CONNECTION_ADHOC_JOIN_ONLY 0x00000002
#endif
#ifndef WLAN_CONNECTION_EAPOL_PASSTHROUGH
#define WLAN_CONNECTION_EAPOL_PASSTHROUGH 0x00000008
#endif
#ifndef WLAN_CONNECTION_HIDDEN_NETWORK
#define WLAN_CONNECTION_HIDDEN_NETWORK 0x00000001
#endif
#ifndef WLAN_CONNECTION_IGNORE_PRIVACY_BIT
#define WLAN_CONNECTION_IGNORE_PRIVACY_BIT 0x00000004
#endif
#ifndef WLAN_CONNECTION_PERSIST_DISCOVERY_PROFILE
#define WLAN_CONNECTION_PERSIST_DISCOVERY_PROFILE 0x00000010
#endif
#ifndef WLAN_CONNECTION_PERSIST_DISCOVERY_PROFILE_CONNECTION_MODE_AUTO
#define WLAN_CONNECTION_PERSIST_DISCOVERY_PROFILE_CONNECTION_MODE_AUTO 0x00000020
#endif
#ifndef WLAN_CONNECTION_PERSIST_DISCOVERY_PROFILE_OVERWRITE_EXISTING
#define WLAN_CONNECTION_PERSIST_DISCOVERY_PROFILE_OVERWRITE_EXISTING 0x00000040
#endif
#ifndef WLAN_PROFILE_CONNECTION_MODE_AUTO
#define WLAN_PROFILE_CONNECTION_MODE_AUTO 0x00020000
#endif
#ifndef WLAN_PROFILE_CONNECTION_MODE_SET_BY_CLIENT
#define WLAN_PROFILE_CONNECTION_MODE_SET_BY_CLIENT 0x00010000
#endif
#ifndef WLAN_PROFILE_GET_PLAINTEXT_KEY
#define WLAN_PROFILE_GET_PLAINTEXT_KEY 0x00000004
#endif
#ifndef WLAN_PROFILE_GROUP_POLICY
#define WLAN_PROFILE_GROUP_POLICY 0x00000001
#endif
#ifndef WLAN_PROFILE_USER
#define WLAN_PROFILE_USER 0x00000002
#endif
#ifndef WLAN_SET_EAPHOST_DATA_ALL_USERS
#define WLAN_SET_EAPHOST_DATA_ALL_USERS 0x00000001
#endif
#ifndef WLAN_UI_API_INITIAL_VERSION
#define WLAN_UI_API_INITIAL_VERSION 1
#endif
#ifndef WLAN_UI_API_VERSION
#define WLAN_UI_API_VERSION 1
#endif

typedef enum _WFD_ROLE_TYPE {
    WFD_ROLE_TYPE_NONE = 0,
    WFD_ROLE_TYPE_DEVICE = 1,
    WFD_ROLE_TYPE_GROUP_OWNER = 2,
    WFD_ROLE_TYPE_CLIENT = 4,
    WFD_ROLE_TYPE_MAX = 5
} WFD_ROLE_TYPE, *PWFD_ROLE_TYPE;

typedef enum _WL_DISPLAY_PAGES {
    WLConnectionPage = 0,
    WLSecurityPage = 1,
    WLAdvPage = 2
} WL_DISPLAY_PAGES, *PWL_DISPLAY_PAGES;

typedef enum _WLAN_ADHOC_NETWORK_STATE {
    wlan_adhoc_network_state_formed = 0,
    wlan_adhoc_network_state_connected = 1
} WLAN_ADHOC_NETWORK_STATE, *PWLAN_ADHOC_NETWORK_STATE;

typedef enum _WLAN_AUTOCONF_OPCODE {
    wlan_autoconf_opcode_start = 0,
    wlan_autoconf_opcode_show_denied_networks = 1,
    wlan_autoconf_opcode_power_setting = 2,
    wlan_autoconf_opcode_only_use_gp_profiles_for_allowed_networks = 3,
    wlan_autoconf_opcode_allow_explicit_creds = 4,
    wlan_autoconf_opcode_block_period = 5,
    wlan_autoconf_opcode_allow_virtual_station_extensibility = 6,
    wlan_autoconf_opcode_end = 7
} WLAN_AUTOCONF_OPCODE, *PWLAN_AUTOCONF_OPCODE;

typedef enum _WLAN_FILTER_LIST_TYPE {
    wlan_filter_list_type_gp_permit = 0,
    wlan_filter_list_type_gp_deny = 1,
    wlan_filter_list_type_user_permit = 2,
    wlan_filter_list_type_user_deny = 3
} WLAN_FILTER_LIST_TYPE, *PWLAN_FILTER_LIST_TYPE;

typedef enum _WLAN_HOSTED_NETWORK_NOTIFICATION_CODE {
    wlan_hosted_network_state_change = 0x1000,
    wlan_hosted_network_peer_state_change = 0x1001,
    wlan_hosted_network_radio_state_change = 0x1002
} WLAN_HOSTED_NETWORK_NOTIFICATION_CODE, *PWLAN_HOSTED_NETWORK_NOTIFICATION_CODE;

typedef enum _WLAN_HOSTED_NETWORK_OPCODE {
    wlan_hosted_network_opcode_connection_settings = 0,
    wlan_hosted_network_opcode_security_settings = 1,
    wlan_hosted_network_opcode_station_profile = 2,
    wlan_hosted_network_opcode_enable = 3
} WLAN_HOSTED_NETWORK_OPCODE, *PWLAN_HOSTED_NETWORK_OPCODE;

typedef enum _WLAN_HOSTED_NETWORK_PEER_AUTH_STATE {
    wlan_hosted_network_peer_state_invalid = 0,
    wlan_hosted_network_peer_state_authenticated = 1
} WLAN_HOSTED_NETWORK_PEER_AUTH_STATE, *PWLAN_HOSTED_NETWORK_PEER_AUTH_STATE;

typedef enum _WLAN_HOSTED_NETWORK_REASON {
    wlan_hosted_network_reason_success = 0,
    wlan_hosted_network_reason_unspecified = 1,
    wlan_hosted_network_reason_bad_parameters = 2,
    wlan_hosted_network_reason_service_shutting_down = 3,
    wlan_hosted_network_reason_insufficient_resources = 4,
    wlan_hosted_network_reason_elevation_required = 5,
    wlan_hosted_network_reason_read_only = 6,
    wlan_hosted_network_reason_persistence_failed = 7,
    wlan_hosted_network_reason_crypt_error = 8,
    wlan_hosted_network_reason_impersonation = 9,
    wlan_hosted_network_reason_stop_before_start = 10,
    wlan_hosted_network_reason_interface_available = 11,
    wlan_hosted_network_reason_interface_unavailable = 12,
    wlan_hosted_network_reason_miniport_stopped = 13,
    wlan_hosted_network_reason_miniport_started = 14,
    wlan_hosted_network_reason_incompatible_connection_started = 15,
    wlan_hosted_network_reason_incompatible_connection_stopped = 16,
    wlan_hosted_network_reason_user_action = 17,
    wlan_hosted_network_reason_client_abort = 18,
    wlan_hosted_network_reason_ap_start_failed = 19,
    wlan_hosted_network_reason_peer_arrived = 20,
    wlan_hosted_network_reason_peer_departed = 21,
    wlan_hosted_network_reason_peer_timeout = 22,
    wlan_hosted_network_reason_gp_denied = 23,
    wlan_hosted_network_reason_service_unavailable = 24,
    wlan_hosted_network_reason_device_change = 25,
    wlan_hosted_network_reason_properties_change = 26,
    wlan_hosted_network_reason_virtual_station_blocking_use = 27,
    wlan_hosted_network_reason_service_available_on_virtual_station = 28
} WLAN_HOSTED_NETWORK_REASON, *PWLAN_HOSTED_NETWORK_REASON;

typedef enum _WLAN_HOSTED_NETWORK_STATE {
    wlan_hosted_network_unavailable = 0,
    wlan_hosted_network_idle = 1,
    wlan_hosted_network_active = 2
} WLAN_HOSTED_NETWORK_STATE, *PWLAN_HOSTED_NETWORK_STATE;

typedef enum _WLAN_NOTIFICATION_SECURITY {
    wlan_notification_security_start = 0,
    wlan_notification_security_end = 1
} WLAN_NOTIFICATION_SECURITY, *PWLAN_NOTIFICATION_SECURITY;

typedef enum _WLAN_OPERATIONAL_STATE {
    wlan_operational_state_unknown = 0,
    wlan_operational_state_off = 1,
    wlan_operational_state_on = 2,
    wlan_operational_state_going_off = 3,
    wlan_operational_state_going_on = 4
} WLAN_OPERATIONAL_STATE, *PWLAN_OPERATIONAL_STATE;

typedef enum _WLAN_POWER_SETTING {
    wlan_power_setting_no_saving = 0,
    wlan_power_setting_low_saving = 1,
    wlan_power_setting_medium_saving = 2,
    wlan_power_setting_maximum_saving = 3,
    wlan_power_setting_invalid = 4
} WLAN_POWER_SETTING, *PWLAN_POWER_SETTING;

typedef enum _WLAN_INTERFACE_STATE * PWLAN_INTERFACE_STATE;

typedef struct _WFD_GROUP_ID {
  DOT11_MAC_ADDRESS DeviceAddress;
  DOT11_SSID GroupSSID;
} WFD_GROUP_ID, *PWFD_GROUP_ID;

typedef struct _WLAN_COUNTRY_OR_REGION_STRING_LIST {
  DWORD dwNumberOfItems;
  DOT11_COUNTRY_OR_REGION_STRING pCountryOrRegionStringList[1];
} WLAN_COUNTRY_OR_REGION_STRING_LIST, *PWLAN_COUNTRY_OR_REGION_STRING_LIST;

typedef struct _WLAN_DEVICE_SERVICE_GUID_LIST {
  DWORD dwNumberOfItems;
  DWORD dwIndex;
  GUID DeviceService[1];
} WLAN_DEVICE_SERVICE_GUID_LIST, *PWLAN_DEVICE_SERVICE_GUID_LIST;

typedef struct _WLAN_DEVICE_SERVICE_NOTIFICATION_DATA {
  GUID DeviceService;
  DWORD dwOpCode;
  DWORD dwDataSize;
  BYTE DataBlob[1];
} WLAN_DEVICE_SERVICE_NOTIFICATION_DATA, *PWLAN_DEVICE_SERVICE_NOTIFICATION_DATA;

typedef struct _WLAN_HOSTED_NETWORK_CONNECTION_SETTINGS {
  DOT11_SSID hostedNetworkSSID;
  DWORD dwMaxNumberOfPeers;
} WLAN_HOSTED_NETWORK_CONNECTION_SETTINGS, *PWLAN_HOSTED_NETWORK_CONNECTION_SETTINGS;

typedef struct _WLAN_HOSTED_NETWORK_PEER_STATE {
  DOT11_MAC_ADDRESS PeerMacAddress;
  WLAN_HOSTED_NETWORK_PEER_AUTH_STATE PeerAuthState;
} WLAN_HOSTED_NETWORK_PEER_STATE, *PWLAN_HOSTED_NETWORK_PEER_STATE;

typedef struct _WLAN_HOSTED_NETWORK_DATA_PEER_STATE_CHANGE {
  WLAN_HOSTED_NETWORK_PEER_STATE OldState;
  WLAN_HOSTED_NETWORK_PEER_STATE NewState;
  WLAN_HOSTED_NETWORK_REASON PeerStateChangeReason;
} WLAN_HOSTED_NETWORK_DATA_PEER_STATE_CHANGE, *PWLAN_HOSTED_NETWORK_DATA_PEER_STATE_CHANGE;

typedef struct _WLAN_HOSTED_NETWORK_RADIO_STATE {
  DOT11_RADIO_STATE dot11SoftwareRadioState;
  DOT11_RADIO_STATE dot11HardwareRadioState;
} WLAN_HOSTED_NETWORK_RADIO_STATE, *PWLAN_HOSTED_NETWORK_RADIO_STATE;

typedef struct _WLAN_HOSTED_NETWORK_SECURITY_SETTINGS {
  DOT11_AUTH_ALGORITHM dot11AuthAlgo;
  DOT11_CIPHER_ALGORITHM dot11CipherAlgo;
} WLAN_HOSTED_NETWORK_SECURITY_SETTINGS, *PWLAN_HOSTED_NETWORK_SECURITY_SETTINGS;

typedef struct _WLAN_HOSTED_NETWORK_STATE_CHANGE {
  WLAN_HOSTED_NETWORK_STATE OldState;
  WLAN_HOSTED_NETWORK_STATE NewState;
  WLAN_HOSTED_NETWORK_REASON StateChangeReason;
} WLAN_HOSTED_NETWORK_STATE_CHANGE, *PWLAN_HOSTED_NETWORK_STATE_CHANGE;

typedef struct _WLAN_HOSTED_NETWORK_STATUS {
  WLAN_HOSTED_NETWORK_STATE HostedNetworkState;
  GUID IPDeviceID;
  DOT11_MAC_ADDRESS wlanHostedNetworkBSSID;
  DOT11_PHY_TYPE dot11PhyType;
  ULONG ulChannelFrequency;
  DWORD dwNumberOfPeers;
  WLAN_HOSTED_NETWORK_PEER_STATE PeerList[1];
} WLAN_HOSTED_NETWORK_STATUS, *PWLAN_HOSTED_NETWORK_STATUS;

typedef struct _WLAN_QOS_CAPABILITIES {
  BOOL bMSCSSupported;
  BOOL bDSCPToUPMappingSupported;
  BOOL bSCSSupported;
  BOOL bDSCPPolicySupported;
} WLAN_QOS_CAPABILITIES, *PWLAN_QOS_CAPABILITIES;

typedef struct _WLAN_CONNECTION_QOS_INFO {
  WLAN_QOS_CAPABILITIES peerCapabilities;
  BOOL bMSCSConfigured;
  BOOL bDSCPToUPMappingConfigured;
  ULONG ulNumConfiguredSCSStreams;
  ULONG ulNumConfiguredDSCPPolicies;
} WLAN_CONNECTION_QOS_INFO, *PWLAN_CONNECTION_QOS_INFO;

typedef struct _WLAN_QOS_INFO {
  WLAN_QOS_CAPABILITIES interfaceCapabilities;
  BOOL bConnected;
  WLAN_CONNECTION_QOS_INFO connectionQoSInfo;
} WLAN_QOS_INFO, *PWLAN_QOS_INFO;

typedef struct _WLAN_REALTIME_CONNECTION_QUALITY_LINK_INFO {
  UCHAR ucLinkID;
  ULONG ulChannelCenterFrequencyMhz;
  ULONG ulBandwidth;
  LONG lRssi;
  WLAN_RATE_SET wlanRateSet;
} WLAN_REALTIME_CONNECTION_QUALITY_LINK_INFO, *PWLAN_REALTIME_CONNECTION_QUALITY_LINK_INFO;

typedef struct _WLAN_REALTIME_CONNECTION_QUALITY {
  DOT11_PHY_TYPE dot11PhyType;
  ULONG ulLinkQuality;
  ULONG ulRxRate;
  ULONG ulTxRate;
  BOOL bIsMLOConnection;
  ULONG ulNumLinks;
  WLAN_REALTIME_CONNECTION_QUALITY_LINK_INFO linksInfo[1];
} WLAN_REALTIME_CONNECTION_QUALITY, *PWLAN_REALTIME_CONNECTION_QUALITY;

typedef struct WLAN_MAC_FRAME_STATISTICS {
  ULONGLONG ullTransmittedFrameCount;
  ULONGLONG ullReceivedFrameCount;
  ULONGLONG ullWEPExcludedCount;
  ULONGLONG ullTKIPLocalMICFailures;
  ULONGLONG ullTKIPReplays;
  ULONGLONG ullTKIPICVErrorCount;
  ULONGLONG ullCCMPReplays;
  ULONGLONG ullCCMPDecryptErrors;
  ULONGLONG ullWEPUndecryptableCount;
  ULONGLONG ullWEPICVErrorCount;
  ULONGLONG ullDecryptSuccessCount;
  ULONGLONG ullDecryptFailureCount;
} WLAN_MAC_FRAME_STATISTICS, *PWLAN_MAC_FRAME_STATISTICS;

typedef struct WLAN_PHY_FRAME_STATISTICS {
  ULONGLONG ullTransmittedFrameCount;
  ULONGLONG ullMulticastTransmittedFrameCount;
  ULONGLONG ullFailedCount;
  ULONGLONG ullRetryCount;
  ULONGLONG ullMultipleRetryCount;
  ULONGLONG ullMaxTXLifetimeExceededCount;
  ULONGLONG ullTransmittedFragmentCount;
  ULONGLONG ullRTSSuccessCount;
  ULONGLONG ullRTSFailureCount;
  ULONGLONG ullACKFailureCount;
  ULONGLONG ullReceivedFrameCount;
  ULONGLONG ullMulticastReceivedFrameCount;
  ULONGLONG ullPromiscuousReceivedFrameCount;
  ULONGLONG ullMaxRXLifetimeExceededCount;
  ULONGLONG ullFrameDuplicateCount;
  ULONGLONG ullReceivedFragmentCount;
  ULONGLONG ullPromiscuousReceivedFragmentCount;
  ULONGLONG ullFCSErrorCount;
} WLAN_PHY_FRAME_STATISTICS, *PWLAN_PHY_FRAME_STATISTICS;

typedef struct WLAN_STATISTICS {
  ULONGLONG ullFourWayHandshakeFailures;
  ULONGLONG ullTKIPCounterMeasuresInvoked;
  ULONGLONG ullReserved;
  WLAN_MAC_FRAME_STATISTICS MacUcastCounters;
  WLAN_MAC_FRAME_STATISTICS MacMcastCounters;
  DWORD dwNumberOfPhys;
  WLAN_PHY_FRAME_STATISTICS PhyCounters[1];
} WLAN_STATISTICS, *PWLAN_STATISTICS;

#define WFD_API_SUPPORTED 0
#define WFD_API_VERSION WFD_API_VERSION_1_0
#define WLAN_API_MAKE_VERSION_major, _minor) (((DWORD) (_minor)) << 16 | (_major))
#define WLAN_API_VERSION 2
#define WLAN_API_VERSION_MAJOR_v) ((_v) & 0xffff)
#define WLAN_API_VERSION_MINOR_v) (((DWORD) (_v)) >> 16)
#define WLAN_NOTIFICATION_SOURCE_DEVICE_SERVICE 0x800
#define WLAN_REASON_CODE_AC_END 0x2FFFF
#define WLAN_REASON_CODE_ADHOC_SECURITY_FAILURE 0x3800A
#define WLAN_REASON_CODE_AP_PROFILE_NOT_ALLOWED 0x28010
#define WLAN_REASON_CODE_AP_PROFILE_NOT_ALLOWED_FOR_CLIENT 0x2800F
#define WLAN_REASON_CODE_AP_STARTING_FAILURE 0x38013
#define WLAN_REASON_CODE_ASSOCIATION_TIMEOUT 0x38003
#define WLAN_REASON_CODE_AUTO_AP_PROFILE_NOT_ALLOWED 0x80019
#define WLAN_REASON_CODE_AUTO_CONNECTION_NOT_ALLOWED 0x8001A
#define WLAN_REASON_CODE_AUTO_SWITCH_SET_FOR_ADHOC 0x80010
#define WLAN_REASON_CODE_AUTO_SWITCH_SET_FOR_MANUAL_CONNECTION 0x80011
#define WLAN_REASON_CODE_BAD_MAX_NUMBER_OF_CLIENTS_FOR_AP 0x80016
#define WLAN_REASON_CODE_BSS_TYPE_NOT_ALLOWED 0x28005
#define WLAN_REASON_CODE_BSS_TYPE_UNMATCH 0x30003
#define WLAN_REASON_CODE_CONFLICT_SECURITY 0x8000B
#define WLAN_REASON_CODE_DATARATE_UNMATCH 0x30005
#define WLAN_REASON_CODE_DISCONNECT_TIMEOUT 0x3800F
#define WLAN_REASON_CODE_DRIVER_DISCONNECTED 0x3800B
#define WLAN_REASON_CODE_DRIVER_OPERATION_FAILURE 0x3800C
#define WLAN_REASON_CODE_GP_DENIED 0x28003
#define WLAN_REASON_CODE_HOTSPOT2_PROFILE_DENIED 0x28011
#define WLAN_REASON_CODE_HOTSPOT2_PROFILE_NOT_ALLOWED 0x8001B
#define WLAN_REASON_CODE_IHV_CONNECTIVITY_NOT_SUPPORTED 0x80015
#define WLAN_REASON_CODE_IHV_NOT_AVAILABLE 0x3800D
#define WLAN_REASON_CODE_IHV_NOT_RESPONDING 0x3800E
#define WLAN_REASON_CODE_IHV_OUI_MISMATCH 0x80008
#define WLAN_REASON_CODE_IHV_OUI_MISSING 0x80009
#define WLAN_REASON_CODE_IHV_SECURITY_NOT_SUPPORTED 0x80007
#define WLAN_REASON_CODE_IHV_SECURITY_ONEX_MISSING 0x80012
#define WLAN_REASON_CODE_IHV_SETTINGS_MISSING 0x8000A
#define WLAN_REASON_CODE_IN_BLOCKED_LIST 0x28007
#define WLAN_REASON_CODE_IN_FAILED_LIST 0x28006
#define WLAN_REASON_CODE_INTERNAL_FAILURE 0x38010
#define WLAN_REASON_CODE_INVALID_ADHOC_CONNECTION_MODE 0x8000E
#define WLAN_REASON_CODE_INVALID_BSS_TYPE 0x8000D
#define WLAN_REASON_CODE_INVALID_CHANNEL 0x80017
#define WLAN_REASON_CODE_INVALID_PHY_TYPE 0x80005
#define WLAN_REASON_CODE_INVALID_PROFILE_NAME 0x80003
#define WLAN_REASON_CODE_INVALID_PROFILE_SCHEMA 0x80001
#define WLAN_REASON_CODE_INVALID_PROFILE_TYPE 0x80004
#define WLAN_REASON_CODE_KEY_MISMATCH 0x2800D
#define WLAN_REASON_CODE_MSM_END 0x3FFFF
#define WLAN_REASON_CODE_MSM_SECURITY_MISSING 0x80006
#define WLAN_REASON_CODE_MSMSEC_AUTH_START_TIMEOUT 0x48002
#define WLAN_REASON_CODE_MSMSEC_AUTH_SUCCESS_TIMEOUT 0x48003
#define WLAN_REASON_CODE_MSMSEC_AUTH_WCN_COMPLETED 0x48019
#define WLAN_REASON_CODE_MSMSEC_CANCELLED 0x48011
#define WLAN_REASON_CODE_MSMSEC_CAPABILITY_DISCOVERY 0x40015
#define WLAN_REASON_CODE_MSMSEC_CAPABILITY_MFP_NW_NIC 0x40025
#define WLAN_REASON_CODE_MSMSEC_CAPABILITY_NETWORK 0x40012
#define WLAN_REASON_CODE_MSMSEC_CAPABILITY_NIC 0x40013
#define WLAN_REASON_CODE_MSMSEC_CAPABILITY_PROFILE 0x40014
#define WLAN_REASON_CODE_MSMSEC_CAPABILITY_PROFILE_AUTH 0x4001E
#define WLAN_REASON_CODE_MSMSEC_CAPABILITY_PROFILE_CIPHER 0x4001F
#define WLAN_REASON_CODE_MSMSEC_CAPABILITY_PROFILE_SAFE_MODE_NIC 0x40021
#define WLAN_REASON_CODE_MSMSEC_CAPABILITY_PROFILE_SAFE_MODE_NW 0x40022
#define WLAN_REASON_CODE_MSMSEC_DOWNGRADE_DETECTED 0x48013
#define WLAN_REASON_CODE_MSMSEC_END 0x4FFFF
#define WLAN_REASON_CODE_MSMSEC_FORCED_FAILURE 0x48015
#define WLAN_REASON_CODE_MSMSEC_G1_MISSING_GRP_KEY 0x4800D
#define WLAN_REASON_CODE_MSMSEC_G1_MISSING_KEY_DATA 0x4800C
#define WLAN_REASON_CODE_MSMSEC_G1_MISSING_MGMT_GRP_KEY 0x4801B
#define WLAN_REASON_CODE_MSMSEC_KEY_FORMAT 0x48012
#define WLAN_REASON_CODE_MSMSEC_M2_MISSING_IE 0x48018
#define WLAN_REASON_CODE_MSMSEC_M2_MISSING_KEY_DATA 0x48017
#define WLAN_REASON_CODE_MSMSEC_M3_MISSING_GRP_KEY 0x48008
#define WLAN_REASON_CODE_MSMSEC_M3_MISSING_IE 0x48007
#define WLAN_REASON_CODE_MSMSEC_M3_MISSING_KEY_DATA 0x48006
#define WLAN_REASON_CODE_MSMSEC_M3_MISSING_MGMT_GRP_KEY 0x4801A
#define WLAN_REASON_CODE_MSMSEC_M3_TOO_MANY_RSNIE 0x48016
#define WLAN_REASON_CODE_MSMSEC_MAX 0x4FFFF
#define WLAN_REASON_CODE_MSMSEC_MIN 0x40000
#define WLAN_REASON_CODE_MSMSEC_MIXED_CELL 0x40019
#define WLAN_REASON_CODE_MSMSEC_NIC_FAILURE 0x48010
#define WLAN_REASON_CODE_MSMSEC_NO_AUTHENTICATOR 0x4800F
#define WLAN_REASON_CODE_MSMSEC_NO_PAIRWISE_KEY 0x4800B
#define WLAN_REASON_CODE_MSMSEC_PEER_INDICATED_INSECURE 0x4800E
#define WLAN_REASON_CODE_MSMSEC_PR_IE_MATCHING 0x48009
#define WLAN_REASON_CODE_MSMSEC_PROFILE_AUTH_TIMERS_INVALID 0x4001A
#define WLAN_REASON_CODE_MSMSEC_PROFILE_DUPLICATE_AUTH_CIPHER 0x40007
#define WLAN_REASON_CODE_MSMSEC_PROFILE_INVALID_AUTH_CIPHER 0x40009
#define WLAN_REASON_CODE_MSMSEC_PROFILE_INVALID_GKEY_INTV 0x4001B
#define WLAN_REASON_CODE_MSMSEC_PROFILE_INVALID_KEY_INDEX 0x40001
#define WLAN_REASON_CODE_MSMSEC_PROFILE_INVALID_PMKCACHE_MODE 0x4000C
#define WLAN_REASON_CODE_MSMSEC_PROFILE_INVALID_PMKCACHE_SIZE 0x4000D
#define WLAN_REASON_CODE_MSMSEC_PROFILE_INVALID_PMKCACHE_TTL 0x4000E
#define WLAN_REASON_CODE_MSMSEC_PROFILE_INVALID_PREAUTH_MODE 0x4000F
#define WLAN_REASON_CODE_MSMSEC_PROFILE_INVALID_PREAUTH_THROTTLE 0x40010
#define WLAN_REASON_CODE_MSMSEC_PROFILE_KEY_LENGTH 0x40003
#define WLAN_REASON_CODE_MSMSEC_PROFILE_KEY_UNMAPPED_CHAR 0x4001D
#define WLAN_REASON_CODE_MSMSEC_PROFILE_KEYMATERIAL_CHAR 0x40017
#define WLAN_REASON_CODE_MSMSEC_PROFILE_NO_AUTH_CIPHER_SPECIFIED 0x40005
#define WLAN_REASON_CODE_MSMSEC_PROFILE_ONEX_DISABLED 0x4000A
#define WLAN_REASON_CODE_MSMSEC_PROFILE_ONEX_ENABLED 0x4000B
#define WLAN_REASON_CODE_MSMSEC_PROFILE_PASSPHRASE_CHAR 0x40016
#define WLAN_REASON_CODE_MSMSEC_PROFILE_PREAUTH_ONLY_ENABLED 0x40011
#define WLAN_REASON_CODE_MSMSEC_PROFILE_PSK_LENGTH 0x40004
#define WLAN_REASON_CODE_MSMSEC_PROFILE_PSK_PRESENT 0x40002
#define WLAN_REASON_CODE_MSMSEC_PROFILE_RAWDATA_INVALID 0x40008
#define WLAN_REASON_CODE_MSMSEC_PROFILE_SAFE_MODE 0x40020
#define WLAN_REASON_CODE_MSMSEC_PROFILE_TOO_MANY_AUTH_CIPHER_SPECIFIED 0x40006
#define WLAN_REASON_CODE_MSMSEC_PROFILE_UNSUPPORTED_AUTH 0x40023
#define WLAN_REASON_CODE_MSMSEC_PROFILE_UNSUPPORTED_CIPHER 0x40024
#define WLAN_REASON_CODE_MSMSEC_PROFILE_WRONG_KEYTYPE 0x40018
#define WLAN_REASON_CODE_MSMSEC_PSK_MISMATCH_SUSPECTED 0x48014
#define WLAN_REASON_CODE_MSMSEC_SEC_IE_MATCHING 0x4800A
#define WLAN_REASON_CODE_MSMSEC_TRANSITION_NETWORK 0x4001C
#define WLAN_REASON_CODE_MSMSEC_UI_REQUEST_FAILURE 0x48001
#define WLAN_REASON_CODE_NETWORK_NOT_COMPATIBLE 0x20001
#define WLAN_REASON_CODE_NO_AUTO_CONNECTION 0x28001
#define WLAN_REASON_CODE_NO_VISIBLE_AP 0x38014
#define WLAN_REASON_CODE_NON_BROADCAST_SET_FOR_ADHOC 0x8000F
#define WLAN_REASON_CODE_NOT_VISIBLE 0x28002
#define WLAN_REASON_CODE_OPERATION_MODE_NOT_SUPPORTED 0x80018
#define WLAN_REASON_CODE_PHY_TYPE_UNMATCH 0x30004
#define WLAN_REASON_CODE_PRE_SECURITY_FAILURE 0x38004
#define WLAN_REASON_CODE_PROFILE_BASE 0x80000
#define WLAN_REASON_CODE_PROFILE_CHANGED_OR_DELETED 0x2800C
#define WLAN_REASON_CODE_PROFILE_CONNECT_BASE 0x88000
#define WLAN_REASON_CODE_PROFILE_END 0x8FFFF
#define WLAN_REASON_CODE_PROFILE_MISSING 0x80002
#define WLAN_REASON_CODE_PROFILE_NOT_COMPATIBLE 0x20002
#define WLAN_REASON_CODE_PROFILE_SSID_INVALID 0x80013
#define WLAN_REASON_CODE_RESERVED_BASE 0xB0000
#define WLAN_REASON_CODE_RESERVED_END 0xBFFFF
#define WLAN_REASON_CODE_ROAMING_FAILURE 0x38008
#define WLAN_REASON_CODE_ROAMING_SECURITY_FAILURE 0x38009
#define WLAN_REASON_CODE_SCAN_CALL_FAIL 0x2800A
#define WLAN_REASON_CODE_SECURITY_FAILURE 0x38006
#define WLAN_REASON_CODE_SECURITY_MISSING 0x8000C
#define WLAN_REASON_CODE_SECURITY_TIMEOUT 0x38007
#define WLAN_REASON_CODE_SSID_LIST_TOO_LONG 0x28008
#define WLAN_REASON_CODE_START_SECURITY_FAILURE 0x38005
#define WLAN_REASON_CODE_TOO_MANY_SECURITY_ATTEMPTS 0x38012
#define WLAN_REASON_CODE_TOO_MANY_SSID 0x80014
#define WLAN_REASON_CODE_UI_REQUEST_TIMEOUT 0x38011
#define WLAN_REASON_CODE_UNSUPPORTED_SECURITY_SET 0x30002
#define WLAN_REASON_CODE_UNSUPPORTED_SECURITY_SET_BY_OS 0x30001
#define WLAN_REASON_CODE_USER_CANCELLED 0x38001
#define WLAN_REASON_CODE_USER_DENIED 0x28004
#define WLAN_REASON_CODE_USER_NOT_RESPOND 0x2800E

struct _WLAN_RAW_DATA_LIST
{
    DWORD dwTotalSize;
    DWORD dwNumberOfItems;
    struct
    {
        DWORD dwDataOffset;
        DWORD dwDataSize;
    } DataList[1];
};

typedef enum _DOT11_RADIO_STATE *PDOT11_RADIO_STATE;

typedef enum _WLAN_IHV_CONTROL_TYPE *PWLAN_IHV_CONTROL_TYPE;

typedef enum _WLAN_INTF_OPCODE *PWLAN_INTF_OPCODE;

typedef enum _WLAN_OPCODE_VALUE_TYPE *PWLAN_OPCODE_VALUE_TYPE;

typedef struct _WLAN_RAW_DATA_LIST *PWLAN_RAW_DATA_LIST;

typedef void (WINAPI *WFD_OPEN_SESSION_COMPLETE_CALLBACK)(HANDLE, PVOID, GUID, DWORD, DWORD);

typedef struct _WLAN_RAW_DATA_LIST WLAN_RAW_DATA_LIST;

#ifdef __cplusplus
extern "C" {
#endif

/* TODO: LiberNT does not implement the functions below yet */
DWORD WINAPI WFDCancelOpenSession(HANDLE hSessionHandle);
DWORD WINAPI WFDCloseHandle(HANDLE hClientHandle);
DWORD WINAPI WFDCloseSession(HANDLE hSessionHandle);
DWORD WINAPI WFDOpenHandle(DWORD dwClientVersion, PDWORD pdwNegotiatedVersion, PHANDLE phClientHandle);
DWORD WINAPI WFDOpenLegacySession(HANDLE hClientHandle, PDOT11_MAC_ADDRESS pLegacyMacAddress, HANDLE *phSessionHandle, GUID *pGuidSessionInterface);
DWORD WINAPI WFDStartOpenSession(HANDLE hClientHandle, PDOT11_MAC_ADDRESS pDeviceAddress, PVOID pvContext, WFD_OPEN_SESSION_COMPLETE_CALLBACK pfnCallback, PHANDLE phSessionHandle);
DWORD WINAPI WFDUpdateDeviceVisibility(PDOT11_MAC_ADDRESS pDeviceAddress);
DWORD WINAPI WlanDeviceServiceCommand(HANDLE hClientHandle, const GUID *pInterfaceGuid, LPGUID pDeviceServiceGuid, DWORD dwOpCode, DWORD dwInBufferSize, PVOID pInBuffer, DWORD dwOutBufferSize, PVOID pOutBuffer, PDWORD pdwBytesReturned);
DWORD WINAPI WlanExtractPsdIEDataList(HANDLE hClientHandle, DWORD dwIeDataSize, const PBYTE pRawIeData, LPCWSTR strFormat, PVOID pReserved, PWLAN_RAW_DATA_LIST *ppPsdIEDataList);
DWORD WINAPI WlanGetFilterList(HANDLE hClientHandle, WLAN_FILTER_LIST_TYPE wlanFilterListType, PVOID pReserved, PDOT11_NETWORK_LIST *ppNetworkList);
DWORD WINAPI WlanGetSupportedDeviceServices(HANDLE hClientHandle, const GUID *pInterfaceGuid, PWLAN_DEVICE_SERVICE_GUID_LIST *ppDevSvcGuidList);
DWORD WINAPI WlanHostedNetworkForceStart(HANDLE hClientHandle, PWLAN_HOSTED_NETWORK_REASON pFailReason, PVOID pvReserved);
DWORD WINAPI WlanHostedNetworkForceStop(HANDLE hClientHandle, PWLAN_HOSTED_NETWORK_REASON pFailReason, PVOID pvReserved);
DWORD WINAPI WlanHostedNetworkInitSettings(HANDLE hClientHandle, PWLAN_HOSTED_NETWORK_REASON pFailReason, PVOID pvReserved);
DWORD WINAPI WlanHostedNetworkQueryProperty(HANDLE hClientHandle, WLAN_HOSTED_NETWORK_OPCODE OpCode, PDWORD pdwDataSize, PVOID *ppvData, PWLAN_OPCODE_VALUE_TYPE pWlanOpcodeValueType, PVOID pvReserved);
DWORD WINAPI WlanHostedNetworkQuerySecondaryKey(HANDLE hClientHandle, PDWORD pdwKeyLength, PUCHAR *ppucKeyData, PBOOL pbIsPassPhrase, PBOOL pbPersistent, PWLAN_HOSTED_NETWORK_REASON pFailReason, PVOID pvReserved);
DWORD WINAPI WlanHostedNetworkQueryStatus(HANDLE hClientHandle, PWLAN_HOSTED_NETWORK_STATUS *ppWlanHostedNetworkStatus, PVOID pvReserved);
DWORD WINAPI WlanHostedNetworkRefreshSecuritySettings(HANDLE hClientHandle, PWLAN_HOSTED_NETWORK_REASON pFailReason, PVOID pvReserved);
DWORD WINAPI WlanHostedNetworkSetProperty(HANDLE hClientHandle, WLAN_HOSTED_NETWORK_OPCODE OpCode, DWORD dwDataSize, PVOID pvData, PWLAN_HOSTED_NETWORK_REASON pFailReason, PVOID pvReserved);
DWORD WINAPI WlanHostedNetworkSetSecondaryKey(HANDLE hClientHandle, DWORD dwKeyLength, PUCHAR pucKeyData, BOOL bIsPassPhrase, BOOL bPersistent, PWLAN_HOSTED_NETWORK_REASON pFailReason, PVOID pvReserved);
DWORD WINAPI WlanHostedNetworkStartUsing(HANDLE hClientHandle, PWLAN_HOSTED_NETWORK_REASON pFailReason, PVOID pvReserved);
DWORD WINAPI WlanHostedNetworkStopUsing(HANDLE hClientHandle, PWLAN_HOSTED_NETWORK_REASON pFailReason, PVOID pvReserved);
DWORD WINAPI WlanQueryAutoConfigParameter(HANDLE hClientHandle, WLAN_AUTOCONF_OPCODE OpCode, PVOID pReserved, PDWORD pdwDataSize, PVOID *ppData, PWLAN_OPCODE_VALUE_TYPE pWlanOpcodeValueType);
DWORD WINAPI WlanRegisterDeviceServiceNotification(HANDLE hClientHandle, const PWLAN_DEVICE_SERVICE_GUID_LIST pDevSvcGuidList);
DWORD WINAPI WlanRegisterVirtualStationNotification(HANDLE hClientHandle, BOOL bRegister, PVOID pReserved);
DWORD WINAPI WlanSaveTemporaryProfile(HANDLE hClientHandle, const GUID *pInterfaceGuid, LPCWSTR strProfileName, LPCWSTR strAllUserProfileSecurity, DWORD dwFlags, BOOL bOverWrite, PVOID pReserved);
DWORD WINAPI WlanSetAutoConfigParameter(HANDLE hClientHandle, WLAN_AUTOCONF_OPCODE OpCode, DWORD dwDataSize, const PVOID pData, PVOID pReserved);
DWORD WINAPI WlanSetFilterList(HANDLE hClientHandle, WLAN_FILTER_LIST_TYPE wlanFilterListType, const PDOT11_NETWORK_LIST pNetworkList, PVOID pReserved);
DWORD WINAPI WlanSetProfileEapXmlUserData(HANDLE hClientHandle, const GUID *pInterfaceGuid, LPCWSTR strProfileName, DWORD dwFlags, LPCWSTR strEapXmlUserData, PVOID pReserved);
DWORD WINAPI WlanSetProfilePosition(HANDLE hClientHandle, const GUID *pInterfaceGuid, LPCWSTR strProfileName, DWORD dwPosition, PVOID pReserved);
DWORD WINAPI WlanSetPsdIEDataList(HANDLE hClientHandle, LPCWSTR strFormat, const PWLAN_RAW_DATA_LIST pPsdIEDataList, PVOID pReserved);
DWORD WINAPI WlanUIEditProfile(DWORD dwClientVersion, LPCWSTR wstrProfileName, GUID *pInterfaceGuid, HWND hWnd, WL_DISPLAY_PAGES wlStartPage, PVOID pReserved, PWLAN_REASON_CODE pWlanReasonCode);

#ifdef __cplusplus
}
#endif

#define WLAN_API_MAKE_VERSION_major, _minor) (((DWORD) (_minor)) << 16 | (_major))
#define WLAN_API_VERSION_MAJOR_v) ((_v) & 0xffff)
#define WLAN_API_VERSION_MINOR_v) (((DWORD) (_v)) >> 16)

#define WLAN_API_MAKE_VERSION_major, _minor) (((DWORD) (_minor)) << 16 | (_major))
#define WLAN_API_VERSION_MAJOR_v) ((_v) & 0xffff)
#define WLAN_API_VERSION_MINOR_v) (((DWORD) (_v)) >> 16)

#endif  // _WLANAPI_H
