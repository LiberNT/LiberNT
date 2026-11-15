/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     File system run-time library internals
 * COPYRIGHT:   Copyright Alex Ionescu <alex.ionescu@reactos.org>
 *              Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

extern PAGED_LOOKASIDE_LIST FsRtlFileLockLookasideList;

NTSTATUS
NTAPI
FsRtlAcquireFileForCcFlushEx(
    _In_ PFILE_OBJECT FileObject);

_Check_return_
NTSTATUS
NTAPI
FsRtlAcquireFileForModWriteEx(
    _In_ PFILE_OBJECT FileObject,
    _In_ PLARGE_INTEGER EndingOffset,
    _Outptr_result_maybenull_ PERESOURCE *ResourceToRelease);

NTSTATUS
NTAPI
FsRtlAcquireToCreateMappedSection(
    _In_ PFILE_OBJECT FileObject,
    _In_ ULONG SectionPageProtection);

VOID
NTAPI
FsRtlReleaseFileForCcFlush(
    _In_ PFILE_OBJECT FileObject);

VOID
NTAPI
FsRtlReleaseFileForModWrite(
    _In_ PFILE_OBJECT FileObject,
    _In_ PERESOURCE ResourceToRelease);

CODE_SEG("INIT")
BOOLEAN
NTAPI
FsRtlInitSystem(VOID);

CODE_SEG("INIT")
VOID
NTAPI
FsRtlInitializeLargeMcbs(VOID);

VOID
NTAPI
FsRtlPTeardownPerFileObjectContexts(
    _In_ PFILE_OBJECT FileObject);

CODE_SEG("INIT")
VOID
NTAPI
FsRtlInitializeTunnels(VOID);
