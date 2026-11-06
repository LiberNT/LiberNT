/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Unwind across a jump between the fragments of one function
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arifing193@gmail.com>
 */

#include "precomp.h"

#define PRIMARY_TO_FRAGMENT 0x15
#define PRIMARY_TAIL_CALL   0x24
#define FRAGMENT_OFFSET     0x40
#define FRAGMENT_TO_PRIMARY 0x50
#define FRAGMENT_RECURSION  0x64
#define OTHER_OFFSET        0x80
#define CODE_SIZE           0x90

#define DYNAMIC_CODE_RVA      0x1000
#define DYNAMIC_TABLE_RVA     0x2000
#define DYNAMIC_PRIMARY_INFO  0x3000
#define DYNAMIC_FRAGMENT_INFO 0x3040
#define DYNAMIC_OTHER_INFO    0x3080

#define SAVED_RBX   0xB0B0B0B0B0B0B0B0ULL
#define SAVED_RSI   0x5151515151515151ULL
#define RETURN_RIP  0x1111222233334444ULL
#define INITIAL_RSI 0x0123456789ABCDEFULL
#define LOCAL_VALUE 2

#ifdef __GNUC__
__asm__(
    ".section .text$RtlVirtualUnwindFragment,\"xr\"\n"
    ".p2align 6\n"
    ".globl UnwindFragmentCode\n"
    "UnwindFragmentCode:\n"
    "    .byte 0x53, 0x48, 0x83, 0xec, 0x20\n"
    "    .fill 0x15 - (. - UnwindFragmentCode), 1, 0xcc\n"
    "    .byte 0xe9\n"
    "    .long 0x40 - 0x1a\n"
    "    .fill 0x20 - (. - UnwindFragmentCode), 1, 0xcc\n"
    "    .byte 0x48, 0x83, 0xc4, 0x20, 0x5b, 0xe9\n"
    "    .long 0x80 - 0x2a\n"
    "    .fill 0x40 - (. - UnwindFragmentCode), 1, 0xcc\n"
    "    .byte 0x48, 0x89, 0x74, 0x24, 0x30\n"
    "    .fill 0x50 - (. - UnwindFragmentCode), 1, 0xcc\n"
    "    .byte 0xe9\n"
    "    .long 0x30 - 0x55\n"
    "    .fill 0x60 - (. - UnwindFragmentCode), 1, 0xcc\n"
    "    .byte 0x48, 0x83, 0xc4, 0x20, 0x5b, 0xe9\n"
    "    .long 0x00 - 0x6a\n"
    "    .fill 0x80 - (. - UnwindFragmentCode), 1, 0xcc\n"
    "    .byte 0xc3\n"
    "    .fill 0x90 - (. - UnwindFragmentCode), 1, 0xcc\n"
    ".section .rdata$RtlVirtualUnwindFragment,\"dr\"\n"
    ".p2align 2\n"
    "UnwindFragmentPrimaryInfo:\n"
    "    .byte 0x01, 0x05, 0x02, 0x00, 0x05, 0x32, 0x01, 0x30\n"
    "UnwindFragmentFragmentInfo:\n"
    "    .byte 0x21, 0x05, 0x02, 0x00, 0x05, 0x64, 0x06, 0x00\n"
    "    .rva UnwindFragmentCode\n"
    "    .rva UnwindFragmentCode + 0x40\n"
    "    .rva UnwindFragmentPrimaryInfo\n"
    "UnwindFragmentOtherInfo:\n"
    "    .byte 0x01, 0x00, 0x00, 0x00\n"
    ".section .pdata,\"dr\"\n"
    ".p2align 2\n"
    "    .rva UnwindFragmentCode\n"
    "    .rva UnwindFragmentCode + 0x40\n"
    "    .rva UnwindFragmentPrimaryInfo\n"
    "    .rva UnwindFragmentCode + 0x40\n"
    "    .rva UnwindFragmentCode + 0x80\n"
    "    .rva UnwindFragmentFragmentInfo\n"
    "    .rva UnwindFragmentCode + 0x80\n"
    "    .rva UnwindFragmentCode + 0x90\n"
    "    .rva UnwindFragmentOtherInfo\n"
    ".text\n");

extern UCHAR UnwindFragmentCode[];
#endif

static VOID
Unwind(PUCHAR Pc, ULONG64 *Stack, ULONG StackSlot, PCONTEXT Context, const char *Name)
{
    PRUNTIME_FUNCTION Entry;
    ULONG64 ImageBase = 0, EstablisherFrame = 0;
    PVOID HandlerData = NULL;

    RtlZeroMemory(Context, sizeof(*Context));
    Context->Rsp = (ULONG64)&Stack[StackSlot];
    Context->Rsi = INITIAL_RSI;
    Entry = RtlLookupFunctionEntry((ULONG64)Pc, &ImageBase, NULL);
    ok(Entry != NULL, "%s: no function entry\n", Name);
    if (Entry == NULL)
        return;
    RtlVirtualUnwind(UNW_FLAG_NHANDLER, ImageBase, (ULONG64)Pc, Entry, Context,
                     &HandlerData, &EstablisherFrame, NULL);
}

static VOID
CheckCaller(PUCHAR Pc, ULONG64 *Stack, ULONG StackSlot, ULONG64 Rsi, const char *Name)
{
    CONTEXT Context;

    Unwind(Pc, Stack, StackSlot, &Context, Name);
    ok(Context.Rip == RETURN_RIP, "%s: Rip %I64x\n", Name, Context.Rip);
    ok(Context.Rsp == (ULONG64)&Stack[6], "%s: Rsp %I64x, expected %p\n", Name, Context.Rsp, &Stack[6]);
    ok(Context.Rbx == SAVED_RBX, "%s: Rbx %I64x\n", Name, Context.Rbx);
    ok(Context.Rsi == Rsi, "%s: Rsi %I64x\n", Name, Context.Rsi);
}

static VOID
CheckTailCall(PUCHAR Pc, ULONG64 *Stack, const char *Name)
{
    CONTEXT Context;

    Unwind(Pc, Stack, 0, &Context, Name);
    ok(Context.Rip == LOCAL_VALUE, "%s: Rip %I64x\n", Name, Context.Rip);
    ok(Context.Rsp == (ULONG64)&Stack[1], "%s: Rsp %I64x, expected %p\n", Name, Context.Rsp, &Stack[1]);
}

static VOID
SetEntry(PRUNTIME_FUNCTION Entry, ULONG Begin, ULONG End, ULONG UnwindData)
{
    Entry->BeginAddress = Begin;
    Entry->EndAddress = End;
    Entry->UnwindData = UnwindData;
}

static VOID
TestDynamicTable(PUCHAR Code, ULONG64 *Stack)
{
    static const UCHAR PrimaryInfo[] = { 0x01, 0x05, 0x02, 0x00, 0x05, 0x32, 0x01, 0x30 };
    static const UCHAR FragmentInfo[] = { 0x21, 0x05, 0x02, 0x00, 0x05, 0x64, 0x06, 0x00 };
    static const UCHAR OtherInfo[] = { 0x01, 0x00, 0x00, 0x00 };
    PRUNTIME_FUNCTION Table;
    PUCHAR Base;

    Base = VirtualAlloc(NULL, 0x4000, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    ok(Base != NULL, "VirtualAlloc failed: %lu\n", GetLastError());
    if (Base == NULL)
        return;
    RtlCopyMemory(Base + DYNAMIC_CODE_RVA, Code, CODE_SIZE);
    Table = (PRUNTIME_FUNCTION)(Base + DYNAMIC_TABLE_RVA);
    SetEntry(&Table[0], DYNAMIC_CODE_RVA, DYNAMIC_CODE_RVA + FRAGMENT_OFFSET, DYNAMIC_PRIMARY_INFO);
    SetEntry(&Table[1], DYNAMIC_CODE_RVA + FRAGMENT_OFFSET, DYNAMIC_CODE_RVA + OTHER_OFFSET, DYNAMIC_FRAGMENT_INFO);
    SetEntry(&Table[2], DYNAMIC_CODE_RVA + OTHER_OFFSET, DYNAMIC_CODE_RVA + CODE_SIZE, DYNAMIC_OTHER_INFO);
    RtlCopyMemory(Base + DYNAMIC_PRIMARY_INFO, PrimaryInfo, sizeof(PrimaryInfo));
    RtlCopyMemory(Base + DYNAMIC_FRAGMENT_INFO, FragmentInfo, sizeof(FragmentInfo));
    RtlCopyMemory(Base + DYNAMIC_FRAGMENT_INFO + sizeof(FragmentInfo), &Table[0], sizeof(Table[0]));
    RtlCopyMemory(Base + DYNAMIC_OTHER_INFO, OtherInfo, sizeof(OtherInfo));
    ok(RtlAddFunctionTable(Table, 3, (ULONG64)Base), "RtlAddFunctionTable failed\n");

    Code = Base + DYNAMIC_CODE_RVA;
    CheckTailCall(Code + PRIMARY_TO_FRAGMENT, Stack, "dynamic primary to fragment");
    CheckTailCall(Code + FRAGMENT_TO_PRIMARY, Stack, "dynamic fragment to primary");
    CheckCaller(Code + PRIMARY_TAIL_CALL, Stack, 4, INITIAL_RSI, "dynamic tail call");
    CheckCaller(Code + FRAGMENT_RECURSION, Stack, 4, INITIAL_RSI, "dynamic recursive tail call");

    RtlDeleteFunctionTable(Table);
    VirtualFree(Base, 0, MEM_RELEASE);
}

START_TEST(RtlVirtualUnwindFragment)
{
    ULONG64 Stack[16] = { LOCAL_VALUE, LOCAL_VALUE, LOCAL_VALUE, LOCAL_VALUE, SAVED_RBX, RETURN_RIP, SAVED_RSI };
#ifdef __GNUC__
    PUCHAR Code = UnwindFragmentCode;

    CheckCaller(Code + PRIMARY_TO_FRAGMENT, Stack, 0, INITIAL_RSI, "image primary to fragment");
    CheckCaller(Code + FRAGMENT_TO_PRIMARY, Stack, 0, SAVED_RSI, "image fragment to primary");
    CheckCaller(Code + PRIMARY_TAIL_CALL, Stack, 4, INITIAL_RSI, "image tail call");
    CheckCaller(Code + FRAGMENT_RECURSION, Stack, 4, INITIAL_RSI, "image recursive tail call");
    TestDynamicTable(Code, Stack);
#else
    skip("the image function table of this test needs the GNU assembler syntax\n");
#endif
}
