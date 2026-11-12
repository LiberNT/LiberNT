/*
 * PROJECT:     LiberNT USB Attached SCSI Driver
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     USB Attached SCSI information units, tags, task management and recovery
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "lntuas.h"

#define NDEBUG
#include <debug.h>

#define UAS_TASK_DATA_SENT  0x80

static IO_COMPLETION_ROUTINE UasPhaseCompletion;
static IO_WORKITEM_ROUTINE UasPortResetWorker;

static
VOID
UasReleaseTask(
    _In_ PUAS_TASK Task);

BOOLEAN
UasScanConfiguration(
    _In_ PUSB_CONFIGURATION_DESCRIPTOR Configuration,
    _Out_ PUSB_INTERFACE_DESCRIPTOR *Interface,
    _Out_writes_(UAS_PIPES) PUCHAR Endpoint,
    _Out_ PULONG StreamCount)
{
    PUCHAR At = (PUCHAR)Configuration;
    PUCHAR End = At + Configuration->wTotalLength;
    PUSB_INTERFACE_DESCRIPTOR Candidate = NULL;
    UCHAR LastEndpoint = 0, Found = 0, Index;
    ULONG CandidateStreams = 0, Streams;

    *Interface = NULL;
    *StreamCount = 0;
    RtlZeroMemory(Endpoint, UAS_PIPES);
    while (At + 2 <= End && At[0] >= 2 && At + At[0] <= End)
    {
        if (At[1] == USB_INTERFACE_DESCRIPTOR_TYPE && At[0] >= sizeof(USB_INTERFACE_DESCRIPTOR))
        {
            PUSB_INTERFACE_DESCRIPTOR Descriptor = (PUSB_INTERFACE_DESCRIPTOR)At;

            if (Candidate && Found == (1 << UAS_PIPES) - 1)
                break;
            Candidate = NULL;
            Found = 0;
            CandidateStreams = 0;
            RtlZeroMemory(Endpoint, UAS_PIPES);
            if (Descriptor->bInterfaceClass == UAS_INTERFACE_CLASS &&
                Descriptor->bInterfaceSubClass == UAS_INTERFACE_SUBCLASS &&
                Descriptor->bInterfaceProtocol == UAS_INTERFACE_PROTOCOL && Descriptor->bNumEndpoints == UAS_PIPES)
            {
                Candidate = Descriptor;
            }
        }
        else if (Candidate && At[1] == USB_ENDPOINT_DESCRIPTOR_TYPE && At[0] >= sizeof(USB_ENDPOINT_DESCRIPTOR))
        {
            LastEndpoint = ((PUSB_ENDPOINT_DESCRIPTOR)At)->bEndpointAddress;
        }
        else if (Candidate && At[1] == UAS_PIPE_USAGE_DESCRIPTOR && At[0] >= 4 && LastEndpoint &&
                 At[2] >= 1 && At[2] <= UAS_PIPES)
        {
            Index = At[2] - 1;
            Endpoint[Index] = LastEndpoint;
            Found |= 1 << Index;
        }
        else if (Candidate && At[1] == UAS_COMPANION_DESCRIPTOR && At[0] >= 6 && (At[3] & UAS_COMPANION_STREAMS))
        {
            Streams = 1UL << (At[3] & UAS_COMPANION_STREAMS);
            CandidateStreams = CandidateStreams ? min(CandidateStreams, Streams) : Streams;
        }
        At += At[0];
    }
    if (!Candidate || Found != (1 << UAS_PIPES) - 1 ||
        USB_ENDPOINT_DIRECTION_IN(Endpoint[UAS_PIPE_COMMAND]) ||
        !USB_ENDPOINT_DIRECTION_IN(Endpoint[UAS_PIPE_STATUS]) ||
        !USB_ENDPOINT_DIRECTION_IN(Endpoint[UAS_PIPE_DATA_IN]) ||
        USB_ENDPOINT_DIRECTION_IN(Endpoint[UAS_PIPE_DATA_OUT]))
    {
        RtlZeroMemory(Endpoint, UAS_PIPES);
        return FALSE;
    }
    *Interface = Candidate;
    *StreamCount = CandidateStreams;
    return TRUE;
}

static IO_COMPLETION_ROUTINE UasSyncCompletion;

static
NTSTATUS
NTAPI
UasSyncCompletion(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp,
    _In_reads_opt_(_Inexpressible_("varies")) PVOID Context)
{
    UNREFERENCED_PARAMETER(DeviceObject);
    UNREFERENCED_PARAMETER(Irp);
    KeSetEvent(Context, IO_NO_INCREMENT, FALSE);
    return STATUS_MORE_PROCESSING_REQUIRED;
}

NTSTATUS
UasSyncUrb(
    _In_ PUAS_ADAPTER Adapter,
    _Inout_ PURB Urb)
{
    PIO_STACK_LOCATION Next;
    NTSTATUS Status;
    KEVENT Event;
    PIRP Irp;

    ASSERT(KeGetCurrentIrql() == PASSIVE_LEVEL);

    Irp = IoAllocateIrp(Adapter->LowerDevice->StackSize, FALSE);
    if (!Irp)
        return STATUS_INSUFFICIENT_RESOURCES;
    KeInitializeEvent(&Event, NotificationEvent, FALSE);
    Next = IoGetNextIrpStackLocation(Irp);
    Next->MajorFunction = IRP_MJ_INTERNAL_DEVICE_CONTROL;
    Next->Parameters.DeviceIoControl.IoControlCode = IOCTL_INTERNAL_USB_SUBMIT_URB;
    Next->Parameters.Others.Argument1 = Urb;
    Irp->IoStatus.Status = STATUS_NOT_SUPPORTED;
    IoSetCompletionRoutine(Irp, UasSyncCompletion, &Event, TRUE, TRUE, TRUE);
    if (IoCallDriver(Adapter->LowerDevice, Irp) == STATUS_PENDING)
        KeWaitForSingleObject(&Event, Executive, KernelMode, FALSE, NULL);
    Status = Irp->IoStatus.Status;
    IoFreeIrp(Irp);
    return Status;
}

NTSTATUS
UasResetPort(
    _In_ PUAS_ADAPTER Adapter)
{
    IO_STATUS_BLOCK IoStatus;
    NTSTATUS Status;
    KEVENT Event;
    PIRP Irp;

    ASSERT(KeGetCurrentIrql() == PASSIVE_LEVEL);

    KeInitializeEvent(&Event, NotificationEvent, FALSE);
    Irp = IoBuildDeviceIoControlRequest(IOCTL_INTERNAL_USB_RESET_PORT, Adapter->LowerDevice, NULL, 0, NULL, 0, TRUE,
                                        &Event, &IoStatus);
    if (!Irp)
        return STATUS_INSUFFICIENT_RESOURCES;
    Status = IoCallDriver(Adapter->LowerDevice, Irp);
    if (Status == STATUS_PENDING)
    {
        KeWaitForSingleObject(&Event, Executive, KernelMode, FALSE, NULL);
        Status = IoStatus.Status;
    }
    return Status;
}

static
BOOLEAN
UasIsManagementTask(
    _In_ PUAS_TASK Task)
{
    return Task == &Task->Adapter->ManagementTask;
}

static
BOOLEAN
UasQuiescedLocked(
    _In_ PUAS_ADAPTER Adapter)
{
    return (Adapter->Resetting || Adapter->Stopped) && Adapter->ActiveTasks == 0 && Adapter->DrainingTasks == 0 &&
           Adapter->ManagementTask.State == UasTaskFree;
}

static
BOOLEAN
UasTasksBusyLocked(
    _In_ PUAS_ADAPTER Adapter)
{
    ULONG Index;

    for (Index = 0; Index < Adapter->TaskCount; Index++)
    {
        if (Adapter->Task[Index].State == UasTaskActive)
            return TRUE;
    }
    return FALSE;
}

static
BOOLEAN
UasHasData(
    _In_ PSCSI_REQUEST_BLOCK Srb)
{
    return Srb->DataTransferLength != 0 &&
           ((Srb->SrbFlags & SRB_FLAGS_UNSPECIFIED_DIRECTION) == SRB_FLAGS_DATA_IN ||
            (Srb->SrbFlags & SRB_FLAGS_UNSPECIFIED_DIRECTION) == SRB_FLAGS_DATA_OUT);
}

static
BOOLEAN
UasDataIn(
    _In_ PSCSI_REQUEST_BLOCK Srb)
{
    return (Srb->SrbFlags & SRB_FLAGS_UNSPECIFIED_DIRECTION) == SRB_FLAGS_DATA_IN;
}

static
USHORT
UasIuTag(
    _In_ PUCHAR Iu)
{
    return (USHORT)((Iu[2] << 8) | Iu[3]);
}

static
USBD_PIPE_HANDLE
UasPipe(
    _In_ PUAS_ADAPTER Adapter,
    _In_ ULONG Role,
    _In_ USHORT Tag)
{
    ASSERT(Role < UAS_PIPES);
    if (Adapter->Streams && Role != UAS_PIPE_COMMAND)
    {
        ASSERT(Tag >= 1 && Tag <= Adapter->StreamCount);
        ASSERT(Adapter->StreamInfo[Role][Tag - 1].StreamID == Tag);
        return Adapter->StreamInfo[Role][Tag - 1].PipeHandle;
    }
    return Adapter->Pipe[Role];
}

static
BOOLEAN
UasSubmit(
    _In_ PUAS_TASK Task,
    _In_ ULONG Phase,
    _In_ ULONG Role,
    _In_ ULONG TransferFlags,
    _In_ ULONG Length,
    _In_opt_ PVOID Buffer,
    _In_opt_ PMDL Mdl)
{
    PUAS_ADAPTER Adapter = Task->Adapter;
    PIRP Irp = Task->Irp[Phase];
    struct _URB_BULK_OR_INTERRUPT_TRANSFER *Urb = &Task->Urb[Phase];
    PIO_STACK_LOCATION Next;
    LONG Flags;

    ASSERT(KeGetCurrentIrql() <= DISPATCH_LEVEL);
    ASSERT(Phase < UAS_PHASES);
    ASSERT(Task->State == UasTaskActive || (Task->State == UasTaskDraining && Phase == UAS_PHASE_STATUS));
    ASSERT(Task->Pending > 0);
    ASSERT((Buffer != NULL) != (Mdl != NULL));

    IoReuseIrp(Irp, STATUS_SUCCESS);
    RtlZeroMemory(Urb, sizeof(*Urb));
    Urb->Hdr.Length = sizeof(*Urb);
    Urb->Hdr.Function = URB_FUNCTION_BULK_OR_INTERRUPT_TRANSFER;
    Urb->PipeHandle = UasPipe(Adapter, Role, Task->Tag);
    Urb->TransferFlags = TransferFlags;
    Urb->TransferBufferLength = Length;
    Urb->TransferBuffer = Buffer;
    Urb->TransferBufferMDL = Mdl;
    Next = IoGetNextIrpStackLocation(Irp);
    Next->MajorFunction = IRP_MJ_INTERNAL_DEVICE_CONTROL;
    Next->Parameters.DeviceIoControl.IoControlCode = IOCTL_INTERNAL_USB_SUBMIT_URB;
    Next->Parameters.Others.Argument1 = Urb;
    IoSetCompletionRoutine(Irp, UasPhaseCompletion, Task, TRUE, TRUE, TRUE);

    Flags = InterlockedAnd(&Task->Flags, ~UAS_TASK_DONE(Phase));
    ASSERT(Flags & UAS_TASK_DONE(Phase));
    if (Flags & UAS_TASK_FAILED)
    {
        InterlockedOr(&Task->Flags, UAS_TASK_DONE(Phase));
        return FALSE;
    }
    InterlockedIncrement(&Task->Pending);
    IoCallDriver(Adapter->LowerDevice, Irp);
    return TRUE;
}

static
VOID
UasSubmitStatus(
    _In_ PUAS_TASK Task)
{
    RtlZeroMemory(Task->StatusIu, UAS_STATUS_IU_SIZE);
    UasSubmit(Task, UAS_PHASE_STATUS, UAS_PIPE_STATUS, USBD_TRANSFER_DIRECTION_IN | USBD_SHORT_TRANSFER_OK,
              UAS_STATUS_IU_SIZE, Task->StatusIu, NULL);
}

static
VOID
UasSubmitData(
    _In_ PUAS_TASK Task)
{
    BOOLEAN In = UasDataIn(Task->Srb);

    ASSERT(Task->Flags & UAS_TASK_DATA);
    ASSERT(Task->DataMdl != NULL);
    InterlockedOr(&Task->Flags, UAS_TASK_DATA_SENT);
    UasSubmit(Task, UAS_PHASE_DATA, In ? UAS_PIPE_DATA_IN : UAS_PIPE_DATA_OUT,
              In ? USBD_TRANSFER_DIRECTION_IN | USBD_SHORT_TRANSFER_OK : USBD_TRANSFER_DIRECTION_OUT,
              Task->Srb->DataTransferLength, NULL, Task->DataMdl);
}

static
VOID
UasCancelPhases(
    _In_ PUAS_TASK Task,
    _In_ LONG Flags)
{
    ULONG Phase;

    for (Phase = 0; Phase < UAS_PHASES; Phase++)
    {
        if (!(Flags & UAS_TASK_DONE(Phase)))
            IoCancelIrp(Task->Irp[Phase]);
    }
}

static
VOID
UasFailTask(
    _In_ PUAS_TASK Task)
{
    LONG Flags = InterlockedOr(&Task->Flags, UAS_TASK_FAILED);

    if (!(Flags & UAS_TASK_FAILED))
        UasCancelPhases(Task, Flags);
}

static
BOOLEAN
UasTakeSense(
    _In_ PSCSI_REQUEST_BLOCK Srb,
    _In_ PUCHAR Iu,
    _In_ ULONG Length)
{
    ULONG SenseLength;

    ASSERT(Iu[0] == UAS_IU_SENSE && Length >= UAS_SENSE_IU_HEADER);
    Srb->ScsiStatus = Iu[6];
    if (Iu[6] == UAS_STATUS_GOOD)
        return TRUE;
    Srb->SrbStatus = SRB_STATUS_ERROR;
    SenseLength = min((ULONG)((Iu[14] << 8) | Iu[15]), Length - UAS_SENSE_IU_HEADER);
    if (Iu[6] == UAS_STATUS_CHECK_CONDITION && SenseLength && !(Srb->SrbFlags & SRB_FLAGS_DISABLE_AUTOSENSE) &&
        Srb->SenseInfoBuffer && Srb->SenseInfoBufferLength)
    {
        SenseLength = min(SenseLength, Srb->SenseInfoBufferLength);
        RtlCopyMemory(Srb->SenseInfoBuffer, Iu + UAS_SENSE_IU_HEADER, SenseLength);
        Srb->SenseInfoBufferLength = (UCHAR)SenseLength;
        Srb->SrbStatus |= SRB_STATUS_AUTOSENSE_VALID;
    }
    return FALSE;
}

static
BOOLEAN
UasResponseSucceeded(
    _In_ PUAS_TASK Task)
{
    PUCHAR Iu = Task->StatusIu;
    ULONG Length = Task->Urb[UAS_PHASE_STATUS].TransferBufferLength;

    if (Length < UAS_RESPONSE_IU_SIZE || Iu[0] != UAS_IU_RESPONSE || UasIuTag(Iu) != Task->Tag)
        return FALSE;
    if (Iu[7] != UAS_RC_TMF_COMPLETE && Iu[7] != UAS_RC_TMF_SUCCEEDED)
    {
        DPRINT1("lntuas: task management function %x was answered with response code %x\n", Task->CommandIu[4],
                Iu[7]);
        return FALSE;
    }
    return TRUE;
}

static
VOID
UasStatusArrived(
    _In_ PUAS_TASK Task)
{
    PUCHAR Iu = Task->StatusIu;
    ULONG Length = Task->Urb[UAS_PHASE_STATUS].TransferBufferLength;
    LONG Flags = InterlockedOr(&Task->Flags, 0);

    if (Flags & UAS_TASK_FAILED)
        return;
    if (UasIsManagementTask(Task))
    {
        if (!UasResponseSucceeded(Task))
            UasFailTask(Task);
        return;
    }
    if (Length < UAS_READY_IU_SIZE || UasIuTag(Iu) != Task->Tag)
    {
        DPRINT1("lntuas: tag %u received %lu status bytes for tag %u\n", Task->Tag, Length,
                Length >= UAS_READY_IU_SIZE ? UasIuTag(Iu) : 0);
        UasFailTask(Task);
        return;
    }
    switch (Iu[0])
    {
        case UAS_IU_READ_READY:
        case UAS_IU_WRITE_READY:
            if (Task->Adapter->Streams || !(Flags & UAS_TASK_DATA) || (Flags & UAS_TASK_DATA_SENT) ||
                (Iu[0] == UAS_IU_READ_READY) != UasDataIn(Task->Srb))
            {
                DPRINT1("lntuas: tag %u got ready IU %x without a matching data phase\n", Task->Tag, Iu[0]);
                UasFailTask(Task);
                return;
            }
            UasSubmitData(Task);
            return;

        case UAS_IU_SENSE:
            if (Length < UAS_SENSE_IU_HEADER)
                break;
            if (!UasTakeSense(Task->Srb, Iu, Length))
            {
                Flags = InterlockedOr(&Task->Flags, UAS_TASK_SENSE_ERROR);
                if (!(Flags & UAS_TASK_DONE(UAS_PHASE_DATA)))
                    IoCancelIrp(Task->Irp[UAS_PHASE_DATA]);
            }
            return;
    }
    DPRINT1("lntuas: tag %u answered IU %x\n", Task->Tag, Iu[0]);
    UasFailTask(Task);
}

static
VOID
UasPhaseDone(
    _In_ PUAS_TASK Task,
    _In_ ULONG Phase,
    _In_ NTSTATUS Status)
{
    LONG Flags = InterlockedOr(&Task->Flags, 0);

    if (Task->State == UasTaskDraining)
    {
        ASSERT(Phase == UAS_PHASE_STATUS);
        if (NT_SUCCESS(Status))
            DPRINT1("lntuas: tag %u drained a status IU %x of its cancelled command\n", Task->Tag, Task->StatusIu[0]);
        return;
    }
    if (!NT_SUCCESS(Status))
    {
        if (Phase == UAS_PHASE_DATA && (Flags & UAS_TASK_SENSE_ERROR))
            return;
        if (!(Flags & UAS_TASK_FAILED))
            DPRINT1("lntuas: tag %u phase %lu failed %lx\n", Task->Tag, Phase, Status);
        UasFailTask(Task);
        return;
    }
    switch (Phase)
    {
        case UAS_PHASE_STATUS:
            UasStatusArrived(Task);
            break;

        case UAS_PHASE_COMMAND:
        case UAS_PHASE_DATA:
            if (!Task->Adapter->Streams)
                UasSubmitStatus(Task);
            break;
    }
}

static
VOID
UasIssueTask(
    _In_ PUAS_TASK Task);

static
VOID
UasIssueTasks(
    _In_reads_(Count) PUAS_TASK *Tasks,
    _In_ ULONG Count)
{
    ULONG Index;

    for (Index = 0; Index < Count; Index++)
        UasIssueTask(Tasks[Index]);
}

static
ULONG
UasActivateParkedLocked(
    _In_ PUAS_ADAPTER Adapter,
    _Out_writes_(UAS_MAX_TASKS) PUAS_TASK *Tasks)
{
    ULONG Index, Count = 0;

    ASSERT(!Adapter->Resetting);
    ASSERT(!Adapter->Stopped);
    for (Index = 0; Index < Adapter->TaskCount; Index++)
    {
        if (Adapter->Task[Index].State != UasTaskParked)
            continue;
        ASSERT(Adapter->Task[Index].Srb != NULL);
        Adapter->Task[Index].State = UasTaskActive;
        Adapter->ActiveTasks++;
        Tasks[Count++] = &Adapter->Task[Index];
    }
    return Count;
}

static
VOID
UasQueuePortResetLocked(
    _In_ PUAS_ADAPTER Adapter,
    _Out_ PBOOLEAN Queue)
{
    *Queue = FALSE;
    if (Adapter->PortResetQueued || Adapter->Stopped)
        return;
    Adapter->PortResetQueued = TRUE;
    Adapter->Resetting = TRUE;
    KeClearEvent(&Adapter->PortResetIdle);
    *Queue = TRUE;
}

static
VOID
UasStartTaskManagement(
    _In_ PUAS_ADAPTER Adapter,
    _In_ UCHAR Function,
    _In_ USHORT ManagedTag,
    _In_ UCHAR Lun)
{
    PUAS_TASK Task = &Adapter->ManagementTask;
    PUCHAR Iu = Task->CommandIu;
    LARGE_INTEGER Due;
    BOOLEAN Failed;
    KIRQL OldIrql;

    ASSERT(KeGetCurrentIrql() <= DISPATCH_LEVEL);
    ASSERT(Function == UAS_TMF_LOGICAL_UNIT_RESET || Function == UAS_TMF_ABORT_TASK ||
           Function == UAS_TMF_I_T_NEXUS_RESET);

    KeAcquireSpinLock(&Adapter->Lock, &OldIrql);
    ASSERT(Task->State == UasTaskActive);
    ASSERT(Adapter->ManagementStarted);
    ASSERT(Task->Pending == 0);
    ASSERT(Function != UAS_TMF_I_T_NEXUS_RESET || (Adapter->NexusPending && Adapter->Resetting));
    Failed = Adapter->Stopped || Adapter->PortResetQueued;
    Task->Flags = UAS_TASK_ALL_DONE | (Failed ? UAS_TASK_FAILED : 0);
    Task->Pending = 1;
    Adapter->TmfTimedOut = FALSE;
    Adapter->TmfDeadline = KeQueryInterruptTime() + (ULONGLONG)UAS_TMF_TIMEOUT_MS * 10000ULL;
    KeReleaseSpinLock(&Adapter->Lock, OldIrql);
    if (!Failed)
    {
        Due.QuadPart = -(LONGLONG)UAS_TMF_TIMEOUT_MS * 10000LL;
        KeSetTimer(&Adapter->TmfTimer, Due, &Adapter->TmfDpc);
    }

    RtlZeroMemory(Iu, UAS_TASK_IU_SIZE);
    Iu[0] = UAS_IU_TASK_MANAGEMENT;
    Iu[2] = (UCHAR)(Task->Tag >> 8);
    Iu[3] = (UCHAR)Task->Tag;
    Iu[4] = Function;
    Iu[6] = (UCHAR)(ManagedTag >> 8);
    Iu[7] = (UCHAR)ManagedTag;
    Iu[9] = Lun;
    if (Adapter->Streams)
        UasSubmitStatus(Task);
    UasSubmit(Task, UAS_PHASE_COMMAND, UAS_PIPE_COMMAND, USBD_TRANSFER_DIRECTION_OUT, UAS_TASK_IU_SIZE, Iu, NULL);
    UasReleaseTask(Task);
}

static
VOID
UasReleaseDrainHold(
    _In_ PUAS_TASK Task)
{
    PUAS_ADAPTER Adapter = Task->Adapter;
    BOOLEAN Quiesced;
    KIRQL OldIrql;

    KeAcquireSpinLock(&Adapter->Lock, &OldIrql);
    ASSERT(Task->State == UasTaskDraining);
    ASSERT(Task->DrainHeld);
    Task->DrainHeld = FALSE;
    if (Task->DrainDone)
    {
        ASSERT(Adapter->DrainingTasks > 0);
        Task->State = UasTaskFree;
        Task->DrainAborted = FALSE;
        Task->DrainDone = FALSE;
        Adapter->DrainingTasks--;
    }
    Quiesced = UasQuiescedLocked(Adapter);
    KeReleaseSpinLock(&Adapter->Lock, OldIrql);

    if (Quiesced)
        KeSetEvent(&Adapter->Quiesced, IO_NO_INCREMENT, FALSE);
}

static
ULONG
UasPurgeActiveLocked(
    _In_ PUAS_ADAPTER Adapter,
    _Out_writes_(UAS_MAX_TASKS) PUAS_TASK *Cancel)
{
    ULONG Index, Count = 0;

    ASSERT(Adapter->Resetting);
    for (Index = 0; Index < Adapter->TaskCount; Index++)
    {
        if (Adapter->Task[Index].State != UasTaskActive)
            continue;
        if (Adapter->Streams)
            InterlockedOr(&Adapter->Task[Index].Flags, UAS_TASK_DRAIN);
        Cancel[Count++] = &Adapter->Task[Index];
    }
    return Count;
}

static
VOID
UasFinishManagement(
    _In_ PUAS_TASK Task)
{
    PUAS_ADAPTER Adapter = Task->Adapter;
    PUAS_TASK Parked[UAS_MAX_TASKS], Drained[UAS_MAX_TASKS], Cancel[UAS_MAX_TASKS], Abort = NULL;
    PSCSI_REQUEST_BLOCK Srb = NULL;
    BOOLEAN Succeeded = !(Task->Flags & UAS_TASK_FAILED);
    UCHAR Function = Task->CommandIu[4];
    UCHAR NextFunction = 0, SrbStatus = SRB_STATUS_SUCCESS;
    BOOLEAN Queue = FALSE, Quiesced, Escalate, ResetFirst;
    ULONG Count = 0, DrainCount = 0, CancelCount = 0, Index;
    UCHAR Lun = 0;
    KIRQL OldIrql;

    ASSERT(Task->Pending == 0);
    KeCancelTimer(&Adapter->TmfTimer);

    KeAcquireSpinLock(&Adapter->Lock, &OldIrql);
    ASSERT(Task->State == UasTaskActive);
    ASSERT(Adapter->ManagementStarted);
    ASSERT(!Adapter->TmfTimedOut || !Succeeded);
    ResetFirst = Adapter->TmfTimedOut && !Adapter->Stopped && !Adapter->PortResetQueued;
    Escalate = !Succeeded && !ResetFirst && Function != UAS_TMF_I_T_NEXUS_RESET && !Adapter->Stopped &&
               !Adapter->PortResetQueued;
    if (Function == UAS_TMF_LOGICAL_UNIT_RESET)
    {
        ASSERT(Adapter->Resetting && Adapter->ResetSrb != NULL);
        ASSERT(!Adapter->NexusPending);
        if (!Escalate && !ResetFirst)
        {
            Srb = Adapter->ResetSrb;
            SrbStatus = Succeeded ? SRB_STATUS_SUCCESS : SRB_STATUS_ERROR;
            Lun = Srb->Lun;
            Adapter->ResetSrb = NULL;
            for (Index = 0; Index < Adapter->TaskCount; Index++)
            {
                if (Adapter->Task[Index].State == UasTaskDraining && Adapter->Task[Index].DrainLun == Lun &&
                    !Adapter->Task[Index].DrainAborted)
                {
                    ASSERT(!Adapter->Task[Index].DrainHeld);
                    Adapter->Task[Index].DrainAborted = TRUE;
                    Adapter->Task[Index].DrainHeld = TRUE;
                    Drained[DrainCount++] = &Adapter->Task[Index];
                }
            }
        }
    }
    else if (Function == UAS_TMF_ABORT_TASK)
    {
        Abort = Adapter->AbortTask;
        ASSERT(!Adapter->NexusPending);
        ASSERT(Abort != NULL && Abort->State == UasTaskDraining && Abort->DrainAborted && Abort->DrainHeld);
        if (Abort->DrainDone)
        {
            ASSERT(Adapter->DrainingTasks > 0);
            Abort->State = UasTaskFree;
            Abort->DrainAborted = FALSE;
            Abort->DrainDone = FALSE;
            Abort->DrainHeld = FALSE;
            Adapter->DrainingTasks--;
        }
        else
        {
            Drained[DrainCount++] = Abort;
        }
        Abort = NULL;
    }
    else
    {
        ASSERT(Function == UAS_TMF_I_T_NEXUS_RESET);
        ASSERT(Adapter->NexusPending && Adapter->Resetting);
        ASSERT(Adapter->AbortTask == NULL);
        Adapter->NexusPending = FALSE;
        if (Succeeded || Adapter->Stopped)
        {
            Srb = Adapter->ResetSrb;
            SrbStatus = Succeeded ? SRB_STATUS_SUCCESS : SRB_STATUS_NO_DEVICE;
            Adapter->ResetSrb = NULL;
        }
        for (Index = 0; Succeeded && Index < Adapter->TaskCount; Index++)
        {
            if (Adapter->Task[Index].State != UasTaskDraining)
                continue;
            ASSERT(!Adapter->Task[Index].DrainHeld);
            Adapter->Task[Index].DrainAborted = TRUE;
            Adapter->Task[Index].DrainHeld = TRUE;
            Drained[DrainCount++] = &Adapter->Task[Index];
        }
    }
    Adapter->AbortTask = NULL;
    if (Escalate)
    {
        Adapter->NexusPending = TRUE;
        Adapter->Resetting = TRUE;
        CancelCount = UasPurgeActiveLocked(Adapter, Cancel);
    }
    else if (!Succeeded)
    {
        UasQueuePortResetLocked(Adapter, &Queue);
        Adapter->NexusAfterReset = Queue && ResetFirst && Function != UAS_TMF_I_T_NEXUS_RESET;
    }
    if (Adapter->ResetSrb == NULL && !Adapter->NexusPending && !Adapter->PortResetQueued && !Adapter->Stopped)
    {
        Adapter->Resetting = FALSE;
        Count = UasActivateParkedLocked(Adapter, Parked);
    }
    if ((Adapter->NexusPending || Adapter->ResetSrb != NULL) && !Adapter->PortResetQueued && !Adapter->Stopped &&
        !UasTasksBusyLocked(Adapter))
    {
        ASSERT(CancelCount == 0);
        NextFunction = Adapter->NexusPending ? UAS_TMF_I_T_NEXUS_RESET : UAS_TMF_LOGICAL_UNIT_RESET;
        Lun = Adapter->NexusPending ? 0 : Adapter->ResetSrb->Lun;
    }
    else if (Succeeded && !Adapter->Resetting && !Adapter->Stopped && !Adapter->PortResetQueued)
    {
        for (Index = 0; Index < Adapter->TaskCount; Index++)
        {
            if (Adapter->Task[Index].State == UasTaskDraining && !Adapter->Task[Index].DrainAborted)
            {
                Abort = &Adapter->Task[Index];
                ASSERT(!Abort->DrainHeld);
                Abort->DrainAborted = TRUE;
                Abort->DrainHeld = TRUE;
                Adapter->AbortTask = Abort;
                break;
            }
        }
    }
    if (NextFunction == 0 && Abort == NULL)
    {
        Task->State = UasTaskFree;
        Adapter->ManagementStarted = FALSE;
    }
    Quiesced = UasQuiescedLocked(Adapter);
    KeReleaseSpinLock(&Adapter->Lock, OldIrql);

    if (Escalate)
    {
        DPRINT1("lntuas: task management function %x failed, resetting the I_T nexus after cancelling %lu commands\n",
                Function, CancelCount);
    }
    else if (ResetFirst)
    {
        DPRINT1("lntuas: task management function %x timed out, resetting the port before the I_T nexus\n", Function);
    }
    for (Index = 0; Index < CancelCount; Index++)
        UasFailTask(Cancel[Index]);
    for (Index = 0; Index < DrainCount; Index++)
    {
        UasFailTask(Drained[Index]);
        UasReleaseDrainHold(Drained[Index]);
    }
    if (Srb != NULL)
    {
        Srb->SrbStatus = SrbStatus;
        StorPortNotification(RequestComplete, Adapter, Srb);
    }
    if (Queue)
        IoQueueWorkItem(Adapter->PortResetWorkItem, UasPortResetWorker, DelayedWorkQueue, Adapter);
    UasIssueTasks(Parked, Count);
    if (NextFunction != 0)
    {
        UasStartTaskManagement(Adapter, NextFunction, 0, Lun);
    }
    else if (Abort != NULL)
    {
        DPRINT1("lntuas: aborting the cancelled command of tag %u on logical unit %u\n", Abort->Tag, Abort->DrainLun);
        UasStartTaskManagement(Adapter, UAS_TMF_ABORT_TASK, Abort->Tag, Abort->DrainLun);
    }
    if (Quiesced)
        KeSetEvent(&Adapter->Quiesced, IO_NO_INCREMENT, FALSE);
}

VOID
NTAPI
UasTmfTimeoutDpc(
    _In_ PKDPC Dpc,
    _In_opt_ PVOID DeferredContext,
    _In_opt_ PVOID SystemArgument1,
    _In_opt_ PVOID SystemArgument2)
{
    PUAS_ADAPTER Adapter = DeferredContext;
    PUAS_TASK Task;
    BOOLEAN Held = FALSE;
    LONG Pending;

    UNREFERENCED_PARAMETER(Dpc);
    UNREFERENCED_PARAMETER(SystemArgument1);
    UNREFERENCED_PARAMETER(SystemArgument2);
    ASSERT(KeGetCurrentIrql() == DISPATCH_LEVEL);
    ASSERT(Adapter != NULL);

    Task = &Adapter->ManagementTask;
    KeAcquireSpinLockAtDpcLevel(&Adapter->Lock);
    if (Adapter->ManagementStarted && Task->State == UasTaskActive && KeQueryInterruptTime() >= Adapter->TmfDeadline)
    {
        for (Pending = ReadAcquire(&Task->Pending); Pending > 0; Pending = ReadAcquire(&Task->Pending))
        {
            if (InterlockedCompareExchange(&Task->Pending, Pending + 1, Pending) == Pending)
            {
                Adapter->TmfTimedOut = TRUE;
                Held = TRUE;
                break;
            }
        }
    }
    KeReleaseSpinLockFromDpcLevel(&Adapter->Lock);
    if (!Held)
        return;

    DPRINT1("lntuas: task management function %x got no answer in %u ms\n", Task->CommandIu[4], UAS_TMF_TIMEOUT_MS);
    UasFailTask(Task);
    UasReleaseTask(Task);
}

static
VOID
UasStartDrain(
    _In_ PUAS_TASK Task)
{
    ASSERT(KeGetCurrentIrql() <= DISPATCH_LEVEL);
    ASSERT(Task->Adapter->Streams);
    ASSERT(Task->Pending == 1);

    UasSubmitStatus(Task);
    UasReleaseTask(Task);
}

static
VOID
UasFinishDrain(
    _In_ PUAS_TASK Task)
{
    PUAS_ADAPTER Adapter = Task->Adapter;
    BOOLEAN Quiesced;
    KIRQL OldIrql;

    ASSERT(Task->Pending == 0);
    ASSERT(Task->Srb == NULL);

    KeAcquireSpinLock(&Adapter->Lock, &OldIrql);
    ASSERT(Task->State == UasTaskDraining);
    ASSERT(Adapter->DrainingTasks > 0);
    if (Task->DrainHeld)
    {
        ASSERT(!Task->DrainDone);
        Task->DrainDone = TRUE;
        KeReleaseSpinLock(&Adapter->Lock, OldIrql);
        return;
    }
    Task->State = UasTaskFree;
    Task->DrainAborted = FALSE;
    Task->DrainDone = FALSE;
    Adapter->DrainingTasks--;
    Quiesced = UasQuiescedLocked(Adapter);
    KeReleaseSpinLock(&Adapter->Lock, OldIrql);

    if (Quiesced)
        KeSetEvent(&Adapter->Quiesced, IO_NO_INCREMENT, FALSE);
}

static
VOID
UasFinishTask(
    _In_ PUAS_TASK Task)
{
    PUAS_ADAPTER Adapter = Task->Adapter;
    PSCSI_REQUEST_BLOCK Srb = Task->Srb;
    LONG Flags = InterlockedOr(&Task->Flags, 0);
    BOOLEAN Queue = FALSE, Manage = FALSE, Drain, Quiesced;
    ULONG Transferred;
    UCHAR Function = UAS_TMF_LOGICAL_UNIT_RESET, Lun = 0;
    KIRQL OldIrql;

    ASSERT(Task->Pending == 0);
    ASSERT((Flags & UAS_TASK_ALL_DONE) == UAS_TASK_ALL_DONE);
    ASSERT(Srb != NULL);

    if (Task->OwnsDataMdl)
        IoFreeMdl(Task->DataMdl);
    Task->DataMdl = NULL;
    Task->OwnsDataMdl = FALSE;

    if (Flags & UAS_TASK_FAILED)
    {
        Srb->SrbStatus = SRB_STATUS_BUS_RESET;
        Srb->DataTransferLength = 0;
    }
    else if (Flags & UAS_TASK_SENSE_ERROR)
    {
        ASSERT(SRB_STATUS(Srb->SrbStatus) == SRB_STATUS_ERROR);
    }
    else if (Flags & UAS_TASK_DATA)
    {
        Transferred = (Flags & UAS_TASK_DATA_SENT) ? Task->Urb[UAS_PHASE_DATA].TransferBufferLength : 0;
        ASSERT(Transferred <= Srb->DataTransferLength);
        Srb->SrbStatus = (Transferred < Srb->DataTransferLength) ? SRB_STATUS_DATA_OVERRUN : SRB_STATUS_SUCCESS;
        Srb->DataTransferLength = Transferred;
    }
    else
    {
        Srb->SrbStatus = SRB_STATUS_SUCCESS;
    }

    KeAcquireSpinLock(&Adapter->Lock, &OldIrql);
    ASSERT(Task->State == UasTaskActive);
    ASSERT(Adapter->ActiveTasks > 0);
    Task->Srb = NULL;
    Drain = (Flags & UAS_TASK_DRAIN) && !Adapter->Stopped && !Adapter->PortResetQueued;
    Task->State = Drain ? UasTaskDraining : UasTaskFree;
    if (Drain)
    {
        Task->DrainLun = Srb->Lun;
        Task->DrainAborted = FALSE;
        Task->DrainDone = FALSE;
        Task->DrainHeld = FALSE;
        Task->Flags = UAS_TASK_ALL_DONE | UAS_TASK_DRAIN;
        Task->Pending = 1;
        Adapter->DrainingTasks++;
    }
    Adapter->ActiveTasks--;
    if ((Flags & UAS_TASK_FAILED) && Adapter->Stopped)
        Srb->SrbStatus = SRB_STATUS_NO_DEVICE;
    if ((Flags & UAS_TASK_FAILED) && !Adapter->Resetting)
        UasQueuePortResetLocked(Adapter, &Queue);
    if (Adapter->Resetting && (Adapter->ResetSrb || Adapter->NexusPending) && !Adapter->ManagementStarted &&
        !Adapter->PortResetQueued && !Adapter->Stopped && !UasTasksBusyLocked(Adapter))
    {
        ASSERT(Adapter->ManagementTask.State == UasTaskFree);
        Adapter->ManagementStarted = TRUE;
        Adapter->ManagementTask.State = UasTaskActive;
        Function = Adapter->NexusPending ? UAS_TMF_I_T_NEXUS_RESET : UAS_TMF_LOGICAL_UNIT_RESET;
        Lun = Adapter->NexusPending ? 0 : Adapter->ResetSrb->Lun;
        Manage = TRUE;
    }
    Quiesced = UasQuiescedLocked(Adapter);
    KeReleaseSpinLock(&Adapter->Lock, OldIrql);

    StorPortNotification(RequestComplete, Adapter, Srb);
    if (Drain)
        UasStartDrain(Task);
    if (Queue)
        IoQueueWorkItem(Adapter->PortResetWorkItem, UasPortResetWorker, DelayedWorkQueue, Adapter);
    if (Manage)
        UasStartTaskManagement(Adapter, Function, 0, Lun);
    if (Quiesced)
        KeSetEvent(&Adapter->Quiesced, IO_NO_INCREMENT, FALSE);
}

static
VOID
UasReleaseTask(
    _In_ PUAS_TASK Task)
{
    LONG Pending = InterlockedDecrement(&Task->Pending);

    ASSERT(Pending >= 0);
    if (Pending != 0)
        return;
    if (UasIsManagementTask(Task))
        UasFinishManagement(Task);
    else if (Task->State == UasTaskDraining)
        UasFinishDrain(Task);
    else
        UasFinishTask(Task);
}

static
NTSTATUS
NTAPI
UasPhaseCompletion(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp,
    _In_reads_opt_(_Inexpressible_("varies")) PVOID Context)
{
    PUAS_TASK Task = Context;
    ULONG Phase;
    LONG Flags;

    UNREFERENCED_PARAMETER(DeviceObject);
    ASSERT(KeGetCurrentIrql() <= DISPATCH_LEVEL);

    for (Phase = 0; Phase < UAS_PHASES && Task->Irp[Phase] != Irp; Phase++);
    ASSERT(Phase < UAS_PHASES);
    Flags = InterlockedOr(&Task->Flags, UAS_TASK_DONE(Phase));
    ASSERT(!(Flags & UAS_TASK_DONE(Phase)));
    UasPhaseDone(Task, Phase, Irp->IoStatus.Status);
    UasReleaseTask(Task);
    return STATUS_MORE_PROCESSING_REQUIRED;
}

static
PMDL
UasBuildDataMdl(
    _In_ PUAS_TASK Task,
    _Out_ PBOOLEAN Owned)
{
    PSCSI_REQUEST_BLOCK Srb = Task->Srb;
    PUCHAR Buffer = Srb->DataBuffer, Start;
    PVOID Original = NULL;
    PMDL Mdl;

    *Owned = FALSE;
    if (StorPortGetOriginalMdl(Task->Adapter, Srb, &Original) != STOR_STATUS_SUCCESS)
        Original = NULL;
    if (Original)
    {
        Start = MmGetMdlVirtualAddress((PMDL)Original);
        if (Buffer < Start || Buffer + Srb->DataTransferLength > Start + MmGetMdlByteCount((PMDL)Original))
            Original = NULL;
        else if (Start == Buffer)
            return Original;
    }
    if (!Original && !Buffer)
        return NULL;
    Mdl = IoAllocateMdl(Buffer, Srb->DataTransferLength, FALSE, FALSE, NULL);
    if (!Mdl)
        return NULL;
    if (Original)
        IoBuildPartialMdl(Original, Mdl, Buffer, Srb->DataTransferLength);
    else
        MmBuildMdlForNonPagedPool(Mdl);
    *Owned = TRUE;
    return Mdl;
}

static
VOID
UasIssueTask(
    _In_ PUAS_TASK Task)
{
    PSCSI_REQUEST_BLOCK Srb = Task->Srb;
    PUCHAR Iu = Task->CommandIu;
    BOOLEAN Data;

    ASSERT(KeGetCurrentIrql() <= DISPATCH_LEVEL);
    ASSERT(Task->State == UasTaskActive);
    ASSERT(Srb != NULL && Srb->CdbLength <= 16);
    ASSERT(Task->DataMdl == NULL);

    Data = UasHasData(Srb);
    Task->Flags = UAS_TASK_ALL_DONE | (Data ? UAS_TASK_DATA : 0);
    Task->Pending = 1;
    RtlZeroMemory(&Task->Urb[UAS_PHASE_DATA], sizeof(Task->Urb[UAS_PHASE_DATA]));
    RtlZeroMemory(Iu, UAS_COMMAND_IU_SIZE);
    Iu[0] = UAS_IU_COMMAND;
    Iu[2] = (UCHAR)(Task->Tag >> 8);
    Iu[3] = (UCHAR)Task->Tag;
    Iu[9] = Srb->Lun;
    RtlCopyMemory(Iu + 16, Srb->Cdb, min(Srb->CdbLength, 16));
    if (Data)
    {
        Task->DataMdl = UasBuildDataMdl(Task, &Task->OwnsDataMdl);
        if (!Task->DataMdl)
        {
            DPRINT1("lntuas: no MDL for tag %u\n", Task->Tag);
            InterlockedOr(&Task->Flags, UAS_TASK_FAILED);
            UasReleaseTask(Task);
            return;
        }
    }
    if (Task->Adapter->Streams)
    {
        UasSubmitStatus(Task);
        if (Data)
            UasSubmitData(Task);
    }
    UasSubmit(Task, UAS_PHASE_COMMAND, UAS_PIPE_COMMAND, USBD_TRANSFER_DIRECTION_OUT, UAS_COMMAND_IU_SIZE, Iu, NULL);
    UasReleaseTask(Task);
}

VOID
UasStartSrb(
    _In_ PUAS_ADAPTER Adapter,
    _In_ PSCSI_REQUEST_BLOCK Srb)
{
    PUAS_TASK Task = NULL;
    BOOLEAN Issue = FALSE, Stopped;
    ULONG Index, Draining;
    KIRQL OldIrql;

    ASSERT(KeGetCurrentIrql() <= DISPATCH_LEVEL);
    ASSERT(Srb->Function == SRB_FUNCTION_EXECUTE_SCSI);

    KeAcquireSpinLock(&Adapter->Lock, &OldIrql);
    if (!Adapter->Stopped)
    {
        for (Index = 0; Index < Adapter->TaskCount; Index++)
        {
            if (Adapter->Task[Index].State == UasTaskFree)
            {
                Task = &Adapter->Task[Index];
                break;
            }
        }
    }
    if (Task)
    {
        ASSERT(Task->Srb == NULL && Task->Pending == 0);
        Task->Srb = Srb;
        if (Adapter->Resetting)
        {
            Task->State = UasTaskParked;
        }
        else
        {
            Task->State = UasTaskActive;
            Adapter->ActiveTasks++;
            Issue = TRUE;
        }
    }
    Stopped = Adapter->Stopped;
    Draining = Adapter->DrainingTasks;
    KeReleaseSpinLock(&Adapter->Lock, OldIrql);

    if (!Task)
    {
        ASSERT(Stopped || Draining != 0);
        Srb->SrbStatus = Stopped ? SRB_STATUS_NO_DEVICE : SRB_STATUS_BUSY;
        StorPortNotification(RequestComplete, Adapter, Srb);
        return;
    }
    if (Issue)
        UasIssueTask(Task);
}

VOID
UasResetLogicalUnit(
    _In_ PUAS_ADAPTER Adapter,
    _In_ PSCSI_REQUEST_BLOCK Srb)
{
    PUAS_TASK Cancel[UAS_MAX_TASKS];
    ULONG Index, Count = 0;
    BOOLEAN Manage = FALSE, Rejected = FALSE;
    UCHAR Function = UAS_TMF_LOGICAL_UNIT_RESET, Lun = Srb->Lun;
    KIRQL OldIrql;

    ASSERT(KeGetCurrentIrql() <= DISPATCH_LEVEL);

    KeAcquireSpinLock(&Adapter->Lock, &OldIrql);
    if (Adapter->Stopped || Adapter->ResetSrb)
    {
        Rejected = TRUE;
    }
    else
    {
        Adapter->ResetSrb = Srb;
        Adapter->Resetting = TRUE;
        Count = UasPurgeActiveLocked(Adapter, Cancel);
        if (Count == 0 && !Adapter->PortResetQueued && !Adapter->ManagementStarted)
        {
            ASSERT(Adapter->ManagementTask.State == UasTaskFree);
            Adapter->ManagementStarted = TRUE;
            Adapter->ManagementTask.State = UasTaskActive;
            if (Adapter->NexusPending)
            {
                Function = UAS_TMF_I_T_NEXUS_RESET;
                Lun = 0;
            }
            Manage = TRUE;
        }
    }
    KeReleaseSpinLock(&Adapter->Lock, OldIrql);

    if (Rejected)
    {
        Srb->SrbStatus = Adapter->Stopped ? SRB_STATUS_NO_DEVICE : SRB_STATUS_BUSY;
        StorPortNotification(RequestComplete, Adapter, Srb);
        return;
    }
    DPRINT1("lntuas: resetting logical unit %u, cancelling %lu commands\n", Srb->Lun, Count);
    for (Index = 0; Index < Count; Index++)
        UasFailTask(Cancel[Index]);
    if (Manage)
        UasStartTaskManagement(Adapter, Function, 0, Lun);
}

static
ULONG
UasCollectActiveLocked(
    _In_ PUAS_ADAPTER Adapter,
    _Out_writes_(UAS_MAX_TASKS + 1) PUAS_TASK *Tasks)
{
    ULONG Index, Count = 0;

    for (Index = 0; Index < Adapter->TaskCount; Index++)
    {
        if (Adapter->Task[Index].State == UasTaskActive || Adapter->Task[Index].State == UasTaskDraining)
            Tasks[Count++] = &Adapter->Task[Index];
    }
    if (Adapter->ManagementTask.State == UasTaskActive)
        Tasks[Count++] = &Adapter->ManagementTask;
    return Count;
}

static
VOID
NTAPI
UasPortResetWorker(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_opt_ PVOID Context)
{
    PUAS_ADAPTER Adapter = Context;
    PUAS_TASK Tasks[UAS_MAX_TASKS + 1];
    PSCSI_REQUEST_BLOCK Srb;
    NTSTATUS Status = STATUS_DEVICE_REMOVED;
    BOOLEAN Quiesced, Stopped, Nexus;
    ULONG Index, Count;
    KIRQL OldIrql;

    UNREFERENCED_PARAMETER(DeviceObject);
    ASSERT(KeGetCurrentIrql() == PASSIVE_LEVEL);

    KeAcquireSpinLock(&Adapter->Lock, &OldIrql);
    ASSERT(Adapter->PortResetQueued && Adapter->Resetting);
    KeClearEvent(&Adapter->Quiesced);
    Count = UasCollectActiveLocked(Adapter, Tasks);
    Quiesced = UasQuiescedLocked(Adapter);
    KeReleaseSpinLock(&Adapter->Lock, OldIrql);

    for (Index = 0; Index < Count; Index++)
        UasFailTask(Tasks[Index]);
    if (!Quiesced)
        KeWaitForSingleObject(&Adapter->Quiesced, Executive, KernelMode, FALSE, NULL);

    if (!Adapter->Stopped)
    {
        Status = UasResetPort(Adapter);
        DPRINT1("lntuas: port reset after %lu cancelled commands: %lx\n", Count, Status);
    }

    KeAcquireSpinLock(&Adapter->Lock, &OldIrql);
    ASSERT(Adapter->ActiveTasks == 0);
    ASSERT(Adapter->ManagementTask.State == UasTaskFree);
    ASSERT(Adapter->AbortTask == NULL);
    Srb = Adapter->ResetSrb;
    Adapter->ResetSrb = NULL;
    Adapter->ManagementStarted = FALSE;
    Adapter->NexusPending = FALSE;
    Adapter->PortResetQueued = FALSE;
    Stopped = Adapter->Stopped;
    Nexus = Adapter->NexusAfterReset && !Stopped && NT_SUCCESS(Status);
    Adapter->NexusAfterReset = FALSE;
    Count = 0;
    if (Nexus)
    {
        ASSERT(Adapter->Resetting);
        Adapter->NexusPending = TRUE;
        Adapter->ManagementStarted = TRUE;
        Adapter->ManagementTask.State = UasTaskActive;
    }
    else if (!Stopped)
    {
        Adapter->Resetting = FALSE;
        Count = UasActivateParkedLocked(Adapter, Tasks);
    }
    KeReleaseSpinLock(&Adapter->Lock, OldIrql);

    if (Srb)
    {
        Srb->SrbStatus = Stopped ? SRB_STATUS_NO_DEVICE : NT_SUCCESS(Status) ? SRB_STATUS_SUCCESS : SRB_STATUS_ERROR;
        StorPortNotification(RequestComplete, Adapter, Srb);
    }
    UasIssueTasks(Tasks, Count);
    if (Nexus)
    {
        DPRINT1("lntuas: resetting the I_T nexus after the port reset\n");
        UasStartTaskManagement(Adapter, UAS_TMF_I_T_NEXUS_RESET, 0, 0);
    }
    KeSetEvent(&Adapter->PortResetIdle, IO_NO_INCREMENT, FALSE);
}

VOID
UasQueuePortReset(
    _In_ PUAS_ADAPTER Adapter)
{
    BOOLEAN Queue;
    KIRQL OldIrql;

    ASSERT(KeGetCurrentIrql() <= DISPATCH_LEVEL);

    KeAcquireSpinLock(&Adapter->Lock, &OldIrql);
    UasQueuePortResetLocked(Adapter, &Queue);
    KeReleaseSpinLock(&Adapter->Lock, OldIrql);
    if (Queue)
        IoQueueWorkItem(Adapter->PortResetWorkItem, UasPortResetWorker, DelayedWorkQueue, Adapter);
}

VOID
UasStopAdapter(
    _In_ PUAS_ADAPTER Adapter)
{
    PUAS_TASK Tasks[UAS_MAX_TASKS + 1];
    PSCSI_REQUEST_BLOCK Parked[UAS_MAX_TASKS + 1];
    ULONG Index, Count, ParkedCount = 0;
    BOOLEAN Quiesced;
    KIRQL OldIrql;

    ASSERT(KeGetCurrentIrql() <= DISPATCH_LEVEL);

    KeAcquireSpinLock(&Adapter->Lock, &OldIrql);
    Adapter->Stopped = TRUE;
    for (Index = 0; Index < Adapter->TaskCount; Index++)
    {
        if (Adapter->Task[Index].State != UasTaskParked)
            continue;
        Parked[ParkedCount++] = Adapter->Task[Index].Srb;
        Adapter->Task[Index].Srb = NULL;
        Adapter->Task[Index].State = UasTaskFree;
    }
    if (Adapter->ResetSrb && !Adapter->ManagementStarted && !Adapter->PortResetQueued)
    {
        Parked[ParkedCount++] = Adapter->ResetSrb;
        Adapter->ResetSrb = NULL;
    }
    Count = UasCollectActiveLocked(Adapter, Tasks);
    Quiesced = UasQuiescedLocked(Adapter);
    KeReleaseSpinLock(&Adapter->Lock, OldIrql);

    for (Index = 0; Index < ParkedCount; Index++)
    {
        Parked[Index]->SrbStatus = SRB_STATUS_NO_DEVICE;
        StorPortNotification(RequestComplete, Adapter, Parked[Index]);
    }
    for (Index = 0; Index < Count; Index++)
        UasFailTask(Tasks[Index]);
    if (Quiesced)
        KeSetEvent(&Adapter->Quiesced, IO_NO_INCREMENT, FALSE);
}

VOID
UasWaitQuiesced(
    _In_ PUAS_ADAPTER Adapter)
{
    BOOLEAN Quiesced;
    KIRQL OldIrql;

    ASSERT(KeGetCurrentIrql() == PASSIVE_LEVEL);

    KeAcquireSpinLock(&Adapter->Lock, &OldIrql);
    ASSERT(Adapter->Stopped);
    KeClearEvent(&Adapter->Quiesced);
    Quiesced = UasQuiescedLocked(Adapter);
    KeReleaseSpinLock(&Adapter->Lock, OldIrql);
    if (!Quiesced)
        KeWaitForSingleObject(&Adapter->Quiesced, Executive, KernelMode, FALSE, NULL);
    KeWaitForSingleObject(&Adapter->PortResetIdle, Executive, KernelMode, FALSE, NULL);
}
