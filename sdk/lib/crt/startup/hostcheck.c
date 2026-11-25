/*
 * PROJECT:     LiberNT CRT startup
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Lets a LiberNT program or DLL start only on LiberNT
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <windows.h>
#include <winternl.h>
#include <ntverp.h>

#define HOSTCHECK_WIDEN_(x) L##x
#define HOSTCHECK_WIDEN(x) HOSTCHECK_WIDEN_(x)

static
int
HostCheckEqualFold(const WCHAR *Left, const WCHAR *Right, SIZE_T Length)
{
    SIZE_T Index;

    for (Index = 0; Index < Length; Index++)
    {
        WCHAR A = Left[Index], B = Right[Index];

        if (A >= L'A' && A <= L'Z')
            A += L'a' - L'A';
        if (B >= L'A' && B <= L'Z')
            B += L'a' - L'A';
        if (A != B)
            return 0;
    }

    return 1;
}

static
const BYTE *
HostCheckNtdllBase(void)
{
    static const WCHAR Name[] = L"ntdll.dll";
    const SIZE_T NameLength = ARRAYSIZE(Name) - 1;
    PPEB Peb = NtCurrentTeb()->ProcessEnvironmentBlock;
    PLIST_ENTRY Head, Entry;

    if (!Peb || !Peb->Ldr)
        return NULL;

    Head = &Peb->Ldr->InMemoryOrderModuleList;
    for (Entry = Head->Flink; Entry != Head; Entry = Entry->Flink)
    {
        PLDR_DATA_TABLE_ENTRY Module = CONTAINING_RECORD(Entry, LDR_DATA_TABLE_ENTRY, InMemoryOrderLinks);
        SIZE_T Length = Module->FullDllName.Length / sizeof(WCHAR);
        const WCHAR *Path = Module->FullDllName.Buffer;

        if (!Path || Length < NameLength)
            continue;
        if (Length > NameLength && Path[Length - NameLength - 1] != L'\\')
            continue;
        if (HostCheckEqualFold(Path + Length - NameLength, Name, NameLength))
            return Module->DllBase;
    }

    return NULL;
}

static
const IMAGE_RESOURCE_DIRECTORY_ENTRY *
HostCheckFindEntry(
    const BYTE *Resources,
    DWORD Size,
    DWORD Offset,
    WORD Id)
{
    const IMAGE_RESOURCE_DIRECTORY *Directory;
    const IMAGE_RESOURCE_DIRECTORY_ENTRY *Entries;
    DWORD Count, Index;

    if (Offset > Size || Size - Offset < sizeof(*Directory))
        return NULL;

    Directory = (const IMAGE_RESOURCE_DIRECTORY *)(Resources + Offset);
    Entries = (const IMAGE_RESOURCE_DIRECTORY_ENTRY *)(Directory + 1);
    Count = (DWORD)Directory->NumberOfNamedEntries + Directory->NumberOfIdEntries;
    if ((Size - Offset - sizeof(*Directory)) / sizeof(*Entries) < Count)
        return NULL;

    for (Index = Directory->NumberOfNamedEntries; Index < Count; Index++)
    {
        if (Id == 0 || Entries[Index].Id == Id)
            return &Entries[Index];
    }

    return NULL;
}

static
int
HostCheckNtdll(void)
{
    static const WCHAR Product[] = HOSTCHECK_WIDEN(VER_PRODUCTNAME_STR);
    const SIZE_T Length = ARRAYSIZE(Product) - 1;
    const BYTE *Base = HostCheckNtdllBase();
    const IMAGE_DOS_HEADER *Dos = (const IMAGE_DOS_HEADER *)Base;
    const IMAGE_NT_HEADERS *Nt;
    const IMAGE_RESOURCE_DIRECTORY_ENTRY *Entry;
    const IMAGE_RESOURCE_DATA_ENTRY *Data;
    const BYTE *Resources;
    const WCHAR *Block;
    DWORD Size, Offset;
    SIZE_T Count, Index;
    WORD Ids[3], Level;

    if (!Base || Dos->e_magic != IMAGE_DOS_SIGNATURE)
        return 0;

    Nt = (const IMAGE_NT_HEADERS *)(Base + Dos->e_lfanew);
    if (Nt->Signature != IMAGE_NT_SIGNATURE ||
        Nt->OptionalHeader.NumberOfRvaAndSizes <= IMAGE_DIRECTORY_ENTRY_RESOURCE)
        return 0;

    Resources = Base + Nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_RESOURCE].VirtualAddress;
    Size = Nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_RESOURCE].Size;
    if (!Size)
        return 0;

    Ids[0] = (WORD)(ULONG_PTR)RT_VERSION;
    Ids[1] = VS_VERSION_INFO;
    Ids[2] = 0;
    Offset = 0;
    for (Level = 0; Level < 3; Level++)
    {
        Entry = HostCheckFindEntry(Resources, Size, Offset, Ids[Level]);
        if (!Entry || (Level < 2) != (Entry->DataIsDirectory != 0))
            return 0;
        Offset = Level < 2 ? Entry->OffsetToDirectory : Entry->OffsetToData;
    }

    if (Offset > Size || Size - Offset < sizeof(*Data))
        return 0;

    Data = (const IMAGE_RESOURCE_DATA_ENTRY *)(Resources + Offset);
    Block = (const WCHAR *)(Base + Data->OffsetToData);
    Count = Data->Size / sizeof(WCHAR);
    for (Index = 0; Index + Length <= Count; Index++)
    {
        SIZE_T Char;

        for (Char = 0; Char < Length && Block[Index + Char] == Product[Char]; Char++)
            ;
        if (Char == Length)
            return 1;
    }

    return 0;
}

int __cdecl
__libernt_host_check(void)
{
    static int Result = -1;

    if (Result < 0)
        Result = HostCheckNtdll();

    return Result;
}
