/*
 * ntddndis.h
 *
 * NDIS device driver interface
 *
 * This file is part of the w32api package.
 *
 * Contributors:
 *   Created by Casper S. Hornstrup <chorns@users.sourceforge.net>
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

#ifndef _NTDDNDIS_
#define _NTDDNDIS_

#include <ifdef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* NDIS 6.30+ Hyper-V switch identifiers. These are scalar ABI types shared
 * by ntddndis.h consumers and the kernel-mode NET_PNP notification surface. */
typedef ULONG NDIS_NIC_SWITCH_ID, *PNDIS_NIC_SWITCH_ID;
typedef ULONG NDIS_NIC_SWITCH_VPORT_ID, *PNDIS_NIC_SWITCH_VPORT_ID;

typedef enum _NDIS_WAN_QUALITY {
	NdisWanRaw,
	NdisWanErrorControl,
	NdisWanReliable
} NDIS_WAN_QUALITY, *PNDIS_WAN_QUALITY;

typedef enum _NDIS_DEVICE_POWER_STATE {
  NdisDeviceStateUnspecified = 0,
  NdisDeviceStateD0,
  NdisDeviceStateD1,
  NdisDeviceStateD2,
  NdisDeviceStateD3,
  NdisDeviceStateMaximum
} NDIS_DEVICE_POWER_STATE, *PNDIS_DEVICE_POWER_STATE;

typedef enum _NDIS_802_11_WEP_STATUS
{
    Ndis802_11WEPEnabled,
    Ndis802_11Encryption1Enabled = Ndis802_11WEPEnabled,
    Ndis802_11WEPDisabled,
    Ndis802_11EncryptionDisabled = Ndis802_11WEPDisabled,
    Ndis802_11WEPKeyAbsent,
    Ndis802_11Encryption1KeyAbsent = Ndis802_11WEPKeyAbsent,
    Ndis802_11WEPNotSupported,
    Ndis802_11EncryptionNotSupported = Ndis802_11WEPNotSupported,
    Ndis802_11Encryption2Enabled,
    Ndis802_11Encryption2KeyAbsent,
    Ndis802_11Encryption3Enabled,
    Ndis802_11Encryption3KeyAbsent
} NDIS_802_11_WEP_STATUS, *PNDIS_802_11_WEP_STATUS,
  NDIS_802_11_ENCRYPTION_STATUS, *PNDIS_802_11_ENCRYPTION_STATUS;

typedef enum _NDIS_802_11_AUTHENTICATION_MODE
{
    Ndis802_11AuthModeOpen,
    Ndis802_11AuthModeShared,
    Ndis802_11AuthModeAutoSwitch,
    Ndis802_11AuthModeWPA,
    Ndis802_11AuthModeWPAPSK,
    Ndis802_11AuthModeWPANone,
    Ndis802_11AuthModeWPA2,
    Ndis802_11AuthModeWPA2PSK,
    Ndis802_11AuthModeWPA3,
    Ndis802_11AuthModeWPA3Ent192 = Ndis802_11AuthModeWPA3,
    Ndis802_11AuthModeWPA3SAE,
    Ndis802_11AuthModeOWE,
    Ndis802_11AuthModeWPA3Ent,
    Ndis802_11AuthModeMax
} NDIS_802_11_AUTHENTICATION_MODE, *PNDIS_802_11_AUTHENTICATION_MODE;

typedef enum _NDIS_802_11_NETWORK_INFRASTRUCTURE
{
    Ndis802_11IBSS,
    Ndis802_11Infrastructure,
    Ndis802_11AutoUnknown,
    Ndis802_11InfrastructureMax
} NDIS_802_11_NETWORK_INFRASTRUCTURE, *PNDIS_802_11_NETWORK_INFRASTRUCTURE;

typedef enum _NDIS_802_11_NETWORK_TYPE
{
    Ndis802_11FH,
    Ndis802_11DS,
    Ndis802_11OFDM5,
    Ndis802_11OFDM24,
    Ndis802_11Automode,
    Ndis802_11NetworkTypeMax
} NDIS_802_11_NETWORK_TYPE, *PNDIS_802_11_NETWORK_TYPE;

typedef struct _NDIS_OBJECT_HEADER
{
    UCHAR Type;
    UCHAR Revision;
    USHORT Size;
} NDIS_OBJECT_HEADER, *PNDIS_OBJECT_HEADER;

#define NDIS_OBJECT_TYPE_DEFAULT                        0x80

#define NDIS_802_11_LENGTH_SSID  32
#define NDIS_802_11_LENGTH_RATES 8

typedef UCHAR NDIS_802_11_MAC_ADDRESS[6];
typedef LONG NDIS_802_11_RSSI;
typedef UCHAR NDIS_802_11_RATES[NDIS_802_11_LENGTH_RATES];

typedef struct _NDIS_802_11_SSID
{
    ULONG SsidLength;
    UCHAR Ssid[NDIS_802_11_LENGTH_SSID];
} NDIS_802_11_SSID, *PNDIS_802_11_SSID;

typedef struct _NDIS_802_11_CONFIGURATION_FH
{
    ULONG Length;
    ULONG HopPattern;
    ULONG HopSet;
    ULONG DwellTime;
} NDIS_802_11_CONFIGURATION_FH, *PNDIS_802_11_CONFIGURATION_FH;

typedef struct _NDIS_802_11_CONFIGURATION
{
    ULONG Length;
    ULONG BeaconPeriod;
    ULONG ATIMWindow;
    ULONG DSConfig;
    NDIS_802_11_CONFIGURATION_FH FHConfig;
} NDIS_802_11_CONFIGURATION, *PNDIS_802_11_CONFIGURATION;

typedef struct _NDIS_WLAN_BSSID
{
    ULONG Length;
    NDIS_802_11_MAC_ADDRESS MacAddress;
    UCHAR Reserved[2];
    NDIS_802_11_SSID Ssid;
    ULONG Privacy;
    NDIS_802_11_RSSI Rssi;
    NDIS_802_11_NETWORK_TYPE NetworkTypeInUse;
    NDIS_802_11_CONFIGURATION Configuration;
    NDIS_802_11_NETWORK_INFRASTRUCTURE InfrastructureMode;
    NDIS_802_11_RATES SupportedRates;
} NDIS_WLAN_BSSID, *PNDIS_WLAN_BSSID;

typedef struct _NDIS_802_11_BSSID_LIST
{
    ULONG NumberOfItems;
    NDIS_WLAN_BSSID Bssid[1];
} NDIS_802_11_BSSID_LIST, *PNDIS_802_11_BSSID_LIST;

typedef struct _NDIS_802_11_WEP
{
    ULONG Length;
    ULONG KeyIndex;
    ULONG KeyLength;
    UCHAR KeyMaterial[1];
} NDIS_802_11_WEP, *PNDIS_802_11_WEP;

typedef ULONGLONG NDIS_802_11_KEY_RSC;

typedef struct _NDIS_802_11_KEY
{
    ULONG Length;
    ULONG KeyIndex;
    ULONG KeyLength;
    NDIS_802_11_MAC_ADDRESS BSSID;
    NDIS_802_11_KEY_RSC KeyRSC;
    UCHAR KeyMaterial[1];
} NDIS_802_11_KEY, *PNDIS_802_11_KEY;

typedef struct _NDIS_PM_WAKE_UP_CAPABILITIES {
  NDIS_DEVICE_POWER_STATE  MinMagicPacketWakeUp;
  NDIS_DEVICE_POWER_STATE  MinPatternWakeUp;
  NDIS_DEVICE_POWER_STATE  MinLinkChangeWakeUp;
} NDIS_PM_WAKE_UP_CAPABILITIES, *PNDIS_PM_WAKE_UP_CAPABILITIES;

/* NDIS_PNP_CAPABILITIES.Flags constants */
#define NDIS_DEVICE_WAKE_UP_ENABLE                0x00000001
#define NDIS_DEVICE_WAKE_ON_PATTERN_MATCH_ENABLE  0x00000002
#define NDIS_DEVICE_WAKE_ON_MAGIC_PACKET_ENABLE   0x00000004

typedef struct _NDIS_PNP_CAPABILITIES {
  ULONG  Flags;
  NDIS_PM_WAKE_UP_CAPABILITIES  WakeUpCapabilities;
} NDIS_PNP_CAPABILITIES, *PNDIS_PNP_CAPABILITIES;

/* NDIS 6.20 power-management capabilities (REVISION_1 form). Target of
 * NDIS_MINIPORT_ADAPTER_GENERAL_ATTRIBUTES::PowerManagementCapabilitiesEx. */
#define NDIS_PM_CAPABILITIES_REVISION_1  1



/* Type (OID_GEN_VLAN_ID) */
typedef ULONG NDIS_VLAN_ID;

/* NDIS driver medium (OID_GEN_MEDIA_SUPPORTED / OID_GEN_MEDIA_IN_USE) */
typedef enum _NDIS_MEDIUM {
    NdisMedium802_3,
    NdisMedium802_5,
    NdisMediumFddi,
    NdisMediumWan,
    NdisMediumLocalTalk,
    NdisMediumDix,          // Not a real medium
    NdisMediumArcnetRaw,
    NdisMediumArcnet878_2,
    NdisMediumAtm,
    NdisMediumWirelessWan,
    NdisMediumIrda,
    NdisMediumBpc,
    NdisMediumCoWan,
    NdisMedium1394,
    NdisMediumInfiniBand,
#if ((NTDDI_VERSION >= NTDDI_VISTA) || NDIS_SUPPORT_NDIS6)
    NdisMediumTunnel,
    NdisMediumNative802_11,
    NdisMediumLoopback,
#endif
#if (NTDDI_VERSION >= NTDDI_WIN7)
    NdisMediumWiMAX,
    NdisMediumIP,
#endif
    NdisMediumMax           // Not a real medium
} NDIS_MEDIUM, *PNDIS_MEDIUM;

/* Physical medium type (OID_GEN_PHYSICAL_MEDIUM) */
typedef enum _NDIS_PHYSICAL_MEDIUM
{
    // Windows Server 2003 SP1 Platform SDK (or earlier?)
    NdisPhysicalMediumUnspecified,
    NdisPhysicalMediumWirelessLan,
    NdisPhysicalMediumCableModem,
    NdisPhysicalMediumPhoneLine,
    NdisPhysicalMediumPowerLine,
    NdisPhysicalMediumDSL,
    NdisPhysicalMediumFibreChannel,
    NdisPhysicalMedium1394,
    NdisPhysicalMediumWirelessWan,
    NdisPhysicalMediumNative802_11,
    NdisPhysicalMediumBluetooth,
    // Windows SDK 7.1A (or earlier?)
    NdisPhysicalMediumInfiniband,
    NdisPhysicalMediumWiMax,
    NdisPhysicalMediumUWB,
    NdisPhysicalMedium802_3,
    NdisPhysicalMedium802_5,
    NdisPhysicalMediumIrda,
    NdisPhysicalMediumWiredWAN,
    NdisPhysicalMediumWiredCoWan,
    NdisPhysicalMediumOther,
    // Windows SDK 10.0.16299.0 (or earlier?)
    NdisPhysicalMediumNative802_15_4,
    // Always
    NdisPhysicalMediumMax               // Not a real physical medium
} NDIS_PHYSICAL_MEDIUM, *PNDIS_PHYSICAL_MEDIUM;

typedef ULONG NDIS_OID, *PNDIS_OID;

/* Required Object IDs (OIDs) */
#define OID_GEN_SUPPORTED_LIST            0x00010101
#define OID_GEN_HARDWARE_STATUS           0x00010102
#define OID_GEN_MEDIA_SUPPORTED           0x00010103
#define OID_GEN_MEDIA_IN_USE              0x00010104
#define OID_GEN_MAXIMUM_LOOKAHEAD         0x00010105
#define OID_GEN_MAXIMUM_FRAME_SIZE        0x00010106
#define OID_GEN_LINK_SPEED                0x00010107
#define OID_GEN_TRANSMIT_BUFFER_SPACE     0x00010108
#define OID_GEN_RECEIVE_BUFFER_SPACE      0x00010109
#define OID_GEN_TRANSMIT_BLOCK_SIZE       0x0001010A
#define OID_GEN_RECEIVE_BLOCK_SIZE        0x0001010B
#define OID_GEN_VENDOR_ID                 0x0001010C
#define OID_GEN_VENDOR_DESCRIPTION        0x0001010D
#define OID_GEN_CURRENT_PACKET_FILTER     0x0001010E
#define OID_GEN_CURRENT_LOOKAHEAD         0x0001010F
#define OID_GEN_DRIVER_VERSION            0x00010110
#define OID_GEN_MAXIMUM_TOTAL_SIZE        0x00010111
#define OID_GEN_PROTOCOL_OPTIONS          0x00010112
#define OID_GEN_MAC_OPTIONS               0x00010113
#define OID_GEN_MEDIA_CONNECT_STATUS      0x00010114
#define OID_GEN_MAXIMUM_SEND_PACKETS      0x00010115
#define OID_GEN_VENDOR_DRIVER_VERSION     0x00010116
#define OID_GEN_SUPPORTED_GUIDS           0x00010117
#define OID_GEN_NETWORK_LAYER_ADDRESSES   0x00010118
#define OID_GEN_TRANSPORT_HEADER_OFFSET   0x00010119
#define OID_GEN_MACHINE_NAME              0x0001021A
#define OID_GEN_RNDIS_CONFIG_PARAMETER    0x0001021B
#define OID_GEN_VLAN_ID                   0x0001021C

/* Optional OIDs */
#define OID_GEN_MEDIA_CAPABILITIES        0x00010201
#define OID_GEN_PHYSICAL_MEDIUM           0x00010202

/* Required statistics OIDs */
#define OID_GEN_XMIT_OK                   0x00020101
#define OID_GEN_RCV_OK                    0x00020102
#define OID_GEN_XMIT_ERROR                0x00020103
#define OID_GEN_RCV_ERROR                 0x00020104
#define OID_GEN_RCV_NO_BUFFER             0x00020105

/* Optional statistics OIDs */
#define OID_GEN_DIRECTED_BYTES_XMIT       0x00020201
#define OID_GEN_DIRECTED_FRAMES_XMIT      0x00020202
#define OID_GEN_MULTICAST_BYTES_XMIT      0x00020203
#define OID_GEN_MULTICAST_FRAMES_XMIT     0x00020204
#define OID_GEN_BROADCAST_BYTES_XMIT      0x00020205
#define OID_GEN_BROADCAST_FRAMES_XMIT     0x00020206
#define OID_GEN_DIRECTED_BYTES_RCV        0x00020207
#define OID_GEN_DIRECTED_FRAMES_RCV       0x00020208
#define OID_GEN_MULTICAST_BYTES_RCV       0x00020209
#define OID_GEN_MULTICAST_FRAMES_RCV      0x0002020A
#define OID_GEN_BROADCAST_BYTES_RCV       0x0002020B
#define OID_GEN_BROADCAST_FRAMES_RCV      0x0002020C
#define OID_GEN_RCV_CRC_ERROR             0x0002020D
#define OID_GEN_TRANSMIT_QUEUE_LENGTH     0x0002020E
#define OID_GEN_GET_TIME_CAPS             0x0002020F
#define OID_GEN_GET_NETCARD_TIME          0x00020210
#define OID_GEN_NETCARD_LOAD              0x00020211
#define OID_GEN_DEVICE_PROFILE            0x00020212
#define OID_GEN_INIT_TIME_MS              0x00020213
#define OID_GEN_RESET_COUNTS              0x00020214
#define OID_GEN_MEDIA_SENSE_COUNTS        0x00020215
#define OID_GEN_FRIENDLY_NAME             0x00020216
#define OID_GEN_MINIPORT_INFO             0x00020217
#define OID_GEN_RESET_VERIFY_PARAMETERS   0x00020218

/* IEEE 802.3 (Ethernet) OIDs */
#define NDIS_802_3_MAC_OPTION_PRIORITY    0x00000001

#define OID_802_3_PERMANENT_ADDRESS       0x01010101
#define OID_802_3_CURRENT_ADDRESS         0x01010102
#define OID_802_3_MULTICAST_LIST          0x01010103
#define OID_802_3_MAXIMUM_LIST_SIZE       0x01010104
#define OID_802_3_MAC_OPTIONS             0x01010105
#define OID_802_3_RCV_ERROR_ALIGNMENT     0x01020101
#define OID_802_3_XMIT_ONE_COLLISION      0x01020102
#define OID_802_3_XMIT_MORE_COLLISIONS    0x01020103
#define OID_802_3_XMIT_DEFERRED           0x01020201
#define OID_802_3_XMIT_MAX_COLLISIONS     0x01020202
#define OID_802_3_RCV_OVERRUN             0x01020203
#define OID_802_3_XMIT_UNDERRUN           0x01020204
#define OID_802_3_XMIT_HEARTBEAT_FAILURE  0x01020205
#define OID_802_3_XMIT_TIMES_CRS_LOST     0x01020206
#define OID_802_3_XMIT_LATE_COLLISIONS    0x01020207

/* IEEE 802.11 (WLAN) OIDs */
#define OID_802_11_BSSID                        0x0D010101
#define OID_802_11_SSID                         0x0D010102
#define OID_802_11_NETWORK_TYPES_SUPPORTED      0x0D010203
#define OID_802_11_NETWORK_TYPE_IN_USE          0x0D010204
#define OID_802_11_TX_POWER_LEVEL               0x0D010205
#define OID_802_11_RSSI                         0x0D010206
#define OID_802_11_RSSI_TRIGGER                 0x0D010207
#define OID_802_11_INFRASTRUCTURE_MODE          0x0D010108
#define OID_802_11_FRAGMENTATION_THRESHOLD      0x0D010209
#define OID_802_11_RTS_THRESHOLD                0x0D01020A
#define OID_802_11_NUMBER_OF_ANTENNAS           0x0D01020B
#define OID_802_11_RX_ANTENNA_SELECTED          0x0D01020C
#define OID_802_11_TX_ANTENNA_SELECTED          0x0D01020D
#define OID_802_11_SUPPORTED_RATES              0x0D01020E
#define OID_802_11_DESIRED_RATES                0x0D010210
#define OID_802_11_CONFIGURATION                0x0D010211
#define OID_802_11_STATISTICS                   0x0D020212
#define OID_802_11_ADD_WEP                      0x0D010113
#define OID_802_11_REMOVE_WEP                   0x0D010114
#define OID_802_11_DISASSOCIATE                 0x0D010115
#define OID_802_11_POWER_MODE                   0x0D010216
#define OID_802_11_BSSID_LIST                   0x0D010217
#define OID_802_11_AUTHENTICATION_MODE          0x0D010118
#define OID_802_11_PRIVACY_FILTER               0x0D010119
#define OID_802_11_BSSID_LIST_SCAN              0x0D01011A
#define OID_802_11_WEP_STATUS                   0x0D01011B
/* New name is supposed to better reflect the extended set of encryption status */
#define OID_802_11_ENCRYPTION_STATUS            OID_802_11_WEP_STATUS
#define OID_802_11_RELOAD_DEFAULTS              0x0D01011C
/* Allows key mapping and default keys */
#define OID_802_11_ADD_KEY                      0x0D01011D
#define OID_802_11_REMOVE_KEY                   0x0D01011E
#define OID_802_11_ASSOCIATION_INFORMATION      0x0D01011F
#define OID_802_11_TEST                         0x0D010120
#define OID_802_11_MEDIA_STREAM_MODE            0x0D010121
#define OID_802_11_CAPABILITY                   0x0D010122
#define OID_802_11_PMKID                        0x0D010123
#define OID_802_11_NON_BCAST_SSID_LIST          0x0D010124
#define OID_802_11_RADIO_STATUS                 0x0D010125

/* PnP and Power Management (PM) OIDs */
#define OID_PNP_CAPABILITIES                    0xFD010100
#define OID_PNP_SET_POWER                       0xFD010101
#define OID_PNP_QUERY_POWER                     0xFD010102
#define OID_PNP_ADD_WAKE_UP_PATTERN             0xFD010103
#define OID_PNP_REMOVE_WAKE_UP_PATTERN          0xFD010104
#define OID_PNP_WAKE_UP_PATTERN_LIST            0xFD010105
#define OID_PNP_ENABLE_WAKE_UP                  0xFD010106

/* Optional PnP/PM statistics OIDs */
#define OID_PNP_WAKE_UP_OK                      0xFD020200
#define OID_PNP_WAKE_UP_ERROR                   0xFD020201

#define NDIS_PNP_WAKE_UP_MAGIC_PACKET           0x00000001
#define NDIS_PNP_WAKE_UP_PATTERN_MATCH          0x00000002
#define NDIS_PNP_WAKE_UP_LINK_CHANGE            0x00000004

/* TCP and IP OIDs */
#define OID_TCP_TASK_OFFLOAD                    0xFC010201
#define OID_TCP_TASK_IPSEC_ADD_SA               0xFC010202
#define OID_TCP_TASK_IPSEC_DELETE_SA            0xFC010203
#define OID_TCP_SAN_SUPPORT                     0xFC010204
#define OID_TCP_TASK_IPSEC_ADD_UDPESP_SA        0xFC010205
#define OID_TCP_TASK_IPSEC_DELETE_UDPESP_SA     0xFC010206
#define OID_TCP4_OFFLOAD_STATS                  0xFC010207
#define OID_TCP6_OFFLOAD_STATS                  0xFC010208
#define OID_IP4_OFFLOAD_STATS                   0xFC010209
#define OID_IP6_OFFLOAD_STATS                   0xFC01020A

/* New NDIS 6 offload OIDs */
#define OID_TCP_OFFLOAD_CURRENT_CONFIG                     0xFC01020B /* NDIS 5 handled. Query only */
#define OID_TCP_OFFLOAD_PARAMETERS                         0xFC01020C /* Set only */
#define OID_TCP_OFFLOAD_HARDWARE_CAPABILITIES              0xFC01020D /* Query only */
#define OID_TCP_CONNECTION_OFFLOAD_CURRENT_CONFIG          0xFC01020E /* Query only */
#define OID_TCP_CONNECTION_OFFLOAD_HARDWARE_CAPABILITIES   0xFC01020F /* Query only */
#define OID_TCP_RSC_STATISTICS                             0x0002021D
#define OID_OFFLOAD_ENCAPSULATION                          0x0101010A

/* Obsolete FFP defines */
#define OID_FFP_SUPPORT                         0xFC010210
#define OID_FFP_FLUSH                           0xFC010211
#define OID_FFP_CONTROL                         0xFC010212
#define OID_FFP_PARAMS                          0xFC010213
#define OID_FFP_DATA                            0xFC010214

#define OID_FFP_DRIVER_STATS                    0xFC020210
#define OID_FFP_ADAPTER_STATS                   0xFC020211

/* OID_GEN_MINIPORT_INFO constants */
#define NDIS_MINIPORT_BUS_MASTER                      0x00000001
#define NDIS_MINIPORT_WDM_DRIVER                      0x00000002
#define NDIS_MINIPORT_SG_LIST                         0x00000004
#define NDIS_MINIPORT_SUPPORTS_MEDIA_QUERY            0x00000008
#define NDIS_MINIPORT_INDICATES_PACKETS               0x00000010
#define NDIS_MINIPORT_IGNORE_PACKET_QUEUE             0x00000020
#define NDIS_MINIPORT_IGNORE_REQUEST_QUEUE            0x00000040
#define NDIS_MINIPORT_IGNORE_TOKEN_RING_ERRORS        0x00000080
#define NDIS_MINIPORT_INTERMEDIATE_DRIVER             0x00000100
#define NDIS_MINIPORT_IS_NDIS_5                       0x00000200
#define NDIS_MINIPORT_IS_CO                           0x00000400
#define NDIS_MINIPORT_DESERIALIZE                     0x00000800
#define NDIS_MINIPORT_REQUIRES_MEDIA_POLLING          0x00001000
#define NDIS_MINIPORT_SUPPORTS_MEDIA_SENSE            0x00002000
#define NDIS_MINIPORT_NETBOOT_CARD                    0x00004000
#define NDIS_MINIPORT_PM_SUPPORTED                    0x00008000
#define NDIS_MINIPORT_SUPPORTS_MAC_ADDRESS_OVERWRITE  0x00010000
#define NDIS_MINIPORT_USES_SAFE_BUFFER_APIS           0x00020000
#define NDIS_MINIPORT_HIDDEN                          0x00040000
#define NDIS_MINIPORT_SWENUM                          0x00080000
#define NDIS_MINIPORT_SURPRISE_REMOVE_OK              0x00100000
#define NDIS_MINIPORT_NO_HALT_ON_SUSPEND              0x00200000
#define NDIS_MINIPORT_HARDWARE_DEVICE                 0x00400000
#define NDIS_MINIPORT_SUPPORTS_CANCEL_SEND_PACKETS    0x00800000
#define NDIS_MINIPORT_64BITS_DMA                      0x01000000

/* Full duplex driver */
#define NDIS_MAC_OPTION_FULL_DUPLEX                      0x00000010 /* Deprecated flag */

#define NDIS_MAC_OPTION_EOTX_INDICATION                  0x00000020
#define NDIS_MAC_OPTION_8021P_PRIORITY                   0x00000040
#define NDIS_MAC_OPTION_SUPPORTS_MAC_ADDRESS_OVERWRITE   0x00000080
#define NDIS_MAC_OPTION_RECEIVE_AT_DPC                   0x00000100
#define NDIS_MAC_OPTION_8021Q_VLAN                       0x00000200
#define NDIS_MAC_OPTION_RESERVED                         0x80000000

#define _NDIS_CONTROL_CODE(request, method) \
    CTL_CODE(FILE_DEVICE_PHYSICAL_NETCARD, request, method, FILE_ANY_ACCESS)

#define IOCTL_NDIS_QUERY_GLOBAL_STATS   _NDIS_CONTROL_CODE(0x00, METHOD_OUT_DIRECT) // 0x170002

#define IOCTL_NDIS_RESERVED7            _NDIS_CONTROL_CODE(0x0F, METHOD_OUT_DIRECT) // 0x17003e

/* Hardware status codes (OID_GEN_HARDWARE_STATUS) */
typedef enum _NDIS_HARDWARE_STATUS {
  NdisHardwareStatusReady,
  NdisHardwareStatusInitializing,
  NdisHardwareStatusReset,
  NdisHardwareStatusClosing,
  NdisHardwareStatusNotReady
} NDIS_HARDWARE_STATUS, *PNDIS_HARDWARE_STATUS;

/* OID_GEN_GET_TIME_CAPS */
typedef struct _GEN_GET_TIME_CAPS {
  ULONG Flags;
  ULONG ClockPrecision;
} GEN_GET_TIME_CAPS, *PGEN_GET_TIME_CAPS;

/* OID_GEN_GET_NETCARD_TIME */
typedef struct _GEN_GET_NETCARD_TIME {
  ULONGLONG ReadTime;
} GEN_GET_NETCARD_TIME, *PGEN_GET_NETCARD_TIME;

/* State of the LAN media (OID_GEN_MEDIA_CONNECT_STATUS) */
typedef enum _NDIS_MEDIA_STATE {
  NdisMediaStateConnected,
  NdisMediaStateDisconnected
} NDIS_MEDIA_STATE, *PNDIS_MEDIA_STATE;

#ifndef _NDIS_
typedef int NDIS_STATUS, *PNDIS_STATUS;
#endif

/* OID_GEN_SUPPORTED_GUIDS */
typedef struct _NDIS_GUID {
  GUID Guid;
  union {
    NDIS_OID Oid;
    NDIS_STATUS Status;
  };
  ULONG Size;
  ULONG Flags;
} NDIS_GUID, *PNDIS_GUID;

#define fNDIS_GUID_TO_OID                   0x00000001
#define fNDIS_GUID_TO_STATUS                0x00000002
#define fNDIS_GUID_ANSI_STRING              0x00000004
#define fNDIS_GUID_UNICODE_STRING           0x00000008
#define fNDIS_GUID_ARRAY                    0x00000010
#define fNDIS_GUID_ALLOW_READ               0x00000020
#define fNDIS_GUID_ALLOW_WRITE              0x00000040
#define fNDIS_GUID_METHOD                   0x00000080
#define fNDIS_GUID_NDIS_RESERVED            0x00000100
#define fNDIS_GUID_SUPPORT_COMMON_HEADER    0x00000200

typedef struct _NDIS_PM_PACKET_PATTERN {
  ULONG Priority;
  ULONG Reserved;
  ULONG MaskSize;
  ULONG PatternOffset;
  ULONG PatternSize;
  ULONG PatternFlags;
} NDIS_PM_PACKET_PATTERN, *PNDIS_PM_PACKET_PATTERN;

/* OID_GEN_NETWORK_LAYER_ADDRESSES */
typedef struct _NETWORK_ADDRESS {
  USHORT AddressLength;
  USHORT AddressType;
  UCHAR Address[1];
} NETWORK_ADDRESS, *PNETWORK_ADDRESS;

typedef struct _NETWORK_ADDRESS_LIST {
  LONG AddressCount;
  USHORT AddressType;
  NETWORK_ADDRESS Address[1];
} NETWORK_ADDRESS_LIST, *PNETWORK_ADDRESS_LIST;

/* OID_GEN_TRANSPORT_HEADER_OFFSET */
typedef struct _TRANSPORT_HEADER_OFFSET {
  USHORT ProtocolType;
  USHORT HeaderOffset;
} TRANSPORT_HEADER_OFFSET, *PTRANSPORT_HEADER_OFFSET;

/* OID_GEN_CO_LINK_SPEED / OID_GEN_CO_MINIMUM_LINK_SPEED */
typedef struct _NDIS_CO_LINK_SPEED {
  ULONG Outbound;
  ULONG Inbound;
} NDIS_CO_LINK_SPEED, *PNDIS_CO_LINK_SPEED;

#define NDIS_PACKET_TYPE_DIRECTED               0x00000001
#define NDIS_PACKET_TYPE_MULTICAST              0x00000002
#define NDIS_PACKET_TYPE_ALL_MULTICAST          0x00000004
#define NDIS_PACKET_TYPE_BROADCAST              0x00000008
#define NDIS_PACKET_TYPE_SOURCE_ROUTING         0x00000010
#define NDIS_PACKET_TYPE_PROMISCUOUS            0x00000020
#define NDIS_PACKET_TYPE_SMT                    0x00000040
#define NDIS_PACKET_TYPE_ALL_LOCAL              0x00000080
#define NDIS_PACKET_TYPE_GROUP                  0x00001000
#define NDIS_PACKET_TYPE_ALL_FUNCTIONAL         0x00002000
#define NDIS_PACKET_TYPE_FUNCTIONAL             0x00004000
#define NDIS_PACKET_TYPE_MAC_FRAME              0x00008000
#define NDIS_PACKET_TYPE_NO_LOCAL               0x00010000

#if !defined(NDIS_SUPPORT_NDIS6) || NDIS_SUPPORT_NDIS6

#if ((NDIS_SUPPORT_NDIS61))

#define NDIS_OBJECT_TYPE_MINIPORT_ADAPTER_HARDWARE_ASSIST_ATTRIBUTES    0xAF

#endif
#if ((NDIS_SUPPORT_NDIS620))

#define NDIS_OBJECT_TYPE_RSS_PROCESSOR_INFO                             0xB1

#endif
#if ((NDIS_SUPPORT_NDIS630))

#define NDIS_OBJECT_TYPE_QOS_CAPABILITIES                               0xB5

#define NDIS_OBJECT_TYPE_QOS_PARAMETERS                                 0xB6

#define NDIS_OBJECT_TYPE_QOS_CLASSIFICATION_ELEMENT                     0xB7

#define NDIS_OBJECT_TYPE_SWITCH_OPTIONAL_HANDLERS                       0xB8

#endif

#define NDIS_OBJECT_REVISION_1 1

#if (((NTDDI_VERSION >= NTDDI_VISTA) || NDIS_SUPPORT_NDIS6))

typedef enum _NDIS_INTERRUPT_MODERATION
{
    NdisInterruptModerationUnknown,
    NdisInterruptModerationNotSupported,
    NdisInterruptModerationEnabled,
    NdisInterruptModerationDisabled
} NDIS_INTERRUPT_MODERATION, *PNDIS_INTERRUPT_MODERATION;

#define NDIS_INTERRUPT_MODERATION_CHANGE_NEEDS_RESET            0x00000001

#define NDIS_INTERRUPT_MODERATION_CHANGE_NEEDS_REINITIALIZE     0x00000002

#define NDIS_INTERRUPT_MODERATION_PARAMETERS_REVISION_1    1

typedef struct _NDIS_INTERRUPT_MODERATION_PARAMETERS
{
    NDIS_OBJECT_HEADER Header;
    ULONG Flags;
    NDIS_INTERRUPT_MODERATION InterruptModeration;
}NDIS_INTERRUPT_MODERATION_PARAMETERS, *PNDIS_INTERRUPT_MODERATION_PARAMETERS;

#define NDIS_SIZEOF_INTERRUPT_MODERATION_PARAMETERS_REVISION_1    \
        RTL_SIZEOF_THROUGH_FIELD(NDIS_INTERRUPT_MODERATION_PARAMETERS, InterruptModeration)

#define OID_GEN_RECEIVE_SCALE_CAPABILITIES      0x00010203

#define OID_GEN_RECEIVE_SCALE_PARAMETERS        0x00010204

#define OID_GEN_MAX_LINK_SPEED                  0x00010206

#endif
#if (((NTDDI_VERSION >= NTDDI_WIN10_RS3) || NDIS_SUPPORT_NDIS680))

#define OID_GEN_RECEIVE_SCALE_PARAMETERS_V2     0x00010214

#endif
#if (((NTDDI_VERSION >= NTDDI_VISTA) || NDIS_SUPPORT_NDIS6))

#define OID_GEN_MEDIA_CONNECT_STATUS_EX         0x0001028A

#define OID_GEN_LINK_SPEED_EX                   0x0001028B

#define OID_GEN_MEDIA_DUPLEX_STATE              0x0001028C

#define OID_GEN_BYTES_RCV                       0x00020219

#define OID_GEN_BYTES_XMIT                      0x0002021A

#define OID_GEN_RCV_DISCARDS                    0x0002021B

#define OID_GEN_XMIT_DISCARDS                   0x0002021C

#define OID_802_3_ADD_MULTICAST_ADDRESS         0x01010208

#define OID_802_3_DELETE_MULTICAST_ADDRESS      0x01010209

#endif

#define NDIS_ETH_TYPE_802_1X            0x888e

#define NDIS_ETH_TYPE_802_1Q            0x8100

#if (((NTDDI_VERSION >= NTDDI_WIN7) || NDIS_SUPPORT_NDIS620))

#define OID_PM_PARAMETERS                       0xFD010109

#define OID_PM_ADD_WOL_PATTERN                  0xFD01010A

#define OID_PM_REMOVE_WOL_PATTERN               0xFD01010B

#define OID_PM_ADD_PROTOCOL_OFFLOAD             0xFD01010D

#define OID_PM_REMOVE_PROTOCOL_OFFLOAD          0xFD01010F

#define OID_RECEIVE_FILTER_ALLOCATE_QUEUE               0x00010223

#define OID_RECEIVE_FILTER_FREE_QUEUE                   0x00010224

#define OID_RECEIVE_FILTER_QUEUE_PARAMETERS             0x00010226

#define OID_RECEIVE_FILTER_SET_FILTER                   0x00010227

#define OID_RECEIVE_FILTER_CLEAR_FILTER                 0x00010228

#define OID_RECEIVE_FILTER_QUEUE_ALLOCATION_COMPLETE    0x0001022B

#endif
#if (((NTDDI_VERSION >= NTDDI_WIN8) || NDIS_SUPPORT_NDIS630))

typedef USHORT NDIS_SRIOV_FUNCTION_ID, *PNDIS_SRIOV_FUNCTION_ID;

#define OID_NIC_SWITCH_ALLOCATE_VF                    0x00010245

#define OID_SWITCH_PROPERTY_ADD                       0x00010263

#define OID_SWITCH_PROPERTY_UPDATE                    0x00010264

#define OID_SWITCH_PROPERTY_DELETE                    0x00010265

#define OID_SWITCH_PROPERTY_ENUM                      0x00010266

#define OID_SWITCH_FEATURE_STATUS_QUERY               0x00010267

#define OID_SWITCH_NIC_REQUEST                        0x00010270

#define OID_SWITCH_PORT_PROPERTY_ADD                  0x00010271

#define OID_SWITCH_PORT_PROPERTY_UPDATE               0x00010272

#define OID_SWITCH_PORT_PROPERTY_DELETE               0x00010273

#define OID_SWITCH_PORT_PROPERTY_ENUM                 0x00010274

#define OID_SWITCH_PARAMETERS                         0x00010275

#define OID_SWITCH_PORT_ARRAY                         0x00010276

#define OID_SWITCH_NIC_ARRAY                          0x00010277

#define OID_SWITCH_PORT_CREATE                        0x00010278

#define OID_SWITCH_PORT_DELETE                        0x00010279

#define OID_SWITCH_NIC_CREATE                         0x0001027A

#define OID_SWITCH_NIC_CONNECT                        0x0001027B

#define OID_SWITCH_NIC_DISCONNECT                     0x0001027C

#define OID_SWITCH_NIC_DELETE                         0x0001027D

#define OID_SWITCH_PORT_FEATURE_STATUS_QUERY          0x0001027E

#define OID_SWITCH_PORT_TEARDOWN                      0x0001027F

#define OID_SWITCH_NIC_SAVE                           0x00010290

#define OID_SWITCH_NIC_SAVE_COMPLETE                  0x00010291

#define OID_SWITCH_NIC_RESTORE                        0x00010292

#define OID_SWITCH_NIC_RESTORE_COMPLETE               0x00010293

#define OID_SWITCH_NIC_UPDATED                        0x00010294

#define OID_SWITCH_PORT_UPDATED                       0x00010295

#endif
#if ((NTDDI_VERSION >= NTDDI_WIN10_RS3) || (NDIS_SUPPORT_NDIS680))

#define OID_GEN_RSS_SET_INDIRECTION_TABLE_ENTRIES     0x000102C0

#endif
typedef ULONG NDIS_PORT_NUMBER, *PNDIS_PORT_NUMBER;

typedef enum _NDIS_SUPPORTED_PAUSE_FUNCTIONS {
  NdisPauseFunctionsUnsupported,
  NdisPauseFunctionsSendOnly,
  NdisPauseFunctionsReceiveOnly,
  NdisPauseFunctionsSendAndReceive,
  NdisPauseFunctionsUnknown
} NDIS_SUPPORTED_PAUSE_FUNCTIONS, *PNDIS_SUPPORTED_PAUSE_FUNCTIONS;


#if (((NTDDI_VERSION >= NTDDI_VISTA) || NDIS_SUPPORT_NDIS6))

typedef struct _NDIS_LINK_SPEED
{
    ULONG64     XmitLinkSpeed;
    ULONG64     RcvLinkSpeed;
} NDIS_LINK_SPEED, *PNDIS_LINK_SPEED;

typedef IF_COUNTED_STRING NDIS_IF_COUNTED_STRING, *PNDIS_IF_COUNTED_STRING;

#define NDIS_MAX_PHYS_ADDRESS_LENGTH IF_MAX_PHYS_ADDRESS_LENGTH
typedef IF_PHYSICAL_ADDRESS NDIS_IF_PHYSICAL_ADDRESS, *PNDIS_IF_PHYSICAL_ADDRESS;

#define NDIS_OFFLOAD_PARAMETERS_NO_CHANGE                  0

#define NDIS_OFFLOAD_PARAMETERS_TX_RX_DISABLED             1

#define NDIS_OFFLOAD_PARAMETERS_TX_ENABLED_RX_DISABLED     2

#define NDIS_OFFLOAD_PARAMETERS_RX_ENABLED_TX_DISABLED     3

#define NDIS_OFFLOAD_PARAMETERS_TX_RX_ENABLED              4

#define NDIS_OFFLOAD_PARAMETERS_REVISION_1            1

typedef struct _NDIS_OFFLOAD_PARAMETERS
{
    NDIS_OBJECT_HEADER      Header;
    UCHAR                   IPv4Checksum;
    UCHAR                   TCPIPv4Checksum;
    UCHAR                   UDPIPv4Checksum;
    UCHAR                   TCPIPv6Checksum;
    UCHAR                   UDPIPv6Checksum;
    UCHAR                   LsoV1;
    UCHAR                   IPsecV1;
    UCHAR                   LsoV2IPv4;
    UCHAR                   LsoV2IPv6;
    UCHAR                   TcpConnectionIPv4;
    UCHAR                   TcpConnectionIPv6;
    ULONG                   Flags;
#if (NDIS_SUPPORT_NDIS61)
    UCHAR                   IPsecV2;
    UCHAR                   IPsecV2IPv4;
#endif
#if (NDIS_SUPPORT_NDIS630)
    struct
    {
        UCHAR               RscIPv4;
        UCHAR               RscIPv6;
    };
    struct
    {
        UCHAR               EncapsulatedPacketTaskOffload;
        UCHAR               EncapsulationTypes;
    };
#endif
#if (NDIS_SUPPORT_NDIS650)
    union _ENCAPSULATION_PROTOCOL_PARAMETERS {
        struct _VXLAN_PARAMETERS {
            USHORT VxlanUDPPortNumber;
        } VxlanParameters;
        ULONG Value;
    } EncapsulationProtocolParameters;
#endif
#if (NDIS_SUPPORT_NDIS683)
    struct
    {
        UCHAR               IPv4;
        UCHAR               IPv6;
    } UdpSegmentation;
#endif
#if (NDIS_SUPPORT_NDIS689)
    struct
    {
        UCHAR               Enabled;
    } UdpRsc;
#endif
} NDIS_OFFLOAD_PARAMETERS, *PNDIS_OFFLOAD_PARAMETERS;

#define NDIS_OFFLOAD_NOT_SUPPORTED              0
#define NDIS_OFFLOAD_SUPPORTED                  1

#define NDIS_OFFLOAD_SET_NO_CHANGE              0
#define NDIS_OFFLOAD_SET_ON                     1
#define NDIS_OFFLOAD_SET_OFF                    2

#define NDIS_ENCAPSULATION_NOT_SUPPORTED        0x00000000
#define NDIS_ENCAPSULATION_NULL                 0x00000001
#define NDIS_ENCAPSULATION_IEEE_802_3           0x00000002
#define NDIS_ENCAPSULATION_IEEE_802_3_P_AND_Q   0x00000004
#define NDIS_ENCAPSULATION_IEEE_802_3_P_AND_Q_IN_OOB 0x00000008
#define NDIS_ENCAPSULATION_IEEE_LLC_SNAP_ROUTED 0x00000010

#define NDIS_OFFLOAD_REVISION_1    1

#if ((NDIS_SUPPORT_NDIS630))

#define NDIS_OFFLOAD_REVISION_3    3

#endif

#define NDIS_SIZEOF_NDIS_OFFLOAD_REVISION_1   RTL_SIZEOF_THROUGH_FIELD(NDIS_OFFLOAD, Flags)

#if ((NDIS_SUPPORT_NDIS620))

typedef enum _NDIS_PM_WOL_PACKET
{
    NdisPMWoLPacketUnspecified,
    NdisPMWoLPacketBitmapPattern,
    NdisPMWoLPacketMagicPacket,
    NdisPMWoLPacketIPv4TcpSyn,
    NdisPMWoLPacketIPv6TcpSyn,
    NdisPMWoLPacketEapolRequestIdMessage,
    NdisPMWoLPacketMaximum
}NDIS_PM_WOL_PACKET, *PNDIS_PM_WOL_PACKET;

#define NDIS_PM_MAX_STRING_SIZE 64

typedef struct _NDIS_PM_COUNTED_STRING
{
    USHORT      Length;
    WCHAR       String[NDIS_PM_MAX_STRING_SIZE + 1];
} NDIS_PM_COUNTED_STRING, *PNDIS_PM_COUNTED_STRING;

#if ((NDIS_SUPPORT_NDIS630))

#define NDIS_PM_CAPABILITIES_REVISION_2              2

#endif

typedef struct _NDIS_PM_CAPABILITIES
{
    NDIS_OBJECT_HEADER      Header;
    ULONG                   Flags;
    ULONG                   SupportedWoLPacketPatterns;
    ULONG                   NumTotalWoLPatterns;
    ULONG                   MaxWoLPatternSize;
    ULONG                   MaxWoLPatternOffset;
    ULONG                   MaxWoLPacketSaveBuffer;
    ULONG                   SupportedProtocolOffloads;
    ULONG                   NumArpOffloadIPv4Addresses;
    ULONG                   NumNSOffloadIPv6Addresses;
    NDIS_DEVICE_POWER_STATE MinMagicPacketWakeUp;
    NDIS_DEVICE_POWER_STATE MinPatternWakeUp;
    NDIS_DEVICE_POWER_STATE MinLinkChangeWakeUp;
#if (NDIS_SUPPORT_NDIS630)
    ULONG                   SupportedWakeUpEvents;
    ULONG                   MediaSpecificWakeUpEvents;
#endif
}NDIS_PM_CAPABILITIES, *PNDIS_PM_CAPABILITIES;

#define NDIS_SIZEOF_NDIS_PM_CAPABILITIES_REVISION_1     \
    RTL_SIZEOF_THROUGH_FIELD(NDIS_PM_CAPABILITIES, MinLinkChangeWakeUp)

#if ((NDIS_SUPPORT_NDIS630))

#define NDIS_SIZEOF_NDIS_PM_CAPABILITIES_REVISION_2     \
    RTL_SIZEOF_THROUGH_FIELD(NDIS_PM_CAPABILITIES, MediaSpecificWakeUpEvents)

#endif

typedef struct _NDIS_PM_WOL_PATTERN
{
    NDIS_OBJECT_HEADER          Header;
    ULONG                       Flags;
    ULONG                       Priority;
    NDIS_PM_WOL_PACKET          WoLPacketType;
    NDIS_PM_COUNTED_STRING      FriendlyName;
    ULONG                       PatternId;
    ULONG                       NextWoLPatternOffset;
    union _WOL_PATTERN
    {
        struct _IPV4_TCP_SYN_WOL_PACKET_PARAMETERS
        {
            ULONG   Flags;
            UCHAR   IPv4SourceAddress[4];
            UCHAR   IPv4DestAddress[4];
            USHORT  TCPSourcePortNumber;
            USHORT  TCPDestPortNumber;
        }IPv4TcpSynParameters;
        struct _IPV6_TCP_SYN_WOL_PACKET_PARAMETERS
        {
            ULONG   Flags;
            UCHAR   IPv6SourceAddress[16];
            UCHAR   IPv6DestAddress[16];
            USHORT  TCPSourcePortNumber;
            USHORT  TCPDestPortNumber;
        }IPv6TcpSynParameters;
        struct _EAPOL_REQUEST_ID_MESSAGE_WOL_PACKET_PARAMETERS
        {
            ULONG   Flags;
        } EapolRequestIdMessageParameters;
        struct _WOL_BITMAP_PATTERN
        {
            ULONG   Flags;
            ULONG   MaskOffset;
            ULONG   MaskSize;
            ULONG   PatternOffset;
            ULONG   PatternSize;
        }WoLBitMapPattern;
    }WoLPattern;
}NDIS_PM_WOL_PATTERN, *PNDIS_PM_WOL_PATTERN;

#define NDIS_RECEIVE_FILTER_MAC_HEADER_SUPPORTED            0x00000001

#define NDIS_RECEIVE_FILTER_MAC_HEADER_DEST_ADDR_SUPPORTED      0x00000001

#define NDIS_RECEIVE_FILTER_MAC_HEADER_VLAN_ID_SUPPORTED        0x00000008

#define NDIS_RECEIVE_FILTER_TEST_HEADER_FIELD_EQUAL_SUPPORTED               0x00000001

#define NDIS_RECEIVE_FILTER_VM_QUEUE_SUPPORTED                      0x00000002

#define NDIS_RECEIVE_FILTER_LOOKAHEAD_SPLIT_SUPPORTED               0x00000004

#if ((NDIS_SUPPORT_NDIS630))

#define NDIS_RECEIVE_FILTER_DYNAMIC_PROCESSOR_AFFINITY_CHANGE_SUPPORTED 0x00000008

#endif

#define NDIS_RECEIVE_FILTER_VMQ_FILTERS_ENABLED                     0x00000001

#define NDIS_RECEIVE_FILTER_VM_QUEUES_ENABLED                       0x00000001

typedef struct _NDIS_RECEIVE_FILTER_CAPABILITIES
{
    _In_  NDIS_OBJECT_HEADER          Header;
    _In_  ULONG                       Flags;
    _In_  ULONG                       EnabledFilterTypes;
    _In_  ULONG                       EnabledQueueTypes;
    _In_  ULONG                       NumQueues;
    _In_  ULONG                       SupportedQueueProperties;
    _In_  ULONG                       SupportedFilterTests;
    _In_  ULONG                       SupportedHeaders;
    _In_  ULONG                       SupportedMacHeaderFields;
    _In_  ULONG                       MaxMacHeaderFilters;
    _In_  ULONG                       MaxQueueGroups;
    _In_  ULONG                       MaxQueuesPerQueueGroup;
    _In_  ULONG                       MinLookaheadSplitSize;
    _In_  ULONG                       MaxLookaheadSplitSize;
#if (NDIS_SUPPORT_NDIS630)
    _In_  ULONG                       SupportedARPHeaderFields;
    _In_  ULONG                       SupportedIPv4HeaderFields;
    _In_  ULONG                       SupportedIPv6HeaderFields;
    _In_  ULONG                       SupportedUdpHeaderFields;
    _In_  ULONG                       MaxFieldTestsPerPacketCoalescingFilter;
    _In_  ULONG                       MaxPacketCoalescingFilters;
    _In_  ULONG                       NdisReserved;
#endif
} NDIS_RECEIVE_FILTER_CAPABILITIES, *PNDIS_RECEIVE_FILTER_CAPABILITIES;

#define NDIS_SIZEOF_RECEIVE_FILTER_CAPABILITIES_REVISION_1     \
    RTL_SIZEOF_THROUGH_FIELD(NDIS_RECEIVE_FILTER_CAPABILITIES, MaxLookaheadSplitSize)

typedef struct _NDIS_NIC_SWITCH_CAPABILITIES
{
    _In_  NDIS_OBJECT_HEADER          Header;
    _In_  ULONG                       Flags;
    _In_  ULONG                       NdisReserved1;
    _In_  ULONG                       NumTotalMacAddresses;
    _In_  ULONG                       NumMacAddressesPerPort;
    _In_  ULONG                       NumVlansPerPort;
    _In_  ULONG                       NdisReserved2;
    _In_  ULONG                       NdisReserved3;
#if (NDIS_SUPPORT_NDIS630)
    _In_  ULONG                       NicSwitchCapabilities;
    _In_  ULONG                       MaxNumSwitches;
    _In_  ULONG                       MaxNumVPorts;
    _In_  ULONG                       NdisReserved4;
    _In_  ULONG                       MaxNumVFs;
    _In_  ULONG                       MaxNumQueuePairs;
    _In_  ULONG                       NdisReserved5;
    _In_  ULONG                       NdisReserved6;
    _In_  ULONG                       NdisReserved7;
    _In_  ULONG                       MaxNumQueuePairsPerNonDefaultVPort;
    _In_  ULONG                       NdisReserved8;
    _In_  ULONG                       NdisReserved9;
    _In_  ULONG                       NdisReserved10;
    _In_  ULONG                       NdisReserved11;
    _In_  ULONG                       NdisReserved12;
    _In_  ULONG                       MaxNumMacAddresses;
    _In_  ULONG                       NdisReserved13;
    _In_  ULONG                       NdisReserved14;
    _In_  ULONG                       NdisReserved15;
    _In_  ULONG                       NdisReserved16;
    _In_  ULONG                       NdisReserved17;
#endif
#if (NDIS_SUPPORT_NDIS660)
    _In_  ULONG                       MaxNumRssCapableNonDefaultPFVPorts;
    _In_  ULONG                       NumberOfIndirectionTableEntriesForDefaultVPort;
    _In_  ULONG                       NumberOfIndirectionTableEntriesPerNonDefaultPFVPort;
    _In_  ULONG                       MaxNumQueuePairsForDefaultVPort;
#endif
}NDIS_NIC_SWITCH_CAPABILITIES, *PNDIS_NIC_SWITCH_CAPABILITIES;

#define NDIS_SIZEOF_NIC_SWITCH_CAPABILITIES_REVISION_1     \
    RTL_SIZEOF_THROUGH_FIELD(NDIS_NIC_SWITCH_CAPABILITIES, NdisReserved3)

typedef ULONG NDIS_RECEIVE_QUEUE_ID, *PNDIS_RECEIVE_QUEUE_ID;

typedef ULONG NDIS_RECEIVE_QUEUE_GROUP_ID, *PNDIS_RECEIVE_QUEUE_GROUP_ID;

#define NDIS_DEFAULT_RECEIVE_QUEUE_ID               0

typedef ULONG NDIS_RECEIVE_FILTER_ID, *PNDIS_RECEIVE_FILTER_ID;

typedef enum _NDIS_RECEIVE_FILTER_TYPE
{
    NdisReceiveFilterTypeUndefined,
    NdisReceiveFilterTypeVMQueue,
    NdisReceiveFilterTypePacketCoalescing,
    NdisReceiveFilterTypeMaximum
}NDIS_RECEIVE_FILTER_TYPE, *PNDIS_RECEIVE_FILTER_TYPE;

typedef enum _NDIS_FRAME_HEADER
{
    NdisFrameHeaderUndefined,
    NdisFrameHeaderMac,
    NdisFrameHeaderArp,
    NdisFrameHeaderIPv4,
    NdisFrameHeaderIPv6,
    NdisFrameHeaderUdp,
    NdisFrameHeaderMaximum
}NDIS_FRAME_HEADER, *PNDIS_FRAME_HEADER;

typedef enum _NDIS_MAC_HEADER_FIELD
{
    NdisMacHeaderFieldUndefined,
    NdisMacHeaderFieldDestinationAddress,
    NdisMacHeaderFieldSourceAddress,
    NdisMacHeaderFieldProtocol,
    NdisMacHeaderFieldVlanId,
    NdisMacHeaderFieldPriority,
    NdisMacHeaderFieldPacketType,
    NdisMacHeaderFieldMaximum
}NDIS_MAC_HEADER_FIELD, *PNDIS_MAC_HEADER_FIELD;

typedef enum _NDIS_ARP_HEADER_FIELD
{
    NdisARPHeaderFieldUndefined,
    NdisARPHeaderFieldOperation,
    NdisARPHeaderFieldSPA,
    NdisARPHeaderFieldTPA,
    NdisARPHeaderFieldMaximum
} NDIS_ARP_HEADER_FIELD, *PNDIS_ARP_HEADER_FIELD;

typedef enum _NDIS_IPV4_HEADER_FIELD
{
    NdisIPv4HeaderFieldUndefined,
    NdisIPv4HeaderFieldProtocol,
    NdisIPv4HeaderFieldMaximum
}NDIS_IPV4_HEADER_FIELD, *PNDIS_IPV4_HEADER_FIELD;

typedef enum _NDIS_IPV6_HEADER_FIELD
{
    NdisIPv6HeaderFieldUndefined,
    NdisIPv6HeaderFieldProtocol,
    NdisIPv6HeaderFieldMaximum
}NDIS_IPV6_HEADER_FIELD, *PNDIS_IPV6_HEADER_FIELD;

typedef enum _NDIS_UDP_HEADER_FIELD
{
    NdisUdpHeaderFieldUndefined,
    NdisUdpHeaderFieldDestinationPort,
    NdisUdpHeaderFieldMaximum
}NDIS_UDP_HEADER_FIELD, *PNDIS_UDP_HEADER_FIELD;

typedef enum _NDIS_RECEIVE_FILTER_TEST
{
    NdisReceiveFilterTestUndefined,
    NdisReceiveFilterTestEqual,
    NdisReceiveFilterTestMaskEqual,
    NdisReceiveFilterTestNotEqual,
    NdisReceiveFilterTestMaximum
}NDIS_RECEIVE_FILTER_TEST, *PNDIS_RECEIVE_FILTER_TEST;

#define NDIS_RECEIVE_FILTER_FIELD_MAC_HEADER_VLAN_UNTAGGED_OR_ZERO  0x00000001

typedef struct _NDIS_RECEIVE_FILTER_FIELD_PARAMETERS
{
    _In_ NDIS_OBJECT_HEADER       Header;
    _In_ ULONG                    Flags;
    _In_ NDIS_FRAME_HEADER        FrameHeader;
    _In_ NDIS_RECEIVE_FILTER_TEST ReceiveFilterTest;
    _In_ union _HEADER_FIELD
    {
        NDIS_MAC_HEADER_FIELD       MacHeaderField;
        NDIS_ARP_HEADER_FIELD       ArpHeaderField;
        NDIS_IPV4_HEADER_FIELD      IPv4HeaderField;
        NDIS_IPV6_HEADER_FIELD      IPv6HeaderField;
        NDIS_UDP_HEADER_FIELD       UdpHeaderField;
    }HeaderField;
    _In_ union _FIELD_VALUE
    {
        UCHAR               FieldByteValue;
        USHORT              FieldShortValue;
        ULONG               FieldLongValue;
        ULONG64             FieldLong64Value;
        UCHAR               FieldByteArrayValue[16];
    }FieldValue;
    _In_ union _RESULT_VALUE
    {
        UCHAR               ResultByteValue;
        USHORT              ResultShortValue;
        ULONG               ResultLongValue;
        ULONG64             ResultLong64Value;
        UCHAR               ResultByteArrayValue[16];
    }ResultValue;
}NDIS_RECEIVE_FILTER_FIELD_PARAMETERS, *PNDIS_RECEIVE_FILTER_FIELD_PARAMETERS;

#define NDIS_RECEIVE_FILTER_PARAMETERS_REVISION_1       1

typedef
  _When_(FieldParametersArrayNumElements == 0, _Struct_size_bytes_(sizeof(NDIS_RECEIVE_FILTER_PARAMETERS)))
  _When_(FieldParametersArrayNumElements > 0, _Struct_size_bytes_(FieldParametersArrayOffset +FieldParametersArrayNumElements*FieldParametersArrayElementSize))
    struct _NDIS_RECEIVE_FILTER_PARAMETERS
{
    _In_    NDIS_OBJECT_HEADER                     Header;
    _In_    ULONG                                  Flags;
    _In_    NDIS_RECEIVE_FILTER_TYPE               FilterType;
    _In_    NDIS_RECEIVE_QUEUE_ID                  QueueId;
    _Inout_ NDIS_RECEIVE_FILTER_ID                 FilterId;
    _In_    ULONG                                  FieldParametersArrayOffset;
    _In_    ULONG                                  FieldParametersArrayNumElements;
    _In_    ULONG                                  FieldParametersArrayElementSize;
    _In_    ULONG                                  RequestedFilterIdBitCount;
#if (NDIS_SUPPORT_NDIS630)
    _In_    ULONG                                  MaxCoalescingDelay;
    _In_    NDIS_NIC_SWITCH_VPORT_ID               VPortId;
#endif
}NDIS_RECEIVE_FILTER_PARAMETERS, *PNDIS_RECEIVE_FILTER_PARAMETERS;

#define NDIS_SIZEOF_RECEIVE_FILTER_PARAMETERS_REVISION_1     \
    RTL_SIZEOF_THROUGH_FIELD(NDIS_RECEIVE_FILTER_PARAMETERS, RequestedFilterIdBitCount)

#define NDIS_RECEIVE_FILTER_CLEAR_PARAMETERS_REVISION_1       1

typedef struct _NDIS_RECEIVE_FILTER_CLEAR_PARAMETERS
{
    _In_ NDIS_OBJECT_HEADER                           Header;
    _In_ ULONG                                        Flags;
    _In_    NDIS_RECEIVE_QUEUE_ID                     QueueId;
    _In_ NDIS_RECEIVE_FILTER_ID                       FilterId;
}NDIS_RECEIVE_FILTER_CLEAR_PARAMETERS, *PNDIS_RECEIVE_FILTER_CLEAR_PARAMETERS;

#define NDIS_SIZEOF_RECEIVE_FILTER_CLEAR_PARAMETERS_REVISION_1     \
    RTL_SIZEOF_THROUGH_FIELD(NDIS_RECEIVE_FILTER_CLEAR_PARAMETERS, FilterId)

typedef enum _NDIS_RECEIVE_QUEUE_TYPE
{
    NdisReceiveQueueTypeUnspecified,
    NdisReceiveQueueTypeVMQueue,
    NdisReceiveQueueTypeMaximum
}NDIS_RECEIVE_QUEUE_TYPE, *PNDIS_RECEIVE_QUEUE_TYPE;

#define NDIS_RECEIVE_QUEUE_PARAMETERS_LOOKAHEAD_SPLIT_REQUIRED                  0x00000002

#define NDIS_RECEIVE_QUEUE_PARAMETERS_FLAGS_CHANGED                             0x00010000

#define NDIS_RECEIVE_QUEUE_PARAMETERS_PROCESSOR_AFFINITY_CHANGED                0x00020000

#define NDIS_RECEIVE_QUEUE_PARAMETERS_SUGGESTED_RECV_BUFFER_NUMBERS_CHANGED     0x00040000

#define NDIS_RECEIVE_QUEUE_PARAMETERS_CHANGE_MASK                               0xFFFF0000

typedef NDIS_IF_COUNTED_STRING NDIS_QUEUE_NAME, *PNDIS_QUEUE_NAME;

typedef NDIS_IF_COUNTED_STRING NDIS_VM_NAME, *PNDIS_VM_NAME;

typedef NDIS_IF_COUNTED_STRING NDIS_VM_FRIENDLYNAME, *PNDIS_VM_FRIENDLYNAME;

typedef NDIS_IF_COUNTED_STRING NDIS_SWITCH_NAME, *PNDIS_SWITCH_NAME;

typedef NDIS_IF_COUNTED_STRING NDIS_SWITCH_FRIENDLYNAME, *PNDIS_SWITCH_FRIENDLYNAME;

typedef NDIS_IF_COUNTED_STRING NDIS_SWITCH_PORT_NAME, *PNDIS_SWITCH_PORT_NAME;

typedef NDIS_IF_COUNTED_STRING NDIS_SWITCH_PORT_FRIENDLYNAME, *PNDIS_SWITCH_PORT_FRIENDLYNAME;

typedef NDIS_IF_COUNTED_STRING NDIS_SWITCH_NIC_NAME, *PNDIS_SWITCH_NIC_NAME;

typedef NDIS_IF_COUNTED_STRING NDIS_SWITCH_NIC_FRIENDLYNAME, *PNDIS_SWITCH_NIC_FRIENDLYNAME;

typedef NDIS_IF_COUNTED_STRING NDIS_SWITCH_EXTENSION_FRIENDLYNAME, *PNDIS_SWITCH_EXTENSION_FRIENDLYNAME;

#if ((NDIS_SUPPORT_NDIS650))

typedef ULONG NDIS_QOS_SQ_ID, *PNDIS_QOS_SQ_ID;

#endif

#define NDIS_RECEIVE_QUEUE_PARAMETERS_REVISION_1       1

typedef struct _NDIS_RECEIVE_QUEUE_PARAMETERS
{
    _In_    NDIS_OBJECT_HEADER               Header;
    _Inout_ ULONG                            Flags;
    _In_    NDIS_RECEIVE_QUEUE_TYPE          QueueType;
    _Inout_ NDIS_RECEIVE_QUEUE_ID            QueueId;
    _Inout_ NDIS_RECEIVE_QUEUE_GROUP_ID      QueueGroupId;
    _In_    GROUP_AFFINITY                   ProcessorAffinity;
    _In_    ULONG                            NumSuggestedReceiveBuffers;
   _Out_    ULONG                            MSIXTableEntry;
    _In_    ULONG                            LookaheadSize;
    _In_    NDIS_VM_NAME                     VmName;
    _In_    NDIS_QUEUE_NAME                  QueueName;
#if (NDIS_SUPPORT_NDIS630)
    _In_    ULONG                            PortId;
    _Out_   ULONG                            InterruptCoalescingDomainId;
#endif
#if (NDIS_SUPPORT_NDIS650)
    _In_    NDIS_QOS_SQ_ID                   QosSqId;
#endif
}NDIS_RECEIVE_QUEUE_PARAMETERS, *PNDIS_RECEIVE_QUEUE_PARAMETERS;

#define NDIS_SIZEOF_RECEIVE_QUEUE_PARAMETERS_REVISION_1     \
    RTL_SIZEOF_THROUGH_FIELD(NDIS_RECEIVE_QUEUE_PARAMETERS, QueueName)

#define NDIS_RECEIVE_QUEUE_FREE_PARAMETERS_REVISION_1       1

typedef struct _NDIS_RECEIVE_QUEUE_FREE_PARAMETERS
{
    _In_ NDIS_OBJECT_HEADER                   Header;
    _In_ ULONG                                Flags;
    _In_ NDIS_RECEIVE_QUEUE_ID                QueueId;
}NDIS_RECEIVE_QUEUE_FREE_PARAMETERS, *PNDIS_RECEIVE_QUEUE_FREE_PARAMETERS;

#define NDIS_SIZEOF_RECEIVE_QUEUE_FREE_PARAMETERS_REVISION_1     \
    RTL_SIZEOF_THROUGH_FIELD(NDIS_RECEIVE_QUEUE_FREE_PARAMETERS, QueueId)

typedef enum _NDIS_RECEIVE_QUEUE_OPERATIONAL_STATE
{
    NdisReceiveQueueOperationalStateUndefined,
    NdisReceiveQueueOperationalStateRunning,
    NdisReceiveQueueOperationalStatePaused,
    NdisReceiveQueueOperationalStateDmaStopped,
    NdisReceiveQueueOperationalStateMaximum
}NDIS_RECEIVE_QUEUE_OPERATIONAL_STATE, *PNDIS_RECEIVE_QUEUE_OPERATIONAL_STATE;

typedef struct _NDIS_RECEIVE_FILTER_INFO
{
    NDIS_OBJECT_HEADER                       Header;
    ULONG                                    Flags;
    NDIS_RECEIVE_FILTER_TYPE                 FilterType;
    NDIS_RECEIVE_FILTER_ID                   FilterId;
}NDIS_RECEIVE_FILTER_INFO, *PNDIS_RECEIVE_FILTER_INFO;

typedef struct _NDIS_RECEIVE_QUEUE_ALLOCATION_COMPLETE_PARAMETERS
{
    _In_  NDIS_OBJECT_HEADER      Header;
    _In_  ULONG                   Flags;
    _In_  NDIS_RECEIVE_QUEUE_ID   QueueId;
    _Out_ NDIS_STATUS             CompletionStatus;
}NDIS_RECEIVE_QUEUE_ALLOCATION_COMPLETE_PARAMETERS, *PNDIS_RECEIVE_QUEUE_ALLOCATION_COMPLETE_PARAMETERS;

#define NDIS_RECEIVE_QUEUE_ALLOCATION_COMPLETE_ARRAY_REVISION_1         1

typedef
        _When_(NumElements >= 1,
            _Struct_size_bytes_(FirstElementOffset + NumElements*ElementSize))
        _When_(NumElements == 0,
            _Struct_size_bytes_(sizeof(NDIS_RECEIVE_QUEUE_ALLOCATION_COMPLETE_ARRAY)))
    struct _NDIS_RECEIVE_QUEUE_ALLOCATION_COMPLETE_ARRAY
{
    NDIS_OBJECT_HEADER                          Header;
    ULONG                                       Flags;
    ULONG                                       FirstElementOffset;
    ULONG                                       NumElements;
    ULONG                                       ElementSize;
}NDIS_RECEIVE_QUEUE_ALLOCATION_COMPLETE_ARRAY, *PNDIS_RECEIVE_QUEUE_ALLOCATION_COMPLETE_ARRAY;

#define NDIS_SIZEOF_RECEIVE_QUEUE_ALLOCATION_COMPLETE_ARRAY_REVISION_1 \
    RTL_SIZEOF_THROUGH_FIELD(NDIS_RECEIVE_QUEUE_ALLOCATION_COMPLETE_ARRAY, ElementSize)

#endif
#if (((NTDDI_VERSION >= NTDDI_VISTA) || NDIS_SUPPORT_NDIS6))

#define NDIS_RSS_CAPS_HASH_TYPE_TCP_IPV4                    0x00000100
#define NDIS_RSS_CAPS_HASH_TYPE_TCP_IPV6                    0x00000200
#define NDIS_RSS_CAPS_HASH_TYPE_TCP_IPV6_EX                 0x00000400
#if (NDIS_SUPPORT_NDIS680)
#define NDIS_RSS_CAPS_HASH_TYPE_UDP_IPV4                    0x00000800
#define NDIS_RSS_CAPS_HASH_TYPE_UDP_IPV6                    0x00001000
#define NDIS_RSS_CAPS_HASH_TYPE_UDP_IPV6_EX                 0x00002000
#endif

#define NDIS_RSS_CAPS_MESSAGE_SIGNALED_INTERRUPTS           0x01000000
#define NDIS_RSS_CAPS_CLASSIFICATION_AT_ISR                 0x02000000
#define NDIS_RSS_CAPS_CLASSIFICATION_AT_DPC                 0x04000000
#if NDIS_SUPPORT_NDIS620
#define NDIS_RSS_CAPS_USING_MSI_X                           0x08000000
#endif
#if (NDIS_SUPPORT_NDIS630)
#define NDIS_RSS_CAPS_RSS_AVAILABLE_ON_PORTS                0x10000000
#define NDIS_RSS_CAPS_SUPPORTS_MSI_X                        0x20000000
#endif
#if (NDIS_SUPPORT_NDIS680)
#define NDIS_RSS_CAPS_SUPPORTS_INDEPENDENT_ENTRY_MOVE       0x40000000
#endif

typedef ULONG NDIS_RSS_CAPS_FLAGS;

typedef struct _NDIS_RECEIVE_SCALE_CAPABILITIES
{
    NDIS_OBJECT_HEADER    Header;
    NDIS_RSS_CAPS_FLAGS   CapabilitiesFlags;
    ULONG                 NumberOfInterruptMessages;
    ULONG                 NumberOfReceiveQueues;
    #if (NDIS_SUPPORT_NDIS630)
    USHORT                NumberOfIndirectionTableEntries;
    #endif
} NDIS_RECEIVE_SCALE_CAPABILITIES, *PNDIS_RECEIVE_SCALE_CAPABILITIES;

#define NdisHashFunctionToeplitz                0x00000001
#define NdisHashFunctionReserved1               0x00000002
#define NdisHashFunctionReserved2               0x00000004
#define NdisHashFunctionReserved3               0x00000008

#define NDIS_HASH_FUNCTION_MASK                 0x000000FF

#define NDIS_HASH_TYPE_MASK                     0x00FFFF00

#define NDIS_HASH_IPV4                          0x00000100
#define NDIS_HASH_TCP_IPV4                      0x00000200
#define NDIS_HASH_IPV6                          0x00000400
#define NDIS_HASH_IPV6_EX                       0x00000800
#define NDIS_HASH_TCP_IPV6                      0x00001000
#define NDIS_HASH_TCP_IPV6_EX                   0x00002000
#if (NDIS_SUPPORT_NDIS680)
#define NDIS_HASH_UDP_IPV4                      0x00004000
#define NDIS_HASH_UDP_IPV6                      0x00008000
#define NDIS_HASH_UDP_IPV6_EX                   0x00010000
#endif

typedef ULONG NDIS_HASH_FLAGS;

#define NDIS_RSS_HASH_FUNC_FROM_HASH_INFO(_HashInfo) ((_HashInfo) & (NDIS_HASH_FUNCTION_MASK))
#define NDIS_RSS_HASH_TYPE_FROM_HASH_INFO(_HashInfo) ((_HashInfo) & (NDIS_HASH_TYPE_MASK))
#define NDIS_RSS_HASH_INFO_FROM_TYPE_AND_FUNC(_HashType, _HashFunction) ((_HashType) | (_HashFunction))

#define NDIS_RSS_PARAM_FLAG_BASE_CPU_UNCHANGED              0x0001

#define NDIS_RSS_PARAM_FLAG_HASH_INFO_UNCHANGED             0x0002

#define NDIS_RSS_PARAM_FLAG_ITABLE_UNCHANGED                0x0004

#define NDIS_RSS_PARAM_FLAG_HASH_KEY_UNCHANGED              0x0008

#define NDIS_RSS_PARAM_FLAG_DISABLE_RSS                     0x0010

#define NDIS_RECEIVE_SCALE_PARAMETERS_REVISION_1     1

typedef struct _NDIS_RECEIVE_SCALE_PARAMETERS
{
    NDIS_OBJECT_HEADER      Header;
    USHORT                  Flags;
    USHORT                  BaseCpuNumber;
    ULONG                   HashInformation;
    USHORT                  IndirectionTableSize;
    ULONG                   IndirectionTableOffset;
    USHORT                  HashSecretKeySize;
    ULONG                   HashSecretKeyOffset;
#if NDIS_SUPPORT_NDIS620
    ULONG                   ProcessorMasksOffset;
    ULONG                   NumberOfProcessorMasks;
    ULONG                   ProcessorMasksEntrySize;
#endif
#if NDIS_SUPPORT_NDIS660
    PROCESSOR_NUMBER        DefaultProcessorNumber;
#endif
} NDIS_RECEIVE_SCALE_PARAMETERS, *PNDIS_RECEIVE_SCALE_PARAMETERS;

#define NDIS_SIZEOF_RECEIVE_SCALE_PARAMETERS_REVISION_1    \
        RTL_SIZEOF_THROUGH_FIELD(NDIS_RECEIVE_SCALE_PARAMETERS, HashSecretKeyOffset)

#define NDIS_RSS_INDIRECTION_TABLE_MAX_SIZE_REVISION_1      128
#define NDIS_RSS_HASH_SECRET_KEY_MAX_SIZE_REVISION_1        40

#if (NDIS_SUPPORT_NDIS620)

#define NDIS_SIZEOF_RECEIVE_SCALE_PARAMETERS_REVISION_2    \
        RTL_SIZEOF_THROUGH_FIELD(NDIS_RECEIVE_SCALE_PARAMETERS, ProcessorMasksEntrySize)

#define NDIS_RSS_INDIRECTION_TABLE_MAX_SIZE_REVISION_2      (128*sizeof(PROCESSOR_NUMBER))
#define NDIS_RSS_HASH_SECRET_KEY_MAX_SIZE_REVISION_2        40

#endif
#if (NDIS_SUPPORT_NDIS660)

#define NDIS_SIZEOF_RECEIVE_SCALE_PARAMETERS_REVISION_3    \
        RTL_SIZEOF_THROUGH_FIELD(NDIS_RECEIVE_SCALE_PARAMETERS, DefaultProcessorNumber)

#define NDIS_RSS_INDIRECTION_TABLE_MAX_SIZE_REVISION_3      (128*sizeof(PROCESSOR_NUMBER))
#define NDIS_RSS_HASH_SECRET_KEY_MAX_SIZE_REVISION_3        40

#endif
#if (NDIS_SUPPORT_NDIS680)

#define NDIS_RECEIVE_SCALE_PARAM_ENABLE_RSS                 0x00000001

#define NDIS_RECEIVE_SCALE_PARAM_HASH_INFO_CHANGED          0x00000002

#define NDIS_RECEIVE_SCALE_PARAM_HASH_KEY_CHANGED           0x00000004

#define NDIS_RECEIVE_SCALE_PARAM_NUMBER_OF_QUEUES_CHANGED   0x00000008

#define NDIS_RECEIVE_SCALE_PARAM_NUMBER_OF_ENTRIES_CHANGED  0x00000010

typedef struct _NDIS_RECEIVE_SCALE_PARAMETERS_V2
{
    NDIS_OBJECT_HEADER      Header;
    ULONG                   Flags;
    ULONG                   HashInformation;
    ULONG                   HashSecretKeySize;
    ULONG                   HashSecretKeyOffset;
    ULONG                   NumberOfQueues;
    ULONG                   NumberOfIndirectionTableEntries;
} NDIS_RECEIVE_SCALE_PARAMETERS_V2, *PNDIS_RECEIVE_SCALE_PARAMETERS_V2;

#define NDIS_SIZEOF_RECEIVE_SCALE_PARAMETERS_V2_REVISION_1 \
        RTL_SIZEOF_THROUGH_FIELD(NDIS_RECEIVE_SCALE_PARAMETERS_V2, \
                                 NumberOfIndirectionTableEntries)

#define NDIS_RSS_SET_INDIRECTION_ENTRY_FLAG_PRIMARY_PROCESSOR   0x00000001

#define NDIS_RSS_SET_INDIRECTION_ENTRY_FLAG_DEFAULT_PROCESSOR   0x00000002

typedef struct _NDIS_RSS_SET_INDIRECTION_ENTRY
{
    NDIS_NIC_SWITCH_ID          SwitchId;
    NDIS_NIC_SWITCH_VPORT_ID    VPortId;
    ULONG                       Flags;
    USHORT                      IndirectionTableIndex;
    PROCESSOR_NUMBER            TargetProcessorNumber;
    NDIS_STATUS                 EntryStatus;
} NDIS_RSS_SET_INDIRECTION_ENTRY, *PNDIS_RSS_SET_INDIRECTION_ENTRY;

typedef struct _NDIS_RSS_SET_INDIRECTION_ENTRIES
{
    NDIS_OBJECT_HEADER      Header;
    ULONG                   Flags;
    ULONG                   RssEntrySize;
    ULONG                   RssEntryTableOffset;
    ULONG                   NumberOfRssEntries;
} NDIS_RSS_SET_INDIRECTION_ENTRIES, *PNDIS_RSS_SET_INDIRECTION_ENTRIES;

#define NDIS_SIZEOF_RSS_SET_INDIRECTION_ENTRIES_REVISION_1 \
        RTL_SIZEOF_THROUGH_FIELD(NDIS_RSS_SET_INDIRECTION_ENTRIES, \
                                 NumberOfRssEntries)

#endif
#if (NDIS_SUPPORT_NDIS620)

typedef struct _NDIS_RSS_PROCESSOR
{
    PROCESSOR_NUMBER ProcNum;
    USHORT           PreferenceIndex;
    USHORT           Reserved;
} NDIS_RSS_PROCESSOR, *PNDIS_RSS_PROCESSOR;

#define NDIS_SIZEOF_RSS_PROCESSOR_REVISION_1    \
        RTL_SIZEOF_THROUGH_FIELD(NDIS_RSS_PROCESSOR, PreferenceIndex)

#define NDIS_RSS_PROCESSOR_INFO_REVISION_1      1

#if ((NDIS_SUPPORT_NDIS630))

#define NDIS_RSS_PROCESSOR_INFO_REVISION_2      2

typedef enum _NDIS_RSS_PROFILE
{
    NdisRssProfileClosest = 1,
    NdisRssProfileClosestStatic,
    NdisRssProfileNuma,
    NdisRssProfileNumaStatic,
    NdisRssProfileConservative,
#if (NDIS_SUPPORT_NDIS688)
    NdisRssProfileBalanced,
#endif
    NdisRssProfileMaximum,
} NDIS_RSS_PROFILE, *PNDIS_RSS_PROFILE;

#endif

typedef struct _NDIS_RSS_PROCESSOR_INFO
{
    NDIS_OBJECT_HEADER      Header;
    ULONG                   Flags;
    PROCESSOR_NUMBER        RssBaseProcessor;
    ULONG                   MaxNumRssProcessors;
    USHORT                  PreferredNumaNode;
    ULONG                   RssProcessorArrayOffset;
    ULONG                   RssProcessorCount;
    ULONG                   RssProcessorEntrySize;
#if (NDIS_SUPPORT_NDIS630)
    PROCESSOR_NUMBER        RssMaxProcessor;
    NDIS_RSS_PROFILE        RssProfile;
#endif
} NDIS_RSS_PROCESSOR_INFO, *PNDIS_RSS_PROCESSOR_INFO;

#define NDIS_SIZEOF_RSS_PROCESSOR_INFO_REVISION_1    \
        RTL_SIZEOF_THROUGH_FIELD(NDIS_RSS_PROCESSOR_INFO, RssProcessorEntrySize)

#if (NDIS_SUPPORT_NDIS630)
#define NDIS_SIZEOF_RSS_PROCESSOR_INFO_REVISION_2    \
        RTL_SIZEOF_THROUGH_FIELD(NDIS_RSS_PROCESSOR_INFO, RssProfile)
#endif

#endif
#if ((NDIS_SUPPORT_NDIS630))

#define OID_QOS_PARAMETERS                      0xFC050003

#define NDIS_QOS_MAXIMUM_PRIORITIES         8

#define NDIS_QOS_MAXIMUM_TRAFFIC_CLASSES    8

#define NDIS_QOS_CAPABILITIES_STRICT_TSA_SUPPORTED      0x00000001

#define NDIS_QOS_CAPABILITIES_REVISION_1    1

typedef _Struct_size_bytes_(Header.Size) struct _NDIS_QOS_CAPABILITIES
{
    _In_ NDIS_OBJECT_HEADER Header;
    _In_ ULONG              Flags;
    _In_ ULONG              MaxNumTrafficClasses;
    _In_ _Field_range_(<=, MaxNumTrafficClasses)
         ULONG              MaxNumEtsCapableTrafficClasses;
    _In_ _Field_range_(<=, MaxNumTrafficClasses)
         ULONG              MaxNumPfcEnabledTrafficClasses;
} NDIS_QOS_CAPABILITIES, *PNDIS_QOS_CAPABILITIES;

#define NDIS_SIZEOF_QOS_CAPABILITIES_REVISION_1 \
    RTL_SIZEOF_THROUGH_FIELD(NDIS_QOS_CAPABILITIES, MaxNumPfcEnabledTrafficClasses)

#define NDIS_QOS_CLASSIFICATION_ENFORCED_BY_MINIPORT    0x01000000

#define NDIS_QOS_CONDITION_DEFAULT          0x1

#define NDIS_QOS_CONDITION_TCP_PORT         0x2

#define NDIS_QOS_CONDITION_ETHERTYPE        0x5

#define NDIS_QOS_CONDITION_MAXIMUM          0x7

#define NDIS_QOS_ACTION_PRIORITY            0x0

#define NDIS_QOS_ACTION_MAXIMUM             0x1

#define NDIS_QOS_CLASSIFICATION_ELEMENT_REVISION_1   1

typedef _Struct_size_bytes_(Header.Size) struct _NDIS_QOS_CLASSIFICATION_ELEMENT
{
    _In_    NDIS_OBJECT_HEADER  Header;
    _Inout_ ULONG               Flags;
    _In_ _Field_range_(<, NDIS_QOS_CONDITION_MAXIMUM)
            USHORT              ConditionSelector;
    _In_    USHORT              ConditionField;
    _In_ _Field_range_(<, NDIS_QOS_ACTION_MAXIMUM)
            USHORT              ActionSelector;
    _In_    USHORT              ActionField;
} NDIS_QOS_CLASSIFICATION_ELEMENT, *PNDIS_QOS_CLASSIFICATION_ELEMENT;

#define NDIS_SIZEOF_QOS_CLASSIFICATION_ELEMENT_REVISION_1 \
    RTL_SIZEOF_THROUGH_FIELD(NDIS_QOS_CLASSIFICATION_ELEMENT, ActionField)

#define NDIS_QOS_PARAMETERS_ETS_CONFIGURED              0x00000002

#define NDIS_QOS_PARAMETERS_PFC_CONFIGURED              0x00000200

#define NDIS_QOS_PARAMETERS_CLASSIFICATION_CONFIGURED   0x00020000

#define NDIS_QOS_TSA_ETS                    0x2

#define NDIS_QOS_PARAMETERS_REVISION_1   1

typedef _Struct_size_bytes_(Header.Size) struct _NDIS_QOS_PARAMETERS
{
    _In_ NDIS_OBJECT_HEADER Header;
    _In_ ULONG              Flags;
    _In_ _Field_range_(0, NDIS_QOS_MAXIMUM_TRAFFIC_CLASSES)
         ULONG              NumTrafficClasses;
    _In_ UCHAR              PriorityAssignmentTable[NDIS_QOS_MAXIMUM_PRIORITIES];
    _In_ UCHAR              TcBandwidthAssignmentTable[NDIS_QOS_MAXIMUM_TRAFFIC_CLASSES];
    _In_ UCHAR              TsaAssignmentTable[NDIS_QOS_MAXIMUM_TRAFFIC_CLASSES];
    _In_ _Field_range_(0, (1ul << NDIS_QOS_MAXIMUM_PRIORITIES) - 1)
         ULONG              PfcEnable;
    _In_ ULONG              NumClassificationElements;
    _In_ ULONG              ClassificationElementSize;
    _In_ ULONG              FirstClassificationElementOffset;
} NDIS_QOS_PARAMETERS, *PNDIS_QOS_PARAMETERS;

#define NDIS_SIZEOF_QOS_PARAMETERS_REVISION_1 \
    RTL_SIZEOF_THROUGH_FIELD(NDIS_QOS_PARAMETERS, FirstClassificationElementOffset)

typedef NDIS_IF_COUNTED_STRING NDIS_NIC_SWITCH_FRIENDLYNAME, *PNDIS_NIC_SWITCH_FRIENDLYNAME;

#define NDIS_INVALID_VPORT_ID               (ULONG) -1

#define NDIS_DEFAULT_SWITCH_ID              0

#define NDIS_INVALID_SWITCH_ID              (ULONG) -1

typedef enum _NDIS_NIC_SWITCH_TYPE
{
    NdisNicSwitchTypeUnspecified,
    NdisNicSwitchTypeExternal,
    NdisNicSwitchTypeMax
} NDIS_NIC_SWITCH_TYPE, *PNDIS_NIC_SWITCH_TYPE;

typedef struct _NDIS_NIC_SWITCH_INFO
{
    _In_        NDIS_OBJECT_HEADER              Header;
    _In_        ULONG                           Flags;
    _In_        NDIS_NIC_SWITCH_TYPE            SwitchType;
    _In_        NDIS_NIC_SWITCH_ID              SwitchId;
    _In_        NDIS_NIC_SWITCH_FRIENDLYNAME    SwitchFriendlyName;
    _In_        ULONG                           NumVFs;
    _In_        ULONG                           NumAllocatedVFs;
    _In_        ULONG                           NumVPorts;
    _In_        ULONG                           NumActiveVPorts;
    _In_        ULONG                           NumQueuePairsForDefaultVPort;
    _In_        ULONG                           NumQueuePairsForNonDefaultVPorts;
    _In_        ULONG                           NumActiveDefaultVPortMacAddresses;
    _In_        ULONG                           NumActiveNonDefaultVPortMacAddresses;
    _In_        ULONG                           NumActiveDefaultVPortVlanIds;
    _In_        ULONG                           NumActiveNonDefaultVPortVlanIds;
}NDIS_NIC_SWITCH_INFO, *PNDIS_NIC_SWITCH_INFO;

#if ((NDIS_SUPPORT_NDIS650))
#if !defined(_NDIS_SWITCH_PORT_ID)

#define _NDIS_SWITCH_PORT_ID NDIS_SWITCH_PORT_ID

typedef UINT32 NDIS_SWITCH_PORT_ID, *PNDIS_SWITCH_PORT_ID;

typedef USHORT NDIS_SWITCH_NIC_INDEX, *PNDIS_SWITCH_NIC_INDEX;

#endif
#endif
#if !defined(_NDIS_SWITCH_PORT_ID)

#define _NDIS_SWITCH_PORT_ID NDIS_SWITCH_PORT_ID

typedef UINT32 NDIS_SWITCH_PORT_ID, *PNDIS_SWITCH_PORT_ID;

typedef USHORT NDIS_SWITCH_NIC_INDEX, *PNDIS_SWITCH_NIC_INDEX;

#endif

typedef GUID NDIS_SWITCH_OBJECT_INSTANCE_ID, *PNDIS_SWITCH_OBJECT_INSTANCE_ID;

typedef GUID NDIS_SWITCH_OBJECT_ID, *PNDIS_SWITCH_OBJECT_ID;

typedef USHORT NDIS_SWITCH_OBJECT_VERSION, *PNDIS_SWITCH_OBJECT_VERSION;

typedef USHORT NDIS_SWITCH_OBJECT_SERIALIZATION_VERSION, *PNDIS_SWITCH_OBJECT_SERIALIZATION_VERSION;

#define NDIS_SWITCH_OBJECT_SERIALIZATION_VERSION_1       1

typedef enum _NDIS_SWITCH_PORT_PROPERTY_TYPE
{
    NdisSwitchPortPropertyTypeUndefined,
    NdisSwitchPortPropertyTypeCustom,
    NdisSwitchPortPropertyTypeSecurity,
    NdisSwitchPortPropertyTypeVlan,
    NdisSwitchPortPropertyTypeProfile,
    NdisSwitchPortPropertyTypeIsolation,
    NdisSwitchPortPropertyTypeRoutingDomain,
    NdisSwitchPortPropertyTypeMaximum
} NDIS_SWITCH_PORT_PROPERTY_TYPE, *PNDIS_SWITCH_PORT_PROPERTY_TYPE;

typedef enum _NDIS_SWITCH_PORT_VLAN_MODE
{
    NdisSwitchPortVlanModeUnknown      = 0,
    NdisSwitchPortVlanModeAccess       = 1,
    NdisSwitchPortVlanModeTrunk        = 2,
    NdisSwitchPortVlanModePrivate      = 3,
    NdisSwitchPortVlanModeMax          = 4
} NDIS_SWITCH_PORT_VLAN_MODE, *PNDIS_SWITCH_PORT_VLAN_MODE;

typedef enum _NDIS_SWITCH_PORT_PVLAN_MODE
{
    NdisSwitchPortPvlanModeUndefined = 0,
    NdisSwitchPortPvlanModeIsolated,
    NdisSwitchPortPvlanModeCommunity,
    NdisSwitchPortPvlanModePromiscuous
} NDIS_SWITCH_PORT_PVLAN_MODE, *PNDIS_SWITCH_PORT_PVLAN_MODE;

#define NDIS_SWITCH_PORT_PROPERTY_VLAN_REVISION_1       1

typedef struct _NDIS_SWITCH_PORT_PROPERTY_VLAN
{
    NDIS_OBJECT_HEADER          Header;
    ULONG                       Flags;
    NDIS_SWITCH_PORT_VLAN_MODE  OperationMode;
    union
    {
        struct
        {
            UINT16              AccessVlanId;
            UINT16              NativeVlanId;
            UINT64              PruneVlanIdArray[64];
            UINT64              TrunkVlanIdArray[64];
        } VlanProperties;
        struct
        {
            NDIS_SWITCH_PORT_PVLAN_MODE
                                PvlanMode;
            UINT16              PrimaryVlanId;
            union
            {
                UINT16          SecondaryVlanId;
                UINT64          SecondaryVlanIdArray[64];
            };
        } PvlanProperties;
    };
} NDIS_SWITCH_PORT_PROPERTY_VLAN, *PNDIS_SWITCH_PORT_PROPERTY_VLAN;

#define NDIS_SWITCH_PORT_PROPERTY_PARAMETERS_REVISION_1     1

typedef struct _NDIS_SWITCH_PORT_PROPERTY_PARAMETERS
{
    NDIS_OBJECT_HEADER                      Header;
    ULONG                                   Flags;
    NDIS_SWITCH_PORT_ID                     PortId;
    NDIS_SWITCH_PORT_PROPERTY_TYPE          PropertyType;
    NDIS_SWITCH_OBJECT_ID                   PropertyId;
    NDIS_SWITCH_OBJECT_VERSION              PropertyVersion;
    NDIS_SWITCH_OBJECT_SERIALIZATION_VERSION SerializationVersion;
    NDIS_SWITCH_OBJECT_INSTANCE_ID          PropertyInstanceId;
    ULONG                                   PropertyBufferLength;
    ULONG                                   PropertyBufferOffset;
    ULONG                                   Reserved;
} NDIS_SWITCH_PORT_PROPERTY_PARAMETERS, *PNDIS_SWITCH_PORT_PROPERTY_PARAMETERS;

#define NDIS_SIZEOF_NDIS_SWITCH_PORT_PROPERTY_PARAMETERS_REVISION_1    \
        RTL_SIZEOF_THROUGH_FIELD(NDIS_SWITCH_PORT_PROPERTY_PARAMETERS, Reserved)

#define NDIS_SWITCH_PORT_PROPERTY_DELETE_PARAMETERS_REVISION_1  1

typedef struct _NDIS_SWITCH_PORT_PROPERTY_DELETE_PARAMETERS
{
    NDIS_OBJECT_HEADER                      Header;
    ULONG                                   Flags;
    NDIS_SWITCH_PORT_ID                     PortId;
    NDIS_SWITCH_PORT_PROPERTY_TYPE          PropertyType;
    NDIS_SWITCH_OBJECT_ID                   PropertyId;
    NDIS_SWITCH_OBJECT_INSTANCE_ID          PropertyInstanceId;
} NDIS_SWITCH_PORT_PROPERTY_DELETE_PARAMETERS, *PNDIS_SWITCH_PORT_PROPERTY_DELETE_PARAMETERS;

#define NDIS_SIZEOF_NDIS_SWITCH_PORT_PROPERTY_DELETE_PARAMETERS_REVISION_1  \
        RTL_SIZEOF_THROUGH_FIELD(NDIS_SWITCH_PORT_PROPERTY_DELETE_PARAMETERS, PropertyInstanceId)

#define NDIS_SWITCH_PORT_PROPERTY_ENUM_PARAMETERS_REVISION_1    1

typedef struct _NDIS_SWITCH_PORT_PROPERTY_ENUM_PARAMETERS
{
    NDIS_OBJECT_HEADER                      Header;
    ULONG                                   Flags;
    NDIS_SWITCH_PORT_ID                     PortId;
    NDIS_SWITCH_PORT_PROPERTY_TYPE          PropertyType;
    NDIS_SWITCH_OBJECT_ID                   PropertyId;
    NDIS_SWITCH_OBJECT_SERIALIZATION_VERSION SerializationVersion;
    ULONG                                   FirstPropertyOffset;
    ULONG                                   NumProperties;
    USHORT                                  Reserved;
} NDIS_SWITCH_PORT_PROPERTY_ENUM_PARAMETERS, *PNDIS_SWITCH_PORT_PROPERTY_ENUM_PARAMETERS;

#define NDIS_SWITCH_PORT_PROPERTY_ENUM_PARAMETERS_GET_FIRST_INFO(_PortEnumParams_)\
            ((PNDIS_SWITCH_PORT_PROPERTY_ENUM_INFO)((PUCHAR)(_PortEnumParams_) + \
                (_PortEnumParams_)->FirstPropertyOffset))

typedef struct _NDIS_SWITCH_PORT_PROPERTY_ENUM_INFO
{
    NDIS_OBJECT_HEADER                      Header;
    ULONG                                   Flags;
    NDIS_SWITCH_OBJECT_VERSION              PropertyVersion;
    NDIS_SWITCH_OBJECT_INSTANCE_ID          PropertyInstanceId;
    ULONG                                   QwordAlignedPropertyBufferLength;
    ULONG                                   PropertyBufferLength;
    ULONG                                   PropertyBufferOffset;
} NDIS_SWITCH_PORT_PROPERTY_ENUM_INFO, *PNDIS_SWITCH_PORT_PROPERTY_ENUM_INFO;

#define NDIS_SWITCH_PORT_PROPERTY_ENUM_INFO_GET_PROPERTY(_PortEnumInfo_)\
            ((PVOID)((PUCHAR)(_PortEnumInfo_) + (_PortEnumInfo_)->PropertyBufferOffset))

typedef enum _NDIS_SWITCH_PORT_FEATURE_STATUS_TYPE
{
    NdisSwitchPortFeatureStatusTypeUndefined,
    NdisSwitchPortFeatureStatusTypeCustom,
    NdisSwitchPortFeatureStatusTypeMaximum
} NDIS_SWITCH_PORT_FEATURE_STATUS_TYPE, *PNDIS_SWITCH_PORT_FEATURE_STATUS_TYPE;

typedef struct _NDIS_SWITCH_PORT_FEATURE_STATUS_PARAMETERS
{
    NDIS_OBJECT_HEADER                            Header;
    ULONG                                         Flags;
    NDIS_SWITCH_PORT_ID                           PortId;
    NDIS_SWITCH_PORT_FEATURE_STATUS_TYPE          FeatureStatusType;
    NDIS_SWITCH_OBJECT_ID                         FeatureStatusId;
    NDIS_SWITCH_OBJECT_VERSION                    FeatureStatusVersion;
    NDIS_SWITCH_OBJECT_SERIALIZATION_VERSION      SerializationVersion;
    NDIS_SWITCH_OBJECT_INSTANCE_ID                FeatureStatusInstanceId;
    ULONG                                         FeatureStatusBufferLength;
    ULONG                                         FeatureStatusBufferOffset;
    ULONG                                         Reserved;
} NDIS_SWITCH_PORT_FEATURE_STATUS_PARAMETERS, *PNDIS_SWITCH_PORT_FEATURE_STATUS_PARAMETERS;

typedef enum _NDIS_SWITCH_PROPERTY_TYPE
{
    NdisSwitchPropertyTypeUndefined,
    NdisSwitchPropertyTypeCustom,
    NdisSwitchPropertyTypeMaximum
} NDIS_SWITCH_PROPERTY_TYPE, *PNDIS_SWITCH_PROPERTY_TYPE;

typedef struct _NDIS_SWITCH_PROPERTY_CUSTOM
{
    NDIS_OBJECT_HEADER      Header;
    ULONG                   Flags;
    ULONG                   PropertyBufferLength;
    ULONG                   PropertyBufferOffset;
} NDIS_SWITCH_PROPERTY_CUSTOM, *PNDIS_SWITCH_PROPERTY_CUSTOM;

#define NDIS_SWITCH_PROPERTY_CUSTOM_GET_BUFFER(_SwitchPropertyCustom_)\
            ((PVOID)((PUCHAR)(_SwitchPropertyCustom_) + (_SwitchPropertyCustom_)->PropertyBufferOffset))

#define NDIS_SWITCH_PROPERTY_PARAMETERS_REVISION_1  1

typedef struct _NDIS_SWITCH_PROPERTY_PARAMETERS
{
    NDIS_OBJECT_HEADER                         Header;
    ULONG                                      Flags;
    NDIS_SWITCH_PROPERTY_TYPE                  PropertyType;
    NDIS_SWITCH_OBJECT_ID                      PropertyId;
    NDIS_SWITCH_OBJECT_VERSION                 PropertyVersion;
    NDIS_SWITCH_OBJECT_SERIALIZATION_VERSION   SerializationVersion;
    NDIS_SWITCH_OBJECT_INSTANCE_ID             PropertyInstanceId;
    ULONG                                      PropertyBufferLength;
    ULONG                                      PropertyBufferOffset;
} NDIS_SWITCH_PROPERTY_PARAMETERS, *PNDIS_SWITCH_PROPERTY_PARAMETERS;

#define NDIS_SIZEOF_NDIS_SWITCH_PROPERTY_PARAMETERS_REVISION_1   \
    RTL_SIZEOF_THROUGH_FIELD(NDIS_SWITCH_PROPERTY_PARAMETERS, PropertyBufferOffset)

#define NDIS_SWITCH_PROPERTY_PARAMETERS_GET_PROPERTY(_SwitchParameters_)\
            ((PVOID)((PUCHAR)(_SwitchParameters_) + (_SwitchParameters_)->PropertyBufferOffset))

#define NDIS_SWITCH_PROPERTY_DELETE_PARAMETERS_REVISION_1  1

typedef struct _NDIS_SWITCH_PROPERTY_DELETE_PARAMETERS
{
    NDIS_OBJECT_HEADER                 Header;
    ULONG                              Flags;
    NDIS_SWITCH_PROPERTY_TYPE          PropertyType;
    NDIS_SWITCH_OBJECT_ID              PropertyId;
    NDIS_SWITCH_OBJECT_INSTANCE_ID     PropertyInstanceId;
} NDIS_SWITCH_PROPERTY_DELETE_PARAMETERS, *PNDIS_SWITCH_PROPERTY_DELETE_PARAMETERS;

#define NDIS_SIZEOF_NDIS_SWITCH_PROPERTY_DELETE_PARAMETERS_REVISION_1   \
    RTL_SIZEOF_THROUGH_FIELD(NDIS_SWITCH_PROPERTY_DELETE_PARAMETERS, PropertyInstanceId)

typedef struct _NDIS_SWITCH_PROPERTY_ENUM_INFO
{
    NDIS_OBJECT_HEADER               Header;
    ULONG                            Flags;
    NDIS_SWITCH_OBJECT_INSTANCE_ID   PropertyInstanceId;
    NDIS_SWITCH_OBJECT_VERSION       PropertyVersion;
    ULONG                            QwordAlignedPropertyBufferLength;
    ULONG                            PropertyBufferLength;
    ULONG                            PropertyBufferOffset;
} NDIS_SWITCH_PROPERTY_ENUM_INFO, *PNDIS_SWITCH_PROPERTY_ENUM_INFO;

#define NDIS_SWITCH_PROPERTY_ENUM_INFO_GET_NEXT(_SwitchEnumInfo_)\
            ((PNDIS_SWITCH_PROPERTY_ENUM_INFO)\
                ((ULONG_PTR)(_SwitchEnumInfo_) +\
                (_SwitchEnumInfo_)->QwordAlignedPropertyBufferLength +\
                 sizeof(NDIS_SWITCH_PROPERTY_ENUM_INFO)))

#define NDIS_SWITCH_PROPERTY_ENUM_INFO_GET_PROPERTY(_SwitchEnumInfo_)\
            ((PVOID)((PUCHAR)(_SwitchEnumInfo_) + (_SwitchEnumInfo_)->PropertyBufferOffset))

#define NDIS_SWITCH_PROPERTY_ENUM_PARAMETERS_REVISION_1  1

typedef struct _NDIS_SWITCH_PROPERTY_ENUM_PARAMETERS
{
    NDIS_OBJECT_HEADER                         Header;
    ULONG                                      Flags;
    NDIS_SWITCH_PROPERTY_TYPE                  PropertyType;
    NDIS_SWITCH_OBJECT_ID                      PropertyId;
    NDIS_SWITCH_OBJECT_SERIALIZATION_VERSION   SerializationVersion;
    ULONG                                      FirstPropertyOffset;
    ULONG                                      NumProperties;
} NDIS_SWITCH_PROPERTY_ENUM_PARAMETERS, *PNDIS_SWITCH_PROPERTY_ENUM_PARAMETERS;

#define NDIS_SWITCH_PROPERTY_ENUM_PARAMETERS_GET_FIRST_INFO(_SwitchEnumParams_)\
            ((PNDIS_SWITCH_PROPERTY_ENUM_INFO)((PUCHAR)(_SwitchEnumParams_) + \
                (_SwitchEnumParams_)->FirstPropertyOffset))

#define NDIS_SWITCH_FEATURE_STATUS_PARAMETERS_REVISION_1         1

typedef enum _NDIS_SWITCH_FEATURE_STATUS_TYPE
{
    NdisSwitchFeatureStatusTypeUndefined,
    NdisSwitchFeatureStatusTypeCustom,
    NdisSwitchFeatureStatusTypeMaximum
} NDIS_SWITCH_FEATURE_STATUS_TYPE, *PNDIS_SWITCH_FEATURE_STATUS_TYPE;

typedef struct _NDIS_SWITCH_FEATURE_STATUS_PARAMETERS
{
    NDIS_OBJECT_HEADER                               Header;
    ULONG                                            Flags;
    NDIS_SWITCH_FEATURE_STATUS_TYPE                  FeatureStatusType;
    NDIS_SWITCH_OBJECT_ID                            FeatureStatusId;
    NDIS_SWITCH_OBJECT_INSTANCE_ID                   FeatureStatusInstanceId;
    NDIS_SWITCH_OBJECT_VERSION                       FeatureStatusVersion;
    NDIS_SWITCH_OBJECT_SERIALIZATION_VERSION         SerializationVersion;
    ULONG                                            FeatureStatusBufferOffset;
    ULONG                                            FeatureStatusBufferLength;
} NDIS_SWITCH_FEATURE_STATUS_PARAMETERS, *PNDIS_SWITCH_FEATURE_STATUS_PARAMETERS;

#define NDIS_SIZEOF_NDIS_SWITCH_FEATURE_STATUS_PARAMETERS_REVISION_1       \
            RTL_SIZEOF_THROUGH_FIELD(NDIS_SWITCH_FEATURE_STATUS_PARAMETERS, FeatureStatusBufferLength)

#define NDIS_SWITCH_FEATURE_STATUS_CUSTOM_REVISION_1       1

typedef struct _NDIS_SWITCH_FEATURE_STATUS_CUSTOM
{
    NDIS_OBJECT_HEADER      Header;
    ULONG                   Flags;
    ULONG                   FeatureStatusCustomBufferLength;
    ULONG                   FeatureStatusCustomBufferOffset;
} NDIS_SWITCH_FEATURE_STATUS_CUSTOM, *PNDIS_SWITCH_FEATURE_STATUS_CUSTOM;

#define NDIS_SIZEOF_NDIS_SWITCH_FEATURE_STATUS_CUSTOM_REVISION_1       \
        RTL_SIZEOF_THROUGH_FIELD(NDIS_SWITCH_FEATURE_STATUS_CUSTOM, FeatureStatusCustomBufferOffset)

#define NDIS_SWITCH_PARAMETERS_REVISION_1      1

typedef struct _NDIS_SWITCH_PARAMETERS
{
  NDIS_OBJECT_HEADER        Header;
  ULONG                     Flags;
  NDIS_SWITCH_NAME          SwitchName;
  NDIS_SWITCH_FRIENDLYNAME  SwitchFriendlyName;
  UINT32                    NumSwitchPorts;
  BOOLEAN                   IsActive;
} NDIS_SWITCH_PARAMETERS, *PNDIS_SWITCH_PARAMETERS;

typedef enum _NDIS_SWITCH_PORT_TYPE
{
    NdisSwitchPortTypeGeneric    = 0,
    NdisSwitchPortTypeExternal   = 1,
    NdisSwitchPortTypeSynthetic  = 2,
    NdisSwitchPortTypeEmulated   = 3,
    NdisSwitchPortTypeInternal   = 4
} NDIS_SWITCH_PORT_TYPE;

typedef enum _NDIS_SWITCH_PORT_STATE
{
    NdisSwitchPortStateUnknown      = 0,
    NdisSwitchPortStateCreated      = 1,
    NdisSwitchPortStateTeardown     = 2,
    NdisSwitchPortStateDeleted      = 3
} NDIS_SWITCH_PORT_STATE;

typedef struct _NDIS_SWITCH_PORT_PARAMETERS
{
  NDIS_OBJECT_HEADER                Header;
  ULONG                             Flags;
  NDIS_SWITCH_PORT_ID               PortId;
  NDIS_SWITCH_PORT_NAME             PortName;
  NDIS_SWITCH_PORT_FRIENDLYNAME     PortFriendlyName;
  NDIS_SWITCH_PORT_TYPE             PortType;
  BOOLEAN                           IsValidationPort;
  NDIS_SWITCH_PORT_STATE            PortState;
} NDIS_SWITCH_PORT_PARAMETERS, *PNDIS_SWITCH_PORT_PARAMETERS;

#define NDIS_SWITCH_PORT_PARAMETERS_REVISION_1      1

#define NDIS_SIZEOF_NDIS_SWITCH_PORT_PARAMETERS_REVISION_1 \
    RTL_SIZEOF_THROUGH_FIELD(NDIS_SWITCH_PORT_PARAMETERS, PortState)

typedef struct _NDIS_SWITCH_PORT_ARRAY
{
  NDIS_OBJECT_HEADER        Header;
  ULONG                     Flags;
  USHORT                    FirstElementOffset;
  ULONG                     NumElements;
  ULONG                     ElementSize;
} NDIS_SWITCH_PORT_ARRAY, *PNDIS_SWITCH_PORT_ARRAY;

#define NDIS_SWITCH_PORT_ARRAY_REVISION_1      1

typedef enum _NDIS_SWITCH_NIC_TYPE
{
    NdisSwitchNicTypeExternal      = 0,
    NdisSwitchNicTypeSynthetic     = 1,
    NdisSwitchNicTypeEmulated      = 2,
    NdisSwitchNicTypeInternal      = 3
} NDIS_SWITCH_NIC_TYPE;

typedef enum _NDIS_SWITCH_NIC_STATE
{
    NdisSwitchNicStateUnknown       = 0,
    NdisSwitchNicStateCreated       = 1,
    NdisSwitchNicStateConnected     = 2,
    NdisSwitchNicStateDisconnected  = 3,
    NdisSwitchNicStateDeleted       = 4
} NDIS_SWITCH_NIC_STATE;

typedef struct _NDIS_SWITCH_NIC_PARAMETERS
{
    NDIS_OBJECT_HEADER              Header;
    ULONG                           Flags;
    NDIS_SWITCH_NIC_NAME            NicName;
    NDIS_SWITCH_NIC_FRIENDLYNAME    NicFriendlyName;
    NDIS_SWITCH_PORT_ID             PortId;
    NDIS_SWITCH_NIC_INDEX           NicIndex;
    NDIS_SWITCH_NIC_TYPE            NicType;
    NDIS_SWITCH_NIC_STATE           NicState;
    NDIS_VM_NAME                    VmName;
    NDIS_VM_FRIENDLYNAME            VmFriendlyName;
    GUID                            NetCfgInstanceId;
    ULONG                           MTU;
    USHORT                          NumaNodeId;
    UCHAR                           PermanentMacAddress[NDIS_MAX_PHYS_ADDRESS_LENGTH];
    UCHAR                           VMMacAddress[NDIS_MAX_PHYS_ADDRESS_LENGTH];
    UCHAR                           CurrentMacAddress[NDIS_MAX_PHYS_ADDRESS_LENGTH];
    BOOLEAN                         VFAssigned;
#if defined(NDIS_SUPPORT_NDIS640)
    ULONG64                         NdisReserved[2];
#endif
} NDIS_SWITCH_NIC_PARAMETERS, *PNDIS_SWITCH_NIC_PARAMETERS;

#define NDIS_SWITCH_NIC_PARAMETERS_REVISION_1      1

#define NDIS_SIZEOF_NDIS_SWITCH_NIC_PARAMETERS_REVISION_1 \
    RTL_SIZEOF_THROUGH_FIELD(NDIS_SWITCH_NIC_PARAMETERS, VFAssigned)

typedef struct _NDIS_SWITCH_NIC_ARRAY
{
  NDIS_OBJECT_HEADER        Header;
  ULONG                     Flags;
  USHORT                    FirstElementOffset;
  ULONG                     NumElements;
  ULONG                     ElementSize;
} NDIS_SWITCH_NIC_ARRAY, *PNDIS_SWITCH_NIC_ARRAY;

#define NDIS_SWITCH_NIC_AT_ARRAY_INDEX(_NicArray_, _Index_)\
    ((PNDIS_SWITCH_NIC_PARAMETERS)((PUCHAR)(_NicArray_) + \
                        (_NicArray_)->FirstElementOffset + \
                        ((_NicArray_)->ElementSize * (_Index_))))

typedef struct _NDIS_OID_REQUEST NDIS_OID_REQUEST, *PNDIS_OID_REQUEST;

typedef struct _NDIS_SWITCH_NIC_OID_REQUEST
{
    NDIS_OBJECT_HEADER          Header;
    ULONG                       Flags;
    NDIS_SWITCH_PORT_ID         SourcePortId;
    NDIS_SWITCH_NIC_INDEX       SourceNicIndex;
    NDIS_SWITCH_PORT_ID         DestinationPortId;
    NDIS_SWITCH_NIC_INDEX       DestinationNicIndex;
    PNDIS_OID_REQUEST           OidRequest;
} NDIS_SWITCH_NIC_OID_REQUEST, *PNDIS_SWITCH_NIC_OID_REQUEST;

#define NDIS_SWITCH_NIC_OID_REQUEST_REVISION_1     1

#define NDIS_SIZEOF_NDIS_SWITCH_NIC_OID_REQUEST_REVISION_1 \
    RTL_SIZEOF_THROUGH_FIELD(NDIS_SWITCH_NIC_OID_REQUEST, OidRequest)

typedef struct _NDIS_SWITCH_NIC_SAVE_STATE
{
  NDIS_OBJECT_HEADER                    Header;
  ULONG                                 Flags;
  NDIS_SWITCH_PORT_ID                   PortId;
  NDIS_SWITCH_NIC_INDEX                 NicIndex;
  GUID                                  ExtensionId;
  NDIS_SWITCH_EXTENSION_FRIENDLYNAME    ExtensionFriendlyName;
  GUID                                  FeatureClassId;
  USHORT                                SaveDataSize;
  USHORT                                SaveDataOffset;
#if (NDIS_SUPPORT_NDIS650)
  ULONG                                 SaveDataSizeOverflow;
#endif
} NDIS_SWITCH_NIC_SAVE_STATE, *PNDIS_SWITCH_NIC_SAVE_STATE;

#define NDIS_SWITCH_NIC_SAVE_STATE_REVISION_1      1

#define NDIS_SIZEOF_NDIS_SWITCH_NIC_SAVE_STATE_REVISION_1 \
    RTL_SIZEOF_THROUGH_FIELD(NDIS_SWITCH_NIC_SAVE_STATE,  SaveDataOffset)

#endif
#endif

#if (NDIS_SUPPORT_NDIS689)
#define NDIS_OFFLOAD_PARAMETERS_UDP_RSC_NO_CHANGE       0
#define NDIS_OFFLOAD_PARAMETERS_UDP_RSC_DISABLED        1
#define NDIS_OFFLOAD_PARAMETERS_UDP_RSC_ENABLED         2

typedef struct _NDIS_UDP_RSC_OFFLOAD
{
    BOOLEAN Enabled;
} NDIS_UDP_RSC_OFFLOAD;
#endif

typedef NET_IF_MEDIA_CONNECT_STATE NDIS_MEDIA_CONNECT_STATE, *PNDIS_MEDIA_CONNECT_STATE;

typedef NET_IF_MEDIA_DUPLEX_STATE NDIS_MEDIA_DUPLEX_STATE, *PNDIS_MEDIA_DUPLEX_STATE;

#define NDIS_LINK_STATE_PAUSE_FUNCTIONS_AUTO_NEGOTIATED         0x00000008
#define NDIS_LINK_PARAMETERS_REVISION_1      1
typedef struct _NDIS_LINK_PARAMETERS
{
    NDIS_OBJECT_HEADER              Header;
    NDIS_MEDIA_DUPLEX_STATE         MediaDuplexState;
    ULONG64                         XmitLinkSpeed;
    ULONG64                         RcvLinkSpeed;
    NDIS_SUPPORTED_PAUSE_FUNCTIONS  PauseFunctions;
    ULONG                           AutoNegotiationFlags;
} NDIS_LINK_PARAMETERS, *PNDIS_LINK_PARAMETERS;

#define NDIS_SIZEOF_LINK_PARAMETERS_REVISION_1    \
        RTL_SIZEOF_THROUGH_FIELD(NDIS_LINK_PARAMETERS, AutoNegotiationFlags)
#define NDIS_OPER_STATE_REVISION_1      1
typedef struct _NDIS_OPER_STATE
{
    NDIS_OBJECT_HEADER              Header;
    NET_IF_OPER_STATUS              OperationalStatus;
    ULONG                           OperationalStatusFlags;
} NDIS_OPER_STATE, *PNDIS_OPER_STATE;

#define NDIS_SIZEOF_OPER_STATE_REVISION_1    \
        RTL_SIZEOF_THROUGH_FIELD(NDIS_OPER_STATE, OperationalStatusFlags)
typedef struct _NDIS_IP_OPER_STATUS
{
    ULONG AddressFamily;
    NET_IF_OPER_STATUS OperationalStatus;
    ULONG OperationalStatusFlags;
} NDIS_IP_OPER_STATUS, *PNDIS_IP_OPER_STATUS;

#define MAXIMUM_IP_OPER_STATUS_ADDRESS_FAMILIES_SUPPORTED 32
#define NDIS_IP_OPER_STATUS_INFO_REVISION_1 1
typedef struct _NDIS_IP_OPER_STATUS_INFO
{
    NDIS_OBJECT_HEADER Header;
    ULONG Flags;
    ULONG NumberofAddressFamiliesReturned;
    NDIS_IP_OPER_STATUS IpOperationalStatus[MAXIMUM_IP_OPER_STATUS_ADDRESS_FAMILIES_SUPPORTED];
} NDIS_IP_OPER_STATUS_INFO, *PNDIS_IP_OPER_STATUS_INFO;

#define NDIS_SIZEOF_IP_OPER_STATUS_INFO_REVISION_1    \
        FIELD_OFFSET(NDIS_IP_OPER_STATUS_INFO, IpOperationalStatus) + \
        MAXIMUM_IP_OPER_STATUS_ADDRESS_FAMILIES_SUPPORTED * sizeof(NDIS_IP_OPER_STATUS)
#define NDIS_IP_OPER_STATE_REVISION_1    1
typedef struct _NDIS_IP_OPER_STATE
{
    NDIS_OBJECT_HEADER Header;
    ULONG Flags;
    NDIS_IP_OPER_STATUS IpOperationalStatus;
} NDIS_IP_OPER_STATE, *PNDIS_IP_OPER_STATE;

#define NDIS_SIZEOF_IP_OPER_STATE_REVISION_1    \
        RTL_SIZEOF_THROUGH_FIELD(NDIS_IP_OPER_STATE, IpOperationalStatus)
#define NDIS_OFFLOAD_PARAMETERS_LSOV1_DISABLED             1
#define NDIS_OFFLOAD_PARAMETERS_LSOV1_ENABLED              2
#define NDIS_OFFLOAD_PARAMETERS_IPSECV1_DISABLED             1
#define NDIS_OFFLOAD_PARAMETERS_IPSECV1_AH_ENABLED           2
#define NDIS_OFFLOAD_PARAMETERS_IPSECV1_ESP_ENABLED          3
#define NDIS_OFFLOAD_PARAMETERS_IPSECV1_AH_AND_ESP_ENABLED   4
#define NDIS_OFFLOAD_PARAMETERS_LSOV2_DISABLED             1
#define NDIS_OFFLOAD_PARAMETERS_LSOV2_ENABLED              2
#if (NDIS_SUPPORT_NDIS61)
#define NDIS_OFFLOAD_PARAMETERS_IPSECV2_DISABLED             1
#define NDIS_OFFLOAD_PARAMETERS_IPSECV2_AH_ENABLED           2
#define NDIS_OFFLOAD_PARAMETERS_IPSECV2_ESP_ENABLED          3
#define NDIS_OFFLOAD_PARAMETERS_IPSECV2_AH_AND_ESP_ENABLED   4
#endif
#if (NDIS_SUPPORT_NDIS630)
#define NDIS_OFFLOAD_PARAMETERS_RSC_DISABLED             1
#define NDIS_OFFLOAD_PARAMETERS_RSC_ENABLED              2
#define NDIS_ENCAPSULATION_TYPE_GRE_MAC          0x00000001
#if (NDIS_SUPPORT_NDIS650)
#define NDIS_ENCAPSULATION_TYPE_VXLAN            0x00000002
#endif
#endif
#define NDIS_OFFLOAD_PARAMETERS_CONNECTION_OFFLOAD_DISABLED     1
#define NDIS_OFFLOAD_PARAMETERS_CONNECTION_OFFLOAD_ENABLED      2
#if (NDIS_SUPPORT_NDIS683)
#define NDIS_OFFLOAD_PARAMETERS_USO_DISABLED            1
#define NDIS_OFFLOAD_PARAMETERS_USO_ENABLED             2
#endif
#if (NDIS_SUPPORT_NDIS61)
#define NDIS_OFFLOAD_PARAMETERS_REVISION_2            2
#endif
#if (NDIS_SUPPORT_NDIS630)
#define NDIS_OFFLOAD_PARAMETERS_REVISION_3            3
#endif
#if (NDIS_SUPPORT_NDIS650)
#define NDIS_OFFLOAD_PARAMETERS_REVISION_4            4
#endif
#if (NDIS_SUPPORT_NDIS683)
#define NDIS_OFFLOAD_PARAMETERS_REVISION_5            5
#endif
#if (NDIS_SUPPORT_NDIS689)
#define NDIS_OFFLOAD_PARAMETERS_REVISION_6            6
#endif
#if (NDIS_SUPPORT_NDIS630)
#define NDIS_OFFLOAD_PARAMETERS_SKIP_REGISTRY_UPDATE    0x00000001
#endif
#define NDIS_SIZEOF_OFFLOAD_PARAMETERS_REVISION_1 RTL_SIZEOF_THROUGH_FIELD(NDIS_OFFLOAD_PARAMETERS, Flags)
#if (NDIS_SUPPORT_NDIS61)
#define NDIS_SIZEOF_OFFLOAD_PARAMETERS_REVISION_2 RTL_SIZEOF_THROUGH_FIELD(NDIS_OFFLOAD_PARAMETERS, IPsecV2IPv4)
#endif
#if (NDIS_SUPPORT_NDIS630)
#define NDIS_SIZEOF_OFFLOAD_PARAMETERS_REVISION_3 RTL_SIZEOF_THROUGH_FIELD(NDIS_OFFLOAD_PARAMETERS, EncapsulationTypes)
#endif
#if (NDIS_SUPPORT_NDIS650)
#define NDIS_SIZEOF_OFFLOAD_PARAMETERS_REVISION_4 RTL_SIZEOF_THROUGH_FIELD(NDIS_OFFLOAD_PARAMETERS, EncapsulationProtocolParameters)
#endif
#if (NDIS_SUPPORT_NDIS683)
#define NDIS_SIZEOF_OFFLOAD_PARAMETERS_REVISION_5 RTL_SIZEOF_THROUGH_FIELD(NDIS_OFFLOAD_PARAMETERS, UdpSegmentation)
#endif
#if (NDIS_SUPPORT_NDIS689)
#define NDIS_SIZEOF_OFFLOAD_PARAMETERS_REVISION_6 RTL_SIZEOF_THROUGH_FIELD(NDIS_OFFLOAD_PARAMETERS, UdpRsc)
#endif
typedef struct _NDIS_TCP_LARGE_SEND_OFFLOAD_V1
{

    struct
    {
        ULONG     Encapsulation;
        ULONG     MaxOffLoadSize;
        ULONG     MinSegmentCount;
        ULONG     TcpOptions:2;
        ULONG     IpOptions:2;
    } IPv4;

} NDIS_TCP_LARGE_SEND_OFFLOAD_V1, *PNDIS_TCP_LARGE_SEND_OFFLOAD_V1;

typedef struct _NDIS_TCP_IP_CHECKSUM_OFFLOAD
{

    struct
    {
        ULONG       Encapsulation;
        ULONG       IpOptionsSupported:2;
        ULONG       TcpOptionsSupported:2;
        ULONG       TcpChecksum:2;
        ULONG       UdpChecksum:2;
        ULONG       IpChecksum:2;
    } IPv4Transmit;

    struct
    {
        ULONG       Encapsulation;
        ULONG       IpOptionsSupported:2;
        ULONG       TcpOptionsSupported:2;
        ULONG       TcpChecksum:2;
        ULONG       UdpChecksum:2;
        ULONG       IpChecksum:2;
    } IPv4Receive;


    struct
    {
        ULONG       Encapsulation;
        ULONG       IpExtensionHeadersSupported:2;
        ULONG       TcpOptionsSupported:2;
        ULONG       TcpChecksum:2;
        ULONG       UdpChecksum:2;

    } IPv6Transmit;

    struct
    {
        ULONG       Encapsulation;
        ULONG       IpExtensionHeadersSupported:2;
        ULONG       TcpOptionsSupported:2;
        ULONG       TcpChecksum:2;
        ULONG       UdpChecksum:2;

    } IPv6Receive;

} NDIS_TCP_IP_CHECKSUM_OFFLOAD, *PNDIS_TCP_IP_CHECKSUM_OFFLOAD;

typedef struct _NDIS_IPSEC_OFFLOAD_V1
{
    struct
    {
        ULONG   Encapsulation;
        ULONG   AhEspCombined;
        ULONG   TransportTunnelCombined;
        ULONG   IPv4Options;
        ULONG   Flags;
    } Supported;

    struct
    {
        ULONG   Md5:2;
        ULONG   Sha_1:2;
        ULONG   Transport:2;
        ULONG   Tunnel:2;
        ULONG   Send:2;
        ULONG   Receive:2;
    } IPv4AH;

    struct
    {
        ULONG   Des:2;
        ULONG   Reserved:2;
        ULONG   TripleDes:2;
        ULONG   NullEsp:2;
        ULONG   Transport:2;
        ULONG   Tunnel:2;
        ULONG   Send:2;
        ULONG   Receive:2;
    } IPv4ESP;

} NDIS_IPSEC_OFFLOAD_V1, *PNDIS_IPSEC_OFFLOAD_V1;

typedef struct _NDIS_TCP_LARGE_SEND_OFFLOAD_V2
{
    struct
    {
         ULONG     Encapsulation;
         ULONG     MaxOffLoadSize;
         ULONG     MinSegmentCount;
    }IPv4;

    struct
    {
         ULONG     Encapsulation;
         ULONG     MaxOffLoadSize;
         ULONG     MinSegmentCount;
         ULONG     IpExtensionHeadersSupported:2;
         ULONG     TcpOptionsSupported:2;
    }IPv6;

} NDIS_TCP_LARGE_SEND_OFFLOAD_V2, *PNDIS_TCP_LARGE_SEND_OFFLOAD_V2;

#if (NDIS_SUPPORT_NDIS61)
#define IPSEC_OFFLOAD_V2_AUTHENTICATION_MD5                      0x00000001
#define IPSEC_OFFLOAD_V2_AUTHENTICATION_SHA_1                    0x00000002
#define IPSEC_OFFLOAD_V2_AUTHENTICATION_SHA_256                  0x00000004
#define IPSEC_OFFLOAD_V2_AUTHENTICATION_AES_GCM_128              0x00000008
#define IPSEC_OFFLOAD_V2_AUTHENTICATION_AES_GCM_192              0x00000010
#define IPSEC_OFFLOAD_V2_AUTHENTICATION_AES_GCM_256              0x00000020
#define IPSEC_OFFLOAD_V2_ENCRYPTION_NONE                          0x00000001
#define IPSEC_OFFLOAD_V2_ENCRYPTION_DES_CBC                       0x00000002
#define IPSEC_OFFLOAD_V2_ENCRYPTION_3_DES_CBC                     0x00000004
#define IPSEC_OFFLOAD_V2_ENCRYPTION_AES_GCM_128                   0x00000008
#define IPSEC_OFFLOAD_V2_ENCRYPTION_AES_GCM_192                   0x00000010
#define IPSEC_OFFLOAD_V2_ENCRYPTION_AES_GCM_256                   0x00000020
#define IPSEC_OFFLOAD_V2_ENCRYPTION_AES_CBC_128                   0x00000040
#define IPSEC_OFFLOAD_V2_ENCRYPTION_AES_CBC_192                   0x00000080
#define IPSEC_OFFLOAD_V2_ENCRYPTION_AES_CBC_256                   0x00000100
typedef struct _NDIS_IPSEC_OFFLOAD_V2
{
    ULONG       Encapsulation;      
    BOOLEAN     IPv6Supported;      
    BOOLEAN     IPv4Options;                           
    BOOLEAN     IPv6NonIPsecExtensionHeaders;          
    BOOLEAN     Ah;
    BOOLEAN     Esp;
    BOOLEAN     AhEspCombined;
    BOOLEAN     Transport;
    BOOLEAN     Tunnel;
    BOOLEAN     TransportTunnelCombined;
    BOOLEAN     LsoSupported;
    BOOLEAN     ExtendedSequenceNumbers;
    ULONG       UdpEsp;
    ULONG       AuthenticationAlgorithms;     
    ULONG       EncryptionAlgorithms;         
    ULONG       SaOffloadCapacity;            
} NDIS_IPSEC_OFFLOAD_V2, *PNDIS_IPSEC_OFFLOAD_V2;

#endif
#if (NDIS_SUPPORT_NDIS630)
typedef struct _NDIS_TCP_RECV_SEG_COALESCE_OFFLOAD
{
    struct
    {
        BOOLEAN Enabled;
    } IPv4;
    struct
    {
        BOOLEAN Enabled;
    } IPv6;
} NDIS_TCP_RECV_SEG_COALESCE_OFFLOAD, *PNDIS_TCP_RECV_SEG_COALESCE_OFFLOAD;

#define NDIS_TCP_RECV_SEG_COALESC_OFFLOAD_REVISION_1            1
#define NDIS_SIZEOF_TCP_RECV_SEG_COALESC_OFFLOAD_REVISION_1     \
    RTL_SIZEOF_THROUGH_FIELD(NDIS_TCP_RECV_SEG_COALESCE_OFFLOAD, IPv6.Enabled)
#define NDIS_ENCAPSULATED_PACKET_TASK_OFFLOAD_NOT_SUPPORTED    0x00000000
#define NDIS_ENCAPSULATED_PACKET_TASK_OFFLOAD_INNER_IPV4       0x00000001
#define NDIS_ENCAPSULATED_PACKET_TASK_OFFLOAD_OUTER_IPV4       0x00000002
#define NDIS_ENCAPSULATED_PACKET_TASK_OFFLOAD_INNER_IPV6       0x00000004
#define NDIS_ENCAPSULATED_PACKET_TASK_OFFLOAD_OUTER_IPV6       0x00000008
typedef struct _NDIS_ENCAPSULATED_PACKET_TASK_OFFLOAD {
    ULONG TransmitChecksumOffloadSupported:4;
    ULONG ReceiveChecksumOffloadSupported:4;
    ULONG LsoV2Supported:4;
    ULONG RssSupported:4;
    ULONG VmqSupported:4;
#if (NDIS_SUPPORT_NDIS685)
    ULONG UsoSupported:4;
    ULONG Reserved:8;
#endif 
    ULONG MaxHeaderSizeSupported;
} NDIS_ENCAPSULATED_PACKET_TASK_OFFLOAD, *PNDIS_ENCAPSULATED_PACKET_TASK_OFFLOAD;

#define NDIS_SIZEOF_ENCAPSULATED_PACKET_TASK_OFFLOAD_REVISION_1     \
        RTL_SIZEOF_THROUGH_FIELD(                                   \
            NDIS_ENCAPSULATED_PACKET_TASK_OFFLOAD, MaxHeaderSizeSupported)
#endif
#if (NDIS_SUPPORT_NDIS650)
typedef struct _NDIS_ENCAPSULATED_PACKET_TASK_OFFLOAD_V2 {
    ULONG TransmitChecksumOffloadSupported:4;
    ULONG ReceiveChecksumOffloadSupported:4;
    ULONG LsoV2Supported:4;
    ULONG RssSupported:4;
    ULONG VmqSupported:4;
#if (NDIS_SUPPORT_NDIS685)
    ULONG UsoSupported:4;
    ULONG Reserved:8;
#else
    ULONG Reserved:12;
#endif 
    ULONG MaxHeaderSizeSupported;

    union _ENCAPSULATION_PROTOCOL_INFO {
        struct _VXLAN_INFO {
           USHORT VxlanUDPPortNumber;
           USHORT VxlanUDPPortNumberConfigurable  :1;
        } VxlanInfo;

        ULONG Value;

    } EncapsulationProtocolInfo;

    ULONG Reserved1;
    ULONG Reserved2;
} NDIS_ENCAPSULATED_PACKET_TASK_OFFLOAD_V2, *PNDIS_ENCAPSULATED_PACKET_TASK_OFFLOAD_V2;

#endif
#if (NDIS_SUPPORT_NDIS670)
typedef enum _NDIS_RFC6877_464XLAT_OFFLOAD_OPTIONS
{
    NDIS_RFC6877_464XLAT_OFFLOAD_NOT_SUPPORTED = 0,    
    NDIS_RFC6877_464XLAT_OFFLOAD_DISABLED,             
    NDIS_RFC6877_464XLAT_OFFLOAD_ENABLED,              
    NDIS_RFC6877_464XLAT_OFFLOAD_ON_DEMAND,            
} NDIS_RFC6877_464XLAT_OFFLOAD_OPTIONS;

typedef struct _NDIS_RFC6877_464XLAT_OFFLOAD
{
    NDIS_RFC6877_464XLAT_OFFLOAD_OPTIONS   XlatOffload;
    ULONG Flags;                                       
} NDIS_RFC6877_464XLAT_OFFLOAD, *PNDIS_RFC6877_464XLAT_OFFLOAD;

#endif
#if (NDIS_SUPPORT_NDIS683)
typedef struct _NDIS_UDP_SEGMENTATION_OFFLOAD
{
    struct
    {
        ULONG     Encapsulation;
        ULONG     MaxOffLoadSize;
        ULONG     MinSegmentCount : 6;
#if (NDIS_SUPPORT_NDIS684)
        ULONG     SubMssFinalSegmentSupported : 1;
        ULONG     Reserved : 25;
#else
        ULONG     Reserved : 26;
#endif 
    } IPv4;

    struct
    {
        ULONG     Encapsulation;
        ULONG     MaxOffLoadSize;
        ULONG     MinSegmentCount : 6;
#if (NDIS_SUPPORT_NDIS684)
        ULONG     SubMssFinalSegmentSupported : 1;
        ULONG     Reserved1 : 25;
#else
        ULONG     Reserved1 : 26;
#endif 
        ULONG     IpExtensionHeadersSupported : 2;
        ULONG     Reserved2 : 30;
    } IPv6;
} NDIS_UDP_SEGMENTATION_OFFLOAD, *PNDIS_UDP_SEGMENTATION_OFFLOAD;

#endif
#define NDIS_OFFLOAD_FLAGS_GROUP_CHECKSUM_CAPABILITIES  0x00000001
#if (NDIS_SUPPORT_NDIS630)
#define IPSEC_OFFLOAD_V2_AND_TCP_CHECKSUM_COEXISTENCE  0x00000002
#define IPSEC_OFFLOAD_V2_AND_UDP_CHECKSUM_COEXISTENCE  0x00000004
#endif
#if (NDIS_SUPPORT_NDIS650)
#define NDIS_OFFLOAD_REVISION_4    4
#endif
#if (NDIS_SUPPORT_NDIS670)
#define NDIS_OFFLOAD_REVISION_5    5
#endif
#if (NDIS_SUPPORT_NDIS683)
#define NDIS_OFFLOAD_REVISION_6    6
#endif
#if (NDIS_SUPPORT_NDIS685)
#define NDIS_OFFLOAD_REVISION_7    7
#endif
#if (NDIS_SUPPORT_NDIS689)
#define NDIS_OFFLOAD_REVISION_8    8
#endif
typedef struct _NDIS_OFFLOAD
{
    NDIS_OBJECT_HEADER                       Header;

    
    
    
    NDIS_TCP_IP_CHECKSUM_OFFLOAD             Checksum;

    
    
    
    NDIS_TCP_LARGE_SEND_OFFLOAD_V1           LsoV1;

    
    
    
    NDIS_IPSEC_OFFLOAD_V1                    IPsecV1;

    
    
    
    NDIS_TCP_LARGE_SEND_OFFLOAD_V2           LsoV2;

    ULONG                                    Flags;

#if (NDIS_SUPPORT_NDIS61)
    
    
    
    NDIS_IPSEC_OFFLOAD_V2                    IPsecV2;
#endif 

#if (NDIS_SUPPORT_NDIS630)
    
    
    
    NDIS_TCP_RECV_SEG_COALESCE_OFFLOAD       Rsc;

    
    
    
    NDIS_ENCAPSULATED_PACKET_TASK_OFFLOAD    EncapsulatedPacketTaskOffloadGre;
#endif 

#if (NDIS_SUPPORT_NDIS650)
    
    
    
    NDIS_ENCAPSULATED_PACKET_TASK_OFFLOAD_V2 EncapsulatedPacketTaskOffloadVxlan;

    
    
    
    UCHAR                                    EncapsulationTypes;
#endif 

#if (NDIS_SUPPORT_NDIS670)
    
    
    
    NDIS_RFC6877_464XLAT_OFFLOAD             Rfc6877Xlat;

#endif 

#if (NDIS_SUPPORT_NDIS683)
    
    
    
    NDIS_UDP_SEGMENTATION_OFFLOAD            UdpSegmentation;
#endif 

#if (NDIS_SUPPORT_NDIS689)
    
    
    
    NDIS_UDP_RSC_OFFLOAD                     UdpRsc;
#endif 

} NDIS_OFFLOAD, *PNDIS_OFFLOAD;

#if (NDIS_SUPPORT_NDIS61)
#define NDIS_SIZEOF_NDIS_OFFLOAD_REVISION_2   RTL_SIZEOF_THROUGH_FIELD(NDIS_OFFLOAD, IPsecV2)
#endif
#if (NDIS_SUPPORT_NDIS630)
#define NDIS_SIZEOF_NDIS_OFFLOAD_REVISION_3   RTL_SIZEOF_THROUGH_FIELD(NDIS_OFFLOAD, EncapsulatedPacketTaskOffloadGre)
#endif
#if (NDIS_SUPPORT_NDIS650)
#define NDIS_SIZEOF_NDIS_OFFLOAD_REVISION_4   RTL_SIZEOF_THROUGH_FIELD(NDIS_OFFLOAD, EncapsulationTypes)
#endif
#if (NDIS_SUPPORT_NDIS670)
#define NDIS_SIZEOF_NDIS_OFFLOAD_REVISION_5   RTL_SIZEOF_THROUGH_FIELD(NDIS_OFFLOAD, Rfc6877Xlat)
#endif
#if (NDIS_SUPPORT_NDIS683)
#define NDIS_SIZEOF_NDIS_OFFLOAD_REVISION_6   RTL_SIZEOF_THROUGH_FIELD(NDIS_OFFLOAD, UdpSegmentation)
#endif
#if (NDIS_SUPPORT_NDIS685)
#define NDIS_SIZEOF_NDIS_OFFLOAD_REVISION_7   RTL_SIZEOF_THROUGH_FIELD(NDIS_OFFLOAD, UdpSegmentation)
#endif
#if (NDIS_SUPPORT_NDIS689)
#define NDIS_SIZEOF_NDIS_OFFLOAD_REVISION_8   RTL_SIZEOF_THROUGH_FIELD(NDIS_OFFLOAD, UdpRsc)
#endif
typedef struct _NDIS_WMI_TCP_LARGE_SEND_OFFLOAD_V1
{
    struct
    {
        ULONG     Encapsulation;
        ULONG     MaxOffLoadSize;
        ULONG     MinSegmentCount;
        ULONG     TcpOptions;
        ULONG     IpOptions;
    } IPv4;

} NDIS_WMI_TCP_LARGE_SEND_OFFLOAD_V1, *PNDIS_WMI_TCP_LARGE_SEND_OFFLOAD_V1;

typedef struct _NDIS_WMI_TCP_IP_CHECKSUM_OFFLOAD
{

    struct
    {
        ULONG       Encapsulation;
        ULONG       IpOptionsSupported;
        ULONG       TcpOptionsSupported;
        ULONG       TcpChecksum;
        ULONG       UdpChecksum;
        ULONG       IpChecksum;
    } IPv4Transmit;

    struct
    {
        ULONG       Encapsulation;
        ULONG       IpOptionsSupported;
        ULONG       TcpOptionsSupported;
        ULONG       TcpChecksum;
        ULONG       UdpChecksum;
        ULONG       IpChecksum;
    } IPv4Receive;


    struct
    {
        ULONG       Encapsulation;
        ULONG       IpExtensionHeadersSupported;
        ULONG       TcpOptionsSupported;
        ULONG       TcpChecksum;
        ULONG       UdpChecksum;

    } IPv6Transmit;

    struct
    {
        ULONG       Encapsulation;
        ULONG       IpExtensionHeadersSupported;
        ULONG       TcpOptionsSupported;
        ULONG       TcpChecksum;
        ULONG       UdpChecksum;

    } IPv6Receive;

} NDIS_WMI_TCP_IP_CHECKSUM_OFFLOAD, *PNDIS_WMI_TCP_IP_CHECKSUM_OFFLOAD;

typedef struct _NDIS_WMI_IPSEC_OFFLOAD_V1
{
    struct
    {
        ULONG   Encapsulation;
        ULONG   AhEspCombined;
        ULONG   TransportTunnelCombined;
        ULONG   IPv4Options;
        ULONG   Flags;
    } Supported;

    struct
    {
        ULONG   Md5;
        ULONG   Sha_1;
        ULONG   Transport;
        ULONG   Tunnel;
        ULONG   Send;
        ULONG   Receive;
    } IPv4AH;

    struct
    {
        ULONG   Des;
        ULONG   Reserved;
        ULONG   TripleDes;
        ULONG   NullEsp;
        ULONG   Transport;
        ULONG   Tunnel;
        ULONG   Send;
        ULONG   Receive;
    } IPv4ESP;

} NDIS_WMI_IPSEC_OFFLOAD_V1, *PNDIS_WMI_IPSEC_OFFLOAD_V1;

typedef struct _NDIS_WMI_TCP_LARGE_SEND_OFFLOAD_V2
{
    struct
    {
         ULONG     Encapsulation;
         ULONG     MaxOffLoadSize;
         ULONG     MinSegmentCount;
    }IPv4;

    struct
    {
         ULONG     Encapsulation;
         ULONG     MaxOffLoadSize;
         ULONG     MinSegmentCount;
         ULONG     IpExtensionHeadersSupported;
         ULONG     TcpOptionsSupported;
    }IPv6;

} NDIS_WMI_TCP_LARGE_SEND_OFFLOAD_V2, *PNDIS_WMI_TCP_LARGE_SEND_OFFLOAD_V2;

typedef struct _NDIS_WMI_OFFLOAD
{
    
    
    
    
    
    NDIS_OBJECT_HEADER                  Header;

    
    
    
    NDIS_WMI_TCP_IP_CHECKSUM_OFFLOAD    Checksum;

    
    
    
    NDIS_WMI_TCP_LARGE_SEND_OFFLOAD_V1   LsoV1;

    
    
    
    NDIS_WMI_IPSEC_OFFLOAD_V1           IPsecV1;
    
    
    
    NDIS_WMI_TCP_LARGE_SEND_OFFLOAD_V2  LsoV2;

    ULONG                               Flags;

#if (NDIS_SUPPORT_NDIS61)
    
    
    
    NDIS_IPSEC_OFFLOAD_V2               IPsecV2;
#endif 


#if (NDIS_SUPPORT_NDIS630)
    
    
    
    NDIS_TCP_RECV_SEG_COALESCE_OFFLOAD  Rsc;

    
    
    
    NDIS_ENCAPSULATED_PACKET_TASK_OFFLOAD EncapsulatedPacketTaskOffloadGre;
#endif 

#if (NDIS_SUPPORT_NDIS683)
    
    
    
    NDIS_UDP_SEGMENTATION_OFFLOAD            UdpSegmentation;
#endif 

#if (NDIS_SUPPORT_NDIS689)
    
    
    
    NDIS_UDP_RSC_OFFLOAD              UdpRsc;
#endif 

} NDIS_WMI_OFFLOAD, *PNDIS_WMI_OFFLOAD;

#define NDIS_SIZEOF_NDIS_WMI_OFFLOAD_REVISION_1   RTL_SIZEOF_THROUGH_FIELD(NDIS_WMI_OFFLOAD, Flags)
#if (NDIS_SUPPORT_NDIS61)
#define NDIS_SIZEOF_NDIS_WMI_OFFLOAD_REVISION_2   RTL_SIZEOF_THROUGH_FIELD(NDIS_WMI_OFFLOAD, IPsecV2)
#endif
#if (NDIS_SUPPORT_NDIS630)
#define NDIS_SIZEOF_NDIS_WMI_OFFLOAD_REVISION_3   RTL_SIZEOF_THROUGH_FIELD(NDIS_WMI_OFFLOAD, EncapsulatedPacketTaskOffloadGre)
#endif
#if (NDIS_SUPPORT_NDIS683)
#define NDIS_SIZEOF_NDIS_WMI_OFFLOAD_REVISION_4   RTL_SIZEOF_THROUGH_FIELD(NDIS_WMI_OFFLOAD, UdpSegmentation)
#endif
#if (NDIS_SUPPORT_NDIS689)
#define NDIS_SIZEOF_NDIS_WMI_OFFLOAD_REVISION_5   RTL_SIZEOF_THROUGH_FIELD(NDIS_WMI_OFFLOAD, UdpRsc)
#endif
#define NDIS_TCP_CONNECTION_OFFLOAD_REVISION_1              1
#if (NDIS_SUPPORT_NDIS61)
#define NDIS_TCP_CONNECTION_OFFLOAD_REVISION_2              2
#endif
typedef struct _NDIS_TCP_CONNECTION_OFFLOAD
{
    
    
    
    
    
    NDIS_OBJECT_HEADER Header;
    ULONG Encapsulation;
    ULONG SupportIPv4:2;
    ULONG SupportIPv6:2;
    ULONG SupportIPv6ExtensionHeaders:2;
    ULONG SupportSack:2;
#if (NDIS_SUPPORT_NDIS61)
    ULONG CongestionAlgorithm:4;
#endif 
    ULONG TcpConnectionOffloadCapacity;
    ULONG Flags;
} NDIS_TCP_CONNECTION_OFFLOAD, *PNDIS_TCP_CONNECTION_OFFLOAD;

#define NDIS_SIZEOF_TCP_CONNECTION_OFFLOAD_REVISION_1 RTL_SIZEOF_THROUGH_FIELD(NDIS_TCP_CONNECTION_OFFLOAD, Flags)
#if (NDIS_SUPPORT_NDIS61)
#define NDIS_SIZEOF_TCP_CONNECTION_OFFLOAD_REVISION_2 RTL_SIZEOF_THROUGH_FIELD(NDIS_TCP_CONNECTION_OFFLOAD, Flags)
#endif
typedef struct _NDIS_WMI_TCP_CONNECTION_OFFLOAD
{
    
    
    
    
    
    NDIS_OBJECT_HEADER Header;
    ULONG Encapsulation;
    ULONG SupportIPv4;
    ULONG SupportIPv6;
    ULONG SupportIPv6ExtensionHeaders;
    ULONG SupportSack;
    ULONG TcpConnectionOffloadCapacity;
    ULONG Flags;
} NDIS_WMI_TCP_CONNECTION_OFFLOAD, *PNDIS_WMI_TCP_CONNECTION_OFFLOAD;

#define NDIS_SIZEOF_WMI_TCP_CONNECTION_OFFLOAD_REVISION_1 RTL_SIZEOF_THROUGH_FIELD(NDIS_WMI_TCP_CONNECTION_OFFLOAD, Flags)
#ifdef NDIS_INCLUDE_LEGACY_NAMES
typedef NDIS_PORT_CONTROL_STATE  NDIS_PORT_CONTROLL_STATE;

typedef PNDIS_PORT_CONTROL_STATE PNDIS_PORT_CONTROLL_STATE;

#endif
typedef enum _NDIS_NETWORK_CHANGE_TYPE
{
    NdisPossibleNetworkChange = 1,
    NdisDefinitelyNetworkChange,
    NdisNetworkChangeFromMediaConnect,
    NdisNetworkChangeMax
} NDIS_NETWORK_CHANGE_TYPE, *PNDIS_NETWORK_CHANGE_TYPE;

#define  NDIS_WMI_DEFAULT_METHOD_ID             1
#define  NDIS_WMI_OBJECT_TYPE_SET               0x01
#define  NDIS_WMI_OBJECT_TYPE_METHOD            0x02
#define  NDIS_WMI_OBJECT_TYPE_EVENT             0x03
#define  NDIS_WMI_OBJECT_TYPE_ENUM_ADAPTER      0x04
#define  NDIS_WMI_OBJECT_TYPE_OUTPUT_INFO       0x05
#define NDIS_WMI_METHOD_HEADER_REVISION_1       1
typedef struct _NDIS_WMI_METHOD_HEADER
{
    NDIS_OBJECT_HEADER   Header;
    NDIS_PORT_NUMBER     PortNumber;
    NET_LUID             NetLuid;
    ULONG64              RequestId;
    ULONG                Timeout;
    UCHAR                Padding[4];
} NDIS_WMI_METHOD_HEADER, *PNDIS_WMI_METHOD_HEADER;

#define NDIS_SIZEOF_WMI_METHOD_HEADER_REVISION_1     \
        RTL_SIZEOF_THROUGH_FIELD(NDIS_WMI_METHOD_HEADER, Padding)
#define NDIS_WMI_SET_HEADER_REVISION_1         1
typedef struct _NDIS_WMI_SET_HEADER
{
    NDIS_OBJECT_HEADER   Header;
    NDIS_PORT_NUMBER     PortNumber;
    NET_LUID             NetLuid;
    ULONG64              RequestId;
    ULONG                Timeout;
    UCHAR                Padding[4];
} NDIS_WMI_SET_HEADER, *PNDIS_WMI_SET_HEADER;

#define NDIS_SIZEOF_WMI_SET_HEADER_REVISION_1     \
        RTL_SIZEOF_THROUGH_FIELD(NDIS_WMI_SET_HEADER, Padding)
#define NDIS_WMI_EVENT_HEADER_REVISION_1      1
typedef struct _NDIS_WMI_EVENT_HEADER
{
    NDIS_OBJECT_HEADER   Header;
    NET_IFINDEX          IfIndex;
    NET_LUID             NetLuid;
    ULONG64              RequestId;
    NDIS_PORT_NUMBER     PortNumber;
    ULONG                DeviceNameLength;
    ULONG                DeviceNameOffset;
    UCHAR                Padding[4];
} NDIS_WMI_EVENT_HEADER, *PNDIS_WMI_EVENT_HEADER;

#define NDIS_SIZEOF_WMI_EVENT_HEADER_REVISION_1     \
        RTL_SIZEOF_THROUGH_FIELD(NDIS_WMI_EVENT_HEADER, Padding)
#define NDIS_WMI_ENUM_ADAPTER_REVISION_1       1
typedef struct _NDIS_WMI_ENUM_ADAPTER
{
    NDIS_OBJECT_HEADER   Header;
    NET_IFINDEX          IfIndex;
    NET_LUID             NetLuid;
    USHORT               DeviceNameLength;
    CHAR                 DeviceName[1];
}NDIS_WMI_ENUM_ADAPTER, *PNDIS_WMI_ENUM_ADAPTER;

#define NDIS_SIZEOF_WMI_ENUM_ADAPTER_REVISION_1     \
        RTL_SIZEOF_THROUGH_FIELD(NDIS_WMI_ENUM_ADAPTER, DeviceName)
#define NDIS_DEVICE_TYPE_ENDPOINT           0x00000001
#if (NDIS_SUPPORT_NDIS61)
#define NDIS_HD_SPLIT_PARAMETERS_REVISION_1      1
typedef struct _NDIS_HD_SPLIT_PARAMETERS
{
    
    
    
    
    
    NDIS_OBJECT_HEADER      Header;
    ULONG                   HDSplitCombineFlags;
}NDIS_HD_SPLIT_PARAMETERS, *PNDIS_HD_SPLIT_PARAMETERS;

#define NDIS_SIZEOF_HD_SPLIT_PARAMETERS_REVISION_1     \
        RTL_SIZEOF_THROUGH_FIELD(NDIS_HD_SPLIT_PARAMETERS, HDSplitCombineFlags)
#define NDIS_HD_SPLIT_COMBINE_ALL_HEADERS          0x00000001
#define NDIS_HD_SPLIT_CURRENT_CONFIG_REVISION_1      1
typedef struct _NDIS_HD_SPLIT_CURRENT_CONFIG
{
    
    
    
    
    
    NDIS_OBJECT_HEADER          Header;
    ULONG                       HardwareCapabilities;
    ULONG                       CurrentCapabilities;
    ULONG                       HDSplitFlags;
    ULONG                       HDSplitCombineFlags;
    ULONG                       BackfillSize;
    ULONG                       MaxHeaderSize;
} NDIS_HD_SPLIT_CURRENT_CONFIG, *PNDIS_HD_SPLIT_CURRENT_CONFIG;

#define NDIS_SIZEOF_HD_SPLIT_CURRENT_CONFIG_REVISION_1     \
        RTL_SIZEOF_THROUGH_FIELD(NDIS_HD_SPLIT_CURRENT_CONFIG, MaxHeaderSize)
#define NDIS_HD_SPLIT_CAPS_SUPPORTS_HEADER_DATA_SPLIT         0x00000001
#define NDIS_HD_SPLIT_CAPS_SUPPORTS_IPV4_OPTIONS              0x00000002
#define NDIS_HD_SPLIT_CAPS_SUPPORTS_IPV6_EXTENSION_HEADERS    0x00000004
#define NDIS_HD_SPLIT_CAPS_SUPPORTS_TCP_OPTIONS               0x00000008
#define NDIS_HD_SPLIT_ENABLE_HEADER_DATA_SPLIT     0x00000001
#endif
#define NDIS_WMI_OUTPUT_INFO_REVISION_1    1;
typedef struct NDIS_WMI_OUTPUT_INFO
{
    NDIS_OBJECT_HEADER   Header;
    ULONG                Flags;
    UCHAR                SupportedRevision;
    ULONG                DataOffset;

} NDIS_WMI_OUTPUT_INFO, *PNDIS_WMI_OUTPUT_INFO;

#define NDIS_SIZEOF_WMI_OUTPUT_INFO_REVISION_1     \
        RTL_SIZEOF_THROUGH_FIELD(NDIS_WMI_OUTPUT_INFO, DataOffset)
#if (NDIS_SUPPORT_NDIS620)
#define NDIS_PM_WOL_BITMAP_PATTERN_SUPPORTED                    0x00000001
#define NDIS_PM_WOL_MAGIC_PACKET_SUPPORTED                      0x00000002
#define NDIS_PM_WOL_IPV4_TCP_SYN_SUPPORTED                      0x00000004
#define NDIS_PM_WOL_IPV6_TCP_SYN_SUPPORTED                      0x00000008
#define NDIS_PM_WOL_IPV4_DEST_ADDR_WILDCARD_SUPPORTED           0x00000200
#define NDIS_PM_WOL_IPV6_DEST_ADDR_WILDCARD_SUPPORTED           0x00000800
#define NDIS_PM_WOL_EAPOL_REQUEST_ID_MESSAGE_SUPPORTED          0x00010000
#define NDIS_PM_PROTOCOL_OFFLOAD_ARP_SUPPORTED                  0x00000001
#define NDIS_PM_PROTOCOL_OFFLOAD_NS_SUPPORTED                   0x00000002
#define NDIS_PM_PROTOCOL_OFFLOAD_80211_RSN_REKEY_SUPPORTED      0x00000080
#if (NDIS_SUPPORT_NDIS630)
#define NDIS_PM_WAKE_ON_MEDIA_CONNECT_SUPPORTED                   0x00000001
#define NDIS_PM_WAKE_ON_MEDIA_DISCONNECT_SUPPORTED                0x00000002
#endif
#if (NDIS_SUPPORT_NDIS630)
#define NDIS_WLAN_WAKE_ON_NLO_DISCOVERY_SUPPORTED                 0x00000001
#define NDIS_WLAN_WAKE_ON_AP_ASSOCIATION_LOST_SUPPORTED           0x00000002
#define NDIS_WLAN_WAKE_ON_GTK_HANDSHAKE_ERROR_SUPPORTED           0x00000004
#define NDIS_WLAN_WAKE_ON_4WAY_HANDSHAKE_REQUEST_SUPPORTED        0x00000008
#endif
#if (NDIS_SUPPORT_NDIS688)
#define NDIS_WLAN_WAKE_ON_INCOMING_ACTION_FRAME_SUPPORTED         0x00000010
#endif
#if (NDIS_SUPPORT_NDIS689)
#define NDIS_WLAN_WAKE_ON_CLIENT_DRIVER_DIAGNOSTIC_SUPPORTED     0x00000020
#endif
#if (NDIS_SUPPORT_NDIS630)
#define NDIS_WWAN_WAKE_ON_REGISTER_STATE_SUPPORTED                0x00000001
#define NDIS_WWAN_WAKE_ON_SMS_RECEIVE_SUPPORTED                   0x00000002
#define NDIS_WWAN_WAKE_ON_USSD_RECEIVE_SUPPORTED                  0x00000004
#define NDIS_WWAN_WAKE_ON_PACKET_STATE_SUPPORTED                  0x00000008
#define NDIS_WWAN_WAKE_ON_UICC_CHANGE_SUPPORTED                   0x00000010
#endif
#if (NDIS_SUPPORT_NDIS630)
#define NDIS_PM_WAKE_PACKET_INDICATION_SUPPORTED                0x00000001
#define NDIS_PM_SELECTIVE_SUSPEND_SUPPORTED                     0x00000002
#endif
#define NDIS_PM_WOL_BITMAP_PATTERN_ENABLED                      0x00000001
#define NDIS_PM_WOL_MAGIC_PACKET_ENABLED                        0x00000002
#define NDIS_PM_WOL_IPV4_TCP_SYN_ENABLED                        0x00000004
#define NDIS_PM_WOL_IPV6_TCP_SYN_ENABLED                        0x00000008
#define NDIS_PM_WOL_IPV4_DEST_ADDR_WILDCARD_ENABLED             0x00000200
#define NDIS_PM_WOL_IPV6_DEST_ADDR_WILDCARD_ENABLED             0x00000800
#define NDIS_PM_WOL_EAPOL_REQUEST_ID_MESSAGE_ENABLED            0x00010000
#define NDIS_PM_PROTOCOL_OFFLOAD_ARP_ENABLED                    0x00000001
#define NDIS_PM_PROTOCOL_OFFLOAD_NS_ENABLED                     0x00000002
#define NDIS_PM_PROTOCOL_OFFLOAD_80211_RSN_REKEY_ENABLED        0x00000080
#define NDIS_PM_WAKE_ON_LINK_CHANGE_ENABLED                     0x00000001
#if (NDIS_SUPPORT_NDIS630)
#define NDIS_PM_WAKE_ON_MEDIA_DISCONNECT_ENABLED                0x00000002
#define NDIS_PM_SELECTIVE_SUSPEND_ENABLED                       0x00000010
#endif
#if (NDIS_SUPPORT_NDIS630)
#define NDIS_WLAN_WAKE_ON_NLO_DISCOVERY_ENABLED                 0x00000001
#define NDIS_WLAN_WAKE_ON_AP_ASSOCIATION_LOST_ENABLED           0x00000002
#define NDIS_WLAN_WAKE_ON_GTK_HANDSHAKE_ERROR_ENABLED           0x00000004
#define NDIS_WLAN_WAKE_ON_4WAY_HANDSHAKE_REQUEST_ENABLED        0x00000008
#endif
#if (NDIS_SUPPORT_NDIS688)
#define NDIS_WLAN_WAKE_ON_INCOMING_ACTION_FRAME_ENABLED         0x00000010
#endif
#if (NDIS_SUPPORT_NDIS689)
#define NDIS_WLAN_WAKE_ON_CLIENT_DRIVER_DIAGNOSTIC_ENABLED     0x00000020
#endif
#if (NDIS_SUPPORT_NDIS630)
#define NDIS_WWAN_WAKE_ON_REGISTER_STATE_ENABLED                0x00000001
#define NDIS_WWAN_WAKE_ON_SMS_RECEIVE_ENABLED                   0x00000002
#define NDIS_WWAN_WAKE_ON_USSD_RECEIVE_ENABLED                  0x00000004
#endif
#if (NDIS_SUPPORT_NDIS650)
#define NDIS_WWAN_WAKE_ON_PACKET_STATE_ENABLED                  0x00000008
#define NDIS_WWAN_WAKE_ON_UICC_CHANGE_ENABLED                   0x00000010
#endif
#define NDIS_PM_WOL_PRIORITY_LOWEST                             0xFFFFFFFF
#define NDIS_PM_WOL_PRIORITY_NORMAL                             0x10000000
#define NDIS_PM_WOL_PRIORITY_HIGHEST                            0x00000001
#define NDIS_PM_PROTOCOL_OFFLOAD_PRIORITY_LOWEST                0xFFFFFFFF
#define NDIS_PM_PROTOCOL_OFFLOAD_PRIORITY_NORMAL                0x10000000
#define NDIS_PM_PROTOCOL_OFFLOAD_PRIORITY_HIGHEST               0x00000001
typedef enum _NDIS_PM_PROTOCOL_OFFLOAD_TYPE
{
    NdisPMProtocolOffloadIdUnspecified,
    NdisPMProtocolOffloadIdIPv4ARP,
    NdisPMProtocolOffloadIdIPv6NS,
    NdisPMProtocolOffload80211RSNRekey,
#if (NDIS_SUPPORT_NDIS684)
    NdisPMProtocolOffload80211RSNRekeyV2,
#endif 
    NdisPMProtocolOffloadIdMaximum
}NDIS_PM_PROTOCOL_OFFLOAD_TYPE, *PNDIS_PM_PROTOCOL_OFFLOAD_TYPE;

#define NDIS_PM_PARAMETERS_REVISION_1              1
#if (NDIS_SUPPORT_NDIS630)
#define NDIS_PM_PARAMETERS_REVISION_2              2
#endif
typedef struct _NDIS_PM_PARAMETERS
{
    
    
    
    
    
    NDIS_OBJECT_HEADER      Header;

    ULONG                   EnabledWoLPacketPatterns; 
    ULONG                   EnabledProtocolOffloads;  
    ULONG                   WakeUpFlags;              

#if (NDIS_SUPPORT_NDIS630)
    ULONG                   MediaSpecificWakeUpEvents; 
#endif 

} NDIS_PM_PARAMETERS, *PNDIS_PM_PARAMETERS;

#define NDIS_SIZEOF_NDIS_PM_PARAMETERS_REVISION_1     \
    RTL_SIZEOF_THROUGH_FIELD(NDIS_PM_PARAMETERS, WakeUpFlags)
#if (NDIS_SUPPORT_NDIS630)
#define NDIS_SIZEOF_NDIS_PM_PARAMETERS_REVISION_2     \
    RTL_SIZEOF_THROUGH_FIELD(NDIS_PM_PARAMETERS, MediaSpecificWakeUpEvents)
#endif
#define EAPOL_REQUEST_ID_WOL_FLAG_MUST_ENCRYPT        0x00000001
#define NDIS_PM_MAX_PATTERN_ID  0x0000FFFF
#define NDIS_PM_PRIVATE_PATTERN_ID  0x00000001
#if (NDIS_SUPPORT_NDIS630)
#define NDIS_PM_WOL_PATTERN_REVISION_2              2
#endif
#define NDIS_SIZEOF_NDIS_PM_WOL_PATTERN_REVISION_1     \
    RTL_SIZEOF_THROUGH_FIELD(NDIS_PM_WOL_PATTERN, WoLPattern)
#if (NDIS_SUPPORT_NDIS630)
#define NDIS_SIZEOF_NDIS_PM_WOL_PATTERN_REVISION_2     \
    RTL_SIZEOF_THROUGH_FIELD(NDIS_PM_WOL_PATTERN, WoLPattern)
#endif
#define DOT11_RSN_KCK_LENGTH                  16
#define DOT11_RSN_KEK_LENGTH                  16
#if (NDIS_SUPPORT_NDIS684)
#define DOT11_RSN_MAX_CIPHER_KEY_LENGTH       32
#endif
#define NDIS_PM_PROTOCOL_OFFLOAD_REVISION_1             1
#if (NDIS_SUPPORT_NDIS684)
#define NDIS_PM_PROTOCOL_OFFLOAD_REVISION_2             2
#endif
typedef struct _NDIS_PM_PROTOCOL_OFFLOAD
{
    
    
    
    
    
    NDIS_OBJECT_HEADER            Header;
    ULONG                         Flags;
    ULONG                         Priority;
    NDIS_PM_PROTOCOL_OFFLOAD_TYPE ProtocolOffloadType;
    NDIS_PM_COUNTED_STRING        FriendlyName;
    ULONG                         ProtocolOffloadId; 
    ULONG                         NextProtocolOffloadOffset;

    union _PROTOCOL_OFFLOAD_PARAMETERS
    {
        
        
        
        struct _IPV4_ARP_PARAMETERS
        {
            ULONG   Flags;
            UCHAR   RemoteIPv4Address[4];   
            UCHAR   HostIPv4Address[4];     
            UCHAR   MacAddress[6];          
        }IPv4ARPParameters;

        
        
        
        
        
        
        struct  _IPV6_NS_PARAMETERS
        {
            ULONG   Flags;
            UCHAR   RemoteIPv6Address[16];              
            UCHAR   SolicitedNodeIPv6Address[16];       
            UCHAR   MacAddress[6];                      
            UCHAR   TargetIPv6Addresses[2][16];         
        }IPv6NSParameters;

        
        
        
        struct _DOT11_RSN_REKEY_PARAMETERS
        {
            ULONG   Flags;
            UCHAR   KCK[DOT11_RSN_KCK_LENGTH];
            UCHAR   KEK[DOT11_RSN_KEK_LENGTH];
            ULONGLONG KeyReplayCounter;
        }Dot11RSNRekeyParameters;

#if (NDIS_SUPPORT_NDIS684)
        
        
        
        struct _DOT11_RSN_REKEY_PARAMETERS_V2
        {
            ULONG       Flags;
            ULONGLONG   KeyReplayCounter;

            
            ULONG       AuthAlgo;
            ULONG       KCKLength;
            ULONG       KEKLength;
            UCHAR       KCK [DOT11_RSN_MAX_CIPHER_KEY_LENGTH];
            UCHAR       KEK [DOT11_RSN_MAX_CIPHER_KEY_LENGTH];
        }Dot11RSNRekeyParametersV2;
#endif 

    }ProtocolOffloadParameters;

}NDIS_PM_PROTOCOL_OFFLOAD, *PNDIS_PM_PROTOCOL_OFFLOAD;

#define NDIS_SIZEOF_NDIS_PM_PROTOCOL_OFFLOAD_REVISION_1     \
    0xF0
#if (NDIS_SUPPORT_NDIS684)
#define NDIS_SIZEOF_NDIS_PM_PROTOCOL_OFFLOAD_REVISION_2     \
    RTL_SIZEOF_THROUGH_FIELD(NDIS_PM_PROTOCOL_OFFLOAD, ProtocolOffloadParameters)
#endif
#if (NDIS_SUPPORT_NDIS630)
typedef enum _NDIS_PM_WAKE_REASON_TYPE
{
    NdisWakeReasonUnspecified               = 0x0000,
    NdisWakeReasonPacket                    = 0x0001,
    NdisWakeReasonMediaDisconnect           = 0x0002,
    NdisWakeReasonMediaConnect              = 0x0003,

    
    NdisWakeReasonWlanNLODiscovery          = 0x1000,
    NdisWakeReasonWlanAPAssociationLost     = 0x1001,
    NdisWakeReasonWlanGTKHandshakeError     = 0x1002,
    NdisWakeReasonWlan4WayHandshakeRequest  = 0x1003,
#if (NDIS_SUPPORT_NDIS688)
    NdisWakeReasonWlanIncomingActionFrame   = 0x1004,
#endif 
#if (NDIS_SUPPORT_NDIS689)
    NdisWakeReasonWlanClientDriverDiagnostic = 0x1005,
#endif 
    
    NdisWakeReasonWwanRegisterState         = 0x2000,
    NdisWakeReasonWwanSMSReceive            = 0x2001,
    NdisWakeReasonWwanUSSDReceive           = 0x2002,
    
#if (NDIS_SUPPORT_NDIS684)
    NdisWakeReasonWwanPacketState           = 0x2004,
    NdisWakeReasonWwanUiccChange            = 0x2005,
#endif
} NDIS_PM_WAKE_REASON_TYPE, *PNDIS_PM_WAKE_REASON_TYPE;

#define NDIS_PM_WAKE_REASON_REVISION_1     1
typedef struct _NDIS_PM_WAKE_REASON
{
    
    
    
    
    
    NDIS_OBJECT_HEADER          Header;

    ULONG                       Flags;                  
    NDIS_PM_WAKE_REASON_TYPE    WakeReason;
    ULONG                       InfoBufferOffset;
    ULONG                       InfoBufferSize;
} NDIS_PM_WAKE_REASON, *PNDIS_PM_WAKE_REASON;

#define NDIS_SIZEOF_PM_WAKE_REASON_REVISION_1 \
    RTL_SIZEOF_THROUGH_FIELD(NDIS_PM_WAKE_REASON, InfoBufferSize)
#define NDIS_PM_WAKE_PACKET_REVISION_1     1
typedef struct _NDIS_PM_WAKE_PACKET
{
    
    
    
    
    
    NDIS_OBJECT_HEADER      Header;

    ULONG                   Flags;                  
    ULONG                   PatternId;              
    NDIS_PM_COUNTED_STRING  PatternFriendlyName;    
    ULONG                   OriginalPacketSize;
    ULONG                   SavedPacketSize;
    ULONG                   SavedPacketOffset;
} NDIS_PM_WAKE_PACKET, *PNDIS_PM_WAKE_PACKET;

#define NDIS_SIZEOF_PM_WAKE_PACKET_REVISION_1 \
    RTL_SIZEOF_THROUGH_FIELD(NDIS_PM_WAKE_PACKET, SavedPacketOffset)
#endif
typedef enum _NDIS_PM_ADMIN_CONFIG_STATE
{
    NdisPMAdminConfigUnspecified = 0,
    NdisPMAdminConfigDisabled = 1,
    NdisPMAdminConfigEnabled = 2
} NDIS_PM_ADMIN_CONFIG_STATE, *PNDIS_PM_ADMIN_CONFIG_STATE;

#define NDIS_WMI_PM_ADMIN_CONFIG_REVISION_1     1
typedef struct _NDIS_WMI_PM_ADMIN_CONFIG
{
    
    
    
    
    
    NDIS_OBJECT_HEADER Header;

    NDIS_PM_ADMIN_CONFIG_STATE   WakeOnPattern;
    NDIS_PM_ADMIN_CONFIG_STATE   WakeOnMagicPacket;
    NDIS_PM_ADMIN_CONFIG_STATE   DeviceSleepOnDisconnect;
    NDIS_PM_ADMIN_CONFIG_STATE   PMARPOffload;
    NDIS_PM_ADMIN_CONFIG_STATE   PMNSOffload;
    NDIS_PM_ADMIN_CONFIG_STATE   PMWiFiRekeyOffload;

} NDIS_WMI_PM_ADMIN_CONFIG, *PNDIS_WMI_PM_ADMIN_CONFIG;

#define NDIS_SIZEOF_WMI_PM_ADMIN_CONFIG_REVISION_1 \
    RTL_SIZEOF_THROUGH_FIELD(NDIS_WMI_PM_ADMIN_CONFIG, PMWiFiRekeyOffload)
typedef enum _NDIS_PM_CAPABILITY_STATE
{
    NdisPMAdminConfigUnsupported = 0,
    NdisPMAdminConfigInactive = 1,
    NdisPMAdminConfigActive = 2
} NDIS_PM_CAPABILITY_STATE, *PNDIS_PM_CAPABILITY_STATE;

#define NDIS_WMI_PM_ACTIVE_CAPABILITIES_REVISION_1     1
typedef struct _NDIS_WMI_PM_ACTIVE_CAPABILITIES
{
    
    
    
    
    
    NDIS_OBJECT_HEADER Header;

    NDIS_PM_CAPABILITY_STATE   WakeOnPattern;
    NDIS_PM_CAPABILITY_STATE   WakeOnMagicPacket;
    NDIS_PM_CAPABILITY_STATE   DeviceSleepOnDisconnect;
    NDIS_PM_CAPABILITY_STATE   PMARPOffload;
    NDIS_PM_CAPABILITY_STATE   PMNSOffload;
    NDIS_PM_CAPABILITY_STATE   PMWiFiRekeyOffload;

} NDIS_WMI_PM_ACTIVE_CAPABILITIES, *PNDIS_WMI_PM_ACTIVE_CAPABILITIES;

#define NDIS_SIZEOF_WMI_PM_ACTIVE_CAPABILITIES_REVISION_1 \
    RTL_SIZEOF_THROUGH_FIELD(NDIS_WMI_PM_ACTIVE_CAPABILITIES, PMWiFiRekeyOffload)
#define NDIS_RECEIVE_FILTER_IPV4_HEADER_SUPPORTED           0x00000002
#define NDIS_RECEIVE_FILTER_IPV6_HEADER_SUPPORTED           0x00000004
#define NDIS_RECEIVE_FILTER_ARP_HEADER_SUPPORTED            0x00000008
#define NDIS_RECEIVE_FILTER_UDP_HEADER_SUPPORTED            0x00000010
#define NDIS_RECEIVE_FILTER_MAC_HEADER_SOURCE_ADDR_SUPPORTED    0x00000002
#define NDIS_RECEIVE_FILTER_MAC_HEADER_PROTOCOL_SUPPORTED       0x00000004
#define NDIS_RECEIVE_FILTER_MAC_HEADER_PRIORITY_SUPPORTED       0x00000010
#define NDIS_RECEIVE_FILTER_MAC_HEADER_PACKET_TYPE_SUPPORTED    0x00000020
#define NDIS_RECEIVE_FILTER_ARP_HEADER_OPERATION_SUPPORTED      0x00000001
#define NDIS_RECEIVE_FILTER_ARP_HEADER_SPA_SUPPORTED            0x00000002
#define NDIS_RECEIVE_FILTER_ARP_HEADER_TPA_SUPPORTED            0x00000004
#define NDIS_RECEIVE_FILTER_IPV4_HEADER_PROTOCOL_SUPPORTED      0x00000001
#define NDIS_RECEIVE_FILTER_IPV6_HEADER_PROTOCOL_SUPPORTED      0x00000001
#define NDIS_RECEIVE_FILTER_UDP_HEADER_DEST_PORT_SUPPORTED      0x00000001
#define NDIS_RECEIVE_FILTER_TEST_HEADER_FIELD_MASK_EQUAL_SUPPORTED          0x00000002
#define NDIS_RECEIVE_FILTER_TEST_HEADER_FIELD_NOT_EQUAL_SUPPORTED           0x00000004
#define NDIS_RECEIVE_FILTER_MSI_X_SUPPORTED                         0x00000001
#if (NDIS_SUPPORT_NDIS630)
#define NDIS_RECEIVE_FILTER_INTERRUPT_VECTOR_COALESCING_SUPPORTED       0x00000010
#define NDIS_RECEIVE_FILTER_IMPLAT_MIN_OF_QUEUES_MODE                   0x00000040
#define NDIS_RECEIVE_FILTER_IMPLAT_SUM_OF_QUEUES_MODE                   0x00000080
#define NDIS_RECEIVE_FILTER_PACKET_COALESCING_SUPPORTED_ON_DEFAULT_QUEUE 0x00000100
#define NDIS_RECEIVE_FILTER_ANY_VLAN_SUPPORTED                          0x00000020
#endif
#if (NDIS_SUPPORT_NDIS680)
#define NDIS_RECEIVE_FILTER_DYNAMIC_PROCESSOR_AFFINITY_CHANGE_FOR_DEFAULT_QUEUE_SUPPORTED 0x00000040
#endif
#define NDIS_RECEIVE_FILTER_PACKET_COALESCING_FILTERS_ENABLED       0x00000002
#if (NDIS_SUPPORT_NDIS630)
#define NDIS_RECEIVE_FILTER_CAPABILITIES_REVISION_2     2
#endif
#if (NDIS_SUPPORT_NDIS630)
#define NDIS_SIZEOF_RECEIVE_FILTER_CAPABILITIES_REVISION_2     \
    RTL_SIZEOF_THROUGH_FIELD(NDIS_RECEIVE_FILTER_CAPABILITIES, NdisReserved)
#endif
#if (NDIS_SUPPORT_NDIS630)
#define NDIS_NIC_SWITCH_CAPS_VLAN_SUPPORTED                                                       0x00000001
#define NDIS_NIC_SWITCH_CAPS_PER_VPORT_INTERRUPT_MODERATION_SUPPORTED                             0x00000002
#define NDIS_NIC_SWITCH_CAPS_ASYMMETRIC_QUEUE_PAIRS_FOR_NONDEFAULT_VPORT_SUPPORTED                0x00000004
#define NDIS_NIC_SWITCH_CAPS_VF_RSS_SUPPORTED                                                     0x00000008
#define NDIS_NIC_SWITCH_CAPS_SINGLE_VPORT_POOL                                                    0x00000010
#if (NDIS_SUPPORT_NDIS650)
#define NDIS_NIC_SWITCH_CAPS_RSS_PARAMETERS_PER_PF_VPORT_SUPPORTED                                0x00000020
#endif
#if (NDIS_SUPPORT_NDIS660)
#define NDIS_NIC_SWITCH_CAPS_NIC_SWITCH_WITHOUT_IOV_SUPPORTED                                     0x00000040
#define NDIS_NIC_SWITCH_CAPS_RSS_ON_PF_VPORTS_SUPPORTED                                           0x00000080
#define NDIS_NIC_SWITCH_CAPS_RSS_PER_PF_VPORT_INDIRECTION_TABLE_SUPPORTED                         0x00000100
#define NDIS_NIC_SWITCH_CAPS_RSS_PER_PF_VPORT_HASH_FUNCTION_SUPPORTED                             0x00000200
#define NDIS_NIC_SWITCH_CAPS_RSS_PER_PF_VPORT_HASH_TYPE_SUPPORTED                                 0x00000400
#define NDIS_NIC_SWITCH_CAPS_RSS_PER_PF_VPORT_HASH_KEY_SUPPORTED                                  0x00000800
#define NDIS_NIC_SWITCH_CAPS_RSS_PER_PF_VPORT_INDIRECTION_TABLE_SIZE_RESTRICTED                   0x00001000
#define NDIS_NIC_SWITCH_CAPS_RSS_PER_PF_VPORT_MASK                             \
            (NDIS_NIC_SWITCH_CAPS_RSS_PER_PF_VPORT_HASH_KEY_SUPPORTED |        \
             NDIS_NIC_SWITCH_CAPS_RSS_PER_PF_VPORT_HASH_FUNCTION_SUPPORTED |   \
             NDIS_NIC_SWITCH_CAPS_RSS_PER_PF_VPORT_HASH_TYPE_SUPPORTED |       \
             NDIS_NIC_SWITCH_CAPS_RSS_PER_PF_VPORT_INDIRECTION_TABLE_SUPPORTED)
#endif
#endif
#if (NDIS_SUPPORT_NDIS630)
#define NDIS_NIC_SWITCH_CAPABILITIES_REVISION_2        2
#endif
#if (NDIS_SUPPORT_NDIS660)
#define NDIS_NIC_SWITCH_CAPABILITIES_REVISION_3        3
#endif
#if (NDIS_SUPPORT_NDIS630)
#define NDIS_SIZEOF_NIC_SWITCH_CAPABILITIES_REVISION_2     \
    RTL_SIZEOF_THROUGH_FIELD(NDIS_NIC_SWITCH_CAPABILITIES, NdisReserved17)
#endif
#if (NDIS_SUPPORT_NDIS660)
#define NDIS_SIZEOF_NIC_SWITCH_CAPABILITIES_REVISION_3     \
    RTL_SIZEOF_THROUGH_FIELD(NDIS_NIC_SWITCH_CAPABILITIES, \
                             MaxNumQueuePairsForDefaultVPort)
#endif
#define NDIS_RECEIVE_FILTER_GLOBAL_PARAMETERS_REVISION_1       1
typedef struct _NDIS_RECEIVE_FILTER_GLOBAL_PARAMETERS
{
    _In_  NDIS_OBJECT_HEADER          Header;
    _In_  ULONG                       Flags;
    _In_  ULONG                       EnabledFilterTypes;
    _In_  ULONG                       EnabledQueueTypes;
} NDIS_RECEIVE_FILTER_GLOBAL_PARAMETERS, *PNDIS_RECEIVE_FILTER_GLOBAL_PARAMETERS;

#define NDIS_SIZEOF_RECEIVE_FILTER_GLOBAL_PARAMETERS_REVISION_1     \
    RTL_SIZEOF_THROUGH_FIELD(NDIS_RECEIVE_FILTER_GLOBAL_PARAMETERS, EnabledQueueTypes)
typedef ULONG NDIS_RECEIVE_QUEUE_ID, *PNDIS_RECEIVE_QUEUE_ID;

typedef ULONG NDIS_RECEIVE_QUEUE_GROUP_ID, *PNDIS_RECEIVE_QUEUE_GROUP_ID;

#define NDIS_DEFAULT_RECEIVE_QUEUE_GROUP_ID         0
#define NDIS_DEFAULT_RECEIVE_FILTER_ID              0
typedef ULONG NDIS_RECEIVE_FILTER_ID, *PNDIS_RECEIVE_FILTER_ID;

typedef enum _NDIS_MAC_PACKET_TYPE
{
    NdisMacPacketTypeUndefined = 0,
    NdisMacPacketTypeUnicast = 1,
    NdisMacPacketTypeMulticast = 2,
    NdisMacPacketTypeBroadcast = 3,
    NdisMacPacketTypeMaximum
} NDIS_MAC_PACKET_TYPE, *PNDIS_MAC_PACKET_TYPE;

#define NDIS_RECEIVE_FILTER_RESERVED                                0x000000FE
#define NDIS_RECEIVE_FILTER_FIELD_PARAMETERS_REVISION_1       1
#if (NDIS_SUPPORT_NDIS630)
#define NDIS_RECEIVE_FILTER_FIELD_PARAMETERS_REVISION_2       2
#endif
#define NDIS_SIZEOF_RECEIVE_FILTER_FIELD_PARAMETERS_REVISION_1     \
    RTL_SIZEOF_THROUGH_FIELD(NDIS_RECEIVE_FILTER_FIELD_PARAMETERS, ResultValue)
#if (NDIS_SUPPORT_NDIS630)
#define NDIS_SIZEOF_RECEIVE_FILTER_FIELD_PARAMETERS_REVISION_2     \
    RTL_SIZEOF_THROUGH_FIELD(NDIS_RECEIVE_FILTER_FIELD_PARAMETERS, ResultValue)
#endif
#define NDIS_RECEIVE_FILTER_FLAGS_RESERVED                          0x00000001
#define NDIS_RECEIVE_FILTER_PACKET_ENCAPSULATION_GRE                0x00000002
#if (NDIS_SUPPORT_NDIS650)
#define NDIS_RECEIVE_FILTER_PACKET_ENCAPSULATION                    0x00000002
#endif
#if (NDIS_SUPPORT_NDIS630)
#define NDIS_RECEIVE_FILTER_PARAMETERS_REVISION_2       2
#endif
#if (NDIS_SUPPORT_NDIS630)
#define NDIS_SIZEOF_RECEIVE_FILTER_PARAMETERS_REVISION_2     \
    RTL_SIZEOF_THROUGH_FIELD(NDIS_RECEIVE_FILTER_PARAMETERS, VPortId)
#endif
#define NDIS_RECEIVE_QUEUE_PARAMETERS_PER_QUEUE_RECEIVE_INDICATION              0x00000001
#define NDIS_RECEIVE_QUEUE_PARAMETERS_NAME_CHANGED                              0x00080000
#if (NDIS_SUPPORT_NDIS630)
#define NDIS_RECEIVE_QUEUE_PARAMETERS_INTERRUPT_COALESCING_DOMAIN_ID_CHANGED    0x00100000
#endif
#if (NDIS_SUPPORT_NDIS650)
#define NDIS_RECEIVE_QUEUE_PARAMETERS_QOS_SQ_ID_CHANGED                         0x00200000
#endif
typedef NDIS_IF_COUNTED_STRING NDIS_QUEUE_NAME, *PNDIS_QUEUE_NAME;

typedef NDIS_IF_COUNTED_STRING NDIS_VM_NAME, *PNDIS_VM_NAME;

typedef NDIS_IF_COUNTED_STRING NDIS_VM_FRIENDLYNAME, *PNDIS_VM_FRIENDLYNAME;

typedef NDIS_IF_COUNTED_STRING NDIS_SWITCH_PORT_PROPERTY_PROFILE_NAME, *PNDIS_SWITCH_PORT_PROPERTY_PROFILE_NAME;

typedef NDIS_IF_COUNTED_STRING NDIS_SWITCH_PORT_PROPERTY_PROFILE_CDN_LABEL, *PNDIS_SWITCH_PORT_PROPERTY_PROFILE_CDN_LABEL;

typedef NDIS_IF_COUNTED_STRING NDIS_SWITCH_NAME, *PNDIS_SWITCH_NAME;

typedef NDIS_IF_COUNTED_STRING NDIS_SWITCH_FRIENDLYNAME, *PNDIS_SWITCH_FRIENDLYNAME;

typedef NDIS_IF_COUNTED_STRING NDIS_SWITCH_PORT_NAME, *PNDIS_SWITCH_PORT_NAME;

typedef NDIS_IF_COUNTED_STRING NDIS_SWITCH_PORT_FRIENDLYNAME, *PNDIS_SWITCH_PORT_FRIENDLYNAME;

typedef NDIS_IF_COUNTED_STRING NDIS_SWITCH_NIC_NAME, *PNDIS_SWITCH_NIC_NAME;

typedef NDIS_IF_COUNTED_STRING NDIS_SWITCH_NIC_FRIENDLYNAME, *PNDIS_SWITCH_NIC_FRIENDLYNAME;

typedef NDIS_IF_COUNTED_STRING NDIS_SWITCH_EXTENSION_FRIENDLYNAME, *PNDIS_SWITCH_EXTENSION_FRIENDLYNAME;

typedef NDIS_IF_COUNTED_STRING NDIS_VENDOR_NAME, *PNDIS_VENDOR_NAME;

#if (NDIS_SUPPORT_NDIS650)
typedef ULONG NDIS_QOS_SQ_ID, *PNDIS_QOS_SQ_ID;

#endif
#if (NDIS_SUPPORT_NDIS630)
#define NDIS_RECEIVE_QUEUE_PARAMETERS_REVISION_2       2
#endif
#if (NDIS_SUPPORT_NDIS650)
#define NDIS_RECEIVE_QUEUE_PARAMETERS_REVISION_3       3
#endif
#if (NDIS_SUPPORT_NDIS630)
#define NDIS_SIZEOF_RECEIVE_QUEUE_PARAMETERS_REVISION_2     \
    RTL_SIZEOF_THROUGH_FIELD(NDIS_RECEIVE_QUEUE_PARAMETERS, InterruptCoalescingDomainId)
#endif
#if (NDIS_SUPPORT_NDIS650)
#define NDIS_SIZEOF_RECEIVE_QUEUE_PARAMETERS_REVISION_3     \
    RTL_SIZEOF_THROUGH_FIELD(NDIS_RECEIVE_QUEUE_PARAMETERS, QosSqId)
#endif
#define NDIS_RECEIVE_QUEUE_INFO_REVISION_1              1
#if (NDIS_SUPPORT_NDIS630)
#define NDIS_RECEIVE_QUEUE_INFO_REVISION_2              2
#endif
typedef struct _NDIS_RECEIVE_QUEUE_INFO
{
    NDIS_OBJECT_HEADER                  Header;
    ULONG                               Flags;
    NDIS_RECEIVE_QUEUE_TYPE             QueueType;
    NDIS_RECEIVE_QUEUE_ID               QueueId;
    NDIS_RECEIVE_QUEUE_GROUP_ID         QueueGroupId;
    NDIS_RECEIVE_QUEUE_OPERATIONAL_STATE QueueState;
    GROUP_AFFINITY                      ProcessorAffinity;
    ULONG                               NumSuggestedReceiveBuffers;
    ULONG                               MSIXTableEntry;
    ULONG                               LookaheadSize;
    NDIS_VM_NAME                        VmName;
    NDIS_QUEUE_NAME                     QueueName;
#if (NDIS_SUPPORT_NDIS630)
    ULONG                               NumFilters;
    ULONG                               InterruptCoalescingDomainId;
#endif 
}NDIS_RECEIVE_QUEUE_INFO, *PNDIS_RECEIVE_QUEUE_INFO;

#define NDIS_SIZEOF_RECEIVE_QUEUE_INFO_REVISION_1      \
        RTL_SIZEOF_THROUGH_FIELD(NDIS_RECEIVE_QUEUE_INFO, QueueName)
#if (NDIS_SUPPORT_NDIS630)
#define NDIS_SIZEOF_RECEIVE_QUEUE_INFO_REVISION_2     \
        RTL_SIZEOF_THROUGH_FIELD(NDIS_RECEIVE_QUEUE_INFO, InterruptCoalescingDomainId)
#endif
#define NDIS_RECEIVE_QUEUE_INFO_ARRAY_REVISION_1             1
typedef struct _NDIS_RECEIVE_QUEUE_INFO_ARRAY
{
    NDIS_OBJECT_HEADER                          Header;
    ULONG                                       FirstElementOffset;
    ULONG                                       NumElements;
    ULONG                                       ElementSize;
}NDIS_RECEIVE_QUEUE_INFO_ARRAY, *PNDIS_RECEIVE_QUEUE_INFO_ARRAY;

#define NDIS_SIZEOF_RECEIVE_QUEUE_INFO_ARRAY_REVISION_1      \
        RTL_SIZEOF_THROUGH_FIELD(NDIS_RECEIVE_QUEUE_INFO_ARRAY, ElementSize)
#define NDIS_SIZEOF_RECEIVE_FILTER_INFO_REVISION_1      \
        RTL_SIZEOF_THROUGH_FIELD(NDIS_RECEIVE_FILTER_INFO, FilterId)
#define NDIS_RECEIVE_FILTER_INFO_ARRAY_REVISION_1             1
#if (NDIS_SUPPORT_NDIS630)
#define NDIS_RECEIVE_FILTER_INFO_ARRAY_REVISION_2             2
#endif
#if (NDIS_SUPPORT_NDIS630)
#define NDIS_RECEIVE_FILTER_INFO_ARRAY_VPORT_ID_SPECIFIED       0x00000001
#endif
typedef struct _NDIS_RECEIVE_FILTER_INFO_ARRAY
{
    NDIS_OBJECT_HEADER                          Header;
    NDIS_RECEIVE_QUEUE_ID                       QueueId;
    ULONG                                       FirstElementOffset;
    ULONG                                       NumElements;
    ULONG                                       ElementSize;
#if (NDIS_SUPPORT_NDIS630)
    ULONG                                       Flags;
    NDIS_NIC_SWITCH_VPORT_ID                    VPortId;
#endif 
}NDIS_RECEIVE_FILTER_INFO_ARRAY, *PNDIS_RECEIVE_FILTER_INFO_ARRAY;

#define NDIS_SIZEOF_RECEIVE_FILTER_INFO_ARRAY_REVISION_1      \
        RTL_SIZEOF_THROUGH_FIELD(NDIS_RECEIVE_FILTER_INFO_ARRAY, ElementSize)
#if (NDIS_SUPPORT_NDIS630)
#define NDIS_SIZEOF_RECEIVE_FILTER_INFO_ARRAY_REVISION_2      \
        RTL_SIZEOF_THROUGH_FIELD(NDIS_RECEIVE_FILTER_INFO_ARRAY, VPortId)
#endif
#define NDIS_RECEIVE_QUEUE_ALLOCATION_COMPLETE_PARAMETERS_REVISION_1    1
#define NDIS_SIZEOF_RECEIVE_QUEUE_ALLOCATION_COMPLETE_PARAMETERS_REVISION_1 \
    RTL_SIZEOF_THROUGH_FIELD(NDIS_RECEIVE_QUEUE_ALLOCATION_COMPLETE_PARAMETERS, CompletionStatus)
#endif

#endif
#if ((NDIS_SUPPORT_NDIS650))

typedef struct _NDIS_GFT_OFFLOAD_CAPABILITIES
{
    NDIS_OBJECT_HEADER                  Header;
    ULONG                               Flags;
    ULONG                               CounterCapabilities;
    ULONG                               SupportedTableTypes;
    ULONG                               SupportedEncapsulationTypes;
    ULONG                               SupportedIngressExactMatchTableActions;
    ULONG                               SupportedEgressExactMatchTableActions;
    ULONG                               SoftwareSupportedIngressExactMatchTableActions;
    ULONG                               SoftwareSupportedEgressExactMatchTableActions;
    ULONG                               SupportedIngressWildcardMatchTableActions;
    ULONG                               SupportedEgressWildcardMatchTableActions;
    ULONG                               SoftwareSupportedIngressWildcardMatchTableActions;
    ULONG                               SoftwareSupportedEgressWildcardMatchTableActions;
    ULONG                               NumPacketCounterObjects;
    ULONG                               NumByteCounterObjects;
    ULONG                               NumPacketByteCounterObjects;
    ULONG                               NumPacketByteCounterAndStateObjects;
    ULONG                               NumCounterObjectsPerIngressExactMatchFlowEntry;
    ULONG                               NumCounterObjectsPerEgressExactMatchFlowEntry;
    ULONG                               NumCounterObjectsPerIngressWildcardMatchFlowEntry;
    ULONG                               NumCounterObjectsPerEgressWildcardMatchFlowEntry;
} NDIS_GFT_OFFLOAD_CAPABILITIES, *PNDIS_GFT_OFFLOAD_CAPABILITIES;

typedef ULONG NDIS_QOS_SQ_ID, *PNDIS_QOS_SQ_ID;

#endif

#define NDIS_OFFLOAD_PARAMETERS_DEFINED 1
#define NDIS_RECEIVE_SCALE_PARAMETERS_DEFINED 1
#define NDIS_INTERRUPT_MODERATION_PARAMETERS_DEFINED 1
#define NDIS_LINK_SPEED_DEFINED 1

#endif

#define NDIS_ETH_TYPE_IPV4              0x0800

#define NDIS_ETH_TYPE_ARP               0x0806

#define NDIS_ETH_TYPE_IPV6              0x86dd

#define NDIS_ETH_TYPE_SLOW_PROTOCOL     0x8809

#ifdef __cplusplus
}
#endif

#if ((NTDDI_VERSION >= NTDDI_VISTA) || NDIS_SUPPORT_NDIS6)
#ifndef __WINDOT11_H__
#include <windot11.h>
#endif
#endif

#ifndef NDIS_802_11_AI_REQFI_CAPABILITIES
#define NDIS_802_11_AI_REQFI_CAPABILITIES 1
#endif
#ifndef NDIS_802_11_AI_REQFI_CURRENTAPADDRESS
#define NDIS_802_11_AI_REQFI_CURRENTAPADDRESS 4
#endif
#ifndef NDIS_802_11_AI_REQFI_LISTENINTERVAL
#define NDIS_802_11_AI_REQFI_LISTENINTERVAL 2
#endif
#ifndef NDIS_802_11_AI_RESFI_ASSOCIATIONID
#define NDIS_802_11_AI_RESFI_ASSOCIATIONID 4
#endif
#ifndef NDIS_802_11_AI_RESFI_CAPABILITIES
#define NDIS_802_11_AI_RESFI_CAPABILITIES 1
#endif
#ifndef NDIS_802_11_AI_RESFI_STATUSCODE
#define NDIS_802_11_AI_RESFI_STATUSCODE 2
#endif
#ifndef NDIS_802_11_AUTH_REQUEST_AUTH_FIELDS
#define NDIS_802_11_AUTH_REQUEST_AUTH_FIELDS 0x0f
#endif
#ifndef NDIS_802_11_AUTH_REQUEST_GROUP_ERROR
#define NDIS_802_11_AUTH_REQUEST_GROUP_ERROR 0x0E
#endif
#ifndef NDIS_802_11_AUTH_REQUEST_KEYUPDATE
#define NDIS_802_11_AUTH_REQUEST_KEYUPDATE 0x02
#endif
#ifndef NDIS_802_11_AUTH_REQUEST_PAIRWISE_ERROR
#define NDIS_802_11_AUTH_REQUEST_PAIRWISE_ERROR 0x06
#endif
#ifndef NDIS_802_11_AUTH_REQUEST_REAUTH
#define NDIS_802_11_AUTH_REQUEST_REAUTH 0x01
#endif
#ifndef NDIS_802_11_LENGTH_RATES_EX
#define NDIS_802_11_LENGTH_RATES_EX 16
#endif
#ifndef NDIS_802_11_PMKID_CANDIDATE_PREAUTH_ENABLED
#define NDIS_802_11_PMKID_CANDIDATE_PREAUTH_ENABLED 0x01
#endif
#ifndef NDIS_CO_MAC_OPTION_DYNAMIC_LINK_SPEED
#define NDIS_CO_MAC_OPTION_DYNAMIC_LINK_SPEED 0x00000001
#endif
#ifndef NDIS_HARDWARE_CROSSTIMESTAMP_REVISION_1
#define NDIS_HARDWARE_CROSSTIMESTAMP_REVISION_1 1
#endif
#ifndef NDIS_MAXIMUM_PORTS
#define NDIS_MAXIMUM_PORTS 0x1000000
#endif
#ifndef NDIS_MEDIA_CAP_RECEIVE
#define NDIS_MEDIA_CAP_RECEIVE 0x00000002
#endif
#ifndef NDIS_MEDIA_CAP_TRANSMIT
#define NDIS_MEDIA_CAP_TRANSMIT 0x00000001
#endif
#ifndef NDIS_OBJECT_TYPE_CLIENT_CHIMNEY_OFFLOAD_CHARACTERISTICS
#define NDIS_OBJECT_TYPE_CLIENT_CHIMNEY_OFFLOAD_CHARACTERISTICS 0x93
#endif
#ifndef NDIS_OBJECT_TYPE_CLIENT_CHIMNEY_OFFLOAD_GENERIC_CHARACTERISTICS
#define NDIS_OBJECT_TYPE_CLIENT_CHIMNEY_OFFLOAD_GENERIC_CHARACTERISTICS 0x8E
#endif
#ifndef NDIS_OBJECT_TYPE_CO_CALL_MANAGER_OPTIONAL_HANDLERS
#define NDIS_OBJECT_TYPE_CO_CALL_MANAGER_OPTIONAL_HANDLERS 0xA5
#endif
#ifndef NDIS_OBJECT_TYPE_CO_CLIENT_OPTIONAL_HANDLERS
#define NDIS_OBJECT_TYPE_CO_CLIENT_OPTIONAL_HANDLERS 0xA6
#endif
#ifndef NDIS_OBJECT_TYPE_CO_MINIPORT_CHARACTERISTICS
#define NDIS_OBJECT_TYPE_CO_MINIPORT_CHARACTERISTICS 0x91
#endif
#ifndef NDIS_OBJECT_TYPE_CO_PROTOCOL_CHARACTERISTICS
#define NDIS_OBJECT_TYPE_CO_PROTOCOL_CHARACTERISTICS 0x90
#endif
#ifndef NDIS_OBJECT_TYPE_NSI_COMPARTMENT_RW_STRUCT
#define NDIS_OBJECT_TYPE_NSI_COMPARTMENT_RW_STRUCT 0xAD
#endif
#ifndef NDIS_OBJECT_TYPE_NSI_INTERFACE_PERSIST_RW_STRUCT
#define NDIS_OBJECT_TYPE_NSI_INTERFACE_PERSIST_RW_STRUCT 0xAE
#endif
#ifndef NDIS_OBJECT_TYPE_NSI_NETWORK_RW_STRUCT
#define NDIS_OBJECT_TYPE_NSI_NETWORK_RW_STRUCT 0xAC
#endif
#ifndef NDIS_OBJECT_TYPE_PCI_DEVICE_CUSTOM_PROPERTIES_REVISION_1
#define NDIS_OBJECT_TYPE_PCI_DEVICE_CUSTOM_PROPERTIES_REVISION_1 1
#endif
#ifndef NDIS_OBJECT_TYPE_PCI_DEVICE_CUSTOM_PROPERTIES_REVISION_2
#define NDIS_OBJECT_TYPE_PCI_DEVICE_CUSTOM_PROPERTIES_REVISION_2 2
#endif
#ifndef NDIS_OBJECT_TYPE_PROVIDER_CHIMNEY_OFFLOAD_CHARACTERISTICS
#define NDIS_OBJECT_TYPE_PROVIDER_CHIMNEY_OFFLOAD_CHARACTERISTICS 0x94
#endif
#ifndef NDIS_OBJECT_TYPE_PROVIDER_CHIMNEY_OFFLOAD_GENERIC_CHARACTERISTICS
#define NDIS_OBJECT_TYPE_PROVIDER_CHIMNEY_OFFLOAD_GENERIC_CHARACTERISTICS 0x8F
#endif
#ifndef NDIS_OBJECT_TYPE_RSS_PARAMETERS_V2
#define NDIS_OBJECT_TYPE_RSS_PARAMETERS_V2 0xC8
#endif
#ifndef NDIS_OBJECT_TYPE_RSS_SET_INDIRECTION_ENTRIES
#define NDIS_OBJECT_TYPE_RSS_SET_INDIRECTION_ENTRIES 0xC9
#endif
#ifndef NDIS_PORT_ARRAY_REVISION_1
#define NDIS_PORT_ARRAY_REVISION_1 1
#endif
#ifndef NDIS_PORT_STATE_REVISION_1
#define NDIS_PORT_STATE_REVISION_1 1
#endif
#ifndef NDIS_PROTOCOL_ID_IP6
#define NDIS_PROTOCOL_ID_IP6 0x03
#endif
#ifndef NDIS_PROT_OPTION_SEND_RESTRICTED
#define NDIS_PROT_OPTION_SEND_RESTRICTED 0x00000008
#endif
#ifndef NDIS_RECEIVE_HASH_FLAG_ENABLE_HASH
#define NDIS_RECEIVE_HASH_FLAG_ENABLE_HASH 0x00000001
#endif
#ifndef NDIS_RECEIVE_HASH_FLAG_HASH_INFO_UNCHANGED
#define NDIS_RECEIVE_HASH_FLAG_HASH_INFO_UNCHANGED 0x00000002
#endif
#ifndef NDIS_RECEIVE_HASH_FLAG_HASH_KEY_UNCHANGED
#define NDIS_RECEIVE_HASH_FLAG_HASH_KEY_UNCHANGED 0x00000004
#endif
#ifndef NDIS_RECEIVE_HASH_PARAMETERS_REVISION_1
#define NDIS_RECEIVE_HASH_PARAMETERS_REVISION_1 1
#endif
#ifndef NDIS_RING_AUTO_REMOVAL_ERROR
#define NDIS_RING_AUTO_REMOVAL_ERROR 0x00000400
#endif
#ifndef NDIS_RING_COUNTER_OVERFLOW
#define NDIS_RING_COUNTER_OVERFLOW 0x00000100
#endif
#ifndef NDIS_RING_HARD_ERROR
#define NDIS_RING_HARD_ERROR 0x00004000
#endif
#ifndef NDIS_RING_LOBE_WIRE_FAULT
#define NDIS_RING_LOBE_WIRE_FAULT 0x00000800
#endif
#ifndef NDIS_RING_REMOVE_RECEIVED
#define NDIS_RING_REMOVE_RECEIVED 0x00000200
#endif
#ifndef NDIS_RING_RING_RECOVERY
#define NDIS_RING_RING_RECOVERY 0x00000040
#endif
#ifndef NDIS_RING_SIGNAL_LOSS
#define NDIS_RING_SIGNAL_LOSS 0x00008000
#endif
#ifndef NDIS_RING_SINGLE_STATION
#define NDIS_RING_SINGLE_STATION 0x00000080
#endif
#ifndef NDIS_RING_SOFT_ERROR
#define NDIS_RING_SOFT_ERROR 0x00002000
#endif
#ifndef NDIS_RING_TRANSMIT_BEACON
#define NDIS_RING_TRANSMIT_BEACON 0x00001000
#endif
#ifndef NDIS_RSS_HASH_SECRET_KEY_SIZE_REVISION_1
#define NDIS_RSS_HASH_SECRET_KEY_SIZE_REVISION_1 40
#endif
#ifndef NDIS_RSS_INDIRECTION_TABLE_SIZE_REVISION_1
#define NDIS_RSS_INDIRECTION_TABLE_SIZE_REVISION_1 128
#endif
#ifndef NDIS_TIMEOUT_DPC_REQUEST_CAPABILITIES_REVISION_1
#define NDIS_TIMEOUT_DPC_REQUEST_CAPABILITIES_REVISION_1 1
#endif
#ifndef NDIS_TIMESTAMP_CAPABILITIES_REVISION_1
#define NDIS_TIMESTAMP_CAPABILITIES_REVISION_1 1
#endif
#ifndef OFFLOAD_INBOUND_SA
#define OFFLOAD_INBOUND_SA 0x0001
#endif
#ifndef OFFLOAD_MAX_SAS
#define OFFLOAD_MAX_SAS 3
#endif
#ifndef OFFLOAD_OUTBOUND_SA
#define OFFLOAD_OUTBOUND_SA 0x0002
#endif
#ifndef OID_1394_LOCAL_NODE_INFO
#define OID_1394_LOCAL_NODE_INFO 0x0C010101
#endif
#ifndef OID_1394_VC_INFO
#define OID_1394_VC_INFO 0x0C010102
#endif
#ifndef OID_802_5_ABORT_DELIMETERS
#define OID_802_5_ABORT_DELIMETERS 0x02020203
#endif
#ifndef OID_802_5_AC_ERRORS
#define OID_802_5_AC_ERRORS 0x02020202
#endif
#ifndef OID_802_5_BURST_ERRORS
#define OID_802_5_BURST_ERRORS 0x02020201
#endif
#ifndef OID_802_5_CURRENT_ADDRESS
#define OID_802_5_CURRENT_ADDRESS 0x02010102
#endif
#ifndef OID_802_5_CURRENT_FUNCTIONAL
#define OID_802_5_CURRENT_FUNCTIONAL 0x02010103
#endif
#ifndef OID_802_5_CURRENT_GROUP
#define OID_802_5_CURRENT_GROUP 0x02010104
#endif
#ifndef OID_802_5_CURRENT_RING_STATE
#define OID_802_5_CURRENT_RING_STATE 0x02010107
#endif
#ifndef OID_802_5_CURRENT_RING_STATUS
#define OID_802_5_CURRENT_RING_STATUS 0x02010106
#endif
#ifndef OID_802_5_FRAME_COPIED_ERRORS
#define OID_802_5_FRAME_COPIED_ERRORS 0x02020204
#endif
#ifndef OID_802_5_FREQUENCY_ERRORS
#define OID_802_5_FREQUENCY_ERRORS 0x02020205
#endif
#ifndef OID_802_5_INTERNAL_ERRORS
#define OID_802_5_INTERNAL_ERRORS 0x02020207
#endif
#ifndef OID_802_5_LAST_OPEN_STATUS
#define OID_802_5_LAST_OPEN_STATUS 0x02010105
#endif
#ifndef OID_802_5_LINE_ERRORS
#define OID_802_5_LINE_ERRORS 0x02020101
#endif
#ifndef OID_802_5_LOST_FRAMES
#define OID_802_5_LOST_FRAMES 0x02020102
#endif
#ifndef OID_802_5_PERMANENT_ADDRESS
#define OID_802_5_PERMANENT_ADDRESS 0x02010101
#endif
#ifndef OID_802_5_TOKEN_ERRORS
#define OID_802_5_TOKEN_ERRORS 0x02020206
#endif
#ifndef OID_ARCNET_CURRENT_ADDRESS
#define OID_ARCNET_CURRENT_ADDRESS 0x06010102
#endif
#ifndef OID_ARCNET_PERMANENT_ADDRESS
#define OID_ARCNET_PERMANENT_ADDRESS 0x06010101
#endif
#ifndef OID_ARCNET_RECONFIGURATIONS
#define OID_ARCNET_RECONFIGURATIONS 0x06020201
#endif
#ifndef OID_ATM_ACQUIRE_ACCESS_NET_RESOURCES
#define OID_ATM_ACQUIRE_ACCESS_NET_RESOURCES 0x08010203
#endif
#ifndef OID_ATM_ALIGNMENT_REQUIRED
#define OID_ATM_ALIGNMENT_REQUIRED 0x08010208
#endif
#ifndef OID_ATM_ASSIGNED_VPI
#define OID_ATM_ASSIGNED_VPI 0x08010202
#endif
#ifndef OID_ATM_CALL_ALERTING
#define OID_ATM_CALL_ALERTING 0x0801020C
#endif
#ifndef OID_ATM_CALL_NOTIFY
#define OID_ATM_CALL_NOTIFY 0x0801020E
#endif
#ifndef OID_ATM_CALL_PROCEEDING
#define OID_ATM_CALL_PROCEEDING 0x0801020B
#endif
#ifndef OID_ATM_CELLS_HEC_ERROR
#define OID_ATM_CELLS_HEC_ERROR 0x08020202
#endif
#ifndef OID_ATM_DIGITAL_BROADCAST_VPIVCI
#define OID_ATM_DIGITAL_BROADCAST_VPIVCI 0x08010206
#endif
#ifndef OID_ATM_GET_NEAREST_FLOW
#define OID_ATM_GET_NEAREST_FLOW 0x08010207
#endif
#ifndef OID_ATM_HW_CURRENT_ADDRESS
#define OID_ATM_HW_CURRENT_ADDRESS 0x08010104
#endif
#ifndef OID_ATM_ILMI_VPIVCI
#define OID_ATM_ILMI_VPIVCI 0x08010205
#endif
#ifndef OID_ATM_LECS_ADDRESS
#define OID_ATM_LECS_ADDRESS 0x08010209
#endif
#ifndef OID_ATM_MAX_AAL0_PACKET_SIZE
#define OID_ATM_MAX_AAL0_PACKET_SIZE 0x08010108
#endif
#ifndef OID_ATM_MAX_AAL1_PACKET_SIZE
#define OID_ATM_MAX_AAL1_PACKET_SIZE 0x08010109
#endif
#ifndef OID_ATM_MAX_AAL34_PACKET_SIZE
#define OID_ATM_MAX_AAL34_PACKET_SIZE 0x0801010A
#endif
#ifndef OID_ATM_MAX_AAL5_PACKET_SIZE
#define OID_ATM_MAX_AAL5_PACKET_SIZE 0x0801010B
#endif
#ifndef OID_ATM_MAX_ACTIVE_VCI_BITS
#define OID_ATM_MAX_ACTIVE_VCI_BITS 0x08010106
#endif
#ifndef OID_ATM_MAX_ACTIVE_VCS
#define OID_ATM_MAX_ACTIVE_VCS 0x08010105
#endif
#ifndef OID_ATM_MAX_ACTIVE_VPI_BITS
#define OID_ATM_MAX_ACTIVE_VPI_BITS 0x08010107
#endif
#ifndef OID_ATM_MY_IP_NM_ADDRESS
#define OID_ATM_MY_IP_NM_ADDRESS 0x0801020F
#endif
#ifndef OID_ATM_PARTY_ALERTING
#define OID_ATM_PARTY_ALERTING 0x0801020D
#endif
#ifndef OID_ATM_RCV_CELLS_DROPPED
#define OID_ATM_RCV_CELLS_DROPPED 0x08020103
#endif
#ifndef OID_ATM_RCV_CELLS_OK
#define OID_ATM_RCV_CELLS_OK 0x08020101
#endif
#ifndef OID_ATM_RCV_INVALID_VPI_VCI
#define OID_ATM_RCV_INVALID_VPI_VCI 0x08020201
#endif
#ifndef OID_ATM_RCV_REASSEMBLY_ERROR
#define OID_ATM_RCV_REASSEMBLY_ERROR 0x08020203
#endif
#ifndef OID_ATM_RELEASE_ACCESS_NET_RESOURCES
#define OID_ATM_RELEASE_ACCESS_NET_RESOURCES 0x08010204
#endif
#ifndef OID_ATM_SERVICE_ADDRESS
#define OID_ATM_SERVICE_ADDRESS 0x0801020A
#endif
#ifndef OID_ATM_SIGNALING_VPIVCI
#define OID_ATM_SIGNALING_VPIVCI 0x08010201
#endif
#ifndef OID_ATM_SUPPORTED_AAL_TYPES
#define OID_ATM_SUPPORTED_AAL_TYPES 0x08010103
#endif
#ifndef OID_ATM_SUPPORTED_SERVICE_CATEGORY
#define OID_ATM_SUPPORTED_SERVICE_CATEGORY 0x08010102
#endif
#ifndef OID_ATM_SUPPORTED_VC_RATES
#define OID_ATM_SUPPORTED_VC_RATES 0x08010101
#endif
#ifndef OID_ATM_XMIT_CELLS_OK
#define OID_ATM_XMIT_CELLS_OK 0x08020102
#endif
#ifndef OID_CO_ADDRESS_CHANGE
#define OID_CO_ADDRESS_CHANGE 0xFE000007
#endif
#ifndef OID_CO_ADD_ADDRESS
#define OID_CO_ADD_ADDRESS 0xFE000004
#endif
#ifndef OID_CO_ADD_PVC
#define OID_CO_ADD_PVC 0xFE000001
#endif
#ifndef OID_CO_AF_CLOSE
#define OID_CO_AF_CLOSE 0xFE00000A
#endif
#ifndef OID_CO_DELETE_ADDRESS
#define OID_CO_DELETE_ADDRESS 0xFE000005
#endif
#ifndef OID_CO_DELETE_PVC
#define OID_CO_DELETE_PVC 0xFE000002
#endif
#ifndef OID_CO_GET_ADDRESSES
#define OID_CO_GET_ADDRESSES 0xFE000006
#endif
#ifndef OID_CO_GET_CALL_INFORMATION
#define OID_CO_GET_CALL_INFORMATION 0xFE000003
#endif
#ifndef OID_CO_SIGNALING_DISABLED
#define OID_CO_SIGNALING_DISABLED 0xFE000009
#endif
#ifndef OID_CO_SIGNALING_ENABLED
#define OID_CO_SIGNALING_ENABLED 0xFE000008
#endif
#ifndef OID_CO_TAPI_ADDRESS_CAPS
#define OID_CO_TAPI_ADDRESS_CAPS 0xFE001003
#endif
#ifndef OID_CO_TAPI_CM_CAPS
#define OID_CO_TAPI_CM_CAPS 0xFE001001
#endif
#ifndef OID_CO_TAPI_DONT_REPORT_DIGITS
#define OID_CO_TAPI_DONT_REPORT_DIGITS 0xFE001009
#endif
#ifndef OID_CO_TAPI_GET_CALL_DIAGNOSTICS
#define OID_CO_TAPI_GET_CALL_DIAGNOSTICS 0xFE001007
#endif
#ifndef OID_CO_TAPI_LINE_CAPS
#define OID_CO_TAPI_LINE_CAPS 0xFE001002
#endif
#ifndef OID_CO_TAPI_REPORT_DIGITS
#define OID_CO_TAPI_REPORT_DIGITS 0xFE001008
#endif
#ifndef OID_CO_TAPI_TRANSLATE_NDIS_CALLPARAMS
#define OID_CO_TAPI_TRANSLATE_NDIS_CALLPARAMS 0xFE001005
#endif
#ifndef OID_CO_TAPI_TRANSLATE_TAPI_CALLPARAMS
#define OID_CO_TAPI_TRANSLATE_TAPI_CALLPARAMS 0xFE001004
#endif
#ifndef OID_CO_TAPI_TRANSLATE_TAPI_SAP
#define OID_CO_TAPI_TRANSLATE_TAPI_SAP 0xFE001006
#endif
#ifndef OID_FDDI_ATTACHMENT_TYPE
#define OID_FDDI_ATTACHMENT_TYPE 0x03020101
#endif
#ifndef OID_FDDI_DOWNSTREAM_NODE_LONG
#define OID_FDDI_DOWNSTREAM_NODE_LONG 0x03020103
#endif
#ifndef OID_FDDI_FRAMES_LOST
#define OID_FDDI_FRAMES_LOST 0x03020105
#endif
#ifndef OID_FDDI_FRAME_ERRORS
#define OID_FDDI_FRAME_ERRORS 0x03020104
#endif
#ifndef OID_FDDI_IF_ADMIN_STATUS
#define OID_FDDI_IF_ADMIN_STATUS 0x0303027E
#endif
#ifndef OID_FDDI_IF_DESCR
#define OID_FDDI_IF_DESCR 0x03030279
#endif
#ifndef OID_FDDI_IF_IN_DISCARDS
#define OID_FDDI_IF_IN_DISCARDS 0x03030284
#endif
#ifndef OID_FDDI_IF_IN_ERRORS
#define OID_FDDI_IF_IN_ERRORS 0x03030285
#endif
#ifndef OID_FDDI_IF_IN_NUCAST_PKTS
#define OID_FDDI_IF_IN_NUCAST_PKTS 0x03030283
#endif
#ifndef OID_FDDI_IF_IN_OCTETS
#define OID_FDDI_IF_IN_OCTETS 0x03030281
#endif
#ifndef OID_FDDI_IF_IN_UCAST_PKTS
#define OID_FDDI_IF_IN_UCAST_PKTS 0x03030282
#endif
#ifndef OID_FDDI_IF_IN_UNKNOWN_PROTOS
#define OID_FDDI_IF_IN_UNKNOWN_PROTOS 0x03030286
#endif
#ifndef OID_FDDI_IF_LAST_CHANGE
#define OID_FDDI_IF_LAST_CHANGE 0x03030280
#endif
#ifndef OID_FDDI_IF_MTU
#define OID_FDDI_IF_MTU 0x0303027B
#endif
#ifndef OID_FDDI_IF_OPER_STATUS
#define OID_FDDI_IF_OPER_STATUS 0x0303027F
#endif
#ifndef OID_FDDI_IF_OUT_DISCARDS
#define OID_FDDI_IF_OUT_DISCARDS 0x0303028A
#endif
#ifndef OID_FDDI_IF_OUT_ERRORS
#define OID_FDDI_IF_OUT_ERRORS 0x0303028B
#endif
#ifndef OID_FDDI_IF_OUT_NUCAST_PKTS
#define OID_FDDI_IF_OUT_NUCAST_PKTS 0x03030289
#endif
#ifndef OID_FDDI_IF_OUT_OCTETS
#define OID_FDDI_IF_OUT_OCTETS 0x03030287
#endif
#ifndef OID_FDDI_IF_OUT_QLEN
#define OID_FDDI_IF_OUT_QLEN 0x0303028C
#endif
#ifndef OID_FDDI_IF_OUT_UCAST_PKTS
#define OID_FDDI_IF_OUT_UCAST_PKTS 0x03030288
#endif
#ifndef OID_FDDI_IF_PHYS_ADDRESS
#define OID_FDDI_IF_PHYS_ADDRESS 0x0303027D
#endif
#ifndef OID_FDDI_IF_SPECIFIC
#define OID_FDDI_IF_SPECIFIC 0x0303028D
#endif
#ifndef OID_FDDI_IF_SPEED
#define OID_FDDI_IF_SPEED 0x0303027C
#endif
#ifndef OID_FDDI_IF_TYPE
#define OID_FDDI_IF_TYPE 0x0303027A
#endif
#ifndef OID_FDDI_LCONNECTION_STATE
#define OID_FDDI_LCONNECTION_STATE 0x03020109
#endif
#ifndef OID_FDDI_LCT_FAILURES
#define OID_FDDI_LCT_FAILURES 0x03020107
#endif
#ifndef OID_FDDI_LEM_REJECTS
#define OID_FDDI_LEM_REJECTS 0x03020108
#endif
#ifndef OID_FDDI_LONG_CURRENT_ADDR
#define OID_FDDI_LONG_CURRENT_ADDR 0x03010102
#endif
#ifndef OID_FDDI_LONG_MAX_LIST_SIZE
#define OID_FDDI_LONG_MAX_LIST_SIZE 0x03010104
#endif
#ifndef OID_FDDI_LONG_MULTICAST_LIST
#define OID_FDDI_LONG_MULTICAST_LIST 0x03010103
#endif
#ifndef OID_FDDI_LONG_PERMANENT_ADDR
#define OID_FDDI_LONG_PERMANENT_ADDR 0x03010101
#endif
#ifndef OID_FDDI_MAC_AVAILABLE_PATHS
#define OID_FDDI_MAC_AVAILABLE_PATHS 0x03030223
#endif
#ifndef OID_FDDI_MAC_BRIDGE_FUNCTIONS
#define OID_FDDI_MAC_BRIDGE_FUNCTIONS 0x03030220
#endif
#ifndef OID_FDDI_MAC_COPIED_CT
#define OID_FDDI_MAC_COPIED_CT 0x0303023C
#endif
#ifndef OID_FDDI_MAC_CURRENT_PATH
#define OID_FDDI_MAC_CURRENT_PATH 0x03030224
#endif
#ifndef OID_FDDI_MAC_DA_FLAG
#define OID_FDDI_MAC_DA_FLAG 0x0303024A
#endif
#ifndef OID_FDDI_MAC_DOWNSTREAM_NBR
#define OID_FDDI_MAC_DOWNSTREAM_NBR 0x03030226
#endif
#ifndef OID_FDDI_MAC_DOWNSTREAM_PORT_TYPE
#define OID_FDDI_MAC_DOWNSTREAM_PORT_TYPE 0x0303022B
#endif
#ifndef OID_FDDI_MAC_DUP_ADDRESS_TEST
#define OID_FDDI_MAC_DUP_ADDRESS_TEST 0x03030229
#endif
#ifndef OID_FDDI_MAC_ERROR_CT
#define OID_FDDI_MAC_ERROR_CT 0x0303023F
#endif
#ifndef OID_FDDI_MAC_FRAME_CT
#define OID_FDDI_MAC_FRAME_CT 0x0303023B
#endif
#ifndef OID_FDDI_MAC_FRAME_ERROR_FLAG
#define OID_FDDI_MAC_FRAME_ERROR_FLAG 0x0303024C
#endif
#ifndef OID_FDDI_MAC_FRAME_ERROR_RATIO
#define OID_FDDI_MAC_FRAME_ERROR_RATIO 0x03030246
#endif
#ifndef OID_FDDI_MAC_FRAME_ERROR_THRESHOLD
#define OID_FDDI_MAC_FRAME_ERROR_THRESHOLD 0x03030245
#endif
#ifndef OID_FDDI_MAC_FRAME_STATUS_FUNCTIONS
#define OID_FDDI_MAC_FRAME_STATUS_FUNCTIONS 0x0303021F
#endif
#ifndef OID_FDDI_MAC_HARDWARE_PRESENT
#define OID_FDDI_MAC_HARDWARE_PRESENT 0x0303024F
#endif
#ifndef OID_FDDI_MAC_INDEX
#define OID_FDDI_MAC_INDEX 0x0303022C
#endif
#ifndef OID_FDDI_MAC_LATE_CT
#define OID_FDDI_MAC_LATE_CT 0x03030243
#endif
#ifndef OID_FDDI_MAC_LONG_GRP_ADDRESS
#define OID_FDDI_MAC_LONG_GRP_ADDRESS 0x0303022E
#endif
#ifndef OID_FDDI_MAC_LOST_CT
#define OID_FDDI_MAC_LOST_CT 0x03030240
#endif
#ifndef OID_FDDI_MAC_MA_UNITDATA_AVAILABLE
#define OID_FDDI_MAC_MA_UNITDATA_AVAILABLE 0x0303024E
#endif
#ifndef OID_FDDI_MAC_MA_UNITDATA_ENABLE
#define OID_FDDI_MAC_MA_UNITDATA_ENABLE 0x03030250
#endif
#ifndef OID_FDDI_MAC_NOT_COPIED_CT
#define OID_FDDI_MAC_NOT_COPIED_CT 0x03030242
#endif
#ifndef OID_FDDI_MAC_NOT_COPIED_FLAG
#define OID_FDDI_MAC_NOT_COPIED_FLAG 0x0303024D
#endif
#ifndef OID_FDDI_MAC_NOT_COPIED_RATIO
#define OID_FDDI_MAC_NOT_COPIED_RATIO 0x03030248
#endif
#ifndef OID_FDDI_MAC_NOT_COPIED_THRESHOLD
#define OID_FDDI_MAC_NOT_COPIED_THRESHOLD 0x03030247
#endif
#ifndef OID_FDDI_MAC_OLD_DOWNSTREAM_NBR
#define OID_FDDI_MAC_OLD_DOWNSTREAM_NBR 0x03030228
#endif
#ifndef OID_FDDI_MAC_OLD_UPSTREAM_NBR
#define OID_FDDI_MAC_OLD_UPSTREAM_NBR 0x03030227
#endif
#ifndef OID_FDDI_MAC_REQUESTED_PATHS
#define OID_FDDI_MAC_REQUESTED_PATHS 0x0303022A
#endif
#ifndef OID_FDDI_MAC_RING_OP_CT
#define OID_FDDI_MAC_RING_OP_CT 0x03030244
#endif
#ifndef OID_FDDI_MAC_RMT_STATE
#define OID_FDDI_MAC_RMT_STATE 0x03030249
#endif
#ifndef OID_FDDI_MAC_SHORT_GRP_ADDRESS
#define OID_FDDI_MAC_SHORT_GRP_ADDRESS 0x0303022F
#endif
#ifndef OID_FDDI_MAC_SMT_ADDRESS
#define OID_FDDI_MAC_SMT_ADDRESS 0x0303022D
#endif
#ifndef OID_FDDI_MAC_TOKEN_CT
#define OID_FDDI_MAC_TOKEN_CT 0x0303023E
#endif
#ifndef OID_FDDI_MAC_TRANSMIT_CT
#define OID_FDDI_MAC_TRANSMIT_CT 0x0303023D
#endif
#ifndef OID_FDDI_MAC_TVX_CAPABILITY
#define OID_FDDI_MAC_TVX_CAPABILITY 0x03030222
#endif
#ifndef OID_FDDI_MAC_TVX_EXPIRED_CT
#define OID_FDDI_MAC_TVX_EXPIRED_CT 0x03030241
#endif
#ifndef OID_FDDI_MAC_TVX_VALUE
#define OID_FDDI_MAC_TVX_VALUE 0x03030233
#endif
#ifndef OID_FDDI_MAC_T_MAX
#define OID_FDDI_MAC_T_MAX 0x03030232
#endif
#ifndef OID_FDDI_MAC_T_MAX_CAPABILITY
#define OID_FDDI_MAC_T_MAX_CAPABILITY 0x03030221
#endif
#ifndef OID_FDDI_MAC_T_NEG
#define OID_FDDI_MAC_T_NEG 0x03030231
#endif
#ifndef OID_FDDI_MAC_T_PRI0
#define OID_FDDI_MAC_T_PRI0 0x03030234
#endif
#ifndef OID_FDDI_MAC_T_PRI1
#define OID_FDDI_MAC_T_PRI1 0x03030235
#endif
#ifndef OID_FDDI_MAC_T_PRI2
#define OID_FDDI_MAC_T_PRI2 0x03030236
#endif
#ifndef OID_FDDI_MAC_T_PRI3
#define OID_FDDI_MAC_T_PRI3 0x03030237
#endif
#ifndef OID_FDDI_MAC_T_PRI4
#define OID_FDDI_MAC_T_PRI4 0x03030238
#endif
#ifndef OID_FDDI_MAC_T_PRI5
#define OID_FDDI_MAC_T_PRI5 0x03030239
#endif
#ifndef OID_FDDI_MAC_T_PRI6
#define OID_FDDI_MAC_T_PRI6 0x0303023A
#endif
#ifndef OID_FDDI_MAC_T_REQ
#define OID_FDDI_MAC_T_REQ 0x03030230
#endif
#ifndef OID_FDDI_MAC_UNDA_FLAG
#define OID_FDDI_MAC_UNDA_FLAG 0x0303024B
#endif
#ifndef OID_FDDI_MAC_UPSTREAM_NBR
#define OID_FDDI_MAC_UPSTREAM_NBR 0x03030225
#endif
#ifndef OID_FDDI_PATH_CONFIGURATION
#define OID_FDDI_PATH_CONFIGURATION 0x03030256
#endif
#ifndef OID_FDDI_PATH_INDEX
#define OID_FDDI_PATH_INDEX 0x03030251
#endif
#ifndef OID_FDDI_PATH_MAX_T_REQ
#define OID_FDDI_PATH_MAX_T_REQ 0x0303025B
#endif
#ifndef OID_FDDI_PATH_RING_LATENCY
#define OID_FDDI_PATH_RING_LATENCY 0x03030252
#endif
#ifndef OID_FDDI_PATH_SBA_AVAILABLE
#define OID_FDDI_PATH_SBA_AVAILABLE 0x03030258
#endif
#ifndef OID_FDDI_PATH_SBA_OVERHEAD
#define OID_FDDI_PATH_SBA_OVERHEAD 0x03030255
#endif
#ifndef OID_FDDI_PATH_SBA_PAYLOAD
#define OID_FDDI_PATH_SBA_PAYLOAD 0x03030254
#endif
#ifndef OID_FDDI_PATH_TRACE_STATUS
#define OID_FDDI_PATH_TRACE_STATUS 0x03030253
#endif
#ifndef OID_FDDI_PATH_TVX_LOWER_BOUND
#define OID_FDDI_PATH_TVX_LOWER_BOUND 0x03030259
#endif
#ifndef OID_FDDI_PATH_T_MAX_LOWER_BOUND
#define OID_FDDI_PATH_T_MAX_LOWER_BOUND 0x0303025A
#endif
#ifndef OID_FDDI_PATH_T_R_MODE
#define OID_FDDI_PATH_T_R_MODE 0x03030257
#endif
#ifndef OID_FDDI_PORT_ACTION
#define OID_FDDI_PORT_ACTION 0x03030278
#endif
#ifndef OID_FDDI_PORT_AVAILABLE_PATHS
#define OID_FDDI_PORT_AVAILABLE_PATHS 0x03030263
#endif
#ifndef OID_FDDI_PORT_BS_FLAG
#define OID_FDDI_PORT_BS_FLAG 0x03030269
#endif
#ifndef OID_FDDI_PORT_CONNECTION_CAPABILITIES
#define OID_FDDI_PORT_CONNECTION_CAPABILITIES 0x03030266
#endif
#ifndef OID_FDDI_PORT_CONNECTION_POLICIES
#define OID_FDDI_PORT_CONNECTION_POLICIES 0x0303025E
#endif
#ifndef OID_FDDI_PORT_CONNNECT_STATE
#define OID_FDDI_PORT_CONNNECT_STATE 0x03030272
#endif
#ifndef OID_FDDI_PORT_CURRENT_PATH
#define OID_FDDI_PORT_CURRENT_PATH 0x03030260
#endif
#ifndef OID_FDDI_PORT_EB_ERROR_CT
#define OID_FDDI_PORT_EB_ERROR_CT 0x0303026B
#endif
#ifndef OID_FDDI_PORT_HARDWARE_PRESENT
#define OID_FDDI_PORT_HARDWARE_PRESENT 0x03030276
#endif
#ifndef OID_FDDI_PORT_INDEX
#define OID_FDDI_PORT_INDEX 0x03030267
#endif
#ifndef OID_FDDI_PORT_LCT_FAIL_CT
#define OID_FDDI_PORT_LCT_FAIL_CT 0x0303026C
#endif
#ifndef OID_FDDI_PORT_LEM_CT
#define OID_FDDI_PORT_LEM_CT 0x0303026F
#endif
#ifndef OID_FDDI_PORT_LEM_REJECT_CT
#define OID_FDDI_PORT_LEM_REJECT_CT 0x0303026E
#endif
#ifndef OID_FDDI_PORT_LER_ALARM
#define OID_FDDI_PORT_LER_ALARM 0x03030271
#endif
#ifndef OID_FDDI_PORT_LER_CUTOFF
#define OID_FDDI_PORT_LER_CUTOFF 0x03030270
#endif
#ifndef OID_FDDI_PORT_LER_ESTIMATE
#define OID_FDDI_PORT_LER_ESTIMATE 0x0303026D
#endif
#ifndef OID_FDDI_PORT_LER_FLAG
#define OID_FDDI_PORT_LER_FLAG 0x03030275
#endif
#ifndef OID_FDDI_PORT_MAC_INDICATED
#define OID_FDDI_PORT_MAC_INDICATED 0x0303025F
#endif
#ifndef OID_FDDI_PORT_MAC_LOOP_TIME
#define OID_FDDI_PORT_MAC_LOOP_TIME 0x03030264
#endif
#ifndef OID_FDDI_PORT_MAC_PLACEMENT
#define OID_FDDI_PORT_MAC_PLACEMENT 0x03030262
#endif
#ifndef OID_FDDI_PORT_MAINT_LS
#define OID_FDDI_PORT_MAINT_LS 0x03030268
#endif
#ifndef OID_FDDI_PORT_MY_TYPE
#define OID_FDDI_PORT_MY_TYPE 0x0303025C
#endif
#ifndef OID_FDDI_PORT_NEIGHBOR_TYPE
#define OID_FDDI_PORT_NEIGHBOR_TYPE 0x0303025D
#endif
#ifndef OID_FDDI_PORT_PCM_STATE
#define OID_FDDI_PORT_PCM_STATE 0x03030273
#endif
#ifndef OID_FDDI_PORT_PC_LS
#define OID_FDDI_PORT_PC_LS 0x0303026A
#endif
#ifndef OID_FDDI_PORT_PC_WITHHOLD
#define OID_FDDI_PORT_PC_WITHHOLD 0x03030274
#endif
#ifndef OID_FDDI_PORT_PMD_CLASS
#define OID_FDDI_PORT_PMD_CLASS 0x03030265
#endif
#ifndef OID_FDDI_PORT_REQUESTED_PATHS
#define OID_FDDI_PORT_REQUESTED_PATHS 0x03030261
#endif
#ifndef OID_FDDI_RING_MGT_STATE
#define OID_FDDI_RING_MGT_STATE 0x03020106
#endif
#ifndef OID_FDDI_SHORT_CURRENT_ADDR
#define OID_FDDI_SHORT_CURRENT_ADDR 0x03010106
#endif
#ifndef OID_FDDI_SHORT_MAX_LIST_SIZE
#define OID_FDDI_SHORT_MAX_LIST_SIZE 0x03010108
#endif
#ifndef OID_FDDI_SHORT_MULTICAST_LIST
#define OID_FDDI_SHORT_MULTICAST_LIST 0x03010107
#endif
#ifndef OID_FDDI_SHORT_PERMANENT_ADDR
#define OID_FDDI_SHORT_PERMANENT_ADDR 0x03010105
#endif
#ifndef OID_FDDI_SMT_AVAILABLE_PATHS
#define OID_FDDI_SMT_AVAILABLE_PATHS 0x0303020B
#endif
#ifndef OID_FDDI_SMT_BYPASS_PRESENT
#define OID_FDDI_SMT_BYPASS_PRESENT 0x03030214
#endif
#ifndef OID_FDDI_SMT_CF_STATE
#define OID_FDDI_SMT_CF_STATE 0x03030216
#endif
#ifndef OID_FDDI_SMT_CONFIG_CAPABILITIES
#define OID_FDDI_SMT_CONFIG_CAPABILITIES 0x0303020C
#endif
#ifndef OID_FDDI_SMT_CONFIG_POLICY
#define OID_FDDI_SMT_CONFIG_POLICY 0x0303020D
#endif
#ifndef OID_FDDI_SMT_CONNECTION_POLICY
#define OID_FDDI_SMT_CONNECTION_POLICY 0x0303020E
#endif
#ifndef OID_FDDI_SMT_ECM_STATE
#define OID_FDDI_SMT_ECM_STATE 0x03030215
#endif
#ifndef OID_FDDI_SMT_HI_VERSION_ID
#define OID_FDDI_SMT_HI_VERSION_ID 0x03030203
#endif
#ifndef OID_FDDI_SMT_HOLD_STATE
#define OID_FDDI_SMT_HOLD_STATE 0x03030217
#endif
#ifndef OID_FDDI_SMT_LAST_SET_STATION_ID
#define OID_FDDI_SMT_LAST_SET_STATION_ID 0x0303021E
#endif
#ifndef OID_FDDI_SMT_LO_VERSION_ID
#define OID_FDDI_SMT_LO_VERSION_ID 0x03030204
#endif
#ifndef OID_FDDI_SMT_MAC_CT
#define OID_FDDI_SMT_MAC_CT 0x03030208
#endif
#ifndef OID_FDDI_SMT_MAC_INDEXES
#define OID_FDDI_SMT_MAC_INDEXES 0x03030213
#endif
#ifndef OID_FDDI_SMT_MANUFACTURER_DATA
#define OID_FDDI_SMT_MANUFACTURER_DATA 0x03030205
#endif
#ifndef OID_FDDI_SMT_MASTER_CT
#define OID_FDDI_SMT_MASTER_CT 0x0303020A
#endif
#ifndef OID_FDDI_SMT_MIB_VERSION_ID
#define OID_FDDI_SMT_MIB_VERSION_ID 0x03030207
#endif
#ifndef OID_FDDI_SMT_MSG_TIME_STAMP
#define OID_FDDI_SMT_MSG_TIME_STAMP 0x0303021B
#endif
#ifndef OID_FDDI_SMT_NON_MASTER_CT
#define OID_FDDI_SMT_NON_MASTER_CT 0x03030209
#endif
#ifndef OID_FDDI_SMT_OP_VERSION_ID
#define OID_FDDI_SMT_OP_VERSION_ID 0x03030202
#endif
#ifndef OID_FDDI_SMT_PEER_WRAP_FLAG
#define OID_FDDI_SMT_PEER_WRAP_FLAG 0x0303021A
#endif
#ifndef OID_FDDI_SMT_PORT_INDEXES
#define OID_FDDI_SMT_PORT_INDEXES 0x03030212
#endif
#ifndef OID_FDDI_SMT_REMOTE_DISCONNECT_FLAG
#define OID_FDDI_SMT_REMOTE_DISCONNECT_FLAG 0x03030218
#endif
#ifndef OID_FDDI_SMT_SET_COUNT
#define OID_FDDI_SMT_SET_COUNT 0x0303021D
#endif
#ifndef OID_FDDI_SMT_STATION_ACTION
#define OID_FDDI_SMT_STATION_ACTION 0x03030277
#endif
#ifndef OID_FDDI_SMT_STATION_ID
#define OID_FDDI_SMT_STATION_ID 0x03030201
#endif
#ifndef OID_FDDI_SMT_STATION_STATUS
#define OID_FDDI_SMT_STATION_STATUS 0x03030219
#endif
#ifndef OID_FDDI_SMT_STAT_RPT_POLICY
#define OID_FDDI_SMT_STAT_RPT_POLICY 0x03030210
#endif
#ifndef OID_FDDI_SMT_TRACE_MAX_EXPIRATION
#define OID_FDDI_SMT_TRACE_MAX_EXPIRATION 0x03030211
#endif
#ifndef OID_FDDI_SMT_TRANSITION_TIME_STAMP
#define OID_FDDI_SMT_TRANSITION_TIME_STAMP 0x0303021C
#endif
#ifndef OID_FDDI_SMT_T_NOTIFY
#define OID_FDDI_SMT_T_NOTIFY 0x0303020F
#endif
#ifndef OID_FDDI_SMT_USER_DATA
#define OID_FDDI_SMT_USER_DATA 0x03030206
#endif
#ifndef OID_FDDI_UPSTREAM_NODE_LONG
#define OID_FDDI_UPSTREAM_NODE_LONG 0x03020102
#endif
#ifndef OID_GEN_ADMIN_STATUS
#define OID_GEN_ADMIN_STATUS 0x00010288
#endif
#ifndef OID_GEN_ALIAS
#define OID_GEN_ALIAS 0x00010289
#endif
#ifndef OID_GEN_CO_BYTES_XMIT_OUTSTANDING
#define OID_GEN_CO_BYTES_XMIT_OUTSTANDING 0x00020221
#endif
#ifndef OID_GEN_DISCONTINUITY_TIME
#define OID_GEN_DISCONTINUITY_TIME 0x00010282
#endif
#ifndef OID_GEN_INTERFACE_INFO
#define OID_GEN_INTERFACE_INFO 0x00010287
#endif
#ifndef OID_GEN_IP_OPER_STATUS
#define OID_GEN_IP_OPER_STATUS 0x0001028D
#endif
#ifndef OID_GEN_ISOLATION_PARAMETERS
#define OID_GEN_ISOLATION_PARAMETERS 0x00010300
#endif
#ifndef OID_GEN_LAST_CHANGE
#define OID_GEN_LAST_CHANGE 0x00010281
#endif
#ifndef OID_GEN_MAC_ADDRESS
#define OID_GEN_MAC_ADDRESS 0x00010205
#endif
#ifndef OID_GEN_NDIS_RESERVED_1
#define OID_GEN_NDIS_RESERVED_1 0x00020217
#endif
#ifndef OID_GEN_NDIS_RESERVED_2
#define OID_GEN_NDIS_RESERVED_2 0x00020218
#endif
#ifndef OID_GEN_NDIS_RESERVED_3
#define OID_GEN_NDIS_RESERVED_3 0x0001020A
#endif
#ifndef OID_GEN_NDIS_RESERVED_4
#define OID_GEN_NDIS_RESERVED_4 0x0001020B
#endif
#ifndef OID_GEN_NDIS_RESERVED_5
#define OID_GEN_NDIS_RESERVED_5 0x0001020C
#endif
#ifndef OID_GEN_NDIS_RESERVED_6
#define OID_GEN_NDIS_RESERVED_6 0x00010212
#endif
#ifndef OID_GEN_NDIS_RESERVED_7
#define OID_GEN_NDIS_RESERVED_7 0x0002021E
#endif
#ifndef OID_GEN_OPERATIONAL_STATUS
#define OID_GEN_OPERATIONAL_STATUS 0x00010283
#endif
#ifndef OID_GEN_PCI_DEVICE_CUSTOM_PROPERTIES
#define OID_GEN_PCI_DEVICE_CUSTOM_PROPERTIES 0x00010211
#endif
#ifndef OID_GEN_PROMISCUOUS_MODE
#define OID_GEN_PROMISCUOUS_MODE 0x00010280
#endif
#ifndef OID_GEN_RCV_LINK_SPEED
#define OID_GEN_RCV_LINK_SPEED 0x00010285
#endif
#ifndef OID_GEN_RECEIVE_HASH
#define OID_GEN_RECEIVE_HASH 0x0001021F
#endif
#ifndef OID_GEN_TIMEOUT_DPC_REQUEST_CAPABILITIES
#define OID_GEN_TIMEOUT_DPC_REQUEST_CAPABILITIES 0x00010210
#endif
#ifndef OID_GEN_UNKNOWN_PROTOS
#define OID_GEN_UNKNOWN_PROTOS 0x00010286
#endif
#ifndef OID_GEN_XMIT_LINK_SPEED
#define OID_GEN_XMIT_LINK_SPEED 0x00010284
#endif
#ifndef OID_IRDA_EXTRA_RCV_BOFS
#define OID_IRDA_EXTRA_RCV_BOFS 0x0A010200
#endif
#ifndef OID_IRDA_LINK_SPEED
#define OID_IRDA_LINK_SPEED 0x0A010103
#endif
#ifndef OID_IRDA_MAX_RECEIVE_WINDOW_SIZE
#define OID_IRDA_MAX_RECEIVE_WINDOW_SIZE 0x0A010204
#endif
#ifndef OID_IRDA_MAX_SEND_WINDOW_SIZE
#define OID_IRDA_MAX_SEND_WINDOW_SIZE 0x0A010205
#endif
#ifndef OID_IRDA_MAX_UNICAST_LIST_SIZE
#define OID_IRDA_MAX_UNICAST_LIST_SIZE 0x0A010203
#endif
#ifndef OID_IRDA_MEDIA_BUSY
#define OID_IRDA_MEDIA_BUSY 0x0A010104
#endif
#ifndef OID_IRDA_RATE_SNIFF
#define OID_IRDA_RATE_SNIFF 0x0A010201
#endif
#ifndef OID_IRDA_RECEIVING
#define OID_IRDA_RECEIVING 0x0A010100
#endif
#ifndef OID_IRDA_RESERVED1
#define OID_IRDA_RESERVED1 0x0A01020A
#endif
#ifndef OID_IRDA_RESERVED2
#define OID_IRDA_RESERVED2 0x0A01020F
#endif
#ifndef OID_IRDA_SUPPORTED_SPEEDS
#define OID_IRDA_SUPPORTED_SPEEDS 0x0A010102
#endif
#ifndef OID_IRDA_TURNAROUND_TIME
#define OID_IRDA_TURNAROUND_TIME 0x0A010101
#endif
#ifndef OID_IRDA_UNICAST_LIST
#define OID_IRDA_UNICAST_LIST 0x0A010202
#endif
#ifndef OID_LTALK_COLLISIONS
#define OID_LTALK_COLLISIONS 0x05020202
#endif
#ifndef OID_LTALK_CURRENT_NODE_ID
#define OID_LTALK_CURRENT_NODE_ID 0x05010102
#endif
#ifndef OID_LTALK_DEFERS
#define OID_LTALK_DEFERS 0x05020203
#endif
#ifndef OID_LTALK_FCS_ERRORS
#define OID_LTALK_FCS_ERRORS 0x05020206
#endif
#ifndef OID_LTALK_IN_BROADCASTS
#define OID_LTALK_IN_BROADCASTS 0x05020101
#endif
#ifndef OID_LTALK_IN_LENGTH_ERRORS
#define OID_LTALK_IN_LENGTH_ERRORS 0x05020102
#endif
#ifndef OID_LTALK_NO_DATA_ERRORS
#define OID_LTALK_NO_DATA_ERRORS 0x05020204
#endif
#ifndef OID_LTALK_OUT_NO_HANDLERS
#define OID_LTALK_OUT_NO_HANDLERS 0x05020201
#endif
#ifndef OID_LTALK_RANDOM_CTS_ERRORS
#define OID_LTALK_RANDOM_CTS_ERRORS 0x05020205
#endif
#ifndef OID_NIC_SWITCH_CREATE_SWITCH
#define OID_NIC_SWITCH_CREATE_SWITCH 0x00010237
#endif
#ifndef OID_NIC_SWITCH_CREATE_VPORT
#define OID_NIC_SWITCH_CREATE_VPORT 0x00010241
#endif
#ifndef OID_NIC_SWITCH_CURRENT_CAPABILITIES
#define OID_NIC_SWITCH_CURRENT_CAPABILITIES 0x0001022F
#endif
#ifndef OID_NIC_SWITCH_DELETE_SWITCH
#define OID_NIC_SWITCH_DELETE_SWITCH 0x00010239
#endif
#ifndef OID_NIC_SWITCH_DELETE_VPORT
#define OID_NIC_SWITCH_DELETE_VPORT 0x00010244
#endif
#ifndef OID_NIC_SWITCH_ENUM_SWITCHES
#define OID_NIC_SWITCH_ENUM_SWITCHES 0x00010240
#endif
#ifndef OID_NIC_SWITCH_ENUM_VFS
#define OID_NIC_SWITCH_ENUM_VFS 0x00010248
#endif
#ifndef OID_NIC_SWITCH_ENUM_VPORTS
#define OID_NIC_SWITCH_ENUM_VPORTS 0x00010243
#endif
#ifndef OID_NIC_SWITCH_FREE_VF
#define OID_NIC_SWITCH_FREE_VF 0x00010246
#endif
#ifndef OID_NIC_SWITCH_HARDWARE_CAPABILITIES
#define OID_NIC_SWITCH_HARDWARE_CAPABILITIES 0x0001022E
#endif
#ifndef OID_NIC_SWITCH_PARAMETERS
#define OID_NIC_SWITCH_PARAMETERS 0x00010238
#endif
#ifndef OID_NIC_SWITCH_VF_PARAMETERS
#define OID_NIC_SWITCH_VF_PARAMETERS 0x00010247
#endif
#ifndef OID_NIC_SWITCH_VPORT_PARAMETERS
#define OID_NIC_SWITCH_VPORT_PARAMETERS 0x00010242
#endif
#ifndef OID_PM_CURRENT_CAPABILITIES
#define OID_PM_CURRENT_CAPABILITIES 0xFD010107
#endif
#ifndef OID_PM_GET_PROTOCOL_OFFLOAD
#define OID_PM_GET_PROTOCOL_OFFLOAD 0xFD01010E
#endif
#ifndef OID_PM_HARDWARE_CAPABILITIES
#define OID_PM_HARDWARE_CAPABILITIES 0xFD010108
#endif
#ifndef OID_PM_PROTOCOL_OFFLOAD_LIST
#define OID_PM_PROTOCOL_OFFLOAD_LIST 0xFD010110
#endif
#ifndef OID_PM_RESERVED_1
#define OID_PM_RESERVED_1 0xFD010111
#endif
#ifndef OID_PM_WOL_PATTERN_LIST
#define OID_PM_WOL_PATTERN_LIST 0xFD01010C
#endif
#ifndef OID_QOS_RESERVED1
#define OID_QOS_RESERVED1 0xFB010100
#endif
#ifndef OID_QOS_RESERVED10
#define OID_QOS_RESERVED10 0xFB010109
#endif
#ifndef OID_QOS_RESERVED11
#define OID_QOS_RESERVED11 0xFB01010A
#endif
#ifndef OID_QOS_RESERVED12
#define OID_QOS_RESERVED12 0xFB01010B
#endif
#ifndef OID_QOS_RESERVED13
#define OID_QOS_RESERVED13 0xFB01010C
#endif
#ifndef OID_QOS_RESERVED14
#define OID_QOS_RESERVED14 0xFB01010D
#endif
#ifndef OID_QOS_RESERVED15
#define OID_QOS_RESERVED15 0xFB01010E
#endif
#ifndef OID_QOS_RESERVED16
#define OID_QOS_RESERVED16 0xFB01010F
#endif
#ifndef OID_QOS_RESERVED17
#define OID_QOS_RESERVED17 0xFB010110
#endif
#ifndef OID_QOS_RESERVED18
#define OID_QOS_RESERVED18 0xFB010111
#endif
#ifndef OID_QOS_RESERVED19
#define OID_QOS_RESERVED19 0xFB010112
#endif
#ifndef OID_QOS_RESERVED2
#define OID_QOS_RESERVED2 0xFB010101
#endif
#ifndef OID_QOS_RESERVED20
#define OID_QOS_RESERVED20 0xFB010113
#endif
#ifndef OID_QOS_RESERVED3
#define OID_QOS_RESERVED3 0xFB010102
#endif
#ifndef OID_QOS_RESERVED4
#define OID_QOS_RESERVED4 0xFB010103
#endif
#ifndef OID_QOS_RESERVED5
#define OID_QOS_RESERVED5 0xFB010104
#endif
#ifndef OID_QOS_RESERVED6
#define OID_QOS_RESERVED6 0xFB010105
#endif
#ifndef OID_QOS_RESERVED7
#define OID_QOS_RESERVED7 0xFB010106
#endif
#ifndef OID_QOS_RESERVED8
#define OID_QOS_RESERVED8 0xFB010107
#endif
#ifndef OID_QOS_RESERVED9
#define OID_QOS_RESERVED9 0xFB010108
#endif
#ifndef OID_RECEIVE_FILTER_CURRENT_CAPABILITIES
#define OID_RECEIVE_FILTER_CURRENT_CAPABILITIES 0x0001022D
#endif
#ifndef OID_RECEIVE_FILTER_ENUM_FILTERS
#define OID_RECEIVE_FILTER_ENUM_FILTERS 0x00010229
#endif
#ifndef OID_RECEIVE_FILTER_ENUM_QUEUES
#define OID_RECEIVE_FILTER_ENUM_QUEUES 0x00010225
#endif
#ifndef OID_RECEIVE_FILTER_GLOBAL_PARAMETERS
#define OID_RECEIVE_FILTER_GLOBAL_PARAMETERS 0x00010222
#endif
#ifndef OID_RECEIVE_FILTER_HARDWARE_CAPABILITIES
#define OID_RECEIVE_FILTER_HARDWARE_CAPABILITIES 0x00010221
#endif
#ifndef OID_RECEIVE_FILTER_PARAMETERS
#define OID_RECEIVE_FILTER_PARAMETERS 0x0001022A
#endif
#ifndef OID_SRIOV_BAR_RESOURCES
#define OID_SRIOV_BAR_RESOURCES 0x00010259
#endif
#ifndef OID_SRIOV_CONFIG_STATE
#define OID_SRIOV_CONFIG_STATE 0x00010261
#endif
#ifndef OID_SRIOV_CURRENT_CAPABILITIES
#define OID_SRIOV_CURRENT_CAPABILITIES 0x00010250
#endif
#ifndef OID_SRIOV_HARDWARE_CAPABILITIES
#define OID_SRIOV_HARDWARE_CAPABILITIES 0x00010249
#endif
#ifndef OID_SRIOV_PF_LUID
#define OID_SRIOV_PF_LUID 0x00010260
#endif
#ifndef OID_SRIOV_PROBED_BARS
#define OID_SRIOV_PROBED_BARS 0x00010258
#endif
#ifndef OID_SRIOV_READ_VF_CONFIG_BLOCK
#define OID_SRIOV_READ_VF_CONFIG_BLOCK 0x00010253
#endif
#ifndef OID_SRIOV_READ_VF_CONFIG_SPACE
#define OID_SRIOV_READ_VF_CONFIG_SPACE 0x00010251
#endif
#ifndef OID_SRIOV_RESET_VF
#define OID_SRIOV_RESET_VF 0x00010255
#endif
#ifndef OID_SRIOV_SET_VF_POWER_STATE
#define OID_SRIOV_SET_VF_POWER_STATE 0x00010256
#endif
#ifndef OID_SRIOV_VF_INVALIDATE_CONFIG_BLOCK
#define OID_SRIOV_VF_INVALIDATE_CONFIG_BLOCK 0x00010269
#endif
#ifndef OID_SRIOV_VF_SERIAL_NUMBER
#define OID_SRIOV_VF_SERIAL_NUMBER 0x00010262
#endif
#ifndef OID_SRIOV_VF_VENDOR_DEVICE_ID
#define OID_SRIOV_VF_VENDOR_DEVICE_ID 0x00010257
#endif
#ifndef OID_SRIOV_WRITE_VF_CONFIG_BLOCK
#define OID_SRIOV_WRITE_VF_CONFIG_BLOCK 0x00010254
#endif
#ifndef OID_SRIOV_WRITE_VF_CONFIG_SPACE
#define OID_SRIOV_WRITE_VF_CONFIG_SPACE 0x00010252
#endif
#ifndef OID_SWITCH_NIC_SUSPENDED_LM_SOURCE_FINISHED
#define OID_SWITCH_NIC_SUSPENDED_LM_SOURCE_FINISHED 0x0001029A
#endif
#ifndef OID_SWITCH_NIC_SUSPENDED_LM_SOURCE_STARTED
#define OID_SWITCH_NIC_SUSPENDED_LM_SOURCE_STARTED 0x00010299
#endif
#ifndef OID_TAPI_ACCEPT
#define OID_TAPI_ACCEPT 0x07030101
#endif
#ifndef OID_TAPI_ANSWER
#define OID_TAPI_ANSWER 0x07030102
#endif
#ifndef OID_TAPI_CLOSE
#define OID_TAPI_CLOSE 0x07030103
#endif
#ifndef OID_TAPI_CLOSE_CALL
#define OID_TAPI_CLOSE_CALL 0x07030104
#endif
#ifndef OID_TAPI_CONDITIONAL_MEDIA_DETECTION
#define OID_TAPI_CONDITIONAL_MEDIA_DETECTION 0x07030105
#endif
#ifndef OID_TAPI_CONFIG_DIALOG
#define OID_TAPI_CONFIG_DIALOG 0x07030106
#endif
#ifndef OID_TAPI_DEV_SPECIFIC
#define OID_TAPI_DEV_SPECIFIC 0x07030107
#endif
#ifndef OID_TAPI_DIAL
#define OID_TAPI_DIAL 0x07030108
#endif
#ifndef OID_TAPI_DROP
#define OID_TAPI_DROP 0x07030109
#endif
#ifndef OID_TAPI_GATHER_DIGITS
#define OID_TAPI_GATHER_DIGITS 0x07030123
#endif
#ifndef OID_TAPI_GET_ADDRESS_CAPS
#define OID_TAPI_GET_ADDRESS_CAPS 0x0703010A
#endif
#ifndef OID_TAPI_GET_ADDRESS_ID
#define OID_TAPI_GET_ADDRESS_ID 0x0703010B
#endif
#ifndef OID_TAPI_GET_ADDRESS_STATUS
#define OID_TAPI_GET_ADDRESS_STATUS 0x0703010C
#endif
#ifndef OID_TAPI_GET_CALL_ADDRESS_ID
#define OID_TAPI_GET_CALL_ADDRESS_ID 0x0703010D
#endif
#ifndef OID_TAPI_GET_CALL_INFO
#define OID_TAPI_GET_CALL_INFO 0x0703010E
#endif
#ifndef OID_TAPI_GET_CALL_STATUS
#define OID_TAPI_GET_CALL_STATUS 0x0703010F
#endif
#ifndef OID_TAPI_GET_DEV_CAPS
#define OID_TAPI_GET_DEV_CAPS 0x07030110
#endif
#ifndef OID_TAPI_GET_DEV_CONFIG
#define OID_TAPI_GET_DEV_CONFIG 0x07030111
#endif
#ifndef OID_TAPI_GET_EXTENSION_ID
#define OID_TAPI_GET_EXTENSION_ID 0x07030112
#endif
#ifndef OID_TAPI_GET_ID
#define OID_TAPI_GET_ID 0x07030113
#endif
#ifndef OID_TAPI_GET_LINE_DEV_STATUS
#define OID_TAPI_GET_LINE_DEV_STATUS 0x07030114
#endif
#ifndef OID_TAPI_MAKE_CALL
#define OID_TAPI_MAKE_CALL 0x07030115
#endif
#ifndef OID_TAPI_MONITOR_DIGITS
#define OID_TAPI_MONITOR_DIGITS 0x07030124
#endif
#ifndef OID_TAPI_NEGOTIATE_EXT_VERSION
#define OID_TAPI_NEGOTIATE_EXT_VERSION 0x07030116
#endif
#ifndef OID_TAPI_OPEN
#define OID_TAPI_OPEN 0x07030117
#endif
#ifndef OID_TAPI_PROVIDER_INITIALIZE
#define OID_TAPI_PROVIDER_INITIALIZE 0x07030118
#endif
#ifndef OID_TAPI_PROVIDER_SHUTDOWN
#define OID_TAPI_PROVIDER_SHUTDOWN 0x07030119
#endif
#ifndef OID_TAPI_SECURE_CALL
#define OID_TAPI_SECURE_CALL 0x0703011A
#endif
#ifndef OID_TAPI_SELECT_EXT_VERSION
#define OID_TAPI_SELECT_EXT_VERSION 0x0703011B
#endif
#ifndef OID_TAPI_SEND_USER_USER_INFO
#define OID_TAPI_SEND_USER_USER_INFO 0x0703011C
#endif
#ifndef OID_TAPI_SET_APP_SPECIFIC
#define OID_TAPI_SET_APP_SPECIFIC 0x0703011D
#endif
#ifndef OID_TAPI_SET_CALL_PARAMS
#define OID_TAPI_SET_CALL_PARAMS 0x0703011E
#endif
#ifndef OID_TAPI_SET_DEFAULT_MEDIA_DETECTION
#define OID_TAPI_SET_DEFAULT_MEDIA_DETECTION 0x0703011F
#endif
#ifndef OID_TAPI_SET_DEV_CONFIG
#define OID_TAPI_SET_DEV_CONFIG 0x07030120
#endif
#ifndef OID_TAPI_SET_MEDIA_MODE
#define OID_TAPI_SET_MEDIA_MODE 0x07030121
#endif
#ifndef OID_TAPI_SET_STATUS_MESSAGES
#define OID_TAPI_SET_STATUS_MESSAGES 0x07030122
#endif
#ifndef OID_TCP_CONNECTION_OFFLOAD_PARAMETERS
#define OID_TCP_CONNECTION_OFFLOAD_PARAMETERS 0xFC030201
#endif
#ifndef OID_TIMESTAMP_CAPABILITY
#define OID_TIMESTAMP_CAPABILITY 0x00A00001
#endif
#ifndef OID_TIMESTAMP_CURRENT_CONFIG
#define OID_TIMESTAMP_CURRENT_CONFIG 0x00A00002
#endif
#ifndef OID_TIMESTAMP_GET_CROSSTIMESTAMP
#define OID_TIMESTAMP_GET_CROSSTIMESTAMP 0x00A00003
#endif
#ifndef OID_TUNNEL_INTERFACE_RELEASE_OID
#define OID_TUNNEL_INTERFACE_RELEASE_OID 0x0f010107
#endif
#ifndef OID_TUNNEL_INTERFACE_SET_OID
#define OID_TUNNEL_INTERFACE_SET_OID 0x0f010106
#endif
#ifndef OID_VLAN_RESERVED1
#define OID_VLAN_RESERVED1 0x00010231
#endif
#ifndef OID_VLAN_RESERVED2
#define OID_VLAN_RESERVED2 0x00010232
#endif
#ifndef OID_VLAN_RESERVED3
#define OID_VLAN_RESERVED3 0x00010233
#endif
#ifndef OID_VLAN_RESERVED4
#define OID_VLAN_RESERVED4 0x00010234
#endif
#ifndef OID_WAN_CO_GET_COMP_INFO
#define OID_WAN_CO_GET_COMP_INFO 0x04010280
#endif
#ifndef OID_WAN_CO_GET_INFO
#define OID_WAN_CO_GET_INFO 0x04010180
#endif
#ifndef OID_WAN_CO_GET_LINK_INFO
#define OID_WAN_CO_GET_LINK_INFO 0x04010182
#endif
#ifndef OID_WAN_CO_GET_STATS_INFO
#define OID_WAN_CO_GET_STATS_INFO 0x04010282
#endif
#ifndef OID_WAN_CO_SET_COMP_INFO
#define OID_WAN_CO_SET_COMP_INFO 0x04010281
#endif
#ifndef OID_WAN_CO_SET_LINK_INFO
#define OID_WAN_CO_SET_LINK_INFO 0x04010181
#endif
#ifndef OID_WAN_CURRENT_ADDRESS
#define OID_WAN_CURRENT_ADDRESS 0x04010102
#endif
#ifndef OID_WAN_GET_BRIDGE_INFO
#define OID_WAN_GET_BRIDGE_INFO 0x0401020A
#endif
#ifndef OID_WAN_GET_COMP_INFO
#define OID_WAN_GET_COMP_INFO 0x0401020C
#endif
#ifndef OID_WAN_GET_INFO
#define OID_WAN_GET_INFO 0x04010107
#endif
#ifndef OID_WAN_GET_LINK_INFO
#define OID_WAN_GET_LINK_INFO 0x04010109
#endif
#ifndef OID_WAN_GET_STATS_INFO
#define OID_WAN_GET_STATS_INFO 0x0401020E
#endif
#ifndef OID_WAN_HEADER_FORMAT
#define OID_WAN_HEADER_FORMAT 0x04010106
#endif
#ifndef OID_WAN_LINE_COUNT
#define OID_WAN_LINE_COUNT 0x0401010A
#endif
#ifndef OID_WAN_MEDIUM_SUBTYPE
#define OID_WAN_MEDIUM_SUBTYPE 0x04010105
#endif
#ifndef OID_WAN_PERMANENT_ADDRESS
#define OID_WAN_PERMANENT_ADDRESS 0x04010101
#endif
#ifndef OID_WAN_PROTOCOL_CAPS
#define OID_WAN_PROTOCOL_CAPS 0x0401010B
#endif
#ifndef OID_WAN_PROTOCOL_TYPE
#define OID_WAN_PROTOCOL_TYPE 0x04010104
#endif
#ifndef OID_WAN_QUALITY_OF_SERVICE
#define OID_WAN_QUALITY_OF_SERVICE 0x04010103
#endif
#ifndef OID_WAN_SET_BRIDGE_INFO
#define OID_WAN_SET_BRIDGE_INFO 0x0401020B
#endif
#ifndef OID_WAN_SET_COMP_INFO
#define OID_WAN_SET_COMP_INFO 0x0401020D
#endif
#ifndef OID_WAN_SET_LINK_INFO
#define OID_WAN_SET_LINK_INFO 0x04010108
#endif
#ifndef OID_WWAN_AUTH_CHALLENGE
#define OID_WWAN_AUTH_CHALLENGE 0x0e010115
#endif
#ifndef OID_WWAN_BASE_STATIONS_INFO
#define OID_WWAN_BASE_STATIONS_INFO 0x0e010148
#endif
#ifndef OID_WWAN_CONNECT
#define OID_WWAN_CONNECT 0x0e01010c
#endif
#ifndef OID_WWAN_CREATE_MAC
#define OID_WWAN_CREATE_MAC 0x0e010126
#endif
#ifndef OID_WWAN_DELETE_MAC
#define OID_WWAN_DELETE_MAC 0x0e010127
#endif
#ifndef OID_WWAN_DEVICE_BINDINGS
#define OID_WWAN_DEVICE_BINDINGS 0x0e010131
#endif
#ifndef OID_WWAN_DEVICE_CAPS
#define OID_WWAN_DEVICE_CAPS 0x0e010101
#endif
#ifndef OID_WWAN_DEVICE_CAPS_EX
#define OID_WWAN_DEVICE_CAPS_EX 0x0e01012e
#endif
#ifndef OID_WWAN_DEVICE_RESET
#define OID_WWAN_DEVICE_RESET 0x0e010147
#endif
#ifndef OID_WWAN_DEVICE_SERVICE_COMMAND
#define OID_WWAN_DEVICE_SERVICE_COMMAND 0x0e010118
#endif
#ifndef OID_WWAN_DEVICE_SERVICE_SESSION
#define OID_WWAN_DEVICE_SERVICE_SESSION 0x0e010123
#endif
#ifndef OID_WWAN_DEVICE_SERVICE_SESSION_WRITE
#define OID_WWAN_DEVICE_SERVICE_SESSION_WRITE 0x0e010124
#endif
#ifndef OID_WWAN_DRIVER_CAPS
#define OID_WWAN_DRIVER_CAPS 0x0e010100
#endif
#ifndef OID_WWAN_ENUMERATE_DEVICE_SERVICES
#define OID_WWAN_ENUMERATE_DEVICE_SERVICES 0x0e010116
#endif
#ifndef OID_WWAN_ENUMERATE_DEVICE_SERVICE_COMMANDS
#define OID_WWAN_ENUMERATE_DEVICE_SERVICE_COMMANDS 0x0e010122
#endif
#ifndef OID_WWAN_HOME_PROVIDER
#define OID_WWAN_HOME_PROVIDER 0x0e010106
#endif
#ifndef OID_WWAN_IMS_VOICE_STATE
#define OID_WWAN_IMS_VOICE_STATE 0x0e010133
#endif
#ifndef OID_WWAN_LOCATION_STATE
#define OID_WWAN_LOCATION_STATE 0x0e010135
#endif
#ifndef OID_WWAN_LTE_ATTACH_CONFIG
#define OID_WWAN_LTE_ATTACH_CONFIG 0x0e010142
#endif
#ifndef OID_WWAN_LTE_ATTACH_STATUS
#define OID_WWAN_LTE_ATTACH_STATUS 0x0e010143
#endif
#ifndef OID_WWAN_MBIM_VERSION
#define OID_WWAN_MBIM_VERSION 0x0e01012c
#endif
#ifndef OID_WWAN_MODEM_CONFIG_INFO
#define OID_WWAN_MODEM_CONFIG_INFO 0x0e010144
#endif
#ifndef OID_WWAN_MODEM_LOGGING_CONFIG
#define OID_WWAN_MODEM_LOGGING_CONFIG 0x0e01014b
#endif
#ifndef OID_WWAN_MPDP
#define OID_WWAN_MPDP 0x0e010149
#endif
#ifndef OID_WWAN_NETWORK_BLACKLIST
#define OID_WWAN_NETWORK_BLACKLIST 0x0e010141
#endif
#ifndef OID_WWAN_NETWORK_IDLE_HINT
#define OID_WWAN_NETWORK_IDLE_HINT 0x0e010137
#endif
#ifndef OID_WWAN_NETWORK_PARAMS
#define OID_WWAN_NETWORK_PARAMS 0x0e01014d
#endif
#ifndef OID_WWAN_NITZ
#define OID_WWAN_NITZ 0x0e010136
#endif
#ifndef OID_WWAN_PACKET_SERVICE
#define OID_WWAN_PACKET_SERVICE 0x0e01010a
#endif
#ifndef OID_WWAN_PCO
#define OID_WWAN_PCO 0x0e010145
#endif
#ifndef OID_WWAN_PIN
#define OID_WWAN_PIN 0x0e010104
#endif
#ifndef OID_WWAN_PIN_EX
#define OID_WWAN_PIN_EX 0x0e010121
#endif
#ifndef OID_WWAN_PIN_EX2
#define OID_WWAN_PIN_EX2 0x0e01012b
#endif
#ifndef OID_WWAN_PIN_LIST
#define OID_WWAN_PIN_LIST 0x0e010105
#endif
#ifndef OID_WWAN_PREFERRED_MULTICARRIER_PROVIDERS
#define OID_WWAN_PREFERRED_MULTICARRIER_PROVIDERS 0x0e010125
#endif
#ifndef OID_WWAN_PREFERRED_PROVIDERS
#define OID_WWAN_PREFERRED_PROVIDERS 0x0e010107
#endif
#ifndef OID_WWAN_PRESHUTDOWN
#define OID_WWAN_PRESHUTDOWN 0x0e010138
#endif
#ifndef OID_WWAN_PROVISIONED_CONTEXTS
#define OID_WWAN_PROVISIONED_CONTEXTS 0x0e01010d
#endif
#ifndef OID_WWAN_PS_MEDIA_CONFIG
#define OID_WWAN_PS_MEDIA_CONFIG 0x0e01013e
#endif
#ifndef OID_WWAN_RADIO_STATE
#define OID_WWAN_RADIO_STATE 0x0e010103
#endif
#ifndef OID_WWAN_READY_INFO
#define OID_WWAN_READY_INFO 0x0e010102
#endif
#ifndef OID_WWAN_REGISTER_PARAMS
#define OID_WWAN_REGISTER_PARAMS 0x0e01014c
#endif
#ifndef OID_WWAN_REGISTER_STATE
#define OID_WWAN_REGISTER_STATE 0x0e010109
#endif
#ifndef OID_WWAN_REGISTER_STATE_EX
#define OID_WWAN_REGISTER_STATE_EX 0x0e010132
#endif
#ifndef OID_WWAN_SAR_CONFIG
#define OID_WWAN_SAR_CONFIG 0x0e01013f
#endif
#ifndef OID_WWAN_SAR_TRANSMISSION_STATUS
#define OID_WWAN_SAR_TRANSMISSION_STATUS 0x0e010140
#endif
#ifndef OID_WWAN_SERVICE_ACTIVATION
#define OID_WWAN_SERVICE_ACTIVATION 0x0e01010e
#endif
#ifndef OID_WWAN_SIGNAL_STATE
#define OID_WWAN_SIGNAL_STATE 0x0e01010b
#endif
#ifndef OID_WWAN_SIGNAL_STATE_EX
#define OID_WWAN_SIGNAL_STATE_EX 0x0e010134
#endif
#ifndef OID_WWAN_SLOT_INFO_STATUS
#define OID_WWAN_SLOT_INFO_STATUS 0x0e010130
#endif
#ifndef OID_WWAN_SMS_CONFIGURATION
#define OID_WWAN_SMS_CONFIGURATION 0x0e01010f
#endif
#ifndef OID_WWAN_SMS_DELETE
#define OID_WWAN_SMS_DELETE 0x0e010112
#endif
#ifndef OID_WWAN_SMS_READ
#define OID_WWAN_SMS_READ 0x0e010110
#endif
#ifndef OID_WWAN_SMS_SEND
#define OID_WWAN_SMS_SEND 0x0e010111
#endif
#ifndef OID_WWAN_SMS_STATUS
#define OID_WWAN_SMS_STATUS 0x0e010113
#endif
#ifndef OID_WWAN_SUBSCRIBE_DEVICE_SERVICE_EVENTS
#define OID_WWAN_SUBSCRIBE_DEVICE_SERVICE_EVENTS 0x0e010117
#endif
#ifndef OID_WWAN_SYS_CAPS
#define OID_WWAN_SYS_CAPS 0x0e01012d
#endif
#ifndef OID_WWAN_SYS_SLOTMAPPINGS
#define OID_WWAN_SYS_SLOTMAPPINGS 0x0e01012f
#endif
#ifndef OID_WWAN_UE_POLICY
#define OID_WWAN_UE_POLICY 0x0e01014e
#endif
#ifndef OID_WWAN_UICC_ACCESS_BINARY
#define OID_WWAN_UICC_ACCESS_BINARY 0x0e010129
#endif
#ifndef OID_WWAN_UICC_ACCESS_RECORD
#define OID_WWAN_UICC_ACCESS_RECORD 0x0e01012a
#endif
#ifndef OID_WWAN_UICC_APDU
#define OID_WWAN_UICC_APDU 0x0e01013c
#endif
#ifndef OID_WWAN_UICC_APP_LIST
#define OID_WWAN_UICC_APP_LIST 0x0e01014a
#endif
#ifndef OID_WWAN_UICC_ATR
#define OID_WWAN_UICC_ATR 0x0e010139
#endif
#ifndef OID_WWAN_UICC_CLOSE_CHANNEL
#define OID_WWAN_UICC_CLOSE_CHANNEL 0x0e01013b
#endif
#ifndef OID_WWAN_UICC_FILE_STATUS
#define OID_WWAN_UICC_FILE_STATUS 0x0e010128
#endif
#ifndef OID_WWAN_UICC_OPEN_CHANNEL
#define OID_WWAN_UICC_OPEN_CHANNEL 0x0e01013a
#endif
#ifndef OID_WWAN_UICC_RESET
#define OID_WWAN_UICC_RESET 0x0e010146
#endif
#ifndef OID_WWAN_UICC_TERMINAL_CAPABILITY
#define OID_WWAN_UICC_TERMINAL_CAPABILITY 0x0e01013d
#endif
#ifndef OID_WWAN_USSD
#define OID_WWAN_USSD 0x0e010119
#endif
#ifndef OID_WWAN_VENDOR_SPECIFIC
#define OID_WWAN_VENDOR_SPECIFIC 0x0e010114
#endif
#ifndef OID_WWAN_VISIBLE_PROVIDERS
#define OID_WWAN_VISIBLE_PROVIDERS 0x0e010108
#endif
#ifndef OID_XBOX_ACC_RESERVED0
#define OID_XBOX_ACC_RESERVED0 0xFA000000
#endif
#ifndef WAN_PROTOCOL_KEEPS_STATS
#define WAN_PROTOCOL_KEEPS_STATS 0x00000001
#endif

#endif /* _NTDDNDIS_ */
