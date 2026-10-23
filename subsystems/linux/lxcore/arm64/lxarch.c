/*
 * PROJECT:     LiberNT Linux Subsystem
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     AArch64 Linux system call table and register state
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "../lxcore.h"

#define LX_EM_AARCH64     183
#define LX_HWCAP_FP       (1ULL << 0)
#define LX_HWCAP_ASIMD    (1ULL << 1)

#define LX_A64_FRAME_SIZE       4688
#define LX_A64_UC               128
#define LX_A64_UC_STACK         (LX_A64_UC + 16)
#define LX_A64_UC_SIGMASK       (LX_A64_UC + 40)
#define LX_A64_MC               (LX_A64_UC + 176)
#define LX_A64_MC_FAULT         (LX_A64_MC + 0)
#define LX_A64_MC_REGS          (LX_A64_MC + 8)
#define LX_A64_MC_SP            (LX_A64_MC + 256)
#define LX_A64_MC_PC            (LX_A64_MC + 264)
#define LX_A64_MC_PSTATE        (LX_A64_MC + 272)
#define LX_A64_MC_RESERVED      (LX_A64_MC + 288)
#define LX_A64_FPSIMD_MAGIC     0x46508001
#define LX_A64_FPSIMD_SIZE      528
#define LX_A64_PSTATE_NZCV      0xF0000000UL

C_ASSERT(LX_A64_MC_RESERVED + 4096 == LX_A64_FRAME_SIZE);

#define LX_ENTRY(Number, Handler, Name) [Number] = { Handler, Name }

static LX_SYSCALL_ENTRY LxArm64Syscalls[] =
{
    LX_ENTRY(5, LxSysXattrSet, "setxattr"),
    LX_ENTRY(6, LxSysXattrSet, "lsetxattr"),
    LX_ENTRY(7, LxSysXattrSet, "fsetxattr"),
    LX_ENTRY(8, LxSysXattrGet, "getxattr"),
    LX_ENTRY(9, LxSysXattrGet, "lgetxattr"),
    LX_ENTRY(10, LxSysXattrGet, "fgetxattr"),
    LX_ENTRY(11, LxSysXattrList, "listxattr"),
    LX_ENTRY(12, LxSysXattrList, "llistxattr"),
    LX_ENTRY(13, LxSysXattrList, "flistxattr"),
    LX_ENTRY(14, LxSysXattrSet, "removexattr"),
    LX_ENTRY(15, LxSysXattrSet, "lremovexattr"),
    LX_ENTRY(16, LxSysXattrSet, "fremovexattr"),
    LX_ENTRY(17, LxSysGetcwd, "getcwd"),
    LX_ENTRY(19, LxSysEventfd2, "eventfd2"),
    LX_ENTRY(20, LxSysEpollCreate1, "epoll_create1"),
    LX_ENTRY(21, LxSysEpollCtl, "epoll_ctl"),
    LX_ENTRY(22, LxSysEpollPwait, "epoll_pwait"),
    LX_ENTRY(23, LxSysDup, "dup"),
    LX_ENTRY(24, LxSysDup3, "dup3"),
    LX_ENTRY(25, LxSysFcntl, "fcntl"),
    LX_ENTRY(26, LxSysInotifyInit1, "inotify_init1"),
    LX_ENTRY(27, LxSysInotifyAddWatch, "inotify_add_watch"),
    LX_ENTRY(28, LxSysInotifyRmWatch, "inotify_rm_watch"),
    LX_ENTRY(29, LxSysIoctl, "ioctl"),
    LX_ENTRY(30, LxSysSucceed, "ioprio_set"),
    LX_ENTRY(31, LxSysIoprioGet, "ioprio_get"),
    LX_ENTRY(32, LxSysFlock, "flock"),
    LX_ENTRY(33, LxSysMknodat, "mknodat"),
    LX_ENTRY(34, LxSysMkdirat, "mkdirat"),
    LX_ENTRY(35, LxSysUnlinkat, "unlinkat"),
    LX_ENTRY(36, LxSysSymlinkat, "symlinkat"),
    LX_ENTRY(37, LxSysLinkat, "linkat"),
    LX_ENTRY(38, LxSysRenameat, "renameat"),
    LX_ENTRY(39, LxSysUmount2, "umount2"),
    LX_ENTRY(40, LxSysMount, "mount"),
    LX_ENTRY(41, NULL, "pivot_root"),
    LX_ENTRY(43, LxSysStatfs, "statfs"),
    LX_ENTRY(44, LxSysFstatfs, "fstatfs"),
    LX_ENTRY(45, LxSysTruncate, "truncate"),
    LX_ENTRY(46, LxSysFtruncate, "ftruncate"),
    LX_ENTRY(47, LxSysFallocate, "fallocate"),
    LX_ENTRY(48, LxSysFaccessat, "faccessat"),
    LX_ENTRY(49, LxSysChdir, "chdir"),
    LX_ENTRY(50, LxSysFchdir, "fchdir"),
    LX_ENTRY(51, LxSysChroot, "chroot"),
    LX_ENTRY(52, LxSysFchmod, "fchmod"),
    LX_ENTRY(53, LxSysFchmodat, "fchmodat"),
    LX_ENTRY(54, LxSysFchownat, "fchownat"),
    LX_ENTRY(55, LxSysFchown, "fchown"),
    LX_ENTRY(56, LxSysOpenat, "openat"),
    LX_ENTRY(57, LxSysClose, "close"),
    LX_ENTRY(58, LxSysSucceed, "vhangup"),
    LX_ENTRY(59, LxSysPipe2, "pipe2"),
    LX_ENTRY(61, LxSysGetdents64, "getdents64"),
    LX_ENTRY(62, LxSysLseek, "lseek"),
    LX_ENTRY(63, LxSysRead, "read"),
    LX_ENTRY(64, LxSysWrite, "write"),
    LX_ENTRY(65, LxSysReadv, "readv"),
    LX_ENTRY(66, LxSysWritev, "writev"),
    LX_ENTRY(67, LxSysPread64, "pread64"),
    LX_ENTRY(68, LxSysPwrite64, "pwrite64"),
    LX_ENTRY(69, LxSysPreadv, "preadv"),
    LX_ENTRY(70, LxSysPwritev, "pwritev"),
    LX_ENTRY(71, LxSysSendfile, "sendfile"),
    LX_ENTRY(72, LxSysPselect6, "pselect6"),
    LX_ENTRY(73, LxSysPpoll, "ppoll"),
    LX_ENTRY(74, LxSysSignalfd4, "signalfd4"),
    LX_ENTRY(75, NULL, "vmsplice"),
    LX_ENTRY(76, NULL, "splice"),
    LX_ENTRY(77, NULL, "tee"),
    LX_ENTRY(78, LxSysReadlinkat, "readlinkat"),
    LX_ENTRY(79, LxSysNewfstatat, "newfstatat"),
    LX_ENTRY(80, LxSysFstat, "fstat"),
    LX_ENTRY(81, LxSysSucceed, "sync"),
    LX_ENTRY(82, LxSysFsync, "fsync"),
    LX_ENTRY(83, LxSysFsync, "fdatasync"),
    LX_ENTRY(84, LxSysSucceed, "sync_file_range"),
    LX_ENTRY(85, LxSysTimerfdCreate, "timerfd_create"),
    LX_ENTRY(86, LxSysTimerfdSettime, "timerfd_settime"),
    LX_ENTRY(87, LxSysTimerfdGettime, "timerfd_gettime"),
    LX_ENTRY(88, LxSysUtimensat, "utimensat"),
    LX_ENTRY(90, LxSysCapget, "capget"),
    LX_ENTRY(91, LxSysCapset, "capset"),
    LX_ENTRY(92, LxSysPersonality, "personality"),
    LX_ENTRY(93, LxSysExit, "exit"),
    LX_ENTRY(94, LxSysExitGroup, "exit_group"),
    LX_ENTRY(95, LxSysWaitid, "waitid"),
    LX_ENTRY(96, LxSysSetTidAddress, "set_tid_address"),
    LX_ENTRY(97, LxSysUnshare, "unshare"),
    LX_ENTRY(98, LxSysFutex, "futex"),
    LX_ENTRY(99, LxSysSucceed, "set_robust_list"),
    LX_ENTRY(101, LxSysNanosleep, "nanosleep"),
    LX_ENTRY(102, NULL, "getitimer"),
    LX_ENTRY(103, NULL, "setitimer"),
    LX_ENTRY(113, LxSysClockGettime, "clock_gettime"),
    LX_ENTRY(114, LxSysClockGetres, "clock_getres"),
    LX_ENTRY(115, LxSysClockNanosleep, "clock_nanosleep"),
    LX_ENTRY(116, LxSysSyslog, "syslog"),
    LX_ENTRY(118, LxSysSucceed, "sched_setparam"),
    LX_ENTRY(119, LxSysSucceed, "sched_setscheduler"),
    LX_ENTRY(120, LxSysSchedGetscheduler, "sched_getscheduler"),
    LX_ENTRY(121, LxSysSchedGetparam, "sched_getparam"),
    LX_ENTRY(122, LxSysSucceed, "sched_setaffinity"),
    LX_ENTRY(123, LxSysSchedGetaffinity, "sched_getaffinity"),
    LX_ENTRY(124, LxSysSchedYield, "sched_yield"),
    LX_ENTRY(125, LxSysSchedGetPriorityMax, "sched_get_priority_max"),
    LX_ENTRY(126, LxSysSchedGetPriorityMax, "sched_get_priority_min"),
    LX_ENTRY(129, LxSysKill, "kill"),
    LX_ENTRY(130, LxSysTkill, "tkill"),
    LX_ENTRY(131, LxSysTgkill, "tgkill"),
    LX_ENTRY(132, LxSysSigaltstack, "sigaltstack"),
    LX_ENTRY(133, LxSysRtSigsuspend, "rt_sigsuspend"),
    LX_ENTRY(134, LxSysRtSigaction, "rt_sigaction"),
    LX_ENTRY(135, LxSysRtSigprocmask, "rt_sigprocmask"),
    LX_ENTRY(136, LxSysRtSigpending, "rt_sigpending"),
    LX_ENTRY(137, LxSysRtSigtimedwait, "rt_sigtimedwait"),
    LX_ENTRY(138, LxSysRtSigqueueinfo, "rt_sigqueueinfo"),
    LX_ENTRY(139, LxSysRtSigreturn, "rt_sigreturn"),
    LX_ENTRY(140, LxSysSucceed, "setpriority"),
    LX_ENTRY(141, LxSysGetpriority, "getpriority"),
    LX_ENTRY(142, LxSysReboot, "reboot"),
    LX_ENTRY(143, LxSysSetid, "setregid"),
    LX_ENTRY(144, LxSysSetid, "setgid"),
    LX_ENTRY(145, LxSysSetid, "setreuid"),
    LX_ENTRY(146, LxSysSetid, "setuid"),
    LX_ENTRY(147, LxSysSetid, "setresuid"),
    LX_ENTRY(148, LxSysGetresuid, "getresuid"),
    LX_ENTRY(149, LxSysSetid, "setresgid"),
    LX_ENTRY(150, LxSysGetresuid, "getresgid"),
    LX_ENTRY(151, LxSysSetid, "setfsuid"),
    LX_ENTRY(152, LxSysSetid, "setfsgid"),
    LX_ENTRY(153, LxSysTimes, "times"),
    LX_ENTRY(154, LxSysSetpgid, "setpgid"),
    LX_ENTRY(155, LxSysGetpgid, "getpgid"),
    LX_ENTRY(156, LxSysGetsid, "getsid"),
    LX_ENTRY(157, LxSysSetsid, "setsid"),
    LX_ENTRY(158, LxSysGetgroups, "getgroups"),
    LX_ENTRY(159, LxSysSetgroups, "setgroups"),
    LX_ENTRY(160, LxSysUname, "uname"),
    LX_ENTRY(161, LxSysSethostname, "sethostname"),
    LX_ENTRY(162, LxSysSetdomainname, "setdomainname"),
    LX_ENTRY(163, LxSysGetrlimit, "getrlimit"),
    LX_ENTRY(164, LxSysSetrlimit, "setrlimit"),
    LX_ENTRY(165, LxSysGetrusage, "getrusage"),
    LX_ENTRY(166, LxSysUmask, "umask"),
    LX_ENTRY(167, LxSysPrctl, "prctl"),
    LX_ENTRY(168, LxSysGetcpu, "getcpu"),
    LX_ENTRY(169, LxSysGettimeofday, "gettimeofday"),
    LX_ENTRY(172, LxSysGetpid, "getpid"),
    LX_ENTRY(173, LxSysGetppid, "getppid"),
    LX_ENTRY(174, LxSysGetuid, "getuid"),
    LX_ENTRY(175, LxSysGetuid, "geteuid"),
    LX_ENTRY(176, LxSysGetuid, "getgid"),
    LX_ENTRY(177, LxSysGetuid, "getegid"),
    LX_ENTRY(178, LxSysGettid, "gettid"),
    LX_ENTRY(179, LxSysSysinfo, "sysinfo"),
    LX_ENTRY(198, LxSysSocket, "socket"),
    LX_ENTRY(199, LxSysSocketpair, "socketpair"),
    LX_ENTRY(200, LxSysBind, "bind"),
    LX_ENTRY(201, LxSysListen, "listen"),
    LX_ENTRY(202, LxSysAccept, "accept"),
    LX_ENTRY(203, LxSysConnect, "connect"),
    LX_ENTRY(204, LxSysGetsockname, "getsockname"),
    LX_ENTRY(205, LxSysGetpeername, "getpeername"),
    LX_ENTRY(206, LxSysSendto, "sendto"),
    LX_ENTRY(207, LxSysRecvfrom, "recvfrom"),
    LX_ENTRY(208, LxSysSetsockopt, "setsockopt"),
    LX_ENTRY(209, LxSysGetsockopt, "getsockopt"),
    LX_ENTRY(210, LxSysShutdown, "shutdown"),
    LX_ENTRY(211, LxSysSendmsg, "sendmsg"),
    LX_ENTRY(212, LxSysRecvmsg, "recvmsg"),
    LX_ENTRY(213, LxSysSucceed, "readahead"),
    LX_ENTRY(214, LxSysBrk, "brk"),
    LX_ENTRY(215, LxSysMunmap, "munmap"),
    LX_ENTRY(216, LxSysMremap, "mremap"),
    LX_ENTRY(217, LxSysKeyctl, "add_key"),
    LX_ENTRY(218, LxSysKeyctl, "request_key"),
    LX_ENTRY(219, LxSysKeyctl, "keyctl"),
    LX_ENTRY(220, LxSysClone, "clone"),
    LX_ENTRY(221, LxSysExecve, "execve"),
    LX_ENTRY(222, LxSysMmap, "mmap"),
    LX_ENTRY(223, LxSysSucceed, "fadvise64"),
    LX_ENTRY(226, LxSysMprotect, "mprotect"),
    LX_ENTRY(227, LxSysSucceed, "msync"),
    LX_ENTRY(228, LxSysSucceed, "mlock"),
    LX_ENTRY(229, LxSysSucceed, "munlock"),
    LX_ENTRY(230, LxSysSucceed, "mlockall"),
    LX_ENTRY(231, LxSysSucceed, "munlockall"),
    LX_ENTRY(232, LxSysMincore, "mincore"),
    LX_ENTRY(233, LxSysSucceed, "madvise"),
    LX_ENTRY(240, LxSysRtTgsigqueueinfo, "rt_tgsigqueueinfo"),
    LX_ENTRY(242, LxSysAccept4, "accept4"),
    LX_ENTRY(243, LxSysRecvmmsg, "recvmmsg"),
    LX_ENTRY(260, LxSysWait4, "wait4"),
    LX_ENTRY(261, LxSysPrlimit64, "prlimit64"),
    LX_ENTRY(264, LxSysNameToHandleAt, "name_to_handle_at"),
    LX_ENTRY(267, LxSysSucceed, "syncfs"),
    LX_ENTRY(268, NULL, "setns"),
    LX_ENTRY(269, LxSysSendmmsg, "sendmmsg"),
    LX_ENTRY(276, LxSysRenameat2, "renameat2"),
    LX_ENTRY(277, NULL, "seccomp"),
    LX_ENTRY(278, LxSysGetrandom, "getrandom"),
    LX_ENTRY(279, LxSysMemfdCreate, "memfd_create"),
    LX_ENTRY(280, NULL, "bpf"),
    LX_ENTRY(281, LxSysExecveat, "execveat"),
    LX_ENTRY(283, NULL, "membarrier"),
    LX_ENTRY(284, LxSysSucceed, "mlock2"),
    LX_ENTRY(285, LxSysCopyFileRange, "copy_file_range"),
    LX_ENTRY(286, LxSysPreadv, "preadv2"),
    LX_ENTRY(287, LxSysPwritev, "pwritev2"),
    LX_ENTRY(291, LxSysStatx, "statx"),
    LX_ENTRY(293, NULL, "rseq"),
    LX_ENTRY(424, LxSysPidfdSendSignal, "pidfd_send_signal"),
    LX_ENTRY(428, NULL, "open_tree"),
    LX_ENTRY(429, NULL, "move_mount"),
    LX_ENTRY(430, NULL, "fsopen"),
    LX_ENTRY(431, NULL, "fsconfig"),
    LX_ENTRY(432, NULL, "fsmount"),
    LX_ENTRY(433, NULL, "fspick"),
    LX_ENTRY(434, LxSysPidfdOpen, "pidfd_open"),
    LX_ENTRY(435, LxSysClone3, "clone3"),
    LX_ENTRY(436, LxSysCloseRange, "close_range"),
    LX_ENTRY(437, NULL, "openat2"),
    LX_ENTRY(438, LxSysPidfdGetfd, "pidfd_getfd"),
    LX_ENTRY(439, LxSysFaccessat2, "faccessat2"),
    LX_ENTRY(441, LxSysEpollPwait2, "epoll_pwait2"),
    LX_ENTRY(442, NULL, "mount_setattr"),
    LX_ENTRY(452, LxSysFchmodat, "fchmodat2"),
};

PLX_SYSCALL_ENTRY
LxArchSyscallEntry(
    _In_ ULONG Number)
{
    if (Number >= RTL_NUMBER_OF(LxArm64Syscalls))
        return NULL;
    return &LxArm64Syscalls[Number];
}

ULONG
LxArchElfMachine(VOID)
{
    return LX_EM_AARCH64;
}

ULONG64
LxArchHwcap(VOID)
{
    return LX_HWCAP_FP | LX_HWCAP_ASIMD;
}

PCSTR
LxArchPlatform(VOID)
{
    return "aarch64";
}

PCSTR
LxArchCpuinfo(VOID)
{
    return "Features\t: fp asimd\nCPU architecture: 8\n";
}

VOID
LxArchGetSyscall(
    _In_ PKTRAP_FRAME TrapFrame,
    _Out_ PULONG Number,
    _Out_writes_(6) PULONG64 Args)
{
    ULONG Index;

    *Number = (ULONG)TrapFrame->X8;
    for (Index = 0; Index < 6; Index++)
        Args[Index] = TrapFrame->X[Index];
}

VOID
LxArchSetSyscallResult(
    _Inout_ PKTRAP_FRAME TrapFrame,
    _In_ LONG64 Result)
{
    TrapFrame->X0 = (ULONG64)Result;
}

VOID
LxArchSetExecContext(
    _Inout_ PKTRAP_FRAME TrapFrame,
    _Inout_opt_ PKEXCEPTION_FRAME ExceptionFrame,
    _In_ ULONG64 Pc,
    _In_ ULONG64 Sp)
{
    RtlZeroMemory(TrapFrame->X, sizeof(TrapFrame->X));
    TrapFrame->Fp = 0;
    TrapFrame->Lr = 0;
    TrapFrame->Sp = Sp;
    TrapFrame->Pc = Pc;
    TrapFrame->Spsr = 0;

    if (ExceptionFrame != NULL)
    {
        ExceptionFrame->X19 = ExceptionFrame->X20 = ExceptionFrame->X21 = 0;
        ExceptionFrame->X22 = ExceptionFrame->X23 = ExceptionFrame->X24 = 0;
        ExceptionFrame->X25 = ExceptionFrame->X26 = ExceptionFrame->X27 = 0;
        ExceptionFrame->X28 = 0;
        ExceptionFrame->Fp = 0;
        ExceptionFrame->Lr = 0;
        ExceptionFrame->Fpcr = 0;
        ExceptionFrame->Fpsr = 0;
    }

    if (TrapFrame->VfpState != NULL)
    {
        RtlZeroMemory(TrapFrame->VfpState->V, sizeof(TrapFrame->VfpState->V));
        TrapFrame->VfpState->Fpcr = 0;
        TrapFrame->VfpState->Fpsr = 0;
    }
}

VOID
LxArchCaptureContext(
    _In_ PKTRAP_FRAME TrapFrame,
    _In_opt_ PKEXCEPTION_FRAME ExceptionFrame,
    _Out_ PCONTEXT Context)
{
    ULONG Index;

    RtlZeroMemory(Context, sizeof(*Context));
    Context->ContextFlags = CONTEXT_CONTROL | CONTEXT_INTEGER | CONTEXT_FLOATING_POINT;
    Context->Cpsr = TrapFrame->Spsr;
    for (Index = 0; Index <= 18; Index++)
        Context->X[Index] = TrapFrame->X[Index];

    if (ExceptionFrame != NULL)
    {
        Context->X19 = ExceptionFrame->X19;
        Context->X20 = ExceptionFrame->X20;
        Context->X21 = ExceptionFrame->X21;
        Context->X22 = ExceptionFrame->X22;
        Context->X23 = ExceptionFrame->X23;
        Context->X24 = ExceptionFrame->X24;
        Context->X25 = ExceptionFrame->X25;
        Context->X26 = ExceptionFrame->X26;
        Context->X27 = ExceptionFrame->X27;
        Context->X28 = ExceptionFrame->X28;
        Context->Fp = ExceptionFrame->Fp;
        Context->Lr = ExceptionFrame->Lr;
    }
    else
    {
        Context->Fp = TrapFrame->Fp;
        Context->Lr = TrapFrame->Lr;
    }

    Context->Sp = TrapFrame->Sp;
    Context->Pc = TrapFrame->Pc;

    if (TrapFrame->VfpState != NULL)
    {
        Context->Fpcr = TrapFrame->VfpState->Fpcr;
        Context->Fpsr = TrapFrame->VfpState->Fpsr;
        RtlCopyMemory(Context->V, TrapFrame->VfpState->V, sizeof(Context->V));
    }
}

VOID
LxArchSetForkContext(
    _Inout_ PCONTEXT Context,
    _In_ ULONG64 StackPointer)
{
    Context->X0 = 0;
    if (StackPointer != 0)
        Context->Sp = StackPointer;
}

VOID
LxArchSetThreadPointer(
    _In_ ULONG64 Value)
{
    __asm__ __volatile__("msr tpidr_el0, %0" : : "r"(Value) : "memory");
}

ULONG64
LxArchGetThreadPointer(VOID)
{
    ULONG64 Value;

    __asm__ __volatile__("mrs %0, tpidr_el0" : "=r"(Value));
    return Value;
}

ULONG64
LxArchGetUserPc(
    _In_ PKTRAP_FRAME TrapFrame)
{
    return TrapFrame->Pc;
}

ULONG64
LxArchGetUserSp(
    _In_ PKTRAP_FRAME TrapFrame)
{
    return TrapFrame->Sp;
}

ULONG
LxArchSigTrampoline(
    _Out_writes_bytes_(16) PUCHAR Code)
{
    static const ULONG Instructions[2] = { 0xD2801168, 0xD4000001 };

    RtlCopyMemory(Code, Instructions, sizeof(Instructions));
    return sizeof(Instructions);
}

static
VOID
LxArm64SetFpLr(
    _Inout_ PKTRAP_FRAME TrapFrame,
    _Inout_opt_ PKEXCEPTION_FRAME ExceptionFrame,
    _In_ ULONG64 Fp,
    _In_ ULONG64 Lr)
{
    TrapFrame->Fp = Fp;
    TrapFrame->Lr = Lr;
    if (ExceptionFrame != NULL)
    {
        ExceptionFrame->Fp = Fp;
        ExceptionFrame->Lr = Lr;
    }
}

BOOLEAN
LxArchSetupSignalFrame(
    _Inout_ PKTRAP_FRAME TrapFrame,
    _Inout_opt_ PKEXCEPTION_FRAME ExceptionFrame,
    _In_ ULONG64 StackTop,
    _In_ LONG Signal,
    _In_ PLX_SIGINFO Info,
    _In_ PLX_SIGACTION Action,
    _In_ ULONG64 Restorer,
    _In_ ULONG64 SavedMask,
    _In_ PLX_STACK AltStack)
{
    CONTEXT Context;
    PUCHAR Buffer;
    ULONG64 Record[2];
    ULONG64 Next, Frame;
    PULONG Fpsimd;
    LONG64 Error;

    LxArchCaptureContext(TrapFrame, ExceptionFrame, &Context);

    Next = LX_ALIGN_DOWN(StackTop - sizeof(Record), 16);
    Frame = Next - LX_A64_FRAME_SIZE;

    Buffer = ExAllocatePoolZero(PagedPool, LX_A64_FRAME_SIZE, LX_TAG);
    if (Buffer == NULL)
        return FALSE;

    RtlCopyMemory(Buffer, Info, sizeof(*Info));
    ((PLX_SIGINFO)Buffer)->Signo = Signal;
    RtlCopyMemory(Buffer + LX_A64_UC_STACK, AltStack, sizeof(*AltStack));
    *(PULONG64)(Buffer + LX_A64_UC_SIGMASK) = SavedMask;

    if ((Signal == LX_SIGSEGV) || (Signal == LX_SIGBUS) || (Signal == LX_SIGILL) ||
        (Signal == LX_SIGFPE) || (Signal == LX_SIGTRAP))
    {
        *(PULONG64)(Buffer + LX_A64_MC_FAULT) = Info->Address;
    }
    RtlCopyMemory(Buffer + LX_A64_MC_REGS, Context.X, sizeof(Context.X));
    *(PULONG64)(Buffer + LX_A64_MC_SP) = Context.Sp;
    *(PULONG64)(Buffer + LX_A64_MC_PC) = Context.Pc;
    *(PULONG64)(Buffer + LX_A64_MC_PSTATE) = Context.Cpsr;

    Fpsimd = (PULONG)(Buffer + LX_A64_MC_RESERVED);
    Fpsimd[0] = LX_A64_FPSIMD_MAGIC;
    Fpsimd[1] = LX_A64_FPSIMD_SIZE;
    Fpsimd[2] = Context.Fpsr;
    Fpsimd[3] = Context.Fpcr;
    RtlCopyMemory(&Fpsimd[4], Context.V, sizeof(Context.V));

    Record[0] = Context.Fp;
    Record[1] = Context.Lr;

    Error = LxCopyToUser(Frame, Buffer, LX_A64_FRAME_SIZE);
    if (Error == 0)
        Error = LxCopyToUser(Next, Record, sizeof(Record));
    ExFreePoolWithTag(Buffer, LX_TAG);
    if (Error != 0)
        return FALSE;

    TrapFrame->X0 = (ULONG64)Signal;
    TrapFrame->X1 = Frame;
    TrapFrame->X2 = Frame + LX_A64_UC;
    TrapFrame->Sp = Frame;
    TrapFrame->Pc = Action->Handler;
    LxArm64SetFpLr(TrapFrame, ExceptionFrame, Next, Restorer);
    return TRUE;
}

LONG64
LxArchSignalReturn(
    _Inout_ PKTRAP_FRAME TrapFrame,
    _Inout_opt_ PKEXCEPTION_FRAME ExceptionFrame,
    _Out_ PULONG64 Mask)
{
    ULONG64 Frame = TrapFrame->Sp;
    PULONG64 Regs;
    PULONG Fpsimd;
    PUCHAR Buffer;
    LONG64 Error;
    ULONG Index;

    *Mask = 0;
    if (Frame & 15)
        return -LX_EFAULT;

    Buffer = ExAllocatePoolZero(PagedPool, LX_A64_FRAME_SIZE, LX_TAG);
    if (Buffer == NULL)
        return -LX_ENOMEM;

    Error = LxCopyFromUser(Buffer, Frame, LX_A64_FRAME_SIZE);
    Fpsimd = (PULONG)(Buffer + LX_A64_MC_RESERVED);
    if ((Error == 0) && ((Fpsimd[0] != LX_A64_FPSIMD_MAGIC) || (Fpsimd[1] != LX_A64_FPSIMD_SIZE)))
        Error = -LX_EINVAL;
    if (Error != 0)
    {
        ExFreePoolWithTag(Buffer, LX_TAG);
        return Error;
    }

    Regs = (PULONG64)(Buffer + LX_A64_MC_REGS);
    for (Index = 0; Index <= 18; Index++)
        TrapFrame->X[Index] = Regs[Index];
    if (ExceptionFrame != NULL)
    {
        ExceptionFrame->X19 = Regs[19];
        ExceptionFrame->X20 = Regs[20];
        ExceptionFrame->X21 = Regs[21];
        ExceptionFrame->X22 = Regs[22];
        ExceptionFrame->X23 = Regs[23];
        ExceptionFrame->X24 = Regs[24];
        ExceptionFrame->X25 = Regs[25];
        ExceptionFrame->X26 = Regs[26];
        ExceptionFrame->X27 = Regs[27];
        ExceptionFrame->X28 = Regs[28];
    }
    LxArm64SetFpLr(TrapFrame, ExceptionFrame, Regs[29], Regs[30]);
    TrapFrame->Sp = *(PULONG64)(Buffer + LX_A64_MC_SP);
    TrapFrame->Pc = *(PULONG64)(Buffer + LX_A64_MC_PC);
    TrapFrame->Spsr = (TrapFrame->Spsr & ~LX_A64_PSTATE_NZCV) |
                      ((ULONG)*(PULONG64)(Buffer + LX_A64_MC_PSTATE) & LX_A64_PSTATE_NZCV);

    if (TrapFrame->VfpState != NULL)
    {
        TrapFrame->VfpState->Fpsr = Fpsimd[2];
        TrapFrame->VfpState->Fpcr = Fpsimd[3];
        RtlCopyMemory(TrapFrame->VfpState->V, &Fpsimd[4], sizeof(TrapFrame->VfpState->V));
    }

    *Mask = *(PULONG64)(Buffer + LX_A64_UC_SIGMASK);
    ExFreePoolWithTag(Buffer, LX_TAG);
    return 0;
}
