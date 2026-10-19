/*
 * PROJECT:     LiberNT storage expansion tool
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Grow the flashed LiberNT partition to fill the storage device
 * COPYRIGHT:   Copyright 2026 Ahmed Arif
 *
 * Usage: lntexpandstorage [/d N] [/p N] [/pad MB] [/min MB] [/n] [/y] [/r]
 */

#include <windows.h>
#include <winioctl.h>
#include <ntddstor.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "expandcore.h"

#ifndef FSCTL_EXTEND_VOLUME
#define FSCTL_EXTEND_VOLUME \
    CTL_CODE(FILE_DEVICE_FILE_SYSTEM, 60, METHOD_BUFFERED, FILE_ANY_ACCESS)
#endif

#define DEFAULT_PAD_MB 1
#define DEFAULT_MIN_MB 100
#define SCRATCH_SIZE   (1024 * 1024)
#define MAX_PARTITION_NUMBER 128

typedef struct _DISK_CONTEXT
{
    HANDLE Handle;
    ULONG SectorSize;
    PUCHAR Scratch;
} DISK_CONTEXT;

static void
Print(const char* Format, ...)
{
    char Line[512];
    va_list Args;

    va_start(Args, Format);
    _vsnprintf(Line, sizeof(Line) - 1, Format, Args);
    va_end(Args);
    Line[sizeof(Line) - 1] = 0;
    printf("%s", Line);
    fflush(stdout);
    OutputDebugStringA(Line);
}

static void
LogLine(void* Context, const char* Line)
{
    UNREFERENCED_PARAMETER(Context);
    Print("  %s", Line);
}

static BOOL
DiskSeekRead(DISK_CONTEXT* Disk, ULONGLONG Offset, ULONG Length, PVOID Buffer)
{
    LARGE_INTEGER Position;
    DWORD Transferred;

    Position.QuadPart = (LONGLONG)Offset;
    if (!SetFilePointerEx(Disk->Handle, Position, NULL, FILE_BEGIN))
        return FALSE;
    if (!ReadFile(Disk->Handle, Buffer, Length, &Transferred, NULL))
        return FALSE;

    return Transferred == Length;
}

static int
DeviceRead(void* Context, uint64_t Offset, uint32_t Length, void* Buffer)
{
    DISK_CONTEXT* Disk = (DISK_CONTEXT*)Context;
    ULONG SectorSize = Disk->SectorSize;
    ULONG Done = 0;

    while (Done < Length)
    {
        ULONGLONG Position = Offset + Done;
        ULONGLONG Aligned = Position & ~(ULONGLONG)(SectorSize - 1);
        ULONG Skew = (ULONG)(Position - Aligned);
        ULONG Wanted = Length - Done;
        ULONG Span;

        if (Skew + Wanted > SCRATCH_SIZE)
            Wanted = SCRATCH_SIZE - Skew;

        Span = (Skew + Wanted + SectorSize - 1) & ~(SectorSize - 1);

        if (!DiskSeekRead(Disk, Aligned, Span, Disk->Scratch))
            return 0;

        memcpy((PUCHAR)Buffer + Done, Disk->Scratch + Skew, Wanted);
        Done += Wanted;
    }

    return 1;
}

static HANDLE
OpenPartition(ULONG DriveNumber, ULONG PartitionNumber, DWORD Access)
{
    WCHAR Path[96];

    _snwprintf(Path,
               sizeof(Path) / sizeof(Path[0]),
               L"\\\\?\\GLOBALROOT\\Device\\Harddisk%lu\\Partition%lu",
               DriveNumber,
               PartitionNumber);

    return CreateFileW(Path,
                       Access,
                       FILE_SHARE_READ | FILE_SHARE_WRITE,
                       NULL,
                       OPEN_EXISTING,
                       0,
                       NULL);
}

static BOOL
QueryPartitionNumber(ULONG DriveNumber, ULONGLONG StartingOffset, PULONG PartitionNumber)
{
    PARTITION_INFORMATION_EX Information;
    HANDLE Partition;
    DWORD Returned;
    ULONG Number;
    BOOL Result;

    for (Number = 1; Number <= MAX_PARTITION_NUMBER; Number++)
    {
        Partition = OpenPartition(DriveNumber, Number, 0);
        if (Partition == INVALID_HANDLE_VALUE)
            continue;

        Result = DeviceIoControl(Partition,
                                 IOCTL_DISK_GET_PARTITION_INFO_EX,
                                 NULL,
                                 0,
                                 &Information,
                                 sizeof(Information),
                                 &Returned,
                                 NULL);
        CloseHandle(Partition);

        if (Result && (ULONGLONG)Information.StartingOffset.QuadPart == StartingOffset)
        {
            *PartitionNumber = Number;
            return TRUE;
        }
    }

    SetLastError(ERROR_NOT_FOUND);
    return FALSE;
}

static BOOL
GrowPartition(DISK_CONTEXT* Disk, ULONG PartitionNumber, ULONGLONG Bytes)
{
    DISK_GROW_PARTITION Grow;
    DWORD Returned;

    Grow.PartitionNumber = PartitionNumber;
    Grow.BytesToGrow.QuadPart = (LONGLONG)Bytes;

    return DeviceIoControl(Disk->Handle,
                           IOCTL_DISK_GROW_PARTITION,
                           &Grow,
                           sizeof(Grow),
                           NULL,
                           0,
                           &Returned,
                           NULL);
}

static BOOL
ExtendVolume(ULONG DriveNumber, ULONG PartitionNumber, ULONGLONG Sectors)
{
    LONGLONG NewSectors = (LONGLONG)Sectors;
    HANDLE Volume;
    DWORD Returned;
    DWORD Error;
    BOOL Result;

    Volume = OpenPartition(DriveNumber, PartitionNumber, GENERIC_READ | GENERIC_WRITE);
    if (Volume == INVALID_HANDLE_VALUE)
        return FALSE;

    Result = DeviceIoControl(Volume,
                             FSCTL_EXTEND_VOLUME,
                             &NewSectors,
                             sizeof(NewSectors),
                             NULL,
                             0,
                             &Returned,
                             NULL);
    Error = GetLastError();
    CloseHandle(Volume);
    SetLastError(Error);
    return Result;
}

static BOOL
QuerySystemDrive(PULONG DriveNumber, PULONG PartitionNumber)
{
    WCHAR SystemDirectory[MAX_PATH];
    WCHAR VolumePath[16];
    STORAGE_DEVICE_NUMBER Number;
    HANDLE Volume;
    DWORD Returned;
    BOOL Result;

    if (!GetSystemDirectoryW(SystemDirectory, MAX_PATH) || SystemDirectory[1] != L':')
        return FALSE;

    wcscpy(VolumePath, L"\\\\.\\A:");
    VolumePath[4] = SystemDirectory[0];

    Volume = CreateFileW(VolumePath,
                         0,
                         FILE_SHARE_READ | FILE_SHARE_WRITE,
                         NULL,
                         OPEN_EXISTING,
                         0,
                         NULL);
    if (Volume == INVALID_HANDLE_VALUE)
        return FALSE;

    Result = DeviceIoControl(Volume,
                             IOCTL_STORAGE_GET_DEVICE_NUMBER,
                             NULL,
                             0,
                             &Number,
                             sizeof(Number),
                             &Returned,
                             NULL);
    CloseHandle(Volume);

    if (!Result)
        return FALSE;

    *DriveNumber = Number.DeviceNumber;
    *PartitionNumber = Number.PartitionNumber;
    return TRUE;
}

static BOOL
QueryDiskSize(HANDLE Handle, PULONGLONG DiskSize, PULONG SectorSize)
{
    DISK_GEOMETRY_EX GeometryEx;
    DISK_GEOMETRY Geometry;
    GET_LENGTH_INFORMATION Length;
    DWORD Returned;

    if (DeviceIoControl(Handle,
                        IOCTL_DISK_GET_DRIVE_GEOMETRY_EX,
                        NULL,
                        0,
                        &GeometryEx,
                        sizeof(GeometryEx),
                        &Returned,
                        NULL) &&
        GeometryEx.DiskSize.QuadPart != 0 &&
        GeometryEx.Geometry.BytesPerSector != 0)
    {
        *DiskSize = (ULONGLONG)GeometryEx.DiskSize.QuadPart;
        *SectorSize = GeometryEx.Geometry.BytesPerSector;
        return TRUE;
    }

    if (!DeviceIoControl(Handle,
                         IOCTL_DISK_GET_DRIVE_GEOMETRY,
                         NULL,
                         0,
                         &Geometry,
                         sizeof(Geometry),
                         &Returned,
                         NULL) ||
        Geometry.BytesPerSector == 0)
    {
        return FALSE;
    }

    *SectorSize = Geometry.BytesPerSector;

    if (DeviceIoControl(Handle,
                        IOCTL_DISK_GET_LENGTH_INFO,
                        NULL,
                        0,
                        &Length,
                        sizeof(Length),
                        &Returned,
                        NULL) &&
        Length.Length.QuadPart != 0)
    {
        *DiskSize = (ULONGLONG)Length.Length.QuadPart;
        return TRUE;
    }

    *DiskSize = (ULONGLONG)Geometry.Cylinders.QuadPart *
                Geometry.TracksPerCylinder *
                Geometry.SectorsPerTrack *
                Geometry.BytesPerSector;
    return *DiskSize != 0;
}

static BOOL
RebootSystem(void)
{
    HANDLE Token;
    TOKEN_PRIVILEGES Privileges;

    if (OpenProcessToken(GetCurrentProcess(),
                         TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY,
                         &Token))
    {
        if (LookupPrivilegeValueW(NULL, L"SeShutdownPrivilege", &Privileges.Privileges[0].Luid))
        {
            Privileges.PrivilegeCount = 1;
            Privileges.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
            AdjustTokenPrivileges(Token, FALSE, &Privileges, 0, NULL, NULL);
        }
        CloseHandle(Token);
    }

    return ExitWindowsEx(EWX_REBOOT | EWX_FORCE, 0);
}

static void
Usage(void)
{
    Print("Grows the last MBR or GPT partition and its NTFS volume to the end of the disk.\n\n");
    Print("lntexpandstorage [/d N] [/p N] [/pad MB] [/min MB] [/n] [/y] [/r]\n\n");
    Print("  /d N     physical drive number (default: the drive holding %%SystemRoot%%)\n");
    Print("  /p N     partition number to grow (default: the last one on the drive)\n");
    Print("  /pad MB  free space to leave at the end of the disk (default %u)\n", DEFAULT_PAD_MB);
    Print("  /min MB  minimum gain worth acting on (default %u)\n", DEFAULT_MIN_MB);
    Print("  /n       report what would change and exit, with code 2 if nothing would\n");
    Print("  /y       do not ask for confirmation\n");
    Print("  /r       reboot once the disk has been updated\n");
}

static void
PrintSize(const char* Label, ULONGLONG Bytes)
{
    Print("  %-22s %llu bytes (%llu MB)\n",
          Label,
          (unsigned long long)Bytes,
          (unsigned long long)(Bytes / (1024 * 1024)));
}

int
main(int argc, char* argv[])
{
    DISK_CONTEXT Disk;
    EXPAND_DEVICE Device;
    EXPAND_PLAN Plan;
    WCHAR DevicePath[64];
    ULONGLONG DiskSize = 0;
    ULONG SectorSize = 0;
    ULONG DriveNumber = 0;
    ULONG SystemPartition = 0;
    ULONG PartitionNumber = 0;
    ULONG LivePartition = 0;
    ULONGLONG PadMb = DEFAULT_PAD_MB;
    ULONGLONG MinMb = DEFAULT_MIN_MB;
    BOOL HaveDrive = FALSE;
    BOOL DryRun = FALSE;
    BOOL Assume = FALSE;
    BOOL Reboot = FALSE;
    int Index;
    int Status;

    memset(&Disk, 0, sizeof(Disk));
    Disk.Handle = INVALID_HANDLE_VALUE;

    for (Index = 1; Index < argc; Index++)
    {
        const char* Argument = argv[Index];

        if (Argument[0] != '/' && Argument[0] != '-')
        {
            Usage();
            return 1;
        }

        Argument++;

        if (!_stricmp(Argument, "?") || !_stricmp(Argument, "h") || !_stricmp(Argument, "help"))
        {
            Usage();
            return 0;
        }
        else if (!_stricmp(Argument, "n"))
        {
            DryRun = TRUE;
        }
        else if (!_stricmp(Argument, "y"))
        {
            Assume = TRUE;
        }
        else if (!_stricmp(Argument, "r"))
        {
            Reboot = TRUE;
        }
        else if (Index + 1 < argc && !_stricmp(Argument, "d"))
        {
            DriveNumber = strtoul(argv[++Index], NULL, 10);
            HaveDrive = TRUE;
        }
        else if (Index + 1 < argc && !_stricmp(Argument, "p"))
        {
            PartitionNumber = strtoul(argv[++Index], NULL, 10);
        }
        else if (Index + 1 < argc && !_stricmp(Argument, "pad"))
        {
            PadMb = _strtoui64(argv[++Index], NULL, 10);
        }
        else if (Index + 1 < argc && !_stricmp(Argument, "min"))
        {
            MinMb = _strtoui64(argv[++Index], NULL, 10);
        }
        else
        {
            Usage();
            return 1;
        }
    }

    if (!HaveDrive)
    {
        if (!QuerySystemDrive(&DriveNumber, &SystemPartition))
        {
            Print("lntexpandstorage: cannot resolve the system drive, use /d\n");
            return 1;
        }
        if (PartitionNumber == 0)
            PartitionNumber = SystemPartition;
    }

    _snwprintf(DevicePath,
               sizeof(DevicePath) / sizeof(DevicePath[0]),
               L"\\\\.\\PhysicalDrive%lu",
               DriveNumber);

    Disk.Handle = CreateFileW(DevicePath,
                              GENERIC_READ | GENERIC_WRITE,
                              FILE_SHARE_READ | FILE_SHARE_WRITE,
                              NULL,
                              OPEN_EXISTING,
                              0,
                              NULL);
    if (Disk.Handle == INVALID_HANDLE_VALUE)
    {
        Print("lntexpandstorage: cannot open PhysicalDrive%lu (error %lu)\n",
              DriveNumber,
              GetLastError());
        return 1;
    }

    if (!QueryDiskSize(Disk.Handle, &DiskSize, &SectorSize))
    {
        Print("lntexpandstorage: cannot query the geometry of PhysicalDrive%lu\n", DriveNumber);
        CloseHandle(Disk.Handle);
        return 1;
    }

    Disk.SectorSize = SectorSize;
    Disk.Scratch = (PUCHAR)VirtualAlloc(NULL, SCRATCH_SIZE, MEM_COMMIT, PAGE_READWRITE);
    if (!Disk.Scratch)
    {
        Print("lntexpandstorage: out of memory\n");
        CloseHandle(Disk.Handle);
        return 1;
    }

    Device.Context = &Disk;
    Device.Read = DeviceRead;
    Device.Write = NULL;
    Device.Log = LogLine;
    Device.DiskSize = DiskSize;
    Device.SectorSize = SectorSize;

    Print("PhysicalDrive%lu: %llu bytes, %lu bytes per sector\n",
          DriveNumber,
          (unsigned long long)DiskSize,
          SectorSize);

    Status = ExpandBuildPlan(&Device,
                             PartitionNumber,
                             PadMb * 1024 * 1024,
                             MinMb * 1024 * 1024,
                             &Plan);

    if (Status == EXPAND_NOTHING_TO_DO)
    {
        Print("Partition %lu already fills the disk, nothing to do.\n", Plan.PartitionNumber);
        VirtualFree(Disk.Scratch, 0, MEM_RELEASE);
        CloseHandle(Disk.Handle);
        return DryRun ? 2 : 0;
    }

    if (Status != EXPAND_OK)
    {
        Print("lntexpandstorage: %s\n", ExpandStatusText(Status));
        VirtualFree(Disk.Scratch, 0, MEM_RELEASE);
        CloseHandle(Disk.Handle);
        return 1;
    }

    Print("Partition %lu (type 0x%02lX) at offset %llu\n",
          Plan.PartitionNumber,
          Plan.PartitionType,
          (unsigned long long)Plan.PartitionStart);
    PrintSize("current size", Plan.OldPartitionBytes);
    PrintSize("new size", Plan.NewPartitionBytes);
    PrintSize("partition gain", Plan.PartitionGain);
    PrintSize("volume gain", Plan.VolumeGain);
    Print("  %-22s %llu -> %llu (%lu bytes each)\n",
          "clusters",
          (unsigned long long)Plan.OldClusters,
          (unsigned long long)Plan.NewClusters,
          Plan.ClusterSize);

    if (DryRun)
    {
        VirtualFree(Disk.Scratch, 0, MEM_RELEASE);
        CloseHandle(Disk.Handle);
        return 0;
    }

    if (!Assume)
    {
        int Answer;

        Print("Proceed? [y/N] ");
        fflush(stdout);
        Answer = getchar();
        if (Answer != 'y' && Answer != 'Y')
        {
            Print("Aborted.\n");
            VirtualFree(Disk.Scratch, 0, MEM_RELEASE);
            CloseHandle(Disk.Handle);
            return 1;
        }
    }

    if (!QueryPartitionNumber(DriveNumber, Plan.PartitionStart, &LivePartition))
    {
        Print("lntexpandstorage: cannot find the partition at offset %llu (error %lu)\n",
              (unsigned long long)Plan.PartitionStart,
              GetLastError());
        VirtualFree(Disk.Scratch, 0, MEM_RELEASE);
        CloseHandle(Disk.Handle);
        return 1;
    }

    if (Plan.RewritePartitionTable)
    {
        if (!GrowPartition(&Disk, LivePartition, Plan.PartitionGain))
        {
            Print("lntexpandstorage: cannot grow partition %lu (error %lu)\n",
                  Plan.PartitionNumber,
                  GetLastError());
            VirtualFree(Disk.Scratch, 0, MEM_RELEASE);
            CloseHandle(Disk.Handle);
            return 1;
        }

        Print("  partition %lu now spans %llu sector(s)\n",
              Plan.PartitionNumber,
              (unsigned long long)(Plan.NewPartitionBytes / SectorSize));
    }

    VirtualFree(Disk.Scratch, 0, MEM_RELEASE);
    CloseHandle(Disk.Handle);

    if (Plan.RewriteVolume)
    {
        if (!ExtendVolume(DriveNumber, LivePartition, Plan.NewVolumeSectors + 1))
        {
            Print("lntexpandstorage: cannot extend the volume (error %lu)\n", GetLastError());
            return 1;
        }

        Print("  volume now spans %llu sector(s)\n",
              (unsigned long long)Plan.NewVolumeSectors);
    }

    Print("Done.\n");

    if (Reboot && !RebootSystem())
        Print("lntexpandstorage: reboot request failed (error %lu)\n", GetLastError());

    return 0;
}
