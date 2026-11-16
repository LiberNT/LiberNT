/*
 * PROJECT:     LiberNT CRT library
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     ARM implementation of _set_statfp for libm
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <stdint.h>

void _set_statfp(uintptr_t mask)
{
    unsigned int fpscr;
    unsigned int flags = 0;

    if (mask & 0x0001) flags |= 0x01;
    if (mask & 0x0002) flags |= 0x80;
    if (mask & 0x0004) flags |= 0x02;
    if (mask & 0x0008) flags |= 0x04;
    if (mask & 0x0010) flags |= 0x08;
    if (mask & 0x0020) flags |= 0x10;

    __asm__ __volatile__("vmrs %0, fpscr" : "=r"(fpscr));
    fpscr |= flags;
    __asm__ __volatile__("vmsr fpscr, %0" : : "r"(fpscr));
}
