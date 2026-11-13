/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Where the debug output goes, and who is listening
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#include "kd.h"
#include "kdterminal.h"
#ifdef KDBG
#include "../kdbg/kdb.h"
#endif
#define NDEBUG
#include <debug.h>

/* GLOBALS ********************************************************************/

/* Which ways out the boot options asked for */
KDP_DEBUG_MODE KdpDebugMode;

/* The serial line the serial provider speaks over */
ULONG SerialPortNumber = 0;
CPPORT SerialPortInfo = {NULL, 0, 0};

/* Everyone who has somewhere to put the output */
LIST_ENTRY KdProviders = {&KdProviders, &KdProviders};

KD_DISPATCH_TABLE DispatchTable[KdMax];

PKDP_INIT_ROUTINE InitRoutines[KdMax] =
{
    KdpScreenInit,
    KdpSerialInit,
    KdpNetInit,
#ifdef KDBG
    KdpKdbgInit,
#endif
};

/* PRIVATE DATA ***************************************************************/

/* The line KdIoPrintf builds, and the lock that keeps two of them apart */
static CHAR KdpIoBuffer[512];
static KSPIN_LOCK KdpIoLock;

#define KDP_BUGCHECK_DEBUGGER_SECONDS 3
#define KDP_BUGCHECK_POLLS_PER_SECOND 100

/* PRIVATE FUNCTIONS **********************************************************/

/*
 * Reads the value of one `NAME=VALUE` boot option. Returns a pointer just past
 * the equals sign, or NULL when the option is not there.
 */
static
PCSTR
KdpOptionValue(
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
        while ((*Current == ' ') || (*Current == '\t') || (*Current == '/'))
            Current++;

        if (*Current == ANSI_NULL)
            break;

        if ((_strnicmp(Current, Name, Length) == 0) && (Current[Length] == '='))
            return Current + Length + 1;

        while ((*Current != ANSI_NULL) && (*Current != ' ') && (*Current != '\t'))
            Current++;
    }

    return NULL;
}

static
ULONG
KdpReadDecimal(
    _In_ PCSTR String)
{
    ULONG Value = 0;

    while ((*String >= '0') && (*String <= '9'))
    {
        Value = (Value * 10) + (ULONG)(*String - '0');
        String++;
    }

    return Value;
}

/* PUBLIC FUNCTIONS ***********************************************************/

/*
 * Works out where the debug output is to go. The port the host is on is named
 * the same way on every architecture, so this is read once and the providers
 * are told what was asked for.
 */
VOID
NTAPI
KdpSelectDebugMode(
    _In_opt_ PLOADER_PARAMETER_BLOCK LoaderBlock)
{
    PCSTR Options = (LoaderBlock != NULL) ? LoaderBlock->LoadOptions : NULL;
    PCSTR Value;

    KeInitializeSpinLock(&KdpIoLock);

    KdpDebugMode.Value = 0;

    Value = KdpOptionValue(Options, "DEBUGPORT");
    if (Value == NULL)
    {
        /*
         * A machine told to debug but not told where is given the screen,
         * which is the one way out that needs no hardware to be named.
         */
        if (!KdPitchDebugger)
            KdpDebugMode.Screen = TRUE;

        return;
    }

    while (Value != NULL)
    {
        if (_strnicmp(Value, "SCREEN", 6) == 0)
        {
            KdpDebugMode.Screen = TRUE;
        }
        else if (_strnicmp(Value, "FILE", 4) == 0)
        {
            KdpDebugMode.File = TRUE;
            if (Value[4] == ':')
            {
                ULONG Length = 0;

                while ((Value[5 + Length] != ANSI_NULL) && (Value[5 + Length] != ',') &&
                       (Value[5 + Length] != ' ') && (Value[5 + Length] != '\t'))
                {
                    Length++;
                }

                if ((Length != 0) && (Length < sizeof(KdpBootLogPath)))
                {
                    RtlCopyMemory(KdpBootLogPath, Value + 5, Length);
                    KdpBootLogPath[Length] = ANSI_NULL;
                }
            }
        }
        else if (_strnicmp(Value, "NET", 3) == 0)
        {
            KdpDebugMode.Net = TRUE;
        }
        else if ((_strnicmp(Value, "COM", 3) == 0) && (Value[3] == ':'))
        {
            ULONG_PTR Address = strtoul(Value + 4, NULL, 0);

            if (Address != 0)
            {
                KdpDebugMode.Serial = TRUE;
                SerialPortNumber = 0;
                SerialPortInfo.Address = (PUCHAR)Address;
            }
        }
        else if (_strnicmp(Value, "COM", 3) == 0)
        {
            ULONG Number = KdpReadDecimal(Value + 3);

            /* COM without a number means the first line */
            if (Number == 0)
                Number = 1;

            if (KdPortIsComPortNumberValid(Number))
            {
                KdpDebugMode.Serial = TRUE;
                SerialPortNumber = Number;
            }
        }

        /* The option may name more than one way out, separated by commas */
        while ((*Value != ANSI_NULL) && (*Value != ',') &&
               (*Value != ' ') && (*Value != '\t'))
        {
            Value++;
        }

        Value = (*Value == ',') ? (Value + 1) : KdpOptionValue(Value, "DEBUGPORT");
    }

    Value = KdpOptionValue(Options, "BAUDRATE");
    if (Value != NULL)
        SerialPortInfo.BaudRate = KdpReadDecimal(Value);
}

/*
 * Brings the output providers up for one boot phase. A provider that cannot
 * work in this phase says so and is asked again in the next one.
 */
NTSTATUS
NTAPI
KdpInitializeProviders(
    _In_ ULONG BootPhase)
{
    ULONG Index;

    for (Index = 0; Index < KdMax; Index++)
    {
        PKD_DISPATCH_TABLE Table = &DispatchTable[Index];

        if (BootPhase == 0)
        {
            InitializeListHead(&Table->KdProvidersList);
            Table->KdpInitRoutine = InitRoutines[Index];
            Table->KdpPrintRoutine = NULL;
            Table->InitStatus = STATUS_SUCCESS;
        }

        if (Table->KdpInitRoutine == NULL)
            continue;

        /*
         * A provider that failed once is not asked again: whatever it needed
         * is not going to appear in a later phase.
         */
        if (!NT_SUCCESS(Table->InitStatus))
            continue;

        Table->InitStatus = Table->KdpInitRoutine(Table, BootPhase);
    }

    /* One provider refusing is not a reason for the boot to stop */
    return STATUS_SUCCESS;
}

/*
 * Hands one string to everyone listening.
 */
VOID
NTAPI
KdpProvidersPrint(
    _In_reads_bytes_(Length) PCCH String,
    _In_ ULONG Length)
{
    PLIST_ENTRY Entry;

    for (Entry = KdProviders.Flink;
         Entry != &KdProviders;
         Entry = Entry->Flink)
    {
        PKD_DISPATCH_TABLE Table;

        Table = CONTAINING_RECORD(Entry, KD_DISPATCH_TABLE, KdProvidersList);

        if (Table->KdpPrintRoutine != NULL)
            Table->KdpPrintRoutine(String, Length);
    }
}

/*
 * The lock the interactive debugger and the providers share. It is taken with
 * interrupts off and reports whether it was taken at all, so that a caller
 * already inside the debugger does not wait for a processor that is frozen.
 */
BOOLEAN
NTAPI
KdbpAcquireLock(
    _In_ PKSPIN_LOCK SpinLock,
    _Out_ PKIRQL OldIrql)
{
    KIRQL Irql;

    KeRaiseIrql(HIGH_LEVEL, &Irql);
    *OldIrql = Irql;

    /* Already ours: the caller is nested inside its own critical section */
    if (KeTestSpinLock(SpinLock) == FALSE)
    {
        /*
         * Somebody holds it. Inside the debugger that somebody may be frozen,
         * so the lock is reported as not taken and the caller goes on.
         */
        if (KdEnteredDebugger)
            return FALSE;
    }

    KxAcquireSpinLock(SpinLock);
    return TRUE;
}

VOID
NTAPI
KdbpReleaseLock(
    _In_ PKSPIN_LOCK SpinLock,
    _In_ KIRQL OldIrql,
    _In_ BOOLEAN LockAcquired)
{
    if (LockAcquired)
        KxReleaseSpinLock(SpinLock);

    KeLowerIrql(OldIrql);
}

/*
 * Writes one string to every provider, without a format to work out first.
 */
VOID
KdIoPuts(
    _In_ PCSTR String)
{
    SIZE_T Length;

    if (String == NULL)
        return;

    Length = strlen(String);
    if (Length == 0)
        return;

    KdpProvidersPrint(String, (ULONG)Length);
}

/*
 * The same, with a format. The line is built in one buffer, so two callers at
 * once are kept apart.
 */
VOID
__cdecl
KdIoPrintf(
    _In_ PCSTR Format,
    ...)
{
    va_list Arguments;
    KIRQL OldIrql;
    BOOLEAN Locked;
    int Length;

    Locked = KdbpAcquireLock(&KdpIoLock, &OldIrql);

    va_start(Arguments, Format);
    Length = _vsnprintf(KdpIoBuffer, sizeof(KdpIoBuffer) - 1, Format, Arguments);
    va_end(Arguments);

    if ((Length < 0) || (Length > (int)sizeof(KdpIoBuffer) - 1))
        Length = (int)sizeof(KdpIoBuffer) - 1;

    KdpIoBuffer[Length] = ANSI_NULL;

    KdpProvidersPrint(KdpIoBuffer, (ULONG)Length);

    KdbpReleaseLock(&KdpIoLock, OldIrql, Locked);
}

/*
 * The machine is about to bug check. Whatever was configured but left off is
 * turned on now, because this is the last chance anybody has to see anything.
 */
VOID
NTAPI
KdEnableBugCheckDebugger(VOID)
{
    /* The terminal is about to be the only thing left running */
    KdTermCrashMode = TRUE;

    if (!KdPitchDebugger && !KdDebuggerEnabled)
        KdEnableDebuggerWithLock(FALSE);

#ifdef KDBG
    if (KdDebuggerEnabled)
        KdDebuggerNotPresent = FALSE;
#endif

    KdpScreenEnterBugCheck(!KdDebuggerEnabled);
}

BOOLEAN
NTAPI
KdBugCheckDebuggerRequested(VOID)
{
    ULONG Tick;

    if (KdPitchDebugger || !KdDebuggerEnabled || KdDebuggerNotPresent)
        return FALSE;

    DbgPrint("\nPress Ctrl+Alt+B+K within %lu seconds to enter the debugger.\n",
             (ULONG)KDP_BUGCHECK_DEBUGGER_SECONDS);

    for (Tick = 0; Tick < KDP_BUGCHECK_DEBUGGER_SECONDS * KDP_BUGCHECK_POLLS_PER_SECOND; Tick++)
    {
        if (KdpKeyboardChordHeld() || KdpUsbKeyboardChordHeld())
            return TRUE;

        if (KdTermSerial && (KdbpTryGetCharSerial(1) == 0x03))
            return TRUE;

        if (KdpDebugMode.Net && KdpNetPollBreakIn())
            return TRUE;

        KeStallExecutionProcessor(1000000 / KDP_BUGCHECK_POLLS_PER_SECOND);
    }

    return FALSE;
}

/*
 * Whether the system should hold the screen at shutdown so that the log can be
 * read. Only worth it when the screen is where the log went.
 */
BOOLEAN
NTAPI
KdShowShutdownLog(VOID)
{
    return (KdpDebugMode.Screen && KdDebuggerEnabled);
}

BOOLEAN
NTAPI
KdScreenIsBootLog(VOID)
{
    return KdpDebugMode.Screen;
}

/* EOF */
