/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests for the ntdll stack probe exports
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"

#if defined(_M_IX86)
static PVOID ProbeFunction;
static ULONG ProbeSize, ProbeBefore, ProbeAfter;

static void
CallProbe(PVOID Function, ULONG Size)
{
    ProbeFunction = Function;
    ProbeSize = Size;
    __asm__ __volatile__(
        "pushl %%esi\n\t"
        "movl %%esp, %%esi\n\t"
        "movl %[size], %%eax\n\t"
        "call *%[fn]\n\t"
        "movl %%esp, %[after]\n\t"
        "movl %%esi, %[before]\n\t"
        "movl %%esi, %%esp\n\t"
        "popl %%esi\n\t"
        : [before] "=m" (ProbeBefore), [after] "=m" (ProbeAfter)
        : [size] "m" (ProbeSize), [fn] "m" (ProbeFunction)
        : "eax", "ecx", "edx", "memory", "cc");
}

static void
Test_Probe(PCSTR Name, ULONG Alignment)
{
    static const ULONG Sizes[] = { 0x40, 0x41, 0x3001, 0x10007 };
    PVOID Function = GetProcAddress(GetModuleHandleW(L"ntdll.dll"), Name);
    ULONG Expected;
    UINT i;

    ok(Function != NULL, "ntdll!%s is missing\n", Name);
    if (!Function)
        return;

    for (i = 0; i < ARRAYSIZE(Sizes); i++)
    {
        CallProbe(Function, Sizes[i]);
        Expected = (ProbeBefore - Sizes[i]) & ~(Alignment - 1);
        ok(ProbeAfter == Expected, "%s(0x%lx): esp 0x%08lx -> 0x%08lx, expected 0x%08lx\n",
           Name, Sizes[i], ProbeBefore, ProbeAfter, Expected);
    }
}
#endif

START_TEST(StackProbe)
{
    HMODULE hNtdll = GetModuleHandleW(L"ntdll.dll");

#if defined(_M_IX86)
    ok(GetProcAddress(hNtdll, "__chkstk") == NULL, "x86 ntdll must not export __chkstk\n");
    Test_Probe("_chkstk", 1);
    Test_Probe("_alloca_probe", 1);
    Test_Probe("_alloca_probe_8", 8);
    Test_Probe("_alloca_probe_16", 16);
#else
    ok(GetProcAddress(hNtdll, "__chkstk") != NULL, "ntdll!__chkstk is missing\n");
    ok(GetProcAddress(hNtdll, "_chkstk") == NULL, "only x86 ntdll exports _chkstk\n");
    ok(GetProcAddress(hNtdll, "_alloca_probe") == NULL, "only x86 ntdll exports _alloca_probe\n");
    ok(GetProcAddress(hNtdll, "_alloca_probe_8") == NULL, "only x86 ntdll exports _alloca_probe_8\n");
    ok(GetProcAddress(hNtdll, "_alloca_probe_16") == NULL, "only x86 ntdll exports _alloca_probe_16\n");
#endif
}
