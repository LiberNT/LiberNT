/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     The registers of this processor, by the names a person uses
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#include "../kdb.h"
#include "../kdbpriv.h"
#include <libernt/smpdbg.h>
#define NDEBUG
#include <debug.h>

/* GLOBALS ********************************************************************/

#define KDB_REG(n, f) {n, (USHORT)FIELD_OFFSET(CONTEXT, f), (USHORT)RTL_FIELD_SIZE(CONTEXT, f)}

const KDB_REGISTER KdbpRegisters[] =
{
    KDB_REG("x0", X0),
    KDB_REG("x1", X1),
    KDB_REG("x2", X2),
    KDB_REG("x3", X3),
    KDB_REG("x4", X4),
    KDB_REG("x5", X5),
    KDB_REG("x6", X6),
    KDB_REG("x7", X7),
    KDB_REG("x8", X8),
    KDB_REG("x9", X9),
    KDB_REG("x10", X10),
    KDB_REG("x11", X11),
    KDB_REG("x12", X12),
    KDB_REG("x13", X13),
    KDB_REG("x14", X14),
    KDB_REG("x15", X15),
    KDB_REG("x16", X16),
    KDB_REG("x17", X17),
    KDB_REG("x18", X18),
    KDB_REG("x19", X19),
    KDB_REG("x20", X20),
    KDB_REG("x21", X21),
    KDB_REG("x22", X22),
    KDB_REG("x23", X23),
    KDB_REG("x24", X24),
    KDB_REG("x25", X25),
    KDB_REG("x26", X26),
    KDB_REG("x27", X27),
    KDB_REG("x28", X28),
    KDB_REG("x29", Fp),
    KDB_REG("x30", Lr),
    KDB_REG("lr", Lr),
    KDB_REG("cpsr", Cpsr),
    KDB_REG("fpcr", Fpcr),
    KDB_REG("fpsr", Fpsr),

    /* The names that mean the same thing on every architecture */
    KDB_REG("pc", Pc),
    KDB_REG("sp", Sp),
    KDB_REG("fp", Fp),
    KDB_REG("ret", X0)
};

const ULONG KdbpRegisterCount = RTL_NUMBER_OF(KdbpRegisters);

const KDB_REGISTER KdbpFloatingRegisters[] =
{
    KDB_REG("v0", V[0]),
    KDB_REG("v1", V[1]),
    KDB_REG("v2", V[2]),
    KDB_REG("v3", V[3]),
    KDB_REG("v4", V[4]),
    KDB_REG("v5", V[5]),
    KDB_REG("v6", V[6]),
    KDB_REG("v7", V[7]),
    KDB_REG("v8", V[8]),
    KDB_REG("v9", V[9]),
    KDB_REG("v10", V[10]),
    KDB_REG("v11", V[11]),
    KDB_REG("v12", V[12]),
    KDB_REG("v13", V[13]),
    KDB_REG("v14", V[14]),
    KDB_REG("v15", V[15]),
    KDB_REG("v16", V[16]),
    KDB_REG("v17", V[17]),
    KDB_REG("v18", V[18]),
    KDB_REG("v19", V[19]),
    KDB_REG("v20", V[20]),
    KDB_REG("v21", V[21]),
    KDB_REG("v22", V[22]),
    KDB_REG("v23", V[23]),
    KDB_REG("v24", V[24]),
    KDB_REG("v25", V[25]),
    KDB_REG("v26", V[26]),
    KDB_REG("v27", V[27]),
    KDB_REG("v28", V[28]),
    KDB_REG("v29", V[29]),
    KDB_REG("v30", V[30]),
    KDB_REG("v31", V[31]),
    KDB_REG("fpcr", Fpcr),
    KDB_REG("fpsr", Fpsr)
};

const ULONG KdbpFloatingRegisterCount = RTL_NUMBER_OF(KdbpFloatingRegisters);

/* PUBLIC FUNCTIONS ***********************************************************/

#define KDB_ARM64_PSTATE_D  0x00000200UL
#define KDB_ARM64_PSTATE_SS 0x00200000UL
#define KDB_ARM64_MDSCR_SS  (1ULL << 0)
#define KDB_ARM64_MDSCR_KDE (1ULL << 13)

NTSTATUS
NTAPI
KdbpArchSetSingleStep(
    _Inout_ PCONTEXT Context,
    _In_ BOOLEAN Enable)
{
    ULONG64 Mdscr = ARM64_READ_SYSREG(MDSCR_EL1);

    /* FIXME: MDSCR_EL1 is per processor, so the step is lost when an interrupt moves the thread to another one before it runs */
    if (Enable)
    {
        Context->Cpsr = (Context->Cpsr | KDB_ARM64_PSTATE_SS) & ~KDB_ARM64_PSTATE_D;
        Mdscr |= KDB_ARM64_MDSCR_SS | KDB_ARM64_MDSCR_KDE;
    }
    else
    {
        Context->Cpsr &= ~KDB_ARM64_PSTATE_SS;
        Mdscr &= ~KDB_ARM64_MDSCR_SS;
    }

    ARM64_WRITE_SYSREG(MDSCR_EL1, Mdscr);
    ARM64_ISB();
    return STATUS_SUCCESS;
}

ULONG
NTAPI
KdbpArchHardwareBreakPointCount(VOID)
{
    /* TODO: the watchpoint registers of this architecture are not driven yet */
    return 0;
}

NTSTATUS
NTAPI
KdbpArchSetHardwareBreakPoint(
    _Inout_ PCONTEXT Context,
    _In_ ULONG Index,
    _In_ ULONG_PTR Address,
    _In_ UCHAR Size,
    _In_ KDB_ACCESS_TYPE AccessType,
    _In_ BOOLEAN Enable)
{
    UNREFERENCED_PARAMETER(Context);
    UNREFERENCED_PARAMETER(Index);
    UNREFERENCED_PARAMETER(Address);
    UNREFERENCED_PARAMETER(Size);
    UNREFERENCED_PARAMETER(AccessType);
    UNREFERENCED_PARAMETER(Enable);

    return STATUS_NOT_SUPPORTED;
}

VOID
NTAPI
KdbpArchPrintInterruptTable(
    _In_ ULONG_PTR Address)
{
    UNREFERENCED_PARAMETER(Address);

    KdbPuts("This processor has no interrupt descriptor table\n");
}

ULONG
NTAPI
KdbpArchInterruptVectorLimit(VOID)
{
    return KiArm64QueryInterruptLimit();
}

BOOLEAN
NTAPI
KdbpArchPrintInterruptVector(
    _In_ ULONG Vector,
    _In_ BOOLEAN ShowUnused)
{
    PKINTERRUPT Head = KiArm64QueryInterrupt(Vector);
    BOOLEAN Shown = FALSE;
    ULONG Priority;
    ULONG Enable;
    ULONG Pending;
    ULONG Active;

    if (Head != NULL)
        Shown = KdbpPrintInterruptChain(Vector, Head, TRUE);

    HalArm64DbgGicState(Vector, &Priority, &Enable, &Pending, &Active);

    if ((Priority == MAXULONG) ||
        (!Shown && !ShowUnused && (Enable != 1) && (Pending != 1) && (Active != 1)))
    {
        if (!Shown && ShowUnused)
            KdbpPrint("Interrupt 0x%lx is not connected\n", Vector);

        return Shown;
    }

    if (!Shown)
        KdbpPrint("Interrupt 0x%lx:\n", Vector);

    KdbpPrint("  GIC priority 0x%02lx%s%s%s\n",
              Priority,
              (Enable == 1) ? " enabled" : " disabled",
              (Pending == 1) ? " pending" : "",
              (Active == 1) ? " active" : "");
    return TRUE;
}

BOOLEAN
NTAPI
KdbpArchContextFromKernelStack(
    _In_ PVOID KernelStack,
    _Out_ PCONTEXT Context)
{
    PUCHAR ExceptionFrame = (PUCHAR)KernelStack + sizeof(KSWITCH_FRAME);
    KEXCEPTION_FRAME Saved;

    if (!NT_SUCCESS(KdbpSafeReadMemory(&Saved, ExceptionFrame, sizeof(Saved))))
        return FALSE;

    RtlZeroMemory(Context, sizeof(*Context));
    Context->ContextFlags = CONTEXT_CONTROL | CONTEXT_INTEGER;
    Context->Pc = Saved.Return;
    Context->Lr = Saved.Return;
    Context->Fp = Saved.Fp;
    Context->Sp = (ULONG_PTR)ExceptionFrame + ALIGN_UP_BY(sizeof(KEXCEPTION_FRAME), 16);
    Context->X19 = Saved.X19;
    Context->X20 = Saved.X20;
    Context->X21 = Saved.X21;
    Context->X22 = Saved.X22;
    Context->X23 = Saved.X23;
    Context->X24 = Saved.X24;
    Context->X25 = Saved.X25;
    Context->X26 = Saved.X26;
    Context->X27 = Saved.X27;
    Context->X28 = Saved.X28;
    return TRUE;
}

/*
 * Whether stepping over the instruction at Address means running to the one
 * after it: a branch with link, which is how a call is made here.
 */
BOOLEAN
NTAPI
KdbpArchIsStepOverInstruction(
    _In_ ULONG_PTR Address)
{
    ULONG Instruction;

    if (!NT_SUCCESS(KdbpSafeReadMemory(&Instruction, (PVOID)Address, sizeof(Instruction))))
        return FALSE;

    /* BL, BLR, and BLRAA, BLRAAZ, BLRAB, BLRABZ */
    return ((Instruction & 0xFC000000) == 0x94000000) ||
           ((Instruction & 0xFFFFFC1F) == 0xD63F0000) ||
           ((Instruction & 0xFEFFF800) == 0xD63F0800);
}

/* What the self-test asks the classification above about */
const KDB_STEP_SAMPLE KdbpArchStepSamples[] =
{
    {{0x00, 0x00, 0x00, 0x94}, 4, TRUE, "bl"},
    {{0x00, 0x00, 0x3F, 0xD6}, 4, TRUE, "blr x0"},
    {{0x3F, 0x08, 0x3F, 0xD6}, 4, TRUE, "blraaz x1"},
    {{0x00, 0x00, 0x00, 0x14}, 4, FALSE, "b"},
    {{0x00, 0x00, 0x1F, 0xD6}, 4, FALSE, "br x0"},
    {{0xC0, 0x03, 0x5F, 0xD6}, 4, FALSE, "ret"},
    {{0x1F, 0x20, 0x03, 0xD5}, 4, FALSE, "nop"}
};

const ULONG KdbpArchStepSampleCount = RTL_NUMBER_OF(KdbpArchStepSamples);

/* There are no watch registers driven here, so none can have fired */
LONG
NTAPI
KdbpArchHardwareBreakPointHit(
    _Inout_ PCONTEXT Context)
{
    UNREFERENCED_PARAMETER(Context);
    return -1;
}

/* The answer to a stop leaves the engine's symbol range as it is: there is nothing else here to keep */
VOID
NTAPI
KdbpArchPrepareContinue(
    _Inout_ PDBGKD_MANIPULATE_STATE64 State,
    _In_opt_ PCONTEXT Context)
{
    UNREFERENCED_PARAMETER(Context);

    State->u.Continue2.ControlSet.CurrentSymbolStart = 1;
}

/* The processor steps by itself here: no stop is a step the debugger has to take back */
KDB_STEP_HIT
NTAPI
KdbpArchTranslateStep(
    _In_opt_ PEXCEPTION_RECORD64 ExceptionRecord,
    _Inout_ PCONTEXT Context)
{
    UNREFERENCED_PARAMETER(ExceptionRecord);
    UNREFERENCED_PARAMETER(Context);
    return KdbStepNotHit;
}

/* BRK #imm16 in the A64 encoding */
#define KDB_ARM64_BRK_MASK  0xFFE0001FUL
#define KDB_ARM64_BRK       0xD4200000UL

BOOLEAN
NTAPI
KdbpArchIsBreakInstruction(
    _In_ ULONG_PTR Address)
{
    ULONG Instruction;

    if (!NT_SUCCESS(KdbpSafeReadMemory(&Instruction, (PVOID)Address, sizeof(Instruction))))
        return TRUE;

    return (Instruction & KDB_ARM64_BRK_MASK) == KDB_ARM64_BRK;
}

/* EOF */
