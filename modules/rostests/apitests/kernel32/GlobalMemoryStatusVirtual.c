/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test that GlobalMemoryStatusEx reports the user address space of the calling process
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"

START_TEST(GlobalMemoryStatusVirtual)
{
    MEMORYSTATUSEX Status = { sizeof(Status) };
    PIMAGE_NT_HEADERS NtHeaders;
    SYSTEM_INFO Info;
    ULONGLONG Range;
    BOOL Wow64 = FALSE;

    GetSystemInfo(&Info);
    Range = (ULONGLONG)((ULONG_PTR)Info.lpMaximumApplicationAddress - (ULONG_PTR)Info.lpMinimumApplicationAddress) + 1;
    ok(GlobalMemoryStatusEx(&Status), "GlobalMemoryStatusEx failed: %lu\n", GetLastError());
    ok(Status.ullTotalVirtual == Range, "ullTotalVirtual 0x%I64x, expected the user range 0x%I64x\n",
       Status.ullTotalVirtual, Range);
    ok(Status.ullAvailVirtual != 0 && Status.ullAvailVirtual <= Status.ullTotalVirtual,
       "ullAvailVirtual 0x%I64x outside 0x%I64x\n", Status.ullAvailVirtual, Status.ullTotalVirtual);
    ok(Status.ullAvailExtendedVirtual == 0, "ullAvailExtendedVirtual 0x%I64x\n", Status.ullAvailExtendedVirtual);

    if (!IsWow64Process(GetCurrentProcess(), &Wow64) || !Wow64)
        return;

    /* A 32-bit process on a 64-bit system has 2 GB of user space, or 4 GB when its image is large address aware */
    NtHeaders = RtlImageNtHeader(NtCurrentPeb()->ImageBaseAddress);
    ok(NtHeaders != NULL, "No image headers\n");
    if (NtHeaders == NULL)
        return;
    ok(Status.ullTotalVirtual == ((NtHeaders->FileHeader.Characteristics & IMAGE_FILE_LARGE_ADDRESS_AWARE) ? 0xFFFE0000 : 0x7FFE0000),
       "WoW64 ullTotalVirtual 0x%I64x with image characteristics 0x%x\n",
       Status.ullTotalVirtual, NtHeaders->FileHeader.Characteristics);
}
