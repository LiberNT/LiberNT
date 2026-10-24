/*
 * PROJECT:     LiberNT Linux Subsystem
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     bpf() programs for the cgroup device controller
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "lxcore.h"

#define NDEBUG
#include <debug.h>

#define LX_BPF_PROG_LOAD            5
#define LX_BPF_PROG_ATTACH          8
#define LX_BPF_PROG_DETACH          9
#define LX_BPF_PROG_GET_NEXT_ID     11
#define LX_BPF_PROG_GET_FD_BY_ID    13
#define LX_BPF_OBJ_GET_INFO_BY_FD   15
#define LX_BPF_PROG_QUERY           16

#define LX_BPF_PROG_TYPE_CGROUP_DEVICE  15
#define LX_BPF_CGROUP_DEVICE            6

#define LX_BPF_F_ALLOW_OVERRIDE     0x1
#define LX_BPF_F_ALLOW_MULTI        0x2
#define LX_BPF_F_REPLACE            0x4
#define LX_BPF_F_QUERY_EFFECTIVE    0x1

#define LX_BPF_MAXINSNS             4096
#define LX_BPF_MAX_PROGS            64
#define LX_BPF_STACK_SIZE           512
#define LX_BPF_RUN_BUDGET           (1024 * 1024)

#define LX_BPF_CLASS(Code)          ((Code) & 0x07)
#define LX_BPF_SIZE(Code)           ((Code) & 0x18)
#define LX_BPF_MODE(Code)           ((Code) & 0xE0)
#define LX_BPF_OP(Code)             ((Code) & 0xF0)
#define LX_BPF_SRC(Code)            ((Code) & 0x08)

#define LX_BPF_LD       0x00
#define LX_BPF_LDX      0x01
#define LX_BPF_ST       0x02
#define LX_BPF_STX      0x03
#define LX_BPF_ALU      0x04
#define LX_BPF_JMP      0x05
#define LX_BPF_JMP32    0x06
#define LX_BPF_ALU64    0x07

#define LX_BPF_W        0x00
#define LX_BPF_H        0x08
#define LX_BPF_B        0x10
#define LX_BPF_DW       0x18

#define LX_BPF_IMM      0x00
#define LX_BPF_MEM      0x60

#define LX_BPF_ADD      0x00
#define LX_BPF_SUB      0x10
#define LX_BPF_MUL      0x20
#define LX_BPF_DIV      0x30
#define LX_BPF_OR       0x40
#define LX_BPF_AND      0x50
#define LX_BPF_LSH      0x60
#define LX_BPF_RSH      0x70
#define LX_BPF_NEG      0x80
#define LX_BPF_MOD      0x90
#define LX_BPF_XOR      0xA0
#define LX_BPF_MOV      0xB0
#define LX_BPF_ARSH     0xC0
#define LX_BPF_END      0xD0

#define LX_BPF_JA       0x00
#define LX_BPF_JEQ      0x10
#define LX_BPF_JGT      0x20
#define LX_BPF_JGE      0x30
#define LX_BPF_JSET     0x40
#define LX_BPF_JNE      0x50
#define LX_BPF_JSGT     0x60
#define LX_BPF_JSGE     0x70
#define LX_BPF_CALL     0x80
#define LX_BPF_EXIT     0x90
#define LX_BPF_JLT      0xA0
#define LX_BPF_JLE      0xB0
#define LX_BPF_JSLT     0xC0
#define LX_BPF_JSLE     0xD0

#define LX_BPF_K        0x00
#define LX_BPF_X        0x08

#pragma pack(push, 1)
typedef struct _LX_BPF_INSN
{
    UCHAR Code;
    UCHAR Regs;
    SHORT Off;
    LONG Imm;
} LX_BPF_INSN, *PLX_BPF_INSN;
#pragma pack(pop)

C_ASSERT(sizeof(LX_BPF_INSN) == 8);

typedef struct _LX_BPF_PROG
{
    volatile LONG RefCount;
    LIST_ENTRY Link;
    ULONG Id;
    ULONG Type;
    ULONG ExpectedAttachType;
    ULONG InsnCount;
    ULONG Uid;
    BOOLEAN Gpl;
    UCHAR Tag[8];
    CHAR Name[16];
    LONG64 LoadTime;
    PLX_BPF_INSN Insns;
} LX_BPF_PROG, *PLX_BPF_PROG;

typedef struct _LX_BPF_ATTACH
{
    LIST_ENTRY Link;
    PLX_BPF_PROG Prog;
} LX_BPF_ATTACH, *PLX_BPF_ATTACH;

typedef struct _LX_BPF_DEV_CTX
{
    ULONG AccessType;
    ULONG Major;
    ULONG Minor;
} LX_BPF_DEV_CTX, *PLX_BPF_DEV_CTX;

static LIST_ENTRY LxBpfProgs;
static FAST_MUTEX LxBpfLock;
static ULONG LxBpfNextId = 1;

VOID
LxBpfInitialize(VOID)
{
    InitializeListHead(&LxBpfProgs);
    ExInitializeFastMutex(&LxBpfLock);
}

static
VOID
LxBpfProgReference(
    _In_ PLX_BPF_PROG Prog)
{
    InterlockedIncrement(&Prog->RefCount);
}

static
VOID
LxBpfProgDereference(
    _In_ PLX_BPF_PROG Prog)
{
    if (InterlockedDecrement(&Prog->RefCount) != 0)
        return;
    ExAcquireFastMutex(&LxBpfLock);
    RemoveEntryList(&Prog->Link);
    ExReleaseFastMutex(&LxBpfLock);
    LxFree(Prog->Insns);
    LxFree(Prog);
}

static
VOID
LxBpfProgRelease(
    _In_ PLX_FILE File)
{
    if (File->Object != NULL)
        LxBpfProgDereference(File->Object);
    File->Object = NULL;
}

static const LX_FILE_OPS LxBpfProgOps =
{
    NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, LxBpfProgRelease
};

static
PLX_BPF_PROG
LxBpfProgFromFd(
    _In_ PLX_PROCESS Process,
    _In_ LONG Fd)
{
    PLX_FILE File = LxFdGet(Process, Fd);
    PLX_BPF_PROG Prog = NULL;

    if (File == NULL)
        return NULL;
    if (File->Ops == &LxBpfProgOps)
    {
        Prog = File->Object;
        LxBpfProgReference(Prog);
    }
    LxFileDereference(File);
    return Prog;
}

static
LONG
LxBpfProgInstall(
    _In_ PLX_PROCESS Process,
    _In_ PLX_BPF_PROG Prog)
{
    PLX_FILE File = LxFileAlloc(&LxBpfProgOps, LX_O_RDWR, 0);
    LONG Fd;

    if (File == NULL)
        return -LX_ENOMEM;
    File->Path = LxStrDup("anon_inode:bpf-prog");
    LxBpfProgReference(Prog);
    File->Object = Prog;
    Fd = LxFdInstall(Process, File, 0, TRUE);
    LxFileDereference(File);
    return Fd;
}

static
BOOLEAN
LxBpfLicenseIsGpl(
    _In_ PCSTR License)
{
    static const PCSTR Gpl[] =
    {
        "GPL", "GPL v2", "GPL and additional rights", "Dual BSD/GPL", "Dual MIT/GPL", "Dual MPL/GPL"
    };
    ULONG Index;

    for (Index = 0; Index < RTL_NUMBER_OF(Gpl); Index++)
    {
        if (strcmp(License, Gpl[Index]) == 0)
            return TRUE;
    }
    return FALSE;
}

static
LONG
LxBpfCheck(
    _In_reads_(Count) PLX_BPF_INSN Insns,
    _In_ ULONG Count)
{
    ULONG Index;

    for (Index = 0; Index < Count; Index++)
    {
        PLX_BPF_INSN Insn = &Insns[Index];
        UCHAR Class = LX_BPF_CLASS(Insn->Code), Op = LX_BPF_OP(Insn->Code);
        UCHAR Dst = Insn->Regs & 0x0F, Src = Insn->Regs >> 4;

        if ((Dst > 10) || (Src > 10))
            return -LX_EINVAL;
        switch (Class)
        {
            case LX_BPF_ALU:
            case LX_BPF_ALU64:
                if (Dst == 10)
                    return -LX_EACCES;
                if ((Op == LX_BPF_END) && (Insn->Imm != 16) && (Insn->Imm != 32) && (Insn->Imm != 64))
                    return -LX_EINVAL;
                if ((Op > LX_BPF_END) || ((Op == LX_BPF_NEG) && LX_BPF_SRC(Insn->Code)))
                    return -LX_EINVAL;
                if (((Op == LX_BPF_DIV) || (Op == LX_BPF_MOD)) && !LX_BPF_SRC(Insn->Code) && (Insn->Imm == 0))
                    return -LX_EINVAL;
                break;

            case LX_BPF_JMP:
            case LX_BPF_JMP32:
                if (Op == LX_BPF_EXIT)
                    break;
                if (Op == LX_BPF_CALL)
                    return -LX_EINVAL;
                if (Op > LX_BPF_JSLE)
                    return -LX_EINVAL;
                if (((LONG)Index + 1 + Insn->Off < 0) || ((LONG)Index + 1 + Insn->Off >= (LONG)Count))
                    return -LX_EINVAL;
                break;

            case LX_BPF_LDX:
                if ((LX_BPF_MODE(Insn->Code) != LX_BPF_MEM) || (Dst == 10))
                    return -LX_EINVAL;
                break;

            case LX_BPF_ST:
            case LX_BPF_STX:
                if (LX_BPF_MODE(Insn->Code) != LX_BPF_MEM)
                    return -LX_EINVAL;
                break;

            case LX_BPF_LD:
                if ((Insn->Code != (LX_BPF_LD | LX_BPF_IMM | LX_BPF_DW)) || (Src != 0) || (Dst == 10) ||
                    (Index + 1 >= Count) || (Insns[Index + 1].Code != 0) || (Insns[Index + 1].Regs != 0) ||
                    (Insns[Index + 1].Off != 0))
                {
                    return -LX_EINVAL;
                }
                Index++;
                break;

            default:
                return -LX_EINVAL;
        }
    }
    if ((Count == 0) || (Insns[Count - 1].Code != (LX_BPF_JMP | LX_BPF_EXIT)))
    {
        if ((Count == 0) || (LX_BPF_CLASS(Insns[Count - 1].Code) != LX_BPF_JMP) ||
            (LX_BPF_OP(Insns[Count - 1].Code) != LX_BPF_JA))
        {
            return -LX_EINVAL;
        }
    }
    return 0;
}

static
BOOLEAN
LxBpfAccess(
    _In_ ULONG64 Address,
    _In_ ULONG Size,
    _In_ BOOLEAN Write,
    _In_ PLX_BPF_DEV_CTX Ctx,
    _In_ PUCHAR Stack)
{
    ULONG64 CtxStart = (ULONG64)(ULONG_PTR)Ctx, StackStart = (ULONG64)(ULONG_PTR)Stack;

    if (!Write && (Address >= CtxStart) && (Address + Size <= CtxStart + sizeof(*Ctx)) && (Address + Size > Address))
    {
        ULONG Offset = (ULONG)(Address - CtxStart);

        if (Offset % Size)
            return FALSE;
        return (Size == sizeof(ULONG)) || (Offset < sizeof(ULONG));
    }
    return (Address >= StackStart) && (Address + Size <= StackStart + LX_BPF_STACK_SIZE) && (Address + Size > Address) &&
           ((Address & (Size - 1)) == 0);
}

static
ULONG64
LxBpfSwap(
    _In_ ULONG64 Value,
    _In_ LONG Bits)
{
    if (Bits == 16)
        return RtlUshortByteSwap((USHORT)Value);
    if (Bits == 32)
        return RtlUlongByteSwap((ULONG)Value);
    return RtlUlonglongByteSwap(Value);
}

static
BOOLEAN
LxBpfRun(
    _In_ PLX_BPF_PROG Prog,
    _In_ PLX_BPF_DEV_CTX Ctx,
    _Out_ PULONG64 Result)
{
    ULONG64 Regs[11];
    DECLSPEC_ALIGN(8) UCHAR Stack[LX_BPF_STACK_SIZE];
    ULONG Pc = 0, Budget = LX_BPF_RUN_BUDGET;

    RtlZeroMemory(Regs, sizeof(Regs));
    RtlZeroMemory(Stack, sizeof(Stack));
    Regs[1] = (ULONG64)(ULONG_PTR)Ctx;
    Regs[10] = (ULONG64)(ULONG_PTR)(Stack + LX_BPF_STACK_SIZE);
    *Result = 0;

    while ((Pc < Prog->InsnCount) && (Budget-- != 0))
    {
        PLX_BPF_INSN Insn = &Prog->Insns[Pc++];
        UCHAR Class = LX_BPF_CLASS(Insn->Code), Op = LX_BPF_OP(Insn->Code);
        UCHAR Dst = Insn->Regs & 0x0F, Src = Insn->Regs >> 4;
        ULONG64 Operand = LX_BPF_SRC(Insn->Code) ? Regs[Src] : (ULONG64)(LONG64)Insn->Imm;

        switch (Class)
        {
            case LX_BPF_ALU64:
            case LX_BPF_ALU:
            {
                BOOLEAN Wide = (Class == LX_BPF_ALU64);
                ULONG64 Value = Wide ? Regs[Dst] : (ULONG)Regs[Dst];
                ULONG64 Right = Wide ? Operand : (ULONG)Operand;
                ULONG Shift = (ULONG)(Right & (Wide ? 63 : 31));

                switch (Op)
                {
                    case LX_BPF_ADD: Value += Right; break;
                    case LX_BPF_SUB: Value -= Right; break;
                    case LX_BPF_MUL: Value *= Right; break;
                    case LX_BPF_DIV: Value = (Right != 0) ? (Value / Right) : 0; break;
                    case LX_BPF_MOD: Value = (Right != 0) ? (Value % Right) : Value; break;
                    case LX_BPF_OR: Value |= Right; break;
                    case LX_BPF_AND: Value &= Right; break;
                    case LX_BPF_XOR: Value ^= Right; break;
                    case LX_BPF_LSH: Value <<= Shift; break;
                    case LX_BPF_RSH: Value >>= Shift; break;
                    case LX_BPF_NEG: Value = (ULONG64)(-(LONG64)Value); break;
                    case LX_BPF_MOV: Value = Right; break;
                    case LX_BPF_ARSH:
                        Value = Wide ? (ULONG64)((LONG64)Value >> Shift) : (ULONG)((LONG)(ULONG)Value >> Shift);
                        break;
                    case LX_BPF_END:
                        if (LX_BPF_SRC(Insn->Code))
                            Value = LxBpfSwap(Regs[Dst], Insn->Imm);
                        else
                            Value = Regs[Dst];
                        if (Insn->Imm == 16)
                            Value &= 0xFFFF;
                        else if (Insn->Imm == 32)
                            Value &= 0xFFFFFFFF;
                        Wide = TRUE;
                        break;
                    default:
                        return FALSE;
                }
                Regs[Dst] = Wide ? Value : (ULONG)Value;
                break;
            }

            case LX_BPF_JMP:
            case LX_BPF_JMP32:
            {
                BOOLEAN Wide = (Class == LX_BPF_JMP), Taken;
                ULONG64 Left = Wide ? Regs[Dst] : (ULONG)Regs[Dst];
                ULONG64 Right = Wide ? Operand : (ULONG)Operand;
                LONG64 SignedLeft = Wide ? (LONG64)Left : (LONG)(ULONG)Left;
                LONG64 SignedRight = Wide ? (LONG64)Right : (LONG)(ULONG)Right;

                if (Op == LX_BPF_EXIT)
                {
                    *Result = Regs[0];
                    return TRUE;
                }
                switch (Op)
                {
                    case LX_BPF_JA: Taken = TRUE; break;
                    case LX_BPF_JEQ: Taken = (Left == Right); break;
                    case LX_BPF_JNE: Taken = (Left != Right); break;
                    case LX_BPF_JGT: Taken = (Left > Right); break;
                    case LX_BPF_JGE: Taken = (Left >= Right); break;
                    case LX_BPF_JLT: Taken = (Left < Right); break;
                    case LX_BPF_JLE: Taken = (Left <= Right); break;
                    case LX_BPF_JSET: Taken = ((Left & Right) != 0); break;
                    case LX_BPF_JSGT: Taken = (SignedLeft > SignedRight); break;
                    case LX_BPF_JSGE: Taken = (SignedLeft >= SignedRight); break;
                    case LX_BPF_JSLT: Taken = (SignedLeft < SignedRight); break;
                    case LX_BPF_JSLE: Taken = (SignedLeft <= SignedRight); break;
                    default: return FALSE;
                }
                if (Taken)
                {
                    LONG Target = (LONG)Pc + (((Op == LX_BPF_JA) && (Class == LX_BPF_JMP32)) ? Insn->Imm : Insn->Off);

                    if ((Target < 0) || ((ULONG)Target >= Prog->InsnCount))
                        return FALSE;
                    Pc = (ULONG)Target;
                }
                break;
            }

            case LX_BPF_LDX:
            case LX_BPF_ST:
            case LX_BPF_STX:
            {
                ULONG Size = (LX_BPF_SIZE(Insn->Code) == LX_BPF_W) ? 4 : (LX_BPF_SIZE(Insn->Code) == LX_BPF_H) ? 2 :
                             (LX_BPF_SIZE(Insn->Code) == LX_BPF_B) ? 1 : 8;
                ULONG64 Address = ((Class == LX_BPF_LDX) ? Regs[Src] : Regs[Dst]) + (LONG64)Insn->Off;
                PVOID Pointer = (PVOID)(ULONG_PTR)Address;

                if (!LxBpfAccess(Address, Size, Class != LX_BPF_LDX, Ctx, Stack))
                    return FALSE;
                if (Class == LX_BPF_LDX)
                {
                    Regs[Dst] = (Size == 1) ? *(PUCHAR)Pointer : (Size == 2) ? *(PUSHORT)Pointer :
                                (Size == 4) ? *(PULONG)Pointer : *(PULONG64)Pointer;
                }
                else
                {
                    ULONG64 Value = (Class == LX_BPF_STX) ? Regs[Src] : (ULONG64)(LONG64)Insn->Imm;

                    if (Size == 1)
                        *(PUCHAR)Pointer = (UCHAR)Value;
                    else if (Size == 2)
                        *(PUSHORT)Pointer = (USHORT)Value;
                    else if (Size == 4)
                        *(PULONG)Pointer = (ULONG)Value;
                    else
                        *(PULONG64)Pointer = Value;
                }
                break;
            }

            case LX_BPF_LD:
                Regs[Dst] = (ULONG)Insn->Imm | ((ULONG64)(ULONG)Prog->Insns[Pc].Imm << 32);
                Pc++;
                break;

            default:
                return FALSE;
        }
    }
    return FALSE;
}

static
LONG64
LxBpfProgLoad(
    _In_ PLX_CALL Call,
    _In_reads_bytes_(Size) PUCHAR Attr,
    _In_ ULONG Size)
{
    ULONG Type, Count, LogSize, ExpectedAttach = 0;
    ULONG64 InsnsPointer, LicensePointer;
    PLX_BPF_PROG Prog;
    PSTR License = NULL;
    LONG64 Result;

    if (Size < 48)
        return -LX_EINVAL;
    RtlCopyMemory(&Type, Attr + 0, 4);
    RtlCopyMemory(&Count, Attr + 4, 4);
    RtlCopyMemory(&InsnsPointer, Attr + 8, 8);
    RtlCopyMemory(&LicensePointer, Attr + 16, 8);
    RtlCopyMemory(&LogSize, Attr + 28, 4);
    if (Size >= 72)
        RtlCopyMemory(&ExpectedAttach, Attr + 68, 4);
    UNREFERENCED_PARAMETER(LogSize);

    if (Type != LX_BPF_PROG_TYPE_CGROUP_DEVICE)
        return -LX_EINVAL;
    if ((!LxCapable(Call->Process, LX_CAP_BPF) && !LxCapable(Call->Process, LX_CAP_SYS_ADMIN)) ||
        (!LxCapable(Call->Process, LX_CAP_NET_ADMIN) && !LxCapable(Call->Process, LX_CAP_SYS_ADMIN)))
    {
        return -LX_EPERM;
    }
    if ((Count == 0) || (Count > LX_BPF_MAXINSNS))
        return -LX_E2BIG;

    Prog = LxAlloc(sizeof(*Prog));
    if (Prog == NULL)
        return -LX_ENOMEM;
    Prog->Insns = LxAlloc((SIZE_T)Count * sizeof(LX_BPF_INSN));
    if (Prog->Insns == NULL)
    {
        LxFree(Prog);
        return -LX_ENOMEM;
    }
    Result = LxCopyFromUser(Prog->Insns, InsnsPointer, (SIZE_T)Count * sizeof(LX_BPF_INSN));
    if ((Result == 0) && (LicensePointer != 0))
        Result = LxCopyStringFromUser(LicensePointer, &License, 127);
    if (Result == 0)
        Result = LxBpfCheck(Prog->Insns, Count);
    if (Result != 0)
    {
        LxFree(License);
        LxFree(Prog->Insns);
        LxFree(Prog);
        return Result;
    }

    Prog->RefCount = 1;
    Prog->Type = Type;
    Prog->ExpectedAttachType = ExpectedAttach;
    Prog->InsnCount = Count;
    Prog->Uid = Call->Process->Uid[1];
    Prog->Gpl = (License != NULL) && LxBpfLicenseIsGpl(License);
    Prog->LoadTime = (LONG64)KeQueryInterruptTime() * 100;
    if (Size >= 64)
    {
        RtlCopyMemory(Prog->Name, Attr + 48, 16);
        Prog->Name[15] = '\0';
    }
    LxFree(License);
    {
        UCHAR Digest[20];
        BCRYPT_ALG_HANDLE Algorithm;
        BCRYPT_HASH_HANDLE Hash;

        if (NT_SUCCESS(BCryptOpenAlgorithmProvider(&Algorithm, BCRYPT_SHA1_ALGORITHM, NULL, 0)))
        {
            if (NT_SUCCESS(BCryptCreateHash(Algorithm, &Hash, NULL, 0, NULL, 0, 0)))
            {
                if (NT_SUCCESS(BCryptHashData(Hash, (PUCHAR)Prog->Insns, Count * sizeof(LX_BPF_INSN), 0)) &&
                    NT_SUCCESS(BCryptFinishHash(Hash, Digest, sizeof(Digest), 0)))
                {
                    RtlCopyMemory(Prog->Tag, Digest, sizeof(Prog->Tag));
                }
                BCryptDestroyHash(Hash);
            }
            BCryptCloseAlgorithmProvider(Algorithm, 0);
        }
    }

    ExAcquireFastMutex(&LxBpfLock);
    Prog->Id = LxBpfNextId++;
    InsertTailList(&LxBpfProgs, &Prog->Link);
    ExReleaseFastMutex(&LxBpfLock);

    Result = LxBpfProgInstall(Call->Process, Prog);
    LxBpfProgDereference(Prog);
    return Result;
}

static
PLX_CGROUP
LxBpfCgroupFromFd(
    _In_ PLX_PROCESS Process,
    _In_ LONG Fd)
{
    PLX_FILE File = LxFdGet(Process, Fd);
    PLX_CGROUP Cgroup;

    if (File == NULL)
        return NULL;
    Cgroup = LxCgroupFromFile(File);
    LxFileDereference(File);
    return Cgroup;
}

static
ULONG
LxBpfListCount(
    _In_ PLIST_ENTRY Head)
{
    PLIST_ENTRY Entry;
    ULONG Count = 0;

    for (Entry = Head->Flink; Entry != Head; Entry = Entry->Flink)
        Count++;
    return Count;
}

static
LONG64
LxBpfAttach(
    _In_ PLX_CALL Call,
    _In_reads_bytes_(Size) PUCHAR Attr,
    _In_ ULONG Size,
    _In_ BOOLEAN Detach)
{
    ULONG TargetFd = 0, ProgFd = 0, AttachType = 0, Flags = 0, ReplaceFd = 0;
    PLX_BPF_PROG Prog = NULL, Replace = NULL;
    PLX_BPF_ATTACH Attach = NULL;
    PLX_CGROUP Cgroup;
    PLIST_ENTRY Entry;
    LONG64 Result = 0;

    if (Size < 16)
        return -LX_EINVAL;
    RtlCopyMemory(&TargetFd, Attr + 0, 4);
    RtlCopyMemory(&ProgFd, Attr + 4, 4);
    RtlCopyMemory(&AttachType, Attr + 8, 4);
    RtlCopyMemory(&Flags, Attr + 12, 4);
    if (Size >= 20)
        RtlCopyMemory(&ReplaceFd, Attr + 16, 4);

    if (AttachType != LX_BPF_CGROUP_DEVICE)
        return -LX_EINVAL;
    if (!Detach)
    {
        if (Flags & ~(LX_BPF_F_ALLOW_OVERRIDE | LX_BPF_F_ALLOW_MULTI | LX_BPF_F_REPLACE))
            return -LX_EINVAL;
        if (((Flags & LX_BPF_F_ALLOW_OVERRIDE) && (Flags & LX_BPF_F_ALLOW_MULTI)) ||
            ((Flags & LX_BPF_F_REPLACE) && !(Flags & LX_BPF_F_ALLOW_MULTI)))
        {
            return -LX_EINVAL;
        }
    }
    else if (Flags != 0)
    {
        return -LX_EINVAL;
    }

    if (!Detach || (ProgFd != 0))
    {
        Prog = LxBpfProgFromFd(Call->Process, (LONG)ProgFd);
        if (Prog == NULL)
            return Detach ? -LX_ENOENT : -LX_EBADF;
        if (Prog->Type != LX_BPF_PROG_TYPE_CGROUP_DEVICE)
        {
            LxBpfProgDereference(Prog);
            return -LX_EINVAL;
        }
    }
    Cgroup = LxBpfCgroupFromFd(Call->Process, (LONG)TargetFd);
    if (Cgroup == NULL)
    {
        if (Prog != NULL)
            LxBpfProgDereference(Prog);
        return -LX_EBADF;
    }
    if (!Detach && (Flags & LX_BPF_F_REPLACE))
    {
        Replace = LxBpfProgFromFd(Call->Process, (LONG)ReplaceFd);
        if (Replace == NULL)
        {
            LxBpfProgDereference(Prog);
            return -LX_EBADF;
        }
    }
    if (!Detach)
    {
        Attach = LxAlloc(sizeof(*Attach));
        if (Attach == NULL)
        {
            if (Replace != NULL)
                LxBpfProgDereference(Replace);
            LxBpfProgDereference(Prog);
            return -LX_ENOMEM;
        }
    }

    ExAcquireFastMutex(&LxBpfLock);
    if (Detach)
    {
        Result = -LX_ENOENT;
        for (Entry = Cgroup->BpfDevice.Flink; Entry != &Cgroup->BpfDevice; Entry = Entry->Flink)
        {
            PLX_BPF_ATTACH Item = CONTAINING_RECORD(Entry, LX_BPF_ATTACH, Link);

            if ((Prog == NULL) || (Item->Prog == Prog))
            {
                if ((Prog == NULL) && (Cgroup->BpfDeviceFlags & LX_BPF_F_ALLOW_MULTI))
                    break;
                RemoveEntryList(&Item->Link);
                Attach = Item;
                Result = 0;
                break;
            }
        }
        if (IsListEmpty(&Cgroup->BpfDevice))
            Cgroup->BpfDeviceFlags = 0;
    }
    else if (!IsListEmpty(&Cgroup->BpfDevice) && (Cgroup->BpfDeviceFlags != (Flags & ~LX_BPF_F_REPLACE)))
    {
        Result = -LX_EPERM;
    }
    else if (Flags & LX_BPF_F_ALLOW_MULTI)
    {
        PLX_BPF_ATTACH Old = NULL;

        for (Entry = Cgroup->BpfDevice.Flink; Entry != &Cgroup->BpfDevice; Entry = Entry->Flink)
        {
            PLX_BPF_ATTACH Item = CONTAINING_RECORD(Entry, LX_BPF_ATTACH, Link);

            if (Item->Prog == Prog)
                Result = -LX_EEXIST;
            if ((Replace != NULL) && (Item->Prog == Replace))
                Old = Item;
        }
        if ((Result == 0) && (Replace != NULL) && (Old == NULL))
            Result = -LX_ENOENT;
        if ((Result == 0) && (Old == NULL) && (LxBpfListCount(&Cgroup->BpfDevice) >= LX_BPF_MAX_PROGS))
            Result = -LX_E2BIG;
        if (Result == 0)
        {
            Attach->Prog = Prog;
            Prog = NULL;
            if (Old != NULL)
            {
                InsertHeadList(&Old->Link, &Attach->Link);
                RemoveEntryList(&Old->Link);
                Attach = Old;
            }
            else
            {
                InsertTailList(&Cgroup->BpfDevice, &Attach->Link);
                Attach = NULL;
            }
            Cgroup->BpfDeviceFlags = Flags & ~LX_BPF_F_REPLACE;
        }
    }
    else
    {
        PLX_BPF_ATTACH Old = IsListEmpty(&Cgroup->BpfDevice) ? NULL :
                             CONTAINING_RECORD(Cgroup->BpfDevice.Flink, LX_BPF_ATTACH, Link);

        Attach->Prog = Prog;
        Prog = NULL;
        if (Old != NULL)
            RemoveEntryList(&Old->Link);
        InsertTailList(&Cgroup->BpfDevice, &Attach->Link);
        Attach = Old;
        Cgroup->BpfDeviceFlags = Flags;
    }
    ExReleaseFastMutex(&LxBpfLock);

    if (Attach != NULL)
    {
        if (Attach->Prog != NULL)
            LxBpfProgDereference(Attach->Prog);
        LxFree(Attach);
    }
    if (Prog != NULL)
        LxBpfProgDereference(Prog);
    if (Replace != NULL)
        LxBpfProgDereference(Replace);
    return Result;
}

static
LONG64
LxBpfQuery(
    _In_ PLX_CALL Call,
    _In_ ULONG64 AttrPointer,
    _In_reads_bytes_(Size) PUCHAR Attr,
    _In_ ULONG Size)
{
    ULONG TargetFd, AttachType, QueryFlags, Capacity, Count = 0, Written = 0, Flags;
    ULONG64 Ids;
    PLX_CGROUP Cgroup, Walk;
    ULONG Buffer[LX_BPF_MAX_PROGS * 2];
    LONG64 Result = 0;

    if (Size < 28)
        return -LX_EINVAL;
    RtlCopyMemory(&TargetFd, Attr + 0, 4);
    RtlCopyMemory(&AttachType, Attr + 4, 4);
    RtlCopyMemory(&QueryFlags, Attr + 8, 4);
    RtlCopyMemory(&Ids, Attr + 16, 8);
    RtlCopyMemory(&Capacity, Attr + 24, 4);
    if (AttachType != LX_BPF_CGROUP_DEVICE)
        return -LX_EINVAL;
    if (QueryFlags & ~LX_BPF_F_QUERY_EFFECTIVE)
        return -LX_EINVAL;
    if (!LxCapable(Call->Process, LX_CAP_NET_ADMIN) && !LxCapable(Call->Process, LX_CAP_SYS_ADMIN))
        return -LX_EPERM;
    Cgroup = LxBpfCgroupFromFd(Call->Process, (LONG)TargetFd);
    if (Cgroup == NULL)
        return -LX_EBADF;

    ExAcquireFastMutex(&LxBpfLock);
    Flags = (QueryFlags & LX_BPF_F_QUERY_EFFECTIVE) ? 0 : Cgroup->BpfDeviceFlags;
    for (Walk = Cgroup; Walk != NULL; Walk = Walk->Parent)
    {
        PLIST_ENTRY Entry;

        if ((Walk != Cgroup) && !(QueryFlags & LX_BPF_F_QUERY_EFFECTIVE))
            break;
        if ((Walk != Cgroup) && (Count != 0) && !(Walk->BpfDeviceFlags & LX_BPF_F_ALLOW_MULTI))
            continue;
        for (Entry = Walk->BpfDevice.Flink; Entry != &Walk->BpfDevice; Entry = Entry->Flink)
        {
            if (Count < RTL_NUMBER_OF(Buffer))
                Buffer[Count] = CONTAINING_RECORD(Entry, LX_BPF_ATTACH, Link)->Prog->Id;
            Count++;
        }
    }
    ExReleaseFastMutex(&LxBpfLock);

    Result = LxCopyToUser(AttrPointer + 12, &Flags, sizeof(Flags));
    if ((Result == 0) && (Ids != 0) && (Capacity != 0))
    {
        Written = min(min(Capacity, Count), (ULONG)RTL_NUMBER_OF(Buffer));
        Result = LxCopyToUser(Ids, Buffer, Written * sizeof(ULONG));
        if ((Result == 0) && (Capacity < Count))
            Result = -LX_ENOSPC;
    }
    if ((Result == 0) || (Result == -LX_ENOSPC))
    {
        LONG64 Copy = LxCopyToUser(AttrPointer + 24, &Count, sizeof(Count));

        if (Copy != 0)
            Result = Copy;
    }
    return Result;
}

static
PLX_BPF_PROG
LxBpfFindById(
    _In_ ULONG Id,
    _In_ BOOLEAN Next)
{
    PLIST_ENTRY Entry;
    PLX_BPF_PROG Found = NULL;

    ExAcquireFastMutex(&LxBpfLock);
    for (Entry = LxBpfProgs.Flink; Entry != &LxBpfProgs; Entry = Entry->Flink)
    {
        PLX_BPF_PROG Prog = CONTAINING_RECORD(Entry, LX_BPF_PROG, Link);

        if (Prog->RefCount == 0)
            continue;
        if (Next ? ((Prog->Id > Id) && ((Found == NULL) || (Prog->Id < Found->Id))) : (Prog->Id == Id))
            Found = Prog;
    }
    if (Found != NULL)
        LxBpfProgReference(Found);
    ExReleaseFastMutex(&LxBpfLock);
    return Found;
}

static
LONG64
LxBpfGetInfo(
    _In_ PLX_CALL Call,
    _In_ ULONG64 AttrPointer,
    _In_reads_bytes_(Size) PUCHAR Attr,
    _In_ ULONG Size)
{
    UCHAR Info[232];
    ULONG Fd, Length, Value, Have = 0;
    ULONG64 InfoPointer, Insns = 0;
    PLX_BPF_PROG Prog;
    LONG64 Result;

    if (Size < 16)
        return -LX_EINVAL;
    RtlCopyMemory(&Fd, Attr + 0, 4);
    RtlCopyMemory(&Length, Attr + 4, 4);
    RtlCopyMemory(&InfoPointer, Attr + 8, 8);
    Prog = LxBpfProgFromFd(Call->Process, (LONG)Fd);
    if (Prog == NULL)
        return -LX_EINVAL;

    Length = min(Length, (ULONG)sizeof(Info));
    RtlZeroMemory(Info, sizeof(Info));
    Result = LxCopyFromUser(Info, InfoPointer, Length);
    if (Result == 0)
    {
        if (Length >= 24)
            RtlCopyMemory(&Have, Info + 20, 4);
        if (Length >= 40)
            RtlCopyMemory(&Insns, Info + 32, 8);
        RtlZeroMemory(Info, sizeof(Info));
        RtlCopyMemory(Info + 0, &Prog->Type, 4);
        RtlCopyMemory(Info + 4, &Prog->Id, 4);
        RtlCopyMemory(Info + 8, Prog->Tag, 8);
        Value = Prog->InsnCount * sizeof(LX_BPF_INSN);
        RtlCopyMemory(Info + 20, &Value, 4);
        RtlCopyMemory(Info + 32, &Insns, 8);
        RtlCopyMemory(Info + 40, &Prog->LoadTime, 8);
        RtlCopyMemory(Info + 48, &Prog->Uid, 4);
        RtlCopyMemory(Info + 64, Prog->Name, 16);
        Value = Prog->Gpl ? 1 : 0;
        RtlCopyMemory(Info + 84, &Value, 4);
        if ((Insns != 0) && (Have != 0))
            Result = LxCopyToUser(Insns, Prog->Insns, min(Have, Prog->InsnCount * (ULONG)sizeof(LX_BPF_INSN)));
    }
    if (Result == 0)
        Result = LxCopyToUser(InfoPointer, Info, Length);
    if (Result == 0)
        Result = LxCopyToUser(AttrPointer + 4, &Length, sizeof(Length));
    LxBpfProgDereference(Prog);
    return Result;
}

LX_SYSCALL(LxSysBpf)
{
    ULONG Command = (ULONG)Call->Args[0];
    ULONG Size = (ULONG)min(Call->Args[2], 256);
    UCHAR Attr[256];
    PLX_BPF_PROG Prog;
    LONG64 Result;
    ULONG Id;

    RtlZeroMemory(Attr, sizeof(Attr));
    if ((Call->Args[2] > 4096) || ((Size != 0) && (LxCopyFromUser(Attr, Call->Args[1], Size) != 0)))
        return (Call->Args[2] > 4096) ? -LX_E2BIG : -LX_EFAULT;

    switch (Command)
    {
        case LX_BPF_PROG_LOAD:
            return LxBpfProgLoad(Call, Attr, Size);

        case LX_BPF_PROG_ATTACH:
            return LxBpfAttach(Call, Attr, Size, FALSE);

        case LX_BPF_PROG_DETACH:
            return LxBpfAttach(Call, Attr, Size, TRUE);

        case LX_BPF_PROG_QUERY:
            return LxBpfQuery(Call, Call->Args[1], Attr, Size);

        case LX_BPF_OBJ_GET_INFO_BY_FD:
            return LxBpfGetInfo(Call, Call->Args[1], Attr, Size);

        case LX_BPF_PROG_GET_NEXT_ID:
        case LX_BPF_PROG_GET_FD_BY_ID:
            if (!LxCapable(Call->Process, LX_CAP_SYS_ADMIN))
                return -LX_EPERM;
            RtlCopyMemory(&Id, Attr, 4);
            Prog = LxBpfFindById(Id, Command == LX_BPF_PROG_GET_NEXT_ID);
            if (Prog == NULL)
                return -LX_ENOENT;
            if (Command == LX_BPF_PROG_GET_NEXT_ID)
                Result = LxCopyToUser(Call->Args[1] + 4, &Prog->Id, sizeof(ULONG));
            else
                Result = LxBpfProgInstall(Call->Process, Prog);
            LxBpfProgDereference(Prog);
            return Result;

        default:
            return -LX_EINVAL;
    }
}

LONG
LxBpfDeviceCheck(
    _In_ PLX_PROCESS Process,
    _In_ BOOLEAN Block,
    _In_ ULONG Major,
    _In_ ULONG Minor,
    _In_ ULONG Access)
{
    PLX_BPF_PROG Progs[LX_BPF_MAX_PROGS];
    LX_BPF_DEV_CTX Ctx;
    PLX_CGROUP Walk;
    ULONG Count = 0, Index;
    LONG Error = 0;

    if ((Process->Cgroup == NULL) || (Access == 0))
        return 0;

    ExAcquireFastMutex(&LxBpfLock);
    for (Walk = Process->Cgroup; (Walk != NULL) && (Count < RTL_NUMBER_OF(Progs)); Walk = Walk->Parent)
    {
        PLIST_ENTRY Entry;

        if ((Count != 0) && !(Walk->BpfDeviceFlags & LX_BPF_F_ALLOW_MULTI))
            continue;
        for (Entry = Walk->BpfDevice.Flink; (Entry != &Walk->BpfDevice) && (Count < RTL_NUMBER_OF(Progs));
             Entry = Entry->Flink)
        {
            Progs[Count] = CONTAINING_RECORD(Entry, LX_BPF_ATTACH, Link)->Prog;
            LxBpfProgReference(Progs[Count]);
            Count++;
        }
    }
    ExReleaseFastMutex(&LxBpfLock);

    Ctx.AccessType = (Access << 16) | (Block ? 1 : 2);
    Ctx.Major = Major;
    Ctx.Minor = Minor;
    for (Index = 0; Index < Count; Index++)
    {
        ULONG64 Result;

        if ((Error == 0) && (!LxBpfRun(Progs[Index], &Ctx, &Result) || (Result == 0)))
            Error = -LX_EPERM;
        LxBpfProgDereference(Progs[Index]);
    }
    return Error;
}

VOID
LxBpfCgroupRelease(
    _In_ PLX_CGROUP Cgroup)
{
    while (!IsListEmpty(&Cgroup->BpfDevice))
    {
        PLX_BPF_ATTACH Attach = CONTAINING_RECORD(RemoveHeadList(&Cgroup->BpfDevice), LX_BPF_ATTACH, Link);

        LxBpfProgDereference(Attach->Prog);
        LxFree(Attach);
    }
}
