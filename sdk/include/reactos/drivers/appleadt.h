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
