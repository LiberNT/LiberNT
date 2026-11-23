/*
 * PROJECT:     LiberNT Win32 Base API
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Control Flow Guard dynamic target registration
 */

#include "k32_api.h"

NTSTATUS NTAPI RtlSetCfgTargetValidity(PVOID Base, SIZE_T Size, ULONG Count, PCFG_CALL_TARGET_INFO Targets);

BOOL
WINAPI
SetProcessValidCallTargets(
    _In_ HANDLE hProcess,
    _In_ PVOID VirtualAddress,
    _In_ SIZE_T RegionSize,
    _In_ ULONG NumberOfOffsets,
    _Inout_updates_(NumberOfOffsets) PCFG_CALL_TARGET_INFO OffsetInformation)
{
    NTSTATUS Status;

#ifdef _M_AMD64
    ULONG Index, Processed = 0;
    MEMORY_RANGE_ENTRY Range = { VirtualAddress, RegionSize };
    CFG_CALL_TARGET_LIST_INFORMATION List = { NumberOfOffsets, 0, &Processed, OffsetInformation, NULL, 0 };
    Status = NtSetInformationVirtualMemory(hProcess, VmCfgCallTargetInformation, 1, &Range, &List, sizeof(List));
    for (Index = 0; Index < Processed; ++Index)
        OffsetInformation[Index].Flags |= CFG_CALL_TARGET_PROCESSED;
#else
    if (hProcess != GetCurrentProcess())
    {
        SetLastError(ERROR_NOT_SUPPORTED);
        return FALSE;
    }
    Status = RtlSetCfgTargetValidity(VirtualAddress, RegionSize, NumberOfOffsets, OffsetInformation);
#endif
    if (!NT_SUCCESS(Status))
    {
        BaseSetLastNTError(Status);
        return FALSE;
    }
    return TRUE;
}
