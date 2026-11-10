/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     What the system says to the debugger, and what it asks of it
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#include "kdpriv.h"
#define NDEBUG
#include <debug.h>

/* PRIVATE DATA ***************************************************************/

/* How many callers may watch the debug output at once */
#define KDP_MAX_PRINT_CALLBACKS 8

/*
 * Everything ever written to the log, counted in bytes. The buffer only holds
 * the last KdPrintBufferSize of them, so this is what tells a reader how much
 * it missed.
 */
static ULONG64 KdpPrintTotalWritten = 0;

static PDEBUG_PRINT_CALLBACK KdpPrintCallbacks[KDP_MAX_PRINT_CALLBACKS];

/* The line the engine builds for its own output */
static CHAR KdpDprintfBuffer[KDP_MSG_BUFFER_SIZE];

/* PRIVATE FUNCTIONS **********************************************************/

VOID
NTAPI
KdpAcquireLock(
    _In_ PKSPIN_LOCK SpinLock,
    _Out_ PKIRQL OldIrql)
{
    KeRaiseIrql(HIGH_LEVEL, OldIrql);
    KxAcquireSpinLock(SpinLock);
}

VOID
NTAPI
KdpReleaseLock(
    _In_ PKSPIN_LOCK SpinLock,
    _In_ KIRQL OldIrql)
{
    KxReleaseSpinLock(SpinLock);
    KeLowerIrql(OldIrql);
}

/*
 * Takes the print lock away from whoever held it. Only the bugcheck path may
 * do this, once every other processor is held and the one that crashed is the
 * only one left: a crash inside a print would otherwise spin here, and the
 * machine would say nothing about why it stopped.
 */
VOID
NTAPI
KdpResetPrintLock(VOID)
{
    KdpPrintSpinLock = 0;
}

/*
 * Appends to the circular log. The log is the only record of what was printed
 * that survives into a crash dump, so it is written whether or not anyone is
 * listening.
 */
static
VOID
KdpAppendToLog(
    _In_reads_bytes_(Length) PCCH String,
    _In_ ULONG Length)
{
    KIRQL OldIrql;
    ULONG Offset;
    ULONG Index;

    BOOLEAN Locked;

    if ((KdPrintCircularBuffer == NULL) || (KdPrintBufferSize == 0))
        return;

    /* Nothing older than one trip round the buffer can be kept anyway */
    if (Length > KdPrintBufferSize)
    {
        String += Length - KdPrintBufferSize;
        Length = KdPrintBufferSize;
    }

    /*
     * Inside the debugger this processor is the only one running, and another
     * processor may have been frozen while holding this lock. Taking it then
     * would wait for a processor that cannot answer, so it is not taken.
     */
    Locked = !(KdpPortLocked && (KdpPortOwnerPrcb == KeGetCurrentPrcb()));
    if (Locked)
        KdpAcquireLock(&KdpPrintSpinLock, &OldIrql);
    else
        OldIrql = KeGetCurrentIrql();

    Offset = (ULONG)(KdPrintWritePointer - KdPrintCircularBuffer);
    if (Offset >= KdPrintBufferSize)
        Offset = 0;

    for (Index = 0; Index < Length; Index++)
    {
        KdPrintCircularBuffer[Offset] = String[Index];

        if (++Offset == KdPrintBufferSize)
        {
            Offset = 0;
            KdPrintRolloverCount++;
        }
    }

    KdPrintWritePointer = KdPrintCircularBuffer + Offset;
    KdpPrintTotalWritten += Length;

    if (Locked)
        KdpReleaseLock(&KdpPrintSpinLock, OldIrql);
}

/* PUBLIC FUNCTIONS ***********************************************************/

/*
 * Writes one string into the log without telling anyone else. Used where the
 * output has already been shown by other means and only the record is wanted.
 */
VOID
NTAPI
KdLogDbgPrint(
    _In_ PSTRING String)
{
    if ((String == NULL) || (String->Buffer == NULL) || (String->Length == 0))
        return;

    KdpAppendToLog(String->Buffer, String->Length);
}

/*
 * Hands the log to a reader from the beginning, oldest byte first. Returns how
 * much was copied and how many times the log has wrapped, which is how the
 * reader knows whether it is seeing the whole story.
 */
ULONG
NTAPI
KdpCopyPrintBuffer(
    _Out_writes_bytes_(BufferSize) PCHAR Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG RolloverCount)
{
    KIRQL OldIrql;
    ULONG Offset;
    ULONG Available;
    ULONG Length;
    ULONG Start;
    ULONG Index;

    *RolloverCount = 0;

    if ((Buffer == NULL) || (BufferSize == 0) || (KdPrintCircularBuffer == NULL))
        return 0;

    KdpAcquireLock(&KdpPrintSpinLock, &OldIrql);

    *RolloverCount = KdPrintRolloverCount;

    Offset = (ULONG)(KdPrintWritePointer - KdPrintCircularBuffer);
    if (Offset > KdPrintBufferSize)
        Offset = 0;

    /* Before the first wrap only what was written is there to be read */
    Available = (KdPrintRolloverCount == 0) ? Offset : KdPrintBufferSize;

    Length = (Available < BufferSize) ? Available : BufferSize;

    /* Take the newest Length bytes, so a short buffer loses the oldest */
    Start = (Offset + KdPrintBufferSize - Length) % KdPrintBufferSize;

    for (Index = 0; Index < Length; Index++)
    {
        Buffer[Index] = KdPrintCircularBuffer[Start];
        if (++Start == KdPrintBufferSize)
            Start = 0;
    }

    KdpReleaseLock(&KdpPrintSpinLock, OldIrql);

    return Length;
}

/*
 * Reads the log as a stream. Position is a byte count into everything ever
 * written; what has already been overwritten is reported through Lost instead
 * of being silently skipped.
 */
ULONG
NTAPI
KdpCopyPrintBufferFrom(
    _Inout_ PULONG64 Position,
    _Out_writes_bytes_(BufferSize) PCHAR Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG64 Lost)
{
    KIRQL OldIrql;
    ULONG64 Oldest;
    ULONG64 From;
    ULONG Offset;
    ULONG Start;
    ULONG Length;
    ULONG Index;

    *Lost = 0;

    if ((Buffer == NULL) || (BufferSize == 0) || (KdPrintCircularBuffer == NULL))
        return 0;

    KdpAcquireLock(&KdpPrintSpinLock, &OldIrql);

    /* The oldest byte still in the buffer, as a stream position */
    Oldest = (KdpPrintTotalWritten > KdPrintBufferSize)
             ? (KdpPrintTotalWritten - KdPrintBufferSize) : 0;

    From = *Position;
    if (From < Oldest)
    {
        *Lost = Oldest - From;
        From = Oldest;
    }

    if (From >= KdpPrintTotalWritten)
    {
        KdpReleaseLock(&KdpPrintSpinLock, OldIrql);
        *Position = KdpPrintTotalWritten;
        return 0;
    }

    Length = (ULONG)(KdpPrintTotalWritten - From);
    if (Length > BufferSize)
        Length = BufferSize;

    Offset = (ULONG)(KdPrintWritePointer - KdPrintCircularBuffer);
    if (Offset > KdPrintBufferSize)
        Offset = 0;

    /* Walk back from the write point by however much is still unread */
    Start = (ULONG)((Offset + KdPrintBufferSize -
                     (ULONG)(KdpPrintTotalWritten - From)) % KdPrintBufferSize);

    for (Index = 0; Index < Length; Index++)
    {
        Buffer[Index] = KdPrintCircularBuffer[Start];
        if (++Start == KdPrintBufferSize)
            Start = 0;
    }

    *Position = From + Length;

    KdpReleaseLock(&KdpPrintSpinLock, OldIrql);

    return Length;
}

/*
 * Hands over the end of the log, newest bytes last, for a reader that only
 * wants to show what happened just now.
 */
ULONG
NTAPI
KdpCopyPrintBufferTail(
    _Out_writes_bytes_(BufferSize) PCHAR Buffer,
    _In_ ULONG BufferSize)
{
    ULONG Rollover;

    return KdpCopyPrintBuffer(Buffer, BufferSize, &Rollover);
}

/*
 * Tells everyone who asked to see the debug output. The callbacks run with the
 * output exactly as the caller wrote it.
 */
VOID
NTAPI
KdpPrintToCallbacks(
    _In_ ULONG ComponentId,
    _In_ ULONG Level,
    _In_reads_bytes_(Length) PCHAR String,
    _In_ USHORT Length,
    _In_ KPROCESSOR_MODE PreviousMode)
{
    STRING Output;
    ULONG Index;

    UNREFERENCED_PARAMETER(PreviousMode);

    Output.Buffer = String;
    Output.Length = Length;
    Output.MaximumLength = Length;

    for (Index = 0; Index < KDP_MAX_PRINT_CALLBACKS; Index++)
    {
        PDEBUG_PRINT_CALLBACK Callback = KdpPrintCallbacks[Index];

        if (Callback != NULL)
            Callback(&Output, ComponentId, Level);
    }
}

/*
 * Lets a driver watch everything the system prints. Documented to fail when
 * the list is full and when a callback is removed that was never added.
 */
NTSTATUS
NTAPI
DbgSetDebugPrintCallback(
    _In_ PDEBUG_PRINT_CALLBACK DebugPrintCallback,
    _In_ BOOLEAN Enable)
{
    KIRQL OldIrql;
    ULONG Index;
    ULONG Free = KDP_MAX_PRINT_CALLBACKS;
    NTSTATUS Status = STATUS_NOT_FOUND;

    if (DebugPrintCallback == NULL)
        return STATUS_INVALID_PARAMETER;

    KdpAcquireLock(&KdpPrintSpinLock, &OldIrql);

    for (Index = 0; Index < KDP_MAX_PRINT_CALLBACKS; Index++)
    {
        if (KdpPrintCallbacks[Index] == DebugPrintCallback)
        {
            if (Enable)
            {
                Status = STATUS_SUCCESS;
            }
            else
            {
                KdpPrintCallbacks[Index] = NULL;
                Status = STATUS_SUCCESS;
            }

            KdpReleaseLock(&KdpPrintSpinLock, OldIrql);
            return Status;
        }

        if ((KdpPrintCallbacks[Index] == NULL) && (Free == KDP_MAX_PRINT_CALLBACKS))
            Free = Index;
    }

    if (Enable)
    {
        if (Free == KDP_MAX_PRINT_CALLBACKS)
        {
            Status = STATUS_INSUFFICIENT_RESOURCES;
        }
        else
        {
            KdpPrintCallbacks[Free] = DebugPrintCallback;
            Status = STATUS_SUCCESS;
        }
    }

    KdpReleaseLock(&KdpPrintSpinLock, OldIrql);
    return Status;
}

/*
 * Sends one string to the host. Returns TRUE when the host answered by asking
 * to stop the machine. The caller owns the debug link.
 */
BOOLEAN
NTAPI
KdpPrintString(
    _In_ PSTRING Output)
{
    STRING Header;
    STRING Data;
    DBGKD_DEBUG_IO DebugIo;

    if ((Output == NULL) || (Output->Buffer == NULL))
        return FALSE;

    DebugIo.ApiNumber = DbgKdPrintStringApi;
    DebugIo.ProcessorLevel = KeProcessorLevel;
    DebugIo.Processor = (USHORT)KeGetCurrentPrcb()->Number;
    DebugIo.u.PrintString.LengthOfString = Output->Length;

    Header.Length = sizeof(DBGKD_DEBUG_IO);
    Header.MaximumLength = sizeof(DBGKD_DEBUG_IO);
    Header.Buffer = (PCHAR)&DebugIo;

    Data = *Output;

    KdSendPacket(PACKET_TYPE_KD_DEBUG_IO, &Header, &Data, &KdpContext);

    /* A break-in may have arrived while the host was being told */
    if (KdpContext.KdpControlCPending)
    {
        KdpContext.KdpControlCPending = FALSE;
        KdpControlCPressed = TRUE;
        return TRUE;
    }

    return FALSE;
}

/*
 * Asks the host a question and waits for the answer. Returns how much of the
 * answer was read; nothing read means the host did not answer.
 */
USHORT
NTAPI
KdpPromptString(
    _In_ PSTRING PromptString,
    _Inout_ PSTRING ResponseString)
{
    STRING Header;
    STRING Data;
    DBGKD_DEBUG_IO DebugIo;
    KDSTATUS Status;
    ULONG Length = 0;
    ULONG Retries = KdpContext.KdpDefaultRetries;
    BOOLEAN Ask = TRUE;

    DebugIo.ApiNumber = DbgKdGetStringApi;
    DebugIo.ProcessorLevel = KeProcessorLevel;
    DebugIo.Processor = (USHORT)KeGetCurrentPrcb()->Number;
    DebugIo.u.GetString.LengthOfPromptString = PromptString->Length;
    DebugIo.u.GetString.LengthOfStringRead = ResponseString->MaximumLength;

    for (;;)
    {
        if (Ask)
        {
            Header.Length = sizeof(DBGKD_DEBUG_IO);
            Header.MaximumLength = sizeof(DBGKD_DEBUG_IO);
            Header.Buffer = (PCHAR)&DebugIo;

            Data = *PromptString;

            KdSendPacket(PACKET_TYPE_KD_DEBUG_IO, &Header, &Data, &KdpContext);
            Ask = FALSE;
        }

        Header.Length = 0;
        Header.MaximumLength = sizeof(DBGKD_DEBUG_IO);
        Header.Buffer = (PCHAR)&DebugIo;

        Data.Length = 0;
        Data.MaximumLength = ResponseString->MaximumLength;
        Data.Buffer = ResponseString->Buffer;

        Status = KdReceivePacket(PACKET_TYPE_KD_DEBUG_IO,
                                 &Header,
                                 &Data,
                                 &Length,
                                 &KdpContext);

        if (Status == KdPacketNeedsResend)
        {
            Ask = TRUE;
            continue;
        }

        if (Status == KdPacketTimedOut)
        {
            if (Retries == 0)
            {
                KdDebuggerNotPresent = TRUE;
                ResponseString->Length = 0;
                return 0;
            }

            Retries--;
            Ask = TRUE;
            continue;
        }

        if (Status != KdPacketReceived)
            continue;

        if (DebugIo.ApiNumber != DbgKdGetStringApi)
            continue;

        KdDebuggerNotPresent = FALSE;

        if (Length > ResponseString->MaximumLength)
            Length = ResponseString->MaximumLength;

        ResponseString->Length = (USHORT)Length;
        return (USHORT)Length;
    }
}

/*
 * The engine's own way of saying something, used where there is no component
 * and no filter to consult: the message is the debugger talking, not the
 * system printing.
 */
VOID
__cdecl
KdpDprintf(
    _In_ PCSTR Format,
    ...)
{
    STRING Output;
    va_list Arguments;
    int Length;

    va_start(Arguments, Format);
    Length = _vsnprintf(KdpDprintfBuffer,
                        sizeof(KdpDprintfBuffer) - 1,
                        Format,
                        Arguments);
    va_end(Arguments);

    if (Length < 0)
    {
        /* Truncated: say what fitted rather than nothing */
        Length = sizeof(KdpDprintfBuffer) - 1;
    }

    KdpDprintfBuffer[Length] = ANSI_NULL;

    Output.Buffer = KdpDprintfBuffer;
    Output.Length = (USHORT)Length;
    Output.MaximumLength = (USHORT)Length;

    KdpAppendToLog(Output.Buffer, Output.Length);

    KdpPortLock();
    KdpPrintString(&Output);
    KdpPortUnlock();
}

/*
 * A component printed something. The log always gets it, the watchers always
 * see it, and the host sees it when the filter for that component allows it.
 */
NTSTATUS
NTAPI
KdpPrint(
    _In_ ULONG ComponentId,
    _In_ ULONG Level,
    _In_reads_bytes_(Length) PCHAR String,
    _In_ USHORT Length,
    _In_ KPROCESSOR_MODE PreviousMode,
    _In_ PKTRAP_FRAME TrapFrame,
    _In_ PKEXCEPTION_FRAME ExceptionFrame,
    _Out_ PBOOLEAN Handled)
{
    STRING Output;
    NTSTATUS Status = STATUS_SUCCESS;
    BOOLEAN BreakIn;

    UNREFERENCED_PARAMETER(TrapFrame);
    UNREFERENCED_PARAMETER(ExceptionFrame);

    *Handled = TRUE;

    if (PreviousMode != KernelMode)
    {
        /* A caller from user mode has to be able to show what it claims to */
        _SEH2_TRY
        {
            ProbeForRead(String, Length, sizeof(CHAR));
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            Status = _SEH2_GetExceptionCode();
        }
        _SEH2_END;

        if (!NT_SUCCESS(Status))
            return Status;
    }

    Output.Buffer = String;
    Output.Length = Length;
    Output.MaximumLength = Length;

    KdpAppendToLog(String, Length);
    KdpPrintToCallbacks(ComponentId, Level, String, Length, PreviousMode);

    /* Whether this component at this level is shown at all */
    if (!KdpFilterAllows(ComponentId, Level))
        return STATUS_SUCCESS;

    if (!KdDebuggerEnabled)
        return STATUS_DEVICE_NOT_CONNECTED;

    /*
     * The providers are local: a serial line, the screen and the log file show
     * what is printed whether or not a host ever answers, so the string is
     * sent and only the status says that nobody is listening.
     */
    KdpPortLock();
    BreakIn = KdpPrintString(&Output);
    KdpPortUnlock();

    /* The caller is told so that it can stop where it stands */
    return BreakIn ? STATUS_BREAKPOINT : STATUS_SUCCESS;
}

/*
 * Whether the buffers a prompt was given can be used at all. A prompt from
 * user mode reads one buffer and writes the other, so both are checked before
 * the host is told anything.
 */
BOOLEAN
NTAPI
KdpPromptBuffersAccessible(
    _In_reads_bytes_(PromptLength) PCHAR PromptString,
    _In_ USHORT PromptLength,
    _In_ PCHAR ResponseString,
    _In_ USHORT MaximumResponseLength,
    _In_ KPROCESSOR_MODE PreviousMode)
{
    BOOLEAN Accessible = TRUE;

    if (PreviousMode == KernelMode)
        return TRUE;

    _SEH2_TRY
    {
        ProbeForRead(PromptString, PromptLength, sizeof(CHAR));
        ProbeForWrite(ResponseString, MaximumResponseLength, sizeof(CHAR));
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Accessible = FALSE;
    }
    _SEH2_END;

    return Accessible;
}

/*
 * A component asked the person at the debugger a question.
 */
USHORT
NTAPI
KdpPrompt(
    _In_reads_bytes_(PromptLength) PCHAR PromptString,
    _In_ USHORT PromptLength,
    _Out_writes_bytes_(MaximumResponseLength) PCHAR ResponseString,
    _In_ USHORT MaximumResponseLength,
    _In_ KPROCESSOR_MODE PreviousMode,
    _In_ PKTRAP_FRAME TrapFrame,
    _In_ PKEXCEPTION_FRAME ExceptionFrame)
{
    STRING Prompt;
    STRING Response;
    USHORT Length;
    BOOLEAN Entered;

    UNREFERENCED_PARAMETER(TrapFrame);
    UNREFERENCED_PARAMETER(ExceptionFrame);

    if (!KdpPromptBuffersAccessible(PromptString,
                                    PromptLength,
                                    ResponseString,
                                    MaximumResponseLength,
                                    PreviousMode))
    {
        return 0;
    }

    if (!KdDebuggerEnabled)
        return 0;

    Prompt.Buffer = PromptString;
    Prompt.Length = PromptLength;
    Prompt.MaximumLength = PromptLength;

    Response.Buffer = ResponseString;
    Response.Length = 0;
    Response.MaximumLength = MaximumResponseLength;

    /*
     * The question and its answer are one exchange: nothing else may speak
     * over the link in between, and no other processor may run code that
     * would.
     */
    Entered = KdEnterDebugger(TrapFrame, ExceptionFrame);
    Length = KdpPromptString(&Prompt, &Response);
    KdExitDebugger(Entered);

    return Length;
}

/*
 * A module arrived or left. The host is told where it is so that it can find
 * its symbols, and it may stop the machine there if it was asked to.
 */
VOID
NTAPI
KdpSymbol(
    _In_ PSTRING DllPath,
    _In_ PKD_SYMBOLS_INFO SymbolInfo,
    _In_ BOOLEAN Unload,
    _In_ KPROCESSOR_MODE PreviousMode,
    _In_ PCONTEXT ContextRecord,
    _In_ PKTRAP_FRAME TrapFrame,
    _In_ PKEXCEPTION_FRAME ExceptionFrame)
{
    STRING Path;
    BOOLEAN Entered;

    if (!KdDebuggerEnabled || KdDebuggerNotPresent)
        return;

    Path.Buffer = NULL;
    Path.Length = 0;
    Path.MaximumLength = 0;

    if ((DllPath != NULL) && (DllPath->Buffer != NULL) && (DllPath->Length != 0))
    {
        USHORT Length = DllPath->Length;

        if (Length > KDP_MSG_BUFFER_SIZE - 1)
            Length = KDP_MSG_BUFFER_SIZE - 1;

        if (PreviousMode != KernelMode)
        {
            BOOLEAN Readable = TRUE;

            _SEH2_TRY
            {
                ProbeForRead(DllPath->Buffer, Length, sizeof(CHAR));
            }
            _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
            {
                Readable = FALSE;
            }
            _SEH2_END;

            if (!Readable)
                Length = 0;
        }

        if (Length != 0)
        {
            /*
             * The path is copied out of the caller's memory before the machine
             * stops, because once it has stopped the caller's address space is
             * not necessarily the one that is current.
             */
            KdpMoveMemory(KdpPathBuffer, DllPath->Buffer, Length);
            KdpPathBuffer[Length] = ANSI_NULL;

            Path.Buffer = KdpPathBuffer;
            Path.Length = Length + 1;
            Path.MaximumLength = Length + 1;
        }
    }

    Entered = KdEnterDebugger(TrapFrame, ExceptionFrame);
    KdpReportLoadSymbolsStateChange(&Path, SymbolInfo, Unload, ContextRecord);
    KdExitDebugger(Entered);
}

/*
 * A component asked the host to run a debugger command for it.
 */
VOID
NTAPI
KdpCommandString(
    _In_ PSTRING NameString,
    _In_ PSTRING CommandString,
    _In_ KPROCESSOR_MODE PreviousMode,
    _In_ PCONTEXT ContextRecord,
    _In_ PKTRAP_FRAME TrapFrame,
    _In_ PKEXCEPTION_FRAME ExceptionFrame)
{
    BOOLEAN Entered;

    if (!KdDebuggerEnabled || KdDebuggerNotPresent)
        return;

    if (PreviousMode != KernelMode)
    {
        BOOLEAN Readable = TRUE;

        _SEH2_TRY
        {
            ProbeForRead(NameString->Buffer, NameString->Length, sizeof(CHAR));
            ProbeForRead(CommandString->Buffer, CommandString->Length, sizeof(CHAR));
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            Readable = FALSE;
        }
        _SEH2_END;

        if (!Readable)
            return;
    }

    Entered = KdEnterDebugger(TrapFrame, ExceptionFrame);
    KdpReportCommandStringStateChange(NameString, CommandString, ContextRecord);
    KdExitDebugger(Entered);
}

/* EOF */
