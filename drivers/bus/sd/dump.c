/*
 * PROJECT:     LiberNT SD/SDIO/eMMC Bus Driver
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Polled crash-time storage path for memory cards on SDHCI hosts
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "sdbus.h"
#include <reactos/drivers/dumpstor.h>

#define NDEBUG
#include <debug.h>

#define SD_DUMP_MAX_TRANSFER        0x10000
#define SD_DUMP_CONTEXT_TAG         'pDdS'
#define SD_DUMP_STATE_TRAN          4
#define SD_DUMP_STATE_DATA          5
#define SD_DUMP_STATE_RCV           6
#define SD_DUMP_FLAGS_R1            (SDHCI_CMD_RESP_48 | SDHCI_CMD_CRC_CHECK | SDHCI_CMD_INDEX_CHECK)
#define SD_DUMP_FLAGS_R1B           (SDHCI_CMD_RESP_48_BUSY | SDHCI_CMD_CRC_CHECK | SDHCI_CMD_INDEX_CHECK)

typedef struct _SD_DUMP_CONTEXT
{
    PFDO_EXTENSION Fdo;
    ULONG RelativeAddress;
    ULONG BytesPerSector;
    ULONG64 TotalSectors;
    BOOLEAN HighCapacity;
    BOOLEAN Masked;
    ULONG SavedStatusEnable;
    ULONG SavedSignalEnable;
    UCHAR Block[PAGE_SIZE];
} SD_DUMP_CONTEXT, *PSD_DUMP_CONTEXT;

static NTSTATUS
SdDumpPollStatus(
    _In_ PFDO_EXTENSION Fdo,
    _In_ ULONG Wanted)
{
    ULONG Iteration;
    ULONG Status;

    for (Iteration = 0; Iteration < ROS_DUMP_POLL_ITERATIONS; Iteration++)
    {
        Status = SdBusReadReg32(Fdo, SDHCI_INT_STATUS);
        if (Status & SDHCI_INT_ERROR_MASK)
        {
            DPRINT1("SD dump: controller error 0x%08lx\n", Status);
            return STATUS_IO_DEVICE_ERROR;
        }
        if (Status & Wanted)
        {
            SdBusWriteReg32(Fdo, SDHCI_INT_STATUS, Status & Wanted);
            return STATUS_SUCCESS;
        }
        KeStallExecutionProcessor(1);
    }

    DPRINT1("SD dump: no status 0x%08lx, have 0x%08lx\n", Wanted, SdBusReadReg32(Fdo, SDHCI_INT_STATUS));
    return STATUS_IO_TIMEOUT;
}

static NTSTATUS
SdDumpWaitInhibit(
    _In_ PFDO_EXTENSION Fdo,
    _In_ ULONG Inhibit)
{
    ULONG Iteration;

    for (Iteration = 0; Iteration < ROS_DUMP_POLL_ITERATIONS; Iteration++)
    {
        if (!(SdBusReadReg32(Fdo, SDHCI_PRESENT_STATE) & Inhibit))
            return STATUS_SUCCESS;
        KeStallExecutionProcessor(1);
    }

    return STATUS_IO_TIMEOUT;
}

static VOID
SdDumpResetLines(
    _In_ PFDO_EXTENSION Fdo)
{
    ULONG Iteration;

    SdBusWriteReg8(Fdo, SDHCI_SOFTWARE_RESET, SDHCI_RESET_CMD | SDHCI_RESET_DATA);
    for (Iteration = 0; Iteration < ROS_DUMP_POLL_ITERATIONS; Iteration++)
    {
        if (!(SdBusReadReg8(Fdo, SDHCI_SOFTWARE_RESET) & (SDHCI_RESET_CMD | SDHCI_RESET_DATA)))
            break;
        KeStallExecutionProcessor(1);
    }
    SdBusWriteReg32(Fdo, SDHCI_INT_STATUS, SDHCI_INT_ALL_MASK);
}

static NTSTATUS
SdDumpSendCommand(
    _In_ PFDO_EXTENSION Fdo,
    _In_ UCHAR Command,
    _In_ ULONG Argument,
    _In_ USHORT Flags,
    _Out_opt_ PULONG Response)
{
    NTSTATUS Status;

    Status = SdDumpWaitInhibit(Fdo, SDHCI_PS_CMD_INHIBIT);
    if (!NT_SUCCESS(Status))
        return Status;

    SdBusWriteReg32(Fdo, SDHCI_INT_STATUS, SDHCI_INT_ALL_MASK);
    SdBusWriteReg32(Fdo, SDHCI_ARGUMENT, Argument);
    SdBusWriteReg16(Fdo, SDHCI_COMMAND, SDHCI_MAKE_CMD(Command, Flags));
    Status = SdDumpPollStatus(Fdo, SDHCI_INT_CMD_COMPLETE);
    if (!NT_SUCCESS(Status))
        return Status;

    if (Response != NULL)
        *Response = SdBusReadReg32(Fdo, SDHCI_RESPONSE0);
    return STATUS_SUCCESS;
}

static NTSTATUS
SdDumpWaitForTransferState(
    _In_ PSD_DUMP_CONTEXT Context)
{
    ULONG Iteration;
    ULONG Response;
    ULONG State;
    NTSTATUS Status;

    for (Iteration = 0; Iteration < ROS_DUMP_POLL_ITERATIONS / 100; Iteration++)
    {
        Status = SdDumpSendCommand(Context->Fdo,
                                   SDCMD_SEND_STATUS,
                                   Context->RelativeAddress << 16,
                                   SD_DUMP_FLAGS_R1,
                                   &Response);
        if (!NT_SUCCESS(Status))
            return Status;

        State = (Response >> SD_STATUS_CURRENT_STATE_SHIFT) & 0xF;
        if ((State == SD_DUMP_STATE_TRAN) && (Response & SD_STATUS_READY_FOR_DATA))
            return STATUS_SUCCESS;

        if ((State == SD_DUMP_STATE_DATA) || (State == SD_DUMP_STATE_RCV))
        {
            /* The crash cut a transfer short: the card is still waiting for its stop. */
            Status = SdDumpSendCommand(Context->Fdo, SDCMD_STOP_TRANSMISSION, 0, SD_DUMP_FLAGS_R1B, NULL);
            if (!NT_SUCCESS(Status))
                return Status;
            Status = SdDumpWaitInhibit(Context->Fdo, SDHCI_PS_DATA_INHIBIT);
            if (!NT_SUCCESS(Status))
                return Status;
        }
        KeStallExecutionProcessor(100);
    }

    DPRINT1("SD dump: card stayed in status 0x%08lx\n", Response);
    return STATUS_IO_TIMEOUT;
}

static NTSTATUS
NTAPI
SdDumpPrepare(
    _In_ PVOID Context)
{
    PSD_DUMP_CONTEXT Dump = Context;
    PFDO_EXTENSION Fdo = Dump->Fdo;

    if (!Fdo->RegistersMapped)
        return STATUS_DEVICE_NOT_READY;

    /* Only the user area may take the data; a boot partition left selected must stay intact. */
    if (Fdo->EmmcPartitionConfigValid && (Fdo->EmmcPartitionConfig & EMMC_PARTITION_ACCESS_MASK) != 0)
        return STATUS_INVALID_DEVICE_STATE;

    if (!Dump->Masked)
    {
        Dump->SavedStatusEnable = SdBusReadReg32(Fdo, SDHCI_INT_STATUS_ENABLE);
        Dump->SavedSignalEnable = SdBusReadReg32(Fdo, SDHCI_INT_SIGNAL_ENABLE);
        Dump->Masked = TRUE;
    }
    SdBusWriteReg32(Fdo, SDHCI_INT_SIGNAL_ENABLE, 0);
    SdBusWriteReg32(Fdo, SDHCI_INT_STATUS_ENABLE, SDHCI_INT_ALL_MASK);
    SdDumpResetLines(Fdo);

    return SdDumpWaitForTransferState(Dump);
}

static BOOLEAN
SdDumpCopyBlock(
    _In_ PSD_DUMP_CONTEXT Dump,
    _In_ PHYSICAL_ADDRESS Source)
{
    ULONG Copied = 0;

    /* The source can be any RAM, also above what a 32-bit DMA engine reaches: the CPU moves it. */
    while (Copied < Dump->BytesPerSector)
    {
        ULONG Offset = BYTE_OFFSET(Source.QuadPart);
        ULONG Chunk = min(Dump->BytesPerSector - Copied, PAGE_SIZE - Offset);
        PVOID Virtual = MmGetVirtualForPhysical(Source);

        if (Virtual == NULL)
            return FALSE;
        RtlCopyMemory(&Dump->Block[Copied], Virtual, Chunk);
        Copied += Chunk;
        Source.QuadPart += Chunk;
    }
    return TRUE;
}

static NTSTATUS
SdDumpWriteBlocks(
    _In_ PSD_DUMP_CONTEXT Dump,
    _In_ ULONG64 FirstBlock,
    _In_ ULONG BlockCount,
    _In_ PHYSICAL_ADDRESS Source)
{
    PFDO_EXTENSION Fdo = Dump->Fdo;
    USHORT Mode = SDHCI_TRNS_BLK_CNT_ENABLE;
    UCHAR Command = SDCMD_WRITE_BLOCK;
    ULONG Argument;
    ULONG Response;
    ULONG Block;
    NTSTATUS Status;

    if (BlockCount > 1)
    {
        Mode |= SDHCI_TRNS_MULTI_BLOCK | SDHCI_TRNS_AUTO_CMD12;
        Command = SDCMD_WRITE_MULTIPLE_BLOCK;
    }
    Argument = (ULONG)(Dump->HighCapacity ? FirstBlock : FirstBlock * Dump->BytesPerSector);

    Status = SdDumpWaitInhibit(Fdo, SDHCI_PS_CMD_INHIBIT | SDHCI_PS_DATA_INHIBIT);
    if (!NT_SUCCESS(Status))
        return Status;

    SdBusWriteReg32(Fdo, SDHCI_INT_STATUS, SDHCI_INT_ALL_MASK);
    SdBusWriteReg32(Fdo, SDHCI_BLOCK_SIZE, (BlockCount << 16) | Dump->BytesPerSector);
    SdBusWriteReg32(Fdo, SDHCI_ARGUMENT, Argument);
    SdBusWriteReg32(Fdo,
                    SDHCI_TRANSFER_MODE,
                    ((ULONG)SDHCI_MAKE_CMD(Command, SD_DUMP_FLAGS_R1 | SDHCI_CMD_DATA_PRESENT) << 16) | Mode);
    Status = SdDumpPollStatus(Fdo, SDHCI_INT_CMD_COMPLETE);
    if (!NT_SUCCESS(Status))
        return Status;

    Response = SdBusReadReg32(Fdo, SDHCI_RESPONSE0);
    if (Response & SD_STATUS_ERROR_MASK)
    {
        DPRINT1("SD dump: write of block %I64u refused, status 0x%08lx\n", FirstBlock, Response);
        return STATUS_IO_DEVICE_ERROR;
    }

    for (Block = 0; Block < BlockCount; Block++)
    {
        PULONG Data = (PULONG)Dump->Block;
        ULONG Word;

        if (!SdDumpCopyBlock(Dump, Source))
            return STATUS_INVALID_ADDRESS;
        Source.QuadPart += Dump->BytesPerSector;

        Status = SdDumpPollStatus(Fdo, SDHCI_INT_BUFFER_WRITE_READY);
        if (!NT_SUCCESS(Status))
            return Status;
        for (Word = 0; Word < Dump->BytesPerSector / sizeof(ULONG); Word++)
            SdBusWriteReg32(Fdo, SDHCI_BUFFER_DATA_PORT, Data[Word]);
    }

    return SdDumpPollStatus(Fdo, SDHCI_INT_XFER_COMPLETE);
}

static NTSTATUS
NTAPI
SdDumpWrite(
    _In_ PVOID Context,
    _In_ ULONG64 DiskByteOffset,
    _In_ PHYSICAL_ADDRESS Buffer,
    _In_ ULONG Length)
{
    PSD_DUMP_CONTEXT Dump = Context;
    ULONG64 FirstBlock;
    ULONG BlockCount;
    NTSTATUS Status;

    if ((Length == 0) ||
        (Length > SD_DUMP_MAX_TRANSFER) ||
        (Length % Dump->BytesPerSector) != 0 ||
        (DiskByteOffset % Dump->BytesPerSector) != 0)
    {
        return STATUS_INVALID_PARAMETER;
    }

    FirstBlock = DiskByteOffset / Dump->BytesPerSector;
    BlockCount = Length / Dump->BytesPerSector;
    if ((FirstBlock >= Dump->TotalSectors) || (BlockCount > Dump->TotalSectors - FirstBlock))
        return STATUS_END_OF_MEDIA;

    Status = SdDumpWriteBlocks(Dump, FirstBlock, BlockCount, Buffer);
    if (!NT_SUCCESS(Status))
        SdDumpResetLines(Dump->Fdo);
    return Status;
}

static NTSTATUS
NTAPI
SdDumpFlush(
    _In_ PVOID Context)
{
    PSD_DUMP_CONTEXT Dump = Context;
    NTSTATUS Status;

    Status = SdDumpWaitForTransferState(Dump);
    if (Dump->Masked)
    {
        /* A hibernation that fails goes back to running on the normal, interrupt-driven path. */
        SdBusWriteReg32(Dump->Fdo, SDHCI_INT_STATUS, SDHCI_INT_ALL_MASK);
        SdBusWriteReg32(Dump->Fdo, SDHCI_INT_STATUS_ENABLE, Dump->SavedStatusEnable);
        SdBusWriteReg32(Dump->Fdo, SDHCI_INT_SIGNAL_ENABLE, Dump->SavedSignalEnable);
        Dump->Masked = FALSE;
    }
    return Status;
}

static NTSTATUS
SdBusGetDumpInterface(
    _In_ PPDO_EXTENSION Pdo,
    _Inout_ PROS_STORAGE_DUMP_INTERFACE Interface)
{
    PFDO_EXTENSION Fdo = Pdo->FdoExtension;
    PSD_DUMP_CONTEXT Dump;
    ULONG BytesPerSector = Interface->BytesPerSector;

    if ((Fdo == NULL) || !Fdo->RegistersMapped)
        return STATUS_DEVICE_NOT_READY;

    /* TODO: crash-time path for the BCM2835 SDHost and DesignWare hosts */
    if (Fdo->HostType != SdBusHostSdhci)
        return STATUS_NOT_SUPPORTED;

    if ((Pdo->CardType == SdCardTypeSdio) || (Pdo->TotalSectors == 0))
        return STATUS_NOT_SUPPORTED;
    if (Pdo->WriteProtected)
        return STATUS_MEDIA_WRITE_PROTECTED;
    if ((BytesPerSector == 0) || (BytesPerSector > PAGE_SIZE) || (BytesPerSector != Pdo->BytesPerSector))
        return STATUS_INVALID_PARAMETER;

    /* The kernel keeps the interface for the whole boot, so the context outlives the PDO. */
    Dump = Pdo->DumpContext;
    if (Dump == NULL)
    {
        Dump = ExAllocatePoolZero(NonPagedPool, sizeof(*Dump), SD_DUMP_CONTEXT_TAG);
        if (Dump == NULL)
            return STATUS_INSUFFICIENT_RESOURCES;
        Dump->Fdo = Fdo;
        Dump->RelativeAddress = Pdo->RelativeAddress;
        Dump->BytesPerSector = BytesPerSector;
        Dump->TotalSectors = Pdo->TotalSectors;
        Dump->HighCapacity = Pdo->HighCapacity;
        Pdo->DumpContext = Dump;
    }

    RtlZeroMemory(Interface, sizeof(*Interface));
    Interface->Version = ROS_STORAGE_DUMP_INTERFACE_VERSION;
    Interface->Size = sizeof(*Interface);
    Interface->Context = Dump;
    Interface->BytesPerSector = BytesPerSector;
    Interface->MaximumTransferLength = SD_DUMP_MAX_TRANSFER;
    Interface->Prepare = SdDumpPrepare;
    Interface->WriteRoutine = SdDumpWrite;
    Interface->Flush = SdDumpFlush;
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
SdBusDispatchDeviceControl(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    PSDBUS_COMMON_EXTENSION Common = DeviceObject->DeviceExtension;
    PIO_STACK_LOCATION IoStack = IoGetCurrentIrpStackLocation(Irp);
    NTSTATUS Status = STATUS_INVALID_DEVICE_REQUEST;

    Irp->IoStatus.Information = 0;
    if (!Common->IsFdo &&
        (IoStack->Parameters.DeviceIoControl.IoControlCode == IOCTL_REACTOS_STORAGE_GET_DUMP_INTERFACE))
    {
        if ((Irp->AssociatedIrp.SystemBuffer == NULL) ||
            (IoStack->Parameters.DeviceIoControl.InputBufferLength < sizeof(ROS_STORAGE_DUMP_INTERFACE)) ||
            (IoStack->Parameters.DeviceIoControl.OutputBufferLength < sizeof(ROS_STORAGE_DUMP_INTERFACE)))
        {
            Status = STATUS_BUFFER_TOO_SMALL;
        }
        else
        {
            Status = SdBusGetDumpInterface(DeviceObject->DeviceExtension, Irp->AssociatedIrp.SystemBuffer);
            if (NT_SUCCESS(Status))
                Irp->IoStatus.Information = sizeof(ROS_STORAGE_DUMP_INTERFACE);
        }
    }

    Irp->IoStatus.Status = Status;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return Status;
}
