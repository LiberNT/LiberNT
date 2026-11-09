/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Stopping the system on purpose, and saying why before it stops
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#include <internal/dump.h>
#include "keprivate.h"
#define NDEBUG
#include <debug.h>

/* GLOBALS ********************************************************************/

/* What the system stopped for: the code and its four parameters, in the order
   a debugger and a dump reader expect to find them. */
ULONG_PTR KiBugCheckData[5];

/* Set once a bug check has begun, so that code reached from inside it can tell
   that the usual rules no longer hold. */
ULONG KeBugCheckActive;

/* How many bug checks this boot has reached. Starts at one, which is what a
   dump header records for the first. */
ULONG KeBugCheckCount = 1;

/* The thread that is running the bug check, and how deep it has re-entered.
   A bug check inside a bug check is the bug check path's own fault, and the
   second one must not be allowed to go round again. */
PKTHREAD KeBugCheckOwner;
ULONG KeBugCheckOwnerRecursionCount;

/* Whether the stop came from the hardware rather than from software. */
ULONG KiHardwareTrigger;

/* The message table in the kernel's own resources, which is where the text for
   each stop code comes from. */
PMESSAGE_RESOURCE_DATA KiBugCodeMessages;

/* Callbacks registered to save state when the system stops. */
LIST_ENTRY KeBugcheckCallbackListHead;
LIST_ENTRY KeBugcheckReasonCallbackListHead;
KSPIN_LOCK BugCheckCallbackLock;

/* Callbacks registered for the unmaskable interrupt. */
PKNMI_HANDLER_CALLBACK KiNmiCallbackListHead;
KSPIN_LOCK KiNmiCallbackListLock;

/* PRIVATE DATA ***************************************************************/

/* Where the kernel's own image is, which is where its message table is
   searched from. Recorded once, before anything can have corrupted the list
   the kernel would otherwise have to walk to find it again. */
static PVOID KiBugCheckImageBase;

/* PRIVATE DEFINITIONS ********************************************************/

/* How many frames of the stopping thread's stack are worth printing. Beyond
   this the trace says more about the depth of the kernel than about the fault. */
#define KI_BUGCHECK_FRAMES 32

/* The resource type the message table is stored under. RT_MESSAGETABLE is a
   resource-name pointer rather than a number, and both callers below want the
   number. */
#define KI_RT_MESSAGETABLE 11

/* The colour of the stop screen. */
#define KI_BUGCHECK_BACKGROUND 4

/* PRIVATE FUNCTIONS **********************************************************/

/*
 * Whether an address falls inside a loaded image.
 */
FORCEINLINE
BOOLEAN
KiAddressInImage(
    _In_ ULONG_PTR Address,
    _In_ PLDR_DATA_TABLE_ENTRY Entry)
{
    ULONG_PTR Base = (ULONG_PTR)Entry->DllBase;

    return (Address >= Base) && (Address < (Base + Entry->SizeOfImage));
}

/*
 * Writes one line to everywhere a stop message has to reach: the debugger, the
 * log the boot loader keeps, and the screen. Each of the three may be absent,
 * and none of them may be allowed to stop the other two.
 */
VOID
KiDisplayAndLogBugCheckString(
    _In_z_ PCSTR String)
{
    DbgPrint("%s", String);

    if (InbvIsBootDriverInstalled())
        InbvDisplayString((PCHAR)String);
}

/*
 * Takes the message for one stop code out of the kernel's own message table.
 * Answers an empty string where the table is missing or has no entry: a stop
 * screen without its explanation is still worth showing.
 */
VOID
NTAPI
KeGetBugMessageText(
    _In_ ULONG BugCheckCode,
    _Out_opt_ PANSI_STRING OutputString)
{
    PMESSAGE_RESOURCE_ENTRY Entry;
    NTSTATUS Status;
    PCHAR Text;
    ULONG Length;

    if (OutputString != NULL)
    {
        OutputString->Buffer = NULL;
        OutputString->Length = 0;
        OutputString->MaximumLength = 0;
    }

    if ((KiBugCheckImageBase == NULL) || (KiBugCodeMessages == NULL))
        return;

    Status = RtlFindMessage(KiBugCheckImageBase,
                            KI_RT_MESSAGETABLE,
                            LANG_NEUTRAL,
                            BugCheckCode,
                            &Entry);
    if (!NT_SUCCESS(Status))
        return;

    /* The table holds either ANSI or wide text; only the ANSI form is used */
    if (Entry->Flags & MESSAGE_RESOURCE_UNICODE)
        return;

    Text = (PCHAR)Entry->Text;
    Length = Entry->Length - (ULONG)FIELD_OFFSET(MESSAGE_RESOURCE_ENTRY, Text);

    /* Trailing line breaks belong to the table, not to the message */
    while ((Length > 0) &&
           ((Text[Length - 1] == '\n') || (Text[Length - 1] == '\r') ||
            (Text[Length - 1] == '\0')))
    {
        Length--;
    }

    if (OutputString != NULL)
    {
        OutputString->Buffer = Text;
        OutputString->Length = (USHORT)Length;
        OutputString->MaximumLength = (USHORT)Length;
        return;
    }

    DbgPrint("%.*s\n", Length, Text);
}

/*
 * Converts a name for the stop screen without allocating anything and without
 * taking a lock. Both would be wrong here: the pool and the locks are exactly
 * what may have brought the system down.
 */
PCHAR
NTAPI
KeBugCheckUnicodeToAnsi(
    _In_ PUNICODE_STRING Unicode,
    _Out_ PCHAR Ansi,
    _In_ ULONG Length)
{
    PCHAR Current = Ansi;
    PWCHAR Source = Unicode->Buffer;
    ULONG Count = Unicode->Length / sizeof(WCHAR);
    ULONG Index;

    if (Length == 0)
        return Ansi;

    /* One byte of the answer is the terminator, whatever else fits */
    if (Count > (Length - 1))
        Count = Length - 1;

    for (Index = 0; Index < Count; Index++)
    {
        *Current++ = (CHAR)Source[Index];
    }

    *Current = ANSI_NULL;
    return Ansi;
}

/*
 * Names the image an address belongs to. Answers its base address, or NULL
 * where the address is in no loaded image at all, which for a stop parameter
 * usually means it was never an address.
 */
PVOID
NTAPI
KiPcToFileHeader(
    _In_ PVOID Pc,
    _Out_ PLDR_DATA_TABLE_ENTRY *LdrEntry,
    _In_ BOOLEAN DriversOnly,
    _Out_ PBOOLEAN InKernel)
{
    PLDR_DATA_TABLE_ENTRY Entry;
    PLIST_ENTRY ListEntry;
    ULONG_PTR Address = (ULONG_PTR)Pc;
    BOOLEAN First = TRUE;

    *LdrEntry = NULL;
    *InKernel = FALSE;

    if (PsLoadedModuleList.Flink == NULL)
        return NULL;

    for (ListEntry = PsLoadedModuleList.Flink;
         ListEntry != &PsLoadedModuleList;
         ListEntry = ListEntry->Flink)
    {
        Entry = CONTAINING_RECORD(ListEntry,
                                  LDR_DATA_TABLE_ENTRY,
                                  InLoadOrderLinks);

        /*
         * The first two entries are the kernel and the layer below it; a
         * caller after a driver is not interested in either.
         */
        if (First)
        {
            First = FALSE;
            if (DriversOnly)
                continue;
        }

        if (!KiAddressInImage(Address, Entry))
            continue;

        *LdrEntry = Entry;
        *InKernel = (ListEntry == PsLoadedModuleList.Flink);
        return Entry->DllBase;
    }

    return NULL;
}

/*
 * The same against the modules of the process that is running, for an address
 * that turned out to be in user space.
 */
PVOID
NTAPI
KiRosPcToUserFileHeader(
    _In_ PVOID Pc,
    _Out_ PLDR_DATA_TABLE_ENTRY *LdrEntry)
{
    PLDR_DATA_TABLE_ENTRY Entry;
    PLIST_ENTRY ListHead, ListEntry;
    PPEB Peb;
    ULONG_PTR Address = (ULONG_PTR)Pc;

    *LdrEntry = NULL;

    Peb = PsGetCurrentProcess()->Peb;
    if ((Peb == NULL) || (Peb->Ldr == NULL))
        return NULL;

    /*
     * The list lives in the process's own memory and is written by code the
     * process controls, so neither its links nor its contents can be trusted
     * while the system is already stopping.
     */
    _SEH2_TRY
    {
        ListHead = &Peb->Ldr->InLoadOrderModuleList;
        for (ListEntry = ListHead->Flink;
             ListEntry != ListHead;
             ListEntry = ListEntry->Flink)
        {
            Entry = CONTAINING_RECORD(ListEntry,
                                      LDR_DATA_TABLE_ENTRY,
                                      InLoadOrderLinks);

            if (KiAddressInImage(Address, Entry))
            {
                *LdrEntry = Entry;
                _SEH2_YIELD(return Entry->DllBase);
            }
        }
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        *LdrEntry = NULL;
    }
    _SEH2_END;

    return NULL;
}

/*
 * Says which image each stop parameter that looks like an address belongs to.
 * It is often the whole answer: a stop code plus the name of the driver the
 * parameters point into tells a reader more than the code alone.
 */
VOID
NTAPI
KiDumpParameterImages(
    _In_z_ PCHAR Message,
    _In_ PULONG_PTR Parameters,
    _In_ ULONG ParameterCount,
    _In_ PKE_BUGCHECK_UNICODE_TO_ANSI Converter)
{
    PLDR_DATA_TABLE_ENTRY LdrEntry;
    BOOLEAN InKernel, Any = FALSE;
    CHAR AnsiName[32];
    ULONG Index;
    PVOID Base;

    for (Index = 0; Index < ParameterCount; Index++)
    {
        Base = KiPcToFileHeader((PVOID)Parameters[Index],
                                &LdrEntry,
                                FALSE,
                                &InKernel);
        if (Base == NULL)
            continue;

        Converter(&LdrEntry->BaseDllName, AnsiName, sizeof(AnsiName));

        if (!Any)
        {
            KiDisplayAndLogBugCheckString(Message);
            Any = TRUE;
        }

        DbgPrint("%s%s", (Index > 0) ? ", " : "", AnsiName);
    }

    if (Any)
        DbgPrint("\n");
}

/*
 * Runs the callbacks something registered against the system stopping. Each
 * one is checked against the sum of what it was registered with: a record the
 * fault has already corrupted must not be called.
 */
VOID
NTAPI
KiDoBugCheckCallbacks(VOID)
{
    PKBUGCHECK_CALLBACK_RECORD Record;
    PLIST_ENTRY ListEntry;
    ULONG_PTR Checksum;

    for (ListEntry = KeBugcheckCallbackListHead.Flink;
         ListEntry != &KeBugcheckCallbackListHead;
         ListEntry = ListEntry->Flink)
    {
        Record = CONTAINING_RECORD(ListEntry,
                                   KBUGCHECK_CALLBACK_RECORD,
                                   Entry);

        Checksum = (ULONG_PTR)Record->CallbackRoutine +
                   (ULONG_PTR)Record->Buffer +
                   (ULONG_PTR)Record->Length +
                   (ULONG_PTR)Record->Component;

        if ((Record->State != BufferInserted) ||
            (Record->Checksum != Checksum))
        {
            continue;
        }

        Record->State = BufferStarted;

        _SEH2_TRY
        {
            Record->CallbackRoutine(Record->Buffer, Record->Length);
            Record->State = BufferFinished;
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            /* The callback faulted: its buffer holds whatever it managed */
            Record->State = BufferIncomplete;
        }
        _SEH2_END;
    }
}

/*
 * Runs the callbacks registered for one particular reason. Unlike the plain
 * ones these are given the reason and their own record, so one routine can
 * serve several reasons.
 */
static
VOID
KiDoBugCheckReasonCallbacks(
    _In_ KBUGCHECK_CALLBACK_REASON Reason)
{
    PKBUGCHECK_REASON_CALLBACK_RECORD Record;
    PLIST_ENTRY ListEntry;
    ULONG_PTR Checksum;

    for (ListEntry = KeBugcheckReasonCallbackListHead.Flink;
         ListEntry != &KeBugcheckReasonCallbackListHead;
         ListEntry = ListEntry->Flink)
    {
        Record = CONTAINING_RECORD(ListEntry,
                                   KBUGCHECK_REASON_CALLBACK_RECORD,
                                   Entry);

        if (Record->Reason != Reason)
            continue;

        Checksum = (ULONG_PTR)Record->CallbackRoutine +
                   (ULONG_PTR)Record->Reason +
                   (ULONG_PTR)Record->Component;

        if ((Record->State != BufferInserted) ||
            (Record->Checksum != Checksum))
        {
            continue;
        }

        Record->State = BufferStarted;

        _SEH2_TRY
        {
            Record->CallbackRoutine(Reason, Record, NULL, 0);
            Record->State = BufferFinished;
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            Record->State = BufferIncomplete;
        }
        _SEH2_END;
    }
}

/*
 * Prints a captured return-address chain, naming the image each frame is in.
 */
VOID
NTAPI
KeRosDumpStackFrameArray(
    _In_ PULONG_PTR Frames,
    _In_ ULONG FrameCount)
{
    PLDR_DATA_TABLE_ENTRY LdrEntry;
    BOOLEAN InKernel;
    CHAR AnsiName[32];
    ULONG Index;
    PVOID Base;

    for (Index = 0; Index < FrameCount; Index++)
    {
        if (Frames[Index] == 0)
            break;

        Base = KiPcToFileHeader((PVOID)Frames[Index],
                                &LdrEntry,
                                FALSE,
                                &InKernel);

        if (Base != NULL)
        {
            KeBugCheckUnicodeToAnsi(&LdrEntry->BaseDllName,
                                    AnsiName,
                                    sizeof(AnsiName));
            DbgPrint("    %p  %s+0x%Ix\n",
                     (PVOID)Frames[Index],
                     AnsiName,
                     Frames[Index] - (ULONG_PTR)Base);
        }
        else
        {
            DbgPrint("    %p\n", (PVOID)Frames[Index]);
        }
    }
}

/*
 * Prints the call chain that led here. With no frame given the chain is walked
 * from this call, which is what a caller asking for its own trace wants.
 */
VOID
NTAPI
KeRosDumpStackFrames(
    _In_opt_ PULONG_PTR Frame,
    _In_ ULONG FrameCount)
{
    ULONG_PTR Frames[KI_BUGCHECK_FRAMES];
    ULONG Captured;

    if (FrameCount > KI_BUGCHECK_FRAMES)
        FrameCount = KI_BUGCHECK_FRAMES;

    if (Frame != NULL)
    {
        KeRosDumpStackFrameArray(Frame, FrameCount);
        return;
    }

    Captured = RtlWalkFrameChain((PVOID *)Frames, FrameCount, 0);
    KeRosDumpStackFrameArray(Frames, Captured);
}

/*
 * The older entry point for the same thing, kept because drivers reach it.
 * A frame of NULL asks for the caller's own chain.
 */
VOID
NTAPI
KeDumpStackFrames(
    _In_opt_ PULONG Frame)
{
    KeRosDumpStackFrames((PULONG_PTR)Frame, KI_BUGCHECK_FRAMES);
}

/*
 * Captures the user-mode part of the current thread's call chain. It is taken
 * under a handler because the stack it walks belongs to the process and may be
 * anything at all.
 */
ULONG
NTAPI
KeRosCaptureUserStackBackTrace(
    _In_ ULONG FramesToSkip,
    _In_ ULONG FramesToCapture,
    _Out_ PVOID *BackTrace,
    _Out_opt_ PULONG BackTraceHash)
{
    ULONG Captured = 0;

    if (BackTraceHash != NULL)
        *BackTraceHash = 0;

    if (FramesToCapture == 0)
        return 0;

    _SEH2_TRY
    {
        /* Flag one asks the walker for the user-mode part of the chain */
        Captured = RtlWalkFrameChain(BackTrace,
                                     FramesToSkip + FramesToCapture,
                                     1);
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Captured = 0;
    }
    _SEH2_END;

    if (Captured <= FramesToSkip)
        return 0;

    Captured -= FramesToSkip;
    if (FramesToSkip != 0)
    {
        RtlMoveMemory(BackTrace,
                      &BackTrace[FramesToSkip],
                      Captured * sizeof(PVOID));
    }

    if (BackTraceHash != NULL)
    {
        ULONG Hash = 0;
        ULONG Index;

        for (Index = 0; Index < Captured; Index++)
            Hash += (ULONG)(ULONG_PTR)BackTrace[Index];

        *BackTraceHash = Hash;
    }

    return Captured;
}

/*
 * Puts the stop screen up. The display is taken away from whoever had it,
 * because by now nothing else is going to give it back.
 */
VOID
NTAPI
KiDisplayBlueScreen(
    _In_ ULONG BugCheckCode,
    _In_opt_ PKTRAP_FRAME TrapFrame,
    _In_opt_ PCONTEXT Context)
{
    ANSI_STRING Message;
    CHAR Line[128];
    PLDR_DATA_TABLE_ENTRY LdrEntry;
    BOOLEAN InKernel;
    CHAR AnsiName[32];
    ULONG Index;

    /* The display belongs to the stop message from here on */
    InbvAcquireDisplayOwnership();
    InbvResetDisplay();
    InbvSolidColorFill(0, 0, 639, 479, KI_BUGCHECK_BACKGROUND);
    InbvSetTextColor(15);
    InbvInstallDisplayStringFilter(NULL);
    InbvEnableDisplayString(TRUE);
    InbvSetScrollRegion(0, 0, 639, 479);

    KiDisplayAndLogBugCheckString("\r\nThe system has been stopped to keep "
                                  "it from damaging anything further.\r\n\r\n");

    KeGetBugMessageText(BugCheckCode, &Message);
    if (Message.Length != 0)
    {
        DbgPrint("%.*s\r\n", Message.Length, Message.Buffer);
        if (InbvIsBootDriverInstalled())
        {
            /* The table's text is not terminated: copy out what fits */
            ULONG Count = min(Message.Length, sizeof(Line) - 3);
            RtlCopyMemory(Line, Message.Buffer, Count);
            Line[Count] = '\r';
            Line[Count + 1] = '\n';
            Line[Count + 2] = ANSI_NULL;
            InbvDisplayString(Line);
        }
    }

    _snprintf(Line,
              sizeof(Line),
              "\r\nStop code: 0x%08lX\r\n",
              BugCheckCode);
    KiDisplayAndLogBugCheckString(Line);

    for (Index = 0; Index < 4; Index++)
    {
        _snprintf(Line,
                  sizeof(Line),
                  "  Parameter %lu: 0x%p\r\n",
                  Index + 1,
                  (PVOID)KiBugCheckData[Index + 1]);
        KiDisplayAndLogBugCheckString(Line);
    }

    /* Which image the fault was in, where that can be worked out */
    if (Context != NULL)
    {
        PVOID Base = KiPcToFileHeader((PVOID)KeGetContextPc(Context),
                                      &LdrEntry,
                                      FALSE,
                                      &InKernel);
        if (Base != NULL)
        {
            KeBugCheckUnicodeToAnsi(&LdrEntry->BaseDllName,
                                    AnsiName,
                                    sizeof(AnsiName));
            _snprintf(Line,
                      sizeof(Line),
                      "\r\nFaulting module: %s + 0x%Ix\r\n",
                      AnsiName,
                      (ULONG_PTR)KeGetContextPc(Context) - (ULONG_PTR)Base);
            KiDisplayAndLogBugCheckString(Line);
        }
    }

    KiDumpParameterImages("\r\nParameters point into: ",
                          &KiBugCheckData[1],
                          4,
                          KeBugCheckUnicodeToAnsi);

    _snprintf(Line,
              sizeof(Line),
              "\r\nLiberNT %lu.%lu build %lu, %d processor(s)\r\n",
              SharedUserData->NtMajorVersion,
              SharedUserData->NtMinorVersion,
              NtBuildNumber & 0xFFFF,
              (int)(CCHAR)KeNumberProcessors);
    KiDisplayAndLogBugCheckString(Line);

    /* The architecture layer knows how to say what its registers held */
#ifdef _WIN64
    if (Context != NULL)
        KiDisplayBugCheckRegisterState(TrapFrame, Context);
#else
    UNREFERENCED_PARAMETER(TrapFrame);
#endif
}

/*
 * Offers the stop to the debugger. Where there is none the break faults, and
 * that fault is the answer: there is nothing to tell.
 */
VOID
NTAPI
KiBugCheckDebugBreak(
    _In_ ULONG StatusCode)
{
    if (KdPitchDebugger || KdDebuggerNotPresent)
        return;

    _SEH2_TRY
    {
        DbgBreakPointWithStatus(StatusCode);
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        NOTHING;
    }
    _SEH2_END;
}

/* PUBLIC FUNCTIONS: STOPPING **************************************************/

/*
 * Stops the system.
 *
 * The order matters and is the whole design of this routine. The parameters are
 * recorded before anything that could fault, because they are the only thing a
 * dump reader is certain to get. Then every other processor is stopped, so that
 * nothing changes underneath what follows. Then the callbacks run, then the
 * debugger is offered the stop, then the screen. The debugger comes before the
 * screen because a debugger that is attached is more use than a screen nobody
 * is looking at, and after the callbacks because their buffers are part of what
 * it will be shown.
 */
DECLSPEC_NORETURN
VOID
NTAPI
KeBugCheckWithTf(
    _In_ ULONG BugCheckCode,
    _In_ ULONG_PTR BugCheckParameter1,
    _In_ ULONG_PTR BugCheckParameter2,
    _In_ ULONG_PTR BugCheckParameter3,
    _In_ ULONG_PTR BugCheckParameter4,
    _In_opt_ PKTRAP_FRAME TrapFrame)
{
    PKTHREAD Thread = KeGetCurrentThread();
    PKPRCB Prcb;
    CONTEXT Context;
    PCONTEXT ContextPointer = NULL;

    /* Nothing on this processor may interrupt what follows */
    KfRaiseIrql(HIGH_LEVEL);

    /*
     * A second stop from the same thread means the stop path itself faulted.
     * Going round again would only lose the first set of parameters, which
     * are the ones worth having.
     */
    if (KeBugCheckOwner == Thread)
    {
        KeBugCheckOwnerRecursionCount++;
        if (KeBugCheckOwnerRecursionCount > 2)
        {
            KeEnterKernelDebugger();
        }
    }
    else if (InterlockedCompareExchangePointer((PVOID volatile *)&KeBugCheckOwner,
                                               Thread,
                                               NULL) != NULL)
    {
        /*
         * Another processor got there first. It is about to stop this one;
         * waiting here is the only correct thing to do, because carrying on
         * would change state its dump is about to record.
         */
        for (;;)
            YieldProcessor();
    }

    KeBugCheckActive = TRUE;
    KeBugCheckCount++;

    KiBugCheckData[0] = BugCheckCode;
    KiBugCheckData[1] = BugCheckParameter1;
    KiBugCheckData[2] = BugCheckParameter2;
    KiBugCheckData[3] = BugCheckParameter3;
    KiBugCheckData[4] = BugCheckParameter4;

    /* Hold every other processor where it is */
    KeFreezeExecution(TrapFrame, NULL);

    /*
     * The register state, as a context record. A trap frame is the better
     * source where there is one: it is the state at the fault rather than the
     * state inside this routine.
     */
    RtlZeroMemory(&Context, sizeof(Context));
    Context.ContextFlags = CONTEXT_FULL;

    if (TrapFrame != NULL)
    {
        KeTrapFrameToContext(TrapFrame, NULL, &Context);
        ContextPointer = &Context;
    }
    else
    {
        RtlCaptureContext(&Context);
        ContextPointer = &Context;
    }

    /* A dump writer finds the state through the processor block */
    Prcb = KeGetCurrentPrcb();
    Prcb->CrashDumpContext = ContextPointer;

    KiDoBugCheckCallbacks();
    KiDoBugCheckReasonCallbacks(KbCallbackDumpIo);

    /* The debugger's first look, before the screen is taken over */
    KiBugCheckDebugBreak(DBG_STATUS_BUGCHECK_FIRST);

    KiDisplayBlueScreen(BugCheckCode, TrapFrame, ContextPointer);

    /* And its second, now that everything has been said */
    KiBugCheckDebugBreak(DBG_STATUS_BUGCHECK_SECOND);

    if (IopAutoReboot)
    {
        HalReturnToFirmware(HalRebootRoutine);
    }

    for (;;)
    {
        KeEnterKernelDebugger();
    }
}

DECLSPEC_NORETURN
VOID
NTAPI
KeBugCheckEx(
    _In_ ULONG BugCheckCode,
    _In_ ULONG_PTR BugCheckParameter1,
    _In_ ULONG_PTR BugCheckParameter2,
    _In_ ULONG_PTR BugCheckParameter3,
    _In_ ULONG_PTR BugCheckParameter4)
{
    KeBugCheckWithTf(BugCheckCode,
                     BugCheckParameter1,
                     BugCheckParameter2,
                     BugCheckParameter3,
                     BugCheckParameter4,
                     NULL);
}

DECLSPEC_NORETURN
VOID
NTAPI
KeBugCheck(
    _In_ ULONG BugCheckCode)
{
    KeBugCheckEx(BugCheckCode, 0, 0, 0, 0);
}

/*
 * Hands the machine to the debugger and never takes it back. Reached where
 * there is nothing sensible left to do, including from inside the stop path
 * when that has itself failed.
 */
DECLSPEC_NORETURN
VOID
NTAPI
KeEnterKernelDebugger(VOID)
{
    KeBugCheckActive = TRUE;

    KfRaiseIrql(HIGH_LEVEL);

    for (;;)
    {
        if (!KdPitchDebugger && !KdDebuggerNotPresent)
        {
            _SEH2_TRY
            {
                DbgBreakPointWithStatus(DBG_STATUS_FATAL);
            }
            _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
            {
                NOTHING;
            }
            _SEH2_END;
        }

        YieldProcessor();
    }
}

/* PUBLIC FUNCTIONS: CALLBACKS *************************************************/

/*
 * Registers a buffer to be filled in when the system stops. The record is the
 * caller's and must stay valid until it is withdrawn, because by the time it is
 * used nothing can be allocated to copy it into.
 */
BOOLEAN
NTAPI
KeRegisterBugCheckCallback(
    _Out_ PKBUGCHECK_CALLBACK_RECORD CallbackRecord,
    _In_ PKBUGCHECK_CALLBACK_ROUTINE CallbackRoutine,
    _In_opt_ PVOID Buffer,
    _In_ ULONG Length,
    _In_ PUCHAR Component)
{
    KIRQL OldIrql;
    BOOLEAN Registered = FALSE;

    ASSERT_IRQL_LESS_OR_EQUAL(DISPATCH_LEVEL);

    KeAcquireSpinLock(&BugCheckCallbackLock, &OldIrql);

    if (CallbackRecord->State == BufferEmpty)
    {
        CallbackRecord->CallbackRoutine = CallbackRoutine;
        CallbackRecord->Buffer = Buffer;
        CallbackRecord->Length = Length;
        CallbackRecord->Component = Component;
        CallbackRecord->Checksum = (ULONG_PTR)CallbackRoutine +
                                   (ULONG_PTR)Buffer +
                                   (ULONG_PTR)Length +
                                   (ULONG_PTR)Component;
        CallbackRecord->State = BufferInserted;

        InsertTailList(&KeBugcheckCallbackListHead, &CallbackRecord->Entry);
        Registered = TRUE;
    }

    KeReleaseSpinLock(&BugCheckCallbackLock, OldIrql);
    return Registered;
}

BOOLEAN
NTAPI
KeDeregisterBugCheckCallback(
    _Inout_ PKBUGCHECK_CALLBACK_RECORD CallbackRecord)
{
    KIRQL OldIrql;
    BOOLEAN Removed = FALSE;

    KeAcquireSpinLock(&BugCheckCallbackLock, &OldIrql);

    if (CallbackRecord->State == BufferInserted)
    {
        CallbackRecord->State = BufferEmpty;
        RemoveEntryList(&CallbackRecord->Entry);
        Removed = TRUE;
    }

    KeReleaseSpinLock(&BugCheckCallbackLock, OldIrql);
    return Removed;
}

/*
 * The same, for a callback that is told why the system stopped and can act
 * differently for each reason.
 */
BOOLEAN
NTAPI
KeRegisterBugCheckReasonCallback(
    _Out_ PKBUGCHECK_REASON_CALLBACK_RECORD CallbackRecord,
    _In_ PKBUGCHECK_REASON_CALLBACK_ROUTINE CallbackRoutine,
    _In_ KBUGCHECK_CALLBACK_REASON Reason,
    _In_ PUCHAR Component)
{
    KIRQL OldIrql;
    BOOLEAN Registered = FALSE;

    ASSERT_IRQL_LESS_OR_EQUAL(DISPATCH_LEVEL);

    KeAcquireSpinLock(&BugCheckCallbackLock, &OldIrql);

    if (CallbackRecord->State == BufferEmpty)
    {
        CallbackRecord->CallbackRoutine = CallbackRoutine;
        CallbackRecord->Component = Component;
        CallbackRecord->Reason = Reason;
        CallbackRecord->Checksum = (ULONG_PTR)CallbackRoutine +
                                   (ULONG_PTR)Reason +
                                   (ULONG_PTR)Component;
        CallbackRecord->State = BufferInserted;

        InsertTailList(&KeBugcheckReasonCallbackListHead,
                       &CallbackRecord->Entry);
        Registered = TRUE;
    }

    KeReleaseSpinLock(&BugCheckCallbackLock, OldIrql);
    return Registered;
}

BOOLEAN
NTAPI
KeDeregisterBugCheckReasonCallback(
    _Inout_ PKBUGCHECK_REASON_CALLBACK_RECORD CallbackRecord)
{
    KIRQL OldIrql;
    BOOLEAN Removed = FALSE;

    KeAcquireSpinLock(&BugCheckCallbackLock, &OldIrql);

    if (CallbackRecord->State == BufferInserted)
    {
        CallbackRecord->State = BufferEmpty;
        RemoveEntryList(&CallbackRecord->Entry);
        Removed = TRUE;
    }

    KeReleaseSpinLock(&BugCheckCallbackLock, OldIrql);
    return Removed;
}

/* PUBLIC FUNCTIONS: THE UNMASKABLE INTERRUPT **********************************/

/*
 * Registers a handler for the unmaskable interrupt. The handler runs with
 * everything else on the processor held off, so what it may do is narrower
 * than for any other callback.
 *
 * Answers an opaque handle, which is the record itself with its own address
 * folded in so that a handle made up by a caller does not match anything.
 */
PVOID
NTAPI
KeRegisterNmiCallback(
    _In_ PNMI_CALLBACK CallbackRoutine,
    _In_opt_ PVOID Context)
{
    PKNMI_HANDLER_CALLBACK Handler;
    KIRQL OldIrql;

    ASSERT_IRQL_LESS_OR_EQUAL(APC_LEVEL);

    Handler = ExAllocatePoolWithTag(NonPagedPool,
                                    sizeof(KNMI_HANDLER_CALLBACK),
                                    TAG_KNMI);
    if (Handler == NULL)
        return NULL;

    Handler->Callback = CallbackRoutine;
    Handler->Context = Context;
    Handler->Handle = Handler;

    KiAcquireNmiListLock(&OldIrql);
    Handler->Next = KiNmiCallbackListHead;
    KiNmiCallbackListHead = Handler;
    KiReleaseNmiListLock(OldIrql);

    return Handler->Handle;
}

/*
 * Withdraws such a handler. The handler may be running on another processor
 * right now, so the record is only freed once it is off the list and the
 * interrupt that could have been in it cannot still be.
 */
NTSTATUS
NTAPI
KeDeregisterNmiCallback(
    _In_ PVOID Handle)
{
    PKNMI_HANDLER_CALLBACK Handler, Previous = NULL;
    KIRQL OldIrql;

    ASSERT_IRQL_LESS_OR_EQUAL(APC_LEVEL);

    KiAcquireNmiListLock(&OldIrql);

    Handler = KiNmiCallbackListHead;
    while (Handler != NULL)
    {
        if (Handler->Handle == Handle)
            break;

        Previous = Handler;
        Handler = Handler->Next;
    }

    if (Handler == NULL)
    {
        KiReleaseNmiListLock(OldIrql);
        return STATUS_INVALID_HANDLE;
    }

    if (Previous != NULL)
        Previous->Next = Handler->Next;
    else
        KiNmiCallbackListHead = Handler->Next;

    KiReleaseNmiListLock(OldIrql);

    /*
     * The list is read without the lock from the interrupt, so a processor may
     * still be walking the old chain. Draining the queues on every processor
     * is what makes it certain that none is.
     */
    KeFlushQueuedDpcs();

    ExFreePoolWithTag(Handler, TAG_KNMI);
    return STATUS_SUCCESS;
}

/*
 * The unmaskable interrupt. Answers TRUE where a handler claimed it, in which
 * case the architecture layer returns from it; otherwise the interrupt is a
 * hardware fault nobody owns and the system stops.
 *
 * The list is walked without its lock on purpose: the interrupt cannot wait
 * for a lock that the interrupted code may be holding.
 */
BOOLEAN
NTAPI
KiHandleNmi(VOID)
{
    PKNMI_HANDLER_CALLBACK Handler;
    BOOLEAN Handled = FALSE;

    Handler = KiNmiCallbackListHead;
    while (Handler != NULL)
    {
        if (Handler->Callback(Handler->Context, Handled))
            Handled = TRUE;

        Handler = Handler->Next;
    }

    /*
     * Nobody owned it, so it came from the machine itself. Recording that
     * before the architecture layer stops the system is what tells a reader
     * the stop was a hardware report and not a software fault.
     */
    if (!Handled)
        KiHardwareTrigger = 1;

    return Handled;
}

/* PUBLIC FUNCTIONS: TRIAGE DATA ***********************************************/

/*
 * Prepares a caller's buffer to hold a list of memory ranges that should be
 * saved in a small dump. The buffer has to be big enough for at least one
 * range, or there is nothing it could record.
 */
NTSTATUS
NTAPI
KeInitializeTriageDumpDataArray(
    _Out_ PKTRIAGE_DUMP_DATA_ARRAY TriageDumpDataArray,
    _In_ ULONG Size)
{
    ULONG HeaderSize = (ULONG)FIELD_OFFSET(KTRIAGE_DUMP_DATA_ARRAY, Blocks);
    ULONG BlockCount;

    if (TriageDumpDataArray == NULL)
        return STATUS_INVALID_PARAMETER;

    if (Size < (HeaderSize + sizeof(KADDRESS_RANGE)))
        return STATUS_BUFFER_TOO_SMALL;

    BlockCount = (Size - HeaderSize) / (ULONG)sizeof(KADDRESS_RANGE);

    RtlZeroMemory(TriageDumpDataArray, Size);

    InitializeListHead(&TriageDumpDataArray->List);
    TriageDumpDataArray->NumBlocksUsed = 0;
    TriageDumpDataArray->NumBlocksTotal = BlockCount;
    TriageDumpDataArray->DataSize = 0;
    TriageDumpDataArray->MaxDataSize = KE_MAX_TRIAGE_DUMP_DATA_MEMORY_SIZE;
    TriageDumpDataArray->ComponentNameBufferLength = 0;
    TriageDumpDataArray->ComponentName = NULL;

    return STATUS_SUCCESS;
}

/*
 * Adds one range of memory to that list.
 *
 * Ranges are half-open and are not merged for touching one another: two ranges
 * that happen to meet are still two ranges, because the caller named them
 * separately and may mean them separately. A range already wholly inside one
 * that is listed adds nothing and is accepted without a new entry.
 */
NTSTATUS
NTAPI
KeAddTriageDumpDataBlock(
    _Inout_ PKTRIAGE_DUMP_DATA_ARRAY TriageDumpDataArray,
    _In_ PVOID Address,
    _In_ SIZE_T Size)
{
    PKADDRESS_RANGE Blocks;
    ULONG_PTR Start = (ULONG_PTR)Address;
    ULONG_PTR End;
    ULONG Index;

    if ((TriageDumpDataArray == NULL) || (Size == 0))
        return STATUS_INVALID_PARAMETER;

    /* A range that runs off the end of the address space names nothing */
    End = Start + Size;
    if (End <= Start)
        return STATUS_INVALID_PARAMETER;

    Blocks = (PKADDRESS_RANGE)TriageDumpDataArray->Blocks;

    for (Index = 0; Index < TriageDumpDataArray->NumBlocksUsed; Index++)
    {
        ULONG_PTR BlockStart = (ULONG_PTR)Blocks[Index].Address;
        ULONG_PTR BlockEnd = BlockStart + Blocks[Index].Size;

        if ((Start >= BlockStart) && (End <= BlockEnd))
            return STATUS_SUCCESS;
    }

    if (TriageDumpDataArray->NumBlocksUsed >=
        TriageDumpDataArray->NumBlocksTotal)
    {
        return STATUS_BUFFER_TOO_SMALL;
    }

    if ((TriageDumpDataArray->DataSize + Size) >
        TriageDumpDataArray->MaxDataSize)
    {
        return STATUS_BUFFER_TOO_SMALL;
    }

    Blocks[TriageDumpDataArray->NumBlocksUsed].Address = Address;
    Blocks[TriageDumpDataArray->NumBlocksUsed].Size = Size;
    TriageDumpDataArray->NumBlocksUsed++;
    TriageDumpDataArray->DataSize += (ULONG)Size;

    return STATUS_SUCCESS;
}

/* PUBLIC FUNCTIONS: THE DUMP HEADER *******************************************/

/*
 * Fills in the header a crash dump file starts with. It names where the reader
 * has to look for everything else: the page tables, the loaded module list, the
 * process list and the debugger's own data block.
 *
 * The context record is the state at the stop, taken from the processor block
 * where the stop path left it.
 */
NTSTATUS
NTAPI
KeInitializeCrashDumpHeader(
    _In_ ULONG Type,
    _In_ ULONG Flags,
    _Out_writes_bytes_(BufferSize) PVOID Buffer,
    _In_ ULONG BufferSize,
    _Out_opt_ PULONG BufferNeeded)
{
    PDUMP_HEADER64 Header;
    PCONTEXT Context;
    LARGE_INTEGER SystemTime, BootTime;

    UNREFERENCED_PARAMETER(Flags);

    if (BufferNeeded != NULL)
        *BufferNeeded = DUMP_HEADER64_SIZE;

    if ((Type != DUMP_TYPE_FULL) &&
        (Type != DUMP_TYPE_SUMMARY) &&
        (Type != DUMP_TYPE_HEADER) &&
        (Type != DUMP_TYPE_TRIAGE) &&
        (Type != DUMP_TYPE_BITMAP_FULL) &&
        (Type != DUMP_TYPE_BITMAP_KERNEL) &&
        (Type != DUMP_TYPE_AUTOMATIC))
    {
        return STATUS_INVALID_PARAMETER;
    }

    if (Buffer == NULL)
        return STATUS_INVALID_PARAMETER;

    if (BufferSize < DUMP_HEADER64_SIZE)
        return STATUS_BUFFER_TOO_SMALL;

    Header = (PDUMP_HEADER64)Buffer;
    RtlZeroMemory(Header, DUMP_HEADER64_SIZE);

    Header->Signature = DUMP_SIGNATURE64;
    Header->ValidDump = DUMP_VALID_DUMP64;
    Header->MajorVersion = NtBuildNumber >> 28;
    Header->MinorVersion = NtBuildNumber & 0xFFFF;

    Header->DirectoryTableBase =
        KPROCESS_DTB0(&PsGetCurrentProcess()->Pcb);
    Header->PfnDataBase = (ULONG64)(ULONG_PTR)MmPfnDatabase;
    Header->PsLoadedModuleList = (ULONG64)(ULONG_PTR)&PsLoadedModuleList;
    Header->PsActiveProcessHead = (ULONG64)(ULONG_PTR)&PsActiveProcessHead;
    /*
     * A dump reader names the machine by the image type, not by the
     * architecture code the version interfaces use, so the two are mapped.
     */
    switch (KeProcessorArchitecture)
    {
        case PROCESSOR_ARCHITECTURE_AMD64:
            Header->MachineImageType = IMAGE_FILE_MACHINE_AMD64;
            break;
        case PROCESSOR_ARCHITECTURE_INTEL:
            Header->MachineImageType = IMAGE_FILE_MACHINE_I386;
            break;
        case PROCESSOR_ARCHITECTURE_ARM64:
            Header->MachineImageType = IMAGE_FILE_MACHINE_ARM64;
            break;
        case PROCESSOR_ARCHITECTURE_ARM:
            Header->MachineImageType = IMAGE_FILE_MACHINE_ARMNT;
            break;
        default:
            Header->MachineImageType = 0;
            break;
    }
    Header->NumberProcessors = (ULONG)(CCHAR)KeNumberProcessors;

    Header->BugCheckCode = (ULONG)KiBugCheckData[0];
    Header->BugCheckParameter1 = KiBugCheckData[1];
    Header->BugCheckParameter2 = KiBugCheckData[2];
    Header->BugCheckParameter3 = KiBugCheckData[3];
    Header->BugCheckParameter4 = KiBugCheckData[4];

    Header->KdDebuggerDataBlock = (ULONG64)(ULONG_PTR)&KdDebuggerDataBlock;

    /*
     * TODO: the runs of physical memory the file will hold are left empty;
     * the memory manager here publishes no run descriptor for the dump
     * writer to copy, and inventing one would describe memory wrongly.
     */

    Context = KeGetCurrentPrcb()->CrashDumpContext;
    if (Context != NULL)
    {
        RtlCopyMemory(Header->ContextRecord,
                      Context,
                      min(sizeof(CONTEXT), DUMP_CONTEXT_RECORD_SIZE64));
    }

    Header->DumpType = Type;

    KeQuerySystemTimePrecise(&SystemTime);
    Header->SystemTime = SystemTime;

    BootTime = KeBootTime;
    Header->SystemUpTime.QuadPart = SystemTime.QuadPart - BootTime.QuadPart;

    Header->ProductType = SharedUserData->NtProductType;
    Header->SuiteMask = SharedUserData->SuiteMask;
    Header->RequiredDumpSpace.QuadPart = 0;

    return STATUS_SUCCESS;
}

/* PUBLIC FUNCTIONS: INITIALISATION ********************************************/

/*
 * Finds the message table the stop messages come from, which lives in the
 * kernel's own resources. Doing it now and not when the system stops is the
 * point: by then the resource directory may be exactly what is broken.
 */
CODE_SEG("INIT")
VOID
NTAPI
KiInitializeBugCheck(VOID)
{
    PMESSAGE_RESOURCE_DATA Messages;
    PLDR_DATA_TABLE_ENTRY Kernel;
    LDR_RESOURCE_INFO ResourceInfo;
    PIMAGE_RESOURCE_DATA_ENTRY DataEntry;
    ULONG Size;
    NTSTATUS Status;

    if (PsLoadedModuleList.Flink == NULL)
        return;

    Kernel = CONTAINING_RECORD(PsLoadedModuleList.Flink,
                               LDR_DATA_TABLE_ENTRY,
                               InLoadOrderLinks);

    ResourceInfo.Type = KI_RT_MESSAGETABLE;
    ResourceInfo.Name = 1;
    ResourceInfo.Language = LANG_NEUTRAL;

    Status = LdrFindResource_U(Kernel->DllBase,
                               &ResourceInfo,
                               RESOURCE_DATA_LEVEL,
                               &DataEntry);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("No stop message table in the kernel image: %lx\n", Status);
        return;
    }

    Status = LdrAccessResource(Kernel->DllBase,
                               DataEntry,
                               (PVOID *)&Messages,
                               &Size);
    if (!NT_SUCCESS(Status))
        return;

    KiBugCodeMessages = Messages;
    KiBugCheckImageBase = Kernel->DllBase;
}

/* EOF */
