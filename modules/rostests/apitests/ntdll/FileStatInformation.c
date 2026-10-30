/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests for FileStatInformation and FileStatLxInformation
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"

/* Layouts and values from the WDK 10.0.26100.0 ntifs.h */
typedef struct _TEST_FILE_STAT_LX_INFORMATION
{
    LARGE_INTEGER FileId;
    LARGE_INTEGER CreationTime;
    LARGE_INTEGER LastAccessTime;
    LARGE_INTEGER LastWriteTime;
    LARGE_INTEGER ChangeTime;
    LARGE_INTEGER AllocationSize;
    LARGE_INTEGER EndOfFile;
    ULONG FileAttributes;
    ULONG ReparseTag;
    ULONG NumberOfLinks;
    ACCESS_MASK EffectiveAccess;
    ULONG LxFlags;
    ULONG LxUid;
    ULONG LxGid;
    ULONG LxMode;
    ULONG LxDeviceIdMajor;
    ULONG LxDeviceIdMinor;
} TEST_FILE_STAT_LX_INFORMATION;

#define TEST_FILE_STAT_SIZE         FIELD_OFFSET(TEST_FILE_STAT_LX_INFORMATION, LxFlags)
#define TestFileStatInformation     ((FILE_INFORMATION_CLASS)68)
#define TestFileStatLxInformation   ((FILE_INFORMATION_CLASS)70)
#define TEST_LX_HAS_UID             0x1
#define TEST_LX_HAS_GID             0x2
#define TEST_LX_HAS_MODE            0x4
#define TEST_LX_HAS_DEVICE_ID       0x8

typedef NTSTATUS (NTAPI *PNT_QUERY_INFORMATION_BY_NAME)(POBJECT_ATTRIBUTES, PIO_STATUS_BLOCK, PVOID, ULONG,
                                                        FILE_INFORMATION_CLASS);

static ULONG AppendEa(PUCHAR Buffer, ULONG Offset, PFILE_FULL_EA_INFORMATION *Previous, PCSTR Name,
                      const VOID *Value, USHORT ValueLength)
{
    PFILE_FULL_EA_INFORMATION Entry = (PFILE_FULL_EA_INFORMATION)(Buffer + Offset);
    UCHAR NameLength = (UCHAR)strlen(Name);

    Entry->NextEntryOffset = 0;
    Entry->Flags = 0;
    Entry->EaNameLength = NameLength;
    Entry->EaValueLength = ValueLength;
    memcpy(Entry->EaName, Name, NameLength + 1);
    memcpy(Entry->EaName + NameLength + 1, Value, ValueLength);
    if (*Previous)
        (*Previous)->NextEntryOffset = (ULONG)((PUCHAR)Entry - (PUCHAR)*Previous);
    *Previous = Entry;
    return Offset + ((FIELD_OFFSET(FILE_FULL_EA_INFORMATION, EaName) + NameLength + 1 + ValueLength + 3) & ~3UL);
}

static VOID CheckCommonFields(HANDLE Handle, const TEST_FILE_STAT_LX_INFORMATION *Stat, PCSTR What)
{
    FILE_BASIC_INFORMATION Basic;
    FILE_STANDARD_INFORMATION Standard;
    FILE_INTERNAL_INFORMATION Internal;
    IO_STATUS_BLOCK Iosb;

    ok_ntstatus(NtQueryInformationFile(Handle, &Iosb, &Basic, sizeof(Basic), FileBasicInformation), STATUS_SUCCESS);
    ok_ntstatus(NtQueryInformationFile(Handle, &Iosb, &Standard, sizeof(Standard), FileStandardInformation), STATUS_SUCCESS);
    ok_ntstatus(NtQueryInformationFile(Handle, &Iosb, &Internal, sizeof(Internal), FileInternalInformation), STATUS_SUCCESS);

    ok(Stat->FileId.QuadPart == Internal.IndexNumber.QuadPart, "%s: FileId %I64x, expected %I64x\n", What,
       Stat->FileId.QuadPart, Internal.IndexNumber.QuadPart);
    ok(Stat->CreationTime.QuadPart == Basic.CreationTime.QuadPart, "%s: CreationTime differs\n", What);
    ok(Stat->LastAccessTime.QuadPart == Basic.LastAccessTime.QuadPart, "%s: LastAccessTime differs\n", What);
    ok(Stat->LastWriteTime.QuadPart == Basic.LastWriteTime.QuadPart, "%s: LastWriteTime differs\n", What);
    ok(Stat->ChangeTime.QuadPart == Basic.ChangeTime.QuadPart, "%s: ChangeTime differs\n", What);
    ok(Stat->AllocationSize.QuadPart == Standard.AllocationSize.QuadPart, "%s: AllocationSize %I64d, expected %I64d\n",
       What, Stat->AllocationSize.QuadPart, Standard.AllocationSize.QuadPart);
    ok(Stat->EndOfFile.QuadPart == Standard.EndOfFile.QuadPart, "%s: EndOfFile %I64d, expected %I64d\n", What,
       Stat->EndOfFile.QuadPart, Standard.EndOfFile.QuadPart);
    ok(Stat->FileAttributes == Basic.FileAttributes, "%s: FileAttributes %lx, expected %lx\n", What,
       Stat->FileAttributes, Basic.FileAttributes);
    ok(Stat->ReparseTag == 0, "%s: ReparseTag %lx\n", What, Stat->ReparseTag);
    ok(Stat->NumberOfLinks == Standard.NumberOfLinks, "%s: NumberOfLinks %lu, expected %lu\n", What,
       Stat->NumberOfLinks, Standard.NumberOfLinks);
    todo_ros ok(Stat->EffectiveAccess == FILE_ALL_ACCESS, "%s: EffectiveAccess %lx\n", What, Stat->EffectiveAccess);
}

static HANDLE OpenPath(PCWSTR Path, ACCESS_MASK Access, ULONG Options)
{
    UNICODE_STRING Name;
    OBJECT_ATTRIBUTES Attributes;
    IO_STATUS_BLOCK Iosb;
    HANDLE Handle = NULL;
    NTSTATUS Status;

    if (!RtlDosPathNameToNtPathName_U(Path, &Name, NULL, NULL))
        return NULL;
    InitializeObjectAttributes(&Attributes, &Name, OBJ_CASE_INSENSITIVE, NULL, NULL);
    Status = NtOpenFile(&Handle, Access | SYNCHRONIZE, &Attributes, &Iosb, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                        Options | FILE_SYNCHRONOUS_IO_NONALERT);
    RtlFreeUnicodeString(&Name);
    return NT_SUCCESS(Status) ? Handle : NULL;
}

START_TEST(FileStatInformation)
{
    PNT_QUERY_INFORMATION_BY_NAME QueryByName =
        (PNT_QUERY_INFORMATION_BY_NAME)GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "NtQueryInformationByName");
    WCHAR Directory[MAX_PATH], Path[MAX_PATH];
    TEST_FILE_STAT_LX_INFORMATION Stat;
    UCHAR EaBuffer[256];
    PFILE_FULL_EA_INFORMATION Previous = NULL;
    ULONG EaLength = 0, Uid = 1000, Gid = 1001, Mode = 0100644, Device[2] = { 5, 7 };
    UNICODE_STRING NtName;
    OBJECT_ATTRIBUTES Attributes;
    IO_STATUS_BLOCK Iosb;
    HANDLE File, Handle;
    DWORD Written;
    NTSTATUS Status;

    GetTempPathW(MAX_PATH, Directory);
    GetTempFileNameW(Directory, L"fst", 0, Path);
    File = CreateFileW(Path, GENERIC_READ | GENERIC_WRITE | FILE_READ_EA | FILE_WRITE_EA, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                       NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    ok(File != INVALID_HANDLE_VALUE, "CreateFileW failed with %lu\n", GetLastError());
    if (File == INVALID_HANDLE_VALUE)
        return;
    ok(WriteFile(File, "0123456789", 10, &Written, NULL), "WriteFile failed with %lu\n", GetLastError());

    memset(&Stat, 0xcc, sizeof(Stat));
    Iosb.Information = 0xdeadbeef;
    Status = NtQueryInformationFile(File, &Iosb, &Stat, TEST_FILE_STAT_SIZE, TestFileStatInformation);
    ok_ntstatus(Status, STATUS_SUCCESS);
    ok_size_t(Iosb.Information, TEST_FILE_STAT_SIZE);
    if (NT_SUCCESS(Status))
        CheckCommonFields(File, &Stat, "stat");
    ok(Stat.LxFlags == 0xcccccccc, "FileStatInformation wrote past its structure\n");

    Status = NtQueryInformationFile(File, &Iosb, &Stat, TEST_FILE_STAT_SIZE - 1, TestFileStatInformation);
    ok_ntstatus(Status, STATUS_INFO_LENGTH_MISMATCH);
    Status = NtQueryInformationFile(File, &Iosb, &Stat, sizeof(Stat) - 1, TestFileStatLxInformation);
    ok_ntstatus(Status, STATUS_INFO_LENGTH_MISMATCH);

    memset(&Stat, 0xcc, sizeof(Stat));
    Iosb.Information = 0xdeadbeef;
    Status = NtQueryInformationFile(File, &Iosb, &Stat, sizeof(Stat), TestFileStatLxInformation);
    ok_ntstatus(Status, STATUS_SUCCESS);
    ok_size_t(Iosb.Information, sizeof(Stat));
    if (NT_SUCCESS(Status))
        CheckCommonFields(File, &Stat, "lx without metadata");
    ok_hex(Stat.LxFlags, 0);
    ok_hex(Stat.LxUid, 0);
    ok_hex(Stat.LxGid, 0);
    ok_hex(Stat.LxMode, 0);
    ok_hex(Stat.LxDeviceIdMajor, 0);
    ok_hex(Stat.LxDeviceIdMinor, 0);

    EaLength = AppendEa(EaBuffer, EaLength, &Previous, "$LXMOD", &Mode, sizeof(Mode));
    ok_ntstatus(NtSetEaFile(File, &Iosb, EaBuffer, EaLength), STATUS_SUCCESS);
    Status = NtQueryInformationFile(File, &Iosb, &Stat, sizeof(Stat), TestFileStatLxInformation);
    ok_ntstatus(Status, STATUS_SUCCESS);
    ok_hex(Stat.LxFlags, TEST_LX_HAS_MODE);
    ok_hex(Stat.LxMode, Mode);
    ok_hex(Stat.LxUid, 0);

    EaLength = 0;
    Previous = NULL;
    EaLength = AppendEa(EaBuffer, EaLength, &Previous, "$LXUID", &Uid, sizeof(Uid));
    EaLength = AppendEa(EaBuffer, EaLength, &Previous, "$LXGID", &Gid, sizeof(Gid));
    EaLength = AppendEa(EaBuffer, EaLength, &Previous, "$LXDEV", Device, sizeof(Device));
    ok_ntstatus(NtSetEaFile(File, &Iosb, EaBuffer, EaLength), STATUS_SUCCESS);
    Status = NtQueryInformationFile(File, &Iosb, &Stat, sizeof(Stat), TestFileStatLxInformation);
    ok_ntstatus(Status, STATUS_SUCCESS);
    ok_hex(Stat.LxFlags, TEST_LX_HAS_UID | TEST_LX_HAS_GID | TEST_LX_HAS_MODE | TEST_LX_HAS_DEVICE_ID);
    ok_hex(Stat.LxUid, Uid);
    ok_hex(Stat.LxGid, Gid);
    ok_hex(Stat.LxMode, Mode);
    ok_hex(Stat.LxDeviceIdMajor, Device[0]);
    ok_hex(Stat.LxDeviceIdMinor, Device[1]);

    Handle = OpenPath(Path, FILE_READ_ATTRIBUTES, FILE_NON_DIRECTORY_FILE);
    ok(Handle != NULL, "Opening with FILE_READ_ATTRIBUTES failed\n");
    if (Handle)
    {
        Status = NtQueryInformationFile(Handle, &Iosb, &Stat, TEST_FILE_STAT_SIZE, TestFileStatInformation);
        ok_ntstatus(Status, STATUS_SUCCESS);
        if (NT_SUCCESS(Status))
            CheckCommonFields(Handle, &Stat, "read attributes");
        Status = NtQueryInformationFile(Handle, &Iosb, &Stat, sizeof(Stat), TestFileStatLxInformation);
        ok_ntstatus(Status, STATUS_ACCESS_DENIED);
        NtClose(Handle);
    }

    Handle = OpenPath(Path, FILE_READ_EA, FILE_NON_DIRECTORY_FILE);
    ok(Handle != NULL, "Opening with FILE_READ_EA failed\n");
    if (Handle)
    {
        Status = NtQueryInformationFile(Handle, &Iosb, &Stat, TEST_FILE_STAT_SIZE, TestFileStatInformation);
        ok_ntstatus(Status, STATUS_ACCESS_DENIED);
        Status = NtQueryInformationFile(Handle, &Iosb, &Stat, sizeof(Stat), TestFileStatLxInformation);
        ok_ntstatus(Status, STATUS_ACCESS_DENIED);
        NtClose(Handle);
    }

    ok(QueryByName != NULL, "NtQueryInformationByName is missing\n");
    if (QueryByName && RtlDosPathNameToNtPathName_U(Path, &NtName, NULL, NULL))
    {
        InitializeObjectAttributes(&Attributes, &NtName, OBJ_CASE_INSENSITIVE, NULL, NULL);
        memset(&Stat, 0xcc, sizeof(Stat));
        Status = QueryByName(&Attributes, &Iosb, &Stat, sizeof(Stat), TestFileStatLxInformation);
        ok_ntstatus(Status, STATUS_SUCCESS);
        ok_size_t(Iosb.Information, sizeof(Stat));
        ok_hex(Stat.LxFlags, TEST_LX_HAS_UID | TEST_LX_HAS_GID | TEST_LX_HAS_MODE | TEST_LX_HAS_DEVICE_ID);
        ok_hex(Stat.LxUid, Uid);
        ok(Stat.EndOfFile.QuadPart == 10, "By name: EndOfFile %I64d\n", Stat.EndOfFile.QuadPart);
        todo_ros ok_hex(Stat.EffectiveAccess, 0);

        memset(&Stat, 0xcc, sizeof(Stat));
        Status = QueryByName(&Attributes, &Iosb, &Stat, TEST_FILE_STAT_SIZE, TestFileStatInformation);
        ok_ntstatus(Status, STATUS_SUCCESS);
        ok_size_t(Iosb.Information, TEST_FILE_STAT_SIZE);
        ok(Stat.EndOfFile.QuadPart == 10, "By name: EndOfFile %I64d\n", Stat.EndOfFile.QuadPart);
        todo_ros ok_hex(Stat.EffectiveAccess, FILE_ALL_ACCESS);
        RtlFreeUnicodeString(&NtName);
    }

    Handle = OpenPath(Directory, FILE_READ_ATTRIBUTES | FILE_READ_EA, FILE_DIRECTORY_FILE);
    ok(Handle != NULL, "Opening the directory failed\n");
    if (Handle)
    {
        Status = NtQueryInformationFile(Handle, &Iosb, &Stat, sizeof(Stat), TestFileStatLxInformation);
        ok_ntstatus(Status, STATUS_SUCCESS);
        if (NT_SUCCESS(Status))
            CheckCommonFields(Handle, &Stat, "directory");
        ok(Stat.FileAttributes & FILE_ATTRIBUTE_DIRECTORY, "Directory attributes %lx\n", Stat.FileAttributes);
        NtClose(Handle);
    }

    CloseHandle(File);
    DeleteFileW(Path);
}
