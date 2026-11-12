/*
 * PROJECT:     LiberNT Linux Subsystem
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Secure computing mode: strict mode and classic BPF system call filters
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "lxcore.h"

#define NDEBUG
#include <debug.h>

#define LX_SECCOMP_SET_MODE_STRICT          0
#define LX_SECCOMP_SET_MODE_FILTER          1
#define LX_SECCOMP_GET_ACTION_AVAIL         2

#define LX_SECCOMP_MODE_DISABLED            0
#define LX_SECCOMP_MODE_STRICT              1
#define LX_SECCOMP_MODE_FILTER              2

#define LX_SECCOMP_FILTER_FLAG_TSYNC        0x01
#define LX_SECCOMP_FILTER_FLAG_LOG          0x02
#define LX_SECCOMP_FILTER_FLAG_SPEC_ALLOW   0x04
#define LX_SECCOMP_FILTER_FLAG_NEW_LISTENER 0x08
#define LX_SECCOMP_FILTER_FLAG_TSYNC_ESRCH  0x10
#define LX_SECCOMP_FILTER_FLAG_WAIT_KILLABLE_RECV 0x20
#define LX_SECCOMP_GET_NOTIF_SIZES          3

#define LX_SECCOMP_USER_NOTIF_FLAG_CONTINUE 0x01
#define LX_SECCOMP_USER_NOTIF_FD_SYNC_WAKE_UP 0x01
#define LX_SECCOMP_ADDFD_FLAG_SETFD         0x01
#define LX_SECCOMP_ADDFD_FLAG_SEND          0x02
#define LX_SECCOMP_IOCTL_NOTIF_RECV         0xC0502100
#define LX_SECCOMP_IOCTL_NOTIF_SEND         0xC0182101
#define LX_SECCOMP_IOCTL_NOTIF_ID_VALID     0x40082102
#define LX_SECCOMP_IOCTL_NOTIF_ID_VALID_OLD 0x80082102
#define LX_SECCOMP_IOCTL_NOTIF_ADDFD        0x40182103
#define LX_SECCOMP_IOCTL_NOTIF_SET_FLAGS    0x40082104

#define LX_SECCOMP_LOG_KILL_PROCESS         0x01
#define LX_SECCOMP_LOG_KILL_THREAD          0x02
#define LX_SECCOMP_LOG_TRAP                 0x04
#define LX_SECCOMP_LOG_ERRNO                0x08
#define LX_SECCOMP_LOG_TRACE                0x10
#define LX_SECCOMP_LOG_LOG                  0x20
#define LX_SECCOMP_LOG_ALLOW                0x40
#define LX_SECCOMP_LOG_USER_NOTIF           0x80

#define LX_SECCOMP_RET_KILL_PROCESS         0x80000000UL
#define LX_SECCOMP_RET_KILL_THREAD          0x00000000UL
#define LX_SECCOMP_RET_TRAP                 0x00030000UL
#define LX_SECCOMP_RET_ERRNO                0x00050000UL
#define LX_SECCOMP_RET_USER_NOTIF           0x7FC00000UL
#define LX_SECCOMP_RET_TRACE                0x7FF00000UL
#define LX_SECCOMP_RET_LOG                  0x7FFC0000UL
#define LX_SECCOMP_RET_ALLOW                0x7FFF0000UL
#define LX_SECCOMP_RET_ACTION_FULL          0xFFFF0000UL
#define LX_SECCOMP_RET_DATA                 0x0000FFFFUL

#define LX_SYS_SECCOMP                      1
#define LX_AUDIT_ARCH_64BIT                 0x80000000UL
#define LX_AUDIT_ARCH_LE                    0x40000000UL
#define LX_MAX_ERRNO                        4095

#define LX_CBPF_MAXINSNS                    4096
#define LX_CBPF_MEMWORDS                    16
#define LX_SECCOMP_MAX_INSNS_PER_PATH       32768
#define LX_SECCOMP_DATA_SIZE                64

#define LX_CBPF_LD                          0x00
#define LX_CBPF_LDX                         0x01
#define LX_CBPF_ST                          0x02
#define LX_CBPF_STX                         0x03
#define LX_CBPF_ALU                         0x04
#define LX_CBPF_JMP                         0x05
#define LX_CBPF_RET                         0x06
#define LX_CBPF_MISC                        0x07

#define LX_CBPF_LD_IMM                      0x00
#define LX_CBPF_LD_ABS                      0x20
#define LX_CBPF_LD_MEM                      0x60
#define LX_CBPF_LD_LEN                      0x80
#define LX_CBPF_LDX_IMM                     0x01
#define LX_CBPF_LDX_MEM                     0x61
#define LX_CBPF_LDX_LEN                     0x81
#define LX_CBPF_RET_K                       0x06
#define LX_CBPF_RET_A                       0x16
#define LX_CBPF_TAX                         0x07
#define LX_CBPF_TXA                         0x87

#define LX_CBPF_SRC_X                       0x08
#define LX_CBPF_ADD                         0x00
#define LX_CBPF_SUB                         0x10
#define LX_CBPF_MUL                         0x20
#define LX_CBPF_DIV                         0x30
#define LX_CBPF_OR                          0x40
#define LX_CBPF_AND                         0x50
#define LX_CBPF_LSH                         0x60
#define LX_CBPF_RSH                         0x70
#define LX_CBPF_NEG                         0x80
#define LX_CBPF_MOD                         0x90
#define LX_CBPF_XOR                         0xA0
#define LX_CBPF_JA                          0x00
#define LX_CBPF_JEQ                         0x10
#define LX_CBPF_JGT                         0x20
#define LX_CBPF_JGE                         0x30
#define LX_CBPF_JSET                        0x40

typedef struct _LX_SOCK_FILTER
{
    USHORT Code;
    UCHAR Jt;
    UCHAR Jf;
    ULONG K;
} LX_SOCK_FILTER, *PLX_SOCK_FILTER;

C_ASSERT(sizeof(LX_SOCK_FILTER) == 8);

typedef struct _LX_SOCK_FPROG
{
    USHORT Length;
    USHORT Pad[3];
    ULONG64 Filter;
} LX_SOCK_FPROG;

C_ASSERT(sizeof(LX_SOCK_FPROG) == 16);

typedef struct _LX_SECCOMP_NOTIF_USER
{
    ULONG64 Id;
    ULONG Pid;
    ULONG Flags;
    ULONG Data[LX_SECCOMP_DATA_SIZE / sizeof(ULONG)];
} LX_SECCOMP_NOTIF_USER;

C_ASSERT(sizeof(LX_SECCOMP_NOTIF_USER) == 80);

typedef struct _LX_SECCOMP_NOTIF_RESP
{
    ULONG64 Id;
    LONG64 Value;
    LONG Error;
    ULONG Flags;
} LX_SECCOMP_NOTIF_RESP;

C_ASSERT(sizeof(LX_SECCOMP_NOTIF_RESP) == 24);

typedef struct _LX_SECCOMP_NOTIF_ADDFD
{
    ULONG64 Id;
    ULONG Flags;
    ULONG SrcFd;
    ULONG NewFd;
    ULONG NewFdFlags;
} LX_SECCOMP_NOTIF_ADDFD;

C_ASSERT(sizeof(LX_SECCOMP_NOTIF_ADDFD) == 24);

typedef struct _LX_SECCOMP_NOTIF_SIZES
{
    USHORT Notif;
    USHORT Resp;
    USHORT Data;
} LX_SECCOMP_NOTIF_SIZES;

C_ASSERT(sizeof(LX_SECCOMP_NOTIF_SIZES) == 6);

typedef enum _LX_SECCOMP_NOTIF_STATE
{
    LxNotifInit,
    LxNotifSent,
    LxNotifReplied
} LX_SECCOMP_NOTIF_STATE;

typedef struct _LX_SECCOMP_ADDFD_REQUEST
{
    LIST_ENTRY Link;
    PLX_FILE File;
    ULONG Flags;
    ULONG NewFd;
    ULONG NewFdFlags;
    LONG Result;
    KEVENT Completed;
} LX_SECCOMP_ADDFD_REQUEST, *PLX_SECCOMP_ADDFD_REQUEST;

typedef struct _LX_SECCOMP_LISTENER LX_SECCOMP_LISTENER, *PLX_SECCOMP_LISTENER;

typedef struct _LX_SECCOMP_NOTIF
{
    LIST_ENTRY Link;
    PLX_SECCOMP_LISTENER Listener;
    ULONG64 Id;
    LX_SECCOMP_NOTIF_STATE State;
    PLX_THREAD Thread;
    ULONG Data[LX_SECCOMP_DATA_SIZE / sizeof(ULONG)];
    LONG64 Value;
    LONG Error;
    ULONG Flags;
    KEVENT Wake;
    LIST_ENTRY AddFds;
} LX_SECCOMP_NOTIF, *PLX_SECCOMP_NOTIF;

struct _LX_SECCOMP_LISTENER
{
    FAST_MUTEX Lock;
    LIST_ENTRY Notifs;
    ULONG64 NextId;
    LX_POLL_HEAD Poll;
    BOOLEAN Closed;
    ULONG Flags;
    PLX_SECCOMP_FILTER Filter;
};

struct _LX_SECCOMP_FILTER
{
    volatile LONG References;
    volatile LONG Users;
    PLX_SECCOMP_FILTER Previous;
    PLX_SECCOMP_LISTENER Listener;
    BOOLEAN WaitKillable;
    ULONG Length;
    ULONG PathLength;
    BOOLEAN Log;
    LX_SOCK_FILTER Insns[ANYSIZE_ARRAY];
};

static volatile LONG LxSeccompActionsLogged = LX_SECCOMP_LOG_KILL_PROCESS | LX_SECCOMP_LOG_KILL_THREAD |
                                              LX_SECCOMP_LOG_TRAP | LX_SECCOMP_LOG_ERRNO |
                                              LX_SECCOMP_LOG_USER_NOTIF | LX_SECCOMP_LOG_TRACE | LX_SECCOMP_LOG_LOG;
static volatile LONG LxAuditSerial;
static LX_RATELIMIT LxAuditLimit;

static const struct
{
    ULONG Mask;
    PCSTR Name;
} LxSeccompActionNames[] =
{
    { LX_SECCOMP_LOG_KILL_PROCESS, "kill_process" },
    { LX_SECCOMP_LOG_KILL_THREAD, "kill_thread" },
    { LX_SECCOMP_LOG_TRAP, "trap" },
    { LX_SECCOMP_LOG_ERRNO, "errno" },
    { LX_SECCOMP_LOG_USER_NOTIF, "user_notif" },
    { LX_SECCOMP_LOG_TRACE, "trace" },
    { LX_SECCOMP_LOG_LOG, "log" },
    { LX_SECCOMP_LOG_ALLOW, "allow" },
};

static
VOID
LxSeccompDereference(
    _In_opt_ PLX_SECCOMP_FILTER Filter)
{
    while ((Filter != NULL) && (InterlockedDecrement(&Filter->References) == 0))
    {
        PLX_SECCOMP_FILTER Previous = Filter->Previous;

        LxFree(Filter->Listener);
        LxFree(Filter);
        Filter = Previous;
    }
}

static
VOID
LxSeccompFinishNotifLocked(
    _Inout_ PLX_SECCOMP_NOTIF Notif)
{
    while (!IsListEmpty(&Notif->AddFds))
    {
        PLX_SECCOMP_ADDFD_REQUEST Request =
            CONTAINING_RECORD(RemoveHeadList(&Notif->AddFds), LX_SECCOMP_ADDFD_REQUEST, Link);

        Request->Result = -LX_ESRCH;
        KeSetEvent(&Request->Completed, IO_NO_INCREMENT, FALSE);
    }
    RemoveEntryList(&Notif->Link);
}

static
VOID
LxSeccompGetUser(
    _In_ PLX_SECCOMP_FILTER Filter)
{
    InterlockedIncrement(&Filter->References);
    InterlockedIncrement(&Filter->Users);
}

static
VOID
LxSeccompPutUser(
    _In_opt_ PLX_SECCOMP_FILTER Filter)
{
    PLX_SECCOMP_FILTER Orphan = Filter;

    while ((Orphan != NULL) && (InterlockedDecrement(&Orphan->Users) == 0))
    {
        if (Orphan->Listener != NULL)
            LxPollWake(&Orphan->Listener->Poll, LX_POLLHUP);
        Orphan = Orphan->Previous;
    }
    LxSeccompDereference(Filter);
}

VOID
LxSeccompInherit(
    _Inout_ PLX_THREAD Thread,
    _In_ PLX_THREAD Parent)
{
    PLX_SECCOMP_FILTER Filter = (PLX_SECCOMP_FILTER)ReadPointerAcquire((PVOID volatile *)&Parent->Seccomp);

    if (Filter != NULL)
        LxSeccompGetUser(Filter);
    Thread->Seccomp = Filter;
    Thread->SeccompMode = Parent->SeccompMode;
}

VOID
LxSeccompRelease(
    _Inout_ PLX_THREAD Thread)
{
    PLX_SECCOMP_NOTIF Notif = (PLX_SECCOMP_NOTIF)InterlockedExchangePointer(&Thread->SeccompNotif, NULL);
    PLX_SECCOMP_FILTER Filter = (PLX_SECCOMP_FILTER)InterlockedExchangePointer((PVOID volatile *)&Thread->Seccomp, NULL);

    if (Notif != NULL)
    {
        PLX_SECCOMP_LISTENER Listener = Notif->Listener;

        ExAcquireFastMutex(&Listener->Lock);
        LxSeccompFinishNotifLocked(Notif);
        ExReleaseFastMutex(&Listener->Lock);
        LxPollWake(&Listener->Poll, LX_POLLOUT | LX_POLLWRNORM);
        LxFree(Notif);
    }
    LxSeccompPutUser(Filter);
}

ULONG
LxSeccompFilterCount(
    _In_ PLX_THREAD Thread)
{
    PLX_SECCOMP_FILTER Filter = (PLX_SECCOMP_FILTER)ReadPointerAcquire((PVOID volatile *)&Thread->Seccomp);
    ULONG Count = 0;

    for (; Filter != NULL; Filter = Filter->Previous)
        Count++;
    return Count;
}

static
LONG
LxSeccompValidate(
    _Inout_updates_(Length) PLX_SOCK_FILTER Insns,
    _In_ ULONG Length)
{
    ULONG Pc;

    for (Pc = 0; Pc < Length; Pc++)
    {
        PLX_SOCK_FILTER Insn = &Insns[Pc];
        ULONG Remaining = Length - Pc - 1;

        if (Insn->Code > 0xFF)
            return -LX_EINVAL;
        switch (Insn->Code & 0x07)
        {
            case LX_CBPF_LD:
                if (Insn->Code == LX_CBPF_LD_ABS)
                {
                    if ((Insn->K >= LX_SECCOMP_DATA_SIZE) || (Insn->K & 3))
                        return -LX_EINVAL;
                }
                else if (Insn->Code == LX_CBPF_LD_LEN)
                {
                    Insn->Code = LX_CBPF_LD_IMM;
                    Insn->K = LX_SECCOMP_DATA_SIZE;
                }
                else if (Insn->Code == LX_CBPF_LD_MEM)
                {
                    if (Insn->K >= LX_CBPF_MEMWORDS)
                        return -LX_EINVAL;
                }
                else if (Insn->Code != LX_CBPF_LD_IMM)
                {
                    return -LX_EINVAL;
                }
                break;

            case LX_CBPF_LDX:
                if (Insn->Code == LX_CBPF_LDX_LEN)
                {
                    Insn->Code = LX_CBPF_LDX_IMM;
                    Insn->K = LX_SECCOMP_DATA_SIZE;
                }
                else if (Insn->Code == LX_CBPF_LDX_MEM)
                {
                    if (Insn->K >= LX_CBPF_MEMWORDS)
                        return -LX_EINVAL;
                }
                else if (Insn->Code != LX_CBPF_LDX_IMM)
                {
                    return -LX_EINVAL;
                }
                break;

            case LX_CBPF_ST:
            case LX_CBPF_STX:
                if ((Insn->Code != LX_CBPF_ST) && (Insn->Code != LX_CBPF_STX))
                    return -LX_EINVAL;
                if (Insn->K >= LX_CBPF_MEMWORDS)
                    return -LX_EINVAL;
                break;

            case LX_CBPF_ALU:
                switch (Insn->Code & 0xF0)
                {
                    case LX_CBPF_ADD:
                    case LX_CBPF_SUB:
                    case LX_CBPF_MUL:
                    case LX_CBPF_OR:
                    case LX_CBPF_AND:
                    case LX_CBPF_XOR:
                        break;
                    case LX_CBPF_DIV:
                    case LX_CBPF_MOD:
                        if (!(Insn->Code & LX_CBPF_SRC_X) && (Insn->K == 0))
                            return -LX_EINVAL;
                        break;
                    case LX_CBPF_LSH:
                    case LX_CBPF_RSH:
                        if (!(Insn->Code & LX_CBPF_SRC_X) && (Insn->K >= 32))
                            return -LX_EINVAL;
                        break;
                    case LX_CBPF_NEG:
                        if (Insn->Code & LX_CBPF_SRC_X)
                            return -LX_EINVAL;
                        break;
                    default:
                        return -LX_EINVAL;
                }
                break;

            case LX_CBPF_JMP:
                switch (Insn->Code & 0xF0)
                {
                    case LX_CBPF_JA:
                        if ((Insn->Code & LX_CBPF_SRC_X) || (Insn->K >= Remaining))
                            return -LX_EINVAL;
                        break;
                    case LX_CBPF_JEQ:
                    case LX_CBPF_JGT:
                    case LX_CBPF_JGE:
                    case LX_CBPF_JSET:
                        if ((Insn->Jt >= Remaining) || (Insn->Jf >= Remaining))
                            return -LX_EINVAL;
                        break;
                    default:
                        return -LX_EINVAL;
                }
                break;

            case LX_CBPF_RET:
                if ((Insn->Code != LX_CBPF_RET_K) && (Insn->Code != LX_CBPF_RET_A))
                    return -LX_EINVAL;
                break;

            case LX_CBPF_MISC:
                if ((Insn->Code != LX_CBPF_TAX) && (Insn->Code != LX_CBPF_TXA))
                    return -LX_EINVAL;
                break;
        }
    }
    if ((Length == 0) || ((Insns[Length - 1].Code & 0x07) != LX_CBPF_RET))
        return -LX_EINVAL;
    return 0;
}

static
ULONG
LxSeccompRun(
    _In_ PLX_SECCOMP_FILTER Filter,
    _In_reads_(LX_SECCOMP_DATA_SIZE / sizeof(ULONG)) const ULONG *Data)
{
    ULONG A = 0, X = 0, Pc = 0, Source;
    ULONG Memory[LX_CBPF_MEMWORDS];
    BOOLEAN Taken;

    RtlZeroMemory(Memory, sizeof(Memory));
    while (Pc < Filter->Length)
    {
        const LX_SOCK_FILTER *Insn = &Filter->Insns[Pc++];

        switch (Insn->Code & 0x07)
        {
            case LX_CBPF_LD:
                if (Insn->Code == LX_CBPF_LD_ABS)
                    A = Data[Insn->K / sizeof(ULONG)];
                else if (Insn->Code == LX_CBPF_LD_MEM)
                    A = Memory[Insn->K];
                else
                    A = Insn->K;
                break;

            case LX_CBPF_LDX:
                X = (Insn->Code == LX_CBPF_LDX_MEM) ? Memory[Insn->K] : Insn->K;
                break;

            case LX_CBPF_ST:
                Memory[Insn->K] = A;
                break;

            case LX_CBPF_STX:
                Memory[Insn->K] = X;
                break;

            case LX_CBPF_ALU:
                Source = (Insn->Code & LX_CBPF_SRC_X) ? X : Insn->K;
                switch (Insn->Code & 0xF0)
                {
                    case LX_CBPF_ADD: A += Source; break;
                    case LX_CBPF_SUB: A -= Source; break;
                    case LX_CBPF_MUL: A *= Source; break;
                    case LX_CBPF_OR: A |= Source; break;
                    case LX_CBPF_AND: A &= Source; break;
                    case LX_CBPF_XOR: A ^= Source; break;
                    case LX_CBPF_LSH: A <<= (Source & 31); break;
                    case LX_CBPF_RSH: A >>= (Source & 31); break;
                    case LX_CBPF_NEG: A = 0 - A; break;
                    case LX_CBPF_DIV:
                        if (Source == 0)
                            return 0;
                        A /= Source;
                        break;
                    case LX_CBPF_MOD:
                        if (Source == 0)
                            return 0;
                        A %= Source;
                        break;
                }
                break;

            case LX_CBPF_JMP:
                if ((Insn->Code & 0xF0) == LX_CBPF_JA)
                {
                    Pc += Insn->K;
                    break;
                }
                Source = (Insn->Code & LX_CBPF_SRC_X) ? X : Insn->K;
                switch (Insn->Code & 0xF0)
                {
                    case LX_CBPF_JEQ: Taken = (A == Source); break;
                    case LX_CBPF_JGT: Taken = (A > Source); break;
                    case LX_CBPF_JGE: Taken = (A >= Source); break;
                    default: Taken = ((A & Source) != 0); break;
                }
                Pc += Taken ? Insn->Jt : Insn->Jf;
                break;

            case LX_CBPF_RET:
                return (Insn->Code == LX_CBPF_RET_A) ? A : Insn->K;

            case LX_CBPF_MISC:
                if (Insn->Code == LX_CBPF_TAX)
                    X = A;
                else
                    A = X;
                break;
        }
    }
    return 0;
}

ULONG
LxSeccompAuditArch(VOID)
{
    return LxArchElfMachine() | LX_AUDIT_ARCH_LE | ((sizeof(PVOID) == 8) ? LX_AUDIT_ARCH_64BIT : 0);
}

static
VOID
LxSeccompLog(
    _In_ PLX_CALL Call,
    _In_ LONG Signal,
    _In_ ULONG Action,
    _In_ BOOLEAN Requested)
{
    PLX_PROCESS Process = Call->Process;
    PLX_CRED Cred = LxProcessCred(Process);
    LARGE_INTEGER Now;
    LX_TIMESPEC Time;
    ULONG Mask, Logged = (ULONG)LxSeccompActionsLogged;
    LONG Missed;
    BOOLEAN Log;

    switch (Action & LX_SECCOMP_RET_ACTION_FULL)
    {
        case LX_SECCOMP_RET_ALLOW:
            return;
        case LX_SECCOMP_RET_TRAP:
            Mask = LX_SECCOMP_LOG_TRAP;
            break;
        case LX_SECCOMP_RET_ERRNO:
            Mask = LX_SECCOMP_LOG_ERRNO;
            break;
        case LX_SECCOMP_RET_TRACE:
            Mask = LX_SECCOMP_LOG_TRACE;
            break;
        case LX_SECCOMP_RET_USER_NOTIF:
            Mask = LX_SECCOMP_LOG_USER_NOTIF;
            break;
        case LX_SECCOMP_RET_LOG:
            Mask = LX_SECCOMP_LOG_LOG;
            Requested = TRUE;
            break;
        case LX_SECCOMP_RET_KILL_THREAD:
            Mask = LX_SECCOMP_LOG_KILL_THREAD;
            Requested = TRUE;
            break;
        default:
            Mask = LX_SECCOMP_LOG_KILL_PROCESS;
            Requested = TRUE;
            break;
    }
    Log = Requested && ((Logged & Mask) != 0);
    if (!Log)
        return;
    if (!LxRatelimit(&LxAuditLimit, LxPrintkRatelimitSeconds, LxPrintkRatelimitBurst, &Missed))
        return;
    if (Missed != 0)
        LxKlogPrintf(4, "kauditd_printk_skb: %ld callbacks suppressed", Missed);
    KeQuerySystemTime(&Now);
    LxNtTimeToTimespec(Now.QuadPart, &Time);
    LxKlogPrintf(5, "audit: type=1326 audit(%lld.%03lld:%lu): auid=4294967295 uid=%lu gid=%lu ses=4294967295 "
                 "pid=%lu comm=\"%s\" exe=\"%s\" sig=%ld arch=%lx syscall=%lu compat=0 ip=0x%llx code=0x%lx",
                 Time.Seconds, Time.Nanoseconds / 1000000, (ULONG)InterlockedIncrement(&LxAuditSerial),
                 Cred->Uid[0], Cred->Gid[0], Process->Pid, Call->Thread->Comm,
                 (Process->ExePath != NULL) ? Process->ExePath : "", Signal, LxSeccompAuditArch(), Call->Number,
                 LxArchGetUserPc(Call->TrapFrame), Action);
}

static
VOID
LxSeccompKill(
    _Inout_ PLX_CALL Call,
    _In_ LONG Signal,
    _In_ BOOLEAN WholeProcess)
{
    LONG Status = Signal;

    if (!WholeProcess && (Call->Process->ThreadCount > 1))
    {
        PsPicoTerminateCurrentThread((NTSTATUS)Signal);
        return;
    }
    if ((Signal == LX_SIGSYS) && LxCoreDump(Call->Thread, Call->TrapFrame, Call->ExceptionFrame, LX_SIGSYS))
        Status |= 0x80;
    LxProcessExitGroup(Call->Process, Status);
}

static
VOID
LxSeccompRunAddFd(
    _In_ PLX_PROCESS Process,
    _Inout_ PLX_SECCOMP_ADDFD_REQUEST Request)
{
    BOOLEAN CloseOnExec = (Request->NewFdFlags & LX_O_CLOEXEC) != 0;
    PLX_FILE Previous;
    LONG Fd;

    if (!(Request->Flags & LX_SECCOMP_ADDFD_FLAG_SETFD))
    {
        LxFileReference(Request->File);
        Fd = LxFdInstall(Process, Request->File, 0, CloseOnExec);
        if (Fd < 0)
            LxFileDereference(Request->File);
        Request->Result = Fd;
        return;
    }
    Fd = (LONG)Request->NewFd;
    if ((Fd < 0) || (Fd >= LX_MAX_FDS) || ((ULONG64)Fd >= Process->Limits[LX_RLIMIT_NOFILE].Cur))
    {
        Request->Result = -LX_EBADF;
        return;
    }
    LxFileReference(Request->File);
    ExAcquireFastMutex(&Process->Lock);
    Previous = Process->Fds[Fd];
    Process->Fds[Fd] = Request->File;
    Process->FdFlags[Fd] = CloseOnExec ? LX_FD_CLOEXEC : 0;
    ExReleaseFastMutex(&Process->Lock);
    if (Previous != NULL)
        LxFileDereference(Previous);
    Request->Result = Fd;
}

static
BOOLEAN
LxSeccompNotify(
    _Inout_ PLX_CALL Call,
    _In_ PLX_SECCOMP_FILTER Match,
    _In_reads_(LX_SECCOMP_DATA_SIZE / sizeof(ULONG)) const ULONG *Data,
    _Out_ PLONG64 Result)
{
    PLX_SECCOMP_LISTENER Listener = Match->Listener;
    PLX_SECCOMP_NOTIF Notif;
    BOOLEAN Continue = FALSE, Killable = FALSE;
    NTSTATUS Status;

    *Result = -LX_ENOSYS;
    if (Listener == NULL)
        return FALSE;
    Notif = LxAlloc(sizeof(*Notif));
    if (Notif == NULL)
    {
        *Result = -LX_ENOMEM;
        return FALSE;
    }
    Notif->Thread = Call->Thread;
    Notif->Listener = Listener;
    Notif->State = LxNotifInit;
    RtlCopyMemory(Notif->Data, Data, sizeof(Notif->Data));
    KeInitializeEvent(&Notif->Wake, NotificationEvent, FALSE);
    InitializeListHead(&Notif->AddFds);

    ExAcquireFastMutex(&Listener->Lock);
    if (Listener->Closed)
    {
        ExReleaseFastMutex(&Listener->Lock);
        LxFree(Notif);
        return FALSE;
    }
    Notif->Id = Listener->NextId++;
    InsertTailList(&Listener->Notifs, &Notif->Link);
    Call->Thread->SeccompNotif = Notif;
    ExReleaseFastMutex(&Listener->Lock);
    LxPollWake(&Listener->Poll, LX_POLLIN | LX_POLLRDNORM);

    for (;;)
    {
        PLX_SECCOMP_ADDFD_REQUEST Request = NULL;

        ExAcquireFastMutex(&Listener->Lock);
        KeClearEvent(&Notif->Wake);
        if ((Notif->State == LxNotifReplied) || Listener->Closed)
        {
            if (Listener->Closed && (Notif->State != LxNotifReplied))
            {
                Notif->Error = -LX_ENOSYS;
                Notif->Value = 0;
                Notif->Flags = 0;
            }
            break;
        }
        if (!IsListEmpty(&Notif->AddFds))
            Request = CONTAINING_RECORD(RemoveHeadList(&Notif->AddFds), LX_SECCOMP_ADDFD_REQUEST, Link);
        ExReleaseFastMutex(&Listener->Lock);

        if (Request != NULL)
        {
            LxSeccompRunAddFd(Call->Process, Request);
            ExAcquireFastMutex(&Listener->Lock);
            if ((Request->Flags & LX_SECCOMP_ADDFD_FLAG_SEND) && (Request->Result >= 0))
            {
                Notif->State = LxNotifReplied;
                Notif->Value = Request->Result;
                Notif->Error = 0;
                Notif->Flags = 0;
            }
            KeSetEvent(&Request->Completed, IO_NO_INCREMENT, FALSE);
            ExReleaseFastMutex(&Listener->Lock);
            LxPollWake(&Listener->Poll, LX_POLLOUT | LX_POLLWRNORM);
            continue;
        }

        if (Killable)
            Status = KeWaitForSingleObject(&Notif->Wake, UserRequest, UserMode, TRUE, NULL);
        else
            Status = LxWaitForEvent(&Notif->Wake, NULL);
        if ((Status == STATUS_ALERTED) || (Status == STATUS_USER_APC))
        {
            ULONG64 Kill = 1ULL << (LX_SIGKILL - 1);

            ExAcquireFastMutex(&Listener->Lock);
            if (Match->WaitKillable && (Notif->State == LxNotifSent) &&
                !((Call->Thread->Pending | Call->Thread->Process->Pending) & Kill) &&
                !PsIsThreadTerminating(PsGetCurrentThread()))
            {
                ExReleaseFastMutex(&Listener->Lock);
                Killable = TRUE;
                continue;
            }
            if (Notif->State != LxNotifReplied)
            {
                Notif->Error = -LX_ERESTARTSYS;
                Notif->Value = 0;
                Notif->Flags = 0;
            }
            break;
        }
    }
    LxSeccompFinishNotifLocked(Notif);
    Call->Thread->SeccompNotif = NULL;
    Continue = (Notif->Flags & LX_SECCOMP_USER_NOTIF_FLAG_CONTINUE) != 0;
    *Result = (Notif->Error != 0) ? (LONG64)Notif->Error : Notif->Value;
    ExReleaseFastMutex(&Listener->Lock);
    LxPollWake(&Listener->Poll, LX_POLLOUT | LX_POLLWRNORM);
    LxFree(Notif);
    return Continue;
}

static
BOOLEAN
LxSeccompCheckCommon(
    _Inout_ PLX_CALL Call,
    _Out_ PLONG64 Result,
    _In_ BOOLEAN Recheck)
{
    PLX_THREAD Thread = Call->Thread;
    PLX_SECCOMP_FILTER Filter, Current, Match = NULL;
    ULONG Data[LX_SECCOMP_DATA_SIZE / sizeof(ULONG)];
    ULONG64 Pc = LxArchGetUserPc(Call->TrapFrame);
    ULONG Action = LX_SECCOMP_RET_ALLOW, Verdict, Index;
    LX_SIGINFO Info;

    *Result = LX_SYSCALL_NO_RESULT;
    if (Thread->SeccompMode == LX_SECCOMP_MODE_STRICT)
    {
        PLX_SYSCALL_ENTRY Entry = LxArchSyscallEntry(Call->Number);

        if ((Entry != NULL) && ((Entry->Handler == LxSysRead) || (Entry->Handler == LxSysWrite) ||
                                (Entry->Handler == LxSysExit) || (Entry->Handler == LxSysRtSigreturn)))
        {
            return TRUE;
        }
        LxSeccompLog(Call, LX_SIGKILL, LX_SECCOMP_RET_KILL_THREAD, TRUE);
        LxSeccompKill(Call, LX_SIGKILL, FALSE);
        return FALSE;
    }

    Filter = (PLX_SECCOMP_FILTER)ReadPointerAcquire((PVOID volatile *)&Thread->Seccomp);
    if (Filter == NULL)
        return TRUE;

    Data[0] = Call->Number;
    Data[1] = LxSeccompAuditArch();
    Data[2] = (ULONG)Pc;
    Data[3] = (ULONG)(Pc >> 32);
    for (Index = 0; Index < 6; Index++)
    {
        Data[4 + Index * 2] = (ULONG)Call->Args[Index];
        Data[5 + Index * 2] = (ULONG)(Call->Args[Index] >> 32);
    }

    for (Current = Filter; Current != NULL; Current = Current->Previous)
    {
        Verdict = LxSeccompRun(Current, Data);
        if ((LONG)(Verdict & LX_SECCOMP_RET_ACTION_FULL) < (LONG)(Action & LX_SECCOMP_RET_ACTION_FULL))
        {
            Action = Verdict;
            Match = Current;
        }
    }

    switch (Action & LX_SECCOMP_RET_ACTION_FULL)
    {
        case LX_SECCOMP_RET_ALLOW:
            return TRUE;

        case LX_SECCOMP_RET_LOG:
            LxSeccompLog(Call, 0, Action, TRUE);
            return TRUE;

        case LX_SECCOMP_RET_ERRNO:
            *Result = -(LONG64)min(Action & LX_SECCOMP_RET_DATA, LX_MAX_ERRNO);
            LxSeccompLog(Call, 0, Action, Match->Log);
            return FALSE;

        case LX_SECCOMP_RET_TRACE:
            if (Recheck)
                return TRUE;
            if (!LxPtraceSeccomp(Call, Action & LX_SECCOMP_RET_DATA))
            {
                *Result = -LX_ENOSYS;
                LxSeccompLog(Call, 0, Action, Match->Log);
                return FALSE;
            }
            if (((((ULONG64)Thread->Pending | (ULONG64)Thread->Process->Pending) >> (LX_SIGKILL - 1)) & 1) ||
                ((LONG)Call->Number < 0))
            {
                LxSeccompLog(Call, 0, Action, Match->Log);
                return FALSE;
            }
            return LxSeccompCheckCommon(Call, Result, TRUE);

        case LX_SECCOMP_RET_USER_NOTIF:
            if (LxSeccompNotify(Call, Match, Data, Result))
            {
                *Result = LX_SYSCALL_NO_RESULT;
                return TRUE;
            }
            LxSeccompLog(Call, 0, Action, Match->Log);
            return FALSE;

        case LX_SECCOMP_RET_TRAP:
            RtlZeroMemory(&Info, sizeof(Info));
            Info.Signo = LX_SIGSYS;
            Info.Errno = (LONG)(Action & LX_SECCOMP_RET_DATA);
            Info.Code = LX_SYS_SECCOMP;
            RtlCopyMemory(&Info.Raw[0], &Pc, sizeof(Pc));
            RtlCopyMemory(&Info.Raw[8], &Data[0], sizeof(ULONG));
            RtlCopyMemory(&Info.Raw[12], &Data[1], sizeof(ULONG));
            LxSeccompLog(Call, 0, Action, Match->Log);
            if (!LxSignalDeliverFault(Thread, Call->TrapFrame, Call->ExceptionFrame, LX_SIGSYS, &Info))
                LxSeccompKill(Call, LX_SIGSYS, TRUE);
            return FALSE;

        case LX_SECCOMP_RET_KILL_THREAD:
            LxSeccompLog(Call, LX_SIGSYS, Action, TRUE);
            LxSeccompKill(Call, LX_SIGSYS, FALSE);
            return FALSE;

        default:
            LxSeccompLog(Call, LX_SIGSYS, Action, TRUE);
            LxSeccompKill(Call, LX_SIGSYS, TRUE);
            return FALSE;
    }
}

BOOLEAN
LxSeccompCheck(
    _Inout_ PLX_CALL Call,
    _Out_ PLONG64 Result)
{
    return LxSeccompCheckCommon(Call, Result, FALSE);
}

static
BOOLEAN
LxSeccompIsAncestor(
    _In_opt_ PLX_SECCOMP_FILTER Candidate,
    _In_opt_ PLX_SECCOMP_FILTER Chain)
{
    if (Candidate == NULL)
        return TRUE;
    for (; Chain != NULL; Chain = Chain->Previous)
    {
        if (Chain == Candidate)
            return TRUE;
    }
    return FALSE;
}

static
BOOLEAN
LxSeccompRecvReady(
    _In_ PVOID Context)
{
    PLX_SECCOMP_LISTENER Listener = Context;
    PLIST_ENTRY Entry;
    BOOLEAN Ready = (Listener->Filter->Users == 0);

    ExAcquireFastMutex(&Listener->Lock);
    for (Entry = Listener->Notifs.Flink; !Ready && (Entry != &Listener->Notifs); Entry = Entry->Flink)
        Ready = (CONTAINING_RECORD(Entry, LX_SECCOMP_NOTIF, Link)->State == LxNotifInit);
    ExReleaseFastMutex(&Listener->Lock);
    return Ready;
}

static
PLX_SECCOMP_NOTIF
LxSeccompFindNotifLocked(
    _In_ PLX_SECCOMP_LISTENER Listener,
    _In_ ULONG64 Id)
{
    PLIST_ENTRY Entry;

    for (Entry = Listener->Notifs.Flink; Entry != &Listener->Notifs; Entry = Entry->Flink)
    {
        PLX_SECCOMP_NOTIF Notif = CONTAINING_RECORD(Entry, LX_SECCOMP_NOTIF, Link);

        if (Notif->Id == Id)
            return Notif;
    }
    return NULL;
}

static
LONG64
LxSeccompListenerRecv(
    _In_ PLX_SECCOMP_LISTENER Listener,
    _In_ ULONG64 Argument)
{
    PLX_THREAD Current = LxCurrentThread();
    LX_SECCOMP_NOTIF_USER User;
    PLX_SECCOMP_NOTIF Notif = NULL;
    PLIST_ENTRY Entry;
    ULONG Index;
    LONG64 Result;

    Result = LxCopyFromUser(&User, Argument, sizeof(User));
    if (Result != 0)
        return Result;
    for (Index = 0; Index < sizeof(User); Index++)
    {
        if (((PUCHAR)&User)[Index] != 0)
            return -LX_EINVAL;
    }
    for (;;)
    {
        ExAcquireFastMutex(&Listener->Lock);
        for (Entry = Listener->Notifs.Flink; Entry != &Listener->Notifs; Entry = Entry->Flink)
        {
            Notif = CONTAINING_RECORD(Entry, LX_SECCOMP_NOTIF, Link);
            if (Notif->State == LxNotifInit)
                break;
            Notif = NULL;
        }
        if (Notif != NULL)
            break;
        if (Listener->Filter->Users == 0)
        {
            ExReleaseFastMutex(&Listener->Lock);
            return -LX_ENOENT;
        }
        ExReleaseFastMutex(&Listener->Lock);
        Result = LxWaitHead(&Listener->Poll, LxSeccompRecvReady, Listener, NULL);
        if (Result != 0)
            return Result;
    }
    Notif->State = LxNotifSent;
    User.Id = Notif->Id;
    User.Pid = (Current != NULL) ? LxTidNr(Current->Process, Notif->Thread) : 0;
    User.Flags = 0;
    RtlCopyMemory(User.Data, Notif->Data, sizeof(User.Data));
    ExReleaseFastMutex(&Listener->Lock);

    Result = LxCopyToUser(Argument, &User, sizeof(User));
    if (Result != 0)
    {
        ExAcquireFastMutex(&Listener->Lock);
        Notif = LxSeccompFindNotifLocked(Listener, User.Id);
        if ((Notif != NULL) && (Notif->State == LxNotifSent))
            Notif->State = LxNotifInit;
        ExReleaseFastMutex(&Listener->Lock);
        LxPollWake(&Listener->Poll, LX_POLLIN | LX_POLLRDNORM);
        return Result;
    }
    LxPollWake(&Listener->Poll, LX_POLLOUT | LX_POLLWRNORM);
    return 0;
}

static
LONG64
LxSeccompListenerSend(
    _In_ PLX_SECCOMP_LISTENER Listener,
    _In_ ULONG64 Argument)
{
    LX_SECCOMP_NOTIF_RESP Resp;
    PLX_SECCOMP_NOTIF Notif;
    LONG64 Result;

    Result = LxCopyFromUser(&Resp, Argument, sizeof(Resp));
    if (Result != 0)
        return Result;
    if (Resp.Flags & ~LX_SECCOMP_USER_NOTIF_FLAG_CONTINUE)
        return -LX_EINVAL;
    if ((Resp.Flags & LX_SECCOMP_USER_NOTIF_FLAG_CONTINUE) && ((Resp.Error != 0) || (Resp.Value != 0)))
        return -LX_EINVAL;
    ExAcquireFastMutex(&Listener->Lock);
    Notif = LxSeccompFindNotifLocked(Listener, Resp.Id);
    if (Notif == NULL)
        Result = -LX_ENOENT;
    else if (Notif->State != LxNotifSent)
        Result = -LX_EINPROGRESS;
    else
    {
        Notif->State = LxNotifReplied;
        Notif->Error = Resp.Error;
        Notif->Value = Resp.Value;
        Notif->Flags = Resp.Flags;
        KeSetEvent(&Notif->Wake, IO_NO_INCREMENT, FALSE);
    }
    ExReleaseFastMutex(&Listener->Lock);
    return Result;
}

static
LONG64
LxSeccompListenerAddFd(
    _In_ PLX_SECCOMP_LISTENER Listener,
    _In_ ULONG64 Argument)
{
    PLX_THREAD Current = LxCurrentThread();
    LX_SECCOMP_NOTIF_ADDFD AddFd;
    LX_SECCOMP_ADDFD_REQUEST Request;
    PLX_SECCOMP_NOTIF Notif;
    NTSTATUS Status;
    LONG64 Result;

    Result = LxCopyFromUser(&AddFd, Argument, sizeof(AddFd));
    if (Result != 0)
        return Result;
    if (AddFd.Flags & ~(LX_SECCOMP_ADDFD_FLAG_SETFD | LX_SECCOMP_ADDFD_FLAG_SEND))
        return -LX_EINVAL;
    if (AddFd.NewFdFlags & ~LX_O_CLOEXEC)
        return -LX_EINVAL;
    if ((AddFd.NewFd != 0) && !(AddFd.Flags & LX_SECCOMP_ADDFD_FLAG_SETFD))
        return -LX_EINVAL;
    if (Current == NULL)
        return -LX_ESRCH;
    RtlZeroMemory(&Request, sizeof(Request));
    Request.File = LxFdGet(Current->Process, (LONG)AddFd.SrcFd);
    if (Request.File == NULL)
        return -LX_EBADF;
    Request.Flags = AddFd.Flags;
    Request.NewFd = AddFd.NewFd;
    Request.NewFdFlags = AddFd.NewFdFlags;
    KeInitializeEvent(&Request.Completed, NotificationEvent, FALSE);

    ExAcquireFastMutex(&Listener->Lock);
    Notif = LxSeccompFindNotifLocked(Listener, AddFd.Id);
    if (Notif == NULL)
        Result = -LX_ENOENT;
    else if (Notif->State != LxNotifSent)
        Result = -LX_EINPROGRESS;
    else
    {
        InsertTailList(&Notif->AddFds, &Request.Link);
        KeSetEvent(&Notif->Wake, IO_NO_INCREMENT, FALSE);
    }
    ExReleaseFastMutex(&Listener->Lock);
    if (Result != 0)
    {
        LxFileDereference(Request.File);
        return Result;
    }

    Status = LxWaitForEvent(&Request.Completed, NULL);
    ExAcquireFastMutex(&Listener->Lock);
    if (!KeReadStateEvent(&Request.Completed))
    {
        RemoveEntryList(&Request.Link);
        Request.Result = (Status == STATUS_ALERTED) ? -LX_EINTR : -LX_ESRCH;
    }
    ExReleaseFastMutex(&Listener->Lock);
    LxFileDereference(Request.File);
    return Request.Result;
}

static
LONG64
LxSeccompListenerIoctl(
    _In_ PLX_FILE File,
    _In_ ULONG Request,
    _In_ ULONG64 Argument)
{
    PLX_SECCOMP_LISTENER Listener = File->Object;
    PLX_SECCOMP_NOTIF Notif;
    ULONG64 Id;
    LONG64 Result;

    switch (Request)
    {
        case LX_SECCOMP_IOCTL_NOTIF_RECV:
            return LxSeccompListenerRecv(Listener, Argument);

        case LX_SECCOMP_IOCTL_NOTIF_SEND:
            return LxSeccompListenerSend(Listener, Argument);

        case LX_SECCOMP_IOCTL_NOTIF_ID_VALID:
        case LX_SECCOMP_IOCTL_NOTIF_ID_VALID_OLD:
            Result = LxCopyFromUser(&Id, Argument, sizeof(Id));
            if (Result != 0)
                return Result;
            ExAcquireFastMutex(&Listener->Lock);
            Notif = LxSeccompFindNotifLocked(Listener, Id);
            Result = ((Notif != NULL) && (Notif->State == LxNotifSent)) ? 0 : -LX_ENOENT;
            ExReleaseFastMutex(&Listener->Lock);
            return Result;

        case LX_SECCOMP_IOCTL_NOTIF_ADDFD:
            return LxSeccompListenerAddFd(Listener, Argument);

        case LX_SECCOMP_IOCTL_NOTIF_SET_FLAGS:
            if (Argument & ~(ULONG64)LX_SECCOMP_USER_NOTIF_FD_SYNC_WAKE_UP)
                return -LX_EINVAL;
            Listener->Flags = (ULONG)Argument;
            return 0;

        default:
            return -LX_EINVAL;
    }
}

static
ULONG
LxSeccompListenerPoll(
    _In_ PLX_FILE File,
    _In_opt_ PLX_POLL_ENTRY Entry)
{
    PLX_SECCOMP_LISTENER Listener = File->Object;
    PLIST_ENTRY Link;
    ULONG Mask = 0;

    if (Entry != NULL)
        LxPollAdd(&Listener->Poll, Entry);
    ExAcquireFastMutex(&Listener->Lock);
    for (Link = Listener->Notifs.Flink; Link != &Listener->Notifs; Link = Link->Flink)
    {
        PLX_SECCOMP_NOTIF Notif = CONTAINING_RECORD(Link, LX_SECCOMP_NOTIF, Link);

        if (Notif->State == LxNotifInit)
            Mask |= LX_POLLIN | LX_POLLRDNORM;
        else if (Notif->State == LxNotifSent)
            Mask |= LX_POLLOUT | LX_POLLWRNORM;
    }
    if (Listener->Filter->Users == 0)
        Mask |= LX_POLLHUP;
    ExReleaseFastMutex(&Listener->Lock);
    return Mask;
}

static
VOID
LxSeccompListenerRelease(
    _In_ PLX_FILE File)
{
    PLX_SECCOMP_LISTENER Listener = File->Object;
    PLIST_ENTRY Entry;

    ExAcquireFastMutex(&Listener->Lock);
    Listener->Closed = TRUE;
    for (Entry = Listener->Notifs.Flink; Entry != &Listener->Notifs; Entry = Entry->Flink)
    {
        PLX_SECCOMP_NOTIF Notif = CONTAINING_RECORD(Entry, LX_SECCOMP_NOTIF, Link);

        if (Notif->State != LxNotifReplied)
        {
            Notif->State = LxNotifReplied;
            Notif->Error = -LX_ENOSYS;
            Notif->Value = 0;
            Notif->Flags = 0;
        }
        KeSetEvent(&Notif->Wake, IO_NO_INCREMENT, FALSE);
    }
    ExReleaseFastMutex(&Listener->Lock);
    File->Object = NULL;
    LxSeccompDereference(Listener->Filter);
}

static const LX_FILE_OPS LxSeccompListenerOps =
{
    NULL, NULL, LxSeccompListenerIoctl, LxSeccompListenerPoll, LxAnonStat, NULL, NULL, NULL, LxSeccompListenerRelease
};

static
BOOLEAN
LxSeccompHasListener(
    _In_opt_ PLX_SECCOMP_FILTER Chain)
{
    for (; Chain != NULL; Chain = Chain->Previous)
    {
        if ((Chain->Listener != NULL) && !Chain->Listener->Closed)
            return TRUE;
    }
    return FALSE;
}

static
LONG64
LxSeccompSetStrict(
    _Inout_ PLX_CALL Call)
{
    PLX_THREAD Thread = Call->Thread;

    if (Thread->SeccompMode == LX_SECCOMP_MODE_FILTER)
        return -LX_EINVAL;
    Thread->SeccompMode = LX_SECCOMP_MODE_STRICT;
    return 0;
}

static
LONG64
LxSeccompSetFilter(
    _Inout_ PLX_CALL Call,
    _In_ ULONG Flags,
    _In_ ULONG64 UserProgram)
{
    PLX_PROCESS Process = Call->Process;
    PLX_THREAD Thread = Call->Thread;
    PLX_SECCOMP_FILTER Filter, Previous;
    PLX_SECCOMP_LISTENER Listener = NULL;
    PLX_FILE ListenerFile = NULL;
    LONG ListenerFd = -1;
    LX_SOCK_FPROG Program;
    PLIST_ENTRY Entry;
    LONG64 Result;
    SIZE_T Size;

    if (Flags & ~(LX_SECCOMP_FILTER_FLAG_TSYNC | LX_SECCOMP_FILTER_FLAG_LOG | LX_SECCOMP_FILTER_FLAG_SPEC_ALLOW |
                  LX_SECCOMP_FILTER_FLAG_NEW_LISTENER | LX_SECCOMP_FILTER_FLAG_TSYNC_ESRCH |
                  LX_SECCOMP_FILTER_FLAG_WAIT_KILLABLE_RECV))
    {
        return -LX_EINVAL;
    }
    if ((Flags & LX_SECCOMP_FILTER_FLAG_TSYNC) && (Flags & LX_SECCOMP_FILTER_FLAG_NEW_LISTENER) &&
        !(Flags & LX_SECCOMP_FILTER_FLAG_TSYNC_ESRCH))
    {
        return -LX_EINVAL;
    }
    if ((Flags & LX_SECCOMP_FILTER_FLAG_WAIT_KILLABLE_RECV) && !(Flags & LX_SECCOMP_FILTER_FLAG_NEW_LISTENER))
        return -LX_EINVAL;

    Result = LxCopyFromUser(&Program, UserProgram, sizeof(Program));
    if (Result != 0)
        return Result;
    if ((Program.Length == 0) || (Program.Length > LX_CBPF_MAXINSNS))
        return -LX_EINVAL;
    if (!Thread->NoNewPrivs && !LxCapable(Process, LX_CAP_SYS_ADMIN))
        return -LX_EACCES;

    Size = FIELD_OFFSET(LX_SECCOMP_FILTER, Insns) + (SIZE_T)Program.Length * sizeof(LX_SOCK_FILTER);
    Filter = LxAlloc(Size);
    if (Filter == NULL)
        return -LX_ENOMEM;
    Filter->References = 1;
    Filter->Users = 1;
    Filter->Length = Program.Length;
    Filter->Log = (Flags & LX_SECCOMP_FILTER_FLAG_LOG) != 0;
    Filter->WaitKillable = (Flags & LX_SECCOMP_FILTER_FLAG_WAIT_KILLABLE_RECV) != 0;
    Result = LxCopyFromUser(Filter->Insns, Program.Filter, (SIZE_T)Program.Length * sizeof(LX_SOCK_FILTER));
    if (Result == 0)
        Result = LxSeccompValidate(Filter->Insns, Filter->Length);
    if (Result != 0)
    {
        LxFree(Filter);
        return Result;
    }

    if (Flags & LX_SECCOMP_FILTER_FLAG_NEW_LISTENER)
    {
        Listener = LxAlloc(sizeof(*Listener));
        ListenerFile = (Listener != NULL) ? LxAnonFileCreate(&LxSeccompListenerOps, LX_O_RDWR, "seccomp notify", 0) :
                       NULL;
        if (ListenerFile == NULL)
        {
            LxFree(Listener);
            LxFree(Filter);
            return -LX_ENOMEM;
        }
        ExInitializeFastMutex(&Listener->Lock);
        InitializeListHead(&Listener->Notifs);
        LxPollHeadInit(&Listener->Poll);
        LxFillRandom((PUCHAR)&Listener->NextId, sizeof(Listener->NextId));
        Listener->Filter = Filter;
        Filter->Listener = Listener;
        Filter->References++;
        ListenerFile->Object = Listener;
        ListenerFd = LxFdInstall(Process, ListenerFile, 0, TRUE);
        if (ListenerFd < 0)
        {
            LxFileDereference(ListenerFile);
            LxSeccompDereference(Filter);
            return ListenerFd;
        }
    }

    ExAcquireFastMutex(&Process->Lock);
    Previous = Thread->Seccomp;
    Filter->PathLength = Filter->Length + ((Previous != NULL) ? Previous->PathLength + 4 : 0);
    if (Thread->SeccompMode == LX_SECCOMP_MODE_STRICT)
        Result = -LX_EINVAL;
    else if (Filter->PathLength > LX_SECCOMP_MAX_INSNS_PER_PATH)
        Result = -LX_ENOMEM;
    else if ((Listener != NULL) && LxSeccompHasListener(Previous))
        Result = -LX_EBUSY;

    if ((Result == 0) && (Flags & LX_SECCOMP_FILTER_FLAG_TSYNC))
    {
        for (Entry = Process->Threads.Flink; Entry != &Process->Threads; Entry = Entry->Flink)
        {
            PLX_THREAD Other = CONTAINING_RECORD(Entry, LX_THREAD, Link);

            if (Other == Thread)
                continue;
            if ((Other->SeccompMode == LX_SECCOMP_MODE_STRICT) || !LxSeccompIsAncestor(Other->Seccomp, Previous))
            {
                Result = (Flags & LX_SECCOMP_FILTER_FLAG_TSYNC_ESRCH) ? -LX_ESRCH : (LONG64)LxTidNr(Process, Other);
                break;
            }
        }
    }

    if (Result != 0)
    {
        ExReleaseFastMutex(&Process->Lock);
        if (ListenerFd >= 0)
        {
            LxFdClose(Process, ListenerFd);
            LxFileDereference(ListenerFile);
        }
        LxSeccompDereference(Filter);
        return Result;
    }

    Filter->Previous = Previous;
    InterlockedExchangePointer((PVOID volatile *)&Thread->Seccomp, Filter);
    Thread->SeccompMode = LX_SECCOMP_MODE_FILTER;

    if (Flags & LX_SECCOMP_FILTER_FLAG_TSYNC)
    {
        for (Entry = Process->Threads.Flink; Entry != &Process->Threads; Entry = Entry->Flink)
        {
            PLX_THREAD Other = CONTAINING_RECORD(Entry, LX_THREAD, Link);
            PLX_SECCOMP_FILTER Old;

            if (Other == Thread)
                continue;
            LxSeccompGetUser(Filter);
            Old = (PLX_SECCOMP_FILTER)InterlockedExchangePointer((PVOID volatile *)&Other->Seccomp, Filter);
            Other->SeccompMode = LX_SECCOMP_MODE_FILTER;
            if (Thread->NoNewPrivs)
                Other->NoNewPrivs = TRUE;
            LxSeccompPutUser(Old);
        }
    }
    ExReleaseFastMutex(&Process->Lock);
    if (ListenerFd >= 0)
    {
        LxFileDereference(ListenerFile);
        return ListenerFd;
    }
    return 0;
}

static
LONG64
LxSeccompActionAvailable(
    _In_ ULONG64 UserAction)
{
    ULONG Action;
    LONG64 Result = LxCopyFromUser(&Action, UserAction, sizeof(Action));

    if (Result != 0)
        return Result;
    switch (Action)
    {
        case LX_SECCOMP_RET_KILL_PROCESS:
        case LX_SECCOMP_RET_KILL_THREAD:
        case LX_SECCOMP_RET_TRAP:
        case LX_SECCOMP_RET_ERRNO:
        case LX_SECCOMP_RET_USER_NOTIF:
        case LX_SECCOMP_RET_TRACE:
        case LX_SECCOMP_RET_LOG:
        case LX_SECCOMP_RET_ALLOW:
            return 0;
        default:
            return -LX_EOPNOTSUPP;
    }
}

LONG64
LxSeccompPrctl(
    _Inout_ PLX_CALL Call,
    _In_ BOOLEAN Set,
    _In_ ULONG64 Mode,
    _In_ ULONG64 Program)
{
    if (!Set)
        return Call->Thread->SeccompMode;
    if (Mode == LX_SECCOMP_MODE_STRICT)
        return LxSeccompSetStrict(Call);
    if (Mode == LX_SECCOMP_MODE_FILTER)
        return LxSeccompSetFilter(Call, 0, Program);
    return -LX_EINVAL;
}

LX_SYSCALL(LxSysSeccomp)
{
    ULONG Operation = (ULONG)Call->Args[0];
    ULONG Flags = (ULONG)Call->Args[1];

    switch (Operation)
    {
        case LX_SECCOMP_SET_MODE_STRICT:
            if ((Flags != 0) || (Call->Args[2] != 0))
                return -LX_EINVAL;
            return LxSeccompSetStrict(Call);

        case LX_SECCOMP_SET_MODE_FILTER:
            return LxSeccompSetFilter(Call, Flags, Call->Args[2]);

        case LX_SECCOMP_GET_ACTION_AVAIL:
            if (Flags != 0)
                return -LX_EINVAL;
            return LxSeccompActionAvailable(Call->Args[2]);

        case LX_SECCOMP_GET_NOTIF_SIZES:
        {
            LX_SECCOMP_NOTIF_SIZES Sizes = { sizeof(LX_SECCOMP_NOTIF_USER), sizeof(LX_SECCOMP_NOTIF_RESP),
                                             LX_SECCOMP_DATA_SIZE };

            if (Flags != 0)
                return -LX_EINVAL;
            return LxCopyToUser(Call->Args[2], &Sizes, sizeof(Sizes));
        }

        default:
            return -LX_EINVAL;
    }
}

ULONG
LxSeccompFormatActions(
    _In_ BOOLEAN Logged,
    _Out_writes_(Size) PSTR Buffer,
    _In_ ULONG Size)
{
    ULONG Mask = Logged ? (ULONG)LxSeccompActionsLogged : MAXULONG, Index, Used = 0;

    Buffer[0] = '\0';
    for (Index = 0; Index < RTL_NUMBER_OF(LxSeccompActionNames); Index++)
    {
        if (!(Mask & LxSeccompActionNames[Index].Mask))
            continue;
        if (!NT_SUCCESS(RtlStringCbPrintfA(Buffer + Used, Size - Used, "%s%s", (Used != 0) ? " " : "",
                                           LxSeccompActionNames[Index].Name)))
        {
            break;
        }
        Used = (ULONG)strlen(Buffer);
    }
    return Used;
}

LONG
LxSeccompWriteActionsLogged(
    _In_ PLX_PROCESS Process,
    _In_reads_bytes_(Length) PCSTR Data,
    _In_ ULONG Length)
{
    CHAR Old[128], New[128];
    LARGE_INTEGER Now;
    LX_TIMESPEC Time;
    ULONG Mask = 0, Start = 0, Index, Name;

    if (!LxCapable(Process, LX_CAP_SYS_ADMIN))
        return -LX_EPERM;
    while ((Length > 0) && (Data[Length - 1] == '\n'))
        Length--;
    for (Index = 0; Index <= Length; Index++)
    {
        if ((Index < Length) && (Data[Index] != ' '))
            continue;
        if (Index > Start)
        {
            for (Name = 0; Name < RTL_NUMBER_OF(LxSeccompActionNames); Name++)
            {
                if ((strlen(LxSeccompActionNames[Name].Name) == Index - Start) &&
                    (strncmp(LxSeccompActionNames[Name].Name, Data + Start, Index - Start) == 0))
                {
                    break;
                }
            }
            if ((Name == RTL_NUMBER_OF(LxSeccompActionNames)) ||
                (LxSeccompActionNames[Name].Mask == LX_SECCOMP_LOG_ALLOW))
            {
                return -LX_EINVAL;
            }
            Mask |= LxSeccompActionNames[Name].Mask;
        }
        Start = Index + 1;
    }
    LxSeccompFormatActions(TRUE, Old, sizeof(Old));
    InterlockedExchange(&LxSeccompActionsLogged, (LONG)Mask);
    LxSeccompFormatActions(TRUE, New, sizeof(New));
    KeQuerySystemTime(&Now);
    LxNtTimeToTimespec(Now.QuadPart, &Time);
    LxKlogPrintf(5, "audit: type=1340 audit(%lld.%03lld:%lu): op=seccomp-logging actions=%s old-actions=%s res=1",
                 Time.Seconds, Time.Nanoseconds / 1000000, (ULONG)InterlockedIncrement(&LxAuditSerial),
                 (New[0] != '\0') ? New : "?", (Old[0] != '\0') ? Old : "?");
    return 0;
}
