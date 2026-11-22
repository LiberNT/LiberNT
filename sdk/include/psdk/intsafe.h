/*!
 * \file intsafe.h
 *
 * \brief Windows helper functions for integer overflow prevention
 *
 * \package This file is part of the ReactOS PSDK package.
 *
 * \author
 *   Timo Kreuzer (timo.kreuzer@reactos.org)
 *
 * \copyright THIS SOFTWARE IS NOT COPYRIGHTED
 *
 * This source code is offered for use in the public domain. You may
 * use, modify or distribute it freely.
 *
 * This code is distributed in the hope that it will be useful but
 * WITHOUT ANY WARRANTY. ALL WARRANTIES, EXPRESS OR IMPLIED ARE HEREBY
 * DISCLAIMED. This includes but is not limited to warranties of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * \todo
 * - missing conversion functions
 * - multiplication functions
 * - signed add, sub and multiply functions
 */
#pragma once

#ifndef _INTSAFE_H_INCLUDED_
#define _INTSAFE_H_INCLUDED_

#include <specstrings.h>

/* Handle ntintsafe here too */
#ifdef _NTINTSAFE_H_INCLUDED_
#ifndef _NTDEF_ /* Guard agains redefinition from ntstatus.h */
typedef _Return_type_success_(return >= 0) long NTSTATUS;
#endif
#define NT_SUCCESS(Status) (((NTSTATUS)(Status)) >= 0)
#define STATUS_SUCCESS ((NTSTATUS)0x00000000)
#define STATUS_INTEGER_OVERFLOW ((NTSTATUS)0xC0000095)
#define INTSAFE_RESULT NTSTATUS
#define INTSAFE_SUCCESS STATUS_SUCCESS
#define INTSAFE_E_ARITHMETIC_OVERFLOW STATUS_INTEGER_OVERFLOW
#define INTSAFE_NAME(name) Rtl##name
#else // _NTINTSAFE_H_INCLUDED_
#ifndef _HRESULT_DEFINED
typedef _Return_type_success_(return >= 0) long HRESULT;
#endif
#ifndef SUCCEEDED
#define SUCCEEDED(hr) (((HRESULT)(hr)) >= 0)
#define FAILED(hr) (((HRESULT)(hr)) < 0)
#define S_OK    ((HRESULT)0L)
#endif
#define INTSAFE_RESULT HRESULT
#define INTSAFE_SUCCESS S_OK
#define INTSAFE_E_ARITHMETIC_OVERFLOW ((HRESULT)0x80070216L)
#define INTSAFE_NAME(name) name
#endif // _NTINTSAFE_H_INCLUDED_

#if !defined(_W64)
#if defined(_MSC_VER) && !defined(__midl) && (defined(_M_IX86) || defined(_M_ARM))
#define _W64 __w64
#else
#define _W64
#endif
#endif

/* Static assert */
#ifndef C_ASSERT
#ifdef _MSC_VER
# define C_ASSERT(e) typedef char __C_ASSERT__[(e)?1:-1]
#else
# define C_ASSERT(e) extern void __C_ASSERT__(int [(e)?1:-1])
#endif
#endif /* C_ASSERT */

/* Typedefs */
#ifndef _WINNT_
#ifndef _NTDEF_
typedef char CHAR;
typedef unsigned char UCHAR, UINT8;
typedef signed char INT8;
typedef short SHORT;
typedef signed short INT16;
typedef unsigned short USHORT, UINT16;
typedef int INT;
typedef unsigned int UINT32;
typedef signed int INT32;
typedef long LONG;
typedef unsigned long ULONG;
typedef long long LONGLONG, LONG64;
typedef signed long long INT64;
typedef unsigned long long ULONGLONG, DWORDLONG, ULONG64, DWORD64, UINT64;
#ifdef _WIN64
typedef long long INT_PTR, LONG_PTR, SSIZE_T, ptrdiff_t;
typedef unsigned long long UINT_PTR, ULONG_PTR, DWORD_PTR, SIZE_T, size_t;
#else // _WIN64
typedef _W64 int INT_PTR, ptrdiff_t;
typedef _W64 unsigned int UINT_PTR, size_t;
typedef _W64 long LONG_PTR, SSIZE_T;
typedef _W64 unsigned long ULONG_PTR, DWORD_PTR, SIZE_T;
#endif // _WIN64
#endif
typedef unsigned char BYTE;
typedef unsigned short WORD;
typedef unsigned int UINT;
typedef unsigned long DWORD;
#endif // _WINNT_

/* Just to be sure! */
C_ASSERT(sizeof(USHORT) == 2);
C_ASSERT(sizeof(INT) == 4);
C_ASSERT(sizeof(UINT) == 4);
C_ASSERT(sizeof(LONG) == 4);
C_ASSERT(sizeof(ULONG) == 4);
C_ASSERT(sizeof(DWORD) == 4);
C_ASSERT(sizeof(UINT_PTR) == sizeof(ULONG_PTR));

/* Integer range margins (use (x-1) to prevent warnings) */
#define INT8_MIN (-128)
#define SHORT_MIN (-32768)
#define INT16_MIN (-32768)
#define INT_MIN (-2147483647 - 1)
#define INT32_MIN (-2147483647 - 1)
#define LONG_MIN (-2147483647L - 1)
#define LONGLONG_MIN (-9223372036854775807LL - 1)
#define LONG64_MIN (-9223372036854775807LL - 1)
#define INT64_MIN (-9223372036854775807LL - 1)
#define INT128_MIN (-170141183460469231731687303715884105727i128 - 1)
#ifdef _WIN64
#define INT_PTR_MIN (-9223372036854775807LL - 1)
#define LONG_PTR_MIN (-9223372036854775807LL - 1)
#define PTRDIFF_T_MIN (-9223372036854775807LL - 1)
#define SSIZE_T_MIN (-9223372036854775807LL - 1)
#else /* _WIN64 */
#define INT_PTR_MIN (-2147483647 - 1)
#define LONG_PTR_MIN (-2147483647L - 1)
#define PTRDIFF_T_MIN (-2147483647 - 1)
#define SSIZE_T_MIN (-2147483647L - 1)
#endif /* _WIN64 */

#define INT8_MAX 127
#define UINT8_MAX 0xff /* 255U */
#define BYTE_MAX 0xff /* 255U */
#define SHORT_MAX 32767
#define INT16_MAX 32767
#define USHORT_MAX 0xffff /* 65535U */
#define UINT16_MAX 0xffff /* 65535U */
#define WORD_MAX 0xffff /* 65535U */
#define INT_MAX 2147483647
#define INT32_MAX 2147483647
#define UINT_MAX 0xffffffff
#define UINT32_MAX 0xffffffff  /* 4294967295U */
#define LONG_MAX 2147483647L
#define ULONG_MAX 0xffffffffUL
#define DWORD_MAX 0xffffffffUL
#define LONGLONG_MAX 9223372036854775807LL
#define LONG64_MAX 9223372036854775807LL
#define INT64_MAX 9223372036854775807LL
#define ULONGLONG_MAX 0xffffffffffffffffULL
#define DWORDLONG_MAX 0xffffffffffffffffULL
#define ULONG64_MAX 0xffffffffffffffffULL
#define DWORD64_MAX 0xffffffffffffffffULL
#define UINT64_MAX 0xffffffffffffffffULL /* 18446744073709551615ULL */
#define INT128_MAX 170141183460469231731687303715884105727i128
#define UINT128_MAX 0xffffffffffffffffffffffffffffffffui128
#undef SIZE_T_MAX
#ifdef _WIN64
#define INT_PTR_MAX 9223372036854775807LL
#define UINT_PTR_MAX 0xffffffffffffffffULL
#define LONG_PTR_MAX 9223372036854775807LL
#define ULONG_PTR_MAX 0xffffffffffffffffULL
#define DWORD_PTR_MAX 0xffffffffffffffffULL
#define PTRDIFF_T_MAX 9223372036854775807LL
#define SIZE_T_MAX 0xffffffffffffffffULL
#define SSIZE_T_MAX 9223372036854775807LL
#define _SIZE_T_MAX 0xffffffffffffffffULL
#else /* _WIN64 */
#define INT_PTR_MAX 2147483647
#define UINT_PTR_MAX 0xffffffff
#define LONG_PTR_MAX 2147483647L
#define ULONG_PTR_MAX 0xffffffffUL
#define DWORD_PTR_MAX 0xffffffffUL
#define PTRDIFF_T_MAX 2147483647
#define SIZE_T_MAX 0xffffffff
#define SSIZE_T_MAX 2147483647L
#define _SIZE_T_MAX 0xffffffffUL
#endif /* _WIN64 */

/* Error values */
#define INT8_ERROR ((signed char)(-1))
#define UINT8_ERROR ((unsigned char)0xff)
#define BYTE_ERROR ((unsigned char)0xff)
#define SHORT_ERROR ((short)(-1))
#define INT16_ERROR ((short)(-1))
#define USHORT_ERROR ((unsigned short)0xffff)
#define UINT16_ERROR ((unsigned short)0xffff)
#define WORD_ERROR ((unsigned short)0xffff)
#define INT_ERROR (-1)
#define INT32_ERROR (-1)
#define UINT_ERROR 0xffffffffU
#define UINT32_ERROR 0xffffffffU
#define LONG_ERROR (-1L)
#define ULONG_ERROR 0xffffffffUL
#define DWORD_ERROR 0xffffffffUL
#define LONGLONG_ERROR (-1LL)
#define LONG64_ERROR (-1LL)
#define INT64_ERROR (-1LL)
#define ULONGLONG_ERROR 0xffffffffffffffffULL
#define DWORDLONG_ERROR 0xffffffffffffffffULL
#define ULONG64_ERROR 0xffffffffffffffffULL
#define UINT64_ERROR 0xffffffffffffffffULL
#ifdef _WIN64
#define INT_PTR_ERROR (-1LL)
#define UINT_PTR_ERROR 0xffffffffffffffffULL
#define LONG_PTR_ERROR (-1LL)
#define ULONG_PTR_ERROR 0xffffffffffffffffULL
#define DWORD_PTR_ERROR 0xffffffffffffffffULL
#define PTRDIFF_T_ERROR (-1LL)
#define SIZE_T_ERROR 0xffffffffffffffffULL
#define SSIZE_T_ERROR (-1LL)
#define _SIZE_T_ERROR 0xffffffffffffffffULL
#else /* _WIN64 */
#define INT_PTR_ERROR (-1)
#define UINT_PTR_ERROR 0xffffffffU
#define LONG_PTR_ERROR (-1L)
#define ULONG_PTR_ERROR 0xffffffffUL
#define DWORD_PTR_ERROR 0xffffffffUL
#define PTRDIFF_T_ERROR (-1)
#define SIZE_T_ERROR 0xffffffffU
#define SSIZE_T_ERROR (-1L)
#define _SIZE_T_ERROR 0xffffffffUL
#endif /* _WIN64 */

/* special definitons (the CHAR ones should not be defined here!) */
#define _INTSAFE_CHAR CHAR
#define _INTSAFE_CHAR_ERROR ((signed char)(-1))
#ifdef _CHAR_UNSIGNED
 #define _INTSAFE_CHAR_MIN ((unsigned char)0)
 #define _INTSAFE_CHAR_MAX ((unsigned char)0xff)
#else
 #define _INTSAFE_CHAR_MIN ((signed char)(-128))
 #define _INTSAFE_CHAR_MAX ((signed char)127)
#endif /* _CHAR_UNSIGNED */

#define size_t_ERROR SIZE_T_ERROR
#define UCHAR_ERROR '\0'
#define CHAR_ERROR '\0'

/* 32 bit x 32 bit to 64 bit unsigned multiplication */
#ifndef UInt32x32To64
#define UInt32x32To64(a,b) ((unsigned __int64)(unsigned int)(a)*(unsigned __int64)(unsigned int)(b))
#endif

/* Convert unsigned to signed or unsigned */
#define DEFINE_SAFE_CONVERT_UTOX(_Name, _TypeFrom, _TypeTo) \
_Must_inspect_result_ \
__forceinline \
INTSAFE_RESULT \
INTSAFE_NAME(_Name)( \
    _In_ _TypeFrom Input, \
    _Out_ _Deref_out_range_(==, Input) _TypeTo *pOutput) \
{ \
    if ((sizeof(_TypeFrom) < sizeof(_TypeTo)) || (Input <= _TypeTo ## _MAX)) \
    { \
        *pOutput = (_TypeTo)Input; \
        return INTSAFE_SUCCESS; \
    } \
    else \
    { \
        *pOutput = _TypeTo ## _ERROR; \
        return INTSAFE_E_ARITHMETIC_OVERFLOW; \
    } \
}

DEFINE_SAFE_CONVERT_UTOX(ByteToChar, BYTE, _INTSAFE_CHAR)
DEFINE_SAFE_CONVERT_UTOX(ByteToInt8, BYTE, INT8)
DEFINE_SAFE_CONVERT_UTOX(UInt8ToChar, UINT8, _INTSAFE_CHAR)
DEFINE_SAFE_CONVERT_UTOX(UInt8ToInt8, UINT8, INT8)
DEFINE_SAFE_CONVERT_UTOX(UShortToChar, USHORT, _INTSAFE_CHAR)
DEFINE_SAFE_CONVERT_UTOX(UShortToUChar, USHORT, UINT8)
DEFINE_SAFE_CONVERT_UTOX(UShortToInt8, USHORT, INT8)
DEFINE_SAFE_CONVERT_UTOX(UShortToUInt8, USHORT, UINT8)
DEFINE_SAFE_CONVERT_UTOX(UShortToShort, USHORT, SHORT)
DEFINE_SAFE_CONVERT_UTOX(UIntToUChar, UINT, UINT8)
DEFINE_SAFE_CONVERT_UTOX(UIntToInt8, UINT, INT8)
DEFINE_SAFE_CONVERT_UTOX(UIntToUInt8, UINT, UINT8)
DEFINE_SAFE_CONVERT_UTOX(UIntToShort, UINT, SHORT)
DEFINE_SAFE_CONVERT_UTOX(UIntToUShort, UINT, USHORT)
DEFINE_SAFE_CONVERT_UTOX(UIntToInt, UINT, INT)
DEFINE_SAFE_CONVERT_UTOX(UIntToLong, UINT, LONG)
DEFINE_SAFE_CONVERT_UTOX(UIntPtrToUChar, UINT_PTR, UINT8)
DEFINE_SAFE_CONVERT_UTOX(UIntPtrToInt8, UINT_PTR, INT8)
DEFINE_SAFE_CONVERT_UTOX(UIntPtrToUInt8, UINT_PTR, UINT8)
DEFINE_SAFE_CONVERT_UTOX(UIntPtrToShort, UINT_PTR, SHORT)
DEFINE_SAFE_CONVERT_UTOX(UIntPtrToUShort, UINT_PTR, USHORT)
DEFINE_SAFE_CONVERT_UTOX(UIntPtrToInt16, UINT_PTR, INT16)
DEFINE_SAFE_CONVERT_UTOX(UIntPtrToUInt16, UINT_PTR, UINT16)
DEFINE_SAFE_CONVERT_UTOX(UIntPtrToInt, UINT_PTR, INT)
DEFINE_SAFE_CONVERT_UTOX(UIntPtrToLong, UINT_PTR, LONG)
DEFINE_SAFE_CONVERT_UTOX(UIntPtrToIntPtr, UINT_PTR, INT_PTR)
DEFINE_SAFE_CONVERT_UTOX(UIntPtrToLongPtr, UINT_PTR, LONG_PTR)
DEFINE_SAFE_CONVERT_UTOX(ULongToUChar, ULONG, UINT8)
DEFINE_SAFE_CONVERT_UTOX(ULongToUInt8, ULONG, UINT8)
DEFINE_SAFE_CONVERT_UTOX(ULongToShort, ULONG, SHORT)
DEFINE_SAFE_CONVERT_UTOX(ULongToUShort, ULONG, USHORT)
DEFINE_SAFE_CONVERT_UTOX(ULongToInt, ULONG, INT)
DEFINE_SAFE_CONVERT_UTOX(ULongToUInt, ULONG, UINT)
DEFINE_SAFE_CONVERT_UTOX(ULongToIntPtr, ULONG, INT_PTR)
DEFINE_SAFE_CONVERT_UTOX(ULongToUIntPtr, ULONG, UINT_PTR)
DEFINE_SAFE_CONVERT_UTOX(ULongToLongPtr, ULONG, LONG_PTR)
DEFINE_SAFE_CONVERT_UTOX(ULongPtrToULong, ULONG_PTR, ULONG)
DEFINE_SAFE_CONVERT_UTOX(ULongLongToUInt, ULONGLONG, UINT)
DEFINE_SAFE_CONVERT_UTOX(ULongLongToULong, ULONGLONG, ULONG)
DEFINE_SAFE_CONVERT_UTOX(ULongLongToULongPtr, ULONGLONG, ULONG_PTR)


/* Convert signed to unsigned */
#define DEFINE_SAFE_CONVERT_STOU(_Name, _TypeFrom, _TypeTo) \
_Must_inspect_result_ \
__forceinline \
INTSAFE_RESULT \
INTSAFE_NAME(_Name)( \
    _In_ _TypeFrom Input, \
    _Out_ _Deref_out_range_(==, Input) _TypeTo *pOutput) \
{ \
    if ((Input >= 0) && \
        ((sizeof(_TypeFrom) <= sizeof(_TypeTo)) || (Input <= (_TypeFrom)_TypeTo ## _MAX))) \
    { \
        *pOutput = (_TypeTo)Input; \
        return INTSAFE_SUCCESS; \
    } \
    else \
    { \
        *pOutput = _TypeTo ## _ERROR; \
        return INTSAFE_E_ARITHMETIC_OVERFLOW; \
    } \
}

DEFINE_SAFE_CONVERT_STOU(Int8ToUChar, INT8, UINT8)
DEFINE_SAFE_CONVERT_STOU(Int8ToUInt8, INT8, UINT8)
DEFINE_SAFE_CONVERT_STOU(Int8ToUShort, INT8, USHORT)
DEFINE_SAFE_CONVERT_STOU(Int8ToUInt, INT8, UINT)
DEFINE_SAFE_CONVERT_STOU(Int8ToULong, INT8, ULONG)
DEFINE_SAFE_CONVERT_STOU(Int8ToUIntPtr, INT8, UINT_PTR)
DEFINE_SAFE_CONVERT_STOU(Int8ToULongPtr, INT8, ULONG_PTR)
DEFINE_SAFE_CONVERT_STOU(Int8ToULongLong, INT8, ULONGLONG)
DEFINE_SAFE_CONVERT_STOU(ShortToUChar, SHORT, UINT8)
DEFINE_SAFE_CONVERT_STOU(ShortToUInt8, SHORT, UINT8)
DEFINE_SAFE_CONVERT_STOU(ShortToUShort, SHORT, USHORT)
DEFINE_SAFE_CONVERT_STOU(ShortToUInt, SHORT, UINT)
DEFINE_SAFE_CONVERT_STOU(ShortToULong, SHORT, ULONG)
DEFINE_SAFE_CONVERT_STOU(ShortToUIntPtr, SHORT, UINT_PTR)
DEFINE_SAFE_CONVERT_STOU(ShortToULongPtr, SHORT, ULONG_PTR)
DEFINE_SAFE_CONVERT_STOU(ShortToDWordPtr, SHORT, DWORD_PTR)
DEFINE_SAFE_CONVERT_STOU(ShortToULongLong, SHORT, ULONGLONG)
DEFINE_SAFE_CONVERT_STOU(IntToUChar, INT, UINT8)
DEFINE_SAFE_CONVERT_STOU(IntToUInt8, INT, UINT8)
DEFINE_SAFE_CONVERT_STOU(IntToUShort, INT, USHORT)
DEFINE_SAFE_CONVERT_STOU(IntToUInt, INT, UINT)
DEFINE_SAFE_CONVERT_STOU(IntToULong, INT, ULONG)
DEFINE_SAFE_CONVERT_STOU(IntToULongLong, INT, ULONGLONG)
DEFINE_SAFE_CONVERT_STOU(LongToUChar, LONG, UINT8)
DEFINE_SAFE_CONVERT_STOU(LongToUInt8, LONG, UINT8)
DEFINE_SAFE_CONVERT_STOU(LongToUShort, LONG, USHORT)
DEFINE_SAFE_CONVERT_STOU(LongToUInt, LONG, UINT)
DEFINE_SAFE_CONVERT_STOU(LongToULong, LONG, ULONG)
DEFINE_SAFE_CONVERT_STOU(LongToUIntPtr, LONG, UINT_PTR)
DEFINE_SAFE_CONVERT_STOU(LongToULongPtr, LONG, ULONG_PTR)
DEFINE_SAFE_CONVERT_STOU(LongToULongLong, LONG, ULONGLONG)
DEFINE_SAFE_CONVERT_STOU(IntPtrToUChar, INT_PTR, UINT8)
DEFINE_SAFE_CONVERT_STOU(IntPtrToUInt8, INT_PTR, UINT8)
DEFINE_SAFE_CONVERT_STOU(IntPtrToUShort, INT_PTR, USHORT)
DEFINE_SAFE_CONVERT_STOU(IntPtrToUInt, INT_PTR, UINT)
DEFINE_SAFE_CONVERT_STOU(IntPtrToULong, INT_PTR, ULONG)
DEFINE_SAFE_CONVERT_STOU(IntPtrToUIntPtr, INT_PTR, UINT_PTR)
DEFINE_SAFE_CONVERT_STOU(IntPtrToULongPtr, INT_PTR, ULONG_PTR)
DEFINE_SAFE_CONVERT_STOU(IntPtrToULongLong, INT_PTR, ULONGLONG)
DEFINE_SAFE_CONVERT_STOU(LongPtrToUChar, LONG_PTR, UINT8)
DEFINE_SAFE_CONVERT_STOU(LongPtrToUInt8, LONG_PTR, UINT8)
DEFINE_SAFE_CONVERT_STOU(LongPtrToUShort, LONG_PTR, USHORT)
DEFINE_SAFE_CONVERT_STOU(LongPtrToUInt, LONG_PTR, UINT)
DEFINE_SAFE_CONVERT_STOU(LongPtrToULong, LONG_PTR, ULONG)
DEFINE_SAFE_CONVERT_STOU(LongPtrToUIntPtr, LONG_PTR, UINT_PTR)
DEFINE_SAFE_CONVERT_STOU(LongPtrToULongPtr, LONG_PTR, ULONG_PTR)
DEFINE_SAFE_CONVERT_STOU(LongPtrToULongLong, LONG_PTR, ULONGLONG)
DEFINE_SAFE_CONVERT_STOU(LongLongToULong, LONGLONG, ULONG)
DEFINE_SAFE_CONVERT_STOU(LongLongToULongPtr, LONGLONG, ULONG_PTR)
#ifdef _CHAR_UNSIGNED
DEFINE_SAFE_CONVERT_STOU(ShortToChar, SHORT, UINT8)
DEFINE_SAFE_CONVERT_STOU(LongPtrToChar, LONG_PTR, UINT8)
#endif


/* Convert signed to signed */
#define DEFINE_SAFE_CONVERT_STOS(_Name, _TypeFrom, _TypeTo) \
_Must_inspect_result_ \
__forceinline \
INTSAFE_RESULT \
INTSAFE_NAME(_Name)( \
    _In_ _TypeFrom Input, \
    _Out_ _Deref_out_range_(==, Input) _TypeTo *pOutput) \
{ \
    if ((Input >= _TypeTo ## _MIN) && (Input <= _TypeTo ## _MAX)) \
    { \
        *pOutput = (_TypeTo)Input; \
        return INTSAFE_SUCCESS; \
    } \
    else \
    { \
        *pOutput = _TypeTo ## _ERROR; \
        return INTSAFE_E_ARITHMETIC_OVERFLOW; \
    } \
}

DEFINE_SAFE_CONVERT_STOS(ShortToInt8, SHORT, INT8)
DEFINE_SAFE_CONVERT_STOS(IntToInt8, INT, INT8)
DEFINE_SAFE_CONVERT_STOS(IntToShort, INT, SHORT)
DEFINE_SAFE_CONVERT_STOS(LongToInt8, LONG, INT8)
DEFINE_SAFE_CONVERT_STOS(LongToShort, LONG, SHORT)
DEFINE_SAFE_CONVERT_STOS(LongToInt, LONG, INT)
DEFINE_SAFE_CONVERT_STOS(IntPtrToInt8, INT_PTR, INT8)
DEFINE_SAFE_CONVERT_STOS(IntPtrToShort, INT_PTR, SHORT)
DEFINE_SAFE_CONVERT_STOS(IntPtrToInt, INT_PTR, INT)
DEFINE_SAFE_CONVERT_STOS(IntPtrToLong, INT_PTR, LONG)
DEFINE_SAFE_CONVERT_STOS(IntPtrToLongPtr, INT_PTR, LONG_PTR)
DEFINE_SAFE_CONVERT_STOS(LongPtrToInt8, LONG_PTR, INT8)
DEFINE_SAFE_CONVERT_STOS(LongPtrToShort, LONG_PTR, SHORT)
DEFINE_SAFE_CONVERT_STOS(LongPtrToInt, LONG_PTR, INT)
DEFINE_SAFE_CONVERT_STOS(LongPtrToLong, LONG_PTR, LONG)
DEFINE_SAFE_CONVERT_STOS(LongPtrToIntPtr, LONG_PTR, INT_PTR)
DEFINE_SAFE_CONVERT_STOS(LongLongToInt, LONGLONG, INT)
DEFINE_SAFE_CONVERT_STOS(LongLongToLong, LONGLONG, LONG)
DEFINE_SAFE_CONVERT_STOS(LongLongToIntPtr, LONGLONG, INT_PTR)
DEFINE_SAFE_CONVERT_STOS(LongLongToLongPtr, LONGLONG, LONG_PTR)
DEFINE_SAFE_CONVERT_STOS(ShortToChar, SHORT, _INTSAFE_CHAR)
DEFINE_SAFE_CONVERT_STOS(LongPtrToChar, LONG_PTR, _INTSAFE_CHAR)


#ifdef _NTINTSAFE_H_INCLUDED_

#define RtlInt8ToByte RtlInt8ToUInt8
#define RtlInt8ToUInt16 RtlInt8ToUShort
#define RtlInt8ToWord RtlInt8ToUShort
#define RtlInt8ToUInt32 RtlInt8ToUInt
#define RtlInt8ToDWord RtlInt8ToULong
#define RtlInt8ToDWordPtr RtlInt8ToULongPtr
#define RtlInt8ToDWordLong RtlInt8ToULongLong
#define RtlInt8ToULong64 RtlInt8ToULongLong
#define RtlInt8ToDWord64 RtlInt8ToULongLong
#define RtlInt8ToUInt64 RtlInt8ToULongLong
#define RtlInt8ToSizeT RtlInt8ToUIntPtr
#define RtlInt8ToSIZET RtlInt8ToULongPtr
#define RtlIntToSizeT RtlIntToUIntPtr
#define RtlIntToSIZET RtlIntToULongPtr
#define RtlULongToSSIZET RtlULongToLongPtr
#define RtlULongToByte RtlULongToUInt8
#define RtlULongLongToInt64 RtlULongLongToLongLong
#define RtlULongLongToLong64 RtlULongLongToLongLong
#define RtlULongLongToPtrdiffT RtlULongLongToIntPtr
#define RtlULongLongToSizeT RtlULongLongToUIntPtr
#define RtlULongLongToSSIZET RtlULongLongToLongPtr
#define RtlULongLongToSIZET RtlULongLongToULongPtr
#define RtlSizeTToULong RtlULongPtrToULong
#define RtlSIZETToULong RtlULongPtrToULong
#define RtlSSIZETToULongLong RtlLongPtrToULongLong
#define RtlSSIZETToULong RtlLongPtrToULong
#define RtlLongLongToSizeT RtlLongLongToUIntPtr
#define RtlLongLongToSSIZET RtlLongLongToLongPtr
#define RtlLongLongToSIZET RtlLongLongToULongPtr
#ifdef _WIN64
#define RtlIntToUIntPtr RtlIntToULongLong
#define RtlULongLongToIntPtr RtlULongLongToLongLong
#else
#define RtlIntToUIntPtr RtlIntToUInt
#define RtlULongLongToIntPtr RtlULongLongToInt
#define RtlULongLongToUIntPtr RtlULongLongToUInt
#define RtlULongLongToULongPtr RtlULongLongToULong
#endif

#else // _NTINTSAFE_H_INCLUDED_

#define Int8ToByte Int8ToUInt8
#define Int8ToUInt16 Int8ToUShort
#define Int8ToWord Int8ToUShort
#define Int8ToUInt32 Int8ToUInt
#define Int8ToDWord Int8ToULong
#define Int8ToDWordPtr Int8ToULongPtr
#define Int8ToDWordLong Int8ToULongLong
#define Int8ToULong64 Int8ToULongLong
#define Int8ToDWord64 Int8ToULongLong
#define Int8ToUInt64 Int8ToULongLong
#define Int8ToSizeT Int8ToUIntPtr
#define Int8ToSIZET Int8ToULongPtr
#define IntToSizeT IntToUIntPtr
#define IntToSIZET IntToULongPtr
#define ULongToSSIZET ULongToLongPtr
#define ULongToByte ULongToUInt8
#define ULongLongToInt64 ULongLongToLongLong
#define ULongLongToLong64 ULongLongToLongLong
#define ULongLongToPtrdiffT ULongLongToIntPtr
#define ULongLongToSizeT ULongLongToUIntPtr
#define ULongLongToSSIZET ULongLongToLongPtr
#define ULongLongToSIZET ULongLongToULongPtr
#define SizeTToULong ULongPtrToULong
#define SIZETToULong ULongPtrToULong
#define SSIZETToULongLong LongPtrToULongLong
#define SSIZETToULong LongPtrToULong
#define LongLongToSizeT LongLongToUIntPtr
#define LongLongToSSIZET LongLongToLongPtr
#define LongLongToSIZET LongLongToULongPtr
#ifdef _WIN64
#define IntToUIntPtr IntToULongLong
#define ULongLongToIntPtr ULongLongToLongLong
#else
#define IntToUIntPtr IntToUInt
#define ULongLongToIntPtr ULongLongToInt
#define ULongLongToUIntPtr ULongLongToUInt
#define ULongLongToULongPtr ULongLongToULong
#endif

#endif // _NTINTSAFE_H_INCLUDED_


#define DEFINE_SAFE_ADD(_Name, _Type) \
_Must_inspect_result_ \
__forceinline \
INTSAFE_RESULT \
INTSAFE_NAME(_Name)( \
    _In_ _Type Augend, \
    _In_ _Type Addend, \
    _Out_ _Deref_out_range_(==, Augend + Addend) _Type *pOutput) \
{ \
    if ((_Type)(Augend + Addend) >= Augend) \
    { \
        *pOutput = Augend + Addend; \
        return INTSAFE_SUCCESS; \
    } \
    else \
    { \
        *pOutput = _Type ## _ERROR; \
        return INTSAFE_E_ARITHMETIC_OVERFLOW; \
    } \
}

DEFINE_SAFE_ADD(UInt8Add, UINT8)
DEFINE_SAFE_ADD(UShortAdd, USHORT)
DEFINE_SAFE_ADD(UIntAdd, UINT)
DEFINE_SAFE_ADD(ULongAdd, ULONG)
DEFINE_SAFE_ADD(UIntPtrAdd, UINT_PTR)
DEFINE_SAFE_ADD(ULongPtrAdd, ULONG_PTR)
DEFINE_SAFE_ADD(DWordPtrAdd, DWORD_PTR)
DEFINE_SAFE_ADD(SizeTAdd, size_t)
DEFINE_SAFE_ADD(SIZETAdd, SIZE_T)
DEFINE_SAFE_ADD(ULongLongAdd, ULONGLONG)


#define DEFINE_SAFE_SUB(_Name, _Type) \
_Must_inspect_result_ \
__forceinline \
INTSAFE_RESULT \
INTSAFE_NAME(_Name)( \
    _In_ _Type Minuend, \
    _In_ _Type Subtrahend, \
    _Out_ _Deref_out_range_(==, Minuend - Subtrahend) _Type* pOutput) \
{ \
    if (Minuend >= Subtrahend) \
    { \
        *pOutput = Minuend - Subtrahend; \
        return INTSAFE_SUCCESS; \
    } \
    else \
    { \
        *pOutput = _Type ## _ERROR; \
        return INTSAFE_E_ARITHMETIC_OVERFLOW; \
    } \
}

DEFINE_SAFE_SUB(UInt8Sub, UINT8)
DEFINE_SAFE_SUB(UShortSub, USHORT)
DEFINE_SAFE_SUB(UIntSub, UINT)
DEFINE_SAFE_SUB(UIntPtrSub, UINT_PTR)
DEFINE_SAFE_SUB(ULongSub, ULONG)
DEFINE_SAFE_SUB(ULongPtrSub, ULONG_PTR)
DEFINE_SAFE_SUB(DWordPtrSub, DWORD_PTR)
DEFINE_SAFE_SUB(SizeTSub, size_t)
DEFINE_SAFE_SUB(SIZETSub, SIZE_T)
DEFINE_SAFE_SUB(ULongLongSub, ULONGLONG)

#ifdef ENABLE_INTSAFE_SIGNED_FUNCTIONS
_Must_inspect_result_
__forceinline
INTSAFE_RESULT
INTSAFE_NAME(LongLongAdd)(
    _In_ LONGLONG Augend,
    _In_ LONGLONG Addend,
    _Out_ _Deref_out_range_(==, Augend + Addend) LONGLONG* pResult)
{
    LONGLONG Result = (LONGLONG)((ULONGLONG)Augend + (ULONGLONG)Addend);

    /* The only way the result can overflow, is when the sign of the augend
       and the addend are the same. In that case the result is expected to
       have the same sign as the two, otherwise it overflowed.
       Sign equality is checked with a binary xor operation. */
    if ( ((Augend ^ Addend) >= 0) && ((Augend ^ Result) < 0) )
    {
        *pResult = LONGLONG_ERROR;
        return INTSAFE_E_ARITHMETIC_OVERFLOW;
    }
    else
    {
        *pResult = Result;
        return INTSAFE_SUCCESS;
    }
}


#define DEFINE_SAFE_ADD_S(_Name, _Type1, _Type2, _Convert) \
C_ASSERT(sizeof(_Type2) > sizeof(_Type1)); \
_Must_inspect_result_ \
__forceinline \
INTSAFE_RESULT \
INTSAFE_NAME(_Name)( \
    _In_ _Type1 Augend, \
    _In_ _Type1 Addend, \
    _Out_ _Deref_out_range_(==, Augend + Addend) _Type1* pOutput) \
{ \
    return INTSAFE_NAME(_Convert)(((_Type2)Augend) + ((_Type2)Addend), pOutput); \
}

DEFINE_SAFE_ADD_S(Int8Add, INT8, SHORT, ShortToInt8)
DEFINE_SAFE_ADD_S(ShortAdd, SHORT, INT, IntToShort)
DEFINE_SAFE_ADD_S(IntAdd, INT, LONGLONG, LongLongToInt)
DEFINE_SAFE_ADD_S(LongAdd, LONG, LONGLONG, LongLongToLong)
#ifndef _WIN64
DEFINE_SAFE_ADD_S(IntPtrAdd, INT_PTR, LONGLONG, LongLongToIntPtr)
DEFINE_SAFE_ADD_S(LongPtrAdd, LONG_PTR, LONGLONG, LongLongToLongPtr)
#endif

_Must_inspect_result_
__forceinline
INTSAFE_RESULT
INTSAFE_NAME(LongLongSub)(
    _In_ LONGLONG Minuend,
    _In_ LONGLONG Subtrahend,
    _Out_ _Deref_out_range_(==, Minuend - Subtrahend) LONGLONG* pResult)
{
    LONGLONG Result = (LONGLONG)((ULONGLONG)Minuend - (ULONGLONG)Subtrahend);

    /* The only way the result can overflow, is when the sign of the minuend
       and the subtrahend differ. In that case the result is expected to
       have the same sign as the minuend, otherwise it overflowed.
       Sign equality is checked with a binary xor operation. */
    if ( ((Minuend ^ Subtrahend) < 0) && ((Minuend ^ Result) < 0) )
    {
        *pResult = LONGLONG_ERROR;
        return INTSAFE_E_ARITHMETIC_OVERFLOW;
    }
    else
    {
        *pResult = Result;
        return INTSAFE_SUCCESS;
    }
}


#define DEFINE_SAFE_SUB_S(_Name, _Type1, _Type2, _Convert) \
C_ASSERT(sizeof(_Type2) > sizeof(_Type1)); \
_Must_inspect_result_ \
__forceinline \
INTSAFE_RESULT \
INTSAFE_NAME(_Name)( \
    _In_ _Type1 Minuend, \
    _In_ _Type1 Subtrahend, \
    _Out_ _Deref_out_range_(==, Minuend - Subtrahend) _Type1* pOutput) \
{ \
    return INTSAFE_NAME(_Convert)(((_Type2)Minuend) - ((_Type2)Subtrahend), pOutput); \
}

DEFINE_SAFE_SUB_S(LongSub, LONG, LONGLONG, LongLongToLong)
#ifndef _WIN64
DEFINE_SAFE_SUB_S(IntPtrSub, INT_PTR, LONGLONG, LongLongToIntPtr)
DEFINE_SAFE_SUB_S(LongPtrSub, LONG_PTR, LONGLONG, LongLongToLongPtr)
#endif

#endif /* ENABLE_INTSAFE_SIGNED_FUNCTIONS */

_Must_inspect_result_
__forceinline
INTSAFE_RESULT
INTSAFE_NAME(ULongLongMult)(
    _In_ ULONGLONG Multiplicand,
    _In_ ULONGLONG Multiplier,
    _Out_ _Deref_out_range_(==, Multiplicand * Multiplier) ULONGLONG* pOutput)
{
    /* We can split the 64 bit numbers in low and high parts:
        M1 = M1Low + M1Hi * 0x100000000
        M2 = M2Low + M2Hi * 0x100000000

       Then the multiplication looks like this:
        M1 * M2 = (M1Low + M1Hi * 0x100000000) * (M2Low + M2Hi * 0x100000000)
                = M1Low * M2Low
                  + M1Low * M2Hi * 0x100000000
                  + M2Low * M1Hi * 0x100000000
                  + M1Hi * M2Hi * 0x100000000 * 0x100000000

        We get an overflow when
            a) M1Hi * M2Hi != 0, so when M1Hi and M2Hi are both not 0
            b) The product of the nonzero high part and the other low part
               is larger than 32 bits.
            c) The addition of the product from b) shifted left by 32 and
               M1Low * M2Low is larger than 64 bits
    */
    ULONG M1Low = Multiplicand & 0xffffffff;
    ULONG M2Low = Multiplier & 0xffffffff;
    ULONG M1Hi = Multiplicand >> 32;
    ULONG M2Hi = Multiplier >> 32;
    ULONGLONG Temp;

    if (M1Hi == 0)
    {
        Temp = UInt32x32To64(M1Low, M2Hi);
    }
    else if (M2Hi == 0)
    {
        Temp = UInt32x32To64(M1Hi, M2Low);
    }
    else
    {
        *pOutput = ULONGLONG_ERROR;
        return INTSAFE_E_ARITHMETIC_OVERFLOW;
    }

    if (Temp > ULONG_MAX)
    {
        *pOutput = ULONGLONG_ERROR;
        return INTSAFE_E_ARITHMETIC_OVERFLOW;
    }

    return INTSAFE_NAME(ULongLongAdd)(Temp << 32, UInt32x32To64(M1Low, M2Low), pOutput);
}


#define DEFINE_SAFE_MULT_U32(_Name, _Type, _Convert) \
_Must_inspect_result_ \
__forceinline \
INTSAFE_RESULT \
INTSAFE_NAME(_Name)( \
    _In_ _Type Multiplicand, \
    _In_ _Type Multiplier, \
    _Out_ _Deref_out_range_(==, Multiplicand * Multiplier) _Type* pOutput) \
{ \
    ULONGLONG Result = UInt32x32To64(Multiplicand, Multiplier); \
    return INTSAFE_NAME(_Convert)(Result, pOutput); \
}

DEFINE_SAFE_MULT_U32(UIntMult, UINT, ULongLongToUInt)
DEFINE_SAFE_MULT_U32(ULongMult, ULONG, ULongLongToULong)
#ifndef _WIN64
DEFINE_SAFE_MULT_U32(SizeTMult, size_t, ULongLongToSizeT)
DEFINE_SAFE_MULT_U32(SIZETMult, SIZE_T, ULongLongToSIZET)
#endif

#define DEFINE_SAFE_MULT_U16(_Name, _Type, _Convert) \
_Must_inspect_result_ \
__forceinline \
INTSAFE_RESULT \
INTSAFE_NAME(_Name)( \
    _In_ _Type Multiplicand, \
    _In_ _Type Multiplier, \
    _Out_ _Deref_out_range_(==, Multiplicand * Multiplier) _Type* pOutput) \
{ \
    ULONG Result = ((ULONG)Multiplicand) * ((ULONG)Multiplier); \
    return INTSAFE_NAME(_Convert)(Result, pOutput); \
}

DEFINE_SAFE_MULT_U16(UShortMult, USHORT, ULongToUShort)


#ifdef _NTINTSAFE_H_INCLUDED_

#define RtlUInt16Add RtlUShortAdd
#define RtlWordAdd RtlUShortAdd
#define RtlUInt32Add RtlUIntAdd
#define RtlDWordAdd RtlULongAdd
#define RtlDWordLongAdd RtlULongLongAdd
#define RtlULong64Add RtlULongLongAdd
#define RtlDWord64Add RtlULongLongAdd
#define RtlUInt64Add RtlULongLongAdd
#define RtlUInt16Sub RtlUShortSub
#define RtlWordSub RtlUShortSub
#define RtlUInt32Sub RtlUIntSub
#define RtlDWordSub RtlULongSub
#define RtlDWordLongSub RtlULongLongSub
#define RtlULong64Sub RtlULongLongSub
#define RtlDWord64Sub RtlULongLongSub
#define RtlUInt64Sub RtlULongLongSub
#define RtlUInt16Mult RtlUShortMult
#define RtlWordMult RtlUShortMult
#ifdef _WIN64
#define RtlIntPtrAdd RtlLongLongAdd
#define RtlLongPtrAdd RtlLongLongAdd
#define RtlIntPtrSub RtlLongLongSub
#define RtlLongPtrSub RtlLongLongSub
#define RtlSizeTMult RtlULongLongMult
#define RtlSIZETMult RtlULongLongMult
#else
#endif

#else // _NTINTSAFE_H_INCLUDED_

#define UInt16Add UShortAdd
#define WordAdd UShortAdd
#define UInt32Add UIntAdd
#define DWordAdd ULongAdd
#define DWordLongAdd ULongLongAdd
#define ULong64Add ULongLongAdd
#define DWord64Add ULongLongAdd
#define UInt64Add ULongLongAdd
#define UInt16Sub UShortSub
#define WordSub UShortSub
#define UInt32Sub UIntSub
#define DWordSub ULongSub
#define DWordLongSub ULongLongSub
#define ULong64Sub ULongLongSub
#define DWord64Sub ULongLongSub
#define UInt64Sub ULongLongSub
#define UInt16Mult UShortMult
#define WordMult UShortMult
#define UInt32Mult UIntMult
#define Int32ToUInt32 IntToUInt
#define SizeTToInt UIntPtrToInt
#ifdef _WIN64
#define IntPtrAdd LongLongAdd
#define LongPtrAdd LongLongAdd
#define IntPtrSub LongLongSub
#define LongPtrSub LongLongSub
#define SizeTMult ULongLongMult
#define SIZETMult ULongLongMult
#else
#endif

#undef _INTSAFE_CHAR_MIN
#undef _INTSAFE_CHAR_MAX
#undef _INTSAFE_CHAR_ERROR

#endif // _NTINTSAFE_H_INCLUDED_

#define DWord64Mult ULongLongMult
#define DWord64ToByte ULongLongToUInt8
#define DWord64ToChar ULongLongToChar
#define DWord64ToDWord ULongLongToULong
#define DWord64ToDWordPtr ULongLongToULongPtr
#define DWord64ToInt ULongLongToInt
#define DWord64ToInt16 ULongLongToShort
#define DWord64ToInt32 ULongLongToInt
#define DWord64ToInt64 ULongLongToLongLong
#define DWord64ToInt8 ULongLongToInt8
#define DWord64ToIntPtr ULongLongToIntPtr
#define DWord64ToLong ULongLongToLong
#define DWord64ToLong64 ULongLongToLongLong
#define DWord64ToLongLong ULongLongToLongLong
#define DWord64ToLongPtr ULongLongToLongPtr
#define DWord64ToPtrdiffT ULongLongToIntPtr
#define DWord64ToShort ULongLongToShort
#define DWord64ToSizeT ULongLongToUIntPtr
#define DWord64ToSIZET ULongLongToULongPtr
#define DWord64ToSSIZET ULongLongToLongPtr
#define DWord64ToUChar ULongLongToUChar
#define DWord64ToUInt ULongLongToUInt
#define DWord64ToUInt16 ULongLongToUShort
#define DWord64ToUInt32 ULongLongToUInt
#define DWord64ToUInt8 ULongLongToUInt8
#define DWord64ToUIntPtr ULongLongToUIntPtr
#define DWord64ToULong ULongLongToULong
#define DWord64ToULongPtr ULongLongToULongPtr
#define DWord64ToUShort ULongLongToUShort
#define DWord64ToWord ULongLongToUShort
#define DWordLongMult ULongLongMult
#define DWordLongToByte ULongLongToUInt8
#define DWordLongToChar ULongLongToChar
#define DWordLongToDWord ULongLongToULong
#define DWordLongToDWordPtr ULongLongToULongPtr
#define DWordLongToInt ULongLongToInt
#define DWordLongToInt16 ULongLongToShort
#define DWordLongToInt32 ULongLongToInt
#define DWordLongToInt64 ULongLongToLongLong
#define DWordLongToInt8 ULongLongToInt8
#define DWordLongToIntPtr ULongLongToIntPtr
#define DWordLongToLong ULongLongToLong
#define DWordLongToLong64 ULongLongToLongLong
#define DWordLongToLongLong ULongLongToLongLong
#define DWordLongToLongPtr ULongLongToLongPtr
#define DWordLongToPtrdiffT ULongLongToIntPtr
#define DWordLongToShort ULongLongToShort
#define DWordLongToSizeT ULongLongToUIntPtr
#define DWordLongToSIZET ULongLongToULongPtr
#define DWordLongToSSIZET ULongLongToLongPtr
#define DWordLongToUChar ULongLongToUChar
#define DWordLongToUInt ULongLongToUInt
#define DWordLongToUInt16 ULongLongToUShort
#define DWordLongToUInt32 ULongLongToUInt
#define DWordLongToUInt8 ULongLongToUInt8
#define DWordLongToUIntPtr ULongLongToUIntPtr
#define DWordLongToULong ULongLongToULong
#define DWordLongToULongPtr ULongLongToULongPtr
#define DWordLongToUShort ULongLongToUShort
#define DWordLongToWord ULongLongToUShort
#define DWordMult ULongMult
#if (defined(_WIN64))
#define DWordPtrMult ULongLongMult
#endif
#define DWordPtrToByte ULongPtrToUInt8
#define DWordPtrToChar ULongPtrToChar
#define DWordPtrToDWord ULongPtrToULong
#define DWordPtrToInt ULongPtrToInt
#define DWordPtrToInt16 ULongPtrToShort
#define DWordPtrToInt32 ULongPtrToInt
#define DWordPtrToInt64 ULongPtrToLongLong
#define DWordPtrToInt8 ULongPtrToInt8
#define DWordPtrToIntPtr ULongPtrToIntPtr
#define DWordPtrToLong ULongPtrToLong
#define DWordPtrToLong64 ULongPtrToLongLong
#define DWordPtrToLongLong ULongPtrToLongLong
#define DWordPtrToLongPtr ULongPtrToLongPtr
#define DWordPtrToPtrdiffT ULongPtrToIntPtr
#define DWordPtrToShort ULongPtrToShort
#define DWordPtrToSSIZET ULongPtrToLongPtr
#define DWordPtrToUChar ULongPtrToUChar
#define DWordPtrToUInt ULongPtrToUInt
#define DWordPtrToUInt16 ULongPtrToUShort
#define DWordPtrToUInt32 ULongPtrToUInt
#define DWordPtrToUInt8 ULongPtrToUInt8
#define DWordPtrToUIntPtr ULongPtrToUIntPtr
#define DWordPtrToULong ULongPtrToULong
#define DWordPtrToUShort ULongPtrToUShort
#define DWordPtrToWord ULongPtrToUShort
#define DWordToByte ULongToUInt8
#define DWordToChar ULongToChar
#define DWordToInt ULongToInt
#define DWordToInt16 ULongToShort
#define DWordToInt32 ULongToInt
#define DWordToInt8 ULongToInt8
#define DWordToIntPtr ULongToIntPtr
#define DWordToLong ULongToLong
#define DWordToLongPtr ULongToLongPtr
#define DWordToPtrdiffT ULongToIntPtr
#define DWordToShort ULongToShort
#define DWordToSSIZET ULongToLongPtr
#define DWordToUChar ULongToUChar
#define DWordToUInt ULongToUInt
#define DWordToUInt16 ULongToUShort
#define DWordToUInt32 ULongToUInt
#define DWordToUInt8 ULongToUInt8
#define DWordToUIntPtr ULongToUIntPtr
#define DWordToUShort ULongToUShort
#define DWordToWord ULongToUShort
#define Int16ToByte ShortToUInt8
#define Int16ToChar ShortToChar
#define Int16ToDWord ShortToULong
#define Int16ToDWord64 ShortToULongLong
#define Int16ToDWordLong ShortToULongLong
#define Int16ToDWordPtr ShortToULongPtr
#define Int16ToInt8 ShortToInt8
#define Int16ToSizeT ShortToUIntPtr
#define Int16ToSIZET ShortToULongPtr
#define Int16ToUChar ShortToUChar
#define Int16ToUInt ShortToUInt
#define Int16ToUInt16 ShortToUShort
#define Int16ToUInt32 ShortToUInt
#define Int16ToUInt64 ShortToULongLong
#define Int16ToUInt8 ShortToUInt8
#define Int16ToUIntPtr ShortToUIntPtr
#define Int16ToULong ShortToULong
#define Int16ToULong64 ShortToULongLong
#define Int16ToULongLong ShortToULongLong
#define Int16ToULongPtr ShortToULongPtr
#define Int16ToUShort ShortToUShort
#define Int16ToWord ShortToUShort
#define Int32ToByte IntToUInt8
#define Int32ToChar IntToChar
#define Int32ToDWord IntToULong
#define Int32ToDWord64 IntToULongLong
#define Int32ToDWordLong IntToULongLong
#define Int32ToDWordPtr IntToULongPtr
#define Int32ToInt16 IntToShort
#define Int32ToInt8 IntToInt8
#define Int32ToShort IntToShort
#define Int32ToSizeT IntToUIntPtr
#define Int32ToSIZET IntToULongPtr
#define Int32ToUChar IntToUChar
#define Int32ToUInt IntToUInt
#define Int32ToUInt16 IntToUShort
#define Int32ToUInt64 IntToULongLong
#define Int32ToUInt8 IntToUInt8
#define Int32ToUIntPtr IntToUIntPtr
#define Int32ToULong IntToULong
#define Int32ToULong64 IntToULongLong
#define Int32ToULongLong IntToULongLong
#define Int32ToULongPtr IntToULongPtr
#define Int32ToUShort IntToUShort
#define Int32ToWord IntToUShort
#define Int64ToByte LongLongToUInt8
#define Int64ToChar LongLongToChar
#define Int64ToDWord LongLongToULong
#define Int64ToDWord64 LongLongToULongLong
#define Int64ToDWordLong LongLongToULongLong
#define Int64ToDWordPtr LongLongToULongPtr
#define Int64ToInt LongLongToInt
#define Int64ToInt16 LongLongToShort
#define Int64ToInt32 LongLongToInt
#define Int64ToInt8 LongLongToInt8
#define Int64ToIntPtr LongLongToIntPtr
#define Int64ToLong LongLongToLong
#define Int64ToLongPtr LongLongToLongPtr
#define Int64ToPtrdiffT LongLongToIntPtr
#define Int64ToShort LongLongToShort
#define Int64ToSizeT LongLongToUIntPtr
#define Int64ToSIZET LongLongToULongPtr
#define Int64ToSSIZET LongLongToLongPtr
#define Int64ToUChar LongLongToUChar
#define Int64ToUInt LongLongToUInt
#define Int64ToUInt16 LongLongToUShort
#define Int64ToUInt32 LongLongToUInt
#define Int64ToUInt64 LongLongToULongLong
#define Int64ToUInt8 LongLongToUInt8
#define Int64ToUIntPtr LongLongToUIntPtr
#define Int64ToULong LongLongToULong
#define Int64ToULong64 LongLongToULongLong
#define Int64ToULongLong LongLongToULongLong
#define Int64ToULongPtr LongLongToULongPtr
#define Int64ToUShort LongLongToUShort
#define Int64ToWord LongLongToUShort
#define IntPtrToByte IntPtrToUInt8
#define IntPtrToDWord IntPtrToULong
#define IntPtrToDWord64 IntPtrToULongLong
#define IntPtrToDWordLong IntPtrToULongLong
#define IntPtrToDWordPtr IntPtrToULongPtr
#define IntPtrToInt16 IntPtrToShort
#define IntPtrToInt32 IntPtrToInt
#define IntPtrToSizeT IntPtrToUIntPtr
#define IntPtrToSIZET IntPtrToULongPtr
#define IntPtrToUInt16 IntPtrToUShort
#define IntPtrToUInt32 IntPtrToUInt
#define IntPtrToUInt64 IntPtrToULongLong
#define IntPtrToULong64 IntPtrToULongLong
#define IntPtrToWord IntPtrToUShort
#define IntToByte IntToUInt8
#define IntToDWord IntToULong
#define IntToDWord64 IntToULongLong
#define IntToDWordLong IntToULongLong
#define IntToDWordPtr IntToULongPtr
#define IntToInt16 IntToShort
#define IntToUInt16 IntToUShort
#define IntToUInt64 IntToULongLong
#define IntToULong64 IntToULongLong
#define IntToWord IntToUShort
#define Long64ToByte LongLongToUInt8
#define Long64ToChar LongLongToChar
#define Long64ToDWord LongLongToULong
#define Long64ToDWordPtr LongLongToULongPtr
#define Long64ToInt LongLongToInt
#define Long64ToInt16 LongLongToShort
#define Long64ToInt32 LongLongToInt
#define Long64ToInt8 LongLongToInt8
#define Long64ToIntPtr LongLongToIntPtr
#define Long64ToLong LongLongToLong
#define Long64ToLongPtr LongLongToLongPtr
#define Long64ToPtrdiffT LongLongToIntPtr
#define Long64ToShort LongLongToShort
#define Long64ToSizeT LongLongToUIntPtr
#define Long64ToSIZET LongLongToULongPtr
#define Long64ToSSIZET LongLongToLongPtr
#define Long64ToUChar LongLongToUChar
#define Long64ToUInt LongLongToUInt
#define Long64ToUInt16 LongLongToUShort
#define Long64ToUInt32 LongLongToUInt
#define Long64ToUInt8 LongLongToUInt8
#define Long64ToUIntPtr LongLongToUIntPtr
#define Long64ToULong LongLongToULong
#define Long64ToULongLong LongLongToULongLong
#define Long64ToULongPtr LongLongToULongPtr
#define Long64ToUShort LongLongToUShort
#define Long64ToWord LongLongToUShort
#define LongLongToByte LongLongToUInt8
#define LongLongToDWord LongLongToULong
#define LongLongToDWord64 LongLongToULongLong
#define LongLongToDWordLong LongLongToULongLong
#define LongLongToDWordPtr LongLongToULongPtr
#define LongLongToInt16 LongLongToShort
#define LongLongToInt32 LongLongToInt
#define LongLongToPtrdiffT LongLongToIntPtr
#define LongLongToUInt16 LongLongToUShort
#define LongLongToUInt32 LongLongToUInt
#define LongLongToUInt64 LongLongToULongLong
#define LongLongToULong64 LongLongToULongLong
#define LongLongToWord LongLongToUShort
#define LongPtrToByte LongPtrToUInt8
#define LongPtrToDWord LongPtrToULong
#define LongPtrToDWord64 LongPtrToULongLong
#define LongPtrToDWordLong LongPtrToULongLong
#define LongPtrToDWordPtr LongPtrToULongPtr
#define LongPtrToInt16 LongPtrToShort
#define LongPtrToInt32 LongPtrToInt
#define LongPtrToSizeT LongPtrToUIntPtr
#define LongPtrToSIZET LongPtrToULongPtr
#define LongPtrToUInt16 LongPtrToUShort
#define LongPtrToUInt32 LongPtrToUInt
#define LongPtrToUInt64 LongPtrToULongLong
#define LongPtrToULong64 LongPtrToULongLong
#define LongPtrToWord LongPtrToUShort
#define LongToByte LongToUInt8
#define LongToDWord LongToULong
#define LongToDWord64 LongToULongLong
#define LongToDWordLong LongToULongLong
#define LongToDWordPtr LongToULongPtr
#define LongToInt16 LongToShort
#define LongToInt32 LongToInt
#define LongToPtrdiffT LongToIntPtr
#define LongToSizeT LongToUIntPtr
#define LongToSIZET LongToULongPtr
#define LongToUInt16 LongToUShort
#define LongToUInt32 LongToUInt
#define LongToUInt64 LongToULongLong
#define LongToULong64 LongToULongLong
#define LongToWord LongToUShort
#define PtrdiffTToByte IntPtrToUInt8
#define PtrdiffTToChar IntPtrToChar
#define PtrdiffTToDWord IntPtrToULong
#define PtrdiffTToDWord64 IntPtrToULongLong
#define PtrdiffTToDWordLong IntPtrToULongLong
#define PtrdiffTToDWordPtr IntPtrToULongPtr
#define PtrdiffTToInt IntPtrToInt
#define PtrdiffTToInt16 IntPtrToShort
#define PtrdiffTToInt32 IntPtrToInt
#define PtrdiffTToInt8 IntPtrToInt8
#define PtrdiffTToLong IntPtrToLong
#define PtrdiffTToLongPtr IntPtrToLongPtr
#define PtrdiffTToShort IntPtrToShort
#define PtrdiffTToSizeT IntPtrToUIntPtr
#define PtrdiffTToSIZET IntPtrToULongPtr
#define PtrdiffTToUChar IntPtrToUChar
#define PtrdiffTToUInt IntPtrToUInt
#define PtrdiffTToUInt16 IntPtrToUShort
#define PtrdiffTToUInt32 IntPtrToUInt
#define PtrdiffTToUInt64 IntPtrToULongLong
#define PtrdiffTToUInt8 IntPtrToUInt8
#define PtrdiffTToUIntPtr IntPtrToUIntPtr
#define PtrdiffTToULong IntPtrToULong
#define PtrdiffTToULong64 IntPtrToULongLong
#define PtrdiffTToULongLong IntPtrToULongLong
#define PtrdiffTToULongPtr IntPtrToULongPtr
#define PtrdiffTToUShort IntPtrToUShort
#define PtrdiffTToWord IntPtrToUShort
#define ShortToByte ShortToUInt8
#define ShortToDWord ShortToULong
#define ShortToDWord64 ShortToULongLong
#define ShortToDWordLong ShortToULongLong
#define ShortToSizeT ShortToUIntPtr
#define ShortToSIZET ShortToULongPtr
#define ShortToUInt16 ShortToUShort
#define ShortToUInt32 ShortToUInt
#define ShortToUInt64 ShortToULongLong
#define ShortToULong64 ShortToULongLong
#define ShortToWord ShortToUShort
#define SizeTToByte UIntPtrToUInt8
#define SIZETToByte ULongPtrToUInt8
#define SizeTToChar UIntPtrToChar
#define SIZETToChar ULongPtrToChar
#define SizeTToDWord UIntPtrToULong
#define SIZETToDWord ULongPtrToULong
#define SIZETToInt ULongPtrToInt
#define SizeTToInt16 UIntPtrToShort
#define SIZETToInt16 ULongPtrToShort
#define SizeTToInt32 UIntPtrToInt
#define SIZETToInt32 ULongPtrToInt
#define SizeTToInt64 UIntPtrToLongLong
#define SIZETToInt64 ULongPtrToLongLong
#define SizeTToInt8 UIntPtrToInt8
#define SIZETToInt8 ULongPtrToInt8
#define SizeTToIntPtr UIntPtrToIntPtr
#define SIZETToIntPtr ULongPtrToIntPtr
#define SizeTToLong UIntPtrToLong
#define SIZETToLong ULongPtrToLong
#define SizeTToLong64 UIntPtrToLongLong
#define SIZETToLong64 ULongPtrToLongLong
#define SizeTToLongLong UIntPtrToLongLong
#define SIZETToLongLong ULongPtrToLongLong
#define SizeTToLongPtr UIntPtrToLongPtr
#define SIZETToLongPtr ULongPtrToLongPtr
#define SizeTToPtrdiffT UIntPtrToIntPtr
#define SIZETToPtrdiffT ULongPtrToIntPtr
#define SizeTToShort UIntPtrToShort
#define SIZETToShort ULongPtrToShort
#define SizeTToSSIZET UIntPtrToLongPtr
#define SIZETToSSIZET ULongPtrToLongPtr
#define SizeTToUChar UIntPtrToUChar
#define SIZETToUChar ULongPtrToUChar
#define SizeTToUInt UIntPtrToUInt
#define SIZETToUInt ULongPtrToUInt
#define SizeTToUInt16 UIntPtrToUShort
#define SIZETToUInt16 ULongPtrToUShort
#define SizeTToUInt32 UIntPtrToUInt
#define SIZETToUInt32 ULongPtrToUInt
#define SizeTToUInt8 UIntPtrToUInt8
#define SIZETToUInt8 ULongPtrToUInt8
#define SIZETToUIntPtr ULongPtrToUIntPtr
#define SizeTToUShort UIntPtrToUShort
#define SIZETToUShort ULongPtrToUShort
#define SizeTToWord UIntPtrToUShort
#define SIZETToWord ULongPtrToUShort
#define SSIZETToByte LongPtrToUInt8
#define SSIZETToChar LongPtrToChar
#define SSIZETToDWord LongPtrToULong
#define SSIZETToDWord64 LongPtrToULongLong
#define SSIZETToDWordLong LongPtrToULongLong
#define SSIZETToDWordPtr LongPtrToULongPtr
#define SSIZETToInt LongPtrToInt
#define SSIZETToInt16 LongPtrToShort
#define SSIZETToInt32 LongPtrToInt
#define SSIZETToInt8 LongPtrToInt8
#define SSIZETToIntPtr LongPtrToIntPtr
#define SSIZETToLong LongPtrToLong
#define SSIZETToShort LongPtrToShort
#define SSIZETToSizeT LongPtrToUIntPtr
#define SSIZETToSIZET LongPtrToULongPtr
#define SSIZETToUChar LongPtrToUChar
#define SSIZETToUInt LongPtrToUInt
#define SSIZETToUInt16 LongPtrToUShort
#define SSIZETToUInt32 LongPtrToUInt
#define SSIZETToUInt64 LongPtrToULongLong
#define SSIZETToUInt8 LongPtrToUInt8
#define SSIZETToUIntPtr LongPtrToUIntPtr
#define SSIZETToULong64 LongPtrToULongLong
#define SSIZETToULongPtr LongPtrToULongPtr
#define SSIZETToUShort LongPtrToUShort
#define SSIZETToWord LongPtrToUShort
#define UInt16ToByte UShortToUInt8
#define UInt16ToChar UShortToChar
#define UInt16ToInt16 UShortToShort
#define UInt16ToInt8 UShortToInt8
#define UInt16ToShort UShortToShort
#define UInt16ToUChar UShortToUChar
#define UInt16ToUInt8 UShortToUInt8
#define UInt32ToByte UInt32ToUInt8
#define UInt32ToChar UIntToChar
#define UInt32ToInt UIntToInt
#define UInt32ToInt16 UIntToShort
#define UInt32ToInt32 UIntToInt
#define UInt32ToInt8 UIntToInt8
#define UInt32ToIntPtr UIntToIntPtr
#define UInt32ToLong UIntToLong
#define UInt32ToLongPtr UIntToLongPtr
#define UInt32ToPtrdiffT UIntToPtrdiffT
#define UInt32ToShort UIntToShort
#define UInt32ToSSIZET UIntToSSIZET
#define UInt32ToUChar UIntToUChar
#define UInt32ToUInt16 UIntToUShort
#define UInt32ToUInt8 UIntToUInt8
#define UInt32ToUShort UIntToUShort
#define UInt32ToWord UIntToUShort
#define UInt64Mult ULongLongMult
#define UInt64ToByte ULongLongToUInt8
#define UInt64ToChar ULongLongToChar
#define UInt64ToDWord ULongLongToULong
#define UInt64ToDWordPtr ULongLongToULongPtr
#define UInt64ToInt ULongLongToInt
#define UInt64ToInt16 ULongLongToShort
#define UInt64ToInt32 ULongLongToInt
#define UInt64ToInt64 ULongLongToLongLong
#define UInt64ToInt8 ULongLongToInt8
#define UInt64ToIntPtr ULongLongToIntPtr
#define UInt64ToLong ULongLongToLong
#define UInt64ToLong64 ULongLongToLongLong
#define UInt64ToLongLong ULongLongToLongLong
#define UInt64ToLongPtr ULongLongToLongPtr
#define UInt64ToPtrdiffT ULongLongToIntPtr
#define UInt64ToShort ULongLongToShort
#define UInt64ToSizeT ULongLongToUIntPtr
#define UInt64ToSIZET ULongLongToULongPtr
#define UInt64ToSSIZET ULongLongToLongPtr
#define UInt64ToUChar ULongLongToUChar
#define UInt64ToUInt ULongLongToUInt
#define UInt64ToUInt16 ULongLongToUShort
#define UInt64ToUInt32 ULongLongToUInt
#define UInt64ToUInt8 ULongLongToUInt8
#define UInt64ToUIntPtr ULongLongToUIntPtr
#define UInt64ToULong ULongLongToULong
#define UInt64ToULongPtr ULongLongToULongPtr
#define UInt64ToUShort ULongLongToUShort
#define UInt64ToWord ULongLongToUShort
#if (defined(_WIN64))
#define UIntPtrMult ULongLongMult
#endif
#define UIntPtrToByte UIntPtrToUInt8
#define UIntPtrToDWord UIntPtrToULong
#define UIntPtrToInt32 UIntPtrToInt
#define UIntPtrToInt64 UIntPtrToLongLong
#define UIntPtrToLong64 UIntPtrToLongLong
#if (defined(_WIN64))
#define UIntPtrToLongLong ULongLongToLongLong
#endif
#define UIntPtrToPtrdiffT UIntPtrToIntPtr
#define UIntPtrToSSIZET UIntPtrToLongPtr
#if (defined(_WIN64))
#define UIntPtrToUInt ULongLongToUInt
#endif
#define UIntPtrToUInt32 UIntPtrToUInt
#if (defined(_WIN64))
#define UIntPtrToULong ULongLongToULong
#endif
#define UIntPtrToWord UIntPtrToUShort
#define UIntToByte UIntToUInt8
#define UIntToInt16 UIntToShort
#define UIntToInt32 UIntToInt
#define UIntToPtrdiffT UIntToIntPtr
#define UIntToSSIZET UIntToLongPtr
#define UIntToUInt16 UIntToUShort
#define UIntToWord UIntToUShort
#define ULong64Mult ULongLongMult
#define ULong64ToByte ULongLongToUInt8
#define ULong64ToChar ULongLongToChar
#define ULong64ToDWord ULongLongToULong
#define ULong64ToDWordPtr ULongLongToULongPtr
#define ULong64ToInt ULongLongToInt
#define ULong64ToInt16 ULongLongToShort
#define ULong64ToInt32 ULongLongToInt
#define ULong64ToInt64 ULongLongToLongLong
#define ULong64ToInt8 ULongLongToInt8
#define ULong64ToIntPtr ULongLongToIntPtr
#define ULong64ToLong ULongLongToLong
#define ULong64ToLong64 ULongLongToLongLong
#define ULong64ToLongLong ULongLongToLongLong
#define ULong64ToLongPtr ULongLongToLongPtr
#define ULong64ToPtrdiffT ULongLongToIntPtr
#define ULong64ToShort ULongLongToShort
#define ULong64ToSizeT ULongLongToUIntPtr
#define ULong64ToSIZET ULongLongToULongPtr
#define ULong64ToSSIZET ULongLongToLongPtr
#define ULong64ToUChar ULongLongToUChar
#define ULong64ToUInt ULongLongToUInt
#define ULong64ToUInt16 ULongLongToUShort
#define ULong64ToUInt32 ULongLongToUInt
#define ULong64ToUInt8 ULongLongToUInt8
#define ULong64ToUIntPtr ULongLongToUIntPtr
#define ULong64ToULong ULongLongToULong
#define ULong64ToULongPtr ULongLongToULongPtr
#define ULong64ToUShort ULongLongToUShort
#define ULong64ToWord ULongLongToUShort
#define ULongLongToByte ULongLongToUInt8
#define ULongLongToDWord ULongLongToULong
#define ULongLongToDWordPtr ULongLongToULongPtr
#define ULongLongToInt16 ULongLongToShort
#define ULongLongToInt32 ULongLongToInt
#define ULongLongToUInt16 ULongLongToUShort
#define ULongLongToUInt32 ULongLongToUInt
#define ULongLongToWord ULongLongToUShort
#if (defined(_WIN64))
#define ULongPtrMult ULongLongMult
#endif
#define ULongPtrToByte ULongPtrToUInt8
#define ULongPtrToDWord ULongPtrToULong
#define ULongPtrToInt16 ULongPtrToShort
#define ULongPtrToInt32 ULongPtrToInt
#define ULongPtrToInt64 ULongPtrToLongLong
#define ULongPtrToLong64 ULongPtrToLongLong
#if (defined(_WIN64))
#define ULongPtrToLongLong ULongLongToLongLong
#endif
#define ULongPtrToPtrdiffT ULongPtrToIntPtr
#define ULongPtrToSSIZET ULongPtrToLongPtr
#if (defined(_WIN64))
#define ULongPtrToUInt ULongLongToUInt
#endif
#define ULongPtrToUInt16 ULongPtrToUShort
#define ULongPtrToUInt32 ULongPtrToUInt
#define ULongPtrToWord ULongPtrToUShort
#define ULongToInt16 ULongToShort
#define ULongToInt32 ULongToInt
#define ULongToPtrdiffT ULongToIntPtr
#define ULongToUInt16 ULongToUShort
#define ULongToUInt32 ULongToUInt
#define ULongToWord ULongToUShort
#define UShortToByte UShortToUInt8
#define UShortToInt16 UShortToShort
#define WordToByte UShortToUInt8
#define WordToChar UShortToChar
#define WordToInt16 UShortToShort
#define WordToInt8 UShortToInt8
#define WordToShort UShortToShort
#define WordToUChar UShortToUChar
#define WordToUInt8 UShortToUInt8

#ifdef __cplusplus
extern "C" {
#endif

/* TODO: LiberNT does not implement the functions below yet */
HRESULT IntPtrToChar(INT_PTR iOperand, CHAR *pch);
HRESULT LongLongToUInt8(LONGLONG llOperand, UINT8 *pu8Result);
#if (defined(_WIN64))
HRESULT LongToIntPtr(LONG lOperand, INT_PTR *piResult);
#endif
HRESULT UInt8Mult(UINT8 u8Multiplicand, UINT8 u8Multiplier, UINT8 *pu8Result);
HRESULT UIntPtrToChar(UINT_PTR uOperand, CHAR *pch);
#if (defined(_WIN64))
HRESULT UIntToIntPtr(UINT uOperand, INT_PTR *piResult);
HRESULT UIntToLongPtr(UINT uOperand, LONG_PTR *plResult);
#endif
HRESULT ULongLongToUInt8(ULONGLONG ullOperand, UINT8 *pu8Result);
HRESULT ULongPtrToChar(ULONG_PTR ulOperand, CHAR *pch);
HRESULT ULongPtrToInt(ULONG_PTR ulOperand, INT *piResult);
HRESULT ULongPtrToInt8(ULONG_PTR ulOperand, INT8 *pi8Result);
HRESULT ULongPtrToIntPtr(ULONG_PTR ulOperand, INT_PTR *piResult);
HRESULT ULongPtrToLong(ULONG_PTR ulOperand, LONG *plResult);
HRESULT ULongPtrToLongPtr(ULONG_PTR ulOperand, LONG_PTR *plResult);
HRESULT ULongPtrToShort(ULONG_PTR ulOperand, SHORT *psResult);
HRESULT ULongPtrToUChar(ULONG_PTR ulOperand, UCHAR *pch);
HRESULT ULongPtrToUInt8(ULONG_PTR ulOperand, UINT8 *pui8Result);
HRESULT ULongPtrToUIntPtr(ULONG_PTR ulOperand, UINT_PTR *puResult);
HRESULT ULongPtrToUShort(ULONG_PTR ulOperand, USHORT *pusResult);

#ifdef __cplusplus
}
#endif

#define __UnsignedMultiply128 _umul128

#endif // !_INTSAFE_H_INCLUDED_
