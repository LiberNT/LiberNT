/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     What the windowing kernel registers with the process manager,
 *              and turning a thread into one that may call it
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#include "pspriv.h"
#define NDEBUG
#include <debug.h>

/*
 * These live with the components that call them, and no header publishes
 * them, so they are named here the way their own components name them.
 */
extern PKWIN32_GLOBALATOMTABLE_CALLOUT ExpGlobalAtomTableCallout;
extern PKWIN32_POWEREVENT_CALLOUT PopEventCallout;
extern PKWIN32_POWERSTATE_CALLOUT PopStateCallout;
extern PKWIN32_SESSION_CALLOUT ExpWindowStationObjectParse;
extern PKWIN32_SESSION_CALLOUT ExpWindowStationObjectDelete;
extern PKWIN32_SESSION_CALLOUT ExpWindowStationObjectOkToClose;
extern PKWIN32_SESSION_CALLOUT ExpWindowStationObjectOpen;
extern PKWIN32_SESSION_CALLOUT ExpWindowStationObjectClose;
extern PKWIN32_SESSION_CALLOUT ExpDesktopObjectOkToClose;
extern PKWIN32_SESSION_CALLOUT ExpDesktopObjectDelete;
extern PKWIN32_SESSION_CALLOUT ExpDesktopObjectOpen;
extern PKWIN32_SESSION_CALLOUT ExpDesktopObjectClose;

/* GLOBALS ********************************************************************/

PKWIN32_PROCESS_CALLOUT PspW32ProcessCallout;
PKWIN32_THREAD_CALLOUT PspW32ThreadCallout;
PKWIN32_JOB_CALLOUT PspW32JobCallout;
PGDI_BATCHFLUSH_ROUTINE KeGdiFlushUserBatch;

/* PUBLIC FUNCTIONS ***********************************************************/

/*
 * The windowing kernel hands over every routine the rest of the kernel needs
 * to call into it. They are taken in one call so that no half-registered set
 * is ever visible: before it, nothing calls out; after it, everything may.
 */
VOID
NTAPI
PsEstablishWin32Callouts(
    _In_ PWIN32_CALLOUTS_FPNS CalloutData)
{
    PAGED_CODE();

    PspW32ProcessCallout = CalloutData->ProcessCallout;
    PspW32ThreadCallout = CalloutData->ThreadCallout;
    PspW32JobCallout = CalloutData->JobCallout;

    ExpGlobalAtomTableCallout = CalloutData->GlobalAtomTableCallout;
    PopEventCallout = CalloutData->PowerEventCallout;
    PopStateCallout = CalloutData->PowerStateCallout;
    KeGdiFlushUserBatch = CalloutData->BatchFlushRoutine;

    ExpWindowStationObjectOpen = CalloutData->WindowStationOpenProcedure;
    ExpWindowStationObjectClose = CalloutData->WindowStationCloseProcedure;
    ExpWindowStationObjectDelete = CalloutData->WindowStationDeleteProcedure;
    ExpWindowStationObjectParse = CalloutData->WindowStationParseProcedure;
    ExpWindowStationObjectOkToClose =
        CalloutData->WindowStationOkToCloseProcedure;

    ExpDesktopObjectOpen = CalloutData->DesktopOpenProcedure;
    ExpDesktopObjectClose = CalloutData->DesktopCloseProcedure;
    ExpDesktopObjectDelete = CalloutData->DesktopDeleteProcedure;
    ExpDesktopObjectOkToClose = CalloutData->DesktopOkToCloseProcedure;
}

/*
 * Takes them all away again, which is what unloading the windowing kernel
 * needs: nothing may still be reaching into an image that is going.
 */
VOID
NTAPI
PsUnEstablishWin32Callouts(VOID)
{
    PAGED_CODE();

    PspW32ProcessCallout = NULL;
    PspW32ThreadCallout = NULL;
    PspW32JobCallout = NULL;

    ExpGlobalAtomTableCallout = NULL;
    PopEventCallout = NULL;
    PopStateCallout = NULL;
    KeGdiFlushUserBatch = NULL;

    ExpWindowStationObjectOpen = NULL;
    ExpWindowStationObjectClose = NULL;
    ExpWindowStationObjectDelete = NULL;
    ExpWindowStationObjectParse = NULL;
    ExpWindowStationObjectOkToClose = NULL;

    ExpDesktopObjectOpen = NULL;
    ExpDesktopObjectClose = NULL;
    ExpDesktopObjectDelete = NULL;
    ExpDesktopObjectOkToClose = NULL;
}

/*
 * Turns the calling thread into one that may call the windowing kernel. The
 * thread is given a larger kernel stack first, because the windowing calls
 * go deeper than an ordinary one, and only then is the windowing kernel told
 * about the thread.
 */
NTSTATUS
NTAPI
PsConvertToGuiThread(VOID)
{
    PETHREAD Thread = PsGetCurrentThread();
    PEPROCESS Process = PsGetCurrentProcess();
    PVOID OldStack;
    PVOID NewStack;
    NTSTATUS Status;

    PAGED_CODE();

    if (KeGetCurrentIrql() != PASSIVE_LEVEL)
        return STATUS_UNSUCCESSFUL;

    /* A thread with no user-mode side has nothing to show */
    if (Thread->Tcb.Teb == NULL) return STATUS_UNSUCCESSFUL;

    if (PspW32ThreadCallout == NULL) return STATUS_ACCESS_DENIED;

    if (Thread->Tcb.GuiThread) return STATUS_ALREADY_WIN32;

    NewStack = MmCreateKernelStack(TRUE, 0);
    if (NewStack == NULL) return STATUS_INSUFFICIENT_RESOURCES;

    /*
     * The switch does not return on the old stack, so nothing that lives on
     * it may be used after this point.
     */
    OldStack = KeSwitchKernelStack(NewStack,
                                   (PCHAR)NewStack - KERNEL_LARGE_STACK_COMMIT);
    MmDeleteKernelStack(OldStack, FALSE);

    /*
     * The thread's process is told first: the windowing kernel keeps state
     * per process that the per-thread state hangs off.
     */
    if (Process->Win32Process == NULL)
    {
        Status = PspW32ProcessCallout(Process, TRUE);
        if (!NT_SUCCESS(Status)) return Status;
    }

    Status = PspW32ThreadCallout(Thread, PsW32ThreadCalloutInitialize);
    if (!NT_SUCCESS(Status)) return Status;

    Thread->Tcb.GuiThread = TRUE;
    return STATUS_SUCCESS;
}
