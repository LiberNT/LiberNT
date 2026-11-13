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

static __inline__ long long __la64_interlocked_add64(long long volatile *Value, long long Addend)
{
    return __atomic_fetch_add((unsigned long long *)Value, (unsigned long long)Addend, __ATOMIC_SEQ_CST);
}

static __inline__ long long __la64_interlocked_and64(long long volatile *Value, long long Mask)
{
    return __atomic_fetch_and((unsigned long long *)Value, (unsigned long long)Mask, __ATOMIC_SEQ_CST);
}

static __inline__ long long __la64_interlocked_or64(long long volatile *Value, long long Mask)
{
    return __atomic_fetch_or((unsigned long long *)Value, (unsigned long long)Mask, __ATOMIC_SEQ_CST);
}

static __inline__ long long __la64_interlocked_xor64(long long volatile *Value, long long Mask)
{
    return __atomic_fetch_xor((unsigned long long *)Value, (unsigned long long)Mask, __ATOMIC_SEQ_CST);
}

static __inline__ unsigned char __la64_interlocked_compare_exchange64(long long volatile *Destination,
                                                                     long long Exchange,
                                                                     long long Comparand)
{
    long long Expected = Comparand;
    return __atomic_compare_exchange_n((unsigned long long *)Destination,
                                       (unsigned long long *)&Expected,
                                       (unsigned long long)Exchange, 0,
                                       __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
}

#define _InterlockedAdd64(v, a) __la64_interlocked_add64((long long volatile *)(v), (a))
#define _InterlockedAnd64(v, m) __la64_interlocked_and64((long long volatile *)(v), (m))
#define _InterlockedOr64(v, m) __la64_interlocked_or64((long long volatile *)(v), (m))
#define _InterlockedXor64(v, m) __la64_interlocked_xor64((long long volatile *)(v), (m))
#define _InterlockedCompareExchange64(d, e, c) __la64_interlocked_compare_exchange64((long long volatile *)(d), (e), (c))

static __inline__ long long __la64_interlocked_exchange64(long long volatile *Target, long long Value)
{
    return __atomic_exchange_n((unsigned long long *)Target, (unsigned long long)Value, __ATOMIC_SEQ_CST);
}

static __inline__ long long __la64_interlocked_exchange_add64(long long volatile *Addend, long long Value)
{
    return __atomic_fetch_add((unsigned long long *)Addend, (unsigned long long)Value, __ATOMIC_SEQ_CST);
}

static __inline__ long long __la64_interlocked_increment64(long long volatile *Addend)
{
    return __atomic_add_fetch((unsigned long long *)Addend, 1ULL, __ATOMIC_SEQ_CST);
}

static __inline__ long long __la64_interlocked_decrement64(long long volatile *Addend)
{
    return __atomic_sub_fetch((unsigned long long *)Addend, 1ULL, __ATOMIC_SEQ_CST);
}

#define _InterlockedExchange64(t, v) __la64_interlocked_exchange64((long long volatile *)(t), (v))
#define _InterlockedExchangeAdd64(a, v) __la64_interlocked_exchange_add64((long long volatile *)(a), (v))
#define _InterlockedIncrement64(a) __la64_interlocked_increment64((long long volatile *)(a))
#define _InterlockedDecrement64(a) __la64_interlocked_decrement64((long long volatile *)(a))
