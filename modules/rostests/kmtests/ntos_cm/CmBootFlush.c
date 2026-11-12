/*
 * PROJECT:     LiberNT kernel-mode tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     No hive write fails while the system starts, however long the boot devices take
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <kmt_test.h>

#define LOG_HEAD_BYTES (1024 * 1024)

static
NTSTATUS
GetBootLogName(
    _Out_ PUNICODE_STRING Name)
{
    UNICODE_STRING SystemRoot = RTL_CONSTANT_STRING(L"\\SystemRoot");
    OBJECT_ATTRIBUTES ObjectAttributes;
    UNICODE_STRING Target;
    HANDLE Link;
    NTSTATUS Status;
    USHORT Index;

    InitializeObjectAttributes(&ObjectAttributes, &SystemRoot,
                               OBJ_KERNEL_HANDLE | OBJ_CASE_INSENSITIVE, NULL, NULL);
    Status = ZwOpenSymbolicLinkObject(&Link, SYMBOLIC_LINK_QUERY, &ObjectAttributes);
    if (!NT_SUCCESS(Status)) return Status;

    Name->MaximumLength = 512 * sizeof(WCHAR);
    Name->Length = 0;
    Name->Buffer = ExAllocatePoolWithTag(PagedPool, Name->MaximumLength, 'FBmC');
    if (Name->Buffer == NULL)
    {
        ZwClose(Link);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    Target = *Name;
    Target.MaximumLength -= 32 * sizeof(WCHAR);
    Status = ZwQuerySymbolicLinkObject(Link, &Target, NULL);
    ZwClose(Link);
    if (!NT_SUCCESS(Status))
    {
        ExFreePoolWithTag(Name->Buffer, 'FBmC');
        return Status;
    }

    for (Index = Target.Length / sizeof(WCHAR); Index > 0; Index--)
    {
        if (Target.Buffer[Index - 1] == L'\\') break;
    }

    Name->Length = Index * sizeof(WCHAR);
    return RtlAppendUnicodeToString(Name, L"lastboot.log");
}

static
NTSTATUS
GetFilePortName(
    _Out_ PUNICODE_STRING Name)
{
    static const WCHAR Option[] = L"DEBUGPORT=FILE:";
    UNICODE_STRING KeyName = RTL_CONSTANT_STRING(L"\\Registry\\Machine\\System\\CurrentControlSet\\Control");
    UNICODE_STRING ValueName = RTL_CONSTANT_STRING(L"SystemStartOptions");
    ULONG OptionLength = RTL_NUMBER_OF(Option) - 1;
    PKEY_VALUE_PARTIAL_INFORMATION Value;
    OBJECT_ATTRIBUTES ObjectAttributes;
    PCWSTR Options, Path;
    ULONG Size, Count, Index, Length;
    HANDLE Key;
    NTSTATUS Status;

    InitializeObjectAttributes(&ObjectAttributes, &KeyName,
                               OBJ_KERNEL_HANDLE | OBJ_CASE_INSENSITIVE, NULL, NULL);
    Status = ZwOpenKey(&Key, KEY_QUERY_VALUE, &ObjectAttributes);
    if (!NT_SUCCESS(Status)) return Status;

    Size = FIELD_OFFSET(KEY_VALUE_PARTIAL_INFORMATION, Data) + 1024 * sizeof(WCHAR);
    Value = ExAllocatePoolWithTag(PagedPool, Size, 'FBmC');
    if (Value == NULL)
    {
        ZwClose(Key);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    Status = ZwQueryValueKey(Key, &ValueName, KeyValuePartialInformation, Value, Size, &Size);
    ZwClose(Key);
    if (NT_SUCCESS(Status) && Value->Type != REG_SZ) Status = STATUS_OBJECT_TYPE_MISMATCH;
    if (!NT_SUCCESS(Status))
    {
        ExFreePoolWithTag(Value, 'FBmC');
        return Status;
    }

    Options = (PCWSTR)Value->Data;
    Count = Value->DataLength / sizeof(WCHAR);
    Status = STATUS_NOT_FOUND;
    for (Index = 0; Index + OptionLength <= Count; Index++)
    {
        if (Index != 0 && Options[Index - 1] != L' ' && Options[Index - 1] != L'/') continue;
        if (_wcsnicmp(&Options[Index], Option, OptionLength) != 0) continue;

        Path = &Options[Index + OptionLength];
        for (Length = 0; Index + OptionLength + Length < Count; Length++)
        {
            if (Path[Length] == UNICODE_NULL || Path[Length] == L' ' || Path[Length] == L',' || Path[Length] == L'\t')
                break;
        }

        if (Length == 0 || (Length >= 4 && _wcsnicmp(Path, L"GPT{", 4) == 0))
        {
            Status = STATUS_NOT_SUPPORTED;
            break;
        }

        Name->Length = (USHORT)(Length * sizeof(WCHAR));
        Name->MaximumLength = Name->Length + sizeof(WCHAR);
        Name->Buffer = ExAllocatePoolWithTag(PagedPool, Name->MaximumLength, 'FBmC');
        if (Name->Buffer == NULL)
        {
            Status = STATUS_INSUFFICIENT_RESOURCES;
            break;
        }

        RtlCopyMemory(Name->Buffer, Path, Name->Length);
        Name->Buffer[Length] = UNICODE_NULL;
        Status = STATUS_SUCCESS;
        break;
    }

    ExFreePoolWithTag(Value, 'FBmC');
    return Status;
}

static
ULONG
CountInLog(
    _In_ PUNICODE_STRING Name,
    _In_ PCSTR Marker,
    _Out_ PNTSTATUS OpenStatus)
{
    OBJECT_ATTRIBUTES ObjectAttributes;
    IO_STATUS_BLOCK IoStatus;
    LARGE_INTEGER Offset;
    SIZE_T MarkerLength = strlen(Marker);
    PUCHAR Buffer;
    HANDLE File;
    ULONG Count = 0;
    ULONG Length, Index;
    NTSTATUS Status;

    InitializeObjectAttributes(&ObjectAttributes, Name,
                               OBJ_KERNEL_HANDLE | OBJ_CASE_INSENSITIVE, NULL, NULL);
    *OpenStatus = ZwOpenFile(&File, FILE_READ_DATA | SYNCHRONIZE, &ObjectAttributes, &IoStatus,
                             FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                             FILE_NON_DIRECTORY_FILE | FILE_SYNCHRONOUS_IO_NONALERT);
    if (!NT_SUCCESS(*OpenStatus)) return 0;

    Buffer = ExAllocatePoolWithTag(PagedPool, LOG_HEAD_BYTES, 'FBmC');
    if (Buffer != NULL)
    {
        Offset.QuadPart = 0;
        Status = ZwReadFile(File, NULL, NULL, NULL, &IoStatus, Buffer, LOG_HEAD_BYTES, &Offset, NULL);
        if (NT_SUCCESS(Status))
        {
            Length = (ULONG)IoStatus.Information;
            for (Index = 0; Index + MarkerLength <= Length; Index++)
            {
                if (RtlCompareMemory(Buffer + Index, Marker, MarkerLength) == MarkerLength)
                    Count++;
            }
        }
        else
        {
            *OpenStatus = Status;
        }

        ExFreePoolWithTag(Buffer, 'FBmC');
    }
    else
    {
        *OpenStatus = STATUS_INSUFFICIENT_RESOURCES;
    }

    ZwClose(File);
    return Count;
}

START_TEST(CmBootFlush)
{
    UNICODE_STRING Name;
    NTSTATUS OpenStatus;
    NTSTATUS Status;
    ULONG Count;

    Status = GetFilePortName(&Name);
    if (!NT_SUCCESS(Status))
        Status = GetBootLogName(&Name);
    if (skip(NT_SUCCESS(Status), "No boot log to read (0x%lx)\n", Status))
        return;

    Count = CountInLog(&Name, "Failed to write the hive log", &OpenStatus);
    if (!skip(NT_SUCCESS(OpenStatus), "%wZ could not be read (0x%lx)\n", &Name, OpenStatus))
        ok(Count == 0, "%wZ records %lu failed hive log writes\n", &Name, Count);

    ExFreePoolWithTag(Name.Buffer, 'FBmC');
}
