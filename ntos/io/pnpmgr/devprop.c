/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     The typed property store of a device instance and an interface
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#include "../ioint.h"
#define INITGUID
#include <devpkey.h>
#include <cfg.h>
#define NDEBUG
#include <debug.h>

/* PRIVATE DATA ***************************************************************/

/* A stored property is the default value of its own key, and the registry type of that value carries the property type */
#define IOP_PROPERTY_TYPE_MARK 0xFFFF0000UL

#define IOP_PROPERTY_MAXIMUM_SIZE (64 * 1024)

#define IOP_PROPERTY_STACK_LIMIT 32

/* A device property that is a value of the device instance key, or of the key of the driver installed for it */
typedef struct _IOP_REGISTRY_PROPERTY
{
    CONST DEVPROPKEY *Key;
    DEVPROPTYPE Type;
    PCWSTR ValueName;
    BOOLEAN DriverKey;
    BOOLEAN Writable;
} IOP_REGISTRY_PROPERTY;
typedef CONST IOP_REGISTRY_PROPERTY *PCIOP_REGISTRY_PROPERTY;

static const IOP_REGISTRY_PROPERTY IopRegistryProperties[] =
{
    { &DEVPKEY_Device_DeviceDesc, DEVPROP_TYPE_STRING, L"DeviceDesc", FALSE, TRUE },
    { &DEVPKEY_Device_HardwareIds, DEVPROP_TYPE_STRING_LIST, L"HardwareID", FALSE, TRUE },
    { &DEVPKEY_Device_CompatibleIds, DEVPROP_TYPE_STRING_LIST, L"CompatibleIDs", FALSE, TRUE },
    { &DEVPKEY_Device_Service, DEVPROP_TYPE_STRING, L"Service", FALSE, FALSE },
    { &DEVPKEY_Device_Class, DEVPROP_TYPE_STRING, L"Class", FALSE, FALSE },
    { &DEVPKEY_Device_ClassGuid, DEVPROP_TYPE_GUID, L"ClassGUID", FALSE, FALSE },
    { &DEVPKEY_Device_Driver, DEVPROP_TYPE_STRING, L"Driver", FALSE, FALSE },
    { &DEVPKEY_Device_ConfigFlags, DEVPROP_TYPE_UINT32, L"ConfigFlags", FALSE, TRUE },
    { &DEVPKEY_Device_Manufacturer, DEVPROP_TYPE_STRING, L"Mfg", FALSE, TRUE },
    { &DEVPKEY_Device_FriendlyName, DEVPROP_TYPE_STRING, L"FriendlyName", FALSE, TRUE },
    { &DEVPKEY_Device_LocationInfo, DEVPROP_TYPE_STRING, L"LocationInformation", FALSE, TRUE },
    { &DEVPKEY_Device_Capabilities, DEVPROP_TYPE_UINT32, L"Capabilities", FALSE, FALSE },
    { &DEVPKEY_Device_UINumber, DEVPROP_TYPE_UINT32, L"UINumber", FALSE, FALSE },
    { &DEVPKEY_Device_UpperFilters, DEVPROP_TYPE_STRING_LIST, L"UpperFilters", FALSE, TRUE },
    { &DEVPKEY_Device_LowerFilters, DEVPROP_TYPE_STRING_LIST, L"LowerFilters", FALSE, TRUE },
    { &DEVPKEY_Device_ContainerId, DEVPROP_TYPE_GUID, L"ContainerID", FALSE, FALSE },
    { &DEVPKEY_Device_DriverDate, DEVPROP_TYPE_FILETIME, L"DriverDateData", TRUE, FALSE },
    { &DEVPKEY_Device_DriverVersion, DEVPROP_TYPE_STRING, L"DriverVersion", TRUE, FALSE },
    { &DEVPKEY_Device_DriverDesc, DEVPROP_TYPE_STRING, L"DriverDesc", TRUE, FALSE },
    { &DEVPKEY_Device_DriverInfPath, DEVPROP_TYPE_STRING, L"InfPath", TRUE, FALSE },
    { &DEVPKEY_Device_DriverInfSection, DEVPROP_TYPE_STRING, L"InfSection", TRUE, FALSE },
    { &DEVPKEY_Device_DriverInfSectionExt, DEVPROP_TYPE_STRING, L"InfSectionExt", TRUE, FALSE },
    { &DEVPKEY_Device_MatchingDeviceId, DEVPROP_TYPE_STRING, L"MatchingDeviceId", TRUE, FALSE },
    { &DEVPKEY_Device_DriverProvider, DEVPROP_TYPE_STRING, L"ProviderName", TRUE, FALSE },
};

/* A device property the device node answers through the older property call */
typedef struct _IOP_LEGACY_PROPERTY
{
    CONST DEVPROPKEY *Key;
    DEVPROPTYPE Type;
    DEVICE_REGISTRY_PROPERTY Property;
} IOP_LEGACY_PROPERTY;

static const IOP_LEGACY_PROPERTY IopLegacyProperties[] =
{
    { &DEVPKEY_Device_PDOName, DEVPROP_TYPE_STRING, DevicePropertyPhysicalDeviceObjectName },
    { &DEVPKEY_Device_BusTypeGuid, DEVPROP_TYPE_GUID, DevicePropertyBusTypeGuid },
    { &DEVPKEY_Device_LegacyBusType, DEVPROP_TYPE_UINT32, DevicePropertyLegacyBusType },
    { &DEVPKEY_Device_BusNumber, DEVPROP_TYPE_UINT32, DevicePropertyBusNumber },
    { &DEVPKEY_Device_EnumeratorName, DEVPROP_TYPE_STRING, DevicePropertyEnumeratorName },
    { &DEVPKEY_Device_Address, DEVPROP_TYPE_UINT32, DevicePropertyAddress },
    { &DEVPKEY_Device_RemovalPolicy, DEVPROP_TYPE_UINT32, DevicePropertyRemovalPolicy },
    { &DEVPKEY_Device_InstallState, DEVPROP_TYPE_UINT32, DevicePropertyInstallState },
};

static const UNICODE_STRING IopControlClassKeyName = RTL_CONSTANT_STRING(
    L"\\Registry\\Machine\\System\\CurrentControlSet\\Control\\Class");

static const UNICODE_STRING IopPropertiesKeyName =
    RTL_CONSTANT_STRING(L"Properties");

/* PRIVATE FUNCTIONS **********************************************************/

/* The byte count a fixed-width property type always carries, or zero */
static
ULONG
IopFixedPropertySize(
    _In_ DEVPROPTYPE Type)
{
    if ((Type & DEVPROP_MASK_TYPEMOD) != 0) return 0;

    switch (Type & DEVPROP_MASK_TYPE)
    {
        case DEVPROP_TYPE_SBYTE:
        case DEVPROP_TYPE_BYTE:
        case DEVPROP_TYPE_BOOLEAN:
            return 1;

        case DEVPROP_TYPE_INT16:
        case DEVPROP_TYPE_UINT16:
            return 2;

        case DEVPROP_TYPE_INT32:
        case DEVPROP_TYPE_UINT32:
        case DEVPROP_TYPE_FLOAT:
        case DEVPROP_TYPE_ERROR:
        case DEVPROP_TYPE_NTSTATUS:
        case DEVPROP_TYPE_DEVPROPTYPE:
            return 4;

        case DEVPROP_TYPE_INT64:
        case DEVPROP_TYPE_UINT64:
        case DEVPROP_TYPE_DOUBLE:
        case DEVPROP_TYPE_CURRENCY:
        case DEVPROP_TYPE_DATE:
        case DEVPROP_TYPE_FILETIME:
            return 8;

        case DEVPROP_TYPE_DECIMAL:
            return 16;

        case DEVPROP_TYPE_GUID:
            return (ULONG)sizeof(GUID);

        case DEVPROP_TYPE_DEVPROPKEY:
            return (ULONG)sizeof(DEVPROPKEY);

        default:
            return 0;
    }
}

static
BOOLEAN
IopIsPropertyTypeKnown(
    _In_ DEVPROPTYPE Type)
{
    DEVPROPTYPE Base = Type & DEVPROP_MASK_TYPE;

    if ((Type & ~(DEVPROP_MASK_TYPE | DEVPROP_MASK_TYPEMOD)) != 0)
    {
        return FALSE;
    }

    if (Base == DEVPROP_TYPE_EMPTY || Base == DEVPROP_TYPE_NULL)
    {
        return (Type & DEVPROP_MASK_TYPEMOD) == 0;
    }

    return Base <= DEVPROP_TYPE_STRING_INDIRECT;
}

static
NTSTATUS
IopCheckPropertyArguments(
    _In_ CONST DEVPROPKEY *PropertyKey,
    _In_ LCID Lcid,
    _In_ ULONG Flags,
    _In_ BOOLEAN SetOperation)
{
    if (PropertyKey == NULL) return STATUS_INVALID_PARAMETER;
    if (PropertyKey->pid < DEVPROPID_FIRST_USABLE)
    {
        return STATUS_INVALID_PARAMETER;
    }

    if (Lcid != LOCALE_NEUTRAL && Lcid != LOCALE_SYSTEM_DEFAULT)
    {
        return STATUS_INVALID_PARAMETER;
    }

    if (SetOperation)
    {
        if (Flags & ~PLUGPLAY_PROPERTY_PERSISTENT)
        {
            return STATUS_INVALID_PARAMETER;
        }

        return STATUS_SUCCESS;
    }

    if (Flags != 0) return STATUS_INVALID_PARAMETER;

    return STATUS_SUCCESS;
}

static
BOOLEAN
IopIsSamePropertyKey(
    _In_ CONST DEVPROPKEY *First,
    _In_ CONST DEVPROPKEY *Second)
{
    return (First->pid == Second->pid) &&
           RtlEqualMemory(&First->fmtid, &Second->fmtid, sizeof(GUID));
}

static
BOOLEAN
IopIsNotFoundStatus(
    _In_ NTSTATUS Status)
{
    return (Status == STATUS_OBJECT_NAME_NOT_FOUND) ||
           (Status == STATUS_OBJECT_PATH_NOT_FOUND) ||
           (Status == STATUS_UNSUCCESSFUL);
}

/* Nothing is written for the caller until there is a value to tell about */
static
NTSTATUS
IopReturnPropertyData(
    _In_ DEVPROPTYPE ValueType,
    _In_reads_bytes_opt_(ValueSize) CONST VOID *Value,
    _In_ ULONG ValueSize,
    _In_ ULONG Size,
    _Out_writes_bytes_to_opt_(Size, *RequiredSize) PVOID Data,
    _Out_ PULONG RequiredSize,
    _Out_ PDEVPROPTYPE Type)
{
    *Type = ValueType;
    *RequiredSize = ValueSize;

    if (ValueSize > Size) return STATUS_BUFFER_TOO_SMALL;
    if (ValueSize == 0) return STATUS_SUCCESS;
    if (Data == NULL) return STATUS_INVALID_PARAMETER;

    RtlCopyMemory(Data, Value, ValueSize);

    return STATUS_SUCCESS;
}

/*
 * The key of one stored property: Properties\{format}\<identifier>, the
 * identifier in four hex digits. The levels on the way down are made only when
 * the caller is writing.
 */
static
NTSTATUS
IopOpenPropertyKey(
    _In_ HANDLE OwnerKey,
    _In_ CONST DEVPROPKEY *PropertyKey,
    _In_ BOOLEAN Create,
    _In_ ACCESS_MASK DesiredAccess,
    _Out_ PHANDLE PropertyHandle)
{
    UNICODE_STRING FormatName;
    UNICODE_STRING IdName;
    WCHAR IdBuffer[9];
    NTSTATUS Status;
    HANDLE Properties;
    HANDLE Format;

    PAGED_CODE();

    *PropertyHandle = NULL;

    if (Create)
    {
        Status = IopCreateRegistryKeyEx(&Properties,
                                        OwnerKey,
                                        (PUNICODE_STRING)&IopPropertiesKeyName,
                                        KEY_ALL_ACCESS,
                                        REG_OPTION_NON_VOLATILE,
                                        NULL);
    }
    else
    {
        Status = IopOpenRegistryKeyEx(&Properties,
                                      OwnerKey,
                                      (PUNICODE_STRING)&IopPropertiesKeyName,
                                      KEY_READ);
    }

    if (!NT_SUCCESS(Status)) return Status;

    Status = RtlStringFromGUID(&PropertyKey->fmtid, &FormatName);
    if (!NT_SUCCESS(Status))
    {
        ZwClose(Properties);
        return Status;
    }

    RtlDowncaseUnicodeString(&FormatName, &FormatName, FALSE);

    if (Create)
    {
        Status = IopCreateRegistryKeyEx(&Format,
                                        Properties,
                                        &FormatName,
                                        KEY_ALL_ACCESS,
                                        REG_OPTION_NON_VOLATILE,
                                        NULL);
    }
    else
    {
        Status = IopOpenRegistryKeyEx(&Format,
                                      Properties,
                                      &FormatName,
                                      KEY_READ);
    }

    RtlFreeUnicodeString(&FormatName);
    ZwClose(Properties);

    if (!NT_SUCCESS(Status)) return Status;

    Status = RtlStringCbPrintfW(IdBuffer,
                                sizeof(IdBuffer),
                                L"%04X",
                                PropertyKey->pid);
    if (!NT_SUCCESS(Status))
    {
        ZwClose(Format);
        return Status;
    }

    RtlInitUnicodeString(&IdName, IdBuffer);

    if (Create)
    {
        Status = IopCreateRegistryKeyEx(PropertyHandle,
                                        Format,
                                        &IdName,
                                        KEY_ALL_ACCESS,
                                        REG_OPTION_NON_VOLATILE,
                                        NULL);
    }
    else
    {
        Status = IopOpenRegistryKeyEx(PropertyHandle,
                                      Format,
                                      &IdName,
                                      DesiredAccess);
    }

    ZwClose(Format);

    return Status;
}

static
NTSTATUS
IopReadPropertyData(
    _In_ HANDLE OwnerKey,
    _In_ CONST DEVPROPKEY *PropertyKey,
    _In_ ULONG Size,
    _Out_writes_bytes_to_opt_(Size, *RequiredSize) PVOID Data,
    _Out_ PULONG RequiredSize,
    _Out_ PDEVPROPTYPE Type)
{
    PKEY_VALUE_FULL_INFORMATION Information;
    NTSTATUS Status;
    HANDLE PropertyHandle;

    PAGED_CODE();

    Status = IopOpenPropertyKey(OwnerKey,
                                PropertyKey,
                                FALSE,
                                KEY_READ,
                                &PropertyHandle);
    if (!NT_SUCCESS(Status))
    {
        return IopIsNotFoundStatus(Status) ? STATUS_OBJECT_NAME_NOT_FOUND
                                           : Status;
    }

    Status = IopGetRegistryValue(PropertyHandle, L"", &Information);
    ZwClose(PropertyHandle);

    if (!NT_SUCCESS(Status))
    {
        return IopIsNotFoundStatus(Status) ? STATUS_OBJECT_NAME_NOT_FOUND
                                           : Status;
    }

    if ((Information->Type & IOP_PROPERTY_TYPE_MARK) != IOP_PROPERTY_TYPE_MARK)
    {
        ExFreePoolWithTag(Information, TAG_IO);
        return STATUS_OBJECT_NAME_NOT_FOUND;
    }

    Status = IopReturnPropertyData(Information->Type & ~IOP_PROPERTY_TYPE_MARK,
                                   (PUCHAR)Information + Information->DataOffset,
                                   Information->DataLength,
                                   Size,
                                   Data,
                                   RequiredSize,
                                   Type);

    ExFreePoolWithTag(Information, TAG_IO);

    return Status;
}

static
NTSTATUS
IopWritePropertyData(
    _In_ HANDLE OwnerKey,
    _In_ CONST DEVPROPKEY *PropertyKey,
    _In_ DEVPROPTYPE Type,
    _In_ ULONG Size,
    _In_reads_bytes_opt_(Size) PVOID Data)
{
    UNICODE_STRING DefaultName = RTL_CONSTANT_STRING(L"");
    BOOLEAN Delete;
    NTSTATUS Status;
    ULONG Fixed;
    HANDLE PropertyHandle;

    PAGED_CODE();

    Delete = (Data == NULL) || (Size == 0) ||
             ((Type & DEVPROP_MASK_TYPE) == DEVPROP_TYPE_EMPTY);

    if (!Delete)
    {
        if (!IopIsPropertyTypeKnown(Type)) return STATUS_INVALID_PARAMETER;
        if (Size > IOP_PROPERTY_MAXIMUM_SIZE) return STATUS_INVALID_PARAMETER;

        Fixed = IopFixedPropertySize(Type);
        if (Fixed != 0 && Size != Fixed) return STATUS_INVALID_PARAMETER;

        if ((Type & DEVPROP_MASK_TYPE) == DEVPROP_TYPE_STRING ||
            (Type & DEVPROP_MASK_TYPE) == DEVPROP_TYPE_STRING_INDIRECT ||
            (Type & DEVPROP_MASK_TYPE) ==
                DEVPROP_TYPE_SECURITY_DESCRIPTOR_STRING)
        {
            if ((Size % sizeof(WCHAR)) != 0) return STATUS_INVALID_PARAMETER;

            if (((PWCHAR)Data)[Size / sizeof(WCHAR) - 1] != UNICODE_NULL)
            {
                return STATUS_INVALID_PARAMETER;
            }
        }

        if ((Type & DEVPROP_MASK_TYPE) == DEVPROP_TYPE_NULL)
        {
            return STATUS_INVALID_PARAMETER;
        }
    }
    else if (Data != NULL && Size != 0)
    {
        return STATUS_INVALID_PARAMETER;
    }

    Status = IopOpenPropertyKey(OwnerKey,
                                PropertyKey,
                                !Delete,
                                KEY_ALL_ACCESS,
                                &PropertyHandle);
    if (!NT_SUCCESS(Status))
    {
        if (!Delete) return Status;

        return IopIsNotFoundStatus(Status) ? STATUS_SUCCESS : Status;
    }

    if (Delete)
    {
        Status = ZwDeleteKey(PropertyHandle);
        ZwClose(PropertyHandle);

        return Status;
    }

    Status = ZwSetValueKey(PropertyHandle,
                           &DefaultName,
                           0,
                           IOP_PROPERTY_TYPE_MARK | Type,
                           Data,
                           Size);

    ZwClose(PropertyHandle);

    return Status;
}

/* SYSTEM PROPERTIES **********************************************************/

static
PCIOP_REGISTRY_PROPERTY
IopFindRegistryProperty(
    _In_ CONST DEVPROPKEY *PropertyKey)
{
    ULONG Index;

    for (Index = 0; Index < RTL_NUMBER_OF(IopRegistryProperties); Index++)
    {
        if (IopIsSamePropertyKey(IopRegistryProperties[Index].Key, PropertyKey))
        {
            return &IopRegistryProperties[Index];
        }
    }

    return NULL;
}

/* The key of the driver installed for a device, named by the Driver value of its instance key */
static
NTSTATUS
IopOpenInstalledDriverKey(
    _In_ HANDLE InstanceKey,
    _Out_ PHANDLE DriverKey)
{
    PKEY_VALUE_FULL_INFORMATION Information;
    UNICODE_STRING DriverName;
    NTSTATUS Status;
    HANDLE ClassKey;
    PWCHAR Name;
    ULONG Count;
    ULONG Index;

    PAGED_CODE();

    *DriverKey = NULL;

    Status = IopGetRegistryValue(InstanceKey, L"Driver", &Information);
    if (!NT_SUCCESS(Status)) return Status;

    if (Information->Type != REG_SZ)
    {
        ExFreePoolWithTag(Information, TAG_IO);
        return STATUS_OBJECT_NAME_NOT_FOUND;
    }

    Name = (PWCHAR)((PUCHAR)Information + Information->DataOffset);
    Count = Information->DataLength / sizeof(WCHAR);

    for (Index = 0; Index < Count; Index++)
    {
        if (Name[Index] == UNICODE_NULL) break;
    }

    if (Index == 0 || Index * sizeof(WCHAR) > MAXUSHORT)
    {
        ExFreePoolWithTag(Information, TAG_IO);
        return STATUS_OBJECT_NAME_NOT_FOUND;
    }

    DriverName.Buffer = Name;
    DriverName.Length = (USHORT)(Index * sizeof(WCHAR));
    DriverName.MaximumLength = DriverName.Length;

    Status = IopOpenRegistryKeyEx(&ClassKey,
                                  NULL,
                                  (PUNICODE_STRING)&IopControlClassKeyName,
                                  KEY_READ);
    if (NT_SUCCESS(Status))
    {
        Status = IopOpenRegistryKeyEx(DriverKey,
                                      ClassKey,
                                      &DriverName,
                                      KEY_READ);
        ZwClose(ClassKey);
    }

    ExFreePoolWithTag(Information, TAG_IO);

    return Status;
}

static
NTSTATUS
IopReadRegistryProperty(
    _In_ PDEVICE_OBJECT Pdo,
    _In_ PCIOP_REGISTRY_PROPERTY Property,
    _In_ ULONG Size,
    _Out_writes_bytes_to_opt_(Size, *RequiredSize) PVOID Data,
    _Out_ PULONG RequiredSize,
    _Out_ PDEVPROPTYPE Type)
{
    PKEY_VALUE_FULL_INFORMATION Information;
    UNICODE_STRING GuidString;
    NTSTATUS Status;
    HANDLE InstanceKey;
    HANDLE ValueKey;
    PUCHAR Bytes;
    ULONG Length;
    ULONG Count;
    ULONG Index;
    GUID Guid;

    PAGED_CODE();

    Status = PnpDeviceObjectToDeviceInstance(Pdo, &InstanceKey, KEY_READ);
    if (!NT_SUCCESS(Status)) return Status;

    ValueKey = InstanceKey;

    if (Property->DriverKey)
    {
        Status = IopOpenInstalledDriverKey(InstanceKey, &ValueKey);
        ZwClose(InstanceKey);

        if (!NT_SUCCESS(Status)) return STATUS_OBJECT_NAME_NOT_FOUND;
    }

    Status = IopGetRegistryValue(ValueKey,
                                 (PWSTR)Property->ValueName,
                                 &Information);
    ZwClose(ValueKey);

    if (!NT_SUCCESS(Status))
    {
        return IopIsNotFoundStatus(Status) ? STATUS_OBJECT_NAME_NOT_FOUND
                                           : Status;
    }

    Bytes = (PUCHAR)Information + Information->DataOffset;
    Length = Information->DataLength;
    Status = STATUS_OBJECT_NAME_NOT_FOUND;

    switch (Property->Type)
    {
        case DEVPROP_TYPE_STRING:
        case DEVPROP_TYPE_GUID:

            if (Information->Type != REG_SZ &&
                Information->Type != REG_EXPAND_SZ)
            {
                break;
            }

            Count = Length / sizeof(WCHAR);

            for (Index = 0; Index < Count; Index++)
            {
                if (((PWCHAR)Bytes)[Index] == UNICODE_NULL) break;
            }

            if (Index * sizeof(WCHAR) > MAXUSHORT - sizeof(WCHAR)) break;

            if (Property->Type == DEVPROP_TYPE_GUID)
            {
                GuidString.Buffer = (PWCHAR)Bytes;
                GuidString.Length = (USHORT)(Index * sizeof(WCHAR));
                GuidString.MaximumLength = GuidString.Length;

                if (!NT_SUCCESS(RtlGUIDFromString(&GuidString, &Guid))) break;

                Status = IopReturnPropertyData(DEVPROP_TYPE_GUID,
                                               &Guid,
                                               sizeof(Guid),
                                               Size,
                                               Data,
                                               RequiredSize,
                                               Type);
                break;
            }

            /* The string is given with one terminator, whatever the value holds past it */
            *Type = DEVPROP_TYPE_STRING;
            *RequiredSize = (ULONG)((Index + 1) * sizeof(WCHAR));

            if (*RequiredSize > Size)
            {
                Status = STATUS_BUFFER_TOO_SMALL;
                break;
            }

            if (Data == NULL)
            {
                Status = STATUS_INVALID_PARAMETER;
                break;
            }

            RtlCopyMemory(Data, Bytes, Index * sizeof(WCHAR));
            ((PWCHAR)Data)[Index] = UNICODE_NULL;
            Status = STATUS_SUCCESS;
            break;

        case DEVPROP_TYPE_STRING_LIST:

            if (Information->Type != REG_MULTI_SZ || Length == 0) break;

            Status = IopReturnPropertyData(DEVPROP_TYPE_STRING_LIST,
                                           Bytes,
                                           Length,
                                           Size,
                                           Data,
                                           RequiredSize,
                                           Type);
            break;

        case DEVPROP_TYPE_UINT32:

            if (Information->Type != REG_DWORD || Length < sizeof(ULONG)) break;

            Status = IopReturnPropertyData(DEVPROP_TYPE_UINT32,
                                           Bytes,
                                           sizeof(ULONG),
                                           Size,
                                           Data,
                                           RequiredSize,
                                           Type);
            break;

        case DEVPROP_TYPE_FILETIME:

            if (Information->Type != REG_BINARY || Length < sizeof(LARGE_INTEGER))
            {
                break;
            }

            Status = IopReturnPropertyData(DEVPROP_TYPE_FILETIME,
                                           Bytes,
                                           sizeof(LARGE_INTEGER),
                                           Size,
                                           Data,
                                           RequiredSize,
                                           Type);
            break;

        default:
            break;
    }

    ExFreePoolWithTag(Information, TAG_IO);

    return Status;
}

/* A property the registry holds as an instance value is written there, where the setup interfaces read it */
static
NTSTATUS
IopWriteRegistryProperty(
    _In_ HANDLE InstanceKey,
    _In_ PCIOP_REGISTRY_PROPERTY Property,
    _In_ DEVPROPTYPE Type,
    _In_ ULONG Size,
    _In_reads_bytes_opt_(Size) PVOID Data)
{
    UNICODE_STRING ValueName;
    NTSTATUS Status;
    ULONG ValueType;

    PAGED_CODE();

    RtlInitUnicodeString(&ValueName, Property->ValueName);

    if (Data == NULL || Size == 0 ||
        (Type & DEVPROP_MASK_TYPE) == DEVPROP_TYPE_EMPTY)
    {
        Status = ZwDeleteValueKey(InstanceKey, &ValueName);

        return IopIsNotFoundStatus(Status) ? STATUS_SUCCESS : Status;
    }

    if (Type != Property->Type) return STATUS_INVALID_PARAMETER;

    switch (Property->Type)
    {
        case DEVPROP_TYPE_STRING:

            if ((Size % sizeof(WCHAR)) != 0 ||
                ((PWCHAR)Data)[Size / sizeof(WCHAR) - 1] != UNICODE_NULL)
            {
                return STATUS_INVALID_PARAMETER;
            }

            ValueType = REG_SZ;
            break;

        case DEVPROP_TYPE_STRING_LIST:

            if ((Size % sizeof(WCHAR)) != 0 ||
                ((PWCHAR)Data)[Size / sizeof(WCHAR) - 1] != UNICODE_NULL)
            {
                return STATUS_INVALID_PARAMETER;
            }

            ValueType = REG_MULTI_SZ;
            break;

        case DEVPROP_TYPE_UINT32:

            if (Size != sizeof(ULONG)) return STATUS_INVALID_PARAMETER;

            ValueType = REG_DWORD;
            break;

        default:
            return STATUS_INVALID_PARAMETER;
    }

    return ZwSetValueKey(InstanceKey, &ValueName, 0, ValueType, Data, Size);
}

/* The drivers of a device stack by name, from the device on top down to the one the bus made */
static
NTSTATUS
IopReadStackProperty(
    _In_ PDEVICE_OBJECT Pdo,
    _In_ ULONG Size,
    _Out_writes_bytes_to_opt_(Size, *RequiredSize) PVOID Data,
    _Out_ PULONG RequiredSize,
    _Out_ PDEVPROPTYPE Type)
{
    PDEVICE_OBJECT Devices[IOP_PROPERTY_STACK_LIMIT];
    PDEVICE_OBJECT Current;
    PUNICODE_STRING Name;
    NTSTATUS Status;
    PWCHAR Cursor;
    ULONG Length = sizeof(WCHAR);
    ULONG Count = 0;
    ULONG Index;

    PAGED_CODE();

    Current = IoGetAttachedDeviceReference(Pdo);

    while (Current != NULL && Count < IOP_PROPERTY_STACK_LIMIT)
    {
        Devices[Count++] = Current;
        Current = IoGetLowerDeviceObject(Current);
    }

    if (Current != NULL) ObDereferenceObject(Current);

    for (Index = 0; Index < Count; Index++)
    {
        Length += Devices[Index]->DriverObject->DriverName.Length +
                  sizeof(WCHAR);
    }

    *Type = DEVPROP_TYPE_STRING_LIST;
    *RequiredSize = Length;

    if (Length > Size)
    {
        Status = STATUS_BUFFER_TOO_SMALL;
    }
    else if (Data == NULL)
    {
        Status = STATUS_INVALID_PARAMETER;
    }
    else
    {
        Cursor = Data;

        for (Index = 0; Index < Count; Index++)
        {
            Name = &Devices[Index]->DriverObject->DriverName;

            RtlCopyMemory(Cursor, Name->Buffer, Name->Length);
            Cursor += Name->Length / sizeof(WCHAR);
            *Cursor++ = UNICODE_NULL;
        }

        *Cursor = UNICODE_NULL;
        Status = STATUS_SUCCESS;
    }

    for (Index = 0; Index < Count; Index++)
    {
        ObDereferenceObject(Devices[Index]);
    }

    return Status;
}

static
NTSTATUS
IopReturnInstancePath(
    _In_ PDEVICE_NODE DeviceNode,
    _In_ ULONG Size,
    _Out_writes_bytes_to_opt_(Size, *RequiredSize) PVOID Data,
    _Out_ PULONG RequiredSize,
    _Out_ PDEVPROPTYPE Type)
{
    NTSTATUS Status;
    PWCHAR Text;
    ULONG Length;

    PAGED_CODE();

    if (DeviceNode->InstancePath.Length == 0)
    {
        return STATUS_OBJECT_NAME_NOT_FOUND;
    }

    Length = DeviceNode->InstancePath.Length + sizeof(WCHAR);

    Text = ExAllocatePoolZero(PagedPool, Length, TAG_IO);
    if (Text == NULL) return STATUS_INSUFFICIENT_RESOURCES;

    RtlCopyMemory(Text,
                  DeviceNode->InstancePath.Buffer,
                  DeviceNode->InstancePath.Length);

    Status = IopReturnPropertyData(DEVPROP_TYPE_STRING,
                                   Text,
                                   Length,
                                   Size,
                                   Data,
                                   RequiredSize,
                                   Type);

    ExFreePoolWithTag(Text, TAG_IO);

    return Status;
}

/* What the device node itself knows; Answered tells whether the key is one of those */
static
NTSTATUS
IopReadDeviceNodeProperty(
    _In_ PDEVICE_OBJECT Pdo,
    _In_ PDEVICE_NODE DeviceNode,
    _In_ CONST DEVPROPKEY *PropertyKey,
    _In_ ULONG Size,
    _Out_writes_bytes_to_opt_(Size, *RequiredSize) PVOID Data,
    _Out_ PULONG RequiredSize,
    _Out_ PDEVPROPTYPE Type,
    _Out_ PBOOLEAN Answered)
{
    PDEVICE_OBJECT ParentObject;
    PDEVICE_NODE Parent;
    DEVPROP_BOOLEAN Flag;
    NTSTATUS Status;
    ULONG DeviceStatus;
    ULONG Problem;
    ULONG Value;
    KIRQL OldIrql;

    PAGED_CODE();

    *Answered = TRUE;

    if (IopIsSamePropertyKey(PropertyKey, &DEVPKEY_Device_Parent))
    {
        KeAcquireSpinLock(&IopDeviceTreeLock, &OldIrql);
        Parent = DeviceNode->Parent;
        ParentObject = (Parent != NULL) ? Parent->PhysicalDeviceObject : NULL;
        if (ParentObject != NULL) ObReferenceObject(ParentObject);
        KeReleaseSpinLock(&IopDeviceTreeLock, OldIrql);

        if (ParentObject == NULL) return STATUS_OBJECT_NAME_NOT_FOUND;

        Status = IopReturnInstancePath(Parent, Size, Data, RequiredSize, Type);

        ObDereferenceObject(ParentObject);
        return Status;
    }

    if (IopIsSamePropertyKey(PropertyKey, &DEVPKEY_Device_InstanceId))
    {
        return IopReturnInstancePath(DeviceNode, Size, Data, RequiredSize, Type);
    }

    if (IopIsSamePropertyKey(PropertyKey, &DEVPKEY_Device_DevNodeStatus) ||
        IopIsSamePropertyKey(PropertyKey, &DEVPKEY_Device_ProblemCode) ||
        IopIsSamePropertyKey(PropertyKey, &DEVPKEY_Device_HasProblem))
    {
        PiQueryDevNodeStatus(DeviceNode, &DeviceStatus, &Problem);

        if (IopIsSamePropertyKey(PropertyKey, &DEVPKEY_Device_HasProblem))
        {
            Flag = (DeviceStatus & DN_HAS_PROBLEM) ? DEVPROP_TRUE
                                                   : DEVPROP_FALSE;

            return IopReturnPropertyData(DEVPROP_TYPE_BOOLEAN,
                                         &Flag,
                                         sizeof(Flag),
                                         Size,
                                         Data,
                                         RequiredSize,
                                         Type);
        }

        /* The flags a user-mode decision sets are left to the user-mode status query */
        Value = IopIsSamePropertyKey(PropertyKey, &DEVPKEY_Device_ProblemCode)
                    ? Problem
                    : (DeviceStatus & ~(DN_DISABLEABLE | DN_REMOVABLE |
                                        DN_WILL_BE_REMOVED | DN_NEED_RESTART |
                                        DN_NO_SHOW_IN_DM));

        return IopReturnPropertyData(DEVPROP_TYPE_UINT32,
                                     &Value,
                                     sizeof(Value),
                                     Size,
                                     Data,
                                     RequiredSize,
                                     Type);
    }

    if (IopIsSamePropertyKey(PropertyKey, &DEVPKEY_Device_IsPresent))
    {
        Flag = DEVPROP_TRUE;

        return IopReturnPropertyData(DEVPROP_TYPE_BOOLEAN,
                                     &Flag,
                                     sizeof(Flag),
                                     Size,
                                     Data,
                                     RequiredSize,
                                     Type);
    }

    if (IopIsSamePropertyKey(PropertyKey, &DEVPKEY_Device_Numa_Node))
    {
        Value = DeviceNode->NumaNodeIndex;

        return IopReturnPropertyData(DEVPROP_TYPE_UINT32,
                                     &Value,
                                     sizeof(Value),
                                     Size,
                                     Data,
                                     RequiredSize,
                                     Type);
    }

    if (IopIsSamePropertyKey(PropertyKey, &DEVPKEY_Device_Stack))
    {
        return IopReadStackProperty(Pdo, Size, Data, RequiredSize, Type);
    }

    /* TODO: Children, Siblings and the relation lists are not answered */
    *Answered = FALSE;

    return STATUS_OBJECT_NAME_NOT_FOUND;
}

/* The last answer for a key no store holds: what the older property call says about the device node */
static
NTSTATUS
IopReadLegacyProperty(
    _In_ PDEVICE_OBJECT Pdo,
    _In_ CONST DEVPROPKEY *PropertyKey,
    _In_ ULONG Size,
    _Out_writes_bytes_to_opt_(Size, *RequiredSize) PVOID Data,
    _Out_ PULONG RequiredSize,
    _Out_ PDEVPROPTYPE Type)
{
    NTSTATUS Status;
    ULONG Length = 0;
    ULONG Index;

    PAGED_CODE();

    for (Index = 0; Index < RTL_NUMBER_OF(IopLegacyProperties); Index++)
    {
        if (!IopIsSamePropertyKey(IopLegacyProperties[Index].Key, PropertyKey))
        {
            continue;
        }

        Status = IoGetDeviceProperty(Pdo,
                                     IopLegacyProperties[Index].Property,
                                     Size,
                                     Data,
                                     &Length);
        if (NT_SUCCESS(Status) || Status == STATUS_BUFFER_TOO_SMALL)
        {
            *Type = IopLegacyProperties[Index].Type;
            *RequiredSize = Length;

            return Status;
        }

        return STATUS_OBJECT_NAME_NOT_FOUND;
    }

    return STATUS_OBJECT_NAME_NOT_FOUND;
}

/*
 * An interface's registry name is its link up to the end of the class
 * identifier, with \??\ swapped for ##?#. What follows the identifier is the
 * reference string, which names a key below.
 */
NTSTATUS
IopInterfaceLinkToRegistryName(
    _In_ PUNICODE_STRING SymbolicLinkName,
    _Out_ LPGUID ClassGuid,
    _Out_ PUNICODE_STRING RegistryName,
    _Out_ PUNICODE_STRING ReferenceString)
{
    USHORT GuidChars = (USHORT)(sizeof(L"{00000000-0000-0000-0000-000000000000}") / sizeof(WCHAR) - 1);
    UNICODE_STRING GuidString;
    UNICODE_STRING Base;
    NTSTATUS Status;
    PWCHAR Buffer;
    USHORT Count;

    PAGED_CODE();

    RtlInitEmptyUnicodeString(RegistryName, NULL, 0);

    /* The class is the GUID that ends the link of the device and class, whatever braces the instance part holds */
    IopSplitInterfaceLink(SymbolicLinkName, &Base, ReferenceString);

    Count = Base.Length / sizeof(WCHAR);
    if (Count < 4 + 1 + GuidChars || Base.Buffer[Count - GuidChars - 1] != L'#') return STATUS_INVALID_PARAMETER;

    GuidString.Buffer = &Base.Buffer[Count - GuidChars];
    GuidString.Length = (USHORT)(GuidChars * sizeof(WCHAR));
    GuidString.MaximumLength = GuidString.Length;

    Status = RtlGUIDFromString(&GuidString, ClassGuid);
    if (!NT_SUCCESS(Status)) return Status;

    Buffer = ExAllocatePoolZero(PagedPool,
                                Base.Length + sizeof(WCHAR),
                                TAG_IO_NAME);
    if (Buffer == NULL) return STATUS_INSUFFICIENT_RESOURCES;

    RtlCopyMemory(Buffer, Base.Buffer, Base.Length);

    RegistryName->Buffer = Buffer;
    RegistryName->Length = Base.Length;
    RegistryName->MaximumLength = (USHORT)(Base.Length + sizeof(WCHAR));

    if (Buffer[0] == L'\\' && Buffer[1] == L'?' && Buffer[2] == L'?' &&
        Buffer[3] == L'\\')
    {
        Buffer[0] = L'#';
        Buffer[1] = L'#';
        Buffer[2] = L'?';
        Buffer[3] = L'#';
    }

    return STATUS_SUCCESS;
}

/* The key an interface's properties hang from: the one of its reference string, which only registering the interface makes */
static
NTSTATUS
IopOpenInterfaceOwnerKey(
    _In_ PUNICODE_STRING SymbolicLinkName,
    _In_ ACCESS_MASK DesiredAccess,
    _Out_ PHANDLE OwnerKey)
{
    UNICODE_STRING RegistryName;
    UNICODE_STRING Reference;
    UNICODE_STRING ReferenceKeyName;
    GUID ClassGuid;
    NTSTATUS Status;
    HANDLE ClassKey;
    HANDLE InterfaceKey;
    PWCHAR Buffer;

    PAGED_CODE();

    *OwnerKey = NULL;

    Status = IopInterfaceLinkToRegistryName(SymbolicLinkName,
                                            &ClassGuid,
                                            &RegistryName,
                                            &Reference);
    if (!NT_SUCCESS(Status)) return Status;

    Status = IopOpenInterfaceClassKey(&ClassGuid, FALSE, &ClassKey);
    if (!NT_SUCCESS(Status))
    {
        ExFreePoolWithTag(RegistryName.Buffer, TAG_IO_NAME);
        return Status;
    }

    Status = IopOpenRegistryKeyEx(&InterfaceKey,
                                  ClassKey,
                                  &RegistryName,
                                  KEY_READ);

    ZwClose(ClassKey);
    ExFreePoolWithTag(RegistryName.Buffer, TAG_IO_NAME);

    if (!NT_SUCCESS(Status)) return Status;

    Buffer = ExAllocatePoolZero(PagedPool,
                                Reference.Length + 2 * sizeof(WCHAR),
                                TAG_IO_NAME);
    if (Buffer == NULL)
    {
        ZwClose(InterfaceKey);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    Buffer[0] = L'#';
    if (Reference.Length != 0)
    {
        RtlCopyMemory(&Buffer[1], Reference.Buffer, Reference.Length);
    }

    ReferenceKeyName.Buffer = Buffer;
    ReferenceKeyName.Length = Reference.Length + sizeof(WCHAR);
    ReferenceKeyName.MaximumLength = Reference.Length + 2 * sizeof(WCHAR);

    Status = IopOpenRegistryKeyEx(OwnerKey,
                                  InterfaceKey,
                                  &ReferenceKeyName,
                                  DesiredAccess);

    ExFreePoolWithTag(Buffer, TAG_IO_NAME);
    ZwClose(InterfaceKey);

    return Status;
}

/* PUBLIC FUNCTIONS ***********************************************************/

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
    PCIOP_REGISTRY_PROPERTY RegistryProperty;
    PDEVICE_NODE DeviceNode;
    BOOLEAN Answered;
    NTSTATUS Status;
    HANDLE InstanceKey;

    PAGED_CODE();

    if (Pdo == NULL || RequiredSize == NULL || Type == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }

    Status = IopCheckPropertyArguments(PropertyKey, Lcid, Flags, FALSE);
    if (!NT_SUCCESS(Status)) return Status;

    if (Size != 0 && Data == NULL) return STATUS_INVALID_PARAMETER;

    DeviceNode = IopGetDeviceNode(Pdo);
    if (DeviceNode == NULL) return STATUS_INVALID_DEVICE_REQUEST;

    RegistryProperty = IopFindRegistryProperty(PropertyKey);
    if (RegistryProperty != NULL)
    {
        Status = IopReadRegistryProperty(Pdo,
                                         RegistryProperty,
                                         Size,
                                         Data,
                                         RequiredSize,
                                         Type);
        if (Status != STATUS_OBJECT_NAME_NOT_FOUND) return Status;
    }

    Status = IopReadDeviceNodeProperty(Pdo,
                                       DeviceNode,
                                       PropertyKey,
                                       Size,
                                       Data,
                                       RequiredSize,
                                       Type,
                                       &Answered);
    if (Answered) return Status;

    Status = PnpDeviceObjectToDeviceInstance(Pdo, &InstanceKey, KEY_READ);
    if (!NT_SUCCESS(Status)) return Status;

    Status = IopReadPropertyData(InstanceKey,
                                 PropertyKey,
                                 Size,
                                 Data,
                                 RequiredSize,
                                 Type);

    ZwClose(InstanceKey);

    if (Status != STATUS_OBJECT_NAME_NOT_FOUND) return Status;

    return IopReadLegacyProperty(Pdo,
                                 PropertyKey,
                                 Size,
                                 Data,
                                 RequiredSize,
                                 Type);
}

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
    PCIOP_REGISTRY_PROPERTY RegistryProperty;
    NTSTATUS Status;
    HANDLE InstanceKey;

    PAGED_CODE();

    if (Pdo == NULL) return STATUS_INVALID_PARAMETER;

    Status = IopCheckPropertyArguments(PropertyKey, Lcid, Flags, TRUE);
    if (!NT_SUCCESS(Status)) return Status;

    Status = PnpDeviceObjectToDeviceInstance(Pdo,
                                             &InstanceKey,
                                             KEY_ALL_ACCESS);
    if (!NT_SUCCESS(Status)) return Status;

    RegistryProperty = IopFindRegistryProperty(PropertyKey);
    if (RegistryProperty != NULL && RegistryProperty->Writable)
    {
        Status = IopWriteRegistryProperty(InstanceKey,
                                          RegistryProperty,
                                          Type,
                                          Size,
                                          Data);

        /* A copy an earlier system kept in the property store would only go stale */
        if (NT_SUCCESS(Status))
        {
            IopWritePropertyData(InstanceKey,
                                 PropertyKey,
                                 DEVPROP_TYPE_EMPTY,
                                 0,
                                 NULL);
        }
    }
    else
    {
        Status = IopWritePropertyData(InstanceKey,
                                      PropertyKey,
                                      Type,
                                      Size,
                                      Data);
    }

    ZwClose(InstanceKey);

    return Status;
}

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
    NTSTATUS Status;
    HANDLE OwnerKey;

    PAGED_CODE();

    if (SymbolicLinkName == NULL || RequiredSize == NULL || Type == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }

    Status = IopCheckPropertyArguments(PropertyKey, Lcid, Flags, FALSE);
    if (!NT_SUCCESS(Status)) return Status;

    if (Size != 0 && Data == NULL) return STATUS_INVALID_PARAMETER;

    /* A name that is no interface's, well formed or not, is an interface that is not there */
    Status = IopOpenInterfaceOwnerKey(SymbolicLinkName, KEY_READ, &OwnerKey);
    if (!NT_SUCCESS(Status))
    {
        return (Status == STATUS_INSUFFICIENT_RESOURCES)
                   ? Status
                   : STATUS_OBJECT_NAME_NOT_FOUND;
    }

    Status = IopReadPropertyData(OwnerKey,
                                 PropertyKey,
                                 Size,
                                 Data,
                                 RequiredSize,
                                 Type);

    ZwClose(OwnerKey);

    return Status;
}

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
    NTSTATUS Status;
    HANDLE OwnerKey;

    PAGED_CODE();

    if (SymbolicLinkName == NULL) return STATUS_INVALID_PARAMETER;

    Status = IopCheckPropertyArguments(PropertyKey, Lcid, Flags, TRUE);
    if (!NT_SUCCESS(Status)) return Status;

    Status = IopOpenInterfaceOwnerKey(SymbolicLinkName,
                                      KEY_ALL_ACCESS,
                                      &OwnerKey);
    if (!NT_SUCCESS(Status))
    {
        return (Status == STATUS_INSUFFICIENT_RESOURCES)
                   ? Status
                   : STATUS_OBJECT_NAME_NOT_FOUND;
    }

    Status = IopWritePropertyData(OwnerKey,
                                  PropertyKey,
                                  Type,
                                  Size,
                                  Data);

    ZwClose(OwnerKey);

    return Status;
}

