/*
 * PROJECT:     LiberNT AMD64 emulation host
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Translation cache of guest code kept between runs of the same program
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "../amd64emu.h"
#include <ntstrsafe.h>
#include "bridge.h"

#define F86C_MAGIC 0x43363846UL
#define F86C_MODULES 64
#define F86C_FLUSH_BYTES (256 * 1024)
#define F86C_MAX_HOST (64 * 1024)
#define F86C_FOREIGN 16

typedef struct _F86C_RECORD
{
    ULONG Magic;
    ULONG Size;
    ULONG Rva;
    ULONG GuestSize;
    ULONG HostSize;
    USHORT Instructions;
    USHORT Literals;
    USHORT Links;
    USHORT HostAlign;
    ULONG Checksum;
} F86C_RECORD, *PF86C_RECORD;

typedef struct _F86C_MODULE
{
    ULONG_PTR Base;
    ULONG_PTR End;
    ULONG64 Key;
    BOOLEAN Loaded;
    BOOLEAN Disabled;
    ULONG IndexSize;
    PF86C_RECORD *Index;
    PUCHAR Buffer;
    ULONG Used;
    ULONG Capacity;
    HANDLE File;
    ULONG ExecutableCount;
    ULONG_PTR ExecutableStart[16];
    ULONG_PTR ExecutableEnd[16];
} F86C_MODULE, *PF86C_MODULE;

static F86C_MODULE F86cModules[F86C_MODULES];
static ULONG F86cModuleCount;
static RTL_SRWLOCK F86cLock = RTL_SRWLOCK_INIT;
static LONG F86cState;
static ULONG64 F86cHostKey;
static ULONG_PTR F86cHostBase;
static ULONG_PTR F86cHostEnd;
static UNICODE_STRING F86cDirectory;
static ULONG_PTR F86cForeignStart[F86C_FOREIGN];
static ULONG_PTR F86cForeignEnd[F86C_FOREIGN];
static ULONG F86cForeignNext;

static
ULONG64
F86cHash(
    _In_ ULONG64 Hash,
    _In_reads_bytes_(Length) const VOID *Data,
    _In_ SIZE_T Length)
{
    const UCHAR *Bytes = Data;
    SIZE_T Index;

    for (Index = 0; Index < Length; Index++)
    {
        Hash ^= Bytes[Index];
        Hash *= 0x100000001B3ULL;
    }
    return Hash;
}

static
ULONG
F86cAlign(
    _In_ ULONG Value)
{
    return (Value + 7) & ~7UL;
}

static
ULONG
F86cPayloadSize(
    _In_ ULONG HostSize,
    _In_ ULONG Instructions,
    _In_ ULONG Literals,
    _In_ ULONG Links)
{
    return F86cAlign(HostSize) + F86cAlign(Instructions * sizeof(USHORT)) + F86cAlign(Literals * sizeof(ULONG)) +
           Literals * sizeof(ULONG64) + F86cAlign(Links * sizeof(ULONG)) + Links * sizeof(LONG64) + F86cAlign(Links);
}

static
BOOLEAN
F86cCreateDirectory(
    _In_ PUNICODE_STRING Path)
{
    OBJECT_ATTRIBUTES Attributes;
    IO_STATUS_BLOCK Io;
    HANDLE Handle;
    NTSTATUS Status;

    InitializeObjectAttributes(&Attributes, Path, OBJ_CASE_INSENSITIVE, NULL, NULL);
    Status = NtCreateFile(&Handle, FILE_LIST_DIRECTORY | SYNCHRONIZE, &Attributes, &Io, NULL, FILE_ATTRIBUTE_DIRECTORY,
                          FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, FILE_OPEN_IF,
                          FILE_DIRECTORY_FILE | FILE_SYNCHRONOUS_IO_NONALERT, NULL, 0);
    if (!NT_SUCCESS(Status))
        return FALSE;
    NtClose(Handle);
    return TRUE;
}

static
BOOLEAN
F86cInitialize(VOID)
{
    UNICODE_STRING Name = RTL_CONSTANT_STRING(L"LOCALAPPDATA");
    UNICODE_STRING Value, DosPath, NtPath;
    WCHAR Buffer[MAX_PATH], Path[MAX_PATH + 40];
    PIMAGE_NT_HEADERS Headers;
    PIMAGE_SECTION_HEADER Section;
    PVOID HostBase = NULL;
    ULONG Index;
    WCHAR Flag[4];
    UNICODE_STRING FlagName = RTL_CONSTANT_STRING(L"FELIX86_CACHE");
    UNICODE_STRING FlagValue = { 0, sizeof(Flag), Flag };

    if (!NT_SUCCESS(RtlQueryEnvironmentVariable_U(NULL, &FlagName, &FlagValue)) || FlagValue.Length != sizeof(WCHAR) ||
        Flag[0] != L'1')
    {
        return FALSE;
    }

    Value.Buffer = Buffer;
    Value.Length = 0;
    Value.MaximumLength = sizeof(Buffer);
    if (!NT_SUCCESS(RtlQueryEnvironmentVariable_U(NULL, &Name, &Value)))
        return FALSE;
    if (!NT_SUCCESS(RtlStringCbPrintfW(Path, sizeof(Path), L"%wZ\\LiberNT", &Value)))
        return FALSE;
    RtlInitUnicodeString(&DosPath, Path);
    if (!RtlDosPathNameToNtPathName_U(DosPath.Buffer, &NtPath, NULL, NULL))
        return FALSE;
    F86cCreateDirectory(&NtPath);
    RtlFreeUnicodeString(&NtPath);
    if (!NT_SUCCESS(RtlStringCbCatW(Path, sizeof(Path), L"\\X64Cache")))
        return FALSE;
    if (!RtlDosPathNameToNtPathName_U(Path, &F86cDirectory, NULL, NULL))
        return FALSE;
    if (!F86cCreateDirectory(&F86cDirectory))
        return FALSE;

    if (!RtlPcToFileHeader((PVOID)F86cInitialize, &HostBase) || !HostBase)
        return FALSE;
    Headers = RtlImageNtHeader(HostBase);
    if (!Headers)
        return FALSE;
    F86cHostBase = (ULONG_PTR)HostBase;
    F86cHostEnd = F86cHostBase + Headers->OptionalHeader.SizeOfImage;
    F86cHostKey = 0xCBF29CE484222325ULL;
    Section = IMAGE_FIRST_SECTION(Headers);
    for (Index = 0; Index < Headers->FileHeader.NumberOfSections; Index++)
    {
        if (Section[Index].Characteristics & IMAGE_SCN_MEM_EXECUTE)
        {
            F86cHostKey = F86cHash(F86cHostKey, (PUCHAR)HostBase + Section[Index].VirtualAddress,
                                   Section[Index].Misc.VirtualSize);
        }
    }
    Index = Felix86NtProbeExtensions();
    F86cHostKey = F86cHash(F86cHostKey, &Index, sizeof(Index));
    Index = Felix86NtVectorBytes();
    F86cHostKey = F86cHash(F86cHostKey, &Index, sizeof(Index));
    return TRUE;
}

static
BOOLEAN
F86cEnabled(VOID)
{
    LONG State = ReadAcquire(&F86cState);

    if (State == 0)
    {
        RtlAcquireSRWLockExclusive(&F86cLock);
        State = F86cState;
        if (State == 0)
        {
            State = F86cInitialize() ? 1 : -1;
            WriteRelease(&F86cState, State);
        }
        RtlReleaseSRWLockExclusive(&F86cLock);
    }
    return State > 0;
}

static
VOID
F86cInsert(
    _Inout_ PF86C_MODULE Module,
    _In_ PF86C_RECORD Record)
{
    ULONG Slot = (Record->Rva * 0x9E3779B1UL) & (Module->IndexSize - 1);

    while (Module->Index[Slot])
    {
        if (Module->Index[Slot]->Rva == Record->Rva)
            return;
        Slot = (Slot + 1) & (Module->IndexSize - 1);
    }
    Module->Index[Slot] = Record;
}

static
BOOLEAN
F86cRecordValid(
    _In_ PF86C_RECORD Record,
    _In_ SIZE_T Available)
{
    return Available >= sizeof(*Record) && Record->Magic == F86C_MAGIC && !(Record->Size & 7) &&
           Record->Size >= sizeof(*Record) && Record->Size <= Available && Record->HostSize <= F86C_MAX_HOST &&
           sizeof(*Record) + F86cPayloadSize(Record->HostSize, Record->Instructions, Record->Literals, Record->Links) <=
               Record->Size;
}

static
VOID
F86cLoadModule(
    _Inout_ PF86C_MODULE Module)
{
    OBJECT_ATTRIBUTES Attributes;
    IO_STATUS_BLOCK Io;
    HANDLE Directory, File, Section;
    WCHAR Mask[32];
    UNICODE_STRING MaskString, FileName;
    UCHAR Buffer[4096];
    PFILE_DIRECTORY_INFORMATION Entry;
    BOOLEAN Restart = TRUE;
    PVOID Views[64];
    SIZE_T Sizes[64];
    ULONG ViewCount = 0, Records = 0, Index;
    NTSTATUS Status;

    Module->Loaded = TRUE;
    InitializeObjectAttributes(&Attributes, &F86cDirectory, OBJ_CASE_INSENSITIVE, NULL, NULL);
    if (!NT_SUCCESS(NtOpenFile(&Directory, FILE_LIST_DIRECTORY | SYNCHRONIZE, &Attributes, &Io,
                               FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                               FILE_DIRECTORY_FILE | FILE_SYNCHRONOUS_IO_NONALERT)))
    {
        return;
    }

    RtlStringCbPrintfW(Mask, sizeof(Mask), L"%016I64x-*.f86", Module->Key);
    RtlInitUnicodeString(&MaskString, Mask);
    for (;;)
    {
        Status = NtQueryDirectoryFile(Directory, NULL, NULL, NULL, &Io, Buffer, sizeof(Buffer),
                                      FileDirectoryInformation, FALSE, &MaskString, Restart);
        Restart = FALSE;
        if (!NT_SUCCESS(Status))
            break;
        for (Entry = (PFILE_DIRECTORY_INFORMATION)Buffer;; Entry = (PFILE_DIRECTORY_INFORMATION)((PUCHAR)Entry + Entry->NextEntryOffset))
        {
            if (ViewCount < RTL_NUMBER_OF(Views) && Entry->EndOfFile.QuadPart > 0)
            {
                FileName.Buffer = Entry->FileName;
                FileName.Length = FileName.MaximumLength = (USHORT)Entry->FileNameLength;
                InitializeObjectAttributes(&Attributes, &FileName, OBJ_CASE_INSENSITIVE, Directory, NULL);
                if (NT_SUCCESS(NtOpenFile(&File, GENERIC_READ | SYNCHRONIZE, &Attributes, &Io,
                                          FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                          FILE_SYNCHRONOUS_IO_NONALERT | FILE_NON_DIRECTORY_FILE)))
                {
                    if (NT_SUCCESS(NtCreateSection(&Section, SECTION_MAP_READ, NULL, NULL, PAGE_READONLY, SEC_COMMIT, File)))
                    {
                        Views[ViewCount] = NULL;
                        Sizes[ViewCount] = 0;
                        if (NT_SUCCESS(NtMapViewOfSection(Section, NtCurrentProcess(), &Views[ViewCount], 0, 0, NULL,
                                                          &Sizes[ViewCount], ViewUnmap, 0, PAGE_READONLY)))
                        {
                            if (Sizes[ViewCount] > (SIZE_T)Entry->EndOfFile.QuadPart)
                                Sizes[ViewCount] = (SIZE_T)Entry->EndOfFile.QuadPart;
                            ViewCount++;
                        }
                        NtClose(Section);
                    }
                    NtClose(File);
                }
            }
            if (!Entry->NextEntryOffset)
                break;
        }
    }
    NtClose(Directory);

    for (Index = 0; Index < ViewCount; Index++)
    {
        SIZE_T Offset = 0;

        while (F86cRecordValid((PF86C_RECORD)((PUCHAR)Views[Index] + Offset), Sizes[Index] - Offset))
        {
            Records++;
            Offset += ((PF86C_RECORD)((PUCHAR)Views[Index] + Offset))->Size;
        }
    }
    if (!Records)
        return;

    for (Module->IndexSize = 1024; Module->IndexSize < Records * 2; Module->IndexSize <<= 1)
        ;
    Module->Index = RtlAllocateHeap(RtlGetProcessHeap(), HEAP_ZERO_MEMORY, Module->IndexSize * sizeof(PF86C_RECORD));
    if (!Module->Index)
        return;

    for (Index = 0; Index < ViewCount; Index++)
    {
        SIZE_T Offset = 0;

        while (F86cRecordValid((PF86C_RECORD)((PUCHAR)Views[Index] + Offset), Sizes[Index] - Offset))
        {
            PF86C_RECORD Record = (PF86C_RECORD)((PUCHAR)Views[Index] + Offset);

            F86cInsert(Module, Record);
            Offset += Record->Size;
        }
    }
}

static
PF86C_MODULE
F86cModuleFor(
    _In_ ULONG_PTR Address)
{
    MEMORY_BASIC_INFORMATION Information;
    PIMAGE_NT_HEADERS Headers;
    PIMAGE_SECTION_HEADER Section;
    PF86C_MODULE Module = NULL;
    ULONG_PTR Base;
    ULONG Index, SizeOfImage, CheckSum;
    ULONG64 Key;

    RtlAcquireSRWLockShared(&F86cLock);
    for (Index = 0; Index < F86cModuleCount; Index++)
    {
        if (Address >= F86cModules[Index].Base && Address < F86cModules[Index].End)
        {
            Module = &F86cModules[Index];
            break;
        }
    }
    for (Index = 0; !Module && Index < F86C_FOREIGN; Index++)
    {
        if (Address >= F86cForeignStart[Index] && Address < F86cForeignEnd[Index])
        {
            RtlReleaseSRWLockShared(&F86cLock);
            return NULL;
        }
    }
    RtlReleaseSRWLockShared(&F86cLock);
    if (Module)
        return Module->Disabled ? NULL : Module;

    if (!NT_SUCCESS(NtQueryVirtualMemory(NtCurrentProcess(), (PVOID)Address, MemoryBasicInformation, &Information,
                                         sizeof(Information), NULL)))
    {
        return NULL;
    }
    if (Information.Type != MEM_IMAGE || !Information.AllocationBase)
    {
        RtlAcquireSRWLockExclusive(&F86cLock);
        F86cForeignStart[F86cForeignNext] = (ULONG_PTR)Information.BaseAddress;
        F86cForeignEnd[F86cForeignNext] = (ULONG_PTR)Information.BaseAddress + Information.RegionSize;
        F86cForeignNext = (F86cForeignNext + 1) % F86C_FOREIGN;
        RtlReleaseSRWLockExclusive(&F86cLock);
        return NULL;
    }

    Base = (ULONG_PTR)Information.AllocationBase;
    Headers = RtlImageNtHeader((PVOID)Base);
    if (!Headers)
        return NULL;
    if (Headers->OptionalHeader.Magic == IMAGE_NT_OPTIONAL_HDR64_MAGIC)
    {
        SizeOfImage = ((PIMAGE_NT_HEADERS64)Headers)->OptionalHeader.SizeOfImage;
        CheckSum = ((PIMAGE_NT_HEADERS64)Headers)->OptionalHeader.CheckSum;
    }
    else
    {
        SizeOfImage = ((PIMAGE_NT_HEADERS32)Headers)->OptionalHeader.SizeOfImage;
        CheckSum = ((PIMAGE_NT_HEADERS32)Headers)->OptionalHeader.CheckSum;
    }

    Key = F86cHash(F86cHostKey, &Base, sizeof(Base));
    Key = F86cHash(Key, &SizeOfImage, sizeof(SizeOfImage));
    Key = F86cHash(Key, &CheckSum, sizeof(CheckSum));
    Key = F86cHash(Key, &Headers->FileHeader.TimeDateStamp, sizeof(Headers->FileHeader.TimeDateStamp));
    Key = F86cHash(Key, &Headers->FileHeader.Machine, sizeof(Headers->FileHeader.Machine));

    RtlAcquireSRWLockExclusive(&F86cLock);
    for (Index = 0; Index < F86cModuleCount; Index++)
    {
        if (F86cModules[Index].Base == Base)
        {
            Module = &F86cModules[Index];
            break;
        }
    }
    if (!Module && F86cModuleCount < F86C_MODULES)
    {
        Module = &F86cModules[F86cModuleCount];
        RtlZeroMemory(Module, sizeof(*Module));
        Module->Base = Base;
        Module->End = Base + SizeOfImage;
        Module->Key = Key;
        Section = IMAGE_FIRST_SECTION(Headers);
        for (Index = 0; Index < Headers->FileHeader.NumberOfSections && Module->ExecutableCount < 16; Index++)
        {
            if (Section[Index].Characteristics & IMAGE_SCN_MEM_EXECUTE)
            {
                Module->ExecutableStart[Module->ExecutableCount] = Base + Section[Index].VirtualAddress;
                Module->ExecutableEnd[Module->ExecutableCount] = Base + Section[Index].VirtualAddress +
                                                                 Section[Index].Misc.VirtualSize;
                Module->ExecutableCount++;
            }
        }
        F86cLoadModule(Module);
        F86cModuleCount++;
    }
    RtlReleaseSRWLockExclusive(&F86cLock);

    return (Module && !Module->Disabled) ? Module : NULL;
}

static
VOID
F86cFlushModule(
    _Inout_ PF86C_MODULE Module)
{
    OBJECT_ATTRIBUTES Attributes;
    IO_STATUS_BLOCK Io;
    WCHAR Name[48];
    UNICODE_STRING FileName;
    HANDLE Directory;

    if (!Module->Used)
        return;

    if (!Module->File)
    {
        InitializeObjectAttributes(&Attributes, &F86cDirectory, OBJ_CASE_INSENSITIVE, NULL, NULL);
        if (NT_SUCCESS(NtOpenFile(&Directory, FILE_TRAVERSE | SYNCHRONIZE, &Attributes, &Io,
                                  FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                  FILE_DIRECTORY_FILE | FILE_SYNCHRONOUS_IO_NONALERT)))
        {
            RtlStringCbPrintfW(Name, sizeof(Name), L"%016I64x-%lu.f86", Module->Key,
                               HandleToUlong(NtCurrentTeb()->ClientId.UniqueProcess));
            RtlInitUnicodeString(&FileName, Name);
            InitializeObjectAttributes(&Attributes, &FileName, OBJ_CASE_INSENSITIVE, Directory, NULL);
            if (!NT_SUCCESS(NtCreateFile(&Module->File, FILE_APPEND_DATA | SYNCHRONIZE, &Attributes, &Io, NULL,
                                         FILE_ATTRIBUTE_NORMAL, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                         FILE_OPEN_IF, FILE_SYNCHRONOUS_IO_NONALERT | FILE_NON_DIRECTORY_FILE, NULL, 0)))
            {
                Module->File = NULL;
            }
            NtClose(Directory);
        }
        if (!Module->File)
        {
            Module->Used = 0;
            return;
        }
    }

    NtWriteFile(Module->File, NULL, NULL, NULL, &Io, Module->Buffer, Module->Used, NULL, NULL);
    Module->Used = 0;
}

int
Felix86NtCacheLookup(
    uint64_t Rip,
    FELIX86_NT_CACHED_BLOCK *Block)
{
    PF86C_MODULE Module;
    PF86C_RECORD Record;
    PUCHAR Payload;
    ULONG Slot, Index, Rva;
    ULONG64 Checksum;
    const ULONG64 *Values;
    const LONG64 *Targets;

    if (!F86cEnabled())
        return 0;
    Module = F86cModuleFor((ULONG_PTR)Rip);
    if (!Module || !Module->Index)
        return 0;

    Rva = (ULONG)((ULONG_PTR)Rip - Module->Base);
    Slot = (Rva * 0x9E3779B1UL) & (Module->IndexSize - 1);
    for (;;)
    {
        Record = Module->Index[Slot];
        if (!Record)
            return 0;
        if (Record->Rva == Rva)
            break;
        Slot = (Slot + 1) & (Module->IndexSize - 1);
    }
    if ((ULONG_PTR)Rip + Record->GuestSize > Module->End)
        return 0;

    Payload = (PUCHAR)(Record + 1);
    Checksum = F86cHash(0xCBF29CE484222325ULL, Payload,
                        F86cPayloadSize(Record->HostSize, Record->Instructions, Record->Literals, Record->Links));
    if ((ULONG)Checksum != Record->Checksum)
        return 0;

    Block->Host = Payload;
    Payload += F86cAlign(Record->HostSize);
    Block->Sizes = (const uint16_t *)Payload;
    Payload += F86cAlign(Record->Instructions * sizeof(USHORT));
    Block->LiteralOffsets = (const uint32_t *)Payload;
    Payload += F86cAlign(Record->Literals * sizeof(ULONG));
    Values = (const ULONG64 *)Payload;
    Payload += Record->Literals * sizeof(ULONG64);
    Block->LinkOffsets = (const uint32_t *)Payload;
    Payload += F86cAlign(Record->Links * sizeof(ULONG));
    Targets = (const LONG64 *)Payload;
    Payload += Record->Links * sizeof(LONG64);
    Block->LinkFlags = Payload;

    if (Record->Literals > FELIX86_NT_CACHE_MAX_LITERALS || Record->Links > FELIX86_NT_CACHE_MAX_LINKS)
        return 0;
    for (Index = 0; Index < Record->Literals; Index++)
    {
        if (Values[Index] >= F86cHostEnd - F86cHostBase)
            return 0;
        Block->LiteralValues[Index] = F86cHostBase + Values[Index];
    }
    for (Index = 0; Index < Record->Links; Index++)
        Block->LinkTargets[Index] = Rip + Targets[Index];

    Block->GuestSize = Record->GuestSize;
    Block->HostSize = Record->HostSize;
    Block->HostAlign = Record->HostAlign;
    Block->Instructions = Record->Instructions;
    Block->Literals = Record->Literals;
    Block->Links = Record->Links;
    return 1;
}

void
Felix86NtCacheRecord(
    uint64_t Rip,
    const FELIX86_NT_CACHED_BLOCK *Block)
{
    PF86C_MODULE Module;
    F86C_RECORD Record;
    ULONG Payload, Index, Needed;
    PUCHAR Out;
    ULONG64 Value;
    LONG64 Target;

    if (!F86cEnabled() || Block->HostSize > F86C_MAX_HOST || Block->Instructions > 0xFFFF ||
        Block->Literals > FELIX86_NT_CACHE_MAX_LITERALS || Block->Links > FELIX86_NT_CACHE_MAX_LINKS)
    {
        return;
    }
    Module = F86cModuleFor((ULONG_PTR)Rip);
    if (!Module || (ULONG_PTR)Rip + Block->GuestSize > Module->End)
        return;
    for (Index = 0; Index < Block->Literals; Index++)
    {
        if (Block->LiteralValues[Index] < F86cHostBase || Block->LiteralValues[Index] >= F86cHostEnd)
            return;
    }

    Payload = F86cPayloadSize(Block->HostSize, Block->Instructions, Block->Literals, Block->Links);
    Needed = sizeof(Record) + Payload;

    RtlAcquireSRWLockExclusive(&F86cLock);
    if (Module->Disabled)
    {
        RtlReleaseSRWLockExclusive(&F86cLock);
        return;
    }
    if (Module->Used + Needed > Module->Capacity)
    {
        ULONG Capacity = max(F86C_FLUSH_BYTES * 2, Module->Used + Needed);
        PUCHAR Buffer = Module->Buffer ? RtlReAllocateHeap(RtlGetProcessHeap(), 0, Module->Buffer, Capacity)
                                       : RtlAllocateHeap(RtlGetProcessHeap(), 0, Capacity);

        if (!Buffer)
        {
            RtlReleaseSRWLockExclusive(&F86cLock);
            return;
        }
        Module->Buffer = Buffer;
        Module->Capacity = Capacity;
    }

    Out = Module->Buffer + Module->Used + sizeof(Record);
    RtlZeroMemory(Out, Payload);
    RtlCopyMemory(Out, Block->Host, Block->HostSize);
    Out += F86cAlign(Block->HostSize);
    RtlCopyMemory(Out, Block->Sizes, Block->Instructions * sizeof(USHORT));
    Out += F86cAlign(Block->Instructions * sizeof(USHORT));
    RtlCopyMemory(Out, Block->LiteralOffsets, Block->Literals * sizeof(ULONG));
    Out += F86cAlign(Block->Literals * sizeof(ULONG));
    for (Index = 0; Index < Block->Literals; Index++)
    {
        Value = Block->LiteralValues[Index] - F86cHostBase;
        RtlCopyMemory(Out, &Value, sizeof(Value));
        Out += sizeof(Value);
    }
    RtlCopyMemory(Out, Block->LinkOffsets, Block->Links * sizeof(ULONG));
    Out += F86cAlign(Block->Links * sizeof(ULONG));
    for (Index = 0; Index < Block->Links; Index++)
    {
        Target = (LONG64)(Block->LinkTargets[Index] - Rip);
        RtlCopyMemory(Out, &Target, sizeof(Target));
        Out += sizeof(Target);
    }
    RtlCopyMemory(Out, Block->LinkFlags, Block->Links);

    Record.Magic = F86C_MAGIC;
    Record.Size = Needed;
    Record.Rva = (ULONG)((ULONG_PTR)Rip - Module->Base);
    Record.GuestSize = Block->GuestSize;
    Record.HostSize = Block->HostSize;
    Record.Instructions = (USHORT)Block->Instructions;
    Record.Literals = (USHORT)Block->Literals;
    Record.Links = (USHORT)Block->Links;
    Record.HostAlign = (USHORT)Block->HostAlign;
    Record.Checksum = (ULONG)F86cHash(0xCBF29CE484222325ULL, Module->Buffer + Module->Used + sizeof(Record), Payload);
    RtlCopyMemory(Module->Buffer + Module->Used, &Record, sizeof(Record));
    Module->Used += Needed;

    if (Module->Used >= F86C_FLUSH_BYTES)
        F86cFlushModule(Module);
    RtlReleaseSRWLockExclusive(&F86cLock);
}

void
Felix86NtCacheFlush(void)
{
    ULONG Index;

    if (ReadAcquire(&F86cState) <= 0)
        return;
    RtlAcquireSRWLockExclusive(&F86cLock);
    for (Index = 0; Index < F86cModuleCount; Index++)
        F86cFlushModule(&F86cModules[Index]);
    RtlReleaseSRWLockExclusive(&F86cLock);
}

void
Felix86NtCacheProtect(
    uint64_t Base,
    uint64_t Size,
    uint32_t NewProtect)
{
    ULONG Index, Section;

    if (ReadAcquire(&F86cState) <= 0 ||
        !(NewProtect & (PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY)))
    {
        return;
    }
    RtlAcquireSRWLockExclusive(&F86cLock);
    for (Index = 0; Index < F86cModuleCount; Index++)
    {
        for (Section = 0; Section < F86cModules[Index].ExecutableCount; Section++)
        {
            if (Base < F86cModules[Index].ExecutableEnd[Section] && Base + Size > F86cModules[Index].ExecutableStart[Section])
                F86cModules[Index].Disabled = TRUE;
        }
    }
    RtlReleaseSRWLockExclusive(&F86cLock);
}

int
Felix86NtCacheEnabled(void)
{
    return F86cEnabled();
}
