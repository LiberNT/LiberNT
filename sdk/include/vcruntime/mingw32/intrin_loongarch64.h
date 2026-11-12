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

static __inline__ unsigned char __la64_bit_scan_forward(unsigned int *Index, unsigned int Mask)
{
    if (Mask == 0) return 0;
    *Index = (unsigned int)__builtin_ctz(Mask);
    return 1;
}

static __inline__ unsigned char __la64_bit_scan_reverse(unsigned int *Index, unsigned int Mask)
{
    if (Mask == 0) return 0;
    *Index = 31u - (unsigned int)__builtin_clz(Mask);
    return 1;
}

static __inline__ unsigned char __la64_bit_scan_forward64(unsigned int *Index, unsigned __int64 Mask)
{
    if (Mask == 0) return 0;
    *Index = (unsigned int)__builtin_ctzll(Mask);
    return 1;
}

static __inline__ unsigned char __la64_bit_scan_reverse64(unsigned int *Index, unsigned __int64 Mask)
{
    if (Mask == 0) return 0;
    *Index = 63u - (unsigned int)__builtin_clzll(Mask);
    return 1;
}

#define _BitScanForward(i, m) __la64_bit_scan_forward((unsigned int *)(i), (unsigned int)(m))
#define _BitScanReverse(i, m) __la64_bit_scan_reverse((unsigned int *)(i), (unsigned int)(m))
#define _BitScanForward64(i, m) __la64_bit_scan_forward64((unsigned int *)(i), (unsigned __int64)(m))
#define _BitScanReverse64(i, m) __la64_bit_scan_reverse64((unsigned int *)(i), (unsigned __int64)(m))

static __inline__ unsigned char __la64_bit_test_and_set64(long long volatile *Value, long long Bit)
{
    unsigned long long Old = __atomic_fetch_or((unsigned long long *)Value, 1ULL << Bit, __ATOMIC_SEQ_CST);
    return (unsigned char)((Old >> Bit) & 1);
}

static __inline__ unsigned char __la64_bit_test_and_reset64(long long volatile *Value, long long Bit)
{
    unsigned long long Old = __atomic_fetch_and((unsigned long long *)Value, ~(1ULL << Bit), __ATOMIC_SEQ_CST);
    return (unsigned char)((Old >> Bit) & 1);
}

#define _interlockedbittestandset64(v, b) __la64_bit_test_and_set64((long long volatile *)(v), (b))
#define _interlockedbittestandreset64(v, b) __la64_bit_test_and_reset64((long long volatile *)(v), (b))
