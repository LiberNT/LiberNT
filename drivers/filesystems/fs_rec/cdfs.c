/*
 * PROJECT:     LiberNT File System Recognizer
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     ISO 9660 and High Sierra volume recognizer
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "fs_rec.h"

#define NDEBUG
#include <debug.h>

#define CDFS_LOGICAL_SECTOR_SIZE 2048
#define CDFS_FIRST_DESCRIPTOR_SECTOR 16
#define CDFS_ISO_ID_OFFSET 1
#define CDFS_ISO_ID "CD001"
#define CDFS_HSG_ID_OFFSET 9
#define CDFS_HSG_ID "CDROM"
#define CDFS_ID_LENGTH 5

C_ASSERT(sizeof(CDFS_ISO_ID) - 1 == CDFS_ID_LENGTH);
C_ASSERT(sizeof(CDFS_HSG_ID) - 1 == CDFS_ID_LENGTH);
C_ASSERT(CDFS_HSG_ID_OFFSET + CDFS_ID_LENGTH <= CDFS_LOGICAL_SECTOR_SIZE);

static
BOOLEAN
FsRecIsCdfsVolume(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ ULONG SectorSize)
{
    PUCHAR Descriptor = NULL;
    LARGE_INTEGER Offset;
    ULONG Length;
    BOOLEAN Recognized = FALSE;

    PAGED_CODE();
    ASSERT(SectorSize != 0);

    Offset.QuadPart = (LONGLONG)CDFS_FIRST_DESCRIPTOR_SECTOR * CDFS_LOGICAL_SECTOR_SIZE;
    if (Offset.QuadPart % SectorSize != 0)
        return FALSE;

    Length = max(SectorSize, CDFS_LOGICAL_SECTOR_SIZE);
    Length = ROUND_UP(Length, SectorSize);

    if (FsRecReadBlock(DeviceObject, &Offset, Length, SectorSize, (PVOID *)&Descriptor, NULL))
    {
        Recognized = RtlEqualMemory(Descriptor + CDFS_ISO_ID_OFFSET, CDFS_ISO_ID, CDFS_ID_LENGTH) ||
                     RtlEqualMemory(Descriptor + CDFS_HSG_ID_OFFSET, CDFS_HSG_ID, CDFS_ID_LENGTH);
    }

    if (Descriptor != NULL)
        ExFreePool(Descriptor);

    return Recognized;
}

NTSTATUS
NTAPI
FsRecCdfsFsControl(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp)
{
    PIO_STACK_LOCATION Stack;
    ULONG SectorSize;

    PAGED_CODE();

    Stack = IoGetCurrentIrpStackLocation(Irp);
    switch (Stack->MinorFunction)
    {
        case IRP_MN_MOUNT_VOLUME:
            if (FsRecGetDeviceSectorSize(Stack->Parameters.MountVolume.DeviceObject, &SectorSize) &&
                FsRecIsCdfsVolume(Stack->Parameters.MountVolume.DeviceObject, SectorSize))
            {
                return STATUS_FS_DRIVER_REQUIRED;
            }
            return STATUS_UNRECOGNIZED_VOLUME;

        case IRP_MN_LOAD_FILE_SYSTEM:
            return FsRecLoadFileSystem(DeviceObject,
                                       L"\\Registry\\Machine\\System\\CurrentControlSet\\Services\\Cdfs");
    }

    return STATUS_INVALID_DEVICE_REQUEST;
}
