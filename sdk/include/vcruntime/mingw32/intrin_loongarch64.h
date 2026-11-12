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

static __inline__ __attribute__((__always_inline__)) void __la64_set_ie(int Enable)
{
    register unsigned long Mask __asm__("$t0") = 4;
    register unsigned long Value __asm__("$t1") = Enable ? 4 : 0;
    __asm__ __volatile__("csrxchg %0, %1, 0x0" :: "r"(Value), "r"(Mask) : "memory");
}
#define _disable() __la64_set_ie(0)
#define _enable()  __la64_set_ie(1)
