/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     The per-processor control space the host reads by index
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#include "kdpriv.h"
#define NDEBUG
#include <debug.h>

/* PRIVATE FUNCTIONS **********************************************************/

/*
 * Finds what the host is asking for. The index is one of the four things a
 * processor has that are not registers; the first three of them are answered
 * with the address of the structure, which is what the host then reads from
 * memory, and the fourth with the registers themselves.
 */
static
NTSTATUS
KdpControlSpaceAddress(
    _In_ ULONG Processor,
    _In_ ULONG64 BaseAddress,
    _Out_ PVOID *Source,
    _Out_ PULONG SourceLength,
    _Out_ PULONG_PTR Scratch)
{
    PKPRCB Prcb;
    PKIPCR Pcr;

    *Source = NULL;
    *SourceLength = 0;
    *Scratch = 0;

    if (Processor >= (ULONG)(CCHAR)KeNumberProcessors)
        return STATUS_INVALID_PARAMETER;

    Prcb = KiProcessorBlock[Processor];
    if (Prcb == NULL)
        return STATUS_INVALID_PARAMETER;

    Pcr = CONTAINING_RECORD(Prcb, KIPCR, Prcb);

    switch (BaseAddress)
    {
        case AMD64_DEBUG_CONTROL_SPACE_KPCR:
            *Scratch = (ULONG_PTR)Pcr;
            *Source = Scratch;
            *SourceLength = sizeof(ULONG_PTR);
            return STATUS_SUCCESS;

        case AMD64_DEBUG_CONTROL_SPACE_KPRCB:
            *Scratch = (ULONG_PTR)Prcb;
            *Source = Scratch;
            *SourceLength = sizeof(ULONG_PTR);
            return STATUS_SUCCESS;

        case AMD64_DEBUG_CONTROL_SPACE_KSPECIAL:
            *Source = &Prcb->ProcessorState.SpecialRegisters;
            *SourceLength = sizeof(KSPECIAL_REGISTERS);
            return STATUS_SUCCESS;

        case AMD64_DEBUG_CONTROL_SPACE_KTHREAD:
            *Scratch = (ULONG_PTR)Prcb->CurrentThread;
            *Source = Scratch;
            *SourceLength = sizeof(ULONG_PTR);
            return STATUS_SUCCESS;

        default:
            return STATUS_INVALID_PARAMETER;
    }
}

/* PUBLIC FUNCTIONS ***********************************************************/

NTSTATUS
NTAPI
KdpSysReadControlSpace(
    _In_ ULONG Processor,
    _In_ ULONG64 BaseAddress,
    _Out_writes_bytes_(Length) PVOID Buffer,
    _In_ ULONG Length,
    _Out_ PULONG ActualLength)
{
    PVOID Source;
    ULONG SourceLength;
    ULONG_PTR Scratch;
    NTSTATUS Status;

    *ActualLength = 0;

    Status = KdpControlSpaceAddress(Processor,
                                    BaseAddress,
                                    &Source,
                                    &SourceLength,
                                    &Scratch);
    if (!NT_SUCCESS(Status))
        return Status;

    if (Length > SourceLength)
        Length = SourceLength;

    KdpMoveMemory(Buffer, Source, Length);
    *ActualLength = Length;

    return STATUS_SUCCESS;
}

/*
 * Only the special registers can be written: the addresses of a processor's
 * own structures are not the host's to change.
 */
NTSTATUS
NTAPI
KdpSysWriteControlSpace(
    _In_ ULONG Processor,
    _In_ ULONG64 BaseAddress,
    _In_reads_bytes_(Length) PVOID Buffer,
    _In_ ULONG Length,
    _Out_ PULONG ActualLength)
{
    PKPRCB Prcb;

    *ActualLength = 0;

    if (BaseAddress != AMD64_DEBUG_CONTROL_SPACE_KSPECIAL)
        return STATUS_INVALID_PARAMETER;

    if (Processor >= (ULONG)(CCHAR)KeNumberProcessors)
        return STATUS_INVALID_PARAMETER;

    Prcb = KiProcessorBlock[Processor];
    if (Prcb == NULL)
        return STATUS_INVALID_PARAMETER;

    if (Length > sizeof(KSPECIAL_REGISTERS))
        Length = sizeof(KSPECIAL_REGISTERS);

    KdpMoveMemory(&Prcb->ProcessorState.SpecialRegisters, Buffer, Length);
    *ActualLength = Length;

    return STATUS_SUCCESS;
}

/* EOF */
