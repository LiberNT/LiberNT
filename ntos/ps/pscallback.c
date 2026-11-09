/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Telling drivers that a process or a thread came or went, and
 *              that an image was mapped
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#include "pspriv.h"
#define NDEBUG
#include <debug.h>

/* GLOBALS ********************************************************************/

EX_CALLBACK PspProcessNotifyRoutine[PSP_MAX_CREATE_PROCESS_NOTIFY];
EX_CALLBACK PspThreadNotifyRoutine[PSP_MAX_CREATE_THREAD_NOTIFY];
EX_CALLBACK PspLoadImageNotifyRoutine[PSP_MAX_LOAD_IMAGE_NOTIFY];

ULONG PspProcessNotifyRoutineCount;
ULONG PspThreadNotifyRoutineCount;
ULONG PspLoadImageNotifyRoutineCount;

PLEGO_NOTIFY_ROUTINE PspLegoNotifyRoutine;
BOOLEAN PsImageNotifyEnabled;

/* PRIVATE FUNCTIONS **********************************************************/

/*
 * Puts a routine into the first free slot of a table, or takes it out of the
 * slot it is in. A routine is registered once: asking again for one already
 * there is refused, so a driver that registers twice learns about it rather
 * than being called twice.
 *
 * The context is what tells the caller of the slot which shape the routine
 * has, so a legacy routine and an extended one can share one table.
 */
static
NTSTATUS
PspChangeNotifyRoutine(
    _Inout_ PEX_CALLBACK Table,
    _In_ ULONG TableLength,
    _Inout_ PULONG Count,
    _In_ PVOID NotifyRoutine,
    _In_opt_ PVOID Context,
    _In_ BOOLEAN Remove)
{
    PEX_CALLBACK_ROUTINE_BLOCK Block;
    PEX_CALLBACK_ROUTINE_BLOCK New;
    ULONG Index;

    PAGED_CODE();

    if (Remove)
    {
        for (Index = 0; Index < TableLength; Index++)
        {
            Block = ExReferenceCallBackBlock(&Table[Index]);
            if (Block == NULL) continue;

            if (ExGetCallBackBlockRoutine(Block) !=
                (PEX_CALLBACK_FUNCTION)NotifyRoutine)
            {
                ExDereferenceCallBackBlock(&Table[Index], Block);
                continue;
            }

            if (ExCompareExchangeCallBack(&Table[Index], NULL, Block))
            {
                InterlockedDecrement((PLONG)Count);
                ExDereferenceCallBackBlock(&Table[Index], Block);

                /* Nothing may still be inside the routine when it is freed */
                ExWaitForCallBacks(Block);
                ExFreeCallBack(Block);
                return STATUS_SUCCESS;
            }

            ExDereferenceCallBackBlock(&Table[Index], Block);
        }

        return STATUS_PROCEDURE_NOT_FOUND;
    }

    /* A routine already in the table is not put in a second time */
    for (Index = 0; Index < TableLength; Index++)
    {
        Block = ExReferenceCallBackBlock(&Table[Index]);
        if (Block == NULL) continue;

        if (ExGetCallBackBlockRoutine(Block) ==
            (PEX_CALLBACK_FUNCTION)NotifyRoutine)
        {
            ExDereferenceCallBackBlock(&Table[Index], Block);
            return STATUS_INVALID_PARAMETER;
        }

        ExDereferenceCallBackBlock(&Table[Index], Block);
    }

    New = ExAllocateCallBack((PEX_CALLBACK_FUNCTION)NotifyRoutine, Context);
    if (New == NULL) return STATUS_INSUFFICIENT_RESOURCES;

    for (Index = 0; Index < TableLength; Index++)
    {
        if (ExCompareExchangeCallBack(&Table[Index], New, NULL))
        {
            InterlockedIncrement((PLONG)Count);
            return STATUS_SUCCESS;
        }
    }

    ExFreeCallBack(New);
    return STATUS_INVALID_PARAMETER;
}

/* PUBLIC FUNCTIONS ***********************************************************/

/*
 * The older shape of the process notification, which is told only the two
 * identifiers and whether the process is coming or going.
 */
NTSTATUS
NTAPI
PsSetCreateProcessNotifyRoutine(
    _In_ PCREATE_PROCESS_NOTIFY_ROUTINE NotifyRoutine,
    _In_ BOOLEAN Remove)
{
    PAGED_CODE();

    return PspChangeNotifyRoutine(PspProcessNotifyRoutine,
                                  PSP_MAX_CREATE_PROCESS_NOTIFY,
                                  &PspProcessNotifyRoutineCount,
                                  NotifyRoutine,
                                  NULL,
                                  Remove);
}

/*
 * The newer shape, which is told the process object and may refuse the
 * creation by writing a failure into the information it is given.
 */
NTSTATUS
NTAPI
PsSetCreateProcessNotifyRoutineEx(
    _In_ PCREATE_PROCESS_NOTIFY_ROUTINE_EX NotifyRoutine,
    _In_ BOOLEAN Remove)
{
    PAGED_CODE();

    /* TODO: refuse an unsigned caller with STATUS_ACCESS_DENIED once image
     * signing is enforced on the kernel's own callers */
    return PspChangeNotifyRoutine(PspProcessNotifyRoutine,
                                  PSP_MAX_CREATE_PROCESS_NOTIFY,
                                  &PspProcessNotifyRoutineCount,
                                  NotifyRoutine,
                                  (PVOID)1,
                                  Remove);
}

/*
 * The same, for a caller that also wants to hear about the processes of
 * another subsystem.
 */
NTSTATUS
NTAPI
PsSetCreateProcessNotifyRoutineEx2(
    _In_ PSCREATEPROCESSNOTIFYTYPE NotifyType,
    _In_ PVOID NotifyInformation,
    _In_ BOOLEAN Remove)
{
    PAGED_CODE();

    if (NotifyType != PsCreateProcessNotifySubsystems)
        return STATUS_INVALID_PARAMETER;

    return PsSetCreateProcessNotifyRoutineEx(
               (PCREATE_PROCESS_NOTIFY_ROUTINE_EX)NotifyInformation,
               Remove);
}

NTSTATUS
NTAPI
PsSetCreateThreadNotifyRoutine(
    _In_ PCREATE_THREAD_NOTIFY_ROUTINE NotifyRoutine)
{
    PAGED_CODE();

    return PspChangeNotifyRoutine(PspThreadNotifyRoutine,
                                  PSP_MAX_CREATE_THREAD_NOTIFY,
                                  &PspThreadNotifyRoutineCount,
                                  NotifyRoutine,
                                  NULL,
                                  FALSE);
}

NTSTATUS
NTAPI
PsRemoveCreateThreadNotifyRoutine(
    _In_ PCREATE_THREAD_NOTIFY_ROUTINE NotifyRoutine)
{
    PAGED_CODE();

    return PspChangeNotifyRoutine(PspThreadNotifyRoutine,
                                  PSP_MAX_CREATE_THREAD_NOTIFY,
                                  &PspThreadNotifyRoutineCount,
                                  NotifyRoutine,
                                  NULL,
                                  TRUE);
}

/*
 * A load-image routine hears about every image mapped into any process from
 * the moment it is registered, which is why the flag that says anybody is
 * listening is set here and never cleared: an image mapped while a routine
 * was registered has to be reported even if the routine goes away after.
 */
NTSTATUS
NTAPI
PsSetLoadImageNotifyRoutine(
    _In_ PLOAD_IMAGE_NOTIFY_ROUTINE NotifyRoutine)
{
    NTSTATUS Status;

    PAGED_CODE();

    Status = PspChangeNotifyRoutine(PspLoadImageNotifyRoutine,
                                    PSP_MAX_LOAD_IMAGE_NOTIFY,
                                    &PspLoadImageNotifyRoutineCount,
                                    NotifyRoutine,
                                    NULL,
                                    FALSE);
    if (NT_SUCCESS(Status)) PsImageNotifyEnabled = TRUE;

    return Status;
}

NTSTATUS
NTAPI
PsRemoveLoadImageNotifyRoutine(
    _In_ PLOAD_IMAGE_NOTIFY_ROUTINE NotifyRoutine)
{
    PAGED_CODE();

    return PspChangeNotifyRoutine(PspLoadImageNotifyRoutine,
                                  PSP_MAX_LOAD_IMAGE_NOTIFY,
                                  &PspLoadImageNotifyRoutineCount,
                                  NotifyRoutine,
                                  NULL,
                                  TRUE);
}

/*
 * Tells whoever is listening that an image was mapped. The image is named by
 * the path it was opened under and by the process it was mapped into; a
 * driver image is reported with no process.
 */
VOID
NTAPI
PsCallImageNotifyRoutines(
    _In_opt_ PUNICODE_STRING FullImageName,
    _In_opt_ HANDLE ProcessId,
    _In_ PIMAGE_INFO ImageInfo)
{
    PAGED_CODE();

    if (!PsImageNotifyEnabled) return;

    PspRunLoadImageNotifyRoutines(FullImageName, ProcessId, ImageInfo);
}

/*
 * The routine the power manager calls on every thread as the machine is put
 * to sleep, so that a thread's state is saved while it is still there.
 */
VOID
NTAPI
PsSetLegoNotifyRoutine(
    _In_ PLEGO_NOTIFY_ROUTINE LegoNotifyRoutine)
{
    PAGED_CODE();

    PspLegoNotifyRoutine = LegoNotifyRoutine;
}
