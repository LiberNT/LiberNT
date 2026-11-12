/*
 * PROJECT:     LiberNT CRT
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     LoongArch64 compiler and instruction intrinsics
 */

#pragma once

#define _ReadBarrier() __asm__ __volatile__("" ::: "memory")
#define _WriteBarrier() __asm__ __volatile__("" ::: "memory")
#define _ReadWriteBarrier() __asm__ __volatile__("" ::: "memory")
#define _ReturnAddress() __builtin_return_address(0)
#define __nop() __asm__ __volatile__("nop")

#define __debugbreak() __builtin_trap()

static __inline__ __attribute__((__always_inline__, __noreturn__)) void __la64_fastfail(unsigned int Code)
{
    (void)Code;
    __builtin_trap();
    __builtin_unreachable();
}
#define __fastfail __la64_fastfail

#define _disable() __asm__ __volatile__("csrwr $zero, 0x0" ::: "memory")
