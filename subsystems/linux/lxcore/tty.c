/*
 * PROJECT:     LiberNT Linux Subsystem
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Terminal line discipline for Linux programs on a console
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "lxcore.h"

#define NDEBUG
#include <debug.h>

#define LX_TTY_ECHO_MAX 2048

static KSTART_ROUTINE LxTtyInputThread;

static
VOID
LxTtyLock(
    _In_ PLX_TTY Tty)
{
    KeWaitForSingleObject(&Tty->Lock, Executive, KernelMode, FALSE, NULL);
}

static
VOID
LxTtyUnlock(
    _In_ PLX_TTY Tty)
{
    KeSetEvent(&Tty->Lock, IO_NO_INCREMENT, FALSE);
}

VOID
LxTtyReference(
    _In_ PLX_TTY Tty)
{
    InterlockedIncrement(&Tty->RefCount);
}

VOID
LxTtyDereference(
    _In_ PLX_TTY Tty)
{
    if (InterlockedDecrement(&Tty->RefCount) != 0)
        return;

    if (Tty->Input != NULL)
        ZwClose(Tty->Input);
    if (Tty->Output != NULL)
        ZwClose(Tty->Output);
    LxFree(Tty);
}

NTSTATUS
LxTtyCreate(
    _In_ HANDLE Input,
    _In_ HANDLE Output,
    _In_ USHORT Rows,
    _In_ USHORT Columns,
    _Out_ PLX_TTY *Result)
{
    OBJECT_ATTRIBUTES ObjectAttributes;
    PLX_TTY Tty;
    HANDLE Thread;
    NTSTATUS Status;

    *Result = NULL;
    Tty = LxAlloc(sizeof(*Tty));
    if (Tty == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;

    Tty->RefCount = 2;
    KeInitializeEvent(&Tty->Lock, SynchronizationEvent, TRUE);
    KeInitializeEvent(&Tty->ReadLock, SynchronizationEvent, TRUE);
    KeInitializeEvent(&Tty->DataReady, NotificationEvent, FALSE);
    Tty->Input = Input;
    Tty->Output = Output;
    LxPollHeadInit(&Tty->Poll);

    Tty->Termios.Iflag = LX_ICRNL | LX_IXON;
    Tty->Termios.Oflag = LX_OPOST | LX_ONLCR;
    Tty->Termios.Cflag = LX_B38400 | LX_CS8 | LX_CREAD | LX_HUPCL;
    Tty->Termios.Lflag = LX_ISIG | LX_ICANON | LX_ECHO | LX_ECHOE | LX_ECHOK |
                         LX_IEXTEN | LX_ECHOCTL | LX_ECHOKE;
    Tty->Termios.Cc[LX_VINTR] = 0x03;
    Tty->Termios.Cc[LX_VQUIT] = 0x1C;
    Tty->Termios.Cc[LX_VERASE] = 0x7F;
    Tty->Termios.Cc[LX_VKILL] = 0x15;
    Tty->Termios.Cc[LX_VEOF] = 0x04;
    Tty->Termios.Cc[LX_VMIN] = 1;
    Tty->Termios.Cc[LX_VSTART] = 0x11;
    Tty->Termios.Cc[LX_VSTOP] = 0x13;
    Tty->Termios.Cc[LX_VSUSP] = 0x1A;
    Tty->Termios.Cc[LX_VREPRINT] = 0x12;
    Tty->Termios.Cc[LX_VDISCARD] = 0x0F;
    Tty->Termios.Cc[LX_VWERASE] = 0x17;
    Tty->Termios.Cc[LX_VLNEXT] = 0x16;
    Tty->WinSize.Rows = Rows ? Rows : 25;
    Tty->WinSize.Cols = Columns ? Columns : 80;

    InitializeObjectAttributes(&ObjectAttributes, NULL, OBJ_KERNEL_HANDLE, NULL, NULL);
    Status = PsCreateSystemThread(&Thread, THREAD_ALL_ACCESS, &ObjectAttributes, NULL, NULL, LxTtyInputThread, Tty);
    if (!NT_SUCCESS(Status))
    {
        Tty->Input = NULL;
        Tty->Output = NULL;
        LxFree(Tty);
        return Status;
    }
    ZwClose(Thread);

    *Result = Tty;
    return STATUS_SUCCESS;
}

static
VOID
LxTtyEchoByte(
    _In_ PLX_TTY Tty,
    _In_ UCHAR Byte,
    _Inout_updates_(LX_TTY_ECHO_MAX) PUCHAR Echo,
    _Inout_ PULONG EchoLength)
{
    if (*EchoLength + 2 > LX_TTY_ECHO_MAX)
        return;

    if ((Tty->Termios.Lflag & LX_ECHOCTL) && (Byte < 0x20) && (Byte != '\n') && (Byte != '\t'))
    {
        Echo[(*EchoLength)++] = '^';
        Echo[(*EchoLength)++] = Byte + 0x40;
    }
    else
    {
        Echo[(*EchoLength)++] = Byte;
    }
}

static
VOID
LxTtyEchoErase(
    _Inout_updates_(LX_TTY_ECHO_MAX) PUCHAR Echo,
    _Inout_ PULONG EchoLength)
{
    if (*EchoLength + 3 > LX_TTY_ECHO_MAX)
        return;
    Echo[(*EchoLength)++] = '\b';
    Echo[(*EchoLength)++] = ' ';
    Echo[(*EchoLength)++] = '\b';
}

static
VOID
LxTtyCommitLine(
    _In_ PLX_TTY Tty)
{
    if (Tty->LineLength == 0)
        Tty->EofPending = TRUE;
    if (Tty->ReadyLength + Tty->LineLength <= LX_TTY_BUFFER)
    {
        RtlCopyMemory(Tty->Ready + Tty->ReadyLength, Tty->Line, Tty->LineLength);
        Tty->ReadyLength += Tty->LineLength;
    }
    Tty->LineLength = 0;
}

static
VOID
LxTtyInputByte(
    _In_ PLX_TTY Tty,
    _In_ UCHAR Byte,
    _Inout_updates_(LX_TTY_ECHO_MAX) PUCHAR Echo,
    _Inout_ PULONG EchoLength,
    _Inout_ PLONG Signal)
{
    PLX_TERMIOS Termios = &Tty->Termios;
    BOOLEAN DoEcho = (Termios->Lflag & LX_ECHO) != 0;

    if (Byte == '\r')
    {
        if (Termios->Iflag & LX_IGNCR)
            return;
        if (Termios->Iflag & LX_ICRNL)
            Byte = '\n';
    }
    else if ((Byte == '\n') && (Termios->Iflag & LX_INLCR))
    {
        Byte = '\r';
    }

    if ((Termios->Lflag & LX_ISIG) && (Byte != 0))
    {
        LONG Raised = 0;

        if (Byte == Termios->Cc[LX_VINTR])
            Raised = LX_SIGINT;
        else if (Byte == Termios->Cc[LX_VQUIT])
            Raised = LX_SIGQUIT;
        else if (Byte == Termios->Cc[LX_VSUSP])
            Raised = LX_SIGTSTP;

        if (Raised != 0)
        {
            if (!(Termios->Lflag & LX_NOFLSH))
            {
                Tty->LineLength = 0;
                Tty->ReadyLength = 0;
            }
            if (DoEcho)
                LxTtyEchoByte(Tty, Byte, Echo, EchoLength);
            *Signal = Raised;
            return;
        }
    }

    if (Termios->Lflag & LX_ICANON)
    {
        if ((Byte == Termios->Cc[LX_VERASE]) || (Byte == '\b'))
        {
            if (Tty->LineLength != 0)
            {
                Tty->LineLength--;
                if (DoEcho && (Termios->Lflag & LX_ECHOE))
                    LxTtyEchoErase(Echo, EchoLength);
            }
            return;
        }

        if (Byte == Termios->Cc[LX_VKILL])
        {
            while (Tty->LineLength != 0)
            {
                Tty->LineLength--;
                if (DoEcho && (Termios->Lflag & LX_ECHOKE))
                    LxTtyEchoErase(Echo, EchoLength);
            }
            return;
        }

        if ((Termios->Lflag & LX_IEXTEN) && (Byte == Termios->Cc[LX_VWERASE]))
        {
            while ((Tty->LineLength != 0) && (Tty->Line[Tty->LineLength - 1] == ' '))
            {
                Tty->LineLength--;
                if (DoEcho)
                    LxTtyEchoErase(Echo, EchoLength);
            }
            while ((Tty->LineLength != 0) && (Tty->Line[Tty->LineLength - 1] != ' '))
            {
                Tty->LineLength--;
                if (DoEcho)
                    LxTtyEchoErase(Echo, EchoLength);
            }
            return;
        }

        if (Byte == Termios->Cc[LX_VEOF])
        {
            LxTtyCommitLine(Tty);
            return;
        }

        if (Tty->LineLength < LX_TTY_BUFFER - 1)
            Tty->Line[Tty->LineLength++] = Byte;

        if (DoEcho)
            LxTtyEchoByte(Tty, Byte, Echo, EchoLength);
        else if ((Byte == '\n') && (Termios->Lflag & LX_ECHONL))
            LxTtyEchoByte(Tty, Byte, Echo, EchoLength);

        if ((Byte == '\n') ||
            ((Termios->Cc[LX_VEOL] != 0) && (Byte == Termios->Cc[LX_VEOL])) ||
            ((Termios->Cc[LX_VEOL2] != 0) && (Byte == Termios->Cc[LX_VEOL2])))
        {
            LxTtyCommitLine(Tty);
        }
        return;
    }

    if (Tty->ReadyLength < LX_TTY_BUFFER)
        Tty->Ready[Tty->ReadyLength++] = Byte;
    if (DoEcho)
        LxTtyEchoByte(Tty, Byte, Echo, EchoLength);
}

static
VOID
NTAPI
LxTtyInputThread(
    _In_ PVOID Context)
{
    PLX_TTY Tty = Context;
    UCHAR Raw[256];
    PUCHAR Echo;
    IO_STATUS_BLOCK Iosb;
    NTSTATUS Status;

    Echo = LxAllocPaged(LX_TTY_ECHO_MAX);

    while (Echo != NULL)
    {
        ULONG EchoLength = 0, Index;
        LONG Signal = 0;
        ULONG Foreground;

        Status = ZwReadFile(Tty->Input, NULL, NULL, NULL, &Iosb, Raw, sizeof(Raw), NULL, NULL);
        if (!NT_SUCCESS(Status) || (Iosb.Information == 0))
            break;

        LxTtyLock(Tty);
        for (Index = 0; Index < Iosb.Information; Index++)
            LxTtyInputByte(Tty, Raw[Index], Echo, &EchoLength, &Signal);
        if ((Tty->ReadyLength != 0) || Tty->EofPending)
            KeSetEvent(&Tty->DataReady, IO_NO_INCREMENT, FALSE);
        Foreground = Tty->ForegroundPgrp;
        LxTtyUnlock(Tty);

        if (EchoLength != 0)
            ZwWriteFile(Tty->Output, NULL, NULL, NULL, &Iosb, Echo, EchoLength, NULL, NULL);
        LxPollWake(&Tty->Poll, LX_POLLIN | LX_POLLRDNORM);
        if ((Signal != 0) && (Foreground != 0) && (Tty->Instance != NULL))
            LxSignalGroup(Tty->Instance, Foreground, Signal);
    }

    LxTtyLock(Tty);
    Tty->InputClosed = TRUE;
    KeSetEvent(&Tty->DataReady, IO_NO_INCREMENT, FALSE);
    LxTtyUnlock(Tty);
    LxPollWake(&Tty->Poll, LX_POLLIN | LX_POLLHUP);

    LxFreePaged(Echo);
    LxTtyDereference(Tty);
    PsTerminateSystemThread(STATUS_SUCCESS);
}

BOOLEAN
LxTtyReadable(
    _In_ PLX_TTY Tty)
{
    BOOLEAN Readable;

    LxTtyLock(Tty);
    Readable = (Tty->ReadyLength != 0) || Tty->EofPending || Tty->InputClosed;
    LxTtyUnlock(Tty);
    return Readable;
}

LONG64
LxTtyRead(
    _In_ PLX_TTY Tty,
    _In_ ULONG64 Buffer,
    _In_ ULONG64 Length,
    _In_ BOOLEAN NonBlocking)
{
    PUCHAR Data;
    LONG64 Result = 0;
    NTSTATUS Status;

    if (Length == 0)
        return 0;

    Data = LxAllocPaged(LX_TTY_BUFFER);
    if (Data == NULL)
        return -LX_ENOMEM;

    Status = KeWaitForSingleObject(&Tty->ReadLock, UserRequest, UserMode, TRUE, NULL);
    if (Status != STATUS_SUCCESS)
    {
        LxFreePaged(Data);
        return -LX_EINTR;
    }

    for (;;)
    {
        ULONG Count;

        LxTtyLock(Tty);
        if (Tty->ReadyLength != 0)
        {
            Count = (ULONG)min(Length, Tty->ReadyLength);
            if (Tty->Termios.Lflag & LX_ICANON)
            {
                ULONG Index;

                for (Index = 0; Index < Count; Index++)
                {
                    if (Tty->Ready[Index] == '\n')
                    {
                        Count = Index + 1;
                        break;
                    }
                }
            }
            RtlCopyMemory(Data, Tty->Ready, Count);
            RtlMoveMemory(Tty->Ready, Tty->Ready + Count, Tty->ReadyLength - Count);
            Tty->ReadyLength -= Count;
            if ((Tty->ReadyLength == 0) && !Tty->EofPending && !Tty->InputClosed)
                KeClearEvent(&Tty->DataReady);
            LxTtyUnlock(Tty);
            Result = LxCopyToUser(Buffer, Data, Count);
            if (Result == 0)
                Result = Count;
            break;
        }

        if (Tty->EofPending || Tty->InputClosed)
        {
            Tty->EofPending = FALSE;
            if (!Tty->InputClosed)
                KeClearEvent(&Tty->DataReady);
            LxTtyUnlock(Tty);
            Result = 0;
            break;
        }

        KeClearEvent(&Tty->DataReady);
        LxTtyUnlock(Tty);

        if (NonBlocking)
        {
            Result = -LX_EAGAIN;
            break;
        }

        Status = KeWaitForSingleObject(&Tty->DataReady, UserRequest, UserMode, TRUE, NULL);
        if (Status != STATUS_SUCCESS)
        {
            Result = -LX_EINTR;
            break;
        }
    }

    KeSetEvent(&Tty->ReadLock, IO_NO_INCREMENT, FALSE);
    LxFreePaged(Data);
    return Result;
}

LONG64
LxTtyWrite(
    _In_ PLX_TTY Tty,
    _In_ ULONG64 Buffer,
    _In_ ULONG64 Length)
{
    UCHAR Data[1024];
    IO_STATUS_BLOCK Iosb;
    ULONG64 Total = 0;
    NTSTATUS Status;
    LONG64 Error;

    while (Total < Length)
    {
        ULONG Chunk = (ULONG)min(Length - Total, sizeof(Data));

        Error = LxCopyFromUser(Data, Buffer + Total, Chunk);
        if (Error != 0)
            return (Total != 0) ? (LONG64)Total : Error;

        Status = ZwWriteFile(Tty->Output, NULL, NULL, NULL, &Iosb, Data, Chunk, NULL, NULL);
        if (!NT_SUCCESS(Status))
            return (Total != 0) ? (LONG64)Total : -LxStatusToErrno(Status);
        Total += Iosb.Information;
        if (Iosb.Information < Chunk)
            break;
    }
    return (LONG64)Total;
}

LONG64
LxTtyIoctl(
    _In_ PLX_PROCESS Process,
    _In_ PLX_TTY Tty,
    _In_ ULONG Request,
    _In_ ULONG64 Argument)
{
    LX_TERMIOS Termios;
    LX_WINSIZE WinSize;
    LONG Value;
    LONG64 Result;

    switch (Request)
    {
        case LX_TCGETS:
        case LX_TCGETS2:
            LxTtyLock(Tty);
            Termios = Tty->Termios;
            LxTtyUnlock(Tty);
            Result = LxCopyToUser(Argument, &Termios, sizeof(Termios));
            if ((Result == 0) && (Request == LX_TCGETS2))
            {
                ULONG Speeds[2] = { 38400, 38400 };

                Result = LxCopyToUser(Argument + sizeof(Termios), Speeds, sizeof(Speeds));
            }
            return Result;

        case LX_TCSETS:
        case LX_TCSETSW:
        case LX_TCSETSF:
        case LX_TCSETS2:
        case LX_TCSETSW2:
        case LX_TCSETSF2:
            Result = LxCopyFromUser(&Termios, Argument, sizeof(Termios));
            if (Result != 0)
                return Result;
            LxTtyLock(Tty);
            if ((Request == LX_TCSETSF) || (Request == LX_TCSETSF2))
            {
                Tty->LineLength = 0;
                Tty->ReadyLength = 0;
            }
            else if ((Tty->Termios.Lflag & LX_ICANON) && !(Termios.Lflag & LX_ICANON) && (Tty->LineLength != 0))
            {
                LxTtyCommitLine(Tty);
                KeSetEvent(&Tty->DataReady, IO_NO_INCREMENT, FALSE);
            }
            Tty->Termios = Termios;
            LxTtyUnlock(Tty);
            return 0;

        case LX_TIOCGWINSZ:
            LxTtyLock(Tty);
            WinSize = Tty->WinSize;
            LxTtyUnlock(Tty);
            return LxCopyToUser(Argument, &WinSize, sizeof(WinSize));

        case LX_TIOCSWINSZ:
            Result = LxCopyFromUser(&WinSize, Argument, sizeof(WinSize));
            if (Result != 0)
                return Result;
            LxTtyLock(Tty);
            Tty->WinSize = WinSize;
            Value = (LONG)Tty->ForegroundPgrp;
            LxTtyUnlock(Tty);
            if ((Value != 0) && (Tty->Instance != NULL))
                LxSignalGroup(Tty->Instance, (ULONG)Value, LX_SIGWINCH);
            return 0;

        case LX_TIOCGPGRP:
            LxTtyLock(Tty);
            Value = (LONG)Tty->ForegroundPgrp;
            LxTtyUnlock(Tty);
            return LxCopyToUser(Argument, &Value, sizeof(Value));

        case LX_TIOCSPGRP:
            Result = LxCopyFromUser(&Value, Argument, sizeof(Value));
            if (Result != 0)
                return Result;
            if (Value <= 0)
                return -LX_EINVAL;
            LxTtyLock(Tty);
            Tty->ForegroundPgrp = (ULONG)Value;
            LxTtyUnlock(Tty);
            return 0;

        case LX_TIOCGSID:
            LxTtyLock(Tty);
            Value = (LONG)Tty->Session;
            LxTtyUnlock(Tty);
            return LxCopyToUser(Argument, &Value, sizeof(Value));

        case LX_TIOCSCTTY:
            LxTtyLock(Tty);
            Tty->Session = Process->Sid;
            Tty->ForegroundPgrp = Process->Pgid;
            LxTtyUnlock(Tty);
            if (Process->Tty == NULL)
            {
                LxTtyReference(Tty);
                Process->Tty = Tty;
            }
            return 0;

        case LX_FIONREAD:
            LxTtyLock(Tty);
            Value = (LONG)Tty->ReadyLength;
            LxTtyUnlock(Tty);
            return LxCopyToUser(Argument, &Value, sizeof(Value));

        case LX_TCFLSH:
            if ((Argument == 0) || (Argument == 2))
            {
                LxTtyLock(Tty);
                Tty->LineLength = 0;
                Tty->ReadyLength = 0;
                LxTtyUnlock(Tty);
            }
            return 0;

        case LX_TIOCOUTQ:
            Value = 0;
            return LxCopyToUser(Argument, &Value, sizeof(Value));

        case LX_TCSBRK:
        case LX_TCXONC:
        case LX_TIOCNOTTY:
        case LX_TIOCVHANGUP:
        case LX_TIOCEXCL:
        case LX_TIOCNXCL:
            return 0;

        case LX_TIOCGPTN:
            Value = 0;
            return LxCopyToUser(Argument, &Value, sizeof(Value));

        default:
            return -LX_ENOTTY;
    }
}

static
LONG64
LxTtyFileRead(
    _In_ PLX_FILE File,
    _In_ ULONG64 Buffer,
    _In_ ULONG64 Length,
    _Inout_opt_ PLONG64 Offset)
{
    UNREFERENCED_PARAMETER(Offset);
    return LxTtyRead(File->Object, Buffer, Length, (File->Flags & LX_O_NONBLOCK) != 0);
}

static
LONG64
LxTtyFileWrite(
    _In_ PLX_FILE File,
    _In_ ULONG64 Buffer,
    _In_ ULONG64 Length,
    _Inout_opt_ PLONG64 Offset)
{
    UNREFERENCED_PARAMETER(Offset);
    return LxTtyWrite(File->Object, Buffer, Length);
}

static
LONG64
LxTtyFileIoctl(
    _In_ PLX_FILE File,
    _In_ ULONG Request,
    _In_ ULONG64 Argument)
{
    PLX_THREAD Thread = LxCurrentThread();

    if (Thread == NULL)
        return -LX_ENOTTY;
    return LxTtyIoctl(Thread->Process, File->Object, Request, Argument);
}

static
ULONG
LxTtyFilePoll(
    _In_ PLX_FILE File,
    _In_opt_ PLX_POLL_ENTRY Entry)
{
    PLX_TTY Tty = File->Object;
    ULONG Mask = LX_POLLOUT | LX_POLLWRNORM;

    if (Entry != NULL)
        LxPollAdd(&Tty->Poll, Entry);
    if (LxTtyReadable(Tty))
        Mask |= LX_POLLIN | LX_POLLRDNORM;
    if (Tty->InputClosed)
        Mask |= LX_POLLHUP;
    return Mask;
}

static
LONG
LxTtyFileStat(
    _In_ PLX_FILE File,
    _Out_ PLX_STAT Stat)
{
    LARGE_INTEGER Now;

    RtlZeroMemory(Stat, sizeof(*Stat));
    Stat->Mode = LX_S_IFCHR | 0620;
    Stat->Nlink = 1;
    Stat->Rdev = File->Rdev ? File->Rdev : (136 << 8);
    Stat->Ino = File->Ino;
    Stat->Dev = 0x16;
    Stat->BlkSize = 1024;
    Stat->Gid = 5;
    KeQuerySystemTime(&Now);
    LxNtTimeToTimespec(Now.QuadPart, &Stat->Mtime);
    Stat->Atime = Stat->Ctime = Stat->Mtime;
    return 0;
}

static
VOID
LxTtyFileRelease(
    _In_ PLX_FILE File)
{
    LxTtyDereference(File->Object);
    File->Object = NULL;
}

const LX_FILE_OPS LxTtyFileOps =
{
    LxTtyFileRead, LxTtyFileWrite, LxTtyFileIoctl, LxTtyFilePoll, LxTtyFileStat, NULL, NULL, NULL, LxTtyFileRelease
};

PLX_FILE
LxTtyOpen(
    _In_ PLX_TTY Tty,
    _In_ ULONG Flags)
{
    PLX_FILE File = LxFileAlloc(&LxTtyFileOps, (Flags & (LX_O_ACCMODE | LX_O_NONBLOCK | LX_O_APPEND)), LX_S_IFCHR);

    if (File == NULL)
        return NULL;
    LxTtyReference(Tty);
    File->Object = Tty;
    File->Mode = 0620;
    return File;
}

PLX_TTY
LxFileGetTty(
    _In_ PLX_FILE File)
{
    return (File->Ops == &LxTtyFileOps) ? File->Object : NULL;
}
