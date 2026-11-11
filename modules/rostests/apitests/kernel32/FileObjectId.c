/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test the object identifier file system controls
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"
#include <winioctl.h>
#include <ndk/iofuncs.h>
#include <ndk/setypes.h>

static WCHAR TestDirectory[MAX_PATH];
static const BYTE ZeroGuid[16];

static
VOID
MakePath(
    _Out_writes_(MAX_PATH) PWSTR Path,
    _In_ PCWSTR Name)
{
    StringCchPrintfW(Path, MAX_PATH, L"%s\\%s", TestDirectory, Name);
}

static
HANDLE
OpenTestFile(
    _In_ PCWSTR Path,
    _In_ DWORD Access,
    _In_ DWORD Disposition)
{
    HANDLE File;

    File = CreateFileW(Path, Access, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, Disposition,
                       FILE_FLAG_BACKUP_SEMANTICS, NULL);
    ok(File != INVALID_HANDLE_VALUE, "CreateFile(%S) failed with %lu\n", Path, GetLastError());
    return File;
}

static
NTSTATUS
Fsctl(
    _In_ HANDLE File,
    _In_ ULONG Code,
    _In_reads_bytes_opt_(InputLength) PVOID Input,
    _In_ ULONG InputLength,
    _Out_writes_bytes_opt_(OutputLength) PVOID Output,
    _In_ ULONG OutputLength,
    _Out_opt_ PULONG_PTR Information)
{
    IO_STATUS_BLOCK IoStatusBlock;
    NTSTATUS Status;

    IoStatusBlock.Status = 0xdeadbeef;
    IoStatusBlock.Information = 0xdeadbeef;
    Status = NtFsControlFile(File, NULL, NULL, NULL, &IoStatusBlock, Code, Input, InputLength, Output, OutputLength);
    if (Information)
        *Information = IoStatusBlock.Information;
    return Status;
}

static
VOID
MakeId(
    _Out_ PFILE_OBJECTID_BUFFER Buffer,
    _In_ BYTE Seed)
{
    ULONG Index;

    for (Index = 0; Index < sizeof(Buffer->ObjectId); Index++)
        Buffer->ObjectId[Index] = (BYTE)(Seed + Index * 7 + GetCurrentProcessId());
    for (Index = 0; Index < sizeof(Buffer->ExtendedInfo); Index++)
        Buffer->ExtendedInfo[Index] = (BYTE)(Seed ^ Index);
}

static
BOOL
SetRestorePrivilege(
    _In_ BOOLEAN Enable)
{
    BOOLEAN Old;

    return NT_SUCCESS(RtlAdjustPrivilege(SE_RESTORE_PRIVILEGE, Enable, FALSE, &Old));
}

static
VOID
TestCreateOrGet(VOID)
{
    FILE_OBJECTID_BUFFER First;
    FILE_OBJECTID_BUFFER Second;
    FILE_OBJECTID_BUFFER Queried;
    BYTE VolumeId[64];
    IO_STATUS_BLOCK IoStatusBlock;
    WCHAR Path[MAX_PATH];
    ULONG_PTR Information;
    HANDLE File;
    NTSTATUS Status;

    MakePath(Path, L"create.bin");
    File = OpenTestFile(Path, GENERIC_READ | GENERIC_WRITE, CREATE_ALWAYS);
    if (File == INVALID_HANDLE_VALUE)
        return;

    Status = Fsctl(File, FSCTL_GET_OBJECT_ID, NULL, 0, &Queried, sizeof(Queried), &Information);
    ok(Status == STATUS_OBJECTID_NOT_FOUND, "FSCTL_GET_OBJECT_ID without an identifier returned 0x%lx\n", Status);
    Status = Fsctl(File, FSCTL_GET_OBJECT_ID, NULL, 0, &Queried, sizeof(Queried) - 1, NULL);
    ok(Status == STATUS_INVALID_PARAMETER, "FSCTL_GET_OBJECT_ID with a short buffer returned 0x%lx\n", Status);
    Status = Fsctl(File, FSCTL_CREATE_OR_GET_OBJECT_ID, NULL, 0, &First, sizeof(First) - 1, NULL);
    ok(Status == STATUS_INVALID_PARAMETER, "FSCTL_CREATE_OR_GET_OBJECT_ID with a short buffer returned 0x%lx\n", Status);

    memset(&First, 0xcc, sizeof(First));
    Status = Fsctl(File, FSCTL_CREATE_OR_GET_OBJECT_ID, NULL, 0, &First, sizeof(First), &Information);
    ok(Status == STATUS_SUCCESS, "FSCTL_CREATE_OR_GET_OBJECT_ID returned 0x%lx\n", Status);
    ok(Information == sizeof(First), "FSCTL_CREATE_OR_GET_OBJECT_ID returned %Iu bytes\n", Information);
    ok(memcmp(First.ObjectId, ZeroGuid, sizeof(ZeroGuid)) != 0, "The new object identifier is zero\n");
    ok(!memcmp(First.BirthObjectId, First.ObjectId, sizeof(First.ObjectId)), "BirthObjectId differs from ObjectId\n");
    ok(!memcmp(First.DomainId, ZeroGuid, sizeof(ZeroGuid)), "DomainId is not zero\n");
    Status = NtQueryVolumeInformationFile(File, &IoStatusBlock, VolumeId, sizeof(VolumeId), FileFsObjectIdInformation);
    if (NT_SUCCESS(Status))
        ok(!memcmp(First.BirthVolumeId, VolumeId, 16), "BirthVolumeId is not the volume's object identifier\n");
    else
        ok(!memcmp(First.BirthVolumeId, ZeroGuid, sizeof(ZeroGuid)), "BirthVolumeId set without a volume identifier\n");

    memset(&Second, 0, sizeof(Second));
    Status = Fsctl(File, FSCTL_CREATE_OR_GET_OBJECT_ID, NULL, 0, &Second, sizeof(Second), NULL);
    ok(Status == STATUS_SUCCESS, "A second FSCTL_CREATE_OR_GET_OBJECT_ID returned 0x%lx\n", Status);
    ok(!memcmp(&Second, &First, sizeof(First)), "A second FSCTL_CREATE_OR_GET_OBJECT_ID made another identifier\n");

    memset(&Queried, 0, sizeof(Queried));
    Status = Fsctl(File, FSCTL_GET_OBJECT_ID, NULL, 0, &Queried, sizeof(Queried) + 8, &Information);
    ok(Status == STATUS_SUCCESS, "FSCTL_GET_OBJECT_ID returned 0x%lx\n", Status);
    ok(Information == sizeof(Queried), "FSCTL_GET_OBJECT_ID returned %Iu bytes\n", Information);
    ok(!memcmp(&Queried, &First, sizeof(First)), "FSCTL_GET_OBJECT_ID differs from the created identifier\n");

    Status = Fsctl(File, FSCTL_DELETE_OBJECT_ID, NULL, 0, NULL, 0, NULL);
    ok(Status == STATUS_SUCCESS, "FSCTL_DELETE_OBJECT_ID returned 0x%lx\n", Status);
    Status = Fsctl(File, FSCTL_GET_OBJECT_ID, NULL, 0, &Queried, sizeof(Queried), NULL);
    ok(Status == STATUS_OBJECTID_NOT_FOUND, "FSCTL_GET_OBJECT_ID after the deletion returned 0x%lx\n", Status);
    Status = Fsctl(File, FSCTL_DELETE_OBJECT_ID, NULL, 0, NULL, 0, NULL);
    ok(Status == STATUS_SUCCESS, "FSCTL_DELETE_OBJECT_ID without an identifier returned 0x%lx\n", Status);

    CloseHandle(File);
    DeleteFileW(Path);

    File = OpenTestFile(TestDirectory, GENERIC_READ, OPEN_EXISTING);
    if (File != INVALID_HANDLE_VALUE)
    {
        Status = Fsctl(File, FSCTL_CREATE_OR_GET_OBJECT_ID, NULL, 0, &First, sizeof(First), NULL);
        ok(Status == STATUS_SUCCESS, "FSCTL_CREATE_OR_GET_OBJECT_ID on a directory returned 0x%lx\n", Status);
        Status = Fsctl(File, FSCTL_DELETE_OBJECT_ID, NULL, 0, NULL, 0, NULL);
        ok(Status == STATUS_SUCCESS, "FSCTL_DELETE_OBJECT_ID on a directory returned 0x%lx\n", Status);
        CloseHandle(File);
    }
}

static
VOID
CheckSetAndExtended(
    _In_ HANDLE File,
    _In_ HANDLE File2,
    _In_ PCWSTR Path)
{
    FILE_OBJECTID_BUFFER Id;
    FILE_OBJECTID_BUFFER Other;
    FILE_OBJECTID_BUFFER Queried;
    BYTE Extended[48];
    HANDLE Reader;
    NTSTATUS Status;
    ULONG Index;

    MakeId(&Id, 0x10);
    MakeId(&Other, 0x40);

    Status = Fsctl(File, FSCTL_SET_OBJECT_ID, &Id, sizeof(Id) - 1, NULL, 0, NULL);
    ok(Status == STATUS_INVALID_PARAMETER, "FSCTL_SET_OBJECT_ID with 63 bytes returned 0x%lx\n", Status);
    Status = Fsctl(File, FSCTL_SET_OBJECT_ID, &Id, sizeof(Id), NULL, 0, NULL);
    ok(Status == STATUS_SUCCESS, "FSCTL_SET_OBJECT_ID returned 0x%lx\n", Status);
    memset(&Queried, 0, sizeof(Queried));
    Status = Fsctl(File, FSCTL_GET_OBJECT_ID, NULL, 0, &Queried, sizeof(Queried), NULL);
    ok(Status == STATUS_SUCCESS && !memcmp(&Queried, &Id, sizeof(Id)), "The set identifier reads back as 0x%lx\n", Status);

    Status = Fsctl(File, FSCTL_SET_OBJECT_ID, &Other, sizeof(Other), NULL, 0, NULL);
    ok(Status == STATUS_OBJECT_NAME_COLLISION, "FSCTL_SET_OBJECT_ID over an identifier returned 0x%lx\n", Status);
    Status = Fsctl(File2, FSCTL_SET_OBJECT_ID, &Id, sizeof(Id), NULL, 0, NULL);
    ok(Status == STATUS_DUPLICATE_NAME, "FSCTL_SET_OBJECT_ID with an identifier in use returned 0x%lx\n", Status);

    for (Index = 0; Index < sizeof(Extended); Index++)
        Extended[Index] = (BYTE)(0xa0 + Index);
    Status = Fsctl(File, FSCTL_SET_OBJECT_ID_EXTENDED, &Other, sizeof(Other), NULL, 0, NULL);
    ok(Status == STATUS_INVALID_PARAMETER, "FSCTL_SET_OBJECT_ID_EXTENDED with 64 bytes returned 0x%lx\n", Status);
    Status = Fsctl(File, FSCTL_SET_OBJECT_ID_EXTENDED, Extended, sizeof(Extended), NULL, 0, NULL);
    ok(Status == STATUS_SUCCESS, "FSCTL_SET_OBJECT_ID_EXTENDED returned 0x%lx\n", Status);
    memset(&Queried, 0, sizeof(Queried));
    Status = Fsctl(File, FSCTL_GET_OBJECT_ID, NULL, 0, &Queried, sizeof(Queried), NULL);
    ok(Status == STATUS_SUCCESS, "FSCTL_GET_OBJECT_ID returned 0x%lx\n", Status);
    ok(!memcmp(Queried.ObjectId, Id.ObjectId, sizeof(Id.ObjectId)), "FSCTL_SET_OBJECT_ID_EXTENDED changed the identifier\n");
    ok(!memcmp(Queried.ExtendedInfo, Extended, sizeof(Extended)), "FSCTL_SET_OBJECT_ID_EXTENDED data not stored\n");
    Status = Fsctl(File2, FSCTL_SET_OBJECT_ID_EXTENDED, Extended, sizeof(Extended), NULL, 0, NULL);
    ok(Status == STATUS_OBJECTID_NOT_FOUND, "FSCTL_SET_OBJECT_ID_EXTENDED without an identifier returned 0x%lx\n", Status);

    Reader = OpenTestFile(Path, FILE_READ_ATTRIBUTES, OPEN_EXISTING);
    if (Reader != INVALID_HANDLE_VALUE)
    {
        Status = Fsctl(Reader, FSCTL_SET_OBJECT_ID_EXTENDED, Extended, sizeof(Extended), NULL, 0, NULL);
        ok(Status == STATUS_ACCESS_DENIED, "FSCTL_SET_OBJECT_ID_EXTENDED without write access returned 0x%lx\n", Status);
        CloseHandle(Reader);
    }

    Status = Fsctl(File, FSCTL_DELETE_OBJECT_ID, NULL, 0, NULL, 0, NULL);
    ok(Status == STATUS_SUCCESS, "FSCTL_DELETE_OBJECT_ID returned 0x%lx\n", Status);
    Status = Fsctl(File2, FSCTL_SET_OBJECT_ID, &Id, sizeof(Id), NULL, 0, NULL);
    ok(Status == STATUS_SUCCESS, "FSCTL_SET_OBJECT_ID with a released identifier returned 0x%lx\n", Status);
}

static
VOID
TestSetAndExtended(VOID)
{
    FILE_OBJECTID_BUFFER Id;
    WCHAR Path[MAX_PATH];
    WCHAR Path2[MAX_PATH];
    WCHAR Path3[MAX_PATH];
    HANDLE File;
    HANDLE File2;
    NTSTATUS Status;

    MakePath(Path, L"set.bin");
    MakePath(Path2, L"set2.bin");
    MakePath(Path3, L"set3.bin");
    MakeId(&Id, 0x10);

    if (!SetRestorePrivilege(FALSE))
    {
        skip("SeRestorePrivilege cannot be adjusted\n");
        return;
    }
    File = OpenTestFile(Path, GENERIC_READ | GENERIC_WRITE, CREATE_ALWAYS);
    if (File == INVALID_HANDLE_VALUE)
        return;
    Status = Fsctl(File, FSCTL_SET_OBJECT_ID, &Id, sizeof(Id), NULL, 0, NULL);
    ok(Status == STATUS_ACCESS_DENIED, "FSCTL_SET_OBJECT_ID without restore access returned 0x%lx\n", Status);
    CloseHandle(File);

    ok(SetRestorePrivilege(TRUE), "Enabling SeRestorePrivilege failed\n");
    File = OpenTestFile(Path, GENERIC_READ | GENERIC_WRITE, OPEN_EXISTING);
    File2 = OpenTestFile(Path2, GENERIC_READ | GENERIC_WRITE, CREATE_ALWAYS);
    if (File != INVALID_HANDLE_VALUE && File2 != INVALID_HANDLE_VALUE)
        CheckSetAndExtended(File, File2, Path);
    if (File != INVALID_HANDLE_VALUE)
        CloseHandle(File);
    if (File2 != INVALID_HANDLE_VALUE)
        CloseHandle(File2);

    ok(DeleteFileW(Path2), "DeleteFile(%S) failed with %lu\n", Path2, GetLastError());
    File2 = OpenTestFile(Path3, GENERIC_READ | GENERIC_WRITE, CREATE_ALWAYS);
    if (File2 != INVALID_HANDLE_VALUE)
    {
        Status = Fsctl(File2, FSCTL_SET_OBJECT_ID, &Id, sizeof(Id), NULL, 0, NULL);
        ok(Status == STATUS_SUCCESS, "The identifier of a deleted file stayed in use: 0x%lx\n", Status);
        Fsctl(File2, FSCTL_DELETE_OBJECT_ID, NULL, 0, NULL, 0, NULL);
        CloseHandle(File2);
    }

    SetRestorePrivilege(FALSE);
    DeleteFileW(Path3);
    DeleteFileW(Path);
}

START_TEST(FileObjectId)
{
    WCHAR TempPath[MAX_PATH];
    DWORD Length;

    Length = GetTempPathW(RTL_NUMBER_OF(TempPath), TempPath);
    ok(Length != 0 && Length < RTL_NUMBER_OF(TempPath), "GetTempPath failed with %lu\n", GetLastError());
    StringCchPrintfW(TestDirectory, RTL_NUMBER_OF(TestDirectory), L"%sobjid%lx", TempPath, GetCurrentProcessId());
    ok(CreateDirectoryW(TestDirectory, NULL), "CreateDirectory(%S) failed with %lu\n", TestDirectory, GetLastError());

    TestCreateOrGet();
    TestSetAndExtended();

    ok(RemoveDirectoryW(TestDirectory), "RemoveDirectory(%S) failed with %lu\n", TestDirectory, GetLastError());
}
