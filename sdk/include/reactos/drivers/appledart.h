/*
 * PROJECT:     LiberNT Apple RTKit Library
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Stream setup of Apple silicon DART IOMMUs
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

#include <reactos/drivers/fdtdrv.h>

NTSTATUS
NTAPI
AppleDartMapPhysical(
    _In_ PFDT_DRIVER_TREE Tree,
    _In_ ULONG Node,
    _In_z_ PCSTR Owner);

NTSTATUS
NTAPI
AppleDartMapStream(
    _In_ PFDT_DRIVER_TREE Tree,
    _In_ ULONG Dart,
    _In_ ULONG Sid,
    _In_z_ PCSTR Owner);

NTSTATUS
NTAPI
AppleDartMapLow(
    _In_ PFDT_DRIVER_TREE Tree,
    _In_ ULONG Dart,
    _In_ ULONG Sid,
    _In_ ULONG64 Physical,
    _In_ SIZE_T Length,
    _Out_ PULONG64 Iova,
    _In_z_ PCSTR Owner);

NTSTATUS
NTAPI
AppleDartUnmapLow(
    _In_ PFDT_DRIVER_TREE Tree,
    _In_ ULONG Dart,
    _In_ ULONG Sid,
    _In_ ULONG64 Iova,
    _In_ SIZE_T Length,
    _In_z_ PCSTR Owner);
