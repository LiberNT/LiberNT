/*
 * COPYRIGHT:   See COPYING in the top level directory
 * PROJECT:     ReactOS NDIS library
 * FILE:        ndissys.h
 * PURPOSE:     NDIS library definitions
 * NOTES:       Spin lock acquire order:
 *                - Miniport list lock
 *                - Adapter list lock
 */
#ifndef __NDISSYS_H
#define __NDISSYS_H

#include <ndis.h>

#include "debug.h"
#include "miniport.h"
#include "protocol.h"
#include "efilter.h"
#include "buffer.h"

FORCEINLINE
ULONG
NdisICurrentProcessorIndex(VOID)
{
#if (NTDDI_VERSION >= NTDDI_WIN7)
    ULONG ProcessorIndex = KeGetCurrentProcessorIndex();
#else
    ULONG ProcessorIndex = KeGetCurrentProcessorNumber();
#endif

    ASSERT(ProcessorIndex < MAXIMUM_PROCESSORS);
    return ProcessorIndex;
}

/* Exported functions */
#ifndef EXPORT
#define EXPORT NTAPI
#endif

#define NDIS_VERSION ((6 << 16) | 89)

#define NDIS_TAG 'SIDN' // "NDIS"

#define MIN(value1, value2) \
    ((value1 < value2)? value1 : value2)

#define MAX(value1, value2) \
    ((value1 > value2)? value1 : value2)

#define RTL_CONSTANT_LARGE_INTEGER(quad_part) { { (quad_part), (quad_part)>>32 } }

#define ExInterlockedRemoveEntryList(_List,_Lock) \
 { KIRQL OldIrql; \
   KeAcquireSpinLock(_Lock, &OldIrql); \
   RemoveEntryList(_List); \
   KeReleaseSpinLock(_Lock, OldIrql); \
 }

/* missing protypes */
VOID
NTAPI
ExGetCurrentProcessorCounts(
   PULONG IdleTime,
   PULONG KernelAndUserTime,
   PULONG ProcessorNumber);

VOID
NTAPI
ExGetCurrentProcessorCpuUsage(
    PULONG CpuUsage);

/* portability fixes */
#ifdef _M_AMD64
#define KfReleaseSpinLock KeReleaseSpinLock
#define KefAcquireSpinLockAtDpcLevel KeAcquireSpinLockAtDpcLevel
#define KefReleaseSpinLockFromDpcLevel KeReleaseSpinLockFromDpcLevel
#endif

#endif /* __NDISSYS_H */
