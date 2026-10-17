/*
 * PROJECT:     LiberNT kernel-mode tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Kernel-Mode Test Suite for the resource arbiter library
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <kmt_test.h>
#include <arblib.h>

#define OWNER(n) ((PDEVICE_OBJECT)(ULONG_PTR)(0x1000 * (n)))

typedef struct _TEST_REQUEST
{
    ARBITER_LIST_ENTRY Entry;
    IO_RESOURCE_DESCRIPTOR Alternatives[2];
    CM_PARTIAL_RESOURCE_DESCRIPTOR Assignment;
} TEST_REQUEST, *PTEST_REQUEST;

static
NTSTATUS
NTAPI
TestUnpackRequirement(
    _In_ PIO_RESOURCE_DESCRIPTOR IoDescriptor,
    _Out_ PUINT64 Minimum,
    _Out_ PUINT64 Maximum,
    _Out_ PUINT64 Length,
    _Out_ PUINT64 Alignment)
{
    if (IoDescriptor->Type != CmResourceTypePort)
        return STATUS_INVALID_PARAMETER;
    *Minimum = (UINT64)IoDescriptor->u.Port.MinimumAddress.QuadPart;
    *Maximum = (UINT64)IoDescriptor->u.Port.MaximumAddress.QuadPart;
    *Length = IoDescriptor->u.Port.Length;
    *Alignment = IoDescriptor->u.Port.Alignment;
    return STATUS_SUCCESS;
}

static
NTSTATUS
NTAPI
TestPackResource(
    _In_ PIO_RESOURCE_DESCRIPTOR IoDescriptor,
    _In_ UINT64 Start,
    _Out_ PCM_PARTIAL_RESOURCE_DESCRIPTOR CmDescriptor)
{
    RtlZeroMemory(CmDescriptor, sizeof(*CmDescriptor));
    CmDescriptor->Type = CmResourceTypePort;
    CmDescriptor->ShareDisposition = IoDescriptor->ShareDisposition;
    CmDescriptor->u.Port.Start.QuadPart = (LONGLONG)Start;
    CmDescriptor->u.Port.Length = IoDescriptor->u.Port.Length;
    return STATUS_SUCCESS;
}

static
NTSTATUS
NTAPI
TestUnpackResource(
    _In_ PCM_PARTIAL_RESOURCE_DESCRIPTOR CmDescriptor,
    _Out_ PUINT64 Start,
    _Out_ PUINT64 Length)
{
    if (CmDescriptor->Type != CmResourceTypePort)
        return STATUS_INVALID_PARAMETER;
    *Start = (UINT64)CmDescriptor->u.Port.Start.QuadPart;
    *Length = CmDescriptor->u.Port.Length;
    return STATUS_SUCCESS;
}

static
VOID
SetPort(
    _Out_ PIO_RESOURCE_DESCRIPTOR Descriptor,
    _In_ ULONG Minimum,
    _In_ ULONG Maximum,
    _In_ ULONG Length,
    _In_ ULONG Alignment,
    _In_ UCHAR Share)
{
    RtlZeroMemory(Descriptor, sizeof(*Descriptor));
    Descriptor->Type = CmResourceTypePort;
    Descriptor->ShareDisposition = Share;
    Descriptor->u.Port.MinimumAddress.QuadPart = Minimum;
    Descriptor->u.Port.MaximumAddress.QuadPart = Maximum;
    Descriptor->u.Port.Length = Length;
    Descriptor->u.Port.Alignment = Alignment;
}

static
VOID
InitRequest(
    _Out_ PTEST_REQUEST Request,
    _In_ PDEVICE_OBJECT Owner,
    _In_ ULONG Minimum,
    _In_ ULONG Maximum,
    _In_ ULONG Length,
    _In_ ULONG Alignment,
    _In_ UCHAR Share)
{
    RtlZeroMemory(Request, sizeof(*Request));
    SetPort(&Request->Alternatives[0], Minimum, Maximum, Length, Alignment, Share);
    Request->Entry.AlternativeCount = 1;
    Request->Entry.Alternatives = Request->Alternatives;
    Request->Entry.PhysicalDeviceObject = Owner;
    Request->Entry.RequestSource = ArbiterRequestPnpEnumerated;
    Request->Entry.Assignment = &Request->Assignment;
    Request->Entry.Result = ArbiterResultUndefined;
}

static
NTSTATUS
InitArbiter(
    _Out_ PARBITER_INSTANCE Arbiter,
    _In_ PCWSTR OrderName)
{
    RtlZeroMemory(Arbiter, sizeof(*Arbiter));
    Arbiter->UnpackRequirement = TestUnpackRequirement;
    Arbiter->PackResource = TestPackResource;
    Arbiter->UnpackResource = TestUnpackResource;
    return ArbInitializeArbiterInstance(Arbiter, NULL, CmResourceTypePort, L"KmtestPort", OrderName, NULL);
}

static
NTSTATUS
Arbitrate(
    _In_ PARBITER_INSTANCE Arbiter,
    _In_ ARBITER_ACTION Action,
    _Inout_ PLIST_ENTRY List,
    _In_ ULONG AllocateFromCount,
    _In_opt_ PCM_PARTIAL_RESOURCE_DESCRIPTOR AllocateFrom)
{
    ARBITER_PARAMETERS Parameters;

    RtlZeroMemory(&Parameters, sizeof(Parameters));
    switch (Action)
    {
        case ArbiterActionTestAllocation:
            Parameters.Parameters.TestAllocation.ArbitrationList = List;
            Parameters.Parameters.TestAllocation.AllocateFromCount = AllocateFromCount;
            Parameters.Parameters.TestAllocation.AllocateFrom = AllocateFrom;
            break;
        case ArbiterActionRetestAllocation:
            Parameters.Parameters.RetestAllocation.ArbitrationList = List;
            break;
        case ArbiterActionBootAllocation:
            Parameters.Parameters.BootAllocation.ArbitrationList = List;
            break;
        default:
            break;
    }
    return ArbArbiterHandler(Arbiter, Action, &Parameters);
}

static
NTSTATUS
TestOne(
    _In_ PARBITER_INSTANCE Arbiter,
    _Inout_ PTEST_REQUEST Request)
{
    LIST_ENTRY List;

    InitializeListHead(&List);
    InsertTailList(&List, &Request->Entry.ListEntry);
    return Arbitrate(Arbiter, ArbiterActionTestAllocation, &List, 0, NULL);
}

static
NTSTATUS
CommitOne(
    _In_ PARBITER_INSTANCE Arbiter,
    _Inout_ PTEST_REQUEST Request)
{
    NTSTATUS Status = TestOne(Arbiter, Request);

    if (NT_SUCCESS(Status))
        Status = ArbArbiterHandler(Arbiter, ArbiterActionCommitAllocation, NULL);
    return Status;
}

static
ULONGLONG
AssignedStart(
    _In_ PTEST_REQUEST Request)
{
    return (ULONGLONG)Request->Assignment.u.Port.Start.QuadPart;
}

static
VOID
TestOrderingLists(VOID)
{
    ARBITER_ORDERING_LIST List;
    NTSTATUS Status;

    Status = ArbInitializeOrderingList(&List);
    ok_eq_hex(Status, STATUS_SUCCESS);
    Status = ArbAddOrdering(&List, 0, 0xFFFF);
    ok_eq_hex(Status, STATUS_SUCCESS);
    Status = ArbAddOrdering(&List, 0x20, 0x10);
    ok_eq_hex(Status, STATUS_INVALID_PARAMETER);
    Status = ArbPruneOrdering(&List, 0x100, 0x1FF);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok_eq_uint(List.Count, 2);
    if (List.Count == 2)
    {
        ok_eq_ulonglong(List.Orderings[0].Start, 0ULL);
        ok_eq_ulonglong(List.Orderings[0].End, 0xFFULL);
        ok_eq_ulonglong(List.Orderings[1].Start, 0x200ULL);
        ok_eq_ulonglong(List.Orderings[1].End, 0xFFFFULL);
    }
    Status = ArbPruneOrdering(&List, 0, 0xFF);
    ok_eq_hex(Status, STATUS_SUCCESS);
    Status = ArbPruneOrdering(&List, 0xFF00, 0x10FFF);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok_eq_uint(List.Count, 1);
    if (List.Count == 1)
    {
        ok_eq_ulonglong(List.Orderings[0].Start, 0x200ULL);
        ok_eq_ulonglong(List.Orderings[0].End, 0xFEFFULL);
    }
    ArbFreeOrderingList(&List);
    ok_eq_pointer(List.Orderings, NULL);
}

static
NTSTATUS
WriteOrderingValues(
    _In_ BOOLEAN Delete)
{
    static const WCHAR OrderKey[] = L"\\Registry\\Machine\\SYSTEM\\CurrentControlSet\\Control\\Arbiters\\AllocationOrder";
    static const WCHAR ReservedKey[] = L"\\Registry\\Machine\\SYSTEM\\CurrentControlSet\\Control\\Arbiters\\ReservedResources";
    static const WCHAR Redirect[] = L"KmtestReserved";
    UCHAR Buffer[FIELD_OFFSET(IO_RESOURCE_REQUIREMENTS_LIST, List[0].Descriptors) + 2 * sizeof(IO_RESOURCE_DESCRIPTOR)];
    PIO_RESOURCE_REQUIREMENTS_LIST Requirements = (PIO_RESOURCE_REQUIREMENTS_LIST)Buffer;
    UNICODE_STRING KeyName, ValueName;
    OBJECT_ATTRIBUTES ObjectAttributes;
    HANDLE Key;
    NTSTATUS Status;

    RtlInitUnicodeString(&KeyName, OrderKey);
    InitializeObjectAttributes(&ObjectAttributes, &KeyName, OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, NULL, NULL);
    Status = ZwCreateKey(&Key, KEY_ALL_ACCESS, &ObjectAttributes, 0, NULL, REG_OPTION_VOLATILE, NULL);
    if (!NT_SUCCESS(Status))
        return Status;
    RtlInitUnicodeString(&ValueName, L"KmtestOrder");
    if (Delete)
    {
        Status = ZwDeleteValueKey(Key, &ValueName);
    }
    else
    {
        RtlZeroMemory(Buffer, sizeof(Buffer));
        Requirements->ListSize = sizeof(Buffer);
        Requirements->AlternativeLists = 1;
        Requirements->List[0].Version = 1;
        Requirements->List[0].Revision = 1;
        Requirements->List[0].Count = 2;
        SetPort(&Requirements->List[0].Descriptors[0], 0x8000, 0x8FFF, 1, 1, CmResourceShareDeviceExclusive);
        SetPort(&Requirements->List[0].Descriptors[1], 0x1000, 0x1FFF, 1, 1, CmResourceShareDeviceExclusive);
        Status = ZwSetValueKey(Key, &ValueName, 0, REG_RESOURCE_REQUIREMENTS_LIST, Buffer, sizeof(Buffer));
    }
    ZwClose(Key);
    if (!NT_SUCCESS(Status))
        return Status;

    RtlInitUnicodeString(&KeyName, ReservedKey);
    InitializeObjectAttributes(&ObjectAttributes, &KeyName, OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, NULL, NULL);
    Status = ZwCreateKey(&Key, KEY_ALL_ACCESS, &ObjectAttributes, 0, NULL, REG_OPTION_VOLATILE, NULL);
    if (!NT_SUCCESS(Status))
        return Status;
    if (Delete)
    {
        RtlInitUnicodeString(&ValueName, L"KmtestOrder");
        ZwDeleteValueKey(Key, &ValueName);
        RtlInitUnicodeString(&ValueName, Redirect);
        Status = ZwDeleteValueKey(Key, &ValueName);
    }
    else
    {
        RtlInitUnicodeString(&ValueName, L"KmtestOrder");
        Status = ZwSetValueKey(Key, &ValueName, 0, REG_SZ, (PVOID)Redirect, sizeof(Redirect));
        if (NT_SUCCESS(Status))
        {
            RtlZeroMemory(Buffer, sizeof(Buffer));
            Requirements->ListSize = sizeof(Buffer) - sizeof(IO_RESOURCE_DESCRIPTOR);
            Requirements->AlternativeLists = 1;
            Requirements->List[0].Version = 1;
            Requirements->List[0].Revision = 1;
            Requirements->List[0].Count = 1;
            SetPort(&Requirements->List[0].Descriptors[0], 0x8400, 0x84FF, 1, 1, CmResourceShareDeviceExclusive);
            RtlInitUnicodeString(&ValueName, Redirect);
            Status = ZwSetValueKey(Key, &ValueName, 0, REG_RESOURCE_REQUIREMENTS_LIST, Buffer,
                                   sizeof(Buffer) - sizeof(IO_RESOURCE_DESCRIPTOR));
        }
    }
    ZwClose(Key);
    return Status;
}

static
VOID
TestRegistryOrdering(VOID)
{
    ARBITER_INSTANCE Arbiter;
    TEST_REQUEST Request;
    NTSTATUS Status;

    Status = WriteOrderingValues(FALSE);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
        return;

    Status = InitArbiter(&Arbiter, L"KmtestOrder");
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (NT_SUCCESS(Status))
    {
        ok_eq_uint(Arbiter.ReservedList.Count, 1);
        ok_eq_uint(Arbiter.OrderingList.Count, 3);
        if (Arbiter.OrderingList.Count == 3)
        {
            ok_eq_ulonglong(Arbiter.OrderingList.Orderings[0].Start, 0x8000ULL);
            ok_eq_ulonglong(Arbiter.OrderingList.Orderings[0].End, 0x83FFULL);
            ok_eq_ulonglong(Arbiter.OrderingList.Orderings[1].Start, 0x8500ULL);
            ok_eq_ulonglong(Arbiter.OrderingList.Orderings[1].End, 0x8FFFULL);
            ok_eq_ulonglong(Arbiter.OrderingList.Orderings[2].Start, 0x1000ULL);
            ok_eq_ulonglong(Arbiter.OrderingList.Orderings[2].End, 0x1FFFULL);
        }

        InitRequest(&Request, OWNER(1), 0, 0xFFFF, 0x100, 0x100, CmResourceShareDeviceExclusive);
        Status = TestOne(&Arbiter, &Request);
        ok_eq_hex(Status, STATUS_SUCCESS);
        ok_eq_ulonglong(AssignedStart(&Request), 0x8300ULL);
        Status = ArbArbiterHandler(&Arbiter, ArbiterActionRollbackAllocation, NULL);
        ok_eq_hex(Status, STATUS_SUCCESS);

        InitRequest(&Request, OWNER(1), 0, 0x7FFF, 0x100, 0x100, CmResourceShareDeviceExclusive);
        Status = TestOne(&Arbiter, &Request);
        ok_eq_hex(Status, STATUS_SUCCESS);
        ok_eq_ulonglong(AssignedStart(&Request), 0x1F00ULL);
        Status = ArbArbiterHandler(&Arbiter, ArbiterActionRollbackAllocation, NULL);
        ok_eq_hex(Status, STATUS_SUCCESS);

        InitRequest(&Request, OWNER(1), 0x8400, 0x84FF, 0x100, 0x100, CmResourceShareDeviceExclusive);
        Status = TestOne(&Arbiter, &Request);
        ok_eq_hex(Status, STATUS_SUCCESS);
        ok_eq_ulonglong(AssignedStart(&Request), 0x8400ULL);
        Status = ArbArbiterHandler(&Arbiter, ArbiterActionRollbackAllocation, NULL);
        ok_eq_hex(Status, STATUS_SUCCESS);

        ArbDeleteArbiterInstance(&Arbiter);
    }

    Status = WriteOrderingValues(TRUE);
    ok_eq_hex(Status, STATUS_SUCCESS);
}

static
VOID
TestAllocation(VOID)
{
    ARBITER_INSTANCE Arbiter;
    TEST_REQUEST First, Second, Third;
    PARBITER_CONFLICT_INFO Conflicts = NULL;
    ARBITER_PARAMETERS Parameters;
    CM_PARTIAL_RESOURCE_DESCRIPTOR Window;
    LIST_ENTRY List;
    ULONG ConflictCount = 0;
    NTSTATUS Status;

    Status = InitArbiter(&Arbiter, L"KmtestNoSuchOrder");
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
        return;
    ok_eq_uint(Arbiter.OrderingList.Count, 0);
    ok_eq_ulong(Arbiter.Signature, ARBITER_SIGNATURE);

    Status = ArbArbiterHandler(&Arbiter, ArbiterActionCommitAllocation, NULL);
    ok_eq_hex(Status, STATUS_INVALID_DEVICE_STATE);

    InitRequest(&First, OWNER(1), 0x1000, 0x1FFF, 0x100, 0x100, CmResourceShareDeviceExclusive);
    InitRequest(&Second, OWNER(2), 0x1000, 0x1FFF, 0x100, 0x100, CmResourceShareDeviceExclusive);
    InitializeListHead(&List);
    InsertTailList(&List, &First.Entry.ListEntry);
    InsertTailList(&List, &Second.Entry.ListEntry);
    Status = Arbitrate(&Arbiter, ArbiterActionTestAllocation, &List, 0, NULL);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok_eq_int(First.Entry.Result, ArbiterResultSuccess);
    ok_eq_int(Second.Entry.Result, ArbiterResultSuccess);
    ok_eq_pointer(First.Entry.SelectedAlternative, &First.Alternatives[0]);
    ok_eq_ulonglong(AssignedStart(&First), 0x1F00ULL);
    ok_eq_ulonglong(AssignedStart(&Second), 0x1E00ULL);
    ok_eq_ulong(First.Assignment.u.Port.Length, 0x100UL);
    Status = ArbArbiterHandler(&Arbiter, ArbiterActionCommitAllocation, NULL);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok_eq_bool(Arbiter.TransactionInProgress, FALSE);

    InitRequest(&Third, OWNER(3), 0x1F00, 0x1FFF, 0x100, 1, CmResourceShareDeviceExclusive);
    Status = TestOne(&Arbiter, &Third);
    ok_eq_hex(Status, STATUS_UNSUCCESSFUL);
    ok_eq_int(Third.Entry.Result, ArbiterResultExternalConflict);
    Status = ArbArbiterHandler(&Arbiter, ArbiterActionRollbackAllocation, NULL);
    ok_eq_hex(Status, STATUS_SUCCESS);

    InitRequest(&Third, OWNER(1), 0x1F00, 0x1FFF, 0x100, 1, CmResourceShareDeviceExclusive);
    Status = TestOne(&Arbiter, &Third);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok_eq_ulonglong(AssignedStart(&Third), 0x1F00ULL);
    Status = ArbArbiterHandler(&Arbiter, ArbiterActionRollbackAllocation, NULL);
    ok_eq_hex(Status, STATUS_SUCCESS);

    InitRequest(&Third, OWNER(3), 0x1000, 0x1FFF, 0x100, 0x100, CmResourceShareDeviceExclusive);
    Status = TestOne(&Arbiter, &Third);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok_eq_ulonglong(AssignedStart(&Third), 0x1D00ULL);
    Status = ArbArbiterHandler(&Arbiter, ArbiterActionRollbackAllocation, NULL);
    ok_eq_hex(Status, STATUS_SUCCESS);

    InitRequest(&First, OWNER(4), 0x3000, 0x30FF, 0x100, 1, CmResourceShareShared);
    Status = CommitOne(&Arbiter, &First);
    ok_eq_hex(Status, STATUS_SUCCESS);
    InitRequest(&Second, OWNER(5), 0x3000, 0x30FF, 0x100, 1, CmResourceShareShared);
    Status = TestOne(&Arbiter, &Second);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok_eq_ulonglong(AssignedStart(&Second), 0x3000ULL);
    Status = ArbArbiterHandler(&Arbiter, ArbiterActionRollbackAllocation, NULL);
    ok_eq_hex(Status, STATUS_SUCCESS);
    InitRequest(&Second, OWNER(5), 0x3000, 0x30FF, 0x100, 1, CmResourceShareDeviceExclusive);
    Status = TestOne(&Arbiter, &Second);
    ok_eq_hex(Status, STATUS_UNSUCCESSFUL);
    Status = ArbArbiterHandler(&Arbiter, ArbiterActionRollbackAllocation, NULL);
    ok_eq_hex(Status, STATUS_SUCCESS);

    InitRequest(&First, OWNER(6), 0x4000, 0x40FF, 0x100, 1, CmResourceShareDeviceExclusive);
    First.Entry.Flags = ARBITER_FLAG_BOOT_CONFIG;
    InitializeListHead(&List);
    InsertTailList(&List, &First.Entry.ListEntry);
    Status = Arbitrate(&Arbiter, ArbiterActionBootAllocation, &List, 0, NULL);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok_eq_bool(Arbiter.TransactionInProgress, FALSE);

    InitRequest(&Second, OWNER(7), 0x4000, 0x40FF, 0x100, 1, CmResourceShareDeviceExclusive);
    Second.Entry.Flags = ARBITER_FLAG_BOOT_CONFIG;
    Status = TestOne(&Arbiter, &Second);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok_eq_ulonglong(AssignedStart(&Second), 0x4000ULL);
    Status = ArbArbiterHandler(&Arbiter, ArbiterActionRollbackAllocation, NULL);
    ok_eq_hex(Status, STATUS_SUCCESS);
    InitRequest(&Second, OWNER(7), 0x4000, 0x40FF, 0x100, 1, CmResourceShareDeviceExclusive);
    Status = TestOne(&Arbiter, &Second);
    ok_eq_hex(Status, STATUS_UNSUCCESSFUL);
    Status = ArbArbiterHandler(&Arbiter, ArbiterActionRollbackAllocation, NULL);
    ok_eq_hex(Status, STATUS_SUCCESS);

    RtlZeroMemory(&Parameters, sizeof(Parameters));
    SetPort(&First.Alternatives[1], 0x4080, 0x40FF, 0x10, 1, CmResourceShareDeviceExclusive);
    Parameters.Parameters.QueryConflict.PhysicalDeviceObject = OWNER(7);
    Parameters.Parameters.QueryConflict.ConflictingResource = &First.Alternatives[1];
    Parameters.Parameters.QueryConflict.ConflictCount = &ConflictCount;
    Parameters.Parameters.QueryConflict.Conflicts = &Conflicts;
    Status = ArbArbiterHandler(&Arbiter, ArbiterActionQueryConflict, &Parameters);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok_eq_ulong(ConflictCount, 1UL);
    if (ConflictCount == 1 && Conflicts)
    {
        ok_eq_pointer(Conflicts[0].OwningObject, OWNER(6));
        ok_eq_ulonglong(Conflicts[0].Start, 0x4000ULL);
        ok_eq_ulonglong(Conflicts[0].End, 0x40FFULL);
    }
    if (Conflicts)
        ExFreePool(Conflicts);

    Conflicts = NULL;
    ConflictCount = 0;
    SetPort(&First.Alternatives[1], 0x4000, 0x4FFF, 0x10, 0x10, CmResourceShareDeviceExclusive);
    Status = ArbArbiterHandler(&Arbiter, ArbiterActionQueryConflict, &Parameters);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok_eq_ulong(ConflictCount, 0UL);
    ok_eq_pointer(Conflicts, NULL);

    InitRequest(&First, OWNER(8), 0, 0xFFFF, 0x10, 0x10, CmResourceShareDeviceExclusive);
    RtlZeroMemory(&Window, sizeof(Window));
    Window.Type = CmResourceTypePort;
    Window.u.Port.Start.QuadPart = 0x8000;
    Window.u.Port.Length = 0x100;
    InitializeListHead(&List);
    InsertTailList(&List, &First.Entry.ListEntry);
    Status = Arbitrate(&Arbiter, ArbiterActionTestAllocation, &List, 1, &Window);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok_eq_ulonglong(AssignedStart(&First), 0x80F0ULL);
    Status = ArbArbiterHandler(&Arbiter, ArbiterActionCommitAllocation, NULL);
    ok_eq_hex(Status, STATUS_SUCCESS);
    InitRequest(&Second, OWNER(12), 0, 0xFFFF, 0x10, 0x10, CmResourceShareDeviceExclusive);
    Status = TestOne(&Arbiter, &Second);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok_eq_ulonglong(AssignedStart(&Second), 0xFFF0ULL);
    Status = ArbArbiterHandler(&Arbiter, ArbiterActionRollbackAllocation, NULL);
    ok_eq_hex(Status, STATUS_SUCCESS);

    InitRequest(&First, OWNER(9), 0x5000, 0x5FFF, 0, 1, CmResourceShareDeviceExclusive);
    Status = TestOne(&Arbiter, &First);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok_eq_int(First.Entry.Result, ArbiterResultNullRequest);
    Status = ArbArbiterHandler(&Arbiter, ArbiterActionRollbackAllocation, NULL);
    ok_eq_hex(Status, STATUS_SUCCESS);

    InitRequest(&First, OWNER(10), 0x7000, 0x70FF, 0x100, 1, CmResourceShareDeviceExclusive);
    InitializeListHead(&List);
    InsertTailList(&List, &First.Entry.ListEntry);
    First.Assignment.Type = CmResourceTypePort;
    First.Assignment.u.Port.Start.QuadPart = 0x7000;
    First.Assignment.u.Port.Length = 0x100;
    Status = Arbitrate(&Arbiter, ArbiterActionRetestAllocation, &List, 0, NULL);
    ok_eq_hex(Status, STATUS_SUCCESS);
    Status = ArbArbiterHandler(&Arbiter, ArbiterActionCommitAllocation, NULL);
    ok_eq_hex(Status, STATUS_SUCCESS);
    InitRequest(&Second, OWNER(11), 0x7000, 0x70FF, 0x100, 1, CmResourceShareDeviceExclusive);
    Status = TestOne(&Arbiter, &Second);
    ok_eq_hex(Status, STATUS_UNSUCCESSFUL);
    Status = ArbArbiterHandler(&Arbiter, ArbiterActionRollbackAllocation, NULL);
    ok_eq_hex(Status, STATUS_SUCCESS);

    ArbDeleteArbiterInstance(&Arbiter);
}

static
VOID
TestBacktracking(VOID)
{
    ARBITER_INSTANCE Arbiter;
    TEST_REQUEST Blocker, Flexible, Choice;
    LIST_ENTRY List;
    NTSTATUS Status;

    Status = InitArbiter(&Arbiter, L"KmtestNoSuchOrder");
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
        return;

    InitRequest(&Blocker, OWNER(1), 0x40, 0x4F, 0x10, 1, CmResourceShareDeviceExclusive);
    Status = CommitOne(&Arbiter, &Blocker);
    ok_eq_hex(Status, STATUS_SUCCESS);

    InitRequest(&Flexible, OWNER(2), 0x00, 0x1F, 0x10, 0x10, CmResourceShareDeviceExclusive);
    InitRequest(&Choice, OWNER(3), 0x10, 0x1F, 0x10, 1, CmResourceShareDeviceExclusive);
    SetPort(&Choice.Alternatives[1], 0x40, 0x4F, 0x10, 1, CmResourceShareDeviceExclusive);
    Choice.Entry.AlternativeCount = 2;

    InitializeListHead(&List);
    InsertTailList(&List, &Flexible.Entry.ListEntry);
    InsertTailList(&List, &Choice.Entry.ListEntry);
    Status = Arbitrate(&Arbiter, ArbiterActionTestAllocation, &List, 0, NULL);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok_eq_ulonglong(AssignedStart(&Flexible), 0x00ULL);
    ok_eq_ulonglong(AssignedStart(&Choice), 0x10ULL);
    ok_eq_pointer(Choice.Entry.SelectedAlternative, &Choice.Alternatives[0]);
    Status = ArbArbiterHandler(&Arbiter, ArbiterActionCommitAllocation, NULL);
    ok_eq_hex(Status, STATUS_SUCCESS);

    InitRequest(&Flexible, OWNER(4), 0x00, 0x4F, 0x10, 0x10, CmResourceShareDeviceExclusive);
    Status = TestOne(&Arbiter, &Flexible);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok_eq_ulonglong(AssignedStart(&Flexible), 0x30ULL);
    Status = ArbArbiterHandler(&Arbiter, ArbiterActionRollbackAllocation, NULL);
    ok_eq_hex(Status, STATUS_SUCCESS);

    InitRequest(&Flexible, OWNER(4), 0x00, 0x2F, 0x30, 0x10, CmResourceShareDeviceExclusive);
    Status = TestOne(&Arbiter, &Flexible);
    ok_eq_hex(Status, STATUS_UNSUCCESSFUL);
    Status = ArbArbiterHandler(&Arbiter, ArbiterActionRollbackAllocation, NULL);
    ok_eq_hex(Status, STATUS_SUCCESS);

    ArbDeleteArbiterInstance(&Arbiter);
}

static
VOID
TestRootOrdering(VOID)
{
    ARBITER_INSTANCE Arbiter;
    NTSTATUS Status;
    ULONG Index;

    Status = InitArbiter(&Arbiter, L"Root");
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
        return;

    ok(Arbiter.OrderingList.Count > 0, "Root port ordering is empty\n");
    ok(Arbiter.ReservedList.Count > 0, "Root port reservations are empty\n");
    for (Index = 0; Index < Arbiter.OrderingList.Count; Index++)
    {
        ULONG Reserved;

        trace("Root port ordering %lu: %I64x-%I64x\n", Index,
              Arbiter.OrderingList.Orderings[Index].Start, Arbiter.OrderingList.Orderings[Index].End);
        ok(Arbiter.OrderingList.Orderings[Index].Start <= Arbiter.OrderingList.Orderings[Index].End,
           "Ordering %lu is inverted\n", Index);
        for (Reserved = 0; Reserved < Arbiter.ReservedList.Count; Reserved++)
        {
            ok(Arbiter.OrderingList.Orderings[Index].End < Arbiter.ReservedList.Orderings[Reserved].Start ||
               Arbiter.OrderingList.Orderings[Index].Start > Arbiter.ReservedList.Orderings[Reserved].End,
               "Ordering %lu overlaps reservation %lu\n", Index, Reserved);
        }
    }

    ArbDeleteArbiterInstance(&Arbiter);
}

START_TEST(IoArbiter)
{
    TestOrderingLists();
    TestRegistryOrdering();
    TestAllocation();
    TestBacktracking();
    TestRootOrdering();
}
