/*
 * PROJECT:     LiberNT zstd decompression library
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Kernel-mode build of the zstd decompressor: memory only comes from the caller's static workspace
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

#define ZSTD_DEPS_MALLOC
#define ZSTD_malloc(s) ((void)(s), (void *)0)
#define ZSTD_calloc(n, s) ((void)(n), (void)(s), (void *)0)
#define ZSTD_free(p) ((void)(p))
#define XXH_NO_STDLIB

#if defined(_MSC_VER) && !defined(__clang__)
/* XXH_NO_STDLIB keeps out <stdlib.h>, where MSVC declares the rotates and byte swaps */
#include <intrin.h>
#endif

#if defined(_MSC_VER) && !defined(__clang__) && defined(__aarch64__)
/* The build defines __aarch64__ for MSVC too, which would select a GNU asm prefetch */
#define NO_PREFETCH
#endif
