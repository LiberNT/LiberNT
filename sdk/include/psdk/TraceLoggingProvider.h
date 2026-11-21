/*
 * PROJECT:     LiberNT SDK
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     TraceLogging provider interface
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

#include <winmeta.h>

/* FIXME: TraceLogging events are not emitted; providers never register and are always disabled */

typedef const struct _TlgProvider_t *TraceLoggingHProvider;

#define TRACELOGGING_DECLARE_PROVIDER(hProvider) \
    extern const TraceLoggingHProvider hProvider

#define TRACELOGGING_DEFINE_PROVIDER(hProvider, providerName, providerId, ...) \
    const TraceLoggingHProvider hProvider = (TraceLoggingHProvider)0

#define TraceLoggingRegister(hProvider) ((HRESULT)0)
#define TraceLoggingRegisterEx(hProvider, pEnableCallback, pCallbackContext) ((HRESULT)0)
#define TraceLoggingUnregister(hProvider) ((void)0)
#define TraceLoggingProviderEnabled(hProvider, eventLevel, eventKeyword) (0)
#define TraceLoggingWrite(hProvider, eventName, ...) ((void)0)
#define TraceLoggingWriteActivity(hProvider, eventName, pActivityId, pRelatedActivityId, ...) ((void)0)

#define _tlg_ASSERTexp, str) ((void) (!(exp) ? (__annotation(L"Debug", L"AssertFail", L"TraceLogging: " _tlg_LSTRINGIZE(exp) L" : " str), TLG_RAISEASSERTIONFAILURE(), 0) : 0))
#define _TLG_ASSERTexp, str) ((void) (!(exp) ? (__annotation(L"Debug", L"AssertFail", L"TraceLogging: " L ## # exp L" : " L ## str), DbgRaiseAssertionFailure(), 0) : 0))
#define _tlg_AssertValidPackedMetadataInTypeinType) _tlg_CASSERT(((inType) & _TlgInTypeMask) != _TlgInPOINTER_unsupported && ((inType) & _TlgInTypeMask) != _TlgInSTRUCT && ((inType) | _TlgInTypeMask | TlgInVcount) == (_TlgInTypeMask | TlgInVcount) && (inType) != (TlgInBINARY | TlgInVcount), "Invalid packed metadata in_type: " # inType)
#define _TLG_AssertValidPackedMetadataInTypein_type) _TLG_CASSERT(((in_type) & _TlgInTypeMask) != _TlgInPOINTER_unsupported && ((in_type) & _TlgInTypeMask) != _TlgInSTRUCT && ((in_type) | _TlgInTypeMask | TlgInVcount) == (_TlgInTypeMask | TlgInVcount) && (in_type) != (TlgInBINARY | TlgInVcount), "Invalid packed metadata in_type: " # in_type)
#define _tlg_AssertValidPackedMetadataInTypeCppOnlyinType)
#define _TLG_AssertValidPackedMetadataInTypeCppOnlyin_type)
#define _tlg_CALL __stdcall
#define _tlg_CASSERTexp, str) _Static_assert(exp, str)
#define _TLG_CASSERTexp, str) C_ASSERT(exp)
#define _tlg_CATa, ...) _tlg_CAT_imp(a, __VA_ARGS__)
#define _TLG_CATa, ...) _TLG_CAT_imp(a, __VA_ARGS__)
#define _tlg_CAT_impa, ...) a ## __VA_ARGS__
#define _TLG_CAT_impa, ...) a ## __VA_ARGS__
#define _TLG_DEBUG 1
#define _tlg_ENABLE_TraceLoggingWriteEx 1
#define _TLG_ENABLE_TraceLoggingWriteEx WINVER >= 0x0601
#define _tlg_EVENT_METADATA_PREAMBLE 0xB
#define _TLG_EVENT_METADATA_PREAMBLE 0xB
#define _tlg_EVENT_SET_INFORMATION_suffix TLG_EVENT_SET_INFORMATION
#define _tlg_EXTERN_C extern
#define _tlg_EXTERN_C_CONST const
#define _tlg_FASTFAILcode) __fastfail(code)
#define _tlg_FLATTEN...) __VA_ARGS__
#define _TLG_FLATTEN...) __VA_ARGS__
#define _tlg_FOR_impn, fnAndArgs) _tlg_PASTE2(_tlg_FOR_imp, n) fnAndArgs
#define _TLG_FOR_impn, fnAndArgs) _TLG_PASTE(_TLG_FOR_imp, n) fnAndArgs
#define _tlg_FOR_imp0f, ...)
#define _TLG_FOR_imp0fn)
#define _tlg_FOR_imp1f, a0) f(0, a0)
#define _TLG_FOR_imp1fn, arg, ...) _TLG_FOR_impN(0, fn, arg, fn, __VA_ARGS__)
#define _tlg_FOR_imp10f, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9) f(0, a0) f(1, a1) f(2, a2) f(3, a3) f(4, a4) f(5, a5) f(6, a6) f(7, a7) f(8, a8) f(9, a9)
#define _TLG_FOR_imp10fn, arg, ...) _TLG_FOR_impN(9, fn, arg, fn, __VA_ARGS__)
#define _tlg_FOR_imp11f, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10) f(0, a0) f(1, a1) f(2, a2) f(3, a3) f(4, a4) f(5, a5) f(6, a6) f(7, a7) f(8, a8) f(9, a9) f(10, a10)
#define _TLG_FOR_imp11fn, arg, ...) _TLG_FOR_impN(10, fn, arg, fn, __VA_ARGS__)
#define _tlg_FOR_imp12f, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11) f(0, a0) f(1, a1) f(2, a2) f(3, a3) f(4, a4) f(5, a5) f(6, a6) f(7, a7) f(8, a8) f(9, a9) f(10, a10) f(11, a11)
#define _TLG_FOR_imp12fn, arg, ...) _TLG_FOR_impN(11, fn, arg, fn, __VA_ARGS__)
#define _tlg_FOR_imp13f, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12) f(0, a0) f(1, a1) f(2, a2) f(3, a3) f(4, a4) f(5, a5) f(6, a6) f(7, a7) f(8, a8) f(9, a9) f(10, a10) f(11, a11) f(12, a12)
#define _TLG_FOR_imp13fn, arg, ...) _TLG_FOR_impN(12, fn, arg, fn, __VA_ARGS__)
#define _tlg_FOR_imp14f, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13) f(0, a0) f(1, a1) f(2, a2) f(3, a3) f(4, a4) f(5, a5) f(6, a6) f(7, a7) f(8, a8) f(9, a9) f(10, a10) f(11, a11) f(12, a12) f(13, a13)
#define _TLG_FOR_imp14fn, arg, ...) _TLG_FOR_impN(13, fn, arg, fn, __VA_ARGS__)
#define _tlg_FOR_imp15f, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14) f(0, a0) f(1, a1) f(2, a2) f(3, a3) f(4, a4) f(5, a5) f(6, a6) f(7, a7) f(8, a8) f(9, a9) f(10, a10) f(11, a11) f(12, a12) f(13, a13) f(14, a14)
#define _TLG_FOR_imp15fn, arg, ...) _TLG_FOR_impN(14, fn, arg, fn, __VA_ARGS__)
#define _tlg_FOR_imp16f, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15) f(0, a0) f(1, a1) f(2, a2) f(3, a3) f(4, a4) f(5, a5) f(6, a6) f(7, a7) f(8, a8) f(9, a9) f(10, a10) f(11, a11) f(12, a12) f(13, a13) f(14, a14) f(15, a15)
#define _TLG_FOR_imp16fn, arg, ...) _TLG_FOR_impN(15, fn, arg, fn, __VA_ARGS__)
#define _tlg_FOR_imp17f, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16) f(0, a0) f(1, a1) f(2, a2) f(3, a3) f(4, a4) f(5, a5) f(6, a6) f(7, a7) f(8, a8) f(9, a9) f(10, a10) f(11, a11) f(12, a12) f(13, a13) f(14, a14) f(15, a15) f(16, a16)
#define _TLG_FOR_imp17fn, arg, ...) _TLG_FOR_impN(16, fn, arg, fn, __VA_ARGS__)
#define _tlg_FOR_imp18f, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17) f(0, a0) f(1, a1) f(2, a2) f(3, a3) f(4, a4) f(5, a5) f(6, a6) f(7, a7) f(8, a8) f(9, a9) f(10, a10) f(11, a11) f(12, a12) f(13, a13) f(14, a14) f(15, a15) f(16, a16) f(17, a17)
#define _TLG_FOR_imp18fn, arg, ...) _TLG_FOR_impN(17, fn, arg, fn, __VA_ARGS__)
#define _tlg_FOR_imp19f, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17, a18) f(0, a0) f(1, a1) f(2, a2) f(3, a3) f(4, a4) f(5, a5) f(6, a6) f(7, a7) f(8, a8) f(9, a9) f(10, a10) f(11, a11) f(12, a12) f(13, a13) f(14, a14) f(15, a15) f(16, a16) f(17, a17) f(18, a18)
#define _TLG_FOR_imp19fn, arg, ...) _TLG_FOR_impN(18, fn, arg, fn, __VA_ARGS__)
#define _tlg_FOR_imp2f, a0, a1) f(0, a0) f(1, a1)
#define _TLG_FOR_imp2fn, arg, ...) _TLG_FOR_impN(1, fn, arg, fn, __VA_ARGS__)
#define _tlg_FOR_imp20f, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17, a18, a19) f(0, a0) f(1, a1) f(2, a2) f(3, a3) f(4, a4) f(5, a5) f(6, a6) f(7, a7) f(8, a8) f(9, a9) f(10, a10) f(11, a11) f(12, a12) f(13, a13) f(14, a14) f(15, a15) f(16, a16) f(17, a17) f(18, a18) f(19, a19)
#define _TLG_FOR_imp20fn, arg, ...) _TLG_FOR_impN(19, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp21fn, arg, ...) _TLG_FOR_impN(20, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp22fn, arg, ...) _TLG_FOR_impN(21, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp23fn, arg, ...) _TLG_FOR_impN(22, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp24fn, arg, ...) _TLG_FOR_impN(23, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp25fn, arg, ...) _TLG_FOR_impN(24, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp26fn, arg, ...) _TLG_FOR_impN(25, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp27fn, arg, ...) _TLG_FOR_impN(26, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp28fn, arg, ...) _TLG_FOR_impN(27, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp29fn, arg, ...) _TLG_FOR_impN(28, fn, arg, fn, __VA_ARGS__)
#define _tlg_FOR_imp3f, a0, a1, a2) f(0, a0) f(1, a1) f(2, a2)
#define _TLG_FOR_imp3fn, arg, ...) _TLG_FOR_impN(2, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp30fn, arg, ...) _TLG_FOR_impN(29, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp31fn, arg, ...) _TLG_FOR_impN(30, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp32fn, arg, ...) _TLG_FOR_impN(31, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp33fn, arg, ...) _TLG_FOR_impN(32, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp34fn, arg, ...) _TLG_FOR_impN(33, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp35fn, arg, ...) _TLG_FOR_impN(34, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp36fn, arg, ...) _TLG_FOR_impN(35, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp37fn, arg, ...) _TLG_FOR_impN(36, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp38fn, arg, ...) _TLG_FOR_impN(37, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp39fn, arg, ...) _TLG_FOR_impN(38, fn, arg, fn, __VA_ARGS__)
#define _tlg_FOR_imp4f, a0, a1, a2, a3) f(0, a0) f(1, a1) f(2, a2) f(3, a3)
#define _TLG_FOR_imp4fn, arg, ...) _TLG_FOR_impN(3, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp40fn, arg, ...) _TLG_FOR_impN(39, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp41fn, arg, ...) _TLG_FOR_impN(40, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp42fn, arg, ...) _TLG_FOR_impN(41, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp43fn, arg, ...) _TLG_FOR_impN(42, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp44fn, arg, ...) _TLG_FOR_impN(43, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp45fn, arg, ...) _TLG_FOR_impN(44, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp46fn, arg, ...) _TLG_FOR_impN(45, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp47fn, arg, ...) _TLG_FOR_impN(46, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp48fn, arg, ...) _TLG_FOR_impN(47, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp49fn, arg, ...) _TLG_FOR_impN(48, fn, arg, fn, __VA_ARGS__)
#define _tlg_FOR_imp5f, a0, a1, a2, a3, a4) f(0, a0) f(1, a1) f(2, a2) f(3, a3) f(4, a4)
#define _TLG_FOR_imp5fn, arg, ...) _TLG_FOR_impN(4, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp50fn, arg, ...) _TLG_FOR_impN(49, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp51fn, arg, ...) _TLG_FOR_impN(50, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp52fn, arg, ...) _TLG_FOR_impN(51, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp53fn, arg, ...) _TLG_FOR_impN(52, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp54fn, arg, ...) _TLG_FOR_impN(53, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp55fn, arg, ...) _TLG_FOR_impN(54, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp56fn, arg, ...) _TLG_FOR_impN(55, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp57fn, arg, ...) _TLG_FOR_impN(56, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp58fn, arg, ...) _TLG_FOR_impN(57, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp59fn, arg, ...) _TLG_FOR_impN(58, fn, arg, fn, __VA_ARGS__)
#define _tlg_FOR_imp6f, a0, a1, a2, a3, a4, a5) f(0, a0) f(1, a1) f(2, a2) f(3, a3) f(4, a4) f(5, a5)
#define _TLG_FOR_imp6fn, arg, ...) _TLG_FOR_impN(5, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp60fn, arg, ...) _TLG_FOR_impN(59, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp61fn, arg, ...) _TLG_FOR_impN(60, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp62fn, arg, ...) _TLG_FOR_impN(61, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp63fn, arg, ...) _TLG_FOR_impN(62, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp64fn, arg, ...) _TLG_FOR_impN(63, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp65fn, arg, ...) _TLG_FOR_impN(64, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp66fn, arg, ...) _TLG_FOR_impN(65, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp67fn, arg, ...) _TLG_FOR_impN(66, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp68fn, arg, ...) _TLG_FOR_impN(67, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp69fn, arg, ...) _TLG_FOR_impN(68, fn, arg, fn, __VA_ARGS__)
#define _tlg_FOR_imp7f, a0, a1, a2, a3, a4, a5, a6) f(0, a0) f(1, a1) f(2, a2) f(3, a3) f(4, a4) f(5, a5) f(6, a6)
#define _TLG_FOR_imp7fn, arg, ...) _TLG_FOR_impN(6, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp70fn, arg, ...) _TLG_FOR_impN(69, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp71fn, arg, ...) _TLG_FOR_impN(70, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp72fn, arg, ...) _TLG_FOR_impN(71, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp73fn, arg, ...) _TLG_FOR_impN(72, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp74fn, arg, ...) _TLG_FOR_impN(73, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp75fn, arg, ...) _TLG_FOR_impN(74, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp76fn, arg, ...) _TLG_FOR_impN(75, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp77fn, arg, ...) _TLG_FOR_impN(76, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp78fn, arg, ...) _TLG_FOR_impN(77, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp79fn, arg, ...) _TLG_FOR_impN(78, fn, arg, fn, __VA_ARGS__)
#define _tlg_FOR_imp8f, a0, a1, a2, a3, a4, a5, a6, a7) f(0, a0) f(1, a1) f(2, a2) f(3, a3) f(4, a4) f(5, a5) f(6, a6) f(7, a7)
#define _TLG_FOR_imp8fn, arg, ...) _TLG_FOR_impN(7, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp80fn, arg, ...) _TLG_FOR_impN(79, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp81fn, arg, ...) _TLG_FOR_impN(80, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp82fn, arg, ...) _TLG_FOR_impN(81, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp83fn, arg, ...) _TLG_FOR_impN(82, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp84fn, arg, ...) _TLG_FOR_impN(83, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp85fn, arg, ...) _TLG_FOR_impN(84, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp86fn, arg, ...) _TLG_FOR_impN(85, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp87fn, arg, ...) _TLG_FOR_impN(86, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp88fn, arg, ...) _TLG_FOR_impN(87, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp89fn, arg, ...) _TLG_FOR_impN(88, fn, arg, fn, __VA_ARGS__)
#define _tlg_FOR_imp9f, a0, a1, a2, a3, a4, a5, a6, a7, a8) f(0, a0) f(1, a1) f(2, a2) f(3, a3) f(4, a4) f(5, a5) f(6, a6) f(7, a7) f(8, a8)
#define _TLG_FOR_imp9fn, arg, ...) _TLG_FOR_impN(8, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp90fn, arg, ...) _TLG_FOR_impN(89, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp91fn, arg, ...) _TLG_FOR_impN(90, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp92fn, arg, ...) _TLG_FOR_impN(91, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp93fn, arg, ...) _TLG_FOR_impN(92, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp94fn, arg, ...) _TLG_FOR_impN(93, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp95fn, arg, ...) _TLG_FOR_impN(94, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp96fn, arg, ...) _TLG_FOR_impN(95, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp97fn, arg, ...) _TLG_FOR_impN(96, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp98fn, arg, ...) _TLG_FOR_impN(97, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp99fn, arg, ...) _TLG_FOR_impN(98, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_impNn, fn, arg, ...) fn(n, arg) _TLG_PASTE(_TLG_FOR_imp, n) _TLG_FLATTEN((__VA_ARGS__))
#define _tlg_IS_PARENTHESIZED_imp0...) 1
#define _TLG_IS_PARENTHESIZED_imp0...) 1
#define _tlg_IS_PARENTHESIZED_imp1 1,
#define _TLG_IS_PARENTHESIZED_imp1 0
#define _tlg_IS_PARENTHESIZED_imp_tlg_IS_PARENTHESIZED_imp0 0,
#define _TLG_IS_PARENTHESIZED_imp_TLG_IS_PARENTHESIZED_imp0 0,
#define _tlg_LSTRINGIZEx) _tlg_LSTRINGIZE_imp(x)
#define _tlg_LSTRINGIZE_impx) _tlg_PASTE2(L, # x)
#define _tlg_MetaDataDescCount 2
#ifdef _WIN64
#define _tlg_MetadataFlags 1
#else
#define _tlg_MetadataFlags 0
#endif
#define _tlg_MetadataMagic (-4933124757307257210LL)
#define _tlg_MetadataSignature 0x30575445
#define _tlg_MetadataVersion 0
#define _tlg_NARGS_impis_empty, args) _tlg_PASTE2(_tlg_NARGS_imp, is_empty) args
#define _TLG_NARGS_impis_empty, args) _TLG_PASTE(_TLG_NARGS_imp, is_empty) args
#define _tlg_NARGS_imp1) 0
#define _TLG_NARGS_imp1) 0
#define _tlg_NARGS_imp2a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17, a18, a19, a20, a21, a22, a23, a24, a25, a26, a27, a28, a29, a30, a31, a32, a33, a34, a35, a36, a37, a38, a39, a40, a41, a42, a43, a44, a45, a46, a47, a48, a49, a50, a51, a52, a53, a54, a55, a56, a57, a58, a59, a60, a61, a62, a63, a64, a65, a66, a67, a68, a69, a70, a71, a72, a73, a74, a75, a76, a77, a78, a79, a80, a81, a82, a83, a84, a85, a86, a87, a88, a89, a90, a91, a92, a93, a94, a95, a96, a97, a98, a99, size, ...) size
#define _TLG_NARGS_imp2a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17, a18, a19, a20, a21, a22, a23, a24, a25, a26, a27, a28, a29, a30, a31, a32, a33, a34, a35, a36, a37, a38, a39, a40, a41, a42, a43, a44, a45, a46, a47, a48, a49, a50, a51, a52, a53, a54, a55, a56, a57, a58, a59, a60, a61, a62, a63, a64, a65, a66, a67, a68, a69, a70, a71, a72, a73, a74, a75, a76, a77, a78, a79, a80, a81, a82, a83, a84, a85, a86, a87, a88, a89, a90, a91, a92, a93, a94, a95, a96, a97, a98, a99, size, ...) size
#define _tlg_PARENTHESIZE...) (__VA_ARGS__)
#define _TLG_PARENTHESIZE...) (__VA_ARGS__)
#define _TLG_PASTEa, b) _TLG_PASTE_imp(a, b)
#define _tlg_PASTE2a, b) _tlg_PASTE2_imp(a, b)
#define _tlg_PASTE2_impa, b) a ## b
#define _tlg_PASTE3a, b, c) _tlg_PASTE3_imp(a, b, c)
#define _tlg_PASTE3_impa, b, c) a ## b ## _ ## c
#define _TLG_PASTE_impa, b) a ## b
#define _tlg_PROVIDER_METADATA_PREAMBLE 16
#define _TLG_PROVIDER_METADATA_PREAMBLE 16
#define _tlg_SPLITcond, ...) _tlg_SPLIT_imp(cond, (__VA_ARGS__))
#define _TLG_SPLITcond, ...) _TLG_SPLIT_imp(cond, (__VA_ARGS__))
#define _tlg_SPLIT_impcond, args) _tlg_PASTE2(_tlg_SPLIT_imp, cond) args
#define _TLG_SPLIT_impcond, args) _TLG_PASTE(_TLG_SPLIT_imp, cond) args
#define _tlg_SPLIT_imp0false_val, ...) false_val
#define _TLG_SPLIT_imp0false_val, ...) false_val
#define _tlg_SPLIT_imp1false_val, ...) __VA_ARGS__
#define _TLG_SPLIT_imp1false_val, ...) __VA_ARGS__
#define _tlg_STRINGIZEx) _tlg_STRINGIZE_imp(x)
#define _TLG_STRINGIZEx) _TLG_STRINGIZE_imp(x)
#define _tlg_STRINGIZE_impx) # x
#define _TLG_STRINGIZE_impx) # x
#define _tlgAnnotationFunc_impuse_annotationFunc, storageVariable) _tlg_PASTE2(_tlgAnnotationFunc_imp, use_annotationFunc) (storageVariable)
#define _TlgAnnotationFunc_impuse_annotationFunc, storageVariable) _TLG_PASTE(_TlgAnnotationFunc_imp, use_annotationFunc) (storageVariable)
#define _tlgAnnotationFunc_imp0storageVariable)
#define _TlgAnnotationFunc_imp0storageVariable) 0
#define _tlgAnnotesn, args) _tlgApplyArgs(_tlgAnnotes, args)
#define _TlgAnnotes_TlgAname, desc) L"|" L ## # name L"=" desc
#define _tlgAnnotes_tlgArrayctype, pValues, cValues, inType, outType, hasOut, name, desc, tags, hasTags) L"|" _tlg_LSTRINGIZE(name) L"=" desc
#define _tlgAnnotes_tlgAutovalue, name, desc, tags, hasTags) L"|" _tlg_LSTRINGIZE(name) L"=" desc
#define _TlgAnnotes_TlgAutoname, desc) L"|" L ## # name L"=" desc
#define _TlgAnnotes_TlgBname, desc) L"|" L ## # name L"=" desc
#define _tlgAnnotes_tlgBinaryctype, pValue, cbValue, inType, outType, hasOut, name, desc, tags, hasTags) L"|" _tlg_LSTRINGIZE(name) L"=" desc
#define _tlgAnnotes_tlgBufferctype, pValue, inType, outType, hasOut, name, desc, tags, hasTags) L"|" _tlg_LSTRINGIZE(name) L"=" desc
#define _tlgAnnotes_tlgChanneleventChannel)
#define _TlgAnnotes_TlgChannelname, desc)
#define _tlgAnnotes_tlgCountedctype, pchValue, cchValue, inType, outType, hasOut, name, desc, tags, hasTags) L"|" _tlg_LSTRINGIZE(name) L"=" desc
#define _TlgAnnotes_TlgCSname, desc) L"|" L ## # name L"=" desc
#define _TlgAnnotes_TlgCustAnnname, desc) L"|" L ## name L"=" desc
#define _tlgAnnotes_tlgCustomctype, pValue, cbValue, protocol, bSchema, cbSchema, name, desc, tags, hasTags) L"|" _tlg_LSTRINGIZE(name) L"=" desc
#define _TlgAnnotes_TlgCustomname, desc) L"|" L ## # name L"=" desc
#define _tlgAnnotes_tlgCustomAnnotkey, value) L"|" key L"=" value
#define _tlgAnnotes_tlgDesceventDescription)
#define _TlgAnnotes_TlgDescname, desc)
#define _TlgAnnotes_TlgEvtTagname, desc)
#define _TlgAnnotes_TlgFAname, desc) L"|" L ## # name L"=" desc
#define _tlgAnnotes_tlgFixedArrayctype, pValues, cValues, inType, outType, hasOut, name, desc, tags, hasTags) L"|" _tlg_LSTRINGIZE(name) L"=" desc
#define _tlgAnnotes_tlgKeywordeventKeyword)
#define _TlgAnnotes_TlgKeywordname, desc)
#define _tlgAnnotes_tlgLeveleventLevel)
#define _TlgAnnotes_TlgLevelname, desc)
#define _tlgAnnotes_tlgOpcodeeventOpcode)
#define _TlgAnnotes_TlgOpcodename, desc)
#define _TlgAnnotes_TlgPname, desc) L"|" L ## # name L"=" desc
#define _tlgAnnotes_tlgPackedDatactype, pValue, cbValue, dataDescType, hasType)
#define _tlgAnnotes_tlgPackedFieldctype, pValue, cbValue, inType, outType, hasOut, name, desc, tags, hasTags) L"|" _tlg_LSTRINGIZE(name) L"=" desc
#define _tlgAnnotes_tlgPackedMetainType, outType, hasOut, name, desc, tags, hasTags) L"|" _tlg_LSTRINGIZE(name) L"=" desc
#define _TlgAnnotes_TlgPDname, desc)
#define _TlgAnnotes_TlgPFname, desc) L"|" L ## # name L"=" desc
#define _TlgAnnotes_TlgPMname, desc) L"|" L ## # name L"=" desc
#define _tlgAnnotes_tlgPszctype, pszValue, inType, outType, hasOut, name, desc, tags, hasTags) L"|" _tlg_LSTRINGIZE(name) L"=" desc
#define _TlgAnnotes_TlgSname, desc) L"|" L ## # name L"=" desc
#define _tlgAnnotes_tlgScalarRefctype, value, inType, outType, hasOut, name, desc, tags, hasTags) L"|" _tlg_LSTRINGIZE(name) L"=" desc
#define _tlgAnnotes_tlgScalarValctype, value, inType, outType, hasOut, name, desc, tags, hasTags) L"|" _tlg_LSTRINGIZE(name) L"=" desc
#define _tlgAnnotes_tlgSidctype, pValue, name, desc, tags, hasTags) L"|" _tlg_LSTRINGIZE(name) L"=" desc
#define _TlgAnnotes_TlgSidname, desc) L"|" L ## # name L"=" desc
#define _TlgAnnotes_TlgStringname, desc) L"|" L ## # name L"=" desc
#define _tlgAnnotes_tlgStructfieldCount, inType, name, desc, tags, hasTags) L"|" _tlg_LSTRINGIZE(name) L"=" desc
#define _TlgAnnotes_TlgStructname, desc) L"|" L ## # name L"=" desc
#define _TlgAnnotes_TlgSzname, desc) L"|" L ## # name L"=" desc
#define _tlgAnnotes_tlgTageventTag)
#define _TlgAnnotes_TlgWszname, desc) L"|" L ## # name L"=" desc
#define _tlgApplyArgsmacro, args) _tlgApplyArgs_impA((macro, _tlgApplyArgs_UNWRAP args))
#define _tlgApplyArgs_CALLmacro, handler, args) macro ## handler args
#define _tlgApplyArgs_impAargs) _tlgApplyArgs_impB args
#define _tlgApplyArgs_impBmacro, handler, ...) _tlgApplyArgs_CALL(macro, handler, (__VA_ARGS__))
#define _tlgApplyArgs_UNWRAP...) __VA_ARGS__
#define _tlgApplyArgsNmacro, n, args) _tlgApplyArgsN_impA((macro, n, _tlgApplyArgs_UNWRAP args))
#define _tlgApplyArgsN_impAargs) _tlgApplyArgsN_impB args
#define _tlgApplyArgsN_impBmacro, n, handler, ...) _tlgApplyArgs_CALL(macro, handler, (n, __VA_ARGS__))
#define _TlgArg_impn, data_type, in_type, out_type, has_out, dim, value, cVals, ...) _TLG_PASTE(_TlgArg_imp, n) _TLG_FLATTEN((data_type, in_type, out_type, has_out, dim, value, cVals, __VA_ARGS__))
#define _TlgArg_imp0data_type, in_type, out_type, has_out, dim, value, cVals, x) (data_type, in_type, out_type, has_out, dim, value, cVals, # value,,, 0)
#define _TlgArg_imp1data_type, in_type, out_type, has_out, dim, value, cVals, name) (data_type, in_type, out_type, has_out, dim, value, cVals, name,,, 0)
#define _TlgArg_imp2data_type, in_type, out_type, has_out, dim, value, cVals, name, desc) (data_type, in_type, out_type, has_out, dim, value, cVals, name, L ## desc,, 0)
#define _TlgArg_imp3data_type, in_type, out_type, has_out, dim, value, cVals, name, desc, tags) (data_type, in_type, out_type, has_out, dim, value, cVals, name, L ## desc, tags, 1)
#define _tlgChannelValn, args) _tlgApplyArgs(_tlgChannelVal, args)
#define _TlgChannelVal_TlgAvalue)
#define _tlgChannelVal_tlgArrayctype, pValues, cValues, inType, outType, hasOut, name, desc, tags, hasTags)
#define _tlgChannelVal_tlgAutovalue, name, desc, tags, hasTags)
#define _TlgChannelVal_TlgAutovalue)
#define _TlgChannelVal_TlgBvalue)
#define _tlgChannelVal_tlgBinaryctype, pValue, cbValue, inType, outType, hasOut, name, desc, tags, hasTags)
#define _tlgChannelVal_tlgBufferctype, pValue, inType, outType, hasOut, name, desc, tags, hasTags)
#define _tlgChannelVal_tlgChanneleventChannel) & 0 | (eventChannel)
#define _TlgChannelVal_TlgChannelvalue) & 0 | _TlgConst(Channel, value)
#define _tlgChannelVal_tlgCountedctype, pchValue, cchValue, inType, outType, hasOut, name, desc, tags, hasTags)
#define _TlgChannelVal_TlgCSvalue)
#define _TlgChannelVal_TlgCustAnnvalue)
#define _tlgChannelVal_tlgCustomctype, pValue, cbValue, protocol, bSchema, cbSchema, name, desc, tags, hasTags)
#define _TlgChannelVal_TlgCustomvalue)
#define _tlgChannelVal_tlgCustomAnnotkey, value)
#define _tlgChannelVal_tlgDesceventDescription)
#define _TlgChannelVal_TlgDescvalue)
#define _TlgChannelVal_TlgEvtTagvalue)
#define _TlgChannelVal_TlgFAvalue)
#define _tlgChannelVal_tlgFixedArrayctype, pValues, cValues, inType, outType, hasOut, name, desc, tags, hasTags)
#define _tlgChannelVal_tlgKeywordeventKeyword)
#define _TlgChannelVal_TlgKeywordvalue)
#define _tlgChannelVal_tlgLeveleventLevel)
#define _TlgChannelVal_TlgLevelvalue)
#define _tlgChannelVal_tlgOpcodeeventOpcode)
#define _TlgChannelVal_TlgOpcodevalue)
#define _TlgChannelVal_TlgPvalue)
#define _tlgChannelVal_tlgPackedDatactype, pValue, cbValue, dataDescType, hasType)
#define _tlgChannelVal_tlgPackedFieldctype, pValue, cbValue, inType, outType, hasOut, name, desc, tags, hasTags)
#define _tlgChannelVal_tlgPackedMetainType, outType, hasOut, name, desc, tags, hasTags)
#define _TlgChannelVal_TlgPDvalue)
#define _TlgChannelVal_TlgPFvalue)
#define _TlgChannelVal_TlgPMvalue)
#define _tlgChannelVal_tlgPszctype, pszValue, inType, outType, hasOut, name, desc, tags, hasTags)
#define _TlgChannelVal_TlgSvalue)
#define _tlgChannelVal_tlgScalarRefctype, value, inType, outType, hasOut, name, desc, tags, hasTags)
#define _tlgChannelVal_tlgScalarValctype, value, inType, outType, hasOut, name, desc, tags, hasTags)
#define _tlgChannelVal_tlgSidctype, pValue, name, desc, tags, hasTags)
#define _TlgChannelVal_TlgSidvalue)
#define _TlgChannelVal_TlgStringvalue)
#define _tlgChannelVal_tlgStructfieldCount, inType, name, desc, tags, hasTags)
#define _TlgChannelVal_TlgStruct...)
#define _TlgChannelVal_TlgSzvalue)
#define _tlgChannelVal_tlgTageventTag)
#define _TlgChannelVal_TlgWszvalue)
#define _tlgCheckStructFieldCountCppOnlyfieldCount) 0
#define _TlgConstname, n) (n)
#define _tlgCreate1Sz_char16_tpDesc, psz) _tlgCreate1Sz_wchar_t(pDesc, psz)
#define _tlgDataDescCountn, args) _tlgApplyArgs(_tlgDataDescCount, args)
#define _TlgDataDescCount_TlgA) + 2
#define _tlgDataDescCount_tlgArrayctype, pValues, cValues, inType, outType, hasOut, name, desc, tags, hasTags) + 2
#define _tlgDataDescCount_tlgAutovalue, name, desc, tags, hasTags) + 1
#define _TlgDataDescCount_TlgAuto) + 1
#define _TlgDataDescCount_TlgB) + 2
#define _tlgDataDescCount_tlgBinaryctype, pValue, cbValue, inType, outType, hasOut, name, desc, tags, hasTags) + 2
#define _tlgDataDescCount_tlgBufferctype, pValue, inType, outType, hasOut, name, desc, tags, hasTags) + 2
#define _tlgDataDescCount_tlgChanneleventChannel)
#define _TlgDataDescCount_TlgChannel)
#define _tlgDataDescCount_tlgCountedctype, pchValue, cchValue, inType, outType, hasOut, name, desc, tags, hasTags) + 2
#define _TlgDataDescCount_TlgCS) + 2
#define _TlgDataDescCount_TlgCustAnn)
#define _tlgDataDescCount_tlgCustomctype, pValue, cbValue, protocol, bSchema, cbSchema, name, desc, tags, hasTags) + 2
#define _TlgDataDescCount_TlgCustom) + 2
#define _tlgDataDescCount_tlgCustomAnnotkey, value)
#define _tlgDataDescCount_tlgDesceventDescription)
#define _TlgDataDescCount_TlgDesc)
#define _TlgDataDescCount_TlgEvtTag)
#define _TlgDataDescCount_TlgFA) + 1
#define _tlgDataDescCount_tlgFixedArrayctype, pValues, cValues, inType, outType, hasOut, name, desc, tags, hasTags) + 1
#define _tlgDataDescCount_tlgKeywordeventKeyword)
#define _TlgDataDescCount_TlgKeyword)
#define _tlgDataDescCount_tlgLeveleventLevel)
#define _TlgDataDescCount_TlgLevel)
#define _tlgDataDescCount_tlgOpcodeeventOpcode)
#define _TlgDataDescCount_TlgOpcode)
#define _TlgDataDescCount_TlgP) + 1
#define _tlgDataDescCount_tlgPackedDatactype, pValue, cbValue, dataDescType, hasType) + 1
#define _tlgDataDescCount_tlgPackedFieldctype, pValue, cbValue, inType, outType, hasOut, name, desc, tags, hasTags) + 1
#define _tlgDataDescCount_tlgPackedMetainType, outType, hasOut, name, desc, tags, hasTags)
#define _TlgDataDescCount_TlgPD) + 1
#define _TlgDataDescCount_TlgPF) + 1
#define _TlgDataDescCount_TlgPM)
#define _tlgDataDescCount_tlgPszctype, pszValue, inType, outType, hasOut, name, desc, tags, hasTags) + 1
#define _TlgDataDescCount_TlgS) + 1
#define _tlgDataDescCount_tlgScalarRefctype, value, inType, outType, hasOut, name, desc, tags, hasTags) + 1
#define _tlgDataDescCount_tlgScalarValctype, value, inType, outType, hasOut, name, desc, tags, hasTags) + 1
#define _tlgDataDescCount_tlgSidctype, pValue, name, desc, tags, hasTags) + 1
#define _TlgDataDescCount_TlgSid) + 1
#define _TlgDataDescCount_TlgString) + 2
#define _tlgDataDescCount_tlgStructfieldCount, inType, name, desc, tags, hasTags) + _tlgCheckStructFieldCountCppOnly(fieldCount)
#define _TlgDataDescCount_TlgStruct)
#define _TlgDataDescCount_TlgSz) + 1
#define _tlgDataDescCount_tlgTageventTag)
#define _TlgDataDescCount_TlgWsz) + 1
#define _tlgDataDescCreaten, args) _tlgApplyArgsN(_tlgDataDescCreate, n, args)
#define _tlgDataDescCreate_tlgChanneln, eventChannel)
#define _TlgDataDescCreate_TlgChanneln, data_type, cVals)
#define _TlgDataDescCreate_TlgCustAnnn, data_type, cVals)
#define _tlgDataDescCreate_tlgCustomAnnotn, key, value)
#define _tlgDataDescCreate_tlgDescn, eventDescription)
#define _TlgDataDescCreate_TlgDescn, data_type, cVals)
#define _TlgDataDescCreate_TlgEvtTagn, data_type, cVals)
#define _tlgDataDescCreate_tlgKeywordn, eventKeyword)
#define _TlgDataDescCreate_TlgKeywordn, data_type, cVals)
#define _tlgDataDescCreate_tlgLeveln, eventLevel)
#define _TlgDataDescCreate_TlgLeveln, data_type, cVals)
#define _tlgDataDescCreate_tlgOpcoden, eventOpcode)
#define _TlgDataDescCreate_TlgOpcoden, data_type, cVals)
#define _tlgDataDescCreate_tlgPackedMetan, inType, outType, hasOut, name, desc, tags, hasTags) _tlg_AssertValidPackedMetadataInType(inType);
#define _TlgDataDescCreate_TlgPMn, data_type, cVals)
#define _tlgDataDescCreate_tlgStructn, fieldCount, inType, name, desc, tags, hasTags) _tlg_CASSERT((fieldCount) < 128, "TraceLoggingStruct fieldCount must be less than 128.");
#define _TlgDataDescCreate_TlgStructn, data_type, cVals)
#define _tlgDataDescCreate_tlgTagn, eventTag)
#define _tlgDefineProvider_functionWrapperBegin0functionPostfix)
#define _TlgDefineProvider_functionWrapperBegin0functionPostfix)
#define _tlgDefineProvider_functionWrapperEnd0
#define _TlgDefineProvider_functionWrapperEnd0
#define _tlgDefineProvider_functionWrapperEnd1 }
#define _TlgDefineProvider_functionWrapperEnd1 }
#define _tlgDescValn, args) _tlgApplyArgs(_tlgDescVal, args)
#define _TlgDescVal_TlgAname, desc)
#define _tlgDescVal_tlgArrayctype, pValues, cValues, inType, outType, hasOut, name, desc, tags, hasTags)
#define _tlgDescVal_tlgAutovalue, name, desc, tags, hasTags)
#define _TlgDescVal_TlgAutoname, desc)
#define _TlgDescVal_TlgBname, desc)
#define _tlgDescVal_tlgBinaryctype, pValue, cbValue, inType, outType, hasOut, name, desc, tags, hasTags)
#define _tlgDescVal_tlgBufferctype, pValue, inType, outType, hasOut, name, desc, tags, hasTags)
#define _tlgDescVal_tlgChanneleventChannel)
#define _TlgDescVal_TlgChannelname, desc)
#define _tlgDescVal_tlgCountedctype, pchValue, cchValue, inType, outType, hasOut, name, desc, tags, hasTags)
#define _TlgDescVal_TlgCSname, desc)
#define _TlgDescVal_TlgCustAnnname, desc)
#define _tlgDescVal_tlgCustomctype, pValue, cbValue, protocol, bSchema, cbSchema, name, desc, tags, hasTags)
#define _TlgDescVal_TlgCustomname, desc)
#define _tlgDescVal_tlgCustomAnnotkey, value)
#define _tlgDescVal_tlgDesceventDescription) eventDescription
#define _TlgDescVal_TlgDescname, desc) desc
#define _TlgDescVal_TlgEvtTagname, desc)
#define _TlgDescVal_TlgFAname, desc)
#define _tlgDescVal_tlgFixedArrayctype, pValues, cValues, inType, outType, hasOut, name, desc, tags, hasTags)
#define _tlgDescVal_tlgKeywordeventKeyword)
#define _TlgDescVal_TlgKeywordname, desc)
#define _tlgDescVal_tlgLeveleventLevel)
#define _TlgDescVal_TlgLevelname, desc)
#define _tlgDescVal_tlgOpcodeeventOpcode)
#define _TlgDescVal_TlgOpcodename, desc)
#define _TlgDescVal_TlgPname, desc)
#define _tlgDescVal_tlgPackedDatactype, pValue, cbValue, dataDescType, hasType)
#define _tlgDescVal_tlgPackedFieldctype, pValue, cbValue, inType, outType, hasOut, name, desc, tags, hasTags)
#define _tlgDescVal_tlgPackedMetainType, outType, hasOut, name, desc, tags, hasTags)
#define _TlgDescVal_TlgPDname, desc)
#define _TlgDescVal_TlgPFname, desc)
#define _TlgDescVal_TlgPMname, desc)
#define _tlgDescVal_tlgPszctype, pszValue, inType, outType, hasOut, name, desc, tags, hasTags)
#define _TlgDescVal_TlgSname, desc)
#define _tlgDescVal_tlgScalarRefctype, value, inType, outType, hasOut, name, desc, tags, hasTags)
#define _tlgDescVal_tlgScalarValctype, value, inType, outType, hasOut, name, desc, tags, hasTags)
#define _tlgDescVal_tlgSidctype, pValue, name, desc, tags, hasTags)
#define _TlgDescVal_TlgSidname, desc)
#define _TlgDescVal_TlgStringname, desc)
#define _tlgDescVal_tlgStructfieldCount, inType, name, desc, tags, hasTags)
#define _TlgDescVal_TlgStructname, desc)
#define _TlgDescVal_TlgSzname, desc)
#define _tlgDescVal_tlgTageventTag)
#define _TlgDescVal_TlgWszname, desc)
#define _tlgDT_imp0name, ...) name,,, 0
#define _tlgDT_imp1name, desc) name, L"" desc,, 0
#define _tlgDT_imp2name, desc, tags) name, L"" desc, tags, 1
#define _tlgDT_impBmacro, args) macro args
#define _TlgEvtTagDecl...)
#define _TlgEvtTagEncodeeventTag) (((eventTag) & 0xfe00000) >> 21) /* Encode 0x0fe00000 bits into UINT16  */ | (((eventTag) & 0x1fc000) >> 6) /* Encode 0x001fc000 bits into UINT16  */ | (((eventTag) & 0x3fff) << 16) /* Trigger warning for 0x00003fff bits */ | (~(~(eventTag) | 0x0FFFFFFF))
#define _tlgEvtTagType UINT16
#define _TlgEvtTagType UINT16
#define _tlgEvtTagValn, args) _tlgApplyArgs(_tlgEvtTagVal, args)
#define _TlgEvtTagVal_TlgAvalue)
#define _tlgEvtTagVal_tlgArrayctype, pValues, cValues, inType, outType, hasOut, name, desc, tags, hasTags)
#define _tlgEvtTagVal_tlgAutovalue, name, desc, tags, hasTags)
#define _TlgEvtTagVal_TlgAutovalue)
#define _TlgEvtTagVal_TlgBvalue)
#define _tlgEvtTagVal_tlgBinaryctype, pValue, cbValue, inType, outType, hasOut, name, desc, tags, hasTags)
#define _tlgEvtTagVal_tlgBufferctype, pValue, inType, outType, hasOut, name, desc, tags, hasTags)
#define _tlgEvtTagVal_tlgChanneleventChannel)
#define _TlgEvtTagVal_TlgChannelvalue)
#define _tlgEvtTagVal_tlgCountedctype, pchValue, cchValue, inType, outType, hasOut, name, desc, tags, hasTags)
#define _TlgEvtTagVal_TlgCSvalue)
#define _TlgEvtTagVal_TlgCustAnnvalue)
#define _tlgEvtTagVal_tlgCustomctype, pValue, cbValue, protocol, bSchema, cbSchema, name, desc, tags, hasTags)
#define _TlgEvtTagVal_TlgCustomvalue)
#define _tlgEvtTagVal_tlgCustomAnnotkey, value)
#define _tlgEvtTagVal_tlgDesceventDescription)
#define _TlgEvtTagVal_TlgDescvalue)
#define _TlgEvtTagVal_TlgEvtTagvalue) | value
#define _TlgEvtTagVal_TlgFAvalue)
#define _tlgEvtTagVal_tlgFixedArrayctype, pValues, cValues, inType, outType, hasOut, name, desc, tags, hasTags)
#define _tlgEvtTagVal_tlgKeywordeventKeyword)
#define _TlgEvtTagVal_TlgKeywordvalue)
#define _tlgEvtTagVal_tlgLeveleventLevel)
#define _TlgEvtTagVal_TlgLevelvalue)
#define _tlgEvtTagVal_tlgOpcodeeventOpcode)
#define _TlgEvtTagVal_TlgOpcodevalue)
#define _TlgEvtTagVal_TlgPvalue)
#define _tlgEvtTagVal_tlgPackedDatactype, pValue, cbValue, dataDescType, hasType)
#define _tlgEvtTagVal_tlgPackedFieldctype, pValue, cbValue, inType, outType, hasOut, name, desc, tags, hasTags)
#define _tlgEvtTagVal_tlgPackedMetainType, outType, hasOut, name, desc, tags, hasTags)
#define _TlgEvtTagVal_TlgPDvalue)
#define _TlgEvtTagVal_TlgPFvalue)
#define _TlgEvtTagVal_TlgPMvalue)
#define _tlgEvtTagVal_tlgPszctype, pszValue, inType, outType, hasOut, name, desc, tags, hasTags)
#define _TlgEvtTagVal_TlgSvalue)
#define _tlgEvtTagVal_tlgScalarRefctype, value, inType, outType, hasOut, name, desc, tags, hasTags)
#define _tlgEvtTagVal_tlgScalarValctype, value, inType, outType, hasOut, name, desc, tags, hasTags)
#define _tlgEvtTagVal_tlgSidctype, pValue, name, desc, tags, hasTags)
#define _TlgEvtTagVal_TlgSidvalue)
#define _TlgEvtTagVal_TlgStringvalue)
#define _tlgEvtTagVal_tlgStructfieldCount, inType, name, desc, tags, hasTags)
#define _TlgEvtTagVal_TlgStruct)
#define _TlgEvtTagVal_TlgSzvalue)
#define _tlgEvtTagVal_tlgTageventTag) | (eventTag)
#define _TlgEvtTagVal_TlgWszvalue)
#define _tlgExpandProviderIda, b, c, d, e, f, g, h, i, j, k) { a, b, c, { d, e, f, g, h, i, j, k } }
#define _tlgExpandType_imp0) (0), 0
#define _tlgExpandType_imp1typeVal) (typeVal), 1
#define _tlgExpandType_impBmacro, args) macro args
#ifdef _WIN64
#define _TlgFlagsVal 1
#else
#define _TlgFlagsVal 0
#endif
#define _tlgGetLengthSidpSid) (8u + (pSid) -> SubAuthorityCount * 4u)
#define _tlgInfoValsn, args) _tlgApplyArgsN(_tlgInfoVals, n, args)
#define _tlgInfoVals_0...)
#define _TlgInfoVals_0...)
#define _tlgInfoVals_00outType, tags)
#define _TlgInfoVals_00out_type, tags)
#define _tlgInfoVals_01outType, tags) | 128, 128, _tlgInfoVals_Tags(tags)
#define _TlgInfoVals_01out_type, tags) | 128, 128, _TlgInfoVals_Tags(tags)
#define _tlgInfoVals_1tags) , _tlgInfoVals_Tags(tags)
#define _TlgInfoVals_1tags) , _TlgInfoVals_Tags(tags)
#define _tlgInfoVals_10outType, tags) | 128, outType
#define _TlgInfoVals_10out_type, tags) | 128, out_type
#define _tlgInfoVals_11outType, tags) | 128, outType | 128, _tlgInfoVals_Tags(tags)
#define _TlgInfoVals_11out_type, tags) | 128, out_type | 128, _TlgInfoVals_Tags(tags)
#define _tlgInfoVals_Tagstags) 0x808080 | (((tags) & 0x7f) << 24) | (((tags) & 0x3f80) << 9) | (((tags) & 0x1fc000) >> 6) | (((tags) & 0xfe00000) >> 21)
#define _TlgInfoVals_Tagstags) 0x808080 | (((tags) & 0x7f) << 24) | (((tags) & 0x3f80) << 9) | (((tags) & 0x1fc000) >> 6) | (((tags) & 0xfe00000) >> 21)
#define _tlgInfoVals_tlgChanneln, eventChannel)
#define _TlgInfoVals_TlgChannelin_type, out_type, has_out, cVals, name, has_tags, tags)
#define _TlgInfoVals_TlgCustAnnin_type, out_type, has_out, cVals, name, has_tags, tags)
#define _tlgInfoVals_tlgCustomn, ctype, pValue, cbValue, protocol, bSchema, cbSchema, name, desc, tags, hasTags) , (name), ((protocol) & 31) | 96 _tlgInfoVals_0 ## hasTags(0, tags), cbSchema, { _tlg_FLATTEN bSchema }
#define _TlgInfoVals_TlgCustomin_type, out_type, has_out, cVals, name, has_tags, tags) , (name), ((in_type) & 31) | 96 _TlgInfoVals_0 ## has_tags(0, tags), has_out, { _TLG_FLATTEN out_type }
#define _tlgInfoVals_tlgCustomAnnotn, key, value)
#define _tlgInfoVals_tlgDescn, eventDescription)
#define _TlgInfoVals_TlgDescin_type, out_type, has_out, cVals, name, has_tags, tags)
#define _TlgInfoVals_TlgEvtTagin_type, out_type, has_out, cVals, name, has_tags, tags)
#define _tlgInfoVals_tlgKeywordn, eventKeyword)
#define _TlgInfoVals_TlgKeywordin_type, out_type, has_out, cVals, name, has_tags, tags)
#define _tlgInfoVals_tlgLeveln, eventLevel)
#define _TlgInfoVals_TlgLevelin_type, out_type, has_out, cVals, name, has_tags, tags)
#define _tlgInfoVals_tlgOpcoden, eventOpcode)
#define _TlgInfoVals_TlgOpcodein_type, out_type, has_out, cVals, name, has_tags, tags)
#define _tlgInfoVals_tlgPackedDatan, ctype, pValue, cbValue, dataDescType, hasType)
#define _TlgInfoVals_TlgPDin_type, out_type, has_out, cVals, name, has_tags, tags)
#define _tlgInfoVals_tlgStructn, fieldCount, inType, name, desc, tags, hasTags) , (name), inType _tlgInfoVals_1 ## hasTags(fieldCount, tags)
#define _TlgInfoVals_TlgStructin_type, out_type, has_out, cVals, name, has_tags, tags) , (name), in_type _TlgInfoVals_1 ## has_tags(cVals, tags)
#define _tlgInfoVals_tlgTagn, eventTag)
#define _tlgInfoVarsn, args) _tlgApplyArgsN(_tlgInfoVars, n, args)
#define _tlgInfoVars_0n)
#define _TlgInfoVars_0n)
#define _tlgInfoVars_00n)
#define _TlgInfoVars_00n)
#define _tlgInfoVars_tlgChanneln, eventChannel)
#define _tlgInfoVars_tlgCustomAnnotn, key, value)
#define _tlgInfoVars_tlgDescn, eventDescription)
#define _tlgInfoVars_tlgKeywordn, eventKeyword)
#define _tlgInfoVars_tlgLeveln, eventLevel)
#define _tlgInfoVars_tlgOpcoden, eventOpcode)
#define _tlgInfoVars_tlgPackedDatan, ctype, pValue, cbValue, dataDescType, hasType)
#define _TlgInfoVars_TlgPDn, name, in_type, has_out, has_tags)
#define _tlgInfoVars_tlgTagn, eventTag)
#define _tlgKeywordConstn) n
#define _TlgKeywordVal_TlgAvalue)
#define _tlgKeywordVal_tlgArrayctype, pValues, cValues, inType, outType, hasOut, name, desc, tags, hasTags)
#define _tlgKeywordVal_tlgAutovalue, name, desc, tags, hasTags)
#define _TlgKeywordVal_TlgAutovalue)
#define _TlgKeywordVal_TlgBvalue)
#define _tlgKeywordVal_tlgBinaryctype, pValue, cbValue, inType, outType, hasOut, name, desc, tags, hasTags)
#define _tlgKeywordVal_tlgBufferctype, pValue, inType, outType, hasOut, name, desc, tags, hasTags)
#define _tlgKeywordVal_tlgChanneleventChannel)
#define _TlgKeywordVal_TlgChannelvalue)
#define _tlgKeywordVal_tlgCountedctype, pchValue, cchValue, inType, outType, hasOut, name, desc, tags, hasTags)
#define _TlgKeywordVal_TlgCSvalue)
#define _TlgKeywordVal_TlgCustAnnvalue)
#define _tlgKeywordVal_tlgCustomctype, pValue, cbValue, protocol, bSchema, cbSchema, name, desc, tags, hasTags)
#define _TlgKeywordVal_TlgCustomvalue)
#define _tlgKeywordVal_tlgCustomAnnotkey, value)
#define _tlgKeywordVal_tlgDesceventDescription)
#define _TlgKeywordVal_TlgDescvalue)
#define _TlgKeywordVal_TlgEvtTagvalue)
#define _TlgKeywordVal_TlgFAvalue)
#define _tlgKeywordVal_tlgFixedArrayctype, pValues, cValues, inType, outType, hasOut, name, desc, tags, hasTags)
#define _tlgKeywordVal_tlgKeywordeventKeyword) | (eventKeyword)
#define _TlgKeywordVal_TlgKeywordvalue) | _TlgConst(Keyword, value)
#define _tlgKeywordVal_tlgLeveleventLevel)
#define _TlgKeywordVal_TlgLevelvalue)
#define _tlgKeywordVal_tlgOpcodeeventOpcode)
#define _TlgKeywordVal_TlgOpcodevalue)
#define _TlgKeywordVal_TlgPvalue)
#define _tlgKeywordVal_tlgPackedDatactype, pValue, cbValue, dataDescType, hasType)
#define _tlgKeywordVal_tlgPackedFieldctype, pValue, cbValue, inType, outType, hasOut, name, desc, tags, hasTags)
#define _tlgKeywordVal_tlgPackedMetainType, outType, hasOut, name, desc, tags, hasTags)
#define _TlgKeywordVal_TlgPDvalue)
#define _TlgKeywordVal_TlgPFvalue)
#define _TlgKeywordVal_TlgPMvalue)
#define _tlgKeywordVal_tlgPszctype, pszValue, inType, outType, hasOut, name, desc, tags, hasTags)
#define _TlgKeywordVal_TlgSvalue)
#define _tlgKeywordVal_tlgScalarRefctype, value, inType, outType, hasOut, name, desc, tags, hasTags)
#define _tlgKeywordVal_tlgScalarValctype, value, inType, outType, hasOut, name, desc, tags, hasTags)
#define _tlgKeywordVal_tlgSidctype, pValue, name, desc, tags, hasTags)
#define _TlgKeywordVal_TlgSidvalue)
#define _TlgKeywordVal_TlgStringvalue)
#define _tlgKeywordVal_tlgStructfieldCount, inType, name, desc, tags, hasTags)
#define _TlgKeywordVal_TlgStruct...)
#define _TlgKeywordVal_TlgSzvalue)
#define _tlgKeywordVal_tlgTageventTag)
#define _TlgKeywordVal_TlgWszvalue)
#define _TlgLevelVal_TlgAvalue)
#define _tlgLevelVal_tlgArrayctype, pValues, cValues, inType, outType, hasOut, name, desc, tags, hasTags)
#define _tlgLevelVal_tlgAutovalue, name, desc, tags, hasTags)
#define _TlgLevelVal_TlgAutovalue)
#define _TlgLevelVal_TlgBvalue)
#define _tlgLevelVal_tlgBinaryctype, pValue, cbValue, inType, outType, hasOut, name, desc, tags, hasTags)
#define _tlgLevelVal_tlgBufferctype, pValue, inType, outType, hasOut, name, desc, tags, hasTags)
#define _tlgLevelVal_tlgChanneleventChannel)
#define _TlgLevelVal_TlgChannelvalue)
#define _tlgLevelVal_tlgCountedctype, pchValue, cchValue, inType, outType, hasOut, name, desc, tags, hasTags)
#define _TlgLevelVal_TlgCSvalue)
#define _TlgLevelVal_TlgCustAnnvalue)
#define _tlgLevelVal_tlgCustomctype, pValue, cbValue, protocol, bSchema, cbSchema, name, desc, tags, hasTags)
#define _TlgLevelVal_TlgCustomvalue)
#define _tlgLevelVal_tlgCustomAnnotkey, value)
#define _tlgLevelVal_tlgDesceventDescription)
#define _TlgLevelVal_TlgDescvalue)
#define _TlgLevelVal_TlgEvtTagvalue)
#define _TlgLevelVal_TlgFAvalue)
#define _tlgLevelVal_tlgFixedArrayctype, pValues, cValues, inType, outType, hasOut, name, desc, tags, hasTags)
#define _tlgLevelVal_tlgKeywordeventKeyword)
#define _TlgLevelVal_TlgKeywordvalue)
#define _tlgLevelVal_tlgLeveleventLevel) & 0 | (eventLevel)
#define _TlgLevelVal_TlgLevelvalue) & 0 | _TlgConst(Level, value)
#define _tlgLevelVal_tlgOpcodeeventOpcode)
#define _TlgLevelVal_TlgOpcodevalue)
#define _TlgLevelVal_TlgPvalue)
#define _tlgLevelVal_tlgPackedDatactype, pValue, cbValue, dataDescType, hasType)
#define _tlgLevelVal_tlgPackedFieldctype, pValue, cbValue, inType, outType, hasOut, name, desc, tags, hasTags)
#define _tlgLevelVal_tlgPackedMetainType, outType, hasOut, name, desc, tags, hasTags)
#define _TlgLevelVal_TlgPDvalue)
#define _TlgLevelVal_TlgPFvalue)
#define _TlgLevelVal_TlgPMvalue)
#define _tlgLevelVal_tlgPszctype, pszValue, inType, outType, hasOut, name, desc, tags, hasTags)
#define _TlgLevelVal_TlgSvalue)
#define _tlgLevelVal_tlgScalarRefctype, value, inType, outType, hasOut, name, desc, tags, hasTags)
#define _tlgLevelVal_tlgScalarValctype, value, inType, outType, hasOut, name, desc, tags, hasTags)
#define _tlgLevelVal_tlgSidctype, pValue, name, desc, tags, hasTags)
#define _TlgLevelVal_TlgSidvalue)
#define _TlgLevelVal_TlgStringvalue)
#define _tlgLevelVal_tlgStructfieldCount, inType, name, desc, tags, hasTags)
#define _TlgLevelVal_TlgStruct...)
#define _TlgLevelVal_TlgSzvalue)
#define _tlgLevelVal_tlgTageventTag)
#define _TlgLevelVal_TlgWszvalue)
#define _TlgMagicVal (-4933124757307257210LL)
#define _tlgNDT_imp0value, ...) # value,,, 0
#define _tlgNDT_imp1value, name) name,,, 0
#define _tlgNDT_imp2value, name, desc) name, L"" desc,, 0
#define _tlgNDT_imp3value, name, desc, tags) name, L"" desc, tags, 1
#define _tlgNDT_impBmacro, args) macro args
#define _tlgOpcodeValn, args) _tlgApplyArgs(_tlgOpcodeVal, args)
#define _TlgOpcodeVal_TlgAvalue)
#define _tlgOpcodeVal_tlgArrayctype, pValues, cValues, inType, outType, hasOut, name, desc, tags, hasTags)
#define _tlgOpcodeVal_tlgAutovalue, name, desc, tags, hasTags)
#define _TlgOpcodeVal_TlgAutovalue)
#define _TlgOpcodeVal_TlgBvalue)
#define _tlgOpcodeVal_tlgBinaryctype, pValue, cbValue, inType, outType, hasOut, name, desc, tags, hasTags)
#define _tlgOpcodeVal_tlgBufferctype, pValue, inType, outType, hasOut, name, desc, tags, hasTags)
#define _tlgOpcodeVal_tlgChanneleventChannel)
#define _TlgOpcodeVal_TlgChannelvalue)
#define _tlgOpcodeVal_tlgCountedctype, pchValue, cchValue, inType, outType, hasOut, name, desc, tags, hasTags)
#define _TlgOpcodeVal_TlgCSvalue)
#define _TlgOpcodeVal_TlgCustAnnvalue)
#define _tlgOpcodeVal_tlgCustomctype, pValue, cbValue, protocol, bSchema, cbSchema, name, desc, tags, hasTags)
#define _TlgOpcodeVal_TlgCustomvalue)
#define _tlgOpcodeVal_tlgCustomAnnotkey, value)
#define _tlgOpcodeVal_tlgDesceventDescription)
#define _TlgOpcodeVal_TlgDescvalue)
#define _TlgOpcodeVal_TlgEvtTagvalue)
#define _TlgOpcodeVal_TlgFAvalue)
#define _tlgOpcodeVal_tlgFixedArrayctype, pValues, cValues, inType, outType, hasOut, name, desc, tags, hasTags)
#define _tlgOpcodeVal_tlgKeywordeventKeyword)
#define _TlgOpcodeVal_TlgKeywordvalue)
#define _tlgOpcodeVal_tlgLeveleventLevel)
#define _TlgOpcodeVal_TlgLevelvalue)
#define _tlgOpcodeVal_tlgOpcodeeventOpcode) & 0 | (eventOpcode)
#define _TlgOpcodeVal_TlgOpcodevalue) & 0 | _TlgConst(Opcode, value)
#define _TlgOpcodeVal_TlgPvalue)
#define _tlgOpcodeVal_tlgPackedDatactype, pValue, cbValue, dataDescType, hasType)
#define _tlgOpcodeVal_tlgPackedFieldctype, pValue, cbValue, inType, outType, hasOut, name, desc, tags, hasTags)
#define _tlgOpcodeVal_tlgPackedMetainType, outType, hasOut, name, desc, tags, hasTags)
#define _TlgOpcodeVal_TlgPDvalue)
#define _TlgOpcodeVal_TlgPFvalue)
#define _TlgOpcodeVal_TlgPMvalue)
#define _tlgOpcodeVal_tlgPszctype, pszValue, inType, outType, hasOut, name, desc, tags, hasTags)
#define _TlgOpcodeVal_TlgSvalue)
#define _tlgOpcodeVal_tlgScalarRefctype, value, inType, outType, hasOut, name, desc, tags, hasTags)
#define _tlgOpcodeVal_tlgScalarValctype, value, inType, outType, hasOut, name, desc, tags, hasTags)
#define _tlgOpcodeVal_tlgSidctype, pValue, name, desc, tags, hasTags)
#define _TlgOpcodeVal_TlgSidvalue)
#define _TlgOpcodeVal_TlgStringvalue)
#define _tlgOpcodeVal_tlgStructfieldCount, inType, name, desc, tags, hasTags)
#define _TlgOpcodeVal_TlgStruct...)
#define _TlgOpcodeVal_TlgSzvalue)
#define _tlgOpcodeVal_tlgTageventTag)
#define _TlgOpcodeVal_TlgWszvalue)
#define _tlgPragmaUtf8Begin
#define _tlgPragmaUtf8End
#define _tlgProvValn, args) _tlgApplyArgs(_tlgProvVal, args)
#define _tlgProvVal_tlgOptiontraitType, ctype, value) , 3 + sizeof (ctype), traitType, _tlg_FLATTEN value
#define _tlgProvVarn, args) _tlgApplyArgsN(_tlgProvVar, n, args)
#define _tlgSegMetadataBegin 0
#define _TlgSegMetadataBegin 0
#define _TlgSeqEnd
#define _TlgSeqStart
#define _TlgSigVal 0x30575445
#define _TlgSkipVal 2
#define _TlgTempVars_TlgChanneln, data_type, in_type, value, cVals)
#define _TlgTempVars_TlgCustAnnn, data_type, in_type, value, cVals)
#define _TlgTempVars_TlgDescn, data_type, in_type, value, cVals)
#define _TlgTempVars_TlgEvtTagn, data_type, in_type, value, cVals)
#define _TlgTempVars_TlgKeywordn, data_type, in_type, value, cVals)
#define _TlgTempVars_TlgLeveln, data_type, in_type, value, cVals)
#define _TlgTempVars_TlgOpcoden, data_type, in_type, value, cVals)
#define _TlgTempVars_TlgPMn, data_type, in_type, value, cVals) _TLG_AssertValidPackedMetadataInType(in_type);
#define _TlgTempVars_TlgStructn, data_type, in_type, value, cVals) _TLG_CASSERT(cVals < 128, "Struct size must be less than 128");
#define _tlgValidateProviderId_parenthesis1...)
#define _tlgValidateProviderId_parenthesisCALLmacro, args) macro args
#define _tlgValidateProviderId_presentCALLmacro, args) macro args
#define _TlgVerVal 0
#define _Tracelogging_SyntaxError_ProviderIdMustBeElevenIntegers_11...) /* not an error */ { __VA_ARGS__ }
#define _Tracelogging_UnrecognizedOption__TlgOptionGroup...) /* recognized */ { __VA_ARGS__ }
#define _traceloggingprovider_ 0
#define _TRACELOGGINGPROVIDER_ 0
#define TLG_DEBUG 1
#define TLG_EVENT_REGISTER 0
#define TLG_EVENT_SET_INFORMATION EtwSetInformation
#define TLG_EVENT_UNREGISTER EtwUnregister
#define TLG_EVENT_WRITE_EX EtwWriteEx
#define TLG_EVENT_WRITE_TRANSFER EtwWriteTransfer
#define TLG_HAVE_EVENT_SET_INFORMATION 1
#define TLG_INLINE __inline
#define TLG_METADATA_SEGMENT_BASE . rdata
#define TLG_NONPAGED_SEGMENT 0
#define TLG_NULL NULL
#define TLG_PAGED_CODE) PAGED_CODE()
#define TLG_PAGED_SEGMENT "PAGE"
#define TLG_PFORCEINLINE 0
#define TLG_RAISEASSERTIONFAILURE) __debugbreak()
#define TRACELOGGING_PROTOCOL_MICROSOFT_RESERVED0 0
#define TRACELOGGING_PROTOCOL_MICROSOFT_RESERVED1 1
#define TRACELOGGING_PROTOCOL_MICROSOFT_RESERVED2 2
#define TRACELOGGING_PROTOCOL_MICROSOFT_RESERVED3 3
#define TRACELOGGING_PROTOCOL_MICROSOFT_RESERVED4 4

enum _TlgBlob_t
{
    _TlgBlobNone = 0,
    _TlgBlobEnd = 1,
    _TlgBlobProvider = 2,
    _TlgBlobEvent3 = 3,
    _TlgBlobProvider3 = 4,
    _TlgBlobEvent2 = 5,
    _TlgBlobEvent4 = 6,
    _TlgBlobMax = 7,
};

enum _TlgFlags_t
{
    _TlgFlagsNone = 0,
    _TlgFlagsPtr64 = 1,
};

enum _TlgOptions_t
{
    _TlgOptionNone = 0,
    _TlgOptionGroup = 1,
};

enum TlgIn_t
{
    TlgInNULL = 0,
    TlgInUNICODESTRING = 1,
    TlgInANSISTRING = 2,
    TlgInINT8 = 3,
    TlgInUINT8 = 4,
    TlgInINT16 = 5,
    TlgInUINT16 = 6,
    TlgInINT32 = 7,
    TlgInUINT32 = 8,
    TlgInINT64 = 9,
    TlgInUINT64 = 0xA,
    TlgInFLOAT = 0xB,
    TlgInDOUBLE = 0xC,
    TlgInBOOL32 = 0xD,
    TlgInBINARY = 0xE,
    TlgInGUID = 0xF,
    _TlgInPOINTER_unsupported = 0x10,
    TlgInFILETIME = 0x11,
    TlgInSYSTEMTIME = 0x12,
    TlgInSID = 0x13,
    TlgInHEXINT32 = 0x14,
    TlgInHEXINT64 = 0x15,
    TlgInCOUNTEDSTRING = 0x16,
    TlgInCOUNTEDANSISTRING = 0x17,
    _TlgInSTRUCT = 0x18,
    TlgInCOUNTEDBINARY = 0x19,
    _TlgInMax = 0x1A,
    TlgInINTPTR = 9,
    TlgInUINTPTR = 0xA,
    TlgInPOINTER = 0x15,
    TlgInLONG = 7,
    TlgInULONG = 8,
    TlgInHEXLONG = 0x14,
    _TlgInCcount = 0x20,
    TlgInVcount = 0x40,
    _TlgInChain = 0x80,
    _TlgInCustom = 0x60,
    _TlgInTypeMask = 0x1F,
    _TlgInCountMask = 0x60,
    _TlgInFlagMask = 0xE0,
};

enum TlgOut_t
{
    TlgOutNULL = 0,
    TlgOutNOPRINT = 1,
    TlgOutSTRING = 2,
    TlgOutBOOLEAN = 3,
    TlgOutHEX = 4,
    TlgOutPID = 5,
    TlgOutTID = 6,
    TlgOutPORT = 7,
    TlgOutIPV4 = 8,
    TlgOutIPV6 = 9,
    TlgOutSOCKETADDRESS = 0xA,
    TlgOutXML = 0xB,
    TlgOutJSON = 0xC,
    TlgOutWIN32ERROR = 0xD,
    TlgOutNTSTATUS = 0xE,
    TlgOutHRESULT = 0xF,
    TlgOutFILETIME = 0x10,
    TlgOutSIGNED = 0x11,
    TlgOutUNSIGNED = 0x12,
    TlgOutUTF8 = 0x23,
    TlgOutPKCS7_WITH_TYPE_INFO = 0x24,
    TlgOutCODE_POINTER = 0x25,
    TlgOutDATETIME_UTC = 0x26,
    _TlgOutMax = 0x27,
    _TlgOutChain = 0x80,
    _TlgOutTypeMask = 0x7F,
};

#pragma pack(push, 1)
struct _TlgEventMetadata_t
{
    UINT8 Type;
    UCHAR Channel;
    UCHAR Level;
    UCHAR Opcode;
    ULONGLONG Keyword;
    UINT16 RemainingSize;
};
#pragma pack(pop)

#pragma pack(push, 1)
struct _tlgEventMetadata_t
{
    UINT8 Type;
    UCHAR Channel;
    UCHAR Level;
    UCHAR Opcode;
    ULONGLONG Keyword;
    UINT16 RemainingSize;
};
#pragma pack(pop)

struct _tlgProvider_t
{
    UINT32 LevelPlus1;
    const __unaligned UINT16 *ProviderMetadataPtr;
    ULONGLONG KeywordAny;
    ULONGLONG KeywordAll;
    REGHANDLE RegHandle;
    TLG_PENABLECALLBACK EnableCallback;
    PVOID CallbackContext;
};

#pragma pack(push, 1)
struct _TlgProviderMetadata_t
{
    UINT8 Type;
    GUID ProviderId;
    UINT16 RemainingSize;
};
#pragma pack(pop)

#pragma pack(push, 1)
struct _tlgProviderMetadata_t
{
    UINT8 Type;
    GUID ProviderId;
    UINT16 RemainingSize;
};
#pragma pack(pop)

struct _TraceLoggingMetadata_t
{
    UINT32 Signature;
    UINT16 Size;
    UINT8 Version;
    UINT8 Flags;
    UINT64 Magic;
};

typedef int TLG_EVENT_REGISTER_must_not_be_a_functionLike_macro_EtwRegister;

typedef void TLG_EVENT_REGISTER_must_not_be_a_functionLike_macro_TLG_EVENT_REGISTER;

typedef int TLG_EVENT_SET_INFORMATION_must_not_be_a_functionLike_macro_EtwSetInformation;

typedef void TLG_EVENT_SET_INFORMATION_must_not_be_a_functionLike_macro_TLG_EVENT_SET_INFORMATION;

typedef int TLG_EVENT_UNREGISTER_must_not_be_a_functionLike_macro_EtwUnregister;

typedef void TLG_EVENT_UNREGISTER_must_not_be_a_functionLike_macro_TLG_EVENT_UNREGISTER;

typedef int TLG_EVENT_WRITE_EX_must_not_be_a_functionLike_macro_EtwWriteEx;

typedef void TLG_EVENT_WRITE_EX_must_not_be_a_functionLike_macro_TLG_EVENT_WRITE_EX;

typedef int TLG_EVENT_WRITE_TRANSFER_must_not_be_a_functionLike_macro_EtwWriteTransfer;

typedef void TLG_EVENT_WRITE_TRANSFER_must_not_be_a_functionLike_macro_TLG_EVENT_WRITE_TRANSFER;

typedef NTSTATUS TLG_STATUS;

#ifdef __cplusplus
extern "C" {
#endif

/* TODO: LiberNT does not implement the functions below yet */
void WINAPI _tlgCreate1Sz_char(PEVENT_DATA_DESCRIPTOR pDesc, const char *psz);
void WINAPI _tlgCreate1Sz_wchar_t(PEVENT_DATA_DESCRIPTOR pDesc, const wchar_t *psz);
void WINAPI _tlgCreate2Binary(PEVENT_DATA_DESCRIPTOR pDesc, const void *pValue, ULONG cbValue);
void _TlgCreateArray(PEVENT_DATA_DESCRIPTOR pDesc, const void *pVals, const UINT16 *pcVals, ULONG cbVal);
void _TlgCreateBinary(PEVENT_DATA_DESCRIPTOR pDesc, const void *pVals, ULONG cVals);
void _TlgCreateFixedArray(PEVENT_DATA_DESCRIPTOR pDesc, const void *pVals, ULONG cVals, ULONG cbVal);
void _TlgCreateSid(PEVENT_DATA_DESCRIPTOR pDesc, const SID *pSid);
void _TlgCreateSz(PEVENT_DATA_DESCRIPTOR pDesc, LPCSTR psz);
void _TlgCreateWsz(PEVENT_DATA_DESCRIPTOR pDesc, LPCWSTR pwsz);
void WINAPI _tlgEnableCallback(LPCGUID pSourceId, ULONG callbackType, UCHAR level, ULONGLONG keywordAny, ULONGLONG keywordAll, PEVENT_FILTER_DESCRIPTOR pFilterData, PVOID pCallbackContext);
void WINAPI _TlgEnableCallback(LPCGUID pSourceId, ULONG callbackType, UCHAR level, ULONGLONG keywordAny, ULONGLONG keywordAll, PEVENT_FILTER_DESCRIPTOR pFilterData, PVOID pCallbackContext);
BOOLEAN WINAPI _tlgKeywordOn(TraceLoggingHProvider hProvider, ULONGLONG keyword);
BOOLEAN _TlgKeywordOn(TraceLoggingHProvider hProvider, ULONGLONG keyword);
TLG_STATUS _TlgWrite(TraceLoggingHProvider hProvider, const void *pEventMetadata, LPCGUID pActivityId, LPCGUID pRelatedActivityId, UINT32 cData, EVENT_DATA_DESCRIPTOR *pData);
void _tlgWriteCommon(TraceLoggingHProvider hProvider, const void *pEventMetadata, ULONGLONG *pDesc, EVENT_DATA_DESCRIPTOR *pData);
void _TlgWriteCommon(TraceLoggingHProvider hProvider, const void *pEventMetadata, ULONGLONG *pDesc, EVENT_DATA_DESCRIPTOR *pData);
TLG_STATUS _TlgWriteEx(TraceLoggingHProvider hProvider, const void *pEventMetadata, ULONG64 filter, ULONG flags, LPCGUID pActivityId, LPCGUID pRelatedActivityId, UINT32 cData, EVENT_DATA_DESCRIPTOR *pData);
TLG_STATUS WINAPI _tlgWriteEx_EtwWriteEx(TraceLoggingHProvider hProvider, const void *pEventMetadata, ULONG64 filter, ULONG flags, LPCGUID pActivityId, LPCGUID pRelatedActivityId, UINT32 cData, EVENT_DATA_DESCRIPTOR *pData);
TLG_STATUS WINAPI _tlgWriteTransfer_EtwWriteTransfer(TraceLoggingHProvider hProvider, const void *pEventMetadata, LPCGUID pActivityId, LPCGUID pRelatedActivityId, UINT32 cData, EVENT_DATA_DESCRIPTOR *pData);
GUID WINAPI TraceLoggingProviderId(TraceLoggingHProvider hProvider);
TLG_STATUS WINAPI TraceLoggingRegister_EtwRegister_EtwSetInformation(TraceLoggingHProvider hProvider);
TLG_STATUS WINAPI TraceLoggingRegisterEx_EtwRegister_EtwSetInformation(TraceLoggingHProvider hProvider, TLG_PENABLECALLBACK pEnableCallback, PVOID pCallbackContext);
TLG_STATUS WINAPI TraceLoggingSetInformation_EtwSetInformation(TraceLoggingHProvider hProvider, EVENT_INFO_CLASS informationClass, PVOID pvInformation, ULONG cbInformation);
void WINAPI TraceLoggingUnregister_EtwUnregister(TraceLoggingHProvider hProvider);

#ifdef __cplusplus
}
#endif

#define _tlg_ASSERTexp, str) ((void) (!(exp) ? (__annotation(L"Debug", L"AssertFail", L"TraceLogging: " _tlg_LSTRINGIZE(exp) L" : " str), TLG_RAISEASSERTIONFAILURE(), 0) : 0))
#define _TLG_ASSERTexp, str) ((void) (!(exp) ? (__annotation(L"Debug", L"AssertFail", L"TraceLogging: " L ## # exp L" : " L ## str), DbgRaiseAssertionFailure(), 0) : 0))
#define _tlg_AssertValidPackedMetadataInTypeCppOnlyinType)
#define _TLG_AssertValidPackedMetadataInTypeCppOnlyin_type)
#define _tlg_CATa, ...) _tlg_CAT_imp(a, __VA_ARGS__)
#define _TLG_CATa, ...) _TLG_CAT_imp(a, __VA_ARGS__)
#define _tlg_FASTFAILcode) __fastfail(code)
#define _tlg_FOR_imp0f, ...)
#define _TLG_FOR_imp0fn)
#define _tlg_FOR_imp1f, a0) f(0, a0)
#define _TLG_FOR_imp1fn, arg, ...) _TLG_FOR_impN(0, fn, arg, fn, __VA_ARGS__)
#define _tlg_FOR_imp10f, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9) f(0, a0) f(1, a1) f(2, a2) f(3, a3) f(4, a4) f(5, a5) f(6, a6) f(7, a7) f(8, a8) f(9, a9)
#define _TLG_FOR_imp10fn, arg, ...) _TLG_FOR_impN(9, fn, arg, fn, __VA_ARGS__)
#define _tlg_FOR_imp11f, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10) f(0, a0) f(1, a1) f(2, a2) f(3, a3) f(4, a4) f(5, a5) f(6, a6) f(7, a7) f(8, a8) f(9, a9) f(10, a10)
#define _TLG_FOR_imp11fn, arg, ...) _TLG_FOR_impN(10, fn, arg, fn, __VA_ARGS__)
#define _tlg_FOR_imp12f, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11) f(0, a0) f(1, a1) f(2, a2) f(3, a3) f(4, a4) f(5, a5) f(6, a6) f(7, a7) f(8, a8) f(9, a9) f(10, a10) f(11, a11)
#define _TLG_FOR_imp12fn, arg, ...) _TLG_FOR_impN(11, fn, arg, fn, __VA_ARGS__)
#define _tlg_FOR_imp13f, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12) f(0, a0) f(1, a1) f(2, a2) f(3, a3) f(4, a4) f(5, a5) f(6, a6) f(7, a7) f(8, a8) f(9, a9) f(10, a10) f(11, a11) f(12, a12)
#define _TLG_FOR_imp13fn, arg, ...) _TLG_FOR_impN(12, fn, arg, fn, __VA_ARGS__)
#define _tlg_FOR_imp14f, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13) f(0, a0) f(1, a1) f(2, a2) f(3, a3) f(4, a4) f(5, a5) f(6, a6) f(7, a7) f(8, a8) f(9, a9) f(10, a10) f(11, a11) f(12, a12) f(13, a13)
#define _TLG_FOR_imp14fn, arg, ...) _TLG_FOR_impN(13, fn, arg, fn, __VA_ARGS__)
#define _tlg_FOR_imp15f, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14) f(0, a0) f(1, a1) f(2, a2) f(3, a3) f(4, a4) f(5, a5) f(6, a6) f(7, a7) f(8, a8) f(9, a9) f(10, a10) f(11, a11) f(12, a12) f(13, a13) f(14, a14)
#define _TLG_FOR_imp15fn, arg, ...) _TLG_FOR_impN(14, fn, arg, fn, __VA_ARGS__)
#define _tlg_FOR_imp16f, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15) f(0, a0) f(1, a1) f(2, a2) f(3, a3) f(4, a4) f(5, a5) f(6, a6) f(7, a7) f(8, a8) f(9, a9) f(10, a10) f(11, a11) f(12, a12) f(13, a13) f(14, a14) f(15, a15)
#define _TLG_FOR_imp16fn, arg, ...) _TLG_FOR_impN(15, fn, arg, fn, __VA_ARGS__)
#define _tlg_FOR_imp17f, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16) f(0, a0) f(1, a1) f(2, a2) f(3, a3) f(4, a4) f(5, a5) f(6, a6) f(7, a7) f(8, a8) f(9, a9) f(10, a10) f(11, a11) f(12, a12) f(13, a13) f(14, a14) f(15, a15) f(16, a16)
#define _TLG_FOR_imp17fn, arg, ...) _TLG_FOR_impN(16, fn, arg, fn, __VA_ARGS__)
#define _tlg_FOR_imp18f, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17) f(0, a0) f(1, a1) f(2, a2) f(3, a3) f(4, a4) f(5, a5) f(6, a6) f(7, a7) f(8, a8) f(9, a9) f(10, a10) f(11, a11) f(12, a12) f(13, a13) f(14, a14) f(15, a15) f(16, a16) f(17, a17)
#define _TLG_FOR_imp18fn, arg, ...) _TLG_FOR_impN(17, fn, arg, fn, __VA_ARGS__)
#define _tlg_FOR_imp19f, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17, a18) f(0, a0) f(1, a1) f(2, a2) f(3, a3) f(4, a4) f(5, a5) f(6, a6) f(7, a7) f(8, a8) f(9, a9) f(10, a10) f(11, a11) f(12, a12) f(13, a13) f(14, a14) f(15, a15) f(16, a16) f(17, a17) f(18, a18)
#define _TLG_FOR_imp19fn, arg, ...) _TLG_FOR_impN(18, fn, arg, fn, __VA_ARGS__)
#define _tlg_FOR_imp2f, a0, a1) f(0, a0) f(1, a1)
#define _TLG_FOR_imp2fn, arg, ...) _TLG_FOR_impN(1, fn, arg, fn, __VA_ARGS__)
#define _tlg_FOR_imp20f, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17, a18, a19) f(0, a0) f(1, a1) f(2, a2) f(3, a3) f(4, a4) f(5, a5) f(6, a6) f(7, a7) f(8, a8) f(9, a9) f(10, a10) f(11, a11) f(12, a12) f(13, a13) f(14, a14) f(15, a15) f(16, a16) f(17, a17) f(18, a18) f(19, a19)
#define _TLG_FOR_imp20fn, arg, ...) _TLG_FOR_impN(19, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp21fn, arg, ...) _TLG_FOR_impN(20, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp22fn, arg, ...) _TLG_FOR_impN(21, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp23fn, arg, ...) _TLG_FOR_impN(22, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp24fn, arg, ...) _TLG_FOR_impN(23, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp25fn, arg, ...) _TLG_FOR_impN(24, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp26fn, arg, ...) _TLG_FOR_impN(25, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp27fn, arg, ...) _TLG_FOR_impN(26, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp28fn, arg, ...) _TLG_FOR_impN(27, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp29fn, arg, ...) _TLG_FOR_impN(28, fn, arg, fn, __VA_ARGS__)
#define _tlg_FOR_imp3f, a0, a1, a2) f(0, a0) f(1, a1) f(2, a2)
#define _TLG_FOR_imp3fn, arg, ...) _TLG_FOR_impN(2, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp30fn, arg, ...) _TLG_FOR_impN(29, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp31fn, arg, ...) _TLG_FOR_impN(30, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp32fn, arg, ...) _TLG_FOR_impN(31, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp33fn, arg, ...) _TLG_FOR_impN(32, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp34fn, arg, ...) _TLG_FOR_impN(33, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp35fn, arg, ...) _TLG_FOR_impN(34, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp36fn, arg, ...) _TLG_FOR_impN(35, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp37fn, arg, ...) _TLG_FOR_impN(36, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp38fn, arg, ...) _TLG_FOR_impN(37, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp39fn, arg, ...) _TLG_FOR_impN(38, fn, arg, fn, __VA_ARGS__)
#define _tlg_FOR_imp4f, a0, a1, a2, a3) f(0, a0) f(1, a1) f(2, a2) f(3, a3)
#define _TLG_FOR_imp4fn, arg, ...) _TLG_FOR_impN(3, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp40fn, arg, ...) _TLG_FOR_impN(39, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp41fn, arg, ...) _TLG_FOR_impN(40, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp42fn, arg, ...) _TLG_FOR_impN(41, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp43fn, arg, ...) _TLG_FOR_impN(42, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp44fn, arg, ...) _TLG_FOR_impN(43, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp45fn, arg, ...) _TLG_FOR_impN(44, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp46fn, arg, ...) _TLG_FOR_impN(45, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp47fn, arg, ...) _TLG_FOR_impN(46, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp48fn, arg, ...) _TLG_FOR_impN(47, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp49fn, arg, ...) _TLG_FOR_impN(48, fn, arg, fn, __VA_ARGS__)
#define _tlg_FOR_imp5f, a0, a1, a2, a3, a4) f(0, a0) f(1, a1) f(2, a2) f(3, a3) f(4, a4)
#define _TLG_FOR_imp5fn, arg, ...) _TLG_FOR_impN(4, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp50fn, arg, ...) _TLG_FOR_impN(49, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp51fn, arg, ...) _TLG_FOR_impN(50, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp52fn, arg, ...) _TLG_FOR_impN(51, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp53fn, arg, ...) _TLG_FOR_impN(52, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp54fn, arg, ...) _TLG_FOR_impN(53, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp55fn, arg, ...) _TLG_FOR_impN(54, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp56fn, arg, ...) _TLG_FOR_impN(55, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp57fn, arg, ...) _TLG_FOR_impN(56, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp58fn, arg, ...) _TLG_FOR_impN(57, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp59fn, arg, ...) _TLG_FOR_impN(58, fn, arg, fn, __VA_ARGS__)
#define _tlg_FOR_imp6f, a0, a1, a2, a3, a4, a5) f(0, a0) f(1, a1) f(2, a2) f(3, a3) f(4, a4) f(5, a5)
#define _TLG_FOR_imp6fn, arg, ...) _TLG_FOR_impN(5, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp60fn, arg, ...) _TLG_FOR_impN(59, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp61fn, arg, ...) _TLG_FOR_impN(60, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp62fn, arg, ...) _TLG_FOR_impN(61, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp63fn, arg, ...) _TLG_FOR_impN(62, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp64fn, arg, ...) _TLG_FOR_impN(63, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp65fn, arg, ...) _TLG_FOR_impN(64, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp66fn, arg, ...) _TLG_FOR_impN(65, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp67fn, arg, ...) _TLG_FOR_impN(66, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp68fn, arg, ...) _TLG_FOR_impN(67, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp69fn, arg, ...) _TLG_FOR_impN(68, fn, arg, fn, __VA_ARGS__)
#define _tlg_FOR_imp7f, a0, a1, a2, a3, a4, a5, a6) f(0, a0) f(1, a1) f(2, a2) f(3, a3) f(4, a4) f(5, a5) f(6, a6)
#define _TLG_FOR_imp7fn, arg, ...) _TLG_FOR_impN(6, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp70fn, arg, ...) _TLG_FOR_impN(69, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp71fn, arg, ...) _TLG_FOR_impN(70, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp72fn, arg, ...) _TLG_FOR_impN(71, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp73fn, arg, ...) _TLG_FOR_impN(72, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp74fn, arg, ...) _TLG_FOR_impN(73, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp75fn, arg, ...) _TLG_FOR_impN(74, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp76fn, arg, ...) _TLG_FOR_impN(75, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp77fn, arg, ...) _TLG_FOR_impN(76, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp78fn, arg, ...) _TLG_FOR_impN(77, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp79fn, arg, ...) _TLG_FOR_impN(78, fn, arg, fn, __VA_ARGS__)
#define _tlg_FOR_imp8f, a0, a1, a2, a3, a4, a5, a6, a7) f(0, a0) f(1, a1) f(2, a2) f(3, a3) f(4, a4) f(5, a5) f(6, a6) f(7, a7)
#define _TLG_FOR_imp8fn, arg, ...) _TLG_FOR_impN(7, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp80fn, arg, ...) _TLG_FOR_impN(79, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp81fn, arg, ...) _TLG_FOR_impN(80, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp82fn, arg, ...) _TLG_FOR_impN(81, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp83fn, arg, ...) _TLG_FOR_impN(82, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp84fn, arg, ...) _TLG_FOR_impN(83, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp85fn, arg, ...) _TLG_FOR_impN(84, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp86fn, arg, ...) _TLG_FOR_impN(85, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp87fn, arg, ...) _TLG_FOR_impN(86, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp88fn, arg, ...) _TLG_FOR_impN(87, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp89fn, arg, ...) _TLG_FOR_impN(88, fn, arg, fn, __VA_ARGS__)
#define _tlg_FOR_imp9f, a0, a1, a2, a3, a4, a5, a6, a7, a8) f(0, a0) f(1, a1) f(2, a2) f(3, a3) f(4, a4) f(5, a5) f(6, a6) f(7, a7) f(8, a8)
#define _TLG_FOR_imp9fn, arg, ...) _TLG_FOR_impN(8, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp90fn, arg, ...) _TLG_FOR_impN(89, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp91fn, arg, ...) _TLG_FOR_impN(90, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp92fn, arg, ...) _TLG_FOR_impN(91, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp93fn, arg, ...) _TLG_FOR_impN(92, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp94fn, arg, ...) _TLG_FOR_impN(93, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp95fn, arg, ...) _TLG_FOR_impN(94, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp96fn, arg, ...) _TLG_FOR_impN(95, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp97fn, arg, ...) _TLG_FOR_impN(96, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp98fn, arg, ...) _TLG_FOR_impN(97, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp99fn, arg, ...) _TLG_FOR_impN(98, fn, arg, fn, __VA_ARGS__)
#define _tlg_NARGS_imp2a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17, a18, a19, a20, a21, a22, a23, a24, a25, a26, a27, a28, a29, a30, a31, a32, a33, a34, a35, a36, a37, a38, a39, a40, a41, a42, a43, a44, a45, a46, a47, a48, a49, a50, a51, a52, a53, a54, a55, a56, a57, a58, a59, a60, a61, a62, a63, a64, a65, a66, a67, a68, a69, a70, a71, a72, a73, a74, a75, a76, a77, a78, a79, a80, a81, a82, a83, a84, a85, a86, a87, a88, a89, a90, a91, a92, a93, a94, a95, a96, a97, a98, a99, size, ...) size
#define _TLG_NARGS_imp2a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17, a18, a19, a20, a21, a22, a23, a24, a25, a26, a27, a28, a29, a30, a31, a32, a33, a34, a35, a36, a37, a38, a39, a40, a41, a42, a43, a44, a45, a46, a47, a48, a49, a50, a51, a52, a53, a54, a55, a56, a57, a58, a59, a60, a61, a62, a63, a64, a65, a66, a67, a68, a69, a70, a71, a72, a73, a74, a75, a76, a77, a78, a79, a80, a81, a82, a83, a84, a85, a86, a87, a88, a89, a90, a91, a92, a93, a94, a95, a96, a97, a98, a99, size, ...) size
#define _tlg_PASTE3a, b, c) _tlg_PASTE3_imp(a, b, c)
#define _tlg_SPLITcond, ...) _tlg_SPLIT_imp(cond, (__VA_ARGS__))
#define _TLG_SPLITcond, ...) _TLG_SPLIT_imp(cond, (__VA_ARGS__))
#define _tlg_SPLIT_imp0false_val, ...) false_val
#define _TLG_SPLIT_imp0false_val, ...) false_val
#define _tlg_SPLIT_imp1false_val, ...) __VA_ARGS__
#define _TLG_SPLIT_imp1false_val, ...) __VA_ARGS__
#define _tlg_STRINGIZEx) _tlg_STRINGIZE_imp(x)
#define _TLG_STRINGIZEx) _TLG_STRINGIZE_imp(x)
#define _tlgAnnotationFunc_imp0storageVariable)
#define _TlgAnnotationFunc_imp0storageVariable) 0
#define _TlgAnnotes_TlgAname, desc) L"|" L ## # name L"=" desc
#define _tlgAnnotes_tlgArrayctype, pValues, cValues, inType, outType, hasOut, name, desc, tags, hasTags) L"|" _tlg_LSTRINGIZE(name) L"=" desc
#define _tlgAnnotes_tlgAutovalue, name, desc, tags, hasTags) L"|" _tlg_LSTRINGIZE(name) L"=" desc
#define _TlgAnnotes_TlgAutoname, desc) L"|" L ## # name L"=" desc
#define _TlgAnnotes_TlgBname, desc) L"|" L ## # name L"=" desc
#define _tlgAnnotes_tlgBinaryctype, pValue, cbValue, inType, outType, hasOut, name, desc, tags, hasTags) L"|" _tlg_LSTRINGIZE(name) L"=" desc
#define _tlgAnnotes_tlgBufferctype, pValue, inType, outType, hasOut, name, desc, tags, hasTags) L"|" _tlg_LSTRINGIZE(name) L"=" desc
#define _tlgAnnotes_tlgChanneleventChannel)
#define _TlgAnnotes_TlgChannelname, desc)
#define _tlgAnnotes_tlgCountedctype, pchValue, cchValue, inType, outType, hasOut, name, desc, tags, hasTags) L"|" _tlg_LSTRINGIZE(name) L"=" desc
#define _TlgAnnotes_TlgCSname, desc) L"|" L ## # name L"=" desc
#define _TlgAnnotes_TlgCustAnnname, desc) L"|" L ## name L"=" desc
#define _tlgAnnotes_tlgCustomctype, pValue, cbValue, protocol, bSchema, cbSchema, name, desc, tags, hasTags) L"|" _tlg_LSTRINGIZE(name) L"=" desc
#define _TlgAnnotes_TlgCustomname, desc) L"|" L ## # name L"=" desc
#define _tlgAnnotes_tlgCustomAnnotkey, value) L"|" key L"=" value
#define _tlgAnnotes_tlgDesceventDescription)
#define _TlgAnnotes_TlgDescname, desc)
#define _TlgAnnotes_TlgEvtTagname, desc)
#define _TlgAnnotes_TlgFAname, desc) L"|" L ## # name L"=" desc
#define _tlgAnnotes_tlgFixedArrayctype, pValues, cValues, inType, outType, hasOut, name, desc, tags, hasTags) L"|" _tlg_LSTRINGIZE(name) L"=" desc
#define _tlgAnnotes_tlgKeywordeventKeyword)
#define _TlgAnnotes_TlgKeywordname, desc)
#define _tlgAnnotes_tlgLeveleventLevel)
#define _TlgAnnotes_TlgLevelname, desc)
#define _tlgAnnotes_tlgOpcodeeventOpcode)
#define _TlgAnnotes_TlgOpcodename, desc)
#define _TlgAnnotes_TlgPname, desc) L"|" L ## # name L"=" desc
#define _tlgAnnotes_tlgPackedDatactype, pValue, cbValue, dataDescType, hasType)
#define _tlgAnnotes_tlgPackedFieldctype, pValue, cbValue, inType, outType, hasOut, name, desc, tags, hasTags) L"|" _tlg_LSTRINGIZE(name) L"=" desc
#define _tlgAnnotes_tlgPackedMetainType, outType, hasOut, name, desc, tags, hasTags) L"|" _tlg_LSTRINGIZE(name) L"=" desc
#define _TlgAnnotes_TlgPDname, desc)
#define _TlgAnnotes_TlgPFname, desc) L"|" L ## # name L"=" desc
#define _TlgAnnotes_TlgPMname, desc) L"|" L ## # name L"=" desc
#define _tlgAnnotes_tlgPszctype, pszValue, inType, outType, hasOut, name, desc, tags, hasTags) L"|" _tlg_LSTRINGIZE(name) L"=" desc
#define _TlgAnnotes_TlgSname, desc) L"|" L ## # name L"=" desc
#define _tlgAnnotes_tlgScalarRefctype, value, inType, outType, hasOut, name, desc, tags, hasTags) L"|" _tlg_LSTRINGIZE(name) L"=" desc
#define _tlgAnnotes_tlgScalarValctype, value, inType, outType, hasOut, name, desc, tags, hasTags) L"|" _tlg_LSTRINGIZE(name) L"=" desc
#define _tlgAnnotes_tlgSidctype, pValue, name, desc, tags, hasTags) L"|" _tlg_LSTRINGIZE(name) L"=" desc
#define _TlgAnnotes_TlgSidname, desc) L"|" L ## # name L"=" desc
#define _TlgAnnotes_TlgStringname, desc) L"|" L ## # name L"=" desc
#define _tlgAnnotes_tlgStructfieldCount, inType, name, desc, tags, hasTags) L"|" _tlg_LSTRINGIZE(name) L"=" desc
#define _TlgAnnotes_TlgStructname, desc) L"|" L ## # name L"=" desc
#define _TlgAnnotes_TlgSzname, desc) L"|" L ## # name L"=" desc
#define _tlgAnnotes_tlgTageventTag)
#define _TlgAnnotes_TlgWszname, desc) L"|" L ## # name L"=" desc
#define _TlgArg_imp0data_type, in_type, out_type, has_out, dim, value, cVals, x) (data_type, in_type, out_type, has_out, dim, value, cVals, # value,,, 0)
#define _TlgArg_imp1data_type, in_type, out_type, has_out, dim, value, cVals, name) (data_type, in_type, out_type, has_out, dim, value, cVals, name,,, 0)
#define _TlgArg_imp2data_type, in_type, out_type, has_out, dim, value, cVals, name, desc) (data_type, in_type, out_type, has_out, dim, value, cVals, name, L ## desc,, 0)
#define _TlgArg_imp3data_type, in_type, out_type, has_out, dim, value, cVals, name, desc, tags) (data_type, in_type, out_type, has_out, dim, value, cVals, name, L ## desc, tags, 1)
#define _TlgChannelVal_TlgAvalue)
#define _tlgChannelVal_tlgArrayctype, pValues, cValues, inType, outType, hasOut, name, desc, tags, hasTags)
#define _tlgChannelVal_tlgAutovalue, name, desc, tags, hasTags)
#define _TlgChannelVal_TlgAutovalue)
#define _TlgChannelVal_TlgBvalue)
#define _tlgChannelVal_tlgBinaryctype, pValue, cbValue, inType, outType, hasOut, name, desc, tags, hasTags)
#define _tlgChannelVal_tlgBufferctype, pValue, inType, outType, hasOut, name, desc, tags, hasTags)
#define _tlgChannelVal_tlgChanneleventChannel) & 0 | (eventChannel)
#define _TlgChannelVal_TlgChannelvalue) & 0 | _TlgConst(Channel, value)
#define _tlgChannelVal_tlgCountedctype, pchValue, cchValue, inType, outType, hasOut, name, desc, tags, hasTags)
#define _TlgChannelVal_TlgCSvalue)
#define _TlgChannelVal_TlgCustAnnvalue)
#define _tlgChannelVal_tlgCustomctype, pValue, cbValue, protocol, bSchema, cbSchema, name, desc, tags, hasTags)
#define _TlgChannelVal_TlgCustomvalue)
#define _tlgChannelVal_tlgCustomAnnotkey, value)
#define _tlgChannelVal_tlgDesceventDescription)
#define _TlgChannelVal_TlgDescvalue)
#define _TlgChannelVal_TlgEvtTagvalue)
#define _TlgChannelVal_TlgFAvalue)
#define _tlgChannelVal_tlgFixedArrayctype, pValues, cValues, inType, outType, hasOut, name, desc, tags, hasTags)
#define _tlgChannelVal_tlgKeywordeventKeyword)
#define _TlgChannelVal_TlgKeywordvalue)
#define _tlgChannelVal_tlgLeveleventLevel)
#define _TlgChannelVal_TlgLevelvalue)
#define _tlgChannelVal_tlgOpcodeeventOpcode)
#define _TlgChannelVal_TlgOpcodevalue)
#define _TlgChannelVal_TlgPvalue)
#define _tlgChannelVal_tlgPackedDatactype, pValue, cbValue, dataDescType, hasType)
#define _tlgChannelVal_tlgPackedFieldctype, pValue, cbValue, inType, outType, hasOut, name, desc, tags, hasTags)
#define _tlgChannelVal_tlgPackedMetainType, outType, hasOut, name, desc, tags, hasTags)
#define _TlgChannelVal_TlgPDvalue)
#define _TlgChannelVal_TlgPFvalue)
#define _TlgChannelVal_TlgPMvalue)
#define _tlgChannelVal_tlgPszctype, pszValue, inType, outType, hasOut, name, desc, tags, hasTags)
#define _TlgChannelVal_TlgSvalue)
#define _tlgChannelVal_tlgScalarRefctype, value, inType, outType, hasOut, name, desc, tags, hasTags)
#define _tlgChannelVal_tlgScalarValctype, value, inType, outType, hasOut, name, desc, tags, hasTags)
#define _tlgChannelVal_tlgSidctype, pValue, name, desc, tags, hasTags)
#define _TlgChannelVal_TlgSidvalue)
#define _TlgChannelVal_TlgStringvalue)
#define _tlgChannelVal_tlgStructfieldCount, inType, name, desc, tags, hasTags)
#define _TlgChannelVal_TlgSzvalue)
#define _tlgChannelVal_tlgTageventTag)
#define _TlgChannelVal_TlgWszvalue)
#define _tlgCreate1Sz_char16_tpDesc, psz) _tlgCreate1Sz_wchar_t(pDesc, psz)
#define _tlgDataDescCount_tlgArrayctype, pValues, cValues, inType, outType, hasOut, name, desc, tags, hasTags) + 2
#define _tlgDataDescCount_tlgAutovalue, name, desc, tags, hasTags) + 1
#define _tlgDataDescCount_tlgBinaryctype, pValue, cbValue, inType, outType, hasOut, name, desc, tags, hasTags) + 2
#define _tlgDataDescCount_tlgBufferctype, pValue, inType, outType, hasOut, name, desc, tags, hasTags) + 2
#define _tlgDataDescCount_tlgChanneleventChannel)
#define _tlgDataDescCount_tlgCountedctype, pchValue, cchValue, inType, outType, hasOut, name, desc, tags, hasTags) + 2
#define _tlgDataDescCount_tlgCustomctype, pValue, cbValue, protocol, bSchema, cbSchema, name, desc, tags, hasTags) + 2
#define _tlgDataDescCount_tlgCustomAnnotkey, value)
#define _tlgDataDescCount_tlgDesceventDescription)
#define _tlgDataDescCount_tlgFixedArrayctype, pValues, cValues, inType, outType, hasOut, name, desc, tags, hasTags) + 1
#define _tlgDataDescCount_tlgKeywordeventKeyword)
#define _tlgDataDescCount_tlgLeveleventLevel)
#define _tlgDataDescCount_tlgOpcodeeventOpcode)
#define _tlgDataDescCount_tlgPackedDatactype, pValue, cbValue, dataDescType, hasType) + 1
#define _tlgDataDescCount_tlgPackedFieldctype, pValue, cbValue, inType, outType, hasOut, name, desc, tags, hasTags) + 1
#define _tlgDataDescCount_tlgPackedMetainType, outType, hasOut, name, desc, tags, hasTags)
#define _tlgDataDescCount_tlgPszctype, pszValue, inType, outType, hasOut, name, desc, tags, hasTags) + 1
#define _tlgDataDescCount_tlgScalarRefctype, value, inType, outType, hasOut, name, desc, tags, hasTags) + 1
#define _tlgDataDescCount_tlgScalarValctype, value, inType, outType, hasOut, name, desc, tags, hasTags) + 1
#define _tlgDataDescCount_tlgSidctype, pValue, name, desc, tags, hasTags) + 1
#define _tlgDataDescCount_tlgStructfieldCount, inType, name, desc, tags, hasTags) + _tlgCheckStructFieldCountCppOnly(fieldCount)
#define _tlgDataDescCount_tlgTageventTag)
#define _tlgDataDescCreate_tlgChanneln, eventChannel)
#define _TlgDataDescCreate_TlgChanneln, data_type, cVals)
#define _TlgDataDescCreate_TlgCustAnnn, data_type, cVals)
#define _tlgDataDescCreate_tlgCustomAnnotn, key, value)
#define _tlgDataDescCreate_tlgDescn, eventDescription)
#define _TlgDataDescCreate_TlgDescn, data_type, cVals)
#define _TlgDataDescCreate_TlgEvtTagn, data_type, cVals)
#define _tlgDataDescCreate_tlgKeywordn, eventKeyword)
#define _TlgDataDescCreate_TlgKeywordn, data_type, cVals)
#define _tlgDataDescCreate_tlgLeveln, eventLevel)
#define _TlgDataDescCreate_TlgLeveln, data_type, cVals)
#define _tlgDataDescCreate_tlgOpcoden, eventOpcode)
#define _TlgDataDescCreate_TlgOpcoden, data_type, cVals)
#define _tlgDataDescCreate_tlgPackedMetan, inType, outType, hasOut, name, desc, tags, hasTags) _tlg_AssertValidPackedMetadataInType(inType);
#define _TlgDataDescCreate_TlgPMn, data_type, cVals)
#define _tlgDataDescCreate_tlgStructn, fieldCount, inType, name, desc, tags, hasTags) _tlg_CASSERT((fieldCount) < 128, "TraceLoggingStruct fieldCount must be less than 128.");
#define _TlgDataDescCreate_TlgStructn, data_type, cVals)
#define _tlgDataDescCreate_tlgTagn, eventTag)
#define _tlgDefineProvider_functionWrapperBegin0functionPostfix)
#define _TlgDefineProvider_functionWrapperBegin0functionPostfix)
#define _TlgDescVal_TlgAname, desc)
#define _tlgDescVal_tlgArrayctype, pValues, cValues, inType, outType, hasOut, name, desc, tags, hasTags)
#define _tlgDescVal_tlgAutovalue, name, desc, tags, hasTags)
#define _TlgDescVal_TlgAutoname, desc)
#define _TlgDescVal_TlgBname, desc)
#define _tlgDescVal_tlgBinaryctype, pValue, cbValue, inType, outType, hasOut, name, desc, tags, hasTags)
#define _tlgDescVal_tlgBufferctype, pValue, inType, outType, hasOut, name, desc, tags, hasTags)
#define _tlgDescVal_tlgChanneleventChannel)
#define _TlgDescVal_TlgChannelname, desc)
#define _tlgDescVal_tlgCountedctype, pchValue, cchValue, inType, outType, hasOut, name, desc, tags, hasTags)
#define _TlgDescVal_TlgCSname, desc)
#define _TlgDescVal_TlgCustAnnname, desc)
#define _tlgDescVal_tlgCustomctype, pValue, cbValue, protocol, bSchema, cbSchema, name, desc, tags, hasTags)
#define _TlgDescVal_TlgCustomname, desc)
#define _tlgDescVal_tlgCustomAnnotkey, value)
#define _tlgDescVal_tlgDesceventDescription) eventDescription
#define _TlgDescVal_TlgDescname, desc) desc
#define _TlgDescVal_TlgEvtTagname, desc)
#define _TlgDescVal_TlgFAname, desc)
#define _tlgDescVal_tlgFixedArrayctype, pValues, cValues, inType, outType, hasOut, name, desc, tags, hasTags)
#define _tlgDescVal_tlgKeywordeventKeyword)
#define _TlgDescVal_TlgKeywordname, desc)
#define _tlgDescVal_tlgLeveleventLevel)
#define _TlgDescVal_TlgLevelname, desc)
#define _tlgDescVal_tlgOpcodeeventOpcode)
#define _TlgDescVal_TlgOpcodename, desc)
#define _TlgDescVal_TlgPname, desc)
#define _tlgDescVal_tlgPackedDatactype, pValue, cbValue, dataDescType, hasType)
#define _tlgDescVal_tlgPackedFieldctype, pValue, cbValue, inType, outType, hasOut, name, desc, tags, hasTags)
#define _tlgDescVal_tlgPackedMetainType, outType, hasOut, name, desc, tags, hasTags)
#define _TlgDescVal_TlgPDname, desc)
#define _TlgDescVal_TlgPFname, desc)
#define _TlgDescVal_TlgPMname, desc)
#define _tlgDescVal_tlgPszctype, pszValue, inType, outType, hasOut, name, desc, tags, hasTags)
#define _TlgDescVal_TlgSname, desc)
#define _tlgDescVal_tlgScalarRefctype, value, inType, outType, hasOut, name, desc, tags, hasTags)
#define _tlgDescVal_tlgScalarValctype, value, inType, outType, hasOut, name, desc, tags, hasTags)
#define _tlgDescVal_tlgSidctype, pValue, name, desc, tags, hasTags)
#define _TlgDescVal_TlgSidname, desc)
#define _TlgDescVal_TlgStringname, desc)
#define _tlgDescVal_tlgStructfieldCount, inType, name, desc, tags, hasTags)
#define _TlgDescVal_TlgStructname, desc)
#define _TlgDescVal_TlgSzname, desc)
#define _tlgDescVal_tlgTageventTag)
#define _TlgDescVal_TlgWszname, desc)
#define _tlgDT_imp0name, ...) name,,, 0
#define _tlgDT_imp1name, desc) name, L"" desc,, 0
#define _tlgDT_imp2name, desc, tags) name, L"" desc, tags, 1
#define _tlgDT_impBmacro, args) macro args
#define _TlgEvtTagEncodeeventTag) (((eventTag) & 0xfe00000) >> 21) /* Encode 0x0fe00000 bits into UINT16  */ | (((eventTag) & 0x1fc000) >> 6) /* Encode 0x001fc000 bits into UINT16  */ | (((eventTag) & 0x3fff) << 16) /* Trigger warning for 0x00003fff bits */ | (~(~(eventTag) | 0x0FFFFFFF))
#define _TlgEvtTagVal_TlgAvalue)
#define _tlgEvtTagVal_tlgArrayctype, pValues, cValues, inType, outType, hasOut, name, desc, tags, hasTags)
#define _tlgEvtTagVal_tlgAutovalue, name, desc, tags, hasTags)
#define _TlgEvtTagVal_TlgAutovalue)
#define _TlgEvtTagVal_TlgBvalue)
#define _tlgEvtTagVal_tlgBinaryctype, pValue, cbValue, inType, outType, hasOut, name, desc, tags, hasTags)
#define _tlgEvtTagVal_tlgBufferctype, pValue, inType, outType, hasOut, name, desc, tags, hasTags)
#define _tlgEvtTagVal_tlgChanneleventChannel)
#define _TlgEvtTagVal_TlgChannelvalue)
#define _tlgEvtTagVal_tlgCountedctype, pchValue, cchValue, inType, outType, hasOut, name, desc, tags, hasTags)
#define _TlgEvtTagVal_TlgCSvalue)
#define _TlgEvtTagVal_TlgCustAnnvalue)
#define _tlgEvtTagVal_tlgCustomctype, pValue, cbValue, protocol, bSchema, cbSchema, name, desc, tags, hasTags)
#define _TlgEvtTagVal_TlgCustomvalue)
#define _tlgEvtTagVal_tlgCustomAnnotkey, value)
#define _tlgEvtTagVal_tlgDesceventDescription)
#define _TlgEvtTagVal_TlgDescvalue)
#define _TlgEvtTagVal_TlgEvtTagvalue) | value
#define _TlgEvtTagVal_TlgFAvalue)
#define _tlgEvtTagVal_tlgFixedArrayctype, pValues, cValues, inType, outType, hasOut, name, desc, tags, hasTags)
#define _tlgEvtTagVal_tlgKeywordeventKeyword)
#define _TlgEvtTagVal_TlgKeywordvalue)
#define _tlgEvtTagVal_tlgLeveleventLevel)
#define _TlgEvtTagVal_TlgLevelvalue)
#define _tlgEvtTagVal_tlgOpcodeeventOpcode)
#define _TlgEvtTagVal_TlgOpcodevalue)
#define _TlgEvtTagVal_TlgPvalue)
#define _tlgEvtTagVal_tlgPackedDatactype, pValue, cbValue, dataDescType, hasType)
#define _tlgEvtTagVal_tlgPackedFieldctype, pValue, cbValue, inType, outType, hasOut, name, desc, tags, hasTags)
#define _tlgEvtTagVal_tlgPackedMetainType, outType, hasOut, name, desc, tags, hasTags)
#define _TlgEvtTagVal_TlgPDvalue)
#define _TlgEvtTagVal_TlgPFvalue)
#define _TlgEvtTagVal_TlgPMvalue)
#define _tlgEvtTagVal_tlgPszctype, pszValue, inType, outType, hasOut, name, desc, tags, hasTags)
#define _TlgEvtTagVal_TlgSvalue)
#define _tlgEvtTagVal_tlgScalarRefctype, value, inType, outType, hasOut, name, desc, tags, hasTags)
#define _tlgEvtTagVal_tlgScalarValctype, value, inType, outType, hasOut, name, desc, tags, hasTags)
#define _tlgEvtTagVal_tlgSidctype, pValue, name, desc, tags, hasTags)
#define _TlgEvtTagVal_TlgSidvalue)
#define _TlgEvtTagVal_TlgStringvalue)
#define _tlgEvtTagVal_tlgStructfieldCount, inType, name, desc, tags, hasTags)
#define _TlgEvtTagVal_TlgSzvalue)
#define _tlgEvtTagVal_tlgTageventTag) | (eventTag)
#define _TlgEvtTagVal_TlgWszvalue)
#define _tlgExpandProviderIda, b, c, d, e, f, g, h, i, j, k) { a, b, c, { d, e, f, g, h, i, j, k } }
#define _tlgExpandType_imp1typeVal) (typeVal), 1
#define _tlgExpandType_impBmacro, args) macro args
#define _tlgGetLengthSidpSid) (8u + (pSid) -> SubAuthorityCount * 4u)
#define _tlgInfoVals_00outType, tags)
#define _TlgInfoVals_00out_type, tags)
#define _tlgInfoVals_01outType, tags) | 128, 128, _tlgInfoVals_Tags(tags)
#define _TlgInfoVals_01out_type, tags) | 128, 128, _TlgInfoVals_Tags(tags)
#define _tlgInfoVals_10outType, tags) | 128, outType
#define _TlgInfoVals_10out_type, tags) | 128, out_type
#define _tlgInfoVals_11outType, tags) | 128, outType | 128, _tlgInfoVals_Tags(tags)
#define _TlgInfoVals_11out_type, tags) | 128, out_type | 128, _TlgInfoVals_Tags(tags)
#define _tlgInfoVals_tlgChanneln, eventChannel)
#define _TlgInfoVals_TlgChannelin_type, out_type, has_out, cVals, name, has_tags, tags)
#define _TlgInfoVals_TlgCustAnnin_type, out_type, has_out, cVals, name, has_tags, tags)
#define _tlgInfoVals_tlgCustomn, ctype, pValue, cbValue, protocol, bSchema, cbSchema, name, desc, tags, hasTags) , (name), ((protocol) & 31) | 96 _tlgInfoVals_0 ## hasTags(0, tags), cbSchema, { _tlg_FLATTEN bSchema }
#define _TlgInfoVals_TlgCustomin_type, out_type, has_out, cVals, name, has_tags, tags) , (name), ((in_type) & 31) | 96 _TlgInfoVals_0 ## has_tags(0, tags), has_out, { _TLG_FLATTEN out_type }
#define _tlgInfoVals_tlgCustomAnnotn, key, value)
#define _tlgInfoVals_tlgDescn, eventDescription)
#define _TlgInfoVals_TlgDescin_type, out_type, has_out, cVals, name, has_tags, tags)
#define _TlgInfoVals_TlgEvtTagin_type, out_type, has_out, cVals, name, has_tags, tags)
#define _tlgInfoVals_tlgKeywordn, eventKeyword)
#define _TlgInfoVals_TlgKeywordin_type, out_type, has_out, cVals, name, has_tags, tags)
#define _tlgInfoVals_tlgLeveln, eventLevel)
#define _TlgInfoVals_TlgLevelin_type, out_type, has_out, cVals, name, has_tags, tags)
#define _tlgInfoVals_tlgOpcoden, eventOpcode)
#define _TlgInfoVals_TlgOpcodein_type, out_type, has_out, cVals, name, has_tags, tags)
#define _tlgInfoVals_tlgPackedDatan, ctype, pValue, cbValue, dataDescType, hasType)
#define _TlgInfoVals_TlgPDin_type, out_type, has_out, cVals, name, has_tags, tags)
#define _tlgInfoVals_tlgSidn, ctype, pValue, name, desc, tags, hasTags) , (name), TlgInSID _tlgInfoVals_0 ## hasTags(outType, tags)
#define _tlgInfoVals_tlgStructn, fieldCount, inType, name, desc, tags, hasTags) , (name), inType _tlgInfoVals_1 ## hasTags(fieldCount, tags)
#define _TlgInfoVals_TlgStructin_type, out_type, has_out, cVals, name, has_tags, tags) , (name), in_type _TlgInfoVals_1 ## has_tags(cVals, tags)
#define _tlgInfoVals_tlgTagn, eventTag)
#define _tlgInfoVars_0n)
#define _TlgInfoVars_0n)
#define _tlgInfoVars_00n)
#define _TlgInfoVars_00n)
#define _tlgInfoVars_tlgChanneln, eventChannel)
#define _tlgInfoVars_tlgCustomAnnotn, key, value)
#define _tlgInfoVars_tlgDescn, eventDescription)
#define _tlgInfoVars_tlgKeywordn, eventKeyword)
#define _tlgInfoVars_tlgLeveln, eventLevel)
#define _tlgInfoVars_tlgOpcoden, eventOpcode)
#define _tlgInfoVars_tlgPackedDatan, ctype, pValue, cbValue, dataDescType, hasType)
#define _TlgInfoVars_TlgPDn, name, in_type, has_out, has_tags)
#define _tlgInfoVars_tlgTagn, eventTag)
#define _tlgKeywordConstn) n
#define _TlgKeywordVal_TlgAvalue)
#define _tlgKeywordVal_tlgArrayctype, pValues, cValues, inType, outType, hasOut, name, desc, tags, hasTags)
#define _tlgKeywordVal_tlgAutovalue, name, desc, tags, hasTags)
#define _TlgKeywordVal_TlgAutovalue)
#define _TlgKeywordVal_TlgBvalue)
#define _tlgKeywordVal_tlgBinaryctype, pValue, cbValue, inType, outType, hasOut, name, desc, tags, hasTags)
#define _tlgKeywordVal_tlgBufferctype, pValue, inType, outType, hasOut, name, desc, tags, hasTags)
#define _tlgKeywordVal_tlgChanneleventChannel)
#define _TlgKeywordVal_TlgChannelvalue)
#define _tlgKeywordVal_tlgCountedctype, pchValue, cchValue, inType, outType, hasOut, name, desc, tags, hasTags)
#define _TlgKeywordVal_TlgCSvalue)
#define _TlgKeywordVal_TlgCustAnnvalue)
#define _tlgKeywordVal_tlgCustomctype, pValue, cbValue, protocol, bSchema, cbSchema, name, desc, tags, hasTags)
#define _TlgKeywordVal_TlgCustomvalue)
#define _tlgKeywordVal_tlgCustomAnnotkey, value)
#define _tlgKeywordVal_tlgDesceventDescription)
#define _TlgKeywordVal_TlgDescvalue)
#define _TlgKeywordVal_TlgEvtTagvalue)
#define _TlgKeywordVal_TlgFAvalue)
#define _tlgKeywordVal_tlgFixedArrayctype, pValues, cValues, inType, outType, hasOut, name, desc, tags, hasTags)
#define _tlgKeywordVal_tlgKeywordeventKeyword) | (eventKeyword)
#define _TlgKeywordVal_TlgKeywordvalue) | _TlgConst(Keyword, value)
#define _tlgKeywordVal_tlgLeveleventLevel)
#define _TlgKeywordVal_TlgLevelvalue)
#define _tlgKeywordVal_tlgOpcodeeventOpcode)
#define _TlgKeywordVal_TlgOpcodevalue)
#define _TlgKeywordVal_TlgPvalue)
#define _tlgKeywordVal_tlgPackedDatactype, pValue, cbValue, dataDescType, hasType)
#define _tlgKeywordVal_tlgPackedFieldctype, pValue, cbValue, inType, outType, hasOut, name, desc, tags, hasTags)
#define _tlgKeywordVal_tlgPackedMetainType, outType, hasOut, name, desc, tags, hasTags)
#define _TlgKeywordVal_TlgPDvalue)
#define _TlgKeywordVal_TlgPFvalue)
#define _TlgKeywordVal_TlgPMvalue)
#define _tlgKeywordVal_tlgPszctype, pszValue, inType, outType, hasOut, name, desc, tags, hasTags)
#define _TlgKeywordVal_TlgSvalue)
#define _tlgKeywordVal_tlgScalarRefctype, value, inType, outType, hasOut, name, desc, tags, hasTags)
#define _tlgKeywordVal_tlgScalarValctype, value, inType, outType, hasOut, name, desc, tags, hasTags)
#define _tlgKeywordVal_tlgSidctype, pValue, name, desc, tags, hasTags)
#define _TlgKeywordVal_TlgSidvalue)
#define _TlgKeywordVal_TlgStringvalue)
#define _tlgKeywordVal_tlgStructfieldCount, inType, name, desc, tags, hasTags)
#define _TlgKeywordVal_TlgSzvalue)
#define _tlgKeywordVal_tlgTageventTag)
#define _TlgKeywordVal_TlgWszvalue)
#define _TlgLevelVal_TlgAvalue)
#define _tlgLevelVal_tlgArrayctype, pValues, cValues, inType, outType, hasOut, name, desc, tags, hasTags)
#define _tlgLevelVal_tlgAutovalue, name, desc, tags, hasTags)
#define _TlgLevelVal_TlgAutovalue)
#define _TlgLevelVal_TlgBvalue)
#define _tlgLevelVal_tlgBinaryctype, pValue, cbValue, inType, outType, hasOut, name, desc, tags, hasTags)
#define _tlgLevelVal_tlgBufferctype, pValue, inType, outType, hasOut, name, desc, tags, hasTags)
#define _tlgLevelVal_tlgChanneleventChannel)
#define _TlgLevelVal_TlgChannelvalue)
#define _tlgLevelVal_tlgCountedctype, pchValue, cchValue, inType, outType, hasOut, name, desc, tags, hasTags)
#define _TlgLevelVal_TlgCSvalue)
#define _TlgLevelVal_TlgCustAnnvalue)
#define _tlgLevelVal_tlgCustomctype, pValue, cbValue, protocol, bSchema, cbSchema, name, desc, tags, hasTags)
#define _TlgLevelVal_TlgCustomvalue)
#define _tlgLevelVal_tlgCustomAnnotkey, value)
#define _tlgLevelVal_tlgDesceventDescription)
#define _TlgLevelVal_TlgDescvalue)
#define _TlgLevelVal_TlgEvtTagvalue)
#define _TlgLevelVal_TlgFAvalue)
#define _tlgLevelVal_tlgFixedArrayctype, pValues, cValues, inType, outType, hasOut, name, desc, tags, hasTags)
#define _tlgLevelVal_tlgKeywordeventKeyword)
#define _TlgLevelVal_TlgKeywordvalue)
#define _tlgLevelVal_tlgLeveleventLevel) & 0 | (eventLevel)
#define _TlgLevelVal_TlgLevelvalue) & 0 | _TlgConst(Level, value)
#define _tlgLevelVal_tlgOpcodeeventOpcode)
#define _TlgLevelVal_TlgOpcodevalue)
#define _TlgLevelVal_TlgPvalue)
#define _tlgLevelVal_tlgPackedDatactype, pValue, cbValue, dataDescType, hasType)
#define _tlgLevelVal_tlgPackedFieldctype, pValue, cbValue, inType, outType, hasOut, name, desc, tags, hasTags)
#define _tlgLevelVal_tlgPackedMetainType, outType, hasOut, name, desc, tags, hasTags)
#define _TlgLevelVal_TlgPDvalue)
#define _TlgLevelVal_TlgPFvalue)
#define _TlgLevelVal_TlgPMvalue)
#define _tlgLevelVal_tlgPszctype, pszValue, inType, outType, hasOut, name, desc, tags, hasTags)
#define _TlgLevelVal_TlgSvalue)
#define _tlgLevelVal_tlgScalarRefctype, value, inType, outType, hasOut, name, desc, tags, hasTags)
#define _tlgLevelVal_tlgScalarValctype, value, inType, outType, hasOut, name, desc, tags, hasTags)
#define _tlgLevelVal_tlgSidctype, pValue, name, desc, tags, hasTags)
#define _TlgLevelVal_TlgSidvalue)
#define _TlgLevelVal_TlgStringvalue)
#define _tlgLevelVal_tlgStructfieldCount, inType, name, desc, tags, hasTags)
#define _TlgLevelVal_TlgSzvalue)
#define _tlgLevelVal_tlgTageventTag)
#define _TlgLevelVal_TlgWszvalue)
#define _tlgNDT_imp0value, ...) # value,,, 0
#define _tlgNDT_imp1value, name) name,,, 0
#define _tlgNDT_imp2value, name, desc) name, L"" desc,, 0
#define _tlgNDT_imp3value, name, desc, tags) name, L"" desc, tags, 1
#define _tlgNDT_impBmacro, args) macro args
#define _TlgOpcodeVal_TlgAvalue)
#define _tlgOpcodeVal_tlgArrayctype, pValues, cValues, inType, outType, hasOut, name, desc, tags, hasTags)
#define _tlgOpcodeVal_tlgAutovalue, name, desc, tags, hasTags)
#define _TlgOpcodeVal_TlgAutovalue)
#define _TlgOpcodeVal_TlgBvalue)
#define _tlgOpcodeVal_tlgBinaryctype, pValue, cbValue, inType, outType, hasOut, name, desc, tags, hasTags)
#define _tlgOpcodeVal_tlgBufferctype, pValue, inType, outType, hasOut, name, desc, tags, hasTags)
#define _tlgOpcodeVal_tlgChanneleventChannel)
#define _TlgOpcodeVal_TlgChannelvalue)
#define _tlgOpcodeVal_tlgCountedctype, pchValue, cchValue, inType, outType, hasOut, name, desc, tags, hasTags)
#define _TlgOpcodeVal_TlgCSvalue)
#define _TlgOpcodeVal_TlgCustAnnvalue)
#define _tlgOpcodeVal_tlgCustomctype, pValue, cbValue, protocol, bSchema, cbSchema, name, desc, tags, hasTags)
#define _TlgOpcodeVal_TlgCustomvalue)
#define _tlgOpcodeVal_tlgCustomAnnotkey, value)
#define _tlgOpcodeVal_tlgDesceventDescription)
#define _TlgOpcodeVal_TlgDescvalue)
#define _TlgOpcodeVal_TlgEvtTagvalue)
#define _TlgOpcodeVal_TlgFAvalue)
#define _tlgOpcodeVal_tlgFixedArrayctype, pValues, cValues, inType, outType, hasOut, name, desc, tags, hasTags)
#define _tlgOpcodeVal_tlgKeywordeventKeyword)
#define _TlgOpcodeVal_TlgKeywordvalue)
#define _tlgOpcodeVal_tlgLeveleventLevel)
#define _TlgOpcodeVal_TlgLevelvalue)
#define _tlgOpcodeVal_tlgOpcodeeventOpcode) & 0 | (eventOpcode)
#define _TlgOpcodeVal_TlgOpcodevalue) & 0 | _TlgConst(Opcode, value)
#define _TlgOpcodeVal_TlgPvalue)
#define _tlgOpcodeVal_tlgPackedDatactype, pValue, cbValue, dataDescType, hasType)
#define _tlgOpcodeVal_tlgPackedFieldctype, pValue, cbValue, inType, outType, hasOut, name, desc, tags, hasTags)
#define _tlgOpcodeVal_tlgPackedMetainType, outType, hasOut, name, desc, tags, hasTags)
#define _TlgOpcodeVal_TlgPDvalue)
#define _TlgOpcodeVal_TlgPFvalue)
#define _TlgOpcodeVal_TlgPMvalue)
#define _tlgOpcodeVal_tlgPszctype, pszValue, inType, outType, hasOut, name, desc, tags, hasTags)
#define _TlgOpcodeVal_TlgSvalue)
#define _tlgOpcodeVal_tlgScalarRefctype, value, inType, outType, hasOut, name, desc, tags, hasTags)
#define _tlgOpcodeVal_tlgScalarValctype, value, inType, outType, hasOut, name, desc, tags, hasTags)
#define _tlgOpcodeVal_tlgSidctype, pValue, name, desc, tags, hasTags)
#define _TlgOpcodeVal_TlgSidvalue)
#define _TlgOpcodeVal_TlgStringvalue)
#define _tlgOpcodeVal_tlgStructfieldCount, inType, name, desc, tags, hasTags)
#define _TlgOpcodeVal_TlgSzvalue)
#define _tlgOpcodeVal_tlgTageventTag)
#define _TlgOpcodeVal_TlgWszvalue)
#define _tlgProvVal_tlgOptiontraitType, ctype, value) , 3 + sizeof (ctype), traitType, _tlg_FLATTEN value
#define _TlgSegMetadataEvents 0
#define _TlgTempVars_TlgChanneln, data_type, in_type, value, cVals)
#define _TlgTempVars_TlgCustAnnn, data_type, in_type, value, cVals)
#define _TlgTempVars_TlgDescn, data_type, in_type, value, cVals)
#define _TlgTempVars_TlgEvtTagn, data_type, in_type, value, cVals)
#define _TlgTempVars_TlgKeywordn, data_type, in_type, value, cVals)
#define _TlgTempVars_TlgLeveln, data_type, in_type, value, cVals)
#define _TlgTempVars_TlgOpcoden, data_type, in_type, value, cVals)
#define _TlgTempVars_TlgPMn, data_type, in_type, value, cVals) _TLG_AssertValidPackedMetadataInType(in_type);
#define _TlgTempVars_TlgStructn, data_type, in_type, value, cVals) _TLG_CASSERT(cVals < 128, "Struct size must be less than 128");
#define _tlgValidateProviderId_parenthesisCALLmacro, args) macro args
#define _tlgValidateProviderId_presentCALLmacro, args) macro args
#define TraceLoggingSetInformation 0

#define _tlg_ASSERTexp, str) ((void) (!(exp) ? (__annotation(L"Debug", L"AssertFail", L"TraceLogging: " _tlg_LSTRINGIZE(exp) L" : " str), TLG_RAISEASSERTIONFAILURE(), 0) : 0))
#define _TLG_ASSERTexp, str) ((void) (!(exp) ? (__annotation(L"Debug", L"AssertFail", L"TraceLogging: " L ## # exp L" : " L ## str), DbgRaiseAssertionFailure(), 0) : 0))
#define _tlg_AssertValidPackedMetadataInTypeCppOnlyinType)
#define _TLG_AssertValidPackedMetadataInTypeCppOnlyin_type)
#define _tlg_CATa, ...) _tlg_CAT_imp(a, __VA_ARGS__)
#define _TLG_CATa, ...) _TLG_CAT_imp(a, __VA_ARGS__)
#define _tlg_FASTFAILcode) __fastfail(code)
#define _tlg_FOR_imp0f, ...)
#define _TLG_FOR_imp0fn)
#define _tlg_FOR_imp1f, a0) f(0, a0)
#define _TLG_FOR_imp1fn, arg, ...) _TLG_FOR_impN(0, fn, arg, fn, __VA_ARGS__)
#define _tlg_FOR_imp10f, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9) f(0, a0) f(1, a1) f(2, a2) f(3, a3) f(4, a4) f(5, a5) f(6, a6) f(7, a7) f(8, a8) f(9, a9)
#define _TLG_FOR_imp10fn, arg, ...) _TLG_FOR_impN(9, fn, arg, fn, __VA_ARGS__)
#define _tlg_FOR_imp11f, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10) f(0, a0) f(1, a1) f(2, a2) f(3, a3) f(4, a4) f(5, a5) f(6, a6) f(7, a7) f(8, a8) f(9, a9) f(10, a10)
#define _TLG_FOR_imp11fn, arg, ...) _TLG_FOR_impN(10, fn, arg, fn, __VA_ARGS__)
#define _tlg_FOR_imp12f, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11) f(0, a0) f(1, a1) f(2, a2) f(3, a3) f(4, a4) f(5, a5) f(6, a6) f(7, a7) f(8, a8) f(9, a9) f(10, a10) f(11, a11)
#define _TLG_FOR_imp12fn, arg, ...) _TLG_FOR_impN(11, fn, arg, fn, __VA_ARGS__)
#define _tlg_FOR_imp13f, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12) f(0, a0) f(1, a1) f(2, a2) f(3, a3) f(4, a4) f(5, a5) f(6, a6) f(7, a7) f(8, a8) f(9, a9) f(10, a10) f(11, a11) f(12, a12)
#define _TLG_FOR_imp13fn, arg, ...) _TLG_FOR_impN(12, fn, arg, fn, __VA_ARGS__)
#define _tlg_FOR_imp14f, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13) f(0, a0) f(1, a1) f(2, a2) f(3, a3) f(4, a4) f(5, a5) f(6, a6) f(7, a7) f(8, a8) f(9, a9) f(10, a10) f(11, a11) f(12, a12) f(13, a13)
#define _TLG_FOR_imp14fn, arg, ...) _TLG_FOR_impN(13, fn, arg, fn, __VA_ARGS__)
#define _tlg_FOR_imp15f, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14) f(0, a0) f(1, a1) f(2, a2) f(3, a3) f(4, a4) f(5, a5) f(6, a6) f(7, a7) f(8, a8) f(9, a9) f(10, a10) f(11, a11) f(12, a12) f(13, a13) f(14, a14)
#define _TLG_FOR_imp15fn, arg, ...) _TLG_FOR_impN(14, fn, arg, fn, __VA_ARGS__)
#define _tlg_FOR_imp16f, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15) f(0, a0) f(1, a1) f(2, a2) f(3, a3) f(4, a4) f(5, a5) f(6, a6) f(7, a7) f(8, a8) f(9, a9) f(10, a10) f(11, a11) f(12, a12) f(13, a13) f(14, a14) f(15, a15)
#define _TLG_FOR_imp16fn, arg, ...) _TLG_FOR_impN(15, fn, arg, fn, __VA_ARGS__)
#define _tlg_FOR_imp17f, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16) f(0, a0) f(1, a1) f(2, a2) f(3, a3) f(4, a4) f(5, a5) f(6, a6) f(7, a7) f(8, a8) f(9, a9) f(10, a10) f(11, a11) f(12, a12) f(13, a13) f(14, a14) f(15, a15) f(16, a16)
#define _TLG_FOR_imp17fn, arg, ...) _TLG_FOR_impN(16, fn, arg, fn, __VA_ARGS__)
#define _tlg_FOR_imp18f, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17) f(0, a0) f(1, a1) f(2, a2) f(3, a3) f(4, a4) f(5, a5) f(6, a6) f(7, a7) f(8, a8) f(9, a9) f(10, a10) f(11, a11) f(12, a12) f(13, a13) f(14, a14) f(15, a15) f(16, a16) f(17, a17)
#define _TLG_FOR_imp18fn, arg, ...) _TLG_FOR_impN(17, fn, arg, fn, __VA_ARGS__)
#define _tlg_FOR_imp19f, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17, a18) f(0, a0) f(1, a1) f(2, a2) f(3, a3) f(4, a4) f(5, a5) f(6, a6) f(7, a7) f(8, a8) f(9, a9) f(10, a10) f(11, a11) f(12, a12) f(13, a13) f(14, a14) f(15, a15) f(16, a16) f(17, a17) f(18, a18)
#define _TLG_FOR_imp19fn, arg, ...) _TLG_FOR_impN(18, fn, arg, fn, __VA_ARGS__)
#define _tlg_FOR_imp2f, a0, a1) f(0, a0) f(1, a1)
#define _TLG_FOR_imp2fn, arg, ...) _TLG_FOR_impN(1, fn, arg, fn, __VA_ARGS__)
#define _tlg_FOR_imp20f, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17, a18, a19) f(0, a0) f(1, a1) f(2, a2) f(3, a3) f(4, a4) f(5, a5) f(6, a6) f(7, a7) f(8, a8) f(9, a9) f(10, a10) f(11, a11) f(12, a12) f(13, a13) f(14, a14) f(15, a15) f(16, a16) f(17, a17) f(18, a18) f(19, a19)
#define _TLG_FOR_imp20fn, arg, ...) _TLG_FOR_impN(19, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp21fn, arg, ...) _TLG_FOR_impN(20, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp22fn, arg, ...) _TLG_FOR_impN(21, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp23fn, arg, ...) _TLG_FOR_impN(22, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp24fn, arg, ...) _TLG_FOR_impN(23, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp25fn, arg, ...) _TLG_FOR_impN(24, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp26fn, arg, ...) _TLG_FOR_impN(25, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp27fn, arg, ...) _TLG_FOR_impN(26, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp28fn, arg, ...) _TLG_FOR_impN(27, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp29fn, arg, ...) _TLG_FOR_impN(28, fn, arg, fn, __VA_ARGS__)
#define _tlg_FOR_imp3f, a0, a1, a2) f(0, a0) f(1, a1) f(2, a2)
#define _TLG_FOR_imp3fn, arg, ...) _TLG_FOR_impN(2, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp30fn, arg, ...) _TLG_FOR_impN(29, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp31fn, arg, ...) _TLG_FOR_impN(30, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp32fn, arg, ...) _TLG_FOR_impN(31, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp33fn, arg, ...) _TLG_FOR_impN(32, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp34fn, arg, ...) _TLG_FOR_impN(33, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp35fn, arg, ...) _TLG_FOR_impN(34, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp36fn, arg, ...) _TLG_FOR_impN(35, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp37fn, arg, ...) _TLG_FOR_impN(36, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp38fn, arg, ...) _TLG_FOR_impN(37, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp39fn, arg, ...) _TLG_FOR_impN(38, fn, arg, fn, __VA_ARGS__)
#define _tlg_FOR_imp4f, a0, a1, a2, a3) f(0, a0) f(1, a1) f(2, a2) f(3, a3)
#define _TLG_FOR_imp4fn, arg, ...) _TLG_FOR_impN(3, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp40fn, arg, ...) _TLG_FOR_impN(39, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp41fn, arg, ...) _TLG_FOR_impN(40, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp42fn, arg, ...) _TLG_FOR_impN(41, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp43fn, arg, ...) _TLG_FOR_impN(42, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp44fn, arg, ...) _TLG_FOR_impN(43, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp45fn, arg, ...) _TLG_FOR_impN(44, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp46fn, arg, ...) _TLG_FOR_impN(45, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp47fn, arg, ...) _TLG_FOR_impN(46, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp48fn, arg, ...) _TLG_FOR_impN(47, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp49fn, arg, ...) _TLG_FOR_impN(48, fn, arg, fn, __VA_ARGS__)
#define _tlg_FOR_imp5f, a0, a1, a2, a3, a4) f(0, a0) f(1, a1) f(2, a2) f(3, a3) f(4, a4)
#define _TLG_FOR_imp5fn, arg, ...) _TLG_FOR_impN(4, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp50fn, arg, ...) _TLG_FOR_impN(49, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp51fn, arg, ...) _TLG_FOR_impN(50, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp52fn, arg, ...) _TLG_FOR_impN(51, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp53fn, arg, ...) _TLG_FOR_impN(52, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp54fn, arg, ...) _TLG_FOR_impN(53, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp55fn, arg, ...) _TLG_FOR_impN(54, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp56fn, arg, ...) _TLG_FOR_impN(55, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp57fn, arg, ...) _TLG_FOR_impN(56, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp58fn, arg, ...) _TLG_FOR_impN(57, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp59fn, arg, ...) _TLG_FOR_impN(58, fn, arg, fn, __VA_ARGS__)
#define _tlg_FOR_imp6f, a0, a1, a2, a3, a4, a5) f(0, a0) f(1, a1) f(2, a2) f(3, a3) f(4, a4) f(5, a5)
#define _TLG_FOR_imp6fn, arg, ...) _TLG_FOR_impN(5, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp60fn, arg, ...) _TLG_FOR_impN(59, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp61fn, arg, ...) _TLG_FOR_impN(60, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp62fn, arg, ...) _TLG_FOR_impN(61, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp63fn, arg, ...) _TLG_FOR_impN(62, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp64fn, arg, ...) _TLG_FOR_impN(63, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp65fn, arg, ...) _TLG_FOR_impN(64, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp66fn, arg, ...) _TLG_FOR_impN(65, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp67fn, arg, ...) _TLG_FOR_impN(66, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp68fn, arg, ...) _TLG_FOR_impN(67, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp69fn, arg, ...) _TLG_FOR_impN(68, fn, arg, fn, __VA_ARGS__)
#define _tlg_FOR_imp7f, a0, a1, a2, a3, a4, a5, a6) f(0, a0) f(1, a1) f(2, a2) f(3, a3) f(4, a4) f(5, a5) f(6, a6)
#define _TLG_FOR_imp7fn, arg, ...) _TLG_FOR_impN(6, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp70fn, arg, ...) _TLG_FOR_impN(69, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp71fn, arg, ...) _TLG_FOR_impN(70, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp72fn, arg, ...) _TLG_FOR_impN(71, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp73fn, arg, ...) _TLG_FOR_impN(72, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp74fn, arg, ...) _TLG_FOR_impN(73, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp75fn, arg, ...) _TLG_FOR_impN(74, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp76fn, arg, ...) _TLG_FOR_impN(75, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp77fn, arg, ...) _TLG_FOR_impN(76, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp78fn, arg, ...) _TLG_FOR_impN(77, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp79fn, arg, ...) _TLG_FOR_impN(78, fn, arg, fn, __VA_ARGS__)
#define _tlg_FOR_imp8f, a0, a1, a2, a3, a4, a5, a6, a7) f(0, a0) f(1, a1) f(2, a2) f(3, a3) f(4, a4) f(5, a5) f(6, a6) f(7, a7)
#define _TLG_FOR_imp8fn, arg, ...) _TLG_FOR_impN(7, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp80fn, arg, ...) _TLG_FOR_impN(79, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp81fn, arg, ...) _TLG_FOR_impN(80, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp82fn, arg, ...) _TLG_FOR_impN(81, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp83fn, arg, ...) _TLG_FOR_impN(82, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp84fn, arg, ...) _TLG_FOR_impN(83, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp85fn, arg, ...) _TLG_FOR_impN(84, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp86fn, arg, ...) _TLG_FOR_impN(85, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp87fn, arg, ...) _TLG_FOR_impN(86, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp88fn, arg, ...) _TLG_FOR_impN(87, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp89fn, arg, ...) _TLG_FOR_impN(88, fn, arg, fn, __VA_ARGS__)
#define _tlg_FOR_imp9f, a0, a1, a2, a3, a4, a5, a6, a7, a8) f(0, a0) f(1, a1) f(2, a2) f(3, a3) f(4, a4) f(5, a5) f(6, a6) f(7, a7) f(8, a8)
#define _TLG_FOR_imp9fn, arg, ...) _TLG_FOR_impN(8, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp90fn, arg, ...) _TLG_FOR_impN(89, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp91fn, arg, ...) _TLG_FOR_impN(90, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp92fn, arg, ...) _TLG_FOR_impN(91, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp93fn, arg, ...) _TLG_FOR_impN(92, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp94fn, arg, ...) _TLG_FOR_impN(93, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp95fn, arg, ...) _TLG_FOR_impN(94, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp96fn, arg, ...) _TLG_FOR_impN(95, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp97fn, arg, ...) _TLG_FOR_impN(96, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp98fn, arg, ...) _TLG_FOR_impN(97, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp99fn, arg, ...) _TLG_FOR_impN(98, fn, arg, fn, __VA_ARGS__)
#define _tlg_NARGS_imp2a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17, a18, a19, a20, a21, a22, a23, a24, a25, a26, a27, a28, a29, a30, a31, a32, a33, a34, a35, a36, a37, a38, a39, a40, a41, a42, a43, a44, a45, a46, a47, a48, a49, a50, a51, a52, a53, a54, a55, a56, a57, a58, a59, a60, a61, a62, a63, a64, a65, a66, a67, a68, a69, a70, a71, a72, a73, a74, a75, a76, a77, a78, a79, a80, a81, a82, a83, a84, a85, a86, a87, a88, a89, a90, a91, a92, a93, a94, a95, a96, a97, a98, a99, size, ...) size
#define _TLG_NARGS_imp2a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17, a18, a19, a20, a21, a22, a23, a24, a25, a26, a27, a28, a29, a30, a31, a32, a33, a34, a35, a36, a37, a38, a39, a40, a41, a42, a43, a44, a45, a46, a47, a48, a49, a50, a51, a52, a53, a54, a55, a56, a57, a58, a59, a60, a61, a62, a63, a64, a65, a66, a67, a68, a69, a70, a71, a72, a73, a74, a75, a76, a77, a78, a79, a80, a81, a82, a83, a84, a85, a86, a87, a88, a89, a90, a91, a92, a93, a94, a95, a96, a97, a98, a99, size, ...) size
#define _tlg_PASTE3a, b, c) _tlg_PASTE3_imp(a, b, c)
#define _tlg_SPLITcond, ...) _tlg_SPLIT_imp(cond, (__VA_ARGS__))
#define _TLG_SPLITcond, ...) _TLG_SPLIT_imp(cond, (__VA_ARGS__))
#define _tlg_SPLIT_imp0false_val, ...) false_val
#define _TLG_SPLIT_imp0false_val, ...) false_val
#define _tlg_SPLIT_imp1false_val, ...) __VA_ARGS__
#define _TLG_SPLIT_imp1false_val, ...) __VA_ARGS__
#define _tlg_STRINGIZEx) _tlg_STRINGIZE_imp(x)
#define _TLG_STRINGIZEx) _TLG_STRINGIZE_imp(x)
#define _tlgAnnotationFunc_imp0storageVariable)
#define _TlgAnnotationFunc_imp0storageVariable) 0
#define _TlgAnnotes_TlgAname, desc) L"|" L ## # name L"=" desc
#define _tlgAnnotes_tlgArrayctype, pValues, cValues, inType, outType, hasOut, name, desc, tags, hasTags) L"|" _tlg_LSTRINGIZE(name) L"=" desc
#define _tlgAnnotes_tlgAutovalue, name, desc, tags, hasTags) L"|" _tlg_LSTRINGIZE(name) L"=" desc
#define _TlgAnnotes_TlgAutoname, desc) L"|" L ## # name L"=" desc
#define _TlgAnnotes_TlgBname, desc) L"|" L ## # name L"=" desc
#define _tlgAnnotes_tlgBinaryctype, pValue, cbValue, inType, outType, hasOut, name, desc, tags, hasTags) L"|" _tlg_LSTRINGIZE(name) L"=" desc
#define _tlgAnnotes_tlgBufferctype, pValue, inType, outType, hasOut, name, desc, tags, hasTags) L"|" _tlg_LSTRINGIZE(name) L"=" desc
#define _tlgAnnotes_tlgChanneleventChannel)
#define _TlgAnnotes_TlgChannelname, desc)
#define _tlgAnnotes_tlgCountedctype, pchValue, cchValue, inType, outType, hasOut, name, desc, tags, hasTags) L"|" _tlg_LSTRINGIZE(name) L"=" desc
#define _TlgAnnotes_TlgCSname, desc) L"|" L ## # name L"=" desc
#define _TlgAnnotes_TlgCustAnnname, desc) L"|" L ## name L"=" desc
#define _tlgAnnotes_tlgCustomctype, pValue, cbValue, protocol, bSchema, cbSchema, name, desc, tags, hasTags) L"|" _tlg_LSTRINGIZE(name) L"=" desc
#define _TlgAnnotes_TlgCustomname, desc) L"|" L ## # name L"=" desc
#define _tlgAnnotes_tlgCustomAnnotkey, value) L"|" key L"=" value
#define _tlgAnnotes_tlgDesceventDescription)
#define _TlgAnnotes_TlgDescname, desc)
#define _TlgAnnotes_TlgEvtTagname, desc)
#define _TlgAnnotes_TlgFAname, desc) L"|" L ## # name L"=" desc
#define _tlgAnnotes_tlgFixedArrayctype, pValues, cValues, inType, outType, hasOut, name, desc, tags, hasTags) L"|" _tlg_LSTRINGIZE(name) L"=" desc
#define _tlgAnnotes_tlgKeywordeventKeyword)
#define _TlgAnnotes_TlgKeywordname, desc)
#define _tlgAnnotes_tlgLeveleventLevel)
#define _TlgAnnotes_TlgLevelname, desc)
#define _tlgAnnotes_tlgOpcodeeventOpcode)
#define _TlgAnnotes_TlgOpcodename, desc)
#define _TlgAnnotes_TlgPname, desc) L"|" L ## # name L"=" desc
#define _tlgAnnotes_tlgPackedDatactype, pValue, cbValue, dataDescType, hasType)
#define _tlgAnnotes_tlgPackedFieldctype, pValue, cbValue, inType, outType, hasOut, name, desc, tags, hasTags) L"|" _tlg_LSTRINGIZE(name) L"=" desc
#define _tlgAnnotes_tlgPackedMetainType, outType, hasOut, name, desc, tags, hasTags) L"|" _tlg_LSTRINGIZE(name) L"=" desc
#define _TlgAnnotes_TlgPDname, desc)
#define _TlgAnnotes_TlgPFname, desc) L"|" L ## # name L"=" desc
#define _TlgAnnotes_TlgPMname, desc) L"|" L ## # name L"=" desc
#define _tlgAnnotes_tlgPszctype, pszValue, inType, outType, hasOut, name, desc, tags, hasTags) L"|" _tlg_LSTRINGIZE(name) L"=" desc
#define _TlgAnnotes_TlgSname, desc) L"|" L ## # name L"=" desc
#define _tlgAnnotes_tlgScalarRefctype, value, inType, outType, hasOut, name, desc, tags, hasTags) L"|" _tlg_LSTRINGIZE(name) L"=" desc
#define _tlgAnnotes_tlgScalarValctype, value, inType, outType, hasOut, name, desc, tags, hasTags) L"|" _tlg_LSTRINGIZE(name) L"=" desc
#define _tlgAnnotes_tlgSidctype, pValue, name, desc, tags, hasTags) L"|" _tlg_LSTRINGIZE(name) L"=" desc
#define _TlgAnnotes_TlgSidname, desc) L"|" L ## # name L"=" desc
#define _TlgAnnotes_TlgStringname, desc) L"|" L ## # name L"=" desc
#define _tlgAnnotes_tlgStructfieldCount, inType, name, desc, tags, hasTags) L"|" _tlg_LSTRINGIZE(name) L"=" desc
#define _TlgAnnotes_TlgStructname, desc) L"|" L ## # name L"=" desc
#define _TlgAnnotes_TlgSzname, desc) L"|" L ## # name L"=" desc
#define _tlgAnnotes_tlgTageventTag)
#define _TlgAnnotes_TlgWszname, desc) L"|" L ## # name L"=" desc
#define _TlgArg_imp0data_type, in_type, out_type, has_out, dim, value, cVals, x) (data_type, in_type, out_type, has_out, dim, value, cVals, # value,,, 0)
#define _TlgArg_imp1data_type, in_type, out_type, has_out, dim, value, cVals, name) (data_type, in_type, out_type, has_out, dim, value, cVals, name,,, 0)
#define _TlgArg_imp2data_type, in_type, out_type, has_out, dim, value, cVals, name, desc) (data_type, in_type, out_type, has_out, dim, value, cVals, name, L ## desc,, 0)
#define _TlgArg_imp3data_type, in_type, out_type, has_out, dim, value, cVals, name, desc, tags) (data_type, in_type, out_type, has_out, dim, value, cVals, name, L ## desc, tags, 1)
#define _TlgChannelVal_TlgAvalue)
#define _tlgChannelVal_tlgArrayctype, pValues, cValues, inType, outType, hasOut, name, desc, tags, hasTags)
#define _tlgChannelVal_tlgAutovalue, name, desc, tags, hasTags)
#define _TlgChannelVal_TlgAutovalue)
#define _TlgChannelVal_TlgBvalue)
#define _tlgChannelVal_tlgBinaryctype, pValue, cbValue, inType, outType, hasOut, name, desc, tags, hasTags)
#define _tlgChannelVal_tlgBufferctype, pValue, inType, outType, hasOut, name, desc, tags, hasTags)
#define _tlgChannelVal_tlgChanneleventChannel) & 0 | (eventChannel)
#define _TlgChannelVal_TlgChannelvalue) & 0 | _TlgConst(Channel, value)
#define _tlgChannelVal_tlgCountedctype, pchValue, cchValue, inType, outType, hasOut, name, desc, tags, hasTags)
#define _TlgChannelVal_TlgCSvalue)
#define _TlgChannelVal_TlgCustAnnvalue)
#define _tlgChannelVal_tlgCustomctype, pValue, cbValue, protocol, bSchema, cbSchema, name, desc, tags, hasTags)
#define _TlgChannelVal_TlgCustomvalue)
#define _tlgChannelVal_tlgCustomAnnotkey, value)
#define _tlgChannelVal_tlgDesceventDescription)
#define _TlgChannelVal_TlgDescvalue)
#define _TlgChannelVal_TlgEvtTagvalue)
#define _TlgChannelVal_TlgFAvalue)
#define _tlgChannelVal_tlgFixedArrayctype, pValues, cValues, inType, outType, hasOut, name, desc, tags, hasTags)
#define _tlgChannelVal_tlgKeywordeventKeyword)
#define _TlgChannelVal_TlgKeywordvalue)
#define _tlgChannelVal_tlgLeveleventLevel)
#define _TlgChannelVal_TlgLevelvalue)
#define _tlgChannelVal_tlgOpcodeeventOpcode)
#define _TlgChannelVal_TlgOpcodevalue)
#define _TlgChannelVal_TlgPvalue)
#define _tlgChannelVal_tlgPackedDatactype, pValue, cbValue, dataDescType, hasType)
#define _tlgChannelVal_tlgPackedFieldctype, pValue, cbValue, inType, outType, hasOut, name, desc, tags, hasTags)
#define _tlgChannelVal_tlgPackedMetainType, outType, hasOut, name, desc, tags, hasTags)
#define _TlgChannelVal_TlgPDvalue)
#define _TlgChannelVal_TlgPFvalue)
#define _TlgChannelVal_TlgPMvalue)
#define _tlgChannelVal_tlgPszctype, pszValue, inType, outType, hasOut, name, desc, tags, hasTags)
#define _TlgChannelVal_TlgSvalue)
#define _tlgChannelVal_tlgScalarRefctype, value, inType, outType, hasOut, name, desc, tags, hasTags)
#define _tlgChannelVal_tlgScalarValctype, value, inType, outType, hasOut, name, desc, tags, hasTags)
#define _tlgChannelVal_tlgSidctype, pValue, name, desc, tags, hasTags)
#define _TlgChannelVal_TlgSidvalue)
#define _TlgChannelVal_TlgStringvalue)
#define _tlgChannelVal_tlgStructfieldCount, inType, name, desc, tags, hasTags)
#define _TlgChannelVal_TlgSzvalue)
#define _tlgChannelVal_tlgTageventTag)
#define _TlgChannelVal_TlgWszvalue)
#define _tlgCreate1Sz_char16_tpDesc, psz) _tlgCreate1Sz_wchar_t(pDesc, psz)
#define _tlgDataDescCount_tlgArrayctype, pValues, cValues, inType, outType, hasOut, name, desc, tags, hasTags) + 2
#define _tlgDataDescCount_tlgAutovalue, name, desc, tags, hasTags) + 1
#define _tlgDataDescCount_tlgBinaryctype, pValue, cbValue, inType, outType, hasOut, name, desc, tags, hasTags) + 2
#define _tlgDataDescCount_tlgBufferctype, pValue, inType, outType, hasOut, name, desc, tags, hasTags) + 2
#define _tlgDataDescCount_tlgChanneleventChannel)
#define _tlgDataDescCount_tlgCountedctype, pchValue, cchValue, inType, outType, hasOut, name, desc, tags, hasTags) + 2
#define _tlgDataDescCount_tlgCustomctype, pValue, cbValue, protocol, bSchema, cbSchema, name, desc, tags, hasTags) + 2
#define _tlgDataDescCount_tlgCustomAnnotkey, value)
#define _tlgDataDescCount_tlgDesceventDescription)
#define _tlgDataDescCount_tlgFixedArrayctype, pValues, cValues, inType, outType, hasOut, name, desc, tags, hasTags) + 1
#define _tlgDataDescCount_tlgKeywordeventKeyword)
#define _tlgDataDescCount_tlgLeveleventLevel)
#define _tlgDataDescCount_tlgOpcodeeventOpcode)
#define _tlgDataDescCount_tlgPackedDatactype, pValue, cbValue, dataDescType, hasType) + 1
#define _tlgDataDescCount_tlgPackedFieldctype, pValue, cbValue, inType, outType, hasOut, name, desc, tags, hasTags) + 1
#define _tlgDataDescCount_tlgPackedMetainType, outType, hasOut, name, desc, tags, hasTags)
#define _tlgDataDescCount_tlgPszctype, pszValue, inType, outType, hasOut, name, desc, tags, hasTags) + 1
#define _tlgDataDescCount_tlgScalarRefctype, value, inType, outType, hasOut, name, desc, tags, hasTags) + 1
#define _tlgDataDescCount_tlgScalarValctype, value, inType, outType, hasOut, name, desc, tags, hasTags) + 1
#define _tlgDataDescCount_tlgSidctype, pValue, name, desc, tags, hasTags) + 1
#define _tlgDataDescCount_tlgStructfieldCount, inType, name, desc, tags, hasTags) + _tlgCheckStructFieldCountCppOnly(fieldCount)
#define _tlgDataDescCount_tlgTageventTag)
#define _tlgDataDescCreate_tlgChanneln, eventChannel)
#define _TlgDataDescCreate_TlgChanneln, data_type, cVals)
#define _TlgDataDescCreate_TlgCustAnnn, data_type, cVals)
#define _tlgDataDescCreate_tlgCustomAnnotn, key, value)
#define _tlgDataDescCreate_tlgDescn, eventDescription)
#define _TlgDataDescCreate_TlgDescn, data_type, cVals)
#define _TlgDataDescCreate_TlgEvtTagn, data_type, cVals)
#define _tlgDataDescCreate_tlgKeywordn, eventKeyword)
#define _TlgDataDescCreate_TlgKeywordn, data_type, cVals)
#define _tlgDataDescCreate_tlgLeveln, eventLevel)
#define _TlgDataDescCreate_TlgLeveln, data_type, cVals)
#define _tlgDataDescCreate_tlgOpcoden, eventOpcode)
#define _TlgDataDescCreate_TlgOpcoden, data_type, cVals)
#define _tlgDataDescCreate_tlgPackedMetan, inType, outType, hasOut, name, desc, tags, hasTags) _tlg_AssertValidPackedMetadataInType(inType);
#define _TlgDataDescCreate_TlgPMn, data_type, cVals)
#define _tlgDataDescCreate_tlgStructn, fieldCount, inType, name, desc, tags, hasTags) _tlg_CASSERT((fieldCount) < 128, "TraceLoggingStruct fieldCount must be less than 128.");
#define _TlgDataDescCreate_TlgStructn, data_type, cVals)
#define _tlgDataDescCreate_tlgTagn, eventTag)
#define _tlgDefineProvider_functionWrapperBegin0functionPostfix)
#define _TlgDefineProvider_functionWrapperBegin0functionPostfix)
#define _TlgDescVal_TlgAname, desc)
#define _tlgDescVal_tlgArrayctype, pValues, cValues, inType, outType, hasOut, name, desc, tags, hasTags)
#define _tlgDescVal_tlgAutovalue, name, desc, tags, hasTags)
#define _TlgDescVal_TlgAutoname, desc)
#define _TlgDescVal_TlgBname, desc)
#define _tlgDescVal_tlgBinaryctype, pValue, cbValue, inType, outType, hasOut, name, desc, tags, hasTags)
#define _tlgDescVal_tlgBufferctype, pValue, inType, outType, hasOut, name, desc, tags, hasTags)
#define _tlgDescVal_tlgChanneleventChannel)
#define _TlgDescVal_TlgChannelname, desc)
#define _tlgDescVal_tlgCountedctype, pchValue, cchValue, inType, outType, hasOut, name, desc, tags, hasTags)
#define _TlgDescVal_TlgCSname, desc)
#define _TlgDescVal_TlgCustAnnname, desc)
#define _tlgDescVal_tlgCustomctype, pValue, cbValue, protocol, bSchema, cbSchema, name, desc, tags, hasTags)
#define _TlgDescVal_TlgCustomname, desc)
#define _tlgDescVal_tlgCustomAnnotkey, value)
#define _tlgDescVal_tlgDesceventDescription) eventDescription
#define _TlgDescVal_TlgDescname, desc) desc
#define _TlgDescVal_TlgEvtTagname, desc)
#define _TlgDescVal_TlgFAname, desc)
#define _tlgDescVal_tlgFixedArrayctype, pValues, cValues, inType, outType, hasOut, name, desc, tags, hasTags)
#define _tlgDescVal_tlgKeywordeventKeyword)
#define _TlgDescVal_TlgKeywordname, desc)
#define _tlgDescVal_tlgLeveleventLevel)
#define _TlgDescVal_TlgLevelname, desc)
#define _tlgDescVal_tlgOpcodeeventOpcode)
#define _TlgDescVal_TlgOpcodename, desc)
#define _TlgDescVal_TlgPname, desc)
#define _tlgDescVal_tlgPackedDatactype, pValue, cbValue, dataDescType, hasType)
#define _tlgDescVal_tlgPackedFieldctype, pValue, cbValue, inType, outType, hasOut, name, desc, tags, hasTags)
#define _tlgDescVal_tlgPackedMetainType, outType, hasOut, name, desc, tags, hasTags)
#define _TlgDescVal_TlgPDname, desc)
#define _TlgDescVal_TlgPFname, desc)
#define _TlgDescVal_TlgPMname, desc)
#define _tlgDescVal_tlgPszctype, pszValue, inType, outType, hasOut, name, desc, tags, hasTags)
#define _TlgDescVal_TlgSname, desc)
#define _tlgDescVal_tlgScalarRefctype, value, inType, outType, hasOut, name, desc, tags, hasTags)
#define _tlgDescVal_tlgScalarValctype, value, inType, outType, hasOut, name, desc, tags, hasTags)
#define _tlgDescVal_tlgSidctype, pValue, name, desc, tags, hasTags)
#define _TlgDescVal_TlgSidname, desc)
#define _TlgDescVal_TlgStringname, desc)
#define _tlgDescVal_tlgStructfieldCount, inType, name, desc, tags, hasTags)
#define _TlgDescVal_TlgStructname, desc)
#define _TlgDescVal_TlgSzname, desc)
#define _tlgDescVal_tlgTageventTag)
#define _TlgDescVal_TlgWszname, desc)
#define _tlgDT_imp0name, ...) name,,, 0
#define _tlgDT_imp1name, desc) name, L"" desc,, 0
#define _tlgDT_imp2name, desc, tags) name, L"" desc, tags, 1
#define _tlgDT_impBmacro, args) macro args
#define _TlgEvtTagEncodeeventTag) (((eventTag) & 0xfe00000) >> 21) /* Encode 0x0fe00000 bits into UINT16  */ | (((eventTag) & 0x1fc000) >> 6) /* Encode 0x001fc000 bits into UINT16  */ | (((eventTag) & 0x3fff) << 16) /* Trigger warning for 0x00003fff bits */ | (~(~(eventTag) | 0x0FFFFFFF))
#define _TlgEvtTagVal_TlgAvalue)
#define _tlgEvtTagVal_tlgArrayctype, pValues, cValues, inType, outType, hasOut, name, desc, tags, hasTags)
#define _tlgEvtTagVal_tlgAutovalue, name, desc, tags, hasTags)
#define _TlgEvtTagVal_TlgAutovalue)
#define _TlgEvtTagVal_TlgBvalue)
#define _tlgEvtTagVal_tlgBinaryctype, pValue, cbValue, inType, outType, hasOut, name, desc, tags, hasTags)
#define _tlgEvtTagVal_tlgBufferctype, pValue, inType, outType, hasOut, name, desc, tags, hasTags)
#define _tlgEvtTagVal_tlgChanneleventChannel)
#define _TlgEvtTagVal_TlgChannelvalue)
#define _tlgEvtTagVal_tlgCountedctype, pchValue, cchValue, inType, outType, hasOut, name, desc, tags, hasTags)
#define _TlgEvtTagVal_TlgCSvalue)
#define _TlgEvtTagVal_TlgCustAnnvalue)
#define _tlgEvtTagVal_tlgCustomctype, pValue, cbValue, protocol, bSchema, cbSchema, name, desc, tags, hasTags)
#define _TlgEvtTagVal_TlgCustomvalue)
#define _tlgEvtTagVal_tlgCustomAnnotkey, value)
#define _tlgEvtTagVal_tlgDesceventDescription)
#define _TlgEvtTagVal_TlgDescvalue)
#define _TlgEvtTagVal_TlgEvtTagvalue) | value
#define _TlgEvtTagVal_TlgFAvalue)
#define _tlgEvtTagVal_tlgFixedArrayctype, pValues, cValues, inType, outType, hasOut, name, desc, tags, hasTags)
#define _tlgEvtTagVal_tlgKeywordeventKeyword)
#define _TlgEvtTagVal_TlgKeywordvalue)
#define _tlgEvtTagVal_tlgLeveleventLevel)
#define _TlgEvtTagVal_TlgLevelvalue)
#define _tlgEvtTagVal_tlgOpcodeeventOpcode)
#define _TlgEvtTagVal_TlgOpcodevalue)
#define _TlgEvtTagVal_TlgPvalue)
#define _tlgEvtTagVal_tlgPackedDatactype, pValue, cbValue, dataDescType, hasType)
#define _tlgEvtTagVal_tlgPackedFieldctype, pValue, cbValue, inType, outType, hasOut, name, desc, tags, hasTags)
#define _tlgEvtTagVal_tlgPackedMetainType, outType, hasOut, name, desc, tags, hasTags)
#define _TlgEvtTagVal_TlgPDvalue)
#define _TlgEvtTagVal_TlgPFvalue)
#define _TlgEvtTagVal_TlgPMvalue)
#define _tlgEvtTagVal_tlgPszctype, pszValue, inType, outType, hasOut, name, desc, tags, hasTags)
#define _TlgEvtTagVal_TlgSvalue)
#define _tlgEvtTagVal_tlgScalarRefctype, value, inType, outType, hasOut, name, desc, tags, hasTags)
#define _tlgEvtTagVal_tlgScalarValctype, value, inType, outType, hasOut, name, desc, tags, hasTags)
#define _tlgEvtTagVal_tlgSidctype, pValue, name, desc, tags, hasTags)
#define _TlgEvtTagVal_TlgSidvalue)
#define _TlgEvtTagVal_TlgStringvalue)
#define _tlgEvtTagVal_tlgStructfieldCount, inType, name, desc, tags, hasTags)
#define _TlgEvtTagVal_TlgSzvalue)
#define _tlgEvtTagVal_tlgTageventTag) | (eventTag)
#define _TlgEvtTagVal_TlgWszvalue)
#define _tlgExpandProviderIda, b, c, d, e, f, g, h, i, j, k) { a, b, c, { d, e, f, g, h, i, j, k } }
#define _tlgExpandType_imp1typeVal) (typeVal), 1
#define _tlgExpandType_impBmacro, args) macro args
#define _tlgGetLengthSidpSid) (8u + (pSid) -> SubAuthorityCount * 4u)
#define _tlgInfoVals_00outType, tags)
#define _TlgInfoVals_00out_type, tags)
#define _tlgInfoVals_01outType, tags) | 128, 128, _tlgInfoVals_Tags(tags)
#define _TlgInfoVals_01out_type, tags) | 128, 128, _TlgInfoVals_Tags(tags)
#define _tlgInfoVals_10outType, tags) | 128, outType
#define _TlgInfoVals_10out_type, tags) | 128, out_type
#define _tlgInfoVals_11outType, tags) | 128, outType | 128, _tlgInfoVals_Tags(tags)
#define _TlgInfoVals_11out_type, tags) | 128, out_type | 128, _TlgInfoVals_Tags(tags)
#define _tlgInfoVals_tlgChanneln, eventChannel)
#define _TlgInfoVals_TlgChannelin_type, out_type, has_out, cVals, name, has_tags, tags)
#define _TlgInfoVals_TlgCustAnnin_type, out_type, has_out, cVals, name, has_tags, tags)
#define _tlgInfoVals_tlgCustomn, ctype, pValue, cbValue, protocol, bSchema, cbSchema, name, desc, tags, hasTags) , (name), ((protocol) & 31) | 96 _tlgInfoVals_0 ## hasTags(0, tags), cbSchema, { _tlg_FLATTEN bSchema }
#define _TlgInfoVals_TlgCustomin_type, out_type, has_out, cVals, name, has_tags, tags) , (name), ((in_type) & 31) | 96 _TlgInfoVals_0 ## has_tags(0, tags), has_out, { _TLG_FLATTEN out_type }
#define _tlgInfoVals_tlgCustomAnnotn, key, value)
#define _tlgInfoVals_tlgDescn, eventDescription)
#define _TlgInfoVals_TlgDescin_type, out_type, has_out, cVals, name, has_tags, tags)
#define _TlgInfoVals_TlgEvtTagin_type, out_type, has_out, cVals, name, has_tags, tags)
#define _tlgInfoVals_tlgKeywordn, eventKeyword)
#define _TlgInfoVals_TlgKeywordin_type, out_type, has_out, cVals, name, has_tags, tags)
#define _tlgInfoVals_tlgLeveln, eventLevel)
#define _TlgInfoVals_TlgLevelin_type, out_type, has_out, cVals, name, has_tags, tags)
#define _tlgInfoVals_tlgOpcoden, eventOpcode)
#define _TlgInfoVals_TlgOpcodein_type, out_type, has_out, cVals, name, has_tags, tags)
#define _tlgInfoVals_tlgPackedDatan, ctype, pValue, cbValue, dataDescType, hasType)
#define _TlgInfoVals_TlgPDin_type, out_type, has_out, cVals, name, has_tags, tags)
#define _tlgInfoVals_tlgSidn, ctype, pValue, name, desc, tags, hasTags) , (name), TlgInSID _tlgInfoVals_0 ## hasTags(outType, tags)
#define _tlgInfoVals_tlgStructn, fieldCount, inType, name, desc, tags, hasTags) , (name), inType _tlgInfoVals_1 ## hasTags(fieldCount, tags)
#define _TlgInfoVals_TlgStructin_type, out_type, has_out, cVals, name, has_tags, tags) , (name), in_type _TlgInfoVals_1 ## has_tags(cVals, tags)
#define _tlgInfoVals_tlgTagn, eventTag)
#define _tlgInfoVars_0n)
#define _TlgInfoVars_0n)
#define _tlgInfoVars_00n)
#define _TlgInfoVars_00n)
#define _tlgInfoVars_tlgChanneln, eventChannel)
#define _tlgInfoVars_tlgCustomAnnotn, key, value)
#define _tlgInfoVars_tlgDescn, eventDescription)
#define _tlgInfoVars_tlgKeywordn, eventKeyword)
#define _tlgInfoVars_tlgLeveln, eventLevel)
#define _tlgInfoVars_tlgOpcoden, eventOpcode)
#define _tlgInfoVars_tlgPackedDatan, ctype, pValue, cbValue, dataDescType, hasType)
#define _TlgInfoVars_TlgPDn, name, in_type, has_out, has_tags)
#define _tlgInfoVars_tlgTagn, eventTag)
#define _tlgKeywordConstn) n
#define _TlgKeywordVal_TlgAvalue)
#define _tlgKeywordVal_tlgArrayctype, pValues, cValues, inType, outType, hasOut, name, desc, tags, hasTags)
#define _tlgKeywordVal_tlgAutovalue, name, desc, tags, hasTags)
#define _TlgKeywordVal_TlgAutovalue)
#define _TlgKeywordVal_TlgBvalue)
#define _tlgKeywordVal_tlgBinaryctype, pValue, cbValue, inType, outType, hasOut, name, desc, tags, hasTags)
#define _tlgKeywordVal_tlgBufferctype, pValue, inType, outType, hasOut, name, desc, tags, hasTags)
#define _tlgKeywordVal_tlgChanneleventChannel)
#define _TlgKeywordVal_TlgChannelvalue)
#define _tlgKeywordVal_tlgCountedctype, pchValue, cchValue, inType, outType, hasOut, name, desc, tags, hasTags)
#define _TlgKeywordVal_TlgCSvalue)
#define _TlgKeywordVal_TlgCustAnnvalue)
#define _tlgKeywordVal_tlgCustomctype, pValue, cbValue, protocol, bSchema, cbSchema, name, desc, tags, hasTags)
#define _TlgKeywordVal_TlgCustomvalue)
#define _tlgKeywordVal_tlgCustomAnnotkey, value)
#define _tlgKeywordVal_tlgDesceventDescription)
#define _TlgKeywordVal_TlgDescvalue)
#define _TlgKeywordVal_TlgEvtTagvalue)
#define _TlgKeywordVal_TlgFAvalue)
#define _tlgKeywordVal_tlgFixedArrayctype, pValues, cValues, inType, outType, hasOut, name, desc, tags, hasTags)
#define _tlgKeywordVal_tlgKeywordeventKeyword) | (eventKeyword)
#define _TlgKeywordVal_TlgKeywordvalue) | _TlgConst(Keyword, value)
#define _tlgKeywordVal_tlgLeveleventLevel)
#define _TlgKeywordVal_TlgLevelvalue)
#define _tlgKeywordVal_tlgOpcodeeventOpcode)
#define _TlgKeywordVal_TlgOpcodevalue)
#define _TlgKeywordVal_TlgPvalue)
#define _tlgKeywordVal_tlgPackedDatactype, pValue, cbValue, dataDescType, hasType)
#define _tlgKeywordVal_tlgPackedFieldctype, pValue, cbValue, inType, outType, hasOut, name, desc, tags, hasTags)
#define _tlgKeywordVal_tlgPackedMetainType, outType, hasOut, name, desc, tags, hasTags)
#define _TlgKeywordVal_TlgPDvalue)
#define _TlgKeywordVal_TlgPFvalue)
#define _TlgKeywordVal_TlgPMvalue)
#define _tlgKeywordVal_tlgPszctype, pszValue, inType, outType, hasOut, name, desc, tags, hasTags)
#define _TlgKeywordVal_TlgSvalue)
#define _tlgKeywordVal_tlgScalarRefctype, value, inType, outType, hasOut, name, desc, tags, hasTags)
#define _tlgKeywordVal_tlgScalarValctype, value, inType, outType, hasOut, name, desc, tags, hasTags)
#define _tlgKeywordVal_tlgSidctype, pValue, name, desc, tags, hasTags)
#define _TlgKeywordVal_TlgSidvalue)
#define _TlgKeywordVal_TlgStringvalue)
#define _tlgKeywordVal_tlgStructfieldCount, inType, name, desc, tags, hasTags)
#define _TlgKeywordVal_TlgSzvalue)
#define _tlgKeywordVal_tlgTageventTag)
#define _TlgKeywordVal_TlgWszvalue)
#define _TlgLevelVal_TlgAvalue)
#define _tlgLevelVal_tlgArrayctype, pValues, cValues, inType, outType, hasOut, name, desc, tags, hasTags)
#define _tlgLevelVal_tlgAutovalue, name, desc, tags, hasTags)
#define _TlgLevelVal_TlgAutovalue)
#define _TlgLevelVal_TlgBvalue)
#define _tlgLevelVal_tlgBinaryctype, pValue, cbValue, inType, outType, hasOut, name, desc, tags, hasTags)
#define _tlgLevelVal_tlgBufferctype, pValue, inType, outType, hasOut, name, desc, tags, hasTags)
#define _tlgLevelVal_tlgChanneleventChannel)
#define _TlgLevelVal_TlgChannelvalue)
#define _tlgLevelVal_tlgCountedctype, pchValue, cchValue, inType, outType, hasOut, name, desc, tags, hasTags)
#define _TlgLevelVal_TlgCSvalue)
#define _TlgLevelVal_TlgCustAnnvalue)
#define _tlgLevelVal_tlgCustomctype, pValue, cbValue, protocol, bSchema, cbSchema, name, desc, tags, hasTags)
#define _TlgLevelVal_TlgCustomvalue)
#define _tlgLevelVal_tlgCustomAnnotkey, value)
#define _tlgLevelVal_tlgDesceventDescription)
#define _TlgLevelVal_TlgDescvalue)
#define _TlgLevelVal_TlgEvtTagvalue)
#define _TlgLevelVal_TlgFAvalue)
#define _tlgLevelVal_tlgFixedArrayctype, pValues, cValues, inType, outType, hasOut, name, desc, tags, hasTags)
#define _tlgLevelVal_tlgKeywordeventKeyword)
#define _TlgLevelVal_TlgKeywordvalue)
#define _tlgLevelVal_tlgLeveleventLevel) & 0 | (eventLevel)
#define _TlgLevelVal_TlgLevelvalue) & 0 | _TlgConst(Level, value)
#define _tlgLevelVal_tlgOpcodeeventOpcode)
#define _TlgLevelVal_TlgOpcodevalue)
#define _TlgLevelVal_TlgPvalue)
#define _tlgLevelVal_tlgPackedDatactype, pValue, cbValue, dataDescType, hasType)
#define _tlgLevelVal_tlgPackedFieldctype, pValue, cbValue, inType, outType, hasOut, name, desc, tags, hasTags)
#define _tlgLevelVal_tlgPackedMetainType, outType, hasOut, name, desc, tags, hasTags)
#define _TlgLevelVal_TlgPDvalue)
#define _TlgLevelVal_TlgPFvalue)
#define _TlgLevelVal_TlgPMvalue)
#define _tlgLevelVal_tlgPszctype, pszValue, inType, outType, hasOut, name, desc, tags, hasTags)
#define _TlgLevelVal_TlgSvalue)
#define _tlgLevelVal_tlgScalarRefctype, value, inType, outType, hasOut, name, desc, tags, hasTags)
#define _tlgLevelVal_tlgScalarValctype, value, inType, outType, hasOut, name, desc, tags, hasTags)
#define _tlgLevelVal_tlgSidctype, pValue, name, desc, tags, hasTags)
#define _TlgLevelVal_TlgSidvalue)
#define _TlgLevelVal_TlgStringvalue)
#define _tlgLevelVal_tlgStructfieldCount, inType, name, desc, tags, hasTags)
#define _TlgLevelVal_TlgSzvalue)
#define _tlgLevelVal_tlgTageventTag)
#define _TlgLevelVal_TlgWszvalue)
#define _tlgNDT_imp0value, ...) # value,,, 0
#define _tlgNDT_imp1value, name) name,,, 0
#define _tlgNDT_imp2value, name, desc) name, L"" desc,, 0
#define _tlgNDT_imp3value, name, desc, tags) name, L"" desc, tags, 1
#define _tlgNDT_impBmacro, args) macro args
#define _TlgOpcodeVal_TlgAvalue)
#define _tlgOpcodeVal_tlgArrayctype, pValues, cValues, inType, outType, hasOut, name, desc, tags, hasTags)
#define _tlgOpcodeVal_tlgAutovalue, name, desc, tags, hasTags)
#define _TlgOpcodeVal_TlgAutovalue)
#define _TlgOpcodeVal_TlgBvalue)
#define _tlgOpcodeVal_tlgBinaryctype, pValue, cbValue, inType, outType, hasOut, name, desc, tags, hasTags)
#define _tlgOpcodeVal_tlgBufferctype, pValue, inType, outType, hasOut, name, desc, tags, hasTags)
#define _tlgOpcodeVal_tlgChanneleventChannel)
#define _TlgOpcodeVal_TlgChannelvalue)
#define _tlgOpcodeVal_tlgCountedctype, pchValue, cchValue, inType, outType, hasOut, name, desc, tags, hasTags)
#define _TlgOpcodeVal_TlgCSvalue)
#define _TlgOpcodeVal_TlgCustAnnvalue)
#define _tlgOpcodeVal_tlgCustomctype, pValue, cbValue, protocol, bSchema, cbSchema, name, desc, tags, hasTags)
#define _TlgOpcodeVal_TlgCustomvalue)
#define _tlgOpcodeVal_tlgCustomAnnotkey, value)
#define _tlgOpcodeVal_tlgDesceventDescription)
#define _TlgOpcodeVal_TlgDescvalue)
#define _TlgOpcodeVal_TlgEvtTagvalue)
#define _TlgOpcodeVal_TlgFAvalue)
#define _tlgOpcodeVal_tlgFixedArrayctype, pValues, cValues, inType, outType, hasOut, name, desc, tags, hasTags)
#define _tlgOpcodeVal_tlgKeywordeventKeyword)
#define _TlgOpcodeVal_TlgKeywordvalue)
#define _tlgOpcodeVal_tlgLeveleventLevel)
#define _TlgOpcodeVal_TlgLevelvalue)
#define _tlgOpcodeVal_tlgOpcodeeventOpcode) & 0 | (eventOpcode)
#define _TlgOpcodeVal_TlgOpcodevalue) & 0 | _TlgConst(Opcode, value)
#define _TlgOpcodeVal_TlgPvalue)
#define _tlgOpcodeVal_tlgPackedDatactype, pValue, cbValue, dataDescType, hasType)
#define _tlgOpcodeVal_tlgPackedFieldctype, pValue, cbValue, inType, outType, hasOut, name, desc, tags, hasTags)
#define _tlgOpcodeVal_tlgPackedMetainType, outType, hasOut, name, desc, tags, hasTags)
#define _TlgOpcodeVal_TlgPDvalue)
#define _TlgOpcodeVal_TlgPFvalue)
#define _TlgOpcodeVal_TlgPMvalue)
#define _tlgOpcodeVal_tlgPszctype, pszValue, inType, outType, hasOut, name, desc, tags, hasTags)
#define _TlgOpcodeVal_TlgSvalue)
#define _tlgOpcodeVal_tlgScalarRefctype, value, inType, outType, hasOut, name, desc, tags, hasTags)
#define _tlgOpcodeVal_tlgScalarValctype, value, inType, outType, hasOut, name, desc, tags, hasTags)
#define _tlgOpcodeVal_tlgSidctype, pValue, name, desc, tags, hasTags)
#define _TlgOpcodeVal_TlgSidvalue)
#define _TlgOpcodeVal_TlgStringvalue)
#define _tlgOpcodeVal_tlgStructfieldCount, inType, name, desc, tags, hasTags)
#define _TlgOpcodeVal_TlgSzvalue)
#define _tlgOpcodeVal_tlgTageventTag)
#define _TlgOpcodeVal_TlgWszvalue)
#define _tlgProvVal_tlgOptiontraitType, ctype, value) , 3 + sizeof (ctype), traitType, _tlg_FLATTEN value
#define _TlgSegMetadataProviders 0
#define _TlgTempVars_TlgChanneln, data_type, in_type, value, cVals)
#define _TlgTempVars_TlgCustAnnn, data_type, in_type, value, cVals)
#define _TlgTempVars_TlgDescn, data_type, in_type, value, cVals)
#define _TlgTempVars_TlgEvtTagn, data_type, in_type, value, cVals)
#define _TlgTempVars_TlgKeywordn, data_type, in_type, value, cVals)
#define _TlgTempVars_TlgLeveln, data_type, in_type, value, cVals)
#define _TlgTempVars_TlgOpcoden, data_type, in_type, value, cVals)
#define _TlgTempVars_TlgPMn, data_type, in_type, value, cVals) _TLG_AssertValidPackedMetadataInType(in_type);
#define _TlgTempVars_TlgStructn, data_type, in_type, value, cVals) _TLG_CASSERT(cVals < 128, "Struct size must be less than 128");
#define _tlgValidateProviderId_parenthesisCALLmacro, args) macro args
#define _tlgValidateProviderId_presentCALLmacro, args) macro args
#define _tlgWriteTransfer 0

#define _tlg_ASSERTexp, str) ((void) (!(exp) ? (__annotation(L"Debug", L"AssertFail", L"TraceLogging: " _tlg_LSTRINGIZE(exp) L" : " str), TLG_RAISEASSERTIONFAILURE(), 0) : 0))
#define _TLG_ASSERTexp, str) ((void) (!(exp) ? (__annotation(L"Debug", L"AssertFail", L"TraceLogging: " L ## # exp L" : " L ## str), DbgRaiseAssertionFailure(), 0) : 0))
#define _tlg_AssertValidPackedMetadataInTypeCppOnlyinType)
#define _TLG_AssertValidPackedMetadataInTypeCppOnlyin_type)
#define _tlg_CATa, ...) _tlg_CAT_imp(a, __VA_ARGS__)
#define _TLG_CATa, ...) _TLG_CAT_imp(a, __VA_ARGS__)
#define _tlg_FASTFAILcode) __fastfail(code)
#define _tlg_FOR_imp0f, ...)
#define _TLG_FOR_imp0fn)
#define _tlg_FOR_imp1f, a0) f(0, a0)
#define _TLG_FOR_imp1fn, arg, ...) _TLG_FOR_impN(0, fn, arg, fn, __VA_ARGS__)
#define _tlg_FOR_imp10f, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9) f(0, a0) f(1, a1) f(2, a2) f(3, a3) f(4, a4) f(5, a5) f(6, a6) f(7, a7) f(8, a8) f(9, a9)
#define _TLG_FOR_imp10fn, arg, ...) _TLG_FOR_impN(9, fn, arg, fn, __VA_ARGS__)
#define _tlg_FOR_imp11f, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10) f(0, a0) f(1, a1) f(2, a2) f(3, a3) f(4, a4) f(5, a5) f(6, a6) f(7, a7) f(8, a8) f(9, a9) f(10, a10)
#define _TLG_FOR_imp11fn, arg, ...) _TLG_FOR_impN(10, fn, arg, fn, __VA_ARGS__)
#define _tlg_FOR_imp12f, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11) f(0, a0) f(1, a1) f(2, a2) f(3, a3) f(4, a4) f(5, a5) f(6, a6) f(7, a7) f(8, a8) f(9, a9) f(10, a10) f(11, a11)
#define _TLG_FOR_imp12fn, arg, ...) _TLG_FOR_impN(11, fn, arg, fn, __VA_ARGS__)
#define _tlg_FOR_imp13f, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12) f(0, a0) f(1, a1) f(2, a2) f(3, a3) f(4, a4) f(5, a5) f(6, a6) f(7, a7) f(8, a8) f(9, a9) f(10, a10) f(11, a11) f(12, a12)
#define _TLG_FOR_imp13fn, arg, ...) _TLG_FOR_impN(12, fn, arg, fn, __VA_ARGS__)
#define _tlg_FOR_imp14f, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13) f(0, a0) f(1, a1) f(2, a2) f(3, a3) f(4, a4) f(5, a5) f(6, a6) f(7, a7) f(8, a8) f(9, a9) f(10, a10) f(11, a11) f(12, a12) f(13, a13)
#define _TLG_FOR_imp14fn, arg, ...) _TLG_FOR_impN(13, fn, arg, fn, __VA_ARGS__)
#define _tlg_FOR_imp15f, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14) f(0, a0) f(1, a1) f(2, a2) f(3, a3) f(4, a4) f(5, a5) f(6, a6) f(7, a7) f(8, a8) f(9, a9) f(10, a10) f(11, a11) f(12, a12) f(13, a13) f(14, a14)
#define _TLG_FOR_imp15fn, arg, ...) _TLG_FOR_impN(14, fn, arg, fn, __VA_ARGS__)
#define _tlg_FOR_imp16f, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15) f(0, a0) f(1, a1) f(2, a2) f(3, a3) f(4, a4) f(5, a5) f(6, a6) f(7, a7) f(8, a8) f(9, a9) f(10, a10) f(11, a11) f(12, a12) f(13, a13) f(14, a14) f(15, a15)
#define _TLG_FOR_imp16fn, arg, ...) _TLG_FOR_impN(15, fn, arg, fn, __VA_ARGS__)
#define _tlg_FOR_imp17f, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16) f(0, a0) f(1, a1) f(2, a2) f(3, a3) f(4, a4) f(5, a5) f(6, a6) f(7, a7) f(8, a8) f(9, a9) f(10, a10) f(11, a11) f(12, a12) f(13, a13) f(14, a14) f(15, a15) f(16, a16)
#define _TLG_FOR_imp17fn, arg, ...) _TLG_FOR_impN(16, fn, arg, fn, __VA_ARGS__)
#define _tlg_FOR_imp18f, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17) f(0, a0) f(1, a1) f(2, a2) f(3, a3) f(4, a4) f(5, a5) f(6, a6) f(7, a7) f(8, a8) f(9, a9) f(10, a10) f(11, a11) f(12, a12) f(13, a13) f(14, a14) f(15, a15) f(16, a16) f(17, a17)
#define _TLG_FOR_imp18fn, arg, ...) _TLG_FOR_impN(17, fn, arg, fn, __VA_ARGS__)
#define _tlg_FOR_imp19f, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17, a18) f(0, a0) f(1, a1) f(2, a2) f(3, a3) f(4, a4) f(5, a5) f(6, a6) f(7, a7) f(8, a8) f(9, a9) f(10, a10) f(11, a11) f(12, a12) f(13, a13) f(14, a14) f(15, a15) f(16, a16) f(17, a17) f(18, a18)
#define _TLG_FOR_imp19fn, arg, ...) _TLG_FOR_impN(18, fn, arg, fn, __VA_ARGS__)
#define _tlg_FOR_imp2f, a0, a1) f(0, a0) f(1, a1)
#define _TLG_FOR_imp2fn, arg, ...) _TLG_FOR_impN(1, fn, arg, fn, __VA_ARGS__)
#define _tlg_FOR_imp20f, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17, a18, a19) f(0, a0) f(1, a1) f(2, a2) f(3, a3) f(4, a4) f(5, a5) f(6, a6) f(7, a7) f(8, a8) f(9, a9) f(10, a10) f(11, a11) f(12, a12) f(13, a13) f(14, a14) f(15, a15) f(16, a16) f(17, a17) f(18, a18) f(19, a19)
#define _TLG_FOR_imp20fn, arg, ...) _TLG_FOR_impN(19, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp21fn, arg, ...) _TLG_FOR_impN(20, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp22fn, arg, ...) _TLG_FOR_impN(21, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp23fn, arg, ...) _TLG_FOR_impN(22, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp24fn, arg, ...) _TLG_FOR_impN(23, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp25fn, arg, ...) _TLG_FOR_impN(24, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp26fn, arg, ...) _TLG_FOR_impN(25, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp27fn, arg, ...) _TLG_FOR_impN(26, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp28fn, arg, ...) _TLG_FOR_impN(27, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp29fn, arg, ...) _TLG_FOR_impN(28, fn, arg, fn, __VA_ARGS__)
#define _tlg_FOR_imp3f, a0, a1, a2) f(0, a0) f(1, a1) f(2, a2)
#define _TLG_FOR_imp3fn, arg, ...) _TLG_FOR_impN(2, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp30fn, arg, ...) _TLG_FOR_impN(29, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp31fn, arg, ...) _TLG_FOR_impN(30, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp32fn, arg, ...) _TLG_FOR_impN(31, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp33fn, arg, ...) _TLG_FOR_impN(32, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp34fn, arg, ...) _TLG_FOR_impN(33, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp35fn, arg, ...) _TLG_FOR_impN(34, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp36fn, arg, ...) _TLG_FOR_impN(35, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp37fn, arg, ...) _TLG_FOR_impN(36, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp38fn, arg, ...) _TLG_FOR_impN(37, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp39fn, arg, ...) _TLG_FOR_impN(38, fn, arg, fn, __VA_ARGS__)
#define _tlg_FOR_imp4f, a0, a1, a2, a3) f(0, a0) f(1, a1) f(2, a2) f(3, a3)
#define _TLG_FOR_imp4fn, arg, ...) _TLG_FOR_impN(3, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp40fn, arg, ...) _TLG_FOR_impN(39, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp41fn, arg, ...) _TLG_FOR_impN(40, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp42fn, arg, ...) _TLG_FOR_impN(41, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp43fn, arg, ...) _TLG_FOR_impN(42, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp44fn, arg, ...) _TLG_FOR_impN(43, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp45fn, arg, ...) _TLG_FOR_impN(44, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp46fn, arg, ...) _TLG_FOR_impN(45, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp47fn, arg, ...) _TLG_FOR_impN(46, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp48fn, arg, ...) _TLG_FOR_impN(47, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp49fn, arg, ...) _TLG_FOR_impN(48, fn, arg, fn, __VA_ARGS__)
#define _tlg_FOR_imp5f, a0, a1, a2, a3, a4) f(0, a0) f(1, a1) f(2, a2) f(3, a3) f(4, a4)
#define _TLG_FOR_imp5fn, arg, ...) _TLG_FOR_impN(4, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp50fn, arg, ...) _TLG_FOR_impN(49, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp51fn, arg, ...) _TLG_FOR_impN(50, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp52fn, arg, ...) _TLG_FOR_impN(51, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp53fn, arg, ...) _TLG_FOR_impN(52, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp54fn, arg, ...) _TLG_FOR_impN(53, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp55fn, arg, ...) _TLG_FOR_impN(54, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp56fn, arg, ...) _TLG_FOR_impN(55, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp57fn, arg, ...) _TLG_FOR_impN(56, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp58fn, arg, ...) _TLG_FOR_impN(57, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp59fn, arg, ...) _TLG_FOR_impN(58, fn, arg, fn, __VA_ARGS__)
#define _tlg_FOR_imp6f, a0, a1, a2, a3, a4, a5) f(0, a0) f(1, a1) f(2, a2) f(3, a3) f(4, a4) f(5, a5)
#define _TLG_FOR_imp6fn, arg, ...) _TLG_FOR_impN(5, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp60fn, arg, ...) _TLG_FOR_impN(59, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp61fn, arg, ...) _TLG_FOR_impN(60, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp62fn, arg, ...) _TLG_FOR_impN(61, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp63fn, arg, ...) _TLG_FOR_impN(62, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp64fn, arg, ...) _TLG_FOR_impN(63, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp65fn, arg, ...) _TLG_FOR_impN(64, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp66fn, arg, ...) _TLG_FOR_impN(65, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp67fn, arg, ...) _TLG_FOR_impN(66, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp68fn, arg, ...) _TLG_FOR_impN(67, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp69fn, arg, ...) _TLG_FOR_impN(68, fn, arg, fn, __VA_ARGS__)
#define _tlg_FOR_imp7f, a0, a1, a2, a3, a4, a5, a6) f(0, a0) f(1, a1) f(2, a2) f(3, a3) f(4, a4) f(5, a5) f(6, a6)
#define _TLG_FOR_imp7fn, arg, ...) _TLG_FOR_impN(6, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp70fn, arg, ...) _TLG_FOR_impN(69, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp71fn, arg, ...) _TLG_FOR_impN(70, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp72fn, arg, ...) _TLG_FOR_impN(71, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp73fn, arg, ...) _TLG_FOR_impN(72, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp74fn, arg, ...) _TLG_FOR_impN(73, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp75fn, arg, ...) _TLG_FOR_impN(74, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp76fn, arg, ...) _TLG_FOR_impN(75, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp77fn, arg, ...) _TLG_FOR_impN(76, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp78fn, arg, ...) _TLG_FOR_impN(77, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp79fn, arg, ...) _TLG_FOR_impN(78, fn, arg, fn, __VA_ARGS__)
#define _tlg_FOR_imp8f, a0, a1, a2, a3, a4, a5, a6, a7) f(0, a0) f(1, a1) f(2, a2) f(3, a3) f(4, a4) f(5, a5) f(6, a6) f(7, a7)
#define _TLG_FOR_imp8fn, arg, ...) _TLG_FOR_impN(7, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp80fn, arg, ...) _TLG_FOR_impN(79, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp81fn, arg, ...) _TLG_FOR_impN(80, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp82fn, arg, ...) _TLG_FOR_impN(81, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp83fn, arg, ...) _TLG_FOR_impN(82, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp84fn, arg, ...) _TLG_FOR_impN(83, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp85fn, arg, ...) _TLG_FOR_impN(84, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp86fn, arg, ...) _TLG_FOR_impN(85, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp87fn, arg, ...) _TLG_FOR_impN(86, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp88fn, arg, ...) _TLG_FOR_impN(87, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp89fn, arg, ...) _TLG_FOR_impN(88, fn, arg, fn, __VA_ARGS__)
#define _tlg_FOR_imp9f, a0, a1, a2, a3, a4, a5, a6, a7, a8) f(0, a0) f(1, a1) f(2, a2) f(3, a3) f(4, a4) f(5, a5) f(6, a6) f(7, a7) f(8, a8)
#define _TLG_FOR_imp9fn, arg, ...) _TLG_FOR_impN(8, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp90fn, arg, ...) _TLG_FOR_impN(89, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp91fn, arg, ...) _TLG_FOR_impN(90, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp92fn, arg, ...) _TLG_FOR_impN(91, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp93fn, arg, ...) _TLG_FOR_impN(92, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp94fn, arg, ...) _TLG_FOR_impN(93, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp95fn, arg, ...) _TLG_FOR_impN(94, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp96fn, arg, ...) _TLG_FOR_impN(95, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp97fn, arg, ...) _TLG_FOR_impN(96, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp98fn, arg, ...) _TLG_FOR_impN(97, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp99fn, arg, ...) _TLG_FOR_impN(98, fn, arg, fn, __VA_ARGS__)
#define _tlg_NARGS_imp2a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17, a18, a19, a20, a21, a22, a23, a24, a25, a26, a27, a28, a29, a30, a31, a32, a33, a34, a35, a36, a37, a38, a39, a40, a41, a42, a43, a44, a45, a46, a47, a48, a49, a50, a51, a52, a53, a54, a55, a56, a57, a58, a59, a60, a61, a62, a63, a64, a65, a66, a67, a68, a69, a70, a71, a72, a73, a74, a75, a76, a77, a78, a79, a80, a81, a82, a83, a84, a85, a86, a87, a88, a89, a90, a91, a92, a93, a94, a95, a96, a97, a98, a99, size, ...) size
#define _TLG_NARGS_imp2a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17, a18, a19, a20, a21, a22, a23, a24, a25, a26, a27, a28, a29, a30, a31, a32, a33, a34, a35, a36, a37, a38, a39, a40, a41, a42, a43, a44, a45, a46, a47, a48, a49, a50, a51, a52, a53, a54, a55, a56, a57, a58, a59, a60, a61, a62, a63, a64, a65, a66, a67, a68, a69, a70, a71, a72, a73, a74, a75, a76, a77, a78, a79, a80, a81, a82, a83, a84, a85, a86, a87, a88, a89, a90, a91, a92, a93, a94, a95, a96, a97, a98, a99, size, ...) size
#define _tlg_PASTE3a, b, c) _tlg_PASTE3_imp(a, b, c)
#define _tlg_SPLITcond, ...) _tlg_SPLIT_imp(cond, (__VA_ARGS__))
#define _TLG_SPLITcond, ...) _TLG_SPLIT_imp(cond, (__VA_ARGS__))
#define _tlg_SPLIT_imp0false_val, ...) false_val
#define _TLG_SPLIT_imp0false_val, ...) false_val
#define _tlg_SPLIT_imp1false_val, ...) __VA_ARGS__
#define _TLG_SPLIT_imp1false_val, ...) __VA_ARGS__
#define _tlg_STRINGIZEx) _tlg_STRINGIZE_imp(x)
#define _TLG_STRINGIZEx) _TLG_STRINGIZE_imp(x)
#define _tlgAnnotationFunc_imp0storageVariable)
#define _TlgAnnotationFunc_imp0storageVariable) 0
#define _TlgAnnotes_TlgAname, desc) L"|" L ## # name L"=" desc
#define _tlgAnnotes_tlgArrayctype, pValues, cValues, inType, outType, hasOut, name, desc, tags, hasTags) L"|" _tlg_LSTRINGIZE(name) L"=" desc
#define _tlgAnnotes_tlgAutovalue, name, desc, tags, hasTags) L"|" _tlg_LSTRINGIZE(name) L"=" desc
#define _TlgAnnotes_TlgAutoname, desc) L"|" L ## # name L"=" desc
#define _TlgAnnotes_TlgBname, desc) L"|" L ## # name L"=" desc
#define _tlgAnnotes_tlgBinaryctype, pValue, cbValue, inType, outType, hasOut, name, desc, tags, hasTags) L"|" _tlg_LSTRINGIZE(name) L"=" desc
#define _tlgAnnotes_tlgBufferctype, pValue, inType, outType, hasOut, name, desc, tags, hasTags) L"|" _tlg_LSTRINGIZE(name) L"=" desc
#define _tlgAnnotes_tlgChanneleventChannel)
#define _TlgAnnotes_TlgChannelname, desc)
#define _tlgAnnotes_tlgCountedctype, pchValue, cchValue, inType, outType, hasOut, name, desc, tags, hasTags) L"|" _tlg_LSTRINGIZE(name) L"=" desc
#define _TlgAnnotes_TlgCSname, desc) L"|" L ## # name L"=" desc
#define _TlgAnnotes_TlgCustAnnname, desc) L"|" L ## name L"=" desc
#define _tlgAnnotes_tlgCustomctype, pValue, cbValue, protocol, bSchema, cbSchema, name, desc, tags, hasTags) L"|" _tlg_LSTRINGIZE(name) L"=" desc
#define _TlgAnnotes_TlgCustomname, desc) L"|" L ## # name L"=" desc
#define _tlgAnnotes_tlgCustomAnnotkey, value) L"|" key L"=" value
#define _tlgAnnotes_tlgDesceventDescription)
#define _TlgAnnotes_TlgDescname, desc)
#define _TlgAnnotes_TlgEvtTagname, desc)
#define _TlgAnnotes_TlgFAname, desc) L"|" L ## # name L"=" desc
#define _tlgAnnotes_tlgFixedArrayctype, pValues, cValues, inType, outType, hasOut, name, desc, tags, hasTags) L"|" _tlg_LSTRINGIZE(name) L"=" desc
#define _tlgAnnotes_tlgKeywordeventKeyword)
#define _TlgAnnotes_TlgKeywordname, desc)
#define _tlgAnnotes_tlgLeveleventLevel)
#define _TlgAnnotes_TlgLevelname, desc)
#define _tlgAnnotes_tlgOpcodeeventOpcode)
#define _TlgAnnotes_TlgOpcodename, desc)
#define _TlgAnnotes_TlgPname, desc) L"|" L ## # name L"=" desc
#define _tlgAnnotes_tlgPackedDatactype, pValue, cbValue, dataDescType, hasType)
#define _tlgAnnotes_tlgPackedFieldctype, pValue, cbValue, inType, outType, hasOut, name, desc, tags, hasTags) L"|" _tlg_LSTRINGIZE(name) L"=" desc
#define _tlgAnnotes_tlgPackedMetainType, outType, hasOut, name, desc, tags, hasTags) L"|" _tlg_LSTRINGIZE(name) L"=" desc
#define _TlgAnnotes_TlgPDname, desc)
#define _TlgAnnotes_TlgPFname, desc) L"|" L ## # name L"=" desc
#define _TlgAnnotes_TlgPMname, desc) L"|" L ## # name L"=" desc
#define _tlgAnnotes_tlgPszctype, pszValue, inType, outType, hasOut, name, desc, tags, hasTags) L"|" _tlg_LSTRINGIZE(name) L"=" desc
#define _TlgAnnotes_TlgSname, desc) L"|" L ## # name L"=" desc
#define _tlgAnnotes_tlgScalarRefctype, value, inType, outType, hasOut, name, desc, tags, hasTags) L"|" _tlg_LSTRINGIZE(name) L"=" desc
#define _tlgAnnotes_tlgScalarValctype, value, inType, outType, hasOut, name, desc, tags, hasTags) L"|" _tlg_LSTRINGIZE(name) L"=" desc
#define _tlgAnnotes_tlgSidctype, pValue, name, desc, tags, hasTags) L"|" _tlg_LSTRINGIZE(name) L"=" desc
#define _TlgAnnotes_TlgSidname, desc) L"|" L ## # name L"=" desc
#define _TlgAnnotes_TlgStringname, desc) L"|" L ## # name L"=" desc
#define _tlgAnnotes_tlgStructfieldCount, inType, name, desc, tags, hasTags) L"|" _tlg_LSTRINGIZE(name) L"=" desc
#define _TlgAnnotes_TlgStructname, desc) L"|" L ## # name L"=" desc
#define _TlgAnnotes_TlgSzname, desc) L"|" L ## # name L"=" desc
#define _tlgAnnotes_tlgTageventTag)
#define _TlgAnnotes_TlgWszname, desc) L"|" L ## # name L"=" desc
#define _TlgArg_imp0data_type, in_type, out_type, has_out, dim, value, cVals, x) (data_type, in_type, out_type, has_out, dim, value, cVals, # value,,, 0)
#define _TlgArg_imp1data_type, in_type, out_type, has_out, dim, value, cVals, name) (data_type, in_type, out_type, has_out, dim, value, cVals, name,,, 0)
#define _TlgArg_imp2data_type, in_type, out_type, has_out, dim, value, cVals, name, desc) (data_type, in_type, out_type, has_out, dim, value, cVals, name, L ## desc,, 0)
#define _TlgArg_imp3data_type, in_type, out_type, has_out, dim, value, cVals, name, desc, tags) (data_type, in_type, out_type, has_out, dim, value, cVals, name, L ## desc, tags, 1)
#define _TlgChannelVal_TlgAvalue)
#define _tlgChannelVal_tlgArrayctype, pValues, cValues, inType, outType, hasOut, name, desc, tags, hasTags)
#define _tlgChannelVal_tlgAutovalue, name, desc, tags, hasTags)
#define _TlgChannelVal_TlgAutovalue)
#define _TlgChannelVal_TlgBvalue)
#define _tlgChannelVal_tlgBinaryctype, pValue, cbValue, inType, outType, hasOut, name, desc, tags, hasTags)
#define _tlgChannelVal_tlgBufferctype, pValue, inType, outType, hasOut, name, desc, tags, hasTags)
#define _tlgChannelVal_tlgChanneleventChannel) & 0 | (eventChannel)
#define _TlgChannelVal_TlgChannelvalue) & 0 | _TlgConst(Channel, value)
#define _tlgChannelVal_tlgCountedctype, pchValue, cchValue, inType, outType, hasOut, name, desc, tags, hasTags)
#define _TlgChannelVal_TlgCSvalue)
#define _TlgChannelVal_TlgCustAnnvalue)
#define _tlgChannelVal_tlgCustomctype, pValue, cbValue, protocol, bSchema, cbSchema, name, desc, tags, hasTags)
#define _TlgChannelVal_TlgCustomvalue)
#define _tlgChannelVal_tlgCustomAnnotkey, value)
#define _tlgChannelVal_tlgDesceventDescription)
#define _TlgChannelVal_TlgDescvalue)
#define _TlgChannelVal_TlgEvtTagvalue)
#define _TlgChannelVal_TlgFAvalue)
#define _tlgChannelVal_tlgFixedArrayctype, pValues, cValues, inType, outType, hasOut, name, desc, tags, hasTags)
#define _tlgChannelVal_tlgKeywordeventKeyword)
#define _TlgChannelVal_TlgKeywordvalue)
#define _tlgChannelVal_tlgLeveleventLevel)
#define _TlgChannelVal_TlgLevelvalue)
#define _tlgChannelVal_tlgOpcodeeventOpcode)
#define _TlgChannelVal_TlgOpcodevalue)
#define _TlgChannelVal_TlgPvalue)
#define _tlgChannelVal_tlgPackedDatactype, pValue, cbValue, dataDescType, hasType)
#define _tlgChannelVal_tlgPackedFieldctype, pValue, cbValue, inType, outType, hasOut, name, desc, tags, hasTags)
#define _tlgChannelVal_tlgPackedMetainType, outType, hasOut, name, desc, tags, hasTags)
#define _TlgChannelVal_TlgPDvalue)
#define _TlgChannelVal_TlgPFvalue)
#define _TlgChannelVal_TlgPMvalue)
#define _tlgChannelVal_tlgPszctype, pszValue, inType, outType, hasOut, name, desc, tags, hasTags)
#define _TlgChannelVal_TlgSvalue)
#define _tlgChannelVal_tlgScalarRefctype, value, inType, outType, hasOut, name, desc, tags, hasTags)
#define _tlgChannelVal_tlgScalarValctype, value, inType, outType, hasOut, name, desc, tags, hasTags)
#define _tlgChannelVal_tlgSidctype, pValue, name, desc, tags, hasTags)
#define _TlgChannelVal_TlgSidvalue)
#define _TlgChannelVal_TlgStringvalue)
#define _tlgChannelVal_tlgStructfieldCount, inType, name, desc, tags, hasTags)
#define _TlgChannelVal_TlgSzvalue)
#define _tlgChannelVal_tlgTageventTag)
#define _TlgChannelVal_TlgWszvalue)
#define _tlgCreate1Sz_char16_tpDesc, psz) _tlgCreate1Sz_wchar_t(pDesc, psz)
#define _tlgDataDescCount_tlgArrayctype, pValues, cValues, inType, outType, hasOut, name, desc, tags, hasTags) + 2
#define _tlgDataDescCount_tlgAutovalue, name, desc, tags, hasTags) + 1
#define _tlgDataDescCount_tlgBinaryctype, pValue, cbValue, inType, outType, hasOut, name, desc, tags, hasTags) + 2
#define _tlgDataDescCount_tlgBufferctype, pValue, inType, outType, hasOut, name, desc, tags, hasTags) + 2
#define _tlgDataDescCount_tlgChanneleventChannel)
#define _tlgDataDescCount_tlgCountedctype, pchValue, cchValue, inType, outType, hasOut, name, desc, tags, hasTags) + 2
#define _tlgDataDescCount_tlgCustomctype, pValue, cbValue, protocol, bSchema, cbSchema, name, desc, tags, hasTags) + 2
#define _tlgDataDescCount_tlgCustomAnnotkey, value)
#define _tlgDataDescCount_tlgDesceventDescription)
#define _tlgDataDescCount_tlgFixedArrayctype, pValues, cValues, inType, outType, hasOut, name, desc, tags, hasTags) + 1
#define _tlgDataDescCount_tlgKeywordeventKeyword)
#define _tlgDataDescCount_tlgLeveleventLevel)
#define _tlgDataDescCount_tlgOpcodeeventOpcode)
#define _tlgDataDescCount_tlgPackedDatactype, pValue, cbValue, dataDescType, hasType) + 1
#define _tlgDataDescCount_tlgPackedFieldctype, pValue, cbValue, inType, outType, hasOut, name, desc, tags, hasTags) + 1
#define _tlgDataDescCount_tlgPackedMetainType, outType, hasOut, name, desc, tags, hasTags)
#define _tlgDataDescCount_tlgPszctype, pszValue, inType, outType, hasOut, name, desc, tags, hasTags) + 1
#define _tlgDataDescCount_tlgScalarRefctype, value, inType, outType, hasOut, name, desc, tags, hasTags) + 1
#define _tlgDataDescCount_tlgScalarValctype, value, inType, outType, hasOut, name, desc, tags, hasTags) + 1
#define _tlgDataDescCount_tlgSidctype, pValue, name, desc, tags, hasTags) + 1
#define _tlgDataDescCount_tlgStructfieldCount, inType, name, desc, tags, hasTags) + _tlgCheckStructFieldCountCppOnly(fieldCount)
#define _tlgDataDescCount_tlgTageventTag)
#define _tlgDataDescCreate_tlgChanneln, eventChannel)
#define _TlgDataDescCreate_TlgChanneln, data_type, cVals)
#define _TlgDataDescCreate_TlgCustAnnn, data_type, cVals)
#define _tlgDataDescCreate_tlgCustomAnnotn, key, value)
#define _tlgDataDescCreate_tlgDescn, eventDescription)
#define _TlgDataDescCreate_TlgDescn, data_type, cVals)
#define _TlgDataDescCreate_TlgEvtTagn, data_type, cVals)
#define _tlgDataDescCreate_tlgKeywordn, eventKeyword)
#define _TlgDataDescCreate_TlgKeywordn, data_type, cVals)
#define _tlgDataDescCreate_tlgLeveln, eventLevel)
#define _TlgDataDescCreate_TlgLeveln, data_type, cVals)
#define _tlgDataDescCreate_tlgOpcoden, eventOpcode)
#define _TlgDataDescCreate_TlgOpcoden, data_type, cVals)
#define _tlgDataDescCreate_tlgPackedMetan, inType, outType, hasOut, name, desc, tags, hasTags) _tlg_AssertValidPackedMetadataInType(inType);
#define _TlgDataDescCreate_TlgPMn, data_type, cVals)
#define _tlgDataDescCreate_tlgStructn, fieldCount, inType, name, desc, tags, hasTags) _tlg_CASSERT((fieldCount) < 128, "TraceLoggingStruct fieldCount must be less than 128.");
#define _TlgDataDescCreate_TlgStructn, data_type, cVals)
#define _tlgDataDescCreate_tlgTagn, eventTag)
#define _tlgDefineProvider_functionWrapperBegin0functionPostfix)
#define _TlgDefineProvider_functionWrapperBegin0functionPostfix)
#define _TlgDescVal_TlgAname, desc)
#define _tlgDescVal_tlgArrayctype, pValues, cValues, inType, outType, hasOut, name, desc, tags, hasTags)
#define _tlgDescVal_tlgAutovalue, name, desc, tags, hasTags)
#define _TlgDescVal_TlgAutoname, desc)
#define _TlgDescVal_TlgBname, desc)
#define _tlgDescVal_tlgBinaryctype, pValue, cbValue, inType, outType, hasOut, name, desc, tags, hasTags)
#define _tlgDescVal_tlgBufferctype, pValue, inType, outType, hasOut, name, desc, tags, hasTags)
#define _tlgDescVal_tlgChanneleventChannel)
#define _TlgDescVal_TlgChannelname, desc)
#define _tlgDescVal_tlgCountedctype, pchValue, cchValue, inType, outType, hasOut, name, desc, tags, hasTags)
#define _TlgDescVal_TlgCSname, desc)
#define _TlgDescVal_TlgCustAnnname, desc)
#define _tlgDescVal_tlgCustomctype, pValue, cbValue, protocol, bSchema, cbSchema, name, desc, tags, hasTags)
#define _TlgDescVal_TlgCustomname, desc)
#define _tlgDescVal_tlgCustomAnnotkey, value)
#define _tlgDescVal_tlgDesceventDescription) eventDescription
#define _TlgDescVal_TlgDescname, desc) desc
#define _TlgDescVal_TlgEvtTagname, desc)
#define _TlgDescVal_TlgFAname, desc)
#define _tlgDescVal_tlgFixedArrayctype, pValues, cValues, inType, outType, hasOut, name, desc, tags, hasTags)
#define _tlgDescVal_tlgKeywordeventKeyword)
#define _TlgDescVal_TlgKeywordname, desc)
#define _tlgDescVal_tlgLeveleventLevel)
#define _TlgDescVal_TlgLevelname, desc)
#define _tlgDescVal_tlgOpcodeeventOpcode)
#define _TlgDescVal_TlgOpcodename, desc)
#define _TlgDescVal_TlgPname, desc)
#define _tlgDescVal_tlgPackedDatactype, pValue, cbValue, dataDescType, hasType)
#define _tlgDescVal_tlgPackedFieldctype, pValue, cbValue, inType, outType, hasOut, name, desc, tags, hasTags)
#define _tlgDescVal_tlgPackedMetainType, outType, hasOut, name, desc, tags, hasTags)
#define _TlgDescVal_TlgPDname, desc)
#define _TlgDescVal_TlgPFname, desc)
#define _TlgDescVal_TlgPMname, desc)
#define _tlgDescVal_tlgPszctype, pszValue, inType, outType, hasOut, name, desc, tags, hasTags)
#define _TlgDescVal_TlgSname, desc)
#define _tlgDescVal_tlgScalarRefctype, value, inType, outType, hasOut, name, desc, tags, hasTags)
#define _tlgDescVal_tlgScalarValctype, value, inType, outType, hasOut, name, desc, tags, hasTags)
#define _tlgDescVal_tlgSidctype, pValue, name, desc, tags, hasTags)
#define _TlgDescVal_TlgSidname, desc)
#define _TlgDescVal_TlgStringname, desc)
#define _tlgDescVal_tlgStructfieldCount, inType, name, desc, tags, hasTags)
#define _TlgDescVal_TlgStructname, desc)
#define _TlgDescVal_TlgSzname, desc)
#define _tlgDescVal_tlgTageventTag)
#define _TlgDescVal_TlgWszname, desc)
#define _tlgDT_imp0name, ...) name,,, 0
#define _tlgDT_imp1name, desc) name, L"" desc,, 0
#define _tlgDT_imp2name, desc, tags) name, L"" desc, tags, 1
#define _tlgDT_impBmacro, args) macro args
#define _TlgEvtTagEncodeeventTag) (((eventTag) & 0xfe00000) >> 21) /* Encode 0x0fe00000 bits into UINT16  */ | (((eventTag) & 0x1fc000) >> 6) /* Encode 0x001fc000 bits into UINT16  */ | (((eventTag) & 0x3fff) << 16) /* Trigger warning for 0x00003fff bits */ | (~(~(eventTag) | 0x0FFFFFFF))
#define _TlgEvtTagVal_TlgAvalue)
#define _tlgEvtTagVal_tlgArrayctype, pValues, cValues, inType, outType, hasOut, name, desc, tags, hasTags)
#define _tlgEvtTagVal_tlgAutovalue, name, desc, tags, hasTags)
#define _TlgEvtTagVal_TlgAutovalue)
#define _TlgEvtTagVal_TlgBvalue)
#define _tlgEvtTagVal_tlgBinaryctype, pValue, cbValue, inType, outType, hasOut, name, desc, tags, hasTags)
#define _tlgEvtTagVal_tlgBufferctype, pValue, inType, outType, hasOut, name, desc, tags, hasTags)
#define _tlgEvtTagVal_tlgChanneleventChannel)
#define _TlgEvtTagVal_TlgChannelvalue)
#define _tlgEvtTagVal_tlgCountedctype, pchValue, cchValue, inType, outType, hasOut, name, desc, tags, hasTags)
#define _TlgEvtTagVal_TlgCSvalue)
#define _TlgEvtTagVal_TlgCustAnnvalue)
#define _tlgEvtTagVal_tlgCustomctype, pValue, cbValue, protocol, bSchema, cbSchema, name, desc, tags, hasTags)
#define _TlgEvtTagVal_TlgCustomvalue)
#define _tlgEvtTagVal_tlgCustomAnnotkey, value)
#define _tlgEvtTagVal_tlgDesceventDescription)
#define _TlgEvtTagVal_TlgDescvalue)
#define _TlgEvtTagVal_TlgEvtTagvalue) | value
#define _TlgEvtTagVal_TlgFAvalue)
#define _tlgEvtTagVal_tlgFixedArrayctype, pValues, cValues, inType, outType, hasOut, name, desc, tags, hasTags)
#define _tlgEvtTagVal_tlgKeywordeventKeyword)
#define _TlgEvtTagVal_TlgKeywordvalue)
#define _tlgEvtTagVal_tlgLeveleventLevel)
#define _TlgEvtTagVal_TlgLevelvalue)
#define _tlgEvtTagVal_tlgOpcodeeventOpcode)
#define _TlgEvtTagVal_TlgOpcodevalue)
#define _TlgEvtTagVal_TlgPvalue)
#define _tlgEvtTagVal_tlgPackedDatactype, pValue, cbValue, dataDescType, hasType)
#define _tlgEvtTagVal_tlgPackedFieldctype, pValue, cbValue, inType, outType, hasOut, name, desc, tags, hasTags)
#define _tlgEvtTagVal_tlgPackedMetainType, outType, hasOut, name, desc, tags, hasTags)
#define _TlgEvtTagVal_TlgPDvalue)
#define _TlgEvtTagVal_TlgPFvalue)
#define _TlgEvtTagVal_TlgPMvalue)
#define _tlgEvtTagVal_tlgPszctype, pszValue, inType, outType, hasOut, name, desc, tags, hasTags)
#define _TlgEvtTagVal_TlgSvalue)
#define _tlgEvtTagVal_tlgScalarRefctype, value, inType, outType, hasOut, name, desc, tags, hasTags)
#define _tlgEvtTagVal_tlgScalarValctype, value, inType, outType, hasOut, name, desc, tags, hasTags)
#define _tlgEvtTagVal_tlgSidctype, pValue, name, desc, tags, hasTags)
#define _TlgEvtTagVal_TlgSidvalue)
#define _TlgEvtTagVal_TlgStringvalue)
#define _tlgEvtTagVal_tlgStructfieldCount, inType, name, desc, tags, hasTags)
#define _TlgEvtTagVal_TlgSzvalue)
#define _tlgEvtTagVal_tlgTageventTag) | (eventTag)
#define _TlgEvtTagVal_TlgWszvalue)
#define _tlgExpandProviderIda, b, c, d, e, f, g, h, i, j, k) { a, b, c, { d, e, f, g, h, i, j, k } }
#define _tlgExpandType_imp1typeVal) (typeVal), 1
#define _tlgExpandType_impBmacro, args) macro args
#define _tlgGetLengthSidpSid) (8u + (pSid) -> SubAuthorityCount * 4u)
#define _tlgInfoVals_00outType, tags)
#define _TlgInfoVals_00out_type, tags)
#define _tlgInfoVals_01outType, tags) | 128, 128, _tlgInfoVals_Tags(tags)
#define _TlgInfoVals_01out_type, tags) | 128, 128, _TlgInfoVals_Tags(tags)
#define _tlgInfoVals_10outType, tags) | 128, outType
#define _TlgInfoVals_10out_type, tags) | 128, out_type
#define _tlgInfoVals_11outType, tags) | 128, outType | 128, _tlgInfoVals_Tags(tags)
#define _TlgInfoVals_11out_type, tags) | 128, out_type | 128, _TlgInfoVals_Tags(tags)
#define _tlgInfoVals_tlgChanneln, eventChannel)
#define _TlgInfoVals_TlgChannelin_type, out_type, has_out, cVals, name, has_tags, tags)
#define _TlgInfoVals_TlgCustAnnin_type, out_type, has_out, cVals, name, has_tags, tags)
#define _tlgInfoVals_tlgCustomn, ctype, pValue, cbValue, protocol, bSchema, cbSchema, name, desc, tags, hasTags) , (name), ((protocol) & 31) | 96 _tlgInfoVals_0 ## hasTags(0, tags), cbSchema, { _tlg_FLATTEN bSchema }
#define _TlgInfoVals_TlgCustomin_type, out_type, has_out, cVals, name, has_tags, tags) , (name), ((in_type) & 31) | 96 _TlgInfoVals_0 ## has_tags(0, tags), has_out, { _TLG_FLATTEN out_type }
#define _tlgInfoVals_tlgCustomAnnotn, key, value)
#define _tlgInfoVals_tlgDescn, eventDescription)
#define _TlgInfoVals_TlgDescin_type, out_type, has_out, cVals, name, has_tags, tags)
#define _TlgInfoVals_TlgEvtTagin_type, out_type, has_out, cVals, name, has_tags, tags)
#define _tlgInfoVals_tlgKeywordn, eventKeyword)
#define _TlgInfoVals_TlgKeywordin_type, out_type, has_out, cVals, name, has_tags, tags)
#define _tlgInfoVals_tlgLeveln, eventLevel)
#define _TlgInfoVals_TlgLevelin_type, out_type, has_out, cVals, name, has_tags, tags)
#define _tlgInfoVals_tlgOpcoden, eventOpcode)
#define _TlgInfoVals_TlgOpcodein_type, out_type, has_out, cVals, name, has_tags, tags)
#define _tlgInfoVals_tlgPackedDatan, ctype, pValue, cbValue, dataDescType, hasType)
#define _TlgInfoVals_TlgPDin_type, out_type, has_out, cVals, name, has_tags, tags)
#define _tlgInfoVals_tlgSidn, ctype, pValue, name, desc, tags, hasTags) , (name), TlgInSID _tlgInfoVals_0 ## hasTags(outType, tags)
#define _tlgInfoVals_tlgStructn, fieldCount, inType, name, desc, tags, hasTags) , (name), inType _tlgInfoVals_1 ## hasTags(fieldCount, tags)
#define _TlgInfoVals_TlgStructin_type, out_type, has_out, cVals, name, has_tags, tags) , (name), in_type _TlgInfoVals_1 ## has_tags(cVals, tags)
#define _tlgInfoVals_tlgTagn, eventTag)
#define _tlgInfoVars_0n)
#define _TlgInfoVars_0n)
#define _tlgInfoVars_00n)
#define _TlgInfoVars_00n)
#define _tlgInfoVars_tlgChanneln, eventChannel)
#define _tlgInfoVars_tlgCustomAnnotn, key, value)
#define _tlgInfoVars_tlgDescn, eventDescription)
#define _tlgInfoVars_tlgKeywordn, eventKeyword)
#define _tlgInfoVars_tlgLeveln, eventLevel)
#define _tlgInfoVars_tlgOpcoden, eventOpcode)
#define _tlgInfoVars_tlgPackedDatan, ctype, pValue, cbValue, dataDescType, hasType)
#define _TlgInfoVars_TlgPDn, name, in_type, has_out, has_tags)
#define _tlgInfoVars_tlgTagn, eventTag)
#define _tlgKeywordConstn) n
#define _TlgKeywordVal_TlgAvalue)
#define _tlgKeywordVal_tlgArrayctype, pValues, cValues, inType, outType, hasOut, name, desc, tags, hasTags)
#define _tlgKeywordVal_tlgAutovalue, name, desc, tags, hasTags)
#define _TlgKeywordVal_TlgAutovalue)
#define _TlgKeywordVal_TlgBvalue)
#define _tlgKeywordVal_tlgBinaryctype, pValue, cbValue, inType, outType, hasOut, name, desc, tags, hasTags)
#define _tlgKeywordVal_tlgBufferctype, pValue, inType, outType, hasOut, name, desc, tags, hasTags)
#define _tlgKeywordVal_tlgChanneleventChannel)
#define _TlgKeywordVal_TlgChannelvalue)
#define _tlgKeywordVal_tlgCountedctype, pchValue, cchValue, inType, outType, hasOut, name, desc, tags, hasTags)
#define _TlgKeywordVal_TlgCSvalue)
#define _TlgKeywordVal_TlgCustAnnvalue)
#define _tlgKeywordVal_tlgCustomctype, pValue, cbValue, protocol, bSchema, cbSchema, name, desc, tags, hasTags)
#define _TlgKeywordVal_TlgCustomvalue)
#define _tlgKeywordVal_tlgCustomAnnotkey, value)
#define _tlgKeywordVal_tlgDesceventDescription)
#define _TlgKeywordVal_TlgDescvalue)
#define _TlgKeywordVal_TlgEvtTagvalue)
#define _TlgKeywordVal_TlgFAvalue)
#define _tlgKeywordVal_tlgFixedArrayctype, pValues, cValues, inType, outType, hasOut, name, desc, tags, hasTags)
#define _tlgKeywordVal_tlgKeywordeventKeyword) | (eventKeyword)
#define _TlgKeywordVal_TlgKeywordvalue) | _TlgConst(Keyword, value)
#define _tlgKeywordVal_tlgLeveleventLevel)
#define _TlgKeywordVal_TlgLevelvalue)
#define _tlgKeywordVal_tlgOpcodeeventOpcode)
#define _TlgKeywordVal_TlgOpcodevalue)
#define _TlgKeywordVal_TlgPvalue)
#define _tlgKeywordVal_tlgPackedDatactype, pValue, cbValue, dataDescType, hasType)
#define _tlgKeywordVal_tlgPackedFieldctype, pValue, cbValue, inType, outType, hasOut, name, desc, tags, hasTags)
#define _tlgKeywordVal_tlgPackedMetainType, outType, hasOut, name, desc, tags, hasTags)
#define _TlgKeywordVal_TlgPDvalue)
#define _TlgKeywordVal_TlgPFvalue)
#define _TlgKeywordVal_TlgPMvalue)
#define _tlgKeywordVal_tlgPszctype, pszValue, inType, outType, hasOut, name, desc, tags, hasTags)
#define _TlgKeywordVal_TlgSvalue)
#define _tlgKeywordVal_tlgScalarRefctype, value, inType, outType, hasOut, name, desc, tags, hasTags)
#define _tlgKeywordVal_tlgScalarValctype, value, inType, outType, hasOut, name, desc, tags, hasTags)
#define _tlgKeywordVal_tlgSidctype, pValue, name, desc, tags, hasTags)
#define _TlgKeywordVal_TlgSidvalue)
#define _TlgKeywordVal_TlgStringvalue)
#define _tlgKeywordVal_tlgStructfieldCount, inType, name, desc, tags, hasTags)
#define _TlgKeywordVal_TlgSzvalue)
#define _tlgKeywordVal_tlgTageventTag)
#define _TlgKeywordVal_TlgWszvalue)
#define _TlgLevelVal_TlgAvalue)
#define _tlgLevelVal_tlgArrayctype, pValues, cValues, inType, outType, hasOut, name, desc, tags, hasTags)
#define _tlgLevelVal_tlgAutovalue, name, desc, tags, hasTags)
#define _TlgLevelVal_TlgAutovalue)
#define _TlgLevelVal_TlgBvalue)
#define _tlgLevelVal_tlgBinaryctype, pValue, cbValue, inType, outType, hasOut, name, desc, tags, hasTags)
#define _tlgLevelVal_tlgBufferctype, pValue, inType, outType, hasOut, name, desc, tags, hasTags)
#define _tlgLevelVal_tlgChanneleventChannel)
#define _TlgLevelVal_TlgChannelvalue)
#define _tlgLevelVal_tlgCountedctype, pchValue, cchValue, inType, outType, hasOut, name, desc, tags, hasTags)
#define _TlgLevelVal_TlgCSvalue)
#define _TlgLevelVal_TlgCustAnnvalue)
#define _tlgLevelVal_tlgCustomctype, pValue, cbValue, protocol, bSchema, cbSchema, name, desc, tags, hasTags)
#define _TlgLevelVal_TlgCustomvalue)
#define _tlgLevelVal_tlgCustomAnnotkey, value)
#define _tlgLevelVal_tlgDesceventDescription)
#define _TlgLevelVal_TlgDescvalue)
#define _TlgLevelVal_TlgEvtTagvalue)
#define _TlgLevelVal_TlgFAvalue)
#define _tlgLevelVal_tlgFixedArrayctype, pValues, cValues, inType, outType, hasOut, name, desc, tags, hasTags)
#define _tlgLevelVal_tlgKeywordeventKeyword)
#define _TlgLevelVal_TlgKeywordvalue)
#define _tlgLevelVal_tlgLeveleventLevel) & 0 | (eventLevel)
#define _TlgLevelVal_TlgLevelvalue) & 0 | _TlgConst(Level, value)
#define _tlgLevelVal_tlgOpcodeeventOpcode)
#define _TlgLevelVal_TlgOpcodevalue)
#define _TlgLevelVal_TlgPvalue)
#define _tlgLevelVal_tlgPackedDatactype, pValue, cbValue, dataDescType, hasType)
#define _tlgLevelVal_tlgPackedFieldctype, pValue, cbValue, inType, outType, hasOut, name, desc, tags, hasTags)
#define _tlgLevelVal_tlgPackedMetainType, outType, hasOut, name, desc, tags, hasTags)
#define _TlgLevelVal_TlgPDvalue)
#define _TlgLevelVal_TlgPFvalue)
#define _TlgLevelVal_TlgPMvalue)
#define _tlgLevelVal_tlgPszctype, pszValue, inType, outType, hasOut, name, desc, tags, hasTags)
#define _TlgLevelVal_TlgSvalue)
#define _tlgLevelVal_tlgScalarRefctype, value, inType, outType, hasOut, name, desc, tags, hasTags)
#define _tlgLevelVal_tlgScalarValctype, value, inType, outType, hasOut, name, desc, tags, hasTags)
#define _tlgLevelVal_tlgSidctype, pValue, name, desc, tags, hasTags)
#define _TlgLevelVal_TlgSidvalue)
#define _TlgLevelVal_TlgStringvalue)
#define _tlgLevelVal_tlgStructfieldCount, inType, name, desc, tags, hasTags)
#define _TlgLevelVal_TlgSzvalue)
#define _tlgLevelVal_tlgTageventTag)
#define _TlgLevelVal_TlgWszvalue)
#define _tlgNDT_imp0value, ...) # value,,, 0
#define _tlgNDT_imp1value, name) name,,, 0
#define _tlgNDT_imp2value, name, desc) name, L"" desc,, 0
#define _tlgNDT_imp3value, name, desc, tags) name, L"" desc, tags, 1
#define _tlgNDT_impBmacro, args) macro args
#define _TlgOpcodeVal_TlgAvalue)
#define _tlgOpcodeVal_tlgArrayctype, pValues, cValues, inType, outType, hasOut, name, desc, tags, hasTags)
#define _tlgOpcodeVal_tlgAutovalue, name, desc, tags, hasTags)
#define _TlgOpcodeVal_TlgAutovalue)
#define _TlgOpcodeVal_TlgBvalue)
#define _tlgOpcodeVal_tlgBinaryctype, pValue, cbValue, inType, outType, hasOut, name, desc, tags, hasTags)
#define _tlgOpcodeVal_tlgBufferctype, pValue, inType, outType, hasOut, name, desc, tags, hasTags)
#define _tlgOpcodeVal_tlgChanneleventChannel)
#define _TlgOpcodeVal_TlgChannelvalue)
#define _tlgOpcodeVal_tlgCountedctype, pchValue, cchValue, inType, outType, hasOut, name, desc, tags, hasTags)
#define _TlgOpcodeVal_TlgCSvalue)
#define _TlgOpcodeVal_TlgCustAnnvalue)
#define _tlgOpcodeVal_tlgCustomctype, pValue, cbValue, protocol, bSchema, cbSchema, name, desc, tags, hasTags)
#define _TlgOpcodeVal_TlgCustomvalue)
#define _tlgOpcodeVal_tlgCustomAnnotkey, value)
#define _tlgOpcodeVal_tlgDesceventDescription)
#define _TlgOpcodeVal_TlgDescvalue)
#define _TlgOpcodeVal_TlgEvtTagvalue)
#define _TlgOpcodeVal_TlgFAvalue)
#define _tlgOpcodeVal_tlgFixedArrayctype, pValues, cValues, inType, outType, hasOut, name, desc, tags, hasTags)
#define _tlgOpcodeVal_tlgKeywordeventKeyword)
#define _TlgOpcodeVal_TlgKeywordvalue)
#define _tlgOpcodeVal_tlgLeveleventLevel)
#define _TlgOpcodeVal_TlgLevelvalue)
#define _tlgOpcodeVal_tlgOpcodeeventOpcode) & 0 | (eventOpcode)
#define _TlgOpcodeVal_TlgOpcodevalue) & 0 | _TlgConst(Opcode, value)
#define _TlgOpcodeVal_TlgPvalue)
#define _tlgOpcodeVal_tlgPackedDatactype, pValue, cbValue, dataDescType, hasType)
#define _tlgOpcodeVal_tlgPackedFieldctype, pValue, cbValue, inType, outType, hasOut, name, desc, tags, hasTags)
#define _tlgOpcodeVal_tlgPackedMetainType, outType, hasOut, name, desc, tags, hasTags)
#define _TlgOpcodeVal_TlgPDvalue)
#define _TlgOpcodeVal_TlgPFvalue)
#define _TlgOpcodeVal_TlgPMvalue)
#define _tlgOpcodeVal_tlgPszctype, pszValue, inType, outType, hasOut, name, desc, tags, hasTags)
#define _TlgOpcodeVal_TlgSvalue)
#define _tlgOpcodeVal_tlgScalarRefctype, value, inType, outType, hasOut, name, desc, tags, hasTags)
#define _tlgOpcodeVal_tlgScalarValctype, value, inType, outType, hasOut, name, desc, tags, hasTags)
#define _tlgOpcodeVal_tlgSidctype, pValue, name, desc, tags, hasTags)
#define _TlgOpcodeVal_TlgSidvalue)
#define _TlgOpcodeVal_TlgStringvalue)
#define _tlgOpcodeVal_tlgStructfieldCount, inType, name, desc, tags, hasTags)
#define _TlgOpcodeVal_TlgSzvalue)
#define _tlgOpcodeVal_tlgTageventTag)
#define _TlgOpcodeVal_TlgWszvalue)
#define _tlgProvVal_tlgOptiontraitType, ctype, value) , 3 + sizeof (ctype), traitType, _tlg_FLATTEN value
#define _TlgSegMetadataEnd 0
#define _TlgTempVars_TlgChanneln, data_type, in_type, value, cVals)
#define _TlgTempVars_TlgCustAnnn, data_type, in_type, value, cVals)
#define _TlgTempVars_TlgDescn, data_type, in_type, value, cVals)
#define _TlgTempVars_TlgEvtTagn, data_type, in_type, value, cVals)
#define _TlgTempVars_TlgKeywordn, data_type, in_type, value, cVals)
#define _TlgTempVars_TlgLeveln, data_type, in_type, value, cVals)
#define _TlgTempVars_TlgOpcoden, data_type, in_type, value, cVals)
#define _TlgTempVars_TlgPMn, data_type, in_type, value, cVals) _TLG_AssertValidPackedMetadataInType(in_type);
#define _TlgTempVars_TlgStructn, data_type, in_type, value, cVals) _TLG_CASSERT(cVals < 128, "Struct size must be less than 128");
#define _tlgValidateProviderId_parenthesisCALLmacro, args) macro args
#define _tlgValidateProviderId_presentCALLmacro, args) macro args
#define _tlgWriteEx 0

