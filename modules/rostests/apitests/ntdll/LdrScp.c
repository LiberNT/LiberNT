/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Verify AMD64 supplemental unwind registration on load and unload
 * COPYRIGHT:   Copyright 2026 LiberNT contributors
 */

#include "precomp.h"
#include <psapi.h>
#include <libernt/wow64shared.h>

#define SECTION_RVA 0x1000
#define SECTION_RAW 0x200

typedef enum _OVERRIDE_FIXTURE
{
    ValidOverride,
    EmptyOverrideList,
    MissingOverrideTargets,
    MissingOverrideFixups,
    EmptyOverrideFixupBlock,
    InstrumentedOverride
} OVERRIDE_FIXTURE;

/* A no-entry DLL with a standard base relocation and a function-override record.
 * Both candidate functions just return. The two fixtures differ by one debug flag. */
static BOOL
WriteFixture(PCWSTR Path, BOOL Hotpatch, OVERRIDE_FIXTURE Fixture)
{
    BYTE Image[0x1000] = {0};
    IMAGE_DOS_HEADER *Dos = (IMAGE_DOS_HEADER *)Image;
    IMAGE_NT_HEADERS64 *Nt = (IMAGE_NT_HEADERS64 *)(Image + 0x80);
    IMAGE_OPTIONAL_HEADER64 *Opt = &Nt->OptionalHeader;
    IMAGE_SECTION_HEADER *Section;
    BYTE *Data = Image + SECTION_RAW;
    IMAGE_DEBUG_DIRECTORY *Debug = (IMAGE_DEBUG_DIRECTORY *)(Data + 0x80);
    IMAGE_LOAD_CONFIG_DIRECTORY64 *Config = (IMAGE_LOAD_CONFIG_DIRECTORY64 *)(Data + 0x100);
    static const DWORD Reloc[] = {SECTION_RVA, 12, (IMAGE_REL_BASED_DIR64 << 12) | 0x40};
    DWORD *Table = (DWORD *)(Data + 0x300);
    const DWORD Target = SECTION_RVA + 0x10;
    DWORD Fixups[] = {SECTION_RVA, 12, 0x1021};
    const DWORD Decisions[] = {1, 24, 0x00020001, 0, 0, 0, 0x00020002, 0};
    const DWORD TargetBytes = (Fixture == MissingOverrideTargets) ? 0 : sizeof(Target);
    DWORD FixupBytes = sizeof(Fixups);
    struct
    {
        DWORD OriginalRva, BddOffset, RvaSize, BaseRelocSize;
    } Record = {SECTION_RVA, 0, TargetBytes, 0};
    BYTE *Records = (BYTE *)(Table + 6);
    static const BYTE Branch[] = {0xe9, 0xdb, 0xff, 0xff, 0xff};
    HANDLE File;
    DWORD Bytes;
    BOOL Success;

    Dos->e_magic = IMAGE_DOS_SIGNATURE;
    Dos->e_lfanew = 0x80;
    Nt->Signature = IMAGE_NT_SIGNATURE;
    Nt->FileHeader.Machine = IMAGE_FILE_MACHINE_AMD64;
    Nt->FileHeader.NumberOfSections = 1;
    Nt->FileHeader.SizeOfOptionalHeader = sizeof(*Opt);
    Nt->FileHeader.Characteristics = IMAGE_FILE_DLL | IMAGE_FILE_EXECUTABLE_IMAGE | IMAGE_FILE_LARGE_ADDRESS_AWARE;
    Opt->Magic = IMAGE_NT_OPTIONAL_HDR64_MAGIC;
    Opt->ImageBase = 0x180000000ULL;
    Opt->SectionAlignment = SECTION_RVA;
    Opt->FileAlignment = SECTION_RAW;
    Opt->BaseOfCode = SECTION_RVA;
    Opt->SizeOfCode = sizeof(Image) - SECTION_RAW;
    Opt->MajorOperatingSystemVersion = Opt->MajorSubsystemVersion = 6;
    Opt->SizeOfImage = 0x2000;
    Opt->SizeOfHeaders = SECTION_RAW;
    Opt->Subsystem = IMAGE_SUBSYSTEM_WINDOWS_CUI;
    Opt->DllCharacteristics = IMAGE_DLLCHARACTERISTICS_DYNAMIC_BASE | IMAGE_DLLCHARACTERISTICS_NX_COMPAT;
    Opt->SizeOfStackReserve = Opt->SizeOfHeapReserve = 0x100000;
    Opt->SizeOfStackCommit = Opt->SizeOfHeapCommit = 0x1000;
    Opt->NumberOfRvaAndSizes = IMAGE_NUMBEROF_DIRECTORY_ENTRIES;
    Opt->DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC].VirtualAddress = SECTION_RVA + 0x50;
    Opt->DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC].Size = sizeof(Reloc);
    Opt->DataDirectory[IMAGE_DIRECTORY_ENTRY_DEBUG].VirtualAddress = SECTION_RVA + 0x80;
    Opt->DataDirectory[IMAGE_DIRECTORY_ENTRY_DEBUG].Size = sizeof(*Debug);
    Opt->DataDirectory[IMAGE_DIRECTORY_ENTRY_LOAD_CONFIG].VirtualAddress = SECTION_RVA + 0x100;
    Opt->DataDirectory[IMAGE_DIRECTORY_ENTRY_LOAD_CONFIG].Size = sizeof(*Config);

    Section = IMAGE_FIRST_SECTION(Nt);
    RtlCopyMemory(Section->Name, ".probe", 6);
    Section->VirtualAddress = SECTION_RVA;
    Section->PointerToRawData = SECTION_RAW;
    Section->SizeOfRawData = Section->Misc.VirtualSize = sizeof(Image) - SECTION_RAW;
    Section->Characteristics = IMAGE_SCN_CNT_CODE | IMAGE_SCN_MEM_EXECUTE | IMAGE_SCN_MEM_READ;
    Data[0] = Data[0x10] = 0xc3;
    RtlCopyMemory(Data + 0x20, Branch, sizeof(Branch));
    *(ULONG64 *)(Data + 0x40) = Opt->ImageBase + SECTION_RVA;
    RtlCopyMemory(Data + 0x50, Reloc, sizeof(Reloc));
    Debug->Type = 20; /* IMAGE_DEBUG_TYPE_EX_DLLCHARACTERISTICS */
    Debug->SizeOfData = sizeof(DWORD);
    Debug->AddressOfRawData = SECTION_RVA + 0xa0;
    Debug->PointerToRawData = SECTION_RAW + 0xa0;
    *(DWORD *)(Data + 0xa0) = Hotpatch ? 0x80 : 0;
    /* An ordinary illegal instruction at the same page offset as a guard dispatch entry. */
    Data[0xc0] = 0x0f;
    Data[0xc1] = 0x0b;
    Config->Size = sizeof(*Config);
    Config->DynamicValueRelocTableOffset = 0x300;
    Config->DynamicValueRelocTableSection = 1;
    if (Fixture == MissingOverrideFixups)
        FixupBytes = 0;
    else if (Fixture == EmptyOverrideFixupBlock)
        Fixups[1] = FixupBytes = sizeof(IMAGE_BASE_RELOCATION);
    Record.BaseRelocSize = FixupBytes;
    Table[5] = (Fixture == EmptyOverrideList) ? 0 : sizeof(Record) + TargetBytes + FixupBytes;
    Table[4] = sizeof(DWORD) + Table[5] + sizeof(Decisions);
    Table[2] = 7;
    Table[1] = sizeof(ULONGLONG) + sizeof(DWORD) + Table[4];
    Table[0] = 1;
    if (Table[5])
    {
        RtlCopyMemory(Records, &Record, sizeof(Record));
        RtlCopyMemory(Records + sizeof(Record), &Target, TargetBytes);
        RtlCopyMemory(Records + sizeof(Record) + TargetBytes, Fixups, FixupBytes);
    }
    RtlCopyMemory(Records + Table[5], Decisions, sizeof(Decisions));

    if (Fixture == InstrumentedOverride)
    {
        static const USHORT Slots[] = {
            0x40, 0x100 + FIELD_OFFSET(IMAGE_LOAD_CONFIG_DIRECTORY64, GuardCFCheckFunctionPointer),
            0x100 + FIELD_OFFSET(IMAGE_LOAD_CONFIG_DIRECTORY64, GuardCFDispatchFunctionPointer),
            0x100 + FIELD_OFFSET(IMAGE_LOAD_CONFIG_DIRECTORY64, GuardCFFunctionTable)
        };
        IMAGE_EXPORT_DIRECTORY *Exports = (IMAGE_EXPORT_DIRECTORY *)(Data + 0x600);
        IMAGE_BASE_RELOCATION *Relocs = (IMAGE_BASE_RELOCATION *)(Data + 0x50);
        USHORT *Entries = (USHORT *)(Relocs + 1);
        ULONGLONG Preferred = Opt->ImageBase + SECTION_RVA;
        ULONG Index;
        Opt->DllCharacteristics |= IMAGE_DLLCHARACTERISTICS_GUARD_CF;
        Config->GuardFlags = IMAGE_GUARD_CF_INSTRUMENTED | IMAGE_GUARD_CF_FUNCTION_TABLE_PRESENT |
                             IMAGE_GUARD_CF_EXPORT_SUPPRESSION_INFO_PRESENT | (1UL << IMAGE_GUARD_CF_FUNCTION_TABLE_SIZE_SHIFT);
        Config->GuardCFCheckFunctionPointer = Preferred + 0x580;
        Config->GuardCFDispatchFunctionPointer = Preferred + 0x588;
        Config->GuardCFFunctionTable = Preferred + 0x500;
        Config->GuardCFFunctionCount = 3;
        for (Index = 0; Index < 3; ++Index)
        {
            ULONG Rva = SECTION_RVA + (Index == 2 ? 0x31 : Index * 0x10);
            RtlCopyMemory(Data + 0x500 + Index * 5, &Rva, sizeof(Rva));
        }
        Data[0x509] = IMAGE_GUARD_FLAG_EXPORT_SUPPRESSED;
        Data[0x31] = 0xc3;
        for (Index = 0; Index < ARRAYSIZE(Slots); ++Index)
            Entries[Index] = (IMAGE_REL_BASED_DIR64 << 12) | Slots[Index];
        Relocs->SizeOfBlock = sizeof(*Relocs) + sizeof(Slots);
        Opt->DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC].Size = Relocs->SizeOfBlock;
        Opt->DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT].VirtualAddress = SECTION_RVA + 0x600;
        Opt->DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT].Size = 0x80;
        Exports->Name = SECTION_RVA + 0x670;
        Exports->Base = Exports->NumberOfFunctions = Exports->NumberOfNames = 1;
        Exports->AddressOfFunctions = SECTION_RVA + 0x640;
        Exports->AddressOfNames = SECTION_RVA + 0x648;
        Exports->AddressOfNameOrdinals = SECTION_RVA + 0x650;
        *(ULONG *)(Data + 0x640) = SECTION_RVA + 16;
        *(ULONG *)(Data + 0x648) = SECTION_RVA + 0x660;
        RtlCopyMemory(Data + 0x660, "CfgExample", sizeof("CfgExample"));
        RtlCopyMemory(Data + 0x670, "cfgtest.dll", sizeof("cfgtest.dll"));
    }

    File = CreateFileW(Path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (File == INVALID_HANDLE_VALUE)
        return FALSE;
    Success = WriteFile(File, Image, sizeof(Image), &Bytes, NULL) && Bytes == sizeof(Image);
    CloseHandle(File);
    return Success;
}

typedef struct _SCP_OBSERVATIONS
{
    UNICODE_STRING Name;
    BOOL Hotpatch;
    ULONG Loads;
    ULONG Unloads;
} SCP_OBSERVATIONS;

static ULONG_PTR
CheckView(PVOID Module, BOOL Hotpatch, BOOL Registered)
{
    MEMORY_IMAGE_EXTENSION_INFORMATION Info = { MemoryImageExtensionCfgScp };
    MEMORY_IMAGE_INFORMATION Image;
    PLDR_DATA_TABLE_ENTRY Entry;
    ULONG_PTR Base, LookupBase = 0, Pc;
    ULONG TableRva;
    PRUNTIME_FUNCTION Table;
    NTSTATUS Status;
    SIZE_T Returned;
    ULONG ImageSize = RtlImageNtHeader(Module)->OptionalHeader.SizeOfImage;

    if (Registered)
    {
        Status = LdrFindEntryForAddress(Module, &Entry);
        ok_ntstatus(Status, STATUS_SUCCESS);
        if (NT_SUCCESS(Status))
        {
            ok_int(Entry->ScpInExceptionTable, Hotpatch);
            ok_hex(Entry->SizeOfImage, ImageSize);
        }
    }
    Status = NtQueryVirtualMemory(NtCurrentProcess(), Module, MemoryImageInformation,
                                  &Image, sizeof(Image), &Returned);
    ok_ntstatus(Status, STATUS_SUCCESS);
    if (NT_SUCCESS(Status))
    {
        ok_ptr(Image.ImageBase, Module);
        ok_size_t(Image.SizeOfImage, ImageSize);
        ok_int(Image.ImageExtensionPresent, Hotpatch);
    }

    Status = NtQueryVirtualMemory(NtCurrentProcess(), Module, MemoryImageExtensionInformation,
                                  &Info, sizeof(Info), &Returned);
    ok_ntstatus(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
        return 0;

    ok_size_t(Returned, sizeof(Info));
    ok_ptr(Info.ExtensionImageBaseRva, Hotpatch ? (PVOID)(ULONG_PTR)ImageSize : NULL);
    ok_size_t(Info.ExtensionSize, Hotpatch ? PAGE_SIZE : 0);
    if (!Hotpatch || (ULONG_PTR)Info.ExtensionImageBaseRva != ImageSize || Info.ExtensionSize != PAGE_SIZE)
        return 0;

    Base = (ULONG_PTR)Module + ImageSize;
    {
        PVOID LastByte = (PVOID)(Base + PAGE_SIZE - 1);
        MEMORY_BASIC_INFORMATION Basic;

        Status = NtQueryVirtualMemory(NtCurrentProcess(), LastByte, MemoryBasicInformation,
                                      &Basic, sizeof(Basic), NULL);
        ok_ntstatus(Status, STATUS_SUCCESS);
        if (!NT_SUCCESS(Status))
            return 0;
        ok_ptr(Basic.AllocationBase, Module);
        ok_ptr(Basic.BaseAddress, (PVOID)Base);
        ok_size_t(Basic.RegionSize, PAGE_SIZE);
        ok_hex(Basic.State, MEM_COMMIT);
        ok_hex(Basic.Type, MEM_IMAGE);
        ok_hex(Basic.Protect, PAGE_EXECUTE_READ);
        if (Basic.State != MEM_COMMIT || Basic.Protect != PAGE_EXECUTE_READ)
            return 0;

        Status = NtQueryVirtualMemory(NtCurrentProcess(), LastByte, MemoryImageInformation,
                                      &Image, sizeof(Image), NULL);
        ok_ntstatus(Status, STATUS_SUCCESS);
        if (NT_SUCCESS(Status))
        {
            ok_ptr(Image.ImageBase, Module);
            ok_size_t(Image.SizeOfImage, ImageSize);
            ok_int(Image.ImageExtensionPresent, TRUE);
        }

        Info.ExtensionImageBaseRva = NULL;
        Info.ExtensionSize = 0;
        Status = NtQueryVirtualMemory(NtCurrentProcess(), LastByte, MemoryImageExtensionInformation,
                                      &Info, sizeof(Info), NULL);
        ok_ntstatus(Status, STATUS_SUCCESS);
        if (NT_SUCCESS(Status))
        {
            ok_ptr(Info.ExtensionImageBaseRva, (PVOID)(ULONG_PTR)ImageSize);
            ok_size_t(Info.ExtensionSize, PAGE_SIZE);
        }
    }
    TableRva = ((PULONG)Base)[5];
    ok(TableRva <= PAGE_SIZE - sizeof(*Table), "Unwind table RVA %#lx\n", TableRva);
    if (TableRva > PAGE_SIZE - sizeof(*Table))
        return 0;
    Table = (PRUNTIME_FUNCTION)(Base + TableRva);
    ok(Table->BeginAddress < Table->EndAddress && Table->EndAddress <= PAGE_SIZE, "Invalid unwind range\n");
    if (Table->BeginAddress >= Table->EndAddress || Table->EndAddress > PAGE_SIZE)
        return 0;
    Pc = Base + Table->BeginAddress;
    ok_ptr(RtlLookupFunctionEntry(Pc, &LookupBase, NULL), Registered ? Table : NULL);
    if (Registered)
        ok_ptr((PVOID)LookupBase, (PVOID)Base);
    ok_ptr(RtlLookupFunctionEntry(Base + Table->EndAddress, &LookupBase, NULL), NULL);
    ok(!NT_SUCCESS(LdrFindEntryForAddress((PVOID)Base, &Entry)), "Extension belongs to loader image index\n");
    return Pc;
}

static VOID CALLBACK
ObserveScp(ULONG Reason, PCLDR_DLL_NOTIFICATION_DATA Data, PVOID Context)
{
    SCP_OBSERVATIONS *Observed = Context;
    if (!RtlEqualUnicodeString(Data->Loaded.FullDllName, &Observed->Name, TRUE))
        return;

    if (Reason == LDR_DLL_NOTIFICATION_REASON_LOADED)
        ++Observed->Loads;
    else if (Reason == LDR_DLL_NOTIFICATION_REASON_UNLOADED)
        ++Observed->Unloads;
    CheckView(Data->Loaded.DllBase, Observed->Hotpatch, TRUE);
}

static void
CheckMapping(PCWSTR Path, BOOL Hotpatch, DWORD Flags)
{
    SCP_OBSERVATIONS Observed = {0};
    HMODULE Module;
    PVOID Cookie = NULL;
    ULONG_PTR LookupBase, Pc;
    NTSTATUS Status;

    RtlInitUnicodeString(&Observed.Name, Path);
    Observed.Hotpatch = Hotpatch;
    Status = LdrRegisterDllNotification(0, ObserveScp, &Observed, &Cookie);
    ok_ntstatus(Status, STATUS_SUCCESS);
    Module = LoadLibraryExW(Path, NULL, Flags);
    ok(Module != NULL, "LoadLibraryExW(%lu) failed, error %lu\n", Flags, GetLastError());
    if (Module)
    {
        Pc = CheckView(Module, Hotpatch, TRUE);
        ok(FreeLibrary(Module), "FreeLibrary failed, error %lu\n", GetLastError());
        if (Pc)
            ok_ptr(RtlLookupFunctionEntry(Pc, &LookupBase, NULL), NULL);
    }
    if (Cookie)
    {
        ok_ntstatus(LdrUnregisterDllNotification(Cookie), STATUS_SUCCESS);
        ok_int(Observed.Loads, Flags == 0);
        ok_int(Observed.Unloads, Flags == 0);
    }
}

static void
CheckSection(PCWSTR Path, BOOL Hotpatch, BOOL NoExecute, BOOL InvalidOverride)
{
    HANDLE File, Section;
    PVOID Base;

    File = CreateFileW(Path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    ok(File != INVALID_HANDLE_VALUE, "Cannot open fixture, error %lu\n", GetLastError());
    if (File == INVALID_HANDLE_VALUE)
        return;
    Section = CreateFileMappingW(File, NULL, PAGE_READONLY | (NoExecute ? SEC_IMAGE_NO_EXECUTE : SEC_IMAGE), 0, 0, NULL);
    if (InvalidOverride)
    {
        DWORD Error = GetLastError();
        ok(Section == NULL, "Created an image section with an invalid function override\n");
        if (Section)
            CloseHandle(Section);
        else
            ok_int(Error, ERROR_BAD_EXE_FORMAT);
        CloseHandle(File);
        return;
    }
    ok(Section != NULL, "Cannot create image section, error %lu\n", GetLastError());
    if (Section)
    {
        Base = MapViewOfFile(Section, FILE_MAP_READ, 0, 0, 0);
        ok(Base != NULL, "Cannot map image section, error %lu\n", GetLastError());
        if (Base)
        {
            MEMORY_IMAGE_INFORMATION Image;
            NTSTATUS Status = NtQueryVirtualMemory(NtCurrentProcess(), Base, MemoryImageInformation,
                                                   &Image, sizeof(Image), NULL);
            ok_ntstatus(Status, STATUS_SUCCESS);
            if (NT_SUCCESS(Status))
                ok_int(Image.ImageNotExecutable, NoExecute);
            CheckView(Base, Hotpatch && !NoExecute, FALSE);
            UnmapViewOfFile(Base);
        }
        CloseHandle(Section);
    }
    CloseHandle(File);
}

static void
CheckQueryFailure(PVOID Address, MEMORY_IMAGE_EXTENSION_TYPE Type, ULONG Flags, SIZE_T Length, NTSTATUS Expected)
{
    MEMORY_IMAGE_EXTENSION_INFORMATION Info = { Type, Flags, (PVOID)(ULONG_PTR)0x1234, 0x5678 };
    MEMORY_IMAGE_EXTENSION_INFORMATION Original = Info;
    SIZE_T Returned = 0x8765;
    NTSTATUS Status = NtQueryVirtualMemory(NtCurrentProcess(), Address, MemoryImageExtensionInformation,
                                           &Info, Length, &Returned);
    ok_ntstatus(Status, Expected);
    ok(!memcmp(&Info, &Original, sizeof(Info)), "Query failure altered the buffer\n");
    ok_size_t(Returned, 0x8765);
}

typedef VOID (NTAPI *SCP_CHECK)(PVOID);
typedef BOOL (WINAPI *SCP_SET_TARGETS)(HANDLE, PVOID, SIZE_T, ULONG, PCFG_CALL_TARGET_INFO);
ULONG TestScpDispatch(ULONG A, ULONG B, ULONG C, ULONG D, ULONG E, PVOID Dispatch, PVOID Target);

static ULONG
ScpTestTarget(ULONG A, ULONG B, ULONG C, ULONG D, ULONG E)
{
    return A + 3 * B + 5 * C + 7 * D + 11 * E;
}

static VOID
CheckGuardImage(PSYSTEM_DLL_INIT_BLOCK Block, ULONG Policy, BOOL Instrumented)
{
    WCHAR Directory[MAX_PATH], Path[MAX_PATH];
    PUCHAR Base, Code;
    HMODULE Module;
    SIZE_T Length = GetTempPathW(ARRAYSIZE(Directory), Directory);
    if (!Length || Length >= ARRAYSIZE(Directory) || !GetTempFileNameW(Directory, L"cfg", 0, Path))
    {
        ok(0, "Cannot create CFG test file, error %lu\n", GetLastError());
        return;
    }
    if (!WriteFixture(Path, TRUE, Instrumented ? InstrumentedOverride : ValidOverride))
    {
        ok(0, "Cannot write guard image, error %lu\n", GetLastError());
        DeleteFileW(Path);
        return;
    }
    Module = LoadLibraryW(Path);
    ok(Module != NULL, "Cannot load guard image, error %lu\n", GetLastError());
    if (Module)
    {
        MEMORY_IMAGE_EXTENSION_INFORMATION Extension = {MemoryImageExtensionCfgScp};
        NTSTATUS Status;
        PUCHAR Page;
        BYTE Value;
        PVOID Proc;
        Base = (PUCHAR)Module;
        Code = Base + SECTION_RVA;
        if (!Instrumented)
        {
            StartSeh()
                ((VOID (NTAPI *)(VOID))(Code + 0xc0))();
            EndSeh(STATUS_ILLEGAL_INSTRUCTION);
        }
        Status = NtQueryVirtualMemory(NtCurrentProcess(), Base, MemoryImageExtensionInformation,
                                      &Extension, sizeof(Extension), NULL);
        ok_ntstatus(Status, STATUS_SUCCESS);
        if (NT_SUCCESS(Status) && Extension.ExtensionSize == PAGE_SIZE)
        {
            Page = Base + RtlImageNtHeader(Base)->OptionalHeader.SizeOfImage;
            ok_int(memcmp(Page, (PUCHAR)(ULONG_PTR)Block->ScpCfgDispatchFunction - 64, PAGE_SIZE) == 0,
                   !Policy || Instrumented);
            ((SCP_CHECK)(Page + 320))(ScpTestTarget);
            ((SCP_CHECK)(Page + 448))(ScpTestTarget);
            ok_hex(TestScpDispatch(1, 2, 3, 4, 5, Page + 64, ScpTestTarget), ScpTestTarget(1, 2, 3, 4, 5));
            ok_hex(TestScpDispatch(1, 2, 3, 4, 5, Page + 192, ScpTestTarget), ScpTestTarget(1, 2, 3, 4, 5));
        }
        else
            ok_size_t(Extension.ExtensionSize, PAGE_SIZE);
        if (!Instrumented || !Policy)
        {
            if (Instrumented)
            {
                ok_ptr(*(PVOID *)(Code + 0x580), NULL);
                ok_ptr(*(PVOID *)(Code + 0x588), NULL);
            }
            goto Unload;
        }
        Value = ((PUCHAR)(ULONG_PTR)Block->CfgBitMap)[(ULONG_PTR)Code >> 6];
        ok_hex(Value, 0xc9);
        Proc = (PVOID)GetProcAddress(Module, "CfgExample");
        ok_ptr(Proc, Code + 16);
        Value = ((PUCHAR)(ULONG_PTR)Block->CfgBitMap)[(ULONG_PTR)Code >> 6];
        ok_hex(Value, Policy == 3 ? 0xc5 : 0xc9);
        ok_ptr(*(PVOID *)(Code + 0x580), (PVOID)(ULONG_PTR)(Policy & 2 ? Block->ScpCfgCheckESFunction : Block->ScpCfgCheckFunction));
        ok_ptr(*(PVOID *)(Code + 0x588), (PVOID)(ULONG_PTR)(Policy & 2 ? Block->ScpCfgDispatchESFunction : Block->ScpCfgDispatchFunction));
        if (Proc && *(PVOID *)(Code + 0x580))
            (*(SCP_CHECK *)(Code + 0x580))(Proc);
Unload:
        FreeLibrary(Module);
    }
    DeleteFileW(Path);
}

static VOID
CheckProcessGuard(ULONG Expected, BOOL Reject)
{
    HMODULE Ntdll = GetModuleHandleW(L"ntdll.dll");
    PSYSTEM_DLL_INIT_BLOCK Block = (PSYSTEM_DLL_INIT_BLOCK)GetProcAddress(Ntdll, "LdrSystemDllInitBlock");
    SCP_SET_TARGETS SetTargets = (SCP_SET_TARGETS)GetProcAddress(GetModuleHandleW(L"kernelbase.dll"), "SetProcessValidCallTargets");
    PROCESS_MITIGATION_CONTROL_FLOW_GUARD_POLICY Policy = {0};
    ULONG64 OptionsMask[2] = {0};
    PSAPI_WORKING_SET_EX_INFORMATION Ws = {0};
    ULONG64 Page = (ULONG_PTR)Ntdll + RtlImageNtHeader(Ntdll)->OptionalHeader.SizeOfImage;
    CFG_CALL_TARGET_INFO Target = { 16, CFG_CALL_TARGET_VALID };
    MEMORY_BASIC_INFORMATION Memory;
    HANDLE Process;
    PUCHAR Allocation;
    ULONG Index;
    const ULONG Args[] = { 17, 29, 31, 47, 53 };
    ULONG Result = ScpTestTarget(Args[0], Args[1], Args[2], Args[3], Args[4]);
    BOOL Ok;

    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    ok(Block && Block->Size >= sizeof(*Block), "Missing CFG initialization block\n");
    if (!Block || Block->Size < sizeof(*Block))
        return;
    ok(GetProcessMitigationPolicy(GetCurrentProcess(), ProcessControlFlowGuardPolicy, &Policy, sizeof(Policy)),
       "GetProcessMitigationPolicy failed with %lu\n", GetLastError());
    ok_hex(Policy.Flags & 3, Expected);
    ok(GetProcessMitigationPolicy(GetCurrentProcess(), ProcessMitigationOptionsMask, OptionsMask, sizeof(OptionsMask)),
       "Getting supported mitigation options failed with %lu\n", GetLastError());
    ok_hex((ULONG)(OptionsMask[0] >> 40) & 3, 3);
    ok_int(Block->CfgBitMap != 0, Expected != 0);
    ok_size_t(Block->CfgBitMapSize, Expected ? 1ULL << 41 : 0);
    ok_ptr((PVOID)(ULONG_PTR)Block->ScpCfgCheckFunction, (PVOID)(ULONG_PTR)(Page + 320));
    ok_ptr((PVOID)(ULONG_PTR)Block->ScpCfgCheckESFunction, (PVOID)(ULONG_PTR)(Page + 448));
    ok_ptr((PVOID)(ULONG_PTR)Block->ScpCfgDispatchFunction, (PVOID)(ULONG_PTR)(Page + 64));
    ok_ptr((PVOID)(ULONG_PTR)Block->ScpCfgDispatchESFunction, (PVOID)(ULONG_PTR)(Page + 192));
    Ws.VirtualAddress = (PVOID)(ULONG_PTR)Page;
    ok_hex(*(volatile ULONG *)Ws.VirtualAddress, 64);
    ok_hex(NtQueryVirtualMemory(NtCurrentProcess(), NULL, MemoryWorkingSetExList, &Ws, sizeof(Ws), NULL), STATUS_SUCCESS);
    ok(Ws.VirtualAttributes.Valid && Ws.VirtualAttributes.Shared && Ws.VirtualAttributes.ShareCount,
       "Guard page working set flags %#Ix\n", Ws.VirtualAttributes.Flags);
    if (!Reject && Block->ScpCfgDispatchFunction)
    {
        CheckGuardImage(Block, Expected, FALSE);
        CheckGuardImage(Block, Expected, TRUE);
    }
    if (!Expected || !Block->CfgBitMap || !Block->ScpCfgCheckFunction || !Block->ScpCfgDispatchFunction ||
        !Block->ScpCfgCheckESFunction || !Block->ScpCfgDispatchESFunction)
        return;

    for (Index = 0; Index < 2; ++Index)
    {
        SCP_CHECK Check = (SCP_CHECK)(ULONG_PTR)(Index ? Block->ScpCfgCheckESFunction : Block->ScpCfgCheckFunction);
        PVOID Dispatch = (PVOID)(ULONG_PTR)(Index ? Block->ScpCfgDispatchESFunction : Block->ScpCfgDispatchFunction);
        Check(ScpTestTarget);
        ok_hex(TestScpDispatch(Args[0], Args[1], Args[2], Args[3], Args[4], Dispatch, ScpTestTarget), Result);
    }
    Allocation = VirtualAlloc(NULL, PAGE_SIZE, MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READ | PAGE_TARGETS_INVALID);
    ok(Allocation != NULL, "VirtualAlloc failed with %lu\n", GetLastError());
    if (!Allocation)
        return;
    if (Reject)
    {
        ((SCP_CHECK)(ULONG_PTR)Block->ScpCfgCheckFunction)(Allocation + Target.Offset);
        ok(0, "CFG accepted an unregistered target\n");
        VirtualFree(Allocation, 0, MEM_RELEASE);
        return;
    }
    Process = OpenProcess(PROCESS_VM_OPERATION, FALSE, GetCurrentProcessId());
    ok(SetTargets != NULL && Process != NULL, "Cannot register a call target with a process handle\n");
    if (SetTargets && Process)
    {
        ULONG_PTR Address = (ULONG_PTR)Allocation + Target.Offset;
        PUCHAR Byte = (PUCHAR)(ULONG_PTR)Block->CfgBitMap + (Address >> 6);
        Ok = SetTargets(Process, Allocation, PAGE_SIZE, 1, &Target);
        ok(Ok, "Registration failed with %lu\n", GetLastError());
        if (Ok)
        {
            ok_hex(Target.Flags, CFG_CALL_TARGET_VALID | CFG_CALL_TARGET_PROCESSED);
            ok_size_t(VirtualQuery(Byte, &Memory, sizeof(Memory)), sizeof(Memory));
            ok_hex(Memory.Protect, PAGE_READONLY);
            ok_int((*Byte >> ((Address >> 3) & 6)) & 3, 1);
            Ws.VirtualAddress = Byte;
            ok_hex(NtQueryVirtualMemory(NtCurrentProcess(), NULL, MemoryWorkingSetExList, &Ws, sizeof(Ws), NULL), STATUS_SUCCESS);
            ok(Ws.VirtualAttributes.Valid && !Ws.VirtualAttributes.Shared && !Ws.VirtualAttributes.ShareCount,
               "Bitmap working set flags %#Ix\n", Ws.VirtualAttributes.Flags);
            ((SCP_CHECK)(ULONG_PTR)Block->ScpCfgCheckFunction)((PVOID)Address);
            Target.Flags = 0;
            ok(SetTargets(Process, Allocation, PAGE_SIZE, 1, &Target), "Revocation failed with %lu\n", GetLastError());
            ok_int((*Byte >> ((Address >> 3) & 6)) & 3, 0);
        }
    }
    if (Process)
        CloseHandle(Process);
    VirtualFree(Allocation, 0, MEM_RELEASE);
}

static VOID
RunGuardChild(const char *Exe, ULONG Policy, BOOL Reject)
{
    STARTUPINFOEXA Startup = {0};
    PROCESS_INFORMATION Process;
    ULONGLONG Option = (ULONGLONG)(Policy ? Policy : 2) << 40;
    CHAR Command[MAX_PATH * 2];
    SIZE_T Size = 0;
    DWORD Wait, Code;
    BOOL Ok;
    InitializeProcThreadAttributeList(NULL, 1, 0, &Size);
    Startup.StartupInfo.cb = sizeof(Startup);
    Startup.lpAttributeList = HeapAlloc(GetProcessHeap(), 0, Size);
    ok(Startup.lpAttributeList != NULL, "No memory for attributes\n");
    if (!Startup.lpAttributeList)
        return;
    Ok = InitializeProcThreadAttributeList(Startup.lpAttributeList, 1, 0, &Size);
    ok(Ok, "Cannot initialize process attributes, error %lu\n", GetLastError());
    if (Ok)
    {
        Ok = UpdateProcThreadAttribute(Startup.lpAttributeList, 0, PROC_THREAD_ATTRIBUTE_MITIGATION_POLICY,
                                       &Option, sizeof(Option), NULL, NULL);
        ok(Ok, "Cannot set creation policy, error %lu\n", GetLastError());
        if (Ok)
        {
            _snprintf(Command, sizeof(Command), "\"%s\" LdrScp cfg %lu %u", Exe, Policy, Reject);
            Ok = CreateProcessA(Exe, Command, NULL, NULL, FALSE, EXTENDED_STARTUPINFO_PRESENT,
                                NULL, NULL, &Startup.StartupInfo, &Process);
            ok(Ok, "Cannot create CFG child, error %lu\n", GetLastError());
            if (Ok)
            {
                Wait = WaitForSingleObject(Process.hProcess, 30000);
                ok_hex(Wait, WAIT_OBJECT_0);
                if (Wait != WAIT_OBJECT_0)
                    TerminateProcess(Process.hProcess, 1);
                Ok = GetExitCodeProcess(Process.hProcess, &Code);
                ok(Ok, "Cannot read child status, error %lu\n", GetLastError());
                if (Ok)
                    ok_hex(Code, Reject ? (DWORD)STATUS_STACK_BUFFER_OVERRUN : 0);
                CloseHandle(Process.hThread);
                CloseHandle(Process.hProcess);
            }
        }
        DeleteProcThreadAttributeList(Startup.lpAttributeList);
    }
    HeapFree(GetProcessHeap(), 0, Startup.lpAttributeList);
}

START_TEST(LdrScp)
{
    WCHAR Directory[MAX_PATH], Path[MAX_PATH];
    BOOL Hotpatch = FALSE;
    OVERRIDE_FIXTURE Fixture;
    DWORD Length = GetTempPathW(ARRAYSIZE(Directory), Directory);
    char **Argv;
    int Argc = winetest_get_mainargs(&Argv);

    if (Argc == 5 && !strcmp(Argv[2], "cfg"))
    {
        CheckProcessGuard((ULONG)atoi(Argv[3]), atoi(Argv[4]) != 0);
        return;
    }
    RunGuardChild(Argv[0], 0, FALSE);
    RunGuardChild(Argv[0], 1, FALSE);
    RunGuardChild(Argv[0], 3, FALSE);
    RunGuardChild(Argv[0], 1, TRUE);

    PULONG InitBlockSize = (PULONG)GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "LdrSystemDllInitBlock");
    PVOID Module = GetModuleHandleW(NULL);
    SIZE_T Size = sizeof(MEMORY_IMAGE_EXTENSION_INFORMATION);

    ok(InitBlockSize != NULL, "Missing LdrSystemDllInitBlock\n");
    if (InitBlockSize)
    {
        MEMORY_BASIC_INFORMATION Memory;
        SIZE_T Bytes = VirtualQuery(InitBlockSize, &Memory, sizeof(Memory));
        ok(*InitBlockSize >= 0x128, "Initialization block too small for 26100: %#lx\n", *InitBlockSize);
        ok_size_t(Bytes, sizeof(Memory));
        if (Bytes == sizeof(Memory))
            ok_hex(Memory.Protect, PAGE_READONLY);
    }

    CheckQueryFailure(NULL, MemoryImageExtensionCfgScp, 0, Size, STATUS_INVALID_PARAMETER);
    CheckQueryFailure(Module, MemoryImageExtensionCfgScp, 0, Size - 1, STATUS_INFO_LENGTH_MISMATCH);
    CheckQueryFailure(Module, MemoryImageExtensionCfgScp, 1, Size, STATUS_INVALID_PARAMETER);
    CheckQueryFailure(Module, MemoryImageExtensionCfgEmulatedScp, 0, Size, STATUS_NOT_SUPPORTED);
    CheckQueryFailure(Module, MemoryImageExtensionTypeMax, 0, Size, STATUS_INVALID_PARAMETER);
    CheckQueryFailure(&Size, MemoryImageExtensionTypeMax, 0, Size, STATUS_INVALID_ADDRESS);
    CheckQueryFailure((PVOID)(ULONG_PTR)1, MemoryImageExtensionCfgScp, 0, Size, STATUS_MEMORY_NOT_ALLOCATED);

    if (!Length || Length >= ARRAYSIZE(Directory) || !GetTempFileNameW(Directory, L"scp", 0, Path))
    {
        ok(0, "Cannot create fixture path, error %lu\n", GetLastError());
        return;
    }

    do
    {
        if (!WriteFixture(Path, Hotpatch, ValidOverride))
        {
            ok(0, "Cannot write DLL, error %lu\n", GetLastError());
            break;
        }
        CheckSection(Path, Hotpatch, FALSE, FALSE);
        CheckSection(Path, Hotpatch, TRUE, FALSE);
        CheckMapping(Path, Hotpatch, 0);
        CheckMapping(Path, Hotpatch, DONT_RESOLVE_DLL_REFERENCES);
        Hotpatch = !Hotpatch;
    } while (Hotpatch);
    for (Fixture = EmptyOverrideList; Fixture <= EmptyOverrideFixupBlock; ++Fixture)
    {
        if (!WriteFixture(Path, TRUE, Fixture))
        {
            ok(0, "Cannot write override fixture %u, error %lu\n", Fixture, GetLastError());
            break;
        }
        CheckSection(Path, TRUE, FALSE, TRUE);
        CheckSection(Path, TRUE, TRUE, TRUE);
    }
    DeleteFileW(Path);
}
