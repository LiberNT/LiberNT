/*
 * PROJECT:     LiberNT kernel-mode tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     System information classes the executive answers itself
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <kmt_test.h>
#include <ndk/exfuncs.h>

#define NDEBUG
#include <debug.h>

#define TAG_TEST 'iSxE'

#define POOL_BLOCKS 32
#define POOL_BLOCK_SIZE 3072

static
ULONG
CountProcessors(
    _In_ KAFFINITY Mask)
{
    ULONG Count = 0;

    while (Mask != 0)
    {
        Count += (ULONG)(Mask & 1);
        Mask >>= 1;
    }

    return Count;
}

static
VOID
TestLogicalProcessorInformation(VOID)
{
    PSYSTEM_LOGICAL_PROCESSOR_INFORMATION Info;
    KAFFINITY Active = KeQueryActiveProcessors();
    KAFFINITY Cores = 0;
    KAFFINITY Packages = 0;
    KAFFINITY Level1 = 0;
    ULONG Length = 0;
    ULONG ReturnLength = 0;
    ULONG Caches = 0;
    ULONG Nodes = 0;
    ULONG Count;
    ULONG Index;
    NTSTATUS Status;

    Status = ZwQuerySystemInformation(SystemLogicalProcessorInformation, NULL, 0, &Length);
    ok_eq_hex(Status, STATUS_INFO_LENGTH_MISMATCH);
    ok((Length != 0) && ((Length % sizeof(*Info)) == 0), "length %lu\n", Length);
    if (skip(Length >= sizeof(*Info), "no records\n"))
        return;

    Info = ExAllocatePoolWithTag(PagedPool, Length, TAG_TEST);
    if (skip(Info != NULL, "no pool\n"))
        return;

    Status = ZwQuerySystemInformation(SystemLogicalProcessorInformation, Info, Length - 1, &ReturnLength);
    ok_eq_hex(Status, STATUS_INFO_LENGTH_MISMATCH);
    ok_eq_ulong(ReturnLength, Length);

    ReturnLength = 0;
    Status = ZwQuerySystemInformation(SystemLogicalProcessorInformation, Info, Length, &ReturnLength);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok_eq_ulong(ReturnLength, Length);

    Count = NT_SUCCESS(Status) ? ReturnLength / sizeof(*Info) : 0;

    for (Index = 0; Index < Count; Index++)
    {
        KAFFINITY Mask = (KAFFINITY)Info[Index].ProcessorMask;

        ok((Mask != 0) && ((Mask & ~Active) == 0),
           "record %lu: processors %Ix outside %Ix\n", Index, Mask, Active);

        switch (Info[Index].Relationship)
        {
            case RelationProcessorCore:
                ok((Cores & Mask) == 0, "record %lu: a processor in two cores\n", Index);
                ok_eq_uint(Info[Index].ProcessorCore.Flags,
                           (CountProcessors(Mask) > 1) ? LTP_PC_SMT : 0);
                Cores |= Mask;
                break;

            case RelationProcessorPackage:
                ok((Packages & Mask) == 0, "record %lu: a processor in two packages\n", Index);
                Packages |= Mask;
                break;

            case RelationCache:
                Caches++;
                ok((Info[Index].Cache.Level >= 1) && (Info[Index].Cache.Level <= 3),
                   "record %lu: cache level %u\n", Index, Info[Index].Cache.Level);
                ok(Info[Index].Cache.Size != 0, "record %lu: empty cache\n", Index);
                ok(Info[Index].Cache.LineSize != 0, "record %lu: no line size\n", Index);
                ok(Info[Index].Cache.Type <= CacheTrace,
                   "record %lu: cache type %d\n", Index, Info[Index].Cache.Type);
                if ((Info[Index].Cache.Level == 1) && (Info[Index].Cache.Type != CacheInstruction))
                    Level1 |= Mask;
                break;

            case RelationNumaNode:
                Nodes++;
                break;

            default:
                ok(FALSE, "record %lu: relationship %d\n", Index, Info[Index].Relationship);
                break;
        }
    }

    ok(Cores == Active, "cores cover %Ix of %Ix\n", Cores, Active);
    ok(Packages == Active, "packages cover %Ix of %Ix\n", Packages, Active);
    ok(Nodes >= 1, "no node record\n");
    ok(Caches != 0, "no cache record\n");
    ok(Level1 == Active, "level 1 data caches cover %Ix of %Ix\n", Level1, Active);

    ExFreePoolWithTag(Info, TAG_TEST);
}

static
VOID
TestPoolCounts(VOID)
{
    SYSTEM_PERFORMANCE_INFORMATION Before;
    SYSTEM_PERFORMANCE_INFORMATION After;
    PVOID Blocks[POOL_BLOCKS];
    ULONG ReturnLength = 0;
    ULONG Index;
    NTSTATUS Status;

    Status = ZwQuerySystemInformation(SystemPerformanceInformation, &Before, sizeof(Before), &ReturnLength);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok_eq_ulong(ReturnLength, (ULONG)sizeof(Before));

    for (Index = 0; Index < POOL_BLOCKS; Index++)
        Blocks[Index] = ExAllocatePoolWithTag(PagedPool, POOL_BLOCK_SIZE, TAG_TEST);
    for (Index = 0; Index < POOL_BLOCKS; Index++)
        if (Blocks[Index] != NULL) ExFreePoolWithTag(Blocks[Index], TAG_TEST);

    for (Index = 0; Index < POOL_BLOCKS; Index++)
        Blocks[Index] = ExAllocatePoolWithTag(NonPagedPoolNx, POOL_BLOCK_SIZE, TAG_TEST);
    for (Index = 0; Index < POOL_BLOCKS; Index++)
        if (Blocks[Index] != NULL) ExFreePoolWithTag(Blocks[Index], TAG_TEST);

    Status = ZwQuerySystemInformation(SystemPerformanceInformation, &After, sizeof(After), &ReturnLength);
    ok_eq_hex(Status, STATUS_SUCCESS);

    ok(After.PagedPoolAllocs - Before.PagedPoolAllocs >= POOL_BLOCKS,
       "paged allocations %lu -> %lu\n", Before.PagedPoolAllocs, After.PagedPoolAllocs);
    ok(After.PagedPoolFrees - Before.PagedPoolFrees >= POOL_BLOCKS,
       "paged frees %lu -> %lu\n", Before.PagedPoolFrees, After.PagedPoolFrees);
    ok(After.NonPagedPoolAllocs - Before.NonPagedPoolAllocs >= POOL_BLOCKS,
       "nonpaged allocations %lu -> %lu\n", Before.NonPagedPoolAllocs, After.NonPagedPoolAllocs);
    ok(After.NonPagedPoolFrees - Before.NonPagedPoolFrees >= POOL_BLOCKS,
       "nonpaged frees %lu -> %lu\n", Before.NonPagedPoolFrees, After.NonPagedPoolFrees);
    ok(After.PagedPoolPages != 0, "no paged pool pages\n");
    ok(After.NonPagedPoolPages != 0, "no nonpaged pool pages\n");
}

/* The device counts are the ones the class drivers keep in the I/O manager's configuration */
static
VOID
TestDeviceCounts(VOID)
{
    PCONFIGURATION_INFORMATION Configuration = IoGetConfigurationInformation();
    SYSTEM_DEVICE_INFORMATION Devices;
    ULONG ReturnLength = 0;
    NTSTATUS Status;

    RtlFillMemory(&Devices, sizeof(Devices), 0xCC);
    Status = ZwQuerySystemInformation(SystemDeviceInformation, &Devices, sizeof(Devices), &ReturnLength);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok_eq_ulong(ReturnLength, (ULONG)sizeof(Devices));
    if (!NT_SUCCESS(Status))
        return;

    ok_eq_ulong(Devices.NumberOfDisks, Configuration->DiskCount);
    ok_eq_ulong(Devices.NumberOfFloppies, Configuration->FloppyCount);
    ok_eq_ulong(Devices.NumberOfCdRoms, Configuration->CdRomCount);
    ok_eq_ulong(Devices.NumberOfTapes, Configuration->TapeCount);
    ok_eq_ulong(Devices.NumberOfSerialPorts, Configuration->SerialCount);
    ok_eq_ulong(Devices.NumberOfParallelPorts, Configuration->ParallelCount);
    ok(Devices.NumberOfDisks != 0, "a system started from a disk reports no disk\n");
}

START_TEST(ExSystemInformation)
{
    TestLogicalProcessorInformation();
    TestPoolCounts();
    TestDeviceCounts();
}
