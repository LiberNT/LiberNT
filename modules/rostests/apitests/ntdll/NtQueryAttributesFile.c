/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests for NtQueryAttributesFile and NtQueryFullAttributesFile
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"

#define QUERY_FILE_BYTES 4100
#define QUERY_LEAK_ROUNDS 4096
#define QUERY_DIRECTORY_FILES 64

static
BOOLEAN
QueryTagOutstanding(
    _In_ ULONG Tag,
    _Out_ PLONGLONG Outstanding)
{
    PSYSTEM_POOLTAG_INFORMATION Info;
    ULONG Length = 0x20000;
    NTSTATUS Status;
    ULONG Index;

    *Outstanding = 0;
    for (;;)
    {
        Info = RtlAllocateHeap(RtlGetProcessHeap(), 0, Length);
        if (Info == NULL)
            return FALSE;
        Status = NtQuerySystemInformation(SystemPoolTagInformation, Info, Length, NULL);
        if (Status != STATUS_INFO_LENGTH_MISMATCH)
            break;
        RtlFreeHeap(RtlGetProcessHeap(), 0, Info);
        Length *= 2;
    }

    if (!NT_SUCCESS(Status))
    {
        RtlFreeHeap(RtlGetProcessHeap(), 0, Info);
        return FALSE;
    }

    for (Index = 0; Index < Info->Count; Index++)
    {
        PSYSTEM_POOLTAG Entry = &Info->TagInfo[Index];

        if (Entry->TagUlong == Tag)
        {
            *Outstanding = (LONGLONG)Entry->PagedAllocs + Entry->NonPagedAllocs -
                           Entry->PagedFrees - Entry->NonPagedFrees;
            break;
        }
    }

    RtlFreeHeap(RtlGetProcessHeap(), 0, Info);
    return TRUE;
}

static
BOOLEAN
MakeNtName(
    _In_ PCWSTR DosPath,
    _Out_ PUNICODE_STRING NtName)
{
    return RtlDosPathNameToNtPathName_U(DosPath, NtName, NULL, NULL);
}

static
NTSTATUS
QueryFull(
    _In_ PCWSTR DosPath,
    _Out_ PFILE_NETWORK_OPEN_INFORMATION Info)
{
    OBJECT_ATTRIBUTES Attributes;
    UNICODE_STRING Name;
    NTSTATUS Status;

    RtlFillMemory(Info, sizeof(*Info), 0xCC);
    if (!MakeNtName(DosPath, &Name))
        return STATUS_OBJECT_PATH_SYNTAX_BAD;
    InitializeObjectAttributes(&Attributes, &Name, OBJ_CASE_INSENSITIVE, NULL, NULL);
    Status = NtQueryFullAttributesFile(&Attributes, Info);
    RtlFreeUnicodeString(&Name);
    return Status;
}

static
NTSTATUS
QueryBasic(
    _In_ PCWSTR DosPath,
    _Out_ PFILE_BASIC_INFORMATION Info)
{
    OBJECT_ATTRIBUTES Attributes;
    UNICODE_STRING Name;
    NTSTATUS Status;

    RtlFillMemory(Info, sizeof(*Info), 0xCC);
    if (!MakeNtName(DosPath, &Name))
        return STATUS_OBJECT_PATH_SYNTAX_BAD;
    InitializeObjectAttributes(&Attributes, &Name, OBJ_CASE_INSENSITIVE, NULL, NULL);
    Status = NtQueryAttributesFile(&Attributes, Info);
    RtlFreeUnicodeString(&Name);
    return Status;
}

static
VOID
TestExistingFile(
    _In_ PCWSTR Path)
{
    FILE_NETWORK_OPEN_INFORMATION Full, ByHandle;
    FILE_BASIC_INFORMATION Basic;
    IO_STATUS_BLOCK IoStatus;
    NTSTATUS Status;
    HANDLE File;

    File = CreateFileW(Path, FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                       NULL, OPEN_EXISTING, 0, NULL);
    ok(File != INVALID_HANDLE_VALUE, "CreateFileW failed with %lu\n", GetLastError());
    if (File == INVALID_HANDLE_VALUE)
        return;
    Status = NtQueryInformationFile(File, &IoStatus, &ByHandle, sizeof(ByHandle), FileNetworkOpenInformation);
    ok_ntstatus(Status, STATUS_SUCCESS);
    CloseHandle(File);

    Status = QueryFull(Path, &Full);
    ok_ntstatus(Status, STATUS_SUCCESS);
    ok(Full.EndOfFile.QuadPart == QUERY_FILE_BYTES, "EndOfFile %I64d\n", Full.EndOfFile.QuadPart);
    ok(Full.AllocationSize.QuadPart >= QUERY_FILE_BYTES, "AllocationSize %I64d\n", Full.AllocationSize.QuadPart);
    ok(Full.AllocationSize.QuadPart == ByHandle.AllocationSize.QuadPart, "AllocationSize %I64d vs %I64d\n",
       Full.AllocationSize.QuadPart, ByHandle.AllocationSize.QuadPart);
    ok(Full.EndOfFile.QuadPart == ByHandle.EndOfFile.QuadPart, "EndOfFile %I64d vs %I64d\n",
       Full.EndOfFile.QuadPart, ByHandle.EndOfFile.QuadPart);
    ok(Full.CreationTime.QuadPart == ByHandle.CreationTime.QuadPart, "CreationTime differs\n");
    ok(Full.LastWriteTime.QuadPart == ByHandle.LastWriteTime.QuadPart, "LastWriteTime differs\n");
    ok(Full.ChangeTime.QuadPart == ByHandle.ChangeTime.QuadPart, "ChangeTime differs\n");
    ok(Full.LastAccessTime.QuadPart != 0, "LastAccessTime is zero\n");
    ok(Full.FileAttributes == ByHandle.FileAttributes, "FileAttributes 0x%lx vs 0x%lx\n",
       Full.FileAttributes, ByHandle.FileAttributes);
    ok(Full.FileAttributes == (FILE_ATTRIBUTE_ARCHIVE | FILE_ATTRIBUTE_READONLY), "FileAttributes 0x%lx\n",
       Full.FileAttributes);

    Status = QueryBasic(Path, &Basic);
    ok_ntstatus(Status, STATUS_SUCCESS);
    ok(Basic.CreationTime.QuadPart == Full.CreationTime.QuadPart, "CreationTime differs\n");
    ok(Basic.LastWriteTime.QuadPart == Full.LastWriteTime.QuadPart, "LastWriteTime differs\n");
    ok(Basic.ChangeTime.QuadPart == Full.ChangeTime.QuadPart, "ChangeTime differs\n");
    ok(Basic.FileAttributes == Full.FileAttributes, "FileAttributes 0x%lx vs 0x%lx\n",
       Basic.FileAttributes, Full.FileAttributes);
}

static
VOID
TestDirectory(
    _In_ PCWSTR Parent)
{
    FILE_NETWORK_OPEN_INFORMATION Full, ByHandle;
    FILE_STANDARD_INFORMATION Standard;
    FILE_BASIC_INFORMATION Basic;
    IO_STATUS_BLOCK IoStatus;
    WCHAR Directory[MAX_PATH];
    WCHAR Path[MAX_PATH];
    NTSTATUS Status;
    HANDLE Handle;
    ULONG Index;

    StringCchPrintfW(Directory, RTL_NUMBER_OF(Directory), L"%s\\qattr-dir", Parent);
    CreateDirectoryW(Directory, NULL);
    for (Index = 0; Index < QUERY_DIRECTORY_FILES; Index++)
    {
        StringCchPrintfW(Path, RTL_NUMBER_OF(Path), L"%s\\directory-entry-with-a-long-name-%04lu.tmp", Directory, Index);
        Handle = CreateFileW(Path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        ok(Handle != INVALID_HANDLE_VALUE, "CreateFileW failed with %lu\n", GetLastError());
        if (Handle != INVALID_HANDLE_VALUE)
            CloseHandle(Handle);
    }

    Handle = CreateFileW(Directory, FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                         NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
    ok(Handle != INVALID_HANDLE_VALUE, "CreateFileW failed with %lu\n", GetLastError());
    if (Handle != INVALID_HANDLE_VALUE)
    {
        Status = NtQueryInformationFile(Handle, &IoStatus, &ByHandle, sizeof(ByHandle), FileNetworkOpenInformation);
        ok_ntstatus(Status, STATUS_SUCCESS);
        Status = NtQueryInformationFile(Handle, &IoStatus, &Standard, sizeof(Standard), FileStandardInformation);
        ok_ntstatus(Status, STATUS_SUCCESS);
        CloseHandle(Handle);
        ok(Standard.Directory == TRUE, "Directory %u\n", Standard.Directory);
        ok(Standard.EndOfFile.QuadPart == ByHandle.EndOfFile.QuadPart, "EndOfFile %I64d vs %I64d\n",
           Standard.EndOfFile.QuadPart, ByHandle.EndOfFile.QuadPart);
        ok(Standard.AllocationSize.QuadPart == ByHandle.AllocationSize.QuadPart, "AllocationSize %I64d vs %I64d\n",
           Standard.AllocationSize.QuadPart, ByHandle.AllocationSize.QuadPart);

        Status = QueryFull(Directory, &Full);
        ok_ntstatus(Status, STATUS_SUCCESS);
        ok(Full.FileAttributes == FILE_ATTRIBUTE_DIRECTORY, "FileAttributes 0x%lx\n", Full.FileAttributes);
        ok(Full.FileAttributes == ByHandle.FileAttributes, "FileAttributes 0x%lx vs 0x%lx\n",
           Full.FileAttributes, ByHandle.FileAttributes);
        ok(Full.EndOfFile.QuadPart == ByHandle.EndOfFile.QuadPart, "EndOfFile %I64d vs %I64d\n",
           Full.EndOfFile.QuadPart, ByHandle.EndOfFile.QuadPart);
        ok(Full.AllocationSize.QuadPart == ByHandle.AllocationSize.QuadPart, "AllocationSize %I64d vs %I64d\n",
           Full.AllocationSize.QuadPart, ByHandle.AllocationSize.QuadPart);
        ok(Full.CreationTime.QuadPart == ByHandle.CreationTime.QuadPart, "CreationTime differs\n");
        ok(Full.LastWriteTime.QuadPart == ByHandle.LastWriteTime.QuadPart, "LastWriteTime differs\n");
        ok(Full.EndOfFile.QuadPart == Full.AllocationSize.QuadPart, "EndOfFile %I64d AllocationSize %I64d\n",
           Full.EndOfFile.QuadPart, Full.AllocationSize.QuadPart);
        ok(Full.AllocationSize.QuadPart != 0 && (Full.AllocationSize.QuadPart % 4096) == 0,
           "AllocationSize %I64d\n", Full.AllocationSize.QuadPart);

        Status = QueryBasic(Directory, &Basic);
        ok_ntstatus(Status, STATUS_SUCCESS);
        ok(Basic.FileAttributes == Full.FileAttributes, "FileAttributes 0x%lx vs 0x%lx\n",
           Basic.FileAttributes, Full.FileAttributes);
    }

    for (Index = 0; Index < QUERY_DIRECTORY_FILES; Index++)
    {
        StringCchPrintfW(Path, RTL_NUMBER_OF(Path), L"%s\\directory-entry-with-a-long-name-%04lu.tmp", Directory, Index);
        DeleteFileW(Path);
    }
    RemoveDirectoryW(Directory);
}

static
VOID
TestMissing(
    _In_ PCWSTR Directory)
{
    FILE_NETWORK_OPEN_INFORMATION Full;
    FILE_BASIC_INFORMATION Basic;
    WCHAR Path[MAX_PATH];
    LONGLONG Before, After;
    BOOLEAN Counted;
    NTSTATUS Status;
    ULONG Round;

    StringCchPrintfW(Path, RTL_NUMBER_OF(Path), L"%s\\qattr-missing.tmp", Directory);
    Status = QueryFull(Path, &Full);
    ok_ntstatus(Status, STATUS_OBJECT_NAME_NOT_FOUND);
    ok(Full.FileAttributes == 0xCCCCCCCC, "FileAttributes 0x%lx\n", Full.FileAttributes);
    Status = QueryBasic(Path, &Basic);
    ok_ntstatus(Status, STATUS_OBJECT_NAME_NOT_FOUND);

    StringCchPrintfW(Path, RTL_NUMBER_OF(Path), L"%s\\qattr-missing-dir\\file.tmp", Directory);
    Status = QueryFull(Path, &Full);
    ok_ntstatus(Status, STATUS_OBJECT_PATH_NOT_FOUND);
    Status = QueryBasic(Path, &Basic);
    ok_ntstatus(Status, STATUS_OBJECT_PATH_NOT_FOUND);

    StringCchPrintfW(Path, RTL_NUMBER_OF(Path), L"%s\\qattr-missing.tmp", Directory);
    Counted = QueryTagOutstanding('mNoI', &Before);
    ok(Counted, "SystemPoolTagInformation failed\n");
    for (Round = 0; Round < QUERY_LEAK_ROUNDS; Round++)
    {
        QueryFull(Path, &Full);
        QueryBasic(Path, &Basic);
    }
    if (Counted && QueryTagOutstanding('mNoI', &After))
    {
        ok(After - Before < QUERY_LEAK_ROUNDS / 16, "%I64d file name allocations outstanding after %lu failed queries\n",
           After - Before, (ULONG)(2 * QUERY_LEAK_ROUNDS));
    }
}

static
VOID
TestOpenFile(
    _In_ PCWSTR Directory)
{
    FILE_DISPOSITION_INFORMATION Disposition;
    FILE_NETWORK_OPEN_INFORMATION Full;
    FILE_BASIC_INFORMATION Basic;
    IO_STATUS_BLOCK IoStatus;
    UCHAR Data[2 * QUERY_FILE_BYTES];
    WCHAR Path[MAX_PATH];
    NTSTATUS Status;
    DWORD Written;
    HANDLE File;

    StringCchPrintfW(Path, RTL_NUMBER_OF(Path), L"%s\\qattr-open.tmp", Directory);
    File = CreateFileW(Path, GENERIC_WRITE | DELETE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                       NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    ok(File != INVALID_HANDLE_VALUE, "CreateFileW failed with %lu\n", GetLastError());
    if (File == INVALID_HANDLE_VALUE)
        return;

    RtlFillMemory(Data, sizeof(Data), 0xA5);
    ok(WriteFile(File, Data, sizeof(Data), &Written, NULL) && Written == sizeof(Data), "WriteFile failed\n");
    Status = QueryFull(Path, &Full);
    ok_ntstatus(Status, STATUS_SUCCESS);
    ok(Full.EndOfFile.QuadPart == sizeof(Data), "EndOfFile %I64d\n", Full.EndOfFile.QuadPart);

    Disposition.DeleteFile = TRUE;
    Status = NtSetInformationFile(File, &IoStatus, &Disposition, sizeof(Disposition), FileDispositionInformation);
    ok_ntstatus(Status, STATUS_SUCCESS);
    Status = QueryFull(Path, &Full);
    ok_ntstatus(Status, STATUS_DELETE_PENDING);
    Status = QueryBasic(Path, &Basic);
    ok_ntstatus(Status, STATUS_DELETE_PENDING);
    CloseHandle(File);

    Status = QueryFull(Path, &Full);
    ok_ntstatus(Status, STATUS_OBJECT_NAME_NOT_FOUND);
}

static
BOOLEAN
MakeWorldDescriptor(
    _Out_ PSECURITY_DESCRIPTOR Descriptor,
    _Out_writes_bytes_(AclLength) PACL Acl,
    _In_ ULONG AclLength,
    _In_ PSID World,
    _In_ ACCESS_MASK Access)
{
    return NT_SUCCESS(RtlCreateAcl(Acl, AclLength, ACL_REVISION)) &&
           NT_SUCCESS(RtlAddAccessAllowedAce(Acl, ACL_REVISION, Access, World)) &&
           NT_SUCCESS(RtlCreateSecurityDescriptor(Descriptor, SECURITY_DESCRIPTOR_REVISION)) &&
           NT_SUCCESS(RtlSetDaclSecurityDescriptor(Descriptor, TRUE, Acl, FALSE)) &&
           NT_SUCCESS(RtlSetControlSecurityDescriptor(Descriptor, SE_DACL_PROTECTED, SE_DACL_PROTECTED));
}

static
VOID
TestParentAccess(
    _In_ PCWSTR Directory,
    _In_ PCWSTR Name,
    _In_ PSID World,
    _In_ ACCESS_MASK DirectoryAccess,
    _In_ ACCESS_MASK FileAccess,
    _In_ NTSTATUS Expected)
{
    FILE_NETWORK_OPEN_INFORMATION Full;
    FILE_BASIC_INFORMATION Basic;
    SECURITY_ATTRIBUTES Attributes = { sizeof(Attributes), NULL, FALSE };
    SECURITY_DESCRIPTOR DirectoryDescriptor, FileDescriptor;
    ULONG DirectoryAcl[64], FileAcl[64];
    WCHAR Child[MAX_PATH], Path[MAX_PATH];
    NTSTATUS Status;
    HANDLE File;

    StringCchPrintfW(Child, RTL_NUMBER_OF(Child), L"%s\\%s", Directory, Name);
    StringCchPrintfW(Path, RTL_NUMBER_OF(Path), L"%s\\qattr-acl.tmp", Child);
    if (!MakeWorldDescriptor(&DirectoryDescriptor, (PACL)DirectoryAcl, sizeof(DirectoryAcl), World, DirectoryAccess) ||
        !MakeWorldDescriptor(&FileDescriptor, (PACL)FileAcl, sizeof(FileAcl), World, FileAccess))
    {
        ok(FALSE, "Failed to build the security descriptors\n");
        return;
    }

    Attributes.lpSecurityDescriptor = &DirectoryDescriptor;
    ok(CreateDirectoryW(Child, &Attributes), "CreateDirectoryW(%ls) failed with %lu\n", Child, GetLastError());
    Attributes.lpSecurityDescriptor = &FileDescriptor;
    File = CreateFileW(Path, GENERIC_WRITE, 0, &Attributes, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
    ok(File != INVALID_HANDLE_VALUE, "CreateFileW(%ls) failed with %lu\n", Path, GetLastError());
    if (File != INVALID_HANDLE_VALUE)
        CloseHandle(File);

    Status = QueryFull(Path, &Full);
    ok(Status == Expected, "%ls: NtQueryFullAttributesFile returned 0x%lx, expected 0x%lx\n", Name, Status, Expected);
    Status = QueryBasic(Path, &Basic);
    ok(Status == Expected, "%ls: NtQueryAttributesFile returned 0x%lx, expected 0x%lx\n", Name, Status, Expected);
    File = CreateFileW(Path, FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                       NULL, OPEN_EXISTING, 0, NULL);
    ok((File != INVALID_HANDLE_VALUE) == NT_SUCCESS(Expected), "%ls: FILE_READ_ATTRIBUTES open %s with %lu\n", Name,
       File != INVALID_HANDLE_VALUE ? "succeeded" : "failed", GetLastError());
    if (File != INVALID_HANDLE_VALUE)
        CloseHandle(File);

    ok(DeleteFileW(Path), "DeleteFileW(%ls) failed with %lu\n", Path, GetLastError());
    ok(RemoveDirectoryW(Child), "RemoveDirectoryW(%ls) failed with %lu\n", Child, GetLastError());
}

static
VOID
TestParentGrantsReadAttributes(
    _In_ PCWSTR Directory)
{
    SID_IDENTIFIER_AUTHORITY WorldAuthority = SECURITY_WORLD_SID_AUTHORITY;
    const ACCESS_MASK DirectoryBase = FILE_TRAVERSE | FILE_ADD_FILE | FILE_DELETE_CHILD | FILE_READ_ATTRIBUTES |
                                      DELETE | READ_CONTROL | WRITE_DAC | SYNCHRONIZE;
    const ACCESS_MASK FileBase = DELETE | READ_CONTROL | WRITE_DAC | SYNCHRONIZE;
    PSID World = NULL;

    if (!NT_SUCCESS(RtlAllocateAndInitializeSid(&WorldAuthority, 1, SECURITY_WORLD_RID, 0, 0, 0, 0, 0, 0, 0, &World)))
    {
        ok(FALSE, "RtlAllocateAndInitializeSid failed\n");
        return;
    }

    TestParentAccess(Directory, L"qattr-list", World, DirectoryBase | FILE_LIST_DIRECTORY, FileBase, STATUS_SUCCESS);
    TestParentAccess(Directory, L"qattr-nolist", World, DirectoryBase, FileBase, STATUS_ACCESS_DENIED);
    TestParentAccess(Directory, L"qattr-own", World, DirectoryBase, FileBase | FILE_READ_ATTRIBUTES, STATUS_SUCCESS);

    RtlFreeSid(World);
}

#define QUERY_STRESS_FILES 16
#define QUERY_STRESS_THREADS 3
#define QUERY_STRESS_MS 600

typedef struct _QUERY_STRESS
{
    WCHAR Paths[QUERY_STRESS_FILES][MAX_PATH];
    volatile LONG Stop;
    volatile LONG Queries;
    volatile LONG Unexpected;
    volatile LONG LastUnexpected;
} QUERY_STRESS, *PQUERY_STRESS;

static
DWORD
WINAPI
QueryStressThread(
    _In_ PVOID Parameter)
{
    PQUERY_STRESS Stress = Parameter;
    FILE_NETWORK_OPEN_INFORMATION Full;
    ULONG Index = 0;
    NTSTATUS Status;

    while (!ReadAcquire(&Stress->Stop))
    {
        Status = QueryFull(Stress->Paths[Index++ % QUERY_STRESS_FILES], &Full);
        if (Status != STATUS_SUCCESS && Status != STATUS_OBJECT_NAME_NOT_FOUND && Status != STATUS_DELETE_PENDING)
        {
            InterlockedIncrement(&Stress->Unexpected);
            InterlockedExchange(&Stress->LastUnexpected, Status);
        }
        InterlockedIncrement(&Stress->Queries);
    }
    return 0;
}

static
BOOL
StressWriteFile(
    _In_ PCWSTR Path,
    _In_ ULONG Size,
    _In_ DWORD Disposition)
{
    UCHAR Data[512];
    DWORD Written;
    HANDLE File;
    BOOL Ok;

    File = CreateFileW(Path, GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL,
                       Disposition, FILE_ATTRIBUTE_NORMAL, NULL);
    if (File == INVALID_HANDLE_VALUE)
        return FALSE;
    RtlFillMemory(Data, sizeof(Data), (UCHAR)Size);
    Ok = TRUE;
    while (Ok && Size != 0)
    {
        DWORD Chunk = min(Size, (ULONG)sizeof(Data));

        Ok = WriteFile(File, Data, Chunk, &Written, NULL) && Written == Chunk;
        Size -= Chunk;
    }
    Ok = SetEndOfFile(File) && Ok;
    CloseHandle(File);
    return Ok;
}

static
VOID
TestConcurrentQueries(
    _In_ PCWSTR Directory)
{
    FILE_NETWORK_OPEN_INFORMATION Full;
    HANDLE Threads[QUERY_STRESS_THREADS];
    PQUERY_STRESS Stress;
    WCHAR Renamed[MAX_PATH];
    ULONG Sizes[QUERY_STRESS_FILES];
    ULONG Index;
    ULONG Round = 0;
    ULONG Failures = 0;
    DWORD Start;
    NTSTATUS Status;

    Stress = RtlAllocateHeap(RtlGetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*Stress));
    ok(Stress != NULL, "Out of memory\n");
    if (!Stress)
        return;
    for (Index = 0; Index < QUERY_STRESS_FILES; Index++)
    {
        StringCchPrintfW(Stress->Paths[Index], MAX_PATH, L"%s\\qattr-stress-%02lu.tmp", Directory, Index);
        Sizes[Index] = 100 + Index;
        ok(StressWriteFile(Stress->Paths[Index], Sizes[Index], CREATE_ALWAYS), "Create %lu failed with %lu\n",
           Index, GetLastError());
    }
    for (Index = 0; Index < QUERY_STRESS_THREADS; Index++)
    {
        Threads[Index] = CreateThread(NULL, 0, QueryStressThread, Stress, 0, NULL);
        ok(Threads[Index] != NULL, "CreateThread failed with %lu\n", GetLastError());
    }

    Start = GetTickCount();
    while (GetTickCount() - Start < QUERY_STRESS_MS)
    {
        Index = Round % QUERY_STRESS_FILES;
        switch (Round++ % 3)
        {
            case 0:
                Sizes[Index] = (Sizes[Index] * 7 + 4097) % 20000;
                if (!StressWriteFile(Stress->Paths[Index], Sizes[Index], OPEN_EXISTING))
                    Failures++;
                break;
            case 1:
                StringCchPrintfW(Renamed, RTL_NUMBER_OF(Renamed), L"%s.moved", Stress->Paths[Index]);
                if (!MoveFileW(Stress->Paths[Index], Renamed) || !MoveFileW(Renamed, Stress->Paths[Index]))
                    Failures++;
                break;
            default:
                if (!DeleteFileW(Stress->Paths[Index]))
                    Failures++;
                Sizes[Index] = 300 + Round % 1000;
                if (!StressWriteFile(Stress->Paths[Index], Sizes[Index], CREATE_NEW))
                    Failures++;
                break;
        }
    }

    InterlockedExchange(&Stress->Stop, 1);
    for (Index = 0; Index < QUERY_STRESS_THREADS; Index++)
    {
        if (Threads[Index])
        {
            ok(WaitForSingleObject(Threads[Index], 10000) == WAIT_OBJECT_0, "Query thread %lu did not stop\n", Index);
            CloseHandle(Threads[Index]);
        }
    }

    ok(Failures == 0, "%lu file operations failed\n", Failures);
    ok(Stress->Queries > 0, "No queries ran\n");
    ok(Stress->Unexpected == 0, "%ld queries returned an unexpected status (last 0x%lx)\n",
       Stress->Unexpected, Stress->LastUnexpected);
    trace("%lu modifications, %ld concurrent queries\n", Round, Stress->Queries);

    for (Index = 0; Index < QUERY_STRESS_FILES; Index++)
    {
        Status = QueryFull(Stress->Paths[Index], &Full);
        ok(Status == STATUS_SUCCESS, "File %lu: status 0x%lx\n", Index, Status);
        ok(Full.EndOfFile.QuadPart == Sizes[Index], "File %lu: EndOfFile %I64d, expected %lu\n", Index,
           Full.EndOfFile.QuadPart, Sizes[Index]);
        ok(DeleteFileW(Stress->Paths[Index]), "Delete %lu failed with %lu\n", Index, GetLastError());
    }
    RtlFreeHeap(RtlGetProcessHeap(), 0, Stress);
}

START_TEST(NtQueryAttributesFile)
{
    WCHAR Directory[MAX_PATH];
    WCHAR Path[MAX_PATH];
    UCHAR Data[QUERY_FILE_BYTES];
    DWORD Written;
    HANDLE File;
    DWORD Length;

    Length = GetTempPathW(RTL_NUMBER_OF(Directory), Directory);
    ok(Length != 0 && Length < RTL_NUMBER_OF(Directory), "GetTempPathW failed\n");
    if (Length == 0 || Length >= RTL_NUMBER_OF(Directory))
        return;
    if (Directory[Length - 1] == L'\\')
        Directory[Length - 1] = UNICODE_NULL;

    StringCchPrintfW(Path, RTL_NUMBER_OF(Path), L"%s\\qattr-file.tmp", Directory);
    SetFileAttributesW(Path, FILE_ATTRIBUTE_NORMAL);
    File = CreateFileW(Path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    ok(File != INVALID_HANDLE_VALUE, "CreateFileW failed with %lu\n", GetLastError());
    if (File == INVALID_HANDLE_VALUE)
        return;
    RtlFillMemory(Data, sizeof(Data), 0x5A);
    ok(WriteFile(File, Data, sizeof(Data), &Written, NULL) && Written == sizeof(Data), "WriteFile failed\n");
    CloseHandle(File);
    ok(SetFileAttributesW(Path, FILE_ATTRIBUTE_ARCHIVE | FILE_ATTRIBUTE_READONLY), "SetFileAttributesW failed\n");

    TestExistingFile(Path);
    TestDirectory(Directory);
    TestOpenFile(Directory);
    TestMissing(Directory);
    TestParentGrantsReadAttributes(Directory);
    TestConcurrentQueries(Directory);

    SetFileAttributesW(Path, FILE_ATTRIBUTE_NORMAL);
    DeleteFileW(Path);
}
