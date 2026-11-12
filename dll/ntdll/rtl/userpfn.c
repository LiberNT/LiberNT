/*
 * PROJECT:     LiberNT NT User-Mode DLL
 * PURPOSE:     USER client procedure table registration
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntdll.h>

static const VOID *RtlpUserPfnClientA;
static const VOID *RtlpUserPfnClientW;
static const VOID *RtlpUserPfnClientWorker;
static ULONG RtlpUserPfnClientASize;
static ULONG RtlpUserPfnClientWSize;
static LONG RtlpUserPfnInitialized;

#define RTLP_USER_PFN_DEFWINDOWPROC 4
#define RTLP_USER_PFN_DIALOGWNDPROC 10

NTSTATUS
NTAPI
RtlInitializeNtUserPfn(
    _In_reads_bytes_(ClientASize) const VOID *ClientA,
    _In_ ULONG ClientASize,
    _In_reads_bytes_(ClientWSize) const VOID *ClientW,
    _In_ ULONG ClientWSize,
    _In_reads_bytes_(ClientWorkerSize) const VOID *ClientWorker,
    _In_ ULONG ClientWorkerSize)
{
    if (!ClientA || !ClientW || !ClientWorker ||
        !ClientASize || !ClientWSize || !ClientWorkerSize)
    {
        return STATUS_INVALID_PARAMETER;
    }

    if (InterlockedCompareExchange(&RtlpUserPfnInitialized, -1, FALSE) != FALSE)
        return STATUS_INVALID_PARAMETER;

    RtlpUserPfnClientA = ClientA;
    RtlpUserPfnClientW = ClientW;
    RtlpUserPfnClientWorker = ClientWorker;
    RtlpUserPfnClientASize = ClientASize;
    RtlpUserPfnClientWSize = ClientWSize;
    InterlockedExchange(&RtlpUserPfnInitialized, TRUE);
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
RtlRetrieveNtUserPfn(
    _Out_ const VOID **ClientA,
    _Out_ const VOID **ClientW,
    _Out_ const VOID **ClientWorker)
{
    if (ReadAcquire(&RtlpUserPfnInitialized) != TRUE)
        return STATUS_INVALID_PARAMETER;

    *ClientA = RtlpUserPfnClientA;
    *ClientW = RtlpUserPfnClientW;
    *ClientWorker = RtlpUserPfnClientWorker;
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
RtlResetNtUserPfn(VOID)
{
    if (InterlockedCompareExchange(&RtlpUserPfnInitialized, FALSE, TRUE) != TRUE)
        return STATUS_INVALID_PARAMETER;

    return STATUS_SUCCESS;
}

static LRESULT
RtlpCallNtUserPfn(
    _In_ BOOLEAN Unicode,
    _In_ ULONG Index,
    _In_ HWND hWnd,
    _In_ UINT Msg,
    _In_ WPARAM wParam,
    _In_ LPARAM lParam)
{
    const ULONG64 *Table;
    ULONG Size;
    WNDPROC Proc;

    if (ReadAcquire(&RtlpUserPfnInitialized) != TRUE)
        return 0;

    Table = Unicode ? RtlpUserPfnClientW : RtlpUserPfnClientA;
    Size = Unicode ? RtlpUserPfnClientWSize : RtlpUserPfnClientASize;
    if (Size / sizeof(ULONG64) <= Index)
        return 0;

    Proc = (WNDPROC)(ULONG_PTR)Table[Index];
    if (!Proc)
        return 0;

    return Proc(hWnd, Msg, wParam, lParam);
}

LRESULT
WINAPI
NtdllDefWindowProc_A(
    _In_ HWND hWnd,
    _In_ UINT Msg,
    _In_ WPARAM wParam,
    _In_ LPARAM lParam)
{
    return RtlpCallNtUserPfn(FALSE, RTLP_USER_PFN_DEFWINDOWPROC, hWnd, Msg, wParam, lParam);
}

LRESULT
WINAPI
NtdllDefWindowProc_W(
    _In_ HWND hWnd,
    _In_ UINT Msg,
    _In_ WPARAM wParam,
    _In_ LPARAM lParam)
{
    return RtlpCallNtUserPfn(TRUE, RTLP_USER_PFN_DEFWINDOWPROC, hWnd, Msg, wParam, lParam);
}

LRESULT
WINAPI
NtdllDialogWndProc_A(
    _In_ HWND hWnd,
    _In_ UINT Msg,
    _In_ WPARAM wParam,
    _In_ LPARAM lParam)
{
    return RtlpCallNtUserPfn(FALSE, RTLP_USER_PFN_DIALOGWNDPROC, hWnd, Msg, wParam, lParam);
}

LRESULT
WINAPI
NtdllDialogWndProc_W(
    _In_ HWND hWnd,
    _In_ UINT Msg,
    _In_ WPARAM wParam,
    _In_ LPARAM lParam)
{
    return RtlpCallNtUserPfn(TRUE, RTLP_USER_PFN_DIALOGWNDPROC, hWnd, Msg, wParam, lParam);
}
