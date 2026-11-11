/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     A 32-bit process under WoW64 sees 2 GB of user space, or 4 GB when its image is large address aware
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"

START_TEST(Wow64AddressLimit)
{
    SYSTEM_BASIC_INFORMATION Basic;
    PIMAGE_NT_HEADERS NtHeaders;
    ULONG_PTR Expected;
    ULONG_PTR Boundary;
    PVOID Address = NULL;
    SIZE_T Size = 0x10000;
    BOOL Wow64 = FALSE;
    NTSTATUS Status;

    if (!IsWow64Process(GetCurrentProcess(), &Wow64) || !Wow64)
    {
        skip("Not a WoW64 process\n");
        return;
    }

    NtHeaders = RtlImageNtHeader(NtCurrentPeb()->ImageBaseAddress);
    ok(NtHeaders != NULL, "No image headers\n");
    if (NtHeaders == NULL) return;

    Expected = (NtHeaders->FileHeader.Characteristics & IMAGE_FILE_LARGE_ADDRESS_AWARE) ?
               0xFFFEFFFF : 0x7FFEFFFF;
    Boundary = (NtHeaders->FileHeader.Characteristics & IMAGE_FILE_LARGE_ADDRESS_AWARE) ?
               0xFFFFFFFF : 0x7FFFFFFF;

    Status = NtQuerySystemInformation(SystemBasicInformation, &Basic, sizeof(Basic), NULL);
    ok_ntstatus(Status, STATUS_SUCCESS);
    ok(Basic.MaximumUserModeAddress == Expected,
       "MaximumUserModeAddress %p, expected %p\n", (PVOID)Basic.MaximumUserModeAddress, (PVOID)Expected);

    Status = NtAllocateVirtualMemory(NtCurrentProcess(), &Address, 0, &Size,
                                     MEM_RESERVE | MEM_TOP_DOWN, PAGE_NOACCESS);
    ok_ntstatus(Status, STATUS_SUCCESS);
    if (NT_SUCCESS(Status))
    {
        ok((ULONG_PTR)Address + Size - 1 <= Boundary,
           "top-down reservation at %p ends above %p\n", Address, (PVOID)Boundary);
        Size = 0;
        NtFreeVirtualMemory(NtCurrentProcess(), &Address, &Size, MEM_RELEASE);
    }
}
