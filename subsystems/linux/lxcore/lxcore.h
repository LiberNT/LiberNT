/*
 * PROJECT:     LiberNT Linux Subsystem
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Linux subsystem driver internal definitions
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

#include <ntifs.h>
#include <stdlib.h>
#include <ndk/ketypes.h>
#include <ndk/exfuncs.h>
#include <ndk/mmfuncs.h>
#include <ndk/psfuncs.h>
#include <ndk/iofuncs.h>
#include <reactos/pico.h>
#include <pseh/pseh2.h>
#include <bcrypt.h>
#include <ntstrsafe.h>

#include "lxabi.h"
#include "../include/lxioctl.h"
#include "../include/lxwin.h"

#define LX_TAG              'xcxL'
#define LX_PAGE_SIZE        0x1000ULL
#define LX_ALLOC_GRANULARITY LX_PAGE_SIZE
#define LX_FAULT_CLUSTER    0x10000ULL
#define LX_PATH_MAX         4096
#define LX_XATTR_NAME_MAX   255
#define LX_XATTR_BLOB_MAX   65535
#define LX_MAX_FDS          1024
#define LX_MAX_ARG_STRLEN   (128 * 1024)
#define LX_MAX_ARG_TOTAL    (2 * 1024 * 1024)
#define LX_IO_CHUNK         (64 * 1024)
#define LX_STACK_SIZE       (8 * 1024 * 1024)
#define LX_BRK_RESERVE      (1024ULL * 1024 * 1024)
#define LX_AUXV_WORDS       40
#define LX_MMAP_MIN_ADDR    0x10000ULL
#define LX_MMAP_GAP         (128ULL * 1024 * 1024)
#define LX_VMA_HUGETLB      0x00020000
#define LX_SPEC_MODE_UNSUPPORTED (-1)
#define LX_SPEC_MODE_UNAFFECTED  0
#define LX_SPEC_MODE_DYNAMIC     1
#define LX_SPEC_MODE_VULNERABLE  2
#define LX_SPEC_SSB_DISABLE      0x01
#define LX_SPEC_SSB_FORCE        0x02
#define LX_SPEC_SSB_NOEXEC       0x04
#define LX_JOBCTL_STOPPED        0x01
#define LX_JOBCTL_CONTINUED      0x02
#define LX_KTIME_SEC_MAX         (MAXLONG64 / 1000000000LL)
#define LX_KTIME_MAX_100NS       (MAXLONG64 / 100)
#define LX_VMA_DONTDUMP     0x00040000
#define LX_VMA_TOUCHED      0x00080000
#define LX_VMA_WIPEONFORK   0x00100000
#define LX_VMA_DONTFORK     0x00200000
#define LX_VMA_POISON       0x00400000
#define LX_VMA_FORKED       0x00800000
#define LX_VMA_NOMAYWRITE   0x01000000
#define LX_VMA_LOCKONFAULT  0x02000000
#define LX_VMA_LOCKED       0x04000000
#define LX_VMA_LOCK_MASK    (LX_VMA_LOCKED | LX_VMA_LOCKONFAULT)
#define LX_VMA_DIRTY        0x08000000
#define LX_VMA_SHM          0x10000000
#define LX_VMA_VIEW         0x20000000
#define LX_VMA_HEAP         0x40000000
#define LX_VMA_STACK        0x80000000
#define LX_MAX_SYMLINKS     40

#define LX_SYSCALL_NO_RESULT ((LONG64)0x7FFFFFFFFFFFFFFFLL)

#define LX_KERNEL_RELEASE       "7.0.0-libernt"
#define LX_KERNEL_UTS_VERSION   "#1 SMP PREEMPT_DYNAMIC LiberNT"
#define LX_KERNEL_VERSION       "Linux version " LX_KERNEL_RELEASE " (lxcore@libernt) " LX_KERNEL_UTS_VERSION

#define LX_ALIGN_DOWN(v, a) ((v) & ~((ULONG64)(a) - 1))
#define LX_ALIGN_UP(v, a)   (((v) + (ULONG64)(a) - 1) & ~((ULONG64)(a) - 1))

typedef enum _LX_NET_SYSCTL
{
    LxNetSomaxconn,
    LxNetConfAllTag,
    LxNetConfDefaultTag,
    LxNetConfLoTag,
    LX_NET_SYSCTL_COUNT
} LX_NET_SYSCTL;

typedef struct _LX_FILE LX_FILE, *PLX_FILE;
typedef struct _LX_RATELIMIT
{
    LONG64 Begin;
    LONG Printed;
    LONG Missed;
} LX_RATELIMIT, *PLX_RATELIMIT;
typedef struct _LX_PROCESS LX_PROCESS, *PLX_PROCESS;
typedef struct _LX_THREAD LX_THREAD, *PLX_THREAD;
typedef struct _LX_PTRACE LX_PTRACE, *PLX_PTRACE;
typedef struct _LX_SECCOMP_FILTER LX_SECCOMP_FILTER, *PLX_SECCOMP_FILTER;
typedef struct _LX_INSTANCE LX_INSTANCE, *PLX_INSTANCE;
typedef struct _LX_MOUNT LX_MOUNT, *PLX_MOUNT;
typedef struct _LX_CGROUP LX_CGROUP, *PLX_CGROUP;
typedef struct _LX_NS LX_NS, *PLX_NS;
typedef struct _LX_KEY LX_KEY, *PLX_KEY;
typedef struct _LX_TMPFS LX_TMPFS, *PLX_TMPFS;
typedef struct _LX_SQUASHFS LX_SQUASHFS, *PLX_SQUASHFS;
typedef struct _LX_LOOP LX_LOOP, *PLX_LOOP;
typedef struct _LX_TMPFS_INODE LX_TMPFS_INODE, *PLX_TMPFS_INODE;

typedef enum _LX_NS_KIND
{
    LxNsMnt,
    LxNsUts,
    LxNsIpc,
    LxNsPid,
    LxNsNet,
    LxNsCgroup,
    LxNsUser,
    LxNsTime,
    LxNsCount
} LX_NS_KIND, *PLX_NS_KIND;

#define LX_PIDNS_MAX_LEVEL  32
#define LX_USERNS_MAX_PARENT_LEVEL  32
#define LX_ID_MAP_MAX_EXTENTS       340
#define LX_THREADS_MAX              63429

typedef struct _LX_ID_EXTENT
{
    ULONG First;
    ULONG LowerFirst;
    ULONG Count;
} LX_ID_EXTENT, *PLX_ID_EXTENT;

typedef struct _LX_ID_MAP
{
    ULONG Count;
    LX_ID_EXTENT Extents[LX_ID_MAP_MAX_EXTENTS];
} LX_ID_MAP, *PLX_ID_MAP;

typedef struct _LX_VMA
{
    LIST_ENTRY Link;
    RTL_BALANCED_NODE TreeNode;
    ULONG64 Start;
    ULONG64 End;
    ULONG Prot;
    ULONG Flags;
    PLX_FILE File;
    ULONG64 Offset;
    PVOID Section;
    ULONG64 ViewBase;
    ULONG64 ViewOffset;
    ULONG64 MapEnd;
} LX_VMA, *PLX_VMA;

typedef struct _LX_POLL_HEAD
{
    KSPIN_LOCK Lock;
    LIST_ENTRY Entries;
} LX_POLL_HEAD, *PLX_POLL_HEAD;

typedef struct _LX_POLL_ENTRY LX_POLL_ENTRY, *PLX_POLL_ENTRY;
typedef VOID (*PLX_POLL_NOTIFY)(_In_ PLX_POLL_ENTRY Entry, _In_ ULONG Events);

struct _LX_POLL_ENTRY
{
    LIST_ENTRY Link;
    PLX_POLL_HEAD Head;
    PLX_POLL_NOTIFY Notify;
    PVOID Context;
};

typedef struct _LX_EVENT_SOURCE
{
    LX_POLL_HEAD Poll;
    volatile LONG64 Generation;
} LX_EVENT_SOURCE, *PLX_EVENT_SOURCE;

typedef struct _LX_HUGE_STORE
{
    ULONG Count;
    PVOID *Pages;
    PUCHAR Faulted;
} LX_HUGE_STORE, *PLX_HUGE_STORE;

typedef struct _LX_MEMFD
{
    volatile LONG RefCount;
    volatile LONG Seals;
    volatile LONG WritableMaps;
    HANDLE Owner;
    PLX_HUGE_STORE Huge;
} LX_MEMFD, *PLX_MEMFD;

typedef struct _LX_FILE_OPS
{
    LONG64 (*Read)(_In_ PLX_FILE File, _In_ ULONG64 Buffer, _In_ ULONG64 Length, _Inout_opt_ PLONG64 Offset);
    LONG64 (*Write)(_In_ PLX_FILE File, _In_ ULONG64 Buffer, _In_ ULONG64 Length, _Inout_opt_ PLONG64 Offset);
    LONG64 (*Ioctl)(_In_ PLX_FILE File, _In_ ULONG Request, _In_ ULONG64 Argument);
    ULONG (*Poll)(_In_ PLX_FILE File, _In_opt_ PLX_POLL_ENTRY Entry);
    LONG (*Stat)(_In_ PLX_FILE File, _Out_ PLX_STAT Stat);
    LONG64 (*Seek)(_In_ PLX_FILE File, _In_ LONG64 Offset, _In_ ULONG Whence);
    LONG64 (*Getdents)(_In_ PLX_FILE File, _In_ ULONG64 Buffer, _In_ ULONG Length);
    LONG (*Truncate)(_In_ PLX_FILE File, _In_ LONG64 Length);
    VOID (*Release)(_In_ PLX_FILE File);
    LONG64 (*WriteVector)(_In_ PLX_FILE File, _In_reads_(Count) PLX_IOVEC Vectors, _In_ ULONG Count,
                          _Inout_opt_ PLONG64 Offset);
} LX_FILE_OPS, *PLX_FILE_OPS;

#define LX_TTY_BUFFER 4096
#define LX_TTY_CONSOLE   0x00000001
#define LX_TTY_LOCKED    0x00000002
#define LX_TTY_NT_MASTER 0x00000004

typedef struct _LX_DEVPTS
{
    LIST_ENTRY Link;
    LIST_ENTRY Ptys;
    ULONG PtmxMode;
    ULONG Mode;
    ULONG Uid;
    ULONG Gid;
    ULONG Max;
    BOOLEAN UidSet;
    BOOLEAN GidSet;
} LX_DEVPTS, *PLX_DEVPTS;

typedef struct _LX_TTY
{
    volatile LONG RefCount;
    KEVENT Lock;
    KEVENT DataReady;
    KEVENT ReadLock;
    KEVENT InputSpace;
    KEVENT OutputReady;
    KEVENT OutputSpace;
    LIST_ENTRY Link;
    LONG Index;
    ULONG Uid;
    ULONG Gid;
    ULONG Mode;
    LX_TERMIOS Termios;
    LX_WINSIZE WinSize;
    ULONG ForegroundPgrp;
    ULONG Session;
    ULONG SlaveOpens;
    BOOLEAN Hangup;
    BOOLEAN Locked;
    BOOLEAN SlaveOpened;
    BOOLEAN NtMaster;
    BOOLEAN EofPending;
    ULONG LineLength;
    ULONG ReadyLength;
    ULONG OutputLength;
    LX_POLL_HEAD Poll;
    LX_POLL_HEAD MasterPoll;
    PLX_INSTANCE Instance;
    UCHAR Line[LX_TTY_BUFFER];
    UCHAR Ready[LX_TTY_BUFFER];
    UCHAR Output[LX_TTY_BUFFER];
} LX_TTY, *PLX_TTY;

struct _LX_FILE
{
    volatile LONG RefCount;
    const LX_FILE_OPS *Ops;
    ULONG Flags;
    ULONG Type;
    PSTR Path;
    PLX_MOUNT Mount;
    HANDLE Handle;
    PVOID Object;
    BOOLEAN Directory;
    BOOLEAN NtPipe;
    KEVENT Lock;
    LONG64 Position;
    PUCHAR Data;
    ULONG DataLength;
    PUCHAR DirBuffer;
    ULONG DirBufferUsed;
    ULONG DirBufferPos;
    BOOLEAN DirRestart;
    BOOLEAN DirEnd;
    LONG64 DirOffset;
    ULONG64 Ino;
    BOOLEAN PollOnly;
    KSPIN_LOCK EpollLock;
    LIST_ENTRY EpollItems;
    ULONG64 Rdev;
    LONG (*WriteHandler)(_In_ PLX_FILE File, _In_reads_bytes_(Length) PCSTR Data, _In_ ULONG Length);
    PVOID HandlerContext;
    PLX_MEMFD Memfd;
    ULONG Mode;
    BOOLEAN Written;
    PLX_EVENT_SOURCE EventSource;
    LONG64 EventSeen;
    PLX_PROCESS PidProcess;
    struct _LX_FILE_ASYNC *Async;
    LONG PathGeneration;
    PLX_NS MountsNs;
    PSTR MountsRoot;
    BOOLEAN MountsFull;
    BOOLEAN MountsRead;
    PSTR SynthRelative;
    BOOLEAN Flocked;
};

typedef enum _LX_FS_KIND
{
    LxFsNt = 1,
    LxFsProc,
    LxFsSys,
    LxFsCgroup,
    LxFsDevpts,
    LxFsNsfs,
    LxFsMqueue,
    LxFsTmpfs
} LX_FS_KIND;

struct _LX_MOUNT
{
    LIST_ENTRY Link;
    LIST_ENTRY InstanceLink;
    PLX_INSTANCE Instance;
    PLX_NS Ns;
    PLX_MOUNT Parent;
    PLX_NS NsFile;
    PLX_NS PidNs;
    PLX_CGROUP CgroupRoot;
    ULONG Id;
    ULONG ParentId;
    LX_FS_KIND Kind;
    PSTR Target;
    PSTR Source;
    PSTR FsType;
    ULONG Flags;
    ULONG64 Magic;
    ULONG Dev;
    UNICODE_STRING NtRoot;
    HANDLE NtRootHandle;
    PLX_TMPFS Tmpfs;
    PLX_DEVPTS Devpts;
    PSTR Root;
    PSTR Alias;
    BOOLEAN Detached;
    BOOLEAN Unbindable;
    ULONG PeerGroup;
    ULONG MasterGroup;
};

#define LX_MS_RDONLY        0x00000001
#define LX_MS_NOSUID        0x00000002
#define LX_MS_NODEV         0x00000004
#define LX_MS_NOEXEC        0x00000008
#define LX_MS_REMOUNT       0x00000020
#define LX_MS_MGC_VAL       0xC0ED0000
#define LX_MS_MGC_MSK       0xFFFF0000
#define LX_MS_NOATIME       0x00000400
#define LX_MS_BIND          0x00001000
#define LX_MS_MOVE          0x00002000
#define LX_MS_REC           0x00004000
#define LX_MS_SILENT        0x00008000
#define LX_MS_UNBINDABLE    0x00020000
#define LX_MS_PRIVATE       0x00040000
#define LX_MS_SLAVE         0x00080000
#define LX_MS_SHARED        0x00100000
#define LX_MS_RELATIME      0x00200000
#define LX_MS_STRICTATIME   0x01000000

#define LX_MNT_FORCE        0x00000001
#define LX_MNT_DETACH       0x00000002
#define LX_MNT_EXPIRE       0x00000004
#define LX_UMOUNT_NOFOLLOW  0x00000008

#define LX_NSFS_MAGIC       0x6E736673ULL
#define LX_PROC_ROOT_INO    1
#define LX_NS_GET_USERNS    0xB701
#define LX_NS_GET_PARENT    0xB702
#define LX_NS_GET_NSTYPE    0xB703
#define LX_NS_GET_OWNER_UID 0xB704

struct _LX_NS
{
    volatile LONG RefCount;
    volatile LONG PassiveRefs;
    LIST_ENTRY Link;
    LX_NS_KIND Kind;
    ULONG64 Inum;
    PLX_INSTANCE Instance;
    PLX_NS Parent;
    PLX_NS Owner;
    ULONG Level;
    union
    {
        struct
        {
            LIST_ENTRY Mounts;
            LX_EVENT_SOURCE Events;
        } Mnt;
        struct
        {
            CHAR Hostname[65];
            CHAR DomainName[65];
        } Uts;
        struct
        {
            ULONG NextPid;
            ULONG ReaperPid;
            BOOLEAN HasInit;
            BOOLEAN Dead;
        } Pid;
        struct
        {
            BOOLEAN LoopbackUp;
            BOOLEAN LoopbackAddressed;
            LONG Sysctl[LX_NET_SYSCTL_COUNT];
        } Net;
        struct
        {
            PLX_CGROUP Root;
        } Cgroup;
        struct
        {
            PLX_ID_MAP Maps[2];
            ULONG OwnerUid;
            ULONG OwnerGid;
            LONG MaxUserNamespaces;
            BOOLEAN SetgroupsDenied;
            BOOLEAN ParentCouldSetfcap;
            BOOLEAN Charged;
            PLX_KEY KeyRegister;
            PLX_KEY PersistentRegister;
        } User;
        struct
        {
            LONG64 Monotonic;
            LONG64 Boottime;
            BOOLEAN Frozen;
        } Time;
    };
};

typedef struct _LX_FS_STATE
{
    volatile LONG RefCount;
    FAST_MUTEX Lock;
    PSTR Cwd;
    PSTR Root;
} LX_FS_STATE, *PLX_FS_STATE;

struct _LX_INSTANCE
{
    volatile LONG RefCount;
    LIST_ENTRY Link;
    UNICODE_STRING RootNt;
    UNICODE_STRING TmpNt;
    FAST_MUTEX Lock;
    LIST_ENTRY AllMounts;
    PLX_NS RootNs[LxNsCount];
    ULONG NextMountId;
    ULONG NextPid;
    CHAR BootId[37];
    LONG64 BootTime;
    PLX_CGROUP RootCgroup;
    PLX_TTY Console;
    LIST_ENTRY DevptsList;
    PLX_DEVPTS Devpts;
    ULONG ProcessCount;
    BOOLEAN Booted;
    BOOLEAN ShuttingDown;
    LONG RebootCommand;
    KEVENT InitExited;
    LONG InitStatus;
    ULONG NextDetachedId;
    ULONG NextPeerGroup;
    LIST_ENTRY Loops;
};

typedef struct _LX_NODE
{
    PSTR Path;
    PLX_MOUNT Mount;
    PCSTR Relative;
    UNICODE_STRING NtPath;
    BOOLEAN Exists;
    ULONG Type;
    PSTR LinkTarget;
} LX_NODE, *PLX_NODE;

typedef struct _LX_TMPFS_OPTIONS
{
    ULONG Mode;
    ULONG Uid;
    ULONG Gid;
    ULONG64 Blocks;
    ULONG64 Inodes;
    BOOLEAN SeenBlocks;
    BOOLEAN SeenInodes;
} LX_TMPFS_OPTIONS, *PLX_TMPFS_OPTIONS;

#define LX_LOOKUP_FOLLOW    0x00000001
#define LX_LOOKUP_DIRECTORY 0x00000002
#define LX_LOOKUP_CREATE    0x00000004
#define LX_LOOKUP_NO_XDEV       0x00000010
#define LX_LOOKUP_NO_MAGICLINKS 0x00000020
#define LX_LOOKUP_NO_SYMLINKS   0x00000040
#define LX_LOOKUP_BENEATH       0x00000080
#define LX_LOOKUP_IN_ROOT       0x00000100
#define LX_LOOKUP_RESOLVE       0x000001F0
#define LX_LOOKUP_ACCESS        0x00000200

#define LX_MAY_EXEC         0x00000001
#define LX_MAY_WRITE        0x00000002
#define LX_MAY_READ         0x00000004

typedef struct _LX_ITIMER
{
    PEX_TIMER Timer;
    WORK_QUEUE_ITEM Work;
    PLX_PROCESS Process;
    volatile LONG WorkQueued;
    LONG64 IntervalNs;
    LONG64 Due;
} LX_ITIMER, *PLX_ITIMER;

typedef struct _LX_USAGE
{
    LONG64 UserTime;
    LONG64 KernelTime;
    ULONG64 MaxRss;
    ULONG64 MinFlt;
    ULONG64 VirtualSize;
    ULONG64 PeakVirtualSize;
    ULONG64 WorkingSet;
    ULONG64 PrivateBytes;
} LX_USAGE, *PLX_USAGE;

typedef struct _LX_CHILD
{
    LIST_ENTRY Link;
    ULONG Pid;
    PEPROCESS Process;
    PLX_PROCESS Lx;
} LX_CHILD, *PLX_CHILD;

struct _LX_CGROUP
{
    LIST_ENTRY Sibling;
    LIST_ENTRY Children;
    LIST_ENTRY DeadLink;
    PLX_CGROUP Parent;
    PLX_CGROUP Root;
    PLX_INSTANCE Instance;
    PSTR Name;
    CHAR SubtreeControl[64];
    ULONG Uid;
    ULONG Gid;
    ULONG FileUid;
    ULONG FileGid;
    ULONG Mode;
    LIST_ENTRY FileAttributes;
    ULONG Members;
    ULONG Populated;
    ULONG64 Id;
    BOOLEAN Frozen;
    BOOLEAN Removed;
    LX_EVENT_SOURCE Events;
    LONG64 Created;
    LIST_ENTRY DeadList;
    LIST_ENTRY BpfDevice;
    ULONG BpfDeviceFlags;
    ULONG SubtreeMask;
    ULONG64 CpusetCpus;
    ULONG64 CpusetMems;
    LONG64 PidsLimit;
    LONG64 PidsCount;
    LONG64 PidsPeak;
    LONG64 PidsEvents;
    LONG64 PidsEventsLocal;
};

typedef struct _LX_CRED
{
    ULONG Uid[3];
    ULONG Gid[3];
    ULONG Fsuid;
    ULONG Fsgid;
    ULONG GroupCount;
    ULONG Groups[32];
    ULONG SecureBits;
    ULONG64 CapInheritable;
    ULONG64 CapPermitted;
    ULONG64 CapEffective;
    ULONG64 CapBounding;
    ULONG64 CapAmbient;
} LX_CRED, *PLX_CRED;

struct _LX_PROCESS
{
    volatile LONG RefCount;
    struct _LX_PROCESS *Mm;
    LIST_ENTRY GlobalLink;
    PLX_INSTANCE Instance;
    ULONG Pid;
    ULONG NtPid;
    ULONG ParentPid;
    ULONG Pgid;
    ULONG Sid;
    PLX_TTY Tty;
    volatile LONG ThreadCount;
    FAST_MUTEX Lock;
    PLX_FILE Fds[LX_MAX_FDS];
    UCHAR FdFlags[LX_MAX_FDS];
    PSTR Cwd;
    PSTR ExePath;
    ULONG Umask;
    KEVENT MmLock;
    PKTHREAD MmLockOwner;
    ULONG MmLockDepth;
    LIST_ENTRY Vmas;
    RTL_RB_TREE VmaTree;
    LONG VmaCount;
    LIST_ENTRY Guards;
    ULONG64 BrkBase;
    ULONG64 BrkCurrent;
    ULONG64 BrkCommitted;
    ULONG64 BrkLimit;
    BOOLEAN BrkReserved;
    ULONG MmLockDefault;
    BOOLEAN SharedVm;
    BOOLEAN OwnerGone;
    volatile LONG VmUsers;
    PVOID ExeDeny;
    ULONG CoreFilter;
    LX_SIGACTION SigAction[LX_NSIG];
    LIST_ENTRY Children;
    LIST_ENTRY Threads;
    volatile LONG64 Pending;
    LX_SIGINFO PendingInfo[LX_NSIG];
    ULONG64 SigTrampoline;
    LONG WaitStatus;
    BOOLEAN WaitStatusValid;
    BOOLEAN Exited;
    BOOLEAN ChildSubreaper;
    KEVENT VforkDone;
    PSTR Cmdline;
    ULONG CmdlineLength;
    PSTR Environ;
    ULONG EnvironLength;
    CHAR Comm[16];
    PLX_CGROUP Cgroup;
    PLX_NS Ns[LxNsCount];
    PLX_NS PidNs;
    PLX_NS TimeNs;
    PSTR Root;
    ULONG NsPid[LX_PIDNS_MAX_LEVEL];
    LONG64 StartTime;
    LONG OomScoreAdj;
    ULONG ParentTid;
    LX_RLIMIT Limits[16];
    LX_POLL_HEAD ExitPoll;
    LX_POLL_HEAD SignalPoll;
    LX_POLL_HEAD ChildPoll;
    LX_CRED Cred;
    ULONG Dumpable;
    BOOLEAN Execed;
    ULONG ExitSignal;
    ULONG64 MinFlt;
    LX_USAGE ExitUsage;
    BOOLEAN ExitUsageValid;
    LONG64 ChildUserTime;
    LONG64 ChildKernelTime;
    ULONG64 ChildMinFlt;
    ULONG64 ChildMaxRss;
    ULONG64 StartCode;
    ULONG64 EndCode;
    ULONG64 StartData;
    ULONG64 EndData;
    ULONG64 StartStack;
    ULONG64 ArgStart;
    ULONG64 ArgEnd;
    ULONG64 EnvStart;
    ULONG64 EnvEnd;
    ULONG64 Auxv[LX_AUXV_WORDS];
    LONG64 ExitTime;
    PLX_ITIMER RealTimer;
    ULONG Trace;
    LIST_ENTRY Timers;
    LONG NextTimerId;
    volatile LONG Membarrier;
    ULONG JobctlFlags;
    LONG GroupExitCode;
    ULONG GroupStopCount;
    volatile LONG ContinueGeneration;
    KEVENT ContinueEvent;
    LIST_ENTRY Tracees;
    LONG64 Ptracer;
    volatile LONG Traced;
    ULONG Personality;
    ULONG LoginUid;
    ULONG SessionId;
    ULONG ThpDisable;
    ULONG64 MmapTop;
    ULONG64 StackTop;
};

typedef struct _LX_CRED_SAVE
{
    ULONG Uid[3];
    ULONG Gid[3];
    ULONG Fsuid;
    ULONG Fsgid;
    ULONG64 CapPermitted;
} LX_CRED_SAVE, *PLX_CRED_SAVE;

typedef struct _LX_FILE_CAPS
{
    ULONG64 Permitted;
    ULONG64 Inheritable;
    BOOLEAN Effective;
    BOOLEAN Present;
} LX_FILE_CAPS, *PLX_FILE_CAPS;

#define LX_CRED_EXEC_SECURE     0x1
#define LX_CRED_EXEC_GAINED     0x2

typedef struct _LX_EXEC_REQUEST
{
    PSTR FileName;
    PLX_FILE File;
    PSTR Args;
    ULONG ArgLength;
    ULONG ArgCount;
    PSTR Env;
    ULONG EnvLength;
    ULONG EnvCount;
    NTSTATUS Status;
    LONG Errno;
    KEVENT Done;
    BOOLEAN User;
    BOOLEAN RuntimeDir;
    BOOLEAN SearchPath;
    BOOLEAN Vfork;
    PLX_THREAD Origin;
    ULONG Uid;
    ULONG Gid;
    ULONG GroupCount;
    ULONG Groups[LX_CREATE_MAX_GROUPS];
} LX_EXEC_REQUEST, *PLX_EXEC_REQUEST;

struct _LX_THREAD
{
    PLX_PROCESS Process;
    ULONG Tid;
    ULONG NsTid[LX_PIDNS_MAX_LEVEL];
    ULONG NtTid;
    ULONG64 ClearChildTid;
    ULONG64 SetChildTid;
    ULONG64 SigMask;
    ULONG64 Tls;
    PLX_EXEC_REQUEST Exec;
    PETHREAD EThread;
    ULONG64 FaultAddress;
    ULONG FaultRetries;
    UCHAR FaultBus;
    LIST_ENTRY Link;
    volatile LONG64 Pending;
    LX_STACK AltStack;
    LX_SCHED_ATTR Sched;
    ULONG64 Affinity;
    ULONG64 UserAffinity;
    ULONG64 SuspendMask;
    BOOLEAN Suspended;
    CHAR Comm[16];
    ULONG CurrentSyscall;
    LONG InKernel;
    volatile LONG SignalApc;
    volatile LONG SharedWake;
    PEX_TIMER SignalRetry;
    ULONG64 CurrentArgs[6];
    ULONG64 RobustList;
    LONG64 Restart;
    ULONG64 RestartArgument;
    PLX_NS NsOverride[LxNsCount];
    PLX_FS_STATE Fs;
    struct _LX_THREAD *Vfork;
    struct _LX_THREAD *VforkParent;
    PCONTEXT VforkContext;
    PCONTEXT StartContext;
    ULONG64 VforkTls;
    LONG64 VforkPid;
    BOOLEAN VforkExited;
    volatile UCHAR SeccompMode;
    PLX_SECCOMP_FILTER Seccomp;
    PVOID volatile SeccompNotif;
    UCHAR SpecSsb;
    volatile BOOLEAN JobctlStop;
    volatile BOOLEAN Stopped;
    PLX_PTRACE Ptrace;
    volatile LONG PtraceWork;
    LONG ExitCode;
    ULONG PtraceVforkPid;
    PLX_CRED Cred;
    LX_CRED OwnCred;
    ULONG PdeathSignal;
    BOOLEAN NoNewPrivs;
    PLX_KEY ThreadKeyring;
    PLX_KEY ProcessKeyring;
    PLX_KEY SessionKeyring;
    PLX_KEY RequestKeyAuth;
    LONG JitKeyring;
};

#define LX_PTRACE_REGSET_MAX 528
#define LX_PTW_SYSCALL  0x01
#define LX_PTW_TRAP     0x02

struct _LX_PTRACE
{
    LIST_ENTRY Link;
    volatile LONG RefCount;
    PLX_PROCESS Tracer;
    PLX_PROCESS Process;
    PLX_THREAD Thread;
    ULONG Tid;
    ULONG Options;
    BOOLEAN Seized;
    BOOLEAN Stopped;
    BOOLEAN Exited;
    BOOLEAN Listening;
    BOOLEAN ContextDirty;
    BOOLEAN HasSiginfo;
    BOOLEAN SingleStep;
    UCHAR SyscallOp;
    LONG Report;
    LONG ExitStatus;
    LONG ResumeSignal;
    LONG SyscallNr;
    ULONG SeccompData;
    ULONG64 Message;
    ULONG64 SyscallArgs[6];
    LONG64 SyscallRet;
    ULONG64 Tls;
    PCONTEXT Context;
    LX_SIGINFO Siginfo;
    volatile LONG Generation;
    KEVENT Resume;
};

typedef struct _LX_CALL
{
    PLX_THREAD Thread;
    PLX_PROCESS Process;
    PKTRAP_FRAME TrapFrame;
    PKEXCEPTION_FRAME ExceptionFrame;
    ULONG Number;
    ULONG64 Args[6];
} LX_CALL, *PLX_CALL;

typedef LONG64 (*PLX_SYSCALL_HANDLER)(_Inout_ PLX_CALL Call);

typedef struct _LX_SYSCALL_ENTRY
{
    PLX_SYSCALL_HANDLER Handler;
    PCSTR Name;
    BOOLEAN MayBlock;
} LX_SYSCALL_ENTRY, *PLX_SYSCALL_ENTRY;

typedef struct _LX_ELF_INFO
{
    ULONG64 Entry;
    ULONG64 Phdr;
    ULONG64 PhEnt;
    ULONG64 PhNum;
    ULONG64 LoadBias;
    ULONG64 ImageEnd;
    ULONG64 StartCode;
    ULONG64 EndCode;
    ULONG64 StartData;
    ULONG64 EndData;
} LX_ELF_INFO, *PLX_ELF_INFO;

typedef struct _LX_ELF64_EHDR
{
    UCHAR Ident[16];
    USHORT Type;
    USHORT Machine;
    ULONG Version;
    ULONG64 Entry;
    ULONG64 PhOff;
    ULONG64 ShOff;
    ULONG Flags;
    USHORT EhSize;
    USHORT PhEntSize;
    USHORT PhNum;
    USHORT ShEntSize;
    USHORT ShNum;
    USHORT ShStrNdx;
} LX_ELF64_EHDR, *PLX_ELF64_EHDR;

typedef struct _LX_ELF64_PHDR
{
    ULONG Type;
    ULONG Flags;
    ULONG64 Offset;
    ULONG64 VAddr;
    ULONG64 PAddr;
    ULONG64 FileSz;
    ULONG64 MemSz;
    ULONG64 Align;
} LX_ELF64_PHDR, *PLX_ELF64_PHDR;

C_ASSERT(sizeof(LX_ELF64_EHDR) == 64);
C_ASSERT(sizeof(LX_ELF64_PHDR) == 56);

#define LX_ELFCLASS64   2
#define LX_ELFDATA2LSB  1
#define LX_ET_EXEC      2
#define LX_ET_DYN       3
#define LX_ET_CORE      4
#define LX_PT_LOAD      1
#define LX_PT_INTERP    3
#define LX_PT_NOTE      4
#define LX_PT_PHDR      6
#define LX_PF_X         1
#define LX_PF_W         2
#define LX_PF_R         4

extern LIST_ENTRY LxProcessList;
extern volatile LONG LxMaxMapCount;

NTSYSAPI VOID NTAPI RtlRbInsertNodeEx(_Inout_ PRTL_RB_TREE Tree, _In_opt_ PRTL_BALANCED_NODE Parent,
                                      _In_ BOOLEAN Right, _Inout_ PRTL_BALANCED_NODE Node);
NTSYSAPI VOID NTAPI RtlRbRemoveNode(_Inout_ PRTL_RB_TREE Tree, _In_ PRTL_BALANCED_NODE Node);
NTKERNELAPI NTSTATUS NTAPI PsGetContextThread(_In_ PETHREAD Thread, _Inout_ PCONTEXT ThreadContext,
                                              _In_ KPROCESSOR_MODE PreviousMode);
NTKERNELAPI NTSTATUS NTAPI PsSetContextThread(_In_ PETHREAD Thread, _In_ PCONTEXT ThreadContext,
                                              _In_ KPROCESSOR_MODE PreviousMode);
NTKERNELAPI NTSTATUS ExGetSystemFirmwareTable(_In_ ULONG FirmwareTableProviderSignature, _In_ ULONG FirmwareTableID,
                                              _Out_writes_bytes_to_opt_(BufferLength, *ReturnLength) PVOID FirmwareTableBuffer,
                                              _In_ ULONG BufferLength, _Out_opt_ PULONG ReturnLength);
extern FAST_MUTEX LxProcessListLock;
extern LIST_ENTRY LxInstanceList;
extern FAST_MUTEX LxInstanceListLock;
extern KEVENT LxInstanceCreateLock;

VOID LxProcessInitialize(VOID);
VOID LxHugeInitialize(VOID);
ULONG64 LxHugePageSize(VOID);
ULONG64 LxHugeQueryPool(_Out_opt_ PULONG64 Free, _Out_opt_ PULONG64 Surplus, _Out_opt_ PULONG64 Reserved);
VOID LxHugeSetPool(_In_ ULONG64 Count);
LONG LxHugeStoreReserve(_Inout_ PLX_HUGE_STORE Store, _In_ ULONG64 First, _In_ ULONG64 Count, _In_ BOOLEAN Surplus);
VOID LxHugeStoreTruncate(_Inout_ PLX_HUGE_STORE Store, _In_ ULONG64 Count);
PVOID LxHugeStorePage(_In_ PLX_HUGE_STORE Store, _In_ ULONG64 Index);
VOID LxHugeStoreFault(_In_ PLX_HUGE_STORE Store, _In_ ULONG64 Index);
NTSTATUS LxHugeMapPage(_In_ PVOID Section, _In_ HANDLE ProcessHandle, _Inout_ PVOID *Base, _In_ ULONG Protect);
NTSTATUS LxHugeCopyPage(_In_ PVOID From, _In_ PVOID To);
LONG64 LxHugeStoreRead(_In_ PLX_HUGE_STORE Store, _In_ ULONG64 Offset, _In_ ULONG64 Buffer, _In_ ULONG64 Length);
PLX_FILE LxHugeFileCreate(_In_ PCSTR Path);
PVOID LxAlloc(_In_ SIZE_T Size);
VOID LxFree(_In_opt_ PVOID Buffer);
PVOID LxAllocPaged(_In_ SIZE_T Size);
VOID LxFreePaged(_In_opt_ PVOID Buffer);
PSTR LxStrDup(_In_ PCSTR String);
PSTR LxStrPrintf(_In_ PCSTR Format, ...);

LONG LxStatusToErrno(_In_ NTSTATUS Status);
LONG64 LxCopyFromUser(_Out_writes_bytes_(Length) PVOID Destination, _In_ ULONG64 Source, _In_ SIZE_T Length);
LONG64 LxCopyToUser(_In_ ULONG64 Destination, _In_reads_bytes_(Length) const VOID *Source, _In_ SIZE_T Length);
LONG64 LxZeroUser(_In_ ULONG64 Destination, _In_ SIZE_T Length);
LONG64 LxCopyStringFromUser(_In_ ULONG64 Source, _Out_ PSTR *String, _In_ SIZE_T MaxLength);
NTSTATUS LxFillRandom(_Out_writes_bytes_(Length) PUCHAR Buffer, _In_ ULONG Length);
VOID LxNtTimeToTimespec(_In_ LONG64 NtTime, _Out_ PLX_TIMESPEC Timespec);
LONG64 LxTimespecToNtTime(_In_ PLX_TIMESPEC Timespec);
LONG64 LxTimespecToNs(_In_ PLX_TIMESPEC Timespec);
LONG64 LxTimespecTo100ns(_In_ PLX_TIMESPEC Timespec);
LONG64 LxTimevalTo100ns(_In_ PLX_TIMEVAL Timeval);
LONG64 LxSocketTimeout(_In_ PLX_TIMEVAL Timeval);
VOID LxSchedInherit(_Inout_ PLX_THREAD Child, _In_ PLX_THREAD Parent);
VOID LxSchedInitialize(VOID);
ULONG64 LxSchedActiveMask(VOID);
VOID LxSchedApplyAffinity(_In_ PLX_THREAD Thread);
ULONG64 LxSchedCombineAffinity(_In_ ULONG64 Requested, _In_ ULONG64 Allowed);
ULONG64 LxCgroupCpusAllowed(_In_ PLX_PROCESS Process);
VOID LxCgroupRefreshAffinity(_In_ PLX_INSTANCE Instance, _In_ PLX_CGROUP Cgroup);
VOID LxSchedRefreshProcess(_In_ PLX_PROCESS Process);
VOID LxFormatCpuList(_In_ ULONG64 Mask, _Out_writes_(Size) PSTR Buffer, _In_ SIZE_T Size);

typedef struct _LX_MEMORY_STATE
{
    ULONG64 Total;
    ULONG64 Free;
    ULONG64 Available;
    ULONG64 Cached;
    ULONG64 SwapTotal;
    ULONG64 SwapFree;
    ULONG64 CommitLimit;
    ULONG64 Committed;
} LX_MEMORY_STATE, *PLX_MEMORY_STATE;

VOID LxQueryMemoryState(_Out_ PLX_MEMORY_STATE State);

#define LX_KEY_INHERIT_THREAD   0
#define LX_KEY_INHERIT_PROCESS  1
#define LX_KEY_INHERIT_TASK     2

VOID LxKeyInitialize(VOID);
LONG LxKeyThreadInherit(_Inout_ PLX_THREAD Thread, _In_ PLX_THREAD Parent, _In_ ULONG Mode);
VOID LxKeyThreadRelease(_Inout_ PLX_THREAD Thread);
VOID LxKeyExec(_Inout_ PLX_THREAD Thread);
VOID LxKeyUserNsRelease(_Inout_ PLX_NS UserNs);
PSTR LxKeyFormatProcKeys(_In_ PLX_THREAD Reader, _Out_ PULONG Length);
PSTR LxKeyFormatProcKeyUsers(_In_ PLX_PROCESS Reader, _Out_ PULONG Length);
BOOLEAN LxKeySysctlGet(_In_ PCSTR Name, _Out_ PLONG Value);
LONG LxKeySysctlSet(_In_ PCSTR Name, _In_ LONG64 Value);

typedef VOID (*PLX_UMH_INIT)(_Inout_ PLX_THREAD Thread, _In_opt_ PVOID Context);
LONG LxUserModeHelper(_In_ PLX_INSTANCE Instance, _In_ PCSTR Path, _In_reads_(ArgCount) PCSTR *Argv, _In_ ULONG ArgCount,
                      _In_reads_(EnvCount) PCSTR *Envp, _In_ ULONG EnvCount, _In_opt_ PLX_UMH_INIT Init,
                      _In_opt_ PVOID Context, _Out_ PLONG WaitStatus);
PLX_THREAD LxCurrentThread(VOID);
PLX_THREAD LxThreadOf(_In_ PETHREAD Thread);
BOOLEAN LxVforkFinish(_Inout_ PKTRAP_FRAME TrapFrame, _Inout_opt_ PKEXCEPTION_FRAME ExceptionFrame);
VOID LxVforkAbandon(_Inout_ PLX_THREAD Thread);

VOID LxPollHeadInit(_Out_ PLX_POLL_HEAD Head);
VOID LxFlockInitialize(VOID);
VOID LxFlockRelease(_In_ PLX_FILE File);
VOID LxEventSourceInit(_Out_ PLX_EVENT_SOURCE Source);
VOID LxEventSourceSignal(_In_ PLX_EVENT_SOURCE Source);
VOID LxPollAdd(_In_ PLX_POLL_HEAD Head, _Inout_ PLX_POLL_ENTRY Entry);
VOID LxPollRemove(_Inout_ PLX_POLL_ENTRY Entry);
VOID LxPollWake(_In_ PLX_POLL_HEAD Head, _In_ ULONG Events);
NTSTATUS LxWaitForEvent(_In_ PKEVENT Event, _In_opt_ PLARGE_INTEGER Timeout);

typedef struct _LX_PRECISE_TIMER
{
    PEX_TIMER Timer;
    KEVENT Expired;
} LX_PRECISE_TIMER, *PLX_PRECISE_TIMER;

PLX_PRECISE_TIMER LxPreciseTimerStart(_In_ LONG64 Interval100ns);
VOID LxPreciseTimerStop(_In_ PLX_PRECISE_TIMER Precise);
LONG64 LxFileWait(_In_ PLX_FILE File, _In_ ULONG Events, _In_opt_ PLARGE_INTEGER Timeout);
LONG64 LxWaitHead(_In_ PLX_POLL_HEAD Head, _In_ BOOLEAN (*Ready)(_In_ PVOID Context), _In_ PVOID Context, _In_opt_ PLARGE_INTEGER Timeout);
LONG LxQueryClock(_In_ ULONG Clock, _Out_ PLX_TIMESPEC Timespec);
BOOLEAN LxSignalPending(_In_opt_ PLX_THREAD Thread);
ULONG LxFilePoll(_In_ PLX_FILE File, _In_opt_ PLX_POLL_ENTRY Entry);

PLX_FILE LxFileAlloc(_In_ const LX_FILE_OPS *Ops, _In_ ULONG Flags, _In_ ULONG Type);
VOID LxFileReference(_In_ PLX_FILE File);
VOID LxFileDereference(_In_ PLX_FILE File);
PLX_FILE LxFdGet(_In_ PLX_PROCESS Process, _In_ LONG Fd);
LONG LxFdInstall(_In_ PLX_PROCESS Process, _In_ PLX_FILE File, _In_ LONG MinFd, _In_ BOOLEAN CloseOnExec);
LONG64 LxFdClose(_In_ PLX_PROCESS Process, _In_ LONG Fd);
VOID LxFdCloseOnExec(_In_ PLX_PROCESS Process);
LONG64 LxFileRead(_In_ PLX_FILE File, _In_ ULONG64 Buffer, _In_ ULONG64 Length, _Inout_opt_ PLONG64 Offset);
LONG64 LxFileWrite(_In_ PLX_FILE File, _In_ ULONG64 Buffer, _In_ ULONG64 Length, _Inout_opt_ PLONG64 Offset);
LONG64 LxFileWriteVector(_In_ PLX_FILE File, _In_ ULONG64 Vectors, _In_ ULONG Count, _Inout_opt_ PLONG64 Offset);
LONG64 LxFileReadKernel(_In_ PLX_FILE File, _Out_writes_bytes_(Length) PVOID Buffer, _In_ ULONG Length, _In_ ULONG64 Offset);
LONG LxMemfdCheckWrite(_In_ PLX_FILE File, _In_ LONG64 Position, _In_ ULONG64 Length);
LONG LxMemfdCheckSize(_In_ PLX_FILE File, _In_ LONG64 Size);
LONG LxFileZeroRange(_In_ PLX_FILE File, _In_ ULONG64 Start, _In_ ULONG64 End);
VOID LxMemfdDereference(_In_opt_ PLX_MEMFD Memfd);
LONG LxFileStat(_In_ PLX_FILE File, _Out_ PLX_STAT Stat);
PLX_FILE LxFileFromNtHandle(_In_ HANDLE Handle, _In_ ULONG Flags);
PLX_FILE LxFileFromNtHandleOfType(_In_ HANDLE Handle, _In_ ULONG Flags, _In_ BOOLEAN Directory);
LONG LxNtStatHandle(_In_ HANDLE Handle, _In_opt_ PLX_MOUNT Mount, _Out_ PLX_STAT Stat);
PLX_FILE LxDataFileCreate(_In_ PCSTR Path, _In_ PVOID Data, _In_ ULONG Length, _In_ ULONG Mode);
LONG64 LxWriteFailed(_In_ LONG Error);
LONG64 LxSocketGatherIov(_In_ PLX_MSGHDR Header, _In_ BOOLEAN KernelIov, _Out_ PLX_IOVEC *Vectors, _Out_ PULONG64 Total);
LONG64 LxSocketCopyIn(_In_reads_(Count) PLX_IOVEC Vectors, _In_ ULONG64 Count, _In_ ULONG64 Skip, _Out_writes_bytes_(Length) PUCHAR Buffer, _In_ ULONG Length);
LONG64 LxSocketCopyOut(_In_reads_(Count) PLX_IOVEC Vectors, _In_ ULONG64 Count, _In_ ULONG64 Skip, _In_reads_bytes_(Length) PUCHAR Buffer, _In_ ULONG Length);
LONG64 LxSockoptInt(_In_ ULONG64 Value, _In_ ULONG64 Length, _Out_ PLONG Result);
LONG64 LxSockoptReturn(_In_ PLX_CALL Call, _In_reads_bytes_(Length) const VOID *Value, _In_ ULONG Length);
typedef struct _LX_SOCKET_OPS
{
    LONG64 (*Bind)(_In_ PLX_CALL Call, _In_ PLX_FILE File);
    LONG64 (*Listen)(_In_ PLX_CALL Call, _In_ PLX_FILE File);
    LONG64 (*Connect)(_In_ PLX_CALL Call, _In_ PLX_FILE File);
    LONG64 (*Accept)(_In_ PLX_CALL Call, _In_ PLX_FILE File, _In_ ULONG Flags);
    LONG64 (*Name)(_In_ PLX_CALL Call, _In_ PLX_FILE File, _In_ BOOLEAN Peer);
    LONG64 (*Sendmsg)(_In_ PLX_FILE File, _In_ PLX_MSGHDR Header, _In_ BOOLEAN KernelIov, _In_ ULONG Flags);
    LONG64 (*Recvmsg)(_In_ PLX_FILE File, _Inout_ PLX_MSGHDR Header, _In_ BOOLEAN KernelIov, _In_ ULONG Flags);
    LONG64 (*Shutdown)(_In_ PLX_CALL Call, _In_ PLX_FILE File);
    LONG64 (*Setsockopt)(_In_ PLX_CALL Call, _In_ PLX_FILE File);
    LONG64 (*Getsockopt)(_In_ PLX_CALL Call, _In_ PLX_FILE File);
} LX_SOCKET_OPS, *PLX_SOCKET_OPS;

extern const LX_SOCKET_OPS LxInetSocketOps;
extern const LX_SOCKET_OPS LxNetlinkSocketOps;

LONG64 LxInetSocket(_In_ PLX_PROCESS Process, _In_ ULONG Family, _In_ ULONG Type, _In_ ULONG Protocol, _In_ ULONG Flags);
LONG64 LxNetlinkSocket(_In_ PLX_PROCESS Process, _In_ ULONG Type, _In_ ULONG Protocol, _In_ ULONG Flags);
VOID LxProcessUsage(_In_ PLX_PROCESS Process, _In_opt_ PEPROCESS EProcess, _Out_ PLX_USAGE Usage);
VOID LxProcessPublishName(_In_ PLX_PROCESS Process, _In_opt_ PEPROCESS EProcess);
VOID LxSchedQueryLoad(_Out_writes_(3) PULONG64 Load, _Out_ PULONG Runnable, _Out_ PULONG Threads);

typedef enum _LX_MQ_LIMIT
{
    LxMqMsgDefault,
    LxMqMsgMax,
    LxMqMsgSizeDefault,
    LxMqMsgSizeMax,
    LxMqQueuesMax,
    LxMqLimitCount
} LX_MQ_LIMIT;

typedef struct _LX_MQ_ATTR_LIMITS
{
    LONG64 MaxMsg;
    LONG64 MsgSize;
} LX_MQ_ATTR_LIMITS, *PLX_MQ_ATTR_LIMITS;

VOID LxMqInitialize(VOID);
VOID LxMqNamespaceCleanup(_In_ PLX_NS Ns);
LONG LxMqOpenNamed(_In_ PLX_PROCESS Process, _In_ PLX_NS IpcNs, _In_ PCSTR Name, _In_ ULONG Flags, _In_ ULONG Mode,
                   _In_opt_ PLX_MQ_ATTR_LIMITS Attr, _In_ PCSTR Path, _Out_ PLX_FILE *Result);
LONG LxMqUnlinkNamed(_In_ PLX_PROCESS Process, _In_ PLX_NS IpcNs, _In_ PCSTR Name);
BOOLEAN LxMqQuery(_In_ PLX_NS IpcNs, _In_ PCSTR Name, _Out_opt_ PLX_STAT Stat);
VOID LxMqList(_In_ PLX_NS IpcNs, _In_ VOID (*Add)(_In_ PVOID Context, _In_ PCSTR Name), _In_ PVOID Context);
ULONG LxMqGetLimit(_In_ PLX_PROCESS Process, _In_ LX_MQ_LIMIT Which);
LONG LxMqSetLimit(_In_ PLX_PROCESS Process, _In_ LX_MQ_LIMIT Which, _In_ ULONG Value);
LONG LxNetlinkDeliver(_In_ PLX_FILE File, _In_reads_bytes_(Length) PVOID Data, _In_ ULONG Length);
LONG LxFileSetOwner(_In_ PLX_FILE File, _In_ LONG Type, _In_ ULONG Owner);
VOID LxFileGetOwner(_In_ PLX_FILE File, _Out_ PLONG Type, _Out_ PULONG Owner);
LONG LxFileSetSignal(_In_ PLX_FILE File, _In_ LONG Signal);
LONG LxFileGetSignal(_In_ PLX_FILE File);
VOID LxFileSetAsync(_In_ PLX_FILE File, _In_ LONG Fd, _In_ BOOLEAN Enable);
NTSTATUS NTAPI MmCopyVirtualMemory(_In_ PEPROCESS SourceProcess, _In_ PVOID SourceAddress, _In_ PEPROCESS TargetProcess,
                                   _Out_ PVOID TargetAddress, _In_ SIZE_T BufferSize, _In_ KPROCESSOR_MODE PreviousMode,
                                   _Out_ PSIZE_T ReturnSize);

typedef struct _LX_UNIX_DIAG
{
    ULONG Type;
    ULONG State;
    ULONG64 Ino;
    ULONG PeerIno;
    ULONG Uid;
    ULONG RecvQueue;
    ULONG SendQueue;
    ULONG RecvBytes;
    ULONG RecvBuffer;
    ULONG SendBuffer;
    ULONG VfsIno;
    ULONG VfsDev;
    BOOLEAN HasVfs;
    UCHAR Shutdown;
    USHORT NameLength;
    UCHAR Name[108];
    ULONG IconCount;
    PULONG Icons;
} LX_UNIX_DIAG, *PLX_UNIX_DIAG;

LONG LxUnixDiagCollect(_In_ PLX_NS NetNs, _In_ ULONG64 Ino, _Out_ PLX_UNIX_DIAG *Entries, _Out_ PULONG Count);
VOID LxUnixDiagFree(_In_opt_ PLX_UNIX_DIAG Entries, _In_ ULONG Count);
BOOLEAN LxNetlinkIsFile(_In_ PLX_FILE File);
LONG64 LxNetDeviceIoctl(_In_ PLX_PROCESS Process, _In_ ULONG Request, _In_ ULONG64 Argument);
ULONG LxNetlinkAddressIndex(_In_ ULONG Address);
BOOLEAN LxInetIsFile(_In_ PLX_FILE File);
LONG64 LxInetBind(_In_ PLX_CALL Call, _In_ PLX_FILE File);
LONG64 LxInetListen(_In_ PLX_CALL Call, _In_ PLX_FILE File);
LONG64 LxInetConnect(_In_ PLX_CALL Call, _In_ PLX_FILE File);
LONG64 LxInetAccept(_In_ PLX_CALL Call, _In_ PLX_FILE File, _In_ ULONG Flags);
LONG64 LxInetName(_In_ PLX_CALL Call, _In_ PLX_FILE File, _In_ BOOLEAN Peer);
LONG64 LxInetSendmsg(_In_ PLX_FILE File, _In_ PLX_MSGHDR Header, _In_ BOOLEAN KernelIov, _In_ ULONG Flags);
LONG64 LxInetRecvmsg(_In_ PLX_FILE File, _Inout_ PLX_MSGHDR Header, _In_ BOOLEAN KernelIov, _In_ ULONG Flags);
LONG64 LxInetShutdown(_In_ PLX_CALL Call, _In_ PLX_FILE File);
LONG64 LxInetSetsockopt(_In_ PLX_CALL Call, _In_ PLX_FILE File);
LONG64 LxInetGetsockopt(_In_ PLX_CALL Call, _In_ PLX_FILE File);
VOID LxEpollFileReleased(_In_ PLX_FILE File);
VOID LxUnicodeDecodeLinuxName(_Inout_updates_(Count) PWCHAR Name, _In_ ULONG Count);

extern const LX_FILE_OPS LxNtFileOps;
extern const LX_FILE_OPS LxTtyFileOps;

PLX_INSTANCE LxInstanceCreate(_In_ PCUNICODE_STRING RootNt, _In_ BOOLEAN Boot);
PLX_INSTANCE LxInstanceFind(_In_ PCUNICODE_STRING RootNt);
VOID LxInstanceReference(_In_ PLX_INSTANCE Instance);
VOID LxInstanceDereference(_In_ PLX_INSTANCE Instance);
ULONG LxInstanceAllocatePid(_In_ PLX_INSTANCE Instance);
BOOLEAN LxInstancePidInUse(_In_ PLX_INSTANCE Instance, _In_ ULONG Pid);
PLX_MOUNT LxMountFind(_In_ PLX_NS Ns, _In_ PCSTR Path, _Out_opt_ PCSTR *Relative);
LONG LxMountAdd(_In_ PLX_PROCESS Process, _In_ PCSTR Target, _In_ PCSTR Source, _In_ PCSTR FsType, _In_ ULONG Flags, _In_opt_ PCSTR Data);
LONG LxMountUnmount(_In_ PLX_NS Ns, _In_ PLX_MOUNT Mount, _In_ ULONG Flags);
LONG LxMountSetPropagation(_In_ PLX_NS Ns, _In_ PLX_MOUNT Mount, _In_ ULONG Flags);
LONG LxMountCreateDetached(_In_ PLX_PROCESS Process, _In_ PCSTR FsType, _In_ PCSTR Source, _In_ ULONG Flags, _In_ ULONG Mode, _Out_ PLX_MOUNT *Mount);
LONG LxMountBindDetached(_In_ PLX_PROCESS Process, _In_ PLX_NODE Source, _Out_ PLX_MOUNT *Mount);
LONG LxMountBind(_In_ PLX_PROCESS Process, _In_ PLX_NODE Source, _In_ PCSTR Target, _In_ ULONG Flags);
LONG LxMountMove(_In_ PLX_NS Ns, _In_ PLX_MOUNT Mount, _In_ PCSTR Target, _In_ BOOLEAN Beneath);
LONG LxMountSetGroup(_In_ PLX_MOUNT From, _In_ PLX_MOUNT To);
VOID LxMountSetFlags(_In_ PLX_MOUNT Mount, _In_ ULONG Set, _In_ ULONG Clear);
PSTR LxMountInfo(_In_ PLX_PROCESS Process, _In_ BOOLEAN Full, _Out_ PULONG Length);
PSTR LxMountInfoNs(_In_ PLX_NS Ns, _In_ PCSTR Root, _In_ BOOLEAN Full, _Out_ PULONG Length);
LONG LxMountStatfs(_In_ PLX_MOUNT Mount, _Out_writes_bytes_(120) PVOID Statfs);
LONG LxMountNsCopy(_In_ PLX_NS New, _In_ PLX_NS Old);
VOID LxMountNsRelease(_In_ PLX_NS Ns);
LONG LxMountPivotRoot(_In_ PLX_PROCESS Process, _In_ PLX_NODE NewRoot, _In_ PLX_NODE PutOld);
PSTR LxPathRebase(_In_ PCSTR Path, _In_ PCSTR Root, _In_ PCSTR NewRoot, _In_ PCSTR PutOld);

PLX_NS LxNsCreate(_In_ PLX_INSTANCE Instance, _In_ LX_NS_KIND Kind, _In_opt_ PLX_NS From, _In_opt_ PLX_NS Owner);
VOID LxNsReference(_In_ PLX_NS Ns);
VOID LxNsDereference(_In_ PLX_NS Ns);
VOID LxNsInstanceShutdown(_In_ PLX_INSTANCE Instance);
PCSTR LxNsName(_In_ LX_NS_KIND Kind);
LONG64 LxTimeNsOffset(_In_opt_ PLX_PROCESS Process, _In_ ULONG Clock);
LONG LxQueryClockIn(_In_opt_ PLX_PROCESS Process, _In_ ULONG Clock, _Out_ PLX_TIMESPEC Timespec);
LONG LxNsInheritProcess(_Inout_ PLX_PROCESS Child, _In_ PLX_PROCESS Parent, _In_ ULONG64 Flags);
VOID LxNsAttachRoot(_Inout_ PLX_PROCESS Process, _In_ PLX_INSTANCE Instance);
VOID LxNsReleaseProcess(_Inout_ PLX_PROCESS Process);
PLX_NS LxNsFromFile(_In_ PLX_FILE File);
PLX_NS LxNsFromNode(_In_ PLX_PROCESS Process, _In_ PLX_NODE Node);
PLX_FILE LxNsFileOpen(_In_ PLX_NS Ns, _In_ ULONG Flags);
VOID LxNsRebaseProcesses(_In_ PLX_NS MntNs, _In_ PCSTR Root, _In_ PCSTR NewRoot, _In_ PCSTR PutOld);
PSTR LxProcessRoot(_In_ PLX_PROCESS Process);
PSTR LxProcessCwd(_In_ PLX_PROCESS Process);
LONG LxProcessSetCwd(_In_ PLX_PROCESS Process, _In_ PCSTR Path);
VOID LxNsInitialize(VOID);
VOID LxNsThreadInherit(_Inout_ PLX_THREAD Thread, _In_opt_ PLX_THREAD Parent);
VOID LxNsThreadRelease(_Inout_ PLX_THREAD Thread);
LONG LxNsShareFs(_Inout_ PLX_THREAD Thread, _In_ PLX_THREAD Parent);
LONG LxProcessChroot(_In_ PLX_PROCESS Process, _In_ PCSTR Path);
PSTR LxPathVisible(_In_ PLX_PROCESS Viewer, _In_ PCSTR Path);
PSTR LxFileCurrentPath(_In_ PLX_FILE File);
LONG LxPidAssign(_Inout_ PLX_PROCESS Process, _Inout_ PLX_THREAD Thread, _In_reads_opt_(SetTidCount) const ULONG *SetTid,
                 _In_ ULONG SetTidCount);
LONG LxTidAssign(_In_ PLX_PROCESS Process, _Inout_ PLX_THREAD Thread, _In_reads_opt_(SetTidCount) const ULONG *SetTid,
                 _In_ ULONG SetTidCount);
ULONG LxPidNsLastPid(_In_ PLX_PROCESS Process);
LONG LxPidNsSetLastPid(_In_ PLX_PROCESS Process, _In_ ULONG Value);
ULONG LxPidNr(_In_ PLX_PROCESS Viewer, _In_ PLX_PROCESS Target);
ULONG LxPidNrIn(_In_ PLX_NS PidNs, _In_ PLX_PROCESS Target);
ULONG LxTidNr(_In_ PLX_PROCESS Viewer, _In_ PLX_THREAD Thread);
ULONG LxGlobalToNr(_In_ PLX_PROCESS Viewer, _In_ ULONG Global);
ULONG LxGlobalToNrIn(_In_ PLX_NS PidNs, _In_ ULONG Global);
ULONG LxNrToGlobal(_In_ PLX_PROCESS Viewer, _In_ ULONG Nr);
ULONG LxNrToGlobalIn(_In_ PLX_NS PidNs, _In_ ULONG Nr);
BOOLEAN LxPidNsContains(_In_ PLX_NS Outer, _In_ PLX_NS Inner);
PLX_PROCESS LxProcessLookupNr(_In_ PLX_PROCESS Viewer, _In_ ULONG Nr);
PLX_THREAD LxThreadLookupNr(_In_ PLX_PROCESS Viewer, _In_ ULONG Nr, _Out_ PLX_PROCESS *Owner);
BOOLEAN LxPidNsIsInit(_In_ PLX_PROCESS Process);

LONG LxLookup(_In_ PLX_PROCESS Process, _In_ LONG DirFd, _In_ PCSTR Path, _In_ ULONG Flags, _Out_ PLX_NODE Node);
VOID LxNodeFree(_Inout_ PLX_NODE Node);
LONG LxPathNormalize(_In_ PCSTR Input, _Out_writes_(LX_PATH_MAX) PSTR Output);
LONG LxNtPath(_In_ PLX_MOUNT Mount, _In_ PCSTR Relative, _Out_ PUNICODE_STRING NtPath);
LONG LxNtOpenHandle(_In_ PCUNICODE_STRING NtPath, _In_ ACCESS_MASK Access, _In_ ULONG Disposition, _In_ ULONG Options,
                    _In_ ULONG Attributes, _Out_ PHANDLE Handle);
LONG LxVfsOpen(_In_ PLX_PROCESS Process, _In_ LONG DirFd, _In_ PCSTR Path, _In_ ULONG Flags, _In_ ULONG Mode, _Out_ PLX_FILE *File);
LONG LxVfsOpenResolve(_In_ PLX_PROCESS Process, _In_ LONG DirFd, _In_ PCSTR Path, _In_ ULONG Flags, _In_ ULONG Mode, _In_ ULONG Resolve, _Out_ PLX_FILE *File);
LONG LxVfsOpenNode(_In_ PLX_PROCESS Process, _Inout_ PLX_NODE Node, _In_ ULONG Flags, _In_ ULONG Mode, _Out_ PLX_FILE *File);
LONG LxVfsReopen(_In_ PLX_PROCESS Process, _In_ PLX_FILE Source, _In_ ULONG Flags, _Out_ PLX_FILE *File);
PSTR LxSynthFollowPath(_In_ PLX_PROCESS Process, _In_ PLX_MOUNT Mount, _In_ PCSTR Relative);
PSTR LxSynthFdPath(_In_ PLX_PROCESS Process, _In_ PLX_NODE Node);
LONG LxVfsStatNode(_In_ PLX_PROCESS Process, _In_ PLX_NODE Node, _Out_ PLX_STAT Stat);
LONG LxVfsMkdir(_In_ PLX_PROCESS Process, _In_ PLX_NODE Node, _In_ ULONG Mode);
LONG LxVfsMknod(_In_ PLX_PROCESS Process, _In_ PLX_NODE Node, _In_ ULONG Mode, _In_ ULONG64 Device);
LONG LxVfsSymlink(_In_ PLX_PROCESS Process, _In_ PLX_NODE Node, _In_ PCSTR Target);
LONG LxVfsUnlink(_In_ PLX_PROCESS Process, _In_ PLX_NODE Node, _In_ BOOLEAN Directory);
LONG LxVfsRename(_In_ PLX_PROCESS Process, _In_ PLX_NODE From, _In_ PLX_NODE To, _In_ ULONG Flags);
LONG LxVfsLink(_In_ PLX_PROCESS Process, _In_ PLX_NODE From, _In_ PLX_NODE To);
LONG LxApplyAttributes(_In_ PLX_PROCESS Process, _In_ ULONG Mode, _In_ ULONG Uid, _In_ ULONG Gid, _Inout_ PULONG OldMode,
                        _Inout_ PULONG OldUid, _Inout_ PULONG OldGid);
LONG LxVfsSetAttributes(_In_ PLX_PROCESS Process, _In_ PLX_NODE Node, _In_opt_ PLX_FILE File, _In_ ULONG Mode, _In_ ULONG Uid, _In_ ULONG Gid);
LONG LxVfsSetTimes(_In_opt_ PLX_NODE Node, _In_opt_ PLX_FILE File, _In_ PLX_TIMESPEC Times);
LONG LxVfsTruncate(_In_ PLX_NODE Node, _In_ LONG64 Length);
LONG LxVfsReadlink(_In_ PLX_PROCESS Process, _In_ PLX_NODE Node, _Out_ PSTR *Target);
LONG LxNtStatHandleType(_In_ HANDLE Handle, _Out_ PLX_STAT Stat);
LONG LxFifoOpen(_In_ PLX_PROCESS Process, _In_ PLX_NODE Node, _In_ ULONG Flags, _Out_ PLX_FILE *Result);
LONG LxPipeCreatePair(_In_opt_ PLX_PROCESS Process, _In_ ULONG Flags, _Out_ PLX_FILE *ReadEnd, _Out_ PLX_FILE *WriteEnd);
LONG LxPipeSetSize(_In_ PLX_FILE File, _In_ ULONG Size);
LONG LxPipeGetSize(_In_ PLX_FILE File);
BOOLEAN LxFileIsPipe(_In_ PLX_FILE File);
LONG LxPipeSetAttributes(_In_ PLX_PROCESS Process, _In_ PLX_FILE File, _In_ ULONG Mode, _In_ ULONG Uid, _In_ ULONG Gid);
LONG64 LxPipeTee(_In_ PLX_FILE In, _In_ PLX_FILE Out, _In_ ULONG64 Length, _In_ BOOLEAN NonBlocking);
PLX_FILE LxEpollTarget(_In_ PLX_FILE EpollFile, _In_ LONG Fd, _In_ ULONG Offset, _Out_ PLONG Error);
VOID LxPipeInitialize(VOID);
BOOLEAN LxFileIsSocket(_In_ PLX_FILE File);
VOID LxSocketInitialize(VOID);
VOID LxInotifyNotify(_In_ PLX_INSTANCE Instance, _In_ PCSTR Path, _In_ ULONG Mask, _In_ BOOLEAN Removed);
VOID LxInotifyEvent(_In_ PLX_PROCESS Process, _In_ PCSTR Path, _In_ ULONG Mask, _In_ BOOLEAN Removed);
VOID LxInotifyFileEvent(_In_ PLX_FILE File, _In_ ULONG Mask);
PLX_PROCESS LxPidfdProcess(_In_ PLX_FILE File);
PLX_FILE LxPidfdCreate(_In_ PLX_PROCESS Process, _In_ ULONG Flags);
VOID LxFdinfoExtra(_In_ PLX_FILE File, _In_opt_ PVOID Context);
VOID LxFdinfoAppend(_In_ PVOID Context, _In_ PCSTR Line);
VOID LxFdObjectsInitialize(VOID);
VOID LxVcacheInitialize(VOID);
VOID LxVcacheInvalidate(VOID);
VOID LxVcacheInvalidatePath(_In_opt_ PLX_NS Owner, _In_ PCSTR Path);
VOID LxVcachePurgeInstance(_In_ PLX_INSTANCE Instance);
VOID LxVcachePurgeOwner(_In_ PLX_NS Owner);
LONG LxNtCreateDirectory(_In_ PCUNICODE_STRING NtPath, _In_ ULONG Mode);
LONG LxNtDeleteTree(_In_ PCUNICODE_STRING NtPath);
LONG LxNtWriteLxAttributes(_In_ HANDLE Handle, _In_ ULONG Mode, _In_ ULONG Uid, _In_ ULONG Gid, _In_ ULONG64 Device, _In_ BOOLEAN WriteDevice);
LONG LxNtReadLxAttributes(_In_ HANDLE Handle, _Out_ PULONG Mode, _Out_ PULONG Uid, _Out_ PULONG Gid, _Out_ PULONG64 Device);
LONG LxXattrOpenNode(_In_ PLX_NODE Node, _In_ BOOLEAN Write, _Out_ PHANDLE Handle);
LONG LxXattrRead(_In_ HANDLE Handle, _Out_writes_bytes_to_(Capacity, *Length) PUCHAR Buffer, _In_ ULONG Capacity, _Out_ PULONG Length);
LONG LxXattrWrite(_In_ HANDLE Handle, _In_reads_bytes_(Length) PUCHAR Blob, _In_ ULONG Length);
LONG LxXattrRemoveName(_In_opt_ PLX_TMPFS_INODE Inode, _In_opt_ HANDLE Handle, _In_ PCSTR Name);
LONG LxFileReadXattr(_In_ PLX_FILE File, _In_ PCSTR Name, _Out_writes_bytes_to_(Capacity, *Length) PUCHAR Buffer, _In_ ULONG Capacity, _Out_ PULONG Length);
LONG LxXattrOpenFile(_In_ PLX_FILE File, _In_ BOOLEAN Write, _Out_ PHANDLE Handle, _Out_ PBOOLEAN Close);
LONG LxDeviceOpen(_In_ PLX_PROCESS Process, _In_ ULONG64 Device, _In_ ULONG Flags, _In_ PCSTR Path, _Out_ PLX_FILE *Result);
LONG LxDeviceStat(_In_ PLX_FILE File, _Out_ PLX_STAT Stat);
VOID LxKlogInitialize(VOID);
VOID LxKlogWrite(_In_ ULONG Priority, _In_reads_(Length) PCSTR Text, _In_ ULONG Length);
VOID LxKlogPrintf(_In_ ULONG Priority, _In_z_ _Printf_format_string_ PCSTR Format, ...);
BOOLEAN LxRatelimit(_Inout_ PLX_RATELIMIT Limit, _In_ LONG IntervalSeconds, _In_ LONG Burst, _Out_opt_ PLONG Missed);
LONG LxKmsgOpen(_In_ PLX_PROCESS Process, _In_ ULONG Flags, _Out_ PLX_FILE *Result);
VOID LxKlogGetPrintk(_Out_writes_(4) PLONG Values);
LONG LxKlogWritePrintk(_In_reads_bytes_(Length) PCSTR Data, _In_ ULONG Length);
LONG LxKlogDmesgRestrictValue(VOID);
VOID LxKlogSetDmesgRestrict(_In_ LONG Value);
PCSTR LxKlogDevkmsgName(VOID);
LONG LxKlogSetDevkmsg(_In_reads_bytes_(Length) PCSTR Data, _In_ ULONG Length);
extern LONG LxPrintkRatelimitSeconds;
extern LONG LxPrintkRatelimitBurst;

LONG LxSynthQuery(_In_ PLX_PROCESS Process, _In_ PLX_MOUNT Mount, _In_ PCSTR Relative, _Out_ PULONG Type, _Out_opt_ PSTR *LinkTarget);
LONG LxSynthOpen(_In_ PLX_PROCESS Process, _In_ PLX_NODE Node, _In_ ULONG Flags, _Out_ PLX_FILE *File);
LONG LxSynthStat(_In_ PLX_PROCESS Process, _In_ PLX_NODE Node, _Out_ PLX_STAT Stat);
LONG LxSynthMkdir(_In_ PLX_PROCESS Process, _In_ PLX_NODE Node);
LONG LxCgroupSetAttributes(_In_ PLX_PROCESS Process, _In_ PLX_NODE Node, _In_ ULONG Mode, _In_ ULONG Uid, _In_ ULONG Gid);
PLX_TTY LxPtsNodeTty(_In_ PLX_PROCESS Process, _In_ PLX_NODE Node);
VOID LxTmpfsInitialize(VOID);
PLX_TMPFS LxTmpfsCreate(_In_ ULONG Mode);
LONG LxTmpfsCreateSquashfs(_In_ PLX_SQUASHFS Squash, _Out_ PLX_TMPFS *Result);
LONG LxTmpfsParseOptions(_In_ PLX_PROCESS Process, _In_opt_ PCSTR Data, _Inout_ PLX_TMPFS_OPTIONS Options);
VOID LxTmpfsApplyOptions(_In_ PLX_TMPFS Fs, _In_ PLX_TMPFS_OPTIONS Options);
VOID LxTmpfsReference(_In_ PLX_TMPFS Fs);
VOID LxTmpfsDereference(_In_ PLX_TMPFS Fs);
VOID LxTmpfsMountReference(_In_ PLX_TMPFS Fs);
VOID LxTmpfsMountDereference(_In_ PLX_TMPFS Fs);
VOID LxTmpfsPurge(_In_ PLX_TMPFS Fs);
ULONG64 LxTmpfsShmemBytes(VOID);
VOID LxTmpfsStatfs(_In_ PLX_TMPFS Fs, _Out_ PULONG64 Blocks, _Out_ PULONG64 Free, _Out_ PULONG64 Files, _Out_ PULONG64 FreeFiles);
LONG LxTmpfsQuery(_In_ PLX_MOUNT Mount, _In_ PCSTR Relative, _Out_ PULONG Type, _Out_opt_ PSTR *LinkTarget);
LONG LxTmpfsLookup(_In_ PLX_MOUNT Mount, _In_ PCSTR Relative, _Out_ PLX_TMPFS_INODE *Inode);
LONG LxTmpfsCreateNode(_In_ PLX_MOUNT Mount, _In_ PCSTR Relative, _In_ ULONG Mode, _In_ ULONG Uid, _In_ ULONG Gid,
                       _In_ ULONG64 Device, _In_opt_ PCSTR LinkTarget, _Out_opt_ PLX_TMPFS_INODE *Inode);
LONG LxTmpfsUnlink(_In_ PLX_MOUNT Mount, _In_ PCSTR Relative, _In_ BOOLEAN Directory);
LONG LxTmpfsRename(_In_ PLX_MOUNT FromMount, _In_ PCSTR FromRelative, _In_ PLX_MOUNT ToMount, _In_ PCSTR ToRelative,
                   _In_ ULONG Flags);
LONG LxTmpfsLink(_In_ PLX_MOUNT FromMount, _In_ PCSTR FromRelative, _In_ PLX_MOUNT ToMount, _In_ PCSTR ToRelative);
VOID LxTmpfsInodeReference(_In_ PLX_TMPFS_INODE Inode);
VOID LxTmpfsInodeDereference(_In_ PLX_TMPFS_INODE Inode);
VOID LxTmpfsInodeStat(_In_ PLX_TMPFS_INODE Inode, _In_opt_ PLX_MOUNT Mount, _Out_ PLX_STAT Stat);
VOID LxTmpfsInodeAttributes(_In_ PLX_TMPFS_INODE Inode, _Out_ PULONG Mode, _Out_ PULONG Uid, _Out_ PULONG Gid,
                            _Out_ PULONG64 Device);
VOID LxTmpfsInodeSetAttributes(_In_ PLX_TMPFS_INODE Inode, _In_ ULONG Mode, _In_ ULONG Uid, _In_ ULONG Gid);
VOID LxTmpfsInodeSetTimes(_In_ PLX_TMPFS_INODE Inode, _In_reads_(2) PLX_TIMESPEC Times);
PSTR LxTmpfsInodeLinkTarget(_In_ PLX_TMPFS_INODE Inode);
PSTR LxTmpfsInodePath(_In_ PLX_TMPFS_INODE Inode, _In_ PLX_MOUNT Mount);
ULONG64 LxTmpfsInodeId(_In_ PLX_TMPFS_INODE Inode);
typedef VOID (*PLX_TMPFS_PRIVATE_RELEASE)(_In_ PVOID Object);
LONG LxTmpfsSetPrivate(_In_ PLX_TMPFS_INODE Inode, _In_ PVOID Object, _In_ PLX_TMPFS_PRIVATE_RELEASE Release);
PVOID LxTmpfsGetPrivate(_In_ PLX_TMPFS_INODE Inode, _In_ PLX_TMPFS_PRIVATE_RELEASE Release);
ULONG64 LxTmpfsInodeSize(_In_ PLX_TMPFS_INODE Inode);
LONG LxTmpfsInodeTruncate(_In_ PLX_TMPFS_INODE Inode, _In_ LONG64 Size);
LONG LxTmpfsInodeZeroRange(_In_ PLX_TMPFS_INODE Inode, _In_ ULONG64 Start, _In_ ULONG64 End);
LONG LxTmpfsInodeRemoveRange(_In_ PLX_TMPFS_INODE Inode, _In_ ULONG64 Start, _In_ ULONG64 End);
LONG LxTmpfsXattrRead(_In_ PLX_TMPFS_INODE Inode, _Out_writes_bytes_to_(Capacity, *Length) PUCHAR Buffer,
                      _In_ ULONG Capacity, _Out_ PULONG Length);
LONG LxTmpfsXattrWrite(_In_ PLX_TMPFS_INODE Inode, _In_reads_bytes_(Length) PUCHAR Blob, _In_ ULONG Length);
LONG LxTmpfsOpenFile(_In_ PLX_TMPFS_INODE Inode, _In_ ULONG Flags, _Out_ PLX_FILE *File);
LONG LxTmpfsCreateAnonymous(_In_ ULONG Mode, _In_ ULONG Uid, _In_ ULONG Gid, _Out_ PLX_FILE *File);
PLX_TMPFS_INODE LxTmpfsFileInode(_In_opt_ PLX_FILE File);
NTSTATUS LxTmpfsMapSection(_In_ PLX_TMPFS_INODE Inode, _In_ BOOLEAN Shared, _Out_ PHANDLE Section, _Out_opt_ PULONG64 Size);
LONG64 LxTmpfsReadKernel(_In_ PLX_TMPFS_INODE Inode, _Out_writes_bytes_(Length) PVOID Buffer, _In_ ULONG Length,
                         _In_ ULONG64 Offset);
LONG LxSynthRmdir(_In_ PLX_PROCESS Process, _In_ PLX_NODE Node);
LONG LxSynthReadlink(_In_ PLX_PROCESS Process, _In_ PLX_NODE Node, _Out_ PSTR *Target);
VOID LxSynthInitialize(VOID);

#define LX_USB_DEVICE_MAJOR 189

typedef VOID (*PLX_SYSDEV_LIST_ROUTINE)(_In_ PVOID Context, _In_ UCHAR Type, _In_ PCSTR Name);

LONG LxSysDevQuery(_In_ PCSTR Path, _Out_ PULONG Type, _Out_ PULONG Mode, _Out_opt_ PSTR *LinkTarget);
VOID LxSysDevList(_In_ PCSTR Path, _In_ PLX_SYSDEV_LIST_ROUTINE Routine, _In_ PVOID Context);
LONG LxSysDevRead(_In_ PLX_PROCESS Process, _In_ PCSTR Path, _Out_ PVOID *Data, _Out_ PULONG Length);
ULONG LxSysDevUsbNodes(_Out_writes_(Count) PULONG Nodes, _In_ ULONG Count);
VOID LxSysDevInitialize(VOID);

#define LX_LOOP_MAJOR           7
#define LX_LOOP_CONTROL_MINOR   237
#define LX_LOOP_PRESET_COUNT    8

typedef VOID (*PLX_LOOP_ENUM_ROUTINE)(_In_ PVOID Context, _In_ ULONG Number, _In_ ULONG64 Capacity);

typedef struct _LX_LOOP_STATE
{
    BOOLEAN Bound;
    ULONG Flags;
    ULONG BlockSize;
    ULONG64 Offset;
    ULONG64 SizeLimit;
    ULONG64 Capacity;
    PSTR BackingPath;
} LX_LOOP_STATE, *PLX_LOOP_STATE;

PLX_FILE LxLoopOpen(_In_ PLX_PROCESS Process, _In_ ULONG Minor, _In_ ULONG Flags, _In_ PCSTR Path);
PLX_FILE LxLoopControlOpen(_In_ PLX_PROCESS Process, _In_ ULONG Flags, _In_ PCSTR Path);
BOOLEAN LxLoopIsDevice(_In_ PLX_FILE File);
LONG64 LxLoopReadKernel(_In_ PLX_FILE Device, _Out_writes_bytes_(Length) PVOID Buffer, _In_ ULONG Length, _In_ ULONG64 Offset);
ULONG64 LxLoopCapacity(_In_ PLX_FILE Device);
VOID LxLoopEnumerate(_In_ PLX_INSTANCE Instance, _In_ PLX_LOOP_ENUM_ROUTINE Routine, _In_ PVOID Context);
VOID LxLoopFreeAll(_In_ PLX_INSTANCE Instance);
BOOLEAN LxLoopQuery(_In_ PLX_INSTANCE Instance, _In_ ULONG Number, _Out_ PLX_LOOP_STATE State);
VOID LxLoopList(_In_ PLX_INSTANCE Instance, _In_ PLX_LOOP_ENUM_ROUTINE Routine, _In_ PVOID Context);

typedef struct _LX_SQUASHFS_INODE
{
    ULONG Mode;
    ULONG Uid;
    ULONG Gid;
    ULONG Nlink;
    ULONG Mtime;
    ULONG Number;
    ULONG64 Device;
    ULONG64 Size;
    ULONG DirBlock;
    ULONG DirOffset;
    ULONG64 DataStart;
    ULONG Fragment;
    ULONG FragmentOffset;
    ULONG64 ListBlock;
    ULONG ListOffset;
    ULONG Xattr;
} LX_SQUASHFS_INODE, *PLX_SQUASHFS_INODE;

typedef LONG (*PLX_SQUASHFS_DIR_ROUTINE)(_In_ PVOID Context, _In_reads_(Length) PCSTR Name, _In_ ULONG Length,
                                         _In_ ULONG64 Inode);

LONG LxSquashfsOpen(_In_ PLX_PROCESS Process, _In_ PCSTR Source, _Out_ PLX_SQUASHFS *Result);
VOID LxSquashfsClose(_In_opt_ PLX_SQUASHFS Squash);
VOID LxSquashfsAddUser(_In_ PLX_SQUASHFS Squash);
VOID LxSquashfsRemoveUser(_In_ PLX_SQUASHFS Squash);
ULONG64 LxSquashfsRoot(_In_ PLX_SQUASHFS Squash);
LONG LxSquashfsReadInode(_In_ PLX_SQUASHFS Squash, _In_ ULONG64 Inode, _Out_ PLX_SQUASHFS_INODE Info);
LONG LxSquashfsReadLink(_In_ PLX_SQUASHFS Squash, _In_ PLX_SQUASHFS_INODE Info, _Out_ PSTR *Target);
LONG LxSquashfsReadXattrs(_In_ PLX_SQUASHFS Squash, _In_ PLX_SQUASHFS_INODE Info,
                          _Outptr_result_bytebuffer_maybenull_(*Length) PUCHAR *Result, _Out_ PULONG Length);
LONG LxSquashfsListDirectory(_In_ PLX_SQUASHFS Squash, _In_ ULONG64 Inode, _In_ PLX_SQUASHFS_DIR_ROUTINE Routine,
                             _In_ PVOID Context);
LONG LxSquashfsReadFile(_In_ PLX_SQUASHFS Squash, _In_ ULONG64 Inode, _Out_writes_bytes_(Capacity) PUCHAR View,
                        _In_ ULONG64 Capacity);
VOID LxSquashfsStatfs(_In_ PLX_SQUASHFS Squash, _Out_ PULONG64 Bytes, _Out_ PULONG64 Inodes);
PLX_FILE LxUsbfsOpen(_In_ ULONG Minor, _In_ ULONG Flags, _In_ PCSTR Path);
PLX_CGROUP LxCgroupFromFile(_In_ PLX_FILE File);
PLX_CGROUP LxCgroupCreateRoot(VOID);
VOID LxCgroupAttach(_In_ PLX_PROCESS Process, _In_ PLX_CGROUP Cgroup);
PSTR LxCgroupPath(_In_ PLX_CGROUP Cgroup);
VOID LxCgroupFreeTree(_In_ PLX_CGROUP Cgroup);

NTSTATUS LxTtyCreate(_In_ PLX_INSTANCE Instance, _In_opt_ PLX_DEVPTS Devpts, _In_ ULONG Flags, _In_ ULONG Uid, _In_ ULONG Gid, _In_ USHORT Rows, _In_ USHORT Columns, _Out_ PLX_TTY *Tty);
LONG LxDevptsCreate(_In_opt_ PLX_PROCESS Process, _In_ PLX_INSTANCE Instance, _In_opt_ PCSTR Data, _Out_ PLX_DEVPTS *Devpts);
VOID LxDevptsFreeAll(_In_ PLX_INSTANCE Instance);
LONG LxDevptsForPtmx(_In_ PLX_PROCESS Process, _In_ PCSTR Path, _Out_ PLX_DEVPTS *Devpts);
VOID LxTtyReference(_In_ PLX_TTY Tty);
VOID LxTtyDereference(_In_ PLX_TTY Tty);
LONG64 LxTtyRead(_In_ PLX_TTY Tty, _In_ ULONG64 Buffer, _In_ ULONG64 Length, _In_ BOOLEAN NonBlocking);
LONG64 LxTtyWrite(_In_ PLX_TTY Tty, _In_ ULONG64 Buffer, _In_ ULONG64 Length, _In_ BOOLEAN NonBlocking);
VOID LxTtyKernelWrite(_In_ PLX_TTY Tty, _In_reads_(Length) PCSTR Text, _In_ ULONG Length);
LONG64 LxTtyIoctl(_In_ PLX_PROCESS Process, _In_ PLX_TTY Tty, _In_ ULONG Request, _In_ ULONG64 Argument);
BOOLEAN LxTtyReadable(_In_ PLX_TTY Tty);
PLX_FILE LxTtyOpen(_In_ PLX_TTY Tty, _In_ ULONG Flags);
PLX_TTY LxFileGetTty(_In_ PLX_FILE File);
VOID LxTtyGetAttributes(_In_ PLX_TTY Tty, _Out_ PULONG Mode, _Out_ PULONG Uid, _Out_ PULONG Gid);
VOID LxTtySetAttributes(_In_ PLX_TTY Tty, _In_ ULONG Mode, _In_ ULONG Uid, _In_ ULONG Gid);
LONG64 LxTtyMasterRead(_In_ PLX_TTY Tty, _Out_writes_bytes_(Length) PVOID Buffer, _In_ ULONG Length, _In_ BOOLEAN NonBlocking);
LONG64 LxTtyMasterWrite(_In_ PLX_TTY Tty, _In_reads_bytes_(Length) const VOID *Buffer, _In_ ULONG Length, _In_ BOOLEAN NonBlocking);
VOID LxTtyMasterClose(_In_ PLX_TTY Tty);
VOID LxTtySetWinSize(_In_ PLX_TTY Tty, _In_ USHORT Rows, _In_ USHORT Columns);
PLX_TTY LxTtyLookup(_In_ PLX_INSTANCE Instance, _In_ PLX_DEVPTS Devpts, _In_ ULONG Index);
LONG LxTtyOpenSlave(_In_ PLX_PROCESS Process, _In_ PLX_TTY Tty, _In_ ULONG Flags, _Out_ PLX_FILE *File);
LONG LxPtmxOpen(_In_ PLX_PROCESS Process, _In_ PLX_DEVPTS Devpts, _In_ ULONG Flags, _Out_ PLX_FILE *File);
ULONG64 LxTtyDevice(_In_ PLX_TTY Tty);
ULONG LxTtyList(_In_ PLX_INSTANCE Instance, _In_ PLX_DEVPTS Devpts, _Out_writes_(Count) PULONG Indexes, _In_ ULONG Count);
BOOLEAN LxTtyAttachControlling(_In_ PLX_PROCESS Process, _In_ PLX_TTY Tty);
VOID LxTtyCheckSession(_In_ PLX_PROCESS Process);
PLX_TTY LxProcessGetTty(_In_ PLX_PROCESS Process);
VOID LxTtyDisassociate(_In_ PLX_PROCESS Process, _In_ BOOLEAN Exiting);
ULONG LxSignalGroup(_In_ PLX_INSTANCE Instance, _In_opt_ PLX_NS PidNs, _In_ ULONG Pgid, _In_ LONG Signal);
ULONG LxSignalGroupInfo(_In_ PLX_INSTANCE Instance, _In_opt_ PLX_NS PidNs, _In_ ULONG Pgid, _In_ LONG Signal,
                        _In_opt_ PLX_SIGINFO Info, _In_opt_ PLX_PROCESS Sender, _Out_opt_ PULONG Denied);
LONG LxSignalPermitted(_In_ PLX_PROCESS Sender, _In_ PLX_PROCESS Target, _In_ LONG Signal);
BOOLEAN LxSignalInfoHasUid(_In_ LONG Signal, _In_ PLX_SIGINFO Info);

BOOLEAN LxCapable(_In_ PLX_PROCESS Process, _In_ ULONG Capability);
BOOLEAN LxNsCapable(_In_ PLX_PROCESS Process, _In_opt_ PLX_NS UserNs, _In_ ULONG Capability);
BOOLEAN LxCapableWrtIds(_In_ PLX_PROCESS Process, _In_ ULONG Uid, _In_ ULONG Gid, _In_ ULONG Capability);
BOOLEAN LxMayMount(_In_ PLX_PROCESS Process);
LONG LxPermissionAs(_In_ PLX_PROCESS Process, _In_ ULONG Fsuid, _In_ ULONG Fsgid, _In_ ULONG64 Effective, _In_ ULONG Mode,
                    _In_ ULONG Uid, _In_ ULONG Gid, _In_ ULONG Mask);
LONG LxPermission(_In_ PLX_PROCESS Process, _In_ ULONG Mode, _In_ ULONG Uid, _In_ ULONG Gid, _In_ ULONG Mask);
BOOLEAN LxOwnerOrCapable(_In_ PLX_PROCESS Process, _In_ ULONG Uid, _In_ ULONG Gid);
BOOLEAN LxIdsMapped(_In_ PLX_PROCESS Process, _In_ ULONG Uid, _In_ ULONG Gid);
LONG LxVfsNodePermission(_In_ PLX_PROCESS Process, _In_ PLX_NODE Node, _In_ ULONG Mask);
LONG LxVfsParentPermission(_In_ PLX_PROCESS Process, _In_ PCSTR Path, _In_ ULONG Mask, _Out_opt_ PLX_STAT Parent);
LONG LxVfsMayDelete(_In_ PLX_PROCESS Process, _In_ PLX_NODE Node);
PLX_NS LxCredUserNs(_In_ PLX_PROCESS Process);
ULONG LxIdToKernel(_In_opt_ PLX_NS UserNs, _In_ BOOLEAN Group, _In_ ULONG Id);
ULONG LxIdRangeToKernel(_In_ PLX_NS UserNs, _In_ BOOLEAN Group, _In_ ULONG Id, _In_ ULONG Count);
ULONG LxIdFromKernel(_In_opt_ PLX_NS UserNs, _In_ BOOLEAN Group, _In_ ULONG Id);
ULONG LxIdFromKernelMunged(_In_opt_ PLX_NS UserNs, _In_ BOOLEAN Group, _In_ ULONG Id);
ULONG LxCredViewId(_In_ PLX_PROCESS Viewer, _In_ BOOLEAN Group, _In_ ULONG Id);
ULONG LxCredViewIdFor(_In_ PLX_PROCESS Viewer, _In_ BOOLEAN Group, _In_ ULONG Id);
ULONG LxCredOverflowId(_In_ BOOLEAN Group);
LONG LxCredSetOverflowId(_In_ PLX_PROCESS Process, _In_ BOOLEAN Group, _In_ LONG64 Value);
PLX_NS LxNsOwner(_In_ PLX_NS Ns);
LONG LxUserNsCreate(_In_ PLX_PROCESS Process, _Out_ PLX_NS *Result);
VOID LxUserNsEnter(_Inout_ PLX_PROCESS Process, _In_ PLX_NS UserNs);
PLX_NS LxUserNsOf(_In_ PLX_PROCESS Process);
PLX_ID_MAP LxUserNsMap(_In_ PLX_NS UserNs, _In_ BOOLEAN Group);
LONG LxUserNsWriteMap(_In_ PLX_PROCESS Writer, _In_ PLX_NS UserNs, _In_ BOOLEAN Group,
                      _In_reads_bytes_(Length) PCSTR Data, _In_ ULONG Length);
LONG LxUserNsWriteSetgroups(_In_ PLX_PROCESS Writer, _In_ PLX_NS UserNs, _In_reads_bytes_(Length) PCSTR Data,
                            _In_ ULONG Length);
BOOLEAN LxUserNsMaySetgroups(_In_ PLX_NS UserNs);
LONG LxUserNsGetMax(_In_ PLX_PROCESS Process);
LONG LxUserNsSetMax(_In_ PLX_PROCESS Process, _In_ LONG64 Value);
BOOLEAN LxInGroup(_In_ PLX_PROCESS Process, _In_ ULONG Gid);
ULONG LxCredMakeId(_In_ PLX_PROCESS Process, _In_ BOOLEAN Group, _In_ ULONG Id);
PLX_CRED LxProcessCred(_In_ PLX_PROCESS Process);
VOID LxCredSave(_In_ PLX_PROCESS Process, _Out_ PLX_CRED_SAVE Save);
VOID LxCredFixSetuid(_Inout_ PLX_PROCESS Process, _In_ PLX_CRED_SAVE Old, _In_ BOOLEAN FileSystem);
VOID LxCredCommit(_Inout_ PLX_PROCESS Process, _Inout_ PLX_THREAD Thread, _In_ PLX_CRED_SAVE Old);
ULONG LxCredExec(_Inout_ PLX_PROCESS Process, _Inout_ PLX_THREAD Thread, _In_ PLX_CRED_SAVE Old, _In_ PLX_FILE_CAPS FileCaps);
BOOLEAN LxCredRootOwnsNs(_In_ PLX_PROCESS Process, _In_ ULONG RootId);
LONG LxCredSetCapabilities(_Inout_ PLX_PROCESS Process, _In_ ULONG64 Effective, _In_ ULONG64 Permitted,
                           _In_ ULONG64 Inheritable);
LONG LxCredSetSecureBits(_Inout_ PLX_PROCESS Process, _In_ ULONG64 Bits);
BOOLEAN LxPtraceMayRead(_In_ PLX_PROCESS Caller, _In_ PLX_PROCESS Target);

PLX_PROCESS LxProcessCreate(_In_ PLX_INSTANCE Instance);
PLX_PROCESS LxProcessFork(_In_ PLX_PROCESS Parent);
VOID LxProcessReference(_In_ PLX_PROCESS Process);
VOID LxProcessDereference(_In_ PLX_PROCESS Process);
PLX_THREAD LxThreadCreate(_In_ PLX_PROCESS Process);
LONG LxThreadCreateLimited(_In_ PLX_PROCESS Process, _Out_ PLX_THREAD *Result);
LONG LxCgroupTaskAdd(_In_ PLX_PROCESS Process, _In_ BOOLEAN Limited);
VOID LxCgroupTaskRemove(_In_ PLX_PROCESS Process);
VOID LxThreadFree(_In_ PLX_THREAD Thread);
PLX_PROCESS LxProcessLookup(_In_ PLX_INSTANCE Instance, _In_ ULONG Pid);
PLX_THREAD LxThreadLookupReferenced(_In_ PLX_INSTANCE Instance, _In_ ULONG Tid, _Out_ PLX_PROCESS *Process);
BOOLEAN LxProcessExitStatus(_In_ PLX_PROCESS Process, _In_ LONG WaitStatus);
VOID LxProcessExitGroup(_In_ PLX_PROCESS Process, _In_ LONG WaitStatus);
VOID LxProcessZapThreads(_In_ PLX_PROCESS Process, _In_opt_ PLX_THREAD Except);
LONG LxMmRandomizeVaSpace(VOID);
VOID LxMmSetRandomizeVaSpace(_In_ LONG Value);
ULONG LxMmRndBits(VOID);
LONG LxMmSetRndBits(_In_ ULONG Bits);
BOOLEAN LxMmPickLayout(_Inout_ PLX_PROCESS Process);
ULONG64 LxMmRandomOffset(VOID);
ULONG64 LxMmRandomizeBrk(_In_ ULONG64 Base);
VOID LxProcessExited(_In_ PLX_PROCESS Process, _In_ NTSTATUS ExitStatus);
VOID LxProcessThreadExited(_In_ PLX_PROCESS Process, _In_ ULONG Tid);
VOID LxItimerRelease(_In_ PLX_PROCESS Process);
VOID LxPosixTimersRelease(_In_ PLX_PROCESS Process, _In_ BOOLEAN Free);
VOID LxProcessRegister(_In_ PLX_PROCESS Process);
VOID LxProcessUnregister(_In_ PLX_PROCESS Process);
VOID LxProcessCloseFiles(_In_ PLX_PROCESS Process);
VOID LxProcessSetCommand(_In_ PLX_PROCESS Process, _In_ PCSTR Args, _In_ ULONG ArgLength, _In_ PCSTR Env, _In_ ULONG EnvLength, _In_ PCSTR ExePath, _In_ PCSTR CommPath);

LONG64 LxMmMap(_In_ PLX_PROCESS Process, _In_ ULONG64 Address, _In_ ULONG64 Length, _In_ ULONG Prot, _In_ ULONG Flags, _In_opt_ PLX_FILE File, _In_ ULONG64 Offset);
LONG64 LxMmUnmap(_In_ PLX_PROCESS Process, _In_ ULONG64 Address, _In_ ULONG64 Length);
LONG64 LxMmProtect(_In_ PLX_PROCESS Process, _In_ ULONG64 Address, _In_ ULONG64 Length, _In_ ULONG Prot);
VOID LxMmReset(_In_ PLX_PROCESS Process);
VOID LxMmLock(_In_ PLX_PROCESS Process);
BOOLEAN LxMmHasVmaAbove(_In_ PLX_PROCESS Process, _In_ ULONG64 Address);
ULONG64 LxMmLockedBytes(_In_ PLX_PROCESS Process);
VOID LxSharedVmTerminate(_In_ PLX_PROCESS Process, _In_ NTSTATUS ExitCode, _In_ BOOLEAN CurrentNtProcessOnly);
VOID LxSharedVmExited(_In_ PLX_PROCESS Process);
ULONG LxMmTruncateViews(_In_ HANDLE Handle);
ULONG LxMmReleaseKeyedViews(_In_ PVOID Key, _In_opt_ VOID (*Visit)(_In_ PVOID Context, _In_ PLX_PROCESS Process),
                            _In_opt_ PVOID Context);
VOID LxMmForkChild(_In_ PLX_PROCESS Parent, _In_ PLX_PROCESS Child, _In_ HANDLE ProcessHandle);
LONG64 LxMmMincore(_In_ PLX_PROCESS Process, _In_ ULONG64 Address, _In_ ULONG64 End, _In_ ULONG64 Output);
ULONG64 LxMmVmaResident(_In_ HANDLE ProcessHandle, _In_ PLX_VMA Vma);
VOID LxMmPagemap(_In_ PLX_PROCESS Process, _In_ HANDLE ProcessHandle, _In_ ULONG64 Address, _In_ ULONG Pages,
                 _In_ BOOLEAN Frames, _Out_writes_(Pages) PULONG64 Entries);
BOOLEAN LxMmMapsPath(_In_ PLX_PROCESS Process, _In_opt_ PCSTR Path);
VOID LxMmSetBrk(_In_ PLX_PROCESS Process, _In_ ULONG64 StartBrk, _In_ ULONG64 Brk);
PLX_FILE LxMmFileAt(_In_ PLX_PROCESS Process, _In_ ULONG64 Start, _In_ ULONG64 End);
ULONG LxMmFileRanges(_In_ PLX_PROCESS Process, _Out_writes_(Max * 2) PULONG64 Ranges, _In_ ULONG Max);
VOID LxMmUnlock(_In_ PLX_PROCESS Process);
LONG LxMmFork(_In_ PLX_PROCESS Child, _In_ PLX_PROCESS Parent);
VOID LxMmRelease(_In_ PLX_PROCESS Process);
BOOLEAN LxMmHandleFault(_In_ PLX_PROCESS Process, _In_ ULONG64 Address, _In_ ULONG Access);
BOOLEAN LxMmAddressMapped(_In_ PLX_PROCESS Process, _In_ ULONG64 Address);
BOOLEAN LxMmFaultIn(_In_ ULONG64 Address, _In_ SIZE_T Length, _In_ BOOLEAN Write);
LONG LxMmZero(_In_ PLX_PROCESS Process, _In_ ULONG64 Address, _In_ ULONG64 End);
ULONG LxProtToNt(_In_ ULONG Prot);

NTSTATUS LxExecImage(_In_ PLX_THREAD Thread, _In_ PLX_EXEC_REQUEST Request, _Inout_ PKTRAP_FRAME TrapFrame, _Inout_opt_ PKEXCEPTION_FRAME ExceptionFrame, _Out_ PLONG Errno);

LONG64 LxFutexWake(_In_ ULONG64 Address, _In_ LONG Count);
VOID LxFutexInitialize(VOID);
VOID LxFutexExitRobustList(_In_ PLX_THREAD Thread);
BOOLEAN LxMmSharedKey(_In_ PLX_PROCESS Process, _In_ ULONG64 Address, _Out_ PVOID *Object, _Out_ PULONG64 Offset);
LONG64 LxMmMapSection(_In_ PLX_PROCESS Process, _In_ PVOID Section, _In_ ULONG64 Address, _In_ ULONG64 Length, _In_ ULONG Prot, _In_ BOOLEAN Remap);
LONG64 LxMmMapHugeSection(_In_ PLX_PROCESS Process, _In_ PLX_FILE File, _In_ ULONG64 Address, _In_ ULONG64 Length, _In_ ULONG Prot, _In_ BOOLEAN Remap);
LONG LxMmDetachSection(_In_ PLX_PROCESS Process, _In_ ULONG64 Address);
LONG LxMmPrepareRemote(_In_ PLX_PROCESS Process, _In_ PEPROCESS EProcess, _In_ ULONG64 Address, _In_ ULONG64 Length, _In_ BOOLEAN Write);
LONG LxMmForceAccess(_In_ PLX_PROCESS Process, _In_ PEPROCESS EProcess, _In_ ULONG64 Address,
                     _Inout_updates_bytes_(Length) PVOID Buffer, _In_ ULONG Length, _In_ BOOLEAN Write);
VOID LxIpcShmVmaOpen(_In_ PVOID Section);
VOID LxIpcShmVmaClose(_In_ PVOID Section);
VOID LxIpcProcessExit(_In_ PLX_PROCESS Process);
VOID LxIpcNamespaceCleanup(_In_ PLX_NS Ns);
PVOID LxIpcSemUndoList(_In_ PLX_PROCESS Process);
VOID LxIpcInitialize(VOID);
VOID LxWinInitialize(VOID);
PLX_FILE LxWinOpen(_In_ PLX_PROCESS Process, _In_ ULONG Flags, _In_ PCSTR Path);
NTSTATUS LxWinHostCreate(_Inout_ PFILE_OBJECT FileObject);
VOID LxWinHostCleanup(_Inout_ PFILE_OBJECT FileObject);
VOID LxWinHostClose(_Inout_ PFILE_OBJECT FileObject);
BOOLEAN LxWinHostIoctl(_In_ PFILE_OBJECT FileObject, _Inout_ PIRP Irp, _In_ PIO_STACK_LOCATION Stack, _Out_ PNTSTATUS Status);
#define LX_DRM_MAJOR 226
VOID LxDrmInitialize(VOID);
PLX_FILE LxDrmOpen(_In_ PLX_PROCESS Process, _In_ ULONG Minor, _In_ ULONG Flags, _In_ PCSTR Path);
BOOLEAN LxDrmIsFile(_In_ PLX_FILE File);
BOOLEAN LxDrmPresent(VOID);
LONG64 LxDrmMmap(_In_ PLX_PROCESS Process, _In_ PLX_FILE File, _In_ ULONG64 Address, _In_ ULONG64 Length, _In_ ULONG Prot,
                 _In_ ULONG Flags, _In_ ULONG64 Offset);
NTSTATUS LxGpuHostCreate(_Inout_ PFILE_OBJECT FileObject);
VOID LxGpuHostCleanup(_Inout_ PFILE_OBJECT FileObject);
VOID LxGpuHostClose(_Inout_ PFILE_OBJECT FileObject);
BOOLEAN LxGpuHostIoctl(_In_ PFILE_OBJECT FileObject, _Inout_ PIRP Irp, _In_ PIO_STACK_LOCATION Stack, _Out_ PNTSTATUS Status);
NTSTATUS LxIpcCreateSection(_In_ ULONG64 Size, _Out_ PVOID *Section);

LONG LxSignalFromException(_In_ NTSTATUS ExceptionCode, _Out_ PLONG Code);
VOID LxSignalSend(_In_ PLX_PROCESS Target, _In_opt_ PLX_THREAD TargetThread, _In_ LONG Signal, _In_opt_ PLX_SIGINFO Info);
VOID LxSignalCheckPending(_In_ PLX_THREAD Thread, _Inout_ PKTRAP_FRAME TrapFrame, _Inout_opt_ PKEXCEPTION_FRAME ExceptionFrame);
VOID LxSeccompInherit(_Inout_ PLX_THREAD Thread, _In_ PLX_THREAD Parent);
VOID LxSeccompRelease(_Inout_ PLX_THREAD Thread);
ULONG LxSeccompFilterCount(_In_ PLX_THREAD Thread);
BOOLEAN LxSeccompCheck(_Inout_ PLX_CALL Call, _Out_ PLONG64 Result);
LONG64 LxSeccompPrctl(_Inout_ PLX_CALL Call, _In_ BOOLEAN Set, _In_ ULONG64 Mode, _In_ ULONG64 Program);
ULONG LxSeccompFormatActions(_In_ BOOLEAN Logged, _Out_writes_(Size) PSTR Buffer, _In_ ULONG Size);
LONG LxSeccompWriteActionsLogged(_In_ PLX_PROCESS Process, _In_reads_bytes_(Length) PCSTR Data, _In_ ULONG Length);
PLX_FILE LxAnonFileCreate(_In_ const LX_FILE_OPS *Ops, _In_ ULONG Flags, _In_ PCSTR Name, _In_ SIZE_T ObjectSize);
LONG LxAnonStat(_In_ PLX_FILE File, _Out_ PLX_STAT Stat);
BOOLEAN LxSignalDeliverFault(_In_ PLX_THREAD Thread, _Inout_ PKTRAP_FRAME TrapFrame, _Inout_opt_ PKEXCEPTION_FRAME ExceptionFrame, _In_ LONG Signal, _In_ PLX_SIGINFO Info);
VOID LxThreadAttach(_In_ PLX_THREAD Thread);
BOOLEAN LxThreadDetach(_In_ PLX_THREAD Thread);
BOOLEAN LxSignalIgnoredByDefault(_In_ LONG Signal);
BOOLEAN LxSignalStopsByDefault(_In_ LONG Signal);
VOID LxSignalWakeThread(_In_ PLX_PROCESS Process, _In_ ULONG Tid);
ULONG64 LxSignalPendingSet(_In_ PLX_THREAD Thread);
VOID LxSignalRecheck(_In_ PLX_THREAD Base, _In_ PLX_THREAD Thread);
VOID LxSignalRetarget(_In_ PLX_PROCESS Process, _In_opt_ PLX_THREAD Thread, _In_ ULONG64 Bits);
VOID LxSignalForce(_In_ PLX_THREAD Thread, _In_ LONG Signal, _In_ PLX_SIGINFO Info);
ULONG LxSeccompAuditArch(VOID);
VOID LxPtraceInitialize(VOID);
BOOLEAN LxPtraceEventEnabled(_In_ PLX_THREAD Thread, _In_ LONG Event);
BOOLEAN LxPtraceEvent(_In_ PLX_THREAD Thread, _Inout_ PKTRAP_FRAME TrapFrame, _Inout_opt_ PKEXCEPTION_FRAME ExceptionFrame,
                      _In_ LONG Event, _In_ ULONG64 Message, _In_ BOOLEAN MessageIsTid);
BOOLEAN LxPtraceSyscallEntry(_Inout_ PLX_CALL Call, _Out_ PLONG64 Result);
VOID LxPtraceSyscallExit(_In_ PLX_THREAD Thread, _Inout_ PKTRAP_FRAME TrapFrame, _Inout_opt_ PKEXCEPTION_FRAME ExceptionFrame,
                         _In_ LONG64 Result);
BOOLEAN LxPtraceSeccomp(_Inout_ PLX_CALL Call, _In_ ULONG Data);
LONG LxPtraceSignal(_In_ PLX_THREAD Thread, _Inout_ PKTRAP_FRAME TrapFrame, _Inout_opt_ PKEXCEPTION_FRAME ExceptionFrame,
                    _In_ LONG Signal, _Inout_ PLX_SIGINFO Info);
BOOLEAN LxPtraceGroupStop(_In_ PLX_THREAD Thread, _Inout_ PKTRAP_FRAME TrapFrame,
                          _Inout_opt_ PKEXCEPTION_FRAME ExceptionFrame, _In_ LONG Signal, _In_ LONG Generation);
VOID LxPtraceTrap(_In_ PLX_THREAD Thread, _Inout_ PKTRAP_FRAME TrapFrame, _Inout_opt_ PKEXCEPTION_FRAME ExceptionFrame);
LONG LxPtraceCloneChild(_In_ PLX_THREAD Parent, _In_ PLX_THREAD Child, _In_ ULONG64 Flags, _In_ ULONG ExitSignal);
VOID LxPtraceExec(_In_ PLX_THREAD Thread, _Inout_ PKTRAP_FRAME TrapFrame, _Inout_opt_ PKEXCEPTION_FRAME ExceptionFrame);
VOID LxPtraceRelease(_In_ PLX_THREAD Thread);
VOID LxPtraceTransfer(_In_ PLX_THREAD From, _In_ PLX_THREAD To);
BOOLEAN LxPtraceStepping(_In_ PLX_THREAD Thread);
BOOLEAN LxPtraceSeccompSuspended(_In_ PLX_THREAD Thread);
VOID LxPtraceThreadExit(_In_ PLX_THREAD Thread);
VOID LxPtraceTracerExit(_In_ PLX_PROCESS Tracer);
BOOLEAN LxPtraceTracedBy(_In_ PLX_PROCESS Process, _In_ PLX_PROCESS Tracer);
BOOLEAN LxPtraceWaitFind(_In_ PLX_PROCESS Tracer, _In_ ULONG Type, _In_ ULONG Id, _In_opt_ PLX_PROCESS PidfdTarget,
                         _In_ ULONG Options, _In_ BOOLEAN Consume, _Out_ PULONG Tid, _Out_ PLONG Status, _Out_ PULONG Uid,
                         _Out_ PBOOLEAN Trapped, _Inout_ PBOOLEAN Any);
BOOLEAN LxPtraceWaitTrapped(_In_ PLX_PROCESS Tracer, _In_ ULONG Type, _In_ ULONG Id, _In_opt_ PLX_PROCESS PidfdTarget,
                            _In_ ULONG Options);
ULONG LxPtraceTracerPid(_In_ PLX_PROCESS Viewer, _In_ PLX_THREAD Thread);
BOOLEAN LxPtraceStopped(_In_ PLX_THREAD Thread);
LONG LxPtraceSetPtracer(_In_ PLX_PROCESS Process, _In_ ULONG64 Value);
LONG LxPtraceAccessCheck(_In_ PLX_PROCESS Caller, _In_ PLX_PROCESS Target);
LONG LxYamaScopeValue(VOID);
LONG LxYamaSetScope(_In_ PLX_PROCESS Process, _In_ ULONG Value);
BOOLEAN LxPgrpOrphaned(_In_ PLX_INSTANCE Instance, _In_ ULONG Pgid, _In_opt_ PLX_PROCESS Ignored);
VOID LxJobctlOrphanCheck(_In_ PLX_PROCESS Process, _In_opt_ PLX_PROCESS Father);
ULONG64 LxSignalDequeue(_In_ PLX_THREAD Thread, _In_ ULONG64 Mask, _Out_ PLX_SIGINFO Info);
VOID LxSignalNotifyFds(_In_ PLX_PROCESS Process);

LONG64 LxSysCall(_Inout_ PLX_CALL Call);

ULONG LxArchElfMachine(VOID);
#define LX_CORE_MAX_REGISTERS 64
ULONG LxArchCoreRegisters(_In_ PKTRAP_FRAME TrapFrame, _In_opt_ PKEXCEPTION_FRAME ExceptionFrame,
                          _Out_writes_(LX_CORE_MAX_REGISTERS) PULONG64 Registers);
BOOLEAN LxSignalDumpsCore(_In_ LONG Signal);
BOOLEAN LxCoreDump(_In_ PLX_THREAD Thread, _In_ PKTRAP_FRAME TrapFrame, _In_opt_ PKEXCEPTION_FRAME ExceptionFrame,
                   _In_ LONG Signal);
VOID LxCoreGetPattern(_Out_writes_(Size) PSTR Buffer, _In_ ULONG Size);
LONG LxCoreSetPattern(_In_reads_bytes_(Length) PCSTR Data, _In_ ULONG Length);
ULONG64 LxArchHwcap(VOID);
ULONG64 LxArchHwcap2(VOID);
LONG LxArchSpecStoreBypassMode(VOID);
LONG LxArchPtraceGetRegset(_In_ PCONTEXT Context, _In_ ULONG64 Tls, _In_ LONG SyscallNr, _In_ ULONG Type,
                           _Out_writes_bytes_(LX_PTRACE_REGSET_MAX) PUCHAR Buffer, _Out_ PULONG Size, _Out_ PULONG Unit);
LONG LxArchPtraceSetRegset(_Inout_ PCONTEXT Context, _Inout_ PULONG64 Tls, _Inout_ PLONG SyscallNr, _In_ ULONG Type,
                           _In_reads_bytes_(LX_PTRACE_REGSET_MAX) PUCHAR Buffer, _In_ ULONG Length);
LONG LxArchPtraceSyscallNumber(_In_ PKTRAP_FRAME TrapFrame, _In_ LONG Recorded);
LONG64 LxArchPtraceSyscallReturn(_In_ PCONTEXT Context);
BOOLEAN LxArchPersonalityAllowed(_In_ ULONG Personality);
ULONG LxArchExecPersonality(_In_ ULONG Personality);
VOID LxArchMmapRndBits(_Out_ PULONG Minimum, _Out_ PULONG Maximum);
ULONG64 LxArchStackRndMask(VOID);
BOOLEAN LxArchPtraceSingleStep(_Inout_ PCONTEXT Context, _In_ BOOLEAN Enable);
VOID LxArchApplySpeculation(_In_ PLX_THREAD Thread, _Inout_ PKTRAP_FRAME TrapFrame);
LONG64 LxSpecCtrlGet(_In_ PLX_THREAD Thread, _In_ ULONG64 Which);
LONG64 LxSpecCtrlSet(_Inout_ PLX_THREAD Thread, _In_ ULONG64 Which, _In_ ULONG64 Control);
VOID LxSpecExec(_Inout_ PLX_THREAD Thread);
PCSTR LxArchPlatform(VOID);
VOID LxArchGetSyscall(_In_ PKTRAP_FRAME TrapFrame, _In_opt_ PKEXCEPTION_FRAME ExceptionFrame, _Out_ PULONG Number,
                      _Out_writes_(6) PULONG64 Args);
VOID LxArchSetSyscallResult(_Inout_ PKTRAP_FRAME TrapFrame, _In_ LONG64 Result);
VOID LxArchRestartSyscall(_Inout_ PKTRAP_FRAME TrapFrame, _In_ ULONG64 Argument);
BOOLEAN LxArchEmulateInstruction(_In_ PEXCEPTION_RECORD ExceptionRecord, _Inout_opt_ PKEXCEPTION_FRAME ExceptionFrame, _Inout_ PKTRAP_FRAME TrapFrame);
VOID LxArchClassifyFault(_In_ PEXCEPTION_RECORD ExceptionRecord, _In_ PKTRAP_FRAME TrapFrame, _Inout_ PLONG Signal, _Inout_ PLONG Code);
BOOLEAN LxArchHardwareBreakpointHit(_In_ PEXCEPTION_RECORD ExceptionRecord, _In_ PKTRAP_FRAME TrapFrame, _Out_ PULONG64 Address);
VOID LxArchSetExecContext(_Inout_ PKTRAP_FRAME TrapFrame, _Inout_opt_ PKEXCEPTION_FRAME ExceptionFrame, _In_ ULONG64 Pc, _In_ ULONG64 Sp);
VOID LxArchCaptureContext(_In_ PKTRAP_FRAME TrapFrame, _In_opt_ PKEXCEPTION_FRAME ExceptionFrame, _Out_ PCONTEXT Context);
VOID LxArchSetForkContext(_Inout_ PCONTEXT Context, _In_ ULONG64 StackPointer);
BOOLEAN LxArchDeliverAsync(_In_ PLX_THREAD Thread);
VOID LxArchRestoreContext(_Inout_ PKTRAP_FRAME TrapFrame, _Inout_opt_ PKEXCEPTION_FRAME ExceptionFrame,
                          _In_ PCONTEXT Context);
VOID LxArchSetThreadPointer(_Inout_ PKTRAP_FRAME TrapFrame, _In_ ULONG64 Value);
ULONG64 LxArchGetThreadPointer(_In_ PKTRAP_FRAME TrapFrame);
ULONG64 LxArchImageBase(_In_ BOOLEAN Interpreter);
LONG64 LxArchReadSigaction(_In_ ULONG64 Address, _Out_ PLX_SIGACTION Action);
LONG64 LxArchWriteSigaction(_In_ ULONG64 Address, _In_ PLX_SIGACTION Action);
ULONG64 LxArchGetUserPc(_In_ PKTRAP_FRAME TrapFrame);
ULONG64 LxArchContextPc(_In_ PCONTEXT Context);
ULONG64 LxArchContextSp(_In_ PCONTEXT Context);
PLX_SYSCALL_ENTRY LxArchSyscallEntry(_In_ ULONG Number);
PLX_SYSCALL_ENTRY LxGenericSyscallEntry(_In_ ULONG Number);
ULONG64 LxArchGetUserSp(_In_ PKTRAP_FRAME TrapFrame);
BOOLEAN LxArchSetupSignalFrame(_Inout_ PKTRAP_FRAME TrapFrame, _Inout_opt_ PKEXCEPTION_FRAME ExceptionFrame, _In_ ULONG64 StackTop, _In_ LONG Signal, _In_ PLX_SIGINFO Info, _In_ PLX_SIGACTION Action, _In_ ULONG64 Restorer, _In_ ULONG64 SavedMask, _In_ PLX_STACK AltStack);
LONG64 LxArchSignalReturn(_Inout_ PKTRAP_FRAME TrapFrame, _Inout_opt_ PKEXCEPTION_FRAME ExceptionFrame, _Out_ PULONG64 Mask);
ULONG LxArchSigTrampoline(_Out_writes_bytes_(16) PUCHAR Code);
PCSTR LxArchCpuinfo(VOID);
PCSTR LxArchCpuVulnerability(_In_ PCSTR Name);
PCSTR LxArchConfigName(VOID);
PCSTR LxArchKernelConfig(VOID);
PUCHAR LxKernelConfigGzip(_Out_ PULONG Length);
VOID LxBpfInitialize(VOID);
LONG LxBpfDeviceCheck(_In_ PLX_PROCESS Process, _In_ BOOLEAN Block, _In_ ULONG Major, _In_ ULONG Minor, _In_ ULONG Access);
VOID LxBpfCgroupRelease(_In_ PLX_CGROUP Cgroup);

FORCEINLINE
BOOLEAN
LxMountIsSynthetic(
    _In_ PLX_MOUNT Mount)
{
    return (Mount->Kind != LxFsNt) && (Mount->Kind != LxFsTmpfs);
}

FORCEINLINE
PLX_NS
LxProcessNs(
    _In_ PLX_PROCESS Process,
    _In_ LX_NS_KIND Kind)
{
    PLX_THREAD Thread = LxCurrentThread();

    if ((Thread != NULL) && (Thread->Process == Process) && (Thread->NsOverride[Kind] != NULL))
        return Thread->NsOverride[Kind];
    return Process->Ns[Kind];
}

#define LX_SYSCALL(Name) LONG64 Name(_Inout_ PLX_CALL Call)


#include "lxsys.h"
