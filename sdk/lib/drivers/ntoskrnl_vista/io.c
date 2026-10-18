/*
 * PROJECT:     ReactOS Kernel - Vista+ APIs
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Io functions of Vista+
 * COPYRIGHT:   2016 Pierre Schweitzer (pierre@reactos.org)
 *              2020 Victor Perevertkin (victor.perevertkin@reactos.org)
 */

#include <ntdef.h>
#include <ntifs.h>
#include <ntstrsafe.h>
#include <initguid.h>
#include <devpkey.h>
#include <ndk/iotypes.h>
#include <cfg.h>

typedef struct _EX_WORKITEM_CONTEXT
{
    PIO_WORKITEM WorkItem;
    PIO_WORKITEM_ROUTINE_EX WorkItemRoutineEx;
    PVOID Context;
} EX_WORKITEM_CONTEXT, *PEX_WORKITEM_CONTEXT;

#define TAG_IOWI 'IWOI'
#define TAG_IOVP 'PVOI'

typedef struct _IOP_DEVPROP_LEGACY_MAP
{
    const DEVPROPKEY *PropertyKey;
    DEVICE_REGISTRY_PROPERTY DeviceProperty;
    DEVPROPTYPE Type;
    BOOLEAN ConvertGuidString;
} IOP_DEVPROP_LEGACY_MAP, *PIOP_DEVPROP_LEGACY_MAP;

static
BOOLEAN
NTAPI
IopIsEqualDevPropKey(
    _In_ CONST DEVPROPKEY *Key1,
    _In_ CONST DEVPROPKEY *Key2)
{
    return Key1 != NULL &&
           Key2 != NULL &&
           Key1->pid == Key2->pid &&
           RtlCompareMemory(&Key1->fmtid, &Key2->fmtid, sizeof(GUID)) == sizeof(GUID);
}

static
NTSTATUS
NTAPI
IopOpenRelativeKey(
    _In_ HANDLE ParentKey,
    _In_ PCWSTR Name,
    _In_ ACCESS_MASK DesiredAccess,
    _In_ BOOLEAN Create,
    _Out_ PHANDLE KeyHandle)
{
    UNICODE_STRING KeyName;
    OBJECT_ATTRIBUTES ObjectAttributes;
    ULONG Disposition;

    RtlInitUnicodeString(&KeyName, Name);
    InitializeObjectAttributes(&ObjectAttributes,
                               &KeyName,
                               OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE,
                               ParentKey,
                               NULL);

    if (Create)
    {
        return ZwCreateKey(KeyHandle,
                           DesiredAccess,
                           &ObjectAttributes,
                           0,
                           NULL,
                           REG_OPTION_NON_VOLATILE,
                           &Disposition);
    }

    return ZwOpenKey(KeyHandle, DesiredAccess, &ObjectAttributes);
}

static
NTSTATUS
NTAPI
IopOpenPropertyDataKeyFromParent(
    _In_ HANDLE ParentKey,
    _In_ CONST DEVPROPKEY *PropertyKey,
    _In_ LCID Lcid,
    _In_ BOOLEAN Create,
    _Out_ PHANDLE PropertyDataKey)
{
    ACCESS_MASK Access = Create ? (KEY_READ | KEY_WRITE) : KEY_READ;
    HANDLE PropertiesKey, GuidKey;
    UNICODE_STRING GuidString;
    WCHAR PropertyLeaf[32];
    NTSTATUS Status;

    *PropertyDataKey = NULL;

    if (Lcid == LOCALE_NEUTRAL)
        Status = RtlStringCchPrintfW(PropertyLeaf, RTL_NUMBER_OF(PropertyLeaf), L"%04lX", PropertyKey->pid);
    else
        Status = RtlStringCchPrintfW(PropertyLeaf, RTL_NUMBER_OF(PropertyLeaf), L"%08lx.%08lx", PropertyKey->pid, Lcid);
    if (!NT_SUCCESS(Status))
        return Status;

    Status = IopOpenRelativeKey(ParentKey, L"Properties", Access, Create, &PropertiesKey);
    if (!NT_SUCCESS(Status))
        return Status;

    Status = RtlStringFromGUID(&PropertyKey->fmtid, &GuidString);
    if (NT_SUCCESS(Status))
    {
        Status = IopOpenRelativeKey(PropertiesKey, GuidString.Buffer, Access, Create, &GuidKey);
        RtlFreeUnicodeString(&GuidString);
    }
    ZwClose(PropertiesKey);
    if (!NT_SUCCESS(Status))
        return Status;

    Status = IopOpenRelativeKey(GuidKey, PropertyLeaf, Access, Create, PropertyDataKey);
    ZwClose(GuidKey);
    return Status;
}

static
NTSTATUS
NTAPI
IopOpenParametersParentKey(
    _In_ HANDLE ParametersKey,
    _In_ ACCESS_MASK DesiredAccess,
    _Out_ PHANDLE ParentKey)
{
    UNICODE_STRING ParametersName = RTL_CONSTANT_STRING(L"\\Device Parameters");
    UNICODE_STRING ParentName, Suffix;
    PKEY_NAME_INFORMATION NameInformation;
    OBJECT_ATTRIBUTES ObjectAttributes;
    ULONG Length = 0;
    NTSTATUS Status;

    *ParentKey = NULL;

    Status = ZwQueryKey(ParametersKey, KeyNameInformation, NULL, 0, &Length);
    if (Status != STATUS_BUFFER_TOO_SMALL && Status != STATUS_BUFFER_OVERFLOW)
        return NT_SUCCESS(Status) ? STATUS_OBJECT_PATH_NOT_FOUND : Status;

    NameInformation = ExAllocatePoolWithTag(PagedPool, Length, TAG_IOVP);
    if (!NameInformation)
        return STATUS_INSUFFICIENT_RESOURCES;

    Status = ZwQueryKey(ParametersKey, KeyNameInformation, NameInformation, Length, &Length);
    if (NT_SUCCESS(Status))
    {
        ParentName.Buffer = NameInformation->Name;
        ParentName.Length = ParentName.MaximumLength = (USHORT)NameInformation->NameLength;
        Status = STATUS_OBJECT_PATH_NOT_FOUND;
        if (ParentName.Length > ParametersName.Length)
        {
            Suffix.Buffer = (PWCHAR)((PUCHAR)ParentName.Buffer + ParentName.Length - ParametersName.Length);
            Suffix.Length = Suffix.MaximumLength = ParametersName.Length;
            if (RtlEqualUnicodeString(&Suffix, &ParametersName, TRUE))
            {
                ParentName.Length -= ParametersName.Length;
                InitializeObjectAttributes(&ObjectAttributes,
                                           &ParentName,
                                           OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE,
                                           NULL,
                                           NULL);
                Status = ZwOpenKey(ParentKey, DesiredAccess, &ObjectAttributes);
            }
        }
    }

    ExFreePoolWithTag(NameInformation, TAG_IOVP);
    return Status;
}

static
NTSTATUS
NTAPI
IopOpenInterfacePropertyDataKey(
    _In_ PUNICODE_STRING SymbolicLinkName,
    _In_ CONST DEVPROPKEY *PropertyKey,
    _In_ LCID Lcid,
    _In_ BOOLEAN Create,
    _Out_ PHANDLE PropertyDataKey)
{
    ACCESS_MASK Access = Create ? (KEY_READ | KEY_WRITE) : KEY_READ;
    HANDLE ParametersKey, InterfaceKey;
    NTSTATUS Status;

    *PropertyDataKey = NULL;

    Status = IoOpenDeviceInterfaceRegistryKey(SymbolicLinkName, KEY_READ, &ParametersKey);
    if (!NT_SUCCESS(Status))
        return Status;

    Status = IopOpenParametersParentKey(ParametersKey, Access, &InterfaceKey);
    ZwClose(ParametersKey);
    if (!NT_SUCCESS(Status))
        return Status;

    Status = IopOpenPropertyDataKeyFromParent(InterfaceKey, PropertyKey, Lcid, Create, PropertyDataKey);
    ZwClose(InterfaceKey);
    return Status;
}

static
NTSTATUS
NTAPI
IopWriteDevicePropertyValue(
    _In_ HANDLE PropertyDataKey,
    _In_ DEVPROPTYPE Type,
    _In_ ULONG Size,
    _In_reads_bytes_(Size) PVOID Data)
{
    UNICODE_STRING ValueName = RTL_CONSTANT_STRING(L"");

    return ZwSetValueKey(PropertyDataKey, &ValueName, 0, 0xFFFF0000 | (Type & 0xFFFF), Data, Size);
}

static
NTSTATUS
NTAPI
IopReadDevicePropertyValue(
    _In_ HANDLE PropertyDataKey,
    _In_ ULONG Size,
    _Out_writes_bytes_opt_(Size) PVOID Data,
    _Out_ PULONG RequiredSize,
    _Out_ PDEVPROPTYPE Type)
{
    UNICODE_STRING ValueName = RTL_CONSTANT_STRING(L"");
    PKEY_VALUE_PARTIAL_INFORMATION Information;
    ULONG Length = 0;
    NTSTATUS Status;

    Status = ZwQueryValueKey(PropertyDataKey, &ValueName, KeyValuePartialInformation, NULL, 0, &Length);
    if (Status != STATUS_BUFFER_TOO_SMALL && Status != STATUS_BUFFER_OVERFLOW)
        return NT_SUCCESS(Status) ? STATUS_OBJECT_NAME_NOT_FOUND : Status;

    Information = ExAllocatePoolWithTag(PagedPool, Length, TAG_IOVP);
    if (!Information)
        return STATUS_INSUFFICIENT_RESOURCES;

    Status = ZwQueryValueKey(PropertyDataKey, &ValueName, KeyValuePartialInformation, Information, Length, &Length);
    if (NT_SUCCESS(Status) && (Information->Type & 0xFFFF0000) != 0xFFFF0000)
        Status = STATUS_OBJECT_NAME_NOT_FOUND;

    if (NT_SUCCESS(Status))
    {
        *RequiredSize = Information->DataLength;
        *Type = Information->Type & 0xFFFF;
        if (Information->DataLength != 0 && (Data == NULL || Size < Information->DataLength))
            Status = STATUS_BUFFER_TOO_SMALL;
        else if (Information->DataLength != 0)
            RtlCopyMemory(Data, Information->Data, Information->DataLength);
    }

    ExFreePoolWithTag(Information, TAG_IOVP);
    return Status;
}

static
NTSTATUS
NTAPI
IopCopyPropertyData(
    _In_ DEVPROPTYPE PropertyType,
    _In_reads_bytes_opt_(DataSize) PVOID PropertyData,
    _In_ ULONG DataSize,
    _In_ ULONG Size,
    _Out_writes_bytes_opt_(Size) PVOID Data,
    _Out_ PULONG RequiredSize,
    _Out_ PDEVPROPTYPE Type)
{
    *RequiredSize = DataSize;
    *Type = PropertyType;

    if (DataSize != 0 && (Data == NULL || Size < DataSize))
        return STATUS_BUFFER_TOO_SMALL;

    if (DataSize != 0)
        RtlCopyMemory(Data, PropertyData, DataSize);

    return STATUS_SUCCESS;
}

static
PDEVICE_NODE
IopGetPdoDeviceNode(
    _In_ PDEVICE_OBJECT Pdo)
{
    PEXTENDED_DEVOBJ_EXTENSION DeviceObjectExtension;

    DeviceObjectExtension = (PEXTENDED_DEVOBJ_EXTENSION)Pdo->DeviceObjectExtension;
    if (!DeviceObjectExtension)
        return NULL;

    return DeviceObjectExtension->DeviceNode;
}

static
NTSTATUS
IopOpenDeviceInstanceKey(
    _In_ PDEVICE_OBJECT Pdo,
    _In_ ACCESS_MASK DesiredAccess,
    _Out_ PHANDLE KeyHandle)
{
    static const WCHAR EnumRoot[] = L"\\Registry\\Machine\\System\\CurrentControlSet\\Enum\\";
    OBJECT_ATTRIBUTES ObjectAttributes;
    UNICODE_STRING KeyName;
    PDEVICE_NODE DeviceNode;
    NTSTATUS Status;

    DeviceNode = IopGetPdoDeviceNode(Pdo);
    if (!DeviceNode || !DeviceNode->InstancePath.Buffer)
        return STATUS_INVALID_DEVICE_REQUEST;

    KeyName.Length = 0;
    KeyName.MaximumLength = sizeof(EnumRoot) + DeviceNode->InstancePath.Length;
    KeyName.Buffer = ExAllocatePoolWithTag(PagedPool, KeyName.MaximumLength, TAG_IOVP);
    if (!KeyName.Buffer)
        return STATUS_INSUFFICIENT_RESOURCES;

    RtlAppendUnicodeToString(&KeyName, EnumRoot);
    RtlAppendUnicodeStringToString(&KeyName, &DeviceNode->InstancePath);

    InitializeObjectAttributes(&ObjectAttributes,
                               &KeyName,
                               OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE,
                               NULL,
                               NULL);
    Status = ZwOpenKey(KeyHandle, DesiredAccess, &ObjectAttributes);
    ExFreePoolWithTag(KeyName.Buffer, TAG_IOVP);
    return Status;
}

static
NTSTATUS
NTAPI
IopOpenDevicePropertyDataKey(
    _In_ PDEVICE_OBJECT Pdo,
    _In_ CONST DEVPROPKEY *PropertyKey,
    _In_ LCID Lcid,
    _In_ BOOLEAN Create,
    _Out_ PHANDLE PropertyDataKey)
{
    HANDLE InstanceKey;
    NTSTATUS Status;

    *PropertyDataKey = NULL;

    Status = IopOpenDeviceInstanceKey(Pdo, Create ? (KEY_READ | KEY_WRITE) : KEY_READ, &InstanceKey);
    if (!NT_SUCCESS(Status))
        return Status;

    Status = IopOpenPropertyDataKeyFromParent(InstanceKey, PropertyKey, Lcid, Create, PropertyDataKey);
    ZwClose(InstanceKey);
    return Status;
}

static
NTSTATUS
NTAPI
IopQueryDeviceRegistryValueData(
    _In_ PDEVICE_OBJECT Pdo,
    _In_ BOOLEAN DriverKey,
    _In_ PCWSTR ValueNameString,
    _In_ ULONG RegistryType,
    _In_ DEVPROPTYPE PropertyType,
    _In_ BOOLEAN ConvertGuidString,
    _In_ ULONG Size,
    _Out_writes_bytes_opt_(Size) PVOID Data,
    _Out_ PULONG RequiredSize,
    _Out_ PDEVPROPTYPE Type)
{
    HANDLE DeviceKey = NULL;
    UNICODE_STRING ValueName;
    ULONG ResultLength = 0;
    PKEY_VALUE_PARTIAL_INFORMATION ValueInfo = NULL;
    NTSTATUS Status;

    if (DriverKey)
        Status = IoOpenDeviceRegistryKey(Pdo, PLUGPLAY_REGKEY_DRIVER, KEY_READ, &DeviceKey);
    else
        Status = IopOpenDeviceInstanceKey(Pdo, KEY_READ, &DeviceKey);
    if (!NT_SUCCESS(Status))
        return Status;

    RtlInitUnicodeString(&ValueName, ValueNameString);
    Status = ZwQueryValueKey(DeviceKey,
                             &ValueName,
                             KeyValuePartialInformation,
                             NULL,
                             0,
                             &ResultLength);
    if (Status != STATUS_BUFFER_TOO_SMALL &&
        Status != STATUS_BUFFER_OVERFLOW)
    {
        ZwClose(DeviceKey);
        return Status;
    }

    ValueInfo = ExAllocatePoolWithTag(PagedPool, ResultLength, TAG_IOVP);
    if (!ValueInfo)
    {
        ZwClose(DeviceKey);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    Status = ZwQueryValueKey(DeviceKey,
                             &ValueName,
                             KeyValuePartialInformation,
                             ValueInfo,
                             ResultLength,
                             &ResultLength);
    ZwClose(DeviceKey);
    if (!NT_SUCCESS(Status))
        goto Cleanup;

    if (ValueInfo->Type != RegistryType)
    {
        Status = STATUS_OBJECT_TYPE_MISMATCH;
        goto Cleanup;
    }

    if (ConvertGuidString)
    {
        UNICODE_STRING GuidString;
        GUID GuidValue;

        if (ValueInfo->DataLength < sizeof(WCHAR))
        {
            Status = STATUS_OBJECT_NAME_NOT_FOUND;
            goto Cleanup;
        }

        RtlInitUnicodeString(&GuidString, (PCWSTR)ValueInfo->Data);
        Status = RtlGUIDFromString(&GuidString, &GuidValue);
        if (!NT_SUCCESS(Status))
            goto Cleanup;

        Status = IopCopyPropertyData(DEVPROP_TYPE_GUID,
                                     &GuidValue,
                                     sizeof(GuidValue),
                                     Size,
                                     Data,
                                     RequiredSize,
                                     Type);
        goto Cleanup;
    }

    Status = IopCopyPropertyData(PropertyType,
                                 ValueInfo->Data,
                                 ValueInfo->DataLength,
                                 Size,
                                 Data,
                                 RequiredSize,
                                 Type);

Cleanup:
    ExFreePoolWithTag(ValueInfo, TAG_IOVP);
    return Status;
}

static
const IOP_DEVPROP_LEGACY_MAP *
NTAPI
IopFindMappedDeviceProperty(
    _In_ CONST DEVPROPKEY *PropertyKey)
{
    static const IOP_DEVPROP_LEGACY_MAP LegacyMappings[] =
    {
        { &DEVPKEY_Device_DeviceDesc,      DevicePropertyDeviceDescription,      DEVPROP_TYPE_STRING,      FALSE },
        { &DEVPKEY_Device_HardwareIds,     DevicePropertyHardwareID,             DEVPROP_TYPE_STRING_LIST, FALSE },
        { &DEVPKEY_Device_CompatibleIds,   DevicePropertyCompatibleIDs,          DEVPROP_TYPE_STRING_LIST, FALSE },
        { &DEVPKEY_Device_Class,           DevicePropertyClassName,              DEVPROP_TYPE_STRING,      FALSE },
        { &DEVPKEY_Device_ClassGuid,       DevicePropertyClassGuid,              DEVPROP_TYPE_GUID,        TRUE  },
        { &DEVPKEY_Device_Driver,          DevicePropertyDriverKeyName,          DEVPROP_TYPE_STRING,      FALSE },
        { &DEVPKEY_Device_Manufacturer,    DevicePropertyManufacturer,           DEVPROP_TYPE_STRING,      FALSE },
        { &DEVPKEY_Device_FriendlyName,    DevicePropertyFriendlyName,           DEVPROP_TYPE_STRING,      FALSE },
        { &DEVPKEY_Device_LocationInfo,    DevicePropertyLocationInformation,    DEVPROP_TYPE_STRING,      FALSE },
        { &DEVPKEY_Device_PDOName,         DevicePropertyPhysicalDeviceObjectName, DEVPROP_TYPE_STRING,    FALSE },
        { &DEVPKEY_Device_BusTypeGuid,     DevicePropertyBusTypeGuid,            DEVPROP_TYPE_GUID,        FALSE },
        { &DEVPKEY_Device_LegacyBusType,   DevicePropertyLegacyBusType,          DEVPROP_TYPE_UINT32,      FALSE },
        { &DEVPKEY_Device_BusNumber,       DevicePropertyBusNumber,              DEVPROP_TYPE_UINT32,      FALSE },
        { &DEVPKEY_Device_EnumeratorName,  DevicePropertyEnumeratorName,         DEVPROP_TYPE_STRING,      FALSE },
        { &DEVPKEY_Device_Address,         DevicePropertyAddress,                DEVPROP_TYPE_UINT32,      FALSE },
        { &DEVPKEY_Device_UINumber,        DevicePropertyUINumber,               DEVPROP_TYPE_UINT32,      FALSE },
        { &DEVPKEY_Device_InstallState,    DevicePropertyInstallState,           DEVPROP_TYPE_UINT32,      FALSE },
        { &DEVPKEY_Device_RemovalPolicy,   DevicePropertyRemovalPolicy,          DEVPROP_TYPE_UINT32,      FALSE },
    };
    ULONG i;

    for (i = 0; i < RTL_NUMBER_OF(LegacyMappings); ++i)
    {
        if (IopIsEqualDevPropKey(PropertyKey, LegacyMappings[i].PropertyKey))
            return &LegacyMappings[i];
    }

    return NULL;
}

static
ULONG
IopGetDevNodeStatus(
    _In_ PDEVICE_NODE DeviceNode)
{
    ULONG Status = DN_NT_ENUMERATOR | DN_NT_DRIVER;

    if (DeviceNode->Parent && !DeviceNode->Parent->Parent)
        Status |= DN_ROOT_ENUMERATED;

    if (DeviceNode->State >= DeviceNodeDriversAdded &&
        DeviceNode->State < DeviceNodeAwaitingQueuedDeletion)
    {
        Status |= DN_DRIVER_LOADED;
    }

    switch (DeviceNode->State)
    {
        case DeviceNodeStartPending:
        case DeviceNodeStartCompletion:
        case DeviceNodeStartPostWork:
        case DeviceNodeStarted:
        case DeviceNodeQueryStopped:
        case DeviceNodeEnumeratePending:
        case DeviceNodeEnumerateCompletion:
        case DeviceNodeStopped:
        case DeviceNodeRestartCompletion:
            Status |= DN_STARTED;
            break;
        default:
            break;
    }

    if (DeviceNode->Flags & DNF_HAS_PROBLEM)
        Status |= DN_HAS_PROBLEM;
    if (DeviceNode->Flags & DNF_HAS_PRIVATE_PROBLEM)
        Status |= DN_PRIVATE_PROBLEM;
    if (DeviceNode->Flags & DNF_DRIVER_BLOCKED)
        Status |= DN_DRIVER_BLOCKED;
    if (DeviceNode->Flags & DNF_CHILD_WITH_INVALID_ID)
        Status |= DN_CHILD_WITH_INVALID_ID;
    if (DeviceNode->Flags & DNF_LEGACY_DRIVER)
        Status |= DN_LEGACY_DRIVER;

    return Status;
}

typedef struct _IOP_DEVPROP_DRIVER_VALUE
{
    CONST DEVPROPKEY *PropertyKey;
    PCWSTR ValueName;
    ULONG RegistryType;
    DEVPROPTYPE Type;
} IOP_DEVPROP_DRIVER_VALUE;

static
const IOP_DEVPROP_DRIVER_VALUE *
IopFindDriverKeyProperty(
    _In_ CONST DEVPROPKEY *PropertyKey)
{
    static const IOP_DEVPROP_DRIVER_VALUE DriverValues[] =
    {
        { &DEVPKEY_Device_DriverDate,       L"DriverDateData",   REG_BINARY, DEVPROP_TYPE_FILETIME },
        { &DEVPKEY_Device_DriverVersion,    L"DriverVersion",    REG_SZ,     DEVPROP_TYPE_STRING },
        { &DEVPKEY_Device_DriverDesc,       L"DriverDesc",       REG_SZ,     DEVPROP_TYPE_STRING },
        { &DEVPKEY_Device_DriverInfPath,    L"InfPath",          REG_SZ,     DEVPROP_TYPE_STRING },
        { &DEVPKEY_Device_DriverInfSection, L"InfSection",       REG_SZ,     DEVPROP_TYPE_STRING },
        { &DEVPKEY_Device_MatchingDeviceId, L"MatchingDeviceId", REG_SZ,     DEVPROP_TYPE_STRING },
        { &DEVPKEY_Device_DriverProvider,   L"ProviderName",     REG_SZ,     DEVPROP_TYPE_STRING },
    };
    ULONG i;

    for (i = 0; i < RTL_NUMBER_OF(DriverValues); ++i)
    {
        if (IopIsEqualDevPropKey(PropertyKey, DriverValues[i].PropertyKey))
            return &DriverValues[i];
    }

    return NULL;
}

static
NTSTATUS
IopQueryDeviceStack(
    _In_ PDEVICE_OBJECT Pdo,
    _In_ ULONG Size,
    _Out_writes_bytes_opt_(Size) PVOID Data,
    _Out_ PULONG RequiredSize,
    _Out_ PDEVPROPTYPE Type)
{
    PDEVICE_OBJECT DeviceObject, LowerDevice;
    PWCHAR Output = Data;
    ULONG Needed = sizeof(WCHAR);
    ULONG Offset = 0;

    *Type = DEVPROP_TYPE_STRING_LIST;

    DeviceObject = IoGetAttachedDeviceReference(Pdo);
    while (DeviceObject)
    {
        PUNICODE_STRING Name = &DeviceObject->DriverObject->DriverName;

        Needed += Name->Length + sizeof(WCHAR);
        if (Output && Needed <= Size)
        {
            RtlCopyMemory((PUCHAR)Output + Offset, Name->Buffer, Name->Length);
            Offset += Name->Length;
            Output[Offset / sizeof(WCHAR)] = UNICODE_NULL;
            Offset += sizeof(WCHAR);
        }

        LowerDevice = (DeviceObject == Pdo) ? NULL : IoGetLowerDeviceObject(DeviceObject);
        ObDereferenceObject(DeviceObject);
        DeviceObject = LowerDevice;
    }

    *RequiredSize = Needed;
    if (!Output || Size < Needed)
        return STATUS_BUFFER_TOO_SMALL;

    Output[Offset / sizeof(WCHAR)] = UNICODE_NULL;
    return STATUS_SUCCESS;
}

static
NTSTATUS
NTAPI
IopQueryMappedDevicePropertyData(
    _In_ PDEVICE_OBJECT Pdo,
    _In_ CONST DEVPROPKEY *PropertyKey,
    _In_ ULONG Size,
    _Out_writes_bytes_opt_(Size) PVOID Data,
    _Out_ PULONG RequiredSize,
    _Out_ PDEVPROPTYPE Type)
{
    const IOP_DEVPROP_LEGACY_MAP *Mapping;
    const IOP_DEVPROP_DRIVER_VALUE *DriverValue;
    NTSTATUS Status;

    if (IopIsEqualDevPropKey(PropertyKey, &DEVPKEY_Device_InLocalMachineContainer))
    {
        DEVPROP_BOOLEAN Value = DEVPROP_TRUE;
        return IopCopyPropertyData(DEVPROP_TYPE_BOOLEAN,
                                   &Value,
                                   sizeof(Value),
                                   Size,
                                   Data,
                                   RequiredSize,
                                   Type);
    }

    if (IopIsEqualDevPropKey(PropertyKey, &DEVPKEY_Device_InstanceId) || IopIsEqualDevPropKey(PropertyKey, &DEVPKEY_Device_Parent))
    {
        PEXTENDED_DEVOBJ_EXTENSION DeviceObjectExtension;
        PDEVICE_NODE DeviceNode;

        DeviceObjectExtension = (PEXTENDED_DEVOBJ_EXTENSION)Pdo->DeviceObjectExtension;
        if (!DeviceObjectExtension)
            return STATUS_INVALID_DEVICE_REQUEST;

        DeviceNode = DeviceObjectExtension->DeviceNode;
        if (!DeviceNode || !DeviceNode->InstancePath.Buffer)
            return STATUS_INVALID_DEVICE_REQUEST;

        if (IopIsEqualDevPropKey(PropertyKey, &DEVPKEY_Device_Parent))
        {
            DeviceNode = DeviceNode->Parent;
            if (!DeviceNode || !DeviceNode->InstancePath.Buffer)
                return STATUS_OBJECT_NAME_NOT_FOUND;
        }

        *RequiredSize = DeviceNode->InstancePath.Length + sizeof(WCHAR);
        *Type = DEVPROP_TYPE_STRING;
        if (!Data || Size < *RequiredSize)
            return STATUS_BUFFER_TOO_SMALL;

        RtlCopyMemory(Data, DeviceNode->InstancePath.Buffer, DeviceNode->InstancePath.Length);
        ((PWCHAR)Data)[DeviceNode->InstancePath.Length / sizeof(WCHAR)] = UNICODE_NULL;
        return STATUS_SUCCESS;
    }

    if (IopIsEqualDevPropKey(PropertyKey, &DEVPKEY_Device_Service))
    {
        return IopQueryDeviceRegistryValueData(Pdo,
                                               FALSE,
                                               L"Service",
                                               REG_SZ,
                                               DEVPROP_TYPE_STRING,
                                               FALSE,
                                               Size,
                                               Data,
                                               RequiredSize,
                                               Type);
    }

    if (IopIsEqualDevPropKey(PropertyKey, &DEVPKEY_Device_ConfigFlags))
    {
        return IopQueryDeviceRegistryValueData(Pdo,
                                               FALSE,
                                               L"ConfigFlags",
                                               REG_DWORD,
                                               DEVPROP_TYPE_UINT32,
                                               FALSE,
                                               Size,
                                               Data,
                                               RequiredSize,
                                               Type);
    }

    if (IopIsEqualDevPropKey(PropertyKey, &DEVPKEY_Device_Capabilities))
    {
        return IopQueryDeviceRegistryValueData(Pdo,
                                               FALSE,
                                               L"Capabilities",
                                               REG_DWORD,
                                               DEVPROP_TYPE_UINT32,
                                               FALSE,
                                               Size,
                                               Data,
                                               RequiredSize,
                                               Type);
    }

    if (IopIsEqualDevPropKey(PropertyKey, &DEVPKEY_Device_UpperFilters))
    {
        return IopQueryDeviceRegistryValueData(Pdo,
                                               FALSE,
                                               L"UpperFilters",
                                               REG_MULTI_SZ,
                                               DEVPROP_TYPE_STRING_LIST,
                                               FALSE,
                                               Size,
                                               Data,
                                               RequiredSize,
                                               Type);
    }

    if (IopIsEqualDevPropKey(PropertyKey, &DEVPKEY_Device_LowerFilters))
    {
        return IopQueryDeviceRegistryValueData(Pdo,
                                               FALSE,
                                               L"LowerFilters",
                                               REG_MULTI_SZ,
                                               DEVPROP_TYPE_STRING_LIST,
                                               FALSE,
                                               Size,
                                               Data,
                                               RequiredSize,
                                               Type);
    }

    if (IopIsEqualDevPropKey(PropertyKey, &DEVPKEY_Device_ContainerId))
    {
        return IopQueryDeviceRegistryValueData(Pdo,
                                               FALSE,
                                               L"ContainerID",
                                               REG_SZ,
                                               DEVPROP_TYPE_GUID,
                                               TRUE,
                                               Size,
                                               Data,
                                               RequiredSize,
                                               Type);
    }

    if (IopIsEqualDevPropKey(PropertyKey, &DEVPKEY_Device_ProblemCode) ||
        IopIsEqualDevPropKey(PropertyKey, &DEVPKEY_Device_HasProblem) ||
        IopIsEqualDevPropKey(PropertyKey, &DEVPKEY_Device_IsPresent))
    {
        PDEVICE_NODE DeviceNode = IopGetPdoDeviceNode(Pdo);
        ULONG Problem;
        DEVPROP_BOOLEAN Value;

        if (!DeviceNode)
            return STATUS_INVALID_DEVICE_REQUEST;

        Problem = (DeviceNode->Flags & DNF_HAS_PROBLEM) ? DeviceNode->Problem : 0;
        if (IopIsEqualDevPropKey(PropertyKey, &DEVPKEY_Device_ProblemCode))
            return IopCopyPropertyData(DEVPROP_TYPE_UINT32, &Problem, sizeof(Problem), Size, Data, RequiredSize, Type);

        if (IopIsEqualDevPropKey(PropertyKey, &DEVPKEY_Device_HasProblem))
            Value = (DeviceNode->Flags & DNF_HAS_PROBLEM) ? DEVPROP_TRUE : DEVPROP_FALSE;
        else
            Value = DEVPROP_TRUE;
        return IopCopyPropertyData(DEVPROP_TYPE_BOOLEAN, &Value, sizeof(Value), Size, Data, RequiredSize, Type);
    }

    if (IopIsEqualDevPropKey(PropertyKey, &DEVPKEY_Device_Stack))
        return IopQueryDeviceStack(Pdo, Size, Data, RequiredSize, Type);

    if (IopIsEqualDevPropKey(PropertyKey, &DEVPKEY_Device_DevNodeStatus))
    {
        PDEVICE_NODE DeviceNode = IopGetPdoDeviceNode(Pdo);
        ULONG Value;

        if (!DeviceNode)
            return STATUS_INVALID_DEVICE_REQUEST;

        Value = IopGetDevNodeStatus(DeviceNode);
        return IopCopyPropertyData(DEVPROP_TYPE_UINT32, &Value, sizeof(Value), Size, Data, RequiredSize, Type);
    }

    DriverValue = IopFindDriverKeyProperty(PropertyKey);
    if (DriverValue)
    {
        return IopQueryDeviceRegistryValueData(Pdo,
                                               TRUE,
                                               DriverValue->ValueName,
                                               DriverValue->RegistryType,
                                               DriverValue->Type,
                                               FALSE,
                                               Size,
                                               Data,
                                               RequiredSize,
                                               Type);
    }

    Mapping = IopFindMappedDeviceProperty(PropertyKey);
    if (!Mapping)
        return STATUS_OBJECT_NAME_NOT_FOUND;

    if (Mapping->ConvertGuidString)
    {
        PWSTR GuidBuffer;
        ULONG GuidBufferSize = 0;
        UNICODE_STRING GuidString;
        GUID GuidValue;

        Status = IoGetDeviceProperty(Pdo,
                                     Mapping->DeviceProperty,
                                     0,
                                     NULL,
                                     &GuidBufferSize);
        if (Status != STATUS_BUFFER_TOO_SMALL)
            return Status;

        GuidBuffer = (PWSTR)ExAllocatePoolWithTag(PagedPool, GuidBufferSize, TAG_IOVP);
        if (!GuidBuffer)
            return STATUS_INSUFFICIENT_RESOURCES;

        Status = IoGetDeviceProperty(Pdo,
                                     Mapping->DeviceProperty,
                                     GuidBufferSize,
                                     GuidBuffer,
                                     &GuidBufferSize);
        if (NT_SUCCESS(Status))
        {
            RtlInitUnicodeString(&GuidString, GuidBuffer);
            Status = RtlGUIDFromString(&GuidString, &GuidValue);
            if (NT_SUCCESS(Status))
            {
                Status = IopCopyPropertyData(DEVPROP_TYPE_GUID,
                                             &GuidValue,
                                             sizeof(GuidValue),
                                             Size,
                                             Data,
                                             RequiredSize,
                                             Type);
            }
        }

        ExFreePoolWithTag(GuidBuffer, TAG_IOVP);
        return Status;
    }

    Status = IoGetDeviceProperty(Pdo,
                                 Mapping->DeviceProperty,
                                 0,
                                 NULL,
                                 RequiredSize);
    if (Status != STATUS_BUFFER_TOO_SMALL && !NT_SUCCESS(Status))
        return Status;

    *Type = Mapping->Type;
    if (*RequiredSize != 0 &&
        (Data == NULL || Size < *RequiredSize))
    {
        return STATUS_BUFFER_TOO_SMALL;
    }

    return IoGetDeviceProperty(Pdo,
                               Mapping->DeviceProperty,
                               Size,
                               Data,
                               RequiredSize);
}

NTKRNLVISTAAPI
NTSTATUS
NTAPI
IoGetIrpExtraCreateParameter(IN PIRP Irp,
                             OUT PECP_LIST *ExtraCreateParameter)
{
    /* Check we have a create operation */
    if (!BooleanFlagOn(Irp->Flags, IRP_CREATE_OPERATION))
    {
        return STATUS_INVALID_PARAMETER;
    }

    /* If so, return the list the user buffer refers to */
    *ExtraCreateParameter = Irp->UserBuffer ? *(PECP_LIST *)Irp->UserBuffer : NULL;
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
IoSetIrpExtraCreateParameter(IN OUT PIRP Irp,
                             IN PECP_LIST ExtraCreateParameter)
{
    if (!BooleanFlagOn(Irp->Flags, IRP_CREATE_OPERATION) || Irp->UserBuffer == NULL)
    {
        return STATUS_INVALID_PARAMETER_2;
    }

    if (*(PECP_LIST *)Irp->UserBuffer != NULL)
    {
        return STATUS_INVALID_PARAMETER_3;
    }

    *(PECP_LIST *)Irp->UserBuffer = ExtraCreateParameter;
    return STATUS_SUCCESS;
}

VOID
NTAPI
IoClearIrpExtraCreateParameter(IN OUT PIRP Irp)
{
    if (BooleanFlagOn(Irp->Flags, IRP_CREATE_OPERATION) && Irp->UserBuffer != NULL)
    {
        *(PECP_LIST *)Irp->UserBuffer = NULL;
    }
}

_Function_class_(IO_WORKITEM_ROUTINE)
static
VOID
NTAPI
IopWorkItemExCallback(
    PDEVICE_OBJECT DeviceObject,
    PVOID Ctx)
{
    PEX_WORKITEM_CONTEXT context = Ctx;

    context->WorkItemRoutineEx(DeviceObject, context->Context, context->WorkItem);
    ExFreePoolWithTag(context, TAG_IOWI);
}

NTKRNLVISTAAPI
VOID
NTAPI
IoQueueWorkItemEx(
    _Inout_ PIO_WORKITEM IoWorkItem,
    _In_ PIO_WORKITEM_ROUTINE_EX WorkerRoutine,
    _In_ WORK_QUEUE_TYPE QueueType,
    _In_opt_ __drv_aliasesMem PVOID Context)
{
    PEX_WORKITEM_CONTEXT newContext = ExAllocatePoolWithTag(NonPagedPoolMustSucceed, sizeof(*newContext), TAG_IOWI);
    newContext->WorkItem = IoWorkItem;
    newContext->WorkItemRoutineEx = WorkerRoutine;
    newContext->Context = Context;

    IoQueueWorkItem(IoWorkItem, IopWorkItemExCallback, QueueType, newContext);
}

_IRQL_requires_max_(PASSIVE_LEVEL)
_Must_inspect_result_
NTKRNLVISTAAPI
NTSTATUS
NTAPI
IoSetDevicePropertyData(
    _In_ PDEVICE_OBJECT Pdo,
    _In_ CONST DEVPROPKEY *PropertyKey,
    _In_ LCID Lcid,
    _In_ ULONG Flags,
    _In_ DEVPROPTYPE Type,
    _In_ ULONG Size,
    _In_opt_ PVOID Data)
{
    HANDLE PropertyDataKey;
    NTSTATUS Status;

    PAGED_CODE();

    UNREFERENCED_PARAMETER(Flags);

    if (!Pdo || !PropertyKey)
        return STATUS_INVALID_PARAMETER;

    if (!Data)
    {
        Status = IopOpenDevicePropertyDataKey(Pdo, PropertyKey, Lcid, FALSE, &PropertyDataKey);
        if (Status == STATUS_OBJECT_NAME_NOT_FOUND)
            return STATUS_SUCCESS;
        if (!NT_SUCCESS(Status))
            return Status;

        Status = ZwDeleteKey(PropertyDataKey);
        ZwClose(PropertyDataKey);
        return Status;
    }

    Status = IopOpenDevicePropertyDataKey(Pdo,
                                          PropertyKey,
                                          Lcid,
                                          TRUE,
                                          &PropertyDataKey);
    if (!NT_SUCCESS(Status))
        return Status;

    Status = IopWriteDevicePropertyValue(PropertyDataKey, Type, Size, Data);
    ZwClose(PropertyDataKey);
    return Status;
}

_IRQL_requires_max_(PASSIVE_LEVEL)
_Must_inspect_result_
NTKRNLVISTAAPI
NTSTATUS
NTAPI
IoGetDevicePropertyData(
    _In_ PDEVICE_OBJECT Pdo,
    _In_ CONST DEVPROPKEY *PropertyKey,
    _In_ LCID Lcid,
    _Reserved_ ULONG Flags,
    _In_ ULONG Size,
    _Out_ PVOID Data,
    _Out_ PULONG RequiredSize,
    _Out_ PDEVPROPTYPE Type)
{
    HANDLE PropertyDataKey;
    NTSTATUS Status;

    PAGED_CODE();

    UNREFERENCED_PARAMETER(Flags);

    if (!Pdo || !PropertyKey || !RequiredSize || !Type)
        return STATUS_INVALID_PARAMETER;

    *RequiredSize = 0;
    *Type = DEVPROP_TYPE_EMPTY;

    Status = IopOpenDevicePropertyDataKey(Pdo,
                                          PropertyKey,
                                          Lcid,
                                          FALSE,
                                          &PropertyDataKey);
    if (NT_SUCCESS(Status))
    {
        Status = IopReadDevicePropertyValue(PropertyDataKey,
                                            Size,
                                            Data,
                                            RequiredSize,
                                            Type);
        ZwClose(PropertyDataKey);
        if (NT_SUCCESS(Status) || Status == STATUS_BUFFER_TOO_SMALL)
            return Status;
    }

    return IopQueryMappedDevicePropertyData(Pdo,
                                            PropertyKey,
                                            Size,
                                            Data,
                                            RequiredSize,
                                            Type);
}

_IRQL_requires_max_(PASSIVE_LEVEL)
_Must_inspect_result_
NTKRNLVISTAAPI
NTSTATUS
NTAPI
IoSetDeviceInterfacePropertyData(
    _In_ PUNICODE_STRING SymbolicLinkName,
    _In_ CONST DEVPROPKEY *PropertyKey,
    _In_ LCID Lcid,
    _In_ ULONG Flags,
    _In_ DEVPROPTYPE Type,
    _In_ ULONG Size,
    _In_reads_bytes_opt_(Size) PVOID Data)
{
    HANDLE PropertyDataKey;
    NTSTATUS Status;

    PAGED_CODE();

    UNREFERENCED_PARAMETER(Flags);

    if (!SymbolicLinkName || !PropertyKey || (Size != 0 && Data == NULL))
        return STATUS_INVALID_PARAMETER;

    Status = IopOpenInterfacePropertyDataKey(SymbolicLinkName,
                                             PropertyKey,
                                             Lcid,
                                             TRUE,
                                             &PropertyDataKey);
    if (!NT_SUCCESS(Status))
        return Status;

    Status = IopWriteDevicePropertyValue(PropertyDataKey, Type, Size, Data);
    ZwClose(PropertyDataKey);
    return Status;
}

_IRQL_requires_max_(PASSIVE_LEVEL)
_Must_inspect_result_
NTKRNLVISTAAPI
NTSTATUS
NTAPI
IoGetDeviceInterfacePropertyData(
    _In_ PUNICODE_STRING SymbolicLinkName,
    _In_ CONST DEVPROPKEY *PropertyKey,
    _In_ LCID Lcid,
    _Reserved_ ULONG Flags,
    _In_ ULONG Size,
    _Out_writes_bytes_to_(Size, *RequiredSize) PVOID Data,
    _Out_ PULONG RequiredSize,
    _Out_ PDEVPROPTYPE Type)
{
    HANDLE PropertyDataKey;
    NTSTATUS Status;

    PAGED_CODE();
    UNREFERENCED_PARAMETER(Flags);

    if ((SymbolicLinkName == NULL) ||
        (SymbolicLinkName->Buffer == NULL) ||
        (SymbolicLinkName->Length == 0) ||
        (PropertyKey == NULL) ||
        (RequiredSize == NULL) || (Type == NULL))
    {
        return STATUS_INVALID_PARAMETER;
    }

    Status = IopOpenInterfacePropertyDataKey(SymbolicLinkName,
                                             PropertyKey,
                                             Lcid,
                                             FALSE,
                                             &PropertyDataKey);
    if (Status == STATUS_INVALID_PARAMETER)
        return STATUS_OBJECT_NAME_NOT_FOUND;
    if (!NT_SUCCESS(Status))
        return Status;

    *RequiredSize = 0;
    *Type = DEVPROP_TYPE_EMPTY;
    Status = IopReadDevicePropertyValue(PropertyDataKey,
                                        Size,
                                        Data,
                                        RequiredSize,
                                        Type);
    ZwClose(PropertyDataKey);
    return Status;
}

NTKRNLVISTAAPI
IO_PRIORITY_HINT
NTAPI
IoGetIoPriorityHint(
    _In_ PIRP Irp)
{
    return IoPriorityNormal;
}

NTKRNLVISTAAPI
VOID
NTAPI
IoSetMasterIrpStatus(
    _Inout_ PIRP MasterIrp,
    _In_ NTSTATUS Status)
{
    NTSTATUS MasterStatus = MasterIrp->IoStatus.Status;

    if (Status == STATUS_FT_READ_FROM_COPY)
    {
        return;
    }

    if ((Status == STATUS_VERIFY_REQUIRED) ||
        (MasterStatus == STATUS_SUCCESS && !NT_SUCCESS(Status)) ||
        (!NT_SUCCESS(MasterStatus) && !NT_SUCCESS(Status) && Status > MasterStatus))
    {
        MasterIrp->IoStatus.Status = Status;
    }
}
