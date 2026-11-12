/*
 * PROJECT:     LiberNT NT User-Mode DLL
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Synchronization barriers
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntdll.h>

#define NDEBUG
#include <debug.h>

#define RTLP_BARRIER_DEFAULT_SPIN   2000

#define RTLP_BARRIER_CLOSED         0x80000000UL
#define RTLP_BARRIER_TRACKED        0x40000000UL
#define RTLP_BARRIER_COUNT_MASK     0x3FFFFFFFUL

typedef struct _RTLP_BARRIER
{
    LONG TotalThreads;
    LONG SpinCount;
    LONG Gate;
    LONG Departed;
    LONG Tracked;
} RTLP_BARRIER, *PRTLP_BARRIER;

C_ASSERT(sizeof(RTLP_BARRIER) <= sizeof(RTL_BARRIER));

static
ULONG
RtlpBarrierSpinLimit(
    _In_ PRTLP_BARRIER Barrier,
    _In_ ULONG Flags)
{
    if (Flags & RTL_BARRIER_FLAGS_BLOCK_ONLY)
        return 0;

    if (Flags & RTL_BARRIER_FLAGS_SPIN_ONLY)
        return MAXULONG;

    if (NtCurrentPeb()->NumberOfProcessors <= 1)
        return 0;

    if (Barrier->SpinCount < 0 || Barrier->SpinCount > RTLP_BARRIER_DEFAULT_SPIN)
        return RTLP_BARRIER_DEFAULT_SPIN;

    return (ULONG)Barrier->SpinCount;
}

static
LONG
RtlpBarrierWaitGateChange(
    _In_ PRTLP_BARRIER Barrier,
    _In_ LONG Observed,
    _In_ ULONG Flags,
    _Inout_ PULONG Spins)
{
    BOOLEAN SpinOnly = (Flags & (RTL_BARRIER_FLAGS_SPIN_ONLY | RTL_BARRIER_FLAGS_BLOCK_ONLY)) == RTL_BARRIER_FLAGS_SPIN_ONLY;
    LONG Value;

    for (;;)
    {
        Value = ReadAcquire(&Barrier->Gate);
        if (Value != Observed)
            return Value;

        if (SpinOnly || *Spins != 0)
        {
            if (*Spins != 0 && *Spins != MAXULONG)
                (*Spins)--;
            YieldProcessor();
            continue;
        }

        RtlWaitOnAddress(&Barrier->Gate, &Observed, sizeof(Observed), NULL);
    }
}

static
VOID
RtlpBarrierDepart(
    _In_ PRTLP_BARRIER Barrier)
{
    if ((ULONG)InterlockedIncrement(&Barrier->Departed) != (ULONG)Barrier->TotalThreads)
        return;

    Barrier->Departed = 0;
    InterlockedExchange(&Barrier->Gate, 0);
    RtlWakeAddressAll(&Barrier->Gate);
}

/*
 * @implemented
 */
NTSTATUS
NTAPI
RtlInitBarrier(
    _Out_ PRTL_BARRIER Barrier,
    _In_ LONG TotalThreads,
    _In_ LONG SpinCount)
{
    PRTLP_BARRIER State = (PRTLP_BARRIER)Barrier;

    if (Barrier == NULL)
        return STATUS_INVALID_PARAMETER;

    RtlZeroMemory(Barrier, sizeof(*Barrier));
    State->TotalThreads = TotalThreads;
    State->SpinCount = SpinCount;
    return STATUS_SUCCESS;
}

/*
 * @implemented
 */
BOOLEAN
NTAPI
RtlBarrier(
    _Inout_ PRTL_BARRIER Barrier,
    _In_ ULONG Flags)
{
    PRTLP_BARRIER State = (PRTLP_BARRIER)Barrier;
    ULONG Spins;
    ULONG Count;
    LONG Old;
    LONG New;
    BOOLEAN Last;

    if (Barrier == NULL)
        return FALSE;

    if (Flags & RTL_BARRIER_FLAGS_DELETE_SAFE)
        InterlockedIncrement(&State->Tracked);

    Spins = RtlpBarrierSpinLimit(State, Flags);
    for (;;)
    {
        Old = ReadAcquire(&State->Gate);
        if ((ULONG)Old & RTLP_BARRIER_CLOSED)
        {
            RtlpBarrierWaitGateChange(State, Old, Flags, &Spins);
            continue;
        }

        Count = ((ULONG)Old & RTLP_BARRIER_COUNT_MASK) + 1;
        New = (LONG)(((ULONG)Old & ~RTLP_BARRIER_COUNT_MASK) | Count);
        if (Flags & RTL_BARRIER_FLAGS_DELETE_SAFE)
            New = (LONG)((ULONG)New | RTLP_BARRIER_TRACKED);
        Last = (BOOLEAN)(Count == (ULONG)State->TotalThreads);
        if (Last)
            New = (LONG)((ULONG)New | RTLP_BARRIER_CLOSED);

        if (InterlockedCompareExchange(&State->Gate, New, Old) == Old)
            break;
    }

    if (Last)
    {
        RtlWakeAddressAll(&State->Gate);
    }
    else
    {
        Spins = RtlpBarrierSpinLimit(State, Flags);
        while (!((ULONG)New & RTLP_BARRIER_CLOSED))
            New = RtlpBarrierWaitGateChange(State, New, Flags, &Spins);
    }

    RtlpBarrierDepart(State);

    if ((Flags & RTL_BARRIER_FLAGS_DELETE_SAFE) && InterlockedDecrement(&State->Tracked) == 0)
        RtlWakeAddressAll(&State->Tracked);

    return Last;
}

/*
 * @implemented
 */
VOID
NTAPI
RtlDeleteBarrier(
    _In_ PRTL_BARRIER Barrier)
{
    PRTLP_BARRIER State = (PRTLP_BARRIER)Barrier;
    LONG Value;

    if (Barrier == NULL)
        return;

    for (;;)
    {
        Value = ReadAcquire(&State->Tracked);
        if (Value != 0)
        {
            RtlWaitOnAddress(&State->Tracked, &Value, sizeof(Value), NULL);
            continue;
        }

        Value = ReadAcquire(&State->Gate);
        if (!((ULONG)Value & RTLP_BARRIER_TRACKED) || !((ULONG)Value & RTLP_BARRIER_CLOSED))
            return;

        RtlWaitOnAddress(&State->Gate, &Value, sizeof(Value), NULL);
    }
}
