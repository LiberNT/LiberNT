/*
 * PROJECT:     LiberNT NT Virtual Memory Subsystem
 * FILE:        ntoskrnl/nvs/arch/arm/archdef.h
 * PURPOSE:     ARMv7 LPAE memory manager architecture definitions
 *
 * SPDX-FileCopyrightText: 2026 Ahmed ARIF
 * SPDX-License-Identifier: GPL-3.0-only
 */

#pragma once

#define MI_ARCH_ID_VALUE        0x00000007
#define MI_ARCH_NAME            "arm"
#define MI_ARCH_PAGING_LEVELS   3
#define MI_ARCH_VA_BITS         32
#define MI_ARCH_PA_BITS         40
#define MI_ARCH_PTE_BYTES       8
#define MI_ARCH_PAGE_SHIFT      12
#define MI_ARCH_LARGE_LEVEL     1

#define MI_ARCH_L0_SHIFT       12
#define MI_ARCH_L0_BITS        9
#define MI_ARCH_L1_SHIFT       21
#define MI_ARCH_L1_BITS        9
#define MI_ARCH_L2_SHIFT       30
#define MI_ARCH_L2_BITS        2
#define MI_ARCH_L3_SHIFT       0
#define MI_ARCH_L3_BITS        0
#define MI_ARCH_L4_SHIFT       0
#define MI_ARCH_L4_BITS        0

/* ARMv7 LPAE stage 1: bit 54 is XN at every level, PL0 is kept off kernel pages by AP[1]. */
#define MI_ARCH_LEAF_KERNEL_EXECUTE_NEVER     0ULL
#define MI_ARCH_LEAF_PRIVILEGED_EXECUTE_NEVER ((1ULL << 53) | (1ULL << 54))
