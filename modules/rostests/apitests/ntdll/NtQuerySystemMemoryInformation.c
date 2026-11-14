/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests for the memory accounting system information classes
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arifing193@gmail.com>
 */

#include "precomp.h"

static
VOID
TestFileCacheInformation(VOID)
{
    SYSTEM_FILECACHE_INFORMATION Info;
    ULONG Length = 0;
    NTSTATUS Status;

    Status = NtQuerySystemInformation(SystemFileCacheInformation,
                                      &Info,
                                      sizeof(Info) - 1,
                                      &Length);
    ok_hex(Status, STATUS_INFO_LENGTH_MISMATCH);

    RtlZeroMemory(&Info, sizeof(Info));
    Length = 0;
    Status = NtQuerySystemInformation(SystemFileCacheInformation,
                                      &Info,
                                      sizeof(Info),
                                      &Length);
    ok_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
        return;

    ok(Length == sizeof(Info), "Length %lu, expected %lu\n",
       Length, (ULONG)sizeof(Info));

    /* CurrentSize and PeakSize are byte counts, so both are whole pages. */
    ok((Info.CurrentSize & (PAGE_SIZE - 1)) == 0,
       "CurrentSize %Iu is not a multiple of the page size\n", Info.CurrentSize);
    ok((Info.PeakSize & (PAGE_SIZE - 1)) == 0,
       "PeakSize %Iu is not a multiple of the page size\n", Info.PeakSize);
    ok(Info.PeakSize >= Info.CurrentSize,
       "PeakSize %Iu is below CurrentSize %Iu\n", Info.PeakSize, Info.CurrentSize);

    /*
     * The two transition members count pages, not bytes, so a size reported in
     * bytes in either of them is off by PAGE_SIZE.
     */
    ok(Info.CurrentSizeIncludingTransitionInPages >= (Info.CurrentSize >> PAGE_SHIFT),
       "CurrentSizeIncludingTransitionInPages %Iu is below the %Iu resident pages\n",
       Info.CurrentSizeIncludingTransitionInPages, Info.CurrentSize >> PAGE_SHIFT);
    ok(Info.CurrentSizeIncludingTransitionInPages < (Info.CurrentSize >> PAGE_SHIFT) + 0x100000,
       "CurrentSizeIncludingTransitionInPages %Iu is implausible for a %Iu-byte cache\n",
       Info.CurrentSizeIncludingTransitionInPages, Info.CurrentSize);
    ok(Info.PeakSizeIncludingTransitionInPages >= Info.CurrentSizeIncludingTransitionInPages,
       "PeakSizeIncludingTransitionInPages %Iu is below CurrentSizeIncludingTransitionInPages %Iu\n",
       Info.PeakSizeIncludingTransitionInPages, Info.CurrentSizeIncludingTransitionInPages);

    ok(Info.MaximumWorkingSet >= Info.CurrentSize,
       "MaximumWorkingSet %Iu is below CurrentSize %Iu\n",
       Info.MaximumWorkingSet, Info.CurrentSize);
    ok(Info.MinimumWorkingSet <= Info.MaximumWorkingSet,
       "MinimumWorkingSet %Iu is above MaximumWorkingSet %Iu\n",
       Info.MinimumWorkingSet, Info.MaximumWorkingSet);
}

static
VOID
TestMemoryListInformation(VOID)
{
    SYSTEM_MEMORY_LIST_INFORMATION Info;
    MEMORYSTATUSEX Status32;
    ULONGLONG Available;
    ULONGLONG Reported;
    ULONG Length = 0;
    ULONG Index;
    NTSTATUS Status;

    Status = NtQuerySystemInformation(SystemMemoryListInformation,
                                      &Info,
                                      sizeof(Info) - 1,
                                      &Length);
    ok_hex(Status, STATUS_INFO_LENGTH_MISMATCH);

    RtlZeroMemory(&Info, sizeof(Info));
    Length = 0;
    Status = NtQuerySystemInformation(SystemMemoryListInformation,
                                      &Info,
                                      sizeof(Info),
                                      &Length);
    ok_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
        return;

    ok(Length == sizeof(Info), "Length %lu, expected %lu\n",
       Length, (ULONG)sizeof(Info));

    Reported = (ULONGLONG)Info.ZeroPageCount + Info.FreePageCount;
    for (Index = 0; Index < RTL_NUMBER_OF(Info.PageCountByPriority); Index++)
        Reported += Info.PageCountByPriority[Index];

    Status32.dwLength = sizeof(Status32);
    if (!GlobalMemoryStatusEx(&Status32))
    {
        skip("GlobalMemoryStatusEx failed %lu\n", GetLastError());
        return;
    }
    Available = Status32.ullAvailPhys >> PAGE_SHIFT;

    /*
     * The free, zeroed and standby lists are what the system counts as
     * available, so the two have to agree beyond a sampling difference.
     */
    ok(Reported != 0, "No page is on the free, zeroed or standby list\n");
    ok(Reported <= (Status32.ullTotalPhys >> PAGE_SHIFT),
       "The lists hold %I64u pages, more than the %I64u the machine has\n",
       Reported, Status32.ullTotalPhys >> PAGE_SHIFT);
    ok(Reported * 2 >= Available,
       "The lists hold %I64u pages against %I64u available\n", Reported, Available);
    ok(Available * 2 >= Reported,
       "%I64u pages are available against %I64u on the lists\n", Available, Reported);
}

START_TEST(NtQuerySystemMemoryInformation)
{
    TestFileCacheInformation();
    TestMemoryListInformation();
}
