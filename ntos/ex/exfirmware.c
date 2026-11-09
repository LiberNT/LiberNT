/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     What the firmware holds: its variables, its tables, its entries
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

/* GLOBALS ********************************************************************/

FIRMWARE_TYPE ExpFirmwareType = FirmwareTypeUnknown;

/* PRIVATE DATA ***************************************************************/

#define EXP_FIRMWARE_TAG 'mriF'

/* The longest name a firmware variable may be given */
#define EXP_FIRMWARE_NAME_MAXIMUM 1024

/* The most a firmware variable may hold */
#define EXP_FIRMWARE_VALUE_MAXIMUM (32 * 1024)

/* The vendor the entries that say what may be started belong to */
static const GUID ExpGlobalVariableGuid =
    { 0x8BE4DF61, 0x93CA, 0x11D2,
      { 0xAA, 0x0D, 0x00, 0xE0, 0x98, 0x03, 0x2B, 0x8C } };

/* PRIVATE FUNCTIONS **********************************************************/

/*
 * The firmware wants a name as a plain run of characters ending in a zero,
 * which a counted string is not, so one is made.
 */
static
NTSTATUS
ExpCopyVariableName(
    _Out_ PWSTR *Name,
    _In_ PUNICODE_STRING Source)
{
    PWSTR Buffer;

    *Name = NULL;

    if ((Source->Length == 0) || (Source->Buffer == NULL) ||
        (Source->Length > EXP_FIRMWARE_NAME_MAXIMUM))
    {
        return STATUS_INVALID_PARAMETER;
    }

    Buffer = ExAllocatePoolZero(PagedPool,
                                Source->Length + sizeof(WCHAR),
                                EXP_FIRMWARE_TAG);
    if (Buffer == NULL)
    {
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    RtlCopyMemory(Buffer, Source->Buffer, Source->Length);
    Buffer[Source->Length / sizeof(WCHAR)] = UNICODE_NULL;

    *Name = Buffer;

    return STATUS_SUCCESS;
}

/*
 * Reads one of the entries that say what the firmware may start. The entries
 * are numbered variables of the firmware's own vendor, so an entry's number
 * becomes the name it is read under.
 */
static
NTSTATUS
ExpReadBootVariable(
    _In_ PCWSTR Format,
    _In_ ULONG Id,
    _Out_writes_bytes_opt_(*Length) PVOID Buffer,
    _Inout_ PULONG Length)
{
    WCHAR Name[16];
    ULONG Index = 0;
    ULONG Digit;

    while ((Format[Index] != UNICODE_NULL) &&
           (Index < RTL_NUMBER_OF(Name) - 5))
    {
        Name[Index] = Format[Index];
        Index++;
    }

    /* The number is four hexadecimal digits, most significant first. */
    for (Digit = 4; Digit > 0; Digit--)
    {
        ULONG Nibble = (Id >> ((Digit - 1) * 4)) & 0xF;

        Name[Index++] = (WCHAR)((Nibble < 10) ? (L'0' + Nibble) :
                                                (L'A' + Nibble - 10));
    }

    Name[Index] = UNICODE_NULL;

    return HalGetEnvironmentVariableEx(Name,
                                       &ExpGlobalVariableGuid,
                                       Buffer,
                                       Length,
                                       NULL);
}

/* PUBLIC FUNCTIONS ***********************************************************/

FIRMWARE_TYPE
NTAPI
ExGetFirmwareType(VOID)
{
    return ExpFirmwareType;
}

/*
 * Reads a variable the firmware keeps for itself across restarts. Only a
 * machine whose firmware offers such variables has any to read, which on this
 * kernel means a machine started through EFI.
 */
NTSTATUS
NTAPI
ExGetFirmwareEnvironmentVariable(
    _In_ PUNICODE_STRING VariableName,
    _In_ LPGUID VendorGuid,
    _Out_writes_bytes_opt_(*ValueLength) PVOID Value,
    _Inout_ PULONG ValueLength,
    _Out_opt_ PULONG Attributes)
{
    PWSTR Name;
    NTSTATUS Status;

    PAGED_CODE();

    /* Only a machine started through EFI keeps variables of this kind. */
    if (ExpFirmwareType != FirmwareTypeUefi)
    {
        return STATUS_NOT_IMPLEMENTED;
    }

    Status = ExpCopyVariableName(&Name, VariableName);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    Status = HalGetEnvironmentVariableEx(Name,
                                         VendorGuid,
                                         Value,
                                         ValueLength,
                                         Attributes);

    ExFreePoolWithTag(Name, EXP_FIRMWARE_TAG);

    return Status;
}

NTSTATUS
NTAPI
ExSetFirmwareEnvironmentVariable(
    _In_ PUNICODE_STRING VariableName,
    _In_ LPGUID VendorGuid,
    _In_reads_bytes_opt_(ValueLength) PVOID Value,
    _In_ ULONG ValueLength,
    _In_ ULONG Attributes)
{
    PWSTR Name;
    NTSTATUS Status;

    PAGED_CODE();

    if (ExpFirmwareType != FirmwareTypeUefi)
    {
        return STATUS_NOT_IMPLEMENTED;
    }

    if (ValueLength > EXP_FIRMWARE_VALUE_MAXIMUM)
    {
        return STATUS_INVALID_PARAMETER_4;
    }

    Status = ExpCopyVariableName(&Name, VariableName);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    Status = HalSetEnvironmentVariableEx(Name,
                                         VendorGuid,
                                         Value,
                                         ValueLength,
                                         Attributes);

    ExFreePoolWithTag(Name, EXP_FIRMWARE_TAG);

    return Status;
}

/*
 * Hands back one of the tables the firmware published, by the signature of
 * whoever provides it and the identifier of the table within that provider.
 */
NTSTATUS
NTAPI
ExGetSystemFirmwareTable(
    _In_ ULONG FirmwareTableProviderSignature,
    _In_ ULONG FirmwareTableID,
    _Out_writes_bytes_to_opt_(BufferLength, *ReturnLength) PVOID FirmwareTableBuffer,
    _In_ ULONG BufferLength,
    _Out_opt_ PULONG ReturnLength)
{
    ULONG Length = 0;
    NTSTATUS Status;

    PAGED_CODE();

    if (FirmwareTableProviderSignature != SIG_RSMB)
    {
        /* TODO: only the raw table of the machine's own description is served;
           the other providers are not registered. */
        return STATUS_NOT_IMPLEMENTED;
    }

    if (FirmwareTableID != 0)
    {
        return STATUS_NOT_FOUND;
    }

    Status = ExpGetRawSMBiosTable(FirmwareTableBuffer, &Length, BufferLength);

    if (ReturnLength != NULL)
    {
        *ReturnLength = Length;
    }

    return Status;
}

/* BOOT AND DRIVER ENTRIES ****************************************************/

/*
 * The entries that say what the firmware may start, and in what order, live in
 * the firmware's own variables. A machine that was not started through EFI has
 * no such variables, so there is nothing for these to read or write and the
 * caller is told so rather than being told it succeeded.
 */

/* The variable that holds the order the entries are tried in */
#define EXP_BOOT_ORDER_NAME L"BootOrder"

/* The variable that holds how long the firmware waits before choosing */
#define EXP_BOOT_TIMEOUT_NAME L"Timeout"

/* The variable that names the entry the firmware started this time */
#define EXP_BOOT_CURRENT_NAME L"BootCurrent"

/* The variable that names the entry the firmware is to start next */
#define EXP_BOOT_NEXT_NAME L"BootNext"

/* The attributes an entry of the firmware's own carries */
#define EXP_FIRMWARE_VARIABLE_ATTRIBUTES 0x00000007

/* The most entries the order may name */
#define EXP_BOOT_ORDER_MAXIMUM 256

/*
 * Which of the options a caller is asking to change. The names are the ones the
 * documented interface uses; no header in this tree declares them.
 */
#define BOOT_OPTIONS_FIELD_TIMEOUT               0x01
#define BOOT_OPTIONS_FIELD_CURRENT_BOOT_ENTRY_ID 0x02
#define BOOT_OPTIONS_FIELD_NEXT_BOOT_ENTRY_ID    0x04
#define BOOT_OPTIONS_FIELD_HEADLESS_REDIRECTION  0x08

static
BOOLEAN
ExpFirmwareHasVariables(VOID)
{
    return ExpFirmwareType == FirmwareTypeUefi;
}

/*
 * Reads the order the entries are tried in, as the numbers of the entries in
 * the order they are to be tried.
 */
static
NTSTATUS
ExpReadBootOrder(
    _Out_writes_to_(Capacity, *Count) PULONG Ids,
    _In_ ULONG Capacity,
    _Out_ PULONG Count)
{
    USHORT Order[EXP_BOOT_ORDER_MAXIMUM];
    ULONG Length = sizeof(Order);
    ULONG Number;
    ULONG Index;
    NTSTATUS Status;

    *Count = 0;

    Status = HalGetEnvironmentVariableEx(EXP_BOOT_ORDER_NAME,
                                         &ExpGlobalVariableGuid,
                                         Order,
                                         &Length,
                                         NULL);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    Number = Length / (ULONG)sizeof(USHORT);
    *Count = Number;

    if (Number > Capacity)
    {
        return STATUS_BUFFER_TOO_SMALL;
    }

    for (Index = 0; Index < Number; Index++)
    {
        Ids[Index] = Order[Index];
    }

    return STATUS_SUCCESS;
}

static
NTSTATUS
ExpWriteBootOrder(
    _In_reads_(Count) PULONG Ids,
    _In_ ULONG Count)
{
    USHORT Order[EXP_BOOT_ORDER_MAXIMUM];
    ULONG Index;

    if (Count > RTL_NUMBER_OF(Order))
    {
        return STATUS_INVALID_PARAMETER_2;
    }

    for (Index = 0; Index < Count; Index++)
    {
        if (Ids[Index] > MAXUSHORT)
        {
            return STATUS_INVALID_PARAMETER_1;
        }

        Order[Index] = (USHORT)Ids[Index];
    }

    return HalSetEnvironmentVariableEx(EXP_BOOT_ORDER_NAME,
                                       &ExpGlobalVariableGuid,
                                       Order,
                                       Count * (ULONG)sizeof(USHORT),
                                       EXP_FIRMWARE_VARIABLE_ATTRIBUTES);
}

/*
 * Writes one entry under the number it is to be known by. The caller's record
 * is passed through as it stands, because what an entry holds is the firmware's
 * business and not this kernel's.
 */
static
NTSTATUS
ExpWriteBootVariable(
    _In_ PCWSTR Format,
    _In_ ULONG Id,
    _In_reads_bytes_opt_(Length) PVOID Buffer,
    _In_ ULONG Length)
{
    WCHAR Name[16];
    ULONG Index = 0;
    ULONG Digit;

    if (Id > MAXUSHORT)
    {
        return STATUS_INVALID_PARAMETER_2;
    }

    while ((Format[Index] != UNICODE_NULL) &&
           (Index < RTL_NUMBER_OF(Name) - 5))
    {
        Name[Index] = Format[Index];
        Index++;
    }

    for (Digit = 4; Digit > 0; Digit--)
    {
        ULONG Nibble = (Id >> ((Digit - 1) * 4)) & 0xF;

        Name[Index++] = (WCHAR)((Nibble < 10) ? (L'0' + Nibble) :
                                                (L'A' + Nibble - 10));
    }

    Name[Index] = UNICODE_NULL;

    return HalSetEnvironmentVariableEx(Name,
                                       &ExpGlobalVariableGuid,
                                       Buffer,
                                       Length,
                                       EXP_FIRMWARE_VARIABLE_ATTRIBUTES);
}

/*
 * Lists the entries the order names, one record after another, each starting on
 * a four-byte boundary as the records themselves are built.
 */
static
NTSTATUS
ExpEnumerateEntries(
    _In_ PCWSTR Format,
    _Out_opt_ PVOID Buffer,
    _Inout_ PULONG BufferLength)
{
    ULONG Ids[EXP_BOOT_ORDER_MAXIMUM];
    PUCHAR Current = Buffer;
    ULONG Count = 0;
    ULONG Used = 0;
    ULONG Index;
    NTSTATUS Status;

    Status = ExpReadBootOrder(Ids, RTL_NUMBER_OF(Ids), &Count);
    if (!NT_SUCCESS(Status))
    {
        *BufferLength = 0;
        return Status;
    }

    for (Index = 0; Index < Count; Index++)
    {
        ULONG Length = 0;

        /* Asking with no room gives the length the entry needs. */
        ExpReadBootVariable(Format, Ids[Index], NULL, &Length);

        if (Length == 0)
        {
            continue;
        }

        Length = ALIGN_UP_BY(Length, sizeof(ULONG));

        if ((Buffer == NULL) || (Used + Length > *BufferLength))
        {
            Used += Length;
            continue;
        }

        Status = ExpReadBootVariable(Format, Ids[Index], Current, &Length);
        if (!NT_SUCCESS(Status))
        {
            continue;
        }

        Length = ALIGN_UP_BY(Length, sizeof(ULONG));
        Current += Length;
        Used += Length;
    }

    if ((Buffer == NULL) || (Used > *BufferLength))
    {
        *BufferLength = Used;
        return STATUS_BUFFER_TOO_SMALL;
    }

    *BufferLength = Used;

    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
NtAddBootEntry(
    _In_ PBOOT_ENTRY BootEntry,
    _In_ ULONG Id)
{
    PAGED_CODE();

    if (!SeSinglePrivilegeCheck(SeSystemEnvironmentPrivilege,
                                ExGetPreviousMode()))
    {
        return STATUS_PRIVILEGE_NOT_HELD;
    }

    if (!ExpFirmwareHasVariables())
    {
        return STATUS_NOT_IMPLEMENTED;
    }

    if (BootEntry == NULL)
    {
        return STATUS_INVALID_PARAMETER_1;
    }

    return ExpWriteBootVariable(L"Boot",
                                Id,
                                BootEntry,
                                BootEntry->Length);
}

NTSTATUS
NTAPI
NtDeleteBootEntry(
    _In_ ULONG Id)
{
    PAGED_CODE();

    if (!SeSinglePrivilegeCheck(SeSystemEnvironmentPrivilege,
                                ExGetPreviousMode()))
    {
        return STATUS_PRIVILEGE_NOT_HELD;
    }

    if (!ExpFirmwareHasVariables())
    {
        return STATUS_NOT_IMPLEMENTED;
    }

    /* A variable written with nothing in it is a variable deleted. */
    return ExpWriteBootVariable(L"Boot", Id, NULL, 0);
}

NTSTATUS
NTAPI
NtModifyBootEntry(
    _In_ PBOOT_ENTRY BootEntry)
{
    PAGED_CODE();

    if (BootEntry == NULL)
    {
        return STATUS_INVALID_PARAMETER_1;
    }

    return NtAddBootEntry(BootEntry, BootEntry->Id);
}

NTSTATUS
NTAPI
NtEnumerateBootEntries(
    _Out_opt_ PVOID Buffer,
    _Inout_ PULONG BufferLength)
{
    PAGED_CODE();

    if (!SeSinglePrivilegeCheck(SeSystemEnvironmentPrivilege,
                                ExGetPreviousMode()))
    {
        return STATUS_PRIVILEGE_NOT_HELD;
    }

    if (!ExpFirmwareHasVariables())
    {
        return STATUS_NOT_IMPLEMENTED;
    }

    return ExpEnumerateEntries(L"Boot", Buffer, BufferLength);
}

NTSTATUS
NTAPI
NtQueryBootEntryOrder(
    _Out_opt_ PULONG Ids,
    _Inout_ PULONG Count)
{
    ULONG Found = 0;
    NTSTATUS Status;

    PAGED_CODE();

    if (!SeSinglePrivilegeCheck(SeSystemEnvironmentPrivilege,
                                ExGetPreviousMode()))
    {
        return STATUS_PRIVILEGE_NOT_HELD;
    }

    if (!ExpFirmwareHasVariables())
    {
        return STATUS_NOT_IMPLEMENTED;
    }

    Status = ExpReadBootOrder(Ids,
                              (Ids != NULL) ? *Count : 0,
                              &Found);

    *Count = Found;

    return Status;
}

NTSTATUS
NTAPI
NtSetBootEntryOrder(
    _In_ PULONG Ids,
    _In_ PULONG Count)
{
    PAGED_CODE();

    if (!SeSinglePrivilegeCheck(SeSystemEnvironmentPrivilege,
                                ExGetPreviousMode()))
    {
        return STATUS_PRIVILEGE_NOT_HELD;
    }

    if (!ExpFirmwareHasVariables())
    {
        return STATUS_NOT_IMPLEMENTED;
    }

    if ((Ids == NULL) || (Count == NULL))
    {
        return STATUS_INVALID_PARAMETER;
    }

    return ExpWriteBootOrder(Ids, *Count);
}

/*
 * What the firmware does before it starts anything: how long it waits, what it
 * started this time, and what it is to start next.
 */
NTSTATUS
NTAPI
NtQueryBootOptions(
    _Out_opt_ PBOOT_OPTIONS BootOptions,
    _Inout_ PULONG BootOptionsLength)
{
    ULONG Needed = (ULONG)FIELD_OFFSET(BOOT_OPTIONS, HeadlessRedirection) +
                   (ULONG)sizeof(WCHAR);
    USHORT Value;
    ULONG Length;

    PAGED_CODE();

    if (!SeSinglePrivilegeCheck(SeSystemEnvironmentPrivilege,
                                ExGetPreviousMode()))
    {
        return STATUS_PRIVILEGE_NOT_HELD;
    }

    if (!ExpFirmwareHasVariables())
    {
        return STATUS_NOT_IMPLEMENTED;
    }

    if ((BootOptions == NULL) || (*BootOptionsLength < Needed))
    {
        *BootOptionsLength = Needed;
        return STATUS_BUFFER_TOO_SMALL;
    }

    RtlZeroMemory(BootOptions, Needed);

    BootOptions->Version = 1;
    BootOptions->Length = Needed;

    Length = sizeof(Value);
    if (NT_SUCCESS(HalGetEnvironmentVariableEx(EXP_BOOT_TIMEOUT_NAME,
                                               &ExpGlobalVariableGuid,
                                               &Value,
                                               &Length,
                                               NULL)))
    {
        BootOptions->Timeout = Value;
    }

    Length = sizeof(Value);
    if (NT_SUCCESS(HalGetEnvironmentVariableEx(EXP_BOOT_CURRENT_NAME,
                                               &ExpGlobalVariableGuid,
                                               &Value,
                                               &Length,
                                               NULL)))
    {
        BootOptions->CurrentBootEntryId = Value;
    }

    Length = sizeof(Value);
    if (NT_SUCCESS(HalGetEnvironmentVariableEx(EXP_BOOT_NEXT_NAME,
                                               &ExpGlobalVariableGuid,
                                               &Value,
                                               &Length,
                                               NULL)))
    {
        BootOptions->NextBootEntryId = Value;
    }

    *BootOptionsLength = Needed;

    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
NtSetBootOptions(
    _In_ PBOOT_OPTIONS BootOptions,
    _In_ ULONG FieldsToChange)
{
    USHORT Value;
    NTSTATUS Status = STATUS_SUCCESS;

    PAGED_CODE();

    if (!SeSinglePrivilegeCheck(SeSystemEnvironmentPrivilege,
                                ExGetPreviousMode()))
    {
        return STATUS_PRIVILEGE_NOT_HELD;
    }

    if (!ExpFirmwareHasVariables())
    {
        return STATUS_NOT_IMPLEMENTED;
    }

    if (BootOptions == NULL)
    {
        return STATUS_INVALID_PARAMETER_1;
    }

    if (FieldsToChange & BOOT_OPTIONS_FIELD_TIMEOUT)
    {
        Value = (USHORT)BootOptions->Timeout;

        Status = HalSetEnvironmentVariableEx(
                     EXP_BOOT_TIMEOUT_NAME,
                     &ExpGlobalVariableGuid,
                     &Value,
                     sizeof(Value),
                     EXP_FIRMWARE_VARIABLE_ATTRIBUTES);
        if (!NT_SUCCESS(Status))
        {
            return Status;
        }
    }

    if (FieldsToChange & BOOT_OPTIONS_FIELD_NEXT_BOOT_ENTRY_ID)
    {
        Value = (USHORT)BootOptions->NextBootEntryId;

        Status = HalSetEnvironmentVariableEx(
                     EXP_BOOT_NEXT_NAME,
                     &ExpGlobalVariableGuid,
                     &Value,
                     sizeof(Value),
                     EXP_FIRMWARE_VARIABLE_ATTRIBUTES);
    }

    return Status;
}

/*
 * Turns one way of naming a file into another. The forms the firmware uses are
 * its own, so only the form this kernel already understands is translated, and
 * that one is handed back unchanged.
 */
NTSTATUS
NTAPI
NtTranslateFilePath(
    _In_ PFILE_PATH InputFilePath,
    _In_ ULONG OutputType,
    _Out_opt_ PFILE_PATH OutputFilePath,
    _In_ ULONG OutputFilePathLength)
{
    PAGED_CODE();

    if (!SeSinglePrivilegeCheck(SeSystemEnvironmentPrivilege,
                                ExGetPreviousMode()))
    {
        return STATUS_PRIVILEGE_NOT_HELD;
    }

    if (InputFilePath == NULL)
    {
        return STATUS_INVALID_PARAMETER_1;
    }

    if (InputFilePath->Type != OutputType)
    {
        /* TODO: no translation between the firmware's forms is carried out. */
        return STATUS_NOT_IMPLEMENTED;
    }

    if ((OutputFilePath == NULL) ||
        (OutputFilePathLength < InputFilePath->Length))
    {
        return STATUS_BUFFER_TOO_SMALL;
    }

    RtlCopyMemory(OutputFilePath, InputFilePath, InputFilePath->Length);

    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
NtAddDriverEntry(
    _In_ PEFI_DRIVER_ENTRY DriverEntry,
    _In_ ULONG Id)
{
    PAGED_CODE();

    if (!SeSinglePrivilegeCheck(SeSystemEnvironmentPrivilege,
                                ExGetPreviousMode()))
    {
        return STATUS_PRIVILEGE_NOT_HELD;
    }

    if (!ExpFirmwareHasVariables())
    {
        return STATUS_NOT_IMPLEMENTED;
    }

    if (DriverEntry == NULL)
    {
        return STATUS_INVALID_PARAMETER_1;
    }

    return ExpWriteBootVariable(L"Driver",
                                Id,
                                DriverEntry,
                                DriverEntry->Length);
}

NTSTATUS
NTAPI
NtDeleteDriverEntry(
    _In_ ULONG Id)
{
    PAGED_CODE();

    if (!SeSinglePrivilegeCheck(SeSystemEnvironmentPrivilege,
                                ExGetPreviousMode()))
    {
        return STATUS_PRIVILEGE_NOT_HELD;
    }

    if (!ExpFirmwareHasVariables())
    {
        return STATUS_NOT_IMPLEMENTED;
    }

    return ExpWriteBootVariable(L"Driver", Id, NULL, 0);
}

NTSTATUS
NTAPI
NtModifyDriverEntry(
    _In_ PEFI_DRIVER_ENTRY DriverEntry)
{
    PAGED_CODE();

    if (DriverEntry == NULL)
    {
        return STATUS_INVALID_PARAMETER_1;
    }

    return NtAddDriverEntry(DriverEntry, DriverEntry->Id);
}

NTSTATUS
NTAPI
NtEnumerateDriverEntries(
    _Out_opt_ PVOID Buffer,
    _Inout_ PULONG BufferLength)
{
    PAGED_CODE();

    if (!SeSinglePrivilegeCheck(SeSystemEnvironmentPrivilege,
                                ExGetPreviousMode()))
    {
        return STATUS_PRIVILEGE_NOT_HELD;
    }

    if (!ExpFirmwareHasVariables())
    {
        return STATUS_NOT_IMPLEMENTED;
    }

    return ExpEnumerateEntries(L"Driver", Buffer, BufferLength);
}

NTSTATUS
NTAPI
NtQueryDriverEntryOrder(
    _Out_opt_ PULONG Ids,
    _Inout_ PULONG Count)
{
    PAGED_CODE();

    /*
     * The drivers the firmware loads are named by their own order variable,
     * which this kernel does not read, so nothing is reported rather than the
     * boot order being reported in its place.
     */
    UNREFERENCED_PARAMETER(Ids);

    if (!SeSinglePrivilegeCheck(SeSystemEnvironmentPrivilege,
                                ExGetPreviousMode()))
    {
        return STATUS_PRIVILEGE_NOT_HELD;
    }

    if (Count != NULL)
    {
        *Count = 0;
    }

    return ExpFirmwareHasVariables() ? STATUS_SUCCESS :
                                       STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
NTAPI
NtSetDriverEntryOrder(
    _In_ PULONG Ids,
    _In_ PULONG Count)
{
    PAGED_CODE();

    UNREFERENCED_PARAMETER(Ids);
    UNREFERENCED_PARAMETER(Count);

    if (!SeSinglePrivilegeCheck(SeSystemEnvironmentPrivilege,
                                ExGetPreviousMode()))
    {
        return STATUS_PRIVILEGE_NOT_HELD;
    }

    /* TODO: the order the firmware loads drivers in is not written. */
    return STATUS_NOT_IMPLEMENTED;
}

/* EOF */
