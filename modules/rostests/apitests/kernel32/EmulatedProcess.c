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
}
