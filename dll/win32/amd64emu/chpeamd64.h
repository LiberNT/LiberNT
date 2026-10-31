/*
 * PROJECT:     LiberNT AMD64 emulation host
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     AMD64 unwind types of the shared CHPE runtime in RV64X processes
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

#include "amd64emu.h"

typedef AMD64_CONTEXT CHPE_AMD64_CONTEXT, *PCHPE_AMD64_CONTEXT;
typedef EMU_AMD64_RUNTIME_FUNCTION CHPE_AMD64_RUNTIME_FUNCTION, *PCHPE_AMD64_RUNTIME_FUNCTION;
typedef EMU_AMD64_CONTEXT_POINTERS CHPE_AMD64_CONTEXT_POINTERS, *PCHPE_AMD64_CONTEXT_POINTERS;
typedef struct _CHPE_AMD64_HISTORY_TABLE CHPE_AMD64_HISTORY_TABLE, *PCHPE_AMD64_HISTORY_TABLE;

#define ChpeDbgPrint DbgPrint

PCHPE_AMD64_RUNTIME_FUNCTION
NTAPI
ChpepAmd64LookupFunctionTable(
    _In_ DWORD64 ControlPc,
    _Out_ PDWORD64 ImageBase,
    _Out_ PULONG Length);

PCHPE_AMD64_RUNTIME_FUNCTION
NTAPI
ChpepAmd64LookupFunctionEntry(
    _In_ DWORD64 ControlPc,
    _Out_ PDWORD64 ImageBase,
    _Inout_opt_ PCHPE_AMD64_HISTORY_TABLE HistoryTable);

PEXCEPTION_ROUTINE
NTAPI
ChpepAmd64VirtualUnwind(
    _In_ ULONG HandlerType,
    _In_ ULONG64 ImageBase,
    _In_ ULONG64 ControlPc,
    _In_ PCHPE_AMD64_RUNTIME_FUNCTION FunctionEntry,
    _Inout_ PCHPE_AMD64_CONTEXT Context,
    _Outptr_ PVOID *HandlerData,
    _Out_ PULONG64 EstablisherFrame,
    _Inout_opt_ PCHPE_AMD64_CONTEXT_POINTERS ContextPointers);
