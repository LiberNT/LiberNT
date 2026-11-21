/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     ARMv7 LPAE memory manager definitions
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#define _MI_PAGING_LEVELS 3
#define _MI_HAS_NO_EXECUTE 1

#define MI_USER_PROBE_ADDRESS           ((PVOID)0x7FFF0000)
#define MI_DEFAULT_SYSTEM_RANGE_START   ((PVOID)0x80000000)
#define MI_HIGHEST_SYSTEM_ADDRESS       ((PVOID)0xFFFFFFFF)
#ifndef MM_LOWEST_USER_ADDRESS
#define MM_LOWEST_USER_ADDRESS          ((PVOID)MM_ALLOCATION_GRANULARITY)
#endif
#define MM_HIGHEST_VAD_ADDRESS          ((PVOID)((ULONG_PTR)MM_HIGHEST_USER_ADDRESS - (16 * PAGE_SIZE)))
#define MI_MAX_ZERO_BITS                21

#define MM_HIGHEST_USER_ADDRESS_WOW64       0x7FFEFFFF
#define MM_HIGHEST_USER_ADDRESS_WOW64_LARGE 0xFFFEFFFF
#define MM_SYSTEM_RANGE_START_WOW64         0x80000000

/* Fault code the trap handler builds from DFSR/IFSR for MmAccessFault. */
#define MI_ARM_FAULT_PRESENT 0x01
#define MI_ARM_FAULT_WRITE   0x02
#define MI_ARM_FAULT_EXECUTE 0x10
#define MI_IS_NOT_PRESENT_FAULT(Code) (!BooleanFlagOn((Code), MI_ARM_FAULT_PRESENT))
#define MI_IS_WRITE_ACCESS(Code) BooleanFlagOn((Code), MI_ARM_FAULT_WRITE)
#define MI_IS_INSTRUCTION_FETCH(Code) BooleanFlagOn((Code), MI_ARM_FAULT_EXECUTE)

#ifdef __cplusplus
}
#endif
