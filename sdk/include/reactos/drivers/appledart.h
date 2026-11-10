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

typedef struct _APPLE_DART_ADOPTED
{
    PUCHAR Base;
    ULONG BaseSize;
    ULONG Sid;
    ULONG Levels;
    ULONG InputBits;
    ULONG64 TopPhysical;
    ULONG64 NextIova;
    ULONG64 EndIova;
    PCSTR Owner;
    ULONG OwnTables;
    ULONG64 OwnPhysical[16];
    PULONG64 OwnVirtual[16];
    ULONG ForeignTables;
    ULONG64 ForeignPhysical[16];
    PULONG64 ForeignVirtual[16];
} APPLE_DART_ADOPTED, *PAPPLE_DART_ADOPTED;

NTSTATUS
NTAPI
AppleDartAdopt(
    _In_ PFDT_DRIVER_TREE Tree,
    _In_ ULONG Dart,
    _In_ ULONG Sid,
    _In_ ULONG64 WindowBase,
    _In_ ULONG64 WindowEnd,
    _In_z_ PCSTR Owner,
    _Out_ PAPPLE_DART_ADOPTED Adopted);

BOOLEAN
NTAPI
AppleDartAdoptedTranslate(
    _Inout_ PAPPLE_DART_ADOPTED Adopted,
    _In_ ULONG64 Iova,
    _Out_ PULONG64 Physical);

BOOLEAN
NTAPI
AppleDartAdoptedRangeFree(
    _Inout_ PAPPLE_DART_ADOPTED Adopted,
    _In_ ULONG64 Iova,
    _In_ SIZE_T Length);

NTSTATUS
NTAPI
AppleDartAdoptedMapAt(
    _Inout_ PAPPLE_DART_ADOPTED Adopted,
    _In_ ULONG64 Iova,
    _In_ ULONG64 Physical,
    _In_ SIZE_T Length);

NTSTATUS
NTAPI
AppleDartAdoptedMap(
    _Inout_ PAPPLE_DART_ADOPTED Adopted,
    _In_ ULONG64 Physical,
    _In_ SIZE_T Length,
    _Out_ PULONG64 Iova);

VOID
NTAPI
AppleDartAdoptedRelease(
    _Inout_ PAPPLE_DART_ADOPTED Adopted);
