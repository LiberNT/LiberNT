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
