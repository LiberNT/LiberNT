/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     i386 process LDT services
 * COPYRIGHT:   Copyright Stefan Ginsberg <stefan.ginsberg@reactos.org>
 *              Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

VOID
NTAPI
PspDeleteLdt(
    PEPROCESS Process)
{
    /* FIXME - LdtInformation must be null as long as we don't implement VDMs */
    ASSERT(Process->LdtInformation == NULL);
}

VOID
NTAPI
PspDeleteVdmObjects(
    PEPROCESS Process)
{
    if (Process->VdmObjects == NULL)
        return;

    /* FIXME: Need to do more than just freeing the main VdmObjects member! */
    UNIMPLEMENTED;

    ExFreePoolWithTag(Process->VdmObjects, TAG_KERNEL);
    Process->VdmObjects = NULL;
}

NTSTATUS
NTAPI
PspQueryDescriptorThread(
    _In_ PETHREAD Thread,
    _In_ PVOID ThreadInformation,
    _In_ ULONG ThreadInformationLength,
    _Out_opt_ PULONG ReturnLength)
{
    DESCRIPTOR_TABLE_ENTRY DescriptorEntry;
    LDT_ENTRY Descriptor;
    NTSTATUS Status;
    PAGED_CODE();

    if (ThreadInformationLength != sizeof(DESCRIPTOR_TABLE_ENTRY))
    {
        return STATUS_INFO_LENGTH_MISMATCH;
    }

    _SEH2_TRY
    {
        RtlCopyMemory(&DescriptorEntry,
                      ThreadInformation,
                      sizeof(DESCRIPTOR_TABLE_ENTRY));
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        _SEH2_YIELD(return _SEH2_GetExceptionCode());
    }
    _SEH2_END;

    if (!(DescriptorEntry.Selector & 0x4))
    {
        Status = Ke386GetGdtEntryThread(&Thread->Tcb,
                                        DescriptorEntry.Selector & 0xFFFFFFF8,
                                        (PKGDTENTRY)&Descriptor);
        if (!NT_SUCCESS(Status)) return Status;
    }
    else
    {
        PUSHORT LdtDescriptor = (PUSHORT)&THREAD_TO_PROCESS(Thread)->Pcb.LdtDescriptor;
        ULONG LdtBase, LdtLimit;
        ULONG Offset = DescriptorEntry.Selector & 0xFFFFFFF8;

        LdtBase = LdtDescriptor[1] |
                  ((LdtDescriptor[2] & 0xff) << 16) |
                  ((LdtDescriptor[3] & ~0xff) << 16);
        LdtLimit = LdtDescriptor[0] |
                   ((LdtDescriptor[3] & 0xf) << 16);

        if (!LdtBase) return STATUS_NO_LDT;
        if (Offset + sizeof(LDT_ENTRY) - 1 > LdtLimit) return STATUS_ACCESS_VIOLATION;

        Descriptor = *(PLDT_ENTRY)(LdtBase + Offset);
    }

    _SEH2_TRY
    {
        /* Copy the entry to caller */
        RtlCopyMemory(&((PDESCRIPTOR_TABLE_ENTRY)ThreadInformation)->
                      Descriptor,
                      &Descriptor,
                      sizeof(LDT_ENTRY));
        if (ReturnLength) *ReturnLength = sizeof(LDT_ENTRY);
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        _SEH2_YIELD(return _SEH2_GetExceptionCode());
    }
    _SEH2_END;

    return STATUS_SUCCESS;
}
