/*
 * PROJECT:     LiberNT kernel-mode tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     The resource search starts at an aligned address, refuses a malformed large window and never wraps around
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
}
