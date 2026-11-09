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
#define NDEBUG
#include <debug.h>

/* PRIVATE DATA ***************************************************************/

/* One registry value carries both the declared type and the caller's bytes */
typedef struct _IOP_PROPERTY_VALUE
{
    DEVPROPTYPE Type;
    UCHAR Data[ANYSIZE_ARRAY];
} IOP_PROPERTY_VALUE, *PIOP_PROPERTY_VALUE;

#define IOP_PROPERTY_HEADER_SIZE \
    ((ULONG)FIELD_OFFSET(IOP_PROPERTY_VALUE, Data))

#define IOP_PROPERTY_MAXIMUM_SIZE (64 * 1024)

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

/* The value name a property identifier is kept under: its eight hex digits */
static
NTSTATUS
IopPropertyValueName(
    _In_ DEVPROPID PropertyId,
    _Out_writes_(9) PWCHAR Buffer,
    _Out_ PUNICODE_STRING Name)
{
    static const WCHAR Digits[] = L"0123456789abcdef";
    ULONG Index;

    for (Index = 0; Index < 8; Index++)
    {
        Buffer[Index] = Digits[(PropertyId >> ((7 - Index) * 4)) & 0xF];
    }

    Buffer[8] = UNICODE_NULL;

    Name->Buffer = Buffer;
    Name->Length = 8 * sizeof(WCHAR);
    Name->MaximumLength = 9 * sizeof(WCHAR);

    return STATUS_SUCCESS;
}

/* The levels on the way down are made only when the caller is writing */
static
NTSTATUS
IopOpenPropertySetKey(
    _In_ HANDLE OwnerKey,
    _In_ CONST DEVPROPKEY *PropertyKey,
    _In_ BOOLEAN Create,
    _Out_ PHANDLE SetKey)
{
    UNICODE_STRING FormatName;
    NTSTATUS Status;
    HANDLE Properties;

    PAGED_CODE();

    *SetKey = NULL;

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

    if (Create)
    {
        Status = IopCreateRegistryKeyEx(SetKey,
                                        Properties,
                                        &FormatName,
                                        KEY_ALL_ACCESS,
                                        REG_OPTION_NON_VOLATILE,
                                        NULL);
    }
    else
    {
        Status = IopOpenRegistryKeyEx(SetKey,
                                      Properties,
                                      &FormatName,
                                      KEY_READ);
    }

    RtlFreeUnicodeString(&FormatName);
    ZwClose(Properties);

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
    PKEY_VALUE_PARTIAL_INFORMATION Information;
    PIOP_PROPERTY_VALUE Value;
    UNICODE_STRING ValueName;
    WCHAR NameBuffer[9];
    NTSTATUS Status;
    ULONG Available;
    ULONG Length = 0;
    HANDLE SetKey;

    PAGED_CODE();

    *RequiredSize = 0;
    *Type = DEVPROP_TYPE_EMPTY;

    Status = IopOpenPropertySetKey(OwnerKey, PropertyKey, FALSE, &SetKey);
    if (!NT_SUCCESS(Status))
    {
        return (Status == STATUS_OBJECT_NAME_NOT_FOUND ||
                Status == STATUS_OBJECT_PATH_NOT_FOUND)
                   ? STATUS_OBJECT_NAME_NOT_FOUND
                   : Status;
    }

    IopPropertyValueName(PropertyKey->pid, NameBuffer, &ValueName);

    Status = ZwQueryValueKey(SetKey,
                             &ValueName,
                             KeyValuePartialInformation,
                             NULL,
                             0,
                             &Length);
    if (Status != STATUS_BUFFER_TOO_SMALL && Status != STATUS_BUFFER_OVERFLOW)
    {
        ZwClose(SetKey);

        if (NT_SUCCESS(Status)) return STATUS_OBJECT_NAME_NOT_FOUND;

        return (Status == STATUS_OBJECT_NAME_NOT_FOUND)
                   ? STATUS_OBJECT_NAME_NOT_FOUND
                   : Status;
    }

    Information = ExAllocatePoolZero(PagedPool, Length, TAG_IO);
    if (Information == NULL)
    {
        ZwClose(SetKey);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    Status = ZwQueryValueKey(SetKey,
                             &ValueName,
                             KeyValuePartialInformation,
                             Information,
                             Length,
                             &Length);
    ZwClose(SetKey);

    if (!NT_SUCCESS(Status))
    {
        ExFreePoolWithTag(Information, TAG_IO);
        return Status;
    }

    if (Information->Type != REG_BINARY ||
        Information->DataLength < IOP_PROPERTY_HEADER_SIZE)
    {
        ExFreePoolWithTag(Information, TAG_IO);
        return STATUS_OBJECT_NAME_NOT_FOUND;
    }

    Value = (PIOP_PROPERTY_VALUE)Information->Data;
    Available = Information->DataLength - IOP_PROPERTY_HEADER_SIZE;

    *Type = Value->Type;
    *RequiredSize = Available;

    if (Available > Size)
    {
        ExFreePoolWithTag(Information, TAG_IO);
        return STATUS_BUFFER_TOO_SMALL;
    }

    if (Available != 0)
    {
        if (Data == NULL)
        {
            ExFreePoolWithTag(Information, TAG_IO);
            return STATUS_INVALID_PARAMETER;
        }

        RtlCopyMemory(Data, Value->Data, Available);
    }

    ExFreePoolWithTag(Information, TAG_IO);

    return STATUS_SUCCESS;
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
    PIOP_PROPERTY_VALUE Value;
    UNICODE_STRING ValueName;
    WCHAR NameBuffer[9];
    BOOLEAN Delete;
    NTSTATUS Status;
    ULONG Fixed;
    ULONG Total;
    HANDLE SetKey;

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

    Status = IopOpenPropertySetKey(OwnerKey, PropertyKey, !Delete, &SetKey);
    if (!NT_SUCCESS(Status))
    {
        if (!Delete) return Status;

        return (Status == STATUS_OBJECT_NAME_NOT_FOUND ||
                Status == STATUS_OBJECT_PATH_NOT_FOUND)
                   ? STATUS_SUCCESS
                   : Status;
    }

    IopPropertyValueName(PropertyKey->pid, NameBuffer, &ValueName);

    if (Delete)
    {
        Status = ZwDeleteValueKey(SetKey, &ValueName);
        ZwClose(SetKey);

        return (Status == STATUS_OBJECT_NAME_NOT_FOUND) ? STATUS_SUCCESS
                                                        : Status;
    }

    Total = IOP_PROPERTY_HEADER_SIZE + Size;

    Value = ExAllocatePoolZero(PagedPool, Total, TAG_IO);
    if (Value == NULL)
    {
        ZwClose(SetKey);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    Value->Type = Type;
    RtlCopyMemory(Value->Data, Data, Size);

    Status = ZwSetValueKey(SetKey,
                           &ValueName,
                           0,
                           REG_BINARY,
                           Value,
                           Total);

    ExFreePoolWithTag(Value, TAG_IO);
    ZwClose(SetKey);

    return Status;
}

/* An interface's registry name is its link with \??\ swapped for ##?# */
NTSTATUS
IopInterfaceLinkToRegistryName(
    _In_ PUNICODE_STRING SymbolicLinkName,
    _Out_ LPGUID ClassGuid,
    _Out_ PUNICODE_STRING RegistryName)
{
    UNICODE_STRING GuidString;
    NTSTATUS Status;
    PWCHAR Buffer;
    USHORT Index;
    USHORT Open = 0;
    BOOLEAN Found = FALSE;

    PAGED_CODE();

    RtlInitEmptyUnicodeString(RegistryName, NULL, 0);

    if (SymbolicLinkName->Length < 4 * sizeof(WCHAR))
    {
        return STATUS_INVALID_PARAMETER;
    }

    for (Index = 0; Index < SymbolicLinkName->Length / sizeof(WCHAR); Index++)
    {
        if (SymbolicLinkName->Buffer[Index] == L'{')
        {
            Open = Index;
            Found = TRUE;
        }
    }

    if (!Found) return STATUS_INVALID_PARAMETER;

    GuidString.Buffer = SymbolicLinkName->Buffer + Open;
    GuidString.Length = 0;
    GuidString.MaximumLength = 0;

    for (Index = Open; Index < SymbolicLinkName->Length / sizeof(WCHAR);
         Index++)
    {
        GuidString.Length += sizeof(WCHAR);

        if (SymbolicLinkName->Buffer[Index] == L'}') break;
    }

    GuidString.MaximumLength = GuidString.Length;

    Status = RtlGUIDFromString(&GuidString, ClassGuid);
    if (!NT_SUCCESS(Status)) return Status;

    Buffer = ExAllocatePoolZero(PagedPool,
                                SymbolicLinkName->Length + sizeof(WCHAR),
                                TAG_IO_NAME);
    if (Buffer == NULL) return STATUS_INSUFFICIENT_RESOURCES;

    RtlCopyMemory(Buffer,
                  SymbolicLinkName->Buffer,
                  SymbolicLinkName->Length);

    RegistryName->Buffer = Buffer;
    RegistryName->Length = SymbolicLinkName->Length;
    RegistryName->MaximumLength = SymbolicLinkName->Length + sizeof(WCHAR);

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

static
NTSTATUS
IopOpenInterfaceOwnerKey(
    _In_ PUNICODE_STRING SymbolicLinkName,
    _In_ BOOLEAN Create,
    _Out_ PHANDLE OwnerKey)
{
    UNICODE_STRING RegistryName;
    GUID ClassGuid;
    NTSTATUS Status;
    HANDLE ClassKey;

    PAGED_CODE();

    *OwnerKey = NULL;

    Status = IopInterfaceLinkToRegistryName(SymbolicLinkName,
                                            &ClassGuid,
                                            &RegistryName);
    if (!NT_SUCCESS(Status)) return Status;

    Status = IopOpenInterfaceClassKey(&ClassGuid, Create, &ClassKey);
    if (!NT_SUCCESS(Status))
    {
        ExFreePoolWithTag(RegistryName.Buffer, TAG_IO_NAME);
        return Status;
    }

    if (Create)
    {
        Status = IopCreateRegistryKeyEx(OwnerKey,
                                        ClassKey,
                                        &RegistryName,
                                        KEY_ALL_ACCESS,
                                        REG_OPTION_NON_VOLATILE,
                                        NULL);
    }
    else
    {
        Status = IopOpenRegistryKeyEx(OwnerKey,
                                      ClassKey,
                                      &RegistryName,
                                      KEY_READ);
    }

    ZwClose(ClassKey);
    ExFreePoolWithTag(RegistryName.Buffer, TAG_IO_NAME);

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
    NTSTATUS Status;
    HANDLE InstanceKey;

    PAGED_CODE();

    if (Pdo == NULL || RequiredSize == NULL || Type == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }

    *RequiredSize = 0;
    *Type = DEVPROP_TYPE_EMPTY;

    Status = IopCheckPropertyArguments(PropertyKey, Lcid, Flags, FALSE);
    if (!NT_SUCCESS(Status)) return Status;

    if (Size != 0 && Data == NULL) return STATUS_INVALID_PARAMETER;

    Status = PnpDeviceObjectToDeviceInstance(Pdo, &InstanceKey, KEY_READ);
    if (!NT_SUCCESS(Status)) return Status;

    Status = IopReadPropertyData(InstanceKey,
                                 PropertyKey,
                                 Size,
                                 Data,
                                 RequiredSize,
                                 Type);

    ZwClose(InstanceKey);

    return Status;
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

    Status = IopWritePropertyData(InstanceKey,
                                  PropertyKey,
                                  Type,
                                  Size,
                                  Data);

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

    *RequiredSize = 0;
    *Type = DEVPROP_TYPE_EMPTY;

    Status = IopCheckPropertyArguments(PropertyKey, Lcid, Flags, FALSE);
    if (!NT_SUCCESS(Status)) return Status;

    if (Size != 0 && Data == NULL) return STATUS_INVALID_PARAMETER;

    Status = IopOpenInterfaceOwnerKey(SymbolicLinkName, FALSE, &OwnerKey);
    if (!NT_SUCCESS(Status)) return Status;

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

    Status = IopOpenInterfaceOwnerKey(SymbolicLinkName, TRUE, &OwnerKey);
    if (!NT_SUCCESS(Status)) return Status;

    Status = IopWritePropertyData(OwnerKey,
                                  PropertyKey,
                                  Type,
                                  Size,
                                  Data);

    ZwClose(OwnerKey);

    return Status;
}
