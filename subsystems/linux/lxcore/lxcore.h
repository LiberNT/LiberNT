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
#define LX_ALLOC_GRANULARITY 0x10000ULL
#define LX_PATH_MAX         4096
#define LX_MAX_FDS          1024
#define LX_MAX_ARG_STRLEN   (128 * 1024)
#define LX_MAX_ARG_TOTAL    (2 * 1024 * 1024)
#define LX_IO_CHUNK         (64 * 1024)
#define LX_STACK_SIZE       (8 * 1024 * 1024)
#define LX_BRK_RESERVE      (1024ULL * 1024 * 1024)
#define LX_AUXV_WORDS       40
#define LX_MMAP_MIN_ADDR    0x10000ULL
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

typedef struct _LX_FILE LX_FILE, *PLX_FILE;
typedef struct _LX_PROCESS LX_PROCESS, *PLX_PROCESS;
typedef struct _LX_THREAD LX_THREAD, *PLX_THREAD;
typedef struct _LX_INSTANCE LX_INSTANCE, *PLX_INSTANCE;
typedef struct _LX_MOUNT LX_MOUNT, *PLX_MOUNT;
typedef struct _LX_CGROUP LX_CGROUP, *PLX_CGROUP;
typedef struct _LX_NS LX_NS, *PLX_NS;

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

typedef struct _LX_VMA
{
    LIST_ENTRY Link;
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
} LX_FILE_OPS, *PLX_FILE_OPS;

#define LX_TTY_BUFFER 4096
#define LX_TTY_CONSOLE   0x00000001
#define LX_TTY_LOCKED    0x00000002
#define LX_TTY_NT_MASTER 0x00000004

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
    ULONG Seals;
    ULONG Mode;
    BOOLEAN Written;
    PLX_EVENT_SOURCE EventSource;
    LONG64 EventSeen;
    PLX_PROCESS PidProcess;
    struct _LX_FILE_ASYNC *Async;
};

typedef enum _LX_FS_KIND
{
    LxFsNt = 1,
    LxFsProc,
    LxFsSys,
    LxFsCgroup,
    LxFsDevpts,
    LxFsNsfs,
    LxFsMqueue
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
    PSTR Root;
    PSTR Alias;
    BOOLEAN Detached;
};

#define LX_MS_RDONLY        0x00000001
#define LX_MS_NOSUID        0x00000002
#define LX_MS_NODEV         0x00000004
#define LX_MS_NOEXEC        0x00000008
#define LX_MS_REMOUNT       0x00000020
#define LX_MS_NOATIME       0x00000400
#define LX_MS_BIND          0x00001000
#define LX_MS_MOVE          0x00002000
#define LX_MS_REC           0x00004000
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
        } Net;
        struct
        {
            PLX_CGROUP Root;
        } Cgroup;
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
    ULONG NextTmpId;
    CHAR BootId[37];
    LONG64 BootTime;
    PLX_CGROUP RootCgroup;
    PLX_TTY Console;
    LIST_ENTRY Ptys;
    ULONG ProcessCount;
    BOOLEAN Booted;
    BOOLEAN ShuttingDown;
    LONG RebootCommand;
    KEVENT InitExited;
    LONG InitStatus;
    ULONG NextDetachedId;
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

#define LX_LOOKUP_FOLLOW    0x00000001
#define LX_LOOKUP_DIRECTORY 0x00000002
#define LX_LOOKUP_CREATE    0x00000004
#define LX_LOOKUP_NO_XDEV       0x00000010
#define LX_LOOKUP_NO_MAGICLINKS 0x00000020
#define LX_LOOKUP_NO_SYMLINKS   0x00000040
#define LX_LOOKUP_BENEATH       0x00000080
#define LX_LOOKUP_IN_ROOT       0x00000100
#define LX_LOOKUP_RESOLVE       0x000001F0

typedef struct _LX_ITIMER
{
    KTIMER Timer;
    KDPC Dpc;
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
};

struct _LX_PROCESS
{
    volatile LONG RefCount;
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
    ULONG64 BrkBase;
    ULONG64 BrkCurrent;
    ULONG64 BrkCommitted;
    ULONG64 BrkLimit;
    BOOLEAN BrkReserved;
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
    ULONG PdeathSignal;
    LX_RLIMIT Limits[16];
    LX_POLL_HEAD ExitPoll;
    LX_POLL_HEAD SignalPoll;
    LX_POLL_HEAD ChildPoll;
    ULONG Uid[3];
    ULONG Gid[3];
    ULONG Fsuid;
    ULONG Fsgid;
    ULONG GroupCount;
    ULONG Groups[32];
    ULONG Dumpable;
    BOOLEAN NoNewPrivs;
    BOOLEAN KeepCaps;
    BOOLEAN Execed;
    ULONG64 CapBounding;
    ULONG64 CapAmbient;
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
};

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
    LIST_ENTRY Link;
    volatile LONG64 Pending;
    LX_STACK AltStack;
    LX_SCHED_ATTR Sched;
    ULONG64 SuspendMask;
    BOOLEAN Suspended;
    CHAR Comm[16];
    ULONG CurrentSyscall;
    ULONG64 CurrentArgs[6];
    ULONG64 RobustList;
    LONG64 Restart;
    ULONG64 RestartArgument;
    PLX_NS NsOverride[LxNsCount];
    PLX_FS_STATE Fs;
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
#define LX_PT_LOAD      1
#define LX_PT_INTERP    3
#define LX_PT_PHDR      6
#define LX_PF_X         1
#define LX_PF_W         2
#define LX_PF_R         4

extern LIST_ENTRY LxProcessList;
extern FAST_MUTEX LxProcessListLock;
extern LIST_ENTRY LxInstanceList;
extern FAST_MUTEX LxInstanceListLock;
extern KEVENT LxInstanceCreateLock;

VOID LxProcessInitialize(VOID);
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
VOID LxSchedInherit(_Inout_ PLX_THREAD Child, _In_ PLX_THREAD Parent);
PLX_THREAD LxCurrentThread(VOID);

VOID LxPollHeadInit(_Out_ PLX_POLL_HEAD Head);
VOID LxEventSourceInit(_Out_ PLX_EVENT_SOURCE Source);
VOID LxEventSourceSignal(_In_ PLX_EVENT_SOURCE Source);
VOID LxPollAdd(_In_ PLX_POLL_HEAD Head, _Inout_ PLX_POLL_ENTRY Entry);
VOID LxPollRemove(_Inout_ PLX_POLL_ENTRY Entry);
VOID LxPollWake(_In_ PLX_POLL_HEAD Head, _In_ ULONG Events);
NTSTATUS LxWaitForEvent(_In_ PKEVENT Event, _In_opt_ PLARGE_INTEGER Timeout);
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
LONG64 LxFileReadKernel(_In_ PLX_FILE File, _Out_writes_bytes_(Length) PVOID Buffer, _In_ ULONG Length, _In_ ULONG64 Offset);
LONG LxFileStat(_In_ PLX_FILE File, _Out_ PLX_STAT Stat);
PLX_FILE LxFileFromNtHandle(_In_ HANDLE Handle, _In_ ULONG Flags);
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
LONG LxMountCreateDetached(_In_ PLX_PROCESS Process, _In_ PCSTR FsType, _In_ PCSTR Source, _In_ ULONG Flags, _In_ ULONG Mode, _Out_ PLX_MOUNT *Mount);
LONG LxMountBindDetached(_In_ PLX_PROCESS Process, _In_ PLX_NODE Source, _Out_ PLX_MOUNT *Mount);
LONG LxMountBind(_In_ PLX_PROCESS Process, _In_ PLX_NODE Source, _In_ PCSTR Target, _In_ ULONG Flags);
LONG LxMountMove(_In_ PLX_NS Ns, _In_ PLX_MOUNT Mount, _In_ PCSTR Target);
VOID LxMountSetFlags(_In_ PLX_MOUNT Mount, _In_ ULONG Set, _In_ ULONG Clear);
PSTR LxMountInfo(_In_ PLX_PROCESS Process, _In_ BOOLEAN Full, _Out_ PULONG Length);
LONG LxMountStatfs(_In_ PLX_MOUNT Mount, _Out_writes_bytes_(120) PVOID Statfs);
LONG LxMountNsCopy(_In_ PLX_NS New, _In_ PLX_NS Old);
VOID LxMountNsRelease(_In_ PLX_NS Ns);
LONG LxMountPivotRoot(_In_ PLX_PROCESS Process, _In_ PLX_NODE NewRoot, _In_ PLX_NODE PutOld);
PSTR LxPathRebase(_In_ PCSTR Path, _In_ PCSTR NewRoot, _In_ PCSTR PutOld);

PLX_NS LxNsCreate(_In_ PLX_INSTANCE Instance, _In_ LX_NS_KIND Kind, _In_opt_ PLX_NS From);
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
VOID LxNsRebaseProcesses(_In_ PLX_NS MntNs, _In_ PCSTR NewRoot, _In_ PCSTR PutOld);
PSTR LxProcessRoot(_In_ PLX_PROCESS Process);
PSTR LxProcessCwd(_In_ PLX_PROCESS Process);
LONG LxProcessSetCwd(_In_ PLX_PROCESS Process, _In_ PCSTR Path);
VOID LxNsInitialize(VOID);
VOID LxNsThreadInherit(_Inout_ PLX_THREAD Thread, _In_opt_ PLX_THREAD Parent);
VOID LxNsThreadRelease(_Inout_ PLX_THREAD Thread);
LONG LxProcessChroot(_In_ PLX_PROCESS Process, _In_ PCSTR Path);
PSTR LxPathVisible(_In_ PLX_PROCESS Viewer, _In_ PCSTR Path);
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
LONG LxVfsStatNode(_In_ PLX_PROCESS Process, _In_ PLX_NODE Node, _Out_ PLX_STAT Stat);
LONG LxVfsMkdir(_In_ PLX_PROCESS Process, _In_ PLX_NODE Node, _In_ ULONG Mode);
LONG LxVfsMknod(_In_ PLX_PROCESS Process, _In_ PLX_NODE Node, _In_ ULONG Mode, _In_ ULONG64 Device);
LONG LxVfsSymlink(_In_ PLX_PROCESS Process, _In_ PLX_NODE Node, _In_ PCSTR Target);
LONG LxVfsUnlink(_In_ PLX_PROCESS Process, _In_ PLX_NODE Node, _In_ BOOLEAN Directory);
LONG LxVfsRename(_In_ PLX_PROCESS Process, _In_ PLX_NODE From, _In_ PLX_NODE To, _In_ ULONG Flags);
LONG LxVfsLink(_In_ PLX_PROCESS Process, _In_ PLX_NODE From, _In_ PLX_NODE To);
LONG LxVfsSetAttributes(_In_ PLX_PROCESS Process, _In_ PLX_NODE Node, _In_opt_ PLX_FILE File, _In_ ULONG Mode, _In_ ULONG Uid, _In_ ULONG Gid);
LONG LxVfsSetTimes(_In_opt_ PLX_NODE Node, _In_opt_ PLX_FILE File, _In_ PLX_TIMESPEC Times);
LONG LxVfsTruncate(_In_ PLX_NODE Node, _In_ LONG64 Length);
LONG LxVfsReadlink(_In_ PLX_PROCESS Process, _In_ PLX_NODE Node, _Out_ PSTR *Target);
LONG LxNtStatHandleType(_In_ HANDLE Handle, _Out_ PLX_STAT Stat);
LONG LxFifoOpen(_In_ PLX_PROCESS Process, _In_ PLX_NODE Node, _In_ ULONG Flags, _Out_ PLX_FILE *Result);
LONG LxPipeCreatePair(_In_ ULONG Flags, _Out_ PLX_FILE *ReadEnd, _Out_ PLX_FILE *WriteEnd);
LONG LxPipeSetSize(_In_ PLX_FILE File, _In_ ULONG Size);
LONG LxPipeGetSize(_In_ PLX_FILE File);
BOOLEAN LxFileIsPipe(_In_ PLX_FILE File);
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
LONG LxXattrOpenFile(_In_ PLX_FILE File, _In_ BOOLEAN Write, _Out_ PHANDLE Handle, _Out_ PBOOLEAN Close);
PLX_FILE LxDeviceOpen(_In_ PLX_PROCESS Process, _In_ ULONG64 Device, _In_ ULONG Flags, _In_ PCSTR Path);

LONG LxSynthQuery(_In_ PLX_PROCESS Process, _In_ PLX_MOUNT Mount, _In_ PCSTR Relative, _Out_ PULONG Type, _Out_opt_ PSTR *LinkTarget);
LONG LxSynthOpen(_In_ PLX_PROCESS Process, _In_ PLX_NODE Node, _In_ ULONG Flags, _Out_ PLX_FILE *File);
LONG LxSynthStat(_In_ PLX_PROCESS Process, _In_ PLX_NODE Node, _Out_ PLX_STAT Stat);
LONG LxSynthMkdir(_In_ PLX_PROCESS Process, _In_ PLX_NODE Node);
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
PLX_FILE LxUsbfsOpen(_In_ ULONG Minor, _In_ ULONG Flags, _In_ PCSTR Path);
PLX_CGROUP LxCgroupFromFile(_In_ PLX_FILE File);
PLX_CGROUP LxCgroupCreateRoot(VOID);
VOID LxCgroupAttach(_In_ PLX_PROCESS Process, _In_ PLX_CGROUP Cgroup);
PSTR LxCgroupPath(_In_ PLX_CGROUP Cgroup);
VOID LxCgroupFreeTree(_In_ PLX_CGROUP Cgroup);

NTSTATUS LxTtyCreate(_In_ PLX_INSTANCE Instance, _In_ ULONG Flags, _In_ ULONG Uid, _In_ USHORT Rows, _In_ USHORT Columns, _Out_ PLX_TTY *Tty);
VOID LxTtyReference(_In_ PLX_TTY Tty);
VOID LxTtyDereference(_In_ PLX_TTY Tty);
LONG64 LxTtyRead(_In_ PLX_TTY Tty, _In_ ULONG64 Buffer, _In_ ULONG64 Length, _In_ BOOLEAN NonBlocking);
LONG64 LxTtyWrite(_In_ PLX_TTY Tty, _In_ ULONG64 Buffer, _In_ ULONG64 Length, _In_ BOOLEAN NonBlocking);
LONG64 LxTtyIoctl(_In_ PLX_PROCESS Process, _In_ PLX_TTY Tty, _In_ ULONG Request, _In_ ULONG64 Argument);
BOOLEAN LxTtyReadable(_In_ PLX_TTY Tty);
PLX_FILE LxTtyOpen(_In_ PLX_TTY Tty, _In_ ULONG Flags);
PLX_TTY LxFileGetTty(_In_ PLX_FILE File);
LONG64 LxTtyMasterRead(_In_ PLX_TTY Tty, _Out_writes_bytes_(Length) PVOID Buffer, _In_ ULONG Length, _In_ BOOLEAN NonBlocking);
LONG64 LxTtyMasterWrite(_In_ PLX_TTY Tty, _In_reads_bytes_(Length) const VOID *Buffer, _In_ ULONG Length, _In_ BOOLEAN NonBlocking);
VOID LxTtyMasterClose(_In_ PLX_TTY Tty);
VOID LxTtySetWinSize(_In_ PLX_TTY Tty, _In_ USHORT Rows, _In_ USHORT Columns);
PLX_TTY LxTtyLookup(_In_ PLX_INSTANCE Instance, _In_ ULONG Index);
LONG LxTtyOpenSlave(_In_ PLX_PROCESS Process, _In_ PLX_TTY Tty, _In_ ULONG Flags, _Out_ PLX_FILE *File);
LONG LxPtmxOpen(_In_ PLX_PROCESS Process, _In_ ULONG Flags, _Out_ PLX_FILE *File);
ULONG64 LxTtyDevice(_In_ PLX_TTY Tty);
ULONG LxTtyList(_In_ PLX_INSTANCE Instance, _Out_writes_(Count) PULONG Indexes, _In_ ULONG Count);
VOID LxTtyAttachControlling(_In_ PLX_PROCESS Process, _In_ PLX_TTY Tty);
ULONG LxSignalGroup(_In_ PLX_INSTANCE Instance, _In_opt_ PLX_NS PidNs, _In_ ULONG Pgid, _In_ LONG Signal);
ULONG LxSignalGroupInfo(_In_ PLX_INSTANCE Instance, _In_opt_ PLX_NS PidNs, _In_ ULONG Pgid, _In_ LONG Signal,
                        _In_opt_ PLX_SIGINFO Info);

PLX_PROCESS LxProcessCreate(_In_ PLX_INSTANCE Instance);
PLX_PROCESS LxProcessFork(_In_ PLX_PROCESS Parent);
VOID LxProcessReference(_In_ PLX_PROCESS Process);
VOID LxProcessDereference(_In_ PLX_PROCESS Process);
PLX_THREAD LxThreadCreate(_In_ PLX_PROCESS Process);
VOID LxThreadFree(_In_ PLX_THREAD Thread);
PLX_PROCESS LxProcessLookup(_In_ PLX_INSTANCE Instance, _In_ ULONG Pid);
PLX_THREAD LxThreadLookupReferenced(_In_ PLX_INSTANCE Instance, _In_ ULONG Tid, _Out_ PLX_PROCESS *Process);
VOID LxProcessExitStatus(_In_ PLX_PROCESS Process, _In_ LONG WaitStatus);
DECLSPEC_NORETURN VOID LxProcessExitGroup(_In_ PLX_PROCESS Process, _In_ LONG WaitStatus);
VOID LxProcessExited(_In_ PLX_PROCESS Process, _In_ NTSTATUS ExitStatus);
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
BOOLEAN LxMmMapsPath(_In_ PLX_PROCESS Process, _In_opt_ PCSTR Path);
VOID LxMmSetBrk(_In_ PLX_PROCESS Process, _In_ ULONG64 StartBrk, _In_ ULONG64 Brk);
PLX_FILE LxMmFileAt(_In_ PLX_PROCESS Process, _In_ ULONG64 Start, _In_ ULONG64 End);
ULONG LxMmFileRanges(_In_ PLX_PROCESS Process, _Out_writes_(Max * 2) PULONG64 Ranges, _In_ ULONG Max);
VOID LxMmUnlock(_In_ PLX_PROCESS Process);
LONG LxMmFork(_In_ PLX_PROCESS Child, _In_ PLX_PROCESS Parent);
VOID LxMmRelease(_In_ PLX_PROCESS Process);
BOOLEAN LxMmHandleFault(_In_ PLX_PROCESS Process, _In_ ULONG64 Address, _In_ ULONG Access);
BOOLEAN LxMmFaultIn(_In_ ULONG64 Address, _In_ SIZE_T Length, _In_ BOOLEAN Write);
LONG LxMmZero(_In_ PLX_PROCESS Process, _In_ ULONG64 Address, _In_ ULONG64 End);
BOOLEAN LxMmIsMapped(_In_ PLX_PROCESS Process, _In_ ULONG64 Start, _In_ ULONG64 End);
ULONG LxProtToNt(_In_ ULONG Prot);

NTSTATUS LxExecImage(_In_ PLX_THREAD Thread, _In_ PLX_EXEC_REQUEST Request, _Inout_ PKTRAP_FRAME TrapFrame, _Inout_opt_ PKEXCEPTION_FRAME ExceptionFrame, _Out_ PLONG Errno);

LONG64 LxFutexWake(_In_ ULONG64 Address, _In_ LONG Count);
VOID LxFutexInitialize(VOID);
VOID LxFutexExitRobustList(_In_ PLX_THREAD Thread);
BOOLEAN LxMmSharedKey(_In_ PLX_PROCESS Process, _In_ ULONG64 Address, _Out_ PVOID *Object, _Out_ PULONG64 Offset);
LONG64 LxMmMapSection(_In_ PLX_PROCESS Process, _In_ PVOID Section, _In_ ULONG64 Address, _In_ ULONG64 Length, _In_ ULONG Prot, _In_ BOOLEAN Remap);
LONG LxMmDetachSection(_In_ PLX_PROCESS Process, _In_ ULONG64 Address);
LONG LxMmPrepareRemote(_In_ PLX_PROCESS Process, _In_ PEPROCESS EProcess, _In_ ULONG64 Address, _In_ ULONG64 Length, _In_ BOOLEAN Write);
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

LONG LxSignalFromException(_In_ NTSTATUS ExceptionCode, _Out_ PLONG Code);
VOID LxSignalSend(_In_ PLX_PROCESS Target, _In_opt_ PLX_THREAD TargetThread, _In_ LONG Signal, _In_opt_ PLX_SIGINFO Info);
VOID LxSignalCheckPending(_In_ PLX_THREAD Thread, _Inout_ PKTRAP_FRAME TrapFrame, _Inout_opt_ PKEXCEPTION_FRAME ExceptionFrame);
BOOLEAN LxSignalDeliverFault(_In_ PLX_THREAD Thread, _Inout_ PKTRAP_FRAME TrapFrame, _Inout_opt_ PKEXCEPTION_FRAME ExceptionFrame, _In_ LONG Signal, _In_ PLX_SIGINFO Info);
VOID LxThreadAttach(_In_ PLX_THREAD Thread);
VOID LxThreadDetach(_In_ PLX_THREAD Thread);
BOOLEAN LxSignalIgnoredByDefault(_In_ LONG Signal);
ULONG64 LxSignalDequeue(_In_ PLX_THREAD Thread, _In_ ULONG64 Mask, _Out_ PLX_SIGINFO Info);
VOID LxSignalNotifyFds(_In_ PLX_PROCESS Process);

LONG64 LxSysCall(_Inout_ PLX_CALL Call);

ULONG LxArchElfMachine(VOID);
ULONG64 LxArchHwcap(VOID);
PCSTR LxArchPlatform(VOID);
VOID LxArchGetSyscall(_In_ PKTRAP_FRAME TrapFrame, _Out_ PULONG Number, _Out_writes_(6) PULONG64 Args);
VOID LxArchSetSyscallResult(_Inout_ PKTRAP_FRAME TrapFrame, _In_ LONG64 Result);
VOID LxArchRestartSyscall(_Inout_ PKTRAP_FRAME TrapFrame, _In_ ULONG64 Argument);
BOOLEAN LxArchEmulateInstruction(_In_ PEXCEPTION_RECORD ExceptionRecord, _Inout_opt_ PKEXCEPTION_FRAME ExceptionFrame, _Inout_ PKTRAP_FRAME TrapFrame);
VOID LxArchSetExecContext(_Inout_ PKTRAP_FRAME TrapFrame, _Inout_opt_ PKEXCEPTION_FRAME ExceptionFrame, _In_ ULONG64 Pc, _In_ ULONG64 Sp);
VOID LxArchCaptureContext(_In_ PKTRAP_FRAME TrapFrame, _In_opt_ PKEXCEPTION_FRAME ExceptionFrame, _Out_ PCONTEXT Context);
VOID LxArchSetForkContext(_Inout_ PCONTEXT Context, _In_ ULONG64 StackPointer);
VOID LxArchSetThreadPointer(_Inout_ PKTRAP_FRAME TrapFrame, _In_ ULONG64 Value);
ULONG64 LxArchGetThreadPointer(_In_ PKTRAP_FRAME TrapFrame);
ULONG64 LxArchImageBase(_In_ BOOLEAN Interpreter);
LONG64 LxArchReadSigaction(_In_ ULONG64 Address, _Out_ PLX_SIGACTION Action);
LONG64 LxArchWriteSigaction(_In_ ULONG64 Address, _In_ PLX_SIGACTION Action);
ULONG64 LxArchGetUserPc(_In_ PKTRAP_FRAME TrapFrame);
PLX_SYSCALL_ENTRY LxArchSyscallEntry(_In_ ULONG Number);
PLX_SYSCALL_ENTRY LxGenericSyscallEntry(_In_ ULONG Number);
ULONG64 LxArchGetUserSp(_In_ PKTRAP_FRAME TrapFrame);
BOOLEAN LxArchSetupSignalFrame(_Inout_ PKTRAP_FRAME TrapFrame, _Inout_opt_ PKEXCEPTION_FRAME ExceptionFrame, _In_ ULONG64 StackTop, _In_ LONG Signal, _In_ PLX_SIGINFO Info, _In_ PLX_SIGACTION Action, _In_ ULONG64 Restorer, _In_ ULONG64 SavedMask, _In_ PLX_STACK AltStack);
LONG64 LxArchSignalReturn(_Inout_ PKTRAP_FRAME TrapFrame, _Inout_opt_ PKEXCEPTION_FRAME ExceptionFrame, _Out_ PULONG64 Mask);
ULONG LxArchSigTrampoline(_Out_writes_bytes_(16) PUCHAR Code);
PCSTR LxArchCpuinfo(VOID);
PCSTR LxArchConfigName(VOID);
PCSTR LxArchKernelConfig(VOID);
PUCHAR LxKernelConfigGzip(_Out_ PULONG Length);
VOID LxBpfInitialize(VOID);
LONG LxBpfDeviceCheck(_In_ PLX_PROCESS Process, _In_ BOOLEAN Block, _In_ ULONG Major, _In_ ULONG Minor, _In_ ULONG Access);
VOID LxBpfCgroupRelease(_In_ PLX_CGROUP Cgroup);

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
