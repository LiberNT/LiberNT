/*
 * PROJECT:     LiberNT Run-Time Library
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     What a failed assertion does
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <rtl.h>
#define NDEBUG
#include <debug.h>

typedef enum _RTLP_ASSERT_ANSWER
{
    RtlpAssertUnanswered,
    RtlpAssertAskAgain,
    RtlpAssertBreakAndAsk,
    RtlpAssertBreakOnce,
    RtlpAssertIgnore,
    RtlpAssertEndProcess,
    RtlpAssertEndThread
} RTLP_ASSERT_ANSWER;

static
RTLP_ASSERT_ANSWER
RtlpAskAboutAssertion(VOID)
{
    CHAR Response[2];
    ULONG Received;

    Received = DbgPrompt("Break repeatedly, break Once, Ignore, "
                         "terminate Process or terminate Thread (boipt)? ",
                         Response,
                         sizeof(Response));
    if (Received == 0)
        return RtlpAssertUnanswered;

    switch (Response[0])
    {
        case 'B':
        case 'b':
            return RtlpAssertBreakAndAsk;

        case 'O':
        case 'o':
            return RtlpAssertBreakOnce;

        case 'I':
        case 'i':
            return RtlpAssertIgnore;

        case 'P':
        case 'p':
            return RtlpAssertEndProcess;

        case 'T':
        case 't':
            return RtlpAssertEndThread;

        default:
            return RtlpAssertAskAgain;
    }
}

VOID
NTAPI
RtlAssert(
    _In_ PVOID FailedAssertion,
    _In_ PVOID FileName,
    _In_ ULONG LineNumber,
    _In_opt_ PCHAR Message)
{
    CONTEXT Context;
    RTLP_ASSERT_ANSWER Answer;

    RtlCaptureContext(&Context);

    do
    {
        DbgPrint("\n*** Assertion failed: %s%s\n"
                 "***   Source File: %s, line %lu\n\n",
                 (Message != NULL) ? Message : "",
                 (PSTR)FailedAssertion,
                 (PSTR)FileName,
                 LineNumber);

        if (RtlGetNtGlobalFlags() & FLG_DISABLE_DEBUG_PROMPTS)
            RtlRaiseStatus(STATUS_ASSERTION_FAILURE);

        Answer = RtlpAskAboutAssertion();
        switch (Answer)
        {
            case RtlpAssertUnanswered:
                /* Nobody can answer: with no debugger this breakpoint is the stop documented for an assertion (bug check 0x1E, STATUS_BREAKPOINT) */
                DbgBreakPoint();
                break;

            case RtlpAssertBreakAndAsk:
            case RtlpAssertBreakOnce:
                DbgPrint("Execute '.cxr %p' to dump context\n", &Context);
                DbgBreakPoint();
                break;

            case RtlpAssertEndProcess:
                ZwTerminateProcess(ZwCurrentProcess(), STATUS_UNSUCCESSFUL);
                break;

            case RtlpAssertEndThread:
                ZwTerminateThread(ZwCurrentThread(), STATUS_UNSUCCESSFUL);
                break;

            default:
                break;
        }
    } while ((Answer != RtlpAssertUnanswered) &&
             (Answer != RtlpAssertBreakOnce) &&
             (Answer != RtlpAssertIgnore));
}
