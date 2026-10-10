/*
 * PROJECT:     LiberNT AMD64 emulation host
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Interface between the NT front end and the AMD64 CPU core
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

typedef struct _EMU_CPU *PEMU_CPU;

typedef enum _EMU_REGISTER
{
    EmuRax,
    EmuRcx,
    EmuRdx,
    EmuRbx,
    EmuRsp,
    EmuRbp,
    EmuRsi,
    EmuRdi,
    EmuR8,
    EmuR9,
    EmuR10,
    EmuR11,
    EmuR12,
    EmuR13,
    EmuR14,
    EmuR15,
    EmuRegisterCount
} EMU_REGISTER;

typedef struct _EMU_REGISTERS
{
    ULONG64 Gpr[EmuRegisterCount];
    ULONG64 Rip;
} EMU_REGISTERS, *PEMU_REGISTERS;

typedef enum _EMU_FAULT
{
    EmuFaultUnrelated,
    EmuFaultResume,
    EmuFaultGuest
} EMU_FAULT;

#define EMU_NO_INTERRUPT 0xFFFFFFFF

NTSTATUS
EmuCoreInitialize(VOID);

NTSTATUS
EmuCoreCreateCpu(
    _In_ PVOID Owner,
    _In_ PVOID GuestTeb,
    _Out_ PEMU_CPU *Cpu);

VOID
EmuCoreDestroyCpu(
    _In_ PEMU_CPU Cpu);

PEMU_REGISTERS
EmuCoreGetRegisters(
    _In_ PEMU_CPU Cpu);

VOID
EmuCoreGetContext(
    _In_ PEMU_CPU Cpu,
    _Inout_ AMD64_CONTEXT *Context);

VOID
EmuCoreSetContext(
    _In_ PEMU_CPU Cpu,
    _In_ const AMD64_CONTEXT *Context);

DECLSPEC_NORETURN
VOID
EmuCoreRun(
    _In_ PEMU_CPU Cpu);

VOID
EmuCoreInvalidate(
    _In_ PVOID Base,
    _In_ SIZE_T Size);

EMU_FAULT
EmuCoreFault(
    _In_ PEMU_CPU Cpu,
    _In_ BOOLEAN InService,
    _In_ const EXCEPTION_RECORD *NativeRecord,
    _Inout_ PCONTEXT NativeContext,
    _Out_ PEXCEPTION_RECORD GuestRecord,
    _Out_ PULONG Interrupt);

VOID
EmuCoreGetProcessor(
    _Out_ PUSHORT Level,
    _Out_ PUSHORT Revision);

VOID
EmuSystemService(
    _In_ PVOID Owner,
    _Inout_ PEMU_REGISTERS Registers);

#ifdef __cplusplus
}
#endif
