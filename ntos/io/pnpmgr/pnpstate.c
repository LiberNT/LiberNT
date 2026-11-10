/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Moving a device node from enumerated to started, and back
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#include "../ioint.h"
#include <umpnpmgr/sysguid.h>
#define NDEBUG
#include <debug.h>

/* GLOBALS ********************************************************************/

KSPIN_LOCK IopDeviceActionLock;
LIST_ENTRY IopDeviceActionRequestList;
BOOLEAN IopDeviceActionInProgress = FALSE;
WORK_QUEUE_ITEM IopDeviceActionWorkItem;

/* PRIVATE DATA ***************************************************************/

static const UNICODE_STRING IopEnumRootName = RTL_CONSTANT_STRING(ENUM_ROOT);

static const UNICODE_STRING IopServicesName = RTL_CONSTANT_STRING(
    L"\\Registry\\Machine\\System\\CurrentControlSet\\Services");

static const UNICODE_STRING IopControlClassName = RTL_CONSTANT_STRING(
    L"\\Registry\\Machine\\System\\CurrentControlSet\\Control\\Class");

static BOOLEAN IopRemovingRenamedStack = FALSE;

/* PRIVATE FUNCTIONS **********************************************************/

BOOLEAN
PiIsRemovingRenamedStack(VOID)
{
    return IopRemovingRenamedStack;
}

/* The bus driver tells which bus the device sits on and its number there */
static
VOID
PiQueryBusInformation(
    _Inout_ PDEVICE_NODE DeviceNode)
{
    IO_STACK_LOCATION StackLocation;
    PPNP_BUS_INFORMATION Information = NULL;
    NTSTATUS Status;

    PAGED_CODE();

    RtlZeroMemory(&StackLocation, sizeof(StackLocation));

    StackLocation.MajorFunction = IRP_MJ_PNP;
    StackLocation.MinorFunction = IRP_MN_QUERY_BUS_INFORMATION;

    Status = IopSynchronousCall(DeviceNode->PhysicalDeviceObject,
                                &StackLocation,
                                (PVOID *)&Information);
    if (!NT_SUCCESS(Status) || Information == NULL) return;

    DeviceNode->ChildBusTypeIndex = IopGetBusTypeGuidIndex(&Information->BusTypeGuid);
    DeviceNode->ChildInterfaceType = Information->LegacyBusType;
    DeviceNode->ChildBusNumber = Information->BusNumber;

    ExFreePool(Information);
}

static
NTSTATUS
PiIrpQueryId(
    _In_ PDEVICE_NODE DeviceNode,
    _In_ BUS_QUERY_ID_TYPE IdType,
    _Out_ PWSTR *Id)
{
    IO_STACK_LOCATION StackLocation;

    PAGED_CODE();

    *Id = NULL;

    RtlZeroMemory(&StackLocation, sizeof(StackLocation));

    StackLocation.MajorFunction = IRP_MJ_PNP;
    StackLocation.MinorFunction = IRP_MN_QUERY_ID;
    StackLocation.Parameters.QueryId.IdType = IdType;

    return IopSynchronousCall(DeviceNode->PhysicalDeviceObject,
                              &StackLocation,
                              (PVOID *)Id);
}

static
NTSTATUS
PiSetMultiSzValue(
    _In_ HANDLE Key,
    _In_ PCWSTR ValueName,
    _In_ PWSTR MultiSz)
{
    UNICODE_STRING Name;
    PWCHAR Cursor = MultiSz;
    ULONG Length = 0;

    if (MultiSz == NULL) return STATUS_SUCCESS;

    while (*Cursor != UNICODE_NULL)
    {
        ULONG One = (ULONG)((wcslen(Cursor) + 1) * sizeof(WCHAR));

        Length += One;
        Cursor += One / sizeof(WCHAR);
    }

    Length += sizeof(WCHAR);

    RtlInitUnicodeString(&Name, ValueName);

    return ZwSetValueKey(Key, &Name, 0, REG_MULTI_SZ, MultiSz, Length);
}

static
NTSTATUS
PiBuildInstancePath(
    _In_ PDEVICE_NODE DeviceNode,
    _In_ PWSTR DeviceId,
    _In_ PWSTR InstanceId,
    _In_ BOOLEAN UniqueId)
{
    UNICODE_STRING Prefix;
    UNICODE_STRING Path;
    USHORT Length;
    PWCHAR Buffer;

    PAGED_CODE();

    RtlInitEmptyUnicodeString(&Prefix, NULL, 0);

    /* An identifier the bus cannot promise is unique is made so by its parent */
    if (!UniqueId)
    {
        IopGetParentIdPrefix(DeviceNode, &Prefix);
    }

    Length = (USHORT)((wcslen(DeviceId) + 1) * sizeof(WCHAR)) +
             Prefix.Length + sizeof(WCHAR) +
             (USHORT)((InstanceId != NULL)
                          ? wcslen(InstanceId) * sizeof(WCHAR)
                          : 0) +
             sizeof(WCHAR);

    Buffer = ExAllocatePoolZero(NonPagedPool, Length, TAG_IO_DEVNODE);
    if (Buffer == NULL)
    {
        if (Prefix.Buffer != NULL) ExFreePool(Prefix.Buffer);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    Path.Buffer = Buffer;
    Path.Length = 0;
    Path.MaximumLength = Length;

    RtlAppendUnicodeToString(&Path, DeviceId);
    RtlAppendUnicodeToString(&Path, L"\\");

    if (Prefix.Length != 0)
    {
        RtlAppendUnicodeStringToString(&Path, &Prefix);
        if (InstanceId != NULL && *InstanceId != UNICODE_NULL)
            RtlAppendUnicodeToString(&Path, L"&");
    }

    if (Prefix.Buffer != NULL) ExFreePool(Prefix.Buffer);

    if (InstanceId != NULL) RtlAppendUnicodeToString(&Path, InstanceId);

    DeviceNode->InstancePath = Path;

    return STATUS_SUCCESS;
}

static
VOID
PiSetDwordValue(
    _In_ HANDLE Key,
    _In_ PCWSTR ValueName,
    _In_ ULONG Data)
{
    UNICODE_STRING Name;

    RtlInitUnicodeString(&Name, ValueName);

    ZwSetValueKey(Key, &Name, 0, REG_DWORD, &Data, sizeof(Data));
}

#define PI_CAPABILITY_RAW_DEVICE_OK 0x00000040

/* The Capabilities value carries the CM_DEVCAP_* bits of cfgmgr32.h */
static
VOID
PiSaveCapabilities(
    _In_ HANDLE InstanceKey,
    _In_ PDEVICE_CAPABILITIES Capabilities)
{
    ULONG Flags = 0;

    if (Capabilities->LockSupported) Flags |= 0x00000001;
    if (Capabilities->EjectSupported) Flags |= 0x00000002;
    if (Capabilities->Removable) Flags |= 0x00000004;
    if (Capabilities->DockDevice) Flags |= 0x00000008;
    if (Capabilities->UniqueID) Flags |= 0x00000010;
    if (Capabilities->SilentInstall) Flags |= 0x00000020;
    if (Capabilities->RawDeviceOK) Flags |= PI_CAPABILITY_RAW_DEVICE_OK;
    if (Capabilities->SurpriseRemovalOK) Flags |= 0x00000080;
    if (Capabilities->HardwareDisabled) Flags |= 0x00000100;
    if (Capabilities->NonDynamic) Flags |= 0x00000200;

    PiSetDwordValue(InstanceKey, L"Capabilities", Flags);

    if (Capabilities->UINumber != MAXULONG)
    {
        PiSetDwordValue(InstanceKey, L"UINumber", Capabilities->UINumber);
    }

    if (Capabilities->Address != MAXULONG)
    {
        PiSetDwordValue(InstanceKey, L"Address", Capabilities->Address);
    }
}

/*
 * Every device belongs to a container: the one its bus names for it, the one
 * its parent belongs to, or, at the root of the tree, the machine itself.
 * The GUID of the machine's container is the one Windows uses for it.
 */
static
VOID
PiSaveContainerId(
    _In_ PDEVICE_NODE DeviceNode,
    _In_ HANDLE InstanceKey)
{
    static CONST UNICODE_STRING ValueName = RTL_CONSTANT_STRING(L"ContainerID");
    static CONST GUID MachineContainer =
        {0x00000000, 0x0000, 0x0000, {0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff}};
    PKEY_VALUE_FULL_INFORMATION Information = NULL;
    UNICODE_STRING ContainerId;
    PDEVICE_NODE Parent;
    HANDLE ParentKey;
    PWSTR Queried = NULL;
    NTSTATUS Status;

    PAGED_CODE();

    /* What the bus says about its child comes first */
    Status = PiIrpQueryId(DeviceNode, BusQueryContainerID, &Queried);
    if (NT_SUCCESS(Status) && Queried != NULL && Queried[0] != UNICODE_NULL)
    {
        RtlInitUnicodeString(&ContainerId, Queried);
        ZwSetValueKey(InstanceKey,
                      (PUNICODE_STRING)&ValueName,
                      0,
                      REG_SZ,
                      ContainerId.Buffer,
                      ContainerId.Length + sizeof(WCHAR));
        ExFreePool(Queried);
        return;
    }

    if (Queried != NULL) ExFreePool(Queried);

    /* Otherwise the device is part of whatever its parent is part of */
    Parent = DeviceNode->Parent;
    if (Parent != NULL && Parent->InstancePath.Length != 0 &&
        NT_SUCCESS(PnpDeviceObjectToDeviceInstance(Parent->PhysicalDeviceObject,
                                                   &ParentKey,
                                                   KEY_READ)))
    {
        Status = IopGetRegistryValue(ParentKey, L"ContainerID", &Information);
        ZwClose(ParentKey);

        if (NT_SUCCESS(Status))
        {
            if (Information->Type == REG_SZ && Information->DataLength != 0)
            {
                ZwSetValueKey(InstanceKey,
                              (PUNICODE_STRING)&ValueName,
                              0,
                              REG_SZ,
                              (PUCHAR)Information + Information->DataOffset,
                              Information->DataLength);
                ExFreePool(Information);
                return;
            }

            ExFreePool(Information);
        }
    }

    Status = RtlStringFromGUID(&MachineContainer, &ContainerId);
    if (!NT_SUCCESS(Status)) return;

    ZwSetValueKey(InstanceKey,
                  (PUNICODE_STRING)&ValueName,
                  0,
                  REG_SZ,
                  ContainerId.Buffer,
                  ContainerId.Length + sizeof(WCHAR));

    RtlFreeUnicodeString(&ContainerId);
}

static
NTSTATUS
PiQueryAndSaveIds(
    _In_ PDEVICE_NODE DeviceNode,
    _Out_ PHANDLE InstanceKey)
{
    PWSTR DeviceId = NULL;
    PWSTR InstanceId = NULL;
    PWSTR HardwareIds = NULL;
    PWSTR CompatibleIds = NULL;
    DEVICE_CAPABILITIES Capabilities;
    BOOLEAN HaveCapabilities;
    BOOLEAN UniqueId;
    UNICODE_STRING KeyPath;
    NTSTATUS Status;
    HANDLE EnumKey;
    PWCHAR Buffer;
    USHORT Length;

    PAGED_CODE();

    *InstanceKey = NULL;

    Status = PiIrpQueryId(DeviceNode, BusQueryDeviceID, &DeviceId);
    if (!NT_SUCCESS(Status) || DeviceId == NULL)
    {
        PiSetDevNodeProblem(DeviceNode, CM_PROB_INVALID_DATA);
        return NT_SUCCESS(Status) ? STATUS_UNSUCCESSFUL : Status;
    }

    Status = PiIrpQueryId(DeviceNode, BusQueryInstanceID, &InstanceId);
    if (!NT_SUCCESS(Status)) InstanceId = NULL;

    UniqueId = FALSE;
    HaveCapabilities = NT_SUCCESS(IopQueryDeviceCapabilities(DeviceNode, &Capabilities));
    if (HaveCapabilities)
    {
        UniqueId = (BOOLEAN)Capabilities.UniqueID;
    }

    Status = PiBuildInstancePath(DeviceNode, DeviceId, InstanceId, UniqueId);

    if (InstanceId != NULL) ExFreePool(InstanceId);
    ExFreePool(DeviceId);

    if (!NT_SUCCESS(Status)) return Status;

    Length = IopEnumRootName.Length + sizeof(WCHAR) +
             DeviceNode->InstancePath.Length + sizeof(WCHAR);

    Buffer = ExAllocatePoolZero(PagedPool, Length, TAG_IO);
    if (Buffer == NULL) return STATUS_INSUFFICIENT_RESOURCES;

    KeyPath.Buffer = Buffer;
    KeyPath.Length = 0;
    KeyPath.MaximumLength = Length;

    RtlAppendUnicodeStringToString(&KeyPath,
                                   (PUNICODE_STRING)&IopEnumRootName);
    RtlAppendUnicodeToString(&KeyPath, L"\\");
    RtlAppendUnicodeStringToString(&KeyPath, &DeviceNode->InstancePath);

    Status = IopCreateDeviceKeyPath(&KeyPath,
                                    REG_OPTION_NON_VOLATILE,
                                    &EnumKey);

    ExFreePoolWithTag(Buffer, TAG_IO);

    if (!NT_SUCCESS(Status)) return Status;

    if (HaveCapabilities) PiSaveCapabilities(EnumKey, &Capabilities);

    Status = PiIrpQueryId(DeviceNode, BusQueryHardwareIDs, &HardwareIds);
    if (NT_SUCCESS(Status) && HardwareIds != NULL)
    {
        PiSetMultiSzValue(EnumKey, L"HardwareID", HardwareIds);
        ExFreePool(HardwareIds);
    }

    Status = PiIrpQueryId(DeviceNode, BusQueryCompatibleIDs, &CompatibleIds);
    if (NT_SUCCESS(Status) && CompatibleIds != NULL)
    {
        PiSetMultiSzValue(EnumKey, L"CompatibleIDs", CompatibleIds);
        ExFreePool(CompatibleIds);
    }

    PiSaveContainerId(DeviceNode, EnumKey);

    /* A device the registry knows no driver for is configured from the driver database */
    IopConfigureDeviceFromDriverDatabase(DeviceNode);

    PiQueryBusInformation(DeviceNode);

    PiSetDevNodeText(DeviceNode, EnumKey);
    IopSetDeviceInstanceData(EnumKey, DeviceNode);

    PiSetDevNodeFlag(DeviceNode, DNF_IDS_QUERIED);

    *InstanceKey = EnumKey;

    return STATUS_SUCCESS;
}

typedef struct _PI_DRIVER_LIST
{
    PDRIVER_OBJECT *Drivers;
    ULONG Count;
    ULONG Capacity;
    ULONG FunctionIndex;
} PI_DRIVER_LIST, *PPI_DRIVER_LIST;

static
NTSTATUS
PiDriverListAppend(
    _Inout_ PPI_DRIVER_LIST List,
    _In_ PDRIVER_OBJECT DriverObject)
{
    PDRIVER_OBJECT *Drivers;
    ULONG Capacity;

    if (List->Count == List->Capacity)
    {
        Capacity = (List->Capacity == 0) ? 4 : List->Capacity * 2;

        Drivers = ExAllocatePoolZero(PagedPool,
                                     Capacity * sizeof(PDRIVER_OBJECT),
                                     TAG_IO_DEVNODE);
        if (Drivers == NULL) return STATUS_INSUFFICIENT_RESOURCES;

        if (List->Drivers != NULL)
        {
            RtlCopyMemory(Drivers, List->Drivers, List->Count * sizeof(PDRIVER_OBJECT));
            ExFreePoolWithTag(List->Drivers, TAG_IO_DEVNODE);
        }

        List->Drivers = Drivers;
        List->Capacity = Capacity;
    }

    List->Drivers[List->Count++] = DriverObject;
    return STATUS_SUCCESS;
}

static
VOID
PiDriverListFree(
    _Inout_ PPI_DRIVER_LIST List)
{
    ULONG Index;

    for (Index = 0; Index < List->Count; Index++)
    {
        ObDereferenceObject(List->Drivers[Index]);
    }

    if (List->Drivers != NULL) ExFreePoolWithTag(List->Drivers, TAG_IO_DEVNODE);

    RtlZeroMemory(List, sizeof(*List));
}

static
NTSTATUS
PiResolveServiceDriver(
    _In_ PUNICODE_STRING ServiceName,
    _Out_ PDRIVER_OBJECT *DriverObject,
    _Out_opt_ PBOOLEAN NoServiceKey)
{
    HANDLE ServicesKey;
    HANDLE ServiceKey;
    NTSTATUS Status;

    *DriverObject = NULL;
    if (NoServiceKey != NULL) *NoServiceKey = FALSE;

    Status = IopOpenRegistryKeyEx(&ServicesKey,
                                  NULL,
                                  (PUNICODE_STRING)&IopServicesName,
                                  KEY_READ);
    if (!NT_SUCCESS(Status)) return Status;

    Status = IopOpenRegistryKeyEx(&ServiceKey, ServicesKey, ServiceName, KEY_READ);
    ZwClose(ServicesKey);
    if (!NT_SUCCESS(Status))
    {
        if (NoServiceKey != NULL) *NoServiceKey = TRUE;
        return Status;
    }

    Status = IopLoadDriverForPnp(ServiceKey, DriverObject);
    ZwClose(ServiceKey);

    return Status;
}

static
NTSTATUS
PiCollectFilters(
    _Inout_ PPI_DRIVER_LIST List,
    _In_opt_ HANDLE Key,
    _In_ PCWSTR ValueName)
{
    PKEY_VALUE_FULL_INFORMATION Information = NULL;
    PUNICODE_STRING Filters = NULL;
    PDRIVER_OBJECT DriverObject;
    ULONG FilterCount = 0;
    NTSTATUS Status;
    ULONG Index;

    PAGED_CODE();

    if (Key == NULL) return STATUS_SUCCESS;

    Status = IopGetRegistryValue(Key, (PWSTR)ValueName, &Information);
    if (!NT_SUCCESS(Status)) return STATUS_SUCCESS;

    Status = PnpRegMultiSzToUnicodeStrings(Information, &Filters, &FilterCount);
    ExFreePool(Information);
    if (!NT_SUCCESS(Status)) return STATUS_SUCCESS;

    for (Index = 0; Index < FilterCount; Index++)
    {
        Status = PiResolveServiceDriver(&Filters[Index], &DriverObject, NULL);
        if (!NT_SUCCESS(Status))
        {
            /* Before the system drivers can load, the whole stack waits for them */
            if (!PnPBootDriversInitialized) break;

            DPRINT1("PnP: the filter %wZ did not load (%lx)\n", &Filters[Index], Status);
            Status = STATUS_SUCCESS;
            continue;
        }

        Status = PiDriverListAppend(List, DriverObject);
        if (!NT_SUCCESS(Status))
        {
            ObDereferenceObject(DriverObject);
            break;
        }
    }

    PnpFreeUnicodeStringList(Filters, FilterCount);
    return Status;
}

static
ULONG
PiGetDwordValue(
    _In_ HANDLE Key,
    _In_ PCWSTR ValueName,
    _In_ ULONG Default)
{
    PKEY_VALUE_FULL_INFORMATION Information = NULL;
    ULONG Value = Default;

    PAGED_CODE();

    if (!NT_SUCCESS(IopGetRegistryValue(Key, (PWSTR)ValueName, &Information)))
        return Default;

    if ((Information->Type == REG_DWORD) &&
        (Information->DataLength == sizeof(ULONG)))
    {
        Value = *(PULONG)((PUCHAR)Information + Information->DataOffset);
    }

    ExFreePool(Information);
    return Value;
}

static
NTSTATUS
PiCollectFunctionDriver(
    _Inout_ PDEVICE_NODE DeviceNode,
    _In_ HANDLE InstanceKey,
    _Inout_ PPI_DRIVER_LIST List)
{
    PKEY_VALUE_FULL_INFORMATION Information = NULL;
    PDRIVER_OBJECT DriverObject;
    UNICODE_STRING ServiceName;
    BOOLEAN NoServiceKey;
    NTSTATUS Status;
    PWCHAR Buffer;

    PAGED_CODE();

    Status = IopGetRegistryValue(InstanceKey, L"Service", &Information);
    if (!NT_SUCCESS(Status)) Information = NULL;

    if (Information == NULL ||
        Information->Type != REG_SZ ||
        Information->DataLength <= sizeof(WCHAR))
    {
        if (Information != NULL) ExFreePool(Information);

        PiSetDevNodeFlag(DeviceNode, DNF_NO_SERVICE);
        return STATUS_SUCCESS;
    }

    PiClearDevNodeFlag(DeviceNode, DNF_NO_SERVICE);

    ServiceName.Buffer = (PWCHAR)((PUCHAR)Information + Information->DataOffset);
    ServiceName.Length = (USHORT)(Information->DataLength - sizeof(WCHAR));
    ServiceName.MaximumLength = ServiceName.Length;

    if (DeviceNode->ServiceName.Buffer == NULL)
    {
        Buffer = ExAllocatePoolZero(NonPagedPool,
                                    ServiceName.Length + sizeof(WCHAR),
                                    TAG_IO_DEVNODE);
        if (Buffer != NULL)
        {
            RtlCopyMemory(Buffer, ServiceName.Buffer, ServiceName.Length);

            DeviceNode->ServiceName.Buffer = Buffer;
            DeviceNode->ServiceName.Length = ServiceName.Length;
            DeviceNode->ServiceName.MaximumLength = ServiceName.Length + sizeof(WCHAR);
        }
    }

    Status = PiResolveServiceDriver(&ServiceName, &DriverObject, &NoServiceKey);
    ExFreePool(Information);

    if (NoServiceKey)
    {
        PiSetDevNodeProblem(DeviceNode, CM_PROB_DEVICE_NOT_THERE);
        return Status;
    }

    if (!NT_SUCCESS(Status))
    {
        /* Only boot drivers run before the system root exists; the device gets another pass */
        if (PnPBootDriversInitialized)
        {
            PiSetDevNodeProblem(DeviceNode, CM_PROB_FAILED_DRIVER_ENTRY);
        }

        return Status;
    }

    Status = PiDriverListAppend(List, DriverObject);
    if (!NT_SUCCESS(Status))
    {
        ObDereferenceObject(DriverObject);
        return Status;
    }

    List->FunctionIndex = List->Count - 1;
    return STATUS_SUCCESS;
}

static
HANDLE
PiOpenDeviceClassKey(
    _In_ HANDLE InstanceKey)
{
    PKEY_VALUE_FULL_INFORMATION Information = NULL;
    UNICODE_STRING ClassName;
    HANDLE ClassRoot;
    HANDLE ClassKey = NULL;
    NTSTATUS Status;

    PAGED_CODE();

    Status = IopGetRegistryValue(InstanceKey, L"ClassGUID", &Information);
    if (!NT_SUCCESS(Status)) return NULL;

    if (Information->Type == REG_SZ && Information->DataLength > sizeof(WCHAR))
    {
        ClassName.Buffer = (PWCHAR)((PUCHAR)Information + Information->DataOffset);
        ClassName.Length = (USHORT)(Information->DataLength - sizeof(WCHAR));
        ClassName.MaximumLength = ClassName.Length;

        Status = IopOpenRegistryKeyEx(&ClassRoot,
                                      NULL,
                                      (PUNICODE_STRING)&IopControlClassName,
                                      KEY_READ);
        if (NT_SUCCESS(Status))
        {
            Status = IopOpenRegistryKeyEx(&ClassKey, ClassRoot, &ClassName, KEY_READ);
            if (!NT_SUCCESS(Status)) ClassKey = NULL;

            ZwClose(ClassRoot);
        }
    }

    ExFreePool(Information);
    return ClassKey;
}

/* Every driver of the stack is found before any of them sees the device */
static
NTSTATUS
PiCollectDeviceDrivers(
    _Inout_ PDEVICE_NODE DeviceNode,
    _In_ HANDLE InstanceKey,
    _In_opt_ HANDLE ClassKey,
    _Inout_ PPI_DRIVER_LIST List)
{
    NTSTATUS Status;

    Status = PiCollectFilters(List, InstanceKey, L"LowerFilters");
    if (!NT_SUCCESS(Status)) return Status;

    Status = PiCollectFilters(List, ClassKey, L"LowerFilters");
    if (!NT_SUCCESS(Status)) return Status;

    Status = PiCollectFunctionDriver(DeviceNode, InstanceKey, List);
    if (!NT_SUCCESS(Status)) return Status;

    Status = PiCollectFilters(List, InstanceKey, L"UpperFilters");
    if (!NT_SUCCESS(Status)) return Status;

    return PiCollectFilters(List, ClassKey, L"UpperFilters");
}

static
NTSTATUS
PiAttachDeviceDrivers(
    _In_ PDEVICE_NODE DeviceNode,
    _In_ HANDLE InstanceKey)
{
    PDRIVER_ADD_DEVICE AddDevice;
    PI_DRIVER_LIST List;
    NTSTATUS AddStatus;
    NTSTATUS Status;
    HANDLE ClassKey;
    ULONG Index;

    PAGED_CODE();

    RtlZeroMemory(&List, sizeof(List));
    List.FunctionIndex = MAXULONG;

    ClassKey = PiOpenDeviceClassKey(InstanceKey);

    Status = PiCollectDeviceDrivers(DeviceNode, InstanceKey, ClassKey, &List);

    /*
     * A device with nothing to drive it is not a device that runs: it waits
     * for an installation, and only a bus that says its children may run raw
     * lets one start without a function driver.
     */
    if (NT_SUCCESS(Status) &&
        (List.FunctionIndex == MAXULONG) &&
        !(PiGetDwordValue(InstanceKey, L"Capabilities", 0) & PI_CAPABILITY_RAW_DEVICE_OK))
    {
        PiSetDevNodeProblem(DeviceNode, CM_PROB_FAILED_INSTALL);
    }

    for (Index = 0; NT_SUCCESS(Status) && Index < List.Count; Index++)
    {
        AddDevice = IoGetDrvObjExtension(List.Drivers[Index])->AddDevice;
        if (AddDevice == NULL) continue;

        AddStatus = AddDevice(List.Drivers[Index], DeviceNode->PhysicalDeviceObject);
        if (Index == List.FunctionIndex && !NT_SUCCESS(AddStatus))
        {
            PiSetDevNodeProblem(DeviceNode, CM_PROB_FAILED_ADD);
            Status = AddStatus;
        }
    }

    PiDriverListFree(&List);

    if (ClassKey != NULL) ZwClose(ClassKey);

    return Status;
}

VOID
PiDevNodeAddDrivers(
    _In_ PDEVICE_NODE DeviceNode)
{
    NTSTATUS Status;
    HANDLE InstanceKey = NULL;

    PAGED_CODE();

    if (!(DeviceNode->Flags & DNF_IDS_QUERIED))
    {
        Status = PiQueryAndSaveIds(DeviceNode, &InstanceKey);
        if (!NT_SUCCESS(Status)) return;
    }
    else
    {
        Status = PnpDeviceObjectToDeviceInstance(
            DeviceNode->PhysicalDeviceObject,
            &InstanceKey,
            KEY_ALL_ACCESS);
        if (!NT_SUCCESS(Status)) return;
    }

    Status = PiAttachDeviceDrivers(DeviceNode, InstanceKey);

    ZwClose(InstanceKey);

    if (NT_SUCCESS(Status) && !(DeviceNode->Flags & DNF_HAS_PROBLEM))
    {
        PiSetDevNodeState(DeviceNode, DeviceNodeDriversAdded);
        return;
    }

    PiSetDevNodeState(DeviceNode, DeviceNodeInitialized);
}

static
VOID
PiProcessStartDevice(
    _In_ PDEVICE_NODE DeviceNode)
{
    NTSTATUS Status;

    PAGED_CODE();

    Status = IopAssignDeviceResources(DeviceNode);
    if (!NT_SUCCESS(Status))
    {
        PiSetDevNodeProblem(DeviceNode, CM_PROB_NORMAL_CONFLICT);
        return;
    }

    PiSetDevNodeState(DeviceNode, DeviceNodeResourcesAssigned);

    PiSetDevNodeState(DeviceNode, DeviceNodeStartPending);

    Status = PiIrpStartDevice(DeviceNode);

    PiSetDevNodeState(DeviceNode, DeviceNodeStartCompletion);

    if (!NT_SUCCESS(Status))
    {
        DPRINT1("PnP: %wZ did not start (%lx)\n",
                &DeviceNode->InstancePath,
                Status);

        IopResDbRelease(DeviceNode);
        PiSetDevNodeProblem(DeviceNode, CM_PROB_FAILED_START);
        PiSetDevNodeState(DeviceNode, DeviceNodeDriversAdded);

        return;
    }

    PiSetDevNodeState(DeviceNode, DeviceNodeStartPostWork);

    IopUpdateResourceMapForPnPDevice(DeviceNode);

    PiSetDevNodeState(DeviceNode, DeviceNodeStarted);

    InbvIndicateProgress();

    /* A device that just started is asked for its children */
    PiSetDevNodeFlag(DeviceNode, DNF_REENUMERATE);

    IopQueueDeviceInstallEvent(&GUID_DEVICE_ARRIVAL,
                               &DeviceNode->InstancePath);
}

static
VOID
PiProcessNewChildren(
    _In_ PDEVICE_NODE DeviceNode)
{
    PDEVICE_RELATIONS Relations;
    PDEVICE_NODE ChildNode;
    PDEVICE_OBJECT Child;
    ULONG Index;

    PAGED_CODE();

    Relations = DeviceNode->OverUsed1.PendingDeviceRelations;
    if (Relations == NULL) return;

    DeviceNode->OverUsed1.PendingDeviceRelations = NULL;

    for (Index = 0; Index < Relations->Count; Index++)
    {
        Child = Relations->Objects[Index];

        ChildNode = IopGetDeviceNode(Child);
        if (ChildNode != NULL)
        {
            ObDereferenceObject(Child);
            continue;
        }

        ChildNode = PipAllocateDeviceNode(Child);
        if (ChildNode == NULL)
        {
            ObDereferenceObject(Child);
            continue;
        }

        PiSetDevNodeFlag(ChildNode, DNF_ENUMERATED);
        PiInsertDevNode(ChildNode, DeviceNode);

        Child->Flags |= DO_BUS_ENUMERATED_DEVICE;

        ObDereferenceObject(Child);
    }

    ExFreePool(Relations);
}

static
VOID
PiProcessEnumeration(
    _In_ PDEVICE_NODE DeviceNode)
{
    NTSTATUS Status;

    PAGED_CODE();

    PiSetDevNodeState(DeviceNode, DeviceNodeEnumeratePending);

    Status = PiIrpQueryDeviceRelations(DeviceNode, BusRelations);

    PiSetDevNodeState(DeviceNode, DeviceNodeEnumerateCompletion);

    if (NT_SUCCESS(Status)) PiProcessNewChildren(DeviceNode);

    PiClearDevNodeFlag(DeviceNode, DNF_REENUMERATE);
    PiSetDevNodeState(DeviceNode, DeviceNodeStarted);
}

static
VOID
PiProcessRemoval(
    _In_ PDEVICE_NODE DeviceNode,
    _In_ BOOLEAN Surprise)
{
    IO_STACK_LOCATION StackLocation;
    PDEVICE_NODE Child;
    PDEVICE_NODE Next;

    PAGED_CODE();

    Child = DeviceNode->Child;
    while (Child != NULL)
    {
        Next = Child->Sibling;
        PiProcessRemoval(Child, Surprise);
        Child = Next;
    }

    RtlZeroMemory(&StackLocation, sizeof(StackLocation));

    StackLocation.MajorFunction = IRP_MJ_PNP;
    StackLocation.MinorFunction = Surprise ? IRP_MN_SURPRISE_REMOVAL
                                           : IRP_MN_REMOVE_DEVICE;

    IopSynchronousCall(DeviceNode->PhysicalDeviceObject,
                       &StackLocation,
                       NULL);

    IopDisableDeviceInterfaces(DeviceNode);
    IopResDbRelease(DeviceNode);

    PiSetDevNodeState(DeviceNode, DeviceNodeRemoved);

    IopQueueDeviceInstallEvent(Surprise ? &GUID_DEVICE_SURPRISE_REMOVAL
                                        : &GUID_DEVICE_SAFE_REMOVAL,
                               &DeviceNode->InstancePath);
}

VOID
PiEnumerationFinished(
    _In_opt_ PDEVICE_NODE DeviceNode)
{
    UNREFERENCED_PARAMETER(DeviceNode);

    if (!PnPBootDriversInitialized) return;

    IopQueueDeviceInstallEvent(&GUID_DEVICE_ENUMERATED, NULL);
}

VOID
PiDevNodeStateMachine(
    _In_ PDEVICE_NODE RootNode)
{
    PDEVICE_NODE Node = RootNode;
    BOOLEAN Progress;

    PAGED_CODE();

    while (TRUE)
    {
        Progress = FALSE;

        switch (Node->State)
        {
            case DeviceNodeUninitialized:
                PiSetDevNodeState(Node, DeviceNodeInitialized);
                Progress = TRUE;
                break;

            case DeviceNodeInitialized:
                if (Node->Flags & DNF_HAS_PROBLEM) break;

                PiDevNodeAddDrivers(Node);
                Progress = (Node->State == DeviceNodeDriversAdded);
                break;

            case DeviceNodeDriversAdded:
                if (Node->Flags & DNF_HAS_PROBLEM) break;

                PiProcessStartDevice(Node);
                Progress = (Node->State == DeviceNodeStarted);
                break;

            case DeviceNodeStarted:
                if (Node->Flags & DNF_REENUMERATE)
                {
                    PiProcessEnumeration(Node);
                    Progress = TRUE;
                }
                break;

            case DeviceNodeAwaitingQueuedRemoval:
                PiProcessRemoval(Node, FALSE);
                Progress = TRUE;
                break;

            case DeviceNodeAwaitingQueuedDeletion:
                PiProcessRemoval(Node, TRUE);
                Progress = TRUE;
                break;

            default:
                break;
        }

        if (Progress) continue;

        if (Node->Child != NULL)
        {
            Node = Node->Child;
            continue;
        }

        while (Node != RootNode && Node->Sibling == NULL)
        {
            Node = Node->Parent;
            if (Node == NULL) return;
        }

        if (Node == RootNode) break;

        Node = Node->Sibling;
    }

    PiEnumerationFinished(RootNode);
}

/* DEVICE ACTIONS *************************************************************/

static
VOID
PiRunDeviceAction(
    _In_ PIOP_DEVICE_ACTION_REQUEST Request)
{
    PDEVICE_NODE DeviceNode;
    PI_QUERY_REMOVE_DATA RemoveData;
    NTSTATUS Status = STATUS_SUCCESS;

    PAGED_CODE();

    DeviceNode = IopGetDeviceNode(Request->DeviceObject);
    if (DeviceNode == NULL)
    {
        Status = STATUS_INVALID_DEVICE_REQUEST;
    }
    else
    {
        switch (Request->Action)
        {
            case PiActionEnumRootDevices:
                PiSetDevNodeFlag(DeviceNode, DNF_REENUMERATE);
                PiDevNodeStateMachine(DeviceNode);
                break;

            case PiActionEnumDeviceTree:
                PiDevNodeStateMachine(DeviceNode);
                break;

            case PiActionAddBootDevices:
                if (DeviceNode->State == DeviceNodeInitialized)
                {
                    PiDevNodeAddDrivers(DeviceNode);
                }
                break;

            case PiActionStartDevice:
                if (DeviceNode->State == DeviceNodeDriversAdded)
                {
                    PiProcessStartDevice(DeviceNode);
                }
                break;

            case PiActionResetDevice:
                PiClearDevNodeProblem(DeviceNode);
                PiSetDevNodeState(DeviceNode, DeviceNodeUninitialized);
                PiDevNodeStateMachine(DeviceNode);
                break;

            case PiActionQueryState:
            {
                PNP_DEVICE_STATE DeviceState = 0;

                Status = PiIrpQueryPnPDeviceState(DeviceNode, &DeviceState);
                if (!NT_SUCCESS(Status)) break;

                if (DeviceState & PNP_DEVICE_DONT_DISPLAY_IN_UI)
                {
                    IopDeviceNodeSetUserFlag(DeviceNode,
                                             DNUF_DONT_SHOW_IN_UI);
                }

                if (DeviceState & PNP_DEVICE_NOT_DISABLEABLE)
                {
                    IopDeviceNodeSetUserFlag(DeviceNode,
                                             DNUF_NOT_DISABLEABLE);
                }

                if (DeviceState & PNP_DEVICE_REMOVED)
                {
                    PiSetDevNodeState(DeviceNode,
                                      DeviceNodeAwaitingQueuedRemoval);
                    PiDevNodeStateMachine(DeviceNode);
                }

                break;
            }

            case PiActionQueryRemoveDevice:
                Status = PiQueryRemoveDevice(Request->DeviceObject,
                                             &RemoveData);
                if (Request->RemoveData != NULL)
                    *Request->RemoveData = RemoveData;
                break;

            case PiActionRemoveDevice:
                PiSetDevNodeState(DeviceNode,
                                  DeviceNodeAwaitingQueuedRemoval);
                PiDevNodeStateMachine(DeviceNode);
                break;

            case PiActionCloseRemovedDevice:
                if (DeviceNode->State == DeviceNodeRemovePendingCloses ||
                    DeviceNode->State == DeviceNodeDeletePendingCloses)
                {
                    PiSetDevNodeState(DeviceNode, DeviceNodeRemoved);
                }
                break;

            case PiActionEjectDevice:
                Status = PiQueryRemoveDevice(Request->DeviceObject,
                                             &RemoveData);
                if (!NT_SUCCESS(Status)) break;

                PiSetDevNodeState(DeviceNode,
                                  DeviceNodeAwaitingQueuedRemoval);
                PiDevNodeStateMachine(DeviceNode);
                break;

            default:
                Status = STATUS_INVALID_PARAMETER;
                break;
        }
    }

    if (Request->CompletionStatus != NULL)
    {
        *Request->CompletionStatus = Status;
    }

    if (Request->CompletionEvent != NULL)
    {
        KeSetEvent(Request->CompletionEvent, IO_NO_INCREMENT, FALSE);
    }

    ObDereferenceObject(Request->DeviceObject);
    ExFreePoolWithTag(Request, TAG_PNP_DEVACTION);
}

static
VOID
NTAPI
PiDeviceActionWorker(
    _In_ PVOID Context)
{
    PIOP_DEVICE_ACTION_REQUEST Request;
    PLIST_ENTRY Entry;
    KIRQL OldIrql;

    PAGED_CODE();

    UNREFERENCED_PARAMETER(Context);

    while (TRUE)
    {
        KeAcquireSpinLock(&IopDeviceActionLock, &OldIrql);

        if (IsListEmpty(&IopDeviceActionRequestList))
        {
            IopDeviceActionInProgress = FALSE;
            KeReleaseSpinLock(&IopDeviceActionLock, OldIrql);
            return;
        }

        Entry = RemoveHeadList(&IopDeviceActionRequestList);

        KeReleaseSpinLock(&IopDeviceActionLock, OldIrql);

        Request = CONTAINING_RECORD(Entry,
                                    IOP_DEVICE_ACTION_REQUEST,
                                    ListEntry);

        PiRunDeviceAction(Request);
    }
}

VOID
PiQueueDeviceAction(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ DEVICE_ACTION Action,
    _In_opt_ PKEVENT CompletionEvent,
    _Out_opt_ NTSTATUS *CompletionStatus)
{
    PiQueueDeviceActionEx(DeviceObject,
                          Action,
                          CompletionEvent,
                          CompletionStatus,
                          NULL);
}

/* The same, for an action whose answer is more than a status */
VOID
PiQueueDeviceActionEx(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ DEVICE_ACTION Action,
    _In_opt_ PKEVENT CompletionEvent,
    _Out_opt_ NTSTATUS *CompletionStatus,
    _Out_opt_ PPI_QUERY_REMOVE_DATA RemoveData)
{
    PIOP_DEVICE_ACTION_REQUEST Request;
    BOOLEAN Queue = FALSE;
    KIRQL OldIrql;

    Request = ExAllocatePoolZero(NonPagedPool,
                                 sizeof(IOP_DEVICE_ACTION_REQUEST),
                                 TAG_PNP_DEVACTION);
    if (Request == NULL)
    {
        if (CompletionStatus != NULL)
        {
            *CompletionStatus = STATUS_INSUFFICIENT_RESOURCES;
        }

        if (CompletionEvent != NULL)
        {
            KeSetEvent(CompletionEvent, IO_NO_INCREMENT, FALSE);
        }

        return;
    }

    ObReferenceObject(DeviceObject);

    Request->DeviceObject = DeviceObject;
    Request->Action = Action;
    Request->CompletionEvent = CompletionEvent;
    Request->CompletionStatus = CompletionStatus;
    Request->RemoveData = RemoveData;

    KeAcquireSpinLock(&IopDeviceActionLock, &OldIrql);

    InsertTailList(&IopDeviceActionRequestList, &Request->ListEntry);

    if (!IopDeviceActionInProgress)
    {
        IopDeviceActionInProgress = TRUE;
        Queue = TRUE;
    }

    KeReleaseSpinLock(&IopDeviceActionLock, OldIrql);

    if (!Queue) return;

    ExInitializeWorkItem(&IopDeviceActionWorkItem,
                         PiDeviceActionWorker,
                         NULL);
    ExQueueWorkItem(&IopDeviceActionWorkItem, DelayedWorkQueue);
}

NTSTATUS
PiPerformSyncDeviceAction(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ DEVICE_ACTION Action)
{
    return PiPerformSyncDeviceActionEx(DeviceObject, Action, NULL);
}

/* The same, for an action whose answer is more than a status */
NTSTATUS
PiPerformSyncDeviceActionEx(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ DEVICE_ACTION Action,
    _Out_opt_ PPI_QUERY_REMOVE_DATA RemoveData)
{
    NTSTATUS Status = STATUS_UNSUCCESSFUL;
    KEVENT Event;

    PAGED_CODE();

    KeInitializeEvent(&Event, NotificationEvent, FALSE);

    PiQueueDeviceActionEx(DeviceObject, Action, &Event, &Status, RemoveData);

    KeWaitForSingleObject(&Event, Executive, KernelMode, FALSE, NULL);

    return Status;
}
