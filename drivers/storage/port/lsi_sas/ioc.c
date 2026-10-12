/*
 * PROJECT:     LiberNT Storage Drivers
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     LSI Fusion-MPT IOC doorbell handshake, reset and bring-up
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif193@gmail.com>
 */

#include "lsi_sas.h"

ULONG
LsiReadRegister(
    _In_ PLSI_DEVICE_EXTENSION Device,
    _In_ ULONG Offset)
{
    return StorPortReadRegisterUlong(Device, (PULONG)(Device->Registers + Offset));
}

VOID
LsiWriteRegister(
    _In_ PLSI_DEVICE_EXTENSION Device,
    _In_ ULONG Offset,
    _In_ ULONG Value)
{
    StorPortWriteRegisterUlong(Device, (PULONG)(Device->Registers + Offset), Value);
}

static
BOOLEAN
LsiWaitInterruptStatus(
    _In_ PLSI_DEVICE_EXTENSION Device,
    _In_ ULONG Mask,
    _In_ ULONG Value,
    _In_ ULONG TimeoutMilliseconds)
{
    ULONG Elapsed;

    for (Elapsed = 0; Elapsed < TimeoutMilliseconds * 100; Elapsed++)
    {
        if ((LsiReadRegister(Device, MPI_HOST_INTERRUPT_STATUS_OFFSET) & Mask) == Value)
            return TRUE;
        StorPortStallExecution(10);
    }
    return FALSE;
}

static
BOOLEAN
LsiWaitDoorbellInterrupt(
    _In_ PLSI_DEVICE_EXTENSION Device,
    _In_ ULONG TimeoutMilliseconds)
{
    return LsiWaitInterruptStatus(Device,
                                  MPI_HIS_DOORBELL_INTERRUPT,
                                  MPI_HIS_DOORBELL_INTERRUPT,
                                  TimeoutMilliseconds);
}

static
BOOLEAN
LsiWaitDoorbellAck(
    _In_ PLSI_DEVICE_EXTENSION Device)
{
    return LsiWaitInterruptStatus(Device,
                                  MPI_HIS_IOP_DOORBELL_STATUS,
                                  0,
                                  LSI_HANDSHAKE_TIMEOUT_MS);
}

static
BOOLEAN
LsiWaitState(
    _In_ PLSI_DEVICE_EXTENSION Device,
    _In_ ULONG State,
    _In_ ULONG TimeoutMilliseconds)
{
    ULONG Elapsed;
    ULONG Current;

    for (Elapsed = 0; Elapsed < TimeoutMilliseconds * 10; Elapsed++)
    {
        Current = LsiReadRegister(Device, MPI_DOORBELL_OFFSET);
        if (Current == MAXULONG)
            return FALSE;
        Current &= MPI_IOC_STATE_MASK;
        if (Current == State)
            return TRUE;
        if (Current == MPI_IOC_STATE_FAULT)
            return FALSE;
        StorPortStallExecution(100);
    }
    return FALSE;
}

static
BOOLEAN
LsiHandshake(
    _In_ PLSI_DEVICE_EXTENSION Device,
    _In_reads_bytes_(RequestLength) PVOID Request,
    _In_ ULONG RequestLength,
    _Out_writes_bytes_(ReplyLength) PVOID Reply,
    _In_ ULONG ReplyLength,
    _In_ ULONG ReplyTimeoutMilliseconds)
{
    PULONG RequestDwords = (PULONG)Request;
    PUSHORT ReplyWords = (PUSHORT)Reply;
    ULONG Count;
    ULONG Index;
    ULONG Words;
    USHORT Word;

    RtlZeroMemory(Reply, ReplyLength);

    if (LsiReadRegister(Device, MPI_DOORBELL_OFFSET) & MPI_DOORBELL_ACTIVE)
    {
        DPRINT1("lsi_sas: doorbell busy before handshake\n");
        return FALSE;
    }

    if (LsiReadRegister(Device, MPI_HOST_INTERRUPT_STATUS_OFFSET) & MPI_HIS_DOORBELL_INTERRUPT)
        LsiWriteRegister(Device, MPI_HOST_INTERRUPT_STATUS_OFFSET, 0);

    Count = RequestLength / sizeof(ULONG);
    LsiWriteRegister(Device,
                     MPI_DOORBELL_OFFSET,
                     (MPI_FUNCTION_HANDSHAKE << MPI_DOORBELL_FUNCTION_SHIFT) |
                     (Count << MPI_DOORBELL_ADD_DWORDS_SHIFT));

    if (!LsiWaitDoorbellInterrupt(Device, LSI_HANDSHAKE_TIMEOUT_MS))
        return FALSE;
    LsiWriteRegister(Device, MPI_HOST_INTERRUPT_STATUS_OFFSET, 0);
    if (!LsiWaitDoorbellAck(Device))
        return FALSE;

    for (Index = 0; Index < Count; Index++)
    {
        LsiWriteRegister(Device, MPI_DOORBELL_OFFSET, RequestDwords[Index]);
        if (!LsiWaitDoorbellAck(Device))
            return FALSE;
    }

    Words = 2;
    for (Index = 0; Index < Words; Index++)
    {
        if (!LsiWaitDoorbellInterrupt(Device,
                                      Index == 0 ? ReplyTimeoutMilliseconds : LSI_HANDSHAKE_TIMEOUT_MS))
        {
            return FALSE;
        }

        Word = (USHORT)(LsiReadRegister(Device, MPI_DOORBELL_OFFSET) & MPI_DOORBELL_DATA_MASK);
        LsiWriteRegister(Device, MPI_HOST_INTERRUPT_STATUS_OFFSET, 0);

        if (Index < ReplyLength / sizeof(USHORT))
            ReplyWords[Index] = Word;
        if (Index == 1)
            Words = max(2, (Word & 0xFF) * 2);
    }

    if (!LsiWaitDoorbellInterrupt(Device, LSI_HANDSHAKE_TIMEOUT_MS))
        return FALSE;
    LsiWriteRegister(Device, MPI_HOST_INTERRUPT_STATUS_OFFSET, 0);
    return TRUE;
}

static
BOOLEAN
LsiMessageUnitReset(
    _In_ PLSI_DEVICE_EXTENSION Device)
{
    if (LsiReadRegister(Device, MPI_DOORBELL_OFFSET) & MPI_DOORBELL_ACTIVE)
        return FALSE;

    LsiWriteRegister(Device,
                     MPI_DOORBELL_OFFSET,
                     MPI_FUNCTION_IOC_MESSAGE_UNIT_RESET << MPI_DOORBELL_FUNCTION_SHIFT);
    if (!LsiWaitDoorbellAck(Device))
        return FALSE;

    return LsiWaitState(Device, MPI_IOC_STATE_READY, LSI_READY_TIMEOUT_MS);
}

static
BOOLEAN
LsiHardReset(
    _In_ PLSI_DEVICE_EXTENSION Device)
{
    ULONG Elapsed;
    ULONG Diagnostic;

    LsiWriteRegister(Device, MPI_WRITE_SEQUENCE_OFFSET, MPI_WRSEQ_KEY_VALUE_MASK);
    LsiWriteRegister(Device, MPI_WRITE_SEQUENCE_OFFSET, MPI_WRSEQ_1ST_KEY_VALUE);
    LsiWriteRegister(Device, MPI_WRITE_SEQUENCE_OFFSET, MPI_WRSEQ_2ND_KEY_VALUE);
    LsiWriteRegister(Device, MPI_WRITE_SEQUENCE_OFFSET, MPI_WRSEQ_3RD_KEY_VALUE);
    LsiWriteRegister(Device, MPI_WRITE_SEQUENCE_OFFSET, MPI_WRSEQ_4TH_KEY_VALUE);
    LsiWriteRegister(Device, MPI_WRITE_SEQUENCE_OFFSET, MPI_WRSEQ_5TH_KEY_VALUE);

    Diagnostic = LsiReadRegister(Device, MPI_DIAGNOSTIC_OFFSET);
    if (!(Diagnostic & MPI_DIAG_DRWE))
    {
        DPRINT1("lsi_sas: diagnostic register write enable failed\n");
        return FALSE;
    }

    LsiWriteRegister(Device, MPI_DIAGNOSTIC_OFFSET, Diagnostic | MPI_DIAG_RESET_ADAPTER);
    for (Elapsed = 0; Elapsed < LSI_DIAG_RESET_TIMEOUT_MS; Elapsed++)
    {
        StorPortStallExecution(1000);
        if (!(LsiReadRegister(Device, MPI_DIAGNOSTIC_OFFSET) & MPI_DIAG_RESET_ADAPTER))
            break;
    }

    LsiWriteRegister(Device, MPI_WRITE_SEQUENCE_OFFSET, MPI_WRSEQ_KEY_VALUE_MASK);

    return LsiWaitState(Device, MPI_IOC_STATE_READY, LSI_READY_TIMEOUT_MS);
}

BOOLEAN
LsiMakeReady(
    _In_ PLSI_DEVICE_EXTENSION Device)
{
    ULONG Doorbell;
    ULONG State;

    Doorbell = LsiReadRegister(Device, MPI_DOORBELL_OFFSET);
    State = Doorbell & MPI_IOC_STATE_MASK;

    if (State == MPI_IOC_STATE_RESET &&
        LsiWaitState(Device, MPI_IOC_STATE_READY, LSI_READY_TIMEOUT_MS))
    {
        Doorbell = LsiReadRegister(Device, MPI_DOORBELL_OFFSET);
        State = Doorbell & MPI_IOC_STATE_MASK;
    }

    if (State == MPI_IOC_STATE_READY && !(Doorbell & MPI_DOORBELL_ACTIVE))
        return TRUE;

    if (State == MPI_IOC_STATE_OPERATIONAL && LsiMessageUnitReset(Device))
        return TRUE;

    DPRINT1("lsi_sas: IOC doorbell 0x%08lx, issuing a diagnostic reset\n", Doorbell);
    return LsiHardReset(Device);
}

BOOLEAN
LsiGetIocFacts(
    _In_ PLSI_DEVICE_EXTENSION Device,
    _Out_ PMPI_IOC_FACTS_REPLY Facts)
{
    MPI_REQUEST_HEADER Request;

    RtlZeroMemory(&Request, sizeof(Request));
    Request.Function = MPI_FUNCTION_IOC_FACTS;

    if (!LsiHandshake(Device, &Request, sizeof(Request), Facts, sizeof(*Facts), LSI_HANDSHAKE_TIMEOUT_MS))
        return FALSE;

    return (Facts->Function == MPI_FUNCTION_IOC_FACTS) &&
           ((Facts->IOCStatus & MPI_IOCSTATUS_MASK) == MPI_IOCSTATUS_SUCCESS);
}

static
BOOLEAN
LsiPortRequest(
    _In_ PLSI_DEVICE_EXTENSION Device,
    _In_ UCHAR Function,
    _Out_writes_bytes_(ReplyLength) PVOID Reply,
    _In_ ULONG ReplyLength,
    _In_ ULONG TimeoutMilliseconds)
{
    MPI_PORT_REQUEST Request;
    PMPI_DEFAULT_REPLY Header = (PMPI_DEFAULT_REPLY)Reply;

    RtlZeroMemory(&Request, sizeof(Request));
    Request.Function = Function;

    if (!LsiHandshake(Device, &Request, sizeof(Request), Reply, ReplyLength, TimeoutMilliseconds))
        return FALSE;

    return (Header->Function == Function) &&
           ((Header->IOCStatus & MPI_IOCSTATUS_MASK) == MPI_IOCSTATUS_SUCCESS);
}

BOOLEAN
LsiStartIoc(
    _In_ PLSI_DEVICE_EXTENSION Device)
{
    MPI_PORT_FACTS_REPLY PortFacts;
    MPI_IOC_INIT_REQUEST Init;
    MPI_DEFAULT_REPLY Reply;
    ULONG Index;

    if (!LsiPortRequest(Device, MPI_FUNCTION_PORT_FACTS, &PortFacts, sizeof(PortFacts), LSI_HANDSHAKE_TIMEOUT_MS))
    {
        DPRINT1("lsi_sas: PortFacts failed\n");
        return FALSE;
    }
    if (PortFacts.PortType == MPI_PORTFACTS_PORTTYPE_INACTIVE)
    {
        DPRINT1("lsi_sas: port 0 is inactive\n");
        return FALSE;
    }
    Device->PortSCSIID = PortFacts.PortSCSIID;

    RtlZeroMemory(&Init, sizeof(Init));
    Init.WhoInit = MPI_WHOINIT_HOST_DRIVER;
    Init.Function = MPI_FUNCTION_IOC_INIT;
    Init.MaxDevices = Device->MaxDevices;
    Init.MaxBuses = Device->MaxBuses;
    Init.ReplyFrameSize = LSI_REPLY_FRAME_SIZE;
    Init.HostMfaHighAddr = Device->FrameHighAddress;
    Init.SenseBufferHighAddr = Device->SenseHighAddress;
    Init.MsgVersion = MPI_VERSION_01_05;
    Init.HeaderVersion = MPI_HEADER_VERSION;

    if (!LsiHandshake(Device, &Init, sizeof(Init), &Reply, sizeof(Reply), LSI_HANDSHAKE_TIMEOUT_MS) ||
        Reply.Function != MPI_FUNCTION_IOC_INIT ||
        (Reply.IOCStatus & MPI_IOCSTATUS_MASK) != MPI_IOCSTATUS_SUCCESS)
    {
        DPRINT1("lsi_sas: IOCInit failed, status 0x%04x\n", Reply.IOCStatus);
        return FALSE;
    }

    if (!LsiWaitState(Device, MPI_IOC_STATE_OPERATIONAL, LSI_READY_TIMEOUT_MS))
    {
        DPRINT1("lsi_sas: IOC did not become operational\n");
        return FALSE;
    }

    for (Index = 0; Index < Device->ReplyFrameCount; Index++)
    {
        LsiWriteRegister(Device,
                         MPI_REPLY_FREE_FIFO_OFFSET,
                         Device->ReplyFramesPhysical + Index * LSI_REPLY_FRAME_SIZE);
    }

    if (!LsiPortRequest(Device, MPI_FUNCTION_PORT_ENABLE, &Reply, sizeof(Reply), LSI_PORT_ENABLE_TIMEOUT_MS))
    {
        DPRINT1("lsi_sas: PortEnable failed, status 0x%04x\n", Reply.IOCStatus);
        return FALSE;
    }

    Device->Operational = TRUE;
    return TRUE;
}

BOOLEAN
LsiReinitializeIoc(
    _In_ PLSI_DEVICE_EXTENSION Device)
{
    MPI_IOC_FACTS_REPLY Facts;
    BOOLEAN Ready;

    Device->Operational = FALSE;
    LsiWriteRegister(Device, MPI_HOST_INTERRUPT_MASK_OFFSET, MPI_HIM_DIM | MPI_HIM_RIM);

    Ready = LsiMakeReady(Device);
    LsiFailOutstanding(Device, SRB_STATUS_BUS_RESET);
    if (!Ready || !LsiGetIocFacts(Device, &Facts))
        return FALSE;

    if (Facts.RequestFrameSize != Device->RequestFrameSize ||
        Facts.GlobalCredits < Device->SlotCount ||
        Facts.ReplyQueueDepth < Device->ReplyFrameCount ||
        Facts.MaxChainDepth < Device->ChainSegments)
    {
        DPRINT1("lsi_sas: IOC facts changed across reset\n");
        return FALSE;
    }

    if (!LsiStartIoc(Device))
        return FALSE;

    if (Device->InterruptsEnabled)
        LsiWriteRegister(Device, MPI_HOST_INTERRUPT_MASK_OFFSET, MPI_HIM_DIM);
    return TRUE;
}

VOID
LsiStopIoc(
    _In_ PLSI_DEVICE_EXTENSION Device)
{
    ULONG Doorbell;

    Device->Operational = FALSE;
    Device->InterruptsEnabled = FALSE;
    LsiWriteRegister(Device, MPI_HOST_INTERRUPT_MASK_OFFSET, MPI_HIM_DIM | MPI_HIM_RIM);

    Doorbell = LsiReadRegister(Device, MPI_DOORBELL_OFFSET);
    if (Doorbell != MAXULONG && (Doorbell & MPI_IOC_STATE_MASK) != MPI_IOC_STATE_READY)
    {
        if ((Doorbell & MPI_IOC_STATE_MASK) != MPI_IOC_STATE_OPERATIONAL || !LsiMessageUnitReset(Device))
            LsiHardReset(Device);
    }

    LsiFailOutstanding(Device, SRB_STATUS_NO_HBA);
}
