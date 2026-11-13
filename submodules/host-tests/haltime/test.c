/*
 * PROJECT:     LiberNT host-native tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Stall end tick arithmetic tests
 * COPYRIGHT:   Copyright 2026 LiberNT contributors
 */

#include <halstall.h>
#include <stdio.h>

static int Failures;

#define CHECK(condition) \
    do { \
        if (!(condition)) { \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
            Failures++; \
        } \
    } while (0)

#define MAX_TICK ((ULONG64)~((ULONG64)0))

int
main(void)
{
    CHECK(HalpComputeStallEndTime(1000, 1000, 500) == 501000);
    CHECK(HalpComputeStallEndTime(1000, 1000, 0) == 1000);
    CHECK(HalpComputeStallEndTime(MAX_TICK - 5, 10, 1) == MAX_TICK);
    CHECK(HalpComputeStallEndTime(MAX_TICK - 1000, 1000, 1) == MAX_TICK);
    CHECK(HalpComputeStallEndTime(MAX_TICK, 1000, 1) == MAX_TICK);

    if (Failures)
    {
        printf("%d check(s) failed\n", Failures);
        return 1;
    }

    printf("haltime.stall: all checks passed\n");
    return 0;
}
