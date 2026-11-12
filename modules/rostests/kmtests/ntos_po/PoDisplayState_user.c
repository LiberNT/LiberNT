/*
 * PROJECT:     LiberNT kernel-mode tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     The platform information and the display requirement from user mode
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <kmt_test.h>
#include <ndk/pofuncs.h>

/* The user-mode winnt.h in the tree lacks these; values and layout from the Windows SDK 10.0.16299 winnt.h */
#define PlatformInformationLevel ((POWER_INFORMATION_LEVEL)66)
#define DisplayStateOn 1

typedef struct _TEST_PLATFORM_INFORMATION
{
    BOOLEAN AoAc;
} TEST_PLATFORM_INFORMATION, *PTEST_PLATFORM_INFORMATION;

static
VOID
TestPlatformInformation(VOID)
{
    SYSTEM_POWER_CAPABILITIES Capabilities;
    ULONG Aligned[2];
    UCHAR Bytes[8];
    PTEST_PLATFORM_INFORMATION Information = (PTEST_PLATFORM_INFORMATION)Aligned;
    PTEST_PLATFORM_INFORMATION Unaligned = (PTEST_PLATFORM_INFORMATION)&Bytes[1];
    ULONG Input = 0;
    NTSTATUS Status;

    RtlZeroMemory(&Capabilities, sizeof(Capabilities));
    Status = NtPowerInformation(SystemPowerCapabilities, NULL, 0, &Capabilities, sizeof(Capabilities));
    ok_eq_hex(Status, STATUS_SUCCESS);

    Information->AoAc = 0xCC;
    Status = NtPowerInformation(PlatformInformationLevel, NULL, 0, Information, sizeof(*Information));
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok_eq_uint(Information->AoAc, Capabilities.AoAc);

    Unaligned->AoAc = 0xCC;
    Status = NtPowerInformation(PlatformInformationLevel, NULL, 0, Unaligned, sizeof(*Unaligned));
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok_eq_uint(Unaligned->AoAc, Capabilities.AoAc);

    Status = NtPowerInformation(PlatformInformationLevel, &Input, sizeof(Input), Information, sizeof(*Information));
    ok_eq_hex(Status, STATUS_INVALID_PARAMETER);
    Status = NtPowerInformation(PlatformInformationLevel, NULL, 0, NULL, 0);
    ok_eq_hex(Status, STATUS_INVALID_PARAMETER);
}

START_TEST(PoDisplayState)
{
    EXECUTION_STATE Previous = 0;
    ULONG State = DisplayStateOn;
    NTSTATUS Status;

    TestPlatformInformation();

    Status = NtPowerInformation(SessionDisplayState, &State, sizeof(State), NULL, 0);
    ok_eq_hex(Status, STATUS_NOT_IMPLEMENTED);

    KmtRunKernelTest("PoDisplayStateOff");

    Status = NtSetThreadExecutionState(ES_CONTINUOUS | ES_DISPLAY_REQUIRED, &Previous);
    ok_eq_hex(Status, STATUS_SUCCESS);
    Status = NtSetThreadExecutionState(ES_CONTINUOUS, &Previous);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok_eq_hex(Previous, ES_CONTINUOUS | ES_DISPLAY_REQUIRED);

    KmtRunKernelTest("PoDisplayStateOn");
}
