/*
 * PROJECT:     LiberNT Run-Time Library
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     ARM (Thumb-2) PE/COFF unwinding support
 * COPYRIGHT:   Unwind-code interpreter ported from the Wine project
 *              (dlls/ntdll/unwind.c), LGPL-2.1-or-later.
 *              Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <rtl.h>

#define NDEBUG
#include <debug.h>

PRUNTIME_FUNCTION
NTAPI
RtlLookupFunctionTable(
    _In_ ULONG_PTR ControlPc,
    _Out_ PULONG_PTR ImageBase,
    _Out_ PULONG Length);

VOID
NTAPI
RtlpArmRestoreCollidedFrame(
    _Inout_ PDISPATCHER_CONTEXT DispatcherContext,
    _Out_ PCONTEXT UnwindContext,
    _Out_writes_bytes_(NonVolatileRegistersLength) PVOID NonVolatileRegisters,
    _In_ ULONG NonVolatileRegistersLength,
    _Out_ PULONG EstablisherFrame);

#define RTLP_ARM_NONVOLATILE_WORDS (8 + 8 * 2)

VOID
NTAPI
RtlpArmCaptureNonVolatileRegisters(
    _Out_writes_(RTLP_ARM_NONVOLATILE_WORDS) PULONG NonVolatileRegisters,
    _In_ PCONTEXT Context);

/* .xdata header and epilogue scope (ARM exception handling, PE/COFF). */
typedef struct _ARM_UNWIND_INFO
{
    ULONG FunctionLength : 18;
    ULONG Version : 2;
    ULONG X : 1;
    ULONG E : 1;
    ULONG F : 1;
    ULONG EpilogueCount : 5;
    ULONG CodeWords : 4;
} ARM_UNWIND_INFO, *PARM_UNWIND_INFO;

typedef struct _ARM_UNWIND_INFO_EXT
{
    USHORT EpilogueCount;
    UCHAR CodeWords;
    UCHAR Reserved;
} ARM_UNWIND_INFO_EXT, *PARM_UNWIND_INFO_EXT;

typedef struct _ARM_UNWIND_EPILOGUE
{
    ULONG Offset : 18;
    ULONG Reserved : 2;
    ULONG Condition : 4;
    ULONG Index : 8;
} ARM_UNWIND_EPILOGUE, *PARM_UNWIND_EPILOGUE;

static const UCHAR RtlpArmUnwindCodeLength[256] =
{
/* 00 */ 1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
/* 20 */ 1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
/* 40 */ 1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
/* 60 */ 1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
/* 80 */ 2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,
/* a0 */ 2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,
/* c0 */ 1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
/* e0 */ 1,1,1,1,1,1,1,1,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,3,4,3,4,1,1,1,1,1
};

static const UCHAR RtlpArmUnwindInstructionLength[256] =
{
/* 00 */ 2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,
/* 20 */ 2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,
/* 40 */ 2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,
/* 60 */ 2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,
/* 80 */ 4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,
/* a0 */ 4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,
/* c0 */ 2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,4,4,4,4,4,4,4,4,
/* e0 */ 4,4,4,4,4,4,4,4,4,4,4,4,2,2,0,4,0,0,0,0,0,4,4,2,2,4,4,2,4,2,4,0
};

static
ULONG
RtlpArmSequenceLength(
    _In_ PUCHAR Code,
    _In_ PUCHAR End,
    _In_ BOOLEAN IncludeEnd)
{
    ULONG Length = 0;

    while (Code < End)
    {
        if (*Code >= 0xFD)
        {
            if ((*Code <= 0xFE) && IncludeEnd)
                Length += RtlpArmUnwindInstructionLength[*Code];
            break;
        }
        Length += RtlpArmUnwindInstructionLength[*Code];
        Code += RtlpArmUnwindCodeLength[*Code];
    }
    return Length;
}

static
VOID
RtlpArmPopRegisterMask(
    _In_ ULONG Mask,
    _Inout_ PCONTEXT Context,
    _Inout_opt_ PKNONVOLATILE_CONTEXT_POINTERS Pointers)
{
    ULONG Index;

    for (Index = 0; Index <= 12; Index++)
    {
        if (!(Mask & (1UL << Index)))
            continue;
        if (Pointers && (Index >= 4) && (Index <= 11))
            (&Pointers->R4)[Index - 4] = (PULONG)(ULONG_PTR)Context->Sp;
        if (Index >= 4)
            (&Context->R0)[Index] = *(PULONG)(ULONG_PTR)Context->Sp;
        Context->Sp += 4;
    }
}

static
VOID
RtlpArmPopRegisterRange(
    _In_ ULONG Last,
    _Inout_ PCONTEXT Context,
    _Inout_opt_ PKNONVOLATILE_CONTEXT_POINTERS Pointers)
{
    ULONG Index;

    for (Index = 4; Index <= Last; Index++)
    {
        if (Pointers)
            (&Pointers->R4)[Index - 4] = (PULONG)(ULONG_PTR)Context->Sp;
        (&Context->R0)[Index] = *(PULONG)(ULONG_PTR)Context->Sp;
        Context->Sp += 4;
    }
}

static
VOID
RtlpArmPopLr(
    _In_ ULONG Increment,
    _Inout_ PCONTEXT Context,
    _Inout_opt_ PKNONVOLATILE_CONTEXT_POINTERS Pointers)
{
    if (Pointers)
        Pointers->Lr = (PULONG)(ULONG_PTR)Context->Sp;
    Context->Lr = *(PULONG)(ULONG_PTR)Context->Sp;
    Context->Sp += Increment;
}

static
VOID
RtlpArmPopFloatRange(
    _In_ ULONG First,
    _In_ ULONG Last,
    _Inout_ PCONTEXT Context,
    _Inout_opt_ PKNONVOLATILE_CONTEXT_POINTERS Pointers)
{
    ULONG Index;

    for (Index = First; Index <= Last; Index++)
    {
        if (Pointers && (Index >= 8) && (Index <= 15))
            (&Pointers->D8)[Index - 8] = (PULONGLONG)(ULONG_PTR)Context->Sp;
        Context->D[Index] = *(PULONGLONG)(ULONG_PTR)Context->Sp;
        Context->Sp += 8;
    }
}

static
VOID
RtlpArmMicrosoftOpcode(
    _In_ UCHAR Opcode,
    _Inout_ PCONTEXT Context,
    _Inout_opt_ PKNONVOLATILE_CONTEXT_POINTERS Pointers)
{
    PCONTEXT Source;
    ULONG Index;

    switch (Opcode)
    {
        case 1: /* machine frame: Sp, Pc */
            Context->Pc = ((PULONG)(ULONG_PTR)Context->Sp)[1];
            Context->Sp = ((PULONG)(ULONG_PTR)Context->Sp)[0];
            Context->ContextFlags &= ~CONTEXT_UNWOUND_TO_CALL;
            break;

        case 2: /* full CONTEXT on the stack */
            Source = (PCONTEXT)(ULONG_PTR)Context->Sp;
            *Context = *Source;
            if (!Pointers)
                break;
            for (Index = 0; Index < 8; Index++)
                (&Pointers->R4)[Index] = &Source->R4 + Index;
            Pointers->Lr = &Source->Lr;
            for (Index = 0; Index < 8; Index++)
                (&Pointers->D8)[Index] = &Source->D[Index + 8];
            break;

        default:
            DPRINT1("Unsupported ARM unwind opcode ee %02x\n", Opcode);
            break;
    }
}

static
VOID
RtlpArmProcessUnwindCodes(
    _In_ PUCHAR Code,
    _In_ PUCHAR End,
    _Inout_ PCONTEXT Context,
    _Inout_opt_ PKNONVOLATILE_CONTEXT_POINTERS Pointers,
    _In_ LONG Skip)
{
    ULONG Value, Length, Index;

    while ((Code < End) && Skip)
    {
        if (*Code >= 0xFD)
            break;
        Skip -= RtlpArmUnwindInstructionLength[*Code];
        Code += RtlpArmUnwindCodeLength[*Code];
    }

    while (Code < End)
    {
        Length = RtlpArmUnwindCodeLength[*Code];
        if (Code + Length > End)
            break;
        Value = 0;
        for (Index = 0; Index < Length; Index++)
            Value = (Value << 8) | Code[Index];

        if (*Code <= 0x7F)
            Context->Sp += 4 * (Value & 0x7F);
        else if (*Code <= 0xBF)
        {
            RtlpArmPopRegisterMask(Value & 0x1FFF, Context, Pointers);
            if (Value & 0x2000)
                RtlpArmPopLr(4, Context, Pointers);
        }
        else if (*Code <= 0xCF)
            Context->Sp = (&Context->R0)[Value & 0x0F];
        else if (*Code <= 0xD7)
        {
            RtlpArmPopRegisterRange((Value & 0x03) + 4, Context, Pointers);
            if (Value & 0x04)
                RtlpArmPopLr(4, Context, Pointers);
        }
        else if (*Code <= 0xDF)
        {
            RtlpArmPopRegisterRange((Value & 0x03) + 8, Context, Pointers);
            if (Value & 0x04)
                RtlpArmPopLr(4, Context, Pointers);
        }
        else if (*Code <= 0xE7)
            RtlpArmPopFloatRange(8, (Value & 0x07) + 8, Context, Pointers);
        else if (*Code <= 0xEB)
            Context->Sp += 4 * (Value & 0x3FF);
        else if (*Code <= 0xED)
        {
            RtlpArmPopRegisterMask(Value & 0xFF, Context, Pointers);
            if (Value & 0x100)
                RtlpArmPopLr(4, Context, Pointers);
        }
        else if (*Code <= 0xEE)
            RtlpArmMicrosoftOpcode((UCHAR)(Value & 0xFF), Context, Pointers);
        else if ((*Code <= 0xEF) && ((Value & 0xFF) <= 0x0F))
            RtlpArmPopLr(4 * (Value & 0x0F), Context, Pointers);
        else if (*Code <= 0xF4)
            DPRINT1("Unsupported ARM unwind code %02x\n", *Code);
        else if (*Code <= 0xF5)
            RtlpArmPopFloatRange((Value & 0xF0) >> 4, Value & 0x0F, Context, Pointers);
        else if (*Code <= 0xF6)
            RtlpArmPopFloatRange(((Value & 0xF0) >> 4) + 16, (Value & 0x0F) + 16, Context, Pointers);
        else if ((*Code == 0xF7) || (*Code == 0xF9))
            Context->Sp += 4 * (Value & 0xFFFF);
        else if ((*Code == 0xF8) || (*Code == 0xFA))
            Context->Sp += 4 * (Value & 0xFFFFFF);
        else if (*Code <= 0xFC)
            ;
        else
            break;

        Code += Length;
    }
}

static
VOID
RtlpArmUnwindPacked(
    _In_ ULONG_PTR ImageBase,
    _In_ ULONG_PTR ControlPc,
    _In_ PRUNTIME_FUNCTION Function,
    _Inout_ PCONTEXT Context,
    _Inout_opt_ PKNONVOLATILE_CONTEXT_POINTERS Pointers)
{
    LONG Index, Position = 0;
    LONG PrologueFolded = 0, EpilogueFolded = 0, FrameOffset = 0, Stack = Function->StackAdjust;
    ULONG PrologueMask = 0, EpilogueMask = 0, Offset, Length;
    UCHAR Prologue[10], Epilogue[20];
    PUCHAR PrologueEnd, EpilogueEnd;

    Offset = (ULONG)(ControlPc - ImageBase) - (Function->BeginAddress & ~1UL);
    if (Function->StackAdjust >= 0x03F4)
    {
        PrologueFolded = Function->StackAdjust & 0x04;
        EpilogueFolded = Function->StackAdjust & 0x08;
        Stack = (Function->StackAdjust & 3) + 1;
    }

    if (!Function->R || PrologueFolded)
    {
        LONG First = 4, Last = Function->Reg + 4;

        if (PrologueFolded)
        {
            First = (~Function->StackAdjust) & 3;
            if (Function->R)
                Last = 3;
        }
        for (Index = First; Index <= Last; Index++)
            PrologueMask |= 1UL << Index;
        FrameOffset = Last + 1 - First;
    }

    if (!Function->R || EpilogueFolded)
    {
        LONG First = 4, Last = Function->Reg + 4;

        if (EpilogueFolded)
        {
            First = (~Function->StackAdjust) & 3;
            if (Function->R)
                Last = 3;
        }
        for (Index = First; Index <= Last; Index++)
            EpilogueMask |= 1UL << Index;
    }

    if (Function->C)
    {
        PrologueMask |= 1UL << 11;
        EpilogueMask |= 1UL << 11;
    }

    if (Function->L)
    {
        PrologueMask |= 1UL << 14;
        if (Function->Ret != 0)
            EpilogueMask |= 1UL << 14;
        else if (!Function->H)
            EpilogueMask |= 1UL << 15;
    }

    /* Prologue, in the order the unwinder undoes it. */
    if (Stack && !PrologueFolded)
    {
        if (Stack <= 0x7F)
        {
            Prologue[Position++] = (UCHAR)Stack;
        }
        else
        {
            Prologue[Position++] = (UCHAR)(0xE8 | (Stack >> 8));
            Prologue[Position++] = (UCHAR)(Stack & 0xFF);
        }
    }

    if (Function->R && (Function->Reg != 7))
        Prologue[Position++] = (UCHAR)(0xE0 | Function->Reg);

    if (Function->C && (FrameOffset == 0))
        Prologue[Position++] = 0xFB;
    else if (Function->C)
        Prologue[Position++] = 0xFC;

    if (PrologueMask & 0xF00)
    {
        ULONG Mask = PrologueMask & 0x1FFF;

        if (PrologueMask & (1UL << 14))
            Mask |= 0x2000;
        Prologue[Position++] = (UCHAR)(0x80 | (Mask >> 8));
        Prologue[Position++] = (UCHAR)(Mask & 0xFF);
    }
    else if (PrologueMask)
    {
        ULONG Mask = PrologueMask & 0xFF;

        if (PrologueMask & (1UL << 14))
            Mask |= 0x100;
        Prologue[Position++] = (UCHAR)(0xEC | (Mask >> 8));
        Prologue[Position++] = (UCHAR)(Mask & 0xFF);
    }

    if (Function->H)
        Prologue[Position++] = 0x04;

    Prologue[Position++] = 0xFF;
    PrologueEnd = &Prologue[Position];

    Position = 0;
    if (Stack && !EpilogueFolded)
    {
        if (Stack <= 0x7F)
        {
            Epilogue[Position++] = (UCHAR)Stack;
        }
        else
        {
            Epilogue[Position++] = (UCHAR)(0xE8 | (Stack >> 8));
            Epilogue[Position++] = (UCHAR)(Stack & 0xFF);
        }
    }

    if (Function->R && (Function->Reg != 7))
        Epilogue[Position++] = (UCHAR)(0xE0 | Function->Reg);

    if (EpilogueMask & 0x7F00)
    {
        ULONG Mask = EpilogueMask & 0x1FFF;

        if (EpilogueMask & (3UL << 14))
            Mask |= 0x2000;
        Epilogue[Position++] = (UCHAR)(0x80 | (Mask >> 8));
        Epilogue[Position++] = (UCHAR)(Mask & 0xFF);
    }
    else if (EpilogueMask)
    {
        ULONG Mask = EpilogueMask & 0xFF;

        if (EpilogueMask & (1UL << 15))
            Mask |= 0x100;
        Epilogue[Position++] = (UCHAR)(0xEC | (Mask >> 8));
        Epilogue[Position++] = (UCHAR)(Mask & 0xFF);
    }

    if (Function->H && !(Function->L && (Function->Ret == 0)))
    {
        Epilogue[Position++] = 0x04;
    }
    else if (Function->H && (Function->L && (Function->Ret == 0)))
    {
        Epilogue[Position++] = 0xEF;
        Epilogue[Position++] = 5;
    }

    if (Function->Ret == 1)
        Epilogue[Position++] = 0xFD;
    else if (Function->Ret == 2)
        Epilogue[Position++] = 0xFE;
    else
        Epilogue[Position++] = 0xFF;
    EpilogueEnd = &Epilogue[Position];

    if ((Function->Flag == 1) && (Offset < 4 * (ULONG)(PrologueEnd - Prologue)))
    {
        Length = RtlpArmSequenceLength(Prologue, PrologueEnd, FALSE);
        if (Offset < Length)
        {
            RtlpArmProcessUnwindCodes(Prologue, PrologueEnd, Context, Pointers, Length - Offset);
            return;
        }
    }

    if ((Function->Ret != 3) && (2 * Function->FunctionLength - Offset <= 4 * (ULONG)(EpilogueEnd - Epilogue)))
    {
        Length = RtlpArmSequenceLength(Epilogue, EpilogueEnd, TRUE);
        if (Offset >= 2 * Function->FunctionLength - Length)
        {
            RtlpArmProcessUnwindCodes(Epilogue, EpilogueEnd, Context, Pointers,
                                      Offset - (2 * Function->FunctionLength - Length));
            return;
        }
    }

    RtlpArmProcessUnwindCodes(Prologue, PrologueEnd, Context, Pointers, 0);
}

static
PVOID
RtlpArmUnwindFull(
    _In_ ULONG_PTR ImageBase,
    _In_ ULONG_PTR ControlPc,
    _In_ PRUNTIME_FUNCTION Function,
    _Inout_ PCONTEXT Context,
    _Out_ PVOID *HandlerData,
    _Inout_opt_ PKNONVOLATILE_CONTEXT_POINTERS Pointers)
{
    PARM_UNWIND_INFO Info;
    PARM_UNWIND_EPILOGUE Epilogues;
    ULONG Index, CodeWords, EpilogueCount, Length, Offset;
    PUCHAR Data, End, Code;
    PULONG HandlerRva;

    Info = (PARM_UNWIND_INFO)(ImageBase + Function->UnwindData);
    Data = (PUCHAR)(Info + 1);
    EpilogueCount = Info->EpilogueCount;
    CodeWords = Info->CodeWords;
    if (!CodeWords && !EpilogueCount)
    {
        PARM_UNWIND_INFO_EXT Extended = (PARM_UNWIND_INFO_EXT)Data;

        CodeWords = Extended->CodeWords;
        EpilogueCount = Extended->EpilogueCount;
        Data = (PUCHAR)(Extended + 1);
    }

    Epilogues = (PARM_UNWIND_EPILOGUE)Data;
    if (!Info->E)
        Data = (PUCHAR)(Epilogues + EpilogueCount);

    Offset = (ULONG)(ControlPc - ImageBase) - (Function->BeginAddress & ~1UL);
    End = Data + CodeWords * 4;

    if ((Offset < CodeWords * 4 * 4) && !Info->F)
    {
        Length = RtlpArmSequenceLength(Data, End, FALSE);
        if (Offset < Length)
        {
            RtlpArmProcessUnwindCodes(Data, End, Context, Pointers, Length - Offset);
            return NULL;
        }
    }

    if (!Info->E)
    {
        for (Index = 0; Index < EpilogueCount; Index++)
        {
            /* TODO: honour epilogue conditions in IT blocks */
            if (Offset < 2 * Epilogues[Index].Offset)
                break;
            if (Offset - 2 * Epilogues[Index].Offset < (CodeWords * 4 - Epilogues[Index].Index) * 4)
            {
                Code = Data + Epilogues[Index].Index;
                Length = RtlpArmSequenceLength(Code, End, TRUE);
                if (Offset <= 2 * Epilogues[Index].Offset + Length)
                {
                    RtlpArmProcessUnwindCodes(Code, End, Context, Pointers, Offset - 2 * Epilogues[Index].Offset);
                    return NULL;
                }
            }
        }
    }
    else if (2 * Info->FunctionLength - Offset <= (CodeWords * 4 - EpilogueCount) * 4)
    {
        Code = Data + EpilogueCount;
        Length = RtlpArmSequenceLength(Code, End, TRUE);
        if (Offset >= 2 * Info->FunctionLength - Length)
        {
            RtlpArmProcessUnwindCodes(Code, End, Context, Pointers, Offset - (2 * Info->FunctionLength - Length));
            return NULL;
        }
    }

    RtlpArmProcessUnwindCodes(Data, End, Context, Pointers, 0);

    if (Info->X)
    {
        HandlerRva = (PULONG)Data + CodeWords;
        *HandlerData = HandlerRva + 1;
        return (PVOID)(ImageBase + *HandlerRva);
    }
    return NULL;
}

static
ULONG
RtlpArmFunctionLength(
    _In_ ULONG_PTR ImageBase,
    _In_ PRUNTIME_FUNCTION Function)
{
    if (Function->Flag)
        return 2 * Function->FunctionLength;
    return 2 * ((PARM_UNWIND_INFO)(ImageBase + Function->UnwindData))->FunctionLength;
}

static
PRUNTIME_FUNCTION
RtlpArmFindFunction(
    _In_ ULONG_PTR ControlPc,
    _In_ ULONG_PTR ImageBase,
    _In_ PRUNTIME_FUNCTION Table,
    _In_ ULONG Count)
{
    LONG Low = 0, High = (LONG)Count - 1, Middle;
    ULONG_PTR Start;

    while (Low <= High)
    {
        Middle = (Low + High) / 2;
        Start = ImageBase + (Table[Middle].BeginAddress & ~1UL);
        if (ControlPc >= Start)
        {
            if (ControlPc < Start + RtlpArmFunctionLength(ImageBase, &Table[Middle]))
                return &Table[Middle];
            Low = Middle + 1;
        }
        else
        {
            High = Middle - 1;
        }
    }
    return NULL;
}

PRUNTIME_FUNCTION
NTAPI
RtlLookupFunctionEntry(
    _In_ ULONG_PTR ControlPc,
    _Out_ PDWORD ImageBase,
    _Inout_opt_ PUNWIND_HISTORY_TABLE HistoryTable)
{
    PRUNTIME_FUNCTION Table;
    ULONG_PTR Base = 0;
    ULONG Size;

    UNREFERENCED_PARAMETER(HistoryTable);

    ControlPc &= ~1UL;
    Table = RtlLookupFunctionTable(ControlPc, &Base, &Size);
    *ImageBase = Table ? Base : 0;
    /* TODO: search the dynamic function tables JIT code registers */
    return Table ? RtlpArmFindFunction(ControlPc, Base, Table, Size / sizeof(*Table)) : NULL;
}

PEXCEPTION_ROUTINE
NTAPI
RtlVirtualUnwind(
    _In_ DWORD HandlerType,
    _In_ DWORD ImageBase,
    _In_ DWORD ControlPc,
    _In_opt_ PRUNTIME_FUNCTION FunctionEntry,
    _Inout_ PCONTEXT Context,
    _Out_ PVOID *HandlerData,
    _Out_ PDWORD EstablisherFrame,
    _Inout_opt_ PKNONVOLATILE_CONTEXT_POINTERS ContextPointers)
{
    PVOID Handler = NULL;
    PVOID Data = NULL;

    ControlPc &= ~1UL;
    Context->Pc = 0;

    if (!FunctionEntry)
    {
        /* A leaf function keeps its return address in lr and moves no stack. */
        if (ControlPc == (Context->Lr & ~1UL))
        {
            *HandlerData = NULL;
            *EstablisherFrame = Context->Sp;
            return NULL;
        }
    }
    else if (FunctionEntry->Flag)
    {
        RtlpArmUnwindPacked(ImageBase, ControlPc, FunctionEntry, Context, ContextPointers);
    }
    else
    {
        Handler = RtlpArmUnwindFull(ImageBase, ControlPc, FunctionEntry, Context, &Data, ContextPointers);
    }

    if (!Context->Pc)
    {
        Context->Pc = Context->Lr;
        Context->ContextFlags |= CONTEXT_UNWOUND_TO_CALL;
    }
    *EstablisherFrame = Context->Sp;

    if (Handler && (HandlerType & (UNW_FLAG_EHANDLER | UNW_FLAG_UHANDLER)))
    {
        *HandlerData = Data;
        return (PEXCEPTION_ROUTINE)Handler;
    }
    *HandlerData = NULL;
    return NULL;
}

VOID
NTAPI
RtlUnwindEx(
    _In_opt_ PVOID TargetFrame,
    _In_opt_ PVOID TargetIp,
    _In_opt_ PEXCEPTION_RECORD ExceptionRecord,
    _In_ PVOID ReturnValue,
    _In_ PCONTEXT ContextRecord,
    _In_opt_ PUNWIND_HISTORY_TABLE HistoryTable)
{
    EXCEPTION_RECORD LocalExceptionRecord;
    DISPATCHER_CONTEXT DispatcherContext;
    ULONG NonVolatileRegisters[RTLP_ARM_NONVOLATILE_WORDS];
    PEXCEPTION_ROUTINE ExceptionRoutine;
    EXCEPTION_DISPOSITION Disposition;
    PRUNTIME_FUNCTION FunctionEntry;
    ULONG ImageBase, EstablisherFrame, ControlPc, LookupPc, PreviousSp;
    ULONG_PTR StackLow, StackHigh;
    CONTEXT UnwindContext;
    BOOLEAN ControlPcIsUnwound, HaveTarget;

    RtlCaptureContext(ContextRecord);

    if (!ExceptionRecord)
    {
        RtlZeroMemory(&LocalExceptionRecord, sizeof(LocalExceptionRecord));
        LocalExceptionRecord.ExceptionCode = STATUS_UNWIND;
        LocalExceptionRecord.ExceptionAddress = (PVOID)(ULONG_PTR)ContextRecord->Pc;
        ExceptionRecord = &LocalExceptionRecord;
    }

    ExceptionRecord->ExceptionFlags = EXCEPTION_UNWINDING;
    if (!TargetFrame)
        ExceptionRecord->ExceptionFlags |= EXCEPTION_EXIT_UNWIND;

    HaveTarget = (TargetFrame != NULL) || (TargetIp != NULL);
    UnwindContext = *ContextRecord;
    RtlZeroMemory(&DispatcherContext, sizeof(DispatcherContext));
    DispatcherContext.ContextRecord = ContextRecord;
    DispatcherContext.HistoryTable = HistoryTable;
    DispatcherContext.TargetPc = (ULONG)(ULONG_PTR)TargetIp;
    DispatcherContext.NonVolatileRegisters = (PBYTE)NonVolatileRegisters;

    RtlpGetStackLimits(&StackLow, &StackHigh);
    if (TargetFrame)
        StackHigh = (ULONG_PTR)TargetFrame + 1;

    for (;;)
    {
        if ((UnwindContext.Sp < StackLow) || (UnwindContext.Sp > StackHigh) || (UnwindContext.Sp & 3))
        {
            ExceptionRecord->ExceptionFlags |= EXCEPTION_STACK_INVALID;
            return;
        }

        ControlPc = UnwindContext.Pc & ~1UL;
        ControlPcIsUnwound = !!(UnwindContext.ContextFlags & CONTEXT_UNWOUND_TO_CALL);
        LookupPc = ControlPcIsUnwound ? (ControlPc - 2) : ControlPc;
        FunctionEntry = RtlLookupFunctionEntry(LookupPc, &ImageBase, NULL);
        if (!FunctionEntry)
        {
            if (!UnwindContext.Lr || ((UnwindContext.Lr & ~1UL) == ControlPc))
                break;
            UnwindContext.Pc = UnwindContext.Lr;
            UnwindContext.ContextFlags |= CONTEXT_UNWOUND_TO_CALL;
            continue;
        }

        DispatcherContext.ControlPc = ControlPc;
        DispatcherContext.ControlPcIsUnwound = ControlPcIsUnwound;
        PreviousSp = UnwindContext.Sp;
        RtlpArmCaptureNonVolatileRegisters(NonVolatileRegisters, &UnwindContext);
        ExceptionRoutine = RtlVirtualUnwind(UNW_FLAG_UHANDLER, ImageBase, LookupPc, FunctionEntry,
                                            &UnwindContext, &DispatcherContext.HandlerData,
                                            &EstablisherFrame, NULL);

        if ((EstablisherFrame < StackLow) || (EstablisherFrame >= StackHigh) || (EstablisherFrame & 3) ||
            ((UnwindContext.Sp == PreviousSp) && ((UnwindContext.Pc & ~1UL) == ControlPc)))
        {
            ExceptionRecord->ExceptionFlags |= EXCEPTION_STACK_INVALID;
            return;
        }

        if (ExceptionRoutine)
        {
            DispatcherContext.ImageBase = ImageBase;
            DispatcherContext.FunctionEntry = FunctionEntry;
            DispatcherContext.LanguageHandler = ExceptionRoutine;
            DispatcherContext.EstablisherFrame = EstablisherFrame;
            DispatcherContext.ScopeIndex = 0;

            do
            {
                if (HaveTarget && (EstablisherFrame == (ULONG)(ULONG_PTR)TargetFrame))
                    ExceptionRecord->ExceptionFlags |= EXCEPTION_TARGET_UNWIND;

                Disposition = RtlpExecuteHandlerForUnwind(ExceptionRecord, (PVOID)(ULONG_PTR)EstablisherFrame,
                                                          ContextRecord, &DispatcherContext,
                                                          DispatcherContext.LanguageHandler);

                ExceptionRecord->ExceptionFlags &= ~(EXCEPTION_TARGET_UNWIND | EXCEPTION_COLLIDED_UNWIND);

                if (Disposition == ExceptionContinueExecution)
                {
                    if (ExceptionRecord->ExceptionFlags & EXCEPTION_NONCONTINUABLE)
                        RtlRaiseStatus(STATUS_NONCONTINUABLE_EXCEPTION);
                    return;
                }

                if (Disposition == ExceptionCollidedUnwind)
                {
                    *ContextRecord = *DispatcherContext.ContextRecord;
                    RtlpArmRestoreCollidedFrame(&DispatcherContext, &UnwindContext, NonVolatileRegisters,
                                                sizeof(NonVolatileRegisters), &EstablisherFrame);
                    DispatcherContext.ContextRecord = ContextRecord;
                    ExceptionRecord->ExceptionFlags |= EXCEPTION_COLLIDED_UNWIND;
                }
                else if (Disposition != ExceptionContinueSearch)
                {
                    RtlRaiseStatus(STATUS_INVALID_DISPOSITION);
                }
            }
            while (Disposition == ExceptionCollidedUnwind);
        }

        if (HaveTarget && (EstablisherFrame == (ULONG)(ULONG_PTR)TargetFrame))
        {
            if (TargetIp && (ExceptionRecord->ExceptionCode != STATUS_UNWIND_CONSOLIDATE))
                ContextRecord->Pc = (ULONG)(ULONG_PTR)TargetIp;
            ContextRecord->R0 = (ULONG)(ULONG_PTR)ReturnValue;
            RtlRestoreContext(ContextRecord, ExceptionRecord);
        }

        *ContextRecord = UnwindContext;
    }
}

VOID
NTAPI
RtlUnwind(
    _In_opt_ PVOID TargetFrame,
    _In_opt_ PVOID TargetIp,
    _In_opt_ PEXCEPTION_RECORD ExceptionRecord,
    _In_ PVOID ReturnValue)
{
    CONTEXT ContextRecord;

    RtlCaptureContext(&ContextRecord);
    RtlUnwindEx(TargetFrame, TargetIp, ExceptionRecord, ReturnValue, &ContextRecord, NULL);
}

PRUNTIME_FUNCTION
NTAPI
RtlLookupFunctionTable(
    _In_ ULONG_PTR ControlPc,
    _Out_ PULONG_PTR ImageBase,
    _Out_ PULONG Length)
{
    PVOID BaseAddress;

    *Length = 0;
    if (!RtlPcToFileHeader((PVOID)ControlPc, &BaseAddress))
        return NULL;

    *ImageBase = (ULONG_PTR)BaseAddress;
    if (!RtlImageNtHeader(BaseAddress))
        return NULL;

    return RtlImageDirectoryEntryToData(BaseAddress, TRUE, IMAGE_DIRECTORY_ENTRY_EXCEPTION, Length);
}
