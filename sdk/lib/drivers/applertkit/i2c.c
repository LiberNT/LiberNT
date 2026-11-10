/*
 * PROJECT:     LiberNT Apple RTKit Library
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Apple silicon I2C controller
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#ifndef UNIT_TEST
#include <ntddk.h>
#endif
#include <reactos/drivers/applepmgr.h>
#include <reactos/drivers/applegpio.h>
#include <reactos/drivers/applei2c.h>

#define I2C_TX_FIFO             0x00
#define I2C_RX_FIFO             0x04
#define I2C_STATUS              0x14
#define I2C_INTERRUPT_MASK      0x18
#define I2C_CONTROL             0x1c
#define I2C_REVISION            0x28
#define I2C_REGISTER_SPAN       0x2c

#define I2C_TX_READ             0x00000400
#define I2C_TX_STOP             0x00000200
#define I2C_TX_START            0x00000100
#define I2C_RX_EMPTY            0x00000100

#define I2C_STATUS_BUSY         0x10000000
#define I2C_STATUS_DONE         0x08000000
#define I2C_STATUS_JAM_DONE     0x02000000
#define I2C_STATUS_JAM          0x01000000
#define I2C_STATUS_TIMEOUT      0x00800000
#define I2C_STATUS_LOST         0x00400000
#define I2C_STATUS_NACK         0x00200000
#define I2C_STATUS_RX_DATA      0x00080000
#define I2C_STATUS_TX_EMPTY     0x00010000
#define I2C_STATUS_CONTROLLER_TIMEOUT 0x00000040

#define I2C_CONTROL_ENABLE      0x00000800
#define I2C_CONTROL_RX_RESET    0x00000400
#define I2C_CONTROL_TX_RESET    0x00000200
#define I2C_CONTROL_UNJAM       0x00000100
#define I2C_CONTROL_DIVIDER     0x000000ff

#define I2C_DEFAULT_FREQUENCY   100000
#define I2C_TIMEOUT_US          100000
#define I2C_POLL_US             10

static
ULONG
AppleI2cRead(
    _In_ PAPPLE_I2C I2c,
    _In_ ULONG Offset)
{
    return READ_REGISTER_ULONG((volatile ULONG *)(I2c->Registers + Offset));
}

static
VOID
AppleI2cWrite(
    _In_ PAPPLE_I2C I2c,
    _In_ ULONG Offset,
    _In_ ULONG Value)
{
    WRITE_REGISTER_ULONG((volatile ULONG *)(I2c->Registers + Offset), Value);
}

static
VOID
AppleI2cReset(
    _In_ PAPPLE_I2C I2c)
{
    AppleI2cWrite(I2c, I2C_CONTROL, I2c->Control);
}

static
NTSTATUS
AppleI2cClear(
    _In_ PAPPLE_I2C I2c)
{
    ULONG Status = 0, Waited;

    for (Waited = 0; Waited <= I2C_TIMEOUT_US; Waited += I2C_POLL_US)
    {
        Status = AppleI2cRead(I2c, I2C_STATUS);
        if (!(Status & (I2C_STATUS_BUSY | I2C_STATUS_JAM)))
            break;
        KeStallExecutionProcessor(I2C_POLL_US);
    }
    if (Waited > I2C_TIMEOUT_US)
        return STATUS_DEVICE_BUSY;
    if ((Status & (I2C_STATUS_RX_DATA | I2C_STATUS_JAM_DONE | I2C_STATUS_TIMEOUT | I2C_STATUS_CONTROLLER_TIMEOUT |
                   I2C_STATUS_NACK | I2C_STATUS_LOST)) ||
        !(Status & I2C_STATUS_TX_EMPTY))
        AppleI2cReset(I2c);
    AppleI2cWrite(I2c, I2C_STATUS, Status);
    return STATUS_SUCCESS;
}

static
NTSTATUS
AppleI2cWaitDone(
    _In_ PAPPLE_I2C I2c)
{
    ULONG Status = 0, Waited;

    for (Waited = 0; Waited <= I2C_TIMEOUT_US; Waited += I2C_POLL_US)
    {
        Status = AppleI2cRead(I2c, I2C_STATUS);
        if (Status & I2C_STATUS_DONE)
            break;
        KeStallExecutionProcessor(I2C_POLL_US);
    }
    if (Waited > I2C_TIMEOUT_US || (Status & I2C_STATUS_TIMEOUT))
        return STATUS_IO_TIMEOUT;
    if (Status & I2C_STATUS_CONTROLLER_TIMEOUT)
        return STATUS_IO_DEVICE_ERROR;
    if (Status & (I2C_STATUS_BUSY | I2C_STATUS_LOST))
        return STATUS_DEVICE_BUSY;
    if (Status & I2C_STATUS_NACK)
        return STATUS_NO_SUCH_DEVICE;
    AppleI2cWrite(I2c, I2C_STATUS, I2C_STATUS_DONE);
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
AppleI2cOpen(
    _In_ PFDT_DRIVER_TREE Tree,
    _In_ ULONG Node,
    _Out_ PAPPLE_I2C I2c)
{
    const VOID *Compatible, *Clocks;
    ULONG Length, Clock, Reference, Frequency, Divider;
    ULONG64 Phandle;
    NTSTATUS Status;

    RtlZeroMemory(I2c, sizeof(*I2c));
    Compatible = FdtGetProperty(&Tree->Fdt, Node, "compatible", &Length);
    if (!FdtStringListContains(Compatible, Length, "apple,t8103-i2c") &&
        !FdtStringListContains(Compatible, Length, "apple,i2c"))
        return STATUS_NOT_SUPPORTED;
    Clocks = FdtGetProperty(&Tree->Fdt, Node, "clocks", &Length);
    if (!Clocks || !FdtReadCells(Clocks, Length, 0, 1, &Phandle) ||
        (Clock = FdtDriverFindPhandle(Tree, (ULONG)Phandle)) == FDT_NO_NODE ||
        !FdtReadU32(&Tree->Fdt, Clock, "clock-frequency", &Reference) || !Reference)
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    if (!FdtReadU32(&Tree->Fdt, Node, "clock-frequency", &Frequency) || !Frequency)
        Frequency = I2C_DEFAULT_FREQUENCY;
    Divider = (Reference + 16 * Frequency - 1) / (16 * Frequency);
    if (Divider < 4 || Divider > I2C_CONTROL_DIVIDER)
        return STATUS_DEVICE_CONFIGURATION_ERROR;

    Status = ApplePmgrPowerOnDevice(Tree, Node);
    if (!NT_SUCCESS(Status))
        return Status;
    Status = AppleGpioApplyPinctrl(Tree, Node);
    if (!NT_SUCCESS(Status))
        return Status;
    I2c->Registers = FdtDriverMapReg(Tree, Node, 0, NULL, &I2c->Size);
    if (!I2c->Registers)
        return STATUS_INSUFFICIENT_RESOURCES;
    if (I2c->Size < I2C_REGISTER_SPAN)
    {
        AppleI2cClose(I2c);
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    }
    I2c->Control = I2C_CONTROL_TX_RESET | I2C_CONTROL_RX_RESET | I2C_CONTROL_UNJAM | Divider;
    if (AppleI2cRead(I2c, I2C_REVISION) >= 6)
        I2c->Control |= I2C_CONTROL_ENABLE;
    AppleI2cWrite(I2c, I2C_INTERRUPT_MASK, 0);
    AppleI2cReset(I2c);
    return STATUS_SUCCESS;
}

VOID
NTAPI
AppleI2cClose(
    _Inout_ PAPPLE_I2C I2c)
{
    if (I2c->Registers)
        MmUnmapIoSpace(I2c->Registers, I2c->Size);
    RtlZeroMemory(I2c, sizeof(*I2c));
}

NTSTATUS
NTAPI
AppleI2cTransfer(
    _Inout_ PAPPLE_I2C I2c,
    _In_ UCHAR Address,
    _In_reads_bytes_opt_(WriteLength) const UCHAR *Write,
    _In_ ULONG WriteLength,
    _Out_writes_bytes_opt_(ReadLength) PUCHAR Read,
    _In_ ULONG ReadLength)
{
    ULONG Index, Value;
    NTSTATUS Status;

    if (!I2c->Registers || Address > 0x7f || (!WriteLength && !ReadLength) ||
        (WriteLength && !Write) || (ReadLength && !Read) || ReadLength > 0xff)
        return STATUS_INVALID_PARAMETER;
    Status = AppleI2cClear(I2c);
    if (!NT_SUCCESS(Status))
        return Status;

    if (WriteLength)
    {
        AppleI2cWrite(I2c, I2C_TX_FIFO, I2C_TX_START | ((ULONG)Address << 1));
        for (Index = 0; Index < WriteLength; ++Index)
        {
            Value = Write[Index];
            if (Index + 1 == WriteLength && !ReadLength)
                Value |= I2C_TX_STOP;
            AppleI2cWrite(I2c, I2C_TX_FIFO, Value);
        }
        if (!ReadLength)
        {
            Status = AppleI2cWaitDone(I2c);
            if (!NT_SUCCESS(Status))
                AppleI2cReset(I2c);
            return Status;
        }
    }

    AppleI2cWrite(I2c, I2C_TX_FIFO, I2C_TX_START | ((ULONG)Address << 1) | 1);
    AppleI2cWrite(I2c, I2C_TX_FIFO, ReadLength | I2C_TX_READ | I2C_TX_STOP);
    Status = AppleI2cWaitDone(I2c);
    for (Index = 0; NT_SUCCESS(Status) && Index < ReadLength; ++Index)
    {
        Value = AppleI2cRead(I2c, I2C_RX_FIFO);
        if (Value & I2C_RX_EMPTY)
            Status = STATUS_DEVICE_DATA_ERROR;
        else
            Read[Index] = (UCHAR)Value;
    }
    if (!NT_SUCCESS(Status))
        AppleI2cReset(I2c);
    return Status;
}
