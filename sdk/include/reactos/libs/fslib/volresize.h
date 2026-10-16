/*
 * PROJECT:     LiberNT volume resize library
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Public definitions for shrinking and extending basic volumes
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#ifndef __VOLRESIZE_H
#define __VOLRESIZE_H

typedef struct _VOLRESIZE_INFO
{
    ULONGLONG PartitionBytes;
    ULONGLONG VolumeBytes;
    ULONGLONG TotalClusters;
    ULONGLONG FreeClusters;
    ULONGLONG UnmovableEndCluster;
    ULONGLONG MaxReclaimableBytes;
    ULONG BytesPerCluster;
    ULONG BytesPerSector;
    ULONG DiskNumber;
    ULONG PartitionNumber;
    BOOLEAN Raw;
} VOLRESIZE_INFO, *PVOLRESIZE_INFO;

typedef BOOLEAN
(NTAPI *PVOLRESIZE_PROGRESS)(
    _In_opt_ PVOID Context,
    _In_ ULONG Percent);

NTSTATUS
NTAPI
VolResizeQuery(
    _In_ PCUNICODE_STRING VolumePath,
    _Out_ PVOLRESIZE_INFO Info);

NTSTATUS
NTAPI
VolResizeShrink(
    _In_ PCUNICODE_STRING VolumePath,
    _In_ ULONGLONG ReclaimBytes,
    _In_opt_ PVOLRESIZE_PROGRESS Progress,
    _In_opt_ PVOID ProgressContext);

NTSTATUS
NTAPI
VolResizeExtend(
    _In_ PCUNICODE_STRING VolumePath,
    _In_ ULONGLONG AddBytes,
    _In_ BOOLEAN ExtendFileSystem);

#endif /* __VOLRESIZE_H */
