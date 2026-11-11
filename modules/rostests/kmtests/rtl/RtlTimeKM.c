/*
 * PROJECT:         LiberNT kernel-mode tests
 * LICENSE:         GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:         Kernel-Mode Test Suite RTL time conversion API
 */

#include <kmt_test.h>

#define NDEBUG
#include <debug.h>

static
VOID
TestTimeFieldsRoundtrip(VOID)
{
    TIME_FIELDS Fields;
    LARGE_INTEGER Time, Back;
    BOOLEAN Ok;

    RtlZeroMemory(&Fields, sizeof(Fields));
    Fields.Year = 2024;
    Fields.Month = 6;
    Fields.Day = 15;
    Fields.Hour = 12;
    Fields.Minute = 34;
    Fields.Second = 56;
    Fields.Milliseconds = 789;

    Ok = RtlTimeFieldsToTime(&Fields, &Time);
    ok_bool_true(Ok, "RtlTimeFieldsToTime");
    ok(Time.QuadPart > 0, "converted time not positive\n");

    RtlZeroMemory(&Fields, sizeof(Fields));
    RtlTimeToTimeFields(&Time, &Fields);
    ok_eq_int(Fields.Year, 2024);
    ok_eq_int(Fields.Month, 6);
    ok_eq_int(Fields.Day, 15);
    ok_eq_int(Fields.Hour, 12);
    ok_eq_int(Fields.Minute, 34);
    ok_eq_int(Fields.Second, 56);
    ok_eq_int(Fields.Milliseconds, 789);
    ok_eq_int(Fields.Weekday, 6);

    RtlTimeToTimeFields(&Time, &Fields);
    Back.QuadPart = 0;
    Ok = RtlTimeFieldsToTime(&Fields, &Back);
    ok_bool_true(Ok, "roundtrip back");
    ok(Back.QuadPart == Time.QuadPart - (Time.QuadPart % 10000), "roundtrip drift: %I64d vs %I64d\n", Back.QuadPart, Time.QuadPart);
}

static
VOID
TestTimeFieldsInvalid(VOID)
{
    TIME_FIELDS Fields;
    LARGE_INTEGER Time;
    BOOLEAN Ok;

    RtlZeroMemory(&Fields, sizeof(Fields));
    Fields.Year = 2024;
    Fields.Month = 13;
    Fields.Day = 1;
    Ok = RtlTimeFieldsToTime(&Fields, &Time);
    ok_bool_false(Ok, "month 13 accepted");

    RtlZeroMemory(&Fields, sizeof(Fields));
    Fields.Year = 2023;
    Fields.Month = 2;
    Fields.Day = 29;
    Ok = RtlTimeFieldsToTime(&Fields, &Time);
    ok_bool_false(Ok, "Feb 29 non-leap accepted");

    RtlZeroMemory(&Fields, sizeof(Fields));
    Fields.Year = 2024;
    Fields.Month = 2;
    Fields.Day = 29;
    Ok = RtlTimeFieldsToTime(&Fields, &Time);
    ok_bool_true(Ok, "Feb 29 leap rejected");
}

static
VOID
TestTimeFieldsRange(VOID)
{
    TIME_FIELDS Fields;
    LARGE_INTEGER Time;
    BOOLEAN Ok;

    RtlZeroMemory(&Fields, sizeof(Fields));
    Fields.Year = 30827;
    Fields.Month = 12;
    Fields.Day = 31;
    Fields.Hour = 23;
    Fields.Minute = 59;
    Fields.Second = 59;
    Fields.Milliseconds = 999;
    Time.QuadPart = 0;
    Ok = RtlTimeFieldsToTime(&Fields, &Time);
    ok_bool_true(Ok, "last moment of 30827 rejected");
    ok(Time.QuadPart == 0x7FFF35F4F06C58F0LL, "30827-12-31 -> %I64x\n", Time.QuadPart);

    RtlZeroMemory(&Fields, sizeof(Fields));
    Fields.Year = 30828;
    Fields.Month = 9;
    Fields.Day = 14;
    Fields.Hour = 2;
    Fields.Minute = 48;
    Fields.Second = 6;
    Ok = RtlTimeFieldsToTime(&Fields, &Time);
    ok_bool_false(Ok, "a moment past the 64-bit range accepted");

    RtlZeroMemory(&Fields, sizeof(Fields));
    Fields.Year = 30829;
    Fields.Month = 1;
    Fields.Day = 1;
    Ok = RtlTimeFieldsToTime(&Fields, &Time);
    ok_bool_false(Ok, "year 30829 accepted");

    RtlZeroMemory(&Fields, sizeof(Fields));
    Fields.Year = 32767;
    Fields.Month = 12;
    Fields.Day = 31;
    Ok = RtlTimeFieldsToTime(&Fields, &Time);
    ok_bool_false(Ok, "year 32767 accepted");
}

static
VOID
TestUnixTimeConversion(VOID)
{
    LARGE_INTEGER Time;
    ULONG Seconds;
    BOOLEAN Ok;

    RtlSecondsSince1970ToTime(0, &Time);
    ok(Time.QuadPart == 0x019DB1DED53E8000LL, "epoch 1970 -> %I64x\n", Time.QuadPart);

    Ok = RtlTimeToSecondsSince1970(&Time, &Seconds);
    ok_bool_true(Ok, "back to 1970 seconds");
    ok_eq_ulong(Seconds, 0UL);

    RtlSecondsSince1980ToTime(0, &Time);
    ok(Time.QuadPart == 0x01A8E79FE1D58000LL, "epoch 1980 -> %I64x\n", Time.QuadPart);

    Ok = RtlTimeToSecondsSince1980(&Time, &Seconds);
    ok_bool_true(Ok, "back to 1980 seconds");
    ok_eq_ulong(Seconds, 0UL);
}

START_TEST(RtlTimeKM)
{
    TestTimeFieldsRoundtrip();
    TestTimeFieldsInvalid();
    TestTimeFieldsRange();
    TestUnixTimeConversion();
}
