/*
 * PROJECT:     LiberNT NT Virtual Memory Subsystem
 * FILE:        ntoskrnl/nvs/arch/loongarch64/descriptor.c
 * PURPOSE:     LoongArch64 memory manager architecture descriptor
 *
 * SPDX-FileCopyrightText: 2026 Ahmed ARIF
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include <mm/include/nvsenv.h>
#include "archdef.h"
#include <mm/include/miarchcheck.h>

/* The loader hands over one 4 KiB root used as both PGDL and PGDH, four
 * 9-bit-index levels (VA 47:39 / 38:30 / 29:21 / 20:12) and a 48-bit PALEN;
 * see bootloader/arch/loongarch64/winldr.c and the handoff contract.
 *
 * Boolean fields, each an ISA fact rather than a policy choice:
 *  - HasRecursiveMap: FALSE.  Directory entries are physical page frames, the
 *    walk starts at PGDL/PGDH and a DMW covers the tables (Vol1 sec. 5.4.5,
 *    7.5.5, 7.5.6).
 *  - HasHardwareDirtyBit: FALSE.  TLBELO.D is a plain per-page field and the
 *    hardware never sets it: a store to a clean (D=0) page with V=1 and a
 *    legal privilege level raises the page modification exception (Ecode 0x4,
 *    Vol1 sec. 5.4.3.1 and Table 21) instead of writing D.  Loongson's kernel
 *    states the same: "We don't have hardware dirty/accessed bits".
 *  - HasHardwareAccessedBit: FALSE.  Neither the TLB entry of sec. 5.4.2 nor
 *    the TLBELO definition of Table 36 has an accessed bit; the backend keeps
 *    one in software bit 10 (see hardware.h).
 *  - RequiresBreakBeforeMake: FALSE.  A leaf is replaced in place and the
 *    translation is dropped afterwards, as on the other mapped ports.
 *  - RequiresExplicitIcacheSync: TRUE.  Vol1 sec. 2.1.7.1 leaves I/D coherence
 *    inside a core to the implementation and requires IBAR (plus CACOP when
 *    that coherence is not implemented) after stores to executable memory;
 *    this port takes the conservative case (reached through KeSweepICache).
 *  - HasCoherentDcache: FALSE.  Sec. 2.1.7.1 makes cross-core and
 *    coherent-I/O coherence a hardware job but leaves I-cache/D-cache
 *    coherence optional, so the port assumes the weaker case and performs
 *    explicit maintenance; the D-cache handling non-coherent DMA needs is
 *    platform-specific and outside Vol1.
 *  - HasAsidTagging: TRUE.  TLB entries carry a 10-bit ASID (Vol1 sec. 5.4.2,
 *    Table 38), and kernel mappings are requested with G so they survive a
 *    context switch.  The core does not switch ASIDs; the boot ASID stays in
 *    place.
 *  - HasExecuteNever: TRUE.  NX, bit 62, in the leaf and in TLBELO (Figure 8,
 *    Table 36).
 *  - HasGlobalPages: TRUE.  G, bit 6, in the leaf and in TLBELO (Figure 8,
 *    Table 36).
 *  - SupportsLargePages: FALSE.  The huge-page format IS specified (Figure 9,
 *    the two differences listed in sec. 5.4.5, and the automatic split into
 *    two half-size TLB entries), but this port has not verified it on
 *    hardware, so no kernel mapping may be built from it yet.
 *    MiArchPteMakeBlock implements the encoding (bit 6 = H, bit 12 = G) for
 *    the day that is verified.
 *
 * SystemReservedEnd is 0: the loader populates nothing in the PGDH half except
 * KI_USER_SHARED_DATA, whose own VAD is adopted, so no stem is reserved. */
static const MI_ARCH_DESCRIPTOR MiArchDescriptor =
{
    MI_ARCH_ID_VALUE,
    MI_ARCH_NAME,
    MI_ARCH_PAGING_LEVELS,
    MI_ARCH_VA_BITS,
    MI_ARCH_PA_BITS,
    MI_ARCH_PTE_BYTES,
    PAGE_SIZE,
    MI_ARCH_PAGE_SHIFT,
    (2ULL * 1024 * 1024),
    MI_ARCH_LARGE_LEVEL,
    {
        { MI_ARCH_L0_SHIFT, MI_ARCH_L0_BITS, (1ULL << MI_ARCH_L0_BITS) - 1, 1ULL << MI_ARCH_L0_BITS },
        { MI_ARCH_L1_SHIFT, MI_ARCH_L1_BITS, (1ULL << MI_ARCH_L1_BITS) - 1, 1ULL << MI_ARCH_L1_BITS },
        { MI_ARCH_L2_SHIFT, MI_ARCH_L2_BITS, (1ULL << MI_ARCH_L2_BITS) - 1, 1ULL << MI_ARCH_L2_BITS },
        { MI_ARCH_L3_SHIFT, MI_ARCH_L3_BITS, (1ULL << MI_ARCH_L3_BITS) - 1, 1ULL << MI_ARCH_L3_BITS },
        { MI_ARCH_L4_SHIFT, MI_ARCH_L4_BITS, (1ULL << MI_ARCH_L4_BITS) - 1, 1ULL << MI_ARCH_L4_BITS },
    },
    0x0000000000010000ULL,
    0x00007FFFFFFEFFFFULL,
    MI_LA64_KERNEL_BASE,
    0xFFFFFFFFFFFFFFFFULL,
    FALSE,
    FALSE,
    FALSE,
    FALSE,
    TRUE,
    FALSE,
    TRUE,
    TRUE,
    TRUE,
    FALSE,
    NULL,
    0,
    0,
    TRUE,
    FALSE,
    0,
    FALSE,
    MI_LA64_SESSION_TOP_ENTRIES
};

const MI_ARCH_DESCRIPTOR *
MiArchDescribe(VOID)
{
    return &MiArchDescriptor;
}
