/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Precompiled header of the kernel
 * COPYRIGHT:   Copyright Alex Ionescu <alex@relsoft.net>
 *              Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#ifndef _NTOSKRNL_PCH
#define _NTOSKRNL_PCH

/* The amd64 WDK headers declare these as intrinsics the kernel exports itself */
#ifdef _M_AMD64
#define IoAllocateAdapterChannel _IoAllocateAdapterChannel
#define KeGetCurrentThread _KeGetCurrentThread
#define RtlFillMemoryUlong _RtlFillMemoryUlong
#endif

#undef __MSVCRT__
#include <psdk/ntverp.h>

#define _REALLY_GET_CALLERS_CALLER
#include <excpt.h>
#include <ntdef.h>
#include <ntifs.h>
#include <wdmguid.h>
#include <diskguid.h>
#include <arc/arc.h>
#include <mountmgr.h>

#ifndef _NTHALDLL_
#undef NTHALAPI
#define NTHALAPI __declspec(dllimport)
#endif
#include <ndk/asm.h>
#include <ndk/cctypes.h>
#include <ndk/cmfuncs.h>
#include <ndk/dbgkfuncs.h>
#include <ndk/exfuncs.h>
#include <ndk/halfuncs.h>
#include <ndk/inbvfuncs.h>
#include <ndk/iofuncs.h>
#include <ndk/kdfuncs.h>
#include <ndk/kefuncs.h>
#include <ndk/ldrfuncs.h>
#include <ndk/lpcfuncs.h>
#include <ndk/mmfuncs.h>
#include <ndk/muptypes.h>
#include <ndk/obfuncs.h>
#include <ndk/pofuncs.h>
#include <ndk/psfuncs.h>
#include <ndk/rtlfuncs.h>
#include <ndk/sefuncs.h>
#include <ndk/vftypes.h>

#undef TEXT
#define TEXT(s) L##s
#define _IN_KERNEL_
#include <regstr.h>
#include <ntstrsafe.h>
#include <ntpoapi.h>
#define ENABLE_INTSAFE_SIGNED_FUNCTIONS
#include <ntintsafe.h>
#undef ENABLE_INTSAFE_SIGNED_FUNCTIONS

#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>
#include <malloc.h>
#include <wchar.h>

#include <pseh/pseh2.h>
#include <arc/setupblk.h>

#define NOEXTAPI
#include <windbgkd.h>
#include <wdbgexts.h>
#include <kddll.h>

/* The built-in debugger exports the KD transport entry points under its own names */
#ifdef KDBG
#define KdD0Transition   KdbgD0Transition
#define KdD3Transition   KdbgD3Transition
#define KdSave           KdbgSave
#define KdRestore        KdbgRestore
#define KdSendPacket     KdbgSendPacket
#define KdReceivePacket  KdbgReceivePacket
#undef _KDDLL_
#include <kddll.h>
#endif

#include <umpnpmgr/sysguid.h>
#include <srmp.h>

#define ExRaiseStatus RtlRaiseStatus

#ifndef Add2Ptr
#define Add2Ptr(P, I) ((PVOID)((PUCHAR)(P) + (I)))
#endif
#ifndef PtrOffset
#define PtrOffset(B, O) ((ULONG)((ULONG_PTR)(O) - (ULONG_PTR)(B)))
#endif

#define MAX_WIN32_PATH 260
C_ASSERT(MAX_WIN32_PATH == MAX_PATH);
#undef MAX_PATH

#include "config.h"
#include "internal/ntoskrnl.h"
#include <reactos/probe.h>
#include "internal/probe.h"
#include "resource.h"
#include "internal/ps_i.h"

#endif
