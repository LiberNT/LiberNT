/*
 * PROJECT:     LiberNT
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Microsoft OS 2.0 descriptors and the private hub query for the descriptor set
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

#define MS_OS_20_DESCRIPTOR_INDEX 0x07
#define MS_OS_20_SET_ALT_ENUMERATION 0x08

#define MS_OS_20_SET_HEADER_DESCRIPTOR 0x00
#define MS_OS_20_SUBSET_HEADER_CONFIGURATION 0x01
#define MS_OS_20_SUBSET_HEADER_FUNCTION 0x02
#define MS_OS_20_FEATURE_COMPATBLE_ID 0x03
#define MS_OS_20_FEATURE_REG_PROPERTY 0x04
#define MS_OS_20_FEATURE_MIN_RESUME_TIME 0x05
#define MS_OS_20_FEATURE_MODEL_ID 0x06
#define MS_OS_20_FEATURE_CCGP_DEVICE 0x07
#define MS_OS_20_FEATURE_VENDOR_REVISION 0x08

#define MS_OS_20_REG_SZ 1
#define MS_OS_20_REG_EXPAND_SZ 2
#define MS_OS_20_REG_BINARY 3
#define MS_OS_20_REG_DWORD_LITTLE_ENDIAN 4
#define MS_OS_20_REG_DWORD_BIG_ENDIAN 5
#define MS_OS_20_REG_LINK 6
#define MS_OS_20_REG_MULTI_SZ 7

#pragma pack(push, 1)

typedef struct _MS_OS_20_DESCRIPTOR_SET_INFORMATION
{
    ULONG dwWindowsVersion;
    USHORT wMSOSDescriptorSetTotalLength;
    UCHAR bMS_VendorCode;
    UCHAR bAltEnumCode;
} MS_OS_20_DESCRIPTOR_SET_INFORMATION, *PMS_OS_20_DESCRIPTOR_SET_INFORMATION;

typedef struct _MS_OS_20_DESCRIPTOR_HEADER
{
    USHORT wLength;
    USHORT wDescriptorType;
} MS_OS_20_DESCRIPTOR_HEADER, *PMS_OS_20_DESCRIPTOR_HEADER;

typedef struct _MS_OS_20_SET_HEADER
{
    USHORT wLength;
    USHORT wDescriptorType;
    ULONG dwWindowsVersion;
    USHORT wTotalLength;
} MS_OS_20_SET_HEADER, *PMS_OS_20_SET_HEADER;

typedef struct _MS_OS_20_CONFIGURATION_SUBSET_HEADER
{
    USHORT wLength;
    USHORT wDescriptorType;
    UCHAR bConfigurationValue;
    UCHAR bReserved;
    USHORT wTotalLength;
} MS_OS_20_CONFIGURATION_SUBSET_HEADER, *PMS_OS_20_CONFIGURATION_SUBSET_HEADER;

typedef struct _MS_OS_20_FUNCTION_SUBSET_HEADER
{
    USHORT wLength;
    USHORT wDescriptorType;
    UCHAR bFirstInterface;
    UCHAR bReserved;
    USHORT wSubsetLength;
} MS_OS_20_FUNCTION_SUBSET_HEADER, *PMS_OS_20_FUNCTION_SUBSET_HEADER;

typedef struct _MS_OS_20_COMPATIBLE_ID_DESCRIPTOR
{
    USHORT wLength;
    USHORT wDescriptorType;
    CHAR CompatibleID[8];
    CHAR SubCompatibleID[8];
} MS_OS_20_COMPATIBLE_ID_DESCRIPTOR, *PMS_OS_20_COMPATIBLE_ID_DESCRIPTOR;

typedef struct _MS_OS_20_REGISTRY_PROPERTY_DESCRIPTOR
{
    USHORT wLength;
    USHORT wDescriptorType;
    USHORT wPropertyDataType;
    USHORT wPropertyNameLength;
} MS_OS_20_REGISTRY_PROPERTY_DESCRIPTOR, *PMS_OS_20_REGISTRY_PROPERTY_DESCRIPTOR;

#pragma pack(pop)

C_ASSERT(sizeof(MS_OS_20_DESCRIPTOR_SET_INFORMATION) == 8);
C_ASSERT(sizeof(MS_OS_20_SET_HEADER) == 10);
C_ASSERT(sizeof(MS_OS_20_CONFIGURATION_SUBSET_HEADER) == 8);
C_ASSERT(sizeof(MS_OS_20_FUNCTION_SUBSET_HEADER) == 8);
C_ASSERT(sizeof(MS_OS_20_COMPATIBLE_ID_DESCRIPTOR) == 20);

#define IOCTL_INTERNAL_REACTOS_USB_GET_MSOS20_DESCRIPTOR_SET CTL_CODE(FILE_DEVICE_UNKNOWN, 0x903, METHOD_NEITHER, FILE_ANY_ACCESS)

typedef struct _ROS_USB_MSOS20_DESCRIPTOR_SET_QUERY
{
    ULONG Size;
    ULONG BufferLength;
    PVOID Buffer;
    ULONG ResultLength;
} ROS_USB_MSOS20_DESCRIPTOR_SET_QUERY, *PROS_USB_MSOS20_DESCRIPTOR_SET_QUERY;

typedef struct _ROS_USB_MSOS20_WALK
{
    const UCHAR *Set;
    ULONG Length;
    ULONG Offset;
    ULONG ConfigurationEnd;
    ULONG FunctionEnd;
    BOOLEAN ConfigurationSelected;
    BOOLEAN Malformed;
    LONG FirstInterface;
} ROS_USB_MSOS20_WALK, *PROS_USB_MSOS20_WALK;

FORCEINLINE
BOOLEAN
RosUsbMsOs20BeginWalk(
    _Out_ PROS_USB_MSOS20_WALK Walk,
    _In_reads_bytes_(Length) const UCHAR *Set,
    _In_ ULONG Length)
{
    const MS_OS_20_SET_HEADER *Header = (const MS_OS_20_SET_HEADER *)Set;

    RtlZeroMemory(Walk, sizeof(*Walk));
    Walk->Set = Set;
    Walk->FirstInterface = -1;
    Walk->ConfigurationSelected = TRUE;
    if (Length < sizeof(*Header) || Header->wLength != sizeof(*Header) ||
        Header->wDescriptorType != MS_OS_20_SET_HEADER_DESCRIPTOR || Header->wTotalLength > Length ||
        Header->wTotalLength < sizeof(*Header))
    {
        Walk->Malformed = TRUE;
        return FALSE;
    }
    Walk->Length = Header->wTotalLength;
    Walk->Offset = sizeof(*Header);
    Walk->ConfigurationEnd = Walk->Length;
    Walk->FunctionEnd = Walk->Length;
    return TRUE;
}

FORCEINLINE
const MS_OS_20_DESCRIPTOR_HEADER *
RosUsbMsOs20NextFeature(
    _Inout_ PROS_USB_MSOS20_WALK Walk)
{
    const MS_OS_20_DESCRIPTOR_HEADER *Descriptor;

    while (!Walk->Malformed && Walk->Offset + sizeof(MS_OS_20_DESCRIPTOR_HEADER) <= Walk->Length)
    {
        if (Walk->Offset >= Walk->FunctionEnd)
        {
            Walk->FunctionEnd = Walk->ConfigurationEnd;
            Walk->FirstInterface = -1;
        }
        if (Walk->Offset >= Walk->ConfigurationEnd)
        {
            Walk->ConfigurationEnd = Walk->Length;
            Walk->FunctionEnd = Walk->Length;
            Walk->ConfigurationSelected = TRUE;
        }

        Descriptor = (const MS_OS_20_DESCRIPTOR_HEADER *)(Walk->Set + Walk->Offset);
        if (Descriptor->wLength < sizeof(*Descriptor) || Walk->Offset + Descriptor->wLength > Walk->Length)
        {
            Walk->Malformed = TRUE;
            break;
        }

        if (Descriptor->wDescriptorType == MS_OS_20_SUBSET_HEADER_CONFIGURATION)
        {
            const MS_OS_20_CONFIGURATION_SUBSET_HEADER *Subset = (const MS_OS_20_CONFIGURATION_SUBSET_HEADER *)Descriptor;

            if (Descriptor->wLength != sizeof(*Subset) || Subset->wTotalLength < sizeof(*Subset) ||
                Walk->Offset + Subset->wTotalLength > Walk->Length || Walk->ConfigurationEnd != Walk->Length)
            {
                Walk->Malformed = TRUE;
                break;
            }
            Walk->ConfigurationEnd = Walk->Offset + Subset->wTotalLength;
            Walk->FunctionEnd = Walk->ConfigurationEnd;
            Walk->ConfigurationSelected = Subset->bConfigurationValue == 0;
            Walk->Offset += sizeof(*Subset);
            continue;
        }

        if (Descriptor->wDescriptorType == MS_OS_20_SUBSET_HEADER_FUNCTION)
        {
            const MS_OS_20_FUNCTION_SUBSET_HEADER *Subset = (const MS_OS_20_FUNCTION_SUBSET_HEADER *)Descriptor;

            if (Descriptor->wLength != sizeof(*Subset) || Subset->wSubsetLength < sizeof(*Subset) ||
                Walk->Offset + Subset->wSubsetLength > Walk->ConfigurationEnd || Walk->FirstInterface != -1)
            {
                Walk->Malformed = TRUE;
                break;
            }
            Walk->FunctionEnd = Walk->Offset + Subset->wSubsetLength;
            Walk->FirstInterface = Subset->bFirstInterface;
            Walk->Offset += sizeof(*Subset);
            continue;
        }

        if (Descriptor->wDescriptorType == MS_OS_20_SET_HEADER_DESCRIPTOR)
        {
            Walk->Malformed = TRUE;
            break;
        }

        Walk->Offset += Descriptor->wLength;
        if (Walk->ConfigurationSelected)
            return Descriptor;
    }
    return NULL;
}

FORCEINLINE
NTSTATUS
RosUsbMsOs20ApplyRegistryProperty(
    _In_ HANDLE Key,
    _In_ const MS_OS_20_DESCRIPTOR_HEADER *Descriptor)
{
    const MS_OS_20_REGISTRY_PROPERTY_DESCRIPTOR *Property = (const MS_OS_20_REGISTRY_PROPERTY_DESCRIPTOR *)Descriptor;
    static const ULONG Types[] = { 0, REG_SZ, REG_EXPAND_SZ, REG_BINARY, REG_DWORD, REG_DWORD_BIG_ENDIAN, REG_LINK,
                                   REG_MULTI_SZ };
    WCHAR Name[256];
    UNICODE_STRING ValueName;
    const UCHAR *Data;
    USHORT NameLength, DataLength;

    ASSERT(KeGetCurrentIrql() == PASSIVE_LEVEL);
    ASSERT(Descriptor->wDescriptorType == MS_OS_20_FEATURE_REG_PROPERTY);

    if (Descriptor->wLength < sizeof(*Property) + sizeof(USHORT))
        return STATUS_INVALID_PARAMETER;
    NameLength = Property->wPropertyNameLength;
    if (Property->wPropertyDataType == 0 || Property->wPropertyDataType >= RTL_NUMBER_OF(Types) ||
        NameLength < 2 * sizeof(WCHAR) || (NameLength & 1) || NameLength > sizeof(Name) ||
        sizeof(*Property) + NameLength + sizeof(USHORT) > Descriptor->wLength)
    {
        return STATUS_INVALID_PARAMETER;
    }
    RtlCopyMemory(&DataLength, (const UCHAR *)Property + sizeof(*Property) + NameLength, sizeof(DataLength));
    if (sizeof(*Property) + NameLength + sizeof(USHORT) + DataLength != Descriptor->wLength)
        return STATUS_INVALID_PARAMETER;
    RtlCopyMemory(Name, Property + 1, NameLength);
    if (Name[NameLength / sizeof(WCHAR) - 1] != UNICODE_NULL)
        return STATUS_INVALID_PARAMETER;
    RtlInitUnicodeString(&ValueName, Name);
    if (!ValueName.Length)
        return STATUS_INVALID_PARAMETER;
    Data = (const UCHAR *)Property + sizeof(*Property) + NameLength + sizeof(USHORT);
    return ZwSetValueKey(Key, &ValueName, 0, Types[Property->wPropertyDataType], (PVOID)Data, DataLength);
}
