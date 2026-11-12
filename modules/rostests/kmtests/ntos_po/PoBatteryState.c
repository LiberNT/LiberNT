/*
 * PROJECT:     LiberNT kernel-mode tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests that the system battery state reports the battery class devices
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <kmt_test.h>
#include <batclass.h>
#include "PoFxState_pnp.h"

#define TEST_TAG            9
#define TEST_FULL_CAPACITY  50000
#define TEST_CAPACITY       20000
#define TEST_RATE           (-5000)
#define TEST_ALERT1         2500
#define TEST_ALERT2         5000
#define TEST_ESTIMATED_TIME 14400
#define TEST_SETTLE_MS      3000
#define TEST_POLL_MS        20

static PVOID volatile TestClassData;

static
NTSTATUS
NTAPI
TestQueryTag(
    _In_ PVOID Context,
    _Out_ PULONG BatteryTag)
{
    UNREFERENCED_PARAMETER(Context);
    *BatteryTag = TEST_TAG;
    return STATUS_SUCCESS;
}

static
NTSTATUS
NTAPI
TestQueryInformation(
    _In_ PVOID Context,
    _In_ ULONG BatteryTag,
    _In_ BATTERY_QUERY_INFORMATION_LEVEL Level,
    _In_opt_ LONG AtRate,
    _Out_writes_bytes_to_(BufferLength, *ReturnedLength) PVOID Buffer,
    _In_ ULONG BufferLength,
    _Out_ PULONG ReturnedLength)
{
    PBATTERY_INFORMATION Information = Buffer;

    UNREFERENCED_PARAMETER(Context);
    UNREFERENCED_PARAMETER(AtRate);
    *ReturnedLength = 0;
    if (BatteryTag != TEST_TAG)
        return STATUS_NO_SUCH_DEVICE;

    switch (Level)
    {
        case BatteryInformation:
            if (BufferLength < sizeof(*Information))
                return STATUS_BUFFER_TOO_SMALL;
            RtlZeroMemory(Information, sizeof(*Information));
            Information->Capabilities = BATTERY_SYSTEM_BATTERY;
            Information->Technology = 1;
            Information->DesignedCapacity = TEST_FULL_CAPACITY;
            Information->FullChargedCapacity = TEST_FULL_CAPACITY;
            Information->DefaultAlert1 = TEST_ALERT1;
            Information->DefaultAlert2 = TEST_ALERT2;
            *ReturnedLength = sizeof(*Information);
            return STATUS_SUCCESS;

        case BatteryEstimatedTime:
            if (BufferLength < sizeof(ULONG))
                return STATUS_BUFFER_TOO_SMALL;
            *(PULONG)Buffer = TEST_ESTIMATED_TIME;
            *ReturnedLength = sizeof(ULONG);
            return STATUS_SUCCESS;

        default:
            return STATUS_INVALID_DEVICE_REQUEST;
    }
}

static
NTSTATUS
NTAPI
TestQueryStatus(
    _In_ PVOID Context,
    _In_ ULONG BatteryTag,
    _Out_ PBATTERY_STATUS BatteryStatus)
{
    UNREFERENCED_PARAMETER(Context);
    if (BatteryTag != TEST_TAG)
        return STATUS_NO_SUCH_DEVICE;

    BatteryStatus->PowerState = BATTERY_DISCHARGING;
    BatteryStatus->Capacity = TEST_CAPACITY;
    BatteryStatus->Voltage = 11000;
    BatteryStatus->Rate = TEST_RATE;
    return STATUS_SUCCESS;
}

static
NTSTATUS
NTAPI
TestSetStatusNotify(
    _In_ PVOID Context,
    _In_ ULONG BatteryTag,
    _In_ PBATTERY_NOTIFY BatteryNotify)
{
    UNREFERENCED_PARAMETER(Context);
    UNREFERENCED_PARAMETER(BatteryNotify);
    return BatteryTag == TEST_TAG ? STATUS_SUCCESS : STATUS_NO_SUCH_DEVICE;
}

static
NTSTATUS
NTAPI
TestDisableStatusNotify(
    _In_ PVOID Context)
{
    UNREFERENCED_PARAMETER(Context);
    return STATUS_SUCCESS;
}

static
NTSTATUS
NTAPI
TestBatteryIrp(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp)
{
    PIO_STACK_LOCATION Stack = IoGetCurrentIrpStackLocation(Irp);
    PVOID ClassData;
    NTSTATUS Status = STATUS_SUCCESS;

    UNREFERENCED_PARAMETER(DeviceObject);

    if (Stack->MajorFunction == IRP_MJ_DEVICE_CONTROL)
    {
        ClassData = InterlockedCompareExchangePointer(&TestClassData, NULL, NULL);
        if (ClassData != NULL &&
            DEVICE_TYPE_FROM_CTL_CODE(Stack->Parameters.DeviceIoControl.IoControlCode) == FILE_DEVICE_BATTERY)
        {
            return BatteryClassIoctl(ClassData, Irp);
        }
        Status = ClassData != NULL ? STATUS_NOT_SUPPORTED : STATUS_DEVICE_REMOVED;
    }

    Irp->IoStatus.Status = Status;
    Irp->IoStatus.Information = 0;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return Status;
}

static
BOOLEAN
WaitBatteryPresent(
    _In_ BOOLEAN Present,
    _Out_ PSYSTEM_BATTERY_STATE State)
{
    LARGE_INTEGER Interval;
    ULONG Waited;
    NTSTATUS Status;

    Interval.QuadPart = -10000LL * TEST_POLL_MS;
    for (Waited = 0; ; Waited += TEST_POLL_MS)
    {
        RtlZeroMemory(State, sizeof(*State));
        Status = ZwPowerInformation(SystemBatteryState, NULL, 0, State, sizeof(*State));
        ok_eq_hex(Status, STATUS_SUCCESS);
        if (!NT_SUCCESS(Status))
            return FALSE;
        if (State->BatteryPresent == Present || Waited >= TEST_SETTLE_MS)
            return State->BatteryPresent == Present;
        KeDelayExecutionThread(KernelMode, FALSE, &Interval);
    }
}

static
VOID
TestBatteryDevice(
    _In_ PDEVICE_OBJECT Pdo)
{
    SYSTEM_BATTERY_STATE State;
    BATTERY_MINIPORT_INFO Info;
    PVOID ClassData = NULL;
    NTSTATUS Status;

    if (!WaitBatteryPresent(FALSE, &State))
    {
        skip(FALSE, "The machine has a battery\n");
        return;
    }

    RtlZeroMemory(&Info, sizeof(Info));
    Info.MajorVersion = BATTERY_CLASS_MAJOR_VERSION;
    Info.MinorVersion = BATTERY_CLASS_MINOR_VERSION;
    Info.QueryTag = TestQueryTag;
    Info.QueryInformation = TestQueryInformation;
    Info.QueryStatus = TestQueryStatus;
    Info.SetStatusNotify = TestSetStatusNotify;
    Info.DisableStatusNotify = TestDisableStatusNotify;
    Info.Pdo = Pdo;

    KmtPoFxSetIrpHook(TestBatteryIrp);
    Status = BatteryClassInitializeDevice(&Info, &ClassData);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
    {
        KmtPoFxSetIrpHook(NULL);
        return;
    }
    InterlockedExchangePointer(&TestClassData, ClassData);
    (VOID)BatteryClassStatusNotify(ClassData);

    ok(WaitBatteryPresent(TRUE, &State), "The battery class device was not reported\n");
    ok_eq_int(State.BatteryPresent, TRUE);
    ok_eq_int(State.AcOnLine, FALSE);
    ok_eq_int(State.Charging, FALSE);
    ok_eq_int(State.Discharging, TRUE);
    ok_eq_ulong(State.MaxCapacity, (ULONG)TEST_FULL_CAPACITY);
    ok_eq_ulong(State.RemainingCapacity, (ULONG)TEST_CAPACITY);
    ok_eq_long((LONG)State.Rate, (LONG)TEST_RATE);
    ok_eq_ulong(State.EstimatedTime, (ULONG)TEST_ESTIMATED_TIME);
    ok_eq_ulong(State.DefaultAlert1, (ULONG)TEST_ALERT1);
    ok_eq_ulong(State.DefaultAlert2, (ULONG)TEST_ALERT2);

    InterlockedExchangePointer(&TestClassData, NULL);
    Status = BatteryClassUnload(ClassData);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok(WaitBatteryPresent(FALSE, &State), "The removed battery is still reported\n");
    KmtPoFxSetIrpHook(NULL);
}

START_TEST(PoBatteryState)
{
    PDEVICE_OBJECT Pdo, Fdo;

    Fdo = KmtPoFxAcquireDevice(&Pdo);
    ok(Fdo != NULL, "No started test device; run through kmtest.exe with its signed INF package\n");
    if (!Fdo)
        return;
    TestBatteryDevice(Pdo);
    KmtPoFxReleaseDevice(Fdo);
}
