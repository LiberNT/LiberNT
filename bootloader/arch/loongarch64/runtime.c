/*
 * PROJECT:     LiberNT Boot Loader
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     LoongArch64 UEFI loader runtime support
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 *              Copyright 2026 Zhou Ji Yang <yinglanxing@gmail.com>
 */


#include <blefi.h>
#include <stdarg.h>

#ifdef KeGetCurrentIrql
#undef KeGetCurrentIrql
#endif

extern volatile BOOLEAN BootServicesExitedFlag;

KIRQL
NTAPI
KeGetCurrentIrql(VOID)
{
    return PASSIVE_LEVEL;
}

BOOLEAN
FwArchDebugPutChar(
    _In_ UCHAR Character)
{
    UNREFERENCED_PARAMETER(Character);
    return FALSE;
}

DECLSPEC_NORETURN
VOID
FrLdrBugCheckWithMessage(
    ULONG BugCode,
    PCHAR File,
    ULONG Line,
    PCSTR Format,
    ...)
{
    CHAR Detail[320];
    CHAR Message[512];
    va_list Arguments;

    va_start(Arguments, Format);
    RtlStringCbVPrintfA(Detail, sizeof(Detail), Format, Arguments);
    va_end(Arguments);

    RtlStringCbPrintfA(Message,
                       sizeof(Message),
                       "FreeLdr bug check %lu at %s:%lu\n%s",
                       BugCode,
                       File ? File : "<unknown>",
                       Line,
                       Detail);

    if (!BootServicesExitedFlag)
        UiMessageBoxCritical(Message);

    for (;;)
        __asm__ __volatile__("idle 0" ::: "memory");
}

VOID
NTAPI
RtlCaptureContext(
    _Out_ PCONTEXT ContextRecord)
{
    RtlZeroMemory(ContextRecord, sizeof(*ContextRecord));
}

PRUNTIME_FUNCTION
NTAPI
RtlLookupFunctionEntry(
    _In_ ULONG64 ControlPc,
    _Out_ PULONG64 ImageBase,
    _Inout_opt_ PUNWIND_HISTORY_TABLE HistoryTable)
{
    UNREFERENCED_PARAMETER(ControlPc);
    UNREFERENCED_PARAMETER(HistoryTable);
    *ImageBase = 0;
    return NULL;
}

PEXCEPTION_ROUTINE
NTAPI
RtlVirtualUnwind(
    _In_ ULONG HandlerType,
    _In_ ULONG64 ImageBase,
    _In_ ULONG64 ControlPc,
    _In_opt_ PRUNTIME_FUNCTION FunctionEntry,
    _Inout_ PCONTEXT ContextRecord,
    _Out_opt_ PVOID *HandlerData,
    _Out_ PULONG64 EstablisherFrame,
    _Inout_opt_ PKNONVOLATILE_CONTEXT_POINTERS ContextPointers)
{
    UNREFERENCED_PARAMETER(HandlerType);
    UNREFERENCED_PARAMETER(ImageBase);
    UNREFERENCED_PARAMETER(ControlPc);
    UNREFERENCED_PARAMETER(FunctionEntry);
    UNREFERENCED_PARAMETER(ContextRecord);
    UNREFERENCED_PARAMETER(ContextPointers);
    if (HandlerData)
        *HandlerData = NULL;
    *EstablisherFrame = 0;
    return NULL;
}

VOID
NTAPI
RtlUnwind(
    _In_opt_ PVOID TargetFrame,
    _In_opt_ PVOID TargetIp,
    _In_opt_ PEXCEPTION_RECORD ExceptionRecord,
    _In_ PVOID ReturnValue)
{
    UNREFERENCED_PARAMETER(TargetFrame);
    UNREFERENCED_PARAMETER(TargetIp);
    UNREFERENCED_PARAMETER(ExceptionRecord);
    UNREFERENCED_PARAMETER(ReturnValue);
}
