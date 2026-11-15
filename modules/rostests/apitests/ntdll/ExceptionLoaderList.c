/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test that an exception still reaches its handler when the process's loader list loops
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"
#include <pseh/pseh2.h>

#define TEST_EXCEPTION_CODE 0xE0004C4C
#define CHILD_RESTORED 0
#define CHILD_NOT_RESTORED 2

static PPEB_LDR_DATA SavedLdr;
static PEB_LDR_DATA LoopLdr;
static LDR_DATA_TABLE_ENTRY LoopEntry;

static
LONG
CALLBACK
RestoreLoaderList(
    _In_ PEXCEPTION_POINTERS Info)
{
    NtCurrentPeb()->Ldr = SavedLdr;

    if (Info->ExceptionRecord->ExceptionCode == TEST_EXCEPTION_CODE)
        return EXCEPTION_CONTINUE_EXECUTION;

    return EXCEPTION_CONTINUE_SEARCH;
}

/* One entry for a real image whose links point back at itself, never at the list head */
static
VOID
LoopLoaderList(VOID)
{
    PPEB Peb = NtCurrentPeb();

    LoopEntry.DllBase = GetModuleHandleW(L"ntdll.dll");
    LoopEntry.InLoadOrderLinks.Flink = &LoopEntry.InLoadOrderLinks;
    LoopEntry.InLoadOrderLinks.Blink = &LoopEntry.InLoadOrderLinks;
    LoopLdr = *Peb->Ldr;
    LoopLdr.InLoadOrderModuleList.Flink = &LoopEntry.InLoadOrderLinks;
    LoopLdr.InLoadOrderModuleList.Blink = &LoopEntry.InLoadOrderLinks;

    SavedLdr = Peb->Ldr;
    Peb->Ldr = &LoopLdr;
}

static
VOID
RunChild(
    _In_ PCSTR Case)
{
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    AddVectoredExceptionHandler(1, RestoreLoaderList);

    if (strcmp(Case, "fault") == 0)
    {
        /* A processor fault goes through the kernel's dispatcher before user mode sees it */
        LoopLoaderList();
        _SEH2_TRY
        {
            *(volatile LONG *)NULL = 0;
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
        }
        _SEH2_END;
    }
    else
    {
        /* A raise without a debugger is dispatched in user mode, which first unwinds to the caller */
        LoopLoaderList();
        RaiseException(TEST_EXCEPTION_CODE, 0, 0, NULL);
    }

    ExitProcess((NtCurrentPeb()->Ldr == SavedLdr) ? CHILD_RESTORED : CHILD_NOT_RESTORED);
}

static
VOID
TestCase(
    _In_ PCSTR Self,
    _In_ PCSTR Case)
{
    STARTUPINFOA Startup = { sizeof(Startup) };
    PROCESS_INFORMATION Process;
    CHAR CommandLine[MAX_PATH * 2];
    DWORD Wait, ExitCode = 0xFFFFFFFF;

    sprintf(CommandLine, "\"%s\" ExceptionLoaderList %s", Self, Case);
    if (!CreateProcessA(NULL, CommandLine, NULL, NULL, FALSE, 0, NULL, NULL, &Startup, &Process))
    {
        ok(0, "%s: CreateProcessA failed with %lu\n", Case, GetLastError());
        return;
    }

    Wait = WaitForSingleObject(Process.hProcess, 30000);
    ok(Wait == WAIT_OBJECT_0, "%s: child still running after 30 s (wait 0x%lx)\n", Case, Wait);
    if (Wait != WAIT_OBJECT_0)
    {
        TerminateProcess(Process.hProcess, 1);
        WaitForSingleObject(Process.hProcess, 5000);
    }

    ok(GetExitCodeProcess(Process.hProcess, &ExitCode), "%s: GetExitCodeProcess failed with %lu\n", Case, GetLastError());
    ok(ExitCode == CHILD_RESTORED, "%s: child exit code 0x%lx\n", Case, ExitCode);

    CloseHandle(Process.hThread);
    CloseHandle(Process.hProcess);
}

START_TEST(ExceptionLoaderList)
{
    char **argv;
    int argc;

    argc = winetest_get_mainargs(&argv);
    if (argc >= 3)
    {
        RunChild(argv[2]);
        return;
    }

    TestCase(argv[0], "fault");
    TestCase(argv[0], "raise");
}
