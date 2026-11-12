/*
 * PROJECT:     LiberNT kernel-mode tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     IoGetDeviceInterfaces lists the present interfaces of a class in the order they were enabled, an unchanged state is reported, the instances of a device share the link named without their reference strings, and an alias keeps the reference string
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <kmt_test.h>
#include <wdmguid.h>

/* ok_eq_hex evaluates its value twice: a call with an effect goes through this once */
#define ok_once(call, expected) \
    do { NTSTATUS Once_ = (call); ok(Once_ == (expected), #call " = 0x%08lx, expected 0x%08lx\n", Once_, (NTSTATUS)(expected)); } while (0)

#define RESULT_KEY L"\\Registry\\Machine\\SOFTWARE\\KmtIoInterfaceOrder"

static const GUID TestInterface = {0x8f6d2a43, 0x61c4, 0x4bd8, {0x9a, 0x35, 0x2b, 0x9e, 0x0f, 0x5c, 0x7d, 0x11}};
static const GUID AliasInterface = {0x3b0e6c21, 0x9a47, 0x4f15, {0x8d, 0x2e, 0x6c, 0x1f, 0x0b, 0x7a, 0x9e, 0x52}};
static const DEVPROPKEY TestProperty = {{0x6a1d3c58, 0x2e4f, 0x4b91, {0x8c, 0x07, 0x5d, 0x3e, 0x9a, 0x41, 0xf2, 0x6b}}, 2};
static const GUID NotifyInterface = {0x9c4e1b27, 0x5d83, 0x4a6f, {0xb1, 0x2e, 0x7f, 0x05, 0xc9, 0x3a, 0x68, 0xd4}};

typedef struct _NOTIFY_STATE
{
    PUNICODE_STRING Link;
    LONG Depth;
    LONG MaxDepth;
    ULONG Count;
    GUID Events[4];
    NTSTATUS Changed[3];
    KEVENT Done;
} NOTIFY_STATE, *PNOTIFY_STATE;

/* The first arrival disables, enables and disables the interface again from inside the notification */
static
NTSTATUS
NTAPI
InterfaceNotified(
    _In_ PVOID NotificationStructure,
    _Inout_opt_ PVOID Context)
{
    PDEVICE_INTERFACE_CHANGE_NOTIFICATION Notification = NotificationStructure;
    PNOTIFY_STATE State = Context;
    LONG Depth;

    if (State == NULL || !RtlEqualUnicodeString(Notification->SymbolicLinkName, State->Link, TRUE)) return STATUS_SUCCESS;

    Depth = InterlockedIncrement(&State->Depth);
    if (Depth > State->MaxDepth) State->MaxDepth = Depth;
    if (State->Count < RTL_NUMBER_OF(State->Events)) State->Events[State->Count] = Notification->Event;
    State->Count++;

    if (State->Count == 1 && IsEqualGUID(&Notification->Event, &GUID_DEVICE_INTERFACE_ARRIVAL))
    {
        State->Changed[0] = IoSetDeviceInterfaceState(State->Link, FALSE);
        State->Changed[1] = IoSetDeviceInterfaceState(State->Link, TRUE);
        State->Changed[2] = IoSetDeviceInterfaceState(State->Link, FALSE);
    }

    InterlockedDecrement(&State->Depth);
    if (State->Count == RTL_NUMBER_OF(State->Events)) KeSetEvent(&State->Done, IO_NO_INCREMENT, FALSE);
    return STATUS_SUCCESS;
}

static
BOOLEAN
ReadDeviceName(
    _Out_writes_(Chars) PWCHAR Name,
    _In_ ULONG Chars)
{
    UNICODE_STRING KeyName = RTL_CONSTANT_STRING(RESULT_KEY);
    UNICODE_STRING ValueName = RTL_CONSTANT_STRING(L"Pdo");
    UCHAR Buffer[sizeof(KEY_VALUE_PARTIAL_INFORMATION) + 128 * sizeof(WCHAR)];
    PKEY_VALUE_PARTIAL_INFORMATION Value = (PKEY_VALUE_PARTIAL_INFORMATION)Buffer;
    OBJECT_ATTRIBUTES ObjectAttributes;
    ULONG Length;
    NTSTATUS Status;
    HANDLE Key;

    InitializeObjectAttributes(&ObjectAttributes, &KeyName, OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, NULL, NULL);
    Status = ZwOpenKey(&Key, KEY_QUERY_VALUE, &ObjectAttributes);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status)) return FALSE;

    RtlZeroMemory(Buffer, sizeof(Buffer));
    Status = ZwQueryValueKey(Key, &ValueName, KeyValuePartialInformation, Value, sizeof(Buffer) - sizeof(WCHAR), &Length);
    ZwClose(Key);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status) || Value->Type != REG_SZ || Value->DataLength >= Chars * sizeof(WCHAR)) return FALSE;

    RtlCopyMemory(Name, Value->Data, Value->DataLength);
    Name[Value->DataLength / sizeof(WCHAR)] = UNICODE_NULL;
    return TRUE;
}

/* A device object cannot be referenced by name without an open, so the root bus's own list is searched */
static
PDEVICE_OBJECT
ReferenceRootDevice(
    _In_ PCUNICODE_STRING Name)
{
    UNICODE_STRING RootName = RTL_CONSTANT_STRING(L"\\Driver\\PnpManager");
    UCHAR Buffer[sizeof(OBJECT_NAME_INFORMATION) + 128 * sizeof(WCHAR)];
    POBJECT_NAME_INFORMATION NameInfo = (POBJECT_NAME_INFORMATION)Buffer;
    PDEVICE_OBJECT Found = NULL;
    PDEVICE_OBJECT *List = NULL;
    PDRIVER_OBJECT RootDriver;
    ULONG Count = 0, Index, Length;
    NTSTATUS Status;

    Status = ObReferenceObjectByName(&RootName, OBJ_CASE_INSENSITIVE, NULL, 0, IoDriverObjectType, KernelMode, NULL,
                                     (PVOID *)&RootDriver);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status)) return NULL;

    Status = IoEnumerateDeviceObjectList(RootDriver, NULL, 0, &Count);
    if (Status == STATUS_BUFFER_TOO_SMALL && Count != 0)
    {
        List = ExAllocatePoolWithTag(NonPagedPool, Count * sizeof(PDEVICE_OBJECT), 'kDtK');
        if (List != NULL) Status = IoEnumerateDeviceObjectList(RootDriver, List, Count * sizeof(PDEVICE_OBJECT), &Count);
    }

    for (Index = 0; List != NULL && NT_SUCCESS(Status) && Index < Count; Index++)
    {
        if (Found == NULL &&
            NT_SUCCESS(ObQueryNameString(List[Index], NameInfo, sizeof(Buffer), &Length)) &&
            RtlEqualUnicodeString(&NameInfo->Name, Name, TRUE))
        {
            Found = List[Index];
            ObReferenceObject(Found);
        }

        ObDereferenceObject(List[Index]);
    }

    if (List != NULL) ExFreePoolWithTag(List, 'kDtK');
    ObDereferenceObject(RootDriver);

    ok(Found != NULL, "no root device object is named %wZ\n", Name);
    return Found;
}

/* An interface path is the link to the device followed by the reference string, which reaches the driver as the file name */
static
NTSTATUS
LinkStatus(
    _In_ PCUNICODE_STRING Interface,
    _In_ PCUNICODE_STRING Reference,
    _In_ PCUNICODE_STRING DeviceName)
{
    OBJECT_ATTRIBUTES ObjectAttributes;
    UNICODE_STRING Suffix;
    UNICODE_STRING Target;
    UNICODE_STRING Link;
    WCHAR Buffer[128];
    NTSTATUS Status;
    HANDLE Handle;

    if (Interface->Length <= Reference->Length + sizeof(WCHAR)) return STATUS_OBJECT_NAME_INVALID;

    Link = *Interface;
    Link.Length = Interface->Length - Reference->Length - sizeof(WCHAR);
    Suffix.Buffer = &Interface->Buffer[Link.Length / sizeof(WCHAR) + 1];
    Suffix.Length = Suffix.MaximumLength = Reference->Length;
    ok(Interface->Buffer[Link.Length / sizeof(WCHAR)] == OBJ_NAME_PATH_SEPARATOR && RtlEqualUnicodeString(&Suffix, Reference, FALSE),
       "%wZ does not end with its reference string %wZ\n", Interface, Reference);

    InitializeObjectAttributes(&ObjectAttributes, &Link, OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, NULL, NULL);
    Status = ZwOpenSymbolicLinkObject(&Handle, SYMBOLIC_LINK_QUERY, &ObjectAttributes);
    if (!NT_SUCCESS(Status)) return Status;

    RtlInitEmptyUnicodeString(&Target, Buffer, sizeof(Buffer));
    Status = ZwQuerySymbolicLinkObject(Handle, &Target, NULL);
    ZwClose(Handle);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (NT_SUCCESS(Status))
        ok(RtlEqualUnicodeString(&Target, DeviceName, TRUE), "%wZ links to %wZ, expected %wZ\n", &Link, &Target, DeviceName);

    return Status;
}

static
VOID
CheckParametersKey(
    _In_ PUNICODE_STRING Interface)
{
    UNICODE_STRING Expected = RTL_CONSTANT_STRING(L"\\#{C}\\Device Parameters");
    UCHAR Buffer[sizeof(KEY_NAME_INFORMATION) + 512 * sizeof(WCHAR)];
    PKEY_NAME_INFORMATION Info = (PKEY_NAME_INFORMATION)Buffer;
    UNICODE_STRING Name;
    ULONG Length;
    NTSTATUS Status;
    HANDLE Key;

    Status = IoOpenDeviceInterfaceRegistryKey(Interface, KEY_READ, &Key);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status)) return;

    Status = ZwQueryKey(Key, KeyNameInformation, Info, sizeof(Buffer), &Length);
    ZwClose(Key);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status) || Info->NameLength < Expected.Length) return;

    Name.Buffer = &Info->Name[(Info->NameLength - Expected.Length) / sizeof(WCHAR)];
    Name.Length = Name.MaximumLength = Expected.Length;
    ok(RtlEqualUnicodeString(&Name, &Expected, TRUE), "the parameters key of %wZ ends with %wZ\n", Interface, &Name);
}

START_TEST(IoInterfaceOrderCheck)
{
    UNICODE_STRING ReferenceA = RTL_CONSTANT_STRING(L"A");
    UNICODE_STRING ReferenceB = RTL_CONSTANT_STRING(L"B");
    UNICODE_STRING ReferenceC = RTL_CONSTANT_STRING(L"{C}");
    UNICODE_STRING ReferenceN = RTL_CONSTANT_STRING(L"N");
    UNICODE_STRING Notified = { 0 };
    LARGE_INTEGER Timeout;
    NOTIFY_STATE State;
    PVOID NotifyEntry = NULL;
    UNICODE_STRING Backslash = RTL_CONSTANT_STRING(L"a\\b");
    UNICODE_STRING Slash = RTL_CONSTANT_STRING(L"a/b");
    UNICODE_STRING Again;
    UNICODE_STRING First = { 0 };
    UNICODE_STRING Second = { 0 };
    UNICODE_STRING Third = { 0 };
    UNICODE_STRING AliasA = { 0 };
    UNICODE_STRING Alias;
    DEVPROPTYPE Type;
    ULONG Required;
    ULONG Value;
    UNICODE_STRING Entry;
    UNICODE_STRING DeviceName;
    PDEVICE_OBJECT Pdo;
    WCHAR Name[128];
    PZZWSTR List;
    NTSTATUS Status;

    if (!ReadDeviceName(Name, RTL_NUMBER_OF(Name))) return;

    RtlInitUnicodeString(&DeviceName, Name);
    Pdo = ReferenceRootDevice(&DeviceName);
    if (Pdo == NULL) return;

    Status = IoRegisterDeviceInterface(Pdo, &TestInterface, &ReferenceA, &First);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (NT_SUCCESS(Status))
    {
        /* The interface's properties hang from its class, the GUID that ends its device's link */
        Value = 0x5a5a;
        ok_once(IoSetDeviceInterfacePropertyData(&First, &TestProperty, 0, 0, DEVPROP_TYPE_UINT32, sizeof(Value), &Value),
                STATUS_SUCCESS);
        Value = 0;
        ok_once(IoGetDeviceInterfacePropertyData(&First, &TestProperty, 0, 0, sizeof(Value), &Value, &Required, &Type),
                STATUS_SUCCESS);
        ok_eq_ulong(Value, 0x5a5aUL);
    }
    if (NT_SUCCESS(Status))
    {
        /* A second registration of an instance gives its name and says it existed */
        ok_once(IoRegisterDeviceInterface(Pdo, &TestInterface, &ReferenceA, &Again), STATUS_OBJECT_NAME_EXISTS);
        ok(RtlEqualUnicodeString(&Again, &First, TRUE), "registered again as %wZ, expected %wZ\n", &Again, &First);
        RtlFreeUnicodeString(&Again);
    }

    /* A reference string is one path component */
    ok_once(IoRegisterDeviceInterface(Pdo, &TestInterface, &Backslash, &Again), STATUS_INVALID_DEVICE_REQUEST);
    RtlFreeUnicodeString(&Again);
    ok_once(IoRegisterDeviceInterface(Pdo, &TestInterface, &Slash, &Again), STATUS_INVALID_DEVICE_REQUEST);
    RtlFreeUnicodeString(&Again);

    if (NT_SUCCESS(Status)) Status = IoRegisterDeviceInterface(Pdo, &TestInterface, &ReferenceB, &Second);
    ok_eq_hex(Status, STATUS_SUCCESS);

    if (NT_SUCCESS(Status))
    {
        ok_once(IoSetDeviceInterfaceState(&Second, TRUE), STATUS_SUCCESS);
        ok_once(IoSetDeviceInterfaceState(&First, TRUE), STATUS_SUCCESS);
        ok_once(IoSetDeviceInterfaceState(&First, TRUE), STATUS_OBJECT_NAME_EXISTS);
        ok_once(LinkStatus(&First, &ReferenceA, &DeviceName), STATUS_SUCCESS);

        Status = IoGetDeviceInterfaces(&TestInterface, NULL, 0, &List);
        ok_eq_hex(Status, STATUS_SUCCESS);
        if (NT_SUCCESS(Status))
        {
            RtlInitUnicodeString(&Entry, List);
            ok(RtlEqualUnicodeString(&Entry, &Second, TRUE), "first listed %wZ, expected %wZ\n", &Entry, &Second);
            if (Entry.Length != 0)
            {
                RtlInitUnicodeString(&Entry, List + Entry.Length / sizeof(WCHAR) + 1);
                ok(RtlEqualUnicodeString(&Entry, &First, TRUE), "second listed %wZ, expected %wZ\n", &Entry, &First);
            }
            ExFreePool(List);
        }

        ok_once(IoSetDeviceInterfaceState(&First, FALSE), STATUS_SUCCESS);
        ok_once(LinkStatus(&Second, &ReferenceB, &DeviceName), STATUS_SUCCESS);
        ok_once(IoSetDeviceInterfaceState(&First, FALSE), STATUS_OBJECT_NAME_NOT_FOUND);
        ok_once(IoSetDeviceInterfaceState(&Second, FALSE), STATUS_SUCCESS);
        ok_once(LinkStatus(&Second, &ReferenceB, &DeviceName), STATUS_OBJECT_NAME_NOT_FOUND);
    }

    Status = IoRegisterDeviceInterface(Pdo, &TestInterface, &ReferenceC, &Third);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (NT_SUCCESS(Status))
    {
        ok_once(IoSetDeviceInterfaceState(&Third, TRUE), STATUS_SUCCESS);
        ok_once(LinkStatus(&Third, &ReferenceC, &DeviceName), STATUS_SUCCESS);
        CheckParametersKey(&Third);
        ok_once(IoSetDeviceInterfaceState(&Third, FALSE), STATUS_SUCCESS);
        ok_once(LinkStatus(&Third, &ReferenceC, &DeviceName), STATUS_OBJECT_NAME_NOT_FOUND);
    }

    /* Instances of two classes on one device are aliases when their reference strings are the same */
    Status = IoRegisterDeviceInterface(Pdo, &AliasInterface, &ReferenceA, &AliasA);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (NT_SUCCESS(Status) && First.Buffer != NULL && Second.Buffer != NULL)
    {
        Status = IoGetDeviceInterfaceAlias(&First, &AliasInterface, &Alias);
        ok_eq_hex(Status, STATUS_SUCCESS);
        if (NT_SUCCESS(Status))
        {
            ok(RtlEqualUnicodeString(&Alias, &AliasA, TRUE), "alias %wZ, expected %wZ\n", &Alias, &AliasA);
            RtlFreeUnicodeString(&Alias);
        }

        Status = IoGetDeviceInterfaceAlias(&Second, &AliasInterface, &Alias);
        ok(Status == STATUS_OBJECT_NAME_NOT_FOUND || Status == STATUS_OBJECT_PATH_NOT_FOUND,
           "IoGetDeviceInterfaceAlias for reference string B returned 0x%lx\n", Status);
        if (NT_SUCCESS(Status)) RtlFreeUnicodeString(&Alias);
    }

    /* Each change of an interface is notified, in order, and never inside the notification of another */
    RtlZeroMemory(&State, sizeof(State));
    KeInitializeEvent(&State.Done, NotificationEvent, FALSE);
    State.Link = &Notified;
    Status = IoRegisterDeviceInterface(Pdo, &NotifyInterface, &ReferenceN, &Notified);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (NT_SUCCESS(Status))
    {
        Status = IoRegisterPlugPlayNotification(EventCategoryDeviceInterfaceChange, 0, (PVOID)&NotifyInterface,
                                                KmtDriverObject, InterfaceNotified, &State, &NotifyEntry);
        ok_eq_hex(Status, STATUS_SUCCESS);
    }
    if (NT_SUCCESS(Status))
    {
        ok_once(IoSetDeviceInterfaceState(&Notified, TRUE), STATUS_SUCCESS);
        Timeout.QuadPart = -10LL * 1000 * 1000;
        ok_once(KeWaitForSingleObject(&State.Done, Executive, KernelMode, FALSE, &Timeout), STATUS_SUCCESS);
        ok_eq_hex(State.Changed[0], STATUS_SUCCESS);
        ok_eq_hex(State.Changed[1], STATUS_SUCCESS);
        ok_eq_hex(State.Changed[2], STATUS_SUCCESS);
        ok_eq_long(State.MaxDepth, 1L);
        ok_eq_ulong(State.Count, 4UL);
        ok(IsEqualGUID(&State.Events[0], &GUID_DEVICE_INTERFACE_ARRIVAL) &&
           IsEqualGUID(&State.Events[1], &GUID_DEVICE_INTERFACE_REMOVAL) &&
           IsEqualGUID(&State.Events[2], &GUID_DEVICE_INTERFACE_ARRIVAL) &&
           IsEqualGUID(&State.Events[3], &GUID_DEVICE_INTERFACE_REMOVAL),
           "the notifications did not come as arrival, removal, arrival, removal\n");
        IoUnregisterPlugPlayNotification(NotifyEntry);
    }

    RtlFreeUnicodeString(&First);
    RtlFreeUnicodeString(&Second);
    RtlFreeUnicodeString(&Third);
    RtlFreeUnicodeString(&AliasA);
    RtlFreeUnicodeString(&Notified);
    ObDereferenceObject(Pdo);
}
