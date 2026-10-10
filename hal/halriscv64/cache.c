/*
 * PROJECT:     LiberNT RISC-V HAL
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     External cache maintenance for the SiFive composable cache
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntifs.h>
#include <ndk/haltypes.h>
#include <reactos/riscv64/fdtlib.h>
#include "halp.h"

#define RISCV_CCACHE_FLUSH64 0x200

static ULONG64 HalpRiscvCcacheAddress;
static ULONG HalpRiscvCcacheLine;
static PUCHAR HalpRiscvCcache;

static
VOID
NTAPI
HalpRiscvFlushIoRectangleExternalCache(
    _In_ PMDL Mdl,
    _In_ ULONG StartOffset,
    _In_ ULONG Width,
    _In_ ULONG Height,
    _In_ ULONG Stride,
    _In_ BOOLEAN ReadOperation)
{
    PPFN_NUMBER Pages = MmGetMdlPfnArray(Mdl);
    ULONG64 Line, End;
    ULONG_PTR Offset;
    ULONG Row, Remaining, InPage, Chunk;

    UNREFERENCED_PARAMETER(ReadOperation);

    __asm__ __volatile__("fence rw, rw" ::: "memory");
    for (Row = 0; Row < Height; ++Row)
    {
        Offset = Mdl->ByteOffset + StartOffset + (ULONG_PTR)Row * Stride;
        for (Remaining = Width; Remaining; Remaining -= Chunk, Offset += Chunk)
        {
            InPage = (ULONG)(Offset & (PAGE_SIZE - 1));
            Chunk = min(Remaining, PAGE_SIZE - InPage);
            Line = (((ULONG64)Pages[Offset >> PAGE_SHIFT] << PAGE_SHIFT) + InPage) &
                   ~(ULONG64)(HalpRiscvCcacheLine - 1);
            End = ((ULONG64)Pages[Offset >> PAGE_SHIFT] << PAGE_SHIFT) + InPage + Chunk;
            for (; Line < End; Line += HalpRiscvCcacheLine)
                WRITE_REGISTER_ULONG64((volatile ULONG64 *)(HalpRiscvCcache + RISCV_CCACHE_FLUSH64), Line);
        }
    }
    __asm__ __volatile__("fence iorw, iorw" ::: "memory");
}

VOID
HalpRiscvInitializeExternalCache(
    _In_reads_bytes_(DeviceTreeSize) const VOID *DeviceTree,
    _In_ SIZE_T DeviceTreeSize)
{
    RISCV_FDT Fdt;
    ULONG Root, Soc, Parent, Node, Length, Line;
    ULONG64 Address, Size;
    const VOID *Property;

    if (!RiscvFdtOpen(DeviceTree, DeviceTreeSize, &Fdt))
        return;
    Root = RiscvFdtRootNode(&Fdt);
    if (!RiscvFdtFindNode(&Fdt, "/soc", &Soc, &Parent) || Parent != Root)
        return;

    for (Node = RiscvFdtFirstChild(&Fdt, Soc);
         Node != RISCV_FDT_NO_NODE;
         Node = RiscvFdtNextSibling(&Fdt, Node))
    {
        Property = RiscvFdtGetProperty(&Fdt, Node, "compatible", &Length);
        if (RiscvFdtStringListContains(Property, Length, "sifive,ccache0") &&
            RiscvFdtReadReg(&Fdt, Node, Soc, 0, &Address, &Size) &&
            Size >= RISCV_CCACHE_FLUSH64 + sizeof(ULONG64) && !(Address & (PAGE_SIZE - 1)) &&
            RiscvFdtReadU32(&Fdt, Node, "cache-block-size", &Line) &&
            Line >= 16 && Line <= PAGE_SIZE && !(Line & (Line - 1)))
        {
            HalpRiscvCcacheAddress = Address;
            HalpRiscvCcacheLine = Line;
            return;
        }
    }
}

BOOLEAN
HalpRiscvMapExternalCache(VOID)
{
    PHYSICAL_ADDRESS Address;

    if (!HalpRiscvCcacheAddress)
        return TRUE;
    Address.QuadPart = HalpRiscvCcacheAddress;
    HalpRiscvCcache = MmMapIoSpace(Address, PAGE_SIZE, MmNonCached);
    if (!HalpRiscvCcache)
        return FALSE;
    HALPRIVATEDISPATCH->HalFlushIoRectangleExternalCache = HalpRiscvFlushIoRectangleExternalCache;
    return TRUE;
}
