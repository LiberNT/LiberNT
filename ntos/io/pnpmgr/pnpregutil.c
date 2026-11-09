/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     What the registry says about a device and how to reach it
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#include "../ioint.h"
#define NDEBUG
#include <debug.h>

/* GLOBALS ********************************************************************/

IO_BUS_TYPE_GUID_LIST PnpBusTypeGuidList;
ERESOURCE PpRegistryDeviceResource;

/* PRIVATE DATA ***************************************************************/

static const UNICODE_STRING IopEnumKeyName =
    RTL_CONSTANT_STRING(ENUM_ROOT);

static const UNICODE_STRING IopClassKeyName = RTL_CONSTANT_STRING(
    L"\\Registry\\Machine\\System\\CurrentControlSet\\Control\\Class");

/* PRIVATE FUNCTIONS **********************************************************/

NTSTATUS
NTAPI
IopOpenRegistryKeyEx(
    PHANDLE KeyHandle,
    HANDLE ParentKey,
    PUNICODE_STRING Name,
    ACCESS_MASK DesiredAccess)
{
    OBJECT_ATTRIBUTES ObjectAttributes;

    PAGED_CODE();

    *KeyHandle = NULL;

    InitializeObjectAttributes(&ObjectAttributes,
                               Name,
                               OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE,
                               ParentKey,
                               NULL);

    return ZwOpenKey(KeyHandle, DesiredAccess, &ObjectAttributes);
}

NTSTATUS
NTAPI
IopCreateRegistryKeyEx(
    OUT PHANDLE Handle,
    IN HANDLE BaseHandle OPTIONAL,
    IN PUNICODE_STRING KeyName,
    IN ACCESS_MASK DesiredAccess,
    IN ULONG CreateOptions,
    OUT PULONG Disposition OPTIONAL)
{
    OBJECT_ATTRIBUTES ObjectAttributes;

    PAGED_CODE();

    *Handle = NULL;

    InitializeObjectAttributes(&ObjectAttributes,
                               KeyName,
                               OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE,
                               BaseHandle,
                               NULL);

    return ZwCreateKey(Handle,
                       DesiredAccess,
                       &ObjectAttributes,
                       0,
                       NULL,
                       CreateOptions,
                       Disposition);
}

NTSTATUS
NTAPI
IopGetRegistryValue(
    IN HANDLE Handle,
    IN PWSTR ValueName,
    OUT PKEY_VALUE_FULL_INFORMATION *Information)
{
    PKEY_VALUE_FULL_INFORMATION Buffer;
    UNICODE_STRING Name;
    NTSTATUS Status;
    ULONG Length = 0;

    PAGED_CODE();

    *Information = NULL;

    RtlInitUnicodeString(&Name, ValueName);

    Status = ZwQueryValueKey(Handle,
                             &Name,
                             KeyValueFullInformation,
                             NULL,
                             0,
                             &Length);
    if (Status != STATUS_BUFFER_TOO_SMALL &&
        Status != STATUS_BUFFER_OVERFLOW)
    {
        return NT_SUCCESS(Status) ? STATUS_UNSUCCESSFUL : Status;
    }

    Buffer = ExAllocatePoolZero(PagedPool, Length, TAG_IO);
    if (Buffer == NULL) return STATUS_INSUFFICIENT_RESOURCES;

    Status = ZwQueryValueKey(Handle,
                             &Name,
                             KeyValueFullInformation,
                             Buffer,
                             Length,
                             &Length);
    if (!NT_SUCCESS(Status))
    {
        ExFreePoolWithTag(Buffer, TAG_IO);
        return Status;
    }

    *Information = Buffer;

    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
IopCreateDeviceKeyPath(
    IN PCUNICODE_STRING RegistryPath,
    IN ULONG CreateOptions,
    OUT PHANDLE Handle)
{
    UNICODE_STRING Component;
    UNICODE_STRING Remaining;
    HANDLE Parent = NULL;
    HANDLE Current;
    NTSTATUS Status;
    USHORT Index;

    PAGED_CODE();

    *Handle = NULL;

    Remaining = *(PUNICODE_STRING)RegistryPath;

    if (Remaining.Length < sizeof(WCHAR) ||
        Remaining.Buffer[0] != OBJ_NAME_PATH_SEPARATOR)
    {
        return STATUS_INVALID_PARAMETER;
    }

    while (Remaining.Length != 0)
    {
        if (Remaining.Buffer[0] == OBJ_NAME_PATH_SEPARATOR)
        {
            Remaining.Buffer++;
            Remaining.Length -= sizeof(WCHAR);
            continue;
        }

        Component.Buffer = Remaining.Buffer;
        Component.Length = 0;

        for (Index = 0; Index < Remaining.Length / sizeof(WCHAR); Index++)
        {
            if (Remaining.Buffer[Index] == OBJ_NAME_PATH_SEPARATOR) break;
            Component.Length += sizeof(WCHAR);
        }

        Component.MaximumLength = Component.Length;

        Remaining.Buffer += Component.Length / sizeof(WCHAR);
        Remaining.Length -= Component.Length;

        if (Parent == NULL)
        {
            UNICODE_STRING Absolute;

            Absolute.Buffer = (PWCHAR)RegistryPath->Buffer;
            Absolute.Length =
                (USHORT)((PUCHAR)Component.Buffer - (PUCHAR)RegistryPath->Buffer) +
                Component.Length;
            Absolute.MaximumLength = Absolute.Length;

            Status = IopCreateRegistryKeyEx(&Current,
                                            NULL,
                                            &Absolute,
                                            KEY_ALL_ACCESS,
                                            CreateOptions,
                                            NULL);
        }
        else
        {
            Status = IopCreateRegistryKeyEx(&Current,
                                            Parent,
                                            &Component,
                                            KEY_ALL_ACCESS,
                                            CreateOptions,
                                            NULL);
        }

        if (Parent != NULL) ZwClose(Parent);

        if (!NT_SUCCESS(Status)) return Status;

        Parent = Current;
    }

    if (Parent == NULL) return STATUS_INVALID_PARAMETER;

    *Handle = Parent;

    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
PnpRegMultiSzToUnicodeStrings(
    IN PKEY_VALUE_FULL_INFORMATION KeyValueInformation,
    OUT PUNICODE_STRING *UnicodeStringList,
    OUT PULONG UnicodeStringCount)
{
    PUNICODE_STRING List;
    PWCHAR Cursor;
    PWCHAR Limit;
    ULONG Count = 0;
    ULONG Index;

    PAGED_CODE();

    *UnicodeStringList = NULL;
    *UnicodeStringCount = 0;

    if (KeyValueInformation->Type != REG_MULTI_SZ)
    {
        return STATUS_INVALID_PARAMETER;
    }

    if (KeyValueInformation->DataLength < sizeof(WCHAR))
    {
        return STATUS_INVALID_PARAMETER;
    }

    Cursor = (PWCHAR)((PUCHAR)KeyValueInformation +
                      KeyValueInformation->DataOffset);
    Limit = Cursor + KeyValueInformation->DataLength / sizeof(WCHAR);

    while (Cursor < Limit && *Cursor != UNICODE_NULL)
    {
        Count++;

        while (Cursor < Limit && *Cursor != UNICODE_NULL) Cursor++;
        if (Cursor < Limit) Cursor++;
    }

    if (Count == 0) return STATUS_INVALID_PARAMETER;

    List = ExAllocatePoolZero(PagedPool,
                              Count * sizeof(UNICODE_STRING),
                              TAG_IO);
    if (List == NULL) return STATUS_INSUFFICIENT_RESOURCES;

    Cursor = (PWCHAR)((PUCHAR)KeyValueInformation +
                      KeyValueInformation->DataOffset);

    for (Index = 0; Index < Count; Index++)
    {
        PWCHAR Start = Cursor;
        USHORT Length = 0;

        while (Cursor < Limit && *Cursor != UNICODE_NULL)
        {
            Length += sizeof(WCHAR);
            Cursor++;
        }

        if (Cursor < Limit) Cursor++;

        List[Index].Buffer = ExAllocatePoolZero(PagedPool,
                                                Length + sizeof(WCHAR),
                                                TAG_IO);
        if (List[Index].Buffer == NULL)
        {
            PnpFreeUnicodeStringList(List, Index);
            return STATUS_INSUFFICIENT_RESOURCES;
        }

        RtlCopyMemory(List[Index].Buffer, Start, Length);

        List[Index].Length = Length;
        List[Index].MaximumLength = Length + sizeof(WCHAR);
    }

    *UnicodeStringList = List;
    *UnicodeStringCount = Count;

    return STATUS_SUCCESS;
}

BOOLEAN
NTAPI
PnpRegSzToString(
    IN PWCHAR RegSzData,
    IN ULONG RegSzLength,
    OUT PUSHORT StringLength OPTIONAL)
{
    PWCHAR Cursor = RegSzData;
    PWCHAR Limit = RegSzData + RegSzLength / sizeof(WCHAR);

    PAGED_CODE();

    while (Cursor < Limit && *Cursor != UNICODE_NULL) Cursor++;

    if (Cursor == Limit) return FALSE;

    if (StringLength != NULL)
    {
        *StringLength = (USHORT)((Cursor - RegSzData) * sizeof(WCHAR));
    }

    return TRUE;
}

VOID
PnpFreeUnicodeStringList(
    _In_opt_ PUNICODE_STRING UnicodeStringList,
    _In_ ULONG StringCount)
{
    ULONG Index;

    PAGED_CODE();

    if (UnicodeStringList == NULL) return;

    for (Index = 0; Index < StringCount; Index++)
    {
        if (UnicodeStringList[Index].Buffer != NULL)
        {
            ExFreePoolWithTag(UnicodeStringList[Index].Buffer, TAG_IO);
        }
    }

    ExFreePoolWithTag(UnicodeStringList, TAG_IO);
}

NTSTATUS
PnpBusTypeGuidGet(
    _In_ USHORT Index,
    _Out_ LPGUID BusTypeGuid)
{
    NTSTATUS Status = STATUS_OBJECT_NAME_NOT_FOUND;

    PAGED_CODE();

    ExAcquireFastMutex(&PnpBusTypeGuidList.Lock);

    if (Index < PnpBusTypeGuidList.GuidCount)
    {
        RtlCopyMemory(BusTypeGuid,
                      &PnpBusTypeGuidList.Guids[Index],
                      sizeof(GUID));
        Status = STATUS_SUCCESS;
    }

    ExReleaseFastMutex(&PnpBusTypeGuidList.Lock);

    return Status;
}

USHORT
IopGetBusTypeGuidIndex(
    _In_ LPGUID BusTypeGuid)
{
    PGUID Grown;
    USHORT Index;

    PAGED_CODE();

    ExAcquireFastMutex(&PnpBusTypeGuidList.Lock);

    for (Index = 0; Index < (USHORT)PnpBusTypeGuidList.GuidCount; Index++)
    {
        if (RtlCompareMemory(&PnpBusTypeGuidList.Guids[Index],
                             BusTypeGuid,
                             sizeof(GUID)) == sizeof(GUID))
        {
            ExReleaseFastMutex(&PnpBusTypeGuidList.Lock);
            return Index;
        }
    }

    if (PnpBusTypeGuidList.GuidCount == PnpBusTypeGuidList.AllocatedCount)
    {
        ULONG NewCount = PnpBusTypeGuidList.AllocatedCount + 8;

        Grown = ExAllocatePoolZero(PagedPool,
                                   NewCount * sizeof(GUID),
                                   TAG_PNP_GUIDS);
        if (Grown == NULL)
        {
            ExReleaseFastMutex(&PnpBusTypeGuidList.Lock);
            return (USHORT)-1;
        }

        if (PnpBusTypeGuidList.Guids != NULL)
        {
            RtlCopyMemory(Grown,
                          PnpBusTypeGuidList.Guids,
                          PnpBusTypeGuidList.GuidCount * sizeof(GUID));
            ExFreePoolWithTag(PnpBusTypeGuidList.Guids, TAG_PNP_GUIDS);
        }

        PnpBusTypeGuidList.Guids = Grown;
        PnpBusTypeGuidList.AllocatedCount = NewCount;
    }

    Index = (USHORT)PnpBusTypeGuidList.GuidCount;

    RtlCopyMemory(&PnpBusTypeGuidList.Guids[Index],
                  BusTypeGuid,
                  sizeof(GUID));
    PnpBusTypeGuidList.GuidCount++;

    ExReleaseFastMutex(&PnpBusTypeGuidList.Lock);

    return Index;
}

NTSTATUS
PnpDeviceObjectToDeviceInstance(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Out_ PHANDLE InstanceKey,
    _In_ ACCESS_MASK DesiredAccess)
{
    PDEVICE_NODE DeviceNode;
    NTSTATUS Status;
    HANDLE EnumHandle;

    PAGED_CODE();

    *InstanceKey = NULL;

    DeviceNode = IopGetDeviceNode(DeviceObject);
    if (DeviceNode == NULL) return STATUS_INVALID_DEVICE_REQUEST;
    if (DeviceNode->InstancePath.Length == 0) return STATUS_NOT_FOUND;

    Status = IopOpenRegistryKeyEx(&EnumHandle,
                                  NULL,
                                  (PUNICODE_STRING)&IopEnumKeyName,
                                  KEY_READ);
    if (!NT_SUCCESS(Status)) return Status;

    Status = IopOpenRegistryKeyEx(InstanceKey,
                                  EnumHandle,
                                  &DeviceNode->InstancePath,
                                  DesiredAccess);

    ZwClose(EnumHandle);

    return Status;
}

NTSTATUS
PiGetDeviceRegistryProperty(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ ULONG ValueType,
    _In_ PCWSTR ValueName,
    _In_opt_ PCWSTR KeyName,
    _Out_opt_ PVOID Buffer,
    _Inout_ PULONG BufferLength)
{
    PKEY_VALUE_FULL_INFORMATION Information = NULL;
    UNICODE_STRING SubKeyName;
    NTSTATUS Status;
    HANDLE InstanceKey;
    HANDLE SubKey;

    PAGED_CODE();

    Status = PnpDeviceObjectToDeviceInstance(DeviceObject,
                                             &InstanceKey,
                                             KEY_READ);
    if (!NT_SUCCESS(Status)) return Status;

    if (KeyName != NULL)
    {
        RtlInitUnicodeString(&SubKeyName, KeyName);

        Status = IopOpenRegistryKeyEx(&SubKey,
                                      InstanceKey,
                                      &SubKeyName,
                                      KEY_READ);
        ZwClose(InstanceKey);

        if (!NT_SUCCESS(Status)) return Status;

        InstanceKey = SubKey;
    }

    Status = IopGetRegistryValue(InstanceKey, (PWSTR)ValueName, &Information);
    ZwClose(InstanceKey);

    if (!NT_SUCCESS(Status)) return Status;

    if (ValueType != REG_NONE && Information->Type != ValueType)
    {
        ExFreePool(Information);
        return STATUS_INVALID_PARAMETER_MIX;
    }

    if (*BufferLength < Information->DataLength)
    {
        *BufferLength = Information->DataLength;
        ExFreePool(Information);
        return STATUS_BUFFER_TOO_SMALL;
    }

    if (Buffer != NULL)
    {
        RtlCopyMemory(Buffer,
                      (PUCHAR)Information + Information->DataOffset,
                      Information->DataLength);
    }

    *BufferLength = Information->DataLength;

    ExFreePool(Information);

    return STATUS_SUCCESS;
}

NTSTATUS
IopGetParentIdPrefix(
    _In_ PDEVICE_NODE DeviceNode,
    _Out_ PUNICODE_STRING Prefix)
{
    UNICODE_STRING Separator;
    PWCHAR Buffer;
    USHORT Length;
    USHORT Index;

    PAGED_CODE();

    RtlInitEmptyUnicodeString(Prefix, NULL, 0);

    if (DeviceNode->Parent == NULL) return STATUS_NOT_FOUND;
    if (DeviceNode->Parent->InstancePath.Length == 0) return STATUS_NOT_FOUND;

    Length = DeviceNode->Parent->InstancePath.Length + sizeof(WCHAR);

    Buffer = ExAllocatePoolZero(PagedPool, Length + sizeof(WCHAR), TAG_IO);
    if (Buffer == NULL) return STATUS_INSUFFICIENT_RESOURCES;

    Prefix->Buffer = Buffer;
    Prefix->Length = 0;
    Prefix->MaximumLength = Length + sizeof(WCHAR);

    RtlAppendUnicodeStringToString(Prefix,
                                   &DeviceNode->Parent->InstancePath);

    for (Index = 0; Index < Prefix->Length / sizeof(WCHAR); Index++)
    {
        if (Prefix->Buffer[Index] == OBJ_NAME_PATH_SEPARATOR)
        {
            Prefix->Buffer[Index] = L'&';
        }
    }

    RtlInitUnicodeString(&Separator, L"&");
    RtlAppendUnicodeStringToString(Prefix, &Separator);

    return STATUS_SUCCESS;
}

NTSTATUS
IopSetDeviceInstanceData(
    _In_ HANDLE InstanceKey,
    _In_ PDEVICE_NODE DeviceNode)
{
    UNICODE_STRING ValueName;
    NTSTATUS Status;
    HANDLE ControlKey;
    ULONG Value;

    PAGED_CODE();

    RtlInitUnicodeString(&ValueName, L"Control");

    Status = IopCreateRegistryKeyEx(&ControlKey,
                                    InstanceKey,
                                    &ValueName,
                                    KEY_ALL_ACCESS,
                                    REG_OPTION_VOLATILE,
                                    NULL);
    if (NT_SUCCESS(Status))
    {
        RtlInitUnicodeString(&ValueName, L"ActiveService");

        if (DeviceNode->ServiceName.Length != 0)
        {
            ZwSetValueKey(ControlKey,
                          &ValueName,
                          0,
                          REG_SZ,
                          DeviceNode->ServiceName.Buffer,
                          DeviceNode->ServiceName.Length + sizeof(WCHAR));
        }

        ZwClose(ControlKey);
    }

    if (DeviceNode->ChildBusTypeIndex != (USHORT)-1)
    {
        RtlInitUnicodeString(&ValueName, L"BusTypeGuid");

        Value = DeviceNode->ChildBusTypeIndex;

        ZwSetValueKey(InstanceKey,
                      &ValueName,
                      0,
                      REG_DWORD,
                      &Value,
                      sizeof(Value));
    }

    RtlInitUnicodeString(&ValueName, L"ConfigFlags");

    Value = 0;
    ZwSetValueKey(InstanceKey, &ValueName, 0, REG_DWORD, &Value, sizeof(Value));

    return STATUS_SUCCESS;
}

/* PUBLIC FUNCTIONS ***********************************************************/

NTSTATUS
NTAPI
IoOpenDeviceRegistryKey(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ ULONG DevInstKeyType,
    _In_ ACCESS_MASK DesiredAccess,
    _Out_ PHANDLE DevInstRegKey)
{
    PKEY_VALUE_FULL_INFORMATION Information = NULL;
    UNICODE_STRING SubKeyName;
    NTSTATUS Status;
    HANDLE InstanceKey;
    HANDLE ClassKey;
    HANDLE Result;

    PAGED_CODE();

    *DevInstRegKey = NULL;

    if (DevInstKeyType & PLUGPLAY_REGKEY_DEVICE)
    {
        Status = PnpDeviceObjectToDeviceInstance(DeviceObject,
                                                 &InstanceKey,
                                                 DesiredAccess);
        if (!NT_SUCCESS(Status)) return Status;

        if (!(DevInstKeyType & PLUGPLAY_REGKEY_CURRENT_HWPROFILE))
        {
            RtlInitUnicodeString(&SubKeyName, L"Device Parameters");

            Status = IopCreateRegistryKeyEx(&Result,
                                            InstanceKey,
                                            &SubKeyName,
                                            DesiredAccess,
                                            REG_OPTION_NON_VOLATILE,
                                            NULL);
            ZwClose(InstanceKey);

            if (!NT_SUCCESS(Status)) return Status;

            *DevInstRegKey = Result;

            return STATUS_SUCCESS;
        }

        *DevInstRegKey = InstanceKey;

        return STATUS_SUCCESS;
    }

    if (!(DevInstKeyType & PLUGPLAY_REGKEY_DRIVER))
    {
        return STATUS_INVALID_PARAMETER;
    }

    Status = PnpDeviceObjectToDeviceInstance(DeviceObject,
                                             &InstanceKey,
                                             KEY_READ);
    if (!NT_SUCCESS(Status)) return Status;

    Status = IopGetRegistryValue(InstanceKey, L"Driver", &Information);
    ZwClose(InstanceKey);

    if (!NT_SUCCESS(Status)) return Status;

    if (Information->Type != REG_SZ ||
        Information->DataLength <= sizeof(WCHAR))
    {
        ExFreePool(Information);
        return STATUS_OBJECT_NAME_NOT_FOUND;
    }

    SubKeyName.Buffer = (PWCHAR)((PUCHAR)Information +
                                 Information->DataOffset);
    SubKeyName.Length = (USHORT)(Information->DataLength - sizeof(WCHAR));
    SubKeyName.MaximumLength = SubKeyName.Length;

    Status = IopOpenRegistryKeyEx(&ClassKey,
                                  NULL,
                                  (PUNICODE_STRING)&IopClassKeyName,
                                  KEY_READ);
    if (!NT_SUCCESS(Status))
    {
        ExFreePool(Information);
        return Status;
    }

    Status = IopCreateRegistryKeyEx(&Result,
                                    ClassKey,
                                    &SubKeyName,
                                    DesiredAccess,
                                    REG_OPTION_NON_VOLATILE,
                                    NULL);

    ZwClose(ClassKey);
    ExFreePool(Information);

    if (!NT_SUCCESS(Status)) return Status;

    *DevInstRegKey = Result;

    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
IoGetDeviceProperty(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ DEVICE_REGISTRY_PROPERTY DeviceProperty,
    _In_ ULONG BufferLength,
    _Out_writes_bytes_opt_(BufferLength) PVOID PropertyBuffer,
    _Out_ PULONG ResultLength)
{
    PDEVICE_NODE DeviceNode;
    ULONG Length = BufferLength;
    NTSTATUS Status;
    ULONG Value;
    PCWSTR ValueName = NULL;
    ULONG ValueType = REG_NONE;

    PAGED_CODE();

    *ResultLength = 0;

    DeviceNode = IopGetDeviceNode(DeviceObject);
    if (DeviceNode == NULL) return STATUS_INVALID_DEVICE_REQUEST;

    switch (DeviceProperty)
    {
        case DevicePropertyBusTypeGuid:
            if (BufferLength < sizeof(GUID))
            {
                *ResultLength = sizeof(GUID);
                return STATUS_BUFFER_TOO_SMALL;
            }

            Status = PnpBusTypeGuidGet(DeviceNode->ChildBusTypeIndex,
                                       PropertyBuffer);
            if (NT_SUCCESS(Status)) *ResultLength = sizeof(GUID);

            return Status;

        case DevicePropertyLegacyBusType:
            if (BufferLength < sizeof(INTERFACE_TYPE))
            {
                *ResultLength = sizeof(INTERFACE_TYPE);
                return STATUS_BUFFER_TOO_SMALL;
            }

            *(PINTERFACE_TYPE)PropertyBuffer = DeviceNode->ChildInterfaceType;
            *ResultLength = sizeof(INTERFACE_TYPE);

            return STATUS_SUCCESS;

        case DevicePropertyBusNumber:
            if (BufferLength < sizeof(ULONG))
            {
                *ResultLength = sizeof(ULONG);
                return STATUS_BUFFER_TOO_SMALL;
            }

            *(PULONG)PropertyBuffer = DeviceNode->ChildBusNumber;
            *ResultLength = sizeof(ULONG);

            return STATUS_SUCCESS;

        case DevicePropertyAddress:
            ValueName = L"Address";
            ValueType = REG_DWORD;
            break;

        case DevicePropertyEnumeratorName:
        {
            USHORT Index;

            if (DeviceNode->InstancePath.Length == 0)
            {
                return STATUS_OBJECT_NAME_NOT_FOUND;
            }

            for (Index = 0;
                 Index < DeviceNode->InstancePath.Length / sizeof(WCHAR);
                 Index++)
            {
                if (DeviceNode->InstancePath.Buffer[Index] ==
                    OBJ_NAME_PATH_SEPARATOR)
                {
                    break;
                }
            }

            Length = (ULONG)(Index * sizeof(WCHAR) + sizeof(WCHAR));
            *ResultLength = Length;

            if (BufferLength < Length) return STATUS_BUFFER_TOO_SMALL;

            RtlCopyMemory(PropertyBuffer,
                          DeviceNode->InstancePath.Buffer,
                          Index * sizeof(WCHAR));
            ((PWCHAR)PropertyBuffer)[Index] = UNICODE_NULL;

            return STATUS_SUCCESS;
        }

        case DevicePropertyPhysicalDeviceObjectName:
        {
            POBJECT_NAME_INFORMATION NameInfo;
            ULONG NameLength = 0;

            Status = ObQueryNameString(DeviceNode->PhysicalDeviceObject,
                                       NULL,
                                       0,
                                       &NameLength);
            if (Status != STATUS_INFO_LENGTH_MISMATCH &&
                Status != STATUS_BUFFER_OVERFLOW &&
                Status != STATUS_BUFFER_TOO_SMALL)
            {
                return NT_SUCCESS(Status) ? STATUS_UNSUCCESSFUL : Status;
            }

            NameInfo = ExAllocatePoolZero(PagedPool, NameLength, TAG_IO_NAME);
            if (NameInfo == NULL) return STATUS_INSUFFICIENT_RESOURCES;

            Status = ObQueryNameString(DeviceNode->PhysicalDeviceObject,
                                       NameInfo,
                                       NameLength,
                                       &NameLength);
            if (!NT_SUCCESS(Status))
            {
                ExFreePoolWithTag(NameInfo, TAG_IO_NAME);
                return Status;
            }

            *ResultLength = NameInfo->Name.Length + sizeof(WCHAR);

            if (BufferLength < *ResultLength)
            {
                ExFreePoolWithTag(NameInfo, TAG_IO_NAME);
                return STATUS_BUFFER_TOO_SMALL;
            }

            RtlCopyMemory(PropertyBuffer,
                          NameInfo->Name.Buffer,
                          NameInfo->Name.Length);
            ((PWCHAR)PropertyBuffer)[NameInfo->Name.Length / sizeof(WCHAR)] =
                UNICODE_NULL;

            ExFreePoolWithTag(NameInfo, TAG_IO_NAME);

            return STATUS_SUCCESS;
        }

        case DevicePropertyInstallState:
            if (BufferLength < sizeof(ULONG))
            {
                *ResultLength = sizeof(ULONG);
                return STATUS_BUFFER_TOO_SMALL;
            }

            Value = (DeviceNode->Flags & DNF_UNINSTALLED) ? 1 : 0;
            *(PULONG)PropertyBuffer = Value;
            *ResultLength = sizeof(ULONG);

            return STATUS_SUCCESS;

        case DevicePropertyRemovalPolicy:
            if (BufferLength < sizeof(ULONG))
            {
                *ResultLength = sizeof(ULONG);
                return STATUS_BUFFER_TOO_SMALL;
            }

            *(PULONG)PropertyBuffer = DeviceNode->RemovalPolicy;
            *ResultLength = sizeof(ULONG);

            return STATUS_SUCCESS;

        case DevicePropertyDeviceDescription:
            ValueName = L"DeviceDesc";
            ValueType = REG_SZ;
            break;

        case DevicePropertyHardwareID:
            ValueName = L"HardwareID";
            ValueType = REG_MULTI_SZ;
            break;

        case DevicePropertyCompatibleIDs:
            ValueName = L"CompatibleIDs";
            ValueType = REG_MULTI_SZ;
            break;

        case DevicePropertyClassName:
            ValueName = L"Class";
            ValueType = REG_SZ;
            break;

        case DevicePropertyClassGuid:
            ValueName = L"ClassGUID";
            ValueType = REG_SZ;
            break;

        case DevicePropertyDriverKeyName:
            ValueName = L"Driver";
            ValueType = REG_SZ;
            break;

        case DevicePropertyManufacturer:
            ValueName = L"Mfg";
            ValueType = REG_SZ;
            break;

        case DevicePropertyFriendlyName:
            ValueName = L"FriendlyName";
            ValueType = REG_SZ;
            break;

        case DevicePropertyLocationInformation:
            ValueName = L"LocationInformation";
            ValueType = REG_SZ;
            break;

        case DevicePropertyUINumber:
            ValueName = L"UINumber";
            ValueType = REG_DWORD;
            break;

        case DevicePropertyBootConfiguration:
            if (DeviceNode->BootResources == NULL)
            {
                return STATUS_OBJECT_NAME_NOT_FOUND;
            }

            *ResultLength =
                PnpDetermineResourceListSize(DeviceNode->BootResources);

            if (BufferLength < *ResultLength) return STATUS_BUFFER_TOO_SMALL;

            RtlCopyMemory(PropertyBuffer,
                          DeviceNode->BootResources,
                          *ResultLength);

            return STATUS_SUCCESS;

        default:
            return STATUS_INVALID_PARAMETER_2;
    }

    Status = PiGetDeviceRegistryProperty(DeviceObject,
                                         ValueType,
                                         ValueName,
                                         NULL,
                                         PropertyBuffer,
                                         &Length);

    *ResultLength = Length;

    return Status;
}
