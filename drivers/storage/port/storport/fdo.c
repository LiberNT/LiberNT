/*
 * PROJECT:     ReactOS Storport Driver
 * LICENSE:     GPL-2.0+ (https://spdx.org/licenses/GPL-2.0+)
 * PURPOSE:     Storport FDO code
 * COPYRIGHT:   Copyright 2017 Eric Kohl (eric.kohl@reactos.org)
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
        /* The kernel connected the fallback line interrupt instead. */
        DeviceExtension->Interrupt = (PKINTERRUPT)DeviceExtension->MessageInfo;
        DeviceExtension->InterruptConnectedEx = TRUE;
        DeviceExtension->MessageInfo = NULL;
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
    if (InterfaceType == InterfaceTypeUndefined)
        return STATUS_NO_SUCH_DEVICE;

    /* Get the driver init data for the given interface type */
    InitData = PortGetDriverInitData(DeviceExtension->DriverExtension,
                                     InterfaceType);
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

    Status = PortInitializeDma(DeviceExtension, &DeviceExtension->Miniport.PortConfig);
    if (!NT_SUCCESS(Status))
        return Status;

    /* The request sizes are settled now; back them with lookasides. */
    Status = PortFdoInitializeRequestPools(DeviceExtension);
    if (!NT_SUCCESS(Status))
        return Status;

    /* Connect the configured interrupt */
    Status = PortFdoConnectInterrupt(DeviceExtension);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("PortFdoConnectInterrupt() failed (Status 0x%08lx)\n", Status);
        return Status;
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



static
NTSTATUS
PortFdoScanBus(
    _In_ PFDO_DEVICE_EXTENSION DeviceExtension)
{
    PPDO_DEVICE_EXTENSION PdoExtension;
    ULONG Bus, Target; //, Lun;
    NTSTATUS Status;

    DPRINT("PortFdoScanBus(%p)\n", DeviceExtension);

    DPRINT("NumberOfBuses: %lu\n", DeviceExtension->Miniport.PortConfig.NumberOfBuses);
    DPRINT("MaximumNumberOfTargets: %lu\n", DeviceExtension->Miniport.PortConfig.MaximumNumberOfTargets);
    DPRINT("MaximumNumberOfLogicalUnits: %lu\n", DeviceExtension->Miniport.PortConfig.MaximumNumberOfLogicalUnits);

    /* Scan all buses */
    for (Bus = 0; Bus < DeviceExtension->Miniport.PortConfig.NumberOfBuses; Bus++)
    {
        DPRINT("Scanning bus %ld\n", Bus);

        /* Scan all targets */
        for (Target = 0; Target < DeviceExtension->Miniport.PortConfig.MaximumNumberOfTargets; Target++)
        {
            DPRINT("  Scanning target %ld:%ld\n", Bus, Target);

            DPRINT("    Scanning logical unit %ld:%ld:%ld\n", Bus, Target, 0);
            Status = PortCreatePdo(DeviceExtension, Bus, Target, 0, &PdoExtension);
            if (NT_SUCCESS(Status))
            {
                /* Scan LUN 0 */
                Status = PortSendInquiry(PdoExtension);
                DPRINT("PortSendInquiry returned 0x%08lx\n", Status);
                if (!NT_SUCCESS(Status))
                {
                    PortDeletePdo(PdoExtension);
                }
                else
                {
                    PortReadSerialNumber(PdoExtension);
                    DPRINT("VendorId: %.8s\n", PdoExtension->InquiryBuffer->VendorId);
                    DPRINT("ProductId: %.16s\n", PdoExtension->InquiryBuffer->ProductId);
                    DPRINT("ProductRevisionLevel: %.4s\n", PdoExtension->InquiryBuffer->ProductRevisionLevel);
                    DPRINT("VendorSpecific: %.20s\n", PdoExtension->InquiryBuffer->VendorSpecific);
                }
            }

#if 0
            /* Scan all logical units */
            for (Lun = 1; Lun < DeviceExtension->Miniport.PortConfig.MaximumNumberOfLogicalUnits; Lun++)
            {
                DPRINT("    Scanning logical unit %ld:%ld:%ld\n", Bus, Target, Lun);
                Status = PortSendInquiry(DeviceExtension->Device, Bus, Target, Lun);
                DPRINT("PortSendInquiry returned 0x%08lx\n", Status);
                if (!NT_SUCCESS(Status))
                    break;
            }
#endif
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

    if (!DeviceExtension->BusScanned)
    {
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

    MiniportAdapterControl(&DeviceExtension->Miniport, ScsiStopAdapter, NULL);
    MiniportAdapterControl(&DeviceExtension->Miniport, ScsiSetBootConfig, NULL);
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


BOOLEAN
PortFdoStartRequest(
    _In_ PFDO_DEVICE_EXTENSION DeviceExtension,
    _In_ PIRP Irp,
    _In_ BOOLEAN PortRequest)
{
    KIRQL OldIrql;
    BOOLEAN Started = TRUE;

    KeAcquireSpinLock(&DeviceExtension->RequestHoldLock, &OldIrql);
    if (DeviceExtension->HoldRequests && !PortRequest)
    {
        IoMarkIrpPending(Irp);
        InsertTailList(&DeviceExtension->HeldRequests, &Irp->Tail.Overlay.ListEntry);
        Started = FALSE;
    }
    else
    {
        InterlockedIncrement(&DeviceExtension->OutstandingRequests);
    }
    KeReleaseSpinLock(&DeviceExtension->RequestHoldLock, OldIrql);
    return Started;
}


VOID
PortFdoEndRequest(
    _In_ PFDO_DEVICE_EXTENSION DeviceExtension)
{
    if (InterlockedDecrement(&DeviceExtension->OutstandingRequests) == 0)
        KeSetEvent(&DeviceExtension->RequestsDrained, IO_NO_INCREMENT, FALSE);
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

    if ((DeviceExtension->PnpState != dsStarted) ||
        (DeviceExtension->Miniport.PortConfig.SrbType != SRB_TYPE_SCSI_REQUEST_BLOCK))
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
    PortFdoStopAdapter(DeviceExtension);
    DeviceExtension->PnpState = dsRemoved;
    PortFdoReleaseRequests(DeviceExtension, STATUS_NO_SUCH_DEVICE);
    PortFdoReleaseAdapter(DeviceExtension);

    Irp->IoStatus.Status = STATUS_SUCCESS;
    IoSkipCurrentIrpStackLocation(Irp);
    Status = IoCallDriver(LowerDevice, Irp);

    IoDetachDevice(LowerDevice);
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

        case IRP_MN_QUERY_CAPABILITIES: /* 0x09 */
            DPRINT("IRP_MJ_PNP / IRP_MN_QUERY_CAPABILITIES\n");
            return ForwardIrpAndForget(DeviceExtension->LowerDevice, Irp);

        case IRP_MN_FILTER_RESOURCE_REQUIREMENTS: /* 0x0d */
            DPRINT("IRP_MJ_PNP / IRP_MN_FILTER_RESOURCE_REQUIREMENTS\n");
            PortFdoFilterRequirements(DeviceExtension, Irp);
            return ForwardIrpAndForget(DeviceExtension->LowerDevice, Irp);

        case IRP_MN_QUERY_PNP_DEVICE_STATE: /* 0x14 */
            DPRINT("IRP_MJ_PNP / IRP_MN_QUERY_PNP_DEVICE_STATE\n");
            return ForwardIrpAndForget(DeviceExtension->LowerDevice, Irp);

        case IRP_MN_DEVICE_USAGE_NOTIFICATION: /* 0x16 */
            DPRINT1("IRP_MJ_PNP / IRP_MN_DEVICE_USAGE_NOTIFICATION\n");
            break;

        case IRP_MN_SURPRISE_REMOVAL: /* 0x17 */
            DPRINT1("IRP_MJ_PNP / IRP_MN_SURPRISE_REMOVAL\n");
            if (DeviceExtension->PnpState == dsStarted)
            {
                PortFdoSendAdapterPnp(DeviceExtension, StorSurpriseRemoval);
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

/* EOF */
