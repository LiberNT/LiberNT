/*
 * PROJECT:     LiberNT kernel-mode tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test that system views can use the system address space the system PTEs do not use
 * COPYRIGHT:   Copyright 2026 mikuta0407 <mikuta0407@mikuta0407.net>
 */

#include <kmt_test.h>

#define TEST_VIEW_SIZE      (16 * 1024 * 1024)
#define TEST_VIEW_TARGET    (256 * 1024 * 1024 / TEST_VIEW_SIZE)
#define TEST_MAPPING_SIZE   (80 * 1024 * 1024)

START_TEST(MmSystemViewSpace)
{
    PVOID Views[TEST_VIEW_TARGET];
    LARGE_INTEGER Maximum;
    HANDLE Section = NULL;
    PVOID SectionObject = NULL;
    PVOID Mapping;
    SIZE_T ViewSize;
    ULONG Count = 0;
    ULONG Index;
    NTSTATUS Status = STATUS_SUCCESS;

    Maximum.QuadPart = TEST_VIEW_SIZE;
    Status = ZwCreateSection(&Section, SECTION_ALL_ACCESS, NULL, &Maximum, PAGE_READWRITE, SEC_COMMIT, NULL);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
        return;

    Status = ObReferenceObjectByHandle(Section, SECTION_ALL_ACCESS, NULL, KernelMode, &SectionObject, NULL);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
        goto Finish;

    while (Count < TEST_VIEW_TARGET)
    {
        Views[Count] = NULL;
        ViewSize = 0;
        Status = MmMapViewInSystemSpace(SectionObject, &Views[Count], &ViewSize);
        if (!NT_SUCCESS(Status))
            break;
        ok(ViewSize >= TEST_VIEW_SIZE, "view %lu: size %Iu\n", Count, ViewSize);
        Count++;
    }
    trace("mapped %lu views of %lu MB\n", Count, TEST_VIEW_SIZE >> 20);
    ok(Count == TEST_VIEW_TARGET, "only %lu MB of system views could be mapped, last status 0x%lx\n",
       Count * (TEST_VIEW_SIZE >> 20), Status);

    for (Index = 0; Index < Count; Index++)
    {
        ((volatile ULONG *)Views[Index])[Index] = 0x56490000 | Index;
        ok_eq_hex(((volatile ULONG *)Views[0])[Index], 0x56490000 | Index);
    }

    Mapping = MmAllocateMappingAddress(TEST_MAPPING_SIZE, 'SVmK');
    ok(Mapping != NULL, "no %lu MB of system PTEs while the views are mapped\n", TEST_MAPPING_SIZE >> 20);
    if (Mapping != NULL)
        MmFreeMappingAddress(Mapping, 'SVmK');

    while (Count != 0)
    {
        Status = MmUnmapViewInSystemSpace(Views[--Count]);
        ok_eq_hex(Status, STATUS_SUCCESS);
    }

Finish:
    if (SectionObject != NULL)
        ObDereferenceObject(SectionObject);
    ZwClose(Section);
}
