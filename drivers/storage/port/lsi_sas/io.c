/*
 * PROJECT:     LiberNT Storage Drivers
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     LSI Fusion-MPT SCSI I/O, task management and reply handling
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif193@gmail.com>
 */

#include "lsi_sas.h"

VOID
LsiCompleteRequest(
    _In_ PLSI_DEVICE_EXTENSION Device,
    _In_ PSCSI_REQUEST_BLOCK Srb,
    _In_ UCHAR SrbStatus)
{
    Srb->SrbStatus = SrbStatus;
    StorPortNotification(RequestComplete, Device, Srb);
}

static
VOID
LsiAssignContext(
    _In_ PLSI_DEVICE_EXTENSION Device,
    _In_ PLSI_SLOT Slot)
{
    Device->Sequence++;
    Slot->Context = ((Device->Sequence << LSI_CONTEXT_SLOT_BITS) | (ULONG)(Slot - Device->Slots)) &
                    MPI_CONTEXT_REPLY_CONTEXT_MASK;
}

static
PLSI_SLOT
LsiSlotFromContext(
    _In_ PLSI_DEVICE_EXTENSION Device,
    _In_ ULONG Context)
{
    PLSI_SLOT Slot;
    ULONG Index = Context & (LSI_MAX_SLOTS - 1);

    if (Index >= Device->SlotCount)
        return NULL;

    Slot = &Device->Slots[Index];
    if (Slot->Type == LSI_SLOT_FREE || Slot->Context != Context)
        return NULL;
    return Slot;
}

static
PLSI_SLOT
LsiAllocateSlot(
    _In_ PLSI_DEVICE_EXTENSION Device,
    _In_ UCHAR Type,
    _In_ PSCSI_REQUEST_BLOCK Srb)
{
    ULONG Count = Device->SlotCount - 1;
    ULONG Index;
    ULONG Candidate;
    PLSI_SLOT Slot;

    for (Index = 0; Index < Count; Index++)
    {
        Candidate = (Device->NextSlot + Index) % Count;
        Slot = &Device->Slots[Candidate];
        if (Slot->Type == LSI_SLOT_FREE)
        {
            Device->NextSlot = (Candidate + 1) % Count;
            Slot->Type = Type;
            Slot->Srb = Srb;
            Slot->Aborted = FALSE;
            LsiAssignContext(Device, Slot);
            return Slot;
        }
    }
    return NULL;
}

static
VOID
LsiFreeSlot(
    _In_ PLSI_SLOT Slot)
{
    Slot->Srb = NULL;
    Slot->Context = 0;
    Slot->Aborted = FALSE;
    Slot->Type = LSI_SLOT_FREE;
}

static
VOID
LsiPostRequest(
    _In_ PLSI_DEVICE_EXTENSION Device,
    _In_ PLSI_SLOT Slot)
{
    KeMemoryBarrier();
    LsiWriteRegister(Device, MPI_REQUEST_POST_FIFO_OFFSET, (ULONG)Slot->FramePhysical);
}

static
UCHAR
LsiBuildSgl(
    _In_ PLSI_DEVICE_EXTENSION Device,
    _In_ PLSI_SLOT Slot,
    _In_opt_ PSTOR_SCATTER_GATHER_LIST Sgl,
    _In_ BOOLEAN WriteToDevice)
{
    PMPI_SGE_SIMPLE64 Sge;
    PMPI_SGE_CHAIN64 Chain;
    PUCHAR Segment;
    ULONGLONG NextPhysical;
    ULONG BaseFlags;
    ULONG Flags;
    ULONG Capacity;
    ULONG NextCapacity;
    ULONG Remaining;
    ULONG Count;
    ULONG NextCount;
    ULONG Element;
    ULONG SegmentIndex;
    ULONG Index;
    UCHAR ChainOffset = 0;

    BaseFlags = MPI_SGE_FLAGS_SIMPLE_ELEMENT | MPI_SGE_FLAGS_64_BIT_ADDRESSING;
    if (WriteToDevice)
        BaseFlags |= MPI_SGE_FLAGS_HOST_TO_IOC;

    Segment = Slot->Frame + sizeof(MPI_SCSI_IO_REQUEST);
    Capacity = (Device->FrameSize - sizeof(MPI_SCSI_IO_REQUEST)) / sizeof(MPI_SGE_SIMPLE64);
    Remaining = (Sgl != NULL) ? Sgl->NumberOfElements : 0;

    if (Remaining == 0)
    {
        Sge = (PMPI_SGE_SIMPLE64)Segment;
        Sge->FlagsLength = (BaseFlags | MPI_SGE_FLAGS_LAST_ELEMENT | MPI_SGE_FLAGS_END_OF_BUFFER |
                            MPI_SGE_FLAGS_END_OF_LIST) << MPI_SGE_FLAGS_SHIFT;
        Sge->AddressLow = 0;
        Sge->AddressHigh = 0;
        return 0;
    }

    Element = 0;
    SegmentIndex = 0;
    for (;;)
    {
        Count = (Remaining <= Capacity) ? Remaining : Capacity - 1;
        Sge = (PMPI_SGE_SIMPLE64)Segment;
        for (Index = 0; Index < Count; Index++, Element++)
        {
            Flags = BaseFlags;
            if (Index == Count - 1)
            {
                Flags |= MPI_SGE_FLAGS_LAST_ELEMENT;
                if (Count == Remaining)
                    Flags |= MPI_SGE_FLAGS_END_OF_BUFFER | MPI_SGE_FLAGS_END_OF_LIST;
            }
            Sge[Index].FlagsLength = (Flags << MPI_SGE_FLAGS_SHIFT) |
                                     (Sgl->List[Element].Length & MPI_SGE_LENGTH_MASK);
            Sge[Index].AddressLow = Sgl->List[Element].PhysicalAddress.LowPart;
            Sge[Index].AddressHigh = (ULONG)Sgl->List[Element].PhysicalAddress.HighPart;
        }

        Remaining -= Count;
        if (Remaining == 0)
            break;

        if (SegmentIndex == 0)
            ChainOffset = (UCHAR)((sizeof(MPI_SCSI_IO_REQUEST) + Count * sizeof(MPI_SGE_SIMPLE64)) / sizeof(ULONG));

        NextCapacity = Device->FrameSize / sizeof(MPI_SGE_SIMPLE64);
        NextCount = (Remaining <= NextCapacity) ? Remaining : NextCapacity - 1;
        NextPhysical = Slot->FramePhysical + (ULONGLONG)Device->FrameSize * (SegmentIndex + 1);

        Chain = (PMPI_SGE_CHAIN64)&Sge[Count];
        Chain->Length = (USHORT)(NextCount * sizeof(MPI_SGE_SIMPLE64));
        Chain->NextChainOffset = 0;
        if (Remaining > NextCapacity)
        {
            Chain->Length += sizeof(MPI_SGE_CHAIN64);
            Chain->NextChainOffset = (UCHAR)(NextCount * sizeof(MPI_SGE_SIMPLE64) / sizeof(ULONG));
        }
        Chain->Flags = MPI_SGE_FLAGS_CHAIN_ELEMENT | MPI_SGE_FLAGS_64_BIT_ADDRESSING;
        Chain->AddressLow = (ULONG)NextPhysical;
        Chain->AddressHigh = (ULONG)(NextPhysical >> 32);

        SegmentIndex++;
        Segment = Slot->Frame + Device->FrameSize * SegmentIndex;
        Capacity = NextCapacity;
    }

    return ChainOffset;
}

VOID
LsiStartScsi(
    _In_ PLSI_DEVICE_EXTENSION Device,
    _In_ PSCSI_REQUEST_BLOCK Srb)
{
    PMPI_SCSI_IO_REQUEST Request;
    PSTOR_SCATTER_GATHER_LIST Sgl = NULL;
    STOR_LOCK_HANDLE LockHandle;
    PLSI_SLOT Slot;
    ULONG Direction;
    ULONG Control;
    UCHAR Status = SRB_STATUS_PENDING;

    if (Srb->PathId >= Device->BusCount)
    {
        LsiCompleteRequest(Device, Srb, SRB_STATUS_INVALID_PATH_ID);
        return;
    }
    if (Srb->TargetId >= Device->TargetCount)
    {
        LsiCompleteRequest(Device, Srb, SRB_STATUS_INVALID_TARGET_ID);
        return;
    }
    if (Srb->Lun >= SCSI_MAXIMUM_LOGICAL_UNITS)
    {
        LsiCompleteRequest(Device, Srb, SRB_STATUS_INVALID_LUN);
        return;
    }

    Direction = Srb->SrbFlags & (SRB_FLAGS_DATA_IN | SRB_FLAGS_DATA_OUT);
    if (Srb->CdbLength == 0 || Srb->CdbLength > sizeof(Request->CDB) ||
        (Srb->DataTransferLength != 0 &&
         (Direction == 0 || Direction == (SRB_FLAGS_DATA_IN | SRB_FLAGS_DATA_OUT))))
    {
        LsiCompleteRequest(Device, Srb, SRB_STATUS_INVALID_REQUEST);
        return;
    }

    if (Srb->DataTransferLength != 0)
    {
        Sgl = StorPortGetScatterGatherList(Device, Srb);
        if (Sgl == NULL || Sgl->NumberOfElements == 0 || Sgl->NumberOfElements > Device->MaxSge)
        {
            LsiCompleteRequest(Device, Srb, SRB_STATUS_INVALID_REQUEST);
            return;
        }
        Control = (Direction == SRB_FLAGS_DATA_IN) ? MPI_SCSIIO_CONTROL_READ : MPI_SCSIIO_CONTROL_WRITE;
    }
    else
    {
        Control = MPI_SCSIIO_CONTROL_NODATATRANSFER;
    }

    if (Srb->SrbFlags & SRB_FLAGS_QUEUE_ACTION_ENABLE)
    {
        if (Srb->QueueAction == SRB_HEAD_OF_QUEUE_TAG_REQUEST)
            Control |= MPI_SCSIIO_CONTROL_HEADOFQ;
        else if (Srb->QueueAction == SRB_ORDERED_QUEUE_TAG_REQUEST)
            Control |= MPI_SCSIIO_CONTROL_ORDEREDQ;
        else
            Control |= MPI_SCSIIO_CONTROL_SIMPLEQ;
    }
    else
    {
        Control |= MPI_SCSIIO_CONTROL_SIMPLEQ;
    }

    StorPortAcquireSpinLock(Device, InterruptLock, NULL, &LockHandle);

    Slot = Device->Operational ? LsiAllocateSlot(Device, LSI_SLOT_SCSI, Srb) : NULL;
    if (Slot == NULL)
    {
        Status = Device->Operational ? SRB_STATUS_BUSY : SRB_STATUS_NO_HBA;
    }
    else
    {
        Request = (PMPI_SCSI_IO_REQUEST)Slot->Frame;
        RtlZeroMemory(Request, sizeof(*Request));
        Request->TargetID = Srb->TargetId;
        Request->Bus = Srb->PathId;
        Request->Function = MPI_FUNCTION_SCSI_IO_REQUEST;
        Request->CDBLength = Srb->CdbLength;
        if (!(Srb->SrbFlags & SRB_FLAGS_DISABLE_AUTOSENSE) &&
            Srb->SenseInfoBuffer != NULL && Srb->SenseInfoBufferLength != 0)
        {
            Request->SenseBufferLength = (UCHAR)min(Srb->SenseInfoBufferLength, LSI_SENSE_LENGTH);
        }
        Request->MsgFlags = MPI_SCSIIO_MSGFLGS_SENSE_WIDTH_32 | MPI_SCSIIO_MSGFLGS_SENSE_LOC_HOST;
        Request->MsgContext = Slot->Context;
        Request->LUN[1] = Srb->Lun;
        Request->Control = Control;
        RtlCopyMemory(Request->CDB, Srb->Cdb, Srb->CdbLength);
        Request->DataLength = Srb->DataTransferLength;
        Request->SenseBufferLowAddr = Slot->SensePhysical;
        Request->ChainOffset = LsiBuildSgl(Device, Slot, Sgl, Direction == SRB_FLAGS_DATA_OUT);

        LsiPostRequest(Device, Slot);
    }

    StorPortReleaseSpinLock(Device, &LockHandle);

    if (Status != SRB_STATUS_PENDING)
        LsiCompleteRequest(Device, Srb, Status);
}

static
PLSI_SLOT
LsiFindSlotForSrb(
    _In_ PLSI_DEVICE_EXTENSION Device,
    _In_opt_ PSCSI_REQUEST_BLOCK Srb)
{
    ULONG Index;

    if (Srb == NULL)
        return NULL;

    for (Index = 0; Index < Device->SlotCount; Index++)
    {
        if (Device->Slots[Index].Type == LSI_SLOT_SCSI && Device->Slots[Index].Srb == Srb)
            return &Device->Slots[Index];
    }
    return NULL;
}

static
VOID
LsiBuildTask(
    _In_ PLSI_DEVICE_EXTENSION Device,
    _In_ PLSI_SLOT Slot,
    _In_ UCHAR TaskType,
    _In_ UCHAR PathId,
    _In_ UCHAR TargetId,
    _In_ UCHAR Lun,
    _In_ ULONG TaskMsgContext)
{
    PMPI_SCSI_TASK_MGMT_REQUEST Request = (PMPI_SCSI_TASK_MGMT_REQUEST)Slot->Frame;

    RtlZeroMemory(Request, sizeof(*Request));
    Request->TargetID = TargetId;
    Request->Bus = PathId;
    Request->Function = MPI_FUNCTION_SCSI_TASK_MGMT;
    Request->TaskType = TaskType;
    Request->MsgContext = Slot->Context;
    Request->LUN[1] = Lun;
    Request->TaskMsgContext = TaskMsgContext;
}

VOID
LsiStartTask(
    _In_ PLSI_DEVICE_EXTENSION Device,
    _In_ PSCSI_REQUEST_BLOCK Srb)
{
    STOR_LOCK_HANDLE LockHandle;
    PLSI_SLOT Victim = NULL;
    PLSI_SLOT Slot;
    ULONG TaskMsgContext = 0;
    UCHAR TaskType;
    UCHAR Status = SRB_STATUS_PENDING;

    switch (Srb->Function)
    {
        case SRB_FUNCTION_ABORT_COMMAND:
            TaskType = MPI_SCSITASKMGMT_TASKTYPE_ABORT_TASK;
            break;
        case SRB_FUNCTION_RESET_DEVICE:
            TaskType = MPI_SCSITASKMGMT_TASKTYPE_TARGET_RESET;
            break;
        case SRB_FUNCTION_RESET_LOGICAL_UNIT:
            TaskType = MPI_SCSITASKMGMT_TASKTYPE_LOGICAL_UNIT_RESET;
            break;
        default:
            TaskType = MPI_SCSITASKMGMT_TASKTYPE_RESET_BUS;
            break;
    }

    StorPortAcquireSpinLock(Device, InterruptLock, NULL, &LockHandle);

    if (!Device->Operational)
    {
        Status = SRB_STATUS_NO_HBA;
    }
    else
    {
        if (TaskType == MPI_SCSITASKMGMT_TASKTYPE_ABORT_TASK)
        {
            Victim = LsiFindSlotForSrb(Device, Srb->NextSrb);
            if (Victim == NULL)
                Status = SRB_STATUS_ABORT_FAILED;
            else
                TaskMsgContext = Victim->Context;
        }

        if (Status == SRB_STATUS_PENDING)
        {
            Slot = LsiAllocateSlot(Device, LSI_SLOT_TASK, Srb);
            if (Slot == NULL)
            {
                Status = SRB_STATUS_BUSY;
            }
            else
            {
                if (Victim != NULL)
                    Victim->Aborted = TRUE;
                LsiBuildTask(Device, Slot, TaskType, Srb->PathId, Srb->TargetId, Srb->Lun, TaskMsgContext);
                LsiPostRequest(Device, Slot);
            }
        }
    }

    StorPortReleaseSpinLock(Device, &LockHandle);

    if (Status != SRB_STATUS_PENDING)
        LsiCompleteRequest(Device, Srb, Status);
}

static
UCHAR
LsiTerminatedStatus(
    _In_ PLSI_SLOT Slot)
{
    return Slot->Aborted ? SRB_STATUS_ABORTED : SRB_STATUS_BUS_RESET;
}

static
VOID
LsiCompleteScsiReply(
    _In_ PLSI_DEVICE_EXTENSION Device,
    _In_ PLSI_SLOT Slot,
    _In_ PMPI_SCSI_IO_REPLY Reply)
{
    PSCSI_REQUEST_BLOCK Srb = Slot->Srb;
    USHORT IocStatus = Reply->IOCStatus & MPI_IOCSTATUS_MASK;
    UCHAR ScsiState = Reply->SCSIState;
    UCHAR ScsiStatus = Reply->SCSIStatus;
    ULONG TransferCount = Reply->TransferCount;
    ULONG SenseCount = Reply->SenseCount;
    ULONG Length;
    UCHAR Status;

    Srb->ScsiStatus = SCSISTAT_GOOD;

    switch (IocStatus)
    {
        case MPI_IOCSTATUS_SUCCESS:
        case MPI_IOCSTATUS_SCSI_RECOVERED_ERROR:
        case MPI_IOCSTATUS_SCSI_DATA_UNDERRUN:
        case MPI_IOCSTATUS_SCSI_RESIDUAL_MISMATCH:
        case MPI_IOCSTATUS_SCSI_DATA_OVERRUN:
            if (ScsiState & MPI_SCSI_STATE_TERMINATED)
            {
                Status = LsiTerminatedStatus(Slot);
            }
            else if (ScsiState & MPI_SCSI_STATE_NO_SCSI_STATUS)
            {
                Status = SRB_STATUS_ERROR;
            }
            else
            {
                Srb->ScsiStatus = ScsiStatus;
                if (ScsiStatus != MPI_SCSI_STATUS_SUCCESS)
                    Status = SRB_STATUS_ERROR;
                else if (IocStatus == MPI_IOCSTATUS_SCSI_DATA_OVERRUN ||
                         TransferCount < Srb->DataTransferLength)
                    Status = SRB_STATUS_DATA_OVERRUN;
                else
                    Status = SRB_STATUS_SUCCESS;
            }
            if (Status != SRB_STATUS_SUCCESS && TransferCount < Srb->DataTransferLength)
                Srb->DataTransferLength = TransferCount;
            break;

        case MPI_IOCSTATUS_SCSI_DEVICE_NOT_THERE:
            Status = SRB_STATUS_SELECTION_TIMEOUT;
            break;

        case MPI_IOCSTATUS_SCSI_INVALID_TARGETID:
            Status = SRB_STATUS_INVALID_TARGET_ID;
            break;

        case MPI_IOCSTATUS_SCSI_INVALID_BUS:
            Status = SRB_STATUS_INVALID_PATH_ID;
            break;

        case MPI_IOCSTATUS_SCSI_TASK_TERMINATED:
        case MPI_IOCSTATUS_SCSI_IOC_TERMINATED:
        case MPI_IOCSTATUS_SCSI_EXT_TERMINATED:
            Status = LsiTerminatedStatus(Slot);
            break;

        case MPI_IOCSTATUS_BUSY:
        case MPI_IOCSTATUS_INSUFFICIENT_RESOURCES:
            Status = SRB_STATUS_BUSY;
            break;

        default:
            DPRINT1("lsi_sas: %u:%u:%u CDB 0x%02x IOCStatus 0x%04x LogInfo 0x%08lx\n",
                    Srb->PathId, Srb->TargetId, Srb->Lun, Srb->Cdb[0], Reply->IOCStatus, Reply->IOCLogInfo);
            Status = SRB_STATUS_ERROR;
            break;
    }

    if (Status == SRB_STATUS_ERROR &&
        Srb->ScsiStatus == MPI_SCSI_STATUS_CHECK_CONDITION &&
        (ScsiState & MPI_SCSI_STATE_AUTOSENSE_VALID) &&
        Srb->SenseInfoBuffer != NULL &&
        !(Srb->SrbFlags & SRB_FLAGS_DISABLE_AUTOSENSE))
    {
        Length = min(SenseCount, min((ULONG)Srb->SenseInfoBufferLength, LSI_SENSE_LENGTH));
        RtlCopyMemory(Srb->SenseInfoBuffer, Slot->Sense, Length);
        Srb->SenseInfoBufferLength = (UCHAR)Length;
        Status |= SRB_STATUS_AUTOSENSE_VALID;
    }

    LsiFreeSlot(Slot);
    LsiCompleteRequest(Device, Srb, Status);
}

static
VOID
LsiCompleteTaskReply(
    _In_ PLSI_DEVICE_EXTENSION Device,
    _In_ PLSI_SLOT Slot,
    _In_ PMPI_SCSI_TASK_MGMT_REPLY Reply)
{
    PSCSI_REQUEST_BLOCK Srb = Slot->Srb;
    BOOLEAN Succeeded;

    Succeeded = ((Reply->IOCStatus & MPI_IOCSTATUS_MASK) == MPI_IOCSTATUS_SUCCESS) &&
                (Reply->ResponseCode == MPI_SCSITASKMGMT_RSP_TM_COMPLETE ||
                 Reply->ResponseCode == MPI_SCSITASKMGMT_RSP_TM_SUCCEEDED);

    if (Slot->Type == LSI_SLOT_INTERNAL)
    {
        Device->InternalTaskSucceeded = Succeeded;
        Device->InternalTaskDone = TRUE;
        LsiFreeSlot(Slot);
        return;
    }

    LsiFreeSlot(Slot);
    if (Succeeded)
        LsiCompleteRequest(Device, Srb, SRB_STATUS_SUCCESS);
    else if (Srb->Function == SRB_FUNCTION_ABORT_COMMAND)
        LsiCompleteRequest(Device, Srb, SRB_STATUS_ABORT_FAILED);
    else
        LsiCompleteRequest(Device, Srb, SRB_STATUS_ERROR);
}

static
VOID
LsiProcessAddressReply(
    _In_ PLSI_DEVICE_EXTENSION Device,
    _In_ ULONG ReplyPhysical)
{
    PMPI_DEFAULT_REPLY Reply;
    PLSI_SLOT Slot;
    ULONG Offset;
    ULONG Context;

    Offset = ReplyPhysical - Device->ReplyFramesPhysical;
    if (ReplyPhysical < Device->ReplyFramesPhysical ||
        Offset >= Device->ReplyFrameCount * LSI_REPLY_FRAME_SIZE ||
        (Offset % LSI_REPLY_FRAME_SIZE) != 0)
    {
        DPRINT1("lsi_sas: reply frame 0x%08lx is outside the reply pool\n", ReplyPhysical);
        return;
    }

    Reply = (PMPI_DEFAULT_REPLY)(Device->ReplyFrames + Offset);
    Context = Reply->MsgContext;
    Slot = LsiSlotFromContext(Device, Context);
    if (Slot != NULL)
    {
        if (Reply->Function == MPI_FUNCTION_SCSI_IO_REQUEST && Slot->Type == LSI_SLOT_SCSI)
            LsiCompleteScsiReply(Device, Slot, (PMPI_SCSI_IO_REPLY)Reply);
        else if (Reply->Function == MPI_FUNCTION_SCSI_TASK_MGMT &&
                 (Slot->Type == LSI_SLOT_TASK || Slot->Type == LSI_SLOT_INTERNAL))
            LsiCompleteTaskReply(Device, Slot, (PMPI_SCSI_TASK_MGMT_REPLY)Reply);
    }

    LsiWriteRegister(Device, MPI_REPLY_FREE_FIFO_OFFSET, ReplyPhysical);
}

static
VOID
LsiProcessContextReply(
    _In_ PLSI_DEVICE_EXTENSION Device,
    _In_ ULONG Descriptor)
{
    PSCSI_REQUEST_BLOCK Srb;
    PLSI_SLOT Slot;
    ULONG Context;

    if (((Descriptor & MPI_CONTEXT_REPLY_TYPE_MASK) >> MPI_CONTEXT_REPLY_TYPE_SHIFT) !=
        MPI_CONTEXT_REPLY_TYPE_SCSI_INIT)
    {
        return;
    }

    Context = Descriptor & MPI_CONTEXT_REPLY_CONTEXT_MASK;
    Slot = LsiSlotFromContext(Device, Context);
    if (Slot == NULL || Slot->Type != LSI_SLOT_SCSI)
        return;

    Srb = Slot->Srb;
    Srb->ScsiStatus = SCSISTAT_GOOD;
    LsiFreeSlot(Slot);
    LsiCompleteRequest(Device, Srb, SRB_STATUS_SUCCESS);
}

VOID
LsiProcessReplies(
    _In_ PLSI_DEVICE_EXTENSION Device)
{
    ULONG Descriptor;

    for (;;)
    {
        Descriptor = LsiReadRegister(Device, MPI_REPLY_POST_FIFO_OFFSET);
        if (Descriptor == LSI_EMPTY_REPLY)
            break;

        KeMemoryBarrier();
        if (Descriptor & MPI_ADDRESS_REPLY_A_BIT)
            LsiProcessAddressReply(Device, Descriptor << 1);
        else
            LsiProcessContextReply(Device, Descriptor);
    }
}

VOID
LsiFailOutstanding(
    _In_ PLSI_DEVICE_EXTENSION Device,
    _In_ UCHAR SrbStatus)
{
    PSCSI_REQUEST_BLOCK Srb;
    PLSI_SLOT Slot;
    ULONG Index;

    for (Index = 0; Index < Device->SlotCount; Index++)
    {
        Slot = &Device->Slots[Index];
        if (Slot->Type == LSI_SLOT_FREE)
            continue;

        Srb = Slot->Srb;
        if (Slot->Type == LSI_SLOT_INTERNAL)
        {
            Device->InternalTaskSucceeded = FALSE;
            Device->InternalTaskDone = TRUE;
        }
        LsiFreeSlot(Slot);
        if (Srb != NULL)
            LsiCompleteRequest(Device, Srb, SrbStatus);
    }
}

BOOLEAN
LsiResetBusLocked(
    _In_ PLSI_DEVICE_EXTENSION Device,
    _In_ ULONG PathId)
{
    PLSI_SLOT Slot = &Device->Slots[Device->SlotCount - 1];
    ULONG Elapsed;

    if (Device->Operational &&
        Slot->Type == LSI_SLOT_FREE &&
        (LsiReadRegister(Device, MPI_DOORBELL_OFFSET) & MPI_IOC_STATE_MASK) == MPI_IOC_STATE_OPERATIONAL)
    {
        Slot->Type = LSI_SLOT_INTERNAL;
        Slot->Srb = NULL;
        Slot->Aborted = FALSE;
        LsiAssignContext(Device, Slot);
        Device->InternalTaskDone = FALSE;
        Device->InternalTaskSucceeded = FALSE;

        LsiBuildTask(Device, Slot, MPI_SCSITASKMGMT_TASKTYPE_RESET_BUS, (UCHAR)PathId, 0, 0, 0);
        LsiPostRequest(Device, Slot);

        for (Elapsed = 0; Elapsed < LSI_TASK_TIMEOUT_MS * 10; Elapsed++)
        {
            LsiProcessReplies(Device);
            if (Device->InternalTaskDone)
                break;
            StorPortStallExecution(100);
        }

        if (Device->InternalTaskDone && Device->InternalTaskSucceeded)
            return TRUE;

        DPRINT1("lsi_sas: bus %lu reset task did not complete, reinitializing the IOC\n", PathId);
    }

    return LsiReinitializeIoc(Device);
}
