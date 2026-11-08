/*
 * PROJECT:     LiberNT Linux Subsystem
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     ELF core dumps of Linux processes killed by core-generating signals
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "lxcore.h"

#define NDEBUG
#include <debug.h>

#define LX_NT_PRSTATUS          1
#define LX_NT_PRPSINFO          3
#define LX_NT_AUXV              6
#define LX_CORE_CHUNK           (64 * 1024)
#define LX_CORE_PRSTATUS_COMMON 112
#define LX_CORE_PRPSINFO_SIZE   136

#define LX_FILTER_ANON_PRIVATE  0x01
#define LX_FILTER_ANON_SHARED   0x02
#define LX_FILTER_FILE_PRIVATE  0x04
#define LX_FILTER_FILE_SHARED   0x08
#define LX_FILTER_ELF_HEADERS   0x10

static CHAR LxCorePattern[256] = "core";
static KSPIN_LOCK LxCorePatternLock;

typedef struct _LX_CORE_SEGMENT
{
    ULONG64 Start;
    ULONG64 End;
    ULONG64 DumpSize;
    ULONG Prot;
} LX_CORE_SEGMENT, *PLX_CORE_SEGMENT;

typedef struct _LX_CORE_WRITER
{
    HANDLE Handle;
    ULONG64 Offset;
    ULONG64 Limit;
    BOOLEAN Failed;
} LX_CORE_WRITER, *PLX_CORE_WRITER;

VOID
LxCoreGetPattern(
    _Out_writes_(Size) PSTR Buffer,
    _In_ ULONG Size)
{
    KIRQL OldIrql;

    KeAcquireSpinLock(&LxCorePatternLock, &OldIrql);
    RtlStringCbCopyA(Buffer, Size, LxCorePattern);
    KeReleaseSpinLock(&LxCorePatternLock, OldIrql);
}

LONG
LxCoreSetPattern(
    _In_reads_bytes_(Length) PCSTR Data,
    _In_ ULONG Length)
{
    CHAR Pattern[sizeof(LxCorePattern)];
    KIRQL OldIrql;

    while ((Length != 0) && ((Data[Length - 1] == '\n') || (Data[Length - 1] == '\0')))
        Length--;
    if (Length >= sizeof(Pattern))
        return -LX_EINVAL;
    RtlCopyMemory(Pattern, Data, Length);
    Pattern[Length] = '\0';
    KeAcquireSpinLock(&LxCorePatternLock, &OldIrql);
    RtlCopyMemory(LxCorePattern, Pattern, Length + 1);
    KeReleaseSpinLock(&LxCorePatternLock, OldIrql);
    return 0;
}

BOOLEAN
LxSignalDumpsCore(
    _In_ LONG Signal)
{
    switch (Signal)
    {
        case LX_SIGQUIT:
        case LX_SIGILL:
        case LX_SIGTRAP:
        case LX_SIGABRT:
        case LX_SIGBUS:
        case LX_SIGFPE:
        case LX_SIGSEGV:
        case LX_SIGXCPU:
        case LX_SIGXFSZ:
        case LX_SIGSYS:
            return TRUE;
        default:
            return FALSE;
    }
}

static
BOOLEAN
LxCoreAppend(
    _Inout_ PSTR Name,
    _In_ ULONG Size,
    _Inout_ PULONG Used,
    _In_ PCSTR Text)
{
    ULONG Length = (ULONG)strlen(Text);

    if (*Used + Length >= Size)
        return FALSE;
    RtlCopyMemory(Name + *Used, Text, Length + 1);
    *Used += Length;
    return TRUE;
}

static
BOOLEAN
LxCoreExpandPattern(
    _In_ PLX_PROCESS Process,
    _In_ PLX_THREAD Thread,
    _In_ LONG Signal,
    _Out_writes_(Size) PSTR Name,
    _In_ ULONG Size)
{
    PLX_CRED Cred = LxProcessCred(Process);
    CHAR Pattern[sizeof(LxCorePattern)], Number[32];
    LARGE_INTEGER Now;
    ULONG Used = 0, Index;

    LxCoreGetPattern(Pattern, sizeof(Pattern));
    if ((Pattern[0] == '\0') || (Pattern[0] == '|'))
        return FALSE;
    Name[0] = '\0';
    for (Index = 0; Pattern[Index] != '\0'; Index++)
    {
        CHAR Plain[2] = { Pattern[Index], '\0' };
        PCSTR Text = Plain;

        if ((Pattern[Index] == '%') && (Pattern[Index + 1] != '\0'))
        {
            Index++;
            Number[0] = '\0';
            Text = Number;
            switch (Pattern[Index])
            {
                case '%': Text = "%"; break;
                case 'p': RtlStringCbPrintfA(Number, sizeof(Number), "%lu", LxPidNr(Process, Process)); break;
                case 'P': RtlStringCbPrintfA(Number, sizeof(Number), "%lu", Process->Pid); break;
                case 'i': RtlStringCbPrintfA(Number, sizeof(Number), "%lu", LxTidNr(Process, Thread)); break;
                case 'I': RtlStringCbPrintfA(Number, sizeof(Number), "%lu", Thread->Tid); break;
                case 'u': RtlStringCbPrintfA(Number, sizeof(Number), "%lu", Cred->Uid[0]); break;
                case 'g': RtlStringCbPrintfA(Number, sizeof(Number), "%lu", Cred->Gid[0]); break;
                case 's': RtlStringCbPrintfA(Number, sizeof(Number), "%ld", Signal); break;
                case 'd': RtlStringCbPrintfA(Number, sizeof(Number), "%lu", Process->Dumpable); break;
                case 'c':
                    RtlStringCbPrintfA(Number, sizeof(Number), "%llu", Process->Limits[LX_RLIMIT_CORE].Cur);
                    break;
                case 't':
                    KeQuerySystemTime(&Now);
                    RtlStringCbPrintfA(Number, sizeof(Number), "%llu",
                                       (ULONG64)(Now.QuadPart - 116444736000000000LL) / 10000000ULL);
                    break;
                case 'h': Text = LxProcessNs(Process, LxNsUts)->Uts.Hostname; break;
                case 'e': Text = Process->Comm; break;
                default: break;
            }
        }
        if (!LxCoreAppend(Name, Size, &Used, Text))
            return FALSE;
    }
    return Used != 0;
}

static
VOID
LxCoreWrite(
    _Inout_ PLX_CORE_WRITER Writer,
    _In_reads_bytes_(Length) const VOID *Data,
    _In_ ULONG Length)
{
    IO_STATUS_BLOCK Iosb;
    LARGE_INTEGER Position;

    if (Writer->Failed)
        return;
    if (Writer->Offset + Length > Writer->Limit)
    {
        Writer->Failed = TRUE;
        return;
    }
    Position.QuadPart = (LONG64)Writer->Offset;
    if (!NT_SUCCESS(ZwWriteFile(Writer->Handle, NULL, NULL, NULL, &Iosb, (PVOID)Data, Length, &Position, NULL)))
        Writer->Failed = TRUE;
    Writer->Offset += Length;
}

static
VOID
LxCoreNote(
    _Inout_ PLX_CORE_WRITER Writer,
    _In_ ULONG Type,
    _In_reads_bytes_(Length) const VOID *Data,
    _In_ ULONG Length)
{
    static const UCHAR Padding[4] = { 0 };
    ULONG Header[3] = { 5, Length, Type };

    LxCoreWrite(Writer, Header, sizeof(Header));
    LxCoreWrite(Writer, "CORE\0\0\0", 8);
    LxCoreWrite(Writer, Data, Length);
    if (Length & 3)
        LxCoreWrite(Writer, Padding, 4 - (Length & 3));
}

static
ULONG
LxCoreNoteSize(
    _In_ ULONG Length)
{
    return 12 + 8 + ((Length + 3) & ~3U);
}

static
ULONG64
LxCoreDumpSize(
    _In_ PLX_VMA Vma,
    _In_ ULONG Filter)
{
    LX_ELF64_EHDR Header;
    BOOLEAN Shared = (Vma->Flags & LX_MAP_SHARED) != 0;

    if ((Vma->Flags & LX_VMA_DONTDUMP) || !(Vma->Prot & LX_PROT_READ))
        return 0;
    if (Vma->File == NULL)
        return (Filter & (Shared ? LX_FILTER_ANON_SHARED : LX_FILTER_ANON_PRIVATE)) ? Vma->End - Vma->Start : 0;
    if (Shared)
        return (Filter & LX_FILTER_FILE_SHARED) ? Vma->End - Vma->Start : 0;
    if ((Vma->Flags & (LX_VMA_TOUCHED | LX_VMA_DIRTY)) && (Filter & LX_FILTER_ANON_PRIVATE))
        return Vma->End - Vma->Start;
    if (Filter & LX_FILTER_FILE_PRIVATE)
        return Vma->End - Vma->Start;
    if ((Filter & LX_FILTER_ELF_HEADERS) && (Vma->Offset == 0) &&
        (LxCopyFromUser(&Header, Vma->Start, sizeof(Header)) == 0) &&
        (Header.Ident[0] == 0x7f) && (Header.Ident[1] == 'E') && (Header.Ident[2] == 'L') && (Header.Ident[3] == 'F'))
    {
        return LX_PAGE_SIZE;
    }
    return 0;
}

static
VOID
LxCoreWriteMemory(
    _Inout_ PLX_CORE_WRITER Writer,
    _In_ ULONG64 Start,
    _In_ ULONG64 Size,
    _Inout_updates_bytes_(LX_CORE_CHUNK) PUCHAR Bounce)
{
    MEMORY_BASIC_INFORMATION Information;
    ULONG64 Cursor = Start, End = Start + Size;

    while ((Cursor < End) && !Writer->Failed)
    {
        ULONG64 RegionEnd = End, Chunk;
        BOOLEAN Committed = TRUE;

        if (NT_SUCCESS(ZwQueryVirtualMemory(NtCurrentProcess(), (PVOID)(ULONG_PTR)Cursor, MemoryBasicInformation,
                                            &Information, sizeof(Information), NULL)))
        {
            RegionEnd = min(End, (ULONG64)(ULONG_PTR)Information.BaseAddress + Information.RegionSize);
            Committed = (Information.State == MEM_COMMIT);
        }
        for (; (Cursor < RegionEnd) && !Writer->Failed; Cursor += Chunk)
        {
            Chunk = min(RegionEnd - Cursor, LX_CORE_CHUNK);
            if (!Committed)
            {
                if (Writer->Offset + Chunk > Writer->Limit)
                    Writer->Failed = TRUE;
                Writer->Offset += Chunk;
                continue;
            }
            if (LxCopyFromUser(Bounce, Cursor, (SIZE_T)Chunk) != 0)
            {
                ULONG64 Page;

                for (Page = 0; Page < Chunk; Page += LX_PAGE_SIZE)
                {
                    if (LxCopyFromUser(Bounce + Page, Cursor + Page, LX_PAGE_SIZE) != 0)
                        RtlZeroMemory(Bounce + Page, LX_PAGE_SIZE);
                }
            }
            LxCoreWrite(Writer, Bounce, (ULONG)Chunk);
        }
    }
}

BOOLEAN
LxCoreDump(
    _In_ PLX_THREAD Thread,
    _In_ PKTRAP_FRAME TrapFrame,
    _In_opt_ PKEXCEPTION_FRAME ExceptionFrame,
    _In_ LONG Signal)
{
    PLX_PROCESS Process = Thread->Process, Mm = Process->Mm;
    PLX_CRED Cred = LxProcessCred(Process);
    ULONG64 Registers[LX_CORE_MAX_REGISTERS], Limit = Process->Limits[LX_RLIMIT_CORE].Cur;
    UCHAR Status[LX_CORE_PRSTATUS_COMMON + sizeof(Registers) + 8];
    UCHAR Info[LX_CORE_PRPSINFO_SIZE];
    ULONG RegisterCount, StatusSize, AuxvSize = 0, Count = 0, Index, Filter = Process->CoreFilter;
    PLX_CORE_SEGMENT Segments = NULL;
    LX_CORE_WRITER Writer;
    LX_ELF64_EHDR Header;
    LX_ELF64_PHDR Phdr;
    PLIST_ENTRY Entry;
    PLX_FILE File = NULL;
    PUCHAR Bounce = NULL;
    CHAR Name[LX_PATH_MAX];
    ULONG64 DataOffset;
    BOOLEAN Dumped = FALSE;

    if ((Limit < LX_PAGE_SIZE) || (Process->Dumpable == 0) || (Mm != Process) ||
        !LxCoreExpandPattern(Process, Thread, Signal, Name, sizeof(Name)))
    {
        return FALSE;
    }
    if (LxVfsOpen(Process, LX_AT_FDCWD, Name, LX_O_WRONLY | LX_O_CREAT | LX_O_TRUNC, 0600, &File) != 0)
        return FALSE;
    if ((File->Handle == NULL) || File->Directory)
        goto Exit;

    Bounce = LxAllocPaged(LX_CORE_CHUNK);
    if (Bounce == NULL)
        goto Exit;

    LxMmLock(Mm);
    Segments = LxAlloc((SIZE_T)(Mm->VmaCount + 1) * sizeof(*Segments));
    if (Segments == NULL)
    {
        LxMmUnlock(Mm);
        goto Exit;
    }
    for (Entry = Mm->Vmas.Flink; (Entry != &Mm->Vmas) && (Count < (ULONG)Mm->VmaCount); Entry = Entry->Flink)
    {
        PLX_VMA Vma = CONTAINING_RECORD(Entry, LX_VMA, Link);

        Segments[Count].Start = Vma->Start;
        Segments[Count].End = Vma->End;
        Segments[Count].Prot = Vma->Prot;
        Segments[Count].DumpSize = LxCoreDumpSize(Vma, Filter);
        Count++;
    }

    while ((AuxvSize < LX_AUXV_WORDS) && (Process->Auxv[AuxvSize] != LX_AT_NULL))
        AuxvSize += 2;
    AuxvSize = min(AuxvSize + 2, LX_AUXV_WORDS) * sizeof(ULONG64);

    RegisterCount = LxArchCoreRegisters(TrapFrame, ExceptionFrame, Registers);
    StatusSize = (LX_CORE_PRSTATUS_COMMON + RegisterCount * sizeof(ULONG64) + sizeof(ULONG) + 7) & ~7U;
    RtlZeroMemory(Status, sizeof(Status));
    *(PLONG)&Status[0] = Signal;
    *(PUSHORT)&Status[12] = (USHORT)Signal;
    *(PULONG64)&Status[16] = (ULONG64)Process->Pending | (ULONG64)Thread->Pending;
    *(PULONG64)&Status[24] = Thread->SigMask;
    *(PULONG)&Status[32] = LxTidNr(Process, Thread);
    *(PULONG)&Status[36] = LxPidNsIsInit(Process) ? 0 : LxGlobalToNr(Process, Process->ParentPid);
    *(PULONG)&Status[40] = LxGlobalToNr(Process, Process->Pgid);
    *(PULONG)&Status[44] = LxGlobalToNr(Process, Process->Sid);
    RtlCopyMemory(&Status[LX_CORE_PRSTATUS_COMMON], Registers, RegisterCount * sizeof(ULONG64));

    RtlZeroMemory(Info, sizeof(Info));
    Info[0] = 0;
    Info[1] = 'R';
    *(PULONG)&Info[16] = Cred->Uid[0];
    *(PULONG)&Info[20] = Cred->Gid[0];
    *(PULONG)&Info[24] = LxPidNr(Process, Process);
    *(PULONG)&Info[28] = *(PULONG)&Status[36];
    *(PULONG)&Info[32] = *(PULONG)&Status[40];
    *(PULONG)&Info[36] = *(PULONG)&Status[44];
    RtlCopyMemory(&Info[40], Process->Comm, min(sizeof(Process->Comm), 16));
    RtlStringCbCopyA((PSTR)&Info[56], 80, Process->Comm);

    Writer.Handle = File->Handle;
    Writer.Offset = 0;
    Writer.Limit = Limit;
    Writer.Failed = FALSE;

    RtlZeroMemory(&Header, sizeof(Header));
    Header.Ident[0] = 0x7f;
    Header.Ident[1] = 'E';
    Header.Ident[2] = 'L';
    Header.Ident[3] = 'F';
    Header.Ident[4] = LX_ELFCLASS64;
    Header.Ident[5] = LX_ELFDATA2LSB;
    Header.Ident[6] = 1;
    Header.Type = LX_ET_CORE;
    Header.Machine = (USHORT)LxArchElfMachine();
    Header.Version = 1;
    Header.PhOff = sizeof(Header);
    Header.EhSize = sizeof(Header);
    Header.PhEntSize = sizeof(LX_ELF64_PHDR);
    Header.PhNum = (USHORT)min(Count + 1, 0xFFFF);
    LxCoreWrite(&Writer, &Header, sizeof(Header));

    RtlZeroMemory(&Phdr, sizeof(Phdr));
    Phdr.Type = LX_PT_NOTE;
    Phdr.Offset = sizeof(Header) + (ULONG64)Header.PhNum * sizeof(LX_ELF64_PHDR);
    Phdr.FileSz = LxCoreNoteSize(StatusSize) + LxCoreNoteSize(sizeof(Info)) + LxCoreNoteSize(AuxvSize);
    Phdr.Align = 4;
    LxCoreWrite(&Writer, &Phdr, sizeof(Phdr));

    DataOffset = LX_ALIGN_UP(Phdr.Offset + Phdr.FileSz, LX_PAGE_SIZE);
    for (Index = 0; (Index < Count) && (Index + 1 < Header.PhNum); Index++)
    {
        RtlZeroMemory(&Phdr, sizeof(Phdr));
        Phdr.Type = LX_PT_LOAD;
        Phdr.Flags = ((Segments[Index].Prot & LX_PROT_READ) ? LX_PF_R : 0) |
                     ((Segments[Index].Prot & LX_PROT_WRITE) ? LX_PF_W : 0) |
                     ((Segments[Index].Prot & LX_PROT_EXEC) ? LX_PF_X : 0);
        Phdr.Offset = DataOffset;
        Phdr.VAddr = Segments[Index].Start;
        Phdr.FileSz = Segments[Index].DumpSize;
        Phdr.MemSz = Segments[Index].End - Segments[Index].Start;
        Phdr.Align = LX_PAGE_SIZE;
        LxCoreWrite(&Writer, &Phdr, sizeof(Phdr));
        DataOffset += Segments[Index].DumpSize;
    }

    LxCoreNote(&Writer, LX_NT_PRSTATUS, Status, StatusSize);
    LxCoreNote(&Writer, LX_NT_PRPSINFO, Info, sizeof(Info));
    LxCoreNote(&Writer, LX_NT_AUXV, Process->Auxv, AuxvSize);

    Writer.Offset = LX_ALIGN_UP(Writer.Offset, LX_PAGE_SIZE);
    for (Index = 0; (Index < Count) && (Index + 1 < Header.PhNum) && !Writer.Failed; Index++)
    {
        if (Segments[Index].DumpSize != 0)
            LxCoreWriteMemory(&Writer, Segments[Index].Start, Segments[Index].DumpSize, Bounce);
    }
    LxMmUnlock(Mm);
    Dumped = (Writer.Offset > sizeof(Header));

Exit:
    LxFree(Segments);
    if (Bounce != NULL)
        LxFreePaged(Bounce);
    LxFileDereference(File);
    return Dumped;
}
