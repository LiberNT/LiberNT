/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     A section a scanner maps to read a file it is being asked about
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#include "fsrtlp.h"
#define NDEBUG
#include <debug.h>

/* PUBLIC FUNCTIONS ***********************************************************/

NTSTATUS
NTAPI
FsRtlCreateSectionForDataScan(
    _Out_ PHANDLE SectionHandle,
    _Outptr_ PVOID *SectionObject,
    _Out_opt_ PLARGE_INTEGER SectionFileSize,
    _In_ PFILE_OBJECT FileObject,
    _In_ ACCESS_MASK DesiredAccess,
    _In_opt_ POBJECT_ATTRIBUTES ObjectAttributes,
    _In_opt_ PLARGE_INTEGER MaximumSize,
    _In_ ULONG SectionPageProtection,
    _In_ ULONG AllocationAttributes,
    _In_ ULONG Flags)
{
    FILE_STANDARD_INFORMATION Information;
    LARGE_INTEGER FileSize;
    LARGE_INTEGER SectionSize;
    PDEVICE_OBJECT DeviceObject;
    ULONG HandleAttributes = 0;
    PVOID Section;
    HANDLE Handle;
    NTSTATUS Status;

    PAGED_CODE();

    if ((SectionHandle == NULL) || (SectionObject == NULL) ||
        (FileObject == NULL) || (Flags != 0))
    {
        return STATUS_INVALID_PARAMETER;
    }

    *SectionHandle = NULL;
    *SectionObject = NULL;

    /* A scanner reads, and may write back what it repaired; nothing else is a scan */
    if ((SectionPageProtection != PAGE_READONLY) &&
        (SectionPageProtection != PAGE_READWRITE))
    {
        return STATUS_INVALID_PARAMETER_8;
    }

    if ((AllocationAttributes & ~(ULONG)SEC_FILE) != SEC_COMMIT)
    {
        return STATUS_INVALID_PARAMETER_9;
    }

    DeviceObject = IoGetRelatedDeviceObject(FileObject);
    if (DeviceObject == NULL) return STATUS_INVALID_DEVICE_REQUEST;

    Status = FsRtlpQueryStandardInformation(FileObject,
                                            DeviceObject,
                                            &Information);
    if (!NT_SUCCESS(Status)) return Status;

    if (Information.Directory) return STATUS_FILE_IS_A_DIRECTORY;

    if (FileObject->SectionObjectPointer == NULL)
    {
        return STATUS_INVALID_DEVICE_REQUEST;
    }

    FileSize = Information.EndOfFile;

    if (FileSize.QuadPart == 0) return STATUS_END_OF_FILE;

    SectionSize = (MaximumSize != NULL) ? *MaximumSize : FileSize;

    /* TODO: the file system's section synchronisation is still taken, so a scanner called from a create callback can recurse into it */
    Status = MmCreateSectionForDataScan(&Section,
                                        DesiredAccess,
                                        ObjectAttributes,
                                        &SectionSize,
                                        SectionPageProtection,
                                        AllocationAttributes,
                                        FileObject);

    if (!NT_SUCCESS(Status)) return Status;

    /* The handle is the caller's kind: a kernel one only where the caller asked for it */
    if (ObjectAttributes != NULL)
    {
        HandleAttributes = ObjectAttributes->Attributes & OBJ_KERNEL_HANDLE;
    }

    Status = ObOpenObjectByPointer(Section,
                                   HandleAttributes,
                                   NULL,
                                   DesiredAccess,
                                   MmSectionObjectType,
                                   KernelMode,
                                   &Handle);

    if (!NT_SUCCESS(Status))
    {
        ObDereferenceObject(Section);
        return Status;
    }

    *SectionHandle = Handle;
    *SectionObject = Section;
    if (SectionFileSize != NULL) *SectionFileSize = FileSize;

    return STATUS_SUCCESS;
}

/* EOF */
