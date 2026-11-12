/*
 * PROJECT:     LiberNT host-native tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     PS/2 test logging shim
 * COPYRIGHT:   Copyright 2026 LiberNT contributors
 */
#define TRACE_(...) ((void)0)
#define INFO_(...) ((void)0)
#define WARN_(...) ((void)0)
#include <stdarg.h>
extern unsigned TestErrors, TestErrorValue;
/* An error report counts, and keeps its first argument for the test to check */
static inline void TestError(const char *Format, ...)
{
    va_list Arguments;
    (void)Format;
    va_start(Arguments, Format);
    TestErrorValue = va_arg(Arguments, unsigned);
    va_end(Arguments);
    TestErrors++;
}
#define ERR_(Channel, ...) TestError(__VA_ARGS__)
