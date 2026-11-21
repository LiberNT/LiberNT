/*
 * PROJECT:     LiberNT Bluetooth stack
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Private radio interface behind BluetoothSetLocalServiceInfo
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

#define IOCTL_BTHMINI_SET_LOCAL_SERVICE CTL_CODE(FILE_DEVICE_BLUETOOTH, 0xb0, METHOD_BUFFERED, FILE_ANY_ACCESS)

#define BTHMINI_LOCAL_SERVICE_NAME_SIZE 256
#define BTHMINI_MAX_LOCAL_SERVICES      100

typedef struct _BTHMINI_LOCAL_SERVICE
{
    GUID ServiceClass;
    ULONG Instance;
    ULONG Enabled;
    ULONGLONG Address;
    WCHAR Name[BTHMINI_LOCAL_SERVICE_NAME_SIZE];
    WCHAR DeviceString[BTHMINI_LOCAL_SERVICE_NAME_SIZE];
} BTHMINI_LOCAL_SERVICE, *PBTHMINI_LOCAL_SERVICE;
