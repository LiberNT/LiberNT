/*
 * PROJECT:     LiberNT Apple RTKit Library
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Power states and resets of Apple silicon PMGR device domains
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

#include <reactos/drivers/fdtdrv.h>

NTSTATUS
NTAPI
ApplePmgrPowerOnDevice(
    _In_ PFDT_DRIVER_TREE Tree,
    _In_ ULONG Node);

NTSTATUS
NTAPI
ApplePmgrResetDevice(
    _In_ PFDT_DRIVER_TREE Tree,
    _In_ ULONG Node);

NTSTATUS
NTAPI
ApplePmgrPowerOffDomains(
    _In_ PFDT_DRIVER_TREE Tree,
    _In_ ULONG Node,
    _In_ ULONG FirstIndex);

#define APPLE_PMGR_PS_ACTIVE 0xf

NTSTATUS
NTAPI
ApplePmgrRaiseMinimumState(
    _In_ PFDT_DRIVER_TREE Tree,
    _In_z_ PCSTR Label,
    _In_ ULONG State,
    _Out_opt_ PULONG Previous);
