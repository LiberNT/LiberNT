/*
 * PROJECT:         ReactOS DiskPart
 * LICENSE:         GPL - See COPYING in the top level directory
 * FILE:            base/system/diskpart/select.c
 * PURPOSE:         Manages all the partitions of the OS in an interactive way.
 * PROGRAMMERS:     Lee Schroeder
 */

#include "diskpart.h"

#define NDEBUG
#include <debug.h>

/* FUNCTIONS ******************************************************************/

EXIT_CODE
SelectDisk(
    _In_ INT argc,
    _In_ PWSTR *argv)
{
    PLIST_ENTRY Entry;
    PDISKENTRY DiskEntry;
    ULONG ulValue;

    DPRINT("Select Disk()\n");

    if (argc > 3)
    {
        ConResPuts(StdErr, IDS_ERROR_INVALID_ARGS);
        return EXIT_SUCCESS;
    }

    if (argc == 2)
    {
        if (CurrentDisk == NULL)
            ConResPuts(StdOut, IDS_SELECT_NO_DISK);
        else
            ConResPrintf(StdOut, IDS_SELECT_DISK, CurrentDisk->DiskNumber);
        return EXIT_SUCCESS;
    }

    if (!_wcsicmp(argv[2], L"system"))
    {
        CurrentDisk = NULL;

        Entry = DiskListHead.Flink;
        DiskEntry = CONTAINING_RECORD(Entry, DISKENTRY, ListEntry);

        CurrentDisk = DiskEntry;
        CurrentPartition = NULL;
        ConResPrintf(StdOut, IDS_SELECT_DISK, CurrentDisk->DiskNumber);
        return EXIT_SUCCESS;
    }
    else if (!_wcsicmp(argv[2], L"next"))
    {
        if (CurrentDisk == NULL)
        {
            CurrentPartition = NULL;
            ConResPuts(StdErr, IDS_SELECT_DISK_ENUM_NO_START);
            return EXIT_SUCCESS;
        }

        if (CurrentDisk->ListEntry.Flink == &DiskListHead)
        {
            CurrentDisk = NULL;
            CurrentPartition = NULL;
            ConResPuts(StdErr, IDS_SELECT_DISK_ENUM_FINISHED);
            return EXIT_SUCCESS;
        }

        Entry = CurrentDisk->ListEntry.Flink;

        DiskEntry = CONTAINING_RECORD(Entry, DISKENTRY, ListEntry);

        CurrentDisk = DiskEntry;
        CurrentPartition = NULL;
        ConResPrintf(StdOut, IDS_SELECT_DISK, CurrentDisk->DiskNumber);
        return EXIT_SUCCESS;
    }
    else if (IsDecString(argv[2]))
    {
        ulValue = wcstoul(argv[2], NULL, 10);
        if ((ulValue == 0) && (errno == ERANGE))
        {
            ConResPuts(StdErr, IDS_ERROR_INVALID_ARGS);
            return EXIT_SUCCESS;
        }

        CurrentDisk = NULL;

        Entry = DiskListHead.Flink;
        while (Entry != &DiskListHead)
        {
            DiskEntry = CONTAINING_RECORD(Entry, DISKENTRY, ListEntry);

            if (DiskEntry->DiskNumber == ulValue)
            {
                CurrentDisk = DiskEntry;
                CurrentPartition = NULL;
                ConResPrintf(StdOut, IDS_SELECT_DISK, CurrentDisk->DiskNumber);
                return EXIT_SUCCESS;
            }

            Entry = Entry->Flink;
        }
    }
    else
    {
        ConResPuts(StdErr, IDS_ERROR_INVALID_ARGS);
        return EXIT_SUCCESS;
    }

    ConResPuts(StdErr, IDS_SELECT_DISK_INVALID);
    return EXIT_SUCCESS;
}


static
VOID
SelectVolumeEntry(
    _In_ PVOLENTRY VolumeEntry)
{
    PDISKENTRY DiskEntry;
    PPARTENTRY PartEntry;

    CurrentVolume = VolumeEntry;

    if (VolumeEntry->pExtents == NULL ||
        VolumeEntry->pExtents->NumberOfDiskExtents != 1)
        return;

    DiskEntry = GetDiskForVolume(VolumeEntry);
    PartEntry = GetPartitionForVolume(VolumeEntry);
    if (DiskEntry == NULL || PartEntry == NULL)
        return;

    CurrentDisk = DiskEntry;
    CurrentPartition = PartEntry;
}


static
ULONG
GetPartitionListNumber(
    _In_ PPARTENTRY SelectedEntry)
{
    PLIST_ENTRY Entry;
    PPARTENTRY PartEntry;
    ULONG PartNumber = 1;

    for (Entry = CurrentDisk->PrimaryPartListHead.Flink; Entry != &CurrentDisk->PrimaryPartListHead; Entry = Entry->Flink)
    {
        PartEntry = CONTAINING_RECORD(Entry, PARTENTRY, ListEntry);
        if (CurrentDisk->PartitionStyle == PARTITION_STYLE_GPT ?
            IsEqualGUID(&PartEntry->Gpt.PartitionType, &PARTITION_ENTRY_UNUSED_GUID) :
            PartEntry->Mbr.PartitionType == PARTITION_ENTRY_UNUSED)
            continue;
        if (PartEntry == SelectedEntry)
            return PartNumber;
        PartNumber++;
    }

    if (CurrentDisk->PartitionStyle == PARTITION_STYLE_MBR)
    {
        for (Entry = CurrentDisk->LogicalPartListHead.Flink; Entry != &CurrentDisk->LogicalPartListHead; Entry = Entry->Flink)
        {
            PartEntry = CONTAINING_RECORD(Entry, PARTENTRY, ListEntry);
            if (PartEntry->Mbr.PartitionType == PARTITION_ENTRY_UNUSED)
                continue;
            if (PartEntry == SelectedEntry)
                return PartNumber;
            PartNumber++;
        }
    }

    return 0;
}


EXIT_CODE
SelectPartition(
    _In_ INT argc,
    _In_ PWSTR *argv)
{
    PLIST_ENTRY Entry;
    PPARTENTRY PartEntry;
    ULONG ulValue;
    ULONG PartNumber = 1;

    DPRINT("Select Partition()\n");

    if (argc > 3)
    {
        ConResPuts(StdErr, IDS_ERROR_INVALID_ARGS);
        return EXIT_SUCCESS;
    }

    if (CurrentDisk == NULL)
    {
        ConResPuts(StdOut, IDS_SELECT_PARTITION_NO_DISK);
        return EXIT_SUCCESS;
    }

    if (argc == 2)
    {
        if (CurrentPartition == NULL)
            ConResPuts(StdOut, IDS_SELECT_NO_PARTITION);
        else
            ConResPrintf(StdOut, IDS_SELECT_PARTITION, GetPartitionListNumber(CurrentPartition));
        return EXIT_SUCCESS;
    }

    if (!IsDecString(argv[2]))
    {
        ConResPuts(StdErr, IDS_ERROR_INVALID_ARGS);
        return EXIT_SUCCESS;
    }

    ulValue = wcstoul(argv[2], NULL, 10);
    if ((ulValue == 0) && (errno == ERANGE))
    {
        ConResPuts(StdErr, IDS_ERROR_INVALID_ARGS);
        return EXIT_SUCCESS;
    }

    if (CurrentDisk->PartitionStyle == PARTITION_STYLE_MBR)
    {
        Entry = CurrentDisk->PrimaryPartListHead.Flink;
        while (Entry != &CurrentDisk->PrimaryPartListHead)
        {
            PartEntry = CONTAINING_RECORD(Entry, PARTENTRY, ListEntry);

            if (PartEntry->Mbr.PartitionType != PARTITION_ENTRY_UNUSED)
            {
                if (PartNumber == ulValue)
                {
                    CurrentPartition = PartEntry;
                    CurrentVolume = GetVolumeFromPartition(PartEntry);
                    ConResPrintf(StdOut, IDS_SELECT_PARTITION, PartNumber);
                    return EXIT_SUCCESS;
                }

                PartNumber++;
            }

            Entry = Entry->Flink;
        }

        Entry = CurrentDisk->LogicalPartListHead.Flink;
        while (Entry != &CurrentDisk->LogicalPartListHead)
        {
            PartEntry = CONTAINING_RECORD(Entry, PARTENTRY, ListEntry);

            if (PartEntry->Mbr.PartitionType != PARTITION_ENTRY_UNUSED)
            {
                if (PartNumber == ulValue)
                {
                    CurrentPartition = PartEntry;
                    CurrentVolume = GetVolumeFromPartition(PartEntry);
                    ConResPrintf(StdOut, IDS_SELECT_PARTITION, PartNumber);
                    return EXIT_SUCCESS;
                }

                PartNumber++;
            }
            Entry = Entry->Flink;
        }
    }
    else if (CurrentDisk->PartitionStyle == PARTITION_STYLE_GPT)
    {
        Entry = CurrentDisk->PrimaryPartListHead.Flink;
        while (Entry != &CurrentDisk->PrimaryPartListHead)
        {
            PartEntry = CONTAINING_RECORD(Entry, PARTENTRY, ListEntry);

            if (!IsEqualGUID(&PartEntry->Gpt.PartitionType, &PARTITION_ENTRY_UNUSED_GUID))
            {
                if (PartNumber == ulValue)
                {
                    CurrentPartition = PartEntry;
                    CurrentVolume = GetVolumeFromPartition(PartEntry);
                    ConResPrintf(StdOut, IDS_SELECT_PARTITION, PartNumber);
                    return EXIT_SUCCESS;
                }

                PartNumber++;
            }

            Entry = Entry->Flink;
        }
    }

    ConResPuts(StdErr, IDS_SELECT_PARTITION_INVALID);
    return EXIT_SUCCESS;
}


EXIT_CODE
SelectVolume(
    _In_ INT argc,
    _In_ PWSTR *argv)
{
    PLIST_ENTRY Entry;
    PVOLENTRY VolumeEntry;
    ULONG ulValue;

    DPRINT("SelectVolume()\n");

    if (argc > 3)
    {
        ConResPuts(StdErr, IDS_ERROR_INVALID_ARGS);
        return EXIT_SUCCESS;
    }

    if (argc == 2)
    {
        if (CurrentVolume == NULL)
            ConResPuts(StdOut, IDS_SELECT_NO_VOLUME);
        else
            ConResPrintf(StdOut, IDS_SELECT_VOLUME, CurrentVolume->VolumeNumber);
        return EXIT_SUCCESS;
    }

    if (iswalpha(argv[2][0]) &&
        (argv[2][1] == UNICODE_NULL ||
         (argv[2][1] == L':' && argv[2][2] == UNICODE_NULL)))
    {
        CurrentVolume = NULL;

        for (Entry = VolumeListHead.Flink; Entry != &VolumeListHead; Entry = Entry->Flink)
        {
            VolumeEntry = CONTAINING_RECORD(Entry, VOLENTRY, ListEntry);

            if (towupper(VolumeEntry->DriveLetter) == towupper(argv[2][0]))
            {
                SelectVolumeEntry(VolumeEntry);
                ConResPrintf(StdOut, IDS_SELECT_VOLUME, CurrentVolume->VolumeNumber);
                return EXIT_SUCCESS;
            }
        }

        ConResPuts(StdErr, IDS_SELECT_VOLUME_INVALID);
        return EXIT_SUCCESS;
    }

    if (!IsDecString(argv[2]))
    {
        ConResPuts(StdErr, IDS_ERROR_INVALID_ARGS);
        return EXIT_SUCCESS;
    }

    ulValue = wcstoul(argv[2], NULL, 10);
    if ((ulValue == 0) && (errno == ERANGE))
    {
        ConResPuts(StdErr, IDS_ERROR_INVALID_ARGS);
        return EXIT_SUCCESS;
    }

    CurrentVolume = NULL;

    Entry = VolumeListHead.Flink;
    while (Entry != &VolumeListHead)
    {
        VolumeEntry = CONTAINING_RECORD(Entry, VOLENTRY, ListEntry);

        if (VolumeEntry->VolumeNumber == ulValue)
        {
            SelectVolumeEntry(VolumeEntry);
            ConResPrintf(StdOut, IDS_SELECT_VOLUME, CurrentVolume->VolumeNumber);
            return EXIT_SUCCESS;
        }

        Entry = Entry->Flink;
    }

    ConResPuts(StdErr, IDS_SELECT_VOLUME_INVALID);
    return EXIT_SUCCESS;
}
