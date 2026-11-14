/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests that an emulated AMD64 program starts as an AMD64 process
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"
#include <ndk/mmfuncs.h>
#include <ndk/obfuncs.h>

static
BOOLEAN
IsAmd64Emulated(VOID)
{
    SYSTEM_SUPPORTED_PROCESSOR_ARCHITECTURES_INFORMATION Machines[8];
    HANDLE Process = NULL;
    NTSTATUS Status;
    ULONG Index;

    RtlZeroMemory(Machines, sizeof(Machines));
    Status = NtQuerySystemInformationEx(SystemSupportedProcessorArchitectures2, &Process, sizeof(Process),
                                        Machines, sizeof(Machines) - sizeof(Machines[0]), NULL);
    if (!NT_SUCCESS(Status))
        return FALSE;

    for (Index = 0; Machines[Index].Machine; Index++)
    {
        if (Machines[Index].Machine == IMAGE_FILE_MACHINE_AMD64)
            return Machines[Index].UserMode && !Machines[Index].Native && !Machines[Index].WoW64Container;
    }
    return FALSE;
}

static
BOOL
WriteAmd64Image(PCWSTR Path)
{
    static const UCHAR Code[] = { 0x31, 0xC0, 0xC3 };
    UCHAR Image[0x400];
    PIMAGE_DOS_HEADER DosHeader = (PIMAGE_DOS_HEADER)Image;
    PIMAGE_NT_HEADERS64 NtHeaders = (PIMAGE_NT_HEADERS64)(Image + 0x40);
    PIMAGE_SECTION_HEADER Section;
    HANDLE File;
    DWORD Written;
    BOOL Result;

    RtlZeroMemory(Image, sizeof(Image));
    DosHeader->e_magic = IMAGE_DOS_SIGNATURE;
    DosHeader->e_lfanew = 0x40;
    NtHeaders->Signature = IMAGE_NT_SIGNATURE;
    NtHeaders->FileHeader.Machine = IMAGE_FILE_MACHINE_AMD64;
    NtHeaders->FileHeader.NumberOfSections = 1;
    NtHeaders->FileHeader.SizeOfOptionalHeader = sizeof(IMAGE_OPTIONAL_HEADER64);
    NtHeaders->FileHeader.Characteristics = IMAGE_FILE_EXECUTABLE_IMAGE | IMAGE_FILE_LARGE_ADDRESS_AWARE;
    NtHeaders->OptionalHeader.Magic = IMAGE_NT_OPTIONAL_HDR64_MAGIC;
    NtHeaders->OptionalHeader.SizeOfCode = 0x200;
    NtHeaders->OptionalHeader.AddressOfEntryPoint = 0x1000;
    NtHeaders->OptionalHeader.BaseOfCode = 0x1000;
    NtHeaders->OptionalHeader.ImageBase = 0x140000000ULL;
    NtHeaders->OptionalHeader.SectionAlignment = 0x1000;
    NtHeaders->OptionalHeader.FileAlignment = 0x200;
    NtHeaders->OptionalHeader.MajorOperatingSystemVersion = 6;
    NtHeaders->OptionalHeader.MajorSubsystemVersion = 6;
    NtHeaders->OptionalHeader.SizeOfImage = 0x2000;
    NtHeaders->OptionalHeader.SizeOfHeaders = 0x200;
    NtHeaders->OptionalHeader.Subsystem = IMAGE_SUBSYSTEM_WINDOWS_CUI;
    NtHeaders->OptionalHeader.DllCharacteristics = IMAGE_DLLCHARACTERISTICS_DYNAMIC_BASE |
                                                   IMAGE_DLLCHARACTERISTICS_NX_COMPAT;
    NtHeaders->OptionalHeader.SizeOfStackReserve = 0x100000;
    NtHeaders->OptionalHeader.SizeOfStackCommit = 0x1000;
    NtHeaders->OptionalHeader.SizeOfHeapReserve = 0x100000;
    NtHeaders->OptionalHeader.SizeOfHeapCommit = 0x1000;
    NtHeaders->OptionalHeader.NumberOfRvaAndSizes = IMAGE_NUMBEROF_DIRECTORY_ENTRIES;
    Section = IMAGE_FIRST_SECTION(NtHeaders);
    RtlCopyMemory(Section->Name, ".text", 5);
    Section->Misc.VirtualSize = sizeof(Code);
    Section->VirtualAddress = 0x1000;
    Section->SizeOfRawData = 0x200;
    Section->PointerToRawData = 0x200;
    Section->Characteristics = IMAGE_SCN_CNT_CODE | IMAGE_SCN_MEM_EXECUTE | IMAGE_SCN_MEM_READ;
    RtlCopyMemory(Image + 0x200, Code, sizeof(Code));

    File = CreateFileW(Path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (File == INVALID_HANDLE_VALUE)
        return FALSE;
    Result = WriteFile(File, Image, sizeof(Image), &Written, NULL) && Written == sizeof(Image);
    CloseHandle(File);
    return Result;
}

static
NTSTATUS
MapImageInto(HANDLE Section, HANDLE Process)
{
    PVOID Base = NULL;
    SIZE_T Size = 0;
    NTSTATUS Status;

    Status = NtMapViewOfSection(Section, Process, &Base, 0, 0, NULL, &Size, ViewUnmap, 0, PAGE_READONLY);
    if (NT_SUCCESS(Status))
        NtUnmapViewOfSection(Process, Base);
    return Status;
}

static
BOOL
WriteBinaryFile(PCWSTR Path, const VOID *Data, DWORD Size)
{
    HANDLE File;
    DWORD Written = 0;
    BOOL Result;

    File = CreateFileW(Path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (File == INVALID_HANDLE_VALUE)
        return FALSE;
    Result = WriteFile(File, Data, Size, &Written, NULL) && Written == Size;
    CloseHandle(File);
    return Result;
}

static
BOOL
WriteArm64EcV2Dll(PCWSTR Path)
{
    static const ULONG Arm64DllMain[] = { 0x52800020, 0xD65F03C0 };
    static const UCHAR X64Export[] = { 0xB8, 0x2A, 0x00, 0x00, 0x00, 0xC3 };
    DECLSPEC_ALIGN(8) UCHAR Image[0xE00];
    PIMAGE_DOS_HEADER DosHeader = (PIMAGE_DOS_HEADER)Image;
    PIMAGE_NT_HEADERS64 NtHeaders = (PIMAGE_NT_HEADERS64)(Image + 0x40);
    PIMAGE_SECTION_HEADER Section;
    PIMAGE_LOAD_CONFIG_DIRECTORY64 LoadConfig;
    PULONG Metadata, CodeMap, ExtraRfe;
    PIMAGE_EXPORT_DIRECTORY ExportDir;
    PIMAGE_BASE_RELOCATION BaseReloc;

    C_ASSERT(0x40 + sizeof(IMAGE_NT_HEADERS64) + 5 * sizeof(IMAGE_SECTION_HEADER) <= 0x400);
    C_ASSERT(sizeof(IMAGE_LOAD_CONFIG_DIRECTORY64) <= 0x140);

    RtlZeroMemory(Image, sizeof(Image));
    DosHeader->e_magic = IMAGE_DOS_SIGNATURE;
    DosHeader->e_lfanew = 0x40;
    NtHeaders->Signature = IMAGE_NT_SIGNATURE;
    NtHeaders->FileHeader.Machine = IMAGE_FILE_MACHINE_AMD64;
    NtHeaders->FileHeader.NumberOfSections = 5;
    NtHeaders->FileHeader.SizeOfOptionalHeader = sizeof(IMAGE_OPTIONAL_HEADER64);
    NtHeaders->FileHeader.Characteristics = IMAGE_FILE_EXECUTABLE_IMAGE |
                                            IMAGE_FILE_LARGE_ADDRESS_AWARE |
                                            IMAGE_FILE_DLL;
    NtHeaders->OptionalHeader.Magic = IMAGE_NT_OPTIONAL_HDR64_MAGIC;
    NtHeaders->OptionalHeader.SizeOfCode = 0x400;
    NtHeaders->OptionalHeader.AddressOfEntryPoint = 0x1000;
    NtHeaders->OptionalHeader.BaseOfCode = 0x1000;
    NtHeaders->OptionalHeader.ImageBase = 0x180000000ULL;
    NtHeaders->OptionalHeader.SectionAlignment = 0x1000;
    NtHeaders->OptionalHeader.FileAlignment = 0x200;
    NtHeaders->OptionalHeader.MajorOperatingSystemVersion = 6;
    NtHeaders->OptionalHeader.MajorSubsystemVersion = 6;
    NtHeaders->OptionalHeader.SizeOfImage = 0x6000;
    NtHeaders->OptionalHeader.SizeOfHeaders = 0x400;
    NtHeaders->OptionalHeader.Subsystem = IMAGE_SUBSYSTEM_WINDOWS_CUI;
    NtHeaders->OptionalHeader.DllCharacteristics = IMAGE_DLLCHARACTERISTICS_DYNAMIC_BASE |
                                                   IMAGE_DLLCHARACTERISTICS_NX_COMPAT;
    NtHeaders->OptionalHeader.SizeOfStackReserve = 0x100000;
    NtHeaders->OptionalHeader.SizeOfStackCommit = 0x1000;
    NtHeaders->OptionalHeader.SizeOfHeapReserve = 0x100000;
    NtHeaders->OptionalHeader.SizeOfHeapCommit = 0x1000;
    NtHeaders->OptionalHeader.NumberOfRvaAndSizes = IMAGE_NUMBEROF_DIRECTORY_ENTRIES;
    NtHeaders->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT].VirtualAddress = 0x21D0;
    NtHeaders->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT].Size = 0x30;
    NtHeaders->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC].VirtualAddress = 0x4000;
    NtHeaders->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC].Size = 0x0C;
    NtHeaders->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_LOAD_CONFIG].VirtualAddress = 0x2000;
    NtHeaders->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_LOAD_CONFIG].Size = sizeof(IMAGE_LOAD_CONFIG_DIRECTORY64);

    Section = IMAGE_FIRST_SECTION(NtHeaders);
    RtlCopyMemory(Section[0].Name, ".text", 5);
    Section[0].Misc.VirtualSize = 0x1000;
    Section[0].VirtualAddress = 0x1000;
    Section[0].SizeOfRawData = 0x200;
    Section[0].PointerToRawData = 0x400;
    Section[0].Characteristics = IMAGE_SCN_CNT_CODE | IMAGE_SCN_MEM_EXECUTE | IMAGE_SCN_MEM_READ;

    RtlCopyMemory(Section[1].Name, ".rdata", 6);
    Section[1].Misc.VirtualSize = 0x1000;
    Section[1].VirtualAddress = 0x2000;
    Section[1].SizeOfRawData = 0x200;
    Section[1].PointerToRawData = 0x600;
    Section[1].Characteristics = IMAGE_SCN_CNT_INITIALIZED_DATA | IMAGE_SCN_MEM_READ;

    RtlCopyMemory(Section[2].Name, ".data", 5);
    Section[2].Misc.VirtualSize = 0x1000;
    Section[2].VirtualAddress = 0x3000;
    Section[2].SizeOfRawData = 0x200;
    Section[2].PointerToRawData = 0x800;
    Section[2].Characteristics = IMAGE_SCN_CNT_INITIALIZED_DATA | IMAGE_SCN_MEM_READ | IMAGE_SCN_MEM_WRITE;

    RtlCopyMemory(Section[3].Name, ".reloc", 6);
    Section[3].Misc.VirtualSize = 0x0C;
    Section[3].VirtualAddress = 0x4000;
    Section[3].SizeOfRawData = 0x200;
    Section[3].PointerToRawData = 0xA00;
    Section[3].Characteristics = IMAGE_SCN_CNT_INITIALIZED_DATA | IMAGE_SCN_MEM_READ | IMAGE_SCN_MEM_DISCARDABLE;

    RtlCopyMemory(Section[4].Name, ".hexpthk", 8);
    Section[4].Misc.VirtualSize = 0x1000;
    Section[4].VirtualAddress = 0x5000;
    Section[4].SizeOfRawData = 0x200;
    Section[4].PointerToRawData = 0xC00;
    Section[4].Characteristics = IMAGE_SCN_CNT_CODE | IMAGE_SCN_MEM_EXECUTE | IMAGE_SCN_MEM_READ;

    RtlCopyMemory(Image + 0x400, Arm64DllMain, sizeof(Arm64DllMain));

    LoadConfig = (PIMAGE_LOAD_CONFIG_DIRECTORY64)(Image + 0x600);
    LoadConfig->Size = sizeof(*LoadConfig);
    LoadConfig->CHPEMetadataPointer = 0x180002140ULL;

    Metadata = (PULONG)(Image + 0x600 + 0x140);
    Metadata[0] = 2;
    Metadata[1] = 0x21B8;
    Metadata[2] = 2;
    Metadata[5] = 0x3000;
    Metadata[6] = 0x3008;
    Metadata[7] = 0x3010;
    Metadata[8] = 0x3018;
    Metadata[9] = 0x3020;
    Metadata[14] = 0x3028;
    Metadata[15] = 0x3030;
    Metadata[16] = 0x21C8;
    Metadata[17] = 8;
    Metadata[18] = 0x3038;

    CodeMap = (PULONG)(Image + 0x600 + 0x1B8);
    CodeMap[0] = 0x1000 | 1;
    CodeMap[1] = 0x1000;
    CodeMap[2] = 0x5000 | 2;
    CodeMap[3] = 0x1000;

    ExtraRfe = (PULONG)(Image + 0x600 + 0x1C8);
    ExtraRfe[0] = 0x1000;
    ExtraRfe[1] = 0x00000009;

    ExportDir = (PIMAGE_EXPORT_DIRECTORY)(Image + 0x600 + 0x1D0);
    ExportDir->Name = 0x21FC;
    ExportDir->Base = 1;
    ExportDir->NumberOfFunctions = 1;
    ExportDir->AddressOfFunctions = 0x21F8;
    *(PULONG)(Image + 0x600 + 0x1F8) = 0x5000;
    RtlCopyMemory(Image + 0x600 + 0x1FC, "v2", 3);

    BaseReloc = (PIMAGE_BASE_RELOCATION)(Image + 0xA00);
    BaseReloc->VirtualAddress = 0x2000;
    BaseReloc->SizeOfBlock = 0x0C;
    *(PUSHORT)(Image + 0xA08) = (USHORT)((IMAGE_REL_BASED_DIR64 << 12) |
                                         FIELD_OFFSET(IMAGE_LOAD_CONFIG_DIRECTORY64, CHPEMetadataPointer));

    RtlCopyMemory(Image + 0xC00, X64Export, sizeof(X64Export));
    return WriteBinaryFile(Path, Image, sizeof(Image));
}

static
BOOL
DeleteTemporaryFile(PCWSTR Path)
{
    ULONG Index;
    BOOL Result;

    for (Index = 0; !(Result = DeleteFileW(Path)) && GetLastError() == ERROR_ACCESS_DENIED && Index < 100; Index++)
        Sleep(100);
    return Result;
}

static
BOOL
WriteAmd64ChpeV2Exe(PCWSTR Path, PCSTR DllFileName)
{
    static const UCHAR Code[] =
    {
        0x48, 0x83, 0xEC, 0x38, 0x48, 0x8D, 0x35, 0xF5, 0x0F, 0x00, 0x00, 0xFF,
        0x56, 0x20, 0xB9, 0x01, 0x00, 0x00, 0x00, 0x83, 0xF8, 0x2A, 0x75, 0x7E,
        0x48, 0x8B, 0x7E, 0x20, 0x48, 0x81, 0xEF, 0x00, 0x50, 0x00, 0x00, 0x48,
        0x8D, 0x8F, 0x00, 0x10, 0x00, 0x00, 0xFF, 0x16, 0xB9, 0x02, 0x00, 0x00,
        0x00, 0x3C, 0x01, 0x75, 0x61, 0x48, 0x8D, 0x8F, 0x00, 0x50, 0x00, 0x00,
        0xFF, 0x16, 0xB9, 0x03, 0x00, 0x00, 0x00, 0x84, 0xC0, 0x75, 0x4F, 0x48,
        0xC7, 0x44, 0x24, 0x30, 0x00, 0x00, 0x00, 0x00, 0xC7, 0x44, 0x24, 0x28,
        0x00, 0x00, 0x00, 0x00, 0x48, 0x8D, 0x8F, 0x00, 0x10, 0x00, 0x00, 0x48,
        0x8D, 0x54, 0x24, 0x30, 0x4C, 0x8D, 0x44, 0x24, 0x28, 0xFF, 0x56, 0x08,
        0x48, 0x29, 0xF8, 0xB9, 0x04, 0x00, 0x00, 0x00, 0x48, 0x3D, 0xC8, 0x21,
        0x00, 0x00, 0x75, 0x1A, 0xB9, 0x05, 0x00, 0x00, 0x00, 0x48, 0x39, 0x7C,
        0x24, 0x30, 0x75, 0x0E, 0xB9, 0x06, 0x00, 0x00, 0x00, 0x83, 0x7C, 0x24,
        0x28, 0x08, 0x75, 0x02, 0x31, 0xC9, 0xFF, 0x56, 0x10, 0xCC
    };
    DECLSPEC_ALIGN(8) UCHAR Image[0x600];
    PIMAGE_DOS_HEADER DosHeader = (PIMAGE_DOS_HEADER)Image;
    PIMAGE_NT_HEADERS64 NtHeaders = (PIMAGE_NT_HEADERS64)(Image + 0x40);
    PIMAGE_SECTION_HEADER Section;
    PIMAGE_IMPORT_DESCRIPTOR Imports;
    PULONGLONG Iat, Int;

    C_ASSERT(0x40 + sizeof(IMAGE_NT_HEADERS64) + 2 * sizeof(IMAGE_SECTION_HEADER) <= 0x200);

    RtlZeroMemory(Image, sizeof(Image));
    DosHeader->e_magic = IMAGE_DOS_SIGNATURE;
    DosHeader->e_lfanew = 0x40;
    NtHeaders->Signature = IMAGE_NT_SIGNATURE;
    NtHeaders->FileHeader.Machine = IMAGE_FILE_MACHINE_AMD64;
    NtHeaders->FileHeader.NumberOfSections = 2;
    NtHeaders->FileHeader.SizeOfOptionalHeader = sizeof(IMAGE_OPTIONAL_HEADER64);
    NtHeaders->FileHeader.Characteristics = IMAGE_FILE_EXECUTABLE_IMAGE | IMAGE_FILE_LARGE_ADDRESS_AWARE;
    NtHeaders->OptionalHeader.Magic = IMAGE_NT_OPTIONAL_HDR64_MAGIC;
    NtHeaders->OptionalHeader.SizeOfCode = 0x200;
    NtHeaders->OptionalHeader.AddressOfEntryPoint = 0x1000;
    NtHeaders->OptionalHeader.BaseOfCode = 0x1000;
    NtHeaders->OptionalHeader.ImageBase = 0x140000000ULL;
    NtHeaders->OptionalHeader.SectionAlignment = 0x1000;
    NtHeaders->OptionalHeader.FileAlignment = 0x200;
    NtHeaders->OptionalHeader.MajorOperatingSystemVersion = 6;
    NtHeaders->OptionalHeader.MajorSubsystemVersion = 6;
    NtHeaders->OptionalHeader.SizeOfImage = 0x3000;
    NtHeaders->OptionalHeader.SizeOfHeaders = 0x200;
    NtHeaders->OptionalHeader.Subsystem = IMAGE_SUBSYSTEM_WINDOWS_CUI;
    NtHeaders->OptionalHeader.DllCharacteristics = IMAGE_DLLCHARACTERISTICS_DYNAMIC_BASE |
                                                   IMAGE_DLLCHARACTERISTICS_NX_COMPAT;
    NtHeaders->OptionalHeader.SizeOfStackReserve = 0x100000;
    NtHeaders->OptionalHeader.SizeOfStackCommit = 0x1000;
    NtHeaders->OptionalHeader.SizeOfHeapReserve = 0x100000;
    NtHeaders->OptionalHeader.SizeOfHeapCommit = 0x1000;
    NtHeaders->OptionalHeader.NumberOfRvaAndSizes = IMAGE_NUMBEROF_DIRECTORY_ENTRIES;
    NtHeaders->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress = 0x2060;
    NtHeaders->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].Size = 3 * sizeof(IMAGE_IMPORT_DESCRIPTOR);
    NtHeaders->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IAT].VirtualAddress = 0x2000;
    NtHeaders->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IAT].Size = 0x30;

    Section = IMAGE_FIRST_SECTION(NtHeaders);
    RtlCopyMemory(Section[0].Name, ".text", 5);
    Section[0].Misc.VirtualSize = sizeof(Code);
    Section[0].VirtualAddress = 0x1000;
    Section[0].SizeOfRawData = 0x200;
    Section[0].PointerToRawData = 0x200;
    Section[0].Characteristics = IMAGE_SCN_CNT_CODE | IMAGE_SCN_MEM_EXECUTE | IMAGE_SCN_MEM_READ;

    RtlCopyMemory(Section[1].Name, ".rdata", 6);
    Section[1].Misc.VirtualSize = 0x200;
    Section[1].VirtualAddress = 0x2000;
    Section[1].SizeOfRawData = 0x200;
    Section[1].PointerToRawData = 0x400;
    Section[1].Characteristics = IMAGE_SCN_CNT_INITIALIZED_DATA | IMAGE_SCN_MEM_READ | IMAGE_SCN_MEM_WRITE;

    RtlCopyMemory(Image + 0x200, Code, sizeof(Code));

    Iat = (PULONGLONG)(Image + 0x400);
    Int = (PULONGLONG)(Image + 0x400 + 0x30);
    Iat[0] = Int[0] = 0x20A0;
    Iat[1] = Int[1] = 0x20B0;
    Iat[2] = Int[2] = 0x20CA;
    Iat[3] = Int[3] = 0;
    Iat[4] = Int[4] = IMAGE_ORDINAL_FLAG64 | 1ULL;
    Iat[5] = Int[5] = 0;

    Imports = (PIMAGE_IMPORT_DESCRIPTOR)(Image + 0x400 + 0x60);
    Imports[0].OriginalFirstThunk = 0x2030;
    Imports[0].Name = 0x20E0;
    Imports[0].FirstThunk = 0x2000;

    Imports[1].OriginalFirstThunk = 0x2050;
    Imports[1].Name = 0x20F0;
    Imports[1].FirstThunk = 0x2020;

    RtlCopyMemory(Image + 0x400 + 0xA0 + 2, "RtlIsEcCode", 12);
    RtlCopyMemory(Image + 0x400 + 0xB0 + 2, "RtlLookupFunctionTable", 23);
    RtlCopyMemory(Image + 0x400 + 0xCA + 2, "RtlExitUserProcess", 19);
    RtlCopyMemory(Image + 0x400 + 0xE0, "ntdll.dll", 10);
    if (FAILED(StringCchCopyA((STRSAFE_LPSTR)(Image + 0x400 + 0xF0), 0x110, DllFileName)))
        return FALSE;

    return WriteBinaryFile(Path, Image, sizeof(Image));
}

static
VOID
TestArm64EcV2Module(PCWSTR Directory)
{
    WCHAR DllPath[MAX_PATH], ExePath[MAX_PATH];
    CHAR DllName[64];
    STARTUPINFOW StartupInfo = { sizeof(StartupInfo) };
    PROCESS_INFORMATION ProcessInfo;
    DWORD Pid = GetCurrentProcessId();
    DWORD ExitCode = 0xDEADBEEF;
    BOOL Result;

    if (FAILED(StringCchPrintfA(DllName, ARRAYSIZE(DllName), "emulated_chpev2_%lu.dll", Pid)) ||
        FAILED(StringCchPrintfW(DllPath, ARRAYSIZE(DllPath), L"%semulated_chpev2_%lu.dll", Directory, Pid)) ||
        FAILED(StringCchPrintfW(ExePath, ARRAYSIZE(ExePath), L"%semulated_chpev2_%lu.exe", Directory, Pid)))
    {
        skip("no room for the CHPEv2 test paths\n");
        return;
    }

    if (!WriteArm64EcV2Dll(DllPath))
    {
        skip("cannot write %ls: %lu\n", DllPath, GetLastError());
        return;
    }

    if (!WriteAmd64ChpeV2Exe(ExePath, DllName))
    {
        skip("cannot write %ls: %lu\n", ExePath, GetLastError());
        DeleteTemporaryFile(DllPath);
        return;
    }

    Result = CreateProcessW(ExePath, NULL, NULL, NULL, FALSE, 0, NULL, Directory, &StartupInfo, &ProcessInfo);
    ok(Result, "CreateProcessW failed: %lu\n", GetLastError());
    if (Result)
    {
        ok_long(WaitForSingleObject(ProcessInfo.hProcess, 30000), WAIT_OBJECT_0);
        ok(GetExitCodeProcess(ProcessInfo.hProcess, &ExitCode), "GetExitCodeProcess failed: %lu\n", GetLastError());
        ok_hex(ExitCode, 0);
        TerminateProcess(ProcessInfo.hProcess, 0);
        CloseHandle(ProcessInfo.hThread);
        CloseHandle(ProcessInfo.hProcess);
    }

    ok(DeleteTemporaryFile(ExePath), "DeleteFileW(%ls) failed: %lu\n", ExePath, GetLastError());
    ok(DeleteTemporaryFile(DllPath), "DeleteFileW(%ls) failed: %lu\n", DllPath, GetLastError());
}

START_TEST(EmulatedProcess)
{
    WCHAR Directory[MAX_PATH], Path[MAX_PATH];
    STARTUPINFOW StartupInfo = { sizeof(StartupInfo) };
    PROCESS_INFORMATION ProcessInfo;
    PROCESS_MACHINE_INFORMATION MachineInfo;
    USHORT ProcessMachine, NativeMachine;
    HANDLE File, Section;
    NTSTATUS Status;
    BOOL Result;
    ULONG Index;

    if (!IsAmd64Emulated())
    {
        skip("AMD64 programs are not emulated on this machine\n");
        return;
    }

    if (!GetTempPathW(ARRAYSIZE(Directory), Directory) ||
        FAILED(StringCchPrintfW(Path, ARRAYSIZE(Path), L"%semulated_amd64_%lu.exe",
                                Directory, GetCurrentProcessId())))
    {
        skip("no room for the image: %lu\n", GetLastError());
        return;
    }

    if (!WriteAmd64Image(Path))
    {
        skip("cannot write %ls: %lu\n", Path, GetLastError());
        return;
    }

    Result = CreateProcessW(Path, NULL, NULL, NULL, FALSE, CREATE_SUSPENDED, NULL, NULL,
                            &StartupInfo, &ProcessInfo);
    ok(Result, "CreateProcessW failed: %lu\n", GetLastError());
    if (!Result)
    {
        DeleteFileW(Path);
        return;
    }

    ProcessMachine = NativeMachine = 0xFFFF;
    ok(IsWow64Process2(ProcessInfo.hProcess, &ProcessMachine, &NativeMachine),
       "IsWow64Process2 failed: %lu\n", GetLastError());
    ok_hex(ProcessMachine, IMAGE_FILE_MACHINE_UNKNOWN);
    ok(NativeMachine != IMAGE_FILE_MACHINE_AMD64 && NativeMachine != IMAGE_FILE_MACHINE_UNKNOWN,
       "native machine %#x\n", NativeMachine);

    RtlFillMemory(&MachineInfo, sizeof(MachineInfo), 0xCC);
    ok(GetProcessInformation(ProcessInfo.hProcess, ProcessMachineTypeInfo, &MachineInfo, sizeof(MachineInfo)),
       "GetProcessInformation failed: %lu\n", GetLastError());
    ok_hex(MachineInfo.ProcessMachine, IMAGE_FILE_MACHINE_AMD64);
    ok_hex(MachineInfo.MachineAttributes, UserEnabled);

    File = CreateFileW(Path, GENERIC_READ | GENERIC_EXECUTE, FILE_SHARE_READ | FILE_SHARE_DELETE, NULL,
                       OPEN_EXISTING, 0, NULL);
    ok(File != INVALID_HANDLE_VALUE, "CreateFileW failed: %lu\n", GetLastError());
    if (File != INVALID_HANDLE_VALUE)
    {
        Status = NtCreateSection(&Section, SECTION_MAP_READ | SECTION_MAP_EXECUTE | SECTION_QUERY, NULL, NULL,
                                 PAGE_EXECUTE_READ, SEC_IMAGE, File);
        ok_ntstatus(Status, STATUS_SUCCESS);
        if (NT_SUCCESS(Status))
        {
            Status = MapImageInto(Section, ProcessInfo.hProcess);
            ok(Status == STATUS_SUCCESS || Status == STATUS_IMAGE_NOT_AT_BASE,
               "mapping into the AMD64 process returned %#lx\n", Status);
            ok_ntstatus(MapImageInto(Section, NtCurrentProcess()), STATUS_IMAGE_MACHINE_TYPE_MISMATCH);
            NtClose(Section);
        }
        CloseHandle(File);
    }

    TerminateProcess(ProcessInfo.hProcess, 0);
    WaitForSingleObject(ProcessInfo.hProcess, 5000);
    CloseHandle(ProcessInfo.hThread);
    CloseHandle(ProcessInfo.hProcess);

    ok(SetEnvironmentVariableW(L"__COMPAT_LAYER", L"WIN7RTM"), "SetEnvironmentVariableW failed: %lu\n", GetLastError());
    Result = CreateProcessW(Path, NULL, NULL, NULL, FALSE, 0, NULL, NULL, &StartupInfo, &ProcessInfo);
    ok(Result, "CreateProcessW with a compatibility layer failed: %lu\n", GetLastError());
    SetEnvironmentVariableW(L"__COMPAT_LAYER", NULL);
    if (Result)
    {
        DWORD ExitCode = 0xDEADBEEF;

        ok_long(WaitForSingleObject(ProcessInfo.hProcess, 30000), WAIT_OBJECT_0);
        ok(GetExitCodeProcess(ProcessInfo.hProcess, &ExitCode), "GetExitCodeProcess failed: %lu\n", GetLastError());
        ok_hex(ExitCode, 0);
        TerminateProcess(ProcessInfo.hProcess, 0);
        CloseHandle(ProcessInfo.hThread);
        CloseHandle(ProcessInfo.hProcess);
    }

    /* Windows 11 keeps the image open for a few seconds after a run with a compatibility layer */
    for (Index = 0; !(Result = DeleteFileW(Path)) && GetLastError() == ERROR_ACCESS_DENIED && Index < 100; Index++)
        Sleep(100);
    ok(Result, "DeleteFileW failed: %lu\n", GetLastError());

    if (NativeMachine == IMAGE_FILE_MACHINE_ARM64)
        TestArm64EcV2Module(Directory);
}
