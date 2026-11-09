/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     A section a scanner maps to read a file it is being asked about
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
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
    LARGE_INTEGER FileSize;
    LARGE_INTEGER SectionSize;
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

    if (FileObject->SectionObjectPointer == NULL)
    {
        return STATUS_INVALID_DEVICE_REQUEST;
    }

    Status = FsRtlGetFileSize(FileObject, &FileSize);
    if (!NT_SUCCESS(Status)) return Status;

    if (FileSize.QuadPart == 0) return STATUS_END_OF_FILE;

    SectionSize = (MaximumSize != NULL) ? *MaximumSize : FileSize;

    /* TODO: Mm does not yet offer a section create that skips the file system's section synchronisation, so a scanner called from a create callback can still recurse into the file system */
    Status = MmCreateSection(&Section,
                             DesiredAccess,
                             ObjectAttributes,
                             &SectionSize,
                             SectionPageProtection,
                             AllocationAttributes,
                             NULL,
                             FileObject);

    if (!NT_SUCCESS(Status)) return Status;

    Status = ObOpenObjectByPointer(Section,
                                   OBJ_KERNEL_HANDLE,
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
