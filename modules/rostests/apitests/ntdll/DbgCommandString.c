/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     The command-string debug service with pointers user mode may not hand over
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"

#ifdef _M_AMD64
/* mov eax, BREAKPOINT_COMMAND_STRING; int 2dh; int 3; ret */
static const UCHAR CommandStringService[] = { 0xB8, 0x05, 0x00, 0x00, 0x00, 0xCD, 0x2D, 0xCC, 0xC3 };

static LONG BreakpointsTaken;

static LONG CALLBACK
SkipServiceBreakpoint(_In_ PEXCEPTION_POINTERS Info)
{
    if (Info->ExceptionRecord->ExceptionCode != STATUS_BREAKPOINT)
        return EXCEPTION_CONTINUE_SEARCH;

    InterlockedIncrement(&BreakpointsTaken);
    Info->ContextRecord->Rip++;
    return EXCEPTION_CONTINUE_EXECUTION;
}
#endif

START_TEST(DbgCommandString)
{
#ifdef _M_AMD64
    typedef VOID (*COMMAND_STRING_SERVICE)(PVOID Name, PVOID Command);
    static STRING KernelBuffer = { 4, 4, (PCHAR)(ULONG_PTR)0xFFFFF78000000000ULL };
    static STRING NonCanonicalBuffer = { 4, 4, (PCHAR)(ULONG_PTR)0x2222222222222222ULL };
    PVOID Pointers[] =
    {
        (PVOID)(ULONG_PTR)0x1111111111111111ULL,
        (PVOID)(ULONG_PTR)0xFFFFF78000000000ULL,
        (PVOID)(ULONG_PTR)0xFFFF800000000000ULL,
        (PVOID)(ULONG_PTR)1,
        &KernelBuffer,
        &NonCanonicalBuffer,
    };
    COMMAND_STRING_SERVICE Service;
    PVOID Handler;
    ULONG Returned = 0;
    ULONG Name, Command;

    Service = VirtualAlloc(NULL, sizeof(CommandStringService), MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    ok(Service != NULL, "VirtualAlloc failed with %lu\n", GetLastError());
    if (Service == NULL)
        return;

    RtlCopyMemory((PVOID)Service, CommandStringService, sizeof(CommandStringService));
    FlushInstructionCache(GetCurrentProcess(), (PVOID)Service, sizeof(CommandStringService));
    Handler = AddVectoredExceptionHandler(1, SkipServiceBreakpoint);
    ok(Handler != NULL, "AddVectoredExceptionHandler failed\n");

    /* Every pair is refused without the kernel reading through it, and the caller carries on */
    for (Name = 0; Name < RTL_NUMBER_OF(Pointers); Name++)
    {
        for (Command = 0; Command < RTL_NUMBER_OF(Pointers); Command++)
        {
            Service(Pointers[Name], Pointers[Command]);
            Returned++;
        }
    }

    ok(Returned == RTL_NUMBER_OF(Pointers) * RTL_NUMBER_OF(Pointers), "Only %lu requests came back\n", Returned);
    ok(BreakpointsTaken == 0 || BreakpointsTaken == (LONG)Returned,
       "%ld of %lu requests fell through to their breakpoint\n", BreakpointsTaken, Returned);

    if (Handler != NULL)
        RemoveVectoredExceptionHandler(Handler);
    VirtualFree((PVOID)Service, 0, MEM_RELEASE);
#else
    skip("The command-string service sequence is only written for x64\n");
#endif
}
