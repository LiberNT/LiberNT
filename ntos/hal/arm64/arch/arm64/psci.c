/*
 * PROJECT:     LiberNT HAL
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     ARM64 PSCI conduits
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntifs.h>
#include <arc/arc.h>
#include <halacpi_arm64.h>

/*
 * PSCI call helper functions.
 * These invoke SMC or HVC based on the FADT ARM_BOOT_ARCH flags.
 * The choice is indicated by HalpArm64PsciInfo.UseHvc.
 */
#if defined(_MSC_VER) && !defined(__clang__)
LONG
HalpArm64PsciSmc(
    _In_ ULONGLONG FunctionId,
    _In_ ULONGLONG Arg0,
    _In_ ULONGLONG Arg1,
    _In_ ULONGLONG Arg2);

LONG
HalpArm64PsciHvc(
    _In_ ULONGLONG FunctionId,
    _In_ ULONGLONG Arg0,
    _In_ ULONGLONG Arg1,
    _In_ ULONGLONG Arg2);
#endif

LONG
HalpPsciCallSmc(
    _In_ ULONGLONG FunctionId,
    _In_ ULONGLONG Arg0,
    _In_ ULONGLONG Arg1,
    _In_ ULONGLONG Arg2)
{
#if defined(_MSC_VER) && !defined(__clang__)
    return HalpArm64PsciSmc(FunctionId, Arg0, Arg1, Arg2);
#else
    register ULONGLONG x0 __asm__("x0") = FunctionId;
    register ULONGLONG x1 __asm__("x1") = Arg0;
    register ULONGLONG x2 __asm__("x2") = Arg1;
    register ULONGLONG x3 __asm__("x3") = Arg2;

    __asm__ __volatile__(
        "smc #0"
        : "+r"(x0), "+r"(x1), "+r"(x2), "+r"(x3)
        :
        : "x4", "x5", "x6", "x7", "x8", "x9", "x10", "x11",
          "x12", "x13", "x14", "x15", "x16", "x17", "memory"
    );

    return (LONG)x0;
#endif
}

LONG
HalpPsciCallHvc(
    _In_ ULONGLONG FunctionId,
    _In_ ULONGLONG Arg0,
    _In_ ULONGLONG Arg1,
    _In_ ULONGLONG Arg2)
{
#if defined(_MSC_VER) && !defined(__clang__)
    return HalpArm64PsciHvc(FunctionId, Arg0, Arg1, Arg2);
#else
    register ULONGLONG x0 __asm__("x0") = FunctionId;
    register ULONGLONG x1 __asm__("x1") = Arg0;
    register ULONGLONG x2 __asm__("x2") = Arg1;
    register ULONGLONG x3 __asm__("x3") = Arg2;

    __asm__ __volatile__(
        "hvc #0"
        : "+r"(x0), "+r"(x1), "+r"(x2), "+r"(x3)
        :
        : "x4", "x5", "x6", "x7", "x8", "x9", "x10", "x11",
          "x12", "x13", "x14", "x15", "x16", "x17", "memory"
    );

    return (LONG)x0;
#endif
}
