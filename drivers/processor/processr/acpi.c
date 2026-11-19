/*
 * PROJECT:     LiberNT Processor Driver
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     ACPI method evaluation and generic register access
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "processr.h"

#define NDEBUG
#include <debug.h>

#define PROC_GAS_DESCRIPTOR         0x82
#define PROC_GAS_MINIMUM_LENGTH     12

static
NTSTATUS
ProcSendEvaluate(
    _In_ PPROC_DEVICE Device,
    _In_reads_bytes_(InputLength) PVOID Input,
    _In_ ULONG InputLength,
    _Out_writes_bytes_(OutputLength) PACPI_EVAL_OUTPUT_BUFFER Output,
    _In_ ULONG OutputLength,
    _Out_ PULONG_PTR Information)
{
    IO_STATUS_BLOCK IoStatus;
    KEVENT Event;
    NTSTATUS Status;
    PIRP Irp;

    PAGED_CODE();

    *Information = 0;
    KeInitializeEvent(&Event, NotificationEvent, FALSE);
    Irp = IoBuildDeviceIoControlRequest(IOCTL_ACPI_EVAL_METHOD,
                                        Device->Lower,
                                        Input,
                                        InputLength,
                                        Output,
                                        OutputLength,
                                        FALSE,
                                        &Event,
                                        &IoStatus);
    if (Irp == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;

    Status = IoCallDriver(Device->Lower, Irp);
    if (Status == STATUS_PENDING)
    {
        KeWaitForSingleObject(&Event, Executive, KernelMode, FALSE, NULL);
        Status = IoStatus.Status;
    }

    *Information = IoStatus.Information;
    return Status;
}

/* The buffer grows to the size the ACPI driver reports, whichever of the two places it reports it in */
static
NTSTATUS
ProcEvaluateBuffer(
    _In_ PPROC_DEVICE Device,
    _In_reads_bytes_(InputLength) PVOID Input,
    _In_ ULONG InputLength,
    _Outptr_ PACPI_EVAL_OUTPUT_BUFFER *Output)
{
    PACPI_EVAL_OUTPUT_BUFFER Buffer;
    ULONG Length = PAGE_SIZE;
    ULONG_PTR Information;
    ULONG Needed;
    NTSTATUS Status;

    PAGED_CODE();

    *Output = NULL;

    for (;;)
    {
        Buffer = ExAllocatePoolZero(PagedPool, Length, PROC_TAG);
        if (Buffer == NULL)
            return STATUS_INSUFFICIENT_RESOURCES;

        Status = ProcSendEvaluate(Device, Input, InputLength, Buffer, Length, &Information);
        if ((Status != STATUS_BUFFER_OVERFLOW) && (Status != STATUS_BUFFER_TOO_SMALL))
            break;

        Needed = (ULONG)min(Information, (ULONG_PTR)MAXULONG);
        if (Buffer->Signature == ACPI_EVAL_OUTPUT_BUFFER_SIGNATURE)
            Needed = max(Needed, Buffer->Length);

        ExFreePoolWithTag(Buffer, PROC_TAG);
        if ((Needed <= Length) || (Needed > PROC_MAX_ACPI_OUTPUT))
            return STATUS_ACPI_INVALID_DATA;

        Length = Needed;
    }

    if (!NT_SUCCESS(Status))
    {
        ExFreePoolWithTag(Buffer, PROC_TAG);
        return Status;
    }

    if ((Buffer->Signature != ACPI_EVAL_OUTPUT_BUFFER_SIGNATURE) ||
        (Buffer->Length < FIELD_OFFSET(ACPI_EVAL_OUTPUT_BUFFER, Argument)) ||
        (Buffer->Length > Length))
    {
        ExFreePoolWithTag(Buffer, PROC_TAG);
        return STATUS_ACPI_INVALID_DATA;
    }

    *Output = Buffer;
    return STATUS_SUCCESS;
}

NTSTATUS
ProcEvaluate(
    _In_ PPROC_DEVICE Device,
    _In_ ULONG Method,
    _Outptr_ PACPI_EVAL_OUTPUT_BUFFER *Output)
{
    ACPI_EVAL_INPUT_BUFFER Input;

    PAGED_CODE();

    RtlZeroMemory(&Input, sizeof(Input));
    Input.Signature = ACPI_EVAL_INPUT_BUFFER_SIGNATURE;
    Input.MethodNameAsUlong = Method;

    return ProcEvaluateBuffer(Device, &Input, sizeof(Input), Output);
}

NTSTATUS
ProcEvaluateComplex(
    _In_ PPROC_DEVICE Device,
    _In_ PACPI_EVAL_INPUT_BUFFER_COMPLEX Input,
    _In_ ULONG InputLength,
    _Outptr_ PACPI_EVAL_OUTPUT_BUFFER *Output)
{
    PAGED_CODE();

    return ProcEvaluateBuffer(Device, Input, InputLength, Output);
}

static
BOOLEAN
ProcArgumentFits(
    _In_ PACPI_METHOD_ARGUMENT Argument,
    _In_ PUCHAR End)
{
    if (((PUCHAR)Argument > End) || ((ULONG_PTR)(End - (PUCHAR)Argument) < ACPI_METHOD_ARGUMENT_LENGTH(0)))
        return FALSE;

    return ACPI_METHOD_ARGUMENT_LENGTH_FROM_ARGUMENT(Argument) <= (ULONG_PTR)(End - (PUCHAR)Argument);
}

PACPI_METHOD_ARGUMENT
ProcArgument(
    _In_ PACPI_EVAL_OUTPUT_BUFFER Output,
    _In_ ULONG Index)
{
    PACPI_METHOD_ARGUMENT Argument = &Output->Argument[0];
    PUCHAR End = (PUCHAR)Output + Output->Length;
    ULONG Current;

    if (Index >= Output->Count)
        return NULL;

    for (Current = 0; ; Current++)
    {
        if (!ProcArgumentFits(Argument, End))
            return NULL;

        if (Current == Index)
            return Argument;

        Argument = ACPI_METHOD_NEXT_ARGUMENT(Argument);
    }
}

PACPI_METHOD_ARGUMENT
ProcPackageElement(
    _In_opt_ PACPI_METHOD_ARGUMENT Package,
    _In_ ULONG Index)
{
    PACPI_METHOD_ARGUMENT Argument;
    PUCHAR End;
    ULONG Current;

    if ((Package == NULL) || (Package->Type != ACPI_METHOD_ARGUMENT_PACKAGE))
        return NULL;

    Argument = (PACPI_METHOD_ARGUMENT)Package->Data;
    End = Package->Data + Package->DataLength;

    for (Current = 0; ProcArgumentFits(Argument, End); Current++)
    {
        if (Current == Index)
            return Argument;

        Argument = ACPI_METHOD_NEXT_ARGUMENT(Argument);
    }

    return NULL;
}

ULONG
ProcPackageCount(
    _In_opt_ PACPI_METHOD_ARGUMENT Package)
{
    ULONG Count = 0;

    while (ProcPackageElement(Package, Count) != NULL)
        Count++;

    return Count;
}

BOOLEAN
ProcInteger(
    _In_opt_ PACPI_METHOD_ARGUMENT Argument,
    _Out_ PULONG Value)
{
    if ((Argument == NULL) || (Argument->Type != ACPI_METHOD_ARGUMENT_INTEGER))
        return FALSE;

    *Value = Argument->Argument;
    return TRUE;
}

BOOLEAN
ProcQueryInteger(
    _In_ PPROC_DEVICE Device,
    _In_ ULONG Method,
    _Out_ PULONG Value)
{
    PACPI_EVAL_OUTPUT_BUFFER Output;
    BOOLEAN Found;

    PAGED_CODE();

    if (!NT_SUCCESS(ProcEvaluate(Device, Method, &Output)))
        return FALSE;

    Found = ProcInteger(ProcArgument(Output, 0), Value);
    ExFreePoolWithTag(Output, PROC_TAG);
    return Found;
}

/* A Generic Register Descriptor (ACPI 6.5, 6.4.3.7) */
BOOLEAN
ProcParseRegister(
    _In_opt_ PACPI_METHOD_ARGUMENT Argument,
    _Out_ PPROC_REGISTER Register)
{
    USHORT Length;

    RtlZeroMemory(Register, sizeof(*Register));

    if ((Argument == NULL) ||
        (Argument->Type != ACPI_METHOD_ARGUMENT_BUFFER) ||
        (Argument->DataLength < 3 + PROC_GAS_MINIMUM_LENGTH) ||
        (Argument->Data[0] != PROC_GAS_DESCRIPTOR))
    {
        return FALSE;
    }

    RtlCopyMemory(&Length, &Argument->Data[1], sizeof(Length));
    if ((Length < PROC_GAS_MINIMUM_LENGTH) || ((ULONG)Length + 3 > Argument->DataLength))
        return FALSE;

    Register->SpaceId = Argument->Data[3];
    Register->BitWidth = Argument->Data[4];
    Register->BitOffset = Argument->Data[5];
    Register->AccessSize = Argument->Data[6];
    RtlCopyMemory(&Register->Address, &Argument->Data[7], sizeof(Register->Address));

    if ((Register->SpaceId != PROC_SPACE_FIXED_HARDWARE) &&
        ((Register->BitWidth == 0) || (Register->BitWidth > 64) || (Register->BitOffset >= 64) ||
         ((ULONG)Register->BitWidth + Register->BitOffset > 64)))
    {
        return FALSE;
    }

    Register->Valid = TRUE;
    return TRUE;
}

static
ULONG
ProcRegisterBytes(
    _In_ PPROC_REGISTER Register)
{
    ULONG Bytes;

    if ((Register->AccessSize >= 1) && (Register->AccessSize <= 4))
        Bytes = 1UL << (Register->AccessSize - 1);
    else
        Bytes = ((ULONG)Register->BitWidth + Register->BitOffset + 7) / 8;

    if (Bytes <= 1)
        return 1;
    if (Bytes <= 2)
        return 2;
    if (Bytes <= 4)
        return 4;
    return 8;
}

BOOLEAN
ProcPrepareRegister(
    _Inout_ PPROC_REGISTER Register)
{
    PHYSICAL_ADDRESS Physical;
    ULONGLONG Offset;
    ULONG Bytes;

    PAGED_CODE();

    if (!Register->Valid)
        return FALSE;

    Bytes = ProcRegisterBytes(Register);

    switch (Register->SpaceId)
    {
        case PROC_SPACE_SYSTEM_MEMORY:
            Offset = Register->Address & (PAGE_SIZE - 1);
            Register->MappingLength = (SIZE_T)ROUND_TO_PAGES(Offset + Bytes);
            Physical.QuadPart = (LONGLONG)(Register->Address - Offset);
            Register->MappingBase = MmMapIoSpace(Physical, Register->MappingLength, MmNonCached);
            if (Register->MappingBase == NULL)
                break;

            Register->Mapped = (PUCHAR)Register->MappingBase + (SIZE_T)Offset;
            return TRUE;

        case PROC_SPACE_SYSTEM_IO:
            if ((Register->Address > MAXUSHORT) || (Bytes > sizeof(ULONG)))
                break;
            return TRUE;

        case PROC_SPACE_FIXED_HARDWARE:
            return TRUE;

        case PROC_SPACE_PCC:
            if (ProcPccPrepare(Register))
                return TRUE;
            break;

        default:
            break;
    }

    RtlZeroMemory(Register, sizeof(*Register));
    return FALSE;
}

VOID
ProcReleaseRegister(
    _Inout_ PPROC_REGISTER Register)
{
    if ((Register->SpaceId == PROC_SPACE_PCC) && (Register->Mapped != NULL))
        ProcPccRelease(Register);

    if (Register->MappingBase != NULL)
        MmUnmapIoSpace(Register->MappingBase, Register->MappingLength);

    RtlZeroMemory(Register, sizeof(*Register));
}

static
ULONGLONG
ProcRegisterMask(
    _In_ PPROC_REGISTER Register)
{
    return (Register->BitWidth >= 64) ? MAXULONGLONG : ((1ULL << Register->BitWidth) - 1);
}

static
BOOLEAN
ProcReadRaw(
    _In_ PPROC_REGISTER Register,
    _Out_ PULONGLONG Value)
{
    ULONG Bytes = ProcRegisterBytes(Register);

    switch (Register->SpaceId)
    {
        case PROC_SPACE_SYSTEM_MEMORY:
            if (Register->Mapped == NULL)
                return FALSE;
            if (Bytes == 1)
                *Value = READ_REGISTER_UCHAR(Register->Mapped);
            else if (Bytes == 2)
                *Value = READ_REGISTER_USHORT(Register->Mapped);
            else if (Bytes == 4)
                *Value = READ_REGISTER_ULONG(Register->Mapped);
            else
            {
                KeMemoryBarrier();
                *Value = *(volatile ULONG64 *)Register->Mapped;
                KeMemoryBarrier();
            }
            return TRUE;

        case PROC_SPACE_SYSTEM_IO:
            if (Bytes == 1)
                *Value = READ_PORT_UCHAR((PUCHAR)(ULONG_PTR)Register->Address);
            else if (Bytes == 2)
                *Value = READ_PORT_USHORT((PUSHORT)(ULONG_PTR)Register->Address);
            else
                *Value = READ_PORT_ULONG((PULONG)(ULONG_PTR)Register->Address);
            return TRUE;

        case PROC_SPACE_FIXED_HARDWARE:
            return ProcArchReadFfh(Register->Address, Value);

        default:
            return FALSE;
    }
}

static
BOOLEAN
ProcWriteRaw(
    _In_ PPROC_REGISTER Register,
    _In_ ULONGLONG Value)
{
    ULONG Bytes = ProcRegisterBytes(Register);

    switch (Register->SpaceId)
    {
        case PROC_SPACE_SYSTEM_MEMORY:
            if (Register->Mapped == NULL)
                return FALSE;
            if (Bytes == 1)
                WRITE_REGISTER_UCHAR(Register->Mapped, (UCHAR)Value);
            else if (Bytes == 2)
                WRITE_REGISTER_USHORT(Register->Mapped, (USHORT)Value);
            else if (Bytes == 4)
                WRITE_REGISTER_ULONG(Register->Mapped, (ULONG)Value);
            else
            {
                KeMemoryBarrier();
                *(volatile ULONG64 *)Register->Mapped = Value;
                KeMemoryBarrier();
            }
            return TRUE;

        case PROC_SPACE_SYSTEM_IO:
            if (Bytes == 1)
                WRITE_PORT_UCHAR((PUCHAR)(ULONG_PTR)Register->Address, (UCHAR)Value);
            else if (Bytes == 2)
                WRITE_PORT_USHORT((PUSHORT)(ULONG_PTR)Register->Address, (USHORT)Value);
            else
                WRITE_PORT_ULONG((PULONG)(ULONG_PTR)Register->Address, (ULONG)Value);
            return TRUE;

        case PROC_SPACE_FIXED_HARDWARE:
            return ProcArchWriteFfh(Register->Address, Value);

        default:
            return FALSE;
    }
}

BOOLEAN
ProcReadRegister(
    _In_ PPROC_REGISTER Register,
    _Out_ PULONGLONG Value)
{
    ULONGLONG Raw;

    if (Register->Valid && (Register->SpaceId == PROC_SPACE_PCC))
        return ProcPccRead(Register, Value);

    if (!Register->Valid || !ProcReadRaw(Register, &Raw))
        return FALSE;

    *Value = (Raw >> Register->BitOffset) & ProcRegisterMask(Register);
    return TRUE;
}

BOOLEAN
ProcWriteRegister(
    _In_ PPROC_REGISTER Register,
    _In_ ULONGLONG Value)
{
    ULONGLONG Mask = ProcRegisterMask(Register);
    ULONGLONG Raw = Value;

    if (!Register->Valid)
        return FALSE;

    if (Register->SpaceId == PROC_SPACE_PCC)
        return ProcPccWrite(1, &Register, &Value);

    if ((Register->BitOffset != 0) || (Register->BitWidth < ProcRegisterBytes(Register) * 8))
    {
        if (!ProcReadRaw(Register, &Raw))
            return FALSE;

        Raw &= ~(Mask << Register->BitOffset);
        Raw |= (Value & Mask) << Register->BitOffset;
    }

    return ProcWriteRaw(Register, Raw);
}

/* Entering an ACPI idle state through a register is a read of it */
VOID
ProcTouchRegister(
    _In_ PPROC_REGISTER Register)
{
    ULONGLONG Ignored;

    (VOID)ProcReadRaw(Register, &Ignored);
}
