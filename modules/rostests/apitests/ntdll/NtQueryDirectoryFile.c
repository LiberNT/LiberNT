/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Directory-query cursor and end-of-enumeration regression test
 */

#include "precomp.h"

static VOID
TestMissingDirectoryEntry(VOID)
{
    UNICODE_STRING Directory = RTL_CONSTANT_STRING(L"\\SystemRoot\\System32");
    UNICODE_STRING Filters[] =
    {
        RTL_CONSTANT_STRING(L"__reactos_query_directory_missing__.inf"),
        RTL_CONSTANT_STRING(L"__reactos_query_directory_missing__*.inf")
    };
    FILE_INFORMATION_CLASS Classes[] =
    {
        FileDirectoryInformation,
        FileFullDirectoryInformation,
        FileBothDirectoryInformation
    };
    OBJECT_ATTRIBUTES Attributes;
    IO_STATUS_BLOCK IoStatus;
    HANDLE Handle;
    NTSTATUS Status;
    ULONG Buffer[256];
    ULONG i, j, k;

    InitializeObjectAttributes(&Attributes, &Directory, OBJ_CASE_INSENSITIVE, NULL, NULL);
    for (i = 0; i < RTL_NUMBER_OF(Filters); ++i)
    {
        for (j = 0; j < RTL_NUMBER_OF(Classes); ++j)
        {
            Status = NtOpenFile(&Handle, FILE_LIST_DIRECTORY | SYNCHRONIZE, &Attributes, &IoStatus, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, FILE_DIRECTORY_FILE | FILE_SYNCHRONOUS_IO_NONALERT);
            ok_hex(Status, STATUS_SUCCESS);
            if (!NT_SUCCESS(Status))
                continue;

            /* Only the first query reports NO_SUCH_FILE. Continuing or
             * restarting the same unsuccessful scan reports NO_MORE_FILES. */
            for (k = 0; k < 3; ++k)
            {
                Status = NtQueryDirectoryFile(Handle, NULL, NULL, NULL, &IoStatus, Buffer, sizeof(Buffer), Classes[j], TRUE, k == 0 ? &Filters[i] : NULL, k != 1);
                ok(Status == (k == 0 ? STATUS_NO_SUCH_FILE : STATUS_NO_MORE_FILES), "Filter %lu, class %u, query %lu: status %08lx\n", i, Classes[j], k, Status);
            }
            NtClose(Handle);
        }
    }
}

static VOID
TestSmallDirectoryBuffer(VOID)
{
    UNICODE_STRING Directory = RTL_CONSTANT_STRING(L"\\SystemRoot\\System32");
    UNICODE_STRING Filter = RTL_CONSTANT_STRING(L"ntdll.dll");
    FILE_INFORMATION_CLASS Classes[] =
    {
        FileDirectoryInformation,
        FileFullDirectoryInformation,
        FileBothDirectoryInformation
    };
    ULONG Sizes[] = {88, 88, 128};
    ULONG Expected[] = {82, 86, 112};
    OBJECT_ATTRIBUTES Attributes;
    IO_STATUS_BLOCK IoStatus;
    HANDLE Handle;
    NTSTATUS Status;
    ULONG i;
    union
    {
        ULONGLONG Alignment;
        UCHAR Bytes[128];
    } Buffer;

    InitializeObjectAttributes(&Attributes, &Directory, OBJ_CASE_INSENSITIVE, NULL, NULL);
    for (i = 0; i < RTL_NUMBER_OF(Classes); ++i)
    {
        Status = NtOpenFile(&Handle, FILE_LIST_DIRECTORY | SYNCHRONIZE, &Attributes, &IoStatus,
                            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                            FILE_DIRECTORY_FILE | FILE_SYNCHRONOUS_IO_NONALERT);
        ok_hex(Status, STATUS_SUCCESS);
        if (!NT_SUCCESS(Status))
            continue;
        RtlZeroMemory(&Buffer, sizeof(Buffer));
        IoStatus.Information = 0;
        Status = NtQueryDirectoryFile(Handle, NULL, NULL, NULL, &IoStatus, &Buffer,
                                      Sizes[i], Classes[i], TRUE, &Filter, TRUE);
        ok_hex(Status, STATUS_SUCCESS);
        if (NT_SUCCESS(Status))
        {
            PFILE_DIRECTORY_INFORMATION Information = (PFILE_DIRECTORY_INFORMATION)&Buffer;
            ok(IoStatus.Information == Expected[i], "Class %u returned %Iu bytes, expected %lu\n",
               Classes[i], (size_t)IoStatus.Information, Expected[i]);
            ok(Information->FileNameLength == Filter.Length, "Class %u returned name length %lu\n",
               Classes[i], Information->FileNameLength);
            if (Information->FileNameLength == Filter.Length)
            {
                ULONG NameOffset = i == 0 ? FIELD_OFFSET(FILE_DIRECTORY_INFORMATION, FileName) :
                                   i == 1 ? FIELD_OFFSET(FILE_FULL_DIR_INFORMATION, FileName) :
                                            FIELD_OFFSET(FILE_BOTH_DIR_INFORMATION, FileName);
                UNICODE_STRING Found;
                Found.Length = Found.MaximumLength = (USHORT)Information->FileNameLength;
                Found.Buffer = (PWSTR)(Buffer.Bytes + NameOffset);
                ok(RtlEqualUnicodeString(&Found, &Filter, TRUE), "Class %u returned the wrong name\n", Classes[i]);
            }
        }
        NtClose(Handle);
    }
}

#define CONCURRENT_DIRECTORIES 4
#define CONCURRENT_ENUMERATORS 4
#define CONCURRENT_STABLE_FILES 300
#define CONCURRENT_ENUMERATIONS 40

typedef struct _CONCURRENT_ENUMERATION
{
    WCHAR Root[MAX_PATH];
    volatile LONG Stop;
    volatile LONG Failures;
    volatile LONG Mutations;
} CONCURRENT_ENUMERATION, *PCONCURRENT_ENUMERATION;

typedef struct _CONCURRENT_ENUMERATOR
{
    PCONCURRENT_ENUMERATION Context;
    ULONG Index;
} CONCURRENT_ENUMERATOR, *PCONCURRENT_ENUMERATOR;

static BOOLEAN
EnumerateStableDirectory(_In_ PCONCURRENT_ENUMERATION Context,
                         _In_ ULONG Directory)
{
    UCHAR Seen[CONCURRENT_STABLE_FILES];
    WCHAR Path[MAX_PATH];
    IO_STATUS_BLOCK IoStatus;
    NTSTATUS Status;
    HANDLE Handle;
    BOOLEAN Restart = TRUE;
    BOOLEAN Good = TRUE;
    ULONG Dots = 0;
    ULONG i;
    union
    {
        ULONGLONG Alignment;
        UCHAR Bytes[1024];
    } Buffer;

    StringCchPrintfW(Path, RTL_NUMBER_OF(Path), L"%s\\d%lu", Context->Root, Directory);
    Handle = CreateFileW(Path, FILE_LIST_DIRECTORY | SYNCHRONIZE,
                         FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                         NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
    if (Handle == INVALID_HANDLE_VALUE)
        return FALSE;

    RtlZeroMemory(Seen, sizeof(Seen));
    for (;;)
    {
        PFILE_DIRECTORY_INFORMATION Entry;

        Status = NtQueryDirectoryFile(Handle, NULL, NULL, NULL, &IoStatus, &Buffer, sizeof(Buffer),
                                      FileDirectoryInformation, FALSE, NULL, Restart);
        Restart = FALSE;
        if (Status == STATUS_NO_MORE_FILES)
            break;
        if (!NT_SUCCESS(Status))
        {
            Good = FALSE;
            break;
        }

        Entry = (PFILE_DIRECTORY_INFORMATION)Buffer.Bytes;
        for (;;)
        {
            ULONG Length = Entry->FileNameLength / sizeof(WCHAR);
            PWCHAR Name = Entry->FileName;

            if ((Length == 1 && Name[0] == L'.') ||
                (Length == 2 && Name[0] == L'.' && Name[1] == L'.'))
            {
                Dots++;
            }
            else if (Length == 8 && Name[0] == L's' &&
                     Name[1] >= L'0' && Name[1] <= L'9' &&
                     Name[2] >= L'0' && Name[2] <= L'9' &&
                     Name[3] >= L'0' && Name[3] <= L'9')
            {
                ULONG Number = (Name[1] - L'0') * 100 + (Name[2] - L'0') * 10 + (Name[3] - L'0');

                if (Number < CONCURRENT_STABLE_FILES)
                    Seen[Number]++;
                else
                    Good = FALSE;
            }
            else if (Length == 0 || Name[0] != L't')
            {
                Good = FALSE;
            }

            if (Entry->NextEntryOffset == 0)
                break;
            Entry = (PFILE_DIRECTORY_INFORMATION)((PUCHAR)Entry + Entry->NextEntryOffset);
        }
    }
    CloseHandle(Handle);

    if (Dots != 2)
        Good = FALSE;
    for (i = 0; i < CONCURRENT_STABLE_FILES; ++i)
    {
        if (Seen[i] != 1)
            Good = FALSE;
    }
    return Good;
}

static DWORD WINAPI
ConcurrentEnumerator(_In_ PVOID Parameter)
{
    PCONCURRENT_ENUMERATOR Enumerator = Parameter;
    PCONCURRENT_ENUMERATION Context = Enumerator->Context;
    ULONG i;

    for (i = 0; i < 1000 && InterlockedCompareExchange(&Context->Mutations, 0, 0) == 0; ++i)
        SwitchToThread();

    for (i = 0; i < CONCURRENT_ENUMERATIONS; ++i)
    {
        ULONG Directory = (i & 1) ? 0 : Enumerator->Index % CONCURRENT_DIRECTORIES;

        if (!EnumerateStableDirectory(Context, Directory))
            InterlockedIncrement(&Context->Failures);
    }
    return 0;
}

static DWORD WINAPI
ConcurrentMutator(_In_ PVOID Parameter)
{
    PCONCURRENT_ENUMERATION Context = Parameter;
    WCHAR Path[MAX_PATH];
    ULONG i;

    for (i = 0; InterlockedCompareExchange(&Context->Stop, 0, 0) == 0; ++i)
    {
        HANDLE File;

        StringCchPrintfW(Path, RTL_NUMBER_OF(Path), L"%s\\d%lu\\t%06lu.dat",
                         Context->Root, i % CONCURRENT_DIRECTORIES, i);
        File = CreateFileW(Path, GENERIC_WRITE | DELETE, 0, NULL, CREATE_NEW,
                           FILE_ATTRIBUTE_NORMAL | FILE_FLAG_DELETE_ON_CLOSE, NULL);
        if (File == INVALID_HANDLE_VALUE)
        {
            InterlockedIncrement(&Context->Failures);
            break;
        }
        CloseHandle(File);
        InterlockedIncrement(&Context->Mutations);
    }
    return 0;
}

static VOID
TestConcurrentEnumeration(VOID)
{
    CONCURRENT_ENUMERATOR Enumerators[CONCURRENT_ENUMERATORS];
    HANDLE Threads[CONCURRENT_ENUMERATORS + 1];
    CONCURRENT_ENUMERATION Context;
    WCHAR Temp[MAX_PATH];
    WCHAR Path[MAX_PATH];
    ULONG Directory, File, i;
    DWORD Wait;

    RtlZeroMemory(&Context, sizeof(Context));
    GetTempPathW(RTL_NUMBER_OF(Temp), Temp);
    StringCchPrintfW(Context.Root, RTL_NUMBER_OF(Context.Root), L"%sqdirconc%lu", Temp, GetCurrentProcessId());
    if (!CreateDirectoryW(Context.Root, NULL))
    {
        ok(FALSE, "CreateDirectoryW failed with %lu\n", GetLastError());
        return;
    }

    for (Directory = 0; Directory < CONCURRENT_DIRECTORIES; ++Directory)
    {
        StringCchPrintfW(Path, RTL_NUMBER_OF(Path), L"%s\\d%lu", Context.Root, Directory);
        ok(CreateDirectoryW(Path, NULL), "CreateDirectoryW failed with %lu\n", GetLastError());
        for (File = 0; File < CONCURRENT_STABLE_FILES; ++File)
        {
            HANDLE Handle;

            StringCchPrintfW(Path, RTL_NUMBER_OF(Path), L"%s\\d%lu\\s%03lu.dat", Context.Root, Directory, File);
            Handle = CreateFileW(Path, GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
            ok(Handle != INVALID_HANDLE_VALUE, "CreateFileW failed with %lu\n", GetLastError());
            if (Handle != INVALID_HANDLE_VALUE)
                CloseHandle(Handle);
        }
    }

    for (Directory = 0; Directory < CONCURRENT_DIRECTORIES; ++Directory)
        ok(EnumerateStableDirectory(&Context, Directory), "Directory %lu enumerated wrongly\n", Directory);

    Threads[CONCURRENT_ENUMERATORS] = CreateThread(NULL, 0, ConcurrentMutator, &Context, 0, NULL);
    ok(Threads[CONCURRENT_ENUMERATORS] != NULL, "CreateThread failed with %lu\n", GetLastError());
    for (i = 0; i < CONCURRENT_ENUMERATORS; ++i)
    {
        Enumerators[i].Context = &Context;
        Enumerators[i].Index = i;
        Threads[i] = CreateThread(NULL, 0, ConcurrentEnumerator, &Enumerators[i], 0, NULL);
        ok(Threads[i] != NULL, "CreateThread failed with %lu\n", GetLastError());
    }

    Wait = WaitForMultipleObjects(CONCURRENT_ENUMERATORS, Threads, TRUE, 60000);
    ok_long(Wait, WAIT_OBJECT_0);
    InterlockedExchange(&Context.Stop, 1);
    Wait = WaitForSingleObject(Threads[CONCURRENT_ENUMERATORS], 60000);
    ok_long(Wait, WAIT_OBJECT_0);
    for (i = 0; i <= CONCURRENT_ENUMERATORS; ++i)
        CloseHandle(Threads[i]);

    ok_long(Context.Failures, 0);
    ok(Context.Mutations > 0, "The directories never changed during enumeration\n");
    trace("%ld files created and deleted during %u enumerations\n",
          Context.Mutations, CONCURRENT_ENUMERATORS * CONCURRENT_ENUMERATIONS);

    for (Directory = 0; Directory < CONCURRENT_DIRECTORIES; ++Directory)
    {
        for (File = 0; File < CONCURRENT_STABLE_FILES; ++File)
        {
            StringCchPrintfW(Path, RTL_NUMBER_OF(Path), L"%s\\d%lu\\s%03lu.dat", Context.Root, Directory, File);
            DeleteFileW(Path);
        }
        StringCchPrintfW(Path, RTL_NUMBER_OF(Path), L"%s\\d%lu", Context.Root, Directory);
        ok(RemoveDirectoryW(Path), "RemoveDirectoryW failed with %lu\n", GetLastError());
    }
    ok(RemoveDirectoryW(Context.Root), "RemoveDirectoryW failed with %lu\n", GetLastError());
}

static VOID
TestUserBufferProbe(VOID)
{
    UNICODE_STRING Directory = RTL_CONSTANT_STRING(L"\\SystemRoot\\System32");
    OBJECT_ATTRIBUTES Attributes;
    IO_STATUS_BLOCK IoStatus;
    PVOID Base = NULL;
    SIZE_T Size = 2 * PAGE_SIZE;
    ULONG OldProtect;
    HANDLE Handle;
    NTSTATUS Status;

    Status = NtAllocateVirtualMemory(NtCurrentProcess(), &Base, 0, &Size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    ok_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
        return;

    InitializeObjectAttributes(&Attributes, &Directory, OBJ_CASE_INSENSITIVE, NULL, NULL);
    Status = NtOpenFile(&Handle, FILE_LIST_DIRECTORY | SYNCHRONIZE, &Attributes, &IoStatus,
                        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                        FILE_DIRECTORY_FILE | FILE_SYNCHRONOUS_IO_NONALERT);
    ok_hex(Status, STATUS_SUCCESS);
    if (NT_SUCCESS(Status))
    {
        Status = NtQueryDirectoryFile(Handle, NULL, NULL, NULL, &IoStatus, (PVOID)(ULONG_PTR)~(ULONG_PTR)0xFFF,
                                      PAGE_SIZE, FileDirectoryInformation, FALSE, NULL, TRUE);
        ok_hex(Status, STATUS_ACCESS_VIOLATION);

        Status = NtQueryDirectoryFile(Handle, NULL, NULL, NULL, &IoStatus, (PUCHAR)Base + 2,
                                      PAGE_SIZE, FileDirectoryInformation, FALSE, NULL, TRUE);
        ok_hex(Status, STATUS_DATATYPE_MISALIGNMENT);

        Size = PAGE_SIZE;
        Status = NtProtectVirtualMemory(NtCurrentProcess(), &Base, &Size, PAGE_READONLY, &OldProtect);
        ok_hex(Status, STATUS_SUCCESS);
        IoStatus.Status = 0x12345678;
        IoStatus.Information = 0x1234;
        Status = NtQueryDirectoryFile(Handle, NULL, NULL, NULL, &IoStatus, Base,
                                      PAGE_SIZE, FileDirectoryInformation, FALSE, NULL, TRUE);
        ok_hex(Status, STATUS_ACCESS_VIOLATION);
        ok_hex(IoStatus.Status, 0x12345678);
        ok(IoStatus.Information == 0x1234, "Information %Iu\n", (size_t)IoStatus.Information);

        Status = NtQueryDirectoryFile(Handle, NULL, NULL, NULL, &IoStatus, (PUCHAR)Base + PAGE_SIZE,
                                      PAGE_SIZE, FileDirectoryInformation, FALSE, NULL, TRUE);
        ok_hex(Status, STATUS_SUCCESS);
        ok(IoStatus.Information != 0, "Information %Iu\n", (size_t)IoStatus.Information);
        NtClose(Handle);
    }

    Size = 0;
    NtFreeVirtualMemory(NtCurrentProcess(), &Base, &Size, MEM_RELEASE);
}

START_TEST(NtQueryDirectoryFile)
{
    UNICODE_STRING Directory = RTL_CONSTANT_STRING(L"\\SystemRoot\\System32");
    UNICODE_STRING Filter = RTL_CONSTANT_STRING(L"ntdll.dll");
    UNICODE_STRING Found;
    OBJECT_ATTRIBUTES Attributes;
    IO_STATUS_BLOCK IoStatus;
    HANDLE Handle;
    NTSTATUS Status;
    ULONG i;
    union
    {
        FILE_DIRECTORY_INFORMATION Information;
        UCHAR Bytes[512];
    } Buffer;

    InitializeObjectAttributes(&Attributes, &Directory, OBJ_CASE_INSENSITIVE, NULL, NULL);
    Status = NtOpenFile(&Handle, FILE_LIST_DIRECTORY | SYNCHRONIZE,
                        &Attributes, &IoStatus,
                        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                        FILE_DIRECTORY_FILE | FILE_SYNCHRONOUS_IO_NONALERT);
    ok_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
        return;

    /* An exact-name search has one result. A lost directory cursor repeats
     * that result forever; a lost completion status hides NO_MORE_FILES. */
    for (i = 0; i < 4; ++i)
    {
        RtlZeroMemory(&Buffer, sizeof(Buffer));
        Status = NtQueryDirectoryFile(Handle, NULL, NULL, NULL, &IoStatus,
                                      &Buffer, sizeof(Buffer), FileDirectoryInformation,
                                      TRUE, i == 0 ? &Filter : NULL, i == 0 || i == 2);
        if (i == 0 || i == 2)
        {
            ok_hex(Status, STATUS_SUCCESS);
            if (NT_SUCCESS(Status))
            {
                ok(Buffer.Information.FileNameLength == Filter.Length,
                   "Name length %lu, expected %u\n", Buffer.Information.FileNameLength, Filter.Length);
                if (Buffer.Information.FileNameLength == Filter.Length)
                {
                    Found.Length = Found.MaximumLength = Filter.Length;
                    Found.Buffer = Buffer.Information.FileName;
                    ok(RtlEqualUnicodeString(&Found, &Filter, TRUE), "Unexpected filename\n");
                }
            }
        }
        else
        {
            ok_hex(Status, STATUS_NO_MORE_FILES);
        }
    }
    NtClose(Handle);
    TestMissingDirectoryEntry();
    TestSmallDirectoryBuffer();
    TestUserBufferProbe();
    TestConcurrentEnumeration();
}
