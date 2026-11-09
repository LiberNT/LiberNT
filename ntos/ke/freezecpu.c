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

/* PUBLIC FUNCTIONS ***********************************************************/

/*
 * Stops every other processor and leaves this one holding the machine.
 *
 * Interrupt time is stopped first. Were it left running, every timer in the
 * system would be found overdue the instant the machine is let go, and the
 * first thing the debugger's user would see after a breakpoint is a storm of
 * expired timers that the break itself caused.
 *
 * Answers whether interrupts were enabled, which the thaw needs to put back.
 */
BOOLEAN
NTAPI
KeFreezeExecution(
    _In_opt_ PKTRAP_FRAME TrapFrame,
    _In_opt_ PKEXCEPTION_FRAME ExceptionFrame)
{
    BOOLEAN Enable;

    UNREFERENCED_PARAMETER(TrapFrame);
    UNREFERENCED_PARAMETER(ExceptionFrame);

    /* Nothing may interrupt this processor from here until the thaw */
    Enable = KeDisableInterrupts();

    /*
     * A nested freeze does not stop the machine again: it is already stopped,
     * and the processors that were told to hold must not be told twice.
     */
    if (KiFreezeDepth != 0)
    {
        KiFreezeDepth++;
        return Enable;
    }

    KiFreezeInterruptTime();

    KiOldIrql = KeGetCurrentIrql();
    if (KiOldIrql < HIGH_LEVEL)
        KfRaiseIrql(HIGH_LEVEL);

    KiFreezeDepth = 1;
    KiFreezeFlag = 1;

    /* The architecture layer holds the other processors where they are */
    KxFreezeExecution();

    return Enable;
}

/*
 * Lets the machine go again, in the order the freeze took it: the other
 * processors first, then this one's level, then the clock, and interrupts
 * last, so that nothing arrives before the state it would look at is sound.
 */
VOID
NTAPI
KeThawExecution(
    _In_ BOOLEAN Enable)
{
    ASSERT(KiFreezeDepth != 0);

    if (KiFreezeDepth > 1)
    {
        KiFreezeDepth--;
        KeRestoreInterrupts(Enable);
        return;
    }

    KxThawExecution();

    KiFreezeDepth = 0;
    KiFreezeFlag = 0;

    if (KiOldIrql < HIGH_LEVEL)
        KeLowerIrql(KiOldIrql);

    KiThawInterruptTime();

    KeRestoreInterrupts(Enable);
}

/* EOF */
