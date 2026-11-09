/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Bringing the boot and system drivers up in the order asked for
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#include "../ioint.h"
#define NDEBUG
#include <debug.h>

/* GLOBALS ********************************************************************/

IOP_GROUP_TABLE IopGroupTable = {NULL, 0};
PULONG IopGroupIndex = NULL;

PUNICODE_STRING PiInitGroupOrderTable = NULL;
ULONG PiInitGroupOrderTableCount = 0;

BOOLEAN PnPBootDriversLoaded = FALSE;
BOOLEAN PnPBootDriversInitialized = FALSE;

/* PRIVATE DATA ***************************************************************/

static const UNICODE_STRING IopGroupOrderKeyName = RTL_CONSTANT_STRING(
    L"\\Registry\\Machine\\System\\CurrentControlSet\\Control"
    L"\\ServiceGroupOrder");

static const UNICODE_STRING IopServicesKeyName = RTL_CONSTANT_STRING(
    L"\\Registry\\Machine\\System\\CurrentControlSet\\Services");

/* PRIVATE FUNCTIONS **********************************************************/

CODE_SEG("INIT")
NTSTATUS
NTAPI
PiInitCacheGroupInformation(VOID)
{
    PKEY_VALUE_FULL_INFORMATION Information = NULL;
    OBJECT_ATTRIBUTES ObjectAttributes;
    PUNICODE_STRING Groups = NULL;
    ULONG GroupCount = 0;
    NTSTATUS Status;
    HANDLE Handle;
    ULONG Index;

    PAGED_CODE();

    InitializeObjectAttributes(&ObjectAttributes,
                               (PUNICODE_STRING)&IopGroupOrderKeyName,
                               OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE,
                               NULL,
                               NULL);

    Status = ZwOpenKey(&Handle, KEY_READ, &ObjectAttributes);
    if (!NT_SUCCESS(Status)) return Status;

    Status = IopGetRegistryValue(Handle, L"List", &Information);
    ZwClose(Handle);

    if (!NT_SUCCESS(Status)) return Status;

    Status = PnpRegMultiSzToUnicodeStrings(Information, &Groups, &GroupCount);
    ExFreePool(Information);

    if (!NT_SUCCESS(Status)) return Status;

    IopGroupIndex = ExAllocatePoolZero(PagedPool,
                                       (GroupCount + 1) * sizeof(ULONG),
                                       TAG_IO);
    if (IopGroupIndex == NULL)
    {
        PnpFreeUnicodeStringList(Groups, GroupCount);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    for (Index = 0; Index <= GroupCount; Index++)
    {
        IopGroupIndex[Index] = Index;
    }

    IopGroupTable.Groups = Groups;
    IopGroupTable.Count = GroupCount;

    PiInitGroupOrderTable = Groups;
    PiInitGroupOrderTableCount = GroupCount;

    return STATUS_SUCCESS;
}

USHORT
NTAPI
PpInitGetGroupOrderIndex(
    IN HANDLE ServiceHandle)
{
    PKEY_VALUE_FULL_INFORMATION Information = NULL;
    UNICODE_STRING GroupName;
    NTSTATUS Status;
    USHORT Index;

    PAGED_CODE();

    if (ServiceHandle == NULL) return (USHORT)IopGroupTable.Count;

    Status = IopGetRegistryValue(ServiceHandle, L"Group", &Information);
    if (!NT_SUCCESS(Status)) return (USHORT)IopGroupTable.Count;

    if (Information->Type != REG_SZ ||
        Information->DataLength <= sizeof(WCHAR))
    {
        ExFreePool(Information);
        return (USHORT)IopGroupTable.Count;
    }

    GroupName.Buffer = (PWCHAR)((PUCHAR)Information + Information->DataOffset);
    GroupName.Length = (USHORT)(Information->DataLength - sizeof(WCHAR));
    GroupName.MaximumLength = GroupName.Length;

    for (Index = 0; Index < (USHORT)IopGroupTable.Count; Index++)
    {
        if (RtlEqualUnicodeString(&GroupName,
                                  &IopGroupTable.Groups[Index],
                                  TRUE))
        {
            ExFreePool(Information);
            return Index;
        }
    }

    ExFreePool(Information);

    return (USHORT)IopGroupTable.Count;
}

USHORT
NTAPI
PipGetDriverTagPriority(
    IN HANDLE ServiceHandle)
{
    PKEY_VALUE_FULL_INFORMATION TagInformation = NULL;
    PKEY_VALUE_FULL_INFORMATION GroupInformation = NULL;
    OBJECT_ATTRIBUTES ObjectAttributes;
    UNICODE_STRING GroupName;
    USHORT Priority = (USHORT)-1;
    NTSTATUS Status;
    HANDLE OrderHandle;
    PULONG TagList;
    ULONG TagCount;
    ULONG Tag;
    ULONG Index;

    PAGED_CODE();

    if (ServiceHandle == NULL) return Priority;

    Status = IopGetRegistryValue(ServiceHandle, L"Tag", &TagInformation);
    if (!NT_SUCCESS(Status)) return Priority;

    if (TagInformation->Type != REG_DWORD ||
        TagInformation->DataLength != sizeof(ULONG))
    {
        ExFreePool(TagInformation);
        return Priority;
    }

    Tag = *(PULONG)((PUCHAR)TagInformation + TagInformation->DataOffset);
    ExFreePool(TagInformation);

    Status = IopGetRegistryValue(ServiceHandle, L"Group", &GroupInformation);
    if (!NT_SUCCESS(Status)) return Priority;

    if (GroupInformation->Type != REG_SZ ||
        GroupInformation->DataLength <= sizeof(WCHAR))
    {
        ExFreePool(GroupInformation);
        return Priority;
    }

    GroupName.Buffer = (PWCHAR)((PUCHAR)GroupInformation +
                                GroupInformation->DataOffset);
    GroupName.Length =
        (USHORT)(GroupInformation->DataLength - sizeof(WCHAR));
    GroupName.MaximumLength = GroupName.Length;

    InitializeObjectAttributes(&ObjectAttributes,
                               (PUNICODE_STRING)&IopGroupOrderKeyName,
                               OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE,
                               NULL,
                               NULL);

    Status = ZwOpenKey(&OrderHandle, KEY_READ, &ObjectAttributes);
    if (!NT_SUCCESS(Status))
    {
        ExFreePool(GroupInformation);
        return Priority;
    }

    TagInformation = NULL;

    Status = IopOpenRegistryKeyEx(&OrderHandle,
                                  OrderHandle,
                                  &GroupName,
                                  KEY_READ);
    ExFreePool(GroupInformation);

    if (!NT_SUCCESS(Status)) return Priority;

    Status = IopGetRegistryValue(OrderHandle, L"", &TagInformation);
    ZwClose(OrderHandle);

    if (!NT_SUCCESS(Status)) return Priority;

    if (TagInformation->Type != REG_BINARY ||
        TagInformation->DataLength < sizeof(ULONG))
    {
        ExFreePool(TagInformation);
        return Priority;
    }

    TagList = (PULONG)((PUCHAR)TagInformation + TagInformation->DataOffset);
    TagCount = TagList[0];

    if (TagCount > (TagInformation->DataLength / sizeof(ULONG)) - 1)
    {
        TagCount = (TagInformation->DataLength / sizeof(ULONG)) - 1;
    }

    for (Index = 0; Index < TagCount; Index++)
    {
        if (TagList[Index + 1] == Tag)
        {
            Priority = (USHORT)Index;
            break;
        }
    }

    ExFreePool(TagInformation);

    return Priority;
}

NTSTATUS
IopInitializeDriverModule(
    _In_ PLDR_DATA_TABLE_ENTRY ModuleObject,
    _In_ HANDLE ServiceHandle,
    _Out_ PDRIVER_OBJECT *DriverObject,
    _Out_ NTSTATUS *DriverEntryStatus,
    _In_ BOOLEAN HoldPnpUnloadBarrier)
{
    PIMAGE_NT_HEADERS NtHeaders;
    PEXTENDED_DRIVER_EXTENSION Extension;
    OBJECT_ATTRIBUTES ObjectAttributes;
    UNICODE_STRING DriverName;
    UNICODE_STRING ServiceName;
    UNICODE_STRING RegistryPath;
    PDRIVER_OBJECT Created;
    NTSTATUS Status;
    ULONG TotalSize;
    PWCHAR Buffer;
    ULONG Index;

    PAGED_CODE();

    *DriverObject = NULL;
    *DriverEntryStatus = STATUS_SUCCESS;

    /*
     * A module with no entry point of its own is a driver library: what it has
     * instead is the DllInitialize it exports, and it owns no driver object.
     */
    NtHeaders = RtlImageNtHeader(ModuleObject->DllBase);

    if ((NtHeaders == NULL) ||
        (NtHeaders->OptionalHeader.AddressOfEntryPoint == 0))
    {
        *DriverObject = NULL;
        return MmCallDllInitialize(ModuleObject, &PsLoadedModuleList);
    }

    RtlInitEmptyUnicodeString(&DriverName, NULL, 0);
    RtlInitEmptyUnicodeString(&ServiceName, NULL, 0);
    RtlInitEmptyUnicodeString(&RegistryPath, NULL, 0);

    Status = IopGetDriverNames(ServiceHandle, &DriverName, &ServiceName);
    if (!NT_SUCCESS(Status)) return Status;

    InitializeObjectAttributes(&ObjectAttributes,
                               &DriverName,
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
                            (PVOID *)&Created);
    if (!NT_SUCCESS(Status))
    {
        ExFreePoolWithTag(DriverName.Buffer, TAG_IO_DRIVER_NAME);
        if (ServiceName.Buffer != NULL)
        {
            ExFreePoolWithTag(ServiceName.Buffer, TAG_IO_DRIVER_NAME);
        }

        return Status;
    }

    RtlZeroMemory(Created, TotalSize);

    Created->Type = IO_TYPE_DRIVER;
    Created->Size = sizeof(DRIVER_OBJECT);
    Created->DriverExtension = (PDRIVER_EXTENSION)(Created + 1);
    Created->DriverStart = ModuleObject->DllBase;
    Created->DriverSize = ModuleObject->SizeOfImage;
    Created->DriverSection = ModuleObject;
    Created->DriverInit = (PDRIVER_INITIALIZE)ModuleObject->EntryPoint;
    Created->DriverName = DriverName;

    for (Index = 0; Index <= IRP_MJ_MAXIMUM_FUNCTION; Index++)
    {
        Created->MajorFunction[Index] = IopInvalidDeviceRequest;
    }

    Extension = IoGetDrvObjExtension(Created);
    Extension->DriverObject = Created;
    Extension->ServiceKeyName = ServiceName;

    Status = ObInsertObject(Created,
                            NULL,
                            FILE_READ_DATA,
                            0,
                            (PVOID *)&Created,
                            NULL);
    if (!NT_SUCCESS(Status)) return Status;

    if (ServiceName.Length != 0)
    {
        USHORT Length = IopServicesKeyName.Length + sizeof(WCHAR) +
                        ServiceName.Length + sizeof(WCHAR);

        Buffer = ExAllocatePoolZero(NonPagedPool, Length, TAG_IO);
        if (Buffer != NULL)
        {
            RegistryPath.Buffer = Buffer;
            RegistryPath.Length = 0;
            RegistryPath.MaximumLength = Length;

            RtlAppendUnicodeStringToString(
                &RegistryPath,
                (PUNICODE_STRING)&IopServicesKeyName);
            RtlAppendUnicodeToString(&RegistryPath, L"\\");
            RtlAppendUnicodeStringToString(&RegistryPath, &ServiceName);
        }
    }

    if (HoldPnpUnloadBarrier) IopAcquirePnpUnloadBarrier(Created);

    *DriverEntryStatus = Created->DriverInit(Created, &RegistryPath);

    if (RegistryPath.Buffer != NULL)
    {
        ExFreePoolWithTag(RegistryPath.Buffer, TAG_IO);
    }

    if (!NT_SUCCESS(*DriverEntryStatus))
    {
        if (HoldPnpUnloadBarrier) IopReleasePnpUnloadBarrier(Created);

        Created->DriverSection = NULL;
        ObMakeTemporaryObject(Created);
        ObDereferenceObject(Created);

        return *DriverEntryStatus;
    }

    IopReadyDeviceObjects(Created);

    *DriverObject = Created;

    return STATUS_SUCCESS;
}

NTSTATUS
IopLoadServiceModule(
    _In_ HANDLE ServiceHandle,
    _Out_ PDRIVER_OBJECT *DriverObject)
{
    PKEY_VALUE_FULL_INFORMATION Information = NULL;
    PLDR_DATA_TABLE_ENTRY ModuleObject = NULL;
    UNICODE_STRING ServiceName;
    UNICODE_STRING ImagePath;
    UNICODE_STRING DriverName;
    NTSTATUS EntryStatus = STATUS_SUCCESS;
    PVOID ImageBase = NULL;
    NTSTATUS Status;

    PAGED_CODE();

    *DriverObject = NULL;

    RtlInitEmptyUnicodeString(&ImagePath, NULL, 0);
    RtlInitEmptyUnicodeString(&ServiceName, NULL, 0);
    RtlInitEmptyUnicodeString(&DriverName, NULL, 0);

    Status = IopGetDriverNames(ServiceHandle, &DriverName, &ServiceName);
    if (!NT_SUCCESS(Status)) return Status;

    ExFreePoolWithTag(DriverName.Buffer, TAG_IO_DRIVER_NAME);

    Status = IopGetRegistryValue(ServiceHandle, L"ImagePath", &Information);
    if (NT_SUCCESS(Status))
    {
        if (Information->Type == REG_SZ || Information->Type == REG_EXPAND_SZ)
        {
            PWCHAR Source = (PWCHAR)((PUCHAR)Information +
                                     Information->DataOffset);
            USHORT Length = (USHORT)Information->DataLength;
            PWCHAR Buffer;

            if (Length >= sizeof(WCHAR) &&
                Source[Length / sizeof(WCHAR) - 1] == UNICODE_NULL)
            {
                Length -= sizeof(WCHAR);
            }

            Buffer = ExAllocatePoolZero(NonPagedPool,
                                        Length + sizeof(WCHAR),
                                        TAG_IO);
            if (Buffer != NULL)
            {
                RtlCopyMemory(Buffer, Source, Length);

                ImagePath.Buffer = Buffer;
                ImagePath.Length = Length;
                ImagePath.MaximumLength = Length + sizeof(WCHAR);
            }
        }

        ExFreePool(Information);
    }

    Status = IopNormalizeImagePath(&ImagePath, &ServiceName);
    if (!NT_SUCCESS(Status))
    {
        if (ServiceName.Buffer != NULL)
        {
            ExFreePoolWithTag(ServiceName.Buffer, TAG_IO_DRIVER_NAME);
        }

        return Status;
    }

    if (!IopBootImageAllowed(&ImagePath, NULL))
    {
        ExFreePoolWithTag(ImagePath.Buffer, TAG_IO);
        if (ServiceName.Buffer != NULL)
        {
            ExFreePoolWithTag(ServiceName.Buffer, TAG_IO_DRIVER_NAME);
        }

        return STATUS_DRIVER_BLOCKED;
    }

    Status = MmLoadSystemImage(&ImagePath,
                               NULL,
                               NULL,
                               0,
                               (PVOID *)&ModuleObject,
                               &ImageBase);

    ExFreePoolWithTag(ImagePath.Buffer, TAG_IO);

    if (!NT_SUCCESS(Status))
    {
        if (ServiceName.Buffer != NULL)
        {
            ExFreePoolWithTag(ServiceName.Buffer, TAG_IO_DRIVER_NAME);
        }

        return Status;
    }

    if (ServiceName.Buffer != NULL)
    {
        ExFreePoolWithTag(ServiceName.Buffer, TAG_IO_DRIVER_NAME);
    }

    Status = IopInitializeDriverModule(ModuleObject,
                                       ServiceHandle,
                                       DriverObject,
                                       &EntryStatus,
                                       TRUE);
    if (!NT_SUCCESS(Status))
    {
        MmUnloadSystemImage(ModuleObject);
        return Status;
    }

    IopReleasePnpUnloadBarrier(*DriverObject);

    return STATUS_SUCCESS;
}

NTSTATUS
LdrProcessDriverModule(
    _In_ PLDR_DATA_TABLE_ENTRY LdrEntry,
    _In_ PUNICODE_STRING FileName,
    _Out_ PLDR_DATA_TABLE_ENTRY *ModuleObject)
{
    PVOID ImageBase = NULL;
    NTSTATUS Status;

    PAGED_CODE();

    Status = MmLoadSystemImage(FileName,
                               NULL,
                               NULL,
                               0,
                               (PVOID *)ModuleObject,
                               &ImageBase);
    if (Status == STATUS_IMAGE_ALREADY_LOADED)
    {
        *ModuleObject = LdrEntry;
        return STATUS_SUCCESS;
    }

    return Status;
}

static
CODE_SEG("INIT")
NTSTATUS
IopOpenServiceKeyForPath(
    _In_ PUNICODE_STRING RegistryPath,
    _Out_ PHANDLE ServiceHandle)
{
    OBJECT_ATTRIBUTES ObjectAttributes;

    InitializeObjectAttributes(&ObjectAttributes,
                               RegistryPath,
                               OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE,
                               NULL,
                               NULL);

    return ZwOpenKey(ServiceHandle, KEY_READ, &ObjectAttributes);
}

CODE_SEG("INIT")
VOID
FASTCALL
IopInitializeBootDrivers(VOID)
{
    PBOOT_DRIVER_LIST_ENTRY BootEntry;
    PDRIVER_OBJECT DriverObject;
    NTSTATUS EntryStatus;
    PLIST_ENTRY ListEntry;
    NTSTATUS Status;
    HANDLE ServiceHandle;

    PAGED_CODE();

    if (IopLoaderBlock == NULL) return;

    Status = PiInitCacheGroupInformation();
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("I/O: the service group order is unavailable (%lx)\n", Status);
    }

    ListEntry = IopLoaderBlock->BootDriverListHead.Flink;
    while (ListEntry != &IopLoaderBlock->BootDriverListHead)
    {
        BootEntry = CONTAINING_RECORD(ListEntry,
                                      BOOT_DRIVER_LIST_ENTRY,
                                      Link);
        ListEntry = ListEntry->Flink;

        if (BootEntry->LdrEntry == NULL) continue;

        Status = IopOpenServiceKeyForPath(&BootEntry->RegistryPath,
                                          &ServiceHandle);
        if (!NT_SUCCESS(Status))
        {
            DPRINT1("I/O: no service key for %wZ (%lx)\n",
                    &BootEntry->RegistryPath,
                    Status);
            continue;
        }

        Status = IopInitializeDriverModule(BootEntry->LdrEntry,
                                           ServiceHandle,
                                           &DriverObject,
                                           &EntryStatus,
                                           FALSE);

        ZwClose(ServiceHandle);

        IopBootLog(&BootEntry->FilePath, NT_SUCCESS(Status));

        if (!NT_SUCCESS(Status))
        {
            DPRINT1("I/O: the boot driver %wZ did not start (%lx)\n",
                    &BootEntry->FilePath,
                    Status);
        }
    }

    PnPBootDriversLoaded = TRUE;

    IopReinitializeBootDrivers();

    PnPBootDriversInitialized = TRUE;
}

CODE_SEG("INIT")
VOID
FASTCALL
IopInitializeSystemDrivers(VOID)
{
    PKEY_BASIC_INFORMATION Information;
    OBJECT_ATTRIBUTES ObjectAttributes;
    PDRIVER_OBJECT DriverObject;
    UNICODE_STRING ServiceName;
    HANDLE ServicesHandle;
    HANDLE ServiceHandle;
    NTSTATUS Status;
    ULONG Length;
    ULONG Index;

    PAGED_CODE();

    InitializeObjectAttributes(&ObjectAttributes,
                               (PUNICODE_STRING)&IopServicesKeyName,
                               OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE,
                               NULL,
                               NULL);

    Status = ZwOpenKey(&ServicesHandle, KEY_READ, &ObjectAttributes);
    if (!NT_SUCCESS(Status)) return;

    Length = (ULONG)(sizeof(KEY_BASIC_INFORMATION) +
                     MAXIMUM_FILENAME_LENGTH * sizeof(WCHAR));

    Information = ExAllocatePoolZero(PagedPool, Length, TAG_IO);
    if (Information == NULL)
    {
        ZwClose(ServicesHandle);
        return;
    }

    for (Index = 0; ; Index++)
    {
        ULONG Returned = 0;
        ULONG StartType = SERVICE_DISABLED;
        PKEY_VALUE_FULL_INFORMATION Value = NULL;

        Status = ZwEnumerateKey(ServicesHandle,
                                Index,
                                KeyBasicInformation,
                                Information,
                                Length,
                                &Returned);
        if (Status == STATUS_NO_MORE_ENTRIES) break;
        if (!NT_SUCCESS(Status)) continue;

        ServiceName.Buffer = Information->Name;
        ServiceName.Length = (USHORT)Information->NameLength;
        ServiceName.MaximumLength = (USHORT)Information->NameLength;

        Status = IopOpenRegistryKeyEx(&ServiceHandle,
                                      ServicesHandle,
                                      &ServiceName,
                                      KEY_READ);
        if (!NT_SUCCESS(Status)) continue;

        Status = IopGetRegistryValue(ServiceHandle, L"Start", &Value);
        if (NT_SUCCESS(Status))
        {
            if (Value->Type == REG_DWORD &&
                Value->DataLength == sizeof(ULONG))
            {
                StartType =
                    *(PULONG)((PUCHAR)Value + Value->DataOffset);
            }

            ExFreePool(Value);
        }

        if (StartType != SERVICE_SYSTEM_START)
        {
            ZwClose(ServiceHandle);
            continue;
        }

        Status = IopLoadServiceModule(ServiceHandle, &DriverObject);

        IopBootLog(&ServiceName, NT_SUCCESS(Status));

        if (NT_SUCCESS(Status)) ObDereferenceObject(DriverObject);

        ZwClose(ServiceHandle);
    }

    ExFreePoolWithTag(Information, TAG_IO);
    ZwClose(ServicesHandle);

    IopReinitializeDrivers();
}
