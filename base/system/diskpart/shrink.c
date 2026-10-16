/*
 * PROJECT:         ReactOS DiskPart
 * LICENSE:         GPL - See COPYING in the top level directory
 * FILE:            base/system/diskpart/shrink.c
 * PURPOSE:         Manages all the partitions of the OS in an interactive way.
 * PROGRAMMERS:     Lee Schroeder
 */

#include "diskpart.h"

#include <fslib/volresize.h>

#define NDEBUG
#include <debug.h>

VOID
PrintResizeSize(
    _In_ UINT MessageId,
    _In_ ULONGLONG ullBytes)
{
    WCHAR szUnitBuffer[8];
    INT nUnitId;

    if (ullBytes >= SIZE_10TB)
    {
        ullBytes /= SIZE_1TB;
        nUnitId = IDS_UNIT_TB;
    }
    else if (ullBytes >= SIZE_10GB)
    {
        ullBytes /= SIZE_1GB;
        nUnitId = IDS_UNIT_GB;
    }
    else if (ullBytes >= SIZE_10MB)
    {
        ullBytes /= SIZE_1MB;
        nUnitId = IDS_UNIT_MB;
    }
    else if (ullBytes >= SIZE_10KB)
    {
        ullBytes /= SIZE_1KB;
        nUnitId = IDS_UNIT_KB;
    }
    else
    {
        nUnitId = IDS_UNIT_B;
    }

    LoadStringW(GetModuleHandle(NULL), nUnitId, szUnitBuffer, ARRAYSIZE(szUnitBuffer));
    ConResPrintf(StdOut, MessageId, ullBytes, szUnitBuffer);
}

VOID
RefreshResizedVolume(
    _In_ ULONG ulVolumeNumber)
{
    PLIST_ENTRY Entry;
    PVOLENTRY VolumeEntry;

    DestroyVolumeList();
    DestroyPartitionList();
    CreatePartitionList();
    CreateVolumeList();

    for (Entry = VolumeListHead.Flink; Entry != &VolumeListHead; Entry = Entry->Flink)
    {
        VolumeEntry = CONTAINING_RECORD(Entry, VOLENTRY, ListEntry);
        if (VolumeEntry->VolumeNumber == ulVolumeNumber)
        {
            CurrentVolume = VolumeEntry;
            break;
        }
    }
}

EXIT_CODE
shrink_main(
    _In_ INT argc,
    _In_ PWSTR *argv)
{
    UNICODE_STRING VolumePath;
    VOLRESIZE_INFO Info;
    ULONGLONG ullDesired = 0, ullMinimum = 0, ullReclaim;
    BOOL bDesired = FALSE, bMinimum = FALSE, bQueryMax = FALSE;
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
        if (HasPrefix(argv[i], L"desired=", &pszSuffix))
        {
            if (!IsDecString(pszSuffix))
            {
                ConResPuts(StdErr, IDS_ERROR_INVALID_ARGS);
                return EXIT_SUCCESS;
            }
            ullDesired = _wcstoui64(pszSuffix, NULL, 10);
            bDesired = TRUE;
        }
        else if (HasPrefix(argv[i], L"minimum=", &pszSuffix))
        {
            if (!IsDecString(pszSuffix))
            {
                ConResPuts(StdErr, IDS_ERROR_INVALID_ARGS);
                return EXIT_SUCCESS;
            }
            ullMinimum = _wcstoui64(pszSuffix, NULL, 10);
            bMinimum = TRUE;
        }
        else if (_wcsicmp(argv[i], L"querymax") == 0)
        {
            bQueryMax = TRUE;
        }
        else if (_wcsicmp(argv[i], L"nowait") != 0 &&
                 _wcsicmp(argv[i], L"noerr") != 0)
        {
            ConResPuts(StdErr, IDS_ERROR_INVALID_ARGS);
            return EXIT_SUCCESS;
        }
    }

    RtlInitUnicodeString(&VolumePath, CurrentVolume->DeviceName);
    Status = VolResizeQuery(&VolumePath, &Info);
    if (Status == STATUS_NOT_SUPPORTED)
    {
        ConResPuts(StdOut, IDS_SHRINK_FS_NOT_SUPPORTED);
        return EXIT_SUCCESS;
    }
    if (!NT_SUCCESS(Status))
    {
        ConResPuts(StdOut, IDS_RESIZE_INVALID_PARAMETER);
        return EXIT_SUCCESS;
    }

    if (bQueryMax)
    {
        PrintResizeSize(IDS_SHRINK_QUERYMAX, Info.MaxReclaimableBytes);
        return EXIT_SUCCESS;
    }

    if ((bDesired && ullDesired > Info.PartitionBytes / SIZE_1MB) ||
        (bMinimum && ullMinimum > Info.PartitionBytes / SIZE_1MB))
    {
        ConResPuts(StdOut, IDS_RESIZE_INVALID_PARAMETER);
        return EXIT_SUCCESS;
    }

    if (bDesired)
        ullReclaim = ullDesired * SIZE_1MB;
    else if (bMinimum)
        ullReclaim = ullMinimum * SIZE_1MB;
    else
        ullReclaim = Info.MaxReclaimableBytes;

    if ((bDesired || bMinimum) && ullReclaim == 0)
    {
        ConResPuts(StdOut, IDS_SHRINK_TOO_SMALL);
        return EXIT_SUCCESS;
    }

    if (ullReclaim > Info.MaxReclaimableBytes)
    {
        if (bDesired && bMinimum && ullMinimum * SIZE_1MB <= Info.MaxReclaimableBytes)
        {
            ullReclaim = Info.MaxReclaimableBytes;
        }
        else
        {
            ConResPuts(StdOut, IDS_SHRINK_TOO_BIG);
            return EXIT_SUCCESS;
        }
    }

    if (ullReclaim == 0)
    {
        ConResPuts(StdOut, IDS_SHRINK_TOO_SMALL);
        return EXIT_SUCCESS;
    }

    ullReclaim -= ullReclaim % Info.BytesPerSector;
    Status = VolResizeShrink(&VolumePath, ullReclaim, NULL, NULL);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("VolResizeShrink failed 0x%08lx\n", Status);
        ConResPuts(StdOut, IDS_SHRINK_TOO_BIG);
        return EXIT_SUCCESS;
    }

    PrintResizeSize(IDS_SHRINK_SUCCESS, ullReclaim);
    RefreshResizedVolume(CurrentVolume->VolumeNumber);
    return EXIT_SUCCESS;
}
