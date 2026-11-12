/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Data shared by the WER crash report test and its runtime exception handler
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

#define WER_HANDLER_MAGIC           0x4C444857
#define WER_HANDLER_MODE_CLAIM      1
#define WER_HANDLER_MODE_NO_CLAIM   2
#define WER_HANDLER_MODE_DEBUGGER   3

typedef struct _WER_HANDLER_CONTEXT
{
    ULONG Magic;
    ULONG Mode;
    WCHAR MappingName[64];
    WCHAR DebuggerLaunch[MAX_PATH * 2];
} WER_HANDLER_CONTEXT, *PWER_HANDLER_CONTEXT;

typedef struct _WER_HANDLER_RESULT
{
    LONG EventCalls;
    LONG SignatureCalls;
    LONG DebuggerCalls;
    ULONG SignatureIndexMask;
    ULONG SignatureOrderErrors;
    ULONG InfoSize;
    ULONG ProcessId;
    ULONG ThreadId;
    ULONG ExceptionCode;
    ULONG EventNameSize;
    ULONG64 Context;
    ULONG64 ExceptionAddress;
    LONG ContextRead;
    WCHAR ReportId[64];
} WER_HANDLER_RESULT, *PWER_HANDLER_RESULT;
