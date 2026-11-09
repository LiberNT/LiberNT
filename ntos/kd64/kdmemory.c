/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Reading and writing the machine's memory for the host
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#include "kdpriv.h"
#define NDEBUG
#include <debug.h>

/* PRIVATE DATA ***************************************************************/

/* How much of a reply body one packet can still carry past the header */
#define KDP_MAX_REPLY_DATA  (PACKET_MAX_SIZE - KDP_MANIPULATE_HEADER_SIZE)

/* The window a memory search reads the machine through */
#define KDP_SEARCH_WINDOW   0x1000

static CHAR KdpSearchBuffer[KDP_SEARCH_WINDOW];

/* PRIVATE FUNCTIONS **********************************************************/

/*
 * Moves bytes without going through the compiler's or the run-time library's
 * idea of a copy: the debugger runs where neither can be relied on.
 */
VOID
NTAPI
KdpMoveMemory(
    _In_ PVOID Destination,
    _In_ PVOID Source,
    _In_ SIZE_T Length)
{
    PCHAR Out = (PCHAR)Destination;
    PCCH In = (PCCH)Source;

    while (Length != 0)
    {
        *Out++ = *In++;
        Length--;
    }
}

VOID
NTAPI
KdpZeroMemory(
    _In_ PVOID Destination,
    _In_ SIZE_T Length)
{
    PCHAR Out = (PCHAR)Destination;

    while (Length != 0)
    {
        *Out++ = 0;
        Length--;
    }
}

/*
 * Largest access the memory manager will make on the debugger's behalf, cut
 * down to a power of two so that every access can be kept aligned.
 */
static
ULONG
KdpLargestChunk(
    _In_ ULONG Requested)
{
    ULONG Chunk = Requested;

    if ((Chunk == 0) || (Chunk > MMDBG_COPY_MAX_SIZE))
        Chunk = MMDBG_COPY_MAX_SIZE;

    while ((Chunk & (Chunk - 1)) != 0)
        Chunk &= Chunk - 1;

    return Chunk;
}

/* PUBLIC FUNCTIONS ***********************************************************/

/*
 * Copies between the machine's memory and a buffer of the engine's own, one
 * access at a time, each aligned and no larger than the memory manager will
 * take. Stops at the first access that cannot be made and reports how far it
 * got, because a partial answer is what the host asked for.
 */
NTSTATUS
NTAPI
KdpCopyMemoryChunks(
    _In_ ULONG64 Address,
    _In_ PVOID Buffer,
    _In_ ULONG TotalSize,
    _In_ ULONG ChunkSize,
    _In_ ULONG Flags,
    _Out_opt_ PULONG ActualSize)
{
    ULONG MaximumChunk = KdpLargestChunk(ChunkSize);
    ULONG Remaining = TotalSize;
    ULONG Copied = 0;
    NTSTATUS Status = STATUS_SUCCESS;

    while (Remaining != 0)
    {
        ULONG Chunk = MaximumChunk;

        /* Narrow this access until it fits and lands on its own boundary */
        while ((Chunk > 1) &&
               ((Chunk > Remaining) || ((Address & (ULONG64)(Chunk - 1)) != 0)))
        {
            Chunk >>= 1;
        }

        Status = MmDbgCopyMemory(Address,
                                 (PVOID)((ULONG_PTR)Buffer + Copied),
                                 Chunk,
                                 Flags);
        if (!NT_SUCCESS(Status))
            break;

        Address += Chunk;
        Copied += Chunk;
        Remaining -= Chunk;
    }

    if (ActualSize != NULL)
        *ActualSize = Copied;

    return (Copied == TotalSize) ? STATUS_SUCCESS : STATUS_UNSUCCESSFUL;
}

/* REQUEST HANDLERS ***********************************************************/

VOID
NTAPI
KdpReadVirtualMemory(
    _Inout_ PDBGKD_MANIPULATE_STATE64 State,
    _Inout_ PSTRING Data,
    _In_ PCONTEXT Context)
{
    ULONG Length = State->u.ReadMemory.TransferCount;
    ULONG Actual = 0;

    UNREFERENCED_PARAMETER(Context);

    if (Length > KDP_MAX_REPLY_DATA)
        Length = KDP_MAX_REPLY_DATA;
    if (Length > Data->MaximumLength)
        Length = Data->MaximumLength;

    State->ReturnStatus = KdpCopyMemoryChunks(State->u.ReadMemory.TargetBaseAddress,
                                              Data->Buffer,
                                              Length,
                                              0,
                                              MMDBG_COPY_UNSAFE,
                                              &Actual);

    State->u.ReadMemory.ActualBytesRead = Actual;
    Data->Length = (USHORT)Actual;

    KdpSendManipulateReply(State, Data);
}

VOID
NTAPI
KdpWriteVirtualMemory(
    _Inout_ PDBGKD_MANIPULATE_STATE64 State,
    _In_ PSTRING Data,
    _In_ PCONTEXT Context)
{
    ULONG Length = State->u.WriteMemory.TransferCount;
    ULONG Actual = 0;

    UNREFERENCED_PARAMETER(Context);

    /* Only what the host actually sent can be written */
    if (Length > Data->Length)
        Length = Data->Length;

    State->ReturnStatus = KdpCopyMemoryChunks(State->u.WriteMemory.TargetBaseAddress,
                                              Data->Buffer,
                                              Length,
                                              0,
                                              MMDBG_COPY_UNSAFE | MMDBG_COPY_WRITE,
                                              &Actual);

    State->u.WriteMemory.ActualBytesWritten = Actual;

    KdpSendManipulateReply(State, NULL);
}

VOID
NTAPI
KdpReadPhysicalMemory(
    _Inout_ PDBGKD_MANIPULATE_STATE64 State,
    _Inout_ PSTRING Data,
    _In_ PCONTEXT Context)
{
    ULONG Length = State->u.ReadMemory.TransferCount;
    ULONG Actual = 0;

    UNREFERENCED_PARAMETER(Context);

    if (Length > KDP_MAX_REPLY_DATA)
        Length = KDP_MAX_REPLY_DATA;
    if (Length > Data->MaximumLength)
        Length = Data->MaximumLength;

    /* TODO: the caching the host asks for is not carried through; cached is used */
    State->ReturnStatus = KdpCopyMemoryChunks(State->u.ReadMemory.TargetBaseAddress,
                                              Data->Buffer,
                                              Length,
                                              0,
                                              MMDBG_COPY_UNSAFE |
                                              MMDBG_COPY_PHYSICAL |
                                              MMDBG_COPY_CACHED,
                                              &Actual);

    State->u.ReadMemory.ActualBytesRead = Actual;
    Data->Length = (USHORT)Actual;

    KdpSendManipulateReply(State, Data);
}

VOID
NTAPI
KdpWritePhysicalMemory(
    _Inout_ PDBGKD_MANIPULATE_STATE64 State,
    _In_ PSTRING Data,
    _In_ PCONTEXT Context)
{
    ULONG Length = State->u.WriteMemory.TransferCount;
    ULONG Actual = 0;

    UNREFERENCED_PARAMETER(Context);

    if (Length > Data->Length)
        Length = Data->Length;

    /* TODO: the caching the host asks for is not carried through; cached is used */
    State->ReturnStatus = KdpCopyMemoryChunks(State->u.WriteMemory.TargetBaseAddress,
                                              Data->Buffer,
                                              Length,
                                              0,
                                              MMDBG_COPY_UNSAFE |
                                              MMDBG_COPY_PHYSICAL |
                                              MMDBG_COPY_CACHED |
                                              MMDBG_COPY_WRITE,
                                              &Actual);

    State->u.WriteMemory.ActualBytesWritten = Actual;

    KdpSendManipulateReply(State, NULL);
}

/*
 * Looks for a run of bytes somewhere in a range. The machine is read through a
 * window so that a long search costs one access per window rather than one per
 * candidate address.
 */
VOID
NTAPI
KdpSearchMemory(
    _Inout_ PDBGKD_MANIPULATE_STATE64 State,
    _Inout_ PSTRING Data,
    _In_ PCONTEXT Context)
{
    ULONG PatternLength = State->u.SearchMemory.PatternLength;
    ULONG64 Address = State->u.SearchMemory.SearchAddress;
    ULONG64 Remaining = State->u.SearchMemory.SearchLength;
    BOOLEAN Found = FALSE;

    UNREFERENCED_PARAMETER(Context);

    if ((PatternLength == 0) ||
        (PatternLength > Data->Length) ||
        (PatternLength > KDP_SEARCH_WINDOW / 2))
    {
        /* A pattern that cannot be held in the window cannot be looked for */
        State->ReturnStatus = STATUS_INVALID_PARAMETER;
        KdpSendManipulateReply(State, NULL);
        return;
    }

    while (!Found && (Remaining >= PatternLength))
    {
        ULONG Window = KDP_SEARCH_WINDOW;
        ULONG Read = 0;
        ULONG Offset;
        ULONG Last;

        if ((ULONG64)Window > Remaining)
            Window = (ULONG)Remaining;

        KdpCopyMemoryChunks(Address,
                            KdpSearchBuffer,
                            Window,
                            0,
                            MMDBG_COPY_UNSAFE,
                            &Read);

        if (Read < PatternLength)
        {
            /*
             * The window could not be read at all, or not far enough to hold
             * the pattern. Step past the part that failed and carry on, since
             * a hole in the middle of a range is not the end of the search.
             */
            ULONG Step = (Read != 0) ? Read : 1;

            if ((ULONG64)Step > Remaining)
                break;

            Address += Step;
            Remaining -= Step;
            continue;
        }

        Last = Read - PatternLength;
        for (Offset = 0; Offset <= Last; Offset++)
        {
            ULONG Index;

            for (Index = 0; Index < PatternLength; Index++)
            {
                if (KdpSearchBuffer[Offset + Index] != Data->Buffer[Index])
                    break;
            }

            if (Index == PatternLength)
            {
                State->u.SearchMemory.FoundAddress = Address + Offset;
                Found = TRUE;
                break;
            }
        }

        if (!Found)
        {
            /* Overlap the next window so a match across the seam is still seen */
            ULONG Step = Read - PatternLength + 1;

            Address += Step;
            Remaining -= Step;
        }
    }

    State->ReturnStatus = Found ? STATUS_SUCCESS : STATUS_NO_MORE_ENTRIES;
    KdpSendManipulateReply(State, NULL);
}

/*
 * Writes a pattern over a range, repeating it as many times as it fits. A
 * partial last copy is written, because the host asks for a byte count and
 * not for a number of repetitions.
 */
VOID
NTAPI
KdpFillMemory(
    _Inout_ PDBGKD_MANIPULATE_STATE64 State,
    _In_ PSTRING Data,
    _In_ PCONTEXT Context)
{
    ULONG PatternLength = State->u.FillMemory.PatternLength;
    ULONG64 Address = State->u.FillMemory.Address;
    ULONG Remaining = State->u.FillMemory.Length;
    ULONG Flags = MMDBG_COPY_UNSAFE | MMDBG_COPY_WRITE;
    NTSTATUS Status = STATUS_SUCCESS;

    UNREFERENCED_PARAMETER(Context);

    if ((PatternLength == 0) || (PatternLength > Data->Length))
    {
        State->ReturnStatus = STATUS_INVALID_PARAMETER;
        KdpSendManipulateReply(State, NULL);
        return;
    }

    if ((State->u.FillMemory.Flags & DBGKD_FILL_MEMORY_PHYSICAL) != 0)
        Flags |= MMDBG_COPY_PHYSICAL | MMDBG_COPY_CACHED;

    while (Remaining != 0)
    {
        ULONG Chunk = (Remaining < PatternLength) ? Remaining : PatternLength;
        ULONG Written = 0;

        Status = KdpCopyMemoryChunks(Address,
                                     Data->Buffer,
                                     Chunk,
                                     0,
                                     Flags,
                                     &Written);
        if (!NT_SUCCESS(Status))
            break;

        Address += Chunk;
        Remaining -= Chunk;
    }

    State->ReturnStatus = Status;
    KdpSendManipulateReply(State, NULL);
}

/*
 * Says which address space an address belongs to and what may be done with
 * it. The answer is about the address, not about the page: a page that is not
 * resident is still part of the space it belongs to.
 */
VOID
NTAPI
KdpQueryMemory(
    _Inout_ PDBGKD_MANIPULATE_STATE64 State,
    _In_ PCONTEXT Context)
{
    PVOID Address = (PVOID)(ULONG_PTR)State->u.QueryMemory.Address;

    UNREFERENCED_PARAMETER(Context);

    if (State->u.QueryMemory.AddressSpace != DBGKD_QUERY_MEMORY_VIRTUAL)
    {
        State->ReturnStatus = STATUS_INVALID_PARAMETER;
        KdpSendManipulateReply(State, NULL);
        return;
    }

    if (Address < MmSystemRangeStart)
    {
        State->u.QueryMemory.AddressSpace = DBGKD_QUERY_MEMORY_PROCESS;
    }
    else if (MmIsSessionAddress(Address))
    {
        State->u.QueryMemory.AddressSpace = DBGKD_QUERY_MEMORY_SESSION;
    }
    else
    {
        State->u.QueryMemory.AddressSpace = DBGKD_QUERY_MEMORY_KERNEL;
    }

    /* TODO: the per-page protection is not reported; read, write and execute
     * are claimed for every address the debugger can reach */
    State->u.QueryMemory.Flags = DBGKD_QUERY_MEMORY_READ |
                                 DBGKD_QUERY_MEMORY_WRITE |
                                 DBGKD_QUERY_MEMORY_EXECUTE;
    State->u.QueryMemory.Reserved = 0;
    State->ReturnStatus = STATUS_SUCCESS;

    KdpSendManipulateReply(State, NULL);
}

/*
 * The host asks the machine to check its own low memory. Whether that can be
 * done at all is the architecture's business.
 */
VOID
NTAPI
KdpCheckLowMemory(
    _Inout_ PDBGKD_MANIPULATE_STATE64 State)
{
    State->ReturnStatus = KdpSysCheckLowMemory(0);
    KdpSendManipulateReply(State, NULL);
}

/* EOF */
