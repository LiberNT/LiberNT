/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     KDBG RISC-V instruction decoder (RV64GC, Zba, Zbb, Zbs, Zicbom, vector memory)
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#include "riscv64-decode.h"

#define RV_RD(i)  (((i) >> 7) & 31)
#define RV_RS1(i) (((i) >> 15) & 31)
#define RV_RS2(i) (((i) >> 20) & 31)
#define RV_RS3(i) (((i) >> 27) & 31)
#define RV_F3(i)  (((i) >> 12) & 7)
#define RV_F7(i)  (((i) >> 25) & 0x7F)

typedef struct _KDB_RISCV_TEXT
{
    PCHAR Buffer;
    ULONG Size;
    ULONG Used;
} KDB_RISCV_TEXT, *PKDB_RISCV_TEXT;

typedef struct _KDB_RISCV_CSR_NAME
{
    USHORT Number;
    PCSTR Name;
} KDB_RISCV_CSR_NAME;

static const CHAR *const KdbpRiscvX[32] =
{
    "zero", "ra", "sp", "gp", "tp", "t0", "t1", "t2",
    "s0", "s1", "a0", "a1", "a2", "a3", "a4", "a5",
    "a6", "a7", "s2", "s3", "s4", "s5", "s6", "s7",
    "s8", "s9", "s10", "s11", "t3", "t4", "t5", "t6"
};

static const CHAR *const KdbpRiscvF[32] =
{
    "ft0", "ft1", "ft2", "ft3", "ft4", "ft5", "ft6", "ft7",
    "fs0", "fs1", "fa0", "fa1", "fa2", "fa3", "fa4", "fa5",
    "fa6", "fa7", "fs2", "fs3", "fs4", "fs5", "fs6", "fs7",
    "fs8", "fs9", "fs10", "fs11", "ft8", "ft9", "ft10", "ft11"
};

static const CHAR *const KdbpRiscvRoundingMode[8] =
{
    "rne", "rtz", "rdn", "rup", "rmm", "0x5", "0x6", "dyn"
};

static const CHAR KdbpRiscvFormatSuffix[4] = { 's', 'd', 'h', 'q' };

static const KDB_RISCV_CSR_NAME KdbpRiscvCsrNames[] =
{
    { 0x001, "fflags" }, { 0x002, "frm" }, { 0x003, "fcsr" },
    { 0x008, "vstart" }, { 0x009, "vxsat" }, { 0x00A, "vxrm" }, { 0x00F, "vcsr" }, { 0x015, "seed" },
    { 0x100, "sstatus" }, { 0x104, "sie" }, { 0x105, "stvec" }, { 0x106, "scounteren" },
    { 0x10A, "senvcfg" }, { 0x10C, "sstateen0" }, { 0x10D, "sstateen1" }, { 0x10E, "sstateen2" },
    { 0x10F, "sstateen3" }, { 0x140, "sscratch" }, { 0x141, "sepc" }, { 0x142, "scause" },
    { 0x143, "stval" }, { 0x144, "sip" }, { 0x14D, "stimecmp" }, { 0x150, "siselect" },
    { 0x151, "sireg" }, { 0x15C, "stopei" }, { 0x180, "satp" }, { 0x5A8, "scontext" },
    { 0x200, "vsstatus" }, { 0x204, "vsie" }, { 0x205, "vstvec" }, { 0x240, "vsscratch" },
    { 0x241, "vsepc" }, { 0x242, "vscause" }, { 0x243, "vstval" }, { 0x244, "vsip" },
    { 0x24D, "vstimecmp" }, { 0x280, "vsatp" },
    { 0x300, "mstatus" }, { 0x301, "misa" }, { 0x302, "medeleg" }, { 0x303, "mideleg" },
    { 0x304, "mie" }, { 0x305, "mtvec" }, { 0x306, "mcounteren" }, { 0x30A, "menvcfg" },
    { 0x310, "mstatush" }, { 0x320, "mcountinhibit" }, { 0x340, "mscratch" }, { 0x341, "mepc" },
    { 0x342, "mcause" }, { 0x343, "mtval" }, { 0x344, "mip" }, { 0x34A, "mtinst" },
    { 0x34B, "mtval2" },
    { 0x600, "hstatus" }, { 0x602, "hedeleg" }, { 0x603, "hideleg" }, { 0x604, "hie" },
    { 0x605, "htimedelta" }, { 0x606, "hcounteren" }, { 0x607, "hgeie" }, { 0x60A, "henvcfg" },
    { 0x643, "htval" }, { 0x644, "hip" }, { 0x645, "hvip" }, { 0x64A, "htinst" },
    { 0x680, "hgatp" },
    { 0x7A0, "tselect" }, { 0x7A1, "tdata1" }, { 0x7A2, "tdata2" }, { 0x7A3, "tdata3" },
    { 0x7A4, "tinfo" }, { 0x7A5, "tcontrol" }, { 0x7A8, "mcontext" }, { 0x7B0, "dcsr" },
    { 0x7B1, "dpc" }, { 0x7B2, "dscratch0" }, { 0x7B3, "dscratch1" },
    { 0xB00, "mcycle" }, { 0xB02, "minstret" },
    { 0xC00, "cycle" }, { 0xC01, "time" }, { 0xC02, "instret" },
    { 0xC20, "vl" }, { 0xC21, "vtype" }, { 0xC22, "vlenb" },
    { 0xDA0, "scountovf" }, { 0xDB0, "stopi" }, { 0xE12, "hgeip" },
    { 0xF11, "mvendorid" }, { 0xF12, "marchid" }, { 0xF13, "mimpid" }, { 0xF14, "mhartid" },
    { 0xF15, "mconfigptr" }
};

static LONG64
KdbpRiscvSext(ULONG64 Value, ULONG Bits)
{
    ULONG Shift = 64 - Bits;

    return ((LONG64)(Value << Shift)) >> Shift;
}

static VOID
KdbpRiscvPut(PKDB_RISCV_TEXT Text, PCSTR String)
{
    while (*String != ANSI_NULL && Text->Used + 1 < Text->Size)
        Text->Buffer[Text->Used++] = *String++;
    Text->Buffer[Text->Used] = ANSI_NULL;
}

static VOID
KdbpRiscvPutUnsigned(PKDB_RISCV_TEXT Text, ULONG64 Value, ULONG Base, BOOLEAN Prefix)
{
    CHAR Digits[24];
    ULONG Count = 0;

    if (Prefix)
        KdbpRiscvPut(Text, "0x");
    do
    {
        Digits[Count++] = "0123456789abcdef"[Value % Base];
        Value /= Base;
    } while (Value != 0);
    while (Count != 0)
    {
        CHAR One[2] = { Digits[--Count], ANSI_NULL };
        KdbpRiscvPut(Text, One);
    }
}

static VOID
KdbpRiscvPutDecimal(PKDB_RISCV_TEXT Text, LONG64 Value)
{
    if (Value < 0)
    {
        KdbpRiscvPut(Text, "-");
        KdbpRiscvPutUnsigned(Text, (ULONG64)0 - (ULONG64)Value, 10, FALSE);
        return;
    }
    KdbpRiscvPutUnsigned(Text, (ULONG64)Value, 10, FALSE);
}

static VOID
KdbpRiscvPutHex(PKDB_RISCV_TEXT Text, ULONG64 Value)
{
    KdbpRiscvPutUnsigned(Text, Value, 16, TRUE);
}

static VOID
KdbpRiscvPutMnemonic(PKDB_RISCV_TEXT Text, PCSTR Mnemonic, PCSTR Suffix)
{
    KdbpRiscvPut(Text, Mnemonic);
    if (Suffix != NULL)
        KdbpRiscvPut(Text, Suffix);
    KdbpRiscvPut(Text, "\t");
}

static VOID
KdbpRiscvPutCsr(PKDB_RISCV_TEXT Text, ULONG Csr)
{
    ULONG Index;
    CHAR Name[24];
    KDB_RISCV_TEXT Local = { Name, sizeof(Name), 0 };

    for (Index = 0; Index < RTL_NUMBER_OF(KdbpRiscvCsrNames); Index++)
    {
        if (KdbpRiscvCsrNames[Index].Number == Csr)
        {
            KdbpRiscvPut(Text, KdbpRiscvCsrNames[Index].Name);
            return;
        }
    }
    if (Csr >= 0xC03 && Csr <= 0xC1F)
    {
        KdbpRiscvPut(&Local, "hpmcounter");
        KdbpRiscvPutUnsigned(&Local, Csr - 0xC00, 10, FALSE);
    }
    else if (Csr >= 0xB03 && Csr <= 0xB1F)
    {
        KdbpRiscvPut(&Local, "mhpmcounter");
        KdbpRiscvPutUnsigned(&Local, Csr - 0xB00, 10, FALSE);
    }
    else if (Csr >= 0x323 && Csr <= 0x33F)
    {
        KdbpRiscvPut(&Local, "mhpmevent");
        KdbpRiscvPutUnsigned(&Local, Csr - 0x320, 10, FALSE);
    }
    else if (Csr >= 0x3A0 && Csr <= 0x3AF)
    {
        KdbpRiscvPut(&Local, "pmpcfg");
        KdbpRiscvPutUnsigned(&Local, Csr - 0x3A0, 10, FALSE);
    }
    else if (Csr >= 0x3B0 && Csr <= 0x3EF)
    {
        KdbpRiscvPut(&Local, "pmpaddr");
        KdbpRiscvPutUnsigned(&Local, Csr - 0x3B0, 10, FALSE);
    }
    else
    {
        KdbpRiscvPutHex(&Local, Csr);
    }
    KdbpRiscvPut(Text, Name);
}

static VOID
KdbpRiscvPutOffsetBase(PKDB_RISCV_TEXT Text, LONG64 Offset, ULONG Base)
{
    KdbpRiscvPutDecimal(Text, Offset);
    KdbpRiscvPut(Text, "(");
    KdbpRiscvPut(Text, KdbpRiscvX[Base]);
    KdbpRiscvPut(Text, ")");
}

static VOID
KdbpRiscvPutFence(PKDB_RISCV_TEXT Text, ULONG Set)
{
    if (Set == 0)
    {
        KdbpRiscvPut(Text, "0");
        return;
    }
    if (Set & 8) KdbpRiscvPut(Text, "i");
    if (Set & 4) KdbpRiscvPut(Text, "o");
    if (Set & 2) KdbpRiscvPut(Text, "r");
    if (Set & 1) KdbpRiscvPut(Text, "w");
}

static ULONG
KdbpRiscvEncodeI(ULONG Opcode, ULONG Rd, ULONG Funct3, ULONG Rs1, LONG Imm)
{
    return Opcode | (Rd << 7) | (Funct3 << 12) | (Rs1 << 15) | (((ULONG)Imm & 0xFFF) << 20);
}

static ULONG
KdbpRiscvEncodeR(ULONG Opcode, ULONG Rd, ULONG Funct3, ULONG Rs1, ULONG Rs2, ULONG Funct7)
{
    return Opcode | (Rd << 7) | (Funct3 << 12) | (Rs1 << 15) | (Rs2 << 20) | (Funct7 << 25);
}

static ULONG
KdbpRiscvEncodeS(ULONG Opcode, ULONG Funct3, ULONG Rs1, ULONG Rs2, LONG Imm)
{
    return Opcode | (((ULONG)Imm & 31) << 7) | (Funct3 << 12) | (Rs1 << 15) | (Rs2 << 20) |
           ((((ULONG)Imm >> 5) & 0x7F) << 25);
}

static ULONG
KdbpRiscvEncodeB(ULONG Funct3, ULONG Rs1, ULONG Rs2, LONG Imm)
{
    ULONG Value = (ULONG)Imm;

    return 0x63 | (((Value >> 11) & 1) << 7) | (((Value >> 1) & 0xF) << 8) | (Funct3 << 12) |
           (Rs1 << 15) | (Rs2 << 20) | (((Value >> 5) & 0x3F) << 25) | (((Value >> 12) & 1) << 31);
}

static ULONG
KdbpRiscvEncodeJ(ULONG Rd, LONG Imm)
{
    ULONG Value = (ULONG)Imm;

    return 0x6F | (Rd << 7) | (((Value >> 12) & 0xFF) << 12) | (((Value >> 11) & 1) << 20) |
           (((Value >> 1) & 0x3FF) << 21) | (((Value >> 20) & 1) << 31);
}

/* Expands a compressed instruction to the base instruction it stands for; 0 when it is reserved */
static ULONG
KdbpRiscvExpand(ULONG C)
{
    ULONG Funct3 = (C >> 13) & 7;
    ULONG Rd = (C >> 7) & 31;
    ULONG Rs2 = (C >> 2) & 31;
    ULONG RdP = 8 + ((C >> 2) & 7);
    ULONG Rs1P = 8 + ((C >> 7) & 7);
    LONG Imm6 = (LONG)KdbpRiscvSext(((C >> 7) & 0x20) | ((C >> 2) & 0x1F), 6);
    ULONG Shamt = ((C >> 7) & 0x20) | ((C >> 2) & 0x1F);
    ULONG Offset;
    LONG Signed;

    switch (C & 3)
    {
        case 0:
            switch (Funct3)
            {
                case 0:
                    Offset = (((C >> 11) & 3) << 4) | (((C >> 7) & 0xF) << 6) | (((C >> 6) & 1) << 2) |
                             (((C >> 5) & 1) << 3);
                    return Offset ? KdbpRiscvEncodeI(0x13, RdP, 0, 2, (LONG)Offset) : 0;
                case 1:
                    Offset = (((C >> 10) & 7) << 3) | (((C >> 5) & 3) << 6);
                    return KdbpRiscvEncodeI(0x07, RdP, 3, Rs1P, (LONG)Offset);
                case 2:
                    Offset = (((C >> 10) & 7) << 3) | (((C >> 6) & 1) << 2) | (((C >> 5) & 1) << 6);
                    return KdbpRiscvEncodeI(0x03, RdP, 2, Rs1P, (LONG)Offset);
                case 3:
                    Offset = (((C >> 10) & 7) << 3) | (((C >> 5) & 3) << 6);
                    return KdbpRiscvEncodeI(0x03, RdP, 3, Rs1P, (LONG)Offset);
                case 5:
                    Offset = (((C >> 10) & 7) << 3) | (((C >> 5) & 3) << 6);
                    return KdbpRiscvEncodeS(0x27, 3, Rs1P, RdP, (LONG)Offset);
                case 6:
                    Offset = (((C >> 10) & 7) << 3) | (((C >> 6) & 1) << 2) | (((C >> 5) & 1) << 6);
                    return KdbpRiscvEncodeS(0x23, 2, Rs1P, RdP, (LONG)Offset);
                case 7:
                    Offset = (((C >> 10) & 7) << 3) | (((C >> 5) & 3) << 6);
                    return KdbpRiscvEncodeS(0x23, 3, Rs1P, RdP, (LONG)Offset);
                default:
                    return 0;
            }

        case 1:
            switch (Funct3)
            {
                case 0:
                    return KdbpRiscvEncodeI(0x13, Rd, 0, Rd, Imm6);
                case 1:
                    return Rd ? KdbpRiscvEncodeI(0x1B, Rd, 0, Rd, Imm6) : 0;
                case 2:
                    return KdbpRiscvEncodeI(0x13, Rd, 0, 0, Imm6);
                case 3:
                    if (Rd == 2)
                    {
                        Signed = (LONG)KdbpRiscvSext((((C >> 12) & 1) << 9) | (((C >> 6) & 1) << 4) |
                                                     (((C >> 5) & 1) << 6) | (((C >> 3) & 3) << 7) |
                                                     (((C >> 2) & 1) << 5), 10);
                        return Signed ? KdbpRiscvEncodeI(0x13, 2, 0, 2, Signed) : 0;
                    }
                    return Imm6 ? (0x37 | (Rd << 7) | (((ULONG)Imm6 & 0xFFFFF) << 12)) : 0;
                case 4:
                    switch ((C >> 10) & 3)
                    {
                        case 0:
                            return KdbpRiscvEncodeI(0x13, Rs1P, 5, Rs1P, (LONG)Shamt);
                        case 1:
                            return KdbpRiscvEncodeI(0x13, Rs1P, 5, Rs1P, (LONG)(0x400 | Shamt));
                        case 2:
                            return KdbpRiscvEncodeI(0x13, Rs1P, 7, Rs1P, Imm6);
                        default:
                            if ((C >> 12) & 1)
                            {
                                if (((C >> 5) & 3) == 0)
                                    return KdbpRiscvEncodeR(0x3B, Rs1P, 0, Rs1P, RdP, 0x20);
                                if (((C >> 5) & 3) == 1)
                                    return KdbpRiscvEncodeR(0x3B, Rs1P, 0, Rs1P, RdP, 0x00);
                                return 0;
                            }
                            switch ((C >> 5) & 3)
                            {
                                case 0: return KdbpRiscvEncodeR(0x33, Rs1P, 0, Rs1P, RdP, 0x20);
                                case 1: return KdbpRiscvEncodeR(0x33, Rs1P, 4, Rs1P, RdP, 0x00);
                                case 2: return KdbpRiscvEncodeR(0x33, Rs1P, 6, Rs1P, RdP, 0x00);
                                default: return KdbpRiscvEncodeR(0x33, Rs1P, 7, Rs1P, RdP, 0x00);
                            }
                    }
                case 5:
                    Signed = (LONG)KdbpRiscvSext((((C >> 12) & 1) << 11) | (((C >> 11) & 1) << 4) |
                                                 (((C >> 9) & 3) << 8) | (((C >> 8) & 1) << 10) |
                                                 (((C >> 7) & 1) << 6) | (((C >> 6) & 1) << 7) |
                                                 (((C >> 3) & 7) << 1) | (((C >> 2) & 1) << 5), 12);
                    return KdbpRiscvEncodeJ(0, Signed);
                default:
                    Signed = (LONG)KdbpRiscvSext((((C >> 12) & 1) << 8) | (((C >> 10) & 3) << 3) |
                                                 (((C >> 5) & 3) << 6) | (((C >> 3) & 3) << 1) |
                                                 (((C >> 2) & 1) << 5), 9);
                    return KdbpRiscvEncodeB(Funct3 == 6 ? 0 : 1, Rs1P, 0, Signed);
            }

        case 2:
            switch (Funct3)
            {
                case 0:
                    return KdbpRiscvEncodeI(0x13, Rd, 1, Rd, (LONG)Shamt);
                case 1:
                    Offset = (((C >> 12) & 1) << 5) | (((C >> 5) & 3) << 3) | (((C >> 2) & 7) << 6);
                    return KdbpRiscvEncodeI(0x07, Rd, 3, 2, (LONG)Offset);
                case 2:
                    Offset = (((C >> 12) & 1) << 5) | (((C >> 4) & 7) << 2) | (((C >> 2) & 3) << 6);
                    return Rd ? KdbpRiscvEncodeI(0x03, Rd, 2, 2, (LONG)Offset) : 0;
                case 3:
                    Offset = (((C >> 12) & 1) << 5) | (((C >> 5) & 3) << 3) | (((C >> 2) & 7) << 6);
                    return Rd ? KdbpRiscvEncodeI(0x03, Rd, 3, 2, (LONG)Offset) : 0;
                case 4:
                    if (((C >> 12) & 1) == 0)
                    {
                        if (Rs2 == 0)
                            return Rd ? KdbpRiscvEncodeI(0x67, 0, 0, Rd, 0) : 0;
                        return KdbpRiscvEncodeI(0x13, Rd, 0, Rs2, 0);
                    }
                    if (Rd == 0 && Rs2 == 0)
                        return 0x00100073;
                    if (Rs2 == 0)
                        return KdbpRiscvEncodeI(0x67, 1, 0, Rd, 0);
                    return KdbpRiscvEncodeR(0x33, Rd, 0, Rd, Rs2, 0);
                case 5:
                    Offset = (((C >> 10) & 7) << 3) | (((C >> 7) & 7) << 6);
                    return KdbpRiscvEncodeS(0x27, 3, 2, Rs2, (LONG)Offset);
                case 6:
                    Offset = (((C >> 9) & 0xF) << 2) | (((C >> 7) & 3) << 6);
                    return KdbpRiscvEncodeS(0x23, 2, 2, Rs2, (LONG)Offset);
                default:
                    Offset = (((C >> 10) & 7) << 3) | (((C >> 7) & 7) << 6);
                    return KdbpRiscvEncodeS(0x23, 3, 2, Rs2, (LONG)Offset);
            }

        default:
            return 0;
    }
}

typedef struct _KDB_RISCV_CONTEXT
{
    KDB_RISCV_TEXT Text;
    PKDB_RISCV_DECODE_STATE State;
    PKDB_RISCV_DECODED Decoded;
    ULONG64 Pc;
    BOOLEAN WritesRd;
    BOOLEAN Compressed;
} KDB_RISCV_CONTEXT, *PKDB_RISCV_CONTEXT;

static VOID
KdbpRiscvRegs(PKDB_RISCV_CONTEXT Ctx, ULONG Count, const CHAR *const *Names, const ULONG *Regs)
{
    ULONG Index;

    for (Index = 0; Index < Count; Index++)
    {
        if (Index != 0)
            KdbpRiscvPut(&Ctx->Text, ",");
        KdbpRiscvPut(&Ctx->Text, Names[Regs[Index]]);
    }
}

static VOID
KdbpRiscv3X(PKDB_RISCV_CONTEXT Ctx, PCSTR Mnemonic, ULONG Rd, ULONG Rs1, ULONG Rs2)
{
    ULONG Regs[3] = { Rd, Rs1, Rs2 };

    KdbpRiscvPutMnemonic(&Ctx->Text, Mnemonic, NULL);
    KdbpRiscvRegs(Ctx, 3, KdbpRiscvX, Regs);
    Ctx->WritesRd = TRUE;
}

static VOID
KdbpRiscv2X(PKDB_RISCV_CONTEXT Ctx, PCSTR Mnemonic, ULONG Rd, ULONG Rs1)
{
    ULONG Regs[2] = { Rd, Rs1 };

    KdbpRiscvPutMnemonic(&Ctx->Text, Mnemonic, NULL);
    KdbpRiscvRegs(Ctx, 2, KdbpRiscvX, Regs);
    Ctx->WritesRd = TRUE;
}

static VOID
KdbpRiscvRegImm(PKDB_RISCV_CONTEXT Ctx, PCSTR Mnemonic, ULONG Rd, ULONG Rs1, LONG64 Imm, BOOLEAN Hex)
{
    KdbpRiscv2X(Ctx, Mnemonic, Rd, Rs1);
    KdbpRiscvPut(&Ctx->Text, ",");
    if (Hex)
        KdbpRiscvPutHex(&Ctx->Text, (ULONG64)Imm);
    else
        KdbpRiscvPutDecimal(&Ctx->Text, Imm);
}

/* An offset from a register a lui or auipc set names an address, which is shown once */
static VOID
KdbpRiscvNoteAddress(PKDB_RISCV_CONTEXT Ctx, ULONG Base, LONG64 Offset, BOOLEAN Wide)
{
    ULONG64 Address;

    if (Base == 0 || !(Ctx->State->Known & (1UL << Base)))
        return;
    Address = Ctx->State->Value[Base] + (ULONG64)Offset;
    if (Wide)
        Address = (ULONG64)(LONG64)(LONG)Address;
    Ctx->State->Known &= ~(1UL << Base);
    Ctx->Decoded->HasAddress = TRUE;
    Ctx->Decoded->Address = Address;
}

static VOID
KdbpRiscvTarget(PKDB_RISCV_CONTEXT Ctx, LONG64 Offset)
{
    ULONG64 Target = Ctx->Pc + (ULONG64)Offset;

    KdbpRiscvPutHex(&Ctx->Text, Target);
    Ctx->Decoded->HasTarget = TRUE;
    Ctx->Decoded->Target = Target;
}

static BOOLEAN
KdbpRiscvOpImm(PKDB_RISCV_CONTEXT Ctx, ULONG I, BOOLEAN Word)
{
    ULONG Rd = RV_RD(I), Rs1 = RV_RS1(I), F3 = RV_F3(I);
    LONG64 Imm = KdbpRiscvSext(I >> 20, 12);
    ULONG Shamt = (I >> 20) & (Word ? 31 : 63);
    ULONG Funct6 = (I >> 26) & 0x3F;
    ULONG Funct12 = (I >> 20) & 0xFFF;

    if (Word)
    {
        switch (F3)
        {
            case 0:
                if (Imm == 0)
                {
                    KdbpRiscv2X(Ctx, "sext.w", Rd, Rs1);
                    return TRUE;
                }
                KdbpRiscvRegImm(Ctx, "addiw", Rd, Rs1, Imm, FALSE);
                KdbpRiscvNoteAddress(Ctx, Rs1, Imm, TRUE);
                return TRUE;
            case 1:
                if (RV_F7(I) == 0)
                {
                    KdbpRiscvRegImm(Ctx, "slliw", Rd, Rs1, Shamt, TRUE);
                    return TRUE;
                }
                if (Funct6 == 0x02)
                {
                    KdbpRiscvRegImm(Ctx, "slli.uw", Rd, Rs1, (I >> 20) & 63, TRUE);
                    return TRUE;
                }
                if (Funct12 == 0x600) { KdbpRiscv2X(Ctx, "clzw", Rd, Rs1); return TRUE; }
                if (Funct12 == 0x601) { KdbpRiscv2X(Ctx, "ctzw", Rd, Rs1); return TRUE; }
                if (Funct12 == 0x602) { KdbpRiscv2X(Ctx, "cpopw", Rd, Rs1); return TRUE; }
                return FALSE;
            case 5:
                if (RV_F7(I) == 0x00) { KdbpRiscvRegImm(Ctx, "srliw", Rd, Rs1, Shamt, TRUE); return TRUE; }
                if (RV_F7(I) == 0x20) { KdbpRiscvRegImm(Ctx, "sraiw", Rd, Rs1, Shamt, TRUE); return TRUE; }
                if (RV_F7(I) == 0x30) { KdbpRiscvRegImm(Ctx, "roriw", Rd, Rs1, Shamt, TRUE); return TRUE; }
                return FALSE;
            default:
                return FALSE;
        }
    }

    switch (F3)
    {
        case 0:
            if (Rd == 0 && Rs1 == 0 && Imm == 0)
            {
                KdbpRiscvPut(&Ctx->Text, "nop");
                return TRUE;
            }
            if (Rs1 == 0)
            {
                KdbpRiscvPutMnemonic(&Ctx->Text, "li", NULL);
                KdbpRiscvPut(&Ctx->Text, KdbpRiscvX[Rd]);
                KdbpRiscvPut(&Ctx->Text, ",");
                KdbpRiscvPutDecimal(&Ctx->Text, Imm);
                Ctx->WritesRd = TRUE;
                return TRUE;
            }
            if (Imm == 0)
            {
                KdbpRiscv2X(Ctx, "mv", Rd, Rs1);
                return TRUE;
            }
            KdbpRiscvRegImm(Ctx, "addi", Rd, Rs1, Imm, FALSE);
            KdbpRiscvNoteAddress(Ctx, Rs1, Imm, FALSE);
            return TRUE;
        case 1:
            if (Funct6 == 0x00) { KdbpRiscvRegImm(Ctx, "slli", Rd, Rs1, Shamt, TRUE); return TRUE; }
            if (Funct6 == 0x0A) { KdbpRiscvRegImm(Ctx, "bseti", Rd, Rs1, Shamt, TRUE); return TRUE; }
            if (Funct6 == 0x12) { KdbpRiscvRegImm(Ctx, "bclri", Rd, Rs1, Shamt, TRUE); return TRUE; }
            if (Funct6 == 0x1A) { KdbpRiscvRegImm(Ctx, "binvi", Rd, Rs1, Shamt, TRUE); return TRUE; }
            if (Funct12 == 0x600) { KdbpRiscv2X(Ctx, "clz", Rd, Rs1); return TRUE; }
            if (Funct12 == 0x601) { KdbpRiscv2X(Ctx, "ctz", Rd, Rs1); return TRUE; }
            if (Funct12 == 0x602) { KdbpRiscv2X(Ctx, "cpop", Rd, Rs1); return TRUE; }
            if (Funct12 == 0x604) { KdbpRiscv2X(Ctx, "sext.b", Rd, Rs1); return TRUE; }
            if (Funct12 == 0x605) { KdbpRiscv2X(Ctx, "sext.h", Rd, Rs1); return TRUE; }
            return FALSE;
        case 2:
            KdbpRiscvRegImm(Ctx, "slti", Rd, Rs1, Imm, FALSE);
            return TRUE;
        case 3:
            if (Imm == 1)
                KdbpRiscv2X(Ctx, "seqz", Rd, Rs1);
            else
                KdbpRiscvRegImm(Ctx, "sltiu", Rd, Rs1, Imm, FALSE);
            return TRUE;
        case 4:
            if (Imm == -1)
                KdbpRiscv2X(Ctx, "not", Rd, Rs1);
            else
                KdbpRiscvRegImm(Ctx, "xori", Rd, Rs1, Imm, FALSE);
            return TRUE;
        case 5:
            if (Funct6 == 0x00) { KdbpRiscvRegImm(Ctx, "srli", Rd, Rs1, Shamt, TRUE); return TRUE; }
            if (Funct6 == 0x10) { KdbpRiscvRegImm(Ctx, "srai", Rd, Rs1, Shamt, TRUE); return TRUE; }
            if (Funct6 == 0x18) { KdbpRiscvRegImm(Ctx, "rori", Rd, Rs1, Shamt, TRUE); return TRUE; }
            if (Funct6 == 0x12) { KdbpRiscvRegImm(Ctx, "bexti", Rd, Rs1, Shamt, TRUE); return TRUE; }
            if (Funct12 == 0x287) { KdbpRiscv2X(Ctx, "orc.b", Rd, Rs1); return TRUE; }
            if (Funct12 == 0x6B8) { KdbpRiscv2X(Ctx, "rev8", Rd, Rs1); return TRUE; }
            return FALSE;
        case 6:
            KdbpRiscvRegImm(Ctx, "ori", Rd, Rs1, Imm, FALSE);
            return TRUE;
        default:
            if (Imm == 255)
                KdbpRiscv2X(Ctx, "zext.b", Rd, Rs1);
            else
                KdbpRiscvRegImm(Ctx, "andi", Rd, Rs1, Imm, FALSE);
            return TRUE;
    }
}

static BOOLEAN
KdbpRiscvOp(PKDB_RISCV_CONTEXT Ctx, ULONG I, BOOLEAN Word)
{
    static const CHAR *const Base[8] = { "add", "sll", "slt", "sltu", "xor", "srl", "or", "and" };
    static const CHAR *const BaseW[8] = { "addw", "sllw", NULL, NULL, NULL, "srlw", NULL, NULL };
    static const CHAR *const MulDiv[8] = { "mul", "mulh", "mulhsu", "mulhu", "div", "divu", "rem", "remu" };
    static const CHAR *const MulDivW[8] = { "mulw", NULL, NULL, NULL, "divw", "divuw", "remw", "remuw" };
    ULONG Rd = RV_RD(I), Rs1 = RV_RS1(I), Rs2 = RV_RS2(I), F3 = RV_F3(I), F7 = RV_F7(I);
    PCSTR Name = NULL;

    switch (F7)
    {
        case 0x00:
            Name = Word ? BaseW[F3] : Base[F3];
            if (!Word && F3 == 3 && Rs1 == 0) { KdbpRiscv2X(Ctx, "snez", Rd, Rs2); return TRUE; }
            if (!Word && F3 == 2 && Rs2 == 0) { KdbpRiscv2X(Ctx, "sltz", Rd, Rs1); return TRUE; }
            if (!Word && F3 == 2 && Rs1 == 0) { KdbpRiscv2X(Ctx, "sgtz", Rd, Rs2); return TRUE; }
            break;
        case 0x01:
            Name = Word ? MulDivW[F3] : MulDiv[F3];
            break;
        case 0x20:
            if (F3 == 0)
            {
                if (Rs1 == 0) { KdbpRiscv2X(Ctx, Word ? "negw" : "neg", Rd, Rs2); return TRUE; }
                Name = Word ? "subw" : "sub";
            }
            else if (F3 == 5) Name = Word ? "sraw" : "sra";
            else if (!Word && F3 == 7) Name = "andn";
            else if (!Word && F3 == 6) Name = "orn";
            else if (!Word && F3 == 4) Name = "xnor";
            break;
        case 0x04:
            if (Word && F3 == 0)
            {
                if (Rs2 == 0) { KdbpRiscv2X(Ctx, "zext.w", Rd, Rs1); return TRUE; }
                Name = "add.uw";
            }
            else if (Word && F3 == 4 && Rs2 == 0) { KdbpRiscv2X(Ctx, "zext.h", Rd, Rs1); return TRUE; }
            break;
        case 0x05:
            if (!Word)
            {
                static const CHAR *const MinMax[8] = { NULL, "clmul", "clmulr", "clmulh", "min", "minu", "max", "maxu" };
                Name = MinMax[F3];
            }
            break;
        case 0x07:
            if (!Word && F3 == 5) Name = "czero.eqz";
            else if (!Word && F3 == 7) Name = "czero.nez";
            break;
        case 0x10:
            if (F3 == 2) Name = Word ? "sh1add.uw" : "sh1add";
            else if (F3 == 4) Name = Word ? "sh2add.uw" : "sh2add";
            else if (F3 == 6) Name = Word ? "sh3add.uw" : "sh3add";
            break;
        case 0x14:
            if (!Word && F3 == 1) Name = "bset";
            break;
        case 0x24:
            if (!Word && F3 == 1) Name = "bclr";
            else if (!Word && F3 == 5) Name = "bext";
            break;
        case 0x30:
            if (F3 == 1) Name = Word ? "rolw" : "rol";
            else if (F3 == 5) Name = Word ? "rorw" : "ror";
            break;
        case 0x34:
            if (!Word && F3 == 1) Name = "binv";
            break;
        default:
            break;
    }
    if (Name == NULL)
        return FALSE;
    KdbpRiscv3X(Ctx, Name, Rd, Rs1, Rs2);
    return TRUE;
}

static BOOLEAN
KdbpRiscvMemory(PKDB_RISCV_CONTEXT Ctx, ULONG I, BOOLEAN Store, BOOLEAN Float)
{
    static const CHAR *const Loads[8] = { "lb", "lh", "lw", "ld", "lbu", "lhu", "lwu", NULL };
    static const CHAR *const Stores[8] = { "sb", "sh", "sw", "sd", NULL, NULL, NULL, NULL };
    static const CHAR *const FloatLoads[8] = { NULL, "flh", "flw", "fld", "flq", NULL, NULL, NULL };
    static const CHAR *const FloatStores[8] = { NULL, "fsh", "fsw", "fsd", "fsq", NULL, NULL, NULL };
    ULONG F3 = RV_F3(I), Base = RV_RS1(I);
    ULONG Reg = Store ? RV_RS2(I) : RV_RD(I);
    LONG64 Offset = Store ? KdbpRiscvSext(((I >> 25) << 5) | ((I >> 7) & 31), 12) : KdbpRiscvSext(I >> 20, 12);
    PCSTR Name = Float ? (Store ? FloatStores[F3] : FloatLoads[F3]) : (Store ? Stores[F3] : Loads[F3]);

    if (Name == NULL)
        return FALSE;
    KdbpRiscvPutMnemonic(&Ctx->Text, Name, NULL);
    KdbpRiscvPut(&Ctx->Text, Float ? KdbpRiscvF[Reg] : KdbpRiscvX[Reg]);
    KdbpRiscvPut(&Ctx->Text, ",");
    KdbpRiscvPutOffsetBase(&Ctx->Text, Offset, Base);
    if (!Ctx->Compressed)
        KdbpRiscvNoteAddress(Ctx, Base, Offset, FALSE);
    Ctx->WritesRd = !Store && !Float;
    return TRUE;
}

static BOOLEAN
KdbpRiscvVectorMemory(PKDB_RISCV_CONTEXT Ctx, ULONG I, BOOLEAN Store)
{
    static const ULONG Width[8] = { 8, 0, 0, 0, 0, 16, 32, 64 };
    ULONG Nf = (I >> 29) & 7, Mew = (I >> 28) & 1, Mop = (I >> 26) & 3, Vm = (I >> 25) & 1;
    ULONG Lumop = RV_RS2(I), Eew = Width[RV_F3(I)], Reg = RV_RD(I);
    CHAR Name[24];
    KDB_RISCV_TEXT Local = { Name, sizeof(Name), 0 };

    if (Eew == 0 || Mew != 0 || Mop != 0)
        return FALSE;
    if (Lumop == 8)
    {
        if (Vm == 0 || (Nf != 0 && Nf != 1 && Nf != 3 && Nf != 7))
            return FALSE;
        KdbpRiscvPut(&Local, Store ? "vs" : "vl");
        KdbpRiscvPutUnsigned(&Local, Nf + 1, 10, FALSE);
        if (Store)
        {
            if (Eew != 8)
                return FALSE;
            KdbpRiscvPut(&Local, "r.v");
        }
        else
        {
            KdbpRiscvPut(&Local, "re");
            KdbpRiscvPutUnsigned(&Local, Eew, 10, FALSE);
            KdbpRiscvPut(&Local, ".v");
        }
    }
    else if (Lumop == 0 && Nf == 0)
    {
        KdbpRiscvPut(&Local, Store ? "vse" : "vle");
        KdbpRiscvPutUnsigned(&Local, Eew, 10, FALSE);
        KdbpRiscvPut(&Local, ".v");
    }
    else
    {
        return FALSE;
    }
    KdbpRiscvPutMnemonic(&Ctx->Text, Name, NULL);
    KdbpRiscvPut(&Ctx->Text, "v");
    KdbpRiscvPutUnsigned(&Ctx->Text, Reg, 10, FALSE);
    KdbpRiscvPut(&Ctx->Text, ",(");
    KdbpRiscvPut(&Ctx->Text, KdbpRiscvX[RV_RS1(I)]);
    KdbpRiscvPut(&Ctx->Text, ")");
    if (Vm == 0)
        KdbpRiscvPut(&Ctx->Text, ",v0.t");
    return TRUE;
}

static VOID
KdbpRiscvVtype(PKDB_RISCV_TEXT Text, ULONG Vtype)
{
    static const CHAR *const Lmul[8] = { "m1", "m2", "m4", "m8", NULL, "mf8", "mf4", "mf2" };
    ULONG Sew = (Vtype >> 3) & 7;

    if ((Vtype >> 8) != 0 || Sew > 3 || Lmul[Vtype & 7] == NULL)
    {
        KdbpRiscvPutHex(Text, Vtype);
        return;
    }
    KdbpRiscvPut(Text, "e");
    KdbpRiscvPutUnsigned(Text, 8UL << Sew, 10, FALSE);
    KdbpRiscvPut(Text, ",");
    KdbpRiscvPut(Text, Lmul[Vtype & 7]);
    KdbpRiscvPut(Text, (Vtype & 0x40) ? ",ta" : ",tu");
    KdbpRiscvPut(Text, (Vtype & 0x80) ? ",ma" : ",mu");
}

static BOOLEAN
KdbpRiscvVectorConfig(PKDB_RISCV_CONTEXT Ctx, ULONG I)
{
    ULONG Rd = RV_RD(I), Rs1 = RV_RS1(I);

    if (RV_F3(I) != 7)
        return FALSE;
    if ((I >> 31) == 0)
    {
        KdbpRiscv2X(Ctx, "vsetvli", Rd, Rs1);
        KdbpRiscvPut(&Ctx->Text, ",");
        KdbpRiscvVtype(&Ctx->Text, (I >> 20) & 0x7FF);
        return TRUE;
    }
    if (((I >> 30) & 3) == 3)
    {
        KdbpRiscvPutMnemonic(&Ctx->Text, "vsetivli", NULL);
        KdbpRiscvPut(&Ctx->Text, KdbpRiscvX[Rd]);
        KdbpRiscvPut(&Ctx->Text, ",");
        KdbpRiscvPutDecimal(&Ctx->Text, Rs1);
        KdbpRiscvPut(&Ctx->Text, ",");
        KdbpRiscvVtype(&Ctx->Text, (I >> 20) & 0x3FF);
        Ctx->WritesRd = TRUE;
        return TRUE;
    }
    if (RV_F7(I) == 0x40)
    {
        KdbpRiscv3X(Ctx, "vsetvl", Rd, Rs1, RV_RS2(I));
        return TRUE;
    }
    return FALSE;
}

static BOOLEAN
KdbpRiscvAtomic(PKDB_RISCV_CONTEXT Ctx, ULONG I)
{
    static const CHAR *const Names[32] =
    {
        "amoadd", "amoswap", "lr", "sc", "amoxor", "amocas", NULL, NULL,
        "amoor", NULL, NULL, NULL, "amoand", NULL, NULL, NULL,
        "amomin", NULL, NULL, NULL, "amomax", NULL, NULL, NULL,
        "amominu", NULL, NULL, NULL, "amomaxu", NULL, NULL, NULL
    };
    ULONG F3 = RV_F3(I), Funct5 = I >> 27, Rd = RV_RD(I), Rs1 = RV_RS1(I), Rs2 = RV_RS2(I);
    PCSTR Name = Names[Funct5];
    CHAR Mnemonic[24];
    KDB_RISCV_TEXT Local = { Mnemonic, sizeof(Mnemonic), 0 };

    if (Name == NULL || (F3 != 2 && F3 != 3) || (Funct5 == 2 && Rs2 != 0))
        return FALSE;
    KdbpRiscvPut(&Local, Name);
    KdbpRiscvPut(&Local, F3 == 2 ? ".w" : ".d");
    if ((I >> 26) & 1) KdbpRiscvPut(&Local, ".aq");
    if ((I >> 25) & 1) KdbpRiscvPut(&Local, ((I >> 26) & 1) ? "rl" : ".rl");
    KdbpRiscvPutMnemonic(&Ctx->Text, Mnemonic, NULL);
    KdbpRiscvPut(&Ctx->Text, KdbpRiscvX[Rd]);
    if (Funct5 != 2)
    {
        KdbpRiscvPut(&Ctx->Text, ",");
        KdbpRiscvPut(&Ctx->Text, KdbpRiscvX[Rs2]);
    }
    KdbpRiscvPut(&Ctx->Text, ",(");
    KdbpRiscvPut(&Ctx->Text, KdbpRiscvX[Rs1]);
    KdbpRiscvPut(&Ctx->Text, ")");
    Ctx->WritesRd = TRUE;
    return TRUE;
}

static VOID
KdbpRiscvFloatRegs(PKDB_RISCV_CONTEXT Ctx, PCSTR Mnemonic, ULONG Fmt, ULONG Count,
                   const CHAR *const *Name0, ULONG R0, const CHAR *const *Name1, ULONG R1,
                   ULONG R2, ULONG R3, LONG Rm)
{
    CHAR Suffix[3] = { '.', KdbpRiscvFormatSuffix[Fmt], ANSI_NULL };

    KdbpRiscvPutMnemonic(&Ctx->Text, Mnemonic, Suffix);
    KdbpRiscvPut(&Ctx->Text, Name0[R0]);
    if (Count > 1) { KdbpRiscvPut(&Ctx->Text, ","); KdbpRiscvPut(&Ctx->Text, Name1[R1]); }
    if (Count > 2) { KdbpRiscvPut(&Ctx->Text, ","); KdbpRiscvPut(&Ctx->Text, KdbpRiscvF[R2]); }
    if (Count > 3) { KdbpRiscvPut(&Ctx->Text, ","); KdbpRiscvPut(&Ctx->Text, KdbpRiscvF[R3]); }
    if (Rm >= 0 && Rm != 7)
    {
        KdbpRiscvPut(&Ctx->Text, ",");
        KdbpRiscvPut(&Ctx->Text, KdbpRiscvRoundingMode[Rm]);
    }
    Ctx->WritesRd = (Name0 == KdbpRiscvX);
}

static BOOLEAN
KdbpRiscvFloat(PKDB_RISCV_CONTEXT Ctx, ULONG I)
{
    static const CHAR *const IntFormats[4] = { "w", "wu", "l", "lu" };
    ULONG Rd = RV_RD(I), Rs1 = RV_RS1(I), Rs2 = RV_RS2(I), F3 = RV_F3(I);
    ULONG Fmt = (I >> 25) & 3, Funct5 = I >> 27;
    CHAR Mnemonic[24];
    KDB_RISCV_TEXT Local = { Mnemonic, sizeof(Mnemonic), 0 };

    if (Fmt == 3)
        return FALSE;
    switch (Funct5)
    {
        case 0x00: KdbpRiscvFloatRegs(Ctx, "fadd", Fmt, 3, KdbpRiscvF, Rd, KdbpRiscvF, Rs1, Rs2, 0, F3); return TRUE;
        case 0x01: KdbpRiscvFloatRegs(Ctx, "fsub", Fmt, 3, KdbpRiscvF, Rd, KdbpRiscvF, Rs1, Rs2, 0, F3); return TRUE;
        case 0x02: KdbpRiscvFloatRegs(Ctx, "fmul", Fmt, 3, KdbpRiscvF, Rd, KdbpRiscvF, Rs1, Rs2, 0, F3); return TRUE;
        case 0x03: KdbpRiscvFloatRegs(Ctx, "fdiv", Fmt, 3, KdbpRiscvF, Rd, KdbpRiscvF, Rs1, Rs2, 0, F3); return TRUE;
        case 0x0B:
            if (Rs2 != 0) return FALSE;
            KdbpRiscvFloatRegs(Ctx, "fsqrt", Fmt, 2, KdbpRiscvF, Rd, KdbpRiscvF, Rs1, 0, 0, F3);
            return TRUE;
        case 0x04:
            if (Rs1 == Rs2)
            {
                static const CHAR *const Moves[3] = { "fmv", "fneg", "fabs" };
                if (F3 > 2) return FALSE;
                KdbpRiscvFloatRegs(Ctx, Moves[F3], Fmt, 2, KdbpRiscvF, Rd, KdbpRiscvF, Rs1, 0, 0, -1);
                return TRUE;
            }
            if (F3 > 2) return FALSE;
            KdbpRiscvFloatRegs(Ctx, F3 == 0 ? "fsgnj" : (F3 == 1 ? "fsgnjn" : "fsgnjx"), Fmt, 3,
                               KdbpRiscvF, Rd, KdbpRiscvF, Rs1, Rs2, 0, -1);
            return TRUE;
        case 0x05:
            if (F3 > 1) return FALSE;
            KdbpRiscvFloatRegs(Ctx, F3 ? "fmax" : "fmin", Fmt, 3, KdbpRiscvF, Rd, KdbpRiscvF, Rs1, Rs2, 0, -1);
            return TRUE;
        case 0x08:
            if (Rs2 > 2 || Rs2 == Fmt) return FALSE;
            KdbpRiscvPut(&Local, "fcvt.");
            KdbpRiscvPut(&Local, Fmt == 0 ? "s" : (Fmt == 1 ? "d" : "h"));
            KdbpRiscvPut(&Local, ".");
            KdbpRiscvPut(&Local, Rs2 == 0 ? "s" : (Rs2 == 1 ? "d" : "h"));
            KdbpRiscvPutMnemonic(&Ctx->Text, Mnemonic, NULL);
            KdbpRiscvPut(&Ctx->Text, KdbpRiscvF[Rd]);
            KdbpRiscvPut(&Ctx->Text, ",");
            KdbpRiscvPut(&Ctx->Text, KdbpRiscvF[Rs1]);
            if (!((Fmt == 1 && Rs2 != 1) || (Fmt == 0 && Rs2 == 2)) && F3 != 7)
            {
                KdbpRiscvPut(&Ctx->Text, ",");
                KdbpRiscvPut(&Ctx->Text, KdbpRiscvRoundingMode[F3]);
            }
            return TRUE;
        case 0x14:
            if (F3 > 2) return FALSE;
            KdbpRiscvFloatRegs(Ctx, F3 == 2 ? "feq" : (F3 == 1 ? "flt" : "fle"), Fmt, 3,
                               KdbpRiscvX, Rd, KdbpRiscvF, Rs1, Rs2, 0, -1);
            return TRUE;
        case 0x18:
            if (Rs2 > 3) return FALSE;
            KdbpRiscvPut(&Local, "fcvt.");
            KdbpRiscvPut(&Local, IntFormats[Rs2]);
            KdbpRiscvPut(&Local, Fmt == 0 ? ".s" : (Fmt == 1 ? ".d" : ".h"));
            KdbpRiscvPutMnemonic(&Ctx->Text, Mnemonic, NULL);
            KdbpRiscvPut(&Ctx->Text, KdbpRiscvX[Rd]);
            KdbpRiscvPut(&Ctx->Text, ",");
            KdbpRiscvPut(&Ctx->Text, KdbpRiscvF[Rs1]);
            if (F3 != 7)
            {
                KdbpRiscvPut(&Ctx->Text, ",");
                KdbpRiscvPut(&Ctx->Text, KdbpRiscvRoundingMode[F3]);
            }
            Ctx->WritesRd = TRUE;
            return TRUE;
        case 0x1A:
            if (Rs2 > 3) return FALSE;
            KdbpRiscvPut(&Local, Fmt == 0 ? "fcvt.s." : (Fmt == 1 ? "fcvt.d." : "fcvt.h."));
            KdbpRiscvPut(&Local, IntFormats[Rs2]);
            KdbpRiscvPutMnemonic(&Ctx->Text, Mnemonic, NULL);
            KdbpRiscvPut(&Ctx->Text, KdbpRiscvF[Rd]);
            KdbpRiscvPut(&Ctx->Text, ",");
            KdbpRiscvPut(&Ctx->Text, KdbpRiscvX[Rs1]);
            if (F3 != 7 && !(Fmt == 1 && Rs2 < 2))
            {
                KdbpRiscvPut(&Ctx->Text, ",");
                KdbpRiscvPut(&Ctx->Text, KdbpRiscvRoundingMode[F3]);
            }
            return TRUE;
        case 0x1C:
            if (Rs2 != 0) return FALSE;
            if (F3 == 0)
            {
                KdbpRiscvPut(&Local, Fmt == 0 ? "fmv.x.w" : (Fmt == 1 ? "fmv.x.d" : "fmv.x.h"));
                KdbpRiscvPutMnemonic(&Ctx->Text, Mnemonic, NULL);
                KdbpRiscvPut(&Ctx->Text, KdbpRiscvX[Rd]);
                KdbpRiscvPut(&Ctx->Text, ",");
                KdbpRiscvPut(&Ctx->Text, KdbpRiscvF[Rs1]);
                Ctx->WritesRd = TRUE;
                return TRUE;
            }
            if (F3 == 1)
            {
                KdbpRiscvFloatRegs(Ctx, "fclass", Fmt, 2, KdbpRiscvX, Rd, KdbpRiscvF, Rs1, 0, 0, -1);
                return TRUE;
            }
            return FALSE;
        case 0x1E:
            if (Rs2 != 0 || F3 != 0) return FALSE;
            KdbpRiscvPut(&Local, Fmt == 0 ? "fmv.w.x" : (Fmt == 1 ? "fmv.d.x" : "fmv.h.x"));
            KdbpRiscvPutMnemonic(&Ctx->Text, Mnemonic, NULL);
            KdbpRiscvPut(&Ctx->Text, KdbpRiscvF[Rd]);
            KdbpRiscvPut(&Ctx->Text, ",");
            KdbpRiscvPut(&Ctx->Text, KdbpRiscvX[Rs1]);
            return TRUE;
        default:
            return FALSE;
    }
}

static BOOLEAN
KdbpRiscvFusedMultiply(PKDB_RISCV_CONTEXT Ctx, ULONG I)
{
    static const CHAR *const Names[4] = { "fmadd", "fmsub", "fnmsub", "fnmadd" };
    ULONG Fmt = (I >> 25) & 3;

    if (Fmt == 3)
        return FALSE;
    KdbpRiscvFloatRegs(Ctx, Names[((I & 0x7F) - 0x43) >> 2], Fmt, 4, KdbpRiscvF, RV_RD(I), KdbpRiscvF,
                       RV_RS1(I), RV_RS2(I), RV_RS3(I), RV_F3(I));
    return TRUE;
}

static BOOLEAN
KdbpRiscvSystem(PKDB_RISCV_CONTEXT Ctx, ULONG I)
{
    ULONG Rd = RV_RD(I), Rs1 = RV_RS1(I), F3 = RV_F3(I), Csr = I >> 20;
    PCSTR Name;

    if (F3 == 0)
    {
        switch (I)
        {
            case 0x00000073: KdbpRiscvPut(&Ctx->Text, "ecall"); return TRUE;
            case 0x00100073: KdbpRiscvPut(&Ctx->Text, "ebreak"); return TRUE;
            case 0x10200073: KdbpRiscvPut(&Ctx->Text, "sret"); return TRUE;
            case 0x30200073: KdbpRiscvPut(&Ctx->Text, "mret"); return TRUE;
            case 0x7B200073: KdbpRiscvPut(&Ctx->Text, "dret"); return TRUE;
            case 0x10500073: KdbpRiscvPut(&Ctx->Text, "wfi"); return TRUE;
            default: break;
        }
        if (Rd != 0)
            return FALSE;
        switch (RV_F7(I))
        {
            case 0x09: Name = "sfence.vma"; break;
            case 0x0B: Name = "sinval.vma"; break;
            case 0x11: Name = "hfence.vvma"; break;
            case 0x31: Name = "hfence.gvma"; break;
            default: return FALSE;
        }
        KdbpRiscvPut(&Ctx->Text, Name);
        if (Rs1 != 0 || RV_RS2(I) != 0)
        {
            KdbpRiscvPut(&Ctx->Text, "\t");
            KdbpRiscvPut(&Ctx->Text, KdbpRiscvX[Rs1]);
            if (RV_RS2(I) != 0)
            {
                KdbpRiscvPut(&Ctx->Text, ",");
                KdbpRiscvPut(&Ctx->Text, KdbpRiscvX[RV_RS2(I)]);
            }
        }
        return TRUE;
    }
    if (F3 == 4)
        return FALSE;

    Ctx->WritesRd = TRUE;
    if (F3 == 2 && Rs1 == 0)
    {
        if (Csr == 0xC00 || Csr == 0xC01 || Csr == 0xC02 || Csr == 0x001 || Csr == 0x002 || Csr == 0x003)
        {
            static const CHAR *const Counters[3] = { "rdcycle", "rdtime", "rdinstret" };
            static const CHAR *const Float[4] = { NULL, "frflags", "frrm", "frcsr" };
            KdbpRiscvPutMnemonic(&Ctx->Text, Csr >= 0xC00 ? Counters[Csr - 0xC00] : Float[Csr], NULL);
            KdbpRiscvPut(&Ctx->Text, KdbpRiscvX[Rd]);
            return TRUE;
        }
        KdbpRiscvPutMnemonic(&Ctx->Text, "csrr", NULL);
        KdbpRiscvPut(&Ctx->Text, KdbpRiscvX[Rd]);
        KdbpRiscvPut(&Ctx->Text, ",");
        KdbpRiscvPutCsr(&Ctx->Text, Csr);
        return TRUE;
    }
    if ((F3 == 1 || F3 == 5) && Csr >= 1 && Csr <= 3)
    {
        static const CHAR *const Writes[4] = { NULL, "fsflags", "fsrm", "fscsr" };
        static const CHAR *const WritesImm[4] = { NULL, "fsflagsi", "fsrmi", NULL };
        PCSTR Mnemonic = (F3 == 1) ? Writes[Csr] : WritesImm[Csr];

        if (Mnemonic != NULL)
        {
            KdbpRiscvPutMnemonic(&Ctx->Text, Mnemonic, NULL);
            if (Rd != 0)
            {
                KdbpRiscvPut(&Ctx->Text, KdbpRiscvX[Rd]);
                KdbpRiscvPut(&Ctx->Text, ",");
            }
            if (F3 == 1)
                KdbpRiscvPut(&Ctx->Text, KdbpRiscvX[Rs1]);
            else
                KdbpRiscvPutDecimal(&Ctx->Text, Rs1);
            return TRUE;
        }
    }
    if (Rd == 0)
    {
        static const CHAR *const Short[8] = { NULL, "csrw", "csrs", "csrc", NULL, "csrwi", "csrsi", "csrci" };
        KdbpRiscvPutMnemonic(&Ctx->Text, Short[F3], NULL);
        KdbpRiscvPutCsr(&Ctx->Text, Csr);
        KdbpRiscvPut(&Ctx->Text, ",");
        if (F3 < 4)
            KdbpRiscvPut(&Ctx->Text, KdbpRiscvX[Rs1]);
        else
            KdbpRiscvPutDecimal(&Ctx->Text, Rs1);
        return TRUE;
    }
    {
        static const CHAR *const Full[8] = { NULL, "csrrw", "csrrs", "csrrc", NULL, "csrrwi", "csrrsi", "csrrci" };
        KdbpRiscvPutMnemonic(&Ctx->Text, Full[F3], NULL);
        KdbpRiscvPut(&Ctx->Text, KdbpRiscvX[Rd]);
        KdbpRiscvPut(&Ctx->Text, ",");
        KdbpRiscvPutCsr(&Ctx->Text, Csr);
        KdbpRiscvPut(&Ctx->Text, ",");
        if (F3 < 4)
            KdbpRiscvPut(&Ctx->Text, KdbpRiscvX[Rs1]);
        else
            KdbpRiscvPutDecimal(&Ctx->Text, Rs1);
    }
    return TRUE;
}

static BOOLEAN
KdbpRiscvFormat(PKDB_RISCV_CONTEXT Ctx, ULONG I)
{
    ULONG Rd = RV_RD(I), Rs1 = RV_RS1(I), Rs2 = RV_RS2(I), F3 = RV_F3(I);
    LONG64 Imm;

    switch (I & 0x7F)
    {
        case 0x37:
        case 0x17:
            KdbpRiscvPutMnemonic(&Ctx->Text, (I & 0x7F) == 0x37 ? "lui" : "auipc", NULL);
            KdbpRiscvPut(&Ctx->Text, KdbpRiscvX[Rd]);
            KdbpRiscvPut(&Ctx->Text, ",");
            KdbpRiscvPutHex(&Ctx->Text, I >> 12);
            if (Rd != 0)
            {
                Imm = (LONG64)(LONG)(I & 0xFFFFF000);
                Ctx->State->Value[Rd] = ((I & 0x7F) == 0x37) ? (ULONG64)Imm : Ctx->Pc + (ULONG64)Imm;
                Ctx->State->Known |= 1UL << Rd;
            }
            return TRUE;

        case 0x6F:
            Imm = KdbpRiscvSext((((I >> 31) & 1) << 20) | (((I >> 21) & 0x3FF) << 1) |
                                (((I >> 20) & 1) << 11) | (((I >> 12) & 0xFF) << 12), 21);
            if (Rd == 0)
                KdbpRiscvPutMnemonic(&Ctx->Text, "j", NULL);
            else if (Rd == 1)
                KdbpRiscvPutMnemonic(&Ctx->Text, "jal", NULL);
            else
            {
                KdbpRiscvPutMnemonic(&Ctx->Text, "jal", NULL);
                KdbpRiscvPut(&Ctx->Text, KdbpRiscvX[Rd]);
                KdbpRiscvPut(&Ctx->Text, ",");
            }
            KdbpRiscvTarget(Ctx, Imm);
            Ctx->WritesRd = TRUE;
            return TRUE;

        case 0x67:
            if (F3 != 0)
                return FALSE;
            Imm = KdbpRiscvSext(I >> 20, 12);
            if (Rd == 0 && Rs1 == 1 && Imm == 0)
            {
                KdbpRiscvPut(&Ctx->Text, "ret");
                return TRUE;
            }
            if (Rd == 0 || Rd == 1)
            {
                KdbpRiscvPutMnemonic(&Ctx->Text, Rd == 0 ? "jr" : "jalr", NULL);
                if (Imm == 0)
                    KdbpRiscvPut(&Ctx->Text, KdbpRiscvX[Rs1]);
                else
                    KdbpRiscvPutOffsetBase(&Ctx->Text, Imm, Rs1);
                if (!Ctx->Compressed)
                    KdbpRiscvNoteAddress(Ctx, Rs1, Imm, FALSE);
            }
            else
            {
                KdbpRiscvPutMnemonic(&Ctx->Text, "jalr", NULL);
                KdbpRiscvPut(&Ctx->Text, KdbpRiscvX[Rd]);
                KdbpRiscvPut(&Ctx->Text, ",");
                KdbpRiscvPutOffsetBase(&Ctx->Text, Imm, Rs1);
                KdbpRiscvNoteAddress(Ctx, Rs1, Imm, FALSE);
            }
            Ctx->WritesRd = TRUE;
            return TRUE;

        case 0x63:
        {
            static const CHAR *const Names[8] = { "beq", "bne", NULL, NULL, "blt", "bge", "bltu", "bgeu" };
            PCSTR Name = Names[F3];

            if (Name == NULL)
                return FALSE;
            Imm = KdbpRiscvSext((((I >> 31) & 1) << 12) | (((I >> 7) & 1) << 11) |
                                (((I >> 25) & 0x3F) << 5) | (((I >> 8) & 0xF) << 1), 13);
            if (Rs2 == 0 && (F3 == 0 || F3 == 1 || F3 == 4 || F3 == 5))
            {
                static const CHAR *const Zero[8] = { "beqz", "bnez", NULL, NULL, "bltz", "bgez", NULL, NULL };
                KdbpRiscvPutMnemonic(&Ctx->Text, Zero[F3], NULL);
                KdbpRiscvPut(&Ctx->Text, KdbpRiscvX[Rs1]);
            }
            else if (Rs1 == 0 && (F3 == 4 || F3 == 5))
            {
                KdbpRiscvPutMnemonic(&Ctx->Text, F3 == 4 ? "bgtz" : "blez", NULL);
                KdbpRiscvPut(&Ctx->Text, KdbpRiscvX[Rs2]);
            }
            else
            {
                KdbpRiscvPutMnemonic(&Ctx->Text, Name, NULL);
                KdbpRiscvPut(&Ctx->Text, KdbpRiscvX[Rs1]);
                KdbpRiscvPut(&Ctx->Text, ",");
                KdbpRiscvPut(&Ctx->Text, KdbpRiscvX[Rs2]);
            }
            KdbpRiscvPut(&Ctx->Text, ",");
            KdbpRiscvTarget(Ctx, Imm);
            return TRUE;
        }

        case 0x03: return KdbpRiscvMemory(Ctx, I, FALSE, FALSE);
        case 0x23: return KdbpRiscvMemory(Ctx, I, TRUE, FALSE);
        case 0x07: return (F3 == 0 || F3 >= 5) ? KdbpRiscvVectorMemory(Ctx, I, FALSE) : KdbpRiscvMemory(Ctx, I, FALSE, TRUE);
        case 0x27: return (F3 == 0 || F3 >= 5) ? KdbpRiscvVectorMemory(Ctx, I, TRUE) : KdbpRiscvMemory(Ctx, I, TRUE, TRUE);
        case 0x13: return KdbpRiscvOpImm(Ctx, I, FALSE);
        case 0x1B: return KdbpRiscvOpImm(Ctx, I, TRUE);
        case 0x33: return KdbpRiscvOp(Ctx, I, FALSE);
        case 0x3B: return KdbpRiscvOp(Ctx, I, TRUE);
        case 0x2F: return KdbpRiscvAtomic(Ctx, I);
        case 0x53: return KdbpRiscvFloat(Ctx, I);
        case 0x43:
        case 0x47:
        case 0x4B:
        case 0x4F: return KdbpRiscvFusedMultiply(Ctx, I);
        case 0x57: return KdbpRiscvVectorConfig(Ctx, I);
        case 0x73: return KdbpRiscvSystem(Ctx, I);

        case 0x0F:
            if (F3 == 1 && I == 0x0000100F)
            {
                KdbpRiscvPut(&Ctx->Text, "fence.i");
                return TRUE;
            }
            if (F3 == 0)
            {
                ULONG Pred = (I >> 24) & 0xF, Succ = (I >> 20) & 0xF, Fm = I >> 28;

                if (Fm == 8 && Pred == 3 && Succ == 3)
                {
                    KdbpRiscvPut(&Ctx->Text, "fence.tso");
                    return TRUE;
                }
                if (Fm != 0)
                    return FALSE;
                if (Pred == 0xF && Succ == 0xF)
                {
                    KdbpRiscvPut(&Ctx->Text, "fence");
                    return TRUE;
                }
                KdbpRiscvPutMnemonic(&Ctx->Text, "fence", NULL);
                KdbpRiscvPutFence(&Ctx->Text, Pred);
                KdbpRiscvPut(&Ctx->Text, ",");
                KdbpRiscvPutFence(&Ctx->Text, Succ);
                return TRUE;
            }
            if (F3 == 2 && Rd == 0)
            {
                static const CHAR *const Cbo[5] = { "cbo.inval", "cbo.clean", "cbo.flush", NULL, "cbo.zero" };
                ULONG Op = I >> 20;

                if (Op > 4 || Cbo[Op] == NULL)
                    return FALSE;
                KdbpRiscvPutMnemonic(&Ctx->Text, Cbo[Op], NULL);
                KdbpRiscvPutOffsetBase(&Ctx->Text, 0, Rs1);
                return TRUE;
            }
            return FALSE;

        default:
            return FALSE;
    }
}

VOID
KdbpRiscvDecodeReset(
    _Out_ PKDB_RISCV_DECODE_STATE State)
{
    RtlZeroMemory(State, sizeof(*State));
}

VOID
KdbpRiscvDecode(
    _In_ ULONG Instruction,
    _In_ ULONG Length,
    _In_ ULONG64 Pc,
    _Inout_ PKDB_RISCV_DECODE_STATE State,
    _In_ BOOLEAN KeepStaleValues,
    _Out_ PKDB_RISCV_DECODED Decoded)
{
    KDB_RISCV_CONTEXT Ctx;
    ULONG Word = Instruction;
    ULONG Rd;

    RtlZeroMemory(Decoded, sizeof(*Decoded));
    Ctx.Text.Buffer = Decoded->Text;
    Ctx.Text.Size = sizeof(Decoded->Text);
    Ctx.Text.Used = 0;
    Ctx.State = State;
    Ctx.Decoded = Decoded;
    Ctx.Pc = Pc;
    Ctx.WritesRd = FALSE;
    Ctx.Compressed = (Length == 2);

    if (Length == 2)
    {
        if ((Instruction & 0xFFFF) == 0)
        {
            KdbpRiscvPut(&Ctx.Text, "unimp");
            return;
        }
        Word = KdbpRiscvExpand(Instruction & 0xFFFF);
    }

    if (Word == 0 || !KdbpRiscvFormat(&Ctx, Word))
    {
        Ctx.Text.Used = 0;
        Decoded->HasTarget = FALSE;
        Decoded->HasAddress = FALSE;
        KdbpRiscvPut(&Ctx.Text, ".insn\t");
        KdbpRiscvPutUnsigned(&Ctx.Text, Length, 10, FALSE);
        KdbpRiscvPut(&Ctx.Text, ", ");
        KdbpRiscvPutHex(&Ctx.Text, Length == 2 ? (Instruction & 0xFFFF) : Instruction);
        return;
    }

    /* A register written by anything but lui or auipc no longer holds what they put there */
    Rd = RV_RD(Word);
    if (!KeepStaleValues && Ctx.WritesRd && Rd != 0)
        State->Known &= ~(1UL << Rd);
}
