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

static
VOID
TestHandleCount(VOID)
{
    SYSTEM_HANDLE_INFORMATION Handles;
    PSYSTEM_HANDLE_INFORMATION Buffer;
    ULONG Length = 0;
    ULONG Count;
    NTSTATUS Status;

    RtlFillMemory(&Handles, sizeof(Handles), 0xCC);
    Status = NtQuerySystemInformation(SystemHandleInformation, &Handles, sizeof(Handles), NULL);
    ok_hex(Status, STATUS_INFO_LENGTH_MISMATCH);
    ok(Handles.NumberOfHandles != 0xCCCCCCCC, "Handle count not written on a short buffer\n");
    Count = Handles.NumberOfHandles;

    RtlFillMemory(&Handles, sizeof(Handles), 0xCC);
    Status = NtQuerySystemInformation(SystemHandleInformation, &Handles, sizeof(Handles), &Length);
    ok_hex(Status, STATUS_INFO_LENGTH_MISMATCH);
    ok(Handles.NumberOfHandles != 0xCCCCCCCC, "Handle count not written on a short buffer\n");
    ok(Length > sizeof(Handles), "Handle information length %lu\n", Length);
    trace("Handle count %lu, length %lu\n", Handles.NumberOfHandles, Length);

    Length += 0x10000;
    Buffer = RtlAllocateHeap(RtlGetProcessHeap(), 0, Length);
    if (!Buffer)
    {
        skip("Could not allocate handle information\n");
        return;
    }
    Status = NtQuerySystemInformation(SystemHandleInformation, Buffer, Length, &Length);
    ok_hex(Status, STATUS_SUCCESS);
    if (NT_SUCCESS(Status))
    {
        ok(Buffer->NumberOfHandles > 1, "Handle count %lu\n", Buffer->NumberOfHandles);
        ok(Length == FIELD_OFFSET(SYSTEM_HANDLE_INFORMATION, Handles) + Buffer->NumberOfHandles * sizeof(Buffer->Handles[0]),
           "Length %lu for %lu handles\n", Length, Buffer->NumberOfHandles);
        ok(Count <= Buffer->NumberOfHandles + 64 && Count + 64 >= Buffer->NumberOfHandles,
           "Short-buffer count %lu, full count %lu\n", Count, Buffer->NumberOfHandles);
    }
    RtlFreeHeap(RtlGetProcessHeap(), 0, Buffer);
}

static VOID
TestShortModuleBuffer(VOID)
{
    struct
    {
        RTL_PROCESS_MODULES Modules;
        RTL_PROCESS_MODULE_INFORMATION Second;
        UCHAR Guard[16];
    } Short;
    PRTL_PROCESS_MODULES Full;
    ULONG Length = 0, FullLength = 0, Index;
    NTSTATUS Status;

    RtlFillMemory(&Short, sizeof(Short), 0xCC);
    Status = NtQuerySystemInformation(SystemModuleInformation, &Short, FIELD_OFFSET(RTL_PROCESS_MODULES, Modules), &Length);
    ok_hex(Status, STATUS_INFO_LENGTH_MISMATCH);
    ok(Short.Modules.NumberOfModules != 0xCCCCCCCC, "Module count not written on a header-only buffer\n");
    ok(Short.Modules.Modules[0].ImageSize == 0xCCCCCCCC, "A module was written past a header-only buffer\n");

    RtlFillMemory(&Short, sizeof(Short), 0xCC);
    Status = NtQuerySystemInformation(SystemModuleInformation, &Short, FIELD_OFFSET(RTL_PROCESS_MODULES, Modules) +
                                      2 * sizeof(RTL_PROCESS_MODULE_INFORMATION), &Length);
    ok_hex(Status, STATUS_INFO_LENGTH_MISMATCH);
    ok(Short.Modules.NumberOfModules != 0xCCCCCCCC && Short.Modules.NumberOfModules > 2,
       "Module count %lu on a two-module buffer\n", Short.Modules.NumberOfModules);
    ok(Short.Modules.Modules[0].ImageSize != 0xCCCCCCCC && (UCHAR)Short.Modules.Modules[0].FullPathName[0] != 0xCC,
       "First module not written on a two-module buffer\n");
    ok(Short.Second.ImageSize != 0xCCCCCCCC && (UCHAR)Short.Second.FullPathName[0] != 0xCC,
       "Second module not written on a two-module buffer\n");
    for (Index = 0; Index < sizeof(Short.Guard); Index++)
    {
        if (Short.Guard[Index] != 0xCC)
            break;
    }
    ok(Index == sizeof(Short.Guard), "Guard byte %lu written past a two-module buffer\n", Index);
    ok(Length == FIELD_OFFSET(RTL_PROCESS_MODULES, Modules) +
                 Short.Modules.NumberOfModules * sizeof(RTL_PROCESS_MODULE_INFORMATION),
       "Length %lu for %lu modules\n", Length, Short.Modules.NumberOfModules);
    trace("Short module buffer: count %lu, length %lu, first %s, second %s\n", Short.Modules.NumberOfModules, Length,
          Short.Modules.Modules[0].FullPathName, Short.Second.FullPathName);

    Full = RtlAllocateHeap(RtlGetProcessHeap(), 0, Length + 4096);
    if (!Full)
    {
        skip("Could not allocate module information\n");
        return;
    }
    Status = NtQuerySystemInformation(SystemModuleInformation, Full, Length + 4096, &FullLength);
    ok_hex(Status, STATUS_SUCCESS);
    if (NT_SUCCESS(Status) && Full->NumberOfModules >= 2)
    {
        ok(strcmp((PCSTR)Full->Modules[0].FullPathName, (PCSTR)Short.Modules.Modules[0].FullPathName) == 0,
           "First module differs: %s\n", Full->Modules[0].FullPathName);
        ok(strcmp((PCSTR)Full->Modules[1].FullPathName, (PCSTR)Short.Second.FullPathName) == 0,
           "Second module differs: %s\n", Full->Modules[1].FullPathName);
    }
    RtlFreeHeap(RtlGetProcessHeap(), 0, Full);
}

static
VOID
TestBootGraphics(VOID)
{
    SYSTEM_BOOT_ENVIRONMENT_INFORMATION Environment;
    SYSTEM_BOOT_GRAPHICS_INFORMATION Graphics;
    UCHAR Large[sizeof(Graphics) + 8];
    ULONG Length;
    NTSTATUS Status;

    RtlZeroMemory(&Environment, sizeof(Environment));
    Status = NtQuerySystemInformation(SystemBootEnvironmentInformation, &Environment, sizeof(Environment), NULL);
    if (!NT_SUCCESS(Status) || Environment.FirmwareType != FirmwareTypeUefi)
    {
        skip("boot graphics information is measured on UEFI firmware only\n");
        return;
    }

    RtlFillMemory(&Graphics, sizeof(Graphics), 0xA5);
    Length = 0;
    Status = NtQuerySystemInformation(SystemBootGraphicsInformation, &Graphics, sizeof(Graphics), &Length);
    ok_hex(Status, STATUS_SUCCESS);
    ok_long(Length, sizeof(Graphics));
    ok(Graphics.FrameBuffer.QuadPart == 0, "user mode was given the framebuffer address\n");
    ok(Graphics.Width != 0 && Graphics.Height != 0, "no boot framebuffer size\n");
    ok(Graphics.PixelStride >= Graphics.Width, "pixel stride %lu below the width %lu\n", Graphics.PixelStride, Graphics.Width);
    ok_long(Graphics.Flags, 0);

    RtlFillMemory(Large, sizeof(Large), 0xA5);
    Length = 0;
    Status = NtQuerySystemInformation(SystemBootGraphicsInformation, Large, sizeof(Large), &Length);
    ok_hex(Status, STATUS_INFO_LENGTH_MISMATCH);
    ok_long(Length, sizeof(Graphics));
    ok(Large[0] == 0xA5, "a refused query wrote the buffer\n");
}

START_TEST(NtQuerySystemInformation)
{
    TestBootGraphics();
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
    TestHandleCount();
    TestShortModuleBuffer();

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
