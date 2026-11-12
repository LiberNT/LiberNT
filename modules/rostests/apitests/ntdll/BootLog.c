/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests that a boot with boot logging lists the loaded drivers in %SystemRoot%\ntbtlog.txt
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"
#include <winreg.h>

#define BOOT_LOG_MAX_SIZE (4 * 1024 * 1024)

static
BOOLEAN
HasBootLogOption(
    _In_reads_(Count) PCWSTR Options,
    _In_ SIZE_T Count)
{
    static const WCHAR Option[] = L"BOOTLOG";
    SIZE_T Length = RTL_NUMBER_OF(Option) - 1;
    SIZE_T Index;

    for (Index = 0; Index + Length <= Count; Index++)
    {
        if ((Index != 0) && (Options[Index - 1] != L' ') && (Options[Index - 1] != L'/')) continue;
        if (_wcsnicmp(&Options[Index], Option, Length) != 0) continue;

        if ((Index + Length == Count) ||
            (Options[Index + Length] == UNICODE_NULL) ||
            (Options[Index + Length] == L' ') ||
            (Options[Index + Length] == L'/'))
        {
            return TRUE;
        }
    }

    return FALSE;
}

static
BOOLEAN
SystemStartedWithBootLog(VOID)
{
    UNICODE_STRING KeyName = RTL_CONSTANT_STRING(L"\\Registry\\Machine\\System\\CurrentControlSet\\Control");
    UNICODE_STRING ValueName = RTL_CONSTANT_STRING(L"SystemStartOptions");
    UCHAR Buffer[FIELD_OFFSET(KEY_VALUE_PARTIAL_INFORMATION, Data) + 1024 * sizeof(WCHAR)];
    PKEY_VALUE_PARTIAL_INFORMATION Value = (PKEY_VALUE_PARTIAL_INFORMATION)Buffer;
    OBJECT_ATTRIBUTES Attributes;
    NTSTATUS Status;
    ULONG Length;
    HANDLE Key;

    InitializeObjectAttributes(&Attributes, &KeyName, OBJ_CASE_INSENSITIVE, NULL, NULL);
    Status = NtOpenKey(&Key, KEY_QUERY_VALUE, &Attributes);
    ok_ntstatus(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status)) return FALSE;

    Status = NtQueryValueKey(Key, &ValueName, KeyValuePartialInformation, Value, sizeof(Buffer), &Length);
    NtClose(Key);
    ok_ntstatus(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status)) return FALSE;

    ok(Value->Type == REG_SZ, "SystemStartOptions has type %lu\n", Value->Type);
    if (Value->Type != REG_SZ) return FALSE;

    return HasBootLogOption((PCWSTR)Value->Data, Value->DataLength / sizeof(WCHAR));
}

static
ULONG
CountLoadedDriverLines(
    _In_reads_(Count) PCWSTR Text,
    _In_ SIZE_T Count)
{
    static const WCHAR Prefix[] = L"Loaded driver ";
    SIZE_T Length = RTL_NUMBER_OF(Prefix) - 1;
    ULONG Lines = 0;
    SIZE_T Index;

    for (Index = 0; Index + Length <= Count; Index++)
    {
        if ((Index != 0) && (Text[Index - 1] != L'\n')) continue;
        if (RtlCompareMemory(&Text[Index], Prefix, Length * sizeof(WCHAR)) == Length * sizeof(WCHAR)) Lines++;
    }

    return Lines;
}

START_TEST(BootLog)
{
    UNICODE_STRING FileName = RTL_CONSTANT_STRING(L"\\SystemRoot\\ntbtlog.txt");
    FILE_STANDARD_INFORMATION Standard;
    OBJECT_ATTRIBUTES Attributes;
    IO_STATUS_BLOCK IoStatus;
    LARGE_INTEGER Offset;
    ULONG Size, TextSize, Lines;
    PUCHAR Data;
    PWSTR Text;
    NTSTATUS Status;
    HANDLE File;

    if (!SystemStartedWithBootLog())
    {
        skip("The system did not start with boot logging\n");
        return;
    }

    InitializeObjectAttributes(&Attributes, &FileName, OBJ_CASE_INSENSITIVE, NULL, NULL);
    Status = NtOpenFile(&File,
                        FILE_READ_DATA | SYNCHRONIZE,
                        &Attributes,
                        &IoStatus,
                        FILE_SHARE_READ | FILE_SHARE_WRITE,
                        FILE_SYNCHRONOUS_IO_NONALERT | FILE_NON_DIRECTORY_FILE);
    ok_ntstatus(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status)) return;

    RtlZeroMemory(&Standard, sizeof(Standard));
    Status = NtQueryInformationFile(File, &IoStatus, &Standard, sizeof(Standard), FileStandardInformation);
    ok_ntstatus(Status, STATUS_SUCCESS);
    ok((Standard.EndOfFile.QuadPart > 0) && (Standard.EndOfFile.QuadPart <= BOOT_LOG_MAX_SIZE),
       "ntbtlog.txt has %I64d bytes\n", Standard.EndOfFile.QuadPart);
    if (!NT_SUCCESS(Status) ||
        (Standard.EndOfFile.QuadPart <= 0) ||
        (Standard.EndOfFile.QuadPart > BOOT_LOG_MAX_SIZE))
    {
        NtClose(File);
        return;
    }

    Size = (ULONG)Standard.EndOfFile.QuadPart;
    Data = RtlAllocateHeap(RtlGetProcessHeap(), 0, Size);
    ok(Data != NULL, "No memory for %lu bytes\n", Size);
    if (Data == NULL)
    {
        NtClose(File);
        return;
    }

    Offset.QuadPart = 0;
    Status = NtReadFile(File, NULL, NULL, NULL, &IoStatus, Data, Size, &Offset, NULL);
    NtClose(File);
    ok_ntstatus(Status, STATUS_SUCCESS);
    ok(IoStatus.Information == Size, "Read %Iu of %lu bytes\n", (size_t)IoStatus.Information, Size);
    if (!NT_SUCCESS(Status) || (IoStatus.Information != Size))
    {
        RtlFreeHeap(RtlGetProcessHeap(), 0, Data);
        return;
    }

    if ((Size >= 2) && (Data[0] == 0xFF) && (Data[1] == 0xFE))
    {
        Lines = CountLoadedDriverLines((PCWSTR)(Data + 2), (Size - 2) / sizeof(WCHAR));
    }
    else
    {
        TextSize = Size * sizeof(WCHAR);
        Text = RtlAllocateHeap(RtlGetProcessHeap(), 0, TextSize);
        ok(Text != NULL, "No memory for %lu bytes\n", TextSize);
        if (Text == NULL)
        {
            RtlFreeHeap(RtlGetProcessHeap(), 0, Data);
            return;
        }

        Status = RtlMultiByteToUnicodeN(Text, TextSize, &TextSize, (PCSTR)Data, Size);
        ok_ntstatus(Status, STATUS_SUCCESS);
        Lines = NT_SUCCESS(Status) ? CountLoadedDriverLines(Text, TextSize / sizeof(WCHAR)) : 0;
        RtlFreeHeap(RtlGetProcessHeap(), 0, Text);
    }

    ok(Lines != 0, "ntbtlog.txt (%lu bytes) lists no loaded driver\n", Size);
    trace("ntbtlog.txt lists %lu loaded drivers\n", Lines);

    RtlFreeHeap(RtlGetProcessHeap(), 0, Data);
}
