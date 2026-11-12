/*
 * PROJECT:     LiberNT Apple PWM Driver
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Apple silicon PWM controller outputs and the LEDs they drive
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

#include <libernt/fdtlib.h>

#define FPWM_REGISTER_SPAN      0x20
#define FPWM_NS_PER_SECOND      1000000000ULL
#define FPWM_MAX_CYCLES         0xFFFFFFFFULL
#define PWM_LED_MAX_NAME        32

typedef struct _FPWM_OUTPUT
{
    ULONG Control;
    ULONG OnCycles;
    ULONG OffCycles;
    BOOLEAN Enabled;
    ULONG64 PeriodNs;
    ULONG64 DutyNs;
} FPWM_OUTPUT, *PFPWM_OUTPUT;

typedef enum _PWM_LED_DEFAULT
{
    PwmLedDefaultOff,
    PwmLedDefaultOn,
    PwmLedDefaultKeep
} PWM_LED_DEFAULT;

typedef struct _PWM_LED
{
    ULONG Provider;
    ULONG Channel;
    ULONG PeriodNs;
    ULONG MaxBrightness;
    ULONG DefaultBrightness;
    BOOLEAN ActiveLow;
    PWM_LED_DEFAULT Default;
    CHAR Name[PWM_LED_MAX_NAME];
} PWM_LED, *PPWM_LED;

VOID
FpwmReadOutput(
    _In_ PUCHAR Base,
    _In_ ULONG64 ClockHz,
    _Out_ PFPWM_OUTPUT Output);

VOID
FpwmApply(
    _In_ PUCHAR Base,
    _In_ ULONG64 ClockHz,
    _In_ BOOLEAN Enable,
    _In_ ULONG64 PeriodNs,
    _In_ ULONG64 DutyNs);

NTSTATUS
PwmLedParse(
    _In_ const FDT *Fdt,
    _In_ ULONG Node,
    _In_ ULONG ProviderCells,
    _Out_ PPWM_LED Led);

ULONG
PwmLedStartBrightness(
    _In_ const PWM_LED *Led,
    _In_ const FPWM_OUTPUT *Current);

ULONG64
PwmLedDutyNs(
    _In_ const PWM_LED *Led,
    _In_ ULONG Brightness);
