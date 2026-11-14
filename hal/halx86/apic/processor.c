/*
 * PROJECT:     ReactOS Hardware Abstraction Layer
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     HAL Processor Routines
 * COPYRIGHT:   Copyright 2010 Timo Kreuzer <timo.kreuzer@reactos.org>
 */

/* INCLUDES ******************************************************************/

#include <hal.h>
#include <smp.h>
#define NDEBUG
#include <debug.h>

KAFFINITY HalpActiveProcessors;
KAFFINITY HalpDefaultInterruptAffinity;

extern HALP_APIC_INFO_TABLE HalpApicInfoTable;

/* PRIVATE FUNCTIONS *********************************************************/

ULONG
NTAPI
HalQueryMaximumProcessorCount(VOID)
{
    ULONG Count = HalpApicInfoTable.ProcessorCount;
    return (Count != 0) ? Count : 1;
}

BOOLEAN
NTAPI
HalIsHyperThreadingEnabled(VOID)
{
    return (KeGetCurrentPrcb()->LogicalProcessorsPerCore > 1);
}

VOID
NTAPI
HaliHaltSystem(VOID)
{
    /* Disable interrupts and halt the CPU */
    _disable();
    __halt();
}

/* FUNCTIONS *****************************************************************/

/*
 * @implemented
 */
BOOLEAN
NTAPI
HalAllProcessorsStarted(VOID)
{
    /* Do nothing */
    return TRUE;
}

/*
 * @implemented
 */
VOID
NTAPI
HalProcessorIdle(VOID)
{
    /* Enable interrupts and halt the processor */
    _enable();
    __halt();
}

/* EOF */
