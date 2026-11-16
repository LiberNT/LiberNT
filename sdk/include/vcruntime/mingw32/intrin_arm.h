/*
	Compatibility <intrin.h> header for GCC -- GCC equivalents of intrinsic
	Microsoft Visual C++ functions. Originally developed for the ReactOS
	(<https://reactos.org/>) and TinyKrnl (<http://www.tinykrnl.org/>)
	projects.

	Copyright (c) 2006 KJK::Hyperion <hackbunny@reactos.com>

	Permission is hereby granted, free of charge, to any person obtaining a
	copy of this software and associated documentation files (the "Software"),
	to deal in the Software without restriction, including without limitation
	the rights to use, copy, modify, merge, publish, distribute, sublicense,
	and/or sell copies of the Software, and to permit persons to whom the
	Software is furnished to do so, subject to the following conditions:

	The above copyright notice and this permission notice shall be included in
	all copies or substantial portions of the Software.

	THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
	IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
	FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
	AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
	LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
	FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
	DEALINGS IN THE SOFTWARE.
*/

#ifndef KJK_INTRIN_ARM_H_
#define KJK_INTRIN_ARM_H_

#ifndef __GNUC__
#error Unsupported compiler
#endif

#define _ReturnAddress() (__builtin_return_address(0))
#define _ReadWriteBarrier() __asm__ __volatile__("" : : : "memory")

#if !HAS_BUILTIN(__yield)
__INTRIN_INLINE void __yield(void) { __asm__ __volatile__("yield"); }
#endif

#if !HAS_BUILTIN(__break)
__INTRIN_INLINE void __break(unsigned int value) { __asm__ __volatile__("bkpt %0": : "M" (value)); }
#endif

#if !HAS_BUILTIN(_byteswap_ushort)
__INTRIN_INLINE unsigned short _byteswap_ushort(unsigned short value)
{
    return __builtin_bswap16(value);
}
#endif

#if !HAS_BUILTIN(_CountLeadingZeros)
__INTRIN_INLINE unsigned int _CountLeadingZeros(unsigned long Mask)
{
    return Mask ? __builtin_clz(Mask) : 32;
}
#endif

#if !HAS_BUILTIN(_CountTrailingZeros)
__INTRIN_INLINE unsigned int _CountTrailingZeros(unsigned long Mask)
{
    return Mask ? __builtin_ctz(Mask) : 32;
}
#endif

#if !HAS_BUILTIN(_BitScanForward)
__INTRIN_INLINE unsigned char _BitScanForward(unsigned long * const Index, const unsigned long Mask)
{
    if (!Mask)
        return 0;
    *Index = __builtin_ctz(Mask);
    return 1;
}
#endif

#if !HAS_BUILTIN(_BitScanReverse)
__INTRIN_INLINE unsigned char _BitScanReverse(unsigned long * const Index, const unsigned long Mask)
{
    if (!Mask)
        return 0;
    *Index = 31 - __builtin_clz(Mask);
    return 1;
}
#endif

#if !HAS_BUILTIN(_InterlockedCompareExchange8)
__INTRIN_INLINE char _InterlockedCompareExchange8(volatile char * const Destination, const char Exchange, const char Comperand)
{
    return __sync_val_compare_and_swap(Destination, Comperand, Exchange);
}
#endif

#if !HAS_BUILTIN(_InterlockedCompareExchange16)
__INTRIN_INLINE short _InterlockedCompareExchange16(volatile short * const Destination, const short Exchange, const short Comperand)
{
    return __sync_val_compare_and_swap(Destination, Comperand, Exchange);
}
#endif

#if !HAS_BUILTIN(_InterlockedCompareExchange)
__INTRIN_INLINE long _InterlockedCompareExchange(volatile long * const Destination, const long Exchange, const long Comperand)
{
    return __sync_val_compare_and_swap(Destination, Comperand, Exchange);
}
#endif

#if !HAS_BUILTIN(_InterlockedCompareExchange64)
__INTRIN_INLINE long long _InterlockedCompareExchange64(volatile long long * const Destination, const long long Exchange, const long long Comperand)
{
    return __sync_val_compare_and_swap(Destination, Comperand, Exchange);
}
#endif

#if !HAS_BUILTIN(_InterlockedCompareExchangePointer)
__INTRIN_INLINE void * _InterlockedCompareExchangePointer(void * volatile * const Destination, void * const Exchange, void * const Comperand)
{
    return __sync_val_compare_and_swap(Destination, Comperand, Exchange);
}
#endif

#if !HAS_BUILTIN(_InterlockedExchange)
__INTRIN_INLINE long _InterlockedExchange(volatile long * const Target, const long Value)
{
    return __atomic_exchange_n(Target, Value, __ATOMIC_SEQ_CST);
}
#endif

#if !HAS_BUILTIN(_InterlockedExchangePointer)
__INTRIN_INLINE void * _InterlockedExchangePointer(void * volatile * const Target, void * const Value)
{
    return __atomic_exchange_n(Target, Value, __ATOMIC_SEQ_CST);
}
#endif

#if !HAS_BUILTIN(_InterlockedExchangeAdd16)
__INTRIN_INLINE short _InterlockedExchangeAdd16(volatile short * const Addend, const short Value)
{
    return __sync_fetch_and_add(Addend, Value);
}
#endif

#if !HAS_BUILTIN(_InterlockedExchangeAdd)
__INTRIN_INLINE long _InterlockedExchangeAdd(volatile long * const Addend, const long Value)
{
    return __sync_fetch_and_add(Addend, Value);
}
#endif

#if !HAS_BUILTIN(_InterlockedAnd8)
__INTRIN_INLINE char _InterlockedAnd8(volatile char * const Value, const char Mask)
{
    return __sync_fetch_and_and(Value, Mask);
}
#endif

#if !HAS_BUILTIN(_InterlockedAnd16)
__INTRIN_INLINE short _InterlockedAnd16(volatile short * const Value, const short Mask)
{
    return __sync_fetch_and_and(Value, Mask);
}
#endif

#if !HAS_BUILTIN(_InterlockedAnd)
__INTRIN_INLINE long _InterlockedAnd(volatile long * const Value, const long Mask)
{
    return __sync_fetch_and_and(Value, Mask);
}
#endif

#if !HAS_BUILTIN(_InterlockedOr8)
__INTRIN_INLINE char _InterlockedOr8(volatile char * const Value, const char Mask)
{
    return __sync_fetch_and_or(Value, Mask);
}
#endif

#if !HAS_BUILTIN(_InterlockedOr16)
__INTRIN_INLINE short _InterlockedOr16(volatile short * const Value, const short Mask)
{
    return __sync_fetch_and_or(Value, Mask);
}
#endif

#if !HAS_BUILTIN(_InterlockedOr)
__INTRIN_INLINE long _InterlockedOr(volatile long * const Value, const long Mask)
{
    return __sync_fetch_and_or(Value, Mask);
}
#endif

#if !HAS_BUILTIN(_InterlockedXor8)
__INTRIN_INLINE char _InterlockedXor8(volatile char * const Value, const char Mask)
{
    return __sync_fetch_and_xor(Value, Mask);
}
#endif

#if !HAS_BUILTIN(_InterlockedXor16)
__INTRIN_INLINE short _InterlockedXor16(volatile short * const Value, const short Mask)
{
    return __sync_fetch_and_xor(Value, Mask);
}
#endif

#if !HAS_BUILTIN(_InterlockedXor)
__INTRIN_INLINE long _InterlockedXor(volatile long * const Value, const long Mask)
{
    return __sync_fetch_and_xor(Value, Mask);
}
#endif

#if !HAS_BUILTIN(_InterlockedDecrement)
__INTRIN_INLINE long _InterlockedDecrement(volatile long * const Addend)
{
    return __sync_sub_and_fetch(Addend, 1);
}
#endif

#if !HAS_BUILTIN(_InterlockedIncrement)
__INTRIN_INLINE long _InterlockedIncrement(volatile long * const Addend)
{
    return __sync_add_and_fetch(Addend, 1);
}
#endif

#if !HAS_BUILTIN(_InterlockedDecrement16)
__INTRIN_INLINE short _InterlockedDecrement16(volatile short * const Addend)
{
    return __sync_sub_and_fetch(Addend, 1);
}
#endif

#if !HAS_BUILTIN(_InterlockedIncrement16)
__INTRIN_INLINE short _InterlockedIncrement16(volatile short * const Addend)
{
    return __sync_add_and_fetch(Addend, 1);
}
#endif

#if !HAS_BUILTIN(_InterlockedAddLargeStatistic)
__INTRIN_INLINE long _InterlockedAddLargeStatistic(volatile long long * const Addend, const long Value)
{
    __sync_fetch_and_add(Addend, (long long)Value);
    return Value;
}
#endif

#if !HAS_BUILTIN(_disable)
__INTRIN_INLINE void _disable(void)
{
    __asm__ __volatile__("cpsid i" : : : "memory", "cc");
}
#endif

#if !HAS_BUILTIN(_enable)
__INTRIN_INLINE void _enable(void)
{
    __asm__ __volatile__("cpsie i" : : : "memory", "cc");
}
#endif

#if !HAS_BUILTIN(_interlockedbittestandset)
__INTRIN_INLINE unsigned char _interlockedbittestandset(volatile long * a, const long b)
{
    return (_InterlockedOr(a, 1L << b) >> b) & 1;
}
#endif

#if !HAS_BUILTIN(_interlockedbittestandreset)
__INTRIN_INLINE unsigned char _interlockedbittestandreset(volatile long * a, const long b)
{
    return (_InterlockedAnd(a, ~(1L << b)) >> b) & 1;
}
#endif

#if !defined(__MSVCRT__) && !HAS_BUILTIN(_rotl)
__INTRIN_INLINE unsigned int _rotl(const unsigned int value, int shift)
{
    shift &= 31;
    return shift ? ((value << shift) | (value >> (32 - shift))) : value;
}
#endif

#endif
/* EOF */
