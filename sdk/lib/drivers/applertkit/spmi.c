/*
 * PROJECT:     LiberNT Apple RTKit Library
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Apple silicon SPMI controller and PMU NVMEM cell reads
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#ifndef UNIT_TEST
#include <ntddk.h>
#endif
#include <reactos/drivers/applepmgr.h>
#include <reactos/drivers/applespmi.h>

#define SPMI_STATUS                 0x00
#define SPMI_STATUS_RX_EMPTY        0x01000000
#define SPMI_COMMAND                0x04
#define SPMI_RESPONSE               0x08
#define SPMI_IRQ_MASK_BASE          0x20
#define SPMI_IRQ_ACK_BASE           0x60
#define SPMI_IRQ_ACK_NOTIFY         0x80
#define SPMI_PERIPHERAL_IRQS        256
#define SPMI_ACTION                 0xa4
#define SPMI_ACTION_FIFO_FLUSH      0x1
#define SPMI_CMD_WAKEUP             0x13
#define SPMI_CMD_EXT_WRITEL         0x30
#define SPMI_CMD_EXT_READL          0x38
#define SPMI_CMD_READ               0x60
#define SPMI_CMD_ZERO_WRITE         0x80
#define SPMI_CMD_START              0x8000
#define SPMI_REPLY_ACK              0x8000
#define SPMI_EXT_READL_MAX          8
#define SPMI_TIMEOUT_US             50000

static
ULONG
AppleSpmiReadRegister(
    _In_ PAPPLE_SPMI Spmi,
    _In_ ULONG Offset)
{
    return READ_REGISTER_ULONG((volatile ULONG *)(Spmi->Registers + Offset));
}

static
VOID
AppleSpmiWriteRegister(
    _In_ PAPPLE_SPMI Spmi,
    _In_ ULONG Offset,
    _In_ ULONG Value)
{
    WRITE_REGISTER_ULONG((volatile ULONG *)(Spmi->Registers + Offset), Value);
}

NTSTATUS
NTAPI
AppleSpmiOpen(
    _In_ PFDT_DRIVER_TREE Tree,
    _In_ ULONG Node,
    _Out_ PAPPLE_SPMI Spmi)
{
    ULONG Length;
    const VOID *Compatible;
    NTSTATUS Status;

    RtlZeroMemory(Spmi, sizeof(*Spmi));
    Compatible = FdtGetProperty(&Tree->Fdt, Node, "compatible", &Length);
    if (!FdtStringListContains(Compatible, Length, "apple,t8103-spmi") &&
        !FdtStringListContains(Compatible, Length, "apple,spmi"))
        return STATUS_NOT_SUPPORTED;
    Status = ApplePmgrPowerOnDevice(Tree, Node);
    if (!NT_SUCCESS(Status))
        return Status;
    Spmi->Registers = FdtDriverMapReg(Tree, Node, 0, NULL, &Spmi->Size);
    if (!Spmi->Registers || Spmi->Size < SPMI_ACTION + sizeof(ULONG))
    {
        AppleSpmiClose(Spmi);
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    }
    return STATUS_SUCCESS;
}

VOID
NTAPI
AppleSpmiClose(
    _Inout_ PAPPLE_SPMI Spmi)
{
    if (Spmi->Registers)
        MmUnmapIoSpace(Spmi->Registers, Spmi->Size);
    Spmi->Registers = NULL;
}

static
NTSTATUS
AppleSpmiCommand(
    _Inout_ PAPPLE_SPMI Spmi,
    _In_ ULONG Opcode,
    _In_ UCHAR Sid,
    _In_ USHORT Address,
    _In_reads_bytes_opt_(WriteLength) const UCHAR *WriteBuffer,
    _In_ ULONG WriteLength,
    _Out_writes_bytes_opt_(ReadLength) PUCHAR ReadBuffer,
    _In_ ULONG ReadLength)
{
    ULONG Reply, Word, Waited, Done, Count;

    if (Spmi->Failed)
    {
        AppleSpmiWriteRegister(Spmi, SPMI_ACTION, SPMI_ACTION_FIFO_FLUSH);
        AppleSpmiWriteRegister(Spmi, SPMI_IRQ_ACK_NOTIFY, 1);
        Spmi->Failed = FALSE;
    }
    AppleSpmiWriteRegister(Spmi, SPMI_COMMAND, Opcode | ((ULONG)Sid << 8) | ((ULONG)Address << 16) | SPMI_CMD_START);
    for (Done = 0; Done < WriteLength; Done += Count)
    {
        Word = 0;
        Count = min(WriteLength - Done, sizeof(Word));
        RtlCopyMemory(&Word, WriteBuffer + Done, Count);
        AppleSpmiWriteRegister(Spmi, SPMI_COMMAND, Word);
    }
    for (Waited = 0; AppleSpmiReadRegister(Spmi, SPMI_STATUS) & SPMI_STATUS_RX_EMPTY; Waited += 10)
    {
        if (Waited >= SPMI_TIMEOUT_US)
        {
            Spmi->Failed = TRUE;
            return STATUS_IO_TIMEOUT;
        }
        KeStallExecutionProcessor(10);
    }
    Reply = AppleSpmiReadRegister(Spmi, SPMI_RESPONSE);
    for (Done = 0; Done < ReadLength; Done += Count)
    {
        if (AppleSpmiReadRegister(Spmi, SPMI_STATUS) & SPMI_STATUS_RX_EMPTY)
        {
            Spmi->Failed = TRUE;
            return STATUS_IO_DEVICE_ERROR;
        }
        Word = AppleSpmiReadRegister(Spmi, SPMI_RESPONSE);
        Count = min(ReadLength - Done, sizeof(Word));
        RtlCopyMemory(ReadBuffer + Done, &Word, Count);
    }
    if (!(AppleSpmiReadRegister(Spmi, SPMI_STATUS) & SPMI_STATUS_RX_EMPTY))
        Spmi->Failed = TRUE;
    if (!ReadLength && !(Reply & SPMI_REPLY_ACK))
        return STATUS_DEVICE_PROTOCOL_ERROR;
    if (~(Reply >> 16) & ((1UL << ReadLength) - 1))
        return STATUS_DEVICE_DATA_ERROR;
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
AppleSpmiRead(
    _Inout_ PAPPLE_SPMI Spmi,
    _In_ UCHAR Sid,
    _In_ USHORT Address,
    _Out_writes_bytes_(Length) PUCHAR Buffer,
    _In_ ULONG Length)
{
    ULONG Done, Count;
    NTSTATUS Status;

    if (!Spmi->Registers || !Length || (ULONG)Address + Length > 0x10000)
        return STATUS_INVALID_PARAMETER;
    for (Done = 0; Done < Length; Done += Count)
    {
        Count = min(Length - Done, SPMI_EXT_READL_MAX);
        Status = AppleSpmiCommand(Spmi, SPMI_CMD_EXT_READL | (Count - 1), Sid, (USHORT)(Address + Done), NULL, 0,
                                  Buffer + Done, Count);
        if (!NT_SUCCESS(Status))
            return Status;
    }
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
AppleSpmiWrite(
    _Inout_ PAPPLE_SPMI Spmi,
    _In_ UCHAR Sid,
    _In_ USHORT Address,
    _In_reads_bytes_(Length) const UCHAR *Buffer,
    _In_ ULONG Length)
{
    ULONG Done, Count;
    NTSTATUS Status;

    if (!Spmi->Registers || !Length || (ULONG)Address + Length > 0x10000)
        return STATUS_INVALID_PARAMETER;
    for (Done = 0; Done < Length; Done += Count)
    {
        Count = min(Length - Done, SPMI_EXT_READL_MAX);
        Status = AppleSpmiCommand(Spmi, SPMI_CMD_EXT_WRITEL | (Count - 1), Sid, (USHORT)(Address + Done), Buffer + Done,
                                  Count, NULL, 0);
        if (!NT_SUCCESS(Status))
            return Status;
    }
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
AppleSpmiRegisterRead(
    _Inout_ PAPPLE_SPMI Spmi,
    _In_ UCHAR Sid,
    _In_ UCHAR Address,
    _Out_ PUCHAR Value)
{
    if (!Spmi->Registers || Address > 0x1f)
        return STATUS_INVALID_PARAMETER;
    return AppleSpmiCommand(Spmi, SPMI_CMD_READ | Address, Sid, Address, NULL, 0, Value, 1);
}

NTSTATUS
NTAPI
AppleSpmiZeroWrite(
    _Inout_ PAPPLE_SPMI Spmi,
    _In_ UCHAR Sid,
    _In_ UCHAR Data)
{
    if (!Spmi->Registers || Data > 0x7f)
        return STATUS_INVALID_PARAMETER;
    return AppleSpmiCommand(Spmi, SPMI_CMD_ZERO_WRITE | Data, Sid, (USHORT)(Data << 8), NULL, 0, NULL, 0);
}

NTSTATUS
NTAPI
AppleSpmiWakeup(
    _Inout_ PAPPLE_SPMI Spmi,
    _In_ UCHAR Sid)
{
    if (!Spmi->Registers)
        return STATUS_INVALID_PARAMETER;
    return AppleSpmiCommand(Spmi, SPMI_CMD_WAKEUP, Sid, 0, NULL, 0, NULL, 0);
}

BOOLEAN
NTAPI
AppleSpmiInterruptPending(
    _Inout_ PAPPLE_SPMI Spmi,
    _In_ ULONG Interrupt)
{
    if (!Spmi->Registers || Interrupt >= SPMI_PERIPHERAL_IRQS)
        return FALSE;
    return (AppleSpmiReadRegister(Spmi, SPMI_IRQ_ACK_BASE + (Interrupt / 32) * sizeof(ULONG)) >> (Interrupt % 32)) & 1;
}

VOID
NTAPI
AppleSpmiEnableInterrupt(
    _Inout_ PAPPLE_SPMI Spmi,
    _In_ ULONG Interrupt)
{
    ULONG Offset = SPMI_IRQ_MASK_BASE + (Interrupt / 32) * sizeof(ULONG);

    if (Spmi->Registers && Interrupt < SPMI_PERIPHERAL_IRQS)
        AppleSpmiWriteRegister(Spmi, Offset, AppleSpmiReadRegister(Spmi, Offset) | (1UL << (Interrupt % 32)));
}

VOID
NTAPI
AppleSpmiAcknowledgeInterrupt(
    _Inout_ PAPPLE_SPMI Spmi,
    _In_ ULONG Interrupt)
{
    if (Spmi->Registers && Interrupt < SPMI_PERIPHERAL_IRQS)
        AppleSpmiWriteRegister(Spmi, SPMI_IRQ_ACK_BASE + (Interrupt / 32) * sizeof(ULONG), 1UL << (Interrupt % 32));
}

static
NTSTATUS
AppleNvmemLocateCell(
    _In_ PFDT_DRIVER_TREE Tree,
    _In_ ULONG Phandle,
    _Out_ PAPPLE_NVMEM_CELL Cell,
    _Out_ PULONG Controller,
    _Out_ PUCHAR Sid)
{
    ULONG Node, Layout, Pmic, Length, Value;
    ULONG64 Address, Size;
    const ULONG *Bits, *Reg;
    const VOID *Compatible;

    RtlZeroMemory(Cell, sizeof(*Cell));
    if ((Node = FdtDriverFindPhandle(Tree, Phandle)) == FDT_NO_NODE ||
        (Layout = FdtDriverFindParentNode(Tree, Node)) == FDT_NO_NODE)
        return STATUS_NOT_FOUND;
    Compatible = FdtGetProperty(&Tree->Fdt, Layout, "compatible", &Length);
    Pmic = FdtStringListContains(Compatible, Length, "fixed-layout") ? FdtDriverFindParentNode(Tree, Layout) : Layout;
    if (Pmic == FDT_NO_NODE || (*Controller = FdtDriverFindParentNode(Tree, Pmic)) == FDT_NO_NODE)
        return STATUS_NOT_FOUND;
    Compatible = FdtGetProperty(&Tree->Fdt, Pmic, "compatible", &Length);
    if (!FdtStringListContains(Compatible, Length, "apple,spmi-nvmem"))
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    Reg = FdtGetProperty(&Tree->Fdt, Pmic, "reg", &Length);
    Value = (Reg && Length >= sizeof(ULONG)) ? FdtReadBigEndian32(Reg) : MAXULONG;
    if (Value > 0xf || !FdtReadReg(&Tree->Fdt, Node, Layout, 0, &Address, &Size) ||
        !Size || Size > sizeof(Cell->Data) || Address + Size > 0x10000)
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    *Sid = (UCHAR)Value;
    Cell->Address = (ULONG)Address;
    Cell->Length = (ULONG)Size;
    Cell->BitCount = Cell->Length * 8;
    Bits = FdtGetProperty(&Tree->Fdt, Node, "bits", &Length);
    if (Bits && Length >= 2 * sizeof(ULONG))
    {
        Cell->BitOffset = FdtReadBigEndian32(&Bits[0]);
        Cell->BitCount = FdtReadBigEndian32(&Bits[1]);
        if (!Cell->BitCount || Cell->BitCount > 32 || Cell->BitOffset + Cell->BitCount > Cell->Length * 8)
            return STATUS_DEVICE_CONFIGURATION_ERROR;
    }
    return STATUS_SUCCESS;
}

ULONG
NTAPI
AppleNvmemCellValue(
    _In_ PAPPLE_NVMEM_CELL Cell)
{
    ULONG64 Value = 0;

    RtlCopyMemory(&Value, Cell->Data, min(Cell->Length, sizeof(Value)));
    Value >>= Cell->BitOffset;
    if (Cell->BitCount < 64)
        Value &= (1ULL << Cell->BitCount) - 1;
    return (ULONG)Value;
}

NTSTATUS
NTAPI
AppleNvmemWriteCell(
    _In_ PFDT_DRIVER_TREE Tree,
    _In_ ULONG Phandle,
    _In_ ULONG Value)
{
    APPLE_NVMEM_CELL Cell;
    APPLE_SPMI Spmi;
    ULONG Controller;
    ULONG64 Data = 0, Mask;
    UCHAR Sid;
    NTSTATUS Status;

    Status = AppleNvmemLocateCell(Tree, Phandle, &Cell, &Controller, &Sid);
    if (!NT_SUCCESS(Status))
        return Status;
    if (Cell.Length > sizeof(Data) || Cell.BitCount > 32 || (Cell.BitCount < 32 && Value >> Cell.BitCount))
        return STATUS_INVALID_PARAMETER;
    Status = AppleSpmiOpen(Tree, Controller, &Spmi);
    if (!NT_SUCCESS(Status))
        return Status;
    Status = AppleSpmiRead(&Spmi, Sid, (USHORT)Cell.Address, Cell.Data, Cell.Length);
    if (NT_SUCCESS(Status))
    {
        RtlCopyMemory(&Data, Cell.Data, Cell.Length);
        Mask = ((1ULL << Cell.BitCount) - 1) << Cell.BitOffset;
        Data = (Data & ~Mask) | (((ULONG64)Value << Cell.BitOffset) & Mask);
        RtlCopyMemory(Cell.Data, &Data, Cell.Length);
        Status = AppleSpmiWrite(&Spmi, Sid, (USHORT)Cell.Address, Cell.Data, Cell.Length);
    }
    AppleSpmiClose(&Spmi);
    return Status;
}

NTSTATUS
NTAPI
AppleNvmemReadCell(
    _In_ PFDT_DRIVER_TREE Tree,
    _In_ ULONG Phandle,
    _Out_ PAPPLE_NVMEM_CELL Cell)
{
    APPLE_SPMI Spmi;
    ULONG Controller;
    UCHAR Sid;
    NTSTATUS Status;

    Status = AppleNvmemLocateCell(Tree, Phandle, Cell, &Controller, &Sid);
    if (!NT_SUCCESS(Status))
        return Status;
    Status = AppleSpmiOpen(Tree, Controller, &Spmi);
    if (!NT_SUCCESS(Status))
        return Status;
    Status = AppleSpmiRead(&Spmi, Sid, (USHORT)Cell->Address, Cell->Data, Cell->Length);
    AppleSpmiClose(&Spmi);
    return Status;
}
