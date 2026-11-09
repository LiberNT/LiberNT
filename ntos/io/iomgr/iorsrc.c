/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     What hardware is present and which resources it has taken
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#include "../ioint.h"
#include "../pnpio.h"
#define NDEBUG
#include <debug.h>

/* GLOBALS ********************************************************************/

CONFIGURATION_INFORMATION IopConfigurationInformation =
{
    0, 0, 0, 0, 0, 0, 0, FALSE, FALSE, 0, 0
};

INTERFACE_TYPE PnpDefaultInterfaceType = InterfaceTypeUndefined;

PWSTR ArcTypes[] =
{
    L"System",
    L"CentralProcessor",
    L"FloatingPointProcessor",
    L"PrimaryIcache",
    L"PrimaryDcache",
    L"SecondaryIcache",
    L"SecondaryDcache",
    L"SecondaryCache",
    L"EisaAdapter",
    L"TcAdapter",
    L"ScsiAdapter",
    L"DtiAdapter",
    L"MultifunctionAdapter",
    L"DiskController",
    L"TapeController",
    L"CdRomController",
    L"WormController",
    L"SerialController",
    L"NetworkController",
    L"DisplayController",
    L"ParallelController",
    L"PointerController",
    L"KeyboardController",
    L"AudioController",
    L"OtherController",
    L"DiskPeripheral",
    L"FloppyDiskPeripheral",
    L"TapePeripheral",
    L"ModemPeripheral",
    L"MonitorPeripheral",
    L"PrinterPeripheral",
    L"PointerPeripheral",
    L"KeyboardPeripheral",
    L"TerminalPeripheral",
    L"OtherPeripheral",
    L"LinePeripheral",
    L"NetworkPeripheral",
    L"SystemMemory",
    L"DockingInformation",
    L"RealModeIrqRoutingTable",
    L"RealModePCIEnumeration",
    L"Undefined"
};

PWSTR IoDeviceInfoNames[] =
{
    L"Identifier",
    L"Configuration Data",
    L"Component Information"
};

/* PRIVATE DATA ***************************************************************/

static const UNICODE_STRING IopResourceMapKeyName = RTL_CONSTANT_STRING(
    L"\\Registry\\Machine\\Hardware\\ResourceMap");

static const UNICODE_STRING IopSystemPartitionKeyName = RTL_CONSTANT_STRING(
    L"\\Registry\\Machine\\System\\Setup");

/* PRIVATE FUNCTIONS **********************************************************/

PCWSTR
IopGetInterfaceTypeString(
    _In_ INTERFACE_TYPE InterfaceType)
{
    switch (InterfaceType)
    {
        case Internal: return L"Internal";
        case Isa: return L"Isa";
        case Eisa: return L"Eisa";
        case MicroChannel: return L"MicroChannel";
        case TurboChannel: return L"TurboChannel";
        case PCIBus: return L"PCIBus";
        case VMEBus: return L"VMEBus";
        case NuBus: return L"NuBus";
        case PCMCIABus: return L"PCMCIABus";
        case CBus: return L"CBus";
        case MPIBus: return L"MPIBus";
        case MPSABus: return L"MPSABus";
        case ProcessorInternal: return L"ProcessorInternal";
        case InternalPowerBus: return L"InternalPowerBus";
        case PNPISABus: return L"PNPISABus";
        case PNPBus: return L"PNPBus";
        case Vmcs: return L"Vmcs";
        case ACPIBus: return L"ACPIBus";
        default: return L"Undefined";
    }
}

INTERFACE_TYPE
IopDetermineDefaultInterfaceType(VOID)
{
    if (PnpDefaultInterfaceType != InterfaceTypeUndefined)
    {
        return PnpDefaultInterfaceType;
    }

    PnpDefaultInterfaceType = PCIBus;

    return PnpDefaultInterfaceType;
}

ULONG
NTAPI
PnpDetermineResourceListSize(
    IN PCM_RESOURCE_LIST ResourceList)
{
    PCM_FULL_RESOURCE_DESCRIPTOR Full;
    ULONG Size;
    ULONG List;

    if (ResourceList == NULL) return 0;

    Size = (ULONG)FIELD_OFFSET(CM_RESOURCE_LIST, List);
    Full = &ResourceList->List[0];

    for (List = 0; List < ResourceList->Count; List++)
    {
        Size += (ULONG)FIELD_OFFSET(CM_FULL_RESOURCE_DESCRIPTOR,
                                    PartialResourceList.PartialDescriptors);
        Size += Full->PartialResourceList.Count *
                (ULONG)sizeof(CM_PARTIAL_RESOURCE_DESCRIPTOR);

        Full = (PCM_FULL_RESOURCE_DESCRIPTOR)
            ((PUCHAR)&Full->PartialResourceList.PartialDescriptors[0] +
             Full->PartialResourceList.Count *
                 sizeof(CM_PARTIAL_RESOURCE_DESCRIPTOR));
    }

    return Size;
}

BOOLEAN
IopIsResourceListValid(
    _In_ PCM_RESOURCE_LIST ResourceList,
    _In_ ULONG ResourceListLength)
{
    if (ResourceList == NULL) return FALSE;
    if (ResourceListLength < FIELD_OFFSET(CM_RESOURCE_LIST, List))
    {
        return FALSE;
    }

    if (ResourceList->Count == 0) return ResourceListLength >= sizeof(ULONG);

    return PnpDetermineResourceListSize(ResourceList) <= ResourceListLength;
}

BOOLEAN
IopDmaDescriptorsConflict(
    _In_ PCM_PARTIAL_RESOURCE_DESCRIPTOR First,
    _In_ PCM_PARTIAL_RESOURCE_DESCRIPTOR Second)
{
    if (First->Type != CmResourceTypeDma) return FALSE;
    if (Second->Type != CmResourceTypeDma) return FALSE;

    return First->u.Dma.Channel == Second->u.Dma.Channel;
}

NTSTATUS
NTAPI
IopDetectResourceConflict(
    IN PCM_RESOURCE_LIST ResourceList,
    IN BOOLEAN Silent,
    OUT OPTIONAL PCM_PARTIAL_RESOURCE_DESCRIPTOR ConflictingDescriptor)
{
    PCM_PARTIAL_RESOURCE_DESCRIPTOR Descriptor;
    PCM_PARTIAL_RESOURCE_DESCRIPTOR Conflict;
    PCM_FULL_RESOURCE_DESCRIPTOR Full;
    BOOLEAN Conflicted;
    ULONG Index;
    ULONG List;

    PAGED_CODE();

    if (ResourceList == NULL || ResourceList->Count == 0)
    {
        return STATUS_SUCCESS;
    }

    IopResDbEnsureSeeded();

    Full = &ResourceList->List[0];

    for (List = 0; List < ResourceList->Count; List++)
    {
        Descriptor = Full->PartialResourceList.PartialDescriptors;

        for (Index = 0; Index < Full->PartialResourceList.Count; Index++)
        {
            Conflict = ConflictingDescriptor;

            Conflicted = IopResDbCheckDescriptor(Descriptor, NULL, Conflict);
            if (Conflicted)
            {
                if (!Silent)
                {
                    DPRINT1("I/O: a reported resource is already taken\n");
                    PipDumpCmResourceDescriptor(Descriptor, 0);
                }

                return STATUS_CONFLICTING_ADDRESSES;
            }

            Descriptor = PipGetNextCmPartialDescriptor(Descriptor);
        }

        Full = (PCM_FULL_RESOURCE_DESCRIPTOR)Descriptor;
    }

    return STATUS_SUCCESS;
}

static
NTSTATUS
IopOpenResourceMapKey(
    _In_ PCWSTR Level1Key,
    _In_ PCWSTR Level2Key,
    _Out_ PHANDLE KeyHandle)
{
    UNICODE_STRING Name;
    NTSTATUS Status;
    HANDLE Parent;
    HANDLE Child;

    PAGED_CODE();

    Status = IopCreateRegistryKeyEx(&Parent,
                                    NULL,
                                    (PUNICODE_STRING)&IopResourceMapKeyName,
                                    KEY_ALL_ACCESS,
                                    REG_OPTION_VOLATILE,
                                    NULL);
    if (!NT_SUCCESS(Status)) return Status;

    RtlInitUnicodeString(&Name, Level1Key);

    Status = IopCreateRegistryKeyEx(&Child,
                                    Parent,
                                    &Name,
                                    KEY_ALL_ACCESS,
                                    REG_OPTION_VOLATILE,
                                    NULL);
    ZwClose(Parent);

    if (!NT_SUCCESS(Status)) return Status;

    RtlInitUnicodeString(&Name, Level2Key);

    Status = IopCreateRegistryKeyEx(KeyHandle,
                                    Child,
                                    &Name,
                                    KEY_ALL_ACCESS,
                                    REG_OPTION_VOLATILE,
                                    NULL);
    ZwClose(Child);

    return Status;
}

NTSTATUS
IopUpdateResourceMap(
    _In_ PDEVICE_NODE DeviceNode,
    _In_ PWSTR Level1Key,
    _In_ PWSTR Level2Key,
    _In_opt_ PCM_RESOURCE_LIST ResourceList)
{
    UNICODE_STRING ValueName;
    NTSTATUS Status;
    HANDLE KeyHandle;
    ULONG Length;

    PAGED_CODE();

    Status = IopOpenResourceMapKey(Level1Key, Level2Key, &KeyHandle);
    if (!NT_SUCCESS(Status)) return Status;

    if (DeviceNode != NULL && DeviceNode->InstancePath.Length != 0)
    {
        ValueName = DeviceNode->InstancePath;
    }
    else
    {
        RtlInitUnicodeString(&ValueName, L"Raw");
    }

    if (ResourceList == NULL)
    {
        Status = ZwDeleteValueKey(KeyHandle, &ValueName);
        ZwClose(KeyHandle);
        return Status;
    }

    Length = PnpDetermineResourceListSize(ResourceList);

    Status = ZwSetValueKey(KeyHandle,
                           &ValueName,
                           0,
                           REG_RESOURCE_LIST,
                           ResourceList,
                           Length);

    ZwClose(KeyHandle);

    return Status;
}

NTSTATUS
IopUpdateResourceMapForPnPDevice(
    _In_ PDEVICE_NODE DeviceNode)
{
    PAGED_CODE();

    if (DeviceNode == NULL) return STATUS_INVALID_PARAMETER;

    return IopUpdateResourceMap(DeviceNode,
                                L"PnP Manager",
                                L"PnpManager",
                                DeviceNode->ResourceList);
}

CODE_SEG("INIT")
VOID
NTAPI
IopInitializeSystemResourceMap(
    _In_ PLOADER_PARAMETER_BLOCK LoaderBlock)
{
    HANDLE KeyHandle;
    NTSTATUS Status;

    PAGED_CODE();

    UNREFERENCED_PARAMETER(LoaderBlock);

    Status = IopCreateRegistryKeyEx(&KeyHandle,
                                    NULL,
                                    (PUNICODE_STRING)&IopResourceMapKeyName,
                                    KEY_ALL_ACCESS,
                                    REG_OPTION_VOLATILE,
                                    NULL);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("I/O: the resource map could not be made (%lx)\n", Status);
        return;
    }

    ZwClose(KeyHandle);
}

NTSTATUS
IopFetchConfigurationInformation(
    _Out_ PWSTR *SymbolicLinkList,
    _In_ GUID Guid,
    _In_ ULONG ExpectedInterfaces,
    _Out_ PULONG Interfaces)
{
    UNICODE_STRING Current;
    NTSTATUS Status;
    ULONG Count = 0;
    PWSTR List;

    PAGED_CODE();

    *SymbolicLinkList = NULL;
    *Interfaces = 0;

    Status = IoGetDeviceInterfaces(&Guid, NULL, 0, SymbolicLinkList);
    if (!NT_SUCCESS(Status)) return Status;

    List = *SymbolicLinkList;
    while (*List != UNICODE_NULL)
    {
        RtlInitUnicodeString(&Current, List);
        List += Current.Length / sizeof(WCHAR) + 1;
        Count++;
    }

    if (Count < ExpectedInterfaces)
    {
        ExFreePool(*SymbolicLinkList);
        *SymbolicLinkList = NULL;
        return STATUS_UNSUCCESSFUL;
    }

    *Interfaces = Count;

    return STATUS_SUCCESS;
}

VOID
IopStoreSystemPartitionInformation(
    _In_ PUNICODE_STRING NtSystemPartitionDeviceName,
    _In_ PUNICODE_STRING OsLoaderPathName)
{
    UNICODE_STRING ValueName;
    NTSTATUS Status;
    HANDLE KeyHandle;

    PAGED_CODE();

    Status = IopCreateRegistryKeyEx(
        &KeyHandle,
        NULL,
        (PUNICODE_STRING)&IopSystemPartitionKeyName,
        KEY_ALL_ACCESS,
        REG_OPTION_NON_VOLATILE,
        NULL);
    if (!NT_SUCCESS(Status)) return;

    RtlInitUnicodeString(&ValueName, L"SystemPartition");

    ZwSetValueKey(KeyHandle,
                  &ValueName,
                  0,
                  REG_SZ,
                  NtSystemPartitionDeviceName->Buffer,
                  NtSystemPartitionDeviceName->Length + sizeof(WCHAR));

    RtlInitUnicodeString(&ValueName, L"OsLoaderPath");

    ZwSetValueKey(KeyHandle,
                  &ValueName,
                  0,
                  REG_SZ,
                  OsLoaderPathName->Buffer,
                  OsLoaderPathName->Length + sizeof(WCHAR));

    ZwClose(KeyHandle);
}

/* PUBLIC FUNCTIONS ***********************************************************/

PCONFIGURATION_INFORMATION
NTAPI
IoGetConfigurationInformation(VOID)
{
    return &IopConfigurationInformation;
}

NTSTATUS
NTAPI
IoReportResourceUsage(
    _In_opt_ PUNICODE_STRING DriverClassName,
    _In_ PDRIVER_OBJECT DriverObject,
    _In_reads_bytes_opt_(DriverListSize) PCM_RESOURCE_LIST DriverList,
    _In_opt_ ULONG DriverListSize,
    _In_opt_ PDEVICE_OBJECT DeviceObject,
    _In_reads_bytes_opt_(DeviceListSize) PCM_RESOURCE_LIST DeviceList,
    _In_opt_ ULONG DeviceListSize,
    _In_ BOOLEAN OverrideConflict,
    _Out_ PBOOLEAN ConflictDetected)
{
    PCM_RESOURCE_LIST ResourceList;
    PDEVICE_NODE DeviceNode = NULL;
    NTSTATUS Status;

    PAGED_CODE();

    UNREFERENCED_PARAMETER(DriverClassName);
    UNREFERENCED_PARAMETER(DriverObject);

    *ConflictDetected = FALSE;

    if (DeviceList != NULL)
    {
        ResourceList = DeviceList;
        if (!IopIsResourceListValid(ResourceList, DeviceListSize))
        {
            return STATUS_INVALID_PARAMETER;
        }
    }
    else if (DriverList != NULL)
    {
        ResourceList = DriverList;
        if (!IopIsResourceListValid(ResourceList, DriverListSize))
        {
            return STATUS_INVALID_PARAMETER;
        }
    }
    else
    {
        return STATUS_INVALID_PARAMETER;
    }

    Status = IopDetectResourceConflict(ResourceList, TRUE, NULL);
    if (Status == STATUS_CONFLICTING_ADDRESSES)
    {
        *ConflictDetected = TRUE;

        if (!OverrideConflict) return STATUS_CONFLICTING_ADDRESSES;
    }
    else if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    if (DeviceObject != NULL) DeviceNode = IopGetDeviceNode(DeviceObject);

    IopResDbReserve(DeviceNode, ResourceList, NULL);

    return IopUpdateResourceMap(DeviceNode,
                                L"OtherDrivers",
                                L"Legacy",
                                ResourceList);
}

NTSTATUS
NTAPI
IoReportResourceForDetection(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_reads_bytes_opt_(DriverListSize) PCM_RESOURCE_LIST DriverList,
    _In_opt_ ULONG DriverListSize,
    _In_opt_ PDEVICE_OBJECT DeviceObject,
    _In_reads_bytes_opt_(DeviceListSize) PCM_RESOURCE_LIST DeviceList,
    _In_opt_ ULONG DeviceListSize,
    _Out_ PBOOLEAN ConflictDetected)
{
    return IoReportResourceUsage(NULL,
                                 DriverObject,
                                 DriverList,
                                 DriverListSize,
                                 DeviceObject,
                                 DeviceList,
                                 DeviceListSize,
                                 FALSE,
                                 ConflictDetected);
}

NTSTATUS
NTAPI
IoReportHalResourceUsage(
    _In_ PUNICODE_STRING HalName,
    _In_ PCM_RESOURCE_LIST RawResourceList,
    _In_ PCM_RESOURCE_LIST TranslatedResourceList,
    _In_ ULONG ResourceListSize)
{
    UNICODE_STRING ValueName;
    NTSTATUS Status;
    HANDLE KeyHandle;

    PAGED_CODE();

    if (!IopIsResourceListValid(RawResourceList, ResourceListSize))
    {
        return STATUS_INVALID_PARAMETER;
    }

    Status = IopOpenResourceMapKey(L"Hardware Abstraction Layer",
                                   HalName->Buffer,
                                   &KeyHandle);
    if (!NT_SUCCESS(Status)) return Status;

    RtlInitUnicodeString(&ValueName, L".Raw");

    Status = ZwSetValueKey(KeyHandle,
                           &ValueName,
                           0,
                           REG_RESOURCE_LIST,
                           RawResourceList,
                           PnpDetermineResourceListSize(RawResourceList));
    if (NT_SUCCESS(Status))
    {
        RtlInitUnicodeString(&ValueName, L".Translated");

        Status = ZwSetValueKey(
            KeyHandle,
            &ValueName,
            0,
            REG_RESOURCE_LIST,
            TranslatedResourceList,
            PnpDetermineResourceListSize(TranslatedResourceList));
    }

    ZwClose(KeyHandle);

    if (NT_SUCCESS(Status))
    {
        IopResDbReserve(NULL, RawResourceList, NULL);
    }

    return Status;
}

NTSTATUS
NTAPI
IoAssignResources(
    _In_ PUNICODE_STRING RegistryPath,
    _In_opt_ PUNICODE_STRING DriverClassName,
    _In_ PDRIVER_OBJECT DriverObject,
    _In_opt_ PDEVICE_OBJECT DeviceObject,
    _In_ PIO_RESOURCE_REQUIREMENTS_LIST RequestedResources,
    _Inout_ PCM_RESOURCE_LIST *AllocatedResources)
{
    PCM_RESOURCE_LIST Assigned = NULL;
    BOOLEAN Conflict = FALSE;
    NTSTATUS Status;

    PAGED_CODE();

    UNREFERENCED_PARAMETER(RegistryPath);
    UNREFERENCED_PARAMETER(DriverClassName);

    if (RequestedResources == NULL) return STATUS_INVALID_PARAMETER;

    Status = IopFixupResourceListWithRequirements(RequestedResources,
                                                  &Assigned,
                                                  NULL);
    if (!NT_SUCCESS(Status)) return Status;

    Status = IoReportResourceUsage(NULL,
                                   DriverObject,
                                   NULL,
                                   0,
                                   DeviceObject,
                                   Assigned,
                                   PnpDetermineResourceListSize(Assigned),
                                   FALSE,
                                   &Conflict);
    if (!NT_SUCCESS(Status))
    {
        ExFreePool(Assigned);
        return Status;
    }

    *AllocatedResources = Assigned;

    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
IoQueryDeviceDescription(
    _In_opt_ PINTERFACE_TYPE BusType,
    _In_opt_ PULONG BusNumber,
    _In_opt_ PCONFIGURATION_TYPE ControllerType,
    _In_opt_ PULONG ControllerNumber,
    _In_opt_ PCONFIGURATION_TYPE PeripheralType,
    _In_opt_ PULONG PeripheralNumber,
    _In_ PIO_QUERY_DEVICE_ROUTINE CalloutRoutine,
    _In_opt_ PVOID Context)
{
    OBJECT_ATTRIBUTES ObjectAttributes;
    UNICODE_STRING KeyName;
    NTSTATUS Status;
    HANDLE RootHandle;

    PAGED_CODE();

    if (BusType == NULL) return STATUS_NOT_IMPLEMENTED;
    if (*BusType < 0 || *BusType >= MaximumInterfaceType)
    {
        return STATUS_INVALID_PARAMETER;
    }

    RtlInitUnicodeString(&KeyName, IopHardwareDatabaseKey);

    InitializeObjectAttributes(&ObjectAttributes,
                               &KeyName,
                               OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE,
                               NULL,
                               NULL);

    Status = ZwOpenKey(&RootHandle, KEY_READ, &ObjectAttributes);
    if (!NT_SUCCESS(Status)) return Status;

    /* TODO: the firmware description tree is walked by the PnP mapper only */
    ZwClose(RootHandle);

    UNREFERENCED_PARAMETER(BusNumber);
    UNREFERENCED_PARAMETER(ControllerType);
    UNREFERENCED_PARAMETER(ControllerNumber);
    UNREFERENCED_PARAMETER(PeripheralType);
    UNREFERENCED_PARAMETER(PeripheralNumber);
    UNREFERENCED_PARAMETER(CalloutRoutine);
    UNREFERENCED_PARAMETER(Context);

    return STATUS_OBJECT_NAME_NOT_FOUND;
}
