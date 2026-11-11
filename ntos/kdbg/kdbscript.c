/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Commands that run other commands, the transcript, and the debugger's own checks
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#include "kdb.h"
#include "kdbpriv.h"
#define NDEBUG
#include <debug.h>

#define KDB_MAXIMUM_ALIASES 32
#define KDB_ALIAS_NAME_MAX 32
#define KDB_ALIAS_COMMAND_MAX 512
#define KDB_TRANSCRIPT_SIZE (64 * 1024)
#define KDB_SCRIPT_LINE_MAX 1024
#define KDB_MAXIMUM_SCRIPT_SIZE (64 * 1024)
#define KDB_MAXIMUM_REPEAT 10000

typedef struct _KDB_ALIAS
{
    CHAR Name[KDB_ALIAS_NAME_MAX];
    CHAR Command[KDB_ALIAS_COMMAND_MAX];
} KDB_ALIAS, *PKDB_ALIAS;

/* GLOBALS ********************************************************************/

static KDB_ALIAS KdbpAliases[KDB_MAXIMUM_ALIASES];
static BOOLEAN KdbpTranscriptEnabled;
static CHAR KdbpTranscript[KDB_TRANSCRIPT_SIZE];
static ULONG KdbpTranscriptWrite;
static ULONG KdbpTranscriptLength;

/* PRIVATE FUNCTIONS **********************************************************/

static
BOOLEAN
KdbpJoinWords(
    _In_ ULONG First,
    _In_ ULONG Argc,
    _In_ PCHAR Argv[],
    _Out_writes_(Length) PCHAR Buffer,
    _In_ ULONG Length)
{
    ULONG Position = 0;
    ULONG Index;

    Buffer[0] = ANSI_NULL;

    for (Index = First; Index < Argc; Index++)
    {
        SIZE_T Word = strlen(Argv[Index]);

        if (Position + (Position != 0) + Word >= Length)
        {
            Buffer[0] = ANSI_NULL;
            return FALSE;
        }

        if (Position != 0)
            Buffer[Position++] = ' ';

        RtlCopyMemory(&Buffer[Position], Argv[Index], Word);
        Position += (ULONG)Word;
        Buffer[Position] = ANSI_NULL;
    }

    return TRUE;
}

static
PKDB_ALIAS
KdbpFindAlias(
    _In_ PCSTR Name)
{
    ULONG Index;

    for (Index = 0; Index < KDB_MAXIMUM_ALIASES; Index++)
    {
        if ((KdbpAliases[Index].Name[0] != ANSI_NULL) && (_stricmp(KdbpAliases[Index].Name, Name) == 0))
            return &KdbpAliases[Index];
    }

    return NULL;
}

static
BOOLEAN
KdbpRunScriptLine(
    _Inout_ PCHAR Line)
{
    while ((*Line == ' ') || (*Line == '\t'))
        Line++;

    if ((*Line == ANSI_NULL) || (*Line == '#'))
        return FALSE;

    return KdbpCliInterpretCommand(Line);
}

/* PUBLIC FUNCTIONS ***********************************************************/

VOID
KdbpTranscriptAppend(
    _In_reads_(Length) PCCH String,
    _In_ ULONG Length)
{
    ULONG Index;

    if (!KdbpTranscriptEnabled)
        return;

    for (Index = 0; Index < Length; Index++)
    {
        KdbpTranscript[KdbpTranscriptWrite] = String[Index];
        KdbpTranscriptWrite = (KdbpTranscriptWrite + 1) % KDB_TRANSCRIPT_SIZE;

        if (KdbpTranscriptLength < KDB_TRANSCRIPT_SIZE)
            KdbpTranscriptLength++;
    }
}

VOID
KdbpTranscriptCommand(
    _In_ PCSTR Command)
{
    static const CHAR Prompt[] = "kdb:> ";

    KdbpTranscriptAppend(Prompt, sizeof(Prompt) - 1);
    KdbpTranscriptAppend(Command, (ULONG)strlen(Command));
    KdbpTranscriptAppend("\n", 1);
}

BOOLEAN
KdbpExpandAlias(
    _In_ ULONG Argc,
    _In_ PCHAR Argv[],
    _Out_writes_(Length) PCHAR Buffer,
    _In_ ULONG Length)
{
    PKDB_ALIAS Alias = KdbpFindAlias(Argv[0]);
    SIZE_T Used;

    if (Alias == NULL)
        return FALSE;

    Used = strlen(Alias->Command);
    if (Used >= Length)
        return FALSE;

    RtlCopyMemory(Buffer, Alias->Command, Used + 1);

    if ((Argc > 1) &&
        ((Used + 1 >= Length) ||
         !KdbpJoinWords(1, Argc, Argv, &Buffer[Used + 1], Length - (ULONG)Used - 1)))
    {
        KdbpPrint("%s is too long once %s is replaced\n", Argv[0], Argv[0]);
        Buffer[0] = ANSI_NULL;
        return TRUE;
    }

    if (Argc > 1)
        Buffer[Used] = ' ';

    return TRUE;
}

BOOLEAN
KdbpCmdVersion(
    ULONG Argc,
    PCHAR Argv[])
{
    UNREFERENCED_PARAMETER(Argc);
    UNREFERENCED_PARAMETER(Argv);

    KdbpPrint("LiberNT %lu.%lu build %lu, kernel at %p, %u processors, debugger on processor %lu\n",
              NtMajorVersion,
              NtMinorVersion,
              NtBuildNumber & 0xFFFF,
              (PVOID)(ULONG_PTR)KdVersionBlock.KernBase,
              (ULONG)(UCHAR)KeNumberProcessors,
              KeGetCurrentProcessorNumber());
    KdbpPrint("KD %u.%u, protocol %u, secondary %u, machine 0x%04x, flags 0x%04x, module list %p\n",
              KdVersionBlock.MajorVersion,
              KdVersionBlock.MinorVersion,
              KdVersionBlock.ProtocolVersion,
              KdVersionBlock.KdSecondaryVersion,
              KdVersionBlock.MachineType,
              KdVersionBlock.Flags,
              (PVOID)(ULONG_PTR)KdVersionBlock.PsLoadedModuleList);

    return TRUE;
}

BOOLEAN
KdbpCmdEcho(
    ULONG Argc,
    PCHAR Argv[])
{
    CHAR Text[KDB_SCRIPT_LINE_MAX];

    if (!KdbpJoinWords(1, Argc, Argv, Text, sizeof(Text)))
    {
        KdbPuts("The text is too long\n");
        return TRUE;
    }

    KdbpPrint("%s\n", Text);
    return TRUE;
}

BOOLEAN
KdbpCmdAlias(
    ULONG Argc,
    PCHAR Argv[])
{
    PKDB_ALIAS Alias;
    ULONG Index;

    if (Argc == 1)
    {
        for (Index = 0; Index < KDB_MAXIMUM_ALIASES; Index++)
        {
            if (KdbpAliases[Index].Name[0] != ANSI_NULL)
                KdbpPrint("%s = %s\n", KdbpAliases[Index].Name, KdbpAliases[Index].Command);
        }

        return TRUE;
    }

    Alias = KdbpFindAlias(Argv[1]);

    if (Argc == 2)
    {
        if (Alias == NULL)
            KdbpPrint("%s is not an alias\n", Argv[1]);
        else
            KdbpPrint("%s = %s\n", Alias->Name, Alias->Command);

        return TRUE;
    }

    if (strlen(Argv[1]) >= KDB_ALIAS_NAME_MAX)
    {
        KdbpPrint("An alias name has at most %u characters\n", KDB_ALIAS_NAME_MAX - 1);
        return TRUE;
    }

    if (KdbpIsCommandName(Argv[1]))
    {
        KdbpPrint("%s is a command of the debugger and cannot be an alias\n", Argv[1]);
        return TRUE;
    }

    for (Index = 0; (Alias == NULL) && (Index < KDB_MAXIMUM_ALIASES); Index++)
    {
        if (KdbpAliases[Index].Name[0] == ANSI_NULL)
            Alias = &KdbpAliases[Index];
    }

    if (Alias == NULL)
    {
        KdbpPrint("All %u aliases are in use\n", KDB_MAXIMUM_ALIASES);
        return TRUE;
    }

    if (!KdbpJoinWords(2, Argc, Argv, Alias->Command, sizeof(Alias->Command)))
    {
        KdbpPrint("What an alias stands for has at most %u characters\n", KDB_ALIAS_COMMAND_MAX - 1);
        Alias->Name[0] = ANSI_NULL;
        return TRUE;
    }

    RtlStringCbCopyA(Alias->Name, sizeof(Alias->Name), Argv[1]);
    KdbpPrint("%s = %s\n", Alias->Name, Alias->Command);
    return TRUE;
}

BOOLEAN
KdbpCmdUnalias(
    ULONG Argc,
    PCHAR Argv[])
{
    PKDB_ALIAS Alias;

    if (Argc < 2)
    {
        KdbPuts("An alias name is needed\n");
        return TRUE;
    }

    Alias = KdbpFindAlias(Argv[1]);
    if (Alias == NULL)
    {
        KdbpPrint("%s is not an alias\n", Argv[1]);
        return TRUE;
    }

    RtlZeroMemory(Alias, sizeof(*Alias));
    KdbpPrint("%s is no longer an alias\n", Argv[1]);
    return TRUE;
}

BOOLEAN
KdbpCmdTranscript(
    ULONG Argc,
    PCHAR Argv[])
{
    BOOLEAN Enabled = KdbpTranscriptEnabled;
    ULONG Start;
    ULONG First;

    if (Argc < 2)
    {
        KdbpPrint("The transcript is %s and holds %lu of %u bytes\n",
                  KdbpTranscriptEnabled ? "on" : "off",
                  KdbpTranscriptLength,
                  KDB_TRANSCRIPT_SIZE);
        return TRUE;
    }

    if (_stricmp(Argv[1], "on") == 0)
    {
        KdbpTranscriptEnabled = TRUE;
        KdbPuts("The transcript is on\n");
    }
    else if (_stricmp(Argv[1], "off") == 0)
    {
        KdbPuts("The transcript is off\n");
        KdbpTranscriptEnabled = FALSE;
    }
    else if (_stricmp(Argv[1], "clear") == 0)
    {
        KdbpTranscriptEnabled = FALSE;
        KdbpTranscriptWrite = 0;
        KdbpTranscriptLength = 0;
        KdbPuts("The transcript is empty\n");
        KdbpTranscriptEnabled = Enabled;
    }
    else if (_stricmp(Argv[1], "show") == 0)
    {
        KdbpTranscriptEnabled = FALSE;
        Start = (KdbpTranscriptWrite + KDB_TRANSCRIPT_SIZE - KdbpTranscriptLength) % KDB_TRANSCRIPT_SIZE;
        First = min(KdbpTranscriptLength, KDB_TRANSCRIPT_SIZE - Start);

        if (First != 0)
            KdbpPager(&KdbpTranscript[Start], First);
        if (KdbpTranscriptLength > First)
            KdbpPager(KdbpTranscript, KdbpTranscriptLength - First);

        KdbpTranscriptEnabled = Enabled;
    }
    else
    {
        KdbpPrint("%s is not on, off, clear or show\n", Argv[1]);
    }

    return TRUE;
}

BOOLEAN
KdbpCmdScript(
    ULONG Argc,
    PCHAR Argv[])
{
    static CHAR Line[KDB_SCRIPT_LINE_MAX];
    ULONG_PTR Address;
    ULONG_PTR Length;
    ULONG_PTR Offset;
    ULONG Position = 0;
    CHAR Char;

    if ((Argc < 3) ||
        !KdbpGetAddressExpression(Argv[1], &Address) ||
        !KdbpGetAddressExpression(Argv[2], &Length))
    {
        KdbPuts("The address and the length of the commands are needed\n");
        return TRUE;
    }

    if ((Length == 0) || (Length > KDB_MAXIMUM_SCRIPT_SIZE))
    {
        KdbpPrint("A script holds from 1 to %u bytes\n", KDB_MAXIMUM_SCRIPT_SIZE);
        return TRUE;
    }

    for (Offset = 0; Offset <= Length; Offset++)
    {
        if (Offset == Length)
        {
            Char = ANSI_NULL;
        }
        else if (!NT_SUCCESS(KdbpSafeReadMemory(&Char, (PVOID)(Address + Offset), sizeof(Char))))
        {
            KdbpPrint("The script cannot be read at %p\n", (PVOID)(Address + Offset));
            return TRUE;
        }

        if ((Char != '\r') && (Char != '\n') && (Char != ANSI_NULL))
        {
            if (Position + 1 >= sizeof(Line))
            {
                KdbpPrint("The line at offset 0x%Ix is longer than %u characters\n",
                          Offset - Position,
                          KDB_SCRIPT_LINE_MAX - 1);
                return TRUE;
            }

            Line[Position++] = Char;
            continue;
        }

        Line[Position] = ANSI_NULL;
        Position = 0;

        if (KdbpRunScriptLine(Line))
            return TRUE;
    }

    return TRUE;
}

BOOLEAN
KdbpCmdRepeat(
    ULONG Argc,
    PCHAR Argv[])
{
    static CHAR Command[KDB_SCRIPT_LINE_MAX];
    static CHAR Run[KDB_SCRIPT_LINE_MAX];
    ULONG_PTR Count;
    ULONG_PTR Index;

    if ((Argc < 3) || !KdbpGetAddressExpression(Argv[1], &Count))
    {
        KdbPuts("A count and a command are needed\n");
        return TRUE;
    }

    if ((Count == 0) || (Count > KDB_MAXIMUM_REPEAT))
    {
        KdbpPrint("A command is repeated from 1 to %u times\n", KDB_MAXIMUM_REPEAT);
        return TRUE;
    }

    if (!KdbpJoinWords(2, Argc, Argv, Command, sizeof(Command)))
    {
        KdbPuts("The command is too long\n");
        return TRUE;
    }

    for (Index = 0; (Index < Count) && !KdbpIsOutputAborted(); Index++)
    {
        RtlCopyMemory(Run, Command, sizeof(Run));

        if (KdbpCliInterpretCommand(Run))
            break;
    }

    return TRUE;
}

BOOLEAN
KdbpCmdSelfTest(
    ULONG Argc,
    PCHAR Argv[])
{
    static const ULONG Sentinel = 0x4B444254;
    CHAR ErrorMessage[128];
    ULONGLONG Result = 0;
    LONG ErrorOffset = 0;
    ULONG_PTR Number = 0;
    ULONG ReadBack = 0;
    ULONG Failures;
    ULONG Index;

    UNREFERENCED_PARAMETER(Argc);
    UNREFERENCED_PARAMETER(Argv);

    Failures = KdbpCheckCommandTable();

    if (!NT_SUCCESS(KdbpSafeReadMemory(&ReadBack, (PVOID)&Sentinel, sizeof(ReadBack))) ||
        (ReadBack != Sentinel))
    {
        KdbPuts("selftest: a guarded read of readable memory failed\n");
        Failures++;
    }

    if (NT_SUCCESS(KdbpSafeReadMemory(&ReadBack, (PVOID)(ULONG_PTR)1, sizeof(ReadBack))))
    {
        KdbPuts("selftest: a guarded read of address 1 succeeded\n");
        Failures++;
    }

    if (!KdbpGetHexNumber("1234", &Number) || (Number != 0x1234) ||
        !KdbpGetHexNumber("0xfeed", &Number) || (Number != 0xFEED) ||
        KdbpGetHexNumber("1234x", &Number) ||
        KdbpGetHexNumber("", &Number) ||
        KdbpGetHexNumber("1000000000000000000", &Number))
    {
        KdbPuts("selftest: the hexadecimal parser accepted or refused the wrong numbers\n");
        Failures++;
    }

    if (!KdbpRpnEvaluateExpression("1+2*3", KdbCurrentTrapFrame, &Result, &ErrorOffset, ErrorMessage) ||
        (Result != 7) ||
        KdbpRpnEvaluateExpression("0x10000000000000000", KdbCurrentTrapFrame, &Result, &ErrorOffset, ErrorMessage))
    {
        KdbPuts("selftest: the expression evaluator got 1+2*3 or a number too large wrong\n");
        Failures++;
    }

    for (Index = 0; Index < KdbpFloatingRegisterCount; Index++)
    {
        if ((KdbpFloatingRegisters[Index].Size == 0) ||
            (KdbpFloatingRegisters[Index].Size > 16) ||
            ((ULONG)KdbpFloatingRegisters[Index].Offset + KdbpFloatingRegisters[Index].Size > sizeof(CONTEXT)))
        {
            KdbpPrint("selftest: the floating-point register %s is outside the context\n",
                      KdbpFloatingRegisters[Index].Name);
            Failures++;
        }
    }

    if (Failures == 0)
        KdbPuts("selftest: guarded reads, parsers, command and register tables are right\n");
    else
        KdbpPrint("selftest: %lu checks failed\n", Failures);

    return TRUE;
}

/* EOF */
