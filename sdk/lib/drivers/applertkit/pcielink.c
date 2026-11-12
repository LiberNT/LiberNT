/*
 * PROJECT:     LiberNT Apple RTKit Library
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Link power states the platform and both ends of an Apple silicon PCIe link allow
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#ifndef UNIT_TEST
#include <ntddk.h>
#endif
#include <reactos/drivers/applepcielink.h>

#define LINKCAP_ASPM(Value)         (((Value) >> 10) & 3)
#define LINKCAP_L0S_EXIT(Value)     (((Value) >> 12) & 7)
#define LINKCAP_L1_EXIT(Value)      (((Value) >> 15) & 7)
#define DEVCAP_L0S_ACCEPT(Value)    (((Value) >> 6) & 7)
#define DEVCAP_L1_ACCEPT(Value)     (((Value) >> 9) & 7)
#define LATENCY_NO_LIMIT            7

BOOLEAN
NTAPI
ApplePcieComputeLinkPower(
    _In_ const APPLE_PCIE_LINK_STATE *State,
    _Out_ PAPPLE_PCIE_LINK_POWER Power)
{
    ULONG Index, Value, L0sExit, L1Exit, Capabilities;

    ASSERT(State->FunctionCount >= 1 && State->FunctionCount <= APPLE_PCIE_LINK_MAX_FUNCTIONS);
    RtlZeroMemory(Power, sizeof(*Power));

    Power->Supported = LINKCAP_ASPM(State->RootLinkCapabilities);
    L0sExit = LINKCAP_L0S_EXIT(State->RootLinkCapabilities);
    L1Exit = LINKCAP_L1_EXIT(State->RootLinkCapabilities);
    for (Index = 0; Index < State->FunctionCount; ++Index)
    {
        Value = State->DeviceLinkCapabilities[Index];
        Power->Supported &= LINKCAP_ASPM(Value);
        if (LINKCAP_L0S_EXIT(Value) > L0sExit)
            L0sExit = LINKCAP_L0S_EXIT(Value);
        if (LINKCAP_L1_EXIT(Value) > L1Exit)
            L1Exit = LINKCAP_L1_EXIT(Value);
    }

    Power->Aspm = State->PlatformAspm & Power->Supported;
    for (Index = 0; Index < State->FunctionCount; ++Index)
    {
        Value = State->DeviceCapabilities[Index];
        if (DEVCAP_L0S_ACCEPT(Value) != LATENCY_NO_LIMIT && L0sExit > DEVCAP_L0S_ACCEPT(Value))
            Power->Aspm &= ~APPLE_PCIE_ASPM_L0S;
        if (DEVCAP_L1_ACCEPT(Value) != LATENCY_NO_LIMIT && L1Exit > DEVCAP_L1_ACCEPT(Value))
            Power->Aspm &= ~APPLE_PCIE_ASPM_L1;
    }

    Power->Ltr = State->HavePlatformLatency && State->RootSupportsLtr && State->DeviceSupportsLtr;

    if (State->HavePlatformL1ss)
    {
        Capabilities = State->RootL1ssCapabilities & State->DeviceL1ssCapabilities;
        if (Capabilities & APPLE_PCIE_L1SS_SUPPORTED)
            Power->L1Substates = State->RootL1ssControl1 & State->DeviceL1ssControl1 & Capabilities & APPLE_PCIE_L1SS_ENABLES;
        if (!Power->Ltr)
            Power->L1Substates &= ~APPLE_PCIE_L1SS_ASPM_L1_2;
    }

    ASSERT(!(Power->Aspm & ~Power->Supported) && !(Power->Supported & ~APPLE_PCIE_ASPM_MASK));
    ASSERT(!(Power->L1Substates & ~APPLE_PCIE_L1SS_ENABLES));
    ASSERT(!(Power->L1Substates & ~State->RootL1ssCapabilities) && !(Power->L1Substates & ~State->DeviceL1ssCapabilities));
    return Power->Aspm != 0 || Power->L1Substates != 0 || Power->Ltr;
}
