/*
 * PROJECT:     LiberNT Runtime Library
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Query table processing of RtlQueryRegistryValues and RtlQueryRegistryValuesEx
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <rtl.h>
#include <ndk/cmfuncs.h>

#define NDEBUG
#include <debug.h>

#define TAG_RTL_QUERY 'qtRL'
#define RTLP_QUERY_BUFFER_SIZE 512
#define RTLP_QUERY_TERMINATOR (2 * sizeof(UNICODE_NULL))

/* The least significant byte of DefaultType is the type of the default; the most significant byte is the TYPECHECK type */
#define RTLP_DEFAULT_TYPE(Entry) ((Entry)->DefaultType & 0xFF)
#define RTLP_EXPECTED_TYPE(Entry) ((Entry)->DefaultType >> RTL_QUERY_REGISTRY_TYPECHECK_SHIFT)

typedef struct _RTLP_QUERY_STATE
{
    HANDLE BaseKey;
    BOOLEAN OwnsBaseKey;
    HANDLE CurrentKey;
    ACCESS_MASK Access;
    PKEY_VALUE_FULL_INFORMATION Value;
    ULONG ValueSize;
    PWSTR ValueName;
    ULONG ValueNameSize;
    PVOID Context;
    PVOID Environment;
} RTLP_QUERY_STATE, *PRTLP_QUERY_STATE;

static
BOOLEAN
RtlpQueryIsString(
    _In_ ULONG Type)
{
    return Type == REG_SZ || Type == REG_EXPAND_SZ || Type == REG_MULTI_SZ;
}

static
PCWSTR
RtlpQueryRoot(
    _In_ ULONG RelativeTo)
{
    switch (RelativeTo)
    {
        case RTL_REGISTRY_SERVICES:
            return L"\\Registry\\Machine\\System\\CurrentControlSet\\Services";
        case RTL_REGISTRY_CONTROL:
            return L"\\Registry\\Machine\\System\\CurrentControlSet\\Control";
        case RTL_REGISTRY_WINDOWS_NT:
            return L"\\Registry\\Machine\\Software\\Microsoft\\Windows NT\\CurrentVersion";
        case RTL_REGISTRY_DEVICEMAP:
            return L"\\Registry\\Machine\\Hardware\\DeviceMap";
        default:
            return NULL;
    }
}

static
NTSTATUS
RtlpQueryOpenRelative(
    _In_opt_ HANDLE Root,
    _In_ PCUNICODE_STRING Name,
    _In_ ACCESS_MASK Access,
    _Out_ PHANDLE Key)
{
    OBJECT_ATTRIBUTES Attributes;

    InitializeObjectAttributes(&Attributes,
                               (PUNICODE_STRING)Name,
                               OBJ_CASE_INSENSITIVE | (RtlpGetMode() == KernelMode ? OBJ_KERNEL_HANDLE : 0),
                               Root,
                               NULL);
    return ZwOpenKey(Key, Access, &Attributes);
}

static
NTSTATUS
RtlpQueryOpenBase(
    _In_ ULONG RelativeTo,
    _In_opt_ PCWSTR Path,
    _Inout_ PRTLP_QUERY_STATE State)
{
    UNICODE_STRING Root = { 0 }, Relative, Full;
    ULONG Base = RelativeTo & ~(RTL_REGISTRY_OPTIONAL | RTL_REGISTRY_HANDLE);
    BOOLEAN OwnsRoot = FALSE;
    PCWSTR Prefix;
    NTSTATUS Status;
    SIZE_T Length;

    if (RelativeTo & RTL_REGISTRY_HANDLE)
    {
        State->BaseKey = (HANDLE)Path;
        State->OwnsBaseKey = FALSE;
        return STATUS_SUCCESS;
    }

    if (Base >= RTL_REGISTRY_MAXIMUM)
        return STATUS_INVALID_PARAMETER;

    if (Base == RTL_REGISTRY_ABSOLUTE)
    {
        if (!Path)
            return STATUS_INVALID_PARAMETER;
        Status = RtlInitUnicodeStringEx(&Full, Path);
        if (!NT_SUCCESS(Status))
            return Status;
        State->OwnsBaseKey = TRUE;
        return RtlpQueryOpenRelative(NULL, &Full, State->Access, &State->BaseKey);
    }

    if (Base == RTL_REGISTRY_USER)
    {
        Status = RtlFormatCurrentUserKeyPath(&Root);
        if (!NT_SUCCESS(Status))
            return Status;
        OwnsRoot = TRUE;
    }
    else
    {
        Prefix = RtlpQueryRoot(Base);
        ASSERT(Prefix != NULL);
        RtlInitUnicodeString(&Root, Prefix);
    }

    RtlInitEmptyUnicodeString(&Relative, NULL, 0);
    if (Path && *Path)
    {
        Status = RtlInitUnicodeStringEx(&Relative, Path);
        if (!NT_SUCCESS(Status))
        {
            if (OwnsRoot)
                RtlFreeUnicodeString(&Root);
            return Status;
        }
    }

    Length = (SIZE_T)Root.Length + sizeof(WCHAR) + Relative.Length + sizeof(UNICODE_NULL);
    if (Length > UNICODE_STRING_MAX_BYTES)
    {
        if (OwnsRoot)
            RtlFreeUnicodeString(&Root);
        return STATUS_NAME_TOO_LONG;
    }

    Full.Buffer = RtlpAllocateMemory(Length, TAG_RTL_QUERY);
    if (!Full.Buffer)
    {
        if (OwnsRoot)
            RtlFreeUnicodeString(&Root);
        return STATUS_NO_MEMORY;
    }
    Full.Length = 0;
    Full.MaximumLength = (USHORT)Length;
    RtlAppendUnicodeStringToString(&Full, &Root);
    if (Relative.Length)
    {
        if (Relative.Buffer[0] != OBJ_NAME_PATH_SEPARATOR)
            RtlAppendUnicodeToString(&Full, L"\\");
        RtlAppendUnicodeStringToString(&Full, &Relative);
    }
    ASSERT(Full.Length <= Full.MaximumLength);

    State->OwnsBaseKey = TRUE;
    Status = RtlpQueryOpenRelative(NULL, &Full, State->Access, &State->BaseKey);

    RtlpFreeMemory(Full.Buffer, TAG_RTL_QUERY);
    if (OwnsRoot)
        RtlFreeUnicodeString(&Root);
    return Status;
}

static
VOID
RtlpQueryCloseCurrent(
    _Inout_ PRTLP_QUERY_STATE State)
{
    if (State->CurrentKey && State->CurrentKey != State->BaseKey)
        ZwClose(State->CurrentKey);
    State->CurrentKey = State->BaseKey;
}

/* Leaves the value in State->Value, its data followed by two NULs, and its NUL-terminated name in State->ValueName */
static
NTSTATUS
RtlpQueryFetch(
    _Inout_ PRTLP_QUERY_STATE State,
    _In_opt_ PCWSTR Name,
    _In_ ULONG Index)
{
    PKEY_VALUE_FULL_INFORMATION Grown;
    UNICODE_STRING ValueName;
    ULONG Needed, NameBytes, DataEnd;
    NTSTATUS Status;
    PWSTR GrownName;

    ASSERT(State->CurrentKey != NULL);
    ASSERT(State->ValueSize > RTLP_QUERY_TERMINATOR);

    if (Name)
    {
        Status = RtlInitUnicodeStringEx(&ValueName, Name);
        if (!NT_SUCCESS(Status))
            return Status;
    }

    for (;;)
    {
        Needed = 0;
        if (Name)
            Status = ZwQueryValueKey(State->CurrentKey, &ValueName, KeyValueFullInformation, State->Value,
                                     State->ValueSize - RTLP_QUERY_TERMINATOR, &Needed);
        else
            Status = ZwEnumerateValueKey(State->CurrentKey, Index, KeyValueFullInformation, State->Value,
                                         State->ValueSize - RTLP_QUERY_TERMINATOR, &Needed);
        if (Status != STATUS_BUFFER_OVERFLOW && Status != STATUS_BUFFER_TOO_SMALL)
            break;
        if (Needed <= State->ValueSize - RTLP_QUERY_TERMINATOR)
            Needed = State->ValueSize * 2;
        if (Needed > MAXULONG - RTLP_QUERY_TERMINATOR)
            return STATUS_INSUFFICIENT_RESOURCES;
        Needed += RTLP_QUERY_TERMINATOR;

        Grown = RtlpAllocateMemory(Needed, TAG_RTL_QUERY);
        if (!Grown)
            return STATUS_NO_MEMORY;
        RtlpFreeMemory(State->Value, TAG_RTL_QUERY);
        State->Value = Grown;
        State->ValueSize = Needed;
    }

    if (!NT_SUCCESS(Status))
        return Status;

    ASSERT((State->Value->NameLength % sizeof(WCHAR)) == 0);
    if (State->Value->DataLength)
    {
        DataEnd = State->Value->DataOffset + State->Value->DataLength;
        ASSERT(DataEnd >= State->Value->DataOffset);
        ASSERT(DataEnd <= State->ValueSize - RTLP_QUERY_TERMINATOR);
        RtlZeroMemory((PUCHAR)State->Value + DataEnd, RTLP_QUERY_TERMINATOR);
    }

    NameBytes = State->Value->NameLength + sizeof(UNICODE_NULL);
    if (NameBytes > State->ValueNameSize)
    {
        GrownName = RtlpAllocateMemory(NameBytes, TAG_RTL_QUERY);
        if (!GrownName)
            return STATUS_NO_MEMORY;
        if (State->ValueName)
            RtlpFreeMemory(State->ValueName, TAG_RTL_QUERY);
        State->ValueName = GrownName;
        State->ValueNameSize = NameBytes;
    }
    RtlCopyMemory(State->ValueName, State->Value->Name, State->Value->NameLength);
    State->ValueName[State->Value->NameLength / sizeof(WCHAR)] = UNICODE_NULL;
    return STATUS_SUCCESS;
}

static
NTSTATUS
RtlpQueryCall(
    _In_ PRTL_QUERY_REGISTRY_TABLE Entry,
    _In_opt_ PCWSTR ValueName,
    _In_ ULONG Type,
    _In_opt_ PVOID Data,
    _In_ ULONG Length,
    _In_ PRTLP_QUERY_STATE State)
{
    NTSTATUS Status;

    ASSERT(Entry->QueryRoutine != NULL);

    Status = Entry->QueryRoutine((PWSTR)ValueName, Type, Data, Length, State->Context, Entry->EntryContext);
    return Status == STATUS_BUFFER_TOO_SMALL ? STATUS_SUCCESS : Status;
}

static
NTSTATUS
RtlpQueryExpand(
    _In_ PRTLP_QUERY_STATE State,
    _In_reads_bytes_(Length) PWSTR Data,
    _In_ ULONG Length,
    _Out_ PWSTR *Expanded,
    _Out_ PULONG ExpandedLength)
{
    UNICODE_STRING Source, Destination;
    ULONG Needed = 0;
    NTSTATUS Status;

    *Expanded = NULL;
    *ExpandedLength = 0;

    Source.Buffer = Data;
    Source.Length = (USHORT)min(Length, UNICODE_STRING_MAX_BYTES);
    while (Source.Length >= sizeof(WCHAR) && Data[Source.Length / sizeof(WCHAR) - 1] == UNICODE_NULL)
        Source.Length -= sizeof(WCHAR);
    Source.MaximumLength = Source.Length;

    RtlInitEmptyUnicodeString(&Destination, NULL, 0);
    Status = RtlExpandEnvironmentStrings_U(State->Environment, &Source, &Destination, &Needed);
    if (Status != STATUS_BUFFER_TOO_SMALL)
        return NT_SUCCESS(Status) ? STATUS_UNSUCCESSFUL : Status;
    if (Needed > UNICODE_STRING_MAX_BYTES)
        return STATUS_BUFFER_OVERFLOW;

    Destination.Buffer = RtlpAllocateMemory(Needed, TAG_RTL_QUERY);
    if (!Destination.Buffer)
        return STATUS_NO_MEMORY;
    Destination.MaximumLength = (USHORT)Needed;
    Status = RtlExpandEnvironmentStrings_U(State->Environment, &Source, &Destination, &Needed);
    if (!NT_SUCCESS(Status))
    {
        RtlpFreeMemory(Destination.Buffer, TAG_RTL_QUERY);
        return Status;
    }

    ASSERT(Destination.Length < Destination.MaximumLength);
    Destination.Buffer[Destination.Length / sizeof(WCHAR)] = UNICODE_NULL;
    *Expanded = Destination.Buffer;
    *ExpandedLength = Destination.Length + sizeof(UNICODE_NULL);
    return STATUS_SUCCESS;
}

static
NTSTATUS
RtlpQueryStoreString(
    _Inout_ PUNICODE_STRING String,
    _In_reads_bytes_opt_(Bytes) PVOID Data,
    _In_ ULONG Bytes)
{
    ULONG Size = max(Bytes, (ULONG)sizeof(UNICODE_NULL));

    ASSERT(Data != NULL || Bytes == 0);

    if (Size > UNICODE_STRING_MAX_BYTES)
        return STATUS_BUFFER_TOO_SMALL;

    if (!String->Buffer)
    {
        String->Buffer = RtlpAllocateStringMemory(Size, TAG_USTR);
        if (!String->Buffer)
            return STATUS_NO_MEMORY;
        String->MaximumLength = (USHORT)Size;
        String->Buffer[0] = UNICODE_NULL;
    }
    else if (Bytes > String->MaximumLength)
    {
        return STATUS_BUFFER_TOO_SMALL;
    }

    if (Bytes)
        RtlCopyMemory(String->Buffer, Data, Bytes);
    String->Length = (USHORT)((Size - sizeof(UNICODE_NULL)) & ~(ULONG)1);

    ASSERT(String->Length < String->MaximumLength);
    return STATUS_SUCCESS;
}

static
NTSTATUS
RtlpQueryStoreDirect(
    _In_ PRTL_QUERY_REGISTRY_TABLE Entry,
    _In_ ULONG Type,
    _In_reads_bytes_opt_(Length) PVOID Data,
    _In_ ULONG Length,
    _In_ PRTLP_QUERY_STATE State)
{
    PWSTR Expanded;
    ULONG ExpandedLength;
    LONG Size;
    NTSTATUS Status;

    ASSERT(Entry->EntryContext != NULL);

    if (RtlpQueryIsString(Type))
    {
        if (Type == REG_EXPAND_SZ && !(Entry->Flags & RTL_QUERY_REGISTRY_NOEXPAND) && Data)
        {
            Status = RtlpQueryExpand(State, Data, Length, &Expanded, &ExpandedLength);
            if (!NT_SUCCESS(Status))
                return Status;
            Status = RtlpQueryStoreString(Entry->EntryContext, Expanded, ExpandedLength);
            RtlpFreeMemory(Expanded, TAG_RTL_QUERY);
            return Status;
        }

        return RtlpQueryStoreString(Entry->EntryContext, Data, Data ? Length : 0);
    }

    if (Length <= sizeof(ULONG))
    {
        if (Length)
            RtlCopyMemory(Entry->EntryContext, Data, Length);
        return STATUS_SUCCESS;
    }

    Size = *(PLONG)Entry->EntryContext;
    if (Size < 0)
    {
        if ((ULONG)-Size < Length)
            return STATUS_BUFFER_TOO_SMALL;
        RtlCopyMemory(Entry->EntryContext, Data, Length);
        return STATUS_SUCCESS;
    }

    if ((ULONG)Size < Length || (ULONG)Size - Length < 2 * sizeof(ULONG))
        return STATUS_BUFFER_TOO_SMALL;
    ((PULONG)Entry->EntryContext)[0] = Length;
    ((PULONG)Entry->EntryContext)[1] = Type;
    RtlCopyMemory((PULONG)Entry->EntryContext + 2, Data, Length);
    return STATUS_SUCCESS;
}

static
NTSTATUS
RtlpQuerySplit(
    _In_ PRTL_QUERY_REGISTRY_TABLE Entry,
    _In_opt_ PCWSTR ValueName,
    _In_reads_bytes_(Length) PWSTR String,
    _In_ ULONG Length,
    _In_ PRTLP_QUERY_STATE State)
{
    ULONG Remaining = Length & ~(ULONG)1, Bytes;
    SIZE_T Chars;
    NTSTATUS Status;

    /* Windows 11 stops once two WCHARs or fewer remain, and passes empty strings before that */
    while (Remaining > 2 * sizeof(UNICODE_NULL))
    {
        for (Chars = 0; (Chars + 1) * sizeof(WCHAR) <= Remaining && String[Chars]; Chars++);

        /* TODO: an unterminated last string is passed without its NUL; Windows 11 is not measured for it */
        Bytes = (ULONG)min((Chars + 1) * sizeof(WCHAR), (SIZE_T)Remaining);
        ASSERT(Bytes <= Remaining);

        Status = RtlpQueryCall(Entry, ValueName, REG_SZ, String, Bytes, State);
        if (!NT_SUCCESS(Status))
            return Status;
        String += Chars + 1;
        Remaining -= Bytes;
    }
    return STATUS_SUCCESS;
}

static
NTSTATUS
RtlpQueryDeliver(
    _In_ PRTL_QUERY_REGISTRY_TABLE Entry,
    _In_opt_ PCWSTR ValueName,
    _In_ ULONG Type,
    _In_reads_bytes_opt_(Length) PVOID Data,
    _In_ ULONG Length,
    _In_ PRTLP_QUERY_STATE State)
{
    PWSTR Expanded;
    ULONG ExpandedLength;
    NTSTATUS Status;

    if (Entry->Flags & RTL_QUERY_REGISTRY_DIRECT)
    {
        Status = RtlpQueryStoreDirect(Entry, Type, Data, Length, State);
        return Status == STATUS_BUFFER_TOO_SMALL ? STATUS_SUCCESS : Status;
    }

    if (!(Entry->Flags & RTL_QUERY_REGISTRY_NOEXPAND) && Data)
    {
        if (Type == REG_MULTI_SZ)
            return RtlpQuerySplit(Entry, ValueName, Data, Length, State);

        if (Type == REG_EXPAND_SZ)
        {
            Status = RtlpQueryExpand(State, Data, Length, &Expanded, &ExpandedLength);
            if (!NT_SUCCESS(Status))
                return Status;
            Status = RtlpQueryCall(Entry, ValueName, REG_SZ, Expanded, ExpandedLength, State);
            RtlpFreeMemory(Expanded, TAG_RTL_QUERY);
            return Status;
        }
    }

    return RtlpQueryCall(Entry, ValueName, Type, Data, Length, State);
}

static
NTSTATUS
RtlpQueryDefault(
    _In_ PRTL_QUERY_REGISTRY_TABLE Entry,
    _In_ PRTLP_QUERY_STATE State)
{
    ULONG Type = RTLP_DEFAULT_TYPE(Entry), Length = Entry->DefaultLength;
    PWSTR String;

    if (Type == REG_NONE)
        return (Entry->Flags & RTL_QUERY_REGISTRY_REQUIRED) ? STATUS_OBJECT_NAME_NOT_FOUND : STATUS_SUCCESS;

    if (Length == 0 && RtlpQueryIsString(Type))
    {
        if (!Entry->DefaultData)
            return STATUS_DATA_OVERRUN;

        String = Entry->DefaultData;
        if (Type == REG_MULTI_SZ)
        {
            while (*String)
                String += wcslen(String) + 1;
            Length = (ULONG)((PUCHAR)(String + 1) - (PUCHAR)Entry->DefaultData);
        }
        else
        {
            Length = (ULONG)((wcslen(String) + 1) * sizeof(WCHAR));
        }
    }

    return RtlpQueryDeliver(Entry, Entry->Name, Type, Entry->DefaultData, Length, State);
}

static
NTSTATUS
RtlpQueryFound(
    _In_ PRTL_QUERY_REGISTRY_TABLE Entry,
    _In_ PRTLP_QUERY_STATE State)
{
    PKEY_VALUE_FULL_INFORMATION Value = State->Value;
    ULONG Length = Value->DataLength;

    ASSERT(Value->DataLength != 0);

    if ((Entry->Flags & (RTL_QUERY_REGISTRY_DIRECT | RTL_QUERY_REGISTRY_TYPECHECK)) ==
        (RTL_QUERY_REGISTRY_DIRECT | RTL_QUERY_REGISTRY_TYPECHECK) &&
        RTLP_EXPECTED_TYPE(Entry) != Value->Type)
    {
        return STATUS_OBJECT_TYPE_MISMATCH;
    }

    /* Windows 11 reads a REG_MULTI_SZ with one more NUL than its stored data */
    if (Value->Type == REG_MULTI_SZ)
        Length += sizeof(UNICODE_NULL);

    return RtlpQueryDeliver(Entry, State->ValueName, Value->Type, (PUCHAR)Value + Value->DataOffset, Length, State);
}

static
NTSTATUS
RtlpQueryNamedValue(
    _In_ PRTL_QUERY_REGISTRY_TABLE Entry,
    _Inout_ PRTLP_QUERY_STATE State)
{
    UNICODE_STRING Name;
    NTSTATUS Status;

    Status = RtlpQueryFetch(State, Entry->Name, 0);
    if (Status == STATUS_OBJECT_NAME_NOT_FOUND || (NT_SUCCESS(Status) && State->Value->DataLength == 0))
        return RtlpQueryDefault(Entry, State);
    if (!NT_SUCCESS(Status))
        return Status;

    Status = RtlpQueryFound(Entry, State);
    if (NT_SUCCESS(Status) && (Entry->Flags & RTL_QUERY_REGISTRY_DELETE))
    {
        RtlInitUnicodeString(&Name, Entry->Name);
        Status = ZwDeleteValueKey(State->CurrentKey, &Name);
    }
    return Status;
}

static
NTSTATUS
RtlpQueryAllValues(
    _In_ PRTL_QUERY_REGISTRY_TABLE Entry,
    _Inout_ PRTLP_QUERY_STATE State)
{
    UNICODE_STRING Name;
    ULONG Index = 0, Count = 0;
    NTSTATUS Status;

    if (!Entry->QueryRoutine)
        return STATUS_INVALID_PARAMETER;

    for (;;)
    {
        Status = RtlpQueryFetch(State, NULL, Index);
        if (Status == STATUS_NO_MORE_ENTRIES)
            break;
        if (!NT_SUCCESS(Status))
            return Status;
        if (State->Value->DataLength == 0)
        {
            Index++;
            continue;
        }

        Status = RtlpQueryFound(Entry, State);
        if (!NT_SUCCESS(Status))
            return Status;
        Count++;

        if (Entry->Flags & RTL_QUERY_REGISTRY_DELETE)
        {
            RtlInitUnicodeString(&Name, State->ValueName);
            Status = ZwDeleteValueKey(State->CurrentKey, &Name);
            if (!NT_SUCCESS(Status))
                return Status;
        }
        else
        {
            Index++;
        }
    }

    if (Count == 0 && (Entry->Flags & RTL_QUERY_REGISTRY_REQUIRED))
        return STATUS_OBJECT_NAME_NOT_FOUND;
    return STATUS_SUCCESS;
}

static
NTSTATUS
RtlpQuerySubkey(
    _In_ PRTL_QUERY_REGISTRY_TABLE Entry,
    _Inout_ PRTLP_QUERY_STATE State)
{
    UNICODE_STRING Name;
    HANDLE Key;
    NTSTATUS Status;

    if (!Entry->Name)
        return STATUS_INVALID_PARAMETER;

    RtlpQueryCloseCurrent(State);
    Status = RtlInitUnicodeStringEx(&Name, Entry->Name);
    if (!NT_SUCCESS(Status))
        return Status;

    Status = RtlpQueryOpenRelative(State->BaseKey, &Name, State->Access, &Key);
    if (!NT_SUCCESS(Status))
        return Status;
    State->CurrentKey = Key;

    return Entry->QueryRoutine ? RtlpQueryAllValues(Entry, State) : STATUS_SUCCESS;
}

static
NTSTATUS
RtlpQueryEntry(
    _In_ PRTL_QUERY_REGISTRY_TABLE Entry,
    _Inout_ PRTLP_QUERY_STATE State)
{
    ASSERT(Entry->QueryRoutine != NULL || Entry->Name != NULL);

    if (Entry->Flags & RTL_QUERY_REGISTRY_TOPKEY)
        RtlpQueryCloseCurrent(State);

    if (Entry->Flags & RTL_QUERY_REGISTRY_SUBKEY)
        return RtlpQuerySubkey(Entry, State);

    if (Entry->Flags & RTL_QUERY_REGISTRY_DIRECT)
    {
        if (Entry->QueryRoutine || !Entry->Name || !Entry->EntryContext)
            return STATUS_INVALID_PARAMETER;
        return RtlpQueryNamedValue(Entry, State);
    }

    if (!Entry->QueryRoutine)
        return STATUS_SUCCESS;

    if (Entry->Name)
        return RtlpQueryNamedValue(Entry, State);

    if (Entry->Flags & RTL_QUERY_REGISTRY_NOVALUE)
        return RtlpQueryCall(Entry, NULL, REG_NONE, NULL, 0, State);

    return RtlpQueryAllValues(Entry, State);
}

static
NTSTATUS
RtlpQueryRegistryTable(
    _In_ ULONG RelativeTo,
    _In_opt_ PCWSTR Path,
    _Inout_ PRTL_QUERY_REGISTRY_TABLE QueryTable,
    _In_opt_ PVOID Context,
    _In_opt_ PVOID Environment)
{
    PRTL_QUERY_REGISTRY_TABLE Entry;
    RTLP_QUERY_STATE State;
    NTSTATUS Status;

    if (!QueryTable)
        return STATUS_INVALID_PARAMETER;

    RtlZeroMemory(&State, sizeof(State));
    State.Context = Context;
    State.Environment = Environment;
    State.Access = KEY_READ;
    for (Entry = QueryTable; Entry->QueryRoutine || Entry->Name; Entry++)
    {
        if (Entry->Flags & RTL_QUERY_REGISTRY_DELETE)
            State.Access |= KEY_SET_VALUE;
    }

    Status = RtlpQueryOpenBase(RelativeTo, Path, &State);
    if (!NT_SUCCESS(Status))
        return Status;
    State.CurrentKey = State.BaseKey;

    State.Value = RtlpAllocateMemory(RTLP_QUERY_BUFFER_SIZE, TAG_RTL_QUERY);
    State.ValueSize = State.Value ? RTLP_QUERY_BUFFER_SIZE : 0;
    Status = State.Value ? STATUS_SUCCESS : STATUS_NO_MEMORY;

    /* TODO: a DIRECT entry without TYPECHECK on a hive outside the system hives is not refused */
    for (Entry = QueryTable; NT_SUCCESS(Status) && (Entry->QueryRoutine || Entry->Name); Entry++)
    {
        Status = RtlpQueryEntry(Entry, &State);
    }

    RtlpQueryCloseCurrent(&State);
    if (State.OwnsBaseKey)
        ZwClose(State.BaseKey);
    if (State.Value)
        RtlpFreeMemory(State.Value, TAG_RTL_QUERY);
    if (State.ValueName)
        RtlpFreeMemory(State.ValueName, TAG_RTL_QUERY);
    return Status;
}

NTSTATUS
NTAPI
RtlQueryRegistryValues(
    _In_ ULONG RelativeTo,
    _In_ PCWSTR Path,
    _Inout_ PRTL_QUERY_REGISTRY_TABLE QueryTable,
    _In_opt_ PVOID Context,
    _In_opt_ PVOID Environment)
{
    return RtlpQueryRegistryTable(RelativeTo, Path, QueryTable, Context, Environment);
}

NTSTATUS
NTAPI
RtlQueryRegistryValuesEx(
    _In_ ULONG RelativeTo,
    _In_ PCWSTR Path,
    _Inout_ PRTL_QUERY_REGISTRY_TABLE QueryTable,
    _In_opt_ PVOID Context,
    _In_opt_ PVOID Environment)
{
    return RtlpQueryRegistryTable(RelativeTo, Path, QueryTable, Context, Environment);
}
