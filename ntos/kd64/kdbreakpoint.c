/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     The breakpoints the host owns, and the instructions they replace
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#include "kdpriv.h"
#define NDEBUG
#include <debug.h>

/* PRIVATE FUNCTIONS **********************************************************/

/*
 * Which address space a breakpoint belongs to. A breakpoint low enough to be
 * in a process, or in a library shared between processes, belongs to the
 * process that was running when it was set; anything higher is the system's
 * and is the same in every address space.
 */
static
ULONG_PTR
KdpBreakpointOwner(
    _In_ PVOID Address)
{
    PKTHREAD Thread;

    if (Address >= KD_HIGHEST_USER_BREAKPOINT_ADDRESS)
        return 0;

    Thread = KeGetCurrentThread();
    if ((Thread == NULL) || (Thread->ApcState.Process == NULL))
        return 0;

    return KPROCESS_DTB0(Thread->ApcState.Process);
}

static
BOOLEAN
KdpBreakpointIsForCurrentProcess(
    _In_ PBREAKPOINT_ENTRY Entry)
{
    if (Entry->DirectoryTableBase == 0)
        return TRUE;

    return (Entry->DirectoryTableBase == KdpBreakpointOwner(Entry->Address));
}

/* PUBLIC FUNCTIONS ***********************************************************/

/*
 * Puts the breakpoint instruction where the saved one was. The caller has
 * already saved what is there.
 */
BOOLEAN
NTAPI
KdpLowWriteContent(
    _In_ ULONG BpEntry)
{
    PBREAKPOINT_ENTRY Entry = &KdpBreakpointTable[BpEntry - 1];
    NTSTATUS Status;

    Status = KdpCopyMemoryChunks((ULONG64)(ULONG_PTR)Entry->Address,
                                 &KdpBreakpointInstruction,
                                 KD_BREAKPOINT_SIZE,
                                 0,
                                 MMDBG_COPY_UNSAFE | MMDBG_COPY_WRITE,
                                 NULL);
    if (!NT_SUCCESS(Status))
        return FALSE;

    /* The processor may already have fetched what used to be there */
    KeSweepICache(Entry->Address, KD_BREAKPOINT_SIZE);
    return TRUE;
}

/*
 * Puts back the instruction the breakpoint displaced.
 */
BOOLEAN
NTAPI
KdpLowRestoreBreakpoint(
    _In_ ULONG BpEntry)
{
    PBREAKPOINT_ENTRY Entry = &KdpBreakpointTable[BpEntry - 1];
    NTSTATUS Status;

    Status = KdpCopyMemoryChunks((ULONG64)(ULONG_PTR)Entry->Address,
                                 &Entry->Content,
                                 KD_BREAKPOINT_SIZE,
                                 0,
                                 MMDBG_COPY_UNSAFE | MMDBG_COPY_WRITE,
                                 NULL);
    if (!NT_SUCCESS(Status))
        return FALSE;

    KeSweepICache(Entry->Address, KD_BREAKPOINT_SIZE);
    return TRUE;
}

/*
 * Takes a breakpoint at an address and returns the handle the host will use to
 * refer to it. Zero means the breakpoint was not taken at all.
 *
 * An address whose page cannot be written yet is remembered rather than
 * refused: the page may be paged out, or not yet mapped by the process it
 * belongs to, and the breakpoint is written the next time that process runs.
 */
ULONG
NTAPI
KdpAddBreakpoint(
    _In_ PVOID Address)
{
    ULONG_PTR Owner;
    ULONG Index;
    ULONG Free = 0;
    KD_BREAKPOINT_TYPE Content;
    NTSTATUS Status;

    if (Address == NULL)
        return 0;

    Owner = KdpBreakpointOwner(Address);

    /* An address that already has a breakpoint keeps the handle it has */
    for (Index = 0; Index < KD_BREAKPOINT_MAX; Index++)
    {
        PBREAKPOINT_ENTRY Entry = &KdpBreakpointTable[Index];

        if (Entry->Flags == 0)
        {
            if (Free == 0)
                Free = Index + 1;

            continue;
        }

        if ((Entry->Address == Address) && (Entry->DirectoryTableBase == Owner))
            return Index + 1;
    }

    if (Free == 0)
        return 0;

    /*
     * What is there now has to be readable, or there is nothing to put back
     * when the breakpoint is removed.
     */
    Status = KdpCopyMemoryChunks((ULONG64)(ULONG_PTR)Address,
                                 &Content,
                                 KD_BREAKPOINT_SIZE,
                                 0,
                                 MMDBG_COPY_UNSAFE,
                                 NULL);
    if (!NT_SUCCESS(Status))
        return 0;

    KdpBreakpointTable[Free - 1].Address = Address;
    KdpBreakpointTable[Free - 1].Content = Content;
    KdpBreakpointTable[Free - 1].DirectoryTableBase = Owner;
    KdpBreakpointTable[Free - 1].Flags = KD_BREAKPOINT_ACTIVE;

    if (!KdpLowWriteContent(Free))
    {
        /* Owed, not lost: the trap path writes it once the page is there */
        KdpBreakpointTable[Free - 1].Flags = KD_BREAKPOINT_ACTIVE |
                                             KD_BREAKPOINT_PENDING;
        KdpOweBreakpoint = TRUE;
    }

    return Free;
}

/*
 * Removes one breakpoint and puts its instruction back.
 */
BOOLEAN
NTAPI
KdpDeleteBreakpoint(
    _In_ ULONG BpEntry)
{
    PBREAKPOINT_ENTRY Entry;

    if ((BpEntry == 0) || (BpEntry > KD_BREAKPOINT_MAX))
        return FALSE;

    Entry = &KdpBreakpointTable[BpEntry - 1];

    if (Entry->Flags == 0)
        return FALSE;

    /*
     * Only a breakpoint whose instruction is actually in memory has something
     * to put back. One that was owed, suspended or whose process has gone
     * leaves memory alone.
     */
    if ((Entry->Flags & (KD_BREAKPOINT_PENDING |
                         KD_BREAKPOINT_SUSPENDED |
                         KD_BREAKPOINT_EXPIRED)) == 0)
    {
        if (!KdpLowRestoreBreakpoint(BpEntry))
        {
            /*
             * The instruction could not be put back. The slot is still freed,
             * because the host has been told the breakpoint is gone and
             * keeping it would leave a handle nobody can use.
             */
            /* FIXME: the breakpoint instruction stays in memory when its page
             * cannot be written at removal time */
            Entry->Flags = 0;
            return FALSE;
        }
    }

    Entry->Flags = 0;
    Entry->Address = NULL;
    Entry->DirectoryTableBase = 0;

    return TRUE;
}

/*
 * Removes every breakpoint that falls in a range of addresses. Used when the
 * memory a breakpoint sits in is about to stop being what it was.
 */
BOOLEAN
NTAPI
KdpDeleteBreakpointRange(
    _In_ PVOID Base,
    _In_ PVOID Limit)
{
    ULONG Index;
    BOOLEAN Deleted = FALSE;

    for (Index = 0; Index < KD_BREAKPOINT_MAX; Index++)
    {
        PBREAKPOINT_ENTRY Entry = &KdpBreakpointTable[Index];

        if (Entry->Flags == 0)
            continue;

        if ((Entry->Address >= Base) && (Entry->Address <= Limit))
        {
            KdpDeleteBreakpoint(Index + 1);
            Deleted = TRUE;
        }
    }

    return Deleted;
}

/*
 * Takes one breakpoint's instruction back out of memory while keeping the
 * breakpoint itself, so that the host can read the code as it really is.
 */
VOID
NTAPI
KdpSuspendBreakPoint(
    _In_ ULONG BpEntry)
{
    PBREAKPOINT_ENTRY Entry;

    if ((BpEntry == 0) || (BpEntry > KD_BREAKPOINT_MAX))
        return;

    Entry = &KdpBreakpointTable[BpEntry - 1];

    if (Entry->Flags == 0)
        return;

    if ((Entry->Flags & (KD_BREAKPOINT_SUSPENDED | KD_BREAKPOINT_PENDING)) != 0)
    {
        Entry->Flags |= KD_BREAKPOINT_SUSPENDED;
        return;
    }

    if (KdpLowRestoreBreakpoint(BpEntry))
        Entry->Flags |= KD_BREAKPOINT_SUSPENDED;
}

VOID
NTAPI
KdpSuspendAllBreakPoints(VOID)
{
    ULONG Index;

    if (BreakpointsSuspended)
        return;

    for (Index = 0; Index < KD_BREAKPOINT_MAX; Index++)
    {
        if (KdpBreakpointTable[Index].Flags != 0)
            KdpSuspendBreakPoint(Index + 1);
    }

    BreakpointsSuspended = TRUE;
}

/*
 * Puts every breakpoint back into memory. A breakpoint that cannot be written
 * now becomes owed rather than lost.
 */
VOID
NTAPI
KdpRestoreAllBreakpoints(VOID)
{
    ULONG Index;

    if (!BreakpointsSuspended)
        return;

    BreakpointsSuspended = FALSE;

    for (Index = 0; Index < KD_BREAKPOINT_MAX; Index++)
    {
        PBREAKPOINT_ENTRY Entry = &KdpBreakpointTable[Index];

        if ((Entry->Flags & KD_BREAKPOINT_SUSPENDED) == 0)
            continue;

        Entry->Flags &= ~KD_BREAKPOINT_SUSPENDED;

        if ((Entry->Flags & KD_BREAKPOINT_PENDING) != 0)
            continue;

        if (!KdpLowWriteContent(Index + 1))
        {
            Entry->Flags |= KD_BREAKPOINT_PENDING;
            KdpOweBreakpoint = TRUE;
        }
    }
}

/*
 * Writes the breakpoints that could not be written when they were set. Called
 * from the trap path, where the address space that owns them is current and
 * their pages stand a chance of being there.
 */
VOID
NTAPI
KdSetOwedBreakpoints(VOID)
{
    ULONG Index;
    BOOLEAN StillOwed = FALSE;
    BOOLEAN Enabled;

    if (!KdpOweBreakpoint)
        return;

    /*
     * This runs on the way out of a trap, so nothing else on this processor
     * may be allowed to see memory half-written.
     */
    Enabled = KeDisableInterrupts();

    for (Index = 0; Index < KD_BREAKPOINT_MAX; Index++)
    {
        PBREAKPOINT_ENTRY Entry = &KdpBreakpointTable[Index];

        if ((Entry->Flags & KD_BREAKPOINT_PENDING) == 0)
            continue;

        if (!KdpBreakpointIsForCurrentProcess(Entry))
        {
            StillOwed = TRUE;
            continue;
        }

        /*
         * What is at the address now is what has to be put back later, so it
         * is read again: the page may be a different one than when the
         * breakpoint was asked for.
         */
        if (!NT_SUCCESS(KdpCopyMemoryChunks((ULONG64)(ULONG_PTR)Entry->Address,
                                            &Entry->Content,
                                            KD_BREAKPOINT_SIZE,
                                            0,
                                            MMDBG_COPY_UNSAFE,
                                            NULL)))
        {
            StillOwed = TRUE;
            continue;
        }

        if (KdpLowWriteContent(Index + 1))
            Entry->Flags &= ~KD_BREAKPOINT_PENDING;
        else
            StillOwed = TRUE;
    }

    KdpOweBreakpoint = StillOwed;

    KeRestoreInterrupts(Enabled);
}

/* REQUEST HANDLERS ***********************************************************/

VOID
NTAPI
KdpWriteBreakpoint(
    _Inout_ PDBGKD_MANIPULATE_STATE64 State,
    _In_ PSTRING Data,
    _In_ PCONTEXT Context)
{
    ULONG Handle;

    UNREFERENCED_PARAMETER(Data);
    UNREFERENCED_PARAMETER(Context);

    Handle = KdpAddBreakpoint((PVOID)(ULONG_PTR)State->u.WriteBreakPoint.BreakPointAddress);

    State->u.WriteBreakPoint.BreakPointHandle = Handle;
    State->ReturnStatus = (Handle != 0) ? STATUS_SUCCESS : STATUS_UNSUCCESSFUL;

    KdpSendManipulateReply(State, NULL);
}

VOID
NTAPI
KdpRestoreBreakpoint(
    _Inout_ PDBGKD_MANIPULATE_STATE64 State,
    _In_ PSTRING Data,
    _In_ PCONTEXT Context)
{
    UNREFERENCED_PARAMETER(Data);
    UNREFERENCED_PARAMETER(Context);

    State->ReturnStatus =
        KdpDeleteBreakpoint(State->u.RestoreBreakPoint.BreakPointHandle)
        ? STATUS_SUCCESS : STATUS_UNSUCCESSFUL;

    KdpSendManipulateReply(State, NULL);
}

/*
 * Sets a whole list of breakpoints in one exchange and then lets the machine
 * go on. Each entry in the list is answered in place, so the host learns which
 * ones it got.
 */
BOOLEAN
NTAPI
KdpWriteBreakPointEx(
    _Inout_ PDBGKD_MANIPULATE_STATE64 State,
    _In_ PSTRING Data,
    _Inout_ PCONTEXT Context)
{
    ULONG Count = State->u.BreakPointEx.BreakPointCount;
    PDBGKD_WRITE_BREAKPOINT64 Breakpoints;
    ULONG Index;

    if ((Count == 0) ||
        (Count > KD_BREAKPOINT_MAX) ||
        (Data->Length < Count * sizeof(DBGKD_WRITE_BREAKPOINT64)))
    {
        State->ReturnStatus = STATUS_INVALID_PARAMETER;
        KdpSendManipulateReply(State, NULL);
        return FALSE;
    }

    Breakpoints = (PDBGKD_WRITE_BREAKPOINT64)Data->Buffer;
    State->ReturnStatus = STATUS_SUCCESS;

    for (Index = 0; Index < Count; Index++)
    {
        ULONG Handle;

        Handle = KdpAddBreakpoint((PVOID)(ULONG_PTR)Breakpoints[Index].BreakPointAddress);
        Breakpoints[Index].BreakPointHandle = Handle;

        if (Handle == 0)
            State->ReturnStatus = STATUS_UNSUCCESSFUL;
    }

    KdpSendManipulateReply(State, Data);

    /* The host may also have asked this processor to run differently */
    KdpGetStateChange(State, Context);

    return TRUE;
}

/*
 * Removes a whole list of breakpoints and then lets the machine go on.
 */
BOOLEAN
NTAPI
KdpRestoreBreakPointEx(
    _Inout_ PDBGKD_MANIPULATE_STATE64 State,
    _In_ PSTRING Data,
    _In_ PCONTEXT Context)
{
    ULONG Count = State->u.BreakPointEx.BreakPointCount;
    PDBGKD_RESTORE_BREAKPOINT Breakpoints;
    ULONG Index;

    UNREFERENCED_PARAMETER(Context);

    if ((Count == 0) ||
        (Count > KD_BREAKPOINT_MAX) ||
        (Data->Length < Count * sizeof(DBGKD_RESTORE_BREAKPOINT)))
    {
        State->ReturnStatus = STATUS_INVALID_PARAMETER;
        KdpSendManipulateReply(State, NULL);
        return FALSE;
    }

    Breakpoints = (PDBGKD_RESTORE_BREAKPOINT)Data->Buffer;
    State->ReturnStatus = STATUS_SUCCESS;

    for (Index = 0; Index < Count; Index++)
    {
        if (!KdpDeleteBreakpoint(Breakpoints[Index].BreakPointHandle))
            State->ReturnStatus = STATUS_UNSUCCESSFUL;
    }

    KdpSendManipulateReply(State, Data);
    return TRUE;
}

/*
 * A breakpoint whose instruction the host chose itself. Only an instruction
 * the same size as this architecture's own breakpoint can be held in the
 * table, so a larger one is refused rather than written half-way.
 */
VOID
NTAPI
KdpWriteCustomBreakpoint(
    _Inout_ PDBGKD_MANIPULATE_STATE64 State,
    _In_ PSTRING Data,
    _In_ PCONTEXT Context)
{
    PDBGKD_WRITE_CUSTOM_BREAKPOINT Custom = &State->u.WriteCustomBreakpoint;
    PVOID Address = (PVOID)(ULONG_PTR)Custom->BreakPointAddress;
    KD_BREAKPOINT_TYPE Instruction;
    KD_BREAKPOINT_TYPE Saved;
    ULONG Handle = 0;
    ULONG Index;
    NTSTATUS Status;

    UNREFERENCED_PARAMETER(Data);
    UNREFERENCED_PARAMETER(Context);

    /* TODO: an instruction of a size other than this architecture's own is refused */
    if ((Custom->BreakPointInstructionSize != KD_BREAKPOINT_SIZE) ||
        ((Custom->BreakPointInstructionAlignment != 0) &&
         (((ULONG_PTR)Address %
           (ULONG_PTR)Custom->BreakPointInstructionAlignment) != 0)))
    {
        Custom->BreakPointHandle = 0;
        State->ReturnStatus = STATUS_INVALID_PARAMETER;
        KdpSendManipulateReply(State, NULL);
        return;
    }

    for (Index = 0; Index < KD_BREAKPOINT_MAX; Index++)
    {
        if (KdpBreakpointTable[Index].Flags == 0)
        {
            Handle = Index + 1;
            break;
        }
    }

    if (Handle == 0)
    {
        Custom->BreakPointHandle = 0;
        State->ReturnStatus = STATUS_INSUFFICIENT_RESOURCES;
        KdpSendManipulateReply(State, NULL);
        return;
    }

    Status = KdpCopyMemoryChunks((ULONG64)(ULONG_PTR)Address,
                                 &Saved,
                                 KD_BREAKPOINT_SIZE,
                                 0,
                                 MMDBG_COPY_UNSAFE,
                                 NULL);
    if (!NT_SUCCESS(Status))
    {
        Custom->BreakPointHandle = 0;
        State->ReturnStatus = Status;
        KdpSendManipulateReply(State, NULL);
        return;
    }

    Instruction = (KD_BREAKPOINT_TYPE)Custom->BreakPointInstruction;

    Status = KdpCopyMemoryChunks((ULONG64)(ULONG_PTR)Address,
                                 &Instruction,
                                 KD_BREAKPOINT_SIZE,
                                 0,
                                 MMDBG_COPY_UNSAFE | MMDBG_COPY_WRITE,
                                 NULL);
    if (!NT_SUCCESS(Status))
    {
        Custom->BreakPointHandle = 0;
        State->ReturnStatus = Status;
        KdpSendManipulateReply(State, NULL);
        return;
    }

    KeSweepICache(Address, KD_BREAKPOINT_SIZE);

    KdpBreakpointTable[Handle - 1].Address = Address;
    KdpBreakpointTable[Handle - 1].Content = Saved;
    KdpBreakpointTable[Handle - 1].DirectoryTableBase = KdpBreakpointOwner(Address);
    KdpBreakpointTable[Handle - 1].Flags = KD_BREAKPOINT_ACTIVE;

    Custom->BreakPointHandle = Handle;
    State->ReturnStatus = STATUS_SUCCESS;

    KdpSendManipulateReply(State, NULL);
}

/* EOF */
