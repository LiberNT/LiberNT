/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Rundown protection: keeping an object alive while it is in use
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

/* PRIVATE DATA ***************************************************************/

#define EXP_RUNDOWN_TAG 'rnuR'

/*
 * While a rundown is being waited for, the reference holds the address of the
 * waiter's block instead of a count. The block is aligned well enough that the
 * active bit stays free.
 */
#define EXP_RUNDOWN_BLOCK_MASK (~((ULONG_PTR)EX_RUNDOWN_ACTIVE))

/* PRIVATE FUNCTIONS **********************************************************/

static
PEX_RUNDOWN_WAIT_BLOCK
ExpRundownWaitBlock(
    _In_ ULONG_PTR Value)
{
    return (PEX_RUNDOWN_WAIT_BLOCK)(Value & EXP_RUNDOWN_BLOCK_MASK);
}

static
ULONG
ExpRundownSlotCount(VOID)
{
    return KeNumberProcessors;
}

static
ULONG
ExpRundownSlotStride(VOID)
{
    ULONG Alignment;

    if (KeNumberProcessors == 1)
    {
        return sizeof(EX_RUNDOWN_REF);
    }

    Alignment = KeGetRecommendedSharedDataAlignment();
    if (Alignment < sizeof(EX_RUNDOWN_REF))
    {
        Alignment = sizeof(EX_RUNDOWN_REF);
    }

    return (sizeof(EX_RUNDOWN_REF) + Alignment - 1) & ~(Alignment - 1);
}

/* PUBLIC FUNCTIONS ***********************************************************/

VOID
FASTCALL
ExfInitializeRundownProtection(
    _Out_ PEX_RUNDOWN_REF RunRef)
{
    RunRef->Count = 0;
}

VOID
FASTCALL
ExfReInitializeRundownProtection(
    _Inout_ PEX_RUNDOWN_REF RunRef)
{
    ASSERT((RunRef->Count & EX_RUNDOWN_ACTIVE) != 0);

    ExpSetRundown(RunRef, 0);
}

BOOLEAN
FASTCALL
ExfAcquireRundownProtectionEx(
    _Inout_ PEX_RUNDOWN_REF RunRef,
    _In_ ULONG Count)
{
    ULONG_PTR Value, NewValue;

    for (;;)
    {
        Value = RunRef->Count;

        if (Value & EX_RUNDOWN_ACTIVE)
        {
            return FALSE;
        }

        NewValue = Value + (ULONG_PTR)Count * EX_RUNDOWN_COUNT_INC;

        if (ExpChangeRundown(RunRef, NewValue, Value) == Value)
        {
            return TRUE;
        }
    }
}

BOOLEAN
FASTCALL
ExfAcquireRundownProtection(
    _Inout_ PEX_RUNDOWN_REF RunRef)
{
    return ExfAcquireRundownProtectionEx(RunRef, 1);
}

VOID
FASTCALL
ExfReleaseRundownProtectionEx(
    _Inout_ PEX_RUNDOWN_REF RunRef,
    _In_ ULONG Count)
{
    ULONG_PTR Value, NewValue;
    PEX_RUNDOWN_WAIT_BLOCK WaitBlock;

    for (;;)
    {
        Value = RunRef->Count;

        if (Value & EX_RUNDOWN_ACTIVE)
        {
            WaitBlock = ExpRundownWaitBlock(Value);

            ASSERT(WaitBlock != NULL);

            /*
             * The waiter owns the block; the last reference to go wakes it, and
             * the block's count is only ever touched while the active bit is
             * set, so a plain subtraction is enough.
             */
            if (InterlockedExchangeAddSizeT((PSIZE_T)&WaitBlock->Count,
                                            -(SSIZE_T)Count) == (SIZE_T)Count)
            {
                KeSetEvent(&WaitBlock->WakeEvent, IO_NO_INCREMENT, FALSE);
            }

            return;
        }

        ASSERT(Value >= (ULONG_PTR)Count * EX_RUNDOWN_COUNT_INC);

        NewValue = Value - (ULONG_PTR)Count * EX_RUNDOWN_COUNT_INC;

        if (ExpChangeRundown(RunRef, NewValue, Value) == Value)
        {
            return;
        }
    }
}

VOID
FASTCALL
ExfReleaseRundownProtection(
    _Inout_ PEX_RUNDOWN_REF RunRef)
{
    ExfReleaseRundownProtectionEx(RunRef, 1);
}

VOID
FASTCALL
ExfRundownCompleted(
    _Out_ PEX_RUNDOWN_REF RunRef)
{
    ExpSetRundown(RunRef, EX_RUNDOWN_ACTIVE);
}

VOID
FASTCALL
ExfWaitForRundownProtectionRelease(
    _Inout_ PEX_RUNDOWN_REF RunRef)
{
    EX_RUNDOWN_WAIT_BLOCK WaitBlock;
    ULONG_PTR Value, NewValue, Count;

    Value = RunRef->Count;

    if ((Value == 0) &&
        (ExpChangeRundown(RunRef, EX_RUNDOWN_ACTIVE, 0) == 0))
    {
        return;
    }

    KeInitializeEvent(&WaitBlock.WakeEvent, NotificationEvent, FALSE);

    for (;;)
    {
        Value = RunRef->Count;

        if (Value & EX_RUNDOWN_ACTIVE)
        {
            return;
        }

        Count = Value / EX_RUNDOWN_COUNT_INC;
        WaitBlock.Count = Count;

        NewValue = (ULONG_PTR)&WaitBlock | EX_RUNDOWN_ACTIVE;

        if (ExpChangeRundown(RunRef, NewValue, Value) == Value)
        {
            if (Count != 0)
            {
                KeWaitForSingleObject(&WaitBlock.WakeEvent,
                                      WrRundown,
                                      KernelMode,
                                      FALSE,
                                      NULL);
            }

            return;
        }
    }
}

/*
 * A caller that places the descriptor itself gets one buffer holding the
 * descriptor and the per-processor slots behind it, so this size covers both.
 */
SIZE_T
NTAPI
ExSizeOfRundownProtectionCacheAware(VOID)
{
    return sizeof(EX_RUNDOWN_REF_CACHE_AWARE) +
           (SIZE_T)ExpRundownSlotStride() * ExpRundownSlotCount();
}

VOID
NTAPI
ExInitializeRundownProtectionCacheAware(
    _Out_ PEX_RUNDOWN_REF_CACHE_AWARE RunRefCacheAware,
    _In_ SIZE_T RunRefSize)
{
    ULONG Number = ExpRundownSlotCount();
    ULONG Stride = ExpRundownSlotStride();
    ULONG Index;

    ASSERT(RunRefSize >= ExSizeOfRundownProtectionCacheAware());
    UNREFERENCED_PARAMETER(RunRefSize);

    RunRefCacheAware->RunRefs =
        (PEX_RUNDOWN_REF)((PUCHAR)RunRefCacheAware +
                          sizeof(EX_RUNDOWN_REF_CACHE_AWARE));
    RunRefCacheAware->PoolToFree = NULL;
    RunRefCacheAware->RunRefSize = Stride;
    RunRefCacheAware->Number = Number;

    for (Index = 0; Index < Number; Index++)
    {
        ExfInitializeRundownProtection(
            ExGetRunRefForGivenProcessor(RunRefCacheAware, Index));
    }
}

PEX_RUNDOWN_REF_CACHE_AWARE
NTAPI
ExAllocateCacheAwareRundownProtection(
    _In_ POOL_TYPE PoolType,
    _In_ ULONG PoolTag)
{
    PEX_RUNDOWN_REF_CACHE_AWARE RunRefCacheAware;
    PEX_RUNDOWN_REF RunRefs;
    ULONG Number = ExpRundownSlotCount();
    ULONG Stride = ExpRundownSlotStride();
    ULONG Index;

    RunRefCacheAware = ExAllocatePoolZero(PoolType,
                                          sizeof(EX_RUNDOWN_REF_CACHE_AWARE),
                                          PoolTag);
    if (RunRefCacheAware == NULL)
    {
        return NULL;
    }

    RunRefs = ExAllocatePoolZero(PoolType, (SIZE_T)Stride * Number, PoolTag);
    if (RunRefs == NULL)
    {
        ExFreePoolWithTag(RunRefCacheAware, PoolTag);
        return NULL;
    }

    RunRefCacheAware->RunRefs = RunRefs;
    RunRefCacheAware->PoolToFree = RunRefs;
    RunRefCacheAware->RunRefSize = Stride;
    RunRefCacheAware->Number = Number;

    for (Index = 0; Index < Number; Index++)
    {
        ExfInitializeRundownProtection(
            ExGetRunRefForGivenProcessor(RunRefCacheAware, Index));
    }

    return RunRefCacheAware;
}

VOID
NTAPI
ExFreeCacheAwareRundownProtection(
    _Inout_ PEX_RUNDOWN_REF_CACHE_AWARE RunRefCacheAware)
{
    if (RunRefCacheAware->PoolToFree != NULL)
    {
        ExFreePool(RunRefCacheAware->PoolToFree);
        RunRefCacheAware->PoolToFree = NULL;
        RunRefCacheAware->RunRefs = NULL;
    }

    ExFreePool(RunRefCacheAware);
}

BOOLEAN
FASTCALL
ExfAcquireRundownProtectionCacheAwareEx(
    _Inout_ PEX_RUNDOWN_REF_CACHE_AWARE RunRefCacheAware,
    _In_ ULONG Count)
{
    PEX_RUNDOWN_REF RunRef;

    RunRef = ExGetRunRefForGivenProcessor(RunRefCacheAware,
                                          KeGetCurrentProcessorNumber());

    return ExfAcquireRundownProtectionEx(RunRef, Count);
}

BOOLEAN
FASTCALL
ExfAcquireRundownProtectionCacheAware(
    _Inout_ PEX_RUNDOWN_REF_CACHE_AWARE RunRefCacheAware)
{
    return ExfAcquireRundownProtectionCacheAwareEx(RunRefCacheAware, 1);
}

VOID
FASTCALL
ExfReleaseRundownProtectionCacheAwareEx(
    _Inout_ PEX_RUNDOWN_REF_CACHE_AWARE RunRefCacheAware,
    _In_ ULONG Count)
{
    PEX_RUNDOWN_REF RunRef;

    RunRef = ExGetRunRefForGivenProcessor(RunRefCacheAware,
                                          KeGetCurrentProcessorNumber());

    ExfReleaseRundownProtectionEx(RunRef, Count);
}

VOID
FASTCALL
ExfReleaseRundownProtectionCacheAware(
    _Inout_ PEX_RUNDOWN_REF_CACHE_AWARE RunRefCacheAware)
{
    ExfReleaseRundownProtectionCacheAwareEx(RunRefCacheAware, 1);
}

/*
 * A reference taken on one processor's slot may be given back on another, so no
 * single slot's count is meaningful on its own. Every slot is closed first, then
 * the references that are still outstanding are gathered onto one slot and
 * waited for there.
 */
VOID
FASTCALL
ExfWaitForRundownProtectionReleaseCacheAware(
    _Inout_ PEX_RUNDOWN_REF_CACHE_AWARE RunRefCacheAware)
{
    EX_RUNDOWN_WAIT_BLOCK WaitBlock;
    PEX_RUNDOWN_REF RunRef;
    ULONG_PTR Value, NewValue;
    ULONG Index;

    KeInitializeEvent(&WaitBlock.WakeEvent, NotificationEvent, FALSE);

    /*
     * The block is counted as one reference of its own until every slot has
     * been closed, so a release that lands while the slots are still being
     * closed cannot bring the count to zero and signal an unfinished wait.
     */
    WaitBlock.Count = 1;

    NewValue = (ULONG_PTR)&WaitBlock | EX_RUNDOWN_ACTIVE;

    for (Index = 0; Index < RunRefCacheAware->Number; Index++)
    {
        RunRef = ExGetRunRefForGivenProcessor(RunRefCacheAware, Index);

        for (;;)
        {
            Value = RunRef->Count;

            ASSERT((Value & EX_RUNDOWN_ACTIVE) == 0);

            if (ExpChangeRundown(RunRef, NewValue, Value) == Value)
            {
                break;
            }
        }

        if (Value != 0)
        {
            InterlockedExchangeAddSizeT((PSIZE_T)&WaitBlock.Count,
                                        (SSIZE_T)(Value /
                                                  EX_RUNDOWN_COUNT_INC));
        }
    }

    if (InterlockedExchangeAddSizeT((PSIZE_T)&WaitBlock.Count, -1) != 1)
    {
        KeWaitForSingleObject(&WaitBlock.WakeEvent,
                              WrRundown,
                              KernelMode,
                              FALSE,
                              NULL);
    }
}

VOID
FASTCALL
ExfRundownCompletedCacheAware(
    _Inout_ PEX_RUNDOWN_REF_CACHE_AWARE RunRefCacheAware)
{
    ULONG Index;

    for (Index = 0; Index < RunRefCacheAware->Number; Index++)
    {
        ExfRundownCompleted(ExGetRunRefForGivenProcessor(RunRefCacheAware,
                                                         Index));
    }
}

VOID
FASTCALL
ExfReInitializeRundownProtectionCacheAware(
    _Inout_ PEX_RUNDOWN_REF_CACHE_AWARE RunRefCacheAware)
{
    ULONG Index;

    for (Index = 0; Index < RunRefCacheAware->Number; Index++)
    {
        ExfReInitializeRundownProtection(
            ExGetRunRefForGivenProcessor(RunRefCacheAware, Index));
    }
}

/* EOF */
