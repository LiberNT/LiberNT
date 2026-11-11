/*
 * PROJECT:     LiberNT NT User-Mode DLL
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Current transaction of the calling thread
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntdll.h>

#define NDEBUG
#include <debug.h>

HANDLE
NTAPI
RtlGetCurrentTransaction(VOID)
{
    return NtCurrentTeb()->CurrentTransactionHandle;
}

LOGICAL
NTAPI
RtlSetCurrentTransaction(
    _In_opt_ HANDLE TransactionHandle)
{
    NtCurrentTeb()->CurrentTransactionHandle = TransactionHandle;
    return TRUE;
}
