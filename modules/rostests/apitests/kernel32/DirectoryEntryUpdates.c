/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests that directory enumeration keeps a file's reparse, EA, sparse and compression state after later creates
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"
#include <winioctl.h>

typedef struct _TEST_FULL_EA
{
    ULONG NextEntryOffset;
    UCHAR Flags;
    UCHAR EaNameLength;
    USHORT EaValueLength;
    CHAR EaName[1];
} TEST_FULL_EA;

typedef struct _TEST_FULL_DIR_INFORMATION
{
    ULONG NextEntryOffset;
    ULONG FileIndex;
    LARGE_INTEGER Times[4];
    LARGE_INTEGER EndOfFile;
    LARGE_INTEGER AllocationSize;
    ULONG FileAttributes;
    ULONG FileNameLength;
    ULONG EaSize;
    WCHAR FileName[1];
} TEST_FULL_DIR_INFORMATION;

typedef struct _TEST_IO_STATUS
{
    union { LONG Status; PVOID Pointer; };
    ULONG_PTR Information;
} TEST_IO_STATUS;

typedef LONG (NTAPI *PFN_NT_SET_EA_FILE)(HANDLE, TEST_IO_STATUS *, PVOID, ULONG);
typedef LONG (NTAPI *PFN_NT_QUERY_DIRECTORY_FILE)(HANDLE, HANDLE, PVOID, PVOID, TEST_IO_STATUS *, PVOID, ULONG, ULONG,
                                                  BOOLEAN, PVOID, BOOLEAN);

static WCHAR s_Directory[MAX_PATH];

static void MakePath(PWSTR Path, PCWSTR Name)
{
    StringCchPrintfW(Path, MAX_PATH, L"%ls\\%ls", s_Directory, Name);
}

static BOOL CreateEmptyFile(PCWSTR Name)
{
    WCHAR Path[MAX_PATH];
    HANDLE File;

    MakePath(Path, Name);
    File = CreateFileW(Path, GENERIC_READ | GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (File == INVALID_HANDLE_VALUE)
        return FALSE;
    CloseHandle(File);
    return TRUE;
}

static BOOL FindEntry(PCWSTR Name, WIN32_FIND_DATAW *Data)
{
    WCHAR Path[MAX_PATH];
    HANDLE Find;

    MakePath(Path, Name);
    Find = FindFirstFileW(Path, Data);
    if (Find == INVALID_HANDLE_VALUE)
        return FALSE;
    FindClose(Find);
    return TRUE;
}

static ULONG EnumeratedEaSize(PFN_NT_QUERY_DIRECTORY_FILE QueryDirectory, PCWSTR Name)
{
    TEST_FULL_DIR_INFORMATION *Entry;
    TEST_IO_STATUS Iosb;
    ULONGLONG Buffer[1024];
    HANDLE Directory;
    LONG Status;
    ULONG EaSize = 0xffffffffUL;

    Directory = CreateFileW(s_Directory, FILE_LIST_DIRECTORY, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                            NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
    if (Directory == INVALID_HANDLE_VALUE)
        return EaSize;
    Status = QueryDirectory(Directory, NULL, NULL, NULL, &Iosb, Buffer, sizeof(Buffer), 2, FALSE, NULL, TRUE);
    for (Entry = (TEST_FULL_DIR_INFORMATION *)Buffer; Status >= 0;
         Entry = (TEST_FULL_DIR_INFORMATION *)((PUCHAR)Entry + Entry->NextEntryOffset))
    {
        if (Entry->FileNameLength == wcslen(Name) * sizeof(WCHAR) &&
            !_wcsnicmp(Entry->FileName, Name, wcslen(Name)))
        {
            EaSize = Entry->EaSize;
            break;
        }
        if (!Entry->NextEntryOffset)
            break;
    }
    CloseHandle(Directory);
    return EaSize;
}

START_TEST(DirectoryEntryUpdates)
{
    PFN_NT_SET_EA_FILE SetEaFile;
    PFN_NT_QUERY_DIRECTORY_FILE QueryDirectory;
    WCHAR Path[MAX_PATH], Target[MAX_PATH];
    WIN32_FIND_DATAW Data;
    TEST_IO_STATUS Iosb;
    ULONGLONG EaBuffer[4] = { 0 };
    TEST_FULL_EA *Ea = (TEST_FULL_EA *)EaBuffer;
    USHORT Compression = COMPRESSION_FORMAT_DEFAULT;
    HANDLE File;
    DWORD Returned;
    BOOL Linked, Sparse, Compressed;
    LONG Status = -1;
    ULONG EaSize;

    SetEaFile = (PFN_NT_SET_EA_FILE)GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "NtSetEaFile");
    QueryDirectory = (PFN_NT_QUERY_DIRECTORY_FILE)GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "NtQueryDirectoryFile");
    ok(SetEaFile && QueryDirectory, "ntdll exports are missing\n");
    if (!SetEaFile || !QueryDirectory)
        return;

    GetTempPathW(ARRAYSIZE(s_Directory), s_Directory);
    StringCchCatW(s_Directory, ARRAYSIZE(s_Directory), L"direntryupd");
    ok(CreateDirectoryW(s_Directory, NULL) || GetLastError() == ERROR_ALREADY_EXISTS, "CreateDirectoryW failed with %lu\n", GetLastError());
    ok(CreateEmptyFile(L"target.txt"), "cannot create the link target: %lu\n", GetLastError());

    MakePath(Path, L"link.lnk");
    MakePath(Target, L"target.txt");
    Linked = CreateSymbolicLinkW(Path, Target, 0);
    if (!Linked)
        skip("CreateSymbolicLinkW failed with %lu\n", GetLastError());

    ok(CreateEmptyFile(L"ea.txt"), "cannot create ea.txt: %lu\n", GetLastError());
    MakePath(Path, L"ea.txt");
    File = CreateFileW(Path, FILE_WRITE_EA | FILE_READ_EA, 0, NULL, OPEN_EXISTING, 0, NULL);
    ok(File != INVALID_HANDLE_VALUE, "cannot open ea.txt: %lu\n", GetLastError());
    if (File != INVALID_HANDLE_VALUE)
    {
        Ea->EaNameLength = 4;
        Ea->EaValueLength = 5;
        memcpy(Ea->EaName, "TEST\0hello", 10);
        Status = SetEaFile(File, &Iosb, EaBuffer, 20);
        ok(Status == 0, "NtSetEaFile returned 0x%lx\n", Status);
        CloseHandle(File);
    }

    ok(CreateEmptyFile(L"sparse.txt"), "cannot create sparse.txt: %lu\n", GetLastError());
    MakePath(Path, L"sparse.txt");
    File = CreateFileW(Path, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
    Sparse = File != INVALID_HANDLE_VALUE && DeviceIoControl(File, FSCTL_SET_SPARSE, NULL, 0, NULL, 0, &Returned, NULL);
    ok(Sparse, "FSCTL_SET_SPARSE failed with %lu\n", GetLastError());
    if (File != INVALID_HANDLE_VALUE)
        CloseHandle(File);

    ok(CreateEmptyFile(L"packed.txt"), "cannot create packed.txt: %lu\n", GetLastError());
    MakePath(Path, L"packed.txt");
    File = CreateFileW(Path, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
    Compressed = File != INVALID_HANDLE_VALUE &&
                 DeviceIoControl(File, FSCTL_SET_COMPRESSION, &Compression, sizeof(Compression), NULL, 0, &Returned, NULL);
    ok(Compressed, "FSCTL_SET_COMPRESSION failed with %lu\n", GetLastError());
    if (File != INVALID_HANDLE_VALUE)
        CloseHandle(File);

    /* Each update above rewrote the directory entry; a create in the same directory must keep it */
    ok(CreateEmptyFile(L"zz_later.txt"), "cannot create zz_later.txt: %lu\n", GetLastError());

    if (Linked)
    {
        ok(FindEntry(L"link.lnk", &Data), "link.lnk is not enumerated: %lu\n", GetLastError());
        ok(Data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT, "link.lnk attributes 0x%lx\n", Data.dwFileAttributes);
        ok(Data.dwReserved0 == IO_REPARSE_TAG_SYMLINK, "link.lnk reparse tag 0x%lx\n", Data.dwReserved0);
    }
    if (Status == 0)
    {
        EaSize = EnumeratedEaSize(QueryDirectory, L"ea.txt");
        ok(EaSize != 0 && EaSize != 0xffffffffUL, "ea.txt EaSize %lu\n", EaSize);
    }
    if (Sparse)
    {
        ok(FindEntry(L"sparse.txt", &Data), "sparse.txt is not enumerated: %lu\n", GetLastError());
        ok(Data.dwFileAttributes & FILE_ATTRIBUTE_SPARSE_FILE, "sparse.txt attributes 0x%lx\n", Data.dwFileAttributes);
    }
    if (Compressed)
    {
        ok(FindEntry(L"packed.txt", &Data), "packed.txt is not enumerated: %lu\n", GetLastError());
        ok(Data.dwFileAttributes & FILE_ATTRIBUTE_COMPRESSED, "packed.txt attributes 0x%lx\n", Data.dwFileAttributes);
    }

    MakePath(Path, L"link.lnk"); DeleteFileW(Path);
    MakePath(Path, L"target.txt"); DeleteFileW(Path);
    MakePath(Path, L"ea.txt"); DeleteFileW(Path);
    MakePath(Path, L"sparse.txt"); DeleteFileW(Path);
    MakePath(Path, L"packed.txt"); DeleteFileW(Path);
    MakePath(Path, L"zz_later.txt"); DeleteFileW(Path);
    RemoveDirectoryW(s_Directory);
}
