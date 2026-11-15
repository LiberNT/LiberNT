/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Cache manager entry points the rest of the kernel calls
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

extern ULONG CcLazyWritePages;
extern ULONG CcLazyWriteIos;
extern ULONG CcMapDataWait;
extern ULONG CcMapDataNoWait;
extern ULONG CcPinReadWait;
extern ULONG CcPinReadNoWait;
extern ULONG CcPinMappedDataCount;
extern ULONG CcDataPages;
extern ULONG CcDataFlushes;

CODE_SEG("INIT")
BOOLEAN
CcInitializeCacheManager(VOID);

CODE_SEG("INIT")
VOID
NTAPI
CcPfInitializePrefetcher(VOID);

VOID
NTAPI
CcShutdownSystem(VOID);

NTSTATUS
CcRosFlushDirtyPages(
    _In_ ULONG Target,
    _Out_ PULONG Count,
    _In_ BOOLEAN Wait,
    _In_ BOOLEAN CalledFromLazy);

ULONG
CcUnmapIdleViews(
    _In_ SIZE_T Bytes);

VOID
NTAPI
CcNtQueryCachePages(
    _Out_ PULONG MappedPages,
    _Out_ PULONG MaximumPages);

VOID
NTAPI
CcNtQueryDirtyPages(
    _Out_ PULONG TotalDirtyPages,
    _Out_ PULONG DirtyPageThreshold);

VOID
NTAPI
CcNtSetDirtyPageThreshold(
    _In_ ULONG DirtyPageThreshold);

VOID
NTAPI
CcMdlReadComplete2(
    _In_ PFILE_OBJECT FileObject,
    _In_ PMDL MemoryDescriptorList);

VOID
NTAPI
CcMdlWriteComplete2(
    _In_ PFILE_OBJECT FileObject,
    _In_ PLARGE_INTEGER FileOffset,
    _In_ PMDL MdlChain);
