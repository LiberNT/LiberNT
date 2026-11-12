/*
 * PROJECT:     LiberNT HAL
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Plug and Play devices described by the firmware device tree
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

#define HALP_FDT_MAX_DEVICES 192

typedef struct _HALP_FDT_DEVICE_NODE
{
    ULONG Node;
    ULONG Parent;
} HALP_FDT_DEVICE_NODE, *PHALP_FDT_DEVICE_NODE;

BOOLEAN
NTAPI
HalpFdtInitialize(VOID);

ULONG
NTAPI
HalpFdtEnumerateDevices(
    _Out_writes_(MaximumCount) PHALP_FDT_DEVICE_NODE Devices,
    _In_ ULONG MaximumCount);

NTSTATUS
NTAPI
HalpFdtQueryId(
    _In_ ULONG Node,
    _In_ ULONG Parent,
    _In_ BUS_QUERY_ID_TYPE IdType,
    _Out_ PWCHAR *Id);

NTSTATUS
NTAPI
HalpFdtQueryDeviceText(
    _In_ ULONG Node,
    _Out_ PWCHAR *Text);

NTSTATUS
NTAPI
HalpFdtQueryResources(
    _In_ ULONG Node,
    _In_ ULONG Parent,
    _Out_ PCM_RESOURCE_LIST *Resources);

NTSTATUS
NTAPI
HalpFdtQueryRequirements(
    _In_ ULONG Node,
    _In_ ULONG Parent,
    _Out_ PIO_RESOURCE_REQUIREMENTS_LIST *Requirements);

VOID
NTAPI
HalpFdtPublishProperties(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ ULONG Node,
    _In_ ULONG Parent);
