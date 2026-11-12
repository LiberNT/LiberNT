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

/* A device plugged in again while its old stack still waits for its last close takes the instance name, and the
 * old node waits on without one (Windows 11) */
static
VOID
PiTakeInstanceName(
    _In_ PDEVICE_NODE DeviceNode)
{
    PDEVICE_NODE Node = IopRootDeviceNode;

    PAGED_CODE();

    KeEnterCriticalRegion();
    ExAcquireResourceExclusiveLite(&IopDeviceTreeResource, TRUE);

    while (Node != NULL)
    {
        if (Node != DeviceNode &&
            (Node->State == DeviceNodeRemovePendingCloses || Node->State == DeviceNodeDeletePendingCloses) &&
            PiCompareInstancePath(&Node->InstancePath, &DeviceNode->InstancePath) == 0)
        {
            Node->InstancePath.Length = 0;
        }

        if (Node->Child != NULL)
        {
            Node = Node->Child;
            continue;
        }

        while (Node != NULL && Node->Sibling == NULL)
        {
            Node = Node->Parent;
        }

        if (Node != NULL) Node = Node->Sibling;
    }

    ExReleaseResourceLite(&IopDeviceTreeResource);
    KeLeaveCriticalRegion();
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

    /* A removed node that is set up again keeps the instance path it was enumerated with */
    if (DeviceNode->InstancePath.Length != 0)
    {
        Status = STATUS_SUCCESS;
    }
    else
    {
        Status = PiBuildInstancePath(DeviceNode, DeviceId, InstanceId, UniqueId);
        if (NT_SUCCESS(Status)) PiTakeInstanceName(DeviceNode);
    }

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

    /* The capability query came before the bus was known */
    if (HaveCapabilities) PiUpdateRemovalPolicy(DeviceNode, (BOOLEAN)Capabilities.Removable);

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
    _In_ BOOLEAN SafeBootClassAllowed,
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

    Status = IopLoadDriverForDevice(ServiceKey, SafeBootClassAllowed, DriverObject);
    ZwClose(ServiceKey);

    return Status;
}

static
NTSTATUS
PiCollectFilters(
    _Inout_ PPI_DRIVER_LIST List,
    _In_opt_ HANDLE Key,
    _In_ PCWSTR ValueName,
    _In_ BOOLEAN SafeBootClassAllowed)
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
        Status = PiResolveServiceDriver(&Filters[Index], SafeBootClassAllowed, &DriverObject, NULL);
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
    _In_ BOOLEAN SafeBootClassAllowed,
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

    Status = PiResolveServiceDriver(&ServiceName, SafeBootClassAllowed, &DriverObject, &NoServiceKey);
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
            PiSetDevNodeProblem(DeviceNode,
                                Status == STATUS_IMAGE_ALREADY_LOADED ?
                                CM_PROB_DRIVER_FAILED_PRIOR_UNLOAD : CM_PROB_FAILED_DRIVER_ENTRY);
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

/* In safe mode a device of a listed class may load every driver of its stack */
static
BOOLEAN
PiIsSafeBootDeviceClass(
    _In_ HANDLE InstanceKey)
{
    PKEY_VALUE_FULL_INFORMATION Information = NULL;
    UNICODE_STRING ClassGuid;
    BOOLEAN Allowed = FALSE;

    PAGED_CODE();

    if (InitSafeBootMode == 0) return FALSE;

    if (!NT_SUCCESS(IopGetRegistryValue(InstanceKey, L"ClassGUID", &Information))) return FALSE;

    if ((Information->Type == REG_SZ) && (Information->DataLength > sizeof(WCHAR)))
    {
        ClassGuid.Buffer = (PWCHAR)((PUCHAR)Information + Information->DataOffset);
        ClassGuid.Length = (USHORT)(Information->DataLength - sizeof(WCHAR));
        ClassGuid.MaximumLength = ClassGuid.Length;

        Allowed = IopIsSafeBootDeviceClass(&ClassGuid);
    }

    ExFreePool(Information);
    return Allowed;
}

/* Every driver of the stack is found before any of them sees the device */
static
NTSTATUS
PiCollectDeviceDrivers(
    _Inout_ PDEVICE_NODE DeviceNode,
    _In_ HANDLE InstanceKey,
    _In_opt_ HANDLE ClassKey,
    _In_ BOOLEAN SafeBootClassAllowed,
    _Inout_ PPI_DRIVER_LIST List)
{
    NTSTATUS Status;

    Status = PiCollectFilters(List, InstanceKey, L"LowerFilters", SafeBootClassAllowed);
    if (!NT_SUCCESS(Status)) return Status;

    Status = PiCollectFilters(List, ClassKey, L"LowerFilters", SafeBootClassAllowed);
    if (!NT_SUCCESS(Status)) return Status;

    Status = PiCollectFunctionDriver(DeviceNode, InstanceKey, SafeBootClassAllowed, List);
    if (!NT_SUCCESS(Status)) return Status;

    Status = PiCollectFilters(List, InstanceKey, L"UpperFilters", SafeBootClassAllowed);
    if (!NT_SUCCESS(Status)) return Status;

    return PiCollectFilters(List, ClassKey, L"UpperFilters", SafeBootClassAllowed);
}

/* An installation configured the device: it has its driver key and is not marked for another one */
static
BOOLEAN
PiIsDeviceConfigured(
    _In_ HANDLE InstanceKey,
    _In_ ULONG ConfigFlags)
{
    PKEY_VALUE_FULL_INFORMATION Information;
    BOOLEAN Configured;

    PAGED_CODE();

    if (ConfigFlags & (CONFIGFLAG_REINSTALL | CONFIGFLAG_FAILEDINSTALL)) return FALSE;
    if (!NT_SUCCESS(IopGetRegistryValue(InstanceKey, L"Driver", &Information))) return FALSE;

    Configured = (Information->Type == REG_SZ) && (Information->DataLength > sizeof(WCHAR));
    ExFreePool(Information);
    return Configured;
}

static
NTSTATUS
PiAttachDeviceDrivers(
    _In_ PDEVICE_NODE DeviceNode,
    _In_ HANDLE InstanceKey,
    _Out_ PBOOLEAN NullDriver)
{
    PDRIVER_ADD_DEVICE AddDevice;
    PI_DRIVER_LIST List;
    NTSTATUS AddStatus;
    NTSTATUS Status;
    ULONG ConfigFlags;
    HANDLE ClassKey;
    ULONG Index;

    PAGED_CODE();

    *NullDriver = FALSE;

    RtlZeroMemory(&List, sizeof(List));
    List.FunctionIndex = MAXULONG;

    ClassKey = PiOpenDeviceClassKey(InstanceKey);

    Status = PiCollectDeviceDrivers(DeviceNode,
                                    InstanceKey,
                                    ClassKey,
                                    PiIsSafeBootDeviceClass(InstanceKey),
                                    &List);

    /*
     * A device with nothing to drive it is not a device that runs: it waits
     * for an installation, and only a bus that says its children may run raw
     * lets one start without a function driver.
     */
    if (NT_SUCCESS(Status) &&
        (List.FunctionIndex == MAXULONG) &&
        !(PiGetDwordValue(InstanceKey, L"Capabilities", 0) & PI_CAPABILITY_RAW_DEVICE_OK))
    {
        ConfigFlags = PiGetDwordValue(InstanceKey, L"ConfigFlags", 0);

        /* Configured with a null driver (AddService = ,2): Windows 11 leaves it down, without a problem */
        if (PiIsDeviceConfigured(InstanceKey, ConfigFlags))
        {
            *NullDriver = TRUE;
        }
        else
        {
            /* FIXME: Windows 11 leaves a device user mode created under the root without a problem until it is set up */
            PiSetDevNodeProblem(DeviceNode, CM_PROB_FAILED_INSTALL);

            /* Windows 11 never offers a device user mode created under the root (CM_Create_DevInst) */
            if (!(ConfigFlags & CONFIGFLAG_FAILEDINSTALL) && DeviceNode->Parent != IopRootDeviceNode)
            {
                IopQueueDeviceInstallEvent(&GUID_DEVICE_ENUMERATED, &DeviceNode->InstancePath);
            }
        }

        /* A device that cannot run gets no stack, its filters included */
        List.Count = 0;
    }

    /* Drivers are added to a bare device only: a failed stack was removed before the device starts over */
    ASSERT(!NT_SUCCESS(Status) || List.Count == 0 || DeviceNode->PhysicalDeviceObject->AttachedDevice == NULL);

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

#define SDDL_REVISION_1 1

/* A security descriptor the device's registry holds covers every device object of its stack once its drivers are
 * added: its DACL, and its integrity label when it has one (SPDRP_SECURITY) */
static
NTSTATUS
PiApplyDeviceSecurity(
    _In_ PDEVICE_NODE DeviceNode,
    _In_ HANDLE InstanceKey,
    _In_ BOOLEAN WholeStack)
{
    UNICODE_STRING ParametersName = RTL_CONSTANT_STRING(L"Device Parameters");
    SECURITY_INFORMATION SecurityInformation = DACL_SECURITY_INFORMATION;
    PKEY_VALUE_FULL_INFORMATION Information = NULL;
    PSECURITY_DESCRIPTOR Converted = NULL;
    PSECURITY_DESCRIPTOR Descriptor;
    PDEVICE_OBJECT UpperDevice;
    HANDLE ParametersKey;
    BOOLEAN SaclPresent, SaclDefaulted;
    PACE_HEADER Ace;
    PWSTR Sddl;
    PACL Sacl;
    ULONG Index;
    NTSTATUS Status;

    PAGED_CODE();

    if (NT_SUCCESS(IopOpenRegistryKeyEx(&ParametersKey, InstanceKey, &ParametersName, KEY_QUERY_VALUE)))
    {
        if (!NT_SUCCESS(IopGetRegistryValue(ParametersKey, REGSTR_VAL_DEVICE_SECURITY_DESCRIPTOR, &Information)))
            Information = NULL;
        ZwClose(ParametersKey);
    }
    if (Information == NULL &&
        !NT_SUCCESS(IopGetRegistryValue(InstanceKey, REGSTR_VAL_DEVICE_SECURITY_DESCRIPTOR, &Information)))
    {
        /* TODO: the class-wide security of Control\Class\{guid}\Properties is applied only by IoCreateDeviceSecure */
        return STATUS_SUCCESS;
    }

    Descriptor = (PUCHAR)Information + Information->DataOffset;

    if (Information->Type == REG_BINARY)
    {
        Status = RtlValidRelativeSecurityDescriptor(Descriptor, Information->DataLength, DACL_SECURITY_INFORMATION)
                     ? STATUS_SUCCESS
                     : STATUS_INVALID_SECURITY_DESCR;
    }
    else if (Information->Type == REG_SZ && Information->DataLength >= sizeof(WCHAR) &&
             (Information->DataLength % sizeof(WCHAR)) == 0)
    {
        Sddl = ExAllocatePoolZero(PagedPool, Information->DataLength + sizeof(WCHAR), TAG_IO);
        Status = (Sddl != NULL) ? STATUS_SUCCESS : STATUS_INSUFFICIENT_RESOURCES;
        if (Sddl != NULL)
        {
            RtlCopyMemory(Sddl, Descriptor, Information->DataLength);
            Status = SeConvertStringSecurityDescriptorToSecurityDescriptor(Sddl, SDDL_REVISION_1, &Converted, NULL);
            ExFreePoolWithTag(Sddl, TAG_IO);
        }
        Descriptor = Converted;
    }
    else
    {
        Status = STATUS_OBJECT_TYPE_MISMATCH;
    }

    if (NT_SUCCESS(Status))
        Status = RtlGetSaclSecurityDescriptor(Descriptor, &SaclPresent, &Sacl, &SaclDefaulted);

    for (Index = 0; NT_SUCCESS(Status) && SaclPresent && Sacl != NULL && Index < Sacl->AceCount; Index++)
    {
        Status = RtlGetAce(Sacl, Index, (PVOID *)&Ace);
        if (NT_SUCCESS(Status) && Ace->AceType == SYSTEM_MANDATORY_LABEL_ACE_TYPE)
            SecurityInformation |= LABEL_SECURITY_INFORMATION;
    }

    if (NT_SUCCESS(Status))
    {
        if (WholeStack)
        {
            UpperDevice = IoGetAttachedDeviceReference(DeviceNode->PhysicalDeviceObject);
        }
        else
        {
            UpperDevice = DeviceNode->PhysicalDeviceObject;
            ObReferenceObject(UpperDevice);
        }
        Status = IopSetDeviceSecurityDescriptors(UpperDevice,
                                                 DeviceNode->PhysicalDeviceObject,
                                                 &SecurityInformation,
                                                 Descriptor,
                                                 PagedPool,
                                                 &IopFileMapping);
        ObDereferenceObject(UpperDevice);
    }

    if (Converted != NULL) ExFreePoolWithTag(Converted, TAG_SD);
    ExFreePool(Information);

    return Status;
}

static
VOID
PiRemoveFailedStack(
    _In_ PDEVICE_NODE DeviceNode);

VOID
PiDevNodeAddDrivers(
    _In_ PDEVICE_NODE DeviceNode)
{
    NTSTATUS Security = STATUS_SUCCESS;
    NTSTATUS Status;
    HANDLE InstanceKey = NULL;
    BOOLEAN NullDriver;

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

    /* The PDO is secured before any driver sees it, so the device is never open to all while its stack is built */
    Security = PiApplyDeviceSecurity(DeviceNode, InstanceKey, FALSE);
    if (!NT_SUCCESS(Security))
    {
        DPRINT1("PnP: %wZ has a security descriptor that cannot be applied (%lx)\n", &DeviceNode->InstancePath, Security);
        PiSetDevNodeProblem(DeviceNode, CM_PROB_REGISTRY);
        ZwClose(InstanceKey);
        return;
    }

    Status = PiAttachDeviceDrivers(DeviceNode, InstanceKey, &NullDriver);
    if (NT_SUCCESS(Status) && !(DeviceNode->Flags & DNF_HAS_PROBLEM))
        Security = PiApplyDeviceSecurity(DeviceNode, InstanceKey, TRUE);

    ZwClose(InstanceKey);

    /*
     * A driver that asked to go on initializing is called back once the whole
     * stack has loaded and seen the device, not before.
     */
    if (IopSystemDriversInitialized) IopReinitializeDrivers();

    if (NT_SUCCESS(Status) && !(DeviceNode->Flags & DNF_HAS_PROBLEM))
    {
        /* Drivers whose device cannot get the security its registry holds are removed, and the device does not start */
        if (!NT_SUCCESS(Security))
        {
            DPRINT1("PnP: %wZ has a security descriptor that cannot be applied (%lx)\n", &DeviceNode->InstancePath, Security);
            PiSetDevNodeProblem(DeviceNode, CM_PROB_REGISTRY);
            PiRemoveFailedStack(DeviceNode);
            return;
        }
        PiSetDevNodeState(DeviceNode, NullDriver ? DeviceNodeInitialized : DeviceNodeDriversAdded);
        return;
    }

    /* The drivers a failed AddDevice left attached are removed */
    if ((DeviceNode->Flags & DNF_HAS_PROBLEM) && (DeviceNode->PhysicalDeviceObject->AttachedDevice != NULL))
    {
        PiRemoveFailedStack(DeviceNode);
        return;
    }

    /* Only a null driver keeps the state machine from looking at the device again */
    PiClearDevNodeFlag(DeviceNode, DNF_NO_SERVICE);
    PiSetDevNodeState(DeviceNode, DeviceNodeInitialized);
}

/* A device that no longer runs gives back what it was started with, its message interrupts included */
static
VOID
PiReleaseDeviceResources(
    _In_ PDEVICE_NODE DeviceNode)
{
    PAGED_CODE();

    IopResDbRelease(DeviceNode);
    IopReleaseMessageTokens(DeviceNode->ResourceList);

    if (DeviceNode->ResourceList != NULL)
    {
        ExFreePool(DeviceNode->ResourceList);
        DeviceNode->ResourceList = NULL;
    }

    if (DeviceNode->ResourceListTranslated != NULL)
    {
        ExFreePool(DeviceNode->ResourceListTranslated);
        DeviceNode->ResourceListTranslated = NULL;
    }

    IopUpdateResourceMapForPnPDevice(DeviceNode);
}

/* A device that fails once its drivers are added has its stack removed, as a failed start has (IRP_MN_REMOVE_DEVICE) */
static
VOID
PiRemoveFailedStack(
    _In_ PDEVICE_NODE DeviceNode)
{
    IO_STACK_LOCATION StackLocation;

    PAGED_CODE();
    ASSERT(DeviceNode->Flags & DNF_HAS_PROBLEM);

    RtlZeroMemory(&StackLocation, sizeof(StackLocation));
    StackLocation.MajorFunction = IRP_MJ_PNP;
    StackLocation.MinorFunction = IRP_MN_REMOVE_DEVICE;
    IopSynchronousCall(DeviceNode->PhysicalDeviceObject, &StackLocation, NULL);

    IopDisableDeviceInterfaces(DeviceNode);
    PiReleaseDeviceResources(DeviceNode);

    if (DeviceNode->ResourceRequirements != NULL)
    {
        ExFreePool(DeviceNode->ResourceRequirements);
        DeviceNode->ResourceRequirements = NULL;
    }

    PiSetDevNodeState(DeviceNode, DeviceNodeRemoved);
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
        PiRemoveFailedStack(DeviceNode);
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

        PiSetDevNodeProblem(DeviceNode, CM_PROB_FAILED_START);
        PiRemoveFailedStack(DeviceNode);

        return;
    }

    PiSetDevNodeState(DeviceNode, DeviceNodeStartPostWork);

    IopUpdateResourceMapForPnPDevice(DeviceNode);

    PiSetDevNodeState(DeviceNode, DeviceNodeStarted);

    /* The PnP manager asks a device for its state right after starting it (PNP_DEVICE_STATE documentation) */
    PiQueueDeviceAction(DeviceNode->PhysicalDeviceObject, PiActionQueryState, NULL, NULL);

    PopFxNotifyDeviceStarted(DeviceNode->PhysicalDeviceObject);

    InbvIndicateProgress();

    /* A device that just started is asked for its children */
    PiSetDevNodeFlag(DeviceNode, DNF_REENUMERATE);

    IopQueueDeviceInstallEvent(&GUID_DEVICE_ARRIVAL,
                               &DeviceNode->InstancePath);
}

static
BOOLEAN
PiIsPendingCloses(
    _In_ PDEVICE_NODE DeviceNode);

/* An eject removes the device first and leaves the eject itself to its bus driver (IoRequestDeviceEject) */
static
NTSTATUS
PiEjectDevice(
    _In_ PDEVICE_NODE DeviceNode)
{
    DEVICE_CAPABILITIES Capabilities;
    IO_STACK_LOCATION StackLocation;
    PI_QUERY_REMOVE_DATA RemoveData;
    BOOLEAN Supported = FALSE;
    NTSTATUS Status;

    PAGED_CODE();

    /* A stack waiting for its last close is surprise-removed already: its close path removes it */
    if (PiIsPendingCloses(DeviceNode)) return STATUS_DELETE_PENDING;

    IopQueueTargetDeviceEvent(&GUID_DEVICE_KERNEL_INITIATED_EJECT, &DeviceNode->InstancePath);

    if (NT_SUCCESS(IopQueryDeviceCapabilities(DeviceNode, &Capabilities))) Supported = Capabilities.EjectSupported;

    /* TODO: the removal and ejection relations of the device are not queried and removed with it */
    Status = (DeviceNode->State == DeviceNodeRemoved) ? STATUS_SUCCESS :
             PiQueryRemoveDevice(DeviceNode->PhysicalDeviceObject, &RemoveData);
    if (NT_SUCCESS(Status))
    {
        if (DeviceNode->State != DeviceNodeRemoved)
        {
            PiSetDevNodeState(DeviceNode, DeviceNodeAwaitingQueuedRemoval);
            PiDevNodeStateMachine(DeviceNode);
        }

        PiSetDevNodeProblem(DeviceNode, CM_PROB_HELD_FOR_EJECT);

        /* TODO: an eject whose removal waits for a child's last close is not sent once that child is gone */
        if (Supported && DeviceNode->State == DeviceNodeRemoved)
        {
            RtlZeroMemory(&StackLocation, sizeof(StackLocation));
            StackLocation.MajorFunction = IRP_MJ_PNP;
            StackLocation.MinorFunction = IRP_MN_EJECT;
            Status = IopSynchronousCall(DeviceNode->PhysicalDeviceObject, &StackLocation, NULL);
        }
    }

    IopQueueTargetDeviceEvent(NT_SUCCESS(Status) ? &GUID_DEVICE_EJECT : &GUID_DEVICE_EJECT_VETOED,
                              &DeviceNode->InstancePath);

    return Status;
}

/* New requirements restart a started device in place with the resources it holds, as Windows 11 does */
static
VOID
PiRestartInPlace(
    _In_ PDEVICE_NODE DeviceNode)
{
    PIO_RESOURCE_REQUIREMENTS_LIST Requirements = NULL;
    NTSTATUS Status;

    PAGED_CODE();

    if (!NT_SUCCESS(PiIrpQueryResourceRequirements(DeviceNode, &Requirements))) Requirements = NULL;

    if (DeviceNode->ResourceRequirements != NULL) ExFreePool(DeviceNode->ResourceRequirements);
    DeviceNode->ResourceRequirements = Requirements;

    if (Requirements != NULL) IopFilterResourceRequirements(DeviceNode);

    Status = PiIrpStartDevice(DeviceNode);
    if (NT_SUCCESS(Status)) return;

    DPRINT1("PnP: %wZ did not restart (%lx)\n", &DeviceNode->InstancePath, Status);

    PiSetDevNodeProblem(DeviceNode, CM_PROB_FAILED_START);
    PiSetDevNodeState(DeviceNode, DeviceNodeAwaitingQueuedRemoval);
    PiDevNodeStateMachine(DeviceNode);
}

static
VOID
PiProcessRemoval(
    _In_ PDEVICE_NODE DeviceNode,
    _In_ BOOLEAN Surprise,
    _In_ BOOLEAN ParentRemoved);

/* A child its bus no longer reports is gone: it is surprise-removed and leaves the tree after its last close */
static
VOID
PiRemoveMissingChildren(
    _In_ PDEVICE_NODE DeviceNode,
    _In_opt_ PDEVICE_RELATIONS Relations)
{
    PDEVICE_NODE ChildNode;
    PDEVICE_NODE Next;
    BOOLEAN Reported;
    ULONG Index;

    PAGED_CODE();

    for (ChildNode = DeviceNode->Child; ChildNode != NULL; ChildNode = Next)
    {
        Next = ChildNode->Sibling;

        Reported = FALSE;
        for (Index = 0; Relations != NULL && Index < Relations->Count && !Reported; Index++)
        {
            Reported = (Relations->Objects[Index] == ChildNode->PhysicalDeviceObject);
        }
        if (Reported) continue;

        PiSetDevNodeFlag(ChildNode, DNF_DEVICE_GONE);
        PiProcessRemoval(ChildNode, TRUE, FALSE);
    }
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
    DeviceNode->OverUsed1.PendingDeviceRelations = NULL;

    PiRemoveMissingChildren(DeviceNode, Relations);
    if (Relations == NULL) return;

    for (Index = 0; Index < Relations->Count; Index++)
    {
        Child = Relations->Objects[Index];

        ChildNode = IopGetDeviceNode(Child);
        if (ChildNode != NULL)
        {
            /* A removed device its bus still reports is set up again, its instance key and IDs included */
            if (ChildNode->State == DeviceNodeRemoved && !(ChildNode->Flags & (DNF_HAS_PROBLEM | DNF_DEVICE_GONE)))
            {
                PiClearDevNodeFlag(ChildNode, DNF_IDS_QUERIED | DNF_UNINSTALLED);
                PiSetDevNodeState(ChildNode, DeviceNodeUninitialized);
            }

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

/* A surprise-removed stack takes no new opens until its remove request (IRP_MN_SURPRISE_REMOVAL documentation) */
static
VOID
PiSetStackRemovePending(
    _In_ PDEVICE_OBJECT PhysicalDeviceObject,
    _In_ BOOLEAN Pending)
{
    PDEVICE_OBJECT Device;
    KIRQL OldIrql;

    KeAcquireSpinLock(&IopDeviceTreeLock, &OldIrql);

    for (Device = PhysicalDeviceObject; Device != NULL; Device = Device->AttachedDevice)
    {
        if (Pending)
            IoGetDevObjExtension(Device)->ExtensionFlags |= DOE_REMOVE_PENDING;
        else
            IoGetDevObjExtension(Device)->ExtensionFlags &= ~DOE_REMOVE_PENDING;
    }

    KeReleaseSpinLock(&IopDeviceTreeLock, OldIrql);
}

static
BOOLEAN
PiStackIsOpen(
    _In_ PDEVICE_OBJECT PhysicalDeviceObject)
{
    PDEVICE_OBJECT Device;
    BOOLEAN Open = FALSE;
    KIRQL OldIrql;

    KeAcquireSpinLock(&IopDeviceTreeLock, &OldIrql);

    for (Device = PhysicalDeviceObject; Device != NULL && !Open; Device = Device->AttachedDevice)
    {
        Open = (Device->ReferenceCount != 0);
    }

    KeReleaseSpinLock(&IopDeviceTreeLock, OldIrql);

    return Open;
}

static
VOID
PiSendRemoval(
    _In_ PDEVICE_NODE DeviceNode,
    _In_ BOOLEAN Surprise)
{
    IO_STACK_LOCATION StackLocation;

    PAGED_CODE();

    RtlZeroMemory(&StackLocation, sizeof(StackLocation));

    StackLocation.MajorFunction = IRP_MJ_PNP;
    StackLocation.MinorFunction = Surprise ? IRP_MN_SURPRISE_REMOVAL
                                           : IRP_MN_REMOVE_DEVICE;

    IopSynchronousCall(DeviceNode->PhysicalDeviceObject,
                       &StackLocation,
                       NULL);

    IopDisableDeviceInterfaces(DeviceNode);
    PiReleaseDeviceResources(DeviceNode);

    /* The next start asks the stack again, so that its filters and the device's interrupt policy apply anew */
    if (DeviceNode->ResourceRequirements != NULL)
    {
        ExFreePool(DeviceNode->ResourceRequirements);
        DeviceNode->ResourceRequirements = NULL;
    }

    /* A surprise-removed device keeps its stack until nothing holds it open (IRP_MN_REMOVE_DEVICE documentation) */
    if (Surprise)
    {
        PiSetStackRemovePending(DeviceNode->PhysicalDeviceObject, TRUE);
        PiNotifyTargetDeviceChange(&GUID_TARGET_DEVICE_REMOVE_COMPLETE,
                                   DeviceNode->PhysicalDeviceObject,
                                   NULL);
        PiSetDevNodeState(DeviceNode, DeviceNodeRemovePendingCloses);
    }
    else
    {
        PiSetStackRemovePending(DeviceNode->PhysicalDeviceObject, FALSE);
        PiSetDevNodeState(DeviceNode, DeviceNodeRemoved);
    }

    /* A node that gave its instance name to a device plugged in again has nothing left to report */
    if (DeviceNode->InstancePath.Length != 0)
    {
        IopQueueTargetDeviceEvent(Surprise ? &GUID_DEVICE_SURPRISE_REMOVAL
                                           : &GUID_DEVICE_SAFE_REMOVAL,
                                  &DeviceNode->InstancePath);
    }
}

/* A device whose stack was removed while it was present keeps its PDO; once its bus stops reporting it, the PDO gets
 * the second remove request with which the bus driver deletes it (removing-a-device-in-a-bus-driver) */
static
VOID
PiRemoveGonePdo(
    _In_ PDEVICE_NODE DeviceNode)
{
    IO_STACK_LOCATION StackLocation;

    PAGED_CODE();

    ASSERT(DeviceNode->State == DeviceNodeRemoved);
    ASSERT(DeviceNode->Flags & DNF_DEVICE_GONE);
    ASSERT(DeviceNode->PhysicalDeviceObject->AttachedDevice == NULL);

    RtlZeroMemory(&StackLocation, sizeof(StackLocation));
    StackLocation.MajorFunction = IRP_MJ_PNP;
    StackLocation.MinorFunction = IRP_MN_REMOVE_DEVICE;

    IopSynchronousCall(DeviceNode->PhysicalDeviceObject, &StackLocation, NULL);
}

/* A node out of the tree lives as long as its device object, whose delete routine frees it */
static
VOID
PiDeleteDevNode(
    _In_ PDEVICE_NODE DeviceNode)
{
    PDEVICE_OBJECT DeviceObject = DeviceNode->PhysicalDeviceObject;

    PAGED_CODE();

    KeEnterCriticalRegion();
    ExAcquireResourceExclusiveLite(&IopDeviceTreeResource, TRUE);
    PiUnlinkDevNode(DeviceNode);
    PiSetDevNodeState(DeviceNode, DeviceNodeDeleted);
    ExReleaseResourceLite(&IopDeviceTreeResource);
    KeLeaveCriticalRegion();

    IopForgetDeviceInterfaces(DeviceObject);

    ObDereferenceObject(DeviceObject);
}

static
BOOLEAN
PiIsPendingCloses(
    _In_ PDEVICE_NODE DeviceNode)
{
    return DeviceNode->State == DeviceNodeRemovePendingCloses ||
           DeviceNode->State == DeviceNodeDeletePendingCloses;
}

/* A surprise-removed device gets its remove request once its children are gone and nothing holds its stack open;
 * one that is gone then leaves the tree, and its parent may be next */
static
VOID
PiCloseRemovedDevice(
    _In_ PDEVICE_NODE DeviceNode)
{
    PDEVICE_NODE Parent;
    BOOLEAN Delete;

    PAGED_CODE();

    while (DeviceNode != NULL && PiIsPendingCloses(DeviceNode))
    {
        if (DeviceNode->Child != NULL || PiStackIsOpen(DeviceNode->PhysicalDeviceObject)) return;

        Delete = (DeviceNode->State == DeviceNodeDeletePendingCloses);
        Parent = DeviceNode->Parent;

        PiSendRemoval(DeviceNode, FALSE);
        if (Delete) PiDeleteDevNode(DeviceNode);

        DeviceNode = Parent;
    }
}

static
VOID
PiProcessRemoval(
    _In_ PDEVICE_NODE DeviceNode,
    _In_ BOOLEAN Surprise,
    _In_ BOOLEAN ParentRemoved)
{
    PDEVICE_NODE Child;
    PDEVICE_NODE Next;
    BOOLEAN Gone;
    BOOLEAN WasRemoved;

    PAGED_CODE();

    Child = DeviceNode->Child;
    while (Child != NULL)
    {
        Next = Child->Sibling;
        PiProcessRemoval(Child, Surprise, TRUE);
        Child = Next;
    }

    Gone = ParentRemoved || (DeviceNode->Flags & DNF_DEVICE_GONE);
    WasRemoved = (DeviceNode->State == DeviceNodeRemoved);

    if (!PiIsPendingCloses(DeviceNode) && DeviceNode->State != DeviceNodeRemoved)
    {
        if (Surprise || DeviceNode->Child == NULL)
        {
            PiSendRemoval(DeviceNode, Surprise);
        }
        else
        {
            /* A child still waiting for its last close is removed before its parent */
            PiSetStackRemovePending(DeviceNode->PhysicalDeviceObject, TRUE);
            PiSetDevNodeState(DeviceNode, DeviceNodeRemovePendingCloses);
        }
    }

    if (PiIsPendingCloses(DeviceNode))
    {
        if (Gone) PiSetDevNodeState(DeviceNode, DeviceNodeDeletePendingCloses);
        PiCloseRemovedDevice(DeviceNode);
        return;
    }

    if (Gone)
    {
        /* A child of a removed parent is deleted by the parent's bus driver, which gets its own remove request */
        if (WasRemoved && (DeviceNode->Flags & DNF_DEVICE_GONE)) PiRemoveGonePdo(DeviceNode);
        PiDeleteDevNode(DeviceNode);
    }
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
                PiClearDevNodeFlag(Node, DNF_NO_SERVICE);
                PiSetDevNodeState(Node, DeviceNodeInitialized);
                Progress = TRUE;
                break;

            case DeviceNodeInitialized:
                if (Node->Flags & (DNF_HAS_PROBLEM | DNF_NO_SERVICE)) break;

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
                PiProcessRemoval(Node, FALSE, FALSE);
                Progress = TRUE;
                break;

            case DeviceNodeAwaitingQueuedDeletion:
                PiProcessRemoval(Node, TRUE, FALSE);
                Progress = TRUE;
                break;

            default:
                break;
        }

        if (Progress) continue;

        /* Children whose subtrees were walked in parallel are done; the walk goes on with the next sibling */
        if (Node->Child != NULL && !(PnpEnableParallelEnum && PiProcessChildrenParallel(Node)))
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
    if (DeviceNode == NULL || DeviceNode->State == DeviceNodeDeleted)
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
                /* A stack waiting for its last close keeps waiting; its close path removes it */
                if (PiIsPendingCloses(DeviceNode)) break;

                /* Only a device without a stack starts over; a stack is never added twice */
                if ((DeviceNode->State != DeviceNodeInitialized) && (DeviceNode->State != DeviceNodeRemoved)) break;

                PiClearDevNodeProblem(DeviceNode);
                PiSetDevNodeState(DeviceNode, DeviceNodeUninitialized);
                PiDevNodeStateMachine(DeviceNode);
                break;

            case PiActionQueryState:
            {
                PNP_DEVICE_STATE DeviceState = 0;

                /* Only a started device is asked for its state (IRP_MN_QUERY_PNP_DEVICE_STATE documentation) */
                if (DeviceNode->State != DeviceNodeStarted) break;

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

                if ((DeviceState & PNP_DEVICE_REMOVED) ||
                    ((DeviceState & PNP_DEVICE_FAILED) && DeviceNode->State == DeviceNodeStarted))
                {
                    if (DeviceState & PNP_DEVICE_FAILED)
                    {
                        PiSetDevNodeProblem(DeviceNode, CM_PROB_FAILED_POST_START);
                    }

                    /* Both are surprise removals (IRP_MN_SURPRISE_REMOVAL documentation) */
                    PiSetDevNodeState(DeviceNode,
                                      DeviceNodeAwaitingQueuedDeletion);
                    PiDevNodeStateMachine(DeviceNode);
                }
                else if ((DeviceState & PNP_DEVICE_RESOURCE_REQUIREMENTS_CHANGED) &&
                         DeviceNode->State == DeviceNodeStarted)
                {
                    PiRestartInPlace(DeviceNode);
                }

                break;
            }

            case PiActionQueryRemoveDevice:
                /* A stack that is removed already is asked nothing more */
                if (DeviceNode->State == DeviceNodeRemoved || PiIsPendingCloses(DeviceNode))
                {
                    RtlZeroMemory(&RemoveData, sizeof(RemoveData));
                    Status = STATUS_SUCCESS;
                }
                else
                {
                    Status = PiQueryRemoveDevice(Request->DeviceObject,
                                                 &RemoveData);
                }
                if (Request->RemoveData != NULL)
                    *Request->RemoveData = RemoveData;
                break;

            case PiActionRemoveDevice:
                /* A removed stack gets no second remove request; a removed node that is gone leaves the tree */
                if (DeviceNode->State == DeviceNodeRemoved || PiIsPendingCloses(DeviceNode))
                {
                    if (DeviceNode->State == DeviceNodeRemoved && (DeviceNode->Flags & DNF_DEVICE_GONE) &&
                        DeviceNode->Child == NULL)
                    {
                        PiRemoveGonePdo(DeviceNode);
                        PiDeleteDevNode(DeviceNode);
                    }
                    break;
                }

                PiSetDevNodeState(DeviceNode,
                                  DeviceNodeAwaitingQueuedRemoval);
                PiDevNodeStateMachine(DeviceNode);
                break;

            case PiActionCloseRemovedDevice:
                PiCloseRemovedDevice(DeviceNode);
                break;

            case PiActionEjectDevice:
                Status = PiEjectDevice(DeviceNode);
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
