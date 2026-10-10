/*
 * PROJECT:     LiberNT AMD64 emulation host
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Memory mapping calls of the felix86 sources over NT virtual memory
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

#include <stddef.h>
#include <stdint.h>

#define PROT_NONE  0
#define PROT_READ  1
#define PROT_WRITE 2
#define PROT_EXEC  4

#define MAP_SHARED          0x01
#define MAP_PRIVATE         0x02
#define MAP_FIXED           0x10
#define MAP_ANONYMOUS       0x20
#define MAP_NORESERVE       0x4000
#define MAP_FIXED_NOREPLACE 0x100000
#define MAP_FAILED          ((void *)-1)

#define MADV_HUGEPAGE 14

#ifdef __cplusplus
extern "C" {
#endif

void *mmap(void *Address, size_t Length, int Protection, int Flags, int File, int64_t Offset);
int munmap(void *Address, size_t Length);
int mprotect(void *Address, size_t Length, int Protection);
int madvise(void *Address, size_t Length, int Advice);

#ifdef __cplusplus
}
#endif
