/*
 * PROJECT:     LiberNT SDK
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     ARM (Thumb-2) forms of the Visual C++ intrinsics clang does not provide
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

#define _ReturnAddress() (__builtin_return_address(0))
#define _ReadWriteBarrier() __asm__ __volatile__("" : : : "memory")
#define _ReadBarrier _ReadWriteBarrier
#define _WriteBarrier _ReadWriteBarrier

#if !HAS_BUILTIN(_CountLeadingZeros)
__INTRIN_INLINE unsigned int _CountLeadingZeros(unsigned long Mask)
{
    return Mask ? (unsigned int)__builtin_clz(Mask) : 32;
}
#endif

#if !HAS_BUILTIN(_CountTrailingZeros)
__INTRIN_INLINE unsigned int _CountTrailingZeros(unsigned long Mask)
{
    return Mask ? (unsigned int)__builtin_ctz(Mask) : 32;
}
#endif

#if !HAS_BUILTIN(_InterlockedAdd)
__INTRIN_INLINE long _InterlockedAdd(long volatile *Addend, long Value)
{
    return __atomic_add_fetch(Addend, Value, __ATOMIC_SEQ_CST);
}
#endif

#if !HAS_BUILTIN(_InterlockedAdd64)
__INTRIN_INLINE long long _InterlockedAdd64(long long volatile *Addend, long long Value)
{
    return __atomic_add_fetch(Addend, Value, __ATOMIC_SEQ_CST);
}
#endif

#if !HAS_BUILTIN(_InterlockedAdd_nf)
__INTRIN_INLINE long _InterlockedAdd_nf(long volatile *Addend, long Value)
{
    return __atomic_add_fetch(Addend, Value, __ATOMIC_RELAXED);
}
#endif

#if !HAS_BUILTIN(_InterlockedAdd64_nf)
__INTRIN_INLINE long long _InterlockedAdd64_nf(long long volatile *Addend, long long Value)
{
    return __atomic_add_fetch(Addend, Value, __ATOMIC_RELAXED);
}
#endif

#if !HAS_BUILTIN(__prefetch)
__INTRIN_INLINE void __prefetch(const void *Address)
{
    __builtin_prefetch(Address);
}
#endif

#if !HAS_BUILTIN(_InterlockedAddLargeStatistic)
__INTRIN_INLINE long _InterlockedAddLargeStatistic(long long volatile *Addend, long Value)
{
    __atomic_fetch_add(Addend, (long long)Value, __ATOMIC_SEQ_CST);
    return Value;
}
#endif

#if !HAS_BUILTIN(_disable)
__INTRIN_INLINE void _disable(void)
{
    __asm__ __volatile__("cpsid i" : : : "memory");
}
#endif

#if !HAS_BUILTIN(_enable)
__INTRIN_INLINE void _enable(void)
{
    __asm__ __volatile__("cpsie i" : : : "memory");
}
#endif
