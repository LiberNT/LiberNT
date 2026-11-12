/*
 * PROJECT:     LiberNT Broadcom/Cypress CYW43xx Native 802.11 Miniport
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     PCIe message buffer protocol: rings, IOCTLs, data path and service thread
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "cyw43455.h"
#include "pcie.h"

#define NDEBUG
#include <debug.h>

static const USHORT CywRingDepth[CYW_RING_COMMON_COUNT] =
{
    64, 2048, 64, 1024, 2048
};

static const USHORT CywRingItemSizeV7[CYW_RING_COMMON_COUNT] =
{
    40, 32, 24, 24, 40
};

static const USHORT CywRingItemSizePreV7[CYW_RING_COMMON_COUNT] =
{
    40, 32, 24, 16, 32
};

static
ULONG
CywLow32(
    _In_ NDIS_PHYSICAL_ADDRESS Address)
{
    return Address.LowPart;
}

static
ULONG
CywHigh32(
    _In_ NDIS_PHYSICAL_ADDRESS Address)
{
    return (ULONG)Address.HighPart;
}

static
VOID
CywRingWriteIndex(
    _In_ PCYW_PCIE Pcie,
    _In_ ULONG Address,
    _In_ USHORT Value)
{
    if (Pcie->IndexSize != 0 && Pcie->IndexBuffer.Va != NULL)
    {
        KeMemoryBarrier();
        *(volatile USHORT *)(Pcie->IndexBuffer.Va + Address) = Value;
        KeMemoryBarrier();
    }
    else
    {
        CywPcieWriteTcm16(Pcie, Address, Value);
    }
}

static
USHORT
CywRingReadIndex(
    _In_ PCYW_PCIE Pcie,
    _In_ ULONG Address)
{
    if (Pcie->IndexSize != 0 && Pcie->IndexBuffer.Va != NULL)
    {
        USHORT Value = *(volatile USHORT *)(Pcie->IndexBuffer.Va + Address);

        KeMemoryBarrier();
        return Value;
    }
    return CywPcieReadTcm16(Pcie, Address);
}

static
PVOID
CywRingReserve(
    _In_ PCYW_PCIE Pcie,
    _In_ PCYW_MSGRING Ring)
{
    USHORT Next = (USHORT)((Ring->Write + 1) % Ring->Depth);

    Ring->Read = CywRingReadIndex(Pcie, Ring->ReadIndex);
    if (Next == Ring->Read)
    {
        return NULL;
    }
    return Ring->Memory.Va + (ULONG)Ring->Write * Ring->ItemSize;
}

static
VOID
CywRingCommit(
    _In_ PCYW_PCIE Pcie,
    _In_ PCYW_MSGRING Ring)
{
    Ring->Write = (USHORT)((Ring->Write + 1) % Ring->Depth);
    KeMemoryBarrier();
    CywRingWriteIndex(Pcie, Ring->WriteIndex, Ring->Write);
    CywPcieRingDoorbell(Pcie);
}

static
PVOID
CywRingNextComplete(
    _In_ PCYW_PCIE Pcie,
    _In_ PCYW_MSGRING Ring)
{
    PVOID Item;

    Ring->Write = CywRingReadIndex(Pcie, Ring->WriteIndex);
    if (Ring->Read == Ring->Write)
    {
        return NULL;
    }
    KeMemoryBarrier();
    Item = Ring->Memory.Va + (ULONG)Ring->Read * Ring->ItemSize;
    return Item;
}

static
VOID
CywRingConsume(
    _In_ PCYW_PCIE Pcie,
    _In_ PCYW_MSGRING Ring)
{
    Ring->Read = (USHORT)((Ring->Read + 1) % Ring->Depth);
    CywRingWriteIndex(Pcie, Ring->ReadIndex, Ring->Read);
}

static
NTSTATUS
CywPoolCreate(
    _In_ PCYW_ADAPTER Adapter,
    _Out_ PCYW_PKT_POOL Pool,
    _In_ ULONG Count,
    _In_ ULONG ItemSize,
    _In_ ULONG IdBase)
{
    ULONG ItemsPerChunk = CYW_POOL_CHUNK_SIZE / ItemSize;
    ULONG Allocated = 0;
    ULONG ChunkIndex = 0;

    RtlZeroMemory(Pool, sizeof(*Pool));
    Pool->ItemSize = ItemSize;
    Pool->ItemsPerChunk = ItemsPerChunk;
    Pool->IdBase = IdBase;
    Pool->State = CywAllocate(Count);
    if (Pool->State == NULL)
    {
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    while (Allocated < Count)
    {
        ULONG Want = min(ItemsPerChunk, Count - Allocated);

        if (ChunkIndex == CYW_POOL_MAX_CHUNKS ||
            !NT_SUCCESS(CywPcieAllocateDma(Adapter, Want * ItemSize, &Pool->Chunks[ChunkIndex])))
        {
            return STATUS_INSUFFICIENT_RESOURCES;
        }
        Allocated += Want;
        ChunkIndex++;
    }
    Pool->ChunkCount = ChunkIndex;
    Pool->Count = Count;
    return STATUS_SUCCESS;
}

static
VOID
CywPoolDestroy(
    _In_ PCYW_ADAPTER Adapter,
    _Inout_ PCYW_PKT_POOL Pool)
{
    ULONG Index;

    for (Index = 0; Index < Pool->ChunkCount; Index++)
    {
        CywPcieFreeDma(Adapter, &Pool->Chunks[Index]);
    }
    CywFree(Pool->State);
    RtlZeroMemory(Pool, sizeof(*Pool));
}

static
PUCHAR
CywPoolItem(
    _In_ PCYW_PKT_POOL Pool,
    _In_ ULONG Index,
    _Out_ PNDIS_PHYSICAL_ADDRESS Physical)
{
    ULONG Chunk = Index / Pool->ItemsPerChunk;
    ULONG Offset = (Index % Pool->ItemsPerChunk) * Pool->ItemSize;

    Physical->QuadPart = Pool->Chunks[Chunk].Pa.QuadPart + Offset;
    return Pool->Chunks[Chunk].Va + Offset;
}

static
PUCHAR
CywPoolAlloc(
    _In_ PCYW_PKT_POOL Pool,
    _Out_ PULONG PktId,
    _Out_ PNDIS_PHYSICAL_ADDRESS Physical)
{
    ULONG Index;

    for (Index = 0; Index < Pool->Count; Index++)
    {
        if (Pool->State[Index] == 0)
        {
            Pool->State[Index] = 1;
            *PktId = Pool->IdBase + Index;
            return CywPoolItem(Pool, Index, Physical);
        }
    }
    return NULL;
}

static
PUCHAR
CywPoolFromId(
    _In_ PCYW_PKT_POOL Pool,
    _In_ ULONG PktId,
    _In_ BOOLEAN Release)
{
    ULONG Index = PktId - Pool->IdBase;
    NDIS_PHYSICAL_ADDRESS Physical;

    if (PktId < Pool->IdBase || Index >= Pool->Count || Pool->State[Index] == 0)
    {
        return NULL;
    }
    if (Release)
    {
        Pool->State[Index] = 0;
    }
    return CywPoolItem(Pool, Index, &Physical);
}

static
NTSTATUS
CywRingAllocate(
    _In_ PCYW_ADAPTER Adapter,
    _In_ PCYW_MSGRING Ring,
    _In_ ULONG RingMemAddress,
    _In_ USHORT Depth,
    _In_ USHORT ItemSize)
{
    PCYW_PCIE Pcie = Adapter->Pcie;
    NTSTATUS Status;

    Status = CywPcieAllocateDma(Adapter, (ULONG)Depth * ItemSize, &Ring->Memory);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }
    Ring->Depth = Depth;
    Ring->ItemSize = ItemSize;
    Ring->Read = 0;
    Ring->Write = 0;
    CywPcieWriteTcm16(Pcie, RingMemAddress + CYW_RING_DESC_MAX_ITEMS, Depth);
    CywPcieWriteTcm16(Pcie, RingMemAddress + CYW_RING_DESC_ITEM_SIZE, ItemSize);
    CywPcieWriteTcm32(Pcie, RingMemAddress + CYW_RING_DESC_BASE_ADDR, CywLow32(Ring->Memory.Pa));
    CywPcieWriteTcm32(Pcie, RingMemAddress + CYW_RING_DESC_BASE_ADDR + 4, CywHigh32(Ring->Memory.Pa));
    return STATUS_SUCCESS;
}

static
NTSTATUS
CywRingsInitialize(
    _In_ PCYW_ADAPTER Adapter)
{
    PCYW_PCIE Pcie = Adapter->Pcie;
    const USHORT *ItemSizes = (Pcie->SharedVersion >= CYW_SHARED_ITEMSIZE_V7) ?
                              CywRingItemSizeV7 : CywRingItemSizePreV7;
    ULONG RingInfo = Pcie->RingInfoAddress;
    ULONG RingMem;
    ULONG H2dWrite;
    ULONG H2dRead;
    ULONG D2hWrite;
    ULONG D2hRead;
    ULONG Stride;
    ULONG Index;
    USHORT MaxFlow;
    USHORT MaxSubmission;
    USHORT MaxCompletion;
    NTSTATUS Status;

    if (!CywPcieTcmRange(Pcie, RingInfo, sizeof(CYW_PCIE_RING_INFO)))
    {
        return STATUS_DEVICE_DATA_ERROR;
    }
    RingMem = CywPcieReadTcm32(Pcie, RingInfo + FIELD_OFFSET(CYW_PCIE_RING_INFO, RingMem));
    MaxFlow = CywPcieReadTcm16(Pcie, RingInfo + FIELD_OFFSET(CYW_PCIE_RING_INFO, MaxFlowRings));
    MaxSubmission = CywPcieReadTcm16(Pcie, RingInfo + FIELD_OFFSET(CYW_PCIE_RING_INFO, MaxSubmissionRings));
    MaxCompletion = CywPcieReadTcm16(Pcie, RingInfo + FIELD_OFFSET(CYW_PCIE_RING_INFO, MaxCompletionRings));

    if (Pcie->SharedVersion >= 6)
    {
        Pcie->MaxSubmissionRings = MaxSubmission;
        Pcie->MaxFlowRings = MaxFlow;
        Pcie->MaxCompletionRings = MaxCompletion;
    }
    else
    {
        Pcie->MaxSubmissionRings = MaxFlow;
        Pcie->MaxFlowRings = MaxFlow - CYW_RING_H2D_COMMON_COUNT;
        Pcie->MaxCompletionRings = CYW_RING_D2H_COMMON_COUNT;
    }
    if (Pcie->MaxFlowRings == 0 || Pcie->MaxFlowRings > CYW_MAX_FLOWRINGS)
    {
        return STATUS_DEVICE_DATA_ERROR;
    }
    if (Pcie->IndexSize != 0)
    {
        ULONG Entries = (ULONG)(Pcie->MaxSubmissionRings + Pcie->MaxCompletionRings) * 2;

        Status = CywPcieAllocateDma(Adapter, Entries * Pcie->IndexSize, &Pcie->IndexBuffer);
        if (!NT_SUCCESS(Status))
        {
            Pcie->IndexSize = 0;
        }
    }

    if (Pcie->IndexSize == 0)
    {
        H2dWrite = CywPcieReadTcm32(Pcie, RingInfo + FIELD_OFFSET(CYW_PCIE_RING_INFO, H2dWriteIndexPtr));
        H2dRead = CywPcieReadTcm32(Pcie, RingInfo + FIELD_OFFSET(CYW_PCIE_RING_INFO, H2dReadIndexPtr));
        D2hWrite = CywPcieReadTcm32(Pcie, RingInfo + FIELD_OFFSET(CYW_PCIE_RING_INFO, D2hWriteIndexPtr));
        D2hRead = CywPcieReadTcm32(Pcie, RingInfo + FIELD_OFFSET(CYW_PCIE_RING_INFO, D2hReadIndexPtr));
        Stride = sizeof(ULONG);
    }
    else
    {
        ULONGLONG Base = (ULONGLONG)Pcie->IndexBuffer.Pa.QuadPart;
        ULONG Host;

        Stride = Pcie->IndexSize;
        H2dWrite = 0;
        H2dRead = H2dWrite + Pcie->MaxSubmissionRings * Stride;
        D2hWrite = H2dRead + Pcie->MaxSubmissionRings * Stride;
        D2hRead = D2hWrite + Pcie->MaxCompletionRings * Stride;

        Host = RingInfo + FIELD_OFFSET(CYW_PCIE_RING_INFO, H2dWriteIndexHost);
        CywPcieWriteTcm32(Pcie, Host, (ULONG)Base);
        CywPcieWriteTcm32(Pcie, Host + 4, (ULONG)(Base >> 32));
        Base += Pcie->MaxSubmissionRings * Stride;
        Host = RingInfo + FIELD_OFFSET(CYW_PCIE_RING_INFO, H2dReadIndexHost);
        CywPcieWriteTcm32(Pcie, Host, (ULONG)Base);
        CywPcieWriteTcm32(Pcie, Host + 4, (ULONG)(Base >> 32));
        Base += Pcie->MaxSubmissionRings * Stride;
        Host = RingInfo + FIELD_OFFSET(CYW_PCIE_RING_INFO, D2hWriteIndexHost);
        CywPcieWriteTcm32(Pcie, Host, (ULONG)Base);
        CywPcieWriteTcm32(Pcie, Host + 4, (ULONG)(Base >> 32));
        Base += Pcie->MaxCompletionRings * Stride;
        Host = RingInfo + FIELD_OFFSET(CYW_PCIE_RING_INFO, D2hReadIndexHost);
        CywPcieWriteTcm32(Pcie, Host, (ULONG)Base);
        CywPcieWriteTcm32(Pcie, Host + 4, (ULONG)(Base >> 32));
    }

    for (Index = 0; Index < CYW_RING_COMMON_COUNT; Index++)
    {
        Status = CywRingAllocate(Adapter, &Pcie->Rings[Index], RingMem,
                                 CywRingDepth[Index], ItemSizes[Index]);
        if (!NT_SUCCESS(Status))
        {
            return Status;
        }
        Pcie->Rings[Index].Id = (USHORT)Index;
        if (Index < CYW_RING_H2D_COMMON_COUNT)
        {
            Pcie->Rings[Index].WriteIndex = H2dWrite;
            Pcie->Rings[Index].ReadIndex = H2dRead;
            H2dWrite += Stride;
            H2dRead += Stride;
        }
        else
        {
            Pcie->Rings[Index].WriteIndex = D2hWrite;
            Pcie->Rings[Index].ReadIndex = D2hRead;
            D2hWrite += Stride;
            D2hRead += Stride;
        }
        RingMem += CYW_RING_DESC_SIZE;
    }

    Pcie->FlowWriteIndexBase = H2dWrite;
    Pcie->FlowReadIndexBase = H2dRead;

    if (Pcie->IndexSize == 0)
    {
        for (Index = 0; Index < CYW_RING_COMMON_COUNT; Index++)
        {
            CywPcieWriteTcm16(Pcie, Pcie->Rings[Index].ReadIndex, 0);
            CywPcieWriteTcm16(Pcie, Pcie->Rings[Index].WriteIndex, 0);
        }
    }
    return STATUS_SUCCESS;
}

static
NTSTATUS
CywScratchInitialize(
    _In_ PCYW_ADAPTER Adapter)
{
    PCYW_PCIE Pcie = Adapter->Pcie;
    NTSTATUS Status;

    Status = CywPcieAllocateDma(Adapter, CYW_SCRATCH_LENGTH, &Pcie->Scratch);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }
    CywPcieWriteTcm32(Pcie, Pcie->SharedAddress + CYW_SHARED_DMA_SCRATCH_ADDR, CywLow32(Pcie->Scratch.Pa));
    CywPcieWriteTcm32(Pcie, Pcie->SharedAddress + CYW_SHARED_DMA_SCRATCH_ADDR + 4, CywHigh32(Pcie->Scratch.Pa));
    CywPcieWriteTcm32(Pcie, Pcie->SharedAddress + CYW_SHARED_DMA_SCRATCH_LEN, CYW_SCRATCH_LENGTH);

    Status = CywPcieAllocateDma(Adapter, CYW_RINGUPD_LENGTH, &Pcie->RingUpdate);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }
    CywPcieWriteTcm32(Pcie, Pcie->SharedAddress + CYW_SHARED_DMA_RINGUPD_ADDR, CywLow32(Pcie->RingUpdate.Pa));
    CywPcieWriteTcm32(Pcie, Pcie->SharedAddress + CYW_SHARED_DMA_RINGUPD_ADDR + 4, CywHigh32(Pcie->RingUpdate.Pa));
    CywPcieWriteTcm32(Pcie, Pcie->SharedAddress + CYW_SHARED_DMA_RINGUPD_LEN, CYW_RINGUPD_LENGTH);
    return STATUS_SUCCESS;
}

static
VOID
CywPostControlBuffers(
    _In_ PCYW_ADAPTER Adapter)
{
    PCYW_PCIE Pcie = Adapter->Pcie;
    PCYW_MSGRING Ring = &Pcie->Rings[CYW_RING_CONTROL_SUBMIT];
    KIRQL OldIrql;

    KeAcquireSpinLock(&Pcie->SubmitLock, &OldIrql);
    while (Pcie->IoctlRespPosted < CYW_IOCTLRESP_POST_COUNT ||
           Pcie->EventPosted < CYW_EVENT_POST_COUNT)
    {
        PMSGBUF_CTRL_POST Post;
        PUCHAR Buffer;
        NDIS_PHYSICAL_ADDRESS Physical;
        ULONG PktId;
        BOOLEAN Event = Pcie->EventPosted < CYW_EVENT_POST_COUNT;

        Buffer = CywPoolAlloc(&Pcie->CtrlPool, &PktId, &Physical);
        if (Buffer == NULL)
        {
            break;
        }
        Post = CywRingReserve(Pcie, Ring);
        if (Post == NULL)
        {
            CywPoolFromId(&Pcie->CtrlPool, PktId, TRUE);
            break;
        }
        RtlZeroMemory(Post, sizeof(*Post));
        Post->Header.MsgType = Event ? MSGBUF_TYPE_EVENT_BUF_POST : MSGBUF_TYPE_IOCTLRESP_BUF_POST;
        Post->Header.RequestId = PktId;
        Post->BufferLength = CYW_CTRL_BUFFER_SIZE;
        Post->Buffer.Low = CywLow32(Physical);
        Post->Buffer.High = CywHigh32(Physical);
        CywRingCommit(Pcie, Ring);
        if (Event)
        {
            Pcie->EventPosted++;
        }
        else
        {
            Pcie->IoctlRespPosted++;
        }
    }
    KeReleaseSpinLock(&Pcie->SubmitLock, OldIrql);
}

static
VOID
CywPostReceiveBuffers(
    _In_ PCYW_ADAPTER Adapter)
{
    PCYW_PCIE Pcie = Adapter->Pcie;
    PCYW_MSGRING Ring = &Pcie->Rings[CYW_RING_RXPOST_SUBMIT];
    KIRQL OldIrql;

    KeAcquireSpinLock(&Pcie->SubmitLock, &OldIrql);
    while (Pcie->RxPosted < Pcie->RxPostTarget)
    {
        PMSGBUF_RX_POST Post;
        PUCHAR Buffer;
        NDIS_PHYSICAL_ADDRESS Physical;
        ULONG PktId;

        Buffer = CywPoolAlloc(&Pcie->RxPool, &PktId, &Physical);
        if (Buffer == NULL)
        {
            break;
        }
        Post = CywRingReserve(Pcie, Ring);
        if (Post == NULL)
        {
            CywPoolFromId(&Pcie->RxPool, PktId, TRUE);
            break;
        }
        RtlZeroMemory(Post, sizeof(*Post));
        Post->Header.MsgType = MSGBUF_TYPE_RXBUF_POST;
        Post->Header.RequestId = PktId;
        Post->DataLength = CYW_DATA_BUFFER_SIZE;
        Post->Data.Low = CywLow32(Physical);
        Post->Data.High = CywHigh32(Physical);
        CywRingCommit(Pcie, Ring);
        Pcie->RxPosted++;
    }
    KeReleaseSpinLock(&Pcie->SubmitLock, OldIrql);
}

NTSTATUS
CywMsgbufSendMailbox(
    _In_ PCYW_ADAPTER Adapter,
    _In_ ULONG Data)
{
    PCYW_PCIE Pcie = Adapter->Pcie;
    PCYW_MSGRING Ring = &Pcie->Rings[CYW_RING_CONTROL_SUBMIT];
    PMSGBUF_H2D_MAILBOX Message;
    KIRQL OldIrql;

    KeAcquireSpinLock(&Pcie->SubmitLock, &OldIrql);
    Message = CywRingReserve(Pcie, Ring);
    if (Message == NULL)
    {
        KeReleaseSpinLock(&Pcie->SubmitLock, OldIrql);
        return STATUS_DEVICE_BUSY;
    }
    RtlZeroMemory(Message, sizeof(*Message));
    Message->Header.MsgType = MSGBUF_TYPE_H2D_MAILBOX_DATA;
    Message->Header.RequestId = 0;
    Message->Data = Data;
    CywRingCommit(Pcie, Ring);
    KeReleaseSpinLock(&Pcie->SubmitLock, OldIrql);
    return STATUS_SUCCESS;
}

NTSTATUS
CywMsgbufIoctl(
    _In_ PCYW_ADAPTER Adapter,
    _In_ ULONG Cmd,
    _In_ BOOLEAN Set,
    _Inout_updates_bytes_opt_(Length) PVOID Data,
    _In_ ULONG Length)
{
    PCYW_PCIE Pcie = Adapter->Pcie;
    PCYW_MSGRING Ring = &Pcie->Rings[CYW_RING_CONTROL_SUBMIT];
    PMSGBUF_IOCTL_REQUEST Request;
    KIRQL OldIrql;
    LARGE_INTEGER Timeout;
    ULONG InputLength = min(Length, CYW_IOCTL_MAX_INPUT);
    NTSTATUS Status;

    if (Length > CYW_CTRL_BUFFER_SIZE)
    {
        return STATUS_INVALID_PARAMETER;
    }
    if (InterlockedCompareExchange(&Pcie->Up, 0, 0) == 0 || Pcie->FirmwareHalted)
    {
        return STATUS_DEVICE_NOT_READY;
    }

    if (Data != NULL)
    {
        RtlCopyMemory(Pcie->IoctlRequest.Va, Data, InputLength);
    }
    else
    {
        RtlZeroMemory(Pcie->IoctlRequest.Va, InputLength);
    }

    KeClearEvent(&Pcie->IoctlEvent);
    Pcie->IoctlTransId++;
    Pcie->IoctlFirmwareStatus = 0;
    Pcie->IoctlResponseLength = 0;
    Pcie->IoctlResponseLost = FALSE;
    InterlockedExchange(&Pcie->IoctlPending, 1);

    KeAcquireSpinLock(&Pcie->SubmitLock, &OldIrql);
    Request = CywRingReserve(Pcie, Ring);
    if (Request == NULL)
    {
        KeReleaseSpinLock(&Pcie->SubmitLock, OldIrql);
        InterlockedExchange(&Pcie->IoctlPending, 0);
        return STATUS_DEVICE_BUSY;
    }
    RtlZeroMemory(Request, sizeof(*Request));
    Request->Header.MsgType = MSGBUF_TYPE_IOCTLPTR_REQ;
    Request->Header.RequestId = CYW_IOCTL_REQUEST_ID;
    Request->Cmd = Cmd;
    Request->TransId = Pcie->IoctlTransId;
    Request->InputLength = (USHORT)InputLength;
    Request->OutputLength = (USHORT)Length;
    Request->Buffer.Low = CywLow32(Pcie->IoctlRequest.Pa);
    Request->Buffer.High = CywHigh32(Pcie->IoctlRequest.Pa);
    CywRingCommit(Pcie, Ring);
    KeReleaseSpinLock(&Pcie->SubmitLock, OldIrql);
    KeSetEvent(&Adapter->BusEvent, IO_NO_INCREMENT, FALSE);

    Timeout.QuadPart = -(LONGLONG)CYW_IOCTL_RESP_TIMEOUT_MS * 10000LL;
    Status = KeWaitForSingleObject(&Pcie->IoctlEvent, Executive, KernelMode, FALSE, &Timeout);
    InterlockedExchange(&Pcie->IoctlPending, 0);
    if (Status != STATUS_SUCCESS)
    {
        DPRINT1("CYW: IOCTL %lu timed out\n", Cmd);
        CywPcieReadConsole(Pcie);
        InterlockedExchange(&Pcie->FirmwareFailed, 1);
        return STATUS_IO_TIMEOUT;
    }
    if (Pcie->IoctlFirmwareStatus != 0)
    {
        DPRINT1("CYW: IOCTL %lu rejected by firmware (%d)\n", Cmd, Pcie->IoctlFirmwareStatus);
        return STATUS_UNSUCCESSFUL;
    }
    if (Pcie->IoctlResponseLost)
    {
        DPRINT1("CYW: the response to IOCTL %lu names no posted buffer\n", Cmd);
        return STATUS_DEVICE_DATA_ERROR;
    }
    if (!Set && Data != NULL)
    {
        ULONG Copy = min(Length, Pcie->IoctlResponseLength);

        RtlZeroMemory(Data, Length);
        if (Copy != 0)
        {
            RtlCopyMemory(Data, Pcie->IoctlRequest.Va, Copy);
        }
    }
    return STATUS_SUCCESS;
}

static
USHORT
CywFlowRingLookup(
    _In_ PCYW_ADAPTER Adapter,
    _In_reads_(6) const UCHAR *Destination,
    _In_ UCHAR Priority)
{
    UNREFERENCED_PARAMETER(Adapter);
    UNREFERENCED_PARAMETER(Destination);
    UNREFERENCED_PARAMETER(Priority);

    /* TODO: per-peer/per-WMM flow rings; a single best-effort ring is used today. */
    return 0;
}

static
NTSTATUS
CywFlowRingStart(
    _In_ PCYW_ADAPTER Adapter)
{
    PCYW_PCIE Pcie = Adapter->Pcie;
    PCYW_MSGRING Ring = &Pcie->Rings[CYW_RING_CONTROL_SUBMIT];
    PMSGBUF_FLOWRING_CREATE Create;
    KIRQL OldIrql;
    NTSTATUS Status;

    if (InterlockedCompareExchange(&Pcie->FlowRingState, CywFlowRingOpening, CywFlowRingClosed) != CywFlowRingClosed)
    {
        return (InterlockedCompareExchange(&Pcie->FlowRingState, 0, 0) == CywFlowRingOpen) ?
               STATUS_SUCCESS : STATUS_DEVICE_NOT_READY;
    }

    CywPcieFreeDma(Adapter, &Pcie->FlowRing.Memory);
    Status = CywPcieAllocateDma(Adapter, CYW_FLOWRING_DEPTH * CYW_FLOWRING_ITEM_SIZE, &Pcie->FlowRing.Memory);
    if (!NT_SUCCESS(Status))
    {
        InterlockedExchange(&Pcie->FlowRingState, CywFlowRingClosed);
        return Status;
    }
    Pcie->FlowRing.Depth = CYW_FLOWRING_DEPTH;
    Pcie->FlowRing.ItemSize = CYW_FLOWRING_ITEM_SIZE;
    Pcie->FlowRing.Read = 0;
    Pcie->FlowRing.Write = 0;
    Pcie->FlowRing.Id = CYW_RING_FLOW_FIRST_ID;
    Pcie->FlowRing.WriteIndex = Pcie->FlowWriteIndexBase;
    Pcie->FlowRing.ReadIndex = Pcie->FlowReadIndexBase;
    CywRingWriteIndex(Pcie, Pcie->FlowRing.ReadIndex, 0);
    CywRingWriteIndex(Pcie, Pcie->FlowRing.WriteIndex, 0);

    KeAcquireSpinLock(&Pcie->SubmitLock, &OldIrql);
    Create = CywRingReserve(Pcie, Ring);
    if (Create == NULL)
    {
        KeReleaseSpinLock(&Pcie->SubmitLock, OldIrql);
        CywPcieFreeDma(Adapter, &Pcie->FlowRing.Memory);
        InterlockedExchange(&Pcie->FlowRingState, CywFlowRingClosed);
        return STATUS_DEVICE_BUSY;
    }
    RtlZeroMemory(Create, sizeof(*Create));
    Create->Header.MsgType = MSGBUF_TYPE_FLOW_RING_CREATE;
    Create->Header.RequestId = 0;
    RtlCopyMemory(Create->Da, Adapter->ConnectedBssid, 6);
    RtlCopyMemory(Create->Sa, Adapter->CurrentAddress, 6);
    Create->Tid = CYW_FLOWRING_TID_BE;
    Create->FlowRingId = Pcie->FlowRing.Id;
    Create->MaxItems = CYW_FLOWRING_DEPTH;
    Create->ItemSize = CYW_FLOWRING_ITEM_SIZE;
    Create->RingAddress.Low = CywLow32(Pcie->FlowRing.Memory.Pa);
    Create->RingAddress.High = CywHigh32(Pcie->FlowRing.Memory.Pa);
    CywRingCommit(Pcie, Ring);
    KeReleaseSpinLock(&Pcie->SubmitLock, OldIrql);
    return STATUS_PENDING;
}

static
NTSTATUS
CywMsgbufTxLocked(
    _In_ PCYW_ADAPTER Adapter,
    _In_reads_bytes_(EthLen) PUCHAR Eth,
    _In_ ULONG EthLen)
{
    PCYW_PCIE Pcie = Adapter->Pcie;
    PMSGBUF_TX_POST Post;
    PUCHAR Buffer;
    NDIS_PHYSICAL_ADDRESS Physical;
    ULONG PktId;

    if (EthLen < 14 || EthLen > CYW_DATA_BUFFER_SIZE)
    {
        return STATUS_INVALID_PARAMETER;
    }
    if (InterlockedCompareExchange(&Pcie->FlowRingState, 0, 0) != CywFlowRingOpen)
    {
        return STATUS_DEVICE_NOT_READY;
    }

    Buffer = CywPoolAlloc(&Pcie->TxPool, &PktId, &Physical);
    if (Buffer == NULL)
    {
        return STATUS_DEVICE_BUSY;
    }
    Post = CywRingReserve(Pcie, &Pcie->FlowRing);
    if (Post == NULL)
    {
        CywPoolFromId(&Pcie->TxPool, PktId, TRUE);
        return STATUS_DEVICE_BUSY;
    }
    RtlCopyMemory(Buffer, Eth + 14, EthLen - 14);
    RtlZeroMemory(Post, sizeof(*Post));
    Post->Header.MsgType = MSGBUF_TYPE_TX_POST;
    Post->Header.RequestId = PktId;
    RtlCopyMemory(Post->EtherHeader, Eth, 14);
    Post->Flags = MSGBUF_PKT_FLAGS_FRAME_802_3 |
                  (UCHAR)(CywFlowRingLookup(Adapter, Eth, 0) << MSGBUF_PKT_FLAGS_PRIO_SHIFT);
    Post->SegmentCount = 1;
    Post->Data.Low = CywLow32(Physical);
    Post->Data.High = CywHigh32(Physical);
    Post->DataLength = (USHORT)(EthLen - 14);
    CywRingCommit(Pcie, &Pcie->FlowRing);
    InterlockedIncrement(&Pcie->TxPosted);
    InterlockedIncrement64(&Adapter->TxOkCount);
    return STATUS_SUCCESS;
}

NTSTATUS
CywMsgbufSendData(
    _In_ PCYW_ADAPTER Adapter,
    _In_reads_bytes_(EthLen) PUCHAR Eth,
    _In_ ULONG EthLen)
{
    PCYW_PCIE Pcie = Adapter->Pcie;
    KIRQL OldIrql;
    LONG State = InterlockedCompareExchange(&Pcie->FlowRingState, 0, 0);
    NTSTATUS Status;

    if (State == CywFlowRingClosed)
    {
        if (InterlockedExchange(&Pcie->FlowRingRefused, 0) != 0)
        {
            return STATUS_UNSUCCESSFUL;
        }
        Status = CywFlowRingStart(Adapter);
        if (!NT_SUCCESS(Status) && Status != STATUS_PENDING && Status != STATUS_DEVICE_NOT_READY)
        {
            return Status;
        }
        /* The ring is opening; requeue this frame until the create completes. */
        return STATUS_DEVICE_BUSY;
    }
    if (State != CywFlowRingOpen)
    {
        return STATUS_DEVICE_BUSY;
    }

    KeAcquireSpinLock(&Pcie->TxLock, &OldIrql);
    Status = CywMsgbufTxLocked(Adapter, Eth, EthLen);
    KeReleaseSpinLock(&Pcie->TxLock, OldIrql);
    if (Status == STATUS_DEVICE_NOT_READY)
    {
        Status = STATUS_DEVICE_BUSY;
    }
    return Status;
}

NTSTATUS
CywMsgbufSendNb(
    _In_ PCYW_ADAPTER Adapter,
    _In_ PNET_BUFFER Nb)
{
    UCHAR Frame[CYW_DATA_BUFFER_SIZE];
    ULONG EthLen;

    EthLen = CywBuildEthFromNbl(Nb, Frame, sizeof(Frame));
    if (EthLen == 0 || !CywTxAllowed(Adapter, Frame, EthLen))
    {
        return (EthLen == 0) ? STATUS_INVALID_PARAMETER : STATUS_INVALID_DEVICE_STATE;
    }
    return CywMsgbufSendData(Adapter, Frame, EthLen);
}

static
VOID
CywCompletionDrain(
    _In_ PCYW_ADAPTER Adapter,
    _In_ PCYW_MSGRING Ring,
    _Inout_ PNET_BUFFER_LIST *ChainHead,
    _Inout_ PNET_BUFFER_LIST *ChainTail,
    _Inout_ PULONG ChainCount)
{
    PCYW_PCIE Pcie = Adapter->Pcie;
    PUCHAR Item;

    while ((Item = CywRingNextComplete(Pcie, Ring)) != NULL)
    {
        PMSGBUF_HEADER Header = (PMSGBUF_HEADER)Item;

        switch (Header->MsgType)
        {
            case MSGBUF_TYPE_IOCTL_CMPLT:
            {
                PMSGBUF_IOCTL_COMPLETE Complete = (PMSGBUF_IOCTL_COMPLETE)Item;
                PUCHAR Buffer = CywPoolFromId(&Pcie->CtrlPool, Header->RequestId, TRUE);

                if (Pcie->IoctlRespPosted > 0)
                {
                    Pcie->IoctlRespPosted--;
                }
                if (Complete->TransId != Pcie->IoctlTransId)
                {
                    DPRINT1("CYW: dropping the response to IOCTL %lu, transaction %u (waiting for %u)\n",
                            Complete->Cmd, Complete->TransId, Pcie->IoctlTransId);
                }
                else if (InterlockedCompareExchange(&Pcie->IoctlPending, 0, 1) == 1)
                {
                    ULONG Copy = min((ULONG)Complete->ResponseLength, CYW_CTRL_BUFFER_SIZE);

                    Pcie->IoctlFirmwareStatus = (SHORT)Complete->Completion.Status;
                    Pcie->IoctlResponseLost = (Buffer == NULL && Copy != 0);
                    Pcie->IoctlResponseLength = (Buffer != NULL) ? Copy : 0;
                    if (Buffer != NULL && Copy != 0)
                    {
                        RtlCopyMemory(Pcie->IoctlRequest.Va, Buffer, Copy);
                    }
                    KeSetEvent(&Pcie->IoctlEvent, IO_NO_INCREMENT, FALSE);
                }
                break;
            }

            case MSGBUF_TYPE_WL_EVENT:
            {
                PMSGBUF_EVENT Event = (PMSGBUF_EVENT)Item;
                PUCHAR Buffer = CywPoolFromId(&Pcie->CtrlPool, Header->RequestId, TRUE);

                if (Pcie->EventPosted > 0)
                {
                    Pcie->EventPosted--;
                }
                if (Buffer != NULL && Event->EventLength != 0 &&
                    Pcie->RxDataOffset < CYW_CTRL_BUFFER_SIZE)
                {
                    CywProcessEventFrame(Adapter, Buffer + Pcie->RxDataOffset,
                                         min((ULONG)Event->EventLength,
                                             CYW_CTRL_BUFFER_SIZE - Pcie->RxDataOffset));
                }
                break;
            }

            case MSGBUF_TYPE_TX_STATUS:
            {
                KIRQL OldIrql;

                KeAcquireSpinLock(&Pcie->TxLock, &OldIrql);
                CywPoolFromId(&Pcie->TxPool, Header->RequestId, TRUE);
                KeReleaseSpinLock(&Pcie->TxLock, OldIrql);
                if (Pcie->TxPosted > 0)
                {
                    InterlockedDecrement(&Pcie->TxPosted);
                }
                break;
            }

            case MSGBUF_TYPE_RX_CMPLT:
            {
                PMSGBUF_RX_COMPLETE Complete = (PMSGBUF_RX_COMPLETE)Item;
                PUCHAR Buffer = CywPoolFromId(&Pcie->RxPool, Header->RequestId, TRUE);

                if (Pcie->RxPosted > 0)
                {
                    Pcie->RxPosted--;
                }
                Pcie->RxCompletedSinceUpdate++;
                if (Buffer != NULL &&
                    (Complete->Flags & MSGBUF_PKT_FLAGS_FRAME_MASK) == MSGBUF_PKT_FLAGS_FRAME_802_11)
                {
                    DPRINT1("CYW: dropping an unexpected 802.11 receive frame\n");
                }
                else if (Buffer != NULL && Complete->DataLength != 0)
                {
                    ULONG Offset = Complete->DataOffset ? Complete->DataOffset : Pcie->RxDataOffset;
                    ULONG DataLength = Complete->DataLength;
                    PNET_BUFFER_LIST Nbl;

                    if (Offset <= CYW_DATA_BUFFER_SIZE && DataLength <= CYW_DATA_BUFFER_SIZE - Offset)
                    {
                        Nbl = CywRxEthernet(Adapter, Buffer + Offset, DataLength);
                        CywRxChainAppend(ChainHead, ChainTail, ChainCount, Nbl);
                        if (*ChainCount >= CYW_RX_INDICATE_BATCH)
                        {
                            CywRxChainFlush(Adapter, ChainHead, ChainTail, ChainCount);
                        }
                    }
                }
                break;
            }

            case MSGBUF_TYPE_FLOW_RING_CREATE_CMPLT:
            {
                PMSGBUF_STATUS Status = (PMSGBUF_STATUS)Item;

                if (Status->Completion.Status != 0)
                {
                    DPRINT1("CYW: the firmware refused the flow ring (%u)\n", Status->Completion.Status);
                    InterlockedExchange(&Pcie->FlowRingRefused, 1);
                    InterlockedExchange(&Pcie->FlowRingState, CywFlowRingClosed);
                }
                else
                {
                    InterlockedExchange(&Pcie->FlowRingState, CywFlowRingOpen);
                }
                break;
            }

            case MSGBUF_TYPE_FLOW_RING_DELETE_CMPLT:
                InterlockedExchange(&Pcie->FlowRingState, CywFlowRingClosed);
                break;

            case MSGBUF_TYPE_IOCTLPTR_REQ_ACK:
                break;

            case MSGBUF_TYPE_D2H_MAILBOX_DATA:
            {
                PMSGBUF_D2H_MAILBOX Mailbox = (PMSGBUF_D2H_MAILBOX)Item;

                CywPcieHandleMailbox(Adapter, Mailbox->Data);
                break;
            }

            case MSGBUF_TYPE_GEN_STATUS:
            case MSGBUF_TYPE_RING_STATUS:
            {
                PMSGBUF_STATUS Report = (PMSGBUF_STATUS)Item;

                if (Report->Completion.Status != 0)
                {
                    DPRINT1("CYW: firmware %s status %u ring %u\n",
                            Header->MsgType == MSGBUF_TYPE_GEN_STATUS ? "general" : "ring",
                            Report->Completion.Status, Report->Completion.FlowRingId);
                }
                break;
            }

            default:
                DPRINT1("CYW: unhandled completion message type 0x%02x\n", Header->MsgType);
                break;
        }

        CywRingConsume(Pcie, Ring);
    }
}

VOID
CywMsgbufService(
    _In_ PCYW_ADAPTER Adapter)
{
    PCYW_PCIE Pcie = Adapter->Pcie;
    PNET_BUFFER_LIST ChainHead = NULL;
    PNET_BUFFER_LIST ChainTail = NULL;
    ULONG ChainCount = 0;

    if (InterlockedCompareExchange(&Pcie->Up, 0, 0) == 0)
    {
        return;
    }

    CywPcieServiceMailbox(Adapter);
    CywCompletionDrain(Adapter, &Pcie->Rings[CYW_RING_CONTROL_COMPLETE], &ChainHead, &ChainTail, &ChainCount);
    CywCompletionDrain(Adapter, &Pcie->Rings[CYW_RING_TX_COMPLETE], &ChainHead, &ChainTail, &ChainCount);
    CywCompletionDrain(Adapter, &Pcie->Rings[CYW_RING_RX_COMPLETE], &ChainHead, &ChainTail, &ChainCount);
    CywRxChainFlush(Adapter, &ChainHead, &ChainTail, &ChainCount);

    CywPostControlBuffers(Adapter);
    if (Pcie->RxCompletedSinceUpdate >= CYW_MSGBUF_RXBUFPOST_THRESHOLD || Pcie->RxPosted == 0)
    {
        Pcie->RxCompletedSinceUpdate = 0;
        CywPostReceiveBuffers(Adapter);
    }
    CywPcieReadConsole(Pcie);
}

NTSTATUS
CywMsgbufInitialize(
    _In_ PCYW_ADAPTER Adapter)
{
    PCYW_PCIE Pcie = Adapter->Pcie;
    NTSTATUS Status;

    Pcie->RxPostTarget = min((ULONG)Pcie->MaxRxBufPost, CYW_RX_SLOTS);
    KeInitializeEvent(&Pcie->IoctlEvent, NotificationEvent, FALSE);

    Status = CywRingsInitialize(Adapter);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }
    Status = CywScratchInitialize(Adapter);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    Status = CywPcieAllocateDma(Adapter, CYW_CTRL_BUFFER_SIZE, &Pcie->IoctlRequest);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }
    Status = CywPoolCreate(Adapter, &Pcie->CtrlPool, CYW_CTRL_SLOTS, CYW_CTRL_BUFFER_SIZE, CYW_PKTID_CTRL_BASE);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }
    Status = CywPoolCreate(Adapter, &Pcie->RxPool, CYW_RX_SLOTS, CYW_DATA_BUFFER_SIZE, CYW_PKTID_RX_BASE);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }
    Status = CywPoolCreate(Adapter, &Pcie->TxPool, CYW_TX_SLOTS, CYW_DATA_BUFFER_SIZE, CYW_PKTID_TX_BASE);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    CywPostControlBuffers(Adapter);
    CywPostReceiveBuffers(Adapter);
    return STATUS_SUCCESS;
}

VOID
CywMsgbufRelease(
    _In_ PCYW_ADAPTER Adapter)
{
    PCYW_PCIE Pcie = Adapter->Pcie;
    ULONG Index;

    if (Pcie == NULL)
    {
        return;
    }
    InterlockedExchange(&Pcie->FlowRingState, CywFlowRingClosed);
    CywPoolDestroy(Adapter, &Pcie->CtrlPool);
    CywPoolDestroy(Adapter, &Pcie->RxPool);
    CywPoolDestroy(Adapter, &Pcie->TxPool);
    CywPcieFreeDma(Adapter, &Pcie->IoctlRequest);
    CywPcieFreeDma(Adapter, &Pcie->FlowRing.Memory);
    for (Index = 0; Index < CYW_RING_COMMON_COUNT; Index++)
    {
        CywPcieFreeDma(Adapter, &Pcie->Rings[Index].Memory);
    }
    CywPcieFreeDma(Adapter, &Pcie->Scratch);
    CywPcieFreeDma(Adapter, &Pcie->RingUpdate);
    CywPcieFreeDma(Adapter, &Pcie->IndexBuffer);
}

VOID
NTAPI
CywPcieThread(
    _In_ PVOID Context)
{
    PCYW_ADAPTER Adapter = (PCYW_ADAPTER)Context;
    PCYW_PCIE Pcie = Adapter->Pcie;
    LARGE_INTEGER Wait;

    while (InterlockedCompareExchange(&Adapter->BusThreadStop, 0, 0) == 0)
    {
        InterlockedExchange(&Pcie->InterruptPending, 0);
        CywMsgbufService(Adapter);
        CywDrainTxQueue(Adapter);
        CywPcieSetInterrupts(Pcie, TRUE);

        if (InterlockedCompareExchange(&Pcie->InterruptPending, 0, 0) != 0)
        {
            continue;
        }
        if (InterlockedCompareExchange(&Pcie->IoctlPending, 0, 0) != 0 ||
            InterlockedCompareExchange(&Pcie->FlowRingState, 0, 0) == CywFlowRingOpening)
        {
            Wait.QuadPart = -1LL * 10000LL;
        }
        else
        {
            Wait.QuadPart = CYW_PCIE_POLL_INTERVAL;
        }
        KeWaitForSingleObject(&Adapter->BusEvent, Executive, KernelMode, FALSE, &Wait);
    }

    KeSetEvent(&Adapter->BusThreadExited, IO_NO_INCREMENT, FALSE);
    PsTerminateSystemThread(STATUS_SUCCESS);
}
