/*
 * PROJECT:     LiberNT Setup
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Disk, boot and firmware changes made when Setup runs on Windows
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#define COBJMACROS
#include "libernt.h"
#include <winioctl.h>
#include <ntddstor.h>
#include <bcrypt.h>
#include <objbase.h>
#include <ndk/setypes.h>
#include <initguid.h>
#include <wbemcli.h>

#include "resource.h"
#include "host.h"
#include "update.h"

#define NDEBUG
#include <debug.h>

#define HOST_LAYOUT_PARTITIONS  128
#define HOST_COPY_CHUNK         (4 * 1024 * 1024)
#define HOST_ISO_BLOCK          2048
#define HOST_ISO_DESCRIPTOR     16
#define HOST_ISO_VOLUME_BLOCKS  80
#define HOST_ISO_BLOCK_SIZE     128
#define HOST_VOLUME_WAIT_MS     30000
#define HOST_VOLUME_POLL_MS     250
#define HOST_STORAGE_NAMESPACE  L"ROOT\\Microsoft\\Windows\\Storage"
#define HOST_BASIC_DATA_TYPE    L"{ebd0a0a2-b9e5-4433-87c0-68b6b72699c7}"
#define HOST_RUN_KEY            L"Microsoft\\Windows\\CurrentVersion\\Run"
#define HOST_COMPLETE_VALUE     L"LiberNTUpdateComplete"
#define HOST_COMPLETE_COMMAND   L"%SystemRoot%\\system32\\lntupdate.exe /complete"
#define HOST_UPGRADE_LOADER     L"\\" HOST_STAGE_DIRECTORY L"\\" UPDATE_STAGE_LOADER
#define HOST_UPDATE_STATE       L"\\EFI\\LiberNT\\Boot\\update.state"

#define EFI_LOAD_OPTION_ACTIVE      0x00000001
#define EFI_LOAD_OPTION_HEADER_SIZE 6
#define EFI_DEVICE_PATH_HEADER_SIZE 4
#define EFI_MEDIA_DEVICE_PATH       0x04
#define EFI_MEDIA_HARDDRIVE_DP      0x01
#define EFI_MEDIA_FILEPATH_DP       0x04
#define EFI_END_DEVICE_PATH_TYPE    0x7F
#define EFI_END_ENTIRE_DEVICE_PATH  0xFF
#define EFI_HARDDRIVE_NODE_SIZE     42
#define EFI_PARTITION_FORMAT_GPT    0x02
#define EFI_SIGNATURE_TYPE_GUID     0x02
#define EFI_LOAD_OPTION_MAX         4096
#define EFI_BOOT_ORDER_MAX          512
#define EFI_BOOT_OPTION_SEARCH_MAX  0x1000
#define EFI_OS_INDICATIONS_BOOT_TO_FW_UI 0x0000000000000001ULL
#define GPT_HEADER_ENTRIES_LBA      72
#define GPT_HEADER_ENTRY_COUNT      80
#define GPT_HEADER_ENTRY_SIZE       84
#define GPT_ENTRY_UNIQUE_ID         16
#define GPT_MAX_ENTRY_BYTES         (1024 * 1024)

DEFINE_GUID(HostSystemPartitionType, 0xc12a7328, 0xf81f, 0x11d2, 0xba, 0x4b, 0x00, 0xa0, 0xc9, 0x3e, 0xc9, 0x3b);

VOID
HostLog(
    _In_ PCWSTR Format,
    ...)
{
    WCHAR Line[1024], Path[MAX_PATH];
    CHAR Utf8[3 * ARRAYSIZE(Line)];
    SYSTEMTIME Time;
    va_list Args;
    HANDLE File;
    DWORD Written;
    SIZE_T Length;
    INT Bytes;

    GetLocalTime(&Time);
    StringCchPrintfW(Line, ARRAYSIZE(Line), L"%04u-%02u-%02u %02u:%02u:%02u ", Time.wYear, Time.wMonth, Time.wDay,
                     Time.wHour, Time.wMinute, Time.wSecond);
    Length = wcslen(Line);
    va_start(Args, Format);
    StringCchVPrintfW(Line + Length, ARRAYSIZE(Line) - Length - 2, Format, Args);
    va_end(Args);
    StringCchCatW(Line, ARRAYSIZE(Line), L"\r\n");
    OutputDebugStringW(Line);

    if (!GetWindowsDirectoryW(Path, ARRAYSIZE(Path)) ||
        FAILED(StringCchCatW(Path, ARRAYSIZE(Path), HOST_LOG_DIRECTORY)))
    {
        return;
    }
    CreateDirectoryW(Path, NULL);
    if (FAILED(StringCchCatW(Path, ARRAYSIZE(Path), HOST_LOG_FILE)))
        return;
    Bytes = WideCharToMultiByte(CP_UTF8, 0, Line, -1, Utf8, sizeof(Utf8), NULL, NULL);
    File = CreateFileW(Path, FILE_APPEND_DATA, FILE_SHARE_READ, NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (File == INVALID_HANDLE_VALUE)
        return;
    if (Bytes > 1)
        WriteFile(File, Utf8, (DWORD)(Bytes - 1), &Written, NULL);
    CloseHandle(File);
}

BOOL
HostIsWindows(VOID)
{
    WCHAR Path[MAX_PATH];
    UINT Length = GetSystemDirectoryW(Path, ARRAYSIZE(Path));

    if (!Length || Length >= ARRAYSIZE(Path) ||
        FAILED(StringCchCatW(Path, ARRAYSIZE(Path), L"\\ntoskrnl.exe")) ||
        GetFileAttributesW(Path) == INVALID_FILE_ATTRIBUTES)
    {
        return FALSE;
    }
    return !IsLiberNtKernel(Path);
}

BOOL
HostPath(
    _In_ PCWSTR DeviceName,
    _In_opt_ PCWSTR Relative,
    _Out_writes_(MAX_PATH) PWSTR Path)
{
    return SUCCEEDED(StringCchPrintfW(Path, MAX_PATH, L"\\\\?\\GLOBALROOT%s%s", DeviceName,
                                      Relative ? Relative : L""));
}

static PDRIVE_LAYOUT_INFORMATION_EX
ReadDriveLayout(
    _In_ ULONG DiskNumber,
    _Out_opt_ PULONG SectorSize)
{
    DWORD Size = FIELD_OFFSET(DRIVE_LAYOUT_INFORMATION_EX, PartitionEntry[HOST_LAYOUT_PARTITIONS]);
    PDRIVE_LAYOUT_INFORMATION_EX Layout;
    DISK_GEOMETRY Geometry;
    WCHAR Path[MAX_PATH];
    DWORD Returned;
    HANDLE Disk;
    BOOL Success;

    if (FAILED(StringCchPrintfW(Path, ARRAYSIZE(Path), L"\\\\.\\PhysicalDrive%lu", DiskNumber)))
        return NULL;
    Layout = HeapAlloc(ProcessHeap, HEAP_ZERO_MEMORY, Size);
    if (!Layout)
        return NULL;

    Disk = CreateFileW(Path, 0, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    Success = Disk != INVALID_HANDLE_VALUE &&
              DeviceIoControl(Disk, IOCTL_DISK_GET_DRIVE_LAYOUT_EX, NULL, 0, Layout, Size, &Returned, NULL) &&
              (!SectorSize ||
               DeviceIoControl(Disk, IOCTL_DISK_GET_DRIVE_GEOMETRY, NULL, 0, &Geometry, sizeof(Geometry),
                               &Returned, NULL));
    if (Disk != INVALID_HANDLE_VALUE)
        CloseHandle(Disk);
    if (!Success)
    {
        HostLog(L"The layout of disk %lu cannot be read, error %lu", DiskNumber, GetLastError());
        HeapFree(ProcessHeap, 0, Layout);
        return NULL;
    }
    if (SectorSize)
        *SectorSize = Geometry.BytesPerSector;
    return Layout;
}

DWORD
HostQueryPartition(
    _In_ PCWSTR DeviceName,
    _Out_ PHOST_PARTITION Partition)
{
    PARTITION_INFORMATION_EX Info;
    STORAGE_DEVICE_NUMBER Number;
    PDRIVE_LAYOUT_INFORMATION_EX Layout;
    WCHAR Path[MAX_PATH];
    DWORD Returned, Error = ERROR_SUCCESS;
    HANDLE Volume;

    ZeroMemory(Partition, sizeof(*Partition));
    if (FAILED(StringCchCopyW(Partition->DeviceName, ARRAYSIZE(Partition->DeviceName), DeviceName)) ||
        !HostPath(DeviceName, NULL, Path))
    {
        return ERROR_BUFFER_OVERFLOW;
    }

    Volume = CreateFileW(Path, 0, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    if (Volume == INVALID_HANDLE_VALUE)
        return GetLastError();
    if (!DeviceIoControl(Volume, IOCTL_STORAGE_GET_DEVICE_NUMBER, NULL, 0, &Number, sizeof(Number), &Returned, NULL) ||
        !DeviceIoControl(Volume, IOCTL_DISK_GET_PARTITION_INFO_EX, NULL, 0, &Info, sizeof(Info), &Returned, NULL))
    {
        Error = GetLastError();
    }
    CloseHandle(Volume);
    if (Error != ERROR_SUCCESS)
        return Error;
    if (Number.DeviceType != FILE_DEVICE_DISK)
        return ERROR_NOT_SUPPORTED;

    Partition->DiskNumber = Number.DeviceNumber;
    Partition->PartitionNumber = Number.PartitionNumber;
    Partition->Offset = Info.StartingOffset.QuadPart;
    Partition->Length = Info.PartitionLength.QuadPart;
    Partition->Style = Info.PartitionStyle;
    if (Info.PartitionStyle == PARTITION_STYLE_GPT)
    {
        Partition->PartitionId = Info.Gpt.PartitionId;
        Partition->PartitionType = Info.Gpt.PartitionType;
    }

    Layout = ReadDriveLayout(Number.DeviceNumber, &Partition->SectorSize);
    if (!Layout)
        return ERROR_READ_FAULT;
    if (Layout->PartitionStyle == PARTITION_STYLE_GPT)
        Partition->DiskId = Layout->Gpt.DiskId;
    HeapFree(ProcessHeap, 0, Layout);
    return ERROR_SUCCESS;
}

DWORD
HostGetWindowsPartition(
    _Out_ PHOST_PARTITION Partition)
{
    WCHAR Directory[MAX_PATH], Drive[3], DeviceName[MAX_PATH];

    if (!GetSystemWindowsDirectoryW(Directory, ARRAYSIZE(Directory)) || Directory[1] != L':')
        return ERROR_PATH_NOT_FOUND;
    Drive[0] = Directory[0];
    Drive[1] = L':';
    Drive[2] = UNICODE_NULL;
    if (!QueryDosDeviceW(Drive, DeviceName, ARRAYSIZE(DeviceName)))
        return GetLastError();
    return HostQueryPartition(DeviceName, Partition);
}

DWORD
HostGetSystemPartition(
    _Out_ PHOST_PARTITION Partition)
{
    WCHAR DeviceName[MAX_PATH];
    DWORD Size = sizeof(DeviceName), Error;

    Error = RegGetValueW(HKEY_LOCAL_MACHINE, L"SYSTEM\\Setup", L"SystemPartition", RRF_RT_REG_SZ, NULL,
                         DeviceName, &Size);
    if (Error != ERROR_SUCCESS)
        return Error;
    Error = HostQueryPartition(DeviceName, Partition);
    if (Error == ERROR_SUCCESS &&
        (Partition->Style != PARTITION_STYLE_GPT ||
         !IsEqualGUID(&Partition->PartitionType, &HostSystemPartitionType)))
    {
        Error = ERROR_NOT_SUPPORTED;
    }
    return Error;
}

ULONGLONG
HostGetFreeBytesAfter(
    _In_ PHOST_PARTITION Partition)
{
    PDRIVE_LAYOUT_INFORMATION_EX Layout = ReadDriveLayout(Partition->DiskNumber, NULL);
    ULONGLONG End = Partition->Offset + Partition->Length, Next, Start;
    ULONG Index;

    if (!Layout || Layout->PartitionStyle != PARTITION_STYLE_GPT)
    {
        if (Layout)
            HeapFree(ProcessHeap, 0, Layout);
        return 0;
    }

    Next = Layout->Gpt.StartingUsableOffset.QuadPart + Layout->Gpt.UsableLength.QuadPart;
    for (Index = 0; Index < Layout->PartitionCount; Index++)
    {
        Start = Layout->PartitionEntry[Index].StartingOffset.QuadPart;
        if (Layout->PartitionEntry[Index].PartitionLength.QuadPart != 0 && Start >= End && Start < Next)
            Next = Start;
    }
    HeapFree(ProcessHeap, 0, Layout);
    return Next > End ? Next - End : 0;
}

static BOOL
GetVolumeDeviceName(
    _In_ PCWSTR VolumeName,
    _Out_writes_(MAX_PATH) PWSTR DeviceName)
{
    WCHAR Name[MAX_PATH];
    SIZE_T Length;

    if (wcsncmp(VolumeName, L"\\\\?\\", 4) != 0 || FAILED(StringCchCopyW(Name, ARRAYSIZE(Name), VolumeName + 4)))
        return FALSE;
    Length = wcslen(Name);
    if (Length && Name[Length - 1] == L'\\')
        Name[Length - 1] = UNICODE_NULL;
    return QueryDosDeviceW(Name, DeviceName, MAX_PATH) != 0;
}

typedef BOOL (*PHOST_VOLUME_CALLBACK)(PCWSTR DeviceName, PVOID Context);

static VOID
EnumerateVolumes(
    _In_ PHOST_VOLUME_CALLBACK Callback,
    _In_opt_ PVOID Context)
{
    WCHAR VolumeName[MAX_PATH], DeviceName[MAX_PATH];
    HANDLE Find;

    Find = FindFirstVolumeW(VolumeName, ARRAYSIZE(VolumeName));
    if (Find == INVALID_HANDLE_VALUE)
        return;
    do
    {
        if (GetVolumeDeviceName(VolumeName, DeviceName) && !Callback(DeviceName, Context))
            break;
    } while (FindNextVolumeW(Find, VolumeName, ARRAYSIZE(VolumeName)));
    FindVolumeClose(Find);
}

static BOOL
HostFileExists(
    _In_ PCWSTR DeviceName,
    _In_ PCWSTR Relative)
{
    WCHAR Path[MAX_PATH];

    return HostPath(DeviceName, Relative, Path) && GetFileAttributesW(Path) != INVALID_FILE_ATTRIBUTES;
}

typedef struct _PREPARED_SEARCH
{
    PHOST_PARTITION Partition;
    DWORD Error;
} PREPARED_SEARCH, *PPREPARED_SEARCH;

static BOOL
FindPreparedCallback(
    _In_ PCWSTR DeviceName,
    _In_ PVOID Context)
{
    PPREPARED_SEARCH Search = Context;

    if (!HostFileExists(DeviceName, L"\\" HOST_STAGE_DIRECTORY L"\\" HOST_SETUP_FILE))
        return TRUE;
    Search->Error = HostQueryPartition(DeviceName, Search->Partition);
    return Search->Error != ERROR_SUCCESS;
}

DWORD
HostFindPreparedPartition(
    _Out_ PHOST_PARTITION Partition)
{
    PREPARED_SEARCH Search = { Partition, ERROR_NOT_FOUND };

    ZeroMemory(Partition, sizeof(*Partition));
    EnumerateVolumes(FindPreparedCallback, &Search);
    return Search.Error;
}

typedef struct _LIBERNT_SEARCH
{
    WORD Machine;
    PHOST_PARTITION Partitions;
    WCHAR (*Versions)[HOST_VERSION_CHARS];
    ULONG MaxCount;
    ULONG Count;
} LIBERNT_SEARCH, *PLIBERNT_SEARCH;

static BOOL
FindLiberNTCallback(
    _In_ PCWSTR DeviceName,
    _In_ PVOID Context)
{
    PLIBERNT_SEARCH Search = Context;
    WCHAR Path[MAX_PATH];
    WORD Machine;

    if (!HostPath(DeviceName, L"\\Windows\\System32\\ntoskrnl.exe", Path) ||
        !GetKernelMachine(Path, &Machine) || Machine != Search->Machine || !IsLiberNtKernel(Path) ||
        HostQueryPartition(DeviceName, &Search->Partitions[Search->Count]) != ERROR_SUCCESS)
    {
        return TRUE;
    }

    if (!HostPath(DeviceName, L"\\Windows\\" UPGRADE_INVENTORY_PATH, Path) ||
        !ReadInventoryVersion(Path, Search->Versions[Search->Count], HOST_VERSION_CHARS))
    {
        LoadStringW(GetModuleHandleW(NULL), IDS_UPGRADE_EARLIER, Search->Versions[Search->Count], HOST_VERSION_CHARS);
    }
    HostLog(L"LiberNT found on %s: %s", DeviceName, Search->Versions[Search->Count]);
    return ++Search->Count < Search->MaxCount;
}

ULONG
HostFindLiberNT(
    _In_ WORD Machine,
    _Out_writes_(MaxCount) PHOST_PARTITION Partitions,
    _Out_writes_(MaxCount) WCHAR (*Versions)[HOST_VERSION_CHARS],
    _In_ ULONG MaxCount)
{
    LIBERNT_SEARCH Search = { Machine, Partitions, Versions, MaxCount, 0 };

    if (MaxCount)
        EnumerateVolumes(FindLiberNTCallback, &Search);
    return Search.Count;
}

static BOOL
GetSourceDevice(
    _In_ PCWSTR SourceRoot,
    _Out_writes_(MAX_PATH) PWSTR Device)
{
    if (!SourceRoot[0] || SourceRoot[1] != L':' || (SourceRoot[2] && (SourceRoot[2] != L'\\' || SourceRoot[3])))
        return FALSE;
    return SUCCEEDED(StringCchPrintfW(Device, MAX_PATH, L"\\\\.\\%c:", SourceRoot[0]));
}

static HANDLE
OpenSource(
    _In_ PCWSTR SourceRoot)
{
    WCHAR Device[MAX_PATH];

    if (!GetSourceDevice(SourceRoot, Device))
    {
        SetLastError(ERROR_BAD_PATHNAME);
        return INVALID_HANDLE_VALUE;
    }
    return CreateFileW(Device, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING,
                       FILE_FLAG_NO_BUFFERING | FILE_FLAG_SEQUENTIAL_SCAN, NULL);
}

static BOOL
ReadAt(
    _In_ HANDLE File,
    _In_ ULONGLONG Offset,
    _Out_writes_bytes_(Length) PVOID Buffer,
    _In_ DWORD Length)
{
    LARGE_INTEGER Position;
    DWORD Read;

    Position.QuadPart = (LONGLONG)Offset;
    return SetFilePointerEx(File, Position, NULL, FILE_BEGIN) &&
           ReadFile(File, Buffer, Length, &Read, NULL) && Read == Length;
}

DWORD
HostReadImageSize(
    _In_ PCWSTR SourceRoot,
    _Out_ PULONGLONG Bytes)
{
    PUCHAR Block;
    HANDLE Source;
    DWORD Error = ERROR_SUCCESS;

    *Bytes = 0;
    Source = OpenSource(SourceRoot);
    if (Source == INVALID_HANDLE_VALUE)
        return GetLastError();

    Block = VirtualAlloc(NULL, HOST_ISO_BLOCK, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!Block)
        Error = ERROR_NOT_ENOUGH_MEMORY;
    else if (!ReadAt(Source, HOST_ISO_DESCRIPTOR * HOST_ISO_BLOCK, Block, HOST_ISO_BLOCK))
        Error = GetLastError() ? GetLastError() : ERROR_READ_FAULT;
    else if (Block[0] != 1 || memcmp(Block + 1, "CD001", 5) != 0)
        Error = ERROR_BAD_FORMAT;
    else
        *Bytes = (ULONGLONG)*(PULONG)(Block + HOST_ISO_VOLUME_BLOCKS) * *(PUSHORT)(Block + HOST_ISO_BLOCK_SIZE);

    if (Error == ERROR_SUCCESS && (*Bytes == 0 || *Bytes % HOST_ISO_BLOCK))
        Error = ERROR_BAD_FORMAT;
    if (Block)
        VirtualFree(Block, 0, MEM_RELEASE);
    CloseHandle(Source);
    return Error;
}

typedef struct _HOST_HASH
{
    BCRYPT_ALG_HANDLE Algorithm;
    BCRYPT_HASH_HANDLE Hash;
} HOST_HASH, *PHOST_HASH;

static BOOL
HashOpen(
    _Out_ PHOST_HASH Hash)
{
    ZeroMemory(Hash, sizeof(*Hash));
    return BCRYPT_SUCCESS(BCryptOpenAlgorithmProvider(&Hash->Algorithm, BCRYPT_SHA256_ALGORITHM, NULL, 0)) &&
           BCRYPT_SUCCESS(BCryptCreateHash(Hash->Algorithm, &Hash->Hash, NULL, 0, NULL, 0, 0));
}

static BOOL
HashClose(
    _Inout_ PHOST_HASH Hash,
    _Out_writes_opt_(HOST_SHA256_CHARS) PWSTR Text)
{
    BYTE Digest[32];
    BOOL Success = FALSE;
    ULONG Index;

    if (Text && Hash->Hash && BCRYPT_SUCCESS(BCryptFinishHash(Hash->Hash, Digest, sizeof(Digest), 0)))
    {
        for (Index = 0; Index < ARRAYSIZE(Digest); Index++)
            StringCchPrintfW(&Text[Index * 2], 3, L"%02x", Digest[Index]);
        Success = TRUE;
    }
    if (Hash->Hash)
        BCryptDestroyHash(Hash->Hash);
    if (Hash->Algorithm)
        BCryptCloseAlgorithmProvider(Hash->Algorithm, 0);
    ZeroMemory(Hash, sizeof(*Hash));
    return Success;
}

DWORD
HostCopyImage(
    _In_ PCWSTR SourceRoot,
    _In_ ULONGLONG Bytes,
    _In_ PCWSTR Target,
    _In_opt_ PHOST_PROGRESS Progress,
    _In_opt_ PVOID Context,
    _Out_writes_(HOST_SHA256_CHARS) PWSTR Hash)
{
    HOST_HASH Digest;
    ULONGLONG Done = 0;
    PUCHAR Buffer;
    HANDLE Source, File = INVALID_HANDLE_VALUE;
    DWORD Chunk, Written, Error = ERROR_SUCCESS;

    Hash[0] = UNICODE_NULL;
    ZeroMemory(&Digest, sizeof(Digest));
    Source = OpenSource(SourceRoot);
    if (Source == INVALID_HANDLE_VALUE)
        return GetLastError();

    Buffer = VirtualAlloc(NULL, HOST_COPY_CHUNK, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!Buffer || !HashOpen(&Digest))
        Error = ERROR_NOT_ENOUGH_MEMORY;
    if (Error == ERROR_SUCCESS)
    {
        File = CreateFileW(Target, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, NULL);
        if (File == INVALID_HANDLE_VALUE)
            Error = GetLastError();
    }

    while (Error == ERROR_SUCCESS && Done < Bytes)
    {
        Chunk = (DWORD)min(Bytes - Done, (ULONGLONG)HOST_COPY_CHUNK);
        if (!ReadAt(Source, Done, Buffer, Chunk))
            Error = GetLastError() ? GetLastError() : ERROR_READ_FAULT;
        else if (!BCRYPT_SUCCESS(BCryptHashData(Digest.Hash, Buffer, Chunk, 0)))
            Error = ERROR_INVALID_DATA;
        else if (!WriteFile(File, Buffer, Chunk, &Written, NULL) || Written != Chunk)
            Error = GetLastError() ? GetLastError() : ERROR_WRITE_FAULT;
        else
            Done += Chunk;

        if (Error == ERROR_SUCCESS && Progress && !Progress(Context, Done, Bytes))
            Error = ERROR_CANCELLED;
    }

    if (Error == ERROR_SUCCESS && !FlushFileBuffers(File))
        Error = GetLastError();
    if (File != INVALID_HANDLE_VALUE)
        CloseHandle(File);
    if (!HashClose(&Digest, Error == ERROR_SUCCESS ? Hash : NULL) && Error == ERROR_SUCCESS)
        Error = ERROR_INVALID_DATA;
    if (Buffer)
        VirtualFree(Buffer, 0, MEM_RELEASE);
    CloseHandle(Source);
    if (Error != ERROR_SUCCESS)
        DeleteFileW(Target);
    HostLog(L"Image copied to %s: %I64u bytes, SHA-256 %s, error %lu", Target, Done, Hash, Error);
    return Error;
}

DWORD
HostHashFile(
    _In_ PCWSTR Path,
    _In_opt_ PHOST_PROGRESS Progress,
    _In_opt_ PVOID Context,
    _Out_writes_(HOST_SHA256_CHARS) PWSTR Hash)
{
    LARGE_INTEGER Size;
    HOST_HASH Digest;
    ULONGLONG Done = 0;
    PUCHAR Buffer;
    HANDLE File;
    DWORD Read, Error = ERROR_SUCCESS;

    Hash[0] = UNICODE_NULL;
    ZeroMemory(&Digest, sizeof(Digest));
    File = CreateFileW(Path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
                       FILE_FLAG_NO_BUFFERING | FILE_FLAG_SEQUENTIAL_SCAN, NULL);
    if (File == INVALID_HANDLE_VALUE)
        return GetLastError();

    Buffer = VirtualAlloc(NULL, HOST_COPY_CHUNK, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!Buffer || !GetFileSizeEx(File, &Size) || !HashOpen(&Digest))
        Error = ERROR_NOT_ENOUGH_MEMORY;

    while (Error == ERROR_SUCCESS)
    {
        if (!ReadFile(File, Buffer, HOST_COPY_CHUNK, &Read, NULL))
            Error = GetLastError();
        else if (Read == 0)
            break;
        else if (!BCRYPT_SUCCESS(BCryptHashData(Digest.Hash, Buffer, Read, 0)))
            Error = ERROR_INVALID_DATA;
        else
            Done += Read;

        if (Error == ERROR_SUCCESS && Progress && !Progress(Context, Done, (ULONGLONG)Size.QuadPart))
            Error = ERROR_CANCELLED;
    }

    if (!HashClose(&Digest, Error == ERROR_SUCCESS ? Hash : NULL) && Error == ERROR_SUCCESS)
        Error = ERROR_INVALID_DATA;
    if (Buffer)
        VirtualFree(Buffer, 0, MEM_RELEASE);
    CloseHandle(File);
    return Error;
}

static PBYTE
ReadSmallFile(
    _In_ PCWSTR Path,
    _Out_ PDWORD Size)
{
    LARGE_INTEGER Length;
    PBYTE Data = NULL;
    HANDLE File;
    DWORD Read;

    *Size = 0;
    File = CreateFileW(Path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (File == INVALID_HANDLE_VALUE)
        return NULL;
    if (GetFileSizeEx(File, &Length) && Length.QuadPart > 0 && Length.QuadPart < 64 * 1024 * 1024)
        Data = HeapAlloc(ProcessHeap, 0, (SIZE_T)Length.QuadPart);
    if (Data && (!ReadFile(File, Data, (DWORD)Length.QuadPart, &Read, NULL) || Read != (DWORD)Length.QuadPart))
    {
        HeapFree(ProcessHeap, 0, Data);
        Data = NULL;
    }
    CloseHandle(File);
    if (Data)
        *Size = (DWORD)Length.QuadPart;
    return Data;
}

DWORD
HostCopyFileVerified(
    _In_ PCWSTR Source,
    _In_ PCWSTR Target)
{
    WCHAR Temporary[MAX_PATH];
    PBYTE Expected, Actual = NULL;
    DWORD ExpectedSize, ActualSize = 0, Written, Error = ERROR_SUCCESS;
    HANDLE File;

    Expected = ReadSmallFile(Source, &ExpectedSize);
    if (!Expected)
        return GetLastError() ? GetLastError() : ERROR_READ_FAULT;
    if (FAILED(StringCchPrintfW(Temporary, ARRAYSIZE(Temporary), L"%s.new", Target)))
    {
        HeapFree(ProcessHeap, 0, Expected);
        return ERROR_BUFFER_OVERFLOW;
    }

    SetFileAttributesW(Temporary, FILE_ATTRIBUTE_NORMAL);
    File = CreateFileW(Temporary, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (File == INVALID_HANDLE_VALUE)
        Error = GetLastError();
    else
    {
        if (!WriteFile(File, Expected, ExpectedSize, &Written, NULL) || Written != ExpectedSize ||
            !FlushFileBuffers(File))
        {
            Error = GetLastError() ? GetLastError() : ERROR_WRITE_FAULT;
        }
        CloseHandle(File);
    }

    if (Error == ERROR_SUCCESS)
    {
        Actual = ReadSmallFile(Temporary, &ActualSize);
        if (!Actual || ActualSize != ExpectedSize || memcmp(Actual, Expected, ExpectedSize) != 0)
            Error = ERROR_INVALID_DATA;
    }
    if (Error == ERROR_SUCCESS)
    {
        SetFileAttributesW(Target, FILE_ATTRIBUTE_NORMAL);
        if (!MoveFileExW(Temporary, Target, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
            Error = GetLastError();
    }
    if (Error != ERROR_SUCCESS)
        DeleteFileW(Temporary);

    if (Actual)
        HeapFree(ProcessHeap, 0, Actual);
    HeapFree(ProcessHeap, 0, Expected);
    HostLog(L"%s copied to %s: %lu", Source, Target, Error);
    return Error;
}

static HRESULT
WmiConnect(
    _Out_ IWbemServices** Services)
{
    IWbemLocator* Locator = NULL;
    BSTR Namespace;
    HRESULT hr;

    *Services = NULL;
    hr = CoCreateInstance(&CLSID_WbemLocator, NULL, CLSCTX_INPROC_SERVER, &IID_IWbemLocator, (PVOID*)&Locator);
    if (FAILED(hr))
        return hr;

    Namespace = SysAllocString(HOST_STORAGE_NAMESPACE);
    hr = Namespace ? IWbemLocator_ConnectServer(Locator, Namespace, NULL, NULL, NULL, 0, NULL, NULL, Services)
                   : E_OUTOFMEMORY;
    SysFreeString(Namespace);
    IWbemLocator_Release(Locator);
    if (FAILED(hr))
        return hr;

    hr = CoSetProxyBlanket((IUnknown*)*Services, RPC_C_AUTHN_WINNT, RPC_C_AUTHZ_NONE, NULL, RPC_C_AUTHN_LEVEL_CALL,
                           RPC_C_IMP_LEVEL_IMPERSONATE, NULL, EOAC_NONE);
    if (FAILED(hr))
    {
        IWbemServices_Release(*Services);
        *Services = NULL;
    }
    return hr;
}

static HRESULT
WmiFindObject(
    _In_ IWbemServices* Services,
    _In_ PCWSTR Query,
    _Out_ IWbemClassObject** Object)
{
    IEnumWbemClassObject* Enum = NULL;
    BSTR Language, Text;
    ULONG Returned = 0;
    HRESULT hr;

    *Object = NULL;
    Language = SysAllocString(L"WQL");
    Text = SysAllocString(Query);
    hr = (Language && Text) ? IWbemServices_ExecQuery(Services, Language, Text,
                                                      WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY,
                                                      NULL, &Enum)
                            : E_OUTOFMEMORY;
    SysFreeString(Language);
    SysFreeString(Text);
    if (SUCCEEDED(hr))
    {
        hr = IEnumWbemClassObject_Next(Enum, WBEM_INFINITE, 1, Object, &Returned);
        if (SUCCEEDED(hr) && Returned == 0)
            hr = WBEM_E_NOT_FOUND;
        IEnumWbemClassObject_Release(Enum);
    }
    if (FAILED(hr))
        HostLog(L"WMI query '%s' failed: 0x%08lx", Query, hr);
    return hr;
}

static HRESULT
WmiGetString(
    _In_ IWbemClassObject* Object,
    _In_ PCWSTR Name,
    _Out_ BSTR* Value)
{
    VARIANT Variant;
    HRESULT hr;

    *Value = NULL;
    VariantInit(&Variant);
    hr = IWbemClassObject_Get(Object, Name, 0, &Variant, NULL, NULL);
    if (SUCCEEDED(hr) && V_VT(&Variant) != VT_BSTR)
        hr = WBEM_E_TYPE_MISMATCH;
    if (SUCCEEDED(hr))
    {
        *Value = V_BSTR(&Variant);
        return hr;
    }
    VariantClear(&Variant);
    return hr;
}

static HRESULT
WmiGetUInt64(
    _In_ IWbemClassObject* Object,
    _In_ PCWSTR Name,
    _Out_ PULONGLONG Value)
{
    VARIANT Variant;
    HRESULT hr;

    *Value = 0;
    VariantInit(&Variant);
    hr = IWbemClassObject_Get(Object, Name, 0, &Variant, NULL, NULL);
    if (FAILED(hr))
        return hr;
    switch (V_VT(&Variant))
    {
        case VT_BSTR:
            *Value = _wcstoui64(V_BSTR(&Variant), NULL, 10);
            break;
        case VT_I4:
        case VT_UI4:
            *Value = (ULONG)V_I4(&Variant);
            break;
        case VT_I8:
        case VT_UI8:
            *Value = V_UI8(&Variant);
            break;
        default:
            hr = WBEM_E_TYPE_MISMATCH;
            break;
    }
    VariantClear(&Variant);
    return hr;
}

static HRESULT
WmiPut(
    _In_ IWbemClassObject* Input,
    _In_ PCWSTR Name,
    _In_ VARTYPE Type,
    _In_opt_ PCWSTR Text,
    _In_ BOOL Flag)
{
    VARIANT Variant;
    HRESULT hr;

    VariantInit(&Variant);
    V_VT(&Variant) = Type;
    if (Type == VT_BSTR)
    {
        V_BSTR(&Variant) = SysAllocString(Text);
        if (!V_BSTR(&Variant))
            return E_OUTOFMEMORY;
    }
    else
    {
        V_BOOL(&Variant) = Flag ? VARIANT_TRUE : VARIANT_FALSE;
    }
    hr = IWbemClassObject_Put(Input, Name, 0, &Variant, 0);
    VariantClear(&Variant);
    return hr;
}

static HRESULT
WmiPutUInt64(
    _In_ IWbemClassObject* Input,
    _In_ PCWSTR Name,
    _In_ ULONGLONG Value)
{
    WCHAR Text[32];

    StringCchPrintfW(Text, ARRAYSIZE(Text), L"%I64u", Value);
    return WmiPut(Input, Name, VT_BSTR, Text, FALSE);
}

static HRESULT
WmiSpawnInput(
    _In_ IWbemServices* Services,
    _In_ PCWSTR ClassName,
    _In_ PCWSTR Method,
    _Out_ IWbemClassObject** Input)
{
    IWbemClassObject *Class = NULL, *Definition = NULL;
    BSTR Name;
    HRESULT hr;

    *Input = NULL;
    Name = SysAllocString(ClassName);
    hr = Name ? IWbemServices_GetObject(Services, Name, 0, NULL, &Class, NULL) : E_OUTOFMEMORY;
    SysFreeString(Name);
    if (FAILED(hr))
        return hr;
    hr = IWbemClassObject_GetMethod(Class, Method, 0, &Definition, NULL);
    if (SUCCEEDED(hr) && Definition)
    {
        hr = IWbemClassObject_SpawnInstance(Definition, 0, Input);
        IWbemClassObject_Release(Definition);
    }
    IWbemClassObject_Release(Class);
    return hr;
}

static DWORD
WmiCall(
    _In_ IWbemServices* Services,
    _In_ IWbemClassObject* Object,
    _In_ PCWSTR Method,
    _In_opt_ IWbemClassObject* Input,
    _Out_opt_ IWbemClassObject** Output)
{
    IWbemClassObject* Out = NULL;
    ULONGLONG ReturnValue = 0;
    BSTR Path = NULL, Name;
    HRESULT hr;

    if (Output)
        *Output = NULL;
    hr = WmiGetString(Object, L"__PATH", &Path);
    Name = SysAllocString(Method);
    if (SUCCEEDED(hr))
        hr = Name ? IWbemServices_ExecMethod(Services, Path, Name, 0, NULL, Input, &Out, NULL) : E_OUTOFMEMORY;
    SysFreeString(Name);
    SysFreeString(Path);
    if (SUCCEEDED(hr) && Out)
        hr = WmiGetUInt64(Out, L"ReturnValue", &ReturnValue);
    else if (SUCCEEDED(hr))
        hr = WBEM_E_FAILED;
    HostLog(L"WMI %s: 0x%08lx, return value %I64u", Method, hr, ReturnValue);

    if (SUCCEEDED(hr) && ReturnValue == 0 && Output)
        *Output = Out;
    else if (Out)
        IWbemClassObject_Release(Out);
    if (FAILED(hr))
        return (DWORD)hr;
    return (DWORD)ReturnValue;
}

static HRESULT
FindPartitionObject(
    _In_ IWbemServices* Services,
    _In_ PHOST_PARTITION Partition,
    _Out_ IWbemClassObject** Object)
{
    WCHAR Query[160];

    StringCchPrintfW(Query, ARRAYSIZE(Query),
                     L"SELECT * FROM MSFT_Partition WHERE DiskNumber=%lu AND PartitionNumber=%lu",
                     Partition->DiskNumber, Partition->PartitionNumber);
    return WmiFindObject(Services, Query, Object);
}

DWORD
HostGetSupportedSize(
    _In_ PHOST_PARTITION Partition,
    _Out_ PULONGLONG MinBytes,
    _Out_ PULONGLONG MaxBytes)
{
    IWbemServices* Services;
    IWbemClassObject *Object = NULL, *Output = NULL;
    DWORD Error;
    HRESULT hr;

    *MinBytes = *MaxBytes = 0;
    hr = WmiConnect(&Services);
    if (FAILED(hr))
        return (DWORD)hr;
    hr = FindPartitionObject(Services, Partition, &Object);
    Error = FAILED(hr) ? (DWORD)hr : WmiCall(Services, Object, L"GetSupportedSize", NULL, &Output);
    if (Error == ERROR_SUCCESS)
    {
        hr = WmiGetUInt64(Output, L"SizeMin", MinBytes);
        if (SUCCEEDED(hr))
            hr = WmiGetUInt64(Output, L"SizeMax", MaxBytes);
        Error = FAILED(hr) ? (DWORD)hr : ERROR_SUCCESS;
    }
    if (Output)
        IWbemClassObject_Release(Output);
    if (Object)
        IWbemClassObject_Release(Object);
    IWbemServices_Release(Services);
    HostLog(L"Partition %lu:%lu supports %I64u..%I64u bytes: %lu", Partition->DiskNumber,
            Partition->PartitionNumber, *MinBytes, *MaxBytes, Error);
    return Error;
}

DWORD
HostResizePartition(
    _In_ PHOST_PARTITION Partition,
    _In_ ULONGLONG Bytes)
{
    IWbemServices* Services;
    IWbemClassObject *Object = NULL, *Input = NULL;
    DWORD Error;
    HRESULT hr;

    hr = WmiConnect(&Services);
    if (FAILED(hr))
        return (DWORD)hr;
    hr = FindPartitionObject(Services, Partition, &Object);
    if (SUCCEEDED(hr))
        hr = WmiSpawnInput(Services, L"MSFT_Partition", L"Resize", &Input);
    if (SUCCEEDED(hr))
        hr = WmiPutUInt64(Input, L"Size", Bytes);
    Error = FAILED(hr) ? (DWORD)hr : WmiCall(Services, Object, L"Resize", Input, NULL);
    if (Input)
        IWbemClassObject_Release(Input);
    if (Object)
        IWbemClassObject_Release(Object);
    IWbemServices_Release(Services);
    if (Error == ERROR_SUCCESS)
        Partition->Length = Bytes;
    return Error;
}

typedef struct _ID_SEARCH
{
    const GUID* PartitionId;
    WCHAR DeviceName[MAX_PATH];
} ID_SEARCH, *PID_SEARCH;

static BOOL
FindPartitionIdCallback(
    _In_ PCWSTR DeviceName,
    _In_ PVOID Context)
{
    PID_SEARCH Search = Context;
    PARTITION_INFORMATION_EX Info;
    WCHAR Path[MAX_PATH];
    DWORD Returned;
    HANDLE Volume;
    BOOL Found;

    if (!HostPath(DeviceName, NULL, Path))
        return TRUE;
    Volume = CreateFileW(Path, 0, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    if (Volume == INVALID_HANDLE_VALUE)
        return TRUE;
    Found = DeviceIoControl(Volume, IOCTL_DISK_GET_PARTITION_INFO_EX, NULL, 0, &Info, sizeof(Info), &Returned, NULL) &&
            Info.PartitionStyle == PARTITION_STYLE_GPT &&
            IsEqualGUID(&Info.Gpt.PartitionId, Search->PartitionId);
    CloseHandle(Volume);
    if (Found)
        StringCchCopyW(Search->DeviceName, ARRAYSIZE(Search->DeviceName), DeviceName);
    return !Found;
}

static DWORD
WaitForPartitionVolume(
    _In_ const GUID* PartitionId,
    _Out_ PHOST_PARTITION Partition)
{
    ID_SEARCH Search;
    DWORD Waited;

    for (Waited = 0; Waited <= HOST_VOLUME_WAIT_MS; Waited += HOST_VOLUME_POLL_MS)
    {
        ZeroMemory(&Search, sizeof(Search));
        Search.PartitionId = PartitionId;
        EnumerateVolumes(FindPartitionIdCallback, &Search);
        if (Search.DeviceName[0])
            return HostQueryPartition(Search.DeviceName, Partition);
        Sleep(HOST_VOLUME_POLL_MS);
    }
    HostLog(L"The volume of the new partition did not arrive");
    return ERROR_TIMEOUT;
}

DWORD
HostCreatePartition(
    _In_ PHOST_PARTITION After,
    _In_ ULONGLONG Bytes,
    _Out_ PHOST_PARTITION Created)
{
    IWbemServices* Services;
    IWbemClassObject *Disk = NULL, *Input = NULL, *Output = NULL, *Partition = NULL;
    ULONGLONG Offset = ALIGN_UP_BY(After->Offset + After->Length, 1024 * 1024);
    WCHAR Query[96];
    VARIANT Variant;
    BSTR Id = NULL;
    GUID PartitionId;
    DWORD Error;
    HRESULT hr;

    ZeroMemory(Created, sizeof(*Created));
    hr = WmiConnect(&Services);
    if (FAILED(hr))
        return (DWORD)hr;

    StringCchPrintfW(Query, ARRAYSIZE(Query), L"SELECT * FROM MSFT_Disk WHERE Number=%lu", After->DiskNumber);
    hr = WmiFindObject(Services, Query, &Disk);
    if (SUCCEEDED(hr))
        hr = WmiSpawnInput(Services, L"MSFT_Disk", L"CreatePartition", &Input);
    if (SUCCEEDED(hr))
        hr = WmiPutUInt64(Input, L"Size", Bytes);
    if (SUCCEEDED(hr))
        hr = WmiPutUInt64(Input, L"Offset", Offset);
    if (SUCCEEDED(hr))
        hr = WmiPut(Input, L"GptType", VT_BSTR, HOST_BASIC_DATA_TYPE, FALSE);
    if (SUCCEEDED(hr))
        hr = WmiPut(Input, L"AssignDriveLetter", VT_BOOL, NULL, FALSE);
    Error = FAILED(hr) ? (DWORD)hr : WmiCall(Services, Disk, L"CreatePartition", Input, &Output);

    if (Error == ERROR_SUCCESS)
    {
        VariantInit(&Variant);
        hr = IWbemClassObject_Get(Output, L"CreatedPartition", 0, &Variant, NULL, NULL);
        if (SUCCEEDED(hr) && V_VT(&Variant) == VT_UNKNOWN && V_UNKNOWN(&Variant))
            hr = IUnknown_QueryInterface(V_UNKNOWN(&Variant), &IID_IWbemClassObject, (PVOID*)&Partition);
        else if (SUCCEEDED(hr))
            hr = WBEM_E_TYPE_MISMATCH;
        VariantClear(&Variant);
        if (SUCCEEDED(hr))
            hr = WmiGetString(Partition, L"Guid", &Id);
        if (SUCCEEDED(hr))
            hr = IIDFromString(Id, &PartitionId);
        Error = FAILED(hr) ? (DWORD)hr : ERROR_SUCCESS;
        SysFreeString(Id);
    }

    if (Partition)
        IWbemClassObject_Release(Partition);
    if (Output)
        IWbemClassObject_Release(Output);
    if (Input)
        IWbemClassObject_Release(Input);
    if (Disk)
        IWbemClassObject_Release(Disk);
    IWbemServices_Release(Services);

    if (Error == ERROR_SUCCESS)
        Error = WaitForPartitionVolume(&PartitionId, Created);
    HostLog(L"Partition of %I64u bytes created at %I64u on disk %lu: %lu (%s)", Bytes, Offset, After->DiskNumber,
            Error, Created->DeviceName);
    return Error;
}

static BOOL
GetVolumeGuidPath(
    _In_ PCWSTR DeviceName,
    _Out_writes_(MAX_PATH) PWSTR VolumeName)
{
    WCHAR Candidate[MAX_PATH], Device[MAX_PATH];
    HANDLE Find;
    BOOL Found = FALSE;

    Find = FindFirstVolumeW(Candidate, ARRAYSIZE(Candidate));
    if (Find == INVALID_HANDLE_VALUE)
        return FALSE;
    do
    {
        Found = GetVolumeDeviceName(Candidate, Device) && _wcsicmp(Device, DeviceName) == 0;
        if (Found)
            StringCchCopyW(VolumeName, MAX_PATH, Candidate);
    } while (!Found && FindNextVolumeW(Find, Candidate, ARRAYSIZE(Candidate)));
    FindVolumeClose(Find);
    return Found;
}

DWORD
HostFormatPartition(
    _In_ PHOST_PARTITION Partition,
    _In_ PCWSTR Label)
{
    IWbemServices* Services;
    IWbemClassObject *Volume = NULL, *Input = NULL;
    WCHAR VolumeName[MAX_PATH], Query[2 * MAX_PATH + 64];
    PWSTR Out;
    PCWSTR In;
    DWORD Error;
    HRESULT hr;

    if (!GetVolumeGuidPath(Partition->DeviceName, VolumeName))
        return ERROR_NOT_FOUND;

    StringCchCopyW(Query, ARRAYSIZE(Query), L"SELECT * FROM MSFT_Volume WHERE Path='");
    for (Out = Query + wcslen(Query), In = VolumeName; *In && Out < Query + ARRAYSIZE(Query) - 4; In++)
    {
        if (*In == L'\\' || *In == L'\'')
            *Out++ = L'\\';
        *Out++ = *In;
    }
    *Out++ = L'\'';
    *Out = UNICODE_NULL;

    hr = WmiConnect(&Services);
    if (FAILED(hr))
        return (DWORD)hr;
    hr = WmiFindObject(Services, Query, &Volume);
    if (SUCCEEDED(hr))
        hr = WmiSpawnInput(Services, L"MSFT_Volume", L"Format", &Input);
    if (SUCCEEDED(hr))
        hr = WmiPut(Input, L"FileSystem", VT_BSTR, L"NTFS", FALSE);
    if (SUCCEEDED(hr))
        hr = WmiPut(Input, L"FileSystemLabel", VT_BSTR, Label, FALSE);
    if (SUCCEEDED(hr))
        hr = WmiPut(Input, L"Full", VT_BOOL, NULL, FALSE);
    if (SUCCEEDED(hr))
        hr = WmiPut(Input, L"Force", VT_BOOL, NULL, TRUE);
    Error = FAILED(hr) ? (DWORD)hr : WmiCall(Services, Volume, L"Format", Input, NULL);
    if (Input)
        IWbemClassObject_Release(Input);
    if (Volume)
        IWbemClassObject_Release(Volume);
    IWbemServices_Release(Services);
    HostLog(L"%s formatted as NTFS: %lu", Partition->DeviceName, Error);
    return Error;
}

DWORD
HostDeletePartition(
    _In_ PHOST_PARTITION Partition)
{
    IWbemServices* Services;
    IWbemClassObject* Object = NULL;
    DWORD Error;
    HRESULT hr;

    hr = WmiConnect(&Services);
    if (FAILED(hr))
        return (DWORD)hr;
    hr = FindPartitionObject(Services, Partition, &Object);
    Error = FAILED(hr) ? (DWORD)hr : WmiCall(Services, Object, L"DeletePartition", NULL, NULL);
    if (Object)
        IWbemClassObject_Release(Object);
    IWbemServices_Release(Services);
    return Error;
}

PCWSTR
HostGetRemovableBootName(
    _In_ WORD Machine)
{
    switch (Machine)
    {
        case IMAGE_FILE_MACHINE_AMD64:
            return L"bootx64.efi";
        case IMAGE_FILE_MACHINE_ARM64:
            return L"bootaa64.efi";
        case IMAGE_FILE_MACHINE_I386:
            return L"bootia32.efi";
        case IMAGE_FILE_MACHINE_RISCV64:
            return L"bootriscv64.efi";
    }
    return NULL;
}

static DWORD
EnsureDirectory(
    _In_ PCWSTR DeviceName,
    _In_ PCWSTR Relative)
{
    WCHAR Path[MAX_PATH], Saved;
    SIZE_T Index;

    if (!HostPath(DeviceName, Relative, Path))
        return ERROR_BUFFER_OVERFLOW;

    for (Index = wcslen(Path) - wcslen(Relative) + 1; ; Index++)
    {
        Saved = Path[Index];
        if (Saved != L'\\' && Saved != UNICODE_NULL)
            continue;
        Path[Index] = UNICODE_NULL;
        if (!CreateDirectoryW(Path, NULL) && GetLastError() != ERROR_ALREADY_EXISTS)
            return GetLastError();
        Path[Index] = Saved;
        if (Saved == UNICODE_NULL)
            return ERROR_SUCCESS;
    }
}

DWORD
HostCreateStageDirectory(
    _In_ PHOST_PARTITION Target)
{
    WCHAR Path[MAX_PATH];
    DWORD Error = EnsureDirectory(Target->DeviceName, L"\\" HOST_STAGE_DIRECTORY);

    if (Error == ERROR_SUCCESS && HostPath(Target->DeviceName, L"\\" HOST_STAGE_DIRECTORY, Path))
        SetFileAttributesW(Path, FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM);
    return Error;
}

DWORD
HostInstallSetupBoot(
    _In_ PCWSTR SourceRoot,
    _In_ WORD Machine,
    _In_ PHOST_PARTITION SystemPartition,
    _In_ PHOST_PARTITION Target)
{
    BYTE SystemDevice[BCD_DEVICE_ELEMENT_MAX], OsDevice[BCD_DEVICE_ELEMENT_MAX];
    WCHAR Media[MAX_PATH], Work[MAX_PATH], Saved[MAX_PATH], Store[MAX_PATH], Log[MAX_PATH];
    WCHAR Source[MAX_PATH], Destination[MAX_PATH];
    DWORD SystemSize = sizeof(SystemDevice), OsSize = sizeof(OsDevice), Error;
    PCWSTR BootName = HostGetRemovableBootName(Machine);
    HKEY Hive;
    UINT Index;
    static const PCWSTR LogSuffixes[] = { L".LOG", L".LOG1", L".LOG2" };

    if (!BootName)
        return ERROR_NOT_SUPPORTED;
    Error = EnsureDirectory(SystemPartition->DeviceName, HOST_SETUP_STORE_DIR);
    if (Error != ERROR_SUCCESS)
        return Error;
    if (FAILED(StringCchPrintfW(Source, ARRAYSIZE(Source), L"%sefi\\boot\\%s", SourceRoot, BootName)) ||
        !HostPath(SystemPartition->DeviceName, HOST_SETUP_MANAGER, Destination))
    {
        return ERROR_BUFFER_OVERFLOW;
    }
    Error = HostCopyFileVerified(Source, Destination);
    if (Error != ERROR_SUCCESS)
        return Error;
    if (FAILED(StringCchPrintfW(Source, ARRAYSIZE(Source), L"%sWindows\\system32\\winload.efi", SourceRoot)) ||
        !HostPath(SystemPartition->DeviceName, HOST_SETUP_LOADER, Destination))
    {
        return ERROR_BUFFER_OVERFLOW;
    }
    Error = HostCopyFileVerified(Source, Destination);
    if (Error != ERROR_SUCCESS)
        return Error;

    if (FAILED(StringCchPrintfW(Media, ARRAYSIZE(Media), L"%sloader\\media.bcd", SourceRoot)) ||
        !GetTempPathW(ARRAYSIZE(Work), Work) ||
        FAILED(StringCchCopyW(Saved, ARRAYSIZE(Saved), Work)) ||
        FAILED(StringCchCatW(Work, ARRAYSIZE(Work), L"LiberNT-Setup.bcd")) ||
        FAILED(StringCchCatW(Saved, ARRAYSIZE(Saved), L"LiberNT-Setup.saved.bcd")) ||
        !HostPath(SystemPartition->DeviceName, HOST_SETUP_STORE, Store))
    {
        return ERROR_BUFFER_OVERFLOW;
    }

    Error = BuildBcdPartitionDevice(&SystemPartition->PartitionId, &SystemPartition->DiskId, SystemDevice, &SystemSize);
    if (Error == ERROR_SUCCESS)
        Error = BuildBcdRamdiskDevice(&Target->PartitionId, &Target->DiskId, HOST_IMAGE_PATH, OsDevice, &OsSize);
    if (Error != ERROR_SUCCESS)
        return Error;

    for (Index = 0; Index < ARRAYSIZE(LogSuffixes); Index++)
    {
        if (SUCCEEDED(StringCchPrintfW(Log, ARRAYSIZE(Log), L"%s%s", Work, LogSuffixes[Index])))
            DeleteFileW(Log);
    }
    SetFileAttributesW(Work, FILE_ATTRIBUTE_NORMAL);
    DeleteFileW(Work);
    DeleteFileW(Saved);
    if (!CopyFileW(Media, Work, FALSE) || !SetFileAttributesW(Work, FILE_ATTRIBUTE_NORMAL))
        return GetLastError();

    Error = OpenBcdStoreFile(Work, &Hive);
    if (Error == ERROR_SUCCESS)
    {
        Error = WriteSetupBootEntry(Hive, HOST_SETUP_MANAGER, HOST_SETUP_LOADER, HOST_SETUP_DESCRIPTION,
                                    SystemDevice, SystemSize, OsDevice, OsSize);
        if (Error == ERROR_SUCCESS)
            Error = RegSaveKeyExW(Hive, Saved, NULL, REG_LATEST_FORMAT);
        RegCloseKey(Hive);
    }

    if (Error == ERROR_SUCCESS)
        Error = HostCopyFileVerified(Saved, Store);

    for (Index = 0; Index < ARRAYSIZE(LogSuffixes); Index++)
    {
        if (SUCCEEDED(StringCchPrintfW(Log, ARRAYSIZE(Log), L"%s%s", Work, LogSuffixes[Index])))
            DeleteFileW(Log);
    }
    DeleteFileW(Work);
    DeleteFileW(Saved);
    HostLog(L"Setup boot store written to %s: %lu", Store, Error);
    return Error;
}

static BOOL
GetFirmwareVariable(
    _In_ PCWSTR Name,
    _Out_writes_bytes_(Size) PVOID Value,
    _In_ DWORD Size,
    _Out_ PDWORD Length)
{
    *Length = GetFirmwareEnvironmentVariableExW(Name, EFI_GLOBAL_VARIABLE_GUID, Value, Size, NULL);
    return *Length != 0;
}

static BOOL
SetFirmwareVariable(
    _In_ PCWSTR Name,
    _In_reads_bytes_opt_(Size) PVOID Value,
    _In_ DWORD Size)
{
    return SetFirmwareEnvironmentVariableExW(Name, EFI_GLOBAL_VARIABLE_GUID, Value, Size,
                                             EFI_VARIABLE_BOOT_ATTRIBUTES);
}

BOOL
HostIsSecureBootOn(VOID)
{
    BOOLEAN Enabled;
    UCHAR Value = 0;
    DWORD Length;

    RtlAdjustPrivilege(SE_SYSTEM_ENVIRONMENT_PRIVILEGE, TRUE, FALSE, &Enabled);
    return GetFirmwareVariable(L"SecureBoot", &Value, sizeof(Value), &Length) && Value == 1;
}

BOOL
HostIsFirmwareSetupSupported(VOID)
{
    ULONGLONG Supported = 0;
    BOOLEAN Enabled;
    DWORD Length;

    RtlAdjustPrivilege(SE_SYSTEM_ENVIRONMENT_PRIVILEGE, TRUE, FALSE, &Enabled);
    return GetFirmwareVariable(L"OsIndicationsSupported", &Supported, sizeof(Supported), &Length) &&
           (Supported & EFI_OS_INDICATIONS_BOOT_TO_FW_UI);
}

BOOL
HostSetFirmwareSetupRequest(VOID)
{
    ULONGLONG Indications = 0;
    BOOLEAN Enabled;
    DWORD Length;

    RtlAdjustPrivilege(SE_SYSTEM_ENVIRONMENT_PRIVILEGE, TRUE, FALSE, &Enabled);
    if (!GetFirmwareVariable(L"OsIndications", &Indications, sizeof(Indications), &Length))
        Indications = 0;
    Indications |= EFI_OS_INDICATIONS_BOOT_TO_FW_UI;
    return SetFirmwareVariable(L"OsIndications", &Indications, sizeof(Indications));
}

static DWORD
GetGptSlot(
    _In_ PHOST_PARTITION Partition,
    _Out_ PULONG Slot)
{
    WCHAR Path[MAX_PATH];
    PUCHAR Header = NULL, Entries = NULL;
    ULONGLONG EntriesLba;
    ULONG Count, EntrySize, Bytes, Index;
    HANDLE Disk;
    DWORD Error = ERROR_NOT_FOUND;

    *Slot = 0;
    if (!Partition->SectorSize ||
        FAILED(StringCchPrintfW(Path, ARRAYSIZE(Path), L"\\\\.\\PhysicalDrive%lu", Partition->DiskNumber)))
    {
        return ERROR_INVALID_PARAMETER;
    }
    Disk = CreateFileW(Path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    if (Disk == INVALID_HANDLE_VALUE)
        return GetLastError();

    Header = HeapAlloc(ProcessHeap, 0, Partition->SectorSize);
    if (!Header || !ReadAt(Disk, Partition->SectorSize, Header, Partition->SectorSize) ||
        memcmp(Header, "EFI PART", 8) != 0)
    {
        Error = Header ? ERROR_BAD_FORMAT : ERROR_NOT_ENOUGH_MEMORY;
    }
    else
    {
        EntriesLba = *(PULONGLONG)(Header + GPT_HEADER_ENTRIES_LBA);
        Count = *(PULONG)(Header + GPT_HEADER_ENTRY_COUNT);
        EntrySize = *(PULONG)(Header + GPT_HEADER_ENTRY_SIZE);
        Bytes = (ULONG)ALIGN_UP_BY((ULONGLONG)Count * EntrySize, Partition->SectorSize);
        if (EntrySize < GPT_ENTRY_UNIQUE_ID + sizeof(GUID) || Bytes > GPT_MAX_ENTRY_BYTES)
        {
            Error = ERROR_BAD_FORMAT;
        }
        else
        {
            Entries = HeapAlloc(ProcessHeap, 0, Bytes);
            if (!Entries || !ReadAt(Disk, EntriesLba * Partition->SectorSize, Entries, Bytes))
            {
                Error = Entries ? ERROR_READ_FAULT : ERROR_NOT_ENOUGH_MEMORY;
            }
            else
            {
                for (Index = 0; Index < Count; Index++)
                {
                    if (memcmp(Entries + (SIZE_T)Index * EntrySize + GPT_ENTRY_UNIQUE_ID, &Partition->PartitionId,
                               sizeof(GUID)) == 0)
                    {
                        *Slot = Index + 1;
                        Error = ERROR_SUCCESS;
                        break;
                    }
                }
            }
        }
    }

    if (Entries)
        HeapFree(ProcessHeap, 0, Entries);
    if (Header)
        HeapFree(ProcessHeap, 0, Header);
    CloseHandle(Disk);
    return Error;
}

static DWORD
BuildLoadOption(
    _In_ PHOST_PARTITION SystemPartition,
    _In_ PCWSTR FilePath,
    _In_ PCWSTR Description,
    _Out_writes_bytes_(EFI_LOAD_OPTION_MAX) PUCHAR Option,
    _Out_ PDWORD OptionSize)
{
    DWORD DescriptionSize = (DWORD)((wcslen(Description) + 1) * sizeof(WCHAR));
    DWORD FileSize = (DWORD)((wcslen(FilePath) + 1) * sizeof(WCHAR));
    DWORD PathSize = EFI_HARDDRIVE_NODE_SIZE + EFI_DEVICE_PATH_HEADER_SIZE + FileSize + EFI_DEVICE_PATH_HEADER_SIZE;
    PUCHAR Node;
    ULONG Slot;
    DWORD Error;

    *OptionSize = EFI_LOAD_OPTION_HEADER_SIZE + DescriptionSize + PathSize;
    if (*OptionSize > EFI_LOAD_OPTION_MAX || !SystemPartition->SectorSize)
        return ERROR_BUFFER_OVERFLOW;
    Error = GetGptSlot(SystemPartition, &Slot);
    if (Error != ERROR_SUCCESS)
        return Error;

    ZeroMemory(Option, EFI_LOAD_OPTION_MAX);
    *(PULONG)Option = EFI_LOAD_OPTION_ACTIVE;
    *(PUSHORT)(Option + sizeof(ULONG)) = (USHORT)PathSize;
    CopyMemory(Option + EFI_LOAD_OPTION_HEADER_SIZE, Description, DescriptionSize);

    Node = Option + EFI_LOAD_OPTION_HEADER_SIZE + DescriptionSize;
    Node[0] = EFI_MEDIA_DEVICE_PATH;
    Node[1] = EFI_MEDIA_HARDDRIVE_DP;
    *(PUSHORT)(Node + 2) = EFI_HARDDRIVE_NODE_SIZE;
    *(PULONG)(Node + 4) = Slot;
    *(PULONGLONG)(Node + 8) = SystemPartition->Offset / SystemPartition->SectorSize;
    *(PULONGLONG)(Node + 16) = SystemPartition->Length / SystemPartition->SectorSize;
    CopyMemory(Node + 24, &SystemPartition->PartitionId, sizeof(GUID));
    Node[40] = EFI_PARTITION_FORMAT_GPT;
    Node[41] = EFI_SIGNATURE_TYPE_GUID;

    Node += EFI_HARDDRIVE_NODE_SIZE;
    Node[0] = EFI_MEDIA_DEVICE_PATH;
    Node[1] = EFI_MEDIA_FILEPATH_DP;
    *(PUSHORT)(Node + 2) = (USHORT)(EFI_DEVICE_PATH_HEADER_SIZE + FileSize);
    CopyMemory(Node + EFI_DEVICE_PATH_HEADER_SIZE, FilePath, FileSize);

    Node += EFI_DEVICE_PATH_HEADER_SIZE + FileSize;
    Node[0] = EFI_END_DEVICE_PATH_TYPE;
    Node[1] = EFI_END_ENTIRE_DEVICE_PATH;
    *(PUSHORT)(Node + 2) = EFI_DEVICE_PATH_HEADER_SIZE;
    return ERROR_SUCCESS;
}

static BOOL
IsLoadOptionFor(
    _In_reads_bytes_(Length) const UCHAR* Option,
    _In_ DWORD Length,
    _In_opt_ PHOST_PARTITION SystemPartition,
    _In_ PCWSTR FilePath)
{
    DWORD Offset = EFI_LOAD_OPTION_HEADER_SIZE, End, NodeLength, Chars;
    BOOL SameDisk = FALSE, SameFile = FALSE;
    const WCHAR* Text;

    if (Length < EFI_LOAD_OPTION_HEADER_SIZE)
        return FALSE;
    while (Offset + sizeof(WCHAR) <= Length && *(const WCHAR*)(Option + Offset) != UNICODE_NULL)
        Offset += sizeof(WCHAR);
    Offset += sizeof(WCHAR);
    End = Offset + *(const USHORT*)(Option + sizeof(ULONG));
    if (End > Length)
        return FALSE;

    while (Offset + EFI_DEVICE_PATH_HEADER_SIZE <= End && Option[Offset] != EFI_END_DEVICE_PATH_TYPE)
    {
        NodeLength = *(const USHORT*)(Option + Offset + 2);
        if (NodeLength < EFI_DEVICE_PATH_HEADER_SIZE || Offset + NodeLength > End)
            return FALSE;
        if (SystemPartition && Option[Offset] == EFI_MEDIA_DEVICE_PATH &&
            Option[Offset + 1] == EFI_MEDIA_HARDDRIVE_DP && NodeLength >= EFI_HARDDRIVE_NODE_SIZE)
        {
            SameDisk = memcmp(Option + Offset + 24, &SystemPartition->PartitionId, sizeof(GUID)) == 0;
        }
        else if (Option[Offset] == EFI_MEDIA_DEVICE_PATH && Option[Offset + 1] == EFI_MEDIA_FILEPATH_DP)
        {
            Text = (const WCHAR*)(Option + Offset + EFI_DEVICE_PATH_HEADER_SIZE);
            Chars = (NodeLength - EFI_DEVICE_PATH_HEADER_SIZE) / sizeof(WCHAR);
            while (Chars && Text[Chars - 1] == UNICODE_NULL)
                Chars--;
            SameFile = Chars == wcslen(FilePath) && _wcsnicmp(Text, FilePath, Chars) == 0;
        }
        Offset += NodeLength;
    }
    return (!SystemPartition || SameDisk) && SameFile;
}

static DWORD
FindLoadOptions(
    _In_ PHOST_PARTITION SystemPartition,
    _In_ PCWSTR FilePath,
    _Out_writes_(MaxCount) PUSHORT Numbers,
    _In_ ULONG MaxCount,
    _Out_ PULONG Count,
    _Out_opt_ PUSHORT FreeNumber)
{
    UCHAR Option[EFI_LOAD_OPTION_MAX];
    WCHAR Name[16];
    DWORD Length;
    ULONG Number;
    BOOL HaveFree = FALSE;

    *Count = 0;
    for (Number = 0; Number < EFI_BOOT_OPTION_SEARCH_MAX; Number++)
    {
        StringCchPrintfW(Name, ARRAYSIZE(Name), L"Boot%04X", Number);
        if (!GetFirmwareVariable(Name, Option, sizeof(Option), &Length))
        {
            if (GetLastError() == ERROR_ENVVAR_NOT_FOUND && !HaveFree && FreeNumber)
            {
                *FreeNumber = (USHORT)Number;
                HaveFree = TRUE;
            }
            continue;
        }
        if (*Count < MaxCount && IsLoadOptionFor(Option, Length, SystemPartition, FilePath))
            Numbers[(*Count)++] = (USHORT)Number;
    }
    return (FreeNumber && !HaveFree && *Count == 0) ? ERROR_NO_MORE_ITEMS : ERROR_SUCCESS;
}

DWORD
HostSetBootNext(
    _In_ PHOST_PARTITION SystemPartition,
    _In_ PCWSTR FilePath,
    _In_ PCWSTR Description)
{
    UCHAR Option[EFI_LOAD_OPTION_MAX];
    USHORT Numbers[16], Number = 0, Check = 0;
    WCHAR Name[16];
    DWORD OptionSize, Length, Error;
    ULONG Count;
    BOOLEAN Enabled;

    RtlAdjustPrivilege(SE_SYSTEM_ENVIRONMENT_PRIVILEGE, TRUE, FALSE, &Enabled);
    Error = BuildLoadOption(SystemPartition, FilePath, Description, Option, &OptionSize);
    if (Error == ERROR_SUCCESS)
        Error = FindLoadOptions(SystemPartition, FilePath, Numbers, ARRAYSIZE(Numbers), &Count, &Number);
    if (Error != ERROR_SUCCESS)
        return Error;

    if (Count)
    {
        Number = Numbers[0];
    }
    else
    {
        StringCchPrintfW(Name, ARRAYSIZE(Name), L"Boot%04X", Number);
        if (!SetFirmwareVariable(Name, Option, OptionSize))
            return GetLastError();
    }

    if (!SetFirmwareVariable(L"BootNext", &Number, sizeof(Number)) ||
        !GetFirmwareVariable(L"BootNext", &Check, sizeof(Check), &Length))
    {
        return GetLastError();
    }
    HostLog(L"BootNext set to Boot%04X (%s), %s option", Number, FilePath, Count ? L"existing" : L"new");
    return Check == Number ? ERROR_SUCCESS : ERROR_INVALID_DATA;
}

static VOID
RemoveFromBootOrder(
    _In_reads_(Count) const USHORT* Numbers,
    _In_ ULONG Count)
{
    USHORT Order[EFI_BOOT_ORDER_MAX / sizeof(USHORT)];
    DWORD Length;
    ULONG In, Out, Index;
    BOOL Keep;

    if (!GetFirmwareVariable(L"BootOrder", Order, sizeof(Order), &Length))
        return;
    for (In = 0, Out = 0; In < Length / sizeof(USHORT); In++)
    {
        for (Keep = TRUE, Index = 0; Index < Count; Index++)
            Keep = Keep && Order[In] != Numbers[Index];
        if (Keep)
            Order[Out++] = Order[In];
    }
    if (Out != Length / sizeof(USHORT))
        SetFirmwareVariable(L"BootOrder", Order, Out * sizeof(USHORT));
}

typedef enum _HOST_BOOT_MANAGER
{
    HostBootOther,
    HostBootWindows,
    HostBootLiberNT
} HOST_BOOT_MANAGER;

static HOST_BOOT_MANAGER
GetBootManager(
    _In_ USHORT Number)
{
    UCHAR Option[EFI_LOAD_OPTION_MAX];
    WCHAR Name[16];
    DWORD Length;

    StringCchPrintfW(Name, ARRAYSIZE(Name), L"Boot%04X", Number);
    if (!GetFirmwareVariable(Name, Option, sizeof(Option), &Length))
        return HostBootOther;
    if (IsLoadOptionFor(Option, Length, NULL, HOST_WINDOWS_MANAGER))
        return HostBootWindows;
    if (IsLoadOptionFor(Option, Length, NULL, HOST_LIBERNT_MANAGER))
        return HostBootLiberNT;
    return HostBootOther;
}

BOOL
HostGetBootManagerOrder(
    _Out_ PBOOL WindowsFirst)
{
    USHORT Order[EFI_BOOT_ORDER_MAX / sizeof(USHORT)];
    HOST_BOOT_MANAGER Manager, First = HostBootOther;
    BOOL Windows = FALSE, LiberNT = FALSE;
    DWORD Length;
    ULONG Index;
    BOOLEAN Enabled;

    *WindowsFirst = FALSE;
    RtlAdjustPrivilege(SE_SYSTEM_ENVIRONMENT_PRIVILEGE, TRUE, FALSE, &Enabled);
    if (!GetFirmwareVariable(L"BootOrder", Order, sizeof(Order), &Length))
        return FALSE;
    for (Index = 0; Index < Length / sizeof(USHORT); Index++)
    {
        Manager = GetBootManager(Order[Index]);
        if (First == HostBootOther)
            First = Manager;
        Windows = Windows || Manager == HostBootWindows;
        LiberNT = LiberNT || Manager == HostBootLiberNT;
    }
    *WindowsFirst = First == HostBootWindows;
    HostLog(L"Boot order: Windows Boot Manager %s, LiberNT Boot Manager %s, %s first",
            Windows ? L"present" : L"absent", LiberNT ? L"present" : L"absent",
            First == HostBootWindows ? L"Windows" : First == HostBootLiberNT ? L"LiberNT" : L"none");
    return Windows && LiberNT;
}

DWORD
HostSetBootManagerFirst(
    _In_ BOOL WindowsFirst)
{
    USHORT Order[EFI_BOOT_ORDER_MAX / sizeof(USHORT)], Sorted[EFI_BOOT_ORDER_MAX / sizeof(USHORT)];
    USHORT Check[EFI_BOOT_ORDER_MAX / sizeof(USHORT)];
    HOST_BOOT_MANAGER Manager[EFI_BOOT_ORDER_MAX / sizeof(USHORT)];
    HOST_BOOT_MANAGER Wanted = WindowsFirst ? HostBootWindows : HostBootLiberNT;
    DWORD Length, CheckLength;
    ULONG Count, First, Out = 0, Index;
    BOOLEAN Enabled;

    RtlAdjustPrivilege(SE_SYSTEM_ENVIRONMENT_PRIVILEGE, TRUE, FALSE, &Enabled);
    if (!GetFirmwareVariable(L"BootOrder", Order, sizeof(Order), &Length))
        return GetLastError();
    Count = Length / sizeof(USHORT);
    for (First = Count, Index = 0; Index < Count; Index++)
    {
        Manager[Index] = GetBootManager(Order[Index]);
        if (Manager[Index] != HostBootOther && First == Count)
            First = Index;
    }

    for (Index = 0; Index < First; Index++)
        Sorted[Out++] = Order[Index];
    for (Index = First; Index < Count; Index++)
    {
        if (Manager[Index] == Wanted)
            Sorted[Out++] = Order[Index];
    }
    for (Index = First; Index < Count; Index++)
    {
        if (Manager[Index] != Wanted)
            Sorted[Out++] = Order[Index];
    }

    if (memcmp(Sorted, Order, Count * sizeof(USHORT)) == 0)
    {
        HostLog(L"Boot order kept for %s Boot Manager first", WindowsFirst ? L"Windows" : L"LiberNT");
        return ERROR_SUCCESS;
    }
    if (!SetFirmwareVariable(L"BootOrder", Sorted, Count * sizeof(USHORT)) ||
        !GetFirmwareVariable(L"BootOrder", Check, sizeof(Check), &CheckLength))
    {
        return GetLastError();
    }
    HostLog(L"Boot order changed so that %s Boot Manager starts first", WindowsFirst ? L"Windows" : L"LiberNT");
    return (CheckLength == Count * sizeof(USHORT) && memcmp(Check, Sorted, CheckLength) == 0) ?
           ERROR_SUCCESS : ERROR_INVALID_DATA;
}

DWORD
HostRemoveSetupBoot(
    _In_ PHOST_PARTITION SystemPartition)
{
    static const PCWSTR Files[] = { HOST_SETUP_MANAGER, HOST_SETUP_LOADER, HOST_SETUP_STORE };
    USHORT Numbers[16], Next;
    WCHAR Path[MAX_PATH], Name[16];
    DWORD Length, Error = ERROR_SUCCESS;
    ULONG Count, Index;
    BOOLEAN Enabled;

    RtlAdjustPrivilege(SE_SYSTEM_ENVIRONMENT_PRIVILEGE, TRUE, FALSE, &Enabled);
    if (FindLoadOptions(SystemPartition, HOST_SETUP_MANAGER, Numbers, ARRAYSIZE(Numbers), &Count, NULL) ==
        ERROR_SUCCESS)
    {
        if (GetFirmwareVariable(L"BootNext", &Next, sizeof(Next), &Length))
        {
            for (Index = 0; Index < Count; Index++)
            {
                if (Next == Numbers[Index])
                    SetFirmwareVariable(L"BootNext", NULL, 0);
            }
        }
        RemoveFromBootOrder(Numbers, Count);
        for (Index = 0; Index < Count; Index++)
        {
            StringCchPrintfW(Name, ARRAYSIZE(Name), L"Boot%04X", Numbers[Index]);
            if (!SetFirmwareVariable(Name, NULL, 0) && Error == ERROR_SUCCESS)
                Error = GetLastError();
        }
    }

    for (Index = 0; Index < ARRAYSIZE(Files); Index++)
    {
        if (HostPath(SystemPartition->DeviceName, Files[Index], Path) && !DeleteFileW(Path) &&
            GetLastError() != ERROR_FILE_NOT_FOUND && GetLastError() != ERROR_PATH_NOT_FOUND && Error == ERROR_SUCCESS)
        {
            Error = GetLastError();
        }
    }
    if (HostPath(SystemPartition->DeviceName, HOST_SETUP_STORE_DIR, Path))
        RemoveDirectoryW(Path);
    HostLog(L"Setup boot files and options removed: %lu", Error);
    return Error;
}

DWORD
HostReadUpgradeStage(
    _In_ PHOST_PARTITION Target,
    _Out_writes_(Chars) PWSTR Stage,
    _In_ DWORD Chars)
{
    WCHAR Path[MAX_PATH];

    Stage[0] = UNICODE_NULL;
    if (!HostPath(Target->DeviceName, L"\\" HOST_STAGE_DIRECTORY L"\\install.log", Path))
        return ERROR_BUFFER_OVERFLOW;
    GetPrivateProfileStringW(L"Install", L"Stage", L"", Stage, Chars, Path);
    return ERROR_SUCCESS;
}

static VOID
DeleteStageFile(
    _In_ PHOST_PARTITION Target,
    _In_ PCWSTR Name)
{
    WCHAR Path[MAX_PATH];

    if (HostPath(Target->DeviceName, L"\\" HOST_STAGE_DIRECTORY L"\\", Path) &&
        SUCCEEDED(StringCchCatW(Path, ARRAYSIZE(Path), Name)))
    {
        SetFileAttributesW(Path, FILE_ATTRIBUTE_NORMAL);
        DeleteFileW(Path);
    }
}

static VOID
DeleteRollback(
    _In_ PHOST_PARTITION Target)
{
    WCHAR Directory[MAX_PATH], Path[MAX_PATH];
    WIN32_FIND_DATAW Find;
    HANDLE hFind;

    if (!HostPath(Target->DeviceName, L"\\" HOST_STAGE_DIRECTORY L"\\Rollback", Directory) ||
        FAILED(StringCchPrintfW(Path, ARRAYSIZE(Path), L"%s\\*", Directory)))
    {
        return;
    }
    hFind = FindFirstFileW(Path, &Find);
    if (hFind != INVALID_HANDLE_VALUE)
    {
        do
        {
            if (!(Find.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) &&
                SUCCEEDED(StringCchPrintfW(Path, ARRAYSIZE(Path), L"%s\\%s", Directory, Find.cFileName)))
            {
                SetFileAttributesW(Path, FILE_ATTRIBUTE_NORMAL);
                DeleteFileW(Path);
            }
        } while (FindNextFileW(hFind, &Find));
        FindClose(hFind);
    }
    RemoveDirectoryW(Directory);
}

typedef struct _STORE_SEARCH
{
    ULONG DiskNumber;
    PHOST_PARTITION Partition;
    BOOL Found;
} STORE_SEARCH, *PSTORE_SEARCH;

static BOOL
FindStoreCallback(
    _In_ PCWSTR DeviceName,
    _In_ PVOID Context)
{
    PSTORE_SEARCH Search = Context;
    HOST_PARTITION Partition;

    if (!HostFileExists(DeviceName, HOST_LIBERNT_STORE) ||
        HostQueryPartition(DeviceName, &Partition) != ERROR_SUCCESS ||
        Partition.DiskNumber != Search->DiskNumber || Partition.Style != PARTITION_STYLE_GPT ||
        !IsEqualGUID(&Partition.PartitionType, &HostSystemPartitionType))
    {
        return TRUE;
    }
    *Search->Partition = Partition;
    Search->Found = TRUE;
    return FALSE;
}

DWORD
HostFindLiberNTStore(
    _In_ PHOST_PARTITION Target,
    _In_ PHOST_PARTITION WindowsSystemPartition,
    _Out_ PHOST_PARTITION StorePartition)
{
    STORE_SEARCH Search = { Target->DiskNumber, StorePartition, FALSE };

    EnumerateVolumes(FindStoreCallback, &Search);
    if (Search.Found)
        return ERROR_SUCCESS;
    if (HostFileExists(WindowsSystemPartition->DeviceName, HOST_LIBERNT_STORE))
    {
        *StorePartition = *WindowsSystemPartition;
        return ERROR_SUCCESS;
    }
    /* TODO: LiberNT installations started by FreeLoader have no boot configuration store to stage the upgrade in */
    return ERROR_NOT_FOUND;
}

DWORD
HostClearUpgradeStage(
    _In_ PHOST_PARTITION StorePartition,
    _In_ PHOST_PARTITION Target)
{
    static const PCWSTR Files[] = { UPDATE_STAGE_LOADER, HOST_STAGE_IMAGE, L"install.log", UPDATE_STAGE_REQUEST,
                                    L"update.zip", L"inventory.tmp", L"setup.sif", L"stub.inf", L"registry.tmp",
                                    L"bootmgr.tmp" };
    WCHAR Path[MAX_PATH], Previous[UPDATE_ID_CHARS];
    HKEY Store;
    ULONG Index;
    DWORD Error;

    if (!HostPath(Target->DeviceName, L"\\" HOST_STAGE_DIRECTORY L"\\" UPDATE_STAGE_REQUEST, Path))
        return ERROR_BUFFER_OVERFLOW;
    GetPrivateProfileStringW(UPDATE_STAGE_SECTION, L"PreviousDefault", L"", Previous, ARRAYSIZE(Previous), Path);

    if (!HostPath(StorePartition->DeviceName, HOST_LIBERNT_STORE, Path))
        return ERROR_BUFFER_OVERFLOW;
    Error = OpenBcdStoreFile(Path, &Store);
    if (Error != ERROR_SUCCESS)
        return Error;
    Error = RemoveBcdUpgradeEntries(Store, HOST_UPGRADE_LOADER, Previous[0] ? Previous : NULL);
    RegCloseKey(Store);
    if (Error != ERROR_SUCCESS)
        return Error;

    for (Index = 0; Index < ARRAYSIZE(Files); Index++)
        DeleteStageFile(Target, Files[Index]);
    DeleteRollback(Target);
    if (HostPath(StorePartition->DeviceName, HOST_UPDATE_STATE, Path))
        DeleteFileW(Path);
    return ERROR_SUCCESS;
}

DWORD
HostWriteUpgradeRequest(
    _In_ PHOST_PARTITION Target,
    _In_ PCWSTR Name,
    _In_ PCWSTR Value)
{
    WCHAR Path[MAX_PATH];

    if (!HostPath(Target->DeviceName, L"\\" HOST_STAGE_DIRECTORY L"\\" UPDATE_STAGE_REQUEST, Path))
        return ERROR_BUFFER_OVERFLOW;
    return WritePrivateProfileStringW(UPDATE_STAGE_SECTION, Name, Value, Path) ? ERROR_SUCCESS : GetLastError();
}

DWORD
HostAddUpgradeEntry(
    _In_ PHOST_PARTITION StorePartition,
    _In_ PHOST_PARTITION Target,
    _In_ PCWSTR ImageHash,
    _Out_writes_(UPDATE_ID_CHARS) PWSTR EntryId,
    _Out_writes_(UPDATE_ID_CHARS) PWSTR PreviousDefault)
{
    WCHAR Path[MAX_PATH], Volume[MAX_PATH];
    HKEY Store;
    DWORD Error;

    EntryId[0] = PreviousDefault[0] = UNICODE_NULL;
    if (!HostPath(StorePartition->DeviceName, HOST_LIBERNT_STORE, Path) || !HostPath(Target->DeviceName, NULL, Volume))
        return ERROR_BUFFER_OVERFLOW;
    Error = OpenBcdStoreFile(Path, &Store);
    if (Error != ERROR_SUCCESS)
        return Error;
    Error = AddBcdUpgradeEntry(Store, HOST_SETUP_DESCRIPTION, HOST_UPGRADE_LOADER, HOST_IMAGE_PATH, ImageHash,
                               Volume, EntryId, PreviousDefault);
    RegCloseKey(Store);
    return Error;
}

DWORD
HostSetCompletionCommand(
    _In_ PHOST_PARTITION Target)
{
    WCHAR Path[MAX_PATH], Mount[64];
    HKEY Run;
    BOOLEAN Enabled;
    DWORD Error;

    if (!HostPath(Target->DeviceName, L"\\Windows\\System32\\config\\SOFTWARE", Path) ||
        FAILED(StringCchPrintfW(Mount, ARRAYSIZE(Mount), L"LiberNT.Setup.%lu", GetCurrentProcessId())))
    {
        return ERROR_BUFFER_OVERFLOW;
    }
    RtlAdjustPrivilege(SE_BACKUP_PRIVILEGE, TRUE, FALSE, &Enabled);
    RtlAdjustPrivilege(SE_RESTORE_PRIVILEGE, TRUE, FALSE, &Enabled);
    Error = RegLoadKeyW(HKEY_LOCAL_MACHINE, Mount, Path);
    if (Error != ERROR_SUCCESS)
        return Error;

    if (FAILED(StringCchPrintfW(Path, ARRAYSIZE(Path), L"%s\\%s", Mount, HOST_RUN_KEY)))
        Error = ERROR_BUFFER_OVERFLOW;
    else
        Error = RegCreateKeyExW(HKEY_LOCAL_MACHINE, Path, 0, NULL, REG_OPTION_BACKUP_RESTORE,
                                KEY_SET_VALUE | KEY_QUERY_VALUE, NULL, &Run, NULL);
    if (Error == ERROR_SUCCESS)
    {
        Error = RegSetValueExW(Run, HOST_COMPLETE_VALUE, 0, REG_EXPAND_SZ, (const BYTE*)HOST_COMPLETE_COMMAND,
                               sizeof(HOST_COMPLETE_COMMAND));
        if (Error == ERROR_SUCCESS)
            Error = RegFlushKey(Run);
        RegCloseKey(Run);
    }
    if (RegUnLoadKeyW(HKEY_LOCAL_MACHINE, Mount) != ERROR_SUCCESS && Error == ERROR_SUCCESS)
        Error = ERROR_INVALID_STATE;
    HostLog(L"Upgrade completion command written to %s: %lu", Target->DeviceName, Error);
    return Error;
}
