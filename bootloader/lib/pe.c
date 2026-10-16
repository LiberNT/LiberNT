/*
 * PROJECT:     LiberNT Boot Loader
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Portable Executable image loading for the kernel module list
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <bl.h>

#define PE_COOKIE_DEFAULT_64 0x00002B992DDFA232ULL
#define PE_COOKIE_DEFAULT_32 0xBB40E64EUL
#define PE_COOKIE_MAX_64     0x0000FFFFFFFFFFFFULL
#define PE_PATH_CHARS        260
#define PE_RELOCATED         1
#define PE_INVALID           0
#define PE_MAX_FORWARD_DEPTH  4

LIST_ENTRY FrLdrModuleList = {&FrLdrModuleList, &FrLdrModuleList};

static const PCSTR PeSearchDirectories[] =
{
    "system32\\",
    "system32\\drivers\\",
};

static CHAR PeSystemRoot[PE_PATH_CHARS];

static
BOOLEAN
PeLoadModuleFrom(
    _Inout_ PLIST_ENTRY ModuleListHead,
    _In_ PDK_DEVICE Device,
    _In_ PCSTR Directory,
    _In_ PCSTR FileName,
    _In_ PCSTR ModuleName,
    _In_ TYPE_OF_MEMORY MemoryType,
    _In_ ULONG Flags,
    _Out_ PLDR_DATA_TABLE_ENTRY *Entry);

VOID
PeSetSystemRoot(
    _In_ PCSTR SystemRoot)
{
    RtlStringCbCopyA(PeSystemRoot, sizeof(PeSystemRoot), SystemRoot);
}

static
BOOLEAN
PeNameMatches(
    _In_ PCSTR Name,
    _In_ SIZE_T NameChars,
    _In_ PUNICODE_STRING ModuleName,
    _In_ BOOLEAN IgnoreExtension)
{
    PCWSTR Buffer = VaToPa(ModuleName->Buffer);
    SIZE_T Chars = ModuleName->Length / sizeof(WCHAR);
    SIZE_T i;

    if (IgnoreExtension)
    {
        for (i = 0; i < Chars; i++)
        {
            if (Buffer[i] == L'.')
            {
                Chars = i;
                break;
            }
        }
    }

    if (Chars != NameChars)
        return FALSE;

    for (i = 0; i < Chars; i++)
    {
        if (toupper((UCHAR)Name[i]) != toupper((UCHAR)Buffer[i]) || Buffer[i] > 0x7F)
            return FALSE;
    }

    return TRUE;
}

static
PLDR_DATA_TABLE_ENTRY
PeFindModule(
    _In_ PLIST_ENTRY ModuleListHead,
    _In_ PCSTR Name,
    _In_ SIZE_T NameChars,
    _In_ BOOLEAN IgnoreExtension)
{
    PLIST_ENTRY Link;
    PLDR_DATA_TABLE_ENTRY Entry;

    for (Link = ModuleListHead->Flink; Link != ModuleListHead; Link = Link->Flink)
    {
        Entry = CONTAINING_RECORD(Link, LDR_DATA_TABLE_ENTRY, InLoadOrderLinks);
        if (PeNameMatches(Name, NameChars, &Entry->BaseDllName, IgnoreExtension))
            return Entry;
    }

    return NULL;
}

BOOLEAN
PeLdrCheckForLoadedDll(
    _Inout_ PLIST_ENTRY ModuleListHead,
    _In_ PCSTR DllName,
    _Out_ PLDR_DATA_TABLE_ENTRY *LoadedEntry)
{
    *LoadedEntry = PeFindModule(ModuleListHead, DllName, strlen(DllName), FALSE);
    if (!*LoadedEntry)
        return FALSE;

    (*LoadedEntry)->LoadCount++;
    return TRUE;
}

static
BOOLEAN
PeSetName(
    _Out_ PUNICODE_STRING String,
    _In_ PCSTR Name)
{
    SIZE_T Chars = strlen(Name);
    PWSTR Buffer;
    SIZE_T i;

    if (Chars == 0 || Chars * sizeof(WCHAR) > MAXUSHORT)
        return FALSE;

    Buffer = MmHeapAllocate(Chars * sizeof(WCHAR));
    if (!Buffer)
        return FALSE;

    for (i = 0; i < Chars; i++)
        Buffer[i] = (UCHAR)Name[i];

    String->Length = (USHORT)(Chars * sizeof(WCHAR));
    String->MaximumLength = String->Length;
    String->Buffer = PaToVa(Buffer);
    return TRUE;
}

BOOLEAN
PeLdrAllocateDataTableEntry(
    _Inout_ PLIST_ENTRY ModuleListHead,
    _In_ PCSTR BaseDllName,
    _In_ PCSTR FullDllName,
    _In_ PVOID BaseVA,
    _Out_ PLDR_DATA_TABLE_ENTRY *NewEntry)
{
    PIMAGE_NT_HEADERS NtHeaders = RtlImageNtHeader(VaToPa(BaseVA));
    PLDR_DATA_TABLE_ENTRY Entry;

    *NewEntry = NULL;

    Entry = MmHeapAllocate(sizeof(*Entry));
    if (!NtHeaders ||
        !Entry ||
        !PeSetName(&Entry->BaseDllName, BaseDllName) ||
        !PeSetName(&Entry->FullDllName, FullDllName))
    {
        return FALSE;
    }

    Entry->DllBase = BaseVA;
    Entry->SizeOfImage = NtHeaders->OptionalHeader.SizeOfImage;
    Entry->EntryPoint = (PVOID)((ULONG_PTR)BaseVA + NtHeaders->OptionalHeader.AddressOfEntryPoint);
    Entry->CheckSum = NtHeaders->OptionalHeader.CheckSum;
    Entry->Flags = LDRP_ENTRY_PROCESSED;
    Entry->LoadCount = 1;

    if (NtHeaders->OptionalHeader.DllCharacteristics & IMAGE_DLLCHARACTERISTICS_FORCE_INTEGRITY)
        Entry->Flags |= LDRP_IMAGE_INTEGRITY_FORCED;

    InsertTailList(ModuleListHead, &Entry->InLoadOrderLinks);
    *NewEntry = Entry;
    return TRUE;
}

VOID
PeInitializeSecurityCookie(
    _In_ PLDR_DATA_TABLE_ENTRY Entry)
{
    PUCHAR Base = VaToPa(Entry->DllBase);
    PIMAGE_LOAD_CONFIG_DIRECTORY Config;
    TIME_FIELDS Time = {0};
    ULONG_PTR Default, Value, CookieVa;
    PULONG_PTR Cookie;
    ULONG Size;

    Config = RtlImageDirectoryEntryToData(Base, TRUE, IMAGE_DIRECTORY_ENTRY_LOAD_CONFIG, &Size);
    if (!Config ||
        Size < RTL_SIZEOF_THROUGH_FIELD(IMAGE_LOAD_CONFIG_DIRECTORY, SecurityCookie) ||
        Config->Size < RTL_SIZEOF_THROUGH_FIELD(IMAGE_LOAD_CONFIG_DIRECTORY, SecurityCookie))
    {
        return;
    }

    CookieVa = (ULONG_PTR)Config->SecurityCookie;
    if (CookieVa < (ULONG_PTR)Entry->DllBase ||
        CookieVa - (ULONG_PTR)Entry->DllBase > Entry->SizeOfImage - sizeof(ULONG_PTR))
    {
        return;
    }

    Cookie = (PULONG_PTR)(Base + (CookieVa - (ULONG_PTR)Entry->DllBase));
    Default = (ULONG_PTR)(sizeof(ULONG_PTR) == sizeof(ULONGLONG) ? PE_COOKIE_DEFAULT_64
                                                                  : PE_COOKIE_DEFAULT_32);
    if (*Cookie != Default && *Cookie != 0)
        return;

    FwGetTime(&Time);
    Value = (ULONG_PTR)Cookie ^
            (((ULONG_PTR)Time.Hour << 24) | ((ULONG_PTR)Time.Minute << 16) |
             ((ULONG_PTR)Time.Second << 8) | (ULONG_PTR)Time.Day);

    if (sizeof(ULONG_PTR) == sizeof(ULONGLONG) && (ULONGLONG)Value > PE_COOKIE_MAX_64)
        Value = (ULONG_PTR)((ULONGLONG)Value >> 16);

    if (Value == 0 || Value == Default)
        Value = Default + 1;

    *Cookie = Value;
}

PVOID
PeLoadImage(
    _In_ PDK_DEVICE Device,
    _In_ PCSTR Path,
    _In_ TYPE_OF_MEMORY MemoryType)
{
    PIMAGE_NT_HEADERS NtHeaders;
    PIMAGE_SECTION_HEADER Section;
    PUCHAR File, Base;
    PVOID VirtualBase;
    ULONG FileSize, ImageSize, HeaderSize, VirtualSize, RawSize, i;

    File = FsReadFileA(Device, Path, &FileSize);
    if (!File)
        return NULL;

    if (!NT_SUCCESS(RtlImageNtHeaderEx(0, File, FileSize, &NtHeaders)) ||
        !(NtHeaders->FileHeader.Characteristics & IMAGE_FILE_EXECUTABLE_IMAGE) ||
        NtHeaders->FileHeader.Machine != IMAGE_FILE_MACHINE_NATIVE)
    {
        FwFreePages(File, FileSize);
        return NULL;
    }

    ImageSize = NtHeaders->OptionalHeader.SizeOfImage;
    HeaderSize = min(NtHeaders->OptionalHeader.SizeOfHeaders, FileSize);

    Base = (HeaderSize <= ImageSize) ? MmAllocateMemoryWithType(ImageSize, MemoryType) : NULL;
    if (!Base)
    {
        FwFreePages(File, FileSize);
        return NULL;
    }

    RtlZeroMemory(Base, ImageSize);
    RtlCopyMemory(Base, File, HeaderSize);

    Section = IMAGE_FIRST_SECTION(NtHeaders);
    for (i = 0; i < NtHeaders->FileHeader.NumberOfSections; i++, Section++)
    {
        VirtualSize = Section->Misc.VirtualSize ? Section->Misc.VirtualSize : Section->SizeOfRawData;
        RawSize = Section->PointerToRawData ? min(Section->SizeOfRawData, VirtualSize) : 0;

        if (Section->VirtualAddress > ImageSize ||
            VirtualSize > ImageSize - Section->VirtualAddress ||
            Section->PointerToRawData > FileSize ||
            RawSize > FileSize - Section->PointerToRawData)
        {
            MmFreeMemory(Base);
            FwFreePages(File, FileSize);
            return NULL;
        }

        RtlCopyMemory(Base + Section->VirtualAddress, File + Section->PointerToRawData, RawSize);
    }

    FwFreePages(File, FileSize);

    VirtualBase = PaToVa(Base);
    NtHeaders = RtlImageNtHeader(Base);
    if (NtHeaders->OptionalHeader.ImageBase != (ULONG_PTR)VirtualBase &&
        LdrRelocateImageWithBias(Base,
                                 (LONGLONG)((ULONG_PTR)VirtualBase - (ULONG_PTR)Base),
                                 "winload",
                                 PE_RELOCATED,
                                 PE_RELOCATED,
                                 PE_INVALID) == PE_INVALID)
    {
        MmFreeMemory(Base);
        return NULL;
    }

    return Base;
}

static
PVOID
PeFindExport(
    _In_ PLIST_ENTRY ModuleListHead,
    _In_ PLDR_DATA_TABLE_ENTRY Module,
    _In_opt_ PCSTR Name,
    _In_ ULONG Ordinal,
    _In_ ULONG Depth)
{
    PUCHAR Base = VaToPa(Module->DllBase);
    PIMAGE_EXPORT_DIRECTORY Exports;
    PULONG Names, Functions;
    PUSHORT Ordinals;
    PLDR_DATA_TABLE_ENTRY Target;
    PCSTR Forwarder, Dot;
    ULONG Size, Index, Rva;
    LONG Low, High, Middle, Compare;

    Exports = RtlImageDirectoryEntryToData(Base, TRUE, IMAGE_DIRECTORY_ENTRY_EXPORT, &Size);
    if (!Exports)
        return NULL;

    Names = (PULONG)(Base + Exports->AddressOfNames);
    Ordinals = (PUSHORT)(Base + Exports->AddressOfNameOrdinals);
    Functions = (PULONG)(Base + Exports->AddressOfFunctions);

    if (Name)
    {
        Index = MAXULONG;
        Low = 0;
        High = (LONG)Exports->NumberOfNames - 1;
        while (Low <= High)
        {
            Middle = (Low + High) / 2;
            Compare = strcmp(Name, (PCSTR)(Base + Names[Middle]));
            if (Compare == 0)
            {
                Index = Ordinals[Middle];
                break;
            }

            if (Compare < 0)
                High = Middle - 1;
            else
                Low = Middle + 1;
        }
    }
    else
    {
        Index = Ordinal - Exports->Base;
    }

    if (Index >= Exports->NumberOfFunctions || Functions[Index] == 0)
        return NULL;

    Rva = Functions[Index];
    if (Rva < (ULONG)((PUCHAR)Exports - Base) || Rva >= (ULONG)((PUCHAR)Exports - Base) + Size)
        return (PUCHAR)Module->DllBase + Rva;

    Forwarder = (PCSTR)(Base + Rva);
    Dot = strchr(Forwarder, '.');
    if (!Dot || Depth == 0)
        return NULL;

    Target = PeFindModule(ModuleListHead, Forwarder, Dot - Forwarder, TRUE);
    if (!Target)
        return NULL;

    return PeFindExport(ModuleListHead, Target, Dot + 1, 0, Depth - 1);
}

BOOLEAN
PeResolveImports(
    _Inout_ PLIST_ENTRY ModuleListHead,
    _In_ PDK_DEVICE Device,
    _In_ PCSTR Directory,
    _In_ PLDR_DATA_TABLE_ENTRY Entry)
{
    PUCHAR Base = VaToPa(Entry->DllBase);
    PIMAGE_IMPORT_DESCRIPTOR Import;
    PIMAGE_THUNK_DATA Thunk, Lookup;
    PIMAGE_IMPORT_BY_NAME ByName;
    PLDR_DATA_TABLE_ENTRY Module;
    UNICODE_STRING EntryName;
    PCSTR DllName;
    PVOID Address;
    ULONG Size;

    EntryName = Entry->BaseDllName;
    EntryName.Buffer = VaToPa(EntryName.Buffer);

    Import = RtlImageDirectoryEntryToData(Base, TRUE, IMAGE_DIRECTORY_ENTRY_IMPORT, &Size);
    if (!Import)
        return TRUE;

    for (; Import->Name != 0 && Import->FirstThunk != 0; Import++)
    {
        DllName = (PCSTR)(Base + Import->Name);

        if (!PeLoadModule(ModuleListHead,
                          Device,
                          Directory,
                          DllName,
                          DllName,
                          LoaderBootDriver,
                          LDRP_DRIVER_DEPENDENT_DLL,
                          &Module))
        {
            FwPrint(L"%hs imported by %wZ could not be loaded.\r\n", DllName, &EntryName);
            return FALSE;
        }

        Thunk = (PIMAGE_THUNK_DATA)(Base + Import->FirstThunk);
        Lookup = Import->OriginalFirstThunk ? (PIMAGE_THUNK_DATA)(Base + Import->OriginalFirstThunk)
                                            : Thunk;

        for (; Lookup->u1.AddressOfData != 0; Lookup++, Thunk++)
        {
            if (IMAGE_SNAP_BY_ORDINAL(Lookup->u1.Ordinal))
            {
                Address = PeFindExport(ModuleListHead,
                                       Module,
                                       NULL,
                                       (ULONG)IMAGE_ORDINAL(Lookup->u1.Ordinal),
                                       PE_MAX_FORWARD_DEPTH);
                ByName = NULL;
            }
            else
            {
                ByName = (PIMAGE_IMPORT_BY_NAME)(Base + Lookup->u1.AddressOfData);
                Address = PeFindExport(ModuleListHead,
                                       Module,
                                       (PCSTR)ByName->Name,
                                       0,
                                       PE_MAX_FORWARD_DEPTH);
            }

            if (!Address)
            {
                FwPrint(L"%wZ needs %hs from %hs, which does not export it.\r\n",
                        &EntryName,
                        ByName ? (PCSTR)ByName->Name : "an ordinal",
                        DllName);
                return FALSE;
            }

            Thunk->u1.Function = (ULONG_PTR)Address;
        }
    }

    return TRUE;
}

static
BOOLEAN
PeLoadModuleFrom(
    _Inout_ PLIST_ENTRY ModuleListHead,
    _In_ PDK_DEVICE Device,
    _In_ PCSTR Directory,
    _In_ PCSTR FileName,
    _In_ PCSTR ModuleName,
    _In_ TYPE_OF_MEMORY MemoryType,
    _In_ ULONG Flags,
    _Out_ PLDR_DATA_TABLE_ENTRY *Entry)
{
    CHAR Path[PE_PATH_CHARS];
    PVOID Base;

    if (!NT_SUCCESS(RtlStringCbPrintfA(Path, sizeof(Path), "%s%s", Directory, FileName)))
        return FALSE;

    Base = PeLoadImage(Device, Path, MemoryType);
    if (!Base)
        return FALSE;

    if (!PeLdrAllocateDataTableEntry(ModuleListHead, ModuleName, ModuleName, PaToVa(Base), Entry))
    {
        MmFreeMemory(Base);
        return FALSE;
    }

    (*Entry)->Flags |= Flags;
    PeInitializeSecurityCookie(*Entry);
    return PeResolveImports(ModuleListHead, Device, Directory, *Entry);
}

BOOLEAN
PeLoadModule(
    _Inout_ PLIST_ENTRY ModuleListHead,
    _In_ PDK_DEVICE Device,
    _In_ PCSTR Directory,
    _In_ PCSTR FileName,
    _In_ PCSTR ModuleName,
    _In_ TYPE_OF_MEMORY MemoryType,
    _In_ ULONG Flags,
    _Out_ PLDR_DATA_TABLE_ENTRY *Entry)
{
    CHAR Fallback[PE_PATH_CHARS];
    ULONG i;

    if (PeLdrCheckForLoadedDll(ModuleListHead, ModuleName, Entry))
        return TRUE;

    if (PeLoadModuleFrom(ModuleListHead, Device, Directory, FileName, ModuleName, MemoryType, Flags, Entry))
        return TRUE;

    if (*Entry)
        return FALSE;

    for (i = 0; i < RTL_NUMBER_OF(PeSearchDirectories); i++)
    {
        if (!NT_SUCCESS(RtlStringCbPrintfA(Fallback,
                                           sizeof(Fallback),
                                           "%s%s",
                                           PeSystemRoot,
                                           PeSearchDirectories[i])) ||
            _stricmp(Fallback, Directory) == 0)
        {
            continue;
        }

        if (PeLoadModuleFrom(ModuleListHead, Device, Fallback, FileName, ModuleName, MemoryType, Flags, Entry))
            return TRUE;

        if (*Entry)
            return FALSE;
    }

    return FALSE;
}
