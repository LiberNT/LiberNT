/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     What the interactive debugger says, and what it reads back
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#include "kdb.h"
#define NDEBUG
#include <debug.h>

/* PRIVATE DATA ***************************************************************/

/* The line being built. One at a time: the debugger has one voice */
static CHAR KdbpFormatBuffer[1024];

/* PUBLIC FUNCTIONS ***********************************************************/

/*
 * Everything the debugger prints goes through here: it is written to the log
 * so a crash dump still has it, and then handed to whoever is deciding how
 * much of it fits on a screen.
 */
VOID
KdbPutsN(
    _In_ PCCH String,
    _In_ USHORT Length)
{
    STRING Output;

    if ((String == NULL) || (Length == 0))
        return;

    Output.Buffer = (PCHAR)String;
    Output.Length = Length;
    Output.MaximumLength = Length;

    KdLogDbgPrint(&Output);
    KdbpCaptureOutput(String, Length);
}

VOID
KdbPuts(
    _In_ PCSTR String)
{
    SIZE_T Length;

    if (String == NULL)
        return;

    Length = strlen(String);
    if (Length > MAXUSHORT)
        Length = MAXUSHORT;

    KdbPutsN(String, (USHORT)Length);
}

VOID
__cdecl
KdbPrintf(
    _In_ PCSTR Format,
    ...)
{
    va_list Arguments;
    int Length;

    va_start(Arguments, Format);
    Length = _vsnprintf(KdbpFormatBuffer,
                        sizeof(KdbpFormatBuffer) - 1,
                        Format,
                        Arguments);
    va_end(Arguments);

    /* What did not fit is said as far as it goes rather than not at all */
    if ((Length < 0) || (Length > (int)sizeof(KdbpFormatBuffer) - 1))
        Length = (int)sizeof(KdbpFormatBuffer) - 1;

    KdbpFormatBuffer[Length] = ANSI_NULL;

    KdbPutsN(KdbpFormatBuffer, (USHORT)Length);
}

VOID
KdbPrintString(
    _In_ const CSTRING* Output)
{
    if ((Output == NULL) || (Output->Buffer == NULL))
        return;

    KdbPutsN(Output->Buffer, Output->Length);
}

/*
 * Asks a question and reads the answer. Returns how much was typed.
 */
SIZE_T
KdbPrompt(
    _In_ PCSTR Prompt,
    _Out_ PCHAR Buffer,
    _In_ SIZE_T Size)
{
    if ((Buffer == NULL) || (Size == 0))
        return 0;

    if (Prompt != NULL)
        KdbPuts(Prompt);

    return KdIoReadLine(Buffer, Size);
}

USHORT
KdbPromptString(
    _In_ const CSTRING* PromptString,
    _Inout_ PSTRING ResponseString)
{
    SIZE_T Length;

    if ((ResponseString == NULL) || (ResponseString->Buffer == NULL))
        return 0;

    if ((PromptString != NULL) && (PromptString->Buffer != NULL))
        KdbPutsN(PromptString->Buffer, PromptString->Length);

    Length = KdIoReadLine(ResponseString->Buffer,
                          ResponseString->MaximumLength);

    ResponseString->Length = (USHORT)Length;
    return (USHORT)Length;
}

/* EOF */
