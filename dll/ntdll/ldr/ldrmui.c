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

typedef struct _LDRP_MUI_MODULE
{
    LIST_ENTRY Links;
    PVOID BaseAddress;
    ULONG TimeDateStamp;
    ULONG SizeOfImage;
    PVOID ResourceBase;
    SIZE_T ViewSize;
} LDRP_MUI_MODULE, *PLDRP_MUI_MODULE;

#define LDRP_MUI_MAX_LANGUAGES 16
#define LDRP_MUI_LANGUAGE_LENGTH (LOCALE_NAME_MAX_LENGTH + 1)

static LIST_ENTRY LdrpMuiModuleList = { &LdrpMuiModuleList, &LdrpMuiModuleList };
static RTL_SRWLOCK LdrpMuiLock = RTL_SRWLOCK_INIT;

NTSTATUS LdrpFindModuleResourceEntry(PVOID BaseAddress, LDR_RESOURCE_INFO *Info, ULONG Level, PVOID *Entry, BOOLEAN WantDirectory);

static
PVOID
LdrpUntagResourceBase(
    _In_ PVOID BaseAddress)
{
    return (PVOID)((ULONG_PTR)BaseAddress & ~(ULONG_PTR)3);
}

BOOLEAN
NTAPI
LdrpModuleHasMuiConfiguration(
    _In_ PVOID BaseAddress)
{
    static const WCHAR MuiType[] = L"MUI";
    LDR_RESOURCE_INFO Info;
    PVOID Entry;

    Info.Type = (ULONG_PTR)MuiType;
    Info.Name = 1;
    Info.Language = 0;
    return NT_SUCCESS(LdrpFindModuleResourceEntry(BaseAddress, &Info, 3, &Entry, FALSE));
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
NTSTATUS
LdrpMapMuiFile(
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

    if (!RtlImageNtHeader(*ViewBase) ||
        !LdrpModuleHasMuiConfiguration((PVOID)((ULONG_PTR)*ViewBase | 1)))
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
    _Out_ PVOID *ViewBase,
    _Out_ PSIZE_T ViewSize)
{
    static const WCHAR Extension[] = L".mui";
    UCHAR NameBuffer[sizeof(MEMORY_SECTION_NAME) + (MAX_PATH * 2 * sizeof(WCHAR))];
    PMEMORY_SECTION_NAME SectionName = (PMEMORY_SECTION_NAME)NameBuffer;
    PWSTR Languages;
    UNICODE_STRING Path;
    PWSTR FileName;
    USHORT DirectoryLength, FileNameLength;
    ULONG Count, Index;
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
        return Status;

    FileName = SectionName->SectionFileName.Buffer + SectionName->SectionFileName.Length / sizeof(WCHAR);
    while (FileName > SectionName->SectionFileName.Buffer && FileName[-1] != L'\\')
        FileName--;
    DirectoryLength = (USHORT)((FileName - SectionName->SectionFileName.Buffer) * sizeof(WCHAR));
    FileNameLength = SectionName->SectionFileName.Length - DirectoryLength;
    if (!DirectoryLength || !FileNameLength)
        return STATUS_MUI_FILE_NOT_FOUND;

    Languages = RtlAllocateHeap(RtlGetProcessHeap(), 0, LDRP_MUI_MAX_LANGUAGES * LDRP_MUI_LANGUAGE_LENGTH * sizeof(WCHAR));
    if (!Languages)
        return STATUS_NO_MEMORY;

    Path.MaximumLength = DirectoryLength + (LDRP_MUI_LANGUAGE_LENGTH + 1) * sizeof(WCHAR) +
                         FileNameLength + sizeof(Extension);
    Path.Buffer = RtlAllocateHeap(RtlGetProcessHeap(), 0, Path.MaximumLength);
    if (!Path.Buffer)
    {
        RtlFreeHeap(RtlGetProcessHeap(), 0, Languages);
        return STATUS_NO_MEMORY;
    }

    Status = STATUS_MUI_FILE_NOT_FOUND;
    Count = LdrpBuildMuiFallbackList(Languages);
    for (Index = 0; Index < Count; Index++)
    {
        UNICODE_STRING Directory, Name, Language;

        Directory.Buffer = SectionName->SectionFileName.Buffer;
        Directory.Length = Directory.MaximumLength = DirectoryLength;
        Name.Buffer = FileName;
        Name.Length = Name.MaximumLength = FileNameLength;
        RtlInitUnicodeString(&Language, &Languages[Index * LDRP_MUI_LANGUAGE_LENGTH]);

        Path.Length = 0;
        RtlAppendUnicodeStringToString(&Path, &Directory);
        RtlAppendUnicodeStringToString(&Path, &Language);
        RtlAppendUnicodeToString(&Path, L"\\");
        RtlAppendUnicodeStringToString(&Path, &Name);
        RtlAppendUnicodeToString(&Path, Extension);

        Status = LdrpMapMuiFile(&Path, ViewBase, ViewSize);
        if (NT_SUCCESS(Status))
            break;
        Status = STATUS_MUI_FILE_NOT_FOUND;
    }

    RtlFreeHeap(RtlGetProcessHeap(), 0, Path.Buffer);
    RtlFreeHeap(RtlGetProcessHeap(), 0, Languages);
    return Status;
}

static
PLDRP_MUI_MODULE
LdrpLookupMuiModule(
    _In_ PVOID BaseAddress)
{
    PLIST_ENTRY Entry;

    for (Entry = LdrpMuiModuleList.Flink; Entry != &LdrpMuiModuleList; Entry = Entry->Flink)
    {
        PLDRP_MUI_MODULE Module = CONTAINING_RECORD(Entry, LDRP_MUI_MODULE, Links);

        if (Module->BaseAddress == BaseAddress)
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

NTSTATUS
NTAPI
LdrpGetAlternateResourceModule(
    _In_ PVOID BaseAddress,
    _Out_ PVOID *ResourceBase)
{
    PVOID Base = LdrpUntagResourceBase(BaseAddress);
    PIMAGE_NT_HEADERS NtHeaders;
    PLDRP_MUI_MODULE Module, Stale = NULL;
    ULONG TimeDateStamp, SizeOfImage;
    PVOID ViewBase;
    SIZE_T ViewSize;
    NTSTATUS Status;

    *ResourceBase = NULL;

    NtHeaders = RtlImageNtHeader(Base);
    if (!NtHeaders || !LdrpModuleHasMuiConfiguration(BaseAddress))
        return STATUS_RESOURCE_DATA_NOT_FOUND;

    TimeDateStamp = NtHeaders->FileHeader.TimeDateStamp;
    SizeOfImage = NtHeaders->OptionalHeader.SizeOfImage;

    RtlAcquireSRWLockShared(&LdrpMuiLock);
    Module = LdrpLookupMuiModule(Base);
    if (Module && Module->TimeDateStamp == TimeDateStamp && Module->SizeOfImage == SizeOfImage)
    {
        *ResourceBase = Module->ResourceBase;
        RtlReleaseSRWLockShared(&LdrpMuiLock);
        if (!*ResourceBase)
            return STATUS_MUI_FILE_NOT_LOADED;
        *ResourceBase = (PVOID)((ULONG_PTR)*ResourceBase | 1);
        return STATUS_SUCCESS;
    }
    RtlReleaseSRWLockShared(&LdrpMuiLock);

    Status = LdrpLoadMuiFile(BaseAddress, &ViewBase, &ViewSize);
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
    Module->ResourceBase = ViewBase;
    Module->ViewSize = ViewSize;

    RtlAcquireSRWLockExclusive(&LdrpMuiLock);
    {
        PLDRP_MUI_MODULE Existing = LdrpLookupMuiModule(Base);

        if (Existing && Existing->TimeDateStamp == TimeDateStamp && Existing->SizeOfImage == SizeOfImage)
        {
            *ResourceBase = Existing->ResourceBase;
            RtlReleaseSRWLockExclusive(&LdrpMuiLock);
            LdrpFreeMuiModule(Module);
            if (!*ResourceBase)
                return STATUS_MUI_FILE_NOT_LOADED;
            *ResourceBase = (PVOID)((ULONG_PTR)*ResourceBase | 1);
            return STATUS_SUCCESS;
        }

        if (Existing)
        {
            RemoveEntryList(&Existing->Links);
            Stale = Existing;
        }

        InsertTailList(&LdrpMuiModuleList, &Module->Links);
    }
    RtlReleaseSRWLockExclusive(&LdrpMuiLock);

    if (Stale)
        LdrpFreeMuiModule(Stale);

    if (!ViewBase)
        return STATUS_MUI_FILE_NOT_FOUND;

    *ResourceBase = (PVOID)((ULONG_PTR)ViewBase | 1);
    return STATUS_SUCCESS;
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
    PLDRP_MUI_MODULE Module;

    RtlAcquireSRWLockExclusive(&LdrpMuiLock);
    Module = LdrpLookupMuiModule(Base);
    if (Module)
        RemoveEntryList(&Module->Links);
    RtlReleaseSRWLockExclusive(&LdrpMuiLock);

    if (!Module)
        return FALSE;

    LdrpFreeMuiModule(Module);
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
    {
        LdrpFreeMuiModule(CONTAINING_RECORD(RemoveHeadList(&Detached), LDRP_MUI_MODULE, Links));
    }

    return TRUE;
}
