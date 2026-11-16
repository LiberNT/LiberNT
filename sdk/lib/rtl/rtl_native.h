/*
 * COPYRIGHT:       See COPYING in the top level directory
 * PROJECT:         ReactOS System Libraries
 * FILE:            lib/rtl/rtl.h
 * PURPOSE:         Run-Time Libary Header
 * PROGRAMMER:      Alex Ionescu
 */

#ifndef RTL_NATIVE_H
#define RTL_NATIVE_H

/* Main RTL Header */
#include "rtl.h"

NTSTATUS
NTAPI
RtlWaitOnAddress(
    _In_ const VOID *Address,
    _In_ const VOID *CompareAddress,
    _In_ SIZE_T AddressSize,
    _In_opt_ const LARGE_INTEGER *Timeout);

VOID
NTAPI
RtlWakeAddressAll(
    _In_ const VOID *Address);

VOID
NTAPI
RtlWakeAddressSingle(
    _In_ const VOID *Address);

#endif /* RTL_NATIVE_H */
