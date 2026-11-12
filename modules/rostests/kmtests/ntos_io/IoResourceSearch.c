/*
 * PROJECT:     LiberNT kernel-mode tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     The resource search: aligned candidates, valid large windows, no wraparound, distinct concurrent settings
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <kmt_test.h>

/* ok_eq_hex evaluates its value twice: a call with an effect goes through this once */
#define ok_once(call, expected) \
    do { NTSTATUS Once_ = (call); ok(Once_ == (expected), #call " = 0x%08lx, expected 0x%08lx\n", Once_, (NTSTATUS)(expected)); } while (0)

#define TOP_PAGE 0xFFFFFFFFFFFFF000ULL

static UNICODE_STRING RegistryPath = RTL_CONSTANT_STRING(L"\\Registry\\Machine\\System\\CurrentControlSet\\Services\\Kmtest");

static
NTSTATUS
Assign(
    _In_ PIO_RESOURCE_DESCRIPTOR Requirement,
    _Out_ PULONGLONG Start)
{
    IO_RESOURCE_REQUIREMENTS_LIST List;
    PCM_RESOURCE_LIST Allocated = NULL;
    NTSTATUS Status;

    RtlZeroMemory(&List, sizeof(List));
    List.ListSize = sizeof(List);
    List.InterfaceType = Internal;
    List.AlternativeLists = 1;
    List.List[0].Version = 1;
    List.List[0].Revision = 1;
    List.List[0].Count = 1;
    List.List[0].Descriptors[0] = *Requirement;

    *Start = 0;
    Status = IoAssignResources(&RegistryPath, NULL, KmtDriverObject, NULL, &List, &Allocated);
    if (NT_SUCCESS(Status) && Allocated != NULL)
    {
        RtlCmDecodeMemIoResource(&Allocated->List[0].PartialResourceList.PartialDescriptors[0], Start);
        ExFreePool(Allocated);
    }
    return Status;
}

static
VOID
DescribeMemory(
    _Out_ PIO_RESOURCE_DESCRIPTOR Requirement,
    _In_ ULONGLONG Minimum,
    _In_ ULONGLONG Maximum,
    _In_ ULONG Length,
    _In_ ULONG Alignment)
{
    RtlZeroMemory(Requirement, sizeof(*Requirement));
    Requirement->Type = CmResourceTypeMemory;
    Requirement->ShareDisposition = CmResourceShareDeviceExclusive;
    Requirement->Flags = CM_RESOURCE_MEMORY_READ_WRITE;
    Requirement->u.Memory.Length = Length;
    Requirement->u.Memory.Alignment = Alignment;
    Requirement->u.Memory.MinimumAddress.QuadPart = (LONGLONG)Minimum;
    Requirement->u.Memory.MaximumAddress.QuadPart = (LONGLONG)Maximum;
}

#define LIST_WINDOW 0x730000000ULL
#define LIST_WINDOW_LENGTH 0x1000000ULL

static
VOID
CheckDescriptorsOfOneList(VOID)
{
    static const ULONG Lengths[2] = { 0x10000, 0x8000 };
    PIO_RESOURCE_REQUIREMENTS_LIST List;
    PCM_RESOURCE_LIST Allocated = NULL;
    PCM_PARTIAL_RESOURCE_DESCRIPTOR Partial;
    ULONGLONG Start[2] = { 0, 0 }, Length[2] = { 0, 0 };
    ULONG Size, Index;
    NTSTATUS Status;

    Size = sizeof(IO_RESOURCE_REQUIREMENTS_LIST) + sizeof(IO_RESOURCE_DESCRIPTOR);
    List = ExAllocatePoolZero(PagedPool, Size, 'sRmK');
    ok(List != NULL, "no memory for the requirements\n");
    if (List == NULL) return;

    List->ListSize = Size;
    List->InterfaceType = Internal;
    List->AlternativeLists = 1;
    List->List[0].Version = 1;
    List->List[0].Revision = 1;
    List->List[0].Count = 2;
    for (Index = 0; Index < 2; Index++)
    {
        DescribeMemory(&List->List[0].Descriptors[Index], LIST_WINDOW, LIST_WINDOW + LIST_WINDOW_LENGTH - 1,
                       Lengths[Index], Lengths[Index]);
    }

    Status = IoAssignResources(&RegistryPath, NULL, KmtDriverObject, NULL, List, &Allocated);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (NT_SUCCESS(Status) && Allocated != NULL)
    {
        ok_eq_ulong(Allocated->Count, 1UL);
        ok_eq_ulong(Allocated->List[0].PartialResourceList.Count, 2UL);
        Partial = Allocated->List[0].PartialResourceList.PartialDescriptors;
        for (Index = 0; Index < 2 && Index < Allocated->List[0].PartialResourceList.Count; Index++)
        {
            Length[Index] = RtlCmDecodeMemIoResource(&Partial[Index], &Start[Index]);
            ok(Length[Index] == Lengths[Index], "setting %lu is 0x%I64x bytes long\n", Index, Length[Index]);
            ok((Start[Index] % Lengths[Index]) == 0 && Start[Index] >= LIST_WINDOW &&
               Start[Index] + Length[Index] <= LIST_WINDOW + LIST_WINDOW_LENGTH,
               "setting %lu at 0x%I64x is not aligned within the requirement\n", Index, Start[Index]);
        }
        ok(Start[0] + Length[0] <= Start[1] || Start[1] + Length[1] <= Start[0],
           "the settings 0x%I64x+0x%I64x and 0x%I64x+0x%I64x of one list overlap\n", Start[0], Length[0], Start[1], Length[1]);
        ExFreePool(Allocated);
    }

    ExFreePoolWithTag(List, 'sRmK');
}

#define RACE_THREADS 8
#define RACE_WINDOW 0x720000000ULL
#define RACE_WINDOW_LENGTH 0x1000000ULL

typedef struct _RACE_ASSIGNMENT
{
    volatile LONG *Arrived;
    LONG Threads;
    KAFFINITY Affinity;
    ULONGLONG Start;
    NTSTATUS Status;
} RACE_ASSIGNMENT, *PRACE_ASSIGNMENT;

typedef struct _RACE_STATE
{
    volatile LONG Arrived;
    RACE_ASSIGNMENT Race[RACE_THREADS];
} RACE_STATE, *PRACE_STATE;

/* One thread per processor; all of them search at the same moment */
static
VOID
NTAPI
RaceAssign(
    _In_ PVOID Context)
{
    PRACE_ASSIGNMENT Race = Context;
    IO_RESOURCE_DESCRIPTOR Requirement;
    ULONGLONG Deadline;

    KeSetSystemAffinityThread(Race->Affinity);
    KeSetPriorityThread(KeGetCurrentThread(), LOW_REALTIME_PRIORITY);
    DescribeMemory(&Requirement, RACE_WINDOW, RACE_WINDOW + RACE_WINDOW_LENGTH - 1, 0x1000, 0x1000);

    Deadline = KeQueryInterruptTime() + 10 * 1000 * 1000;
    InterlockedIncrement(Race->Arrived);
    while (InterlockedCompareExchange(Race->Arrived, 0, 0) < Race->Threads && KeQueryInterruptTime() < Deadline)
        YieldProcessor();

    Race->Status = Assign(&Requirement, &Race->Start);
    KeRevertToUserAffinityThread();
    PsTerminateSystemThread(STATUS_SUCCESS);
}

/* Assignments asked for at once from one window never get the same page; the window has room for many runs of the test */
static
VOID
CheckConcurrentAssignments(VOID)
{
    PRACE_STATE State;
    PRACE_ASSIGNMENT Race;
    PVOID Threads[RACE_THREADS];
    LARGE_INTEGER Timeout;
    HANDLE Handle;
    ULONG Index, Other, Count, Shared = 0, Failed = 0, Created = 0, Joined = 0;
    NTSTATUS Status;

    Count = min((ULONG)KeNumberProcessors, (ULONG)RACE_THREADS);
    if (skip(Count >= 2, "One processor: nothing runs at once\n")) return;

    State = ExAllocatePoolZero(NonPagedPool, sizeof(*State), 'sRmK');
    ok(State != NULL, "no memory for the assignments\n");
    if (State == NULL) return;
    Race = State->Race;

    for (Index = 0; Index < Count; Index++)
    {
        Race[Index].Arrived = &State->Arrived;
        Race[Index].Threads = (LONG)Count;
        Race[Index].Affinity = AFFINITY_MASK(Index);
        Race[Index].Status = STATUS_PENDING;
        Threads[Index] = NULL;
        Status = PsCreateSystemThread(&Handle, THREAD_ALL_ACCESS, NULL, NULL, NULL, RaceAssign, &Race[Index]);
        ok_eq_hex(Status, STATUS_SUCCESS);
        if (!NT_SUCCESS(Status)) continue;
        Created++;
        Status = ObReferenceObjectByHandle(Handle, SYNCHRONIZE, *PsThreadType, KernelMode, &Threads[Index], NULL);
        ok_eq_hex(Status, STATUS_SUCCESS);
        ZwClose(Handle);
    }

    Timeout.QuadPart = -5LL * 1000 * 1000 * 10;
    for (Index = 0; Index < Count; Index++)
    {
        if (Threads[Index] == NULL) continue;
        Status = KeWaitForSingleObject(Threads[Index], Executive, KernelMode, FALSE, &Timeout);
        ok_eq_hex(Status, STATUS_SUCCESS);
        if (Status == STATUS_SUCCESS) Joined++;
        ObDereferenceObject(Threads[Index]);
    }

    for (Index = 0; Index < Count; Index++)
    {
        if (Threads[Index] == NULL) continue;
        if (!NT_SUCCESS(Race[Index].Status))
        {
            Failed++;
            continue;
        }
        for (Other = Index + 1; Other < Count; Other++)
        {
            if (Threads[Other] != NULL && NT_SUCCESS(Race[Other].Status) && Race[Other].Start == Race[Index].Start) Shared++;
        }
    }

    ok_eq_ulong(Failed, 0UL);
    ok_eq_ulong(Shared, 0UL);

    /* A thread still running owns its slot; the slots stay */
    if (Joined == Created) ExFreePoolWithTag(State, 'sRmK');
}

START_TEST(IoResourceSearch)
{
    IO_RESOURCE_DESCRIPTOR Requirement;
    CM_RESOURCE_LIST Claim;
    BOOLEAN Conflict;
    ULONGLONG Start;
    NTSTATUS Status;

    /* The candidates are aligned up from the minimum */
    DescribeMemory(&Requirement, 0x700001001ULL, 0x7000FFFFFULL, 0x1000, 0x1000);
    ok_once(Assign(&Requirement, &Start), STATUS_SUCCESS);
    ok((Start % 0x1000) == 0 && Start > 0x700001001ULL && Start + 0xFFF <= 0x7000FFFFFULL,
       "the window starts at 0x%I64x, not aligned within the requirement\n", Start);

    /* A large window carries exactly one CM_RESOURCE_MEMORY_LARGE flag */
    RtlZeroMemory(&Requirement, sizeof(Requirement));
    Requirement.Type = CmResourceTypeMemoryLarge;
    Requirement.ShareDisposition = CmResourceShareDeviceExclusive;
    Requirement.u.Memory.Length = 2;
    Requirement.u.Memory.Alignment = 1;
    Requirement.u.Memory.MinimumAddress.QuadPart = 0x800000000LL;
    Requirement.u.Memory.MaximumAddress.QuadPart = 0x8FFFFFFFFLL;
    ok(!NT_SUCCESS(Assign(&Requirement, &Start)), "a large window without a size flag got 0x%I64x\n", Start);
    Requirement.Flags = CM_RESOURCE_MEMORY_LARGE_40 | CM_RESOURCE_MEMORY_LARGE_48;
    ok(!NT_SUCCESS(Assign(&Requirement, &Start)), "a large window with two size flags got 0x%I64x\n", Start);

    /* A range that ends at the top of the address space overlaps a second claim of it */
    RtlZeroMemory(&Claim, sizeof(Claim));
    Claim.Count = 1;
    Claim.List[0].InterfaceType = Internal;
    Claim.List[0].PartialResourceList.Version = 1;
    Claim.List[0].PartialResourceList.Revision = 1;
    Claim.List[0].PartialResourceList.Count = 1;
    Claim.List[0].PartialResourceList.PartialDescriptors[0].Type = CmResourceTypeMemory;
    Claim.List[0].PartialResourceList.PartialDescriptors[0].ShareDisposition = CmResourceShareDeviceExclusive;
    Claim.List[0].PartialResourceList.PartialDescriptors[0].Flags = CM_RESOURCE_MEMORY_READ_WRITE;
    Claim.List[0].PartialResourceList.PartialDescriptors[0].u.Memory.Start.QuadPart = (LONGLONG)TOP_PAGE;
    Claim.List[0].PartialResourceList.PartialDescriptors[0].u.Memory.Length = 0x1000;

    Conflict = TRUE;
    Status = IoReportResourceUsage(NULL, KmtDriverObject, &Claim, sizeof(Claim), NULL, NULL, 0, FALSE, &Conflict);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok(!Conflict, "the first claim of the top page conflicted\n");

    Conflict = FALSE;
    Status = IoReportResourceUsage(NULL, KmtDriverObject, &Claim, sizeof(Claim), NULL, NULL, 0, FALSE, &Conflict);
    ok_eq_hex(Status, STATUS_CONFLICTING_ADDRESSES);
    ok(Conflict, "a second claim of the top page did not conflict\n");

    /* The search does not step past the top to address 0 */
    DescribeMemory(&Requirement, TOP_PAGE, MAXULONGLONG, 0x1000, 0x1000);
    Status = Assign(&Requirement, &Start);
    ok(!NT_SUCCESS(Status) || Start >= TOP_PAGE, "the search wrapped around to 0x%I64x\n", Start);

    CheckDescriptorsOfOneList();
    CheckConcurrentAssignments();
}
