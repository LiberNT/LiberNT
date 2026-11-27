/*
 * PROJECT:     LiberNT NT User-Mode Library
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     DLL directories and the search paths built from them
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntdll.h>
#define NDEBUG
#include <debug.h>

#define LDRP_SEARCH_FLAGS (LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_APPLICATION_DIR | \
                           LOAD_LIBRARY_SEARCH_USER_DIRS | LOAD_LIBRARY_SEARCH_SYSTEM32 | \
                           LOAD_LIBRARY_SEARCH_DEFAULT_DIRS)

typedef struct _LDRP_USER_DLL_DIRECTORY
{
    LIST_ENTRY Link;
    WCHAR Path[ANYSIZE_ARRAY];
} LDRP_USER_DLL_DIRECTORY, *PLDRP_USER_DLL_DIRECTORY;

static RTL_SRWLOCK LdrpDllDirectoryLock = RTL_SRWLOCK_INIT;
static UNICODE_STRING LdrpDllDirectory;
static LIST_ENTRY LdrpUserDllDirectories = { &LdrpUserDllDirectories, &LdrpUserDllDirectories };
static ULONG LdrpDefaultDllDirectories;
static LONG LdrpSafeDllSearchMode = -1;

static
BOOLEAN
LdrpSafeDllSearch(VOID)
{
    UNICODE_STRING KeyName = RTL_CONSTANT_STRING(L"\\Registry\\Machine\\System\\CurrentControlSet\\Control\\Session Manager");
    UNICODE_STRING ValueName = RTL_CONSTANT_STRING(L"SafeDllSearchMode");
    OBJECT_ATTRIBUTES ObjectAttributes = RTL_CONSTANT_OBJECT_ATTRIBUTES(&KeyName, OBJ_CASE_INSENSITIVE);
    UCHAR Buffer[FIELD_OFFSET(KEY_VALUE_PARTIAL_INFORMATION, Data) + sizeof(ULONG)];
    PKEY_VALUE_PARTIAL_INFORMATION Information = (PKEY_VALUE_PARTIAL_INFORMATION)Buffer;
    LONG Mode = LdrpSafeDllSearchMode;
    HANDLE Key;
    ULONG Length;

    if (Mode >= 0)
        return Mode != 0;

    Mode = 1;
    if (NT_SUCCESS(NtOpenKey(&Key, KEY_QUERY_VALUE, &ObjectAttributes)))
    {
        if (NT_SUCCESS(NtQueryValueKey(Key, &ValueName, KeyValuePartialInformation, Buffer, sizeof(Buffer), &Length)) &&
            Information->Type == REG_DWORD && Information->DataLength == sizeof(ULONG) &&
            *(PULONG)Information->Data == 0)
        {
            Mode = 0;
        }
        NtClose(Key);
    }

    InterlockedCompareExchange(&LdrpSafeDllSearchMode, Mode, -1);
    return LdrpSafeDllSearchMode != 0;
}

static
SIZE_T
LdrpDirectoryLength(
    _In_z_ PCWSTR Path)
{
    PCWSTR End = Path, Separator;

    if ((Separator = wcsrchr(End, L'\\')))
        End = Separator;
    if ((Separator = wcsrchr(End, L'/')))
        End = Separator;
    if (End == Path + 2 && Path[1] == L':')
        End++;
    if (End == Path && Path[0] && Path[1] == L':')
        End += 2;
    return End - Path;
}

static
SIZE_T
LdrpSystemDirectoriesLength(VOID)
{
    SIZE_T Root = wcslen(SharedUserData->NtSystemRoot);

    return (Root + sizeof("\\System32;") - 1) + (Root + sizeof("\\System;") - 1) + Root;
}

static
PWSTR
LdrpAppendDirectory(
    _Out_ PWSTR Destination,
    _In_reads_(Length) PCWSTR Directory,
    _In_ SIZE_T Length)
{
    if (Length == 0)
        return Destination;
    RtlCopyMemory(Destination, Directory, Length * sizeof(WCHAR));
    Destination[Length] = L';';
    return Destination + Length + 1;
}

static
PWSTR
LdrpAppendSystemDirectories(
    _Out_ PWSTR Destination)
{
    PCWSTR Root = SharedUserData->NtSystemRoot;
    SIZE_T RootLength = wcslen(Root);

    RtlCopyMemory(Destination, Root, RootLength * sizeof(WCHAR));
    RtlCopyMemory(Destination + RootLength, L"\\System32;", 10 * sizeof(WCHAR));
    Destination += RootLength + 10;
    RtlCopyMemory(Destination, Root, RootLength * sizeof(WCHAR));
    RtlCopyMemory(Destination + RootLength, L"\\System;", 8 * sizeof(WCHAR));
    Destination += RootLength + 8;
    RtlCopyMemory(Destination, Root, RootLength * sizeof(WCHAR));
    Destination[RootLength] = L';';
    return Destination + RootLength + 1;
}

static
BOOLEAN
LdrpPrefersSystem32(VOID)
{
    struct
    {
        ULONG Policy;
        ULONG Flags;
    } ImageLoadPolicy = { ProcessImageLoadPolicy, 0 };

    return NT_SUCCESS(NtQueryInformationProcess(NtCurrentProcess(), ProcessMitigationPolicy, &ImageLoadPolicy,
                                                sizeof(ImageLoadPolicy), NULL)) &&
           (ImageLoadPolicy.Flags & 4);
}

static
NTSTATUS
LdrpBuildLoadPath(
    _In_opt_z_ PCWSTR Module,
    _In_opt_ PCUNICODE_STRING DllDirectory,
    _In_ BOOLEAN SafeMode,
    _In_ BOOLEAN PreferSystem32,
    _Outptr_ PWSTR *Path)
{
    UNICODE_STRING Name = RTL_CONSTANT_STRING(L"PATH"), Value;
    SIZE_T ModuleLength = Module ? LdrpDirectoryLength(Module) : 0;
    SIZE_T Length, EnvLength = 0;
    PWSTR Buffer, Current;
    NTSTATUS Status;

    *Path = NULL;

    RtlInitEmptyUnicodeString(&Value, NULL, 0);
    Status = RtlQueryEnvironmentVariable_U(NULL, &Name, &Value);
    if (Status == STATUS_BUFFER_TOO_SMALL)
        EnvLength = Value.Length / sizeof(WCHAR);

    Length = ModuleLength + 1 + LdrpSystemDirectoriesLength() + 1 + 2 + EnvLength + 1;
    if (DllDirectory)
        Length += DllDirectory->Length / sizeof(WCHAR) + 1;
    if (PreferSystem32)
        Length += wcslen(SharedUserData->NtSystemRoot) + sizeof("\\System32;") - 1;

    for (;;)
    {
        Buffer = RtlAllocateHeap(RtlGetProcessHeap(), 0, Length * sizeof(WCHAR));
        if (!Buffer)
            return STATUS_NO_MEMORY;

        Current = Buffer;
        if (PreferSystem32)
        {
            SIZE_T RootLength = wcslen(SharedUserData->NtSystemRoot);

            RtlCopyMemory(Current, SharedUserData->NtSystemRoot, RootLength * sizeof(WCHAR));
            RtlCopyMemory(Current + RootLength, L"\\System32;", 10 * sizeof(WCHAR));
            Current += RootLength + 10;
        }
        Current = LdrpAppendDirectory(Current, Module, ModuleLength);
        if (DllDirectory)
            Current = LdrpAppendDirectory(Current, DllDirectory->Buffer, DllDirectory->Length / sizeof(WCHAR));
        else if (!SafeMode)
            Current = LdrpAppendDirectory(Current, L".", 1);
        Current = LdrpAppendSystemDirectories(Current);
        if (!DllDirectory && SafeMode)
            Current = LdrpAppendDirectory(Current, L".", 1);

        ASSERT(Current + EnvLength + 1 <= Buffer + Length);
        Value.Buffer = Current;
        Value.Length = 0;
        Value.MaximumLength = (USHORT)min((Buffer + Length - Current - 1) * sizeof(WCHAR), MAXUSHORT);
        Status = RtlQueryEnvironmentVariable_U(NULL, &Name, &Value);
        if (Status == STATUS_BUFFER_TOO_SMALL)
        {
            RtlFreeHeap(RtlGetProcessHeap(), 0, Buffer);
            Length += Value.Length / sizeof(WCHAR) + 1;
            continue;
        }
        if (NT_SUCCESS(Status))
        {
            Current[Value.Length / sizeof(WCHAR)] = UNICODE_NULL;
        }
        else
        {
            if (Current > Buffer)
                Current--;
            *Current = UNICODE_NULL;
        }
        break;
    }

    *Path = Buffer;
    return STATUS_SUCCESS;
}

_Requires_lock_held_(LdrpDllDirectoryLock)
static
NTSTATUS
LdrpBuildSearchFlagsPath(
    _In_opt_z_ PCWSTR Module,
    _In_ ULONG Flags,
    _Outptr_ PWSTR *Path)
{
    PCWSTR Image = NULL;
    SIZE_T ModuleLength = 0, ImageLength = 0, Length = 1;
    PLDRP_USER_DLL_DIRECTORY Entry;
    PLIST_ENTRY Link;
    PWSTR Buffer, Current;

    *Path = NULL;

    if (Flags & LOAD_LIBRARY_SEARCH_DEFAULT_DIRS)
        Flags |= LOAD_LIBRARY_SEARCH_APPLICATION_DIR | LOAD_LIBRARY_SEARCH_USER_DIRS | LOAD_LIBRARY_SEARCH_SYSTEM32;

    if (Flags & LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR)
    {
        RTL_PATH_TYPE Type = Module ? RtlDetermineDosPathNameType_U(Module) : RtlPathTypeUnknown;

        if (Type != RtlPathTypeDriveAbsolute && Type != RtlPathTypeRooted &&
            Type != RtlPathTypeLocalDevice && Type != RtlPathTypeUncAbsolute)
        {
            return STATUS_INVALID_PARAMETER;
        }
        ModuleLength = LdrpDirectoryLength(Module);
        Length += ModuleLength + 1;
    }

    if (Flags & LOAD_LIBRARY_SEARCH_APPLICATION_DIR)
    {
        Image = NtCurrentPeb()->ProcessParameters->ImagePathName.Buffer;
        ImageLength = LdrpDirectoryLength(Image);
        Length += ImageLength + 1;
    }

    if (Flags & LOAD_LIBRARY_SEARCH_USER_DIRS)
    {
        for (Link = LdrpUserDllDirectories.Flink; Link != &LdrpUserDllDirectories; Link = Link->Flink)
            Length += wcslen(CONTAINING_RECORD(Link, LDRP_USER_DLL_DIRECTORY, Link)->Path) + 1;
        Length += LdrpDllDirectory.Length / sizeof(WCHAR) + 1;
    }

    if (Flags & LOAD_LIBRARY_SEARCH_SYSTEM32)
        Length += wcslen(SharedUserData->NtSystemRoot) + sizeof("\\System32;") - 1;

    Buffer = RtlAllocateHeap(RtlGetProcessHeap(), 0, Length * sizeof(WCHAR));
    if (!Buffer)
        return STATUS_NO_MEMORY;

    Current = Buffer;
    if (ModuleLength)
        Current = LdrpAppendDirectory(Current, Module, ModuleLength);
    if (ImageLength)
        Current = LdrpAppendDirectory(Current, Image, ImageLength);
    if (Flags & LOAD_LIBRARY_SEARCH_USER_DIRS)
    {
        for (Link = LdrpUserDllDirectories.Flink; Link != &LdrpUserDllDirectories; Link = Link->Flink)
        {
            Entry = CONTAINING_RECORD(Link, LDRP_USER_DLL_DIRECTORY, Link);
            Current = LdrpAppendDirectory(Current, Entry->Path, wcslen(Entry->Path));
        }
        Current = LdrpAppendDirectory(Current, LdrpDllDirectory.Buffer, LdrpDllDirectory.Length / sizeof(WCHAR));
    }
    if (Flags & LOAD_LIBRARY_SEARCH_SYSTEM32)
    {
        SIZE_T RootLength = wcslen(SharedUserData->NtSystemRoot);

        RtlCopyMemory(Current, SharedUserData->NtSystemRoot, RootLength * sizeof(WCHAR));
        RtlCopyMemory(Current + RootLength, L"\\System32;", 10 * sizeof(WCHAR));
        Current += RootLength + 10;
    }

    if (Current > Buffer)
        Current--;
    *Current = UNICODE_NULL;
    ASSERT(Current < Buffer + Length);

    *Path = Buffer;
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
LdrSetDllDirectory(
    _In_ PCUNICODE_STRING Directory)
{
    UNICODE_STRING New, Old;
    NTSTATUS Status;

    if (Directory->Buffer)
    {
        Status = RtlDuplicateUnicodeString(RTL_DUPLICATE_UNICODE_STRING_NULL_TERMINATE |
                                           RTL_DUPLICATE_UNICODE_STRING_ALLOCATE_NULL_STRING,
                                           Directory, &New);
        if (!NT_SUCCESS(Status))
            return Status;
        ASSERT(New.Buffer != NULL);
    }
    else
    {
        RtlInitEmptyUnicodeString(&New, NULL, 0);
    }

    RtlAcquireSRWLockExclusive(&LdrpDllDirectoryLock);
    Old = LdrpDllDirectory;
    LdrpDllDirectory = New;
    RtlReleaseSRWLockExclusive(&LdrpDllDirectoryLock);

    RtlFreeUnicodeString(&Old);
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
LdrGetDllDirectory(
    _Inout_ PUNICODE_STRING Directory)
{
    NTSTATUS Status = STATUS_SUCCESS;
    USHORT Needed;

    RtlAcquireSRWLockShared(&LdrpDllDirectoryLock);
    Needed = LdrpDllDirectory.Length + sizeof(UNICODE_NULL);
    if (Directory->MaximumLength >= Needed)
    {
        RtlCopyMemory(Directory->Buffer, LdrpDllDirectory.Buffer, LdrpDllDirectory.Length);
        Directory->Buffer[LdrpDllDirectory.Length / sizeof(WCHAR)] = UNICODE_NULL;
        Directory->Length = LdrpDllDirectory.Length;
    }
    else
    {
        Directory->Length = Needed;
        if (Directory->MaximumLength)
            Directory->Buffer[0] = UNICODE_NULL;
        Status = STATUS_BUFFER_TOO_SMALL;
    }
    RtlReleaseSRWLockShared(&LdrpDllDirectoryLock);
    return Status;
}

NTSTATUS
NTAPI
LdrAddDllDirectory(
    _In_ PCUNICODE_STRING Directory,
    _Out_ PVOID *Cookie)
{
    FILE_BASIC_INFORMATION Information;
    OBJECT_ATTRIBUTES ObjectAttributes;
    PLDRP_USER_DLL_DIRECTORY Entry;
    UNICODE_STRING NtPath;
    RTL_PATH_TYPE Type;
    PCWSTR Dos;
    SIZE_T Length;
    NTSTATUS Status;

    Type = RtlDetermineDosPathNameType_U(Directory->Buffer);
    if (Type != RtlPathTypeDriveAbsolute && Type != RtlPathTypeRooted &&
        Type != RtlPathTypeUncAbsolute && Type != RtlPathTypeLocalDevice)
    {
        return STATUS_INVALID_PARAMETER;
    }

    Status = RtlDosPathNameToNtPathName_U_WithStatus(Directory->Buffer, &NtPath, NULL, NULL);
    if (!NT_SUCCESS(Status))
        return Status;

    InitializeObjectAttributes(&ObjectAttributes, &NtPath, OBJ_CASE_INSENSITIVE, NULL, NULL);
    Status = NtQueryAttributesFile(&ObjectAttributes, &Information);
    if (!NT_SUCCESS(Status))
    {
        RtlFreeUnicodeString(&NtPath);
        return Status;
    }

    /* The search path names the directory in its DOS form, without the \??\ the NT form starts with */
    Dos = NtPath.Buffer;
    Length = NtPath.Length / sizeof(WCHAR);
    if (Length >= 4 && !wcsncmp(Dos, L"\\??\\", 4))
    {
        Dos += 4;
        Length -= 4;
    }

    Entry = RtlAllocateHeap(RtlGetProcessHeap(), 0,
                            FIELD_OFFSET(LDRP_USER_DLL_DIRECTORY, Path[Length + 1]));
    if (!Entry)
    {
        RtlFreeUnicodeString(&NtPath);
        return STATUS_NO_MEMORY;
    }
    RtlCopyMemory(Entry->Path, Dos, Length * sizeof(WCHAR));
    Entry->Path[Length] = UNICODE_NULL;
    RtlFreeUnicodeString(&NtPath);

    RtlAcquireSRWLockExclusive(&LdrpDllDirectoryLock);
    InsertHeadList(&LdrpUserDllDirectories, &Entry->Link);
    RtlReleaseSRWLockExclusive(&LdrpDllDirectoryLock);

    *Cookie = Entry;
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
LdrRemoveDllDirectory(
    _In_ PVOID Cookie)
{
    PLDRP_USER_DLL_DIRECTORY Entry = Cookie;

    RtlAcquireSRWLockExclusive(&LdrpDllDirectoryLock);
    RemoveEntryList(&Entry->Link);
    RtlReleaseSRWLockExclusive(&LdrpDllDirectoryLock);

    RtlFreeHeap(RtlGetProcessHeap(), 0, Entry);
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
LdrSetDefaultDllDirectories(
    _In_ ULONG Flags)
{
    if (!Flags || (Flags & ~(LDRP_SEARCH_FLAGS & ~LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR)))
        return STATUS_INVALID_PARAMETER;

    RtlAcquireSRWLockExclusive(&LdrpDllDirectoryLock);
    LdrpDefaultDllDirectories = Flags;
    RtlReleaseSRWLockExclusive(&LdrpDllDirectoryLock);
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
LdrGetDllPath(
    _In_opt_z_ PCWSTR Module,
    _In_ ULONG Flags,
    _Outptr_ PWSTR *Path,
    _Outptr_result_maybenull_ PWSTR *Unknown)
{
    UNICODE_STRING DllDirectory;
    NTSTATUS Status;

    *Unknown = NULL;

    RtlAcquireSRWLockShared(&LdrpDllDirectoryLock);

    if (Flags & LOAD_WITH_ALTERED_SEARCH_PATH)
    {
        if (Flags & LDRP_SEARCH_FLAGS)
        {
            RtlReleaseSRWLockShared(&LdrpDllDirectoryLock);
            return STATUS_INVALID_PARAMETER;
        }
        if (LdrpDefaultDllDirectories)
            Flags |= LdrpDefaultDllDirectories | LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR;
    }
    else if (!(Flags & LDRP_SEARCH_FLAGS))
    {
        Flags |= LdrpDefaultDllDirectories;
    }

    if (Flags & LDRP_SEARCH_FLAGS)
    {
        Status = LdrpBuildSearchFlagsPath(Module, Flags, Path);
    }
    else
    {
        if (!(Flags & LOAD_WITH_ALTERED_SEARCH_PATH) || !Module || !wcschr(Module, L'\\'))
            Module = NtCurrentPeb()->ProcessParameters->ImagePathName.Buffer;
        DllDirectory = LdrpDllDirectory;
        Status = LdrpBuildLoadPath(Module, DllDirectory.Buffer ? &DllDirectory : NULL,
                                   LdrpSafeDllSearch(), LdrpPrefersSystem32(), Path);
    }

    RtlReleaseSRWLockShared(&LdrpDllDirectoryLock);
    return Status;
}

/* FIXME: Windows 11 puts the current directory after the system directories here; whether RtlSetSearchPathMode changes that is not known */
NTSTATUS
NTAPI
RtlGetSearchPath(
    _Outptr_ PWSTR *Path)
{
    return LdrpBuildLoadPath(NtCurrentPeb()->ProcessParameters->ImagePathName.Buffer, NULL, TRUE, FALSE, Path);
}

NTSTATUS
NTAPI
RtlGetExePath(
    _In_z_ PCWSTR Name,
    _Outptr_ PWSTR *Path)
{
    UNICODE_STRING Variable = RTL_CONSTANT_STRING(L"NoDefaultCurrentDirectoryInExePath");
    UNICODE_STRING Value, NoDirectory = RTL_CONSTANT_STRING(L"");

    /* A name with a directory always searches the current directory, as NeedCurrentDirectoryForExePath says */
    if (!wcschr(Name, L'\\'))
    {
        RtlInitEmptyUnicodeString(&Value, NULL, 0);
        if (RtlQueryEnvironmentVariable_U(NULL, &Variable, &Value) != STATUS_VARIABLE_NOT_FOUND)
            return LdrpBuildLoadPath(NtCurrentPeb()->ProcessParameters->ImagePathName.Buffer, &NoDirectory,
                                     FALSE, FALSE, Path);
    }

    return LdrpBuildLoadPath(NtCurrentPeb()->ProcessParameters->ImagePathName.Buffer, NULL, FALSE, FALSE, Path);
}

VOID
NTAPI
RtlReleasePath(
    _In_ PWSTR Path)
{
    RtlFreeHeap(RtlGetProcessHeap(), 0, Path);
}
