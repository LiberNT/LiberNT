/*
 * PROJECT:     LiberNT ACPI Bus Driver
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Device and hardware identifiers of the processors
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"

#include <ntstrsafe.h>

#define NDEBUG
#include <debug.h>

#define ACPI_PROCESSOR_ID_CHARS 512
#define ACPI_PROCESSOR_VALUE_CHARS 128
#define ACPI_PROCESSOR_TAG 'IpcA'

LPWSTR ProcessorNameString = NULL;
static PWSTR AcpiProcessorDeviceId = NULL;
static PWSTR AcpiProcessorHardwareIds = NULL;
static SIZE_T AcpiProcessorHardwareIdsChars = 0;

static
NTSTATUS
AcpiProcessorReadValue(
    _In_ HANDLE Key,
    _In_ PCWSTR Name,
    _Out_writes_(Chars) PWSTR Buffer,
    _In_ SIZE_T Chars)
{
    ULONG Data[(FIELD_OFFSET(KEY_VALUE_PARTIAL_INFORMATION, Data) + ACPI_PROCESSOR_VALUE_CHARS * sizeof(WCHAR) +
                sizeof(ULONG) - 1) / sizeof(ULONG)];
    PKEY_VALUE_PARTIAL_INFORMATION Value = (PKEY_VALUE_PARTIAL_INFORMATION)Data;
    UNICODE_STRING ValueName;
    ULONG Length;
    NTSTATUS Status;

    PAGED_CODE();
    ASSERT(Chars != 0);

    Buffer[0] = UNICODE_NULL;
    RtlInitUnicodeString(&ValueName, Name);
    Status = ZwQueryValueKey(Key, &ValueName, KeyValuePartialInformation, Value, sizeof(Data), &Length);
    if (!NT_SUCCESS(Status))
        return Status;
    if (Value->Type != REG_SZ || Value->DataLength < sizeof(WCHAR))
        return STATUS_OBJECT_TYPE_MISMATCH;

    ASSERT(Value->DataLength <= sizeof(Data) - FIELD_OFFSET(KEY_VALUE_PARTIAL_INFORMATION, Data));
    return RtlStringCchCopyNW(Buffer, Chars, (PCWSTR)Value->Data, Value->DataLength / sizeof(WCHAR));
}

/* Windows 11 writes the spaces of the registry strings as underscores */
static
VOID
AcpiProcessorUnderscore(
    _Inout_ PWSTR String)
{
    for (; *String != UNICODE_NULL; String++)
    {
        /* FIXME: only spaces are measured on Windows 11; the other characters a device ID cannot hold are treated alike */
        if (*String <= L' ' || *String >= 0x7F || *String == L',')
            *String = L'_';
    }
}

static
VOID
AcpiProcessorCut(
    _Inout_ PWSTR String,
    _In_ PCWSTR At)
{
    PWSTR Found = wcsstr(String, At);

    if (Found != NULL)
        *Found = UNICODE_NULL;
}

/* Appends one string and its terminating NUL to a multi-string under construction */
static
NTSTATUS
AcpiProcessorAppend(
    _Inout_updates_(ACPI_PROCESSOR_ID_CHARS) PWSTR Buffer,
    _Inout_ PSIZE_T Used,
    _In_ PCWSTR Prefix,
    _In_ PCWSTR Vendor,
    _In_ PCWSTR Rest)
{
    size_t Remaining;
    PWSTR End;
    NTSTATUS Status;

    ASSERT(*Used < ACPI_PROCESSOR_ID_CHARS);

    Status = RtlStringCchPrintfExW(Buffer + *Used, ACPI_PROCESSOR_ID_CHARS - *Used, &End, &Remaining, 0,
                                   L"%ls%ls_-_%ls", Prefix, Vendor, Rest);
    if (!NT_SUCCESS(Status) || Remaining < 2)
        return STATUS_BUFFER_OVERFLOW;

    *Used = (SIZE_T)(End - Buffer) + 1;
    ASSERT(Buffer[*Used - 1] == UNICODE_NULL);
    return STATUS_SUCCESS;
}

static
PWSTR
AcpiProcessorDuplicate(
    _In_reads_(Chars) PCWSTR Source,
    _In_ SIZE_T Chars)
{
    PWSTR Copy;

    ASSERT(Chars != 0);
    ASSERT(Source[Chars - 1] == UNICODE_NULL);

    Copy = ExAllocatePoolWithTag(PagedPool, Chars * sizeof(WCHAR), ACPI_PROCESSOR_TAG);
    if (Copy != NULL)
        RtlCopyMemory(Copy, Source, Chars * sizeof(WCHAR));
    return Copy;
}

/*
 * The identifiers come from the processor's registry description: the device ID is
 * vendor, identifier without its stepping and name; the hardware IDs drop the name,
 * then the model, then the family, each as an ACPI\ and a * form.
 */
NTSTATUS
AcpiProcessorIdsInitialize(VOID)
{
    UNICODE_STRING KeyName = RTL_CONSTANT_STRING(L"\\Registry\\Machine\\HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0");
    WCHAR Vendor[ACPI_PROCESSOR_VALUE_CHARS], Model[ACPI_PROCESSOR_VALUE_CHARS], Name[ACPI_PROCESSOR_VALUE_CHARS];
    WCHAR Family[ACPI_PROCESSOR_VALUE_CHARS], Arch[ACPI_PROCESSOR_VALUE_CHARS], IdName[ACPI_PROCESSOR_VALUE_CHARS];
    WCHAR Buffer[ACPI_PROCESSOR_ID_CHARS];
    OBJECT_ATTRIBUTES Attributes;
    SIZE_T Used = 0;
    HANDLE Key;
    NTSTATUS Status;

    PAGED_CODE();
    ASSERT(AcpiProcessorDeviceId == NULL);
    ASSERT(AcpiProcessorHardwareIds == NULL);
    ASSERT(ProcessorNameString == NULL);

    InitializeObjectAttributes(&Attributes, &KeyName, OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, NULL, NULL);
    Status = ZwOpenKey(&Key, KEY_QUERY_VALUE, &Attributes);
    if (!NT_SUCCESS(Status))
        return Status;

    Status = AcpiProcessorReadValue(Key, L"VendorIdentifier", Vendor, ARRAYSIZE(Vendor));
    if (NT_SUCCESS(Status))
        Status = AcpiProcessorReadValue(Key, L"Identifier", Model, ARRAYSIZE(Model));
    if (NT_SUCCESS(Status) && !NT_SUCCESS(AcpiProcessorReadValue(Key, L"ProcessorNameString", Name, ARRAYSIZE(Name))))
        Name[0] = UNICODE_NULL;
    ZwClose(Key);
    if (!NT_SUCCESS(Status))
        return Status;

    /* TODO: the identifiers Windows 11 gives ARM64 processors are not measured; an identifier without a stepping is used whole */
    AcpiProcessorCut(Model, L" Stepping");
    RtlCopyMemory(Family, Model, sizeof(Family));
    AcpiProcessorCut(Family, L" Model");
    RtlCopyMemory(Arch, Family, sizeof(Arch));
    AcpiProcessorCut(Arch, L" ");
    RtlCopyMemory(IdName, Name, sizeof(IdName));
    AcpiProcessorUnderscore(Vendor);
    AcpiProcessorUnderscore(Model);
    AcpiProcessorUnderscore(Family);
    AcpiProcessorUnderscore(IdName);

    if (IdName[0] != UNICODE_NULL)
        Status = RtlStringCchPrintfW(Buffer, ARRAYSIZE(Buffer), L"ACPI\\%ls_-_%ls_-_%ls", Vendor, Model, IdName);
    else
        /* TODO: the device ID Windows 11 gives a processor without a name string is not measured; it ends after the model */
        Status = RtlStringCchPrintfW(Buffer, ARRAYSIZE(Buffer), L"ACPI\\%ls_-_%ls", Vendor, Model);
    if (!NT_SUCCESS(Status))
        return Status;
    AcpiProcessorDeviceId = AcpiProcessorDuplicate(Buffer, wcslen(Buffer) + 1);

    Status = AcpiProcessorAppend(Buffer, &Used, L"ACPI\\", Vendor, Model);
    if (NT_SUCCESS(Status))
        Status = AcpiProcessorAppend(Buffer, &Used, L"*", Vendor, Model);
    if (NT_SUCCESS(Status))
        Status = AcpiProcessorAppend(Buffer, &Used, L"ACPI\\", Vendor, Family);
    if (NT_SUCCESS(Status))
        Status = AcpiProcessorAppend(Buffer, &Used, L"*", Vendor, Family);
    if (NT_SUCCESS(Status))
        Status = AcpiProcessorAppend(Buffer, &Used, L"ACPI\\", Vendor, Arch);
    if (NT_SUCCESS(Status))
        Status = AcpiProcessorAppend(Buffer, &Used, L"*", Vendor, Arch);
    if (!NT_SUCCESS(Status))
        return Status;

    ASSERT(Used < ACPI_PROCESSOR_ID_CHARS);
    Buffer[Used++] = UNICODE_NULL;
    AcpiProcessorHardwareIds = AcpiProcessorDuplicate(Buffer, Used);
    AcpiProcessorHardwareIdsChars = Used;

    if (Name[0] != UNICODE_NULL)
        ProcessorNameString = AcpiProcessorDuplicate(Name, wcslen(Name) + 1);

    DPRINT("Processor device ID %ls\n", AcpiProcessorDeviceId);
    if (AcpiProcessorDeviceId == NULL || AcpiProcessorHardwareIds == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;
    return STATUS_SUCCESS;
}

NTSTATUS
AcpiProcessorQueryId(
    _In_ BUS_QUERY_ID_TYPE IdType,
    _Out_ PWSTR *Ids)
{
    PCWSTR Source;
    SIZE_T Chars;

    PAGED_CODE();

    *Ids = NULL;
    if (IdType == BusQueryDeviceID)
    {
        Source = AcpiProcessorDeviceId;
        Chars = Source != NULL ? wcslen(Source) + 1 : 0;
    }
    else if (IdType == BusQueryHardwareIDs)
    {
        Source = AcpiProcessorHardwareIds;
        Chars = AcpiProcessorHardwareIdsChars;
    }
    else
    {
        return STATUS_NOT_SUPPORTED;
    }

    if (Source == NULL)
        return STATUS_NOT_FOUND;

    *Ids = AcpiProcessorDuplicate(Source, Chars);
    return *Ids != NULL ? STATUS_SUCCESS : STATUS_INSUFFICIENT_RESOURCES;
}
