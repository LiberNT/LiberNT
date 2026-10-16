/*
 * PROJECT:     LiberNT Bluetooth stack
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Internal interface between the Bluetooth enumerator and LiberNT profile drivers
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

#include <bthdef.h>
#ifndef CALLBACK
#define CALLBACK NTAPI
#endif
#include <bthledef.h>

#define IOCTL_INTERNAL_BTHMINI_GET_SERVICE_RECORD \
    CTL_CODE(FILE_DEVICE_BLUETOOTH, 0x800, METHOD_NEITHER, FILE_ANY_ACCESS)

#define IOCTL_INTERNAL_BTHMINI_GATT \
    CTL_CODE(FILE_DEVICE_BLUETOOTH, 0x801, METHOD_NEITHER, FILE_ANY_ACCESS)

typedef enum _BTHMINI_GATT_OPERATION
{
    BthMiniGattGetDeviceInfo,
    BthMiniGattGetCharacteristics,
    BthMiniGattGetDescriptors,
    BthMiniGattRead,
    BthMiniGattWrite,
    BthMiniGattWriteWithoutResponse,
    BthMiniGattSubscribe,
    BthMiniGattUnsubscribe
} BTHMINI_GATT_OPERATION;

typedef
VOID
NTAPI
BTHMINI_GATT_NOTIFY(
    _In_ PVOID Context,
    _In_ USHORT Handle,
    _In_reads_bytes_(Length) const UCHAR *Value,
    _In_ ULONG Length);
typedef BTHMINI_GATT_NOTIFY *PFN_BTHMINI_GATT_NOTIFY;

typedef struct _BTHMINI_GATT_DEVICE_INFO
{
    BTH_ADDR Address;
    BOOLEAN HasPnpId;
    UCHAR VendorIdSource;
    USHORT VendorId;
    USHORT ProductId;
    USHORT Version;
    CHAR Name[BTH_MAX_NAME_SIZE];
} BTHMINI_GATT_DEVICE_INFO, *PBTHMINI_GATT_DEVICE_INFO;

typedef struct _BTHMINI_GATT_DESCRIPTOR
{
    USHORT CharacteristicHandle;
    USHORT AttributeHandle;
    BTH_LE_UUID DescriptorUuid;
} BTHMINI_GATT_DESCRIPTOR, *PBTHMINI_GATT_DESCRIPTOR;

typedef struct _BTHMINI_GATT_REQUEST
{
    BTHMINI_GATT_OPERATION Operation;
    USHORT Handle;
    ULONG Length;
    PVOID Buffer;
    PFN_BTHMINI_GATT_NOTIFY Notify;
    PVOID NotifyContext;
} BTHMINI_GATT_REQUEST, *PBTHMINI_GATT_REQUEST;
