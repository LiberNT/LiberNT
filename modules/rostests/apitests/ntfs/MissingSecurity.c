/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test NTFS security for file records whose security descriptor cannot be found
 * COPYRIGHT:   Copyright 2026 Ostap Brehin <osbre@protonmail.com>
 */

#define WIN32_NO_STATUS
#include <apitest.h>
#include <winioctl.h>
#include <sddl.h>
#include <strsafe.h>
#include <ntstatus.h>
#define NTOS_MODE_USER
#include <ndk/obfuncs.h>

#define TEST_VOLUME_LABEL L"NTFSTEST"
#define NTFS_FIXUP_STRIDE 512
#define NTFS_SI_V1_LENGTH 0x30
#define NTFS_SI_SECURITY_ID 0x34
#define BAD_SECURITY_ID 0xF000

typedef enum _PATCH_KIND
{
    PatchZero,
    PatchBadId,
    PatchV1,
} PATCH_KIND;

typedef struct _TEST_OBJECT
{
    PCWSTR Name;
    BOOL Directory;
    PATCH_KIND Patch;
    ULONGLONG Record;
} TEST_OBJECT, *PTEST_OBJECT;

static TEST_OBJECT Objects[] =
{
    { L"zero.txt", FALSE, PatchZero },
    { L"badid.txt", FALSE, PatchBadId },
    { L"v1.txt", FALSE, PatchV1 },
    { L"zero_dir", TRUE, PatchZero },
    { L"set.txt", FALSE, PatchZero },
};

#define SET_OBJECT 4

static WCHAR VolumeRoot[] = L"?:\\";
static WCHAR VolumeDevice[] = L"\\\\.\\?:";
static WCHAR TestDir[MAX_PATH];

static PCWSTR
TestPath(PCWSTR Name)
{
    static WCHAR Path[4][MAX_PATH];
    static ULONG Next;
    PWSTR Buffer = Path[Next++ % ARRAYSIZE(Path)];

    StringCchPrintfW(Buffer, MAX_PATH, L"%s\\%s", TestDir, Name);
    return Buffer;
}

static BOOL
FindTestVolume(VOID)
{
    WCHAR Label[MAX_PATH + 1];
    WCHAR FileSystem[MAX_PATH + 1];
    WCHAR Letter;

    for (Letter = L'C'; Letter <= L'Z'; Letter++)
    {
        VolumeRoot[0] = Letter;
        if (GetVolumeInformationW(VolumeRoot, Label, ARRAYSIZE(Label), NULL, NULL, NULL,
                                  FileSystem, ARRAYSIZE(FileSystem)) &&
            !wcscmp(Label, TEST_VOLUME_LABEL) &&
            !wcscmp(FileSystem, L"NTFS"))
        {
            VolumeDevice[4] = Letter;
            StringCchPrintfW(TestDir, ARRAYSIZE(TestDir), L"%c:\\MissingSecurity", Letter);
            return TRUE;
        }
    }
    return FALSE;
}

static VOID
RemoveTestFiles(VOID)
{
    ULONG Index;

    DeleteFileW(TestPath(L"zero_dir\\inner.txt"));
    DeleteFileW(TestPath(L"zero_dir\\renamed.txt"));
    DeleteFileW(TestPath(L"zero_dir\\elevated.txt"));
    DeleteFileW(TestPath(L"zero_dir\\limited.txt"));
    RemoveDirectoryW(TestPath(L"zero_dir\\limited"));
    DeleteFileW(TestPath(L"control.txt"));
    for (Index = 0; Index < ARRAYSIZE(Objects); Index++)
    {
        if (Objects[Index].Directory)
            RemoveDirectoryW(TestPath(Objects[Index].Name));
        else
            DeleteFileW(TestPath(Objects[Index].Name));
    }
    RemoveDirectoryW(TestDir);
}

static BOOL
CreateTestFile(PCWSTR Path, PCWSTR Sddl)
{
    SECURITY_ATTRIBUTES Attributes = { sizeof(Attributes), NULL, FALSE };
    HANDLE Handle;

    if (Sddl &&
        !ConvertStringSecurityDescriptorToSecurityDescriptorW(Sddl, SDDL_REVISION_1,
                                                              &Attributes.lpSecurityDescriptor, NULL))
    {
        return FALSE;
    }
    Handle = CreateFileW(Path, GENERIC_WRITE, 0, Sddl ? &Attributes : NULL, CREATE_NEW, 0, NULL);
    if (Attributes.lpSecurityDescriptor)
        LocalFree(Attributes.lpSecurityDescriptor);
    if (Handle == INVALID_HANDLE_VALUE)
        return FALSE;
    WriteFile(Handle, "content", 7, NULL, NULL);
    CloseHandle(Handle);
    return TRUE;
}

static BOOL
CreateTestObjects(VOID)
{
    BY_HANDLE_FILE_INFORMATION Information;
    HANDLE Handle;
    ULONG Index;

    if (!CreateDirectoryW(TestDir, NULL))
        return FALSE;

    for (Index = 0; Index < ARRAYSIZE(Objects); Index++)
    {
        PCWSTR Path = TestPath(Objects[Index].Name);

        if (Objects[Index].Directory ? !CreateDirectoryW(Path, NULL) : !CreateTestFile(Path, NULL))
            return FALSE;

        Handle = CreateFileW(Path, FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                             NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
        if (Handle == INVALID_HANDLE_VALUE)
            return FALSE;
        if (!GetFileInformationByHandle(Handle, &Information))
        {
            CloseHandle(Handle);
            return FALSE;
        }
        CloseHandle(Handle);
        Objects[Index].Record = (((ULONGLONG)Information.nFileIndexHigh << 32) |
                                 Information.nFileIndexLow) & 0xFFFFFFFFFFFFULL;
    }

    return CreateTestFile(TestPath(L"zero_dir\\inner.txt"), L"D:P(A;;FA;;;WD)") &&
           CreateTestFile(TestPath(L"control.txt"), L"D:P(A;;FA;;;WD)");
}

static BOOL
ApplyFixups(PUCHAR Record, ULONG RecordSize, BOOL Decode)
{
    USHORT UsaOffset = *(PUSHORT)(Record + 4);
    USHORT UsaCount = *(PUSHORT)(Record + 6);
    PUSHORT Usa = (PUSHORT)(Record + UsaOffset);
    ULONG Index;

    if (UsaCount != RecordSize / NTFS_FIXUP_STRIDE + 1 ||
        UsaOffset + UsaCount * sizeof(USHORT) > NTFS_FIXUP_STRIDE)
    {
        return FALSE;
    }

    for (Index = 1; Index < UsaCount; Index++)
    {
        PUSHORT Tail = (PUSHORT)(Record + Index * NTFS_FIXUP_STRIDE - sizeof(USHORT));

        if (Decode)
        {
            if (*Tail != Usa[0])
                return FALSE;
            *Tail = Usa[Index];
        }
        else
        {
            Usa[Index] = *Tail;
            *Tail = Usa[0];
        }
    }
    return TRUE;
}

static PUCHAR
FindAttribute(PUCHAR Record, ULONG RecordSize, ULONG Type)
{
    ULONG Offset = *(PUSHORT)(Record + 0x14);

    while (Offset + 0x18 <= RecordSize)
    {
        ULONG AttributeType = *(PULONG)(Record + Offset);
        ULONG Length = *(PULONG)(Record + Offset + 4);

        if (AttributeType == 0xFFFFFFFF || Length < 0x18 || Offset + Length > RecordSize)
            break;
        if (AttributeType == Type)
            return Record + Offset;
        Offset += Length;
    }
    return NULL;
}

static PUCHAR
FindStandardInformation(PUCHAR Record, ULONG RecordSize, ULONGLONG RecordNumber, PULONG ValueLength)
{
    PUCHAR Attribute;

    if (memcmp(Record, "FILE", 4) != 0 ||
        *(PUSHORT)(Record + 4) < 0x30 ||
        *(PULONG)(Record + 0x2C) != (ULONG)RecordNumber ||
        !(*(PUSHORT)(Record + 0x16) & 1) ||
        FindAttribute(Record, RecordSize, 0x50))
    {
        return NULL;
    }

    Attribute = FindAttribute(Record, RecordSize, 0x10);
    if (!Attribute || Attribute[8] != 0)
        return NULL;
    *ValueLength = *(PULONG)(Attribute + 0x10);
    if (*ValueLength < NTFS_SI_V1_LENGTH)
        return NULL;
    return Attribute;
}

static VOID
ShortenStandardInformation(PUCHAR Record, PUCHAR Attribute)
{
    USHORT ValueOffset = *(PUSHORT)(Attribute + 0x14);
    ULONG OldLength = *(PULONG)(Attribute + 4);
    ULONG NewLength = (ValueOffset + NTFS_SI_V1_LENGTH + 7) & ~7;
    PULONG BytesInUse = (PULONG)(Record + 0x18);
    PUCHAR Next = Attribute + OldLength;

    MoveMemory(Attribute + NewLength, Next, *BytesInUse - (ULONG)(Next - Record));
    *BytesInUse -= OldLength - NewLength;
    *(PULONG)(Attribute + 4) = NewLength;
    *(PULONG)(Attribute + 0x10) = NTFS_SI_V1_LENGTH;
}

static BOOL
ExpectedPatch(PTEST_OBJECT Object, ULONG ValueLength, PUCHAR Value)
{
    if (Object->Patch == PatchV1)
        return ValueLength == NTFS_SI_V1_LENGTH;
    if (ValueLength < NTFS_SI_SECURITY_ID + sizeof(ULONG))
        return FALSE;
    return *(PULONG)(Value + NTFS_SI_SECURITY_ID) ==
           (Object->Patch == PatchBadId ? BAD_SECURITY_ID : 0);
}

static BOOL
AccessRecords(BOOL Patch, ULONG Count)
{
    NTFS_VOLUME_DATA_BUFFER VolumeData;
    PUCHAR Record = NULL;
    HANDLE Volume;
    DWORD Bytes;
    BOOL Locked = FALSE;
    BOOL Result = FALSE;
    ULONG Index;

    Volume = CreateFileW(VolumeDevice, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                         NULL, OPEN_EXISTING, 0, NULL);
    ok(Volume != INVALID_HANDLE_VALUE, "Opening the volume failed with %lu\n", GetLastError());
    if (Volume == INVALID_HANDLE_VALUE)
        return FALSE;

    if (!DeviceIoControl(Volume, FSCTL_GET_NTFS_VOLUME_DATA, NULL, 0, &VolumeData, sizeof(VolumeData),
                         &Bytes, NULL))
    {
        ok(FALSE, "FSCTL_GET_NTFS_VOLUME_DATA failed with %lu\n", GetLastError());
        goto Done;
    }

    Locked = DeviceIoControl(Volume, FSCTL_LOCK_VOLUME, NULL, 0, NULL, 0, &Bytes, NULL);
    ok(Locked, "FSCTL_LOCK_VOLUME failed with %lu\n", GetLastError());
    if (!Locked)
        goto Done;
    if (!DeviceIoControl(Volume, FSCTL_DISMOUNT_VOLUME, NULL, 0, NULL, 0, &Bytes, NULL))
    {
        ok(FALSE, "FSCTL_DISMOUNT_VOLUME failed with %lu\n", GetLastError());
        goto Done;
    }

    Record = VirtualAlloc(NULL, VolumeData.BytesPerFileRecordSegment, MEM_COMMIT, PAGE_READWRITE);
    if (!Record)
        goto Done;

    for (Index = 0; Index < Count; Index++)
    {
        PTEST_OBJECT Object = &Objects[Index];
        LARGE_INTEGER Offset;
        PUCHAR Attribute;
        PUCHAR Value;
        ULONG ValueLength;

        Offset.QuadPart = VolumeData.MftStartLcn.QuadPart * VolumeData.BytesPerCluster +
                          Object->Record * VolumeData.BytesPerFileRecordSegment;
        if (!SetFilePointerEx(Volume, Offset, NULL, FILE_BEGIN) ||
            !ReadFile(Volume, Record, VolumeData.BytesPerFileRecordSegment, &Bytes, NULL) ||
            Bytes != VolumeData.BytesPerFileRecordSegment ||
            !ApplyFixups(Record, VolumeData.BytesPerFileRecordSegment, TRUE))
        {
            ok(FALSE, "Reading the record of %ls failed with %lu\n", Object->Name, GetLastError());
            goto Done;
        }

        Attribute = FindStandardInformation(Record, VolumeData.BytesPerFileRecordSegment,
                                            Object->Record, &ValueLength);
        ok(Attribute != NULL, "The record of %ls has an unexpected layout\n", Object->Name);
        if (!Attribute)
            goto Done;
        Value = Attribute + *(PUSHORT)(Attribute + 0x14);

        if (!Patch)
        {
            ok(ExpectedPatch(Object, ValueLength, Value),
               "The standard information of %ls was rewritten\n", Object->Name);
            continue;
        }

        if (Object->Patch == PatchV1)
            ShortenStandardInformation(Record, Attribute);
        else if (ValueLength >= NTFS_SI_SECURITY_ID + sizeof(ULONG))
            *(PULONG)(Value + NTFS_SI_SECURITY_ID) = Object->Patch == PatchBadId ? BAD_SECURITY_ID : 0;

        if (!ApplyFixups(Record, VolumeData.BytesPerFileRecordSegment, FALSE) ||
            !SetFilePointerEx(Volume, Offset, NULL, FILE_BEGIN) ||
            !WriteFile(Volume, Record, VolumeData.BytesPerFileRecordSegment, &Bytes, NULL) ||
            Bytes != VolumeData.BytesPerFileRecordSegment)
        {
            ok(FALSE, "Writing the record of %ls failed with %lu\n", Object->Name, GetLastError());
            goto Done;
        }
    }
    Result = TRUE;

Done:
    if (Record)
        VirtualFree(Record, 0, MEM_RELEASE);
    if (Locked)
        DeviceIoControl(Volume, FSCTL_UNLOCK_VOLUME, NULL, 0, NULL, 0, &Bytes, NULL);
    CloseHandle(Volume);
    return Result;
}

static BOOL
IsAdministrator(VOID)
{
    PSID Administrators = NULL;
    BOOL Member = FALSE;

    if (!ConvertStringSidToSidA("S-1-5-32-544", &Administrators))
        return FALSE;
    if (!CheckTokenMembership(NULL, Administrators, &Member))
        Member = FALSE;
    LocalFree(Administrators);
    return Member;
}

static PSECURITY_DESCRIPTOR
QuerySecurity(PCWSTR Path, PULONG Length)
{
    SECURITY_INFORMATION Information = OWNER_SECURITY_INFORMATION | GROUP_SECURITY_INFORMATION |
                                       DACL_SECURITY_INFORMATION;
    PSECURITY_DESCRIPTOR Descriptor;
    NTSTATUS Status;
    HANDLE Handle;

    *Length = 0;
    Handle = CreateFileW(Path, READ_CONTROL, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                         NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
    ok(Handle != INVALID_HANDLE_VALUE, "Opening %ls for READ_CONTROL failed with %lu\n", Path, GetLastError());
    if (Handle == INVALID_HANDLE_VALUE)
        return NULL;

    Status = NtQuerySecurityObject(Handle, Information, NULL, 0, Length);
    ok(Status == STATUS_BUFFER_TOO_SMALL, "NtQuerySecurityObject(%ls) returned 0x%lx\n", Path, Status);
    Descriptor = HeapAlloc(GetProcessHeap(), 0, *Length);
    if (Descriptor)
    {
        Status = NtQuerySecurityObject(Handle, Information, Descriptor, *Length, Length);
        ok(Status == STATUS_SUCCESS, "NtQuerySecurityObject(%ls) returned 0x%lx\n", Path, Status);
        if (Status != STATUS_SUCCESS)
        {
            HeapFree(GetProcessHeap(), 0, Descriptor);
            Descriptor = NULL;
        }
    }
    CloseHandle(Handle);
    return Descriptor;
}

static VOID
CheckAce(PACL Dacl, ULONG Index, PCSTR Sid, PCWSTR Path)
{
    PACCESS_ALLOWED_ACE Ace;
    PSID Expected = NULL;

    if (!GetAce(Dacl, Index, (PVOID *)&Ace))
    {
        ok(FALSE, "ACE %lu of %ls is missing\n", Index, Path);
        return;
    }
    ConvertStringSidToSidA(Sid, &Expected);
    ok(Ace->Header.AceType == ACCESS_ALLOWED_ACE_TYPE, "ACE %lu of %ls has type %u\n",
       Index, Path, Ace->Header.AceType);
    ok(Ace->Header.AceFlags == 0, "ACE %lu of %ls has flags 0x%x\n", Index, Path, Ace->Header.AceFlags);
    ok(Ace->Mask == FILE_ALL_ACCESS, "ACE %lu of %ls has mask 0x%lx\n", Index, Path, Ace->Mask);
    ok(Expected && EqualSid(&Ace->SidStart, Expected), "ACE %lu of %ls is not for %s\n", Index, Path, Sid);
    if (Expected)
        LocalFree(Expected);
}

static VOID
CheckDescriptor(PCWSTR Path, SECURITY_DESCRIPTOR_CONTROL ExpectedControl, ULONG ExpectedLength,
                ULONG AceCount, PCSTR Ace0, PCSTR Ace1)
{
    PSECURITY_DESCRIPTOR Descriptor;
    SECURITY_DESCRIPTOR_CONTROL Control = 0;
    DWORD Revision;
    PSID Owner = NULL;
    PSID Group = NULL;
    PSID Administrators = NULL;
    PSID System = NULL;
    PACL Dacl = NULL;
    BOOL Present = FALSE;
    BOOL Defaulted;
    ULONG Length;

    Descriptor = QuerySecurity(Path, &Length);
    if (!Descriptor)
        return;

    ConvertStringSidToSidA("S-1-5-32-544", &Administrators);
    ConvertStringSidToSidA("S-1-5-18", &System);
    GetSecurityDescriptorControl(Descriptor, &Control, &Revision);
    GetSecurityDescriptorOwner(Descriptor, &Owner, &Defaulted);
    GetSecurityDescriptorGroup(Descriptor, &Group, &Defaulted);
    GetSecurityDescriptorDacl(Descriptor, &Present, &Dacl, &Defaulted);

    ok(Length == ExpectedLength, "%ls: length %lu, expected %lu\n", Path, Length, ExpectedLength);
    ok(Control == ExpectedControl, "%ls: control 0x%x, expected 0x%x\n", Path, Control, ExpectedControl);
    ok(Owner && EqualSid(Owner, Administrators), "%ls: owner is not BUILTIN\\Administrators\n", Path);
    ok(Group && EqualSid(Group, System), "%ls: group is not SYSTEM\n", Path);
    ok(Present && Dacl, "%ls: no DACL\n", Path);
    if (Present && Dacl)
    {
        ok(Dacl->AceCount == AceCount, "%ls: %u ACEs, expected %lu\n", Path, Dacl->AceCount, AceCount);
        if (Dacl->AceCount == AceCount)
        {
            CheckAce(Dacl, 0, Ace0, Path);
            if (Ace1)
                CheckAce(Dacl, 1, Ace1, Path);
        }
    }

    LocalFree(Administrators);
    LocalFree(System);
    HeapFree(GetProcessHeap(), 0, Descriptor);
}

static PVOID
QueryToken(HANDLE Token, TOKEN_INFORMATION_CLASS Class)
{
    DWORD Length = 0;
    PVOID Buffer;

    GetTokenInformation(Token, Class, NULL, 0, &Length);
    Buffer = HeapAlloc(GetProcessHeap(), 0, Length);
    if (Buffer && !GetTokenInformation(Token, Class, Buffer, Length, &Length))
    {
        HeapFree(GetProcessHeap(), 0, Buffer);
        Buffer = NULL;
    }
    return Buffer;
}

static VOID
CheckTokenDefaultDescriptor(PCWSTR Path)
{
    PSECURITY_DESCRIPTOR Descriptor;
    PTOKEN_OWNER Owner;
    PTOKEN_PRIMARY_GROUP Group;
    PTOKEN_DEFAULT_DACL DefaultDacl;
    PSID ActualOwner = NULL;
    PSID ActualGroup = NULL;
    PACL Dacl = NULL;
    BOOL Present = FALSE;
    BOOL Defaulted;
    HANDLE Token;
    ULONG Length;
    ULONG Index;

    Descriptor = QuerySecurity(Path, &Length);
    if (!Descriptor)
        return;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &Token))
    {
        HeapFree(GetProcessHeap(), 0, Descriptor);
        return;
    }

    Owner = QueryToken(Token, TokenOwner);
    Group = QueryToken(Token, TokenPrimaryGroup);
    DefaultDacl = QueryToken(Token, TokenDefaultDacl);
    GetSecurityDescriptorOwner(Descriptor, &ActualOwner, &Defaulted);
    GetSecurityDescriptorGroup(Descriptor, &ActualGroup, &Defaulted);
    GetSecurityDescriptorDacl(Descriptor, &Present, &Dacl, &Defaulted);

    ok(Owner && ActualOwner && EqualSid(ActualOwner, Owner->Owner), "%ls: owner is not the token owner\n", Path);
    ok(Group && ActualGroup && EqualSid(ActualGroup, Group->PrimaryGroup),
       "%ls: group is not the token primary group\n", Path);
    ok(Present && Dacl && DefaultDacl && DefaultDacl->DefaultDacl &&
       Dacl->AceCount == DefaultDacl->DefaultDacl->AceCount,
       "%ls: DACL is not the token default DACL\n", Path);
    if (Present && Dacl && DefaultDacl && DefaultDacl->DefaultDacl &&
        Dacl->AceCount == DefaultDacl->DefaultDacl->AceCount)
    {
        GENERIC_MAPPING Mapping = { FILE_GENERIC_READ, FILE_GENERIC_WRITE, FILE_GENERIC_EXECUTE, FILE_ALL_ACCESS };

        for (Index = 0; Index < Dacl->AceCount; Index++)
        {
            PACCESS_ALLOWED_ACE Actual;
            PACCESS_ALLOWED_ACE Expected;
            ACCESS_MASK Mask;

            if (!GetAce(Dacl, Index, (PVOID *)&Actual) ||
                !GetAce(DefaultDacl->DefaultDacl, Index, (PVOID *)&Expected))
            {
                ok(FALSE, "%ls: ACE %lu is missing\n", Path, Index);
                continue;
            }
            Mask = Expected->Mask;
            MapGenericMask(&Mask, &Mapping);
            ok(Actual->Header.AceType == Expected->Header.AceType &&
               Actual->Header.AceFlags == Expected->Header.AceFlags &&
               Actual->Mask == Mask &&
               EqualSid(&Actual->SidStart, &Expected->SidStart),
               "%ls: ACE %lu does not match the token default DACL\n", Path, Index);
        }
    }

    HeapFree(GetProcessHeap(), 0, Owner);
    HeapFree(GetProcessHeap(), 0, Group);
    HeapFree(GetProcessHeap(), 0, DefaultDacl);
    CloseHandle(Token);
    HeapFree(GetProcessHeap(), 0, Descriptor);
}

static VOID
CheckOpen(PCWSTR Path, ACCESS_MASK Access, DWORD ExpectedError)
{
    HANDLE Handle;
    DWORD Error;

    SetLastError(0xdeadbeef);
    Handle = CreateFileW(Path, Access, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                         NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
    Error = Handle == INVALID_HANDLE_VALUE ? GetLastError() : ERROR_SUCCESS;
    ok(Error == ExpectedError, "Opening %ls for 0x%lx: error %lu, expected %lu\n",
       Path, Access, Error, ExpectedError);
    if (Handle != INVALID_HANDLE_VALUE)
        CloseHandle(Handle);
}

static VOID
CheckResult(BOOL Result, DWORD ExpectedError, PCSTR Operation)
{
    DWORD Error = Result ? ERROR_SUCCESS : GetLastError();

    ok(Error == ExpectedError, "%s: error %lu, expected %lu\n", Operation, Error, ExpectedError);
}

static HANDLE
CreateLimitedToken(VOID)
{
    SID_AND_ATTRIBUTES DenyOnly[2] = { { NULL, 0 }, { NULL, 0 } };
    PTOKEN_USER User;
    HANDLE Process;
    HANDLE Limited = NULL;

    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_DUPLICATE | TOKEN_QUERY | TOKEN_ASSIGN_PRIMARY, &Process))
        return NULL;
    User = QueryToken(Process, TokenUser);
    ConvertStringSidToSidA("S-1-5-32-544", &DenyOnly[0].Sid);
    if (User && DenyOnly[0].Sid)
    {
        DenyOnly[1].Sid = User->User.Sid;
        if (!CreateRestrictedToken(Process, DISABLE_MAX_PRIVILEGE | LUA_TOKEN, ARRAYSIZE(DenyOnly), DenyOnly,
                                   0, NULL, 0, NULL, &Limited))
        {
            Limited = NULL;
        }
    }
    LocalFree(DenyOnly[0].Sid);
    HeapFree(GetProcessHeap(), 0, User);
    CloseHandle(Process);
    return Limited;
}

static VOID
TestLimitedAccess(VOID)
{
    static const ACCESS_MASK FileAccess[] =
    {
        FILE_READ_DATA, FILE_WRITE_DATA, DELETE, READ_CONTROL, WRITE_DAC
    };
    static const ACCESS_MASK DirectoryAccess[] =
    {
        FILE_LIST_DIRECTORY, FILE_ADD_FILE, FILE_ADD_SUBDIRECTORY, FILE_DELETE_CHILD, FILE_TRAVERSE,
        DELETE, READ_CONTROL, WRITE_DAC
    };
    HANDLE Token;
    HANDLE Handle;
    ULONG Index;
    ULONG Access;

    Token = CreateLimitedToken();
    ok(Token != NULL, "CreateRestrictedToken failed with %lu\n", GetLastError());
    if (!Token)
        return;
    if (!ImpersonateLoggedOnUser(Token))
    {
        ok(FALSE, "ImpersonateLoggedOnUser failed with %lu\n", GetLastError());
        CloseHandle(Token);
        return;
    }

    CheckOpen(TestPath(L"control.txt"), FILE_READ_DATA, ERROR_SUCCESS);

    for (Index = 0; Index < ARRAYSIZE(Objects); Index++)
    {
        if (Index == SET_OBJECT)
            continue;
        if (Objects[Index].Directory)
        {
            for (Access = 0; Access < ARRAYSIZE(DirectoryAccess); Access++)
                CheckOpen(TestPath(Objects[Index].Name), DirectoryAccess[Access], ERROR_ACCESS_DENIED);
        }
        else
        {
            for (Access = 0; Access < ARRAYSIZE(FileAccess); Access++)
                CheckOpen(TestPath(Objects[Index].Name), FileAccess[Access], ERROR_ACCESS_DENIED);
        }
    }

    SetLastError(0xdeadbeef);
    Handle = CreateFileW(TestPath(L"zero_dir\\limited.txt"), GENERIC_WRITE, 0, NULL, CREATE_NEW, 0, NULL);
    CheckResult(Handle != INVALID_HANDLE_VALUE, ERROR_ACCESS_DENIED, "Creating a file in zero_dir");
    if (Handle != INVALID_HANDLE_VALUE)
    {
        CloseHandle(Handle);
        DeleteFileW(TestPath(L"zero_dir\\limited.txt"));
    }
    SetLastError(0xdeadbeef);
    CheckResult(CreateDirectoryW(TestPath(L"zero_dir\\limited"), NULL), ERROR_ACCESS_DENIED,
                "Creating a directory in zero_dir");
    SetLastError(0xdeadbeef);
    CheckResult(MoveFileW(TestPath(L"zero_dir\\inner.txt"), TestPath(L"zero_dir\\renamed.txt")),
                ERROR_ACCESS_DENIED, "Renaming a file in zero_dir");
    SetLastError(0xdeadbeef);
    CheckResult(DeleteFileW(TestPath(L"zero_dir\\inner.txt")), ERROR_SUCCESS,
                "Deleting a file that grants Everyone access in zero_dir");

    RevertToSelf();
    CloseHandle(Token);
}

static VOID
TestElevatedAccess(VOID)
{
    PSECURITY_DESCRIPTOR Descriptor = NULL;
    HANDLE Handle;
    ULONG Index;

    for (Index = 0; Index < ARRAYSIZE(Objects); Index++)
    {
        if (Index == SET_OBJECT)
            continue;
        CheckDescriptor(TestPath(Objects[Index].Name), SE_SELF_RELATIVE | SE_DACL_PRESENT, 100, 2,
                        "S-1-5-18", "S-1-5-32-544");
        CheckOpen(TestPath(Objects[Index].Name),
                  Objects[Index].Directory ?
                      FILE_LIST_DIRECTORY | FILE_ADD_FILE | FILE_ADD_SUBDIRECTORY | DELETE | WRITE_DAC :
                      FILE_READ_DATA | FILE_WRITE_DATA | DELETE | WRITE_DAC,
                  ERROR_SUCCESS);
    }

    ok(ConvertStringSecurityDescriptorToSecurityDescriptorW(L"D:P(A;;FA;;;WD)", SDDL_REVISION_1,
                                                            &Descriptor, NULL),
       "ConvertStringSecurityDescriptorToSecurityDescriptorW failed with %lu\n", GetLastError());
    if (Descriptor)
    {
        SetLastError(0xdeadbeef);
        CheckResult(SetFileSecurityW(TestPath(Objects[SET_OBJECT].Name), DACL_SECURITY_INFORMATION, Descriptor),
                    ERROR_SUCCESS, "SetFileSecurityW");
        LocalFree(Descriptor);
        CheckDescriptor(TestPath(Objects[SET_OBJECT].Name),
                        SE_SELF_RELATIVE | SE_DACL_PROTECTED | SE_DACL_PRESENT, 76, 1, "S-1-1-0", NULL);
    }

    Handle = CreateFileW(TestPath(L"zero_dir\\elevated.txt"), GENERIC_WRITE, 0, NULL, CREATE_NEW, 0, NULL);
    ok(Handle != INVALID_HANDLE_VALUE, "Creating a file in zero_dir failed with %lu\n", GetLastError());
    if (Handle != INVALID_HANDLE_VALUE)
    {
        CloseHandle(Handle);
        CheckTokenDefaultDescriptor(TestPath(L"zero_dir\\elevated.txt"));
    }
}

START_TEST(MissingSecurity)
{
    if (!FindTestVolume())
    {
        skip("No NTFS volume labelled %ls\n", TEST_VOLUME_LABEL);
        return;
    }
    if (!IsAdministrator())
    {
        skip("The test must run elevated\n");
        return;
    }

    RemoveTestFiles();
    if (!CreateTestObjects())
    {
        ok(FALSE, "Creating the test files failed with %lu\n", GetLastError());
        RemoveTestFiles();
        return;
    }

    if (AccessRecords(TRUE, ARRAYSIZE(Objects)))
    {
        TestLimitedAccess();
        TestElevatedAccess();
        AccessRecords(FALSE, SET_OBJECT);
    }

    RemoveTestFiles();
}
