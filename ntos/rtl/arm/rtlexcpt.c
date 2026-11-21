/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     ARMv7 kernel stack walking
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

static volatile LONG RtlpArmWalkActive[MAXIMUM_PROCESSORS];

static
BOOLEAN
RtlpArmStackLimits(
    _In_ ULONG_PTR Address,
    _Out_ PULONG_PTR Low,
    _Out_ PULONG_PTR High)
{
    PKTHREAD Thread = KeGetCurrentThread();
    PKPRCB Prcb = KeGetCurrentPrcb();
    ULONG_PTR DpcStack;

    if (Thread && (Address >= Thread->StackLimit) && (Address < (ULONG_PTR)Thread->StackBase))
    {
        *Low = Address;
        *High = (ULONG_PTR)Thread->StackBase;
        return TRUE;
    }

    DpcStack = (ULONG_PTR)Prcb->DpcStack;
    if (DpcStack && (Address >= DpcStack - KERNEL_STACK_SIZE) && (Address < DpcStack))
    {
        *Low = Address;
        *High = DpcStack;
        return TRUE;
    }
    return FALSE;
}

/*
 * Walk the r11 frame records ({r11, lr} pairs) of the current kernel stack.
 * Every read is checked against the stack limits and the mapping first, so a
 * walk started from a damaged stack stops instead of faulting.
 */
ULONG
NTAPI
RtlWalkFrameChain(
    _Out_writes_(Count) PVOID *Callers,
    _In_ ULONG Count,
    _In_ ULONG Flags)
{
    ULONG_PTR Frame, Low, High, Next, Return;
    ULONG Skip = Flags >> 8, Captured = 0, Index, Processor;

    if (!Callers || !Count || ((Flags & 0xFF) == 1))
        return 0;

    Processor = KeGetCurrentProcessorNumber() % MAXIMUM_PROCESSORS;
    if (InterlockedCompareExchange(&RtlpArmWalkActive[Processor], 1, 0) != 0)
        return 0;

    __asm__ __volatile__("mov %0, r11" : "=r"(Frame));
    if (!RtlpArmStackLimits(Frame, &Low, &High))
        goto Done;

    for (Index = 0; Index < Skip + Count; Index++)
    {
        if ((Frame < Low) || (Frame > High - 2 * sizeof(ULONG_PTR)) || (Frame & 3) ||
            !MmIsAddressValid((PVOID)Frame) || !MmIsAddressValid((PVOID)(Frame + sizeof(ULONG_PTR))))
        {
            break;
        }

        Next = ((PULONG_PTR)Frame)[0];
        Return = ((PULONG_PTR)Frame)[1];
        if (!Return || (Return < (ULONG_PTR)MmSystemRangeStart))
            break;
        if (Index >= Skip)
            Callers[Captured++] = (PVOID)Return;
        if (Next <= Frame)
            break;
        Frame = Next;
    }

Done:
    InterlockedExchange(&RtlpArmWalkActive[Processor], 0);
    return Captured;
}
