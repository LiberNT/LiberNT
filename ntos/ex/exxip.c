/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Running from read-only memory instead of from a disk
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

/* PRIVATE DATA ***************************************************************/

/*
 * Where the image the system is running from lives, when it is being run where
 * it lies rather than read into memory first.
 */
static PHYSICAL_ADDRESS ExpXipPhysicalBase;
static BOOLEAN ExpXipEnabled;

/* PRIVATE FUNCTIONS **********************************************************/

/*
 * Finds the run of memory the loader marked as holding the image, which is the
 * run the system is to be read from rather than written over.
 */
PMEMORY_ALLOCATION_DESCRIPTOR
NTAPI
XIPpFindMemoryDescriptor(
    _In_ PLOADER_PARAMETER_BLOCK LoaderBlock,
    _In_ TYPE_OF_MEMORY MemoryType)
{
    PLIST_ENTRY Entry;

    for (Entry = LoaderBlock->MemoryDescriptorListHead.Flink;
         Entry != &LoaderBlock->MemoryDescriptorListHead;
         Entry = Entry->Flink)
    {
        PMEMORY_ALLOCATION_DESCRIPTOR Descriptor =
            CONTAINING_RECORD(Entry, MEMORY_ALLOCATION_DESCRIPTOR, ListEntry);

        if (Descriptor->MemoryType == MemoryType)
        {
            return Descriptor;
        }
    }

    return NULL;
}

/* PUBLIC FUNCTIONS ***********************************************************/

CODE_SEG("INIT")
VOID
NTAPI
XIPInit(
    _In_ PLOADER_PARAMETER_BLOCK LoaderBlock)
{
    PMEMORY_ALLOCATION_DESCRIPTOR Descriptor;

    ExpXipEnabled = FALSE;

    Descriptor = XIPpFindMemoryDescriptor(LoaderBlock, LoaderXIPRom);
    if (Descriptor == NULL)
    {
        return;
    }

    ExpXipPhysicalBase.QuadPart = (LONGLONG)Descriptor->BasePage << PAGE_SHIFT;
    ExpXipEnabled = TRUE;
}

NTSTATUS
NTAPI
XIPDispatch(
    _In_ ULONG Command,
    _Out_opt_ PVOID Buffer,
    _In_ ULONG BufferSize)
{
    if (!ExpXipEnabled)
    {
        return STATUS_NOT_SUPPORTED;
    }

    /*
     * The only thing a caller can ask is where the image lies, so it can read
     * from it instead of from a disk.
     */
    if (Command != 0)
    {
        /* TODO: no command other than the one that reports the image's place
           is served. */
        return STATUS_NOT_IMPLEMENTED;
    }

    if ((Buffer == NULL) || (BufferSize < sizeof(PHYSICAL_ADDRESS)))
    {
        return STATUS_BUFFER_TOO_SMALL;
    }

    *(PPHYSICAL_ADDRESS)Buffer = ExpXipPhysicalBase;

    return STATUS_SUCCESS;
}

/* EOF */
