/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF
 * PURPOSE:     LoongArch64 explicit fault recovery for architecture-owned
 *              user copies
 */

#include <ntoskrnl.h>
#include "la64ke.h"
#define NDEBUG
#include <debug.h>

extern UCHAR KiLa64CopyFromUserFault[], KiLa64CopyToUserFault[], KiLa64CopyUserFailure[];
extern UCHAR KiLa64ReadKernelFault[];
NTSTATUS NTAPI KiLa64ReadKernelRaw(PVOID Destination, const VOID *Source, SIZE_T Length);
NTSTATUS NTAPI KiLa64CopyFromUserRaw(PVOID Destination, const VOID *Source, SIZE_T Length);
NTSTATUS NTAPI KiLa64CopyToUserRaw(PVOID Destination, const VOID *Source, SIZE_T Length);

static
BOOLEAN
KiLa64UserRange(_In_ const VOID *Address, _In_ SIZE_T Length)
{
    ULONG_PTR Start = (ULONG_PTR)Address;
    ULONG_PTR Limit = (ULONG_PTR)MmUserProbeAddress;

    return (Start >= MM_ALLOCATION_GRANULARITY) && (Start < Limit) && (Length <= Limit - Start);
}

/* Kernel code reaches a U page through the ordinary TLB entry, and hardware
 * still refuses a page that is not PLV 3. Require the actual user
 * permissions, not merely a low address; the assembly fixup is still needed
 * if a mapping changes after the probe. */
static
NTSTATUS
KiLa64ProbeUserPages(_In_ const VOID *Address, _In_ SIZE_T Length, _In_ BOOLEAN Write)
{
    ULONG_PTR Page = ALIGN_DOWN_BY((ULONG_PTR)Address, PAGE_SIZE);
    ULONG_PTR Last = ALIGN_DOWN_BY((ULONG_PTR)Address + Length - 1, PAGE_SIZE);
    ULONG64 Access = MI_LA64_PTE_VALID | MI_LA64_PTE_PRESENT | MI_LA64_PTE_PLV_USER;
    MI_LA64_PAGE_WALK Walk;
    NTSTATUS Status;
    ULONG Fault;

    if (Write)
        Access |= MI_LA64_PTE_WRITE | MI_LA64_PTE_DIRTY;

    for (;;)
    {
        Status = MiLa64WalkCurrentPageTables((PVOID)Page, &Walk);
        if (!NT_SUCCESS(Status) || ((Walk.Value.u.Long & Access) != Access))
        {
            Fault = Write ? MI_LA64_FAULT_WRITE : 0;
            if (NT_SUCCESS(Status))
                Fault |= MI_LA64_FAULT_PRESENT;
            Status = MmAccessFault(Fault, (PVOID)Page, UserMode, KeGetCurrentThread()->TrapFrame);
            if (!NT_SUCCESS(Status))
                return Status;
            Status = MiLa64WalkCurrentPageTables((PVOID)Page, &Walk);
            if (!NT_SUCCESS(Status) || ((Walk.Value.u.Long & Access) != Access))
                return STATUS_ACCESS_VIOLATION;
        }
        if (Page == Last)
            return STATUS_SUCCESS;
        Page += PAGE_SIZE;
    }
}

NTSTATUS
NTAPI
KiLa64CopyFromUser(_Out_writes_bytes_(Length) PVOID Destination, _In_ const VOID *Source, _In_ SIZE_T Length)
{
    NTSTATUS Status;

    ASSERT(KeGetCurrentIrql() <= APC_LEVEL);
    if (!Length)
        return STATUS_SUCCESS;
    if (!KiLa64UserRange(Source, Length))
        return STATUS_ACCESS_VIOLATION;
    Status = KiLa64ProbeUserPages(Source, Length, FALSE);
    if (!NT_SUCCESS(Status))
        return Status;
    return KiLa64CopyFromUserRaw(Destination, Source, Length);
}

NTSTATUS
NTAPI
KiLa64CopyToUser(_Out_ PVOID Destination, _In_reads_bytes_(Length) const VOID *Source, _In_ SIZE_T Length)
{
    NTSTATUS Status;

    ASSERT(KeGetCurrentIrql() <= APC_LEVEL);
    if (!Length)
        return STATUS_SUCCESS;
    if (!KiLa64UserRange(Destination, Length))
        return STATUS_ACCESS_VIOLATION;
    Status = KiLa64ProbeUserPages(Destination, Length, TRUE);
    if (!NT_SUCCESS(Status))
        return Status;
    return KiLa64CopyToUserRaw(Destination, Source, Length);
}

BOOLEAN
NTAPI
KiLa64FixupUserCopy(_Inout_ PKTRAP_FRAME Frame, _In_ NTSTATUS Status)
{
    ULONG_PTR Pc = Frame->Context.Pc;
    ULONG Ecode = (Frame->Estat >> LA64_ESTAT_ECODE_SHIFT) & LA64_ESTAT_ECODE_MASK;
    ULONG_PTR UserAddress;

    if (KiLa64UserTrap(Frame))
        return FALSE;

    /* The kernel metadata reader has exactly one recoverable access: the
     * source load, with the source in $a1. */
    if ((Pc == (ULONG_PTR)KiLa64ReadKernelFault) &&
        (Ecode == LA64_ECODE_PIL) &&
        (Frame->Badv == Frame->Context.A1))
    {
        Frame->Context.A0 = (ULONG_PTR)(LONG_PTR)Status;
        Frame->Context.Pc = (ULONG_PTR)KiLa64CopyUserFailure;
        return TRUE;
    }

    if ((Pc == (ULONG_PTR)KiLa64CopyFromUserFault) && (Ecode == LA64_ECODE_PIL))
        UserAddress = Frame->Context.A1;
    else if ((Pc == (ULONG_PTR)KiLa64CopyToUserFault) && (Ecode == LA64_ECODE_PIS))
        UserAddress = Frame->Context.A0;
    else
        return FALSE;

    if ((Frame->Badv != UserAddress) || !KiLa64UserRange((PVOID)UserAddress, 1))
        return FALSE;

    Frame->Context.A0 = (ULONG_PTR)(LONG_PTR)Status;
    Frame->Context.Pc = (ULONG_PTR)KiLa64CopyUserFailure;
    return TRUE;
}

NTSTATUS
NTAPI
KiLa64ReadMemory(_Out_writes_bytes_(Length) PVOID Destination, _In_ const VOID *Source, _In_ SIZE_T Length)
{
    ULONG_PTR Address = (ULONG_PTR)Source, Last, Page;
    MI_LA64_PAGE_WALK Walk;

    if (!Length)
        return STATUS_SUCCESS;
    if (Address < (ULONG_PTR)MmSystemRangeStart)
        return KiLa64CopyFromUser(Destination, Source, Length);
    if (Length - 1 > MAXULONG_PTR - Address)
        return STATUS_ACCESS_VIOLATION;

    Last = ALIGN_DOWN_BY(Address + Length - 1, PAGE_SIZE);
    for (Page = ALIGN_DOWN_BY(Address, PAGE_SIZE); ; Page += PAGE_SIZE)
    {
        if (!NT_SUCCESS(MiLa64WalkCurrentPageTables((PVOID)Page, &Walk)) ||
            !(Walk.Value.u.Long & (MI_LA64_PTE_VALID | MI_LA64_PTE_PRESENT)))
        {
            return STATUS_ACCESS_VIOLATION;
        }
        if (Page == Last)
            break;
    }
    return KiLa64ReadKernelRaw(Destination, Source, Length);
}

/* NtReadVirtualMemory's current-process user path must be fault-safe before
 * the user dispatcher can consume metadata. Both sides are U pages, copied
 * through a bounded kernel buffer using the existing fixups. */
NTSTATUS
NTAPI
KiLa64ReadCurrentProcess(PVOID Source, PVOID Destination, SIZE_T Length,
                         PSIZE_T Returned)
{
    UCHAR Buffer[256];
    SIZE_T Done = 0, Chunk;
    NTSTATUS Status = STATUS_SUCCESS, CopyStatus;

    if (Length && (!KiLa64UserRange(Source, Length) ||
                   !KiLa64UserRange(Destination, Length)))
    {
        return STATUS_ACCESS_VIOLATION;
    }
    if (Returned)
    {
        Status = KiLa64CopyToUser(Returned, &Done, sizeof(Done));
        if (!NT_SUCCESS(Status))
            return Status;
    }
    while (Done < Length)
    {
        Chunk = min(Length - Done, sizeof(Buffer));
        Status = KiLa64CopyFromUser(Buffer, (PUCHAR)Source + Done, Chunk);
        if (!NT_SUCCESS(Status))
            break;
        Status = KiLa64CopyToUser((PUCHAR)Destination + Done, Buffer, Chunk);
        if (!NT_SUCCESS(Status))
            break;
        Done += Chunk;
    }
    if (!NT_SUCCESS(Status) && Done)
        Status = STATUS_PARTIAL_COPY;
    if (Returned)
    {
        CopyStatus = KiLa64CopyToUser(Returned, &Done, sizeof(Done));
        if (!NT_SUCCESS(CopyStatus))
            return CopyStatus;
    }
    return Status;
}
