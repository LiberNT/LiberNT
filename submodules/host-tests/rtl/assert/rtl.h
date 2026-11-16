/*
 * PROJECT:     LiberNT host-native tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Host environment for compiling sdk/lib/rtl/assert.c
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

#include <stdint.h>
#include <stddef.h>

typedef uint32_t ULONG;
typedef int32_t NTSTATUS;
typedef char CHAR, *PCHAR, *PSTR, *PCH;
typedef const char *PCCH;
typedef void VOID, *PVOID, *HANDLE;

typedef struct _CONTEXT
{
    ULONG ContextFlags;
} CONTEXT, *PCONTEXT;

#define NTAPI
#define _In_
#define _In_opt_
#define FLG_DISABLE_DEBUG_PROMPTS 0x08000000
#define STATUS_UNSUCCESSFUL ((NTSTATUS)0xC0000001)
#define STATUS_ASSERTION_FAILURE ((NTSTATUS)0xC0000420)
#define ZwCurrentProcess() ((HANDLE)(intptr_t)-1)
#define ZwCurrentThread() ((HANDLE)(intptr_t)-2)

ULONG DbgPrint(const char *Format, ...);
ULONG DbgPrompt(PCCH Prompt, PCH Response, ULONG MaximumResponseLength);
VOID DbgBreakPoint(VOID);
VOID RtlCaptureContext(PCONTEXT Context);
ULONG RtlGetNtGlobalFlags(VOID);
VOID RtlRaiseStatus(NTSTATUS Status);
NTSTATUS ZwTerminateProcess(HANDLE Process, NTSTATUS ExitStatus);
NTSTATUS ZwTerminateThread(HANDLE Thread, NTSTATUS ExitStatus);
VOID NTAPI RtlAssert(PVOID FailedAssertion, PVOID FileName, ULONG LineNumber, PCHAR Message);
