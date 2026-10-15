/*
 * PROJECT:     LiberNT Realtek RTL8852BS Native 802.11 Miniport
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     SDIO receive path, card interrupt and host-to-card commands
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 *              Copyright (c) 2019-2024 Realtek Corporation (BSD-3-Clause, see COPYING.rtw89)
 */

#include "rtw8852bs.h"
#include <rsna_ccmp.h>

#define NDEBUG
#include <debug.h>

#define RTW_RXD_SHORT_LENGTH            16
#define RTW_RXD_LONG_LENGTH             32
#define RTW_RX_AGG_ALIGNMENT            8
#define RTW_C2H_HEADER_LENGTH           8

NTSTATUS
RtwSendH2c(
    _In_ PRTW_ADAPTER Adapter,
    _In_ UCHAR Category,
    _In_ UCHAR Class,
    _In_ UCHAR Function,
    _In_ BOOLEAN DoneAck,
    _In_reads_bytes_opt_(Length) const VOID *Data,
    _In_ ULONG Length)
{
    PUCHAR Header = Adapter->TxBuffer + RTW_WD_BODY_LENGTH;
    BOOLEAN RecAck;
    NTSTATUS Status;
    ULONG Required, Available, Poll;

    if (Length + RTW_H2C_HEADER_LENGTH + RTW_WD_BODY_LENGTH > RTW_TX_BUFFER_SIZE)
        return STATUS_INVALID_PARAMETER;

    ExAcquireFastMutex(&Adapter->TxLock);

    RecAck = (Adapter->H2cSequence % 4) == 0;
    RtlZeroMemory(Adapter->TxBuffer, RTW_WD_BODY_LENGTH);
    RtwPutLe32(Adapter->TxBuffer, (ULONG)RTW_DMA_CH_H2C << 16);
    RtwPutLe32(Adapter->TxBuffer + 8, (Length + RTW_H2C_HEADER_LENGTH) & 0x3FFF);
    RtwPutLe32(Header,
               (Category & 0x3) |
               ((ULONG)Class << 2) |
               ((ULONG)Function << 8) |
               ((ULONG)Adapter->H2cSequence << 24));
    RtwPutLe32(Header + 4,
               ((Length + RTW_H2C_HEADER_LENGTH) & 0x3FFF) |
               (RecAck ? RTW_BIT(14) : 0) |
               (DoneAck ? RTW_BIT(15) : 0));
    if (Length)
        RtlCopyMemory(Header + RTW_H2C_HEADER_LENGTH, Data, Length);
    Adapter->H2cSequence++;

    Required = ((Length + RTW_H2C_HEADER_LENGTH + 127) / 128) * 2;
    for (Poll = 0; ; Poll++)
    {
        Available = (RtwRead32(Adapter, R_AX_SDIO_TXPG_WP) >> B_AX_SDIO_ACH12_AVAL_PG_SHIFT) & B_AX_SDIO_AVAL_PG_MASK;
        if (Available >= Required)
            break;
        if (Adapter->IoError || Poll >= 2000)
        {
            ExReleaseFastMutex(&Adapter->TxLock);
            DPRINT1("RTW8852BS: H2C %u/%u queue full, %lu of %lu pages\n", Class, Function, Available, Required);
            return STATUS_DEVICE_BUSY;
        }
        KeStallExecutionProcessor(50);
    }

    Status = RtwSdioWritePacket(Adapter, RTW_DMA_CH_H2C, RTW_WD_BODY_LENGTH + RTW_H2C_HEADER_LENGTH + Length);
    ExReleaseFastMutex(&Adapter->TxLock);
    return Status;
}

static
VOID
RtwHandleC2h(
    _In_ PRTW_ADAPTER Adapter,
    _In_reads_bytes_(Length) const UCHAR *Data,
    _In_ ULONG Length)
{
    ULONG Word0, Category, Class, Function, C2hLength;

    if (Length < RTW_C2H_HEADER_LENGTH)
        return;

    Word0 = RtwLe32(Data);
    Category = Word0 & 0x3;
    Class = (Word0 >> 2) & 0x3F;
    Function = (Word0 >> 8) & 0xFF;
    C2hLength = RtwLe32(Data + 4) & 0x3FFF;

    Adapter->C2hCount++;
    DPRINT("RTW8852BS: C2H cat %lu class %lu func %lu len %lu\n", Category, Class, Function, C2hLength);
}

static
BOOLEAN
RtwSoftwareDecrypt(
    _In_ PRTW_ADAPTER Adapter,
    _Inout_updates_bytes_(*Length) PUCHAR Frame,
    _Inout_ PULONG Length)
{
    ULONG HeaderLength = RtwFrameHeaderLength(Frame, *Length);
    PUCHAR Body = Frame + HeaderLength;
    BOOLEAN Group = (Frame[4] & 0x01) != 0;
    ULONGLONG Pn = 0;
    UCHAR KeyId, Tk[16];
    BOOLEAN Valid;

    if (*Length < HeaderLength + RSNA_CCMP_HDR_LEN + RSNA_CCMP_MIC_LEN)
        return FALSE;
    KeyId = Body[3] >> 6;

    NdisAcquireSpinLock(&Adapter->Lock);
    Valid = Adapter->SoftwareCrypto && (Group ? Adapter->GroupKeyValid[KeyId] : Adapter->MgmtKeyValid);
    if (Valid)
        RtlCopyMemory(Tk, Group ? Adapter->GroupKey[KeyId] : Adapter->MgmtTk, sizeof(Tk));
    NdisReleaseSpinLock(&Adapter->Lock);
    if (!Valid)
        return FALSE;

    Valid = RsnaCcmpDecrypt(Tk, Frame, HeaderLength, Body, *Length - HeaderLength, Body, &Pn) == 0;
    RtlSecureZeroMemory(Tk, sizeof(Tk));
    if (!Valid)
        return FALSE;

    NdisAcquireSpinLock(&Adapter->Lock);
    if (Group)
    {
        Valid = Pn > Adapter->GroupRxPn[KeyId];
        if (Valid)
            Adapter->GroupRxPn[KeyId] = Pn;
    }
    else
    {
        Valid = Pn > Adapter->DataRxPn;
        if (Valid)
            Adapter->DataRxPn = Pn;
    }
    NdisReleaseSpinLock(&Adapter->Lock);
    if (!Valid)
        return FALSE;

    Frame[1] &= ~DOT11_FC1_PROTECTED;
    *Length -= RSNA_CCMP_HDR_LEN + RSNA_CCMP_MIC_LEN;
    return TRUE;
}

static
VOID
RtwHandleWifiFrame(
    _In_ PRTW_ADAPTER Adapter,
    _Inout_updates_bytes_(Length) PUCHAR Frame,
    _In_ ULONG Length,
    _In_ BOOLEAN CrcError,
    _In_ ULONG Rate)
{
    LONG Rssi = RTW_DEFAULT_RSSI;

    if (CrcError || Length < 10 + DOT11_FCS_LENGTH)
    {
        Adapter->RxError++;
        return;
    }
    Length -= DOT11_FCS_LENGTH;

    switch (Frame[0] & DOT11_FC0_TYPE_MASK)
    {
        case DOT11_FC0_TYPE_MGMT:
            RtwMlmeReceive(Adapter, Frame, Length, Rssi, FALSE);
            break;

        case DOT11_FC0_TYPE_DATA:
            if (Frame[1] & DOT11_FC1_PROTECTED)
            {
                if (!RtwSoftwareDecrypt(Adapter, Frame, &Length))
                    break;
            }
            RtwReceiveData(Adapter, Frame, Length, Rssi, Rate);
            break;

        default:
            break;
    }
}

static
VOID
RtwProcessRxBuffer(
    _In_ PRTW_ADAPTER Adapter,
    _Inout_updates_bytes_(Length) PUCHAR Buffer,
    _In_ ULONG Length)
{
    ULONG Offset = 0, Word0, Word3, PacketLength, DescLength, DrvInfo, Type, PayloadOffset;

    while (Offset + RTW_RXD_SHORT_LENGTH <= Length)
    {
        Word0 = RtwLe32(Buffer + Offset);
        Word3 = RtwLe32(Buffer + Offset + 12);
        PacketLength = Word0 & 0x3FFF;
        Type = (Word0 >> 24) & 0xF;
        DrvInfo = ((Word0 >> 28) & 0x7) * 8;
        DescLength = (Word0 & RTW_BIT(31)) ? RTW_RXD_LONG_LENGTH : RTW_RXD_SHORT_LENGTH;
        PayloadOffset = Offset + DescLength + DrvInfo;

        if (PacketLength == 0 || PayloadOffset + PacketLength > Length)
            break;

        switch (Type)
        {
            case 0:
                RtwHandleWifiFrame(Adapter, Buffer + PayloadOffset, PacketLength, (Word3 & RTW_BIT(9)) != 0,
                                   (RtwLe32(Buffer + Offset + 4) >> 16) & 0x1FF);
                break;
            case 10:
                RtwHandleC2h(Adapter, Buffer + PayloadOffset, PacketLength);
                break;
            default:
                break;
        }

        Offset = (PayloadOffset + PacketLength + RTW_RX_AGG_ALIGNMENT - 1) & ~(RTW_RX_AGG_ALIGNMENT - 1);
    }
}

static
NTSTATUS
RtwReadRxFifo(
    _In_ PRTW_ADAPTER Adapter,
    _In_ ULONG Length)
{
    ULONG Blocks, Offset = 0, Chunk;
    NTSTATUS Status;

    Blocks = Length > RTW_SDIO_BYTE_MODE_MAX ? Length / RTW_SDIO_BLOCK_SIZE : 0;
    if (Blocks)
    {
        Offset = Blocks * RTW_SDIO_BLOCK_SIZE;
        MmInitializeMdl(Adapter->RxMdl, Adapter->RxBuffer, Offset);
        MmBuildMdlForNonPagedPool(Adapter->RxMdl);
        Status = RtwSdioTransfer(Adapter, FALSE, RTW_SDIO_RX_FIFO, Adapter->RxMdl, Offset);
        if (!NT_SUCCESS(Status))
            return Status;
    }

    while (Offset < Length)
    {
        Chunk = min(Length - Offset, (ULONG)RTW_SDIO_BYTE_MODE_MAX);
        MmInitializeMdl(Adapter->RxMdl, Adapter->RxBuffer + Offset, Chunk);
        MmBuildMdlForNonPagedPool(Adapter->RxMdl);
        Status = RtwSdioTransfer(Adapter, FALSE, RTW_SDIO_RX_FIFO + Offset, Adapter->RxMdl, Chunk);
        if (!NT_SUCCESS(Status))
            return Status;
        Offset += Chunk;
    }

    return STATUS_SUCCESS;
}

static
VOID
RtwServiceInterrupt(
    _In_ PRTW_ADAPTER Adapter)
{
    ULONG Status, Length, Loops;

    Status = RtwRead32(Adapter, R_AX_SDIO_HISR);
    if (Status & B_AX_HS0ISR_IND_EN)
    {
        ULONG General = RtwRead32(Adapter, R_AX_HISR0);

        RtwWrite32(Adapter, R_AX_HISR0, General);
        if (General & B_AX_HALT_C2H_INT_EN)
            DPRINT1("RTW8852BS: firmware halt C2H 0x%08lx\n", RtwRead32(Adapter, R_AX_HALT_C2H));
    }

    for (Loops = 0; Loops < 64; Loops++)
    {
        Length = RtwRead32(Adapter, R_AX_SDIO_RX_REQ_LEN) & B_AX_RX_REQ_LEN_MASK;
        if (Length == 0 || Adapter->IoError)
            break;
        if (Length > RTW_RX_BUFFER_SIZE)
        {
            DPRINT1("RTW8852BS: RX request %lu exceeds buffer\n", Length);
            break;
        }
        if (!NT_SUCCESS(RtwReadRxFifo(Adapter, Length)))
            break;
        RtwProcessRxBuffer(Adapter, Adapter->RxBuffer, Length);
    }
}

static
VOID
NTAPI
RtwCardInterrupt(
    _In_ PVOID Context,
    _In_ ULONG InterruptType)
{
    PRTW_ADAPTER Adapter = Context;

    UNREFERENCED_PARAMETER(InterruptType);

    InterlockedExchange(&Adapter->InterruptPending, 1);
    KeSetEvent(&Adapter->WorkerEvent, IO_NETWORK_INCREMENT, FALSE);
}

static
VOID
NTAPI
RtwWorkerThread(
    _In_ PVOID Context)
{
    PRTW_ADAPTER Adapter = Context;
    LARGE_INTEGER Timeout;
    BOOLEAN Interrupted;

    Timeout.QuadPart = -10LL * 1000 * 20;
    while (!Adapter->WorkerStop)
    {
        KeWaitForSingleObject(&Adapter->WorkerEvent, Executive, KernelMode, FALSE, &Timeout);
        if (Adapter->WorkerStop)
            break;

        Interrupted = InterlockedExchange(&Adapter->InterruptPending, 0) != 0;
        RtwServiceInterrupt(Adapter);
        if (Interrupted && Adapter->SdBus.AcknowledgeInterrupt)
            Adapter->SdBus.AcknowledgeInterrupt(Adapter->SdBus.Context);
        RtwTxPump(Adapter);
    }

    KeSetEvent(&Adapter->WorkerStopped, IO_NO_INCREMENT, FALSE);
    PsTerminateSystemThread(STATUS_SUCCESS);
}

NTSTATUS
RtwStartRx(
    _In_ PRTW_ADAPTER Adapter)
{
    SDBUS_INTERFACE_PARAMETERS Parameters;
    SDBUS_REQUEST_PACKET Packet;
    BOOLEAN Enable = TRUE;
    HANDLE Thread;
    NTSTATUS Status;

    Adapter->RxBuffer = RtwAllocate(RTW_RX_BUFFER_SIZE);
    if (!Adapter->RxBuffer)
        return STATUS_INSUFFICIENT_RESOURCES;
    Adapter->RxMdl = IoAllocateMdl(Adapter->RxBuffer, RTW_RX_BUFFER_SIZE, FALSE, FALSE, NULL);
    if (!Adapter->RxMdl)
        return STATUS_INSUFFICIENT_RESOURCES;
    MmBuildMdlForNonPagedPool(Adapter->RxMdl);

    KeInitializeEvent(&Adapter->WorkerEvent, SynchronizationEvent, FALSE);
    KeInitializeEvent(&Adapter->WorkerStopped, NotificationEvent, FALSE);

    Status = PsCreateSystemThread(&Thread, THREAD_ALL_ACCESS, NULL, NULL, NULL, RtwWorkerThread, Adapter);
    if (!NT_SUCCESS(Status))
        return Status;
    ObReferenceObjectByHandle(Thread, THREAD_ALL_ACCESS, NULL, KernelMode, (PVOID *)&Adapter->WorkerThread, NULL);
    ZwClose(Thread);

    RtlZeroMemory(&Parameters, sizeof(Parameters));
    Parameters.Size = sizeof(Parameters);
    Parameters.TargetObject = Adapter->PhysicalDeviceObject;
    Parameters.DeviceGeneratesInterrupts = TRUE;
    Parameters.CallbackAtDpcLevel = TRUE;
    Parameters.CallbackRoutine = RtwCardInterrupt;
    Parameters.CallbackRoutineContext = Adapter;
    Status = Adapter->SdBus.InitializeInterface(Adapter->SdBus.Context, &Parameters);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("RTW8852BS: card interrupt registration failed 0x%08lx, polling\n", Status);
    }
    else
    {
        Adapter->InterruptRegistered = TRUE;
        SD_INIT_REQUEST_PACKET(&Packet, SDRF_SET_PROPERTY);
        Packet.Parameters.GetSetProperty.Property = SDP_FUNCTION_INT_ENABLE;
        Packet.Parameters.GetSetProperty.Buffer = &Enable;
        Packet.Parameters.GetSetProperty.Length = sizeof(Enable);
        Status = SdBusSubmitRequest(Adapter->SdBus.Context, &Packet);
        if (!NT_SUCCESS(Status))
            DPRINT1("RTW8852BS: SDIO function interrupt enable failed 0x%08lx\n", Status);
    }

    RtwWrite32(Adapter, R_AX_SDIO_HIMR, B_AX_HS0ISR_IND_EN | B_AX_RX_REQUEST_INT_EN);
    RtwWrite32(Adapter, R_AX_HIMR0, B_AX_HALT_C2H_INT_EN);
    return STATUS_SUCCESS;
}

VOID
RtwStopRx(
    _In_ PRTW_ADAPTER Adapter)
{
    SDBUS_INTERFACE_PARAMETERS Parameters;

    if (Adapter->PowerOn)
    {
        RtwWrite32(Adapter, R_AX_SDIO_HIMR, 0);
        RtwWrite32(Adapter, R_AX_HIMR0, 0);
    }

    if (Adapter->InterruptRegistered)
    {
        RtlZeroMemory(&Parameters, sizeof(Parameters));
        Parameters.Size = sizeof(Parameters);
        Parameters.TargetObject = Adapter->PhysicalDeviceObject;
        Adapter->SdBus.InitializeInterface(Adapter->SdBus.Context, &Parameters);
        Adapter->InterruptRegistered = FALSE;
    }

    if (Adapter->WorkerThread)
    {
        Adapter->WorkerStop = TRUE;
        KeSetEvent(&Adapter->WorkerEvent, IO_NO_INCREMENT, FALSE);
        KeWaitForSingleObject(&Adapter->WorkerStopped, Executive, KernelMode, FALSE, NULL);
        ObDereferenceObject(Adapter->WorkerThread);
        Adapter->WorkerThread = NULL;
    }

    if (Adapter->RxMdl)
    {
        IoFreeMdl(Adapter->RxMdl);
        Adapter->RxMdl = NULL;
    }
    RtwFree(Adapter->RxBuffer);
    Adapter->RxBuffer = NULL;
}
