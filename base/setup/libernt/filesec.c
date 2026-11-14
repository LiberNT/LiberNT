/*
 * PROJECT:     LiberNT Setup
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Give the files Setup installs the security of the preinstalled image
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "libernt.h"
#include <sddl.h>
#include <ndk/obfuncs.h>
#include <ndk/setypes.h>

#include "resource.h"

#define NDEBUG
#include <debug.h>

#define FILESEC_MAX_PATH        2048
#define FILESEC_MAX_VALUE       8192
#define FILESEC_LIST_BUFFER     0x10000

typedef struct _FILESEC_DEFINE
{
    PWSTR Name;
    PWSTR Value;
} FILESEC_DEFINE, *PFILESEC_DEFINE;

typedef struct _FILESEC_RULE
{
    PWSTR Path;
    SIZE_T PathLength;
    BOOLEAN Directory;
    BOOLEAN Recursive;
    PSECURITY_DESCRIPTOR Descriptor;
} FILESEC_RULE, *PFILESEC_RULE;

struct _FILESEC_POLICY
{
    PFILESEC_RULE Rules;
    ULONG Count;
    ULONG Capacity;
    PFILESEC_DEFINE Defines;
    ULONG DefineCount;
    ULONG DefineCapacity;
};

typedef struct _FILESEC_WALK
{
    PFILESEC_POLICY Policy;
    WCHAR Path[FILESEC_MAX_PATH];
    SIZE_T Length;
    PCWSTR SystemDirectory;
    NTSTATUS Status;
    ULONG Applied;
    PVOID ListBuffer;
} FILESEC_WALK, *PFILESEC_WALK;

static PWSTR
FileSecDuplicate(
    _In_reads_(Length) PCWSTR Text,
    _In_ SIZE_T Length)
{
    PWSTR Copy = HeapAlloc(GetProcessHeap(), 0, (Length + 1) * sizeof(WCHAR));

    if (Copy)
    {
        CopyMemory(Copy, Text, Length * sizeof(WCHAR));
        Copy[Length] = UNICODE_NULL;
    }
    return Copy;
}

static BOOLEAN
FileSecGrow(
    _Inout_ PVOID *Array,
    _Inout_ PULONG Capacity,
    _In_ ULONG Count,
    _In_ SIZE_T ElementSize)
{
    ULONG NewCapacity;
    PVOID NewArray;

    if (Count < *Capacity)
        return TRUE;

    NewCapacity = *Capacity ? *Capacity * 2 : 32;
    NewArray = *Array ? HeapReAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, *Array, NewCapacity * ElementSize)
                      : HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, NewCapacity * ElementSize);
    if (!NewArray)
        return FALSE;

    *Array = NewArray;
    *Capacity = NewCapacity;
    return TRUE;
}

/* Same form as the image builder: '/' separators, no leading or trailing one, lower case */
static VOID
FileSecNormalizePath(
    _Inout_ PWSTR Path)
{
    PWSTR Read = Path;
    PWSTR Write = Path;
    SIZE_T Length;

    while (*Read == L'/' || *Read == L'\\')
        Read++;
    while (*Read)
    {
        *Write++ = (*Read == L'\\') ? L'/' : towlower(*Read);
        Read++;
    }
    *Write = UNICODE_NULL;

    Length = Write - Path;
    while (Length > 0 && Path[Length - 1] == L'/')
        Path[--Length] = UNICODE_NULL;
}

static PCWSTR
FileSecFindDefine(
    _In_ PFILESEC_POLICY Policy,
    _In_reads_(Length) PCWSTR Name,
    _In_ SIZE_T Length)
{
    ULONG Index;

    for (Index = 0; Index < Policy->DefineCount; Index++)
    {
        if (wcslen(Policy->Defines[Index].Name) == Length &&
            wcsncmp(Policy->Defines[Index].Name, Name, Length) == 0)
        {
            return Policy->Defines[Index].Value;
        }
    }
    return NULL;
}

static BOOLEAN
FileSecExpand(
    _In_ PFILESEC_POLICY Policy,
    _In_ PCWSTR Value,
    _Out_writes_(FILESEC_MAX_VALUE) PWSTR Expanded)
{
    PCWSTR Open, Close, Definition;
    SIZE_T Used = 0, Length;

    while (*Value)
    {
        Open = wcschr(Value, L'{');
        if (!Open)
            Open = Value + wcslen(Value);

        Length = Open - Value;
        if (Used + Length >= FILESEC_MAX_VALUE)
            return FALSE;
        CopyMemory(Expanded + Used, Value, Length * sizeof(WCHAR));
        Used += Length;
        if (!*Open)
            break;

        Close = wcschr(Open, L'}');
        if (!Close)
            return FALSE;
        Definition = FileSecFindDefine(Policy, Open + 1, Close - Open - 1);
        if (!Definition)
            return FALSE;

        Length = wcslen(Definition);
        if (Used + Length >= FILESEC_MAX_VALUE)
            return FALSE;
        CopyMemory(Expanded + Used, Definition, Length * sizeof(WCHAR));
        Used += Length;
        Value = Close + 1;
    }

    Expanded[Used] = UNICODE_NULL;
    return TRUE;
}

static BOOLEAN
FileSecParseLine(
    _In_ PFILESEC_POLICY Policy,
    _Inout_ PWSTR Line,
    _Inout_updates_(FILESEC_MAX_VALUE) PWSTR Expanded)
{
    PWSTR Space, Separator, Name;
    SECURITY_DESCRIPTOR_CONTROL Control;
    PFILESEC_RULE Rule;
    BOOLEAN Directory, Recursive;
    SIZE_T Length;
    DWORD Revision;

    Length = wcslen(Line);
    while (Length > 0 && (Line[Length - 1] == L'\r' || Line[Length - 1] == L' '))
        Line[--Length] = UNICODE_NULL;
    if (Length == 0)
        return TRUE;

    Separator = wcschr(Line, L'=');
    Space = wcschr(Line, L' ');
    if (!Separator || !Space || Space > Separator)
        return FALSE;

    *Space = UNICODE_NULL;
    *Separator = UNICODE_NULL;
    Name = Space + 1;
    if (!FileSecExpand(Policy, Separator + 1, Expanded))
        return FALSE;

    if (wcscmp(Line, L"define") == 0)
    {
        if (!FileSecGrow((PVOID*)&Policy->Defines, &Policy->DefineCapacity,
                         Policy->DefineCount, sizeof(FILESEC_DEFINE)))
            return FALSE;
        Policy->Defines[Policy->DefineCount].Name = FileSecDuplicate(Name, wcslen(Name));
        Policy->Defines[Policy->DefineCount].Value = FileSecDuplicate(Expanded, wcslen(Expanded));
        if (!Policy->Defines[Policy->DefineCount].Name || !Policy->Defines[Policy->DefineCount].Value)
            return FALSE;
        Policy->DefineCount++;
        return TRUE;
    }

    if (wcscmp(Line, L"dir") == 0 || wcscmp(Line, L"dirs") == 0)
        Directory = TRUE;
    else if (wcscmp(Line, L"file") == 0 || wcscmp(Line, L"files") == 0)
        Directory = FALSE;
    else
        return FALSE;
    Recursive = (Line[wcslen(Line) - 1] == L's');

    if (!FileSecGrow((PVOID*)&Policy->Rules, &Policy->Capacity, Policy->Count, sizeof(FILESEC_RULE)))
        return FALSE;
    Rule = &Policy->Rules[Policy->Count];
    Rule->Path = FileSecDuplicate(Name, wcslen(Name));
    if (!Rule->Path)
        return FALSE;
    FileSecNormalizePath(Rule->Path);
    Rule->PathLength = wcslen(Rule->Path);
    Rule->Directory = Directory;
    Rule->Recursive = Recursive;
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(Expanded, SDDL_REVISION_1,
                                                              &Rule->Descriptor, NULL))
    {
        DPRINT1("Invalid descriptor for '%S' (error %lu)\n", Rule->Path, GetLastError());
        HeapFree(GetProcessHeap(), 0, Rule->Path);
        Rule->Path = NULL;
        return FALSE;
    }

    /* Without the request the kernel drops the auto-inherited mark of a list that is set */
    if (GetSecurityDescriptorControl(Rule->Descriptor, &Control, &Revision) &&
        (Control & SE_DACL_AUTO_INHERITED))
    {
        SetSecurityDescriptorControl(Rule->Descriptor, SE_DACL_AUTO_INHERIT_REQ, SE_DACL_AUTO_INHERIT_REQ);
    }
    Policy->Count++;
    return TRUE;
}

VOID
FileSecurityFreePolicy(
    _In_opt_ PFILESEC_POLICY Policy)
{
    ULONG Index;

    if (!Policy)
        return;

    for (Index = 0; Index < Policy->Count; Index++)
    {
        HeapFree(GetProcessHeap(), 0, Policy->Rules[Index].Path);
        LocalFree(Policy->Rules[Index].Descriptor);
    }
    for (Index = 0; Index < Policy->DefineCount; Index++)
    {
        HeapFree(GetProcessHeap(), 0, Policy->Defines[Index].Name);
        HeapFree(GetProcessHeap(), 0, Policy->Defines[Index].Value);
    }
    HeapFree(GetProcessHeap(), 0, Policy->Rules);
    HeapFree(GetProcessHeap(), 0, Policy->Defines);
    HeapFree(GetProcessHeap(), 0, Policy);
}

DWORD
FileSecurityLoadPolicy(
    _Out_ PFILESEC_POLICY *Policy)
{
    PFILESEC_POLICY NewPolicy = NULL;
    PWSTR Text = NULL, Expanded = NULL, Line, Next;
    HRSRC Resource;
    HGLOBAL Loaded;
    PCSTR Bytes;
    DWORD Size, Error = ERROR_SUCCESS;
    int Length;

    *Policy = NULL;

    Resource = FindResourceW(NULL, MAKEINTRESOURCEW(IDR_FILESECURITY), (LPCWSTR)RT_RCDATA);
    Loaded = Resource ? LoadResource(NULL, Resource) : NULL;
    Bytes = Loaded ? LockResource(Loaded) : NULL;
    Size = Resource ? SizeofResource(NULL, Resource) : 0;
    if (!Bytes || Size == 0)
        return ERROR_RESOURCE_DATA_NOT_FOUND;

    Length = MultiByteToWideChar(CP_UTF8, 0, Bytes, Size, NULL, 0);
    NewPolicy = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*NewPolicy));
    Text = HeapAlloc(GetProcessHeap(), 0, (Length + 1) * sizeof(WCHAR));
    Expanded = HeapAlloc(GetProcessHeap(), 0, FILESEC_MAX_VALUE * sizeof(WCHAR));
    if (Length <= 0 || !NewPolicy || !Text || !Expanded)
    {
        Error = ERROR_NOT_ENOUGH_MEMORY;
    }
    else
    {
        MultiByteToWideChar(CP_UTF8, 0, Bytes, Size, Text, Length);
        Text[Length] = UNICODE_NULL;

        for (Line = Text; Line && *Line; Line = Next)
        {
            Next = wcschr(Line, L'\n');
            if (Next)
                *Next++ = UNICODE_NULL;
            if (!FileSecParseLine(NewPolicy, Line, Expanded))
            {
                DPRINT1("Invalid file security rule '%S'\n", Line);
                Error = ERROR_INVALID_DATA;
                break;
            }
        }
    }

    HeapFree(GetProcessHeap(), 0, Expanded);
    HeapFree(GetProcessHeap(), 0, Text);
    if (Error != ERROR_SUCCESS)
    {
        FileSecurityFreePolicy(NewPolicy);
        return Error;
    }

    *Policy = NewPolicy;
    return ERROR_SUCCESS;
}

/* An exact rule wins; otherwise the deepest recursive rule above the path */
PSECURITY_DESCRIPTOR
FileSecurityFindDescriptor(
    _In_ PFILESEC_POLICY Policy,
    _In_ PCWSTR Path,
    _In_ BOOLEAN Directory)
{
    PFILESEC_RULE Best = NULL;
    PFILESEC_RULE Rule;
    SIZE_T PathLength = wcslen(Path);
    ULONG Index;

    for (Index = 0; Index < Policy->Count; Index++)
    {
        Rule = &Policy->Rules[Index];
        if (Rule->Directory != Directory)
            continue;
        if (!Rule->Recursive)
        {
            if (_wcsicmp(Rule->Path, Path) == 0)
                Best = Rule;
            continue;
        }
        if (Best && (!Best->Recursive || Best->PathLength > Rule->PathLength))
            continue;
        if (Rule->PathLength == 0 ? PathLength != 0
                                  : (PathLength > Rule->PathLength &&
                                     Path[Rule->PathLength] == L'/' &&
                                     _wcsnicmp(Path, Rule->Path, Rule->PathLength) == 0))
        {
            Best = Rule;
        }
    }

    return Best ? Best->Descriptor : NULL;
}

static NTSTATUS
FileSecApplyToHandle(
    _Inout_ PFILESEC_WALK Walk,
    _In_ HANDLE Handle,
    _In_ BOOLEAN Directory)
{
    PSECURITY_DESCRIPTOR Descriptor;
    NTSTATUS Status;

    Descriptor = FileSecurityFindDescriptor(Walk->Policy, Walk->Path, Directory);
    if (!Descriptor)
        return STATUS_SUCCESS;

    Status = NtSetSecurityObject(Handle,
                                 OWNER_SECURITY_INFORMATION | GROUP_SECURITY_INFORMATION |
                                 DACL_SECURITY_INFORMATION,
                                 Descriptor);
    if (NT_SUCCESS(Status))
        Walk->Applied++;
    else
        DPRINT1("Setting the security of '%S' failed (Status 0x%08lx)\n", Walk->Path, Status);
    return Status;
}

static VOID
FileSecRecordFailure(
    _Inout_ PFILESEC_WALK Walk,
    _In_ NTSTATUS Status)
{
    if (!NT_SUCCESS(Status) && NT_SUCCESS(Walk->Status))
        Walk->Status = Status;
}

static NTSTATUS
FileSecOpen(
    _Out_ PHANDLE Handle,
    _In_opt_ HANDLE Parent,
    _In_ PUNICODE_STRING Name,
    _In_ BOOLEAN List)
{
    OBJECT_ATTRIBUTES ObjectAttributes;
    IO_STATUS_BLOCK IoStatusBlock;

    InitializeObjectAttributes(&ObjectAttributes, Name, OBJ_CASE_INSENSITIVE, Parent, NULL);
    return NtOpenFile(Handle,
                      READ_CONTROL | WRITE_DAC | WRITE_OWNER | SYNCHRONIZE |
                      (List ? FILE_LIST_DIRECTORY : 0),
                      &ObjectAttributes,
                      &IoStatusBlock,
                      FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                      FILE_OPEN_FOR_BACKUP_INTENT | FILE_OPEN_REPARSE_POINT |
                      FILE_SYNCHRONOUS_IO_NONALERT);
}

static VOID
FileSecWalkDirectory(
    _Inout_ PFILESEC_WALK Walk,
    _In_ HANDLE Directory);

static VOID
FileSecVisit(
    _Inout_ PFILESEC_WALK Walk,
    _In_ HANDLE Parent,
    _In_reads_(NameLength) PCWSTR Name,
    _In_ SIZE_T NameLength,
    _In_ ULONG Attributes)
{
    BOOLEAN Directory = !!(Attributes & FILE_ATTRIBUTE_DIRECTORY);
    BOOLEAN Descend = Directory && !(Attributes & FILE_ATTRIBUTE_REPARSE_POINT);
    SIZE_T SavedLength = Walk->Length;
    UNICODE_STRING NameU;
    HANDLE Handle;
    NTSTATUS Status;
    SIZE_T Index;

    if (Walk->Length + 1 + NameLength >= ARRAYSIZE(Walk->Path))
    {
        FileSecRecordFailure(Walk, STATUS_NAME_TOO_LONG);
        return;
    }

    if (Walk->Length != 0)
        Walk->Path[Walk->Length++] = L'/';
    if (SavedLength == 0 && Walk->SystemDirectory &&
        wcslen(Walk->SystemDirectory) == NameLength &&
        _wcsnicmp(Walk->SystemDirectory, Name, NameLength) == 0)
    {
        /* The rules name the system directory as the image does */
        StringCchCopyW(Walk->Path, ARRAYSIZE(Walk->Path), L"windows");
        Walk->Length = wcslen(Walk->Path);
    }
    else
    {
        for (Index = 0; Index < NameLength; Index++)
            Walk->Path[Walk->Length++] = towlower(Name[Index]);
        Walk->Path[Walk->Length] = UNICODE_NULL;
    }

    NameU.Buffer = (PWSTR)Name;
    NameU.Length = NameU.MaximumLength = (USHORT)(NameLength * sizeof(WCHAR));
    Status = FileSecOpen(&Handle, Parent, &NameU, Descend);
    if (NT_SUCCESS(Status))
    {
        FileSecRecordFailure(Walk, FileSecApplyToHandle(Walk, Handle, Directory));
        if (Descend)
            FileSecWalkDirectory(Walk, Handle);
        NtClose(Handle);
    }
    else
    {
        DPRINT1("Opening '%S' failed (Status 0x%08lx)\n", Walk->Path, Status);
        FileSecRecordFailure(Walk, Status);
    }

    Walk->Length = SavedLength;
    Walk->Path[SavedLength] = UNICODE_NULL;
}

static VOID
FileSecWalkDirectory(
    _Inout_ PFILESEC_WALK Walk,
    _In_ HANDLE Directory)
{
    PFILE_DIRECTORY_INFORMATION Entry;
    IO_STATUS_BLOCK IoStatusBlock;
    PVOID Buffer;
    BOOLEAN Restart = TRUE;
    NTSTATUS Status;
    SIZE_T NameLength;

    Buffer = HeapAlloc(GetProcessHeap(), 0, FILESEC_LIST_BUFFER);
    if (!Buffer)
    {
        FileSecRecordFailure(Walk, STATUS_INSUFFICIENT_RESOURCES);
        return;
    }

    for (;;)
    {
        Status = NtQueryDirectoryFile(Directory, NULL, NULL, NULL, &IoStatusBlock,
                                      Buffer, FILESEC_LIST_BUFFER, FileDirectoryInformation,
                                      FALSE, NULL, Restart);
        Restart = FALSE;
        if (Status == STATUS_NO_MORE_FILES)
            break;
        if (!NT_SUCCESS(Status))
        {
            FileSecRecordFailure(Walk, Status);
            break;
        }

        for (Entry = Buffer; ; Entry = (PFILE_DIRECTORY_INFORMATION)((PUCHAR)Entry + Entry->NextEntryOffset))
        {
            NameLength = Entry->FileNameLength / sizeof(WCHAR);
            if (!(NameLength == 1 && Entry->FileName[0] == L'.') &&
                !(NameLength == 2 && Entry->FileName[0] == L'.' && Entry->FileName[1] == L'.'))
            {
                FileSecVisit(Walk, Directory, Entry->FileName, NameLength, Entry->FileAttributes);
            }
            if (Entry->NextEntryOffset == 0)
                break;
        }
    }

    HeapFree(GetProcessHeap(), 0, Buffer);
}

NTSTATUS
FileSecurityApply(
    _In_ PFILESEC_POLICY Policy,
    _In_ PCWSTR VolumeRoot,
    _In_ PCWSTR InstallationDirectory,
    _In_ BOOLEAN WholeVolume)
{
    PFILESEC_WALK Walk;
    UNICODE_STRING RootU;
    HANDLE Root;
    NTSTATUS Status;
    PCWSTR SystemDirectory = InstallationDirectory;

    while (*SystemDirectory == L'\\' || *SystemDirectory == L'/')
        SystemDirectory++;
    if (*SystemDirectory == UNICODE_NULL || wcspbrk(SystemDirectory, L"\\/"))
    {
        /* TODO: a system directory below the volume root keeps the security it inherits */
        DPRINT1("No file security rules for system directory '%S'\n", InstallationDirectory);
        return STATUS_SUCCESS;
    }

    Walk = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*Walk));
    if (!Walk)
        return STATUS_INSUFFICIENT_RESOURCES;
    Walk->Policy = Policy;
    Walk->SystemDirectory = SystemDirectory;

    RtlInitUnicodeString(&RootU, VolumeRoot);
    Status = FileSecOpen(&Root, NULL, &RootU, TRUE);
    if (!NT_SUCCESS(Status))
    {
        HeapFree(GetProcessHeap(), 0, Walk);
        return Status;
    }

    if (WholeVolume)
    {
        FileSecRecordFailure(Walk, FileSecApplyToHandle(Walk, Root, TRUE));
        FileSecWalkDirectory(Walk, Root);
    }
    else
    {
        /* TODO: on a volume Setup did not format, only the system directory gets the image security */
        FileSecVisit(Walk, Root, SystemDirectory, wcslen(SystemDirectory), FILE_ATTRIBUTE_DIRECTORY);
    }
    NtClose(Root);

    DPRINT1("File security applied to %lu items under %S (Status 0x%08lx)\n",
            Walk->Applied, VolumeRoot, Walk->Status);
    Status = Walk->Status;
    HeapFree(GetProcessHeap(), 0, Walk);
    return Status;
}

NTSTATUS
InstallApplyFileSecurity(
    _In_ PCWSTR VolumeRoot,
    _In_ PCWSTR InstallationDirectory,
    _In_ BOOLEAN WholeVolume)
{
    BOOLEAN Restore = FALSE, Backup = FALSE, Owner = FALSE;
    PFILESEC_POLICY Policy;
    NTSTATUS Status;
    DWORD Error;

    Error = FileSecurityLoadPolicy(&Policy);
    if (Error != ERROR_SUCCESS)
    {
        DPRINT1("Loading the file security policy failed (error %lu)\n", Error);
        return STATUS_INVALID_IMAGE_FORMAT;
    }

    RtlAdjustPrivilege(SE_RESTORE_PRIVILEGE, TRUE, FALSE, &Restore);
    RtlAdjustPrivilege(SE_BACKUP_PRIVILEGE, TRUE, FALSE, &Backup);
    RtlAdjustPrivilege(SE_TAKE_OWNERSHIP_PRIVILEGE, TRUE, FALSE, &Owner);

    Status = FileSecurityApply(Policy, VolumeRoot, InstallationDirectory, WholeVolume);

    RtlAdjustPrivilege(SE_TAKE_OWNERSHIP_PRIVILEGE, Owner, FALSE, &Owner);
    RtlAdjustPrivilege(SE_BACKUP_PRIVILEGE, Backup, FALSE, &Backup);
    RtlAdjustPrivilege(SE_RESTORE_PRIVILEGE, Restore, FALSE, &Restore);

    FileSecurityFreePolicy(Policy);
    return Status;
}

/* EOF */
