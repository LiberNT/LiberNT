/*
 * PROJECT:     LiberNT Boot Loader
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     NTFS writes on boot devices
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

struct _DK_DEVICE;

#ifdef __cplusplus
extern "C" {
#endif

NTSTATUS
NtfsBlMount(
    _In_ struct _DK_DEVICE* Device);

NTSTATUS
NtfsBlUnmount(VOID);

NTSTATUS
NtfsBlReadFile(
    _In_ PCSTR Path,
    _Out_writes_bytes_(Length) PVOID Buffer,
    _In_ ULONG Length,
    _Out_ PULONG Read);

NTSTATUS
NtfsBlWriteFile(
    _In_ PCSTR Path,
    _In_reads_bytes_(Size) const VOID* Data,
    _In_ ULONG Size);

NTSTATUS
NtfsBlCreateDirectory(
    _In_ PCSTR Path);

NTSTATUS
NtfsBlOpenFile(
    _In_ PCSTR Path,
    _Out_ PVOID* File,
    _Out_opt_ PULONGLONG Size,
    _Out_opt_ PBOOLEAN IsDirectory);

NTSTATUS
NtfsBlReadFileAt(
    _In_ PVOID File,
    _In_ ULONGLONG Offset,
    _Out_writes_bytes_(Length) PVOID Buffer,
    _In_ ULONG Length,
    _Out_ PULONG Read);

VOID
NtfsBlCloseFile(
    _In_opt_ PVOID File);

NTSTATUS
NtfsBlDeleteFile(
    _In_ PCSTR Path,
    _In_ BOOLEAN IsDirectory);

NTSTATUS
NtfsBlSetDirty(
    _In_ BOOLEAN Dirty);

NTSTATUS
NtfsBlQuerySpace(
    _Out_ PULONGLONG FreeBytes,
    _Out_ PULONG ClusterSize);

#ifdef __cplusplus
}
#endif
