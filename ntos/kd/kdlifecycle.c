/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     The debug link's own life: start, sleep, wake and shutdown
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#include "kd.h"
#include "kdterminal.h"
#ifdef KDBG
#include "../kdbg/kdb.h"
#endif

/* These names are the link's, not the interactive debugger's */
#ifdef KDBG
#undef KdD0Transition
#undef KdD3Transition
#undef KdSave
#undef KdRestore
#endif

#define NDEBUG
#include <debug.h>

/* PUBLIC FUNCTIONS ***********************************************************/

/*
 * The first thing the debugger does, before anything else in the system is
 * ready: work out where the output is to go and claim whatever hardware that
 * needs. Nothing here may allocate, wait, or touch a device driver.
 */
NTSTATUS
NTAPI
KdDebuggerInitialize0(
    _In_opt_ PLOADER_PARAMETER_BLOCK LoaderBlock)
{
    KdpSelectDebugMode(LoaderBlock);

#ifdef KDBG
    /* How the person at the terminal asked to be talked to */
    if ((LoaderBlock != NULL) && (LoaderBlock->LoadOptions != NULL))
        KdbpGetCommandLineSettings(LoaderBlock->LoadOptions);
#endif

    return STATUS_SUCCESS;
}

/*
 * By now there is a working system around the debugger, so the terminal can be
 * asked about itself and the interactive debugger can read its start-up file.
 */
NTSTATUS
NTAPI
KdDebuggerInitialize1(
    _In_opt_ PLOADER_PARAMETER_BLOCK LoaderBlock)
{
    UNREFERENCED_PARAMETER(LoaderBlock);

    KdpInitTerminal();

    return STATUS_SUCCESS;
}

/*
 * The system root is mounted. Whatever was waiting for a file to write to can
 * have it now.
 */
VOID
NTAPI
KdSystemRootAvailable(VOID)
{
    KdpInitializeProviders(KDP_BOOT_PHASE_SYSTEM_ROOT);
}

/*
 * The debug hardware has been powered back up and may have forgotten how it
 * was set.
 */
NTSTATUS
NTAPI
KdD0Transition(VOID)
{
    if (KdpDebugMode.Serial)
        KdPortInitializeEx(&SerialPortInfo, SerialPortNumber);

    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
KdD3Transition(VOID)
{
    return STATUS_SUCCESS;
}

/*
 * The machine is going to sleep. There is nothing in the providers that has to
 * be written down to survive it; the hardware is set up again on the way back.
 */
NTSTATUS
NTAPI
KdSave(
    _In_ BOOLEAN SleepTransition)
{
    UNREFERENCED_PARAMETER(SleepTransition);

    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
KdRestore(
    _In_ BOOLEAN SleepTransition)
{
    UNREFERENCED_PARAMETER(SleepTransition);

    if (KdpDebugMode.Serial)
        KdPortInitializeEx(&SerialPortInfo, SerialPortNumber);

    /* The terminal at the far end may be a different one, or gone */
    if (KdTermSerial)
        KdTermReportsSize = TRUE;

    return STATUS_SUCCESS;
}

/* EOF */
