/*
 * PROJECT:     LiberNT NT Library
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     NTDLL time query wrappers
 */

#include <ntdll.h>
#include <reactos/precisetime.h>

#define NDEBUG
#include <debug.h>

BOOL
WINAPI
RtlQueryPerformanceCounter(PLARGE_INTEGER Counter)
{
    NTSTATUS Status;
    LARGE_INTEGER Value;

    if (!Counter)
        return FALSE;

    if (RtlpArchQueryPerformanceCounter(&Value))
    {
        _SEH2_TRY
        {
            *Counter = Value;
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            _SEH2_YIELD(goto SlowPath);
        }
        _SEH2_END;
        return TRUE;
    }

SlowPath:
    Status = NtQueryPerformanceCounter(Counter, NULL);
    return NT_SUCCESS(Status);
}

BOOL
WINAPI
RtlQueryPerformanceFrequency(PLARGE_INTEGER Frequency)
{
    LARGE_INTEGER Counter;
    NTSTATUS Status;
    LARGE_INTEGER Value;

    if (!Frequency)
        return FALSE;

    if (RtlpArchQueryPerformanceFrequency(&Value))
    {
        _SEH2_TRY
        {
            *Frequency = Value;
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            _SEH2_YIELD(goto SlowPath);
        }
        _SEH2_END;
        return TRUE;
    }

SlowPath:
    Status = NtQueryPerformanceCounter(&Counter, Frequency);
    return NT_SUCCESS(Status);
}

BOOL
WINAPI
RtlQueryUnbiasedInterruptTime(PULONGLONG UnbiasedTime)
{
    LARGE_INTEGER InterruptTime;

    if (!UnbiasedTime)
    {
        RtlSetLastWin32ErrorAndNtStatusFromNtStatus(STATUS_INVALID_PARAMETER);
        return FALSE;
    }

    InterruptTime = KiReadSystemTime(&SharedUserData->InterruptTime);
    *UnbiasedTime = (ULONGLONG)InterruptTime.QuadPart - SharedUserData->InterruptTimeBias;
    return TRUE;
}

VOID
WINAPI
RtlQuerySystemTime(PLARGE_INTEGER SystemTime)
{
    if (SystemTime)
        NtQuerySystemTime(SystemTime);
}

LONGLONG
WINAPI
RtlGetSystemTimePrecise(VOID)
{
    LARGE_INTEGER Counter;
    ULONGLONG Time, Baseline, Increment;
    ULONG Sequence;
    UCHAR Shift;

    for (;;)
    {
        Sequence = ReadULongAcquire((volatile ULONG *)&SharedUserData->TimeUpdateLock);
        if (Sequence & 1)
        {
            YieldProcessor();
            continue;
        }
        Time = KiReadSystemTime(&SharedUserData->SystemTime).QuadPart;
        Baseline = ReadULong64NoFence(&SharedUserData->BaselineSystemTimeQpc);
        Increment = ReadULong64NoFence(&SharedUserData->QpcSystemTimeIncrement);
        Shift = ReadUCharNoFence(&SharedUserData->QpcSystemTimeIncrementShift);
        RtlQueryPerformanceCounter(&Counter);
        MemoryBarrier();
        if (Sequence == ReadULongAcquire((volatile ULONG *)&SharedUserData->TimeUpdateLock)) break;
    }
    return Time + RtlpScaleTimeDelta(Counter.QuadPart - Baseline, Increment, Shift, NULL);
}

VOID
WINAPI
RtlSystemTimeToTimeFields(const LARGE_INTEGER *SystemTime,
                          PTIME_FIELDS TimeFields)
{
    if (!SystemTime || !TimeFields)
        return;

    RtlTimeToTimeFields((PLARGE_INTEGER)SystemTime, TimeFields);
}
