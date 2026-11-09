/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     The interfaces a device publishes for others to open it by
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#include "../ioint.h"
#include <wdmguid.h>
#define NDEBUG
#include <debug.h>

/* GLOBALS ********************************************************************/

LIST_ENTRY IopDeviceInterfaceListHead;
KGUARDED_MUTEX IopDeviceInterfaceLock;

/* PRIVATE DATA ***************************************************************/

static const UNICODE_STRING IopDeviceClassesKeyName = RTL_CONSTANT_STRING(
    L"\\Registry\\Machine\\System\\CurrentControlSet\\Control"
    L"\\DeviceClasses");

/* PRIVATE FUNCTIONS **********************************************************/

static
NTSTATUS
IopBuildInterfaceName(
    _In_ PUNICODE_STRING InstancePath,
    _In_ CONST GUID *ClassGuid,
    _In_opt_ PUNICODE_STRING ReferenceString,
    _In_ BOOLEAN RegistryForm,
    _Out_ PUNICODE_STRING Name)
{
    UNICODE_STRING GuidString;
    UNICODE_STRING Escaped;
    NTSTATUS Status;
    USHORT Length;
    PWCHAR Buffer;
    USHORT Index;

    PAGED_CODE();

    RtlInitEmptyUnicodeString(Name, NULL, 0);

    Status = RtlStringFromGUID(ClassGuid, &GuidString);
    if (!NT_SUCCESS(Status)) return Status;

    Length = (USHORT)(4 * sizeof(WCHAR)) +
             InstancePath->Length +
             (USHORT)sizeof(WCHAR) +
             GuidString.Length +
             ((ReferenceString != NULL)
                  ? ReferenceString->Length + (USHORT)sizeof(WCHAR)
                  : 0) +
             (USHORT)sizeof(WCHAR);

    Buffer = ExAllocatePoolZero(PagedPool, Length, TAG_IO_NAME);
    if (Buffer == NULL)
    {
        RtlFreeUnicodeString(&GuidString);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    Name->Buffer = Buffer;
    Name->Length = 0;
    Name->MaximumLength = Length;

    if (RegistryForm)
    {
        RtlAppendUnicodeToString(Name, L"##?#");
    }
    else
    {
        RtlAppendUnicodeToString(Name, L"\\??\\");
    }

    Escaped.Buffer = Name->Buffer + Name->Length / sizeof(WCHAR);
    Escaped.Length = InstancePath->Length;
    Escaped.MaximumLength = InstancePath->Length;

    RtlCopyMemory(Escaped.Buffer, InstancePath->Buffer, InstancePath->Length);

    for (Index = 0; Index < Escaped.Length / sizeof(WCHAR); Index++)
    {
        if (Escaped.Buffer[Index] == OBJ_NAME_PATH_SEPARATOR)
        {
            Escaped.Buffer[Index] = L'#';
        }
    }

    Name->Length += InstancePath->Length;

    RtlAppendUnicodeToString(Name, L"#");
    RtlAppendUnicodeStringToString(Name, &GuidString);

    if (ReferenceString != NULL && ReferenceString->Length != 0)
    {
        RtlAppendUnicodeToString(Name, L"\\");
        RtlAppendUnicodeStringToString(Name, ReferenceString);
    }

    RtlFreeUnicodeString(&GuidString);

    return STATUS_SUCCESS;
}

NTSTATUS
IopOpenInterfaceClassKey(
    _In_ CONST GUID *ClassGuid,
    _In_ BOOLEAN Create,
    _Out_ PHANDLE KeyHandle)
{
    UNICODE_STRING GuidString;
    NTSTATUS Status;
    HANDLE Classes;

    PAGED_CODE();

    *KeyHandle = NULL;

    Status = RtlStringFromGUID(ClassGuid, &GuidString);
    if (!NT_SUCCESS(Status)) return Status;

    if (Create)
    {
        Status = IopCreateRegistryKeyEx(
            &Classes,
            NULL,
            (PUNICODE_STRING)&IopDeviceClassesKeyName,
            KEY_ALL_ACCESS,
            REG_OPTION_NON_VOLATILE,
            NULL);
    }
    else
    {
        Status = IopOpenRegistryKeyEx(
            &Classes,
            NULL,
            (PUNICODE_STRING)&IopDeviceClassesKeyName,
            KEY_READ);
    }

    if (!NT_SUCCESS(Status))
    {
        RtlFreeUnicodeString(&GuidString);
        return Status;
    }

    if (Create)
    {
        Status = IopCreateRegistryKeyEx(KeyHandle,
                                        Classes,
                                        &GuidString,
                                        KEY_ALL_ACCESS,
                                        REG_OPTION_NON_VOLATILE,
                                        NULL);
    }
    else
    {
        Status = IopOpenRegistryKeyEx(KeyHandle,
                                      Classes,
                                      &GuidString,
                                      KEY_READ);
    }

    ZwClose(Classes);
    RtlFreeUnicodeString(&GuidString);

    return Status;
}

static
PIOP_DEVICE_INTERFACE
IopFindInterface(
    _In_ PUNICODE_STRING SymbolicLinkName)
{
    PIOP_DEVICE_INTERFACE Interface;
    PLIST_ENTRY Entry;

    Entry = IopDeviceInterfaceListHead.Flink;
    while (Entry != &IopDeviceInterfaceListHead)
    {
        Interface = CONTAINING_RECORD(Entry,
                                      IOP_DEVICE_INTERFACE,
                                      ListEntry);
        Entry = Entry->Flink;

        if (RtlEqualUnicodeString(&Interface->SymbolicLinkName,
                                  SymbolicLinkName,
                                  TRUE))
        {
            return Interface;
        }
    }

    return NULL;
}

/* PUBLIC FUNCTIONS ***********************************************************/

NTSTATUS
NTAPI
IoRegisterDeviceInterface(
    _In_ PDEVICE_OBJECT PhysicalDeviceObject,
    _In_ CONST GUID *InterfaceClassGuid,
    _In_opt_ PUNICODE_STRING ReferenceString,
    _Out_ PUNICODE_STRING SymbolicLinkName)
{
    PIOP_DEVICE_INTERFACE Interface;
    UNICODE_STRING RegistryName;
    UNICODE_STRING LinkName;
    UNICODE_STRING ValueName;
    PDEVICE_NODE DeviceNode;
    NTSTATUS Status;
    HANDLE ClassKey;
    HANDLE InstanceKey;
    HANDLE ControlKey;
    PWCHAR Buffer;

    PAGED_CODE();

    RtlInitEmptyUnicodeString(SymbolicLinkName, NULL, 0);

    DeviceNode = IopGetDeviceNode(PhysicalDeviceObject);
    if (DeviceNode == NULL) return STATUS_INVALID_DEVICE_REQUEST;
    if (DeviceNode->InstancePath.Length == 0) return STATUS_INVALID_DEVICE_REQUEST;

    Status = IopBuildInterfaceName(&DeviceNode->InstancePath,
                                   InterfaceClassGuid,
                                   ReferenceString,
                                   FALSE,
                                   &LinkName);
    if (!NT_SUCCESS(Status)) return Status;

    Status = IopBuildInterfaceName(&DeviceNode->InstancePath,
                                   InterfaceClassGuid,
                                   ReferenceString,
                                   TRUE,
                                   &RegistryName);
    if (!NT_SUCCESS(Status))
    {
        ExFreePoolWithTag(LinkName.Buffer, TAG_IO_NAME);
        return Status;
    }

    Status = IopOpenInterfaceClassKey(InterfaceClassGuid, TRUE, &ClassKey);
    if (!NT_SUCCESS(Status))
    {
        ExFreePoolWithTag(RegistryName.Buffer, TAG_IO_NAME);
        ExFreePoolWithTag(LinkName.Buffer, TAG_IO_NAME);
        return Status;
    }

    Status = IopCreateRegistryKeyEx(&InstanceKey,
                                    ClassKey,
                                    &RegistryName,
                                    KEY_ALL_ACCESS,
                                    REG_OPTION_NON_VOLATILE,
                                    NULL);
    ZwClose(ClassKey);

    ExFreePoolWithTag(RegistryName.Buffer, TAG_IO_NAME);

    if (!NT_SUCCESS(Status))
    {
        ExFreePoolWithTag(LinkName.Buffer, TAG_IO_NAME);
        return Status;
    }

    RtlInitUnicodeString(&ValueName, L"DeviceInstance");

    ZwSetValueKey(InstanceKey,
                  &ValueName,
                  0,
                  REG_SZ,
                  DeviceNode->InstancePath.Buffer,
                  DeviceNode->InstancePath.Length + sizeof(WCHAR));

    RtlInitUnicodeString(&ValueName, L"#");

    Status = IopCreateRegistryKeyEx(&ControlKey,
                                    InstanceKey,
                                    &ValueName,
                                    KEY_ALL_ACCESS,
                                    REG_OPTION_NON_VOLATILE,
                                    NULL);
    if (NT_SUCCESS(Status))
    {
        RtlInitUnicodeString(&ValueName, L"SymbolicLink");

        ZwSetValueKey(ControlKey,
                      &ValueName,
                      0,
                      REG_SZ,
                      LinkName.Buffer,
                      LinkName.Length + sizeof(WCHAR));

        ZwClose(ControlKey);
    }

    ZwClose(InstanceKey);

    Interface = ExAllocatePoolZero(NonPagedPool,
                                   sizeof(IOP_DEVICE_INTERFACE),
                                   TAG_IO_NAME);
    if (Interface == NULL)
    {
        ExFreePoolWithTag(LinkName.Buffer, TAG_IO_NAME);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    Interface->SymbolicLinkName = LinkName;
    Interface->ClassGuid = *InterfaceClassGuid;
    Interface->PhysicalDeviceObject = PhysicalDeviceObject;
    Interface->Enabled = FALSE;

    KeAcquireGuardedMutex(&IopDeviceInterfaceLock);
    InsertTailList(&IopDeviceInterfaceListHead, &Interface->ListEntry);
    KeReleaseGuardedMutex(&IopDeviceInterfaceLock);

    Buffer = ExAllocatePoolZero(PagedPool,
                                LinkName.Length + sizeof(WCHAR),
                                TAG_IO_NAME);
    if (Buffer == NULL) return STATUS_INSUFFICIENT_RESOURCES;

    RtlCopyMemory(Buffer, LinkName.Buffer, LinkName.Length);

    SymbolicLinkName->Buffer = Buffer;
    SymbolicLinkName->Length = LinkName.Length;
    SymbolicLinkName->MaximumLength = LinkName.Length + sizeof(WCHAR);

    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
IoSetDeviceInterfaceState(
    _In_ PUNICODE_STRING SymbolicLinkName,
    _In_ BOOLEAN Enable)
{
    PIOP_DEVICE_INTERFACE Interface;
    PDEVICE_NODE DeviceNode;
    UNICODE_STRING DeviceName;
    NTSTATUS Status;

    PAGED_CODE();

    KeAcquireGuardedMutex(&IopDeviceInterfaceLock);

    Interface = IopFindInterface(SymbolicLinkName);
    if (Interface == NULL)
    {
        KeReleaseGuardedMutex(&IopDeviceInterfaceLock);
        return STATUS_OBJECT_NAME_NOT_FOUND;
    }

    if (Interface->Enabled == Enable)
    {
        KeReleaseGuardedMutex(&IopDeviceInterfaceLock);
        return STATUS_OBJECT_NAME_EXISTS;
    }

    Interface->Enabled = Enable;

    KeReleaseGuardedMutex(&IopDeviceInterfaceLock);

    if (Enable)
    {
        POBJECT_NAME_INFORMATION NameInfo;
        ULONG NameLength = 0;

        DeviceNode = IopGetDeviceNode(Interface->PhysicalDeviceObject);
        if (DeviceNode == NULL) return STATUS_INVALID_DEVICE_REQUEST;

        Status = ObQueryNameString(Interface->PhysicalDeviceObject,
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

        Status = ObQueryNameString(Interface->PhysicalDeviceObject,
                                   NameInfo,
                                   NameLength,
                                   &NameLength);
        if (!NT_SUCCESS(Status))
        {
            ExFreePoolWithTag(NameInfo, TAG_IO_NAME);
            return Status;
        }

        DeviceName = NameInfo->Name;

        Status = IoCreateSymbolicLink(SymbolicLinkName, &DeviceName);

        ExFreePoolWithTag(NameInfo, TAG_IO_NAME);

        if (!NT_SUCCESS(Status))
        {
            DPRINT("PnP: the interface link %wZ was not made (%lx)\n",
                   SymbolicLinkName,
                   Status);
        }

        PiNotifyDeviceInterfaceChange(&GUID_DEVICE_INTERFACE_ARRIVAL,
                                      &Interface->ClassGuid,
                                      SymbolicLinkName);

        return STATUS_SUCCESS;
    }

    IoDeleteSymbolicLink(SymbolicLinkName);

    PiNotifyDeviceInterfaceChange(&GUID_DEVICE_INTERFACE_REMOVAL,
                                  &Interface->ClassGuid,
                                  SymbolicLinkName);

    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
IoGetDeviceInterfaces(
    _In_ CONST GUID *InterfaceClassGuid,
    _In_opt_ PDEVICE_OBJECT PhysicalDeviceObject,
    _In_ ULONG Flags,
    _Outptr_ PZZWSTR *SymbolicLinkList)
{
    PIOP_DEVICE_INTERFACE Interface;
    PLIST_ENTRY Entry;
    ULONG Length = sizeof(WCHAR);
    PWCHAR Cursor;
    PWCHAR List;

    PAGED_CODE();

    *SymbolicLinkList = NULL;

    KeAcquireGuardedMutex(&IopDeviceInterfaceLock);

    Entry = IopDeviceInterfaceListHead.Flink;
    while (Entry != &IopDeviceInterfaceListHead)
    {
        Interface = CONTAINING_RECORD(Entry,
                                      IOP_DEVICE_INTERFACE,
                                      ListEntry);
        Entry = Entry->Flink;

        if (RtlCompareMemory(&Interface->ClassGuid,
                             InterfaceClassGuid,
                             sizeof(GUID)) != sizeof(GUID))
        {
            continue;
        }

        if (PhysicalDeviceObject != NULL &&
            Interface->PhysicalDeviceObject != PhysicalDeviceObject)
        {
            continue;
        }

        if (!Interface->Enabled &&
            !(Flags & DEVICE_INTERFACE_INCLUDE_NONACTIVE))
        {
            continue;
        }

        Length += Interface->SymbolicLinkName.Length + sizeof(WCHAR);
    }

    List = ExAllocatePoolZero(PagedPool, Length, TAG_IO_NAME);
    if (List == NULL)
    {
        KeReleaseGuardedMutex(&IopDeviceInterfaceLock);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    Cursor = List;

    Entry = IopDeviceInterfaceListHead.Flink;
    while (Entry != &IopDeviceInterfaceListHead)
    {
        Interface = CONTAINING_RECORD(Entry,
                                      IOP_DEVICE_INTERFACE,
                                      ListEntry);
        Entry = Entry->Flink;

        if (RtlCompareMemory(&Interface->ClassGuid,
                             InterfaceClassGuid,
                             sizeof(GUID)) != sizeof(GUID))
        {
            continue;
        }

        if (PhysicalDeviceObject != NULL &&
            Interface->PhysicalDeviceObject != PhysicalDeviceObject)
        {
            continue;
        }

        if (!Interface->Enabled &&
            !(Flags & DEVICE_INTERFACE_INCLUDE_NONACTIVE))
        {
            continue;
        }

        RtlCopyMemory(Cursor,
                      Interface->SymbolicLinkName.Buffer,
                      Interface->SymbolicLinkName.Length);

        Cursor += Interface->SymbolicLinkName.Length / sizeof(WCHAR);
        *Cursor = UNICODE_NULL;
        Cursor++;
    }

    *Cursor = UNICODE_NULL;

    KeReleaseGuardedMutex(&IopDeviceInterfaceLock);

    *SymbolicLinkList = List;

    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
IoGetDeviceInterfaceAlias(
    _In_ PUNICODE_STRING SymbolicLinkName,
    _In_ CONST GUID *AliasInterfaceClassGuid,
    _Out_ PUNICODE_STRING AliasSymbolicLinkName)
{
    PIOP_DEVICE_INTERFACE Interface;
    PDEVICE_NODE DeviceNode;
    NTSTATUS Status;

    PAGED_CODE();

    RtlInitEmptyUnicodeString(AliasSymbolicLinkName, NULL, 0);

    KeAcquireGuardedMutex(&IopDeviceInterfaceLock);

    Interface = IopFindInterface(SymbolicLinkName);
    if (Interface == NULL)
    {
        KeReleaseGuardedMutex(&IopDeviceInterfaceLock);
        return STATUS_OBJECT_NAME_NOT_FOUND;
    }

    DeviceNode = IopGetDeviceNode(Interface->PhysicalDeviceObject);
    if (DeviceNode == NULL)
    {
        KeReleaseGuardedMutex(&IopDeviceInterfaceLock);
        return STATUS_INVALID_DEVICE_REQUEST;
    }

    Status = IopBuildInterfaceName(&DeviceNode->InstancePath,
                                   AliasInterfaceClassGuid,
                                   NULL,
                                   FALSE,
                                   AliasSymbolicLinkName);

    KeReleaseGuardedMutex(&IopDeviceInterfaceLock);

    return Status;
}

NTSTATUS
NTAPI
IoOpenDeviceInterfaceRegistryKey(
    _In_ PUNICODE_STRING SymbolicLinkName,
    _In_ ACCESS_MASK DesiredAccess,
    _Out_ PHANDLE DeviceInterfaceKey)
{
    PIOP_DEVICE_INTERFACE Interface;
    UNICODE_STRING RegistryName;
    UNICODE_STRING DeviceParams;
    PDEVICE_NODE DeviceNode;
    NTSTATUS Status;
    HANDLE ClassKey;
    HANDLE InstanceKey;

    PAGED_CODE();

    *DeviceInterfaceKey = NULL;

    KeAcquireGuardedMutex(&IopDeviceInterfaceLock);

    Interface = IopFindInterface(SymbolicLinkName);
    if (Interface == NULL)
    {
        KeReleaseGuardedMutex(&IopDeviceInterfaceLock);
        return STATUS_OBJECT_NAME_NOT_FOUND;
    }

    DeviceNode = IopGetDeviceNode(Interface->PhysicalDeviceObject);
    if (DeviceNode == NULL)
    {
        KeReleaseGuardedMutex(&IopDeviceInterfaceLock);
        return STATUS_INVALID_DEVICE_REQUEST;
    }

    Status = IopBuildInterfaceName(&DeviceNode->InstancePath,
                                   &Interface->ClassGuid,
                                   NULL,
                                   TRUE,
                                   &RegistryName);

    KeReleaseGuardedMutex(&IopDeviceInterfaceLock);

    if (!NT_SUCCESS(Status)) return Status;

    Status = IopOpenInterfaceClassKey(&Interface->ClassGuid,
                                      FALSE,
                                      &ClassKey);
    if (!NT_SUCCESS(Status))
    {
        ExFreePoolWithTag(RegistryName.Buffer, TAG_IO_NAME);
        return Status;
    }

    Status = IopOpenRegistryKeyEx(&InstanceKey,
                                  ClassKey,
                                  &RegistryName,
                                  KEY_READ);
    ZwClose(ClassKey);
    ExFreePoolWithTag(RegistryName.Buffer, TAG_IO_NAME);

    if (!NT_SUCCESS(Status)) return Status;

    RtlInitUnicodeString(&DeviceParams, L"#\\Device Parameters");

    Status = IopCreateRegistryKeyEx(DeviceInterfaceKey,
                                    InstanceKey,
                                    &DeviceParams,
                                    DesiredAccess,
                                    REG_OPTION_NON_VOLATILE,
                                    NULL);

    ZwClose(InstanceKey);

    return Status;
}
