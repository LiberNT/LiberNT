/*
 * PROJECT:     LiberNT LoongArch64 HAL
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF
 * PURPOSE:     LS7A real-time clock (TOY) on the 7A1000 bridge
 *
 * Register map: 7A1000 user manual chapter 13, Table 192 (offsets) and
 * Table 199/196 (field layout of SYS_TOYREAD0/1). The TOY is a calendar, not
 * a seconds counter:
 *
 *   SYS_TOYREAD0  0x2C  month 31:26, day 25:21, hour 20:16, minute 15:10,
 *                       second 9:4, 0.1 s 3:0
 *   SYS_TOYREAD1  0x30  year 31:0 (0..16383)
 *   SYS_TOYWRITE0 0x24  same layout as TOYREAD0, write commits the time
 *   SYS_TOYWRITE1 0x28  year
 *   SYS_RTCCTRL   0x40  REN 13, TEN 11, EO 8 - all three must be 1
 *
 * QEMU's model (hw/rtc/ls7a_rtc.c) implements TOYREAD0 as "apply the current
 * seconds field", so a reader can observe a torn value across a second
 * boundary; the read loop below re-reads until two snapshots agree.
 */

#include <ntifs.h>
#include <ioaccess.h>
#include "halp.h"

#define HALP_LA64_RTC_TOYCTRL     0x40
#define HALP_LA64_RTC_REN         0x00002000UL  /* bit 13 */
#define HALP_LA64_RTC_TEN         0x00000800UL  /* bit 11 */
#define HALP_LA64_RTC_EO          0x00000100UL  /* bit 8  */
#define HALP_LA64_RTC_TOYWRITE0   0x24
#define HALP_LA64_RTC_TOYWRITE1   0x28
#define HALP_LA64_RTC_TOYREAD0    0x2C
#define HALP_LA64_RTC_TOYREAD1    0x30
#define HALP_LA64_RTC_YEAR_MAX    16383

static PUCHAR HalpLa64Rtc;
static KSPIN_LOCK HalpLa64RtcLock;

static
ULONG
HalpLa64RtcRead(
    _In_ ULONG Offset)
{
    return READ_REGISTER_ULONG((volatile ULONG *)(HalpLa64Rtc + Offset));
}

static
VOID
HalpLa64RtcWrite(
    _In_ ULONG Offset,
    _In_ ULONG Value)
{
    WRITE_REGISTER_ULONG((volatile ULONG *)(HalpLa64Rtc + Offset), Value);
}

BOOLEAN
HalpLa64InitializeRtc(VOID)
{
    if (!HalpLa64Platform.RtcBase || HalpLa64Platform.RtcLength < 0x100)
        return FALSE;
    KeInitializeSpinLock(&HalpLa64RtcLock);
    return TRUE;
}

BOOLEAN
HalpLa64MapRtc(VOID)
{
    PHYSICAL_ADDRESS Address;
    ULONG Control;

    if (!HalpLa64Platform.RtcBase)
        return FALSE;
    Address.QuadPart = (LONGLONG)HalpLa64Platform.RtcBase;
    HalpLa64Rtc = MmMapIoSpace(Address, PAGE_SIZE, MmNonCached);
    if (!HalpLa64Rtc)
        return FALSE;

    /* Both the calendar counter and its crystal must be enabled, or every TOY
     * read returns zero (manual Table 200: REN/TEN "needs to be initialized to
     * 1"; EO gates the 32.768 kHz oscillator). */
    Control = HalpLa64RtcRead(HALP_LA64_RTC_TOYCTRL);
    if ((Control & (HALP_LA64_RTC_REN | HALP_LA64_RTC_TEN | HALP_LA64_RTC_EO)) !=
        (HALP_LA64_RTC_REN | HALP_LA64_RTC_TEN | HALP_LA64_RTC_EO))
    {
        HalpLa64RtcWrite(HALP_LA64_RTC_TOYCTRL,
                         Control | HALP_LA64_RTC_REN | HALP_LA64_RTC_TEN |
                         HALP_LA64_RTC_EO);
    }
    return TRUE;
}

BOOLEAN
NTAPI
HalQueryRealTimeClock(
    _Out_ PTIME_FIELDS RtcTime)
{
    ULONG Low, High, Again;
    KIRQL OldIrql;
    TIME_FIELDS Fields;
    LARGE_INTEGER Time;

    if (!RtcTime || !HalpLa64Rtc)
        return FALSE;

    KeRaiseIrql(HIGH_LEVEL, &OldIrql);
    KeAcquireSpinLockAtDpcLevel(&HalpLa64RtcLock);
    /* Re-read until two snapshots agree, so a second boundary between the two
     * register reads cannot produce a mixed date. */
    Low = HalpLa64RtcRead(HALP_LA64_RTC_TOYREAD0);
    do
    {
        Again = Low;
        High = HalpLa64RtcRead(HALP_LA64_RTC_TOYREAD1);
        Low = HalpLa64RtcRead(HALP_LA64_RTC_TOYREAD0);
    } while (Low != Again);
    KeReleaseSpinLockFromDpcLevel(&HalpLa64RtcLock);
    KeLowerIrql(OldIrql);

    Fields.Year = (USHORT)(High & HALP_LA64_RTC_YEAR_MAX);
    Fields.Month = (USHORT)((Low >> 26) & 0x3F);
    Fields.Day = (USHORT)((Low >> 21) & 0x1F);
    Fields.Hour = (USHORT)((Low >> 16) & 0x1F);
    Fields.Minute = (USHORT)((Low >> 10) & 0x3F);
    Fields.Second = (USHORT)((Low >> 4) & 0x3F);
    Fields.Milliseconds = (USHORT)((Low & 0xF) * 100);
    Fields.Weekday = 0;

    if (!RtlTimeFieldsToTime(&Fields, &Time))
        return FALSE;
    RtlTimeToTimeFields(&Time, RtcTime);
    return TRUE;
}

BOOLEAN
NTAPI
HalSetRealTimeClock(
    _In_ PTIME_FIELDS RtcTime)
{
    ULONG Low;
    KIRQL OldIrql;

    if (!RtcTime || !HalpLa64Rtc || RtcTime->Year > HALP_LA64_RTC_YEAR_MAX ||
        RtcTime->Month < 1 || RtcTime->Month > 12 || RtcTime->Day < 1 ||
        RtcTime->Hour > 23 || RtcTime->Minute > 59 || RtcTime->Second > 59)
        return FALSE;

    Low = ((ULONG)(RtcTime->Month & 0x3F) << 26) |
          ((ULONG)(RtcTime->Day & 0x1F) << 21) |
          ((ULONG)(RtcTime->Hour & 0x1F) << 16) |
          ((ULONG)(RtcTime->Minute & 0x3F) << 10) |
          ((ULONG)(RtcTime->Second & 0x3F) << 4) |
          (ULONG)((RtcTime->Milliseconds / 100) & 0xF);

    KeRaiseIrql(HIGH_LEVEL, &OldIrql);
    KeAcquireSpinLockAtDpcLevel(&HalpLa64RtcLock);
    /* Year first: the manual defines the two write registers independently,
     * and QEMU's model applies each one on its own, so writing the time last
     * keeps "new time, old year" from being visible for a whole second. */
    HalpLa64RtcWrite(HALP_LA64_RTC_TOYWRITE1, (ULONG)RtcTime->Year);
    HalpLa64RtcWrite(HALP_LA64_RTC_TOYWRITE0, Low);
    KeReleaseSpinLockFromDpcLevel(&HalpLa64RtcLock);
    KeLowerIrql(OldIrql);
    return TRUE;
}
