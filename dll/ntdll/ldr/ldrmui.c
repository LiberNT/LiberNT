/*
 * PROJECT:     LiberNT NT User-Mode DLL
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Language-specific resource files of language-neutral modules
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntdll.h>
#include <winnls.h>

#define NDEBUG
#include <debug.h>

#define LDRP_MUI_MAX_LANGUAGES 16
#define LDRP_MUI_LANGUAGE_LENGTH (LOCALE_NAME_MAX_LENGTH + 1)

#define LDRP_MUI_RC_SIGNATURE 0xFECDFECD
#define LDRP_MUI_RC_HEADER_SIZE 0x88
#define LDRP_MUI_RC_FILE_TYPE 0x10
#define LDRP_MUI_RC_CHECKSUM 0x2C
#define LDRP_MUI_RC_MUI_TYPE_NAMES 0x64
#define LDRP_MUI_RC_MUI_TYPE_IDS 0x6C
#define LDRP_MUI_RC_LANGUAGE 0x74
#define LDRP_MUI_RC_FALLBACK_LANGUAGE 0x7C
#define LDRP_MUI_RC_CHECKSUM_SIZE 16
#define LDRP_MUI_FILE_LANGUAGE_NEUTRAL 0x11
#define LDRP_MUI_FILE_LANGUAGE_SPECIFIC 0x12

typedef struct _LDRP_MUI_CONFIG
{
    ULONG FileType;
    PUCHAR Checksum;
    PCWSTR MuiTypeNames;
    ULONG MuiTypeNamesSize;
    PULONG MuiTypeIds;
    ULONG MuiTypeIdCount;
    PCWSTR Language;
    ULONG LanguageSize;
    PCWSTR FallbackLanguage;
    ULONG FallbackLanguageSize;
} LDRP_MUI_CONFIG, *PLDRP_MUI_CONFIG;

typedef struct _LDRP_MUI_MODULE
{
    LIST_ENTRY Links;
    PVOID BaseAddress;
    ULONG TimeDateStamp;
    ULONG SizeOfImage;
    WCHAR LanguageKey[LDRP_MUI_LANGUAGE_LENGTH];
    PVOID ResourceBase;
    SIZE_T ViewSize;
} LDRP_MUI_MODULE, *PLDRP_MUI_MODULE;

static LIST_ENTRY LdrpMuiModuleList = { &LdrpMuiModuleList, &LdrpMuiModuleList };
static RTL_SRWLOCK LdrpMuiLock = RTL_SRWLOCK_INIT;

static
PVOID
LdrpUntagResourceBase(
    _In_ PVOID BaseAddress)
{
    return (PVOID)((ULONG_PTR)BaseAddress & ~(ULONG_PTR)3);
}

static
BOOLEAN
LdrpMuiRange(
    _In_ PUCHAR Data,
    _In_ ULONG Size,
    _In_ ULONG Field,
    _Out_ PVOID *Pointer,
    _Out_ PULONG Length)
{
    ULONG Offset = *(PULONG)(Data + Field);
    ULONG Bytes = *(PULONG)(Data + Field + sizeof(ULONG));

    *Pointer = NULL;
    *Length = 0;
    if (!Offset || !Bytes)
        return TRUE;
    if (Offset > Size || Bytes > Size - Offset)
        return FALSE;

    *Pointer = Data + Offset;
    *Length = Bytes;
    return TRUE;
}

static
BOOLEAN
LdrpGetMuiConfig(
    _In_ PVOID BaseAddress,
    _Out_ PLDRP_MUI_CONFIG Config)
{
    static const WCHAR MuiType[] = L"MUI";
    LDR_RESOURCE_INFO Info;
    PIMAGE_RESOURCE_DATA_ENTRY Entry;
    PUCHAR Data;
    ULONG Size, Length;
    PVOID Pointer;

    RtlZeroMemory(Config, sizeof(*Config));

    Info.Type = (ULONG_PTR)MuiType;
    Info.Name = 1;
    Info.Language = 0;
    if (!NT_SUCCESS(LdrpFindModuleResourceEntry(BaseAddress, &Info, 3, (PVOID *)&Entry, FALSE)) ||
        !NT_SUCCESS(LdrAccessResource(BaseAddress, Entry, (PVOID *)&Data, &Size)))
    {
        return FALSE;
    }

    _SEH2_TRY
    {
        if (Size < LDRP_MUI_RC_HEADER_SIZE ||
            *(PULONG)Data != LDRP_MUI_RC_SIGNATURE ||
            *(PULONG)(Data + sizeof(ULONG)) > Size)
        {
            _SEH2_YIELD(return FALSE);
        }
        Size = *(PULONG)(Data + sizeof(ULONG));

        Config->FileType = *(PULONG)(Data + LDRP_MUI_RC_FILE_TYPE);
        Config->Checksum = Data + LDRP_MUI_RC_CHECKSUM;

        if (!LdrpMuiRange(Data, Size, LDRP_MUI_RC_MUI_TYPE_NAMES, &Pointer, &Length))
            _SEH2_YIELD(return FALSE);
        Config->MuiTypeNames = Pointer;
        Config->MuiTypeNamesSize = Length;

        if (!LdrpMuiRange(Data, Size, LDRP_MUI_RC_MUI_TYPE_IDS, &Pointer, &Length))
            _SEH2_YIELD(return FALSE);
        Config->MuiTypeIds = Pointer;
        Config->MuiTypeIdCount = Length / sizeof(ULONG);

        if (!LdrpMuiRange(Data, Size, LDRP_MUI_RC_LANGUAGE, &Pointer, &Length))
            _SEH2_YIELD(return FALSE);
        Config->Language = Pointer;
        Config->LanguageSize = Length;

        if (!LdrpMuiRange(Data, Size, LDRP_MUI_RC_FALLBACK_LANGUAGE, &Pointer, &Length))
            _SEH2_YIELD(return FALSE);
        Config->FallbackLanguage = Pointer;
        Config->FallbackLanguageSize = Length;
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        _SEH2_YIELD(return FALSE);
    }
    _SEH2_END;

    return TRUE;
}

static
BOOLEAN
LdrpIsMuiResourceType(
    _In_ PLDRP_MUI_CONFIG Config,
    _In_ ULONG_PTR Type)
{
    ULONG Index;

    if (IS_INTRESOURCE(Type))
    {
        for (Index = 0; Index < Config->MuiTypeIdCount; Index++)
        {
            if (Config->MuiTypeIds[Index] == (ULONG)Type)
                return TRUE;
        }
    }
    else if (Config->MuiTypeNames)
    {
        PCWSTR Name = Config->MuiTypeNames;
        PCWSTR End = (PCWSTR)((PUCHAR)Config->MuiTypeNames + Config->MuiTypeNamesSize);

        while (Name < End && *Name)
        {
            SIZE_T Length = 0;

            while (Name + Length < End && Name[Length])
                Length++;
            if (Name + Length >= End)
                break;
            if (!_wcsicmp(Name, (PCWSTR)Type))
                return TRUE;
            Name += Length + 1;
        }
    }

    return FALSE;
}

static
BOOLEAN
LdrpMuiLanguageEquals(
    _In_ PLDRP_MUI_CONFIG Config,
    _In_ PCWSTR Language)
{
    SIZE_T Length = wcslen(Language);

    if (!Config->Language || Config->LanguageSize < (Length + 1) * sizeof(WCHAR))
        return FALSE;

    return !_wcsnicmp(Config->Language, Language, Length) && Config->Language[Length] == UNICODE_NULL;
}

static
ULONG
LdrpAddMuiLanguage(
    _Inout_updates_(LDRP_MUI_MAX_LANGUAGES * LDRP_MUI_LANGUAGE_LENGTH) PWSTR Languages,
    _In_ ULONG Count,
    _In_ PCWSTR Language,
    _In_ SIZE_T Length)
{
    ULONG Index;

    if (!Length || Length >= LDRP_MUI_LANGUAGE_LENGTH || Count >= LDRP_MUI_MAX_LANGUAGES)
        return Count;

    for (Index = 0; Index < Count; Index++)
    {
        PCWSTR Existing = &Languages[Index * LDRP_MUI_LANGUAGE_LENGTH];

        if (wcslen(Existing) == Length && !_wcsnicmp(Existing, Language, Length))
            return Count;
    }

    RtlCopyMemory(&Languages[Count * LDRP_MUI_LANGUAGE_LENGTH], Language, Length * sizeof(WCHAR));
    Languages[Count * LDRP_MUI_LANGUAGE_LENGTH + Length] = UNICODE_NULL;
    return Count + 1;
}

static
ULONG
LdrpAddMuiLanguageList(
    _Inout_updates_(LDRP_MUI_MAX_LANGUAGES * LDRP_MUI_LANGUAGE_LENGTH) PWSTR Languages,
    _In_ ULONG Count,
    _In_ PCZZWSTR List)
{
    PCWSTR Current;

    for (Current = List; *Current; Current += wcslen(Current) + 1)
    {
        SIZE_T Length = wcslen(Current);
        PCWSTR Dash = wcschr(Current, L'-');

        Count = LdrpAddMuiLanguage(Languages, Count, Current, Length);
        if (Dash)
            Count = LdrpAddMuiLanguage(Languages, Count, Current, Dash - Current);
    }

    return Count;
}

static
ULONG
LdrpBuildMuiFallbackList(
    _Out_writes_(LDRP_MUI_MAX_LANGUAGES * LDRP_MUI_LANGUAGE_LENGTH) PWSTR Languages)
{
    WCHAR List[(LDRP_MUI_LANGUAGE_LENGTH * 5) + 1];
    ULONG Count = 0;
    ULONG ListCount;
    ULONG Size;

    Size = RTL_NUMBER_OF(List);
    if (NT_SUCCESS(RtlGetThreadPreferredUILanguages(MUI_LANGUAGE_NAME, &ListCount, List, &Size)) && ListCount)
        Count = LdrpAddMuiLanguageList(Languages, Count, List);

    Size = RTL_NUMBER_OF(List);
    if (NT_SUCCESS(RtlGetProcessPreferredUILanguages(MUI_LANGUAGE_NAME, &ListCount, List, &Size)) && ListCount)
        Count = LdrpAddMuiLanguageList(Languages, Count, List);

    Size = RTL_NUMBER_OF(List);
    if (NT_SUCCESS(RtlGetUserPreferredUILanguages(MUI_LANGUAGE_NAME, 0, &ListCount, List, &Size)) && ListCount)
        Count = LdrpAddMuiLanguageList(Languages, Count, List);

    Size = RTL_NUMBER_OF(List);
    if (NT_SUCCESS(RtlGetSystemPreferredUILanguages(MUI_LANGUAGE_NAME, 0, &ListCount, List, &Size)) && ListCount)
        Count = LdrpAddMuiLanguageList(Languages, Count, List);

    return Count;
}

static
BOOLEAN
LdrpGetLanguageKey(
    _In_ USHORT LanguageId,
    _Out_writes_(LDRP_MUI_LANGUAGE_LENGTH) PWSTR LanguageKey)
{
    UNICODE_STRING Name;

    LanguageKey[0] = UNICODE_NULL;
    if (PRIMARYLANGID(LanguageId) == LANG_NEUTRAL)
        return TRUE;

    Name.Buffer = LanguageKey;
    Name.Length = 0;
    Name.MaximumLength = (LDRP_MUI_LANGUAGE_LENGTH - 1) * sizeof(WCHAR);
    if (!NT_SUCCESS(RtlLcidToLocaleName(MAKELCID(LanguageId, SORT_DEFAULT), &Name, RTL_LOCALE_ALLOW_NEUTRAL_NAMES, FALSE)))
        return FALSE;

    LanguageKey[Name.Length / sizeof(WCHAR)] = UNICODE_NULL;
    return TRUE;
}

static
NTSTATUS
LdrpMapResourceFile(
    _In_ PCUNICODE_STRING Path,
    _Out_ PVOID *ViewBase,
    _Out_ PSIZE_T ViewSize)
{
    OBJECT_ATTRIBUTES ObjectAttributes;
    IO_STATUS_BLOCK IoStatusBlock;
    HANDLE FileHandle, SectionHandle;
    NTSTATUS Status;

    *ViewBase = NULL;
    *ViewSize = 0;

    InitializeObjectAttributes(&ObjectAttributes, (PUNICODE_STRING)Path, OBJ_CASE_INSENSITIVE, NULL, NULL);
    Status = NtOpenFile(&FileHandle,
                        FILE_READ_DATA | SYNCHRONIZE,
                        &ObjectAttributes,
                        &IoStatusBlock,
                        FILE_SHARE_READ | FILE_SHARE_DELETE,
                        FILE_SYNCHRONOUS_IO_NONALERT | FILE_NON_DIRECTORY_FILE);
    if (!NT_SUCCESS(Status))
        return Status;

    Status = NtCreateSection(&SectionHandle,
                             SECTION_MAP_READ | SECTION_QUERY,
                             NULL,
                             NULL,
                             PAGE_READONLY,
                             SEC_COMMIT,
                             FileHandle);
    NtClose(FileHandle);
    if (!NT_SUCCESS(Status))
        return Status;

    Status = NtMapViewOfSection(SectionHandle,
                                NtCurrentProcess(),
                                ViewBase,
                                0,
                                0,
                                NULL,
                                ViewSize,
                                ViewShare,
                                0,
                                PAGE_READONLY);
    NtClose(SectionHandle);
    if (!NT_SUCCESS(Status))
        return Status;

    if (!RtlImageNtHeader(*ViewBase))
    {
        NtUnmapViewOfSection(NtCurrentProcess(), *ViewBase);
        *ViewBase = NULL;
        *ViewSize = 0;
        return STATUS_INVALID_IMAGE_FORMAT;
    }

    return STATUS_SUCCESS;
}

static
NTSTATUS
LdrpMapMuiFile(
    _In_ PCUNICODE_STRING Path,
    _In_ PLDRP_MUI_CONFIG ModuleConfig,
    _In_ PCWSTR Language,
    _Out_ PVOID *ViewBase,
    _Out_ PSIZE_T ViewSize)
{
    LDRP_MUI_CONFIG Config;
    NTSTATUS Status;

    Status = LdrpMapResourceFile(Path, ViewBase, ViewSize);
    if (!NT_SUCCESS(Status))
        return Status;

    if (!LdrpGetMuiConfig((PVOID)((ULONG_PTR)*ViewBase | 1), &Config) ||
        Config.FileType != LDRP_MUI_FILE_LANGUAGE_SPECIFIC ||
        RtlCompareMemory(Config.Checksum, ModuleConfig->Checksum, LDRP_MUI_RC_CHECKSUM_SIZE) != LDRP_MUI_RC_CHECKSUM_SIZE ||
        !LdrpMuiLanguageEquals(&Config, Language))
    {
        NtUnmapViewOfSection(NtCurrentProcess(), *ViewBase);
        *ViewBase = NULL;
        *ViewSize = 0;
        return STATUS_MUI_INVALID_FILE;
    }

    return STATUS_SUCCESS;
}

static
NTSTATUS
LdrpLoadMuiFile(
    _In_ PVOID BaseAddress,
    _In_ PLDRP_MUI_CONFIG ModuleConfig,
    _In_ PCWSTR Language,
    _Out_ PVOID *ViewBase,
    _Out_ PSIZE_T ViewSize)
{
    static const WCHAR Extension[] = L".mui";
    UCHAR NameBuffer[sizeof(MEMORY_SECTION_NAME) + (MAX_PATH * 2 * sizeof(WCHAR))];
    PMEMORY_SECTION_NAME SectionName = (PMEMORY_SECTION_NAME)NameBuffer;
    UNICODE_STRING Path, Directory, Name, LanguageName;
    PWSTR FileName;
    USHORT DirectoryLength, FileNameLength;
    SIZE_T ReturnLength;
    NTSTATUS Status;

    *ViewBase = NULL;
    *ViewSize = 0;

    Status = NtQueryVirtualMemory(NtCurrentProcess(),
                                  LdrpUntagResourceBase(BaseAddress),
                                  MemorySectionName,
                                  SectionName,
                                  sizeof(NameBuffer),
                                  &ReturnLength);
    if (!NT_SUCCESS(Status))
        return STATUS_MUI_FILE_NOT_FOUND;

    FileName = SectionName->SectionFileName.Buffer + SectionName->SectionFileName.Length / sizeof(WCHAR);
    while (FileName > SectionName->SectionFileName.Buffer && FileName[-1] != L'\\')
        FileName--;
    DirectoryLength = (USHORT)((FileName - SectionName->SectionFileName.Buffer) * sizeof(WCHAR));
    FileNameLength = SectionName->SectionFileName.Length - DirectoryLength;
    if (!DirectoryLength || !FileNameLength)
        return STATUS_MUI_FILE_NOT_FOUND;

    Path.MaximumLength = DirectoryLength + (LDRP_MUI_LANGUAGE_LENGTH + 1) * sizeof(WCHAR) +
                         FileNameLength + sizeof(Extension);
    Path.Buffer = RtlAllocateHeap(RtlGetProcessHeap(), 0, Path.MaximumLength);
    if (!Path.Buffer)
        return STATUS_NO_MEMORY;

    Directory.Buffer = SectionName->SectionFileName.Buffer;
    Directory.Length = Directory.MaximumLength = DirectoryLength;
    Name.Buffer = FileName;
    Name.Length = Name.MaximumLength = FileNameLength;
    RtlInitUnicodeString(&LanguageName, Language);

    Path.Length = 0;
    RtlAppendUnicodeStringToString(&Path, &Directory);
    RtlAppendUnicodeStringToString(&Path, &LanguageName);
    RtlAppendUnicodeToString(&Path, L"\\");
    RtlAppendUnicodeStringToString(&Path, &Name);
    RtlAppendUnicodeToString(&Path, Extension);

    Status = LdrpMapMuiFile(&Path, ModuleConfig, Language, ViewBase, ViewSize);
    RtlFreeHeap(RtlGetProcessHeap(), 0, Path.Buffer);
    return NT_SUCCESS(Status) ? Status : STATUS_MUI_FILE_NOT_FOUND;
}

static
BOOLEAN
LdrpIsSystemModuleDirectory(
    _In_ PCWSTR Directory,
    _In_ SIZE_T Length)
{
    static const PCWSTR SystemDirectories[] = { L"System32", L"SysWOW64", L"arm64ec" };
    ULONG Index;

    for (Index = 0; Index < RTL_NUMBER_OF(SystemDirectories); Index++)
    {
        if (wcslen(SystemDirectories[Index]) == Length &&
            !_wcsnicmp(Directory, SystemDirectories[Index], Length))
        {
            return TRUE;
        }
    }

    return FALSE;
}

static
NTSTATUS
LdrpLoadMunFile(
    _In_ PVOID BaseAddress,
    _Out_ PVOID *ViewBase,
    _Out_ PSIZE_T ViewSize)
{
    static const WCHAR Directory[] = L"\\SystemRoot\\SystemResources\\";
    static const WCHAR Extension[] = L".mun";
    UCHAR NameBuffer[sizeof(MEMORY_SECTION_NAME) + (MAX_PATH * 2 * sizeof(WCHAR))];
    PMEMORY_SECTION_NAME SectionName = (PMEMORY_SECTION_NAME)NameBuffer;
    UNICODE_STRING Path, Name;
    PWSTR FileName, Parent;
    USHORT FileNameLength;
    SIZE_T ReturnLength;
    NTSTATUS Status;

    *ViewBase = NULL;
    *ViewSize = 0;

    Status = NtQueryVirtualMemory(NtCurrentProcess(),
                                  LdrpUntagResourceBase(BaseAddress),
                                  MemorySectionName,
                                  SectionName,
                                  sizeof(NameBuffer),
                                  &ReturnLength);
    if (!NT_SUCCESS(Status))
        return STATUS_MUI_FILE_NOT_FOUND;

    FileName = SectionName->SectionFileName.Buffer + SectionName->SectionFileName.Length / sizeof(WCHAR);
    while (FileName > SectionName->SectionFileName.Buffer && FileName[-1] != L'\\')
        FileName--;
    if (FileName <= SectionName->SectionFileName.Buffer + 1)
        return STATUS_MUI_FILE_NOT_FOUND;
    Parent = FileName - 1;
    while (Parent > SectionName->SectionFileName.Buffer && Parent[-1] != L'\\')
        Parent--;
    if (!LdrpIsSystemModuleDirectory(Parent, FileName - 1 - Parent))
        return STATUS_MUI_FILE_NOT_FOUND;

    FileNameLength = (USHORT)(SectionName->SectionFileName.Length -
                              (FileName - SectionName->SectionFileName.Buffer) * sizeof(WCHAR));
    if (!FileNameLength)
        return STATUS_MUI_FILE_NOT_FOUND;

    Path.MaximumLength = sizeof(Directory) + FileNameLength + sizeof(Extension);
    Path.Buffer = RtlAllocateHeap(RtlGetProcessHeap(), 0, Path.MaximumLength);
    if (!Path.Buffer)
        return STATUS_NO_MEMORY;

    Name.Buffer = FileName;
    Name.Length = Name.MaximumLength = FileNameLength;
    Path.Length = 0;
    RtlAppendUnicodeToString(&Path, Directory);
    RtlAppendUnicodeStringToString(&Path, &Name);
    RtlAppendUnicodeToString(&Path, Extension);

    Status = LdrpMapResourceFile(&Path, ViewBase, ViewSize);
    RtlFreeHeap(RtlGetProcessHeap(), 0, Path.Buffer);
    return NT_SUCCESS(Status) ? Status : STATUS_MUI_FILE_NOT_FOUND;
}

static
PLDRP_MUI_MODULE
LdrpLookupMuiModule(
    _In_ PVOID BaseAddress,
    _In_ PCWSTR LanguageKey)
{
    PLIST_ENTRY Entry;

    for (Entry = LdrpMuiModuleList.Flink; Entry != &LdrpMuiModuleList; Entry = Entry->Flink)
    {
        PLDRP_MUI_MODULE Module = CONTAINING_RECORD(Entry, LDRP_MUI_MODULE, Links);

        if (Module->BaseAddress == BaseAddress && !_wcsicmp(Module->LanguageKey, LanguageKey))
            return Module;
    }

    return NULL;
}

static
VOID
LdrpFreeMuiModule(
    _In_ PLDRP_MUI_MODULE Module)
{
    if (Module->ResourceBase)
        NtUnmapViewOfSection(NtCurrentProcess(), Module->ResourceBase);
    RtlFreeHeap(RtlGetProcessHeap(), 0, Module);
}

static
NTSTATUS
LdrpGetMuiModuleForLanguage(
    _In_ PVOID BaseAddress,
    _In_opt_ PLDRP_MUI_CONFIG Config,
    _In_ ULONG TimeDateStamp,
    _In_ ULONG SizeOfImage,
    _In_ PCWSTR Language,
    _Out_ PVOID *ResourceBase,
    _Out_ PBOOLEAN Attempted)
{
    PVOID Base = LdrpUntagResourceBase(BaseAddress);
    PLDRP_MUI_MODULE Module, Existing, Stale = NULL;
    PVOID ViewBase;
    SIZE_T ViewSize, Length;
    NTSTATUS Status;

    *ResourceBase = NULL;
    *Attempted = FALSE;

    RtlAcquireSRWLockShared(&LdrpMuiLock);
    Module = LdrpLookupMuiModule(Base, Language);
    if (Module && Module->TimeDateStamp == TimeDateStamp && Module->SizeOfImage == SizeOfImage)
    {
        if (Module->ResourceBase)
            *ResourceBase = (PVOID)((ULONG_PTR)Module->ResourceBase | 1);
        RtlReleaseSRWLockShared(&LdrpMuiLock);
        return *ResourceBase ? STATUS_SUCCESS : STATUS_MUI_FILE_NOT_LOADED;
    }
    RtlReleaseSRWLockShared(&LdrpMuiLock);

    *Attempted = TRUE;
    if (Language[0])
        Status = LdrpLoadMuiFile(BaseAddress, Config, Language, &ViewBase, &ViewSize);
    else
        Status = LdrpLoadMunFile(BaseAddress, &ViewBase, &ViewSize);
    if (Status == STATUS_NO_MEMORY)
        return Status;
    if (!NT_SUCCESS(Status))
    {
        ViewBase = NULL;
        ViewSize = 0;
    }

    Module = RtlAllocateHeap(RtlGetProcessHeap(), 0, sizeof(*Module));
    if (!Module)
    {
        if (ViewBase)
            NtUnmapViewOfSection(NtCurrentProcess(), ViewBase);
        return STATUS_NO_MEMORY;
    }
    Module->BaseAddress = Base;
    Module->TimeDateStamp = TimeDateStamp;
    Module->SizeOfImage = SizeOfImage;
    Length = wcslen(Language);
    if (Length >= LDRP_MUI_LANGUAGE_LENGTH)
        Length = LDRP_MUI_LANGUAGE_LENGTH - 1;
    RtlCopyMemory(Module->LanguageKey, Language, Length * sizeof(WCHAR));
    Module->LanguageKey[Length] = UNICODE_NULL;
    Module->ResourceBase = ViewBase;
    Module->ViewSize = ViewSize;

    RtlAcquireSRWLockExclusive(&LdrpMuiLock);
    Existing = LdrpLookupMuiModule(Base, Language);
    if (Existing && Existing->TimeDateStamp == TimeDateStamp && Existing->SizeOfImage == SizeOfImage)
    {
        if (Existing->ResourceBase)
            *ResourceBase = (PVOID)((ULONG_PTR)Existing->ResourceBase | 1);
        RtlReleaseSRWLockExclusive(&LdrpMuiLock);
        LdrpFreeMuiModule(Module);
        return *ResourceBase ? STATUS_SUCCESS : STATUS_MUI_FILE_NOT_FOUND;
    }
    if (Existing)
    {
        RemoveEntryList(&Existing->Links);
        Stale = Existing;
    }
    InsertTailList(&LdrpMuiModuleList, &Module->Links);
    RtlReleaseSRWLockExclusive(&LdrpMuiLock);

    if (Stale)
        LdrpFreeMuiModule(Stale);

    if (!ViewBase)
        return STATUS_MUI_FILE_NOT_FOUND;

    *ResourceBase = (PVOID)((ULONG_PTR)ViewBase | 1);
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
LdrpGetAlternateResourceModule(
    _In_ PVOID BaseAddress,
    _In_ ULONG_PTR Type,
    _In_ USHORT LanguageId,
    _Out_ PVOID *ResourceBase)
{
    PVOID Base = LdrpUntagResourceBase(BaseAddress);
    WCHAR LanguageKey[LDRP_MUI_LANGUAGE_LENGTH];
    PIMAGE_NT_HEADERS NtHeaders;
    LDRP_MUI_CONFIG Config;
    PWSTR Languages;
    ULONG Count, Index;
    BOOLEAN Attempted, AnyAttempted = FALSE;
    NTSTATUS Status = STATUS_MUI_FILE_NOT_LOADED;

    *ResourceBase = NULL;

    NtHeaders = RtlImageNtHeader(Base);
    if (!NtHeaders ||
        !LdrpGetMuiConfig(BaseAddress, &Config) ||
        Config.FileType != LDRP_MUI_FILE_LANGUAGE_NEUTRAL ||
        !LdrpIsMuiResourceType(&Config, Type) ||
        !LdrpGetLanguageKey(LanguageId, LanguageKey))
    {
        return STATUS_RESOURCE_DATA_NOT_FOUND;
    }

    Languages = RtlAllocateHeap(RtlGetProcessHeap(), 0, LDRP_MUI_MAX_LANGUAGES * LDRP_MUI_LANGUAGE_LENGTH * sizeof(WCHAR));
    if (!Languages)
        return STATUS_NO_MEMORY;

    if (LanguageKey[0])
    {
        Count = LdrpAddMuiLanguage(Languages, 0, LanguageKey, wcslen(LanguageKey));
    }
    else
    {
        Count = LdrpBuildMuiFallbackList(Languages);
        if (Config.FallbackLanguage)
        {
            SIZE_T Length = 0;

            while ((Length + 1) * sizeof(WCHAR) <= Config.FallbackLanguageSize && Config.FallbackLanguage[Length])
                Length++;
            Count = LdrpAddMuiLanguage(Languages, Count, Config.FallbackLanguage, Length);
        }
    }

    for (Index = 0; Index < Count; Index++)
    {
        Status = LdrpGetMuiModuleForLanguage(BaseAddress,
                                             &Config,
                                             NtHeaders->FileHeader.TimeDateStamp,
                                             NtHeaders->OptionalHeader.SizeOfImage,
                                             &Languages[Index * LDRP_MUI_LANGUAGE_LENGTH],
                                             ResourceBase,
                                             &Attempted);
        if (Attempted)
            AnyAttempted = TRUE;
        if (NT_SUCCESS(Status) || Status == STATUS_NO_MEMORY)
            break;
    }

    RtlFreeHeap(RtlGetProcessHeap(), 0, Languages);

    if (NT_SUCCESS(Status) || Status == STATUS_NO_MEMORY)
        return Status;
    return AnyAttempted ? STATUS_MUI_FILE_NOT_FOUND : STATUS_MUI_FILE_NOT_LOADED;
}

NTSTATUS
NTAPI
LdrpGetNeutralResourceModule(
    _In_ PVOID BaseAddress,
    _Out_ PVOID *ResourceBase)
{
    PIMAGE_NT_HEADERS NtHeaders = RtlImageNtHeader(LdrpUntagResourceBase(BaseAddress));
    BOOLEAN Attempted;

    *ResourceBase = NULL;
    if (!NtHeaders)
        return STATUS_RESOURCE_DATA_NOT_FOUND;

    return LdrpGetMuiModuleForLanguage(BaseAddress,
                                       NULL,
                                       NtHeaders->FileHeader.TimeDateStamp,
                                       NtHeaders->OptionalHeader.SizeOfImage,
                                       L"",
                                       ResourceBase,
                                       &Attempted);
}

PVOID
NTAPI
LdrpGetResourceModuleForEntry(
    _In_ PVOID BaseAddress,
    _In_ PVOID Entry)
{
    PLIST_ENTRY ListEntry;
    PVOID Result = BaseAddress;

    RtlAcquireSRWLockShared(&LdrpMuiLock);
    for (ListEntry = LdrpMuiModuleList.Flink; ListEntry != &LdrpMuiModuleList; ListEntry = ListEntry->Flink)
    {
        PLDRP_MUI_MODULE Module = CONTAINING_RECORD(ListEntry, LDRP_MUI_MODULE, Links);

        if (Module->ResourceBase &&
            (ULONG_PTR)Entry >= (ULONG_PTR)Module->ResourceBase &&
            (ULONG_PTR)Entry - (ULONG_PTR)Module->ResourceBase < Module->ViewSize)
        {
            Result = (PVOID)((ULONG_PTR)Module->ResourceBase | 1);
            break;
        }
    }
    RtlReleaseSRWLockShared(&LdrpMuiLock);

    return Result;
}

BOOLEAN
NTAPI
LdrpUnloadAlternateResourceModule(
    _In_ PVOID BaseAddress)
{
    PVOID Base = LdrpUntagResourceBase(BaseAddress);
    LIST_ENTRY Detached;
    PLIST_ENTRY Entry, Next;

    InitializeListHead(&Detached);
    RtlAcquireSRWLockExclusive(&LdrpMuiLock);
    for (Entry = LdrpMuiModuleList.Flink; Entry != &LdrpMuiModuleList; Entry = Next)
    {
        PLDRP_MUI_MODULE Module = CONTAINING_RECORD(Entry, LDRP_MUI_MODULE, Links);

        Next = Entry->Flink;
        if (Module->BaseAddress == Base)
        {
            RemoveEntryList(Entry);
            InsertTailList(&Detached, Entry);
        }
    }
    RtlReleaseSRWLockExclusive(&LdrpMuiLock);

    if (IsListEmpty(&Detached))
        return FALSE;

    while (!IsListEmpty(&Detached))
        LdrpFreeMuiModule(CONTAINING_RECORD(RemoveHeadList(&Detached), LDRP_MUI_MODULE, Links));

    return TRUE;
}

BOOLEAN
NTAPI
LdrpFlushAlternateResourceModules(VOID)
{
    LIST_ENTRY Detached;

    InitializeListHead(&Detached);
    RtlAcquireSRWLockExclusive(&LdrpMuiLock);
    while (!IsListEmpty(&LdrpMuiModuleList))
    {
        PLIST_ENTRY Entry = RemoveHeadList(&LdrpMuiModuleList);
        InsertTailList(&Detached, Entry);
    }
    RtlReleaseSRWLockExclusive(&LdrpMuiLock);

    while (!IsListEmpty(&Detached))
        LdrpFreeMuiModule(CONTAINING_RECORD(RemoveHeadList(&Detached), LDRP_MUI_MODULE, Links));

    return TRUE;
}
