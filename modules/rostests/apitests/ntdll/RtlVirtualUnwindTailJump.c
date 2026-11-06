/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Unwind from inside an epilog that ends with an indirect tail jump
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"

#define FUNCTION_SIZE 0x20
#define EPILOG_POP    0x09
#define TERMINATOR    0x0A
#define TABLE_RVA     0x1000
#define INFO_RVA      0x1800
#define CODE_RVA      0x2000

#define SAVED_RBX  0xB0B0B0B0B0B0B0B0ULL
#define RETURN_RIP 0x1111222233334444ULL
#define BODY_RBX   0xB1B1B1B1B1B1B1B1ULL
#define BODY_RIP   0x5555666677778888ULL

typedef struct _TAIL_JUMP
{
    const char *Name;
    UCHAR Bytes[8];
    ULONG Length;
    BOOLEAN Epilog;
} TAIL_JUMP;

static const TAIL_JUMP TailJumps[] =
{
    { "rex.w jmp rax",     { 0x48, 0xff, 0xe0 }, 3, TRUE },
    { "rex.wb jmp r11",    { 0x49, 0xff, 0xe3 }, 3, TRUE },
    { "rex.b jmp r11",     { 0x41, 0xff, 0xe3 }, 3, FALSE },
    { "rex jmp rax",       { 0x40, 0xff, 0xe0 }, 3, FALSE },
    { "jmp rax",           { 0xff, 0xe0 }, 2, FALSE },
    { "jmp [rip]",         { 0xff, 0x25, 0x00, 0x00, 0x00, 0x00 }, 6, TRUE },
    { "rex.w jmp [rip]",   { 0x48, 0xff, 0x25, 0x00, 0x00, 0x00, 0x00 }, 7, TRUE },
    { "jmp [rax]",         { 0xff, 0x20 }, 2, FALSE },
    { "jmp [rax+8]",       { 0xff, 0x60, 0x08 }, 3, FALSE },
    { "jmp [rax+disp32]",  { 0xff, 0xa0, 0x08, 0x00, 0x00, 0x00 }, 6, FALSE },
    { "rex.w jmp [rax]",   { 0x48, 0xff, 0x20 }, 3, TRUE },
    { "rex.w jmp [rsp]",   { 0x48, 0xff, 0x24, 0x24 }, 4, TRUE },
    { "rex.w jmp [rax+8]", { 0x48, 0xff, 0x60, 0x08 }, 4, TRUE },
    { "rex.w jmp [rax+disp32]", { 0x48, 0xff, 0xa0, 0x08, 0x00, 0x00, 0x00 }, 7, TRUE },
    { "rex.b jmp [r8]",    { 0x41, 0xff, 0x20 }, 3, FALSE },
    { "rex.b jmp [r8+8]",  { 0x41, 0xff, 0x60, 0x08 }, 4, FALSE },
    { "rex.w jmp far [rax]", { 0x48, 0xff, 0x28 }, 3, FALSE },
};

static const UCHAR FunctionBody[] =
{
    0x53,
    0x48, 0x83, 0xec, 0x20,
    0x48, 0x83, 0xc4, 0x20,
    0x5b,
};

static const UCHAR UnwindInfo[] = { 0x01, 0x05, 0x02, 0x00, 0x05, 0x32, 0x01, 0x30 };

static VOID
CheckTailJump(PUCHAR Code, const TAIL_JUMP *TailJump)
{
    ULONG64 Stack[8] = { SAVED_RBX, RETURN_RIP, 0, 0, BODY_RBX, BODY_RIP };
    PRUNTIME_FUNCTION Entry;
    ULONG64 ImageBase = 0, EstablisherFrame = 0;
    PVOID HandlerData = NULL;
    CONTEXT Context;

    RtlZeroMemory(&Context, sizeof(Context));
    Context.Rsp = (ULONG64)&Stack[0];
    Entry = RtlLookupFunctionEntry((ULONG64)(Code + EPILOG_POP), &ImageBase, NULL);
    ok(Entry != NULL, "%s: no function entry\n", TailJump->Name);
    if (Entry == NULL)
        return;

    RtlVirtualUnwind(UNW_FLAG_NHANDLER, ImageBase, (ULONG64)(Code + EPILOG_POP), Entry, &Context,
                     &HandlerData, &EstablisherFrame, NULL);

    if (TailJump->Epilog)
    {
        ok(Context.Rip == RETURN_RIP, "%s: Rip %I64x\n", TailJump->Name, Context.Rip);
        ok(Context.Rsp == (ULONG64)&Stack[2], "%s: Rsp %I64x, expected %p\n", TailJump->Name, Context.Rsp, &Stack[2]);
        ok(Context.Rbx == SAVED_RBX, "%s: Rbx %I64x\n", TailJump->Name, Context.Rbx);
    }
    else
    {
        ok(Context.Rip == BODY_RIP, "%s: Rip %I64x\n", TailJump->Name, Context.Rip);
        ok(Context.Rsp == (ULONG64)&Stack[6], "%s: Rsp %I64x, expected %p\n", TailJump->Name, Context.Rsp, &Stack[6]);
        ok(Context.Rbx == BODY_RBX, "%s: Rbx %I64x\n", TailJump->Name, Context.Rbx);
    }
}

START_TEST(RtlVirtualUnwindTailJump)
{
    PRUNTIME_FUNCTION Table;
    PUCHAR Base, Code;
    ULONG Index;

    Base = VirtualAlloc(NULL, 0x4000, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    ok(Base != NULL, "VirtualAlloc failed: %lu\n", GetLastError());
    if (Base == NULL)
        return;

    Table = (PRUNTIME_FUNCTION)(Base + TABLE_RVA);
    RtlCopyMemory(Base + INFO_RVA, UnwindInfo, sizeof(UnwindInfo));
    for (Index = 0; Index < ARRAYSIZE(TailJumps); Index++)
    {
        Code = Base + CODE_RVA + Index * FUNCTION_SIZE;
        RtlFillMemory(Code, FUNCTION_SIZE, 0xcc);
        RtlCopyMemory(Code, FunctionBody, sizeof(FunctionBody));
        RtlCopyMemory(Code + TERMINATOR, TailJumps[Index].Bytes, TailJumps[Index].Length);
        Table[Index].BeginAddress = CODE_RVA + Index * FUNCTION_SIZE;
        Table[Index].EndAddress = CODE_RVA + (Index + 1) * FUNCTION_SIZE;
        Table[Index].UnwindData = INFO_RVA;
    }
    ok(RtlAddFunctionTable(Table, ARRAYSIZE(TailJumps), (ULONG64)Base), "RtlAddFunctionTable failed\n");

    for (Index = 0; Index < ARRAYSIZE(TailJumps); Index++)
        CheckTailJump(Base + CODE_RVA + Index * FUNCTION_SIZE, &TailJumps[Index]);

    RtlDeleteFunctionTable(Table);
    VirtualFree(Base, 0, MEM_RELEASE);
}
