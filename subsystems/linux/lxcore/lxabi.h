/*
 * PROJECT:     LiberNT Linux Subsystem
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Linux user ABI constants and structures (values from musl 1.2.5)
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

#include <lxarchabi.h>

#define LX_EPERM            1
#define LX_ENOENT           2
#define LX_ESRCH            3
#define LX_EINTR            4
#define LX_EIO              5
#define LX_ENXIO            6
#define LX_E2BIG            7
#define LX_ENOEXEC          8
#define LX_EBADF            9
#define LX_ECHILD           10
#define LX_EAGAIN           11
#define LX_ENOMEM           12
#define LX_EACCES           13
#define LX_EFAULT           14
#define LX_EBUSY            16
#define LX_EEXIST           17
#define LX_EXDEV            18
#define LX_ENODEV           19
#define LX_ENOTDIR          20
#define LX_EISDIR           21
#define LX_EINVAL           22
#define LX_ENFILE           23
#define LX_EMFILE           24
#define LX_ENOTTY           25
#define LX_EFBIG            27
#define LX_ENOSPC           28
#define LX_ESPIPE           29
#define LX_EROFS            30
#define LX_EPIPE            32
#define LX_ERANGE           34
#define LX_ENAMETOOLONG     36
#define LX_ENOSYS           38
#define LX_ENOTEMPTY        39
#define LX_ELOOP            40
#define LX_ELIBBAD          80
#define LX_ETIMEDOUT        110
#define LX_EIDRM            43
#define LX_ERESTARTSYS      512
#define LX_ERESTARTNOINTR   513
#define LX_ERESTARTNOHAND   514
#define LX_ENOTBLK          15
#define LX_ETXTBSY          26
#define LX_EMLINK           31
#define LX_EDOM             33
#define LX_EDEADLK          35
#define LX_ENOLCK           37
#define LX_ENOMSG           42
#define LX_ENODATA          61
#define LX_EBADMSG          74
#define LX_EOVERFLOW        75
#define LX_EUSERS           87
#define LX_ENOTSOCK         88
#define LX_EDESTADDRREQ     89
#define LX_EMSGSIZE         90
#define LX_EPROTOTYPE       91
#define LX_ENOPROTOOPT      92
#define LX_EPROTONOSUPPORT  93
#define LX_ESOCKTNOSUPPORT  94
#define LX_EOPNOTSUPP       95
#define LX_EPFNOSUPPORT     96
#define LX_EAFNOSUPPORT     97
#define LX_EADDRINUSE       98
#define LX_EADDRNOTAVAIL    99
#define LX_ENETUNREACH      101
#define LX_ECONNABORTED     103
#define LX_ECONNRESET       104
#define LX_ENOBUFS          105
#define LX_EISCONN          106
#define LX_ENOTCONN         107
#define LX_ESHUTDOWN        108
#define LX_ECONNREFUSED     111
#define LX_EHOSTUNREACH     113
#define LX_EALREADY         114
#define LX_EINPROGRESS      115
#define LX_ESTALE           116
#define LX_ENOMEDIUM        123
#define LX_ECANCELED        125
#define LX_ENOKEY           126

#define LX_O_ACCMODE        03
#define LX_O_RDONLY         00
#define LX_O_WRONLY         01
#define LX_O_RDWR           02
#define LX_O_CREAT          0100
#define LX_O_EXCL           0200
#define LX_O_NOCTTY         0400
#define LX_O_TRUNC          01000
#define LX_O_APPEND         02000
#define LX_O_NONBLOCK       04000
#define LX_O_CLOEXEC        02000000
#define LX_O_PATH           010000000
#define LX_O_DSYNC          010000
#define LX_O_ASYNC          020000
#define LX_O_NOATIME        01000000
#define LX_O_SYNC           04010000
#define LX_O_TMPFILE        (020000000 | LX_O_DIRECTORY)

#define LX_F_DUPFD          0
#define LX_F_GETFD          1
#define LX_F_SETFD          2
#define LX_F_GETFL          3
#define LX_F_SETFL          4
#define LX_F_DUPFD_CLOEXEC  1030
#define LX_FD_CLOEXEC       1
#define LX_F_GETLK          5
#define LX_F_SETLK          6
#define LX_F_SETLKW         7
#define LX_F_SETOWN         8
#define LX_F_GETOWN         9
#define LX_F_SETSIG         10
#define LX_F_GETSIG         11
#define LX_F_SETOWN_EX      15
#define LX_F_GETOWN_EX      16
#define LX_F_OWNER_TID      0
#define LX_F_OWNER_PID      1
#define LX_F_OWNER_PGRP     2
#define LX_F_OFD_GETLK      36
#define LX_F_OFD_SETLK      37
#define LX_F_OFD_SETLKW     38
#define LX_F_SETLEASE       1024
#define LX_F_GETLEASE       1025
#define LX_F_NOTIFY         1026
#define LX_F_SETPIPE_SZ     1031
#define LX_F_GETPIPE_SZ     1032
#define LX_F_ADD_SEALS      1033
#define LX_F_GET_SEALS      1034
#define LX_F_SEAL_SEAL      0x0001
#define LX_F_SEAL_SHRINK    0x0002
#define LX_F_SEAL_GROW      0x0004
#define LX_F_SEAL_WRITE     0x0008
#define LX_F_SEAL_FUTURE_WRITE 0x0010
#define LX_F_RDLCK          0
#define LX_F_WRLCK          1
#define LX_F_UNLCK          2
#define LX_LOCK_SH          1
#define LX_LOCK_EX          2
#define LX_LOCK_NB          4
#define LX_LOCK_UN          8

#define LX_AT_FDCWD             (-100)
#define LX_AT_SYMLINK_NOFOLLOW  0x100
#define LX_AT_REMOVEDIR         0x200
#define LX_AT_EMPTY_PATH        0x1000
#define LX_AT_EACCESS           0x200
#define LX_AT_NO_AUTOMOUNT      0x800
#define LX_AT_RECURSIVE         0x8000
#define LX_RENAME_NOREPLACE     1
#define LX_RENAME_EXCHANGE      2
#define LX_RENAME_WHITEOUT      4

#define LX_STATX_TYPE           0x0001
#define LX_STATX_MODE           0x0002
#define LX_STATX_NLINK          0x0004
#define LX_STATX_UID            0x0008
#define LX_STATX_GID            0x0010
#define LX_STATX_ATIME          0x0020
#define LX_STATX_MTIME          0x0040
#define LX_STATX_CTIME          0x0080
#define LX_STATX_INO            0x0100
#define LX_STATX_SIZE           0x0200
#define LX_STATX_BLOCKS         0x0400
#define LX_STATX_BASIC_STATS    0x07FF
#define LX_STATX_BTIME          0x0800
#define LX_STATX_MNT_ID         0x1000

#define LX_SEEK_SET         0
#define LX_SEEK_CUR         1
#define LX_SEEK_END         2

#define LX_S_IFMT           0170000
#define LX_S_ISUID          0004000
#define LX_S_ISGID          0002000
#define LX_S_ISVTX          0001000
#define LX_S_IXGRP          0000010
#define LX_S_IFDIR          0040000
#define LX_S_IFCHR          0020000
#define LX_S_IFREG          0100000
#define LX_S_IFIFO          0010000
#define LX_S_IFLNK          0120000
#define LX_S_IFBLK          0060000
#define LX_S_IFSOCK         0140000

#define LX_DT_UNKNOWN       0
#define LX_DT_FIFO          1
#define LX_DT_CHR           2
#define LX_DT_DIR           4
#define LX_DT_REG           8
#define LX_DT_BLK           6
#define LX_DT_LNK           10
#define LX_DT_SOCK          12

#define LX_PROT_NONE        0
#define LX_PROT_READ        1
#define LX_PROT_WRITE       2
#define LX_PROT_EXEC        4

#define LX_MAP_SHARED           0x01
#define LX_MAP_PRIVATE          0x02
#define LX_MAP_FIXED            0x10
#define LX_MAP_ANONYMOUS        0x20
#define LX_MAP_GROWSDOWN        0x0100
#define LX_MAP_LOCKED           0x2000
#define LX_MAP_POPULATE         0x8000
#define LX_MAP_STACK            0x20000
#define LX_MAP_FIXED_NOREPLACE  0x100000

#define LX_MREMAP_MAYMOVE   1
#define LX_MREMAP_FIXED     2
#define LX_MREMAP_DONTUNMAP 4

#define LX_RESOLVE_NO_XDEV          0x01
#define LX_RESOLVE_NO_MAGICLINKS    0x02
#define LX_RESOLVE_NO_SYMLINKS      0x04
#define LX_RESOLVE_BENEATH          0x08
#define LX_RESOLVE_IN_ROOT          0x10
#define LX_RESOLVE_CACHED           0x20

typedef struct _LX_OPEN_HOW
{
    ULONG64 Flags;
    ULONG64 Mode;
    ULONG64 Resolve;
} LX_OPEN_HOW, *PLX_OPEN_HOW;

#define LX_SCHED_NORMAL     0
#define LX_SCHED_FIFO       1
#define LX_SCHED_RR         2
#define LX_SCHED_BATCH      3
#define LX_SCHED_IDLE       5
#define LX_SCHED_DEADLINE   6
#define LX_SCHED_EXT        7
#define LX_SCHED_RESET_ON_FORK          0x40000000
#define LX_SCHED_FLAG_RESET_ON_FORK     0x01
#define LX_SCHED_FLAG_KEEP_POLICY       0x08
#define LX_SCHED_FLAG_KEEP_PARAMS       0x10
#define LX_SCHED_FLAG_UTIL_CLAMP_MIN    0x20
#define LX_SCHED_FLAG_UTIL_CLAMP_MAX    0x40
#define LX_SCHED_FLAG_ALL               0x7F
#define LX_SCHED_ATTR_SIZE_VER0         48
#define LX_PRIO_PROCESS     0
#define LX_PRIO_PGRP        1
#define LX_PRIO_USER        2

typedef struct _LX_SCHED_ATTR
{
    ULONG Size;
    ULONG Policy;
    ULONG64 Flags;
    LONG Nice;
    ULONG Priority;
    ULONG64 Runtime;
    ULONG64 Deadline;
    ULONG64 Period;
    ULONG UtilMin;
    ULONG UtilMax;
} LX_SCHED_ATTR, *PLX_SCHED_ATTR;

#define LX_MS_ASYNC         1
#define LX_MS_INVALIDATE    2
#define LX_MS_SYNC          4

#define LX_MADV_DONTNEED    4
#define LX_MADV_FREE        8

#define LX_AT_NULL          0
#define LX_AT_PHDR          3
#define LX_AT_PHENT         4
#define LX_AT_PHNUM         5
#define LX_AT_PAGESZ        6
#define LX_AT_BASE          7
#define LX_AT_FLAGS         8
#define LX_AT_ENTRY         9
#define LX_AT_UID           11
#define LX_AT_EUID          12
#define LX_AT_GID           13
#define LX_AT_EGID          14
#define LX_AT_PLATFORM      15
#define LX_AT_HWCAP         16
#define LX_AT_CLKTCK        17
#define LX_AT_SECURE        23
#define LX_AT_RANDOM        25
#define LX_AT_EXECFN        31

#define LX_CLOCK_REALTIME           0
#define LX_CLOCK_MONOTONIC          1
#define LX_CLOCK_PROCESS_CPUTIME_ID 2
#define LX_CLOCK_THREAD_CPUTIME_ID  3
#define LX_CLOCK_MONOTONIC_RAW      4
#define LX_CLOCK_REALTIME_COARSE    5
#define LX_CLOCK_MONOTONIC_COARSE   6
#define LX_CLOCK_BOOTTIME           7
#define LX_CLOCK_REALTIME_ALARM     8
#define LX_CLOCK_BOOTTIME_ALARM     9
#define LX_CLOCK_TAI                11

#define LX_TCGETS           0x5401
#define LX_TCSETS           0x5402
#define LX_TCSETSW          0x5403
#define LX_TCSETSF          0x5404
#define LX_TCSBRK           0x5409
#define LX_TCXONC           0x540A
#define LX_TCFLSH           0x540B
#define LX_TIOCSCTTY        0x540E
#define LX_TIOCGPGRP        0x540F
#define LX_TIOCSPGRP        0x5410
#define LX_TIOCOUTQ         0x5411
#define LX_TIOCGWINSZ       0x5413
#define LX_TIOCSWINSZ       0x5414
#define LX_FIONREAD         0x541B
#define LX_TIOCNOTTY        0x5422
#define LX_TIOCGSID         0x5429
#define LX_TIOCGPTN         0x80045430
#define LX_TIOCSPTLCK       0x40045431
#define LX_TIOCGPTPEER      0x5441
#define LX_TIOCVHANGUP      0x5437
#define LX_TIOCEXCL         0x540C
#define LX_TIOCNXCL         0x540D
#define LX_TIOCSTI          0x5412
#define LX_TIOCMGET         0x5415
#define LX_FIONBIO          0x5421
#define LX_FIOCLEX          0x5451
#define LX_FIONCLEX         0x5450
#define LX_FIOASYNC         0x5452
#define LX_TIOCGLCKTRMIOS   0x5456
#define LX_TCGETS2          0x802C542A
#define LX_TCSETS2          0x402C542B
#define LX_TCSETSW2         0x402C542C
#define LX_TCSETSF2         0x402C542D

#define LX_VINTR            0
#define LX_VQUIT            1
#define LX_VERASE           2
#define LX_VKILL            3
#define LX_VEOF             4
#define LX_VTIME            5
#define LX_VMIN             6
#define LX_VSTART           8
#define LX_VSTOP            9
#define LX_VSUSP            10
#define LX_VEOL             11
#define LX_VREPRINT         12
#define LX_VDISCARD         13
#define LX_VWERASE          14
#define LX_VLNEXT           15
#define LX_VEOL2            16
#define LX_KERNEL_NCCS      19

#define LX_INLCR            0000100
#define LX_IGNCR            0000200
#define LX_ICRNL            0000400
#define LX_IXON             0002000
#define LX_OPOST            0000001
#define LX_ONLCR            0000004
#define LX_B38400           0000017
#define LX_CS8              0000060
#define LX_CREAD            0000200
#define LX_HUPCL            0002000
#define LX_ISIG             0000001
#define LX_ICANON           0000002
#define LX_ECHO             0000010
#define LX_ECHOE            0000020
#define LX_ECHOK            0000040
#define LX_ECHONL           0000100
#define LX_NOFLSH           0000200
#define LX_IEXTEN           0100000
#define LX_ECHOCTL          0001000
#define LX_ECHOKE           0004000

#define LX_SIGHUP           1
#define LX_SIGINT           2
#define LX_SIGQUIT          3
#define LX_SIGILL           4
#define LX_SIGTRAP          5
#define LX_SIGABRT          6
#define LX_SIGBUS           7
#define LX_SIGFPE           8
#define LX_SIGKILL          9
#define LX_SIGUSR1          10
#define LX_SIGSEGV          11
#define LX_SIGUSR2          12
#define LX_SIGPIPE          13
#define LX_SIGALRM          14
#define LX_SIGTERM          15
#define LX_SIGCHLD          17
#define LX_SIGCONT          18
#define LX_SIGSTOP          19
#define LX_SIGTSTP          20
#define LX_SIGTTIN          21
#define LX_SIGTTOU          22
#define LX_SIGURG           23
#define LX_SIGWINCH         28
#define LX_SIGSTKFLT        16
#define LX_SIGXCPU          24
#define LX_SIGXFSZ          25
#define LX_SIGVTALRM        26
#define LX_SIGPROF          27
#define LX_SIGIO            29
#define LX_SIGPWR           30
#define LX_SIGSYS           31
#define LX_SIGRTMIN         32
#define LX_NSIG             64

#define LX_SIG_DFL          0
#define LX_SIG_IGN          1

#define LX_SA_SIGINFO       0x00000004
#define LX_SA_RESTORER      0x04000000
#define LX_SA_ONSTACK       0x08000000
#define LX_SA_RESTART       0x10000000
#define LX_SA_NODEFER       0x40000000
#define LX_SA_RESETHAND     0x80000000

#define LX_SS_ONSTACK       1
#define LX_SS_DISABLE       2

#define LX_SI_USER          0
#define LX_SI_TKILL         (-6)
#define LX_SI_KERNEL        0x80
#define LX_SI_QUEUE         (-1)
#define LX_SI_TIMER         (-2)
#define LX_CLD_DUMPED       3
#define LX_CLD_TRAPPED      4
#define LX_CLD_STOPPED      5
#define LX_CLD_CONTINUED    6
#define LX_SEGV_MAPERR      1
#define LX_SEGV_ACCERR      2
#define LX_ILL_ILLOPC       1
#define LX_BUS_ADRALN       1
#define LX_TRAP_BRKPT       1
#define LX_FPE_INTDIV       1
#define LX_CLD_EXITED       1
#define LX_CLD_KILLED       2

#define LX_SIG_BLOCK        0
#define LX_SIG_UNBLOCK      1
#define LX_SIG_SETMASK      2

#define LX_CLONE_VM             0x00000100
#define LX_CLONE_FS             0x00000200
#define LX_CLONE_FILES          0x00000400
#define LX_CLONE_SIGHAND        0x00000800
#define LX_CLONE_VFORK          0x00004000
#define LX_CLONE_THREAD         0x00010000
#define LX_CLONE_SETTLS         0x00080000
#define LX_CLONE_PARENT_SETTID  0x00100000
#define LX_CLONE_CHILD_CLEARTID 0x00200000
#define LX_CLONE_CHILD_SETTID   0x01000000
#define LX_CLONE_NEWTIME        0x00000080
#define LX_CLONE_PIDFD          0x00001000
#define LX_CLONE_PTRACE         0x00002000
#define LX_CLONE_PARENT         0x00008000
#define LX_CLONE_NEWNS          0x00020000
#define LX_CLONE_SYSVSEM        0x00040000
#define LX_CLONE_DETACHED       0x00400000
#define LX_CLONE_UNTRACED       0x00800000
#define LX_CLONE_NEWCGROUP      0x02000000
#define LX_CLONE_NEWUTS         0x04000000
#define LX_CLONE_NEWIPC         0x08000000
#define LX_CLONE_NEWUSER        0x10000000
#define LX_CLONE_NEWPID         0x20000000
#define LX_CLONE_NEWNET         0x40000000
#define LX_CLONE_IO             0x80000000ULL
#define LX_CLONE_CLEAR_SIGHAND  0x100000000ULL
#define LX_CLONE_INTO_CGROUP    0x200000000ULL

#define LX_WNOHANG          1
#define LX_WUNTRACED        2
#define LX_WSTOPPED         2
#define LX_WEXITED          4
#define LX_WCONTINUED       8
#define LX_WNOWAIT          0x01000000
#define LX_WNOTHREAD        0x20000000
#define LX_WALL             0x40000000
#define LX_WCLONE           0x80000000
#define LX_P_ALL            0
#define LX_P_PID            1
#define LX_P_PGID           2
#define LX_P_PIDFD          3

#define LX_FUTEX_WAIT           0
#define LX_FUTEX_WAKE           1
#define LX_FUTEX_REQUEUE        3
#define LX_FUTEX_CMP_REQUEUE    4
#define LX_FUTEX_WAKE_OP        5
#define LX_FUTEX_LOCK_PI        6
#define LX_FUTEX_UNLOCK_PI      7
#define LX_FUTEX_TRYLOCK_PI     8
#define LX_FUTEX_WAIT_BITSET    9
#define LX_FUTEX_WAKE_BITSET    10
#define LX_FUTEX_WAIT_REQUEUE_PI 11
#define LX_FUTEX_LOCK_PI2       13
#define LX_FUTEX_PRIVATE        128
#define LX_FUTEX_CLOCK_REALTIME 256
#define LX_FUTEX_OP_OPARG_SHIFT 8
#define LX_FUTEX_BITSET_MATCH_ANY 0xFFFFFFFF
#define LX_FUTEX_WAITERS        0x80000000
#define LX_FUTEX_OWNER_DIED     0x40000000
#define LX_FUTEX_TID_MASK       0x3FFFFFFF
#define LX_ROBUST_LIST_LIMIT    2048
#define LX_FUTEX2_SIZE_U32      0x02
#define LX_FUTEX2_SIZE_MASK     0x03
#define LX_FUTEX2_NUMA          0x04
#define LX_FUTEX2_MPOL          0x08
#define LX_FUTEX2_PRIVATE       LX_FUTEX_PRIVATE
#define LX_FUTEX2_VALID_MASK    (LX_FUTEX2_SIZE_MASK | LX_FUTEX2_NUMA | LX_FUTEX2_MPOL | LX_FUTEX2_PRIVATE)
#define LX_FUTEX_NO_NODE        0xFFFFFFFF
#define LX_FUTEX_WAITV_MAX      128

#define LX_IPC_PRIVATE          0
#define LX_IPC_CREAT            01000
#define LX_IPC_EXCL             02000
#define LX_IPC_NOWAIT           04000
#define LX_IPC_RMID             0
#define LX_IPC_SET              1
#define LX_IPC_STAT             2
#define LX_IPC_INFO             3
#define LX_SHM_HUGETLB          04000
#define LX_SHM_RDONLY           010000
#define LX_SHM_RND              020000
#define LX_SHM_REMAP            040000
#define LX_SHM_EXEC             0100000
#define LX_SHM_LOCK             11
#define LX_SHM_UNLOCK           12
#define LX_SHM_STAT             13
#define LX_SHM_INFO             14
#define LX_SHM_STAT_ANY         15
#define LX_SHM_DEST             01000
#define LX_SHM_LOCKED           02000
#define LX_SHMMNI               4096
#define LX_SHMMAX               (~0ULL - (1ULL << 24))
#define LX_SEM_UNDO             0x1000
#define LX_GETPID               11
#define LX_GETVAL               12
#define LX_GETALL               13
#define LX_GETNCNT              14
#define LX_GETZCNT              15
#define LX_SETVAL               16
#define LX_SETALL               17
#define LX_SEM_STAT             18
#define LX_SEM_INFO             19
#define LX_SEM_STAT_ANY         20
#define LX_SEMMNI               32000
#define LX_SEMMSL               32000
#define LX_SEMMNS               (LX_SEMMNI * LX_SEMMSL)
#define LX_SEMOPM               500
#define LX_SEMVMX               32767
#define LX_SEMAEM               LX_SEMVMX
#define LX_SEMUSZ               20
#define LX_MSG_STAT             11
#define LX_MSG_INFO             12
#define LX_MSG_STAT_ANY         13
#define LX_MSG_NOERROR          010000
#define LX_MSG_EXCEPT           020000
#define LX_MSG_COPY             040000
#define LX_MSGMNI               32000
#define LX_MSGMAX               8192
#define LX_MSGMNB               16384
#define LX_MSGPOOL              (LX_MSGMNI * LX_MSGMNB / 1024)
#define LX_MSGSSZ               16
#define LX_MSGSEG               0xFFFF

typedef struct _LX_IPC64_PERM
{
    LONG Key;
    ULONG Uid;
    ULONG Gid;
    ULONG Cuid;
    ULONG Cgid;
    ULONG Mode;
    USHORT Seq;
    USHORT Pad2;
    ULONG Pad3;
    ULONG64 Unused1;
    ULONG64 Unused2;
} LX_IPC64_PERM, *PLX_IPC64_PERM;

C_ASSERT(sizeof(LX_IPC64_PERM) == 48);

typedef struct _LX_SHMID64_DS
{
    LX_IPC64_PERM Perm;
    ULONG64 Segsz;
    LONG64 Atime;
    LONG64 Dtime;
    LONG64 Ctime;
    ULONG Cpid;
    ULONG Lpid;
    ULONG64 Nattch;
    ULONG64 Unused4;
    ULONG64 Unused5;
} LX_SHMID64_DS, *PLX_SHMID64_DS;

C_ASSERT(sizeof(LX_SHMID64_DS) == 112);

typedef struct _LX_SHMINFO64
{
    ULONG64 Shmmax;
    ULONG64 Shmmin;
    ULONG64 Shmmni;
    ULONG64 Shmseg;
    ULONG64 Shmall;
    ULONG64 Unused[4];
} LX_SHMINFO64, *PLX_SHMINFO64;

typedef struct _LX_SHM_USAGE
{
    LONG UsedIds;
    ULONG64 ShmTot;
    ULONG64 ShmRss;
    ULONG64 ShmSwp;
    ULONG64 SwapAttempts;
    ULONG64 SwapSuccesses;
} LX_SHM_USAGE, *PLX_SHM_USAGE;

C_ASSERT(sizeof(LX_SHM_USAGE) == 48);

typedef struct _LX_SEMID64_DS
{
    LX_IPC64_PERM Perm;
    LONG64 Otime;
    LONG64 Ctime;
    ULONG64 Nsems;
    ULONG64 Unused3;
    ULONG64 Unused4;
} LX_SEMID64_DS, *PLX_SEMID64_DS;

C_ASSERT(sizeof(LX_SEMID64_DS) == 88);

typedef struct _LX_SEMINFO
{
    LONG Semmap;
    LONG Semmni;
    LONG Semmns;
    LONG Semmnu;
    LONG Semmsl;
    LONG Semopm;
    LONG Semume;
    LONG Semusz;
    LONG Semvmx;
    LONG Semaem;
} LX_SEMINFO, *PLX_SEMINFO;

typedef struct _LX_SEMBUF
{
    USHORT Num;
    SHORT Op;
    SHORT Flags;
} LX_SEMBUF, *PLX_SEMBUF;

C_ASSERT(sizeof(LX_SEMBUF) == 6);

typedef struct _LX_MSQID64_DS
{
    LX_IPC64_PERM Perm;
    LONG64 Stime;
    LONG64 Rtime;
    LONG64 Ctime;
    ULONG64 Cbytes;
    ULONG64 Qnum;
    ULONG64 Qbytes;
    ULONG Lspid;
    ULONG Lrpid;
    ULONG64 Unused4;
    ULONG64 Unused5;
} LX_MSQID64_DS, *PLX_MSQID64_DS;

C_ASSERT(sizeof(LX_MSQID64_DS) == 120);

typedef struct _LX_MSGINFO
{
    LONG Msgpool;
    LONG Msgmap;
    LONG Msgmax;
    LONG Msgmnb;
    LONG Msgmni;
    LONG Msgssz;
    LONG Msgtql;
    USHORT Msgseg;
} LX_MSGINFO, *PLX_MSGINFO;

#define LX_MEMBARRIER_CMD_QUERY                     0
#define LX_MEMBARRIER_CMD_GLOBAL                    0x001
#define LX_MEMBARRIER_CMD_GLOBAL_EXPEDITED          0x002
#define LX_MEMBARRIER_CMD_REGISTER_GLOBAL_EXPEDITED 0x004
#define LX_MEMBARRIER_CMD_PRIVATE_EXPEDITED         0x008
#define LX_MEMBARRIER_CMD_REGISTER_PRIVATE_EXPEDITED 0x010
#define LX_MEMBARRIER_CMD_PRIVATE_EXPEDITED_SYNC_CORE 0x020
#define LX_MEMBARRIER_CMD_REGISTER_PRIVATE_EXPEDITED_SYNC_CORE 0x040
#define LX_MEMBARRIER_CMD_PRIVATE_EXPEDITED_RSEQ    0x080
#define LX_MEMBARRIER_CMD_GET_REGISTRATIONS         0x200
#define LX_MEMBARRIER_CMD_FLAG_CPU                  0x001

#define LX_PKEY_ACCESS_MASK     0x0F
#define LX_UIO_MAXIOV           1024

#define LX_CAP_CHOWN            0
#define LX_CAP_DAC_OVERRIDE     1
#define LX_CAP_DAC_READ_SEARCH  2
#define LX_CAP_FOWNER           3
#define LX_CAP_FSETID           4
#define LX_CAP_KILL             5
#define LX_CAP_SETGID           6
#define LX_CAP_SETUID           7
#define LX_CAP_SETPCAP          8
#define LX_CAP_LINUX_IMMUTABLE  9
#define LX_CAP_NET_ADMIN        12
#define LX_CAP_IPC_LOCK         14
#define LX_CAP_IPC_OWNER        15
#define LX_CAP_SYS_CHROOT       18
#define LX_CAP_SYS_PTRACE       19
#define LX_CAP_SYS_ADMIN        21
#define LX_CAP_SYS_BOOT         22
#define LX_CAP_SYS_RESOURCE     24
#define LX_CAP_SYS_TIME         25
#define LX_CAP_MKNOD            27
#define LX_CAP_MAC_OVERRIDE     32
#define LX_CAP_BPF              39
#define LX_CAP_CHECKPOINT_RESTORE 40
#define LX_CAP_LAST_CAP         40
#define LX_CAP_FULL_SET         ((1ULL << (LX_CAP_LAST_CAP + 1)) - 1)
#define LX_CAP_FS_SET           ((1ULL << LX_CAP_CHOWN) | (1ULL << LX_CAP_MKNOD) | (1ULL << LX_CAP_DAC_OVERRIDE) | \
                                 (1ULL << LX_CAP_DAC_READ_SEARCH) | (1ULL << LX_CAP_FOWNER) | (1ULL << LX_CAP_FSETID) | \
                                 (1ULL << LX_CAP_MAC_OVERRIDE) | (1ULL << LX_CAP_LINUX_IMMUTABLE))

#define LX_SECBIT_NOROOT                0x001
#define LX_SECBIT_NO_SETUID_FIXUP       0x004
#define LX_SECBIT_KEEP_CAPS             0x010
#define LX_SECBIT_KEEP_CAPS_LOCKED      0x020
#define LX_SECBIT_NO_CAP_AMBIENT_RAISE  0x040
#define LX_SECURE_ALL_BITS              0x555
#define LX_SECURE_ALL_LOCKS             0xAAA
#define LX_SECURE_ALL_UNPRIVILEGED      0x500

#define LX_USB_SPEED_LOW            1
#define LX_USB_SPEED_FULL           2
#define LX_USB_SPEED_HIGH           3
#define LX_USB_SPEED_SUPER          5
#define LX_USB_SPEED_SUPER_PLUS     6

#define LX_USBDEVFS_CONTROL         0xC0185500
#define LX_USBDEVFS_GETDRIVER       0x41045508
#define LX_USBDEVFS_SUBMITURB       0x8038550A
#define LX_USBDEVFS_DISCARDURB      0x0000550B
#define LX_USBDEVFS_REAPURB         0x4008550C
#define LX_USBDEVFS_REAPURBNDELAY   0x4008550D
#define LX_USBDEVFS_CLAIMINTERFACE  0x8004550F
#define LX_USBDEVFS_RELEASEINTERFACE 0x80045510
#define LX_USBDEVFS_CONNECTINFO     0x40085511
#define LX_USBDEVFS_GET_CAPABILITIES 0x8004551A
#define LX_USBDEVFS_GET_SPEED       0x0000551F
#define LX_USBDEVFS_URB_TYPE_CONTROL 2

typedef struct _LX_USBDEVFS_CTRLTRANSFER
{
    UCHAR RequestType;
    UCHAR Request;
    USHORT Value;
    USHORT Index;
    USHORT Length;
    ULONG Timeout;
    ULONG64 Data;
} LX_USBDEVFS_CTRLTRANSFER, *PLX_USBDEVFS_CTRLTRANSFER;

typedef struct _LX_USBDEVFS_URB
{
    UCHAR Type;
    UCHAR Endpoint;
    INT Status;
    ULONG Flags;
    ULONG64 Buffer;
    INT BufferLength;
    INT ActualLength;
    INT StartFrame;
    INT NumberOfPackets;
    INT ErrorCount;
    ULONG Signal;
    ULONG64 UserContext;
} LX_USBDEVFS_URB, *PLX_USBDEVFS_URB;

C_ASSERT(sizeof(LX_USBDEVFS_CTRLTRANSFER) == 24);
C_ASSERT(FIELD_OFFSET(LX_USBDEVFS_URB, Buffer) == 16);
C_ASSERT(FIELD_OFFSET(LX_USBDEVFS_URB, ActualLength) == 28);
C_ASSERT(sizeof(LX_USBDEVFS_URB) == 56);

#define LX_SPLICE_F_NONBLOCK    0x02
#define LX_SPLICE_F_ALL         0x0F

#define LX_KCMP_FILE            0
#define LX_KCMP_VM              1
#define LX_KCMP_FILES           2
#define LX_KCMP_FS              3
#define LX_KCMP_SIGHAND         4
#define LX_KCMP_IO              5
#define LX_KCMP_SYSVSEM         6
#define LX_KCMP_EPOLL_TFD       7

typedef struct _LX_KCMP_EPOLL_SLOT
{
    ULONG Efd;
    ULONG Tfd;
    ULONG Toff;
} LX_KCMP_EPOLL_SLOT, *PLX_KCMP_EPOLL_SLOT;

#define LX_ADJ_OFFSET_SS_READ   0xA001
#define LX_STA_UNSYNC           0x0040
#define LX_TIME_ERROR           5
#define LX_NTP_PHASE_LIMIT      16000000
#define LX_NTP_TOLERANCE        32768000
#define LX_USER_TICK_USEC       10000

typedef struct _LX_TIMEX
{
    ULONG Modes;
    LONG Pad0;
    LONG64 Offset;
    LONG64 Freq;
    LONG64 MaxError;
    LONG64 EstError;
    LONG Status;
    LONG Pad1;
    LONG64 Constant;
    LONG64 Precision;
    LONG64 Tolerance;
    LONG64 TimeSeconds;
    LONG64 TimeMicroseconds;
    LONG64 Tick;
    LONG64 PpsFreq;
    LONG64 Jitter;
    LONG Shift;
    LONG Pad2;
    LONG64 Stabil;
    LONG64 JitCnt;
    LONG64 CalCnt;
    LONG64 ErrCnt;
    LONG64 StbCnt;
    LONG Tai;
    LONG Reserved[11];
} LX_TIMEX, *PLX_TIMEX;

C_ASSERT(sizeof(LX_TIMEX) == 208);

typedef struct _LX_FUTEX_WAITV
{
    ULONG64 Value;
    ULONG64 Address;
    ULONG Flags;
    ULONG Reserved;
} LX_FUTEX_WAITV, *PLX_FUTEX_WAITV;

typedef struct _LX_ROBUST_LIST_HEAD
{
    ULONG64 List;
    LONG64 FutexOffset;
    ULONG64 ListOpPending;
} LX_ROBUST_LIST_HEAD, *PLX_ROBUST_LIST_HEAD;

#define LX_RLIMIT_STACK     3
#define LX_RLIMIT_NOFILE    7
#define LX_RLIMIT_MEMLOCK   8
#define LX_RLIM_INFINITY    (~0ULL)

typedef struct _LX_TIMESPEC
{
    LONG64 Seconds;
    LONG64 Nanoseconds;
} LX_TIMESPEC, *PLX_TIMESPEC;

typedef struct _LX_TIMEVAL
{
    LONG64 Seconds;
    LONG64 Microseconds;
} LX_TIMEVAL, *PLX_TIMEVAL;

typedef struct _LX_IOVEC
{
    ULONG64 Base;
    ULONG64 Length;
} LX_IOVEC, *PLX_IOVEC;

typedef struct _LX_STAT
{
    ULONG64 Dev;
    ULONG64 Ino;
    ULONG Mode;
    ULONG Nlink;
    ULONG Uid;
    ULONG Gid;
    ULONG64 Rdev;
    ULONG64 Pad;
    LONG64 Size;
    LONG BlkSize;
    LONG Pad2;
    LONG64 Blocks;
    LX_TIMESPEC Atime;
    LX_TIMESPEC Mtime;
    LX_TIMESPEC Ctime;
    ULONG Unused[2];
} LX_STAT, *PLX_STAT;

C_ASSERT(sizeof(LX_STAT) == 128);

typedef struct _LX_UTSNAME
{
    CHAR SysName[65];
    CHAR NodeName[65];
    CHAR Release[65];
    CHAR Version[65];
    CHAR Machine[65];
    CHAR DomainName[65];
} LX_UTSNAME, *PLX_UTSNAME;

typedef struct _LX_DIRENT64
{
    ULONG64 Ino;
    LONG64 Off;
    USHORT RecLen;
    UCHAR Type;
    CHAR Name[1];
} LX_DIRENT64, *PLX_DIRENT64;

typedef struct _LX_SIGACTION
{
    ULONG64 Handler;
    ULONG64 Flags;
    ULONG64 Restorer;
    ULONG64 Mask;
} LX_SIGACTION, *PLX_SIGACTION;

#define LX_POLLIN           0x0001
#define LX_POLLPRI          0x0002
#define LX_POLLOUT          0x0004
#define LX_POLLERR          0x0008
#define LX_POLLHUP          0x0010
#define LX_POLLNVAL         0x0020
#define LX_POLLRDNORM       0x0040
#define LX_POLLRDBAND       0x0080
#define LX_POLLWRNORM       0x0100
#define LX_POLLWRBAND       0x0200
#define LX_POLLRDHUP        0x2000

#define LX_EPOLL_CTL_ADD    1
#define LX_EPOLL_CTL_DEL    2
#define LX_EPOLL_CTL_MOD    3
#define LX_EPOLLEXCLUSIVE   (1U << 28)
#define LX_EPOLLWAKEUP      (1U << 29)
#define LX_EPOLLONESHOT     (1U << 30)
#define LX_EPOLLET          (1U << 31)

typedef struct _LX_EPOLL_EVENT
{
    ULONG Events;
    ULONG Pad;
    ULONG64 Data;
} LX_EPOLL_EVENT, *PLX_EPOLL_EVENT;

C_ASSERT(sizeof(LX_EPOLL_EVENT) == 16);

typedef struct _LX_SIGINFO
{
    LONG Signo;
    LONG Errno;
    LONG Code;
    LONG Pad;
    union
    {
        struct
        {
            LONG Pid;
            ULONG Uid;
            LONG Status;
        } Child;
        ULONG64 Address;
        UCHAR Raw[112];
    };
} LX_SIGINFO, *PLX_SIGINFO;

C_ASSERT(sizeof(LX_SIGINFO) == 128);

typedef struct _LX_STACK
{
    ULONG64 Sp;
    LONG Flags;
    LONG Pad;
    ULONG64 Size;
} LX_STACK, *PLX_STACK;

typedef struct _LX_RLIMIT
{
    ULONG64 Cur;
    ULONG64 Max;
} LX_RLIMIT, *PLX_RLIMIT;

typedef struct _LX_TERMIOS
{
    ULONG Iflag;
    ULONG Oflag;
    ULONG Cflag;
    ULONG Lflag;
    UCHAR Line;
    UCHAR Cc[LX_KERNEL_NCCS];
} LX_TERMIOS, *PLX_TERMIOS;

C_ASSERT(sizeof(LX_TERMIOS) == 36);

typedef struct _LX_WINSIZE
{
    USHORT Rows;
    USHORT Cols;
    USHORT XPixel;
    USHORT YPixel;
} LX_WINSIZE, *PLX_WINSIZE;

#define LX_AF_UNSPEC        0
#define LX_AF_UNIX          1
#define LX_AF_INET          2
#define LX_AF_INET6         10
#define LX_AF_NETLINK       16
#define LX_AF_PACKET        17
#define LX_SOCK_STREAM      1
#define LX_SOCK_DGRAM       2
#define LX_SOCK_RAW         3
#define LX_SOCK_SEQPACKET   5
#define LX_SOCK_TYPE_MASK   0xF
#define LX_SOCK_NONBLOCK    04000
#define LX_SOCK_CLOEXEC     02000000
#define LX_SOL_SOCKET       1
#define LX_IPPROTO_IP       0
#define LX_IPPROTO_ICMP     1
#define LX_IPPROTO_TCP      6
#define LX_IPPROTO_UDP      17
#define LX_IPPROTO_IPV6     41
#define LX_IPPROTO_ICMPV6   58
#define LX_IP_TOS           1
#define LX_IP_TTL           2
#define LX_IP_PKTINFO       8
#define LX_IP_MTU_DISCOVER  10
#define LX_IP_RECVERR       11
#define LX_IP_RECVTTL       12
#define LX_IP_RECVTOS       13
#define LX_IP_FREEBIND      15
#define LX_IP_TRANSPARENT   19
#define LX_IP_RECVFRAGSIZE  25
#define LX_IP_MULTICAST_ALL 49
#define LX_IP_UNICAST_IF    50
#define LX_IP_BIND_ADDRESS_NO_PORT 24
#define LX_IP_MULTICAST_IF  32
#define LX_IP_MULTICAST_TTL 33
#define LX_IP_MULTICAST_LOOP 34
#define LX_IP_ADD_MEMBERSHIP 35
#define LX_IP_DROP_MEMBERSHIP 36
#define LX_IPV6_UNICAST_HOPS 16
#define LX_IPV6_MULTICAST_HOPS 18
#define LX_IPV6_MULTICAST_LOOP 19
#define LX_IPV6_JOIN_GROUP  20
#define LX_IPV6_LEAVE_GROUP 21
#define LX_IPV6_MTU_DISCOVER 23
#define LX_IPV6_RECVERR     25
#define LX_IPV6_V6ONLY      26
#define LX_IPV6_RECVPKTINFO 49
#define LX_IPV6_PKTINFO     50
#define LX_IPV6_RECVHOPLIMIT 51
#define LX_IPV6_RECVTCLASS  66
#define LX_IPV6_UNICAST_IF  76
#define LX_IPV6_RECVFRAGSIZE 77
#define LX_IPV6_FREEBIND    78
#define LX_IPV6_TCLASS      67
#define LX_TCP_NODELAY      1
#define LX_TCP_MAXSEG       2
#define LX_TCP_CORK         3
#define LX_TCP_KEEPIDLE     4
#define LX_TCP_KEEPINTVL    5
#define LX_TCP_KEEPCNT      6
#define LX_TCP_SYNCNT       7
#define LX_TCP_LINGER2      8
#define LX_TCP_DEFER_ACCEPT 9
#define LX_TCP_WINDOW_CLAMP 10
#define LX_TCP_INFO         11
#define LX_TCP_QUICKACK     12
#define LX_TCP_CONGESTION   13
#define LX_TCP_USER_TIMEOUT 18
#define LX_TCP_FASTOPEN     23
#define LX_TCP_NOTSENT_LOWAT 25
#define LX_TCP_FASTOPEN_CONNECT 30

typedef struct _LX_SOCKADDR_IN
{
    USHORT Family;
    USHORT Port;
    ULONG Address;
    UCHAR Zero[8];
} LX_SOCKADDR_IN, *PLX_SOCKADDR_IN;

typedef struct _LX_SOCKADDR_IN6
{
    USHORT Family;
    USHORT Port;
    ULONG FlowInfo;
    UCHAR Address[16];
    ULONG ScopeId;
} LX_SOCKADDR_IN6, *PLX_SOCKADDR_IN6;

#define LX_SO_DEBUG         1
#define LX_SO_REUSEADDR     2
#define LX_SO_TYPE          3
#define LX_SO_ERROR         4
#define LX_SO_BROADCAST     6
#define LX_SO_SNDBUF        7
#define LX_SO_RCVBUF        8
#define LX_SO_KEEPALIVE     9
#define LX_SO_PRIORITY      12
#define LX_SO_LINGER        13
#define LX_SO_REUSEPORT     15
#define LX_SO_PASSCRED      16
#define LX_SO_PEERCRED      17
#define LX_SO_RCVLOWAT      18
#define LX_SO_SNDLOWAT      19
#define LX_SO_RCVTIMEO_OLD  20
#define LX_SO_SNDTIMEO_OLD  21
#define LX_SO_TIMESTAMP     29
#define LX_SO_TIMESTAMPNS   35
#define LX_SO_MARK          36
#define LX_SO_BINDTOIFINDEX 62
#define LX_SO_ACCEPTCONN    30
#define LX_SO_PEERSEC       31
#define LX_SO_SNDBUFFORCE   32
#define LX_SO_RCVBUFFORCE   33
#define LX_SO_PASSSEC       34
#define LX_SO_PROTOCOL      38
#define LX_SO_DOMAIN        39
#define LX_SO_PEERGROUPS    59
#define LX_SO_RCVTIMEO_NEW  66
#define LX_SO_SNDTIMEO_NEW  67
#define LX_SO_PEERPIDFD     77
#define LX_SCM_RIGHTS       1
#define LX_SCM_CREDENTIALS  2
#define LX_MSG_OOB          0x0001
#define LX_MSG_PEEK         0x0002
#define LX_MSG_CTRUNC       0x0008
#define LX_MSG_TRUNC        0x0020
#define LX_MSG_DONTWAIT     0x0040
#define LX_MSG_EOR          0x0080
#define LX_MSG_WAITALL      0x0100
#define LX_MSG_NOSIGNAL     0x4000
#define LX_MSG_CMSG_CLOEXEC 0x40000000
#define LX_SHUT_RD          0
#define LX_SHUT_WR          1
#define LX_SHUT_RDWR        2
#define LX_UNIX_PATH_MAX    108

typedef struct _LX_MSGHDR
{
    ULONG64 Name;
    LONG NameLength;
    LONG Pad;
    ULONG64 Iov;
    ULONG64 IovLength;
    ULONG64 Control;
    ULONG64 ControlLength;
    ULONG Flags;
    ULONG Pad2;
} LX_MSGHDR, *PLX_MSGHDR;

C_ASSERT(sizeof(LX_MSGHDR) == 56);

typedef struct _LX_MMSGHDR
{
    LX_MSGHDR Header;
    ULONG Length;
    ULONG Pad;
} LX_MMSGHDR, *PLX_MMSGHDR;

C_ASSERT(sizeof(LX_MMSGHDR) == 64);

typedef struct _LX_CMSGHDR
{
    ULONG64 Length;
    LONG Level;
    LONG Type;
} LX_CMSGHDR, *PLX_CMSGHDR;

C_ASSERT(sizeof(LX_CMSGHDR) == 16);

typedef struct _LX_UCRED
{
    ULONG Pid;
    ULONG Uid;
    ULONG Gid;
} LX_UCRED, *PLX_UCRED;

#define LX_EFD_SEMAPHORE    1
#define LX_TFD_TIMER_ABSTIME        1
#define LX_TFD_TIMER_CANCEL_ON_SET  2

typedef struct _LX_ITIMERSPEC
{
    LX_TIMESPEC Interval;
    LX_TIMESPEC Value;
} LX_ITIMERSPEC, *PLX_ITIMERSPEC;

typedef struct _LX_SIGNALFD_SIGINFO
{
    ULONG Signo;
    LONG Errno;
    LONG Code;
    ULONG Pid;
    ULONG Uid;
    LONG Fd;
    ULONG Tid;
    ULONG Band;
    ULONG Overrun;
    ULONG Trapno;
    LONG Status;
    LONG Int;
    ULONG64 Ptr;
    ULONG64 Utime;
    ULONG64 Stime;
    ULONG64 Addr;
    USHORT AddrLsb;
    USHORT Pad2;
    LONG Syscall;
    ULONG64 CallAddr;
    ULONG Arch;
    UCHAR Pad[28];
} LX_SIGNALFD_SIGINFO, *PLX_SIGNALFD_SIGINFO;

C_ASSERT(sizeof(LX_SIGNALFD_SIGINFO) == 128);

#define LX_IN_ACCESS        0x00000001
#define LX_IN_MODIFY        0x00000002
#define LX_IN_ATTRIB        0x00000004
#define LX_IN_CLOSE_WRITE   0x00000008
#define LX_IN_CLOSE_NOWRITE 0x00000010
#define LX_IN_OPEN          0x00000020
#define LX_IN_MOVED_FROM    0x00000040
#define LX_IN_MOVED_TO      0x00000080
#define LX_IN_CREATE        0x00000100
#define LX_IN_DELETE        0x00000200
#define LX_IN_DELETE_SELF   0x00000400
#define LX_IN_MOVE_SELF     0x00000800
#define LX_IN_ALL_EVENTS    0x00000FFF
#define LX_IN_UNMOUNT       0x00002000
#define LX_IN_Q_OVERFLOW    0x00004000
#define LX_IN_IGNORED       0x00008000
#define LX_IN_ONLYDIR       0x01000000
#define LX_IN_DONT_FOLLOW   0x02000000
#define LX_IN_EXCL_UNLINK   0x04000000
#define LX_IN_MASK_CREATE   0x10000000
#define LX_IN_MASK_ADD      0x20000000
#define LX_IN_ISDIR         0x40000000
#define LX_IN_ONESHOT       0x80000000

typedef struct _LX_INOTIFY_EVENT
{
    LONG Wd;
    ULONG Mask;
    ULONG Cookie;
    ULONG Length;
} LX_INOTIFY_EVENT, *PLX_INOTIFY_EVENT;

typedef struct _LX_STATX_TIMESTAMP
{
    LONG64 Seconds;
    ULONG Nanoseconds;
    LONG Pad;
} LX_STATX_TIMESTAMP;

typedef struct _LX_STATX
{
    ULONG Mask;
    ULONG BlkSize;
    ULONG64 Attributes;
    ULONG Nlink;
    ULONG Uid;
    ULONG Gid;
    USHORT Mode;
    USHORT Pad0;
    ULONG64 Ino;
    ULONG64 Size;
    ULONG64 Blocks;
    ULONG64 AttributesMask;
    LX_STATX_TIMESTAMP Atime;
    LX_STATX_TIMESTAMP Btime;
    LX_STATX_TIMESTAMP Ctime;
    LX_STATX_TIMESTAMP Mtime;
    ULONG RdevMajor;
    ULONG RdevMinor;
    ULONG DevMajor;
    ULONG DevMinor;
    ULONG64 MntId;
    ULONG64 Pad1[13];
} LX_STATX, *PLX_STATX;

C_ASSERT(sizeof(LX_STATX) == 256);

typedef struct _LX_CLONE_ARGS
{
    ULONG64 Flags;
    ULONG64 Pidfd;
    ULONG64 ChildTid;
    ULONG64 ParentTid;
    ULONG64 ExitSignal;
    ULONG64 Stack;
    ULONG64 StackSize;
    ULONG64 Tls;
    ULONG64 SetTid;
    ULONG64 SetTidSize;
    ULONG64 Cgroup;
} LX_CLONE_ARGS, *PLX_CLONE_ARGS;

C_ASSERT(sizeof(LX_CLONE_ARGS) == 88);

typedef struct _LX_FLOCK
{
    SHORT Type;
    SHORT Whence;
    LONG Pad;
    LONG64 Start;
    LONG64 Length;
    LONG Pid;
    LONG Pad2;
} LX_FLOCK, *PLX_FLOCK;

C_ASSERT(sizeof(LX_FLOCK) == 32);

#define LX_PR_SET_PDEATHSIG         1
#define LX_PR_GET_PDEATHSIG         2
#define LX_PR_GET_DUMPABLE          3
#define LX_PR_SET_DUMPABLE          4
#define LX_PR_GET_KEEPCAPS          7
#define LX_PR_SET_KEEPCAPS          8
#define LX_PR_SET_NAME              15
#define LX_PR_GET_NAME              16
#define LX_PR_GET_SECCOMP           21
#define LX_PR_SET_SECCOMP           22
#define LX_PR_CAPBSET_READ          23
#define LX_PR_CAPBSET_DROP          24
#define LX_PR_GET_SECUREBITS        27
#define LX_PR_SET_SECUREBITS        28
#define LX_PR_SET_TIMERSLACK        29
#define LX_PR_GET_TIMERSLACK        30
#define LX_PR_SET_MM                35
#define LX_PR_SET_CHILD_SUBREAPER   36
#define LX_PR_GET_CHILD_SUBREAPER   37
#define LX_PR_SET_NO_NEW_PRIVS      38
#define LX_PR_GET_NO_NEW_PRIVS      39
#define LX_PR_GET_TID_ADDRESS       40
#define LX_PR_SET_THP_DISABLE       41
#define LX_PR_GET_THP_DISABLE       42
#define LX_PR_CAP_AMBIENT           47

#define LX_REBOOT_MAGIC1            0xFEE1DEAD
#define LX_REBOOT_MAGIC2            672274793
#define LX_REBOOT_CMD_RESTART       0x01234567
#define LX_REBOOT_CMD_HALT          0xCDEF0123
#define LX_REBOOT_CMD_CAD_ON        0x89ABCDEF
#define LX_REBOOT_CMD_CAD_OFF       0
#define LX_REBOOT_CMD_POWER_OFF     0x4321FEDC
#define LX_REBOOT_CMD_KEXEC         0x45584543
