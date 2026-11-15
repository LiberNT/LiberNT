#pragma once

#include <section_attribs.h>

#ifdef _NTOSKRNL_

#define KeGetCurrentThread  _KeGetCurrentThread
#define KeGetPreviousMode   _KeGetPreviousMode
#undef  PsGetCurrentProcess
#define PsGetCurrentProcess _PsGetCurrentProcess

#define RVA(m, b) ((PVOID)((ULONG_PTR)(b) + (ULONG_PTR)(m)))

#define InterlockedDecrement         _InterlockedDecrement
#define InterlockedDecrement16       _InterlockedDecrement16
#define InterlockedIncrement         _InterlockedIncrement
#define InterlockedIncrement16       _InterlockedIncrement16
#define InterlockedCompareExchange   _InterlockedCompareExchange
#define InterlockedCompareExchange16 _InterlockedCompareExchange16
#define InterlockedCompareExchange64 _InterlockedCompareExchange64
#define InterlockedExchange          _InterlockedExchange
#define InterlockedExchangeAdd       _InterlockedExchangeAdd
#define InterlockedOr                _InterlockedOr
#define InterlockedAnd               _InterlockedAnd

#define ExEnterCriticalRegionAndAcquireFastMutexUnsafe _ExEnterCriticalRegionAndAcquireFastMutexUnsafe
#define ExReleaseFastMutexUnsafeAndLeaveCriticalRegion _ExReleaseFastMutexUnsafeAndLeaveCriticalRegion
#define KeAcquireGuardedMutexUnsafe _KeAcquireGuardedMutexUnsafe
#define KeReleaseGuardedMutexUnsafe _KeReleaseGuardedMutexUnsafe

#define ForwardClusterOnly CacheManagerActive
#define AddressSpaceOwner  Spare



#endif

#include "tag.h"
#include "ke.h"
#include "ob.h"
#include "mm.h"
#include "ex.h"
#include "wnf.h"
#include "cm.h"
#include "ps.h"
#include "cc.h"
#include "io.h"
#include "kse.h"
#include "po.h"
#include "se.h"
#include "tm.h"
#include "ldr.h"
#ifndef _WINKD_
#include "kd.h"
#endif
#include "kd64.h"
#include "fsrtl.h"
#include "alpc.h"
#include "rtl.h"
#include "dbgk.h"
#include "spinlock.h"
#include "inbv.h"
#include "vdm.h"
#include "hal.h"
#include "hdl.h"
#include "icif.h"
#include "arch/intrin_i.h"
#include <arblib.h>

#endif
