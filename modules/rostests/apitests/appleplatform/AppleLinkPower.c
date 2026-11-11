/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Unit tests for the Apple silicon PCIe link power decision
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <apitest.h>

#define WIN32_NO_STATUS
#include <ndk/ntndk.h>

#define UNIT_TEST

#include "../../../../sdk/lib/drivers/applertkit/pcielink.c"

#define M3_ROOT_LINK_CAPS       0x00737814UL
#define M3_WLAN_LINK_CAPS       0x0046f812UL
#define M3_BT_LINK_CAPS         0x0046d812UL
#define M3_WLAN_DEVICE_CAPS     0x10008f82UL
#define M3_BT_DEVICE_CAPS       0x10008f82UL
#define M3_SD_LINK_CAPS         0x5547ec12UL
#define M3_SD_DEVICE_CAPS       0x00008fa1UL
#define M3_ROOT_L1SS_CONTROL1   0x4055190fUL
#define M3_WLAN_L1SS_CONTROL1   0x4055000fUL
#define ALL_SUBSTATES           (APPLE_PCIE_L1SS_SUPPORTED | APPLE_PCIE_L1SS_ENABLES)

static
VOID
WifiLink(
    _Out_ PAPPLE_PCIE_LINK_STATE State)
{
    RtlZeroMemory(State, sizeof(*State));
    State->PlatformAspm = APPLE_PCIE_ASPM_MASK & 2 & 2;
    State->HavePlatformL1ss = TRUE;
    State->RootL1ssControl1 = M3_ROOT_L1SS_CONTROL1;
    State->DeviceL1ssControl1 = M3_WLAN_L1SS_CONTROL1;
    State->HavePlatformLatency = TRUE;
    State->RootLinkCapabilities = M3_ROOT_LINK_CAPS;
    State->RootL1ssCapabilities = ALL_SUBSTATES;
    State->RootSupportsLtr = TRUE;
    State->FunctionCount = 2;
    State->DeviceLinkCapabilities[0] = M3_WLAN_LINK_CAPS;
    State->DeviceCapabilities[0] = M3_WLAN_DEVICE_CAPS;
    State->DeviceLinkCapabilities[1] = M3_BT_LINK_CAPS;
    State->DeviceCapabilities[1] = M3_BT_DEVICE_CAPS;
    State->DeviceL1ssCapabilities = ALL_SUBSTATES;
    State->DeviceSupportsLtr = TRUE;
}

static
VOID
SdReaderLink(
    _Out_ PAPPLE_PCIE_LINK_STATE State)
{
    RtlZeroMemory(State, sizeof(*State));
    State->PlatformAspm = APPLE_PCIE_ASPM_MASK & 0;
    State->RootLinkCapabilities = M3_ROOT_LINK_CAPS;
    State->RootL1ssCapabilities = ALL_SUBSTATES;
    State->RootSupportsLtr = TRUE;
    State->FunctionCount = 1;
    State->DeviceLinkCapabilities[0] = M3_SD_LINK_CAPS;
    State->DeviceCapabilities[0] = M3_SD_DEVICE_CAPS;
    State->DeviceL1ssCapabilities = ALL_SUBSTATES;
    State->DeviceSupportsLtr = TRUE;
}

static
VOID
CheckPower(
    _In_ const APPLE_PCIE_LINK_STATE *State,
    _In_ BOOLEAN Enables,
    _In_ ULONG Supported,
    _In_ ULONG Aspm,
    _In_ ULONG L1Substates,
    _In_ BOOLEAN Ltr,
    _In_ const char *Name)
{
    APPLE_PCIE_LINK_POWER Power;
    BOOLEAN Result;

    memset(&Power, 0xcc, sizeof(Power));
    Result = ApplePcieComputeLinkPower(State, &Power);
    ok(Result == Enables, "%s: the link %s something to enable\n", Name, Result ? "has" : "does not have");
    ok(Power.Supported == Supported, "%s: both ends support ASPM %lx, expected %lx\n", Name,
       (unsigned long)Power.Supported, (unsigned long)Supported);
    ok(Power.Aspm == Aspm, "%s: ASPM %lx, expected %lx\n", Name, (unsigned long)Power.Aspm, (unsigned long)Aspm);
    ok(Power.L1Substates == L1Substates, "%s: L1 substates %lx, expected %lx\n", Name,
       (unsigned long)Power.L1Substates, (unsigned long)L1Substates);
    ok(Power.Ltr == Ltr, "%s: LTR %u, expected %u\n", Name, Power.Ltr, Ltr);
}

START_TEST(AppleLinkPower)
{
    APPLE_PCIE_LINK_STATE State;

    WifiLink(&State);
    CheckPower(&State, TRUE, APPLE_PCIE_ASPM_L1, APPLE_PCIE_ASPM_L1, APPLE_PCIE_L1SS_ENABLES, TRUE,
               "M3 Pro Wi-Fi and Bluetooth link");

    WifiLink(&State);
    State.FunctionCount = 1;
    CheckPower(&State, TRUE, APPLE_PCIE_ASPM_L1, APPLE_PCIE_ASPM_L1, APPLE_PCIE_L1SS_ENABLES, TRUE, "Wi-Fi function alone");

    WifiLink(&State);
    State.RootSupportsLtr = FALSE;
    CheckPower(&State, TRUE, APPLE_PCIE_ASPM_L1, APPLE_PCIE_ASPM_L1,
               APPLE_PCIE_L1SS_ENABLES & ~APPLE_PCIE_L1SS_ASPM_L1_2, FALSE, "root port without LTR");

    WifiLink(&State);
    State.DeviceSupportsLtr = FALSE;
    CheckPower(&State, TRUE, APPLE_PCIE_ASPM_L1, APPLE_PCIE_ASPM_L1,
               APPLE_PCIE_L1SS_ENABLES & ~APPLE_PCIE_L1SS_ASPM_L1_2, FALSE, "device without LTR");

    WifiLink(&State);
    State.HavePlatformLatency = FALSE;
    CheckPower(&State, TRUE, APPLE_PCIE_ASPM_L1, APPLE_PCIE_ASPM_L1,
               APPLE_PCIE_L1SS_ENABLES & ~APPLE_PCIE_L1SS_ASPM_L1_2, FALSE, "no platform latency");

    WifiLink(&State);
    State.DeviceL1ssCapabilities = 0;
    CheckPower(&State, TRUE, APPLE_PCIE_ASPM_L1, APPLE_PCIE_ASPM_L1, 0, TRUE, "device without L1 substates");

    WifiLink(&State);
    State.RootL1ssCapabilities = APPLE_PCIE_L1SS_ENABLES;
    CheckPower(&State, TRUE, APPLE_PCIE_ASPM_L1, APPLE_PCIE_ASPM_L1, 0, TRUE, "root port without the substates bit");

    WifiLink(&State);
    State.DeviceL1ssCapabilities = APPLE_PCIE_L1SS_SUPPORTED | APPLE_PCIE_L1SS_PCIPM_L1_1 | APPLE_PCIE_L1SS_ASPM_L1_1;
    CheckPower(&State, TRUE, APPLE_PCIE_ASPM_L1, APPLE_PCIE_ASPM_L1,
               APPLE_PCIE_L1SS_PCIPM_L1_1 | APPLE_PCIE_L1SS_ASPM_L1_1, TRUE, "device with L1.1 only");

    WifiLink(&State);
    State.HavePlatformL1ss = FALSE;
    CheckPower(&State, TRUE, APPLE_PCIE_ASPM_L1, APPLE_PCIE_ASPM_L1, 0, TRUE, "no platform L1 substates");

    WifiLink(&State);
    State.DeviceL1ssControl1 = M3_WLAN_L1SS_CONTROL1 & ~APPLE_PCIE_L1SS_PCIPM_L1_2;
    CheckPower(&State, TRUE, APPLE_PCIE_ASPM_L1, APPLE_PCIE_ASPM_L1,
               APPLE_PCIE_L1SS_ENABLES & ~APPLE_PCIE_L1SS_PCIPM_L1_2, TRUE, "platform without PCI-PM L1.2 on one end");

    WifiLink(&State);
    State.DeviceCapabilities[1] = (M3_BT_DEVICE_CAPS & ~(7UL << 9)) | (4UL << 9);
    CheckPower(&State, TRUE, APPLE_PCIE_ASPM_L1, 0, APPLE_PCIE_L1SS_ENABLES, TRUE, "function that cannot wait for L1 exit");

    WifiLink(&State);
    State.DeviceCapabilities[1] = (M3_BT_DEVICE_CAPS & ~(7UL << 9)) | (6UL << 9);
    CheckPower(&State, TRUE, APPLE_PCIE_ASPM_L1, APPLE_PCIE_ASPM_L1, APPLE_PCIE_L1SS_ENABLES, TRUE,
               "function that waits exactly the L1 exit latency");

    WifiLink(&State);
    State.DeviceLinkCapabilities[1] = (M3_BT_LINK_CAPS & ~(3UL << 10)) | (1UL << 10);
    CheckPower(&State, TRUE, 0, 0, APPLE_PCIE_L1SS_ENABLES, TRUE, "function with L0s only");

    WifiLink(&State);
    State.PlatformAspm = APPLE_PCIE_ASPM_MASK & 2 & 0;
    CheckPower(&State, TRUE, APPLE_PCIE_ASPM_L1, 0, APPLE_PCIE_L1SS_ENABLES, TRUE, "one function the platform keeps out of L1");

    SdReaderLink(&State);
    CheckPower(&State, FALSE, APPLE_PCIE_ASPM_L1, 0, 0, FALSE, "M3 Pro SD card reader link");

    SdReaderLink(&State);
    State.PlatformAspm = APPLE_PCIE_ASPM_MASK;
    CheckPower(&State, TRUE, APPLE_PCIE_ASPM_L1, APPLE_PCIE_ASPM_L1, 0, FALSE, "card reader link asked for L0s and L1");

    SdReaderLink(&State);
    State.PlatformAspm = APPLE_PCIE_ASPM_MASK;
    State.RootLinkCapabilities = M3_ROOT_LINK_CAPS | (APPLE_PCIE_ASPM_L0S << 10);
    CheckPower(&State, TRUE, APPLE_PCIE_ASPM_MASK, APPLE_PCIE_ASPM_L1, 0, FALSE,
               "L0s exit longer than the device accepts");

    SdReaderLink(&State);
    State.PlatformAspm = APPLE_PCIE_ASPM_MASK;
    State.RootLinkCapabilities = (M3_ROOT_LINK_CAPS | (APPLE_PCIE_ASPM_L0S << 10)) & ~(7UL << 12);
    State.DeviceLinkCapabilities[0] = M3_SD_LINK_CAPS & ~(7UL << 12);
    CheckPower(&State, TRUE, APPLE_PCIE_ASPM_MASK, APPLE_PCIE_ASPM_MASK, 0, FALSE, "L0s exit the device accepts");

    RtlZeroMemory(&State, sizeof(State));
    State.FunctionCount = 1;
    CheckPower(&State, FALSE, 0, 0, 0, FALSE, "link with no capabilities and no platform data");
}
