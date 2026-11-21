/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     The interactive debugger: what it stops for and what it holds
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#include "kdb.h"
#include "kdbpriv.h"
#include "../kd/kdterminal.h"
#define NDEBUG
#include <debug.h>

/* GLOBALS ********************************************************************/

/* What the debugger is looking at right now */
PEPROCESS KdbCurrentProcess = NULL;
PETHREAD KdbCurrentThread = NULL;
PETHREAD KdbOriginalThread = NULL;
PEPROCESS KdbOriginalProcess = NULL;
PKDB_KTRAP_FRAME KdbCurrentTrapFrame = NULL;

EXCEPTION_RECORD64 KdbCurrentExceptionRecord;
BOOLEAN KdbCurrentExceptionRecordValid = FALSE;

/* Whether the machine is stopped inside the debugger */
BOOLEAN KdbActive = FALSE;

/* The bugcheck command's crash, raised once the debugger has let go of the machine */
BOOLEAN KdbBugCheckRequested = FALSE;

/* The breakpoint the machine last stopped at, or -1 */
LONG KdbLastBreakPointNr = -1;
PKTHREAD KdbBreakRequestThread = NULL;

/* How many instructions are still to be stepped, and whether calls are entered */
ULONG KdbNumSingleSteps = 0;
BOOLEAN KdbSingleStepOver = FALSE;

/* PRIVATE DATA ***************************************************************/

#define KDB_MAXIMUM_BREAKPOINT_COUNT    64
#define KDB_CONDITION_LENGTH            128

/* The exception numbers the enter conditions are kept per */
#define KDB_MAXIMUM_EXCEPTION_NR        19

static KDB_BREAKPOINT KdbpBreakPoints[KDB_MAXIMUM_BREAKPOINT_COUNT];

/*
 * The text of each breakpoint's condition, kept here rather than allocated:
 * breakpoints are set with the machine stopped, where nothing may allocate.
 */
static CHAR KdbpConditionText[KDB_MAXIMUM_BREAKPOINT_COUNT][KDB_CONDITION_LENGTH];
static CHAR KdbpCommandText[KDB_MAXIMUM_BREAKPOINT_COUNT][KDB_BREAKPOINT_COMMANDS_MAX];

/* The commands of the breakpoint the machine stopped at, kept for the prompt: a one-shot breakpoint is gone by then */
static CHAR KdbpStopCommands[KDB_BREAKPOINT_COMMANDS_MAX];

/* Which of this architecture's watchpoint registers is taken by which breakpoint */
static LONG KdbpHardwareOwner[8];

/* When to stop, per exception and per chance */
static KDB_ENTER_CONDITION KdbpEnterCondition[KDB_MAXIMUM_EXCEPTION_NR + 1][2];

#ifndef EXCEPTION_SOFTWARE_ORIGINATE
#define EXCEPTION_SOFTWARE_ORIGINATE 0x80
#endif

/* What the person at the prompt decided to do on the way out */
static KD_CONTINUE_TYPE KdbpContinueType = kdHandleException;

/* The breakpoints taken out of memory for one instruction, each until its thread's step is done */
typedef struct _KDB_STEP_OVER
{
    PKTHREAD Thread;
    LONG BreakPointNr;
    BOOLEAN InUse;
} KDB_STEP_OVER;

static KDB_STEP_OVER KdbpStepOvers[MAXIMUM_PROCESSORS];

/* The thread the person at the prompt is stepping */
static PKTHREAD KdbpStepThread;

static BOOLEAN KdbpEnterConditionsInitialised = FALSE;

/* PRIVATE FUNCTIONS **********************************************************/

/*
 * The number the enter conditions are kept under. It is the number the
 * processor would raise for the fault, which is what a person setting a
 * condition is thinking of.
 */
static
LONG
KdbpExceptionNumber(
    _In_ NTSTATUS Code)
{
    switch (Code)
    {
        case STATUS_INTEGER_DIVIDE_BY_ZERO: return 0;
        case STATUS_SINGLE_STEP: return 1;
        case STATUS_BREAKPOINT: return 3;
        case STATUS_INTEGER_OVERFLOW: return 4;
        case STATUS_ARRAY_BOUNDS_EXCEEDED: return 5;
        case STATUS_ILLEGAL_INSTRUCTION: return 6;
        case STATUS_FLOAT_INVALID_OPERATION: return 7;
        case STATUS_STACK_OVERFLOW: return 12;
        case STATUS_ACCESS_VIOLATION: return 14;
        case STATUS_IN_PAGE_ERROR: return 14;
        case STATUS_DATATYPE_MISALIGNMENT: return 17;
        default: return -1;
    }
}

static
VOID
KdbpInitializeEnterConditions(VOID)
{
    LONG Index;

    if (KdbpEnterConditionsInitialised)
        return;

    for (Index = 0; Index <= KDB_MAXIMUM_EXCEPTION_NR; Index++)
    {
        /*
         * A fault in a process is that process's own business until nothing
         * else has dealt with it; a fault in the kernel is always the
         * debugger's.
         */
        KdbpEnterCondition[Index][0] = KdbEnterFromKmode;
        KdbpEnterCondition[Index][1] = KdbEnterAlways;
    }

    for (Index = 0; Index < (LONG)RTL_NUMBER_OF(KdbpHardwareOwner); Index++)
        KdbpHardwareOwner[Index] = -1;

    KdbpEnterConditionsInitialised = TRUE;
}

static
PKDB_BREAKPOINT
KdbpBreakPointFromNr(
    _In_ LONG BreakPointNr)
{
    if ((BreakPointNr < 0) || (BreakPointNr >= KDB_MAXIMUM_BREAKPOINT_COUNT))
        return NULL;

    if (KdbpBreakPoints[BreakPointNr].Type == KdbBreakPointNone)
        return NULL;

    return &KdbpBreakPoints[BreakPointNr];
}

static
LONG
KdbpBreakPointNr(
    _In_ PKDB_BREAKPOINT BreakPoint)
{
    LONG Index = (LONG)(BreakPoint - KdbpBreakPoints);

    if ((Index < 0) || (Index >= KDB_MAXIMUM_BREAKPOINT_COUNT))
        return -1;

    return Index;
}

/*
 * Whether a breakpoint belongs to what is running now. A breakpoint set on a
 * user address belongs to the process it was set in; one in the system
 * belongs everywhere.
 */
static
BOOLEAN
KdbpBreakPointApplies(
    _In_ PKDB_BREAKPOINT BreakPoint)
{
    if (BreakPoint->Global)
        return TRUE;

    return (BreakPoint->Process == KdbCurrentProcess);
}

/*
 * Puts a software breakpoint's instruction into memory, or takes it back out.
 */
static
NTSTATUS
KdbpApplySoftwareBreakPoint(
    _Inout_ PKDB_BREAKPOINT BreakPoint,
    _In_ BOOLEAN Enable)
{
    KD_BREAKPOINT_TYPE Instruction = KD_BREAKPOINT_VALUE;
    PEPROCESS Process = BreakPoint->Global ? NULL : BreakPoint->Process;
    NTSTATUS Status;

    /* A breakpoint of a process is written in that process, wherever the machine stopped */
    if (Process == PsGetCurrentProcess())
        Process = NULL;

    if (Enable)
    {
        Status = KdpCopyProcessMemoryChunks(Process,
                                            (ULONG64)BreakPoint->Address,
                                            &BreakPoint->Data.SavedInstruction,
                                            sizeof(KD_BREAKPOINT_TYPE),
                                            0,
                                            MMDBG_COPY_UNSAFE,
                                            NULL);
        if (!NT_SUCCESS(Status))
            return Status;
    }

    Status = KdpCopyProcessMemoryChunks(Process,
                                        (ULONG64)BreakPoint->Address,
                                        Enable ? &Instruction : &BreakPoint->Data.SavedInstruction,
                                        sizeof(KD_BREAKPOINT_TYPE),
                                        0,
                                        MMDBG_COPY_UNSAFE | MMDBG_COPY_WRITE,
                                        NULL);

    /* The instruction fetch path does not see a store to code until it is told to */
    if (NT_SUCCESS(Status))
    {
        if (Process == NULL)
            KeSweepICache((PVOID)BreakPoint->Address, sizeof(KD_BREAKPOINT_TYPE));
        else
            KeSweepICache(NULL, 0);
    }

    return Status;
}

static
NTSTATUS
KdbpApplyBreakPoint(
    _Inout_ PKDB_BREAKPOINT BreakPoint,
    _In_ BOOLEAN Enable)
{
    if (BreakPoint->Type == KdbBreakPointHardware)
    {
        if (KdbCurrentTrapFrame == NULL)
            return STATUS_UNSUCCESSFUL;

        return KdbpArchSetHardwareBreakPoint(KdbCurrentTrapFrame,
                                             BreakPoint->Data.Hw.DebugReg,
                                             BreakPoint->Address,
                                             BreakPoint->Data.Hw.Size,
                                             BreakPoint->Data.Hw.AccessType,
                                             Enable);
    }

    return KdbpApplySoftwareBreakPoint(BreakPoint, Enable);
}

/*
 * The breakpoint at an address, if there is one that applies here.
 */
static
LONG
KdbpFindBreakPointAt(
    _In_ ULONG_PTR Address)
{
    LONG Index;

    for (Index = 0; Index < KDB_MAXIMUM_BREAKPOINT_COUNT; Index++)
    {
        PKDB_BREAKPOINT BreakPoint = &KdbpBreakPoints[Index];

        if (BreakPoint->Type == KdbBreakPointNone)
            continue;

        if (BreakPoint->Address != Address)
            continue;

        if (!KdbpBreakPointApplies(BreakPoint))
            continue;

        return Index;
    }

    return -1;
}

static
LONG
KdbpFindStepOver(
    _In_opt_ PKTHREAD Thread)
{
    LONG Index;

    for (Index = 0; Index < (LONG)RTL_NUMBER_OF(KdbpStepOvers); Index++)
    {
        if (KdbpStepOvers[Index].InUse && (KdbpStepOvers[Index].Thread == Thread))
            return Index;
    }

    return -1;
}

/* Whether some step still has this breakpoint's instruction out of memory */
static
BOOLEAN
KdbpBreakPointSteppedOver(
    _In_ LONG BreakPointNr)
{
    ULONG Index;

    if (BreakPointNr < 0)
        return FALSE;

    for (Index = 0; Index < RTL_NUMBER_OF(KdbpStepOvers); Index++)
    {
        if (KdbpStepOvers[Index].InUse && (KdbpStepOvers[Index].BreakPointNr == BreakPointNr))
            return TRUE;
    }

    return FALSE;
}

static
VOID
KdbpEndStepOverAt(
    _In_ LONG Index)
{
    PKDB_BREAKPOINT BreakPoint;
    LONG BreakPointNr;

    ASSERT((Index >= 0) && (Index < (LONG)RTL_NUMBER_OF(KdbpStepOvers)));
    ASSERT(KdbpStepOvers[Index].InUse);

    BreakPointNr = KdbpStepOvers[Index].BreakPointNr;
    KdbpStepOvers[Index].InUse = FALSE;
    KdbpStepOvers[Index].Thread = NULL;
    KdbpStepOvers[Index].BreakPointNr = -1;

    if (KdbpBreakPointSteppedOver(BreakPointNr))
        return;

    BreakPoint = KdbpBreakPointFromNr(BreakPointNr);
    if ((BreakPoint != NULL) && BreakPoint->Enabled)
        KdbpApplyBreakPoint(BreakPoint, TRUE);
}

/* The thread's step past a breakpoint is done: the breakpoint goes back once no other step needs it out */
static
VOID
KdbpEndStepOver(
    _In_opt_ PKTHREAD Thread)
{
    LONG Index = KdbpFindStepOver(Thread);

    if (Index >= 0)
        KdbpEndStepOverAt(Index);
}

/*
 * Takes a breakpoint's instruction out of memory so the current thread can run
 * it, and steps the thread over it. Other threads that reach the address in the
 * meantime run the instruction as written.
 */
static
VOID
KdbpBeginStepOver(
    _In_ LONG BreakPointNr,
    _Inout_ PCONTEXT Context)
{
    PKDB_BREAKPOINT BreakPoint = KdbpBreakPointFromNr(BreakPointNr);
    PKTHREAD Thread = KeGetCurrentThread();
    LONG Index;

    ASSERT(BreakPoint != NULL);
    KdbpEndStepOver(Thread);

    for (Index = 0; Index < (LONG)RTL_NUMBER_OF(KdbpStepOvers); Index++)
    {
        if (!KdbpStepOvers[Index].InUse)
            break;
    }

    /* More steps in flight than processors: the breakpoint stays and fires again */
    if (Index == (LONG)RTL_NUMBER_OF(KdbpStepOvers))
        return;

    if (!KdbpBreakPointSteppedOver(BreakPointNr))
        KdbpApplyBreakPoint(BreakPoint, FALSE);

    KdbpStepOvers[Index].InUse = TRUE;
    KdbpStepOvers[Index].BreakPointNr = BreakPointNr;

    /* A step that cannot be taken leaves the breakpoint out until the machine next stops */
    KdbpStepOvers[Index].Thread = NT_SUCCESS(KdbpArchSetSingleStep(Context, TRUE)) ? Thread : NULL;
}

/* Every step past a breakpoint is given up, and every breakpoint goes back */
static
VOID
KdbpEndAllStepOvers(
    _In_ BOOLEAN OnlyWithoutStep)
{
    LONG Index;

    for (Index = 0; Index < (LONG)RTL_NUMBER_OF(KdbpStepOvers); Index++)
    {
        if (KdbpStepOvers[Index].InUse && (!OnlyWithoutStep || (KdbpStepOvers[Index].Thread == NULL)))
            KdbpEndStepOverAt(Index);
    }
}

/* PUBLIC FUNCTIONS ***********************************************************/

/*
 * Reads from the machine without trusting the address. Used for everything the
 * debugger looks at, because the whole point of looking is not knowing.
 */
/* The process looked at with proc or thread, when it is not the one whose address space the machine stopped in */
static
PEPROCESS
KdbpMemoryProcess(VOID)
{
    if ((KdbCurrentProcess == NULL) || (KdbCurrentProcess == PsGetCurrentProcess()))
        return NULL;

    return KdbCurrentProcess;
}

NTSTATUS
KdbpSafeReadMemory(
    _Out_ PVOID Dest,
    _In_ PVOID Src,
    _In_ ULONG Bytes)
{
    PUCHAR Destination = Dest;
    PUCHAR Source = Src;

    /*
     * The copy below walks the page tables and maps the frame for every eight
     * bytes. System memory wider than one access is copied a page at a time
     * instead, once the page is known to be there; one access of a device
     * register still goes through the copy at its own width.
     */
    while ((Bytes > sizeof(ULONG64)) &&
           KdbpIsSystemAddress((ULONG_PTR)Source) &&
           ((ULONG_PTR)Source + Bytes > (ULONG_PTR)Source) &&
           MmIsAddressValid(Source))
    {
        ULONG Piece = min(Bytes, PAGE_SIZE - (ULONG)BYTE_OFFSET(Source));

        RtlCopyMemory(Destination, Source, Piece);
        Destination += Piece;
        Source += Piece;
        Bytes -= Piece;
    }

    if (Bytes == 0)
        return STATUS_SUCCESS;

    return KdpCopyProcessMemoryChunks(KdbpMemoryProcess(),
                                      (ULONG64)(ULONG_PTR)Source,
                                      Destination,
                                      Bytes,
                                      0,
                                      MMDBG_COPY_UNSAFE,
                                      NULL);
}

NTSTATUS
KdbpSafeWriteMemory(
    _Out_ PVOID Dest,
    _In_ PVOID Src,
    _In_ ULONG Bytes)
{
    return KdpCopyProcessMemoryChunks(KdbpMemoryProcess(),
                                      (ULONG64)(ULONG_PTR)Dest,
                                      Src,
                                      Bytes,
                                      0,
                                      MMDBG_COPY_UNSAFE | MMDBG_COPY_WRITE,
                                      NULL);
}

/*
 * The next breakpoint in use at or after Start, so that a caller can list them
 * without knowing how they are kept. Returns -1 when there are no more.
 */
LONG
KdbpGetNextBreakPointNr(
    _In_ ULONG Start)
{
    LONG Index;

    for (Index = (LONG)Start; Index < KDB_MAXIMUM_BREAKPOINT_COUNT; Index++)
    {
        if (KdbpBreakPoints[Index].Type != KdbBreakPointNone)
            return Index;
    }

    return -1;
}

BOOLEAN
KdbpGetBreakPointInfo(
    _In_ ULONG BreakPointNr,
    _Out_opt_ ULONG_PTR *Address,
    _Out_opt_ KDB_BREAKPOINT_TYPE *Type,
    _Out_opt_ UCHAR *Size,
    _Out_opt_ KDB_ACCESS_TYPE *AccessType,
    _Out_opt_ UCHAR *DebugReg,
    _Out_opt_ BOOLEAN *Enabled,
    _Out_opt_ BOOLEAN *Global,
    _Out_opt_ PEPROCESS *Process,
    _Out_opt_ PCHAR *ConditionExpression)
{
    PKDB_BREAKPOINT BreakPoint = KdbpBreakPointFromNr((LONG)BreakPointNr);

    if (BreakPoint == NULL)
        return FALSE;

    if (Address != NULL)
        *Address = BreakPoint->Address;
    if (Type != NULL)
        *Type = BreakPoint->Type;
    if (Enabled != NULL)
        *Enabled = BreakPoint->Enabled;
    if (Global != NULL)
        *Global = BreakPoint->Global;
    if (Process != NULL)
        *Process = BreakPoint->Process;
    if (ConditionExpression != NULL)
        *ConditionExpression = BreakPoint->ConditionExpression;

    if (BreakPoint->Type == KdbBreakPointHardware)
    {
        if (Size != NULL)
            *Size = BreakPoint->Data.Hw.Size;
        if (AccessType != NULL)
            *AccessType = BreakPoint->Data.Hw.AccessType;
        if (DebugReg != NULL)
            *DebugReg = BreakPoint->Data.Hw.DebugReg;
    }
    else
    {
        if (Size != NULL)
            *Size = sizeof(KD_BREAKPOINT_TYPE);
        if (AccessType != NULL)
            *AccessType = KdbAccessExec;
        if (DebugReg != NULL)
            *DebugReg = 0;
    }

    return TRUE;
}

/*
 * Takes a new breakpoint. A condition may be given, in which case the machine
 * only stops when it comes out true where the breakpoint is.
 */
NTSTATUS
KdbpInsertBreakPoint(
    _In_ ULONG_PTR Address,
    _In_ KDB_BREAKPOINT_TYPE Type,
    _In_ UCHAR Size,
    _In_ KDB_ACCESS_TYPE AccessType,
    _In_opt_ PCHAR ConditionExpression,
    _In_ BOOLEAN Global,
    _Out_opt_ PLONG BreakPointNr)
{
    PKDB_BREAKPOINT BreakPoint = NULL;
    LONG Index;
    LONG Register = -1;
    NTSTATUS Status;

    KdbpInitializeEnterConditions();

    if (BreakPointNr != NULL)
        *BreakPointNr = -1;

    if ((Type != KdbBreakPointSoftware) &&
        (Type != KdbBreakPointHardware) &&
        (Type != KdbBreakPointTemporary))
    {
        return STATUS_INVALID_PARAMETER;
    }

    /* An instruction starts at a multiple of the breakpoint's size: an address marked as Thumb code names the one below it */
    if (Type != KdbBreakPointHardware)
        Address &= ~(ULONG_PTR)(KD_BREAKPOINT_SIZE - 1);

    /* The same address is not watched twice */
    if (KdbpFindBreakPointAt(Address) >= 0)
        return STATUS_OBJECT_NAME_COLLISION;

    for (Index = 0; Index < KDB_MAXIMUM_BREAKPOINT_COUNT; Index++)
    {
        if (KdbpBreakPoints[Index].Type == KdbBreakPointNone)
        {
            BreakPoint = &KdbpBreakPoints[Index];
            break;
        }
    }

    if (BreakPoint == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;

    if (Type == KdbBreakPointHardware)
    {
        ULONG Available = KdbpArchHardwareBreakPointCount();
        ULONG Slot;

        if (Available == 0)
            return STATUS_NOT_SUPPORTED;

        if (Available > RTL_NUMBER_OF(KdbpHardwareOwner))
            Available = RTL_NUMBER_OF(KdbpHardwareOwner);

        for (Slot = 0; Slot < Available; Slot++)
        {
            if (KdbpHardwareOwner[Slot] < 0)
            {
                Register = (LONG)Slot;
                break;
            }
        }

        if (Register < 0)
            return STATUS_INSUFFICIENT_RESOURCES;

        if ((Size != 1) && (Size != 2) && (Size != 4) && (Size != 8))
            return STATUS_INVALID_PARAMETER;
    }

    RtlZeroMemory(BreakPoint, sizeof(*BreakPoint));

    BreakPoint->Type = Type;
    BreakPoint->Address = Address;
    BreakPoint->Global = Global || (KdbCurrentProcess == NULL);
    BreakPoint->Process = BreakPoint->Global ? NULL : KdbCurrentProcess;
    BreakPoint->Enabled = TRUE;

    if (Type == KdbBreakPointHardware)
    {
        BreakPoint->Data.Hw.DebugReg = (UCHAR)Register;
        BreakPoint->Data.Hw.Size = Size;
        BreakPoint->Data.Hw.AccessType = AccessType;
    }

    Index = KdbpBreakPointNr(BreakPoint);

    /* A condition is kept as the text that was typed, checked for sense now */
    if ((ConditionExpression != NULL) && (*ConditionExpression != ANSI_NULL))
    {
        CHAR ErrorMessage[128];
        LONG ErrorOffset = 0;
        SIZE_T Length = strlen(ConditionExpression);

        if (Length >= KDB_CONDITION_LENGTH)
        {
            BreakPoint->Type = KdbBreakPointNone;
            return STATUS_BUFFER_OVERFLOW;
        }

        if (KdbpRpnParseExpression(ConditionExpression,
                                   &ErrorOffset,
                                   ErrorMessage) == NULL)
        {
            BreakPoint->Type = KdbBreakPointNone;
            return STATUS_INVALID_PARAMETER;
        }

        RtlCopyMemory(KdbpConditionText[Index], ConditionExpression, Length + 1);
        BreakPoint->ConditionExpression = KdbpConditionText[Index];
        BreakPoint->Condition = KdbpConditionText[Index];
    }

    Status = KdbpApplyBreakPoint(BreakPoint, TRUE);
    if (!NT_SUCCESS(Status))
    {
        BreakPoint->Type = KdbBreakPointNone;
        BreakPoint->ConditionExpression = NULL;
        BreakPoint->Condition = NULL;
        return Status;
    }

    if (Register >= 0)
        KdbpHardwareOwner[Register] = Index;

    /* The process has to outlive the breakpoint that writes into it */
    if (BreakPoint->Process != NULL)
        ObReferenceObject(BreakPoint->Process);

    if (BreakPointNr != NULL)
        *BreakPointNr = Index;

    return STATUS_SUCCESS;
}

/*
 * Gives up a breakpoint. Either the number or the breakpoint itself may be
 * given, since a caller walking the list has one and a caller at the prompt
 * has the other.
 */
BOOLEAN
KdbpDeleteBreakPoint(
    _In_ LONG BreakPointNr,
    _Inout_opt_ PKDB_BREAKPOINT BreakPoint)
{
    ULONG Index;

    if (BreakPoint == NULL)
        BreakPoint = KdbpBreakPointFromNr(BreakPointNr);
    else
        BreakPointNr = KdbpBreakPointNr(BreakPoint);

    if ((BreakPoint == NULL) || (BreakPointNr < 0))
        return FALSE;

    if (BreakPoint->Enabled && !KdbpBreakPointSteppedOver(BreakPointNr))
        KdbpApplyBreakPoint(BreakPoint, FALSE);

    /* A step in flight past it still ends silently, with nothing to write back */
    for (Index = 0; Index < RTL_NUMBER_OF(KdbpStepOvers); Index++)
    {
        if (KdbpStepOvers[Index].InUse && (KdbpStepOvers[Index].BreakPointNr == BreakPointNr))
            KdbpStepOvers[Index].BreakPointNr = -1;
    }

    if (BreakPoint->Type == KdbBreakPointHardware)
    {
        UCHAR Register = BreakPoint->Data.Hw.DebugReg;

        if (Register < RTL_NUMBER_OF(KdbpHardwareOwner))
            KdbpHardwareOwner[Register] = -1;
    }

    /* The machine is stopped: the last reference may only be dropped by a deferred delete */
    if (BreakPoint->Process != NULL)
        ObDereferenceObjectDeferDelete(BreakPoint->Process);

    RtlZeroMemory(BreakPoint, sizeof(*BreakPoint));
    BreakPoint->Type = KdbBreakPointNone;

    if (KdbLastBreakPointNr == BreakPointNr)
        KdbLastBreakPointNr = -1;

    return TRUE;
}

/*
 * Marks a breakpoint as one a step or a run to an address set for itself. It
 * goes at the next stop, whatever the machine stops for.
 */
BOOLEAN
KdbpSetBreakPointInternal(
    _In_ LONG BreakPointNr)
{
    PKDB_BREAKPOINT BreakPoint = KdbpBreakPointFromNr(BreakPointNr);

    if (BreakPoint == NULL)
        return FALSE;

    BreakPoint->Internal = TRUE;
    return TRUE;
}

/* Whether the machine stands on the instruction of the breakpoint that has this number now: the number may have gone to another one since the stop */
BOOLEAN
KdbpStandsOnBreakPoint(
    _In_ LONG BreakPointNr,
    _In_ PCONTEXT Context)
{
    PKDB_BREAKPOINT BreakPoint = KdbpBreakPointFromNr(BreakPointNr);

    if ((BreakPoint == NULL) || !BreakPoint->Enabled)
        return FALSE;

    if ((BreakPoint->Type == KdbBreakPointHardware) && (BreakPoint->Data.Hw.AccessType != KdbAccessExec))
        return FALSE;

    return BreakPoint->Address == (ULONG_PTR)KeGetContextPc(Context);
}

/* A breakpoint on such an address belongs to every process; any other one to the process it was set in */
BOOLEAN
KdbpIsSystemAddress(
    _In_ ULONG_PTR Address)
{
    return (Address >= (ULONG_PTR)MmSystemRangeStart);
}

static
VOID
KdbpDeleteInternalBreakPoints(VOID)
{
    LONG Index;

    for (Index = 0; Index < KDB_MAXIMUM_BREAKPOINT_COUNT; Index++)
    {
        if ((KdbpBreakPoints[Index].Type != KdbBreakPointNone) && KdbpBreakPoints[Index].Internal)
            KdbpDeleteBreakPoint(Index, NULL);
    }
}

BOOLEAN
KdbpSetBreakPointCommands(
    _In_ LONG BreakPointNr,
    _In_opt_ PCSTR Commands)
{
    PKDB_BREAKPOINT BreakPoint = KdbpBreakPointFromNr(BreakPointNr);
    SIZE_T Length;

    if (BreakPoint == NULL)
        return FALSE;

    if ((Commands == NULL) || (*Commands == ANSI_NULL))
    {
        BreakPoint->Commands = NULL;
        return TRUE;
    }

    Length = strlen(Commands);
    if (Length >= KDB_BREAKPOINT_COMMANDS_MAX)
        return FALSE;

    RtlCopyMemory(KdbpCommandText[BreakPointNr], Commands, Length + 1);
    BreakPoint->Commands = KdbpCommandText[BreakPointNr];
    return TRUE;
}

BOOLEAN
KdbpSetBreakPointIgnoreCount(
    _In_ LONG BreakPointNr,
    _In_ ULONG Count)
{
    PKDB_BREAKPOINT BreakPoint = KdbpBreakPointFromNr(BreakPointNr);

    if (BreakPoint == NULL)
        return FALSE;

    BreakPoint->IgnoreCount = Count;
    return TRUE;
}

BOOLEAN
KdbpGetBreakPointCounts(
    _In_ LONG BreakPointNr,
    _Out_ PULONG HitCount,
    _Out_ PULONG IgnoreCount,
    _Out_ PCSTR *Commands)
{
    PKDB_BREAKPOINT BreakPoint = KdbpBreakPointFromNr(BreakPointNr);

    if (BreakPoint == NULL)
        return FALSE;

    *HitCount = BreakPoint->HitCount;
    *IgnoreCount = BreakPoint->IgnoreCount;
    *Commands = BreakPoint->Commands;
    return TRUE;
}

BOOLEAN
KdbpTakeStopCommands(
    _Out_writes_(Length) PCHAR Buffer,
    _In_ ULONG Length)
{
    if ((KdbpStopCommands[0] == ANSI_NULL) || (Length == 0))
        return FALSE;

    RtlStringCbCopyA(Buffer, Length, KdbpStopCommands);
    KdbpStopCommands[0] = ANSI_NULL;
    return TRUE;
}

/*
 * Whether a breakpoint that was reached stops the machine this time: it has
 * to be on, its condition has to hold, and the hits it was told to let pass
 * have to be used up. Every hit with the condition holding is counted.
 */
static
BOOLEAN
KdbpBreakPointStops(
    _Inout_ PKDB_BREAKPOINT BreakPoint)
{
    if (!BreakPoint->Enabled || !KdbpCheckBreakPointCondition(BreakPoint))
        return FALSE;

    BreakPoint->HitCount++;

    if (BreakPoint->IgnoreCount != 0)
    {
        BreakPoint->IgnoreCount--;
        return FALSE;
    }

    return TRUE;
}

/* The machine stops at this breakpoint: what the prompt needs of it is kept before a one-shot one goes */
static
VOID
KdbpTakeBreakPointStop(
    _In_ LONG BreakPointNr,
    _Out_ PBOOLEAN Internal)
{
    PKDB_BREAKPOINT BreakPoint = &KdbpBreakPoints[BreakPointNr];

    *Internal = BreakPoint->Internal;

    if (BreakPoint->Commands != NULL)
        RtlStringCbCopyA(KdbpStopCommands, sizeof(KdbpStopCommands), BreakPoint->Commands);

    if (BreakPoint->Type == KdbBreakPointTemporary)
        KdbpDeleteBreakPoint(BreakPointNr, NULL);

    /* After the deletion, which forgets the number of a breakpoint that is gone */
    KdbLastBreakPointNr = BreakPointNr;
}

BOOLEAN
KdbpEnableBreakPoint(
    _In_ LONG BreakPointNr,
    _Inout_opt_ PKDB_BREAKPOINT BreakPoint)
{
    if (BreakPoint == NULL)
        BreakPoint = KdbpBreakPointFromNr(BreakPointNr);

    if (BreakPoint == NULL)
        return FALSE;

    if (BreakPoint->Enabled)
        return TRUE;

    /* Out of memory for a step in flight: it is written when that step is done */
    if (!KdbpBreakPointSteppedOver(KdbpBreakPointNr(BreakPoint)) &&
        !NT_SUCCESS(KdbpApplyBreakPoint(BreakPoint, TRUE)))
    {
        return FALSE;
    }

    BreakPoint->Enabled = TRUE;
    return TRUE;
}

BOOLEAN
KdbpDisableBreakPoint(
    _In_ LONG BreakPointNr,
    _Inout_opt_ PKDB_BREAKPOINT BreakPoint)
{
    if (BreakPoint == NULL)
        BreakPoint = KdbpBreakPointFromNr(BreakPointNr);

    if (BreakPoint == NULL)
        return FALSE;

    if (!BreakPoint->Enabled)
        return TRUE;

    if (!KdbpBreakPointSteppedOver(KdbpBreakPointNr(BreakPoint)) &&
        !NT_SUCCESS(KdbpApplyBreakPoint(BreakPoint, FALSE)))
    {
        return FALSE;
    }

    BreakPoint->Enabled = FALSE;
    return TRUE;
}

/*
 * Whether a breakpoint's condition holds. A breakpoint with no condition
 * always holds; one whose condition cannot be worked out stops the machine,
 * because silently running past it would be worse.
 */
BOOLEAN
KdbpCheckBreakPointCondition(
    _In_ PKDB_BREAKPOINT BreakPoint)
{
    ULONGLONG Result = 0;

    if (BreakPoint->Condition == NULL)
        return TRUE;

    if (!KdbpRpnEvaluateParsedExpression(BreakPoint->Condition,
                                         KdbCurrentTrapFrame,
                                         &Result,
                                         NULL,
                                         NULL))
    {
        return TRUE;
    }

    return (Result != 0);
}

BOOLEAN
KdbpGetEnterCondition(
    _In_ LONG ExceptionNr,
    _In_ BOOLEAN FirstChance,
    _Out_ KDB_ENTER_CONDITION *Condition)
{
    KdbpInitializeEnterConditions();

    if ((ExceptionNr < 0) || (ExceptionNr > KDB_MAXIMUM_EXCEPTION_NR))
        return FALSE;

    *Condition = KdbpEnterCondition[ExceptionNr][FirstChance ? 0 : 1];
    return TRUE;
}

BOOLEAN
KdbpSetEnterCondition(
    _In_ LONG ExceptionNr,
    _In_ BOOLEAN FirstChance,
    _In_ KDB_ENTER_CONDITION Condition)
{
    LONG Index;

    KdbpInitializeEnterConditions();

    if (ExceptionNr < 0)
    {
        /* Everything at once, which is what a person usually means */
        for (Index = 0; Index <= KDB_MAXIMUM_EXCEPTION_NR; Index++)
            KdbpEnterCondition[Index][FirstChance ? 0 : 1] = Condition;

        return TRUE;
    }

    if (ExceptionNr > KDB_MAXIMUM_EXCEPTION_NR)
        return FALSE;

    KdbpEnterCondition[ExceptionNr][FirstChance ? 0 : 1] = Condition;
    return TRUE;
}

/*
 * The thread or the process with a given identifier, without trusting the
 * tables: a debugger is often looking at a system whose tables are wrong.
 */
/*
 * Walks a list the way the debugger reads anything, through guarded reads. A
 * link that cannot be read ends the walk, and so does a list longer than
 * Maximum, which is what a list that has lost its head looks like. Each entry
 * is handed over as the address of its link.
 */
KDB_WALK_RESULT
KdbpWalkList(
    _In_ PLIST_ENTRY Head,
    _In_ ULONG Maximum,
    _In_ PKDB_WALK_ROUTINE Routine,
    _In_opt_ PVOID Context,
    _Out_opt_ PULONG Count,
    _Out_opt_ PLIST_ENTRY *Broken)
{
    KDB_WALK_RESULT Result = KdbWalkComplete;
    LIST_ENTRY Link;
    PLIST_ENTRY Current;
    ULONG Visited = 0;

    if (Broken != NULL)
        *Broken = NULL;

    if (!NT_SUCCESS(KdbpSafeReadMemory(&Link, Head, sizeof(Link))))
    {
        Result = KdbWalkUnreadable;
        if (Broken != NULL)
            *Broken = Head;
    }

    for (Current = Link.Flink; (Result == KdbWalkComplete) && (Current != Head); Current = Link.Flink)
    {
        if (Visited >= Maximum)
        {
            Result = KdbWalkTooLong;
            break;
        }

        if (!NT_SUCCESS(KdbpSafeReadMemory(&Link, Current, sizeof(Link))))
        {
            Result = KdbWalkUnreadable;
            if (Broken != NULL)
                *Broken = Current;
            break;
        }

        Visited++;
        if (!Routine(Current, Context))
            Result = KdbWalkStopped;
    }

    if (Count != NULL)
        *Count = Visited;

    return Result;
}

/* Says why a walk ended early, if it did */
VOID
KdbpReportWalk(
    _In_ KDB_WALK_RESULT Result,
    _In_opt_ PLIST_ENTRY Broken,
    _In_ ULONG Maximum)
{
    if (Result == KdbWalkUnreadable)
        KdbpPrint("The list cannot be read at %p\n", Broken);
    else if (Result == KdbWalkTooLong)
        KdbpPrint("The list goes on past %lu entries without coming back to its head\n", Maximum);
}

typedef struct _KDB_FIND_CONTEXT
{
    PVOID Id;
    PEPROCESS Process;
    PETHREAD Thread;
} KDB_FIND_CONTEXT, *PKDB_FIND_CONTEXT;

static
BOOLEAN
NTAPI
KdbpFindThreadInList(
    _In_ PLIST_ENTRY Link,
    _In_opt_ PVOID Context)
{
    PKDB_FIND_CONTEXT Find = Context;
    PETHREAD Thread = CONTAINING_RECORD(Link, ETHREAD, ThreadListEntry);
    HANDLE Id = NULL;

    if (NT_SUCCESS(KdbpSafeReadMemory(&Id, &Thread->Cid.UniqueThread, sizeof(Id))) && (Id == Find->Id))
    {
        Find->Thread = Thread;
        return FALSE;
    }

    return TRUE;
}

static
BOOLEAN
NTAPI
KdbpFindThreadInProcess(
    _In_ PLIST_ENTRY Link,
    _In_opt_ PVOID Context)
{
    PEPROCESS Process = CONTAINING_RECORD(Link, EPROCESS, ActiveProcessLinks);

    KdbpWalkList(&Process->ThreadListHead, KDB_MAXIMUM_LIST_WALK, KdbpFindThreadInList, Context, NULL, NULL);
    return (((PKDB_FIND_CONTEXT)Context)->Thread == NULL);
}

/*
 * The thread or the process with a given identifier, without trusting the
 * tables: a debugger is often looking at a system whose tables are wrong.
 */
BOOLEAN
KdbpFindThreadById(
    _In_ PVOID ThreadId,
    _Out_ PETHREAD *Thread)
{
    KDB_FIND_CONTEXT Find = {ThreadId, NULL, NULL};

    KdbpWalkList(&PsActiveProcessHead, KDB_MAXIMUM_LIST_WALK, KdbpFindThreadInProcess, &Find, NULL, NULL);

    *Thread = Find.Thread;
    return (Find.Thread != NULL);
}

static
BOOLEAN
NTAPI
KdbpFindProcessInList(
    _In_ PLIST_ENTRY Link,
    _In_opt_ PVOID Context)
{
    PKDB_FIND_CONTEXT Find = Context;
    PEPROCESS Process = CONTAINING_RECORD(Link, EPROCESS, ActiveProcessLinks);
    HANDLE Id = NULL;

    if (NT_SUCCESS(KdbpSafeReadMemory(&Id, &Process->UniqueProcessId, sizeof(Id))) && (Id == Find->Id))
    {
        Find->Process = Process;
        return FALSE;
    }

    return TRUE;
}

BOOLEAN
KdbpFindProcessById(
    _In_ PVOID ProcessId,
    _Out_ PEPROCESS *Process)
{
    KDB_FIND_CONTEXT Find = {ProcessId, NULL, NULL};

    KdbpWalkList(&PsActiveProcessHead, KDB_MAXIMUM_LIST_WALK, KdbpFindProcessInList, &Find, NULL, NULL);

    *Process = Find.Process;
    return (Find.Process != NULL);
}

/*
 * Builds a context out of a thread that is not running. What can be said about
 * such a thread is where its stack was left.
 */
BOOLEAN
KdbpKdbTrapFrameFromKernelStack(
    _In_ PVOID KernelStack,
    _Out_ PKDB_KTRAP_FRAME KdbTrapFrame)
{
    if ((KernelStack == NULL) || (KdbTrapFrame == NULL))
        return FALSE;

    return KdbpArchContextFromKernelStack(KernelStack, KdbTrapFrame);
}

/*
 * Looks at another thread. The registers shown afterwards are that thread's,
 * and so is the address space the debugger reads through.
 */
BOOLEAN
KdbpAttachToThread(
    _In_ PVOID ThreadId)
{
    PETHREAD Thread = NULL;
    PEPROCESS Process = NULL;

    if (!KdbpFindThreadById(ThreadId, &Thread))
        return FALSE;

    /* The thread that stopped the machine is read from the stop itself */
    if (Thread == KdbOriginalThread)
    {
        KdbCurrentThread = KdbOriginalThread;
        KdbCurrentProcess = KdbOriginalProcess;
        return TRUE;
    }

    if (!NT_SUCCESS(KdbpSafeReadMemory(&Process, &Thread->Tcb.ApcState.Process, sizeof(Process))) ||
        (Process == NULL))
    {
        return FALSE;
    }

    KdbCurrentThread = Thread;
    KdbCurrentProcess = Process;

    return TRUE;
}

BOOLEAN
KdbpAttachToProcess(
    _In_ PVOID ProcessId)
{
    PEPROCESS Process = NULL;
    LIST_ENTRY Threads;
    HANDLE ThreadId = NULL;

    if (!KdbpFindProcessById(ProcessId, &Process))
        return FALSE;

    if (!NT_SUCCESS(KdbpSafeReadMemory(&Threads, &Process->ThreadListHead, sizeof(Threads))) ||
        (Threads.Flink == &Process->ThreadListHead) ||
        !NT_SUCCESS(KdbpSafeReadMemory(&ThreadId,
                                       &CONTAINING_RECORD(Threads.Flink, ETHREAD, ThreadListEntry)->Cid.UniqueThread,
                                       sizeof(ThreadId))))
    {
        return FALSE;
    }

    KdbCurrentProcess = Process;

    return KdbpAttachToThread(ThreadId);
}

/*
 * What the person at the prompt asked for on the kernel command line.
 */
VOID
KdbpGetCommandLineSettings(
    _In_ PCSTR p1)
{
    if (p1 == NULL)
        return;

    if (strstr(p1, "KDSERIAL") != NULL)
        KdbDebugState |= KD_DEBUG_KDSERIAL;

    if (strstr(p1, "KDNOECHO") != NULL)
        KdbDebugState |= KD_DEBUG_KDNOECHO;
}

/*
 * What to do once the prompt is left. The commands that resume the machine
 * each say which of these they meant.
 */
VOID
KdbpSetContinueType(
    _In_ KD_CONTINUE_TYPE Type)
{
    KdbpContinueType = Type;
}

static
VOID
KdbpStepPastBreakInstruction(
    _Inout_ PCONTEXT Context)
{
    ULONG_PTR Pc = (ULONG_PTR)KeGetContextPc(Context);
    KD_BREAKPOINT_TYPE Instruction;

    if (!NT_SUCCESS(KdbpSafeReadMemory(&Instruction, (PVOID)Pc, sizeof(Instruction))))
        return;

    if (Instruction == KD_BREAKPOINT_VALUE)
        KeSetContextPc(Context, Pc + KD_BREAKPOINT_SIZE);
}

static
BOOLEAN
KdbpIsStaleBreak(
    _In_opt_ PEXCEPTION_RECORD64 ExceptionRecord,
    _In_ PCONTEXT Context)
{
    ULONG_PTR Address;
    ULONG_PTR Pc = (ULONG_PTR)KeGetContextPc(Context);

    if ((ExceptionRecord == NULL) ||
        (ExceptionRecord->ExceptionCode != (NTSTATUS)STATUS_BREAKPOINT) ||
        (ExceptionRecord->NumberParameters < 1) ||
        (ExceptionRecord->ExceptionInformation[0] != BREAKPOINT_BREAK) ||
        (ExceptionRecord->ExceptionFlags & EXCEPTION_SOFTWARE_ORIGINATE))
    {
        return FALSE;
    }

    Address = (ULONG_PTR)ExceptionRecord->ExceptionAddress;
    if ((Pc != Address) && (Pc != Address + KD_BREAKPOINT_SIZE))
        return FALSE;

    return (KdbpFindBreakPointAt(Address) < 0) && !KdbpArchIsBreakInstruction(Address);
}

/*
 * Where the machine stopped, and why. Everything the debugger does happens
 * inside this call: when it returns, the machine runs again.
 */
KD_CONTINUE_TYPE
KdbEnterDebuggerException(
    _In_ PEXCEPTION_RECORD64 ExceptionRecord,
    _In_ KPROCESSOR_MODE PreviousMode,
    _Inout_ PCONTEXT Context,
    _In_ BOOLEAN FirstChance)
{
    KDB_ENTER_CONDITION EnterCondition = KdbEnterAlways;
    LONG ExceptionNr;
    LONG BreakPointNr = -1;
    EXCEPTION_RECORD64 StepRecord;
    KDB_STEP_HIT StepHit;
    BOOLEAN EnteredOnSingleStep = FALSE;
    BOOLEAN StoppedAtInternal = FALSE;
    BOOLEAN SteppedOverBreakPoint;
    BOOLEAN DisplayAcquired;

    KdbpInitializeEnterConditions();

    /* Already inside: a fault in the debugger itself is not the debugger's */
    if (KdbActive)
        return kdHandleException;

    KdbpStopCommands[0] = ANSI_NULL;
    KdbCurrentThread = NULL;
    KdbCurrentProcess = NULL;

    /* A step the processor could not take by itself ends as a breakpoint, and is reported as the step it was */
    StepHit = KdbpArchTranslateStep(ExceptionRecord, Context);
    if (StepHit == KdbStepDone)
    {
        ASSERT(ExceptionRecord != NULL);
        StepRecord = *ExceptionRecord;
        StepRecord.ExceptionCode = STATUS_SINGLE_STEP;
        ExceptionRecord = &StepRecord;
    }

    /* Code moved past the debugger's own breakpoints stops nothing, and leaves the step it ran into pending */
    if (StepHit == KdbStepForeignHit)
        return kdContinue;

    /* A step whose stepping the debugger has given up since ends here, unseen */
    if ((StepHit == KdbStepDone) && (KdbpFindStepOver(KeGetCurrentThread()) < 0) &&
        ((KdbNumSingleSteps == 0) || (KdbpStepThread != KeGetCurrentThread())))
    {
        return kdContinue;
    }

    /* A breakpoint taken out after its trap and before the trap reached the debugger: the code runs on as written */
    if (KdbpIsStaleBreak(ExceptionRecord, Context))
    {
        KeSetContextPc(Context, (ULONG_PTR)ExceptionRecord->ExceptionAddress);
        return kdContinue;
    }

    ExceptionNr = (ExceptionRecord != NULL)
                  ? KdbpExceptionNumber(ExceptionRecord->ExceptionCode) : 3;

    KdbCurrentTrapFrame = Context;
    KdbOriginalThread = PsGetCurrentThread();
    KdbOriginalProcess = PsGetCurrentProcess();
    KdbCurrentThread = KdbOriginalThread;
    KdbCurrentProcess = KdbOriginalProcess;

    if (ExceptionRecord != NULL)
    {
        KdbCurrentExceptionRecord = *ExceptionRecord;
        KdbCurrentExceptionRecordValid = TRUE;
    }
    else
    {
        KdbCurrentExceptionRecordValid = FALSE;
    }

    /* Whether this thread is in the middle of a step past a breakpoint; an entry from elsewhere leaves that step alone */
    SteppedOverBreakPoint = (KdbpFindStepOver(KeGetCurrentThread()) >= 0);

    /* Other code reached a step's breakpoint and the step was given up: it runs the instruction as written and nothing stops */
    if (StepHit == KdbStepAbandoned)
    {
        KdbpEndAllStepOvers(FALSE);
        KdbCurrentExceptionRecordValid = FALSE;
        KdbCurrentTrapFrame = NULL;
        return kdContinue;
    }

    if ((ExceptionRecord != NULL) &&
        (ExceptionRecord->ExceptionCode == (NTSTATUS)STATUS_BREAKPOINT))
    {
        /*
         * The exception is reported at the breakpoint instruction on every
         * processor, wherever each leaves the program counter.
         */
        BreakPointNr = KdbpFindBreakPointAt((ULONG_PTR)ExceptionRecord->ExceptionAddress);

        if (BreakPointNr >= 0)
        {
            PKDB_BREAKPOINT BreakPoint = &KdbpBreakPoints[BreakPointNr];

            /* Back onto the instruction the breakpoint stands in for */
            KeSetContextPc(Context, BreakPoint->Address);

            if (!KdbpBreakPointStops(BreakPoint))
            {
                /*
                 * Not this time. The instruction goes back, the thread steps
                 * over it, and the breakpoint is written again after that step.
                 */
                KdbpBeginStepOver(BreakPointNr, Context);

                KdbCurrentExceptionRecordValid = FALSE;
                KdbCurrentTrapFrame = NULL;
                return kdContinue;
            }

            KdbpTakeBreakPointStop(BreakPointNr, &StoppedAtInternal);
        }
    }

    /* A watch register that fired makes this a breakpoint, not a step */
    if ((ExceptionRecord != NULL) &&
        (ExceptionRecord->ExceptionCode == (NTSTATUS)STATUS_SINGLE_STEP))
    {
        LONG Register = KdbpArchHardwareBreakPointHit(Context);

        if ((Register >= 0) &&
            (Register < (LONG)RTL_NUMBER_OF(KdbpHardwareOwner)) &&
            (KdbpHardwareOwner[Register] >= 0))
        {
            PKDB_BREAKPOINT BreakPoint = &KdbpBreakPoints[KdbpHardwareOwner[Register]];

            if (KdbpBreakPointStops(BreakPoint))
            {
                KdbpTakeBreakPointStop(KdbpHardwareOwner[Register], &StoppedAtInternal);
                BreakPointNr = KdbLastBreakPointNr;

                /* A breakpoint ends whatever stepping was going on */
                KdbNumSingleSteps = 0;
                KdbpArchSetSingleStep(Context, FALSE);
            }
            else if (BreakPoint->Data.Hw.AccessType == KdbAccessExec)
            {
                /* An instruction breakpoint fires before its instruction: it is stepped over with the watch off */
                KdbpBeginStepOver(KdbpHardwareOwner[Register], Context);

                if (KdbNumSingleSteps == 0)
                {
                    KdbCurrentExceptionRecordValid = FALSE;
                    KdbCurrentTrapFrame = NULL;
                    return kdContinue;
                }
            }
            else if (!SteppedOverBreakPoint && (KdbNumSingleSteps == 0))
            {
                KdbCurrentExceptionRecordValid = FALSE;
                KdbCurrentTrapFrame = NULL;
                return kdContinue;
            }
        }
    }

    /* A step the debugger did not take is an exception like any other */
    if ((ExceptionRecord != NULL) &&
        (ExceptionRecord->ExceptionCode == (NTSTATUS)STATUS_SINGLE_STEP) &&
        (BreakPointNr < 0) &&
        (SteppedOverBreakPoint || ((KdbNumSingleSteps != 0) && (KdbpStepThread == KeGetCurrentThread()))))
    {
        EnteredOnSingleStep = TRUE;
        KdbpEndStepOver(KeGetCurrentThread());

        /*
         * A step the debugger took itself, to get off a breakpoint's own
         * instruction, is nothing for the person at the prompt to see.
         */
        if ((KdbNumSingleSteps == 0) || (KdbpStepThread != KeGetCurrentThread()))
        {
            KdbpArchSetSingleStep(Context, FALSE);
            KdbCurrentExceptionRecordValid = FALSE;
            KdbCurrentTrapFrame = NULL;
            return kdContinue;
        }

        if (KdbNumSingleSteps != 0)
        {
            KdbNumSingleSteps--;

            if (KdbNumSingleSteps != 0)
            {
                /* Still stepping: nothing to show yet */
                if (NT_SUCCESS(KdbpArchSetSingleStep(Context, TRUE)))
                {
                    KdbCurrentTrapFrame = NULL;
                    return kdContinue;
                }

                /* The next instruction cannot be stepped to, so the steps end here */
                KdbNumSingleSteps = 0;
            }
        }

        /* The step is done; do not step again by accident */
        KdbpArchSetSingleStep(Context, FALSE);
    }

    /* Whether this is a stop the debugger was asked to take */
    if ((ExceptionNr >= 0) && (BreakPointNr < 0) && !EnteredOnSingleStep)
    {
        KdbpGetEnterCondition(ExceptionNr, FirstChance, &EnterCondition);

        if (KdbBreakRequestThread == KeGetCurrentThread())
        {
            if (ExceptionNr == KdbpExceptionNumber(STATUS_BREAKPOINT))
                EnterCondition = KdbEnterAlways;

            KdbBreakRequestThread = NULL;
        }

        if ((EnterCondition == KdbDoNotEnter) ||
            ((EnterCondition == KdbEnterFromKmode) && (PreviousMode != KernelMode)) ||
            ((EnterCondition == KdbEnterFromUmode) && (PreviousMode == KernelMode)))
        {
            KdbCurrentExceptionRecordValid = FALSE;
            KdbCurrentTrapFrame = NULL;
            return kdHandleException;
        }
    }

    /* A step's own breakpoints mean nothing once the machine has stopped, wherever that was */
    if (StoppedAtInternal)
    {
        BreakPointNr = -1;
        KdbLastBreakPointNr = -1;
        EnteredOnSingleStep = TRUE;
    }

    KdbpDeleteInternalBreakPoints();
    KdbpEndAllStepOvers(TRUE);

    /* From here the machine belongs to whoever is at the prompt */
    KdbActive = TRUE;
    KdbpContinueType = kdHandleException;

    KbdDisableMouse();
    DisplayAcquired = KdpScreenEnterStop(TRUE);
    KdpScreenHoldOutput(TRUE);

    KdbpPagerReset();
    KdbpCliMainLoop(EnteredOnSingleStep);

    KdpScreenRelease(DisplayAcquired);
    KbdEnableMouse();

    KdbActive = FALSE;
    KdbpStepThread = (KdbNumSingleSteps != 0) ? KeGetCurrentThread() : NULL;

    if (KdbBugCheckRequested)
    {
        KdbBugCheckRequested = FALSE;
        KeBugCheckEx(MANUALLY_INITIATED_CRASH, 0, 0, 0, 0);
    }

    /*
     * A breakpoint the machine is standing on has to be taken out of memory
     * for one instruction, or it would fire again at once.
     */
    if ((BreakPointNr >= 0) && (KdbpContinueType == kdContinue))
    {
        /* A watch on data fires after the access; one on an instruction would fire again at once */
        if (KdbpStandsOnBreakPoint(BreakPointNr, Context))
            KdbpBeginStepOver(BreakPointNr, Context);
    }
    else if ((BreakPointNr < 0) &&
             (KdbpContinueType == kdContinue) &&
             (ExceptionRecord != NULL) &&
             (ExceptionRecord->ExceptionCode == (NTSTATUS)STATUS_BREAKPOINT))
    {
        /* The program counter is still on a breakpoint instruction the debugger did not write, such as DbgBreakPoint's */
        KdbpStepPastBreakInstruction(Context);
    }

    KdbCurrentExceptionRecordValid = FALSE;
    KdbCurrentTrapFrame = NULL;
    KdbCurrentThread = NULL;
    KdbCurrentProcess = NULL;

    return KdbpContinueType;
}

/* EOF */
