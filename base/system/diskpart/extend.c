/*
 * PROJECT:         ReactOS DiskPart
 * LICENSE:         GPL - See COPYING in the top level directory
 * FILE:            base/system/diskpart/extend.c
 * PURPOSE:         Manages all the partitions of the OS in an interactive way.
 * PROGRAMMERS:     Lee Schroeder
 */

#include "diskpart.h"

#include <fslib/volresize.h>

#define NDEBUG
#include <debug.h>

static
ULONGLONG
GetFollowingFreeBytes(
    _In_ PVOLENTRY VolumeEntry)
{
    PLIST_ENTRY DiskEntry, PartEntry;
    PDISKENTRY Disk;
    PPARTENTRY Partition, Next;
    ULONGLONG ullBytes = 0;

    if (VolumeEntry->pExtents == NULL || VolumeEntry->pExtents->NumberOfDiskExtents != 1)
        return 0;

    for (DiskEntry = DiskListHead.Flink; DiskEntry != &DiskListHead; DiskEntry = DiskEntry->Flink)
    {
        Disk = CONTAINING_RECORD(DiskEntry, DISKENTRY, ListEntry);
        if (Disk->DiskNumber != VolumeEntry->pExtents->Extents[0].DiskNumber)
            continue;

        for (PartEntry = Disk->PrimaryPartListHead.Flink;
             PartEntry != &Disk->PrimaryPartListHead;
             PartEntry = PartEntry->Flink)
        {
            Partition = CONTAINING_RECORD(PartEntry, PARTENTRY, ListEntry);
            if (Partition->StartSector.QuadPart * Disk->BytesPerSector !=
                (ULONGLONG)VolumeEntry->pExtents->Extents[0].StartingOffset.QuadPart)
            {
                continue;
            }
            for (PartEntry = PartEntry->Flink;
                 PartEntry != &Disk->PrimaryPartListHead;
                 PartEntry = PartEntry->Flink)
            {
                Next = CONTAINING_RECORD(PartEntry, PARTENTRY, ListEntry);
                if (Next->IsPartitioned)
                    break;
                ullBytes += Next->SectorCount.QuadPart * Disk->BytesPerSector;
            }
            return ullBytes;
        }
    }
    return ullBytes;
}

EXIT_CODE
extend_main(
    _In_ INT argc,
    _In_ PWSTR *argv)
{
    UNICODE_STRING VolumePath;
    ULONGLONG ullSize = 0, ullFree, ullAdd;
    BOOL bSize = FALSE, bFileSystem = FALSE, bNtfs, bRaw;
    PWSTR pszSuffix = NULL;
    NTSTATUS Status;
    INT i;

    if (CurrentVolume == NULL)
    {
        ConResPuts(StdOut, IDS_SHRINK_NO_VOLUME);
        return EXIT_SUCCESS;
    }

    for (i = 1; i < argc; i++)
    {
        if (HasPrefix(argv[i], L"size=", &pszSuffix))
        {
            if (!IsDecString(pszSuffix))
            {
                ConResPuts(StdErr, IDS_ERROR_INVALID_ARGS);
                return EXIT_SUCCESS;
            }
            ullSize = _wcstoui64(pszSuffix, NULL, 10);
            bSize = TRUE;
        }
        else if (_wcsicmp(argv[i], L"filesystem") == 0)
        {
            bFileSystem = TRUE;
        }
        else if (!HasPrefix(argv[i], L"disk=", NULL) &&
                 _wcsicmp(argv[i], L"noerr") != 0)
        {
            ConResPuts(StdErr, IDS_ERROR_INVALID_ARGS);
            return EXIT_SUCCESS;
        }
    }

    bNtfs = CurrentVolume->pszFilesystem != NULL &&
            _wcsicmp(CurrentVolume->pszFilesystem, L"NTFS") == 0;
    bRaw = CurrentVolume->pszFilesystem == NULL ||
           CurrentVolume->pszFilesystem[0] == UNICODE_NULL ||
           _wcsicmp(CurrentVolume->pszFilesystem, L"RAW") == 0;
    if (!bNtfs && !bRaw)
    {
        ConResPuts(StdOut, IDS_EXTEND_FS_NOT_SUPPORTED);
        return EXIT_SUCCESS;
    }

    RtlInitUnicodeString(&VolumePath, CurrentVolume->DeviceName);

    if (bFileSystem)
    {
        Status = VolResizeExtend(&VolumePath, 0, TRUE);
        if (!NT_SUCCESS(Status))
        {
            ConResPuts(StdOut, IDS_RESIZE_INVALID_PARAMETER);
            return EXIT_SUCCESS;
        }
        ConResPuts(StdOut, IDS_EXTEND_FS_SUCCESS);
        return EXIT_SUCCESS;
    }

    ullFree = GetFollowingFreeBytes(CurrentVolume);
    ullFree -= ullFree % SIZE_1MB;
    if (ullFree == 0)
    {
        ConResPuts(StdOut, IDS_EXTEND_NO_SPACE);
        return EXIT_SUCCESS;
    }

    ullAdd = bSize ? ullSize * SIZE_1MB : ullFree;
    if (ullAdd == 0 || ullAdd > ullFree)
    {
        ConResPuts(StdOut, IDS_EXTEND_NOT_ENOUGH);
        return EXIT_SUCCESS;
    }

    Status = VolResizeExtend(&VolumePath, ullAdd, bNtfs);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("VolResizeExtend failed 0x%08lx\n", Status);
        ConResPuts(StdOut, IDS_EXTEND_NOT_ENOUGH);
        return EXIT_SUCCESS;
    }

    ConResPuts(StdOut, IDS_EXTEND_SUCCESS);
    RefreshResizedVolume(CurrentVolume->VolumeNumber);
    return EXIT_SUCCESS;
}
