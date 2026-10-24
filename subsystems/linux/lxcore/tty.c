/*
 * PROJECT:     LiberNT Linux Subsystem
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Pseudo-terminals and their line discipline
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "lxcore.h"

#define NDEBUG
#include <debug.h>

#define LX_PTY_MAX          4096
#define LX_PTS_MAJOR        136
#define LX_TTY_GID          5

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

    LxFree(Tty);
}

static
BOOLEAN
LxTtyIndexUsed(
    _In_ PLX_INSTANCE Instance,
    _In_ LONG Index)
{
    PLIST_ENTRY Entry;

    for (Entry = Instance->Ptys.Flink; Entry != &Instance->Ptys; Entry = Entry->Flink)
    {
        if (CONTAINING_RECORD(Entry, LX_TTY, Link)->Index == Index)
            return TRUE;
    }
    return FALSE;
}

NTSTATUS
LxTtyCreate(
    _In_ PLX_INSTANCE Instance,
    _In_ ULONG Flags,
    _In_ ULONG Uid,
    _In_ USHORT Rows,
    _In_ USHORT Columns,
    _Out_ PLX_TTY *Result)
{
    PLX_TTY Tty;
    LONG Index;

    *Result = NULL;
    Tty = LxAlloc(sizeof(*Tty));
    if (Tty == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;

    Tty->RefCount = 1;
    KeInitializeEvent(&Tty->Lock, SynchronizationEvent, TRUE);
    KeInitializeEvent(&Tty->ReadLock, SynchronizationEvent, TRUE);
    KeInitializeEvent(&Tty->DataReady, NotificationEvent, FALSE);
    KeInitializeEvent(&Tty->InputSpace, NotificationEvent, TRUE);
    KeInitializeEvent(&Tty->OutputReady, NotificationEvent, FALSE);
    KeInitializeEvent(&Tty->OutputSpace, NotificationEvent, TRUE);
    LxPollHeadInit(&Tty->Poll);
    LxPollHeadInit(&Tty->MasterPoll);
    InitializeListHead(&Tty->Link);
    Tty->Index = -1;
    Tty->Uid = Uid;
    Tty->Gid = LX_TTY_GID;
    Tty->Locked = (Flags & LX_TTY_LOCKED) != 0;
    Tty->NtMaster = (Flags & LX_TTY_NT_MASTER) != 0;

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
    Tty->WinSize.Rows = Rows;
    Tty->WinSize.Cols = Columns;

    if (!(Flags & LX_TTY_CONSOLE))
    {
        ExAcquireFastMutex(&Instance->Lock);
        for (Index = 0; Index < LX_PTY_MAX; Index++)
        {
            if (!LxTtyIndexUsed(Instance, Index))
                break;
        }
        if (Index == LX_PTY_MAX)
        {
            ExReleaseFastMutex(&Instance->Lock);
            LxFree(Tty);
            return STATUS_TOO_MANY_OPENED_FILES;
        }
        Tty->Index = Index;
        InsertTailList(&Instance->Ptys, &Tty->Link);
        ExReleaseFastMutex(&Instance->Lock);
    }

    LxInstanceReference(Instance);
    Tty->Instance = Instance;
    *Result = Tty;
    return STATUS_SUCCESS;
}

ULONG64
LxTtyDevice(
    _In_ PLX_TTY Tty)
{
    ULONG64 Minor;

    if (Tty->Index < 0)
        return (5 << 8) | 1;
    Minor = (ULONG)Tty->Index;
    return (Minor & 0xFF) | (LX_PTS_MAJOR << 8) | ((Minor & ~0xFFULL) << 12);
}

PLX_TTY
LxTtyLookup(
    _In_ PLX_INSTANCE Instance,
    _In_ ULONG Index)
{
    PLIST_ENTRY Entry;
    PLX_TTY Found = NULL;

    ExAcquireFastMutex(&Instance->Lock);
    for (Entry = Instance->Ptys.Flink; Entry != &Instance->Ptys; Entry = Entry->Flink)
    {
        PLX_TTY Tty = CONTAINING_RECORD(Entry, LX_TTY, Link);

        if ((ULONG)Tty->Index == Index)
        {
            LxTtyReference(Tty);
            Found = Tty;
            break;
        }
    }
    ExReleaseFastMutex(&Instance->Lock);
    return Found;
}

ULONG
LxTtyList(
    _In_ PLX_INSTANCE Instance,
    _Out_writes_(Count) PULONG Indexes,
    _In_ ULONG Count)
{
    PLIST_ENTRY Entry;
    ULONG Used = 0;

    ExAcquireFastMutex(&Instance->Lock);
    for (Entry = Instance->Ptys.Flink; (Entry != &Instance->Ptys) && (Used < Count); Entry = Entry->Flink)
        Indexes[Used++] = (ULONG)CONTAINING_RECORD(Entry, LX_TTY, Link)->Index;
    ExReleaseFastMutex(&Instance->Lock);
    return Used;
}

VOID
LxTtyAttachControlling(
    _In_ PLX_PROCESS Process,
    _In_ PLX_TTY Tty)
{
    if ((Process->Sid != Process->Pid) || (Process->Tty != NULL))
        return;

    LxTtyReference(Tty);
    Process->Tty = Tty;
    Tty->Session = Process->Sid;
    Tty->ForegroundPgrp = Process->Pgid;
}

static
BOOLEAN
LxTtySlaveGone(
    _In_ PLX_TTY Tty)
{
    return !Tty->NtMaster && Tty->SlaveOpened && (Tty->SlaveOpens == 0);
}

static
BOOLEAN
LxTtyInputRoom(
    _In_ PLX_TTY Tty)
{
    if (Tty->Termios.Lflag & LX_ICANON)
        return Tty->ReadyLength + Tty->LineLength < LX_TTY_BUFFER;
    return Tty->ReadyLength < LX_TTY_BUFFER;
}

static
BOOLEAN
LxTtyOutputByte(
    _In_ PLX_TTY Tty,
    _In_ UCHAR Byte)
{
    if ((Byte == '\n') && ((Tty->Termios.Oflag & (LX_OPOST | LX_ONLCR)) == (LX_OPOST | LX_ONLCR)))
    {
        if (Tty->OutputLength + 2 > LX_TTY_BUFFER)
            return FALSE;
        Tty->Output[Tty->OutputLength++] = '\r';
    }
    else if (Tty->OutputLength >= LX_TTY_BUFFER)
    {
        return FALSE;
    }
    Tty->Output[Tty->OutputLength++] = Byte;
    return TRUE;
}

static
VOID
LxTtyEchoByte(
    _In_ PLX_TTY Tty,
    _In_ UCHAR Byte)
{
    if ((Tty->Termios.Lflag & LX_ECHOCTL) && (Byte < 0x20) && (Byte != '\n') && (Byte != '\t'))
    {
        if (LxTtyOutputByte(Tty, '^'))
            LxTtyOutputByte(Tty, Byte + 0x40);
    }
    else
    {
        LxTtyOutputByte(Tty, Byte);
    }
}

static
VOID
LxTtyEchoErase(
    _In_ PLX_TTY Tty)
{
    if (Tty->OutputLength + 3 > LX_TTY_BUFFER)
        return;
    LxTtyOutputByte(Tty, '\b');
    LxTtyOutputByte(Tty, ' ');
    LxTtyOutputByte(Tty, '\b');
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
                LxTtyEchoByte(Tty, Byte);
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
                    LxTtyEchoErase(Tty);
            }
            return;
        }

        if (Byte == Termios->Cc[LX_VKILL])
        {
            while (Tty->LineLength != 0)
            {
                Tty->LineLength--;
                if (DoEcho && (Termios->Lflag & LX_ECHOKE))
                    LxTtyEchoErase(Tty);
            }
            return;
        }

        if ((Termios->Lflag & LX_IEXTEN) && (Byte == Termios->Cc[LX_VWERASE]))
        {
            while ((Tty->LineLength != 0) && (Tty->Line[Tty->LineLength - 1] == ' '))
            {
                Tty->LineLength--;
                if (DoEcho)
                    LxTtyEchoErase(Tty);
            }
            while ((Tty->LineLength != 0) && (Tty->Line[Tty->LineLength - 1] != ' '))
            {
                Tty->LineLength--;
                if (DoEcho)
                    LxTtyEchoErase(Tty);
            }
            return;
        }

        if (Byte == Termios->Cc[LX_VEOF])
        {
            LxTtyCommitLine(Tty);
            return;
        }

        if ((Tty->LineLength < LX_TTY_BUFFER - 1) || ((Byte == '\n') && (Tty->LineLength < LX_TTY_BUFFER)))
            Tty->Line[Tty->LineLength++] = Byte;

        if (DoEcho)
            LxTtyEchoByte(Tty, Byte);
        else if ((Byte == '\n') && (Termios->Lflag & LX_ECHONL))
            LxTtyEchoByte(Tty, Byte);

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
        LxTtyEchoByte(Tty, Byte);
}

LONG64
LxTtyMasterWrite(
    _In_ PLX_TTY Tty,
    _In_reads_bytes_(Length) const VOID *Buffer,
    _In_ ULONG Length,
    _In_ BOOLEAN NonBlocking)
{
    const UCHAR *Data = Buffer;
    ULONG Done = 0;
    NTSTATUS Status;

    while (Done < Length)
    {
        PLX_INSTANCE Instance = NULL;
        ULONG OutputBefore, Foreground = 0;
        LONG Signal = 0;
        BOOLEAN Room, Echoed;

        LxTtyLock(Tty);
        if (Tty->Hangup || LxTtySlaveGone(Tty))
        {
            LxTtyUnlock(Tty);
            return (Done != 0) ? (LONG64)Done : -LX_EIO;
        }

        OutputBefore = Tty->OutputLength;
        while ((Done < Length) && (Signal == 0) && LxTtyInputRoom(Tty))
            LxTtyInputByte(Tty, Data[Done++], &Signal);

        if ((Tty->ReadyLength != 0) || Tty->EofPending)
            KeSetEvent(&Tty->DataReady, IO_NO_INCREMENT, FALSE);
        Echoed = (Tty->OutputLength != OutputBefore);
        if (Echoed)
            KeSetEvent(&Tty->OutputReady, IO_NO_INCREMENT, FALSE);
        Room = LxTtyInputRoom(Tty);
        if (!Room)
            KeClearEvent(&Tty->InputSpace);
        if ((Signal != 0) && (Tty->ForegroundPgrp != 0) && (Tty->Instance != NULL))
        {
            Foreground = Tty->ForegroundPgrp;
            Instance = Tty->Instance;
            LxInstanceReference(Instance);
        }
        LxTtyUnlock(Tty);

        LxPollWake(&Tty->Poll, LX_POLLIN | LX_POLLRDNORM);
        if (Echoed)
            LxPollWake(&Tty->MasterPoll, LX_POLLIN | LX_POLLRDNORM);
        if (Instance != NULL)
        {
            LxSignalGroup(Instance, NULL, Foreground, Signal);
            LxInstanceDereference(Instance);
        }

        if ((Done < Length) && !Room)
        {
            if (NonBlocking)
                return (Done != 0) ? (LONG64)Done : -LX_EAGAIN;
            Status = KeWaitForSingleObject(&Tty->InputSpace, UserRequest, UserMode, TRUE, NULL);
            if (Status != STATUS_SUCCESS)
                return (Done != 0) ? (LONG64)Done : -LX_EINTR;
        }
    }
    return (LONG64)Done;
}

LONG64
LxTtyMasterRead(
    _In_ PLX_TTY Tty,
    _Out_writes_bytes_(Length) PVOID Buffer,
    _In_ ULONG Length,
    _In_ BOOLEAN NonBlocking)
{
    NTSTATUS Status;

    if (Length == 0)
        return 0;

    for (;;)
    {
        ULONG Count;

        LxTtyLock(Tty);
        if (Tty->OutputLength != 0)
        {
            Count = min(Length, Tty->OutputLength);
            RtlCopyMemory(Buffer, Tty->Output, Count);
            RtlMoveMemory(Tty->Output, Tty->Output + Count, Tty->OutputLength - Count);
            Tty->OutputLength -= Count;
            if ((Tty->OutputLength == 0) && !Tty->Hangup && !LxTtySlaveGone(Tty))
                KeClearEvent(&Tty->OutputReady);
            KeSetEvent(&Tty->OutputSpace, IO_NO_INCREMENT, FALSE);
            LxTtyUnlock(Tty);
            LxPollWake(&Tty->Poll, LX_POLLOUT | LX_POLLWRNORM);
            return Count;
        }

        if (Tty->Hangup || LxTtySlaveGone(Tty))
        {
            LxTtyUnlock(Tty);
            return -LX_EIO;
        }

        KeClearEvent(&Tty->OutputReady);
        LxTtyUnlock(Tty);

        if (NonBlocking)
            return -LX_EAGAIN;

        Status = KeWaitForSingleObject(&Tty->OutputReady, UserRequest, UserMode, TRUE, NULL);
        if (Status != STATUS_SUCCESS)
            return -LX_EINTR;
    }
}

static
VOID
LxTtyApplyWinSize(
    _In_ PLX_TTY Tty,
    _In_ PLX_WINSIZE WinSize,
    _In_opt_ PLX_INSTANCE Instance)
{
    BOOLEAN Changed;
    ULONG Foreground;

    LxTtyLock(Tty);
    Changed = (Tty->WinSize.Rows != WinSize->Rows) || (Tty->WinSize.Cols != WinSize->Cols);
    Tty->WinSize = *WinSize;
    Foreground = Tty->ForegroundPgrp;
    if (Instance == NULL)
        Instance = Tty->Instance;
    if (Instance != NULL)
        LxInstanceReference(Instance);
    LxTtyUnlock(Tty);

    if (Instance == NULL)
        return;
    if (Changed && (Foreground != 0))
        LxSignalGroup(Instance, NULL, Foreground, LX_SIGWINCH);
    LxInstanceDereference(Instance);
}

VOID
LxTtySetWinSize(
    _In_ PLX_TTY Tty,
    _In_ USHORT Rows,
    _In_ USHORT Columns)
{
    LX_WINSIZE WinSize;

    RtlZeroMemory(&WinSize, sizeof(WinSize));
    WinSize.Rows = Rows;
    WinSize.Cols = Columns;
    LxTtyApplyWinSize(Tty, &WinSize, NULL);
}

VOID
LxTtyMasterClose(
    _In_ PLX_TTY Tty)
{
    PLX_INSTANCE Instance = Tty->Instance;
    PLX_PROCESS Leader;
    ULONG Session;

    if (Instance == NULL)
        return;

    if (Tty->Index >= 0)
    {
        ExAcquireFastMutex(&Instance->Lock);
        RemoveEntryList(&Tty->Link);
        InitializeListHead(&Tty->Link);
        ExReleaseFastMutex(&Instance->Lock);
    }

    LxTtyLock(Tty);
    Tty->Hangup = TRUE;
    Tty->Instance = NULL;
    Session = Tty->Session;
    KeSetEvent(&Tty->DataReady, IO_NO_INCREMENT, FALSE);
    KeSetEvent(&Tty->InputSpace, IO_NO_INCREMENT, FALSE);
    KeSetEvent(&Tty->OutputReady, IO_NO_INCREMENT, FALSE);
    KeSetEvent(&Tty->OutputSpace, IO_NO_INCREMENT, FALSE);
    LxTtyUnlock(Tty);

    LxPollWake(&Tty->Poll, LX_POLLIN | LX_POLLRDNORM | LX_POLLOUT | LX_POLLWRNORM | LX_POLLHUP | LX_POLLERR);
    LxPollWake(&Tty->MasterPoll, LX_POLLIN | LX_POLLHUP);

    if (Session != 0)
    {
        Leader = LxProcessLookup(Instance, Session);
        if (Leader != NULL)
        {
            LxSignalSend(Leader, NULL, LX_SIGHUP, NULL);
            LxSignalSend(Leader, NULL, LX_SIGCONT, NULL);
            LxProcessDereference(Leader);
        }
    }
    LxInstanceDereference(Instance);
}

BOOLEAN
LxTtyReadable(
    _In_ PLX_TTY Tty)
{
    BOOLEAN Readable;

    LxTtyLock(Tty);
    Readable = (Tty->ReadyLength != 0) || Tty->EofPending || Tty->Hangup;
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
            if ((Tty->ReadyLength == 0) && !Tty->EofPending && !Tty->Hangup)
                KeClearEvent(&Tty->DataReady);
            KeSetEvent(&Tty->InputSpace, IO_NO_INCREMENT, FALSE);
            LxTtyUnlock(Tty);
            LxPollWake(&Tty->MasterPoll, LX_POLLOUT | LX_POLLWRNORM);
            Result = LxCopyToUser(Buffer, Data, Count);
            if (Result == 0)
                Result = Count;
            break;
        }

        if (Tty->EofPending || Tty->Hangup)
        {
            Tty->EofPending = FALSE;
            if (!Tty->Hangup)
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
    _In_ ULONG64 Length,
    _In_ BOOLEAN NonBlocking)
{
    UCHAR Data[512];
    ULONG64 Total = 0;
    NTSTATUS Status;
    LONG64 Error;

    while (Total < Length)
    {
        ULONG Chunk = (ULONG)min(Length - Total, sizeof(Data));
        ULONG Used = 0;

        Error = LxCopyFromUser(Data, Buffer + Total, Chunk);
        if (Error != 0)
            return (Total != 0) ? (LONG64)Total : Error;

        while (Used < Chunk)
        {
            ULONG Before = Used;
            BOOLEAN Full;

            LxTtyLock(Tty);
            if (Tty->Hangup)
            {
                LxTtyUnlock(Tty);
                return (Total != 0) ? (LONG64)Total : -LX_EIO;
            }
            while ((Used < Chunk) && LxTtyOutputByte(Tty, Data[Used]))
                Used++;
            if (Used != Before)
                KeSetEvent(&Tty->OutputReady, IO_NO_INCREMENT, FALSE);
            Full = (Used < Chunk);
            if (Full)
                KeClearEvent(&Tty->OutputSpace);
            LxTtyUnlock(Tty);

            Total += Used - Before;
            if (Used != Before)
                LxPollWake(&Tty->MasterPoll, LX_POLLIN | LX_POLLRDNORM);

            if (Full)
            {
                if (NonBlocking)
                    return (Total != 0) ? (LONG64)Total : -LX_EAGAIN;
                Status = KeWaitForSingleObject(&Tty->OutputSpace, UserRequest, UserMode, TRUE, NULL);
                if (Status != STATUS_SUCCESS)
                    return (Total != 0) ? (LONG64)Total : -LX_EINTR;
            }
        }
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
                KeSetEvent(&Tty->InputSpace, IO_NO_INCREMENT, FALSE);
            }
            else if ((Tty->Termios.Lflag & LX_ICANON) && !(Termios.Lflag & LX_ICANON) && (Tty->LineLength != 0))
            {
                LxTtyCommitLine(Tty);
                KeSetEvent(&Tty->DataReady, IO_NO_INCREMENT, FALSE);
            }
            Tty->Termios = Termios;
            if (LxTtyInputRoom(Tty))
                KeSetEvent(&Tty->InputSpace, IO_NO_INCREMENT, FALSE);
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
            LxTtyApplyWinSize(Tty, &WinSize, Process->Instance);
            return 0;

        case LX_TIOCGPGRP:
            LxTtyLock(Tty);
            Value = (LONG)Tty->ForegroundPgrp;
            LxTtyUnlock(Tty);
            Value = (LONG)LxGlobalToNr(Process, (ULONG)Value);
            return LxCopyToUser(Argument, &Value, sizeof(Value));

        case LX_TIOCSPGRP:
            Result = LxCopyFromUser(&Value, Argument, sizeof(Value));
            if (Result != 0)
                return Result;
            if (Value <= 0)
                return -LX_EINVAL;
            Value = (LONG)LxNrToGlobal(Process, (ULONG)Value);
            if (Value <= 0)
                return -LX_EPERM;
            LxTtyLock(Tty);
            Tty->ForegroundPgrp = (ULONG)Value;
            LxTtyUnlock(Tty);
            return 0;

        case LX_TIOCGSID:
            LxTtyLock(Tty);
            Value = (LONG)Tty->Session;
            LxTtyUnlock(Tty);
            Value = (LONG)LxGlobalToNr(Process, (ULONG)Value);
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
                KeSetEvent(&Tty->InputSpace, IO_NO_INCREMENT, FALSE);
                LxTtyUnlock(Tty);
            }
            return 0;

        case LX_TIOCOUTQ:
            LxTtyLock(Tty);
            Value = (LONG)Tty->OutputLength;
            LxTtyUnlock(Tty);
            return LxCopyToUser(Argument, &Value, sizeof(Value));

        case LX_TCSBRK:
        case LX_TCXONC:
        case LX_TIOCNOTTY:
        case LX_TIOCVHANGUP:
        case LX_TIOCEXCL:
        case LX_TIOCNXCL:
            return 0;

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
    return LxTtyWrite(File->Object, Buffer, Length, (File->Flags & LX_O_NONBLOCK) != 0);
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
    ULONG Mask = 0;

    if (Entry != NULL)
        LxPollAdd(&Tty->Poll, Entry);

    LxTtyLock(Tty);
    if (Tty->Hangup)
    {
        Mask = LX_POLLIN | LX_POLLRDNORM | LX_POLLOUT | LX_POLLWRNORM | LX_POLLHUP | LX_POLLERR;
    }
    else
    {
        if ((Tty->ReadyLength != 0) || Tty->EofPending)
            Mask |= LX_POLLIN | LX_POLLRDNORM;
        if (Tty->OutputLength < LX_TTY_BUFFER)
            Mask |= LX_POLLOUT | LX_POLLWRNORM;
    }
    LxTtyUnlock(Tty);
    return Mask;
}

static
LONG
LxTtyFileStat(
    _In_ PLX_FILE File,
    _Out_ PLX_STAT Stat)
{
    PLX_TTY Tty = File->Object;
    LARGE_INTEGER Now;

    RtlZeroMemory(Stat, sizeof(*Stat));
    Stat->Mode = LX_S_IFCHR | 0620;
    Stat->Nlink = 1;
    Stat->Rdev = File->Rdev ? File->Rdev : LxTtyDevice(Tty);
    Stat->Ino = File->Ino;
    Stat->Dev = (File->Mount != NULL) ? File->Mount->Dev : 0x16;
    Stat->BlkSize = 1024;
    Stat->Uid = Tty->Uid;
    Stat->Gid = Tty->Gid;
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
    PLX_TTY Tty = File->Object;
    BOOLEAN Gone;

    LxTtyLock(Tty);
    Tty->SlaveOpens--;
    Gone = LxTtySlaveGone(Tty);
    if (Gone)
    {
        KeSetEvent(&Tty->OutputReady, IO_NO_INCREMENT, FALSE);
        KeSetEvent(&Tty->InputSpace, IO_NO_INCREMENT, FALSE);
    }
    LxTtyUnlock(Tty);

    if (Gone)
        LxPollWake(&Tty->MasterPoll, LX_POLLIN | LX_POLLHUP);
    LxTtyDereference(Tty);
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
    LxTtyLock(Tty);
    Tty->SlaveOpens++;
    Tty->SlaveOpened = TRUE;
    LxTtyUnlock(Tty);
    File->Object = Tty;
    File->Mode = 0620;
    return File;
}

LONG
LxTtyOpenSlave(
    _In_ PLX_PROCESS Process,
    _In_ PLX_TTY Tty,
    _In_ ULONG Flags,
    _Out_ PLX_FILE *Result)
{
    PLX_FILE File;
    BOOLEAN Usable;

    *Result = NULL;
    LxTtyLock(Tty);
    Usable = !Tty->Locked && !Tty->Hangup;
    LxTtyUnlock(Tty);
    if (!Usable)
        return -LX_EIO;

    File = LxTtyOpen(Tty, Flags);
    if (File == NULL)
        return -LX_ENOMEM;
    File->Rdev = LxTtyDevice(Tty);
    if (!(Flags & LX_O_NOCTTY))
        LxTtyAttachControlling(Process, Tty);
    *Result = File;
    return 0;
}

PLX_TTY
LxFileGetTty(
    _In_ PLX_FILE File)
{
    return (File->Ops == &LxTtyFileOps) ? File->Object : NULL;
}

static
LONG64
LxPtmxFileRead(
    _In_ PLX_FILE File,
    _In_ ULONG64 Buffer,
    _In_ ULONG64 Length,
    _Inout_opt_ PLONG64 Offset)
{
    ULONG Chunk = (ULONG)min(Length, LX_TTY_BUFFER);
    PUCHAR Data;
    LONG64 Result;

    UNREFERENCED_PARAMETER(Offset);
    if (Chunk == 0)
        return 0;
    Data = LxAllocPaged(Chunk);
    if (Data == NULL)
        return -LX_ENOMEM;
    Result = LxTtyMasterRead(File->Object, Data, Chunk, (File->Flags & LX_O_NONBLOCK) != 0);
    if (Result > 0)
    {
        LONG64 Error = LxCopyToUser(Buffer, Data, (ULONG)Result);

        if (Error != 0)
            Result = Error;
    }
    LxFreePaged(Data);
    return Result;
}

static
LONG64
LxPtmxFileWrite(
    _In_ PLX_FILE File,
    _In_ ULONG64 Buffer,
    _In_ ULONG64 Length,
    _Inout_opt_ PLONG64 Offset)
{
    UCHAR Data[512];
    ULONG64 Total = 0;
    LONG64 Result;

    UNREFERENCED_PARAMETER(Offset);
    while (Total < Length)
    {
        ULONG Chunk = (ULONG)min(Length - Total, sizeof(Data));

        Result = LxCopyFromUser(Data, Buffer + Total, Chunk);
        if (Result == 0)
            Result = LxTtyMasterWrite(File->Object, Data, Chunk, (File->Flags & LX_O_NONBLOCK) != 0);
        if (Result < 0)
            return (Total != 0) ? (LONG64)Total : Result;
        Total += (ULONG64)Result;
        if ((ULONG)Result < Chunk)
            break;
    }
    return (LONG64)Total;
}

static
LONG64
LxPtmxFileIoctl(
    _In_ PLX_FILE File,
    _In_ ULONG Request,
    _In_ ULONG64 Argument)
{
    PLX_TTY Tty = File->Object;
    PLX_THREAD Thread = LxCurrentThread();
    PLX_FILE Slave;
    LX_WINSIZE WinSize;
    LONG Value;
    LONG64 Result;

    if (Thread == NULL)
        return -LX_ENOTTY;

    switch (Request)
    {
        case LX_TIOCGPTN:
            Value = Tty->Index;
            return LxCopyToUser(Argument, &Value, sizeof(Value));

        case LX_TIOCSPTLCK:
            Result = LxCopyFromUser(&Value, Argument, sizeof(Value));
            if (Result != 0)
                return Result;
            LxTtyLock(Tty);
            Tty->Locked = (Value != 0);
            LxTtyUnlock(Tty);
            return 0;

        case LX_TIOCGPTPEER:
        {
            PSTR Path;

            if (Argument & ~(ULONG64)(LX_O_ACCMODE | LX_O_NOCTTY | LX_O_NONBLOCK | LX_O_CLOEXEC))
                return -LX_EINVAL;
            Path = LxStrPrintf("/dev/pts/%ld", Tty->Index);
            if (Path == NULL)
                return -LX_ENOMEM;
            Result = LxVfsOpenResolve(Thread->Process, LX_AT_FDCWD, Path,
                                      (ULONG)Argument & (LX_O_ACCMODE | LX_O_NOCTTY | LX_O_NONBLOCK), 0, 0, &Slave);
            if ((Result == 0) && (LxFileGetTty(Slave) != Tty))
            {
                LxFileDereference(Slave);
                Result = -LX_EIO;
            }
            if (Result == -LX_ENOENT)
            {
                Result = LxTtyOpenSlave(Thread->Process, Tty, (ULONG)Argument, &Slave);
                if (Result == 0)
                {
                    Slave->Path = Path;
                    Path = NULL;
                }
            }
            LxFree(Path);
            if (Result != 0)
                return Result;
            Result = LxFdInstall(Thread->Process, Slave, 0, (Argument & LX_O_CLOEXEC) != 0);
            LxFileDereference(Slave);
            return Result;
        }

        case LX_TIOCSWINSZ:
            Result = LxCopyFromUser(&WinSize, Argument, sizeof(WinSize));
            if (Result != 0)
                return Result;
            LxTtyApplyWinSize(Tty, &WinSize, Thread->Process->Instance);
            return 0;

        case LX_FIONREAD:
            LxTtyLock(Tty);
            Value = (LONG)Tty->OutputLength;
            LxTtyUnlock(Tty);
            return LxCopyToUser(Argument, &Value, sizeof(Value));

        default:
            return LxTtyIoctl(Thread->Process, Tty, Request, Argument);
    }
}

static
ULONG
LxPtmxFilePoll(
    _In_ PLX_FILE File,
    _In_opt_ PLX_POLL_ENTRY Entry)
{
    PLX_TTY Tty = File->Object;
    ULONG Mask = 0;

    if (Entry != NULL)
        LxPollAdd(&Tty->MasterPoll, Entry);

    LxTtyLock(Tty);
    if (Tty->OutputLength != 0)
        Mask |= LX_POLLIN | LX_POLLRDNORM;
    if (LxTtySlaveGone(Tty))
        Mask |= LX_POLLHUP;
    if (LxTtyInputRoom(Tty))
        Mask |= LX_POLLOUT | LX_POLLWRNORM;
    LxTtyUnlock(Tty);
    return Mask;
}

static
LONG
LxPtmxFileStat(
    _In_ PLX_FILE File,
    _Out_ PLX_STAT Stat)
{
    LARGE_INTEGER Now;

    RtlZeroMemory(Stat, sizeof(*Stat));
    Stat->Mode = LX_S_IFCHR | 0666;
    Stat->Nlink = 1;
    Stat->Rdev = (5 << 8) | 2;
    Stat->Ino = File->Ino;
    Stat->Dev = (File->Mount != NULL) ? File->Mount->Dev : 5;
    Stat->BlkSize = 4096;
    Stat->Gid = LX_TTY_GID;
    KeQuerySystemTime(&Now);
    LxNtTimeToTimespec(Now.QuadPart, &Stat->Mtime);
    Stat->Atime = Stat->Ctime = Stat->Mtime;
    return 0;
}

static
VOID
LxPtmxFileRelease(
    _In_ PLX_FILE File)
{
    LxTtyMasterClose(File->Object);
    LxTtyDereference(File->Object);
    File->Object = NULL;
}

static const LX_FILE_OPS LxPtmxFileOps =
{
    LxPtmxFileRead, LxPtmxFileWrite, LxPtmxFileIoctl, LxPtmxFilePoll, LxPtmxFileStat, NULL, NULL, NULL, LxPtmxFileRelease
};

LONG
LxPtmxOpen(
    _In_ PLX_PROCESS Process,
    _In_ ULONG Flags,
    _Out_ PLX_FILE *Result)
{
    PLX_FILE File;
    PLX_TTY Tty;
    NTSTATUS Status;

    *Result = NULL;
    Status = LxTtyCreate(Process->Instance, LX_TTY_LOCKED, Process->Uid[1], 0, 0, &Tty);
    if (Status == STATUS_TOO_MANY_OPENED_FILES)
        return -LX_ENOSPC;
    if (!NT_SUCCESS(Status))
        return -LX_ENOMEM;

    File = LxFileAlloc(&LxPtmxFileOps, Flags & (LX_O_ACCMODE | LX_O_NONBLOCK), LX_S_IFCHR);
    if (File == NULL)
    {
        LxTtyMasterClose(Tty);
        LxTtyDereference(Tty);
        return -LX_ENOMEM;
    }
    File->Object = Tty;
    File->Mode = 0666;
    File->Rdev = (5 << 8) | 2;
    *Result = File;
    return 0;
}
