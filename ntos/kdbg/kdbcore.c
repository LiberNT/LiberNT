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

/* The breakpoint the machine last stopped at, or -1 */
LONG KdbLastBreakPointNr = -1;

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

/* Which of this architecture's watchpoint registers is taken by which breakpoint */
static LONG KdbpHardwareOwner[8];

/* When to stop, per exception and per chance */
static KDB_ENTER_CONDITION KdbpEnterCondition[KDB_MAXIMUM_EXCEPTION_NR + 1][2];

/* What the person at the prompt decided to do on the way out */
static KD_CONTINUE_TYPE KdbpContinueType = kdHandleException;

/* The breakpoint whose instruction has to go back before the machine runs on */
static LONG KdbpStepOverBreakPoint = -1;

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

    /* A breakpoint and a step are nobody's business but the debugger's */
    KdbpEnterCondition[1][0] = KdbEnterAlways;
    KdbpEnterCondition[3][0] = KdbEnterAlways;

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
    NTSTATUS Status;

    if (Enable)
    {
        Status = KdbpSafeReadMemory(&BreakPoint->Data.SavedInstruction,
                                    (PVOID)BreakPoint->Address,
                                    sizeof(KD_BREAKPOINT_TYPE));
        if (!NT_SUCCESS(Status))
            return Status;

        return KdbpSafeWriteMemory((PVOID)BreakPoint->Address,
                                   &Instruction,
                                   sizeof(KD_BREAKPOINT_TYPE));
    }

    return KdbpSafeWriteMemory((PVOID)BreakPoint->Address,
                               &BreakPoint->Data.SavedInstruction,
                               sizeof(KD_BREAKPOINT_TYPE));
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

/* PUBLIC FUNCTIONS ***********************************************************/

/*
 * Reads from the machine without trusting the address. Used for everything the
 * debugger looks at, because the whole point of looking is not knowing.
 */
NTSTATUS
KdbpSafeReadMemory(
    _Out_ PVOID Dest,
    _In_ PVOID Src,
    _In_ ULONG Bytes)
{
    return KdpCopyMemoryChunks((ULONG64)(ULONG_PTR)Src,
                               Dest,
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
    return KdpCopyMemoryChunks((ULONG64)(ULONG_PTR)Dest,
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
    BreakPoint->Global = Global;
    BreakPoint->Process = Global ? NULL : KdbCurrentProcess;
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
    if (BreakPoint == NULL)
        BreakPoint = KdbpBreakPointFromNr(BreakPointNr);
    else
        BreakPointNr = KdbpBreakPointNr(BreakPoint);

    if ((BreakPoint == NULL) || (BreakPointNr < 0))
        return FALSE;

    if (BreakPoint->Enabled)
        KdbpApplyBreakPoint(BreakPoint, FALSE);

    if (BreakPoint->Type == KdbBreakPointHardware)
    {
        UCHAR Register = BreakPoint->Data.Hw.DebugReg;

        if (Register < RTL_NUMBER_OF(KdbpHardwareOwner))
            KdbpHardwareOwner[Register] = -1;
    }

    RtlZeroMemory(BreakPoint, sizeof(*BreakPoint));
    BreakPoint->Type = KdbBreakPointNone;

    if (KdbLastBreakPointNr == BreakPointNr)
        KdbLastBreakPointNr = -1;

    return TRUE;
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

    if (!NT_SUCCESS(KdbpApplyBreakPoint(BreakPoint, TRUE)))
        return FALSE;

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

    if (!NT_SUCCESS(KdbpApplyBreakPoint(BreakPoint, FALSE)))
        return FALSE;

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
BOOLEAN
KdbpFindThreadById(
    _In_ PVOID ThreadId,
    _Out_ PETHREAD *Thread)
{
    PLIST_ENTRY ProcessEntry;

    *Thread = NULL;

    for (ProcessEntry = PsActiveProcessHead.Flink;
         ProcessEntry != &PsActiveProcessHead;
         ProcessEntry = ProcessEntry->Flink)
    {
        PEPROCESS Process;
        PLIST_ENTRY ThreadEntry;

        Process = CONTAINING_RECORD(ProcessEntry, EPROCESS, ActiveProcessLinks);

        for (ThreadEntry = Process->ThreadListHead.Flink;
             ThreadEntry != &Process->ThreadListHead;
             ThreadEntry = ThreadEntry->Flink)
        {
            PETHREAD Candidate;

            Candidate = CONTAINING_RECORD(ThreadEntry, ETHREAD, ThreadListEntry);

            if (Candidate->Cid.UniqueThread == ThreadId)
            {
                *Thread = Candidate;
                return TRUE;
            }
        }
    }

    return FALSE;
}

BOOLEAN
KdbpFindProcessById(
    _In_ PVOID ProcessId,
    _Out_ PEPROCESS *Process)
{
    PLIST_ENTRY Entry;

    *Process = NULL;

    for (Entry = PsActiveProcessHead.Flink;
         Entry != &PsActiveProcessHead;
         Entry = Entry->Flink)
    {
        PEPROCESS Candidate;

        Candidate = CONTAINING_RECORD(Entry, EPROCESS, ActiveProcessLinks);

        if (Candidate->UniqueProcessId == ProcessId)
        {
            *Process = Candidate;
            return TRUE;
        }
    }

    return FALSE;
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

    RtlZeroMemory(KdbTrapFrame, sizeof(*KdbTrapFrame));

    /* TODO: only the stack pointer is recovered; the saved registers of a
     * switched-out thread need this architecture's own switch frame */
    KdbpSetRegisterValue(KdbTrapFrame, "sp", (ULONGLONG)(ULONG_PTR)KernelStack);

    return TRUE;
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

    if (!KdbpFindThreadById(ThreadId, &Thread))
        return FALSE;

    /* The thread that stopped the machine is read from the stop itself */
    if (Thread == KdbOriginalThread)
    {
        KdbCurrentThread = KdbOriginalThread;
        KdbCurrentProcess = KdbOriginalProcess;
        return TRUE;
    }

    if (Thread->Tcb.State == Running)
    {
        /* TODO: a thread running on another processor is not followed */
        return FALSE;
    }

    KdbCurrentThread = Thread;
    KdbCurrentProcess = (PEPROCESS)Thread->Tcb.ApcState.Process;

    return TRUE;
}

BOOLEAN
KdbpAttachToProcess(
    _In_ PVOID ProcessId)
{
    PEPROCESS Process = NULL;
    PLIST_ENTRY Entry;

    if (!KdbpFindProcessById(ProcessId, &Process))
        return FALSE;

    Entry = Process->ThreadListHead.Flink;
    if (Entry == &Process->ThreadListHead)
        return FALSE;

    KdbCurrentProcess = Process;

    return KdbpAttachToThread(
        CONTAINING_RECORD(Entry, ETHREAD, ThreadListEntry)->Cid.UniqueThread);
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
    BOOLEAN EnteredOnSingleStep = FALSE;
    BOOLEAN SteppedOverBreakPoint;
    BOOLEAN DisplayAcquired;

    KdbpInitializeEnterConditions();

    /* Already inside: a fault in the debugger itself is not the debugger's */
    if (KdbActive)
        return kdHandleException;

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

    /*
     * The instruction a breakpoint displaced has to be back in memory before
     * anything reads the code around the stop.
     */
    SteppedOverBreakPoint = (KdbpStepOverBreakPoint >= 0);

    if (SteppedOverBreakPoint)
    {
        PKDB_BREAKPOINT Pending = KdbpBreakPointFromNr(KdbpStepOverBreakPoint);

        if ((Pending != NULL) && Pending->Enabled)
            KdbpApplyBreakPoint(Pending, TRUE);

        KdbpStepOverBreakPoint = -1;
    }

    if ((ExceptionRecord != NULL) &&
        (ExceptionRecord->ExceptionCode == (NTSTATUS)STATUS_BREAKPOINT))
    {
        ULONG_PTR Pc = (ULONG_PTR)KeGetContextPc(Context);

        /*
         * The program counter is past the breakpoint instruction by the time
         * the fault is reported, so the breakpoint is looked for behind it.
         */
        BreakPointNr = KdbpFindBreakPointAt(Pc - KD_BREAKPOINT_SIZE);
        if (BreakPointNr < 0)
            BreakPointNr = KdbpFindBreakPointAt(Pc);

        if (BreakPointNr >= 0)
        {
            PKDB_BREAKPOINT BreakPoint = &KdbpBreakPoints[BreakPointNr];

            /* Back onto the instruction the breakpoint stands in for */
            KeSetContextPc(Context, BreakPoint->Address);

            if (!BreakPoint->Enabled || !KdbpCheckBreakPointCondition(BreakPoint))
            {
                /*
                 * Not this time. The instruction goes back, the machine steps
                 * over it, and the breakpoint is written again at the next stop.
                 */
                KdbpApplyBreakPoint(BreakPoint, FALSE);
                KdbpStepOverBreakPoint = BreakPointNr;
                KdbpArchSetSingleStep(Context, TRUE);

                KdbCurrentExceptionRecordValid = FALSE;
                KdbCurrentTrapFrame = NULL;
                return kdContinue;
            }

            KdbLastBreakPointNr = BreakPointNr;

            if (BreakPoint->Type == KdbBreakPointTemporary)
                KdbpDeleteBreakPoint(BreakPointNr, NULL);
        }
    }

    if ((ExceptionRecord != NULL) &&
        (ExceptionRecord->ExceptionCode == (NTSTATUS)STATUS_SINGLE_STEP))
    {
        EnteredOnSingleStep = TRUE;

        /*
         * A step the debugger took itself, to get off a breakpoint's own
         * instruction, is nothing for the person at the prompt to see.
         */
        if (SteppedOverBreakPoint && (KdbNumSingleSteps == 0))
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
                KdbpArchSetSingleStep(Context, TRUE);
                KdbCurrentTrapFrame = NULL;
                return kdContinue;
            }
        }

        /* The step is done; do not step again by accident */
        KdbpArchSetSingleStep(Context, FALSE);
    }

    /* Whether this is a stop the debugger was asked to take */
    if ((ExceptionNr >= 0) && (BreakPointNr < 0) && !EnteredOnSingleStep)
    {
        KdbpGetEnterCondition(ExceptionNr, FirstChance, &EnterCondition);

        if ((EnterCondition == KdbDoNotEnter) ||
            ((EnterCondition == KdbEnterFromKmode) && (PreviousMode != KernelMode)) ||
            ((EnterCondition == KdbEnterFromUmode) && (PreviousMode == KernelMode)))
        {
            KdbCurrentExceptionRecordValid = FALSE;
            KdbCurrentTrapFrame = NULL;
            return kdHandleException;
        }
    }

    /* From here the machine belongs to whoever is at the prompt */
    KdbActive = TRUE;
    KdbpContinueType = kdHandleException;

    KbdDisableMouse();
    DisplayAcquired = KdpScreenEnterStop(TRUE);

    KdbpPagerReset();
    KdbpCliMainLoop(EnteredOnSingleStep);

    KdpScreenRelease(DisplayAcquired);
    KbdEnableMouse();

    KdbActive = FALSE;

    /*
     * A breakpoint the machine is standing on has to be taken out of memory
     * for one instruction, or it would fire again at once.
     */
    if ((BreakPointNr >= 0) && (KdbpContinueType == kdContinue))
    {
        PKDB_BREAKPOINT BreakPoint = KdbpBreakPointFromNr(BreakPointNr);

        if ((BreakPoint != NULL) && BreakPoint->Enabled &&
            (BreakPoint->Type != KdbBreakPointHardware))
        {
            KdbpApplyBreakPoint(BreakPoint, FALSE);
            KdbpStepOverBreakPoint = BreakPointNr;
            KdbpArchSetSingleStep(Context, TRUE);
        }
    }

    KdbCurrentExceptionRecordValid = FALSE;
    KdbCurrentTrapFrame = NULL;
    KdbCurrentThread = NULL;
    KdbCurrentProcess = NULL;

    return KdbpContinueType;
}

/* EOF */
