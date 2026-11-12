/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Task Manager mouse wheel scrolling tests
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <apitest.h>
#include <windows.h>
#include "wheel_scroll.h"

static int ScrollSteps(int *remainder, int delta, int count, int notchDistance)
{
    int total = 0;
    int i;

    for (i = 0; i < count; i++)
        total += TmWheelScroll(remainder, delta, notchDistance);
    return total;
}

START_TEST(wheel_scroll)
{
    int remainder = 0;
    int distance = TmWheelScroll(&remainder, -WHEEL_DELTA, 84);

    ok(distance == 84 && remainder == 0,
       "a notch toward the user scrolled %d (remainder %d), expected 84 down\n", distance, remainder);

    distance = TmWheelScroll(&remainder, WHEEL_DELTA, 84);
    ok(distance == -84 && remainder == 0,
       "a notch away from the user scrolled %d (remainder %d), expected 84 up\n", distance, remainder);

    distance = TmWheelScroll(&remainder, -30, 84);
    ok(distance == 21, "a quarter notch scrolled %d, expected 21\n", distance);

    remainder = 0;
    distance = ScrollSteps(&remainder, -30, 12, 84);
    ok(distance == 3 * 84 && remainder == 0,
       "twelve quarter notches scrolled %d (remainder %d), expected three notches\n", distance, remainder);

    distance = ScrollSteps(&remainder, -1, WHEEL_DELTA, 84);
    ok(distance == 84 && remainder == 0,
       "a notch in single units scrolled %d (remainder %d), expected 84\n", distance, remainder);

    distance = ScrollSteps(&remainder, 1, WHEEL_DELTA, 84);
    ok(distance == -84 && remainder == 0,
       "a notch back in single units scrolled %d (remainder %d), expected -84\n", distance, remainder);

    distance = TmWheelScroll(&remainder, -1, 84);
    distance += TmWheelScroll(&remainder, 1, 84);
    ok(distance == 0 && remainder == 0,
       "a unit forth and back scrolled %d (remainder %d), expected nothing left\n", distance, remainder);
}
