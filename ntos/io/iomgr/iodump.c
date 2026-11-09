/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Holding on to what a crash dump will need
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#include "../ioint.h"
#define NDEBUG
#include <debug.h>

/* GLOBALS ********************************************************************/

ULONG IopNumTriageDumpDataBlocks = 0;
PVOID IopTriageDumpDataBlocks[64];

/* PRIVATE DATA ***************************************************************/

static HANDLE IopCrashDumpFileHandle = NULL;
static PFILE_OBJECT IopCrashDumpFileObject = NULL;
static UNICODE_STRING IopCrashDumpBootDevice;

/* PUBLIC FUNCTIONS ***********************************************************/

BOOLEAN
NTAPI
IoInitializeCrashDumpCore(
    IN PLOADER_PARAMETER_BLOCK LoaderBlock OPTIONAL)
{
    UNREFERENCED_PARAMETER(LoaderBlock);

    IopNumTriageDumpDataBlocks = 0;
    RtlZeroMemory(IopTriageDumpDataBlocks, sizeof(IopTriageDumpDataBlocks));

    RtlInitEmptyUnicodeString(&IopCrashDumpBootDevice, NULL, 0);

    /* TODO: the dump stack lives in the dump port driver, which is not loaded
     * at this point */
    return FALSE;
}

BOOLEAN
NTAPI
IoInitializeCrashDump(
    IN HANDLE DumpFileHandle)
{
    PFILE_OBJECT FileObject;
    NTSTATUS Status;

    PAGED_CODE();

    if (IopCrashDumpFileHandle != NULL) return FALSE;

    Status = ObReferenceObjectByHandle(DumpFileHandle,
                                       FILE_READ_DATA | FILE_WRITE_DATA,
                                       IoFileObjectType,
                                       KernelMode,
                                       (PVOID *)&FileObject,
                                       NULL);
    if (!NT_SUCCESS(Status)) return FALSE;

    IopCrashDumpFileHandle = DumpFileHandle;
    IopCrashDumpFileObject = FileObject;

    /* TODO: the dump stack is not built, so only the file is held open */
    return TRUE;
}

BOOLEAN
NTAPI
IoInitializeDedicatedCrashDump(
    IN PFILE_OBJECT BootFileObject)
{
    PAGED_CODE();

    if (BootFileObject == NULL) return FALSE;
    if (IopCrashDumpFileObject != NULL) return FALSE;

    ObReferenceObject(BootFileObject);
    IopCrashDumpFileObject = BootFileObject;

    /* TODO: a dedicated dump file needs the dump stack to reserve its runs */
    return TRUE;
}

BOOLEAN
NTAPI
IoInitializeBootPageFileCrashDump(
    IN PCUNICODE_STRING BootDeviceName)
{
    PWCHAR Buffer;

    PAGED_CODE();

    if (BootDeviceName == NULL || BootDeviceName->Length == 0) return FALSE;

    if (IopCrashDumpBootDevice.Buffer != NULL) return FALSE;

    Buffer = ExAllocatePoolZero(NonPagedPool,
                                BootDeviceName->Length + sizeof(WCHAR),
                                TAG_IO);
    if (Buffer == NULL) return FALSE;

    RtlCopyMemory(Buffer, BootDeviceName->Buffer, BootDeviceName->Length);

    IopCrashDumpBootDevice.Buffer = Buffer;
    IopCrashDumpBootDevice.Length = BootDeviceName->Length;
    IopCrashDumpBootDevice.MaximumLength =
        BootDeviceName->Length + sizeof(WCHAR);

    /* TODO: the page file's extents are claimed once the dump stack exists */
    return TRUE;
}

BOOLEAN
NTAPI
IoReleaseCrashDumpFile(VOID)
{
    PAGED_CODE();

    if (IopCrashDumpFileObject == NULL) return FALSE;

    ObDereferenceObject(IopCrashDumpFileObject);
    IopCrashDumpFileObject = NULL;

    if (IopCrashDumpFileHandle != NULL)
    {
        ZwClose(IopCrashDumpFileHandle);
        IopCrashDumpFileHandle = NULL;
    }

    if (IopCrashDumpBootDevice.Buffer != NULL)
    {
        ExFreePoolWithTag(IopCrashDumpBootDevice.Buffer, TAG_IO);
        RtlInitEmptyUnicodeString(&IopCrashDumpBootDevice, NULL, 0);
    }

    return TRUE;
}

VOID
NTAPI
IoPublishCrashDumpArtifacts(VOID)
{
    PAGED_CODE();

    /* TODO: no dump artifacts are produced until the dump stack exists */
    DPRINT("I/O: no crash dump artifacts to publish\n");
}
