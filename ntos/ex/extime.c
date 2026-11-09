/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     System time, the time zone it is shown in, and the clock's step
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

/* GLOBALS ********************************************************************/

RTL_TIME_ZONE_INFORMATION ExpTimeZoneInfo;
LARGE_INTEGER ExpTimeZoneBias;
ULONG ExpTimeZoneId = TIME_ZONE_ID_UNKNOWN;
ULONG ExpAltTimeZoneBias = 0;
ULONG ExpLastTimeZoneBias = (ULONG)-1;
ULONG ExpTickCountMultiplier = 0;

ERESOURCE ExpTimeRefreshLock;

LIST_ENTRY ExpTimerResolutionListHead;
ULONG ExpKernelRequestedResolution = 0;
ULONG ExpKernelResolutionCount = 0;

/* PRIVATE DATA ***************************************************************/

#define EXP_TIME_TAG 'emiT'

/* How far the clock may differ from the hardware before it is pulled back */
#define EXP_DEFAULT_CMOS_SEPARATION 60

/* The days each month holds, outside a leap year */
static const UCHAR ExpDaysInMonth[12] =
    { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };

typedef struct _EXP_RESOLUTION_REQUEST
{
    LIST_ENTRY Link;
    PEPROCESS Process;
    ULONG Resolution;
} EXP_RESOLUTION_REQUEST, *PEXP_RESOLUTION_REQUEST;

static KSPIN_LOCK ExpResolutionLock;

/* PRIVATE FUNCTIONS **********************************************************/

static
BOOLEAN
ExpIsLeapYear(
    _In_ CSHORT Year)
{
    if ((Year % 4) != 0)
    {
        return FALSE;
    }

    if ((Year % 100) != 0)
    {
        return TRUE;
    }

    return (Year % 400) == 0;
}

static
CSHORT
ExpDaysInThisMonth(
    _In_ CSHORT Year,
    _In_ CSHORT Month)
{
    if ((Month == 2) && ExpIsLeapYear(Year))
    {
        return 29;
    }

    return ExpDaysInMonth[Month - 1];
}

/*
 * A time zone names the day it changes over in one of two ways. With a year it
 * is one exact date, which happens once and never again. Without one it is the
 * nth weekday of a month, which is what a zone that changes over every year
 * says, and the day that falls on has to be worked out for the year in hand.
 */
static
BOOLEAN
ExpResolveCutoverDate(
    _In_ PTIME_FIELDS Cutover,
    _In_ CSHORT Year,
    _Out_ PLARGE_INTEGER LocalTime)
{
    TIME_FIELDS Fields;
    LARGE_INTEGER First;
    CSHORT FirstWeekday;
    CSHORT Day;
    CSHORT Days;

    if (Cutover->Month == 0)
    {
        return FALSE;
    }

    Fields = *Cutover;

    if (Cutover->Year != 0)
    {
        return RtlTimeFieldsToTime(&Fields, LocalTime);
    }

    if ((Cutover->Month < 1) || (Cutover->Month > 12) ||
        (Cutover->Day < 1) || (Cutover->Day > 5) ||
        (Cutover->Weekday < 0) || (Cutover->Weekday > 6))
    {
        return FALSE;
    }

    /* Which weekday the month opens on decides where its nth one falls. */
    Fields.Year = Year;
    Fields.Day = 1;

    if (!RtlTimeFieldsToTime(&Fields, &First))
    {
        return FALSE;
    }

    RtlTimeToTimeFields(&First, &Fields);
    FirstWeekday = Fields.Weekday;

    Day = 1 + ((Cutover->Weekday - FirstWeekday + 7) % 7) +
          (Cutover->Day - 1) * 7;

    Days = ExpDaysInThisMonth(Year, Cutover->Month);

    /*
     * The fifth of a weekday means the last one, and a month may hold only
     * four, so a day past the end of the month steps back a week at a time.
     */
    while (Day > Days)
    {
        Day -= 7;
    }

    Fields = *Cutover;
    Fields.Year = Year;
    Fields.Day = Day;
    Fields.Weekday = 0;

    return RtlTimeFieldsToTime(&Fields, LocalTime);
}

/*
 * Decides which of a zone's two periods a moment falls in. Each cutover is
 * named in the local time of the period it ends, which is why the two are
 * converted through different biases.
 */
static
ULONG
ExpResolveTimeZoneId(
    _In_ PRTL_TIME_ZONE_INFORMATION Information,
    _In_ PLARGE_INTEGER SystemTime)
{
    TIME_FIELDS Now;
    LARGE_INTEGER StandardLocal, DaylightLocal;
    LARGE_INTEGER StandardUtc, DaylightUtc;

    if ((Information->StandardDate.Month == 0) ||
        (Information->DaylightDate.Month == 0))
    {
        return TIME_ZONE_ID_UNKNOWN;
    }

    RtlTimeToTimeFields(SystemTime, &Now);

    if (!ExpResolveCutoverDate(&Information->StandardDate,
                               Now.Year,
                               &StandardLocal) ||
        !ExpResolveCutoverDate(&Information->DaylightDate,
                               Now.Year,
                               &DaylightLocal))
    {
        return TIME_ZONE_ID_UNKNOWN;
    }

    /* The daylight period ends at the standard cutover, so that instant is
       read in daylight time; the standard period ends at the other one. */
    StandardUtc.QuadPart = StandardLocal.QuadPart +
        Int32x32To64((Information->Bias + Information->DaylightBias) * 60,
                     10000000);
    DaylightUtc.QuadPart = DaylightLocal.QuadPart +
        Int32x32To64((Information->Bias + Information->StandardBias) * 60,
                     10000000);

    /*
     * Where the daylight cutover comes first in the year, daylight time is the
     * middle of the year; where it comes second, daylight time spans the turn
     * of the year and standard time is the middle.
     */
    if (DaylightUtc.QuadPart < StandardUtc.QuadPart)
    {
        if ((SystemTime->QuadPart >= DaylightUtc.QuadPart) &&
            (SystemTime->QuadPart < StandardUtc.QuadPart))
        {
            return TIME_ZONE_ID_DAYLIGHT;
        }

        return TIME_ZONE_ID_STANDARD;
    }

    if ((SystemTime->QuadPart >= StandardUtc.QuadPart) &&
        (SystemTime->QuadPart < DaylightUtc.QuadPart))
    {
        return TIME_ZONE_ID_STANDARD;
    }

    return TIME_ZONE_ID_DAYLIGHT;
}

/*
 * Picks the shortest step anybody has asked for and tells the kernel to use it,
 * or lets the clock fall back to its own step when nobody is asking.
 */
static
VOID
ExpApplyTimerResolution(VOID)
{
    PLIST_ENTRY Entry;
    ULONG Shortest = 0;
    KIRQL OldIrql;

    KeAcquireSpinLock(&ExpResolutionLock, &OldIrql);

    for (Entry = ExpTimerResolutionListHead.Flink;
         Entry != &ExpTimerResolutionListHead;
         Entry = Entry->Flink)
    {
        PEXP_RESOLUTION_REQUEST Request =
            CONTAINING_RECORD(Entry, EXP_RESOLUTION_REQUEST, Link);

        if ((Shortest == 0) || (Request->Resolution < Shortest))
        {
            Shortest = Request->Resolution;
        }
    }

    if ((ExpKernelResolutionCount != 0) &&
        ((Shortest == 0) || (ExpKernelRequestedResolution < Shortest)))
    {
        Shortest = ExpKernelRequestedResolution;
    }

    KeReleaseSpinLock(&ExpResolutionLock, OldIrql);

    ExpSetHighResolutionClock(Shortest != 0);
}

static
PEXP_RESOLUTION_REQUEST
ExpFindResolutionRequest(
    _In_ PEPROCESS Process)
{
    PLIST_ENTRY Entry;

    for (Entry = ExpTimerResolutionListHead.Flink;
         Entry != &ExpTimerResolutionListHead;
         Entry = Entry->Flink)
    {
        PEXP_RESOLUTION_REQUEST Request =
            CONTAINING_RECORD(Entry, EXP_RESOLUTION_REQUEST, Link);

        if (Request->Process == Process)
        {
            return Request;
        }
    }

    return NULL;
}

/* PUBLIC FUNCTIONS ***********************************************************/

CODE_SEG("INIT")
VOID
NTAPI
ExpInitializeTime(VOID)
{
    KeInitializeSpinLock(&ExpResolutionLock);
    InitializeListHead(&ExpTimerResolutionListHead);

    RtlZeroMemory(&ExpTimeZoneInfo, sizeof(ExpTimeZoneInfo));
    ExpTimeZoneBias.QuadPart = 0;
    ExpTimeZoneId = TIME_ZONE_ID_UNKNOWN;

    ExInitializeResourceLite(&ExpTimeRefreshLock);
}

/*
 * Works out the number that turns a count of clock ticks into milliseconds,
 * carried as a fraction with twenty-four bits after the point so the shared
 * page can multiply rather than divide.
 */
ULONG
NTAPI
ExComputeTickCountMultiplier(
    _In_ ULONG TimeIncrement)
{
    ULONG Multiplier;
    ULONG Shift;

    Multiplier = TimeIncrement / 10000;
    TimeIncrement = TimeIncrement % 10000;

    for (Shift = 0; Shift < 24; Shift++)
    {
        Multiplier <<= 1;
        TimeIncrement <<= 1;

        if (TimeIncrement >= 10000)
        {
            TimeIncrement -= 10000;
            Multiplier |= 1;
        }
    }

    return Multiplier;
}

BOOLEAN
NTAPI
ExAcquireTimeRefreshLock(
    _In_ BOOLEAN Wait)
{
    KeEnterCriticalRegion();

    if (ExAcquireResourceExclusiveLite(&ExpTimeRefreshLock, Wait))
    {
        return TRUE;
    }

    KeLeaveCriticalRegion();
    return FALSE;
}

VOID
NTAPI
ExReleaseTimeRefreshLock(VOID)
{
    ExReleaseResourceLite(&ExpTimeRefreshLock);
    KeLeaveCriticalRegion();
}

BOOLEAN
NTAPI
ExRealTimeIsUniversal(VOID)
{
    return ExpRealTimeIsUniversal;
}

VOID
NTAPI
ExLocalTimeToSystemTime(
    _In_ PLARGE_INTEGER LocalTime,
    _Out_ PLARGE_INTEGER SystemTime)
{
    SystemTime->QuadPart = LocalTime->QuadPart + ExpTimeZoneBias.QuadPart;
}

VOID
NTAPI
ExSystemTimeToLocalTime(
    _In_ PLARGE_INTEGER SystemTime,
    _Out_ PLARGE_INTEGER LocalTime)
{
    LocalTime->QuadPart = SystemTime->QuadPart - ExpTimeZoneBias.QuadPart;
}

/*
 * Takes the time zone the registry describes and makes it the one the system
 * shows times in, moving the clock by whatever the bias has changed by so the
 * moment being named stays the same.
 */
NTSTATUS
ExpSetTimeZoneInformation(
    _In_ PRTL_TIME_ZONE_INFORMATION TimeZoneInformation)
{
    LARGE_INTEGER LocalTime, SystemTime, NewTime;
    LONG Bias;

    PAGED_CODE();

    ExpTimeZoneInfo = *TimeZoneInformation;

    /*
     * Which bias applies depends on whether the zone is in its standard or its
     * daylight period at this moment; a zone that names no cutover dates only
     * ever has the one bias.
     */
    KeQuerySystemTime(&SystemTime);

    ExpTimeZoneId = ExpResolveTimeZoneId(TimeZoneInformation, &SystemTime);

    if (ExpTimeZoneId == TIME_ZONE_ID_DAYLIGHT)
    {
        Bias = TimeZoneInformation->Bias +
               TimeZoneInformation->DaylightBias;
    }
    else if (ExpTimeZoneId == TIME_ZONE_ID_STANDARD)
    {
        Bias = TimeZoneInformation->Bias +
               TimeZoneInformation->StandardBias;
    }
    else
    {
        Bias = TimeZoneInformation->Bias;
    }

    ExpAltTimeZoneBias = (ULONG)(TimeZoneInformation->Bias +
                                 TimeZoneInformation->DaylightBias);

    /* The clock keeps universal time; the bias is how far local time is behind. */
    ExSystemTimeToLocalTime(&SystemTime, &LocalTime);

    ExpTimeZoneBias.QuadPart = Int32x32To64(Bias * 60, 10000000);

    SharedUserData->TimeZoneBias.High2Time = ExpTimeZoneBias.HighPart;
    SharedUserData->TimeZoneBias.LowPart = ExpTimeZoneBias.LowPart;
    SharedUserData->TimeZoneBias.High1Time = ExpTimeZoneBias.HighPart;
    SharedUserData->TimeZoneId = ExpTimeZoneId;

    /*
     * A clock that keeps local time rather than universal time has to be read
     * through the new bias, so the moment it names does not shift.
     */
    if (!ExpRealTimeIsUniversal)
    {
        ExLocalTimeToSystemTime(&LocalTime, &NewTime);
        KeSetSystemTime(&NewTime, &SystemTime, FALSE, NULL);
    }

    ExpLastTimeZoneBias = (ULONG)Bias;

    return STATUS_SUCCESS;
}

BOOLEAN
NTAPI
ExRefreshTimeZoneInformation(
    _In_ PLARGE_INTEGER SystemBootTime)
{
    RTL_TIME_ZONE_INFORMATION TimeZoneInformation;
    NTSTATUS Status;

    PAGED_CODE();

    UNREFERENCED_PARAMETER(SystemBootTime);

    Status = RtlQueryTimeZoneInformation(&TimeZoneInformation);
    if (!NT_SUCCESS(Status))
    {
        return FALSE;
    }

    Status = ExpSetTimeZoneInformation(&TimeZoneInformation);

    return NT_SUCCESS(Status);
}

/*
 * Pulls the clock back towards the hardware clock when the two have drifted
 * further apart than the caller is willing to accept.
 */
VOID
NTAPI
ExUpdateSystemTimeFromCmos(
    _In_ BOOLEAN UpdateInterruptTime,
    _In_ ULONG MaxSepInSeconds)
{
    TIME_FIELDS TimeFields;
    LARGE_INTEGER HardwareTime, SystemTime, NewTime, Difference;
    LONGLONG Separation;

    if (!HalQueryRealTimeClock(&TimeFields))
    {
        return;
    }

    if (!RtlTimeFieldsToTime(&TimeFields, &HardwareTime))
    {
        return;
    }

    if (ExpRealTimeIsUniversal)
    {
        NewTime = HardwareTime;
    }
    else
    {
        ExLocalTimeToSystemTime(&HardwareTime, &NewTime);
    }

    if (MaxSepInSeconds == 0)
    {
        KeSetSystemTime(&NewTime, &SystemTime, UpdateInterruptTime, NULL);
        return;
    }

    KeQuerySystemTime(&SystemTime);

    Difference.QuadPart = NewTime.QuadPart - SystemTime.QuadPart;
    Separation = Int32x32To64(MaxSepInSeconds, 10000000);

    if ((Difference.QuadPart > Separation) ||
        (Difference.QuadPart < -Separation))
    {
        KeSetSystemTime(&NewTime, &SystemTime, UpdateInterruptTime, NULL);
    }
}

/*
 * Asks the kernel for the shortest clock step it can manage, or lets it go back
 * to its usual one. A short step costs power, so it is only held while asked for.
 */
VOID
NTAPI
ExpSetHighResolutionClock(
    _In_ BOOLEAN Active)
{
    if (Active)
    {
        KeSetTimeIncrement(KeMaximumIncrement, KeMinimumIncrement);
        return;
    }

    KeSetTimeIncrement(KeMaximumIncrement, KeMaximumIncrement);
}

VOID
NTAPI
ExQueryTimerResolution(
    _Out_ PULONG MaximumTime,
    _Out_ PULONG MinimumTime,
    _Out_ PULONG CurrentTime)
{
    *MaximumTime = KeMaximumIncrement;
    *MinimumTime = KeMinimumIncrement;
    *CurrentTime = KeTimeIncrement;
}

/*
 * A caller asks for a step and gets told what the clock now runs at, which may
 * be shorter than it asked for because somebody else asked for less.
 */
ULONG
NTAPI
ExSetTimerResolution(
    _In_ ULONG DesiredTime,
    _In_ BOOLEAN SetResolution)
{
    PEPROCESS Process = PsGetCurrentProcess();
    PEXP_RESOLUTION_REQUEST Request;
    PEXP_RESOLUTION_REQUEST New = NULL;
    KIRQL OldIrql;

    if (SetResolution)
    {
        if (DesiredTime < KeMinimumIncrement)
        {
            DesiredTime = KeMinimumIncrement;
        }

        if (DesiredTime > KeMaximumIncrement)
        {
            DesiredTime = KeMaximumIncrement;
        }

        New = ExAllocatePoolZero(NonPagedPool,
                                 sizeof(EXP_RESOLUTION_REQUEST),
                                 EXP_TIME_TAG);
    }

    KeAcquireSpinLock(&ExpResolutionLock, &OldIrql);

    Request = ExpFindResolutionRequest(Process);

    if (!SetResolution)
    {
        if (Request != NULL)
        {
            RemoveEntryList(&Request->Link);
        }

        KeReleaseSpinLock(&ExpResolutionLock, OldIrql);

        if (Request != NULL)
        {
            ExFreePoolWithTag(Request, EXP_TIME_TAG);
        }

        ExpApplyTimerResolution();

        return KeTimeIncrement;
    }

    if (Request != NULL)
    {
        Request->Resolution = DesiredTime;
        KeReleaseSpinLock(&ExpResolutionLock, OldIrql);

        if (New != NULL)
        {
            ExFreePoolWithTag(New, EXP_TIME_TAG);
        }

        ExpApplyTimerResolution();

        return KeTimeIncrement;
    }

    if (New == NULL)
    {
        KeReleaseSpinLock(&ExpResolutionLock, OldIrql);
        return KeTimeIncrement;
    }

    New->Process = Process;
    New->Resolution = DesiredTime;
    InsertTailList(&ExpTimerResolutionListHead, &New->Link);

    KeReleaseSpinLock(&ExpResolutionLock, OldIrql);

    ExpApplyTimerResolution();

    return KeTimeIncrement;
}

NTSTATUS
NTAPI
NtQuerySystemTime(
    _Out_ PLARGE_INTEGER SystemTime)
{
    LARGE_INTEGER Time;

    if (ExGetPreviousMode() == KernelMode)
    {
        KeQuerySystemTime(SystemTime);
        return STATUS_SUCCESS;
    }

    KeQuerySystemTime(&Time);

    _SEH2_TRY
    {
        ProbeForWriteLargeInteger(SystemTime);
        *SystemTime = Time;
    }
    _SEH2_EXCEPT(ExSystemExceptionFilter())
    {
        _SEH2_YIELD(return _SEH2_GetExceptionCode());
    }
    _SEH2_END;

    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
NtSetSystemTime(
    _In_opt_ PLARGE_INTEGER SystemTime,
    _Out_opt_ PLARGE_INTEGER PreviousTime)
{
    KPROCESSOR_MODE PreviousMode = ExGetPreviousMode();
    LARGE_INTEGER NewTime, OldTime, LocalTime;
    TIME_FIELDS TimeFields;
    NTSTATUS Status = STATUS_SUCCESS;

    PAGED_CODE();

    if (!SeSinglePrivilegeCheck(SeSystemtimePrivilege, PreviousMode))
    {
        return STATUS_PRIVILEGE_NOT_HELD;
    }

    if (SystemTime != NULL)
    {
        if (PreviousMode != KernelMode)
        {
            _SEH2_TRY
            {
                ProbeForReadLargeInteger(SystemTime);
                NewTime = *SystemTime;
            }
            _SEH2_EXCEPT(ExSystemExceptionFilter())
            {
                _SEH2_YIELD(return _SEH2_GetExceptionCode());
            }
            _SEH2_END;
        }
        else
        {
            NewTime = *SystemTime;
        }

        if (NewTime.QuadPart < 0)
        {
            return STATUS_INVALID_PARAMETER;
        }
    }

    if (!ExAcquireTimeRefreshLock(TRUE))
    {
        return STATUS_UNSUCCESSFUL;
    }

    if (SystemTime == NULL)
    {
        /* Nothing was given, so the hardware clock has the say. */
        ExUpdateSystemTimeFromCmos(FALSE, 0);
        KeQuerySystemTime(&OldTime);
    }
    else
    {
        KeSetSystemTime(&NewTime, &OldTime, FALSE, NULL);

        /*
         * The hardware clock is written in whatever it is understood to keep,
         * so the time survives a restart.
         */
        if (ExpRealTimeIsUniversal)
        {
            LocalTime = NewTime;
        }
        else
        {
            ExSystemTimeToLocalTime(&NewTime, &LocalTime);
        }

        RtlTimeToTimeFields(&LocalTime, &TimeFields);
        HalSetRealTimeClock(&TimeFields);
    }

    ExReleaseTimeRefreshLock();

    if (PreviousTime == NULL)
    {
        return STATUS_SUCCESS;
    }

    if (PreviousMode != KernelMode)
    {
        _SEH2_TRY
        {
            ProbeForWriteLargeInteger(PreviousTime);
            *PreviousTime = OldTime;
        }
        _SEH2_EXCEPT(ExSystemExceptionFilter())
        {
            Status = _SEH2_GetExceptionCode();
        }
        _SEH2_END;

        return Status;
    }

    *PreviousTime = OldTime;
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
NtQueryTimerResolution(
    _Out_ PULONG MaximumTime,
    _Out_ PULONG MinimumTime,
    _Out_ PULONG CurrentTime)
{
    ULONG Maximum, Minimum, Current;
    NTSTATUS Status = STATUS_SUCCESS;

    PAGED_CODE();

    ExQueryTimerResolution(&Maximum, &Minimum, &Current);

    if (ExGetPreviousMode() == KernelMode)
    {
        *MaximumTime = Maximum;
        *MinimumTime = Minimum;
        *CurrentTime = Current;
        return STATUS_SUCCESS;
    }

    _SEH2_TRY
    {
        ProbeForWriteUlong(MaximumTime);
        ProbeForWriteUlong(MinimumTime);
        ProbeForWriteUlong(CurrentTime);

        *MaximumTime = Maximum;
        *MinimumTime = Minimum;
        *CurrentTime = Current;
    }
    _SEH2_EXCEPT(ExSystemExceptionFilter())
    {
        Status = _SEH2_GetExceptionCode();
    }
    _SEH2_END;

    return Status;
}

NTSTATUS
NTAPI
NtSetTimerResolution(
    _In_ ULONG DesiredTime,
    _In_ BOOLEAN SetResolution,
    _Out_ PULONG CurrentTime)
{
    ULONG Current;
    NTSTATUS Status = STATUS_SUCCESS;

    PAGED_CODE();

    if (SetResolution &&
        ((DesiredTime < KeMinimumIncrement) ||
         (DesiredTime > KeMaximumIncrement)))
    {
        /* The clock cannot run at the step asked for; it is clamped instead. */
        Status = STATUS_TIMER_RESOLUTION_NOT_SET;
    }

    Current = ExSetTimerResolution(DesiredTime, SetResolution);

    if (ExGetPreviousMode() == KernelMode)
    {
        *CurrentTime = Current;
        return Status;
    }

    _SEH2_TRY
    {
        ProbeForWriteUlong(CurrentTime);
        *CurrentTime = Current;
    }
    _SEH2_EXCEPT(ExSystemExceptionFilter())
    {
        Status = _SEH2_GetExceptionCode();
    }
    _SEH2_END;

    return Status;
}

/* EOF */
