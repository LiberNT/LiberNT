/*
 * PROJECT:     LiberNT kernel-mode tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test that the HAL real-time clock runs and keeps the time the system sets
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <kmt_test.h>
#include <ndk/halfuncs.h>

#define NDEBUG
#include <debug.h>

#define SECOND_100NS 10000000LL

static
LONGLONG
QueryClock(VOID)
{
    TIME_FIELDS Fields;
    LARGE_INTEGER Time;

    RtlZeroMemory(&Fields, sizeof(Fields));
    if (!HalQueryRealTimeClock(&Fields))
        return 0;
    if (!RtlTimeFieldsToTime(&Fields, &Time))
        return 0;
    return Time.QuadPart;
}

static
LONGLONG
Distance(
    _In_ LONGLONG A,
    _In_ LONGLONG B)
{
    return (A > B) ? A - B : B - A;
}

START_TEST(HalRealTimeClock)
{
    LARGE_INTEGER SystemTime, LocalTime, Delay;
    LONGLONG First, Second, Clock;
    NTSTATUS Status;

    First = QueryClock();
    ok(First != 0, "HalQueryRealTimeClock failed\n");

    Delay.QuadPart = -(SECOND_100NS * 3 / 2);
    KeDelayExecutionThread(KernelMode, FALSE, &Delay);
    Second = QueryClock();
    ok(Second - First >= SECOND_100NS && Second - First <= 3 * SECOND_100NS,
       "The real-time clock moved by %I64d ms in 1500 ms\n", (Second - First) / 10000);

    KeQuerySystemTime(&SystemTime);
    Status = ZwSetSystemTime(&SystemTime, NULL);
    ok_eq_hex(Status, STATUS_SUCCESS);

    Clock = QueryClock();
    KeQuerySystemTime(&SystemTime);
    ExSystemTimeToLocalTime(&SystemTime, &LocalTime);
    ok(Distance(Clock, LocalTime.QuadPart) <= 2 * SECOND_100NS || Distance(Clock, SystemTime.QuadPart) <= 2 * SECOND_100NS,
       "The real-time clock is %I64d s from local time and %I64d s from system time after setting the time\n",
       (Clock - LocalTime.QuadPart) / SECOND_100NS, (Clock - SystemTime.QuadPart) / SECOND_100NS);
}
