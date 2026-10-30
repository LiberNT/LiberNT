/*
 * PROJECT:     LiberNT CPU profiler
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     ARM64 generic timer and cycle counter reads
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <windows.h>
#include <reactos/arm64/sysreg.h>
#include "../clock.h"

ULONGLONG ReadClockTicks(void)
{
    return ARM64_READ_SYSREG(CNTVCT_EL0);
}

ULONGLONG ReadClockCycles(void)
{
    ARM64_ISB();
    return ARM64_READ_SYSREG(PMCCNTR_EL0);
}

ULONGLONG ReadClockFrequency(void)
{
    return ARM64_READ_SYSREG(CNTFRQ_EL0);
}
