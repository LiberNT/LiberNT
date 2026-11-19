/*
 * PROJECT:     LiberNT kernel-mode tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Callbacks a keyboard filter hooks into the i8042 port
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <kmt_test.h>
#include <ntdd8042.h>
#include <hook_core.h>

typedef struct _HOOK_FILTER
{
    NTSTATUS Status;
    BOOLEAN Write;
    BOOLEAN Value;
    ULONG Calls;
    PVOID InitializationContext;
    PVOID SynchFuncContext;
    PI8042_SYNCH_READ_PORT ReadPort;
    PI8042_SYNCH_WRITE_PORT WritePort;
    PBOOLEAN TurnTranslationOn;
    BOOLEAN Received;
} HOOK_FILTER, *PHOOK_FILTER;

static
NTSTATUS
NTAPI
TestReadPort(
    _In_ PVOID Context,
    _Out_ PUCHAR Value,
    _In_ BOOLEAN WaitForAck)
{
    UNREFERENCED_PARAMETER(Context);
    UNREFERENCED_PARAMETER(WaitForAck);

    *Value = 0;
    return STATUS_SUCCESS;
}

static
NTSTATUS
NTAPI
TestWritePort(
    _In_ PVOID Context,
    _In_ UCHAR Value,
    _In_ BOOLEAN WaitForAck)
{
    UNREFERENCED_PARAMETER(Context);
    UNREFERENCED_PARAMETER(Value);
    UNREFERENCED_PARAMETER(WaitForAck);

    return STATUS_SUCCESS;
}

static
NTSTATUS
NTAPI
TestInitializationRoutine(
    _In_ PVOID InitializationContext,
    _In_ PVOID SynchFuncContext,
    _In_ PI8042_SYNCH_READ_PORT ReadPort,
    _In_ PI8042_SYNCH_WRITE_PORT WritePort,
    _Out_ PBOOLEAN TurnTranslationOn)
{
    PHOOK_FILTER Filter = InitializationContext;

    Filter->Calls++;
    Filter->InitializationContext = InitializationContext;
    Filter->SynchFuncContext = SynchFuncContext;
    Filter->ReadPort = ReadPort;
    Filter->WritePort = WritePort;
    Filter->TurnTranslationOn = TurnTranslationOn;
    if (!TurnTranslationOn)
        return STATUS_ACCESS_VIOLATION;

    Filter->Received = *TurnTranslationOn;
    if (Filter->Write)
        *TurnTranslationOn = Filter->Value;
    return Filter->Status;
}

static
VOID
TestFilter(
    _In_ NTSTATUS FilterStatus,
    _In_ BOOLEAN Write,
    _In_ BOOLEAN Value,
    _In_ BOOLEAN ExpectedTranslation)
{
    HOOK_FILTER Filter;
    ULONG SynchContext = 0;
    BOOLEAN TurnTranslationOn = 0x55;
    NTSTATUS Status;

    RtlZeroMemory(&Filter, sizeof(Filter));
    Filter.Status = FilterStatus;
    Filter.Write = Write;
    Filter.Value = Value;

    Status = I8042CoreKeyboardInitialization(TestInitializationRoutine,
                                             &Filter,
                                             &SynchContext,
                                             TestReadPort,
                                             TestWritePort,
                                             &TurnTranslationOn);

    ok_eq_hex(Status, FilterStatus);
    ok_eq_ulong(Filter.Calls, 1UL);
    ok_eq_pointer(Filter.InitializationContext, &Filter);
    ok_eq_pointer(Filter.SynchFuncContext, &SynchContext);
    ok_eq_pointer(Filter.ReadPort, TestReadPort);
    ok_eq_pointer(Filter.WritePort, TestWritePort);
    ok_eq_pointer(Filter.TurnTranslationOn, &TurnTranslationOn);
    ok_eq_bool(Filter.Received, TRUE);
    ok_eq_bool(TurnTranslationOn, ExpectedTranslation);
}

START_TEST(IoI8042Hook)
{
    TestFilter(STATUS_SUCCESS, TRUE, FALSE, FALSE);
    TestFilter(STATUS_SUCCESS, TRUE, TRUE, TRUE);
    TestFilter(STATUS_SUCCESS, FALSE, FALSE, TRUE);
    TestFilter(STATUS_DEVICE_NOT_READY, TRUE, FALSE, FALSE);
}
