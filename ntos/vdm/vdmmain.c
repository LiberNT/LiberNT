/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Virtual DOS machine services: control calls, initialization and the BIOS area
 * COPYRIGHT:   Copyright Alex Ionescu <alex.ionescu@reactos.org>
 *              Copyright Aleksey Bragin <aleksey@reactos.org>
 *              Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

CODE_SEG("INIT")
VOID
NTAPI
Ki386VdmEnablePentiumExtentions(IN BOOLEAN Enable)
{
    ULONG EFlags, Cr4;

    EFlags = __readeflags();
    _disable();

    Cr4 = __readcr4();
    __writecr4(Enable ? Cr4 | CR4_VME : Cr4 & ~CR4_VME);

    __writeeflags(EFlags);
}

CODE_SEG("INIT")
VOID
NTAPI
KeI386VdmInitialize(VOID)
{
    NTSTATUS Status;
    OBJECT_ATTRIBUTES ObjectAttributes;
    HANDLE RegHandle;
    UNICODE_STRING Name;
    UCHAR KeyValueInfo[sizeof(KEY_VALUE_BASIC_INFORMATION) + 30];
    ULONG ReturnLength;

    RtlInitUnicodeString(&Name,
                         L"\\Registry\\Machine\\System\\CurrentControlSet\\"
                         L"Control\\Wow");
    InitializeObjectAttributes(&ObjectAttributes,
                               &Name,
                               OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE,
                               NULL,
                               NULL);
    Status = ZwOpenKey(&RegHandle, KEY_READ, &ObjectAttributes);
    if (!NT_SUCCESS(Status)) return;

    RtlInitUnicodeString(&Name, L"DisableVme");
    Status = ZwQueryValueKey(RegHandle,
                             &Name,
                             KeyValueBasicInformation,
                             &KeyValueInfo,
                             sizeof(KeyValueInfo),
                             &ReturnLength);
    if (!NT_SUCCESS(Status))
    {
        if (KeGetPcr()->Prcb->FeatureBits & KF_V86_VIS)
        {
            /* Enable them. FIXME: Use IPI */
            Ki386VdmEnablePentiumExtentions(TRUE);
            KeI386VirtualIntExtensions = TRUE;
        }
    }

    ZwClose(RegHandle);
}

NTSTATUS
NTAPI
VdmpInitialize(
    PVOID ControlData)
{
    OBJECT_ATTRIBUTES ObjectAttributes;
    UNICODE_STRING PhysMemName = RTL_CONSTANT_STRING(L"\\Device\\PhysicalMemory");
    NTSTATUS Status;
    HANDLE PhysMemHandle;
    PVOID BaseAddress;
    volatile PVOID NullAddress = NULL;
    LARGE_INTEGER Offset;
    ULONG ViewSize;

    InitializeObjectAttributes(&ObjectAttributes,
                               &PhysMemName,
                               OBJ_KERNEL_HANDLE,
                               NULL,
                               NULL);
    Status = ZwOpenSection(&PhysMemHandle,
                           SECTION_ALL_ACCESS,
                           &ObjectAttributes);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("Couldn't open \\Device\\PhysicalMemory\n");
        return Status;
    }

    Offset.QuadPart = 0;
    ViewSize = PAGE_SIZE;
    BaseAddress = 0;
    Status = ZwMapViewOfSection(PhysMemHandle,
                                NtCurrentProcess(),
                                &BaseAddress,
                                0,
                                ViewSize,
                                &Offset,
                                &ViewSize,
                                ViewUnmap,
                                0,
                                PAGE_READWRITE);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("Couldn't map physical memory (%x)\n", Status);
        ZwClose(PhysMemHandle);
        return Status;
    }

    _SEH2_TRY
    {
        RtlMoveMemory(NullAddress, BaseAddress, ViewSize);
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        DPRINT1("Couldn't copy first page (%x)\n", Status);
        ZwClose(PhysMemHandle);
        ZwUnmapViewOfSection(NtCurrentProcess(), BaseAddress);
        _SEH2_YIELD(return _SEH2_GetExceptionCode());
    }
    _SEH2_END;

    ZwClose(PhysMemHandle);

    Status = ZwUnmapViewOfSection(NtCurrentProcess(), BaseAddress);

    if (!NT_SUCCESS(Status))
    {
        DPRINT1("Couldn't unmap the section (%x)\n", Status);
        return Status;
    }

    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
NtVdmControl(
    _In_ ULONG ControlCode,
    _In_ PVOID ControlData)
{
    NTSTATUS Status;
    PAGED_CODE();

    switch (ControlCode)
    {
        case VdmStartExecution:

            Status = VdmpStartExecution();
            break;

        case VdmInitialize:

            Status = VdmpInitialize(ControlData);
            break;

        default:

            DPRINT1("Unknown VDM call: %lx\n", ControlCode);
            Status = STATUS_INVALID_PARAMETER;
    }

    return Status;
}
