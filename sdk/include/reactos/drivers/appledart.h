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

#define APPLE_DART_PAGE_SIZE        0x4000ULL
#define APPLE_DART_DOMAIN_STREAMS   4

typedef struct _APPLE_DART_DOMAIN_STREAM
{
    PUCHAR Base;
    ULONG BaseSize;
    ULONG Sid;
    ULONG64 Physical;
} APPLE_DART_DOMAIN_STREAM, *PAPPLE_DART_DOMAIN_STREAM;

typedef struct _APPLE_DART_DOMAIN
{
    PCSTR Owner;
    PVOID Tables;
    ULONG InputBits;
    ULONG Levels;
    ULONG64 TopPhysical;
    ULONG Streams;
    APPLE_DART_DOMAIN_STREAM Stream[APPLE_DART_DOMAIN_STREAMS];
} APPLE_DART_DOMAIN, *PAPPLE_DART_DOMAIN;

NTSTATUS
NTAPI
AppleDartDomainCreate(
    _In_ PFDT_DRIVER_TREE Tree,
    _In_ ULONG Node,
    _In_z_ PCSTR Owner,
    _Out_ PAPPLE_DART_DOMAIN Domain);

NTSTATUS
NTAPI
AppleDartDomainMap(
    _Inout_ PAPPLE_DART_DOMAIN Domain,
    _In_ ULONG64 Iova,
    _In_ ULONG64 Physical,
    _In_ SIZE_T Length);

VOID
NTAPI
AppleDartDomainUnmap(
    _Inout_ PAPPLE_DART_DOMAIN Domain,
    _In_ ULONG64 Iova,
    _In_ SIZE_T Length);

BOOLEAN
NTAPI
AppleDartDomainTranslate(
    _In_ PAPPLE_DART_DOMAIN Domain,
    _In_ ULONG64 Iova,
    _Out_ PULONG64 Physical);

VOID
NTAPI
AppleDartDomainDestroy(
    _Inout_ PAPPLE_DART_DOMAIN Domain);
