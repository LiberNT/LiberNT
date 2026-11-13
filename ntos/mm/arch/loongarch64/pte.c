/*
 * PROJECT:     LiberNT NT Virtual Memory Subsystem
 * FILE:        ntoskrnl/nvs/arch/loongarch64/pte.c
 * PURPOSE:     LoongArch64 page-table entry operations
 *
 * SPDX-FileCopyrightText: 2026 Ahmed ARIF
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include <mm/include/mienv.h>
#include "hardware.h"

/*
 * Protection to leaf bits.  The leaf format is Figure 8 (common pages) of
 * Vol1 sec. 5.4.5:
 *   V(0) D(1) PLV(3:2) MAT(5:4) G(6) P(7) W(8) PA(PALEN-1:12) NR(61) NX(62)
 *   RPLV(63)
 * The other positions are the TLBELO fields of Table 36; P and W exist only
 * in the page-table entry -- the manual says this information "is not filled
 * in the TLB table entry, but is used during the page walking".
 *
 *  - A readable page needs V (valid) and P (the physical page exists), which
 *    is what makes the walk accept the leaf.
 *  - Writable needs W (bit 8) and, for the store to proceed, D (bit 1):
 *    pgtable-bits.h defines __WRITEABLE as _PAGE_DIRTY | _PAGE_WRITE.  D is
 *    only set when the caller reports the page dirty, so the first store to a
 *    fresh writable page raises PME (Vol1 sec. 5.4.3.1: PME is raised when
 *    V=1, the privilege level is legal and D=0) and the trap layer completes
 *    it.
 *    TODO: the manual only says W "represents whether the page is writable"
 *    and never states how a walker combines W with D, so the W-and-D rule is
 *    the conservative reading that keeps PME reachable; keep it.
 *  - Write-copy leaves are read-only with software bit 11 set, the same
 *    division of labour amd64 and riscv64 use.
 *  - NT PAGE_EXECUTE stays readable, as the loader expects on the other NT
 *    ports; NX (bit 62) is what removes execute permission.
 */
static const MI_PTE MiLa64Access[8] =
{
    0,
    MI_LA64_PTE_VALID | MI_LA64_PTE_PRESENT | MI_LA64_PTE_NX,
    MI_LA64_PTE_VALID | MI_LA64_PTE_PRESENT,
    MI_LA64_PTE_VALID | MI_LA64_PTE_PRESENT,
    MI_LA64_PTE_VALID | MI_LA64_PTE_PRESENT | MI_LA64_PTE_WRITE | MI_LA64_PTE_NX,
    MI_LA64_PTE_VALID | MI_LA64_PTE_PRESENT | MI_LA64_PTE_COPYONWRITE | MI_LA64_PTE_NX,
    MI_LA64_PTE_VALID | MI_LA64_PTE_PRESENT | MI_LA64_PTE_WRITE,
    MI_LA64_PTE_VALID | MI_LA64_PTE_PRESENT | MI_LA64_PTE_COPYONWRITE
};

/*
 * MAT: 0 = SUC (strongly-ordered uncached), 1 = CC (coherent cached),
 * 2 = WUC (weakly-ordered uncached), 3 reserved (Vol1 sec. 2.1.7, Table 36);
 * the loader's DMW1 uses CC and its DMW0/DMW2 use SUC/WUC, matching the
 * handoff contract.
 *
 * Device and no-cache memory become SUC: strongly ordered is the only safe
 * mapping for a register window, and it is what Loongson's pgprot_noncached
 * selects.  The ISA has three MAT values, so a DEVICE page and a NOCACHE page
 * cannot be told apart once encoded.  Write-combine maps to WUC.
 */
static
MI_PTE
MiLa64CacheBits(_In_ ULONG Flags)
{
    if (Flags & (MI_LEAF_DEVICE | MI_LEAF_NOCACHE))
        return MI_LA64_PTE_MAT_SUC;

    if (Flags & MI_LEAF_WRITECOMBINE)
        return MI_LA64_PTE_MAT_WUC;

    return MI_LA64_PTE_MAT_CC;
}

MI_PTE
MiArchPteMakeLeaf(
    _In_ ULONG64 Frame,
    _In_ ULONG Protection,
    _In_ ULONG Flags)
{
    MI_PTE Pte;

    if (!MI_PROT_IS_ACCESSIBLE(Protection) || MI_PROT_IS_GUARD(Protection))
        return 0;
    Flags |= MI_PROT_LEAF_CACHE(Protection);

    MI_ASSERT(((Frame << MI_ARCH_PAGE_SHIFT) & ~MI_LA64_PTE_PFN_MASK) == 0);

    Pte = MiLa64Access[Protection & MI_PROT_ACCESS_MASK] | MiLa64CacheBits(Flags) |
          ((Frame << MI_ARCH_PAGE_SHIFT) & MI_LA64_PTE_PFN_MASK) | MI_LA64_PTE_ACCESSED;

    /* With RPLV=0 a page is accessible to any program whose privilege level
     * is not lower than PLV (Table 36; the check is spelled out in sec. 5.4.4),
     * so PLV 3 serves user and kernel code and PLV 0 only the kernel. */
    if (Flags & MI_LEAF_USER)
        Pte |= MI_LA64_PTE_PLV_USER;

    if (Flags & MI_LEAF_GLOBAL)
        Pte |= MI_LA64_PTE_GLOBAL;

    if ((Pte & MI_LA64_PTE_WRITE) && (Flags & (MI_LEAF_DIRTY | MI_LEAF_HARDWARE_DIRTY)))
        Pte |= MI_LA64_PTE_DIRTY;

    return Pte;
}

MI_PTE
MiArchPteMakeTable(
    _In_ ULONG64 Frame,
    _In_ ULONG Flags)
{
    /* A directory entry is the child frame in PA[PALEN-1:12] plus P.  Bit 6
     * must stay clear: in a directory entry it is the huge-page flag (H), not
     * G, and a normal child pointer is not huge (Figures 8/9 and the two
     * differences listed in Vol1 sec. 5.4.5).  PLV and MAT have no meaning in
     * a directory entry, so the caller's MI_LEAF_USER flag has no encoding. */
    UNREFERENCED_PARAMETER(Flags);

    MI_ASSERT(((Frame << MI_ARCH_PAGE_SHIFT) & ~MI_LA64_PTE_PFN_MASK) == 0);

    return ((Frame << MI_ARCH_PAGE_SHIFT) & MI_LA64_PTE_PFN_MASK) | MI_LA64_PTE_PRESENT;
}

BOOLEAN
MiArchPteIsBlock(_In_ MI_PTE Pte, _In_ ULONG Level)
{
    /* Bit 6 of a directory entry is the huge-page flag H, set when the entry
     * holds a huge-page leaf instead of a child pointer (Figure 9 and the
     * differences listed in Vol1 sec. 5.4.5).  Huge pages are not produced
     * while the descriptor reports SupportsLargePages = FALSE, so this only
     * has to recognise a foreign one. */
    return Level != 0 && (Pte & MI_LA64_PTE_PRESENT) && (Pte & MI_LA64_PTE_HUGE);
}

MI_PTE
MiArchPteMakeBlock(_In_ ULONG64 Frame, _In_ ULONG Protection, _In_ ULONG Flags)
{
    MI_PTE Leaf = MiArchPteMakeLeaf(Frame, Protection, Flags);
    BOOLEAN Global;
    ULONG64 Span = 1ULL << MI_ARCH_L1_SHIFT;

    if (Leaf == 0)
        return 0;

    /* Figure 9 (Vol1 sec. 5.4.5): in a huge-page entry bit 6 becomes the huge
     * flag H and the G bit moves to bit 12, while the PA field starts at the
     * huge page's own shift ("PA[PALEN-1:24]" for the manual's 16 MB example,
     * so PA[PALEN-1:21] for the 2 MiB level this descriptor uses).  Bit 12 lies
     * inside the PFN field, so G has to be moved, not just kept. */
    MI_ASSERT(((Frame << MI_ARCH_PAGE_SHIFT) & (Span - 1)) == 0);

    Global = (Leaf & MI_LA64_PTE_GLOBAL) != 0;
    Leaf &= ~MI_LA64_PTE_GLOBAL;

    if (Global)
        Leaf |= MI_LA64_PTE_HGLOBAL;

    /* TODO: G2 - verify on hardware that a directory entry with HUGE (bit 6)
     * and the aligned huge frame is split by LDPTE/the walker into the two
     * half-size TLB entries of Vol1 sec. 5.4.5 before SupportsLargePages is
     * turned on. */
    return Leaf | MI_LA64_PTE_HUGE;
}

BOOLEAN
MiArchPteIsValid(_In_ MI_PTE Pte)
{
    /* Leaves are V|P; directory slots are a frame plus P.  The loader's
     * shared empty-table pointer is neither and reads as invalid. */
    return MiLa64PteIsLeaf(Pte) || MiLa64PteIsTable(Pte);
}

BOOLEAN
MiArchPteIsLeafDescriptor(_In_ MI_PTE Pte)
{
    return MiLa64PteIsLeaf(Pte);
}

MI_PTE
MiArchPteBlockToPage(MI_PTE Pte, ULONG64 Frame)
{
    MI_PTE Converted = (Pte & ~(MI_LA64_PTE_PFN_MASK | MI_LA64_PTE_HUGE)) |
                       ((Frame << MI_ARCH_PAGE_SHIFT) & MI_LA64_PTE_PFN_MASK);

    /* The huge page's global bit lives at bit 12; the leaf's at bit 6. */
    if (Pte & MI_LA64_PTE_HGLOBAL)
        Converted |= MI_LA64_PTE_GLOBAL;

    return Converted;
}

MI_PTE
MiArchPteWithCache(MI_PTE Pte, ULONG Flags)
{
    return (Pte & ~MI_LA64_PTE_MAT_MASK) | MiLa64CacheBits(Flags);
}

ULONG64
MiArchPteFrame(_In_ MI_PTE Pte)
{
    return (Pte & MI_LA64_PTE_PFN_MASK) >> MI_ARCH_PAGE_SHIFT;
}

BOOLEAN
MiArchPteIsWritable(_In_ MI_PTE Pte)
{
    return MiLa64PteIsLeaf(Pte) && (Pte & MI_LA64_PTE_WRITE) != 0;
}

BOOLEAN
MiArchPteIsHardwareWritable(_In_ MI_PTE Pte)
{
    /* W alone leaves the store to fault with PME. */
    return MiLa64PteIsLeaf(Pte) &&
           (Pte & (MI_LA64_PTE_WRITE | MI_LA64_PTE_DIRTY)) ==
               (MI_LA64_PTE_WRITE | MI_LA64_PTE_DIRTY);
}

BOOLEAN
MiArchPteIsCopyOnWrite(_In_ MI_PTE Pte)
{
    return MiLa64PteIsLeaf(Pte) && (Pte & MI_LA64_PTE_COPYONWRITE) != 0;
}

ULONG
MiArchPteLeafFlags(_In_ MI_PTE Pte)
{
    switch (Pte & MI_LA64_PTE_MAT_MASK)
    {
        case MI_LA64_PTE_MAT_SUC:
            return MI_LEAF_NOCACHE;
        case MI_LA64_PTE_MAT_WUC:
            return MI_LEAF_WRITECOMBINE;
        default:
            return 0;
    }
}

BOOLEAN
MiArchPteIsDirty(_In_ MI_PTE Pte)
{
    return MiLa64PteIsLeaf(Pte) && (Pte & MI_LA64_PTE_DIRTY) != 0;
}

BOOLEAN
MiArchPteIsAccessed(_In_ MI_PTE Pte)
{
    /* TODO: there is no hardware accessed bit (descriptor
     * HasHardwareAccessedBit); this bit is set when the fault path installs
     * or touches a page and cleared by working-set trim, so a page that is
     * accessed after being trimmed does not fault and is not re-marked.
     * Aging is advisory until an access fault is manufactured. */
    return MiLa64PteIsLeaf(Pte) && (Pte & MI_LA64_PTE_ACCESSED) != 0;
}

BOOLEAN
MiArchIsSelfMapAddress(_In_ ULONG64 VirtualAddress)
{
    /* The walk starts at PGDL/PGDH; page tables are reached through the DMW
     * direct map, not a recursive slot. */
    UNREFERENCED_PARAMETER(VirtualAddress);
    return FALSE;
}

BOOLEAN
MiArchTranslateWindow(
    _In_ ULONG64 VirtualAddress,
    _Out_ PULONG64 PhysicalAddress,
    _Out_opt_ PMI_PTE LeafPte)
{
    ULONG Segment = (ULONG)(VirtualAddress >> MI_LA64_DMW_VSEG_SHIFT);
    ULONG Index;
    ULONG64 Crmd;

    /* A direct-mapping window hits when VA[63:60] equals its VSEG field and
     * the window is enabled for the current privilege level; the hit maps
     * VA[PALEN-1:0] with no TLB entry (Vol1 sec. 5.2.1, 7.5.18).  Windows 2
     * and 3 are load/store windows, so a hit here can only stand for a
     * load/store translation. */
    Crmd = MiLa64ReadCsrCrmd();
    if ((Crmd & MI_LA64_CRMD_PG) == 0 || (Crmd & MI_LA64_CRMD_DA) != 0)
        return FALSE;

    for (Index = 0; Index < 4; Index++)
    {
        ULONG64 Dmw = MiLa64ReadDmw(Index);

        if ((ULONG)((Dmw >> MI_LA64_DMW_VSEG_SHIFT) & 0xF) != Segment)
            continue;

        /* Table 55: PLV0-3 are bits 3:0 and CRMD.PLV selects one of them.
         * Another window may share this VSEG and be enabled here. */
        if ((Dmw & (1ULL << (Crmd & MI_LA64_CRMD_PLV_MASK))) == 0)
            continue;

        *PhysicalAddress = VirtualAddress & MI_LA64_PHYSICAL_MASK;
        if (LeafPte != NULL)
            *LeafPte = 0;
        return TRUE;
    }

    /* The loader leaves DMW3 = 0, whose VSEG field is 0 with no PLV bit set,
     * so it can never match. */
    return FALSE;
}

BOOLEAN
MiArchPteIsUser(_In_ MI_PTE Pte)
{
    return MiLa64PteIsLeaf(Pte) &&
           (Pte & MI_LA64_PTE_PLV_MASK) == MI_LA64_PTE_PLV_USER;
}

BOOLEAN
MiArchPteIsExecutable(_In_ MI_PTE Pte, _In_ BOOLEAN UserMode)
{
    /* NX removes execute permission (Figure 8, bit 62; Table 36 for TLBELO);
     * a supervisor leaf (PLV 0) is never executed as user code and vice
     * versa. */
    return MiLa64PteIsLeaf(Pte) && (Pte & MI_LA64_PTE_NX) == 0 &&
           (((Pte & MI_LA64_PTE_PLV_MASK) == MI_LA64_PTE_PLV_USER) == (UserMode != FALSE));
}

MI_PTE
MiArchPteGrantUserExecute(_In_ MI_PTE Pte)
{
    if (!MiLa64PteIsLeaf(Pte))
        return Pte;

    return Pte & ~MI_LA64_PTE_NX;
}

MI_PTE
MiArchPteRevokeUserExecute(_In_ MI_PTE Pte)
{
    if (!MiLa64PteIsLeaf(Pte))
        return Pte;

    return Pte | MI_LA64_PTE_NX;
}

MI_PTE
MiArchPteSetDirty(_In_ MI_PTE Pte, _In_ BOOLEAN Dirty)
{
    /* Only a writable leaf carries D.  Clearing it arms the page modification
     * exception, which is how write-watch and executable-write detection see
     * the next store. */
    if (!MiLa64PteIsLeaf(Pte) || (Pte & MI_LA64_PTE_WRITE) == 0)
        return Pte;

    return Dirty ? (Pte | MI_LA64_PTE_DIRTY) : (Pte & ~MI_LA64_PTE_DIRTY);
}

MI_PTE
MiArchPteSetAccessed(_In_ MI_PTE Pte, _In_ BOOLEAN Accessed)
{
    if (!MiLa64PteIsLeaf(Pte))
        return Pte;

    return Accessed ? (Pte | MI_LA64_PTE_ACCESSED) : (Pte & ~MI_LA64_PTE_ACCESSED);
}

BOOLEAN
MiArchPteNeedsBreak(_In_ MI_PTE Old, _In_ MI_PTE New)
{
    /* A leaf is replaced in place and the translation is dropped afterwards,
     * as on the other mapped ports. */
    UNREFERENCED_PARAMETER(Old);
    UNREFERENCED_PARAMETER(New);
    return FALSE;
}

VOID
MiArchInitializeProcessRoot(_In_ ULONG64 SystemRootFrame, _In_ ULONG64 ProcessRootFrame)
{
    PMI_PTE System = MiArchMapFrame(SystemRootFrame);
    PMI_PTE Process = MiArchMapFrame(ProcessRootFrame);
    ULONG Index;

    MI_ASSERT(SystemRootFrame != ProcessRootFrame);

    /* Both roots serve both halves, so copying the supervisor slots copies
     * every kernel mapping (handoff contract sec. 2.5).  The user half of a
     * fresh process root is already zeroed by the core, but write it
     * explicitly so the result does not depend on the caller. */
    for (Index = 0; Index < MI_LA64_ROOT_ENTRIES; Index++)
    {
        MI_PTE Value = (Index >= MI_LA64_ROOT_KERNEL_INDEX) ? MiArchPteRead(&System[Index]) : 0;

        MiArchPteWrite(&Process[Index], Value);
    }

    MiArchUnmapFrame(Process);
    MiArchUnmapFrame(System);
}
