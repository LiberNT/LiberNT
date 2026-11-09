/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests for ProcessDeviceMap
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"

/* Declared by the WDK, not by the NDK the tests build against */
typedef struct _TEST_PROCESS_DEVICEMAP_INFORMATION_EX {
    union {
        struct { HANDLE DirectoryHandle; } Set;
        struct { ULONG DriveMap; UCHAR DriveType[32]; } Query;
    };
    ULONG Flags;
} TEST_PROCESS_DEVICEMAP_INFORMATION_EX;

static
ULONG
SystemDriveIndex(VOID)
{
    WCHAR Buffer[MAX_PATH];

    if (!GetWindowsDirectoryW(Buffer, ARRAYSIZE(Buffer))) return 2;
    return (ULONG)((Buffer[0] | 0x20) - L'a');
}

START_TEST(NtProcessDeviceMap)
{
    PROCESS_DEVICEMAP_INFORMATION Info;
    TEST_PROCESS_DEVICEMAP_INFORMATION_EX InfoEx;
    ULONG Index = SystemDriveIndex();
    ULONG Length;
    NTSTATUS Status;

    RtlFillMemory(&Info, sizeof(Info), 0x55);
    Length = 0x55555555;
    Status = NtQueryInformationProcess(NtCurrentProcess(),
                                       ProcessDeviceMap,
                                       &Info,
                                       sizeof(Info.Query),
                                       &Length);
    ok_hex(Status, STATUS_SUCCESS);
    if (NT_SUCCESS(Status))
    {
        ok_hex(Length, sizeof(Info.Query));
        ok(Index < 32, "system drive index %lu out of range\n", Index);
        if (Index < 32)
        {
            ok((Info.Query.DriveMap & (1UL << Index)) != 0,
               "system drive %lu missing from drive map %lx\n", Index,
               Info.Query.DriveMap);
            ok_int(Info.Query.DriveType[Index], DRIVE_FIXED);
        }
    }

    /* The query takes the base structure or the extended one, nothing else */
    Status = NtQueryInformationProcess(NtCurrentProcess(),
                                       ProcessDeviceMap,
                                       &Info,
                                       sizeof(Info.Query) - 1,
                                       &Length);
    ok_hex(Status, STATUS_INFO_LENGTH_MISMATCH);

    RtlFillMemory(&InfoEx, sizeof(InfoEx), 0x55);
    InfoEx.Flags = 2;
    Status = NtQueryInformationProcess(NtCurrentProcess(),
                                       ProcessDeviceMap,
                                       &InfoEx,
                                       sizeof(InfoEx),
                                       &Length);
    ok_hex(Status, STATUS_INVALID_PARAMETER);

    RtlZeroMemory(&InfoEx, sizeof(InfoEx));
    InfoEx.Flags = 0;
    Status = NtQueryInformationProcess(NtCurrentProcess(),
                                       ProcessDeviceMap,
                                       &InfoEx,
                                       sizeof(InfoEx),
                                       &Length);
    ok_hex(Status, STATUS_SUCCESS);
    if (NT_SUCCESS(Status) && Index < 32)
    {
        ok((InfoEx.Query.DriveMap & (1UL << Index)) != 0,
           "system drive %lu missing from extended drive map %lx\n", Index,
           InfoEx.Query.DriveMap);
    }
}
