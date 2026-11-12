/*
 * PROJECT:     LiberNT kernel-mode tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Bus and function driver that records how the PnP manager filters, restarts, removes, ejects and fails a device
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntifs.h>
#include <initguid.h>
#include <wdmguid.h>

#define TEST_TAG 'qRFK'
#define TEST_REPORTED_MAGIC 0x4B465251
#define TEST_ADDED_MAGIC 0x4B464144
#define TEST_DMA_CHANNEL 3
#define TEST_DMA_REQUEST_LINE 21
#define TEST_LARGE_LENGTH 0x200000000ULL
#define TEST_LARGE_MINIMUM 0x1000000000ULL
#define TEST_OCCUPIED_BASE 0x6F0000000ULL
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
    PDEVICE_OBJECT NullChild;
    PDEVICE_OBJECT Pdo;
    LONG Starts;
    BOOLEAN ReportChange;
    BOOLEAN Removed;
    BOOLEAN Missing;
    BOOLEAN Null;
    UNICODE_STRING Interface;
} TEST_EXTENSION, *PTEST_EXTENSION;

static const GUID ChildInterfaceGuid = { 0x5e1a6a42, 0x8c3b, 0x4b21, { 0x9c, 0x7e, 0x3f, 0x2d, 0x8a, 0x61, 0xb0, 0xc4 } };

static WCHAR ServicePath[260];
static BOOLEAN EjectRequested;
static ULONG RelationsQueried;
static ULONG BusStateQueried;
static BOOLEAN FailRequested;
static ULONG ChildFdoRemoves;
static ULONG ChildAddDevices;
static ULONG ChildSurprises;
static PDEVICE_OBJECT MissingChild;
static ULONG ChildPdoRemoves;
static ULONG ChildPdoQueryRemoves;
static ULONG BusFdoRemoves;
static const WCHAR ChildDeviceId[] = L"KMTEST\\FILTERREQCHILD";
static const WCHAR ChildHardwareIds[] = L"KMTEST\\FILTERREQCHILD\0";
static const WCHAR ChildInstanceId[] = L"0";
static const WCHAR NullDeviceId[] = L"KMTEST\\FILTERREQNULL";
static const WCHAR NullHardwareIds[] = L"KMTEST\\FILTERREQNULL\0";
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

static PIO_RESOURCE_REQUIREMENTS_LIST BuildRequirements(ULONG Count, BOOLEAN WithDma, BOOLEAN WithLargeMemory,
                                                       BOOLEAN WithOccupied)
{
    PIO_RESOURCE_REQUIREMENTS_LIST List;
    PIO_RESOURCE_DESCRIPTOR Dma;
    PIO_RESOURCE_DESCRIPTOR Connection;
    PIO_RESOURCE_DESCRIPTOR Large;
    PIO_RESOURCE_DESCRIPTOR Occupied;
    ULONG Size = sizeof(IO_RESOURCE_REQUIREMENTS_LIST) +
                 (Count + (WithDma ? 2 : 0) + (WithLargeMemory ? 1 : 0) + (WithOccupied ? 1 : 0) - 1) *
                     sizeof(IO_RESOURCE_DESCRIPTOR);
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
    if (WithLargeMemory)
    {
        Large = &List->List[0].Descriptors[List->List[0].Count];
        Large->Type = CmResourceTypeMemoryLarge;
        Large->ShareDisposition = CmResourceShareDeviceExclusive;
        Large->Flags = CM_RESOURCE_MEMORY_READ_WRITE | CM_RESOURCE_MEMORY_LARGE_40;
        Large->u.Memory40.Length40 = (ULONG)(TEST_LARGE_LENGTH >> 8);
        Large->u.Memory40.Alignment40 = (ULONG)(TEST_LARGE_LENGTH >> 8);
        Large->u.Memory40.MinimumAddress.QuadPart = TEST_LARGE_MINIMUM;
        Large->u.Memory40.MaximumAddress.QuadPart = TEST_LARGE_MINIMUM * 4 - 1;
        List->List[0].Count++;
    }
    if (WithOccupied)
    {
        Occupied = &List->List[0].Descriptors[List->List[0].Count];
        Occupied->Type = CmResourceTypeMemory;
        Occupied->ShareDisposition = CmResourceShareDeviceExclusive;
        Occupied->Flags = CM_RESOURCE_MEMORY_READ_WRITE;
        Occupied->u.Memory.Length = PAGE_SIZE;
        Occupied->u.Memory.Alignment = PAGE_SIZE;
        Occupied->u.Memory.MinimumAddress.QuadPart = TEST_OCCUPIED_BASE;
        Occupied->u.Memory.MaximumAddress.QuadPart = TEST_OCCUPIED_BASE + PAGE_SIZE - 1;
        List->List[0].Count++;
    }
    return List;
}

/* The page the child is made to ask for is held by this driver as a legacy claim */
static VOID ClaimOccupiedPage(PDRIVER_OBJECT DriverObject)
{
    CM_RESOURCE_LIST Claim;
    BOOLEAN Conflict = FALSE;

    RtlZeroMemory(&Claim, sizeof(Claim));
    Claim.Count = 1;
    Claim.List[0].InterfaceType = Internal;
    Claim.List[0].PartialResourceList.Version = 1;
    Claim.List[0].PartialResourceList.Revision = 1;
    Claim.List[0].PartialResourceList.Count = 1;
    Claim.List[0].PartialResourceList.PartialDescriptors[0].Type = CmResourceTypeMemory;
    Claim.List[0].PartialResourceList.PartialDescriptors[0].ShareDisposition = CmResourceShareDeviceExclusive;
    Claim.List[0].PartialResourceList.PartialDescriptors[0].Flags = CM_RESOURCE_MEMORY_READ_WRITE;
    Claim.List[0].PartialResourceList.PartialDescriptors[0].u.Memory.Start.QuadPart = (LONGLONG)TEST_OCCUPIED_BASE;
    Claim.List[0].PartialResourceList.PartialDescriptors[0].u.Memory.Length = PAGE_SIZE;
    Record(L"OccupiedClaim", (ULONG)IoReportResourceUsage(NULL, DriverObject, &Claim, sizeof(Claim), NULL, NULL, 0,
                                                          FALSE, &Conflict));
}

/* The large window the child was started with: its length and start in GB */
static ULONG StartedLargeMemory(PCM_RESOURCE_LIST Resources)
{
    PCM_PARTIAL_RESOURCE_DESCRIPTOR Descriptor;
    ULONGLONG Start = 0, Length;
    ULONG Index;

    if (Resources == NULL || Resources->Count == 0)
        return 0;
    for (Index = 0; Index < Resources->List[0].PartialResourceList.Count; Index++)
    {
        Descriptor = &Resources->List[0].PartialResourceList.PartialDescriptors[Index];
        if (Descriptor->Type != CmResourceTypeMemoryLarge)
            continue;
        Length = RtlCmDecodeMemIoResource(Descriptor, &Start);
        return (ULONG)(Length >> 30) | ((ULONG)(Start >> 30) << 16);
    }
    return 0;
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

    if (Requested(L"OccupiedMemory"))
        ClaimOccupiedPage(IoGetCurrentIrpStackLocation(Irp)->DeviceObject->DriverObject);
    Filtered = BuildRequirements(2, TRUE, Requested(L"LargeMemory"), Requested(L"OccupiedMemory"));
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
            if (NT_SUCCESS(Status) && Requested(L"FailChildStart"))
                return Complete(Irp, STATUS_UNSUCCESSFUL);
            Starts = InterlockedIncrement(&Extension->Starts);
            Record(Starts == 1 ? L"StartPrivateMask" : L"RestartPrivateMask",
                   StartedPrivateMask(Stack->Parameters.StartDevice.AllocatedResources));
            Record(L"StartCount", (ULONG)Starts);
            Record(L"StartDmaV3", StartedDmaV3(Stack->Parameters.StartDevice.AllocatedResources));
            Record(L"StartConnection", StartedConnection(Stack->Parameters.StartDevice.AllocatedResources));
            Record(L"StartLargeMemory", StartedLargeMemory(Stack->Parameters.StartDevice.AllocatedResources));
            /* The child leaves disabling its interface to the PnP manager when it goes */
            if (NT_SUCCESS(Status) && Extension->Interface.Buffer == NULL &&
                NT_SUCCESS(IoRegisterDeviceInterface(Extension->Pdo, &ChildInterfaceGuid, NULL, &Extension->Interface)))
            {
                Record(L"ChildInterfaceEnabled", NT_SUCCESS(IoSetDeviceInterfaceState(&Extension->Interface, TRUE)));
            }
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
            RtlFreeUnicodeString(&Extension->Interface);
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

static NTSTATUS CreateChild(PDEVICE_OBJECT DeviceObject, BOOLEAN Null, PDEVICE_OBJECT *Child)
{
    PTEST_EXTENSION ChildExtension;
    NTSTATUS Status;

    Status = IoCreateDevice(DeviceObject->DriverObject, sizeof(TEST_EXTENSION), NULL, FILE_DEVICE_UNKNOWN,
                            FILE_AUTOGENERATED_DEVICE_NAME, FALSE, Child);
    if (!NT_SUCCESS(Status))
        return Status;
    ChildExtension = (*Child)->DeviceExtension;
    RtlZeroMemory(ChildExtension, sizeof(*ChildExtension));
    ChildExtension->Kind = TestChildPdo;
    ChildExtension->Null = Null;
    (*Child)->Flags |= DO_POWER_PAGABLE;
    (*Child)->Flags &= ~DO_DEVICE_INITIALIZING;
    return STATUS_SUCCESS;
}

static NTSTATUS ReportChild(PDEVICE_OBJECT DeviceObject, PTEST_EXTENSION Extension, PIRP Irp)
{
    PDEVICE_RELATIONS Existing = (PDEVICE_RELATIONS)Irp->IoStatus.Information;
    PDEVICE_RELATIONS Relations;
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
        Status = CreateChild(DeviceObject, FALSE, &Extension->Child);
        if (!NT_SUCCESS(Status))
            return Complete(Irp, Status);
    }

    /* A second child, driven by a null driver, while the test asks for it */
    if (Requested(L"NullChild"))
    {
        if (Extension->NullChild == NULL)
        {
            Status = CreateChild(DeviceObject, TRUE, &Extension->NullChild);
            if (!NT_SUCCESS(Status))
                return Complete(Irp, Status);
        }
    }
    else if (Extension->NullChild != NULL)
    {
        ((PTEST_EXTENSION)Extension->NullChild->DeviceExtension)->Missing = TRUE;
        Extension->NullChild = NULL;
    }

    Relations = ExAllocatePoolWithTag(PagedPool, FIELD_OFFSET(DEVICE_RELATIONS, Objects[Count + 2]), TEST_TAG);
    if (Relations == NULL)
        return Complete(Irp, STATUS_INSUFFICIENT_RESOURCES);
    if (Existing != NULL)
    {
        RtlCopyMemory(Relations->Objects, Existing->Objects, Count * sizeof(PDEVICE_OBJECT));
        ExFreePool(Existing);
    }
    ObReferenceObject(Extension->Child);
    Relations->Objects[Count++] = Extension->Child;
    if (Extension->NullChild != NULL)
    {
        ObReferenceObject(Extension->NullChild);
        Relations->Objects[Count++] = Extension->NullChild;
    }
    Relations->Count = Count;
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
            if (Extension->NullChild != NULL)
                IoDeleteDevice(Extension->NullChild);
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

static NTSTATUS ChildPdoQueryId(PTEST_EXTENSION Extension, PIRP Irp, BUS_QUERY_ID_TYPE Type)
{
    PWSTR Id = NULL;

    switch (Type)
    {
        case BusQueryDeviceID:
            Id = Extension->Null ? CopyString(NullDeviceId, sizeof(NullDeviceId)) : CopyString(ChildDeviceId, sizeof(ChildDeviceId));
            break;
        case BusQueryHardwareIDs:
            Id = Extension->Null ? CopyString(NullHardwareIds, sizeof(NullHardwareIds)) :
                                   CopyString(ChildHardwareIds, sizeof(ChildHardwareIds));
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
            Status = ChildPdoQueryId(Extension, Irp, Stack->Parameters.QueryId.IdType);
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
            if (!Extension->Null)
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
            Requirements = BuildRequirements(1, FALSE, FALSE, FALSE);
            Status = Requirements ? STATUS_SUCCESS : STATUS_INSUFFICIENT_RESOURCES;
            if (Requirements != NULL)
                Irp->IoStatus.Information = (ULONG_PTR)Requirements;
            break;

        case IRP_MN_START_DEVICE:
            Extension->Removed = FALSE;
            Status = STATUS_SUCCESS;
            break;

        case IRP_MN_QUERY_REMOVE_DEVICE:
            if (!Extension->Null)
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
/* A holder of the child registers for target device change on its handle, then refuses the removal or closes the handle */
#define HOLD_REFUSE 1
#define HOLD_CLOSE 2
#define HOLD_REREGISTER 4
static HANDLE HoldHandle;
static PFILE_OBJECT HoldFile;
static PVOID HoldEntry;
static ULONG HoldMode;
static ULONG HoldQueryRemoves;
static ULONG HoldCancels;
static ULONG HoldCompletes;
static ULONG HoldReregistrations;
static PDRIVER_OBJECT HoldDriver;
static WCHAR HoldNameBuffer[128];
static UNICODE_STRING HoldName;

static VOID HoldClose(VOID)
{
    if (HoldFile != NULL)
        ObDereferenceObject(HoldFile);
    HoldFile = NULL;
    if (HoldHandle != NULL)
        ZwClose(HoldHandle);
    HoldHandle = NULL;
}

static NTSTATUS NTAPI HoldNotify(PVOID NotificationStructure, PVOID Context);

static NTSTATUS HoldOpen(VOID)
{
    OBJECT_ATTRIBUTES Attributes;
    IO_STATUS_BLOCK Iosb;
    NTSTATUS Status;

    InitializeObjectAttributes(&Attributes, &HoldName, OBJ_KERNEL_HANDLE, NULL, NULL);
    Status = ZwCreateFile(&HoldHandle, FILE_READ_ATTRIBUTES, &Attributes, &Iosb, NULL, 0, FILE_SHARE_READ | FILE_SHARE_WRITE,
                          FILE_OPEN, 0, NULL, 0);
    if (!NT_SUCCESS(Status))
    {
        HoldHandle = NULL;
        return Status;
    }

    Status = ObReferenceObjectByHandle(HoldHandle, 0, *IoFileObjectType, KernelMode, (PVOID *)&HoldFile, NULL);
    if (NT_SUCCESS(Status))
        Status = IoRegisterPlugPlayNotification(EventCategoryTargetDeviceChange, 0, HoldFile, HoldDriver, HoldNotify, NULL,
                                                &HoldEntry);
    if (!NT_SUCCESS(Status))
    {
        HoldEntry = NULL;
        HoldClose();
    }
    return Status;
}

/* The documented handler of a cancelled removal: unregister, reopen the device and register on the new handle */
static VOID HoldReregister(VOID)
{
    IoUnregisterPlugPlayNotification(HoldEntry);
    HoldEntry = NULL;
    HoldClose();
    Record(L"HoldReregistered", NT_SUCCESS(HoldOpen()));
}

static NTSTATUS NTAPI HoldNotify(PVOID NotificationStructure, PVOID Context)
{
    PTARGET_DEVICE_REMOVAL_NOTIFICATION Notification = NotificationStructure;

    UNREFERENCED_PARAMETER(Context);

    if (IsEqualGUID(&Notification->Event, &GUID_TARGET_DEVICE_QUERY_REMOVE))
    {
        Record(L"HoldQueryRemoves", ++HoldQueryRemoves);
        if (HoldMode == HOLD_REFUSE || HoldMode == HOLD_REREGISTER)
            return STATUS_UNSUCCESSFUL;
        HoldClose();
    }
    else if (IsEqualGUID(&Notification->Event, &GUID_TARGET_DEVICE_REMOVE_CANCELLED))
    {
        Record(L"HoldCancels", ++HoldCancels);
        /* A round that also calls the new registration would loop; three are enough to tell */
        if (HoldMode == HOLD_REREGISTER && HoldReregistrations++ < 3)
            HoldReregister();
    }
    else if (IsEqualGUID(&Notification->Event, &GUID_TARGET_DEVICE_REMOVE_COMPLETE))
    {
        Record(L"HoldCompletes", ++HoldCompletes);
    }
    return STATUS_SUCCESS;
}

static NTSTATUS HoldChild(PDEVICE_OBJECT DeviceObject, PTEST_EXTENSION Extension, ULONG Mode)
{
    UCHAR Buffer[sizeof(OBJECT_NAME_INFORMATION) + 128 * sizeof(WCHAR)];
    POBJECT_NAME_INFORMATION Name = (POBJECT_NAME_INFORMATION)Buffer;
    ULONG Length;
    NTSTATUS Status;

    if (Extension->Kind != TestBusFdo || Extension->Child == NULL || HoldEntry != NULL)
        return STATUS_INVALID_DEVICE_REQUEST;

    Status = ObQueryNameString(Extension->Child, Name, sizeof(Buffer), &Length);
    if (!NT_SUCCESS(Status))
        return Status;
    if (Name->Name.Length > sizeof(HoldNameBuffer))
        return STATUS_NAME_TOO_LONG;

    RtlCopyMemory(HoldNameBuffer, Name->Name.Buffer, Name->Name.Length);
    HoldName.Buffer = HoldNameBuffer;
    HoldName.Length = Name->Name.Length;
    HoldName.MaximumLength = sizeof(HoldNameBuffer);
    HoldDriver = DeviceObject->DriverObject;
    HoldMode = Mode;
    HoldReregistrations = 0;

    return HoldOpen();
}

static VOID ReleaseChild(VOID)
{
    if (HoldEntry != NULL)
        IoUnregisterPlugPlayNotification(HoldEntry);
    HoldEntry = NULL;
    HoldClose();
}

static NTSTATUS NTAPI TestDeviceControl(PDEVICE_OBJECT DeviceObject, PIRP Irp)
{
    PTEST_EXTENSION Extension = DeviceObject->DeviceExtension;
    PIO_STACK_LOCATION Stack = IoGetCurrentIrpStackLocation(Irp);
    PFILE_OBJECT Stream = NULL;

    switch (Stack->Parameters.DeviceIoControl.IoControlCode)
    {
        case CTL_CODE(FILE_DEVICE_UNKNOWN, 0x801, METHOD_BUFFERED, FILE_ANY_ACCESS):
            if (Stack->Parameters.DeviceIoControl.InputBufferLength < sizeof(ULONG))
                return Complete(Irp, STATUS_INVALID_PARAMETER);
            return Complete(Irp, HoldChild(DeviceObject, Extension, *(PULONG)Irp->AssociatedIrp.SystemBuffer));

        case CTL_CODE(FILE_DEVICE_UNKNOWN, 0x802, METHOD_BUFFERED, FILE_ANY_ACCESS):
            ReleaseChild();
            return Complete(Irp, STATUS_SUCCESS);

        default:
            break;
    }

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
    LARGE_INTEGER Delay;
    NTSTATUS Status;

    /* The test opens the child while its stack is being built */
    if (Pdo->DriverObject == DriverObject && Requested(L"SlowAddDevice"))
    {
        Record(L"AddDeviceEntered", 1);
        Delay.QuadPart = -200LL * 10000;
        KeDelayExecutionThread(KernelMode, FALSE, &Delay);
    }

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
    if (Extension->Kind == TestChildFdo)
        Record(L"ChildAddDevices", ++ChildAddDevices);
    return STATUS_SUCCESS;
}

static VOID NTAPI TestUnload(PDRIVER_OBJECT DriverObject)
{
    UNREFERENCED_PARAMETER(DriverObject);
    Record(L"DriverUnloaded", 1);
}

NTSTATUS NTAPI DriverEntry(PDRIVER_OBJECT DriverObject, PUNICODE_STRING RegistryPath)
{
    LARGE_INTEGER Delay;

    if (RegistryPath->Length >= sizeof(ServicePath))
        return STATUS_NAME_TOO_LONG;
    RtlCopyMemory(ServicePath, RegistryPath->Buffer, RegistryPath->Length);
    ServicePath[RegistryPath->Length / sizeof(WCHAR)] = UNICODE_NULL;

    /* The test holds the entry of a load it started while it sets up a device of the driver */
    if (Requested(L"HoldEntry"))
    {
        Record(L"EntryHeld", 1);
        Delay.QuadPart = -200LL * 10000;
        KeDelayExecutionThread(KernelMode, FALSE, &Delay);
    }

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
