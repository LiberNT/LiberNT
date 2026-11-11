/*
 * PROJECT:     ReactOS Kernel
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     Test for NtQuerySystemInformation
 * COPYRIGHT:   Copyright 2019 Thomas Faber (thomas.faber@reactos.org)
 */

#include "precomp.h"

static
VOID
TestProcessImageName(VOID)
{
    PUNICODE_STRING ImagePath = &NtCurrentPeb()->ProcessParameters->ImagePathName;
    PSYSTEM_PROCESS_INFORMATION Info, Buffer;
    UNICODE_STRING BaseName;
    ULONG Length = 0x40000;
    USHORT Index;
    NTSTATUS Status;

    Index = ImagePath->Length / sizeof(WCHAR);
    while (Index != 0 && ImagePath->Buffer[Index - 1] != L'\\')
        Index--;
    BaseName.Buffer = &ImagePath->Buffer[Index];
    BaseName.Length = BaseName.MaximumLength = ImagePath->Length - Index * sizeof(WCHAR);

    Buffer = RtlAllocateHeap(RtlGetProcessHeap(), 0, Length);
    if (!Buffer)
    {
        skip("Could not allocate process information\n");
        return;
    }
    Status = NtQuerySystemInformation(SystemProcessInformation, Buffer, Length, &Length);
    ok_hex(Status, STATUS_SUCCESS);
    if (NT_SUCCESS(Status))
    {
        Info = Buffer;
        while (Info->UniqueProcessId != NtCurrentTeb()->ClientId.UniqueProcess && Info->NextEntryOffset != 0)
            Info = (PSYSTEM_PROCESS_INFORMATION)((PUCHAR)Info + Info->NextEntryOffset);
        ok(Info->UniqueProcessId == NtCurrentTeb()->ClientId.UniqueProcess, "Current process not listed\n");
        ok(RtlEqualUnicodeString(&Info->ImageName, &BaseName, TRUE),
           "Image name %.*ls, expected %.*ls\n",
           Info->ImageName.Length / (int)sizeof(WCHAR), Info->ImageName.Buffer,
           BaseName.Length / (int)sizeof(WCHAR), BaseName.Buffer);
    }
    RtlFreeHeap(RtlGetProcessHeap(), 0, Buffer);
}

START_TEST(NtQuerySystemInformation)
{
    NTSTATUS Status;
    ULONG Length = 0;
    PRTL_PROCESS_MODULES Modules;
    SYSTEM_BASIC_INFORMATION Basic;

    Status = NtQuerySystemInformation(0, NULL, 0, NULL);
    ok_hex(Status, STATUS_INFO_LENGTH_MISMATCH);

    Status = NtQuerySystemInformation(0x80000000, NULL, 0, NULL);
    ok_hex(Status, STATUS_INVALID_INFO_CLASS);

    RtlZeroMemory(&Basic, sizeof(Basic));
    Status = NtQuerySystemInformation(SystemBasicInformation, &Basic, sizeof(Basic), &Length);
    ok_hex(Status, STATUS_SUCCESS);
    if (NT_SUCCESS(Status))
    {
        ok(Basic.NumberOfPhysicalPages != 0, "No physical memory reported\n");
        ok(SharedUserData->NumberOfPhysicalPages == Basic.NumberOfPhysicalPages,
           "Shared physical pages %lu, system information %lu\n",
           SharedUserData->NumberOfPhysicalPages, Basic.NumberOfPhysicalPages);
        trace("Physical pages: shared %lu, queried %lu\n",
              SharedUserData->NumberOfPhysicalPages, Basic.NumberOfPhysicalPages);
    }

    TestProcessImageName();

    /* Both loaded-image lists must be initialized even when no user images
     * have been registered with the kernel's module list. */
    Status = NtQuerySystemInformation(SystemModuleInformation, NULL, 0, &Length);
    ok_hex(Status, STATUS_INFO_LENGTH_MISMATCH);
    ok(Length >= sizeof(*Modules), "Module information length %lu\n", Length);
    Modules = RtlAllocateHeap(RtlGetProcessHeap(), 0, Length + 4096);
    if (!Modules)
    {
        skip("Could not allocate module information\n");
        return;
    }
    Status = NtQuerySystemInformation(SystemModuleInformation, Modules, Length + 4096, &Length);
    ok_hex(Status, STATUS_SUCCESS);
    if (NT_SUCCESS(Status))
        ok(Modules->NumberOfModules != 0, "No kernel modules reported\n");
    RtlFreeHeap(RtlGetProcessHeap(), 0, Modules);
}
