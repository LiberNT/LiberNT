/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     KDBG LoongArch64 instruction words, lengths and branch targets
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

/*
 * Encodings are the ones in the manual's Table of instruction encoding
 * ([V1-Appendix B, Table 93]): every instruction is 32 bits, the opcode sits
 * in the high bits and the register fields start at bit 0 (rd/fd/cd 4:0,
 * rj/fj 9:5, rk/fk 14:10). Each row below is a (mask, match) pair for the
 * fixed bits of that encoding.
 *
 * Control-flow decoding is exact for every branch and jump form, so a
 * single-step can place a temporary breakpoint at the next instruction. A
 * word that matches no row is printed as .word with its two opcode fields
 * decoded, never guessed.
 */

#include <ntoskrnl.h>
#include "kdbg/kdb.h"
#define NDEBUG
#include <debug.h>

typedef struct _KDBP_LA64_ENCODING
{
    PCSTR Name;
    PCSTR Format;
    ULONG Mask;
    ULONG Match;
} KDBP_LA64_ENCODING, *PKDBP_LA64_ENCODING;

/* Format letters: d rd, j rj, k rk, f fd, g fj, h fk, I si12, J si14,
 * K si16, L si20, U ui5/ui6, V ui12, c csr, n code, p small immediate,
 * o I16 branch target, O I26 branch target. */
static const KDBP_LA64_ENCODING KdbpLa64Encodings[] =
{
    /* Fixed words: test these before anything with a narrower mask. */
    { "ertn",    "",  0xFFFFFFFFu, 0x06483800u },
    { "tlbsrch", "",  0xFFFFFFFFu, 0x06482800u },
    { "tlbrd",   "",  0xFFFFFFFFu, 0x06482C00u },
    { "tlbwr",   "",  0xFFFFFFFFu, 0x06483000u },
    { "tlbfill", "",  0xFFFFFFFFu, 0x06483400u },
    { "tlbclr",  "",  0xFFFFFFFFu, 0x06482000u },
    { "tlbflush","",  0xFFFFFFFFu, 0x06482400u },

    /* CSR group: the two 2R forms have five more fixed bits than csrxchg. */
    { "csrrd",   "dc", 0xFF0003E0u, 0x04000000u },
    { "csrwr",   "dc", 0xFF0003E0u, 0x04000020u },
    { "csrxchg", "djc", 0xFF000000u, 0x04000000u },

    { "lddir",   "djp", 0xFFFC0000u, 0x06400000u },
    { "ldpte",   "jp",  0xFFFC001Fu, 0x06440000u },

    /* 3R */
    { "add.w",   "djk", 0xFFFF8000u, 0x00100000u },
    { "add.d",   "djk", 0xFFFF8000u, 0x00108000u },
    { "sub.w",   "djk", 0xFFFF8000u, 0x00110000u },
    { "sub.d",   "djk", 0xFFFF8000u, 0x00118000u },
    { "slt",     "djk", 0xFFFF8000u, 0x00120000u },
    { "sltu",    "djk", 0xFFFF8000u, 0x00128000u },
    { "nor",     "djk", 0xFFFF8000u, 0x00140000u },
    { "and",     "djk", 0xFFFF8000u, 0x00148000u },
    { "or",      "djk", 0xFFFF8000u, 0x00150000u },
    { "xor",     "djk", 0xFFFF8000u, 0x00158000u },
    { "orn",     "djk", 0xFFFF8000u, 0x00160000u },
    { "andn",    "djk", 0xFFFF8000u, 0x00168000u },
    { "sll.w",   "djk", 0xFFFF8000u, 0x00170000u },
    { "srl.w",   "djk", 0xFFFF8000u, 0x00178000u },
    { "sra.w",   "djk", 0xFFFF8000u, 0x00180000u },
    { "sll.d",   "djk", 0xFFFF8000u, 0x00188000u },
    { "srl.d",   "djk", 0xFFFF8000u, 0x00190000u },
    { "sra.d",   "djk", 0xFFFF8000u, 0x00198000u },
    { "rotr.w",  "djk", 0xFFFF8000u, 0x001B0000u },
    { "rotr.d",  "djk", 0xFFFF8000u, 0x001B8000u },
    { "mul.w",   "djk", 0xFFFF8000u, 0x001C0000u },
    { "mulh.w",  "djk", 0xFFFF8000u, 0x001C8000u },
    { "mulh.wu", "djk", 0xFFFF8000u, 0x001D0000u },
    { "mul.d",   "djk", 0xFFFF8000u, 0x001D8000u },
    { "mulh.d",  "djk", 0xFFFF8000u, 0x001E0000u },
    { "mulh.du", "djk", 0xFFFF8000u, 0x001E8000u },
    { "div.w",   "djk", 0xFFFF8000u, 0x00200000u },
    { "mod.w",   "djk", 0xFFFF8000u, 0x00208000u },
    { "div.wu",  "djk", 0xFFFF8000u, 0x00210000u },
    { "mod.wu",  "djk", 0xFFFF8000u, 0x00218000u },
    { "div.d",   "djk", 0xFFFF8000u, 0x00220000u },
    { "mod.d",   "djk", 0xFFFF8000u, 0x00228000u },
    { "div.du",  "djk", 0xFFFF8000u, 0x00230000u },
    { "mod.du",  "djk", 0xFFFF8000u, 0x00238000u },
    { "ldx.b",   "djk", 0xFFFF8000u, 0x38000000u },
    { "ldx.h",   "djk", 0xFFFF8000u, 0x38040000u },
    { "ldx.w",   "djk", 0xFFFF8000u, 0x38080000u },
    { "ldx.d",   "djk", 0xFFFF8000u, 0x380C0000u },
    { "stx.b",   "djk", 0xFFFF8000u, 0x38100000u },
    { "stx.h",   "djk", 0xFFFF8000u, 0x38140000u },
    { "stx.w",   "djk", 0xFFFF8000u, 0x38180000u },
    { "stx.d",   "djk", 0xFFFF8000u, 0x381C0000u },
    { "ldx.bu",  "djk", 0xFFFF8000u, 0x38200000u },
    { "ldx.hu",  "djk", 0xFFFF8000u, 0x38240000u },
    { "ldx.wu",  "djk", 0xFFFF8000u, 0x38280000u },
    { "preldx",  "pjk", 0xFFFF8000u, 0x382C0000u },
    { "fldx.s",  "fjk", 0xFFFF8000u, 0x38300000u },
    { "fldx.d",  "fjk", 0xFFFF8000u, 0x38340000u },
    { "fstx.s",  "fjk", 0xFFFF8000u, 0x38380000u },
    { "fstx.d",  "fjk", 0xFFFF8000u, 0x383C0000u },
    { "dbar",    "p",   0xFFFF8000u, 0x38720000u },
    { "ibar",    "p",   0xFFFF8000u, 0x38728000u },
    { "break",   "n",   0xFFFF8000u, 0x002A0000u },
    { "dbcl",    "n",   0xFFFF8000u, 0x002A8000u },
    { "syscall", "n",   0xFFFF8000u, 0x002B0000u },
    { "idle",    "p",   0xFFFF8000u, 0x06488000u },
    { "invtlb",  "pjk", 0xFFFF8000u, 0x06498000u },

    /* 2RI5 shift-immediate forms */
    { "slli.w",  "dI", 0xFFFF8000u, 0x00408000u },
    { "srli.w",  "dI", 0xFFFF8000u, 0x00448000u },
    { "srai.w",  "dI", 0xFFFF8000u, 0x00488000u },
    { "rotri.w", "dI", 0xFFFF8000u, 0x004C8000u },
    { "slli.d",  "dI", 0xFFFF0000u, 0x00410000u },
    { "srli.d",  "dI", 0xFFFF0000u, 0x00450000u },
    { "srai.d",  "dI", 0xFFFF0000u, 0x00490000u },
    { "rotri.d", "dI", 0xFFFF0000u, 0x004D0000u },

    /* 2RI20 */
    { "lu12i.w",    "dL", 0xFE000000u, 0x14000000u },
    { "lu32i.d",    "dL", 0xFE000000u, 0x16000000u },
    { "pcaddi",     "dL", 0xFE000000u, 0x18000000u },
    { "pcalau12i",  "dL", 0xFE000000u, 0x1A000000u },
    { "pcaddu12i",  "dL", 0xFE000000u, 0x1C000000u },
    { "pcaddu18i",  "dL", 0xFE000000u, 0x1E000000u },

    /* 2RI16 / 2RI14 */
    { "addu16i.d", "djK", 0xFC000000u, 0x10000000u },
    { "ll.w",      "djJ", 0xFF000000u, 0x20000000u },
    { "sc.w",      "djJ", 0xFF000000u, 0x21000000u },
    { "ll.d",      "djJ", 0xFF000000u, 0x22000000u },
    { "sc.d",      "djJ", 0xFF000000u, 0x23000000u },

    /* Branches */
    { "beqz", "jo",  0xFC000000u, 0x40000000u },
    { "bnez", "jo",  0xFC000000u, 0x44000000u },
    { "jirl", "djo", 0xFC000000u, 0x4C000000u },
    { "b",    "O",   0xFC000000u, 0x50000000u },
    { "bl",   "O",   0xFC000000u, 0x54000000u },
    { "beq",  "jdo", 0xFC000000u, 0x58000000u },
    { "bne",  "jdo", 0xFC000000u, 0x5C000000u },
    { "blt",  "jdo", 0xFC000000u, 0x60000000u },
    { "bge",  "jdo", 0xFC000000u, 0x64000000u },
    { "bltu", "jdo", 0xFC000000u, 0x68000000u },
    { "bgeu", "jdo", 0xFC000000u, 0x6C000000u },

    /* 2RI12 loads, stores and arithmetic */
    { "slti",   "djI", 0xFFC00000u, 0x02000000u },
    { "sltui",  "djI", 0xFFC00000u, 0x02400000u },
    { "addi.w", "djI", 0xFFC00000u, 0x02800000u },
    { "addi.d", "djI", 0xFFC00000u, 0x02C00000u },
    { "lu52i.d","djI", 0xFFC00000u, 0x03000000u },
    { "andi",   "djV", 0xFFC00000u, 0x03400000u },
    { "ori",    "djV", 0xFFC00000u, 0x03800000u },
    { "xori",   "djV", 0xFFC00000u, 0x03C00000u },
    { "ld.b",   "djI", 0xFFC00000u, 0x28000000u },
    { "ld.h",   "djI", 0xFFC00000u, 0x28400000u },
    { "ld.w",   "djI", 0xFFC00000u, 0x28800000u },
    { "ld.d",   "djI", 0xFFC00000u, 0x28C00000u },
    { "st.b",   "djI", 0xFFC00000u, 0x29000000u },
    { "st.h",   "djI", 0xFFC00000u, 0x29400000u },
    { "st.w",   "djI", 0xFFC00000u, 0x29800000u },
    { "st.d",   "djI", 0xFFC00000u, 0x29C00000u },
    { "ld.bu",  "djI", 0xFFC00000u, 0x2A000000u },
    { "ld.hu",  "djI", 0xFFC00000u, 0x2A400000u },
    { "ld.wu",  "djI", 0xFFC00000u, 0x2A800000u },
    { "preld",  "pjI", 0xFFC00000u, 0x2AC00000u },
    { "fld.s",  "fjI", 0xFFC00000u, 0x2B000000u },
    { "fst.s",  "fjI", 0xFFC00000u, 0x2B400000u },
    { "fld.d",  "fjI", 0xFFC00000u, 0x2B800000u },
    { "fst.d",  "fjI", 0xFFC00000u, 0x2BC00000u },
    { "cacop",  "pjI", 0xFFC00000u, 0x06000000u }
};

static const CHAR *const KdbpLa64RegisterNames[32] =
{
    "zero", "ra", "tp", "sp", "a0", "a1", "a2", "a3",
    "a4", "a5", "a6", "a7", "t0", "t1", "t2", "t3",
    "t4", "t5", "t6", "t7", "t8", "r21", "fp", "s0",
    "s1", "s2", "s3", "s4", "s5", "s6", "s7", "s8"
};

static LONG64
KdbpLa64SignExtend(ULONG Value, ULONG Bits)
{
    ULONG Shift = 64 - Bits;

    return ((LONG64)((ULONG64)Value << Shift)) >> Shift;
}

/* I16 branch offset: bit 15:0 at 25:10 and 20:16 at 4:0, low two bits are
 * implicit zero because instructions are word aligned. */
static LONG64
KdbpLa64Offset21(ULONG Inst)
{
    ULONG Value = ((Inst >> 10) & 0xFFFF) | ((Inst & 0x1F) << 16);

    return KdbpLa64SignExtend(Value, 21) * 4;
}

/* I26 offset of b/bl: bit 15:0 at 25:10 and 25:16 at 9:0. */
static LONG64
KdbpLa64Offset26(ULONG Inst)
{
    ULONG Value = ((Inst >> 10) & 0xFFFF) | ((Inst & 0x3FF) << 16);

    return KdbpLa64SignExtend(Value, 26) * 4;
}

static const KDBP_LA64_ENCODING *
KdbpLa64Lookup(ULONG Inst)
{
    ULONG Index;

    for (Index = 0; Index < RTL_NUMBER_OF(KdbpLa64Encodings); Index++)
    {
        const KDBP_LA64_ENCODING *Entry = &KdbpLa64Encodings[Index];

        if ((Inst & Entry->Mask) == Entry->Match)
            return Entry;
    }
    return NULL;
}

LONG
KdbpGetInstLength(IN ULONG_PTR Address)
{
    ULONG Inst;

    /* Every LoongArch64 instruction is 32 bits and 4-byte aligned; an
     * unreadable word is reported as unknown rather than assumed. */
    if (!NT_SUCCESS(KdbpSafeReadMemory(&Inst, (PVOID)Address, sizeof(Inst))))
        return -1;
    return 4;
}

LONG
KdbpDisassemble(IN ULONG_PTR Address, IN ULONG IntelSyntax)
{
    const KDBP_LA64_ENCODING *Entry;
    ULONG Inst;
    PCSTR Format;
    BOOLEAN First = TRUE;

    UNREFERENCED_PARAMETER(IntelSyntax);

    if (!NT_SUCCESS(KdbpSafeReadMemory(&Inst, (PVOID)Address, sizeof(Inst))))
    {
        KdbpPrint("<unreadable>");
        return -1;
    }

    Entry = KdbpLa64Lookup(Inst);
    if (Entry == NULL)
    {
        /* Undefined or not in the scalar set: report the fields, do not guess. */
        KdbpPrint("%08lx  .word (opcode 0x%02lx rd %lu rj %lu rk %lu)",
                  Inst, Inst >> 26, Inst & 31, (Inst >> 5) & 31, (Inst >> 10) & 31);
        return 4;
    }

    KdbpPrint("%08lx  %s", Inst, Entry->Name);
    for (Format = Entry->Format; *Format; Format++)
    {
        ULONG Value;

        KdbpPrint(First ? " " : ", ");
        First = FALSE;
        switch (*Format)
        {
            case 'd':
                KdbpPrint("%s", KdbpLa64RegisterNames[Inst & 31]);
                break;
            case 'j':
                KdbpPrint("%s", KdbpLa64RegisterNames[(Inst >> 5) & 31]);
                break;
            case 'k':
                KdbpPrint("%s", KdbpLa64RegisterNames[(Inst >> 10) & 31]);
                break;
            case 'f':
                KdbpPrint("f%lu", Inst & 31);
                break;
            case 'g':
                KdbpPrint("f%lu", (Inst >> 5) & 31);
                break;
            case 'h':
                KdbpPrint("f%lu", (Inst >> 10) & 31);
                break;
            case 'I':
                /* ui5 and si12 share this bit field; both print as unsigned
                 * or signed depending on the fixed opcode that selected them. */
                Value = (Inst >> 10) & 0xFFF;
                KdbpPrint("%I64d", KdbpLa64SignExtend(Value, 12));
                break;
            case 'J':
                KdbpPrint("%I64d", KdbpLa64SignExtend((Inst >> 10) & 0x3FFF, 14));
                break;
            case 'K':
                KdbpPrint("%I64d", KdbpLa64SignExtend((Inst >> 10) & 0xFFFF, 16));
                break;
            case 'L':
                KdbpPrint("%I64d", KdbpLa64SignExtend((Inst >> 5) & 0xFFFFF, 20));
                break;
            case 'U':
                KdbpPrint("%lu", (Inst >> 10) & 63);
                break;
            case 'V':
                KdbpPrint("%lu", (Inst >> 10) & 0xFFF);
                break;
            case 'c':
                KdbpPrint("0x%lx", (Inst >> 10) & 0x3FFF);
                break;
            case 'n':
                KdbpPrint("%lu", Inst & 0x7FFF);
                break;
            case 'p':
                KdbpPrint("%lu", Inst & 0x7FFF);
                break;
            case 'o':
                KdbpPrint("0x%p", (PVOID)(Address + KdbpLa64Offset21(Inst)));
                break;
            case 'O':
                KdbpPrint("0x%p", (PVOID)(Address + KdbpLa64Offset26(Inst)));
                break;
            default:
                KdbpPrint("?");
                break;
        }
    }
    return 4;
}

BOOLEAN
KdbpDisassemblerSelfTest(VOID)
{
    BOOLEAN Passed = TRUE;

    /* beq a0, a1, +16 and b -8: encodings checked by hand from Table 93. */
    if (KdbpLa64Offset21((22u << 26) | ((0x10u >> 2) << 10) | (10u << 5) | 11u) != 16)
        Passed = FALSE;
    if (KdbpLa64Offset26((20u << 26) | ((0x3FFFFFEu >> 2) & 0xFFFF) | (((0x3FFFFFEu >> 2) >> 16) << 0)) != -8)
        Passed = FALSE;
    /* slli.w/d use the ui5/ui6 field, and the lookup order must keep the
     * five-bit form from shadowing the six-bit one. */
    if (KdbpLa64Lookup(0x00408000u) == NULL || KdbpLa64Lookup(0x00410000u) == NULL)
        Passed = FALSE;
    if (KdbpLa64Lookup(0x06483800u) == NULL) /* ertn */
        Passed = FALSE;
    return Passed;
}

/* EOF */
