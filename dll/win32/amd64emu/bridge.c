/*
 * PROJECT:     LiberNT AMD64 emulation host
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     AMD64 entry points of native functions whose interface depends on the architecture
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "chpeamd64.h"
#include "signature.h"

#define EmuBridgeArgument(Registers, Index) (((PULONG64)(ULONG_PTR)(Registers)->Gpr[EmuRsp])[(Index) + 1])

typedef PCHPE_AMD64_RUNTIME_FUNCTION (*PEMU_X64_TABLE_CALLBACK)(DWORD64 ControlPc, PVOID Context);

BOOLEAN NTAPI ChpepAmd64AddFunctionTable(PCHPE_AMD64_RUNTIME_FUNCTION FunctionTable, DWORD EntryCount, DWORD64 BaseAddress);
BOOLEAN NTAPI ChpepAmd64InstallFunctionTableCallback(DWORD64 TableIdentifier, DWORD64 BaseAddress, DWORD Length,
                                                     PEMU_X64_TABLE_CALLBACK Callback, PVOID Context,
                                                     PCWSTR OutOfProcessCallbackDll);
DWORD NTAPI ChpepAmd64AddGrowableFunctionTable(PVOID *DynamicTable, PCHPE_AMD64_RUNTIME_FUNCTION FunctionTable,
                                               DWORD EntryCount, DWORD MaximumEntryCount, ULONG_PTR RangeBase,
                                               ULONG_PTR RangeEnd);
VOID NTAPI ChpepAmd64GrowFunctionTable(PVOID DynamicTable, DWORD NewEntryCount);
VOID NTAPI ChpepAmd64DeleteGrowableFunctionTable(PVOID DynamicTable);
BOOLEAN NTAPI ChpepAmd64DeleteFunctionTable(PCHPE_AMD64_RUNTIME_FUNCTION FunctionTable);

typedef VOID (*PEMU_BRIDGE_ROUTINE)(
    _In_ PEMU_THREAD Thread,
    _Inout_ PEMU_REGISTERS Registers);

typedef struct _EMU_BRIDGE
{
    PCWSTR Module;
    PCSTR Name;
    PEMU_BRIDGE_ROUTINE Routine;
    BOOLEAN Raw;
    PVOID Native;
    ULONG64 Stub;
} EMU_BRIDGE, *PEMU_BRIDGE;

typedef struct _EMU_X64_SERVICE
{
    ULONG Native;
    ULONG Raw;
    ULONG Stub;
} EMU_X64_SERVICE, *PEMU_X64_SERVICE;

static const EMU_X64_SERVICE *EmuX64Services;
static ULONG EmuX64ServiceCount;
static ULONG_PTR EmuNtdllBase;

static
PVOID
EmuBridgeNative(
    _In_ PEMU_BRIDGE_ROUTINE Routine);

static
PVOID
EmuX64ServiceView(
    _In_opt_ PVOID Address)
{
    ULONG_PTR Rva = (ULONG_PTR)Address - EmuNtdllBase;
    ULONG Index;

    for (Index = 0; Address && Index < EmuX64ServiceCount; Index++)
    {
        if (EmuX64Services[Index].Native == Rva || EmuX64Services[Index].Raw == Rva)
            return (PVOID)(EmuNtdllBase + EmuX64Services[Index].Stub);
    }
    return Address;
}

static
PVOID
EmuNativeServiceView(
    _In_opt_ PVOID Address)
{
    ULONG_PTR Rva = (ULONG_PTR)Address - EmuNtdllBase;
    ULONG Index;

    for (Index = 0; Address && Index < EmuX64ServiceCount; Index++)
    {
        if (EmuX64Services[Index].Stub == Rva) return (PVOID)(EmuNtdllBase + EmuX64Services[Index].Native);
    }
    return Address;
}

static
VOID
EmuBridgeGetProcAddress(
    _In_ PEMU_THREAD Thread,
    _Inout_ PEMU_REGISTERS Registers)
{
    PVOID (NTAPI *GetProcAddressRoutine)(PVOID, PCSTR) = EmuBridgeNative(EmuBridgeGetProcAddress);

    UNREFERENCED_PARAMETER(Thread);
    Registers->Gpr[EmuRax] = (ULONG64)(ULONG_PTR)EmuX64ServiceView(
        GetProcAddressRoutine((PVOID)(ULONG_PTR)Registers->Gpr[EmuR10], (PCSTR)(ULONG_PTR)Registers->Gpr[EmuRdx]));
}

static
VOID
EmuBridgeLdrGetProcedureAddress(
    _In_ PEMU_THREAD Thread,
    _Inout_ PEMU_REGISTERS Registers)
{
    PVOID *Address = (PVOID *)(ULONG_PTR)Registers->Gpr[EmuR9];
    NTSTATUS Status;

    UNREFERENCED_PARAMETER(Thread);
    Status = LdrGetProcedureAddress((PVOID)(ULONG_PTR)Registers->Gpr[EmuR10],
                                    (PANSI_STRING)(ULONG_PTR)Registers->Gpr[EmuRdx],
                                    (ULONG)Registers->Gpr[EmuR8],
                                    Address);
    if (NT_SUCCESS(Status)) *Address = EmuX64ServiceView(*Address);
    Registers->Gpr[EmuRax] = (ULONG)Status;
}

static
VOID
EmuBridgeLdrGetProcedureAddressEx(
    _In_ PEMU_THREAD Thread,
    _Inout_ PEMU_REGISTERS Registers)
{
    PVOID *Address = (PVOID *)(ULONG_PTR)Registers->Gpr[EmuR9];
    NTSTATUS Status;

    UNREFERENCED_PARAMETER(Thread);
    Status = LdrGetProcedureAddressEx((PVOID)(ULONG_PTR)Registers->Gpr[EmuR10],
                                      (PANSI_STRING)(ULONG_PTR)Registers->Gpr[EmuRdx],
                                      (ULONG)Registers->Gpr[EmuR8],
                                      Address,
                                      (ULONG)EmuBridgeArgument(Registers, 4));
    if (NT_SUCCESS(Status)) *Address = EmuX64ServiceView(*Address);
    Registers->Gpr[EmuRax] = (ULONG)Status;
}

static
VOID
EmuBridgeWin32Result(
    _Inout_ PEMU_REGISTERS Registers,
    _In_ NTSTATUS Status)
{
    if (!NT_SUCCESS(Status)) RtlSetLastWin32Error(RtlNtStatusToDosError(Status));
    Registers->Gpr[EmuRax] = NT_SUCCESS(Status);
}

static
VOID
EmuBridgeNtGetContextThread(
    _In_ PEMU_THREAD Thread,
    _Inout_ PEMU_REGISTERS Registers)
{
    Registers->Gpr[EmuRax] = (ULONG)EmuGetContextThread((HANDLE)Registers->Gpr[EmuR10],
                                                        (AMD64_CONTEXT *)Registers->Gpr[EmuRdx]);
}

static
VOID
EmuBridgeGetThreadContext(
    _In_ PEMU_THREAD Thread,
    _Inout_ PEMU_REGISTERS Registers)
{
    EmuBridgeWin32Result(Registers, EmuGetContextThread((HANDLE)Registers->Gpr[EmuR10],
                                                        (AMD64_CONTEXT *)Registers->Gpr[EmuRdx]));
}

static
VOID
EmuBridgeNtSetContextThread(
    _In_ PEMU_THREAD Thread,
    _Inout_ PEMU_REGISTERS Registers)
{
    BOOLEAN Resumed;
    NTSTATUS Status;

    Status = EmuSetContextThread(Thread, (HANDLE)Registers->Gpr[EmuR10],
                                 (const AMD64_CONTEXT *)Registers->Gpr[EmuRdx], &Resumed);
    if (!Resumed) Registers->Gpr[EmuRax] = (ULONG)Status;
}

static
VOID
EmuBridgeSetThreadContext(
    _In_ PEMU_THREAD Thread,
    _Inout_ PEMU_REGISTERS Registers)
{
    BOOLEAN Resumed;
    NTSTATUS Status;

    Status = EmuSetContextThread(Thread, (HANDLE)Registers->Gpr[EmuR10],
                                 (const AMD64_CONTEXT *)Registers->Gpr[EmuRdx], &Resumed);
    if (!Resumed) EmuBridgeWin32Result(Registers, Status);
}

static
VOID
EmuBridgeNtSuspendThread(
    _In_ PEMU_THREAD Thread,
    _Inout_ PEMU_REGISTERS Registers)
{
    Registers->Gpr[EmuRax] = (ULONG)EmuSuspendThread((HANDLE)Registers->Gpr[EmuR10], (PULONG)Registers->Gpr[EmuRdx]);
}

static
VOID
EmuBridgeSuspendThread(
    _In_ PEMU_THREAD Thread,
    _Inout_ PEMU_REGISTERS Registers)
{
    ULONG Previous;
    NTSTATUS Status;

    Status = EmuSuspendThread((HANDLE)Registers->Gpr[EmuR10], &Previous);
    if (NT_SUCCESS(Status))
    {
        Registers->Gpr[EmuRax] = Previous;
        return;
    }
    RtlSetLastWin32Error(RtlNtStatusToDosError(Status));
    Registers->Gpr[EmuRax] = (ULONG)-1;
}

static
VOID
EmuBridgeRtlCaptureContext(
    _In_ PEMU_THREAD Thread,
    _Inout_ PEMU_REGISTERS Registers)
{
    AMD64_CONTEXT *Context = (AMD64_CONTEXT *)Registers->Gpr[EmuR10];
    ULONG64 Rsp = Registers->Gpr[EmuRsp];

    Context->ContextFlags = CONTEXT_AMD64_FULL | CONTEXT_AMD64_SEGMENTS;
    EmuCoreGetContext(Thread->Cpu, Context);
    Context->Rcx = Registers->Gpr[EmuR10];
    Context->Rip = *(PULONG64)Rsp;
    Context->Rsp = Rsp + sizeof(ULONG64);
}

static
VOID
EmuBridgeRtlUnwindEx(
    _In_ PEMU_THREAD Thread,
    _Inout_ PEMU_REGISTERS Registers)
{
    EmuX64UnwindEx(Thread, Registers->Gpr[EmuR10], Registers->Gpr[EmuRdx],
                   (PEXCEPTION_RECORD)Registers->Gpr[EmuR8], Registers->Gpr[EmuR9]);
}

static
VOID
EmuBridgeRtlRestoreContext(
    _In_ PEMU_THREAD Thread,
    _Inout_ PEMU_REGISTERS Registers)
{
    AMD64_CONTEXT Context = *(AMD64_CONTEXT *)Registers->Gpr[EmuR10];

    EmuX64RestoreContext(Thread, &Context, (PEXCEPTION_RECORD)Registers->Gpr[EmuRdx]);
}

static
VOID
EmuBridgeNtContinue(
    _In_ PEMU_THREAD Thread,
    _Inout_ PEMU_REGISTERS Registers)
{
    BOOLEAN TestAlert = (BOOLEAN)Registers->Gpr[EmuRdx];
    NTSTATUS Status;

    Status = EmuContinue(Thread, (const AMD64_CONTEXT *)Registers->Gpr[EmuR10]);
    if (!NT_SUCCESS(Status))
        Registers->Gpr[EmuRax] = (ULONG)Status;
    else if (TestAlert)
        NtTestAlert();
}

static
VOID
EmuBridgeNtRaiseException(
    _In_ PEMU_THREAD Thread,
    _Inout_ PEMU_REGISTERS Registers)
{
    NTSTATUS Status;

    Status = EmuRaiseException(Thread, (const EXCEPTION_RECORD *)Registers->Gpr[EmuR10],
                               (const AMD64_CONTEXT *)Registers->Gpr[EmuRdx], (BOOLEAN)Registers->Gpr[EmuR8]);
    if (!NT_SUCCESS(Status)) Registers->Gpr[EmuRax] = (ULONG)Status;
}

static
VOID
EmuBridgeRtlLookupFunctionEntry(
    _In_ PEMU_THREAD Thread,
    _Inout_ PEMU_REGISTERS Registers)
{
    PULONG64 ImageBasePointer = (PULONG64)Registers->Gpr[EmuRdx];
    ULONG64 ImageBase;

    Registers->Gpr[EmuRax] = (ULONG_PTR)EmuX64LookupFunctionEntry(Registers->Gpr[EmuR10], &ImageBase);
    *ImageBasePointer = ImageBase;
}

static
VOID
EmuBridgeRtlLookupFunctionTable(
    _In_ PEMU_THREAD Thread,
    _Inout_ PEMU_REGISTERS Registers)
{
    PULONG64 ImageBasePointer = (PULONG64)Registers->Gpr[EmuRdx];
    PULONG LengthPointer = (PULONG)Registers->Gpr[EmuR8];
    ULONG64 ImageBase;
    ULONG Length;

    Registers->Gpr[EmuRax] = (ULONG_PTR)EmuX64LookupFunctionTable(Registers->Gpr[EmuR10], &ImageBase, &Length);
    *ImageBasePointer = ImageBase;
    *LengthPointer = Length;
}

static
VOID
EmuBridgeRtlVirtualUnwind(
    _In_ PEMU_THREAD Thread,
    _Inout_ PEMU_REGISTERS Registers)
{
    Registers->Gpr[EmuRax] = EmuX64VirtualUnwind((ULONG)Registers->Gpr[EmuR10], Registers->Gpr[EmuRdx],
                                                 Registers->Gpr[EmuR8],
                                                 (PEMU_AMD64_RUNTIME_FUNCTION)Registers->Gpr[EmuR9],
                                                 (AMD64_CONTEXT *)EmuBridgeArgument(Registers, 4),
                                                 (PVOID *)EmuBridgeArgument(Registers, 5),
                                                 (PULONG64)EmuBridgeArgument(Registers, 6),
                                                 (PEMU_AMD64_CONTEXT_POINTERS)EmuBridgeArgument(Registers, 7));
}

static
VOID
EmuBridgeRtlAddFunctionTable(
    _In_ PEMU_THREAD Thread,
    _Inout_ PEMU_REGISTERS Registers)
{
    Registers->Gpr[EmuRax] = ChpepAmd64AddFunctionTable((PCHPE_AMD64_RUNTIME_FUNCTION)Registers->Gpr[EmuR10],
                                                        (DWORD)Registers->Gpr[EmuRdx], Registers->Gpr[EmuR8]);
}

static
VOID
EmuBridgeRtlDeleteFunctionTable(
    _In_ PEMU_THREAD Thread,
    _Inout_ PEMU_REGISTERS Registers)
{
    Registers->Gpr[EmuRax] = ChpepAmd64DeleteFunctionTable((PCHPE_AMD64_RUNTIME_FUNCTION)Registers->Gpr[EmuR10]);
}

static
VOID
EmuBridgeRtlInstallFunctionTableCallback(
    _In_ PEMU_THREAD Thread,
    _Inout_ PEMU_REGISTERS Registers)
{
    Registers->Gpr[EmuRax] = ChpepAmd64InstallFunctionTableCallback(Registers->Gpr[EmuR10], Registers->Gpr[EmuRdx],
                                                                    (DWORD)Registers->Gpr[EmuR8],
                                                                    (PEMU_X64_TABLE_CALLBACK)Registers->Gpr[EmuR9],
                                                                    (PVOID)EmuBridgeArgument(Registers, 4),
                                                                    (PCWSTR)EmuBridgeArgument(Registers, 5));
}

static
VOID
EmuBridgeRtlAddGrowableFunctionTable(
    _In_ PEMU_THREAD Thread,
    _Inout_ PEMU_REGISTERS Registers)
{
    Registers->Gpr[EmuRax] = ChpepAmd64AddGrowableFunctionTable((PVOID *)Registers->Gpr[EmuR10],
                                                                (PCHPE_AMD64_RUNTIME_FUNCTION)Registers->Gpr[EmuRdx],
                                                                (DWORD)Registers->Gpr[EmuR8],
                                                                (DWORD)Registers->Gpr[EmuR9],
                                                                EmuBridgeArgument(Registers, 4),
                                                                EmuBridgeArgument(Registers, 5));
}

static
VOID
EmuBridgeRtlGrowFunctionTable(
    _In_ PEMU_THREAD Thread,
    _Inout_ PEMU_REGISTERS Registers)
{
    ChpepAmd64GrowFunctionTable((PVOID)Registers->Gpr[EmuR10], (DWORD)Registers->Gpr[EmuRdx]);
}

static
VOID
EmuBridgeRtlDeleteGrowableFunctionTable(
    _In_ PEMU_THREAD Thread,
    _Inout_ PEMU_REGISTERS Registers)
{
    ChpepAmd64DeleteGrowableFunctionTable((PVOID)Registers->Gpr[EmuR10]);
}

static
VOID
EmuBridgeCSpecificHandler(
    _In_ PEMU_THREAD Thread,
    _Inout_ PEMU_REGISTERS Registers)
{
    Registers->Gpr[EmuRax] = (ULONG)EmuX64CSpecificHandler((PEXCEPTION_RECORD)Registers->Gpr[EmuR10],
                                                           Registers->Gpr[EmuRdx],
                                                           (AMD64_CONTEXT *)Registers->Gpr[EmuR8],
                                                           (PEMU_AMD64_DISPATCHER_CONTEXT)Registers->Gpr[EmuR9]);
}

static
VOID
EmuBridgeRtlAddVectoredExceptionHandler(
    _In_ PEMU_THREAD Thread,
    _Inout_ PEMU_REGISTERS Registers)
{
    Registers->Gpr[EmuRax] = (ULONG_PTR)EmuX64AddVectoredHandler(FALSE, (ULONG)Registers->Gpr[EmuR10],
                                                                 Registers->Gpr[EmuRdx]);
}

static
VOID
EmuBridgeRtlRemoveVectoredExceptionHandler(
    _In_ PEMU_THREAD Thread,
    _Inout_ PEMU_REGISTERS Registers)
{
    Registers->Gpr[EmuRax] = EmuX64RemoveVectoredHandler(FALSE, (PVOID)Registers->Gpr[EmuR10]);
}

static
VOID
EmuBridgeRtlAddVectoredContinueHandler(
    _In_ PEMU_THREAD Thread,
    _Inout_ PEMU_REGISTERS Registers)
{
    Registers->Gpr[EmuRax] = (ULONG_PTR)EmuX64AddVectoredHandler(TRUE, (ULONG)Registers->Gpr[EmuR10],
                                                                 Registers->Gpr[EmuRdx]);
}

static
VOID
EmuBridgeRtlRemoveVectoredContinueHandler(
    _In_ PEMU_THREAD Thread,
    _Inout_ PEMU_REGISTERS Registers)
{
    Registers->Gpr[EmuRax] = EmuX64RemoveVectoredHandler(TRUE, (PVOID)Registers->Gpr[EmuR10]);
}

static EMU_BRIDGE EmuBridges[] =
{
    { L"ntdll.dll", "NtGetContextThread", EmuBridgeNtGetContextThread },
    { L"ntdll.dll", "NtSetContextThread", EmuBridgeNtSetContextThread },
    { L"ntdll.dll", "NtSuspendThread", EmuBridgeNtSuspendThread },
    { L"ntdll.dll", "RtlCaptureContext", EmuBridgeRtlCaptureContext },
    { L"ntdll.dll", "NtContinue", EmuBridgeNtContinue },
    { L"ntdll.dll", "ZwContinue", EmuBridgeNtContinue },
    { L"ntdll.dll", "NtRaiseException", EmuBridgeNtRaiseException },
    { L"ntdll.dll", "ZwRaiseException", EmuBridgeNtRaiseException },
    { L"ntdll.dll", "RtlUnwindEx", EmuBridgeRtlUnwindEx, TRUE },
    { L"ntdll.dll", "RtlUnwind", EmuBridgeRtlUnwindEx, TRUE },
    { L"ntdll.dll", "RtlRestoreContext", EmuBridgeRtlRestoreContext, TRUE },
    { L"ntdll.dll", "RtlLookupFunctionEntry", EmuBridgeRtlLookupFunctionEntry, TRUE },
    { L"ntdll.dll", "RtlLookupFunctionTable", EmuBridgeRtlLookupFunctionTable, TRUE },
    { L"ntdll.dll", "RtlVirtualUnwind", EmuBridgeRtlVirtualUnwind, TRUE },
    { L"ntdll.dll", "RtlAddFunctionTable", EmuBridgeRtlAddFunctionTable, TRUE },
    { L"ntdll.dll", "RtlDeleteFunctionTable", EmuBridgeRtlDeleteFunctionTable, TRUE },
    { L"ntdll.dll", "RtlInstallFunctionTableCallback", EmuBridgeRtlInstallFunctionTableCallback, TRUE },
    { L"ntdll.dll", "RtlAddGrowableFunctionTable", EmuBridgeRtlAddGrowableFunctionTable, TRUE },
    { L"ntdll.dll", "RtlGrowFunctionTable", EmuBridgeRtlGrowFunctionTable, TRUE },
    { L"ntdll.dll", "RtlDeleteGrowableFunctionTable", EmuBridgeRtlDeleteGrowableFunctionTable, TRUE },
    { L"ntdll.dll", "__C_specific_handler", EmuBridgeCSpecificHandler, TRUE },
    { L"ntdll.dll", "RtlAddVectoredExceptionHandler", EmuBridgeRtlAddVectoredExceptionHandler, TRUE },
    { L"ntdll.dll", "RtlRemoveVectoredExceptionHandler", EmuBridgeRtlRemoveVectoredExceptionHandler, TRUE },
    { L"ntdll.dll", "RtlAddVectoredContinueHandler", EmuBridgeRtlAddVectoredContinueHandler, TRUE },
    { L"ntdll.dll", "RtlRemoveVectoredContinueHandler", EmuBridgeRtlRemoveVectoredContinueHandler, TRUE },
    { L"ntdll.dll", "LdrGetProcedureAddress", EmuBridgeLdrGetProcedureAddress },
    { L"ntdll.dll", "LdrGetProcedureAddressEx", EmuBridgeLdrGetProcedureAddressEx },
    { L"kernelbase.dll", "GetProcAddress", EmuBridgeGetProcAddress },
    { L"kernel32.dll", "GetProcAddress", EmuBridgeGetProcAddress },
    { L"kernelbase.dll", "GetThreadContext", EmuBridgeGetThreadContext },
    { L"kernelbase.dll", "SetThreadContext", EmuBridgeSetThreadContext },
    { L"kernelbase.dll", "SuspendThread", EmuBridgeSuspendThread },
    { L"kernel32.dll", "GetThreadContext", EmuBridgeGetThreadContext },
    { L"kernel32.dll", "SetThreadContext", EmuBridgeSetThreadContext },
    { L"kernel32.dll", "SuspendThread", EmuBridgeSuspendThread },
};

static
VOID
EmuResolveBridges(VOID)
{
    UNICODE_STRING Module;
    ANSI_STRING Name;
    PVOID Base, Native;
    ULONG Index;

    for (Index = 0; Index < RTL_NUMBER_OF(EmuBridges); Index++)
    {
        if (EmuBridges[Index].Native) continue;
        RtlInitUnicodeString(&Module, EmuBridges[Index].Module);
        if (!NT_SUCCESS(LdrGetDllHandle(NULL, NULL, &Module, &Base))) continue;
        RtlInitAnsiString(&Name, EmuBridges[Index].Name);
        if (NT_SUCCESS(LdrGetProcedureAddress(Base, &Name, 0, &Native)))
            InterlockedExchangePointer(&EmuBridges[Index].Native, Native);
    }
}

static
PVOID
EmuBridgeNative(
    _In_ PEMU_BRIDGE_ROUTINE Routine)
{
    ULONG Index;

    for (Index = 0; Index < RTL_NUMBER_OF(EmuBridges); Index++)
    {
        if (EmuBridges[Index].Routine == Routine && EmuBridges[Index].Native) return EmuBridges[Index].Native;
    }
    return NULL;
}

#define SVC_WRAP_(Name, Count) SVC_(Name, Count)

static const struct
{
    PCSTR Name;
    UCHAR Count;
} EmuServices[] =
{
#define SVC_(Name, Count) { #Name, Count },
#include <sysfuncs.h>
#undef SVC_
};

static const UCHAR EmuWin32kServiceCounts[] =
{
#define SVC_(Name, Count) Count,
#include <w32ksvc64.h>
#undef SVC_
};

#undef SVC_WRAP_

static struct
{
    ULONG_PTR Address;
    ULONG Index;
} EmuServiceEntries[2 * RTL_NUMBER_OF(EmuServices)];
static ULONG EmuServiceEntryCount;

NTSTATUS
EmuInitializeServiceEntries(VOID)
{
    static const UNICODE_STRING NtdllName = RTL_CONSTANT_STRING(L"ntdll.dll");
    static const PCSTR Prefixes[] = { "Nt", "Zw" };
    CHAR Name[96];
    PVOID Ntdll, Address;
    PIMAGE_NT_HEADERS Headers;
    PIMAGE_SECTION_HEADER Section;
    ULONG Index, Prefix;
    NTSTATUS Status;

    Status = LdrGetDllHandle(NULL, NULL, (PUNICODE_STRING)&NtdllName, &Ntdll);
    if (!NT_SUCCESS(Status)) return Status;
    Headers = RtlImageNtHeader(Ntdll);
    if (!Headers) return STATUS_INVALID_IMAGE_FORMAT;
    Section = IMAGE_FIRST_SECTION(Headers);
    for (Index = 0; Index < Headers->FileHeader.NumberOfSections; Index++, Section++)
    {
        if (strncmp((PCSTR)Section->Name, ".x64svm", IMAGE_SIZEOF_SHORT_NAME)) continue;
        EmuNtdllBase = (ULONG_PTR)Ntdll;
        EmuX64Services = (const EMU_X64_SERVICE *)((PUCHAR)Ntdll + Section->VirtualAddress);
        EmuX64ServiceCount = Section->Misc.VirtualSize / sizeof(EMU_X64_SERVICE);
        break;
    }
    for (Index = 0; Index < RTL_NUMBER_OF(EmuServices); Index++)
    {
        for (Prefix = 0; Prefix < RTL_NUMBER_OF(Prefixes); Prefix++)
        {
            if (strlen(EmuServices[Index].Name) + 3 > sizeof(Name)) continue;
            strcpy(Name, Prefixes[Prefix]);
            strcat(Name, EmuServices[Index].Name);
            Address = EmuNativeServiceView(RtlFindExportedRoutineByName(Ntdll, Name));
            if (!Address) continue;
            EmuServiceEntries[EmuServiceEntryCount].Address = (ULONG_PTR)Address;
            EmuServiceEntries[EmuServiceEntryCount].Index = Index;
            EmuServiceEntryCount++;
        }
    }
    return STATUS_SUCCESS;
}

static
LONG
EmuSignExtend(
    _In_ ULONG Value,
    _In_ ULONG Bits)
{
    return (LONG)(Value << (32 - Bits)) >> (32 - Bits);
}

static
ULONG
EmuCompressedImmediate(
    _In_ USHORT Code)
{
    return EmuSignExtend(((Code >> 7) & 0x20) | ((Code >> 2) & 0x1F), 6);
}

static
BOOLEAN
EmuDecodeServiceStub(
    _In_ ULONG64 Rip,
    _Out_ PULONG Service)
{
    PUCHAR Code = (PUCHAR)(ULONG_PTR)Rip;
    LONG Value = 0;
    USHORT Half;
    ULONG Word;
    BOOLEAN Loaded = FALSE;

    for (;;)
    {
        Half = *(PUSHORT)Code;
        if ((Half & 3) != 3)
        {
            if ((Half & 0xEF83) == 0x4281 && !Loaded) Value = EmuCompressedImmediate(Half);
            else if ((Half & 0xEF83) == 0x6281 && !Loaded) Value = EmuCompressedImmediate(Half) << 12;
            else if (((Half & 0xEF83) == 0x2281 || (Half & 0xEF83) == 0x0281) && Loaded) Value += EmuCompressedImmediate(Half);
            else return FALSE;
            Code += sizeof(USHORT);
            Loaded = TRUE;
            continue;
        }
        Word = *(PULONG)Code;
        if (Word == 0x00000073) break;
        if ((Word & 0x000FFFFF) == 0x00000293 && !Loaded) Value = (LONG)Word >> 20;
        else if ((Word & 0x00000FFF) == 0x000002B7 && !Loaded) Value = (LONG)(Word & 0xFFFFF000);
        else if (((Word & 0x000FFFFF) == 0x0002829B || (Word & 0x000FFFFF) == 0x00028293) && Loaded) Value += (LONG)Word >> 20;
        else return FALSE;
        Code += sizeof(ULONG);
        Loaded = TRUE;
    }
    if (!Loaded || Value < 0) return FALSE;
    *Service = (ULONG)Value;
    return TRUE;
}

static
ULONG64
EmuServiceEntry(
    _In_ ULONG64 Rip)
{
    static const CHAR Types[] =
    {
        EMU_HOST_TYPE_INT64,
        EMU_HOST_TYPE_INT32,
        EMU_HOST_TYPE_INT16,
        EMU_HOST_TYPE_INT8,
        EMU_HOST_TYPE_UINT16,
        EMU_HOST_TYPE_UINT8
    };
    CHAR Signature[EMU_HOST_MAX_ARGUMENTS + 2];
    ULONG Entry, Service, Index, Count, Argument;
    ULONG64 Kinds;
    BOOLEAN Decoded = FALSE;

    for (Entry = 0; Entry < EmuServiceEntryCount; Entry++)
    {
        if (EmuServiceEntries[Entry].Address == Rip) break;
    }
    _SEH2_TRY
    {
        if (Entry == EmuServiceEntryCount) Decoded = EmuDecodeServiceStub(Rip, &Service);
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Decoded = FALSE;
    }
    _SEH2_END;

    if (Entry < EmuServiceEntryCount)
    {
        Index = EmuServiceEntries[Entry].Index;
        Count = EmuServices[Index].Count;
        Kinds = EmuNtSignatures[Index];
    }
    else if (Decoded && (Service >> 12) == 0 && (Service & 0xFFF) < RTL_NUMBER_OF(EmuServices))
    {
        Index = Service & 0xFFF;
        Count = EmuServices[Index].Count;
        Kinds = EmuNtSignatures[Index];
    }
    else if (Decoded && (Service >> 12) == 1 && (Service & 0xFFF) < RTL_NUMBER_OF(EmuWin32kServiceCounts))
    {
        Index = Service & 0xFFF;
        Count = EmuWin32kServiceCounts[Index];
        Kinds = EmuWin32kSignatures[Index];
    }
    else
    {
        return 0;
    }

    if (Count > EMU_HOST_MAX_ARGUMENTS) return 0;
    Signature[0] = EMU_HOST_TYPE_INT64;
    for (Argument = 0; Argument < Count; Argument++, Kinds >>= EMU_ARGUMENT_BITS)
    {
        if ((Kinds & EMU_ARGUMENT_MASK) >= RTL_NUMBER_OF(Types)) return 0;
        Signature[Argument + 1] = Types[Kinds & EMU_ARGUMENT_MASK];
    }
    Signature[Count + 1] = ANSI_NULL;
    return EmuHostGetInterface()->GuestThunk((PVOID)(ULONG_PTR)Rip, Signature);
}

ULONG64
EmuBridgeEntry(
    _In_ ULONG64 Rip)
{
    UCHAR Code[] = { 0x4C, 0x8B, 0xD1, 0xB8, 0, 0, 0, 0, 0x0F, 0x05, 0xC3 };
    ULONG Service, Index;

    EmuResolveBridges();
    for (Index = 0; Index < RTL_NUMBER_OF(EmuBridges); Index++)
    {
        if ((ULONG_PTR)EmuBridges[Index].Native != Rip) continue;
        if (!EmuBridges[Index].Stub)
        {
            Service = EMU_BRIDGE_SERVICE + Index;
            RtlCopyMemory(&Code[4], &Service, sizeof(Service));
            EmuBridges[Index].Stub = EmuCreateStub(Code, sizeof(Code));
        }
        return EmuBridges[Index].Stub;
    }
    return EmuServiceEntry(Rip);
}

VOID
EmuX64SystemService(
    _In_ ULONG Service,
    _Inout_ PEMU_REGISTERS Registers)
{
    ULONG64 Target = 0;
    ULONG Entry;

    for (Entry = 0; !(Service >> 12) && Entry < EmuServiceEntryCount; Entry++)
    {
        if (EmuServiceEntries[Entry].Index != Service) continue;
        Target = EmuBridgeEntry(EmuServiceEntries[Entry].Address);
        break;
    }
    if (!Target)
    {
        Registers->Gpr[EmuRax] = (ULONG)STATUS_INVALID_SYSTEM_SERVICE;
        Registers->Rip = Registers->Gpr[EmuRcx];
        return;
    }
    Registers->Gpr[EmuRcx] = Registers->Gpr[EmuR10];
    Registers->Rip = Target;
}

BOOLEAN
EmuBridgeService(
    _In_ PEMU_THREAD Thread,
    _In_ ULONG Service,
    _Inout_ PEMU_REGISTERS Registers)
{
    ULONG Index = Service - EMU_BRIDGE_SERVICE;

    if (Service < EMU_BRIDGE_SERVICE || Index >= RTL_NUMBER_OF(EmuBridges)) return FALSE;
    if (EmuBridges[Index].Raw)
    {
        EmuBridges[Index].Routine(Thread, Registers);
        return TRUE;
    }

    _SEH2_TRY
    {
        EmuBridges[Index].Routine(Thread, Registers);
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Registers->Gpr[EmuRax] = (ULONG)_SEH2_GetExceptionCode();
    }
    _SEH2_END;
    return TRUE;
}
