/*
 * PROJECT:     LiberNT Apple RTKit Library
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Link power states the platform and both ends of an Apple silicon PCIe link allow
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

#define APPLE_PCIE_LINK_MAX_FUNCTIONS   8

#define APPLE_PCIE_ASPM_L0S             1UL
#define APPLE_PCIE_ASPM_L1              2UL
#define APPLE_PCIE_ASPM_MASK            3UL

#define APPLE_PCIE_L1SS_PCIPM_L1_2      (1UL << 0)
#define APPLE_PCIE_L1SS_PCIPM_L1_1      (1UL << 1)
#define APPLE_PCIE_L1SS_ASPM_L1_2       (1UL << 2)
#define APPLE_PCIE_L1SS_ASPM_L1_1       (1UL << 3)
#define APPLE_PCIE_L1SS_ENABLES         0x0FUL
#define APPLE_PCIE_L1SS_SUPPORTED       (1UL << 4)

typedef struct _APPLE_PCIE_LINK_STATE
{
    ULONG PlatformAspm;
    BOOLEAN HavePlatformL1ss;
    ULONG RootL1ssControl1;
    ULONG DeviceL1ssControl1;
    BOOLEAN HavePlatformLatency;
    ULONG RootLinkCapabilities;
    ULONG RootL1ssCapabilities;
    BOOLEAN RootSupportsLtr;
    ULONG FunctionCount;
    ULONG DeviceLinkCapabilities[APPLE_PCIE_LINK_MAX_FUNCTIONS];
    ULONG DeviceCapabilities[APPLE_PCIE_LINK_MAX_FUNCTIONS];
    ULONG DeviceL1ssCapabilities;
    BOOLEAN DeviceSupportsLtr;
} APPLE_PCIE_LINK_STATE, *PAPPLE_PCIE_LINK_STATE;

typedef struct _APPLE_PCIE_LINK_POWER
{
    ULONG Supported;
    ULONG Aspm;
    ULONG L1Substates;
    BOOLEAN Ltr;
} APPLE_PCIE_LINK_POWER, *PAPPLE_PCIE_LINK_POWER;

BOOLEAN
NTAPI
ApplePcieComputeLinkPower(
    _In_ const APPLE_PCIE_LINK_STATE *State,
    _Out_ PAPPLE_PCIE_LINK_POWER Power);
