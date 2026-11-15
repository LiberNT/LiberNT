/*
 * PROJECT:     LiberNT NT Virtual Memory Subsystem
 * LICENSE:     GPL-3.0-only (https://spdx.org/licenses/GPL-3.0-only)
 * PURPOSE:     System image loader internals shared by the loader files
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

#include <mm/nt/mint.h>

#define MM_SYSLDR_NO_IMPORTS   ((PVOID)(ULONG_PTR)-2)
#define MM_SYSLDR_BOOT_LOADED  ((PVOID)(ULONG_PTR)-1)
#define MM_SYSLDR_SINGLE_ENTRY 0x1

#define MI_EXPORT_FORWARD_DEPTH 8

extern BOOLEAN MmEnforceWriteProtection;

VOID
MiLockSystemImages(VOID);

VOID
MiUnlockSystemImages(VOID);

PLDR_DATA_TABLE_ENTRY
MiFindSystemImage(
    _In_ PCUNICODE_STRING Name,
    _In_ BOOLEAN MatchFullName);

VOID
MiPublishSystemImage(
    _In_ PLDR_DATA_TABLE_ENTRY Entry);

VOID
MiWithdrawSystemImage(
    _In_ PLDR_DATA_TABLE_ENTRY Entry);

PLDR_DATA_TABLE_ENTRY
NTAPI
MiLookupDataTableEntry(
    _In_ PVOID Address);

ULONG
MiImportListCount(
    _In_opt_ PVOID ImportList);

PLDR_DATA_TABLE_ENTRY
MiImportListEntry(
    _In_ PVOID ImportList,
    _In_ ULONG Index);

PVOID
MiCreateImportList(
    _In_reads_(Count) PLDR_DATA_TABLE_ENTRY *Modules,
    _In_ ULONG Count);

VOID
MiFreeImportList(
    _In_opt_ PVOID ImportList);

PVOID
MiFindExport(
    _In_ PVOID ImageBase,
    _In_ PCSTR Name);

NTSTATUS
MiBindImports(
    _In_ PVOID ImageBase,
    _In_ PCUNICODE_STRING ImageName,
    _In_ PCUNICODE_STRING Directory,
    _Out_ PVOID *ImportList);

VOID
MiDropImportReferences(
    _In_opt_ PVOID ImportList);
