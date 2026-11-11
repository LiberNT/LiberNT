/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Stopping the whole machine so one processor can look at it
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

/* GLOBALS ********************************************************************/

/*
 * Non-zero while the machine is frozen. The debugger reads it to tell a
 * machine it stopped itself from one that merely has interrupts off, and the
 * freeze path reads it to notice a second freeze arriving on another processor.
 */
ULONG KiFreezeFlag;

/* The level the processor that froze the machine came from. */
KIRQL KiOldIrql;

/* How deep the one freeze owner has nested its own freezes. */
static ULONG KiFreezeDepth;

/* The processor those freezes belong to; any other one has to wait its turn. */
static PKPRCB KiFreezeHolder;

/* Set when a processor never answered a freeze or a thaw: the machine is stopping. */
static volatile LONG KiFreezeFailure;

/* PUBLIC FUNCTIONS ***********************************************************/

VOID
KiSetFreezeFailure(VOID)
{
    InterlockedExchange(&KiFreezeFailure, TRUE);
}

BOOLEAN
KiHasFreezeFailure(VOID)
{
    return InterlockedCompareExchange(&KiFreezeFailure, FALSE, FALSE) != FALSE;
}

/*
 * Stops every other processor and leaves this one holding the machine.
 *
 * Interrupt time is stopped as soon as this processor holds the machine. Were
 * it left running, every timer in the system would be found overdue the
 * instant the machine is let go, and the first thing the debugger's user would
 * see after a breakpoint is a storm of expired timers that the break itself
 * caused.
 *
 * Answers whether interrupts were enabled, which the thaw needs to put back.
 */
BOOLEAN
NTAPI
KeFreezeExecution(
    _In_opt_ PKTRAP_FRAME TrapFrame,
    _In_opt_ PKEXCEPTION_FRAME ExceptionFrame)
{
    PKPRCB Prcb;
    BOOLEAN Enable;
    KIRQL OldIrql;

    UNREFERENCED_PARAMETER(TrapFrame);
    UNREFERENCED_PARAMETER(ExceptionFrame);

    /* Nothing may interrupt this processor from here until the thaw */
    Enable = KeDisableInterrupts();
    Prcb = KeGetCurrentPrcb();

    /*
     * A nested freeze does not stop the machine again: it is already stopped,
     * and the processors that were told to hold must not be told twice.
     */
    if (KiFreezeHolder == Prcb)
    {
        KiFreezeDepth++;
        return Enable;
    }

    OldIrql = KeGetCurrentIrql();
    if (OldIrql < HIGH_LEVEL)
        KfRaiseIrql(HIGH_LEVEL);

    /* The architecture layer holds the other processors where they are */
    KxFreezeExecution();

    KiFreezeInterruptTime();

    KiOldIrql = OldIrql;
    KiFreezeDepth = 1;
    KiFreezeFlag = 1;
    KiFreezeHolder = Prcb;

    return Enable;
}

/*
 * Lets the machine go again: the clock and the freeze state first, as the next
 * holder may take the machine the moment it is let go, then the other
 * processors, then this one's level, and interrupts last, so that nothing
 * arrives before the state it would look at is sound.
 */
VOID
NTAPI
KeThawExecution(
    _In_ BOOLEAN Enable)
{
    KIRQL OldIrql;

    /* Nothing is let go after a failed freeze: only the owner of the stop goes on */
    if (KiHasFreezeFailure())
    {
        if (!KiIsBugCheckEntryOwner(KeGetCurrentPrcb()))
        {
            for (;;)
                HalHaltSystem();
        }
        return;
    }

    ASSERT(KiFreezeDepth != 0);
    ASSERT(KiFreezeHolder == KeGetCurrentPrcb());

    if (KiFreezeDepth > 1)
    {
        KiFreezeDepth--;
        KeRestoreInterrupts(Enable);
        return;
    }

    OldIrql = KiOldIrql;
    KiFreezeDepth = 0;
    KiFreezeFlag = 0;
    KiFreezeHolder = NULL;
    KiThawInterruptTime();

    KxThawExecution();
    if (KiHasFreezeFailure())
        return;

    if (OldIrql < HIGH_LEVEL)
        KeLowerIrql(OldIrql);

    KeRestoreInterrupts(Enable);
}

/* EOF */
