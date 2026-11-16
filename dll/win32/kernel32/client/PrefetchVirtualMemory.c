/*
 * PROJECT:     ReactOS Win32 Base API
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     PrefetchVirtualMemory
 * COPYRIGHT:   Adapted from Wine dlls/kernelbase/memory.c
 */

#include "k32_api.h"

C_ASSERT(sizeof(WIN32_MEMORY_RANGE_ENTRY) == sizeof(MEMORY_RANGE_ENTRY));
C_ASSERT(FIELD_OFFSET(WIN32_MEMORY_RANGE_ENTRY, VirtualAddress) == FIELD_OFFSET(MEMORY_RANGE_ENTRY, VirtualAddress));
C_ASSERT(FIELD_OFFSET(WIN32_MEMORY_RANGE_ENTRY, NumberOfBytes) == FIELD_OFFSET(MEMORY_RANGE_ENTRY, NumberOfBytes));

BOOL
WINAPI
PrefetchVirtualMemory(
    _In_ HANDLE hProcess,
    _In_ ULONG_PTR NumberOfEntries,
    _In_reads_(NumberOfEntries) PWIN32_MEMORY_RANGE_ENTRY VirtualAddresses,
    _In_ ULONG Flags)
{
    SYSTEM_INFO SystemInfo;
    ULONG_PTR EntryIndex;

    if (Flags != 0 || NumberOfEntries == 0 || !VirtualAddresses)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    NTSTATUS Status;
    ULONG PrefetchFlags = Flags;

    Status = NtSetInformationVirtualMemory(hProcess, 0, NumberOfEntries, (PMEMORY_RANGE_ENTRY)VirtualAddresses, &PrefetchFlags, sizeof(PrefetchFlags));
    if (Status != STATUS_NOT_SUPPORTED)
    {
        if (!NT_SUCCESS(Status))
        {
            BaseSetLastNTError(Status);
            return FALSE;
        }
        return TRUE;
    }

    GetSystemInfo(&SystemInfo);

    for (EntryIndex = 0; EntryIndex < NumberOfEntries; EntryIndex++)
    {
        ULONG_PTR Address = (ULONG_PTR)VirtualAddresses[EntryIndex].VirtualAddress;
        SIZE_T Remaining = VirtualAddresses[EntryIndex].NumberOfBytes;

        if (!Address || !Remaining || Address + Remaining < Address)
        {
            SetLastError(ERROR_INVALID_PARAMETER);
            return FALSE;
        }

        while (Remaining)
        {
            UCHAR Byte;
            SIZE_T Step = SystemInfo.dwPageSize - (Address & (SystemInfo.dwPageSize - 1));
            NTSTATUS Status = NtReadVirtualMemory(hProcess, (PVOID)Address, &Byte, sizeof(Byte), NULL);

            if (!NT_SUCCESS(Status))
            {
                BaseSetLastNTError(Status);
                return FALSE;
            }

            if (Step > Remaining)
                Step = Remaining;
            Address += Step;
            Remaining -= Step;
        }
    }

    return TRUE;
}
