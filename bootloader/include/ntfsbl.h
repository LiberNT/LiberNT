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
NtfsBlFormatMemory(
    _Out_writes_bytes_all_(Size) PVOID Volume,
    _In_ ULONGLONG Size,
    _In_ ULONG HiddenSectors,
    _In_ PCWSTR Label);

NTSTATUS
NtfsBlWriteFileAt(
    _In_ PVOID File,
    _In_ ULONGLONG Offset,
    _In_reads_bytes_(Length) const VOID* Buffer,
    _In_ ULONG Length);

NTSTATUS
NtfsBlCreateWimFile(
    _In_ PCSTR Path,
    _In_ ULONG FileAttributes,
    _In_ ULONGLONG Size,
    _In_reads_bytes_opt_(ReparseLength) const UCHAR* Reparse,
    _In_ ULONG ReparseLength);

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
