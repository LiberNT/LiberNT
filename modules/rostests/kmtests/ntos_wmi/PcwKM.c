/*
 * PROJECT:     LiberNT kernel-mode tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Performance Counters for Windows provider registration and instances
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <kmt_test.h>

static const PCW_COUNTER_DESCRIPTOR KmtPcwCounters[] =
{
    { 1, 0, 0, sizeof(ULONG) },
    { 2, 0, 8, sizeof(ULONG64) },
    { 3, 1, 0, sizeof(ULONG) },
};

static
VOID
KmtInitRegistration(
    _Out_ PPCW_REGISTRATION_INFORMATION Info,
    _In_ PCUNICODE_STRING Name,
    _In_ PPCW_COUNTER_DESCRIPTOR Counters)
{
    RtlZeroMemory(Info, sizeof(*Info));
    Info->Version = PCW_VERSION_1;
    Info->Name = Name;
    Info->CounterCount = RTL_NUMBER_OF(KmtPcwCounters);
    Info->Counters = Counters;
}

static
VOID
TestRegistration(VOID)
{
    UNICODE_STRING Name = RTL_CONSTANT_STRING(L"KmtPcwCounterset");
    UNICODE_STRING Empty = { 0, 0, NULL };
    UNICODE_STRING Odd = { 3, sizeof(L"KmtPcw"), L"KmtPcw" };
    PCW_COUNTER_DESCRIPTOR Counters[RTL_NUMBER_OF(KmtPcwCounters)];
    PCW_REGISTRATION_INFORMATION Info;
    PPCW_REGISTRATION Registration;
    NTSTATUS Status;

    RtlCopyMemory(Counters, KmtPcwCounters, sizeof(Counters));

    KmtInitRegistration(&Info, &Name, Counters);
    Info.Version = 0x0300;
    Status = PcwRegister(&Registration, &Info);
    ok_eq_hex(Status, STATUS_INVALID_PARAMETER_2);

    KmtInitRegistration(&Info, &Empty, Counters);
    Status = PcwRegister(&Registration, &Info);
    ok_eq_hex(Status, STATUS_INVALID_PARAMETER_2);

    KmtInitRegistration(&Info, &Odd, Counters);
    Status = PcwRegister(&Registration, &Info);
    ok_eq_hex(Status, STATUS_INVALID_PARAMETER_2);

    /* Flags the running version does not know, where the caller's version carries flags */
    KmtInitRegistration(&Info, &Name, Counters);
    Info.Version = PCW_VERSION_2;
    Info.Flags = (PCW_REGISTRATION_FLAGS)0x2;
    Status = PcwRegister(&Registration, &Info);
    ok_eq_hex(Status, STATUS_INVALID_PARAMETER_2);

    /* A first version caller has no flags, so whatever lies there is ignored */
    KmtInitRegistration(&Info, &Name, Counters);
    Info.Flags = (PCW_REGISTRATION_FLAGS)0x2;
    Status = PcwRegister(&Registration, &Info);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (NT_SUCCESS(Status))
        PcwUnregister(Registration);

    KmtInitRegistration(&Info, &Name, Counters);
    Info.Version = PCW_VERSION_2;
    Info.Flags = PcwRegistrationSiloNeutral;
    Status = PcwRegister(&Registration, &Info);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (NT_SUCCESS(Status))
        PcwUnregister(Registration);
}

static
VOID
TestInstances(VOID)
{
    UNICODE_STRING Name = RTL_CONSTANT_STRING(L"KmtPcwCounterset");
    UNICODE_STRING First = RTL_CONSTANT_STRING(L"KmtInstance0");
    UNICODE_STRING Second = RTL_CONSTANT_STRING(L"KmtInstance1");
    PCW_COUNTER_DESCRIPTOR Counters[RTL_NUMBER_OF(KmtPcwCounters)];
    PCW_REGISTRATION_INFORMATION Info;
    PPCW_REGISTRATION Registration;
    PPCW_INSTANCE Instance, Kept;
    ULONG64 Block0[2] = { 0 };
    ULONG Block1 = 0;
    PCW_DATA Data[2];
    NTSTATUS Status;

    RtlCopyMemory(Counters, KmtPcwCounters, sizeof(Counters));
    KmtInitRegistration(&Info, &Name, Counters);
    Status = PcwRegister(&Registration, &Info);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
        return;

    /* The registration kept its own copy of the counters */
    RtlFillMemory(Counters, sizeof(Counters), 0x7F);

    Data[0].Data = Block0;
    Data[0].Size = sizeof(Block0);
    Data[1].Data = &Block1;
    Data[1].Size = sizeof(Block1);

    /* The third counter lives in a second block */
    Status = PcwCreateInstance(&Instance, Registration, &First, 1, Data);
    ok_eq_hex(Status, STATUS_INVALID_PARAMETER_4);

    Data[1].Size = sizeof(USHORT);
    Status = PcwCreateInstance(&Instance, Registration, &First, 2, Data);
    ok_eq_hex(Status, STATUS_INVALID_BUFFER_SIZE);
    Data[1].Size = sizeof(Block1);

    Data[0].Size = 12;
    Status = PcwCreateInstance(&Instance, Registration, &First, 2, Data);
    ok_eq_hex(Status, STATUS_INVALID_BUFFER_SIZE);
    Data[0].Size = sizeof(Block0);

    Status = PcwCreateInstance(&Instance, Registration, &First, 2, Data);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (NT_SUCCESS(Status))
        PcwCloseInstance(Instance);

    /* An instance left open is closed with its registration */
    Status = PcwCreateInstance(&Kept, Registration, &Second, 2, Data);
    ok_eq_hex(Status, STATUS_SUCCESS);

    PcwUnregister(Registration);
}

START_TEST(PcwKM)
{
    TestRegistration();
    TestInstances();
}
