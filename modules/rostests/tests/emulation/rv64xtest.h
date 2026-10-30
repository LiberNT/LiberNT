/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Table shared by the RV64X host library and the emulated test
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

#define RV64X_TEST_LIBRARY L"rv64xhost.dll"

typedef int (*PRV64X_INT_CALLBACK)(int, short, unsigned char, float, double, int, float, long long, int, double);
typedef float (*PRV64X_FLOAT_CALLBACK)(float, double);
typedef int (*PRV64X_FORMAT_CALLBACK)(char *, size_t, const char *, ...);

typedef struct _RV64X_TEST_TABLE
{
    ULONG Size;
    ULONG Reserved;
    ULONG64 Add;
    ULONG64 Mix;
    ULONG64 CallInt;
    ULONG64 CallFloat;
    ULONG64 CallFormat;
    ULONG64 NativeAdd;
    ULONG64 NativeMix;
    ULONG64 NativeFormat;
} RV64X_TEST_TABLE, *PRV64X_TEST_TABLE;
