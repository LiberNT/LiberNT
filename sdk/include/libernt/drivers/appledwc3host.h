/*
 * PROJECT:     LiberNT Apple RTKit Library
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Host start of an Apple silicon Type-C DWC3 controller
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

#include <libernt/drivers/fdtdrv.h>

#define APPLE_DWC3_REGISTER_SPAN    0xC800

NTSTATUS
NTAPI
AppleDwc3StartHost(
    _In_ PFDT_DRIVER_TREE Tree,
    _In_ ULONG Node,
    _In_ ULONG64 CoreBase,
    _In_z_ PCSTR Owner);
