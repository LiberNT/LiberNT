/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Driver objects, their extensions and their coming and going
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#include "../ioint.h"
#define NDEBUG
#include <debug.h>

/* GLOBALS ********************************************************************/

LIST_ENTRY DriverReinitListHead;
KSPIN_LOCK DriverReinitListLock;
PLIST_ENTRY DriverReinitTailEntry = NULL;

LIST_ENTRY DriverBootReinitListHead;
KSPIN_LOCK DriverBootReinitListLock;
PLIST_ENTRY DriverBootReinitTailEntry = NULL;

/* PRIVATE DATA ***************************************************************/

#define IOP_DRIVER_LOAD_SLOTS 8

static KEVENT IopDriverLoadSlots[IOP_DRIVER_LOAD_SLOTS];

static const UNICODE_STRING IopDriverDirectory =
    RTL_CONSTANT_STRING(L"\\Driver");
static const UNICODE_STRING IopFileSystemDirectory =
    RTL_CONSTANT_STRING(L"\\FileSystem");

/* PRIVATE FUNCTIONS **********************************************************/

CODE_SEG("INIT")
VOID
IopInitDriverImplementation(VOID)
{
    InitializeListHead(&DriverReinitListHead);
    KeInitializeSpinLock(&DriverReinitListLock);

    InitializeListHead(&DriverBootReinitListHead);
    KeInitializeSpinLock(&DriverBootReinitListLock);
}

CODE_SEG("INIT")
VOID
IopInitDriverLoadSlots(VOID)
{
    ULONG Index;

    for (Index = 0; Index < IOP_DRIVER_LOAD_SLOTS; Index++)
    {
        KeInitializeEvent(&IopDriverLoadSlots[Index],
                          SynchronizationEvent,
                          TRUE);
    }
}

VOID
NTAPI
IopAcquirePnpUnloadBarrier(
    _In_ PDRIVER_OBJECT DriverObject)
{
    PEXTENDED_DRIVER_EXTENSION Extension = IoGetDrvObjExtension(DriverObject);

    InterlockedIncrement(&Extension->PnpUnloadBarrier);
}

VOID
NTAPI
IopReleasePnpUnloadBarrier(
    _In_ PDRIVER_OBJECT DriverObject)
{
    PEXTENDED_DRIVER_EXTENSION Extension = IoGetDrvObjExtension(DriverObject);

    if (InterlockedDecrement(&Extension->PnpUnloadBarrier) != 0) return;

    if (InterlockedExchange(&Extension->PnpUnloadPending, 0) == 0) return;

    IopTryUnloadDriver(DriverObject, FALSE);
}

VOID
NTAPI
IopDeleteDriver(
    IN PVOID ObjectBody)
{
    PDRIVER_OBJECT DriverObject = ObjectBody;
    PEXTENDED_DRIVER_EXTENSION Extension;
    PIO_CLIENT_EXTENSION Client;
    PIO_CLIENT_EXTENSION Next;

    PAGED_CODE();

    Extension = IoGetDrvObjExtension(DriverObject);

    Client = Extension->ClientDriverExtension;
    while (Client != NULL)
    {
        Next = Client->NextExtension;
        ExFreePoolWithTag(Client, TAG_DRIVER_EXTENSION);
        Client = Next;
    }

    if (Extension->ServiceKeyName.Buffer != NULL)
    {
        ExFreePool(Extension->ServiceKeyName.Buffer);
        Extension->ServiceKeyName.Buffer = NULL;
    }

    if (DriverObject->DriverName.Buffer != NULL)
    {
        ExFreePool(DriverObject->DriverName.Buffer);
        DriverObject->DriverName.Buffer = NULL;
    }

    /* The last thing the driver object holds is the image it was made from */
    if (DriverObject->DriverSection != NULL)
    {
        MmUnloadSystemImage(DriverObject->DriverSection);
        DriverObject->DriverSection = NULL;
    }
}

NTSTATUS
IopGetDriverNames(
    _In_ HANDLE ServiceHandle,
    _Out_ PUNICODE_STRING DriverName,
    _Out_opt_ PUNICODE_STRING ServiceName)
{
    PKEY_VALUE_FULL_INFORMATION Information = NULL;
    UNICODE_STRING Prefix;
    UNICODE_STRING Name;
    UNICODE_STRING Service;
    NTSTATUS Status;
    PWCHAR Buffer;
    ULONG Length;
    ULONG Type = 0;

    PAGED_CODE();

    RtlInitEmptyUnicodeString(&Service, NULL, 0);

    Status = IopGetRegistryValue(ServiceHandle, L"Type", &Information);
    if (NT_SUCCESS(Status))
    {
        if (Information->Type == REG_DWORD &&
            Information->DataLength == sizeof(ULONG))
        {
            Type = *(PULONG)((PUCHAR)Information + Information->DataOffset);
        }

        ExFreePool(Information);
        Information = NULL;
    }

    Prefix = (Type == SERVICE_FILE_SYSTEM_DRIVER ||
              Type == SERVICE_RECOGNIZER_DRIVER)
                 ? IopFileSystemDirectory
                 : IopDriverDirectory;

    Status = IopGetRegistryValue(ServiceHandle, L"ObjectName", &Information);
    if (NT_SUCCESS(Status))
    {
        if (Information->Type == REG_SZ && Information->DataLength > sizeof(WCHAR))
        {
            Name.Buffer = (PWCHAR)((PUCHAR)Information +
                                   Information->DataOffset);
            Name.Length = (USHORT)(Information->DataLength - sizeof(WCHAR));
            Name.MaximumLength = Name.Length;

            Buffer = ExAllocatePoolZero(NonPagedPool,
                                        Name.Length + sizeof(WCHAR),
                                        TAG_IO_DRIVER_NAME);
            if (Buffer == NULL)
            {
                ExFreePool(Information);
                return STATUS_INSUFFICIENT_RESOURCES;
            }

            RtlCopyMemory(Buffer, Name.Buffer, Name.Length);

            DriverName->Buffer = Buffer;
            DriverName->Length = Name.Length;
            DriverName->MaximumLength = Name.Length + sizeof(WCHAR);

            ExFreePool(Information);

            if (ServiceName != NULL)
            {
                *ServiceName = Service;
            }

            return STATUS_SUCCESS;
        }

        ExFreePool(Information);
        Information = NULL;
    }

    Status = IopGetRegistryValue(ServiceHandle, L"ImagePath", &Information);
    if (Information != NULL)
    {
        ExFreePool(Information);
        Information = NULL;
    }

    Status = ZwQueryKey(ServiceHandle, KeyBasicInformation, NULL, 0, &Length);
    if (Status != STATUS_BUFFER_TOO_SMALL &&
        Status != STATUS_BUFFER_OVERFLOW)
    {
        return NT_SUCCESS(Status) ? STATUS_UNSUCCESSFUL : Status;
    }

    Information = ExAllocatePoolZero(PagedPool, Length, TAG_IO);
    if (Information == NULL) return STATUS_INSUFFICIENT_RESOURCES;

    Status = ZwQueryKey(ServiceHandle,
                        KeyBasicInformation,
                        Information,
                        Length,
                        &Length);
    if (!NT_SUCCESS(Status))
    {
        ExFreePoolWithTag(Information, TAG_IO);
        return Status;
    }

    Service.Buffer = ((PKEY_BASIC_INFORMATION)Information)->Name;
    Service.Length = (USHORT)((PKEY_BASIC_INFORMATION)Information)->NameLength;
    Service.MaximumLength = Service.Length;

    Length = Prefix.Length + sizeof(WCHAR) + Service.Length + sizeof(WCHAR);

    Buffer = ExAllocatePoolZero(NonPagedPool, Length, TAG_IO_DRIVER_NAME);
    if (Buffer == NULL)
    {
        ExFreePoolWithTag(Information, TAG_IO);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    DriverName->Buffer = Buffer;
    DriverName->Length = 0;
    DriverName->MaximumLength = (USHORT)Length;

    RtlAppendUnicodeStringToString(DriverName, &Prefix);
    RtlAppendUnicodeToString(DriverName, L"\\");
    RtlAppendUnicodeStringToString(DriverName, &Service);

    if (ServiceName != NULL)
    {
        Buffer = ExAllocatePoolZero(NonPagedPool,
                                    Service.Length + sizeof(WCHAR),
                                    TAG_IO_DRIVER_NAME);
        if (Buffer == NULL)
        {
            ExFreePoolWithTag(DriverName->Buffer, TAG_IO_DRIVER_NAME);
            ExFreePoolWithTag(Information, TAG_IO);
            return STATUS_INSUFFICIENT_RESOURCES;
        }

        RtlCopyMemory(Buffer, Service.Buffer, Service.Length);

        ServiceName->Buffer = Buffer;
        ServiceName->Length = Service.Length;
        ServiceName->MaximumLength = Service.Length + sizeof(WCHAR);
    }

    ExFreePoolWithTag(Information, TAG_IO);

    return STATUS_SUCCESS;
}

NTSTATUS
IopNormalizeImagePath(
    _Inout_ PUNICODE_STRING ImagePath,
    _In_ PUNICODE_STRING ServiceName)
{
    UNICODE_STRING Normalized;
    PWCHAR Buffer;
    USHORT Length;

    PAGED_CODE();

    static const UNICODE_STRING SystemRoot =
        RTL_CONSTANT_STRING(L"\\SystemRoot\\");
    static const UNICODE_STRING DriversPath =
        RTL_CONSTANT_STRING(L"\\SystemRoot\\System32\\drivers\\");
    static const UNICODE_STRING SystemExtension =
        RTL_CONSTANT_STRING(L".sys");

    if (ImagePath->Length == 0)
    {
        Length = DriversPath.Length + ServiceName->Length +
                 SystemExtension.Length + sizeof(WCHAR);

        Buffer = ExAllocatePoolZero(NonPagedPool, Length, TAG_IO);
        if (Buffer == NULL) return STATUS_INSUFFICIENT_RESOURCES;

        Normalized.Buffer = Buffer;
        Normalized.Length = 0;
        Normalized.MaximumLength = Length;

        RtlAppendUnicodeStringToString(&Normalized, &DriversPath);
        RtlAppendUnicodeStringToString(&Normalized, ServiceName);
        RtlAppendUnicodeStringToString(&Normalized, &SystemExtension);

        *ImagePath = Normalized;

        return STATUS_SUCCESS;
    }

    if (ImagePath->Buffer[0] == L'\\') return STATUS_SUCCESS;

    Length = SystemRoot.Length + ImagePath->Length + sizeof(WCHAR);

    Buffer = ExAllocatePoolZero(NonPagedPool, Length, TAG_IO);
    if (Buffer == NULL) return STATUS_INSUFFICIENT_RESOURCES;

    Normalized.Buffer = Buffer;
    Normalized.Length = 0;
    Normalized.MaximumLength = Length;

    RtlAppendUnicodeStringToString(&Normalized, &SystemRoot);
    RtlAppendUnicodeStringToString(&Normalized, ImagePath);

    ExFreePool(ImagePath->Buffer);

    *ImagePath = Normalized;

    return STATUS_SUCCESS;
}

NTSTATUS
IopOpenSafeBootKey(
    _Out_ PHANDLE SafeBootHandle)
{
    OBJECT_ATTRIBUTES ObjectAttributes;
    UNICODE_STRING KeyName;

    PAGED_CODE();

    *SafeBootHandle = NULL;

    if (InitSafeBootMode == 0) return STATUS_NOT_FOUND;

    if (InitSafeBootMode == 1)
    {
        RtlInitUnicodeString(&KeyName,
                             L"\\Registry\\Machine\\System\\CurrentControlSet"
                             L"\\Control\\SafeBoot\\Minimal");
    }
    else if (InitSafeBootMode == 2)
    {
        RtlInitUnicodeString(&KeyName,
                             L"\\Registry\\Machine\\System\\CurrentControlSet"
                             L"\\Control\\SafeBoot\\Network");
    }
    else
    {
        return STATUS_NOT_FOUND;
    }

    InitializeObjectAttributes(&ObjectAttributes,
                               &KeyName,
                               OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE,
                               NULL,
                               NULL);

    return ZwOpenKey(SafeBootHandle, KEY_READ, &ObjectAttributes);
}

BOOLEAN
IopHasSafeBootEntry(
    _In_ HANDLE SafeBootHandle,
    _In_ PUNICODE_STRING Name)
{
    OBJECT_ATTRIBUTES ObjectAttributes;
    NTSTATUS Status;
    HANDLE Handle;

    PAGED_CODE();

    if (SafeBootHandle == NULL) return FALSE;

    InitializeObjectAttributes(&ObjectAttributes,
                               Name,
                               OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE,
                               SafeBootHandle,
                               NULL);

    Status = ZwOpenKey(&Handle, KEY_READ, &ObjectAttributes);
    if (!NT_SUCCESS(Status)) return FALSE;

    ZwClose(Handle);

    return TRUE;
}

static
BOOLEAN
IopSafeBootInstanceString(
    _In_ HANDLE InstanceKey,
    _In_ PWSTR ValueName,
    _Out_ PKEY_VALUE_FULL_INFORMATION *Information,
    _Out_ PUNICODE_STRING String)
{
    PAGED_CODE();

    if (!NT_SUCCESS(IopGetRegistryValue(InstanceKey, ValueName, Information))) return FALSE;

    if (((*Information)->Type == REG_SZ) &&
        ((*Information)->DataLength >= sizeof(WCHAR)) &&
        ((*Information)->DataLength <= UNICODE_STRING_MAX_BYTES))
    {
        String->Buffer = (PWCHAR)((PUCHAR)*Information + (*Information)->DataOffset);
        String->Length = (USHORT)((*Information)->DataLength & ~(sizeof(WCHAR) - 1));
        while ((String->Length >= sizeof(WCHAR)) &&
               (String->Buffer[String->Length / sizeof(WCHAR) - 1] == UNICODE_NULL))
        {
            String->Length -= sizeof(WCHAR);
        }
        String->MaximumLength = String->Length;

        if (String->Length != 0) return TRUE;
    }

    ExFreePool(*Information);
    return FALSE;
}

/* Enumerator, device and instance levels lie below Enum; Depth counts the levels left */
static
BOOLEAN
IopSafeBootScanInstances(
    _In_ HANDLE Key,
    _In_ ULONG Depth,
    _In_ PUNICODE_STRING ServiceName,
    _In_ HANDLE SafeBoot)
{
    ULONGLONG Buffer[(sizeof(KEY_BASIC_INFORMATION) + 256 * sizeof(WCHAR)) / sizeof(ULONGLONG) + 1];
    PKEY_BASIC_INFORMATION BasicInfo = (PKEY_BASIC_INFORMATION)Buffer;
    PKEY_VALUE_FULL_INFORMATION ServiceInfo;
    PKEY_VALUE_FULL_INFORMATION ClassInfo;
    UNICODE_STRING Name;
    UNICODE_STRING ClassGuid;
    BOOLEAN Allowed = FALSE;
    HANDLE SubKey;
    ULONG Index;
    ULONG Length;
    NTSTATUS Status;

    PAGED_CODE();

    if (Depth == 0)
    {
        if (!IopSafeBootInstanceString(Key, L"Service", &ServiceInfo, &Name)) return FALSE;

        if (RtlEqualUnicodeString(&Name, ServiceName, TRUE) &&
            IopSafeBootInstanceString(Key, L"ClassGUID", &ClassInfo, &ClassGuid))
        {
            Allowed = IopHasSafeBootEntry(SafeBoot, &ClassGuid);
            ExFreePool(ClassInfo);
        }

        ExFreePool(ServiceInfo);
        return Allowed;
    }

    for (Index = 0; !Allowed; Index++)
    {
        Status = ZwEnumerateKey(Key, Index, KeyBasicInformation, BasicInfo, sizeof(Buffer), &Length);
        if ((Status == STATUS_BUFFER_OVERFLOW) || (Status == STATUS_BUFFER_TOO_SMALL)) continue;
        if (!NT_SUCCESS(Status)) break;

        Name.Buffer = BasicInfo->Name;
        Name.Length = (USHORT)BasicInfo->NameLength;
        Name.MaximumLength = Name.Length;

        if (!NT_SUCCESS(IopOpenRegistryKeyEx(&SubKey, Key, &Name, KEY_READ))) continue;

        Allowed = IopSafeBootScanInstances(SubKey, Depth - 1, ServiceName, SafeBoot);
        ZwClose(SubKey);
    }

    return Allowed;
}

/* The persistent instance keys, as a service's volatile Enum list is still empty when system drivers start */
static
BOOLEAN
IopSafeBootAllowsServiceDevices(
    _In_ HANDLE SafeBoot,
    _In_ PUNICODE_STRING ServiceName)
{
    UNICODE_STRING EnumRoot = RTL_CONSTANT_STRING(ENUM_ROOT);
    BOOLEAN Allowed = FALSE;
    HANDLE RootKey;

    PAGED_CODE();

    if (!NT_SUCCESS(IopOpenRegistryKeyEx(&RootKey, NULL, &EnumRoot, KEY_READ))) return FALSE;

    Allowed = IopSafeBootScanInstances(RootKey, 3, ServiceName, SafeBoot);
    ZwClose(RootKey);

    return Allowed;
}

/* The lists name some drivers by their image file, such as dxgkrnl.sys, rather than by service */
static
BOOLEAN
IopSafeBootAllowsImageName(
    _In_ HANDLE SafeBoot,
    _In_ HANDLE ServiceHandle,
    _In_ PUNICODE_STRING ServiceName)
{
    PKEY_VALUE_FULL_INFORMATION Information = NULL;
    UNICODE_STRING ImageName;
    BOOLEAN Allowed = FALSE;
    USHORT Index;
    PWCHAR Buffer;

    PAGED_CODE();

    if (NT_SUCCESS(IopGetRegistryValue(ServiceHandle, L"ImagePath", &Information)))
    {
        if (((Information->Type == REG_SZ) || (Information->Type == REG_EXPAND_SZ)) &&
            (Information->DataLength >= sizeof(WCHAR)) &&
            (Information->DataLength <= UNICODE_STRING_MAX_BYTES))
        {
            ImageName.Buffer = (PWCHAR)((PUCHAR)Information + Information->DataOffset);
            ImageName.Length = (USHORT)(Information->DataLength & ~(sizeof(WCHAR) - 1));
            while ((ImageName.Length >= sizeof(WCHAR)) &&
                   (ImageName.Buffer[ImageName.Length / sizeof(WCHAR) - 1] == UNICODE_NULL))
            {
                ImageName.Length -= sizeof(WCHAR);
            }

            for (Index = ImageName.Length / sizeof(WCHAR); Index > 0; Index--)
            {
                if (ImageName.Buffer[Index - 1] == OBJ_NAME_PATH_SEPARATOR) break;
            }

            ImageName.Buffer += Index;
            ImageName.Length -= Index * sizeof(WCHAR);
            ImageName.MaximumLength = ImageName.Length;

            if (ImageName.Length != 0) Allowed = IopHasSafeBootEntry(SafeBoot, &ImageName);
        }

        ExFreePool(Information);
        return Allowed;
    }

    ImageName.Length = 0;
    ImageName.MaximumLength = ServiceName->Length + 4 * sizeof(WCHAR);
    Buffer = ExAllocatePoolWithTag(PagedPool, ImageName.MaximumLength, TAG_IO);
    if (Buffer == NULL) return FALSE;

    ImageName.Buffer = Buffer;
    RtlAppendUnicodeStringToString(&ImageName, ServiceName);
    RtlAppendUnicodeToString(&ImageName, L".sys");

    Allowed = IopHasSafeBootEntry(SafeBoot, &ImageName);
    ExFreePoolWithTag(Buffer, TAG_IO);

    return Allowed;
}

BOOLEAN
IopIsSafeBootDeviceClass(
    _In_ PUNICODE_STRING ClassGuid)
{
    BOOLEAN Allowed;
    HANDLE SafeBoot;

    PAGED_CODE();

    if (InitSafeBootMode == 0) return TRUE;

    if (!NT_SUCCESS(IopOpenSafeBootKey(&SafeBoot))) return TRUE;

    Allowed = IopHasSafeBootEntry(SafeBoot, ClassGuid);
    ZwClose(SafeBoot);

    return Allowed;
}

BOOLEAN
IopCheckSafeBootDriver(
    _In_ HANDLE ServiceHandle,
    _In_ PUNICODE_STRING ServiceName)
{
    PKEY_VALUE_FULL_INFORMATION Information = NULL;
    UNICODE_STRING GroupName;
    BOOLEAN Allowed = FALSE;
    NTSTATUS Status;
    HANDLE SafeBoot;

    PAGED_CODE();

    if (InitSafeBootMode == 0) return TRUE;

    Status = IopOpenSafeBootKey(&SafeBoot);
    if (!NT_SUCCESS(Status)) return TRUE;

    if (IopHasSafeBootEntry(SafeBoot, ServiceName) ||
        IopSafeBootAllowsImageName(SafeBoot, ServiceHandle, ServiceName))
    {
        ZwClose(SafeBoot);
        return TRUE;
    }

    Status = IopGetRegistryValue(ServiceHandle, L"Group", &Information);
    if (NT_SUCCESS(Status))
    {
        if (Information->Type == REG_SZ &&
            Information->DataLength > sizeof(WCHAR))
        {
            GroupName.Buffer = (PWCHAR)((PUCHAR)Information +
                                        Information->DataOffset);
            GroupName.Length =
                (USHORT)(Information->DataLength - sizeof(WCHAR));
            GroupName.MaximumLength = GroupName.Length;

            Allowed = IopHasSafeBootEntry(SafeBoot, &GroupName);
        }

        ExFreePool(Information);
    }

    if (!Allowed) Allowed = IopSafeBootAllowsServiceDevices(SafeBoot, ServiceName);

    ZwClose(SafeBoot);

    return Allowed;
}

NTSTATUS
IopReferenceDriverObject(
    _In_ PCUNICODE_STRING DriverName,
    _Out_ PDRIVER_OBJECT *DriverObject)
{
    UNICODE_STRING Name;
    NTSTATUS Status;

    PAGED_CODE();

    Name.Buffer = DriverName->Buffer;
    Name.Length = DriverName->Length;
    Name.MaximumLength = DriverName->MaximumLength;

    Status = ObReferenceObjectByName(&Name,
                                     OBJ_CASE_INSENSITIVE,
                                     NULL,
                                     0,
                                     IoDriverObjectType,
                                     KernelMode,
                                     NULL,
                                     (PVOID *)DriverObject);

    return Status;
}

NTSTATUS
IopTryUnloadDriver(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ BOOLEAN Force)
{
    PEXTENDED_DRIVER_EXTENSION Extension = IoGetDrvObjExtension(DriverObject);
    PDEVICE_OBJECT DeviceObject;
    BOOLEAN Busy = FALSE;
    KIRQL OldIrql;

    PAGED_CODE();

    if (DriverObject->DriverUnload == NULL) return STATUS_INVALID_DEVICE_REQUEST;

    /* The unload routine deletes the driver's devices, which leads back here; it is run once */
    if (DriverObject->Flags & DRVO_UNLOAD_INVOKED) return STATUS_SUCCESS;

    if (Extension->PnpUnloadBarrier != 0 && !Force)
    {
        InterlockedExchange(&Extension->PnpUnloadPending, 1);
        return STATUS_PENDING;
    }

    if (!Force)
    {
        /*
         * A driver goes once nothing holds any of its devices: no open file,
         * no device attached on top. Until then its devices are marked, and
         * the last reference to go brings the unload back.
         */
        KeAcquireSpinLock(&IopDeviceTreeLock, &OldIrql);

        for (DeviceObject = DriverObject->DeviceObject;
             DeviceObject != NULL;
             DeviceObject = DeviceObject->NextDevice)
        {
            if (DeviceObject->ReferenceCount != 0 ||
                DeviceObject->AttachedDevice != NULL)
            {
                Busy = TRUE;
            }
        }

        if (Busy)
        {
            for (DeviceObject = DriverObject->DeviceObject;
                 DeviceObject != NULL;
                 DeviceObject = DeviceObject->NextDevice)
            {
                IoGetDevObjExtension(DeviceObject)->ExtensionFlags |=
                    DOE_UNLOAD_PENDING;
            }
        }

        KeReleaseSpinLock(&IopDeviceTreeLock, OldIrql);

        if (Busy) return STATUS_SUCCESS;
    }

    DriverObject->Flags |= DRVO_UNLOAD_INVOKED;
    DriverObject->DriverUnload(DriverObject);

    ObMakeTemporaryObject(DriverObject);
    ObDereferenceObject(DriverObject);

    return STATUS_SUCCESS;
}

NTSTATUS
IopUnloadDriver(
    _In_ PUNICODE_STRING DriverServiceName,
    _In_ BOOLEAN UnloadPnpDrivers)
{
    PDRIVER_OBJECT DriverObject;
    OBJECT_ATTRIBUTES ObjectAttributes;
    UNICODE_STRING DriverName;
    NTSTATUS Status;
    HANDLE ServiceHandle;

    PAGED_CODE();

    /* The caller names the service by the full path of its key */
    InitializeObjectAttributes(&ObjectAttributes,
                               DriverServiceName,
                               OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE,
                               NULL,
                               NULL);

    Status = ZwOpenKey(&ServiceHandle, KEY_READ, &ObjectAttributes);
    if (!NT_SUCCESS(Status)) return Status;

    RtlInitEmptyUnicodeString(&DriverName, NULL, 0);

    Status = IopGetDriverNames(ServiceHandle, &DriverName, NULL);
    ZwClose(ServiceHandle);

    if (!NT_SUCCESS(Status)) return Status;

    Status = IopReferenceDriverObject(&DriverName, &DriverObject);
    ExFreePoolWithTag(DriverName.Buffer, TAG_IO_DRIVER_NAME);

    if (!NT_SUCCESS(Status)) return Status;

    Status = IopTryUnloadDriver(DriverObject, UnloadPnpDrivers);

    ObDereferenceObject(DriverObject);

    return Status;
}

NTSTATUS
IopDoLoadUnloadDriver(
    _In_opt_ PUNICODE_STRING DriverServiceName,
    _Inout_ PDRIVER_OBJECT *DriverObject)
{
    PAGED_CODE();

    if (DriverServiceName == NULL)
    {
        return IopTryUnloadDriver(*DriverObject, TRUE);
    }

    return STATUS_INVALID_PARAMETER;
}

static
BOOLEAN
IopIsServiceDriverLoaded(
    _In_ HANDLE ServiceHandle)
{
    UNICODE_STRING DriverName;
    PDRIVER_OBJECT DriverObject;
    NTSTATUS Status;

    RtlInitEmptyUnicodeString(&DriverName, NULL, 0);

    Status = IopGetDriverNames(ServiceHandle, &DriverName, NULL);
    if (!NT_SUCCESS(Status)) return FALSE;

    Status = IopReferenceDriverObject(&DriverName, &DriverObject);
    ExFreePoolWithTag(DriverName.Buffer, TAG_IO_DRIVER_NAME);

    if (!NT_SUCCESS(Status)) return FALSE;

    ObDereferenceObject(DriverObject);
    return TRUE;
}

NTSTATUS
IopLoadDriverForDevice(
    _In_ HANDLE ServiceHandle,
    _In_ BOOLEAN SafeBootClassAllowed,
    _Out_ PDRIVER_OBJECT *DriverObject)
{
    UNICODE_STRING DriverName;
    UNICODE_STRING ServiceName;
    NTSTATUS Status;

    PAGED_CODE();

    *DriverObject = NULL;

    RtlInitEmptyUnicodeString(&DriverName, NULL, 0);
    RtlInitEmptyUnicodeString(&ServiceName, NULL, 0);

    Status = IopGetDriverNames(ServiceHandle, &DriverName, &ServiceName);
    if (!NT_SUCCESS(Status)) return Status;

    /* A driver found by its name may still be in its DriverEntry: the lookup waits for any load in progress */
    KeEnterCriticalRegion();
    ExAcquireResourceExclusiveLite(&IopDriverLoadResource, TRUE);

    Status = IopReferenceDriverObject(&DriverName, DriverObject);
    if (!NT_SUCCESS(Status))
    {
        /* Until the system root exists only the drivers the loader brought can run */
        if (!PnPBootDriversInitialized)
        {
            Status = STATUS_DRIVER_UNABLE_TO_LOAD;
        }
        else if (!SafeBootClassAllowed && !IopCheckSafeBootDriver(ServiceHandle, &ServiceName))
        {
            Status = STATUS_NOT_SAFE_MODE_DRIVER;
        }
        else
        {
            Status = IopLoadServiceModule(ServiceHandle, DriverObject);
        }
    }

    ExReleaseResourceLite(&IopDriverLoadResource);
    KeLeaveCriticalRegion();

    ASSERT(NT_SUCCESS(Status) || *DriverObject == NULL);

    ExFreePoolWithTag(DriverName.Buffer, TAG_IO_DRIVER_NAME);
    if (ServiceName.Buffer != NULL)
    {
        ExFreePoolWithTag(ServiceName.Buffer, TAG_IO_DRIVER_NAME);
    }

    return Status;
}

NTSTATUS
IopLoadDriverForPnp(
    _In_ HANDLE ServiceHandle,
    _Out_ PDRIVER_OBJECT *DriverObject)
{
    PAGED_CODE();

    return IopLoadDriverForDevice(ServiceHandle, FALSE, DriverObject);
}

/* PUBLIC FUNCTIONS ***********************************************************/

NTSTATUS
NTAPI
IoCreateDriver(
    _In_opt_ PUNICODE_STRING DriverName,
    _In_ PDRIVER_INITIALIZE InitializationFunction)
{
    PEXTENDED_DRIVER_EXTENSION Extension;
    OBJECT_ATTRIBUTES ObjectAttributes;
    UNICODE_STRING LocalName;
    PDRIVER_OBJECT DriverObject;
    NTSTATUS Status;
    ULONG TotalSize;
    PWCHAR Buffer;
    ULONG Index;

    PAGED_CODE();

    WCHAR Generated[32];
    UNICODE_STRING Suffix;

    RtlInitEmptyUnicodeString(&LocalName, NULL, 0);

    if (DriverName == NULL)
    {
        static const UNICODE_STRING AnonymousPrefix =
            RTL_CONSTANT_STRING(L"\\Driver\\");
        static volatile LONG AnonymousCount = 0;

        LocalName.Buffer = Generated;
        LocalName.Length = 0;
        LocalName.MaximumLength = sizeof(Generated);

        RtlAppendUnicodeStringToString(&LocalName, &AnonymousPrefix);

        Suffix.Buffer = Generated + LocalName.Length / sizeof(WCHAR);
        Suffix.Length = 0;
        Suffix.MaximumLength =
            (USHORT)(sizeof(Generated) - LocalName.Length);

        Status = RtlIntegerToUnicodeString(
            (ULONG)InterlockedIncrement(&AnonymousCount),
            16,
            &Suffix);
        if (!NT_SUCCESS(Status)) return Status;

        LocalName.Length += Suffix.Length;
        DriverName = &LocalName;
    }

    InitializeObjectAttributes(&ObjectAttributes,
                               DriverName,
                               OBJ_PERMANENT | OBJ_CASE_INSENSITIVE |
                                   OBJ_KERNEL_HANDLE,
                               NULL,
                               NULL);

    TotalSize = (ULONG)(sizeof(DRIVER_OBJECT) +
                        sizeof(EXTENDED_DRIVER_EXTENSION));

    Status = ObCreateObject(KernelMode,
                            IoDriverObjectType,
                            &ObjectAttributes,
                            KernelMode,
                            NULL,
                            TotalSize,
                            0,
                            0,
                            (PVOID *)&DriverObject);
    if (!NT_SUCCESS(Status)) return Status;

    RtlZeroMemory(DriverObject, TotalSize);

    DriverObject->Type = IO_TYPE_DRIVER;
    DriverObject->Size = sizeof(DRIVER_OBJECT);
    DriverObject->Flags = DRVO_BUILTIN_DRIVER;
    DriverObject->DriverInit = InitializationFunction;
    DriverObject->HardwareDatabase = &IopHardwareDatabaseName;
    DriverObject->DriverExtension = (PDRIVER_EXTENSION)(DriverObject + 1);

    Extension = IoGetDrvObjExtension(DriverObject);
    Extension->DriverObject = DriverObject;

    for (Index = 0; Index <= IRP_MJ_MAXIMUM_FUNCTION; Index++)
    {
        DriverObject->MajorFunction[Index] = IopInvalidDeviceRequest;
    }

    Buffer = ExAllocatePoolZero(NonPagedPool,
                                DriverName->Length + sizeof(WCHAR),
                                TAG_IO_DRIVER_NAME);
    if (Buffer == NULL)
    {
        ObDereferenceObject(DriverObject);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    RtlCopyMemory(Buffer, DriverName->Buffer, DriverName->Length);

    DriverObject->DriverName.Buffer = Buffer;
    DriverObject->DriverName.Length = DriverName->Length;
    DriverObject->DriverName.MaximumLength =
        DriverName->Length + sizeof(WCHAR);

    Status = ObInsertObject(DriverObject,
                            NULL,
                            FILE_READ_DATA,
                            0,
                            (PVOID *)&DriverObject,
                            NULL);
    if (!NT_SUCCESS(Status)) return Status;

    Status = DriverObject->DriverInit(DriverObject, NULL);
    if (!NT_SUCCESS(Status))
    {
        DriverObject->DriverSection = NULL;
        ObMakeTemporaryObject(DriverObject);
        ObDereferenceObject(DriverObject);
        return Status;
    }

    IopReadyDeviceObjects(DriverObject);

    return STATUS_SUCCESS;
}

VOID
NTAPI
IoDeleteDriver(
    _In_ PDRIVER_OBJECT DriverObject)
{
    PAGED_CODE();

    ObMakeTemporaryObject(DriverObject);
    ObDereferenceObject(DriverObject);
}

NTSTATUS
NTAPI
IoAllocateDriverObjectExtension(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PVOID ClientIdentificationAddress,
    _In_ ULONG DriverObjectExtensionSize,
    _Outptr_ PVOID *DriverObjectExtension)
{
    PEXTENDED_DRIVER_EXTENSION Extension;
    PIO_CLIENT_EXTENSION Client;
    PIO_CLIENT_EXTENSION Existing;
    KIRQL OldIrql;

    *DriverObjectExtension = NULL;

    Client = ExAllocatePoolZero(NonPagedPool,
                                sizeof(IO_CLIENT_EXTENSION) +
                                    DriverObjectExtensionSize,
                                TAG_DRIVER_EXTENSION);
    if (Client == NULL) return STATUS_INSUFFICIENT_RESOURCES;

    Client->ClientIdentificationAddress = ClientIdentificationAddress;

    Extension = IoGetDrvObjExtension(DriverObject);

    KeAcquireSpinLock(&IopDeviceTreeLock, &OldIrql);

    Existing = Extension->ClientDriverExtension;
    while (Existing != NULL)
    {
        if (Existing->ClientIdentificationAddress ==
            ClientIdentificationAddress)
        {
            KeReleaseSpinLock(&IopDeviceTreeLock, OldIrql);
            ExFreePoolWithTag(Client, TAG_DRIVER_EXTENSION);
            return STATUS_OBJECT_NAME_COLLISION;
        }

        Existing = Existing->NextExtension;
    }

    Client->NextExtension = Extension->ClientDriverExtension;
    Extension->ClientDriverExtension = Client;

    KeReleaseSpinLock(&IopDeviceTreeLock, OldIrql);

    *DriverObjectExtension = Client + 1;

    return STATUS_SUCCESS;
}

PVOID
NTAPI
IoGetDriverObjectExtension(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PVOID ClientIdentificationAddress)
{
    PEXTENDED_DRIVER_EXTENSION Extension;
    PIO_CLIENT_EXTENSION Client;
    PVOID Found = NULL;
    KIRQL OldIrql;

    Extension = IoGetDrvObjExtension(DriverObject);

    KeAcquireSpinLock(&IopDeviceTreeLock, &OldIrql);

    Client = Extension->ClientDriverExtension;
    while (Client != NULL)
    {
        if (Client->ClientIdentificationAddress == ClientIdentificationAddress)
        {
            Found = Client + 1;
            break;
        }

        Client = Client->NextExtension;
    }

    KeReleaseSpinLock(&IopDeviceTreeLock, OldIrql);

    return Found;
}

static
VOID
IopRegisterReinitialization(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PDRIVER_REINITIALIZE ReinitRoutine,
    _In_opt_ PVOID Context,
    _In_ PLIST_ENTRY ListHead,
    _In_ PKSPIN_LOCK Lock)
{
    PDRIVER_REINIT_ITEM Item;
    KIRQL OldIrql;

    PAGED_CODE();

    Item = ExAllocatePoolZero(NonPagedPool,
                              sizeof(DRIVER_REINIT_ITEM),
                              TAG_IO);
    if (Item == NULL) return;

    Item->DriverObject = DriverObject;
    Item->ReinitRoutine = ReinitRoutine;
    Item->Context = Context;

    DriverObject->Flags |= DRVO_REINIT_REGISTERED;

    KeAcquireSpinLock(Lock, &OldIrql);
    InsertTailList(ListHead, &Item->ItemEntry);
    KeReleaseSpinLock(Lock, OldIrql);
}

VOID
NTAPI
IoRegisterDriverReinitialization(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PDRIVER_REINITIALIZE DriverReinitializationRoutine,
    _In_opt_ PVOID Context)
{
    IopRegisterReinitialization(DriverObject,
                                DriverReinitializationRoutine,
                                Context,
                                &DriverReinitListHead,
                                &DriverReinitListLock);
}

VOID
NTAPI
IoRegisterBootDriverReinitialization(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PDRIVER_REINITIALIZE DriverReinitializationRoutine,
    _In_opt_ PVOID Context)
{
    IopRegisterReinitialization(DriverObject,
                                DriverReinitializationRoutine,
                                Context,
                                &DriverBootReinitListHead,
                                &DriverBootReinitListLock);
}

static
VOID
IopRunReinitializationList(
    _In_ PLIST_ENTRY ListHead,
    _In_ PKSPIN_LOCK Lock,
    _Inout_ PLIST_ENTRY *TailEntry)
{
    PDRIVER_REINIT_ITEM Item;
    PLIST_ENTRY Entry;
    KIRQL OldIrql;

    PAGED_CODE();

    /*
     * Only what is queued when the list is run gets to run: a routine that
     * registers itself again is called back by the next pass, not this one.
     */
    KeAcquireSpinLock(Lock, &OldIrql);
    *TailEntry = IsListEmpty(ListHead) ? NULL : ListHead->Blink;
    KeReleaseSpinLock(Lock, OldIrql);

    while (TRUE)
    {
        KeAcquireSpinLock(Lock, &OldIrql);

        if (*TailEntry == NULL || IsListEmpty(ListHead))
        {
            *TailEntry = NULL;
            KeReleaseSpinLock(Lock, OldIrql);
            return;
        }

        Entry = RemoveHeadList(ListHead);
        if (Entry == *TailEntry) *TailEntry = NULL;

        KeReleaseSpinLock(Lock, OldIrql);

        Item = CONTAINING_RECORD(Entry, DRIVER_REINIT_ITEM, ItemEntry);

        /* FIXME: concurrent passes (NtLoadDriver beside the PnP worker) share *TailEntry, so a routine that registers itself again can run twice in one pass, and two items of one driver race on DriverExtension->Count */
        Item->ReinitRoutine(Item->DriverObject,
                            Item->Context,
                            ++Item->DriverObject->DriverExtension->Count);

        ExFreePoolWithTag(Item, TAG_IO);
    }
}

VOID
NTAPI
IopReinitializeDrivers(VOID)
{
    IopRunReinitializationList(&DriverReinitListHead,
                               &DriverReinitListLock,
                               &DriverReinitTailEntry);
}

VOID
NTAPI
IopReinitializeBootDrivers(VOID)
{
    IopRunReinitializationList(&DriverBootReinitListHead,
                               &DriverBootReinitListLock,
                               &DriverBootReinitTailEntry);
}

NTSTATUS
NTAPI
NtLoadDriver(
    _In_ PUNICODE_STRING DriverServiceName)
{
    KPROCESSOR_MODE PreviousMode = ExGetPreviousMode();
    UNICODE_STRING CapturedName;
    PDRIVER_OBJECT DriverObject;
    OBJECT_ATTRIBUTES ObjectAttributes;
    NTSTATUS Status;
    HANDLE ServiceHandle;

    PAGED_CODE();

    if (!SeSinglePrivilegeCheck(SeLoadDriverPrivilege, PreviousMode))
    {
        return STATUS_PRIVILEGE_NOT_HELD;
    }

    Status = ProbeAndCaptureUnicodeString(&CapturedName,
                                          PreviousMode,
                                          DriverServiceName);
    if (!NT_SUCCESS(Status)) return Status;

    if (CapturedName.Length == 0)
    {
        ReleaseCapturedUnicodeString(&CapturedName, PreviousMode);
        return STATUS_INVALID_PARAMETER;
    }

    /* The caller names the service by the full path of its key */
    InitializeObjectAttributes(&ObjectAttributes,
                               &CapturedName,
                               OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE,
                               NULL,
                               NULL);

    Status = ZwOpenKey(&ServiceHandle, KEY_READ, &ObjectAttributes);

    ReleaseCapturedUnicodeString(&CapturedName, PreviousMode);

    if (!NT_SUCCESS(Status)) return Status;

    KeEnterCriticalRegion();
    ExAcquireResourceExclusiveLite(&IopDriverLoadResource, TRUE);

    /* A caller asking for a driver that is already running is told so */
    if (IopIsServiceDriverLoaded(ServiceHandle))
    {
        Status = STATUS_IMAGE_ALREADY_LOADED;
    }
    else
    {
        Status = IopLoadDriverForPnp(ServiceHandle, &DriverObject);
        if (NT_SUCCESS(Status) && DriverObject != NULL)
        {
            ObDereferenceObject(DriverObject);
        }
    }

    ExReleaseResourceLite(&IopDriverLoadResource);
    KeLeaveCriticalRegion();

    ZwClose(ServiceHandle);

    /* A driver with no device to attach still gets to finish initializing */
    if (IopSystemDriversInitialized) IopReinitializeDrivers();

    return Status;
}

NTSTATUS
NTAPI
NtUnloadDriver(
    _In_ PUNICODE_STRING DriverServiceName)
{
    KPROCESSOR_MODE PreviousMode = ExGetPreviousMode();
    UNICODE_STRING CapturedName;
    NTSTATUS Status;

    PAGED_CODE();

    if (!SeSinglePrivilegeCheck(SeLoadDriverPrivilege, PreviousMode))
    {
        return STATUS_PRIVILEGE_NOT_HELD;
    }

    Status = ProbeAndCaptureUnicodeString(&CapturedName,
                                          PreviousMode,
                                          DriverServiceName);
    if (!NT_SUCCESS(Status)) return Status;

    if (CapturedName.Length == 0)
    {
        ReleaseCapturedUnicodeString(&CapturedName, PreviousMode);
        return STATUS_INVALID_PARAMETER;
    }

    KeEnterCriticalRegion();
    ExAcquireResourceExclusiveLite(&IopDriverLoadResource, TRUE);

    Status = IopUnloadDriver(&CapturedName, FALSE);

    ExReleaseResourceLite(&IopDriverLoadResource);
    KeLeaveCriticalRegion();

    ReleaseCapturedUnicodeString(&CapturedName, PreviousMode);

    return Status;
}
