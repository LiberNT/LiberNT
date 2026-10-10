/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     C++ exception unwinding inside an emulated process
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <windows.h>
#include <stdio.h>
#include "emulation_test.h"

namespace
{

struct CLEANUP
{
    LONG &Count;

    explicit CLEANUP(LONG &Counter) : Count(Counter)
    {
    }

    ~CLEANUP()
    {
        ++Count;
    }
};

struct FAILURE
{
    ULONG Code;
};

DECLSPEC_NOINLINE
void
ThrowThrough(LONG &Destroyed, ULONG Depth)
{
    CLEANUP Local(Destroyed);

    if (Depth == 0) throw FAILURE{0x1234};
    ThrowThrough(Destroyed, Depth - 1);
}

}

VOID
EmuTestCxxExceptions(VOID)
{
    LONG Destroyed = 0;
    ULONG Code = 0;
    bool Caught = false;

    try
    {
        CLEANUP Local(Destroyed);
        throw 42;
    }
    catch (int Value)
    {
        Caught = Value == 42;
    }
    CHECK(Caught && Destroyed == 1);

    Destroyed = 0;
    try
    {
        ThrowThrough(Destroyed, 7);
    }
    catch (const FAILURE &Failure)
    {
        Code = Failure.Code;
    }
    CHECK(Code == 0x1234 && Destroyed == 8);

    Caught = false;
    try
    {
        try
        {
            throw FAILURE{7};
        }
        catch (...)
        {
            throw;
        }
    }
    catch (const FAILURE &Failure)
    {
        Caught = Failure.Code == 7;
    }
    CHECK(Caught);
    printf("EMUTEST: C++ exceptions caught, %ld destructors unwound\n", Destroyed);
}
