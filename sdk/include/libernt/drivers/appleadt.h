/*
 * PROJECT:     LiberNT Apple RTKit Library
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Reader of the Apple device tree the boot loader exports
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

#define APPLE_ADT_NAME_LENGTH       32
#define APPLE_ADT_NO_NODE           0xffffffffUL
#define APPLE_ADT_TEMPLATE          0x80000000UL

typedef struct _APPLE_ADT
{
    const UCHAR *Base;
    ULONG Length;
} APPLE_ADT, *PAPPLE_ADT;

BOOLEAN
NTAPI
AppleAdtOpen(
    _Out_ PAPPLE_ADT Adt,
    _In_reads_bytes_(Length) const VOID *Base,
    _In_ ULONG Length);

ULONG
NTAPI
AppleAdtRootNode(
    _In_ PAPPLE_ADT Adt);

ULONG
NTAPI
AppleAdtFirstChild(
    _In_ PAPPLE_ADT Adt,
    _In_ ULONG Node);

ULONG
NTAPI
AppleAdtNextSibling(
    _In_ PAPPLE_ADT Adt,
    _In_ ULONG Node);

ULONG
NTAPI
AppleAdtChildCount(
    _In_ PAPPLE_ADT Adt,
    _In_ ULONG Node);

ULONG
NTAPI
AppleAdtChild(
    _In_ PAPPLE_ADT Adt,
    _In_ ULONG Node,
    _In_z_ PCSTR Name);

ULONG
NTAPI
AppleAdtPath(
    _In_ PAPPLE_ADT Adt,
    _In_z_ PCSTR Path);

const VOID *
NTAPI
AppleAdtGetProperty(
    _In_ PAPPLE_ADT Adt,
    _In_ ULONG Node,
    _In_z_ PCSTR Name,
    _Out_opt_ PULONG Length);

BOOLEAN
NTAPI
AppleAdtReadU32(
    _In_ PAPPLE_ADT Adt,
    _In_ ULONG Node,
    _In_z_ PCSTR Name,
    _Out_ PULONG Value);

BOOLEAN
NTAPI
AppleAdtReadU64(
    _In_ PAPPLE_ADT Adt,
    _In_ ULONG Node,
    _In_z_ PCSTR Name,
    _Out_ PULONG64 Value);

PCSTR
NTAPI
AppleAdtNodeName(
    _In_ PAPPLE_ADT Adt,
    _In_ ULONG Node);

BOOLEAN
NTAPI
AppleAdtIsCompatible(
    _In_ PAPPLE_ADT Adt,
    _In_ ULONG Node,
    _In_z_ PCSTR Compatible);

#define APPLE_PMP_PTD_UPDATE_OFFSET     0x10000
#define APPLE_PMP_PTD_READ_STRIDE       16
#define APPLE_PMP_PTD_UPDATE_STRIDE     8
#define APPLE_PMP_NO_REQUEST_BIT        0xffffffffUL

NTSTATUS
NTAPI
AppleAdtPtdRangeIndex(
    _In_ PAPPLE_ADT Adt,
    _In_z_ PCSTR Name,
    _Out_ PULONG Index);

NTSTATUS
NTAPI
AppleAdtPmpRequestWord(
    _In_ PAPPLE_ADT Adt,
    _Out_ PULONG64 Table,
    _Out_ PULONG Index);

NTSTATUS
NTAPI
AppleAdtPmgrPmpBit(
    _In_ PAPPLE_ADT Adt,
    _In_z_ PCSTR Name,
    _Out_ PULONG Bit);

/* What the graphics driver needs from the machine's tree: the regions, the loaded
 * firmware segments and the operating points of the firmware the boot chain loaded. */
#define APPLE_AGX_REGIONS           4
#define APPLE_AGX_MAX_STATES        16
#define APPLE_AGX_FIRMWARE_UPPER    0xfffffc0000000000ULL

#define APPLE_AGX_REGION_TABLE      0
#define APPLE_AGX_REGION_ROOT       1
#define APPLE_AGX_REGION_HANDOFF    2
#define APPLE_AGX_REGION_WINDOW     3

typedef struct _APPLE_AGX_SEGMENT
{
    ULONG64 Physical;
    ULONG64 Virtual;
    ULONG64 Remap;
    ULONG Size;
    ULONG Flags;
} APPLE_AGX_SEGMENT, *PAPPLE_AGX_SEGMENT;

typedef struct _APPLE_AGX_STATE
{
    ULONG Frequency;
    ULONG Millivolts;
} APPLE_AGX_STATE, *PAPPLE_AGX_STATE;

typedef struct _APPLE_AGX_DESCRIPTION
{
    ULONG64 RegionBase[APPLE_AGX_REGIONS];
    ULONG64 RegionSize[APPLE_AGX_REGIONS];
    APPLE_AGX_SEGMENT Text;
    APPLE_AGX_SEGMENT Data;
    APPLE_AGX_STATE State[APPLE_AGX_MAX_STATES];
    APPLE_AGX_STATE SramState[APPLE_AGX_MAX_STATES];
    ULONG States;
    BOOLEAN SegmentsKnown;
    BOOLEAN SegmentsPlaced;
} APPLE_AGX_DESCRIPTION, *PAPPLE_AGX_DESCRIPTION;

NTSTATUS
NTAPI
AppleAgxDescribe(
    _In_ PAPPLE_ADT Adt,
    _In_z_ PCSTR Part,
    _In_ ULONG PageSize,
    _Out_ PAPPLE_AGX_DESCRIPTION Description);
