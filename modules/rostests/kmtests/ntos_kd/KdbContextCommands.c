/*
 * PROJECT:     LiberNT kernel-mode tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Stops in the debugger with a context record and an exception record to look at
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <kmt_test.h>
#include <ndk/rtlfuncs.h>

CONTEXT KdbTestContextRecord;
EXCEPTION_RECORD KdbTestExceptionRecord;

static
DECLSPEC_NOINLINE
VOID
KdbTestCaptureSite(VOID)
{
    RtlCaptureContext(&KdbTestContextRecord);
}

START_TEST(KdbContextCommands)
{
    KdbTestCaptureSite();

    RtlZeroMemory(&KdbTestExceptionRecord, sizeof(KdbTestExceptionRecord));
    KdbTestExceptionRecord.ExceptionCode = STATUS_ACCESS_VIOLATION;
    KdbTestExceptionRecord.ExceptionAddress = (PVOID)KdbTestCaptureSite;
    KdbTestExceptionRecord.NumberParameters = 2;
    KdbTestExceptionRecord.ExceptionInformation[0] = 1;
    KdbTestExceptionRecord.ExceptionInformation[1] = 0x1234;

    /* The debugger's start-up file looks at both records here */
    DbgBreakPoint();
}
