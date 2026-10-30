/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Native host library built as RV64X, called by emulated x64 code and calling it back
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <stdarg.h>
#include <stdio.h>
#include <windef.h>
#include <reactos/emuhost.h>
#include "rv64xtest.h"

static RV64X_TEST_TABLE Rv64xTable;

static
int
Rv64xAdd(
    _In_ int A,
    _In_ short B,
    _In_ unsigned char C,
    _In_ long long D)
{
    return (int)(A + B + C + D);
}

static
double
Rv64xMix(
    _In_ float A,
    _In_ double B,
    _In_ int C,
    _In_ float D,
    _In_ double E,
    _In_ int F,
    _In_ float G,
    _In_ double H,
    _In_ signed char I,
    _In_ unsigned short J)
{
    return A + B * 2 + C * 4 + D * 8 + E * 16 + F * 32 + G * 64 + H * 128 + I * 256 + J * 512.0;
}

static
int
Rv64xCallInt(
    _In_ PRV64X_INT_CALLBACK Callback)
{
    int Result = Callback(-7, -2, 250, 1.5f, -2.25, 100000, 0.5f, -5000000000LL, 3, 9.0);

    return Result < 0 ? Result * 3 : 1000 + Result;
}

static
float
Rv64xCallFloat(
    _In_ PRV64X_FLOAT_CALLBACK Callback)
{
    return Callback(1.25f, 2.5) * 2.0f;
}

static
int
Rv64xCallFormat(
    _In_ PRV64X_FORMAT_CALLBACK Format,
    _Out_writes_(Size) char *Buffer,
    _In_ size_t Size)
{
    return Format(Buffer, Size, "%d %.2f %s %.1f", 42, 3.25, "x", 0.5f);
}

static
int
Rv64xFormat(
    _Out_writes_(Size) char *Buffer,
    _In_ size_t Size,
    _In_ const char *Format,
    ...)
{
    va_list Arguments;
    int Length;

    va_start(Arguments, Format);
    Length = _vsnprintf(Buffer, Size, Format, Arguments);
    va_end(Arguments);
    return Length;
}

ULONG64
NTAPI
EmuHostInitialize(VOID)
{
    const EMU_HOST_INTERFACE *Interface = EmuHostGetInterface();

    Rv64xTable.Size = sizeof(Rv64xTable);
    Rv64xTable.Add = Interface->GuestThunk((PVOID)Rv64xAdd, NULL);
    Rv64xTable.Mix = Interface->GuestThunk((PVOID)Rv64xMix, NULL);
    Rv64xTable.CallInt = Interface->GuestThunk((PVOID)Rv64xCallInt, NULL);
    Rv64xTable.CallFloat = Interface->GuestThunk((PVOID)Rv64xCallFloat, NULL);
    Rv64xTable.CallFormat = Interface->GuestThunk((PVOID)Rv64xCallFormat, NULL);
    Rv64xTable.NativeAdd = (ULONG64)(ULONG_PTR)Rv64xAdd;
    Rv64xTable.NativeMix = (ULONG64)(ULONG_PTR)Rv64xMix;
    Rv64xTable.NativeFormat = (ULONG64)(ULONG_PTR)Rv64xFormat;
    if (!Rv64xTable.Add || !Rv64xTable.Mix || !Rv64xTable.CallInt || !Rv64xTable.CallFloat || !Rv64xTable.CallFormat)
        return 0;
    return (ULONG64)&Rv64xTable;
}
