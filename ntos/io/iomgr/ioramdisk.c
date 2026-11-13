/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Turning the loader's RAM image into a disk to boot from
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#define INITGUID
#include <ntoskrnl.h>
#include "../ioint.h"
#include <ntddrdsk.h>
#define NDEBUG
#include <debug.h>

/* PRIVATE FUNCTIONS **********************************************************/

static
CODE_SEG("INIT")
PMEMORY_ALLOCATION_DESCRIPTOR
IopFindXipRomDescriptor(
    _In_ PLOADER_PARAMETER_BLOCK LoaderBlock)
{
    PMEMORY_ALLOCATION_DESCRIPTOR Descriptor;
    PLIST_ENTRY Entry;

    Entry = LoaderBlock->MemoryDescriptorListHead.Flink;
    while (Entry != &LoaderBlock->MemoryDescriptorListHead)
    {
        Descriptor = CONTAINING_RECORD(Entry,
                                       MEMORY_ALLOCATION_DESCRIPTOR,
                                       ListEntry);
        Entry = Entry->Flink;

        if (Descriptor->MemoryType == LoaderXIPRom) return Descriptor;
    }

    return NULL;
}

static
CODE_SEG("INIT")
NTSTATUS
IopReadLoadOptionNumber(
    _In_ PCSTR LoadOptions,
    _In_ PCSTR Option,
    _Out_ PULONG Value)
{
    PCSTR Found;
    ULONG Result = 0;
    ULONG Digit;

    *Value = 0;

    if (LoadOptions == NULL) return STATUS_NOT_FOUND;

    Found = strstr(LoadOptions, Option);
    if (Found == NULL) return STATUS_NOT_FOUND;

    Found += strlen(Option);
    if (*Found != '=') return STATUS_INVALID_PARAMETER;
    Found++;

    if (*Found < '0' || *Found > '9') return STATUS_INVALID_PARAMETER;

    while (*Found >= '0' && *Found <= '9')
    {
        Digit = (ULONG)(*Found - '0');

        if (Result > (MAXULONG - Digit) / 10)
        {
            return STATUS_INTEGER_OVERFLOW;
        }

        Result = Result * 10 + Digit;
        Found++;
    }

    *Value = Result;

    return STATUS_SUCCESS;
}

/* PUBLIC FUNCTIONS ***********************************************************/

CODE_SEG("INIT")
NTSTATUS
NTAPI
IopStartRamdisk(
    IN PLOADER_PARAMETER_BLOCK LoaderBlock)
{
    PMEMORY_ALLOCATION_DESCRIPTOR Descriptor;
    OBJECT_ATTRIBUTES ObjectAttributes;
    IO_STATUS_BLOCK IoStatusBlock;
    UNICODE_STRING DeviceName, GuidString, DeviceString, ArcName, LinkName;
    ANSI_STRING AnsiArcName;
    WCHAR DeviceBuffer[64];
    WCHAR LinkBuffer[64];
    RAMDISK_CREATE_INPUT Input;
    PDEVICE_OBJECT BusDevice;
    PFILE_OBJECT BusFileObject;
    ULONG Offset = 0;
    ULONG Length = 0;
    NTSTATUS Status;
    HANDLE Handle;

    PAGED_CODE();

    Descriptor = IopFindXipRomDescriptor(LoaderBlock);
    if (Descriptor == NULL)
    {
        KeBugCheckEx(INACCESSIBLE_BOOT_DEVICE,
                     RD_NO_XIPROM_DESCRIPTOR,
                     0,
                     0,
                     0);
    }

    Status = IopReadLoadOptionNumber(LoaderBlock->LoadOptions,
                                     "RDIMAGEOFFSET",
                                     &Offset);
    if (Status == STATUS_INVALID_PARAMETER ||
        Status == STATUS_INTEGER_OVERFLOW)
    {
        KeBugCheckEx(INACCESSIBLE_BOOT_DEVICE, RD_INVALID_OFFSET, 0, 0, 0);
    }

    Status = IopReadLoadOptionNumber(LoaderBlock->LoadOptions,
                                     "RDIMAGELENGTH",
                                     &Length);
    if (Status == STATUS_INVALID_PARAMETER ||
        Status == STATUS_INTEGER_OVERFLOW)
    {
        KeBugCheckEx(INACCESSIBLE_BOOT_DEVICE, RD_INVALID_LENGTH, 0, 0, 0);
    }

    if (Length == 0)
    {
        Length = (ULONG)(Descriptor->PageCount << PAGE_SHIFT);
    }

    RtlInitUnicodeString(&DeviceName, L"\\Device\\Ramdisk");

    InitializeObjectAttributes(&ObjectAttributes,
                               &DeviceName,
                               OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE,
                               NULL,
                               NULL);

    Status = ZwOpenFile(&Handle,
                        FILE_ALL_ACCESS,
                        &ObjectAttributes,
                        &IoStatusBlock,
                        0,
                        FILE_SYNCHRONOUS_IO_NONALERT);
    if (!NT_SUCCESS(Status))
    {
        KeBugCheckEx(INACCESSIBLE_BOOT_DEVICE,
                     RD_NO_RAMDISK_DRIVER,
                     Status,
                     0,
                     0);
    }

    Status = ObReferenceObjectByHandle(Handle,
                                       0,
                                       IoFileObjectType,
                                       KernelMode,
                                       (PVOID *)&BusFileObject,
                                       NULL);
    if (!NT_SUCCESS(Status))
    {
        KeBugCheckEx(INACCESSIBLE_BOOT_DEVICE,
                     RD_NO_RAMDISK_DRIVER,
                     Status,
                     0,
                     0);
    }

    RtlZeroMemory(&Input, sizeof(Input));

    Input.Version = sizeof(Input);
    Input.DiskGuid = RAMDISK_BOOTDISK_GUID;
    Input.DiskType = RAMDISK_BOOT_DISK;
    Input.Options.Fixed = TRUE;
    Input.BasePage = Descriptor->BasePage;
    Input.DiskOffset = Offset;
    Input.DiskLength.QuadPart = Length;

    Status = ZwDeviceIoControlFile(Handle,
                                   NULL,
                                   NULL,
                                   NULL,
                                   &IoStatusBlock,
                                   FSCTL_CREATE_RAM_DISK,
                                   &Input,
                                   sizeof(Input),
                                   NULL,
                                   0);

    ZwClose(Handle);

    if (!NT_SUCCESS(Status))
    {
        KeBugCheckEx(INACCESSIBLE_BOOT_DEVICE,
                     RD_FSCTL_FAILED,
                     Status,
                     0,
                     0);
    }

    /* The mount manager learns a volume from its interface arrival, which only the start of the disk runs */
    Status = IoGetRelatedTargetDevice(BusFileObject, &BusDevice);
    if (NT_SUCCESS(Status))
    {
        Status = IoSynchronousInvalidateDeviceRelations(BusDevice, BusRelations);
        ObDereferenceObject(BusDevice);
    }

    ObDereferenceObject(BusFileObject);
    if (!NT_SUCCESS(Status))
    {
        KeBugCheckEx(INACCESSIBLE_BOOT_DEVICE,
                     RD_ENUMERATE_FAILED,
                     Status,
                     0,
                     0);
    }

    Status = RtlStringFromGUID(&Input.DiskGuid, &GuidString);
    if (!NT_SUCCESS(Status))
    {
        KeBugCheckEx(INACCESSIBLE_BOOT_DEVICE,
                     RD_GUID_CONVERT_FAILED,
                     Status,
                     0,
                     0);
    }

    RtlInitEmptyUnicodeString(&DeviceString,
                              DeviceBuffer,
                              sizeof(DeviceBuffer));
    Status = RtlAppendUnicodeToString(&DeviceString, L"\\Device\\Ramdisk");
    if (NT_SUCCESS(Status))
    {
        Status = RtlAppendUnicodeStringToString(&DeviceString, &GuidString);
    }

    RtlFreeUnicodeString(&GuidString);

    if (!NT_SUCCESS(Status))
    {
        KeBugCheckEx(INACCESSIBLE_BOOT_DEVICE,
                     RD_NAME_BUILD_FAILED,
                     Status,
                     0,
                     0);
    }

    RtlInitAnsiString(&AnsiArcName, LoaderBlock->ArcBootDeviceName);
    Status = RtlAnsiStringToUnicodeString(&ArcName, &AnsiArcName, TRUE);
    if (!NT_SUCCESS(Status))
    {
        KeBugCheckEx(INACCESSIBLE_BOOT_DEVICE,
                     RD_SYMLINK_CREATE_FAILED,
                     Status,
                     0,
                     0);
    }

    RtlInitEmptyUnicodeString(&LinkName, LinkBuffer, sizeof(LinkBuffer));
    Status = RtlAppendUnicodeToString(&LinkName, L"\\ArcName\\");
    if (NT_SUCCESS(Status))
    {
        Status = RtlAppendUnicodeStringToString(&LinkName, &ArcName);
    }

    RtlFreeUnicodeString(&ArcName);

    if (!NT_SUCCESS(Status))
    {
        KeBugCheckEx(INACCESSIBLE_BOOT_DEVICE,
                     RD_NAME_BUILD_FAILED,
                     Status,
                     0,
                     0);
    }

    Status = IoCreateSymbolicLink(&LinkName, &DeviceString);
    if (Status == STATUS_OBJECT_NAME_COLLISION) Status = STATUS_SUCCESS;
    if (!NT_SUCCESS(Status))
    {
        KeBugCheckEx(INACCESSIBLE_BOOT_DEVICE,
                     RD_SYMLINK_CREATE_FAILED,
                     Status,
                     0,
                     0);
    }

    return STATUS_SUCCESS;
}
