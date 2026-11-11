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

    /* In the registry the reference string names a subkey, not part of this key */
    if (!RegistryForm && ReferenceString != NULL && ReferenceString->Length != 0)
    {
        RtlAppendUnicodeToString(Name, L"\\");
        RtlAppendUnicodeStringToString(Name, ReferenceString);
    }

    RtlFreeUnicodeString(&GuidString);

    return STATUS_SUCCESS;
}

/*
 * The subkey of an interface's key that belongs to one reference string: the
 * string behind a '#', or the '#' alone when there is none. A suffix may be
 * appended to reach a key below it.
 */
static
NTSTATUS
IopBuildReferenceKeyName(
    _In_opt_ PCUNICODE_STRING ReferenceString,
    _In_opt_ PCWSTR Suffix,
    _Out_ PUNICODE_STRING Name)
{
    USHORT Length;
    PWCHAR Buffer;

    Length = (USHORT)sizeof(WCHAR) +
             ((ReferenceString != NULL) ? ReferenceString->Length : 0) +
             (USHORT)((Suffix != NULL) ? wcslen(Suffix) * sizeof(WCHAR) : 0) +
             (USHORT)sizeof(WCHAR);

    Buffer = ExAllocatePoolZero(PagedPool, Length, TAG_IO_NAME);
    if (Buffer == NULL) return STATUS_INSUFFICIENT_RESOURCES;

    RtlInitEmptyUnicodeString(Name, Buffer, Length);
    RtlAppendUnicodeToString(Name, L"#");
    if (ReferenceString != NULL) RtlAppendUnicodeStringToString(Name, ReferenceString);
    if (Suffix != NULL) RtlAppendUnicodeToString(Name, Suffix);

    return STATUS_SUCCESS;
}

/* The reference string a symbolic link carries after its class identifier */
static
VOID
IopReferenceFromLinkName(
    _In_ PCUNICODE_STRING SymbolicLinkName,
    _Out_ PUNICODE_STRING ReferenceString)
{
    USHORT Count = SymbolicLinkName->Length / sizeof(WCHAR);
    USHORT Index;

    RtlInitEmptyUnicodeString(ReferenceString, NULL, 0);

    for (Index = Count; Index != 0; Index--)
    {
        if (SymbolicLinkName->Buffer[Index - 1] == L'}') break;
    }

    if (Index == 0 || Index >= Count || SymbolicLinkName->Buffer[Index] != OBJ_NAME_PATH_SEPARATOR) return;

    ReferenceString->Buffer = &SymbolicLinkName->Buffer[Index + 1];
    ReferenceString->Length = (USHORT)((Count - Index - 1) * sizeof(WCHAR));
    ReferenceString->MaximumLength = ReferenceString->Length;
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

VOID
IopSetInterfaceLinked(
    _In_ PIOP_DEVICE_INTERFACE Interface,
    _In_ PDEVICE_OBJECT PhysicalDeviceObject,
    _In_ BOOLEAN Linked)
{
    UNICODE_STRING LinkedName = RTL_CONSTANT_STRING(L"Linked");
    UNICODE_STRING RegistryName;
    UNICODE_STRING ControlName;
    UNICODE_STRING Reference;
    PDEVICE_NODE DeviceNode;
    ULONG Value = Linked;
    HANDLE InstanceKey;
    HANDLE ControlKey;
    HANDLE ClassKey;
    NTSTATUS Status;

    PAGED_CODE();

    DeviceNode = IopGetDeviceNode(PhysicalDeviceObject);
    if (DeviceNode == NULL) return;

    Status = IopBuildInterfaceName(&DeviceNode->InstancePath,
                                   &Interface->ClassGuid,
                                   NULL,
                                   TRUE,
                                   &RegistryName);
    if (!NT_SUCCESS(Status)) return;

    Status = IopOpenInterfaceClassKey(&Interface->ClassGuid, FALSE, &ClassKey);
    if (NT_SUCCESS(Status))
    {
        Status = IopOpenRegistryKeyEx(&InstanceKey, ClassKey, &RegistryName, KEY_READ);
        ZwClose(ClassKey);
    }
    ExFreePoolWithTag(RegistryName.Buffer, TAG_IO_NAME);
    if (!NT_SUCCESS(Status)) return;

    IopReferenceFromLinkName(&Interface->SymbolicLinkName, &Reference);

    Status = IopBuildReferenceKeyName(&Reference, L"\\Control", &ControlName);
    if (NT_SUCCESS(Status))
    {
        Status = IopCreateRegistryKeyEx(&ControlKey,
                                        InstanceKey,
                                        &ControlName,
                                        KEY_SET_VALUE,
                                        REG_OPTION_VOLATILE,
                                        NULL);
        ExFreePoolWithTag(ControlName.Buffer, TAG_IO_NAME);
    }
    ZwClose(InstanceKey);
    if (!NT_SUCCESS(Status)) return;

    ZwSetValueKey(ControlKey, &LinkedName, 0, REG_DWORD, &Value, sizeof(Value));
    ZwClose(ControlKey);
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
    PIOP_DEVICE_INTERFACE NewInterface;
    PIOP_DEVICE_INTERFACE Interface;
    PDEVICE_OBJECT Previous = NULL;
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

    Status = IopBuildReferenceKeyName(ReferenceString, NULL, &ValueName);
    if (NT_SUCCESS(Status))
    {
        Status = IopCreateRegistryKeyEx(&ControlKey,
                                        InstanceKey,
                                        &ValueName,
                                        KEY_ALL_ACCESS,
                                        REG_OPTION_NON_VOLATILE,
                                        NULL);
        ExFreePoolWithTag(ValueName.Buffer, TAG_IO_NAME);
    }

    if (NT_SUCCESS(Status))
    {
        RtlInitUnicodeString(&ValueName, L"SymbolicLink");

        /* The registry keeps the link in its user-mode form, \\?\ */
        LinkName.Buffer[1] = OBJ_NAME_PATH_SEPARATOR;
        ZwSetValueKey(ControlKey,
                      &ValueName,
                      0,
                      REG_SZ,
                      LinkName.Buffer,
                      LinkName.Length + sizeof(WCHAR));
        LinkName.Buffer[1] = L'?';

        ZwClose(ControlKey);
    }

    ZwClose(InstanceKey);

    NewInterface = ExAllocatePoolZero(NonPagedPool,
                                      sizeof(IOP_DEVICE_INTERFACE),
                                      TAG_IO_NAME);
    if (NewInterface == NULL)
    {
        ExFreePoolWithTag(LinkName.Buffer, TAG_IO_NAME);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    ObReferenceObject(PhysicalDeviceObject);

    /* A device object made again for the same instance takes over the interface its predecessor registered */
    KeAcquireGuardedMutex(&IopDeviceInterfaceLock);
    Interface = IopFindInterface(&LinkName);
    if (Interface != NULL)
    {
        Previous = Interface->PhysicalDeviceObject;
        Interface->PhysicalDeviceObject = PhysicalDeviceObject;
    }
    else
    {
        NewInterface->SymbolicLinkName = LinkName;
        NewInterface->ClassGuid = *InterfaceClassGuid;
        NewInterface->PhysicalDeviceObject = PhysicalDeviceObject;
        NewInterface->Enabled = FALSE;
        InsertTailList(&IopDeviceInterfaceListHead, &NewInterface->ListEntry);
    }
    KeReleaseGuardedMutex(&IopDeviceInterfaceLock);

    if (Interface != NULL)
    {
        ObDereferenceObject(Previous);
        ExFreePoolWithTag(NewInterface, TAG_IO_NAME);
    }

    Buffer = ExAllocatePoolZero(PagedPool,
                                LinkName.Length + sizeof(WCHAR),
                                TAG_IO_NAME);
    if (Buffer == NULL)
    {
        if (Interface != NULL) ExFreePoolWithTag(LinkName.Buffer, TAG_IO_NAME);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    RtlCopyMemory(Buffer, LinkName.Buffer, LinkName.Length);
    if (Interface != NULL) ExFreePoolWithTag(LinkName.Buffer, TAG_IO_NAME);

    SymbolicLinkName->Buffer = Buffer;
    SymbolicLinkName->Length = LinkName.Length;
    SymbolicLinkName->MaximumLength = LinkName.Length + sizeof(WCHAR);

    return STATUS_SUCCESS;
}

static
NTSTATUS
IopLinkDeviceInterface(
    _In_ PIOP_DEVICE_INTERFACE Interface,
    _In_ PDEVICE_OBJECT PhysicalDeviceObject,
    _In_ PUNICODE_STRING SymbolicLinkName)
{
    POBJECT_NAME_INFORMATION NameInfo;
    UNICODE_STRING DeviceName;
    ULONG NameLength = 0;
    NTSTATUS Status;

    PAGED_CODE();

    if (IopGetDeviceNode(PhysicalDeviceObject) == NULL) return STATUS_INVALID_DEVICE_REQUEST;

    Status = ObQueryNameString(PhysicalDeviceObject,
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

    Status = ObQueryNameString(PhysicalDeviceObject,
                               NameInfo,
                               NameLength,
                               &NameLength);
    if (!NT_SUCCESS(Status))
    {
        ExFreePoolWithTag(NameInfo, TAG_IO_NAME);
        return Status;
    }

    DeviceName = NameInfo->Name;

    IoCreateSymbolicLink(SymbolicLinkName, &DeviceName);

    ExFreePoolWithTag(NameInfo, TAG_IO_NAME);

    IopSetInterfaceLinked(Interface, PhysicalDeviceObject, TRUE);

    PiNotifyDeviceInterfaceChange(&GUID_DEVICE_INTERFACE_ARRIVAL,
                                  &Interface->ClassGuid,
                                  SymbolicLinkName);

    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
IoSetDeviceInterfaceState(
    _In_ PUNICODE_STRING SymbolicLinkName,
    _In_ BOOLEAN Enable)
{
    PIOP_DEVICE_INTERFACE Interface;
    PDEVICE_OBJECT PhysicalDeviceObject;
    NTSTATUS Status = STATUS_SUCCESS;

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

    PhysicalDeviceObject = Interface->PhysicalDeviceObject;
    ObReferenceObject(PhysicalDeviceObject);

    KeReleaseGuardedMutex(&IopDeviceInterfaceLock);

    if (Enable)
    {
        Status = IopLinkDeviceInterface(Interface, PhysicalDeviceObject, SymbolicLinkName);
    }
    else
    {
        IoDeleteSymbolicLink(SymbolicLinkName);

        IopSetInterfaceLinked(Interface, PhysicalDeviceObject, FALSE);

        PiNotifyDeviceInterfaceChange(&GUID_DEVICE_INTERFACE_REMOVAL,
                                      &Interface->ClassGuid,
                                      SymbolicLinkName);
    }

    ObDereferenceObject(PhysicalDeviceObject);

    return Status;
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
    UNICODE_STRING Reference;
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

    IopReferenceFromLinkName(SymbolicLinkName, &Reference);

    Status = IopBuildReferenceKeyName(&Reference, L"\\Device Parameters", &DeviceParams);
    if (NT_SUCCESS(Status))
    {
        Status = IopCreateRegistryKeyEx(DeviceInterfaceKey,
                                        InstanceKey,
                                        &DeviceParams,
                                        DesiredAccess,
                                        REG_OPTION_NON_VOLATILE,
                                        NULL);
        ExFreePoolWithTag(DeviceParams.Buffer, TAG_IO_NAME);
    }

    ZwClose(InstanceKey);

    return Status;
}
