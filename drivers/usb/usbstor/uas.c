/*
 * PROJECT:     LiberNT USB Mass Storage Driver
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     USB Attached SCSI transport
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "usbstor.h"

#define NDEBUG
#include <debug.h>

#define UAS_INTERFACE_CLASS     0x08
#define UAS_INTERFACE_SUBCLASS  0x06
#define UAS_STATUS_GOOD         0x00
#define UAS_STATUS_CHECK        0x02
#define UAS_COMPANION_TYPE      0x30
#define UAS_COMPANION_STREAMS   0x1f

#define UAS_STATE_DATA          0x01
#define UAS_STATE_COMMAND_DONE  0x02
#define UAS_STATE_DATA_DONE     0x04
#define UAS_STATE_STATUS_DONE   0x08
#define UAS_STATE_SENSE_ERROR   0x10
#define UAS_STATE_FAILED        0x20

BOOLEAN
USBSTOR_UasScanConfiguration(
    IN PUSB_CONFIGURATION_DESCRIPTOR ConfigurationDescriptor,
    OUT PUSB_INTERFACE_DESCRIPTOR *InterfaceDescriptor,
    OUT PUCHAR EndpointAddress,
    OUT PBOOLEAN Streams)
{
    PUCHAR At = (PUCHAR)ConfigurationDescriptor;
    PUCHAR End = At + ConfigurationDescriptor->wTotalLength;
    PUSB_INTERFACE_DESCRIPTOR Candidate = NULL;
    UCHAR LastEndpoint = 0, Found = 0, Index;
    BOOLEAN CandidateStreams = FALSE;

    *InterfaceDescriptor = NULL;
    *Streams = FALSE;
    RtlZeroMemory(EndpointAddress, UAS_PIPES);
    while (At + 2 <= End && At[0] >= 2 && At + At[0] <= End)
    {
        if (At[1] == USB_INTERFACE_DESCRIPTOR_TYPE && At[0] >= sizeof(USB_INTERFACE_DESCRIPTOR))
        {
            PUSB_INTERFACE_DESCRIPTOR Interface = (PUSB_INTERFACE_DESCRIPTOR)At;

            if (Candidate && Found == (1 << UAS_PIPES) - 1)
                break;
            Candidate = NULL;
            Found = 0;
            CandidateStreams = FALSE;
            RtlZeroMemory(EndpointAddress, UAS_PIPES);
            if (Interface->bInterfaceClass == UAS_INTERFACE_CLASS &&
                Interface->bInterfaceSubClass == UAS_INTERFACE_SUBCLASS &&
                Interface->bInterfaceProtocol == USB_PROTOCOL_UAS && Interface->bNumEndpoints == UAS_PIPES)
            {
                Candidate = Interface;
            }
        }
        else if (Candidate && At[1] == USB_ENDPOINT_DESCRIPTOR_TYPE && At[0] >= sizeof(USB_ENDPOINT_DESCRIPTOR))
        {
            LastEndpoint = ((PUSB_ENDPOINT_DESCRIPTOR)At)->bEndpointAddress;
        }
        else if (Candidate && At[1] == UAS_PIPE_USAGE_DESCRIPTOR_TYPE && At[0] >= 4 && LastEndpoint &&
                 At[2] >= 1 && At[2] <= UAS_PIPES)
        {
            Index = At[2] - 1;
            EndpointAddress[Index] = LastEndpoint;
            Found |= 1 << Index;
        }
        else if (Candidate && At[1] == UAS_COMPANION_TYPE && At[0] >= 6 && (At[3] & UAS_COMPANION_STREAMS))
        {
            CandidateStreams = TRUE;
        }
        At += At[0];
    }
    if (!Candidate || Found != (1 << UAS_PIPES) - 1 ||
        USB_ENDPOINT_DIRECTION_IN(EndpointAddress[UAS_PIPE_COMMAND]) ||
        !USB_ENDPOINT_DIRECTION_IN(EndpointAddress[UAS_PIPE_STATUS]) ||
        !USB_ENDPOINT_DIRECTION_IN(EndpointAddress[UAS_PIPE_DATA_IN]) ||
        USB_ENDPOINT_DIRECTION_IN(EndpointAddress[UAS_PIPE_DATA_OUT]))
    {
        RtlZeroMemory(EndpointAddress, UAS_PIPES);
        return FALSE;
    }
    *InterfaceDescriptor = Candidate;
    *Streams = CandidateStreams;
    return TRUE;
}

static
PUCHAR
UasAllocateDma(
    _Outptr_ PVOID *Allocation,
    _In_ SIZE_T Size)
{
    PUCHAR Raw = ExAllocatePoolWithTag(NonPagedPool, Size + SYSTEM_CACHE_ALIGNMENT_SIZE, USB_STOR_TAG);

    *Allocation = Raw;
    if (!Raw)
        return NULL;
    RtlZeroMemory(Raw, Size + SYSTEM_CACHE_ALIGNMENT_SIZE);
    return (PUCHAR)(((ULONG_PTR)Raw + SYSTEM_CACHE_ALIGNMENT_SIZE - 1) & ~((ULONG_PTR)SYSTEM_CACHE_ALIGNMENT_SIZE - 1));
}

static
USBD_PIPE_HANDLE
UasPipeHandle(
    IN PFDO_DEVICE_EXTENSION DeviceExtension,
    IN ULONG Role)
{
    if (DeviceExtension->UasStreams && Role != UAS_PIPE_COMMAND)
        return DeviceExtension->UasStream[Role].PipeHandle;
    return DeviceExtension->InterfaceInformation->Pipes[DeviceExtension->UasPipe[Role]].PipeHandle;
}

NTSTATUS
USBSTOR_UasStart(
    IN PFDO_DEVICE_EXTENSION DeviceExtension)
{
    URB Urb;
    ULONG Role, Index;
    NTSTATUS Status;

    for (Role = 0; Role < UAS_PIPES; ++Role)
    {
        for (Index = 0; Index < DeviceExtension->InterfaceInformation->NumberOfPipes; ++Index)
        {
            if (DeviceExtension->InterfaceInformation->Pipes[Index].EndpointAddress ==
                DeviceExtension->UasEndpoint[Role])
            {
                break;
            }
        }
        if (Index == DeviceExtension->InterfaceInformation->NumberOfPipes)
        {
            DPRINT1("USBSTOR: UAS endpoint %x has no pipe\n", DeviceExtension->UasEndpoint[Role]);
            return STATUS_DEVICE_CONFIGURATION_ERROR;
        }
        DeviceExtension->UasPipe[Role] = (UCHAR)Index;
    }
    DeviceExtension->UasCommand = UasAllocateDma(&DeviceExtension->UasCommandAllocation, UAS_COMMAND_IU_SIZE);
    DeviceExtension->UasStatus = UasAllocateDma(&DeviceExtension->UasStatusAllocation, UAS_STATUS_IU_SIZE);
    if (!DeviceExtension->UasCommand || !DeviceExtension->UasStatus)
        return STATUS_INSUFFICIENT_RESOURCES;
    if (!DeviceExtension->UasStreams)
        return STATUS_SUCCESS;
    for (Role = UAS_PIPE_STATUS; Role < UAS_PIPES; ++Role)
    {
        RtlZeroMemory(&Urb, sizeof(Urb));
        Urb.UrbHeader.Length = sizeof(struct _URB_OPEN_STATIC_STREAMS);
        Urb.UrbHeader.Function = URB_FUNCTION_OPEN_STATIC_STREAMS;
        Urb.UrbOpenStaticStreams.PipeHandle =
            DeviceExtension->InterfaceInformation->Pipes[DeviceExtension->UasPipe[Role]].PipeHandle;
        Urb.UrbOpenStaticStreams.NumberOfStreams = UAS_TAG;
        Urb.UrbOpenStaticStreams.StreamInfoVersion = URB_OPEN_STATIC_STREAMS_VERSION_100;
        Urb.UrbOpenStaticStreams.StreamInfoSize = sizeof(USBD_STREAM_INFORMATION);
        Urb.UrbOpenStaticStreams.Streams = &DeviceExtension->UasStream[Role];
        Status = USBSTOR_SyncUrbRequest(DeviceExtension->LowerDeviceObject, &Urb);
        if (!NT_SUCCESS(Status) || DeviceExtension->UasStream[Role].StreamID != UAS_TAG)
        {
            DPRINT1("USBSTOR: UAS pipe %lu opened no stream: %x\n", Role, Status);
            return NT_SUCCESS(Status) ? STATUS_DEVICE_CONFIGURATION_ERROR : Status;
        }
    }
    DeviceExtension->UasCommandIrp = IoAllocateIrp(DeviceExtension->LowerDeviceObject->StackSize, FALSE);
    DeviceExtension->UasDataIrp = IoAllocateIrp(DeviceExtension->LowerDeviceObject->StackSize, FALSE);
    if (!DeviceExtension->UasCommandIrp || !DeviceExtension->UasDataIrp)
        return STATUS_INSUFFICIENT_RESOURCES;
    return STATUS_SUCCESS;
}

VOID
USBSTOR_UasStop(
    IN PFDO_DEVICE_EXTENSION DeviceExtension)
{
    if (DeviceExtension->UasCommandAllocation)
        ExFreePoolWithTag(DeviceExtension->UasCommandAllocation, USB_STOR_TAG);
    if (DeviceExtension->UasStatusAllocation)
        ExFreePoolWithTag(DeviceExtension->UasStatusAllocation, USB_STOR_TAG);
    DeviceExtension->UasCommandAllocation = NULL;
    DeviceExtension->UasStatusAllocation = NULL;
    DeviceExtension->UasCommand = NULL;
    DeviceExtension->UasStatus = NULL;
    if (DeviceExtension->UasCommandIrp)
        IoFreeIrp(DeviceExtension->UasCommandIrp);
    if (DeviceExtension->UasDataIrp)
        IoFreeIrp(DeviceExtension->UasDataIrp);
    DeviceExtension->UasCommandIrp = NULL;
    DeviceExtension->UasDataIrp = NULL;
}

static
NTSTATUS
UasRecover(
    IN PFDO_DEVICE_EXTENSION FDODeviceExtension,
    IN PIRP Irp)
{
    PSCSI_REQUEST_BLOCK Request = FDODeviceExtension->ActiveSrb;

    /* FIXME: recovery resets the device the bulk-only way; UAS would abort the task and reset the interface. */
    IoGetCurrentIrpStackLocation(Irp)->Parameters.Scsi.Srb = Request;
    USBSTOR_SetResetRecoveryStatus(FDODeviceExtension, Irp, Request);
    if (!USBSTOR_FinishRequest(FDODeviceExtension, Irp, TRUE))
        return STATUS_MORE_PROCESSING_REQUIRED;
    return STATUS_CONTINUE_COMPLETION;
}

static IO_COMPLETION_ROUTINE UasStatusCompletion;
static IO_COMPLETION_ROUTINE UasDataCompletion;
static IO_COMPLETION_ROUTINE UasCommandCompletion;

static
BOOLEAN
UasHasData(
    IN PSCSI_REQUEST_BLOCK Request)
{
    return Request->DataTransferLength != 0 &&
           ((Request->SrbFlags & SRB_FLAGS_UNSPECIFIED_DIRECTION) == SRB_FLAGS_DATA_IN ||
            (Request->SrbFlags & SRB_FLAGS_UNSPECIFIED_DIRECTION) == SRB_FLAGS_DATA_OUT);
}

static
BOOLEAN
UasTakeSense(
    IN PSCSI_REQUEST_BLOCK Request,
    IN PUCHAR Iu,
    IN ULONG Length)
{
    ULONG SenseLength;

    ASSERT(Iu[0] == UAS_IU_SENSE && Length >= UAS_SENSE_IU_HEADER);
    Request->ScsiStatus = Iu[6];
    if (Iu[6] == UAS_STATUS_GOOD)
        return TRUE;
    Request->SrbStatus = SRB_STATUS_ERROR;
    SenseLength = min((ULONG)((Iu[14] << 8) | Iu[15]), Length - UAS_SENSE_IU_HEADER);
    if (Iu[6] == UAS_STATUS_CHECK && SenseLength && !(Request->SrbFlags & SRB_FLAGS_DISABLE_AUTOSENSE) &&
        Request->SenseInfoBuffer && Request->SenseInfoBufferLength)
    {
        SenseLength = min(SenseLength, Request->SenseInfoBufferLength);
        RtlCopyMemory(Request->SenseInfoBuffer, Iu + UAS_SENSE_IU_HEADER, SenseLength);
        Request->SenseInfoBufferLength = (UCHAR)SenseLength;
        Request->SrbStatus |= SRB_STATUS_AUTOSENSE_VALID;
    }
    return FALSE;
}

static
PMDL
UasDataMdl(
    IN PIRP Irp,
    IN PSCSI_REQUEST_BLOCK Request)
{
    PMDL Mdl;

    if (Irp->MdlAddress && MmGetMdlVirtualAddress(Irp->MdlAddress) == Request->DataBuffer)
        return Irp->MdlAddress;
    if (!Request->DataBuffer)
        return NULL;
    Mdl = IoAllocateMdl(Request->DataBuffer, Request->DataTransferLength, FALSE, FALSE, NULL);
    if (!Mdl)
        return NULL;
    if (Irp->MdlAddress)
        IoBuildPartialMdl(Irp->MdlAddress, Mdl, Request->DataBuffer, Request->DataTransferLength);
    else
        MmBuildMdlForNonPagedPool(Mdl);
    return Mdl;
}

static
NTSTATUS
UasIssueStatus(
    IN PFDO_DEVICE_EXTENSION FDODeviceExtension,
    IN PIRP Irp)
{
    RtlZeroMemory(FDODeviceExtension->UasStatus, UAS_STATUS_IU_SIZE);
    return USBSTOR_IssueBulkOrInterruptRequest(FDODeviceExtension, Irp,
                                               UasPipeHandle(FDODeviceExtension, UAS_PIPE_STATUS),
                                               USBD_TRANSFER_DIRECTION_IN | USBD_SHORT_TRANSFER_OK,
                                               UAS_STATUS_IU_SIZE, FDODeviceExtension->UasStatus, NULL,
                                               UasStatusCompletion);
}

static
NTSTATUS
UasIssueData(
    IN PFDO_DEVICE_EXTENSION FDODeviceExtension,
    IN PIRP Irp)
{
    PSCSI_REQUEST_BLOCK Request = FDODeviceExtension->ActiveSrb;
    BOOLEAN In = (Request->SrbFlags & SRB_FLAGS_UNSPECIFIED_DIRECTION) == SRB_FLAGS_DATA_IN;
    PMDL Mdl = UasDataMdl(Irp, Request);

    if (!Mdl)
        return UasRecover(FDODeviceExtension, Irp);
    USBSTOR_IssueBulkOrInterruptRequest(FDODeviceExtension, Irp,
                                        UasPipeHandle(FDODeviceExtension, In ? UAS_PIPE_DATA_IN : UAS_PIPE_DATA_OUT),
                                        In ? USBD_TRANSFER_DIRECTION_IN | USBD_SHORT_TRANSFER_OK
                                           : USBD_TRANSFER_DIRECTION_OUT,
                                        Request->DataTransferLength, NULL, Mdl, UasDataCompletion);
    return STATUS_MORE_PROCESSING_REQUIRED;
}

static
NTSTATUS
NTAPI
UasCommandCompletion(
    PDEVICE_OBJECT DeviceObject,
    PIRP Irp,
    PVOID Ctx)
{
    PFDO_DEVICE_EXTENSION FDODeviceExtension = Ctx;

    UNREFERENCED_PARAMETER(DeviceObject);
    if (!NT_SUCCESS(Irp->IoStatus.Status))
    {
        DPRINT1("USBSTOR: UAS command IU failed %x\n", FDODeviceExtension->CurrentIrpContext.Urb.UrbHeader.Status);
        return UasRecover(FDODeviceExtension, Irp);
    }
    UasIssueStatus(FDODeviceExtension, Irp);
    return STATUS_MORE_PROCESSING_REQUIRED;
}

static
NTSTATUS
NTAPI
UasDataCompletion(
    PDEVICE_OBJECT DeviceObject,
    PIRP Irp,
    PVOID Ctx)
{
    PFDO_DEVICE_EXTENSION FDODeviceExtension = Ctx;
    PIRP_CONTEXT Context = &FDODeviceExtension->CurrentIrpContext;
    PSCSI_REQUEST_BLOCK Request = FDODeviceExtension->ActiveSrb;
    ULONG Transferred = Context->Urb.UrbBulkOrInterruptTransfer.TransferBufferLength;

    UNREFERENCED_PARAMETER(DeviceObject);
    if (Context->Urb.UrbBulkOrInterruptTransfer.TransferBufferMDL != Irp->MdlAddress)
        IoFreeMdl(Context->Urb.UrbBulkOrInterruptTransfer.TransferBufferMDL);
    if (!NT_SUCCESS(Irp->IoStatus.Status))
    {
        DPRINT1("USBSTOR: UAS data phase failed %x\n", Context->Urb.UrbHeader.Status);
        return UasRecover(FDODeviceExtension, Irp);
    }
    Request->SrbStatus = (Transferred < Request->DataTransferLength) ? SRB_STATUS_DATA_OVERRUN : SRB_STATUS_SUCCESS;
    Request->DataTransferLength = Transferred;
    UasIssueStatus(FDODeviceExtension, Irp);
    return STATUS_MORE_PROCESSING_REQUIRED;
}

static
NTSTATUS
NTAPI
UasStatusCompletion(
    PDEVICE_OBJECT DeviceObject,
    PIRP Irp,
    PVOID Ctx)
{
    PFDO_DEVICE_EXTENSION FDODeviceExtension = Ctx;
    PIRP_CONTEXT Context = &FDODeviceExtension->CurrentIrpContext;
    PSCSI_REQUEST_BLOCK Request = FDODeviceExtension->ActiveSrb;
    PUCHAR Iu = FDODeviceExtension->UasStatus;
    ULONG Length = Context->Urb.UrbBulkOrInterruptTransfer.TransferBufferLength;

    UNREFERENCED_PARAMETER(DeviceObject);
    if (!NT_SUCCESS(Irp->IoStatus.Status) || Length < 4 || ((Iu[2] << 8) | Iu[3]) != UAS_TAG)
    {
        DPRINT1("USBSTOR: UAS status failed %x length %lu IU %x\n", Context->Urb.UrbHeader.Status, Length,
                Length ? Iu[0] : 0);
        return UasRecover(FDODeviceExtension, Irp);
    }
    switch (Iu[0])
    {
        case UAS_IU_READ_READY:
        case UAS_IU_WRITE_READY:
            if (!UasHasData(Request))
                return UasRecover(FDODeviceExtension, Irp);
            return UasIssueData(FDODeviceExtension, Irp);

        case UAS_IU_SENSE:
            if (Length < UAS_SENSE_IU_HEADER)
                return UasRecover(FDODeviceExtension, Irp);
            if (UasTakeSense(Request, Iu, Length))
            {
                if (!UasHasData(Request))
                    Request->SrbStatus = SRB_STATUS_SUCCESS;
                break;
            }
            Request->DataTransferLength = 0;
            break;

        default:
            DPRINT1("USBSTOR: UAS answered IU %x code %x\n", Iu[0], Length > 7 ? Iu[7] : 0);
            return UasRecover(FDODeviceExtension, Irp);
    }
    InterlockedExchange(&FDODeviceExtension->BotResetPending, 0);
    Irp->IoStatus.Status = USBSTOR_SrbStatusToNtStatus(Request);
    Irp->IoStatus.Information = Request->DataTransferLength;
    if (!USBSTOR_FinishRequest(FDODeviceExtension, Irp, FALSE))
        return STATUS_MORE_PROCESSING_REQUIRED;
    return STATUS_CONTINUE_COMPLETION;
}

static
NTSTATUS
UasSubmit(
    IN PFDO_DEVICE_EXTENSION FDODeviceExtension,
    IN PIRP Irp,
    IN PURB Urb,
    IN ULONG Role,
    IN ULONG TransferFlags,
    IN ULONG Length,
    IN PVOID Buffer,
    IN PMDL Mdl,
    IN PIO_COMPLETION_ROUTINE CompletionRoutine)
{
    PIO_STACK_LOCATION NextStack = IoGetNextIrpStackLocation(Irp);

    RtlZeroMemory(Urb, sizeof(struct _URB_BULK_OR_INTERRUPT_TRANSFER));
    Urb->UrbHeader.Length = sizeof(struct _URB_BULK_OR_INTERRUPT_TRANSFER);
    Urb->UrbHeader.Function = URB_FUNCTION_BULK_OR_INTERRUPT_TRANSFER;
    Urb->UrbBulkOrInterruptTransfer.PipeHandle = UasPipeHandle(FDODeviceExtension, Role);
    Urb->UrbBulkOrInterruptTransfer.TransferFlags = TransferFlags;
    Urb->UrbBulkOrInterruptTransfer.TransferBufferLength = Length;
    Urb->UrbBulkOrInterruptTransfer.TransferBuffer = Buffer;
    Urb->UrbBulkOrInterruptTransfer.TransferBufferMDL = Mdl;
    NextStack->MajorFunction = IRP_MJ_INTERNAL_DEVICE_CONTROL;
    NextStack->Parameters.DeviceIoControl.IoControlCode = IOCTL_INTERNAL_USB_SUBMIT_URB;
    NextStack->Parameters.Others.Argument1 = Urb;
    IoSetCompletionRoutine(Irp, CompletionRoutine, FDODeviceExtension, TRUE, TRUE, TRUE);
    return IoCallDriver(FDODeviceExtension->LowerDeviceObject, Irp);
}

static
VOID
UasCancelPending(
    IN PFDO_DEVICE_EXTENSION FDODeviceExtension,
    IN BOOLEAN DataOnly)
{
    LONG State = InterlockedOr(&FDODeviceExtension->UasState, 0);

    if ((State & UAS_STATE_DATA) && !(State & UAS_STATE_DATA_DONE))
        IoCancelIrp(FDODeviceExtension->UasDataIrp);
    if (DataOnly)
        return;
    if (!(State & UAS_STATE_COMMAND_DONE))
        IoCancelIrp(FDODeviceExtension->UasCommandIrp);
    if (!(State & UAS_STATE_STATUS_DONE))
        IoCancelIrp(FDODeviceExtension->CurrentIrpContext.Irp);
}

static
VOID
UasStreamsFail(
    IN PFDO_DEVICE_EXTENSION FDODeviceExtension)
{
    if (!(InterlockedOr(&FDODeviceExtension->UasState, UAS_STATE_FAILED) & UAS_STATE_FAILED))
        UasCancelPending(FDODeviceExtension, FALSE);
}

static
BOOLEAN
UasStreamsFinish(
    IN PFDO_DEVICE_EXTENSION FDODeviceExtension,
    IN PIRP Irp)
{
    PSCSI_REQUEST_BLOCK Request = FDODeviceExtension->ActiveSrb;
    LONG State = InterlockedOr(&FDODeviceExtension->UasState, 0);
    ULONG Transferred;

    ASSERT(Irp == FDODeviceExtension->CurrentIrpContext.Irp);
    ASSERT(FDODeviceExtension->UasOutstanding == 0);
    ASSERT((State & (UAS_STATE_COMMAND_DONE | UAS_STATE_STATUS_DONE)) ==
           (UAS_STATE_COMMAND_DONE | UAS_STATE_STATUS_DONE));
    ASSERT(!(State & UAS_STATE_DATA) || (State & UAS_STATE_DATA_DONE));
    if (FDODeviceExtension->UasDataMdl && FDODeviceExtension->UasDataMdl != Irp->MdlAddress)
        IoFreeMdl(FDODeviceExtension->UasDataMdl);
    FDODeviceExtension->UasDataMdl = NULL;
    if (State & UAS_STATE_FAILED)
    {
        IoGetCurrentIrpStackLocation(Irp)->Parameters.Scsi.Srb = Request;
        USBSTOR_SetResetRecoveryStatus(FDODeviceExtension, Irp, Request);
        return USBSTOR_FinishRequest(FDODeviceExtension, Irp, TRUE);
    }
    if (State & UAS_STATE_SENSE_ERROR)
    {
        Request->DataTransferLength = 0;
    }
    else if (State & UAS_STATE_DATA)
    {
        Transferred = FDODeviceExtension->UasDataUrb.UrbBulkOrInterruptTransfer.TransferBufferLength;
        Request->SrbStatus = (Transferred < Request->DataTransferLength) ? SRB_STATUS_DATA_OVERRUN : SRB_STATUS_SUCCESS;
        Request->DataTransferLength = Transferred;
    }
    else
    {
        Request->SrbStatus = SRB_STATUS_SUCCESS;
    }
    InterlockedExchange(&FDODeviceExtension->BotResetPending, 0);
    Irp->IoStatus.Status = USBSTOR_SrbStatusToNtStatus(Request);
    Irp->IoStatus.Information = Request->DataTransferLength;
    return USBSTOR_FinishRequest(FDODeviceExtension, Irp, FALSE);
}

static
VOID
UasStreamsRelease(
    IN PFDO_DEVICE_EXTENSION FDODeviceExtension)
{
    PIRP Irp = FDODeviceExtension->CurrentIrpContext.Irp;

    if (InterlockedDecrement(&FDODeviceExtension->UasOutstanding) == 0 &&
        UasStreamsFinish(FDODeviceExtension, Irp))
    {
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
    }
}

static
NTSTATUS
NTAPI
UasStreamsCommandCompletion(
    PDEVICE_OBJECT DeviceObject,
    PIRP Irp,
    PVOID Ctx)
{
    PFDO_DEVICE_EXTENSION FDODeviceExtension = Ctx;

    UNREFERENCED_PARAMETER(DeviceObject);
    ASSERT(Irp == FDODeviceExtension->UasCommandIrp);
    InterlockedOr(&FDODeviceExtension->UasState, UAS_STATE_COMMAND_DONE);
    if (!NT_SUCCESS(Irp->IoStatus.Status))
    {
        DPRINT1("USBSTOR: UAS command IU failed %x\n", FDODeviceExtension->UasCommandUrb.UrbHeader.Status);
        UasStreamsFail(FDODeviceExtension);
    }
    UasStreamsRelease(FDODeviceExtension);
    return STATUS_MORE_PROCESSING_REQUIRED;
}

static
NTSTATUS
NTAPI
UasStreamsDataCompletion(
    PDEVICE_OBJECT DeviceObject,
    PIRP Irp,
    PVOID Ctx)
{
    PFDO_DEVICE_EXTENSION FDODeviceExtension = Ctx;
    LONG State;

    UNREFERENCED_PARAMETER(DeviceObject);
    ASSERT(Irp == FDODeviceExtension->UasDataIrp);
    State = InterlockedOr(&FDODeviceExtension->UasState, UAS_STATE_DATA_DONE);
    ASSERT(State & UAS_STATE_DATA);
    if (!NT_SUCCESS(Irp->IoStatus.Status) && !(State & UAS_STATE_SENSE_ERROR))
    {
        DPRINT1("USBSTOR: UAS data phase failed %x\n", FDODeviceExtension->UasDataUrb.UrbHeader.Status);
        UasStreamsFail(FDODeviceExtension);
    }
    UasStreamsRelease(FDODeviceExtension);
    return STATUS_MORE_PROCESSING_REQUIRED;
}

static
NTSTATUS
NTAPI
UasStreamsStatusCompletion(
    PDEVICE_OBJECT DeviceObject,
    PIRP Irp,
    PVOID Ctx)
{
    PFDO_DEVICE_EXTENSION FDODeviceExtension = Ctx;
    PIRP_CONTEXT Context = &FDODeviceExtension->CurrentIrpContext;
    PUCHAR Iu = FDODeviceExtension->UasStatus;
    ULONG Length = Context->Urb.UrbBulkOrInterruptTransfer.TransferBufferLength;
    LONG State;

    UNREFERENCED_PARAMETER(DeviceObject);
    ASSERT(Irp == Context->Irp);
    State = InterlockedOr(&FDODeviceExtension->UasState, UAS_STATE_STATUS_DONE);
    if (State & UAS_STATE_FAILED)
        NOTHING;
    else if (!NT_SUCCESS(Irp->IoStatus.Status) || Length < UAS_SENSE_IU_HEADER || Iu[0] != UAS_IU_SENSE ||
             ((Iu[2] << 8) | Iu[3]) != UAS_TAG)
    {
        DPRINT1("USBSTOR: UAS status failed %x length %lu IU %x\n", Context->Urb.UrbHeader.Status, Length,
                Length ? Iu[0] : 0);
        UasStreamsFail(FDODeviceExtension);
    }
    else if (!UasTakeSense(FDODeviceExtension->ActiveSrb, Iu, Length))
    {
        InterlockedOr(&FDODeviceExtension->UasState, UAS_STATE_SENSE_ERROR);
        UasCancelPending(FDODeviceExtension, TRUE);
    }
    if (InterlockedDecrement(&FDODeviceExtension->UasOutstanding) != 0)
        return STATUS_MORE_PROCESSING_REQUIRED;
    if (!UasStreamsFinish(FDODeviceExtension, Irp))
        return STATUS_MORE_PROCESSING_REQUIRED;
    return STATUS_CONTINUE_COMPLETION;
}

static
NTSTATUS
UasStreamsExecute(
    IN PFDO_DEVICE_EXTENSION FDODeviceExtension,
    IN PIRP Irp)
{
    PSCSI_REQUEST_BLOCK Request = FDODeviceExtension->ActiveSrb;
    BOOLEAN In = (Request->SrbFlags & SRB_FLAGS_UNSPECIFIED_DIRECTION) == SRB_FLAGS_DATA_IN;
    BOOLEAN Data = UasHasData(Request);

    ASSERT(FDODeviceExtension->UasDataMdl == NULL);
    FDODeviceExtension->UasState = 0;
    if (Data)
    {
        FDODeviceExtension->UasDataMdl = UasDataMdl(Irp, Request);
        if (!FDODeviceExtension->UasDataMdl)
        {
            USBSTOR_SetResetRecoveryStatus(FDODeviceExtension, Irp, Request);
            if (USBSTOR_FinishRequest(FDODeviceExtension, Irp, TRUE))
                IoCompleteRequest(Irp, IO_NO_INCREMENT);
            return STATUS_PENDING;
        }
        FDODeviceExtension->UasState = UAS_STATE_DATA;
    }
    FDODeviceExtension->UasOutstanding = Data ? 4 : 3;
    IoReuseIrp(FDODeviceExtension->UasCommandIrp, STATUS_SUCCESS);
    IoReuseIrp(FDODeviceExtension->UasDataIrp, STATUS_SUCCESS);
    RtlZeroMemory(FDODeviceExtension->UasStatus, UAS_STATUS_IU_SIZE);
    UasSubmit(FDODeviceExtension, Irp, &FDODeviceExtension->CurrentIrpContext.Urb, UAS_PIPE_STATUS,
              USBD_TRANSFER_DIRECTION_IN | USBD_SHORT_TRANSFER_OK, UAS_STATUS_IU_SIZE,
              FDODeviceExtension->UasStatus, NULL, UasStreamsStatusCompletion);
    if (Data)
    {
        UasSubmit(FDODeviceExtension, FDODeviceExtension->UasDataIrp, &FDODeviceExtension->UasDataUrb,
                  In ? UAS_PIPE_DATA_IN : UAS_PIPE_DATA_OUT,
                  In ? USBD_TRANSFER_DIRECTION_IN | USBD_SHORT_TRANSFER_OK : USBD_TRANSFER_DIRECTION_OUT,
                  Request->DataTransferLength, NULL, FDODeviceExtension->UasDataMdl, UasStreamsDataCompletion);
    }
    UasSubmit(FDODeviceExtension, FDODeviceExtension->UasCommandIrp, &FDODeviceExtension->UasCommandUrb,
              UAS_PIPE_COMMAND, USBD_TRANSFER_DIRECTION_OUT, UAS_COMMAND_IU_SIZE, FDODeviceExtension->UasCommand,
              NULL, UasStreamsCommandCompletion);
    if (InterlockedOr(&FDODeviceExtension->UasState, 0) & UAS_STATE_FAILED)
        UasCancelPending(FDODeviceExtension, FALSE);
    UasStreamsRelease(FDODeviceExtension);
    return STATUS_PENDING;
}

NTSTATUS
USBSTOR_UasExecute(
    IN PFDO_DEVICE_EXTENSION FDODeviceExtension,
    IN PIRP Irp)
{
    PIO_STACK_LOCATION IoStack = IoGetCurrentIrpStackLocation(Irp);
    PPDO_DEVICE_EXTENSION PDODeviceExtension = IoStack->DeviceObject->DeviceExtension;
    PSCSI_REQUEST_BLOCK Request = IoStack->Parameters.Scsi.Srb;
    PUCHAR Iu = FDODeviceExtension->UasCommand;

    ASSERT(Request == FDODeviceExtension->ActiveSrb);
    ASSERT(Request->CdbLength <= 16);
    RtlZeroMemory(Iu, UAS_COMMAND_IU_SIZE);
    Iu[0] = UAS_IU_COMMAND;
    Iu[2] = (UCHAR)(UAS_TAG >> 8);
    Iu[3] = (UCHAR)UAS_TAG;
    Iu[9] = PDODeviceExtension->LUN;
    RtlCopyMemory(Iu + 16, Request->Cdb, min(Request->CdbLength, 16));
    FDODeviceExtension->CurrentIrpContext.Irp = Irp;
    FDODeviceExtension->CurrentIrpContext.StallRetryCount = 0;
    if (FDODeviceExtension->UasStreams)
        return UasStreamsExecute(FDODeviceExtension, Irp);
    return USBSTOR_IssueBulkOrInterruptRequest(FDODeviceExtension, Irp,
                                               UasPipeHandle(FDODeviceExtension, UAS_PIPE_COMMAND),
                                               USBD_TRANSFER_DIRECTION_OUT, UAS_COMMAND_IU_SIZE, Iu, NULL,
                                               UasCommandCompletion);
}
