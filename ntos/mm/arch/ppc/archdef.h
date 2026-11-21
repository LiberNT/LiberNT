/*
 * PROJECT:     LiberNT NT Virtual Memory Subsystem
 * FILE:        ntoskrnl/nvs/arch/ppc/archdef.h
 * PURPOSE:     Windows NT PowerPC memory manager architecture definitions
 *
 * SPDX-FileCopyrightText: 2026 Ahmed ARIF
 * SPDX-License-Identifier: GPL-3.0-only
 */

#pragma once

/* Software page tables with 32-bit entries: a 1024-entry root (VA[31:22])
 * and 1024-entry leaves. The hashed page table caches them. */
#define MI_ARCH_ID_VALUE        0x00000006
#define MI_ARCH_NAME            "ppc"
#define MI_ARCH_PAGING_LEVELS   2
#define MI_ARCH_VA_BITS         32
#define MI_ARCH_PA_BITS         32
#define MI_ARCH_PTE_BYTES       4
#define MI_ARCH_PAGE_SHIFT      12
#define MI_ARCH_LARGE_LEVEL     1

#define MI_PPC_SESSION_TOP_ENTRIES 64

#define MI_ARCH_L0_SHIFT       12
#define MI_ARCH_L0_BITS        10
#define MI_ARCH_L1_SHIFT       22
#define MI_ARCH_L1_BITS        10
#define MI_ARCH_L2_SHIFT       0
#define MI_ARCH_L2_BITS        0
#define MI_ARCH_L3_SHIFT       0
#define MI_ARCH_L3_BITS        0
#define MI_ARCH_L4_SHIFT       0
#define MI_ARCH_L4_BITS        0
