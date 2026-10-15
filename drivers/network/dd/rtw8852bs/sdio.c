/*
 * PROJECT:     LiberNT Realtek RTL8852BS Native 802.11 Miniport
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     SDIO transport and register access
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 *              Copyright (c) 2019-2024 Realtek Corporation (BSD-3-Clause, see COPYING.rtw89)
 */

#include "rtw8852bs.h"

#define NDEBUG
#include <debug.h>

PVOID
RtwAllocate(
    _In_ SIZE_T Size)
{
    PVOID Buffer = ExAllocatePoolWithTag(NonPagedPool, Size, RTW_TAG);

    if (Buffer)
        RtlZeroMemory(Buffer, Size);
    return Buffer;
}

VOID
RtwFree(
    _In_opt_ PVOID Buffer)
{
    if (Buffer)
        ExFreePoolWithTag(Buffer, RTW_TAG);
}

VOID
RtwSleep(
    _In_ ULONG Milliseconds)
{
    LARGE_INTEGER Interval;

    Interval.QuadPart = -(LONGLONG)Milliseconds * 10000;
    KeDelayExecutionThread(KernelMode, FALSE, &Interval);
}

VOID
RtwStall(
    _In_ ULONG Microseconds)
{
    if (Microseconds >= 1000)
        RtwSleep((Microseconds + 999) / 1000);
    else
        KeStallExecutionProcessor(Microseconds);
}

static
NTSTATUS
RtwSdioCmd52(
    _In_ PRTW_ADAPTER Adapter,
    _In_ BOOLEAN Write,
    _In_ ULONG Address,
    _In_ UCHAR DataIn,
    _Out_opt_ PUCHAR DataOut)
{
    SDBUS_REQUEST_PACKET Packet;
    SD_RW_DIRECT_ARGUMENT Argument;
    NTSTATUS Status;

    Argument.u.AsULONG = 0;
    Argument.u.bits.Function = RTW_SDIO_FUNCTION;
    Argument.u.bits.Address = Address & RTW_SDIO_MAX_ADDRESS;
    Argument.u.bits.WriteToDevice = Write ? 1 : 0;
    Argument.u.bits.Data = Write ? DataIn : 0;

    SD_INIT_REQUEST_PACKET(&Packet, SDRF_DEVICE_COMMAND);
    Packet.Parameters.DeviceCommand.CmdDesc.Cmd = SDCMD_IO_RW_DIRECT;
    Packet.Parameters.DeviceCommand.CmdDesc.CmdClass = SDCC_STANDARD;
    Packet.Parameters.DeviceCommand.CmdDesc.TransferDirection = Write ? SDTD_WRITE : SDTD_READ;
    Packet.Parameters.DeviceCommand.CmdDesc.TransferType = SDTT_CMD_ONLY;
    Packet.Parameters.DeviceCommand.CmdDesc.ResponseType = SDRT_5;
    Packet.Parameters.DeviceCommand.Argument = Argument.u.AsULONG;

    Status = SdBusSubmitRequest(Adapter->SdBus.Context, &Packet);
    if (!NT_SUCCESS(Status))
    {
        if (!Adapter->IoError)
            DPRINT1("RTW8852BS: CMD52 %s 0x%05lx failed 0x%08lx\n", Write ? "write" : "read", Address, Status);
        Adapter->IoError = TRUE;
        if (DataOut)
            *DataOut = 0xFF;
        return Status;
    }

    if (DataOut)
        *DataOut = (UCHAR)Packet.ResponseData.AsULONG[0];
    return STATUS_SUCCESS;
}

NTSTATUS
RtwSdioTransfer(
    _In_ PRTW_ADAPTER Adapter,
    _In_ BOOLEAN Write,
    _In_ ULONG Address,
    _In_ PMDL Mdl,
    _In_ ULONG Length)
{
    SDBUS_REQUEST_PACKET Packet;
    SD_RW_EXTENDED_ARGUMENT Argument;
    NTSTATUS Status;
    BOOLEAN BlockMode;

    if (Length == 0 || Length - 1 > RTW_SDIO_MAX_ADDRESS - (Address & RTW_SDIO_MAX_ADDRESS))
        return STATUS_INVALID_PARAMETER;

    BlockMode = Length > RTW_SDIO_BYTE_MODE_MAX;
    if (BlockMode && (Length % RTW_SDIO_BLOCK_SIZE) != 0)
        return STATUS_INVALID_PARAMETER;

    Argument.u.AsULONG = 0;
    Argument.u.bits.Function = RTW_SDIO_FUNCTION;
    Argument.u.bits.Address = Address & RTW_SDIO_MAX_ADDRESS;
    Argument.u.bits.OpCode = 1;
    Argument.u.bits.BlockMode = BlockMode ? 1 : 0;
    Argument.u.bits.WriteToDevice = Write ? 1 : 0;
    Argument.u.bits.Count = BlockMode ? Length / RTW_SDIO_BLOCK_SIZE : (Length & 0x1FF);

    SD_INIT_REQUEST_PACKET(&Packet, SDRF_DEVICE_COMMAND);
    Packet.Parameters.DeviceCommand.CmdDesc.Cmd = SDCMD_IO_RW_EXTENDED;
    Packet.Parameters.DeviceCommand.CmdDesc.CmdClass = SDCC_STANDARD;
    Packet.Parameters.DeviceCommand.CmdDesc.TransferDirection = Write ? SDTD_WRITE : SDTD_READ;
    Packet.Parameters.DeviceCommand.CmdDesc.TransferType = BlockMode ? SDTT_MULTI_BLOCK_NO_CMD12 : SDTT_SINGLE_BLOCK;
    Packet.Parameters.DeviceCommand.CmdDesc.ResponseType = SDRT_5;
    Packet.Parameters.DeviceCommand.Argument = Argument.u.AsULONG;
    Packet.Parameters.DeviceCommand.Mdl = Mdl;
    Packet.Parameters.DeviceCommand.Length = Length;

    Status = SdBusSubmitRequest(Adapter->SdBus.Context, &Packet);
    if (!NT_SUCCESS(Status))
    {
        if (!Adapter->IoError)
            DPRINT1("RTW8852BS: CMD53 %s 0x%05lx len %lu failed 0x%08lx\n", Write ? "write" : "read", Address, Length, Status);
        Adapter->IoError = TRUE;
    }
    return Status;
}

NTSTATUS
RtwSdioWritePacket(
    _In_ PRTW_ADAPTER Adapter,
    _In_ UCHAR Channel,
    _In_ ULONG Length)
{
    ULONG Address, Aligned, Blocks, Offset;
    NTSTATUS Status;

    Aligned = (Length + 7) & ~7UL;
    if (Length == 0 || Aligned > RTW_TX_BUFFER_SIZE)
        return STATUS_INVALID_PARAMETER;
    RtlZeroMemory(Adapter->TxBuffer + Length, Aligned - Length);

    Address = RTW_SDIO_TX_BASE |
              ((ULONG)Channel << RTW_SDIO_TX_CHANNEL_SHIFT) |
              ((Aligned / 8) & RTW_SDIO_TX_LENGTH_MASK);

    Blocks = Aligned > RTW_SDIO_BYTE_MODE_MAX ? Aligned / RTW_SDIO_BLOCK_SIZE : 0;
    Offset = 0;
    if (Blocks)
    {
        Offset = Blocks * RTW_SDIO_BLOCK_SIZE;
        MmInitializeMdl(Adapter->TxMdl, Adapter->TxBuffer, Offset);
        MmBuildMdlForNonPagedPool(Adapter->TxMdl);
        Status = RtwSdioTransfer(Adapter, TRUE, Address, Adapter->TxMdl, Offset);
        if (!NT_SUCCESS(Status))
            return Status;
    }

    if (Offset < Aligned)
    {
        MmInitializeMdl(Adapter->TxMdl, Adapter->TxBuffer + Offset, Aligned - Offset);
        MmBuildMdlForNonPagedPool(Adapter->TxMdl);
        Status = RtwSdioTransfer(Adapter, TRUE, Address + Offset, Adapter->TxMdl, Aligned - Offset);
        if (!NT_SUCCESS(Status))
            return Status;
    }

    return STATUS_SUCCESS;
}

static
NTSTATUS
RtwSdioScratchIo(
    _In_ PRTW_ADAPTER Adapter,
    _In_ BOOLEAN Write,
    _In_ ULONG Address,
    _Inout_updates_bytes_(Length) PUCHAR Data,
    _In_ ULONG Length)
{
    NTSTATUS Status;

    if (Length > RTW_SCRATCH_SIZE)
        return STATUS_INVALID_PARAMETER;

    if (Write)
        RtlCopyMemory(Adapter->Scratch, Data, Length);
    else
        RtlFillMemory(Adapter->Scratch, Length, 0xFF);

    MmInitializeMdl(Adapter->ScratchMdl, Adapter->Scratch, Length);
    MmBuildMdlForNonPagedPool(Adapter->ScratchMdl);
    Status = RtwSdioTransfer(Adapter, Write, Address, Adapter->ScratchMdl, Length);

    if (!Write)
        RtlCopyMemory(Data, Adapter->Scratch, Length);
    return Status;
}

static
BOOLEAN
RtwIsLocal(
    _In_ ULONG Address)
{
    return Address >= RTW_SDIO_LOCAL_START && Address < RTW_SDIO_LOCAL_END;
}

static
BOOLEAN
RtwIsCmac(
    _In_ ULONG Address)
{
    return Address >= R_AX_CMAC_FUNC_EN && Address <= R_AX_CMAC_REG_END;
}

static
VOID
RtwWaitIndirect(
    _In_ PRTW_ADAPTER Adapter)
{
    UCHAR Value = 0;
    ULONG Poll;

    for (Poll = 0; Poll < RTW_SDIO_INDIRECT_POLLS; Poll++)
    {
        if (!NT_SUCCESS(RtwSdioCmd52(Adapter, FALSE, R_AX_SDIO_INDIRECT_ADDR + 3, 0, &Value)))
            return;
        if (Value & 0x80)
            return;
    }

    DPRINT1("RTW8852BS: indirect access timed out\n");
}

static
VOID
RtwIndirectSetup52(
    _In_ PRTW_ADAPTER Adapter,
    _In_ ULONG Address)
{
    ULONG Word = Address | B_AX_INDIRECT_RDY;

    RtwSdioCmd52(Adapter, TRUE, R_AX_SDIO_INDIRECT_ADDR, (UCHAR)Word, NULL);
    RtwSdioCmd52(Adapter, TRUE, R_AX_SDIO_INDIRECT_ADDR + 1, (UCHAR)(Word >> 8), NULL);
    RtwSdioCmd52(Adapter, TRUE, R_AX_SDIO_INDIRECT_ADDR + 2, (UCHAR)(Word >> 16), NULL);
    RtwSdioCmd52(Adapter, TRUE, R_AX_SDIO_INDIRECT_ADDR + 3, (UCHAR)(Word >> 24), NULL);
}

static
ULONG
RtwIndirectRead52(
    _In_ PRTW_ADAPTER Adapter,
    _In_ ULONG Address)
{
    UCHAR Bytes[4];
    ULONG i;

    RtwIndirectSetup52(Adapter, Address);
    RtwSdioCmd52(Adapter, TRUE, R_AX_SDIO_INDIRECT_CTRL, (UCHAR)B_AX_INDIRECT_REG_R, NULL);
    RtwWaitIndirect(Adapter);

    for (i = 0; i < 4; i++)
        RtwSdioCmd52(Adapter, FALSE, R_AX_SDIO_INDIRECT_DATA + i, 0, &Bytes[i]);

    return Bytes[0] | ((ULONG)Bytes[1] << 8) | ((ULONG)Bytes[2] << 16) | ((ULONG)Bytes[3] << 24);
}

static
ULONG
RtwIndirectRead53(
    _In_ PRTW_ADAPTER Adapter,
    _In_ ULONG Address)
{
    UCHAR Command[12];
    UCHAR Reply[8];
    ULONG Word = Address | B_AX_INDIRECT_RDY;
    ULONG Poll;

    RtlZeroMemory(Command, sizeof(Command));
    Command[0] = (UCHAR)Word;
    Command[1] = (UCHAR)(Word >> 8);
    Command[2] = (UCHAR)(Word >> 16);
    Command[3] = (UCHAR)(Word >> 24);
    Command[8] = (UCHAR)B_AX_INDIRECT_REG_R;
    if (!NT_SUCCESS(RtwSdioScratchIo(Adapter, TRUE, R_AX_SDIO_INDIRECT_ADDR, Command, sizeof(Command))))
        return MAXULONG;

    RtlZeroMemory(Reply, sizeof(Reply));
    for (Poll = 0; Poll < RTW_SDIO_INDIRECT_POLLS; Poll++)
    {
        if (!NT_SUCCESS(RtwSdioScratchIo(Adapter, FALSE, R_AX_SDIO_INDIRECT_ADDR + 3, Reply, sizeof(Reply))))
            return MAXULONG;
        if (Reply[0] & 0x80)
            break;
    }
    if (!(Reply[0] & 0x80))
        DPRINT1("RTW8852BS: indirect read 0x%05lx timed out\n", Address);

    return Reply[1] | ((ULONG)Reply[2] << 8) | ((ULONG)Reply[3] << 16) | ((ULONG)Reply[4] << 24);
}

static
ULONG
RtwIndirectRead(
    _In_ PRTW_ADAPTER Adapter,
    _In_ ULONG Address)
{
    ULONG Aligned = Address & ~3UL;
    ULONG Value;
    ULONG Retry;

    if (!RtwIsCmac(Address))
        return Adapter->PowerOn ? RtwIndirectRead53(Adapter, Address) : RtwIndirectRead52(Adapter, Address);

    for (Retry = 0; Retry < RTW_SDIO_DEAD_RETRIES; Retry++)
    {
        Value = Adapter->PowerOn ? RtwIndirectRead53(Adapter, Aligned) : RtwIndirectRead52(Adapter, Aligned);
        if (Value != MAC_AX_R32_DEAD)
            break;
        RtwSdioCmd52(Adapter, TRUE, R_AX_CK_EN, 0xFF, NULL);
        RtwSdioCmd52(Adapter, TRUE, R_AX_CK_EN + 1, 0xFF, NULL);
        RtwSdioCmd52(Adapter, TRUE, R_AX_CK_EN + 2, 0xFF, NULL);
        RtwSdioCmd52(Adapter, TRUE, R_AX_CK_EN + 3, 0xFF, NULL);
    }

    return Value >> ((Address & 3) * 8);
}

static
VOID
RtwIndirectWrite(
    _In_ PRTW_ADAPTER Adapter,
    _In_ ULONG Address,
    _In_ ULONG Value,
    _In_ UCHAR SizeCode)
{
    UCHAR Command[12];
    ULONG Word = Address | B_AX_INDIRECT_RDY;
    ULONG i;

    if (!Adapter->PowerOn)
    {
        RtwIndirectSetup52(Adapter, Address);
        for (i = 0; i <= (ULONG)(1u << SizeCode) - 1; i++)
            RtwSdioCmd52(Adapter, TRUE, R_AX_SDIO_INDIRECT_DATA + i, (UCHAR)(Value >> (i * 8)), NULL);
        RtwSdioCmd52(Adapter, TRUE, R_AX_SDIO_INDIRECT_CTRL, (UCHAR)(B_AX_INDIRECT_REG_W | SizeCode), NULL);
        RtwWaitIndirect(Adapter);
        return;
    }

    Command[0] = (UCHAR)Word;
    Command[1] = (UCHAR)(Word >> 8);
    Command[2] = (UCHAR)(Word >> 16);
    Command[3] = (UCHAR)(Word >> 24);
    for (i = 0; i < 4; i++)
        Command[4 + i] = (UCHAR)(Value >> (i * 8));
    Command[8] = (UCHAR)(B_AX_INDIRECT_REG_W | SizeCode);
    Command[9] = 0;
    Command[10] = 0;
    Command[11] = 0;
    if (NT_SUCCESS(RtwSdioScratchIo(Adapter, TRUE, R_AX_SDIO_INDIRECT_ADDR, Command, sizeof(Command))))
        RtwWaitIndirect(Adapter);
}

static
ULONG
RtwDirectRead(
    _In_ PRTW_ADAPTER Adapter,
    _In_ ULONG Address,
    _In_ ULONG Length)
{
    UCHAR Bytes[4] = { 0xFF, 0xFF, 0xFF, 0xFF };
    ULONG Aligned = Address & ~3UL;
    ULONG Value;
    ULONG Retry;

    if (!RtwIsCmac(Address))
    {
        RtwSdioScratchIo(Adapter, FALSE, Address, Bytes, Length);
        Value = Bytes[0] | ((ULONG)Bytes[1] << 8) | ((ULONG)Bytes[2] << 16) | ((ULONG)Bytes[3] << 24);
        return Value;
    }

    for (Retry = 0; Retry < RTW_SDIO_DEAD_RETRIES; Retry++)
    {
        RtwSdioScratchIo(Adapter, FALSE, Aligned, Bytes, 4);
        Value = Bytes[0] | ((ULONG)Bytes[1] << 8) | ((ULONG)Bytes[2] << 16) | ((ULONG)Bytes[3] << 24);
        if (Value != MAC_AX_R32_DEAD)
            break;
        Bytes[0] = Bytes[1] = Bytes[2] = Bytes[3] = 0xFF;
        RtwSdioScratchIo(Adapter, TRUE, R_AX_CK_EN, Bytes, 4);
    }

    return Value >> ((Address & 3) * 8);
}

static
VOID
RtwDirectWrite(
    _In_ PRTW_ADAPTER Adapter,
    _In_ ULONG Address,
    _In_ ULONG Value,
    _In_ ULONG Length)
{
    UCHAR Bytes[4];

    Bytes[0] = (UCHAR)Value;
    Bytes[1] = (UCHAR)(Value >> 8);
    Bytes[2] = (UCHAR)(Value >> 16);
    Bytes[3] = (UCHAR)(Value >> 24);
    RtwSdioScratchIo(Adapter, TRUE, Address, Bytes, Length);
}

static
VOID
RtwIoLock(
    _In_ PRTW_ADAPTER Adapter)
{
    KeWaitForSingleObject(&Adapter->IoLock, Executive, KernelMode, FALSE, NULL);
}

static
VOID
RtwIoUnlock(
    _In_ PRTW_ADAPTER Adapter)
{
    KeReleaseMutex(&Adapter->IoLock, FALSE);
}

UCHAR
RtwRead8(
    _In_ PRTW_ADAPTER Adapter,
    _In_ ULONG Address)
{
    UCHAR Value = 0xFF;

    RtwIoLock(Adapter);
    if (RtwIsLocal(Address))
        RtwSdioCmd52(Adapter, FALSE, Address, 0, &Value);
    else if (Address > RTW_SDIO_MAX_ADDRESS || !Adapter->PowerOn)
        Value = (UCHAR)RtwIndirectRead(Adapter, Address);
    else
        Value = (UCHAR)RtwDirectRead(Adapter, Address, 1);
    RtwIoUnlock(Adapter);
    return Value;
}

USHORT
RtwRead16(
    _In_ PRTW_ADAPTER Adapter,
    _In_ ULONG Address)
{
    UCHAR Low = 0xFF, High = 0xFF;
    USHORT Value;

    if (Address & 1)
        return RtwRead8(Adapter, Address) | ((USHORT)RtwRead8(Adapter, Address + 1) << 8);

    RtwIoLock(Adapter);
    if (Adapter->PowerOn && Address <= RTW_SDIO_MAX_ADDRESS)
    {
        Value = (USHORT)RtwDirectRead(Adapter, Address, 2);
    }
    else if (RtwIsLocal(Address))
    {
        RtwSdioCmd52(Adapter, FALSE, Address, 0, &Low);
        RtwSdioCmd52(Adapter, FALSE, Address + 1, 0, &High);
        Value = Low | ((USHORT)High << 8);
    }
    else
    {
        Value = (USHORT)RtwIndirectRead(Adapter, Address);
    }
    RtwIoUnlock(Adapter);
    return Value;
}

ULONG
RtwRead32(
    _In_ PRTW_ADAPTER Adapter,
    _In_ ULONG Address)
{
    UCHAR Bytes[4];
    ULONG Value;
    ULONG i;

    if (Address & 3)
    {
        for (i = 0; i < 4; i++)
            Bytes[i] = RtwRead8(Adapter, Address + i);
        return Bytes[0] | ((ULONG)Bytes[1] << 8) | ((ULONG)Bytes[2] << 16) | ((ULONG)Bytes[3] << 24);
    }

    RtwIoLock(Adapter);
    if (Adapter->PowerOn && Address <= RTW_SDIO_MAX_ADDRESS)
    {
        Value = RtwDirectRead(Adapter, Address, 4);
    }
    else if (RtwIsLocal(Address))
    {
        for (i = 0; i < 4; i++)
            RtwSdioCmd52(Adapter, FALSE, Address + i, 0, &Bytes[i]);
        Value = Bytes[0] | ((ULONG)Bytes[1] << 8) | ((ULONG)Bytes[2] << 16) | ((ULONG)Bytes[3] << 24);
    }
    else
    {
        Value = RtwIndirectRead(Adapter, Address);
    }
    RtwIoUnlock(Adapter);
    return Value;
}

VOID
RtwWrite8(
    _In_ PRTW_ADAPTER Adapter,
    _In_ ULONG Address,
    _In_ UCHAR Value)
{
    RtwIoLock(Adapter);
    if (RtwIsLocal(Address))
        RtwSdioCmd52(Adapter, TRUE, Address, Value, NULL);
    else
        RtwIndirectWrite(Adapter, Address, Value, 0);
    RtwIoUnlock(Adapter);
}

VOID
RtwWrite16(
    _In_ PRTW_ADAPTER Adapter,
    _In_ ULONG Address,
    _In_ USHORT Value)
{
    if (Address & 1)
    {
        RtwWrite8(Adapter, Address, (UCHAR)Value);
        RtwWrite8(Adapter, Address + 1, (UCHAR)(Value >> 8));
        return;
    }

    RtwIoLock(Adapter);
    if (Adapter->PowerOn && Address <= RTW_SDIO_MAX_ADDRESS)
    {
        RtwDirectWrite(Adapter, Address, Value, 2);
    }
    else if (RtwIsLocal(Address))
    {
        RtwSdioCmd52(Adapter, TRUE, Address, (UCHAR)Value, NULL);
        RtwSdioCmd52(Adapter, TRUE, Address + 1, (UCHAR)(Value >> 8), NULL);
    }
    else
    {
        RtwIndirectWrite(Adapter, Address, Value, 1);
    }
    RtwIoUnlock(Adapter);
}

VOID
RtwWrite32(
    _In_ PRTW_ADAPTER Adapter,
    _In_ ULONG Address,
    _In_ ULONG Value)
{
    ULONG i;

    if (Address & 3)
    {
        for (i = 0; i < 4; i++)
            RtwWrite8(Adapter, Address + i, (UCHAR)(Value >> (i * 8)));
        return;
    }

    RtwIoLock(Adapter);
    if (Adapter->PowerOn && Address <= RTW_SDIO_MAX_ADDRESS)
    {
        RtwDirectWrite(Adapter, Address, Value, 4);
    }
    else if (RtwIsLocal(Address))
    {
        for (i = 0; i < 4; i++)
            RtwSdioCmd52(Adapter, TRUE, Address + i, (UCHAR)(Value >> (i * 8)), NULL);
    }
    else
    {
        RtwIndirectWrite(Adapter, Address, Value, 2);
    }
    RtwIoUnlock(Adapter);
}

VOID
RtwWrite32Set(
    _In_ PRTW_ADAPTER Adapter,
    _In_ ULONG Address,
    _In_ ULONG Bits)
{
    RtwWrite32(Adapter, Address, RtwRead32(Adapter, Address) | Bits);
}

VOID
RtwWrite32Clear(
    _In_ PRTW_ADAPTER Adapter,
    _In_ ULONG Address,
    _In_ ULONG Bits)
{
    RtwWrite32(Adapter, Address, RtwRead32(Adapter, Address) & ~Bits);
}

VOID
RtwWrite32Mask(
    _In_ PRTW_ADAPTER Adapter,
    _In_ ULONG Address,
    _In_ ULONG Mask,
    _In_ ULONG Value)
{
    ULONG Shift = 0;

    while (Shift < 32 && !(Mask & (1UL << Shift)))
        Shift++;
    RtwWrite32(Adapter, Address, (RtwRead32(Adapter, Address) & ~Mask) | ((Value << Shift) & Mask));
}

NTSTATUS
RtwPoll32(
    _In_ PRTW_ADAPTER Adapter,
    _In_ ULONG Address,
    _In_ ULONG Mask,
    _In_ ULONG Value,
    _In_ ULONG TimeoutUs)
{
    ULONG Elapsed;

    for (Elapsed = 0; ; Elapsed += 50)
    {
        if ((RtwRead32(Adapter, Address) & Mask) == Value)
            return STATUS_SUCCESS;
        if (Adapter->IoError || Elapsed >= TimeoutUs)
            break;
        KeStallExecutionProcessor(50);
    }

    return Adapter->IoError ? STATUS_DEVICE_DATA_ERROR : STATUS_IO_TIMEOUT;
}

NTSTATUS
RtwSdioOpen(
    _In_ PRTW_ADAPTER Adapter)
{
    SDBUS_REQUEST_PACKET Packet;
    USHORT BlockLength = RTW_SDIO_BLOCK_SIZE;
    NTSTATUS Status;

    KeInitializeMutex(&Adapter->IoLock, 0);
    ExInitializeFastMutex(&Adapter->TxLock);

    Adapter->Scratch = RtwAllocate(RTW_SCRATCH_SIZE);
    if (!Adapter->Scratch)
        return STATUS_INSUFFICIENT_RESOURCES;

    Adapter->ScratchMdl = IoAllocateMdl(Adapter->Scratch, RTW_SCRATCH_SIZE, FALSE, FALSE, NULL);
    if (!Adapter->ScratchMdl)
        return STATUS_INSUFFICIENT_RESOURCES;
    MmBuildMdlForNonPagedPool(Adapter->ScratchMdl);

    Adapter->TxBuffer = RtwAllocate(RTW_TX_BUFFER_SIZE);
    if (!Adapter->TxBuffer)
        return STATUS_INSUFFICIENT_RESOURCES;

    Adapter->TxMdl = IoAllocateMdl(Adapter->TxBuffer, RTW_TX_BUFFER_SIZE, FALSE, FALSE, NULL);
    if (!Adapter->TxMdl)
        return STATUS_INSUFFICIENT_RESOURCES;
    MmBuildMdlForNonPagedPool(Adapter->TxMdl);

    Status = SdBusOpenInterface(Adapter->PhysicalDeviceObject,
                                &Adapter->SdBus,
                                sizeof(SDBUS_INTERFACE_STANDARD),
                                SDBUS_INTERFACE_VERSION);
    if (!NT_SUCCESS(Status))
        return Status;
    Adapter->SdBusOpened = TRUE;

    SD_INIT_REQUEST_PACKET(&Packet, SDRF_SET_PROPERTY);
    Packet.Parameters.GetSetProperty.Property = SDP_FUNCTION_BLOCK_LENGTH;
    Packet.Parameters.GetSetProperty.Buffer = &BlockLength;
    Packet.Parameters.GetSetProperty.Length = sizeof(BlockLength);
    Status = SdBusSubmitRequest(Adapter->SdBus.Context, &Packet);
    if (!NT_SUCCESS(Status))
        DPRINT1("RTW8852BS: setting the SDIO block length failed 0x%08lx\n", Status);

    return Status;
}

VOID
RtwSdioClose(
    _In_ PRTW_ADAPTER Adapter)
{
    if (Adapter->SdBusOpened && Adapter->SdBus.InterfaceDereference)
        Adapter->SdBus.InterfaceDereference(Adapter->SdBus.Context);
    Adapter->SdBusOpened = FALSE;

    if (Adapter->ScratchMdl)
    {
        IoFreeMdl(Adapter->ScratchMdl);
        Adapter->ScratchMdl = NULL;
    }
    RtwFree(Adapter->Scratch);
    Adapter->Scratch = NULL;

    if (Adapter->TxMdl)
    {
        IoFreeMdl(Adapter->TxMdl);
        Adapter->TxMdl = NULL;
    }
    RtwFree(Adapter->TxBuffer);
    Adapter->TxBuffer = NULL;
}
