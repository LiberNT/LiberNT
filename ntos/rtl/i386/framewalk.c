/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Collects return addresses by following the ebp frame chain
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

static
BOOLEAN
RtlpKernelStackBounds(
    _In_ ULONG_PTR Frame,
    _Out_ PULONG_PTR Low,
    _Out_ PULONG_PTR High)
{
    PKTHREAD Thread = KeGetCurrentThread();
    ULONG_PTR DpcTop;

    if (KeGetCurrentIrql() > DISPATCH_LEVEL)
        return FALSE;

    if (Frame >= (ULONG_PTR)Thread->StackLimit && Frame <= (ULONG_PTR)Thread->StackBase)
    {
        *Low = Frame;
        *High = (ULONG_PTR)Thread->StackBase;
        return TRUE;
    }

    DpcTop = (ULONG_PTR)KeGetCurrentPrcb()->DpcStack;
    if (DpcTop != 0 && Frame > DpcTop - KERNEL_STACK_SIZE && Frame <= DpcTop)
    {
        *Low = Frame;
        *High = DpcTop;
        return TRUE;
    }

    return FALSE;
}

static
BOOLEAN
RtlpUserStackBounds(
    _Out_ PULONG_PTR Frame,
    _Out_ PULONG_PTR Low,
    _Out_ PULONG_PTR High)
{
    PKTHREAD Thread = KeGetCurrentThread();
    PKTRAP_FRAME TrapFrame;
    PTEB Teb = Thread->Teb;

    if (Teb == NULL || KeIsAttachedProcess() || KeGetCurrentIrql() >= DISPATCH_LEVEL)
        return FALSE;

    TrapFrame = KeGetTrapFrame(Thread);
    *Low = (ULONG_PTR)Teb->NtTib.StackLimit;
    *High = (ULONG_PTR)Teb->NtTib.StackBase;
    if (*High <= *Low)
        return FALSE;

    ProbeForRead((PVOID)*Low, *High - *Low, sizeof(UCHAR));
    *Frame = TrapFrame->Ebp;
    return TRUE;
}

ULONG
NTAPI
RtlWalkFrameChain(
    _Out_writes_to_(Count, return) PVOID *Callers,
    _In_ ULONG Count,
    _In_ ULONG Flags)
{
    ULONG_PTR Frame = (ULONG_PTR)_AddressOfReturnAddress() - sizeof(ULONG_PTR);
    ULONG_PTR Low = 0, High = 0, Next, Return;
    ULONG Found = 0;

    if (Flags > 1)
        return 0;

    if (Flags == 0 && !RtlpKernelStackBounds(Frame, &Low, &High))
        return 0;

    _SEH2_TRY
    {
        if (Flags == 1 && !RtlpUserStackBounds(&Frame, &Low, &High))
            _SEH2_YIELD(return 0);

        while (Found < Count)
        {
            if (Frame >= High || Frame < Low || (Found != 0 && Frame == Low) ||
                High - Frame < 2 * sizeof(ULONG_PTR))
            {
                break;
            }

            Next = ((PULONG_PTR)Frame)[0];
            Return = ((PULONG_PTR)Frame)[1];

            if (Return > Low && Return < High)
                break;

            if (Flags == 0 && Return < (ULONG_PTR)MmSystemRangeStart)
                break;

            Callers[Found++] = (PVOID)Return;

            if (Next <= Frame || Next >= High)
                break;

            Frame = Next;
        }
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Found = 0;
    }
    _SEH2_END;

    return Found;
}
