/*
 * PROJECT:         ReactOS kernel-mode tests
 * LICENSE:         LGPLv2+ - See COPYING.LIB in the top level directory
 * PURPOSE:         Kernel-Mode Test Suite Executive Regressions KM-Test
 * PROGRAMMER:      Aleksey Bragin <aleksey@reactos.org>
 *                  Ahmed ARIF <arif.ing@outlook.com>
 */

#include <kmt_test.h>

#define NDEBUG
#include <debug.h>

static
VOID
CheckStallDuration(
    _In_z_ PCSTR Description,
    _In_ ULONG MicroSeconds,
    _In_ ULONG Iterations)
{
    ULONG i;
    ULONGLONG TimeStart, TimeFinish;
    LONGLONG ExpectedMs, ElapsedMs;

    TimeStart = KeQueryInterruptTime();
    for (i = 0; i < Iterations; i++)
    {
        KeStallExecutionProcessor(MicroSeconds);
    }
    TimeFinish = KeQueryInterruptTime();

    ExpectedMs = ((LONGLONG)MicroSeconds * Iterations) / 1000;
    ElapsedMs = (LONGLONG)(TimeFinish - TimeStart) / 10000;
    ok(ElapsedMs >= ExpectedMs - 50,
       "%s returned too early: %I64d ms, expected at least %I64d ms\n",
       Description, ElapsedMs, ExpectedMs - 50);
}

static VOID KeStallExecutionProcessorTest(VOID)
{
    CheckStallDuration("50us stalls", 50, 10000);
    CheckStallDuration("1000us stalls", 1000, 500);
    CheckStallDuration("1us stalls", 1, 200000);
    CheckStallDuration("one large stall", 500000, 1);
}

#define CORE_RECORD_SIZE (FIELD_OFFSET(SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX, Processor) + \
                          FIELD_OFFSET(PROCESSOR_RELATIONSHIP, GroupMask) + sizeof(GROUP_AFFINITY))

static
PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX
QueryRelationship(
    _In_opt_ PPROCESSOR_NUMBER Number,
    _In_ LOGICAL_PROCESSOR_RELATIONSHIP Relationship,
    _Out_ PULONG Length)
{
    PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX Buffer;
    NTSTATUS Status;

    *Length = 0;
    Status = KeQueryLogicalProcessorRelationship(Number, Relationship, NULL, Length);
    ok_eq_hex(Status, STATUS_INFO_LENGTH_MISMATCH);
    ok(*Length != 0, "Relationship %d: no length\n", Relationship);
    if (*Length == 0)
        return NULL;

    Buffer = ExAllocatePoolWithTag(NonPagedPool, *Length, 'PrcK');
    if (!skip(Buffer != NULL, "No memory for %lu bytes\n", *Length))
    {
        Status = KeQueryLogicalProcessorRelationship(Number, Relationship, Buffer, Length);
        ok_eq_hex(Status, STATUS_SUCCESS);
        if (NT_SUCCESS(Status))
            return Buffer;
        ExFreePoolWithTag(Buffer, 'PrcK');
    }
    return NULL;
}

static VOID KeQueryLogicalProcessorRelationshipTest(VOID)
{
    PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX Cores, Single, Packages, Info;
    UCHAR CoreClass[MAXIMUM_PROCESSORS];
    UCHAR Lowest = MAXUCHAR, Highest = 0;
    KAFFINITY Covered = 0, Mask;
    PROCESSOR_NUMBER Number;
    ULONG Length, SingleLength, Offset, Index, ActiveCount;

    RtlZeroMemory(CoreClass, sizeof(CoreClass));
    ActiveCount = KeQueryActiveProcessorCountEx(ALL_PROCESSOR_GROUPS);

    Cores = QueryRelationship(NULL, RelationProcessorCore, &Length);
    if (Cores == NULL)
        return;
    ok((Length % CORE_RECORD_SIZE) == 0, "Core length %lu\n", Length);
    for (Offset = 0; Offset < Length; Offset += Info->Size)
    {
        Info = (PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX)((PUCHAR)Cores + Offset);
        ok_eq_long(Info->Relationship, RelationProcessorCore);
        ok_eq_ulong(Info->Size, (ULONG)CORE_RECORD_SIZE);
        if (Info->Size == 0)
            break;
        ok_eq_uint(Info->Processor.GroupCount, 1);
        ok_eq_uint(Info->Processor.GroupMask[0].Group, 0);
        Mask = Info->Processor.GroupMask[0].Mask;
        ok((Mask != 0) && !(Mask & Covered), "Core mask %Ix overlaps %Ix\n", Mask, Covered);
        ok_eq_uint(Info->Processor.Flags, (Mask & (Mask - 1)) ? LTP_PC_SMT : 0);
        Covered |= Mask;
        Lowest = min(Lowest, Info->Processor.EfficiencyClass);
        Highest = max(Highest, Info->Processor.EfficiencyClass);
        for (Index = 0; Index < MAXIMUM_PROCESSORS; Index++)
        {
            if (Mask & AFFINITY_MASK(Index))
                CoreClass[Index] = Info->Processor.EfficiencyClass;
        }
    }
    ok_eq_ulong(Offset, Length);
    ok_eq_ulong(RtlNumberOfSetBitsUlongPtr(Covered), ActiveCount);
    if (Lowest == Highest)
        ok_eq_uint(Lowest, 0);
    trace("Efficiency classes %u..%u over processors %Ix\n", Lowest, Highest, Covered);

    for (Index = 0; Index < MAXIMUM_PROCESSORS; Index++)
    {
        if (!(Covered & AFFINITY_MASK(Index)))
            continue;
        RtlZeroMemory(&Number, sizeof(Number));
        Number.Number = (UCHAR)Index;
        Single = QueryRelationship(&Number, RelationProcessorCore, &SingleLength);
        if (Single == NULL)
            continue;
        ok_eq_ulong(SingleLength, (ULONG)CORE_RECORD_SIZE);
        ok(Single->Processor.GroupMask[0].Mask & AFFINITY_MASK(Index), "Core %Ix misses processor %lu\n",
           Single->Processor.GroupMask[0].Mask, Index);
        ok_eq_uint(Single->Processor.EfficiencyClass, CoreClass[Index]);
        ExFreePoolWithTag(Single, 'PrcK');
    }

    Packages = QueryRelationship(NULL, RelationProcessorPackage, &Length);
    if (Packages != NULL)
    {
        for (Offset = 0; Offset < Length; Offset += Info->Size)
        {
            Info = (PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX)((PUCHAR)Packages + Offset);
            ok_eq_long(Info->Relationship, RelationProcessorPackage);
            if (Info->Size == 0)
                break;
            ok_eq_uint(Info->Processor.EfficiencyClass, 0);
        }
        ExFreePoolWithTag(Packages, 'PrcK');
    }

    ExFreePoolWithTag(Cores, 'PrcK');
}

START_TEST(KeProcessor)
{
    KeStallExecutionProcessorTest();
    KeQueryLogicalProcessorRelationshipTest();
}
