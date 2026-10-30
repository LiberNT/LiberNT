/*
 * PROJECT:     LiberNT CPU profiler
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Architecture timer and cycle counter interface
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

ULONGLONG ReadClockTicks(void);
ULONGLONG ReadClockCycles(void);
ULONGLONG ReadClockFrequency(void);
