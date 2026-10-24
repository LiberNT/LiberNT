/*
 * PROJECT:     LiberNT Linux Subsystem
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     ELF image loading and Linux process start-up state
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "lxcore.h"

#define NDEBUG
#include <debug.h>

#define LX_MAX_INTERP_DEPTH 4
#define LX_SHEBANG_MAX      256

typedef struct _LX_ELF_IMAGE
{
    PLX_FILE File;
    LX_ELF64_EHDR Header;
    PLX_ELF64_PHDR Phdrs;
} LX_ELF_IMAGE, *PLX_ELF_IMAGE;

static
VOID
LxElfClose(
    _Inout_ PLX_ELF_IMAGE Image)
{
    if (Image->File != NULL)
        LxFileDereference(Image->File);
    LxFree(Image->Phdrs);
    RtlZeroMemory(Image, sizeof(*Image));
}

static
LONG
LxElfOpen(
    _In_ PLX_PROCESS Process,
    _In_ PCSTR Path,
    _Out_ PLX_ELF_IMAGE Image,
    _Out_opt_ PCHAR Shebang)
{
    LONG64 Read;
    ULONG Size;
    LONG Error;

    RtlZeroMemory(Image, sizeof(*Image));
    if (Shebang != NULL)
        Shebang[0] = '\0';

    Error = LxVfsOpen(Process, LX_AT_FDCWD, Path, LX_O_RDONLY, 0, &Image->File);
    if (Error != 0)
        return Error;
    if (Image->File->Directory || (Image->File->Handle == NULL) || Image->File->NtPipe)
    {
        LxElfClose(Image);
        return -LX_EACCES;
    }

    Read = LxFileReadKernel(Image->File, &Image->Header, sizeof(Image->Header), 0);
    if (Read < 0)
    {
        LxElfClose(Image);
        return (LONG)Read;
    }

    if ((Read >= 2) && (Image->Header.Ident[0] == '#') && (Image->Header.Ident[1] == '!') && (Shebang != NULL))
    {
        Read = LxFileReadKernel(Image->File, Shebang, LX_SHEBANG_MAX - 1, 0);
        LxElfClose(Image);
        if (Read < 2)
            return -LX_ENOEXEC;
        Shebang[Read] = '\0';
        return 1;
    }

    if ((Read != sizeof(Image->Header)) ||
        (Image->Header.Ident[0] != 0x7F) || (Image->Header.Ident[1] != 'E') ||
        (Image->Header.Ident[2] != 'L') || (Image->Header.Ident[3] != 'F') ||
        (Image->Header.Ident[4] != LX_ELFCLASS64) ||
        (Image->Header.Ident[5] != LX_ELFDATA2LSB) ||
        ((Image->Header.Type != LX_ET_EXEC) && (Image->Header.Type != LX_ET_DYN)) ||
        (Image->Header.Machine != LxArchElfMachine()) ||
        (Image->Header.PhEntSize != sizeof(LX_ELF64_PHDR)) ||
        (Image->Header.PhNum == 0) || (Image->Header.PhNum > 128))
    {
        LxElfClose(Image);
        return -LX_ENOEXEC;
    }

    Size = Image->Header.PhNum * sizeof(LX_ELF64_PHDR);
    Image->Phdrs = LxAlloc(Size);
    if (Image->Phdrs == NULL)
    {
        LxElfClose(Image);
        return -LX_ENOMEM;
    }

    Read = LxFileReadKernel(Image->File, Image->Phdrs, Size, Image->Header.PhOff);
    if (Read != Size)
    {
        LxElfClose(Image);
        return (Read < 0) ? (LONG)Read : -LX_ENOEXEC;
    }

    return 0;
}

static
LONG
LxElfInterpreter(
    _In_ PLX_ELF_IMAGE Image,
    _Out_ PSTR *Interpreter)
{
    ULONG Index;

    *Interpreter = NULL;
    for (Index = 0; Index < Image->Header.PhNum; Index++)
    {
        PLX_ELF64_PHDR Phdr = &Image->Phdrs[Index];
        PSTR Path;
        LONG64 Read;

        if (Phdr->Type != LX_PT_INTERP)
            continue;

        if ((Phdr->FileSz < 2) || (Phdr->FileSz >= LX_PATH_MAX))
            return -LX_ENOEXEC;

        Path = LxAlloc((SIZE_T)Phdr->FileSz + 1);
        if (Path == NULL)
            return -LX_ENOMEM;

        Read = LxFileReadKernel(Image->File, Path, (ULONG)Phdr->FileSz, Phdr->Offset);
        if (Read != (LONG64)Phdr->FileSz)
        {
            LxFree(Path);
            return -LX_ENOEXEC;
        }
        Path[Phdr->FileSz] = '\0';
        *Interpreter = Path;
        return 0;
    }
    return 0;
}

static
ULONG
LxElfProtection(
    _In_ ULONG Flags)
{
    ULONG Prot = 0;

    if (Flags & LX_PF_R)
        Prot |= LX_PROT_READ;
    if (Flags & LX_PF_W)
        Prot |= LX_PROT_WRITE;
    if (Flags & LX_PF_X)
        Prot |= LX_PROT_EXEC;
    return Prot;
}

static
LONG
LxElfMap(
    _In_ PLX_PROCESS Process,
    _In_ PLX_ELF_IMAGE Image,
    _In_ ULONG64 DynamicBase,
    _Out_ PLX_ELF_INFO Info)
{
    ULONG64 Low = MAXULONG64, High = 0, Bias = 0, Base, Size, PreviousEnd = 0;
    ULONG PreviousProt = 0, Index;
    PVOID Address;
    SIZE_T RegionSize;
    NTSTATUS Status;
    LONG64 Result;
    LONG Error;

    RtlZeroMemory(Info, sizeof(*Info));

    for (Index = 0; Index < Image->Header.PhNum; Index++)
    {
        PLX_ELF64_PHDR Phdr = &Image->Phdrs[Index];

        if (Phdr->Type != LX_PT_LOAD)
            continue;
        if ((Phdr->FileSz > Phdr->MemSz) || (Phdr->VAddr + Phdr->MemSz < Phdr->VAddr))
            return -LX_ENOEXEC;
        Low = min(Low, LX_ALIGN_DOWN(Phdr->VAddr, LX_PAGE_SIZE));
        High = max(High, LX_ALIGN_UP(Phdr->VAddr + Phdr->MemSz, LX_PAGE_SIZE));
    }
    if (High <= Low)
        return -LX_ENOEXEC;

    if (Image->Header.Type == LX_ET_DYN)
        Bias = DynamicBase - LX_ALIGN_DOWN(Low, LX_ALLOC_GRANULARITY);

    Base = LX_ALIGN_DOWN(Low + Bias, LX_ALLOC_GRANULARITY);
    Size = LX_ALIGN_UP(High + Bias, LX_ALLOC_GRANULARITY) - Base;
    Address = (PVOID)(ULONG_PTR)Base;
    RegionSize = (SIZE_T)Size;
    Status = ZwAllocateVirtualMemory(NtCurrentProcess(), &Address, 0, &RegionSize, MEM_RESERVE, PAGE_NOACCESS);
    if (!NT_SUCCESS(Status) || ((ULONG64)(ULONG_PTR)Address != Base))
    {
        DPRINT1("Linux image range %I64x-%I64x is not available: 0x%08lx\n", Base, Base + Size, Status);
        return -LX_ENOMEM;
    }

    for (Index = 0; Index < Image->Header.PhNum; Index++)
    {
        PLX_ELF64_PHDR Phdr = &Image->Phdrs[Index];
        ULONG64 Start, FileEnd, End;
        ULONG Prot;

        if (Phdr->Type != LX_PT_LOAD)
            continue;
        if ((Phdr->FileSz != 0) && ((Phdr->VAddr - Phdr->Offset) & (LX_PAGE_SIZE - 1)))
            return -LX_ENOEXEC;

        Start = LX_ALIGN_DOWN(Phdr->VAddr + Bias, LX_PAGE_SIZE);
        FileEnd = (Phdr->FileSz != 0) ? LX_ALIGN_UP(Phdr->VAddr + Phdr->FileSz + Bias, LX_PAGE_SIZE) : Start;
        End = LX_ALIGN_UP(Phdr->VAddr + Phdr->MemSz + Bias, LX_PAGE_SIZE);
        Prot = LxElfProtection(Phdr->Flags);

        if (FileEnd > Start)
        {
            Result = LxMmMap(Process, Start, FileEnd - Start, Prot, LX_MAP_PRIVATE | LX_MAP_FIXED,
                             Image->File, LX_ALIGN_DOWN(Phdr->Offset, LX_PAGE_SIZE));
            if (Result < 0)
                return (LONG)Result;
        }
        if (End > FileEnd)
        {
            Result = LxMmMap(Process, FileEnd, End - FileEnd, Prot, LX_MAP_PRIVATE | LX_MAP_FIXED, NULL, 0);
            if (Result < 0)
                return (LONG)Result;
        }
        if ((Phdr->MemSz > Phdr->FileSz) && (Phdr->FileSz != 0))
        {
            Error = LxMmZero(Process, Phdr->VAddr + Phdr->FileSz + Bias, FileEnd);
            if (Error != 0)
                return Error;
        }
        if (Start < PreviousEnd)
            LxMmProtect(Process, Start, LX_PAGE_SIZE, Prot | PreviousProt);

        PreviousEnd = End;
        PreviousProt = Prot;
    }

    for (Index = 0; Index < Image->Header.PhNum; Index++)
    {
        PLX_ELF64_PHDR Phdr = &Image->Phdrs[Index];

        if (Phdr->Type == LX_PT_PHDR)
        {
            Info->Phdr = Phdr->VAddr + Bias;
        }
        else if ((Phdr->Type == LX_PT_LOAD) && (Info->Phdr == 0) &&
                 (Image->Header.PhOff >= Phdr->Offset) &&
                 (Image->Header.PhOff < Phdr->Offset + Phdr->FileSz))
        {
            Info->Phdr = Phdr->VAddr + (Image->Header.PhOff - Phdr->Offset) + Bias;
        }
    }

    Info->Entry = Image->Header.Entry + Bias;
    Info->PhEnt = sizeof(LX_ELF64_PHDR);
    Info->PhNum = Image->Header.PhNum;
    Info->LoadBias = Bias;
    Info->ImageEnd = High + Bias;
    return 0;
}

static
LONG
LxStringListAppend(
    _Inout_ PSTR *List,
    _Inout_ PULONG Length,
    _Inout_ PULONG Count,
    _In_ PCSTR String,
    _In_ BOOLEAN Prepend)
{
    ULONG StringLength = (ULONG)strlen(String) + 1;
    PSTR NewList;

    if (*Length + StringLength > LX_MAX_ARG_TOTAL)
        return -LX_E2BIG;

    NewList = LxAlloc(*Length + StringLength);
    if (NewList == NULL)
        return -LX_ENOMEM;

    if (Prepend)
    {
        RtlCopyMemory(NewList, String, StringLength);
        if (*Length != 0)
            RtlCopyMemory(NewList + StringLength, *List, *Length);
    }
    else
    {
        if (*Length != 0)
            RtlCopyMemory(NewList, *List, *Length);
        RtlCopyMemory(NewList + *Length, String, StringLength);
    }

    LxFree(*List);
    *List = NewList;
    *Length += StringLength;
    (*Count)++;
    return 0;
}

static
LONG
LxApplyShebang(
    _Inout_ PLX_EXEC_REQUEST Request,
    _Inout_ PSTR *FileName,
    _In_ PSTR Line)
{
    PSTR Interpreter, Argument = NULL, Cursor, End;
    PSTR Args = Request->Args;
    ULONG FirstLength;
    LONG Error;

    End = strchr(Line, '\n');
    if (End != NULL)
        *End = '\0';

    Cursor = Line + 2;
    while ((*Cursor == ' ') || (*Cursor == '\t'))
        Cursor++;
    if (*Cursor == '\0')
        return -LX_ENOEXEC;

    Interpreter = Cursor;
    while ((*Cursor != '\0') && (*Cursor != ' ') && (*Cursor != '\t'))
        Cursor++;
    if (*Cursor != '\0')
    {
        *Cursor++ = '\0';
        while ((*Cursor == ' ') || (*Cursor == '\t'))
            Cursor++;
        if (*Cursor != '\0')
        {
            Argument = Cursor;
            End = Cursor + strlen(Cursor);
            while ((End > Argument) && ((End[-1] == ' ') || (End[-1] == '\t') || (End[-1] == '\r')))
                *--End = '\0';
        }
    }

    FirstLength = (Request->ArgCount != 0) ? (ULONG)strlen(Args) + 1 : 0;
    Request->Args = NULL;
    Request->ArgLength -= FirstLength;
    if (Request->ArgCount != 0)
        Request->ArgCount--;
    if (Request->ArgLength != 0)
    {
        Request->Args = LxAlloc(Request->ArgLength);
        if (Request->Args == NULL)
        {
            LxFree(Args);
            return -LX_ENOMEM;
        }
        RtlCopyMemory(Request->Args, Args + FirstLength, Request->ArgLength);
    }
    LxFree(Args);

    Error = LxStringListAppend(&Request->Args, &Request->ArgLength, &Request->ArgCount, *FileName, TRUE);
    if ((Error == 0) && (Argument != NULL))
        Error = LxStringListAppend(&Request->Args, &Request->ArgLength, &Request->ArgCount, Argument, TRUE);
    if (Error == 0)
        Error = LxStringListAppend(&Request->Args, &Request->ArgLength, &Request->ArgCount, Interpreter, TRUE);
    if (Error != 0)
        return Error;

    LxFree(*FileName);
    *FileName = LxStrDup(Interpreter);
    return (*FileName != NULL) ? 0 : -LX_ENOMEM;
}

static
LONG
LxBuildStack(
    _In_ PLX_PROCESS Process,
    _In_ PLX_EXEC_REQUEST Request,
    _In_ PCSTR ExecFn,
    _In_ PLX_ELF_INFO Main,
    _In_ ULONG64 InterpreterBase,
    _Out_ PULONG64 StackPointer)
{
    PCSTR Platform = LxArchPlatform();
    ULONG64 Base = 0, Top, Cursor, ExecFnAddress, PlatformAddress, RandomAddress, ArgAddress, EnvAddress;
    ULONG64 Auxv[2 * 20];
    PULONG64 Vector;
    ULONG AuxCount = 0, Words, Index, Offset;
    UCHAR Random[16];
    LONG64 Error;

    Error = LxMmMap(Process, 0, LX_STACK_SIZE, LX_PROT_READ | LX_PROT_WRITE, LX_MAP_PRIVATE | LX_VMA_STACK, NULL, 0);
    if (Error < 0)
        return (LONG)Error;
    Base = (ULONG64)Error;
    Top = Base + LX_STACK_SIZE;

    Cursor = Top - 16;
    Cursor -= strlen(ExecFn) + 1;
    ExecFnAddress = Cursor;
    Cursor -= strlen(Platform) + 1;
    PlatformAddress = Cursor;
    Cursor -= sizeof(Random);
    RandomAddress = Cursor;
    Cursor -= Request->EnvLength;
    EnvAddress = Cursor;
    Cursor -= Request->ArgLength;
    ArgAddress = Cursor;
    Cursor = LX_ALIGN_DOWN(Cursor, 16);

    if (!NT_SUCCESS(LxFillRandom(Random, sizeof(Random))))
        return -LX_EIO;

#define LX_AUX(Type, Value) do { Auxv[AuxCount++] = (Type); Auxv[AuxCount++] = (Value); } while (0)
    LX_AUX(LX_AT_PHDR, Main->Phdr);
    LX_AUX(LX_AT_PHENT, Main->PhEnt);
    LX_AUX(LX_AT_PHNUM, Main->PhNum);
    LX_AUX(LX_AT_PAGESZ, LX_PAGE_SIZE);
    LX_AUX(LX_AT_BASE, InterpreterBase);
    LX_AUX(LX_AT_FLAGS, 0);
    LX_AUX(LX_AT_ENTRY, Main->Entry);
    LX_AUX(LX_AT_UID, Process->Uid[0]);
    LX_AUX(LX_AT_EUID, Process->Uid[1]);
    LX_AUX(LX_AT_GID, Process->Gid[0]);
    LX_AUX(LX_AT_EGID, Process->Gid[1]);
    LX_AUX(LX_AT_SECURE, (Process->Uid[0] != Process->Uid[1]) || (Process->Gid[0] != Process->Gid[1]));
    LX_AUX(LX_AT_HWCAP, LxArchHwcap());
    LX_AUX(LX_AT_CLKTCK, 100);
    LX_AUX(LX_AT_RANDOM, RandomAddress);
    LX_AUX(LX_AT_PLATFORM, PlatformAddress);
    LX_AUX(LX_AT_EXECFN, ExecFnAddress);
    LX_AUX(LX_AT_NULL, 0);
#undef LX_AUX

    Words = 1 + Request->ArgCount + 1 + Request->EnvCount + 1 + AuxCount;
    Cursor = LX_ALIGN_DOWN(Cursor - (ULONG64)Words * sizeof(ULONG64), 16);

    Vector = LxAllocPaged((SIZE_T)Words * sizeof(ULONG64));
    if (Vector == NULL)
        return -LX_ENOMEM;

    Index = 0;
    Vector[Index++] = Request->ArgCount;
    for (Offset = 0; Offset < Request->ArgLength; Offset += (ULONG)strlen(Request->Args + Offset) + 1)
        Vector[Index++] = ArgAddress + Offset;
    Vector[Index++] = 0;
    for (Offset = 0; Offset < Request->EnvLength; Offset += (ULONG)strlen(Request->Env + Offset) + 1)
        Vector[Index++] = EnvAddress + Offset;
    Vector[Index++] = 0;
    RtlCopyMemory(&Vector[Index], Auxv, AuxCount * sizeof(ULONG64));

    Error = LxCopyToUser(ExecFnAddress, ExecFn, strlen(ExecFn) + 1);
    if (Error == 0)
        Error = LxCopyToUser(PlatformAddress, Platform, strlen(Platform) + 1);
    if (Error == 0)
        Error = LxCopyToUser(RandomAddress, Random, sizeof(Random));
    if (Error == 0)
        Error = LxCopyToUser(EnvAddress, Request->Env, Request->EnvLength);
    if (Error == 0)
        Error = LxCopyToUser(ArgAddress, Request->Args, Request->ArgLength);
    if (Error == 0)
        Error = LxCopyToUser(Cursor, Vector, (SIZE_T)Words * sizeof(ULONG64));

    LxFreePaged(Vector);
    if (Error != 0)
        return (LONG)Error;

    *StackPointer = Cursor;
    return 0;
}

static
ULONG64
LxMapSigTrampoline(
    _In_ PLX_PROCESS Process)
{
    UCHAR Code[16];
    ULONG Size = LxArchSigTrampoline(Code);
    LONG64 Base;

    Base = LxMmMap(Process, 0, LX_PAGE_SIZE, LX_PROT_READ | LX_PROT_WRITE, LX_MAP_PRIVATE, NULL, 0);
    if (Base < 0)
        return 0;
    if ((LxCopyToUser((ULONG64)Base, Code, Size) != 0) ||
        (LxMmProtect(Process, (ULONG64)Base, LX_PAGE_SIZE, LX_PROT_READ | LX_PROT_EXEC) != 0))
    {
        return 0;
    }
    return (ULONG64)Base;
}

NTSTATUS
LxExecImage(
    _In_ PLX_THREAD Thread,
    _In_ PLX_EXEC_REQUEST Request,
    _Inout_ PKTRAP_FRAME TrapFrame,
    _Inout_opt_ PKEXCEPTION_FRAME ExceptionFrame,
    _Out_ PLONG Errno)
{
    PLX_PROCESS Process = Thread->Process;
    LX_ELF_IMAGE Main = { 0 }, Interp = { 0 };
    LX_ELF_INFO MainInfo, InterpInfo;
    PSTR FileName = NULL, InterpreterPath = NULL, ExecPath = NULL;
    CHAR Shebang[LX_SHEBANG_MAX];
    ULONG64 StackPointer, Entry, Device;
    ULONG Depth, Mode, FileUid, FileGid;
    BOOLEAN SetUid = FALSE, SetGid = FALSE;
    LONG Error;
    LONG Sig;

    *Errno = 0;
    FileName = LxStrDup(Request->FileName);
    if (FileName == NULL)
    {
        *Errno = LX_ENOMEM;
        return STATUS_NO_MEMORY;
    }

    for (Depth = 0;; Depth++)
    {
        Error = LxElfOpen(Process, FileName, &Main, Shebang);
        if (Error != 1)
            break;
        if (Depth >= LX_MAX_INTERP_DEPTH)
        {
            Error = -LX_ELOOP;
            break;
        }
        Error = LxApplyShebang(Request, &FileName, Shebang);
        if (Error != 0)
            break;
    }
    if (Error == 0)
        Error = LxElfInterpreter(&Main, &InterpreterPath);
    if ((Error == 0) && (InterpreterPath != NULL))
    {
        Error = LxElfOpen(Process, InterpreterPath, &Interp, NULL);
        if ((Error == 0) && (Interp.Header.Type != LX_ET_DYN))
            Error = -LX_ELIBBAD;
    }
    if (Error == 0)
    {
        ExecPath = LxStrDup(Main.File->Path);
        if (ExecPath == NULL)
            Error = -LX_ENOMEM;
    }
    if ((Error == 0) && !Process->NoNewPrivs &&
        (LxNtReadLxAttributes(Main.File->Handle, &Mode, &FileUid, &FileGid, &Device) == 0) && (Mode != MAXULONG))
    {
        SetUid = ((Mode & LX_S_ISUID) != 0) && (FileUid != MAXULONG);
        SetGid = ((Mode & (LX_S_ISGID | LX_S_IXGRP)) == (LX_S_ISGID | LX_S_IXGRP)) && (FileGid != MAXULONG);
    }
    if (Error != 0)
    {
        LxElfClose(&Main);
        LxElfClose(&Interp);
        LxFree(FileName);
        LxFree(InterpreterPath);
        *Errno = -Error;
        switch (-Error)
        {
            case LX_ENOENT: return STATUS_OBJECT_NAME_NOT_FOUND;
            case LX_EACCES: return STATUS_ACCESS_DENIED;
            case LX_ENOMEM: return STATUS_NO_MEMORY;
            default: return STATUS_INVALID_IMAGE_FORMAT;
        }
    }

    LxFutexExitRobustList(Thread);
    LxMmReset(Process);
    LxFdCloseOnExec(Process);
    ExAcquireFastMutex(&Process->Lock);
    for (Sig = 0; Sig < LX_NSIG; Sig++)
    {
        if (Process->SigAction[Sig].Handler != LX_SIG_IGN)
            RtlZeroMemory(&Process->SigAction[Sig], sizeof(Process->SigAction[Sig]));
    }
    if ((SetUid && (FileUid != Process->Uid[1])) || (SetGid && (FileGid != Process->Gid[1])))
        Process->Dumpable = 0;
    if (SetUid || SetGid)
        Process->CapAmbient = 0;
    if (SetUid)
        Process->Uid[1] = FileUid;
    if (SetGid)
        Process->Gid[1] = FileGid;
    Process->Uid[2] = Process->Fsuid = Process->Uid[1];
    Process->Gid[2] = Process->Fsgid = Process->Gid[1];
    ExReleaseFastMutex(&Process->Lock);

    Error = LxElfMap(Process, &Main, LxArchImageBase(FALSE), &MainInfo);
    if ((Error == 0) && (Interp.File != NULL))
        Error = LxElfMap(Process, &Interp, LxArchImageBase(TRUE), &InterpInfo);
    if (Error == 0)
    {
        Process->BrkBase = LX_ALIGN_UP(MainInfo.ImageEnd, LX_ALLOC_GRANULARITY);
        Process->BrkCurrent = Process->BrkBase;
        Process->BrkCommitted = Process->BrkBase;
        Process->BrkLimit = Process->BrkBase;
        Process->BrkReserved = FALSE;
        Error = LxBuildStack(Process,
                             Request,
                             FileName,
                             &MainInfo,
                             (Interp.File != NULL) ? InterpInfo.LoadBias : 0,
                             &StackPointer);
    }

    if (Error == 0)
    {
        Process->SigTrampoline = LxMapSigTrampoline(Process);
        Entry = (Interp.File != NULL) ? InterpInfo.Entry : MainInfo.Entry;
        LxArchSetExecContext(TrapFrame, ExceptionFrame, Entry, StackPointer);
        LxArchSetThreadPointer(TrapFrame, 0);
        Thread->Tls = 0;
        Thread->ClearChildTid = 0;
        Thread->SetChildTid = 0;

        LxProcessSetCommand(Process, Request->Args, Request->ArgLength, Request->Env, Request->EnvLength, ExecPath, Request->FileName);
        LxPosixTimersRelease(Process, TRUE);
        Process->Execed = TRUE;
        RtlZeroMemory(&Thread->AltStack, sizeof(Thread->AltStack));
        Thread->AltStack.Flags = LX_SS_DISABLE;
        RtlCopyMemory(Thread->Comm, Process->Comm, sizeof(Thread->Comm));
    }
    else
    {
        DPRINT1("Linux program '%s' failed to load after its old image was released: errno %ld\n",
                FileName, -Error);
    }

    LxElfClose(&Main);
    LxElfClose(&Interp);
    LxFree(FileName);
    LxFree(InterpreterPath);
    LxFree(ExecPath);

    if (Error != 0)
    {
        *Errno = -Error;
        return STATUS_UNSUCCESSFUL;
    }

    KeSetEvent(&Process->VforkDone, IO_NO_INCREMENT, FALSE);
    return STATUS_SUCCESS;
}
