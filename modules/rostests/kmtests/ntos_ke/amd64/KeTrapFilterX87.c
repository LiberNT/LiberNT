/*
 * PROJECT:     LiberNT kernel-mode tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test that a kernel-mode trap filter leaves the thread's x87 state alone
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <kmt_test.h>

#define X87_ZERO_DIVIDE 0x0004

static LONG FilterCalls;
static USHORT FilterControlWord;

static
LONG
TrapFilter(
    _In_ NTSTATUS ExceptionCode)
{
    __asm__ __volatile__("fnstcw %0" : "=m" (FilterControlWord));
    FilterCalls++;
    return (ExceptionCode == STATUS_ACCESS_VIOLATION) ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH;
}

START_TEST(KeTrapFilterX87)
{
    static const double One = 1.0, Zero = 0.0;
    volatile double Result;
    KFLOATING_SAVE FloatSave;
    PVOID Base = NULL;
    SIZE_T Size = PAGE_SIZE;
    USHORT ControlWord, StatusWord, AfterControlWord, AfterStatusWord;
    NTSTATUS Status;

    Status = ZwAllocateVirtualMemory(ZwCurrentProcess(), &Base, 0, &Size, MEM_RESERVE, PAGE_READWRITE);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
        return;

    Status = KeSaveFloatingPointState(&FloatSave);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (NT_SUCCESS(Status))
    {
        /* A masked divide by zero leaves its flag pending in the status word */
        __asm__ __volatile__("fninit\n\t"
                             "fldl %1\n\t"
                             "fdivl %2\n\t"
                             "fstpl %0"
                             : "=m" (Result) : "m" (One), "m" (Zero));
        __asm__ __volatile__("fnstcw %0" : "=m" (ControlWord));
        __asm__ __volatile__("fnstsw %0" : "=m" (StatusWord));
        ok(StatusWord & X87_ZERO_DIVIDE, "the divide by zero flag is not pending: %04x\n", StatusWord);

        FilterCalls = 0;
        Status = STATUS_SUCCESS;
        _SEH2_TRY
        {
            *(volatile ULONG *)Base = 1;
        }
        _SEH2_EXCEPT(TrapFilter(_SEH2_GetExceptionCode()))
        {
            Status = _SEH2_GetExceptionCode();
        }
        _SEH2_END;

        __asm__ __volatile__("fnstcw %0" : "=m" (AfterControlWord));
        __asm__ __volatile__("fnstsw %0" : "=m" (AfterStatusWord));
        ok_eq_hex(Status, STATUS_ACCESS_VIOLATION);
        ok_eq_long(FilterCalls, 1L);
        ok_eq_hex(FilterControlWord, ControlWord);
        ok_eq_hex(AfterControlWord, ControlWord);
        ok_eq_hex(AfterStatusWord & X87_ZERO_DIVIDE, X87_ZERO_DIVIDE);

        __asm__ __volatile__("fnclex");
        KeRestoreFloatingPointState(&FloatSave);
    }

    Size = 0;
    Status = ZwFreeVirtualMemory(ZwCurrentProcess(), &Base, &Size, MEM_RELEASE);
    ok_eq_hex(Status, STATUS_SUCCESS);
}
