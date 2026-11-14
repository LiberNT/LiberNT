#pragma once

#include <stdarg.h>

static inline void HostDebugPrint(const char *Format, ...)
{
    va_list Arguments;

    va_start(Arguments, Format);
    vfprintf(stderr, Format, Arguments);
    va_end(Arguments);
}

#define DPRINT(...) do { } while (0)
#define DPRINT1 HostDebugPrint
