/*
 * PROJECT:     LiberNT kernel-mode tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     The kernel log reaches lastboot.log at the boot volume root
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <kmt_test.h>

#define LOG_TAIL_BYTES (256 * 1024)
#define LOG_HEAD_BYTES 4096

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
    Name->Buffer = ExAllocatePoolWithTag(PagedPool, Name->MaximumLength, 'LBmK');
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
        ExFreePoolWithTag(Name->Buffer, 'LBmK');
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
BOOLEAN
IsTimestampBefore(
    _In_reads_bytes_(Offset) PUCHAR Buffer,
    _In_ ULONG Offset)
{
    ULONG Index;

    if (Offset < sizeof("[    0.000000] ") - 1)
        return FALSE;

    Index = Offset - (sizeof("[    0.000000] ") - 1);
    if (Buffer[Index] != '[' || Buffer[Offset - 2] != ']' || Buffer[Offset - 1] != ' ' ||
        Buffer[Offset - 9] != '.')
    {
        return FALSE;
    }

    for (Index = Offset - 8; Index < Offset - 2; Index++)
    {
        if (Buffer[Index] < '0' || Buffer[Index] > '9') return FALSE;
    }

    return TRUE;
}

static
BOOLEAN
LogContains(
    _In_ PUNICODE_STRING Name,
    _In_ PCSTR Marker,
    _Out_ PNTSTATUS OpenStatus,
    _Out_ PBOOLEAN Stamped,
    _Out_ PULONG Count)
{
    FILE_STANDARD_INFORMATION Standard;
    OBJECT_ATTRIBUTES ObjectAttributes;
    IO_STATUS_BLOCK IoStatus;
    LARGE_INTEGER Offset;
    SIZE_T MarkerLength = strlen(Marker);
    BOOLEAN Found = FALSE;
    PUCHAR Buffer;
    HANDLE File;
    ULONG Length, Index;
    NTSTATUS Status;

    InitializeObjectAttributes(&ObjectAttributes, Name,
                               OBJ_KERNEL_HANDLE | OBJ_CASE_INSENSITIVE, NULL, NULL);
    *Stamped = FALSE;
    *Count = 0;
    *OpenStatus = ZwOpenFile(&File, FILE_READ_DATA | SYNCHRONIZE, &ObjectAttributes, &IoStatus,
                             FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                             FILE_NON_DIRECTORY_FILE | FILE_SYNCHRONOUS_IO_NONALERT);
    if (!NT_SUCCESS(*OpenStatus)) return FALSE;

    Status = ZwQueryInformationFile(File, &IoStatus, &Standard, sizeof(Standard),
                                    FileStandardInformation);
    Buffer = ExAllocatePoolWithTag(PagedPool, LOG_TAIL_BYTES, 'LBmK');
    if (NT_SUCCESS(Status) && Buffer != NULL)
    {
        Offset.QuadPart = (Standard.EndOfFile.QuadPart > LOG_TAIL_BYTES)
                          ? Standard.EndOfFile.QuadPart - LOG_TAIL_BYTES : 0;
        Status = ZwReadFile(File, NULL, NULL, NULL, &IoStatus, Buffer, LOG_TAIL_BYTES,
                            &Offset, NULL);
        if (NT_SUCCESS(Status))
        {
            Length = (ULONG)IoStatus.Information;
            for (Index = 0; Index + MarkerLength <= Length; Index++)
            {
                if (RtlCompareMemory(Buffer + Index, Marker, MarkerLength) != MarkerLength)
                    continue;
                if (!Found) *Stamped = IsTimestampBefore(Buffer, Index);
                Found = TRUE;
                (*Count)++;
            }
        }
    }

    if (Buffer != NULL) ExFreePoolWithTag(Buffer, 'LBmK');
    ZwClose(File);
    return Found;
}

static
ULONG
LogHeadNuls(
    _In_ PUNICODE_STRING Name)
{
    OBJECT_ATTRIBUTES ObjectAttributes;
    IO_STATUS_BLOCK IoStatus;
    LARGE_INTEGER Offset;
    UCHAR Buffer[LOG_HEAD_BYTES];
    ULONG Nuls = MAXULONG, Index;
    HANDLE File;

    InitializeObjectAttributes(&ObjectAttributes, Name,
                               OBJ_KERNEL_HANDLE | OBJ_CASE_INSENSITIVE, NULL, NULL);
    if (!NT_SUCCESS(ZwOpenFile(&File, FILE_READ_DATA | SYNCHRONIZE, &ObjectAttributes, &IoStatus,
                               FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                               FILE_NON_DIRECTORY_FILE | FILE_SYNCHRONOUS_IO_NONALERT)))
        return Nuls;

    Offset.QuadPart = 0;
    if (NT_SUCCESS(ZwReadFile(File, NULL, NULL, NULL, &IoStatus, Buffer, sizeof(Buffer), &Offset, NULL)))
    {
        for (Nuls = 0, Index = 0; Index < (ULONG)IoStatus.Information; Index++)
            Nuls += (Buffer[Index] == 0);
    }

    ZwClose(File);
    return Nuls;
}

START_TEST(KdBootLog)
{
    UNICODE_STRING Name;
    LARGE_INTEGER Interval;
    CHAR Marker[64];
    NTSTATUS Status, OpenStatus = STATUS_UNSUCCESSFUL;
    BOOLEAN Found = FALSE, Stamped = FALSE;
    ULONG Try, Count = 0;

    Status = GetBootLogName(&Name);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status)) return;

    Status = RtlStringCbPrintfA(Marker, sizeof(Marker), "KMT_BOOTLOG_%I64x",
                                KeQueryInterruptTime());
    ok_eq_hex(Status, STATUS_SUCCESS);
    DbgPrint("%s\n", Marker);

    Interval.QuadPart = -10000LL * 250;
    for (Try = 0; Try < 20 && !Found; Try++)
    {
        KeDelayExecutionThread(KernelMode, FALSE, &Interval);
        Found = LogContains(&Name, Marker, &OpenStatus, &Stamped, &Count);
    }

    ok_eq_hex(OpenStatus, STATUS_SUCCESS);
    ok(Found, "%s did not reach %wZ\n", Marker, &Name);
    ok(Stamped, "%s is not preceded by the time since boot\n", Marker);
    ok(Count == 1, "%s is in %wZ %lu times\n", Marker, &Name, Count);
    ok_eq_ulong(LogHeadNuls(&Name), 0UL);

    ExFreePoolWithTag(Name.Buffer, 'LBmK');
}
