/*
 * PROJECT:     LiberNT kernel-mode tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Stops in the debugger with memory for its memory commands to read and change
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <kmt_test.h>

UCHAR KdbTestBytes[16] = { 0x10, 0x11, 0xA5, 0x5A, 0x14, 0x15, 0x16, 0x17,
                           0x18, 0x19, 0x1A, 0x1B, 0x1C, 0x1D, 0x1E, 0x1F };
UCHAR KdbTestCopy[16];
WCHAR KdbTestText[] = L"LiberNT";
PVOID KdbTestPointers[2];

START_TEST(KdbMemoryCommands)
{
    static const UCHAR Filled[4] = { 0xEE, 0xFF, 0xEE, 0xFF };

    KdbTestPointers[0] = (PVOID)Test_KdbMemoryCommands;
    KdbTestPointers[1] = KdbTestText;

    /* The debugger's start-up file fills and copies these here */
    DbgBreakPoint();

    ok(RtlCompareMemory(&KdbTestBytes[8], Filled, sizeof(Filled)) == sizeof(Filled),
       "fill did not write the pattern\n");
    ok(RtlCompareMemory(KdbTestCopy, KdbTestBytes, 8) == 8, "move did not copy the bytes\n");
    ok_eq_hex(KdbTestCopy[8], 0);
}
