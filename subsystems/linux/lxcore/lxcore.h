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
#define LX_MMAP_MIN_ADDR    0x10000ULL
#define LX_VMA_VIEW         0x20000000
#define LX_VMA_HEAP         0x40000000
#define LX_VMA_STACK        0x80000000
#define LX_PIE_BASE         0x0000555555550000ULL
#define LX_INTERP_BASE      0x00007FF000000000ULL
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

typedef struct _LX_TTY
{
    volatile LONG RefCount;
    KEVENT Lock;
    KEVENT DataReady;
    KEVENT ReadLock;
    HANDLE Input;
    HANDLE Output;
    LX_TERMIOS Termios;
    LX_WINSIZE WinSize;
    ULONG ForegroundPgrp;
    ULONG Session;
    BOOLEAN InputClosed;
    BOOLEAN EofPending;
    ULONG LineLength;
    ULONG ReadyLength;
    LX_POLL_HEAD Poll;
    PLX_INSTANCE Instance;
    UCHAR Line[LX_TTY_BUFFER];
    UCHAR Ready[LX_TTY_BUFFER];
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
};

typedef enum _LX_FS_KIND
{
    LxFsNt = 1,
    LxFsProc,
    LxFsSys,
    LxFsCgroup,
    LxFsDevpts
} LX_FS_KIND;

struct _LX_MOUNT
{
    LIST_ENTRY Link;
    PLX_INSTANCE Instance;
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

struct _LX_INSTANCE
{
    volatile LONG RefCount;
    LIST_ENTRY Link;
    UNICODE_STRING RootNt;
    UNICODE_STRING TmpNt;
    FAST_MUTEX Lock;
    LIST_ENTRY Mounts;
    ULONG NextMountId;
    ULONG NextPid;
    ULONG NextTmpId;
    CHAR Hostname[65];
    CHAR DomainName[65];
    CHAR BootId[37];
    LONG64 BootTime;
    PLX_CGROUP RootCgroup;
    PLX_TTY Console;
    ULONG ProcessCount;
    BOOLEAN Booted;
    BOOLEAN ShuttingDown;
    LONG RebootCommand;
    KEVENT InitExited;
    LONG InitStatus;
    LX_EVENT_SOURCE MountEvents;
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
    LONG64 ExitTime;
    PLX_ITIMER RealTimer;
    ULONG Trace;
    LIST_ENTRY Timers;
    LONG NextTimerId;
};

typedef struct _LX_EXEC_REQUEST
{
    PSTR FileName;
    PSTR Args;
    ULONG ArgLength;
    ULONG ArgCount;
    PSTR Env;
    ULONG EnvLength;
    ULONG EnvCount;
    NTSTATUS Status;
    LONG Errno;
    KEVENT Done;
} LX_EXEC_REQUEST, *PLX_EXEC_REQUEST;

struct _LX_THREAD
{
    PLX_PROCESS Process;
    ULONG Tid;
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
PLX_MOUNT LxMountFind(_In_ PLX_INSTANCE Instance, _In_ PCSTR Path, _Out_opt_ PCSTR *Relative);
LONG LxMountAdd(_In_ PLX_INSTANCE Instance, _In_ PCSTR Target, _In_ PCSTR Source, _In_ PCSTR FsType, _In_ ULONG Flags, _In_opt_ PCSTR Data);
LONG LxMountRemove(_In_ PLX_INSTANCE Instance, _In_ PCSTR Target);
LONG LxMountCreateDetached(_In_ PLX_INSTANCE Instance, _In_ PCSTR FsType, _In_ PCSTR Source, _In_ ULONG Flags, _In_ ULONG Mode, _Out_ PLX_MOUNT *Mount);
LONG LxMountBindDetached(_In_ PLX_INSTANCE Instance, _In_ PLX_NODE Source, _Out_ PLX_MOUNT *Mount);
LONG LxMountBind(_In_ PLX_INSTANCE Instance, _In_ PLX_NODE Source, _In_ PCSTR Target, _In_ ULONG Flags);
LONG LxMountMove(_In_ PLX_INSTANCE Instance, _In_ PLX_MOUNT Mount, _In_ PCSTR Target);
VOID LxMountSetFlags(_In_ PLX_INSTANCE Instance, _In_ PLX_MOUNT Mount, _In_ ULONG Set, _In_ ULONG Clear);
PSTR LxMountInfo(_In_ PLX_PROCESS Process, _In_ BOOLEAN Full, _Out_ PULONG Length);
LONG LxMountStatfs(_In_ PLX_MOUNT Mount, _Out_writes_bytes_(120) PVOID Statfs);

LONG LxLookup(_In_ PLX_PROCESS Process, _In_ LONG DirFd, _In_ PCSTR Path, _In_ ULONG Flags, _Out_ PLX_NODE Node);
VOID LxNodeFree(_Inout_ PLX_NODE Node);
LONG LxPathNormalize(_In_ PCSTR Input, _Out_writes_(LX_PATH_MAX) PSTR Output);
LONG LxNtPath(_In_ PLX_MOUNT Mount, _In_ PCSTR Relative, _Out_ PUNICODE_STRING NtPath);
LONG LxVfsOpen(_In_ PLX_PROCESS Process, _In_ LONG DirFd, _In_ PCSTR Path, _In_ ULONG Flags, _In_ ULONG Mode, _Out_ PLX_FILE *File);
LONG LxVfsOpenResolve(_In_ PLX_PROCESS Process, _In_ LONG DirFd, _In_ PCSTR Path, _In_ ULONG Flags, _In_ ULONG Mode, _In_ ULONG Resolve, _Out_ PLX_FILE *File);
LONG LxVfsOpenNode(_In_ PLX_PROCESS Process, _Inout_ PLX_NODE Node, _In_ ULONG Flags, _In_ ULONG Mode, _Out_ PLX_FILE *File);
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
VOID LxVcacheInvalidatePath(_In_ PLX_INSTANCE Instance, _In_ PCSTR Path);
VOID LxVcachePurgeInstance(_In_ PLX_INSTANCE Instance);
LONG LxNtCreateDirectory(_In_ PCUNICODE_STRING NtPath, _In_ ULONG Mode);
LONG LxNtDeleteTree(_In_ PCUNICODE_STRING NtPath);
LONG LxNtWriteLxAttributes(_In_ HANDLE Handle, _In_ ULONG Mode, _In_ ULONG Uid, _In_ ULONG Gid, _In_ ULONG64 Device, _In_ BOOLEAN WriteDevice);
LONG LxNtReadLxAttributes(_In_ HANDLE Handle, _Out_ PULONG Mode, _Out_ PULONG Uid, _Out_ PULONG Gid, _Out_ PULONG64 Device);
PLX_FILE LxDeviceOpen(_In_ PLX_PROCESS Process, _In_ ULONG64 Device, _In_ ULONG Flags, _In_ PCSTR Path);

LONG LxSynthQuery(_In_ PLX_PROCESS Process, _In_ PLX_MOUNT Mount, _In_ PCSTR Relative, _Out_ PULONG Type, _Out_opt_ PSTR *LinkTarget);
LONG LxSynthOpen(_In_ PLX_PROCESS Process, _In_ PLX_NODE Node, _In_ ULONG Flags, _Out_ PLX_FILE *File);
LONG LxSynthStat(_In_ PLX_PROCESS Process, _In_ PLX_NODE Node, _Out_ PLX_STAT Stat);
LONG LxSynthMkdir(_In_ PLX_PROCESS Process, _In_ PLX_NODE Node);
LONG LxSynthRmdir(_In_ PLX_PROCESS Process, _In_ PLX_NODE Node);
LONG LxSynthReadlink(_In_ PLX_PROCESS Process, _In_ PLX_NODE Node, _Out_ PSTR *Target);
VOID LxSynthInitialize(VOID);
PLX_CGROUP LxCgroupFromFile(_In_ PLX_FILE File);
PLX_CGROUP LxCgroupCreateRoot(VOID);
VOID LxCgroupAttach(_In_ PLX_PROCESS Process, _In_ PLX_CGROUP Cgroup);
PSTR LxCgroupPath(_In_ PLX_CGROUP Cgroup);
VOID LxCgroupFreeTree(_In_ PLX_CGROUP Cgroup);

NTSTATUS LxTtyCreate(_In_ HANDLE Input, _In_ HANDLE Output, _In_ USHORT Rows, _In_ USHORT Columns, _Out_ PLX_TTY *Tty);
VOID LxTtyReference(_In_ PLX_TTY Tty);
VOID LxTtyDereference(_In_ PLX_TTY Tty);
LONG64 LxTtyRead(_In_ PLX_TTY Tty, _In_ ULONG64 Buffer, _In_ ULONG64 Length, _In_ BOOLEAN NonBlocking);
LONG64 LxTtyWrite(_In_ PLX_TTY Tty, _In_ ULONG64 Buffer, _In_ ULONG64 Length);
LONG64 LxTtyIoctl(_In_ PLX_PROCESS Process, _In_ PLX_TTY Tty, _In_ ULONG Request, _In_ ULONG64 Argument);
BOOLEAN LxTtyReadable(_In_ PLX_TTY Tty);
PLX_FILE LxTtyOpen(_In_ PLX_TTY Tty, _In_ ULONG Flags);
PLX_TTY LxFileGetTty(_In_ PLX_FILE File);
ULONG LxSignalGroup(_In_ PLX_INSTANCE Instance, _In_ ULONG Pgid, _In_ LONG Signal);

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
VOID LxArchSetExecContext(_Inout_ PKTRAP_FRAME TrapFrame, _Inout_opt_ PKEXCEPTION_FRAME ExceptionFrame, _In_ ULONG64 Pc, _In_ ULONG64 Sp);
VOID LxArchCaptureContext(_In_ PKTRAP_FRAME TrapFrame, _In_opt_ PKEXCEPTION_FRAME ExceptionFrame, _Out_ PCONTEXT Context);
VOID LxArchSetForkContext(_Inout_ PCONTEXT Context, _In_ ULONG64 StackPointer);
VOID LxArchSetThreadPointer(_In_ ULONG64 Value);
ULONG64 LxArchGetThreadPointer(VOID);
ULONG64 LxArchGetUserPc(_In_ PKTRAP_FRAME TrapFrame);
PLX_SYSCALL_ENTRY LxArchSyscallEntry(_In_ ULONG Number);
PLX_SYSCALL_ENTRY LxGenericSyscallEntry(_In_ ULONG Number);
ULONG64 LxArchGetUserSp(_In_ PKTRAP_FRAME TrapFrame);
BOOLEAN LxArchSetupSignalFrame(_Inout_ PKTRAP_FRAME TrapFrame, _Inout_opt_ PKEXCEPTION_FRAME ExceptionFrame, _In_ ULONG64 StackTop, _In_ LONG Signal, _In_ PLX_SIGINFO Info, _In_ PLX_SIGACTION Action, _In_ ULONG64 Restorer, _In_ ULONG64 SavedMask, _In_ PLX_STACK AltStack);
LONG64 LxArchSignalReturn(_Inout_ PKTRAP_FRAME TrapFrame, _Inout_opt_ PKEXCEPTION_FRAME ExceptionFrame, _Out_ PULONG64 Mask);
ULONG LxArchSigTrampoline(_Out_writes_bytes_(16) PUCHAR Code);
PCSTR LxArchCpuinfo(VOID);

#define LX_SYSCALL(Name) LONG64 Name(_Inout_ PLX_CALL Call)

#include "lxsys.h"
