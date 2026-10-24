/*
 * PROJECT:     LiberNT Linux Subsystem
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Driver entry, control device and pico provider callbacks
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "lxcore.h"

#define NDEBUG
#include <debug.h>

DRIVER_INITIALIZE DriverEntry;

static PICO_SYSTEM_CALL_DISPATCH LxPicoSystemCall;
static PICO_DISPATCH_EXCEPTION LxPicoDispatchException;
static PICO_THREAD_START LxPicoThreadStart;
static PICO_THREAD_EXIT LxPicoThreadExit;
static PICO_PROCESS_EXIT LxPicoProcessExit;

typedef struct _LX_NT_PTY
{
    PLX_TTY volatile Tty;
    volatile LONG Closed;
} LX_NT_PTY, *PLX_NT_PTY;

static PDEVICE_OBJECT LxDeviceObject;

static
VOID
NTAPI
LxPicoSystemCall(
    _Inout_ PKTRAP_FRAME TrapFrame,
    _Inout_opt_ PKEXCEPTION_FRAME ExceptionFrame)
{
    LX_CALL Call;
    LONG64 Result;

    RtlZeroMemory(&Call, sizeof(Call));
    Call.Thread = PsPicoGetThreadContext(PsGetCurrentThread());
    Call.Process = Call.Thread->Process;
    Call.TrapFrame = TrapFrame;
    Call.ExceptionFrame = ExceptionFrame;
    LxArchGetSyscall(TrapFrame, &Call.Number, Call.Args);

    Result = LxSysCall(&Call);
    if ((Result == -LX_ERESTARTSYS) || (Result == -LX_ERESTARTNOINTR) || (Result == -LX_ERESTARTNOHAND))
    {
        Call.Thread->Restart = Result;
        Call.Thread->RestartArgument = Call.Args[0];
        Result = -LX_EINTR;
    }
    if (Result != LX_SYSCALL_NO_RESULT)
        LxArchSetSyscallResult(TrapFrame, Result);

    LxSignalCheckPending(Call.Thread, TrapFrame, ExceptionFrame);
}

static
BOOLEAN
NTAPI
LxPicoDispatchException(
    _Inout_ PEXCEPTION_RECORD ExceptionRecord,
    _Inout_opt_ PKEXCEPTION_FRAME ExceptionFrame,
    _Inout_ PKTRAP_FRAME TrapFrame)
{
    PLX_THREAD Thread = PsPicoGetThreadContext(PsGetCurrentThread());
    LX_SIGINFO Info;
    LONG Code;
    LONG Signal = LxSignalFromException(ExceptionRecord->ExceptionCode, &Code);

    if (LxArchEmulateInstruction(ExceptionRecord, ExceptionFrame, TrapFrame))
        return TRUE;
    if ((ExceptionRecord->ExceptionCode == STATUS_ACCESS_VIOLATION) &&
        (ExceptionRecord->NumberParameters >= 2) &&
        LxMmHandleFault(Thread->Process,
                        (ULONG64)ExceptionRecord->ExceptionInformation[1],
                        (ExceptionRecord->ExceptionInformation[0] == 1) ? LX_PROT_WRITE :
                        (ExceptionRecord->ExceptionInformation[0] == 8) ? LX_PROT_EXEC : LX_PROT_READ))
    {
        return TRUE;
    }

    RtlZeroMemory(&Info, sizeof(Info));
    Info.Signo = Signal;
    Info.Code = Code;
    Info.Address = (ExceptionRecord->NumberParameters >= 2) ?
                   (ULONG64)ExceptionRecord->ExceptionInformation[1] :
                   (ULONG64)(ULONG_PTR)ExceptionRecord->ExceptionAddress;

    if (LxSignalDeliverFault(Thread, TrapFrame, ExceptionFrame, Signal, &Info))
        return TRUE;

    DPRINT1("Linux process %lu was killed by signal %ld: exception 0x%08lx at %p, address %p\n",
            Thread->Process->Pid,
            Signal,
            ExceptionRecord->ExceptionCode,
            (PVOID)(ULONG_PTR)LxArchGetUserPc(TrapFrame),
            (ExceptionRecord->NumberParameters >= 2) ?
                (PVOID)ExceptionRecord->ExceptionInformation[1] : NULL);

    LxProcessExitGroup(Thread->Process, Signal);
}

static
VOID
NTAPI
LxPicoThreadStart(
    _Inout_ PKTRAP_FRAME TrapFrame,
    _Inout_opt_ PKEXCEPTION_FRAME ExceptionFrame)
{
    PLX_THREAD Thread = PsPicoGetThreadContext(PsGetCurrentThread());
    PLX_EXEC_REQUEST Request = Thread->Exec;

    if (Request != NULL)
    {
        LONG Errno = 0;
        NTSTATUS Status;

        Thread->Exec = NULL;
        LxThreadAttach(Thread);
        LxArchSetThreadPointer(TrapFrame, 0);
        Status = LxExecImage(Thread, Request, TrapFrame, ExceptionFrame, &Errno);
        Request->Status = Status;
        Request->Errno = Errno;
        KeSetEvent(&Request->Done, IO_NO_INCREMENT, FALSE);
        if (!NT_SUCCESS(Status))
            LxProcessExitGroup(Thread->Process, 127 << 8);
        return;
    }

    LxThreadAttach(Thread);
    LxArchSetThreadPointer(TrapFrame, Thread->Tls);
    if (Thread->SetChildTid != 0)
        LxCopyToUser(Thread->SetChildTid, &Thread->Tid, sizeof(Thread->Tid));
}

static
VOID
NTAPI
LxPicoThreadExit(
    _In_ PETHREAD EThread)
{
    PLX_THREAD Thread = PsPicoGetThreadContext(EThread);
    ULONG Zero = 0;

    if (Thread == NULL)
        return;

    LxFutexExitRobustList(Thread);
    if (Thread->ClearChildTid != 0)
    {
        if (LxCopyToUser(Thread->ClearChildTid, &Zero, sizeof(Zero)) == 0)
            LxFutexWake(Thread->ClearChildTid, 1);
    }

    LxThreadDetach(Thread);
    LxThreadFree(Thread);
}

static
VOID
NTAPI
LxPicoProcessExit(
    _In_ PEPROCESS EProcess)
{
    PLX_PROCESS Process = PsPicoGetProcessContext(EProcess);

    if (Process == NULL)
        return;

    LxMmLock(Process);
    LxMmRelease(Process);
    LxMmUnlock(Process);
    LxProcessExited(Process, PsGetProcessExitStatus(EProcess));
    InterlockedDecrement((PLONG)&Process->Instance->ProcessCount);
    LxProcessDereference(Process);
}

static
NTSTATUS
LxCaptureRange(
    _In_ PUCHAR Buffer,
    _In_ ULONG BufferLength,
    _In_ ULONG Offset,
    _In_ ULONG Length,
    _In_ ULONG MaxLength,
    _Out_ PUCHAR *Data)
{
    if ((Length > MaxLength) ||
        (Offset > BufferLength) ||
        (Length > BufferLength - Offset))
    {
        return STATUS_INVALID_PARAMETER;
    }

    *Data = Buffer + Offset;
    return STATUS_SUCCESS;
}

static
NTSTATUS
LxCaptureStringList(
    _In_ PUCHAR Data,
    _In_ ULONG Length,
    _In_ ULONG Count,
    _Out_ PSTR *Copy)
{
    ULONG Index, Strings = 0;

    *Copy = NULL;
    if ((Length == 0) != (Count == 0))
        return STATUS_INVALID_PARAMETER;
    if (Length == 0)
        return STATUS_SUCCESS;
    if (Data[Length - 1] != '\0')
        return STATUS_INVALID_PARAMETER;

    for (Index = 0; Index < Length; Index++)
    {
        if (Data[Index] == '\0')
            Strings++;
    }
    if (Strings != Count)
        return STATUS_INVALID_PARAMETER;

    *Copy = LxAlloc(Length);
    if (*Copy == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;

    RtlCopyMemory(*Copy, Data, Length);
    return STATUS_SUCCESS;
}

static
PSTR
LxCaptureUtf8(
    _In_ PUCHAR Data,
    _In_ ULONG Length)
{
    PSTR Copy = LxAlloc(Length + 1);

    if (Copy != NULL)
    {
        RtlCopyMemory(Copy, Data, Length);
        Copy[Length] = '\0';
    }
    return Copy;
}

static
VOID
LxFreeRequest(
    _In_ PLX_EXEC_REQUEST Request)
{
    LxFree(Request->FileName);
    LxFree(Request->Args);
    LxFree(Request->Env);
    LxFree(Request);
}

static
NTSTATUS
LxCaptureUserFile(
    _In_ HANDLE UserHandle,
    _In_ ACCESS_MASK Access,
    _Out_ PHANDLE KernelHandle)
{
    PFILE_OBJECT FileObject;
    NTSTATUS Status;

    Status = ObReferenceObjectByHandle(UserHandle,
                                       Access,
                                       *IoFileObjectType,
                                       UserMode,
                                       (PVOID*)&FileObject,
                                       NULL);
    if (!NT_SUCCESS(Status))
        return Status;

    Status = ObOpenObjectByPointer(FileObject,
                                   OBJ_KERNEL_HANDLE,
                                   NULL,
                                   Access | SYNCHRONIZE | FILE_READ_ATTRIBUTES,
                                   *IoFileObjectType,
                                   KernelMode,
                                   KernelHandle);
    ObDereferenceObject(FileObject);
    return Status;
}

static
NTSTATUS
LxInstallFile(
    _In_ PLX_PROCESS Process,
    _In_ PLX_FILE File,
    _In_ LONG Fd)
{
    LONG Result = LxFdInstall(Process, File, Fd, FALSE);

    LxFileDereference(File);
    return (Result == Fd) ? STATUS_SUCCESS : STATUS_TOO_MANY_OPENED_FILES;
}

static
NTSTATUS
LxInstallStdHandle(
    _In_ PLX_PROCESS Process,
    _In_ HANDLE UserHandle,
    _In_ LONG Fd)
{
    HANDLE KernelHandle;
    PLX_FILE File;
    NTSTATUS Status;

    Status = LxCaptureUserFile(UserHandle, (Fd == 0) ? FILE_READ_DATA : FILE_WRITE_DATA, &KernelHandle);
    if (!NT_SUCCESS(Status))
        return Status;

    File = LxFileFromNtHandle(KernelHandle, (Fd == 0) ? LX_O_RDONLY : LX_O_WRONLY);
    if (File == NULL)
    {
        ZwClose(KernelHandle);
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    if (File->NtPipe)
        File->Path = LxStrPrintf("pipe:[%llu]", File->Ino);

    return LxInstallFile(Process, File, Fd);
}

static
NTSTATUS
LxInstallTty(
    _In_ PLX_PROCESS Process,
    _In_ PLX_CREATE_PROCESS_IN In)
{
    BOOLEAN Boot = (In->Flags & LX_CREATE_BOOT) != 0;
    PFILE_OBJECT FileObject;
    PLX_NT_PTY Pty;
    PLX_TTY Tty;
    NTSTATUS Status;
    LONG Fd;

    Status = ObReferenceObjectByHandle(In->Pty,
                                       FILE_READ_DATA | FILE_WRITE_DATA,
                                       *IoFileObjectType,
                                       UserMode,
                                       (PVOID*)&FileObject,
                                       NULL);
    if (!NT_SUCCESS(Status))
        return Status;

    Pty = (FileObject->DeviceObject == LxDeviceObject) ? FileObject->FsContext : NULL;
    if ((Pty == NULL) || Pty->Closed || (Pty->Tty != NULL))
    {
        ObDereferenceObject(FileObject);
        return STATUS_INVALID_HANDLE;
    }

    Status = LxTtyCreate(Process->Instance,
                         (Boot ? LX_TTY_CONSOLE : 0) | LX_TTY_NT_MASTER,
                         0,
                         In->Rows ? In->Rows : 25,
                         In->Columns ? In->Columns : 80,
                         &Tty);
    if (NT_SUCCESS(Status) && (InterlockedCompareExchangePointer((PVOID volatile *)&Pty->Tty, Tty, NULL) != NULL))
    {
        LxTtyMasterClose(Tty);
        LxTtyDereference(Tty);
        Status = STATUS_INVALID_HANDLE;
    }
    ObDereferenceObject(FileObject);
    if (!NT_SUCCESS(Status))
        return Status;

    if (Boot || (Process->Instance->Console == NULL))
    {
        LxTtyReference(Tty);
        Process->Instance->Console = Tty;
    }
    if (!Boot)
    {
        LxTtyReference(Tty);
        Process->Tty = Tty;
    }

    for (Fd = 0; Fd < 3; Fd++)
    {
        PLX_FILE File = NULL;
        PSTR Path;

        if (!(In->TtyMask & (1UL << Fd)))
            continue;

        if (!Boot)
        {
            Path = LxStrPrintf("/dev/pts/%ld", Tty->Index);
            if (Path != NULL)
            {
                if (LxVfsOpenResolve(Process, LX_AT_FDCWD, Path, LX_O_RDWR | LX_O_NOCTTY, 0, 0, &File) != 0)
                    File = NULL;
                LxFree(Path);
            }
        }
        if (File == NULL)
        {
            File = LxTtyOpen(Tty, LX_O_RDWR);
            if (File == NULL)
                return STATUS_INSUFFICIENT_RESOURCES;
            File->Rdev = Boot ? ((5 << 8) | 1) : LxTtyDevice(Tty);
            File->Path = Boot ? LxStrDup("/dev/console") : LxStrPrintf("/dev/pts/%ld", Tty->Index);
        }
        Status = LxInstallFile(Process, File, Fd);
        if (!NT_SUCCESS(Status))
            return Status;
    }
    return STATUS_SUCCESS;
}

static
NTSTATUS
LxIoctlCreateProcess(
    _Inout_ PIRP Irp,
    _In_ PIO_STACK_LOCATION Stack)
{
    PLX_CREATE_PROCESS_IN In = Irp->AssociatedIrp.SystemBuffer;
    PLX_CREATE_PROCESS_OUT Out = Irp->AssociatedIrp.SystemBuffer;
    ULONG InLength = Stack->Parameters.DeviceIoControl.InputBufferLength;
    ULONG OutLength = Stack->Parameters.DeviceIoControl.OutputBufferLength;
    PUCHAR Buffer = (PUCHAR)In;
    PUCHAR Root, Cwd, FileName, Args, Env;
    PLX_EXEC_REQUEST Request = NULL;
    PLX_PROCESS Process = NULL;
    PLX_THREAD Thread = NULL;
    PLX_INSTANCE Instance = NULL;
    UNICODE_STRING RootNt;
    HANDLE ProcessHandle = NULL, ThreadHandle = NULL, UserHandle = NULL;
    PEPROCESS EProcess = NULL;
    CONTEXT Context;
    BOOLEAN Listed = FALSE;
    NTSTATUS Status;
    LONG Fd;

    if ((InLength < sizeof(*In)) || (OutLength < sizeof(*Out)) ||
        (In->Size != InLength) || (In->Version != LX_CREATE_PROCESS_VERSION))
    {
        return STATUS_INVALID_PARAMETER;
    }

    Status = LxCaptureRange(Buffer, InLength, In->RootOffset, In->RootLength, 32767 * sizeof(WCHAR), &Root);
    if (NT_SUCCESS(Status))
        Status = LxCaptureRange(Buffer, InLength, In->CwdOffset, In->CwdLength, LX_PATH_MAX - 1, &Cwd);
    if (NT_SUCCESS(Status))
        Status = LxCaptureRange(Buffer, InLength, In->FileOffset, In->FileLength, LX_PATH_MAX - 1, &FileName);
    if (NT_SUCCESS(Status))
        Status = LxCaptureRange(Buffer, InLength, In->ArgOffset, In->ArgLength, LX_MAX_ARG_TOTAL, &Args);
    if (NT_SUCCESS(Status))
        Status = LxCaptureRange(Buffer, InLength, In->EnvOffset, In->EnvLength, LX_MAX_ARG_TOTAL, &Env);
    if (!NT_SUCCESS(Status))
        return Status;

    if ((In->RootLength < 2 * sizeof(WCHAR)) || (In->RootLength & 1) ||
        (In->FileLength == 0) || (In->ArgCount == 0))
    {
        return STATUS_INVALID_PARAMETER;
    }

    if (In->Flags & ~(LX_CREATE_BOOT | LX_CREATE_NEW_INSTANCE | LX_CREATE_TRACE | LX_CREATE_TRACE_ALL | LX_CREATE_JOIN |
                      LX_CREATE_USER | LX_CREATE_SEARCH_PATH | LX_CREATE_RUNTIME_DIR))
        return STATUS_INVALID_PARAMETER;
    if (((In->Flags & LX_CREATE_RUNTIME_DIR) && !(In->Flags & LX_CREATE_USER)) ||
        (In->GroupCount > LX_CREATE_MAX_GROUPS))
        return STATUS_INVALID_PARAMETER;
    if ((In->Flags & LX_CREATE_JOIN) && (In->Flags & (LX_CREATE_BOOT | LX_CREATE_NEW_INSTANCE)))
        return STATUS_INVALID_PARAMETER;

    RootNt.Buffer = (PWCHAR)Root;
    RootNt.Length = (USHORT)In->RootLength;
    while ((RootNt.Length > 2 * sizeof(WCHAR)) && (RootNt.Buffer[RootNt.Length / sizeof(WCHAR) - 1] == L'\\'))
        RootNt.Length -= sizeof(WCHAR);
    RootNt.MaximumLength = RootNt.Length;

    KeWaitForSingleObject(&LxInstanceCreateLock, Executive, KernelMode, FALSE, NULL);
    Instance = LxInstanceFind(&RootNt);
    if ((Instance != NULL) && (Instance->ShuttingDown || (In->Flags & LX_CREATE_NEW_INSTANCE)))
    {
        BOOLEAN Busy = !Instance->ShuttingDown;

        LxInstanceDereference(Instance);
        Instance = NULL;
        if (Busy && (In->Flags & LX_CREATE_BOOT))
        {
            KeSetEvent(&LxInstanceCreateLock, IO_NO_INCREMENT, FALSE);
            return STATUS_OBJECT_NAME_COLLISION;
        }
    }
    if ((Instance == NULL) && (In->Flags & LX_CREATE_JOIN))
    {
        KeSetEvent(&LxInstanceCreateLock, IO_NO_INCREMENT, FALSE);
        return STATUS_NOT_FOUND;
    }
    if (Instance == NULL)
        Instance = LxInstanceCreate(&RootNt, (In->Flags & LX_CREATE_BOOT) != 0);
    KeSetEvent(&LxInstanceCreateLock, IO_NO_INCREMENT, FALSE);
    if (Instance == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;

    Request = LxAlloc(sizeof(*Request));
    Process = LxProcessCreate(Instance);
    LxInstanceDereference(Instance);
    if (Process != NULL)
        Process->Trace = (In->Flags & LX_CREATE_TRACE_ALL) ? 2 : ((In->Flags & LX_CREATE_TRACE) ? 1 : 0);
    if ((Request == NULL) || (Process == NULL))
    {
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto Cleanup;
    }
    KeInitializeEvent(&Request->Done, NotificationEvent, FALSE);

    Request->FileName = LxCaptureUtf8(FileName, In->FileLength);
    Request->User = (In->Flags & LX_CREATE_USER) != 0;
    Request->RuntimeDir = (In->Flags & LX_CREATE_RUNTIME_DIR) != 0;
    Request->SearchPath = (In->Flags & LX_CREATE_SEARCH_PATH) != 0;
    Request->Uid = In->Uid;
    Request->Gid = In->Gid;
    Request->GroupCount = In->GroupCount;
    RtlCopyMemory(Request->Groups, In->Groups, In->GroupCount * sizeof(ULONG));
    Request->ArgLength = In->ArgLength;
    Request->ArgCount = In->ArgCount;
    Request->EnvLength = In->EnvLength;
    Request->EnvCount = In->EnvCount;
    Status = LxCaptureStringList(Args, In->ArgLength, In->ArgCount, &Request->Args);
    if (NT_SUCCESS(Status))
        Status = LxCaptureStringList(Env, In->EnvLength, In->EnvCount, &Request->Env);
    if (NT_SUCCESS(Status) && (Request->FileName == NULL))
        Status = STATUS_INSUFFICIENT_RESOURCES;
    if (!NT_SUCCESS(Status))
        goto Cleanup;

    if ((In->CwdLength != 0) && (Cwd[0] == '/'))
        Process->Cwd = LxCaptureUtf8(Cwd, In->CwdLength);
    else
        Process->Cwd = LxStrDup("/");
    if (Process->Cwd == NULL)
    {
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto Cleanup;
    }
    Process->Pid = LxInstanceAllocatePid(Process->Instance);
    Process->NsPid[0] = Process->Pid;
    Process->Pgid = Process->Pid;
    Process->Sid = Process->Pid;
    LxCgroupAttach(Process, Process->Instance->RootCgroup);

    if ((In->TtyMask & ~7UL) || ((In->TtyMask != 0) && (In->Pty == NULL)))
    {
        Status = STATUS_INVALID_PARAMETER;
        goto Cleanup;
    }
    if (In->TtyMask != 0)
    {
        Status = LxInstallTty(Process, In);
        if (!NT_SUCCESS(Status))
            goto Cleanup;
    }

    for (Fd = 0; Fd < 3; Fd++)
    {
        if ((In->StdHandles[Fd] != NULL) && !(In->TtyMask & (1UL << Fd)))
        {
            Status = LxInstallStdHandle(Process, In->StdHandles[Fd], Fd);
            if (!NT_SUCCESS(Status))
                goto Cleanup;
        }
    }

    Thread = LxThreadCreate(Process);
    if (Thread == NULL)
    {
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto Cleanup;
    }
    Thread->Exec = Request;

    Status = PsPicoCreateProcess(FALSE, Process, &ProcessHandle);
    if (!NT_SUCCESS(Status))
        goto Cleanup;

    Status = ObReferenceObjectByHandle(ProcessHandle,
                                       0,
                                       *PsProcessType,
                                       KernelMode,
                                       (PVOID*)&EProcess,
                                       NULL);
    if (!NT_SUCCESS(Status))
        goto Cleanup;

    Process->NtPid = HandleToUlong(PsGetProcessId(EProcess));
    Process->ParentPid = 0;
    Thread->Tid = Process->Pid;
    Thread->NsTid[0] = Process->Pid;
    if (Process->Tty != NULL)
    {
        Process->Tty->Session = Process->Pid;
        Process->Tty->ForegroundPgrp = Process->Pid;
    }
    else if ((Process->Instance->Console != NULL) && (In->Flags & LX_CREATE_BOOT))
    {
        Process->Instance->Console->Session = 0;
        Process->Instance->Console->ForegroundPgrp = 0;
    }

    LxProcessRegister(Process);
    InterlockedIncrement((PLONG)&Process->Instance->ProcessCount);
    Listed = TRUE;

    RtlZeroMemory(&Context, sizeof(Context));
    Context.ContextFlags = CONTEXT_FULL;
    Status = PsPicoCreateThread(ProcessHandle, &Context, Thread, &ThreadHandle, NULL);
    if (!NT_SUCCESS(Status))
        goto Cleanup;
    Thread = NULL;

    KeWaitForSingleObject(&Request->Done, Executive, KernelMode, FALSE, NULL);
    Status = Request->Status;
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("Linux program '%s' could not start: status 0x%08lx, errno %ld\n",
                Request->FileName, Status, Request->Errno);
        goto Done;
    }

    Status = ObOpenObjectByPointer(EProcess,
                                   0,
                                   NULL,
                                   SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_TERMINATE,
                                   *PsProcessType,
                                   UserMode,
                                   &UserHandle);
    if (!NT_SUCCESS(Status))
        goto Done;

    Out->ProcessHandle = UserHandle;
    Out->ProcessId = HandleToUlong(PsGetProcessId(EProcess));
    Out->Reserved = 0;
    Irp->IoStatus.Information = sizeof(*Out);

Done:
    ZwClose(ThreadHandle);
    ZwClose(ProcessHandle);
    ObDereferenceObject(EProcess);
    LxFreeRequest(Request);
    return Status;

Cleanup:
    if (Listed)
    {
        LxProcessUnregister(Process);
        InterlockedDecrement((PLONG)&Process->Instance->ProcessCount);
    }
    if (ProcessHandle != NULL)
    {
        ZwTerminateProcess(ProcessHandle, Status);
        ZwClose(ProcessHandle);
    }
    if (EProcess != NULL)
        ObDereferenceObject(EProcess);
    if (Thread != NULL)
        LxThreadFree(Thread);
    if (Process != NULL)
        LxProcessDereference(Process);
    if (Request != NULL)
        LxFreeRequest(Request);
    return Status;
}

static
NTSTATUS
LxCompleteIrp(
    _Inout_ PIRP Irp,
    _In_ NTSTATUS Status,
    _In_ ULONG_PTR Information)
{
    Irp->IoStatus.Status = Status;
    Irp->IoStatus.Information = Information;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return Status;
}

static
NTSTATUS
NTAPI
LxDispatchCreate(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    PIO_STACK_LOCATION Stack = IoGetCurrentIrpStackLocation(Irp);
    UNICODE_STRING PtyName = RTL_CONSTANT_STRING(LX_PTY_FILE_NAME);
    UNICODE_STRING WindowName = RTL_CONSTANT_STRING(LX_WIN_FILE_NAME);
    PFILE_OBJECT FileObject = Stack->FileObject;
    PLX_NT_PTY Pty;

    UNREFERENCED_PARAMETER(DeviceObject);

    if (FileObject->FileName.Length == 0)
        return LxCompleteIrp(Irp, STATUS_SUCCESS, 0);
    if (RtlEqualUnicodeString(&FileObject->FileName, &WindowName, TRUE))
        return LxCompleteIrp(Irp, LxWinHostCreate(FileObject), FILE_OPENED);
    if (!RtlEqualUnicodeString(&FileObject->FileName, &PtyName, TRUE))
        return LxCompleteIrp(Irp, STATUS_OBJECT_NAME_NOT_FOUND, 0);

    Pty = LxAlloc(sizeof(*Pty));
    if (Pty == NULL)
        return LxCompleteIrp(Irp, STATUS_INSUFFICIENT_RESOURCES, 0);
    FileObject->FsContext = Pty;
    return LxCompleteIrp(Irp, STATUS_SUCCESS, FILE_OPENED);
}

static
NTSTATUS
NTAPI
LxDispatchCleanup(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    PLX_NT_PTY Pty = IoGetCurrentIrpStackLocation(Irp)->FileObject->FsContext;
    PLX_TTY Tty;

    UNREFERENCED_PARAMETER(DeviceObject);

    LxWinHostCleanup(IoGetCurrentIrpStackLocation(Irp)->FileObject);
    if (Pty != NULL)
    {
        InterlockedExchange(&Pty->Closed, TRUE);
        Tty = InterlockedCompareExchangePointer((PVOID volatile *)&Pty->Tty, NULL, NULL);
        if (Tty != NULL)
            LxTtyMasterClose(Tty);
    }
    return LxCompleteIrp(Irp, STATUS_SUCCESS, 0);
}

static
NTSTATUS
NTAPI
LxDispatchClose(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    PFILE_OBJECT FileObject = IoGetCurrentIrpStackLocation(Irp)->FileObject;
    PLX_NT_PTY Pty = FileObject->FsContext;

    UNREFERENCED_PARAMETER(DeviceObject);

    LxWinHostClose(FileObject);
    if (Pty != NULL)
    {
        FileObject->FsContext = NULL;
        if (Pty->Tty != NULL)
            LxTtyDereference(Pty->Tty);
        LxFree(Pty);
    }
    return LxCompleteIrp(Irp, STATUS_SUCCESS, 0);
}

static
NTSTATUS
LxPtyStatus(
    _In_ LONG64 Result)
{
    if (Result >= 0)
        return STATUS_SUCCESS;
    if (Result == -LX_EIO)
        return STATUS_PIPE_BROKEN;
    if (Result == -LX_EINTR)
        return STATUS_CANCELLED;
    return STATUS_UNSUCCESSFUL;
}

static
NTSTATUS
NTAPI
LxDispatchReadWrite(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    PIO_STACK_LOCATION Stack = IoGetCurrentIrpStackLocation(Irp);
    PLX_NT_PTY Pty = Stack->FileObject->FsContext;
    PLX_TTY Tty = (Pty != NULL) ? Pty->Tty : NULL;
    LONG64 Result;

    UNREFERENCED_PARAMETER(DeviceObject);

    if (Tty == NULL)
        return LxCompleteIrp(Irp, STATUS_INVALID_DEVICE_REQUEST, 0);

    if (Stack->MajorFunction == IRP_MJ_READ)
        Result = LxTtyMasterRead(Tty, Irp->AssociatedIrp.SystemBuffer, Stack->Parameters.Read.Length, FALSE);
    else
        Result = LxTtyMasterWrite(Tty, Irp->AssociatedIrp.SystemBuffer, Stack->Parameters.Write.Length, FALSE);

    return LxCompleteIrp(Irp, LxPtyStatus(Result), (Result > 0) ? (ULONG_PTR)Result : 0);
}

static
NTSTATUS
LxIoctlPtyResize(
    _Inout_ PIRP Irp,
    _In_ PIO_STACK_LOCATION Stack)
{
    PLX_NT_PTY Pty = Stack->FileObject->FsContext;
    PLX_PTY_SIZE Size = Irp->AssociatedIrp.SystemBuffer;

    if ((Pty == NULL) || (Pty->Tty == NULL))
        return STATUS_INVALID_DEVICE_REQUEST;
    if (Stack->Parameters.DeviceIoControl.InputBufferLength < sizeof(*Size))
        return STATUS_INVALID_PARAMETER;
    LxTtySetWinSize(Pty->Tty, Size->Rows, Size->Columns);
    return STATUS_SUCCESS;
}

static
NTSTATUS
NTAPI
LxDispatchDeviceControl(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    PIO_STACK_LOCATION Stack = IoGetCurrentIrpStackLocation(Irp);
    NTSTATUS Status;

    UNREFERENCED_PARAMETER(DeviceObject);

    if (LxWinHostIoctl(Stack->FileObject, Irp, Stack, &Status))
        return Status;

    Irp->IoStatus.Information = 0;
    switch (Stack->Parameters.DeviceIoControl.IoControlCode)
    {
        case IOCTL_LX_CREATE_PROCESS:
            Status = LxIoctlCreateProcess(Irp, Stack);
            break;

        case IOCTL_LX_PTY_RESIZE:
            Status = LxIoctlPtyResize(Irp, Stack);
            break;

        default:
            Status = STATUS_INVALID_DEVICE_REQUEST;
            break;
    }

    Irp->IoStatus.Status = Status;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return Status;
}

NTSTATUS
NTAPI
DriverEntry(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PUNICODE_STRING RegistryPath)
{
    static const PICO_PROVIDER_ROUTINES Routines =
    {
        sizeof(PICO_PROVIDER_ROUTINES),
        LxPicoSystemCall,
        LxPicoDispatchException,
        LxPicoThreadStart,
        LxPicoThreadExit,
        LxPicoProcessExit
    };
    UNICODE_STRING DeviceName = RTL_CONSTANT_STRING(LX_DEVICE_NAME);
    UNICODE_STRING DosName = RTL_CONSTANT_STRING(LX_DOS_DEVICE_NAME);
    PDEVICE_OBJECT DeviceObject;
    NTSTATUS Status;

    UNREFERENCED_PARAMETER(RegistryPath);

    LxProcessInitialize();
    InitializeListHead(&LxInstanceList);
    ExInitializeFastMutex(&LxInstanceListLock);
    KeInitializeEvent(&LxInstanceCreateLock, SynchronizationEvent, TRUE);
    LxPipeInitialize();
    LxVcacheInitialize();
    LxSynthInitialize();
    LxSysDevInitialize();
    LxFdObjectsInitialize();
    LxSocketInitialize();

    Status = IoCreateDevice(DriverObject,
                            0,
                            &DeviceName,
                            FILE_DEVICE_UNKNOWN,
                            FILE_DEVICE_SECURE_OPEN,
                            FALSE,
                            &DeviceObject);
    if (!NT_SUCCESS(Status))
        return Status;

    Status = IoCreateSymbolicLink(&DosName, &DeviceName);
    if (!NT_SUCCESS(Status))
    {
        IoDeleteDevice(DeviceObject);
        return Status;
    }

    Status = PsPicoRegisterProvider(&Routines);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("The Linux subsystem could not register as the pico provider: 0x%08lx\n", Status);
        IoDeleteSymbolicLink(&DosName);
        IoDeleteDevice(DeviceObject);
        return Status;
    }

    LxDeviceObject = DeviceObject;
    DriverObject->MajorFunction[IRP_MJ_CREATE] = LxDispatchCreate;
    DriverObject->MajorFunction[IRP_MJ_CLEANUP] = LxDispatchCleanup;
    DriverObject->MajorFunction[IRP_MJ_CLOSE] = LxDispatchClose;
    DriverObject->MajorFunction[IRP_MJ_READ] = LxDispatchReadWrite;
    DriverObject->MajorFunction[IRP_MJ_WRITE] = LxDispatchReadWrite;
    DriverObject->MajorFunction[IRP_MJ_DEVICE_CONTROL] = LxDispatchDeviceControl;
    DeviceObject->Flags |= DO_BUFFERED_IO;
    DeviceObject->Flags &= ~DO_DEVICE_INITIALIZING;

    return STATUS_SUCCESS;
}
