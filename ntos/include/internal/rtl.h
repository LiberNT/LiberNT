/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Kernel run-time library internals
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

#ifdef _WIN64
VOID
NTAPI
RtlInitializeSListHead(IN PSLIST_HEADER ListHead);
#define InitializeSListHead RtlInitializeSListHead
#endif

#define RTL_FIND_CHAR_IN_UNICODE_STRING_START_AT_END        1

#define RTL_FIND_CHAR_IN_UNICODE_STRING_COMPLEMENT_CHAR_SET 2

#define RTL_FIND_CHAR_IN_UNICODE_STRING_CASE_INSENSITIVE    4

_IRQL_requires_max_(APC_LEVEL)
ULONG
NTAPI
RtlRosGetAppcompatVersion(VOID);

PVOID
NTAPI
RtlPcToFileHeader(
    _In_ PVOID PcValue,
    _Out_ PVOID *BaseOfImage);

NTSTATUS
NTAPI
RtlQueryAtomListInAtomTable(
    _In_ PRTL_ATOM_TABLE AtomTable,
    _In_ ULONG MaxAtomCount,
    _Out_ ULONG *AtomCount,
    _Out_ RTL_ATOM *AtomList);

NTSTATUS
NTAPI
RtlFindCharInUnicodeString(
    _In_ ULONG Flags,
    _In_ PCUNICODE_STRING SearchString,
    _In_ PCUNICODE_STRING MatchString,
    _Out_ PUSHORT Position);
