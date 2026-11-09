/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Bringing the kernel debugger up, phase by phase
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#include "kdpriv.h"
#include "../kd/kd.h"
#define NDEBUG
#include <debug.h>

/* PRIVATE FUNCTIONS **********************************************************/

/*
 * Looks for one boot option in the loader's option string. An option counts
 * only when it stands on its own, so DEBUG does not answer for NODEBUG and
 * DEBUGPORT does not answer for DEBUG.
 */
static
PCSTR
KdpFindBootOption(
    _In_opt_ PCSTR Options,
    _In_ PCSTR Name)
{
    SIZE_T Length;
    PCSTR Current;

    if (Options == NULL)
        return NULL;

    Length = strlen(Name);
    Current = Options;

    while (*Current != ANSI_NULL)
    {
        /* Step over whatever separates one option from the next */
        while ((*Current == ' ') || (*Current == '\t') || (*Current == '/'))
            Current++;

        if (*Current == ANSI_NULL)
            break;

        if (_strnicmp(Current, Name, Length) == 0)
        {
            CHAR Follows = Current[Length];

            if ((Follows == ANSI_NULL) || (Follows == ' ') ||
                (Follows == '\t') || (Follows == '=') || (Follows == '/'))
            {
                return Current + Length;
            }
        }

        /* Not this one: skip to the end of the word */
        while ((*Current != ANSI_NULL) && (*Current != ' ') && (*Current != '\t'))
            Current++;
    }

    return NULL;
}

static
BOOLEAN
KdpBootOptionPresent(
    _In_opt_ PCSTR Options,
    _In_ PCSTR Name)
{
    return (KdpFindBootOption(Options, Name) != NULL);
}

/*
 * Reads the debugger's own boot options. Nothing here touches the transport:
 * which port to speak over is the transport's business, and it reads the same
 * string for itself.
 */
static
VOID
KdpReadBootOptions(
    _In_opt_ PLOADER_PARAMETER_BLOCK LoaderBlock)
{
    PCSTR Options = NULL;

    if (LoaderBlock != NULL)
        Options = LoaderBlock->LoadOptions;

    /* Nothing asked for means no debugger at all, and no port held open */
    if (Options == NULL)
    {
        KdPitchDebugger = TRUE;
        KdDebuggerEnabled = FALSE;
        return;
    }

    if (KdpBootOptionPresent(Options, "NODEBUG"))
    {
        KdPitchDebugger = TRUE;
        KdDebuggerEnabled = FALSE;
    }
    else if (KdpBootOptionPresent(Options, "CRASHDEBUG"))
    {
        /*
         * The transport stays claimed but the debugger is silent until
         * something goes wrong, at which point the bug check path enables it.
         */
        KdPitchDebugger = FALSE;
        KdDebuggerEnabled = FALSE;
        KdPreviouslyEnabled = TRUE;
        KdAutoEnableOnEvent = TRUE;
    }
    else if (KdpBootOptionPresent(Options, "DEBUG"))
    {
        KdPitchDebugger = FALSE;
        KdDebuggerEnabled = TRUE;
    }
    else
    {
        KdPitchDebugger = TRUE;
        KdDebuggerEnabled = FALSE;
    }

    /* Stop as soon as the host has symbols for the first module it loads */
    if (KdpBootOptionPresent(Options, "BREAK"))
        KdBreakAfterSymbolLoad = TRUE;

    /* Leave user-mode exceptions to whoever is debugging that process */
    if (KdpBootOptionPresent(Options, "NOUMEX"))
        KdIgnoreUmExceptions = TRUE;
}

static
VOID
KdpInitializeStructures(VOID)
{
    KeInitializeSpinLock(&KdpDebuggerLock);
    KeInitializeSpinLock(&KdpDataSpinLock);
    KeInitializeSpinLock(&KdpPrintSpinLock);
    KeInitializeSpinLock(&KdpTimeSlipEventLock);

    KdpContext.KdpDefaultRetries = KDP_MAXIMUM_RETRIES;
    KdpContext.KdpControlCPending = FALSE;

    KdpZeroMemory(KdpBreakpointTable, sizeof(KdpBreakpointTable));
    KdpBreakpointInstruction = KD_BREAKPOINT_VALUE;
    KdpOweBreakpoint = FALSE;
    BreakpointsSuspended = FALSE;

    KdPrintCircularBuffer = KdPrintDefaultCircularBuffer;
    KdPrintWritePointer = KdPrintDefaultCircularBuffer;
    KdPrintBufferSize = sizeof(KdPrintDefaultCircularBuffer);
    KdPrintRolloverCount = 0;

    /* The host walks this list to find every data block the system publishes */
    InitializeListHead(&KdpDebuggerDataListHead);
    KdRegisterDebuggerDataBlock('GBDK',
                                &KdDebuggerDataBlock.Header,
                                sizeof(KDDEBUGGER_DATA64));
}

static
VOID
KdpPublishVersion(VOID)
{
    KdVersionBlock.MinorVersion = (USHORT)(NtBuildNumber & 0xFFFF);
    KdVersionBlock.KernBase = (ULONG64)(ULONG_PTR)PsNtosImageBase;
    KdVersionBlock.PsLoadedModuleList = (ULONG64)(ULONG_PTR)&PsLoadedModuleList;
    KdVersionBlock.DebuggerDataList = (ULONG64)(ULONG_PTR)&KdpDebuggerDataListHead;

    KdDebuggerDataBlock.KernBase = KdVersionBlock.KernBase;
    KdDebuggerDataBlock.BreakpointWithStatus =
        (ULONG64)(ULONG_PTR)DbgBreakPointWithStatus;
}

/* PUBLIC FUNCTIONS ***********************************************************/

/*
 * Adds one block of well-known addresses to the list the host reads before it
 * has symbols. The caller owns the storage for as long as the system runs.
 */
BOOLEAN
NTAPI
KdRegisterDebuggerDataBlock(
    _In_ ULONG Tag,
    _In_ PDBGKD_DEBUG_DATA_HEADER64 DataHeader,
    _In_ ULONG Size)
{
    KIRQL OldIrql;
    PLIST_ENTRY Entry;

    KeAcquireSpinLock(&KdpDataSpinLock, &OldIrql);

    /* A block that is already on the list is not added a second time */
    for (Entry = KdpDebuggerDataListHead.Flink;
         Entry != &KdpDebuggerDataListHead;
         Entry = Entry->Flink)
    {
        if (Entry == (PLIST_ENTRY)&DataHeader->List)
        {
            KeReleaseSpinLock(&KdpDataSpinLock, OldIrql);
            return FALSE;
        }
    }

    DataHeader->OwnerTag = Tag;
    DataHeader->Size = Size;

    /*
     * The links the host reads are 64 bits wide whatever this kernel is, so
     * they are cleared first and the native insert then zero-extends them.
     */
    DataHeader->List.Flink = 0;
    DataHeader->List.Blink = 0;
    InsertTailList(&KdpDebuggerDataListHead, (PLIST_ENTRY)&DataHeader->List);

    KeReleaseSpinLock(&KdpDataSpinLock, OldIrql);
    return TRUE;
}

/*
 * Fills in what the data block can only say once the structures it describes
 * exist. Called again whenever one of those moves.
 */
VOID
NTAPI
KdUpdateDataBlock(VOID)
{
    KdDebuggerDataBlock.MmPageSize = PAGE_SIZE;
    KdDebuggerDataBlock.PsLoadedModuleList = (ULONG64)(ULONG_PTR)&PsLoadedModuleList;
    KdDebuggerDataBlock.PsActiveProcessHead = (ULONG64)(ULONG_PTR)&PsActiveProcessHead;
    KdDebuggerDataBlock.PspCidTable = (ULONG64)(ULONG_PTR)&PspCidTable;
    KdDebuggerDataBlock.KdPrintCircularBuffer = (ULONG64)(ULONG_PTR)KdPrintCircularBuffer;
    KdDebuggerDataBlock.KdPrintCircularBufferEnd =
        (ULONG64)(ULONG_PTR)KdPrintCircularBuffer + KdPrintBufferSize;
    KdDebuggerDataBlock.KeLoaderBlock = (ULONG64)(ULONG_PTR)&KeLoaderBlock;
}

/*
 * Brings the debugger up one boot phase at a time. Phase 0 is the engine and
 * the transport; every later phase belongs to the output providers, which need
 * more of the system than the engine does.
 */
BOOLEAN
NTAPI
KdInitSystem(
    _In_ ULONG BootPhase,
    _In_opt_ PLOADER_PARAMETER_BLOCK LoaderBlock)
{
    NTSTATUS Status;
    LARGE_INTEGER Frequency;

    if (BootPhase > 0)
    {
        if (BootPhase == 1)
        {
            /*
             * The clock can only be corrected once there is a thread to do it
             * on, which there is from this phase onwards.
             */
            KeInitializeDpc(&KdpTimeSlipDpc, KdpTimeSlipDpcRoutine, NULL);
            KeInitializeTimer(&KdpTimeSlipTimer);
        }

        /* The providers decide for themselves what each later phase means */
        Status = KdpInitializeProviders(BootPhase);
        return NT_SUCCESS(Status);
    }

    /*
     * A second phase-0 call is the bug check path asking for a debugger that
     * was configured but left disabled. Everything is already built, so only
     * the switch has to be thrown.
     */
    if (KdpDebuggerStructuresInitialized)
    {
        if (!KdPitchDebugger && !KdDebuggerEnabled)
            KdEnableDebuggerWithLock(FALSE);

        return TRUE;
    }

    KdpReadBootOptions(LoaderBlock);
    KdpInitializeStructures();
    KdpPublishVersion();

    /* Timing the host asks about, taken before anything else can skew it */
    KeQueryPerformanceCounter(&Frequency);
    KdPerformanceCounterRate = Frequency;
    KdTimerStart.QuadPart = 0;
    KdTimerStop.QuadPart = 0;
    KdTimerDifference.QuadPart = 0;

    if (KdPitchDebugger)
    {
        /* No transport is claimed and no trap is ours */
        KiDebugRoutine = KdpStub;
        KdDebuggerEnabled = FALSE;
        KdDebuggerNotPresent = TRUE;
        KdpDebuggerStructuresInitialized = TRUE;
        return TRUE;
    }

    /* Let the transport claim its hardware before anyone sends a packet */
    Status = KdDebuggerInitialize0(LoaderBlock);
    if (!NT_SUCCESS(Status))
    {
        KdPitchDebugger = TRUE;
        KdDebuggerEnabled = FALSE;
        KdDebuggerNotPresent = TRUE;
        KiDebugRoutine = KdpStub;
        KdpDebuggerStructuresInitialized = TRUE;
        return TRUE;
    }

    KdpDebuggerStructuresInitialized = TRUE;

    /* Traps are ours from here on */
    KiDebugRoutine = KdpTrap;

    if (KdDebuggerEnabled)
    {
        KdPreviouslyEnabled = TRUE;

        /*
         * Nothing has been heard from the host yet. The first packet that
         * times out leaves this set, and the first one that is answered
         * clears it.
         */
        KdDebuggerNotPresent = TRUE;
    }

    Status = KdpInitializeProviders(BootPhase);
    return NT_SUCCESS(Status);
}

/* TIME SLIP ******************************************************************/

/*
 * The host stops the machine for as long as a person takes to read a screen,
 * and the clock has to be told. Both halves run outside the debugger, so the
 * work item does the part that may block and the DPC only schedules it.
 */
VOID
NTAPI
KdpTimeSlipWork(
    _In_ PVOID Context)
{
    KIRQL OldIrql;

    UNREFERENCED_PARAMETER(Context);

    /* Tell whoever is waiting that the clock has to be looked at again */
    KeAcquireSpinLock(&KdpTimeSlipEventLock, &OldIrql);
    if (KdpTimeSlipEvent != NULL)
        KeSetEvent(KdpTimeSlipEvent, 0, FALSE);
    KeReleaseSpinLock(&KdpTimeSlipEventLock, OldIrql);

    InterlockedExchange(&KdpTimeSlipPending, 0);
}

VOID
NTAPI
KdpTimeSlipDpcRoutine(
    _In_ PKDPC Dpc,
    _In_opt_ PVOID DeferredContext,
    _In_opt_ PVOID SystemArgument1,
    _In_opt_ PVOID SystemArgument2)
{
    UNREFERENCED_PARAMETER(Dpc);
    UNREFERENCED_PARAMETER(DeferredContext);
    UNREFERENCED_PARAMETER(SystemArgument1);
    UNREFERENCED_PARAMETER(SystemArgument2);

    /* One slip at a time: a second timer tick while one is pending is dropped */
    if (InterlockedCompareExchange(&KdpTimeSlipPending, 1, 0) != 0)
        return;

    ExInitializeWorkItem(&KdpTimeSlipWorkItem, KdpTimeSlipWork, NULL);
    ExQueueWorkItem(&KdpTimeSlipWorkItem, DelayedWorkQueue);
}

/* EOF */
