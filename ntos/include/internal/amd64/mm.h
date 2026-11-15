/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     amd64 address space limits, fault codes and page table self-map lookups
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

#define _MI_PAGING_LEVELS 4
#define MI_AMD64_VIRTUAL_ADDRESS_BITS 48

#define MI_USER_PROBE_ADDRESS ((PVOID)((ULONG_PTR)MI_HIGHEST_USER_ADDRESS + 1))
#define MI_LOADER_MAPPINGS 0xFFFFF80000000000ULL
#define MM_HIGHEST_USER_ADDRESS_WOW64 0x7FFEFFFF
#define MM_HIGHEST_VAD_ADDRESS ((PVOID)((ULONG_PTR)MM_HIGHEST_USER_ADDRESS - (16 * PAGE_SIZE)))
#define MI_MAX_ZERO_BITS 53

#define MI_IS_WRITE_ACCESS(FaultCode) BooleanFlagOn(FaultCode, 0x00000002)
#define MI_IS_INSTRUCTION_FETCH(FaultCode) BooleanFlagOn(FaultCode, 0x00000010)

FORCEINLINE
PMMPTE
MiAmd64TableEntry(
    _In_ ULONG64 TableBase,
    _In_ ULONG Shift,
    _In_ PVOID Address)
{
    ULONG64 IndexMask = (1ULL << (MI_AMD64_VIRTUAL_ADDRESS_BITS - Shift)) - 1;
    ULONG64 Index = ((ULONG64)(ULONG_PTR)Address >> Shift) & IndexMask;

    return (PMMPTE)(ULONG_PTR)(TableBase + Index * sizeof(MMPTE));
}

FORCEINLINE
PMMPTE
_MiAddressToPte(
    _In_ PVOID Address)
{
    return MiAmd64TableEntry(PTE_BASE, PTI_SHIFT, Address);
}

#define MiAddressToPte(Address) _MiAddressToPte((PVOID)(Address))
#define MiAddressToPde(Address) MiAmd64TableEntry(PDE_BASE, PDI_SHIFT, (PVOID)(Address))
#define MiAddressToPpe(Address) MiAmd64TableEntry(PPE_BASE, PPI_SHIFT, (PVOID)(Address))
#define MiAddressToPxe(Address) MiAmd64TableEntry(PXE_BASE, PXI_SHIFT, (PVOID)(Address))
