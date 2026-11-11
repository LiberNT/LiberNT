/*
 * PROJECT:     LiberNT NT User-Mode DLL
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Preferred user-interface language queries
 * COPYRIGHT:   Copyright 2026 Ahmed Arif <arif.ing@outlook.com>
 */

#include <ntdll.h>
#include <winnls.h>

#define NDEBUG
#include <debug.h>

typedef struct _RTL_MUI_REGISTRY_VALUE
{
    PKEY_VALUE_PARTIAL_INFORMATION Information;
    PCWSTR Languages;
    ULONG CharacterCount;
} RTL_MUI_REGISTRY_VALUE, *PRTL_MUI_REGISTRY_VALUE;

typedef struct _RTL_MUI_THREAD_LANGUAGES
{
    ULONG Filter;
    WCHAR Languages[ANYSIZE_ARRAY];
} RTL_MUI_THREAD_LANGUAGES, *PRTL_MUI_THREAD_LANGUAGES;

typedef struct _RTL_NLS_LOCALE_HEADER
{
    UINT Offset;
    UINT Unknown1;
    UINT Version;
    UINT Magic;
    UINT Unknown2[3];
    USHORT HeaderSize;
    USHORT LcidCount;
    USHORT LocaleCount;
    USHORT LocaleSize;
    UINT LocalesOffset;
    USHORT LcnameCount;
    USHORT Pad;
    UINT LcidsOffset;
    UINT LcnamesOffset;
    UINT Unknown3;
    USHORT CalendarCount;
    USHORT CalendarSize;
    UINT CalendarsOffset;
    UINT StringsOffset;
} RTL_NLS_LOCALE_HEADER;

typedef struct _RTL_NLS_LOCALE_LCNAME_INDEX
{
    USHORT Name;
    USHORT Index;
    UINT Lcid;
} RTL_NLS_LOCALE_LCNAME_INDEX;

typedef struct _RTL_NLS_LOCALE_DATA
{
    UINT SName;
    UCHAR Reserved1[0x06e - 0x004];
    USHORT IDefaultAnsiCodePage;
    UCHAR Reserved2[0x114 - 0x070];
    UINT SConsoleFallbackName;
} RTL_NLS_LOCALE_DATA;

C_ASSERT(FIELD_OFFSET(RTL_NLS_LOCALE_HEADER, LocalesOffset) == 0x24);
C_ASSERT(FIELD_OFFSET(RTL_NLS_LOCALE_HEADER, LcnamesOffset) == 0x30);
C_ASSERT(FIELD_OFFSET(RTL_NLS_LOCALE_HEADER, StringsOffset) == 0x40);
C_ASSERT(FIELD_OFFSET(RTL_NLS_LOCALE_DATA, IDefaultAnsiCodePage) == 0x06e);
C_ASSERT(FIELD_OFFSET(RTL_NLS_LOCALE_DATA, SConsoleFallbackName) == 0x114);

#define RTL_MUI_MAX_PROCESS_LANGUAGES 5

static PWSTR RtlpProcessPreferredLanguages;
static ULONG RtlpProcessPreferredLanguageCharacters;

static NTSTATUS
RtlpOpenUserLanguageKey(PHANDLE KeyHandle)
{
    static const UNICODE_STRING LanguageKeyName = RTL_CONSTANT_STRING(L"Control Panel\\International\\User Profile");
    OBJECT_ATTRIBUTES ObjectAttributes;
    HANDLE CurrentUser;
    NTSTATUS Status;

    Status = RtlOpenCurrentUser(KEY_QUERY_VALUE, &CurrentUser);
    if (!NT_SUCCESS(Status)) return Status;

    InitializeObjectAttributes(&ObjectAttributes, (PUNICODE_STRING)&LanguageKeyName, OBJ_CASE_INSENSITIVE, CurrentUser, NULL);
    Status = NtOpenKey(KeyHandle, KEY_QUERY_VALUE, &ObjectAttributes);
    NtClose(CurrentUser);
    return Status;
}

static VOID
RtlpFreeRegistryLanguages(PRTL_MUI_REGISTRY_VALUE Value)
{
    if (Value->Information) RtlFreeHeap(RtlGetProcessHeap(), 0, Value->Information);
    RtlZeroMemory(Value, sizeof(*Value));
}

static NTSTATUS
RtlpReadRegistryLanguages(PRTL_MUI_REGISTRY_VALUE Value)
{
    static const UNICODE_STRING LanguageValueName = RTL_CONSTANT_STRING(L"Languages");
    PKEY_VALUE_PARTIAL_INFORMATION Information;
    HANDLE KeyHandle;
    ULONG ReturnLength;
    NTSTATUS Status;

    RtlZeroMemory(Value, sizeof(*Value));
    Status = RtlpOpenUserLanguageKey(&KeyHandle);
    if (!NT_SUCCESS(Status)) return Status;

    ReturnLength = 0;
    Status = NtQueryValueKey(KeyHandle, (PUNICODE_STRING)&LanguageValueName, KeyValuePartialInformation, NULL, 0, &ReturnLength);
    if (Status != STATUS_BUFFER_TOO_SMALL && Status != STATUS_BUFFER_OVERFLOW)
    {
        NtClose(KeyHandle);
        return Status;
    }

    Information = RtlAllocateHeap(RtlGetProcessHeap(), 0, ReturnLength);
    if (!Information)
    {
        NtClose(KeyHandle);
        return STATUS_NO_MEMORY;
    }

    Status = NtQueryValueKey(KeyHandle, (PUNICODE_STRING)&LanguageValueName, KeyValuePartialInformation, Information, ReturnLength, &ReturnLength);
    NtClose(KeyHandle);
    if (!NT_SUCCESS(Status))
    {
        RtlFreeHeap(RtlGetProcessHeap(), 0, Information);
        return Status;
    }

    if (Information->Type != REG_MULTI_SZ || Information->DataLength < 2 * sizeof(WCHAR) || Information->DataLength % sizeof(WCHAR))
    {
        RtlFreeHeap(RtlGetProcessHeap(), 0, Information);
        return STATUS_OBJECT_TYPE_MISMATCH;
    }

    Value->Information = Information;
    Value->Languages = (PCWSTR)Information->Data;
    Value->CharacterCount = Information->DataLength / sizeof(WCHAR);
    if (Value->Languages[Value->CharacterCount - 1] || Value->Languages[Value->CharacterCount - 2])
    {
        RtlpFreeRegistryLanguages(Value);
        return STATUS_REGISTRY_CORRUPT;
    }

    return STATUS_SUCCESS;
}

static NTSTATUS
RtlpFormatLanguageId(PCWSTR LanguageName, PWSTR Buffer, PULONG Length)
{
    static const WCHAR HexDigits[] = L"0123456789ABCDEF";
    LCID Locale;
    LANGID LanguageId;
    NTSTATUS Status;

    Status = RtlLocaleNameToLcid(LanguageName, &Locale, RTL_LOCALE_ALLOW_NEUTRAL_NAMES);
    if (!NT_SUCCESS(Status)) return Status;

    LanguageId = LANGIDFROMLCID(Locale);
    Buffer[0] = HexDigits[(LanguageId >> 12) & 0xf];
    Buffer[1] = HexDigits[(LanguageId >> 8) & 0xf];
    Buffer[2] = HexDigits[(LanguageId >> 4) & 0xf];
    Buffer[3] = HexDigits[LanguageId & 0xf];
    Buffer[4] = UNICODE_NULL;
    *Length = 4;
    return STATUS_SUCCESS;
}

static NTSTATUS
RtlpFormatLanguageName(PCWSTR LanguageName, PWSTR Buffer, PULONG Length)
{
    UNICODE_STRING LocaleName;
    LCID Locale;
    NTSTATUS Status;

    Status = RtlLocaleNameToLcid(LanguageName, &Locale, RTL_LOCALE_ALLOW_NEUTRAL_NAMES);
    if (!NT_SUCCESS(Status)) return Status;

    LocaleName.Buffer = Buffer;
    LocaleName.Length = 0;
    LocaleName.MaximumLength = LOCALE_NAME_MAX_LENGTH * sizeof(WCHAR);
    Status = RtlLcidToLocaleName(Locale, &LocaleName, RTL_LOCALE_ALLOW_NEUTRAL_NAMES, FALSE);
    if (!NT_SUCCESS(Status)) return Status;

    *Length = LocaleName.Length / sizeof(WCHAR);
    Buffer[*Length] = UNICODE_NULL;
    return STATUS_SUCCESS;
}

static NTSTATUS
RtlpWritePreferredLanguages(DWORD Flags, PCWSTR Languages, ULONG LanguageCharacters, PULONG Count, PWSTR Buffer, PULONG Size)
{
    WCHAR Converted[LOCALE_NAME_MAX_LENGTH];
    PCWSTR Current;
    ULONG Available;
    ULONG ConvertedLength;
    ULONG LanguageCount = 0;
    ULONG RequiredSize = 1;
    NTSTATUS Status;

    Current = Languages;
    Available = LanguageCharacters;
    while (Available && *Current)
    {
        ULONG InputLength = 0;

        while (InputLength < Available && Current[InputLength]) ++InputLength;
        if (InputLength == Available) return STATUS_REGISTRY_CORRUPT;

        if (Flags & MUI_LANGUAGE_ID) Status = RtlpFormatLanguageId(Current, Converted, &ConvertedLength);
        else Status = RtlpFormatLanguageName(Current, Converted, &ConvertedLength);
        if (NT_SUCCESS(Status))
        {
            ++LanguageCount;
            RequiredSize += ConvertedLength + 1;
        }

        Current += InputLength + 1;
        Available -= InputLength + 1;
    }

    if (!LanguageCount) return STATUS_NOT_FOUND;
    if (Buffer && *Size) Buffer[0] = UNICODE_NULL;
    if (Buffer && *Size < RequiredSize)
    {
        *Size = RequiredSize;
        return STATUS_BUFFER_TOO_SMALL;
    }

    if (Buffer)
    {
        PWSTR Output = Buffer;

        Current = Languages;
        Available = LanguageCharacters;
        while (Available && *Current)
        {
            ULONG InputLength = 0;

            while (InputLength < Available && Current[InputLength]) ++InputLength;
            if (Flags & MUI_LANGUAGE_ID) Status = RtlpFormatLanguageId(Current, Converted, &ConvertedLength);
            else Status = RtlpFormatLanguageName(Current, Converted, &ConvertedLength);
            if (NT_SUCCESS(Status))
            {
                RtlCopyMemory(Output, Converted, (ConvertedLength + 1) * sizeof(WCHAR));
                Output += ConvertedLength + 1;
            }
            Current += InputLength + 1;
            Available -= InputLength + 1;
        }
        *Output = UNICODE_NULL;
    }

    *Count = LanguageCount;
    *Size = RequiredSize;
    return STATUS_SUCCESS;
}

static NTSTATUS
RtlpGetPreferredLanguageById(DWORD Flags, LANGID LanguageId, PULONG Count, PWSTR Buffer, PULONG Size)
{
    WCHAR Languages[LOCALE_NAME_MAX_LENGTH + 1];
    UNICODE_STRING LocaleName;
    NTSTATUS Status;

    LocaleName.Buffer = Languages;
    LocaleName.Length = 0;
    LocaleName.MaximumLength = LOCALE_NAME_MAX_LENGTH * sizeof(WCHAR);
    Status = RtlLcidToLocaleName(LanguageId, &LocaleName, RTL_LOCALE_ALLOW_NEUTRAL_NAMES, FALSE);
    if (!NT_SUCCESS(Status)) return Status;

    Languages[LocaleName.Length / sizeof(WCHAR)] = UNICODE_NULL;
    Languages[LocaleName.Length / sizeof(WCHAR) + 1] = UNICODE_NULL;
    return RtlpWritePreferredLanguages(Flags, Languages, LocaleName.Length / sizeof(WCHAR) + 2, Count, Buffer, Size);
}

static NTSTATUS
RtlpGetDefaultPreferredLanguage(DWORD Flags, PULONG Count, PWSTR Buffer, PULONG Size)
{
    LANGID LanguageId;
    NTSTATUS Status;

    Status = NtQueryDefaultUILanguage(&LanguageId);
    if (!NT_SUCCESS(Status)) return Status;

    return RtlpGetPreferredLanguageById(Flags, LanguageId, Count, Buffer, Size);
}

static INT
RtlpHexDigitValue(WCHAR Character)
{
    if (Character >= L'0' && Character <= L'9') return Character - L'0';
    if (Character >= L'A' && Character <= L'F') return Character - L'A' + 10;
    if (Character >= L'a' && Character <= L'f') return Character - L'a' + 10;
    return -1;
}

static BOOLEAN
RtlpIsDefaultLanguageId(ULONG Value)
{
    return Value == LANGIDFROMLCID(LOCALE_SYSTEM_DEFAULT) ||
           Value == LANGIDFROMLCID(LOCALE_USER_DEFAULT) ||
           Value == LANGIDFROMLCID(LOCALE_CUSTOM_DEFAULT) ||
           Value == LANGIDFROMLCID(LOCALE_CUSTOM_UI_DEFAULT) ||
           Value == LANGIDFROMLCID(LOCALE_CUSTOM_UNSPECIFIED);
}

static NTSTATUS
RtlpCanonicalizePreferredLanguage(DWORD Flags, BOOLEAN RejectDefaultIds, PCWSTR Input, PWSTR Output, PULONG Length)
{
    UNICODE_STRING LocaleName;
    LCID Locale;
    NTSTATUS Status;

    if (Flags & MUI_LANGUAGE_ID)
    {
        ULONG Value = 0;
        ULONG Index;

        if (wcslen(Input) != 4) return STATUS_INVALID_PARAMETER;
        for (Index = 0; Index < 4; ++Index)
        {
            INT Digit = RtlpHexDigitValue(Input[Index]);

            if (Digit < 0) return STATUS_INVALID_PARAMETER;
            Value = (Value << 4) | Digit;
        }
        if (RejectDefaultIds && RtlpIsDefaultLanguageId(Value)) return STATUS_INVALID_PARAMETER;
        Locale = MAKELCID((LANGID)Value, SORT_DEFAULT);
    }
    else
    {
        Status = RtlLocaleNameToLcid(Input, &Locale, RTL_LOCALE_ALLOW_NEUTRAL_NAMES);
        if (!NT_SUCCESS(Status)) return Status;
    }

    LocaleName.Buffer = Output;
    LocaleName.Length = 0;
    LocaleName.MaximumLength = LOCALE_NAME_MAX_LENGTH * sizeof(WCHAR);
    Status = RtlLcidToLocaleName(Locale, &LocaleName, RTL_LOCALE_ALLOW_NEUTRAL_NAMES, FALSE);
    if (!NT_SUCCESS(Status)) return Status;

    *Length = LocaleName.Length / sizeof(WCHAR);
    Output[*Length] = UNICODE_NULL;
    return STATUS_SUCCESS;
}

static NTSTATUS
RtlpNormalizePreferredLanguages(DWORD Flags,
                                BOOLEAN RejectDefaultIds,
                                PCZZWSTR Input,
                                PWSTR *Languages,
                                PULONG CharacterCount,
                                PULONG LanguageCount)
{
    WCHAR Canonical[RTL_MUI_MAX_PROCESS_LANGUAGES][LOCALE_NAME_MAX_LENGTH];
    ULONG Lengths[RTL_MUI_MAX_PROCESS_LANGUAGES];
    ULONG ValidCount = 0;
    ULONG UniqueCount = 0;
    ULONG RequiredCharacters = 1;
    PCWSTR Current = Input;
    PWSTR Result;
    PWSTR Output;
    ULONG Index;

    while (*Current && ValidCount < RTL_MUI_MAX_PROCESS_LANGUAGES)
    {
        ULONG Length;
        ULONG InputLength = wcslen(Current);
        NTSTATUS Status;
        BOOLEAN Duplicate = FALSE;

        Status = RtlpCanonicalizePreferredLanguage(Flags,
                                                   RejectDefaultIds,
                                                   Current,
                                                   Canonical[UniqueCount],
                                                   &Length);
        Current += InputLength + 1;
        if (!NT_SUCCESS(Status)) continue;

        ++ValidCount;
        for (Index = 0; Index < UniqueCount; ++Index)
        {
            UNICODE_STRING Existing;
            UNICODE_STRING Candidate;

            Existing.Buffer = Canonical[Index];
            Existing.Length = Lengths[Index] * sizeof(WCHAR);
            Existing.MaximumLength = Existing.Length;
            Candidate.Buffer = Canonical[UniqueCount];
            Candidate.Length = Length * sizeof(WCHAR);
            Candidate.MaximumLength = Candidate.Length;
            if (RtlEqualUnicodeString(&Existing, &Candidate, TRUE))
            {
                Duplicate = TRUE;
                break;
            }
        }
        if (Duplicate) continue;

        Lengths[UniqueCount] = Length;
        RequiredCharacters += Length + 1;
        ++UniqueCount;
    }

    if (!UniqueCount)
    {
        *Languages = NULL;
        *CharacterCount = 0;
        *LanguageCount = 0;
        return STATUS_SUCCESS;
    }

    Result = RtlAllocateHeap(RtlGetProcessHeap(),
                             0,
                             RequiredCharacters * sizeof(WCHAR));
    if (!Result) return STATUS_NO_MEMORY;

    Output = Result;
    for (Index = 0; Index < UniqueCount; ++Index)
    {
        RtlCopyMemory(Output,
                      Canonical[Index],
                      (Lengths[Index] + 1) * sizeof(WCHAR));
        Output += Lengths[Index] + 1;
    }
    *Output = UNICODE_NULL;

    *Languages = Result;
    *CharacterCount = RequiredCharacters;
    *LanguageCount = UniqueCount;
    return STATUS_SUCCESS;
}

static NTSTATUS
RtlpWriteEmptyLanguageList(PWSTR Buffer, PULONG Size)
{
    if (Buffer && *Size < 2)
    {
        *Size = 2;
        return STATUS_BUFFER_TOO_SMALL;
    }

    if (Buffer)
    {
        Buffer[0] = UNICODE_NULL;
        Buffer[1] = UNICODE_NULL;
    }
    *Size = 2;
    return STATUS_SUCCESS;
}

static ULONG
RtlpMultiStringCharacters(PCZZWSTR List)
{
    PCWSTR Current = List;

    while (*Current) Current += wcslen(Current) + 1;
    return (ULONG)(Current - List) + 1;
}

NTSTATUS
NTAPI
RtlGetProcessPreferredUILanguages(DWORD Flags, PULONG Count, PWSTR Buffer, PULONG Size)
{
    NTSTATUS Status;

    if (!Count || !Size ||
        (Flags & ~(MUI_LANGUAGE_NAME | MUI_LANGUAGE_ID)) ||
        ((Flags & MUI_LANGUAGE_NAME) && (Flags & MUI_LANGUAGE_ID)) ||
        (*Size && !Buffer))
    {
        return STATUS_INVALID_PARAMETER;
    }

    RtlAcquirePebLock();
    if (RtlpProcessPreferredLanguages)
    {
        Status = RtlpWritePreferredLanguages(Flags,
                                             RtlpProcessPreferredLanguages,
                                             RtlpProcessPreferredLanguageCharacters,
                                             Count,
                                             Buffer,
                                             Size);
    }
    else
    {
        Status = RtlpWriteEmptyLanguageList(Buffer, Size);
    }
    RtlReleasePebLock();
    return Status;
}

NTSTATUS
NTAPI
RtlSetProcessPreferredUILanguages(DWORD Flags, PCZZWSTR Buffer, PULONG Count)
{
    PWSTR Languages = NULL;
    PWSTR OldLanguages;
    ULONG CharacterCount = 0;
    ULONG LanguageCount = 0;
    NTSTATUS Status;

    if ((Flags & ~(MUI_LANGUAGE_NAME | MUI_LANGUAGE_ID)) ||
        ((Flags & MUI_LANGUAGE_NAME) && (Flags & MUI_LANGUAGE_ID)))
    {
        return STATUS_INVALID_PARAMETER;
    }

    if (Buffer)
    {
        Status = RtlpNormalizePreferredLanguages(Flags,
                                                 FALSE,
                                                 Buffer,
                                                 &Languages,
                                                 &CharacterCount,
                                                 &LanguageCount);
        if (!NT_SUCCESS(Status)) return Status;
    }

    RtlAcquirePebLock();
    OldLanguages = RtlpProcessPreferredLanguages;
    RtlpProcessPreferredLanguages = Languages;
    RtlpProcessPreferredLanguageCharacters = CharacterCount;
    RtlReleasePebLock();

    if (OldLanguages) RtlFreeHeap(RtlGetProcessHeap(), 0, OldLanguages);
    if (Buffer && Count) *Count = LanguageCount;
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
RtlGetUserPreferredUILanguages(DWORD Flags, ULONG Reserved, PULONG Count, PWSTR Buffer, PULONG Size)
{
    RTL_MUI_REGISTRY_VALUE Value;
    NTSTATUS Status;

    UNREFERENCED_PARAMETER(Reserved);

    if (!Count || !Size || (Flags & ~(MUI_LANGUAGE_NAME | MUI_LANGUAGE_ID)) || ((Flags & MUI_LANGUAGE_NAME) && (Flags & MUI_LANGUAGE_ID)) || (*Size && !Buffer)) return STATUS_INVALID_PARAMETER;

    Status = RtlpReadRegistryLanguages(&Value);
    if (NT_SUCCESS(Status))
    {
        Status = RtlpWritePreferredLanguages(Flags, Value.Languages, Value.CharacterCount, Count, Buffer, Size);
        RtlpFreeRegistryLanguages(&Value);
        if (Status != STATUS_NOT_FOUND) return Status;
    }
    else if (Status != STATUS_OBJECT_NAME_NOT_FOUND && Status != STATUS_OBJECT_PATH_NOT_FOUND && Status != STATUS_OBJECT_TYPE_MISMATCH && Status != STATUS_REGISTRY_CORRUPT)
    {
        return Status;
    }

    return RtlpGetDefaultPreferredLanguage(Flags, Count, Buffer, Size);
}

NTSTATUS
NTAPI
RtlGetSystemPreferredUILanguages(DWORD Flags, ULONG Reserved, PULONG Count, PWSTR Buffer, PULONG Size)
{
    LANGID LanguageId;
    NTSTATUS Status;

    UNREFERENCED_PARAMETER(Reserved);

    if (!Count || !Size ||
        (Flags & ~(MUI_LANGUAGE_NAME | MUI_LANGUAGE_ID | MUI_MACHINE_LANGUAGE_SETTINGS)) ||
        ((Flags & MUI_LANGUAGE_NAME) && (Flags & MUI_LANGUAGE_ID)) ||
        (*Size && !Buffer))
    {
        return STATUS_INVALID_PARAMETER;
    }

    Status = NtQueryInstallUILanguage(&LanguageId);
    if (!NT_SUCCESS(Status)) return Status;

    return RtlpGetPreferredLanguageById(Flags, LanguageId, Count, Buffer, Size);
}

static VOID
RtlpAppendMergedLanguage(PWSTR Merged, PULONG Used, PCWSTR Language, SIZE_T Length)
{
    PCWSTR Current;

    if (!Length || Length >= LOCALE_NAME_MAX_LENGTH) return;

    for (Current = Merged; *Current; Current += wcslen(Current) + 1)
    {
        if (wcslen(Current) == Length && !_wcsnicmp(Current, Language, Length)) return;
    }

    RtlCopyMemory(Merged + *Used, Language, Length * sizeof(WCHAR));
    *Used += (ULONG)Length;
    Merged[(*Used)++] = UNICODE_NULL;
    Merged[*Used] = UNICODE_NULL;
}

static VOID
RtlpAppendMergedList(PWSTR Merged, PULONG Used, PCZZWSTR List, BOOLEAN AddNeutral)
{
    PCWSTR Current;

    for (Current = List; *Current; Current += wcslen(Current) + 1)
    {
        PCWSTR Dash = wcschr(Current, L'-');

        RtlpAppendMergedLanguage(Merged, Used, Current, wcslen(Current));
        if (AddNeutral && Dash) RtlpAppendMergedLanguage(Merged, Used, Current, Dash - Current);
    }
}

static WCHAR
RtlpUpcaseLocaleNameCharacter(WCHAR Character)
{
    if (Character >= L'a' && Character <= L'z') return Character - (L'a' - L'A');
    if (Character == L'_') return L'-';
    return Character;
}

static INT
RtlpCompareLocaleNames(PCWSTR Name1, PCWSTR Name2)
{
    for (;;)
    {
        WCHAR Character1 = RtlpUpcaseLocaleNameCharacter(*Name1++);
        WCHAR Character2 = RtlpUpcaseLocaleNameCharacter(*Name2++);

        if (!Character1 || Character1 != Character2) return Character1 - Character2;
    }
}

static const RTL_NLS_LOCALE_DATA *
RtlpFindLocaleData(PCWSTR Name, PCWSTR *Strings)
{
    const RTL_NLS_LOCALE_HEADER *Header;
    const RTL_NLS_LOCALE_LCNAME_INDEX *Index;
    PVOID Base;
    LCID DefaultLocale;
    LONG Low, High;

    if (!NT_SUCCESS(RtlGetLocaleFileMappingAddress(&Base, &DefaultLocale, NULL))) return NULL;

    Header = (const RTL_NLS_LOCALE_HEADER *)((const UCHAR *)Base + ((const UINT *)Base)[4]);
    Index = (const RTL_NLS_LOCALE_LCNAME_INDEX *)((const UCHAR *)Header + Header->LcnamesOffset);
    *Strings = (PCWSTR)((const UCHAR *)Header + Header->StringsOffset);

    Low = 0;
    High = (LONG)Header->LcnameCount - 1;
    while (Low <= High)
    {
        LONG Middle = (Low + High) / 2;
        INT Result = RtlpCompareLocaleNames(Name, *Strings + Index[Middle].Name + 1);

        if (Result < 0) High = Middle - 1;
        else if (Result > 0) Low = Middle + 1;
        else return (const RTL_NLS_LOCALE_DATA *)((const UCHAR *)Header + Header->LocalesOffset +
                                                  (ULONG)Index[Middle].Index * Header->LocaleSize);
    }
    return NULL;
}

static BOOLEAN
RtlpIsSameLanguage(PCWSTR Name1, PCWSTR Name2)
{
    for (;; Name1++, Name2++)
    {
        WCHAR Character1 = RtlpUpcaseLocaleNameCharacter(*Name1);
        WCHAR Character2 = RtlpUpcaseLocaleNameCharacter(*Name2);

        if (Character1 == L'-') Character1 = UNICODE_NULL;
        if (Character2 == L'-') Character2 = UNICODE_NULL;
        if (Character1 != Character2) return FALSE;
        if (!Character1) return TRUE;
    }
}

static BOOLEAN
RtlpIsConsoleLanguage(DWORD Filter, PCWSTR Language, PCWSTR *Fallback)
{
    const RTL_NLS_LOCALE_DATA *Locale;
    PCWSTR Strings;
    PCWSTR FallbackName;
    USHORT AnsiCodePage;
    USHORT OemCodePage;
    LCID Lcid;

    *Fallback = NULL;
    Locale = RtlpFindLocaleData(Language, &Strings);
    if (!Locale) return TRUE;

    FallbackName = Strings + Locale->SConsoleFallbackName + 1;
    if (!RtlpIsSameLanguage(Language, FallbackName))
    {
        *Fallback = FallbackName;
        return FALSE;
    }

    if (Filter != MUI_CONSOLE_FILTER) return TRUE;

    /* FIXME: the console code page is approximated by the system ANSI code page, and English counts as legible in every code page */
    RtlGetDefaultCodePage(&AnsiCodePage, &OemCodePage);
    if (Locale->IDefaultAnsiCodePage == AnsiCodePage) return TRUE;
    return NT_SUCCESS(RtlLocaleNameToLcid(Language, &Lcid, RTL_LOCALE_ALLOW_NEUTRAL_NAMES)) &&
           PRIMARYLANGID(LANGIDFROMLCID(Lcid)) == LANG_ENGLISH;
}

static NTSTATUS
RtlpFilterPreferredLanguages(DWORD Filter, PCZZWSTR List, PWSTR *Filtered, PULONG Characters)
{
    PCWSTR Current;
    PCWSTR Fallback;
    PWSTR Result;
    ULONG Capacity = 1;
    ULONG Used = 0;

    for (Current = List; *Current; Current += wcslen(Current) + 1)
        Capacity += (ULONG)wcslen(Current) + 1 + LOCALE_NAME_MAX_LENGTH + 1;

    Result = RtlAllocateHeap(RtlGetProcessHeap(), 0, Capacity * sizeof(WCHAR));
    if (!Result) return STATUS_NO_MEMORY;

    Result[0] = UNICODE_NULL;
    for (Current = List; *Current; Current += wcslen(Current) + 1)
    {
        if (RtlpIsConsoleLanguage(Filter, Current, &Fallback))
            RtlpAppendMergedLanguage(Result, &Used, Current, wcslen(Current));
    }
    for (Current = List; *Current; Current += wcslen(Current) + 1)
    {
        if (!RtlpIsConsoleLanguage(Filter, Current, &Fallback) && Fallback)
            RtlpAppendMergedLanguage(Result, &Used, Fallback, wcslen(Fallback));
    }

    *Filtered = Result;
    *Characters = Used + 1;
    return STATUS_SUCCESS;
}

static NTSTATUS
RtlpWriteFilteredLanguages(DWORD Flags, DWORD Filter, PCZZWSTR List, ULONG ListCharacters, PULONG Count, PWSTR Buffer, PULONG Size)
{
    PWSTR Filtered;
    ULONG Characters;
    NTSTATUS Status;

    if (!Filter) return RtlpWritePreferredLanguages(Flags, List, ListCharacters, Count, Buffer, Size);

    Status = RtlpFilterPreferredLanguages(Filter, List, &Filtered, &Characters);
    if (!NT_SUCCESS(Status)) return Status;

    if (Filtered[0]) Status = RtlpWritePreferredLanguages(Flags, Filtered, Characters, Count, Buffer, Size);
    else Status = RtlpWriteEmptyLanguageList(Buffer, Size);

    RtlFreeHeap(RtlGetProcessHeap(), 0, Filtered);
    return Status;
}

static PCZZWSTR
RtlpGetThreadLanguageList(PDWORD Filter)
{
    PRTL_MUI_THREAD_LANGUAGES Thread = NtCurrentTeb()->PreferredLanguages;

    *Filter = Thread ? Thread->Filter : 0;
    return (Thread && Thread->Languages[0]) ? Thread->Languages : NULL;
}

static NTSTATUS
RtlpStoreThreadLanguages(DWORD Filter, PCZZWSTR Languages)
{
    PTEB Teb = NtCurrentTeb();
    PRTL_MUI_THREAD_LANGUAGES OldThread = Teb->PreferredLanguages;
    PRTL_MUI_THREAD_LANGUAGES NewThread = NULL;
    ULONG Characters = (Languages && Languages[0]) ? RtlpMultiStringCharacters(Languages) : 1;

    if (Filter || Characters > 1)
    {
        NewThread = RtlAllocateHeap(RtlGetProcessHeap(), 0,
                                    FIELD_OFFSET(RTL_MUI_THREAD_LANGUAGES, Languages) + (Characters + 1) * sizeof(WCHAR));
        if (!NewThread) return STATUS_NO_MEMORY;

        NewThread->Filter = Filter;
        if (Characters > 1) RtlCopyMemory(NewThread->Languages, Languages, Characters * sizeof(WCHAR));
        else NewThread->Languages[0] = UNICODE_NULL;
        NewThread->Languages[Characters] = UNICODE_NULL;
    }

    Teb->PreferredLanguages = NewThread;
    if (OldThread) RtlFreeHeap(RtlGetProcessHeap(), 0, OldThread);
    return STATUS_SUCCESS;
}

static NTSTATUS
RtlpGetMergedPreferredLanguages(DWORD Flags, PULONG Count, PWSTR Buffer, PULONG Size)
{
    WCHAR Process[RTL_MUI_MAX_PROCESS_LANGUAGES * LOCALE_NAME_MAX_LENGTH + 1];
    WCHAR System[LOCALE_NAME_MAX_LENGTH + 1];
    DWORD Filter;
    PCWSTR Thread = RtlpGetThreadLanguageList(&Filter);
    BOOLEAN AddNeutral = (Flags & MUI_MERGE_SYSTEM_FALLBACK) != 0;
    UNICODE_STRING SystemName;
    PWSTR User = NULL;
    PWSTR Merged;
    ULONG ListCount;
    ULONG ProcessSize = RTL_NUMBER_OF(Process);
    ULONG UserSize = 0;
    ULONG Capacity;
    ULONG Used = 0;
    LANGID LanguageId;
    NTSTATUS Status;

    if (!NT_SUCCESS(RtlGetProcessPreferredUILanguages(MUI_LANGUAGE_NAME, &ListCount, Process, &ProcessSize)))
    {
        Process[0] = UNICODE_NULL;
        ProcessSize = 1;
    }

    if (NT_SUCCESS(RtlGetUserPreferredUILanguages(MUI_LANGUAGE_NAME, 0, &ListCount, NULL, &UserSize)) && UserSize)
    {
        User = RtlAllocateHeap(RtlGetProcessHeap(), 0, UserSize * sizeof(WCHAR));
        if (!User) return STATUS_NO_MEMORY;

        if (!NT_SUCCESS(RtlGetUserPreferredUILanguages(MUI_LANGUAGE_NAME, 0, &ListCount, User, &UserSize)))
        {
            RtlFreeHeap(RtlGetProcessHeap(), 0, User);
            User = NULL;
            UserSize = 0;
        }
    }

    System[0] = UNICODE_NULL;
    if (NT_SUCCESS(NtQueryInstallUILanguage(&LanguageId)))
    {
        SystemName.Buffer = System;
        SystemName.Length = 0;
        SystemName.MaximumLength = (LOCALE_NAME_MAX_LENGTH - 1) * sizeof(WCHAR);
        if (NT_SUCCESS(RtlLcidToLocaleName(MAKELCID(LanguageId, SORT_DEFAULT), &SystemName, RTL_LOCALE_ALLOW_NEUTRAL_NAMES, FALSE)))
            System[SystemName.Length / sizeof(WCHAR)] = UNICODE_NULL;
        else
            System[0] = UNICODE_NULL;
    }
    System[wcslen(System) + 1] = UNICODE_NULL;

    Capacity = 2 * ((Thread ? RtlpMultiStringCharacters(Thread) : 1) + ProcessSize + UserSize + RTL_NUMBER_OF(System)) + 1;
    Merged = RtlAllocateHeap(RtlGetProcessHeap(), 0, Capacity * sizeof(WCHAR));
    if (!Merged)
    {
        if (User) RtlFreeHeap(RtlGetProcessHeap(), 0, User);
        return STATUS_NO_MEMORY;
    }

    Merged[0] = UNICODE_NULL;
    if (Thread) RtlpAppendMergedList(Merged, &Used, Thread, AddNeutral);
    RtlpAppendMergedList(Merged, &Used, Process, AddNeutral);
    if (User) RtlpAppendMergedList(Merged, &Used, User, AddNeutral);
    RtlpAppendMergedList(Merged, &Used, System, AddNeutral);

    Status = RtlpWriteFilteredLanguages(Flags, Filter, Merged, Used + 1, Count, Buffer, Size);

    RtlFreeHeap(RtlGetProcessHeap(), 0, Merged);
    if (User) RtlFreeHeap(RtlGetProcessHeap(), 0, User);
    return Status;
}

NTSTATUS
NTAPI
RtlGetThreadPreferredUILanguages(DWORD Flags, PULONG Count, PWSTR Buffer, PULONG Size)
{
    PCWSTR Thread;
    DWORD Filter;

    if (!Count || !Size ||
        (Flags & ~(MUI_LANGUAGE_NAME | MUI_LANGUAGE_ID | MUI_MERGE_SYSTEM_FALLBACK | MUI_MERGE_USER_FALLBACK | MUI_THREAD_LANGUAGES)) ||
        ((Flags & MUI_LANGUAGE_NAME) && (Flags & MUI_LANGUAGE_ID)) ||
        ((Flags & MUI_THREAD_LANGUAGES) && (Flags & (MUI_MERGE_SYSTEM_FALLBACK | MUI_MERGE_USER_FALLBACK))) ||
        ((Flags & MUI_MERGE_SYSTEM_FALLBACK) && !(Flags & MUI_MERGE_USER_FALLBACK)) ||
        (*Size && !Buffer))
    {
        return STATUS_INVALID_PARAMETER;
    }

    if (!(Flags & MUI_THREAD_LANGUAGES))
        return RtlpGetMergedPreferredLanguages(Flags, Count, Buffer, Size);

    Thread = RtlpGetThreadLanguageList(&Filter);
    if (!Thread) return RtlpWriteEmptyLanguageList(Buffer, Size);

    return RtlpWriteFilteredLanguages(Flags, Filter, Thread, RtlpMultiStringCharacters(Thread), Count, Buffer, Size);
}

NTSTATUS
NTAPI
RtlSetThreadPreferredUILanguages(DWORD Flags, PCZZWSTR Buffer, PULONG Count)
{
    static const DWORD FilterFlags = MUI_CONSOLE_FILTER | MUI_COMPLEX_SCRIPT_FILTER | MUI_RESET_FILTERS;
    DWORD Filter = Flags & FilterFlags;
    DWORD CurrentFilter;
    PCZZWSTR Current = RtlpGetThreadLanguageList(&CurrentFilter);
    PWSTR Languages = NULL;
    ULONG CharacterCount = 0;
    ULONG LanguageCount = 0;
    NTSTATUS Status;

    if ((Flags & ~(MUI_LANGUAGE_NAME | MUI_LANGUAGE_ID | FilterFlags)) ||
        ((Flags & MUI_LANGUAGE_NAME) && (Flags & MUI_LANGUAGE_ID)) ||
        (Filter & (Filter - 1)))
    {
        return STATUS_INVALID_PARAMETER;
    }

    if (Filter)
    {
        if (Buffer || Count) return STATUS_INVALID_PARAMETER;
        return RtlpStoreThreadLanguages((Filter == MUI_RESET_FILTERS) ? 0 : Filter, Current);
    }

    if (Buffer)
    {
        Status = RtlpNormalizePreferredLanguages(Flags,
                                                 TRUE,
                                                 Buffer,
                                                 &Languages,
                                                 &CharacterCount,
                                                 &LanguageCount);
        if (!NT_SUCCESS(Status)) return Status;
    }

    Status = RtlpStoreThreadLanguages(CurrentFilter, Languages);
    if (Languages) RtlFreeHeap(RtlGetProcessHeap(), 0, Languages);
    if (!NT_SUCCESS(Status)) return Status;

    if (Buffer && Count) *Count = LanguageCount;
    return STATUS_SUCCESS;
}

VOID
NTAPI
RtlpFreeThreadPreferredUILanguages(VOID)
{
    PTEB Teb = NtCurrentTeb();
    PVOID Languages = Teb->PreferredLanguages;

    Teb->PreferredLanguages = NULL;
    if (Languages) RtlFreeHeap(RtlGetProcessHeap(), 0, Languages);
}


