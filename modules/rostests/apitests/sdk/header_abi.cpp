/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Verify public type and function aliases against the 26100 SDK
 * COPYRIGHT:   Copyright 2026 LiberNT contributors
 */

#include <TraceLoggingProvider.h>
#include <windows.h>
#include <setupapi.h>

template<class A, class B> struct SameType { enum { value = false }; };
template<class A> struct SameType<A, A> { enum { value = true }; };

static_assert(_tlg_MetadataMagic == 0xBB8A052B88040E86ULL && _tlg_MetadataMagic > 0, "metadata signature");
static_assert(_tlg_FLATTEN(3 + 4) == 7, "TraceLogging macro expansion");
static_assert(TlgInPOINTER == (sizeof(void *) == 8 ? TlgInHEXINT64 : TlgInHEXINT32), "pointer metadata");
static_assert(sizeof(POINTER_64_INT) == sizeof(void *), "pointer-sized integer");
static_assert(sizeof(PHANDLE64) == sizeof(void *), "native pointer to HANDLE64");
static_assert(sizeof(HANDLE64) == 8, "64-bit handle");
#if defined(_MSC_VER) || (defined(__clang__) && (defined(__i386__) || defined(__x86_64__) || defined(__aarch64__)))
static_assert(sizeof(void *POINTER_32) == 4, "32-bit pointer");
static_assert(sizeof(void *POINTER_64) == 8, "64-bit pointer");
#endif

static_assert(SameType<NPWNDCLASSA, WNDCLASSA *>::value, "ANSI window class");
static_assert(SameType<NPWNDCLASSW, WNDCLASSW *>::value, "Unicode window class");
static_assert(SameType<NPWNDCLASSEXA, WNDCLASSEXA *>::value, "extended ANSI window class");
static_assert(SameType<NPWNDCLASSEXW, WNDCLASSEXW *>::value, "extended Unicode window class");
static_assert(SameType<NPWNDCLASS, WNDCLASS *>::value, "generic window class");
static_assert(SameType<NPWNDCLASSEX, WNDCLASSEX *>::value, "generic extended window class");

#if (_SETUPAPI_VER >= _WIN32_WINNT_WINXP)
#if USE_SP_INF_SIGNER_INFO_V1 || (_SETUPAPI_VER < _WIN32_WINNT_LONGHORN)
static_assert(SameType<SP_INF_SIGNER_INFO, SP_INF_SIGNER_INFO_V1>::value, "V1 signer");
#else
static_assert(SameType<SP_INF_SIGNER_INFO, SP_INF_SIGNER_INFO_V2>::value, "V2 signer");
#endif
static_assert(SameType<PSP_INF_SIGNER_INFO, SP_INF_SIGNER_INFO *>::value, "signer pointer");

typedef BOOL (WINAPI *VerifyInf)(LPCTSTR, PSP_ALTPLATFORM_INFO, PSP_INF_SIGNER_INFO);
static_assert(SameType<decltype(&SetupVerifyInfFile), VerifyInf>::value, "signer API");
#elif defined(SetupVerifyInfFile)
#error SetupVerifyInfFile requires XP or later
#endif

#if USE_SP_ALTPLATFORM_INFO_V1 || (_SETUPAPI_VER < _WIN32_WINNT_WINXP)
static_assert(SameType<SP_ALTPLATFORM_INFO, SP_ALTPLATFORM_INFO_V1>::value, "V1 platform");
#elif USE_SP_ALTPLATFORM_INFO_V3 && (NTDDI_VERSION >= NTDDI_WIN10_RS1)
static_assert(SameType<SP_ALTPLATFORM_INFO, SP_ALTPLATFORM_INFO_V3>::value, "V3 platform");
#else
static_assert(SameType<SP_ALTPLATFORM_INFO, SP_ALTPLATFORM_INFO_V2>::value, "V2 platform");
#endif
static_assert(sizeof(SP_ALTPLATFORM_INFO_V3) == (sizeof(void *) == 4 ? 35 : 36), "platform packing");

#if (_WIN32_WINNT >= 0x0600)
typedef HANDLE (WINAPI *CreateMutexExtended)(LPSECURITY_ATTRIBUTES, LPCTSTR, DWORD, DWORD);
static_assert(SameType<decltype(&CreateMutexEx), CreateMutexExtended>::value, "mutex API");
#elif defined(CreateMutexEx)
#error CreateMutexEx requires Vista or later
#endif

#if defined(_ARM64_) || defined(_AMD64_) || defined(_X86_)
static_assert(sizeof(XSAVE_ARM64_SVE_HEADER) == 32, "SVE header");
static_assert(XSTATE_ARM64_SVE == 2 && XSTATE_MASK_ARM64_SVE == 4, "SVE feature");
#endif
#ifdef _ARM64_
static_assert(XSTATE_MASK_ALLOWED == 4 && XSTATE_MASK_LEGACY == 0, "ARM64 state masks");
static_assert(CONTEXT_XSTATE == 0x00400020 && CONTEXT_ALL == 0x0040001f, "ARM64 context flags");
#endif

static_assert(JobObjectGroupInformation == 11, "job processor groups");
static_assert(JobObjectNetworkAccountingInformation == 51, "job network accounting");
static_assert(MaxJobObjectInfoClass == 52, "job information class limit");
