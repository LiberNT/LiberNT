/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests for the status DbgPrint and DbgPrintEx return without a debugger
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"
#include <dpfilter.h>

NTSYSAPI
ULONG
NTAPI
vDbgPrintExWithPrefix(
    _In_z_ PCCH Prefix,
    _In_ ULONG ComponentId,
    _In_ ULONG Level,
    _In_z_ PCCH Format,
    _In_ va_list ap);

#ifndef DBG_PRINTEXCEPTION_C
#define DBG_PRINTEXCEPTION_C 0x40010006
#endif

static
LONG
NTAPI
SwallowDbgPrintException(
    _In_ PEXCEPTION_POINTERS Pointers)
{
    if (Pointers->ExceptionRecord->ExceptionCode == DBG_PRINTEXCEPTION_C)
        return EXCEPTION_CONTINUE_EXECUTION;
    return EXCEPTION_CONTINUE_SEARCH;
}

static
NTSTATUS
CallVDbgPrintExWithPrefix(
    _In_ PCCH Prefix,
    _In_ ULONG ComponentId,
    _In_ ULONG Level,
    _In_ PCCH Format,
    ...)
{
    NTSTATUS Status;
    va_list Arguments;

    va_start(Arguments, Format);
    Status = vDbgPrintExWithPrefix(Prefix, ComponentId, Level, Format, Arguments);
    va_end(Arguments);
    return Status;
}

START_TEST(DbgPrintEx)
{
    PVOID Handler;
    NTSTATUS Status;
    ULONG Index;

    if (NtCurrentPeb()->BeingDebugged)
    {
        skip("A debugger is attached\n");
        return;
    }

    Handler = RtlAddVectoredExceptionHandler(TRUE, SwallowDbgPrintException);
    ok(Handler != NULL, "RtlAddVectoredExceptionHandler failed\n");

    Status = DbgPrint("DbgPrintEx apitest: %s\n", "DbgPrint");
    ok_ntstatus(Status, STATUS_SUCCESS);
    Status = DbgPrintEx(DPFLTR_DEFAULT_ID, DPFLTR_ERROR_LEVEL, "DbgPrintEx apitest: %s\n", "error");
    ok_ntstatus(Status, STATUS_SUCCESS);
    Status = DbgPrintEx(DPFLTR_DEFAULT_ID, DPFLTR_WARNING_LEVEL, "DbgPrintEx apitest: %s\n", "warning");
    ok_ntstatus(Status, STATUS_SUCCESS);
    Status = DbgPrintEx(DPFLTR_DEFAULT_ID, DPFLTR_TRACE_LEVEL, "DbgPrintEx apitest: %s\n", "trace");
    ok_ntstatus(Status, STATUS_SUCCESS);
    Status = DbgPrintEx(DPFLTR_DEFAULT_ID, DPFLTR_MASK | (1 << DPFLTR_INFO_LEVEL), "DbgPrintEx apitest: %s\n", "mask");
    ok_ntstatus(Status, STATUS_SUCCESS);
    Status = CallVDbgPrintExWithPrefix("[prefix] ", DPFLTR_DEFAULT_ID, DPFLTR_ERROR_LEVEL, "DbgPrintEx apitest: %s\n", "prefix");
    ok_ntstatus(Status, STATUS_SUCCESS);

    for (Index = 0; Index < 1000; Index++)
    {
        Status = DbgPrintEx(DPFLTR_DEFAULT_ID, DPFLTR_TRACE_LEVEL, "DbgPrintEx apitest: %lu\n", Index);
        if (Status != STATUS_SUCCESS)
            break;
    }
    ok_ntstatus(Status, STATUS_SUCCESS);

    if (Handler)
        RtlRemoveVectoredExceptionHandler(Handler);
}
