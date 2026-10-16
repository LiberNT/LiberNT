/*
 * PROJECT:     LiberNT Setup
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     End-to-end probes of the Setup disk path on blank test disks
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "reactos.h"
#include <ntddstor.h>
#include <fslib/volresize.h>
#include <ndk/cmfuncs.h>
#include <ndk/obfuncs.h>
#include <ndk/setypes.h>
#include <objbase.h>

#define SELFTEST_MEGABYTE       (1024ULL * 1024ULL)
#define SELFTEST_GIGABYTE       (1024ULL * SELFTEST_MEGABYTE)
#define SELFTEST_PATTERN_SIZE   (8 * 1024 * 1024)
#define SELFTEST_MAX_DISKS      2
#define SELFTEST_BOOT_ORDER_MAX 128

typedef struct _SELFTEST_CONTEXT
{
    PSETUPDATA SetupData;
    PPARTLIST List;
    ULONG Failures;
    ULONG Passes;
    HANDLE LogFile;
    PVOLENTRY SystemVolume;
    PVOLENTRY InstallVolume;
} SELFTEST_CONTEXT, *PSELFTEST_CONTEXT;

static ULONG SelfTestVolumesInUse;

static const GUID SelfTestEfiGuid =
{ 0xC12A7328, 0xF81F, 0x11D2, { 0xBA, 0x4B, 0x00, 0xA0, 0xC9, 0x3E, 0xC9, 0x3B } };
static const GUID SelfTestMsrGuid =
{ 0xE3C9E316, 0x0B5C, 0x4DB8, { 0x81, 0x7D, 0xF9, 0x2D, 0xF0, 0x02, 0x15, 0xAE } };
static const GUID SelfTestBasicDataGuid =
{ 0xEBD0A0A2, 0xB9E5, 0x4433, { 0x87, 0xC0, 0x68, 0xB6, 0xB7, 0x26, 0x99, 0xC7 } };

static VOID
SelfTestLog(
    _In_ PSELFTEST_CONTEXT Context,
    _In_ PCWSTR Format,
    ...)
{
    WCHAR Line[512];
    CHAR Ansi[512];
    va_list Args;
    DWORD Written;
    int Length;

    va_start(Args, Format);
    StringCchVPrintfW(Line, ARRAYSIZE(Line), Format, Args);
    va_end(Args);
    StringCchCatW(Line, ARRAYSIZE(Line), L"\n");

    OutputDebugStringW(Line);
    if (Context->LogFile != INVALID_HANDLE_VALUE)
    {
        Length = WideCharToMultiByte(CP_ACP, 0, Line, -1, Ansi, sizeof(Ansi), NULL, NULL);
        if (Length > 1)
            WriteFile(Context->LogFile, Ansi, Length - 1, &Written, NULL);
    }
}

static BOOLEAN
SelfTestCheck(
    _In_ PSELFTEST_CONTEXT Context,
    _In_ BOOLEAN Condition,
    _In_ PCWSTR Probe,
    _In_ PCWSTR Format,
    ...)
{
    WCHAR Detail[384];
    va_list Args;

    va_start(Args, Format);
    StringCchVPrintfW(Detail, ARRAYSIZE(Detail), Format, Args);
    va_end(Args);

    SelfTestLog(Context, L"SETUPTEST %s %s %s", Condition ? L"PASS" : L"FAIL", Probe, Detail);
    if (Condition)
        Context->Passes++;
    else
        Context->Failures++;
    return Condition;
}

static BOOLEAN
NTAPI
SelfTestChkdskCallback(
    _In_ CALLBACKCOMMAND Command,
    _In_ ULONG Modifier,
    _In_ PVOID Argument)
{
    if (Command == VOLUMEINUSE)
        SelfTestVolumesInUse++;
    return TRUE;
}

static FSVOL_OP
CALLBACK
SelfTestFsVolCallback(
    _In_opt_ PVOID Context,
    _In_ FSVOLNOTIFY FormatStatus,
    _In_ ULONG_PTR Param1,
    _In_ ULONG_PTR Param2)
{
    PSELFTEST_CONTEXT Test = (PSELFTEST_CONTEXT)Context;

    UNREFERENCED_PARAMETER(Param2);

    switch (FormatStatus)
    {
        case ChangeSystemPartition:
            return FSVOL_DOIT;

        case FSVOLNOTIFY_PARTITIONERROR:
            SelfTestLog(Test, L"SETUPTEST INFO partition error 0x%08lx", (ULONG)Param1);
            return FSVOL_ABORT;

        case FSVOLNOTIFY_STARTFORMAT:
        {
            PFORMAT_VOLUME_INFO FmtInfo = (PFORMAT_VOLUME_INFO)Param1;

            if (FmtInfo->Volume == Test->SystemVolume)
            {
                FmtInfo->FileSystemName = L"FAT32";
                FmtInfo->Label = L"System";
            }
            else if (FmtInfo->Volume == Test->InstallVolume)
            {
                FmtInfo->FileSystemName = L"NTFS";
                FmtInfo->Label = NULL;
            }
            else
            {
                return FSVOL_SKIP;
            }

            FmtInfo->MediaFlag = FMIFS_HARDDISK;
            FmtInfo->QuickFormat = TRUE;
            FmtInfo->ClusterSize = 0;
            FmtInfo->Callback = NULL;
            return FSVOL_DOIT;
        }

        case FSVOLNOTIFY_FORMATERROR:
        {
            PFORMAT_VOLUME_INFO FmtInfo = (PFORMAT_VOLUME_INFO)Param1;

            SelfTestLog(Test, L"SETUPTEST INFO format error 0x%08lx on %s",
                        FmtInfo->ErrorStatus, FmtInfo->Volume->Info.DeviceName);
            return FSVOL_ABORT;
        }

        case FSVOLNOTIFY_STARTCHECK:
        {
            PCHECK_VOLUME_INFO ChkInfo = (PCHECK_VOLUME_INFO)Param1;

            ChkInfo->FixErrors = TRUE;
            ChkInfo->Verbose = FALSE;
            ChkInfo->CheckOnlyIfDirty = TRUE;
            ChkInfo->ScanDrive = FALSE;
            ChkInfo->Callback = SelfTestChkdskCallback;
            return FSVOL_DOIT;
        }

        case FSVOLNOTIFY_CHECKERROR:
        {
            PCHECK_VOLUME_INFO ChkInfo = (PCHECK_VOLUME_INFO)Param1;

            SelfTestLog(Test, L"SETUPTEST INFO check error 0x%08lx on %s",
                        ChkInfo->ErrorStatus, ChkInfo->Volume->Info.DeviceName);
            return FSVOL_SKIP;
        }

        default:
            return FSVOL_DOIT;
    }
}

static PDISKENTRY
SelfTestFindDisk(
    _In_ PPARTLIST List,
    _In_ ULONG DiskNumber)
{
    PLIST_ENTRY Entry;
    PDISKENTRY DiskEntry;

    for (Entry = List->DiskListHead.Flink; Entry != &List->DiskListHead; Entry = Entry->Flink)
    {
        DiskEntry = CONTAINING_RECORD(Entry, DISKENTRY, ListEntry);
        if (DiskEntry->DiskNumber == DiskNumber)
            return DiskEntry;
    }

    return NULL;
}

static PPARTENTRY
SelfTestFirstFreeRegion(
    _In_ PDISKENTRY DiskEntry)
{
    PLIST_ENTRY Entry;
    PPARTENTRY PartEntry;

    for (Entry = DiskEntry->PrimaryPartListHead.Flink;
         Entry != &DiskEntry->PrimaryPartListHead;
         Entry = Entry->Flink)
    {
        PartEntry = CONTAINING_RECORD(Entry, PARTENTRY, ListEntry);
        if (!PartEntry->IsPartitioned)
            return PartEntry;
    }

    return NULL;
}

static BOOLEAN
SelfTestRefreshList(
    _In_ PSELFTEST_CONTEXT Context)
{
    if (Context->List)
        DestroyPartitionList(Context->List);

    Context->List = CreatePartitionList();
    Context->SetupData->PartitionList = Context->List;
    return (Context->List != NULL);
}

static PDRIVE_LAYOUT_INFORMATION_EX
SelfTestReadLayout(
    _In_ ULONG DiskNumber)
{
    WCHAR Path[64];
    HANDLE Disk;
    PDRIVE_LAYOUT_INFORMATION_EX Layout;
    DWORD Size = FIELD_OFFSET(DRIVE_LAYOUT_INFORMATION_EX, PartitionEntry[128]);
    DWORD Returned;

    StringCchPrintfW(Path, ARRAYSIZE(Path), L"\\\\.\\PhysicalDrive%lu", DiskNumber);
    Disk = CreateFileW(Path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    if (Disk == INVALID_HANDLE_VALUE)
        return NULL;

    Layout = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, Size);
    if (Layout &&
        !DeviceIoControl(Disk, IOCTL_DISK_GET_DRIVE_LAYOUT_EX, NULL, 0, Layout, Size, &Returned, NULL))
    {
        HeapFree(GetProcessHeap(), 0, Layout);
        Layout = NULL;
    }

    CloseHandle(Disk);
    return Layout;
}

static ULONG
SelfTestCountType(
    _In_ PDRIVE_LAYOUT_INFORMATION_EX Layout,
    _In_ const GUID *Type,
    _Out_opt_ PPARTITION_INFORMATION_EX *First)
{
    ULONG Index;
    ULONG Count = 0;

    if (First)
        *First = NULL;

    for (Index = 0; Index < Layout->PartitionCount; Index++)
    {
        if (IsEqualGUID(&Layout->PartitionEntry[Index].Gpt.PartitionType, Type))
        {
            if (Count == 0 && First)
                *First = &Layout->PartitionEntry[Index];
            Count++;
        }
    }

    return Count;
}

static BOOLEAN
SelfTestGetFileSystem(
    _In_ PCWSTR DeviceName,
    _Out_writes_(cchName) PWSTR Name,
    _In_ DWORD cchName)
{
    struct
    {
        FILE_FS_ATTRIBUTE_INFORMATION;
        WCHAR Buffer[MAX_PATH];
    } Attribute;
    OBJECT_ATTRIBUTES ObjectAttributes;
    IO_STATUS_BLOCK IoStatusBlock;
    UNICODE_STRING Path;
    WCHAR Root[MAX_PATH];
    HANDLE Handle;
    NTSTATUS Status;

    Name[0] = UNICODE_NULL;
    StringCchPrintfW(Root, ARRAYSIZE(Root), L"%s\\", DeviceName);
    RtlInitUnicodeString(&Path, Root);
    InitializeObjectAttributes(&ObjectAttributes, &Path, OBJ_CASE_INSENSITIVE, NULL, NULL);
    Status = NtOpenFile(&Handle, SYNCHRONIZE | FILE_READ_ATTRIBUTES, &ObjectAttributes, &IoStatusBlock,
                        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                        FILE_DIRECTORY_FILE | FILE_SYNCHRONOUS_IO_NONALERT);
    if (!NT_SUCCESS(Status))
        return FALSE;

    Status = NtQueryVolumeInformationFile(Handle, &IoStatusBlock, &Attribute, sizeof(Attribute),
                                          FileFsAttributeInformation);
    NtClose(Handle);
    if (!NT_SUCCESS(Status))
        return FALSE;

    StringCchCopyNW(Name, cchName, Attribute.FileSystemName, Attribute.FileSystemNameLength / sizeof(WCHAR));
    return TRUE;
}

static BOOLEAN
SelfTestVolumeIdle(
    _In_ PSELFTEST_CONTEXT Context,
    _In_ PCWSTR Probe,
    _In_ PCWSTR DeviceName)
{
    OBJECT_ATTRIBUTES ObjectAttributes;
    IO_STATUS_BLOCK IoStatusBlock;
    UNICODE_STRING Path;
    HANDLE Handle;
    NTSTATUS Status;
    ULONG Attempt;

    RtlInitUnicodeString(&Path, DeviceName);
    InitializeObjectAttributes(&ObjectAttributes, &Path, OBJ_CASE_INSENSITIVE, NULL, NULL);
    for (Attempt = 0; Attempt < 8; Attempt++)
    {
        if (Attempt != 0)
            Sleep(1000);
        Status = NtOpenFile(&Handle, FILE_GENERIC_READ | FILE_GENERIC_WRITE, &ObjectAttributes, &IoStatusBlock,
                            FILE_SHARE_READ | FILE_SHARE_WRITE, FILE_SYNCHRONOUS_IO_NONALERT);
        if (NT_SUCCESS(Status))
        {
            Status = NtFsControlFile(Handle, NULL, NULL, NULL, &IoStatusBlock, FSCTL_LOCK_VOLUME, NULL, 0, NULL, 0);
            if (NT_SUCCESS(Status))
                NtFsControlFile(Handle, NULL, NULL, NULL, &IoStatusBlock, FSCTL_UNLOCK_VOLUME, NULL, 0, NULL, 0);
            NtClose(Handle);
        }
        if (NT_SUCCESS(Status))
            break;
    }
    return SelfTestCheck(Context, NT_SUCCESS(Status), Probe, L"%s lockable after %lu attempts 0x%08lx",
                         DeviceName, Attempt + 1, Status);
}

static BYTE
SelfTestPatternByte(
    _In_ ULONG Offset)
{
    return (BYTE)((Offset * 2654435761UL) >> 24);
}

static BOOLEAN
SelfTestWritePattern(
    _In_ PCWSTR DeviceName)
{
    WCHAR Path[MAX_PATH];
    PBYTE Buffer;
    HANDLE File;
    DWORD Written;
    ULONG Index;
    BOOL Ok;

    Buffer = HeapAlloc(GetProcessHeap(), 0, SELFTEST_PATTERN_SIZE);
    if (!Buffer)
        return FALSE;

    for (Index = 0; Index < SELFTEST_PATTERN_SIZE; Index++)
        Buffer[Index] = SelfTestPatternByte(Index);

    StringCchPrintfW(Path, ARRAYSIZE(Path), L"\\\\?\\GLOBALROOT%s\\setuptest.bin", DeviceName);
    File = CreateFileW(Path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_FLAG_WRITE_THROUGH, NULL);
    Ok = (File != INVALID_HANDLE_VALUE);
    if (Ok)
    {
        Ok = WriteFile(File, Buffer, SELFTEST_PATTERN_SIZE, &Written, NULL) &&
             Written == SELFTEST_PATTERN_SIZE &&
             FlushFileBuffers(File);
        CloseHandle(File);
    }

    HeapFree(GetProcessHeap(), 0, Buffer);
    return Ok;
}

static BOOLEAN
SelfTestVerifyPattern(
    _In_ PCWSTR DeviceName)
{
    WCHAR Path[MAX_PATH];
    PBYTE Buffer;
    HANDLE File;
    DWORD Read;
    ULONG Index;
    BOOL Ok;

    Buffer = HeapAlloc(GetProcessHeap(), 0, SELFTEST_PATTERN_SIZE);
    if (!Buffer)
        return FALSE;

    StringCchPrintfW(Path, ARRAYSIZE(Path), L"\\\\?\\GLOBALROOT%s\\setuptest.bin", DeviceName);
    File = CreateFileW(Path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    Ok = (File != INVALID_HANDLE_VALUE);
    if (Ok)
    {
        Ok = ReadFile(File, Buffer, SELFTEST_PATTERN_SIZE, &Read, NULL) && Read == SELFTEST_PATTERN_SIZE;
        CloseHandle(File);
    }

    for (Index = 0; Ok && Index < SELFTEST_PATTERN_SIZE; Index++)
    {
        if (Buffer[Index] != SelfTestPatternByte(Index))
            Ok = FALSE;
    }

    HeapFree(GetProcessHeap(), 0, Buffer);
    return Ok;
}

static BOOLEAN
SelfTestQuery(
    _In_ PCWSTR DeviceName,
    _Out_ PVOLRESIZE_INFO Info)
{
    UNICODE_STRING Path;

    RtlInitUnicodeString(&Path, DeviceName);
    return NT_SUCCESS(VolResizeQuery(&Path, Info));
}

static BOOLEAN
SelfTestLayoutDisk(
    _In_ PSELFTEST_CONTEXT Context,
    _In_ ULONG DiskNumber,
    _In_ BOOLEAN ExpectOwnEsp,
    _In_ PCWSTR Name,
    _Out_ PWSTR InstallDevice,
    _In_ SIZE_T cchInstallDevice,
    _Out_ PWSTR SystemDevice,
    _In_ SIZE_T cchSystemDevice)
{
    PDISKENTRY DiskEntry;
    PPARTENTRY Region;
    PPARTENTRY EfiPart = NULL;
    PPARTENTRY ReservedPart = NULL;
    PPARTENTRY SystemPart = NULL;
    PDRIVE_LAYOUT_INFORMATION_EX Layout;
    PPARTITION_INFORMATION_EX Esp, Msr, Data;
    WCHAR Probe[32];
    WCHAR FileSystem[MAX_PATH];
    ULONGLONG DiskBytes;
    BOOLEAN NeedsSystem;
    BOOLEAN Ok;

    InstallDevice[0] = UNICODE_NULL;
    SystemDevice[0] = UNICODE_NULL;

    if (!SelfTestRefreshList(Context))
        return SelfTestCheck(Context, FALSE, L"list", L"CreatePartitionList failed");

    DiskEntry = SelfTestFindDisk(Context->List, DiskNumber);
    Region = DiskEntry ? SelfTestFirstFreeRegion(DiskEntry) : NULL;
    StringCchPrintfW(Probe, ARRAYSIZE(Probe), L"%s.region", Name);
    if (!SelfTestCheck(Context, Region != NULL && DiskEntry->DiskStyle == PARTITION_STYLE_GPT,
                       Probe, L"disk %lu style %lu", DiskNumber, DiskEntry ? (ULONG)DiskEntry->DiskStyle : 99))
    {
        return FALSE;
    }

    DiskBytes = DiskEntry->SectorCount.QuadPart * DiskEntry->BytesPerSector;
    NeedsSystem = GptDiskNeedsSystemPartitions(Region);
    StringCchPrintfW(Probe, ARRAYSIZE(Probe), L"%s.needs", Name);
    SelfTestCheck(Context, NeedsSystem, Probe, L"GptDiskNeedsSystemPartitions %u", NeedsSystem);

    if (NeedsSystem &&
        (!CreateGptSystemPartitions(Context->List, &Region, &EfiPart, &ReservedPart) || !Region))
    {
        StringCchPrintfW(Probe, ARRAYSIZE(Probe), L"%s.system", Name);
        return SelfTestCheck(Context, FALSE, Probe, L"CreateGptSystemPartitions failed");
    }

    StringCchPrintfW(Probe, ARRAYSIZE(Probe), L"%s.espplan", Name);
    SelfTestCheck(Context, (EfiPart != NULL) == ExpectOwnEsp, Probe,
                  L"ESP planned %u expected %u", EfiPart != NULL, ExpectOwnEsp);

    StringCchPrintfW(Probe, ARRAYSIZE(Probe), L"%s.create", Name);
    if (!SelfTestCheck(Context, CreatePartition(Context->List, Region, 0, 0) && Region->Volume != NULL,
                       Probe, L"data partition"))
    {
        return FALSE;
    }

    Ok = InitSystemPartition(Context->List, Region, &SystemPart, SelfTestFsVolCallback, Context);
    StringCchPrintfW(Probe, ARRAYSIZE(Probe), L"%s.sysinit", Name);
    if (!SelfTestCheck(Context, Ok && SystemPart != NULL && SystemPart->Volume != NULL, Probe,
                       L"InitSystemPartition disk %lu partition %lu",
                       SystemPart ? SystemPart->DiskEntry->DiskNumber : 0,
                       SystemPart ? SystemPart->OnDiskPartitionNumber : 0))
    {
        return FALSE;
    }

    StringCchPrintfW(Probe, ARRAYSIZE(Probe), L"%s.sysdisk", Name);
    SelfTestCheck(Context,
                  ExpectOwnEsp ? (SystemPart == EfiPart) : (SystemPart->DiskEntry->DiskNumber != DiskNumber),
                  Probe, L"system partition on disk %lu", SystemPart->DiskEntry->DiskNumber);

    Context->SystemVolume = SystemPart->Volume;
    Context->InstallVolume = Region->Volume;
    SelfTestVolumesInUse = 0;
    Ok = FsVolCommitOpsQueue(Context->List, Context->SystemVolume, Context->InstallVolume,
                             SelfTestFsVolCallback, Context);
    StringCchPrintfW(Probe, ARRAYSIZE(Probe), L"%s.commit", Name);
    if (!SelfTestCheck(Context, Ok, Probe, L"FsVolCommitOpsQueue"))
        return FALSE;
    SelfTestLog(Context, L"SETUPTEST INFO %s %lu volumes in use during checks", Probe, SelfTestVolumesInUse);

    StringCchCopyW(InstallDevice, cchInstallDevice, Context->InstallVolume->Info.DeviceName);
    StringCchCopyW(SystemDevice, cchSystemDevice, Context->SystemVolume->Info.DeviceName);

    Layout = SelfTestReadLayout(DiskNumber);
    StringCchPrintfW(Probe, ARRAYSIZE(Probe), L"%s.layout", Name);
    if (!SelfTestCheck(Context, Layout != NULL && Layout->PartitionStyle == PARTITION_STYLE_GPT,
                       Probe, L"IOCTL_DISK_GET_DRIVE_LAYOUT_EX"))
    {
        if (Layout)
            HeapFree(GetProcessHeap(), 0, Layout);
        return FALSE;
    }

    SelfTestCheck(Context, SelfTestCountType(Layout, &SelfTestEfiGuid, &Esp) == (ExpectOwnEsp ? 1UL : 0UL),
                  Probe, L"ESP count %lu", SelfTestCountType(Layout, &SelfTestEfiGuid, NULL));
    SelfTestCheck(Context, SelfTestCountType(Layout, &SelfTestMsrGuid, &Msr) == 1,
                  Probe, L"MSR count %lu", SelfTestCountType(Layout, &SelfTestMsrGuid, NULL));
    SelfTestCheck(Context, SelfTestCountType(Layout, &SelfTestBasicDataGuid, &Data) == 1,
                  Probe, L"data count %lu", SelfTestCountType(Layout, &SelfTestBasicDataGuid, NULL));

    if (ExpectOwnEsp && Esp)
    {
        SelfTestCheck(Context,
                      Esp->StartingOffset.QuadPart == SELFTEST_MEGABYTE &&
                      Esp->PartitionLength.QuadPart == 100 * SELFTEST_MEGABYTE,
                      Probe, L"ESP at %I64u size %I64u",
                      Esp->StartingOffset.QuadPart, Esp->PartitionLength.QuadPart);
    }

    if (Msr)
    {
        SelfTestCheck(Context,
                      Msr->PartitionLength.QuadPart == 16 * SELFTEST_MEGABYTE &&
                      Msr->StartingOffset.QuadPart == (ExpectOwnEsp ? 101 : 1) * SELFTEST_MEGABYTE,
                      Probe, L"MSR at %I64u size %I64u",
                      Msr->StartingOffset.QuadPart, Msr->PartitionLength.QuadPart);
    }

    if (Data && Msr)
    {
        SelfTestCheck(Context,
                      Data->StartingOffset.QuadPart == Msr->StartingOffset.QuadPart + Msr->PartitionLength.QuadPart &&
                      Data->StartingOffset.QuadPart % SELFTEST_MEGABYTE == 0 &&
                      Data->StartingOffset.QuadPart + Data->PartitionLength.QuadPart + SELFTEST_MEGABYTE >= DiskBytes - 2 * SELFTEST_MEGABYTE,
                      Probe, L"data at %I64u size %I64u disk %I64u",
                      Data->StartingOffset.QuadPart, Data->PartitionLength.QuadPart, DiskBytes);
    }

    HeapFree(GetProcessHeap(), 0, Layout);

    StringCchPrintfW(Probe, ARRAYSIZE(Probe), L"%s.format", Name);
    SelfTestCheck(Context,
                  SelfTestGetFileSystem(InstallDevice, FileSystem, ARRAYSIZE(FileSystem)) &&
                  _wcsicmp(FileSystem, L"NTFS") == 0,
                  Probe, L"install volume %s file system %s", InstallDevice, FileSystem);
    if (ExpectOwnEsp)
    {
        FileSystem[0] = UNICODE_NULL;
        SelfTestCheck(Context,
                      SelfTestGetFileSystem(SystemDevice, FileSystem, ARRAYSIZE(FileSystem)) &&
                      _wcsicmp(FileSystem, L"FAT32") == 0,
                      Probe, L"system volume %s file system %s", SystemDevice, FileSystem);
    }

    StringCchPrintfW(Probe, ARRAYSIZE(Probe), L"%s.idle", Name);
    SelfTestVolumeIdle(Context, Probe, InstallDevice);
    SelfTestVolumeIdle(Context, Probe, SystemDevice);

    return TRUE;
}

static VOID
SelfTestResize(
    _In_ PSELFTEST_CONTEXT Context,
    _In_ PCWSTR DeviceName)
{
    UNICODE_STRING Path;
    VOLRESIZE_INFO Before, After;
    ULONGLONG Reclaim;
    NTSTATUS Status;

    RtlInitUnicodeString(&Path, DeviceName);

    SelfTestCheck(Context, SelfTestWritePattern(DeviceName), L"A.data", L"write 8 MiB pattern");
    SelfTestCheck(Context, SelfTestVerifyPattern(DeviceName), L"A.data", L"read back pattern");

    if (!SelfTestCheck(Context, SelfTestQuery(DeviceName, &Before), L"A.query", L"VolResizeQuery"))
        return;

    SelfTestLog(Context, L"SETUPTEST INFO partition %I64u volume %I64u max %I64u",
                Before.PartitionBytes, Before.VolumeBytes, Before.MaxReclaimableBytes);

    if (!SelfTestCheck(Context, Before.MaxReclaimableBytes >= SELFTEST_GIGABYTE, L"A.query",
                       L"max reclaimable %I64u", Before.MaxReclaimableBytes))
    {
        return;
    }

    Status = VolResizeShrink(&Path, SELFTEST_GIGABYTE, NULL, NULL);
    SelfTestCheck(Context, NT_SUCCESS(Status), L"A.shrink", L"VolResizeShrink 1 GiB 0x%08lx", Status);
    if (SelfTestQuery(DeviceName, &After))
    {
        SelfTestCheck(Context,
                      After.PartitionBytes == Before.PartitionBytes - SELFTEST_GIGABYTE &&
                      After.VolumeBytes <= After.PartitionBytes &&
                      After.VolumeBytes + SELFTEST_MEGABYTE >= After.PartitionBytes,
                      L"A.shrink", L"partition %I64u volume %I64u",
                      After.PartitionBytes, After.VolumeBytes);
    }
    SelfTestCheck(Context, SelfTestVerifyPattern(DeviceName), L"A.shrink", L"pattern intact");

    Status = VolResizeExtend(&Path, SELFTEST_GIGABYTE, TRUE);
    SelfTestCheck(Context, NT_SUCCESS(Status), L"A.extend", L"VolResizeExtend 1 GiB 0x%08lx", Status);
    if (SelfTestQuery(DeviceName, &After))
    {
        SelfTestCheck(Context,
                      After.PartitionBytes == Before.PartitionBytes &&
                      After.VolumeBytes + SELFTEST_MEGABYTE >= After.PartitionBytes,
                      L"A.extend", L"partition %I64u volume %I64u",
                      After.PartitionBytes, After.VolumeBytes);
    }
    SelfTestCheck(Context, SelfTestVerifyPattern(DeviceName), L"A.extend", L"pattern intact");

    if (!SelfTestQuery(DeviceName, &Before))
        return;

    Reclaim = Before.MaxReclaimableBytes & ~(SELFTEST_MEGABYTE - 1);
    Status = VolResizeShrink(&Path, Reclaim, NULL, NULL);
    SelfTestCheck(Context, NT_SUCCESS(Status), L"A.shrinkmax", L"VolResizeShrink %I64u 0x%08lx", Reclaim, Status);
    if (SelfTestQuery(DeviceName, &After))
    {
        SelfTestCheck(Context, After.PartitionBytes == Before.PartitionBytes - Reclaim,
                      L"A.shrinkmax", L"partition %I64u volume %I64u", After.PartitionBytes, After.VolumeBytes);
    }
    SelfTestCheck(Context, SelfTestVerifyPattern(DeviceName), L"A.shrinkmax", L"pattern intact");

    Status = VolResizeExtend(&Path, Reclaim, TRUE);
    SelfTestCheck(Context, NT_SUCCESS(Status), L"A.extendmax", L"VolResizeExtend %I64u 0x%08lx", Reclaim, Status);
    if (SelfTestQuery(DeviceName, &After))
    {
        SelfTestCheck(Context, After.PartitionBytes == Before.PartitionBytes,
                      L"A.extendmax", L"partition %I64u volume %I64u", After.PartitionBytes, After.VolumeBytes);
    }
    SelfTestCheck(Context, SelfTestVerifyPattern(DeviceName), L"A.extendmax", L"pattern intact");
}

static BOOLEAN
SelfTestKeyExists(
    _In_ PCWSTR Name)
{
    OBJECT_ATTRIBUTES ObjectAttributes;
    UNICODE_STRING KeyName;
    HANDLE Key;

    RtlInitUnicodeString(&KeyName, Name);
    InitializeObjectAttributes(&ObjectAttributes, &KeyName, OBJ_CASE_INSENSITIVE, NULL, NULL);
    if (!NT_SUCCESS(NtOpenKey(&Key, KEY_READ, &ObjectAttributes)))
        return FALSE;

    NtClose(Key);
    return TRUE;
}

static VOID
SelfTestBoot(
    _In_ PSELFTEST_CONTEXT Context,
    _In_ PCWSTR SystemDevice)
{
    PUSETUP_DATA USetupData = &Context->SetupData->USetupData;
    WCHAR PathBuffer[MAX_PATH];
    WCHAR Probe[MAX_PATH];
    USHORT OldOrder[SELFTEST_BOOT_ORDER_MAX];
    USHORT NewOrder[SELFTEST_BOOT_ORDER_MAX];
    WCHAR Name[16];
    DWORD OldSize, NewSize;
    ULONG Index, Old;
    BOOLEAN Known;
    BOOLEAN Enabled;
    NTSTATUS Status;

    RtlAdjustPrivilege(SE_SYSTEM_ENVIRONMENT_PRIVILEGE, TRUE, FALSE, &Enabled);
    OldSize = GetFirmwareEnvironmentVariableExW(L"BootOrder", L"{8BE4DF61-93CA-11D2-AA0D-00E098032B8C}",
                                                OldOrder, sizeof(OldOrder), NULL);

    Status = InitDestinationPaths(USetupData, NULL, Context->InstallVolume);
    if (!SelfTestCheck(Context, NT_SUCCESS(Status), L"A.boot", L"InitDestinationPaths 0x%08lx", Status))
        return;

    RtlFreeUnicodeString(&USetupData->SystemRootPath);
    StringCchPrintfW(PathBuffer, ARRAYSIZE(PathBuffer), L"%s\\", Context->SystemVolume->PartEntry->DeviceName);
    RtlCreateUnicodeString(&USetupData->SystemRootPath, PathBuffer);

    Status = InstallBootManagerAndBootEntries(USetupData->ArchType,
                                              &USetupData->SystemRootPath,
                                              &USetupData->SourceRootPath,
                                              &USetupData->DestinationArcPath,
                                              0);
    SelfTestCheck(Context, NT_SUCCESS(Status), L"A.boot", L"InstallBootManagerAndBootEntries 0x%08lx on %wZ",
                  Status, &USetupData->SystemRootPath);
    SelfTestCheck(Context, !SelfTestKeyExists(L"\\Registry\\Machine\\SetupBcd"), L"A.boot", L"BCD hive unloaded");
    SelfTestVolumeIdle(Context, L"A.boot", SystemDevice);

    StringCchPrintfW(Probe, ARRAYSIZE(Probe), L"\\\\?\\GLOBALROOT%s\\EFI\\LiberNT\\Boot\\BCD", SystemDevice);
    SelfTestCheck(Context, GetFileAttributesW(Probe) != INVALID_FILE_ATTRIBUTES, L"A.boot", L"BCD present");
    StringCchPrintfW(Probe, ARRAYSIZE(Probe), L"\\\\?\\GLOBALROOT%s\\EFI\\LiberNT\\Boot\\bootmgfw.efi", SystemDevice);
    SelfTestCheck(Context, GetFileAttributesW(Probe) != INVALID_FILE_ATTRIBUTES, L"A.boot", L"boot manager present");

    NewSize = GetFirmwareEnvironmentVariableExW(L"BootOrder", L"{8BE4DF61-93CA-11D2-AA0D-00E098032B8C}",
                                                NewOrder, sizeof(NewOrder), NULL);
    SelfTestCheck(Context, NewSize >= sizeof(USHORT), L"A.boot", L"BootOrder has %lu entries after install",
                  NewSize / (ULONG)sizeof(USHORT));
    if (NewSize < sizeof(USHORT) || OldSize == 0)
        return;

    SelfTestLog(Context, L"SETUPTEST INFO first boot entry Boot%04X", NewOrder[0]);
    for (Index = 0; Index < NewSize / sizeof(USHORT); Index++)
    {
        Known = FALSE;
        for (Old = 0; Old < OldSize / sizeof(USHORT); Old++)
        {
            if (OldOrder[Old] == NewOrder[Index])
                Known = TRUE;
        }

        if (!Known)
        {
            StringCchPrintfW(Name, ARRAYSIZE(Name), L"Boot%04X", NewOrder[Index]);
            SetFirmwareEnvironmentVariableExW(Name, L"{8BE4DF61-93CA-11D2-AA0D-00E098032B8C}", NULL, 0, 0);
        }
    }

    SelfTestCheck(Context,
                  SetFirmwareEnvironmentVariableExW(L"BootOrder", L"{8BE4DF61-93CA-11D2-AA0D-00E098032B8C}",
                                                    OldOrder, OldSize, 7),
                  L"A.boot", L"firmware boot order restored");
}

static VOID
SelfTestClearDisk(
    _In_ PSELFTEST_CONTEXT Context,
    _In_ ULONG DiskNumber)
{
    WCHAR Path[64];
    HANDLE Disk;
    DWORD Returned;
    BOOL Ok;

    StringCchPrintfW(Path, ARRAYSIZE(Path), L"\\\\.\\PhysicalDrive%lu", DiskNumber);
    Disk = CreateFileW(Path, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                       NULL, OPEN_EXISTING, 0, NULL);
    Ok = (Disk != INVALID_HANDLE_VALUE);
    if (Ok)
    {
        Ok = DeviceIoControl(Disk, IOCTL_DISK_DELETE_DRIVE_LAYOUT, NULL, 0, NULL, 0, &Returned, NULL);
        DeviceIoControl(Disk, IOCTL_DISK_UPDATE_PROPERTIES, NULL, 0, NULL, 0, &Returned, NULL);
        CloseHandle(Disk);
    }

    SelfTestCheck(Context, Ok, L"cleanup", L"disk %lu layout deleted", DiskNumber);
}

static ULONG
SelfTestFindTargets(
    _In_ PSELFTEST_CONTEXT Context,
    _Out_writes_(SELFTEST_MAX_DISKS) PULONG Targets)
{
    PLIST_ENTRY Entry;
    PDISKENTRY DiskEntry;
    ULONG Count = 0;

    if (!SelfTestRefreshList(Context))
        return 0;

    for (Entry = Context->List->DiskListHead.Flink;
         Entry != &Context->List->DiskListHead;
         Entry = Entry->Flink)
    {
        DiskEntry = CONTAINING_RECORD(Entry, DISKENTRY, ListEntry);
        SelfTestLog(Context, L"SETUPTEST INFO disk %lu media %lu style %lu new %u size %I64u",
                    DiskEntry->DiskNumber, (ULONG)DiskEntry->MediaType, (ULONG)DiskEntry->DiskStyle,
                    DiskEntry->NewDisk, DiskEntry->SectorCount.QuadPart * DiskEntry->BytesPerSector);
        if (DiskEntry->MediaType == FixedMedia && DiskEntry->NewDisk && Count < SELFTEST_MAX_DISKS)
            Targets[Count++] = DiskEntry->DiskNumber;
    }

    return Count;
}

ULONG
RunSetupSelfTest(
    _In_ PSETUPDATA SetupData)
{
    SELFTEST_CONTEXT Context;
    WCHAR LogPath[MAX_PATH];
    WCHAR InstallDevice[MAX_PATH];
    WCHAR SystemDevice[MAX_PATH];
    WCHAR OtherInstall[MAX_PATH];
    WCHAR OtherSystem[MAX_PATH];
    ULONG Targets[SELFTEST_MAX_DISKS];
    ULONG Count;
    ULONG Index;
    HRESULT hrCom = E_FAIL;

    ZeroMemory(&Context, sizeof(Context));
    Context.SetupData = SetupData;
    Context.LogFile = INVALID_HANDLE_VALUE;

    if (GetTempPathW(ARRAYSIZE(LogPath), LogPath) &&
        SUCCEEDED(StringCchCatW(LogPath, ARRAYSIZE(LogPath), L"setuptest.log")))
    {
        Context.LogFile = CreateFileW(LogPath, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, 0, NULL);
    }

    SelfTestLog(&Context, L"SETUPTEST START source %wZ", &SetupData->USetupData.SourceRootPath);

    Count = SelfTestFindTargets(&Context, Targets);
    if (!SelfTestCheck(&Context, Count != 0, L"enum", L"%lu blank fixed disks", Count))
        goto Done;

    hrCom = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    DiskInfoRefresh();
    for (Index = 0; Index < Count; Index++)
    {
        WCHAR Description[128];
        BOOL Found = DiskInfoGetDescription(Targets[Index], Description, ARRAYSIZE(Description));

        SelfTestCheck(&Context,
                      Found && wcsncmp(Description, L"Wine", 4) != 0 && wcschr(Description, L'\x00B7') != NULL,
                      L"wmi", L"MSFT_PhysicalDisk %lu: %s", Targets[Index], Found ? Description : L"(none)");
    }
    DiskInfoFree();

    if (SelfTestLayoutDisk(&Context, Targets[0], TRUE, L"A",
                           InstallDevice, ARRAYSIZE(InstallDevice),
                           SystemDevice, ARRAYSIZE(SystemDevice)))
    {
        SelfTestResize(&Context, InstallDevice);
        SelfTestVolumeIdle(&Context, L"A.resize", InstallDevice);
        SelfTestBoot(&Context, SystemDevice);
    }

    if (Count > 1)
    {
        SelfTestLayoutDisk(&Context, Targets[1], FALSE, L"B",
                           OtherInstall, ARRAYSIZE(OtherInstall),
                           OtherSystem, ARRAYSIZE(OtherSystem));
        SelfTestCheck(&Context, _wcsicmp(OtherSystem, SystemDevice) == 0, L"B.reuse",
                      L"system volume %s expected %s", OtherSystem, SystemDevice);
    }

    if (Context.List)
    {
        DestroyPartitionList(Context.List);
        Context.List = NULL;
        SetupData->PartitionList = NULL;
    }

    for (Index = Count; Index > 0; Index--)
        SelfTestClearDisk(&Context, Targets[Index - 1]);

Done:
    SelfTestLog(&Context, L"SETUPTEST END pass %lu fail %lu", Context.Passes, Context.Failures);

    if (Context.List)
    {
        DestroyPartitionList(Context.List);
        SetupData->PartitionList = NULL;
    }
    if (Context.LogFile != INVALID_HANDLE_VALUE)
        CloseHandle(Context.LogFile);
    if (SUCCEEDED(hrCom))
        CoUninitialize();

    return Context.Failures;
}
