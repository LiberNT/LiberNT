/*
 * PROJECT:     LiberNT Apple RTKit Library
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Address translations of the Apple silicon graphics processor
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

#define APPLE_UAT_PAGE_SHIFT        14
#define APPLE_UAT_PAGE_SIZE         (1ULL << APPLE_UAT_PAGE_SHIFT)
#define APPLE_UAT_LEVEL_BITS        11
#define APPLE_UAT_TABLE_ENTRIES     (1UL << APPLE_UAT_LEVEL_BITS)
#define APPLE_UAT_LEVELS            3
#define APPLE_UAT_MAX_ADDRESS       (1ULL << (APPLE_UAT_PAGE_SHIFT + APPLE_UAT_LEVELS * APPLE_UAT_LEVEL_BITS))

/* Who may reach a page: the firmware, the graphics processor, or both. */
#define APPLE_UAT_ACCESS_BOTH       0
#define APPLE_UAT_ACCESS_FIRMWARE   1
#define APPLE_UAT_ACCESS_GPU        2

/* How a page is cached. */
#define APPLE_UAT_MEMORY_CACHED     0
#define APPLE_UAT_MEMORY_DEVICE     1
#define APPLE_UAT_MEMORY_UNCACHED   2

typedef struct _APPLE_UAT_PROTECTION
{
    UCHAR Access;
    UCHAR Memory;
    BOOLEAN NoExecuteUser;
    BOOLEAN NoExecutePrivileged;
} APPLE_UAT_PROTECTION, *PAPPLE_UAT_PROTECTION;

typedef struct _APPLE_UAT_TABLE APPLE_UAT_TABLE, *PAPPLE_UAT_TABLE;

typedef struct _APPLE_UAT
{
    PAPPLE_UAT_TABLE Top;
    ULONG64 TopPhysical;
    ULONG Tables;
    BOOLEAN Global;
    PCSTR Owner;
} APPLE_UAT, *PAPPLE_UAT;

NTSTATUS
NTAPI
AppleUatCreate(
    _Out_ PAPPLE_UAT Uat,
    _In_ BOOLEAN Global,
    _In_z_ PCSTR Owner);

VOID
NTAPI
AppleUatDestroy(
    _Inout_ PAPPLE_UAT Uat);

NTSTATUS
NTAPI
AppleUatMap(
    _Inout_ PAPPLE_UAT Uat,
    _In_ ULONG64 Address,
    _In_ ULONG64 Physical,
    _In_ SIZE_T Length,
    _In_ const APPLE_UAT_PROTECTION *Protection);

BOOLEAN
NTAPI
AppleUatTranslate(
    _In_ PAPPLE_UAT Uat,
    _In_ ULONG64 Address,
    _Out_ PULONG64 Physical,
    _Out_opt_ PAPPLE_UAT_PROTECTION Protection);

NTSTATUS
NTAPI
AppleUatUnmap(
    _Inout_ PAPPLE_UAT Uat,
    _In_ ULONG64 Address,
    _In_ SIZE_T Length);

ULONG64
NTAPI
AppleUatEntry(
    _In_ ULONG64 Physical,
    _In_ const APPLE_UAT_PROTECTION *Protection,
    _In_ BOOLEAN Global);
