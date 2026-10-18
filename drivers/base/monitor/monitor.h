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
    MonitorWmiBrightness,
    MonitorWmiBrightnessMethods,
    MonitorWmiBrightnessEvent,
    MonitorWmiBlockCount
} MONITOR_WMI_BLOCK;

typedef struct _MONITOR_BRIGHTNESS *PMONITOR_BRIGHTNESS;

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
    BOOLEAN BrightnessEvents;
    PMONITOR_BRIGHTNESS Brightness;
} MONITOR_DEVICE_EXTENSION, *PMONITOR_DEVICE_EXTENSION;

extern UNICODE_STRING MonitorRegistryPath;

VOID
MonitorWmiRegister(
    _Inout_ PMONITOR_DEVICE_EXTENSION DeviceExtension);

VOID
MonitorWmiDeregister(
    _Inout_ PMONITOR_DEVICE_EXTENSION DeviceExtension);

NTSTATUS
MonitorQueryDescriptor(
    _In_ PMONITOR_DEVICE_EXTENSION DeviceExtension,
    _Outptr_ PRXGK_MONITOR_DESCRIPTOR *OutDescriptor);

VOID
MonitorWmiBrightnessChanged(
    _In_ PMONITOR_DEVICE_EXTENSION DeviceExtension,
    _In_ UCHAR Level);

VOID
MonitorBrightnessInitialize(VOID);

VOID
MonitorBrightnessStart(
    _Inout_ PMONITOR_DEVICE_EXTENSION DeviceExtension);

VOID
MonitorBrightnessStop(
    _Inout_ PMONITOR_DEVICE_EXTENSION DeviceExtension);

BOOLEAN
MonitorBrightnessSupported(
    _In_ PMONITOR_DEVICE_EXTENSION DeviceExtension);

NTSTATUS
MonitorBrightnessQuery(
    _In_ PMONITOR_DEVICE_EXTENSION DeviceExtension,
    _Out_ PUCHAR Current,
    _Out_writes_opt_(MaxLevels) PUCHAR Levels,
    _In_ ULONG MaxLevels,
    _Out_ PULONG LevelCount);

NTSTATUS
MonitorBrightnessSetCurrent(
    _In_ PMONITOR_DEVICE_EXTENSION DeviceExtension,
    _In_ UCHAR Level);

NTSTATUS
MonitorBrightnessRevert(
    _In_ PMONITOR_DEVICE_EXTENSION DeviceExtension);

NTSTATUS
MonitorBrightnessDeviceControl(
    _In_ PMONITOR_DEVICE_EXTENSION DeviceExtension,
    _Inout_ PIRP Irp);

NTSTATUS
MonitorLcdDispatch(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp);
