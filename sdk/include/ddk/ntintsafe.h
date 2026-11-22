/*
 * ntintsafe.h
 *
 * Windows NT helper functions for integer overflow prevention
 *
 * This file is part of the ReactOS DDK package.
 *
 * Contributors:
 *   Timo Kreuzer (timo.kreuzer@reactos.org)
 *
 * THIS SOFTWARE IS NOT COPYRIGHTED
 *
 * This source code is offered for use in the public domain. You may
 * use, modify or distribute it freely.
 *
 * This code is distributed in the hope that it will be useful but
 * WITHOUT ANY WARRANTY. ALL WARRANTIES, EXPRESS OR IMPLIED ARE HEREBY
 * DISCLAIMED. This includes but is not limited to warranties of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 */
#pragma once

#ifndef _NTINTSAFE_H_INCLUDED_
#define _NTINTSAFE_H_INCLUDED_

/* Include the sdk version */
#include <intsafe.h>

/* We don't want this one */
#undef _INTSAFE_H_INCLUDED_

#if !defined(LODWORD)

#define LODWORD(_qw)    ((DWORD)(_qw))

#endif
#if !defined(HIDWORD)

#define HIDWORD(_qw)    ((DWORD)(((_qw) >> 32) & 0xffffffff))

#endif

#if (!defined(_M_CEE) &&      ((defined(_M_X64) && !defined(_M_ARM64EC)) ||       (defined(_ARM64_MULT_INTRINS_SUPPORTED) && _ARM64_MULT_INTRINS_SUPPORTED) ||       (defined(_M_IA64) && (_MSC_VER >= 1400))))
#define _USE_INTRINSIC_MULTIPLY128
#endif
#define RtlDWord64Mult RtlULongLongMult
#define RtlDWord64ToByte RtlULongLongToUInt8
#define RtlDWord64ToChar RtlULongLongToChar
#define RtlDWord64ToDWord RtlULongLongToULong
#define RtlDWord64ToDWordPtr RtlULongLongToULongPtr
#define RtlDWord64ToInt RtlULongLongToInt
#define RtlDWord64ToInt16 RtlULongLongToShort
#define RtlDWord64ToInt32 RtlULongLongToInt
#define RtlDWord64ToInt64 RtlULongLongToLongLong
#define RtlDWord64ToInt8 RtlULongLongToInt8
#define RtlDWord64ToIntPtr RtlULongLongToIntPtr
#define RtlDWord64ToLong RtlULongLongToLong
#define RtlDWord64ToLong64 RtlULongLongToLongLong
#define RtlDWord64ToLongLong RtlULongLongToLongLong
#define RtlDWord64ToLongPtr RtlULongLongToLongPtr
#define RtlDWord64ToPtrdiffT RtlULongLongToIntPtr
#define RtlDWord64ToShort RtlULongLongToShort
#define RtlDWord64ToSizeT RtlULongLongToUIntPtr
#define RtlDWord64ToSIZET RtlULongLongToULongPtr
#define RtlDWord64ToSSIZET RtlULongLongToLongPtr
#define RtlDWord64ToUChar RtlULongLongToUChar
#define RtlDWord64ToUInt RtlULongLongToUInt
#define RtlDWord64ToUInt16 RtlULongLongToUShort
#define RtlDWord64ToUInt32 RtlULongLongToUInt
#define RtlDWord64ToUInt8 RtlULongLongToUInt8
#define RtlDWord64ToUIntPtr RtlULongLongToUIntPtr
#define RtlDWord64ToULong RtlULongLongToULong
#define RtlDWord64ToULongPtr RtlULongLongToULongPtr
#define RtlDWord64ToUShort RtlULongLongToUShort
#define RtlDWord64ToWord RtlULongLongToUShort
#define RtlDWordLongMult RtlULongLongMult
#define RtlDWordLongToByte RtlULongLongToUInt8
#define RtlDWordLongToChar RtlULongLongToChar
#define RtlDWordLongToDWord RtlULongLongToULong
#define RtlDWordLongToDWordPtr RtlULongLongToULongPtr
#define RtlDWordLongToInt RtlULongLongToInt
#define RtlDWordLongToInt16 RtlULongLongToShort
#define RtlDWordLongToInt32 RtlULongLongToInt
#define RtlDWordLongToInt64 RtlULongLongToLongLong
#define RtlDWordLongToInt8 RtlULongLongToInt8
#define RtlDWordLongToIntPtr RtlULongLongToIntPtr
#define RtlDWordLongToLong RtlULongLongToLong
#define RtlDWordLongToLong64 RtlULongLongToLongLong
#define RtlDWordLongToLongLong RtlULongLongToLongLong
#define RtlDWordLongToLongPtr RtlULongLongToLongPtr
#define RtlDWordLongToPtrdiffT RtlULongLongToIntPtr
#define RtlDWordLongToShort RtlULongLongToShort
#define RtlDWordLongToSizeT RtlULongLongToUIntPtr
#define RtlDWordLongToSIZET RtlULongLongToULongPtr
#define RtlDWordLongToSSIZET RtlULongLongToLongPtr
#define RtlDWordLongToUChar RtlULongLongToUChar
#define RtlDWordLongToUInt RtlULongLongToUInt
#define RtlDWordLongToUInt16 RtlULongLongToUShort
#define RtlDWordLongToUInt32 RtlULongLongToUInt
#define RtlDWordLongToUInt8 RtlULongLongToUInt8
#define RtlDWordLongToUIntPtr RtlULongLongToUIntPtr
#define RtlDWordLongToULong RtlULongLongToULong
#define RtlDWordLongToULongPtr RtlULongLongToULongPtr
#define RtlDWordLongToUShort RtlULongLongToUShort
#define RtlDWordLongToWord RtlULongLongToUShort
#define RtlDWordMult RtlULongMult
#if (defined(_WIN64))
#define RtlDWordPtrAdd RtlULongLongAdd
#define RtlDWordPtrMult RtlULongLongMult
#define RtlDWordPtrSub RtlULongLongSub
#endif
#define RtlDWordPtrToByte RtlULongPtrToUInt8
#define RtlDWordPtrToChar RtlULongPtrToChar
#define RtlDWordPtrToDWord RtlULongPtrToULong
#define RtlDWordPtrToInt RtlULongPtrToInt
#define RtlDWordPtrToInt16 RtlULongPtrToShort
#define RtlDWordPtrToInt32 RtlULongPtrToInt
#define RtlDWordPtrToInt64 RtlULongPtrToLongLong
#define RtlDWordPtrToInt8 RtlULongPtrToInt8
#define RtlDWordPtrToIntPtr RtlULongPtrToIntPtr
#define RtlDWordPtrToLong RtlULongPtrToLong
#define RtlDWordPtrToLong64 RtlULongPtrToLongLong
#define RtlDWordPtrToLongLong RtlULongPtrToLongLong
#define RtlDWordPtrToLongPtr RtlULongPtrToLongPtr
#define RtlDWordPtrToPtrdiffT RtlULongPtrToIntPtr
#define RtlDWordPtrToShort RtlULongPtrToShort
#define RtlDWordPtrToSSIZET RtlULongPtrToLongPtr
#define RtlDWordPtrToUChar RtlULongPtrToUChar
#define RtlDWordPtrToUInt RtlULongPtrToUInt
#define RtlDWordPtrToUInt16 RtlULongPtrToUShort
#define RtlDWordPtrToUInt32 RtlULongPtrToUInt
#define RtlDWordPtrToUInt8 RtlULongPtrToUInt8
#define RtlDWordPtrToUIntPtr RtlULongPtrToUIntPtr
#define RtlDWordPtrToULong RtlULongPtrToULong
#define RtlDWordPtrToUShort RtlULongPtrToUShort
#define RtlDWordPtrToWord RtlULongPtrToUShort
#define RtlDWordToByte RtlULongToUInt8
#define RtlDWordToChar RtlULongToChar
#define RtlDWordToInt RtlULongToInt
#define RtlDWordToInt16 RtlULongToShort
#define RtlDWordToInt32 RtlULongToInt
#define RtlDWordToInt8 RtlULongToInt8
#define RtlDWordToIntPtr RtlULongToIntPtr
#define RtlDWordToLong RtlULongToLong
#define RtlDWordToLongPtr RtlULongToLongPtr
#define RtlDWordToPtrdiffT RtlULongToIntPtr
#define RtlDWordToShort RtlULongToShort
#define RtlDWordToSSIZET RtlULongToLongPtr
#define RtlDWordToUChar RtlULongToUChar
#define RtlDWordToUInt RtlULongToUInt
#define RtlDWordToUInt16 RtlULongToUShort
#define RtlDWordToUInt32 RtlULongToUInt
#define RtlDWordToUInt8 RtlULongToUInt8
#define RtlDWordToUIntPtr RtlULongToUIntPtr
#define RtlDWordToUShort RtlULongToUShort
#define RtlDWordToWord RtlULongToUShort
#define RtlInt16ToByte RtlShortToUInt8
#define RtlInt16ToChar RtlShortToChar
#define RtlInt16ToDWord RtlShortToULong
#define RtlInt16ToDWord64 RtlShortToULongLong
#define RtlInt16ToDWordLong RtlShortToULongLong
#define RtlInt16ToDWordPtr RtlShortToULongPtr
#define RtlInt16ToInt8 RtlShortToInt8
#define RtlInt16ToSizeT RtlShortToUIntPtr
#define RtlInt16ToSIZET RtlShortToULongPtr
#define RtlInt16ToUChar RtlShortToUChar
#define RtlInt16ToUInt RtlShortToUInt
#define RtlInt16ToUInt16 RtlShortToUShort
#define RtlInt16ToUInt32 RtlShortToUInt
#define RtlInt16ToUInt64 RtlShortToULongLong
#define RtlInt16ToUInt8 RtlShortToUInt8
#define RtlInt16ToUIntPtr RtlShortToUIntPtr
#define RtlInt16ToULong RtlShortToULong
#define RtlInt16ToULong64 RtlShortToULongLong
#define RtlInt16ToULongLong RtlShortToULongLong
#define RtlInt16ToULongPtr RtlShortToULongPtr
#define RtlInt16ToUShort RtlShortToUShort
#define RtlInt16ToWord RtlShortToUShort
#define RtlInt32ToByte RtlIntToUInt8
#define RtlInt32ToChar RtlIntToChar
#define RtlInt32ToDWord RtlIntToULong
#define RtlInt32ToDWord64 RtlIntToULongLong
#define RtlInt32ToDWordLong RtlIntToULongLong
#define RtlInt32ToDWordPtr RtlIntToULongPtr
#define RtlInt32ToInt16 RtlIntToShort
#define RtlInt32ToInt8 RtlIntToInt8
#define RtlInt32ToShort RtlIntToShort
#define RtlInt32ToSizeT RtlIntToUIntPtr
#define RtlInt32ToSIZET RtlIntToULongPtr
#define RtlInt32ToUChar RtlIntToUChar
#define RtlInt32ToUInt RtlIntToUInt
#define RtlInt32ToUInt16 RtlIntToUShort
#define RtlInt32ToUInt32 RtlIntToUInt
#define RtlInt32ToUInt64 RtlIntToULongLong
#define RtlInt32ToUInt8 RtlIntToUInt8
#define RtlInt32ToUIntPtr RtlIntToUIntPtr
#define RtlInt32ToULong RtlIntToULong
#define RtlInt32ToULong64 RtlIntToULongLong
#define RtlInt32ToULongLong RtlIntToULongLong
#define RtlInt32ToULongPtr RtlIntToULongPtr
#define RtlInt32ToUShort RtlIntToUShort
#define RtlInt32ToWord RtlIntToUShort
#define RtlInt64ToByte RtlLongLongToUInt8
#define RtlInt64ToChar RtlLongLongToChar
#define RtlInt64ToDWord RtlLongLongToULong
#define RtlInt64ToDWord64 RtlLongLongToULongLong
#define RtlInt64ToDWordLong RtlLongLongToULongLong
#define RtlInt64ToDWordPtr RtlLongLongToULongPtr
#define RtlInt64ToInt RtlLongLongToInt
#define RtlInt64ToInt16 RtlLongLongToShort
#define RtlInt64ToInt32 RtlLongLongToInt
#define RtlInt64ToInt8 RtlLongLongToInt8
#define RtlInt64ToIntPtr RtlLongLongToIntPtr
#define RtlInt64ToLong RtlLongLongToLong
#define RtlInt64ToLongPtr RtlLongLongToLongPtr
#define RtlInt64ToPtrdiffT RtlLongLongToIntPtr
#define RtlInt64ToShort RtlLongLongToShort
#define RtlInt64ToSizeT RtlLongLongToUIntPtr
#define RtlInt64ToSIZET RtlLongLongToULongPtr
#define RtlInt64ToSSIZET RtlLongLongToLongPtr
#define RtlInt64ToUChar RtlLongLongToUChar
#define RtlInt64ToUInt RtlLongLongToUInt
#define RtlInt64ToUInt16 RtlLongLongToUShort
#define RtlInt64ToUInt32 RtlLongLongToUInt
#define RtlInt64ToUInt64 RtlLongLongToULongLong
#define RtlInt64ToUInt8 RtlLongLongToUInt8
#define RtlInt64ToUIntPtr RtlLongLongToUIntPtr
#define RtlInt64ToULong RtlLongLongToULong
#define RtlInt64ToULong64 RtlLongLongToULongLong
#define RtlInt64ToULongLong RtlLongLongToULongLong
#define RtlInt64ToULongPtr RtlLongLongToULongPtr
#define RtlInt64ToUShort RtlLongLongToUShort
#define RtlInt64ToWord RtlLongLongToUShort
#define RtlIntPtrToByte RtlIntPtrToUInt8
#define RtlIntPtrToDWord RtlIntPtrToULong
#define RtlIntPtrToDWord64 RtlIntPtrToULongLong
#define RtlIntPtrToDWordLong RtlIntPtrToULongLong
#define RtlIntPtrToDWordPtr RtlIntPtrToULongPtr
#if (defined(_WIN64))
#define RtlIntPtrToInt RtlLongLongToInt
#endif
#define RtlIntPtrToInt16 RtlIntPtrToShort
#define RtlIntPtrToInt32 RtlIntPtrToInt
#if (defined(_WIN64))
#define RtlIntPtrToLong RtlLongLongToLong
#endif
#define RtlIntPtrToSizeT RtlIntPtrToUIntPtr
#define RtlIntPtrToSIZET RtlIntPtrToULongPtr
#if (defined(_WIN64))
#define RtlIntPtrToUInt RtlLongLongToUInt
#endif
#define RtlIntPtrToUInt16 RtlIntPtrToUShort
#define RtlIntPtrToUInt32 RtlIntPtrToUInt
#define RtlIntPtrToUInt64 RtlIntPtrToULongLong
#if (defined(_WIN64))
#define RtlIntPtrToUIntPtr RtlLongLongToULongLong
#define RtlIntPtrToULong RtlLongLongToULong
#endif
#define RtlIntPtrToULong64 RtlIntPtrToULongLong
#if (defined(_WIN64))
#define RtlIntPtrToULongLong RtlLongLongToULongLong
#define RtlIntPtrToULongPtr RtlLongLongToULongLong
#endif
#define RtlIntPtrToWord RtlIntPtrToUShort
#define RtlIntToByte RtlIntToUInt8
#define RtlIntToDWord RtlIntToULong
#define RtlIntToDWord64 RtlIntToULongLong
#define RtlIntToDWordLong RtlIntToULongLong
#define RtlIntToDWordPtr RtlIntToULongPtr
#define RtlIntToInt16 RtlIntToShort
#define RtlIntToUInt16 RtlIntToUShort
#define RtlIntToUInt64 RtlIntToULongLong
#define RtlIntToULong64 RtlIntToULongLong
#define RtlIntToWord RtlIntToUShort
#define RtlLong64ToByte RtlLongLongToUInt8
#define RtlLong64ToChar RtlLongLongToChar
#define RtlLong64ToDWord RtlLongLongToULong
#define RtlLong64ToDWordPtr RtlLongLongToULongPtr
#define RtlLong64ToInt RtlLongLongToInt
#define RtlLong64ToInt16 RtlLongLongToShort
#define RtlLong64ToInt32 RtlLongLongToInt
#define RtlLong64ToInt8 RtlLongLongToInt8
#define RtlLong64ToIntPtr RtlLongLongToIntPtr
#define RtlLong64ToLong RtlLongLongToLong
#define RtlLong64ToLongPtr RtlLongLongToLongPtr
#define RtlLong64ToPtrdiffT RtlLongLongToIntPtr
#define RtlLong64ToShort RtlLongLongToShort
#define RtlLong64ToSizeT RtlLongLongToUIntPtr
#define RtlLong64ToSIZET RtlLongLongToULongPtr
#define RtlLong64ToSSIZET RtlLongLongToLongPtr
#define RtlLong64ToUChar RtlLongLongToUChar
#define RtlLong64ToUInt RtlLongLongToUInt
#define RtlLong64ToUInt16 RtlLongLongToUShort
#define RtlLong64ToUInt32 RtlLongLongToUInt
#define RtlLong64ToUInt8 RtlLongLongToUInt8
#define RtlLong64ToUIntPtr RtlLongLongToUIntPtr
#define RtlLong64ToULong RtlLongLongToULong
#define RtlLong64ToULongLong RtlLongLongToULongLong
#define RtlLong64ToULongPtr RtlLongLongToULongPtr
#define RtlLong64ToUShort RtlLongLongToUShort
#define RtlLong64ToWord RtlLongLongToUShort
#define RtlLongLongToByte RtlLongLongToUInt8
#define RtlLongLongToDWord RtlLongLongToULong
#define RtlLongLongToDWord64 RtlLongLongToULongLong
#define RtlLongLongToDWordLong RtlLongLongToULongLong
#define RtlLongLongToDWordPtr RtlLongLongToULongPtr
#define RtlLongLongToInt16 RtlLongLongToShort
#define RtlLongLongToInt32 RtlLongLongToInt
#define RtlLongLongToPtrdiffT RtlLongLongToIntPtr
#define RtlLongLongToUInt16 RtlLongLongToUShort
#define RtlLongLongToUInt32 RtlLongLongToUInt
#define RtlLongLongToUInt64 RtlLongLongToULongLong
#define RtlLongLongToULong64 RtlLongLongToULongLong
#define RtlLongLongToWord RtlLongLongToUShort
#define RtlLongPtrToByte RtlLongPtrToUInt8
#define RtlLongPtrToDWord RtlLongPtrToULong
#define RtlLongPtrToDWord64 RtlLongPtrToULongLong
#define RtlLongPtrToDWordLong RtlLongPtrToULongLong
#define RtlLongPtrToDWordPtr RtlLongPtrToULongPtr
#if (defined(_WIN64))
#define RtlLongPtrToInt RtlLongLongToInt
#endif
#define RtlLongPtrToInt16 RtlLongPtrToShort
#define RtlLongPtrToInt32 RtlLongPtrToInt
#if (defined(_WIN64))
#define RtlLongPtrToLong RtlLongLongToLong
#endif
#define RtlLongPtrToSizeT RtlLongPtrToUIntPtr
#define RtlLongPtrToSIZET RtlLongPtrToULongPtr
#if (defined(_WIN64))
#define RtlLongPtrToUInt RtlLongLongToUInt
#endif
#define RtlLongPtrToUInt16 RtlLongPtrToUShort
#define RtlLongPtrToUInt32 RtlLongPtrToUInt
#define RtlLongPtrToUInt64 RtlLongPtrToULongLong
#define RtlLongPtrToULong64 RtlLongPtrToULongLong
#define RtlLongPtrToWord RtlLongPtrToUShort
#define RtlLongToByte RtlLongToUInt8
#define RtlLongToDWord RtlLongToULong
#define RtlLongToDWord64 RtlLongToULongLong
#define RtlLongToDWordLong RtlLongToULongLong
#define RtlLongToDWordPtr RtlLongToULongPtr
#define RtlLongToInt16 RtlLongToShort
#define RtlLongToInt32 RtlLongToInt
#define RtlLongToPtrdiffT RtlLongToIntPtr
#define RtlLongToSizeT RtlLongToUIntPtr
#define RtlLongToSIZET RtlLongToULongPtr
#define RtlLongToUInt16 RtlLongToUShort
#define RtlLongToUInt32 RtlLongToUInt
#define RtlLongToUInt64 RtlLongToULongLong
#define RtlLongToULong64 RtlLongToULongLong
#define RtlLongToWord RtlLongToUShort
#define RtlPtrdiffTToByte RtlIntPtrToUInt8
#define RtlPtrdiffTToChar RtlIntPtrToChar
#define RtlPtrdiffTToDWord RtlIntPtrToULong
#define RtlPtrdiffTToDWord64 RtlIntPtrToULongLong
#define RtlPtrdiffTToDWordLong RtlIntPtrToULongLong
#define RtlPtrdiffTToDWordPtr RtlIntPtrToULongPtr
#define RtlPtrdiffTToInt RtlIntPtrToInt
#define RtlPtrdiffTToInt16 RtlIntPtrToShort
#define RtlPtrdiffTToInt32 RtlIntPtrToInt
#define RtlPtrdiffTToInt8 RtlIntPtrToInt8
#define RtlPtrdiffTToLong RtlIntPtrToLong
#define RtlPtrdiffTToLongPtr RtlIntPtrToLongPtr
#define RtlPtrdiffTToShort RtlIntPtrToShort
#define RtlPtrdiffTToSizeT RtlIntPtrToUIntPtr
#define RtlPtrdiffTToSIZET RtlIntPtrToULongPtr
#define RtlPtrdiffTToUChar RtlIntPtrToUChar
#define RtlPtrdiffTToUInt RtlIntPtrToUInt
#define RtlPtrdiffTToUInt16 RtlIntPtrToUShort
#define RtlPtrdiffTToUInt32 RtlIntPtrToUInt
#define RtlPtrdiffTToUInt64 RtlIntPtrToULongLong
#define RtlPtrdiffTToUInt8 RtlIntPtrToUInt8
#define RtlPtrdiffTToUIntPtr RtlIntPtrToUIntPtr
#define RtlPtrdiffTToULong RtlIntPtrToULong
#define RtlPtrdiffTToULong64 RtlIntPtrToULongLong
#define RtlPtrdiffTToULongLong RtlIntPtrToULongLong
#define RtlPtrdiffTToULongPtr RtlIntPtrToULongPtr
#define RtlPtrdiffTToUShort RtlIntPtrToUShort
#define RtlPtrdiffTToWord RtlIntPtrToUShort
#define RtlShortToByte RtlShortToUInt8
#define RtlShortToDWord RtlShortToULong
#define RtlShortToDWord64 RtlShortToULongLong
#define RtlShortToDWordLong RtlShortToULongLong
#define RtlShortToSizeT RtlShortToUIntPtr
#define RtlShortToSIZET RtlShortToULongPtr
#define RtlShortToUInt16 RtlShortToUShort
#define RtlShortToUInt32 RtlShortToUInt
#define RtlShortToUInt64 RtlShortToULongLong
#define RtlShortToULong64 RtlShortToULongLong
#define RtlShortToWord RtlShortToUShort
#if (defined(_WIN64))
#define RtlSIZETAdd RtlULongLongAdd
#define RtlSIZETSub RtlULongLongSub
#endif
#define RtlSizeTToByte RtlUIntPtrToUInt8
#define RtlSIZETToByte RtlULongPtrToUInt8
#define RtlSizeTToChar RtlUIntPtrToChar
#define RtlSIZETToChar RtlULongPtrToChar
#define RtlSizeTToDWord RtlUIntPtrToULong
#define RtlSIZETToDWord RtlULongPtrToULong
#define RtlSizeTToInt RtlUIntPtrToInt
#define RtlSIZETToInt RtlULongPtrToInt
#define RtlSizeTToInt16 RtlUIntPtrToShort
#define RtlSIZETToInt16 RtlULongPtrToShort
#define RtlSizeTToInt32 RtlUIntPtrToInt
#define RtlSIZETToInt32 RtlULongPtrToInt
#define RtlSizeTToInt64 RtlUIntPtrToLongLong
#define RtlSIZETToInt64 RtlULongPtrToLongLong
#define RtlSizeTToInt8 RtlUIntPtrToInt8
#define RtlSIZETToInt8 RtlULongPtrToInt8
#define RtlSizeTToIntPtr RtlUIntPtrToIntPtr
#define RtlSIZETToIntPtr RtlULongPtrToIntPtr
#define RtlSizeTToLong RtlUIntPtrToLong
#define RtlSIZETToLong RtlULongPtrToLong
#define RtlSizeTToLong64 RtlUIntPtrToLongLong
#define RtlSIZETToLong64 RtlULongPtrToLongLong
#define RtlSizeTToLongLong RtlUIntPtrToLongLong
#define RtlSIZETToLongLong RtlULongPtrToLongLong
#define RtlSizeTToLongPtr RtlUIntPtrToLongPtr
#define RtlSIZETToLongPtr RtlULongPtrToLongPtr
#define RtlSizeTToPtrdiffT RtlUIntPtrToIntPtr
#define RtlSIZETToPtrdiffT RtlULongPtrToIntPtr
#define RtlSizeTToShort RtlUIntPtrToShort
#define RtlSIZETToShort RtlULongPtrToShort
#define RtlSizeTToSSIZET RtlUIntPtrToLongPtr
#define RtlSIZETToSSIZET RtlULongPtrToLongPtr
#define RtlSizeTToUChar RtlUIntPtrToUChar
#define RtlSIZETToUChar RtlULongPtrToUChar
#define RtlSizeTToUInt RtlUIntPtrToUInt
#define RtlSIZETToUInt RtlULongPtrToUInt
#define RtlSizeTToUInt16 RtlUIntPtrToUShort
#define RtlSIZETToUInt16 RtlULongPtrToUShort
#define RtlSizeTToUInt32 RtlUIntPtrToUInt
#define RtlSIZETToUInt32 RtlULongPtrToUInt
#define RtlSizeTToUInt8 RtlUIntPtrToUInt8
#define RtlSIZETToUInt8 RtlULongPtrToUInt8
#define RtlSIZETToUIntPtr RtlULongPtrToUIntPtr
#define RtlSizeTToUShort RtlUIntPtrToUShort
#define RtlSIZETToUShort RtlULongPtrToUShort
#define RtlSizeTToWord RtlUIntPtrToUShort
#define RtlSIZETToWord RtlULongPtrToUShort
#define RtlSSIZETToByte RtlLongPtrToUInt8
#define RtlSSIZETToChar RtlLongPtrToChar
#define RtlSSIZETToDWord RtlLongPtrToULong
#define RtlSSIZETToDWord64 RtlLongPtrToULongLong
#define RtlSSIZETToDWordLong RtlLongPtrToULongLong
#define RtlSSIZETToDWordPtr RtlLongPtrToULongPtr
#define RtlSSIZETToInt RtlLongPtrToInt
#define RtlSSIZETToInt16 RtlLongPtrToShort
#define RtlSSIZETToInt32 RtlLongPtrToInt
#define RtlSSIZETToInt8 RtlLongPtrToInt8
#define RtlSSIZETToIntPtr RtlLongPtrToIntPtr
#define RtlSSIZETToLong RtlLongPtrToLong
#define RtlSSIZETToShort RtlLongPtrToShort
#define RtlSSIZETToSizeT RtlLongPtrToUIntPtr
#define RtlSSIZETToSIZET RtlLongPtrToULongPtr
#define RtlSSIZETToUChar RtlLongPtrToUChar
#define RtlSSIZETToUInt RtlLongPtrToUInt
#define RtlSSIZETToUInt16 RtlLongPtrToUShort
#define RtlSSIZETToUInt32 RtlLongPtrToUInt
#define RtlSSIZETToUInt64 RtlLongPtrToULongLong
#define RtlSSIZETToUInt8 RtlLongPtrToUInt8
#define RtlSSIZETToUIntPtr RtlLongPtrToUIntPtr
#define RtlSSIZETToULong64 RtlLongPtrToULongLong
#define RtlSSIZETToULongPtr RtlLongPtrToULongPtr
#define RtlSSIZETToUShort RtlLongPtrToUShort
#define RtlSSIZETToWord RtlLongPtrToUShort
#define RtlUInt16ToByte RtlUShortToUInt8
#define RtlUInt16ToChar RtlUShortToChar
#define RtlUInt16ToInt16 RtlUShortToShort
#define RtlUInt16ToInt8 RtlUShortToInt8
#define RtlUInt16ToShort RtlUShortToShort
#define RtlUInt16ToUChar RtlUShortToUChar
#define RtlUInt16ToUInt8 RtlUShortToUInt8
#define RtlUInt32Mult RtlUIntMult
#define RtlUInt32ToByte RtlUInt32ToUInt8
#define RtlUInt32ToChar RtlUIntToChar
#define RtlUInt32ToInt RtlUIntToInt
#define RtlUInt32ToInt16 RtlUIntToShort
#define RtlUInt32ToInt32 RtlUIntToInt
#define RtlUInt32ToInt8 RtlUIntToInt8
#define RtlUInt32ToIntPtr RtlUIntToIntPtr
#define RtlUInt32ToLong RtlUIntToLong
#define RtlUInt32ToLongPtr RtlUIntToLongPtr
#define RtlUInt32ToPtrdiffT RtlUIntToPtrdiffT
#define RtlUInt32ToShort RtlUIntToShort
#define RtlUInt32ToSSIZET RtlUIntToSSIZET
#define RtlUInt32ToUChar RtlUIntToUChar
#define RtlUInt32ToUInt16 RtlUIntToUShort
#define RtlUInt32ToUInt8 RtlUIntToUInt8
#define RtlUInt32ToUShort RtlUIntToUShort
#define RtlUInt32ToWord RtlUIntToUShort
#define RtlUInt64Mult RtlULongLongMult
#define RtlUInt64ToByte RtlULongLongToUInt8
#define RtlUInt64ToChar RtlULongLongToChar
#define RtlUInt64ToDWord RtlULongLongToULong
#define RtlUInt64ToDWordPtr RtlULongLongToULongPtr
#define RtlUInt64ToInt RtlULongLongToInt
#define RtlUInt64ToInt16 RtlULongLongToShort
#define RtlUInt64ToInt32 RtlULongLongToInt
#define RtlUInt64ToInt64 RtlULongLongToLongLong
#define RtlUInt64ToInt8 RtlULongLongToInt8
#define RtlUInt64ToIntPtr RtlULongLongToIntPtr
#define RtlUInt64ToLong RtlULongLongToLong
#define RtlUInt64ToLong64 RtlULongLongToLongLong
#define RtlUInt64ToLongLong RtlULongLongToLongLong
#define RtlUInt64ToLongPtr RtlULongLongToLongPtr
#define RtlUInt64ToPtrdiffT RtlULongLongToIntPtr
#define RtlUInt64ToShort RtlULongLongToShort
#define RtlUInt64ToSizeT RtlULongLongToUIntPtr
#define RtlUInt64ToSIZET RtlULongLongToULongPtr
#define RtlUInt64ToSSIZET RtlULongLongToLongPtr
#define RtlUInt64ToUChar RtlULongLongToUChar
#define RtlUInt64ToUInt RtlULongLongToUInt
#define RtlUInt64ToUInt16 RtlULongLongToUShort
#define RtlUInt64ToUInt32 RtlULongLongToUInt
#define RtlUInt64ToUInt8 RtlULongLongToUInt8
#define RtlUInt64ToUIntPtr RtlULongLongToUIntPtr
#define RtlUInt64ToULong RtlULongLongToULong
#define RtlUInt64ToULongPtr RtlULongLongToULongPtr
#define RtlUInt64ToUShort RtlULongLongToUShort
#define RtlUInt64ToWord RtlULongLongToUShort
#if (defined(_WIN64))
#define RtlUIntPtrAdd RtlULongLongAdd
#define RtlUIntPtrMult RtlULongLongMult
#define RtlUIntPtrSub RtlULongLongSub
#endif
#define RtlUIntPtrToByte RtlUIntPtrToUInt8
#define RtlUIntPtrToDWord RtlUIntPtrToULong
#define RtlUIntPtrToInt32 RtlUIntPtrToInt
#define RtlUIntPtrToInt64 RtlUIntPtrToLongLong
#define RtlUIntPtrToLong64 RtlUIntPtrToLongLong
#if (defined(_WIN64))
#define RtlUIntPtrToLongLong RtlULongLongToLongLong
#endif
#define RtlUIntPtrToPtrdiffT RtlUIntPtrToIntPtr
#define RtlUIntPtrToSSIZET RtlUIntPtrToLongPtr
#if (defined(_WIN64))
#define RtlUIntPtrToUInt RtlULongLongToUInt
#endif
#define RtlUIntPtrToUInt32 RtlUIntPtrToUInt
#if (defined(_WIN64))
#define RtlUIntPtrToULong RtlULongLongToULong
#endif
#define RtlUIntPtrToWord RtlUIntPtrToUShort
#define RtlUIntToByte RtlUIntToUInt8
#define RtlUIntToInt16 RtlUIntToShort
#define RtlUIntToInt32 RtlUIntToInt
#define RtlUIntToPtrdiffT RtlUIntToIntPtr
#define RtlUIntToSSIZET RtlUIntToLongPtr
#define RtlUIntToUInt16 RtlUIntToUShort
#define RtlUIntToWord RtlUIntToUShort
#define RtlULong64Mult RtlULongLongMult
#define RtlULong64ToByte RtlULongLongToUInt8
#define RtlULong64ToChar RtlULongLongToChar
#define RtlULong64ToDWord RtlULongLongToULong
#define RtlULong64ToDWordPtr RtlULongLongToULongPtr
#define RtlULong64ToInt RtlULongLongToInt
#define RtlULong64ToInt16 RtlULongLongToShort
#define RtlULong64ToInt32 RtlULongLongToInt
#define RtlULong64ToInt64 RtlULongLongToLongLong
#define RtlULong64ToInt8 RtlULongLongToInt8
#define RtlULong64ToIntPtr RtlULongLongToIntPtr
#define RtlULong64ToLong RtlULongLongToLong
#define RtlULong64ToLong64 RtlULongLongToLongLong
#define RtlULong64ToLongLong RtlULongLongToLongLong
#define RtlULong64ToLongPtr RtlULongLongToLongPtr
#define RtlULong64ToPtrdiffT RtlULongLongToIntPtr
#define RtlULong64ToShort RtlULongLongToShort
#define RtlULong64ToSizeT RtlULongLongToUIntPtr
#define RtlULong64ToSIZET RtlULongLongToULongPtr
#define RtlULong64ToSSIZET RtlULongLongToLongPtr
#define RtlULong64ToUChar RtlULongLongToUChar
#define RtlULong64ToUInt RtlULongLongToUInt
#define RtlULong64ToUInt16 RtlULongLongToUShort
#define RtlULong64ToUInt32 RtlULongLongToUInt
#define RtlULong64ToUInt8 RtlULongLongToUInt8
#define RtlULong64ToUIntPtr RtlULongLongToUIntPtr
#define RtlULong64ToULong RtlULongLongToULong
#define RtlULong64ToULongPtr RtlULongLongToULongPtr
#define RtlULong64ToUShort RtlULongLongToUShort
#define RtlULong64ToWord RtlULongLongToUShort
#define RtlULongLongToByte RtlULongLongToUInt8
#define RtlULongLongToDWord RtlULongLongToULong
#define RtlULongLongToDWordPtr RtlULongLongToULongPtr
#define RtlULongLongToInt16 RtlULongLongToShort
#define RtlULongLongToInt32 RtlULongLongToInt
#define RtlULongLongToUInt16 RtlULongLongToUShort
#define RtlULongLongToUInt32 RtlULongLongToUInt
#define RtlULongLongToWord RtlULongLongToUShort
#if (defined(_WIN64))
#define RtlULongPtrAdd RtlULongLongAdd
#define RtlULongPtrMult RtlULongLongMult
#define RtlULongPtrSub RtlULongLongSub
#endif
#define RtlULongPtrToByte RtlULongPtrToUInt8
#define RtlULongPtrToDWord RtlULongPtrToULong
#define RtlULongPtrToInt16 RtlULongPtrToShort
#define RtlULongPtrToInt32 RtlULongPtrToInt
#define RtlULongPtrToInt64 RtlULongPtrToLongLong
#define RtlULongPtrToLong64 RtlULongPtrToLongLong
#if (defined(_WIN64))
#define RtlULongPtrToLongLong RtlULongLongToLongLong
#endif
#define RtlULongPtrToPtrdiffT RtlULongPtrToIntPtr
#define RtlULongPtrToSSIZET RtlULongPtrToLongPtr
#if (defined(_WIN64))
#define RtlULongPtrToUInt RtlULongLongToUInt
#endif
#define RtlULongPtrToUInt16 RtlULongPtrToUShort
#define RtlULongPtrToUInt32 RtlULongPtrToUInt
#define RtlULongPtrToWord RtlULongPtrToUShort
#define RtlULongToInt16 RtlULongToShort
#define RtlULongToInt32 RtlULongToInt
#define RtlULongToPtrdiffT RtlULongToIntPtr
#define RtlULongToUInt16 RtlULongToUShort
#define RtlULongToUInt32 RtlULongToUInt
#define RtlULongToWord RtlULongToUShort
#define RtlUShortToByte RtlUShortToUInt8
#define RtlUShortToInt16 RtlUShortToShort
#define RtlWordToByte RtlUShortToUInt8
#define RtlWordToChar RtlUShortToChar
#define RtlWordToInt16 RtlUShortToShort
#define RtlWordToInt8 RtlUShortToInt8
#define RtlWordToShort RtlUShortToShort
#define RtlWordToUChar RtlUShortToUChar
#define RtlWordToUInt8 RtlUShortToUInt8

typedef long long RtlINT64;

#ifdef __cplusplus
extern "C" {
#endif

/* TODO: LiberNT does not implement the functions below yet */
NTSTATUS RtlIntPtrToChar(INT_PTR iOperand, CHAR *pch);
NTSTATUS RtlIntToChar(INT iOperand, CHAR *pch);
NTSTATUS RtlLongLongToChar(LONGLONG llOperand, CHAR *pch);
NTSTATUS RtlLongLongToInt8(LONGLONG llOperand, INT8 *pi8Result);
NTSTATUS RtlLongLongToShort(LONGLONG llOperand, SHORT *psResult);
NTSTATUS RtlLongLongToUChar(LONGLONG llOperand, UCHAR *pch);
NTSTATUS RtlLongLongToUInt(LONGLONG llOperand, UINT *puResult);
NTSTATUS RtlLongLongToUInt8(LONGLONG llOperand, UINT8 *pu8Result);
NTSTATUS RtlLongLongToULongLong(LONGLONG llOperand, ULONGLONG *pullResult);
NTSTATUS RtlLongLongToUShort(LONGLONG llOperand, USHORT *pusResult);
NTSTATUS RtlLongToChar(LONG lOperand, CHAR *pch);
#if (defined(_WIN64))
NTSTATUS RtlLongToIntPtr(LONG lOperand, INT_PTR *piResult);
#endif
NTSTATUS RtlUInt8Mult(UINT8 u8Multiplicand, UINT8 u8Multiplier, UINT8 *pu8Result);
NTSTATUS RtlUIntPtrToChar(UINT_PTR uOperand, CHAR *pch);
NTSTATUS RtlUIntToChar(UINT uOperand, CHAR *pch);
#if (defined(_WIN64))
NTSTATUS RtlUIntToIntPtr(UINT uOperand, INT_PTR *piResult);
#endif
#if (defined(_WIN64))
NTSTATUS RtlUIntToLongPtr(UINT uOperand, LONG_PTR *plResult);
#endif
NTSTATUS RtlULongLongToChar(ULONGLONG ullOperand, CHAR *pch);
NTSTATUS RtlULongLongToInt8(ULONGLONG ullOperand, INT8 *pi8Result);
NTSTATUS RtlULongLongToLong(ULONGLONG ullOperand, LONG *plResult);
NTSTATUS RtlULongLongToShort(ULONGLONG ullOperand, SHORT *psResult);
NTSTATUS RtlULongLongToUChar(ULONGLONG ullOperand, UCHAR *pch);
NTSTATUS RtlULongLongToUInt8(ULONGLONG ullOperand, UINT8 *pu8Result);
NTSTATUS RtlULongLongToUShort(ULONGLONG ullOperand, USHORT *pusResult);
NTSTATUS RtlULongPtrToChar(ULONG_PTR ulOperand, CHAR *pch);
NTSTATUS RtlULongPtrToInt(ULONG_PTR ulOperand, INT *piResult);
NTSTATUS RtlULongPtrToInt8(ULONG_PTR ulOperand, INT8 *pi8Result);
NTSTATUS RtlULongPtrToIntPtr(ULONG_PTR ulOperand, INT_PTR *piResult);
NTSTATUS RtlULongPtrToLong(ULONG_PTR ulOperand, LONG *plResult);
NTSTATUS RtlULongPtrToLongPtr(ULONG_PTR ulOperand, LONG_PTR *plResult);
NTSTATUS RtlULongPtrToShort(ULONG_PTR ulOperand, SHORT *psResult);
NTSTATUS RtlULongPtrToUChar(ULONG_PTR ulOperand, UCHAR *pch);
NTSTATUS RtlULongPtrToUInt8(ULONG_PTR ulOperand, UINT8 *pui8Result);
NTSTATUS RtlULongPtrToUIntPtr(ULONG_PTR ulOperand, UINT_PTR *puResult);
NTSTATUS RtlULongPtrToUShort(ULONG_PTR ulOperand, USHORT *pusResult);
NTSTATUS RtlULongToChar(ULONG ulOperand, CHAR *pch);
NTSTATUS RtlULongToInt8(ULONG ulOperand, INT8 *pi8Result);
NTSTATUS RtlULongToLong(ULONG ulOperand, LONG *plResult);

#ifdef __cplusplus
}
#endif

#define __RtlpUnsignedMultiply128 _umul128

#endif // !_NTINTSAFE_H_INCLUDED_

#ifndef _ARM64_MULT_INTRINS_SUPPORTED
#define _ARM64_MULT_INTRINS_SUPPORTED 0
#endif
