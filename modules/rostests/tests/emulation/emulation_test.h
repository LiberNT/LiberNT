/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Shared declarations of the emulated process test
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

extern LONG EmuTestChecks;
extern LONG EmuTestFailures;

#define CHECK(Expression) \
    do \
    { \
        InterlockedIncrement(&EmuTestChecks); \
        if (!(Expression)) \
        { \
            InterlockedIncrement(&EmuTestFailures); \
            printf("EMUTEST: FAIL %s:%u error=%lu\n", __FILE__, __LINE__, GetLastError()); \
        } \
    } while (0)

VOID
EmuTestCxxExceptions(VOID);

#ifdef __cplusplus
}
#endif
