/*
 * IOCTL definitions for interfacing with winebth.sys
 *
 * Copyright 2024 Vibhav Pant
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 *
 */

#ifndef __BTHIOCTL_H__
#define __BTHIOCTL_H__

#define IOCTL_BTH_GET_LOCAL_INFO    CTL_CODE(FILE_DEVICE_BLUETOOTH, 0x00, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_BTH_GET_RADIO_INFO    CTL_CODE(FILE_DEVICE_BLUETOOTH, 0x01, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_BTH_GET_DEVICE_INFO   CTL_CODE(FILE_DEVICE_BLUETOOTH, 0x02, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_BTH_DISCONNECT_DEVICE CTL_CODE(FILE_DEVICE_BLUETOOTH, 0x03, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_BTH_HCI_VENDOR_COMMAND CTL_CODE(FILE_DEVICE_BLUETOOTH, 0x14, METHOD_BUFFERED, FILE_ANY_ACCESS)

#pragma pack(push,1)

typedef struct _BTH_RADIO_INFO
{
    ULONGLONG lmpSupportedFeatures;
    USHORT mfg;
    USHORT lmpSubversion;
    UCHAR lmpVersion;
} BTH_RADIO_INFO, *PBTH_RADIO_INFO;

typedef struct _BTH_LOCAL_RADIO_INFO
{
    BTH_DEVICE_INFO localInfo;
    ULONG flags;
    USHORT hciRevision;
    UCHAR hciVersion;
    BTH_RADIO_INFO radioInfo;
} BTH_LOCAL_RADIO_INFO, *PBTH_LOCAL_RADIO_INFO;

typedef struct _BTH_DEVICE_INFO_LIST
{
    ULONG numOfDevices;
    BTH_DEVICE_INFO deviceList[1];
} BTH_DEVICE_INFO_LIST, *PBTH_DEVICE_INFO_LIST;

#pragma pack(pop)

#if ((NTDDI_VERSION >= NTDDI_VISTA))

#define BTH_IOCTL_BASE      0

#define BTH_CTL(id)         CTL_CODE(FILE_DEVICE_BLUETOOTH,  \
                                     (id), \
                                     METHOD_BUFFERED,  \
                                     FILE_ANY_ACCESS)

#define BTH_KERNEL_CTL(id)  CTL_CODE(FILE_DEVICE_BLUETOOTH,  \
                                     (id), \
                                     METHOD_NEITHER,  \
                                     FILE_ANY_ACCESS)

#define IOCTL_INTERNAL_BTH_SUBMIT_BRB       BTH_KERNEL_CTL(BTH_IOCTL_BASE+0x00)

#define IOCTL_INTERNAL_BTHENUM_GET_ENUMINFO BTH_KERNEL_CTL(BTH_IOCTL_BASE+0x01)

#define IOCTL_INTERNAL_BTHENUM_GET_DEVINFO  BTH_KERNEL_CTL(BTH_IOCTL_BASE+0x02)

#define IOCTL_BTH_SDP_CONNECT               BTH_CTL(BTH_IOCTL_BASE+0x80)

#define IOCTL_BTH_SDP_DISCONNECT            BTH_CTL(BTH_IOCTL_BASE+0x81)

#define IOCTL_BTH_SDP_SERVICE_SEARCH        BTH_CTL(BTH_IOCTL_BASE+0x82)

#define IOCTL_BTH_SDP_ATTRIBUTE_SEARCH      BTH_CTL(BTH_IOCTL_BASE+0x83)

#define IOCTL_BTH_SDP_SERVICE_ATTRIBUTE_SEARCH \
                                            BTH_CTL(BTH_IOCTL_BASE+0x84)

#define IOCTL_BTH_SDP_SUBMIT_RECORD         BTH_CTL(BTH_IOCTL_BASE+0x85)

#define IOCTL_BTH_SDP_REMOVE_RECORD         BTH_CTL(BTH_IOCTL_BASE+0x86)

#define IOCTL_BTH_SDP_SUBMIT_RECORD_WITH_INFO BTH_CTL(BTH_IOCTL_BASE+0x87)

#if ((NTDDI_VERSION >= NTDDI_WIN8))

#define IOCTL_BTH_GET_HOST_SUPPORTED_FEATURES BTH_CTL(BTH_IOCTL_BASE+0x88)

#endif

#define SDP_CONNECT_CACHE           (0x00000001)
#define SDP_CONNECT_ALLOW_PIN       (0x00000002)

#define SDP_REQUEST_TO_DEFAULT      (0)
#define SDP_REQUEST_TO_MIN          (10)
#define SDP_REQUEST_TO_MAX          (45)

#define SERVICE_OPTION_DO_NOT_PUBLISH       (0x00000002)
#define SERVICE_OPTION_NO_PUBLIC_BROWSE     (0x00000004)
#define SERVICE_OPTION_DO_NOT_PUBLISH_EIR   (0x00000008)

#define SERVICE_SECURITY_USE_DEFAULTS       (0x00000000)
#define SERVICE_SECURITY_NONE               (0x00000001)
#define SERVICE_SECURITY_AUTHORIZE          (0x00000002)
#define SERVICE_SECURITY_AUTHENTICATE       (0x00000004)
#define SERVICE_SECURITY_ENCRYPT_REQUIRED   (0x00000010)
#define SERVICE_SECURITY_ENCRYPT_OPTIONAL   (0x00000020)
#define SERVICE_SECURITY_DISABLED           (0x10000000)
#define SERVICE_SECURITY_NO_ASK             (0x20000000)

#define SDP_SEARCH_NO_PARSE_CHECK   (0x00000001)
#define SDP_SEARCH_NO_FORMAT_CHECK  (0x00000002)

#if !defined(HANDLE_SDP_TYPE)

typedef ULONGLONG HANDLE_SDP, *PHANDLE_SDP;

#define HANDLE_SDP_TYPE         HANDLE_SDP

#define HANDLE_SDP_FIELD_NAME   hConnection

#define HANDLE_SDP_NULL     ((HANDLE_SDP)0x0)

#endif

#define HANDLE_SDP_LOCAL    ((HANDLE_SDP) -2)

#pragma pack(push, 1)
typedef struct _BTH_SDP_CONNECT
{
    BTH_ADDR     bthAddress;
    ULONG       fSdpConnect;
    HANDLE_SDP_TYPE HANDLE_SDP_FIELD_NAME;
    UCHAR       requestTimeout;
} BTH_SDP_CONNECT,  *PBTH_SDP_CONNECT;
#pragma pack(pop)

#pragma pack(push, 1)
typedef struct _BTH_SDP_DISCONNECT
{
    HANDLE_SDP_TYPE HANDLE_SDP_FIELD_NAME;
} BTH_SDP_DISCONNECT, *PBTH_SDP_DISCONNECT;
#pragma pack(pop)

#pragma pack(push, 1)
typedef struct _BTH_SDP_RECORD
{
    ULONG fSecurity;
    ULONG fOptions;
    ULONG fCodService;
    ULONG recordLength;
    UCHAR record[1];
} BTH_SDP_RECORD, *PBTH_SDP_RECORD;
#pragma pack(pop)

#pragma pack(push, 1)
typedef struct _BTH_SDP_SERVICE_SEARCH_REQUEST
{
    HANDLE_SDP_TYPE HANDLE_SDP_FIELD_NAME;
    SdpQueryUuid uuids[MAX_UUIDS_IN_QUERY];
} BTH_SDP_SERVICE_SEARCH_REQUEST, *PBTH_SDP_SERVICE_SEARCH_REQUEST;
#pragma pack(pop)

#pragma pack(push, 1)
typedef struct _BTH_SDP_ATTRIBUTE_SEARCH_REQUEST
{
    HANDLE_SDP_TYPE HANDLE_SDP_FIELD_NAME;
    ULONG searchFlags;
    ULONG recordHandle;
    SdpAttributeRange range[1];
} BTH_SDP_ATTRIBUTE_SEARCH_REQUEST, *PBTH_SDP_ATTRIBUTE_SEARCH_REQUEST;
#pragma pack(pop)

#pragma pack(push, 1)
typedef struct _BTH_SDP_SERVICE_ATTRIBUTE_SEARCH_REQUEST
{
    HANDLE_SDP_TYPE HANDLE_SDP_FIELD_NAME;
    ULONG searchFlags;
    SdpQueryUuid uuids[MAX_UUIDS_IN_QUERY];
    SdpAttributeRange range[1];
} BTH_SDP_SERVICE_ATTRIBUTE_SEARCH_REQUEST,
  *PBTH_SDP_SERVICE_ATTRIBUTE_SEARCH_REQUEST;
#pragma pack(pop)

#pragma pack(push, 1)
typedef struct _BTH_SDP_STREAM_RESPONSE
{
    ULONG requiredSize;
    ULONG responseSize;
    UCHAR response[1];
} BTH_SDP_STREAM_RESPONSE, *PBTH_SDP_STREAM_RESPONSE;
#pragma pack(pop)

#pragma pack(push, 1)
typedef struct _BTH_COMMAND_HEADER
{
    USHORT OpCode;
    UCHAR TotalParameterLength;
} BTH_COMMAND_HEADER, *PBTH_COMMAND_HEADER;

typedef struct _BTH_VENDOR_SPECIFIC_COMMAND
{
    ULONG ManufacturerId;
    UCHAR LmpVersion;
    BOOLEAN MatchAnySinglePattern;
    BTH_COMMAND_HEADER HciHeader;
    UCHAR Data[1];
} BTH_VENDOR_SPECIFIC_COMMAND, *PBTH_VENDOR_SPECIFIC_COMMAND;

typedef struct _BTH_VENDOR_PATTERN
{
    UCHAR Offset;
    UCHAR Size;
    UCHAR Pattern[1];
} BTH_VENDOR_PATTERN, *PBTH_VENDOR_PATTERN;

typedef struct _BTH_VENDOR_EVENT_INFO
{
    BTH_ADDR BthAddress;
    ULONG EventSize;
    UCHAR EventInfo[1];
} BTH_VENDOR_EVENT_INFO, *PBTH_VENDOR_EVENT_INFO;
#pragma pack(pop)

#if ((NTDDI_VERSION >= NTDDI_WIN8))

#define BTH_HOST_FEATURE_ENHANCED_RETRANSMISSION_MODE          (0x0000000000000001)
#define BTH_HOST_FEATURE_STREAMING_MODE                        (0x0000000000000002)
#define BTH_HOST_FEATURE_LOW_ENERGY                            (0x0000000000000004)
#define BTH_HOST_FEATURE_SCO_HCI                               (0x0000000000000008)
#define BTH_HOST_FEATURE_SCO_HCIBYPASS                         (0x0000000000000010)

#pragma pack(push, 1)
typedef struct _BTH_HOST_FEATURE_MASK
{
    ULONGLONG Mask;
    ULONGLONG Reserved1;
    ULONGLONG Reserved2;
} BTH_HOST_FEATURE_MASK, *PBTH_HOST_FEATURE_MASK;
#pragma pack(pop)

#endif
#endif

#endif /* __BTHIOCTL_H__ */
