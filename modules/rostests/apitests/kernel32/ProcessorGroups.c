/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test that every processor of every processor group runs threads and is reported consistently
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"

#define MAX_PROCESSORS 2048
#define RUN_MS 300
#define JOIN_MS 20000
#define BUSY_MS (RUN_MS / 10)
#define MAX_REPORTED 32

typedef struct _PINNED_WORKER
{
    HANDLE Thread;
    DWORD ThreadId;
    WORD Group;
    BYTE Number;
    volatile LONG Spins;
    volatile LONG Strayed;
    volatile LONG Pinned;
} PINNED_WORKER;

static PINNED_WORKER Pinned[MAX_PROCESSORS];
static volatile LONG StartPinned, StopPinned, PinnedReady;

static DWORD WINAPI
PinnedThread(_In_ PVOID Parameter)
{
    PINNED_WORKER *Worker = Parameter;
    GROUP_AFFINITY Affinity = { 0 };
    PROCESSOR_NUMBER Current;

    Affinity.Group = Worker->Group;
    Affinity.Mask = (KAFFINITY)1 << Worker->Number;
    if (SetThreadGroupAffinity(GetCurrentThread(), &Affinity, NULL))
        InterlockedExchange(&Worker->Pinned, 1);

    InterlockedIncrement(&PinnedReady);
    while (!StartPinned)
        SwitchToThread();

    while (!StopPinned)
    {
        GetCurrentProcessorNumberEx(&Current);
        if (Current.Group != Worker->Group || Current.Number != Worker->Number)
            InterlockedIncrement(&Worker->Strayed);
        InterlockedIncrement(&Worker->Spins);
    }

    return 0;
}

static PSYSTEM_PROCESS_INFORMATION
SnapshotThreads(VOID)
{
    ULONG Size = 0x80000, Needed = 0;
    NTSTATUS Status;
    PVOID Buffer;

    for (;;)
    {
        Buffer = HeapAlloc(GetProcessHeap(), 0, Size);
        if (Buffer == NULL)
            return NULL;

        Status = NtQuerySystemInformation(SystemProcessInformation, Buffer, Size, &Needed);
        if (NT_SUCCESS(Status))
            return Buffer;

        HeapFree(GetProcessHeap(), 0, Buffer);
        if (Status != STATUS_INFO_LENGTH_MISMATCH || Size >= 0x4000000)
            return NULL;
        Size = max(Size * 2, Needed + 0x10000);
    }
}

static ULONGLONG
ThreadTimeIn(_In_opt_ PSYSTEM_PROCESS_INFORMATION Snapshot, _In_ const SYSTEM_THREAD_INFORMATION *Wanted)
{
    PSYSTEM_PROCESS_INFORMATION Process = Snapshot;
    PSYSTEM_THREAD_INFORMATION Threads;
    ULONG Index;

    while (Process != NULL)
    {
        Threads = (PSYSTEM_THREAD_INFORMATION)(Process + 1);
        for (Index = 0; Index < Process->NumberOfThreads; Index++)
        {
            if (Threads[Index].ClientId.UniqueProcess == Wanted->ClientId.UniqueProcess &&
                Threads[Index].ClientId.UniqueThread == Wanted->ClientId.UniqueThread &&
                Threads[Index].CreateTime.QuadPart == Wanted->CreateTime.QuadPart)
            {
                return (ULONGLONG)(Threads[Index].KernelTime.QuadPart + Threads[Index].UserTime.QuadPart);
            }
        }

        Process = Process->NextEntryOffset ? (PVOID)((PUCHAR)Process + Process->NextEntryOffset) : NULL;
    }

    return 0;
}

/* Says what a starved thread was doing and which other threads had the processors meanwhile */
static VOID
ReportStarvation(_In_opt_ PSYSTEM_PROCESS_INFORMATION Before, _In_ DWORD Count)
{
    PSYSTEM_PROCESS_INFORMATION After = SnapshotThreads(), Process = After;
    PSYSTEM_THREAD_INFORMATION Threads, Thread;
    HANDLE Self = UlongToHandle(GetCurrentProcessId());
    DWORD Worker, Reported = 0;
    ULONG Index, RanMs;

    while (Process != NULL)
    {
        Threads = (PSYSTEM_THREAD_INFORMATION)(Process + 1);
        for (Index = 0; Index < Process->NumberOfThreads; Index++)
        {
            Thread = &Threads[Index];
            RanMs = (ULONG)(((ULONGLONG)(Thread->KernelTime.QuadPart + Thread->UserTime.QuadPart) -
                             ThreadTimeIn(Before, Thread)) / 10000);

            Worker = Count;
            if (Process->UniqueProcessId == Self)
            {
                for (Worker = 0; Worker < Count; Worker++)
                {
                    if (UlongToHandle(Pinned[Worker].ThreadId) == Thread->ClientId.UniqueThread)
                        break;
                }
            }

            if (Worker < Count)
            {
                if (Pinned[Worker].Spins == 0)
                    trace("PROCESSOR_GROUPS starved (%u, %u): state=%lu wait_reason=%lu priority=%ld switches=%lu ran_ms=%lu\n",
                          Pinned[Worker].Group, Pinned[Worker].Number, Thread->ThreadState, Thread->WaitReason,
                          Thread->Priority, Thread->ContextSwitches, RanMs);
            }
            else if (RanMs >= BUSY_MS && Reported < MAX_REPORTED)
            {
                trace("PROCESSOR_GROUPS busy %wZ pid=%lu tid=%lu: state=%lu priority=%ld base=%ld ran_ms=%lu\n",
                      &Process->ImageName, HandleToUlong(Process->UniqueProcessId),
                      HandleToUlong(Thread->ClientId.UniqueThread), Thread->ThreadState,
                      Thread->Priority, Thread->BasePriority, RanMs);
                Reported++;
            }
        }

        Process = Process->NextEntryOffset ? (PVOID)((PUCHAR)Process + Process->NextEntryOffset) : NULL;
    }

    if (After != NULL)
        HeapFree(GetProcessHeap(), 0, After);
}

static VOID
TestGroupCounts(_Out_ PWORD GroupCount, _Out_writes_(64) PDWORD GroupSizes)
{
    WORD Groups = GetActiveProcessorGroupCount();
    WORD MaximumGroups = GetMaximumProcessorGroupCount();
    DWORD Total = GetActiveProcessorCount(ALL_PROCESSOR_GROUPS);
    DWORD Sum = 0;
    WORD Group;

    ok(Groups >= 1 && Groups <= 64, "GetActiveProcessorGroupCount returned %u\n", Groups);
    ok(MaximumGroups >= Groups, "Maximum group count %u is below the active count %u\n", MaximumGroups, Groups);
    ok(Total >= 1 && Total <= MAX_PROCESSORS, "GetActiveProcessorCount(ALL_PROCESSOR_GROUPS) returned %lu\n", Total);

    *GroupCount = min(Groups, 64);
    for (Group = 0; Group < *GroupCount; Group++)
    {
        GroupSizes[Group] = GetActiveProcessorCount(Group);
        ok(GroupSizes[Group] >= 1 && GroupSizes[Group] <= 64, "Group %u has %lu processors\n", Group, GroupSizes[Group]);
        ok(GetMaximumProcessorCount(Group) >= GroupSizes[Group], "Group %u: maximum below active\n", Group);
        Sum += GroupSizes[Group];
    }

    ok(Sum == Total, "The groups hold %lu processors, the system %lu\n", Sum, Total);
    ok(GetActiveProcessorCount(Groups) == 0, "A group past the last one has %lu processors\n", GetActiveProcessorCount(Groups));
    trace("PROCESSOR_GROUPS groups=%u maximum=%u processors=%lu first_group=%lu\n",
          Groups, MaximumGroups, Total, GroupSizes[0]);
}

static VOID
TestLegacyView(_In_ WORD GroupCount, _In_reads_(GroupCount) const DWORD *GroupSizes)
{
    GROUP_AFFINITY Affinity = { 0 };
    DWORD_PTR ProcessMask = 0, SystemMask = 0;
    SYSTEM_INFO Info;
    BOOL Success;

    Success = GetThreadGroupAffinity(GetCurrentThread(), &Affinity);
    ok(Success, "GetThreadGroupAffinity failed: %lu\n", GetLastError());
    ok(Affinity.Group < GroupCount, "The thread is in group %u of %u\n", Affinity.Group, GroupCount);
    if (!Success || Affinity.Group >= GroupCount)
        return;

    GetSystemInfo(&Info);
    ok(Info.dwNumberOfProcessors == GroupSizes[Affinity.Group],
       "GetSystemInfo counts %lu processors, the thread's group %u has %lu\n",
       Info.dwNumberOfProcessors, Affinity.Group, GroupSizes[Affinity.Group]);

    Success = GetProcessAffinityMask(GetCurrentProcess(), &ProcessMask, &SystemMask);
    ok(Success, "GetProcessAffinityMask failed: %lu\n", GetLastError());
    ok(SystemMask != 0 && (ProcessMask & ~SystemMask) == 0,
       "Process mask 0x%Ix is not within the system mask 0x%Ix\n", (size_t)ProcessMask, (size_t)SystemMask);
    ok(SystemMask == (GroupSizes[Affinity.Group] >= sizeof(DWORD_PTR) * 8 ? ~(DWORD_PTR)0 : ((DWORD_PTR)1 << GroupSizes[Affinity.Group]) - 1),
       "The system mask is 0x%Ix, the thread's group %u has %lu processors\n",
       (size_t)SystemMask, Affinity.Group, GroupSizes[Affinity.Group]);
    trace("PROCESSOR_GROUPS thread_group=%u thread_mask=0x%Ix system_mask=0x%Ix\n",
          Affinity.Group, (size_t)Affinity.Mask, (size_t)SystemMask);
}

static VOID
TestVisitEveryProcessor(_In_ WORD GroupCount, _In_reads_(GroupCount) const DWORD *GroupSizes)
{
    GROUP_AFFINITY Original = { 0 }, Affinity, Previous, Readback;
    PROCESSOR_NUMBER Current;
    DWORD Number, Legacy, Visited = 0;
    WORD Group;
    BOOL Success;

    Success = GetThreadGroupAffinity(GetCurrentThread(), &Original);
    ok(Success, "GetThreadGroupAffinity failed: %lu\n", GetLastError());
    if (!Success)
        return;

    for (Group = 0; Group < GroupCount; Group++)
    {
        for (Number = 0; Number < GroupSizes[Group]; Number++)
        {
            ZeroMemory(&Affinity, sizeof(Affinity));
            Affinity.Group = Group;
            Affinity.Mask = (KAFFINITY)1 << Number;

            Success = SetThreadGroupAffinity(GetCurrentThread(), &Affinity, &Previous);
            ok(Success, "SetThreadGroupAffinity(%u, %lu) failed: %lu\n", Group, Number, GetLastError());
            if (!Success)
                continue;

            GetCurrentProcessorNumberEx(&Current);
            ok(Current.Group == Group && Current.Number == Number,
               "Confined to (%u, %lu) but running on (%u, %u)\n", Group, Number, Current.Group, Current.Number);

            Legacy = GetCurrentProcessorNumber();
            if (Group == 0)
                ok(Legacy == Number, "On (0, %lu) GetCurrentProcessorNumber returned %lu\n", Number, Legacy);
            else
                ok(Legacy < GroupSizes[0], "On (%u, %lu) GetCurrentProcessorNumber returned %lu, group 0 has %lu\n",
                   Group, Number, Legacy, GroupSizes[0]);

            ZeroMemory(&Readback, sizeof(Readback));
            Success = GetThreadGroupAffinity(GetCurrentThread(), &Readback);
            ok(Success && Readback.Group == Group && Readback.Mask == Affinity.Mask,
               "Set (%u, 0x%Ix), read back (%u, 0x%Ix)\n",
               Group, (size_t)Affinity.Mask, Readback.Group, (size_t)Readback.Mask);
            Visited++;
        }
    }

    ZeroMemory(&Affinity, sizeof(Affinity));
    Affinity.Group = GroupCount;
    Affinity.Mask = 1;
    SetLastError(0xdeadbeef);
    Success = SetThreadGroupAffinity(GetCurrentThread(), &Affinity, NULL);
    ok(!Success && GetLastError() == ERROR_INVALID_PARAMETER,
       "A group that does not exist: %d, error %lu\n", Success, GetLastError());

    ZeroMemory(&Affinity, sizeof(Affinity));
    SetLastError(0xdeadbeef);
    Success = SetThreadGroupAffinity(GetCurrentThread(), &Affinity, NULL);
    ok(!Success && GetLastError() == ERROR_INVALID_PARAMETER,
       "An empty mask: %d, error %lu\n", Success, GetLastError());

    Success = SetThreadGroupAffinity(GetCurrentThread(), &Original, NULL);
    ok(Success, "Restoring the affinity failed: %lu\n", GetLastError());
    trace("PROCESSOR_GROUPS visited=%lu\n", Visited);
}

static VOID
TestEveryProcessorAtOnce(_In_ WORD GroupCount, _In_reads_(GroupCount) const DWORD *GroupSizes)
{
    DWORD Count = 0, Index, Number, Ran = 0, Strayed = 0, Unpinned = 0, Starved = 0;
    PSYSTEM_PROCESS_INFORMATION Before;
    ULONGLONG Deadline;
    WORD Group;

    StartPinned = StopPinned = PinnedReady = 0;
    for (Group = 0; Group < GroupCount; Group++)
    {
        for (Number = 0; Number < GroupSizes[Group] && Count < MAX_PROCESSORS; Number++)
        {
            Pinned[Count].Group = Group;
            Pinned[Count].Number = (BYTE)Number;
            Pinned[Count].Spins = Pinned[Count].Strayed = Pinned[Count].Pinned = 0;
            Pinned[Count].Thread = CreateThread(NULL, 0, PinnedThread, &Pinned[Count], 0, &Pinned[Count].ThreadId);
            ok(Pinned[Count].Thread != NULL, "CreateThread failed: %lu\n", GetLastError());
            if (Pinned[Count].Thread == NULL)
                break;
            Count++;
        }
    }

    Deadline = GetTickCount64() + JOIN_MS;
    while ((DWORD)PinnedReady != Count && GetTickCount64() < Deadline)
        Sleep(1);
    ok((DWORD)PinnedReady == Count, "%ld of %lu threads reached their processor\n", PinnedReady, Count);

    Before = SnapshotThreads();
    InterlockedExchange(&StartPinned, 1);
    Sleep(RUN_MS);

    for (Index = 0; Index < Count; Index++)
    {
        if (Pinned[Index].Spins == 0) Starved++;
    }
    if (Starved != 0)
        ReportStarvation(Before, Count);
    if (Before != NULL)
        HeapFree(GetProcessHeap(), 0, Before);

    InterlockedExchange(&StopPinned, 1);

    for (Index = 0; Index < Count; Index++)
    {
        ok(WaitForSingleObject(Pinned[Index].Thread, JOIN_MS) == WAIT_OBJECT_0, "Thread %lu did not finish\n", Index);
        CloseHandle(Pinned[Index].Thread);
        if (Pinned[Index].Spins != 0) Ran++;
        if (Pinned[Index].Strayed != 0) Strayed++;
        if (Pinned[Index].Pinned == 0) Unpinned++;
    }

    ok(Unpinned == 0, "%lu threads could not be confined to their processor\n", Unpinned);
    ok(Ran == Count, "%lu of %lu processors ran their thread\n", Ran, Count);
    ok(Strayed == 0, "%lu threads ran on a processor other than theirs\n", Strayed);
    trace("PROCESSOR_GROUPS concurrent=%lu ran=%lu strayed=%lu\n", Count, Ran, Strayed);
}

START_TEST(ProcessorGroups)
{
    DWORD GroupSizes[64] = { 0 };
    WORD GroupCount = 0;

    TestGroupCounts(&GroupCount, GroupSizes);
    if (GroupCount == 0)
        return;

    TestLegacyView(GroupCount, GroupSizes);
    TestVisitEveryProcessor(GroupCount, GroupSizes);
    TestEveryProcessorAtOnce(GroupCount, GroupSizes);
}
