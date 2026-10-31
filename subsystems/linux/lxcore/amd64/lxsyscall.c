/*
 * PROJECT:     LiberNT Linux Subsystem
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     System call table of the x86-64 Linux system call numbering
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "../lxcore.h"

#define LX_ENTRY(Number, Handler, Name) [Number] = { Handler, Name }

#define LX_ARCH_SET_GS      0x1001
#define LX_ARCH_SET_FS      0x1002
#define LX_ARCH_GET_FS      0x1003
#define LX_ARCH_GET_GS      0x1004
#define LX_ARCH_GET_CPUID   0x1011
#define LX_ARCH_SET_CPUID   0x1012
#define LX_USER_ADDRESS_END 0x800000000000ULL

static
VOID
LxAmd64ShiftArgs(
    _Inout_ PLX_CALL Call,
    _In_ ULONG64 First)
{
    ULONG Index;

    for (Index = 5; Index > 0; Index--)
        Call->Args[Index] = Call->Args[Index - 1];
    Call->Args[0] = First;
}

static
LX_SYSCALL(LxAmd64Open)
{
    LxAmd64ShiftArgs(Call, (ULONG64)(LONG64)LX_AT_FDCWD);
    return LxSysOpenat(Call);
}

static
LX_SYSCALL(LxAmd64Creat)
{
    Call->Args[2] = Call->Args[1];
    Call->Args[1] = LX_O_CREAT | LX_O_WRONLY | LX_O_TRUNC;
    return LxAmd64Open(Call);
}

static
LX_SYSCALL(LxAmd64Stat)
{
    LxAmd64ShiftArgs(Call, (ULONG64)(LONG64)LX_AT_FDCWD);
    Call->Args[3] = 0;
    return LxSysNewfstatat(Call);
}

static
LX_SYSCALL(LxAmd64Lstat)
{
    LxAmd64ShiftArgs(Call, (ULONG64)(LONG64)LX_AT_FDCWD);
    Call->Args[3] = LX_AT_SYMLINK_NOFOLLOW;
    return LxSysNewfstatat(Call);
}

static
LX_SYSCALL(LxAmd64Access)
{
    LxAmd64ShiftArgs(Call, (ULONG64)(LONG64)LX_AT_FDCWD);
    Call->Args[3] = 0;
    return LxSysFaccessat(Call);
}

static
LX_SYSCALL(LxAmd64Pipe)
{
    Call->Args[1] = 0;
    return LxSysPipe2(Call);
}

static
LX_SYSCALL(LxAmd64Dup2)
{
    if ((LONG)Call->Args[0] == (LONG)Call->Args[1])
    {
        PLX_FILE File = LxFdGet(Call->Process, (LONG)Call->Args[0]);

        if (File == NULL)
            return -LX_EBADF;
        LxFileDereference(File);
        return (LONG)Call->Args[1];
    }
    Call->Args[2] = 0;
    return LxSysDup3(Call);
}

static
LX_SYSCALL(LxAmd64Clone)
{
    ULONG64 ChildTid = Call->Args[3];

    Call->Args[3] = Call->Args[4];
    Call->Args[4] = ChildTid;
    return LxSysClone(Call);
}

static
LX_SYSCALL(LxAmd64Fork)
{
    RtlZeroMemory(Call->Args, sizeof(Call->Args));
    Call->Args[0] = LX_SIGCHLD;
    return LxSysClone(Call);
}

static
LX_SYSCALL(LxAmd64Vfork)
{
    RtlZeroMemory(Call->Args, sizeof(Call->Args));
    Call->Args[0] = LX_CLONE_VM | LX_CLONE_VFORK | LX_SIGCHLD;
    return LxSysClone(Call);
}

static
LX_SYSCALL(LxAmd64Rename)
{
    Call->Args[3] = Call->Args[1];
    Call->Args[2] = (ULONG64)(LONG64)LX_AT_FDCWD;
    Call->Args[1] = Call->Args[0];
    Call->Args[0] = (ULONG64)(LONG64)LX_AT_FDCWD;
    return LxSysRenameat(Call);
}

static
LX_SYSCALL(LxAmd64Mkdir)
{
    LxAmd64ShiftArgs(Call, (ULONG64)(LONG64)LX_AT_FDCWD);
    return LxSysMkdirat(Call);
}

static
LX_SYSCALL(LxAmd64Rmdir)
{
    LxAmd64ShiftArgs(Call, (ULONG64)(LONG64)LX_AT_FDCWD);
    Call->Args[2] = LX_AT_REMOVEDIR;
    return LxSysUnlinkat(Call);
}

static
LX_SYSCALL(LxAmd64Unlink)
{
    LxAmd64ShiftArgs(Call, (ULONG64)(LONG64)LX_AT_FDCWD);
    Call->Args[2] = 0;
    return LxSysUnlinkat(Call);
}

static
LX_SYSCALL(LxAmd64Link)
{
    Call->Args[3] = Call->Args[1];
    Call->Args[2] = (ULONG64)(LONG64)LX_AT_FDCWD;
    Call->Args[1] = Call->Args[0];
    Call->Args[0] = (ULONG64)(LONG64)LX_AT_FDCWD;
    Call->Args[4] = 0;
    return LxSysLinkat(Call);
}

static
LX_SYSCALL(LxAmd64Symlink)
{
    Call->Args[2] = Call->Args[1];
    Call->Args[1] = (ULONG64)(LONG64)LX_AT_FDCWD;
    return LxSysSymlinkat(Call);
}

static
LX_SYSCALL(LxAmd64Readlink)
{
    LxAmd64ShiftArgs(Call, (ULONG64)(LONG64)LX_AT_FDCWD);
    return LxSysReadlinkat(Call);
}

static
LX_SYSCALL(LxAmd64Chmod)
{
    LxAmd64ShiftArgs(Call, (ULONG64)(LONG64)LX_AT_FDCWD);
    Call->Args[3] = 0;
    return LxSysFchmodat(Call);
}

static
LX_SYSCALL(LxAmd64Chown)
{
    LxAmd64ShiftArgs(Call, (ULONG64)(LONG64)LX_AT_FDCWD);
    Call->Args[4] = 0;
    return LxSysFchownat(Call);
}

static
LX_SYSCALL(LxAmd64Lchown)
{
    LxAmd64ShiftArgs(Call, (ULONG64)(LONG64)LX_AT_FDCWD);
    Call->Args[4] = LX_AT_SYMLINK_NOFOLLOW;
    return LxSysFchownat(Call);
}

static
LX_SYSCALL(LxAmd64Mknod)
{
    LxAmd64ShiftArgs(Call, (ULONG64)(LONG64)LX_AT_FDCWD);
    return LxSysMknodat(Call);
}

static
LX_SYSCALL(LxAmd64Getpgrp)
{
    Call->Args[0] = 0;
    return LxSysGetpgid(Call);
}

static
LX_SYSCALL(LxAmd64EpollCreate)
{
    if ((LONG)Call->Args[0] <= 0)
        return -LX_EINVAL;
    Call->Args[0] = 0;
    return LxSysEpollCreate1(Call);
}

static
LX_SYSCALL(LxAmd64EpollWait)
{
    Call->Args[4] = 0;
    Call->Args[5] = 0;
    return LxSysEpollPwait(Call);
}

static
LX_SYSCALL(LxAmd64InotifyInit)
{
    Call->Args[0] = 0;
    return LxSysInotifyInit1(Call);
}

static
LX_SYSCALL(LxAmd64Signalfd)
{
    Call->Args[3] = 0;
    return LxSysSignalfd4(Call);
}

static
LX_SYSCALL(LxAmd64Eventfd)
{
    Call->Args[1] = 0;
    return LxSysEventfd2(Call);
}

static
LX_SYSCALL(LxAmd64RestartSyscall)
{
    UNREFERENCED_PARAMETER(Call);
    return -LX_EINTR;
}

static
LX_SYSCALL(LxAmd64Iopl)
{
    return ((ULONG)Call->Args[0] == 0) ? 0 : -LX_EPERM;
}

static
LX_SYSCALL(LxAmd64Ioperm)
{
    return (Call->Args[2] == 0) ? 0 : -LX_EPERM;
}

static
LX_SYSCALL(LxAmd64ArchPrctl)
{
    ULONG64 Address = Call->Args[1];
    PETHREAD Thread = PsGetCurrentThread();
    ULONG64 Value;

    switch ((ULONG)Call->Args[0])
    {
        case LX_ARCH_SET_FS:
            if (Address >= LX_USER_ADDRESS_END)
                return -LX_EPERM;
            LxArchSetThreadPointer(Call->TrapFrame, Address);
            return 0;

        case LX_ARCH_SET_GS:
            if (Address >= LX_USER_ADDRESS_END)
                return -LX_EPERM;
            Thread->UserGsBase = Address;
            __writemsr(MSR_GS_SWAP, Address);
            return 0;

        case LX_ARCH_GET_FS:
            Value = Thread->UserFsBase;
            return LxCopyToUser(Address, &Value, sizeof(Value));

        case LX_ARCH_GET_GS:
            Value = Thread->UserGsBase;
            return LxCopyToUser(Address, &Value, sizeof(Value));

        case LX_ARCH_GET_CPUID:
            return 1;

        case LX_ARCH_SET_CPUID:
            return -LX_ENODEV;

        default:
            return -LX_EINVAL;
    }
}

static LX_SYSCALL_ENTRY LxAmd64Syscalls[] =
{
    LX_ENTRY(0, LxSysRead, "read"),
    LX_ENTRY(1, LxSysWrite, "write"),
    LX_ENTRY(2, LxAmd64Open, "open"),
    LX_ENTRY(3, LxSysClose, "close"),
    LX_ENTRY(4, LxAmd64Stat, "stat"),
    LX_ENTRY(5, LxSysFstat, "fstat"),
    LX_ENTRY(6, LxAmd64Lstat, "lstat"),
    LX_ENTRY(7, LxSysPoll, "poll"),
    LX_ENTRY(8, LxSysLseek, "lseek"),
    LX_ENTRY(9, LxSysMmap, "mmap"),
    LX_ENTRY(10, LxSysMprotect, "mprotect"),
    LX_ENTRY(11, LxSysMunmap, "munmap"),
    LX_ENTRY(12, LxSysBrk, "brk"),
    LX_ENTRY(13, LxSysRtSigaction, "rt_sigaction"),
    LX_ENTRY(14, LxSysRtSigprocmask, "rt_sigprocmask"),
    LX_ENTRY(15, LxSysRtSigreturn, "rt_sigreturn"),
    LX_ENTRY(16, LxSysIoctl, "ioctl"),
    LX_ENTRY(17, LxSysPread64, "pread64"),
    LX_ENTRY(18, LxSysPwrite64, "pwrite64"),
    LX_ENTRY(19, LxSysReadv, "readv"),
    LX_ENTRY(20, LxSysWritev, "writev"),
    LX_ENTRY(21, LxAmd64Access, "access"),
    LX_ENTRY(22, LxAmd64Pipe, "pipe"),
    LX_ENTRY(23, LxSysSelect, "select"),
    LX_ENTRY(24, LxSysSchedYield, "sched_yield"),
    LX_ENTRY(25, LxSysMremap, "mremap"),
    LX_ENTRY(26, LxSysMsync, "msync"),
    LX_ENTRY(27, LxSysMincore, "mincore"),
    LX_ENTRY(28, LxSysMadvise, "madvise"),
    LX_ENTRY(29, LxSysShmget, "shmget"),
    LX_ENTRY(30, LxSysShmat, "shmat"),
    LX_ENTRY(31, LxSysShmctl, "shmctl"),
    LX_ENTRY(32, LxSysDup, "dup"),
    LX_ENTRY(33, LxAmd64Dup2, "dup2"),
    LX_ENTRY(34, LxSysPause, "pause"),
    LX_ENTRY(35, LxSysNanosleep, "nanosleep"),
    LX_ENTRY(36, LxSysGetitimer, "getitimer"),
    LX_ENTRY(37, LxSysAlarm, "alarm"),
    LX_ENTRY(38, LxSysSetitimer, "setitimer"),
    LX_ENTRY(39, LxSysGetpid, "getpid"),
    LX_ENTRY(40, LxSysSendfile, "sendfile"),
    LX_ENTRY(41, LxSysSocket, "socket"),
    LX_ENTRY(42, LxSysConnect, "connect"),
    LX_ENTRY(43, LxSysAccept, "accept"),
    LX_ENTRY(44, LxSysSendto, "sendto"),
    LX_ENTRY(45, LxSysRecvfrom, "recvfrom"),
    LX_ENTRY(46, LxSysSendmsg, "sendmsg"),
    LX_ENTRY(47, LxSysRecvmsg, "recvmsg"),
    LX_ENTRY(48, LxSysShutdown, "shutdown"),
    LX_ENTRY(49, LxSysBind, "bind"),
    LX_ENTRY(50, LxSysListen, "listen"),
    LX_ENTRY(51, LxSysGetsockname, "getsockname"),
    LX_ENTRY(52, LxSysGetpeername, "getpeername"),
    LX_ENTRY(53, LxSysSocketpair, "socketpair"),
    LX_ENTRY(54, LxSysSetsockopt, "setsockopt"),
    LX_ENTRY(55, LxSysGetsockopt, "getsockopt"),
    LX_ENTRY(56, LxAmd64Clone, "clone"),
    LX_ENTRY(57, LxAmd64Fork, "fork"),
    LX_ENTRY(58, LxAmd64Vfork, "vfork"),
    LX_ENTRY(59, LxSysExecve, "execve"),
    LX_ENTRY(60, LxSysExit, "exit"),
    LX_ENTRY(61, LxSysWait4, "wait4"),
    LX_ENTRY(62, LxSysKill, "kill"),
    LX_ENTRY(63, LxSysUname, "uname"),
    LX_ENTRY(64, LxSysSemget, "semget"),
    LX_ENTRY(65, LxSysSemop, "semop"),
    LX_ENTRY(66, LxSysSemctl, "semctl"),
    LX_ENTRY(67, LxSysShmdt, "shmdt"),
    LX_ENTRY(68, LxSysMsgget, "msgget"),
    LX_ENTRY(69, LxSysMsgsnd, "msgsnd"),
    LX_ENTRY(70, LxSysMsgrcv, "msgrcv"),
    LX_ENTRY(71, LxSysMsgctl, "msgctl"),
    LX_ENTRY(72, LxSysFcntl, "fcntl"),
    LX_ENTRY(73, LxSysFlock, "flock"),
    LX_ENTRY(74, LxSysFsync, "fsync"),
    LX_ENTRY(75, LxSysFsync, "fdatasync"),
    LX_ENTRY(76, LxSysTruncate, "truncate"),
    LX_ENTRY(77, LxSysFtruncate, "ftruncate"),
    LX_ENTRY(78, LxSysGetdents, "getdents"),
    LX_ENTRY(79, LxSysGetcwd, "getcwd"),
    LX_ENTRY(80, LxSysChdir, "chdir"),
    LX_ENTRY(81, LxSysFchdir, "fchdir"),
    LX_ENTRY(82, LxAmd64Rename, "rename"),
    LX_ENTRY(83, LxAmd64Mkdir, "mkdir"),
    LX_ENTRY(84, LxAmd64Rmdir, "rmdir"),
    LX_ENTRY(85, LxAmd64Creat, "creat"),
    LX_ENTRY(86, LxAmd64Link, "link"),
    LX_ENTRY(87, LxAmd64Unlink, "unlink"),
    LX_ENTRY(88, LxAmd64Symlink, "symlink"),
    LX_ENTRY(89, LxAmd64Readlink, "readlink"),
    LX_ENTRY(90, LxAmd64Chmod, "chmod"),
    LX_ENTRY(91, LxSysFchmod, "fchmod"),
    LX_ENTRY(92, LxAmd64Chown, "chown"),
    LX_ENTRY(93, LxSysFchown, "fchown"),
    LX_ENTRY(94, LxAmd64Lchown, "lchown"),
    LX_ENTRY(95, LxSysUmask, "umask"),
    LX_ENTRY(96, LxSysGettimeofday, "gettimeofday"),
    LX_ENTRY(97, LxSysGetrlimit, "getrlimit"),
    LX_ENTRY(98, LxSysGetrusage, "getrusage"),
    LX_ENTRY(99, LxSysSysinfo, "sysinfo"),
    LX_ENTRY(100, LxSysTimes, "times"),
    LX_ENTRY(101, LxSysPtrace, "ptrace"),
    LX_ENTRY(102, LxSysGetuid, "getuid"),
    LX_ENTRY(103, LxSysSyslog, "syslog"),
    LX_ENTRY(104, LxSysGetgid, "getgid"),
    LX_ENTRY(105, LxSysSetuid, "setuid"),
    LX_ENTRY(106, LxSysSetgid, "setgid"),
    LX_ENTRY(107, LxSysGeteuid, "geteuid"),
    LX_ENTRY(108, LxSysGetegid, "getegid"),
    LX_ENTRY(109, LxSysSetpgid, "setpgid"),
    LX_ENTRY(110, LxSysGetppid, "getppid"),
    LX_ENTRY(111, LxAmd64Getpgrp, "getpgrp"),
    LX_ENTRY(112, LxSysSetsid, "setsid"),
    LX_ENTRY(113, LxSysSetreuid, "setreuid"),
    LX_ENTRY(114, LxSysSetregid, "setregid"),
    LX_ENTRY(115, LxSysGetgroups, "getgroups"),
    LX_ENTRY(116, LxSysSetgroups, "setgroups"),
    LX_ENTRY(117, LxSysSetresuid, "setresuid"),
    LX_ENTRY(118, LxSysGetresuid, "getresuid"),
    LX_ENTRY(119, LxSysSetresgid, "setresgid"),
    LX_ENTRY(120, LxSysGetresgid, "getresgid"),
    LX_ENTRY(121, LxSysGetpgid, "getpgid"),
    LX_ENTRY(122, LxSysSetfsuid, "setfsuid"),
    LX_ENTRY(123, LxSysSetfsgid, "setfsgid"),
    LX_ENTRY(124, LxSysGetsid, "getsid"),
    LX_ENTRY(125, LxSysCapget, "capget"),
    LX_ENTRY(126, LxSysCapset, "capset"),
    LX_ENTRY(127, LxSysRtSigpending, "rt_sigpending"),
    LX_ENTRY(128, LxSysRtSigtimedwait, "rt_sigtimedwait"),
    LX_ENTRY(129, LxSysRtSigqueueinfo, "rt_sigqueueinfo"),
    LX_ENTRY(130, LxSysRtSigsuspend, "rt_sigsuspend"),
    LX_ENTRY(131, LxSysSigaltstack, "sigaltstack"),
    LX_ENTRY(132, LxSysUtime, "utime"),
    LX_ENTRY(133, LxAmd64Mknod, "mknod"),
    LX_ENTRY(135, LxSysPersonality, "personality"),
    LX_ENTRY(137, LxSysStatfs, "statfs"),
    LX_ENTRY(138, LxSysFstatfs, "fstatfs"),
    LX_ENTRY(140, LxSysGetpriority, "getpriority"),
    LX_ENTRY(141, LxSysSetpriority, "setpriority"),
    LX_ENTRY(142, LxSysSchedSetparam, "sched_setparam"),
    LX_ENTRY(143, LxSysSchedGetparam, "sched_getparam"),
    LX_ENTRY(144, LxSysSchedSetscheduler, "sched_setscheduler"),
    LX_ENTRY(145, LxSysSchedGetscheduler, "sched_getscheduler"),
    LX_ENTRY(146, LxSysSchedGetPriorityMax, "sched_get_priority_max"),
    LX_ENTRY(147, LxSysSchedGetPriorityMin, "sched_get_priority_min"),
    LX_ENTRY(149, LxSysMlock, "mlock"),
    LX_ENTRY(150, LxSysMunlock, "munlock"),
    LX_ENTRY(151, LxSysMlockall, "mlockall"),
    LX_ENTRY(152, LxSysMunlockall, "munlockall"),
    LX_ENTRY(153, LxSysSucceed, "vhangup"),
    LX_ENTRY(155, LxSysPivotRoot, "pivot_root"),
    LX_ENTRY(157, LxSysPrctl, "prctl"),
    LX_ENTRY(158, LxAmd64ArchPrctl, "arch_prctl"),
    LX_ENTRY(159, LxSysAdjtimex, "adjtimex"),
    LX_ENTRY(160, LxSysSetrlimit, "setrlimit"),
    LX_ENTRY(161, LxSysChroot, "chroot"),
    LX_ENTRY(162, LxSysSync, "sync"),
    LX_ENTRY(164, LxSysSettimeofday, "settimeofday"),
    LX_ENTRY(165, LxSysMount, "mount"),
    LX_ENTRY(166, LxSysUmount2, "umount2"),
    LX_ENTRY(169, LxSysReboot, "reboot"),
    LX_ENTRY(170, LxSysSethostname, "sethostname"),
    LX_ENTRY(171, LxSysSetdomainname, "setdomainname"),
    LX_ENTRY(172, LxAmd64Iopl, "iopl"),
    LX_ENTRY(173, LxAmd64Ioperm, "ioperm"),
    LX_ENTRY(186, LxSysGettid, "gettid"),
    LX_ENTRY(187, LxSysSucceed, "readahead"),
    LX_ENTRY(188, LxSysSetxattr, "setxattr"),
    LX_ENTRY(189, LxSysLsetxattr, "lsetxattr"),
    LX_ENTRY(190, LxSysFsetxattr, "fsetxattr"),
    LX_ENTRY(191, LxSysGetxattr, "getxattr"),
    LX_ENTRY(192, LxSysLgetxattr, "lgetxattr"),
    LX_ENTRY(193, LxSysFgetxattr, "fgetxattr"),
    LX_ENTRY(194, LxSysListxattr, "listxattr"),
    LX_ENTRY(195, LxSysLlistxattr, "llistxattr"),
    LX_ENTRY(196, LxSysFlistxattr, "flistxattr"),
    LX_ENTRY(197, LxSysRemovexattr, "removexattr"),
    LX_ENTRY(198, LxSysLremovexattr, "lremovexattr"),
    LX_ENTRY(199, LxSysFremovexattr, "fremovexattr"),
    LX_ENTRY(200, LxSysTkill, "tkill"),
    LX_ENTRY(201, LxSysTime, "time"),
    LX_ENTRY(202, LxSysFutex, "futex"),
    LX_ENTRY(203, LxSysSchedSetaffinity, "sched_setaffinity"),
    LX_ENTRY(204, LxSysSchedGetaffinity, "sched_getaffinity"),
    LX_ENTRY(213, LxAmd64EpollCreate, "epoll_create"),
    LX_ENTRY(216, LxSysRemapFilePages, "remap_file_pages"),
    LX_ENTRY(217, LxSysGetdents64, "getdents64"),
    LX_ENTRY(218, LxSysSetTidAddress, "set_tid_address"),
    LX_ENTRY(219, LxAmd64RestartSyscall, "restart_syscall"),
    LX_ENTRY(220, LxSysSemtimedop, "semtimedop"),
    LX_ENTRY(221, LxSysSucceed, "fadvise64"),
    LX_ENTRY(222, LxSysTimerCreate, "timer_create"),
    LX_ENTRY(223, LxSysTimerSettime, "timer_settime"),
    LX_ENTRY(224, LxSysTimerGettime, "timer_gettime"),
    LX_ENTRY(225, LxSysTimerGetoverrun, "timer_getoverrun"),
    LX_ENTRY(226, LxSysTimerDelete, "timer_delete"),
    LX_ENTRY(227, LxSysClockSettime, "clock_settime"),
    LX_ENTRY(228, LxSysClockGettime, "clock_gettime"),
    LX_ENTRY(229, LxSysClockGetres, "clock_getres"),
    LX_ENTRY(230, LxSysClockNanosleep, "clock_nanosleep"),
    LX_ENTRY(231, LxSysExitGroup, "exit_group"),
    LX_ENTRY(232, LxAmd64EpollWait, "epoll_wait"),
    LX_ENTRY(233, LxSysEpollCtl, "epoll_ctl"),
    LX_ENTRY(234, LxSysTgkill, "tgkill"),
    LX_ENTRY(235, LxSysUtimes, "utimes"),
    LX_ENTRY(240, LxSysMqOpen, "mq_open"),
    LX_ENTRY(241, LxSysMqUnlink, "mq_unlink"),
    LX_ENTRY(242, LxSysMqTimedsend, "mq_timedsend"),
    LX_ENTRY(243, LxSysMqTimedreceive, "mq_timedreceive"),
    LX_ENTRY(244, LxSysMqNotify, "mq_notify"),
    LX_ENTRY(245, LxSysMqGetsetattr, "mq_getsetattr"),
    LX_ENTRY(247, LxSysWaitid, "waitid"),
    LX_ENTRY(248, LxSysAddKey, "add_key"),
    LX_ENTRY(249, LxSysRequestKey, "request_key"),
    LX_ENTRY(250, LxSysKeyctl, "keyctl"),
    LX_ENTRY(251, LxSysSucceed, "ioprio_set"),
    LX_ENTRY(252, LxSysIoprioGet, "ioprio_get"),
    LX_ENTRY(253, LxAmd64InotifyInit, "inotify_init"),
    LX_ENTRY(254, LxSysInotifyAddWatch, "inotify_add_watch"),
    LX_ENTRY(255, LxSysInotifyRmWatch, "inotify_rm_watch"),
    LX_ENTRY(257, LxSysOpenat, "openat"),
    LX_ENTRY(258, LxSysMkdirat, "mkdirat"),
    LX_ENTRY(259, LxSysMknodat, "mknodat"),
    LX_ENTRY(260, LxSysFchownat, "fchownat"),
    LX_ENTRY(261, LxSysFutimesat, "futimesat"),
    LX_ENTRY(262, LxSysNewfstatat, "newfstatat"),
    LX_ENTRY(263, LxSysUnlinkat, "unlinkat"),
    LX_ENTRY(264, LxSysRenameat, "renameat"),
    LX_ENTRY(265, LxSysLinkat, "linkat"),
    LX_ENTRY(266, LxSysSymlinkat, "symlinkat"),
    LX_ENTRY(267, LxSysReadlinkat, "readlinkat"),
    LX_ENTRY(268, LxSysFchmodat, "fchmodat"),
    LX_ENTRY(269, LxSysFaccessat, "faccessat"),
    LX_ENTRY(270, LxSysPselect6, "pselect6"),
    LX_ENTRY(271, LxSysPpoll, "ppoll"),
    LX_ENTRY(272, LxSysUnshare, "unshare"),
    LX_ENTRY(273, LxSysSetRobustList, "set_robust_list"),
    LX_ENTRY(274, LxSysGetRobustList, "get_robust_list"),
    LX_ENTRY(275, LxSysSplice, "splice"),
    LX_ENTRY(276, LxSysTee, "tee"),
    LX_ENTRY(277, LxSysSucceed, "sync_file_range"),
    LX_ENTRY(278, LxSysVmsplice, "vmsplice"),
    LX_ENTRY(280, LxSysUtimensat, "utimensat"),
    LX_ENTRY(281, LxSysEpollPwait, "epoll_pwait"),
    LX_ENTRY(282, LxAmd64Signalfd, "signalfd"),
    LX_ENTRY(283, LxSysTimerfdCreate, "timerfd_create"),
    LX_ENTRY(284, LxAmd64Eventfd, "eventfd"),
    LX_ENTRY(285, LxSysFallocate, "fallocate"),
    LX_ENTRY(286, LxSysTimerfdSettime, "timerfd_settime"),
    LX_ENTRY(287, LxSysTimerfdGettime, "timerfd_gettime"),
    LX_ENTRY(288, LxSysAccept4, "accept4"),
    LX_ENTRY(289, LxSysSignalfd4, "signalfd4"),
    LX_ENTRY(290, LxSysEventfd2, "eventfd2"),
    LX_ENTRY(291, LxSysEpollCreate1, "epoll_create1"),
    LX_ENTRY(292, LxSysDup3, "dup3"),
    LX_ENTRY(293, LxSysPipe2, "pipe2"),
    LX_ENTRY(294, LxSysInotifyInit1, "inotify_init1"),
    LX_ENTRY(295, LxSysPreadv, "preadv"),
    LX_ENTRY(296, LxSysPwritev, "pwritev"),
    LX_ENTRY(297, LxSysRtTgsigqueueinfo, "rt_tgsigqueueinfo"),
    LX_ENTRY(299, LxSysRecvmmsg, "recvmmsg"),
    LX_ENTRY(302, LxSysPrlimit64, "prlimit64"),
    LX_ENTRY(303, LxSysNameToHandleAt, "name_to_handle_at"),
    LX_ENTRY(304, LxSysOpenByHandleAt, "open_by_handle_at"),
    LX_ENTRY(305, LxSysClockAdjtime, "clock_adjtime"),
    LX_ENTRY(306, LxSysSyncfs, "syncfs"),
    LX_ENTRY(307, LxSysSendmmsg, "sendmmsg"),
    LX_ENTRY(308, LxSysSetns, "setns"),
    LX_ENTRY(309, LxSysGetcpu, "getcpu"),
    LX_ENTRY(310, LxSysProcessVmReadv, "process_vm_readv"),
    LX_ENTRY(311, LxSysProcessVmWritev, "process_vm_writev"),
    LX_ENTRY(312, LxSysKcmp, "kcmp"),
    LX_ENTRY(314, LxSysSchedSetattr, "sched_setattr"),
    LX_ENTRY(315, LxSysSchedGetattr, "sched_getattr"),
    LX_ENTRY(316, LxSysRenameat2, "renameat2"),
    LX_ENTRY(317, LxSysSeccomp, "seccomp"),
    LX_ENTRY(318, LxSysGetrandom, "getrandom"),
    LX_ENTRY(319, LxSysMemfdCreate, "memfd_create"),
    LX_ENTRY(321, LxSysBpf, "bpf"),
    LX_ENTRY(322, LxSysExecveat, "execveat"),
    LX_ENTRY(324, LxSysMembarrier, "membarrier"),
    LX_ENTRY(325, LxSysMlock2, "mlock2"),
    LX_ENTRY(326, LxSysCopyFileRange, "copy_file_range"),
    LX_ENTRY(327, LxSysPreadv, "preadv2"),
    LX_ENTRY(328, LxSysPwritev, "pwritev2"),
    LX_ENTRY(329, LxSysPkeyMprotect, "pkey_mprotect"),
    LX_ENTRY(330, LxSysPkeyAlloc, "pkey_alloc"),
    LX_ENTRY(331, LxSysPkeyFree, "pkey_free"),
    LX_ENTRY(332, LxSysStatx, "statx"),
    LX_ENTRY(334, NULL, "rseq"),
    LX_ENTRY(424, LxSysPidfdSendSignal, "pidfd_send_signal"),
    LX_ENTRY(428, LxSysOpenTree, "open_tree"),
    LX_ENTRY(429, LxSysMoveMount, "move_mount"),
    LX_ENTRY(430, LxSysFsopen, "fsopen"),
    LX_ENTRY(431, LxSysFsconfig, "fsconfig"),
    LX_ENTRY(432, LxSysFsmount, "fsmount"),
    LX_ENTRY(433, NULL, "fspick"),
    LX_ENTRY(434, LxSysPidfdOpen, "pidfd_open"),
    LX_ENTRY(435, LxSysClone3, "clone3"),
    LX_ENTRY(436, LxSysCloseRange, "close_range"),
    LX_ENTRY(437, LxSysOpenat2, "openat2"),
    LX_ENTRY(438, LxSysPidfdGetfd, "pidfd_getfd"),
    LX_ENTRY(439, LxSysFaccessat2, "faccessat2"),
    LX_ENTRY(441, LxSysEpollPwait2, "epoll_pwait2"),
    LX_ENTRY(442, LxSysMountSetattr, "mount_setattr"),
    LX_ENTRY(449, LxSysFutexWaitv, "futex_waitv"),
    LX_ENTRY(452, LxSysFchmodat, "fchmodat2"),
    LX_ENTRY(454, LxSysFutexWake, "futex_wake"),
    LX_ENTRY(455, LxSysFutexWait, "futex_wait"),
    LX_ENTRY(456, LxSysFutexRequeue, "futex_requeue"),
    LX_ENTRY(463, LxSysSetxattrat, "setxattrat"),
    LX_ENTRY(464, LxSysGetxattrat, "getxattrat"),
    LX_ENTRY(465, LxSysListxattrat, "listxattrat"),
    LX_ENTRY(466, LxSysRemovexattrat, "removexattrat"),
};

PLX_SYSCALL_ENTRY
LxAmd64SyscallEntry(
    _In_ ULONG Number)
{
    if (Number >= RTL_NUMBER_OF(LxAmd64Syscalls))
        return NULL;
    return &LxAmd64Syscalls[Number];
}
