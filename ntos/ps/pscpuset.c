/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     The sets of processors a process or a thread is allowed to
 *              choose from, named by identifier rather than by mask
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#include "pspriv.h"
#define NDEBUG
#include <debug.h>

/*
 * The identifier the system gives the set that holds one processor. The
 * executive publishes these identifiers as this base plus the processor's
 * own index, so the two translate into each other without a table.
 */
#define PSP_CPU_SET_ID_BASE 0x100

/* How many identifiers one call may name */
#define PSP_MAX_CPU_SET_IDS 64

/* The one processor a set identifier stands for, as a mask */
#define PSP_CPU_SET_MASK(Index) ((KAFFINITY)1 << (Index))

/* PRIVATE FUNCTIONS **********************************************************/

/*
 * Turns the identifiers a caller named into the mask of processors they
 * stand for. An identifier that names no processor on this machine makes the
 * whole call fail: a caller that asked for a processor that is not there has
 * to be told, not quietly narrowed.
 */
static
NTSTATUS
PspCpuSetIdsToAffinity(
    _In_reads_(Count) const ULONG *Ids,
    _In_ ULONG Count,
    _Out_ PKAFFINITY Affinity)
{
    KAFFINITY Mask = 0;
    ULONG Index;

    *Affinity = 0;

    for (Index = 0; Index < Count; Index++)
    {
        ULONG Processor;

        if (Ids[Index] < PSP_CPU_SET_ID_BASE) return STATUS_INVALID_PARAMETER;

        Processor = Ids[Index] - PSP_CPU_SET_ID_BASE;
        if (Processor >= (ULONG)KeNumberProcessors)
            return STATUS_INVALID_PARAMETER;

        Mask |= PSP_CPU_SET_MASK(Processor);
    }

    if ((Mask & ~(KAFFINITY)KeActiveProcessors) != 0)
        return STATUS_INVALID_PARAMETER;

    *Affinity = Mask;
    return STATUS_SUCCESS;
}

/*
 * Turns a mask back into the identifiers it stands for, which is the shape a
 * caller reads back.
 */
static
NTSTATUS
PspAffinityToCpuSetIds(
    _In_ KAFFINITY Affinity,
    _Out_writes_bytes_(Length) PVOID Buffer,
    _In_ ULONG Length,
    _Out_ PULONG ReturnLength)
{
    ULONG Ids[PSP_MAX_CPU_SET_IDS];
    ULONG Count = 0;
    ULONG Index;

    for (Index = 0;
         Index < (ULONG)KeNumberProcessors && Count < RTL_NUMBER_OF(Ids);
         Index++)
    {
        if ((Affinity & PSP_CPU_SET_MASK(Index)) == 0) continue;

        Ids[Count] = PSP_CPU_SET_ID_BASE + Index;
        Count++;
    }

    *ReturnLength = Count * (ULONG)sizeof(ULONG);

    /* A process that chose no set of its own answers with nothing at all */
    if (Count == 0) return STATUS_SUCCESS;

    if (Length < *ReturnLength) return STATUS_BUFFER_TOO_SMALL;

    RtlCopyMemory(Buffer, Ids, *ReturnLength);
    return STATUS_SUCCESS;
}

/*
 * Reads the identifiers a caller is setting. A call that names none is
 * clearing the set, which is the only way back to the system's own choice.
 */
static
NTSTATUS
PspCaptureCpuSetIds(
    _In_reads_bytes_(Length) PVOID Information,
    _In_ ULONG Length,
    _In_ KPROCESSOR_MODE PreviousMode,
    _Out_writes_(PSP_MAX_CPU_SET_IDS) PULONG Ids,
    _Out_ PULONG Count)
{
    *Count = 0;

    if (Length == 0) return STATUS_SUCCESS;

    if ((Length % sizeof(ULONG)) != 0) return STATUS_INVALID_PARAMETER;
    if (Length > PSP_MAX_CPU_SET_IDS * sizeof(ULONG))
        return STATUS_INVALID_PARAMETER;

    if (PreviousMode == KernelMode)
    {
        RtlCopyMemory(Ids, Information, Length);
        *Count = Length / (ULONG)sizeof(ULONG);
        return STATUS_SUCCESS;
    }

    _SEH2_TRY
    {
        ProbeForRead(Information, Length, sizeof(ULONG));
        RtlCopyMemory(Ids, Information, Length);
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        _SEH2_YIELD(return _SEH2_GetExceptionCode());
    }
    _SEH2_END;

    *Count = Length / (ULONG)sizeof(ULONG);
    return STATUS_SUCCESS;
}

/* PUBLIC FUNCTIONS ***********************************************************/

/*
 * The sets a process will choose from. A process keeps two: the ones it is
 * allowed to use at all, and the ones its threads are given by default when
 * they choose none of their own.
 */
NTSTATUS
NTAPI
PspQueryProcessCpuSets(
    _In_ PEPROCESS Process,
    _In_ BOOLEAN Allowed,
    _Out_writes_bytes_(Length) PVOID Buffer,
    _In_ ULONG Length,
    _Out_ PULONG ReturnLength)
{
    KAFFINITY Affinity;

    PAGED_CODE();

    /*
     * A process with more sets than fit in a word keeps them elsewhere and
     * says so, and this system has no machine wide enough for that.
     */
    if (Process->IndirectCpuSets)
    {
        *ReturnLength = 0;
        return STATUS_NOT_SUPPORTED;
    }

    Affinity = (KAFFINITY)(Allowed ? Process->AllowedCpuSets
                                   : Process->DefaultCpuSets);

    return PspAffinityToCpuSetIds(Affinity, Buffer, Length, ReturnLength);
}

NTSTATUS
NTAPI
PspSetProcessCpuSets(
    _In_ PEPROCESS Process,
    _In_ BOOLEAN Allowed,
    _In_reads_bytes_(Length) PVOID Information,
    _In_ ULONG Length,
    _In_ KPROCESSOR_MODE PreviousMode)
{
    ULONG Ids[PSP_MAX_CPU_SET_IDS];
    KAFFINITY Affinity = 0;
    ULONG Count;
    NTSTATUS Status;

    PAGED_CODE();

    Status = PspCaptureCpuSetIds(Information,
                                 Length,
                                 PreviousMode,
                                 Ids,
                                 &Count);
    if (!NT_SUCCESS(Status)) return Status;

    if (Count != 0)
    {
        Status = PspCpuSetIdsToAffinity(Ids, Count, &Affinity);
        if (!NT_SUCCESS(Status)) return Status;
    }

    /*
     * Which sets a process may use at all is a system decision: a process
     * that could widen its own would not be bounded by one.
     */
    if (Allowed &&
        !SeSinglePrivilegeCheck(SeIncreaseBasePriorityPrivilege, PreviousMode))
    {
        return STATUS_PRIVILEGE_NOT_HELD;
    }

    if (Allowed) Process->AllowedCpuSets = (ULONGLONG)Affinity;
    else Process->DefaultCpuSets = (ULONGLONG)Affinity;

    return STATUS_SUCCESS;
}

/*
 * The sets one thread chose for itself, out of the ones its process allows.
 */
NTSTATUS
NTAPI
PspQueryThreadCpuSets(
    _In_ PETHREAD Thread,
    _Out_writes_bytes_(Length) PVOID Buffer,
    _In_ ULONG Length,
    _Out_ PULONG ReturnLength)
{
    PAGED_CODE();

    if (Thread->IndirectCpuSets)
    {
        *ReturnLength = 0;
        return STATUS_NOT_SUPPORTED;
    }

    return PspAffinityToCpuSetIds((KAFFINITY)Thread->SelectedCpuSets,
                                  Buffer,
                                  Length,
                                  ReturnLength);
}

/*
 * Narrows a thread to some of the sets its process allows. A thread may only
 * narrow itself: a set its process does not allow is refused, and clearing
 * the choice puts the thread back on the process's default.
 */
NTSTATUS
NTAPI
PspSetThreadCpuSets(
    _In_ PETHREAD Thread,
    _In_reads_bytes_(Length) PVOID Information,
    _In_ ULONG Length,
    _In_ KPROCESSOR_MODE PreviousMode)
{
    PEPROCESS Process = THREAD_TO_PROCESS(Thread);
    ULONG Ids[PSP_MAX_CPU_SET_IDS];
    ULONG64 Masks[PSP_MAX_CPU_SET_IDS];
    KAFFINITY Allowed;
    KAFFINITY Affinity = 0;
    ULONG Count;
    ULONG Index;
    NTSTATUS Status;

    PAGED_CODE();

    Status = PspCaptureCpuSetIds(Information,
                                 Length,
                                 PreviousMode,
                                 Ids,
                                 &Count);
    if (!NT_SUCCESS(Status)) return Status;

    if (Count == 0)
    {
        Thread->SelectedCpuSets = 0;
        return KeSetSelectedCpuSetsThread(&Thread->Tcb, 0, NULL);
    }

    Status = PspCpuSetIdsToAffinity(Ids, Count, &Affinity);
    if (!NT_SUCCESS(Status)) return Status;

    /*
     * A process that named no sets of its own allows all of them, so the
     * thread is bounded by the machine rather than by the process.
     */
    Allowed = (KAFFINITY)Process->AllowedCpuSets;
    if (Allowed == 0) Allowed = (KAFFINITY)KeActiveProcessors;

    if ((Affinity & ~Allowed) != 0) return STATUS_INVALID_PARAMETER;

    for (Index = 0; Index < Count; Index++)
    {
        Masks[Index] =
            (ULONG64)PSP_CPU_SET_MASK(Ids[Index] - PSP_CPU_SET_ID_BASE);
    }

    Status = KeSetSelectedCpuSetsThread(&Thread->Tcb, Count, Masks);
    if (NT_SUCCESS(Status)) Thread->SelectedCpuSets = (ULONG64)Affinity;

    return Status;
}
