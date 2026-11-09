/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Owning the debug link, and asking it whether the host wants in
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#include "kdpriv.h"
#define NDEBUG
#include <debug.h>

/* PRIVATE DATA ***************************************************************/

/*
 * The link is one piece of hardware and the engine talks over it with
 * interrupts off, so the lock has to remember what it interrupted. The engine
 * re-enters it a few levels deep at most, which is what this small stack is
 * for; a deeper nesting is a defect, not a condition to recover from.
 */
#define KDP_PORT_NESTING_MAX 8

static KIRQL KdpPortSavedIrql[KDP_PORT_NESTING_MAX];
static BOOLEAN KdpPortSavedInterrupts[KDP_PORT_NESTING_MAX];
static ULONG KdpPortNesting = 0;

/* PUBLIC FUNCTIONS ***********************************************************/

/*
 * Takes the debug link for this processor. Interrupts go off so that nothing
 * on this processor can start a second conversation in the middle of one.
 */
VOID
NTAPI
KdpPortLock(VOID)
{
    KIRQL OldIrql;
    BOOLEAN Interrupts;
    PKPRCB Prcb;

    OldIrql = KfRaiseIrql(HIGH_LEVEL);
    Interrupts = KeDisableInterrupts();

    Prcb = KeGetCurrentPrcb();

    if (KdpPortOwnerPrcb != Prcb)
    {
        KxAcquireSpinLock(&KdpDebuggerLock);
        KdpPortOwnerPrcb = Prcb;
        KdpPortNesting = 0;
    }

    ASSERT(KdpPortNesting < KDP_PORT_NESTING_MAX);
    if (KdpPortNesting < KDP_PORT_NESTING_MAX)
    {
        KdpPortSavedIrql[KdpPortNesting] = OldIrql;
        KdpPortSavedInterrupts[KdpPortNesting] = Interrupts;
    }
    KdpPortNesting++;

    KdpPortLocked = TRUE;
    KeMemoryBarrier();
}

/*
 * Gives the link back. Only the outermost release lets another processor in.
 */
VOID
NTAPI
KdpPortUnlock(VOID)
{
    KIRQL OldIrql;
    BOOLEAN Interrupts;

    ASSERT(KdpPortNesting != 0);
    if (KdpPortNesting == 0)
        return;

    KdpPortNesting--;

    if (KdpPortNesting < KDP_PORT_NESTING_MAX)
    {
        OldIrql = KdpPortSavedIrql[KdpPortNesting];
        Interrupts = KdpPortSavedInterrupts[KdpPortNesting];
    }
    else
    {
        OldIrql = HIGH_LEVEL;
        Interrupts = FALSE;
    }

    if (KdpPortNesting == 0)
    {
        KdpPortLocked = FALSE;
        KdpPortOwnerPrcb = NULL;
        KeMemoryBarrier();
        KxReleaseSpinLock(&KdpDebuggerLock);
    }

    KeRestoreInterrupts(Interrupts);
    KeLowerIrql(OldIrql);
}

/*
 * Asks the transport whether the host sent a break-in byte. The caller holds
 * the link, which is what makes it safe to read from it here.
 */
BOOLEAN
NTAPI
KdpPollBreakInWithPortLock(VOID)
{
    if (KdReceivePacket(PACKET_TYPE_KD_POLL_BREAKIN,
                        NULL,
                        NULL,
                        NULL,
                        NULL) != KdPacketReceived)
    {
        return FALSE;
    }

    KdpControlCPressed = TRUE;
    return TRUE;
}

/*
 * Whether the host has asked to stop the machine. Called from the clock tick
 * and from the boot path, so it has to be cheap when there is no debugger and
 * must never wait for anything.
 */
BOOLEAN
NTAPI
KdPollBreakIn(VOID)
{
    BOOLEAN BreakIn;

    if (!KdDebuggerEnabled || !KdpDebuggerStructuresInitialized)
        return FALSE;

    /* Already inside the debugger: the request would have nowhere to go */
    if (KdEnteredDebugger)
        return FALSE;

    /* A request the transport noticed earlier is answered once */
    if (KdpContext.KdpControlCPending)
    {
        KdpContext.KdpControlCPending = FALSE;
        KdpControlCPressed = TRUE;
        return TRUE;
    }

    KdpPortLock();
    BreakIn = KdpPollBreakInWithPortLock();
    KdpPortUnlock();

    return BreakIn;
}

/* EOF */
