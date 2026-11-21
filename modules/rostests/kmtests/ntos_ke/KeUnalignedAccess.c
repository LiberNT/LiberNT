/*
 * PROJECT:     LiberNT kernel-mode tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Loads and stores at addresses that are not aligned
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <kmt_test.h>

static union
{
    ULONGLONG Alignment;
    UCHAR Bytes[64];
} Area;

static
VOID
FillArea(VOID)
{
    ULONG Index;

    for (Index = 0; Index < sizeof(Area.Bytes); Index++)
        Area.Bytes[Index] = (UCHAR)(0x10 + Index);
}

static
ULONG
WordAt(
    _In_ ULONG Offset)
{
    return Area.Bytes[Offset] | ((ULONG)Area.Bytes[Offset + 1] << 8) |
           ((ULONG)Area.Bytes[Offset + 2] << 16) | ((ULONG)Area.Bytes[Offset + 3] << 24);
}

static
ULONGLONG
QuadAt(
    _In_ ULONG Offset)
{
    return WordAt(Offset) | ((ULONGLONG)WordAt(Offset + 4) << 32);
}

/* What every architecture does with a 64-bit value that is not at an aligned address */
static
VOID
TestQuadWords(VOID)
{
    volatile ULONGLONG *Pointer;
    ULONGLONG Value;
    ULONG Offset;

    for (Offset = 1; Offset < sizeof(ULONGLONG); Offset++)
    {
        FillArea();
        Pointer = (volatile ULONGLONG *)(Area.Bytes + Offset);
        Value = *Pointer;
        ok(Value == QuadAt(Offset), "offset %lu: read %I64x, expected %I64x\n", Offset, Value, QuadAt(Offset));

        *Pointer = 0x8877665544332211ULL;
        ok(QuadAt(Offset) == 0x8877665544332211ULL, "offset %lu: wrote %I64x\n", Offset, QuadAt(Offset));
        ok_eq_uint(Area.Bytes[Offset - 1], 0x10 + Offset - 1);
        ok_eq_uint(Area.Bytes[Offset + 8], 0x10 + Offset + 8);
    }
}

#ifdef _M_ARM

/* The instructions the processor refuses at an address that is not word aligned, which the kernel carries out */
static
VOID
TestRefusedInstructions(VOID)
{
    PUCHAR Base = Area.Bytes + 1;
    ULONG Out[4];
    ULONG Low, High, Skipped;
    PUCHAR Pointer;

    FillArea();
    Low = High = 0;
    __asm__ __volatile__("ldrd %[low], %[high], [%[base]]"
                         : [low] "=&r"(Low), [high] "=&r"(High) : [base] "r"(Base) : "memory");
    ok_eq_hex(Low, WordAt(1));
    ok_eq_hex(High, WordAt(5));

    Low = High = 0;
    __asm__ __volatile__("ldrd %[low], %[high], [%[base], #8]"
                         : [low] "=&r"(Low), [high] "=&r"(High) : [base] "r"(Base) : "memory");
    ok_eq_hex(Low, WordAt(9));
    ok_eq_hex(High, WordAt(13));

    Pointer = Base;
    Low = High = 0;
    __asm__ __volatile__("ldrd %[low], %[high], [%[pointer]], #8"
                         : [low] "=&r"(Low), [high] "=&r"(High), [pointer] "+&r"(Pointer) : : "memory");
    ok_eq_hex(Low, WordAt(1));
    ok_eq_hex(High, WordAt(5));
    ok_eq_pointer(Pointer, Base + 8);

    Pointer = Base;
    Low = High = 0;
    __asm__ __volatile__("ldrd %[low], %[high], [%[pointer], #16]!"
                         : [low] "=&r"(Low), [high] "=&r"(High), [pointer] "+&r"(Pointer) : : "memory");
    ok_eq_hex(Low, WordAt(17));
    ok_eq_hex(High, WordAt(21));
    ok_eq_pointer(Pointer, Base + 16);

    Low = 0x44332211;
    High = 0x88776655;
    __asm__ __volatile__("strd %[low], %[high], [%[base], #4]"
                         : : [low] "r"(Low), [high] "r"(High), [base] "r"(Base) : "memory");
    ok_eq_hex(WordAt(5), 0x44332211UL);
    ok_eq_hex(WordAt(9), 0x88776655UL);
    ok_eq_uint(Area.Bytes[4], 0x14);
    ok_eq_uint(Area.Bytes[13], 0x1D);

    /* An instruction of an IT block that is carried out leaves the rest of the block its conditions */
    FillArea();
    Low = High = 0;
    Skipped = 0;
    __asm__ __volatile__("cmp %[base], #0\n"
                         "ite ne\n"
                         "ldrdne %[low], %[high], [%[base]]\n"
                         "moveq %[skipped], #1"
                         : [low] "+&r"(Low), [high] "+&r"(High), [skipped] "+&r"(Skipped)
                         : [base] "r"(Base) : "cc", "memory");
    ok_eq_hex(Low, WordAt(1));
    ok_eq_hex(High, WordAt(5));
    ok_eq_ulong(Skipped, 0UL);

    RtlZeroMemory(Out, sizeof(Out));
    __asm__ __volatile__("mov r0, %[base]\n"
                         "ldmia r0!, {r1, r2, r3}\n"
                         "str r1, [%[out]]\n"
                         "str r2, [%[out], #4]\n"
                         "str r3, [%[out], #8]\n"
                         "str r0, [%[out], #12]"
                         : : [base] "r"(Base), [out] "r"(Out) : "r0", "r1", "r2", "r3", "memory");
    ok_eq_hex(Out[0], WordAt(1));
    ok_eq_hex(Out[1], WordAt(5));
    ok_eq_hex(Out[2], WordAt(9));
    ok_eq_hex(Out[3], (ULONG)(ULONG_PTR)(Base + 12));

    RtlZeroMemory(Out, sizeof(Out));
    __asm__ __volatile__("mov r0, %[base]\n"
                         "add r0, r0, #24\n"
                         "ldmdb r0!, {r1, r2, r12}\n"
                         "str r1, [%[out]]\n"
                         "str r2, [%[out], #4]\n"
                         "str r12, [%[out], #8]\n"
                         "str r0, [%[out], #12]"
                         : : [base] "r"(Base), [out] "r"(Out) : "r0", "r1", "r2", "r12", "memory");
    ok_eq_hex(Out[0], WordAt(13));
    ok_eq_hex(Out[1], WordAt(17));
    ok_eq_hex(Out[2], WordAt(21));
    ok_eq_hex(Out[3], (ULONG)(ULONG_PTR)(Base + 12));

    FillArea();
    Out[3] = 0;
    __asm__ __volatile__("mov r0, %[base]\n"
                         "ldr r1, =0x01020304\n"
                         "ldr r2, =0x05060708\n"
                         "ldr r12, =0x090A0B0C\n"
                         "stmia r0!, {r1, r2, r12}\n"
                         "str r0, [%[out], #12]"
                         : : [base] "r"(Base), [out] "r"(Out) : "r0", "r1", "r2", "r12", "memory");
    ok_eq_hex(WordAt(1), 0x01020304UL);
    ok_eq_hex(WordAt(5), 0x05060708UL);
    ok_eq_hex(WordAt(9), 0x090A0B0CUL);
    ok_eq_hex(Out[3], (ULONG)(ULONG_PTR)(Base + 12));
    ok_eq_uint(Area.Bytes[0], 0x10);
    ok_eq_uint(Area.Bytes[13], 0x1D);

    FillArea();
    Low = High = 0;
    __asm__ __volatile__("vldr d0, [%[base], #8]\n"
                         "vmov %[low], %[high], d0"
                         : [low] "=&r"(Low), [high] "=&r"(High) : [base] "r"(Base) : "d0", "memory");
    ok_eq_hex(Low, WordAt(9));
    ok_eq_hex(High, WordAt(13));

    Low = 0xA1A2A3A4;
    High = 0xB1B2B3B4;
    __asm__ __volatile__("vmov d0, %[low], %[high]\n"
                         "vstr d0, [%[base], #16]"
                         : : [low] "r"(Low), [high] "r"(High), [base] "r"(Base) : "d0", "memory");
    ok_eq_hex(WordAt(17), 0xA1A2A3A4UL);
    ok_eq_hex(WordAt(21), 0xB1B2B3B4UL);
    ok_eq_uint(Area.Bytes[16], 0x20);
    ok_eq_uint(Area.Bytes[25], 0x29);

    FillArea();
    Low = 0;
    __asm__ __volatile__("vldr s0, [%[base], #4]\n"
                         "vmov %[low], s0"
                         : [low] "=&r"(Low) : [base] "r"(Base) : "s0", "memory");
    ok_eq_hex(Low, WordAt(5));

    RtlZeroMemory(Out, sizeof(Out));
    __asm__ __volatile__("mov r0, %[base]\n"
                         "vldmia r0!, {d0, d1}\n"
                         "vmov r1, r2, d0\n"
                         "str r1, [%[out]]\n"
                         "str r2, [%[out], #4]\n"
                         "vmov r1, r2, d1\n"
                         "str r1, [%[out], #8]\n"
                         "str r0, [%[out], #12]"
                         : : [base] "r"(Base), [out] "r"(Out) : "r0", "r1", "r2", "d0", "d1", "memory");
    ok_eq_hex(Out[0], WordAt(1));
    ok_eq_hex(Out[1], WordAt(5));
    ok_eq_hex(Out[2], WordAt(9));
    ok_eq_hex(Out[3], (ULONG)(ULONG_PTR)(Base + 16));
}

#endif

START_TEST(KeUnalignedAccess)
{
    TestQuadWords();
#ifdef _M_ARM
    TestRefusedInstructions();
#endif
}
