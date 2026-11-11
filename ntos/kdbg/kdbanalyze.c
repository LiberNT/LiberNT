/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     What the debugger can tell by itself about the stop it is in
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#include "kdb.h"
#include "kdbpriv.h"
#define NDEBUG
#include <debug.h>

/* PRIVATE TYPES **************************************************************/

typedef enum _KDB_STOP_ROLE
{
    KdbRoleNone,
    KdbRoleAddress,
    KdbRoleInstruction,
    KdbRoleIrql,
    KdbRoleAccess,
    KdbRoleStatus,
    KdbRoleTrapFrame,
    KdbRoleExceptionRecord,
    KdbRoleContext,
    KdbRoleProcess,
    KdbRolePrcb,
    KdbRoleProcessor
} KDB_STOP_ROLE;

typedef struct _KDB_STOP_DECODER
{
    ULONG Code;
    PCSTR Name;
    UCHAR Roles[4];
} KDB_STOP_DECODER, *PKDB_STOP_DECODER;

/* PRIVATE DATA ***************************************************************/

/* The parameters as the Microsoft bug check reference describes them */
static const KDB_STOP_DECODER KdbpStopDecoders[] =
{
    { IRQL_NOT_LESS_OR_EQUAL, "IRQL_NOT_LESS_OR_EQUAL",
      { KdbRoleAddress, KdbRoleIrql, KdbRoleAccess, KdbRoleInstruction } },
    { KMODE_EXCEPTION_NOT_HANDLED, "KMODE_EXCEPTION_NOT_HANDLED",
      { KdbRoleStatus, KdbRoleInstruction, KdbRoleNone, KdbRoleNone } },
    { PAGE_FAULT_IN_NONPAGED_AREA, "PAGE_FAULT_IN_NONPAGED_AREA",
      { KdbRoleAddress, KdbRoleNone, KdbRoleInstruction, KdbRoleNone } },
    { SYSTEM_THREAD_EXCEPTION_NOT_HANDLED, "SYSTEM_THREAD_EXCEPTION_NOT_HANDLED",
      { KdbRoleStatus, KdbRoleInstruction, KdbRoleExceptionRecord, KdbRoleContext } },
    { KERNEL_MODE_EXCEPTION_NOT_HANDLED, "KERNEL_MODE_EXCEPTION_NOT_HANDLED",
      { KdbRoleStatus, KdbRoleInstruction, KdbRoleTrapFrame, KdbRoleNone } },
    { DRIVER_IRQL_NOT_LESS_OR_EQUAL, "DRIVER_IRQL_NOT_LESS_OR_EQUAL",
      { KdbRoleAddress, KdbRoleIrql, KdbRoleNone, KdbRoleInstruction } },
    { MANUALLY_INITIATED_CRASH, "MANUALLY_INITIATED_CRASH",
      { KdbRoleNone, KdbRoleNone, KdbRoleNone, KdbRoleNone } },
    { CRITICAL_PROCESS_DIED, "CRITICAL_PROCESS_DIED",
      { KdbRoleProcess, KdbRoleNone, KdbRoleNone, KdbRoleNone } },
    { CLOCK_WATCHDOG_TIMEOUT, "CLOCK_WATCHDOG_TIMEOUT",
      { KdbRoleNone, KdbRoleNone, KdbRolePrcb, KdbRoleProcessor } },
    { DPC_WATCHDOG_VIOLATION, "DPC_WATCHDOG_VIOLATION",
      { KdbRoleNone, KdbRoleNone, KdbRoleNone, KdbRoleNone } },
    { KERNEL_SECURITY_CHECK_FAILURE, "KERNEL_SECURITY_CHECK_FAILURE",
      { KdbRoleNone, KdbRoleTrapFrame, KdbRoleExceptionRecord, KdbRoleNone } },
};

/* PRIVATE FUNCTIONS **********************************************************/

static
BOOLEAN
KdbpAddressHasSymbol(
    _In_ ULONG_PTR Address,
    _Out_opt_ PBOOLEAN Driver)
{
    CHAR Module[64];
    CHAR Function[128];
    ULONG_PTR Displacement;
    PLDR_DATA_TABLE_ENTRY LdrEntry;
    BOOLEAN InKernel;

    if ((Address == 0) ||
        !KdbSymDescribeAddress((PVOID)Address,
                               Module,
                               sizeof(Module),
                               Function,
                               sizeof(Function),
                               &Displacement,
                               NULL,
                               0,
                               NULL))
    {
        return FALSE;
    }

    if (Driver != NULL)
        *Driver = (KiPcToFileHeader((PVOID)Address, &LdrEntry, TRUE, &InKernel) != NULL);

    return (Function[0] != ANSI_NULL);
}

static
VOID
KdbpAnalyzeAddress(
    _In_ ULONG_PTR Address)
{
    if (Address < 0x10000)
        KdbpPrint("NULL + 0x%Ix", Address);
    else if (Address <= (ULONG_PTR)MmHighestUserAddress)
        KdbpPrint("user address %p", (PVOID)Address);
    else if (!KdbSymPrintAddress((PVOID)Address, NULL))
        KdbpPrint("%p outside every module", (PVOID)Address);
}

static
VOID
KdbpPrintProcessName(
    _In_ ULONG_PTR Address)
{
    CHAR Name[16];

    RtlZeroMemory(Name, sizeof(Name));
    if ((Address <= (ULONG_PTR)MmHighestUserAddress) ||
        !NT_SUCCESS(KdbpSafeReadMemory(Name,
                                       ((PEPROCESS)Address)->ImageFileName,
                                       sizeof(Name) - 1)))
    {
        KdbPuts("process (cannot be read)");
        return;
    }

    KdbpPrint("process %s", Name);
}

static
VOID
KdbpAnalyzeParameter(
    _In_ ULONG Index,
    _In_ ULONG_PTR Value,
    _In_ KDB_STOP_ROLE Role)
{
    KdbpPrint("P%lu %p ", Index + 1, (PVOID)Value);

    switch (Role)
    {
        case KdbRoleAddress:
            KdbPuts("address ");
            KdbpAnalyzeAddress(Value);
            break;

        case KdbRoleInstruction:
            KdbPuts("instruction ");
            KdbpAnalyzeAddress(Value);
            break;

        case KdbRoleIrql:
            KdbpPrint("IRQL %lu", (ULONG)Value);
            break;

        case KdbRoleAccess:
            KdbpPrint("%s%s", (Value & 1) ? "write" : "read", (Value & 8) ? ", execute" : "");
            break;

        case KdbRoleStatus:
            KdbpPrint("status 0x%08lx", (ULONG)Value);
            break;

        case KdbRoleTrapFrame:
            KdbPuts("trap frame");
            break;

        case KdbRoleExceptionRecord:
            KdbPuts("exception record");
            break;

        case KdbRoleContext:
            KdbPuts("context record");
            break;

        case KdbRoleProcess:
            KdbpPrintProcessName(Value);
            break;

        case KdbRolePrcb:
            KdbPuts("processor block");
            break;

        case KdbRoleProcessor:
            KdbpPrint("processor %lu", (ULONG)Value);
            break;

        default:
            if (Value > (ULONG_PTR)MmHighestUserAddress)
                KdbpAnalyzeAddress(Value);
            break;
    }

    KdbPuts("\n");
}

static
VOID
KdbpAnalyzeStop(VOID)
{
    const KDB_STOP_DECODER *Decoder = NULL;
    ULONG Code = (ULONG)KiBugCheckData[0];
    ULONG Index;

    for (Index = 0; Index < RTL_NUMBER_OF(KdbpStopDecoders); Index++)
    {
        if (KdbpStopDecoders[Index].Code == Code)
        {
            Decoder = &KdbpStopDecoders[Index];
            break;
        }
    }

    KdbpPrint("\nANALYSIS: stop 0x%08lx %s\n", Code, (Decoder != NULL) ? Decoder->Name : "");

    for (Index = 0; Index < 4; Index++)
    {
        KdbpAnalyzeParameter(Index,
                             KiBugCheckData[Index + 1],
                             (Decoder != NULL) ? (KDB_STOP_ROLE)Decoder->Roles[Index] : KdbRoleNone);
    }
}

static
VOID
KdbpAnalyzeException(VOID)
{
    PEXCEPTION_RECORD64 Record = &KdbCurrentExceptionRecord;
    ULONG64 Access;

    KdbpPrint("\nANALYSIS: exception 0x%08lx at ", Record->ExceptionCode);
    KdbpAnalyzeAddress((ULONG_PTR)Record->ExceptionAddress);
    KdbPuts("\n");

    if (((Record->ExceptionCode == STATUS_ACCESS_VIOLATION) ||
         (Record->ExceptionCode == STATUS_IN_PAGE_ERROR)) &&
        (Record->NumberParameters >= 2))
    {
        Access = Record->ExceptionInformation[0];
        KdbpPrint("%s of ", (Access == 8) ? "execute" : ((Access == 1) ? "write" : "read"));
        KdbpAnalyzeAddress((ULONG_PTR)Record->ExceptionInformation[1]);
        KdbPuts("\n");
    }
}

static
VOID
KdbpPrintStackSymbols(
    _In_ PCONTEXT Context)
{
    ULONG_PTR Stack = (ULONG_PTR)KeGetContextStackRegister(Context);
    ULONG_PTR Value;
    BOOLEAN Driver;
    ULONG Slot;

    KdbPuts("STACK SYMBOLS:\n");

    for (Slot = 0; (Slot < 64) && !KdbpIsOutputAborted(); Slot++)
    {
        if (!NT_SUCCESS(KdbpSafeReadMemory(&Value,
                                           (PVOID)(Stack + Slot * sizeof(ULONG_PTR)),
                                           sizeof(Value))))
        {
            break;
        }

        if (!KdbpAddressHasSymbol(Value, &Driver))
            continue;

        KdbpPrint("  [sp+0x%03lx] %p ", Slot * (ULONG)sizeof(ULONG_PTR), (PVOID)Value);
        KdbSymPrintAddress((PVOID)Value, NULL);
        KdbPuts("\n");

        if (!Driver)
            break;
    }
}

static
VOID
KdbpAnalyzeFaultContext(
    _In_ PCONTEXT Context)
{
    static CONTEXT Walk;
    ULONG_PTR Pc = (ULONG_PTR)KeGetContextPc(Context);
    BOOLEAN Driver;
    ULONG Depth;

    if (KdbpAddressHasSymbol(Pc, NULL))
    {
        KdbpPrint("FAULTING IP: %p ", (PVOID)Pc);
        KdbSymPrintAddress((PVOID)Pc, NULL);
        KdbPuts(": ");
        KdbpDisassemble(Pc, 1);
        KdbPuts("\n");
    }
    else
    {
        KdbpPrint("FAULTING IP: %p ", (PVOID)Pc);
        KdbpAnalyzeAddress(Pc);
        KdbPuts(", a call or jump through a bad pointer\n");

        Walk = *Context;
        if (KiUnwindFrame(&Walk, FALSE))
        {
            KdbpPrint("CALLED FROM: %p ", (PVOID)KeGetContextPc(&Walk));
            KdbSymPrintAddress((PVOID)KeGetContextPc(&Walk), NULL);
            KdbPuts("\n");
        }

        KdbpPrintStackSymbols(Context);
    }

    Walk = *Context;
    for (Depth = 0; (Depth < 32) && !KdbpIsOutputAborted(); Depth++)
    {
        Pc = (ULONG_PTR)KeGetContextPc(&Walk);
        if (KdbpAddressHasSymbol(Pc, &Driver) && Driver)
        {
            KdbpPrint("FIRST DRIVER FRAME: %p ", (PVOID)Pc);
            KdbSymPrintAddress((PVOID)Pc, NULL);
            KdbPuts("\n");
            break;
        }

        if (!KiUnwindFrame(&Walk, (Depth != 0)))
            break;
    }
}

/* PUBLIC FUNCTIONS ***********************************************************/

BOOLEAN
KdbpCmdAnalyze(
    ULONG Argc,
    PCHAR Argv[])
{
    PCONTEXT Context = KdbCurrentTrapFrame;
    PETHREAD Thread = KdbCurrentThread;

    UNREFERENCED_PARAMETER(Argc);
    UNREFERENCED_PARAMETER(Argv);

    if (KiBugCheckData[0] != 0)
    {
        KdbpAnalyzeStop();

        if (KiPrcbContext(KeGetCurrentPrcb()) != NULL)
            Context = KiPrcbContext(KeGetCurrentPrcb());
    }
    else if (KdbCurrentExceptionRecordValid)
    {
        KdbpAnalyzeException();
    }
    else
    {
        KdbPuts("No stop or exception to analyze\n");
        return TRUE;
    }

    if (Thread != NULL)
    {
        KdbpPrint("THREAD: %p (%p.%p) ",
                  Thread,
                  Thread->Cid.UniqueProcess,
                  Thread->Cid.UniqueThread);
        KdbpPrintProcessName((ULONG_PTR)Thread->Tcb.ApcState.Process);
        KdbPuts("\n");
    }

    if (Context != NULL)
        KdbpAnalyzeFaultContext(Context);

    return TRUE;
}

/* EOF */
