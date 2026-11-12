/*
 * PROJECT:     ReactOS Storport Driver
 * LICENSE:     GPL-2.0+ (https://spdx.org/licenses/GPL-2.0+)
 * PURPOSE:     Storport FDO code
 * COPYRIGHT:   Copyright 2017 Eric Kohl (eric.kohl@reactos.org)
 *              Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

/* INCLUDES *******************************************************************/

#include "precomp.h"

#define NDEBUG
#include <debug.h>


/* FUNCTIONS ******************************************************************/

static
BOOLEAN
NTAPI
PortFdoInterruptRoutine(
    _In_ PKINTERRUPT Interrupt,
    _In_ PVOID ServiceContext)
{
    PFDO_DEVICE_EXTENSION DeviceExtension;

    DPRINT("PortFdoInterruptRoutine(%p %p)\n", Interrupt, ServiceContext);

    DeviceExtension = (PFDO_DEVICE_EXTENSION)ServiceContext;

    return MiniportHwInterrupt(&DeviceExtension->Miniport);
}

static
BOOLEAN
NTAPI
PortFdoMessageInterruptRoutine(
    _In_ PKINTERRUPT Interrupt,
    _In_ PVOID ServiceContext,
    _In_ ULONG MessageId)
{
    PFDO_DEVICE_EXTENSION DeviceExtension = (PFDO_DEVICE_EXTENSION)ServiceContext;

    UNREFERENCED_PARAMETER(Interrupt);
    return MiniportHwMSInterrupt(&DeviceExtension->Miniport, MessageId);
}

/*
 * A miniport that registered a message routine in its port configuration is
 * asking for message-signalled interrupts. The kernel falls back to a plain
 * line connection by itself when the device was not granted any messages, so
 * a failure here still lands on the wired-interrupt path below.
 */
static
NTSTATUS
PortFdoConnectMessageInterrupts(
    _In_ PFDO_DEVICE_EXTENSION DeviceExtension)
{
    IO_CONNECT_INTERRUPT_PARAMETERS Parameters;
    NTSTATUS Status;

    if (DeviceExtension->Miniport.PortConfig.HwMSInterruptRoutine == NULL)
        return STATUS_NOT_SUPPORTED;

    RtlZeroMemory(&Parameters, sizeof(Parameters));
    Parameters.Version = CONNECT_MESSAGE_BASED;
    Parameters.MessageBased.PhysicalDeviceObject = DeviceExtension->PhysicalDevice;
    Parameters.MessageBased.ConnectionContext.InterruptMessageTable = &DeviceExtension->MessageInfo;
    Parameters.MessageBased.MessageServiceRoutine = PortFdoMessageInterruptRoutine;
    Parameters.MessageBased.ServiceContext = DeviceExtension;
    Parameters.MessageBased.FallBackServiceRoutine = PortFdoInterruptRoutine;
    if (DeviceExtension->Miniport.PortConfig.InterruptSynchronizationMode != InterruptSynchronizePerMessage)
        Parameters.MessageBased.SpinLock = &DeviceExtension->MessageInterruptLock;

    Status = IoConnectInterruptEx(&Parameters);
    if (!NT_SUCCESS(Status))
        return Status;

    if (Parameters.Version == CONNECT_MESSAGE_BASED)
    {
        DeviceExtension->InterruptIrql = DeviceExtension->MessageInfo->UnifiedIrql;
        DPRINT1("Storport: %lu message interrupts connected\n", DeviceExtension->MessageInfo->MessageCount);
    }
    else
    {
        ULONG Vector;
        KIRQL Irql;
        KINTERRUPT_MODE InterruptMode;
        BOOLEAN ShareVector;
        KAFFINITY Affinity;

        /* The kernel connected the fallback line interrupt instead. */
        DeviceExtension->Interrupt = (PKINTERRUPT)DeviceExtension->MessageInfo;
        DeviceExtension->InterruptConnectedEx = TRUE;
        DeviceExtension->MessageInfo = NULL;
        if (DeviceExtension->TranslatedResources != NULL &&
            NT_SUCCESS(GetResourceListInterrupt(DeviceExtension, &Vector, &Irql,
                                                &InterruptMode, &ShareVector, &Affinity)))
        {
            DeviceExtension->InterruptIrql = Irql;
        }
        DPRINT1("Storport: message connect fell back to a line interrupt\n");
    }
    return STATUS_SUCCESS;
}


static
NTSTATUS
PortFdoConnectInterrupt(
    _In_ PFDO_DEVICE_EXTENSION DeviceExtension)
{
    ULONG Vector;
    KIRQL Irql;
    KINTERRUPT_MODE InterruptMode;
    BOOLEAN ShareVector;
    KAFFINITY Affinity;
    NTSTATUS Status;

    DPRINT("PortFdoConnectInterrupt(%p)\n",
            DeviceExtension);

    if (NT_SUCCESS(PortFdoConnectMessageInterrupts(DeviceExtension)))
        return STATUS_SUCCESS;

    /* No resources, no interrupt. Done! */
    if (DeviceExtension->AllocatedResources == NULL ||
        DeviceExtension->TranslatedResources == NULL)
    {
        DPRINT1("Checkpoint\n");
        return STATUS_SUCCESS;
    }

    /* Get the interrupt data from the resource list */
    Status = GetResourceListInterrupt(DeviceExtension,
                                      &Vector,
                                      &Irql,
                                      &InterruptMode,
                                      &ShareVector,
                                      &Affinity);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("GetResourceListInterrupt() failed (Status 0x%08lx)\n", Status);
        return Status;
    }

    DPRINT("Vector: %lu\n", Vector);
    DPRINT("Irql: %lu\n", Irql);

    DPRINT("Affinity: 0x%08lx\n", Affinity);

    /* Connect the interrupt */
    Status = IoConnectInterrupt(&DeviceExtension->Interrupt,
                                PortFdoInterruptRoutine,
                                DeviceExtension,
                                NULL,
                                Vector,
                                Irql,
                                Irql,
                                InterruptMode,
                                ShareVector,
                                Affinity,
                                FALSE);
    if (NT_SUCCESS(Status))
    {
        DeviceExtension->InterruptIrql = Irql;
    }
    else
    {
        DPRINT1("IoConnectInterrupt() failed (Status 0x%08lx)\n", Status);
    }

    return Status;
}


static
NTSTATUS
PortFdoInitializeMiniport(
    _In_ PFDO_DEVICE_EXTENSION DeviceExtension)
{
    PHW_INITIALIZATION_DATA InitData;
    INTERFACE_TYPE InterfaceType;

    /* Get the interface type of the lower device */
    InterfaceType = GetBusInterface(DeviceExtension->LowerDevice);

    /* Get the driver init data for the given interface type */
    InitData = NULL;
    if (InterfaceType != InterfaceTypeUndefined)
        InitData = PortGetDriverInitData(DeviceExtension->DriverExtension, InterfaceType);
    if (InitData == NULL)
        InitData = PortGetVirtualDriverInitData(DeviceExtension->DriverExtension);
    if (InitData == NULL)
        return STATUS_NO_SUCH_DEVICE;

    /* Initialize the miniport */
    return MiniportInitialize(&DeviceExtension->Miniport, DeviceExtension, InitData);
}


static
NTSTATUS
PortFdoStartMiniport(
    _In_ PFDO_DEVICE_EXTENSION DeviceExtension)
{
    STOR_LOCK_HANDLE LockHandle;
    NTSTATUS Status;

    DPRINT("PortFdoStartDevice(%p)\n", DeviceExtension);

    Status = PortFdoInitializeMiniport(DeviceExtension);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("MiniportInitialize() failed (Status 0x%08lx)\n", Status);
        return Status;
    }

    /* Call the miniports FindAdapter function */
    Status = MiniportFindAdapter(&DeviceExtension->Miniport);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("MiniportFindAdapter() failed (Status 0x%08lx)\n", Status);
        return Status;
    }

    MiniportQuerySupportedControlTypes(&DeviceExtension->Miniport);
    MiniportQuerySupportedUnitControlTypes(&DeviceExtension->Miniport);

    if (!DeviceExtension->Miniport.PortConfig.VirtualDevice)
    {
        Status = PortInitializeDma(DeviceExtension, &DeviceExtension->Miniport.PortConfig);
        if (!NT_SUCCESS(Status))
            return Status;
    }

    /* The request sizes are settled now; back them with lookasides. */
    Status = PortFdoInitializeRequestPools(DeviceExtension);
    if (!NT_SUCCESS(Status))
        return Status;

    if (!DeviceExtension->Miniport.PortConfig.VirtualDevice)
    {
        /* Connect the configured interrupt */
        Status = PortFdoConnectInterrupt(DeviceExtension);
        if (!NT_SUCCESS(Status))
        {
            DPRINT1("PortFdoConnectInterrupt() failed (Status 0x%08lx)\n", Status);
            return Status;
        }
    }

    /* Call the miniports HwInitialize function */
    PortAcquireSpinLock(DeviceExtension, InterruptLock, NULL, &LockHandle);
    Status = MiniportHwInitialize(&DeviceExtension->Miniport);
    PortReleaseSpinLock(DeviceExtension, &LockHandle);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("MiniportHwInitialize() failed (Status 0x%08lx)\n", Status);
        return Status;
    }

    /* Call the HwPassiveInitRoutine function, if available */
    if (DeviceExtension->HwPassiveInitRoutine != NULL)
    {
        DPRINT("Calling HwPassiveInitRoutine()\n");
        if (!DeviceExtension->HwPassiveInitRoutine(&DeviceExtension->Miniport.MiniportExtension->HwDeviceExtension))
        {
            DPRINT1("HwPassiveInitRoutine() failed\n");
            return STATUS_UNSUCCESSFUL;
        }
    }

    return STATUS_SUCCESS;
}


static
NTSTATUS
NTAPI
PortFdoStartDevice(
    _In_ PFDO_DEVICE_EXTENSION DeviceExtension,
    _In_ PIRP Irp)
{
    PIO_STACK_LOCATION Stack;
    NTSTATUS Status;

    DPRINT("PortFdoStartDevice(%p %p)\n",
            DeviceExtension, Irp);

    ASSERT(DeviceExtension->ExtensionType == FdoExtension);

    /* Get the current stack location */
    Stack = IoGetCurrentIrpStackLocation(Irp);

    if (DeviceExtension->PnpState == dsStarted)
    {
        if (!IoForwardIrpSynchronously(DeviceExtension->LowerDevice, Irp))
            return STATUS_UNSUCCESSFUL;
        return Irp->IoStatus.Status;
    }

    /* Start the lower device if the FDO is in 'stopped' state */
    if (DeviceExtension->PnpState == dsStopped)
    {
        if (IoForwardIrpSynchronously(DeviceExtension->LowerDevice, Irp))
        {
            Status = Irp->IoStatus.Status;
        }
        else
        {
            Status = STATUS_UNSUCCESSFUL;
        }

        if (!NT_SUCCESS(Status))
        {
            DPRINT1("Lower device failed the IRP (Status 0x%08lx)\n", Status);
            return Status;
        }
    }

    /* Change to the 'started' state */
    DeviceExtension->PnpState = dsStarted;
    PortFdoStartTimeouts(DeviceExtension);

    /* Copy the raw and translated resource lists into the device extension */
    if (Stack->Parameters.StartDevice.AllocatedResources != NULL &&
        Stack->Parameters.StartDevice.AllocatedResourcesTranslated != NULL)
    {
        DeviceExtension->AllocatedResources = CopyResourceList(NonPagedPool,
                                                               Stack->Parameters.StartDevice.AllocatedResources);
        if (DeviceExtension->AllocatedResources == NULL)
            return STATUS_NO_MEMORY;

        DeviceExtension->TranslatedResources = CopyResourceList(NonPagedPool,
                                                                Stack->Parameters.StartDevice.AllocatedResourcesTranslated);
        if (DeviceExtension->TranslatedResources == NULL)
            return STATUS_NO_MEMORY;
    }

    /* Get the bus interface of the lower (bus) device */
    Status = QueryBusInterface(DeviceExtension->LowerDevice,
                               (PGUID)&GUID_BUS_INTERFACE_STANDARD,
                               sizeof(BUS_INTERFACE_STANDARD),
                               1,
                               &DeviceExtension->BusInterface,
                               NULL);
    DPRINT("Status: 0x%08lx\n", Status);
    if (NT_SUCCESS(Status))
    {
        DPRINT("Context: %p\n", DeviceExtension->BusInterface.Context);
        DeviceExtension->BusInitialized = TRUE;
    }

    /* Start the miniport (FindAdapter & Initialize) */
    Status = PortFdoStartMiniport(DeviceExtension);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("FdoStartMiniport() failed (Status 0x%08lx)\n", Status);
        DeviceExtension->PnpState = dsStopped;
    }
    else
    {
        PortRegisterAdapterMap(DeviceExtension);
    }

    return Status;
}


static
VOID
PortReadSerialNumber(
    _In_ PPDO_DEVICE_EXTENSION PdoExtension)
{
    IO_STATUS_BLOCK IoStatusBlock;
    KEVENT Event;
    PIRP Irp;
    NTSTATUS Status;
    SCSI_REQUEST_BLOCK Srb;
    PCDB Cdb;
    PUCHAR Buffer;
    UCHAR SrbStatus;
    ULONG Length;

    /* The unit serial number VPD page is optional, so any failure leaves the serial empty */
    PdoExtension->SerialNumber[0] = ANSI_NULL;

    Buffer = ExAllocatePoolWithTag(NonPagedPool, MAXUCHAR + SENSE_BUFFER_SIZE, TAG_INQUIRY_DATA);
    if (Buffer == NULL)
        return;

    RtlZeroMemory(Buffer, MAXUCHAR + SENSE_BUFFER_SIZE);

    KeInitializeEvent(&Event,
                      NotificationEvent,
                      FALSE);

    Irp = IoBuildDeviceIoControlRequest(IOCTL_SCSI_EXECUTE_IN,
                                        PdoExtension->Device,
                                        NULL,
                                        0,
                                        Buffer,
                                        MAXUCHAR,
                                        TRUE,
                                        &Event,
                                        &IoStatusBlock);
    if (Irp == NULL)
        goto Done;

    RtlZeroMemory(&Srb, sizeof(SCSI_REQUEST_BLOCK));

    Srb.Length = sizeof(SCSI_REQUEST_BLOCK);
    Srb.OriginalRequest = Irp;
    Srb.PathId = PdoExtension->Bus;
    Srb.TargetId = PdoExtension->Target;
    Srb.Lun = PdoExtension->Lun;
    Srb.Function = SRB_FUNCTION_EXECUTE_SCSI;
    Srb.SrbFlags = SRB_FLAGS_DATA_IN | SRB_FLAGS_DISABLE_SYNCH_TRANSFER | SRB_FLAGS_NO_QUEUE_FREEZE;
    Srb.TimeOutValue = 4;
    Srb.CdbLength = 6;

    Srb.SenseInfoBuffer = Buffer + MAXUCHAR;
    Srb.SenseInfoBufferLength = SENSE_BUFFER_SIZE;

    Srb.DataBuffer = Buffer;
    Srb.DataTransferLength = MAXUCHAR;

    IoGetNextIrpStackLocation(Irp)->Parameters.Scsi.Srb = &Srb;

    Cdb = (PCDB)Srb.Cdb;
    Cdb->CDB6INQUIRY3.OperationCode = SCSIOP_INQUIRY;
    Cdb->CDB6INQUIRY3.EnableVitalProductData = 1;
    Cdb->CDB6INQUIRY3.PageCode = VPD_SERIAL_NUMBER;
    Cdb->CDB6INQUIRY3.AllocationLength = MAXUCHAR;

    Status = IoCallDriver(PdoExtension->Device, Irp);
    if (Status == STATUS_PENDING)
    {
        KeWaitForSingleObject(&Event,
                              Executive,
                              KernelMode,
                              FALSE,
                              NULL);
    }

    /* A short page completes with SRB_STATUS_DATA_OVERRUN (underrun) */
    SrbStatus = SRB_STATUS(Srb.SrbStatus);
    if ((SrbStatus != SRB_STATUS_SUCCESS && SrbStatus != SRB_STATUS_DATA_OVERRUN) ||
        Srb.DataTransferLength < 4 ||
        Buffer[1] != VPD_SERIAL_NUMBER)
    {
        goto Done;
    }

    /* Page header: bytes 2-3 hold the big-endian serial number length */
    Length = ((ULONG)Buffer[2] << 8) | Buffer[3];
    Length = min(Length, Srb.DataTransferLength - 4);
    Length = min(Length, sizeof(PdoExtension->SerialNumber) - 1);

    /* Drop the trailing padding */
    while (Length > 0 && (Buffer[3 + Length] == ' ' || Buffer[3 + Length] == ANSI_NULL))
        Length--;

    RtlCopyMemory(PdoExtension->SerialNumber, Buffer + 4, Length);
    PdoExtension->SerialNumber[Length] = ANSI_NULL;

Done:
    ExFreePoolWithTag(Buffer, TAG_INQUIRY_DATA);
}


static
NTSTATUS
PortSendInquiry(
    _In_ PPDO_DEVICE_EXTENSION PdoExtension)
{
    IO_STATUS_BLOCK IoStatusBlock;
    PIO_STACK_LOCATION IrpStack;
    KEVENT Event;
//    KIRQL Irql;
    PIRP Irp;
    NTSTATUS Status;
    PSENSE_DATA SenseBuffer;
    BOOLEAN KeepTrying = TRUE;
    ULONG RetryCount = 0;
    SCSI_REQUEST_BLOCK Srb;
    PCDB Cdb;
//    PSCSI_PORT_LUN_EXTENSION LunExtension;
//    PFDO_DEVICE_EXTENSION DeviceExtension;

    DPRINT("PortSendInquiry(%p)\n", PdoExtension);

    if (PdoExtension->InquiryBuffer == NULL)
    {
        PdoExtension->InquiryBuffer = ExAllocatePoolWithTag(NonPagedPool, INQUIRYDATABUFFERSIZE, TAG_INQUIRY_DATA);
        if (PdoExtension->InquiryBuffer == NULL)
            return STATUS_INSUFFICIENT_RESOURCES;
    }

    SenseBuffer = ExAllocatePoolWithTag(NonPagedPool, SENSE_BUFFER_SIZE, TAG_SENSE_DATA);
    if (SenseBuffer == NULL)
    {
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    while (KeepTrying)
    {
        RtlZeroMemory(SenseBuffer, SENSE_BUFFER_SIZE);

        /* Initialize event for waiting */
        KeInitializeEvent(&Event,
                          NotificationEvent,
                          FALSE);

        /* Create an IRP */
        Irp = IoBuildDeviceIoControlRequest(IOCTL_SCSI_EXECUTE_IN,
                                            PdoExtension->Device,
                                            NULL,
                                            0,
                                            PdoExtension->InquiryBuffer,
                                            INQUIRYDATABUFFERSIZE,
                                            TRUE,
                                            &Event,
                                            &IoStatusBlock);
        if (Irp == NULL)
        {
            DPRINT("IoBuildDeviceIoControlRequest() failed\n");

            /* Quit the loop */
            Status = STATUS_INSUFFICIENT_RESOURCES;
            KeepTrying = FALSE;
            continue;
        }

        /* Prepare SRB */
        RtlZeroMemory(&Srb, sizeof(SCSI_REQUEST_BLOCK));

        Srb.Length = sizeof(SCSI_REQUEST_BLOCK);
        Srb.OriginalRequest = Irp;
        Srb.PathId = PdoExtension->Bus;
        Srb.TargetId = PdoExtension->Target;
        Srb.Lun = PdoExtension->Lun;
        Srb.Function = SRB_FUNCTION_EXECUTE_SCSI;
        Srb.SrbFlags = SRB_FLAGS_DATA_IN | SRB_FLAGS_DISABLE_SYNCH_TRANSFER;
        Srb.TimeOutValue = 4;
        Srb.CdbLength = 6;

        Srb.SenseInfoBuffer = SenseBuffer;
        Srb.SenseInfoBufferLength = SENSE_BUFFER_SIZE;

        Srb.DataBuffer = PdoExtension->InquiryBuffer;
        Srb.DataTransferLength = INQUIRYDATABUFFERSIZE;

        /* Attach Srb to the Irp */
        IrpStack = IoGetNextIrpStackLocation(Irp);
        IrpStack->Parameters.Scsi.Srb = &Srb;

        /* Fill in CDB */
        Cdb = (PCDB)Srb.Cdb;
        Cdb->CDB6INQUIRY.OperationCode = SCSIOP_INQUIRY;
        Cdb->CDB6INQUIRY.LogicalUnitNumber = PdoExtension->Lun;
        Cdb->CDB6INQUIRY.AllocationLength = INQUIRYDATABUFFERSIZE;

        /* Call the driver */
        Status = IoCallDriver(PdoExtension->Device, Irp);

        /* Wait for it to complete */
        if (Status == STATUS_PENDING)
        {
            DPRINT("PortSendInquiry(): Waiting for the driver to process request...\n");
            KeWaitForSingleObject(&Event,
                                  Executive,
                                  KernelMode,
                                  FALSE,
                                  NULL);
            Status = IoStatusBlock.Status;
        }

        DPRINT("PortSendInquiry(): Request processed by driver, status = 0x%08X\n", Status);
        if (Srb.SrbStatus & SRB_STATUS_AUTOSENSE_VALID)
            DPRINT1("StorPort: inquiry sense key 0x%02x ASC 0x%02x ASCQ 0x%02x\n", SenseBuffer->SenseKey, SenseBuffer->AdditionalSenseCode, SenseBuffer->AdditionalSenseCodeQualifier);

        if (SRB_STATUS(Srb.SrbStatus) == SRB_STATUS_SUCCESS)
        {
            DPRINT("Found a device!\n");

            /* Quit the loop */
            Status = STATUS_SUCCESS;
            KeepTrying = FALSE;
            continue;
        }

        DPRINT("Inquiry SRB failed with SrbStatus 0x%08X\n", Srb.SrbStatus);

        /* Check if the queue is frozen */
        if (Srb.SrbStatus & SRB_STATUS_QUEUE_FROZEN)
        {
            /* Something weird happened, deal with it (unfreeze the queue) */
            KeepTrying = FALSE;

            DPRINT("SpiSendInquiry(): the queue is frozen at TargetId %d\n", Srb.TargetId);

//            LunExtension = SpiGetLunExtension(DeviceExtension,
//                                              LunInfo->PathId,
//                                              LunInfo->TargetId,
//                                              LunInfo->Lun);

            /* Clear frozen flag */
//            LunExtension->Flags &= ~LUNEX_FROZEN_QUEUE;

            /* Acquire the spinlock */
//            KeAcquireSpinLock(&DeviceExtension->SpinLock, &Irql);

            /* Process the request */
//            SpiGetNextRequestFromLun(DeviceObject->DeviceExtension, LunExtension);

            /* SpiGetNextRequestFromLun() releases the spinlock,
                so we just lower irql back to what it was before */
//            KeLowerIrql(Irql);
        }

        /* Check if data overrun happened */
        if (SRB_STATUS(Srb.SrbStatus) == SRB_STATUS_DATA_OVERRUN)
        {
            DPRINT("Data overrun at TargetId %d\n", PdoExtension->Target);

            /* Quit the loop */
            Status = STATUS_SUCCESS;
            KeepTrying = FALSE;
        }
        else if ((Srb.SrbStatus & SRB_STATUS_AUTOSENSE_VALID) &&
                 SenseBuffer->SenseKey == SCSI_SENSE_ILLEGAL_REQUEST)
        {
            /* LUN is not valid, but some device responds there.
                Mark it as invalid anyway */

            /* Quit the loop */
            Status = STATUS_INVALID_DEVICE_REQUEST;
            KeepTrying = FALSE;
        }
        else
        {
            /* Retry a couple of times if no timeout happened */
            if ((RetryCount < 2) &&
                (SRB_STATUS(Srb.SrbStatus) != SRB_STATUS_NO_DEVICE) &&
                (SRB_STATUS(Srb.SrbStatus) != SRB_STATUS_SELECTION_TIMEOUT))
            {
                RetryCount++;
                KeepTrying = TRUE;
            }
            else
            {
                /* That's all, quit the loop */
                KeepTrying = FALSE;

                /* Set status according to SRB status */
                if (SRB_STATUS(Srb.SrbStatus) == SRB_STATUS_BAD_FUNCTION ||
                    SRB_STATUS(Srb.SrbStatus) == SRB_STATUS_BAD_SRB_BLOCK_LENGTH)
                {
                    Status = STATUS_INVALID_DEVICE_REQUEST;
                }
                else
                {
                    Status = STATUS_IO_DEVICE_ERROR;
                }
            }
        }
    }

    /* Free the sense buffer */
    ExFreePoolWithTag(SenseBuffer, TAG_SENSE_DATA);

    DPRINT("PortSendInquiry() done with Status 0x%08X\n", Status);

    return Status;
}



#define PORT_REPORT_LUNS_LENGTH (8 + 8 * 256)

static
NTSTATUS
PortReportLuns(
    _In_ PPDO_DEVICE_EXTENSION PdoExtension,
    _In_ ULONG MaximumLuns,
    _Out_writes_(MaximumLuns) PBOOLEAN Present)
{
    IO_STATUS_BLOCK IoStatusBlock;
    SCSI_REQUEST_BLOCK Srb;
    KEVENT Event;
    PUCHAR Buffer, Entry;
    PCDB Cdb;
    PIRP Irp;
    ULONG Length, Offset, Lun;
    UCHAR SrbStatus;
    NTSTATUS Status = STATUS_UNSUCCESSFUL;

    Buffer = ExAllocatePoolWithTag(NonPagedPool, PORT_REPORT_LUNS_LENGTH + SENSE_BUFFER_SIZE, TAG_INQUIRY_DATA);
    if (Buffer == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;
    RtlZeroMemory(Buffer, PORT_REPORT_LUNS_LENGTH + SENSE_BUFFER_SIZE);

    KeInitializeEvent(&Event, NotificationEvent, FALSE);
    Irp = IoBuildDeviceIoControlRequest(IOCTL_SCSI_EXECUTE_IN,
                                        PdoExtension->Device,
                                        NULL,
                                        0,
                                        Buffer,
                                        PORT_REPORT_LUNS_LENGTH,
                                        TRUE,
                                        &Event,
                                        &IoStatusBlock);
    if (Irp == NULL)
    {
        ExFreePoolWithTag(Buffer, TAG_INQUIRY_DATA);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    RtlZeroMemory(&Srb, sizeof(SCSI_REQUEST_BLOCK));
    Srb.Length = sizeof(SCSI_REQUEST_BLOCK);
    Srb.OriginalRequest = Irp;
    Srb.PathId = PdoExtension->Bus;
    Srb.TargetId = PdoExtension->Target;
    Srb.Lun = PdoExtension->Lun;
    Srb.Function = SRB_FUNCTION_EXECUTE_SCSI;
    Srb.SrbFlags = SRB_FLAGS_DATA_IN | SRB_FLAGS_DISABLE_SYNCH_TRANSFER | SRB_FLAGS_NO_QUEUE_FREEZE;
    Srb.TimeOutValue = 4;
    Srb.CdbLength = 12;
    Srb.SenseInfoBuffer = Buffer + PORT_REPORT_LUNS_LENGTH;
    Srb.SenseInfoBufferLength = SENSE_BUFFER_SIZE;
    Srb.DataBuffer = Buffer;
    Srb.DataTransferLength = PORT_REPORT_LUNS_LENGTH;
    IoGetNextIrpStackLocation(Irp)->Parameters.Scsi.Srb = &Srb;

    Cdb = (PCDB)Srb.Cdb;
    Cdb->REPORT_LUNS.OperationCode = SCSIOP_REPORT_LUNS;
    Cdb->REPORT_LUNS.AllocationLength[0] = (UCHAR)(PORT_REPORT_LUNS_LENGTH >> 24);
    Cdb->REPORT_LUNS.AllocationLength[1] = (UCHAR)(PORT_REPORT_LUNS_LENGTH >> 16);
    Cdb->REPORT_LUNS.AllocationLength[2] = (UCHAR)(PORT_REPORT_LUNS_LENGTH >> 8);
    Cdb->REPORT_LUNS.AllocationLength[3] = (UCHAR)PORT_REPORT_LUNS_LENGTH;

    if (IoCallDriver(PdoExtension->Device, Irp) == STATUS_PENDING)
        KeWaitForSingleObject(&Event, Executive, KernelMode, FALSE, NULL);

    SrbStatus = SRB_STATUS(Srb.SrbStatus);
    if (((SrbStatus == SRB_STATUS_SUCCESS) || (SrbStatus == SRB_STATUS_DATA_OVERRUN)) &&
        (Srb.DataTransferLength >= 8))
    {
        Length = ((ULONG)Buffer[0] << 24) | ((ULONG)Buffer[1] << 16) | ((ULONG)Buffer[2] << 8) | Buffer[3];
        Length = min(Length, Srb.DataTransferLength - 8);
        for (Offset = 0; Offset + 8 <= Length; Offset += 8)
        {
            Entry = Buffer + 8 + Offset;
            if ((Entry[0] >> 6) == 0)
            {
                if (Entry[0] != 0)
                    continue;
                Lun = Entry[1];
            }
            else if ((Entry[0] >> 6) == 1)
            {
                Lun = ((ULONG)(Entry[0] & 0x3F) << 8) | Entry[1];
            }
            else
            {
                continue;
            }

            if (Lun < MaximumLuns)
                Present[Lun] = TRUE;
        }
        Status = STATUS_SUCCESS;
    }

    ExFreePoolWithTag(Buffer, TAG_INQUIRY_DATA);
    return Status;
}


static
VOID
PortReadRichDescription(
    _In_ PPDO_DEVICE_EXTENSION PdoExtension)
{
    STOR_RICH_DEVICE_DESCRIPTION_V2 Description;
    STOR_ADDR_BTL8 Address;
    ULONG Length;

    PdoExtension->ModelNumber[0] = ANSI_NULL;
    PdoExtension->FirmwareRevision[0] = ANSI_NULL;

    RtlZeroMemory(&Address, sizeof(Address));
    Address.Type = STOR_ADDRESS_TYPE_BTL8;
    Address.Port = (USHORT)PdoExtension->FdoExtension->PortNumber;
    Address.AddressLength = STOR_ADDR_BTL8_ADDRESS_LENGTH;
    Address.Path = (UCHAR)PdoExtension->Bus;
    Address.Target = (UCHAR)PdoExtension->Target;
    Address.Lun = (UCHAR)PdoExtension->Lun;

    RtlZeroMemory(&Description, sizeof(Description));
    Description.Version = STOR_RICH_DEVICE_DESCRIPTION_STRUCTURE_VERSION_V2;
    Description.Size = sizeof(Description);
    Description.Address = (PSTOR_ADDRESS)&Address;

    if (MiniportUnitControl(&PdoExtension->FdoExtension->Miniport,
                            ScsiUnitRichDescription,
                            &Description) != ScsiUnitControlSuccess)
    {
        return;
    }

    Description.ModelNumber[STOR_MODEL_NUMBER_LENGTH] = ANSI_NULL;
    Length = (ULONG)strlen(Description.ModelNumber);
    while ((Length > 0) && (Description.ModelNumber[Length - 1] == ' '))
        Length--;

    RtlCopyMemory(PdoExtension->ModelNumber, Description.ModelNumber, Length);
    PdoExtension->ModelNumber[Length] = ANSI_NULL;

    Description.FirmwareRevision[STOR_FIRMWARE_REVISION_LENGTH] = ANSI_NULL;
    Length = (ULONG)strlen(Description.FirmwareRevision);
    while ((Length > 0) && (Description.FirmwareRevision[Length - 1] == ' '))
        Length--;

    RtlCopyMemory(PdoExtension->FirmwareRevision, Description.FirmwareRevision, Length);
    PdoExtension->FirmwareRevision[Length] = ANSI_NULL;
}


static
BOOLEAN
PortScanLun(
    _In_ PPDO_DEVICE_EXTENSION PdoExtension)
{
    NTSTATUS Status;

    Status = PortSendInquiry(PdoExtension);
    if (NT_SUCCESS(Status) &&
        (PdoExtension->InquiryBuffer->DeviceTypeQualifier == DEVICE_QUALIFIER_NOT_SUPPORTED))
    {
        Status = STATUS_NO_SUCH_DEVICE;
    }
    DPRINT("PortSendInquiry returned 0x%08lx\n", Status);
    if (!NT_SUCCESS(Status))
    {
        PortDeletePdo(PdoExtension);
        return FALSE;
    }

    PortReadSerialNumber(PdoExtension);
    PortReadRichDescription(PdoExtension);
    PortRegisterLunMap(PdoExtension);
    DPRINT("VendorId: %.8s\n", PdoExtension->InquiryBuffer->VendorId);
    DPRINT("ProductId: %.16s\n", PdoExtension->InquiryBuffer->ProductId);
    DPRINT("ProductRevisionLevel: %.4s\n", PdoExtension->InquiryBuffer->ProductRevisionLevel);
    DPRINT("VendorSpecific: %.20s\n", PdoExtension->InquiryBuffer->VendorSpecific);
    return TRUE;
}


static
BOOLEAN
PortFdoHasTarget(
    _In_ PFDO_DEVICE_EXTENSION DeviceExtension,
    _In_ ULONG Bus,
    _In_ ULONG Target)
{
    KLOCK_QUEUE_HANDLE LockHandle;
    PPDO_DEVICE_EXTENSION PdoExtension;
    PLIST_ENTRY Entry;
    BOOLEAN Found = FALSE;

    KeAcquireInStackQueuedSpinLock(&DeviceExtension->PdoListLock, &LockHandle);
    for (Entry = DeviceExtension->PdoListHead.Flink;
         Entry != &DeviceExtension->PdoListHead;
         Entry = Entry->Flink)
    {
        PdoExtension = CONTAINING_RECORD(Entry, PDO_DEVICE_EXTENSION, PdoListEntry);
        if ((PdoExtension->Bus == Bus) && (PdoExtension->Target == Target))
        {
            Found = TRUE;
            break;
        }
    }
    KeReleaseInStackQueuedSpinLock(&LockHandle);

    return Found;
}


static
NTSTATUS
PortFdoScanBus(
    _In_ PFDO_DEVICE_EXTENSION DeviceExtension)
{
    PPDO_DEVICE_EXTENSION PdoExtension;
    BOOLEAN Present[256];
    BOOLEAN Reported;
    ULONG Bus, Target, Lun, MaximumLuns;
    NTSTATUS Status;

    DPRINT("PortFdoScanBus(%p)\n", DeviceExtension);

    DPRINT("NumberOfBuses: %lu\n", DeviceExtension->Miniport.PortConfig.NumberOfBuses);
    DPRINT("MaximumNumberOfTargets: %lu\n", DeviceExtension->Miniport.PortConfig.MaximumNumberOfTargets);
    DPRINT("MaximumNumberOfLogicalUnits: %lu\n", DeviceExtension->Miniport.PortConfig.MaximumNumberOfLogicalUnits);

    MaximumLuns = DeviceExtension->Miniport.PortConfig.MaximumNumberOfLogicalUnits;

    /* Scan all buses */
    for (Bus = 0; Bus < DeviceExtension->Miniport.PortConfig.NumberOfBuses; Bus++)
    {
        DPRINT("Scanning bus %ld\n", Bus);

        /* Scan all targets */
        for (Target = 0; Target < DeviceExtension->Miniport.PortConfig.MaximumNumberOfTargets; Target++)
        {
            DPRINT("  Scanning target %ld:%ld\n", Bus, Target);

            if (PortFdoHasTarget(DeviceExtension, Bus, Target))
                continue;

            Status = PortCreatePdo(DeviceExtension, Bus, Target, 0, &PdoExtension);
            if (!NT_SUCCESS(Status))
                continue;

            RtlZeroMemory(Present, sizeof(Present));
            Reported = NT_SUCCESS(PortReportLuns(PdoExtension, MaximumLuns, Present));
            if (!Reported)
            {
                for (Lun = 1; Lun < MaximumLuns; Lun++)
                    Present[Lun] = TRUE;
            }

            if (!PortScanLun(PdoExtension) && !Reported)
                continue;

            for (Lun = 1; Lun < MaximumLuns; Lun++)
            {
                if (!Present[Lun])
                    continue;

                DPRINT("    Scanning logical unit %ld:%ld:%ld\n", Bus, Target, Lun);
                if (NT_SUCCESS(PortCreatePdo(DeviceExtension, Bus, Target, Lun, &PdoExtension)))
                    PortScanLun(PdoExtension);
            }
        }
    }

    DPRINT("PortFdoScanBus() done!\n");

    return STATUS_SUCCESS;
}


static
NTSTATUS
PortFdoQueryBusRelations(
    _In_ PFDO_DEVICE_EXTENSION DeviceExtension,
    _Out_ PULONG_PTR Information)
{
    ULONG Count, Index, Size;
    PLIST_ENTRY Entry;
    NTSTATUS Status;
    KLOCK_QUEUE_HANDLE LockHandle;
    PDEVICE_RELATIONS Relations;
    PPDO_DEVICE_EXTENSION PdoExtension;

    DPRINT("PortFdoQueryBusRelations(%p %p)\n",
            DeviceExtension, Information);

    if (!DeviceExtension->BusScanned || DeviceExtension->BusRescanPending)
    {
        DeviceExtension->BusRescanPending = FALSE;
        Status = PortFdoScanBus(DeviceExtension);
        if (!NT_SUCCESS(Status))
        {
            *Information = 0;
            return Status;
        }

        DeviceExtension->BusScanned = TRUE;
    }

    KeAcquireInStackQueuedSpinLock(&DeviceExtension->PdoListLock, &LockHandle);
    Count = DeviceExtension->PdoCount;
    KeReleaseInStackQueuedSpinLock(&LockHandle);

    Size = FIELD_OFFSET(DEVICE_RELATIONS, Objects) +
           Count * sizeof(PDEVICE_OBJECT);
    Relations = ExAllocatePoolWithTag(PagedPool,
                                      Size,
                                      TAG_DEVICE_RELATION);
    if (Relations == NULL)
    {
        *Information = 0;
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    Relations->Count = 0;
    Index = 0;

    KeAcquireInStackQueuedSpinLock(&DeviceExtension->PdoListLock, &LockHandle);
    for (Entry = DeviceExtension->PdoListHead.Flink;
         (Entry != &DeviceExtension->PdoListHead) && (Index < Count);
         Entry = Entry->Flink)
    {
        PdoExtension = CONTAINING_RECORD(Entry,
                                         PDO_DEVICE_EXTENSION,
                                         PdoListEntry);
        Relations->Objects[Index] = PdoExtension->Device;
        ObReferenceObject(PdoExtension->Device);
        Index++;
    }
    KeReleaseInStackQueuedSpinLock(&LockHandle);

    Relations->Count = Index;
    *Information = (ULONG_PTR)Relations;

    DPRINT("Units reported: %lu\n", Index);

    return STATUS_SUCCESS;
}


static
NTSTATUS
PortFdoFilterRequirements(
    PFDO_DEVICE_EXTENSION DeviceExtension,
    PIRP Irp)
{
    PIO_RESOURCE_REQUIREMENTS_LIST RequirementsList;
    NTSTATUS Status;

    DPRINT("PortFdoFilterRequirements(%p %p)\n", DeviceExtension, Irp);

    /* Get the bus number and the slot number */
    RequirementsList =(PIO_RESOURCE_REQUIREMENTS_LIST)Irp->IoStatus.Information;
    if (RequirementsList != NULL)
    {
        DeviceExtension->BusNumber = RequirementsList->BusNumber;
        DeviceExtension->SlotNumber = RequirementsList->SlotNumber;

        Status = PortFdoInitializeMiniport(DeviceExtension);
        if (!NT_SUCCESS(Status))
            return Status;

        Status = MiniportAdapterControlPreFind(&DeviceExtension->Miniport, RequirementsList);
        if (!NT_SUCCESS(Status))
            return Status;
    }

    return STATUS_SUCCESS;
}


NTSTATUS
NTAPI
PortFdoScsi(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp)
{
    PFDO_DEVICE_EXTENSION DeviceExtension;
//    PIO_STACK_LOCATION Stack;
    ULONG_PTR Information = 0;
    NTSTATUS Status = STATUS_NOT_SUPPORTED;

    DPRINT("PortFdoScsi(%p %p)\n", DeviceObject, Irp);

    DeviceExtension = (PFDO_DEVICE_EXTENSION)DeviceObject->DeviceExtension;
    ASSERT(DeviceExtension);
    ASSERT(DeviceExtension->ExtensionType == FdoExtension);

//    Stack = IoGetCurrentIrpStackLocation(Irp);


    Irp->IoStatus.Information = Information;
    Irp->IoStatus.Status = Status;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);

    return Status;
}


static
VOID
PortFdoDisconnectInterrupt(
    _In_ PFDO_DEVICE_EXTENSION DeviceExtension)
{
    IO_DISCONNECT_INTERRUPT_PARAMETERS Parameters;

    RtlZeroMemory(&Parameters, sizeof(Parameters));
    if (DeviceExtension->MessageInfo != NULL)
    {
        Parameters.Version = CONNECT_MESSAGE_BASED;
        Parameters.ConnectionContext.InterruptMessageTable = DeviceExtension->MessageInfo;
        IoDisconnectInterruptEx(&Parameters);
        DeviceExtension->MessageInfo = NULL;
    }
    else if (DeviceExtension->Interrupt != NULL)
    {
        if (DeviceExtension->InterruptConnectedEx)
        {
            Parameters.Version = CONNECT_LINE_BASED;
            Parameters.ConnectionContext.InterruptObject = DeviceExtension->Interrupt;
            IoDisconnectInterruptEx(&Parameters);
        }
        else
        {
            IoDisconnectInterrupt(DeviceExtension->Interrupt);
        }
    }

    DeviceExtension->Interrupt = NULL;
    DeviceExtension->InterruptConnectedEx = FALSE;
    DeviceExtension->InterruptIrql = 0;
}


static
VOID
PortFdoStopAdapter(
    _In_ PFDO_DEVICE_EXTENSION DeviceExtension)
{
    if ((DeviceExtension->PnpState != dsStarted) &&
        (DeviceExtension->PnpState != dsSurpriseRemoved))
        return;

    ASSERT(DeviceExtension->TimeoutsStopped);
    ASSERT(KeReadStateEvent(&DeviceExtension->ResetIdle) != 0);
    MiniportAdapterControl(&DeviceExtension->Miniport, ScsiStopAdapter, NULL);
    MiniportAdapterControl(&DeviceExtension->Miniport, ScsiSetBootConfig, NULL);
    KeFlushQueuedDpcs();
    KeCancelTimer(&DeviceExtension->MiniportTimer);
    KeFlushQueuedDpcs();
    PortFdoDisconnectInterrupt(DeviceExtension);
    DeviceExtension->PnpState = dsStopped;
}


static
VOID
PortFdoReleaseHardware(
    _In_ PFDO_DEVICE_EXTENSION DeviceExtension)
{
    PMAPPED_ADDRESS Mapping;

    KeCancelTimer(&DeviceExtension->MiniportTimer);

    while ((Mapping = DeviceExtension->MappedAddressList) != NULL)
    {
        DeviceExtension->MappedAddressList = Mapping->NextMappedAddress;
        if (Mapping->MappedAddress != NULL)
            MmUnmapIoSpace(Mapping->MappedAddress, Mapping->NumberOfBytes);
        ExFreePoolWithTag(Mapping, TAG_ADDRESS_MAPPING);
    }

    if (DeviceExtension->BusInitialized && DeviceExtension->BusInterface.InterfaceDereference != NULL)
        DeviceExtension->BusInterface.InterfaceDereference(DeviceExtension->BusInterface.Context);
    DeviceExtension->BusInitialized = FALSE;

    if (DeviceExtension->AllocatedResources != NULL)
        ExFreePoolWithTag(DeviceExtension->AllocatedResources, TAG_RESOURCE_LIST);
    if (DeviceExtension->TranslatedResources != NULL)
        ExFreePoolWithTag(DeviceExtension->TranslatedResources, TAG_RESOURCE_LIST);
    DeviceExtension->AllocatedResources = NULL;
    DeviceExtension->TranslatedResources = NULL;
}


static
VOID
PortFdoReleaseAdapter(
    _In_ PFDO_DEVICE_EXTENSION DeviceExtension)
{
    KLOCK_QUEUE_HANDLE LockHandle;
    PLIST_ENTRY Entry;

    for (;;)
    {
        KeAcquireInStackQueuedSpinLock(&DeviceExtension->PdoListLock, &LockHandle);
        Entry = IsListEmpty(&DeviceExtension->PdoListHead) ? NULL : DeviceExtension->PdoListHead.Flink;
        KeReleaseInStackQueuedSpinLock(&LockHandle);
        if (Entry == NULL)
            break;
        PortDeletePdo(CONTAINING_RECORD(Entry, PDO_DEVICE_EXTENSION, PdoListEntry));
    }

    KeCancelTimer(&DeviceExtension->MiniportTimer);
    PortFreeMiniportTimers(DeviceExtension);
    PortFdoReleaseHardware(DeviceExtension);

    if (DeviceExtension->Miniport.PortConfig.VirtualDevice &&
        DeviceExtension->Miniport.InitData != NULL &&
        DeviceExtension->Miniport.InitData->HwFreeAdapterResources != NULL &&
        DeviceExtension->Miniport.MiniportExtension != NULL)
    {
        DeviceExtension->Miniport.InitData->HwFreeAdapterResources(
            &DeviceExtension->Miniport.MiniportExtension->HwDeviceExtension);
    }

    ASSERT(!DeviceExtension->Miniport.PortConfig.VirtualDevice || DeviceExtension->DmaAdapter == NULL);
    if (DeviceExtension->Miniport.PortConfig.VirtualDevice && DeviceExtension->SrbExtensionPool != NULL)
        ExFreePoolWithTag(DeviceExtension->SrbExtensionPool, TAG_SRB_EXTENSION);
    PortReleaseDma(DeviceExtension);
    DeviceExtension->UncachedExtensionVirtualBase = NULL;
    DeviceExtension->SrbExtensionPool = NULL;
    InitializeSListHead(&DeviceExtension->FreeSrbExtensions);

    if (DeviceExtension->RequestPoolsReady)
    {
        ExDeleteNPagedLookasideList(&DeviceExtension->SrbContextLookaside);
        ExDeleteNPagedLookasideList(&DeviceExtension->MiniportSrbLookaside);
        ExDeleteNPagedLookasideList(&DeviceExtension->SglLookaside);
        DeviceExtension->RequestPoolsReady = FALSE;
    }

    if (DeviceExtension->Miniport.PortConfig.AccessRanges != NULL)
        ExFreePoolWithTag(DeviceExtension->Miniport.PortConfig.AccessRanges, TAG_ACCRESS_RANGE);
    DeviceExtension->Miniport.PortConfig.AccessRanges = NULL;

    if (DeviceExtension->Miniport.MiniportExtension != NULL)
        ExFreePoolWithTag(DeviceExtension->Miniport.MiniportExtension, TAG_MINIPORT_DATA);
    DeviceExtension->Miniport.MiniportExtension = NULL;

    if (DeviceExtension->DriverExtension != NULL)
    {
        KeAcquireInStackQueuedSpinLock(&DeviceExtension->DriverExtension->AdapterListLock, &LockHandle);
        RemoveEntryList(&DeviceExtension->AdapterListEntry);
        DeviceExtension->DriverExtension->AdapterCount--;
        KeReleaseInStackQueuedSpinLock(&LockHandle);
    }
}


static
PPDO_DEVICE_EXTENSION
PortFdoRequestUnit(
    _In_ PIRP Irp)
{
    PDEVICE_OBJECT Device = IoGetCurrentIrpStackLocation(Irp)->DeviceObject;
    PPDO_DEVICE_EXTENSION Unit;

    if (Device == NULL)
        return NULL;
    Unit = Device->DeviceExtension;
    return (Unit->ExtensionType == PdoExtension) ? Unit : NULL;
}


static
BOOLEAN
PortFdoRequestUntagged(
    _In_ PIRP Irp)
{
    PVOID Srb = IoGetCurrentIrpStackLocation(Irp)->Parameters.Scsi.Srb;
    PSTORAGE_REQUEST_BLOCK ExtendedSrb = Srb;
    PSCSI_REQUEST_BLOCK LegacySrb = Srb;

    if (Srb == NULL)
        return FALSE;
    if (PortIsExtendedSrb(Srb))
    {
        return (ExtendedSrb->SrbFunction == SRB_FUNCTION_EXECUTE_SCSI) &&
               !(ExtendedSrb->SrbFlags & SRB_FLAGS_QUEUE_ACTION_ENABLE);
    }
    return (LegacySrb->Function == SRB_FUNCTION_EXECUTE_SCSI) && !(LegacySrb->SrbFlags & SRB_FLAGS_QUEUE_ACTION_ENABLE);
}


static
VOID
PortFdoCountRequestLocked(
    _In_ PFDO_DEVICE_EXTENSION DeviceExtension,
    _In_ PIRP Irp,
    _In_ LONG Delta)
{
    PPDO_DEVICE_EXTENSION PdoExtension = PortFdoRequestUnit(Irp);

    ASSERT(Delta == 1 || Delta == -1);
    InterlockedExchangeAdd(&DeviceExtension->OutstandingRequests, Delta);
    ASSERT(DeviceExtension->OutstandingRequests >= 0);
    if (PdoExtension == NULL)
        return;
    PdoExtension->OutstandingRequests += Delta;
    ASSERT(PdoExtension->OutstandingRequests >= 0);
    ASSERT(Irp->Tail.Overlay.DriverContext[3] != PORT_DUMP_IRP_MARKER);
    if (Delta > 0)
        Irp->Tail.Overlay.DriverContext[2] = PortFdoRequestUntagged(Irp) ? PORT_UNTAGGED_MARKER : NULL;
    if (PortRequestCountedUntagged(Irp))
    {
        PdoExtension->UntaggedRequests += Delta;
        ASSERT(PdoExtension->UntaggedRequests >= 0 &&
               PdoExtension->UntaggedRequests <= PdoExtension->OutstandingRequests);
    }
    if (Delta < 0)
        Irp->Tail.Overlay.DriverContext[2] = NULL;
    if (Delta < 0 && PdoExtension->BusyCompletions > 0)
        PdoExtension->BusyCompletions--;
}


static
BOOLEAN
PortFdoRequestBlockedLocked(
    _In_ PFDO_DEVICE_EXTENSION DeviceExtension,
    _In_ PIRP Irp)
{
    PSCSI_REQUEST_BLOCK Srb = IoGetCurrentIrpStackLocation(Irp)->Parameters.Scsi.Srb;
    PPDO_DEVICE_EXTENSION PdoExtension = PortFdoRequestUnit(Irp);

    if (DeviceExtension->HoldRequests || DeviceExtension->BusyCompletions > 0)
        return TRUE;
    if ((PdoExtension != NULL) &&
        ((PdoExtension->BusyCompletions > 0) ||
         ((PdoExtension->BusyUntil != 0) && (KeQueryInterruptTime() < PdoExtension->BusyUntil)) ||
         ((PdoExtension->UntaggedRequests > 0) && PortFdoRequestUntagged(Irp))))
    {
        return TRUE;
    }
    if (DeviceExtension->ResetActive &&
        (DeviceExtension->BusResetActive || (Srb == NULL) ||
         ((Srb->PathId == DeviceExtension->ResetPathId) && (Srb->TargetId == DeviceExtension->ResetTargetId) &&
          (Srb->Lun == DeviceExtension->ResetLun))))
    {
        return TRUE;
    }
    return (DeviceExtension->RequestLimit != 0) &&
           (DeviceExtension->OutstandingRequests >= (LONG)DeviceExtension->RequestLimit);
}


BOOLEAN
PortFdoStartRequest(
    _In_ PFDO_DEVICE_EXTENSION DeviceExtension,
    _In_ PIRP Irp,
    _In_ BOOLEAN PortRequest)
{
    KIRQL OldIrql;
    BOOLEAN Started = TRUE;

    KeAcquireSpinLock(&DeviceExtension->RequestHoldLock, &OldIrql);
    if (!PortRequest && PortFdoRequestBlockedLocked(DeviceExtension, Irp))
    {
        IoMarkIrpPending(Irp);
        InsertTailList(&DeviceExtension->HeldRequests, &Irp->Tail.Overlay.ListEntry);
        Started = FALSE;
    }
    else
    {
        PortFdoCountRequestLocked(DeviceExtension, Irp, 1);
    }
    KeReleaseSpinLock(&DeviceExtension->RequestHoldLock, OldIrql);
    return Started;
}


VOID
PortFdoEndRequest(
    _In_ PFDO_DEVICE_EXTENSION DeviceExtension,
    _In_ PIRP Irp)
{
    KIRQL OldIrql;
    BOOLEAN Drained, Restart;

    KeAcquireSpinLock(&DeviceExtension->RequestHoldLock, &OldIrql);
    PortFdoCountRequestLocked(DeviceExtension, Irp, -1);
    Drained = (DeviceExtension->OutstandingRequests == 0);
    if (DeviceExtension->BusyCompletions > 0)
        DeviceExtension->BusyCompletions--;
    Restart = !DeviceExtension->HoldRequests && !IsListEmpty(&DeviceExtension->HeldRequests);
    KeReleaseSpinLock(&DeviceExtension->RequestHoldLock, OldIrql);

    if (Drained)
        KeSetEvent(&DeviceExtension->RequestsDrained, IO_NO_INCREMENT, FALSE);

    if (Restart)
        KeInsertQueueDpc(&DeviceExtension->RequestRestartDpc, NULL, NULL);
}


VOID
NTAPI
PortFdoBusChangeDpc(
    _In_ PKDPC Dpc,
    _In_opt_ PVOID DeferredContext,
    _In_opt_ PVOID SystemArgument1,
    _In_opt_ PVOID SystemArgument2)
{
    PFDO_DEVICE_EXTENSION DeviceExtension = DeferredContext;

    UNREFERENCED_PARAMETER(Dpc);
    UNREFERENCED_PARAMETER(SystemArgument1);
    UNREFERENCED_PARAMETER(SystemArgument2);

    IoInvalidateDeviceRelations(DeviceExtension->PhysicalDevice, BusRelations);
}


VOID
NTAPI
PortFdoRestartRequestsDpc(
    _In_ PKDPC Dpc,
    _In_opt_ PVOID DeferredContext,
    _In_opt_ PVOID SystemArgument1,
    _In_opt_ PVOID SystemArgument2)
{
    PFDO_DEVICE_EXTENSION DeviceExtension = DeferredContext;
    PIO_STACK_LOCATION Stack;
    PLIST_ENTRY Entry;
    PIRP Irp;
    ULONGLONG Now;
    ULONGLONG RetryAt;
    LARGE_INTEGER DueTime;

    UNREFERENCED_PARAMETER(Dpc);
    UNREFERENCED_PARAMETER(SystemArgument1);
    UNREFERENCED_PARAMETER(SystemArgument2);

    for (;;)
    {
        Irp = NULL;
        RetryAt = 0;
        KeAcquireSpinLockAtDpcLevel(&DeviceExtension->RequestHoldLock);
        Now = KeQueryInterruptTime();
        for (Entry = DeviceExtension->HeldRequests.Flink;
             Entry != &DeviceExtension->HeldRequests;
             Entry = Entry->Flink)
        {
            PIRP Held = CONTAINING_RECORD(Entry, IRP, Tail.Overlay.ListEntry);

            if (PortFdoRequestBlockedLocked(DeviceExtension, Held))
            {
                PPDO_DEVICE_EXTENSION Unit = PortFdoRequestUnit(Held);

                if ((Unit != NULL) && (Unit->BusyUntil > Now) && ((RetryAt == 0) || (Unit->BusyUntil < RetryAt)))
                    RetryAt = Unit->BusyUntil;
                continue;
            }
            RemoveEntryList(Entry);
            Irp = Held;
            PortFdoCountRequestLocked(DeviceExtension, Irp, 1);
            break;
        }
        KeReleaseSpinLockFromDpcLevel(&DeviceExtension->RequestHoldLock);
        if (Irp == NULL)
        {
            /* A unit still busy is looked at again once its busy time is over: nothing else would restart it */
            if (RetryAt != 0)
            {
                DueTime.QuadPart = -(LONGLONG)(RetryAt - Now);
                KeSetTimer(&DeviceExtension->BusyRetryTimer, DueTime, &DeviceExtension->BusyRetryDpc);
            }
            break;
        }

        Stack = IoGetCurrentIrpStackLocation(Irp);
        PortSubmitSrb(DeviceExtension, Stack->DeviceObject, Irp, Stack->Parameters.Scsi.Srb);
    }
}


typedef struct _PORT_RESET_REQUEST
{
    SCSI_REQUEST_BLOCK Srb;
    KEVENT Completed;
    PDEVICE_OBJECT Device;
    volatile LONG References;
} PORT_RESET_REQUEST, *PPORT_RESET_REQUEST;


static
VOID
PortFdoArmTimeoutsLocked(
    _In_ PFDO_DEVICE_EXTENSION DeviceExtension)
{
    LARGE_INTEGER DueTime;

    ASSERT(KeGetCurrentIrql() == DISPATCH_LEVEL);
    ASSERT(!DeviceExtension->TimeoutArmed && !DeviceExtension->TimeoutsStopped);

    DeviceExtension->TimeoutArmed = TRUE;
    DueTime.QuadPart = -10000000LL;
    KeSetCoalescableTimer(&DeviceExtension->TimeoutTimer, DueTime, 1000, 100, &DeviceExtension->TimeoutDpc);
}


VOID
PortFdoTrackRequest(
    _In_ PFDO_DEVICE_EXTENSION DeviceExtension,
    _Inout_ PSTOR_SRB_CONTEXT SrbContext)
{
    PSCSI_REQUEST_BLOCK Srb = SrbContext->LegacySrb;

    ASSERT(Srb != NULL);
    ASSERT(KeGetCurrentIrql() == DISPATCH_LEVEL);
    ASSERT(IsListEmpty(&SrbContext->ActiveEntry));
    ASSERT(SrbContext->State == PORT_SRB_PREPARED);
    ASSERT(!SrbContext->WasRecovering);

    SrbContext->Deadline = (Srb->TimeOutValue != 0) ?
                           KeQueryInterruptTime() + (ULONGLONG)Srb->TimeOutValue * 10000000ULL :
                           MAXULONGLONG;

    KeAcquireSpinLockAtDpcLevel(&DeviceExtension->ActiveLock);
    InsertTailList(&DeviceExtension->ActiveRequests, &SrbContext->ActiveEntry);
    InterlockedExchange(&SrbContext->State, PORT_SRB_BUILDING);
    if (!DeviceExtension->TimeoutArmed && !DeviceExtension->TimeoutsStopped && (Srb->TimeOutValue != 0))
        PortFdoArmTimeoutsLocked(DeviceExtension);
    KeReleaseSpinLockFromDpcLevel(&DeviceExtension->ActiveLock);
}


VOID
PortFdoUntrackRequest(
    _In_ PFDO_DEVICE_EXTENSION DeviceExtension,
    _Inout_ PSTOR_SRB_CONTEXT SrbContext)
{
    KIRQL OldIrql = KeGetCurrentIrql();

    ASSERT(OldIrql <= DISPATCH_LEVEL);
    ASSERT((SrbContext->State == PORT_SRB_PREPARED) || (SrbContext->State == PORT_SRB_COMPLETED));

    if (IsListEmpty(&SrbContext->ActiveEntry))
    {
        ASSERT(SrbContext->State == PORT_SRB_PREPARED);
        return;
    }
    if (OldIrql == DISPATCH_LEVEL)
        KeAcquireSpinLockAtDpcLevel(&DeviceExtension->ActiveLock);
    else
        KeAcquireSpinLock(&DeviceExtension->ActiveLock, &OldIrql);
    ASSERT(SrbContext->State == PORT_SRB_COMPLETED);
    RemoveEntryList(&SrbContext->ActiveEntry);
    InitializeListHead(&SrbContext->ActiveEntry);
    if (OldIrql == DISPATCH_LEVEL)
        KeReleaseSpinLockFromDpcLevel(&DeviceExtension->ActiveLock);
    else
        KeReleaseSpinLock(&DeviceExtension->ActiveLock, OldIrql);
}


VOID
PortFdoStartRequestIo(
    _In_ PFDO_DEVICE_EXTENSION DeviceExtension,
    _In_ PVOID Srb)
{
    PSTOR_SRB_CONTEXT SrbContext;
    LONG State;
    PIRP Irp;

    ASSERT(KeGetCurrentIrql() == DISPATCH_LEVEL);
    ASSERT(!DeviceExtension->DumpMode);

    Irp = PortGetOriginalRequestFromSrb(Srb);
    ASSERT(Irp != NULL);
    SrbContext = PortGetSrbContext(Irp);
    ASSERT((SrbContext != NULL) && (SrbContext->FdoExtension == DeviceExtension));
    SrbContext->StartSequence = InterlockedIncrement64(&DeviceExtension->StartSequence);
    State = InterlockedCompareExchange(&SrbContext->State, PORT_SRB_ACTIVE, PORT_SRB_BUILDING);
    ASSERT(State == PORT_SRB_BUILDING);
    UNREFERENCED_PARAMETER(State);
}


BOOLEAN
PortFdoClaimRequest(
    _Inout_ PSTOR_SRB_CONTEXT SrbContext,
    _In_ BOOLEAN Started)
{
    LONG State;

    for (;;)
    {
        State = SrbContext->State;
        if ((State != PORT_SRB_ACTIVE) && (State != PORT_SRB_RECOVERING) &&
            (Started || (State != PORT_SRB_BUILDING)))
        {
            return FALSE;
        }
        if (InterlockedCompareExchange(&SrbContext->State, PORT_SRB_COMPLETED, State) == State)
            break;
    }

    ASSERT(!SrbContext->WasRecovering);
    SrbContext->WasRecovering = (State == PORT_SRB_RECOVERING);
    return TRUE;
}


static
VOID
PortFdoRequestRecoveredLocked(
    _In_ PFDO_DEVICE_EXTENSION DeviceExtension)
{
    LONG Left;

    ASSERT(KeGetCurrentIrql() == DISPATCH_LEVEL);

    Left = InterlockedDecrement(&DeviceExtension->RecoveringRequests);
    ASSERT(Left >= 0);
    if (Left == 0)
        KeSetEvent(&DeviceExtension->Recovered, IO_NO_INCREMENT, FALSE);
}


VOID
PortFdoRequestRecovered(
    _In_ PFDO_DEVICE_EXTENSION DeviceExtension)
{
    KIRQL OldIrql;

    ASSERT(KeGetCurrentIrql() <= DISPATCH_LEVEL);

    KeAcquireSpinLock(&DeviceExtension->ActiveLock, &OldIrql);
    PortFdoRequestRecoveredLocked(DeviceExtension);
    KeReleaseSpinLock(&DeviceExtension->ActiveLock, OldIrql);
}


VOID
PortFdoCompleteRequests(
    _In_ PFDO_DEVICE_EXTENSION DeviceExtension,
    _In_ UCHAR PathId,
    _In_ UCHAR TargetId,
    _In_ UCHAR Lun,
    _In_ UCHAR SrbStatus,
    _In_ LONG64 LastStarted)
{
    PSTOR_SRB_CONTEXT SrbContext;
    PSCSI_REQUEST_BLOCK Srb;
    PLIST_ENTRY Entry;
    ULONG Count = 0;
    KIRQL OldIrql;
    PIRP Irp;

    ASSERT(KeGetCurrentIrql() <= DISPATCH_LEVEL);
    ASSERT(!DeviceExtension->DumpMode);

    KeAcquireSpinLock(&DeviceExtension->ActiveLock, &OldIrql);
    for (Entry = DeviceExtension->ActiveRequests.Flink;
         Entry != &DeviceExtension->ActiveRequests;
         Entry = Entry->Flink)
    {
        SrbContext = CONTAINING_RECORD(Entry, STOR_SRB_CONTEXT, ActiveEntry);
        Srb = SrbContext->LegacySrb;
        if (((PathId != SP_UNTAGGED) && (Srb->PathId != PathId)) ||
            ((TargetId != SP_UNTAGGED) && (Srb->TargetId != TargetId)) ||
            ((Lun != SP_UNTAGGED) && (Srb->Lun != Lun)) ||
            (ReadAcquire(&SrbContext->State) == PORT_SRB_BUILDING) || (SrbContext->StartSequence > LastStarted) ||
            !PortFdoClaimRequest(SrbContext, TRUE))
        {
            continue;
        }

        Irp = Srb->OriginalRequest;
        ASSERT((Irp != NULL) && (PortGetSrbContext(Irp) == SrbContext));
        if (SrbContext->MiniportSrb != NULL)
        {
            ((PSTORAGE_REQUEST_BLOCK)SrbContext->MiniportSrb)->SrbStatus = SrbStatus;
            Irp->Tail.Overlay.DriverContext[1] = SrbContext->MiniportSrb;
        }
        else
        {
            Srb->SrbStatus = SrbStatus;
            Irp->Tail.Overlay.DriverContext[1] = Srb;
        }
        ExInterlockedInsertTailList(&DeviceExtension->CompletionListHead,
                                    &Irp->Tail.Overlay.ListEntry,
                                    &DeviceExtension->CompletionLock);
        Count++;
    }
    KeReleaseSpinLock(&DeviceExtension->ActiveLock, OldIrql);

    DPRINT1("Storport: completing %lu requests to %u:%u:%u with status 0x%02x\n",
            Count, PathId, TargetId, Lun, SrbStatus);
    if (Count != 0)
        KeInsertQueueDpc(&DeviceExtension->CompletionDpc, NULL, NULL);
}


VOID
NTAPI
PortFdoCompleteRequestsDpc(
    _In_ PKDPC Dpc,
    _In_opt_ PVOID DeferredContext,
    _In_opt_ PVOID SystemArgument1,
    _In_opt_ PVOID SystemArgument2)
{
    PFDO_DEVICE_EXTENSION DeviceExtension = DeferredContext;
    PPORT_COMPLETE_REQUESTS Request;
    PSLIST_ENTRY Entry, Ordered = NULL, Next;

    UNREFERENCED_PARAMETER(Dpc);
    UNREFERENCED_PARAMETER(SystemArgument1);
    UNREFERENCED_PARAMETER(SystemArgument2);
    ASSERT(KeGetCurrentIrql() == DISPATCH_LEVEL);

    for (Entry = InterlockedFlushSList(&DeviceExtension->CompleteRequestsPending); Entry != NULL; Entry = Next)
    {
        Next = Entry->Next;
        Entry->Next = Ordered;
        Ordered = Entry;
    }
    for (Entry = Ordered; Entry != NULL; Entry = Next)
    {
        Next = Entry->Next;
        Request = CONTAINING_RECORD(Entry, PORT_COMPLETE_REQUESTS, Entry);
        PortFdoCompleteRequests(DeviceExtension, Request->PathId, Request->TargetId, Request->Lun, Request->SrbStatus,
                                Request->LastStarted);
        InterlockedPushEntrySList(&DeviceExtension->CompleteRequestsFree, &Request->Entry);
    }
}


VOID
PortFdoRetryBusyRequest(
    _In_ PFDO_DEVICE_EXTENSION DeviceExtension,
    _In_ PIRP Irp)
{
    static volatile LONG Retries;
    PPDO_DEVICE_EXTENSION PdoExtension;
    LARGE_INTEGER DueTime;
    LONG Count;
    KIRQL OldIrql;

    ASSERT(KeGetCurrentIrql() <= DISPATCH_LEVEL);
    ASSERT(PortGetSrbContext(Irp) == NULL);

    Count = InterlockedIncrement(&Retries);
    if (Count <= 3 || (Count & 0xFF) == 0)
        DPRINT1("Storport: the miniport is busy, requeueing request %p (%ld so far)\n", Irp, Count);

    PdoExtension = PortFdoRequestUnit(Irp);
    ASSERT(PdoExtension != NULL);
    KeAcquireSpinLock(&DeviceExtension->RequestHoldLock, &OldIrql);
    InsertHeadList(&DeviceExtension->HeldRequests, &Irp->Tail.Overlay.ListEntry);
    if (PdoExtension != NULL)
        PdoExtension->BusyUntil = KeQueryInterruptTime() + 30000ULL;
    KeReleaseSpinLock(&DeviceExtension->RequestHoldLock, OldIrql);
    DueTime.QuadPart = -30000LL;
    KeSetTimer(&DeviceExtension->BusyRetryTimer, DueTime, &DeviceExtension->BusyRetryDpc);
}


VOID
NTAPI
PortFdoBusyRetryDpc(
    _In_ PKDPC Dpc,
    _In_opt_ PVOID DeferredContext,
    _In_opt_ PVOID SystemArgument1,
    _In_opt_ PVOID SystemArgument2)
{
    PFDO_DEVICE_EXTENSION DeviceExtension = DeferredContext;

    UNREFERENCED_PARAMETER(Dpc);
    UNREFERENCED_PARAMETER(SystemArgument1);
    UNREFERENCED_PARAMETER(SystemArgument2);

    KeInsertQueueDpc(&DeviceExtension->RequestRestartDpc, NULL, NULL);
}


BOOLEAN
PortFdoSetBusy(
    _In_ PFDO_DEVICE_EXTENSION DeviceExtension,
    _In_ BOOLEAN Unit,
    _In_ UCHAR PathId,
    _In_ UCHAR TargetId,
    _In_ UCHAR Lun,
    _In_ ULONG RequestsToComplete)
{
    PPDO_DEVICE_EXTENSION PdoExtension = NULL;
    KLOCK_QUEUE_HANDLE LockHandle;
    PLIST_ENTRY Entry;
    BOOLEAN Restart;
    KIRQL OldIrql;

    ASSERT(KeGetCurrentIrql() <= DISPATCH_LEVEL);

    if (Unit)
    {
        KeAcquireInStackQueuedSpinLock(&DeviceExtension->PdoListLock, &LockHandle);
        for (Entry = DeviceExtension->PdoListHead.Flink; Entry != &DeviceExtension->PdoListHead; Entry = Entry->Flink)
        {
            PPDO_DEVICE_EXTENSION Candidate = CONTAINING_RECORD(Entry, PDO_DEVICE_EXTENSION, PdoListEntry);

            if ((Candidate->Bus == PathId) && (Candidate->Target == TargetId) && (Candidate->Lun == Lun))
            {
                PdoExtension = Candidate;
                break;
            }
        }
        KeReleaseInStackQueuedSpinLock(&LockHandle);
        if (PdoExtension == NULL)
            return FALSE;
    }

    KeAcquireSpinLock(&DeviceExtension->RequestHoldLock, &OldIrql);
    if (Unit)
        PdoExtension->BusyCompletions = (LONG)min((ULONG)PdoExtension->OutstandingRequests, RequestsToComplete);
    else
        DeviceExtension->BusyCompletions = (LONG)min((ULONG)DeviceExtension->OutstandingRequests, RequestsToComplete);
    Restart = !DeviceExtension->HoldRequests && !IsListEmpty(&DeviceExtension->HeldRequests);
    KeReleaseSpinLock(&DeviceExtension->RequestHoldLock, OldIrql);

    if (Restart)
        KeInsertQueueDpc(&DeviceExtension->RequestRestartDpc, NULL, NULL);
    return TRUE;
}


static
BOOLEAN
PortFdoTimeoutsStopped(
    _In_ PFDO_DEVICE_EXTENSION DeviceExtension)
{
    BOOLEAN Stopped;
    KIRQL OldIrql;

    KeAcquireSpinLock(&DeviceExtension->ActiveLock, &OldIrql);
    Stopped = DeviceExtension->TimeoutsStopped;
    KeReleaseSpinLock(&DeviceExtension->ActiveLock, OldIrql);
    return Stopped;
}


static
BOOLEAN
PortFdoWaitReset(
    _In_ PFDO_DEVICE_EXTENSION DeviceExtension,
    _In_ PKEVENT Event)
{
    PVOID Objects[2];
    LARGE_INTEGER Timeout;

    ASSERT(KeGetCurrentIrql() == PASSIVE_LEVEL);
    ASSERT(DeviceExtension->ResetTimeOutValue != 0);

    Objects[0] = Event;
    Objects[1] = &DeviceExtension->TimeoutsStopEvent;
    Timeout.QuadPart = -(LONGLONG)DeviceExtension->ResetTimeOutValue * 10000000LL;
    return KeWaitForMultipleObjects(2, Objects, WaitAny, Executive, KernelMode, FALSE, &Timeout, NULL) ==
           STATUS_WAIT_0;
}


static
PDEVICE_OBJECT
PortFdoFindUnit(
    _In_ PFDO_DEVICE_EXTENSION DeviceExtension)
{
    PPDO_DEVICE_EXTENSION PdoExtension;
    KLOCK_QUEUE_HANDLE LockHandle;
    PDEVICE_OBJECT Device = DeviceExtension->Device;
    PLIST_ENTRY Entry;

    KeAcquireInStackQueuedSpinLock(&DeviceExtension->PdoListLock, &LockHandle);
    for (Entry = DeviceExtension->PdoListHead.Flink;
         Entry != &DeviceExtension->PdoListHead;
         Entry = Entry->Flink)
    {
        PdoExtension = CONTAINING_RECORD(Entry, PDO_DEVICE_EXTENSION, PdoListEntry);
        if ((PdoExtension->Bus == DeviceExtension->ResetPathId) &&
            (PdoExtension->Target == DeviceExtension->ResetTargetId) &&
            (PdoExtension->Lun == DeviceExtension->ResetLun))
        {
            Device = PdoExtension->Device;
            break;
        }
    }
    ObReferenceObject(Device);
    KeReleaseInStackQueuedSpinLock(&LockHandle);
    return Device;
}


static
VOID
PortFdoReleaseResetRequest(
    _In_ PPORT_RESET_REQUEST Request)
{
    LONG References = InterlockedDecrement(&Request->References);

    ASSERT(References >= 0);
    if (References == 0)
    {
        ObDereferenceObject(Request->Device);
        ExFreePoolWithTag(Request, TAG_RESET_REQUEST);
    }
}


static IO_COMPLETION_ROUTINE PortFdoResetCompletion;

static
NTSTATUS
NTAPI
PortFdoResetCompletion(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp,
    _In_opt_ PVOID Context)
{
    PPORT_RESET_REQUEST Request = Context;

    UNREFERENCED_PARAMETER(DeviceObject);
    ASSERT(Request != NULL);
    ASSERT(Request->Srb.OriginalRequest == Irp);

    IoFreeIrp(Irp);
    KeSetEvent(&Request->Completed, IO_NO_INCREMENT, FALSE);
    PortFdoReleaseResetRequest(Request);
    return STATUS_MORE_PROCESSING_REQUIRED;
}


static
BOOLEAN
PortFdoResetBus(
    _In_ PFDO_DEVICE_EXTENSION DeviceExtension)
{
    STOR_LOCK_HANDLE LockHandle;
    BOOLEAN Result;
    KIRQL OldIrql;

    ASSERT(KeGetCurrentIrql() == PASSIVE_LEVEL);
    ASSERT(DeviceExtension->ResetActive);
    ASSERT(DeviceExtension->Miniport.InitData->HwResetBus != NULL);

    if (PortFdoTimeoutsStopped(DeviceExtension))
        return FALSE;

    KeAcquireSpinLock(&DeviceExtension->RequestHoldLock, &OldIrql);
    DeviceExtension->BusResetActive = TRUE;
    KeReleaseSpinLock(&DeviceExtension->RequestHoldLock, OldIrql);
    KeRaiseIrql(DISPATCH_LEVEL, &OldIrql);
    ExAcquireSpinLockExclusiveAtDpcLevel(&DeviceExtension->ChannelLock);
    PortAcquireSpinLock(DeviceExtension, StartIoLock, NULL, &LockHandle);
    Result = DeviceExtension->Miniport.InitData->HwResetBus(
        &DeviceExtension->Miniport.MiniportExtension->HwDeviceExtension, DeviceExtension->ResetPathId);
    PortReleaseSpinLock(DeviceExtension, &LockHandle);
    ExReleaseSpinLockExclusiveFromDpcLevel(&DeviceExtension->ChannelLock);
    KeLowerIrql(OldIrql);
    if (!Result)
        DPRINT1("Storport: the miniport failed to reset bus %u\n", DeviceExtension->ResetPathId);
    return TRUE;
}


static
BOOLEAN
PortFdoResetLogicalUnit(
    _In_ PFDO_DEVICE_EXTENSION DeviceExtension,
    _Out_ PBOOLEAN BusReset)
{
    PPORT_RESET_REQUEST Request;
    PIO_STACK_LOCATION Stack;
    PDEVICE_OBJECT Device;
    BOOLEAN Completed;
    BOOLEAN Success;
    PIRP Irp;

    ASSERT(KeGetCurrentIrql() == PASSIVE_LEVEL);
    ASSERT(DeviceExtension->ResetActive);

    *BusReset = FALSE;
    Device = PortFdoFindUnit(DeviceExtension);
    Request = ExAllocatePoolWithTag(NonPagedPool, sizeof(*Request), TAG_RESET_REQUEST);
    Irp = IoAllocateIrp(Device->StackSize, FALSE);
    if ((Request == NULL) || (Irp == NULL))
    {
        if (Irp != NULL)
            IoFreeIrp(Irp);
        if (Request != NULL)
            ExFreePoolWithTag(Request, TAG_RESET_REQUEST);
        ObDereferenceObject(Device);
        return FALSE;
    }

    RtlZeroMemory(Request, sizeof(*Request));
    KeInitializeEvent(&Request->Completed, NotificationEvent, FALSE);
    Request->Device = Device;
    Request->References = 2;
    Request->Srb.Length = sizeof(SCSI_REQUEST_BLOCK);
    Request->Srb.Function = SRB_FUNCTION_RESET_LOGICAL_UNIT;
    Request->Srb.PathId = DeviceExtension->ResetPathId;
    Request->Srb.TargetId = DeviceExtension->ResetTargetId;
    Request->Srb.Lun = DeviceExtension->ResetLun;
    Request->Srb.SrbFlags = SRB_FLAGS_NO_QUEUE_FREEZE;
    Request->Srb.SrbStatus = SRB_STATUS_PENDING;
    Request->Srb.OriginalRequest = Irp;

    IoSetCompletionRoutine(Irp, PortFdoResetCompletion, Request, TRUE, TRUE, TRUE);
    IoSetNextIrpStackLocation(Irp);
    Stack = IoGetCurrentIrpStackLocation(Irp);
    Stack->MajorFunction = IRP_MJ_SCSI;
    Stack->DeviceObject = Device;
    Stack->Parameters.Scsi.Srb = &Request->Srb;

    PortFdoStartRequest(DeviceExtension, Irp, TRUE);
    PortSubmitSrb(DeviceExtension, Device, Irp, &Request->Srb);

    Completed = PortFdoWaitReset(DeviceExtension, &Request->Completed);
    if (!Completed && !PortFdoTimeoutsStopped(DeviceExtension))
    {
        DPRINT1("Storport: the logical unit reset of %u:%u:%u did not complete in %lu seconds\n",
                DeviceExtension->ResetPathId, DeviceExtension->ResetTargetId, DeviceExtension->ResetLun,
                DeviceExtension->ResetTimeOutValue);
        *BusReset = PortFdoResetBus(DeviceExtension);
        if (*BusReset)
            Completed = PortFdoWaitReset(DeviceExtension, &Request->Completed);
    }

    Success = Completed && (SRB_STATUS(Request->Srb.SrbStatus) == SRB_STATUS_SUCCESS);
    if (!Completed)
    {
        DPRINT1("Storport: abandoning the logical unit reset of %u:%u:%u to the miniport\n",
                DeviceExtension->ResetPathId, DeviceExtension->ResetTargetId, DeviceExtension->ResetLun);
    }
    PortFdoReleaseResetRequest(Request);
    return Success;
}


static
BOOLEAN
PortFdoWaitRecovered(
    _In_ PFDO_DEVICE_EXTENSION DeviceExtension)
{
    ASSERT(KeGetCurrentIrql() == PASSIVE_LEVEL);
    return PortFdoWaitReset(DeviceExtension, &DeviceExtension->Recovered);
}


static
VOID
PortFdoRearmRecovering(
    _In_ PFDO_DEVICE_EXTENSION DeviceExtension)
{
    PSTOR_SRB_CONTEXT SrbContext;
    PLIST_ENTRY Entry;
    ULONGLONG Now = KeQueryInterruptTime();
    ULONG Count = 0;
    KIRQL OldIrql;

    KeAcquireSpinLock(&DeviceExtension->ActiveLock, &OldIrql);
    for (Entry = DeviceExtension->ActiveRequests.Flink;
         Entry != &DeviceExtension->ActiveRequests;
         Entry = Entry->Flink)
    {
        SrbContext = CONTAINING_RECORD(Entry, STOR_SRB_CONTEXT, ActiveEntry);
        if (InterlockedCompareExchange(&SrbContext->State, PORT_SRB_ACTIVE, PORT_SRB_RECOVERING) !=
            PORT_SRB_RECOVERING)
        {
            continue;
        }
        ASSERT(SrbContext->LegacySrb->TimeOutValue != 0);
        SrbContext->Deadline = Now + (ULONGLONG)SrbContext->LegacySrb->TimeOutValue * 10000000ULL;
        PortFdoRequestRecoveredLocked(DeviceExtension);
        Count++;
    }
    KeReleaseSpinLock(&DeviceExtension->ActiveLock, OldIrql);

    if (Count != 0)
    {
        DPRINT1("Storport: %lu requests to %u:%u:%u survived the reset, timing them again\n",
                Count, DeviceExtension->ResetPathId, DeviceExtension->ResetTargetId, DeviceExtension->ResetLun);
    }
}


static IO_WORKITEM_ROUTINE PortFdoResetWorker;

static
VOID
NTAPI
PortFdoResetWorker(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_opt_ PVOID Context)
{
    PFDO_DEVICE_EXTENSION DeviceExtension = DeviceObject->DeviceExtension;
    BOOLEAN BusReset = FALSE;
    BOOLEAN Recovered = FALSE;
    KIRQL OldIrql;

    UNREFERENCED_PARAMETER(Context);
    ASSERT(KeGetCurrentIrql() == PASSIVE_LEVEL);
    ASSERT(DeviceExtension->ExtensionType == FdoExtension);
    ASSERT(DeviceExtension->ResetActive);
    ASSERT(KeReadStateEvent(&DeviceExtension->ResetIdle) == 0);

    if (!PortFdoTimeoutsStopped(DeviceExtension))
    {
        Recovered = PortFdoResetLogicalUnit(DeviceExtension, &BusReset) && PortFdoWaitRecovered(DeviceExtension);
        if (!Recovered && !BusReset)
            BusReset = PortFdoResetBus(DeviceExtension);
        if (!Recovered && BusReset)
            Recovered = PortFdoWaitRecovered(DeviceExtension);
    }
    if (!Recovered)
        PortFdoRearmRecovering(DeviceExtension);

    KeAcquireSpinLock(&DeviceExtension->RequestHoldLock, &OldIrql);
    DeviceExtension->ResetActive = FALSE;
    DeviceExtension->BusResetActive = FALSE;
    KeReleaseSpinLock(&DeviceExtension->RequestHoldLock, OldIrql);
    KeInsertQueueDpc(&DeviceExtension->RequestRestartDpc, NULL, NULL);
    KeAcquireSpinLock(&DeviceExtension->ActiveLock, &OldIrql);
    ASSERT(KeReadStateEvent(&DeviceExtension->ResetIdle) == 0);
    KeSetEvent(&DeviceExtension->ResetIdle, IO_NO_INCREMENT, FALSE);
    KeReleaseSpinLock(&DeviceExtension->ActiveLock, OldIrql);
}


static
BOOLEAN
PortFdoSameUnit(
    _In_ PFDO_DEVICE_EXTENSION DeviceExtension,
    _In_ PSCSI_REQUEST_BLOCK Srb)
{
    return (Srb->PathId == DeviceExtension->ResetPathId) &&
           (Srb->TargetId == DeviceExtension->ResetTargetId) &&
           (Srb->Lun == DeviceExtension->ResetLun);
}


VOID
NTAPI
PortFdoTimeoutDpc(
    _In_ PKDPC Dpc,
    _In_opt_ PVOID DeferredContext,
    _In_opt_ PVOID SystemArgument1,
    _In_opt_ PVOID SystemArgument2)
{
    PFDO_DEVICE_EXTENSION DeviceExtension = DeferredContext;
    PSTOR_SRB_CONTEXT SrbContext, Expired = NULL;
    PLIST_ENTRY Entry;
    ULONGLONG Now = KeQueryInterruptTime();

    UNREFERENCED_PARAMETER(Dpc);
    UNREFERENCED_PARAMETER(SystemArgument1);
    UNREFERENCED_PARAMETER(SystemArgument2);
    ASSERT(KeGetCurrentIrql() == DISPATCH_LEVEL);

    KeAcquireSpinLockAtDpcLevel(&DeviceExtension->ActiveLock);
    if (IsListEmpty(&DeviceExtension->ActiveRequests) || DeviceExtension->TimeoutsStopped)
    {
        DeviceExtension->TimeoutArmed = FALSE;
        KeCancelTimer(&DeviceExtension->TimeoutTimer);
    }
    else if (!DeviceExtension->ResetActive && (KeReadStateEvent(&DeviceExtension->ResetIdle) != 0) &&
             (DeviceExtension->PnpState == dsStarted) && !DeviceExtension->DumpMode)
    {
        for (Entry = DeviceExtension->ActiveRequests.Flink;
             Entry != &DeviceExtension->ActiveRequests;
             Entry = Entry->Flink)
        {
            SrbContext = CONTAINING_RECORD(Entry, STOR_SRB_CONTEXT, ActiveEntry);
            ASSERT(SrbContext->State != PORT_SRB_PREPARED);
            if ((SrbContext->State == PORT_SRB_ACTIVE) && (SrbContext->Deadline <= Now))
            {
                Expired = SrbContext;
                break;
            }
        }

        if (Expired != NULL)
        {
            KeAcquireSpinLockAtDpcLevel(&DeviceExtension->RequestHoldLock);
            ASSERT(!DeviceExtension->BusResetActive);
            DeviceExtension->ResetPathId = Expired->LegacySrb->PathId;
            DeviceExtension->ResetTargetId = Expired->LegacySrb->TargetId;
            DeviceExtension->ResetLun = Expired->LegacySrb->Lun;
            DeviceExtension->ResetTimeOutValue = Expired->LegacySrb->TimeOutValue;
            DeviceExtension->ResetActive = TRUE;
            KeReleaseSpinLockFromDpcLevel(&DeviceExtension->RequestHoldLock);
            KeClearEvent(&DeviceExtension->ResetIdle);
            KeClearEvent(&DeviceExtension->Recovered);
            InterlockedIncrement(&DeviceExtension->RecoveringRequests);
            for (Entry = DeviceExtension->ActiveRequests.Flink;
                 Entry != &DeviceExtension->ActiveRequests;
                 Entry = Entry->Flink)
            {
                SrbContext = CONTAINING_RECORD(Entry, STOR_SRB_CONTEXT, ActiveEntry);
                if (!PortFdoSameUnit(DeviceExtension, SrbContext->LegacySrb) || (SrbContext->Deadline > Now))
                    continue;
                InterlockedIncrement(&DeviceExtension->RecoveringRequests);
                if (InterlockedCompareExchange(&SrbContext->State, PORT_SRB_RECOVERING, PORT_SRB_ACTIVE) !=
                    PORT_SRB_ACTIVE)
                {
                    PortFdoRequestRecoveredLocked(DeviceExtension);
                }
            }
            PortFdoRequestRecoveredLocked(DeviceExtension);
            DPRINT1("Storport: a request to %u:%u:%u timed out after %lu seconds, resetting the logical unit\n",
                    DeviceExtension->ResetPathId, DeviceExtension->ResetTargetId, DeviceExtension->ResetLun,
                    DeviceExtension->ResetTimeOutValue);
        }
    }
    KeReleaseSpinLockFromDpcLevel(&DeviceExtension->ActiveLock);

    if (Expired != NULL)
        IoQueueWorkItem(DeviceExtension->ResetWorkItem, PortFdoResetWorker, DelayedWorkQueue, NULL);
}


VOID
PortFdoStopTimeouts(
    _In_ PFDO_DEVICE_EXTENSION DeviceExtension)
{
    KIRQL OldIrql;

    ASSERT(KeGetCurrentIrql() == PASSIVE_LEVEL);

    KeAcquireSpinLock(&DeviceExtension->ActiveLock, &OldIrql);
    DeviceExtension->TimeoutsStopped = TRUE;
    DeviceExtension->TimeoutArmed = FALSE;
    KeCancelTimer(&DeviceExtension->TimeoutTimer);
    KeSetEvent(&DeviceExtension->TimeoutsStopEvent, IO_NO_INCREMENT, FALSE);
    KeReleaseSpinLock(&DeviceExtension->ActiveLock, OldIrql);
    KeFlushQueuedDpcs();
    KeWaitForSingleObject(&DeviceExtension->ResetIdle, Executive, KernelMode, FALSE, NULL);
    KeFlushQueuedDpcs();
    ASSERT(!DeviceExtension->ResetActive);
    ASSERT(!DeviceExtension->TimeoutArmed);
}


VOID
PortFdoStartTimeouts(
    _In_ PFDO_DEVICE_EXTENSION DeviceExtension)
{
    KIRQL OldIrql;

    ASSERT(KeGetCurrentIrql() <= DISPATCH_LEVEL);

    KeAcquireSpinLock(&DeviceExtension->ActiveLock, &OldIrql);
    ASSERT(!DeviceExtension->ResetActive);
    ASSERT(KeReadStateEvent(&DeviceExtension->ResetIdle) != 0);
    ASSERT(!DeviceExtension->TimeoutArmed);
    DeviceExtension->TimeoutsStopped = FALSE;
    KeClearEvent(&DeviceExtension->TimeoutsStopEvent);
    if (!IsListEmpty(&DeviceExtension->ActiveRequests))
        PortFdoArmTimeoutsLocked(DeviceExtension);
    KeReleaseSpinLock(&DeviceExtension->ActiveLock, OldIrql);
}


static
VOID
PortFdoHoldRequests(
    _In_ PFDO_DEVICE_EXTENSION DeviceExtension,
    _In_ BOOLEAN Drain)
{
    KIRQL OldIrql;
    LONG Outstanding;

    KeAcquireSpinLock(&DeviceExtension->RequestHoldLock, &OldIrql);
    DeviceExtension->HoldRequests = TRUE;
    KeClearEvent(&DeviceExtension->RequestsDrained);
    Outstanding = DeviceExtension->OutstandingRequests;
    KeReleaseSpinLock(&DeviceExtension->RequestHoldLock, OldIrql);

    if (Drain && (Outstanding != 0))
        KeWaitForSingleObject(&DeviceExtension->RequestsDrained, Executive, KernelMode, FALSE, NULL);
}


static
VOID
PortFdoReleaseRequests(
    _In_ PFDO_DEVICE_EXTENSION DeviceExtension,
    _In_ NTSTATUS Status)
{
    LIST_ENTRY Held;
    PIO_STACK_LOCATION Stack;
    KIRQL OldIrql;
    PIRP Irp;

    InitializeListHead(&Held);
    KeAcquireSpinLock(&DeviceExtension->RequestHoldLock, &OldIrql);
    if (NT_SUCCESS(Status))
        DeviceExtension->HoldRequests = FALSE;
    while (!IsListEmpty(&DeviceExtension->HeldRequests))
        InsertTailList(&Held, RemoveHeadList(&DeviceExtension->HeldRequests));
    KeReleaseSpinLock(&DeviceExtension->RequestHoldLock, OldIrql);

    while (!IsListEmpty(&Held))
    {
        Irp = CONTAINING_RECORD(RemoveHeadList(&Held), IRP, Tail.Overlay.ListEntry);
        Stack = IoGetCurrentIrpStackLocation(Irp);
        if (NT_SUCCESS(Status))
        {
            PortPdoScsi(Stack->DeviceObject, Irp);
            continue;
        }

        Stack->Parameters.Scsi.Srb->SrbStatus = SRB_STATUS_NO_DEVICE;
        Irp->IoStatus.Information = 0;
        Irp->IoStatus.Status = Status;
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
    }
}


static
VOID
PortFdoSendAdapterPnp(
    _In_ PFDO_DEVICE_EXTENSION DeviceExtension,
    _In_ STOR_PNP_ACTION Action)
{
    SCSI_PNP_REQUEST_BLOCK Srb;
    IO_STATUS_BLOCK IoStatusBlock;
    PIO_STACK_LOCATION Stack;
    KEVENT Event;
    PIRP Irp;

    if (DeviceExtension->PnpState != dsStarted)
        return;

    KeInitializeEvent(&Event, NotificationEvent, FALSE);
    Irp = IoBuildDeviceIoControlRequest(IOCTL_SCSI_EXECUTE_NONE,
                                        DeviceExtension->Device,
                                        NULL,
                                        0,
                                        NULL,
                                        0,
                                        TRUE,
                                        &Event,
                                        &IoStatusBlock);
    if (Irp == NULL)
        return;

    RtlZeroMemory(&Srb, sizeof(Srb));
    Srb.Length = sizeof(SCSI_PNP_REQUEST_BLOCK);
    Srb.Function = SRB_FUNCTION_PNP;
    Srb.PnPAction = Action;
    Srb.SrbPnPFlags = SRB_PNP_FLAGS_ADAPTER_REQUEST;
    Srb.SrbFlags = SRB_FLAGS_NO_QUEUE_FREEZE;
    Srb.TimeOutValue = 10;
    Srb.OriginalRequest = Irp;

    IoSetNextIrpStackLocation(Irp);
    Stack = IoGetCurrentIrpStackLocation(Irp);
    Stack->DeviceObject = DeviceExtension->Device;
    Stack->Parameters.Scsi.Srb = (PSCSI_REQUEST_BLOCK)&Srb;

    PortFdoStartRequest(DeviceExtension, Irp, TRUE);
    if (PortSubmitSrb(DeviceExtension, DeviceExtension->Device, Irp, (PSCSI_REQUEST_BLOCK)&Srb) == STATUS_PENDING)
        KeWaitForSingleObject(&Event, Executive, KernelMode, FALSE, NULL);
}


static
NTSTATUS
PortFdoRemoveDevice(
    _In_ PFDO_DEVICE_EXTENSION DeviceExtension,
    _In_ PIRP Irp)
{
    PDEVICE_OBJECT LowerDevice = DeviceExtension->LowerDevice;
    NTSTATUS Status;

    PortFdoHoldRequests(DeviceExtension, DeviceExtension->PnpState != dsSurpriseRemoved);
    PortFdoSendAdapterPnp(DeviceExtension, StorRemoveDevice);
    PortFdoStopTimeouts(DeviceExtension);
    PortFdoStopAdapter(DeviceExtension);
    DeviceExtension->PnpState = dsRemoved;
    PortFdoReleaseRequests(DeviceExtension, STATUS_NO_SUCH_DEVICE);
    PortFdoReleaseAdapter(DeviceExtension);

    Irp->IoStatus.Status = STATUS_SUCCESS;
    IoSkipCurrentIrpStackLocation(Irp);
    Status = IoCallDriver(LowerDevice, Irp);

    IoDetachDevice(LowerDevice);
    PortDeleteScsiPortLinks(DeviceExtension);
    ASSERT(DeviceExtension->TimeoutsStopped);
    ASSERT(KeReadStateEvent(&DeviceExtension->ResetIdle) != 0);
    KeCancelTimer(&DeviceExtension->BusyRetryTimer);
    KeFlushQueuedDpcs();
    KeFlushQueuedDpcs();
    ASSERT(ExQueryDepthSList(&DeviceExtension->CompleteRequestsPending) == 0);
    ExFreePoolWithTag(DeviceExtension->CompleteRequestsPool, TAG_RESET_REQUEST);
    IoFreeWorkItem(DeviceExtension->ResetWorkItem);
    IoDeleteDevice(DeviceExtension->Device);
    return Status;
}


NTSTATUS
NTAPI
PortFdoPnp(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp)
{
    PFDO_DEVICE_EXTENSION DeviceExtension;
    PIO_STACK_LOCATION Stack;
    ULONG_PTR Information = 0;
    NTSTATUS Status = STATUS_NOT_SUPPORTED;

    DPRINT("PortFdoPnp(%p %p)\n",
            DeviceObject, Irp);

    DeviceExtension = (PFDO_DEVICE_EXTENSION)DeviceObject->DeviceExtension;
    ASSERT(DeviceExtension);
    ASSERT(DeviceExtension->ExtensionType == FdoExtension);

    Stack = IoGetCurrentIrpStackLocation(Irp);

    switch (Stack->MinorFunction)
    {
        case IRP_MN_START_DEVICE: /* 0x00 */
            DPRINT("IRP_MJ_PNP / IRP_MN_START_DEVICE\n");
            Status = PortFdoStartDevice(DeviceExtension, Irp);
            if (NT_SUCCESS(Status))
                PortFdoReleaseRequests(DeviceExtension, STATUS_SUCCESS);
            break;

        case IRP_MN_QUERY_REMOVE_DEVICE: /* 0x01 */
            DPRINT1("IRP_MJ_PNP / IRP_MN_QUERY_REMOVE_DEVICE\n");
            Irp->IoStatus.Status = STATUS_SUCCESS;
            return ForwardIrpAndForget(DeviceExtension->LowerDevice, Irp);

        case IRP_MN_REMOVE_DEVICE: /* 0x02 */
            DPRINT1("IRP_MJ_PNP / IRP_MN_REMOVE_DEVICE\n");
            return PortFdoRemoveDevice(DeviceExtension, Irp);

        case IRP_MN_CANCEL_REMOVE_DEVICE: /* 0x03 */
            DPRINT1("IRP_MJ_PNP / IRP_MN_CANCEL_REMOVE_DEVICE\n");
            Irp->IoStatus.Status = STATUS_SUCCESS;
            return ForwardIrpAndForget(DeviceExtension->LowerDevice, Irp);

        case IRP_MN_STOP_DEVICE: /* 0x04 */
            DPRINT1("IRP_MJ_PNP / IRP_MN_STOP_DEVICE\n");
            PortFdoHoldRequests(DeviceExtension, TRUE);
            PortFdoStopTimeouts(DeviceExtension);
            PortFdoStopAdapter(DeviceExtension);
            PortFdoReleaseHardware(DeviceExtension);
            Irp->IoStatus.Status = STATUS_SUCCESS;
            return ForwardIrpAndForget(DeviceExtension->LowerDevice, Irp);

        case IRP_MN_QUERY_STOP_DEVICE: /* 0x05 */
            DPRINT1("IRP_MJ_PNP / IRP_MN_QUERY_STOP_DEVICE\n");
            PortFdoHoldRequests(DeviceExtension, TRUE);
            Irp->IoStatus.Status = STATUS_SUCCESS;
            return ForwardIrpAndForget(DeviceExtension->LowerDevice, Irp);

        case IRP_MN_CANCEL_STOP_DEVICE: /* 0x06 */
            DPRINT1("IRP_MJ_PNP / IRP_MN_CANCEL_STOP_DEVICE\n");
            Irp->IoStatus.Status = STATUS_SUCCESS;
            IoForwardIrpSynchronously(DeviceExtension->LowerDevice, Irp);
            if (DeviceExtension->PnpState == dsStarted)
                PortFdoReleaseRequests(DeviceExtension, STATUS_SUCCESS);
            Status = STATUS_SUCCESS;
            break;

        case IRP_MN_QUERY_DEVICE_RELATIONS: /* 0x07 */
            DPRINT("IRP_MJ_PNP / IRP_MN_QUERY_DEVICE_RELATIONS\n");
            switch (Stack->Parameters.QueryDeviceRelations.Type)
            {
                case BusRelations:
                    DPRINT("    IRP_MJ_PNP / IRP_MN_QUERY_DEVICE_RELATIONS / BusRelations\n");
                    Status = PortFdoQueryBusRelations(DeviceExtension, &Information);
                    break;

                case RemovalRelations:
                    DPRINT1("    IRP_MJ_PNP / IRP_MN_QUERY_DEVICE_RELATIONS / RemovalRelations\n");
                    return ForwardIrpAndForget(DeviceExtension->LowerDevice, Irp);

                default:
                    DPRINT1("    IRP_MJ_PNP / IRP_MN_QUERY_DEVICE_RELATIONS / Unknown type 0x%lx\n",
                            Stack->Parameters.QueryDeviceRelations.Type);
                    return ForwardIrpAndForget(DeviceExtension->LowerDevice, Irp);
            }
            break;

        case IRP_MN_QUERY_INTERFACE: /* 0x08 */
            DPRINT("IRP_MJ_PNP / IRP_MN_QUERY_INTERFACE\n");
            return ForwardIrpAndForget(DeviceExtension->LowerDevice, Irp);

        case IRP_MN_QUERY_CAPABILITIES: /* 0x09 */
            DPRINT("IRP_MJ_PNP / IRP_MN_QUERY_CAPABILITIES\n");
            return ForwardIrpAndForget(DeviceExtension->LowerDevice, Irp);

        case IRP_MN_FILTER_RESOURCE_REQUIREMENTS: /* 0x0d */
            DPRINT("IRP_MJ_PNP / IRP_MN_FILTER_RESOURCE_REQUIREMENTS\n");
            PortFdoFilterRequirements(DeviceExtension, Irp);
            return ForwardIrpAndForget(DeviceExtension->LowerDevice, Irp);

        case IRP_MN_QUERY_PNP_DEVICE_STATE: /* 0x14 */
            DPRINT("IRP_MJ_PNP / IRP_MN_QUERY_PNP_DEVICE_STATE\n");
            if (PortIsInUsagePath(&DeviceExtension->Usage))
            {
                Irp->IoStatus.Information |= PNP_DEVICE_NOT_DISABLEABLE;
                Irp->IoStatus.Status = STATUS_SUCCESS;
            }
            return ForwardIrpAndForget(DeviceExtension->LowerDevice, Irp);

        case IRP_MN_DEVICE_USAGE_NOTIFICATION: /* 0x16 */
            DPRINT1("IRP_MJ_PNP / IRP_MN_DEVICE_USAGE_NOTIFICATION\n");
            if (!IoForwardIrpSynchronously(DeviceExtension->LowerDevice, Irp))
            {
                Status = STATUS_UNSUCCESSFUL;
                break;
            }
            Status = Irp->IoStatus.Status;
            Information = Irp->IoStatus.Information;
            if (NT_SUCCESS(Status))
                PortAdjustUsageCount(&DeviceExtension->Usage, Stack);
            break;

        case IRP_MN_SURPRISE_REMOVAL: /* 0x17 */
            DPRINT1("IRP_MJ_PNP / IRP_MN_SURPRISE_REMOVAL\n");
            if (DeviceExtension->PnpState == dsStarted)
            {
                PortFdoSendAdapterPnp(DeviceExtension, StorSurpriseRemoval);
                PortFdoStopTimeouts(DeviceExtension);
                MiniportAdapterControl(&DeviceExtension->Miniport, ScsiAdapterSurpriseRemoval, NULL);
                DeviceExtension->PnpState = dsSurpriseRemoved;
            }
            Irp->IoStatus.Status = STATUS_SUCCESS;
            return ForwardIrpAndForget(DeviceExtension->LowerDevice, Irp);

        default:
            DPRINT1("IRP_MJ_PNP / Unknown IOCTL 0x%lx\n", Stack->MinorFunction);
            return ForwardIrpAndForget(DeviceExtension->LowerDevice, Irp);
    }

    Irp->IoStatus.Information = Information;
    Irp->IoStatus.Status = Status;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);

    return Status;
}


static
VOID
PortFdoRestartAdapter(
    _In_ PFDO_DEVICE_EXTENSION DeviceExtension)
{
    STOR_LOCK_HANDLE LockHandle;
    NTSTATUS Status;

    if (DeviceExtension->PnpState != dsStarted)
        return;

    if (MiniportAdapterControl(&DeviceExtension->Miniport, ScsiRestartAdapter, NULL) == ScsiAdapterControlSuccess)
        return;

    Status = MiniportFindAdapter(&DeviceExtension->Miniport);
    if (NT_SUCCESS(Status))
    {
        PortAcquireSpinLock(DeviceExtension, InterruptLock, NULL, &LockHandle);
        Status = MiniportHwInitialize(&DeviceExtension->Miniport);
        PortReleaseSpinLock(DeviceExtension, &LockHandle);
    }

    if (NT_SUCCESS(Status) &&
        DeviceExtension->HwPassiveInitRoutine != NULL &&
        !DeviceExtension->HwPassiveInitRoutine(&DeviceExtension->Miniport.MiniportExtension->HwDeviceExtension))
    {
        Status = STATUS_UNSUCCESSFUL;
    }

    if (!NT_SUCCESS(Status))
        DPRINT1("Reinitializing the adapter after power-up failed (Status 0x%08lx)\n", Status);
}


static IO_WORKITEM_ROUTINE PortFdoPowerUpWorker;

static
VOID
NTAPI
PortFdoPowerUpWorker(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_opt_ PVOID Context)
{
    PFDO_DEVICE_EXTENSION DeviceExtension = DeviceObject->DeviceExtension;
    PIRP Irp = Context;
    PIO_WORKITEM WorkItem = Irp->Tail.Overlay.DriverContext[0];
    POWER_STATE PowerState;

    DeviceExtension->DumpMode = FALSE;
    KeMemoryBarrier();
    PortFdoRestartAdapter(DeviceExtension);
    PortFdoStartTimeouts(DeviceExtension);

    PowerState.DeviceState = PowerDeviceD0;
    DeviceExtension->DevicePowerState = PowerDeviceD0;
    PoSetPowerState(DeviceObject, DevicePowerState, PowerState);
    PortFdoReleaseRequests(DeviceExtension, STATUS_SUCCESS);

    PoStartNextPowerIrp(Irp);
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    IoFreeWorkItem(WorkItem);
}


static IO_COMPLETION_ROUTINE PortFdoPowerUpCompletion;

static
NTSTATUS
NTAPI
PortFdoPowerUpCompletion(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp,
    _In_opt_ PVOID Context)
{
    PFDO_DEVICE_EXTENSION DeviceExtension = Context;
    PIO_WORKITEM WorkItem;

    UNREFERENCED_PARAMETER(DeviceObject);

    if (NT_SUCCESS(Irp->IoStatus.Status))
    {
        WorkItem = IoAllocateWorkItem(DeviceExtension->Device);
        if (WorkItem != NULL)
        {
            Irp->Tail.Overlay.DriverContext[0] = WorkItem;
            IoQueueWorkItem(WorkItem, PortFdoPowerUpWorker, CriticalWorkQueue, Irp);
            return STATUS_MORE_PROCESSING_REQUIRED;
        }

        Irp->IoStatus.Status = STATUS_INSUFFICIENT_RESOURCES;
    }

    PortFdoReleaseRequests(DeviceExtension, Irp->IoStatus.Status);
    PoStartNextPowerIrp(Irp);
    return STATUS_CONTINUE_COMPLETION;
}


static REQUEST_POWER_COMPLETE PortFdoDevicePowerRequestComplete;

static
VOID
NTAPI
PortFdoDevicePowerRequestComplete(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ UCHAR MinorFunction,
    _In_ POWER_STATE PowerState,
    _In_opt_ PVOID Context,
    _In_ PIO_STATUS_BLOCK IoStatus)
{
    PIRP SystemIrp = Context;

    UNREFERENCED_PARAMETER(DeviceObject);
    UNREFERENCED_PARAMETER(MinorFunction);
    UNREFERENCED_PARAMETER(PowerState);

    SystemIrp->IoStatus.Status = IoStatus->Status;
    PoStartNextPowerIrp(SystemIrp);
    IoCompleteRequest(SystemIrp, IO_NO_INCREMENT);
}


static IO_COMPLETION_ROUTINE PortFdoSystemPowerCompletion;

static
NTSTATUS
NTAPI
PortFdoSystemPowerCompletion(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp,
    _In_opt_ PVOID Context)
{
    PFDO_DEVICE_EXTENSION DeviceExtension = Context;
    PIO_STACK_LOCATION Stack = IoGetCurrentIrpStackLocation(Irp);
    POWER_STATE PowerState;
    NTSTATUS Status;

    UNREFERENCED_PARAMETER(DeviceObject);

    if (!NT_SUCCESS(Irp->IoStatus.Status))
    {
        PoStartNextPowerIrp(Irp);
        return STATUS_CONTINUE_COMPLETION;
    }

    PowerState.DeviceState = (Stack->Parameters.Power.State.SystemState == PowerSystemWorking) ?
                             PowerDeviceD0 : PowerDeviceD3;
    if (PowerState.DeviceState == DeviceExtension->DevicePowerState ||
        (PowerState.DeviceState == PowerDeviceD0 &&
         DeviceExtension->DevicePowerState == PowerDeviceUnspecified))
    {
        PoStartNextPowerIrp(Irp);
        return STATUS_CONTINUE_COMPLETION;
    }

    Status = PoRequestPowerIrp(DeviceExtension->Device,
                               IRP_MN_SET_POWER,
                               PowerState,
                               PortFdoDevicePowerRequestComplete,
                               Irp,
                               NULL);
    if (!NT_SUCCESS(Status))
    {
        Irp->IoStatus.Status = Status;
        PoStartNextPowerIrp(Irp);
        return STATUS_CONTINUE_COMPLETION;
    }

    return STATUS_MORE_PROCESSING_REQUIRED;
}


NTSTATUS
NTAPI
PortFdoPower(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp)
{
    PFDO_DEVICE_EXTENSION DeviceExtension = DeviceObject->DeviceExtension;
    PIO_STACK_LOCATION Stack = IoGetCurrentIrpStackLocation(Irp);
    DEVICE_POWER_STATE NewState;

    if (Stack->MinorFunction != IRP_MN_SET_POWER || DeviceExtension->PnpState != dsStarted)
    {
        PoStartNextPowerIrp(Irp);
        IoSkipCurrentIrpStackLocation(Irp);
        return PoCallDriver(DeviceExtension->LowerDevice, Irp);
    }

    if (Stack->Parameters.Power.Type == SystemPowerState)
    {
        DeviceExtension->SystemPowerState = Stack->Parameters.Power.State.SystemState;
        IoMarkIrpPending(Irp);
        IoCopyCurrentIrpStackLocationToNext(Irp);
        IoSetCompletionRoutine(Irp, PortFdoSystemPowerCompletion, DeviceExtension, TRUE, TRUE, TRUE);
        PoCallDriver(DeviceExtension->LowerDevice, Irp);
        return STATUS_PENDING;
    }

    NewState = Stack->Parameters.Power.State.DeviceState;
    if (NewState == PowerDeviceD0)
    {
        if (DeviceExtension->DevicePowerState == PowerDeviceD0 ||
            DeviceExtension->DevicePowerState == PowerDeviceUnspecified)
        {
            PoStartNextPowerIrp(Irp);
            IoSkipCurrentIrpStackLocation(Irp);
            return PoCallDriver(DeviceExtension->LowerDevice, Irp);
        }

        IoMarkIrpPending(Irp);
        IoCopyCurrentIrpStackLocationToNext(Irp);
        IoSetCompletionRoutine(Irp, PortFdoPowerUpCompletion, DeviceExtension, TRUE, TRUE, TRUE);
        PoCallDriver(DeviceExtension->LowerDevice, Irp);
        return STATUS_PENDING;
    }

    if (DeviceExtension->DevicePowerState == PowerDeviceD0 ||
        DeviceExtension->DevicePowerState == PowerDeviceUnspecified)
    {
        PortFdoHoldRequests(DeviceExtension, TRUE);
        PortFdoStopTimeouts(DeviceExtension);
        ASSERT(DeviceExtension->TimeoutsStopped && (KeReadStateEvent(&DeviceExtension->ResetIdle) != 0));
        DPRINT1("Storport: adapter %p to D%u: requests drained, timeouts stopped, stopping the miniport\n",
                DeviceExtension->Device, NewState - PowerDeviceD0);
        MiniportAdapterControl(&DeviceExtension->Miniport, ScsiStopAdapter, NULL);
    }

    DeviceExtension->DevicePowerState = NewState;
    PoSetPowerState(DeviceObject, DevicePowerState, Stack->Parameters.Power.State);

    PoStartNextPowerIrp(Irp);
    IoSkipCurrentIrpStackLocation(Irp);
    return PoCallDriver(DeviceExtension->LowerDevice, Irp);
}

/* EOF */
