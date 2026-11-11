/*
 * PROJECT:     LiberNT Apple RTKit Library
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Port and link bring-up of the Apple silicon PCIe controller
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

#include <reactos/drivers/fdtdrv.h>
#include <reactos/drivers/appleadt.h>
#include <reactos/drivers/applepcielink.h>

#define APPLE_PCIE_MAX_PORTS 4
#define APPLE_PCIE_MSI_DOORBELL 0xFFFFF000UL

typedef
NTSTATUS
(NTAPI *PAPPLE_PCIE_SET_SMC_GPIO)(
    _In_opt_ PVOID Context,
    _In_ BOOLEAN LowBank,
    _In_ UCHAR Pin,
    _In_ BOOLEAN Value);

typedef struct _APPLE_PCIE_REGION
{
    PUCHAR Base;
    ULONG Size;
} APPLE_PCIE_REGION, *PAPPLE_PCIE_REGION;

typedef struct _APPLE_PCIE_PORT
{
    ULONG Node;
    ULONG Index;
    ULONG Bus;
    APPLE_PCIE_REGION Registers;
    APPLE_PCIE_REGION Phy;
    BOOLEAN WasUp;
    BOOLEAN LinkUp;
    ULONG Rid2SidEntries;
    ULONG Rid2SidUsed;
    ULONG LinkWaitMs;
} APPLE_PCIE_PORT, *PAPPLE_PCIE_PORT;

#define APPLE_PCIE_MAX_WINDOWS 4

typedef struct _APPLE_PCIE_WINDOW
{
    ULONG64 BusBase;
    ULONG Size;
    BOOLEAN Prefetchable;
} APPLE_PCIE_WINDOW, *PAPPLE_PCIE_WINDOW;

typedef struct _APPLE_PCIE
{
    ULONG Node;
    ULONG FirstBus;
    ULONG LastBus;
    ULONG WindowCount;
    APPLE_PCIE_WINDOW Windows[APPLE_PCIE_MAX_WINDOWS];
    ULONG64 ConfigBase;
    ULONG64 ConfigSize;
    ULONG MsiVectors;
    ULONG PinctrlNode;
    APPLE_PCIE_REGION Pinctrl;
    ULONG PortCount;
    APPLE_PCIE_PORT Ports[APPLE_PCIE_MAX_PORTS];
    PAPPLE_PCIE_SET_SMC_GPIO SetSmcGpio;
    PVOID SmcGpioContext;
} APPLE_PCIE, *PAPPLE_PCIE;

NTSTATUS
NTAPI
ApplePcieOpen(
    _In_ PFDT_DRIVER_TREE Tree,
    _In_ ULONG Node,
    _Out_ PAPPLE_PCIE Pcie);

VOID
NTAPI
ApplePcieClose(
    _Inout_ PAPPLE_PCIE Pcie);

NTSTATUS
NTAPI
ApplePcieStartPort(
    _In_ PFDT_DRIVER_TREE Tree,
    _Inout_ PAPPLE_PCIE Pcie,
    _Inout_ PAPPLE_PCIE_PORT Port);

BOOLEAN
NTAPI
ApplePcieFindStream(
    _In_ PFDT_DRIVER_TREE Tree,
    _In_ ULONG PcieNode,
    _In_ ULONG Rid,
    _Out_ PULONG Phandle,
    _Out_ PULONG Sid);

ULONG
NTAPI
ApplePcieMapPortDevices(
    _In_ PFDT_DRIVER_TREE Tree,
    _Inout_ PAPPLE_PCIE Pcie,
    _Inout_ PAPPLE_PCIE_PORT Port);

ULONG
NTAPI
ApplePcieReadConfig(
    _In_ PAPPLE_PCIE Pcie,
    _In_ ULONG Bus,
    _In_ ULONG Device,
    _In_ ULONG Function,
    _In_ ULONG Offset);

ULONG
NTAPI
ApplePcieFindAdtController(
    _In_ PAPPLE_ADT Adt,
    _In_ PAPPLE_PCIE Pcie);

VOID
NTAPI
ApplePcieConfigureLinkPower(
    _In_ PAPPLE_ADT Adt,
    _In_ ULONG Controller,
    _In_ PAPPLE_PCIE Pcie,
    _In_ PAPPLE_PCIE_PORT Port);
