#pragma once

#include "archdef.h"

#define MI_ARM_DIRECT_BASE        ARM_LOADER_KSEG0_BASE
#define MI_ARM_DIRECT_PAGES       (ARM_LOADER_KSEG0_SIZE >> PAGE_SHIFT)
#define MI_ARM_ROOT_KERNEL_INDEX  2
#define MI_ARM_ROOT_ENTRIES       4
#define MI_ARM_TTBR_FRAME_MASK    0x000000FFFFFFF000ULL

extern ULONG MiArmDirectFirstFrame;

VOID MiArmSetDirectFirstFrame(_In_ ULONG Frame);
