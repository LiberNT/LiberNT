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

#ifdef __cplusplus
}
#endif
