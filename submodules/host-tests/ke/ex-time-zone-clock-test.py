# PROJECT:     LiberNT host-native tests
# LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
# PURPOSE:     Boot reads a local-time hardware clock; only a later change of bias rewrites it
# COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
import argparse
import pathlib
import subprocess
import tempfile

parser = argparse.ArgumentParser()
parser.add_argument('--root', required=True)
parser.add_argument('--cc', required=True)
args = parser.parse_args()
source = (pathlib.Path(args.root) / 'ntos/ex/extime.c').read_text()


def definition(name):
    index = source.index('\n' + name + '(')
    start = index
    while True:
        line_start = source.rfind('\n', 0, start) + 1
        line = source[line_start:start].strip()
        if line == '' or line.endswith('*/') or line.endswith(';') or line.endswith('}') or line.startswith('#'):
            break
        start = line_start - 1
    start = start + 1 if source[start] == '\n' else start
    depth = 1
    end = source.index('{', index) + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end] + '\n'


fixture = r'''
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#define VOID void
#define NTAPI
#define _In_
#define _Out_
#define TRUE 1
#define FALSE 0
#define MAXLONGLONG INT64_MAX
#define STATUS_SUCCESS ((NTSTATUS)0)
#define NT_SUCCESS(s) ((NTSTATUS)(s) >= 0)
#define TIME_ZONE_ID_UNKNOWN 0
#define TIME_ZONE_ID_STANDARD 1
#define TIME_ZONE_ID_DAYLIGHT 2
#define PAGED_CODE()
#define UNREFERENCED_PARAMETER(x) ((void)(x))
#define CHECK(x) do { if (!(x)) { printf("%s:%d: %s failed\n", Scenario, __LINE__, #x); exit(1); } } while (0)
#define ASSERT(x) CHECK(x)
#define Int32x32To64(a, b) ((int64_t)(int32_t)(a) * (int64_t)(int32_t)(b))
typedef unsigned char BOOLEAN;
typedef void *PVOID;
typedef uint32_t ULONG;
typedef int32_t LONG, NTSTATUS;
typedef int64_t LONGLONG;
typedef union { LONGLONG QuadPart; struct { uint32_t LowPart; int32_t HighPart; }; } LARGE_INTEGER, *PLARGE_INTEGER;
typedef struct { LONG Bias; LONG StandardBias; LONG DaylightBias; } RTL_TIME_ZONE_INFORMATION, *PRTL_TIME_ZONE_INFORMATION;
typedef struct { LONGLONG Time; } TIME_FIELDS, *PTIME_FIELDS;
typedef struct { uint32_t LowPart; int32_t High1Time; int32_t High2Time; } KSYSTEM_TIME;
typedef struct { KSYSTEM_TIME TimeZoneBias; ULONG TimeZoneId; } KUSER_SHARED_DATA;
static const char *Scenario = "setup";
static KUSER_SHARED_DATA SharedData, *MmWriteableSharedUserData = &SharedData;
static RTL_TIME_ZONE_INFORMATION ExpTimeZoneInfo, Registry;
static LARGE_INTEGER ExpTimeZoneBias;
static ULONG ExpTimeZoneId = TIME_ZONE_ID_UNKNOWN, ExpAltTimeZoneBias, ExpLastTimeZoneBias = (ULONG)-1;
static BOOLEAN ExpRealTimeIsUniversal = FALSE;
static LONGLONG Clock, Now;
static int ClockWrites;
static ULONG ExpResolveTimeZoneId(PRTL_TIME_ZONE_INFORMATION Zone, PLARGE_INTEGER Time) { (void)Zone; (void)Time; return TIME_ZONE_ID_UNKNOWN; }
static void KeQuerySystemTime(PLARGE_INTEGER Time) { Time->QuadPart = Now; }
static void KeSetSystemTime(PLARGE_INTEGER New, PLARGE_INTEGER Old, BOOLEAN Interrupt, PVOID Hal) {
    (void)Interrupt; (void)Hal; Old->QuadPart = Now; Now = New->QuadPart;
}
static void PoNotifySystemTimeSet(void) {}
static NTSTATUS RtlQueryTimeZoneInformation(PRTL_TIME_ZONE_INFORMATION Zone) { *Zone = Registry; return STATUS_SUCCESS; }
static void RtlTimeToTimeFields(PLARGE_INTEGER Time, PTIME_FIELDS Fields) { Fields->Time = Time->QuadPart; }
static BOOLEAN RtlTimeFieldsToTime(PTIME_FIELDS Fields, PLARGE_INTEGER Time) { Time->QuadPart = Fields->Time; return TRUE; }
static BOOLEAN HalQueryRealTimeClock(PTIME_FIELDS Fields) { Fields->Time = Clock; return TRUE; }
static BOOLEAN HalSetRealTimeClock(PTIME_FIELDS Fields) { Clock = Fields->Time; ClockWrites++; return TRUE; }
'''
for name in ['ExSystemTimeToLocalTime', 'ExpRealTimeToSystemTime', 'ExpSetTimeZoneInformation',
             'ExRefreshTimeZoneInformation', 'ExUpdateSystemTimeFromCmos']:
    fixture += definition(name)
fixture += r'''
#define MINUTE 600000000LL
int main(void) {
    LARGE_INTEGER BootTime = { .QuadPart = 10000000 };
    LONGLONG Local = 133700000000000000LL, Utc;

    /* What boot does: load the zone, then take the time from the clock, which keeps local time */
    Scenario = "boot";
    Clock = Local;
    Now = BootTime.QuadPart;
    Registry.Bias = -60;
    CHECK(ExRefreshTimeZoneInformation(&BootTime));
    CHECK(ClockWrites == 0);
    CHECK(Clock == Local);
    ExUpdateSystemTimeFromCmos(FALSE, 0);
    Utc = Local - 60 * MINUTE;
    CHECK(Now == Utc);

    Scenario = "zone with the same bias";
    CHECK(ExpSetTimeZoneInformation(&Registry) == STATUS_SUCCESS);
    CHECK(ClockWrites == 0);
    CHECK(Now == Utc);

    Scenario = "zone with another bias";
    Registry.Bias = 300;
    CHECK(ExpSetTimeZoneInformation(&Registry) == STATUS_SUCCESS);
    CHECK(Now == Utc);
    CHECK(ClockWrites == 1);
    CHECK(Clock == Utc - 300 * MINUTE);
    return 0;
}
'''

with tempfile.TemporaryDirectory(prefix='ex-time-zone-clock-') as tmp:
    path = pathlib.Path(tmp)
    (path / 'test.c').write_text(fixture)
    subprocess.run([args.cc, '-std=gnu11', '-Wall', '-Wextra', '-Werror', '-Wno-unused-function',
                    str(path / 'test.c'), '-o', str(path / 'test')], check=True)
    subprocess.run([str(path / 'test')], check=True, timeout=10)
