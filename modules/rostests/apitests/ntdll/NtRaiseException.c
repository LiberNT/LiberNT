/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     NtRaiseException hands the handlers the caller's context and reads only the record the caller declares
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 *              Copyright 2026 John Fabienke <jfabienke@arqitekta.com>
 */

#include "precomp.h"
#include <malloc.h>

#define TEST_EXCEPTION_CODE 0xE0004E52

#ifdef _M_AMD64

static CONTEXT RaiseContext;
static CONTEXT SeenContext;
static volatile LONG Raised;
static volatile LONG Seen;

static
LONG
CALLBACK
RaiseHandler(
    _In_ PEXCEPTION_POINTERS ExceptionInfo)
{
    if (ExceptionInfo->ExceptionRecord->ExceptionCode != TEST_EXCEPTION_CODE)
        return EXCEPTION_CONTINUE_SEARCH;

    SeenContext = *ExceptionInfo->ContextRecord;
    Seen = 1;

    /* Carry on where the context was captured, with the registers it held */
    *ExceptionInfo->ContextRecord = RaiseContext;
    return EXCEPTION_CONTINUE_EXECUTION;
}

static
VOID
TestCallerContext(VOID)
{
    EXCEPTION_RECORD Record;
    CONTEXT Context;
    PVOID Handler;
    NTSTATUS Status;

    Raised = 0;
    Seen = 0;
    Handler = RtlAddVectoredExceptionHandler(TRUE, RaiseHandler);
    ok(Handler != NULL, "RtlAddVectoredExceptionHandler failed\n");
    if (Handler == NULL)
        return;

    RtlCaptureContext(&RaiseContext);
    if (!Raised)
    {
        Raised = 1;

        Context = RaiseContext;
        Context.Rbx = 0x1111111111111111ULL;
        Context.Rsi = 0x2222222222222222ULL;
        Context.Rdi = 0x3333333333333333ULL;
        Context.R12 = 0x4444444444444444ULL;
        Context.R13 = 0x5555555555555555ULL;
        Context.R14 = 0x6666666666666666ULL;
        Context.R15 = 0x7777777777777777ULL;
        Context.Xmm6.Low = 0x8888888888888888ULL;
        Context.Xmm15.High = 0x9999999999999999ULL;

        RtlZeroMemory(&Record, sizeof(Record));
        Record.ExceptionCode = TEST_EXCEPTION_CODE;
        Record.ExceptionAddress = (PVOID)Context.Rip;

        Status = NtRaiseException(&Record, &Context, TRUE);
        ok(FALSE, "NtRaiseException returned 0x%lx\n", Status);
    }

    RtlRemoveVectoredExceptionHandler(Handler);

    ok(Seen == 1, "The handler did not run\n");
    if (Seen != 1)
        return;

    ok(SeenContext.Rbx == 0x1111111111111111ULL, "Rbx %I64x\n", SeenContext.Rbx);
    ok(SeenContext.Rsi == 0x2222222222222222ULL, "Rsi %I64x\n", SeenContext.Rsi);
    ok(SeenContext.Rdi == 0x3333333333333333ULL, "Rdi %I64x\n", SeenContext.Rdi);
    ok(SeenContext.R12 == 0x4444444444444444ULL, "R12 %I64x\n", SeenContext.R12);
    ok(SeenContext.R13 == 0x5555555555555555ULL, "R13 %I64x\n", SeenContext.R13);
    ok(SeenContext.R14 == 0x6666666666666666ULL, "R14 %I64x\n", SeenContext.R14);
    ok(SeenContext.R15 == 0x7777777777777777ULL, "R15 %I64x\n", SeenContext.R15);
    ok(SeenContext.Xmm6.Low == 0x8888888888888888ULL, "Xmm6 %I64x\n", SeenContext.Xmm6.Low);
    ok(SeenContext.Xmm15.High == 0x9999999999999999ULL, "Xmm15 %I64x\n", SeenContext.Xmm15.High);
    ok(SeenContext.Rip == RaiseContext.Rip, "Rip %I64x, expected %I64x\n", SeenContext.Rip, RaiseContext.Rip);
}

#else

static
VOID
TestCallerContext(VOID)
{
    skip("The nonvolatile register checks are written for amd64\n");
}

#endif

#define RAISE_TEST_CODE 0xE0000223UL

static volatile LONG g_DeliveredCount;
static DWORD g_MainThreadId;
static EXCEPTION_RECORD g_DeliveredRecord;

static
ULONG_PTR
MakeParameter(
    _In_ ULONG Count,
    _In_ ULONG Index)
{
    ULONG64 Pattern = 0x5A3C96E100000000ULL |
                      0xA5000000ULL |
                      ((ULONG64)Count << 8) |
                      (ULONG64)Index;
    return (ULONG_PTR)Pattern;
}

static
LONG
CALLBACK
VectoredHandler(
    _In_ PEXCEPTION_POINTERS ExceptionInfo)
{
    PEXCEPTION_RECORD Record = ExceptionInfo->ExceptionRecord;
    ULONG Count, i;

    if (GetCurrentThreadId() != g_MainThreadId ||
        Record->ExceptionCode != RAISE_TEST_CODE ||
        (Record->ExceptionFlags & EXCEPTION_UNWIND))
    {
        return EXCEPTION_CONTINUE_SEARCH;
    }

    if (InterlockedIncrement(&g_DeliveredCount) == 1)
    {
        RtlZeroMemory(&g_DeliveredRecord, sizeof(g_DeliveredRecord));
        g_DeliveredRecord.ExceptionCode = Record->ExceptionCode;
        g_DeliveredRecord.ExceptionFlags = Record->ExceptionFlags;
        g_DeliveredRecord.ExceptionRecord = Record->ExceptionRecord;
        g_DeliveredRecord.ExceptionAddress = Record->ExceptionAddress;
        g_DeliveredRecord.NumberParameters = Record->NumberParameters;
        Count = Record->NumberParameters;
        if (Count > EXCEPTION_MAXIMUM_PARAMETERS)
            Count = EXCEPTION_MAXIMUM_PARAMETERS;
        for (i = 0; i < Count; i++)
            g_DeliveredRecord.ExceptionInformation[i] = Record->ExceptionInformation[i];
    }

    return EXCEPTION_CONTINUE_SEARCH;
}

static
DECLSPEC_NOINLINE
NTSTATUS
RaiseLeaf(
    _In_ PEXCEPTION_RECORD Record)
{
    CONTEXT Context;

    RtlZeroMemory(&Context, sizeof(Context));
    RtlCaptureContext(&Context);
    return NtRaiseException(Record, &Context, TRUE);
}

static
DECLSPEC_NOINLINE
NTSTATUS
RaiseWithStackReserve(
    _In_ PEXCEPTION_RECORD Record)
{
    /* Keep a frame-pointer reserve below the outer SEH registration on 32-bit x86. */
    volatile SIZE_T ReserveSize = 4096;
    volatile UCHAR *Reserve = _alloca(ReserveSize);
    NTSTATUS Status;

    RtlZeroMemory((PVOID)Reserve, ReserveSize);
    Status = RaiseLeaf(Record);
    Reserve[0] = (UCHAR)Status;
    return Status;
}

static
DECLSPEC_NOINLINE
NTSTATUS
InvokeRaiseException(
    _In_ PEXCEPTION_RECORD Record,
    _Out_ PBOOLEAN CaughtBySeh)
{
    _SEH2_VOLATILE BOOLEAN Caught = FALSE;
    _SEH2_VOLATILE NTSTATUS Status = STATUS_SUCCESS;

    _SEH2_TRY
    {
        Status = RaiseWithStackReserve(Record);
    }
    _SEH2_EXCEPT((DWORD)_SEH2_GetExceptionCode() == RAISE_TEST_CODE
                     ? EXCEPTION_EXECUTE_HANDLER
                     : EXCEPTION_CONTINUE_SEARCH)
    {
        Caught = TRUE;
        Status = STATUS_SUCCESS;
    }
    _SEH2_END;

    *CaughtBySeh = Caught;
    return Status;
}

static
VOID
TestBoundaryRecord(
    _In_ PUCHAR Pages,
    _In_ SIZE_T PageSize,
    _In_ BOOL IsWow64,
    _In_ ULONG Count,
    _In_ SIZE_T ReadableBytes)
{
    EXCEPTION_RECORD LocalRecord = {0};
    PUCHAR Target = Pages + PageSize - ReadableBytes;
    PVOID ExpectedAddress = (PVOID)(ULONG_PTR)MakeParameter;
    SIZE_T RequiredBytes;
    BOOL ExpectDelivery;
    BOOLEAN CaughtBySeh = FALSE;
    NTSTATUS Status;
    ULONG i;

    /* WoW64 thunks copy the full 32-bit record before entering the 64-bit kernel. */
    RequiredBytes = (sizeof(void *) == 4 && IsWow64)
        ? sizeof(EXCEPTION_RECORD)
        : FIELD_OFFSET(EXCEPTION_RECORD, ExceptionInformation) + Count * sizeof(ULONG_PTR);
    ExpectDelivery = (ReadableBytes >= RequiredBytes);

    RtlFillMemory(&LocalRecord, sizeof(LocalRecord), 0xCC);
    LocalRecord.ExceptionCode = RAISE_TEST_CODE;
    LocalRecord.ExceptionFlags = 0;
    LocalRecord.ExceptionRecord = NULL;
    LocalRecord.ExceptionAddress = ExpectedAddress;
    LocalRecord.NumberParameters = Count;
    for (i = 0; i < Count; i++)
        LocalRecord.ExceptionInformation[i] = MakeParameter(Count, i);

    RtlFillMemory(Pages, PageSize, 0xCC);
    RtlCopyMemory(Target, &LocalRecord, ReadableBytes);

    g_DeliveredCount = 0;
    RtlFillMemory(&g_DeliveredRecord, sizeof(g_DeliveredRecord), 0xDD);

    Status = InvokeRaiseException((PEXCEPTION_RECORD)Target, &CaughtBySeh);

    if (ExpectDelivery)
    {
        ok(CaughtBySeh, "Count %lu size %lu: exception was not delivered (status 0x%08lx)\n",
           Count, (ULONG)ReadableBytes, Status);
        ok(g_DeliveredCount == 1, "Count %lu size %lu: vectored handler ran %ld times\n",
           Count, (ULONG)ReadableBytes, g_DeliveredCount);
        ok_ntstatus(Status, STATUS_SUCCESS);
        ok_hex(g_DeliveredRecord.ExceptionCode, RAISE_TEST_CODE);
        ok_ptr(g_DeliveredRecord.ExceptionRecord, NULL);
        ok_ptr(g_DeliveredRecord.ExceptionAddress, ExpectedAddress);
        ok_dec(g_DeliveredRecord.NumberParameters, (INT)Count);
        for (i = 0; i < Count; i++)
            ok_size_t(g_DeliveredRecord.ExceptionInformation[i], MakeParameter(Count, i));
    }
    else
    {
        ok(!CaughtBySeh, "Count %lu size %lu: unexpected exception delivery\n",
           Count, (ULONG)ReadableBytes);
        ok(g_DeliveredCount == 0, "Count %lu size %lu: vectored handler ran %ld times\n",
           Count, (ULONG)ReadableBytes, g_DeliveredCount);
        ok(Status == STATUS_ACCESS_VIOLATION,
           "Count %lu size %lu: status 0x%08lx, expected STATUS_ACCESS_VIOLATION\n",
           Count, (ULONG)ReadableBytes, Status);
    }
}

static
VOID
TestRecordCapture(VOID)
{
    SYSTEM_INFO SystemInfo;
    SIZE_T PageSize;
    PUCHAR Pages;
    DWORD OldProtect;
    PVOID Handler;
    BOOL IsWow64 = FALSE;
    ULONG Count;

    g_MainThreadId = GetCurrentThreadId();
    IsWow64Process(GetCurrentProcess(), &IsWow64);

    GetSystemInfo(&SystemInfo);
    PageSize = SystemInfo.dwPageSize;
    ok(PageSize >= sizeof(EXCEPTION_RECORD), "Unexpected page size %lu\n", (ULONG)PageSize);
    if (PageSize < sizeof(EXCEPTION_RECORD))
        return;

    Pages = VirtualAlloc(NULL, 2 * PageSize, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    ok(Pages != NULL, "VirtualAlloc failed: %lu\n", GetLastError());
    if (!Pages)
        return;

    if (!VirtualProtect(Pages + PageSize, PageSize, PAGE_NOACCESS, &OldProtect))
    {
        ok(FALSE, "VirtualProtect failed: %lu\n", GetLastError());
        VirtualFree(Pages, 0, MEM_RELEASE);
        return;
    }

    Handler = AddVectoredExceptionHandler(1, VectoredHandler);
    ok(Handler != NULL, "AddVectoredExceptionHandler failed\n");
    if (Handler)
    {
        for (Count = 0; Count <= EXCEPTION_MAXIMUM_PARAMETERS; Count++)
        {
            TestBoundaryRecord(Pages,
                               PageSize,
                               IsWow64,
                               Count,
                               FIELD_OFFSET(EXCEPTION_RECORD, ExceptionInformation) +
                                   Count * sizeof(ULONG_PTR));
        }

        for (Count = 0; Count <= EXCEPTION_MAXIMUM_PARAMETERS; Count++)
        {
            TestBoundaryRecord(Pages,
                               PageSize,
                               IsWow64,
                               Count,
                               RTL_SIZEOF_THROUGH_FIELD(EXCEPTION_RECORD, NumberParameters));
        }

        TestBoundaryRecord(Pages,
                           PageSize,
                           IsWow64,
                           0,
                           FIELD_OFFSET(EXCEPTION_RECORD, NumberParameters));

        RemoveVectoredExceptionHandler(Handler);
    }

    VirtualFree(Pages, 0, MEM_RELEASE);
}

START_TEST(NtRaiseException)
{
    TestCallerContext();
    TestRecordCapture();
}
