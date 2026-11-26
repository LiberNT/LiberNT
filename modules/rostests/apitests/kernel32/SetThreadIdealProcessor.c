/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests for SetThreadIdealProcessor and ThreadIdealProcessor
 */

#include "precomp.h"
#include <ndk/psfuncs.h>

START_TEST(SetThreadIdealProcessor)
{
    SYSTEM_INFO SysInfo;
    DWORD Original, Prev, Expected;
    ULONG Proc;
    NTSTATUS Status;

    GetSystemInfo(&SysInfo);
    ok(SysInfo.dwNumberOfProcessors > 0,
       "dwNumberOfProcessors = %lu\n",
       SysInfo.dwNumberOfProcessors);

    SetLastError(0xdeadbeef);
    Original = SetThreadIdealProcessor(GetCurrentThread(), MAXIMUM_PROCESSORS);
    ok(Original != (DWORD)-1, "Query failed, error %lu\n", GetLastError());
    ok(Original < SysInfo.dwNumberOfProcessors,
       "Original ideal processor %lu >= %lu\n",
       Original,
       SysInfo.dwNumberOfProcessors);

    Prev = SetThreadIdealProcessor(GetCurrentThread(), 0);
    ok_long(Prev, Original);

    Prev = SetThreadIdealProcessor(GetCurrentThread(), MAXIMUM_PROCESSORS);
    ok_long(Prev, 0);

    Expected = 0;
    if (SysInfo.dwNumberOfProcessors > 1)
    {
        Prev = SetThreadIdealProcessor(GetCurrentThread(), 1);
        ok_long(Prev, 0);

        Prev = SetThreadIdealProcessor(GetCurrentThread(), MAXIMUM_PROCESSORS);
        ok_long(Prev, 1);
        Expected = 1;
    }

    Proc = MAXIMUM_PROCESSORS;
    Status = NtSetInformationThread(GetCurrentThread(),
                                    ThreadIdealProcessor,
                                    &Proc,
                                    sizeof(Proc));
    ok_ntstatus(Status, (NTSTATUS)Expected);

    SetLastError(0xdeadbeef);
    Prev = SetThreadIdealProcessor(GetCurrentThread(), MAXIMUM_PROCESSORS + 1);
    ok_long(Prev, (DWORD)-1);
    ok_err(ERROR_INVALID_PARAMETER);

    if (Original != (DWORD)-1)
    {
        Prev = SetThreadIdealProcessor(GetCurrentThread(), Original);
        ok_long(Prev, Expected);
    }
}
