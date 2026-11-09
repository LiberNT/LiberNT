/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Keeping a record of what a process did with its handles, so
 *              a handle used after it was closed can be traced back
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#include "pspriv.h"
#define NDEBUG
#include <debug.h>

/* How many records are kept when the caller does not say */
#define PSP_DEFAULT_HANDLE_TRACES 0x1000

/* The most that may be asked for, so one process cannot take the pool */
#define PSP_MAX_HANDLE_TRACES 0x20000

/* PRIVATE FUNCTIONS **********************************************************/

/*
 * Reads what the caller is asking for. Turning the record off carries
 * nothing at all; turning it on carries a flags word, and may also carry
 * how many records to keep.
 */
static
NTSTATUS
PspCaptureHandleTracingRequest(
    _In_reads_bytes_(Length) PVOID Information,
    _In_ ULONG Length,
    _In_ KPROCESSOR_MODE PreviousMode,
    _Out_ PBOOLEAN Enable,
    _Out_ PULONG Slots)
{
    PROCESS_HANDLE_TRACING_ENABLE_EX Request;

    PAGED_CODE();

    *Enable = FALSE;
    *Slots = PSP_DEFAULT_HANDLE_TRACES;

    /* Nothing to read: the record is being turned off */
    if (Length == 0) return STATUS_SUCCESS;

    if (Length != sizeof(PROCESS_HANDLE_TRACING_ENABLE) &&
        Length != sizeof(PROCESS_HANDLE_TRACING_ENABLE_EX))
    {
        return STATUS_INFO_LENGTH_MISMATCH;
    }

    RtlZeroMemory(&Request, sizeof(Request));

    if (PreviousMode != KernelMode)
    {
        _SEH2_TRY
        {
            ProbeForRead(Information, Length, sizeof(ULONG));
            RtlCopyMemory(&Request, Information, Length);
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            _SEH2_YIELD(return _SEH2_GetExceptionCode());
        }
        _SEH2_END;
    }
    else
    {
        RtlCopyMemory(&Request, Information, Length);
    }

    /* No flag is defined, so any flag at all is a caller's mistake */
    if (Request.Flags != 0) return STATUS_INVALID_PARAMETER;

    *Enable = TRUE;

    if (Length == sizeof(PROCESS_HANDLE_TRACING_ENABLE_EX) &&
        Request.TotalSlots != 0)
    {
        if (Request.TotalSlots > PSP_MAX_HANDLE_TRACES)
            return STATUS_INVALID_PARAMETER;

        *Slots = Request.TotalSlots;
    }

    return STATUS_SUCCESS;
}

/* PUBLIC FUNCTIONS ***********************************************************/

/*
 * Turns the record on or off for a process. A caller that asks for it on a
 * process other than its own is changing how that process behaves, so it
 * needs the right to set the process's state, which its handle already
 * carried by the time this is reached.
 */
NTSTATUS
NTAPI
PspSetHandleTracing(
    _In_ PEPROCESS Process,
    _In_reads_bytes_(Length) PVOID Information,
    _In_ ULONG Length,
    _In_ KPROCESSOR_MODE PreviousMode)
{
    BOOLEAN Enable;
    ULONG Slots;
    NTSTATUS Status;

    PAGED_CODE();

    Status = PspCaptureHandleTracingRequest(Information,
                                            Length,
                                            PreviousMode,
                                            &Enable,
                                            &Slots);
    if (!NT_SUCCESS(Status)) return Status;

    if (!ExAcquireRundownProtection(&Process->RundownProtect))
        return STATUS_PROCESS_IS_TERMINATING;

    /*
     * Turning the record off always succeeds, because a process that is not
     * keeping one is already in the state the caller asked for.
     */
    if (!Enable)
    {
        Process->RefTraceEnabled = FALSE;
        ExReleaseRundownProtection(&Process->RundownProtect);
        return STATUS_SUCCESS;
    }

    Process->RefTraceEnabled = TRUE;
    ExReleaseRundownProtection(&Process->RundownProtect);

    /*
     * TODO: the records themselves live in the executive's handle table,
     * whose published layout overlaps the pointer to them with its own free
     * lists, so there is nowhere to hang them from here. Until the handle
     * table offers a way to attach and fill a trace block, the record is
     * marked as wanted and stays empty. INFO_NEEDED: where Windows 11 keeps
     * the block for a handle table of this layout.
     */
    UNREFERENCED_PARAMETER(Slots);

    return STATUS_SUCCESS;
}

/*
 * Hands back what has been recorded. A caller asks twice: once to learn the
 * length, and once with room for the records.
 */
NTSTATUS
NTAPI
PspQueryHandleTracing(
    _In_ PEPROCESS Process,
    _Out_writes_bytes_(Length) PVOID Buffer,
    _In_ ULONG Length,
    _Out_ PULONG ReturnLength)
{
    PPROCESS_HANDLE_TRACING_QUERY Query = Buffer;
    ULONG Needed;

    PAGED_CODE();

    Needed = (ULONG)FIELD_OFFSET(PROCESS_HANDLE_TRACING_QUERY, HandleTrace);
    *ReturnLength = Needed;

    /* A process that was never asked to keep a record has none to give */
    if (!Process->RefTraceEnabled) return STATUS_INVALID_PARAMETER;

    if (Length < Needed) return STATUS_INFO_LENGTH_MISMATCH;

    RtlZeroMemory(Query, Needed);

    /* TODO: fill in the records once the handle table keeps them */
    Query->Handle = NULL;
    Query->TotalTraces = 0;

    return STATUS_SUCCESS;
}
