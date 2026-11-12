/*
 * PROJECT:     LiberNT Storport NVMe miniport
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     I/O queue pairs, submission slots and interrupt service
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "stornvme.h"

/*
 * Vector spread mirrors the Linux mapping: message 0 carries the admin
 * queue, I/O queues take dedicated vectors when the grant is large enough
 * and share the tail vectors round-robin otherwise.
 */
ULONG
NvmeQueueVector(_In_ PNVME_DEVICE_EXTENSION Device, _In_ ULONG QueueId)
{
    if (!Device->MessageInterrupts || Device->MessageCount <= 1)
        return 0;
    if (Device->MessageCount > Device->IoQueueCount)
        return QueueId;
    return 1 + ((QueueId - 1) % (Device->MessageCount - 1));
}

BOOLEAN
NvmeCreateIoQueues(_In_ PNVME_DEVICE_EXTENSION Device)
{
    NVME_COMMAND Command;
    ULONG Index;

    for (Index = 0; Index < Device->IoQueueCount; Index++)
    {
        PNVME_QUEUE Queue = &Device->IoQueues[Index];

        Queue->QueueId = Index + 1;
        Queue->Vector = NvmeQueueVector(Device, Queue->QueueId);
        Queue->SqTail = 0;
        Queue->SqHead = 0;
        Queue->CqHead = 0;
        Queue->Phase = 1;
        Queue->SqDoorbell = NVME_SQ_DOORBELL(Device->DoorbellStride, Queue->QueueId);
        Queue->CqDoorbell = NVME_CQ_DOORBELL(Device->DoorbellStride, Queue->QueueId);
        if (Device->Transport && Device->Transport->PrepareIoQueue)
            Device->Transport->PrepareIoQueue(Device, Queue);
        RtlZeroMemory(Queue->Sq, Queue->Entries * sizeof(NVME_COMMAND));
        RtlZeroMemory(Queue->Cq, Queue->Entries * sizeof(NVME_COMPLETION));
        RtlZeroMemory(Queue->Outstanding, sizeof(Queue->Outstanding));

        RtlZeroMemory(&Command, sizeof(Command));
        NVME_COMMAND_SET_OPCODE(&Command, NVME_ADMIN_CREATE_CQ, 0);
        Command.PRP1 = Queue->CqPhysical;
        Command.CDW10 = ((Queue->Entries - 1) << 16) | Queue->QueueId;
        Command.CDW11 = (Queue->Vector << 16) | 0x3;
        if (!NvmeAdminCommandSync(Device, &Command, NULL))
            goto Failure;

        RtlZeroMemory(&Command, sizeof(Command));
        NVME_COMMAND_SET_OPCODE(&Command, NVME_ADMIN_CREATE_SQ, 0);
        Command.PRP1 = Queue->SqPhysical;
        Command.CDW10 = ((Queue->Entries - 1) << 16) | Queue->QueueId;
        Command.CDW11 = (Queue->QueueId << 16) | 0x1;
        if (!NvmeAdminCommandSync(Device, &Command, NULL))
            goto Failure;

        Queue->Created = TRUE;
    }
    DPRINT1("stornvme: %lu I/O queues on %lu message(s)\n", Device->IoQueueCount, Device->MessageCount);
    return TRUE;

Failure:
    NvmeDisableController(Device);
    for (Index = 0; Index < Device->IoQueueCount; Index++)
        Device->IoQueues[Index].Created = FALSE;
    return FALSE;
}

PNVME_QUEUE
NvmeSelectQueue(_In_ PNVME_DEVICE_EXTENSION Device)
{
#if (NTDDI_VERSION >= NTDDI_WIN7)
    ULONG Processor = KeGetCurrentProcessorNumberEx(NULL);
#else
    ULONG Processor = KeGetCurrentProcessorNumber();
#endif

    return &Device->IoQueues[Processor % Device->IoQueueCount];
}

/*
 * Claims a submission slot and rings the doorbell under the queue's vector
 * lock. The slot index doubles as the NVMe command identifier.
 */
BOOLEAN
NvmeSubmitIoCommand(_In_ PNVME_DEVICE_EXTENSION Device,
                    _In_ PNVME_QUEUE Queue,
                    _In_ PSCSI_REQUEST_BLOCK Srb,
                    _Inout_ PNVME_COMMAND Command)
{
    PNVME_SRB_EXTENSION SrbExtension = (PNVME_SRB_EXTENSION)Srb->SrbExtension;
    NVME_LOCK Lock;
    ULONG NextTail;
    ULONG Slot;

    if (SrbExtension == NULL)
    {
        NvmeCompleteSrb(Device, Srb, SRB_STATUS_ERROR);
        return TRUE;
    }

    NvmeAcquireLock(Device, Queue->Vector, &Lock);
    if (!Device->ControllerStarted || !Queue->Created)
    {
        NvmeReleaseLock(Device, &Lock);
        NvmeCompleteSrb(Device, Srb, SRB_STATUS_NO_DEVICE);
        return TRUE;
    }

    NextTail = (Queue->SqTail + 1) % Queue->Entries;
    if (!Device->Transport && NextTail == Queue->SqHead)
        goto Busy;

    for (Slot = 0; Slot < Queue->Entries; Slot++)
    {
        if (Queue->Outstanding[Slot] == NULL)
            break;
    }
    if (Slot >= Queue->Entries)
        goto Busy;

    Command->CDW0 = (Command->CDW0 & 0xFFFF) | (Slot << 16);
    SrbExtension->QueueId = Queue->QueueId;
    SrbExtension->Slot = Slot;
    Queue->Outstanding[Slot] = Srb;
    if (Device->Transport)
    {
        Device->Transport->Submit(Device, Queue, Slot, Command);
        NvmeReleaseLock(Device, &Lock);
        return TRUE;
    }
    Queue->Sq[Queue->SqTail] = *Command;
    KeMemoryBarrier();
    Queue->SqTail = NextTail;
    NvmeWriteRegister(Device, Queue->SqDoorbell, Queue->SqTail);
    NvmeReleaseLock(Device, &Lock);
    return TRUE;

Busy:
    NvmeReleaseLock(Device, &Lock);
    NvmeCompleteSrb(Device, Srb, SRB_STATUS_BUSY);
    return TRUE;
}

/* Caller holds the queue's vector domain. */
static
ULONG
NvmeProcessIoCompletionsLocked(_In_ PNVME_DEVICE_EXTENSION Device,
                               _In_ PNVME_QUEUE Queue,
                               _Out_ PNVME_COMPLETED_SRB Batch,
                               _In_ ULONG BatchLimit)
{
    BOOLEAN Handled = FALSE;
    ULONG Collected = 0;

    if (!Queue->Created)
        return 0;

    while (Collected < BatchLimit)
    {
        PNVME_COMPLETION Completion = &Queue->Cq[Queue->CqHead];
        USHORT CommandId;
        USHORT Status;

        if (NVME_COMPLETION_PHASE(Completion) != Queue->Phase)
            break;
        KeMemoryBarrier();

        Handled = TRUE;
        CommandId = NVME_COMPLETION_COMMAND_ID(Completion);
        Status = NVME_COMPLETION_STATUS(Completion);
        Queue->SqHead = NVME_COMPLETION_SQ_HEAD(Completion);

        Queue->CqHead = (Queue->CqHead + 1) % Queue->Entries;
        if (Queue->CqHead == 0)
            Queue->Phase ^= 1;
        if (Device->Transport)
            Device->Transport->Completed(Device, Queue, CommandId);

        if (CommandId < Queue->Entries && Queue->Outstanding[CommandId] != NULL)
        {
            Batch[Collected].Srb = Queue->Outstanding[CommandId];
            Batch[Collected].Status = Status;
            Batch[Collected].SlotType = NVME_SLOT_FREE;
            Collected++;
            Queue->Outstanding[CommandId] = NULL;
        }
    }

    if (Handled)
        NvmeWriteRegister(Device, Queue->CqDoorbell, Queue->CqHead);
    return Collected;
}

static
VOID
NvmeCompleteIoBatch(_In_ PNVME_DEVICE_EXTENSION Device,
                    _In_ PNVME_QUEUE Queue,
                    _In_reads_(Collected) PNVME_COMPLETED_SRB Batch,
                    _In_ ULONG Collected)
{
    ULONG Index;

    for (Index = 0; Index < Collected; Index++)
    {
        if (Batch[Index].Status == 0)
        {
            NvmeCompleteSrb(Device, Batch[Index].Srb, SRB_STATUS_SUCCESS);
        }
        else
        {
            DPRINT1("stornvme: queue %lu command failed, status 0x%04x\n",
                    Queue->QueueId, Batch[Index].Status);
            NvmeSetSenseAndComplete(Device, Batch[Index].Srb,
                                    NVME_STATUS_CODE_TYPE(Batch[Index].Status) == NVME_SCT_MEDIA
                                        ? SCSI_SENSE_MEDIUM_ERROR
                                        : SCSI_SENSE_HARDWARE_ERROR,
                                    0x00);
        }
    }
}

BOOLEAN
NvmeDrainIoQueue(_In_ PNVME_DEVICE_EXTENSION Device, _In_ PNVME_QUEUE Queue)
{
    NVME_COMPLETED_SRB Batch[NVME_COMPLETION_BATCH];
    NVME_LOCK Lock;
    ULONG Collected;
    ULONG Batches = 0;

    do
    {
        NvmeAcquireLock(Device, Queue->Vector, &Lock);
        Collected = NvmeProcessIoCompletionsLocked(Device, Queue, Batch, NVME_COMPLETION_BATCH);
        NvmeReleaseLock(Device, &Lock);
        NvmeCompleteIoBatch(Device, Queue, Batch, Collected);
    } while (Collected == NVME_COMPLETION_BATCH && ++Batches < NVME_DPC_BATCH_LIMIT);

    return Collected == NVME_COMPLETION_BATCH;
}

/* Takes the queue out of service and fails whatever it still holds. */
VOID
NvmeRetireQueue(_In_ PNVME_DEVICE_EXTENSION Device, _In_ PNVME_QUEUE Queue, _In_ UCHAR SrbStatus)
{
    PSCSI_REQUEST_BLOCK Srbs[NVME_IO_QUEUE_ENTRIES];
    NVME_LOCK Lock;
    ULONG Count = 0;
    ULONG Slot;

    NvmeAcquireLock(Device, Queue->Vector, &Lock);
    Queue->Created = FALSE;
    for (Slot = 0; Slot < Queue->Entries; Slot++)
    {
        if (Queue->Outstanding[Slot] != NULL)
        {
            Srbs[Count++] = Queue->Outstanding[Slot];
            Queue->Outstanding[Slot] = NULL;
        }
    }
    NvmeReleaseLock(Device, &Lock);

    for (Slot = 0; Slot < Count; Slot++)
        NvmeCompleteSrb(Device, Srbs[Slot], SrbStatus);
}

VOID
NTAPI
NvmeAdminDpc(_In_ PSTOR_DPC Dpc,
             _In_ PVOID HwDeviceExtension,
             _In_opt_ PVOID SystemArgument1,
             _In_opt_ PVOID SystemArgument2)
{
    PNVME_DEVICE_EXTENSION Device = (PNVME_DEVICE_EXTENSION)HwDeviceExtension;
    BOOLEAN Again = FALSE;
    NVME_LOCK Lock;
    ULONG Index;

    UNREFERENCED_PARAMETER(Dpc);
    UNREFERENCED_PARAMETER(SystemArgument1);
    UNREFERENCED_PARAMETER(SystemArgument2);

    NvmeDrainAdminQueue(Device);
    if (Device->MessageInterrupts)
        return;

    for (Index = 0; Index < Device->IoQueueCount; Index++)
    {
        PNVME_QUEUE Queue = &Device->IoQueues[Index];

        if (Queue->Created && NvmeDrainIoQueue(Device, Queue))
            Again = TRUE;
    }

    if (Again)
    {
        StorPortIssueDpc(Device, &Device->AdminQueue.Dpc, NULL, NULL);
        return;
    }

    NvmeAcquireLock(Device, 0, &Lock);
    Device->LineMasked = FALSE;
    NvmeWriteRegister(Device, NVME_REG_INTMC, 0xFFFFFFFF);
    NvmeReleaseLock(Device, &Lock);
}

VOID
NTAPI
NvmeIoQueueDpc(_In_ PSTOR_DPC Dpc,
               _In_ PVOID HwDeviceExtension,
               _In_opt_ PVOID SystemArgument1,
               _In_opt_ PVOID SystemArgument2)
{
    PNVME_DEVICE_EXTENSION Device = (PNVME_DEVICE_EXTENSION)HwDeviceExtension;
    PNVME_QUEUE Queue = (PNVME_QUEUE)SystemArgument1;

    UNREFERENCED_PARAMETER(Dpc);
    UNREFERENCED_PARAMETER(SystemArgument2);

    if (NvmeDrainIoQueue(Device, Queue))
        StorPortIssueDpc(Device, &Queue->Dpc, Queue, NULL);
}

/*
 * Interrupt service only schedules the queue DPCs: completing a request
 * releases Storport bookkeeping that cannot happen at device IRQL.
 */
BOOLEAN
NTAPI
NvmeHwMSInterrupt(_In_ PVOID DeviceExtension, _In_ ULONG MessageId)
{
    PNVME_DEVICE_EXTENSION Device = (PNVME_DEVICE_EXTENSION)DeviceExtension;
    ULONG Index;

    if (!Device->InterruptsLive)
        return FALSE;
    if (MessageId == 0)
        StorPortIssueDpc(Device, &Device->AdminQueue.Dpc, NULL, NULL);

    for (Index = 0; Index < Device->IoQueueCount; Index++)
    {
        PNVME_QUEUE Queue = &Device->IoQueues[Index];

        if (Queue->Vector == MessageId && Queue->Created)
            StorPortIssueDpc(Device, &Queue->Dpc, Queue, NULL);
    }
    return TRUE;
}

static
BOOLEAN
NvmeQueueHasWork(_In_ PNVME_QUEUE Queue)
{
    return NVME_COMPLETION_PHASE(&Queue->Cq[Queue->CqHead]) == Queue->Phase;
}

BOOLEAN
NTAPI
NvmeHwInterrupt(_In_ PVOID DeviceExtension)
{
    PNVME_DEVICE_EXTENSION Device = (PNVME_DEVICE_EXTENSION)DeviceExtension;
    BOOLEAN Pending;
    ULONG Index;

    if (!Device->InterruptsLive || Device->LineMasked)
        return FALSE;
    if (Device->Transport)
        return NvmeServiceQueuesAtInterrupt(Device);

    /*
     * A wired interrupt stays asserted until the CQ heads catch up; mask it
     * and let the DPCs drain, they unmask when done.
     */
    Pending = Device->AdminReady && NvmeQueueHasWork(&Device->AdminQueue);
    for (Index = 0; !Pending && Index < Device->IoQueueCount; Index++)
    {
        PNVME_QUEUE Queue = &Device->IoQueues[Index];

        Pending = Queue->Created && NvmeQueueHasWork(Queue);
    }
    if (!Pending)
        return FALSE;

    Device->LineMasked = TRUE;
    NvmeWriteRegister(Device, NVME_REG_INTMS, 0xFFFFFFFF);
    StorPortIssueDpc(Device, &Device->AdminQueue.Dpc, NULL, NULL);
    return TRUE;
}

/*
 * A wired interrupt that the controller cannot mask through INTMS: consume the
 * completions here, under the interrupt lock the caller already holds, so the
 * doorbell writes release the line. Storport queues the SRB completions.
 */
BOOLEAN
NvmeServiceQueuesAtInterrupt(_In_ PNVME_DEVICE_EXTENSION Device)
{
    NVME_COMPLETED_SRB Batch[NVME_COMPLETION_BATCH];
    BOOLEAN Handled = FALSE;
    ULONG Collected;
    ULONG Index;
    ULONG Queue;

    do
    {
        Handled |= Device->AdminReady && NvmeQueueHasWork(&Device->AdminQueue);
        Collected = NvmeProcessAdminCompletionsLocked(Device, Batch, NVME_COMPLETION_BATCH);
        for (Index = 0; Index < Collected; Index++)
        {
            if (Batch[Index].SlotType == NVME_SLOT_SRB_LOG)
                NvmeCompleteLogSenseSrb(Device, Batch[Index].Srb, Batch[Index].Status);
            else
                NvmeCompleteFeatureSrb(Device, Batch[Index].Srb, Batch[Index].Status);
        }
    } while (Collected == NVME_COMPLETION_BATCH);

    for (Queue = 0; Queue < Device->IoQueueCount; Queue++)
    {
        do
        {
            Handled |= Device->IoQueues[Queue].Created && NvmeQueueHasWork(&Device->IoQueues[Queue]);
            Collected = NvmeProcessIoCompletionsLocked(Device, &Device->IoQueues[Queue], Batch, NVME_COMPLETION_BATCH);
            NvmeCompleteIoBatch(Device, &Device->IoQueues[Queue], Batch, Collected);
        } while (Collected == NVME_COMPLETION_BATCH);
    }
    return Handled;
}
