/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests the thread current transaction and the transaction status codes
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"

typedef HANDLE (NTAPI *PFN_RTL_GET_CURRENT_TRANSACTION)(VOID);
typedef LOGICAL (NTAPI *PFN_RTL_SET_CURRENT_TRANSACTION)(HANDLE);

static PFN_RTL_GET_CURRENT_TRANSACTION pRtlGetCurrentTransaction;
static PFN_RTL_SET_CURRENT_TRANSACTION pRtlSetCurrentTransaction;

typedef struct _TRANSACTION_THREAD
{
    HANDLE Before;
    HANDLE TebBefore;
    LOGICAL SetResult;
    HANDLE After;
} TRANSACTION_THREAD, *PTRANSACTION_THREAD;

static
DWORD
WINAPI
TransactionThread(
    _In_ PVOID Parameter)
{
    PTRANSACTION_THREAD Result = Parameter;

    Result->Before = pRtlGetCurrentTransaction();
    Result->TebBefore = NtCurrentTeb()->CurrentTransactionHandle;
    Result->SetResult = pRtlSetCurrentTransaction((HANDLE)(ULONG_PTR)0x5678);
    Result->After = pRtlGetCurrentTransaction();
    return 0;
}

static
VOID
TestCurrentTransaction(VOID)
{
    TRANSACTION_THREAD Result;
    HANDLE Thread;
    LOGICAL Set;
    DWORD Wait;

    ok(pRtlGetCurrentTransaction() == NULL, "initial transaction %p\n", pRtlGetCurrentTransaction());
    ok(NtCurrentTeb()->CurrentTransactionHandle == NULL,
       "TEB transaction %p\n", NtCurrentTeb()->CurrentTransactionHandle);

    Set = pRtlSetCurrentTransaction((HANDLE)(ULONG_PTR)0x1234);
    ok(Set == TRUE, "RtlSetCurrentTransaction returned %lu\n", Set);
    ok(pRtlGetCurrentTransaction() == (HANDLE)(ULONG_PTR)0x1234,
       "transaction %p\n", pRtlGetCurrentTransaction());
    ok(NtCurrentTeb()->CurrentTransactionHandle == (HANDLE)(ULONG_PTR)0x1234,
       "TEB transaction %p\n", NtCurrentTeb()->CurrentTransactionHandle);

    NtCurrentTeb()->CurrentTransactionHandle = (HANDLE)(ULONG_PTR)0x4444;
    ok(pRtlGetCurrentTransaction() == (HANDLE)(ULONG_PTR)0x4444,
       "transaction after TEB write %p\n", pRtlGetCurrentTransaction());
    pRtlSetCurrentTransaction((HANDLE)(ULONG_PTR)0x1234);

    RtlZeroMemory(&Result, sizeof(Result));
    Thread = CreateThread(NULL, 0, TransactionThread, &Result, 0, NULL);
    ok(Thread != NULL, "CreateThread failed with %lu\n", GetLastError());
    if (Thread != NULL)
    {
        Wait = WaitForSingleObject(Thread, 5000);
        ok(Wait == WAIT_OBJECT_0, "wait returned %lu\n", Wait);
        CloseHandle(Thread);
        ok(Result.Before == NULL, "new thread transaction %p\n", Result.Before);
        ok(Result.TebBefore == NULL, "new thread TEB transaction %p\n", Result.TebBefore);
        ok(Result.SetResult == TRUE, "new thread set returned %lu\n", Result.SetResult);
        ok(Result.After == (HANDLE)(ULONG_PTR)0x5678, "new thread transaction after set %p\n", Result.After);
    }
    ok(pRtlGetCurrentTransaction() == (HANDLE)(ULONG_PTR)0x1234,
       "transaction after the other thread %p\n", pRtlGetCurrentTransaction());

    Set = pRtlSetCurrentTransaction(NULL);
    ok(Set == TRUE, "RtlSetCurrentTransaction(NULL) returned %lu\n", Set);
    ok(pRtlGetCurrentTransaction() == NULL, "transaction after clear %p\n", pRtlGetCurrentTransaction());
    ok(NtCurrentTeb()->CurrentTransactionHandle == NULL,
       "TEB transaction after clear %p\n", NtCurrentTeb()->CurrentTransactionHandle);
}

static
VOID
TestTransactionStatusMapping(VOID)
{
    static const struct
    {
        NTSTATUS Status;
        ULONG Error;
    } Map[] =
    {
        { STATUS_TRANSACTIONAL_CONFLICT, ERROR_TRANSACTIONAL_CONFLICT },
        { STATUS_INVALID_TRANSACTION, ERROR_INVALID_TRANSACTION },
        { STATUS_TRANSACTION_NOT_ACTIVE, ERROR_TRANSACTION_NOT_ACTIVE },
        { STATUS_RM_NOT_ACTIVE, ERROR_RM_NOT_ACTIVE },
        { STATUS_TRANSACTIONS_UNSUPPORTED_REMOTE, ERROR_TRANSACTIONS_UNSUPPORTED_REMOTE },
        { STATUS_TRANSACTION_ALREADY_ABORTED, ERROR_TRANSACTION_ALREADY_ABORTED },
        { STATUS_TRANSACTION_ALREADY_COMMITTED, ERROR_TRANSACTION_ALREADY_COMMITTED },
        { STATUS_CURRENT_TRANSACTION_NOT_VALID, ERROR_CURRENT_TRANSACTION_NOT_VALID },
        { STATUS_CANT_BREAK_TRANSACTIONAL_DEPENDENCY, ERROR_CANT_BREAK_TRANSACTIONAL_DEPENDENCY },
        { STATUS_EFS_NOT_ALLOWED_IN_TRANSACTION, ERROR_EFS_NOT_ALLOWED_IN_TRANSACTION },
        { STATUS_TRANSACTIONAL_OPEN_NOT_ALLOWED, ERROR_TRANSACTIONAL_OPEN_NOT_ALLOWED },
        { STATUS_OPERATION_NOT_SUPPORTED_IN_TRANSACTION, ERROR_OPERATION_NOT_SUPPORTED_IN_TRANSACTION },
        { STATUS_TRANSACTION_NOT_ENLISTED, ERROR_TRANSACTION_NOT_ENLISTED },
        { STATUS_RM_ALREADY_STARTED, ERROR_RM_ALREADY_STARTED },
        { STATUS_TXF_METADATA_ALREADY_PRESENT, ERROR_TXF_METADATA_ALREADY_PRESENT },
    };
    ULONG Index;
    ULONG Error;

    for (Index = 0; Index < RTL_NUMBER_OF(Map); Index++)
    {
        Error = RtlNtStatusToDosError(Map[Index].Status);
        ok(Error == Map[Index].Error, "status 0x%08lx mapped to %lu, expected %lu\n",
           Map[Index].Status, Error, Map[Index].Error);
    }
}

START_TEST(RtlCurrentTransaction)
{
    HMODULE Ntdll = GetModuleHandleW(L"ntdll.dll");

    TestTransactionStatusMapping();

    pRtlGetCurrentTransaction = (PFN_RTL_GET_CURRENT_TRANSACTION)GetProcAddress(Ntdll, "RtlGetCurrentTransaction");
    pRtlSetCurrentTransaction = (PFN_RTL_SET_CURRENT_TRANSACTION)GetProcAddress(Ntdll, "RtlSetCurrentTransaction");
    if (pRtlGetCurrentTransaction == NULL || pRtlSetCurrentTransaction == NULL)
    {
        skip("RtlGetCurrentTransaction/RtlSetCurrentTransaction not exported\n");
        return;
    }

    TestCurrentTransaction();
}
