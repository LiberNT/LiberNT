/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later),
 *              with portions under the BSD license in COPYING.ARM
 * PURPOSE:     i386 virtual-8086 trap emulation for the VDM
 * COPYRIGHT:   Copyright ReactOS Portable Systems Group
 *              Copyright Alex Ionescu <alex.ionescu@reactos.org>
 *              Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

#define KiVdmGetInstructionSize(x) ((x) & 0xFF)
#define KiVdmGetPrefixFlags(x)     ((x) & 0xFFFFFF00)

ULONG KeI386EFlagsAndMaskV86 = EFLAGS_USER_SANITIZE;
ULONG KeI386EFlagsOrMaskV86 = EFLAGS_INTERRUPT_MASK;
PVOID Ki386IopmSaveArea;
BOOLEAN KeI386VirtualIntExtensions = FALSE;
const PULONG KiNtVdmState = (PULONG)FIXED_NTVDMSTATE_LINEAR_PC_AT;

KiVdmUnhandledOpcode(F);
KiVdmUnhandledOpcode(OUTSW);
KiVdmUnhandledOpcode(OUTSB);
KiVdmUnhandledOpcode(INSB);
KiVdmUnhandledOpcode(INSW);
KiVdmUnhandledOpcode(NPX);
KiVdmUnhandledOpcode(INBimm);
KiVdmUnhandledOpcode(INWimm);
KiVdmUnhandledOpcode(OUTBimm);
KiVdmUnhandledOpcode(OUTWimm);
KiVdmUnhandledOpcode(INB);
KiVdmUnhandledOpcode(INW);
KiVdmUnhandledOpcode(OUTB);
KiVdmUnhandledOpcode(OUTW);
KiVdmUnhandledOpcode(HLT);
KiVdmUnhandledOpcode(INTO);
KiVdmUnhandledOpcode(INV);

BOOLEAN
FASTCALL
KiVdmOpcodePUSHF(
    _In_ PKTRAP_FRAME TrapFrame,
    _In_ ULONG Flags)
{
    ULONG Esp, V86EFlags, TrapEFlags;

    V86EFlags = *KiNtVdmState;
    V86EFlags &= ~EFLAGS_INTERRUPT_MASK;

    TrapEFlags = TrapFrame->EFlags;

    if(KeI386VirtualIntExtensions)
    {
        TrapEFlags &= ~EFLAGS_INTERRUPT_MASK;
        if(TrapEFlags & EFLAGS_VIF)
            TrapEFlags |= EFLAGS_INTERRUPT_MASK;
    }

    TrapEFlags &= (EFLAGS_ALIGN_CHECK | EFLAGS_NESTED_TASK | EFLAGS_INTERRUPT_MASK);

    V86EFlags |= TrapEFlags;
    V86EFlags |= EFLAGS_IOPL;

    Esp = (TrapFrame->HardwareSegSs << 4) + (USHORT)TrapFrame->HardwareEsp;

    if (KiVdmGetPrefixFlags(Flags) & PFX_FLAG_OPER32)
    {
        Esp -= 4;
        *(PULONG)Esp = V86EFlags;
    }
    else
    {
        Esp -= 2;
        *(PUSHORT)Esp = (USHORT)V86EFlags;
    }

    TrapFrame->HardwareEsp = Esp - (TrapFrame->HardwareSegSs << 4);
    TrapFrame->Eip += KiVdmGetInstructionSize(Flags);

    return TRUE;
}

BOOLEAN
FASTCALL
KiVdmOpcodePOPF(
    _In_ PKTRAP_FRAME TrapFrame,
    _In_ ULONG Flags)
{
    ULONG Esp, V86EFlags, EFlags, TrapEFlags;

    Esp = (TrapFrame->HardwareSegSs << 4) + (USHORT)TrapFrame->HardwareEsp;

    if (KiVdmGetPrefixFlags(Flags) & PFX_FLAG_OPER32)
    {
        EFlags = *(PULONG)Esp;
        Esp += 4;
    }
    else
    {
        EFlags = *(PUSHORT)Esp;
        Esp += 2;
    }

    TrapFrame->HardwareEsp = Esp - (TrapFrame->HardwareSegSs << 4);

    EFlags &= ~EFLAGS_IOPL;

    V86EFlags = EFlags & ~EFLAGS_NESTED_TASK;

    EFlags &= (EFLAGS_ALIGN_CHECK | EFLAGS_NESTED_TASK | EFLAGS_INTERRUPT_MASK);

    TrapEFlags = TrapFrame->EFlags;

    if(KeI386VirtualIntExtensions)
    {
        V86EFlags &= ~EFLAGS_VIF;
        if(V86EFlags & EFLAGS_INTERRUPT_MASK)
        {
            V86EFlags |= EFLAGS_VIF;

            V86EFlags &= ~EFLAGS_INTERRUPT_MASK;
        }
    }

    V86EFlags |= EFLAGS_V86_MASK;

    TrapFrame->EFlags |= V86EFlags;

    if (TrapEFlags & EFLAGS_V86_MASK) Ki386AdjustEsp0(TrapFrame);

    KiVdmClearVdmEFlags(EFLAGS_ALIGN_CHECK | EFLAGS_NESTED_TASK | EFLAGS_INTERRUPT_MASK);
    KiVdmSetVdmEFlags(EFlags);

    /* FIXME: Check for VDM interrupts */
    TrapFrame->Eip += KiVdmGetInstructionSize(Flags);

    return TRUE;
}

BOOLEAN
FASTCALL
KiVdmOpcodeINTnn(
    _In_ PKTRAP_FRAME TrapFrame,
    _In_ ULONG Flags)
{
    ULONG Esp, V86EFlags, TrapEFlags, Eip, Interrupt;

    TrapEFlags = TrapFrame->EFlags;

    V86EFlags = *KiNtVdmState;
    KiVdmClearVdmEFlags(EFLAGS_INTERRUPT_MASK);

    V86EFlags &= (EFLAGS_ALIGN_CHECK | EFLAGS_INTERRUPT_MASK);

    if (KeI386VirtualIntExtensions)
    {
        V86EFlags &= ~EFLAGS_INTERRUPT_MASK;
        if (TrapEFlags & EFLAGS_VIF)
        {
            V86EFlags |= EFLAGS_INTERRUPT_MASK;
        }
    }

    V86EFlags |= (TrapEFlags & ~EFLAGS_INTERRUPT_MASK);

    TrapFrame->EFlags = TrapEFlags & ~(EFLAGS_VIF | EFLAGS_NESTED_TASK | EFLAGS_TF);

    V86EFlags |= EFLAGS_IOPL;

    Esp = (TrapFrame->HardwareSegSs << 4) + TrapFrame->HardwareEsp;

    Esp -= 2;
    *(PUSHORT)(Esp) = (USHORT)V86EFlags;

    Esp -= 2;
    *(PUSHORT)(Esp) = (USHORT)TrapFrame->SegCs;

    Esp -= 2;
    *(PUSHORT)(Esp) = (USHORT)TrapFrame->Eip + KiVdmGetInstructionSize(Flags) + 1;

    TrapFrame->HardwareEsp = (USHORT)Esp;

    Eip = (TrapFrame->SegCs << 4) + TrapFrame->Eip;

    Eip += KiVdmGetInstructionSize(Flags);

    Interrupt = *(PUCHAR)Eip;

    Interrupt = *(PULONG)(Interrupt * 4);
    TrapFrame->Eip = (USHORT)Interrupt;

    Interrupt = (USHORT)(Interrupt >> 16);

    if (!(TrapFrame->EFlags & EFLAGS_V86_MASK))
    {
        Interrupt |= RPL_MASK;
        if (TrapFrame->SegCs == KGDT_R0_CODE)
        {
            TrapFrame->SegCs = Interrupt;
        }
        else
        {
            TrapFrame->SegCs = KGDT_R3_CODE | RPL_MASK;
        }
    }
    else
    {
        TrapFrame->SegCs = Interrupt;
    }

    return TRUE;
}

BOOLEAN
FASTCALL
KiVdmOpcodeIRET(
    _In_ PKTRAP_FRAME TrapFrame,
    _In_ ULONG Flags)
{
    ULONG Esp, V86EFlags, EFlags, TrapEFlags, Eip;

    Esp = (TrapFrame->HardwareSegSs << 4) + TrapFrame->HardwareEsp;

    if (KiVdmGetPrefixFlags(Flags) & PFX_FLAG_OPER32)
    {
        TrapFrame->Eip = *(PULONG)Esp;
        TrapFrame->SegCs = *(PUSHORT)(Esp + 4);

        TrapFrame->HardwareEsp += 12;

        EFlags = *(PULONG)(Esp + 8);
    }
    else
    {
        TrapFrame->Eip = *(PUSHORT)Esp;
        TrapFrame->SegCs = *(PUSHORT)(Esp + 2);

        TrapFrame->HardwareEsp += 6;

        EFlags = *(PUSHORT)(Esp + 4);
    }

    EFlags &= ~(EFLAGS_VIP | EFLAGS_VIF | EFLAGS_NESTED_TASK | EFLAGS_IOPL);
    V86EFlags = EFlags;

    if (KeI386VirtualIntExtensions)
    {
        if (EFlags & EFLAGS_INTERRUPT_MASK)
        {
            EFlags |= EFLAGS_VIF;
        }
    }

    EFlags |= EFLAGS_V86_MASK | EFLAGS_INTERRUPT_MASK;

    TrapEFlags = TrapFrame->EFlags;
    TrapFrame->EFlags = (TrapFrame->EFlags & EFLAGS_VIP) | EFlags;

    if (!(TrapEFlags & EFLAGS_V86_MASK)) Ki386AdjustEsp0(TrapFrame);

    KiVdmClearVdmEFlags(EFLAGS_INTERRUPT_MASK);
    KiVdmSetVdmEFlags(V86EFlags);

    Eip = (TrapFrame->SegCs << 4) + TrapFrame->Eip;
    if (*(PUSHORT)Eip == 0xC4C4)
    {
        VdmDispatchBop(TrapFrame);
    }
    else
    {
        /* FIXME: Check for VDM interrupts */
       DPRINT("FIXME: Check for VDM interrupts\n");
    }

    return TRUE;
}

BOOLEAN
FASTCALL
KiVdmOpcodeCLI(
    _In_ PKTRAP_FRAME TrapFrame,
    _In_ ULONG Flags)
{
    ASSERT(KeI386VirtualIntExtensions == FALSE);

    KiVdmClearVdmEFlags(EFLAGS_INTERRUPT_MASK);

    TrapFrame->Eip += KiVdmGetInstructionSize(Flags);

    return TRUE;
}

BOOLEAN
FASTCALL
KiVdmOpcodeSTI(
    _In_ PKTRAP_FRAME TrapFrame,
    _In_ ULONG Flags)
{
    ASSERT(KeI386VirtualIntExtensions == FALSE);

    KiVdmSetVdmEFlags(EFLAGS_INTERRUPT_MASK);

    TrapFrame->Eip += KiVdmGetInstructionSize(Flags);

    return TRUE;
}

BOOLEAN
FASTCALL
KiVdmHandleOpcode(
    _In_ PKTRAP_FRAME TrapFrame,
    _In_ ULONG Flags)
{
    ULONG Eip;

    Eip = (TrapFrame->SegCs << 4) + TrapFrame->Eip;
    Eip += KiVdmGetInstructionSize(Flags) - 1;

    switch (*(PUCHAR)Eip)
    {
        case 0xF:               return KiCallVdmHandler(F);
        case 0x26:              return KiCallVdmPrefixHandler(PFX_FLAG_ES);
        case 0x2E:              return KiCallVdmPrefixHandler(PFX_FLAG_CS);
        case 0x36:              return KiCallVdmPrefixHandler(PFX_FLAG_SS);
        case 0x3E:              return KiCallVdmPrefixHandler(PFX_FLAG_DS);
        case 0x64:              return KiCallVdmPrefixHandler(PFX_FLAG_FS);
        case 0x65:              return KiCallVdmPrefixHandler(PFX_FLAG_GS);
        case 0x66:              return KiCallVdmPrefixHandler(PFX_FLAG_OPER32);
        case 0x67:              return KiCallVdmPrefixHandler(PFX_FLAG_ADDR32);
        case 0xF0:              return KiCallVdmPrefixHandler(PFX_FLAG_LOCK);
        case 0xF2:              return KiCallVdmPrefixHandler(PFX_FLAG_REPNE);
        case 0xF3:              return KiCallVdmPrefixHandler(PFX_FLAG_REP);
        case 0x6C:              return KiCallVdmHandler(INSB);
        case 0x6D:              return KiCallVdmHandler(INSW);
        case 0x6E:              return KiCallVdmHandler(OUTSB);
        case 0x6F:              return KiCallVdmHandler(OUTSW);
        case 0x98:              return KiCallVdmHandler(NPX);
        case 0xD8:              return KiCallVdmHandler(NPX);
        case 0xD9:              return KiCallVdmHandler(NPX);
        case 0xDA:              return KiCallVdmHandler(NPX);
        case 0xDB:              return KiCallVdmHandler(NPX);
        case 0xDC:              return KiCallVdmHandler(NPX);
        case 0xDD:              return KiCallVdmHandler(NPX);
        case 0xDE:              return KiCallVdmHandler(NPX);
        case 0xDF:              return KiCallVdmHandler(NPX);
        case 0x9C:              return KiCallVdmHandler(PUSHF);
        case 0x9D:              return KiCallVdmHandler(POPF);
        case 0xCD:              return KiCallVdmHandler(INTnn);
        case 0xCE:              return KiCallVdmHandler(INTO);
        case 0xCF:              return KiCallVdmHandler(IRET);
        case 0xE4:              return KiCallVdmHandler(INBimm);
        case 0xE5:              return KiCallVdmHandler(INWimm);
        case 0xE6:              return KiCallVdmHandler(OUTBimm);
        case 0xE7:              return KiCallVdmHandler(OUTWimm);
        case 0xEC:              return KiCallVdmHandler(INB);
        case 0xED:              return KiCallVdmHandler(INW);
        case 0xEE:              return KiCallVdmHandler(OUTB);
        case 0xEF:              return KiCallVdmHandler(OUTW);
        case 0xF4:              return KiCallVdmHandler(HLT);
        case 0xFA:              return KiCallVdmHandler(CLI);
        case 0xFB:              return KiCallVdmHandler(STI);
        default:
            DPRINT1("Unhandled instruction: 0x%02x.\n", *(PUCHAR)Eip);
            return KiCallVdmHandler(INV);
    }
}

BOOLEAN
FASTCALL
KiVdmOpcodePrefix(
    _In_ PKTRAP_FRAME TrapFrame,
    _In_ ULONG Flags)
{
    Flags++;

    return KiVdmHandleOpcode(TrapFrame, Flags);
}

BOOLEAN
FASTCALL
Ki386HandleOpcodeV86(
    _In_ PKTRAP_FRAME TrapFrame)
{
    TrapFrame->Eip &= 0xFFFF;
    TrapFrame->HardwareEsp &= 0xFFFF;

    return KiVdmHandleOpcode(TrapFrame, 1);
}

ULONG_PTR
FASTCALL
KiExitV86Mode(
    _In_ PKTRAP_FRAME TrapFrame)
{
    PKPCR Pcr = KeGetPcr();
    ULONG_PTR StackFrameUnaligned;
    PKV8086_STACK_FRAME StackFrame;
    PKTHREAD Thread;
    PKV86_FRAME V86Frame;
    PFX_SAVE_AREA NpxFrame;

    StackFrameUnaligned = TrapFrame->Esi;
    StackFrame = (PKV8086_STACK_FRAME)(ROUND_UP(StackFrameUnaligned - 4, 16) + 4);
    V86Frame = &StackFrame->V86Frame;
    NpxFrame = &StackFrame->NpxArea;
    ASSERT((ULONG_PTR)NpxFrame % 16 == 0);

    Thread = KeGetCurrentThread();
    RtlCopyMemory(KiGetThreadNpxArea(Thread), NpxFrame, sizeof(FX_SAVE_AREA));

    Thread->InitialStack = (PVOID)((ULONG_PTR)V86Frame->ThreadStack + sizeof(FX_SAVE_AREA));

    Pcr->TSS->Esp0 = (ULONG_PTR)Thread->InitialStack;
    Pcr->TSS->Esp0 -= sizeof(KTRAP_FRAME) - FIELD_OFFSET(KTRAP_FRAME, V86Es);
    Pcr->TSS->Esp0 -= NPX_FRAME_LENGTH;

    Thread->Teb = V86Frame->ThreadTeb;
    KiSetTebBase(KeGetPcr(), V86Frame->ThreadTeb);

    _enable();
    return StackFrameUnaligned;
}

VOID
FASTCALL
KiEnterV86Mode(
    _In_ ULONG_PTR StackFrameUnaligned)
{
    PKTHREAD Thread;
    PKV8086_STACK_FRAME StackFrame = (PKV8086_STACK_FRAME)(ROUND_UP(StackFrameUnaligned - 4, 16) + 4);
    PKTRAP_FRAME TrapFrame = &StackFrame->TrapFrame;
    PKV86_FRAME V86Frame = &StackFrame->V86Frame;
    PFX_SAVE_AREA NpxFrame = &StackFrame->NpxArea;

    ASSERT((ULONG_PTR)NpxFrame % 16 == 0);

    TrapFrame->SegCs = KGDT_R0_CODE | RPL_MASK;
    TrapFrame->SegEs = TrapFrame->SegDs = TrapFrame->SegFs = TrapFrame->SegGs = 0;
    TrapFrame->ErrCode = 0;

    Thread = KeGetCurrentThread();
    V86Frame->ThreadStack = KiGetThreadNpxArea(Thread);

    V86Frame->ThreadTeb = Thread->Teb;
    V86Frame->PcrTeb = KeGetPcr()->NtTib.Self;

    TrapFrame->Eip = (ULONG_PTR)Ki386BiosCallReturnAddress;

    TrapFrame->Esi = StackFrameUnaligned;
    TrapFrame->Edi = (ULONG_PTR)_AddressOfReturnAddress() + 4;

    TrapFrame->EFlags = __readeflags() & 0x60DD7;
    TrapFrame->EFlags |= EFLAGS_INTERRUPT_MASK;

    TrapFrame->HardwareSegSs = KGDT_R3_DATA | RPL_MASK;
    TrapFrame->HardwareEsp = 0x11FFE;
    TrapFrame->ExceptionList = EXCEPTION_CHAIN_END;
    TrapFrame->Dr7 = 0;

    /* Set some debug fields if trap debugging is enabled */
    KiFillTrapFrameDebug(TrapFrame);

    _disable();

    RtlCopyMemory(NpxFrame, V86Frame->ThreadStack, sizeof(FX_SAVE_AREA));

    KeGetPcr()->NtTib.ExceptionList = EXCEPTION_CHAIN_END;

    KeGetPcr()->TSS->Esp0 = (ULONG_PTR)&TrapFrame->V86Es;

    Thread->InitialStack = V86Frame;

    Thread->Teb = (PTEB)TRAMPOLINE_TEB;
    KiSetTebBase(KeGetPcr(), (PVOID)TRAMPOLINE_TEB);

    _enable();

    NtVdmControl(VdmStartExecution, NULL);

    KiEoiHelper(TrapFrame);
}

VOID
NTAPI
Ke386SetIOPL(VOID)
{
    PKTHREAD Thread = KeGetCurrentThread();
    PKPROCESS Process = Thread->ApcState.Process;
    PKTRAP_FRAME TrapFrame;
    CONTEXT Context;

    Process->Iopl = TRUE;
    Thread->Iopl = TRUE;

    TrapFrame = KeGetTrapFrame(Thread);

    Context.ContextFlags = CONTEXT_CONTROL;
    KeTrapFrameToContext(TrapFrame, NULL, &Context);

    Context.EFlags |= EFLAGS_IOPL;

    KeContextToTrapFrame(&Context, NULL, TrapFrame, CONTEXT_CONTROL, UserMode);
}

NTSTATUS
NTAPI
Ke386CallBios(
    _In_ ULONG Int,
    _Out_ PCONTEXT Context)
{
    PUCHAR Trampoline = (PUCHAR)TRAMPOLINE_BASE;
    PTEB VdmTeb = (PTEB)TRAMPOLINE_TEB;
    PVDM_TIB VdmTib = (PVDM_TIB)TRAMPOLINE_TIB;
    ULONG ContextSize = FIELD_OFFSET(CONTEXT, ExtendedRegisters);
    PKTHREAD Thread = KeGetCurrentThread();
    PKTSS Tss = KeGetPcr()->TSS;
    PKPROCESS Process = Thread->ApcState.Process;
    PVDM_PROCESS_OBJECTS VdmProcessObjects;
    USHORT OldOffset, OldBase;

    RtlZeroMemory(VdmTeb, sizeof(TEB));

    *Trampoline++ = 0xCD;
    *Trampoline++ = (UCHAR)Int;
    *(PULONG)Trampoline = TRAMPOLINE_BOP;

    VdmTeb->Vdm = (PVOID)TRAMPOLINE_TIB;
    RtlZeroMemory(VdmTib, sizeof(VDM_TIB));
    VdmTib->Size = sizeof(VDM_TIB);

    *KiNtVdmState = 0;

    RtlCopyMemory(&VdmTib->VdmContext, Context, ContextSize);
    VdmTib->VdmContext.SegCs = (ULONG_PTR)Trampoline >> 4;
    VdmTib->VdmContext.SegSs = (ULONG_PTR)Trampoline >> 4;
    VdmTib->VdmContext.Eip = 0;
    VdmTib->VdmContext.Esp = 2 * PAGE_SIZE - sizeof(ULONG_PTR);
    VdmTib->VdmContext.EFlags |= EFLAGS_V86_MASK | EFLAGS_INTERRUPT_MASK;
    VdmTib->VdmContext.ContextFlags = CONTEXT_FULL;

    ASSERT(PsGetCurrentProcess()->VdmObjects == NULL);

    VdmProcessObjects = ExAllocatePoolWithTag(NonPagedPool,
                                              sizeof(VDM_PROCESS_OBJECTS),
                                              TAG_KERNEL);
    if (!VdmProcessObjects) return STATUS_NO_MEMORY;

    RtlZeroMemory(VdmProcessObjects, sizeof(VDM_PROCESS_OBJECTS));
    VdmProcessObjects->VdmTib = VdmTib;
    PsGetCurrentProcess()->VdmObjects = VdmProcessObjects;

    KeSetSystemAffinityThread(1);

    ASSERT(((PKIPCR)KeGetPcr())->GDT[KGDT_TSS / 8].LimitLow >=
            (0x2000 + IOPM_OFFSET - 1));
    RtlCopyMemory(Ki386IopmSaveArea, &Tss->IoMaps[0].IoMap, IOPM_SIZE);
    RtlZeroMemory(&Tss->IoMaps[0].IoMap, IOPM_SIZE);

    OldOffset = Process->IopmOffset;
    OldBase = Tss->IoMapBase;
    Process->IopmOffset = (USHORT)IOPM_OFFSET;
    Tss->IoMapBase = (USHORT)IOPM_OFFSET;

    Ki386SetupAndExitToV86Mode(VdmTeb);

    RtlCopyMemory(&Tss->IoMaps[0].IoMap, Ki386IopmSaveArea, IOPM_SIZE);
    Process->IopmOffset = OldOffset;
    Tss->IoMapBase = OldBase;

    KeRevertToUserAffinityThread();

    RtlCopyMemory(Context, &VdmTib->VdmContext, ContextSize);
    Context->ContextFlags = CONTEXT_FULL;

    ExFreePoolWithTag(PsGetCurrentProcess()->VdmObjects, TAG_KERNEL);
    PsGetCurrentProcess()->VdmObjects = NULL;

    return STATUS_SUCCESS;
}

BOOLEAN
NTAPI
Ke386IoSetAccessProcess(
    _In_ PKPROCESS Process,
    _In_ ULONG MapNumber)
{
    USHORT MapOffset;
    PKPRCB Prcb;
    KAFFINITY TargetProcessors;

    if(MapNumber > IOPM_COUNT)
        return FALSE;

    MapOffset = KiComputeIopmOffset(MapNumber);

    Process->IopmOffset = MapOffset;

    TargetProcessors = *KiProcessActiveProcessors(Process);
    Prcb = KeGetCurrentPrcb();
    if (TargetProcessors & Prcb->SetMember)
        KeGetPcr()->TSS->IoMapBase = MapOffset;

    return TRUE;
}

BOOLEAN
NTAPI
Ke386SetIoAccessMap(
    _In_ ULONG MapNumber,
    _In_ PKIO_ACCESS_MAP IopmBuffer)
{
    PKPROCESS CurrentProcess;
    PKPRCB Prcb;
    PVOID pt;

    if ((MapNumber > IOPM_COUNT) || (MapNumber == IO_ACCESS_MAP_NONE))
        return FALSE;

    Prcb = KeGetCurrentPrcb();

    pt = &(KeGetPcr()->TSS->IoMaps[MapNumber-1].IoMap);
    RtlMoveMemory(pt, (PVOID)IopmBuffer, IOPM_SIZE);
    CurrentProcess = Prcb->CurrentThread->ApcState.Process;
    KeGetPcr()->TSS->IoMapBase = CurrentProcess->IopmOffset;

    return TRUE;
}

BOOLEAN
NTAPI
Ke386QueryIoAccessMap(
    _In_ ULONG MapNumber,
    _In_ PKIO_ACCESS_MAP IopmBuffer)
{
    ULONG i;
    PVOID Map;
    PUCHAR p;

    if (MapNumber > IOPM_COUNT)
        return FALSE;

    if (MapNumber == IO_ACCESS_MAP_NONE)
    {
        p = (PUCHAR)IopmBuffer;
        for (i = 0; i < IOPM_SIZE; i++) {
            p[i] = (UCHAR)-1;
        }
    }
    else
    {
        Map = (PVOID)&(KeGetPcr()->TSS->IoMaps[MapNumber-1].IoMap);
        RtlMoveMemory((PVOID)IopmBuffer, Map, IOPM_SIZE);
    }

    return TRUE;
}
