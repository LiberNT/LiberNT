/*
 * PROJECT:     LiberNT AMD64 emulation host
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Run AMD64 code in a process whose images the native loader maps
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "amd64emu.h"

#define EMU_ATTACHED_STACK_SIZE 0x100000

#define EmuTebCpuArea(Teb) (*(PCHPE_V2_CPU_AREA_INFO *)((PUCHAR)(Teb) + CHPE_TEB_CPU_AREA_OFFSET))

PEMU_THREAD
EmuAttachedThread(VOID)
{
    PCHPE_V2_CPU_AREA_INFO CpuArea = EmuTebCpuArea(NtCurrentTeb());

    return CpuArea ? CpuArea->EmulatorData[0] : NULL;
}

PEMU_THREAD
EmuEnterAttachedThread(VOID)
{
    PEMU_THREAD Thread = EmuAttachedThread();
    PTEB Teb = NtCurrentTeb();
    AMD64_CONTEXT Context;
    SIZE_T Size = EMU_ATTACHED_STACK_SIZE;
    PVOID Stack = NULL;
    NTSTATUS Status;

    if (Thread) return Thread;

    Thread = RtlAllocateHeap(RtlGetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*Thread));
    if (!Thread) return NULL;

    /* FIXME: x64 frames live on a stack of their own above the thread stack, so AMD64 __chkstk never probes across; the TEB keeps describing the native stack */
    Status = NtAllocateVirtualMemory(NtCurrentProcess(), &Stack, 0, &Size, MEM_RESERVE | MEM_COMMIT | MEM_TOP_DOWN,
                                     PAGE_READWRITE);
    if (!NT_SUCCESS(Status)) goto Failure;
    if ((ULONG_PTR)Stack < (ULONG_PTR)Teb->NtTib.StackBase)
    {
        Status = STATUS_CONFLICTING_ADDRESSES;
        goto Failure;
    }
    Status = EmuCoreCreateCpu(Thread, Teb, &Thread->Cpu);
    if (!NT_SUCCESS(Status)) goto Failure;
    Status = EmuRegisterThread(Thread);
    if (!NT_SUCCESS(Status)) goto Failure;

    Thread->GuestTeb = Teb;
    Thread->HostDepth = 1;
    Thread->GuestStack.Base = (PUCHAR)Stack + Size;
    Thread->GuestStack.Limit = Stack;
    Thread->CpuArea.EmulatorData[0] = Thread;

    RtlZeroMemory(&Context, sizeof(Context));
    Context.ContextFlags = CONTEXT_AMD64_FULL | CONTEXT_AMD64_SEGMENTS;
    Context.Rsp = (ULONG64)(ULONG_PTR)Thread->GuestStack.Base;
    Context.MxCsr = 0x1F80;
    Context.FltSave.ControlWord = 0x27F;
    Context.FltSave.MxCsr = 0x1F80;
    Context.EFlags = 0x202;
    Context.SegCs = EMU_SEG_CODE;
    Context.SegDs = EMU_SEG_DATA;
    Context.SegEs = EMU_SEG_DATA;
    Context.SegSs = EMU_SEG_DATA;
    Context.SegGs = EMU_SEG_DATA;
    Context.SegFs = EMU_SEG_TEB;
    EmuCoreSetContext(Thread->Cpu, &Context);

    EmuTebCpuArea(Teb) = &Thread->CpuArea;
    return Thread;

Failure:
    if (Thread->Cpu) EmuCoreDestroyCpu(Thread->Cpu);
    if (Stack)
    {
        Size = 0;
        NtFreeVirtualMemory(NtCurrentProcess(), &Stack, &Size, MEM_RELEASE);
    }
    RtlFreeHeap(RtlGetProcessHeap(), 0, Thread);
    return NULL;
}

VOID
NTAPI
EmuDetachThread(VOID)
{
    PEMU_THREAD Thread = EmuAttachedThread();
    PVOID Stack;
    SIZE_T Size = 0;

    if (!Thread) return;
    EmuTebCpuArea(NtCurrentTeb()) = NULL;
    EmuUnregisterThread(Thread);
    EmuCoreDestroyCpu(Thread->Cpu);
    Stack = Thread->GuestStack.Limit;
    NtFreeVirtualMemory(NtCurrentProcess(), &Stack, &Size, MEM_RELEASE);
    RtlFreeHeap(RtlGetProcessHeap(), 0, Thread);
}

NTSTATUS
NTAPI
EmuAttachProcess(VOID)
{
    NTSTATUS Status;

    EmuProcess.Attached = TRUE;
    EmuProcess.GuestPeb = NtCurrentPeb();
    InitializeListHead(&EmuProcess.Threads);
    Status = RtlInitializeCriticalSection(&EmuProcess.ThreadLock);
    if (!NT_SUCCESS(Status)) return Status;
    Status = EmuCoreInitialize();
    if (!NT_SUCCESS(Status)) return Status;
    if (!RtlAddVectoredExceptionHandler(1, EmuVectoredHandler)) return STATUS_NO_MEMORY;
    return EmuInitializeHostCalls();
}
