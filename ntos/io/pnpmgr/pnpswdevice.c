/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Software devices the PnP manager enumerates for the components a device's driver installed
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#include "../ioint.h"
#define NDEBUG
#include <debug.h>

/* PRIVATE DATA ***************************************************************/

typedef struct _PNP_SW_DEVICE
{
    LIST_ENTRY ListEntry;
    PDEVICE_OBJECT Parent;
    PDEVICE_OBJECT PhysicalDeviceObject;
    UNICODE_STRING Name;
    UNICODE_STRING InstanceId;
    ULONG Capabilities;
    BOOLEAN Created;
    BOOLEAN Listed;
    BOOLEAN Reported;
    BOOLEAN Handed;
    BOOLEAN Retired;
} PNP_SW_DEVICE, *PPNP_SW_DEVICE;

typedef struct _PNP_SW_PDO_EXTENSION
{
    PPNP_SW_DEVICE Device;
} PNP_SW_PDO_EXTENSION, *PPNP_SW_PDO_EXTENSION;

static PDRIVER_OBJECT PnpSwDriverObject = NULL;
static LIST_ENTRY PnpSwDeviceList;
static ERESOURCE PnpSwDeviceLock;

static const UNICODE_STRING PnpSwDriverName =
    RTL_CONSTANT_STRING(L"\\Driver\\SoftwareDevice");

static const UNICODE_STRING PnpSwDevicesKeyName =
    RTL_CONSTANT_STRING(L"Devices");

static const UNICODE_STRING PnpSwCreatedKeyName =
    RTL_CONSTANT_STRING(ENUM_ROOT L"\\SWD");

/* Windows 11 enumerates a driver's component as SWD\DRIVERENUM\<component>&<prefix the parent gives its children> */
static const WCHAR PnpSwDeviceId[] = L"SWD\\DRIVERENUM";
static const WCHAR PnpSwCompatibleIds[] = L"SWD\\GenericRaw\0SWD\\Generic\0";
static const WCHAR PnpSwNoIds[] = L"";
static const WCHAR PnpSwContainerId[] = L"{00000000-0000-0000-ffff-ffffffffffff}";

#define PNP_SW_MAX_DEVICE_ID_CHARS 200

#define CM_DEVCAP_REMOVABLE         0x00000004
#define CM_DEVCAP_UNIQUEID          0x00000010
#define CM_DEVCAP_SILENTINSTALL     0x00000020
#define CM_DEVCAP_RAWDEVICEOK       0x00000040
#define CM_DEVCAP_SURPRISEREMOVALOK 0x00000080

/* PRIVATE FUNCTIONS **********************************************************/

#if DBG
static
BOOLEAN
PnpSwIsLockHeld(VOID)
{
    return ExIsResourceAcquiredExclusiveLite(&PnpSwDeviceLock) ||
           ExIsResourceAcquiredSharedLite(&PnpSwDeviceLock) != 0;
}
#endif

static
VOID
PnpSwFreeRecord(
    _In_ _Post_invalid_ PPNP_SW_DEVICE Device)
{
    PAGED_CODE();

    ASSERT(Device->PhysicalDeviceObject == NULL);

    if (Device->Name.Buffer != NULL) ExFreePoolWithTag(Device->Name.Buffer, TAG_PNP_SWDEVICE);
    if (Device->InstanceId.Buffer != NULL) ExFreePoolWithTag(Device->InstanceId.Buffer, TAG_PNP_SWDEVICE);
    if (Device->Parent != NULL) ObDereferenceObject(Device->Parent);

    ExFreePoolWithTag(Device, TAG_PNP_SWDEVICE);
}

/* An instance identifier holds printable characters other than a backslash or a comma (device instance ID documentation) */
static
BOOLEAN
PnpSwIsValidName(
    _In_ PCUNICODE_STRING Name)
{
    ULONG Index;

    if (Name->Length == 0) return FALSE;

    for (Index = 0; Index < Name->Length / sizeof(WCHAR); Index++)
    {
        if (Name->Buffer[Index] <= L' ' || Name->Buffer[Index] > 0x7F ||
            Name->Buffer[Index] == L'\\' || Name->Buffer[Index] == L',')
        {
            return FALSE;
        }
    }

    return TRUE;
}

static
NTSTATUS
PnpSwOpenComponentKey(
    _In_ PPNP_SW_DEVICE Device,
    _Out_ PHANDLE ComponentKey)
{
    HANDLE InstanceKey, DevicesKey;
    NTSTATUS Status;

    PAGED_CODE();

    ASSERT(PnpSwIsLockHeld());

    *ComponentKey = NULL;

    if (Device->Created)
    {
        Status = IopOpenRegistryKeyEx(&DevicesKey, NULL, (PUNICODE_STRING)&PnpSwCreatedKeyName, KEY_READ);
        if (!NT_SUCCESS(Status)) return Status;

        Status = IopOpenRegistryKeyEx(ComponentKey, DevicesKey, &Device->Name, KEY_READ);
        ZwClose(DevicesKey);
        return Status;
    }

    ASSERT(Device->Parent != NULL);

    Status = PnpDeviceObjectToDeviceInstance(Device->Parent, &InstanceKey, KEY_READ);
    if (!NT_SUCCESS(Status)) return Status;

    Status = IopOpenRegistryKeyEx(&DevicesKey, InstanceKey, (PUNICODE_STRING)&PnpSwDevicesKeyName, KEY_READ);
    ZwClose(InstanceKey);
    if (!NT_SUCCESS(Status)) return Status;

    Status = IopOpenRegistryKeyEx(ComponentKey, DevicesKey, &Device->Name, KEY_READ);
    ZwClose(DevicesKey);

    return Status;
}

static
NTSTATUS
PnpSwQueryValue(
    _In_ PPNP_SW_DEVICE Device,
    _In_ PWSTR ValueName,
    _In_ ULONG Type,
    _Out_ PKEY_VALUE_FULL_INFORMATION *Information)
{
    HANDLE ComponentKey;
    NTSTATUS Status;

    PAGED_CODE();

    *Information = NULL;

    Status = PnpSwOpenComponentKey(Device, &ComponentKey);
    if (!NT_SUCCESS(Status)) return Status;

    Status = IopGetRegistryValue(ComponentKey, ValueName, Information);
    ZwClose(ComponentKey);
    if (!NT_SUCCESS(Status)) return Status;

    if ((*Information)->Type != Type)
    {
        ExFreePool(*Information);
        *Information = NULL;
        return STATUS_OBJECT_TYPE_MISMATCH;
    }

    return STATUS_SUCCESS;
}

/* The identifiers recorded for the component, followed by the ones every such device carries */
static
NTSTATUS
PnpSwQueryIds(
    _In_ PPNP_SW_DEVICE Device,
    _In_ PWSTR ValueName,
    _In_reads_bytes_(ExtraSize) PCWSTR Extra,
    _In_ ULONG ExtraSize,
    _Out_ PWSTR *Ids)
{
    PKEY_VALUE_FULL_INFORMATION Information;
    ULONG DataChars = 0;
    ULONG Position = 0;
    PWCHAR Buffer;
    NTSTATUS Status;

    PAGED_CODE();

    ASSERT(ExtraSize >= sizeof(WCHAR));
    ASSERT((ExtraSize % sizeof(WCHAR)) == 0);
    ASSERT(Extra[ExtraSize / sizeof(WCHAR) - 1] == UNICODE_NULL);

    *Ids = NULL;

    Status = PnpSwQueryValue(Device, ValueName, REG_MULTI_SZ, &Information);
    if (!NT_SUCCESS(Status))
    {
        if (ExtraSize == sizeof(WCHAR)) return STATUS_NOT_SUPPORTED;
        Information = NULL;
    }

    if (Information != NULL) DataChars = Information->DataLength / sizeof(WCHAR);

    Buffer = ExAllocatePoolZero(PagedPool, (DataChars + 2) * sizeof(WCHAR) + ExtraSize, TAG_PNP_SWDEVICE);
    if (Buffer == NULL)
    {
        if (Information != NULL) ExFreePool(Information);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    if (Information != NULL)
    {
        RtlCopyMemory(Buffer, (PUCHAR)Information + Information->DataOffset, DataChars * sizeof(WCHAR));
        ExFreePool(Information);
    }

    /* The buffer ends with two zeroes past the recorded data, so the walk stops inside it */
    while (Buffer[Position] != UNICODE_NULL)
    {
        Position += (ULONG)wcslen(&Buffer[Position]) + 1;
        ASSERT(Position <= DataChars + 1);
    }

    RtlCopyMemory(&Buffer[Position], Extra, ExtraSize);

    if (Buffer[0] == UNICODE_NULL)
    {
        ExFreePoolWithTag(Buffer, TAG_PNP_SWDEVICE);
        return STATUS_NOT_SUPPORTED;
    }

    *Ids = Buffer;
    return STATUS_SUCCESS;
}

static
NTSTATUS
PnpSwCopyString(
    _In_ PCWSTR Source,
    _Out_ PWSTR *Copy)
{
    SIZE_T Size = (wcslen(Source) + 1) * sizeof(WCHAR);
    PWCHAR Buffer;

    PAGED_CODE();

    *Copy = NULL;

    Buffer = ExAllocatePoolZero(PagedPool, Size, TAG_PNP_SWDEVICE);
    if (Buffer == NULL) return STATUS_INSUFFICIENT_RESOURCES;

    RtlCopyMemory(Buffer, Source, Size);
    *Copy = Buffer;
    return STATUS_SUCCESS;
}

static
NTSTATUS
PnpSwFormatCreatedDeviceId(
    _In_ PPNP_SW_DEVICE Device,
    _Out_ PWSTR *Id)
{
    USHORT Enumerator = 0;
    PWCHAR Buffer;
    SIZE_T Size;

    PAGED_CODE();

    ASSERT(Device->Created);

    while (Enumerator < Device->Name.Length / sizeof(WCHAR) && Device->Name.Buffer[Enumerator] != L'\\') Enumerator++;
    ASSERT(Enumerator < Device->Name.Length / sizeof(WCHAR));

    *Id = NULL;
    Size = sizeof(L"SWD\\") + Enumerator * sizeof(WCHAR);
    Buffer = ExAllocatePoolZero(PagedPool, Size, TAG_PNP_SWDEVICE);
    if (Buffer == NULL) return STATUS_INSUFFICIENT_RESOURCES;

    RtlCopyMemory(Buffer, L"SWD\\", sizeof(L"SWD\\") - sizeof(WCHAR));
    RtlCopyMemory(&Buffer[4], Device->Name.Buffer, Enumerator * sizeof(WCHAR));
    *Id = Buffer;
    return STATUS_SUCCESS;
}

static
NTSTATUS
PnpSwQueryId(
    _In_ PPNP_SW_DEVICE Device,
    _In_ BUS_QUERY_ID_TYPE IdType,
    _Out_ PWSTR *Id)
{
    PAGED_CODE();

    *Id = NULL;

    switch (IdType)
    {
        case BusQueryDeviceID:
            if (Device->Created) return PnpSwFormatCreatedDeviceId(Device, Id);
            return PnpSwCopyString(PnpSwDeviceId, Id);

        case BusQueryInstanceID:
            ASSERT(Device->InstanceId.Buffer[Device->InstanceId.Length / sizeof(WCHAR)] == UNICODE_NULL);
            return PnpSwCopyString(Device->InstanceId.Buffer, Id);

        case BusQueryHardwareIDs:
            if (Device->Created) return PnpSwQueryIds(Device, L"HardwareID", PnpSwNoIds, sizeof(PnpSwNoIds), Id);
            return PnpSwQueryIds(Device, L"HardwareIds", PnpSwNoIds, sizeof(PnpSwNoIds), Id);

        case BusQueryCompatibleIDs:
            if (Device->Created) return PnpSwQueryIds(Device, L"CompatibleIDs", PnpSwNoIds, sizeof(PnpSwNoIds), Id);
            return PnpSwQueryIds(Device, L"CompatibleIds", PnpSwCompatibleIds, sizeof(PnpSwCompatibleIds), Id);

        case BusQueryContainerID:
            /* TODO: the container ID SwDeviceCreate was given */
            if (Device->Created) return STATUS_NOT_SUPPORTED;
            return PnpSwCopyString(PnpSwContainerId, Id);

        default:
            return STATUS_NOT_SUPPORTED;
    }
}

static
NTSTATUS
PnpSwQueryDescription(
    _In_ PPNP_SW_DEVICE Device,
    _Out_ PWSTR *Text)
{
    PKEY_VALUE_FULL_INFORMATION Information;
    ULONG Chars;
    PWCHAR Buffer;
    NTSTATUS Status;

    PAGED_CODE();

    *Text = NULL;

    Status = PnpSwQueryValue(Device, Device->Created ? L"DeviceDesc" : L"Description", REG_SZ, &Information);
    if (!NT_SUCCESS(Status)) return STATUS_NOT_SUPPORTED;

    Chars = Information->DataLength / sizeof(WCHAR);
    Buffer = ExAllocatePoolZero(PagedPool, (Chars + 1) * sizeof(WCHAR), TAG_PNP_SWDEVICE);
    if (Buffer == NULL)
    {
        ExFreePool(Information);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    RtlCopyMemory(Buffer, (PUCHAR)Information + Information->DataOffset, Chars * sizeof(WCHAR));
    ExFreePool(Information);

    if (Buffer[0] == UNICODE_NULL)
    {
        ExFreePoolWithTag(Buffer, TAG_PNP_SWDEVICE);
        return STATUS_NOT_SUPPORTED;
    }

    *Text = Buffer;
    return STATUS_SUCCESS;
}

static
PPNP_SW_DEVICE
PnpSwFindDevice(
    _In_ PDEVICE_OBJECT Parent,
    _In_ PCUNICODE_STRING Name)
{
    PPNP_SW_DEVICE Device;
    PLIST_ENTRY Entry;

    PAGED_CODE();

    ASSERT(PnpSwIsLockHeld());

    for (Entry = PnpSwDeviceList.Flink; Entry != &PnpSwDeviceList; Entry = Entry->Flink)
    {
        Device = CONTAINING_RECORD(Entry, PNP_SW_DEVICE, ListEntry);

        if (Device->Parent == Parent && !Device->Retired && RtlEqualUnicodeString(&Device->Name, Name, TRUE))
            return Device;
    }

    return NULL;
}

static
NTSTATUS
PnpSwFormatInstanceId(
    _In_ PDEVICE_NODE ParentNode,
    _In_ PCUNICODE_STRING Name,
    _Out_ PUNICODE_STRING InstanceId)
{
    UNICODE_STRING Prefix;
    USHORT Size;
    NTSTATUS Status;

    PAGED_CODE();

    RtlInitEmptyUnicodeString(InstanceId, NULL, 0);

    Status = PiGetChildIdPrefix(ParentNode, &Prefix);
    if (!NT_SUCCESS(Status)) return Status;

    if ((ULONG)Name->Length + sizeof(WCHAR) + Prefix.Length + sizeof(PnpSwDeviceId) >=
        PNP_SW_MAX_DEVICE_ID_CHARS * sizeof(WCHAR))
    {
        ExFreePool(Prefix.Buffer);
        return STATUS_NAME_TOO_LONG;
    }

    Size = (USHORT)(Name->Length + sizeof(WCHAR) + Prefix.Length + sizeof(WCHAR));
    InstanceId->Buffer = ExAllocatePoolZero(PagedPool, Size, TAG_PNP_SWDEVICE);
    if (InstanceId->Buffer == NULL)
    {
        ExFreePool(Prefix.Buffer);
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    InstanceId->MaximumLength = Size;

    Status = RtlUnicodeStringPrintf(InstanceId, L"%wZ&%wZ", Name, &Prefix);
    ExFreePool(Prefix.Buffer);

    ASSERT(!NT_SUCCESS(Status) || InstanceId->Length + sizeof(WCHAR) == Size);
    return Status;
}

static
NTSTATUS
PnpSwSetCreatedInstance(
    _In_ PPNP_SW_DEVICE Device)
{
    PKEY_VALUE_FULL_INFORMATION Information;
    USHORT Enumerator = 0;
    UNICODE_STRING Instance;
    NTSTATUS Status;

    PAGED_CODE();

    ASSERT(Device->Created);

    while (Enumerator < Device->Name.Length / sizeof(WCHAR) && Device->Name.Buffer[Enumerator] != L'\\') Enumerator++;
    if (Enumerator + 1 >= Device->Name.Length / sizeof(WCHAR)) return STATUS_OBJECT_NAME_INVALID;

    Instance.Buffer = &Device->Name.Buffer[Enumerator + 1];
    Instance.Length = Device->Name.Length - (Enumerator + 1) * sizeof(WCHAR);
    Instance.MaximumLength = Instance.Length;

    Device->InstanceId.Buffer = ExAllocatePoolZero(PagedPool, Instance.Length + sizeof(WCHAR), TAG_PNP_SWDEVICE);
    if (Device->InstanceId.Buffer == NULL) return STATUS_INSUFFICIENT_RESOURCES;
    Device->InstanceId.MaximumLength = Instance.Length + sizeof(WCHAR);
    RtlCopyUnicodeString(&Device->InstanceId, &Instance);

    Status = PnpSwQueryValue(Device, L"Capabilities", REG_DWORD, &Information);
    if (NT_SUCCESS(Status))
    {
        if (Information->DataLength == sizeof(ULONG))
            Device->Capabilities = *(PULONG)((PUCHAR)Information + Information->DataOffset);
        ExFreePool(Information);
    }

    return STATUS_SUCCESS;
}

static
PPNP_SW_DEVICE
PnpSwCreateDevice(
    _In_ PDEVICE_NODE ParentNode,
    _In_ PCUNICODE_STRING Name,
    _In_ BOOLEAN Created)
{
    PPNP_SW_PDO_EXTENSION Extension;
    PPNP_SW_DEVICE Device;
    PDEVICE_OBJECT Pdo;
    NTSTATUS Status;

    PAGED_CODE();

    ASSERT(ExIsResourceAcquiredExclusiveLite(&PnpSwDeviceLock));
    ASSERT(PnpSwFindDevice(ParentNode->PhysicalDeviceObject, Name) == NULL);

    Device = ExAllocatePoolZero(PagedPool, sizeof(*Device), TAG_PNP_SWDEVICE);
    if (Device == NULL) return NULL;

    Device->Name.Buffer = ExAllocatePoolZero(PagedPool, Name->Length + sizeof(WCHAR), TAG_PNP_SWDEVICE);
    if (Device->Name.Buffer == NULL)
    {
        PnpSwFreeRecord(Device);
        return NULL;
    }
    Device->Name.MaximumLength = Name->Length + sizeof(WCHAR);
    RtlCopyUnicodeString(&Device->Name, Name);
    Device->Created = Created;

    if (Created)
        Status = PnpSwSetCreatedInstance(Device);
    else
        Status = PnpSwFormatInstanceId(ParentNode, Name, &Device->InstanceId);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("PnP: component %wZ of %wZ gets no instance (%lx)\n", Name, &ParentNode->InstancePath, Status);
        PnpSwFreeRecord(Device);
        return NULL;
    }

    Status = IoCreateDevice(PnpSwDriverObject,
                            sizeof(PNP_SW_PDO_EXTENSION),
                            NULL,
                            FILE_DEVICE_UNKNOWN,
                            FILE_AUTOGENERATED_DEVICE_NAME | FILE_DEVICE_SECURE_OPEN,
                            FALSE,
                            &Pdo);
    if (!NT_SUCCESS(Status))
    {
        PnpSwFreeRecord(Device);
        return NULL;
    }

    Extension = Pdo->DeviceExtension;
    Extension->Device = Device;

    ObReferenceObject(ParentNode->PhysicalDeviceObject);
    Device->Parent = ParentNode->PhysicalDeviceObject;
    Device->PhysicalDeviceObject = Pdo;

    Pdo->Flags &= ~DO_DEVICE_INITIALIZING;

    InsertTailList(&PnpSwDeviceList, &Device->ListEntry);

    return Device;
}

static
VOID
PnpSwDeleteDevice(
    _In_ PPNP_SW_DEVICE Device)
{
    PDEVICE_OBJECT Pdo = Device->PhysicalDeviceObject;
    PPNP_SW_PDO_EXTENSION Extension;

    PAGED_CODE();

    ASSERT(!PnpSwIsLockHeld());
    ASSERT(Pdo != NULL);

    Extension = Pdo->DeviceExtension;
    ASSERT(Extension->Device == NULL);

    Device->PhysicalDeviceObject = NULL;
    PnpSwFreeRecord(Device);
    IoDeleteDevice(Pdo);
}

/* The names of the components recorded under Devices of the parent's instance key */
static
NTSTATUS
PnpSwReadComponentNames(
    _In_ PDEVICE_OBJECT Parent,
    _Out_ PUNICODE_STRING *Names,
    _Out_ PULONG Count)
{
    PKEY_BASIC_INFORMATION Information;
    HANDLE InstanceKey, DevicesKey;
    KEY_FULL_INFORMATION KeyInformation;
    PUNICODE_STRING List;
    ULONG Length, Index, Used = 0;
    NTSTATUS Status;

    PAGED_CODE();

    *Names = NULL;
    *Count = 0;

    Status = PnpDeviceObjectToDeviceInstance(Parent, &InstanceKey, KEY_READ);
    if (!NT_SUCCESS(Status)) return Status;

    Status = IopOpenRegistryKeyEx(&DevicesKey, InstanceKey, (PUNICODE_STRING)&PnpSwDevicesKeyName, KEY_READ);
    ZwClose(InstanceKey);
    if (!NT_SUCCESS(Status)) return STATUS_SUCCESS;

    Status = ZwQueryKey(DevicesKey, KeyFullInformation, &KeyInformation, sizeof(KeyInformation), &Length);
    if ((Status != STATUS_SUCCESS && Status != STATUS_BUFFER_OVERFLOW) || KeyInformation.SubKeys == 0)
    {
        ZwClose(DevicesKey);
        return STATUS_SUCCESS;
    }

    Length = FIELD_OFFSET(KEY_BASIC_INFORMATION, Name) + PNP_SW_MAX_DEVICE_ID_CHARS * sizeof(WCHAR);
    Information = ExAllocatePoolZero(PagedPool, Length, TAG_PNP_SWDEVICE);
    List = ExAllocatePoolZero(PagedPool, KeyInformation.SubKeys * sizeof(UNICODE_STRING), TAG_PNP_SWDEVICE);
    if (Information == NULL || List == NULL)
    {
        if (Information != NULL) ExFreePoolWithTag(Information, TAG_PNP_SWDEVICE);
        if (List != NULL) ExFreePoolWithTag(List, TAG_PNP_SWDEVICE);
        ZwClose(DevicesKey);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    for (Index = 0; Used < KeyInformation.SubKeys; Index++)
    {
        ULONG Returned;
        UNICODE_STRING Name;

        Status = ZwEnumerateKey(DevicesKey, Index, KeyBasicInformation, Information, Length, &Returned);
        if (Status == STATUS_BUFFER_OVERFLOW || Status == STATUS_BUFFER_TOO_SMALL) continue;
        if (!NT_SUCCESS(Status)) break;

        Name.Buffer = Information->Name;
        Name.Length = (USHORT)Information->NameLength;
        Name.MaximumLength = Name.Length;

        if (!PnpSwIsValidName(&Name))
        {
            DPRINT1("PnP: component name %wZ cannot name a device\n", &Name);
            continue;
        }

        List[Used].Buffer = ExAllocatePoolZero(PagedPool, Name.Length + sizeof(WCHAR), TAG_PNP_SWDEVICE);
        if (List[Used].Buffer == NULL) break;
        List[Used].MaximumLength = Name.Length + sizeof(WCHAR);
        RtlCopyUnicodeString(&List[Used], &Name);
        Used++;
    }

    ExFreePoolWithTag(Information, TAG_PNP_SWDEVICE);
    ZwClose(DevicesKey);

    *Names = List;
    *Count = Used;
    return STATUS_SUCCESS;
}

static
VOID
PnpSwFreeNames(
    _In_opt_ PUNICODE_STRING Names,
    _In_ ULONG Count)
{
    ULONG Index;

    PAGED_CODE();

    if (Names == NULL) return;

    for (Index = 0; Index < Count; Index++)
    {
        ExFreePoolWithTag(Names[Index].Buffer, TAG_PNP_SWDEVICE);
    }

    ExFreePoolWithTag(Names, TAG_PNP_SWDEVICE);
}

static
BOOLEAN
PnpSwIsCreatedInstanceReported(
    _In_ HANDLE InstanceKey)
{
    UNICODE_STRING ControlName = RTL_CONSTANT_STRING(L"Control");
    PKEY_VALUE_FULL_INFORMATION Information;
    BOOLEAN Reported = FALSE;
    HANDLE ControlKey;

    PAGED_CODE();

    if (!NT_SUCCESS(IopOpenRegistryKeyEx(&ControlKey, InstanceKey, &ControlName, KEY_QUERY_VALUE))) return FALSE;

    if (NT_SUCCESS(IopGetRegistryValue(ControlKey, L"DeviceReported", &Information)))
    {
        Reported = Information->Type == REG_DWORD && Information->DataLength == sizeof(ULONG) &&
                   *(PULONG)((PUCHAR)Information + Information->DataOffset) != 0;
        ExFreePool(Information);
    }

    ZwClose(ControlKey);
    return Reported;
}

static
NTSTATUS
PnpSwAppendName(
    _Inout_ PUNICODE_STRING *Names,
    _Inout_ PULONG Count,
    _Inout_ PULONG Capacity,
    _In_ PCUNICODE_STRING Enumerator,
    _In_ PCUNICODE_STRING Instance)
{
    PUNICODE_STRING List;
    USHORT Size;

    PAGED_CODE();

    ASSERT(*Count <= *Capacity);

    if (*Count == *Capacity)
    {
        List = ExAllocatePoolZero(PagedPool, (*Capacity + 8) * sizeof(UNICODE_STRING), TAG_PNP_SWDEVICE);
        if (List == NULL) return STATUS_INSUFFICIENT_RESOURCES;
        if (*Names != NULL)
        {
            RtlCopyMemory(List, *Names, *Count * sizeof(UNICODE_STRING));
            ExFreePoolWithTag(*Names, TAG_PNP_SWDEVICE);
        }
        *Names = List;
        *Capacity += 8;
    }

    Size = Enumerator->Length + sizeof(WCHAR) + Instance->Length + sizeof(WCHAR);
    List = &(*Names)[*Count];
    List->Buffer = ExAllocatePoolZero(PagedPool, Size, TAG_PNP_SWDEVICE);
    if (List->Buffer == NULL) return STATUS_INSUFFICIENT_RESOURCES;
    List->MaximumLength = Size;
    RtlCopyUnicodeString(List, Enumerator);
    RtlAppendUnicodeToString(List, L"\\");
    RtlAppendUnicodeStringToString(List, Instance);
    (*Count)++;

    return STATUS_SUCCESS;
}

/* The devices SwDeviceCreate reports under the root, which umpnpmgr marks with Control\DeviceReported */
static
NTSTATUS
PnpSwReadCreatedNames(
    _Out_ PUNICODE_STRING *Names,
    _Out_ PULONG Count)
{
    UNICODE_STRING Enumerator, Instance;
    PKEY_BASIC_INFORMATION EnumeratorInformation, InstanceInformation;
    HANDLE CreatedKey, EnumeratorKey, InstanceKey;
    ULONG Length, Returned, EnumeratorIndex, InstanceIndex, Capacity = 0;
    NTSTATUS Status;

    PAGED_CODE();

    *Names = NULL;
    *Count = 0;

    Status = IopOpenRegistryKeyEx(&CreatedKey, NULL, (PUNICODE_STRING)&PnpSwCreatedKeyName, KEY_READ);
    if (!NT_SUCCESS(Status)) return STATUS_SUCCESS;

    Length = FIELD_OFFSET(KEY_BASIC_INFORMATION, Name) + PNP_SW_MAX_DEVICE_ID_CHARS * sizeof(WCHAR);
    EnumeratorInformation = ExAllocatePoolZero(PagedPool, Length, TAG_PNP_SWDEVICE);
    InstanceInformation = ExAllocatePoolZero(PagedPool, Length, TAG_PNP_SWDEVICE);
    if (EnumeratorInformation == NULL || InstanceInformation == NULL)
    {
        if (EnumeratorInformation != NULL) ExFreePoolWithTag(EnumeratorInformation, TAG_PNP_SWDEVICE);
        if (InstanceInformation != NULL) ExFreePoolWithTag(InstanceInformation, TAG_PNP_SWDEVICE);
        ZwClose(CreatedKey);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    Status = STATUS_SUCCESS;
    for (EnumeratorIndex = 0; NT_SUCCESS(Status); EnumeratorIndex++)
    {
        Status = ZwEnumerateKey(CreatedKey, EnumeratorIndex, KeyBasicInformation, EnumeratorInformation, Length,
                                &Returned);
        if (Status == STATUS_NO_MORE_ENTRIES)
        {
            Status = STATUS_SUCCESS;
            break;
        }
        if (Status == STATUS_BUFFER_OVERFLOW || Status == STATUS_BUFFER_TOO_SMALL)
        {
            Status = STATUS_SUCCESS;
            continue;
        }
        if (!NT_SUCCESS(Status)) break;

        Enumerator.Buffer = EnumeratorInformation->Name;
        Enumerator.Length = Enumerator.MaximumLength = (USHORT)EnumeratorInformation->NameLength;
        if (!PnpSwIsValidName(&Enumerator)) continue;
        if (!NT_SUCCESS(IopOpenRegistryKeyEx(&EnumeratorKey, CreatedKey, &Enumerator, KEY_READ))) continue;

        for (InstanceIndex = 0; NT_SUCCESS(Status); InstanceIndex++)
        {
            Status = ZwEnumerateKey(EnumeratorKey, InstanceIndex, KeyBasicInformation, InstanceInformation, Length,
                                    &Returned);
            if (Status == STATUS_NO_MORE_ENTRIES)
            {
                Status = STATUS_SUCCESS;
                break;
            }
            if (Status == STATUS_BUFFER_OVERFLOW || Status == STATUS_BUFFER_TOO_SMALL)
            {
                Status = STATUS_SUCCESS;
                continue;
            }
            if (!NT_SUCCESS(Status)) break;

            Instance.Buffer = InstanceInformation->Name;
            Instance.Length = Instance.MaximumLength = (USHORT)InstanceInformation->NameLength;
            if (!PnpSwIsValidName(&Instance)) continue;
            if (Enumerator.Length + sizeof(WCHAR) + Instance.Length + sizeof(L"SWD\\") >=
                PNP_SW_MAX_DEVICE_ID_CHARS * sizeof(WCHAR))
                continue;
            if (!NT_SUCCESS(IopOpenRegistryKeyEx(&InstanceKey, EnumeratorKey, &Instance, KEY_READ))) continue;

            if (PnpSwIsCreatedInstanceReported(InstanceKey))
                Status = PnpSwAppendName(Names, Count, &Capacity, &Enumerator, &Instance);
            ZwClose(InstanceKey);
        }

        ZwClose(EnumeratorKey);
    }

    ExFreePoolWithTag(EnumeratorInformation, TAG_PNP_SWDEVICE);
    ExFreePoolWithTag(InstanceInformation, TAG_PNP_SWDEVICE);
    ZwClose(CreatedKey);

    if (!NT_SUCCESS(Status))
    {
        PnpSwFreeNames(*Names, *Count);
        *Names = NULL;
        *Count = 0;
    }

    return Status;
}

static
NTSTATUS
NTAPI
PnpSwDevicePnp(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp)
{
    PIO_STACK_LOCATION StackPtr = IoGetCurrentIrpStackLocation(Irp);
    PPNP_SW_PDO_EXTENSION Extension = DeviceObject->DeviceExtension;
    PPNP_SW_DEVICE Device = NULL;
    NTSTATUS Status = Irp->IoStatus.Status;
    BOOLEAN Delete = FALSE;
    PWSTR Text = NULL;

    PAGED_CODE();

    ASSERT(DeviceObject->DriverObject == PnpSwDriverObject);

    switch (StackPtr->MinorFunction)
    {
        case IRP_MN_QUERY_ID:
            KeEnterCriticalRegion();
            ExAcquireResourceSharedLite(&PnpSwDeviceLock, TRUE);
            Device = Extension->Device;
            if (Device == NULL)
            {
                Status = STATUS_NO_SUCH_DEVICE;
            }
            else
            {
                Status = PnpSwQueryId(Device, StackPtr->Parameters.QueryId.IdType, &Text);
                if (Status == STATUS_NOT_SUPPORTED) Status = Irp->IoStatus.Status;
                else if (NT_SUCCESS(Status)) Irp->IoStatus.Information = (ULONG_PTR)Text;
            }
            ExReleaseResourceLite(&PnpSwDeviceLock);
            KeLeaveCriticalRegion();
            break;

        case IRP_MN_QUERY_DEVICE_TEXT:
            if (StackPtr->Parameters.QueryDeviceText.DeviceTextType != DeviceTextDescription) break;

            KeEnterCriticalRegion();
            ExAcquireResourceSharedLite(&PnpSwDeviceLock, TRUE);
            Device = Extension->Device;
            if (Device != NULL)
            {
                Status = PnpSwQueryDescription(Device, &Text);
                if (Status == STATUS_NOT_SUPPORTED) Status = Irp->IoStatus.Status;
                else if (NT_SUCCESS(Status)) Irp->IoStatus.Information = (ULONG_PTR)Text;
            }
            ExReleaseResourceLite(&PnpSwDeviceLock);
            KeLeaveCriticalRegion();
            break;

        case IRP_MN_QUERY_CAPABILITIES:
        {
            PDEVICE_CAPABILITIES Capabilities = StackPtr->Parameters.DeviceCapabilities.Capabilities;

            if (Capabilities->Version != 1)
            {
                Status = STATUS_UNSUCCESSFUL;
                break;
            }

            /* Windows 11 records 0xD0 for these devices: unique ID, raw device OK, surprise removal OK */
            Capabilities->UniqueID = TRUE;
            Capabilities->RawDeviceOK = TRUE;
            Capabilities->SurpriseRemovalOK = TRUE;
            Capabilities->Address = MAXULONG;
            Capabilities->UINumber = MAXULONG;

            KeEnterCriticalRegion();
            ExAcquireResourceSharedLite(&PnpSwDeviceLock, TRUE);
            Device = Extension->Device;
            if (Device != NULL && Device->Created)
            {
                Capabilities->Removable = !!(Device->Capabilities & CM_DEVCAP_REMOVABLE);
                Capabilities->UniqueID = !!(Device->Capabilities & CM_DEVCAP_UNIQUEID);
                Capabilities->SilentInstall = !!(Device->Capabilities & CM_DEVCAP_SILENTINSTALL);
                Capabilities->RawDeviceOK = !!(Device->Capabilities & CM_DEVCAP_RAWDEVICEOK);
                Capabilities->SurpriseRemovalOK = !!(Device->Capabilities & CM_DEVCAP_SURPRISEREMOVALOK);
            }
            ExReleaseResourceLite(&PnpSwDeviceLock);
            KeLeaveCriticalRegion();

            Status = STATUS_SUCCESS;
            break;
        }

        case IRP_MN_QUERY_DEVICE_RELATIONS:
        {
            PDEVICE_RELATIONS Relations;

            if (StackPtr->Parameters.QueryDeviceRelations.Type != TargetDeviceRelation) break;

            Relations = ExAllocatePoolZero(PagedPool, sizeof(DEVICE_RELATIONS), TAG_PNP_SWDEVICE);
            if (Relations == NULL)
            {
                Status = STATUS_INSUFFICIENT_RESOURCES;
                break;
            }

            ObReferenceObject(DeviceObject);
            Relations->Count = 1;
            Relations->Objects[0] = DeviceObject;

            Irp->IoStatus.Information = (ULONG_PTR)Relations;
            Status = STATUS_SUCCESS;
            break;
        }

        case IRP_MN_START_DEVICE:
        case IRP_MN_QUERY_REMOVE_DEVICE:
        case IRP_MN_CANCEL_REMOVE_DEVICE:
        case IRP_MN_STOP_DEVICE:
        case IRP_MN_QUERY_STOP_DEVICE:
        case IRP_MN_CANCEL_STOP_DEVICE:
        case IRP_MN_SURPRISE_REMOVAL:
        case IRP_MN_QUERY_PNP_DEVICE_STATE:
            Status = STATUS_SUCCESS;
            break;

        case IRP_MN_REMOVE_DEVICE:
            /* A component its parent still lists keeps its device; one no longer listed is deleted */
            KeEnterCriticalRegion();
            ExAcquireResourceExclusiveLite(&PnpSwDeviceLock, TRUE);
            Device = Extension->Device;
            if (Device != NULL && !Device->Reported)
            {
                ASSERT(Device->PhysicalDeviceObject == DeviceObject);
                RemoveEntryList(&Device->ListEntry);
                Extension->Device = NULL;
                Delete = TRUE;
            }
            ExReleaseResourceLite(&PnpSwDeviceLock);
            KeLeaveCriticalRegion();

            Status = STATUS_SUCCESS;
            break;

        default:
            break;
    }

    Irp->IoStatus.Status = Status;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);

    if (Delete) PnpSwDeleteDevice(Device);

    return Status;
}

static
NTSTATUS
NTAPI
PnpSwDevicePower(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp)
{
    PIO_STACK_LOCATION StackPtr = IoGetCurrentIrpStackLocation(Irp);
    NTSTATUS Status = Irp->IoStatus.Status;

    UNREFERENCED_PARAMETER(DeviceObject);

    if (StackPtr->MinorFunction == IRP_MN_SET_POWER ||
        StackPtr->MinorFunction == IRP_MN_QUERY_POWER)
    {
        Status = STATUS_SUCCESS;
        Irp->IoStatus.Status = Status;
    }

    PoStartNextPowerIrp(Irp);
    IoCompleteRequest(Irp, IO_NO_INCREMENT);

    return Status;
}

/* PUBLIC FUNCTIONS ***********************************************************/

VOID
PnpSwDeviceInitialize(VOID)
{
    InitializeListHead(&PnpSwDeviceList);
    ExInitializeResourceLite(&PnpSwDeviceLock);
}

NTSTATUS
NTAPI
PnpSwDeviceDriverEntry(
    _In_opt_ PDRIVER_OBJECT DriverObject,
    _In_opt_ PUNICODE_STRING RegistryPath)
{
    ULONG Index;

    PAGED_CODE();

    UNREFERENCED_PARAMETER(RegistryPath);

    if (DriverObject == NULL)
    {
        return IoCreateDriver((PUNICODE_STRING)&PnpSwDriverName, PnpSwDeviceDriverEntry);
    }

    ASSERT(PnpSwDriverObject == NULL);
    PnpSwDriverObject = DriverObject;

    for (Index = 0; Index <= IRP_MJ_MAXIMUM_FUNCTION; Index++)
    {
        DriverObject->MajorFunction[Index] = IopInvalidDeviceRequest;
    }

    DriverObject->MajorFunction[IRP_MJ_PNP] = PnpSwDevicePnp;
    DriverObject->MajorFunction[IRP_MJ_POWER] = PnpSwDevicePower;

    return STATUS_SUCCESS;
}

/* The components a device's driver installed are its children whatever its bus driver reports */
NTSTATUS
PnpSwDeviceAddChildren(
    _In_ PDEVICE_NODE DeviceNode,
    _In_ NTSTATUS QueryStatus)
{
    PDEVICE_RELATIONS Relations, Merged;
    PDEVICE_OBJECT Parent = DeviceNode->PhysicalDeviceObject;
    PUNICODE_STRING Names = NULL;
    PPNP_SW_DEVICE Device;
    PLIST_ENTRY Entry;
    ULONG Count = 0, Existing = 0, Reported = 0, Previous, Index;
    BOOLEAN Created = (DeviceNode == IopRootDeviceNode);
    BOOLEAN Dropped = FALSE;
    NTSTATUS Status;

    PAGED_CODE();

    ASSERT(Parent != NULL);

    if (!NT_SUCCESS(QueryStatus) && QueryStatus != STATUS_NOT_SUPPORTED) return QueryStatus;
    if (PnpSwDriverObject == NULL) return QueryStatus;

    Relations = DeviceNode->OverUsed1.PendingDeviceRelations;
    ASSERT(NT_SUCCESS(QueryStatus) || Relations == NULL);
    Previous = (Relations != NULL) ? Relations->Count : 0;

    if (Created)
        Status = PnpSwReadCreatedNames(&Names, &Count);
    else
        Status = PnpSwReadComponentNames(Parent, &Names, &Count);
    if (!NT_SUCCESS(Status)) Count = 0;

    Merged = ExAllocatePoolZero(PagedPool,
                                FIELD_OFFSET(DEVICE_RELATIONS, Objects) + (Previous + Count) * sizeof(PDEVICE_OBJECT),
                                TAG_PNP_SWDEVICE);

    KeEnterCriticalRegion();
    ExAcquireResourceExclusiveLite(&PnpSwDeviceLock, TRUE);

    for (Entry = PnpSwDeviceList.Flink; Entry != &PnpSwDeviceList; Entry = Entry->Flink)
    {
        Device = CONTAINING_RECORD(Entry, PNP_SW_DEVICE, ListEntry);
        if (Device->Parent != Parent || Device->Retired) continue;

        ASSERT(Device->Created == Created);
        Device->Listed = FALSE;
        Existing++;
    }

    if (Count == 0 && Existing == 0)
    {
        ExReleaseResourceLite(&PnpSwDeviceLock);
        KeLeaveCriticalRegion();
        if (Merged != NULL) ExFreePoolWithTag(Merged, TAG_PNP_SWDEVICE);
        PnpSwFreeNames(Names, Count);
        return QueryStatus;
    }

    /* A dropped device keeps its record until its remove request, and its name gets a new device meanwhile */
    for (Index = 0; Index < Count && Merged != NULL; Index++)
    {
        Device = PnpSwFindDevice(Parent, &Names[Index]);
        if (Device == NULL) Device = PnpSwCreateDevice(DeviceNode, &Names[Index], Created);
        if (Device != NULL) Device->Listed = TRUE;
    }

    if (Merged != NULL)
    {
        if (Previous != 0) RtlCopyMemory(Merged->Objects, Relations->Objects, Previous * sizeof(PDEVICE_OBJECT));
        Merged->Count = Previous;
    }

    for (Entry = PnpSwDeviceList.Flink; Entry != &PnpSwDeviceList; Entry = Entry->Flink)
    {
        Device = CONTAINING_RECORD(Entry, PNP_SW_DEVICE, ListEntry);
        if (Device->Parent != Parent || Device->Retired) continue;

        if (Device->Listed)
        {
            ASSERT(Merged != NULL);
            ASSERT(Merged->Count < Previous + Count);
            Device->Reported = TRUE;
            Device->Handed = TRUE;
            ObReferenceObject(Device->PhysicalDeviceObject);
            Merged->Objects[Merged->Count++] = Device->PhysicalDeviceObject;
            Reported++;
            continue;
        }

        Device->Reported = FALSE;
        if (Device->Handed)
        {
            Device->Retired = TRUE;
            Dropped = TRUE;
        }
    }
    ASSERT(Merged == NULL || Merged->Count == Previous + Reported);

    ExReleaseResourceLite(&PnpSwDeviceLock);
    KeLeaveCriticalRegion();
    PnpSwFreeNames(Names, Count);

    if (Merged == NULL)
    {
        /* The devices go unreported this time; the next enumeration reports them again */
        if (Dropped) IoInvalidateDeviceRelations(Parent, BusRelations);
        return QueryStatus;
    }

    if (Relations != NULL) ExFreePool(Relations);
    DeviceNode->OverUsed1.PendingDeviceRelations = Merged;

    return STATUS_SUCCESS;
}

/* The PnP manager is the bus of a device's components: when the device's stack is removed, their devices are deleted */
VOID
PnpSwDeviceParentRemoved(
    _In_ PDEVICE_OBJECT Parent)
{
    LIST_ENTRY Removed;
    PPNP_SW_PDO_EXTENSION Extension;
    PPNP_SW_DEVICE Device;
    PLIST_ENTRY Entry, Next;

    PAGED_CODE();

    if (PnpSwDriverObject == NULL) return;

    InitializeListHead(&Removed);

    KeEnterCriticalRegion();
    ExAcquireResourceExclusiveLite(&PnpSwDeviceLock, TRUE);

    for (Entry = PnpSwDeviceList.Flink; Entry != &PnpSwDeviceList; Entry = Next)
    {
        Next = Entry->Flink;
        Device = CONTAINING_RECORD(Entry, PNP_SW_DEVICE, ListEntry);
        if (Device->Parent != Parent) continue;

        Extension = Device->PhysicalDeviceObject->DeviceExtension;
        ASSERT(Extension->Device == Device);
        Extension->Device = NULL;

        RemoveEntryList(&Device->ListEntry);
        InsertTailList(&Removed, &Device->ListEntry);
    }

    ExReleaseResourceLite(&PnpSwDeviceLock);
    KeLeaveCriticalRegion();

    while (!IsListEmpty(&Removed))
    {
        Entry = RemoveHeadList(&Removed);
        PnpSwDeleteDevice(CONTAINING_RECORD(Entry, PNP_SW_DEVICE, ListEntry));
    }
}
