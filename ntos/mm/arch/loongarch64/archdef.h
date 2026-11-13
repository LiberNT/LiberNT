/*
 * PROJECT:     LiberNT NT Virtual Memory Subsystem
 * FILE:        ntoskrnl/nvs/arch/loongarch64/archdef.h
 * PURPOSE:     LoongArch64 memory manager architecture definitions
 *
 * SPDX-FileCopyrightText: 2026 Ahmed ARIF
 * SPDX-License-Identifier: GPL-3.0-only
 */

#pragma once

/*
 * Four-level walk, 4 KiB pages, 64-bit entries.
 *
 * PWCL = 0x13E4D52C encodes Table 42 exactly: PTbase 12 (bits 4:0),
 * PTwidth 9 (9:5), Dir1_base 21 (14:10), Dir1_width 9 (19:15), Dir2_base 30
 * (24:20), Dir2_width 9 (29:25), PTEWidth 0 (31:30, 64-bit entries).
 * PWCH = 0x267 encodes Table 43: Dir3_base 39 (5:0), Dir3_width 9 (11:6),
 * Dir4_base/Dir4_width 0 (17:12 / 23:18, "0 means there is no such level"),
 * and bits 31:24 read-only 0.  With Dir4_width 0 the root is Dir3, so the
 * top-level index is VA[47:39].
 * Source: LoongArch Vol1 sec. 7.5.8 (PWCL), sec. 7.5.9 (PWCH); the same values
 * are what the loader installs (LOONGARCH64_PWCL_4LEVEL_4K/PWCH_4LEVEL_4K) and
 * what MI_LA64_PWCL_4LEVEL_4K/MI_LA64_PWCH_4LEVEL_4K encode in
 * <ndk/loongarch64/mmtypes.h>.
 *
 * Level numbering here is the memory manager's: Level 0 is the PTE table
 * (VA 20:12), Level 3 is the root selected by PGDL/PGDH.
 */
#define MI_ARCH_ID_VALUE        0x00000006
#define MI_ARCH_NAME            "loongarch64"
#define MI_ARCH_PAGING_LEVELS   4
#define MI_ARCH_VA_BITS         48
#define MI_ARCH_PA_BITS         48
#define MI_ARCH_PTE_BYTES       8
#define MI_ARCH_PAGE_SHIFT      12
#define MI_ARCH_LARGE_LEVEL     1

#define MI_ARCH_L0_SHIFT       12
#define MI_ARCH_L0_BITS        9
#define MI_ARCH_L1_SHIFT       21
#define MI_ARCH_L1_BITS        9
#define MI_ARCH_L2_SHIFT       30
#define MI_ARCH_L2_BITS        9
#define MI_ARCH_L3_SHIFT       39
#define MI_ARCH_L3_BITS        9
#define MI_ARCH_L4_SHIFT       0
#define MI_ARCH_L4_BITS        0

/* VALEN = PALEN = 48: VA bit 47 selects the root, PGDL when it is 0 and PGDH
 * when it is 1 (Vol1 sec. 5.4.5, "determined by the (PALEN-1) bit of the
 * queried virtual address"; sec. 7.5.5/7.5.6 define the registers).  Because
 * the top-level index is the full 9 bits VA[47:39], that selector bit is also
 * index bit 8, so root entries 256-511 are the kernel half and 0-255 the user
 * half -- the same geometry Linux uses (PGDIR_SHIFT 39, VA_BITS 48).
 *
 * PALEN and VALEN are implementation properties reported by CPUCFG word 1
 * (Vol1 sec. 5.1: PALEN is "determined by the implementation", read through
 * the 0x1 configuration word).  The loader accepts only 48/48
 * (winldr.c La64InitializeHandoff) and this port hard-codes that width.
 * TODO: a CPU with VALEN or PALEN other than 48 would need the root selector
 * taken from the live PALEN instead of bit 47; the loader currently bug-checks
 * such a part, so the constant is exact for every machine that boots. */
#define MI_LA64_ROOT_KERNEL_INDEX   256
#define MI_LA64_VALEN               48
#define MI_LA64_PHYSICAL_MASK       0x0000FFFFFFFFFFFFULL
#define MI_LA64_PHYSICAL_LIMIT      (MI_LA64_PHYSICAL_MASK + 1)
#define MI_LA64_MAX_FRAME           (MI_LA64_PHYSICAL_LIMIT >> MI_ARCH_PAGE_SHIFT)
#define MI_LA64_ROOT_ENTRIES        512

/* One top-level (512 GiB) span is set aside for session space, as on the other
 * 64-bit ports. */
#define MI_LA64_SESSION_TOP_ENTRIES 1
