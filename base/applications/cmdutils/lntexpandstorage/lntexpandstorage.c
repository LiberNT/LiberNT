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
#include <ntsecapi.h>
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
#define MAX_DISK_NUMBER 32

#define BCD_OBJECTS_KEY             L"BCD00000000\\Objects"
#define BCD_ELEMENT_FORMAT_DEVICE   1
#define BCD_DEVICE_TYPE_OFFSET      0x10
#define BCD_DEVICE_STYLE_OFFSET     0x34
#define BCD_DEVICE_DISK_OFFSET      0x38
#define BCD_DEVICE_PARTITION        6
#define BCD_PARTITION_STYLE_MBR     1
#define BCD_MAX_DEVICES             32
#define BCD_MAX_DEVICE_SIZE         0x200

typedef struct _DISK_CONTEXT
{
    HANDLE Handle;
    ULONG SectorSize;
    PUCHAR Scratch;
} DISK_CONTEXT;

typedef struct _BCD_DEVICE_VALUE
{
    HKEY Key;
    DWORD Size;
    BYTE Data[BCD_MAX_DEVICE_SIZE];
} BCD_DEVICE_VALUE;

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

static PDRIVE_LAYOUT_INFORMATION_EX
QueryDriveLayout(HANDLE Handle)
{
    DWORD Size = FIELD_OFFSET(DRIVE_LAYOUT_INFORMATION_EX, PartitionEntry[MAX_PARTITION_NUMBER]);
    PDRIVE_LAYOUT_INFORMATION_EX Layout;
    DWORD Returned;

    Layout = (PDRIVE_LAYOUT_INFORMATION_EX)HeapAlloc(GetProcessHeap(), 0, Size);
    if (!Layout)
        return NULL;

    if (!DeviceIoControl(Handle,
                         IOCTL_DISK_GET_DRIVE_LAYOUT_EX,
                         NULL,
                         0,
                         Layout,
                         Size,
                         &Returned,
                         NULL))
    {
        HeapFree(GetProcessHeap(), 0, Layout);
        return NULL;
    }

    return Layout;
}

static BOOL
IsSignatureInUse(ULONG DriveNumber, ULONG Signature)
{
    PDRIVE_LAYOUT_INFORMATION_EX Layout;
    WCHAR DevicePath[64];
    HANDLE Handle;
    ULONG Number;
    BOOL InUse = FALSE;

    for (Number = 0; Number < MAX_DISK_NUMBER && !InUse; Number++)
    {
        if (Number == DriveNumber)
            continue;

        _snwprintf(DevicePath, sizeof(DevicePath) / sizeof(DevicePath[0]), L"\\\\.\\PhysicalDrive%lu", Number);
        Handle = CreateFileW(DevicePath, 0, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
        if (Handle == INVALID_HANDLE_VALUE)
            continue;

        Layout = QueryDriveLayout(Handle);
        CloseHandle(Handle);
        if (!Layout)
            continue;

        InUse = (Layout->PartitionStyle == PARTITION_STYLE_MBR && Layout->Mbr.Signature == Signature);
        HeapFree(GetProcessHeap(), 0, Layout);
    }

    return InUse;
}

static BOOL
QuerySystemPartitionDrive(PULONG DriveNumber)
{
    WCHAR Path[MAX_PATH + 16];
    WCHAR Partition[MAX_PATH];
    DWORD Size = sizeof(Partition) - sizeof(WCHAR);
    STORAGE_DEVICE_NUMBER Number;
    HKEY Key;
    HANDLE Volume;
    DWORD Type, Returned;
    BOOL Result;

    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SYSTEM\\Setup", 0, KEY_QUERY_VALUE, &Key) != ERROR_SUCCESS)
        return FALSE;

    Result = RegQueryValueExW(Key, L"SystemPartition", NULL, &Type, (LPBYTE)Partition, &Size) == ERROR_SUCCESS &&
             Type == REG_SZ;
    RegCloseKey(Key);
    if (!Result)
        return FALSE;

    Partition[Size / sizeof(WCHAR)] = UNICODE_NULL;
    _snwprintf(Path, sizeof(Path) / sizeof(Path[0]), L"\\\\?\\GLOBALROOT%s", Partition);
    Path[sizeof(Path) / sizeof(Path[0]) - 1] = UNICODE_NULL;

    Volume = CreateFileW(Path, 0, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
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
    return TRUE;
}

static void
ScheduleUniqueRetry(void)
{
    WCHAR Path[MAX_PATH];
    WCHAR Command[MAX_PATH + 8];
    DWORD Length;
    HKEY Key;

    Length = GetModuleFileNameW(NULL, Path, sizeof(Path) / sizeof(Path[0]));
    if (Length == 0 || Length >= sizeof(Path) / sizeof(Path[0]))
        return;

    _snwprintf(Command, sizeof(Command) / sizeof(Command[0]), L"\"%s\" /u", Path);
    Command[sizeof(Command) / sizeof(Command[0]) - 1] = UNICODE_NULL;

    if (RegCreateKeyExW(HKEY_LOCAL_MACHINE,
                        L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\RunOnce",
                        0,
                        NULL,
                        0,
                        KEY_SET_VALUE,
                        NULL,
                        &Key,
                        NULL) != ERROR_SUCCESS)
    {
        return;
    }

    RegSetValueExW(Key,
                   L"LiberNTDiskSignature",
                   0,
                   REG_SZ,
                   (const BYTE*)Command,
                   (DWORD)((wcslen(Command) + 1) * sizeof(WCHAR)));
    RegCloseKey(Key);
}

static void
EnablePrivilege(LPCWSTR Name)
{
    TOKEN_PRIVILEGES Privileges;
    HANDLE Token;

    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &Token))
        return;

    if (LookupPrivilegeValueW(NULL, Name, &Privileges.Privileges[0].Luid))
    {
        Privileges.PrivilegeCount = 1;
        Privileges.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
        AdjustTokenPrivileges(Token, FALSE, &Privileges, 0, NULL, NULL);
    }
    CloseHandle(Token);
}

static LONG
CollectBcdDevices(ULONG Signature, BCD_DEVICE_VALUE* Devices, ULONG MaxDevices, PULONG DeviceCount)
{
    WCHAR ObjectName[64];
    WCHAR ElementName[16];
    WCHAR Path[128];
    HKEY Objects, Elements, Element;
    DWORD ObjectIndex, ElementIndex, Length, Type;
    ULONG Count = 0;
    BCD_DEVICE_VALUE* Device;
    LONG Error;

    *DeviceCount = 0;
    Error = RegOpenKeyExW(HKEY_LOCAL_MACHINE, BCD_OBJECTS_KEY, REG_OPTION_BACKUP_RESTORE, KEY_ENUMERATE_SUB_KEYS, &Objects);
    if (Error != ERROR_SUCCESS)
        return Error;

    for (ObjectIndex = 0; Error == ERROR_SUCCESS; ObjectIndex++)
    {
        Length = sizeof(ObjectName) / sizeof(ObjectName[0]);
        if (RegEnumKeyExW(Objects, ObjectIndex, ObjectName, &Length, NULL, NULL, NULL, NULL) != ERROR_SUCCESS)
            break;

        _snwprintf(Path, sizeof(Path) / sizeof(Path[0]), L"%s\\Elements", ObjectName);
        Path[sizeof(Path) / sizeof(Path[0]) - 1] = UNICODE_NULL;
        if (RegOpenKeyExW(Objects, Path, REG_OPTION_BACKUP_RESTORE, KEY_ENUMERATE_SUB_KEYS, &Elements) != ERROR_SUCCESS)
            continue;

        for (ElementIndex = 0; Error == ERROR_SUCCESS; ElementIndex++)
        {
            Length = sizeof(ElementName) / sizeof(ElementName[0]);
            if (RegEnumKeyExW(Elements, ElementIndex, ElementName, &Length, NULL, NULL, NULL, NULL) != ERROR_SUCCESS)
                break;

            if (((wcstoul(ElementName, NULL, 16) >> 24) & 0xF) != BCD_ELEMENT_FORMAT_DEVICE)
                continue;

            if (Count == MaxDevices)
            {
                Error = ERROR_MORE_DATA;
                break;
            }

            Error = RegOpenKeyExW(Elements,
                                  ElementName,
                                  REG_OPTION_BACKUP_RESTORE,
                                  KEY_QUERY_VALUE | KEY_SET_VALUE,
                                  &Element);
            if (Error != ERROR_SUCCESS)
                break;

            Device = &Devices[Count];
            Device->Size = sizeof(Device->Data);
            if (RegQueryValueExW(Element, L"Element", NULL, &Type, Device->Data, &Device->Size) == ERROR_SUCCESS &&
                Type == REG_BINARY &&
                Device->Size >= BCD_DEVICE_DISK_OFFSET + sizeof(ULONG) &&
                *(PULONG)(Device->Data + BCD_DEVICE_TYPE_OFFSET) == BCD_DEVICE_PARTITION &&
                *(PULONG)(Device->Data + BCD_DEVICE_STYLE_OFFSET) == BCD_PARTITION_STYLE_MBR &&
                *(PULONG)(Device->Data + BCD_DEVICE_DISK_OFFSET) == Signature)
            {
                Device->Key = Element;
                Count++;
            }
            else
            {
                RegCloseKey(Element);
            }
        }

        RegCloseKey(Elements);
    }

    RegCloseKey(Objects);
    *DeviceCount = Count;
    return Error;
}

static BOOL
SetBcdDeviceSignature(BCD_DEVICE_VALUE* Devices, ULONG Count, ULONG Signature)
{
    ULONG Index;

    for (Index = 0; Index < Count; Index++)
    {
        *(PULONG)(Devices[Index].Data + BCD_DEVICE_DISK_OFFSET) = Signature;
        if (RegSetValueExW(Devices[Index].Key,
                           L"Element",
                           0,
                           REG_BINARY,
                           Devices[Index].Data,
                           Devices[Index].Size) != ERROR_SUCCESS)
        {
            return FALSE;
        }
    }

    return TRUE;
}

static void
FlushBcd(void)
{
    HKEY Objects;

    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, BCD_OBJECTS_KEY, REG_OPTION_BACKUP_RESTORE, KEY_QUERY_VALUE, &Objects) == ERROR_SUCCESS)
    {
        RegFlushKey(Objects);
        RegCloseKey(Objects);
    }
}

static int
MakeDiskUnique(ULONG DriveNumber)
{
    PDRIVE_LAYOUT_INFORMATION_EX Layout;
    BCD_DEVICE_VALUE* Devices;
    WCHAR DevicePath[64];
    HANDLE Handle;
    DWORD Returned;
    ULONG OldSignature, Signature, Count, Index, BootDrive;
    LONG Error;
    int Result = 1;

    _snwprintf(DevicePath, sizeof(DevicePath) / sizeof(DevicePath[0]), L"\\\\.\\PhysicalDrive%lu", DriveNumber);
    Handle = CreateFileW(DevicePath,
                         GENERIC_READ | GENERIC_WRITE,
                         FILE_SHARE_READ | FILE_SHARE_WRITE,
                         NULL,
                         OPEN_EXISTING,
                         0,
                         NULL);
    if (Handle == INVALID_HANDLE_VALUE)
    {
        Print("lntexpandstorage: cannot open PhysicalDrive%lu (error %lu)\n", DriveNumber, GetLastError());
        return 1;
    }

    Layout = QueryDriveLayout(Handle);
    if (!Layout)
    {
        Print("lntexpandstorage: cannot read the layout of PhysicalDrive%lu (error %lu)\n", DriveNumber, GetLastError());
        CloseHandle(Handle);
        return 1;
    }

    if (Layout->PartitionStyle != PARTITION_STYLE_MBR || Layout->Mbr.Signature != LIBERNT_STANDARD_SIGNATURE)
    {
        Print("PhysicalDrive%lu already has its own disk signature.\n", DriveNumber);
        HeapFree(GetProcessHeap(), 0, Layout);
        CloseHandle(Handle);
        return 2;
    }

    if (!QuerySystemPartitionDrive(&BootDrive) || BootDrive != DriveNumber)
    {
        Print("lntexpandstorage: the boot configuration in use is not on PhysicalDrive%lu, its signature stays\n",
              DriveNumber);
        ScheduleUniqueRetry();
        HeapFree(GetProcessHeap(), 0, Layout);
        CloseHandle(Handle);
        return 1;
    }

    do
    {
        if (!RtlGenRandom(&Signature, sizeof(Signature)))
        {
            Print("lntexpandstorage: cannot generate a disk signature\n");
            HeapFree(GetProcessHeap(), 0, Layout);
            CloseHandle(Handle);
            return 1;
        }
    } while (Signature == 0 || Signature == LIBERNT_STANDARD_SIGNATURE || IsSignatureInUse(DriveNumber, Signature));

    Devices = (BCD_DEVICE_VALUE*)HeapAlloc(GetProcessHeap(), 0, BCD_MAX_DEVICES * sizeof(*Devices));
    if (!Devices)
    {
        Print("lntexpandstorage: out of memory\n");
        HeapFree(GetProcessHeap(), 0, Layout);
        CloseHandle(Handle);
        return 1;
    }

    OldSignature = Layout->Mbr.Signature;
    EnablePrivilege(L"SeBackupPrivilege");
    EnablePrivilege(L"SeRestorePrivilege");
    Error = CollectBcdDevices(OldSignature, Devices, BCD_MAX_DEVICES, &Count);
    if (Error != ERROR_SUCCESS)
    {
        Print("lntexpandstorage: cannot read the boot configuration (error %ld)\n", Error);
        goto Done;
    }

    if (!SetBcdDeviceSignature(Devices, Count, Signature))
    {
        Print("lntexpandstorage: cannot update the boot configuration (error %lu)\n", GetLastError());
        SetBcdDeviceSignature(Devices, Count, OldSignature);
        goto Done;
    }

    Layout->Mbr.Signature = Signature;
    if (!DeviceIoControl(Handle,
                         IOCTL_DISK_SET_DRIVE_LAYOUT_EX,
                         Layout,
                         FIELD_OFFSET(DRIVE_LAYOUT_INFORMATION_EX, PartitionEntry[Layout->PartitionCount]),
                         NULL,
                         0,
                         &Returned,
                         NULL))
    {
        Print("lntexpandstorage: cannot write the disk signature (error %lu)\n", GetLastError());
        SetBcdDeviceSignature(Devices, Count, OldSignature);
        goto Done;
    }

    Print("PhysicalDrive%lu: disk signature %08lX -> %08lX, %lu boot configuration device(s) updated\n",
          DriveNumber,
          OldSignature,
          Signature,
          Count);
    Result = 0;

Done:
    FlushBcd();
    for (Index = 0; Index < Count; Index++)
        RegCloseKey(Devices[Index].Key);
    HeapFree(GetProcessHeap(), 0, Devices);
    HeapFree(GetProcessHeap(), 0, Layout);
    CloseHandle(Handle);
    return Result;
}

static void
Usage(void)
{
    Print("Grows the last MBR or GPT partition and its NTFS volume to the end of the disk.\n\n");
    Print("lntexpandstorage [/d N] [/p N] [/pad MB] [/min MB] [/n] [/y] [/r] [/u]\n\n");
    Print("  /d N     physical drive number (default: the drive holding %%SystemRoot%%)\n");
    Print("  /p N     partition number to grow (default: the last one on the drive)\n");
    Print("  /pad MB  free space to leave at the end of the disk (default %u)\n", DEFAULT_PAD_MB);
    Print("  /min MB  minimum gain worth acting on (default %u)\n", DEFAULT_MIN_MB);
    Print("  /n       report what would change and exit, with code 2 if nothing would\n");
    Print("  /y       do not ask for confirmation\n");
    Print("  /r       reboot once the disk has been updated\n");
    Print("  /u       give a disk that still has the image's disk signature its own, and exit\n");
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
    BOOL Unique = FALSE;
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
        else if (!_stricmp(Argument, "u"))
        {
            Unique = TRUE;
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

    if (Unique)
        return MakeDiskUnique(DriveNumber);

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
