/*
 * PROJECT:     LiberNT FreeLoader
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Long mode exception handling for the PC-AT (BIOS) loader
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <freeldr.h>
#include <arch/pc/x86common.h>

#include <debug.h>
DBG_DEFAULT_CHANNEL(WARNING);

#define FRLDR_TRAP_VECTORS 32

typedef struct _FRLDR_TRAP_STATE
{
    ULONG64 R15, R14, R13, R12, R11, R10, R9, R8;
    ULONG64 Rdi, Rsi, Rbp, Rbx, Rdx, Rcx, Rax;
    ULONG64 Vector;
    ULONG64 ErrorCode;
    ULONG64 Rip, SegCs, EFlags, Rsp, SegSs;
} FRLDR_TRAP_STATE, *PFRLDR_TRAP_STATE;

typedef struct _FRLDR_IDT_GATE
{
    USHORT OffsetLow;
    USHORT Selector;
    UCHAR Ist;
    UCHAR Type;
    USHORT OffsetMiddle;
    ULONG OffsetHigh;
    ULONG Reserved;
} FRLDR_IDT_GATE;

#pragma pack(push, 1)
typedef struct _FRLDR_IDT_POINTER
{
    USHORT Limit;
    ULONG64 Base;
} FRLDR_IDT_POINTER;
#pragma pack(pop)

extern ULONG_PTR FrLdrTrapStubTable[FRLDR_TRAP_VECTORS];
extern FRLDR_IDT_POINTER FrLdrIdtPointer;

VOID
NTAPI
i386PrintExceptionText(ULONG TrapIndex, PKTRAP_FRAME TrapFrame, PKSPECIAL_REGISTERS Special);

static FRLDR_IDT_GATE DECLSPEC_ALIGN(16) FrLdrIdt[FRLDR_TRAP_VECTORS];
static BOOLEAN FrLdrInTrap = FALSE;

VOID
FrLdrAmd64InitializeIdt(VOID)
{
    ULONG i;

    for (i = 0; i < FRLDR_TRAP_VECTORS; i++)
    {
        ULONG_PTR Handler = FrLdrTrapStubTable[i];

        FrLdrIdt[i].OffsetLow = (USHORT)Handler;
        FrLdrIdt[i].Selector = LMODE_CS;
        FrLdrIdt[i].Ist = 0;
        FrLdrIdt[i].Type = 0x8E;
        FrLdrIdt[i].OffsetMiddle = (USHORT)(Handler >> 16);
        FrLdrIdt[i].OffsetHigh = (ULONG)(Handler >> 32);
        FrLdrIdt[i].Reserved = 0;
    }

    FrLdrIdtPointer.Limit = sizeof(FrLdrIdt) - 1;
    FrLdrIdtPointer.Base = (ULONG_PTR)FrLdrIdt;
    __lidt(&FrLdrIdtPointer);
}

DECLSPEC_NORETURN
VOID
FrLdrAmd64TrapHandler(
    _In_ PFRLDR_TRAP_STATE State)
{
    KTRAP_FRAME TrapFrame;
    KSPECIAL_REGISTERS Special;

    if (FrLdrInTrap)
        goto Halt;
    FrLdrInTrap = TRUE;

    ERR("*** FreeLdr exception 0x%02lx, error code %p\n",
        (ULONG)State->Vector, (PVOID)State->ErrorCode);
    ERR("RIP %p  RSP %p  RFLAGS %p  CR2 %p\n",
        (PVOID)State->Rip, (PVOID)State->Rsp, (PVOID)State->EFlags, (PVOID)__readcr2());
    ERR("RAX %p  RBX %p  RCX %p  RDX %p\n",
        (PVOID)State->Rax, (PVOID)State->Rbx, (PVOID)State->Rcx, (PVOID)State->Rdx);
    ERR("RSI %p  RDI %p  RBP %p  R8  %p\n",
        (PVOID)State->Rsi, (PVOID)State->Rdi, (PVOID)State->Rbp, (PVOID)State->R8);
    ERR("R9  %p  R10 %p  R11 %p  R12 %p\n",
        (PVOID)State->R9, (PVOID)State->R10, (PVOID)State->R11, (PVOID)State->R12);
    ERR("R13 %p  R14 %p  R15 %p  CR3 %p\n",
        (PVOID)State->R13, (PVOID)State->R14, (PVOID)State->R15, (PVOID)__readcr3());

    if (State->Vector <= 18)
    {
        RtlZeroMemory(&TrapFrame, sizeof(TrapFrame));
        RtlZeroMemory(&Special, sizeof(Special));

        TrapFrame.Rax = State->Rax;
        TrapFrame.Rbx = State->Rbx;
        TrapFrame.Rcx = State->Rcx;
        TrapFrame.Rdx = State->Rdx;
        TrapFrame.Rsi = State->Rsi;
        TrapFrame.Rdi = State->Rdi;
        TrapFrame.Rbp = State->Rbp;
        TrapFrame.R8 = State->R8;
        TrapFrame.R9 = State->R9;
        TrapFrame.R10 = State->R10;
        TrapFrame.R11 = State->R11;
        TrapFrame.Rip = State->Rip;
        TrapFrame.Rsp = State->Rsp;
        TrapFrame.EFlags = (ULONG)State->EFlags;
        TrapFrame.ErrorCode = (ULONG)State->ErrorCode;
        TrapFrame.SegCs = (USHORT)State->SegCs;
        TrapFrame.SegSs = (USHORT)State->SegSs;
        TrapFrame.SegDs = TrapFrame.SegEs = TrapFrame.SegFs = TrapFrame.SegGs = LMODE_DS;

        Special.Cr0 = __readcr0();
        Special.Cr2 = __readcr2();
        Special.Cr3 = __readcr3();
        Special.Cr4 = __readcr4();
        __sidt(&Special.Idtr.Limit);

        i386PrintExceptionText((ULONG)State->Vector, &TrapFrame, &Special);
    }

Halt:
    for (;;)
    {
        _disable();
        __halt();
    }
}
