/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF
 * PURPOSE:     LoongArch64 processor diagnostic state
 */

#include <ntoskrnl.h>
#include "la64ke.h"

/* One CSR per accessor: the CSR address in csrrd/csrwr is a 23:10 immediate
 * ([V1-4.2.1]), so it cannot be a variable. The numbers are the ones in
 * internal/loongarch64/ke.h and [V1-7.4.x]. */
#define KI_LA64_DEFINE_CSR64( Name, Address )                        \
    FORCEINLINE ULONG64 KiLa64Read##Name(VOID)                       \
    {                                                                \
        ULONG64 Value;                                               \
        __asm__ __volatile__("csrrd %0, " #Address : "=r"(Value));   \
        return Value;                                                \
    }                                                                \
    FORCEINLINE VOID KiLa64Write##Name(_In_ ULONG64 Value)           \
    {                                                                \
        __asm__ __volatile__("csrwr %0, " #Address                   \
                             :: "r"(Value) : "memory");              \
    }

#define KI_LA64_DEFINE_CSR32( Name, Address )                        \
    FORCEINLINE ULONG KiLa64Read##Name(VOID)                         \
    {                                                                \
        ULONG64 Value;                                               \
        __asm__ __volatile__("csrrd %0, " #Address : "=r"(Value));   \
        return (ULONG)Value;                                         \
    }                                                                \
    FORCEINLINE VOID KiLa64Write##Name(_In_ ULONG Value)             \
    {                                                                \
        __asm__ __volatile__("csrwr %0, " #Address                   \
                             :: "r"((ULONG_PTR)Value) : "memory");   \
    }

KI_LA64_DEFINE_CSR64(Era, 0x6)
KI_LA64_DEFINE_CSR64(Badv, 0x7)
KI_LA64_DEFINE_CSR64(Eentry, 0xc)
KI_LA64_DEFINE_CSR64(Pgdl, 0x19)
KI_LA64_DEFINE_CSR64(Pgdh, 0x1a)
KI_LA64_DEFINE_CSR64(Dmw0, 0x180)
KI_LA64_DEFINE_CSR64(Dmw1, 0x181)
KI_LA64_DEFINE_CSR64(Dmw2, 0x182)
KI_LA64_DEFINE_CSR64(Dmw3, 0x183)
KI_LA64_DEFINE_CSR32(Llbctl, 0x60)
KI_LA64_DEFINE_CSR32(Stlbps, 0x1e)
KI_LA64_DEFINE_CSR32(Pwcl, 0x1c)
KI_LA64_DEFINE_CSR32(Pwch, 0x1d)

/* EUEN is written field-by-field because the other bits are mode controls
 * the kernel never reprograms independently ([V1-7.4.3]). */
FORCEINLINE VOID
KiLa64WriteEuenValue(_In_ ULONG Value)
{
    __asm__ __volatile__("csrwr %0, 0x2" :: "r"((ULONG_PTR)Value) : "memory");
}

/* ASID is 10 bits wide ([V1-7.5.4]); the reader lives in ke.h and masks it,
 * and only the defined field is ever written back. */
FORCEINLINE VOID
KiLa64WriteAsidValue(_In_ ULONG Value)
{
    ULONG Current = Value & LA64_ASID_MASK;
    ULONG Mask = LA64_ASID_MASK;

    __asm__ __volatile__("csrxchg %0, %1, 0x18"
                         : "+r"(Current)
                         : "r"(Mask)
                         : "memory");
}

VOID NTAPI
KiSaveProcessorState(PKTRAP_FRAME TrapFrame, PKEXCEPTION_FRAME ExceptionFrame)
{
    PKPROCESSOR_STATE State = &KeGetCurrentPrcb()->ProcessorState;

    UNREFERENCED_PARAMETER(ExceptionFrame);
    State->ContextFrame = TrapFrame->Context;
    KiSaveProcessorControlState(State);
}

VOID NTAPI
KiRestoreProcessorState(PKTRAP_FRAME TrapFrame, PKEXCEPTION_FRAME ExceptionFrame)
{
    PKPROCESSOR_STATE State = &KeGetCurrentPrcb()->ProcessorState;

    UNREFERENCED_PARAMETER(ExceptionFrame);
    TrapFrame->Context = State->ContextFrame;
    KiRestoreProcessorControlState(State);
}

ULONG
NTAPI
KeGetRecommendedSharedDataAlignment(VOID)
{
    ULONG Line = KiLa64QueryLargestCacheLine();

    if (!Line)
        Line = KiLa64QueryCacheBlockSize();
    /* Page alignment also separates data when the core reports no geometry. */
    return Line ? Line : PAGE_SIZE;
}

VOID
__cdecl
KeSaveStateForHibernate(_Out_ PKPROCESSOR_STATE ProcessorState)
{
    RtlCaptureContext(&ProcessorState->ContextFrame);
    KiSaveProcessorControlState(ProcessorState);
}

VOID
FASTCALL
KeZeroPages(_Out_writes_bytes_(Size) PVOID Address, _In_ ULONG Size)
{
    ASSERT(((ULONG_PTR)Address & (PAGE_SIZE - 1)) == 0);
    ASSERT((Size & (PAGE_SIZE - 1)) == 0);

    /* MM owns this writable mapping and publishes the page only after zeroing. */
    RtlZeroMemory(Address, Size);
}

VOID
NTAPI
KiSaveProcessorControlState(
    _Out_ PKPROCESSOR_STATE ProcessorState)
{
    PKSPECIAL_REGISTERS Registers = &ProcessorState->SpecialRegisters;

    /* These are sequential call-time observations. Trap entry saves its own
     * interrupted state before a nested trap can overwrite the CSRs. */
    Registers->Era = KiLa64ReadEra();
    Registers->Badv = KiLa64ReadBadv();
    Registers->Eentry = KiLa64ReadEentry();
    Registers->Pgdl = KiLa64ReadPgdl();
    Registers->Pgdh = KiLa64ReadPgdh();
    Registers->Dmw[0] = KiLa64ReadDmw0();
    Registers->Dmw[1] = KiLa64ReadDmw1();
    Registers->Dmw[2] = KiLa64ReadDmw2();
    Registers->Dmw[3] = KiLa64ReadDmw3();
    Registers->Crmd = KiLa64ReadCrmd();
    {
        ULONG64 Prmd;

        __asm__ __volatile__("csrrd %0, 0x1" : "=r"(Prmd));
        Registers->Prmd = (ULONG)Prmd;
    }
    Registers->Euen = KiLa64ReadEuen();
    Registers->Ecfg = KiLa64ReadEcfg();
    Registers->Estat = KiLa64ReadEstat();
    Registers->Asid = KiLa64ReadAsid();
    Registers->Stlbps = KiLa64ReadStlbps();
    Registers->Pwcl = KiLa64ReadPwcl();
    Registers->Pwch = KiLa64ReadPwch();
    Registers->Llbctl = KiLa64ReadLlbctl();
}

VOID
NTAPI
KiRestoreProcessorControlState(_In_ PKPROCESSOR_STATE ProcessorState)
{
    PKSPECIAL_REGISTERS Registers;
    ULONG64 Root;

    if (!ProcessorState)
        return;

    Registers = &ProcessorState->SpecialRegisters;
    KeDisableInterrupts();

    KiLa64WriteEentry(Registers->Eentry);
    KiLa64WritePwcl(Registers->Pwcl);
    KiLa64WritePwch(Registers->Pwch);
    KiLa64WriteStlbps(Registers->Stlbps);

    /* A different root changes every translation on this core, so the stale
     * entries must go with it. Both halves are written: PGDL and PGDH are
     * two registers here ([V1-7.5.5], [V1-7.5.6]). */
    Root = Registers->Pgdl;
    if ((Root != KiLa64ReadPgdl()) || (Registers->Pgdh != KiLa64ReadPgdh()))
    {
        KiLa64WritePgdl(Root);
        KiLa64WritePgdh(Registers->Pgdh);
        KiLa64InvalidateAllTlb();
        KiLa64MemoryBarrier();
    }
    KiLa64WriteAsidValue(Registers->Asid);

    KiLa64WriteDmw0(Registers->Dmw[0]);
    KiLa64WriteDmw1(Registers->Dmw[1]);
    KiLa64WriteDmw2(Registers->Dmw[2]);
    KiLa64WriteDmw3(Registers->Dmw[3]);
    KiLa64WriteLlbctl(Registers->Llbctl);

    /* Pending timer and line interrupts belong to their controllers: the
     * saved ESTAT snapshot is read-only status and is not replayed. Ecfg
     * decides which lines the core may take, so it is restored. */
    KiLa64WriteEcfg(Registers->Ecfg);
    KiLa64WriteEuenValue(Registers->Euen);

    KiLa64WriteEra(Registers->Era);
    KiLa64WriteBadv(Registers->Badv);
    KiLa64WritePrmd(Registers->Prmd);

    /* CRMD last: the saved image carries PLV, IE, DA, PG, DATF and DATM, and
     * writing anything else would change the translation mode under the
     * instruction stream. */
    KiLa64WriteCrmd(Registers->Crmd);
    KiLa64MemoryBarrier();
}

/* Register state for the bugcheck screen and log. */
VOID
KiDisplayBugCheckRegisterState(
    _In_opt_ PKTRAP_FRAME TrapFrame,
    _In_ PCONTEXT Context)
{
    CHAR Line[128];

    static const PCSTR Names[32] =
    {
        "zero", "ra", "tp", "sp", "a0", "a1", "a2", "a3",
        "a4", "a5", "a6", "a7", "t0", "t1", "t2", "t3",
        "t4", "t5", "t6", "t7", "t8", "u0", "fp", "s0",
        "s1", "s2", "s3", "s4", "s5", "s6", "s7", "s8"
    };
    const CONTEXT *Registers = TrapFrame ? &TrapFrame->Context : Context;
    ULONG Index;

    if (TrapFrame != NULL)
    {
        RtlStringCbPrintfA(Line, sizeof(Line), "pc=%016I64x prmd=%08lx estat=%08lx\r\n",
                           Registers->Pc, TrapFrame->Prmd, TrapFrame->Estat);
        KiDisplayAndLogBugCheckString(Line);
        RtlStringCbPrintfA(Line, sizeof(Line), "badv=%016I64x crmd=%08lx euen=%08lx\r\n",
                           TrapFrame->Badv, TrapFrame->Crmd, TrapFrame->Euen);
    }
    else
    {
        RtlStringCbPrintfA(Line, sizeof(Line), "pc=%016I64x\r\n", Registers->Pc);
    }
    KiDisplayAndLogBugCheckString(Line);

    /* r1..r31, three per line */
    for (Index = 1; Index < RTL_NUMBER_OF(Names); Index += 3)
    {
        if (Index + 2 < RTL_NUMBER_OF(Names))
        {
            RtlStringCbPrintfA(Line, sizeof(Line), "%-4s=%016I64x %-4s=%016I64x %-4s=%016I64x\r\n",
                               Names[Index], Registers->R[Index],
                               Names[Index + 1], Registers->R[Index + 1],
                               Names[Index + 2], Registers->R[Index + 2]);
        }
        else
        {
            RtlStringCbPrintfA(Line, sizeof(Line), "%-4s=%016I64x\r\n",
                               Names[Index], Registers->R[Index]);
        }
        KiDisplayAndLogBugCheckString(Line);
    }
}

BOOLEAN
NTAPI
KiUnwindFrame(
    _Inout_ PCONTEXT Context,
    _In_ BOOLEAN CallerFrame)
{
    UNREFERENCED_PARAMETER(CallerFrame);

    /* FIXME: the LoongArch psABI keeps the return address at fp-8 and the previous fp at fp-16 */
    return KiUnwindFrameRecord(Context);
}
