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
KMUTEX IopInterfaceLinkedLock;

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
    USHORT Index = (USHORT)(sizeof(L"\\??\\") / sizeof(WCHAR) - 1);

    RtlInitEmptyUnicodeString(ReferenceString, NULL, 0);

    /* A reference string holds no separator, and the instance part has '#' for its separators */
    while (Index < Count && SymbolicLinkName->Buffer[Index] != OBJ_NAME_PATH_SEPARATOR) Index++;

    if (Index >= Count) return;

    ReferenceString->Buffer = &SymbolicLinkName->Buffer[Index + 1];
    ReferenceString->Length = (USHORT)((Count - Index - 1) * sizeof(WCHAR));
    ReferenceString->MaximumLength = ReferenceString->Length;
}

/* The instances of one device and class share one link, named without their reference strings */
static
VOID
IopGetInterfaceLinkObjectName(
    _In_ PCUNICODE_STRING SymbolicLinkName,
    _Out_ PUNICODE_STRING LinkObjectName)
{
    UNICODE_STRING Reference;

    IopReferenceFromLinkName(SymbolicLinkName, &Reference);

    *LinkObjectName = *SymbolicLinkName;
    if (Reference.Buffer == NULL) return;

    LinkObjectName->Length = (USHORT)((Reference.Buffer - 1 - SymbolicLinkName->Buffer) * sizeof(WCHAR));
    LinkObjectName->MaximumLength = LinkObjectName->Length;
}

VOID
IopSplitInterfaceLink(
    _In_ PCUNICODE_STRING SymbolicLinkName,
    _Out_ PUNICODE_STRING LinkObjectName,
    _Out_ PUNICODE_STRING ReferenceString)
{
    IopGetInterfaceLinkObjectName(SymbolicLinkName, LinkObjectName);
    IopReferenceFromLinkName(SymbolicLinkName, ReferenceString);
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

static
BOOLEAN
IopIsInterfaceLinkShared(
    _In_ PIOP_DEVICE_INTERFACE Interface)
{
    PIOP_DEVICE_INTERFACE Other;
    UNICODE_STRING OtherName;
    UNICODE_STRING Name;
    PLIST_ENTRY Entry;

    ASSERT(IopDeviceInterfaceLock.Owner == KeGetCurrentThread());

    IopGetInterfaceLinkObjectName(&Interface->SymbolicLinkName, &Name);

    for (Entry = IopDeviceInterfaceListHead.Flink; Entry != &IopDeviceInterfaceListHead; Entry = Entry->Flink)
    {
        Other = CONTAINING_RECORD(Entry, IOP_DEVICE_INTERFACE, ListEntry);
        if (Other == Interface || !Other->Enabled) continue;
        if (Other->PhysicalDeviceObject != Interface->PhysicalDeviceObject) continue;

        IopGetInterfaceLinkObjectName(&Other->SymbolicLinkName, &OtherName);
        if (RtlEqualUnicodeString(&OtherName, &Name, TRUE)) return TRUE;
    }

    return FALSE;
}

VOID
IopDropInterfaceLink(
    _In_ PIOP_DEVICE_INTERFACE Interface)
{
    UNICODE_STRING Name;

    PAGED_CODE();
    ASSERT(IopDeviceInterfaceLock.Owner == KeGetCurrentThread());
    ASSERT(!Interface->Enabled);

    if (IopIsInterfaceLinkShared(Interface)) return;

    IopGetInterfaceLinkObjectName(&Interface->SymbolicLinkName, &Name);
    IoDeleteSymbolicLink(&Name);
}

/* Every change of an interface is announced once, in order, and never inside another announcement of it */
VOID
IopAnnounceInterface(
    _In_ PIOP_DEVICE_INTERFACE Interface)
{
    BOOLEAN Arrival;

    PAGED_CODE();

    KeAcquireGuardedMutex(&IopDeviceInterfaceLock);

    if (Interface->Announcing)
    {
        KeReleaseGuardedMutex(&IopDeviceInterfaceLock);
        return;
    }

    Interface->Announcing = TRUE;

    while (Interface->Unannounced != 0)
    {
        Interface->Unannounced--;
        Interface->Announced = !Interface->Announced;
        Arrival = Interface->Announced;

        KeReleaseGuardedMutex(&IopDeviceInterfaceLock);

        PiNotifyDeviceInterfaceChange(Arrival ? &GUID_DEVICE_INTERFACE_ARRIVAL : &GUID_DEVICE_INTERFACE_REMOVAL,
                                      &Interface->ClassGuid,
                                      &Interface->SymbolicLinkName);

        KeAcquireGuardedMutex(&IopDeviceInterfaceLock);
    }

    ASSERT(Interface->Announced == Interface->Enabled);
    Interface->Announcing = FALSE;

    KeReleaseGuardedMutex(&IopDeviceInterfaceLock);
}

/* A device that leaves the tree lets go of its interfaces: they stay registered without a device until it comes back */
VOID
IopForgetDeviceInterfaces(
    _In_ PDEVICE_OBJECT PhysicalDeviceObject)
{
    PIOP_DEVICE_INTERFACE Interface;
    PLIST_ENTRY Entry;
    ULONG Released = 0;

    PAGED_CODE();
    ASSERT(PhysicalDeviceObject != NULL);

    KeAcquireGuardedMutex(&IopDeviceInterfaceLock);

    for (Entry = IopDeviceInterfaceListHead.Flink; Entry != &IopDeviceInterfaceListHead; Entry = Entry->Flink)
    {
        Interface = CONTAINING_RECORD(Entry, IOP_DEVICE_INTERFACE, ListEntry);
        if (Interface->PhysicalDeviceObject != PhysicalDeviceObject) continue;

        /* The removal that comes before a device leaves the tree disabled its interfaces */
        ASSERT(!Interface->Enabled);
        if (Interface->Enabled) continue;

        Interface->PhysicalDeviceObject = NULL;
        Released++;
    }

    KeReleaseGuardedMutex(&IopDeviceInterfaceLock);

    while (Released != 0)
    {
        ObDereferenceObject(PhysicalDeviceObject);
        Released--;
    }
}

static
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

/* The registry is written outside the interface lock, where registry filters may call back into the PnP manager;
 * of changes written out of order, the one made last stays */
VOID
IopWriteInterfaceLinked(
    _In_ PIOP_DEVICE_INTERFACE Interface,
    _In_ PDEVICE_OBJECT PhysicalDeviceObject,
    _In_ BOOLEAN Linked,
    _In_ ULONG Toggle)
{
    PAGED_CODE();
    ASSERT(IopDeviceInterfaceLock.Owner != KeGetCurrentThread());

    KeWaitForSingleObject(&IopInterfaceLinkedLock, Executive, KernelMode, FALSE, NULL);

    if ((LONG)(Toggle - Interface->LinkedToggle) > 0)
    {
        IopSetInterfaceLinked(Interface, PhysicalDeviceObject, Linked);
        Interface->LinkedToggle = Toggle;
    }

    KeReleaseMutex(&IopInterfaceLinkedLock, FALSE);
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
    ULONG Disposition = 0;
    NTSTATUS Status;
    HANDLE ClassKey;
    HANDLE InstanceKey;
    HANDLE ControlKey;
    PWCHAR Buffer;
    USHORT Index;

    PAGED_CODE();

    RtlInitEmptyUnicodeString(SymbolicLinkName, NULL, 0);

    DeviceNode = IopGetDeviceNode(PhysicalDeviceObject);
    if (DeviceNode == NULL) return STATUS_INVALID_DEVICE_REQUEST;
    if (DeviceNode->InstancePath.Length == 0) return STATUS_INVALID_DEVICE_REQUEST;

    /* A reference string holds no path separator, '/' or '\' (IoRegisterDeviceInterface documentation) */
    for (Index = 0; ReferenceString != NULL && Index < ReferenceString->Length / sizeof(WCHAR); Index++)
    {
        if (ReferenceString->Buffer[Index] == OBJ_NAME_PATH_SEPARATOR || ReferenceString->Buffer[Index] == L'/')
            return STATUS_INVALID_DEVICE_REQUEST;
    }

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
                                        &Disposition);
        ExFreePoolWithTag(ValueName.Buffer, TAG_IO_NAME);
    }

    if (!NT_SUCCESS(Status))
    {
        ZwClose(InstanceKey);
        ExFreePoolWithTag(LinkName.Buffer, TAG_IO_NAME);
        return Status;
    }

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
        if (Previous != NULL) ObDereferenceObject(Previous);
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

    /* Registrations persist across boots: one the registry already holds is reported (IoRegisterDeviceInterface documentation) */
    return (Disposition == REG_OPENED_EXISTING_KEY) ? STATUS_OBJECT_NAME_EXISTS : STATUS_SUCCESS;
}

static
NTSTATUS
IopLinkDeviceInterface(
    _In_ PIOP_DEVICE_INTERFACE Interface)
{
    PDEVICE_OBJECT PhysicalDeviceObject = Interface->PhysicalDeviceObject;
    POBJECT_NAME_INFORMATION NameInfo;
    UNICODE_STRING LinkObjectName;
    ULONG NameLength = 0;
    NTSTATUS Status;

    PAGED_CODE();
    ASSERT(IopDeviceInterfaceLock.Owner == KeGetCurrentThread());
    ASSERT(!Interface->Enabled);

    if (IopGetDeviceNode(PhysicalDeviceObject) == NULL) return STATUS_INVALID_DEVICE_REQUEST;
    if (IopIsInterfaceLinkShared(Interface)) return STATUS_SUCCESS;

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

    IopGetInterfaceLinkObjectName(&Interface->SymbolicLinkName, &LinkObjectName);

    Status = IoCreateSymbolicLink(&LinkObjectName, &NameInfo->Name);

    ExFreePoolWithTag(NameInfo, TAG_IO_NAME);

    return Status;
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
    ULONG Toggle;

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
        return Enable ? STATUS_OBJECT_NAME_EXISTS : STATUS_OBJECT_NAME_NOT_FOUND;
    }

    if (Enable)
    {
        Status = IopLinkDeviceInterface(Interface);
        if (!NT_SUCCESS(Status))
        {
            KeReleaseGuardedMutex(&IopDeviceInterfaceLock);
            return Status;
        }

        /* Present interfaces are listed in the order they were enabled, as on Windows 11 */
        RemoveEntryList(&Interface->ListEntry);
        InsertTailList(&IopDeviceInterfaceListHead, &Interface->ListEntry);
    }

    Interface->Enabled = Enable;
    Interface->Unannounced++;
    Toggle = ++Interface->Toggles;
    if (!Enable) IopDropInterfaceLink(Interface);

    PhysicalDeviceObject = Interface->PhysicalDeviceObject;
    ObReferenceObject(PhysicalDeviceObject);

    KeReleaseGuardedMutex(&IopDeviceInterfaceLock);

    IopWriteInterfaceLinked(Interface, PhysicalDeviceObject, Enable, Toggle);
    IopAnnounceInterface(Interface);

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
    UNICODE_STRING Reference;
    UNICODE_STRING AliasName;
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

    /* The alias is the registered instance of the other class with the same device and reference string */
    IopReferenceFromLinkName(&Interface->SymbolicLinkName, &Reference);
    Status = IopBuildInterfaceName(&DeviceNode->InstancePath,
                                   AliasInterfaceClassGuid,
                                   &Reference,
                                   FALSE,
                                   &AliasName);

    /* TODO: an alias registered in an earlier boot and not again in this one is not found */
    if (NT_SUCCESS(Status) && IopFindInterface(&AliasName) == NULL)
    {
        ExFreePoolWithTag(AliasName.Buffer, TAG_IO_NAME);
        Status = STATUS_OBJECT_NAME_NOT_FOUND;
    }

    KeReleaseGuardedMutex(&IopDeviceInterfaceLock);

    if (NT_SUCCESS(Status)) *AliasSymbolicLinkName = AliasName;

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
