/*
 * PROJECT:     LiberNT Monitor Class Function Driver
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Shared definitions of the monitor function driver
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

#include <ntddk.h>
#include <wmistr.h>
#include <wmilib.h>
#include <reactos/rddm/rxgkmonitor.h>

#define MONITOR_TAG 'noMM'

typedef enum _MONITOR_WMI_BLOCK
{
    MonitorWmiId,
    MonitorWmiBasicDisplayParams,
    MonitorWmiConnectionParams,
    MonitorWmiAnalogVideoInputParams,
    MonitorWmiDigitalVideoInputParams,
    MonitorWmiColorCharacteristics,
    MonitorWmiListedSupportedSourceModes,
    MonitorWmiDescriptorMethods,
    MonitorWmiBlockCount
} MONITOR_WMI_BLOCK;

typedef struct _MONITOR_DEVICE_EXTENSION
{
    PDEVICE_OBJECT DeviceObject;
    PDEVICE_OBJECT PhysicalDevice;
    PDEVICE_OBJECT LowerDevice;
    IO_REMOVE_LOCK RemoveLock;
    WMILIB_CONTEXT WmiLibContext;
    WMIGUIDREGINFO GuidList[MonitorWmiBlockCount];
    MONITOR_WMI_BLOCK Blocks[MonitorWmiBlockCount];
    BOOLEAN WmiRegistered;
} MONITOR_DEVICE_EXTENSION, *PMONITOR_DEVICE_EXTENSION;

extern UNICODE_STRING MonitorRegistryPath;

VOID
MonitorWmiRegister(
    _Inout_ PMONITOR_DEVICE_EXTENSION DeviceExtension);

VOID
MonitorWmiDeregister(
    _Inout_ PMONITOR_DEVICE_EXTENSION DeviceExtension);
