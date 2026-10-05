/*
 * PROJECT:     LiberNT Resource Hub private interface
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Connection registration shared by firmware enumerators
 */

#pragma once

#include <reshub.h>

DEFINE_GUID(GUID_DEVINTERFACE_RESOURCE_HUB_CONTROLLER, 0xbeec5086, 0xd20f, 0x4e20, 0x96, 0x8e, 0x80, 0x94, 0xa8, 0x77, 0x7e, 0x04);

#define IOCTL_RH_REGISTER_CONNECTION CTL_CODE(FILE_DEVICE_RESOURCE_HUB, 0x800, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define RH_REGISTER_CONNECTION_VERSION 2
#define RH_SECONDARY_INTERRUPT_CONNECTION_ID(Gsiv) (0x8000000100000000ULL | (ULONGLONG)(ULONG)(Gsiv))

typedef struct _RH_REGISTER_CONNECTION_INPUT
{
    ULONG Version;
    LARGE_INTEGER ConnectionId;
    UCHAR Class;
    UCHAR Type;
    USHORT Reserved;
    PVOID ControllerDevice;
    ULONG PropertiesLength;
    UCHAR Properties[ANYSIZE_ARRAY];
} RH_REGISTER_CONNECTION_INPUT, *PRH_REGISTER_CONNECTION_INPUT;
