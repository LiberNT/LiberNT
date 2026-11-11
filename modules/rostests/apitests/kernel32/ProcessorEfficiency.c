/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Processor efficiency classes, CPU set flags and EcoQoS placement
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"

#define MAX_TEST_PROCESSORS 64
#define CORE_RECORD_SIZE (FIELD_OFFSET(SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX, Processor) + \
                          FIELD_OFFSET(PROCESSOR_RELATIONSHIP, GroupMask) + sizeof(GROUP_AFFINITY))
#define ECO_SAMPLE_MILLISECONDS 300

typedef BOOL (WINAPI *PFN_GET_LOGICAL_PROCESSOR_INFORMATION_EX)(LOGICAL_PROCESSOR_RELATIONSHIP,
                                                                 PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX,
                                                                 PDWORD);
typedef BOOL (WINAPI *PFN_GET_SYSTEM_CPU_SET_INFORMATION)(PSYSTEM_CPU_SET_INFORMATION, ULONG, PULONG, HANDLE, ULONG);
typedef BOOL (WINAPI *PFN_SET_THREAD_INFORMATION)(HANDLE, THREAD_INFORMATION_CLASS, LPVOID, DWORD);

static PFN_GET_LOGICAL_PROCESSOR_INFORMATION_EX pGetLogicalProcessorInformationEx;
static PFN_GET_SYSTEM_CPU_SET_INFORMATION pGetSystemCpuSetInformation;
static PFN_SET_THREAD_INFORMATION pSetThreadInformation;

static UCHAR CoreClass[MAX_TEST_PROCESSORS];
static ULONGLONG ClassSeen;
static ULONGLONG ProcessorSeen;
static UCHAR LowestClass = 0xFF;
static UCHAR HighestClass;

static
PVOID
QueryRelationship(
    LOGICAL_PROCESSOR_RELATIONSHIP Relationship,
    PDWORD Length)
{
    PVOID Buffer;
    BOOL Result;

    *Length = 0;
    SetLastError(0xDEADBEEF);
    Result = pGetLogicalProcessorInformationEx(Relationship, NULL, Length);
    ok(!Result, "Relationship %d: NULL buffer query succeeded\n", Relationship);
    ok(GetLastError() == ERROR_INSUFFICIENT_BUFFER, "Relationship %d: error %lu\n", Relationship, GetLastError());
    ok(*Length != 0, "Relationship %d: no length\n", Relationship);
    if (*Length == 0)
        return NULL;

    Buffer = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, *Length);
    if (Buffer == NULL)
        return NULL;
    Result = pGetLogicalProcessorInformationEx(Relationship, Buffer, Length);
    ok(Result, "Relationship %d: query failed with %lu\n", Relationship, GetLastError());
    if (!Result)
    {
        HeapFree(GetProcessHeap(), 0, Buffer);
        return NULL;
    }
    return Buffer;
}

static
VOID
TestCoreClasses(VOID)
{
    PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX Info;
    PUCHAR Buffer, Current;
    DWORD Length, Index;
    KAFFINITY Mask;

    Buffer = QueryRelationship(RelationProcessorCore, &Length);
    if (Buffer == NULL)
        return;

    for (Current = Buffer; Current < Buffer + Length; Current += Info->Size)
    {
        Info = (PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX)Current;
        ok(Info->Relationship == RelationProcessorCore, "Core record relationship %d\n", Info->Relationship);
        ok(Info->Size == CORE_RECORD_SIZE, "Core record size %lu, expected %lu\n", Info->Size, (ULONG)CORE_RECORD_SIZE);
        if (Info->Size == 0)
            break;
        ok(Info->Processor.GroupCount == 1, "Core group count %u\n", Info->Processor.GroupCount);
        ok(Info->Processor.GroupMask[0].Group == 0, "Core group %u\n", Info->Processor.GroupMask[0].Group);
        ok(Info->Processor.GroupMask[0].Mask != 0, "Core with an empty mask\n");
        for (Index = 0; Index < sizeof(Info->Processor.Reserved); Index++)
            ok(Info->Processor.Reserved[Index] == 0, "Core reserved byte %lu is %u\n", Index, Info->Processor.Reserved[Index]);

        ClassSeen |= 1ULL << (Info->Processor.EfficiencyClass & 63);
        LowestClass = min(LowestClass, Info->Processor.EfficiencyClass);
        HighestClass = max(HighestClass, Info->Processor.EfficiencyClass);
        for (Mask = Info->Processor.GroupMask[0].Mask, Index = 0; Mask != 0; Mask >>= 1, Index++)
        {
            if (!(Mask & 1) || (Index >= MAX_TEST_PROCESSORS))
                continue;
            ok(!(ProcessorSeen & (1ULL << Index)), "Processor %lu is in two cores\n", Index);
            ProcessorSeen |= 1ULL << Index;
            CoreClass[Index] = Info->Processor.EfficiencyClass;
        }
    }
    ok(Current == Buffer + Length, "Core records end at %Id of %lu\n", Current - Buffer, Length);
    ok(ProcessorSeen != 0, "No processor in any core\n");
    if (LowestClass == HighestClass)
        ok(LowestClass == 0, "Homogeneous cores report efficiency class %u\n", LowestClass);
    trace("Efficiency classes %u..%u, class mask 0x%I64x, processors 0x%I64x\n",
          LowestClass, HighestClass, ClassSeen, ProcessorSeen);

    HeapFree(GetProcessHeap(), 0, Buffer);
}

static
VOID
TestPackageClasses(VOID)
{
    PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX Info;
    PUCHAR Buffer, Current;
    DWORD Length;
    ULONGLONG Covered = 0;

    Buffer = QueryRelationship(RelationProcessorPackage, &Length);
    if (Buffer == NULL)
        return;

    for (Current = Buffer; Current < Buffer + Length; Current += Info->Size)
    {
        Info = (PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX)Current;
        ok(Info->Relationship == RelationProcessorPackage, "Package record relationship %d\n", Info->Relationship);
        if (Info->Size == 0)
            break;
        ok(Info->Processor.EfficiencyClass == 0, "Package efficiency class %u\n", Info->Processor.EfficiencyClass);
        ok(Info->Processor.Flags == 0, "Package flags 0x%x\n", Info->Processor.Flags);
        Covered |= Info->Processor.GroupMask[0].Mask;
    }
    ok(Covered == ProcessorSeen, "Packages cover 0x%I64x, cores cover 0x%I64x\n", Covered, ProcessorSeen);

    HeapFree(GetProcessHeap(), 0, Buffer);
}

static
VOID
TestCpuSets(VOID)
{
    PSYSTEM_CPU_SET_INFORMATION Sets;
    PSYSTEM_CPU_SET_INFORMATION Set;
    ULONG Length = 0, Returned = 0, Count, Index, Unparked = 0;
    USHORT UnparkedBefore, UnparkedAfter;
    BOOL Result;

    SetLastError(0xDEADBEEF);
    Result = pGetSystemCpuSetInformation(NULL, 0, &Length, NULL, 0);
    ok(!Result, "NULL buffer CPU set query succeeded\n");
    ok(GetLastError() == ERROR_INSUFFICIENT_BUFFER, "CPU set error %lu\n", GetLastError());
    ok(Length != 0 && (Length % sizeof(SYSTEM_CPU_SET_INFORMATION)) == 0, "CPU set length %lu\n", Length);
    if (Length == 0)
        return;

    Sets = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, Length);
    if (Sets == NULL)
        return;

    UnparkedBefore = SharedUserData->UnparkedProcessorCount;
    Result = pGetSystemCpuSetInformation(Sets, Length, &Returned, GetCurrentProcess(), 0);
    UnparkedAfter = SharedUserData->UnparkedProcessorCount;
    ok(Result, "CPU set query failed with %lu\n", GetLastError());
    ok(Returned == Length, "CPU set returned %lu, expected %lu\n", Returned, Length);
    if (!Result)
    {
        HeapFree(GetProcessHeap(), 0, Sets);
        return;
    }

    Count = Length / sizeof(SYSTEM_CPU_SET_INFORMATION);
    for (Index = 0; Index < Count; Index++)
    {
        Set = &Sets[Index];
        ok(Set->Size == sizeof(SYSTEM_CPU_SET_INFORMATION), "CPU set %lu size %lu\n", Index, Set->Size);
        ok(Set->Type == CpuSetInformation, "CPU set %lu type %d\n", Index, Set->Type);
        ok(Set->CpuSet.Id == 0x100 + Set->CpuSet.LogicalProcessorIndex, "CPU set %lu id 0x%lx for processor %u\n",
           Index, Set->CpuSet.Id, Set->CpuSet.LogicalProcessorIndex);
        ok(Set->CpuSet.Group == 0, "CPU set %lu group %u\n", Index, Set->CpuSet.Group);
        ok(Set->CpuSet.LogicalProcessorIndex < MAX_TEST_PROCESSORS, "CPU set %lu processor %u\n",
           Index, Set->CpuSet.LogicalProcessorIndex);
        if (Set->CpuSet.LogicalProcessorIndex >= MAX_TEST_PROCESSORS)
            continue;
        ok(ProcessorSeen & (1ULL << Set->CpuSet.LogicalProcessorIndex), "CPU set %lu processor %u is in no core\n",
           Index, Set->CpuSet.LogicalProcessorIndex);
        ok(Set->CpuSet.EfficiencyClass == CoreClass[Set->CpuSet.LogicalProcessorIndex],
           "CPU set %lu efficiency class %u, core class %u\n",
           Index, Set->CpuSet.EfficiencyClass, CoreClass[Set->CpuSet.LogicalProcessorIndex]);
        ok((Set->CpuSet.AllFlags & ~SYSTEM_CPU_SET_INFORMATION_PARKED) == 0, "CPU set %lu flags 0x%x\n",
           Index, Set->CpuSet.AllFlags);
        ok(Set->CpuSet.SchedulingClass == 0, "CPU set %lu scheduling class %u\n", Index, Set->CpuSet.SchedulingClass);
        ok(Set->CpuSet.AllocationTag == 0, "CPU set %lu allocation tag 0x%I64x\n", Index, Set->CpuSet.AllocationTag);
        if (!Set->CpuSet.Parked)
            Unparked++;
        trace("CPU set 0x%lx: processor %u core %u class %u flags 0x%x\n", Set->CpuSet.Id,
              Set->CpuSet.LogicalProcessorIndex, Set->CpuSet.CoreIndex, Set->CpuSet.EfficiencyClass,
              Set->CpuSet.AllFlags);
    }
    ok(Unparked != 0, "Every CPU set is parked\n");
    ok(Unparked == UnparkedBefore || Unparked == UnparkedAfter,
       "%lu unparked CPU sets, shared data says %u then %u\n", Unparked, UnparkedBefore, UnparkedAfter);

    HeapFree(GetProcessHeap(), 0, Sets);
}

static
DWORD
WINAPI
EcoThread(
    LPVOID Parameter)
{
    PULONGLONG Used = Parameter;
    THREAD_POWER_THROTTLING_STATE State;
    DWORD Start;
    ULONG Processor;

    State.Version = THREAD_POWER_THROTTLING_CURRENT_VERSION;
    State.ControlMask = THREAD_POWER_THROTTLING_EXECUTION_SPEED;
    State.StateMask = THREAD_POWER_THROTTLING_EXECUTION_SPEED;
    if (!pSetThreadInformation(GetCurrentThread(), ThreadPowerThrottling, &State, sizeof(State)))
        return GetLastError();

    Sleep(1);
    Start = GetTickCount();
    while (GetTickCount() - Start < ECO_SAMPLE_MILLISECONDS)
    {
        Processor = GetCurrentProcessorNumber();
        if (Processor < MAX_TEST_PROCESSORS)
            *Used |= 1ULL << Processor;
    }
    return ERROR_SUCCESS;
}

static
VOID
TestEcoQosPlacement(VOID)
{
    THREAD_POWER_THROTTLING_STATE State;
    ULONGLONG Used = 0, Efficient = 0;
    DWORD ExitCode = ERROR_GEN_FAILURE;
    HANDLE Thread;
    ULONG Index;
    BOOL Result;

    State.Version = THREAD_POWER_THROTTLING_CURRENT_VERSION;
    State.ControlMask = THREAD_POWER_THROTTLING_EXECUTION_SPEED;
    State.StateMask = THREAD_POWER_THROTTLING_EXECUTION_SPEED;
    Result = pSetThreadInformation(GetCurrentThread(), ThreadPowerThrottling, &State, sizeof(State));
    ok(Result, "EcoQoS opt-in failed with %lu\n", GetLastError());
    State.StateMask = 0;
    Result = pSetThreadInformation(GetCurrentThread(), ThreadPowerThrottling, &State, sizeof(State));
    ok(Result, "HighQoS opt-out failed with %lu\n", GetLastError());
    State.ControlMask = 0;
    Result = pSetThreadInformation(GetCurrentThread(), ThreadPowerThrottling, &State, sizeof(State));
    ok(Result, "Reset to system policy failed with %lu\n", GetLastError());

    if (LowestClass == HighestClass)
    {
        skip("Homogeneous processors, EcoQoS placement has no efficiency class to prefer\n");
        return;
    }

    for (Index = 0; Index < MAX_TEST_PROCESSORS; Index++)
    {
        if ((ProcessorSeen & (1ULL << Index)) && (CoreClass[Index] == LowestClass))
            Efficient |= 1ULL << Index;
    }

    Thread = CreateThread(NULL, 0, EcoThread, &Used, 0, NULL);
    ok(Thread != NULL, "CreateThread failed with %lu\n", GetLastError());
    if (Thread == NULL)
        return;
    ok(WaitForSingleObject(Thread, 10 * ECO_SAMPLE_MILLISECONDS) == WAIT_OBJECT_0, "EcoQoS thread did not finish\n");
    GetExitCodeThread(Thread, &ExitCode);
    CloseHandle(Thread);
    ok(ExitCode == ERROR_SUCCESS, "EcoQoS thread exit code %lu\n", ExitCode);
    ok(Used != 0, "EcoQoS thread never sampled a processor\n");
    ok((Used & ~Efficient) == 0, "EcoQoS thread ran on processors 0x%I64x, efficiency class %u processors are 0x%I64x\n",
       Used, LowestClass, Efficient);
}

START_TEST(ProcessorEfficiency)
{
    HMODULE Kernel32 = GetModuleHandleW(L"kernel32.dll");

    pGetLogicalProcessorInformationEx = (PFN_GET_LOGICAL_PROCESSOR_INFORMATION_EX)
        GetProcAddress(Kernel32, "GetLogicalProcessorInformationEx");
    pGetSystemCpuSetInformation = (PFN_GET_SYSTEM_CPU_SET_INFORMATION)
        GetProcAddress(Kernel32, "GetSystemCpuSetInformation");
    pSetThreadInformation = (PFN_SET_THREAD_INFORMATION)GetProcAddress(Kernel32, "SetThreadInformation");
    if (!pGetLogicalProcessorInformationEx || !pGetSystemCpuSetInformation || !pSetThreadInformation)
    {
        skip("Processor topology APIs are missing\n");
        return;
    }

    TestCoreClasses();
    TestPackageClasses();
    TestCpuSets();
    TestEcoQosPlacement();
}
