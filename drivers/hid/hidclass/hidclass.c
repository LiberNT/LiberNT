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
    Context->ReportQueueCapacity = HIDCLASS_QUEUED_REPORTS;
    InitializeListHead(&Context->PendingReads);

    CollectionDescription = HidClassPDO_GetCollectionDescription(&PDODeviceExtension->Common.DeviceDescription,
                                                                 PDODeviceExtension->CollectionNumber);
    if (CollectionDescription && CollectionDescription->InputLength)
    {
        Context->ReportSlotLength = sizeof(USHORT) + CollectionDescription->InputLength;
        Context->ReportQueue = ExAllocatePoolWithTag(NonPagedPool,
                                                     Context->ReportSlotLength * Context->ReportQueueCapacity,
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

    HidClassFDO_KickRead(PDODeviceExtension->FDODeviceObject);

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
    PHIDCLASS_FILEOP_CONTEXT Context = IoStack->FileObject ? IoStack->FileObject->FsContext : NULL;
    ULONG ReadLength = IoStack->Parameters.Read.Length;
    ULONG Offset = FDODeviceExtension->Common.DeviceDescription.ReportIDs[0].ReportID ? 0 : 1;
    PUCHAR Address = HidClass_GetReadBuffer(Irp);
    PHIDP_COLLECTION_DESC CollectionDescription = NULL;
    ULONG CopyLength, TotalLength;

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

    if (Context)
        CollectionDescription = HidClassPDO_GetCollectionDescription(&FDODeviceExtension->Common.DeviceDescription,
                                                                     Context->DeviceExtension->CollectionNumber);
    TotalLength = ReadLength;
    if (CollectionDescription && CollectionDescription->InputLength && CollectionDescription->InputLength < TotalLength)
        TotalLength = CollectionDescription->InputLength;

    CopyLength = min(ReportLength, TotalLength - Offset);
    RtlCopyMemory(&Address[Offset], Report, CopyLength);
    if (FDODeviceExtension->Common.DriverExtension->DevicesArePolled)
        TotalLength = Offset + CopyLength;
    else if (Offset + CopyLength < TotalLength)
        RtlZeroMemory(&Address[Offset + CopyLength], TotalLength - Offset - CopyLength);
    Irp->IoStatus.Information = TotalLength;
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
    ULONG ExpectedLength = 0;
    ULONG Index;

    if (!ReportLength || !DeviceDescription->ReportIDsLength)
        return;

    if (DeviceDescription->ReportIDs[0].ReportID == 0)
    {
        CollectionNumber = DeviceDescription->ReportIDs[0].CollectionNumber;
        ExpectedLength = DeviceDescription->ReportIDs[0].InputLength;
    }
    else
    {
        for (Index = 0; Index < DeviceDescription->ReportIDsLength; Index++)
        {
            if (DeviceDescription->ReportIDs[Index].ReportID == Report[0] &&
                DeviceDescription->ReportIDs[Index].InputLength)
            {
                CollectionNumber = DeviceDescription->ReportIDs[Index].CollectionNumber;
                ExpectedLength = DeviceDescription->ReportIDs[Index].InputLength;
                break;
            }
        }
    }

    if (!CollectionNumber)
        return;

    if (FDODeviceExtension->Common.DriverExtension->DevicesArePolled)
    {
        for (Entry = FDODeviceExtension->OpenFileList.Flink;
             Entry != &FDODeviceExtension->OpenFileList;
             Entry = Entry->Flink)
        {
            PHIDCLASS_FILEOP_CONTEXT Context = CONTAINING_RECORD(Entry, HIDCLASS_FILEOP_CONTEXT, FdoListEntry);
            PIRP Irp;

            if (Context->StopInProgress || Context->DeviceExtension->CollectionNumber != CollectionNumber)
                continue;

            while ((Irp = HidClass_RemovePendingRead(FDODeviceExtension, Context)) != NULL)
            {
                HidClass_CopyReport(FDODeviceExtension, Irp, Report, ReportLength);
                InsertTailList(CompleteList, &Irp->Tail.Overlay.ListEntry);
            }
        }
        return;
    }

    if (ReportLength < ExpectedLength)
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

        if (Context->ReportQueueCount == Context->ReportQueueCapacity)
        {
            Context->ReportQueueHead = (Context->ReportQueueHead + 1) % Context->ReportQueueCapacity;
            Context->ReportQueueCount--;
        }

        SlotIndex = (Context->ReportQueueHead + Context->ReportQueueCount) % Context->ReportQueueCapacity;
        Slot = Context->ReportQueue + SlotIndex * Context->ReportSlotLength;
        Slot[0] = (UCHAR)ReportLength;
        Slot[1] = (UCHAR)(ReportLength >> 8);
        RtlCopyMemory(Slot + sizeof(USHORT), Report, ReportLength);
        Context->ReportQueueCount++;
    }
}

static IO_COMPLETION_ROUTINE HidClassFDO_ReadCompletion;

static BOOLEAN
HidClassFDO_WantsRead(
    IN PHIDCLASS_FDO_EXTENSION FDODeviceExtension)
{
    if (FDODeviceExtension->ReadStopped)
        return FALSE;
    if (FDODeviceExtension->WaitingReads)
        return TRUE;
    return !FDODeviceExtension->Common.DriverExtension->DevicesArePolled &&
           !IsListEmpty(&FDODeviceExtension->OpenFileList);
}

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
    if (!HidClassFDO_WantsRead(FDODeviceExtension))
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
    Continue = NT_SUCCESS(Status) && HidClassFDO_WantsRead(FDODeviceExtension);
    if (!Continue)
    {
        FDODeviceExtension->ReadActive = FALSE;
        KeSetEvent(&FDODeviceExtension->ReadIdleEvent, IO_NO_INCREMENT, FALSE);
    }
    KeReleaseSpinLock(&FDODeviceExtension->ReadLock, OldLevel);

    if (Continue && FDODeviceExtension->Common.DriverExtension->DevicesArePolled)
    {
        LARGE_INTEGER DueTime;

        DueTime.QuadPart = -(LONGLONG)FDODeviceExtension->PollInterval * 10000;
        KeSetTimer(&FDODeviceExtension->PollTimer, DueTime, &FDODeviceExtension->ReadDpc);
    }
    else if (Continue)
    {
        KeInsertQueueDpc(&FDODeviceExtension->ReadDpc, NULL, NULL);
    }

    return STATUS_MORE_PROCESSING_REQUIRED;
}

VOID
HidClassFDO_InitializeRead(
    IN PDEVICE_OBJECT FDODeviceObject)
{
    PHIDCLASS_FDO_EXTENSION FDODeviceExtension = FDODeviceObject->DeviceExtension;

    KeInitializeSpinLock(&FDODeviceExtension->ReadLock);
    KeInitializeSpinLock(&FDODeviceExtension->WakeLock);
    KeInitializeEvent(&FDODeviceExtension->WakeDone, NotificationEvent, FALSE);
    InitializeListHead(&FDODeviceExtension->OpenFileList);
    KeInitializeEvent(&FDODeviceExtension->ReadIdleEvent, NotificationEvent, TRUE);
    KeInitializeDpc(&FDODeviceExtension->ReadDpc, HidClassFDO_ReadDpc, FDODeviceObject);
    KeInitializeTimer(&FDODeviceExtension->PollTimer);
    FDODeviceExtension->PollInterval = HIDCLASS_DEFAULT_POLL_INTERVAL;
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

    if (KeCancelTimer(&FDODeviceExtension->PollTimer))
    {
        KeAcquireSpinLock(&FDODeviceExtension->ReadLock, &OldLevel);
        FDODeviceExtension->ReadActive = FALSE;
        KeSetEvent(&FDODeviceExtension->ReadIdleEvent, IO_NO_INCREMENT, FALSE);
        KeReleaseSpinLock(&FDODeviceExtension->ReadLock, OldLevel);
    }
    else if (Active && FDODeviceExtension->ReadIrp)
    {
        IoCancelIrp(FDODeviceExtension->ReadIrp);
    }
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

VOID
HidClassFDO_KickRead(
    IN PDEVICE_OBJECT FDODeviceObject)
{
    PHIDCLASS_FDO_EXTENSION FDODeviceExtension = FDODeviceObject->DeviceExtension;
    BOOLEAN Start = FALSE;
    KIRQL OldLevel;

    if (FDODeviceExtension->Common.DriverExtension->DevicesArePolled)
        return;

    KeAcquireSpinLock(&FDODeviceExtension->ReadLock, &OldLevel);
    if (!FDODeviceExtension->ReadActive && FDODeviceExtension->ReadIrp && HidClassFDO_WantsRead(FDODeviceExtension))
    {
        FDODeviceExtension->ReadActive = TRUE;
        KeClearEvent(&FDODeviceExtension->ReadIdleEvent);
        Start = TRUE;
    }
    KeReleaseSpinLock(&FDODeviceExtension->ReadLock, OldLevel);

    if (Start)
        HidClassFDO_SubmitRead(FDODeviceObject);
}

VOID
HidClassPDO_SurpriseRemoval(
    IN PHIDCLASS_PDO_DEVICE_EXTENSION PDODeviceExtension)
{
    PHIDCLASS_FDO_EXTENSION FDODeviceExtension = PDODeviceExtension->FDODeviceExtension;
    LIST_ENTRY CompleteList;
    PLIST_ENTRY Entry;
    KIRQL OldLevel;

    InitializeListHead(&CompleteList);

    KeAcquireSpinLock(&FDODeviceExtension->ReadLock, &OldLevel);
    PDODeviceExtension->Removed = TRUE;
    for (Entry = FDODeviceExtension->OpenFileList.Flink;
         Entry != &FDODeviceExtension->OpenFileList;
         Entry = Entry->Flink)
    {
        PHIDCLASS_FILEOP_CONTEXT FileContext = CONTAINING_RECORD(Entry, HIDCLASS_FILEOP_CONTEXT, FdoListEntry);
        PIRP PendingIrp;

        if (FileContext->DeviceExtension != PDODeviceExtension)
            continue;

        while ((PendingIrp = HidClass_RemovePendingRead(FDODeviceExtension, FileContext)) != NULL)
        {
            PendingIrp->IoStatus.Status = STATUS_DEVICE_NOT_CONNECTED;
            PendingIrp->IoStatus.Information = 0;
            InsertTailList(&CompleteList, &PendingIrp->Tail.Overlay.ListEntry);
        }
    }
    KeReleaseSpinLock(&FDODeviceExtension->ReadLock, OldLevel);

    HidClass_CompleteReadList(&CompleteList);
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
    PHIDP_COLLECTION_DESC CollectionDescription;
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

    CollectionDescription = HidClassPDO_GetCollectionDescription(&Context->DeviceExtension->Common.DeviceDescription,
                                                                 Context->DeviceExtension->CollectionNumber);
    if (!IoStack->Parameters.Read.Length || !CollectionDescription ||
        IoStack->Parameters.Read.Length < CollectionDescription->InputLength)
    {
        Irp->IoStatus.Status = STATUS_INVALID_USER_BUFFER;
        Irp->IoStatus.Information = 0;
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
        return STATUS_INVALID_USER_BUFFER;
    }

    FDODeviceObject = Context->DeviceExtension->FDODeviceObject;
    FDODeviceExtension = Context->DeviceExtension->FDODeviceExtension;

    KeAcquireSpinLock(&FDODeviceExtension->ReadLock, &OldLevel);

    if (Context->DeviceExtension->Removed)
    {
        KeReleaseSpinLock(&FDODeviceExtension->ReadLock, OldLevel);
        Irp->IoStatus.Status = STATUS_DEVICE_NOT_CONNECTED;
        Irp->IoStatus.Information = 0;
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
        return STATUS_DEVICE_NOT_CONNECTED;
    }

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
        Context->ReportQueueHead = (Context->ReportQueueHead + 1) % Context->ReportQueueCapacity;
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

    if (Start && FDODeviceExtension->Common.DriverExtension->DevicesArePolled)
    {
        LARGE_INTEGER DueTime;

        DueTime.QuadPart = -(LONGLONG)FDODeviceExtension->PollInterval * 10000;
        KeSetTimer(&FDODeviceExtension->PollTimer, DueTime, &FDODeviceExtension->ReadDpc);
    }
    else if (Start)
    {
        HidClassFDO_SubmitRead(FDODeviceObject);
    }

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
    IN PHID_XFER_PACKET XferPacket,
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
    if (IoControlCode == IOCTL_HID_GET_FEATURE || IoControlCode == IOCTL_HID_GET_INPUT_REPORT)
        IoStack->Parameters.DeviceIoControl.OutputBufferLength = sizeof(HID_XFER_PACKET);
    else
        IoStack->Parameters.DeviceIoControl.InputBufferLength = sizeof(HID_XFER_PACKET);
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

    *Information = 0;
    if (SyncContext.Completed)
    {
        Status = SubIrp->IoStatus.Status;
        *Information = SubIrp->IoStatus.Information;
    }

    IoFreeIrp(SubIrp);
    return Status;
}

static NTSTATUS
HidClass_DispatchMiniDriverMdlRequest(
    IN PHIDCLASS_PDO_DEVICE_EXTENSION PDODeviceExtension,
    IN ULONG IoControlCode,
    IN PIRP Irp,
    OUT PULONG_PTR Information)
{
    PIO_STACK_LOCATION OriginalStack = IoGetCurrentIrpStackLocation(Irp);
    PDEVICE_OBJECT FDODeviceObject = PDODeviceExtension->FDODeviceObject;
    PHIDCLASS_COMMON_DEVICE_EXTENSION FDOCommonExtension = FDODeviceObject->DeviceExtension;
    HIDCLASS_SYNC_IRP_CONTEXT SyncContext;
    PIO_STACK_LOCATION IoStack;
    PIRP SubIrp;
    NTSTATUS Status;

    *Information = 0;
    if (!Irp->MdlAddress)
        return STATUS_INVALID_USER_BUFFER;

    SubIrp = IoAllocateIrp(FDODeviceObject->StackSize, FALSE);
    if (!SubIrp)
        return STATUS_NO_MEMORY;

    RtlZeroMemory(&SyncContext, sizeof(SyncContext));
    KeInitializeEvent(&SyncContext.Event, NotificationEvent, FALSE);

    SubIrp->MdlAddress = Irp->MdlAddress;
    SubIrp->UserBuffer = MmGetSystemAddressForMdlSafe(Irp->MdlAddress, NormalPagePriority);

    IoStack = IoGetNextIrpStackLocation(SubIrp);
    RtlZeroMemory(IoStack, sizeof(*IoStack));
    IoStack->MajorFunction = IRP_MJ_INTERNAL_DEVICE_CONTROL;
    IoStack->Parameters.DeviceIoControl.IoControlCode = IoControlCode;
    IoStack->Parameters.DeviceIoControl.OutputBufferLength = OriginalStack->Parameters.DeviceIoControl.OutputBufferLength;
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
        *Information = SubIrp->IoStatus.Information;
    }

    SubIrp->MdlAddress = NULL;
    IoFreeIrp(SubIrp);
    return Status;
}

static NTSTATUS
HidClass_TransferReport(
    IN PHIDCLASS_PDO_DEVICE_EXTENSION PDODeviceExtension,
    IN ULONG IoControlCode,
    IN HIDP_REPORT_TYPE ReportType,
    IN PUCHAR Buffer,
    IN ULONG BufferLength,
    IN BOOLEAN WholeBuffer,
    OUT PULONG ReportLength,
    OUT PULONG_PTR Information)
{
    PHIDP_DEVICE_DESC DeviceDescription = &PDODeviceExtension->Common.DeviceDescription;
    PHIDP_COLLECTION_DESC CollectionDescription;
    HID_XFER_PACKET XferPacket;
    BOOLEAN UsesReportIds = FALSE, Found = FALSE;
    ULONG Index, FoundLength = 0;

    *ReportLength = 0;
    *Information = 0;
    if (!Buffer || !BufferLength)
        return STATUS_INVALID_USER_BUFFER;

    CollectionDescription = HidClassPDO_GetCollectionDescription(DeviceDescription, PDODeviceExtension->CollectionNumber);
    if (!CollectionDescription)
        return STATUS_INVALID_PARAMETER;

    if (ReportType == HidP_Input)
        *ReportLength = CollectionDescription->InputLength;
    else if (ReportType == HidP_Output)
        *ReportLength = CollectionDescription->OutputLength;
    else
        *ReportLength = CollectionDescription->FeatureLength;
    if (!*ReportLength || BufferLength < *ReportLength)
        return STATUS_INVALID_PARAMETER;

    for (Index = 0; Index < DeviceDescription->ReportIDsLength; Index++)
    {
        PHIDP_REPORT_IDS Report = &DeviceDescription->ReportIDs[Index];
        USHORT Length = ReportType == HidP_Input ? Report->InputLength :
                        ReportType == HidP_Output ? Report->OutputLength : Report->FeatureLength;

        if (Report->CollectionNumber != PDODeviceExtension->CollectionNumber)
            continue;
        if (Report->ReportID)
            UsesReportIds = TRUE;
        if (Report->ReportID == Buffer[0] && Length)
        {
            Found = TRUE;
            FoundLength = Length;
        }
    }
    if (UsesReportIds && !Found)
        return STATUS_INVALID_PARAMETER;

    RtlZeroMemory(&XferPacket, sizeof(XferPacket));
    XferPacket.reportBuffer = Buffer;
    XferPacket.reportBufferLen = WholeBuffer ? BufferLength : *ReportLength;
    XferPacket.reportId = Buffer[0];
    if (!UsesReportIds)
    {
        XferPacket.reportId = 0;
        XferPacket.reportBuffer++;
        XferPacket.reportBufferLen--;
    }
    else if (!WholeBuffer && FoundLength)
    {
        XferPacket.reportBufferLen = FoundLength;
    }

    return HidClass_DispatchMiniDriverRequest(PDODeviceExtension, IoControlCode, &XferPacket, Information);
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
    NTSTATUS Status;
    ULONG Length, ReportLength;
    ULONG_PTR Information;
    PHIDCLASS_PDO_DEVICE_EXTENSION PDODeviceExtension;

    IoStack = IoGetCurrentIrpStackLocation(Irp);
    Length = IoStack->Parameters.Write.Length;

    CommonDeviceExtension = DeviceObject->DeviceExtension;
    if (CommonDeviceExtension->IsFDO)
    {
        Irp->IoStatus.Status = STATUS_INVALID_PARAMETER_1;
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
        return STATUS_INVALID_PARAMETER_1;
    }

    PDODeviceExtension = DeviceObject->DeviceExtension;
    Status = HidClass_TransferReport(PDODeviceExtension,
                                     IOCTL_HID_WRITE_REPORT,
                                     HidP_Output,
                                     Irp->AssociatedIrp.SystemBuffer,
                                     Length,
                                     FALSE,
                                     &ReportLength,
                                     &Information);
    Irp->IoStatus.Status = Status;
    Irp->IoStatus.Information = NT_SUCCESS(Status) ? ReportLength : 0;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return Status;
}

static
NTSTATUS
HidClass_SetReportQueueCapacity(
    IN PHIDCLASS_FILEOP_CONTEXT Context,
    IN ULONG Capacity)
{
    PHIDCLASS_FDO_EXTENSION FDODeviceExtension = Context->DeviceExtension->FDODeviceExtension;
    PUCHAR NewQueue = NULL, OldQueue;
    ULONG Index, Count, Skip;
    KIRQL OldLevel;

    if (Context->ReportSlotLength)
    {
        NewQueue = ExAllocatePoolWithTag(NonPagedPool, Context->ReportSlotLength * Capacity, HIDCLASS_TAG);
        if (!NewQueue)
            return STATUS_INSUFFICIENT_RESOURCES;
    }

    KeAcquireSpinLock(&FDODeviceExtension->ReadLock, &OldLevel);
    OldQueue = Context->ReportQueue;
    Count = OldQueue ? min(Context->ReportQueueCount, Capacity) : 0;
    Skip = Context->ReportQueueCount - Count;
    for (Index = 0; Index < Count; Index++)
    {
        ULONG Slot = (Context->ReportQueueHead + Skip + Index) % Context->ReportQueueCapacity;
        RtlCopyMemory(NewQueue + Index * Context->ReportSlotLength,
                      OldQueue + Slot * Context->ReportSlotLength,
                      Context->ReportSlotLength);
    }
    Context->ReportQueue = NewQueue;
    Context->ReportQueueHead = 0;
    Context->ReportQueueCount = Count;
    Context->ReportQueueCapacity = Capacity;
    KeReleaseSpinLock(&FDODeviceExtension->ReadLock, OldLevel);

    if (OldQueue)
        ExFreePoolWithTag(OldQueue, HIDCLASS_TAG);
    return STATUS_SUCCESS;
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
        case IOCTL_HID_GET_POLL_FREQUENCY_MSEC:
        {
            if (IoStack->Parameters.DeviceIoControl.OutputBufferLength < sizeof(ULONG))
            {
                Irp->IoStatus.Status = STATUS_BUFFER_TOO_SMALL;
                IoCompleteRequest(Irp, IO_NO_INCREMENT);
                return STATUS_BUFFER_TOO_SMALL;
            }

            *(PULONG)Irp->AssociatedIrp.SystemBuffer =
                CommonDeviceExtension->DriverExtension->DevicesArePolled ? PDODeviceExtension->FDODeviceExtension->PollInterval : 0;
            Irp->IoStatus.Information = sizeof(ULONG);
            Irp->IoStatus.Status = STATUS_SUCCESS;
            IoCompleteRequest(Irp, IO_NO_INCREMENT);
            return STATUS_SUCCESS;
        }
        case IOCTL_HID_SET_POLL_FREQUENCY_MSEC:
        {
            ULONG Interval;

            if (IoStack->Parameters.DeviceIoControl.InputBufferLength < sizeof(ULONG))
            {
                Irp->IoStatus.Status = STATUS_BUFFER_TOO_SMALL;
                IoCompleteRequest(Irp, IO_NO_INCREMENT);
                return STATUS_BUFFER_TOO_SMALL;
            }

            Interval = *(PULONG)Irp->AssociatedIrp.SystemBuffer;
            if (Interval && CommonDeviceExtension->DriverExtension->DevicesArePolled)
                PDODeviceExtension->FDODeviceExtension->PollInterval = min(Interval, HIDCLASS_MAX_POLL_INTERVAL);
            Irp->IoStatus.Information = 0;
            Irp->IoStatus.Status = STATUS_SUCCESS;
            IoCompleteRequest(Irp, IO_NO_INCREMENT);
            return STATUS_SUCCESS;
        }
        case IOCTL_HID_GET_COLLECTION_INFORMATION:
        {
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
        case IOCTL_GET_NUM_DEVICE_INPUT_BUFFERS:
        {
            PHIDCLASS_FILEOP_CONTEXT Context = IoStack->FileObject ? IoStack->FileObject->FsContext : NULL;

            if (!Context || IoStack->Parameters.DeviceIoControl.OutputBufferLength < sizeof(ULONG))
            {
                Irp->IoStatus.Status = STATUS_INVALID_PARAMETER;
                IoCompleteRequest(Irp, IO_NO_INCREMENT);
                return STATUS_INVALID_PARAMETER;
            }

            *(PULONG)Irp->AssociatedIrp.SystemBuffer = Context->ReportQueueCapacity;
            Irp->IoStatus.Information = sizeof(ULONG);
            Irp->IoStatus.Status = STATUS_SUCCESS;
            IoCompleteRequest(Irp, IO_NO_INCREMENT);
            return STATUS_SUCCESS;
        }
        case IOCTL_SET_NUM_DEVICE_INPUT_BUFFERS:
        {
            PHIDCLASS_FILEOP_CONTEXT Context = IoStack->FileObject ? IoStack->FileObject->FsContext : NULL;
            ULONG Capacity;

            if (!Context || IoStack->Parameters.DeviceIoControl.InputBufferLength < sizeof(ULONG))
            {
                Irp->IoStatus.Status = STATUS_INVALID_PARAMETER;
                IoCompleteRequest(Irp, IO_NO_INCREMENT);
                return STATUS_INVALID_PARAMETER;
            }

            Capacity = *(PULONG)Irp->AssociatedIrp.SystemBuffer;
            if (Capacity < HIDCLASS_MIN_QUEUED_REPORTS || Capacity > HIDCLASS_MAX_QUEUED_REPORTS)
                Status = STATUS_INVALID_PARAMETER;
            else
                Status = HidClass_SetReportQueueCapacity(Context, Capacity);

            Irp->IoStatus.Information = 0;
            Irp->IoStatus.Status = Status;
            IoCompleteRequest(Irp, IO_NO_INCREMENT);
            return Status;
        }
        case IOCTL_HID_GET_COLLECTION_DESCRIPTOR:
        {
            //
            // get collection description
            //
            CollectionDescription = HidClassPDO_GetCollectionDescription(&CommonDeviceExtension->DeviceDescription,
                                                                         PDODeviceExtension->CollectionNumber);
            ASSERT(CollectionDescription);

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
                                  CollectionDescription->PreparsedDataLength,
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
                          CollectionDescription->PreparsedDataLength);

            //
            // complete request
            //
            Irp->IoStatus.Information = CollectionDescription->PreparsedDataLength;
            Irp->IoStatus.Status = STATUS_SUCCESS;
            IoCompleteRequest(Irp, IO_NO_INCREMENT);
            return STATUS_SUCCESS;
        }
        case IOCTL_HID_GET_FEATURE:
        {
            NTSTATUS Status;
            PUCHAR ReportBuffer;
            ULONG ReportLength;
            ULONG_PTR Information;

            if (!Irp->MdlAddress)
            {
                Irp->IoStatus.Status = STATUS_INVALID_USER_BUFFER;
                IoCompleteRequest(Irp, IO_NO_INCREMENT);
                return STATUS_INVALID_USER_BUFFER;
            }

            ReportBuffer = MmGetSystemAddressForMdlSafe(Irp->MdlAddress, NormalPagePriority);
            if (!ReportBuffer)
            {
                Irp->IoStatus.Status = STATUS_INSUFFICIENT_RESOURCES;
                IoCompleteRequest(Irp, IO_NO_INCREMENT);
                return STATUS_INSUFFICIENT_RESOURCES;
            }

            Status = HidClass_TransferReport(PDODeviceExtension,
                                             IOCTL_HID_GET_FEATURE,
                                             HidP_Feature,
                                             ReportBuffer,
                                             IoStack->Parameters.DeviceIoControl.OutputBufferLength,
                                             TRUE,
                                             &ReportLength,
                                             &Information);
            Irp->IoStatus.Status = Status;
            Irp->IoStatus.Information = NT_SUCCESS(Status) ? Information : 0;
            IoCompleteRequest(Irp, IO_NO_INCREMENT);
            return Status;
        }
        case IOCTL_HID_SET_FEATURE:
        {
            NTSTATUS Status;
            ULONG ReportLength;
            ULONG_PTR Information;

            Status = HidClass_TransferReport(PDODeviceExtension,
                                             IOCTL_HID_SET_FEATURE,
                                             HidP_Feature,
                                             Irp->AssociatedIrp.SystemBuffer,
                                             IoStack->Parameters.DeviceIoControl.InputBufferLength,
                                             FALSE,
                                             &ReportLength,
                                             &Information);
            Irp->IoStatus.Status = Status;
            Irp->IoStatus.Information = NT_SUCCESS(Status) ? Information : 0;
            IoCompleteRequest(Irp, IO_NO_INCREMENT);
            return Status;
        }
        case IOCTL_HID_GET_INPUT_REPORT:
        {
            NTSTATUS Status;
            PUCHAR ReportBuffer;
            ULONG ReportLength;
            ULONG_PTR Information;

            if (!Irp->MdlAddress)
            {
                Irp->IoStatus.Status = STATUS_INVALID_USER_BUFFER;
                IoCompleteRequest(Irp, IO_NO_INCREMENT);
                return STATUS_INVALID_USER_BUFFER;
            }

            ReportBuffer = MmGetSystemAddressForMdlSafe(Irp->MdlAddress, NormalPagePriority);
            if (!ReportBuffer)
            {
                Irp->IoStatus.Status = STATUS_INSUFFICIENT_RESOURCES;
                IoCompleteRequest(Irp, IO_NO_INCREMENT);
                return STATUS_INSUFFICIENT_RESOURCES;
            }

            Status = HidClass_TransferReport(PDODeviceExtension,
                                             IOCTL_HID_GET_INPUT_REPORT,
                                             HidP_Input,
                                             ReportBuffer,
                                             IoStack->Parameters.DeviceIoControl.OutputBufferLength,
                                             TRUE,
                                             &ReportLength,
                                             &Information);
            Irp->IoStatus.Status = Status;
            Irp->IoStatus.Information = NT_SUCCESS(Status) ? Information : 0;
            IoCompleteRequest(Irp, IO_NO_INCREMENT);
            return Status;
        }
        case IOCTL_HID_SET_OUTPUT_REPORT:
        {
            NTSTATUS Status;
            ULONG ReportLength;
            ULONG_PTR Information;

            Status = HidClass_TransferReport(PDODeviceExtension,
                                             IOCTL_HID_SET_OUTPUT_REPORT,
                                             HidP_Output,
                                             Irp->AssociatedIrp.SystemBuffer,
                                             IoStack->Parameters.DeviceIoControl.InputBufferLength,
                                             FALSE,
                                             &ReportLength,
                                             &Information);
            Irp->IoStatus.Status = Status;
            Irp->IoStatus.Information = NT_SUCCESS(Status) ? Information : 0;
            IoCompleteRequest(Irp, IO_NO_INCREMENT);
            return Status;
        }
        case IOCTL_GET_PHYSICAL_DESCRIPTOR:
        {
            NTSTATUS Status;
            ULONG_PTR Information;

            Status = HidClass_DispatchMiniDriverMdlRequest(PDODeviceExtension,
                                                           IOCTL_GET_PHYSICAL_DESCRIPTOR,
                                                           Irp,
                                                           &Information);
            Irp->IoStatus.Status = Status;
            Irp->IoStatus.Information = NT_SUCCESS(Status) ? Information : 0;
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
    PIO_STACK_LOCATION IoStack = IoGetCurrentIrpStackLocation(Irp);

    DPRINT1("[HIDCLASS] InternalDeviceControl IoControlCode 0x%x not supported\n", IoStack->Parameters.DeviceIoControl.IoControlCode);
    Irp->IoStatus.Status = STATUS_INVALID_DEVICE_REQUEST;
    Irp->IoStatus.Information = 0;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return STATUS_INVALID_DEVICE_REQUEST;
}

static REQUEST_POWER_COMPLETE HidClassFDO_WakeComplete;
static DRIVER_CANCEL HidClassPDO_WaitWakeCancel;

static
VOID
HidClassFDO_ArmWake(
    IN PHIDCLASS_FDO_EXTENSION FDODeviceExtension)
{
    POWER_STATE PowerState;

    if (InterlockedCompareExchange(&FDODeviceExtension->WakeState, HIDCLASS_WAKE_ARMED, HIDCLASS_WAKE_IDLE) != HIDCLASS_WAKE_IDLE)
        return;

    PowerState.SystemState = FDODeviceExtension->Capabilities.SystemWake;
    PoRequestPowerIrp(FDODeviceExtension->Common.HidDeviceExtension.PhysicalDeviceObject,
                      IRP_MN_WAIT_WAKE,
                      PowerState,
                      HidClassFDO_WakeComplete,
                      FDODeviceExtension,
                      &FDODeviceExtension->WakeIrp);
}

VOID
HidClassPDO_CompleteWake(
    IN PHIDCLASS_PDO_DEVICE_EXTENSION PDODeviceExtension,
    IN NTSTATUS Status)
{
    PHIDCLASS_FDO_EXTENSION FDODeviceExtension = PDODeviceExtension->FDODeviceExtension;
    KIRQL OldLevel;
    PIRP Irp;

    KeAcquireSpinLock(&FDODeviceExtension->WakeLock, &OldLevel);
    Irp = PDODeviceExtension->WaitWakeIrp;
    if (Irp != NULL && IoSetCancelRoutine(Irp, NULL) != NULL)
        PDODeviceExtension->WaitWakeIrp = NULL;
    else
        Irp = NULL;
    KeReleaseSpinLock(&FDODeviceExtension->WakeLock, OldLevel);

    if (Irp == NULL)
        return;

    Irp->IoStatus.Status = Status;
    Irp->IoStatus.Information = 0;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
}

static
VOID
NTAPI
HidClassFDO_WakeComplete(
    IN PDEVICE_OBJECT DeviceObject,
    IN UCHAR MinorFunction,
    IN POWER_STATE PowerState,
    IN PVOID Context,
    IN PIO_STATUS_BLOCK IoStatus)
{
    PHIDCLASS_FDO_EXTENSION FDODeviceExtension = Context;
    PDEVICE_RELATIONS Relations;
    LONG PreviousState;
    ULONG Index;

    UNREFERENCED_PARAMETER(DeviceObject);
    UNREFERENCED_PARAMETER(MinorFunction);
    UNREFERENCED_PARAMETER(PowerState);

    PreviousState = InterlockedExchange(&FDODeviceExtension->WakeState, HIDCLASS_WAKE_IDLE);

    Relations = FDODeviceExtension->DeviceRelations;
    for (Index = 0; Relations != NULL && Index < Relations->Count; Index++)
        HidClassPDO_CompleteWake(Relations->Objects[Index]->DeviceExtension, IoStatus->Status);

    if (PreviousState == HIDCLASS_WAKE_CANCELLING)
        KeSetEvent(&FDODeviceExtension->WakeDone, IO_NO_INCREMENT, FALSE);
}

VOID
HidClassFDO_DisarmWake(
    IN PHIDCLASS_FDO_EXTENSION FDODeviceExtension)
{
    KeClearEvent(&FDODeviceExtension->WakeDone);
    if (InterlockedCompareExchange(&FDODeviceExtension->WakeState, HIDCLASS_WAKE_CANCELLING, HIDCLASS_WAKE_ARMED) == HIDCLASS_WAKE_ARMED)
    {
        IoCancelIrp(FDODeviceExtension->WakeIrp);
        KeWaitForSingleObject(&FDODeviceExtension->WakeDone, Executive, KernelMode, FALSE, NULL);
    }
}

static
VOID
NTAPI
HidClassPDO_WaitWakeCancel(
    IN PDEVICE_OBJECT DeviceObject,
    IN PIRP Irp)
{
    PHIDCLASS_PDO_DEVICE_EXTENSION PDODeviceExtension = DeviceObject->DeviceExtension;
    PHIDCLASS_FDO_EXTENSION FDODeviceExtension = PDODeviceExtension->FDODeviceExtension;
    KIRQL OldLevel;

    IoReleaseCancelSpinLock(Irp->CancelIrql);

    KeAcquireSpinLock(&FDODeviceExtension->WakeLock, &OldLevel);
    if (PDODeviceExtension->WaitWakeIrp == Irp)
        PDODeviceExtension->WaitWakeIrp = NULL;
    KeReleaseSpinLock(&FDODeviceExtension->WakeLock, OldLevel);

    Irp->IoStatus.Status = STATUS_CANCELLED;
    Irp->IoStatus.Information = 0;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
}

static
NTSTATUS
HidClassPDO_WaitWake(
    IN PDEVICE_OBJECT DeviceObject,
    IN PIRP Irp)
{
    PHIDCLASS_PDO_DEVICE_EXTENSION PDODeviceExtension = DeviceObject->DeviceExtension;
    PHIDCLASS_FDO_EXTENSION FDODeviceExtension = PDODeviceExtension->FDODeviceExtension;
    PIO_STACK_LOCATION IoStack = IoGetCurrentIrpStackLocation(Irp);
    NTSTATUS Status;
    KIRQL OldLevel;

    if (FDODeviceExtension->Capabilities.SystemWake <= PowerSystemWorking ||
        FDODeviceExtension->Capabilities.DeviceWake == PowerDeviceUnspecified)
    {
        Status = STATUS_NOT_SUPPORTED;
        goto Complete;
    }

    if (IoStack->Parameters.WaitWake.PowerState > FDODeviceExtension->Capabilities.SystemWake)
    {
        Status = STATUS_INVALID_DEVICE_STATE;
        goto Complete;
    }

    KeAcquireSpinLock(&FDODeviceExtension->WakeLock, &OldLevel);
    if (PDODeviceExtension->WaitWakeIrp != NULL)
    {
        KeReleaseSpinLock(&FDODeviceExtension->WakeLock, OldLevel);
        Status = STATUS_DEVICE_BUSY;
        goto Complete;
    }

    IoMarkIrpPending(Irp);
    PDODeviceExtension->WaitWakeIrp = Irp;
    IoSetCancelRoutine(Irp, HidClassPDO_WaitWakeCancel);
    if (Irp->Cancel && IoSetCancelRoutine(Irp, NULL) != NULL)
    {
        PDODeviceExtension->WaitWakeIrp = NULL;
        KeReleaseSpinLock(&FDODeviceExtension->WakeLock, OldLevel);
        Irp->IoStatus.Status = STATUS_CANCELLED;
        Irp->IoStatus.Information = 0;
        PoStartNextPowerIrp(Irp);
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
        return STATUS_PENDING;
    }
    KeReleaseSpinLock(&FDODeviceExtension->WakeLock, OldLevel);

    PoStartNextPowerIrp(Irp);
    HidClassFDO_ArmWake(FDODeviceExtension);
    return STATUS_PENDING;

Complete:
    Irp->IoStatus.Status = Status;
    PoStartNextPowerIrp(Irp);
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return Status;
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
    else if (IoGetCurrentIrpStackLocation(Irp)->MinorFunction == IRP_MN_WAIT_WAKE)
    {
        return HidClassPDO_WaitWake(DeviceObject, Irp);
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
    PHIDCLASS_COMMON_DEVICE_EXTENSION CommonDeviceExtension;
    PIO_STACK_LOCATION IoStack;

    //
    // get current stack location
    //
    IoStack = IoGetCurrentIrpStackLocation(Irp);
    DPRINT("[HIDCLASS] Dispatch Major %x Minor %x\n", IoStack->MajorFunction, IoStack->MinorFunction);

    CommonDeviceExtension = DeviceObject->DeviceExtension;
    if (!CommonDeviceExtension->IsFDO &&
        ((PHIDCLASS_PDO_DEVICE_EXTENSION)CommonDeviceExtension)->Removed &&
        (IoStack->MajorFunction == IRP_MJ_CREATE ||
         IoStack->MajorFunction == IRP_MJ_READ ||
         IoStack->MajorFunction == IRP_MJ_WRITE ||
         IoStack->MajorFunction == IRP_MJ_DEVICE_CONTROL ||
         IoStack->MajorFunction == IRP_MJ_INTERNAL_DEVICE_CONTROL))
    {
        Irp->IoStatus.Status = STATUS_DEVICE_NOT_CONNECTED;
        Irp->IoStatus.Information = 0;
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
        return STATUS_DEVICE_NOT_CONNECTED;
    }

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
