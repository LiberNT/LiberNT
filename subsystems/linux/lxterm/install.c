/*
 * PROJECT:     LiberNT Linux Subsystem
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Downloads, verifies and unpacks a Linux root file system
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <wctype.h>

#define WIN32_NO_STATUS
#include <windef.h>
#include <winbase.h>
#include <winnls.h>
#include <winhttp.h>
#include <bcrypt.h>
#define NTOS_MODE_USER
#include <ndk/iofuncs.h>
#include <ndk/obfuncs.h>
#include <ndk/rtlfuncs.h>
#undef WIN32_NO_STATUS
#include <ntstatus.h>

#include "lxinstall.h"

#define LX_DATA_CHUNK       (1024 * 1024)
#define LX_HTTP_CHUNK       (64 * 1024)
#define LX_PATH_CHARS       32768
#define LX_TAR_NAME         4096
#define LX_SUMS_MAX         (1024 * 1024)
#define LX_PROGRESS_TICKS   250
#define LX_PROBE_TAIL       (64 * 1024)
#define LX_SPACE_MARGIN     (64ULL * 1024 * 1024)

#define LX_S_IFIFO          0010000
#define LX_S_IFCHR          0020000
#define LX_S_IFDIR          0040000
#define LX_S_IFBLK          0060000
#define LX_S_IFREG          0100000
#define LX_S_IFLNK          0120000

#define LX_UBUNTU_BASE      L"https://cloud-images.ubuntu.com/minimal/releases/resolute/release/"

typedef struct _LX_TAR_ENTRY
{
    CHAR Name[LX_TAR_NAME];
    CHAR Link[LX_TAR_NAME];
    ULONG64 Size;
    ULONG64 Mtime;
    ULONG Mode;
    ULONG Uid;
    ULONG Gid;
    ULONG Major;
    ULONG Minor;
    CHAR Type;
} LX_TAR_ENTRY, *PLX_TAR_ENTRY;

typedef struct _LX_TAR_OVERRIDE
{
    CHAR Name[LX_TAR_NAME];
    CHAR Link[LX_TAR_NAME];
    ULONG64 Size;
    ULONG64 Mtime;
    ULONG Uid;
    ULONG Gid;
    BOOLEAN HasName;
    BOOLEAN HasLink;
    BOOLEAN HasSize;
    BOOLEAN HasMtime;
    BOOLEAN HasUid;
    BOOLEAN HasGid;
} LX_TAR_OVERRIDE, *PLX_TAR_OVERRIDE;

typedef struct _LX_EXTRACT
{
    HANDLE File;
    PLX_XZ Xz;
    PLX_GZ Gz;
    ULONG64 PlainPosition;
    ULONG64 ArchiveSize;
    UNICODE_STRING Root;
    PWCHAR Path;
    PFILE_LINK_INFORMATION Link;
    PUCHAR Data;
    LX_TAR_ENTRY Entry;
    LX_TAR_OVERRIDE Override;
    ULONG Entries;
    ULONG Failures;
    NTSTATUS FirstStatus;
    CHAR FirstName[MAX_PATH];
    BOOL StreamError;
    DWORD LastProgress;
} LX_EXTRACT, *PLX_EXTRACT;

typedef BOOL (*PLX_HTTP_SINK)(_In_ PVOID Context, _In_reads_bytes_(Length) const VOID *Data, _In_ ULONG Length);

typedef struct _LX_FILE_SINK
{
    HANDLE File;
    BCRYPT_HASH_HANDLE Hash;
} LX_FILE_SINK, *PLX_FILE_SINK;

typedef struct _LX_MEMORY_SINK
{
    PCHAR Buffer;
    ULONG Length;
} LX_MEMORY_SINK, *PLX_MEMORY_SINK;

static
PCWSTR
LxDefaultUrl(VOID)
{
    SYSTEM_INFO Info;

    GetNativeSystemInfo(&Info);
    switch (Info.wProcessorArchitecture)
    {
        case PROCESSOR_ARCHITECTURE_ARM64:
            return LX_UBUNTU_BASE L"ubuntu-26.04-minimal-cloudimg-arm64-root.tar.xz";
        case PROCESSOR_ARCHITECTURE_AMD64:
            return LX_UBUNTU_BASE L"ubuntu-26.04-minimal-cloudimg-amd64-root.tar.xz";
    }
    return NULL;
}

static
BOOL
LxIsUrl(
    _In_ PCWSTR Source)
{
    return (_wcsnicmp(Source, L"https://", 8) == 0) || (_wcsnicmp(Source, L"http://", 7) == 0);
}

static
VOID
LxPrintDownloadError(
    _In_ DWORD Error)
{
    PCWSTR Text = NULL;

    switch (Error)
    {
        case ERROR_WINHTTP_NAME_NOT_RESOLVED:
            Text = L"the server name cannot be resolved; check the network connection";
            break;
        case ERROR_WINHTTP_CANNOT_CONNECT:
        case ERROR_WINHTTP_CONNECTION_ERROR:
            Text = L"the connection to the server failed; check the network connection";
            break;
        case ERROR_WINHTTP_TIMEOUT:
            Text = L"the server did not answer in time";
            break;
        case ERROR_WINHTTP_SECURE_FAILURE:
            Text = L"the secure connection to the server could not be established";
            break;
        case ERROR_FILE_NOT_FOUND:
            Text = L"the server has no such file";
            break;
        case ERROR_HANDLE_EOF:
            Text = L"the connection closed before the whole file arrived";
            break;
        case ERROR_DISK_FULL:
            Text = L"there is not enough free disk space for the download";
            break;
    }
    if (Text != NULL)
        fwprintf(stderr, L"\nlxterm: the download failed: %s (error %lu)\n", Text, Error);
    else
        fwprintf(stderr, L"\nlxterm: the download failed (error %lu)\n", Error);
}

static
VOID
LxPrintSize(
    _In_ ULONG64 Bytes)
{
    if (Bytes >= 1024ULL * 1024 * 1024)
        wprintf(L"%I64u.%I64u GB", Bytes >> 30, (Bytes & ((1ULL << 30) - 1)) * 10 >> 30);
    else
        wprintf(L"%I64u.%I64u MB", Bytes >> 20, (Bytes & ((1ULL << 20) - 1)) * 10 >> 20);
}

static
DWORD
LxHttpGet(
    _In_ PCWSTR Url,
    _In_ PLX_HTTP_SINK Sink,
    _In_ PVOID Context,
    _In_ BOOL Progress,
    _In_opt_ PCWSTR Range,
    _In_ ULONG64 Limit,
    _Out_opt_ PULONG64 TotalSize)
{
    URL_COMPONENTS Parts;
    WCHAR Host[256], Path[2048], LengthText[32], RangeText[96];
    HINTERNET Session = NULL, Connect = NULL, Request = NULL;
    DWORD Error = ERROR_SUCCESS, StatusCode = 0, Size, Read, LastTick = 0;
    ULONG64 Expected = 0, Done = 0;
    PUCHAR Buffer;

    if (TotalSize != NULL)
        *TotalSize = 0;
    ZeroMemory(&Parts, sizeof(Parts));
    Parts.dwStructSize = sizeof(Parts);
    Parts.lpszHostName = Host;
    Parts.dwHostNameLength = ARRAYSIZE(Host);
    Parts.lpszUrlPath = Path;
    Parts.dwUrlPathLength = ARRAYSIZE(Path);
    if (!WinHttpCrackUrl(Url, 0, 0, &Parts))
        return GetLastError();

    Buffer = malloc(LX_HTTP_CHUNK);
    if (Buffer == NULL)
        return ERROR_NOT_ENOUGH_MEMORY;

    Session = WinHttpOpen(L"LiberNT lxterm", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME,
                          WINHTTP_NO_PROXY_BYPASS, 0);
    if (Session != NULL)
        Connect = WinHttpConnect(Session, Host, Parts.nPort, 0);
    if (Connect != NULL)
    {
        Request = WinHttpOpenRequest(Connect, L"GET", Path, NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                     (Parts.nScheme == INTERNET_SCHEME_HTTPS) ? WINHTTP_FLAG_SECURE : 0);
    }
    if ((Request == NULL) ||
        !WinHttpSendRequest(Request, (Range != NULL) ? Range : WINHTTP_NO_ADDITIONAL_HEADERS,
                            (Range != NULL) ? (DWORD)-1L : 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
        !WinHttpReceiveResponse(Request, NULL))
    {
        Error = GetLastError();
        goto Cleanup;
    }

    Size = sizeof(StatusCode);
    if (!WinHttpQueryHeaders(Request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX,
                             &StatusCode, &Size, WINHTTP_NO_HEADER_INDEX) ||
        ((StatusCode != 200) && ((Range == NULL) || (StatusCode != 206))))
    {
        if (Progress)
            fwprintf(stderr, L"lxterm: the server answered HTTP %lu for %s\n", StatusCode, Url);
        Error = ERROR_FILE_NOT_FOUND;
        goto Cleanup;
    }
    Size = sizeof(LengthText);
    if (WinHttpQueryHeaders(Request, WINHTTP_QUERY_CONTENT_LENGTH, WINHTTP_HEADER_NAME_BY_INDEX, LengthText, &Size,
                            WINHTTP_NO_HEADER_INDEX))
    {
        Expected = _wcstoui64(LengthText, NULL, 10);
    }
    if (TotalSize != NULL)
    {
        PCWSTR Slash;

        *TotalSize = Expected;
        Size = sizeof(RangeText);
        if ((StatusCode == 206) &&
            WinHttpQueryHeaders(Request, WINHTTP_QUERY_CONTENT_RANGE, WINHTTP_HEADER_NAME_BY_INDEX, RangeText, &Size,
                                WINHTTP_NO_HEADER_INDEX) &&
            ((Slash = wcschr(RangeText, L'/')) != NULL))
        {
            *TotalSize = _wcstoui64(Slash + 1, NULL, 10);
        }
    }
    if (Expected > Limit)
    {
        Error = ERROR_DISK_FULL;
        goto Cleanup;
    }

    for (;;)
    {
        if (!WinHttpReadData(Request, Buffer, LX_HTTP_CHUNK, &Read))
        {
            Error = GetLastError();
            break;
        }
        if (Read == 0)
            break;
        if (!Sink(Context, Buffer, Read))
        {
            Error = GetLastError() ? GetLastError() : ERROR_WRITE_FAULT;
            break;
        }
        Done += Read;
        if (Progress && (GetTickCount() - LastTick >= LX_PROGRESS_TICKS))
        {
            LastTick = GetTickCount();
            wprintf(L"\r  Downloading: ");
            LxPrintSize(Done);
            if (Expected != 0)
            {
                wprintf(L" of ");
                LxPrintSize(Expected);
                wprintf(L" (%I64u%%)", Done * 100 / Expected);
            }
            wprintf(L"   ");
            fflush(stdout);
        }
    }
    if ((Error == ERROR_SUCCESS) && (Expected != 0) && (Done != Expected))
        Error = ERROR_HANDLE_EOF;
    if (Progress && (Error == ERROR_SUCCESS))
    {
        wprintf(L"\r  Downloaded ");
        LxPrintSize(Done);
        wprintf(L".                              \n");
    }

Cleanup:
    if (Request != NULL)
        WinHttpCloseHandle(Request);
    if (Connect != NULL)
        WinHttpCloseHandle(Connect);
    if (Session != NULL)
        WinHttpCloseHandle(Session);
    free(Buffer);
    return Error;
}

static
BOOL
LxFileSink(
    _In_ PVOID Context,
    _In_reads_bytes_(Length) const VOID *Data,
    _In_ ULONG Length)
{
    PLX_FILE_SINK Sink = Context;
    DWORD Written;

    if (!WriteFile(Sink->File, Data, Length, &Written, NULL) || (Written != Length))
        return FALSE;
    return BCRYPT_SUCCESS(BCryptHashData(Sink->Hash, (PUCHAR)Data, Length, 0));
}

static
BOOL
LxMemorySink(
    _In_ PVOID Context,
    _In_reads_bytes_(Length) const VOID *Data,
    _In_ ULONG Length)
{
    PLX_MEMORY_SINK Sink = Context;
    PCHAR Buffer;

    if (Sink->Length + Length >= LX_SUMS_MAX)
        return FALSE;
    Buffer = realloc(Sink->Buffer, Sink->Length + Length + 1);
    if (Buffer == NULL)
        return FALSE;
    memcpy(Buffer + Sink->Length, Data, Length);
    Sink->Buffer = Buffer;
    Sink->Length += Length;
    Sink->Buffer[Sink->Length] = '\0';
    return TRUE;
}

static
BOOL
LxCreateHash(
    _Out_ BCRYPT_ALG_HANDLE *Algorithm,
    _Out_ BCRYPT_HASH_HANDLE *Hash)
{
    *Hash = NULL;
    if (!BCRYPT_SUCCESS(BCryptOpenAlgorithmProvider(Algorithm, BCRYPT_SHA256_ALGORITHM, NULL, 0)))
        return FALSE;
    if (!BCRYPT_SUCCESS(BCryptCreateHash(*Algorithm, Hash, NULL, 0, NULL, 0, 0)))
    {
        BCryptCloseAlgorithmProvider(*Algorithm, 0);
        return FALSE;
    }
    return TRUE;
}

static
DWORD
LxDownloadFile(
    _In_ PCWSTR Url,
    _In_ PCWSTR Target,
    _In_ ULONG64 Limit,
    _Out_writes_bytes_(32) PUCHAR Digest)
{
    BCRYPT_ALG_HANDLE Algorithm;
    LX_FILE_SINK Sink;
    DWORD Error;

    if (!LxCreateHash(&Algorithm, &Sink.Hash))
        return ERROR_NOT_SUPPORTED;
    Sink.File = CreateFileW(Target, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (Sink.File == INVALID_HANDLE_VALUE)
    {
        Error = GetLastError();
    }
    else
    {
        Error = LxHttpGet(Url, LxFileSink, &Sink, TRUE, NULL, Limit, NULL);
        CloseHandle(Sink.File);
    }
    if ((Error == ERROR_SUCCESS) && !BCRYPT_SUCCESS(BCryptFinishHash(Sink.Hash, Digest, 32, 0)))
        Error = ERROR_NOT_SUPPORTED;
    BCryptDestroyHash(Sink.Hash);
    BCryptCloseAlgorithmProvider(Algorithm, 0);
    return Error;
}

static
BOOL
LxHashFile(
    _In_ PCWSTR Path,
    _Out_writes_bytes_(32) PUCHAR Digest)
{
    BCRYPT_ALG_HANDLE Algorithm;
    BCRYPT_HASH_HANDLE Hash;
    HANDLE File;
    PUCHAR Buffer;
    DWORD Read;
    BOOL Result = FALSE;

    File = CreateFileW(Path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, NULL);
    if (File == INVALID_HANDLE_VALUE)
        return FALSE;
    Buffer = malloc(LX_DATA_CHUNK);
    if ((Buffer != NULL) && LxCreateHash(&Algorithm, &Hash))
    {
        while (ReadFile(File, Buffer, LX_DATA_CHUNK, &Read, NULL) && (Read != 0))
        {
            if (!BCRYPT_SUCCESS(BCryptHashData(Hash, Buffer, Read, 0)))
                break;
        }
        Result = (Read == 0) && BCRYPT_SUCCESS(BCryptFinishHash(Hash, Digest, 32, 0));
        BCryptDestroyHash(Hash);
        BCryptCloseAlgorithmProvider(Algorithm, 0);
    }
    free(Buffer);
    CloseHandle(File);
    return Result;
}

static
int
LxHexValue(
    _In_ CHAR Character)
{
    if ((Character >= '0') && (Character <= '9'))
        return Character - '0';
    if ((Character >= 'a') && (Character <= 'f'))
        return Character - 'a' + 10;
    if ((Character >= 'A') && (Character <= 'F'))
        return Character - 'A' + 10;
    return -1;
}

static
BOOL
LxFetchChecksum(
    _In_ PCWSTR Url,
    _Out_writes_bytes_(32) PUCHAR Digest)
{
    WCHAR SumsUrl[2048];
    CHAR Name[MAX_PATH];
    PCWSTR Slash = wcsrchr(Url, L'/');
    LX_MEMORY_SINK Sink = { NULL, 0 };
    PCHAR Line, Next;
    BOOL Found = FALSE;

    if ((Slash == NULL) || ((SIZE_T)(Slash - Url) + 12 >= ARRAYSIZE(SumsUrl)))
        return FALSE;
    if (!WideCharToMultiByte(CP_UTF8, 0, Slash + 1, -1, Name, sizeof(Name), NULL, NULL))
        return FALSE;
    memcpy(SumsUrl, Url, (Slash - Url + 1) * sizeof(WCHAR));
    wcscpy(SumsUrl + (Slash - Url + 1), L"SHA256SUMS");
    if ((LxHttpGet(SumsUrl, LxMemorySink, &Sink, FALSE, NULL, LX_SUMS_MAX, NULL) != ERROR_SUCCESS) ||
        (Sink.Buffer == NULL))
    {
        free(Sink.Buffer);
        return FALSE;
    }

    for (Line = Sink.Buffer; (Line != NULL) && !Found; Line = Next)
    {
        PCHAR File;
        int Index;

        Next = strchr(Line, '\n');
        if (Next != NULL)
            *Next++ = '\0';
        Line[strcspn(Line, "\r")] = '\0';
        if ((strlen(Line) < 66) || (Line[64] != ' '))
            continue;
        File = Line + 65;
        if ((*File == '*') || (*File == ' '))
            File++;
        if (strcmp(File, Name) != 0)
            continue;
        for (Index = 0; Index < 32; Index++)
        {
            int High = LxHexValue(Line[Index * 2]), Low = LxHexValue(Line[Index * 2 + 1]);

            if ((High < 0) || (Low < 0))
                break;
            Digest[Index] = (UCHAR)((High << 4) | Low);
        }
        Found = (Index == 32);
    }
    free(Sink.Buffer);
    return Found;
}

static
BOOL
LxFreeSpace(
    _In_ PCWSTR Path,
    _Out_writes_(MAX_PATH) PWCHAR Volume,
    _Out_ PULONG64 Free)
{
    ULARGE_INTEGER Available;

    *Free = 0;
    if (!GetVolumePathNameW(Path, Volume, MAX_PATH))
        return FALSE;
    if (!GetDiskFreeSpaceExW(Volume, &Available, NULL, NULL))
        return FALSE;
    *Free = Available.QuadPart;
    return TRUE;
}

static
BOOL
LxReadSource(
    _In_ PCWSTR Source,
    _In_ BOOL Tail,
    _Out_ PLX_MEMORY_SINK Data,
    _Out_ PULONG64 TotalSize)
{
    LARGE_INTEGER Size, Offset;
    HANDLE File;
    DWORD Read;
    ULONG Length;

    Data->Buffer = NULL;
    Data->Length = 0;
    *TotalSize = 0;
    if (LxIsUrl(Source))
    {
        if (LxHttpGet(Source, LxMemorySink, Data, FALSE, Tail ? L"Range: bytes=-65536" : L"Range: bytes=0-15",
                      LX_SUMS_MAX, TotalSize) == ERROR_SUCCESS)
        {
            return (Data->Buffer != NULL) && (*TotalSize != 0);
        }
        free(Data->Buffer);
        Data->Buffer = NULL;
        return FALSE;
    }

    File = CreateFileW(Source, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (File == INVALID_HANDLE_VALUE)
        return FALSE;
    if (GetFileSizeEx(File, &Size))
    {
        *TotalSize = Size.QuadPart;
        Length = (ULONG)min(*TotalSize, (ULONG64)(Tail ? LX_PROBE_TAIL : 16));
        Offset.QuadPart = Tail ? (LONGLONG)(*TotalSize - Length) : 0;
        Data->Buffer = malloc(Length + 1);
        if ((Data->Buffer != NULL) && SetFilePointerEx(File, Offset, NULL, FILE_BEGIN) &&
            ReadFile(File, Data->Buffer, Length, &Read, NULL) && (Read == Length))
        {
            Data->Length = Length;
        }
    }
    CloseHandle(File);
    if (Data->Length == 0)
    {
        free(Data->Buffer);
        Data->Buffer = NULL;
        return FALSE;
    }
    return TRUE;
}

static
BOOL
LxArchiveSizes(
    _In_ PCWSTR Source,
    _Out_ PULONG64 ArchiveSize,
    _Out_ PULONG64 ContentSize)
{
    static const UCHAR XzMagic[6] = { 0xFD, '7', 'z', 'X', 'Z', 0x00 };
    LX_MEMORY_SINK Head, Tail;
    ULONG64 TailTotal;

    *ArchiveSize = *ContentSize = 0;
    if (!LxReadSource(Source, FALSE, &Head, ArchiveSize))
        return FALSE;
    if (!LxReadSource(Source, TRUE, &Tail, &TailTotal) || (TailTotal != *ArchiveSize))
    {
        free(Head.Buffer);
        free(Tail.Buffer);
        return FALSE;
    }
    if ((Head.Length >= sizeof(XzMagic)) && (memcmp(Head.Buffer, XzMagic, sizeof(XzMagic)) == 0))
    {
        *ContentSize = LxXzContentSize((PUCHAR)Tail.Buffer, Tail.Length, *ArchiveSize);
    }
    else if ((Head.Length >= 2) && ((UCHAR)Head.Buffer[0] == 0x1F) && ((UCHAR)Head.Buffer[1] == 0x8B))
    {
        PUCHAR End = (PUCHAR)Tail.Buffer + Tail.Length - 4;

        if (Tail.Length >= 4)
            *ContentSize = (ULONG)End[0] | ((ULONG)End[1] << 8) | ((ULONG)End[2] << 16) | ((ULONG)End[3] << 24);
        if (*ContentSize < *ArchiveSize)
            *ContentSize = 0;
    }
    else
    {
        *ContentSize = *ArchiveSize;
    }
    free(Head.Buffer);
    free(Tail.Buffer);
    return *ContentSize != 0;
}

static
ULONG64
LxInstalledSize(
    _In_ ULONG64 ContentSize)
{
    return ContentSize + ContentSize / 10 + LX_SPACE_MARGIN;
}

static
VOID
LxPrintNoSpace(
    _In_ PCWSTR Volume,
    _In_ ULONG64 Needed,
    _In_ ULONG64 Free)
{
    wprintf(L"Not enough disk space on %s: the installation needs about ", Volume);
    LxPrintSize(Needed);
    wprintf(L", and ");
    LxPrintSize(Free);
    wprintf(L" is free.\nFree up ");
    LxPrintSize(Needed - Free);
    wprintf(L" or choose a directory on another drive with -r.\n");
}

static
BOOL
LxBuildPath(
    _Out_writes_(LX_PATH_CHARS) PWCHAR Buffer,
    _In_ PCUNICODE_STRING Root,
    _In_ PCSTR Name,
    _Out_ PUNICODE_STRING Path)
{
    ULONG Offset = Root->Length / sizeof(WCHAR), Index;
    int Count;

    memcpy(Buffer, Root->Buffer, Root->Length);
    if (Name[0] != '\0')
    {
        Buffer[Offset++] = L'\\';
        Count = MultiByteToWideChar(CP_UTF8, 0, Name, -1, Buffer + Offset, LX_PATH_CHARS - Offset);
        if (Count <= 1)
            return FALSE;
        for (Index = Offset; Index < Offset + Count - 1; Index++)
        {
            WCHAR Character = Buffer[Index];

            if (Character == L'/')
                Buffer[Index] = L'\\';
            else if ((Character < 0x20) || wcschr(L":*?\"<>|\\", Character))
                Buffer[Index] = 0xF000 + Character;
        }
        Offset += Count - 1;
    }
    if (Offset * sizeof(WCHAR) > MAXUSHORT)
        return FALSE;
    Path->Buffer = Buffer;
    Path->Length = Path->MaximumLength = (USHORT)(Offset * sizeof(WCHAR));
    return TRUE;
}

static
NTSTATUS
LxOpenEntry(
    _In_ PLX_EXTRACT Extract,
    _In_ PCSTR Name,
    _In_ ACCESS_MASK Access,
    _In_ ULONG Disposition,
    _In_ ULONG Options,
    _In_ ULONG Attributes,
    _Out_ PHANDLE Handle,
    _Out_opt_ PULONG_PTR Information)
{
    UNICODE_STRING Path;
    OBJECT_ATTRIBUTES ObjectAttributes;
    IO_STATUS_BLOCK Iosb;
    NTSTATUS Status;

    *Handle = NULL;
    if (!LxBuildPath(Extract->Path, &Extract->Root, Name, &Path))
        return STATUS_OBJECT_NAME_INVALID;
    InitializeObjectAttributes(&ObjectAttributes, &Path, OBJ_CASE_INSENSITIVE, NULL, NULL);
    Iosb.Information = 0;
    Status = NtCreateFile(Handle, Access | SYNCHRONIZE, &ObjectAttributes, &Iosb, NULL, Attributes,
                          FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, Disposition,
                          Options | FILE_SYNCHRONOUS_IO_NONALERT | FILE_OPEN_REPARSE_POINT, NULL, 0);
    if (Information != NULL)
        *Information = Iosb.Information;
    return Status;
}

static
NTSTATUS
LxSetAttributes(
    _In_ HANDLE Handle,
    _In_ ULONG Mode,
    _In_ ULONG Uid,
    _In_ ULONG Gid,
    _In_ ULONG Major,
    _In_ ULONG Minor)
{
    static const PCSTR Names[4] = { "$LXUID", "$LXGID", "$LXMOD", "$LXDEV" };
    ULONG Values[5] = { Uid, Gid, Mode, Major, Minor };
    ULONG Type = Mode & 0170000, Count = ((Type == LX_S_IFCHR) || (Type == LX_S_IFBLK)) ? 4 : 3;
    ULONG Length = 0, Index;
    PFILE_FULL_EA_INFORMATION Previous = NULL;
    IO_STATUS_BLOCK Iosb;
    UCHAR Buffer[160];

    ZeroMemory(Buffer, sizeof(Buffer));
    for (Index = 0; Index < Count; Index++)
    {
        PFILE_FULL_EA_INFORMATION Entry = (PFILE_FULL_EA_INFORMATION)(Buffer + Length);
        ULONG NameLength = (ULONG)strlen(Names[Index]);
        ULONG ValueLength = (Index == 3) ? 8 : 4;

        Entry->EaNameLength = (UCHAR)NameLength;
        Entry->EaValueLength = (USHORT)ValueLength;
        memcpy(Entry->EaName, Names[Index], NameLength);
        memcpy(Entry->EaName + NameLength + 1, &Values[Index], ValueLength);
        if (Previous != NULL)
            Previous->NextEntryOffset = (ULONG)((PUCHAR)Entry - (PUCHAR)Previous);
        Previous = Entry;
        Length += (FIELD_OFFSET(FILE_FULL_EA_INFORMATION, EaName) + NameLength + 1 + ValueLength + 3) & ~3UL;
    }
    return NtSetEaFile(Handle, &Iosb, Buffer, Length);
}

static
VOID
LxSetTimes(
    _In_ HANDLE Handle,
    _In_ ULONG64 Mtime,
    _In_ ULONG Attributes)
{
    FILE_BASIC_INFORMATION Basic;
    IO_STATUS_BLOCK Iosb;

    ZeroMemory(&Basic, sizeof(Basic));
    Basic.LastAccessTime.QuadPart = (LONGLONG)Mtime * 10000000 + 116444736000000000LL;
    Basic.LastWriteTime = Basic.LastAccessTime;
    Basic.ChangeTime = Basic.LastAccessTime;
    Basic.FileAttributes = Attributes;
    NtSetInformationFile(Handle, &Iosb, &Basic, sizeof(Basic), FileBasicInformation);
}

static
VOID
LxCreateParents(
    _In_ PLX_EXTRACT Extract,
    _In_ PCSTR Name)
{
    CHAR Prefix[LX_TAR_NAME];
    PCSTR Slash = Name;

    while ((Slash = strchr(Slash, '/')) != NULL)
    {
        ULONG_PTR Information;
        HANDLE Handle;

        memcpy(Prefix, Name, Slash - Name);
        Prefix[Slash - Name] = '\0';
        if (NT_SUCCESS(LxOpenEntry(Extract, Prefix, FILE_LIST_DIRECTORY | FILE_WRITE_EA, FILE_OPEN_IF,
                                   FILE_DIRECTORY_FILE, FILE_ATTRIBUTE_NORMAL, &Handle, &Information)))
        {
            if (Information == FILE_CREATED)
                LxSetAttributes(Handle, LX_S_IFDIR | 0755, 0, 0, 0, 0);
            NtClose(Handle);
        }
        Slash++;
    }
}

static
NTSTATUS
LxCreateEntry(
    _In_ PLX_EXTRACT Extract,
    _In_ PCSTR Name,
    _In_ ACCESS_MASK Access,
    _In_ ULONG Disposition,
    _In_ ULONG Options,
    _In_ ULONG Attributes,
    _Out_ PHANDLE Handle)
{
    NTSTATUS Status;

    Status = LxOpenEntry(Extract, Name, Access, Disposition, Options, Attributes, Handle, NULL);
    if (Status == STATUS_OBJECT_PATH_NOT_FOUND)
    {
        LxCreateParents(Extract, Name);
        Status = LxOpenEntry(Extract, Name, Access, Disposition, Options, Attributes, Handle, NULL);
    }
    return Status;
}

static
LONG
LxSourceRead(
    _In_ PLX_EXTRACT Extract,
    _Out_writes_bytes_(Size) PVOID Buffer,
    _In_ ULONG Size)
{
    ULONG Done = 0;
    DWORD Read;

    if (Extract->Xz != NULL)
        return LxXzRead(Extract->Xz, Buffer, Size);
    if (Extract->Gz != NULL)
        return LxGzRead(Extract->Gz, Buffer, Size);
    while (Done < Size)
    {
        if (!ReadFile(Extract->File, (PUCHAR)Buffer + Done, Size - Done, &Read, NULL))
            return -1;
        if (Read == 0)
            break;
        Done += Read;
    }
    Extract->PlainPosition += Done;
    return (LONG)Done;
}

static
ULONG64
LxSourcePosition(
    _In_ PLX_EXTRACT Extract)
{
    if (Extract->Xz != NULL)
        return LxXzPosition(Extract->Xz);
    if (Extract->Gz != NULL)
        return LxGzPosition(Extract->Gz);
    return Extract->PlainPosition;
}

static
BOOL
LxTarRead(
    _In_ PLX_EXTRACT Extract,
    _Out_writes_bytes_(Size) PVOID Buffer,
    _In_ ULONG Size)
{
    if (LxSourceRead(Extract, Buffer, Size) == (LONG)Size)
        return TRUE;
    Extract->StreamError = TRUE;
    return FALSE;
}

static
BOOL
LxTarSkip(
    _In_ PLX_EXTRACT Extract,
    _In_ ULONG64 Size)
{
    while (Size != 0)
    {
        ULONG Count = (ULONG)min(Size, (ULONG64)LX_DATA_CHUNK);

        if (!LxTarRead(Extract, Extract->Data, Count))
            return FALSE;
        Size -= Count;
    }
    return TRUE;
}

static
ULONG64
LxTarPadding(
    _In_ ULONG64 Size)
{
    return (512 - (Size & 511)) & 511;
}

static
ULONG64
LxTarNumber(
    _In_reads_(Length) const UCHAR *Field,
    _In_ ULONG Length)
{
    ULONG64 Value = 0;
    ULONG Index = 0;

    if (Field[0] & 0x80)
    {
        Value = Field[0] & 0x7F;
        for (Index = 1; Index < Length; Index++)
            Value = (Value << 8) | Field[Index];
        return Value;
    }
    while ((Index < Length) && (Field[Index] == ' '))
        Index++;
    for (; (Index < Length) && (Field[Index] >= '0') && (Field[Index] <= '7'); Index++)
        Value = Value * 8 + (Field[Index] - '0');
    return Value;
}

static
VOID
LxTarField(
    _Out_writes_z_(TargetSize) PCHAR Target,
    _In_ SIZE_T TargetSize,
    _In_reads_(Length) const UCHAR *Field,
    _In_ SIZE_T Length)
{
    SIZE_T Count = 0;

    while ((Count < Length) && (Count + 1 < TargetSize) && (Field[Count] != '\0'))
    {
        Target[Count] = (CHAR)Field[Count];
        Count++;
    }
    Target[Count] = '\0';
}

static
BOOL
LxTarChecksum(
    _In_reads_(512) const UCHAR *Header)
{
    ULONG Sum = 0, Index;

    for (Index = 0; Index < 512; Index++)
        Sum += ((Index >= 148) && (Index < 156)) ? ' ' : Header[Index];
    return Sum == (ULONG)LxTarNumber(Header + 148, 8);
}

static
BOOL
LxTarNormalize(
    _Inout_z_ PCHAR Name)
{
    PCHAR Source = Name, Component;
    SIZE_T Length;

    for (;;)
    {
        if ((Source[0] == '.') && (Source[1] == '/'))
            Source += 2;
        else if (Source[0] == '/')
            Source++;
        else
            break;
    }
    memmove(Name, Source, strlen(Source) + 1);
    Length = strlen(Name);
    while ((Length != 0) && (Name[Length - 1] == '/'))
        Name[--Length] = '\0';
    if (strcmp(Name, ".") == 0)
        Name[0] = '\0';

    for (Component = Name; *Component != '\0';)
    {
        SIZE_T Size = strcspn(Component, "/");

        if ((Size == 0) || ((Size == 1) && (Component[0] == '.')) ||
            ((Size == 2) && (Component[0] == '.') && (Component[1] == '.')))
        {
            return FALSE;
        }
        Component += Size;
        if (*Component == '/')
            Component++;
    }
    return TRUE;
}

static
VOID
LxTarParsePax(
    _In_ PLX_EXTRACT Extract,
    _Inout_updates_(Length) PCHAR Data,
    _In_ ULONG Length)
{
    PLX_TAR_OVERRIDE Override = &Extract->Override;
    ULONG Position = 0;

    while (Position < Length)
    {
        ULONG RecordLength = 0, Start = Position;
        PCHAR Key, Value;

        while ((Position < Length) && (Data[Position] >= '0') && (Data[Position] <= '9'))
            RecordLength = RecordLength * 10 + (Data[Position++] - '0');
        if ((RecordLength == 0) || (Start + RecordLength > Length) || (Position >= Length) ||
            (Data[Position] != ' ') || (Data[Start + RecordLength - 1] != '\n'))
        {
            return;
        }
        Data[Start + RecordLength - 1] = '\0';
        Key = Data + Position + 1;
        Value = strchr(Key, '=');
        Position = Start + RecordLength;
        if (Value == NULL)
            continue;
        *Value++ = '\0';

        if ((strcmp(Key, "path") == 0) && (strlen(Value) < LX_TAR_NAME))
        {
            strcpy(Override->Name, Value);
            Override->HasName = TRUE;
        }
        else if ((strcmp(Key, "linkpath") == 0) && (strlen(Value) < LX_TAR_NAME))
        {
            strcpy(Override->Link, Value);
            Override->HasLink = TRUE;
        }
        else if (strcmp(Key, "size") == 0)
        {
            Override->Size = _strtoui64(Value, NULL, 10);
            Override->HasSize = TRUE;
        }
        else if (strcmp(Key, "mtime") == 0)
        {
            Override->Mtime = _strtoui64(Value, NULL, 10);
            Override->HasMtime = TRUE;
        }
        else if (strcmp(Key, "uid") == 0)
        {
            Override->Uid = strtoul(Value, NULL, 10);
            Override->HasUid = TRUE;
        }
        else if (strcmp(Key, "gid") == 0)
        {
            Override->Gid = strtoul(Value, NULL, 10);
            Override->HasGid = TRUE;
        }
    }
}

static
VOID
LxTarFailure(
    _In_ PLX_EXTRACT Extract,
    _In_ PCSTR Name,
    _In_ NTSTATUS Status)
{
    if (Extract->Failures++ == 0)
    {
        Extract->FirstStatus = Status;
        strncpy(Extract->FirstName, Name, sizeof(Extract->FirstName) - 1);
    }
}

static
NTSTATUS
LxWriteRegular(
    _In_ PLX_EXTRACT Extract,
    _In_ PLX_TAR_ENTRY Entry)
{
    ULONG64 Left = Entry->Size;
    IO_STATUS_BLOCK Iosb;
    NTSTATUS Status;
    HANDLE Handle;

    Status = LxCreateEntry(Extract, Entry->Name, FILE_WRITE_DATA | FILE_WRITE_EA | FILE_WRITE_ATTRIBUTES, FILE_OVERWRITE_IF,
                           FILE_NON_DIRECTORY_FILE | FILE_SEQUENTIAL_ONLY, FILE_ATTRIBUTE_NORMAL, &Handle);
    while (Left != 0)
    {
        ULONG Count = (ULONG)min(Left, (ULONG64)LX_DATA_CHUNK);

        if (!LxTarRead(Extract, Extract->Data, Count))
            break;
        if (NT_SUCCESS(Status))
            Status = NtWriteFile(Handle, NULL, NULL, NULL, &Iosb, Extract->Data, Count, NULL, NULL);
        Left -= Count;
    }
    if (Handle != NULL)
    {
        if (NT_SUCCESS(Status))
            Status = LxSetAttributes(Handle, LX_S_IFREG | (Entry->Mode & 07777), Entry->Uid, Entry->Gid, 0, 0);
        if (NT_SUCCESS(Status))
            LxSetTimes(Handle, Entry->Mtime, 0);
        NtClose(Handle);
    }
    return Status;
}

static
NTSTATUS
LxWriteDirectory(
    _In_ PLX_EXTRACT Extract,
    _In_ PLX_TAR_ENTRY Entry)
{
    NTSTATUS Status;
    HANDLE Handle;

    Status = LxCreateEntry(Extract, Entry->Name, FILE_LIST_DIRECTORY | FILE_WRITE_EA | FILE_WRITE_ATTRIBUTES, FILE_OPEN_IF,
                           FILE_DIRECTORY_FILE, FILE_ATTRIBUTE_NORMAL, &Handle);
    if (!NT_SUCCESS(Status))
        return Status;
    Status = LxSetAttributes(Handle, LX_S_IFDIR | (Entry->Mode & 07777), Entry->Uid, Entry->Gid, 0, 0);
    NtClose(Handle);
    return Status;
}

static
NTSTATUS
LxWriteNode(
    _In_ PLX_EXTRACT Extract,
    _In_ PLX_TAR_ENTRY Entry,
    _In_ ULONG Type)
{
    IO_STATUS_BLOCK Iosb;
    NTSTATUS Status;
    HANDLE Handle;

    Status = LxCreateEntry(Extract, Entry->Name, FILE_WRITE_DATA | FILE_WRITE_EA | FILE_WRITE_ATTRIBUTES, FILE_OVERWRITE_IF,
                           FILE_NON_DIRECTORY_FILE, FILE_ATTRIBUTE_SYSTEM, &Handle);
    if (!NT_SUCCESS(Status))
        return Status;
    if ((Type == LX_S_IFLNK) && (Entry->Link[0] != '\0'))
        Status = NtWriteFile(Handle, NULL, NULL, NULL, &Iosb, Entry->Link, (ULONG)strlen(Entry->Link), NULL, NULL);
    if (NT_SUCCESS(Status))
    {
        Status = LxSetAttributes(Handle, Type | (Entry->Mode & 07777), Entry->Uid, Entry->Gid, Entry->Major,
                                 Entry->Minor);
    }
    if (NT_SUCCESS(Status))
        LxSetTimes(Handle, Entry->Mtime, FILE_ATTRIBUTE_SYSTEM);
    NtClose(Handle);
    return Status;
}

static
NTSTATUS
LxWriteHardLink(
    _In_ PLX_EXTRACT Extract,
    _In_ PLX_TAR_ENTRY Entry)
{
    UNICODE_STRING Path;
    IO_STATUS_BLOCK Iosb;
    NTSTATUS Status;
    HANDLE Handle;

    Status = LxOpenEntry(Extract, Entry->Link, FILE_WRITE_ATTRIBUTES, FILE_OPEN, FILE_NON_DIRECTORY_FILE, 0, &Handle, NULL);
    if (!NT_SUCCESS(Status))
        return Status;
    LxCreateParents(Extract, Entry->Name);
    if (!LxBuildPath(Extract->Link->FileName, &Extract->Root, Entry->Name, &Path))
    {
        NtClose(Handle);
        return STATUS_OBJECT_NAME_INVALID;
    }
    Extract->Link->ReplaceIfExists = TRUE;
    Extract->Link->RootDirectory = NULL;
    Extract->Link->FileNameLength = Path.Length;
    Status = NtSetInformationFile(Handle, &Iosb, Extract->Link,
                                  FIELD_OFFSET(FILE_LINK_INFORMATION, FileName) + Path.Length, FileLinkInformation);
    NtClose(Handle);
    return Status;
}

static
VOID
LxExtractProgress(
    _In_ PLX_EXTRACT Extract,
    _In_ BOOL Final)
{
    if (!Final && (GetTickCount() - Extract->LastProgress < LX_PROGRESS_TICKS))
        return;
    Extract->LastProgress = GetTickCount();
    if (Final)
    {
        wprintf(L"\r  Unpacked %lu files.                              \n", Extract->Entries);
    }
    else
    {
        wprintf(L"\r  Unpacking: %lu files (%I64u%%)   ", Extract->Entries,
                Extract->ArchiveSize ? (LxSourcePosition(Extract) * 100 / Extract->ArchiveSize) : 0);
    }
    fflush(stdout);
}

static
BOOL
LxExtract(
    _In_ PLX_EXTRACT Extract)
{
    PLX_TAR_ENTRY Entry = &Extract->Entry;
    PLX_TAR_OVERRIDE Override = &Extract->Override;
    UCHAR Header[512];
    ULONG Zero = 0, Index;

    for (;;)
    {
        LONG Read = LxSourceRead(Extract, Header, sizeof(Header));
        NTSTATUS Status = STATUS_SUCCESS;
        ULONG64 Size;
        CHAR Type;

        if (Read == 0)
            return TRUE;
        if (Read != sizeof(Header))
            return FALSE;
        for (Index = 0; (Index < sizeof(Header)) && (Header[Index] == 0); Index++);
        if (Index == sizeof(Header))
        {
            if (++Zero == 2)
                return TRUE;
            continue;
        }
        Zero = 0;
        if (!LxTarChecksum(Header))
            return FALSE;

        Type = (CHAR)Header[156];
        Size = LxTarNumber(Header + 124, 12);
        if ((Type == 'x') || (Type == 'g') || (Type == 'L') || (Type == 'K'))
        {
            if (Size + LxTarPadding(Size) > LX_DATA_CHUNK)
            {
                if (!LxTarSkip(Extract, Size + LxTarPadding(Size)))
                    return FALSE;
                continue;
            }
            if (!LxTarRead(Extract, Extract->Data, (ULONG)(Size + LxTarPadding(Size))))
                return FALSE;
            Extract->Data[Size] = '\0';
            if (Type == 'x')
            {
                LxTarParsePax(Extract, (PCHAR)Extract->Data, (ULONG)Size);
            }
            else if (Type == 'L')
            {
                LxTarField(Override->Name, LX_TAR_NAME, Extract->Data, (SIZE_T)Size);
                Override->HasName = TRUE;
            }
            else if (Type == 'K')
            {
                LxTarField(Override->Link, LX_TAR_NAME, Extract->Data, (SIZE_T)Size);
                Override->HasLink = TRUE;
            }
            continue;
        }

        Entry->Type = Type;
        if (Override->HasName)
        {
            strcpy(Entry->Name, Override->Name);
        }
        else if ((memcmp(Header + 257, "ustar", 6) == 0) && (Header[345] != '\0'))
        {
            LxTarField(Entry->Name, LX_TAR_NAME - 101, Header + 345, 155);
            strcat(Entry->Name, "/");
            LxTarField(Entry->Name + strlen(Entry->Name), 101, Header, 100);
        }
        else
        {
            LxTarField(Entry->Name, LX_TAR_NAME, Header, 100);
        }
        if (Override->HasLink)
            strcpy(Entry->Link, Override->Link);
        else
            LxTarField(Entry->Link, LX_TAR_NAME, Header + 157, 100);
        Entry->Mode = (ULONG)LxTarNumber(Header + 100, 8);
        Entry->Uid = Override->HasUid ? Override->Uid : (ULONG)LxTarNumber(Header + 108, 8);
        Entry->Gid = Override->HasGid ? Override->Gid : (ULONG)LxTarNumber(Header + 116, 8);
        Entry->Size = Override->HasSize ? Override->Size : Size;
        Entry->Mtime = Override->HasMtime ? Override->Mtime : LxTarNumber(Header + 136, 12);
        Entry->Major = (ULONG)LxTarNumber(Header + 329, 8);
        Entry->Minor = (ULONG)LxTarNumber(Header + 337, 8);
        Override->HasName = Override->HasLink = Override->HasSize = FALSE;
        Override->HasMtime = Override->HasUid = Override->HasGid = FALSE;

        if (!LxTarNormalize(Entry->Name) || ((Type == '1') && !LxTarNormalize(Entry->Link)))
        {
            LxTarFailure(Extract, Entry->Name, STATUS_OBJECT_NAME_INVALID);
            if (!LxTarSkip(Extract, Entry->Size + LxTarPadding(Entry->Size)))
                return FALSE;
            continue;
        }

        switch (Type)
        {
            case '0':
            case '\0':
            case '7':
                Status = LxWriteRegular(Extract, Entry);
                if (Extract->StreamError || !LxTarSkip(Extract, LxTarPadding(Entry->Size)))
                    return FALSE;
                break;

            default:
                if (Type == '1')
                    Status = LxWriteHardLink(Extract, Entry);
                else if (Type == '2')
                    Status = LxWriteNode(Extract, Entry, LX_S_IFLNK);
                else if (Type == '3')
                    Status = LxWriteNode(Extract, Entry, LX_S_IFCHR);
                else if (Type == '4')
                    Status = LxWriteNode(Extract, Entry, LX_S_IFBLK);
                else if (Type == '5')
                    Status = LxWriteDirectory(Extract, Entry);
                else if (Type == '6')
                    Status = LxWriteNode(Extract, Entry, LX_S_IFIFO);
                if (!LxTarSkip(Extract, Entry->Size + LxTarPadding(Entry->Size)))
                    return FALSE;
                break;
        }

        if (!NT_SUCCESS(Status))
        {
            LxTarFailure(Extract, Entry->Name, Status);
            if (Status == STATUS_DISK_FULL)
                return FALSE;
        }
        Extract->Entries++;
        LxExtractProgress(Extract, FALSE);
    }
}

static
NTSTATUS
LxWriteFileData(
    _In_ PLX_EXTRACT Extract,
    _In_ PCSTR Name,
    _In_reads_bytes_(Length) const VOID *Data,
    _In_ ULONG Length,
    _In_ ULONG Mode)
{
    IO_STATUS_BLOCK Iosb;
    NTSTATUS Status;
    HANDLE Handle;

    Status = LxCreateEntry(Extract, Name, FILE_WRITE_DATA | FILE_WRITE_EA | FILE_WRITE_ATTRIBUTES, FILE_OVERWRITE_IF,
                           FILE_NON_DIRECTORY_FILE, FILE_ATTRIBUTE_NORMAL, &Handle);
    if (!NT_SUCCESS(Status))
        return Status;
    if (Length != 0)
        Status = NtWriteFile(Handle, NULL, NULL, NULL, &Iosb, (PVOID)Data, Length, NULL, NULL);
    if (NT_SUCCESS(Status))
        Status = LxSetAttributes(Handle, LX_S_IFREG | Mode, 0, 0, 0, 0);
    NtClose(Handle);
    return Status;
}

static
NTSTATUS
LxCopyGuiFile(
    _In_ PLX_EXTRACT Extract,
    _In_ PCWSTR Directory,
    _In_ PCWSTR Source,
    _In_ PCSTR Target,
    _In_ ULONG Mode,
    _In_ BOOL Text)
{
    WCHAR Path[MAX_PATH];
    PCHAR Data;
    DWORD Size, Read, Index, Length = 0;
    NTSTATUS Status;
    HANDLE File;

    _snwprintf(Path, ARRAYSIZE(Path), L"%s\\%s", Directory, Source);
    Path[ARRAYSIZE(Path) - 1] = L'\0';
    File = CreateFileW(Path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (File == INVALID_HANDLE_VALUE)
        return STATUS_OBJECT_NAME_NOT_FOUND;
    Size = GetFileSize(File, NULL);
    Data = ((Size != INVALID_FILE_SIZE) && (Size <= LX_DATA_CHUNK)) ? malloc(Size + 1) : NULL;
    if ((Data == NULL) || !ReadFile(File, Data, Size, &Read, NULL))
    {
        free(Data);
        CloseHandle(File);
        return STATUS_UNSUCCESSFUL;
    }
    CloseHandle(File);
    for (Index = 0; Index < Read; Index++)
    {
        if (!Text || (Data[Index] != '\r'))
            Data[Length++] = Data[Index];
    }
    Status = LxWriteFileData(Extract, Target, Data, Length, Mode);
    free(Data);
    return Status;
}

static
BOOL
LxProvisionGui(
    _In_ PLX_EXTRACT Extract)
{
    static const struct
    {
        PCWSTR Source;
        PCSTR Target;
        ULONG Mode;
        BOOL Text;
    } Files[] =
    {
        { L"lxwayland", "usr/local/lib/libernt/lxwayland/lxwayland", 0755, FALSE },
        { L"lxwayland.service", "etc/systemd/system/lxwayland.service", 0644, TRUE },
        { L"lx-gui-setup.service", "etc/systemd/system/lx-gui-setup.service", 0644, TRUE },
    };
    static const PCSTR Units[] = { "lxwayland.service", "lx-gui-setup.service" };
    PLX_TAR_ENTRY Entry = &Extract->Entry;
    WCHAR Directory[MAX_PATH];
    ULONG Index;

    if (!GetSystemDirectoryW(Directory, MAX_PATH - 16))
        return FALSE;
    wcscat(Directory, L"\\lxwayland");
    for (Index = 0; Index < ARRAYSIZE(Files); Index++)
    {
        if (!NT_SUCCESS(LxCopyGuiFile(Extract, Directory, Files[Index].Source, Files[Index].Target, Files[Index].Mode,
                                       Files[Index].Text)))
            return FALSE;
    }
    for (Index = 0; Index < ARRAYSIZE(Units); Index++)
    {
        ZeroMemory(Entry, sizeof(*Entry));
        _snprintf(Entry->Name, LX_TAR_NAME, "etc/systemd/system/multi-user.target.wants/%s", Units[Index]);
        _snprintf(Entry->Link, LX_TAR_NAME, "/etc/systemd/system/%s", Units[Index]);
        Entry->Mode = 0777;
        if (!NT_SUCCESS(LxWriteNode(Extract, Entry, LX_S_IFLNK)))
            return FALSE;
    }
    return TRUE;
}

static
int
LxUnpack(
    _In_ PCWSTR Archive,
    _In_ PCWSTR Staging)
{
    static const UCHAR XzMagic[6] = { 0xFD, '7', 'z', 'X', 'Z', 0x00 };
    LX_EXTRACT *Extract;
    LARGE_INTEGER Size;
    UCHAR Magic[6];
    DWORD Read = 0;
    HANDLE File;
    int Result = 1;

    Extract = calloc(1, sizeof(*Extract));
    if (Extract == NULL)
        return 1;
    Extract->Path = malloc(LX_PATH_CHARS * sizeof(WCHAR));
    Extract->Link = malloc(FIELD_OFFSET(FILE_LINK_INFORMATION, FileName) + LX_PATH_CHARS * sizeof(WCHAR));
    Extract->Data = malloc(LX_DATA_CHUNK + 1);
    File = CreateFileW(Archive, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, NULL);
    if ((Extract->Path == NULL) || (Extract->Link == NULL) || (Extract->Data == NULL) || (File == INVALID_HANDLE_VALUE) ||
        !RtlDosPathNameToNtPathName_U(Staging, &Extract->Root, NULL, NULL))
    {
        fwprintf(stderr, L"lxterm: cannot open %s\n", Archive);
        goto Cleanup;
    }
    if (GetFileSizeEx(File, &Size))
        Extract->ArchiveSize = Size.QuadPart;
    Extract->File = File;
    if (!ReadFile(File, Magic, sizeof(Magic), &Read, NULL) || (SetFilePointer(File, 0, NULL, FILE_BEGIN) != 0))
        Read = 0;
    if ((Read == sizeof(Magic)) && (memcmp(Magic, XzMagic, sizeof(XzMagic)) == 0))
    {
        Extract->Xz = LxXzOpen(File);
        if (Extract->Xz == NULL)
            goto Cleanup;
    }
    else if ((Read >= 2) && (Magic[0] == 0x1F) && (Magic[1] == 0x8B))
    {
        Extract->Gz = LxGzOpen(File);
        if (Extract->Gz == NULL)
            goto Cleanup;
    }

    if (!LxExtract(Extract))
    {
        LxExtractProgress(Extract, TRUE);
        if (Extract->FirstStatus == STATUS_DISK_FULL)
            fwprintf(stderr, L"lxterm: the disk is full\n");
        else
            fwprintf(stderr, L"lxterm: %s is damaged or is not a .tar, .tar.gz or .tar.xz archive\n", Archive);
        goto Cleanup;
    }
    LxExtractProgress(Extract, TRUE);
    if (Extract->Failures != 0)
    {
        fwprintf(stderr, L"lxterm: %lu entries could not be created; the first was /%S (0x%08lx)\n",
                 Extract->Failures, Extract->FirstName, Extract->FirstStatus);
    }

    if (!NT_SUCCESS(LxWriteFileData(Extract, "etc/fstab", "# UNCONFIGURED FSTAB\n", 21, 0644)))
    {
        fwprintf(stderr, L"lxterm: cannot write /etc/fstab\n");
        goto Cleanup;
    }
    if (!LxProvisionGui(Extract))
        wprintf(L"  The Wayland compositor files were not found; Linux windows will not be shown.\n");
    Result = 0;

Cleanup:
    if (Extract->Xz != NULL)
        LxXzClose(Extract->Xz);
    if (Extract->Gz != NULL)
        LxGzClose(Extract->Gz);
    if (File != INVALID_HANDLE_VALUE)
        CloseHandle(File);
    if (Extract->Root.Buffer != NULL)
        RtlFreeUnicodeString(&Extract->Root);
    free(Extract->Data);
    free(Extract->Link);
    free(Extract->Path);
    free(Extract);
    return Result;
}

static
BOOL
LxDirectoryEmpty(
    _In_ PCWSTR Directory)
{
    WCHAR Pattern[MAX_PATH + 4];
    WIN32_FIND_DATAW Data;
    HANDLE Find;
    BOOL Empty = TRUE;

    _snwprintf(Pattern, ARRAYSIZE(Pattern), L"%s\\*", Directory);
    Pattern[ARRAYSIZE(Pattern) - 1] = L'\0';
    Find = FindFirstFileW(Pattern, &Data);
    if (Find == INVALID_HANDLE_VALUE)
        return TRUE;
    do
    {
        if ((wcscmp(Data.cFileName, L".") != 0) && (wcscmp(Data.cFileName, L"..") != 0))
            Empty = FALSE;
    } while (Empty && FindNextFileW(Find, &Data));
    FindClose(Find);
    return Empty;
}

static
VOID
LxDeleteTree(
    _In_ PCWSTR Path)
{
    WIN32_FIND_DATAW Data;
    PWCHAR Pattern;
    HANDLE Find;
    SIZE_T Length = wcslen(Path);

    Pattern = malloc((Length + MAX_PATH + 8) * sizeof(WCHAR));
    if (Pattern == NULL)
        return;
    _snwprintf(Pattern, Length + 3, L"%s\\*", Path);
    Pattern[Length + 2] = L'\0';
    Find = FindFirstFileW(Pattern, &Data);
    if (Find != INVALID_HANDLE_VALUE)
    {
        do
        {
            if ((wcscmp(Data.cFileName, L".") == 0) || (wcscmp(Data.cFileName, L"..") == 0))
                continue;
            _snwprintf(Pattern, Length + MAX_PATH + 8, L"%s\\%s", Path, Data.cFileName);
            Pattern[Length + MAX_PATH + 7] = L'\0';
            if ((Data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) &&
                !(Data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT))
            {
                LxDeleteTree(Pattern);
            }
            else
            {
                if (Data.dwFileAttributes & FILE_ATTRIBUTE_READONLY)
                    SetFileAttributesW(Pattern, FILE_ATTRIBUTE_NORMAL);
                DeleteFileW(Pattern);
            }
        } while (FindNextFileW(Find, &Data));
        FindClose(Find);
    }
    free(Pattern);
    RemoveDirectoryW(Path);
}

static
VOID
LxCleanupInstall(
    _In_ PCWSTR Archive,
    _In_ PCWSTR Part,
    _In_ PCWSTR Staging,
    _In_ BOOL DeleteArchive)
{
    WCHAR Long[MAX_PATH + 24];

    DeleteFileW(Part);
    if (DeleteArchive)
        DeleteFileW(Archive);
    if (GetFileAttributesW(Staging) != INVALID_FILE_ATTRIBUTES)
    {
        if (Staging[0] == L'\\')
            wcsncpy(Long, Staging, ARRAYSIZE(Long));
        else
            _snwprintf(Long, ARRAYSIZE(Long), L"\\\\?\\%s", Staging);
        Long[ARRAYSIZE(Long) - 1] = L'\0';
        wprintf(L"  Removing the unfinished installation...\n");
        LxDeleteTree(Long);
    }
}

BOOL
LxAppendRootFile(
    _In_ PCWSTR Root,
    _In_ PCWSTR Relative,
    _In_ PCSTR Text)
{
    WCHAR Path[MAX_PATH];
    HANDLE File;
    DWORD Written;
    BOOL Created, Success;

    if (!GetFullPathNameW(Root, MAX_PATH, Path, NULL) || (wcslen(Path) + wcslen(Relative) >= MAX_PATH))
        return FALSE;
    wcscat(Path, Relative);
    File = CreateFileW(Path, FILE_APPEND_DATA | FILE_WRITE_EA | SYNCHRONIZE, FILE_SHARE_READ, NULL, OPEN_ALWAYS,
                       FILE_ATTRIBUTE_NORMAL, NULL);
    if (File == INVALID_HANDLE_VALUE)
        return FALSE;
    Created = (GetLastError() != ERROR_ALREADY_EXISTS);
    Success = WriteFile(File, Text, (DWORD)strlen(Text), &Written, NULL) && (Written == strlen(Text));
    if (Success && Created)
        Success = NT_SUCCESS(LxSetAttributes(File, LX_S_IFREG | 0644, 0, 0, 0, 0));
    CloseHandle(File);
    return Success;
}

BOOL
LxRootInstalled(
    _In_ PCWSTR Root)
{
    WCHAR Path[MAX_PATH];
    DWORD Attributes;

    if (!GetFullPathNameW(Root, MAX_PATH, Path, NULL) || (wcslen(Path) + wcslen(L"\\etc\\passwd") >= MAX_PATH))
        return FALSE;
    wcscat(Path, L"\\etc\\passwd");
    Attributes = GetFileAttributesW(Path);
    return (Attributes != INVALID_FILE_ATTRIBUTES) && !(Attributes & FILE_ATTRIBUTE_DIRECTORY);
}

int
LxInstallRoot(
    _In_ PCWSTR Root,
    _In_opt_ PCWSTR Url,
    _In_ BOOL AssumeYes)
{
    WCHAR Full[MAX_PATH], Archive[MAX_PATH + 16], Part[MAX_PATH + 24], Staging[MAX_PATH + 16], Answer[16];
    PCWSTR Source = (Url != NULL) ? Url : LxDefaultUrl();
    UCHAR Expected[32], Actual[32];
    ULONG64 ArchiveSize = 0, ContentSize = 0, Free = 0, Needed = 0;
    WCHAR Volume[MAX_PATH];
    DWORD Attributes, Error;
    BOOL HaveExpected, HaveFree;
    SIZE_T Length;

    if (Source == NULL)
    {
        fwprintf(stderr, L"lxterm: no Linux download is known for this processor; pass -u <url>\n");
        return 1;
    }
    if (!GetFullPathNameW(Root, MAX_PATH, Full, NULL))
    {
        fwprintf(stderr, L"lxterm: invalid root directory '%s'\n", Root);
        return 1;
    }
    Length = wcslen(Full);
    if ((Length == 0) || (Full[Length - 1] == L'\\'))
    {
        fwprintf(stderr, L"lxterm: choose a directory for the Linux system, not %s\n", Full);
        return 1;
    }
    Attributes = GetFileAttributesW(Full);
    if ((Attributes != INVALID_FILE_ATTRIBUTES) &&
        (!(Attributes & FILE_ATTRIBUTE_DIRECTORY) || !LxDirectoryEmpty(Full)))
    {
        fwprintf(stderr, L"lxterm: %s exists but holds no Linux system; choose another directory with -r\n", Full);
        return 1;
    }

    wprintf(L"No Linux system is installed in %s.\n", Full);
    if (Url == NULL)
        wprintf(L"Ubuntu 26.04 LTS can be downloaded from Canonical:\n");
    else if (LxIsUrl(Source))
        wprintf(L"A Linux system can be downloaded from:\n");
    else
        wprintf(L"A Linux system can be installed from:\n");
    wprintf(L"  %s\n", Source);
    if (LxArchiveSizes(Source, &ArchiveSize, &ContentSize))
    {
        Needed = LxInstalledSize(ContentSize) + (LxIsUrl(Source) ? ArchiveSize : 0);
        wprintf(LxIsUrl(Source) ? L"  Download size: " : L"  Archive size: ");
        LxPrintSize(ArchiveSize);
        wprintf(L". Installed size: about ");
        LxPrintSize(ContentSize + ContentSize / 10);
        wprintf(L".\n");
    }
    HaveFree = LxFreeSpace(Full, Volume, &Free);
    if (HaveFree)
    {
        wprintf(L"  Free space on %s: ", Volume);
        LxPrintSize(Free);
        wprintf(L".\n");
        if ((Needed != 0) && (Free < Needed))
        {
            LxPrintNoSpace(Volume, Needed, Free);
            return 1;
        }
    }
    if (!AssumeYes)
    {
        wprintf(LxIsUrl(Source) ? L"Download and install it now? [Y/n] " : L"Install it now? [Y/n] ");
        fflush(stdout);
        if (fgetws(Answer, ARRAYSIZE(Answer), stdin) == NULL)
            return 1;
        if ((Answer[0] != L'\n') && (Answer[0] != L'\r') && (Answer[0] != L'\0') && (towlower(Answer[0]) != L'y'))
        {
            wprintf(L"Installation cancelled.\n");
            return 1;
        }
    }

    _snwprintf(Archive, ARRAYSIZE(Archive), L"%s.download", Full);
    _snwprintf(Part, ARRAYSIZE(Part), L"%s.part", Archive);
    _snwprintf(Staging, ARRAYSIZE(Staging), L"%s.install", Full);

    if (!LxIsUrl(Source))
    {
        if (!GetFullPathNameW(Source, MAX_PATH, Archive, NULL) ||
            (GetFileAttributesW(Archive) == INVALID_FILE_ATTRIBUTES))
        {
            fwprintf(stderr, L"lxterm: cannot find %s\n", Source);
            return 1;
        }
        HaveExpected = FALSE;
    }
    else
    {
        HaveExpected = LxFetchChecksum(Source, Expected);
        if (!HaveExpected)
            wprintf(L"  No SHA256SUMS entry was found for this file; the download cannot be verified.\n");
    }
    if (LxIsUrl(Source) &&
        (!HaveExpected || !LxHashFile(Archive, Actual) || (memcmp(Expected, Actual, sizeof(Actual)) != 0)))
    {
        Error = LxDownloadFile(Source, Part, HaveFree ? ((Free > LX_SPACE_MARGIN) ? Free - LX_SPACE_MARGIN : 0) : MAXULONG64,
                               Actual);
        if (Error != ERROR_SUCCESS)
        {
            LxPrintDownloadError(Error);
            LxCleanupInstall(Archive, Part, Staging, TRUE);
            return 1;
        }
        if (HaveExpected && (memcmp(Expected, Actual, sizeof(Actual)) != 0))
        {
            fwprintf(stderr, L"lxterm: the download does not match its published SHA-256 checksum\n");
            LxCleanupInstall(Archive, Part, Staging, TRUE);
            return 1;
        }
        if (!MoveFileExW(Part, Archive, MOVEFILE_REPLACE_EXISTING))
        {
            fwprintf(stderr, L"lxterm: cannot rename %s (error %lu)\n", Part, GetLastError());
            LxCleanupInstall(Archive, Part, Staging, TRUE);
            return 1;
        }
    }
    if (HaveExpected)
        wprintf(L"  The SHA-256 checksum matches Canonical's SHA256SUMS.\n");

    LxCleanupInstall(Archive, Part, Staging, FALSE);
    if (LxArchiveSizes(Archive, &ArchiveSize, &ContentSize) && LxFreeSpace(Full, Volume, &Free) &&
        (Free < LxInstalledSize(ContentSize)))
    {
        LxPrintNoSpace(Volume, LxInstalledSize(ContentSize), Free);
        LxCleanupInstall(Archive, Part, Staging, LxIsUrl(Source));
        return 1;
    }
    if (!CreateDirectoryW(Staging, NULL))
    {
        fwprintf(stderr, L"lxterm: cannot create %s (error %lu)\n", Staging, GetLastError());
        LxCleanupInstall(Archive, Part, Staging, LxIsUrl(Source));
        return 1;
    }
    if (LxUnpack(Archive, Staging) != 0)
    {
        LxCleanupInstall(Archive, Part, Staging, LxIsUrl(Source));
        return 1;
    }

    if (GetFileAttributesW(Full) != INVALID_FILE_ATTRIBUTES)
        RemoveDirectoryW(Full);
    if (!MoveFileExW(Staging, Full, 0))
    {
        fwprintf(stderr, L"lxterm: cannot rename %s to %s (error %lu)\n", Staging, Full, GetLastError());
        LxCleanupInstall(Archive, Part, Staging, LxIsUrl(Source));
        return 1;
    }
    LxCleanupInstall(Archive, Part, Staging, LxIsUrl(Source));
    wprintf(L"The Linux system is installed in %s.\n\n", Full);
    return 0;
}
