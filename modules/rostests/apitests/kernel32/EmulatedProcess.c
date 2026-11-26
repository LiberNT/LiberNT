/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests that an emulated AMD64 program starts as an AMD64 process
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"
#include <ndk/mmfuncs.h>
#include <ndk/obfuncs.h>
#include <ndk/kefuncs.h>

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
WriteAmd64Image(PCWSTR Path, const UCHAR *Code, SIZE_T CodeSize, const UCHAR *Data, SIZE_T DataSize)
{
    UCHAR Image[0x600];
    DWORD ImageSize = Data ? 0x600 : 0x400;
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
    NtHeaders->FileHeader.NumberOfSections = Data ? 2 : 1;
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
    NtHeaders->OptionalHeader.SizeOfImage = Data ? 0x3000 : 0x2000;
    NtHeaders->OptionalHeader.SizeOfHeaders = 0x200;
    NtHeaders->OptionalHeader.Subsystem = IMAGE_SUBSYSTEM_WINDOWS_CUI;
    NtHeaders->OptionalHeader.DllCharacteristics = (Data ? 0 : IMAGE_DLLCHARACTERISTICS_DYNAMIC_BASE) |
                                                   IMAGE_DLLCHARACTERISTICS_NX_COMPAT;
    NtHeaders->OptionalHeader.SizeOfStackReserve = 0x100000;
    NtHeaders->OptionalHeader.SizeOfStackCommit = 0x1000;
    NtHeaders->OptionalHeader.SizeOfHeapReserve = 0x100000;
    NtHeaders->OptionalHeader.SizeOfHeapCommit = 0x1000;
    NtHeaders->OptionalHeader.NumberOfRvaAndSizes = IMAGE_NUMBEROF_DIRECTORY_ENTRIES;
    Section = IMAGE_FIRST_SECTION(NtHeaders);
    RtlCopyMemory(Section->Name, ".text", 5);
    Section->Misc.VirtualSize = (DWORD)CodeSize;
    Section->VirtualAddress = 0x1000;
    Section->SizeOfRawData = 0x200;
    Section->PointerToRawData = 0x200;
    Section->Characteristics = IMAGE_SCN_CNT_CODE | IMAGE_SCN_MEM_EXECUTE | IMAGE_SCN_MEM_READ;
    RtlCopyMemory(Image + 0x200, Code, CodeSize);
    if (Data)
    {
        NtHeaders->OptionalHeader.SizeOfInitializedData = 0x200;
        NtHeaders->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress = 0x2030;
        NtHeaders->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].Size = 2 * sizeof(IMAGE_IMPORT_DESCRIPTOR);
        NtHeaders->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IAT].VirtualAddress = 0x2000;
        NtHeaders->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IAT].Size = 3 * sizeof(ULONGLONG);
        Section++;
        RtlCopyMemory(Section->Name, ".data", 5);
        Section->Misc.VirtualSize = (DWORD)DataSize;
        Section->VirtualAddress = 0x2000;
        Section->SizeOfRawData = 0x200;
        Section->PointerToRawData = 0x400;
        Section->Characteristics = IMAGE_SCN_CNT_INITIALIZED_DATA | IMAGE_SCN_MEM_READ | IMAGE_SCN_MEM_WRITE;
        RtlCopyMemory(Image + 0x400, Data, DataSize);
    }

    File = CreateFileW(Path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (File == INVALID_HANDLE_VALUE)
        return FALSE;
    Result = WriteFile(File, Image, ImageSize, &Written, NULL) && Written == ImageSize;
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

typedef struct _EC_CALL_DATA
{
    ULONG64 Reserved[6];
    ULONG64 Status;
} EC_CALL_DATA, *PEC_CALL_DATA;

C_ASSERT(FIELD_OFFSET(EC_CALL_DATA, Status) == 48);

static const ULONG EcCode[] =
{
    0x00000000, 0x0000000c, 0x52800028, 0xb9003008,
    0x14000000, 0xd61f0120,
};

#define EC_PROBE    (2 * sizeof(ULONG))
#define EC_DATA     0x100
#define EC_HOST_DATA 0x140002000ULL

typedef struct _EC_HOST
{
    WCHAR Path[MAX_PATH];
    PROCESS_INFORMATION ProcessInfo;
    ULONG_PTR Code;
} EC_HOST, *PEC_HOST;

static
BOOL
StartEcHost(PCWSTR Directory, PEC_HOST Host)
{
    static const UCHAR HostCode[] =
    {
        0x48, 0x83, 0xec, 0x48, 0x48, 0xc7, 0xc1, 0xff, 0xff, 0xff, 0xff, 0x48,
        0x8d, 0x15, 0x9e, 0x10, 0x00, 0x00, 0x4c, 0x8d, 0x05, 0x9f, 0x10, 0x00,
        0x00, 0x41, 0xb9, 0x00, 0x30, 0x00, 0x00, 0xc7, 0x44, 0x24, 0x20, 0x40,
        0x00, 0x00, 0x00, 0x48, 0x8d, 0x05, 0x92, 0x10, 0x00, 0x00, 0x48, 0x89,
        0x44, 0x24, 0x28, 0xc7, 0x44, 0x24, 0x30, 0x01, 0x00, 0x00, 0x00, 0xff,
        0x15, 0xbf, 0x0f, 0x00, 0x00, 0x89, 0x05, 0x91, 0x10, 0x00, 0x00, 0xb9,
        0x01, 0x00, 0x00, 0x00, 0x48, 0x8d, 0x15, 0x7d, 0x10, 0x00, 0x00, 0xff,
        0x15, 0xaf, 0x0f, 0x00, 0x00, 0xeb, 0xec,
    };
    DECLSPEC_ALIGN(8) UCHAR Data[0xE0];
    PIMAGE_IMPORT_DESCRIPTOR Import = (PIMAGE_IMPORT_DESCRIPTOR)(Data + 0x30);
    PMEM_EXTENDED_PARAMETER Parameter = (PMEM_EXTENDED_PARAMETER)(Data + 0xC0);
    STARTUPINFOW StartupInfo = { sizeof(StartupInfo) };
    ULONG AllocationStatus = 0xFFFFFFFF;
    ULONG Index;

    RtlZeroMemory(Data, sizeof(Data));
    *(PULONGLONG)(Data + 0x00) = *(PULONGLONG)(Data + 0x18) = 0x2070;
    *(PULONGLONG)(Data + 0x08) = *(PULONGLONG)(Data + 0x20) = 0x2090;
    Import->OriginalFirstThunk = 0x2018;
    Import->Name = 0x2060;
    Import->FirstThunk = 0x2000;
    RtlCopyMemory(Data + 0x60, "ntdll.dll", sizeof("ntdll.dll"));
    RtlCopyMemory(Data + 0x72, "NtAllocateVirtualMemoryEx", sizeof("NtAllocateVirtualMemoryEx"));
    RtlCopyMemory(Data + 0x92, "NtDelayExecution", sizeof("NtDelayExecution"));
    *(PULONGLONG)(Data + 0xB8) = PAGE_SIZE;
    Parameter->Type = MemExtendedParameterAttributeFlags;
    Parameter->ULong64 = MEM_EXTENDED_PARAMETER_EC_CODE;
    *(PLONGLONG)(Data + 0xD0) = -10 * 1000 * 10;
    *(PULONG)(Data + 0xD8) = 0xFFFFFFFF;

    Host->Code = 0;
    if (FAILED(StringCchPrintfW(Host->Path, ARRAYSIZE(Host->Path), L"%semulated_amd64_ec_%lu.exe",
                                Directory, GetCurrentProcessId())) ||
        !WriteAmd64Image(Host->Path, HostCode, sizeof(HostCode), Data, sizeof(Data)))
    {
        skip("cannot write the EC host image: %lu\n", GetLastError());
        return FALSE;
    }

    if (!CreateProcessW(Host->Path, NULL, NULL, NULL, FALSE, 0, NULL, NULL, &StartupInfo, &Host->ProcessInfo))
    {
        ok(FALSE, "CreateProcessW failed: %lu\n", GetLastError());
        DeleteFileW(Host->Path);
        return FALSE;
    }

    for (Index = 0; Index < 500; Index++)
    {
        if (!ReadProcessMemory(Host->ProcessInfo.hProcess, (PVOID)(ULONG_PTR)(EC_HOST_DATA + 0xD8),
                               &AllocationStatus, sizeof(AllocationStatus), NULL) ||
            AllocationStatus != 0xFFFFFFFF)
        {
            break;
        }
        Sleep(20);
    }
    ok_hex(AllocationStatus, STATUS_SUCCESS);
    ok(ReadProcessMemory(Host->ProcessInfo.hProcess, (PVOID)(ULONG_PTR)(EC_HOST_DATA + 0xB0),
                         &Host->Code, sizeof(Host->Code), NULL),
       "ReadProcessMemory failed: %lu\n", GetLastError());
    if (AllocationStatus == STATUS_SUCCESS && Host->Code)
    {
        ok(WriteProcessMemory(Host->ProcessInfo.hProcess, (PVOID)Host->Code, EcCode, sizeof(EcCode), NULL),
           "WriteProcessMemory failed: %lu\n", GetLastError());
        FlushInstructionCache(Host->ProcessInfo.hProcess, (PVOID)Host->Code, sizeof(EcCode));
        return TRUE;
    }

    Host->Code = 0;
    return TRUE;
}

static
VOID
StopEcHost(PEC_HOST Host)
{
    TerminateProcess(Host->ProcessInfo.hProcess, 0);
    WaitForSingleObject(Host->ProcessInfo.hProcess, 5000);
    CloseHandle(Host->ProcessInfo.hThread);
    CloseHandle(Host->ProcessInfo.hProcess);
    DeleteFileW(Host->Path);
}

static
VOID
RunEcApc(PEC_HOST Host, ULONG_PTR Routine, PEC_CALL_DATA Call)
{
    NTSTATUS Status;
    ULONG Index;

    Call->Status = 0xDEADBEEF;
    ok(WriteProcessMemory(Host->ProcessInfo.hProcess, (PVOID)(Host->Code + EC_DATA), Call, sizeof(*Call), NULL),
       "WriteProcessMemory failed: %lu\n", GetLastError());
    Status = NtQueueApcThread(Host->ProcessInfo.hThread, (PKNORMAL_ROUTINE)(Host->Code + Routine),
                              (PVOID)(Host->Code + EC_DATA), NULL, NULL);
    ok_ntstatus(Status, STATUS_SUCCESS);
    for (Index = 0; NT_SUCCESS(Status) && Index < 500; Index++)
    {
        if (!ReadProcessMemory(Host->ProcessInfo.hProcess, (PVOID)(Host->Code + EC_DATA), Call, sizeof(*Call), NULL) ||
            Call->Status != 0xDEADBEEF)
        {
            break;
        }
        Sleep(20);
    }
    ok_long(WaitForSingleObject(Host->ProcessInfo.hProcess, 0), WAIT_TIMEOUT);
}

static
VOID
TestEcCodeOfEmulatedProgram(PCWSTR Directory)
{
    EC_HOST Host;
    EC_CALL_DATA Call;

    if (!StartEcHost(Directory, &Host))
        return;

    if (Host.Code)
    {
        RtlZeroMemory(&Call, sizeof(Call));
        RunEcApc(&Host, EC_PROBE, &Call);
        ok_hex((ULONG)Call.Status, 1);
    }
    StopEcHost(&Host);
}

START_TEST(EmulatedProcess)
{
    static const UCHAR ReturnZero[] = { 0x31, 0xC0, 0xC3 };
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

    TestEcCodeOfEmulatedProgram(Directory);

    if (!WriteAmd64Image(Path, ReturnZero, sizeof(ReturnZero), NULL, 0))
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
}
