/*
 * PROJECT:     ReactOS Universal Serial Bus Human Interface Device Driver
 * LICENSE:     GPL - See COPYING in the top level directory
 * FILE:        drivers/hid/hidclass/hidclass.c
 * PURPOSE:     HID Class Driver
 * PROGRAMMERS:
 *              Michael Martin (michael.martin@reactos.org)
 *              Johannes Anderwald (johannes.anderwald@reactos.org)
 */

#include "precomp.h"

#define NDEBUG
#include <debug.h>

static LPWSTR ClientIdentificationAddress = L"HIDCLASS";
static ULONG HidClassDeviceNumber = 0;

NTSTATUS
NTAPI
DllInitialize(
    IN PUNICODE_STRING RegistryPath)
{
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
DllUnload(VOID)
{
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
HidClassAddDevice(
    IN PDRIVER_OBJECT DriverObject,
    IN PDEVICE_OBJECT PhysicalDeviceObject)
{
    WCHAR CharDeviceName[64];
    NTSTATUS Status;
    UNICODE_STRING DeviceName;
    PDEVICE_OBJECT NewDeviceObject;
    PHIDCLASS_FDO_EXTENSION FDODeviceExtension;
    ULONG DeviceExtensionSize;
    PHIDCLASS_DRIVER_EXTENSION DriverExtension;

    /* increment device number */
    InterlockedIncrement((PLONG)&HidClassDeviceNumber);

    /* construct device name */
    _swprintf(CharDeviceName, L"\\Device\\_HID%08x", HidClassDeviceNumber);

    /* initialize device name */
    RtlInitUnicodeString(&DeviceName, CharDeviceName);

    /* get driver object extension */
    DriverExtension = IoGetDriverObjectExtension(DriverObject, ClientIdentificationAddress);
    if (!DriverExtension)
    {
        /* device removed */
        ASSERT(FALSE);
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    }

    /* calculate device extension size */
    DeviceExtensionSize = sizeof(HIDCLASS_FDO_EXTENSION) + DriverExtension->DeviceExtensionSize;

    /* now create the device */
    Status = IoCreateDevice(DriverObject, DeviceExtensionSize, &DeviceName, FILE_DEVICE_UNKNOWN, 0, FALSE, &NewDeviceObject);
    if (!NT_SUCCESS(Status))
    {
        /* failed to create device object */
        ASSERT(FALSE);
        return Status;
    }

    /* get device extension */
    FDODeviceExtension = NewDeviceObject->DeviceExtension;

    /* zero device extension */
    RtlZeroMemory(FDODeviceExtension, sizeof(HIDCLASS_FDO_EXTENSION));

    /* initialize device extension */
    HidClassFDO_InitializeRead(NewDeviceObject);
    FDODeviceExtension->Common.IsFDO = TRUE;
    FDODeviceExtension->Common.DriverExtension = DriverExtension;
    FDODeviceExtension->Common.HidDeviceExtension.PhysicalDeviceObject = PhysicalDeviceObject;
    FDODeviceExtension->Common.HidDeviceExtension.MiniDeviceExtension = (PVOID)((ULONG_PTR)FDODeviceExtension + sizeof(HIDCLASS_FDO_EXTENSION));
    FDODeviceExtension->Common.HidDeviceExtension.NextDeviceObject = IoAttachDeviceToDeviceStack(NewDeviceObject, PhysicalDeviceObject);
    if (FDODeviceExtension->Common.HidDeviceExtension.NextDeviceObject == NULL)
    {
        /* no PDO */
        IoDeleteDevice(NewDeviceObject);
        DPRINT1("[HIDCLASS] failed to attach to device stack\n");
        return STATUS_DEVICE_REMOVED;
    }

    /* sanity check */
    ASSERT(FDODeviceExtension->Common.HidDeviceExtension.NextDeviceObject);

    /* increment stack size */
    NewDeviceObject->StackSize++;

    /* init device object */
    NewDeviceObject->Flags |= DO_BUFFERED_IO | DO_POWER_PAGABLE;
    NewDeviceObject->Flags  &= ~DO_DEVICE_INITIALIZING;

    /* now call driver provided add device routine */
    ASSERT(DriverExtension->AddDevice != 0);
    Status = DriverExtension->AddDevice(DriverObject, NewDeviceObject);
    if (!NT_SUCCESS(Status))
    {
        /* failed */
        DPRINT1("HIDCLASS: AddDevice failed with %x\n", Status);
        IoDetachDevice(FDODeviceExtension->Common.HidDeviceExtension.NextDeviceObject);
        IoDeleteDevice(NewDeviceObject);
        return Status;
    }

    /* succeeded */
    return Status;
}

VOID
NTAPI
HidClassDriverUnload(
    IN PDRIVER_OBJECT DriverObject)
{
    UNIMPLEMENTED;
}

NTSTATUS
NTAPI
HidClass_Create(
    IN PDEVICE_OBJECT DeviceObject,
    IN PIRP Irp)
{
    PIO_STACK_LOCATION IoStack;
    PHIDCLASS_COMMON_DEVICE_EXTENSION CommonDeviceExtension;
    PHIDCLASS_PDO_DEVICE_EXTENSION PDODeviceExtension;
    PHIDCLASS_FILEOP_CONTEXT Context;
    PHIDP_COLLECTION_DESC CollectionDescription;
    KIRQL OldLevel;

    //
    // get device extension
    //
    CommonDeviceExtension = DeviceObject->DeviceExtension;
    if (CommonDeviceExtension->IsFDO)
    {
         //
         // only supported for PDO
         //
         Irp->IoStatus.Status = STATUS_UNSUCCESSFUL;
         IoCompleteRequest(Irp, IO_NO_INCREMENT);
         return STATUS_UNSUCCESSFUL;
    }

    //
    // must be a PDO
    //
    ASSERT(CommonDeviceExtension->IsFDO == FALSE);

    //
    // get device extension
    //
    PDODeviceExtension = DeviceObject->DeviceExtension;

    //
    // get stack location
    //
    IoStack = IoGetCurrentIrpStackLocation(Irp);

    DPRINT("ShareAccess %x\n", IoStack->Parameters.Create.ShareAccess);
    DPRINT("Options %x\n", IoStack->Parameters.Create.Options);
    DPRINT("DesiredAccess %x\n", IoStack->Parameters.Create.SecurityContext->DesiredAccess);

    //
    // allocate context
    //
    Context = ExAllocatePoolWithTag(NonPagedPool, sizeof(HIDCLASS_FILEOP_CONTEXT), HIDCLASS_TAG);
    if (!Context)
    {
        //
        // no memory
        //
        Irp->IoStatus.Status = STATUS_INSUFFICIENT_RESOURCES;
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    //
    // init context
    //
    RtlZeroMemory(Context, sizeof(HIDCLASS_FILEOP_CONTEXT));
    Context->DeviceExtension = PDODeviceExtension;
    InitializeListHead(&Context->PendingReads);

    CollectionDescription = HidClassPDO_GetCollectionDescription(&PDODeviceExtension->Common.DeviceDescription,
                                                                 PDODeviceExtension->CollectionNumber);
    if (CollectionDescription && CollectionDescription->InputLength)
    {
        Context->ReportSlotLength = sizeof(USHORT) + CollectionDescription->InputLength;
        Context->ReportQueue = ExAllocatePoolWithTag(NonPagedPool,
                                                     Context->ReportSlotLength * HIDCLASS_QUEUED_REPORTS,
                                                     HIDCLASS_TAG);
        if (!Context->ReportQueue)
        {
            ExFreePoolWithTag(Context, HIDCLASS_TAG);
            Irp->IoStatus.Status = STATUS_INSUFFICIENT_RESOURCES;
            IoCompleteRequest(Irp, IO_NO_INCREMENT);
            return STATUS_INSUFFICIENT_RESOURCES;
        }
    }

    KeAcquireSpinLock(&PDODeviceExtension->FDODeviceExtension->ReadLock, &OldLevel);
    InsertTailList(&PDODeviceExtension->FDODeviceExtension->OpenFileList, &Context->FdoListEntry);
    KeReleaseSpinLock(&PDODeviceExtension->FDODeviceExtension->ReadLock, OldLevel);

    //
    // store context
    //
    ASSERT(IoStack->FileObject);
    IoStack->FileObject->FsContext = Context;

    //
    // done
    //
    Irp->IoStatus.Status = STATUS_SUCCESS;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return STATUS_SUCCESS;
}

static PUCHAR
HidClass_GetReadBuffer(
    IN PIRP Irp)
{
    if (Irp->MdlAddress)
        return MmGetSystemAddressForMdlSafe(Irp->MdlAddress, NormalPagePriority);

    return Irp->AssociatedIrp.SystemBuffer;
}

static VOID
HidClass_CopyReport(
    IN PHIDCLASS_FDO_EXTENSION FDODeviceExtension,
    IN PIRP Irp,
    IN const UCHAR *Report,
    IN ULONG ReportLength)
{
    PIO_STACK_LOCATION IoStack = IoGetCurrentIrpStackLocation(Irp);
    ULONG ReadLength = IoStack->Parameters.Read.Length;
    ULONG Offset = FDODeviceExtension->Common.DeviceDescription.ReportIDs[0].ReportID ? 0 : 1;
    PUCHAR Address = HidClass_GetReadBuffer(Irp);
    ULONG CopyLength;

    Irp->IoStatus.Information = 0;
    if (!Address)
    {
        Irp->IoStatus.Status = STATUS_INVALID_USER_BUFFER;
        return;
    }

    if (Offset >= ReadLength)
    {
        Irp->IoStatus.Status = STATUS_BUFFER_TOO_SMALL;
        return;
    }

    if (Offset)
        Address[0] = 0;

    CopyLength = min(ReportLength, ReadLength - Offset);
    RtlCopyMemory(&Address[Offset], Report, CopyLength);
    Irp->IoStatus.Information = Offset + CopyLength;
    Irp->IoStatus.Status = STATUS_SUCCESS;
}

static PIRP
HidClass_RemovePendingRead(
    IN PHIDCLASS_FDO_EXTENSION FDODeviceExtension,
    IN PHIDCLASS_FILEOP_CONTEXT Context)
{
    while (!IsListEmpty(&Context->PendingReads))
    {
        PLIST_ENTRY Entry = RemoveHeadList(&Context->PendingReads);
        PIRP Irp = CONTAINING_RECORD(Entry, IRP, Tail.Overlay.ListEntry);

        FDODeviceExtension->WaitingReads--;
        if (IoSetCancelRoutine(Irp, NULL))
            return Irp;

        InitializeListHead(&Irp->Tail.Overlay.ListEntry);
    }

    return NULL;
}

static VOID
HidClass_CompleteReadList(
    IN PLIST_ENTRY CompleteList)
{
    while (!IsListEmpty(CompleteList))
    {
        PLIST_ENTRY Entry = RemoveHeadList(CompleteList);
        PIRP Irp = CONTAINING_RECORD(Entry, IRP, Tail.Overlay.ListEntry);

        IoCompleteRequest(Irp, IO_NO_INCREMENT);
    }
}

static VOID
NTAPI
HidClass_ReadCancel(
    IN PDEVICE_OBJECT DeviceObject,
    IN PIRP Irp)
{
    PHIDCLASS_PDO_DEVICE_EXTENSION PDODeviceExtension = DeviceObject->DeviceExtension;
    PHIDCLASS_FDO_EXTENSION FDODeviceExtension = PDODeviceExtension->FDODeviceExtension;
    KIRQL OldLevel;

    IoReleaseCancelSpinLock(Irp->CancelIrql);

    KeAcquireSpinLock(&FDODeviceExtension->ReadLock, &OldLevel);
    if (!IsListEmpty(&Irp->Tail.Overlay.ListEntry))
    {
        RemoveEntryList(&Irp->Tail.Overlay.ListEntry);
        FDODeviceExtension->WaitingReads--;
    }
    KeReleaseSpinLock(&FDODeviceExtension->ReadLock, OldLevel);

    Irp->IoStatus.Status = STATUS_CANCELLED;
    Irp->IoStatus.Information = 0;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
}

static VOID
HidClass_StopPendingReads(
    IN PHIDCLASS_FILEOP_CONTEXT Context)
{
    PHIDCLASS_FDO_EXTENSION FDODeviceExtension = Context->DeviceExtension->FDODeviceExtension;
    LIST_ENTRY CompleteList;
    KIRQL OldLevel;
    PIRP Irp;

    InitializeListHead(&CompleteList);

    KeAcquireSpinLock(&FDODeviceExtension->ReadLock, &OldLevel);
    Context->StopInProgress = TRUE;
    Context->ReportQueueCount = 0;
    while ((Irp = HidClass_RemovePendingRead(FDODeviceExtension, Context)) != NULL)
    {
        Irp->IoStatus.Status = STATUS_CANCELLED;
        Irp->IoStatus.Information = 0;
        InsertTailList(&CompleteList, &Irp->Tail.Overlay.ListEntry);
    }
    KeReleaseSpinLock(&FDODeviceExtension->ReadLock, OldLevel);

    HidClass_CompleteReadList(&CompleteList);
}

static VOID
HidClassFDO_RouteReport(
    IN PHIDCLASS_FDO_EXTENSION FDODeviceExtension,
    IN const UCHAR *Report,
    IN ULONG ReportLength,
    IN OUT PLIST_ENTRY CompleteList)
{
    PHIDP_DEVICE_DESC DeviceDescription = &FDODeviceExtension->Common.DeviceDescription;
    PLIST_ENTRY Entry;
    ULONG CollectionNumber = 0;
    ULONG Index;

    if (!ReportLength || !DeviceDescription->ReportIDsLength)
        return;

    if (DeviceDescription->ReportIDs[0].ReportID == 0)
    {
        CollectionNumber = DeviceDescription->ReportIDs[0].CollectionNumber;
    }
    else
    {
        for (Index = 0; Index < DeviceDescription->ReportIDsLength; Index++)
        {
            if (DeviceDescription->ReportIDs[Index].ReportID == Report[0] &&
                DeviceDescription->ReportIDs[Index].InputLength)
            {
                CollectionNumber = DeviceDescription->ReportIDs[Index].CollectionNumber;
                break;
            }
        }
    }

    if (!CollectionNumber)
        return;

    for (Entry = FDODeviceExtension->OpenFileList.Flink;
         Entry != &FDODeviceExtension->OpenFileList;
         Entry = Entry->Flink)
    {
        PHIDCLASS_FILEOP_CONTEXT Context = CONTAINING_RECORD(Entry, HIDCLASS_FILEOP_CONTEXT, FdoListEntry);
        PUCHAR Slot;
        ULONG SlotIndex;
        PIRP Irp;

        if (Context->StopInProgress || Context->DeviceExtension->CollectionNumber != CollectionNumber)
            continue;

        Irp = HidClass_RemovePendingRead(FDODeviceExtension, Context);
        if (Irp)
        {
            HidClass_CopyReport(FDODeviceExtension, Irp, Report, ReportLength);
            InsertTailList(CompleteList, &Irp->Tail.Overlay.ListEntry);
            continue;
        }

        if (!Context->ReportQueue || ReportLength > Context->ReportSlotLength - sizeof(USHORT))
            continue;

        if (Context->ReportQueueCount == HIDCLASS_QUEUED_REPORTS)
        {
            Context->ReportQueueHead = (Context->ReportQueueHead + 1) % HIDCLASS_QUEUED_REPORTS;
            Context->ReportQueueCount--;
        }

        SlotIndex = (Context->ReportQueueHead + Context->ReportQueueCount) % HIDCLASS_QUEUED_REPORTS;
        Slot = Context->ReportQueue + SlotIndex * Context->ReportSlotLength;
        Slot[0] = (UCHAR)ReportLength;
        Slot[1] = (UCHAR)(ReportLength >> 8);
        RtlCopyMemory(Slot + sizeof(USHORT), Report, ReportLength);
        Context->ReportQueueCount++;
    }
}

static IO_COMPLETION_ROUTINE HidClassFDO_ReadCompletion;

static VOID
HidClassFDO_SubmitRead(
    IN PDEVICE_OBJECT FDODeviceObject)
{
    PHIDCLASS_FDO_EXTENSION FDODeviceExtension = FDODeviceObject->DeviceExtension;
    PIRP Irp = FDODeviceExtension->ReadIrp;
    PIO_STACK_LOCATION IoStack;

    IoReuseIrp(Irp, STATUS_SUCCESS);
    Irp->UserBuffer = FDODeviceExtension->ReadBuffer;

    IoStack = IoGetNextIrpStackLocation(Irp);
    IoStack->MajorFunction = IRP_MJ_INTERNAL_DEVICE_CONTROL;
    IoStack->Parameters.DeviceIoControl.IoControlCode = IOCTL_HID_READ_REPORT;
    IoStack->Parameters.DeviceIoControl.OutputBufferLength = FDODeviceExtension->ReadBufferLength;
    IoStack->DeviceObject = FDODeviceObject;

    IoSetCompletionRoutine(Irp, HidClassFDO_ReadCompletion, FDODeviceObject, TRUE, TRUE, TRUE);
    IoSetNextIrpStackLocation(Irp);
    FDODeviceExtension->Common.DriverExtension->MajorFunction[IRP_MJ_INTERNAL_DEVICE_CONTROL](FDODeviceObject, Irp);
}

static VOID
NTAPI
HidClassFDO_ReadDpc(
    IN PKDPC Dpc,
    IN PVOID DeferredContext,
    IN PVOID SystemArgument1,
    IN PVOID SystemArgument2)
{
    PDEVICE_OBJECT FDODeviceObject = DeferredContext;
    PHIDCLASS_FDO_EXTENSION FDODeviceExtension = FDODeviceObject->DeviceExtension;
    KIRQL OldLevel;

    UNREFERENCED_PARAMETER(Dpc);
    UNREFERENCED_PARAMETER(SystemArgument1);
    UNREFERENCED_PARAMETER(SystemArgument2);

    KeAcquireSpinLock(&FDODeviceExtension->ReadLock, &OldLevel);
    if (FDODeviceExtension->ReadStopped || !FDODeviceExtension->WaitingReads)
    {
        FDODeviceExtension->ReadActive = FALSE;
        KeSetEvent(&FDODeviceExtension->ReadIdleEvent, IO_NO_INCREMENT, FALSE);
        KeReleaseSpinLock(&FDODeviceExtension->ReadLock, OldLevel);
        return;
    }
    KeReleaseSpinLock(&FDODeviceExtension->ReadLock, OldLevel);

    HidClassFDO_SubmitRead(FDODeviceObject);
}

static NTSTATUS
NTAPI
HidClassFDO_ReadCompletion(
    IN PDEVICE_OBJECT DeviceObject,
    IN PIRP Irp,
    IN PVOID Context)
{
    PDEVICE_OBJECT FDODeviceObject = Context;
    PHIDCLASS_FDO_EXTENSION FDODeviceExtension = FDODeviceObject->DeviceExtension;
    LIST_ENTRY CompleteList;
    PLIST_ENTRY Entry;
    KIRQL OldLevel;
    NTSTATUS Status = Irp->IoStatus.Status;
    ULONG Length = (ULONG)Irp->IoStatus.Information;
    BOOLEAN Continue;

    UNREFERENCED_PARAMETER(DeviceObject);

    InitializeListHead(&CompleteList);

    KeAcquireSpinLock(&FDODeviceExtension->ReadLock, &OldLevel);
    if (NT_SUCCESS(Status))
    {
        HidClassFDO_RouteReport(FDODeviceExtension,
                                FDODeviceExtension->ReadBuffer,
                                min(Length, FDODeviceExtension->ReadBufferLength),
                                &CompleteList);
    }
    else
    {
        for (Entry = FDODeviceExtension->OpenFileList.Flink;
             Entry != &FDODeviceExtension->OpenFileList;
             Entry = Entry->Flink)
        {
            PHIDCLASS_FILEOP_CONTEXT FileContext = CONTAINING_RECORD(Entry, HIDCLASS_FILEOP_CONTEXT, FdoListEntry);
            PIRP PendingIrp;

            while ((PendingIrp = HidClass_RemovePendingRead(FDODeviceExtension, FileContext)) != NULL)
            {
                PendingIrp->IoStatus.Status = Status;
                PendingIrp->IoStatus.Information = 0;
                InsertTailList(&CompleteList, &PendingIrp->Tail.Overlay.ListEntry);
            }
        }
    }
    KeReleaseSpinLock(&FDODeviceExtension->ReadLock, OldLevel);

    HidClass_CompleteReadList(&CompleteList);

    KeAcquireSpinLock(&FDODeviceExtension->ReadLock, &OldLevel);
    Continue = NT_SUCCESS(Status) && !FDODeviceExtension->ReadStopped && FDODeviceExtension->WaitingReads;
    if (!Continue)
    {
        FDODeviceExtension->ReadActive = FALSE;
        KeSetEvent(&FDODeviceExtension->ReadIdleEvent, IO_NO_INCREMENT, FALSE);
    }
    KeReleaseSpinLock(&FDODeviceExtension->ReadLock, OldLevel);

    if (Continue)
        KeInsertQueueDpc(&FDODeviceExtension->ReadDpc, NULL, NULL);

    return STATUS_MORE_PROCESSING_REQUIRED;
}

VOID
HidClassFDO_InitializeRead(
    IN PDEVICE_OBJECT FDODeviceObject)
{
    PHIDCLASS_FDO_EXTENSION FDODeviceExtension = FDODeviceObject->DeviceExtension;

    KeInitializeSpinLock(&FDODeviceExtension->ReadLock);
    InitializeListHead(&FDODeviceExtension->OpenFileList);
    KeInitializeEvent(&FDODeviceExtension->ReadIdleEvent, NotificationEvent, TRUE);
    KeInitializeDpc(&FDODeviceExtension->ReadDpc, HidClassFDO_ReadDpc, FDODeviceObject);
}

VOID
HidClassFDO_StopRead(
    IN PDEVICE_OBJECT FDODeviceObject,
    IN BOOLEAN Wait)
{
    PHIDCLASS_FDO_EXTENSION FDODeviceExtension = FDODeviceObject->DeviceExtension;
    LIST_ENTRY CompleteList;
    PLIST_ENTRY Entry;
    KIRQL OldLevel;
    BOOLEAN Active;

    InitializeListHead(&CompleteList);

    KeAcquireSpinLock(&FDODeviceExtension->ReadLock, &OldLevel);
    FDODeviceExtension->ReadStopped = TRUE;
    Active = FDODeviceExtension->ReadActive;
    for (Entry = FDODeviceExtension->OpenFileList.Flink;
         Entry != &FDODeviceExtension->OpenFileList;
         Entry = Entry->Flink)
    {
        PHIDCLASS_FILEOP_CONTEXT FileContext = CONTAINING_RECORD(Entry, HIDCLASS_FILEOP_CONTEXT, FdoListEntry);
        PIRP PendingIrp;

        while ((PendingIrp = HidClass_RemovePendingRead(FDODeviceExtension, FileContext)) != NULL)
        {
            PendingIrp->IoStatus.Status = STATUS_DEVICE_NOT_CONNECTED;
            PendingIrp->IoStatus.Information = 0;
            InsertTailList(&CompleteList, &PendingIrp->Tail.Overlay.ListEntry);
        }
    }
    KeReleaseSpinLock(&FDODeviceExtension->ReadLock, OldLevel);

    HidClass_CompleteReadList(&CompleteList);

    if (Active && FDODeviceExtension->ReadIrp)
        IoCancelIrp(FDODeviceExtension->ReadIrp);
    if (!Wait)
        return;

    KeWaitForSingleObject(&FDODeviceExtension->ReadIdleEvent, Executive, KernelMode, FALSE, NULL);
    KeFlushQueuedDpcs();

    if (FDODeviceExtension->ReadIrp)
    {
        IoFreeIrp(FDODeviceExtension->ReadIrp);
        FDODeviceExtension->ReadIrp = NULL;
    }

    if (FDODeviceExtension->ReadBuffer)
    {
        ExFreePoolWithTag(FDODeviceExtension->ReadBuffer, HIDCLASS_TAG);
        FDODeviceExtension->ReadBuffer = NULL;
    }
}

NTSTATUS
HidClassFDO_StartRead(
    IN PDEVICE_OBJECT FDODeviceObject)
{
    PHIDCLASS_FDO_EXTENSION FDODeviceExtension = FDODeviceObject->DeviceExtension;
    PHIDP_DEVICE_DESC DeviceDescription = &FDODeviceExtension->Common.DeviceDescription;
    ULONG Length = 0;
    ULONG Index;
    KIRQL OldLevel;

    if (FDODeviceExtension->ReadIrp)
        return STATUS_SUCCESS;

    for (Index = 0; Index < DeviceDescription->ReportIDsLength; Index++)
        Length = max(Length, DeviceDescription->ReportIDs[Index].InputLength);

    if (!Length)
        return STATUS_SUCCESS;

    FDODeviceExtension->ReadBuffer = ExAllocatePoolWithTag(NonPagedPool, Length, HIDCLASS_TAG);
    if (!FDODeviceExtension->ReadBuffer)
        return STATUS_INSUFFICIENT_RESOURCES;

    FDODeviceExtension->ReadIrp = IoAllocateIrp(FDODeviceObject->StackSize, FALSE);
    if (!FDODeviceExtension->ReadIrp)
    {
        ExFreePoolWithTag(FDODeviceExtension->ReadBuffer, HIDCLASS_TAG);
        FDODeviceExtension->ReadBuffer = NULL;
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    KeAcquireSpinLock(&FDODeviceExtension->ReadLock, &OldLevel);
    FDODeviceExtension->ReadBufferLength = Length;
    FDODeviceExtension->ReadStopped = FALSE;
    KeReleaseSpinLock(&FDODeviceExtension->ReadLock, OldLevel);
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
HidClass_Close(
    IN PDEVICE_OBJECT DeviceObject,
    IN PIRP Irp)
{
    PIO_STACK_LOCATION IoStack;
    PHIDCLASS_COMMON_DEVICE_EXTENSION CommonDeviceExtension;
    PHIDCLASS_FDO_EXTENSION FDODeviceExtension;
    PHIDCLASS_FILEOP_CONTEXT IrpContext;
    KIRQL OldLevel;

    CommonDeviceExtension = DeviceObject->DeviceExtension;
    if (CommonDeviceExtension->IsFDO)
    {
        Irp->IoStatus.Status = STATUS_INVALID_PARAMETER_1;
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
        return STATUS_INVALID_PARAMETER_1;
    }

    IoStack = IoGetCurrentIrpStackLocation(Irp);
    ASSERT(IoStack->FileObject);
    ASSERT(IoStack->FileObject->FsContext);

    IrpContext = IoStack->FileObject->FsContext;
    ASSERT(IrpContext);

    HidClass_StopPendingReads(IrpContext);

    FDODeviceExtension = IrpContext->DeviceExtension->FDODeviceExtension;
    KeAcquireSpinLock(&FDODeviceExtension->ReadLock, &OldLevel);
    RemoveEntryList(&IrpContext->FdoListEntry);
    KeReleaseSpinLock(&FDODeviceExtension->ReadLock, OldLevel);

    IoStack->FileObject->FsContext = NULL;

    if (IrpContext->ReportQueue)
        ExFreePoolWithTag(IrpContext->ReportQueue, HIDCLASS_TAG);
    ExFreePoolWithTag(IrpContext, HIDCLASS_TAG);

    Irp->IoStatus.Status = STATUS_SUCCESS;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
HidClass_Cleanup(
    IN PDEVICE_OBJECT DeviceObject,
    IN PIRP Irp)
{
    PIO_STACK_LOCATION IoStack;
    PHIDCLASS_COMMON_DEVICE_EXTENSION CommonDeviceExtension;
    PHIDCLASS_FILEOP_CONTEXT IrpContext;

    CommonDeviceExtension = DeviceObject->DeviceExtension;
    if (CommonDeviceExtension->IsFDO)
    {
        Irp->IoStatus.Status = STATUS_INVALID_PARAMETER_1;
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
        return STATUS_INVALID_PARAMETER_1;
    }

    IoStack = IoGetCurrentIrpStackLocation(Irp);
    IrpContext = IoStack->FileObject ? IoStack->FileObject->FsContext : NULL;

    if (IrpContext)
        HidClass_StopPendingReads(IrpContext);

    Irp->IoStatus.Status = STATUS_SUCCESS;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
HidClass_Read(
    IN PDEVICE_OBJECT DeviceObject,
    IN PIRP Irp)
{
    PIO_STACK_LOCATION IoStack;
    PHIDCLASS_FILEOP_CONTEXT Context;
    PHIDCLASS_COMMON_DEVICE_EXTENSION CommonDeviceExtension;
    PHIDCLASS_FDO_EXTENSION FDODeviceExtension;
    PDEVICE_OBJECT FDODeviceObject;
    KIRQL OldLevel;
    NTSTATUS Status;
    BOOLEAN Start = FALSE;

    IoStack = IoGetCurrentIrpStackLocation(Irp);

    CommonDeviceExtension = DeviceObject->DeviceExtension;
    ASSERT(CommonDeviceExtension->IsFDO == FALSE);
    ASSERT(IoStack->FileObject);
    ASSERT(IoStack->FileObject->FsContext);

    Context = IoStack->FileObject->FsContext;
    ASSERT(Context);
    ASSERT(Context->DeviceExtension);

    //
    // Polled HID miniports are not implemented here. Do not assert in
    // checked builds; fail reads explicitly until a real poll path exists.
    //
    if (Context->DeviceExtension->Common.DriverExtension->DevicesArePolled)
    {
        DPRINT1("[HIDCLASS] ReadDispatch: polled device not supported via IRP path\n");
        Irp->IoStatus.Status = STATUS_NOT_SUPPORTED;
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
        return STATUS_NOT_SUPPORTED;
    }

    FDODeviceObject = Context->DeviceExtension->FDODeviceObject;
    FDODeviceExtension = Context->DeviceExtension->FDODeviceExtension;

    KeAcquireSpinLock(&FDODeviceExtension->ReadLock, &OldLevel);

    if (Context->StopInProgress || FDODeviceExtension->ReadStopped)
    {
        KeReleaseSpinLock(&FDODeviceExtension->ReadLock, OldLevel);
        Irp->IoStatus.Status = STATUS_CANCELLED;
        Irp->IoStatus.Information = 0;
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
        return STATUS_CANCELLED;
    }

    if (Context->ReportQueueCount)
    {
        PUCHAR Slot = Context->ReportQueue + Context->ReportQueueHead * Context->ReportSlotLength;

        HidClass_CopyReport(FDODeviceExtension, Irp, Slot + sizeof(USHORT), Slot[0] | ((ULONG)Slot[1] << 8));
        Context->ReportQueueHead = (Context->ReportQueueHead + 1) % HIDCLASS_QUEUED_REPORTS;
        Context->ReportQueueCount--;
        KeReleaseSpinLock(&FDODeviceExtension->ReadLock, OldLevel);

        Status = Irp->IoStatus.Status;
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
        return Status;
    }

    IoMarkIrpPending(Irp);
    InsertTailList(&Context->PendingReads, &Irp->Tail.Overlay.ListEntry);
    FDODeviceExtension->WaitingReads++;
    IoSetCancelRoutine(Irp, HidClass_ReadCancel);
    if (Irp->Cancel && IoSetCancelRoutine(Irp, NULL))
    {
        RemoveEntryList(&Irp->Tail.Overlay.ListEntry);
        FDODeviceExtension->WaitingReads--;
        KeReleaseSpinLock(&FDODeviceExtension->ReadLock, OldLevel);

        Irp->IoStatus.Status = STATUS_CANCELLED;
        Irp->IoStatus.Information = 0;
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
        return STATUS_PENDING;
    }

    if (!FDODeviceExtension->ReadActive && FDODeviceExtension->ReadIrp)
    {
        FDODeviceExtension->ReadActive = TRUE;
        KeClearEvent(&FDODeviceExtension->ReadIdleEvent);
        Start = TRUE;
    }

    KeReleaseSpinLock(&FDODeviceExtension->ReadLock, OldLevel);

    if (Start)
        HidClassFDO_SubmitRead(FDODeviceObject);

    return STATUS_PENDING;
}

typedef struct _HIDCLASS_SYNC_IRP_CONTEXT
{
    KEVENT Event;
    BOOLEAN Completed;
} HIDCLASS_SYNC_IRP_CONTEXT, *PHIDCLASS_SYNC_IRP_CONTEXT;

static NTSTATUS
NTAPI
HidClass_SyncIrpCompletion(
    IN PDEVICE_OBJECT DeviceObject,
    IN PIRP Irp,
    IN PVOID Context)
{
    PHIDCLASS_SYNC_IRP_CONTEXT SyncContext = Context;

    UNREFERENCED_PARAMETER(DeviceObject);
    UNREFERENCED_PARAMETER(Irp);

    SyncContext->Completed = TRUE;
    KeSetEvent(&SyncContext->Event, IO_NO_INCREMENT, FALSE);

    return STATUS_MORE_PROCESSING_REQUIRED;
}

static NTSTATUS
HidClass_DispatchMiniDriverRequest(
    IN PHIDCLASS_PDO_DEVICE_EXTENSION PDODeviceExtension,
    IN ULONG IoControlCode,
    IN PHID_XFER_PACKET XferPacket)
{
    PDEVICE_OBJECT FDODeviceObject;
    PHIDCLASS_COMMON_DEVICE_EXTENSION FDOCommonExtension;
    HIDCLASS_SYNC_IRP_CONTEXT SyncContext;
    PIO_STACK_LOCATION IoStack;
    PIRP SubIrp;
    NTSTATUS Status;

    ASSERT(PDODeviceExtension);
    ASSERT(PDODeviceExtension->Common.IsFDO == FALSE);
    ASSERT(PDODeviceExtension->FDODeviceObject);

    FDODeviceObject = PDODeviceExtension->FDODeviceObject;
    FDOCommonExtension = FDODeviceObject->DeviceExtension;

    ASSERT(FDOCommonExtension->IsFDO);
    ASSERT(FDOCommonExtension->DriverExtension);
    ASSERT(FDOCommonExtension->DriverExtension->MajorFunction[IRP_MJ_INTERNAL_DEVICE_CONTROL]);

    SubIrp = IoAllocateIrp(FDODeviceObject->StackSize, FALSE);
    if (!SubIrp)
        return STATUS_NO_MEMORY;

    RtlZeroMemory(&SyncContext, sizeof(SyncContext));
    KeInitializeEvent(&SyncContext.Event, NotificationEvent, FALSE);

    SubIrp->UserBuffer = XferPacket;

    IoStack = IoGetNextIrpStackLocation(SubIrp);
    RtlZeroMemory(IoStack, sizeof(*IoStack));
    IoStack->MajorFunction = IRP_MJ_INTERNAL_DEVICE_CONTROL;
    IoStack->Parameters.DeviceIoControl.IoControlCode = IoControlCode;
    IoStack->Parameters.DeviceIoControl.InputBufferLength = XferPacket->reportBufferLen;
    IoStack->Parameters.DeviceIoControl.OutputBufferLength = XferPacket->reportBufferLen;
    IoStack->DeviceObject = FDODeviceObject;

    IoSetCompletionRoutine(SubIrp,
                           HidClass_SyncIrpCompletion,
                           &SyncContext,
                           TRUE,
                           TRUE,
                           TRUE);

    IoSetNextIrpStackLocation(SubIrp);
    Status = FDOCommonExtension->DriverExtension->MajorFunction[IRP_MJ_INTERNAL_DEVICE_CONTROL](FDODeviceObject, SubIrp);
    if (Status == STATUS_PENDING)
    {
        KeWaitForSingleObject(&SyncContext.Event, Executive, KernelMode, FALSE, NULL);
    }

    if (SyncContext.Completed)
        Status = SubIrp->IoStatus.Status;

    IoFreeIrp(SubIrp);
    return Status;
}

static NTSTATUS
HidClass_DispatchMiniDriverStringRequest(
    IN PHIDCLASS_PDO_DEVICE_EXTENSION PDODeviceExtension,
    IN ULONG IoControlCode,
    IN ULONG StringId,
    OUT PVOID StringBuffer,
    IN ULONG StringBufferLength,
    OUT PULONG_PTR Information)
{
    PDEVICE_OBJECT FDODeviceObject;
    PHIDCLASS_COMMON_DEVICE_EXTENSION FDOCommonExtension;
    HIDCLASS_SYNC_IRP_CONTEXT SyncContext;
    PIO_STACK_LOCATION IoStack;
    PIRP SubIrp;
    NTSTATUS Status;

    ASSERT(PDODeviceExtension);
    ASSERT(PDODeviceExtension->Common.IsFDO == FALSE);
    ASSERT(PDODeviceExtension->FDODeviceObject);

    if (Information)
        *Information = 0;

    FDODeviceObject = PDODeviceExtension->FDODeviceObject;
    FDOCommonExtension = FDODeviceObject->DeviceExtension;

    ASSERT(FDOCommonExtension->IsFDO);
    ASSERT(FDOCommonExtension->DriverExtension);
    ASSERT(FDOCommonExtension->DriverExtension->MajorFunction[IRP_MJ_INTERNAL_DEVICE_CONTROL]);

    SubIrp = IoAllocateIrp(FDODeviceObject->StackSize, FALSE);
    if (!SubIrp)
        return STATUS_NO_MEMORY;

    RtlZeroMemory(&SyncContext, sizeof(SyncContext));
    KeInitializeEvent(&SyncContext.Event, NotificationEvent, FALSE);

    SubIrp->UserBuffer = StringBuffer;

    IoStack = IoGetNextIrpStackLocation(SubIrp);
    RtlZeroMemory(IoStack, sizeof(*IoStack));
    IoStack->MajorFunction = IRP_MJ_INTERNAL_DEVICE_CONTROL;
    IoStack->Parameters.DeviceIoControl.IoControlCode = IoControlCode;
    IoStack->Parameters.DeviceIoControl.InputBufferLength = sizeof(ULONG);
    IoStack->Parameters.DeviceIoControl.OutputBufferLength = StringBufferLength;
    IoStack->Parameters.DeviceIoControl.Type3InputBuffer = UlongToPtr(StringId);
    IoStack->DeviceObject = FDODeviceObject;

    IoSetCompletionRoutine(SubIrp,
                           HidClass_SyncIrpCompletion,
                           &SyncContext,
                           TRUE,
                           TRUE,
                           TRUE);

    IoSetNextIrpStackLocation(SubIrp);
    Status = FDOCommonExtension->DriverExtension->MajorFunction[IRP_MJ_INTERNAL_DEVICE_CONTROL](FDODeviceObject, SubIrp);
    if (Status == STATUS_PENDING)
    {
        KeWaitForSingleObject(&SyncContext.Event, Executive, KernelMode, FALSE, NULL);
    }

    if (SyncContext.Completed)
    {
        Status = SubIrp->IoStatus.Status;
        if (Information)
            *Information = SubIrp->IoStatus.Information;
    }

    IoFreeIrp(SubIrp);
    return Status;
}

NTSTATUS
NTAPI
HidClass_Write(
    IN PDEVICE_OBJECT DeviceObject,
    IN PIRP Irp)
{
    PIO_STACK_LOCATION IoStack;
    PHIDCLASS_COMMON_DEVICE_EXTENSION CommonDeviceExtension;
    HID_XFER_PACKET XferPacket;
    NTSTATUS Status;
    ULONG Length;
    PHIDCLASS_PDO_DEVICE_EXTENSION PDODeviceExtension;

    IoStack = IoGetCurrentIrpStackLocation(Irp);
    Length = IoStack->Parameters.Write.Length;
    if (Length < 1)
    {
        Irp->IoStatus.Status = STATUS_INVALID_PARAMETER;
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
        return STATUS_INVALID_PARAMETER;
    }

    RtlZeroMemory(&XferPacket, sizeof(XferPacket));
    XferPacket.reportBufferLen = Length;
    XferPacket.reportBuffer = Irp->UserBuffer;
    XferPacket.reportId = XferPacket.reportBuffer[0];

    CommonDeviceExtension = DeviceObject->DeviceExtension;
    if (CommonDeviceExtension->IsFDO)
    {
        Irp->IoStatus.Status = STATUS_INVALID_PARAMETER_1;
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
        return STATUS_INVALID_PARAMETER_1;
    }

    PDODeviceExtension = DeviceObject->DeviceExtension;
    Status = HidClass_DispatchMiniDriverRequest(PDODeviceExtension,
                                                IOCTL_HID_WRITE_REPORT,
                                                &XferPacket);
    Irp->IoStatus.Status = Status;
    if (NT_SUCCESS(Status))
        Irp->IoStatus.Information = XferPacket.reportBufferLen;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return Status;
}

NTSTATUS
NTAPI
HidClass_DeviceControl(
    IN PDEVICE_OBJECT DeviceObject,
    IN PIRP Irp)
{
    PIO_STACK_LOCATION IoStack;
    PHIDCLASS_COMMON_DEVICE_EXTENSION CommonDeviceExtension;
    PHID_COLLECTION_INFORMATION CollectionInformation;
    PHIDP_COLLECTION_DESC CollectionDescription;
    PHIDCLASS_PDO_DEVICE_EXTENSION PDODeviceExtension;
    NTSTATUS Status;

    //
    // get device extension
    //
    CommonDeviceExtension = DeviceObject->DeviceExtension;

    //
    // only PDO are supported
    //
    if (CommonDeviceExtension->IsFDO)
    {
        //
        // invalid request
        //
        DPRINT1("[HIDCLASS] DeviceControl Irp for FDO arrived\n");
        Irp->IoStatus.Status = STATUS_INVALID_PARAMETER_1;
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
        return STATUS_INVALID_PARAMETER_1;
    }

    ASSERT(CommonDeviceExtension->IsFDO == FALSE);

    //
    // get pdo device extension
    //
    PDODeviceExtension = DeviceObject->DeviceExtension;

    //
    // get stack location
    //
    IoStack = IoGetCurrentIrpStackLocation(Irp);

    switch (IoStack->Parameters.DeviceIoControl.IoControlCode)
    {
        case IOCTL_HID_GET_COLLECTION_INFORMATION:
        {
            ULONG CompositeLength;

            //
            // check if output buffer is big enough
            //
            if (IoStack->Parameters.DeviceIoControl.OutputBufferLength < sizeof(HID_COLLECTION_INFORMATION))
            {
                //
                // invalid buffer size
                //
                Irp->IoStatus.Status = STATUS_INVALID_BUFFER_SIZE;
                IoCompleteRequest(Irp, IO_NO_INCREMENT);
                return STATUS_INVALID_BUFFER_SIZE;
            }

            //
            // get output buffer
            //
            CollectionInformation = Irp->AssociatedIrp.SystemBuffer;
            ASSERT(CollectionInformation);

            //
            // get collection description
            //
            CollectionDescription = HidClassPDO_GetCollectionDescription(&CommonDeviceExtension->DeviceDescription,
                                                                         PDODeviceExtension->CollectionNumber);
            ASSERT(CollectionDescription);

            //
            // init result buffer
            //
            CollectionInformation->DescriptorSize = CollectionDescription->PreparsedDataLength;
            /* Kernel HID clients pass this buffer directly to HidP_* routines,
             * so they also need the private parser context after the public KDR. */
            if (Irp->RequestorMode == KernelMode && HidP_GetCompositePreparsedDataSize(CollectionDescription->PreparsedData, CollectionDescription->PreparsedDataLength, &CompositeLength))
                CollectionInformation->DescriptorSize = CompositeLength;
            CollectionInformation->Polled = CommonDeviceExtension->DriverExtension->DevicesArePolled;
            CollectionInformation->VendorID = CommonDeviceExtension->Attributes.VendorID;
            CollectionInformation->ProductID = CommonDeviceExtension->Attributes.ProductID;
            CollectionInformation->VersionNumber = CommonDeviceExtension->Attributes.VersionNumber;

            //
            // complete request
            //
            Irp->IoStatus.Information = sizeof(HID_COLLECTION_INFORMATION);
            Irp->IoStatus.Status = STATUS_SUCCESS;
            IoCompleteRequest(Irp, IO_NO_INCREMENT);
            return STATUS_SUCCESS;
        }
        case IOCTL_HID_GET_COLLECTION_DESCRIPTOR:
        {
            ULONG CopyLength;
            ULONG CompositeLength;

            //
            // get collection description
            //
            CollectionDescription = HidClassPDO_GetCollectionDescription(&CommonDeviceExtension->DeviceDescription,
                                                                         PDODeviceExtension->CollectionNumber);
            ASSERT(CollectionDescription);

            CopyLength = CollectionDescription->PreparsedDataLength;
            if (HidP_GetCompositePreparsedDataSize(CollectionDescription->PreparsedData, CollectionDescription->PreparsedDataLength, &CompositeLength) && IoStack->Parameters.DeviceIoControl.OutputBufferLength >= CompositeLength)
                CopyLength = CompositeLength;

            //
            // check if output buffer is big enough
            //
            if (IoStack->Parameters.DeviceIoControl.OutputBufferLength < CollectionDescription->PreparsedDataLength)
            {
                //
                // invalid buffer size
                //
                Irp->IoStatus.Status = STATUS_INVALID_BUFFER_SIZE;
                IoCompleteRequest(Irp, IO_NO_INCREMENT);
                return STATUS_INVALID_BUFFER_SIZE;
            }

            //
            // copy result
            //
            ASSERT(Irp->UserBuffer);
            Status = STATUS_SUCCESS;

            if (Irp->RequestorMode != KernelMode)
            {
                _SEH2_TRY
                {
                    ProbeForWrite(Irp->UserBuffer,
                                  CopyLength,
                                  sizeof(UCHAR));
                }
                _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
                {
                    Status = _SEH2_GetExceptionCode();
                }
                _SEH2_END;
            }

            if (!NT_SUCCESS(Status))
            {
                Irp->IoStatus.Status = Status;
                IoCompleteRequest(Irp, IO_NO_INCREMENT);
                return Status;
            }

            RtlCopyMemory(Irp->UserBuffer,
                          CollectionDescription->PreparsedData,
                          CopyLength);

            //
            // complete request
            //
            Irp->IoStatus.Information = CopyLength;
            Irp->IoStatus.Status = STATUS_SUCCESS;
            IoCompleteRequest(Irp, IO_NO_INCREMENT);
            return STATUS_SUCCESS;
        }
        case IOCTL_HID_GET_FEATURE:
        {
            HID_XFER_PACKET XferPacket;
            NTSTATUS Status;
            PHIDP_REPORT_IDS ReportDescription;
            PUCHAR ReportBuffer;

            ReportBuffer = MmGetSystemAddressForMdlSafe(Irp->MdlAddress, NormalPagePriority);
            if (!ReportBuffer)
            {
                Irp->IoStatus.Status = STATUS_INSUFFICIENT_RESOURCES;
                IoCompleteRequest(Irp, IO_NO_INCREMENT);
                return STATUS_INSUFFICIENT_RESOURCES;
            }

            ReportDescription = HidClassPDO_GetReportDescriptionByReportID(&PDODeviceExtension->Common.DeviceDescription, ReportBuffer[0]);
            if (!ReportDescription || IoStack->Parameters.DeviceIoControl.OutputBufferLength < ReportDescription->FeatureLength)
            {
                Irp->IoStatus.Status = STATUS_INVALID_PARAMETER;
                IoCompleteRequest(Irp, IO_NO_INCREMENT);
                return STATUS_INVALID_PARAMETER;
            }

            RtlZeroMemory(&XferPacket, sizeof(XferPacket));
            XferPacket.reportBufferLen = ReportDescription->FeatureLength;
            XferPacket.reportBuffer = ReportBuffer;
            XferPacket.reportId = ReportBuffer[0];

            Status = HidClass_DispatchMiniDriverRequest(PDODeviceExtension,
                                                        IOCTL_HID_GET_FEATURE,
                                                        &XferPacket);
            Irp->IoStatus.Status = Status;
            if (NT_SUCCESS(Status))
                Irp->IoStatus.Information = XferPacket.reportBufferLen;
            IoCompleteRequest(Irp, IO_NO_INCREMENT);
            return Status;
        }
        case IOCTL_HID_SET_FEATURE:
        {
            HID_XFER_PACKET XferPacket;
            NTSTATUS Status;
            PHIDP_REPORT_IDS ReportDescription;

            if (IoStack->Parameters.DeviceIoControl.InputBufferLength < 1)
            {
                Irp->IoStatus.Status = STATUS_INVALID_PARAMETER;
                IoCompleteRequest(Irp, IO_NO_INCREMENT);
                return STATUS_INVALID_PARAMETER;
            }
            ReportDescription = HidClassPDO_GetReportDescriptionByReportID(&PDODeviceExtension->Common.DeviceDescription, ((PUCHAR)Irp->AssociatedIrp.SystemBuffer)[0]);
            if (!ReportDescription || IoStack->Parameters.DeviceIoControl.InputBufferLength < ReportDescription->FeatureLength)
            {
                Irp->IoStatus.Status = STATUS_INVALID_PARAMETER;
                IoCompleteRequest(Irp, IO_NO_INCREMENT);
                return STATUS_INVALID_PARAMETER;
            }

            RtlZeroMemory(&XferPacket, sizeof(XferPacket));
            XferPacket.reportBufferLen = ReportDescription->FeatureLength;
            XferPacket.reportBuffer = Irp->AssociatedIrp.SystemBuffer;
            XferPacket.reportId = XferPacket.reportBuffer[0];

            Status = HidClass_DispatchMiniDriverRequest(PDODeviceExtension,
                                                        IOCTL_HID_SET_FEATURE,
                                                        &XferPacket);
            Irp->IoStatus.Status = Status;
            if (NT_SUCCESS(Status))
                Irp->IoStatus.Information = XferPacket.reportBufferLen;
            IoCompleteRequest(Irp, IO_NO_INCREMENT);
            return Status;
        }
        case IOCTL_HID_GET_INPUT_REPORT:
        {
            HID_XFER_PACKET XferPacket;
            NTSTATUS Status;
            PHIDP_REPORT_IDS ReportDescription;
            PUCHAR ReportBuffer;

            ReportBuffer = MmGetSystemAddressForMdlSafe(Irp->MdlAddress, NormalPagePriority);
            if (!ReportBuffer)
            {
                Irp->IoStatus.Status = STATUS_INSUFFICIENT_RESOURCES;
                IoCompleteRequest(Irp, IO_NO_INCREMENT);
                return STATUS_INSUFFICIENT_RESOURCES;
            }

            ReportDescription = HidClassPDO_GetReportDescriptionByReportID(&PDODeviceExtension->Common.DeviceDescription, ReportBuffer[0]);
            if (!ReportDescription || IoStack->Parameters.DeviceIoControl.OutputBufferLength < ReportDescription->InputLength)
            {
                Irp->IoStatus.Status = STATUS_INVALID_PARAMETER;
                IoCompleteRequest(Irp, IO_NO_INCREMENT);
                return STATUS_INVALID_PARAMETER;
            }

            RtlZeroMemory(&XferPacket, sizeof(XferPacket));
            XferPacket.reportBufferLen = ReportDescription->InputLength;
            XferPacket.reportBuffer = ReportBuffer;
            XferPacket.reportId = ReportBuffer[0];

            Status = HidClass_DispatchMiniDriverRequest(PDODeviceExtension,
                                                        IOCTL_HID_GET_INPUT_REPORT,
                                                        &XferPacket);
            Irp->IoStatus.Status = Status;
            if (NT_SUCCESS(Status))
                Irp->IoStatus.Information = XferPacket.reportBufferLen;
            IoCompleteRequest(Irp, IO_NO_INCREMENT);
            return Status;
        }
        case IOCTL_HID_SET_OUTPUT_REPORT:
        {
            HID_XFER_PACKET XferPacket;
            NTSTATUS Status;
            PHIDP_REPORT_IDS ReportDescription;

            if (IoStack->Parameters.DeviceIoControl.InputBufferLength < 1)
            {
                Irp->IoStatus.Status = STATUS_INVALID_PARAMETER;
                IoCompleteRequest(Irp, IO_NO_INCREMENT);
                return STATUS_INVALID_PARAMETER;
            }
            ReportDescription = HidClassPDO_GetReportDescriptionByReportID(&PDODeviceExtension->Common.DeviceDescription, ((PUCHAR)Irp->AssociatedIrp.SystemBuffer)[0]);
            if (!ReportDescription || IoStack->Parameters.DeviceIoControl.InputBufferLength < ReportDescription->OutputLength)
            {
                Irp->IoStatus.Status = STATUS_INVALID_PARAMETER;
                IoCompleteRequest(Irp, IO_NO_INCREMENT);
                return STATUS_INVALID_PARAMETER;
            }

            RtlZeroMemory(&XferPacket, sizeof(XferPacket));
            XferPacket.reportBufferLen = ReportDescription->OutputLength;
            XferPacket.reportBuffer = Irp->AssociatedIrp.SystemBuffer;
            XferPacket.reportId = XferPacket.reportBuffer[0];

            Status = HidClass_DispatchMiniDriverRequest(PDODeviceExtension,
                                                        IOCTL_HID_SET_OUTPUT_REPORT,
                                                        &XferPacket);
            Irp->IoStatus.Status = Status;
            if (NT_SUCCESS(Status))
                Irp->IoStatus.Information = XferPacket.reportBufferLen;
            IoCompleteRequest(Irp, IO_NO_INCREMENT);
            return Status;
        }
        case IOCTL_HID_GET_MANUFACTURER_STRING:
        case IOCTL_HID_GET_PRODUCT_STRING:
        case IOCTL_HID_GET_SERIALNUMBER_STRING:
        case IOCTL_HID_GET_INDEXED_STRING:
        {
            ULONG StringId;
            ULONG MiniDriverIoControlCode = IOCTL_HID_GET_STRING;
            PVOID StringBuffer;
            ULONG_PTR Information = 0;

            if (IoStack->Parameters.DeviceIoControl.OutputBufferLength == 0)
            {
                Irp->IoStatus.Status = STATUS_INVALID_BUFFER_SIZE;
                IoCompleteRequest(Irp, IO_NO_INCREMENT);
                return STATUS_INVALID_BUFFER_SIZE;
            }

            StringBuffer = MmGetSystemAddressForMdlSafe(Irp->MdlAddress, NormalPagePriority);
            if (!StringBuffer)
            {
                Irp->IoStatus.Status = STATUS_INSUFFICIENT_RESOURCES;
                IoCompleteRequest(Irp, IO_NO_INCREMENT);
                return STATUS_INSUFFICIENT_RESOURCES;
            }

            switch (IoStack->Parameters.DeviceIoControl.IoControlCode)
            {
                case IOCTL_HID_GET_MANUFACTURER_STRING:
                    StringId = HID_STRING_ID_IMANUFACTURER;
                    break;

                case IOCTL_HID_GET_PRODUCT_STRING:
                    StringId = HID_STRING_ID_IPRODUCT;
                    break;

                case IOCTL_HID_GET_SERIALNUMBER_STRING:
                    StringId = HID_STRING_ID_ISERIALNUMBER;
                    break;

                default:
                    if (IoStack->Parameters.DeviceIoControl.InputBufferLength < sizeof(ULONG) ||
                        !Irp->AssociatedIrp.SystemBuffer)
                    {
                        Irp->IoStatus.Status = STATUS_INVALID_PARAMETER;
                        IoCompleteRequest(Irp, IO_NO_INCREMENT);
                        return STATUS_INVALID_PARAMETER;
                    }

                    StringId = *(PULONG)Irp->AssociatedIrp.SystemBuffer;
                    MiniDriverIoControlCode = IOCTL_HID_GET_INDEXED_STRING;
                    break;
            }

            Status = HidClass_DispatchMiniDriverStringRequest(PDODeviceExtension,
                                                              MiniDriverIoControlCode,
                                                              StringId,
                                                              StringBuffer,
                                                              IoStack->Parameters.DeviceIoControl.OutputBufferLength,
                                                              &Information);
            Irp->IoStatus.Status = Status;
            if (NT_SUCCESS(Status))
                Irp->IoStatus.Information = Information;
            IoCompleteRequest(Irp, IO_NO_INCREMENT);
            return Status;
        }
        default:
        {
            DPRINT1("[HIDCLASS] DeviceControl IoControlCode 0x%x not implemented\n", IoStack->Parameters.DeviceIoControl.IoControlCode);
            Irp->IoStatus.Status = STATUS_NOT_IMPLEMENTED;
            IoCompleteRequest(Irp, IO_NO_INCREMENT);
            return STATUS_NOT_IMPLEMENTED;
        }
    }
}

NTSTATUS
NTAPI
HidClass_InternalDeviceControl(
    IN PDEVICE_OBJECT DeviceObject,
    IN PIRP Irp)
{
    UNIMPLEMENTED;
    ASSERT(FALSE);
    Irp->IoStatus.Status = STATUS_NOT_IMPLEMENTED;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
NTAPI
HidClass_Power(
    IN PDEVICE_OBJECT DeviceObject,
    IN PIRP Irp)
{
    PHIDCLASS_COMMON_DEVICE_EXTENSION CommonDeviceExtension;
    CommonDeviceExtension = DeviceObject->DeviceExtension;

    if (CommonDeviceExtension->IsFDO)
    {
        IoCopyCurrentIrpStackLocationToNext(Irp);
        return HidClassFDO_DispatchRequest(DeviceObject, Irp);
    }
    else
    {
        Irp->IoStatus.Status = STATUS_SUCCESS;
        PoStartNextPowerIrp(Irp);
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
        return STATUS_SUCCESS;
    }
}

NTSTATUS
NTAPI
HidClass_PnP(
    IN PDEVICE_OBJECT DeviceObject,
    IN PIRP Irp)
{
    PHIDCLASS_COMMON_DEVICE_EXTENSION CommonDeviceExtension;

    //
    // get common device extension
    //
    CommonDeviceExtension = DeviceObject->DeviceExtension;

    //
    // check type of device object
    //
    if (CommonDeviceExtension->IsFDO)
    {
        //
        // handle request
        //
        return HidClassFDO_PnP(DeviceObject, Irp);
    }
    else
    {
        //
        // handle request
        //
        return HidClassPDO_PnP(DeviceObject, Irp);
    }
}

NTSTATUS
NTAPI
HidClass_DispatchDefault(
    IN PDEVICE_OBJECT DeviceObject,
    IN PIRP Irp)
{
    PHIDCLASS_COMMON_DEVICE_EXTENSION CommonDeviceExtension;

    //
    // get common device extension
    //
    CommonDeviceExtension = DeviceObject->DeviceExtension;

    //
    // FIXME: support PDO
    //
    ASSERT(CommonDeviceExtension->IsFDO == TRUE);

    //
    // skip current irp stack location
    //
    IoSkipCurrentIrpStackLocation(Irp);

    //
    // dispatch to lower device object
    //
    return IoCallDriver(CommonDeviceExtension->HidDeviceExtension.NextDeviceObject, Irp);
}

NTSTATUS
NTAPI
HidClassDispatch(
    IN PDEVICE_OBJECT DeviceObject,
    IN PIRP Irp)
{
    PIO_STACK_LOCATION IoStack;

    //
    // get current stack location
    //
    IoStack = IoGetCurrentIrpStackLocation(Irp);
    DPRINT("[HIDCLASS] Dispatch Major %x Minor %x\n", IoStack->MajorFunction, IoStack->MinorFunction);

    //
    // dispatch request based on major function
    //
    switch (IoStack->MajorFunction)
    {
        case IRP_MJ_CREATE:
            return HidClass_Create(DeviceObject, Irp);
        case IRP_MJ_CLEANUP:
            return HidClass_Cleanup(DeviceObject, Irp);
        case IRP_MJ_CLOSE:
            return HidClass_Close(DeviceObject, Irp);
        case IRP_MJ_READ:
            return HidClass_Read(DeviceObject, Irp);
        case IRP_MJ_WRITE:
            return HidClass_Write(DeviceObject, Irp);
        case IRP_MJ_DEVICE_CONTROL:
            return HidClass_DeviceControl(DeviceObject, Irp);
        case IRP_MJ_INTERNAL_DEVICE_CONTROL:
           return HidClass_InternalDeviceControl(DeviceObject, Irp);
        case IRP_MJ_POWER:
            return HidClass_Power(DeviceObject, Irp);
        case IRP_MJ_PNP:
            return HidClass_PnP(DeviceObject, Irp);
        default:
            return HidClass_DispatchDefault(DeviceObject, Irp);
    }
}

NTSTATUS
NTAPI
HidRegisterMinidriver(
    IN PHID_MINIDRIVER_REGISTRATION MinidriverRegistration)
{
    NTSTATUS Status;
    PHIDCLASS_DRIVER_EXTENSION DriverExtension;

    /* check if the version matches */
    if (MinidriverRegistration->Revision > HID_REVISION)
    {
        /* revision mismatch */
        ASSERT(FALSE);
        return STATUS_REVISION_MISMATCH;
    }

    /* now allocate the driver object extension */
    Status = IoAllocateDriverObjectExtension(MinidriverRegistration->DriverObject,
                                             ClientIdentificationAddress,
                                             sizeof(HIDCLASS_DRIVER_EXTENSION),
                                             (PVOID *)&DriverExtension);
    if (!NT_SUCCESS(Status))
    {
        /* failed to allocate driver extension */
        ASSERT(FALSE);
        return Status;
    }

    /* zero driver extension */
    RtlZeroMemory(DriverExtension, sizeof(HIDCLASS_DRIVER_EXTENSION));

    /* init driver extension */
    DriverExtension->DriverObject = MinidriverRegistration->DriverObject;
    DriverExtension->DeviceExtensionSize = MinidriverRegistration->DeviceExtensionSize;
    DriverExtension->DevicesArePolled = MinidriverRegistration->DevicesArePolled;
    DriverExtension->AddDevice = MinidriverRegistration->DriverObject->DriverExtension->AddDevice;
    DriverExtension->DriverUnload = MinidriverRegistration->DriverObject->DriverUnload;

    /* copy driver dispatch routines */
    RtlCopyMemory(DriverExtension->MajorFunction,
                  MinidriverRegistration->DriverObject->MajorFunction,
                  sizeof(PDRIVER_DISPATCH) * (IRP_MJ_MAXIMUM_FUNCTION + 1));

    /* initialize lock */
    KeInitializeSpinLock(&DriverExtension->Lock);

    /* now replace dispatch routines */
    DriverExtension->DriverObject->DriverExtension->AddDevice = HidClassAddDevice;
    DriverExtension->DriverObject->DriverUnload = HidClassDriverUnload;
    DriverExtension->DriverObject->MajorFunction[IRP_MJ_CREATE] = HidClassDispatch;
    DriverExtension->DriverObject->MajorFunction[IRP_MJ_CLEANUP] = HidClassDispatch;
    DriverExtension->DriverObject->MajorFunction[IRP_MJ_CLOSE] = HidClassDispatch;
    DriverExtension->DriverObject->MajorFunction[IRP_MJ_READ] = HidClassDispatch;
    DriverExtension->DriverObject->MajorFunction[IRP_MJ_WRITE] = HidClassDispatch;
    DriverExtension->DriverObject->MajorFunction[IRP_MJ_DEVICE_CONTROL] = HidClassDispatch;
    DriverExtension->DriverObject->MajorFunction[IRP_MJ_INTERNAL_DEVICE_CONTROL] = HidClassDispatch;
    DriverExtension->DriverObject->MajorFunction[IRP_MJ_POWER] = HidClassDispatch;
    DriverExtension->DriverObject->MajorFunction[IRP_MJ_PNP] = HidClassDispatch;

    /* done */
    return STATUS_SUCCESS;
}
