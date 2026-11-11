/*
 * PROJECT:         LiberNT kernel-mode tests
 * LICENSE:         GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:         Kernel-Mode Test Suite processor affinity API
 */

#include <kmt_test.h>

#define NDEBUG
#include <debug.h>

static
VOID
TestProcessorCounts(VOID)
{
    KAFFINITY Active = 0;
    ULONG Count, CountEx, Maximum;
    KAFFINITY Mask;
    ULONG i, Bits;

    Count = KeQueryActiveProcessorCount(&Active);
    ok(Count >= 1, "active count %lu\n", Count);
    ok(Active != 0, "active mask zero\n");
    ok_eq_ulonglong((ULONGLONG)Active, (ULONGLONG)KeQueryActiveProcessors());

    Bits = 0;
    Mask = Active;
    for (i = 0; i < sizeof(KAFFINITY) * 8; i++)
        if (Mask & ((KAFFINITY)1 << i)) Bits++;
    ok_eq_ulong(Bits, Count);

    CountEx = KeQueryActiveProcessorCountEx(0);
    ok_eq_ulong(CountEx, Count);
    CountEx = KeQueryActiveProcessorCountEx(ALL_PROCESSOR_GROUPS);
    ok_eq_ulong(CountEx, Count);
    CountEx = KeQueryActiveProcessorCountEx(63);
    ok_eq_ulong(CountEx, 0UL);

    Maximum = KeQueryMaximumProcessorCount();
    ok(Maximum >= Count, "maximum %lu < active %lu\n", Maximum, Count);
}

static
VOID
TestAffinityPinning(VOID)
{
    KAFFINITY Active = KeQueryActiveProcessors();
    KAFFINITY Old;
    KPROCESSOR_MODE PreviousMode = ExGetPreviousMode();
    BOOLEAN Attached = KeIsAttachedProcess();
    ULONG OriginalCpu, Cpu, i;

    OriginalCpu = KeGetCurrentProcessorNumberEx(NULL);

    for (i = 0; i < sizeof(KAFFINITY) * 8; i++)
    {
        if ((Active & ((KAFFINITY)1 << i)) == 0) continue;

        Old = KeSetSystemAffinityThreadEx((KAFFINITY)1 << i);
        Cpu = KeGetCurrentProcessorNumberEx(NULL);
        ok_eq_ulong(Cpu, i);
        KeRevertToUserAffinityThreadEx(Old);
        ok_eq_int(ExGetPreviousMode(), PreviousMode);
        ok_eq_bool(KeIsAttachedProcess(), Attached);
    }

    KeSetSystemAffinityThread((KAFFINITY)1 << OriginalCpu);
    ok_eq_ulong(KeGetCurrentProcessorNumberEx(NULL), OriginalCpu);
    KeRevertToUserAffinityThread();
    ok_eq_int(ExGetPreviousMode(), PreviousMode);
    ok_eq_bool(KeIsAttachedProcess(), Attached);
}

static
VOID
TestNestedAffinity(VOID)
{
    KAFFINITY Active = KeQueryActiveProcessors();
    KAFFINITY Old1, Old2;
    ULONG FirstCpu, SecondCpu, i;

    FirstCpu = 0;
    SecondCpu = 0;
    for (i = 0; i < sizeof(KAFFINITY) * 8; i++)
    {
        if (Active & ((KAFFINITY)1 << i))
        {
            FirstCpu = i;
            break;
        }
    }
    for (i = FirstCpu + 1; i < sizeof(KAFFINITY) * 8; i++)
    {
        if (Active & ((KAFFINITY)1 << i))
        {
            SecondCpu = i;
            break;
        }
    }
    if (SecondCpu == 0)
    {
        skip(FALSE, "uniprocessor, nested affinity not exercised\n");
        return;
    }

    Old1 = KeSetSystemAffinityThreadEx((KAFFINITY)1 << FirstCpu);
    ok_eq_ulonglong((ULONGLONG)Old1, 0ULL);
    ok_eq_ulong(KeGetCurrentProcessorNumberEx(NULL), FirstCpu);

    Old2 = KeSetSystemAffinityThreadEx((KAFFINITY)1 << SecondCpu);
    ok_eq_ulonglong((ULONGLONG)Old2, (ULONGLONG)((KAFFINITY)1 << FirstCpu));
    ok_eq_ulong(KeGetCurrentProcessorNumberEx(NULL), SecondCpu);

    KeRevertToUserAffinityThreadEx(Old2);
    ok_eq_ulong(KeGetCurrentProcessorNumberEx(NULL), FirstCpu);

    KeRevertToUserAffinityThreadEx(Old1);
}

static
VOID
TestProcessorRelationships(VOID)
{
    static const LOGICAL_PROCESSOR_RELATIONSHIP Domains[] =
    {
        RelationProcessorCore, RelationProcessorModule, RelationProcessorDie, RelationProcessorPackage
    };
    const ULONG ProcessorSize = FIELD_OFFSET(SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX, Processor.GroupMask[1]);
    PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX Info, Record;
    KAFFINITY Active = KeQueryActiveProcessors();
    PROCESSOR_NUMBER Processor;
    ULONG Counts[RelationProcessorModule + 1];
    ULONG Length, Offset, i, d;
    KAFFINITY Self, Mask;
    NTSTATUS Status;

    RtlZeroMemory(&Processor, sizeof(Processor));
    Processor.Group = 1;
    Length = 0;
    Status = KeQueryLogicalProcessorRelationship(&Processor, RelationAll, NULL, &Length);
    ok_eq_hex(Status, STATUS_INVALID_PARAMETER);

    for (i = 0; i < sizeof(KAFFINITY) * 8; i++)
    {
        Self = (KAFFINITY)1 << i;
        if (!(Active & Self))
            continue;

        Processor.Group = 0;
        Processor.Number = (UCHAR)i;

        for (d = 0; d < RTL_NUMBER_OF(Domains); d++)
        {
            Length = 0;
            Status = KeQueryLogicalProcessorRelationship(&Processor, Domains[d], NULL, &Length);
            ok_eq_hex(Status, STATUS_INFO_LENGTH_MISMATCH);
            ok(Length == ProcessorSize, "Processor %lu, relationship %d: %lu bytes\n", i, Domains[d], Length);
            if (Length != ProcessorSize)
                continue;

            Info = ExAllocatePoolWithTag(NonPagedPool, Length, 'tAeK');
            if (!skip(Info != NULL, "No memory\n"))
            {
                Status = KeQueryLogicalProcessorRelationship(&Processor, Domains[d], Info, &Length);
                ok_eq_hex(Status, STATUS_SUCCESS);
                ok_eq_ulong(Length, ProcessorSize);
                ok(Info->Relationship == Domains[d], "Processor %lu: record of %d for %d\n", i, Info->Relationship, Domains[d]);
                ok(Info->Processor.GroupMask[0].Mask & Self, "Processor %lu, relationship %d: mask 0x%Ix\n", i, Domains[d], Info->Processor.GroupMask[0].Mask);
                ExFreePoolWithTag(Info, 'tAeK');
            }
        }

        Length = 0;
        Status = KeQueryLogicalProcessorRelationship(&Processor, RelationAll, NULL, &Length);
        ok_eq_hex(Status, STATUS_INFO_LENGTH_MISMATCH);
        if (Length == 0)
            continue;

        Info = ExAllocatePoolWithTag(NonPagedPool, Length, 'tAeK');
        if (skip(Info != NULL, "No memory\n"))
            continue;

        Status = KeQueryLogicalProcessorRelationship(&Processor, RelationAll, Info, &Length);
        ok_eq_hex(Status, STATUS_SUCCESS);

        RtlZeroMemory(Counts, sizeof(Counts));
        for (Offset = 0; (Status == STATUS_SUCCESS) && (Offset < Length); Offset += Record->Size)
        {
            Record = (PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX)((PUCHAR)Info + Offset);
            if (Record->Size == 0)
            {
                ok(0, "Processor %lu: empty record\n", i);
                break;
            }

            if (Record->Relationship <= RelationProcessorModule)
                Counts[Record->Relationship]++;

            if (Record->Relationship == RelationCache)
                Mask = Record->Cache.GroupMask.Mask;
            else if (Record->Relationship == RelationNumaNode)
                Mask = Record->NumaNode.GroupMask.Mask;
            else if (Record->Relationship == RelationGroup)
                Mask = Record->Group.GroupInfo[0].ActiveProcessorMask;
            else
                Mask = Record->Processor.GroupMask[0].Mask;
            ok(Mask & Self, "Processor %lu: record of %d with mask 0x%Ix\n", i, Record->Relationship, Mask);
        }

        ok_eq_ulong(Counts[RelationProcessorCore], 1UL);
        ok_eq_ulong(Counts[RelationProcessorModule], 1UL);
        ok_eq_ulong(Counts[RelationProcessorPackage], 1UL);
        ok_eq_ulong(Counts[RelationProcessorDie], 0UL);
        ok_eq_ulong(Counts[RelationNumaNode], 1UL);
        ok_eq_ulong(Counts[RelationGroup], 1UL);
        ExFreePoolWithTag(Info, 'tAeK');
    }
}

START_TEST(KeAffinityKM)
{
    TestProcessorCounts();
    TestAffinityPinning();
    TestNestedAffinity();
    TestProcessorRelationships();
}
