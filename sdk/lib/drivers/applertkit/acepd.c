/*
 * PROJECT:     LiberNT Apple RTKit Library
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Apple SN201202x (ACE3) USB-C power delivery controllers over SPMI
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#ifndef UNIT_TEST
#include <ntddk.h>
#endif
#include <reactos/drivers/appleacepd.h>

#define ACE_READ_WINDOW         0x20
#define ACE_WRITE_WINDOW        0xa0
#define ACE_MAX_LENGTH          0x40
#define ACE_SELECT_BUSY         0x80
#define ACE_SELECT_TIMEOUT_MS   100
#define ACE_WAKE_TIMEOUT_MS     100
#define ACE_COMMAND_TIMEOUT_MS  1000
#define ACE_INVALID_COMMAND     0x444d4321UL
#define ACE_TASK_TIMEOUT        1
#define ACE_TASK_REJECTED       3

#ifndef UNIT_TEST
static
VOID
AcePdSleepMs(
    _In_ ULONG Milliseconds)
{
    LARGE_INTEGER Interval;

    Interval.QuadPart = -10000LL * Milliseconds;
    KeDelayExecutionThread(KernelMode, FALSE, &Interval);
}
#endif

static
NTSTATUS
AcePdSelect(
    _In_ PAPPLE_ACE_PD Pd,
    _In_ UCHAR Register)
{
    ULONG Waited;
    UCHAR Echo;
    NTSTATUS Status;

    if (Register & ACE_SELECT_BUSY)
        return STATUS_INVALID_PARAMETER;
    Status = AppleSpmiZeroWrite(Pd->Spmi, Pd->Sid, Register);
    if (!NT_SUCCESS(Status))
        return Status;
    for (Waited = 0;; ++Waited)
    {
        Status = AppleSpmiRegisterRead(Pd->Spmi, Pd->Sid, 0, &Echo);
        if (!NT_SUCCESS(Status))
            return Status;
        if (Echo == Register)
            return STATUS_SUCCESS;
        if (Echo != (Register | ACE_SELECT_BUSY))
            return STATUS_DEVICE_PROTOCOL_ERROR;
        if (Waited >= ACE_SELECT_TIMEOUT_MS)
            return STATUS_IO_TIMEOUT;
        AcePdSleepMs(1);
    }
}

NTSTATUS
NTAPI
AppleAcePdRead(
    _In_ PAPPLE_ACE_PD Pd,
    _In_ UCHAR Register,
    _Out_writes_bytes_(Length) PVOID Buffer,
    _In_ ULONG Length)
{
    NTSTATUS Status;

    if (!Length || Length > ACE_MAX_LENGTH)
        return STATUS_INVALID_PARAMETER;
    Status = AcePdSelect(Pd, Register);
    if (NT_SUCCESS(Status))
        Status = AppleSpmiRead(Pd->Spmi, Pd->Sid, ACE_READ_WINDOW, Buffer, Length);
    return Status;
}

NTSTATUS
NTAPI
AppleAcePdWrite(
    _In_ PAPPLE_ACE_PD Pd,
    _In_ UCHAR Register,
    _In_reads_bytes_(Length) const VOID *Buffer,
    _In_ ULONG Length)
{
    NTSTATUS Status;

    if (!Length || Length > ACE_MAX_LENGTH)
        return STATUS_INVALID_PARAMETER;
    Status = AcePdSelect(Pd, Register);
    if (NT_SUCCESS(Status))
        Status = AppleSpmiWrite(Pd->Spmi, Pd->Sid, ACE_WRITE_WINDOW, Buffer, Length);
    return Status;
}

NTSTATUS
NTAPI
AppleAcePdCommand(
    _In_ PAPPLE_ACE_PD Pd,
    _In_reads_(4) const CHAR *Command,
    _In_reads_bytes_opt_(InputLength) const VOID *Input,
    _In_ ULONG InputLength)
{
    ULONG Value, Waited;
    UCHAR Result;
    NTSTATUS Status;

    Status = AppleAcePdRead(Pd, APPLE_ACE_REG_CMD1, &Value, sizeof(Value));
    if (!NT_SUCCESS(Status))
        return Status;
    if (Value && Value != ACE_INVALID_COMMAND)
        return STATUS_DEVICE_BUSY;
    if (InputLength)
    {
        Status = AppleAcePdWrite(Pd, APPLE_ACE_REG_DATA1, Input, InputLength);
        if (!NT_SUCCESS(Status))
            return Status;
    }
    Status = AppleAcePdWrite(Pd, APPLE_ACE_REG_CMD1, Command, 4);
    if (!NT_SUCCESS(Status))
        return Status;
    for (Waited = 0;; ++Waited)
    {
        Status = AppleAcePdRead(Pd, APPLE_ACE_REG_CMD1, &Value, sizeof(Value));
        if (!NT_SUCCESS(Status))
            return Status;
        if (Value == ACE_INVALID_COMMAND)
            return STATUS_NOT_SUPPORTED;
        if (!Value)
            break;
        if (Waited >= ACE_COMMAND_TIMEOUT_MS)
            return STATUS_IO_TIMEOUT;
        AcePdSleepMs(1);
    }
    Status = AppleAcePdRead(Pd, APPLE_ACE_REG_DATA1, &Result, sizeof(Result));
    if (!NT_SUCCESS(Status))
        return Status;
    if (Result == ACE_TASK_TIMEOUT)
        return STATUS_IO_TIMEOUT;
    if (Result == ACE_TASK_REJECTED)
        return STATUS_REQUEST_NOT_ACCEPTED;
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
AppleAcePdInitialize(
    _In_ PAPPLE_ACE_PD Pd,
    _Out_opt_ PUCHAR PreviousPowerState)
{
    static const CHAR SetPowerState[4] = { 'S', 'S', 'P', 'S' };
    ULONG64 Mask = APPLE_ACE_INT_PLUG_EVENT | APPLE_ACE_INT_POWER_STATUS_UPDATE | APPLE_ACE_INT_DATA_STATUS_UPDATE;
    UCHAR State, Target = APPLE_ACE_POWER_STATE_S0;
    ULONG Waited;
    NTSTATUS Status;

    AppleSpmiEnableInterrupt(Pd->Spmi, Pd->WakeInterrupt);
    AppleSpmiAcknowledgeInterrupt(Pd->Spmi, Pd->WakeInterrupt);
    Status = AppleSpmiWakeup(Pd->Spmi, Pd->Sid);
    if (!NT_SUCCESS(Status))
        return Status;
    for (Waited = 0; !AppleSpmiInterruptPending(Pd->Spmi, Pd->WakeInterrupt); ++Waited)
    {
        if (Waited >= ACE_WAKE_TIMEOUT_MS)
            return STATUS_IO_TIMEOUT;
        AcePdSleepMs(1);
    }
    AppleSpmiAcknowledgeInterrupt(Pd->Spmi, Pd->WakeInterrupt);
    Status = AppleAcePdRead(Pd, APPLE_ACE_REG_SYSTEM_POWER_STATE, &State, sizeof(State));
    if (!NT_SUCCESS(Status))
        return Status;
    if (PreviousPowerState)
        *PreviousPowerState = State;
    if (State != Target)
    {
        Status = AppleAcePdCommand(Pd, SetPowerState, &Target, sizeof(Target));
        if (NT_SUCCESS(Status))
            Status = AppleAcePdRead(Pd, APPLE_ACE_REG_SYSTEM_POWER_STATE, &State, sizeof(State));
        if (!NT_SUCCESS(Status))
            return Status;
        if (State != Target)
            return STATUS_DEVICE_POWER_FAILURE;
    }
    return AppleAcePdWrite(Pd, APPLE_ACE_REG_INT_MASK1, &Mask, sizeof(Mask));
}

NTSTATUS
NTAPI
AppleAcePdPoll(
    _In_ PAPPLE_ACE_PD Pd,
    _Out_ PAPPLE_ACE_PD_STATE State)
{
    NTSTATUS Status;

    RtlZeroMemory(State, sizeof(*State));
    Status = AppleAcePdRead(Pd, APPLE_ACE_REG_INT_EVENT1, &State->Events, sizeof(State->Events));
    if (NT_SUCCESS(Status) && State->Events)
        Status = AppleAcePdWrite(Pd, APPLE_ACE_REG_INT_CLEAR1, &State->Events, sizeof(State->Events));
    if (NT_SUCCESS(Status))
        Status = AppleAcePdRead(Pd, APPLE_ACE_REG_STATUS, &State->Status, sizeof(State->Status));
    if (NT_SUCCESS(Status))
        Status = AppleAcePdRead(Pd, APPLE_ACE_REG_DATA_STATUS, &State->DataStatus, sizeof(State->DataStatus));
    if (NT_SUCCESS(Status))
        Status = AppleAcePdRead(Pd, APPLE_ACE_REG_POWER_STATUS, &State->PowerStatus, sizeof(State->PowerStatus));
    return Status;
}
