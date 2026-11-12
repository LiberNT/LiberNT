/*
 * PROJECT:     ReactOS Kernel
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     Test for NtQueryInformationFile
 * COPYRIGHT:   Copyright 2019 Thomas Faber (thomas.faber@reactos.org)
 */

#include "precomp.h"

#define ntv6(x) (LOBYTE(LOWORD(GetVersion())) >= 6 ? (x) : 0)

typedef struct _TEST_FILE_ALL_INFORMATION
{
    FILE_BASIC_INFORMATION BasicInformation;
    FILE_STANDARD_INFORMATION StandardInformation;
    FILE_INTERNAL_INFORMATION InternalInformation;
    FILE_EA_INFORMATION EaInformation;
    FILE_ACCESS_INFORMATION AccessInformation;
    FILE_POSITION_INFORMATION PositionInformation;
    FILE_MODE_INFORMATION ModeInformation;
    ULONG AlignmentRequirement;
    FILE_NAME_INFORMATION NameInformation;
} TEST_FILE_ALL_INFORMATION;

static void TestAllInformationLength(HANDLE hFile, BOOLEAN Ntfs)
{
    NTSTATUS Status;
    IO_STATUS_BLOCK IoStatusBlock;
    ULONG64 Storage[(sizeof(TEST_FILE_ALL_INFORMATION) + MAX_PATH * sizeof(WCHAR)) / sizeof(ULONG64)];

    Status = NtQueryInformationFile(hFile,
                                    &IoStatusBlock,
                                    Storage,
                                    FIELD_OFFSET(TEST_FILE_ALL_INFORMATION, NameInformation.FileName) - 1,
                                    FileAllInformation);
    ok_hex(Status, STATUS_INFO_LENGTH_MISMATCH);
    if (Ntfs)
    {
        Status = NtQueryInformationFile(hFile,
                                        &IoStatusBlock,
                                        Storage,
                                        sizeof(TEST_FILE_ALL_INFORMATION) - 1,
                                        FileAllInformation);
        ok_hex(Status, STATUS_INFO_LENGTH_MISMATCH);
    }
    Status = NtQueryInformationFile(hFile,
                                    &IoStatusBlock,
                                    Storage,
                                    sizeof(Storage),
                                    FileAllInformation);
    ok_hex(Status, STATUS_SUCCESS);
}

static BOOLEAN IsOnNtfs(PCSTR Path)
{
    CHAR Root[4], FileSystem[MAX_PATH];

    StringCchCopyNA(Root, ARRAYSIZE(Root), Path, 3);
    if (!GetVolumeInformationA(Root, NULL, 0, NULL, NULL, NULL, FileSystem, ARRAYSIZE(FileSystem)))
        return FALSE;
    return !strcmp(FileSystem, "NTFS");
}

static HANDLE OpenFatVolumeRoot(void)
{
    WCHAR Drives[256], FileSystem[MAX_PATH];
    LPWSTR Drive;

    if (!GetLogicalDriveStringsW(ARRAYSIZE(Drives), Drives))
        return INVALID_HANDLE_VALUE;
    for (Drive = Drives; *Drive; Drive += wcslen(Drive) + 1)
    {
        if (GetDriveTypeW(Drive) != DRIVE_FIXED && GetDriveTypeW(Drive) != DRIVE_REMOVABLE)
            continue;
        if (!GetVolumeInformationW(Drive, NULL, 0, NULL, NULL, NULL, FileSystem, ARRAYSIZE(FileSystem)))
            continue;
        if (!wcsncmp(FileSystem, L"FAT", 3))
            return CreateFileW(Drive, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                               OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
    }
    return INVALID_HANDLE_VALUE;
}

START_TEST(NtQueryInformationFile)
{
    NTSTATUS Status;

    Status = NtQueryInformationFile(NULL, NULL, NULL, 0, 0);
    ok(Status == STATUS_INVALID_INFO_CLASS ||
       ntv6(Status == STATUS_NOT_IMPLEMENTED), "Status = %lx\n", Status);

    Status = NtQueryInformationFile(NULL, NULL, NULL, 0, 0x80000000);
    ok(Status == STATUS_INVALID_INFO_CLASS ||
       ntv6(Status == STATUS_NOT_IMPLEMENTED), "Status = %lx\n", Status);

    /* Get the full path of the current executable */
    CHAR Path[MAX_PATH];
    DWORD Length = GetModuleFileNameA(NULL, Path, _countof(Path));
    ok(Length != 0, "GetModuleFileNameA failed\n");
    if (Length == 0)
        return;

    /* Open the file */
    HANDLE hFile = CreateFileA(Path,
                               GENERIC_READ,
                               FILE_SHARE_READ,
                               NULL,
                               OPEN_EXISTING,
                               FILE_ATTRIBUTE_NORMAL,
                               NULL);
    ok(hFile != INVALID_HANDLE_VALUE, "CreateFileA failed\n");
    if (hFile == INVALID_HANDLE_VALUE)
        return;

    /* Query FileEndOfFileInformation */
    FILE_END_OF_FILE_INFORMATION EndOfFileInformation;
    EndOfFileInformation.EndOfFile.QuadPart = 0xdeaddead;
    Status = NtQueryInformationFile(hFile,
                                    NULL,
                                    &EndOfFileInformation,
                                    sizeof(EndOfFileInformation),
                                    FileEndOfFileInformation);
    ok_hex(Status, STATUS_INVALID_INFO_CLASS);
    ok(EndOfFileInformation.EndOfFile.QuadPart == 0xdeaddead, "EndOfFile is modified\n");

    FILE_BASIC_INFORMATION BasicInformation;
    IO_STATUS_BLOCK IoStatusBlock;
    ULONG64 Storage[8];
    Status = NtQueryInformationFile(hFile,
                                    &IoStatusBlock,
                                    (PUCHAR)Storage + 1,
                                    sizeof(FILE_BASIC_INFORMATION),
                                    FileBasicInformation);
    ok_hex(Status, STATUS_DATATYPE_MISALIGNMENT);
    Status = NtQueryInformationFile(hFile,
                                    &IoStatusBlock,
                                    (PUCHAR)Storage + 1,
                                    sizeof(FILE_STANDARD_INFORMATION),
                                    FileStandardInformation);
    ok_hex(Status, STATUS_DATATYPE_MISALIGNMENT);
    Status = NtQueryInformationFile(hFile,
                                    &IoStatusBlock,
                                    &BasicInformation,
                                    sizeof(BasicInformation),
                                    FileBasicInformation);
    ok_hex(Status, STATUS_SUCCESS);

    TestAllInformationLength(hFile, IsOnNtfs(Path));
    CloseHandle(hFile);

    hFile = OpenFatVolumeRoot();
    if (hFile == INVALID_HANDLE_VALUE)
    {
        skip("No FAT volume\n");
        return;
    }
    TestAllInformationLength(hFile, FALSE);
    CloseHandle(hFile);
}
