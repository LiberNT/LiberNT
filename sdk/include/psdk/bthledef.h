/*
 * Copyright 2025 Vibhav Pant
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
 */
#ifndef __BTHLEDEFS_H
#define __BTHLEDEFS_H

#ifdef __cplusplus
extern "C" {
#endif

#define BTH_LE_SERVICE_GAP  0x1800
#define BTH_LE_SERVICE_GATT 0x1801

#define BTH_LE_GATT_ATTRIBUTE_TYPE_PRIMARY_SERVICE   0x2800
#define BTH_LE_GATT_ATTRIBUTE_TYPE_SECONDARY_SERVICE 0x2801
#define BTH_LE_GATT_ATTRIBUTE_TYPE_INCLUDE           0x2802
#define BTH_LE_GATT_ATTRIBUTE_TYPE_CHARACTERISTIC    0x2803

#define BTH_LE_GATT_CHARACTERISTIC_DESCRIPTOR_EXTENDED_PROPERTIES  0x2900
#define BTH_LE_GATT_CHARACTERISTIC_DESCRIPTOR_USER_DESCRIPTION     0x2901
#define BTH_LE_GATT_CHARACTERISTIC_DESCRIPTOR_CLIENT_CONFIGURATION 0x2902
#define BTH_LE_GATT_CHARACTERISTIC_DESCRIPTOR_SERVER_CONFIGURATION 0x2903
#define BTH_LE_GATT_CHARACTERISTIC_DESCRIPTOR_FORMAT               0x2904
#define BTH_LE_GATT_CHARACTERISTIC_DESCRIPTOR_AGGREGATE_FORMAT     0x2905

#define BTH_LE_GATT_CHARACTERISTIC_TYPE_DEVICE_NAME                0x2A00
#define BTH_LE_GATT_CHARACTERISTIC_TYPE_APPEARANCE                 0x2A01
#define BTH_LE_GATT_CHARACTERISTIC_TYPE_PERIPHERAL_PRIVACY_FLAG    0x2A02
#define BTH_LE_GATT_CHARACTERISTIC_TYPE_RECONNECTION_ADDRESS       0x2A03
#define BTH_LE_GATT_CHARACTERISTIC_TYPE_PERIPHERAL_PREFERED_CONNECTION_PARAMETER 0x2A04
#define BTH_LE_GATT_CHARACTERISTIC_TYPE_SERVICE_CHANGED            0x2A05

#define BTH_LE_GATT_DEFAULT_MAX_INCLUDED_SERVICES_DEPTH 3
#define BTH_LE_ATT_TRANSACTION_TIMEOUT                  30
#define BTH_LE_ATT_MAX_VALUE_SIZE                       512
#define BTH_LE_ATT_CID                                  0x0004
#define BTHLEENUM_ATT_MTU_MIN                           23
#define BTHLEENUM_ATT_MTU_MAX                           0xFFFF
#define BTHLEENUM_ATT_MTU_DEFAULT                       BTHLEENUM_ATT_MTU_MIN
#define BTHLEENUM_ATT_MTU_INITIAL_NEGOTIATION           525

#define BTH_LE_ERROR_INVALID_HANDLE                   0x01
#define BTH_LE_ERROR_READ_NOT_PERMITTED               0x02
#define BTH_LE_ERROR_WRITE_NOT_PERMITTED              0x03
#define BTH_LE_ERROR_INVALID_PDU                      0x04
#define BTH_LE_ERROR_INSUFFICIENT_AUTHENTICATION      0x05
#define BTH_LE_ERROR_REQUEST_NOT_SUPPORTED            0x06
#define BTH_LE_ERROR_INVALID_OFFSET                   0x07
#define BTH_LE_ERROR_INSUFFICIENT_AUTHORIZATION       0x08
#define BTH_LE_ERROR_PREPARE_QUEUE_FULL               0x09
#define BTH_LE_ERROR_ATTRIBUTE_NOT_FOUND              0x0A
#define BTH_LE_ERROR_ATTRIBUTE_NOT_LONG               0x0B
#define BTH_LE_ERROR_INSUFFICIENT_ENCRYPTION_KEY_SIZE 0x0C
#define BTH_LE_ERROR_INVALID_ATTRIBUTE_VALUE_LENGTH   0x0D
#define BTH_LE_ERROR_UNLIKELY                         0x0E
#define BTH_LE_ERROR_INSUFFICIENT_ENCRYPTION          0x0F
#define BTH_LE_ERROR_UNSUPPORTED_GROUP_TYPE           0x10
#define BTH_LE_ERROR_INSUFFICIENT_RESOURCES           0x11
#define BTH_LE_ERROR_UNKNOWN                          0x1000

#define BLUETOOTH_GATT_FLAG_NONE                     0x00000000
#define BLUETOOTH_GATT_FLAG_CONNECTION_ENCRYPTED     0x00000001
#define BLUETOOTH_GATT_FLAG_CONNECTION_AUTHENTICATED 0x00000002
#define BLUETOOTH_GATT_FLAG_FORCE_READ_FROM_DEVICE   0x00000004
#define BLUETOOTH_GATT_FLAG_FORCE_READ_FROM_CACHE    0x00000008
#define BLUETOOTH_GATT_FLAG_SIGNED_WRITE             0x00000010
#define BLUETOOTH_GATT_FLAG_WRITE_WITHOUT_RESPONSE   0x00000020
#define BLUETOOTH_GATT_FLAG_RETURN_ALL               0x00000040

#define BLUETOOTH_GATT_FLAG_VALID_MASK \
    (BLUETOOTH_GATT_FLAG_NONE | BLUETOOTH_GATT_FLAG_CONNECTION_ENCRYPTED | \
     BLUETOOTH_GATT_FLAG_CONNECTION_AUTHENTICATED | BLUETOOTH_GATT_FLAG_FORCE_READ_FROM_DEVICE | \
     BLUETOOTH_GATT_FLAG_FORCE_READ_FROM_CACHE | BLUETOOTH_GATT_FLAG_SIGNED_WRITE | \
     BLUETOOTH_GATT_FLAG_WRITE_WITHOUT_RESPONSE | BLUETOOTH_GATT_FLAG_RETURN_ALL)

#define IS_BLUETOOTH_GATT_FLAG_VALID(f) (0 == (f & ~BLUETOOTH_GATT_FLAG_VALID_MASK))

typedef HANDLE BLUETOOTH_GATT_EVENT_HANDLE;

typedef struct _BTH_LE_UUID
{
    BOOLEAN IsShortUuid;
    union
    {
        USHORT ShortUuid;
        GUID LongUuid;
    } Value;
} BTH_LE_UUID, *PBTH_LE_UUID;

typedef struct _BTH_LE_GATT_SERVICE
{
    BTH_LE_UUID ServiceUuid;
    USHORT AttributeHandle;
} BTH_LE_GATT_SERVICE, *PBTH_LE_GATT_SERVICE;

typedef struct _BTH_LE_GATT_CHARACTERISTIC
{
    USHORT ServiceHandle;
    BTH_LE_UUID CharacteristicUuid;
    USHORT AttributeHandle;
    USHORT CharacteristicValueHandle;
    BOOLEAN IsBroadcastable;
    BOOLEAN IsReadable;
    BOOLEAN IsWritable;
    BOOLEAN IsWritableWithoutResponse;
    BOOLEAN IsSignedWritable;
    BOOLEAN IsNotifiable;
    BOOLEAN IsIndicatable;
    BOOLEAN HasExtendedProperties;
} BTH_LE_GATT_CHARACTERISTIC, *PBTH_LE_GATT_CHARACTERISTIC;

typedef struct _BTH_LE_GATT_CHARACTERISTIC_VALUE
{
    ULONG DataSize;
    UCHAR Data[1];
} BTH_LE_GATT_CHARACTERISTIC_VALUE, *PBTH_LE_GATT_CHARACTERISTIC_VALUE;

typedef enum _BTH_LE_GATT_DESCRIPTOR_TYPE
{
    CharacteristicExtendedProperties,
    CharacteristicUserDescription,
    ClientCharacteristicConfiguration,
    ServerCharacteristicConfiguration,
    CharacteristicFormat,
    CharacteristicAggregateFormat,
    CustomDescriptor
} BTH_LE_GATT_DESCRIPTOR_TYPE, *PBTH_LE_GATT_DESCRIPTOR_TYPE;

typedef struct _BTH_LE_GATT_DESCRIPTOR
{
    USHORT ServiceHandle;
    USHORT CharacteristicHandle;
    BTH_LE_GATT_DESCRIPTOR_TYPE DescriptorType;
    BTH_LE_UUID DescriptorUuid;
    USHORT AttributeHandle;
} BTH_LE_GATT_DESCRIPTOR, *PBTH_LE_GATT_DESCRIPTOR;

typedef struct _BTH_LE_GATT_DESCRIPTOR_VALUE
{
    BTH_LE_GATT_DESCRIPTOR_TYPE DescriptorType;
    BTH_LE_UUID DescriptorUuid;
    union
    {
        struct
        {
            BOOLEAN IsReliableWriteEnabled;
            BOOLEAN IsAuxiliariesWritable;
        } CharacteristicExtendedProperties;
        struct
        {
            BOOLEAN IsSubscribeToNotification;
            BOOLEAN IsSubscribeToIndication;
        } ClientCharacteristicConfiguration;
        struct
        {
            BOOLEAN IsBroadcast;
        } ServerCharacteristicConfiguration;
        struct
        {
            UCHAR Format;
            UCHAR Exponent;
            BTH_LE_UUID Unit;
            UCHAR NameSpace;
            BTH_LE_UUID Description;
        } CharacteristicFormat;
    } DUMMYUNIONNAME;
    ULONG DataSize;
    UCHAR Data[1];
} BTH_LE_GATT_DESCRIPTOR_VALUE, *PBTH_LE_GATT_DESCRIPTOR_VALUE;

typedef enum _BTH_LE_GATT_EVENT_TYPE
{
    CharacteristicValueChangedEvent,
} BTH_LE_GATT_EVENT_TYPE;

typedef VOID (CALLBACK *PFNBLUETOOTH_GATT_EVENT_CALLBACK)(BTH_LE_GATT_EVENT_TYPE EventType, PVOID EventOutParameter,
                                                          PVOID Context);

typedef struct _BLUETOOTH_GATT_VALUE_CHANGED_EVENT_REGISTRATION
{
    USHORT NumCharacteristics;
    BTH_LE_GATT_CHARACTERISTIC Characteristics[1];
} BLUETOOTH_GATT_VALUE_CHANGED_EVENT_REGISTRATION, *PBLUETOOTH_GATT_VALUE_CHANGED_EVENT_REGISTRATION;

typedef struct _BLUETOOTH_GATT_VALUE_CHANGED_EVENT
{
    USHORT ChangedAttributeHandle;
    size_t CharacteristicValueDataSize;
    PBTH_LE_GATT_CHARACTERISTIC_VALUE CharacteristicValue;
} BLUETOOTH_GATT_VALUE_CHANGED_EVENT, *PBLUETOOTH_GATT_VALUE_CHANGED_EVENT;

typedef ULONG64 BTH_LE_GATT_RELIABLE_WRITE_CONTEXT, *PBTH_LE_GATT_RELIABLE_WRITE_CONTEXT;

DEFINE_GUID( GUID_BLUETOOTHLE_DEVICE_INTERFACE, 0x781aee18, 0x7733, 0x4ce4, 0xad, 0xd0, 0x91, 0xf4, 0x1c, 0x67, 0xb5, 0x92 );
DEFINE_GUID( GUID_BLUETOOTH_GATT_SERVICE_DEVICE_INTERFACE, 0x6e3bb679, 0x4372, 0x40c8, 0x9e, 0xaa, 0x45, 0x09, 0xdf, 0x26, 0x0c, 0xd8 );
DEFINE_GUID( BTH_LE_ATT_BLUETOOTH_BASE_GUID, 0, 0, 0x1000, 0x80, 0x00, 0x00, 0x80, 0x5f, 0x9b, 0x34, 0xfb );

FORCEINLINE BOOLEAN IsBthLEUuidMatch( BTH_LE_UUID u1, BTH_LE_UUID u2 )
{
    GUID full1, full2;

    if (u1.IsShortUuid && u2.IsShortUuid)
        return u1.Value.ShortUuid == u2.Value.ShortUuid;

    if (u1.IsShortUuid)
    {
        full1 = BTH_LE_ATT_BLUETOOTH_BASE_GUID;
        full1.Data1 = u1.Value.ShortUuid;
    }
    else
        full1 = u1.Value.LongUuid;
    if (u2.IsShortUuid)
    {
        full2 = BTH_LE_ATT_BLUETOOTH_BASE_GUID;
        full2.Data1 = u2.Value.ShortUuid;
    }
    else
        full2 = u2.Value.LongUuid;
    return !memcmp( &full1, &full2, sizeof( full1 ) );
}

#ifdef __cplusplus
}
#endif
#endif
