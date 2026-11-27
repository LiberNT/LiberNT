/*
 * PROJECT:     LiberNT host-native tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     What RtlAssert does with each answer to its prompt, and with no answer
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <setjmp.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "assert/rtl.h"

#define PROMPT_LIMIT 8

static unsigned long Checks, Failures;

#define CHECK(c) do { Checks++; if (!(c)) { Failures++; fprintf(stderr, "FAIL %s:%d %s\n", __FILE__, __LINE__, #c); } } while (0)

static const char *Script;
static ULONG GlobalFlags;
static BOOLEAN DebuggerEnabled = TRUE;
static ULONG Reports, Prompts, Breaks, ContextHints, ProcessEnds, ThreadEnds;
static ULONG PromptBufferSize;
static NTSTATUS Raised;
static char LastReport[256];
static jmp_buf Escape;

enum
{
    ESCAPE_RAISED = 1,
    ESCAPE_ASKED_FOREVER
};

static ULONG RecordPrint(const char *Format, va_list Arguments)
{
    if (strncmp(Format, "\n*** Assertion failed", 21) == 0)
    {
        Reports++;
        vsnprintf(LastReport, sizeof(LastReport), Format, Arguments);
    }
    else if (strncmp(Format, "Execute '.cxr", 13) == 0)
    {
        ContextHints++;
    }
    return 0;
}

ULONG DbgPrint(const char *Format, ...)
{
    va_list Arguments;

    va_start(Arguments, Format);
    RecordPrint(Format, Arguments);
    va_end(Arguments);
    return 0;
}

ULONG DbgPrintEx(ULONG ComponentId, ULONG Level, const char *Format, ...)
{
    va_list Arguments;

    (void)ComponentId;
    (void)Level;
    va_start(Arguments, Format);
    RecordPrint(Format, Arguments);
    va_end(Arguments);
    return 0;
}

BOOLEAN RtlpKernelDebuggerEnabled(VOID)
{
    return DebuggerEnabled;
}

ULONG DbgPrompt(PCCH Prompt, PCH Response, ULONG MaximumResponseLength)
{
    CHECK(Prompt != NULL);
    PromptBufferSize = MaximumResponseLength;

    Prompts++;
    if (Prompts > PROMPT_LIMIT)
        longjmp(Escape, ESCAPE_ASKED_FOREVER);

    /* A script that ran out is a prompt nobody answers: the response is left as it was */
    if (*Script == '\0')
        return 0;

    Response[0] = *Script++;
    Response[1] = '\n';
    return 2;
}

VOID DbgBreakPoint(VOID)
{
    Breaks++;
}

VOID RtlCaptureContext(PCONTEXT Context)
{
    Context->ContextFlags = 0;
}

ULONG RtlGetNtGlobalFlags(VOID)
{
    return GlobalFlags;
}

VOID RtlRaiseStatus(NTSTATUS Status)
{
    Raised = Status;
    longjmp(Escape, ESCAPE_RAISED);
}

NTSTATUS ZwTerminateProcess(HANDLE Process, NTSTATUS ExitStatus)
{
    CHECK(Process == ZwCurrentProcess());
    CHECK(ExitStatus == STATUS_UNSUCCESSFUL);
    ProcessEnds++;
    return STATUS_UNSUCCESSFUL;
}

NTSTATUS ZwTerminateThread(HANDLE Thread, NTSTATUS ExitStatus)
{
    CHECK(Thread == ZwCurrentThread());
    CHECK(ExitStatus == STATUS_UNSUCCESSFUL);
    ThreadEnds++;
    return STATUS_UNSUCCESSFUL;
}

static int Fail(const char *Answers, ULONG Flags, char *Message)
{
    int Escaped;

    Script = Answers;
    GlobalFlags = Flags;
    Reports = Prompts = Breaks = ContextHints = ProcessEnds = ThreadEnds = 0;
    PromptBufferSize = 0;
    Raised = 0;
    LastReport[0] = '\0';

    Escaped = setjmp(Escape);
    if (Escaped == 0)
        RtlAssert("Count == 0", "driver.c", 42, Message);

    return Escaped;
}

int main(void)
{
    /* Nobody answers: one report, ignored without breaking, and the caller gets control back */
    CHECK(Fail("", 0, NULL) == 0);
    CHECK(Reports == 1);
    CHECK(Prompts == 1);
    CHECK(Breaks == 0);
    CHECK(ContextHints == 0);
    CHECK(PromptBufferSize == 2);
    CHECK(strcmp(LastReport, "\n*** Assertion failed: Count == 0\n***   Source File: driver.c, line 42\n\n") == 0);

    CHECK(Fail("", 0, "Slot in use: ") == 0);
    CHECK(strcmp(LastReport, "\n*** Assertion failed: Slot in use: Count == 0\n***   Source File: driver.c, line 42\n\n") == 0);

    /* Ignore */
    CHECK(Fail("i", 0, NULL) == 0);
    CHECK(Reports == 1);
    CHECK(Prompts == 1);
    CHECK(Breaks == 0);

    CHECK(Fail("I", 0, NULL) == 0);
    CHECK(Prompts == 1);
    CHECK(Breaks == 0);

    /* Break once */
    CHECK(Fail("o", 0, NULL) == 0);
    CHECK(Reports == 1);
    CHECK(Prompts == 1);
    CHECK(Breaks == 1);
    CHECK(ContextHints == 1);

    /* Break repeatedly asks again after each stop */
    CHECK(Fail("bBi", 0, NULL) == 0);
    CHECK(Reports == 3);
    CHECK(Prompts == 3);
    CHECK(Breaks == 2);
    CHECK(ContextHints == 2);

    /* An answer that means nothing asks again */
    CHECK(Fail("x?i", 0, NULL) == 0);
    CHECK(Reports == 3);
    CHECK(Prompts == 3);
    CHECK(Breaks == 0);

    /* An answer that means nothing, then nobody: ignored without breaking */
    CHECK(Fail("x", 0, NULL) == 0);
    CHECK(Prompts == 2);
    CHECK(Breaks == 0);

    /* Terminations that come back ask again */
    CHECK(Fail("pi", 0, NULL) == 0);
    CHECK(ProcessEnds == 1);
    CHECK(ThreadEnds == 0);
    CHECK(Prompts == 2);
    CHECK(Breaks == 0);

    CHECK(Fail("Ti", 0, NULL) == 0);
    CHECK(ProcessEnds == 0);
    CHECK(ThreadEnds == 1);
    CHECK(Prompts == 2);
    CHECK(Breaks == 0);

    /* Prompts disabled: reported, raised, never asked */
    CHECK(Fail("i", FLG_DISABLE_DEBUG_PROMPTS, NULL) == ESCAPE_RAISED);
    CHECK(Raised == STATUS_ASSERTION_FAILURE);
    CHECK(Reports == 1);
    CHECK(Prompts == 0);
    CHECK(Breaks == 0);

    /* No kernel debugger attached: reported, never prompted, returns without breaking */
    DebuggerEnabled = FALSE;
    CHECK(Fail("o", 0, NULL) == 0);
    CHECK(Reports == 1);
    CHECK(Prompts == 0);
    CHECK(Breaks == 0);
    DebuggerEnabled = TRUE;

    printf("rtl_assert: %lu checks, %lu failures\n", Checks, Failures);
    return Failures != 0;
}
