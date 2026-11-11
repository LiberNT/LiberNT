/*
 * PROJECT:     LiberNT kernel-mode tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Bus and function driver that records how the PnP manager filters, restarts, removes, ejects and fails a device
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntifs.h>

#define TEST_TAG 'qRFK'
#define TEST_REPORTED_MAGIC 0x4B465251
#define TEST_ADDED_MAGIC 0x4B464144
#define TEST_DMA_CHANNEL 3
#define TEST_DMA_REQUEST_LINE 21
#define TEST_CONNECTION_ID_LOW 0x5A7E
#define TEST_CONNECTION_ID_HIGH 0x2

typedef enum _TEST_KIND
{
    TestBusFdo,
    TestChildPdo,
    TestChildFdo
} TEST_KIND;

typedef struct _TEST_EXTENSION
{
    TEST_KIND Kind;
    PDEVICE_OBJECT Lower;
    PDEVICE_OBJECT Child;
    PDEVICE_OBJECT Pdo;
    LONG Starts;
    BOOLEAN ReportChange;
    BOOLEAN Removed;
    BOOLEAN Missing;
} TEST_EXTENSION, *PTEST_EXTENSION;

static WCHAR ServicePath[260];
static BOOLEAN EjectRequested;
static ULONG RelationsQueried;
static ULONG BusStateQueried;
static BOOLEAN FailRequested;
static ULONG ChildFdoRemoves;
static ULONG ChildSurprises;
static PDEVICE_OBJECT MissingChild;
static ULONG ChildPdoRemoves;
static ULONG ChildPdoQueryRemoves;
static ULONG BusFdoRemoves;
static const WCHAR ChildDeviceId[] = L"KMTEST\\FILTERREQCHILD";
static const WCHAR ChildHardwareIds[] = L"KMTEST\\FILTERREQCHILD\0";
static const WCHAR ChildInstanceId[] = L"0";
static const WCHAR ChildDescription[] = L"Kmtest filter requirements child";

static VOID Record(PCWSTR Name, ULONG Value)
{
    RtlWriteRegistryValue(RTL_REGISTRY_ABSOLUTE, ServicePath, Name, REG_DWORD, &Value, sizeof(Value));
}

static BOOLEAN Requested(PCWSTR Name)
{
    ULONG Value = 0;
    RTL_QUERY_REGISTRY_TABLE Table[2];

    RtlZeroMemory(Table, sizeof(Table));
    Table[0].Flags = RTL_QUERY_REGISTRY_DIRECT | RTL_QUERY_REGISTRY_TYPECHECK;
    Table[0].Name = (PWSTR)Name;
    Table[0].EntryContext = &Value;
    Table[0].DefaultType = (REG_DWORD << RTL_QUERY_REGISTRY_TYPECHECK_SHIFT) | REG_NONE;
    return NT_SUCCESS(RtlQueryRegistryValues(RTL_REGISTRY_ABSOLUTE, ServicePath, Table, NULL, NULL)) && Value != 0;
}

static NTSTATUS Complete(PIRP Irp, NTSTATUS Status)
{
    Irp->IoStatus.Status = Status;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return Status;
}

static NTSTATUS Pass(PTEST_EXTENSION Extension, PIRP Irp)
{
    IoSkipCurrentIrpStackLocation(Irp);
    return IoCallDriver(Extension->Lower, Irp);
}

static PWSTR CopyString(const WCHAR *Source, SIZE_T Size)
{
    PWSTR Buffer = ExAllocatePoolWithTag(PagedPool, Size, TEST_TAG);

    if (Buffer != NULL)
        RtlCopyMemory(Buffer, Source, Size);
    return Buffer;
}

static PIO_RESOURCE_REQUIREMENTS_LIST BuildRequirements(ULONG Count, BOOLEAN WithDma)
{
    PIO_RESOURCE_REQUIREMENTS_LIST List;
    PIO_RESOURCE_DESCRIPTOR Dma;
    PIO_RESOURCE_DESCRIPTOR Connection;
    ULONG Size = sizeof(IO_RESOURCE_REQUIREMENTS_LIST) + (Count + (WithDma ? 2 : 0) - 1) * sizeof(IO_RESOURCE_DESCRIPTOR);
    ULONG Index;

    List = ExAllocatePoolZero(PagedPool, Size, TEST_TAG);
    if (List == NULL)
        return NULL;
    List->ListSize = Size;
    List->InterfaceType = Internal;
    List->AlternativeLists = 1;
    List->List[0].Version = 1;
    List->List[0].Revision = 1;
    List->List[0].Count = Count;
    for (Index = 0; Index < Count; Index++)
    {
        List->List[0].Descriptors[Index].Type = CmResourceTypeDevicePrivate;
        List->List[0].Descriptors[Index].ShareDisposition = CmResourceShareDeviceExclusive;
        List->List[0].Descriptors[Index].u.DevicePrivate.Data[0] = Index ? TEST_ADDED_MAGIC : TEST_REPORTED_MAGIC;
    }
    if (WithDma)
    {
        Dma = &List->List[0].Descriptors[Count];
        Dma->Type = CmResourceTypeDma;
        Dma->Flags = CM_RESOURCE_DMA_V3;
        Dma->ShareDisposition = CmResourceShareDeviceExclusive;
        Dma->u.DmaV3.Channel = TEST_DMA_CHANNEL;
        Dma->u.DmaV3.RequestLine = TEST_DMA_REQUEST_LINE;
        Dma->u.DmaV3.TransferWidth = Width32Bits;
        List->List[0].Count++;

        Connection = &List->List[0].Descriptors[List->List[0].Count];
        Connection->Type = CmResourceTypeConnection;
        Connection->ShareDisposition = CmResourceShareDeviceExclusive;
        Connection->u.Connection.Class = CM_RESOURCE_CONNECTION_CLASS_SERIAL;
        Connection->u.Connection.Type = CM_RESOURCE_CONNECTION_TYPE_SERIAL_I2C;
        Connection->u.Connection.IdLowPart = TEST_CONNECTION_ID_LOW;
        Connection->u.Connection.IdHighPart = TEST_CONNECTION_ID_HIGH;
        List->List[0].Count++;
    }
    return List;
}

static ULONG StartedConnection(PCM_RESOURCE_LIST Resources)
{
    PCM_PARTIAL_RESOURCE_DESCRIPTOR Descriptor;
    ULONG Index;

    if (Resources == NULL || Resources->Count == 0)
        return 0;
    for (Index = 0; Index < Resources->List[0].PartialResourceList.Count; Index++)
    {
        Descriptor = &Resources->List[0].PartialResourceList.PartialDescriptors[Index];
        if (Descriptor->Type == CmResourceTypeConnection && Descriptor->u.Connection.IdHighPart == TEST_CONNECTION_ID_HIGH)
            return Descriptor->u.Connection.Class | (Descriptor->u.Connection.Type << 8) | (Descriptor->u.Connection.IdLowPart << 16);
    }
    return 0;
}

static ULONG StartedDmaV3(PCM_RESOURCE_LIST Resources)
{
    PCM_PARTIAL_RESOURCE_DESCRIPTOR Descriptor;
    ULONG Index;

    if (Resources == NULL || Resources->Count == 0)
        return 0;
    for (Index = 0; Index < Resources->List[0].PartialResourceList.Count; Index++)
    {
        Descriptor = &Resources->List[0].PartialResourceList.PartialDescriptors[Index];
        if (Descriptor->Type == CmResourceTypeDma && (Descriptor->Flags & CM_RESOURCE_DMA_V3))
            return Descriptor->u.DmaV3.Channel | (Descriptor->u.DmaV3.RequestLine << 8) | (Descriptor->u.DmaV3.TransferWidth << 16);
    }
    return 0;
}

static ULONG StartedPrivateMask(PCM_RESOURCE_LIST Resources)
{
    PCM_PARTIAL_RESOURCE_LIST Partial;
    ULONG Mask = 0;
    ULONG Index;

    if (Resources == NULL || Resources->Count == 0)
        return 0;
    Partial = &Resources->List[0].PartialResourceList;
    for (Index = 0; Index < Partial->Count; Index++)
    {
        if (Partial->PartialDescriptors[Index].Type != CmResourceTypeDevicePrivate)
            continue;
        if (Partial->PartialDescriptors[Index].u.DevicePrivate.Data[0] == TEST_REPORTED_MAGIC)
            Mask |= 1;
        else if (Partial->PartialDescriptors[Index].u.DevicePrivate.Data[0] == TEST_ADDED_MAGIC)
            Mask |= 2;
    }
    return Mask;
}

static NTSTATUS ChildFdoFilter(PTEST_EXTENSION Extension, PIRP Irp)
{
    PIO_STACK_LOCATION Stack = IoGetCurrentIrpStackLocation(Irp);
    PIO_RESOURCE_REQUIREMENTS_LIST Supplied = Stack->Parameters.FilterResourceRequirements.IoResourceRequirementList;
    PIO_RESOURCE_REQUIREMENTS_LIST Current = (PIO_RESOURCE_REQUIREMENTS_LIST)Irp->IoStatus.Information;
    PIO_RESOURCE_REQUIREMENTS_LIST Filtered;
    NTSTATUS Status;

    Record(L"FilterInformationIsList", Current != NULL && Current == Supplied);
    Record(L"FilterReportedMagic", (Current != NULL && Current->AlternativeLists == 1 && Current->List[0].Count == 1)
                                       ? Current->List[0].Descriptors[0].u.DevicePrivate.Data[0] : 0);

    Status = IoForwardIrpSynchronously(Extension->Lower, Irp) ? Irp->IoStatus.Status : STATUS_UNSUCCESSFUL;
    Current = (PIO_RESOURCE_REQUIREMENTS_LIST)Irp->IoStatus.Information;
    Record(L"FilterBusKeptList", Current != NULL && Current == Supplied);
    if (Current == NULL || Current != Supplied)
        return Complete(Irp, Status);

    Filtered = BuildRequirements(2, TRUE);
    if (Filtered == NULL)
        return Complete(Irp, STATUS_INSUFFICIENT_RESOURCES);
    ExFreePool(Current);
    Irp->IoStatus.Information = (ULONG_PTR)Filtered;
    return Complete(Irp, STATUS_SUCCESS);
}

static NTSTATUS ChildFdoPnp(PDEVICE_OBJECT DeviceObject, PTEST_EXTENSION Extension, PIRP Irp)
{
    PIO_STACK_LOCATION Stack = IoGetCurrentIrpStackLocation(Irp);
    NTSTATUS Status;
    LONG Starts;

    switch (Stack->MinorFunction)
    {
        case IRP_MN_FILTER_RESOURCE_REQUIREMENTS:
            return ChildFdoFilter(Extension, Irp);

        case IRP_MN_START_DEVICE:
            Status = IoForwardIrpSynchronously(Extension->Lower, Irp) ? Irp->IoStatus.Status : STATUS_UNSUCCESSFUL;
            Starts = InterlockedIncrement(&Extension->Starts);
            Record(Starts == 1 ? L"StartPrivateMask" : L"RestartPrivateMask",
                   StartedPrivateMask(Stack->Parameters.StartDevice.AllocatedResources));
            Record(L"StartCount", (ULONG)Starts);
            Record(L"StartDmaV3", StartedDmaV3(Stack->Parameters.StartDevice.AllocatedResources));
            Record(L"StartConnection", StartedConnection(Stack->Parameters.StartDevice.AllocatedResources));
            if (Starts == 1)
            {
                Extension->ReportChange = TRUE;
                IoInvalidateDeviceState(Extension->Pdo);
            }
            return Complete(Irp, Status);

        case IRP_MN_QUERY_PNP_DEVICE_STATE:
            IoForwardIrpSynchronously(Extension->Lower, Irp);
            if (Extension->ReportChange)
            {
                Extension->ReportChange = FALSE;
                Irp->IoStatus.Information |= PNP_DEVICE_RESOURCE_REQUIREMENTS_CHANGED;
            }
            return Complete(Irp, STATUS_SUCCESS);

        case IRP_MN_QUERY_STOP_DEVICE:
        case IRP_MN_STOP_DEVICE:
            Record(L"StopSeen", 1);
            Irp->IoStatus.Status = STATUS_SUCCESS;
            return Pass(Extension, Irp);

        case IRP_MN_REMOVE_DEVICE:
            Record(L"ChildFdoRemoves", ++ChildFdoRemoves);
            Irp->IoStatus.Status = STATUS_SUCCESS;
            Status = Pass(Extension, Irp);
            IoDetachDevice(Extension->Lower);
            IoDeleteDevice(DeviceObject);
            return Status;

        case IRP_MN_SURPRISE_REMOVAL:
            Record(L"ChildSurprises", ++ChildSurprises);
            /* A driver may still report its device's state once the device is gone */
            IoInvalidateDeviceState(Extension->Pdo);
            Irp->IoStatus.Status = STATUS_SUCCESS;
            return Pass(Extension, Irp);

        case IRP_MN_QUERY_REMOVE_DEVICE:
        case IRP_MN_CANCEL_REMOVE_DEVICE:
        case IRP_MN_CANCEL_STOP_DEVICE:
            Irp->IoStatus.Status = STATUS_SUCCESS;
            return Pass(Extension, Irp);

        default:
            return Pass(Extension, Irp);
    }
}

static NTSTATUS ReportChild(PDEVICE_OBJECT DeviceObject, PTEST_EXTENSION Extension, PIRP Irp)
{
    PDEVICE_RELATIONS Existing = (PDEVICE_RELATIONS)Irp->IoStatus.Information;
    PDEVICE_RELATIONS Relations;
    PTEST_EXTENSION ChildExtension;
    ULONG Count = Existing ? Existing->Count : 0;
    NTSTATUS Status;

    /* The bus forgets a child it stops reporting; that child goes with its remove request */
    if (Requested(L"HideChild"))
    {
        if (Extension->Child != NULL)
        {
            ((PTEST_EXTENSION)Extension->Child->DeviceExtension)->Missing = TRUE;
            MissingChild = Extension->Child;
            Extension->Child = NULL;
        }
        Irp->IoStatus.Status = STATUS_SUCCESS;
        return Pass(Extension, Irp);
    }

    if (Extension->Child == NULL)
    {
        Status = IoCreateDevice(DeviceObject->DriverObject, sizeof(TEST_EXTENSION), NULL, FILE_DEVICE_UNKNOWN,
                                FILE_AUTOGENERATED_DEVICE_NAME, FALSE, &Extension->Child);
        if (!NT_SUCCESS(Status))
            return Complete(Irp, Status);
        ChildExtension = Extension->Child->DeviceExtension;
        RtlZeroMemory(ChildExtension, sizeof(*ChildExtension));
        ChildExtension->Kind = TestChildPdo;
        Extension->Child->Flags |= DO_POWER_PAGABLE;
        Extension->Child->Flags &= ~DO_DEVICE_INITIALIZING;
    }

    Relations = ExAllocatePoolWithTag(PagedPool, FIELD_OFFSET(DEVICE_RELATIONS, Objects[Count + 1]), TEST_TAG);
    if (Relations == NULL)
        return Complete(Irp, STATUS_INSUFFICIENT_RESOURCES);
    if (Existing != NULL)
    {
        RtlCopyMemory(Relations->Objects, Existing->Objects, Count * sizeof(PDEVICE_OBJECT));
        ExFreePool(Existing);
    }
    ObReferenceObject(Extension->Child);
    Relations->Objects[Count] = Extension->Child;
    Relations->Count = Count + 1;
    Irp->IoStatus.Information = (ULONG_PTR)Relations;
    Irp->IoStatus.Status = STATUS_SUCCESS;
    return Pass(Extension, Irp);
}

static NTSTATUS BusFdoPnp(PDEVICE_OBJECT DeviceObject, PTEST_EXTENSION Extension, PIRP Irp)
{
    PIO_STACK_LOCATION Stack = IoGetCurrentIrpStackLocation(Irp);
    NTSTATUS Status;

    switch (Stack->MinorFunction)
    {
        case IRP_MN_QUERY_PNP_DEVICE_STATE:
            IoForwardIrpSynchronously(Extension->Lower, Irp);
            Record(L"BusStateQueried", ++BusStateQueried);
            if (FailRequested)
                Irp->IoStatus.Information |= PNP_DEVICE_FAILED;
            return Complete(Irp, STATUS_SUCCESS);

        case IRP_MN_START_DEVICE:
            Status = IoForwardIrpSynchronously(Extension->Lower, Irp) ? Irp->IoStatus.Status : STATUS_UNSUCCESSFUL;
            return Complete(Irp, Status);

        case IRP_MN_QUERY_DEVICE_RELATIONS:
            if (Stack->Parameters.QueryDeviceRelations.Type != BusRelations)
                return Pass(Extension, Irp);
            Record(L"BusRelationsQueried", ++RelationsQueried);
            if (!FailRequested && Requested(L"FailBus"))
            {
                FailRequested = TRUE;
                IoInvalidateDeviceState(Extension->Pdo);
            }
            if (!EjectRequested && Extension->Child != NULL && Requested(L"EjectChild"))
            {
                EjectRequested = TRUE;
                IoRequestDeviceEject(Extension->Child);
            }
            if (MissingChild != NULL && Requested(L"EjectMissing"))
                IoRequestDeviceEject(MissingChild);
            return ReportChild(DeviceObject, Extension, Irp);

        case IRP_MN_REMOVE_DEVICE:
            Record(L"BusFdoRemoves", ++BusFdoRemoves);
            Irp->IoStatus.Status = STATUS_SUCCESS;
            Status = Pass(Extension, Irp);
            IoDetachDevice(Extension->Lower);
            if (Extension->Child != NULL)
                IoDeleteDevice(Extension->Child);
            IoDeleteDevice(DeviceObject);
            return Status;

        case IRP_MN_SURPRISE_REMOVAL:
            Record(L"BusSurprised", 1);
            Irp->IoStatus.Status = STATUS_SUCCESS;
            return Pass(Extension, Irp);

        case IRP_MN_QUERY_REMOVE_DEVICE:
        case IRP_MN_CANCEL_REMOVE_DEVICE:
        case IRP_MN_QUERY_STOP_DEVICE:
        case IRP_MN_CANCEL_STOP_DEVICE:
        case IRP_MN_STOP_DEVICE:
            Irp->IoStatus.Status = STATUS_SUCCESS;
            return Pass(Extension, Irp);

        default:
            return Pass(Extension, Irp);
    }
}

static NTSTATUS ChildPdoQueryId(PIRP Irp, BUS_QUERY_ID_TYPE Type)
{
    PWSTR Id = NULL;

    switch (Type)
    {
        case BusQueryDeviceID:
            Id = CopyString(ChildDeviceId, sizeof(ChildDeviceId));
            break;
        case BusQueryHardwareIDs:
            Id = CopyString(ChildHardwareIds, sizeof(ChildHardwareIds));
            break;
        case BusQueryInstanceID:
            Id = CopyString(ChildInstanceId, sizeof(ChildInstanceId));
            break;
        default:
            return Irp->IoStatus.Status;
    }
    if (Id == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;
    Irp->IoStatus.Information = (ULONG_PTR)Id;
    return STATUS_SUCCESS;
}

static NTSTATUS ChildPdoPnp(PDEVICE_OBJECT DeviceObject, PIRP Irp)
{
    PTEST_EXTENSION Extension = DeviceObject->DeviceExtension;
    PIO_STACK_LOCATION Stack = IoGetCurrentIrpStackLocation(Irp);
    PDEVICE_RELATIONS Relations;
    PIO_RESOURCE_REQUIREMENTS_LIST Requirements;
    PWSTR Text;
    NTSTATUS Status = Irp->IoStatus.Status;

    switch (Stack->MinorFunction)
    {
        case IRP_MN_QUERY_ID:
            Status = ChildPdoQueryId(Irp, Stack->Parameters.QueryId.IdType);
            break;

        case IRP_MN_QUERY_DEVICE_TEXT:
            if (Stack->Parameters.QueryDeviceText.DeviceTextType != DeviceTextDescription)
                break;
            Text = CopyString(ChildDescription, sizeof(ChildDescription));
            Status = Text ? STATUS_SUCCESS : STATUS_INSUFFICIENT_RESOURCES;
            if (Text != NULL)
                Irp->IoStatus.Information = (ULONG_PTR)Text;
            break;

        case IRP_MN_QUERY_CAPABILITIES:
            Stack->Parameters.DeviceCapabilities.Capabilities->UniqueID = FALSE;
            Stack->Parameters.DeviceCapabilities.Capabilities->SilentInstall = TRUE;
            Stack->Parameters.DeviceCapabilities.Capabilities->EjectSupported = TRUE;
            Status = STATUS_SUCCESS;
            break;

        case IRP_MN_EJECT:
            Record(L"EjectAfterRemove", Extension->Removed);
            Status = STATUS_SUCCESS;
            break;

        case IRP_MN_REMOVE_DEVICE:
            Record(L"ChildPdoRemoves", ++ChildPdoRemoves);
            Extension->Removed = TRUE;
            Complete(Irp, STATUS_SUCCESS);
            if (Extension->Missing)
            {
                if (MissingChild == DeviceObject)
                    MissingChild = NULL;
                IoDeleteDevice(DeviceObject);
            }
            return STATUS_SUCCESS;

        case IRP_MN_QUERY_DEVICE_RELATIONS:
            if (Stack->Parameters.QueryDeviceRelations.Type != TargetDeviceRelation)
                break;
            Relations = ExAllocatePoolWithTag(PagedPool, sizeof(DEVICE_RELATIONS), TEST_TAG);
            Status = Relations ? STATUS_SUCCESS : STATUS_INSUFFICIENT_RESOURCES;
            if (Relations == NULL)
                break;
            ObReferenceObject(DeviceObject);
            Relations->Count = 1;
            Relations->Objects[0] = DeviceObject;
            Irp->IoStatus.Information = (ULONG_PTR)Relations;
            break;

        case IRP_MN_QUERY_RESOURCE_REQUIREMENTS:
            Requirements = BuildRequirements(1, FALSE);
            Status = Requirements ? STATUS_SUCCESS : STATUS_INSUFFICIENT_RESOURCES;
            if (Requirements != NULL)
                Irp->IoStatus.Information = (ULONG_PTR)Requirements;
            break;

        case IRP_MN_START_DEVICE:
            Extension->Removed = FALSE;
            Status = STATUS_SUCCESS;
            break;

        case IRP_MN_QUERY_REMOVE_DEVICE:
            Record(L"ChildPdoQueryRemoves", ++ChildPdoQueryRemoves);
            Status = STATUS_SUCCESS;
            break;

        case IRP_MN_QUERY_PNP_DEVICE_STATE:
            if (Extension->Missing)
                Irp->IoStatus.Information |= PNP_DEVICE_REMOVED;
            Status = STATUS_SUCCESS;
            break;

        case IRP_MN_CANCEL_REMOVE_DEVICE:
        case IRP_MN_QUERY_STOP_DEVICE:
        case IRP_MN_CANCEL_STOP_DEVICE:
        case IRP_MN_STOP_DEVICE:
        case IRP_MN_SURPRISE_REMOVAL:
            Status = STATUS_SUCCESS;
            break;

        default:
            break;
    }
    return Complete(Irp, Status);
}

static NTSTATUS NTAPI TestPnp(PDEVICE_OBJECT DeviceObject, PIRP Irp)
{
    PTEST_EXTENSION Extension = DeviceObject->DeviceExtension;

    if (Extension->Kind == TestChildPdo)
        return ChildPdoPnp(DeviceObject, Irp);
    if (Extension->Kind == TestChildFdo)
        return ChildFdoPnp(DeviceObject, Extension, Irp);
    return BusFdoPnp(DeviceObject, Extension, Irp);
}

static NTSTATUS NTAPI TestCreateClose(PDEVICE_OBJECT DeviceObject, PIRP Irp)
{
    UNREFERENCED_PARAMETER(DeviceObject);
    return Complete(Irp, STATUS_SUCCESS);
}

/* Like a file system on its volume, the child's function driver opens and closes a stream file on its device */
static NTSTATUS NTAPI TestDeviceControl(PDEVICE_OBJECT DeviceObject, PIRP Irp)
{
    PTEST_EXTENSION Extension = DeviceObject->DeviceExtension;
    PFILE_OBJECT Stream = NULL;

    if (Extension->Kind == TestChildFdo)
        Stream = IoCreateStreamFileObjectLite(NULL, Extension->Pdo);
    if (Stream != NULL)
        ObDereferenceObject(Stream);
    return Complete(Irp, Stream != NULL ? STATUS_SUCCESS : STATUS_INVALID_DEVICE_REQUEST);
}

static NTSTATUS NTAPI TestPower(PDEVICE_OBJECT DeviceObject, PIRP Irp)
{
    PTEST_EXTENSION Extension = DeviceObject->DeviceExtension;
    PIO_STACK_LOCATION Stack = IoGetCurrentIrpStackLocation(Irp);
    NTSTATUS Status = Irp->IoStatus.Status;

    PoStartNextPowerIrp(Irp);
    if (Extension->Kind != TestChildPdo)
    {
        IoSkipCurrentIrpStackLocation(Irp);
        return PoCallDriver(Extension->Lower, Irp);
    }
    if (Stack->MinorFunction == IRP_MN_SET_POWER || Stack->MinorFunction == IRP_MN_QUERY_POWER)
        Status = STATUS_SUCCESS;
    return Complete(Irp, Status);
}

static NTSTATUS NTAPI TestAddDevice(PDRIVER_OBJECT DriverObject, PDEVICE_OBJECT Pdo)
{
    PTEST_EXTENSION Extension;
    PDEVICE_OBJECT Fdo;
    NTSTATUS Status;

    Status = IoCreateDevice(DriverObject, sizeof(TEST_EXTENSION), NULL, FILE_DEVICE_UNKNOWN, 0, FALSE, &Fdo);
    if (!NT_SUCCESS(Status))
        return Status;
    Extension = Fdo->DeviceExtension;
    RtlZeroMemory(Extension, sizeof(*Extension));
    Extension->Kind = (Pdo->DriverObject == DriverObject) ? TestChildFdo : TestBusFdo;
    Extension->Pdo = Pdo;
    Extension->Lower = IoAttachDeviceToDeviceStack(Fdo, Pdo);
    if (Extension->Lower == NULL)
    {
        IoDeleteDevice(Fdo);
        return STATUS_NO_SUCH_DEVICE;
    }
    Fdo->Flags |= DO_POWER_PAGABLE;
    Fdo->Flags &= ~DO_DEVICE_INITIALIZING;
    return STATUS_SUCCESS;
}

static VOID NTAPI TestUnload(PDRIVER_OBJECT DriverObject)
{
    UNREFERENCED_PARAMETER(DriverObject);
}

NTSTATUS NTAPI DriverEntry(PDRIVER_OBJECT DriverObject, PUNICODE_STRING RegistryPath)
{
    if (RegistryPath->Length >= sizeof(ServicePath))
        return STATUS_NAME_TOO_LONG;
    RtlCopyMemory(ServicePath, RegistryPath->Buffer, RegistryPath->Length);
    ServicePath[RegistryPath->Length / sizeof(WCHAR)] = UNICODE_NULL;

    DriverObject->DriverUnload = TestUnload;
    DriverObject->DriverExtension->AddDevice = TestAddDevice;
    DriverObject->MajorFunction[IRP_MJ_CREATE] = TestCreateClose;
    DriverObject->MajorFunction[IRP_MJ_CLEANUP] = TestCreateClose;
    DriverObject->MajorFunction[IRP_MJ_CLOSE] = TestCreateClose;
    DriverObject->MajorFunction[IRP_MJ_DEVICE_CONTROL] = TestDeviceControl;
    DriverObject->MajorFunction[IRP_MJ_PNP] = TestPnp;
    DriverObject->MajorFunction[IRP_MJ_POWER] = TestPower;
    return STATUS_SUCCESS;
}
