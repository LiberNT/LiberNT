/*
 * PROJECT:     LiberNT kernel-mode tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Structured exception dispatch and unwind order tests
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#define KMT_EMULATE_KERNEL
#include <kmt_test.h>
#include <setjmp.h>

#define FIRST_STATUS STATUS_INVALID_PARAMETER_1
#define SECOND_STATUS STATUS_INVALID_PARAMETER_2

static volatile ULONG UnwoundFilterCalls;
static volatile ULONG RecursionFinallyCount;
static volatile BOOLEAN RaiseEnabled = TRUE;

static
DECLSPEC_NOINLINE
VOID
RaiseStatus(
    _In_ NTSTATUS Status)
{
    if (RaiseEnabled)
        ExRaiseStatus(Status);
}

static
VOID
TestExceptInsideFinally(VOID)
{
    volatile ULONG Flags = 0;
    volatile ULONG Count = 0;

    _SEH2_TRY
    {
        _SEH2_TRY
        {
            Flags |= 1;
            RaiseStatus(FIRST_STATUS);
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            ok_eq_ulong(Count, 0UL);
            Flags |= 2;
        }
        _SEH2_END;

        ok_eq_ulong(Count, 0UL);
        Flags |= 4;
    }
    _SEH2_FINALLY
    {
        Count++;
        Flags |= 8;
    }
    _SEH2_END;

    ok_eq_hex(Flags, 15UL);
    ok_eq_ulong(Count, 1UL);
}

static
VOID
TestExceptionInFinally(VOID)
{
    volatile ULONG Flags = 0;
    volatile ULONG Count = 0;

    _SEH2_TRY
    {
        _SEH2_TRY
        {
            _SEH2_TRY
            {
                _SEH2_TRY
                {
                    Flags |= 1;
                    RaiseStatus(FIRST_STATUS);
                }
                _SEH2_FINALLY
                {
                    Flags |= 2;
                    RaiseStatus(SECOND_STATUS);
                }
                _SEH2_END;
            }
            _SEH2_EXCEPT(_SEH2_GetExceptionCode() == FIRST_STATUS ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH)
            {
                Flags |= 4;
            }
            _SEH2_END;
        }
        _SEH2_FINALLY
        {
            Count++;
            Flags |= 8;
        }
        _SEH2_END;
    }
    _SEH2_EXCEPT(_SEH2_GetExceptionCode() == SECOND_STATUS ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH)
    {
        Flags |= 16;
    }
    _SEH2_END;

    ok_eq_hex(Flags, 27UL);
    ok_eq_ulong(Count, 1UL);
}

static
LONG
UnwoundFrameFilter(
    _In_ NTSTATUS Code)
{
    if (Code == SECOND_STATUS)
        UnwoundFilterCalls++;
    return EXCEPTION_CONTINUE_SEARCH;
}

static
DECLSPEC_NOINLINE
VOID
RaiseBelowFinally(VOID)
{
    _SEH2_TRY
    {
        RaiseStatus(FIRST_STATUS);
    }
    _SEH2_EXCEPT(UnwoundFrameFilter(_SEH2_GetExceptionCode()))
    {
    }
    _SEH2_END;
}

static
DECLSPEC_NOINLINE
VOID
RaiseInFinally(
    _Inout_ volatile ULONG *Flags)
{
    _SEH2_TRY
    {
        RaiseBelowFinally();
    }
    _SEH2_FINALLY
    {
        *Flags |= 2;
        RaiseStatus(SECOND_STATUS);
    }
    _SEH2_END;
}

static
VOID
TestExceptionInFinallyUnwoundFrame(VOID)
{
    volatile ULONG Flags = 0;

    UnwoundFilterCalls = 0;

    _SEH2_TRY
    {
        _SEH2_TRY
        {
            Flags |= 1;
            RaiseInFinally(&Flags);
        }
        _SEH2_EXCEPT(_SEH2_GetExceptionCode() == FIRST_STATUS ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH)
        {
            Flags |= 4;
        }
        _SEH2_END;
    }
    _SEH2_EXCEPT(_SEH2_GetExceptionCode() == SECOND_STATUS ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH)
    {
        Flags |= 8;
    }
    _SEH2_END;

    ok_eq_hex(Flags, 11UL);
    ok_eq_ulong(UnwoundFilterCalls, 0UL);
}

static
DECLSPEC_NOINLINE
VOID
RaiseThroughRecursion(
    _In_ ULONG Depth)
{
    _SEH2_TRY
    {
        _SEH2_TRY
        {
            if (Depth == 0)
                RaiseStatus(FIRST_STATUS);
            else
                RaiseThroughRecursion(Depth - 1);
        }
        _SEH2_EXCEPT(Depth == 2 ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH)
        {
        }
        _SEH2_END;
    }
    _SEH2_FINALLY
    {
        RecursionFinallyCount++;
    }
    _SEH2_END;
}

static
VOID
TestRecursion(VOID)
{
    RecursionFinallyCount = 0;
    RaiseThroughRecursion(2);
    ok_eq_ulong(RecursionFinallyCount, 3UL);
}

static
DECLSPEC_NOINLINE
VOID
JumpOutOfFinallyScope(
    _In_ jmp_buf JumpBuffer,
    _Inout_ volatile ULONG *Flags)
{
    _SEH2_TRY
    {
        *Flags |= 2;
        longjmp(JumpBuffer, 1);
    }
    _SEH2_FINALLY
    {
        *Flags |= 4;
    }
    _SEH2_END;
}

static
VOID
TestLongjmpIntoScope(VOID)
{
    volatile ULONG Flags = 0;
    volatile ULONG Count = 0;
    jmp_buf JumpBuffer;

    _SEH2_TRY
    {
        if (setjmp(JumpBuffer) == 0)
        {
            Flags |= 1;
            JumpOutOfFinallyScope(JumpBuffer, &Flags);
        }
        else
        {
            ok_eq_ulong(Count, 0UL);
            Flags |= 8;
        }
    }
    _SEH2_FINALLY
    {
        Count++;
        Flags |= 16;
    }
    _SEH2_END;

    ok_eq_hex(Flags, 31UL);
    ok_eq_ulong(Count, 1UL);
}

START_TEST(RtlSehUnwind)
{
    TestExceptInsideFinally();
    TestExceptionInFinally();
    TestExceptionInFinallyUnwoundFrame();
    TestRecursion();
    TestLongjmpIntoScope();
}
