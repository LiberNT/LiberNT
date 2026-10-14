/*
 * PROJECT:     FreeLoader
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF
 * PURPOSE:     RISC-V64 UEFI loader runtime support
 */

#include <uefildr.h>
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

/* RISC-V has no I/O port space: legacy port probes find no device. */
UCHAR NTAPI READ_PORT_UCHAR(PUCHAR Port) { UNREFERENCED_PARAMETER(Port); return 0xFF; }
USHORT NTAPI READ_PORT_USHORT(PUSHORT Port) { UNREFERENCED_PARAMETER(Port); return 0xFFFF; }
ULONG NTAPI READ_PORT_ULONG(PULONG Port) { UNREFERENCED_PARAMETER(Port); return 0xFFFFFFFF; }
VOID NTAPI WRITE_PORT_UCHAR(PUCHAR Port, UCHAR Value) { UNREFERENCED_PARAMETER(Port); UNREFERENCED_PARAMETER(Value); }
VOID NTAPI WRITE_PORT_USHORT(PUSHORT Port, USHORT Value) { UNREFERENCED_PARAMETER(Port); UNREFERENCED_PARAMETER(Value); }
VOID NTAPI WRITE_PORT_ULONG(PULONG Port, ULONG Value) { UNREFERENCED_PARAMETER(Port); UNREFERENCED_PARAMETER(Value); }

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

    __asm__ __volatile__("csrci sstatus, 2" ::: "memory");
    for (;;)
        __asm__ __volatile__("wfi");
}

#define SBI_EXT_LEGACY_CONSOLE_PUTCHAR  0x01UL
#define SBI_EXT_BASE                    0x10UL
#define SBI_BASE_PROBE_EXTENSION        3UL
#define SBI_EXT_DBCN                    0x4442434EUL
#define SBI_DBCN_CONSOLE_WRITE_BYTE     2UL

typedef struct _SBI_RETURN
{
    LONG_PTR Error;
    ULONG_PTR Value;
} SBI_RETURN;

static
SBI_RETURN
SbiCall(
    _In_ ULONG_PTR Extension,
    _In_ ULONG_PTR Function,
    _In_ ULONG_PTR Argument0)
{
    register ULONG_PTR A0 __asm__("a0") = Argument0;
    register ULONG_PTR A1 __asm__("a1") = 0;
    register ULONG_PTR A6 __asm__("a6") = Function;
    register ULONG_PTR A7 __asm__("a7") = Extension;
    SBI_RETURN Result;

    __asm__ __volatile__("ecall"
                         : "+r"(A0), "+r"(A1)
                         : "r"(A6), "r"(A7)
                         : "memory");
    Result.Error = (LONG_PTR)A0;
    Result.Value = A1;
    return Result;
}

static
BOOLEAN
SbiProbeExtension(
    _In_ ULONG_PTR Extension)
{
    SBI_RETURN Result = SbiCall(SBI_EXT_BASE, SBI_BASE_PROBE_EXTENSION, Extension);

    return (Result.Error == 0) && (Result.Value != 0);
}

BOOLEAN
UefiArchSerialPutChar(
    _In_ UCHAR Character)
{
    static ULONG_PTR Extension;
    static BOOLEAN Probed;

    if (!Probed)
    {
        Probed = TRUE;
        if (SbiProbeExtension(SBI_EXT_DBCN))
            Extension = SBI_EXT_DBCN;
        else if (SbiProbeExtension(SBI_EXT_LEGACY_CONSOLE_PUTCHAR))
            Extension = SBI_EXT_LEGACY_CONSOLE_PUTCHAR;
    }

    if (Extension == SBI_EXT_DBCN)
        SbiCall(SBI_EXT_DBCN, SBI_DBCN_CONSOLE_WRITE_BYTE, Character);
    else if (Extension == SBI_EXT_LEGACY_CONSOLE_PUTCHAR)
        SbiCall(SBI_EXT_LEGACY_CONSOLE_PUTCHAR, 0, Character);
    else
        return FALSE;

    return TRUE;
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
