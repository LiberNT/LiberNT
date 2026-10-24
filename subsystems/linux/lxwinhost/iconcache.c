/*
 * PROJECT:     LiberNT Linux Subsystem
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Per-user cache of the icons of Linux applications
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define WIN32_NO_STATUS
#include <windef.h>
#include <winbase.h>
#include <wingdi.h>
#include <winuser.h>
#include <winnls.h>
#include <winreg.h>
#include <shlobj.h>
#include <bcrypt.h>

#include "../include/lxwin.h"
#include "../include/lxappid.h"
#include "lxwinhost.h"

#define LX_CACHE_KEY            L"Software\\LiberNT\\Linux\\IconCache"
#define LX_CACHE_MUTEX          L"Local\\LiberNT.Linux.IconCache"
#define LX_CACHE_MAX_ENTRIES    512
#define LX_CACHE_MAX_FILE       (4 * 1024 * 1024)
#define LX_FILETIME_HOUR        (3600ULL * 10000000ULL)
#define LX_FILETIME_DAY         (24ULL * LX_FILETIME_HOUR)
#define LX_CACHE_ROOT_GONE_AGE  (30ULL * LX_FILETIME_DAY)
#define LX_CACHE_UNUSED_AGE     (90ULL * LX_FILETIME_DAY)
#define LX_CACHE_ORPHAN_AGE     LX_FILETIME_HOUR
#define LX_CACHE_TOUCH_AGE      LX_FILETIME_HOUR
#define LX_NO_ENTRY             0xFFFFFFFFUL

static const UCHAR LxCacheNamespace[16] =
{
    0xb9, 0x61, 0xa2, 0x44, 0x4c, 0xd1, 0x40, 0xd5, 0xaa, 0x74, 0x17, 0xc5, 0x4a, 0x02, 0xaa, 0x8d
};

static WCHAR LxCacheRoot[MAX_PATH];
static WCHAR LxCacheDirectory[MAX_PATH];
static HANDLE LxCacheMutex;
static BOOL LxCacheReady;

typedef struct _LX_CACHE_ENTRY
{
    WCHAR Name[40];
    ULONG64 LastUsed;
} LX_CACHE_ENTRY, *PLX_CACHE_ENTRY;

static
ULONG64
LxNow(VOID)
{
    FILETIME Now;

    GetSystemTimeAsFileTime(&Now);
    return ((ULONG64)Now.dwHighDateTime << 32) | Now.dwLowDateTime;
}

static
BOOL
LxHash(
    _In_ PCWSTR Algorithm,
    _In_reads_bytes_(Length) const VOID *Data,
    _In_ ULONG Length,
    _Out_writes_bytes_(OutputLength) PUCHAR Output,
    _In_ ULONG OutputLength)
{
    BCRYPT_ALG_HANDLE Provider;
    NTSTATUS Status;

    if (BCryptOpenAlgorithmProvider(&Provider, Algorithm, NULL, 0) != 0)
        return FALSE;
    Status = BCryptHash(Provider, NULL, 0, (PUCHAR)Data, Length, Output, OutputLength);
    BCryptCloseAlgorithmProvider(Provider, 0);
    return Status == 0;
}

static
BOOL
LxCacheEntryName(
    _In_reads_bytes_(AppIdLength) PCSTR AppId,
    _In_ ULONG AppIdLength,
    _Out_writes_(40) PWSTR Name)
{
    UCHAR Digest[20];
    PUCHAR Input;
    int RootLength;
    ULONG Length;
    BOOL Success;

    RootLength = WideCharToMultiByte(CP_UTF8, 0, LxCacheRoot, -1, NULL, 0, NULL, NULL);
    if (RootLength <= 0)
        return FALSE;
    Length = sizeof(LxCacheNamespace) + (ULONG)RootLength + AppIdLength;
    Input = HeapAlloc(GetProcessHeap(), 0, Length);
    if (Input == NULL)
        return FALSE;
    memcpy(Input, LxCacheNamespace, sizeof(LxCacheNamespace));
    WideCharToMultiByte(CP_UTF8, 0, LxCacheRoot, -1, (PCHAR)Input + sizeof(LxCacheNamespace), RootLength, NULL, NULL);
    Input[sizeof(LxCacheNamespace) + RootLength - 1] = '\n';
    memcpy(Input + sizeof(LxCacheNamespace) + RootLength, AppId, AppIdLength);
    Success = LxHash(BCRYPT_SHA1_ALGORITHM, Input, Length, Digest, sizeof(Digest));
    HeapFree(GetProcessHeap(), 0, Input);
    if (!Success)
        return FALSE;
    Digest[6] = (Digest[6] & 0x0F) | 0x50;
    Digest[8] = (Digest[8] & 0x3F) | 0x80;
    _snwprintf(Name, 40, L"{%02X%02X%02X%02X-%02X%02X-%02X%02X-%02X%02X-%02X%02X%02X%02X%02X%02X}",
               Digest[0], Digest[1], Digest[2], Digest[3], Digest[4], Digest[5], Digest[6], Digest[7],
               Digest[8], Digest[9], Digest[10], Digest[11], Digest[12], Digest[13], Digest[14], Digest[15]);
    Name[39] = UNICODE_NULL;
    return TRUE;
}

static
VOID
LxCacheFilePath(
    _In_ PCWSTR Name,
    _In_ PCWSTR Extension,
    _Out_writes_(MAX_PATH) PWSTR Path)
{
    _snwprintf(Path, MAX_PATH, L"%s\\%s%s", LxCacheDirectory, Name, Extension);
    Path[MAX_PATH - 1] = UNICODE_NULL;
}

static
BOOL
LxCacheLock(VOID)
{
    DWORD Wait;

    if (!LxCacheReady)
        return FALSE;
    Wait = WaitForSingleObject(LxCacheMutex, 10000);
    return (Wait == WAIT_OBJECT_0) || (Wait == WAIT_ABANDONED);
}

static
VOID
LxCacheUnlock(VOID)
{
    ReleaseMutex(LxCacheMutex);
}

static
HKEY
LxCacheOpenEntry(
    _In_ PCWSTR Name,
    _In_ BOOL Create)
{
    WCHAR KeyPath[128];
    HKEY Key;
    LONG Error;

    _snwprintf(KeyPath, ARRAYSIZE(KeyPath), L"%s\\%s", LX_CACHE_KEY, Name);
    KeyPath[ARRAYSIZE(KeyPath) - 1] = UNICODE_NULL;
    if (Create)
        Error = RegCreateKeyExW(HKEY_CURRENT_USER, KeyPath, 0, NULL, 0, KEY_READ | KEY_WRITE, NULL, &Key, NULL);
    else
        Error = RegOpenKeyExW(HKEY_CURRENT_USER, KeyPath, 0, KEY_READ | KEY_WRITE, &Key);
    return (Error == ERROR_SUCCESS) ? Key : NULL;
}

static
ULONG64
LxCacheReadTime(
    _In_ HKEY Key,
    _In_ PCWSTR Value)
{
    ULONG64 Time = 0;
    DWORD Size = sizeof(Time), Type;

    if ((RegQueryValueExW(Key, Value, NULL, &Type, (PBYTE)&Time, &Size) != ERROR_SUCCESS) ||
        (Type != REG_QWORD) || (Size != sizeof(Time)))
    {
        return 0;
    }
    return Time;
}

static
VOID
LxCacheWriteTime(
    _In_ HKEY Key,
    _In_ PCWSTR Value,
    _In_ ULONG64 Time)
{
    RegSetValueExW(Key, Value, 0, REG_QWORD, (const BYTE *)&Time, sizeof(Time));
}

static
VOID
LxCacheWriteString(
    _In_ HKEY Key,
    _In_ PCWSTR Value,
    _In_ PCWSTR Data)
{
    RegSetValueExW(Key, Value, 0, REG_SZ, (const BYTE *)Data, (DWORD)((wcslen(Data) + 1) * sizeof(WCHAR)));
}

static
BOOL
LxCacheReadString(
    _In_ HKEY Key,
    _In_ PCWSTR Value,
    _Out_writes_(Count) PWSTR Data,
    _In_ DWORD Count)
{
    DWORD Size = (Count - 1) * sizeof(WCHAR), Type;

    ZeroMemory(Data, Count * sizeof(WCHAR));
    return (RegQueryValueExW(Key, Value, NULL, &Type, (PBYTE)Data, &Size) == ERROR_SUCCESS) && (Type == REG_SZ);
}

static
PUCHAR
LxReadFile(
    _In_ PCWSTR Path,
    _Out_ PDWORD Size)
{
    HANDLE File = CreateFileW(Path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE, NULL, OPEN_EXISTING, 0, NULL);
    PUCHAR Data = NULL;
    DWORD Read;

    *Size = 0;
    if (File == INVALID_HANDLE_VALUE)
        return NULL;
    *Size = GetFileSize(File, NULL);
    if ((*Size != INVALID_FILE_SIZE) && (*Size != 0) && (*Size <= LX_CACHE_MAX_FILE))
        Data = HeapAlloc(GetProcessHeap(), 0, *Size);
    if ((Data != NULL) && (!ReadFile(File, Data, *Size, &Read, NULL) || (Read != *Size)))
    {
        HeapFree(GetProcessHeap(), 0, Data);
        Data = NULL;
    }
    CloseHandle(File);
    return Data;
}

static
VOID
LxPutWord(
    _Out_writes_bytes_(2) PUCHAR Out,
    _In_ ULONG Value)
{
    Out[0] = (UCHAR)Value;
    Out[1] = (UCHAR)(Value >> 8);
}

static
VOID
LxPutDword(
    _Out_writes_bytes_(4) PUCHAR Out,
    _In_ ULONG Value)
{
    LxPutWord(Out, Value & 0xFFFF);
    LxPutWord(Out + 2, Value >> 16);
}

static
ULONG
LxGetDword(
    _In_reads_bytes_(4) const UCHAR *In)
{
    return In[0] | ((ULONG)In[1] << 8) | ((ULONG)In[2] << 16) | ((ULONG)In[3] << 24);
}

PUCHAR
LxBuildIcon(
    _In_ PLX_WIN_ICON_MSG Message,
    _In_ ULONG Size,
    _Out_ PULONG IconSize)
{
    PLX_WIN_ICON_IMAGE Images = (PLX_WIN_ICON_IMAGE)(Message + 1);
    const UCHAR *Pixels;
    ULONG64 Needed, Total;
    PUCHAR Icon, Entry, Image;
    ULONG Index, Offset;

    *IconSize = 0;
    if ((Message->Count == 0) || (Message->Count > 16) ||
        (Size < sizeof(*Message) + (ULONG64)Message->Count * sizeof(LX_WIN_ICON_IMAGE)))
    {
        return NULL;
    }
    Needed = sizeof(*Message) + (ULONG64)Message->Count * sizeof(LX_WIN_ICON_IMAGE);
    Total = 6 + 16ULL * Message->Count;
    for (Index = 0; Index < Message->Count; Index++)
    {
        ULONG Width = Images[Index].Width;

        if ((Width == 0) || (Width > LX_MAX_ICON) || (Images[Index].Height != Width))
            return NULL;
        Needed += (ULONG64)Width * Width * 4;
        Total += sizeof(BITMAPINFOHEADER) + (ULONG64)Width * Width * 4 + (ULONG64)((Width + 31) / 32) * 4 * Width;
    }
    if ((Needed > Size) || (Total > LX_CACHE_MAX_FILE))
        return NULL;

    Icon = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, (SIZE_T)Total);
    if (Icon == NULL)
        return NULL;
    LxPutWord(Icon, 0);
    LxPutWord(Icon + 2, 1);
    LxPutWord(Icon + 4, Message->Count);
    Offset = 6 + 16 * Message->Count;
    Pixels = (const UCHAR *)(Images + Message->Count);
    for (Index = 0; Index < Message->Count; Index++)
    {
        ULONG Width = Images[Index].Width;
        ULONG MaskStride = ((Width + 31) / 32) * 4;
        ULONG Bytes = sizeof(BITMAPINFOHEADER) + Width * Width * 4 + MaskStride * Width;
        BITMAPINFOHEADER Header;
        PUCHAR Mask;
        ULONG Row, Column;

        Entry = Icon + 6 + 16 * Index;
        Entry[0] = (UCHAR)(Width >= 256 ? 0 : Width);
        Entry[1] = (UCHAR)(Width >= 256 ? 0 : Width);
        LxPutWord(Entry + 4, 1);
        LxPutWord(Entry + 6, 32);
        LxPutDword(Entry + 8, Bytes);
        LxPutDword(Entry + 12, Offset);

        Image = Icon + Offset;
        ZeroMemory(&Header, sizeof(Header));
        Header.biSize = sizeof(Header);
        Header.biWidth = (LONG)Width;
        Header.biHeight = (LONG)(Width * 2);
        Header.biPlanes = 1;
        Header.biBitCount = 32;
        Header.biCompression = BI_RGB;
        Header.biSizeImage = Width * Width * 4 + MaskStride * Width;
        memcpy(Image, &Header, sizeof(Header));
        Mask = Image + sizeof(Header) + Width * Width * 4;
        for (Row = 0; Row < Width; Row++)
        {
            const UCHAR *Source = Pixels + (SIZE_T)(Width - 1 - Row) * Width * 4;
            PUCHAR Target = Image + sizeof(Header) + (SIZE_T)Row * Width * 4;

            for (Column = 0; Column < Width; Column++)
            {
                ULONG Alpha = Source[Column * 4 + 3];
                ULONG Channel;

                for (Channel = 0; Channel < 3; Channel++)
                {
                    ULONG Value = Source[Column * 4 + Channel];

                    if ((Alpha != 0) && (Alpha != 255))
                        Value = min(255, (Value * 255 + Alpha / 2) / Alpha);
                    else if (Alpha == 0)
                        Value = 0;
                    Target[Column * 4 + Channel] = (UCHAR)Value;
                }
                Target[Column * 4 + 3] = (UCHAR)Alpha;
                if (Alpha == 0)
                    Mask[Row * MaskStride + Column / 8] |= (UCHAR)(0x80 >> (Column % 8));
            }
        }
        Offset += Bytes;
        Pixels += (SIZE_T)Width * Width * 4;
    }
    *IconSize = Offset;
    return Icon;
}

static
BOOL
LxValidIcon(
    _In_reads_bytes_(Size) const UCHAR *Icon,
    _In_ ULONG Size)
{
    ULONG Count, Index;

    if ((Size < 6) || (Icon[0] != 0) || (Icon[1] != 0) || (Icon[2] != 1) || (Icon[3] != 0))
        return FALSE;
    Count = Icon[4] | ((ULONG)Icon[5] << 8);
    if ((Count == 0) || (6 + 16ULL * Count > Size))
        return FALSE;
    for (Index = 0; Index < Count; Index++)
    {
        const UCHAR *Entry = Icon + 6 + 16 * Index;
        ULONG Bytes = LxGetDword(Entry + 8), Offset = LxGetDword(Entry + 12);

        if ((Offset > Size) || (Bytes > Size - Offset) || (Bytes < sizeof(BITMAPINFOHEADER)))
            return FALSE;
    }
    return TRUE;
}

HICON
LxCreateIconFromIco(
    _In_reads_bytes_(Size) const UCHAR *Icon,
    _In_ ULONG Size,
    _In_ int Desired)
{
    ULONG Count, Index, Best = LX_NO_ENTRY, BestWidth = 0;

    if (!LxValidIcon(Icon, Size))
        return NULL;
    Count = Icon[4] | ((ULONG)Icon[5] << 8);
    for (Index = 0; Index < Count; Index++)
    {
        ULONG Width = Icon[6 + 16 * Index] != 0 ? Icon[6 + 16 * Index] : 256;

        if ((Best == LX_NO_ENTRY) ||
            ((Width >= (ULONG)Desired) && ((BestWidth < (ULONG)Desired) || (Width < BestWidth))) ||
            ((BestWidth < (ULONG)Desired) && (Width > BestWidth)))
        {
            Best = Index;
            BestWidth = Width;
        }
    }
    return CreateIconFromResourceEx((PBYTE)Icon + LxGetDword(Icon + 6 + 16 * Best + 12),
                                    LxGetDword(Icon + 6 + 16 * Best + 8), TRUE, 0x00030000, Desired, Desired,
                                    LR_DEFAULTCOLOR);
}

static
VOID
LxCacheSizes(
    _In_reads_bytes_(Size) const UCHAR *Icon,
    _In_ ULONG Size,
    _Out_writes_(Count) PWSTR Sizes,
    _In_ ULONG Count)
{
    ULONG Entries = Icon[4] | ((ULONG)Icon[5] << 8), Index, Length = 0;

    Sizes[0] = UNICODE_NULL;
    for (Index = 0; (Index < Entries) && (Length + 6 < Count); Index++)
    {
        ULONG Width = Icon[6 + 16 * Index] != 0 ? Icon[6 + 16 * Index] : 256;

        Length += _snwprintf(Sizes + Length, Count - Length, Index == 0 ? L"%lu" : L",%lu", Width);
    }
    Sizes[Count - 1] = UNICODE_NULL;
}

BOOL
LxIconCacheLoad(
    _In_reads_bytes_(AppIdLength) PCSTR AppId,
    _In_ ULONG AppIdLength,
    _Out_ HICON *BigIcon,
    _Out_ HICON *SmallIcon)
{
    WCHAR Name[40], Path[MAX_PATH];
    PUCHAR Icon = NULL;
    DWORD Size = 0;
    ULONG64 Now = LxNow();
    HKEY Key;

    *BigIcon = NULL;
    *SmallIcon = NULL;
    if (!LxCacheEntryName(AppId, AppIdLength, Name) || !LxCacheLock())
        return FALSE;
    Key = LxCacheOpenEntry(Name, FALSE);
    if (Key != NULL)
    {
        LxCacheFilePath(Name, L".ico", Path);
        Icon = LxReadFile(Path, &Size);
        if ((Icon != NULL) && (Now - LxCacheReadTime(Key, L"LastUsed") > LX_CACHE_TOUCH_AGE))
            LxCacheWriteTime(Key, L"LastUsed", Now);
        RegCloseKey(Key);
    }
    LxCacheUnlock();
    if (Icon == NULL)
        return FALSE;
    *BigIcon = LxCreateIconFromIco(Icon, Size, GetSystemMetrics(SM_CXICON));
    *SmallIcon = LxCreateIconFromIco(Icon, Size, GetSystemMetrics(SM_CXSMICON));
    HeapFree(GetProcessHeap(), 0, Icon);
    return (*BigIcon != NULL) || (*SmallIcon != NULL);
}

VOID
LxIconCacheStore(
    _In_reads_bytes_(AppIdLength) PCSTR AppId,
    _In_ ULONG AppIdLength,
    _In_reads_bytes_(IconSize) const UCHAR *Icon,
    _In_ ULONG IconSize)
{
    WCHAR Name[40], Path[MAX_PATH], Temporary[MAX_PATH], AppIdText[LX_APP_ID_MAX + 1], Sizes[64];
    UCHAR Fingerprint[32], Stored[32];
    DWORD StoredSize = sizeof(Stored), Type, Written;
    ULONG64 Now = LxNow();
    BOOL Same = FALSE;
    HANDLE File;
    HKEY Key;
    int Length;

    if (!LxHash(BCRYPT_SHA256_ALGORITHM, Icon, IconSize, Fingerprint, sizeof(Fingerprint)) ||
        !LxCacheEntryName(AppId, AppIdLength, Name))
    {
        return;
    }
    Length = MultiByteToWideChar(CP_UTF8, 0, AppId, (int)AppIdLength, AppIdText, LX_APP_ID_MAX);
    if (Length <= 0)
        return;
    AppIdText[Length] = UNICODE_NULL;
    if (!LxCacheLock())
        return;
    Key = LxCacheOpenEntry(Name, TRUE);
    if (Key == NULL)
    {
        LxCacheUnlock();
        return;
    }
    LxCacheFilePath(Name, L".ico", Path);
    if ((RegQueryValueExW(Key, L"Fingerprint", NULL, &Type, Stored, &StoredSize) == ERROR_SUCCESS) &&
        (Type == REG_BINARY) && (StoredSize == sizeof(Stored)) && (memcmp(Stored, Fingerprint, sizeof(Stored)) == 0) &&
        (GetFileAttributesW(Path) != INVALID_FILE_ATTRIBUTES))
    {
        Same = TRUE;
    }
    if (!Same)
    {
        LxCacheFilePath(Name, L".tmp", Temporary);
        File = CreateFileW(Temporary, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        if (File != INVALID_HANDLE_VALUE)
        {
            BOOL Success = WriteFile(File, Icon, IconSize, &Written, NULL) && (Written == IconSize);

            CloseHandle(File);
            if (Success && MoveFileExW(Temporary, Path, MOVEFILE_REPLACE_EXISTING))
            {
                if (LxCacheReadTime(Key, L"Created") == 0)
                    LxCacheWriteTime(Key, L"Created", Now);
                LxCacheWriteTime(Key, L"Updated", Now);
                LxCacheWriteString(Key, L"Root", LxCacheRoot);
                LxCacheWriteString(Key, L"AppId", AppIdText);
                LxCacheWriteString(Key, L"File", Path);
                LxCacheSizes(Icon, IconSize, Sizes, ARRAYSIZE(Sizes));
                LxCacheWriteString(Key, L"Sizes", Sizes);
                RegSetValueExW(Key, L"Fingerprint", 0, REG_BINARY, Fingerprint, sizeof(Fingerprint));
                LxCacheWriteTime(Key, L"LastUsed", Now);
            }
            else
            {
                DeleteFileW(Temporary);
            }
        }
    }
    else if (Now - LxCacheReadTime(Key, L"LastUsed") > LX_CACHE_TOUCH_AGE)
    {
        LxCacheWriteTime(Key, L"LastUsed", Now);
    }
    RegCloseKey(Key);
    LxCacheUnlock();
}

static
int
__cdecl
LxCompareEntries(
    _In_ const void *Left,
    _In_ const void *Right)
{
    ULONG64 A = ((const LX_CACHE_ENTRY *)Left)->LastUsed, B = ((const LX_CACHE_ENTRY *)Right)->LastUsed;

    return (A < B) ? -1 : (A > B) ? 1 : 0;
}

static
VOID
LxCacheDeleteEntry(
    _In_ HKEY Cache,
    _In_ PCWSTR Name)
{
    WCHAR Path[MAX_PATH];

    LxCacheFilePath(Name, L".ico", Path);
    DeleteFileW(Path);
    RegDeleteKeyW(Cache, Name);
}

static
BOOL
LxCacheKnown(
    _In_reads_(Count) const LX_CACHE_ENTRY *Entries,
    _In_ ULONG Count,
    _In_ PCWSTR FileName)
{
    ULONG Index;
    SIZE_T Length;

    for (Index = 0; Index < Count; Index++)
    {
        Length = wcslen(Entries[Index].Name);
        if ((_wcsnicmp(FileName, Entries[Index].Name, Length) == 0) && (_wcsicmp(FileName + Length, L".ico") == 0))
            return TRUE;
    }
    return FALSE;
}

static
DWORD
WINAPI
LxIconCacheCleanup(
    _In_ PVOID Parameter)
{
    PLX_CACHE_ENTRY Entries = NULL;
    ULONG Count = 0, Capacity = 0, Kept = 0, Index;
    WCHAR Name[40], Path[MAX_PATH], Root[MAX_PATH];
    ULONG64 Now = LxNow();
    WIN32_FIND_DATAW Find;
    HANDLE Search;
    HKEY Cache, Key;
    DWORD NameLength;

    UNREFERENCED_PARAMETER(Parameter);
    if (!LxCacheLock())
        return 0;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, LX_CACHE_KEY, 0, NULL, 0, KEY_READ | KEY_WRITE, NULL, &Cache, NULL) !=
        ERROR_SUCCESS)
    {
        LxCacheUnlock();
        return 0;
    }
    for (Index = 0;; Index++)
    {
        NameLength = ARRAYSIZE(Name);
        if (RegEnumKeyExW(Cache, Index, Name, &NameLength, NULL, NULL, NULL, NULL) != ERROR_SUCCESS)
            break;
        if (Count == Capacity)
        {
            PLX_CACHE_ENTRY Grown;

            Capacity = Capacity != 0 ? Capacity * 2 : 64;
            Grown = Entries != NULL ? HeapReAlloc(GetProcessHeap(), 0, Entries, Capacity * sizeof(*Entries))
                                    : HeapAlloc(GetProcessHeap(), 0, Capacity * sizeof(*Entries));
            if (Grown == NULL)
                break;
            Entries = Grown;
        }
        wcscpy(Entries[Count].Name, Name);
        Entries[Count].LastUsed = 0;
        Count++;
    }
    for (Index = 0; Index < Count; Index++)
    {
        BOOL Delete = FALSE, RootGone;
        ULONG64 LastUsed;

        if (RegOpenKeyExW(Cache, Entries[Index].Name, 0, KEY_READ, &Key) != ERROR_SUCCESS)
            continue;
        LastUsed = LxCacheReadTime(Key, L"LastUsed");
        RootGone = !LxCacheReadString(Key, L"Root", Root, ARRAYSIZE(Root)) ||
                   (GetFileAttributesW(Root) == INVALID_FILE_ATTRIBUTES);
        RegCloseKey(Key);
        LxCacheFilePath(Entries[Index].Name, L".ico", Path);
        if (GetFileAttributesW(Path) == INVALID_FILE_ATTRIBUTES)
            Delete = TRUE;
        else if (RootGone && (Now - LastUsed > LX_CACHE_ROOT_GONE_AGE))
            Delete = TRUE;
        else if (Now - LastUsed > LX_CACHE_UNUSED_AGE)
            Delete = TRUE;
        if (Delete)
        {
            LxCacheDeleteEntry(Cache, Entries[Index].Name);
            continue;
        }
        Entries[Index].LastUsed = LastUsed;
        Entries[Kept++] = Entries[Index];
    }
    if (Kept > LX_CACHE_MAX_ENTRIES)
    {
        qsort(Entries, Kept, sizeof(*Entries), LxCompareEntries);
        for (Index = 0; Index < Kept - LX_CACHE_MAX_ENTRIES; Index++)
            LxCacheDeleteEntry(Cache, Entries[Index].Name);
        memmove(Entries, Entries + Kept - LX_CACHE_MAX_ENTRIES, LX_CACHE_MAX_ENTRIES * sizeof(*Entries));
        Kept = LX_CACHE_MAX_ENTRIES;
    }
    RegCloseKey(Cache);

    _snwprintf(Path, ARRAYSIZE(Path), L"%s\\*", LxCacheDirectory);
    Path[ARRAYSIZE(Path) - 1] = UNICODE_NULL;
    Search = FindFirstFileW(Path, &Find);
    if (Search != INVALID_HANDLE_VALUE)
    {
        do
        {
            ULONG64 Written = ((ULONG64)Find.ftLastWriteTime.dwHighDateTime << 32) | Find.ftLastWriteTime.dwLowDateTime;

            if ((Find.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || LxCacheKnown(Entries, Kept, Find.cFileName) ||
                (Now - Written <= LX_CACHE_ORPHAN_AGE))
            {
                continue;
            }
            _snwprintf(Path, ARRAYSIZE(Path), L"%s\\%s", LxCacheDirectory, Find.cFileName);
            Path[ARRAYSIZE(Path) - 1] = UNICODE_NULL;
            DeleteFileW(Path);
        } while (FindNextFileW(Search, &Find));
        FindClose(Search);
    }
    LxCacheUnlock();
    if (Entries != NULL)
        HeapFree(GetProcessHeap(), 0, Entries);
    return 0;
}

VOID
LxIconCacheInitialize(
    _In_ PCWSTR Root)
{
    WCHAR LocalAppData[MAX_PATH];
    HANDLE Thread;

    if (!LxNormalizeRoot(Root, LxCacheRoot, ARRAYSIZE(LxCacheRoot)) ||
        FAILED(SHGetFolderPathW(NULL, CSIDL_LOCAL_APPDATA | CSIDL_FLAG_CREATE, NULL, 0, LocalAppData)))
    {
        return;
    }
    _snwprintf(LxCacheDirectory, ARRAYSIZE(LxCacheDirectory), L"%s\\LiberNT\\Linux\\IconCache", LocalAppData);
    LxCacheDirectory[ARRAYSIZE(LxCacheDirectory) - 1] = UNICODE_NULL;
    if ((SHCreateDirectoryExW(NULL, LxCacheDirectory, NULL) != ERROR_SUCCESS) &&
        (GetFileAttributesW(LxCacheDirectory) == INVALID_FILE_ATTRIBUTES))
    {
        return;
    }
    LxCacheMutex = CreateMutexW(NULL, FALSE, LX_CACHE_MUTEX);
    if (LxCacheMutex == NULL)
        return;
    LxCacheReady = TRUE;
    Thread = CreateThread(NULL, 0, LxIconCacheCleanup, NULL, 0, NULL);
    if (Thread != NULL)
    {
        SetThreadPriority(Thread, THREAD_PRIORITY_LOWEST);
        CloseHandle(Thread);
    }
}
