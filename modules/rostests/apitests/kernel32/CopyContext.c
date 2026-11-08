/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test CopyContext exported by kernel32
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"

#if defined(_M_IX86)
#define CONTEXT_PC(Context) ((Context)->Eip)
#define CONTEXT_SP(Context) ((Context)->Esp)
#elif defined(_M_AMD64)
#define CONTEXT_PC(Context) ((Context)->Rip)
#define CONTEXT_SP(Context) ((Context)->Rsp)
#elif defined(_M_ARM64)
#define CONTEXT_PC(Context) ((Context)->Pc)
#define CONTEXT_SP(Context) ((Context)->Sp)
#endif

typedef BOOL (WINAPI *PFN_COPY_CONTEXT)(PCONTEXT Destination, DWORD ContextFlags, PCONTEXT Source);

START_TEST(CopyContext)
{
    static CONTEXT Source, Destination;
    PFN_COPY_CONTEXT pCopyContext;

    pCopyContext = (PFN_COPY_CONTEXT)GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "CopyContext");
    ok(pCopyContext != NULL, "kernel32 does not export CopyContext\n");
    if (!pCopyContext)
        return;

    RtlCaptureContext(&Source);
    trace("captured ContextFlags %08lx\n", Source.ContextFlags);
    ok((Source.ContextFlags & CONTEXT_CONTROL) == CONTEXT_CONTROL,
       "the captured context does not claim its control registers: %08lx\n", Source.ContextFlags);
    ok(CONTEXT_PC(&Source) != 0 && CONTEXT_SP(&Source) != 0, "the captured context has no control registers\n");

    RtlZeroMemory(&Destination, sizeof(Destination));
    Destination.ContextFlags = CONTEXT_FULL;
    ok(pCopyContext(&Destination, CONTEXT_CONTROL, &Source), "CopyContext failed with %lu\n", GetLastError());
    ok(CONTEXT_PC(&Destination) == CONTEXT_PC(&Source), "the instruction pointer was not copied\n");
    ok(CONTEXT_SP(&Destination) == CONTEXT_SP(&Source), "the stack pointer was not copied\n");
}
