/*
 * PROJECT:     LiberNT Setup
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Extraction of the LiberNT image from a downloaded update
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "libernt.h"
#include <zlib.h>

#include "update.h"

#define NDEBUG
#include <debug.h>

#define ZIP_END_SIGNATURE       0x06054b50
#define ZIP64_LOCATOR_SIGNATURE 0x07064b50
#define ZIP64_END_SIGNATURE     0x06064b50
#define ZIP_CENTRAL_SIGNATURE   0x02014b50
#define ZIP_LOCAL_SIGNATURE     0x04034b50
#define ZIP_END_SIZE            22
#define ZIP64_LOCATOR_SIZE      20
#define ZIP64_END_SIZE          56
#define ZIP_CENTRAL_SIZE        46
#define ZIP_LOCAL_SIZE          30
#define ZIP_COMMENT_MAX         0xFFFF
#define ZIP_DIRECTORY_MAX       (16 * 1024 * 1024)
#define ZIP_ZIP64_EXTRA         0x0001
#define ZIP_STORED              0
#define ZIP_DEFLATED            8
#define ZIP_CHUNK               (1024 * 1024)
#define ZIP_SPACE_MARGIN        (64ULL * 1024 * 1024)

#define ISO_BLOCK_SIZE          2048
#define ISO_DESCRIPTOR_BLOCK    16
#define ISO_PRIMARY             1
#define ISO_ROOT_RECORD         156
#define ISO_RECORD_EXTENT       2
#define ISO_RECORD_LENGTH       10
#define ISO_RECORD_FLAGS        25
#define ISO_RECORD_NAME_SIZE    32
#define ISO_RECORD_NAME         33
#define ISO_FLAG_DIRECTORY      0x02
#define ISO_NAME_CHARS          256
#define ISO_DIRECTORY_MAX       (4 * 1024 * 1024)

typedef struct _ZIP_ENTRY
{
    USHORT Method;
    ULONG Crc;
    ULONGLONG Compressed;
    ULONGLONG Uncompressed;
    ULONGLONG Offset;
} ZIP_ENTRY, *PZIP_ENTRY;

static USHORT
Le16(
    _In_reads_bytes_(2) const BYTE* Data)
{
    return (USHORT)(Data[0] | (Data[1] << 8));
}

static ULONG
Le32(
    _In_reads_bytes_(4) const BYTE* Data)
{
    return Data[0] | (Data[1] << 8) | (Data[2] << 16) | ((ULONG)Data[3] << 24);
}

static ULONGLONG
Le64(
    _In_reads_bytes_(8) const BYTE* Data)
{
    return Le32(Data) | ((ULONGLONG)Le32(Data + 4) << 32);
}

static BOOL
ReadAt(
    _In_ HANDLE File,
    _In_ ULONGLONG Offset,
    _Out_writes_bytes_(Size) PVOID Buffer,
    _In_ DWORD Size)
{
    LARGE_INTEGER Position;
    DWORD Read;

    Position.QuadPart = (LONGLONG)Offset;
    return SetFilePointerEx(File, Position, NULL, FILE_BEGIN) &&
           ReadFile(File, Buffer, Size, &Read, NULL) && Read == Size;
}

static BOOL
FindCentralDirectory(
    _In_ HANDLE File,
    _In_ ULONGLONG FileSize,
    _Out_ PULONGLONG DirectoryOffset,
    _Out_ PULONGLONG DirectorySize)
{
    BYTE Locator[ZIP64_LOCATOR_SIZE];
    BYTE End64[ZIP64_END_SIZE];
    PBYTE Tail;
    DWORD TailSize;
    LONG Index;
    BOOL Found = FALSE;

    TailSize = (DWORD)min(FileSize, (ULONGLONG)ZIP_COMMENT_MAX + ZIP_END_SIZE);
    if (TailSize < ZIP_END_SIZE)
        return FALSE;
    Tail = HeapAlloc(ProcessHeap, 0, TailSize);
    if (!Tail)
        return FALSE;

    if (ReadAt(File, FileSize - TailSize, Tail, TailSize))
    {
        for (Index = (LONG)(TailSize - ZIP_END_SIZE); Index >= 0 && !Found; Index--)
        {
            if (Le32(Tail + Index) != ZIP_END_SIGNATURE)
                continue;
            Found = TRUE;
            *DirectorySize = Le32(Tail + Index + 12);
            *DirectoryOffset = Le32(Tail + Index + 16);
            if ((*DirectoryOffset == MAXULONG || *DirectorySize == MAXULONG) &&
                FileSize - TailSize + Index >= ZIP64_LOCATOR_SIZE &&
                ReadAt(File, FileSize - TailSize + Index - ZIP64_LOCATOR_SIZE, Locator, sizeof(Locator)) &&
                Le32(Locator) == ZIP64_LOCATOR_SIGNATURE &&
                ReadAt(File, Le64(Locator + 8), End64, sizeof(End64)) &&
                Le32(End64) == ZIP64_END_SIGNATURE)
            {
                *DirectorySize = Le64(End64 + 40);
                *DirectoryOffset = Le64(End64 + 48);
            }
        }
    }

    HeapFree(ProcessHeap, 0, Tail);
    return Found && *DirectoryOffset < FileSize && *DirectorySize <= ZIP_DIRECTORY_MAX &&
           *DirectoryOffset + *DirectorySize <= FileSize;
}

static VOID
ReadZip64Sizes(
    _In_reads_bytes_(Size) const BYTE* Extra,
    _In_ ULONG Size,
    _Inout_ PZIP_ENTRY Entry,
    _In_ BOOL WantUncompressed,
    _In_ BOOL WantCompressed,
    _In_ BOOL WantOffset)
{
    ULONG Offset = 0, Field;

    while (Offset + 4 <= Size)
    {
        USHORT Id = Le16(Extra + Offset);
        USHORT Length = Le16(Extra + Offset + 2);

        if (Offset + 4 + Length > Size)
            return;
        if (Id == ZIP_ZIP64_EXTRA)
        {
            Field = Offset + 4;
            if (WantUncompressed && Field + 8 <= Offset + 4 + Length)
            {
                Entry->Uncompressed = Le64(Extra + Field);
                Field += 8;
            }
            if (WantCompressed && Field + 8 <= Offset + 4 + Length)
            {
                Entry->Compressed = Le64(Extra + Field);
                Field += 8;
            }
            if (WantOffset && Field + 8 <= Offset + 4 + Length)
                Entry->Offset = Le64(Extra + Field);
            return;
        }
        Offset += 4 + Length;
    }
}

static BOOL
FindImageEntry(
    _In_reads_bytes_(Size) const BYTE* Directory,
    _In_ ULONG Size,
    _Out_ PZIP_ENTRY Entry)
{
    ULONG Offset = 0, NameLength, ExtraLength, CommentLength;
    const BYTE* Record;

    while (Offset + ZIP_CENTRAL_SIZE <= Size)
    {
        Record = Directory + Offset;
        if (Le32(Record) != ZIP_CENTRAL_SIGNATURE)
            return FALSE;

        NameLength = Le16(Record + 28);
        ExtraLength = Le16(Record + 30);
        CommentLength = Le16(Record + 32);
        if (Offset + ZIP_CENTRAL_SIZE + NameLength + ExtraLength + CommentLength > Size)
            return FALSE;

        if (NameLength > 4 && _strnicmp((PCSTR)Record + ZIP_CENTRAL_SIZE + NameLength - 4, ".iso", 4) == 0)
        {
            Entry->Method = Le16(Record + 10);
            Entry->Crc = Le32(Record + 16);
            Entry->Compressed = Le32(Record + 20);
            Entry->Uncompressed = Le32(Record + 24);
            Entry->Offset = Le32(Record + 42);
            ReadZip64Sizes(Record + ZIP_CENTRAL_SIZE + NameLength, ExtraLength, Entry,
                           Entry->Uncompressed == MAXULONG, Entry->Compressed == MAXULONG, Entry->Offset == MAXULONG);
            return TRUE;
        }
        Offset += ZIP_CENTRAL_SIZE + NameLength + ExtraLength + CommentLength;
    }
    return FALSE;
}

static BOOL
HasRoomFor(
    _In_ PCWSTR OutPath,
    _In_ ULONGLONG Size)
{
    WCHAR Directory[MAX_PATH];
    ULARGE_INTEGER FreeBytes;
    PWSTR Name;

    if (FAILED(StringCchCopyW(Directory, ARRAYSIZE(Directory), OutPath)))
        return FALSE;
    Name = wcsrchr(Directory, L'\\');
    if (Name)
        Name[1] = UNICODE_NULL;
    return !GetDiskFreeSpaceExW(Directory, &FreeBytes, NULL, NULL) ||
           FreeBytes.QuadPart >= Size + ZIP_SPACE_MARGIN;
}

static DWORD
CopyEntryData(
    _In_ HANDLE Zip,
    _In_ HANDLE Out,
    _In_ PZIP_ENTRY Entry,
    _In_ ULONGLONG DataOffset,
    _In_opt_ PUPDATE_PROGRESS Progress,
    _In_opt_ PVOID Context)
{
    z_stream Stream;
    PBYTE Input, Output;
    ULONGLONG Consumed = 0, Produced = 0;
    ULONG Crc = crc32(0L, Z_NULL, 0);
    DWORD Chunk, Written, Error = ERROR_SUCCESS;
    int Result = Z_OK;

    Input = HeapAlloc(ProcessHeap, 0, 2 * ZIP_CHUNK);
    if (!Input)
        return ERROR_NOT_ENOUGH_MEMORY;
    Output = Input + ZIP_CHUNK;

    ZeroMemory(&Stream, sizeof(Stream));
    if (Entry->Method == ZIP_DEFLATED && inflateInit2(&Stream, -MAX_WBITS) != Z_OK)
    {
        HeapFree(ProcessHeap, 0, Input);
        return ERROR_INVALID_DATA;
    }

    while (Error == ERROR_SUCCESS && Consumed < Entry->Compressed && Result != Z_STREAM_END)
    {
        Chunk = (DWORD)min((ULONGLONG)ZIP_CHUNK, Entry->Compressed - Consumed);
        if (!ReadAt(Zip, DataOffset + Consumed, Input, Chunk))
        {
            Error = GetLastError() ? GetLastError() : ERROR_HANDLE_EOF;
            break;
        }
        Consumed += Chunk;

        if (Entry->Method == ZIP_STORED)
        {
            Crc = crc32(Crc, Input, Chunk);
            Produced += Chunk;
            if (!WriteFile(Out, Input, Chunk, &Written, NULL) || Written != Chunk)
                Error = GetLastError();
        }
        else
        {
            Stream.next_in = Input;
            Stream.avail_in = Chunk;
            while (Error == ERROR_SUCCESS && Stream.avail_in != 0 && Result != Z_STREAM_END)
            {
                Stream.next_out = Output;
                Stream.avail_out = ZIP_CHUNK;
                Result = inflate(&Stream, Z_NO_FLUSH);
                if (Result != Z_OK && Result != Z_STREAM_END)
                {
                    Error = ERROR_INVALID_DATA;
                    break;
                }
                Written = ZIP_CHUNK - Stream.avail_out;
                Crc = crc32(Crc, Output, Written);
                Produced += Written;
                if (Written && !WriteFile(Out, Output, Written, &Written, NULL))
                    Error = GetLastError();
            }
        }

        if (Error == ERROR_SUCCESS && Progress && !Progress(Context, Consumed, Entry->Compressed))
            Error = ERROR_CANCELLED;
    }

    if (Entry->Method == ZIP_DEFLATED)
        inflateEnd(&Stream);
    HeapFree(ProcessHeap, 0, Input);

    if (Error == ERROR_SUCCESS && (Produced != Entry->Uncompressed || Crc != Entry->Crc))
    {
        DPRINT1("Image size %I64u crc %08lx, expected %I64u crc %08lx\n",
                Produced, Crc, Entry->Uncompressed, Entry->Crc);
        Error = ERROR_INVALID_DATA;
    }
    return Error;
}

DWORD
ExtractZipImage(
    _In_ PCWSTR ZipPath,
    _In_ PCWSTR OutPath,
    _In_opt_ PUPDATE_PROGRESS Progress,
    _In_opt_ PVOID Context)
{
    BYTE Local[ZIP_LOCAL_SIZE];
    LARGE_INTEGER FileSize;
    ULONGLONG DirectoryOffset = 0, DirectorySize = 0;
    ZIP_ENTRY Entry;
    PBYTE Directory = NULL;
    HANDLE Zip, Out;
    DWORD Error = ERROR_INVALID_DATA;

    Zip = CreateFileW(ZipPath, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, NULL);
    if (Zip == INVALID_HANDLE_VALUE)
        return GetLastError();

    if (GetFileSizeEx(Zip, &FileSize) &&
        FindCentralDirectory(Zip, (ULONGLONG)FileSize.QuadPart, &DirectoryOffset, &DirectorySize) &&
        (Directory = HeapAlloc(ProcessHeap, 0, (SIZE_T)DirectorySize + 1)) != NULL &&
        ReadAt(Zip, DirectoryOffset, Directory, (DWORD)DirectorySize) &&
        FindImageEntry(Directory, (ULONG)DirectorySize, &Entry) &&
        (Entry.Method == ZIP_STORED || Entry.Method == ZIP_DEFLATED) &&
        ReadAt(Zip, Entry.Offset, Local, sizeof(Local)) &&
        Le32(Local) == ZIP_LOCAL_SIGNATURE)
    {
        Out = HasRoomFor(OutPath, Entry.Uncompressed)
            ? CreateFileW(OutPath, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL)
            : INVALID_HANDLE_VALUE;
        if (Out == INVALID_HANDLE_VALUE)
        {
            Error = HasRoomFor(OutPath, Entry.Uncompressed) ? GetLastError() : ERROR_DISK_FULL;
        }
        else
        {
            Error = CopyEntryData(Zip, Out, &Entry,
                                  Entry.Offset + ZIP_LOCAL_SIZE + Le16(Local + 26) + Le16(Local + 28),
                                  Progress, Context);
            if (Error == ERROR_SUCCESS && !FlushFileBuffers(Out))
                Error = GetLastError();
            CloseHandle(Out);
            if (Error != ERROR_SUCCESS)
                DeleteFileW(OutPath);
        }
    }

    if (Directory)
        HeapFree(ProcessHeap, 0, Directory);
    CloseHandle(Zip);
    DPRINT1("Image extracted from %S: %lu\n", ZipPath, Error);
    return Error;
}

static BOOL
GetIsoName(
    _In_reads_bytes_(ISO_RECORD_NAME + 256) const BYTE* Record,
    _Out_writes_z_(ISO_NAME_CHARS) PSTR Name)
{
    ULONG Length = Record[ISO_RECORD_NAME_SIZE];
    ULONG i;

    if (Length == 1 && Record[ISO_RECORD_NAME] <= 1)
        return FALSE;

    for (i = 0; i < Length && i < ISO_NAME_CHARS - 1 && Record[ISO_RECORD_NAME + i] != ';'; i++)
        Name[i] = (CHAR)Record[ISO_RECORD_NAME + i];
    while (i != 0 && Name[i - 1] == '.')
        i--;
    Name[i] = ANSI_NULL;
    return i != 0;
}

static BOOL
FindIsoRecord(
    _In_ HANDLE Image,
    _Inout_ PULONG Block,
    _Inout_ PULONG Length,
    _Inout_ PBOOL IsDirectory,
    _In_ PCSTR Component)
{
    CHAR Name[ISO_NAME_CHARS];
    PBYTE Directory, Record;
    ULONG Offset = 0;
    BOOL Found = FALSE;

    if (!*IsDirectory || *Length == 0 || *Length > ISO_DIRECTORY_MAX)
        return FALSE;
    Directory = HeapAlloc(ProcessHeap, 0, *Length);
    if (!Directory)
        return FALSE;

    if (!ReadAt(Image, (ULONGLONG)*Block * ISO_BLOCK_SIZE, Directory, *Length))
        Offset = *Length;

    while (!Found && Offset + ISO_RECORD_NAME < *Length)
    {
        Record = Directory + Offset;
        if (Record[0] == 0)
        {
            Offset = (Offset / ISO_BLOCK_SIZE + 1) * ISO_BLOCK_SIZE;
            continue;
        }
        if (Record[0] <= ISO_RECORD_NAME || Record[0] > *Length - Offset ||
            Record[ISO_RECORD_NAME_SIZE] > Record[0] - ISO_RECORD_NAME)
        {
            break;
        }
        Offset += Record[0];

        if (GetIsoName(Record, Name) && _stricmp(Name, Component) == 0)
        {
            *Block = Le32(Record + ISO_RECORD_EXTENT);
            *Length = Le32(Record + ISO_RECORD_LENGTH);
            *IsDirectory = (Record[ISO_RECORD_FLAGS] & ISO_FLAG_DIRECTORY) != 0;
            Found = TRUE;
        }
    }

    HeapFree(ProcessHeap, 0, Directory);
    return Found;
}

static DWORD
VerifyWrittenFile(
    _In_ PCWSTR Path,
    _In_reads_bytes_(Length) const BYTE* Data,
    _In_ ULONG Length)
{
    LARGE_INTEGER Size;
    PBYTE Copy;
    HANDLE File;
    DWORD Read = 0, Error;

    File = CreateFileW(Path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (File == INVALID_HANDLE_VALUE)
        return GetLastError();

    Copy = HeapAlloc(ProcessHeap, 0, max(Length, 1));
    if (!Copy)
        Error = ERROR_NOT_ENOUGH_MEMORY;
    else if (!GetFileSizeEx(File, &Size) || Size.QuadPart != Length ||
             !ReadFile(File, Copy, Length, &Read, NULL) || Read != Length || memcmp(Copy, Data, Length) != 0)
        Error = ERROR_CRC;
    else
        Error = ERROR_SUCCESS;

    if (Copy)
        HeapFree(ProcessHeap, 0, Copy);
    CloseHandle(File);
    return Error;
}

DWORD
ExtractImageFile(
    _In_ PCWSTR ImagePath,
    _In_ PCWSTR FileName,
    _In_ PCWSTR OutPath)
{
    BYTE Descriptor[ISO_BLOCK_SIZE];
    CHAR Component[ISO_NAME_CHARS];
    PCWSTR Path = FileName;
    ULONG Block, Length, i;
    BOOL IsDirectory = TRUE;
    PBYTE Data;
    HANDLE Image, Out;
    DWORD Written, Error = ERROR_FILE_NOT_FOUND;

    Image = CreateFileW(ImagePath, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (Image == INVALID_HANDLE_VALUE)
        return GetLastError();

    if (!ReadAt(Image, (ULONGLONG)ISO_DESCRIPTOR_BLOCK * ISO_BLOCK_SIZE, Descriptor, sizeof(Descriptor)) ||
        Descriptor[0] != ISO_PRIMARY || memcmp(Descriptor + 1, "CD001", 5) != 0)
    {
        CloseHandle(Image);
        return ERROR_INVALID_DATA;
    }

    Block = Le32(Descriptor + ISO_ROOT_RECORD + ISO_RECORD_EXTENT);
    Length = Le32(Descriptor + ISO_ROOT_RECORD + ISO_RECORD_LENGTH);
    while (*Path)
    {
        while (*Path == L'\\')
            Path++;
        for (i = 0; Path[i] && Path[i] != L'\\' && i < ISO_NAME_CHARS - 1; i++)
            Component[i] = (CHAR)Path[i];
        Component[i] = ANSI_NULL;
        Path += i;
        if (i == 0)
            break;
        if (!FindIsoRecord(Image, &Block, &Length, &IsDirectory, Component))
        {
            CloseHandle(Image);
            return ERROR_FILE_NOT_FOUND;
        }
    }

    if (!IsDirectory && Length != 0 && (Data = HeapAlloc(ProcessHeap, 0, Length)) != NULL)
    {
        if (ReadAt(Image, (ULONGLONG)Block * ISO_BLOCK_SIZE, Data, Length))
        {
            Out = CreateFileW(OutPath, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
            if (Out != INVALID_HANDLE_VALUE)
            {
                Error = (WriteFile(Out, Data, Length, &Written, NULL) && Written == Length &&
                         FlushFileBuffers(Out)) ? ERROR_SUCCESS : GetLastError();
                CloseHandle(Out);
                if (Error == ERROR_SUCCESS)
                    Error = VerifyWrittenFile(OutPath, Data, Length);
            }
            else
            {
                Error = GetLastError();
            }
        }
        HeapFree(ProcessHeap, 0, Data);
    }

    CloseHandle(Image);
    return Error;
}
