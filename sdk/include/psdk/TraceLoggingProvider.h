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

#define _tlg_CALL __stdcall
#define _TLG_DEBUG 1
#define _tlg_ENABLE_TraceLoggingWriteEx 1
#define _TLG_ENABLE_TraceLoggingWriteEx WINVER >= 0x0601
#define _tlg_EVENT_METADATA_PREAMBLE 0xB
#define _TLG_EVENT_METADATA_PREAMBLE 0xB
#define _tlg_EVENT_SET_INFORMATION_suffix TLG_EVENT_SET_INFORMATION
#define _tlg_EXTERN_C extern
#define _tlg_EXTERN_C_CONST const
#define _tlg_FLATTEN...) __VA_ARGS__
#define _TLG_FLATTEN...) __VA_ARGS__
#define _tlg_IS_PARENTHESIZED_imp0...) 1
#define _TLG_IS_PARENTHESIZED_imp0...) 1
#define _tlg_IS_PARENTHESIZED_imp1 1,
#define _TLG_IS_PARENTHESIZED_imp1 0
#define _tlg_IS_PARENTHESIZED_imp_tlg_IS_PARENTHESIZED_imp0 0,
#define _TLG_IS_PARENTHESIZED_imp_TLG_IS_PARENTHESIZED_imp0 0,
#define _tlg_MetaDataDescCount 2
#ifdef _WIN64
#define _tlg_MetadataFlags 1
#else
#define _tlg_MetadataFlags 0
#endif
#define _tlg_MetadataMagic (-4933124757307257210LL)
#define _tlg_MetadataSignature 0x30575445
#define _tlg_MetadataVersion 0
#define _tlg_PARENTHESIZE...) (__VA_ARGS__)
#define _TLG_PARENTHESIZE...) (__VA_ARGS__)
#define _tlg_PROVIDER_METADATA_PREAMBLE 16
#define _TLG_PROVIDER_METADATA_PREAMBLE 16
#define _tlgApplyArgs_UNWRAP...) __VA_ARGS__
#define _TlgChannelVal_TlgStruct...)
#define _tlgDefineProvider_functionWrapperEnd0
#define _TlgDefineProvider_functionWrapperEnd0
#define _tlgDefineProvider_functionWrapperEnd1 }
#define _TlgDefineProvider_functionWrapperEnd1 }
#define _TlgEvtTagDecl...)
#define _tlgEvtTagType UINT16
#define _TlgEvtTagType UINT16
#ifdef _WIN64
#define _TlgFlagsVal 1
#else
#define _TlgFlagsVal 0
#endif
#define _tlgInfoVals_0...)
#define _TlgInfoVals_0...)
#define _TlgKeywordVal_TlgStruct...)
#define _TlgLevelVal_TlgStruct...)
#define _TlgMagicVal (-4933124757307257210LL)
#define _TlgOpcodeVal_TlgStruct...)
#define _tlgPragmaUtf8Begin
#define _tlgPragmaUtf8End
#define _tlgSegMetadataBegin 0
#define _TlgSegMetadataBegin 0
#define _TlgSeqEnd
#define _TlgSeqStart
#define _TlgSigVal 0x30575445
#define _TlgSkipVal 2
#define _tlgValidateProviderId_parenthesis1...)
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
#define TLG_PAGED_SEGMENT "PAGE"
#define TLG_PFORCEINLINE 0
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

#define _TlgSegMetadataEvents 0
#define TraceLoggingSetInformation 0

#define _TlgSegMetadataProviders 0
#define _tlgWriteTransfer 0

#define _TlgSegMetadataEnd 0
#define _tlgWriteEx 0

#define _TlgPragmaUtf8Begin 0
#define _tlgSegMetadataEvents 0

#define _TlgPragmaUtf8End 0
#define _tlgSegMetadataProviders 0

#define _tlgSegMetadataEnd 0

#define _tlgValidateProviderId_parenthesis0 0

#define _tlg_ASSERT(exp, str) ((void) (!(exp) ? (__annotation(L"Debug", L"AssertFail", L"TraceLogging: " _tlg_LSTRINGIZE(exp) L" : " str), TLG_RAISEASSERTIONFAILURE(), 0) : 0))
#define _TLG_ASSERT(exp, str) ((void) (!(exp) ? (__annotation(L"Debug", L"AssertFail", L"TraceLogging: " L ## # exp L" : " L ## str), DbgRaiseAssertionFailure(), 0) : 0))
#define _tlg_AssertValidPackedMetadataInType(inType) _tlg_CASSERT(((inType) & _TlgInTypeMask) != _TlgInPOINTER_unsupported && ((inType) & _TlgInTypeMask) != _TlgInSTRUCT && ((inType) | _TlgInTypeMask | TlgInVcount) == (_TlgInTypeMask | TlgInVcount) && (inType) != (TlgInBINARY | TlgInVcount), "Invalid packed metadata in_type: " # inType)
#define _TLG_AssertValidPackedMetadataInType(in_type) _TLG_CASSERT(((in_type) & _TlgInTypeMask) != _TlgInPOINTER_unsupported && ((in_type) & _TlgInTypeMask) != _TlgInSTRUCT && ((in_type) | _TlgInTypeMask | TlgInVcount) == (_TlgInTypeMask | TlgInVcount) && (in_type) != (TlgInBINARY | TlgInVcount), "Invalid packed metadata in_type: " # in_type)
#define _tlg_AssertValidPackedMetadataInTypeCppOnly(inType)
#define _TLG_AssertValidPackedMetadataInTypeCppOnly(in_type)
#define _tlg_CASSERT(exp, str) _Static_assert(exp, str)
#define _TLG_CASSERT(exp, str) C_ASSERT(exp)
#define _tlg_CAT(a, ...) _tlg_CAT_imp(a, __VA_ARGS__)
#define _TLG_CAT(a, ...) _TLG_CAT_imp(a, __VA_ARGS__)
#define _tlg_CAT_imp(a, ...) a ## __VA_ARGS__
#define _TLG_CAT_imp(a, ...) a ## __VA_ARGS__
#define _tlg_FASTFAIL(code) __fastfail(code)
#define _tlg_FOR_imp(n, fnAndArgs) _tlg_PASTE2(_tlg_FOR_imp, n) fnAndArgs
#define _TLG_FOR_imp(n, fnAndArgs) _TLG_PASTE(_TLG_FOR_imp, n) fnAndArgs
#define _tlg_FOR_imp0(f, ...)
#define _TLG_FOR_imp0(fn)
#define _tlg_FOR_imp1(f, a0) f(0, a0)
#define _TLG_FOR_imp1(fn, arg, ...) _TLG_FOR_impN(0, fn, arg, fn, __VA_ARGS__)
#define _tlg_FOR_imp10(f, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9) f(0, a0) f(1, a1) f(2, a2) f(3, a3) f(4, a4) f(5, a5) f(6, a6) f(7, a7) f(8, a8) f(9, a9)
#define _TLG_FOR_imp10(fn, arg, ...) _TLG_FOR_impN(9, fn, arg, fn, __VA_ARGS__)
#define _tlg_FOR_imp11(f, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10) f(0, a0) f(1, a1) f(2, a2) f(3, a3) f(4, a4) f(5, a5) f(6, a6) f(7, a7) f(8, a8) f(9, a9) f(10, a10)
#define _TLG_FOR_imp11(fn, arg, ...) _TLG_FOR_impN(10, fn, arg, fn, __VA_ARGS__)
#define _tlg_FOR_imp12(f, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11) f(0, a0) f(1, a1) f(2, a2) f(3, a3) f(4, a4) f(5, a5) f(6, a6) f(7, a7) f(8, a8) f(9, a9) f(10, a10) f(11, a11)
#define _TLG_FOR_imp12(fn, arg, ...) _TLG_FOR_impN(11, fn, arg, fn, __VA_ARGS__)
#define _tlg_FOR_imp13(f, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12) f(0, a0) f(1, a1) f(2, a2) f(3, a3) f(4, a4) f(5, a5) f(6, a6) f(7, a7) f(8, a8) f(9, a9) f(10, a10) f(11, a11) f(12, a12)
#define _TLG_FOR_imp13(fn, arg, ...) _TLG_FOR_impN(12, fn, arg, fn, __VA_ARGS__)
#define _tlg_FOR_imp14(f, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13) f(0, a0) f(1, a1) f(2, a2) f(3, a3) f(4, a4) f(5, a5) f(6, a6) f(7, a7) f(8, a8) f(9, a9) f(10, a10) f(11, a11) f(12, a12) f(13, a13)
#define _TLG_FOR_imp14(fn, arg, ...) _TLG_FOR_impN(13, fn, arg, fn, __VA_ARGS__)
#define _tlg_FOR_imp15(f, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14) f(0, a0) f(1, a1) f(2, a2) f(3, a3) f(4, a4) f(5, a5) f(6, a6) f(7, a7) f(8, a8) f(9, a9) f(10, a10) f(11, a11) f(12, a12) f(13, a13) f(14, a14)
#define _TLG_FOR_imp15(fn, arg, ...) _TLG_FOR_impN(14, fn, arg, fn, __VA_ARGS__)
#define _tlg_FOR_imp16(f, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15) f(0, a0) f(1, a1) f(2, a2) f(3, a3) f(4, a4) f(5, a5) f(6, a6) f(7, a7) f(8, a8) f(9, a9) f(10, a10) f(11, a11) f(12, a12) f(13, a13) f(14, a14) f(15, a15)
#define _TLG_FOR_imp16(fn, arg, ...) _TLG_FOR_impN(15, fn, arg, fn, __VA_ARGS__)
#define _tlg_FOR_imp17(f, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16) f(0, a0) f(1, a1) f(2, a2) f(3, a3) f(4, a4) f(5, a5) f(6, a6) f(7, a7) f(8, a8) f(9, a9) f(10, a10) f(11, a11) f(12, a12) f(13, a13) f(14, a14) f(15, a15) f(16, a16)
#define _TLG_FOR_imp17(fn, arg, ...) _TLG_FOR_impN(16, fn, arg, fn, __VA_ARGS__)
#define _tlg_FOR_imp18(f, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17) f(0, a0) f(1, a1) f(2, a2) f(3, a3) f(4, a4) f(5, a5) f(6, a6) f(7, a7) f(8, a8) f(9, a9) f(10, a10) f(11, a11) f(12, a12) f(13, a13) f(14, a14) f(15, a15) f(16, a16) f(17, a17)
#define _TLG_FOR_imp18(fn, arg, ...) _TLG_FOR_impN(17, fn, arg, fn, __VA_ARGS__)
#define _tlg_FOR_imp19(f, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17, a18) f(0, a0) f(1, a1) f(2, a2) f(3, a3) f(4, a4) f(5, a5) f(6, a6) f(7, a7) f(8, a8) f(9, a9) f(10, a10) f(11, a11) f(12, a12) f(13, a13) f(14, a14) f(15, a15) f(16, a16) f(17, a17) f(18, a18)
#define _TLG_FOR_imp19(fn, arg, ...) _TLG_FOR_impN(18, fn, arg, fn, __VA_ARGS__)
#define _tlg_FOR_imp2(f, a0, a1) f(0, a0) f(1, a1)
#define _TLG_FOR_imp2(fn, arg, ...) _TLG_FOR_impN(1, fn, arg, fn, __VA_ARGS__)
#define _tlg_FOR_imp20(f, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17, a18, a19) f(0, a0) f(1, a1) f(2, a2) f(3, a3) f(4, a4) f(5, a5) f(6, a6) f(7, a7) f(8, a8) f(9, a9) f(10, a10) f(11, a11) f(12, a12) f(13, a13) f(14, a14) f(15, a15) f(16, a16) f(17, a17) f(18, a18) f(19, a19)
#define _TLG_FOR_imp20(fn, arg, ...) _TLG_FOR_impN(19, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp21(fn, arg, ...) _TLG_FOR_impN(20, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp22(fn, arg, ...) _TLG_FOR_impN(21, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp23(fn, arg, ...) _TLG_FOR_impN(22, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp24(fn, arg, ...) _TLG_FOR_impN(23, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp25(fn, arg, ...) _TLG_FOR_impN(24, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp26(fn, arg, ...) _TLG_FOR_impN(25, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp27(fn, arg, ...) _TLG_FOR_impN(26, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp28(fn, arg, ...) _TLG_FOR_impN(27, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp29(fn, arg, ...) _TLG_FOR_impN(28, fn, arg, fn, __VA_ARGS__)
#define _tlg_FOR_imp3(f, a0, a1, a2) f(0, a0) f(1, a1) f(2, a2)
#define _TLG_FOR_imp3(fn, arg, ...) _TLG_FOR_impN(2, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp30(fn, arg, ...) _TLG_FOR_impN(29, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp31(fn, arg, ...) _TLG_FOR_impN(30, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp32(fn, arg, ...) _TLG_FOR_impN(31, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp33(fn, arg, ...) _TLG_FOR_impN(32, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp34(fn, arg, ...) _TLG_FOR_impN(33, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp35(fn, arg, ...) _TLG_FOR_impN(34, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp36(fn, arg, ...) _TLG_FOR_impN(35, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp37(fn, arg, ...) _TLG_FOR_impN(36, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp38(fn, arg, ...) _TLG_FOR_impN(37, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp39(fn, arg, ...) _TLG_FOR_impN(38, fn, arg, fn, __VA_ARGS__)
#define _tlg_FOR_imp4(f, a0, a1, a2, a3) f(0, a0) f(1, a1) f(2, a2) f(3, a3)
#define _TLG_FOR_imp4(fn, arg, ...) _TLG_FOR_impN(3, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp40(fn, arg, ...) _TLG_FOR_impN(39, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp41(fn, arg, ...) _TLG_FOR_impN(40, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp42(fn, arg, ...) _TLG_FOR_impN(41, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp43(fn, arg, ...) _TLG_FOR_impN(42, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp44(fn, arg, ...) _TLG_FOR_impN(43, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp45(fn, arg, ...) _TLG_FOR_impN(44, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp46(fn, arg, ...) _TLG_FOR_impN(45, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp47(fn, arg, ...) _TLG_FOR_impN(46, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp48(fn, arg, ...) _TLG_FOR_impN(47, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp49(fn, arg, ...) _TLG_FOR_impN(48, fn, arg, fn, __VA_ARGS__)
#define _tlg_FOR_imp5(f, a0, a1, a2, a3, a4) f(0, a0) f(1, a1) f(2, a2) f(3, a3) f(4, a4)
#define _TLG_FOR_imp5(fn, arg, ...) _TLG_FOR_impN(4, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp50(fn, arg, ...) _TLG_FOR_impN(49, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp51(fn, arg, ...) _TLG_FOR_impN(50, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp52(fn, arg, ...) _TLG_FOR_impN(51, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp53(fn, arg, ...) _TLG_FOR_impN(52, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp54(fn, arg, ...) _TLG_FOR_impN(53, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp55(fn, arg, ...) _TLG_FOR_impN(54, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp56(fn, arg, ...) _TLG_FOR_impN(55, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp57(fn, arg, ...) _TLG_FOR_impN(56, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp58(fn, arg, ...) _TLG_FOR_impN(57, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp59(fn, arg, ...) _TLG_FOR_impN(58, fn, arg, fn, __VA_ARGS__)
#define _tlg_FOR_imp6(f, a0, a1, a2, a3, a4, a5) f(0, a0) f(1, a1) f(2, a2) f(3, a3) f(4, a4) f(5, a5)
#define _TLG_FOR_imp6(fn, arg, ...) _TLG_FOR_impN(5, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp60(fn, arg, ...) _TLG_FOR_impN(59, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp61(fn, arg, ...) _TLG_FOR_impN(60, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp62(fn, arg, ...) _TLG_FOR_impN(61, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp63(fn, arg, ...) _TLG_FOR_impN(62, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp64(fn, arg, ...) _TLG_FOR_impN(63, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp65(fn, arg, ...) _TLG_FOR_impN(64, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp66(fn, arg, ...) _TLG_FOR_impN(65, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp67(fn, arg, ...) _TLG_FOR_impN(66, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp68(fn, arg, ...) _TLG_FOR_impN(67, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp69(fn, arg, ...) _TLG_FOR_impN(68, fn, arg, fn, __VA_ARGS__)
#define _tlg_FOR_imp7(f, a0, a1, a2, a3, a4, a5, a6) f(0, a0) f(1, a1) f(2, a2) f(3, a3) f(4, a4) f(5, a5) f(6, a6)
#define _TLG_FOR_imp7(fn, arg, ...) _TLG_FOR_impN(6, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp70(fn, arg, ...) _TLG_FOR_impN(69, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp71(fn, arg, ...) _TLG_FOR_impN(70, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp72(fn, arg, ...) _TLG_FOR_impN(71, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp73(fn, arg, ...) _TLG_FOR_impN(72, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp74(fn, arg, ...) _TLG_FOR_impN(73, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp75(fn, arg, ...) _TLG_FOR_impN(74, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp76(fn, arg, ...) _TLG_FOR_impN(75, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp77(fn, arg, ...) _TLG_FOR_impN(76, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp78(fn, arg, ...) _TLG_FOR_impN(77, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp79(fn, arg, ...) _TLG_FOR_impN(78, fn, arg, fn, __VA_ARGS__)
#define _tlg_FOR_imp8(f, a0, a1, a2, a3, a4, a5, a6, a7) f(0, a0) f(1, a1) f(2, a2) f(3, a3) f(4, a4) f(5, a5) f(6, a6) f(7, a7)
#define _TLG_FOR_imp8(fn, arg, ...) _TLG_FOR_impN(7, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp80(fn, arg, ...) _TLG_FOR_impN(79, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp81(fn, arg, ...) _TLG_FOR_impN(80, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp82(fn, arg, ...) _TLG_FOR_impN(81, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp83(fn, arg, ...) _TLG_FOR_impN(82, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp84(fn, arg, ...) _TLG_FOR_impN(83, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp85(fn, arg, ...) _TLG_FOR_impN(84, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp86(fn, arg, ...) _TLG_FOR_impN(85, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp87(fn, arg, ...) _TLG_FOR_impN(86, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp88(fn, arg, ...) _TLG_FOR_impN(87, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp89(fn, arg, ...) _TLG_FOR_impN(88, fn, arg, fn, __VA_ARGS__)
#define _tlg_FOR_imp9(f, a0, a1, a2, a3, a4, a5, a6, a7, a8) f(0, a0) f(1, a1) f(2, a2) f(3, a3) f(4, a4) f(5, a5) f(6, a6) f(7, a7) f(8, a8)
#define _TLG_FOR_imp9(fn, arg, ...) _TLG_FOR_impN(8, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp90(fn, arg, ...) _TLG_FOR_impN(89, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp91(fn, arg, ...) _TLG_FOR_impN(90, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp92(fn, arg, ...) _TLG_FOR_impN(91, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp93(fn, arg, ...) _TLG_FOR_impN(92, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp94(fn, arg, ...) _TLG_FOR_impN(93, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp95(fn, arg, ...) _TLG_FOR_impN(94, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp96(fn, arg, ...) _TLG_FOR_impN(95, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp97(fn, arg, ...) _TLG_FOR_impN(96, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp98(fn, arg, ...) _TLG_FOR_impN(97, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_imp99(fn, arg, ...) _TLG_FOR_impN(98, fn, arg, fn, __VA_ARGS__)
#define _TLG_FOR_impN(n, fn, arg, ...) fn(n, arg) _TLG_PASTE(_TLG_FOR_imp, n) _TLG_FLATTEN((__VA_ARGS__))
#define _tlg_LSTRINGIZE(x) _tlg_LSTRINGIZE_imp(x)
#define _tlg_LSTRINGIZE_imp(x) _tlg_PASTE2(L, # x)
#define _tlg_NARGS_imp(is_empty, args) _tlg_PASTE2(_tlg_NARGS_imp, is_empty) args
#define _TLG_NARGS_imp(is_empty, args) _TLG_PASTE(_TLG_NARGS_imp, is_empty) args
#define _tlg_NARGS_imp1() 0
#define _TLG_NARGS_imp1() 0
#define _tlg_NARGS_imp2(a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17, a18, a19, a20, a21, a22, a23, a24, a25, a26, a27, a28, a29, a30, a31, a32, a33, a34, a35, a36, a37, a38, a39, a40, a41, a42, a43, a44, a45, a46, a47, a48, a49, a50, a51, a52, a53, a54, a55, a56, a57, a58, a59, a60, a61, a62, a63, a64, a65, a66, a67, a68, a69, a70, a71, a72, a73, a74, a75, a76, a77, a78, a79, a80, a81, a82, a83, a84, a85, a86, a87, a88, a89, a90, a91, a92, a93, a94, a95, a96, a97, a98, a99, size, ...) size
#define _TLG_NARGS_imp2(a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17, a18, a19, a20, a21, a22, a23, a24, a25, a26, a27, a28, a29, a30, a31, a32, a33, a34, a35, a36, a37, a38, a39, a40, a41, a42, a43, a44, a45, a46, a47, a48, a49, a50, a51, a52, a53, a54, a55, a56, a57, a58, a59, a60, a61, a62, a63, a64, a65, a66, a67, a68, a69, a70, a71, a72, a73, a74, a75, a76, a77, a78, a79, a80, a81, a82, a83, a84, a85, a86, a87, a88, a89, a90, a91, a92, a93, a94, a95, a96, a97, a98, a99, size, ...) size
#define _TLG_PASTE(a, b) _TLG_PASTE_imp(a, b)
#define _tlg_PASTE2(a, b) _tlg_PASTE2_imp(a, b)
#define _tlg_PASTE2_imp(a, b) a ## b
#define _tlg_PASTE3(a, b, c) _tlg_PASTE3_imp(a, b, c)
#define _tlg_PASTE3_imp(a, b, c) a ## b ## _ ## c
#define _TLG_PASTE_imp(a, b) a ## b
#define _tlg_SPLIT(cond, ...) _tlg_SPLIT_imp(cond, (__VA_ARGS__))
#define _TLG_SPLIT(cond, ...) _TLG_SPLIT_imp(cond, (__VA_ARGS__))
#define _tlg_SPLIT_imp(cond, args) _tlg_PASTE2(_tlg_SPLIT_imp, cond) args
#define _TLG_SPLIT_imp(cond, args) _TLG_PASTE(_TLG_SPLIT_imp, cond) args
#define _tlg_SPLIT_imp0(false_val, ...) false_val
#define _TLG_SPLIT_imp0(false_val, ...) false_val
#define _tlg_SPLIT_imp1(false_val, ...) __VA_ARGS__
#define _TLG_SPLIT_imp1(false_val, ...) __VA_ARGS__
#define _tlg_STRINGIZE(x) _tlg_STRINGIZE_imp(x)
#define _TLG_STRINGIZE(x) _TLG_STRINGIZE_imp(x)
#define _tlg_STRINGIZE_imp(x) # x
#define _TLG_STRINGIZE_imp(x) # x
#define _tlgAnnotationFunc_imp(use_annotationFunc, storageVariable) _tlg_PASTE2(_tlgAnnotationFunc_imp, use_annotationFunc) (storageVariable)
#define _TlgAnnotationFunc_imp(use_annotationFunc, storageVariable) _TLG_PASTE(_TlgAnnotationFunc_imp, use_annotationFunc) (storageVariable)
#define _tlgAnnotationFunc_imp0(storageVariable)
#define _TlgAnnotationFunc_imp0(storageVariable) 0
#define _tlgAnnotes(n, args) _tlgApplyArgs(_tlgAnnotes, args)
#define _TlgAnnotes_TlgA(name, desc) L"|" L ## # name L"=" desc
#define _tlgAnnotes_tlgArray(ctype, pValues, cValues, inType, outType, hasOut, name, desc, tags, hasTags) L"|" _tlg_LSTRINGIZE(name) L"=" desc
#define _tlgAnnotes_tlgAuto(value, name, desc, tags, hasTags) L"|" _tlg_LSTRINGIZE(name) L"=" desc
#define _TlgAnnotes_TlgAuto(name, desc) L"|" L ## # name L"=" desc
#define _TlgAnnotes_TlgB(name, desc) L"|" L ## # name L"=" desc
#define _tlgAnnotes_tlgBinary(ctype, pValue, cbValue, inType, outType, hasOut, name, desc, tags, hasTags) L"|" _tlg_LSTRINGIZE(name) L"=" desc
#define _tlgAnnotes_tlgBuffer(ctype, pValue, inType, outType, hasOut, name, desc, tags, hasTags) L"|" _tlg_LSTRINGIZE(name) L"=" desc
#define _tlgAnnotes_tlgChannel(eventChannel)
#define _TlgAnnotes_TlgChannel(name, desc)
#define _tlgAnnotes_tlgCounted(ctype, pchValue, cchValue, inType, outType, hasOut, name, desc, tags, hasTags) L"|" _tlg_LSTRINGIZE(name) L"=" desc
#define _TlgAnnotes_TlgCS(name, desc) L"|" L ## # name L"=" desc
#define _TlgAnnotes_TlgCustAnn(name, desc) L"|" L ## name L"=" desc
#define _tlgAnnotes_tlgCustom(ctype, pValue, cbValue, protocol, bSchema, cbSchema, name, desc, tags, hasTags) L"|" _tlg_LSTRINGIZE(name) L"=" desc
#define _TlgAnnotes_TlgCustom(name, desc) L"|" L ## # name L"=" desc
#define _tlgAnnotes_tlgCustomAnnot(key, value) L"|" key L"=" value
#define _tlgAnnotes_tlgDesc(eventDescription)
#define _TlgAnnotes_TlgDesc(name, desc)
#define _TlgAnnotes_TlgEvtTag(name, desc)
#define _TlgAnnotes_TlgFA(name, desc) L"|" L ## # name L"=" desc
#define _tlgAnnotes_tlgFixedArray(ctype, pValues, cValues, inType, outType, hasOut, name, desc, tags, hasTags) L"|" _tlg_LSTRINGIZE(name) L"=" desc
#define _tlgAnnotes_tlgKeyword(eventKeyword)
#define _TlgAnnotes_TlgKeyword(name, desc)
#define _tlgAnnotes_tlgLevel(eventLevel)
#define _TlgAnnotes_TlgLevel(name, desc)
#define _tlgAnnotes_tlgOpcode(eventOpcode)
#define _TlgAnnotes_TlgOpcode(name, desc)
#define _TlgAnnotes_TlgP(name, desc) L"|" L ## # name L"=" desc
#define _tlgAnnotes_tlgPackedData(ctype, pValue, cbValue, dataDescType, hasType)
#define _tlgAnnotes_tlgPackedField(ctype, pValue, cbValue, inType, outType, hasOut, name, desc, tags, hasTags) L"|" _tlg_LSTRINGIZE(name) L"=" desc
#define _tlgAnnotes_tlgPackedMeta(inType, outType, hasOut, name, desc, tags, hasTags) L"|" _tlg_LSTRINGIZE(name) L"=" desc
#define _TlgAnnotes_TlgPD(name, desc)
#define _TlgAnnotes_TlgPF(name, desc) L"|" L ## # name L"=" desc
#define _TlgAnnotes_TlgPM(name, desc) L"|" L ## # name L"=" desc
#define _tlgAnnotes_tlgPsz(ctype, pszValue, inType, outType, hasOut, name, desc, tags, hasTags) L"|" _tlg_LSTRINGIZE(name) L"=" desc
#define _TlgAnnotes_TlgS(name, desc) L"|" L ## # name L"=" desc
#define _tlgAnnotes_tlgScalarRef(ctype, value, inType, outType, hasOut, name, desc, tags, hasTags) L"|" _tlg_LSTRINGIZE(name) L"=" desc
#define _tlgAnnotes_tlgScalarVal(ctype, value, inType, outType, hasOut, name, desc, tags, hasTags) L"|" _tlg_LSTRINGIZE(name) L"=" desc
#define _tlgAnnotes_tlgSid(ctype, pValue, name, desc, tags, hasTags) L"|" _tlg_LSTRINGIZE(name) L"=" desc
#define _TlgAnnotes_TlgSid(name, desc) L"|" L ## # name L"=" desc
#define _TlgAnnotes_TlgString(name, desc) L"|" L ## # name L"=" desc
#define _tlgAnnotes_tlgStruct(fieldCount, inType, name, desc, tags, hasTags) L"|" _tlg_LSTRINGIZE(name) L"=" desc
#define _TlgAnnotes_TlgStruct(name, desc) L"|" L ## # name L"=" desc
#define _TlgAnnotes_TlgSz(name, desc) L"|" L ## # name L"=" desc
#define _tlgAnnotes_tlgTag(eventTag)
#define _TlgAnnotes_TlgWsz(name, desc) L"|" L ## # name L"=" desc
#define _tlgApplyArgs(macro, args) _tlgApplyArgs_impA((macro, _tlgApplyArgs_UNWRAP args))
#define _tlgApplyArgs_CALL(macro, handler, args) macro ## handler args
#define _tlgApplyArgs_impA(args) _tlgApplyArgs_impB args
#define _tlgApplyArgs_impB(macro, handler, ...) _tlgApplyArgs_CALL(macro, handler, (__VA_ARGS__))
#define _tlgApplyArgsN(macro, n, args) _tlgApplyArgsN_impA((macro, n, _tlgApplyArgs_UNWRAP args))
#define _tlgApplyArgsN_impA(args) _tlgApplyArgsN_impB args
#define _tlgApplyArgsN_impB(macro, n, handler, ...) _tlgApplyArgs_CALL(macro, handler, (n, __VA_ARGS__))
#define _TlgArg_imp(n, data_type, in_type, out_type, has_out, dim, value, cVals, ...) _TLG_PASTE(_TlgArg_imp, n) _TLG_FLATTEN((data_type, in_type, out_type, has_out, dim, value, cVals, __VA_ARGS__))
#define _TlgArg_imp0(data_type, in_type, out_type, has_out, dim, value, cVals, x) (data_type, in_type, out_type, has_out, dim, value, cVals, # value,,, 0)
#define _TlgArg_imp1(data_type, in_type, out_type, has_out, dim, value, cVals, name) (data_type, in_type, out_type, has_out, dim, value, cVals, name,,, 0)
#define _TlgArg_imp2(data_type, in_type, out_type, has_out, dim, value, cVals, name, desc) (data_type, in_type, out_type, has_out, dim, value, cVals, name, L ## desc,, 0)
#define _TlgArg_imp3(data_type, in_type, out_type, has_out, dim, value, cVals, name, desc, tags) (data_type, in_type, out_type, has_out, dim, value, cVals, name, L ## desc, tags, 1)
#define _tlgChannelVal(n, args) _tlgApplyArgs(_tlgChannelVal, args)
#define _TlgChannelVal_TlgA(value)
#define _tlgChannelVal_tlgArray(ctype, pValues, cValues, inType, outType, hasOut, name, desc, tags, hasTags)
#define _tlgChannelVal_tlgAuto(value, name, desc, tags, hasTags)
#define _TlgChannelVal_TlgAuto(value)
#define _TlgChannelVal_TlgB(value)
#define _tlgChannelVal_tlgBinary(ctype, pValue, cbValue, inType, outType, hasOut, name, desc, tags, hasTags)
#define _tlgChannelVal_tlgBuffer(ctype, pValue, inType, outType, hasOut, name, desc, tags, hasTags)
#define _tlgChannelVal_tlgChannel(eventChannel) & 0 | (eventChannel)
#define _TlgChannelVal_TlgChannel(value) & 0 | _TlgConst(Channel, value)
#define _tlgChannelVal_tlgCounted(ctype, pchValue, cchValue, inType, outType, hasOut, name, desc, tags, hasTags)
#define _TlgChannelVal_TlgCS(value)
#define _TlgChannelVal_TlgCustAnn(value)
#define _tlgChannelVal_tlgCustom(ctype, pValue, cbValue, protocol, bSchema, cbSchema, name, desc, tags, hasTags)
#define _TlgChannelVal_TlgCustom(value)
#define _tlgChannelVal_tlgCustomAnnot(key, value)
#define _tlgChannelVal_tlgDesc(eventDescription)
#define _TlgChannelVal_TlgDesc(value)
#define _TlgChannelVal_TlgEvtTag(value)
#define _TlgChannelVal_TlgFA(value)
#define _tlgChannelVal_tlgFixedArray(ctype, pValues, cValues, inType, outType, hasOut, name, desc, tags, hasTags)
#define _tlgChannelVal_tlgKeyword(eventKeyword)
#define _TlgChannelVal_TlgKeyword(value)
#define _tlgChannelVal_tlgLevel(eventLevel)
#define _TlgChannelVal_TlgLevel(value)
#define _tlgChannelVal_tlgOpcode(eventOpcode)
#define _TlgChannelVal_TlgOpcode(value)
#define _TlgChannelVal_TlgP(value)
#define _tlgChannelVal_tlgPackedData(ctype, pValue, cbValue, dataDescType, hasType)
#define _tlgChannelVal_tlgPackedField(ctype, pValue, cbValue, inType, outType, hasOut, name, desc, tags, hasTags)
#define _tlgChannelVal_tlgPackedMeta(inType, outType, hasOut, name, desc, tags, hasTags)
#define _TlgChannelVal_TlgPD(value)
#define _TlgChannelVal_TlgPF(value)
#define _TlgChannelVal_TlgPM(value)
#define _tlgChannelVal_tlgPsz(ctype, pszValue, inType, outType, hasOut, name, desc, tags, hasTags)
#define _TlgChannelVal_TlgS(value)
#define _tlgChannelVal_tlgScalarRef(ctype, value, inType, outType, hasOut, name, desc, tags, hasTags)
#define _tlgChannelVal_tlgScalarVal(ctype, value, inType, outType, hasOut, name, desc, tags, hasTags)
#define _tlgChannelVal_tlgSid(ctype, pValue, name, desc, tags, hasTags)
#define _TlgChannelVal_TlgSid(value)
#define _TlgChannelVal_TlgString(value)
#define _tlgChannelVal_tlgStruct(fieldCount, inType, name, desc, tags, hasTags)
#define _TlgChannelVal_TlgSz(value)
#define _tlgChannelVal_tlgTag(eventTag)
#define _TlgChannelVal_TlgWsz(value)
#define _tlgCheckStructFieldCountCppOnly(fieldCount) 0
#define _TlgConst(name, n) (n)
#define _tlgCreate1Sz_char16_t(pDesc, psz) _tlgCreate1Sz_wchar_t(pDesc, psz)
#define _tlgDataDescCount(n, args) _tlgApplyArgs(_tlgDataDescCount, args)
#define _TlgDataDescCount_TlgA() + 2
#define _tlgDataDescCount_tlgArray(ctype, pValues, cValues, inType, outType, hasOut, name, desc, tags, hasTags) + 2
#define _tlgDataDescCount_tlgAuto(value, name, desc, tags, hasTags) + 1
#define _TlgDataDescCount_TlgAuto() + 1
#define _TlgDataDescCount_TlgB() + 2
#define _tlgDataDescCount_tlgBinary(ctype, pValue, cbValue, inType, outType, hasOut, name, desc, tags, hasTags) + 2
#define _tlgDataDescCount_tlgBuffer(ctype, pValue, inType, outType, hasOut, name, desc, tags, hasTags) + 2
#define _tlgDataDescCount_tlgChannel(eventChannel)
#define _TlgDataDescCount_TlgChannel()
#define _tlgDataDescCount_tlgCounted(ctype, pchValue, cchValue, inType, outType, hasOut, name, desc, tags, hasTags) + 2
#define _TlgDataDescCount_TlgCS() + 2
#define _TlgDataDescCount_TlgCustAnn()
#define _tlgDataDescCount_tlgCustom(ctype, pValue, cbValue, protocol, bSchema, cbSchema, name, desc, tags, hasTags) + 2
#define _TlgDataDescCount_TlgCustom() + 2
#define _tlgDataDescCount_tlgCustomAnnot(key, value)
#define _tlgDataDescCount_tlgDesc(eventDescription)
#define _TlgDataDescCount_TlgDesc()
#define _TlgDataDescCount_TlgEvtTag()
#define _TlgDataDescCount_TlgFA() + 1
#define _tlgDataDescCount_tlgFixedArray(ctype, pValues, cValues, inType, outType, hasOut, name, desc, tags, hasTags) + 1
#define _tlgDataDescCount_tlgKeyword(eventKeyword)
#define _TlgDataDescCount_TlgKeyword()
#define _tlgDataDescCount_tlgLevel(eventLevel)
#define _TlgDataDescCount_TlgLevel()
#define _tlgDataDescCount_tlgOpcode(eventOpcode)
#define _TlgDataDescCount_TlgOpcode()
#define _TlgDataDescCount_TlgP() + 1
#define _tlgDataDescCount_tlgPackedData(ctype, pValue, cbValue, dataDescType, hasType) + 1
#define _tlgDataDescCount_tlgPackedField(ctype, pValue, cbValue, inType, outType, hasOut, name, desc, tags, hasTags) + 1
#define _tlgDataDescCount_tlgPackedMeta(inType, outType, hasOut, name, desc, tags, hasTags)
#define _TlgDataDescCount_TlgPD() + 1
#define _TlgDataDescCount_TlgPF() + 1
#define _TlgDataDescCount_TlgPM()
#define _tlgDataDescCount_tlgPsz(ctype, pszValue, inType, outType, hasOut, name, desc, tags, hasTags) + 1
#define _TlgDataDescCount_TlgS() + 1
#define _tlgDataDescCount_tlgScalarRef(ctype, value, inType, outType, hasOut, name, desc, tags, hasTags) + 1
#define _tlgDataDescCount_tlgScalarVal(ctype, value, inType, outType, hasOut, name, desc, tags, hasTags) + 1
#define _tlgDataDescCount_tlgSid(ctype, pValue, name, desc, tags, hasTags) + 1
#define _TlgDataDescCount_TlgSid() + 1
#define _TlgDataDescCount_TlgString() + 2
#define _tlgDataDescCount_tlgStruct(fieldCount, inType, name, desc, tags, hasTags) + _tlgCheckStructFieldCountCppOnly(fieldCount)
#define _TlgDataDescCount_TlgStruct()
#define _TlgDataDescCount_TlgSz() + 1
#define _tlgDataDescCount_tlgTag(eventTag)
#define _TlgDataDescCount_TlgWsz() + 1
#define _tlgDataDescCreate(n, args) _tlgApplyArgsN(_tlgDataDescCreate, n, args)
#define _tlgDataDescCreate_tlgChannel(n, eventChannel)
#define _TlgDataDescCreate_TlgChannel(n, data_type, cVals)
#define _TlgDataDescCreate_TlgCustAnn(n, data_type, cVals)
#define _tlgDataDescCreate_tlgCustomAnnot(n, key, value)
#define _tlgDataDescCreate_tlgDesc(n, eventDescription)
#define _TlgDataDescCreate_TlgDesc(n, data_type, cVals)
#define _TlgDataDescCreate_TlgEvtTag(n, data_type, cVals)
#define _tlgDataDescCreate_tlgKeyword(n, eventKeyword)
#define _TlgDataDescCreate_TlgKeyword(n, data_type, cVals)
#define _tlgDataDescCreate_tlgLevel(n, eventLevel)
#define _TlgDataDescCreate_TlgLevel(n, data_type, cVals)
#define _tlgDataDescCreate_tlgOpcode(n, eventOpcode)
#define _TlgDataDescCreate_TlgOpcode(n, data_type, cVals)
#define _tlgDataDescCreate_tlgPackedMeta(n, inType, outType, hasOut, name, desc, tags, hasTags) _tlg_AssertValidPackedMetadataInType(inType);
#define _TlgDataDescCreate_TlgPM(n, data_type, cVals)
#define _tlgDataDescCreate_tlgStruct(n, fieldCount, inType, name, desc, tags, hasTags) _tlg_CASSERT((fieldCount) < 128, "TraceLoggingStruct fieldCount must be less than 128.");
#define _TlgDataDescCreate_TlgStruct(n, data_type, cVals)
#define _tlgDataDescCreate_tlgTag(n, eventTag)
#define _tlgDefineProvider_functionWrapperBegin0(functionPostfix)
#define _TlgDefineProvider_functionWrapperBegin0(functionPostfix)
#define _tlgDescVal(n, args) _tlgApplyArgs(_tlgDescVal, args)
#define _TlgDescVal_TlgA(name, desc)
#define _tlgDescVal_tlgArray(ctype, pValues, cValues, inType, outType, hasOut, name, desc, tags, hasTags)
#define _tlgDescVal_tlgAuto(value, name, desc, tags, hasTags)
#define _TlgDescVal_TlgAuto(name, desc)
#define _TlgDescVal_TlgB(name, desc)
#define _tlgDescVal_tlgBinary(ctype, pValue, cbValue, inType, outType, hasOut, name, desc, tags, hasTags)
#define _tlgDescVal_tlgBuffer(ctype, pValue, inType, outType, hasOut, name, desc, tags, hasTags)
#define _tlgDescVal_tlgChannel(eventChannel)
#define _TlgDescVal_TlgChannel(name, desc)
#define _tlgDescVal_tlgCounted(ctype, pchValue, cchValue, inType, outType, hasOut, name, desc, tags, hasTags)
#define _TlgDescVal_TlgCS(name, desc)
#define _TlgDescVal_TlgCustAnn(name, desc)
#define _tlgDescVal_tlgCustom(ctype, pValue, cbValue, protocol, bSchema, cbSchema, name, desc, tags, hasTags)
#define _TlgDescVal_TlgCustom(name, desc)
#define _tlgDescVal_tlgCustomAnnot(key, value)
#define _tlgDescVal_tlgDesc(eventDescription) eventDescription
#define _TlgDescVal_TlgDesc(name, desc) desc
#define _TlgDescVal_TlgEvtTag(name, desc)
#define _TlgDescVal_TlgFA(name, desc)
#define _tlgDescVal_tlgFixedArray(ctype, pValues, cValues, inType, outType, hasOut, name, desc, tags, hasTags)
#define _tlgDescVal_tlgKeyword(eventKeyword)
#define _TlgDescVal_TlgKeyword(name, desc)
#define _tlgDescVal_tlgLevel(eventLevel)
#define _TlgDescVal_TlgLevel(name, desc)
#define _tlgDescVal_tlgOpcode(eventOpcode)
#define _TlgDescVal_TlgOpcode(name, desc)
#define _TlgDescVal_TlgP(name, desc)
#define _tlgDescVal_tlgPackedData(ctype, pValue, cbValue, dataDescType, hasType)
#define _tlgDescVal_tlgPackedField(ctype, pValue, cbValue, inType, outType, hasOut, name, desc, tags, hasTags)
#define _tlgDescVal_tlgPackedMeta(inType, outType, hasOut, name, desc, tags, hasTags)
#define _TlgDescVal_TlgPD(name, desc)
#define _TlgDescVal_TlgPF(name, desc)
#define _TlgDescVal_TlgPM(name, desc)
#define _tlgDescVal_tlgPsz(ctype, pszValue, inType, outType, hasOut, name, desc, tags, hasTags)
#define _TlgDescVal_TlgS(name, desc)
#define _tlgDescVal_tlgScalarRef(ctype, value, inType, outType, hasOut, name, desc, tags, hasTags)
#define _tlgDescVal_tlgScalarVal(ctype, value, inType, outType, hasOut, name, desc, tags, hasTags)
#define _tlgDescVal_tlgSid(ctype, pValue, name, desc, tags, hasTags)
#define _TlgDescVal_TlgSid(name, desc)
#define _TlgDescVal_TlgString(name, desc)
#define _tlgDescVal_tlgStruct(fieldCount, inType, name, desc, tags, hasTags)
#define _TlgDescVal_TlgStruct(name, desc)
#define _TlgDescVal_TlgSz(name, desc)
#define _tlgDescVal_tlgTag(eventTag)
#define _TlgDescVal_TlgWsz(name, desc)
#define _tlgDT_imp0(name, ...) name,,, 0
#define _tlgDT_imp1(name, desc) name, L"" desc,, 0
#define _tlgDT_imp2(name, desc, tags) name, L"" desc, tags, 1
#define _tlgDT_impB(macro, args) macro args
#define _TlgEvtTagEncode(eventTag) (((eventTag) & 0xfe00000) >> 21) /* Encode 0x0fe00000 bits into UINT16  */ | (((eventTag) & 0x1fc000) >> 6) /* Encode 0x001fc000 bits into UINT16  */ | (((eventTag) & 0x3fff) << 16) /* Trigger warning for 0x00003fff bits */ | (~(~(eventTag) | 0x0FFFFFFF))
#define _tlgEvtTagInit 0
#define _tlgEvtTagVal(n, args) _tlgApplyArgs(_tlgEvtTagVal, args)
#define _TlgEvtTagVal_TlgA(value)
#define _tlgEvtTagVal_tlgArray(ctype, pValues, cValues, inType, outType, hasOut, name, desc, tags, hasTags)
#define _tlgEvtTagVal_tlgAuto(value, name, desc, tags, hasTags)
#define _TlgEvtTagVal_TlgAuto(value)
#define _TlgEvtTagVal_TlgB(value)
#define _tlgEvtTagVal_tlgBinary(ctype, pValue, cbValue, inType, outType, hasOut, name, desc, tags, hasTags)
#define _tlgEvtTagVal_tlgBuffer(ctype, pValue, inType, outType, hasOut, name, desc, tags, hasTags)
#define _tlgEvtTagVal_tlgChannel(eventChannel)
#define _TlgEvtTagVal_TlgChannel(value)
#define _tlgEvtTagVal_tlgCounted(ctype, pchValue, cchValue, inType, outType, hasOut, name, desc, tags, hasTags)
#define _TlgEvtTagVal_TlgCS(value)
#define _TlgEvtTagVal_TlgCustAnn(value)
#define _tlgEvtTagVal_tlgCustom(ctype, pValue, cbValue, protocol, bSchema, cbSchema, name, desc, tags, hasTags)
#define _TlgEvtTagVal_TlgCustom(value)
#define _tlgEvtTagVal_tlgCustomAnnot(key, value)
#define _tlgEvtTagVal_tlgDesc(eventDescription)
#define _TlgEvtTagVal_TlgDesc(value)
#define _TlgEvtTagVal_TlgEvtTag(value) | value
#define _TlgEvtTagVal_TlgFA(value)
#define _tlgEvtTagVal_tlgFixedArray(ctype, pValues, cValues, inType, outType, hasOut, name, desc, tags, hasTags)
#define _tlgEvtTagVal_tlgKeyword(eventKeyword)
#define _TlgEvtTagVal_TlgKeyword(value)
#define _tlgEvtTagVal_tlgLevel(eventLevel)
#define _TlgEvtTagVal_TlgLevel(value)
#define _tlgEvtTagVal_tlgOpcode(eventOpcode)
#define _TlgEvtTagVal_TlgOpcode(value)
#define _TlgEvtTagVal_TlgP(value)
#define _tlgEvtTagVal_tlgPackedData(ctype, pValue, cbValue, dataDescType, hasType)
#define _tlgEvtTagVal_tlgPackedField(ctype, pValue, cbValue, inType, outType, hasOut, name, desc, tags, hasTags)
#define _tlgEvtTagVal_tlgPackedMeta(inType, outType, hasOut, name, desc, tags, hasTags)
#define _TlgEvtTagVal_TlgPD(value)
#define _TlgEvtTagVal_TlgPF(value)
#define _TlgEvtTagVal_TlgPM(value)
#define _tlgEvtTagVal_tlgPsz(ctype, pszValue, inType, outType, hasOut, name, desc, tags, hasTags)
#define _TlgEvtTagVal_TlgS(value)
#define _tlgEvtTagVal_tlgScalarRef(ctype, value, inType, outType, hasOut, name, desc, tags, hasTags)
#define _tlgEvtTagVal_tlgScalarVal(ctype, value, inType, outType, hasOut, name, desc, tags, hasTags)
#define _tlgEvtTagVal_tlgSid(ctype, pValue, name, desc, tags, hasTags)
#define _TlgEvtTagVal_TlgSid(value)
#define _TlgEvtTagVal_TlgString(value)
#define _tlgEvtTagVal_tlgStruct(fieldCount, inType, name, desc, tags, hasTags)
#define _TlgEvtTagVal_TlgStruct()
#define _TlgEvtTagVal_TlgSz(value)
#define _tlgEvtTagVal_tlgTag(eventTag) | (eventTag)
#define _TlgEvtTagVal_TlgWsz(value)
#define _tlgExpandProviderId(a, b, c, d, e, f, g, h, i, j, k) { a, b, c, { d, e, f, g, h, i, j, k } }
#define _tlgExpandType_imp0() (0), 0
#define _tlgExpandType_imp1(typeVal) (typeVal), 1
#define _tlgExpandType_impB(macro, args) macro args
#define _tlgGetLengthSid(pSid) (8u + (pSid) -> SubAuthorityCount * 4u)
#define _tlgInfoVals(n, args) _tlgApplyArgsN(_tlgInfoVals, n, args)
#define _tlgInfoVals_00(outType, tags)
#define _TlgInfoVals_00(out_type, tags)
#define _tlgInfoVals_01(outType, tags) | 128, 128, _tlgInfoVals_Tags(tags)
#define _TlgInfoVals_01(out_type, tags) | 128, 128, _TlgInfoVals_Tags(tags)
#define _tlgInfoVals_1(tags) , _tlgInfoVals_Tags(tags)
#define _TlgInfoVals_1(tags) , _TlgInfoVals_Tags(tags)
#define _tlgInfoVals_10(outType, tags) | 128, outType
#define _TlgInfoVals_10(out_type, tags) | 128, out_type
#define _tlgInfoVals_11(outType, tags) | 128, outType | 128, _tlgInfoVals_Tags(tags)
#define _TlgInfoVals_11(out_type, tags) | 128, out_type | 128, _TlgInfoVals_Tags(tags)
#define _tlgInfoVals_Tags(tags) 0x808080 | (((tags) & 0x7f) << 24) | (((tags) & 0x3f80) << 9) | (((tags) & 0x1fc000) >> 6) | (((tags) & 0xfe00000) >> 21)
#define _TlgInfoVals_Tags(tags) 0x808080 | (((tags) & 0x7f) << 24) | (((tags) & 0x3f80) << 9) | (((tags) & 0x1fc000) >> 6) | (((tags) & 0xfe00000) >> 21)
#define _tlgInfoVals_tlgChannel(n, eventChannel)
#define _TlgInfoVals_TlgChannel(in_type, out_type, has_out, cVals, name, has_tags, tags)
#define _TlgInfoVals_TlgCustAnn(in_type, out_type, has_out, cVals, name, has_tags, tags)
#define _tlgInfoVals_tlgCustom(n, ctype, pValue, cbValue, protocol, bSchema, cbSchema, name, desc, tags, hasTags) , (name), ((protocol) & 31) | 96 _tlgInfoVals_0 ## hasTags(0, tags), cbSchema, { _tlg_FLATTEN bSchema }
#define _TlgInfoVals_TlgCustom(in_type, out_type, has_out, cVals, name, has_tags, tags) , (name), ((in_type) & 31) | 96 _TlgInfoVals_0 ## has_tags(0, tags), has_out, { _TLG_FLATTEN out_type }
#define _tlgInfoVals_tlgCustomAnnot(n, key, value)
#define _tlgInfoVals_tlgDesc(n, eventDescription)
#define _TlgInfoVals_TlgDesc(in_type, out_type, has_out, cVals, name, has_tags, tags)
#define _TlgInfoVals_TlgEvtTag(in_type, out_type, has_out, cVals, name, has_tags, tags)
#define _tlgInfoVals_tlgKeyword(n, eventKeyword)
#define _TlgInfoVals_TlgKeyword(in_type, out_type, has_out, cVals, name, has_tags, tags)
#define _tlgInfoVals_tlgLevel(n, eventLevel)
#define _TlgInfoVals_TlgLevel(in_type, out_type, has_out, cVals, name, has_tags, tags)
#define _tlgInfoVals_tlgOpcode(n, eventOpcode)
#define _TlgInfoVals_TlgOpcode(in_type, out_type, has_out, cVals, name, has_tags, tags)
#define _tlgInfoVals_tlgPackedData(n, ctype, pValue, cbValue, dataDescType, hasType)
#define _TlgInfoVals_TlgPD(in_type, out_type, has_out, cVals, name, has_tags, tags)
#define _tlgInfoVals_tlgStruct(n, fieldCount, inType, name, desc, tags, hasTags) , (name), inType _tlgInfoVals_1 ## hasTags(fieldCount, tags)
#define _TlgInfoVals_TlgStruct(in_type, out_type, has_out, cVals, name, has_tags, tags) , (name), in_type _TlgInfoVals_1 ## has_tags(cVals, tags)
#define _tlgInfoVals_tlgTag(n, eventTag)
#define _tlgInfoVars(n, args) _tlgApplyArgsN(_tlgInfoVars, n, args)
#define _tlgInfoVars_0(n)
#define _TlgInfoVars_0(n)
#define _tlgInfoVars_00(n)
#define _TlgInfoVars_00(n)
#define _tlgInfoVars_tlgChannel(n, eventChannel)
#define _tlgInfoVars_tlgCustomAnnot(n, key, value)
#define _tlgInfoVars_tlgDesc(n, eventDescription)
#define _tlgInfoVars_tlgKeyword(n, eventKeyword)
#define _tlgInfoVars_tlgLevel(n, eventLevel)
#define _tlgInfoVars_tlgOpcode(n, eventOpcode)
#define _tlgInfoVars_tlgPackedData(n, ctype, pValue, cbValue, dataDescType, hasType)
#define _TlgInfoVars_TlgPD(n, name, in_type, has_out, has_tags)
#define _tlgInfoVars_tlgTag(n, eventTag)
#define _tlgKeywordConst(n) n
#define _TlgKeywordVal_TlgA(value)
#define _tlgKeywordVal_tlgArray(ctype, pValues, cValues, inType, outType, hasOut, name, desc, tags, hasTags)
#define _tlgKeywordVal_tlgAuto(value, name, desc, tags, hasTags)
#define _TlgKeywordVal_TlgAuto(value)
#define _TlgKeywordVal_TlgB(value)
#define _tlgKeywordVal_tlgBinary(ctype, pValue, cbValue, inType, outType, hasOut, name, desc, tags, hasTags)
#define _tlgKeywordVal_tlgBuffer(ctype, pValue, inType, outType, hasOut, name, desc, tags, hasTags)
#define _tlgKeywordVal_tlgChannel(eventChannel)
#define _TlgKeywordVal_TlgChannel(value)
#define _tlgKeywordVal_tlgCounted(ctype, pchValue, cchValue, inType, outType, hasOut, name, desc, tags, hasTags)
#define _TlgKeywordVal_TlgCS(value)
#define _TlgKeywordVal_TlgCustAnn(value)
#define _tlgKeywordVal_tlgCustom(ctype, pValue, cbValue, protocol, bSchema, cbSchema, name, desc, tags, hasTags)
#define _TlgKeywordVal_TlgCustom(value)
#define _tlgKeywordVal_tlgCustomAnnot(key, value)
#define _tlgKeywordVal_tlgDesc(eventDescription)
#define _TlgKeywordVal_TlgDesc(value)
#define _TlgKeywordVal_TlgEvtTag(value)
#define _TlgKeywordVal_TlgFA(value)
#define _tlgKeywordVal_tlgFixedArray(ctype, pValues, cValues, inType, outType, hasOut, name, desc, tags, hasTags)
#define _tlgKeywordVal_tlgKeyword(eventKeyword) | (eventKeyword)
#define _TlgKeywordVal_TlgKeyword(value) | _TlgConst(Keyword, value)
#define _tlgKeywordVal_tlgLevel(eventLevel)
#define _TlgKeywordVal_TlgLevel(value)
#define _tlgKeywordVal_tlgOpcode(eventOpcode)
#define _TlgKeywordVal_TlgOpcode(value)
#define _TlgKeywordVal_TlgP(value)
#define _tlgKeywordVal_tlgPackedData(ctype, pValue, cbValue, dataDescType, hasType)
#define _tlgKeywordVal_tlgPackedField(ctype, pValue, cbValue, inType, outType, hasOut, name, desc, tags, hasTags)
#define _tlgKeywordVal_tlgPackedMeta(inType, outType, hasOut, name, desc, tags, hasTags)
#define _TlgKeywordVal_TlgPD(value)
#define _TlgKeywordVal_TlgPF(value)
#define _TlgKeywordVal_TlgPM(value)
#define _tlgKeywordVal_tlgPsz(ctype, pszValue, inType, outType, hasOut, name, desc, tags, hasTags)
#define _TlgKeywordVal_TlgS(value)
#define _tlgKeywordVal_tlgScalarRef(ctype, value, inType, outType, hasOut, name, desc, tags, hasTags)
#define _tlgKeywordVal_tlgScalarVal(ctype, value, inType, outType, hasOut, name, desc, tags, hasTags)
#define _tlgKeywordVal_tlgSid(ctype, pValue, name, desc, tags, hasTags)
#define _TlgKeywordVal_TlgSid(value)
#define _TlgKeywordVal_TlgString(value)
#define _tlgKeywordVal_tlgStruct(fieldCount, inType, name, desc, tags, hasTags)
#define _TlgKeywordVal_TlgSz(value)
#define _tlgKeywordVal_tlgTag(eventTag)
#define _TlgKeywordVal_TlgWsz(value)
#define _TlgLevelVal_TlgA(value)
#define _tlgLevelVal_tlgArray(ctype, pValues, cValues, inType, outType, hasOut, name, desc, tags, hasTags)
#define _tlgLevelVal_tlgAuto(value, name, desc, tags, hasTags)
#define _TlgLevelVal_TlgAuto(value)
#define _TlgLevelVal_TlgB(value)
#define _tlgLevelVal_tlgBinary(ctype, pValue, cbValue, inType, outType, hasOut, name, desc, tags, hasTags)
#define _tlgLevelVal_tlgBuffer(ctype, pValue, inType, outType, hasOut, name, desc, tags, hasTags)
#define _tlgLevelVal_tlgChannel(eventChannel)
#define _TlgLevelVal_TlgChannel(value)
#define _tlgLevelVal_tlgCounted(ctype, pchValue, cchValue, inType, outType, hasOut, name, desc, tags, hasTags)
#define _TlgLevelVal_TlgCS(value)
#define _TlgLevelVal_TlgCustAnn(value)
#define _tlgLevelVal_tlgCustom(ctype, pValue, cbValue, protocol, bSchema, cbSchema, name, desc, tags, hasTags)
#define _TlgLevelVal_TlgCustom(value)
#define _tlgLevelVal_tlgCustomAnnot(key, value)
#define _tlgLevelVal_tlgDesc(eventDescription)
#define _TlgLevelVal_TlgDesc(value)
#define _TlgLevelVal_TlgEvtTag(value)
#define _TlgLevelVal_TlgFA(value)
#define _tlgLevelVal_tlgFixedArray(ctype, pValues, cValues, inType, outType, hasOut, name, desc, tags, hasTags)
#define _tlgLevelVal_tlgKeyword(eventKeyword)
#define _TlgLevelVal_TlgKeyword(value)
#define _tlgLevelVal_tlgLevel(eventLevel) & 0 | (eventLevel)
#define _TlgLevelVal_TlgLevel(value) & 0 | _TlgConst(Level, value)
#define _tlgLevelVal_tlgOpcode(eventOpcode)
#define _TlgLevelVal_TlgOpcode(value)
#define _TlgLevelVal_TlgP(value)
#define _tlgLevelVal_tlgPackedData(ctype, pValue, cbValue, dataDescType, hasType)
#define _tlgLevelVal_tlgPackedField(ctype, pValue, cbValue, inType, outType, hasOut, name, desc, tags, hasTags)
#define _tlgLevelVal_tlgPackedMeta(inType, outType, hasOut, name, desc, tags, hasTags)
#define _TlgLevelVal_TlgPD(value)
#define _TlgLevelVal_TlgPF(value)
#define _TlgLevelVal_TlgPM(value)
#define _tlgLevelVal_tlgPsz(ctype, pszValue, inType, outType, hasOut, name, desc, tags, hasTags)
#define _TlgLevelVal_TlgS(value)
#define _tlgLevelVal_tlgScalarRef(ctype, value, inType, outType, hasOut, name, desc, tags, hasTags)
#define _tlgLevelVal_tlgScalarVal(ctype, value, inType, outType, hasOut, name, desc, tags, hasTags)
#define _tlgLevelVal_tlgSid(ctype, pValue, name, desc, tags, hasTags)
#define _TlgLevelVal_TlgSid(value)
#define _TlgLevelVal_TlgString(value)
#define _tlgLevelVal_tlgStruct(fieldCount, inType, name, desc, tags, hasTags)
#define _TlgLevelVal_TlgSz(value)
#define _tlgLevelVal_tlgTag(eventTag)
#define _TlgLevelVal_TlgWsz(value)
#define _tlgNDT_imp0(value, ...) # value,,, 0
#define _tlgNDT_imp1(value, name) name,,, 0
#define _tlgNDT_imp2(value, name, desc) name, L"" desc,, 0
#define _tlgNDT_imp3(value, name, desc, tags) name, L"" desc, tags, 1
#define _tlgNDT_impB(macro, args) macro args
#define _tlgOpcodeVal(n, args) _tlgApplyArgs(_tlgOpcodeVal, args)
#define _TlgOpcodeVal_TlgA(value)
#define _tlgOpcodeVal_tlgArray(ctype, pValues, cValues, inType, outType, hasOut, name, desc, tags, hasTags)
#define _tlgOpcodeVal_tlgAuto(value, name, desc, tags, hasTags)
#define _TlgOpcodeVal_TlgAuto(value)
#define _TlgOpcodeVal_TlgB(value)
#define _tlgOpcodeVal_tlgBinary(ctype, pValue, cbValue, inType, outType, hasOut, name, desc, tags, hasTags)
#define _tlgOpcodeVal_tlgBuffer(ctype, pValue, inType, outType, hasOut, name, desc, tags, hasTags)
#define _tlgOpcodeVal_tlgChannel(eventChannel)
#define _TlgOpcodeVal_TlgChannel(value)
#define _tlgOpcodeVal_tlgCounted(ctype, pchValue, cchValue, inType, outType, hasOut, name, desc, tags, hasTags)
#define _TlgOpcodeVal_TlgCS(value)
#define _TlgOpcodeVal_TlgCustAnn(value)
#define _tlgOpcodeVal_tlgCustom(ctype, pValue, cbValue, protocol, bSchema, cbSchema, name, desc, tags, hasTags)
#define _TlgOpcodeVal_TlgCustom(value)
#define _tlgOpcodeVal_tlgCustomAnnot(key, value)
#define _tlgOpcodeVal_tlgDesc(eventDescription)
#define _TlgOpcodeVal_TlgDesc(value)
#define _TlgOpcodeVal_TlgEvtTag(value)
#define _TlgOpcodeVal_TlgFA(value)
#define _tlgOpcodeVal_tlgFixedArray(ctype, pValues, cValues, inType, outType, hasOut, name, desc, tags, hasTags)
#define _tlgOpcodeVal_tlgKeyword(eventKeyword)
#define _TlgOpcodeVal_TlgKeyword(value)
#define _tlgOpcodeVal_tlgLevel(eventLevel)
#define _TlgOpcodeVal_TlgLevel(value)
#define _tlgOpcodeVal_tlgOpcode(eventOpcode) & 0 | (eventOpcode)
#define _TlgOpcodeVal_TlgOpcode(value) & 0 | _TlgConst(Opcode, value)
#define _TlgOpcodeVal_TlgP(value)
#define _tlgOpcodeVal_tlgPackedData(ctype, pValue, cbValue, dataDescType, hasType)
#define _tlgOpcodeVal_tlgPackedField(ctype, pValue, cbValue, inType, outType, hasOut, name, desc, tags, hasTags)
#define _tlgOpcodeVal_tlgPackedMeta(inType, outType, hasOut, name, desc, tags, hasTags)
#define _TlgOpcodeVal_TlgPD(value)
#define _TlgOpcodeVal_TlgPF(value)
#define _TlgOpcodeVal_TlgPM(value)
#define _tlgOpcodeVal_tlgPsz(ctype, pszValue, inType, outType, hasOut, name, desc, tags, hasTags)
#define _TlgOpcodeVal_TlgS(value)
#define _tlgOpcodeVal_tlgScalarRef(ctype, value, inType, outType, hasOut, name, desc, tags, hasTags)
#define _tlgOpcodeVal_tlgScalarVal(ctype, value, inType, outType, hasOut, name, desc, tags, hasTags)
#define _tlgOpcodeVal_tlgSid(ctype, pValue, name, desc, tags, hasTags)
#define _TlgOpcodeVal_TlgSid(value)
#define _TlgOpcodeVal_TlgString(value)
#define _tlgOpcodeVal_tlgStruct(fieldCount, inType, name, desc, tags, hasTags)
#define _TlgOpcodeVal_TlgSz(value)
#define _tlgOpcodeVal_tlgTag(eventTag)
#define _TlgOpcodeVal_TlgWsz(value)
#define _tlgProvVal(n, args) _tlgApplyArgs(_tlgProvVal, args)
#define _tlgProvVal_tlgOption(traitType, ctype, value) , 3 + sizeof (ctype), traitType, _tlg_FLATTEN value
#define _tlgProvVar(n, args) _tlgApplyArgsN(_tlgProvVar, n, args)
#define _TlgTempVars_TlgChannel(n, data_type, in_type, value, cVals)
#define _TlgTempVars_TlgCustAnn(n, data_type, in_type, value, cVals)
#define _TlgTempVars_TlgDesc(n, data_type, in_type, value, cVals)
#define _TlgTempVars_TlgEvtTag(n, data_type, in_type, value, cVals)
#define _TlgTempVars_TlgKeyword(n, data_type, in_type, value, cVals)
#define _TlgTempVars_TlgLevel(n, data_type, in_type, value, cVals)
#define _TlgTempVars_TlgOpcode(n, data_type, in_type, value, cVals)
#define _TlgTempVars_TlgPM(n, data_type, in_type, value, cVals) _TLG_AssertValidPackedMetadataInType(in_type);
#define _TlgTempVars_TlgStruct(n, data_type, in_type, value, cVals) _TLG_CASSERT(cVals < 128, "Struct size must be less than 128");
#define _tlgValidateProviderId_parenthesisCALL(macro, args) macro args
#define _tlgValidateProviderId_presentCALL(macro, args) macro args
#define TLG_PAGED_CODE() PAGED_CODE()
#define TLG_RAISEASSERTIONFAILURE() __debugbreak()

#define _tlgInfoVals_tlgSid(n, ctype, pValue, name, desc, tags, hasTags) , (name), TlgInSID _tlgInfoVals_0 ## hasTags(outType, tags)

