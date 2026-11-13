/*
 * PROJECT:     LiberNT HAL
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     What the x86 HAL can measure time with, recorded at init
 * COPYRIGHT:   Copyright 2026 LiberNT contributors
 */

#pragma once

#include <ntdef.h>

typedef enum _HALP_TIME_SOURCE
{
    HalpTimeSourceNone = 0,
    HalpTimeSourceTsc,
    HalpTimeSourcePmTimer,
    HalpTimeSourceApicTimer,
    HalpTimeSourceRtc,
} HALP_TIME_SOURCE;

typedef enum _HALP_TIME_CALIBRATION
{
    HalpTimeCalibrationNone = 0,
    HalpTimeCalibrationHypervisorMsr,
    HalpTimeCalibrationPmTimer,
    HalpTimeCalibrationTsc,
    HalpTimeCalibrationRtc,
} HALP_TIME_CALIBRATION;

/* 0 leaves the fact unknown; it is never guessed */
typedef struct _HALP_TIME_CAPABILITY
{
    HALP_TIME_SOURCE Source;
    ULONG64 Frequency;
    UCHAR AvailabilityStage;
    UCHAR CrossCpuComparable;
    UCHAR SleepResumeAffected;
    HALP_TIME_CALIBRATION Calibration;
} HALP_TIME_CAPABILITY;

typedef struct _HALP_TIME_CAPABILITIES
{
    HALP_TIME_CAPABILITY Counter;
    HALP_TIME_CAPABILITY Interrupt;
    HALP_TIME_CAPABILITY Delay;
} HALP_TIME_CAPABILITIES;

extern HALP_TIME_CAPABILITIES HalpTimeCapabilities;
extern HALP_TIME_CALIBRATION HalpTscCalibration;
