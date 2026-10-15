/*
 * PROJECT:     LiberNT Win32 Base API
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Language-specific resource files of language-neutral modules
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <k32.h>

#define NDEBUG
#include <debug.h>

#define BASEP_MUI_RC_SIGNATURE 0xFECDFECD
#define BASEP_MUI_RC_MINIMUM_SIZE 0x84
#define BASEP_MUI_RC_FILE_TYPE 0x10
#define BASEP_MUI_RC_CHECKSUM 0x2C
#define BASEP_MUI_RC_FALLBACK_LANGUAGE 0x7C
#define BASEP_MUI_RC_CHECKSUM_SIZE 16
#define BASEP_MUI_FILE_LANGUAGE_NEUTRAL 0x11
#define BASEP_MUI_FILE_LANGUAGE_SPECIFIC 0x12
#define BASEP_MUI_MAX_LANGUAGES 32

typedef struct _BASEP_MUI_CONFIG
{
    ULONG FileType;
    UCHAR Checksum[BASEP_MUI_RC_CHECKSUM_SIZE];
    WCHAR FallbackLanguage[LOCALE_NAME_MAX_LENGTH];
} BASEP_MUI_CONFIG, *PBASEP_MUI_CONFIG;

typedef struct _BASEP_MUI_LANGUAGES
{
    ULONG Count;
    WCHAR Names[BASEP_MUI_MAX_LANGUAGES][LOCALE_NAME_MAX_LENGTH];
} BASEP_MUI_LANGUAGES, *PBASEP_MUI_LANGUAGES;

typedef struct _BASEP_RES_ITEM
{
    ULONG_PTR Id;
    PWSTR Name;
} BASEP_RES_ITEM, *PBASEP_RES_ITEM;

typedef struct _BASEP_RES_LIST
{
    PBASEP_RES_ITEM Items;
    ULONG Count;
    ULONG Capacity;
} BASEP_RES_LIST, *PBASEP_RES_LIST;

typedef BOOL (*PBASEP_RES_CALLBACK)(HMODULE Module, PBASEP_RES_ITEM Item, PVOID Context);

static
BOOL
BasepGetMuiConfig(
    _In_ PVOID Module,
    _Out_ PBASEP_MUI_CONFIG Config)
{
    LDR_RESOURCE_INFO Info;
    PIMAGE_RESOURCE_DATA_ENTRY Entry;
    PUCHAR Data;
    ULONG Size, Offset, Length;
    BOOL Result = FALSE;

    RtlZeroMemory(Config, sizeof(*Config));

    Info.Type = (ULONG_PTR)L"MUI";
    Info.Name = 1;
    Info.Language = 0;
    if (!NT_SUCCESS(LdrFindResource_U(Module, &Info, 3, &Entry)) ||
        !NT_SUCCESS(LdrAccessResource(Module, Entry, (PVOID *)&Data, &Size)))
    {
        return FALSE;
    }

    _SEH2_TRY
    {
        if (Size >= BASEP_MUI_RC_MINIMUM_SIZE &&
            *(PULONG)Data == BASEP_MUI_RC_SIGNATURE &&
            *(PULONG)(Data + sizeof(ULONG)) <= Size &&
            *(PULONG)(Data + sizeof(ULONG)) >= BASEP_MUI_RC_MINIMUM_SIZE)
        {
            Size = *(PULONG)(Data + sizeof(ULONG));
            Config->FileType = *(PULONG)(Data + BASEP_MUI_RC_FILE_TYPE);
            RtlCopyMemory(Config->Checksum, Data + BASEP_MUI_RC_CHECKSUM, BASEP_MUI_RC_CHECKSUM_SIZE);

            Offset = *(PULONG)(Data + BASEP_MUI_RC_FALLBACK_LANGUAGE);
            Length = *(PULONG)(Data + BASEP_MUI_RC_FALLBACK_LANGUAGE + sizeof(ULONG));
            if (Offset && Length && Offset <= Size && Length <= Size - Offset)
            {
                PCWSTR Language = (PCWSTR)(Data + Offset);
                ULONG Chars = 0;

                while ((Chars + 1) * sizeof(WCHAR) <= Length && Chars < LOCALE_NAME_MAX_LENGTH - 1 && Language[Chars])
                {
                    Config->FallbackLanguage[Chars] = Language[Chars];
                    Chars++;
                }
                Config->FallbackLanguage[Chars] = UNICODE_NULL;
            }
            Result = TRUE;
        }
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Result = FALSE;
    }
    _SEH2_END;

    return Result;
}

static
VOID
BasepAddLanguage(
    _Inout_ PBASEP_MUI_LANGUAGES Languages,
    _In_ PCWSTR Language,
    _In_ SIZE_T Length)
{
    ULONG Index;

    if (!Length || Length >= LOCALE_NAME_MAX_LENGTH || Languages->Count >= BASEP_MUI_MAX_LANGUAGES)
        return;

    for (Index = 0; Index < Languages->Count; Index++)
    {
        if (wcslen(Languages->Names[Index]) == Length && !_wcsnicmp(Languages->Names[Index], Language, Length))
            return;
    }

    RtlCopyMemory(Languages->Names[Languages->Count], Language, Length * sizeof(WCHAR));
    Languages->Names[Languages->Count][Length] = UNICODE_NULL;
    Languages->Count++;
}

static
VOID
BasepAddLanguageList(
    _Inout_ PBASEP_MUI_LANGUAGES Languages,
    _In_ PCZZWSTR List,
    _In_ BOOL AddNeutral)
{
    PCWSTR Current;

    for (Current = List; *Current; Current += wcslen(Current) + 1)
    {
        PCWSTR Dash = wcschr(Current, L'-');

        BasepAddLanguage(Languages, Current, wcslen(Current));
        if (AddNeutral && Dash)
            BasepAddLanguage(Languages, Current, Dash - Current);
    }
}

static
VOID
BasepGetPreferredLanguages(
    _Out_ PBASEP_MUI_LANGUAGES Languages,
    _In_ BOOL InstalledOnly)
{
    WCHAR List[LOCALE_NAME_MAX_LENGTH * 8 + 1];
    ULONG Count, Size;

    Languages->Count = 0;

    if (!InstalledOnly)
    {
        Size = RTL_NUMBER_OF(List);
        if (NT_SUCCESS(RtlGetThreadPreferredUILanguages(MUI_LANGUAGE_NAME, &Count, List, &Size)) && Count)
            BasepAddLanguageList(Languages, List, TRUE);

        Size = RTL_NUMBER_OF(List);
        if (NT_SUCCESS(RtlGetProcessPreferredUILanguages(MUI_LANGUAGE_NAME, &Count, List, &Size)) && Count)
            BasepAddLanguageList(Languages, List, TRUE);
    }

    Size = RTL_NUMBER_OF(List);
    if (NT_SUCCESS(RtlGetUserPreferredUILanguages(MUI_LANGUAGE_NAME, 0, &Count, List, &Size)) && Count)
        BasepAddLanguageList(Languages, List, !InstalledOnly);

    Size = RTL_NUMBER_OF(List);
    if (NT_SUCCESS(RtlGetSystemPreferredUILanguages(MUI_LANGUAGE_NAME, 0, &Count, List, &Size)) && Count)
        BasepAddLanguageList(Languages, List, !InstalledOnly);
}

static
BOOL
BasepIsLocaleDirectoryName(
    _In_ PCWSTR Name)
{
    LCID Lcid;

    if (!wcscmp(Name, L".") || !wcscmp(Name, L".."))
        return FALSE;

    return NT_SUCCESS(RtlLocaleNameToLcid(Name, &Lcid, RTL_LOCALE_ALLOW_NEUTRAL_NAMES));
}

static
VOID
BasepGetDirectoryLanguages(
    _In_ PCWSTR Directory,
    _Out_ PBASEP_MUI_LANGUAGES Languages)
{
    WCHAR Pattern[MAX_PATH];
    WIN32_FIND_DATAW FindData;
    HANDLE Find;

    Languages->Count = 0;
    if (!NT_SUCCESS(RtlStringCchPrintfW(Pattern, RTL_NUMBER_OF(Pattern), L"%s*", Directory)))
        return;

    Find = FindFirstFileW(Pattern, &FindData);
    if (Find == INVALID_HANDLE_VALUE)
        return;

    do
    {
        if ((FindData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) &&
            BasepIsLocaleDirectoryName(FindData.cFileName))
        {
            BasepAddLanguage(Languages, FindData.cFileName, wcslen(FindData.cFileName));
        }
    } while (FindNextFileW(Find, &FindData));

    FindClose(Find);
}

static
BOOL
BasepFileExists(
    _In_ PCWSTR Path)
{
    DWORD Attributes = GetFileAttributesW(Path);

    return Attributes != INVALID_FILE_ATTRIBUTES && !(Attributes & FILE_ATTRIBUTE_DIRECTORY);
}

static
BOOL
BasepGetFileMuiConfig(
    _In_ PCWSTR Path,
    _Out_ PBASEP_MUI_CONFIG Config)
{
    HMODULE Module;
    BOOL Result;

    Module = LoadLibraryExW(Path, NULL, LOAD_LIBRARY_AS_DATAFILE | LOAD_LIBRARY_AS_IMAGE_RESOURCE);
    if (!Module)
        return FALSE;

    Result = BasepGetMuiConfig(Module, Config);
    FreeLibrary(Module);
    return Result;
}

static
BOOL
BasepFormatMuiLanguage(
    _In_ DWORD Flags,
    _In_ PCWSTR Name,
    _Out_writes_(LOCALE_NAME_MAX_LENGTH) PWSTR Language)
{
    LCID Lcid;

    if (!(Flags & MUI_LANGUAGE_ID))
        return NT_SUCCESS(RtlStringCchCopyW(Language, LOCALE_NAME_MAX_LENGTH, Name));

    if (!NT_SUCCESS(RtlLocaleNameToLcid(Name, &Lcid, RTL_LOCALE_ALLOW_NEUTRAL_NAMES)))
        return FALSE;

    return NT_SUCCESS(RtlStringCchPrintfW(Language, LOCALE_NAME_MAX_LENGTH, L"%04x", LANGIDFROMLCID(Lcid)));
}

BOOL
WINAPI
GetFileMUIPath(
    _In_ DWORD dwFlags,
    _In_ PCWSTR pcwszFilePath,
    _Inout_updates_opt_(*pcchLanguage) PWSTR pwszLanguage,
    _Inout_ PULONG pcchLanguage,
    _Out_writes_opt_(*pcchFileMUIPath) PWSTR pwszFileMUIPath,
    _Inout_ PULONG pcchFileMUIPath,
    _Inout_ PULONGLONG pululEnumerator)
{
    static const DWORD FilterFlags = MUI_USER_PREFERRED_UI_LANGUAGES | MUI_USE_INSTALLED_LANGUAGES | MUI_USE_SEARCH_ALL_LANGUAGES;
    PBASEP_MUI_LANGUAGES Languages;
    BASEP_MUI_CONFIG Config;
    WCHAR Directory[MAX_PATH], Candidate[MAX_PATH], Language[LOCALE_NAME_MAX_LENGTH];
    PCWSTR FileName, Slash, Extension = L"";
    SIZE_T DirectoryLength;
    ULONGLONG Index;
    ULONG Required;
    DWORD Filter, LastError = GetLastError();
    BOOL LanguageNeutral = FALSE;

    RtlZeroMemory(&Config, sizeof(Config));

    if (!pcwszFilePath || !pcchFileMUIPath || !pululEnumerator ||
        (pwszLanguage && !pcchLanguage) ||
        (dwFlags & (MUI_LANGUAGE_ID | MUI_LANGUAGE_NAME)) == (MUI_LANGUAGE_ID | MUI_LANGUAGE_NAME) ||
        (dwFlags & (MUI_LANG_NEUTRAL_PE_FILE | MUI_NON_LANG_NEUTRAL_FILE)) == (MUI_LANG_NEUTRAL_PE_FILE | MUI_NON_LANG_NEUTRAL_FILE))
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    Filter = dwFlags & FilterFlags;
    if (Filter & (Filter - 1))
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    Slash = wcsrchr(pcwszFilePath, L'\\');
    FileName = Slash ? Slash + 1 : pcwszFilePath;
    DirectoryLength = FileName - pcwszFilePath;
    if (!*FileName || DirectoryLength >= RTL_NUMBER_OF(Directory))
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    RtlCopyMemory(Directory, pcwszFilePath, DirectoryLength * sizeof(WCHAR));
    Directory[DirectoryLength] = UNICODE_NULL;

    if (dwFlags & MUI_LANG_NEUTRAL_PE_FILE)
    {
        LanguageNeutral = TRUE;
    }
    else if (!(dwFlags & MUI_NON_LANG_NEUTRAL_FILE))
    {
        LanguageNeutral = BasepGetFileMuiConfig(pcwszFilePath, &Config) &&
                          Config.FileType == BASEP_MUI_FILE_LANGUAGE_NEUTRAL;
    }
    if (LanguageNeutral)
        Extension = L".mui";

    Languages = RtlAllocateHeap(RtlGetProcessHeap(), 0, sizeof(*Languages));
    if (!Languages)
    {
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return FALSE;
    }

    if (Filter == MUI_USE_SEARCH_ALL_LANGUAGES)
    {
        BasepGetDirectoryLanguages(Directory, Languages);
    }
    else
    {
        BasepGetPreferredLanguages(Languages, Filter == MUI_USE_INSTALLED_LANGUAGES);
        if (Filter != MUI_USE_INSTALLED_LANGUAGES && LanguageNeutral &&
            !(dwFlags & MUI_LANG_NEUTRAL_PE_FILE) && Config.FallbackLanguage[0])
        {
            BasepAddLanguage(Languages, Config.FallbackLanguage, wcslen(Config.FallbackLanguage));
        }
    }

    for (Index = *pululEnumerator; Index < Languages->Count; Index++)
    {
        if (!NT_SUCCESS(RtlStringCchPrintfW(Candidate, RTL_NUMBER_OF(Candidate), L"%s%s\\%s%s",
                                    Directory, Languages->Names[Index], FileName, Extension)) ||
            !BasepFileExists(Candidate) ||
            !BasepFormatMuiLanguage(dwFlags, Languages->Names[Index], Language))
        {
            continue;
        }

        RtlFreeHeap(RtlGetProcessHeap(), 0, Languages);

        Required = (ULONG)wcslen(Candidate) + 1;
        if (!pwszFileMUIPath || !*pcchFileMUIPath)
        {
            *pcchFileMUIPath = Required;
            if (!pwszLanguage && pcchLanguage && !*pcchLanguage)
                *pcchLanguage = LOCALE_NAME_MAX_LENGTH;
            SetLastError(LastError);
            return TRUE;
        }

        if (*pcchFileMUIPath < Required)
            *pcchFileMUIPath = Required;
        else
            RtlCopyMemory(pwszFileMUIPath, Candidate, Required * sizeof(WCHAR));

        if (pwszLanguage)
        {
            if (*pcchLanguage > wcslen(Language))
                RtlStringCchCopyW(pwszLanguage, *pcchLanguage, Language);
        }
        else if (pcchLanguage && !*pcchLanguage)
        {
            *pcchLanguage = LOCALE_NAME_MAX_LENGTH;
        }

        *pululEnumerator = Index + 1;
        SetLastError(LastError);
        return TRUE;
    }

    RtlFreeHeap(RtlGetProcessHeap(), 0, Languages);
    SetLastError(ERROR_NO_MORE_FILES);
    return FALSE;
}

static
BOOL
BasepMapMuiFile(
    _In_ PCUNICODE_STRING Path,
    _In_ PBASEP_MUI_CONFIG ModuleConfig,
    _Out_ PVOID *ViewBase)
{
    OBJECT_ATTRIBUTES ObjectAttributes;
    IO_STATUS_BLOCK IoStatusBlock;
    BASEP_MUI_CONFIG Config;
    HANDLE FileHandle, SectionHandle;
    SIZE_T ViewSize = 0;
    NTSTATUS Status;

    *ViewBase = NULL;

    InitializeObjectAttributes(&ObjectAttributes, (PUNICODE_STRING)Path, OBJ_CASE_INSENSITIVE, NULL, NULL);
    Status = NtOpenFile(&FileHandle,
                        FILE_READ_DATA | SYNCHRONIZE,
                        &ObjectAttributes,
                        &IoStatusBlock,
                        FILE_SHARE_READ | FILE_SHARE_DELETE,
                        FILE_SYNCHRONOUS_IO_NONALERT | FILE_NON_DIRECTORY_FILE);
    if (!NT_SUCCESS(Status))
        return FALSE;

    Status = NtCreateSection(&SectionHandle,
                             SECTION_MAP_READ | SECTION_QUERY,
                             NULL,
                             NULL,
                             PAGE_READONLY,
                             SEC_COMMIT,
                             FileHandle);
    NtClose(FileHandle);
    if (!NT_SUCCESS(Status))
        return FALSE;

    Status = NtMapViewOfSection(SectionHandle,
                                NtCurrentProcess(),
                                ViewBase,
                                0,
                                0,
                                NULL,
                                &ViewSize,
                                ViewShare,
                                0,
                                PAGE_READONLY);
    NtClose(SectionHandle);
    if (!NT_SUCCESS(Status))
    {
        *ViewBase = NULL;
        return FALSE;
    }

    if (!RtlImageNtHeader(*ViewBase) ||
        !BasepGetMuiConfig((PVOID)((ULONG_PTR)*ViewBase | 1), &Config) ||
        Config.FileType != BASEP_MUI_FILE_LANGUAGE_SPECIFIC ||
        RtlCompareMemory(Config.Checksum, ModuleConfig->Checksum, BASEP_MUI_RC_CHECKSUM_SIZE) != BASEP_MUI_RC_CHECKSUM_SIZE)
    {
        NtUnmapViewOfSection(NtCurrentProcess(), *ViewBase);
        *ViewBase = NULL;
        return FALSE;
    }

    return TRUE;
}

static
PVOID
BasepMapModuleMuiFile(
    _In_ HMODULE Module,
    _In_ LANGID LanguageId)
{
    UCHAR NameBuffer[sizeof(MEMORY_SECTION_NAME) + (MAX_PATH * 2 * sizeof(WCHAR))];
    PMEMORY_SECTION_NAME SectionName = (PMEMORY_SECTION_NAME)NameBuffer;
    PBASEP_MUI_LANGUAGES Languages;
    BASEP_MUI_CONFIG Config;
    UNICODE_STRING Path;
    WCHAR LanguageName[LOCALE_NAME_MAX_LENGTH];
    PWSTR FileName;
    SIZE_T ReturnLength, DirectoryLength;
    PVOID ViewBase = NULL;
    ULONG Index;

    if (!BasepGetMuiConfig(Module, &Config) || Config.FileType != BASEP_MUI_FILE_LANGUAGE_NEUTRAL)
        return NULL;

    if (!NT_SUCCESS(NtQueryVirtualMemory(NtCurrentProcess(),
                                         (PVOID)((ULONG_PTR)Module & ~(ULONG_PTR)3),
                                         MemorySectionName,
                                         SectionName,
                                         sizeof(NameBuffer),
                                         &ReturnLength)))
    {
        return NULL;
    }

    FileName = SectionName->SectionFileName.Buffer + SectionName->SectionFileName.Length / sizeof(WCHAR);
    while (FileName > SectionName->SectionFileName.Buffer && FileName[-1] != L'\\')
        FileName--;
    DirectoryLength = FileName - SectionName->SectionFileName.Buffer;
    if (!DirectoryLength || DirectoryLength * sizeof(WCHAR) >= SectionName->SectionFileName.Length)
        return NULL;

    Languages = RtlAllocateHeap(RtlGetProcessHeap(), 0, sizeof(*Languages));
    if (!Languages)
        return NULL;

    Languages->Count = 0;
    if (PRIMARYLANGID(LanguageId) != LANG_NEUTRAL)
    {
        UNICODE_STRING Name;

        Name.Buffer = LanguageName;
        Name.Length = 0;
        Name.MaximumLength = sizeof(LanguageName) - sizeof(WCHAR);
        if (NT_SUCCESS(RtlLcidToLocaleName(MAKELCID(LanguageId, SORT_DEFAULT), &Name, RTL_LOCALE_ALLOW_NEUTRAL_NAMES, FALSE)))
            BasepAddLanguage(Languages, LanguageName, Name.Length / sizeof(WCHAR));
    }
    else
    {
        BasepGetPreferredLanguages(Languages, FALSE);
        if (Config.FallbackLanguage[0])
            BasepAddLanguage(Languages, Config.FallbackLanguage, wcslen(Config.FallbackLanguage));
    }

    Path.MaximumLength = (USHORT)(SectionName->SectionFileName.Length + (LOCALE_NAME_MAX_LENGTH + 6) * sizeof(WCHAR));
    Path.Buffer = RtlAllocateHeap(RtlGetProcessHeap(), 0, Path.MaximumLength);
    if (!Path.Buffer)
    {
        RtlFreeHeap(RtlGetProcessHeap(), 0, Languages);
        return NULL;
    }

    for (Index = 0; Index < Languages->Count && !ViewBase; Index++)
    {
        if (!NT_SUCCESS(RtlStringCbPrintfW(Path.Buffer, Path.MaximumLength, L"%.*s%s\\%.*s.mui",
                                           (int)DirectoryLength, SectionName->SectionFileName.Buffer,
                                           Languages->Names[Index],
                                           (int)(SectionName->SectionFileName.Length / sizeof(WCHAR) - DirectoryLength),
                                           FileName)))
        {
            continue;
        }
        Path.Length = (USHORT)(wcslen(Path.Buffer) * sizeof(WCHAR));
        BasepMapMuiFile(&Path, &Config, &ViewBase);
    }

    RtlFreeHeap(RtlGetProcessHeap(), 0, Path.Buffer);
    RtlFreeHeap(RtlGetProcessHeap(), 0, Languages);
    return ViewBase ? (PVOID)((ULONG_PTR)ViewBase | 1) : NULL;
}

static
VOID
BasepUnmapModuleMuiFile(
    _In_opt_ PVOID MuiModule)
{
    if (MuiModule)
        NtUnmapViewOfSection(NtCurrentProcess(), (PVOID)((ULONG_PTR)MuiModule & ~(ULONG_PTR)3));
}

static
LONG
BasepCompareResItems(
    _In_ PBASEP_RES_ITEM Item1,
    _In_ PBASEP_RES_ITEM Item2)
{
    if (Item1->Name && Item2->Name)
        return _wcsicmp(Item1->Name, Item2->Name);
    if (Item1->Name)
        return -1;
    if (Item2->Name)
        return 1;
    if (Item1->Id == Item2->Id)
        return 0;
    return Item1->Id < Item2->Id ? -1 : 1;
}

static
BOOL
BasepInsertResItem(
    _Inout_ PBASEP_RES_LIST List,
    _In_ ULONG_PTR Id,
    _In_opt_ const IMAGE_RESOURCE_DIR_STRING_U *String)
{
    BASEP_RES_ITEM Item;
    ULONG Index;
    LONG Compare;

    Item.Id = Id;
    Item.Name = NULL;
    if (String)
    {
        Item.Name = RtlAllocateHeap(RtlGetProcessHeap(), 0, (String->Length + 1) * sizeof(WCHAR));
        if (!Item.Name)
            return FALSE;
        RtlCopyMemory(Item.Name, String->NameString, String->Length * sizeof(WCHAR));
        Item.Name[String->Length] = UNICODE_NULL;
    }

    for (Index = 0; Index < List->Count; Index++)
    {
        Compare = BasepCompareResItems(&Item, &List->Items[Index]);
        if (!Compare)
        {
            if (Item.Name)
                RtlFreeHeap(RtlGetProcessHeap(), 0, Item.Name);
            return TRUE;
        }
        if (Compare < 0)
            break;
    }

    if (List->Count == List->Capacity)
    {
        ULONG Capacity = List->Capacity ? List->Capacity * 2 : 16;
        PBASEP_RES_ITEM Items;

        if (List->Items)
            Items = RtlReAllocateHeap(RtlGetProcessHeap(), 0, List->Items, Capacity * sizeof(*Items));
        else
            Items = RtlAllocateHeap(RtlGetProcessHeap(), 0, Capacity * sizeof(*Items));
        if (!Items)
        {
            if (Item.Name)
                RtlFreeHeap(RtlGetProcessHeap(), 0, Item.Name);
            return FALSE;
        }
        List->Items = Items;
        List->Capacity = Capacity;
    }

    RtlMoveMemory(&List->Items[Index + 1], &List->Items[Index], (List->Count - Index) * sizeof(*List->Items));
    List->Items[Index] = Item;
    List->Count++;
    return TRUE;
}

static
VOID
BasepFreeResList(
    _Inout_ PBASEP_RES_LIST List)
{
    ULONG Index;

    for (Index = 0; Index < List->Count; Index++)
    {
        if (List->Items[Index].Name)
            RtlFreeHeap(RtlGetProcessHeap(), 0, List->Items[Index].Name);
    }
    if (List->Items)
        RtlFreeHeap(RtlGetProcessHeap(), 0, List->Items);
    RtlZeroMemory(List, sizeof(*List));
}

static
NTSTATUS
BasepCollectResDirectory(
    _In_ PVOID Module,
    _In_ PLDR_RESOURCE_INFO Info,
    _In_ ULONG Level,
    _In_ BOOL SkipMuiType,
    _Inout_ PBASEP_RES_LIST List)
{
    PIMAGE_RESOURCE_DIRECTORY Root, Directory;
    PIMAGE_RESOURCE_DIRECTORY_ENTRY Entries;
    const IMAGE_RESOURCE_DIR_STRING_U *String;
    NTSTATUS Status;
    ULONG Index;

    Status = LdrFindResourceDirectory_U(Module, NULL, 0, &Root);
    if (!NT_SUCCESS(Status))
        return Status;
    if (Level)
    {
        Status = LdrFindResourceDirectory_U(Module, Info, Level, &Directory);
        if (!NT_SUCCESS(Status))
            return Status;
    }
    else
    {
        Directory = Root;
    }

    _SEH2_TRY
    {
        Entries = (PIMAGE_RESOURCE_DIRECTORY_ENTRY)(Directory + 1);
        for (Index = 0; Index < (ULONG)Directory->NumberOfNamedEntries + Directory->NumberOfIdEntries; Index++)
        {
            if (Entries[Index].NameIsString)
            {
                String = (const IMAGE_RESOURCE_DIR_STRING_U *)((PUCHAR)Root + Entries[Index].NameOffset);
                if (SkipMuiType && String->Length == 3 && !_wcsnicmp(String->NameString, L"MUI", 3))
                    continue;
                if (!BasepInsertResItem(List, 0, String))
                {
                    Status = STATUS_NO_MEMORY;
                    break;
                }
            }
            else if (!BasepInsertResItem(List, Entries[Index].Id, NULL))
            {
                Status = STATUS_NO_MEMORY;
                break;
            }
        }
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Status = STATUS_ACCESS_VIOLATION;
    }
    _SEH2_END;

    return Status;
}

static
BOOL
BasepGetResourceName(
    _In_ LPCWSTR Name,
    _Out_ PULONG_PTR Value,
    _Out_ PWSTR *Allocated)
{
    UNICODE_STRING String;
    ULONG Number;

    *Allocated = NULL;
    if (IS_INTRESOURCE(Name))
    {
        *Value = (ULONG_PTR)LOWORD(Name);
        return TRUE;
    }

    if (Name[0] == L'#')
    {
        RtlInitUnicodeString(&String, Name + 1);
        if (!NT_SUCCESS(RtlUnicodeStringToInteger(&String, 10, &Number)) || HIWORD(Number))
            return FALSE;
        *Value = Number;
        return TRUE;
    }

    *Allocated = RtlAllocateHeap(RtlGetProcessHeap(), 0, (wcslen(Name) + 1) * sizeof(WCHAR));
    if (!*Allocated)
        return FALSE;
    wcscpy(*Allocated, Name);
    _wcsupr(*Allocated);
    *Value = (ULONG_PTR)*Allocated;
    return TRUE;
}

static
BOOL
BasepEnumResources(
    _In_opt_ HMODULE Module,
    _In_opt_ LPCWSTR Type,
    _In_opt_ LPCWSTR Name,
    _In_ ULONG Level,
    _In_ DWORD Flags,
    _In_ LANGID LanguageId,
    _In_ PBASEP_RES_CALLBACK Callback,
    _In_ PVOID Context)
{
    LDR_RESOURCE_INFO Info = { 0 };
    BASEP_RES_LIST List = { 0 };
    PWSTR TypeBuffer = NULL, NameBuffer = NULL;
    PVOID MuiModule = NULL;
    NTSTATUS Status = STATUS_RESOURCE_DATA_NOT_FOUND, MuiStatus = STATUS_RESOURCE_DATA_NOT_FOUND;
    BOOL Result = FALSE;
    ULONG Index;

    if (!Flags)
        Flags = RESOURCE_ENUM_LN | RESOURCE_ENUM_MUI;
    if (!(Flags & (RESOURCE_ENUM_LN | RESOURCE_ENUM_MUI | RESOURCE_ENUM_MUI_SYSTEM)))
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    if (!Module)
        Module = GetModuleHandleW(NULL);

    if ((Level >= 1 && !BasepGetResourceName(Type, &Info.Type, &TypeBuffer)) ||
        (Level >= 2 && !BasepGetResourceName(Name, &Info.Name, &NameBuffer)))
    {
        Status = STATUS_INVALID_PARAMETER;
        goto Done;
    }

    if (Flags & RESOURCE_ENUM_LN)
    {
        Status = BasepCollectResDirectory(Module, &Info, Level, FALSE, &List);
        if (Status == STATUS_NO_MEMORY)
            goto Done;
    }

    if (Flags & (RESOURCE_ENUM_MUI | RESOURCE_ENUM_MUI_SYSTEM))
    {
        MuiModule = BasepMapModuleMuiFile(Module, LanguageId);
        if (MuiModule)
        {
            MuiStatus = BasepCollectResDirectory(MuiModule, &Info, Level, Level == 0, &List);
            if (MuiStatus == STATUS_NO_MEMORY)
            {
                Status = MuiStatus;
                goto Done;
            }
        }
        if (!(Flags & RESOURCE_ENUM_LN))
            Status = MuiModule ? MuiStatus : STATUS_MUI_FILE_NOT_FOUND;
    }

    if (!List.Count)
        goto Done;

    BasepUnmapModuleMuiFile(MuiModule);
    MuiModule = NULL;

    Status = STATUS_SUCCESS;
    for (Index = 0; Index < List.Count; Index++)
    {
        if (!Callback(Module, &List.Items[Index], Context))
        {
            BasepFreeResList(&List);
            if (TypeBuffer)
                RtlFreeHeap(RtlGetProcessHeap(), 0, TypeBuffer);
            if (NameBuffer)
                RtlFreeHeap(RtlGetProcessHeap(), 0, NameBuffer);
            SetLastError(ERROR_RESOURCE_ENUM_USER_STOP);
            return FALSE;
        }
    }
    Result = TRUE;

Done:
    BasepUnmapModuleMuiFile(MuiModule);
    BasepFreeResList(&List);
    if (TypeBuffer)
        RtlFreeHeap(RtlGetProcessHeap(), 0, TypeBuffer);
    if (NameBuffer)
        RtlFreeHeap(RtlGetProcessHeap(), 0, NameBuffer);
    if (!Result)
        BaseSetLastNTError(Status);
    return Result;
}

static
PSTR
BasepResNameToAnsi(
    _In_ PBASEP_RES_ITEM Item)
{
    PSTR Name;
    int Length;

    Length = WideCharToMultiByte(CP_ACP, 0, Item->Name, -1, NULL, 0, NULL, NULL);
    Name = RtlAllocateHeap(RtlGetProcessHeap(), 0, Length);
    if (Name)
        WideCharToMultiByte(CP_ACP, 0, Item->Name, -1, Name, Length, NULL, NULL);
    return Name;
}

static
PWSTR
BasepResNameToUnicode(
    _In_ LPCSTR Name)
{
    PWSTR Unicode;
    int Length;

    if (IS_INTRESOURCE(Name))
        return (PWSTR)Name;

    Length = MultiByteToWideChar(CP_ACP, 0, Name, -1, NULL, 0);
    Unicode = RtlAllocateHeap(RtlGetProcessHeap(), 0, Length * sizeof(WCHAR));
    if (Unicode)
        MultiByteToWideChar(CP_ACP, 0, Name, -1, Unicode, Length);
    return Unicode;
}

static
VOID
BasepFreeResNameUnicode(
    _In_ LPCSTR Name,
    _In_opt_ PWSTR Unicode)
{
    if (!IS_INTRESOURCE(Name) && Unicode)
        RtlFreeHeap(RtlGetProcessHeap(), 0, Unicode);
}

typedef struct _BASEP_ENUM_TYPES_CONTEXT
{
    PVOID Proc;
    LONG_PTR Param;
} BASEP_ENUM_TYPES_CONTEXT, *PBASEP_ENUM_TYPES_CONTEXT;

typedef struct _BASEP_ENUM_NAMES_CONTEXT
{
    PVOID Proc;
    LONG_PTR Param;
    LPCVOID Type;
} BASEP_ENUM_NAMES_CONTEXT, *PBASEP_ENUM_NAMES_CONTEXT;

typedef struct _BASEP_ENUM_LANGUAGES_CONTEXT
{
    PVOID Proc;
    LONG_PTR Param;
    LPCVOID Type;
    LPCVOID Name;
} BASEP_ENUM_LANGUAGES_CONTEXT, *PBASEP_ENUM_LANGUAGES_CONTEXT;

static
BOOL
BasepEnumTypeW(
    _In_ HMODULE Module,
    _In_ PBASEP_RES_ITEM Item,
    _In_ PVOID Context)
{
    PBASEP_ENUM_TYPES_CONTEXT Types = Context;

    return ((ENUMRESTYPEPROCW)Types->Proc)(Module, Item->Name ? Item->Name : (LPWSTR)Item->Id, Types->Param);
}

static
BOOL
BasepEnumTypeA(
    _In_ HMODULE Module,
    _In_ PBASEP_RES_ITEM Item,
    _In_ PVOID Context)
{
    PBASEP_ENUM_TYPES_CONTEXT Types = Context;
    PSTR Name;
    BOOL Result;

    if (!Item->Name)
        return ((ENUMRESTYPEPROCA)Types->Proc)(Module, (LPSTR)Item->Id, Types->Param);

    Name = BasepResNameToAnsi(Item);
    if (!Name)
        return FALSE;
    Result = ((ENUMRESTYPEPROCA)Types->Proc)(Module, Name, Types->Param);
    RtlFreeHeap(RtlGetProcessHeap(), 0, Name);
    return Result;
}

static
BOOL
BasepEnumNameW(
    _In_ HMODULE Module,
    _In_ PBASEP_RES_ITEM Item,
    _In_ PVOID Context)
{
    PBASEP_ENUM_NAMES_CONTEXT Names = Context;

    return ((ENUMRESNAMEPROCW)Names->Proc)(Module, (LPCWSTR)Names->Type, Item->Name ? Item->Name : (LPWSTR)Item->Id, Names->Param);
}

static
BOOL
BasepEnumNameA(
    _In_ HMODULE Module,
    _In_ PBASEP_RES_ITEM Item,
    _In_ PVOID Context)
{
    PBASEP_ENUM_NAMES_CONTEXT Names = Context;
    PSTR Name;
    BOOL Result;

    if (!Item->Name)
        return ((ENUMRESNAMEPROCA)Names->Proc)(Module, (LPCSTR)Names->Type, (LPSTR)Item->Id, Names->Param);

    Name = BasepResNameToAnsi(Item);
    if (!Name)
        return FALSE;
    Result = ((ENUMRESNAMEPROCA)Names->Proc)(Module, (LPCSTR)Names->Type, Name, Names->Param);
    RtlFreeHeap(RtlGetProcessHeap(), 0, Name);
    return Result;
}

static
BOOL
BasepEnumLanguageW(
    _In_ HMODULE Module,
    _In_ PBASEP_RES_ITEM Item,
    _In_ PVOID Context)
{
    PBASEP_ENUM_LANGUAGES_CONTEXT Languages = Context;

    return ((ENUMRESLANGPROCW)Languages->Proc)(Module, (LPCWSTR)Languages->Type, (LPCWSTR)Languages->Name, (WORD)Item->Id, Languages->Param);
}

static
BOOL
BasepEnumLanguageA(
    _In_ HMODULE Module,
    _In_ PBASEP_RES_ITEM Item,
    _In_ PVOID Context)
{
    PBASEP_ENUM_LANGUAGES_CONTEXT Languages = Context;

    return ((ENUMRESLANGPROCA)Languages->Proc)(Module, (LPCSTR)Languages->Type, (LPCSTR)Languages->Name, (WORD)Item->Id, Languages->Param);
}

BOOL
WINAPI
EnumResourceTypesExW(
    _In_opt_ HMODULE hModule,
    _In_ ENUMRESTYPEPROCW lpEnumFunc,
    _In_ LONG_PTR lParam,
    _In_ DWORD dwFlags,
    _In_ LANGID LangId)
{
    BASEP_ENUM_TYPES_CONTEXT Context = { (PVOID)lpEnumFunc, lParam };

    return BasepEnumResources(hModule, NULL, NULL, 0, dwFlags, LangId, BasepEnumTypeW, &Context);
}

BOOL
WINAPI
EnumResourceTypesExA(
    _In_opt_ HMODULE hModule,
    _In_ ENUMRESTYPEPROCA lpEnumFunc,
    _In_ LONG_PTR lParam,
    _In_ DWORD dwFlags,
    _In_ LANGID LangId)
{
    BASEP_ENUM_TYPES_CONTEXT Context = { (PVOID)lpEnumFunc, lParam };

    return BasepEnumResources(hModule, NULL, NULL, 0, dwFlags, LangId, BasepEnumTypeA, &Context);
}

BOOL
WINAPI
EnumResourceNamesExW(
    _In_opt_ HMODULE hModule,
    _In_ LPCWSTR lpType,
    _In_ ENUMRESNAMEPROCW lpEnumFunc,
    _In_ LONG_PTR lParam,
    _In_ DWORD dwFlags,
    _In_ LANGID LangId)
{
    BASEP_ENUM_NAMES_CONTEXT Context = { (PVOID)lpEnumFunc, lParam, lpType };

    return BasepEnumResources(hModule, lpType, NULL, 1, dwFlags, LangId, BasepEnumNameW, &Context);
}

BOOL
WINAPI
EnumResourceNamesExA(
    _In_opt_ HMODULE hModule,
    _In_ LPCSTR lpType,
    _In_ ENUMRESNAMEPROCA lpEnumFunc,
    _In_ LONG_PTR lParam,
    _In_ DWORD dwFlags,
    _In_ LANGID LangId)
{
    BASEP_ENUM_NAMES_CONTEXT Context = { (PVOID)lpEnumFunc, lParam, lpType };
    PWSTR Type;
    BOOL Result;

    Type = BasepResNameToUnicode(lpType);
    if (!Type)
    {
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return FALSE;
    }
    Result = BasepEnumResources(hModule, Type, NULL, 1, dwFlags, LangId, BasepEnumNameA, &Context);
    BasepFreeResNameUnicode(lpType, Type);
    return Result;
}

BOOL
WINAPI
EnumResourceLanguagesExW(
    _In_opt_ HMODULE hModule,
    _In_ LPCWSTR lpType,
    _In_ LPCWSTR lpName,
    _In_ ENUMRESLANGPROCW lpEnumFunc,
    _In_ LONG_PTR lParam,
    _In_ DWORD dwFlags,
    _In_ LANGID LangId)
{
    BASEP_ENUM_LANGUAGES_CONTEXT Context = { (PVOID)lpEnumFunc, lParam, lpType, lpName };

    return BasepEnumResources(hModule, lpType, lpName, 2, dwFlags, LangId, BasepEnumLanguageW, &Context);
}

BOOL
WINAPI
EnumResourceLanguagesExA(
    _In_opt_ HMODULE hModule,
    _In_ LPCSTR lpType,
    _In_ LPCSTR lpName,
    _In_ ENUMRESLANGPROCA lpEnumFunc,
    _In_ LONG_PTR lParam,
    _In_ DWORD dwFlags,
    _In_ LANGID LangId)
{
    BASEP_ENUM_LANGUAGES_CONTEXT Context = { (PVOID)lpEnumFunc, lParam, lpType, lpName };
    PWSTR Type, Name;
    BOOL Result = FALSE;

    Type = BasepResNameToUnicode(lpType);
    Name = BasepResNameToUnicode(lpName);
    if (Type && Name)
        Result = BasepEnumResources(hModule, Type, Name, 2, dwFlags, LangId, BasepEnumLanguageA, &Context);
    else
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
    BasepFreeResNameUnicode(lpType, Type);
    BasepFreeResNameUnicode(lpName, Name);
    return Result;
}

BOOL
WINAPI
EnumResourceTypesW(
    _In_opt_ HMODULE hModule,
    _In_ ENUMRESTYPEPROCW lpEnumFunc,
    _In_ LONG_PTR lParam)
{
    return EnumResourceTypesExW(hModule, lpEnumFunc, lParam, 0, 0);
}

BOOL
WINAPI
EnumResourceTypesA(
    _In_opt_ HMODULE hModule,
    _In_ ENUMRESTYPEPROCA lpEnumFunc,
    _In_ LONG_PTR lParam)
{
    return EnumResourceTypesExA(hModule, lpEnumFunc, lParam, 0, 0);
}

BOOL
WINAPI
EnumResourceNamesW(
    _In_opt_ HMODULE hModule,
    _In_ LPCWSTR lpType,
    _In_ ENUMRESNAMEPROCW lpEnumFunc,
    _In_ LONG_PTR lParam)
{
    return EnumResourceNamesExW(hModule, lpType, lpEnumFunc, lParam, 0, 0);
}

BOOL
WINAPI
EnumResourceNamesA(
    _In_opt_ HMODULE hModule,
    _In_ LPCSTR lpType,
    _In_ ENUMRESNAMEPROCA lpEnumFunc,
    _In_ LONG_PTR lParam)
{
    return EnumResourceNamesExA(hModule, lpType, lpEnumFunc, lParam, 0, 0);
}

BOOL
WINAPI
EnumResourceLanguagesW(
    _In_opt_ HMODULE hModule,
    _In_ LPCWSTR lpType,
    _In_ LPCWSTR lpName,
    _In_ ENUMRESLANGPROCW lpEnumFunc,
    _In_ LONG_PTR lParam)
{
    return EnumResourceLanguagesExW(hModule, lpType, lpName, lpEnumFunc, lParam, 0, 0);
}

BOOL
WINAPI
EnumResourceLanguagesA(
    _In_opt_ HMODULE hModule,
    _In_ LPCSTR lpType,
    _In_ LPCSTR lpName,
    _In_ ENUMRESLANGPROCA lpEnumFunc,
    _In_ LONG_PTR lParam)
{
    return EnumResourceLanguagesExA(hModule, lpType, lpName, lpEnumFunc, lParam, 0, 0);
}
