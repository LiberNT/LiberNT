/*
 * PROJECT:     ReactOS SDK
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     API definitions for api-ms-win-core-synch-l1
 * COPYRIGHT:   Copyright 2024 Timo Kreuzer (timo.kreuzer@reactos.org)
 */

#pragma once
#define _SYNCHAPI_H_

#ifdef __cplusplus
extern "C" {
#endif

WINBASEAPI
BOOL
WINAPI
InitializeCriticalSectionEx(
    _Out_ LPCRITICAL_SECTION lpCriticalSection,
    _In_ DWORD dwSpinCount,
    _In_ DWORD Flags);

WINBASEAPI
BOOL
WINAPI
WaitOnAddress(
    _In_ volatile VOID *Address,
    _In_ PVOID CompareAddress,
    _In_ SIZE_T AddressSize,
    _In_ DWORD dwMilliseconds);

WINBASEAPI
VOID
WINAPI
WakeByAddressAll(
    _In_ PVOID Address);

WINBASEAPI
VOID
WINAPI
WakeByAddressSingle(
    _In_ PVOID Address);

typedef RTL_BARRIER SYNCHRONIZATION_BARRIER;
typedef PRTL_BARRIER PSYNCHRONIZATION_BARRIER;
typedef PRTL_BARRIER LPSYNCHRONIZATION_BARRIER;

#define SYNCHRONIZATION_BARRIER_FLAGS_SPIN_ONLY  0x01
#define SYNCHRONIZATION_BARRIER_FLAGS_BLOCK_ONLY 0x02
#define SYNCHRONIZATION_BARRIER_FLAGS_NO_DELETE  0x04

WINBASEAPI
BOOL
WINAPI
EnterSynchronizationBarrier(
    _Inout_ LPSYNCHRONIZATION_BARRIER lpBarrier,
    _In_ DWORD dwFlags);

WINBASEAPI
BOOL
WINAPI
InitializeSynchronizationBarrier(
    _Out_ LPSYNCHRONIZATION_BARRIER lpBarrier,
    _In_ LONG lTotalThreads,
    _In_ LONG lSpinCount);

WINBASEAPI
BOOL
WINAPI
DeleteSynchronizationBarrier(
    _Inout_ LPSYNCHRONIZATION_BARRIER lpBarrier);

#ifdef __cplusplus
} // extern "C"
#endif
