/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test CreateProcess with programs of a machine other than the native one
 * COPYRIGHT:   Copyright 2026 mikuta0407 <mikuta0407@mikuta0407.net>
 */

#include "precomp.h"

typedef HRESULT (WINAPI *PFN_GETMACHINETYPEATTRIBUTES)(USHORT, MACHINE_ATTRIBUTES *);

static
USHORT
GetProcessMachine(
    _In_ HANDLE Process)
{
    SYSTEM_SUPPORTED_PROCESSOR_ARCHITECTURES_INFORMATION Machines[8];
    NTSTATUS Status;
    ULONG Index;

    ZeroMemory(Machines, sizeof(Machines));
    Status = NtQuerySystemInformationEx(SystemSupportedProcessorArchitectures2, &Process, sizeof(Process), Machines,
                                        sizeof(Machines) - sizeof(Machines[0]), NULL);
    ok_ntstatus(Status, STATUS_SUCCESS);
    for (Index = 0; Machines[Index].Machine; Index++)
    {
        if (Machines[Index].Process)
            return (USHORT)Machines[Index].Machine;
    }
    return 0;
}

static
BOOL
WriteMinimalImage(
    _In_ PCWSTR Path,
    _In_ USHORT Machine)
{
    static const BYTE Code[] = { 0xB8, 0x2A, 0x00, 0x00, 0x00, 0xC3 };
    BYTE Image[0x400];
    PIMAGE_DOS_HEADER Dos = (PIMAGE_DOS_HEADER)Image;
    PIMAGE_NT_HEADERS64 Nt = (PIMAGE_NT_HEADERS64)(Image + 0x40);
    PIMAGE_SECTION_HEADER Section;
    HANDLE File;
    DWORD Written;
    BOOL Result;

    ZeroMemory(Image, sizeof(Image));
    Dos->e_magic = IMAGE_DOS_SIGNATURE;
    Dos->e_lfanew = 0x40;
    Nt->Signature = IMAGE_NT_SIGNATURE;
    Nt->FileHeader.Machine = Machine;
    Nt->FileHeader.NumberOfSections = 1;
    Nt->FileHeader.SizeOfOptionalHeader = sizeof(IMAGE_OPTIONAL_HEADER64);
    Nt->FileHeader.Characteristics = IMAGE_FILE_EXECUTABLE_IMAGE | IMAGE_FILE_LARGE_ADDRESS_AWARE |
                                     IMAGE_FILE_RELOCS_STRIPPED;
    Nt->OptionalHeader.Magic = IMAGE_NT_OPTIONAL_HDR64_MAGIC;
    Nt->OptionalHeader.AddressOfEntryPoint = 0x1000;
    Nt->OptionalHeader.BaseOfCode = 0x1000;
    Nt->OptionalHeader.ImageBase = 0x140000000ULL;
    Nt->OptionalHeader.SectionAlignment = 0x1000;
    Nt->OptionalHeader.FileAlignment = 0x200;
    Nt->OptionalHeader.MajorOperatingSystemVersion = 6;
    Nt->OptionalHeader.MajorSubsystemVersion = 6;
    Nt->OptionalHeader.SizeOfImage = 0x2000;
    Nt->OptionalHeader.SizeOfHeaders = 0x200;
    Nt->OptionalHeader.Subsystem = IMAGE_SUBSYSTEM_WINDOWS_CUI;
    Nt->OptionalHeader.DllCharacteristics = IMAGE_DLLCHARACTERISTICS_NX_COMPAT;
    Nt->OptionalHeader.SizeOfStackReserve = 0x100000;
    Nt->OptionalHeader.SizeOfStackCommit = 0x1000;
    Nt->OptionalHeader.SizeOfHeapReserve = 0x100000;
    Nt->OptionalHeader.SizeOfHeapCommit = 0x1000;
    Nt->OptionalHeader.NumberOfRvaAndSizes = IMAGE_NUMBEROF_DIRECTORY_ENTRIES;
    Section = IMAGE_FIRST_SECTION(Nt);
    memcpy(Section->Name, ".text", 5);
    Section->Misc.VirtualSize = sizeof(Code);
    Section->VirtualAddress = 0x1000;
    Section->SizeOfRawData = 0x200;
    Section->PointerToRawData = 0x200;
    Section->Characteristics = IMAGE_SCN_CNT_CODE | IMAGE_SCN_MEM_EXECUTE | IMAGE_SCN_MEM_READ;
    memcpy(Image + 0x200, Code, sizeof(Code));

    File = CreateFileW(Path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (File == INVALID_HANDLE_VALUE)
        return FALSE;
    Result = WriteFile(File, Image, sizeof(Image), &Written, NULL) && (Written == sizeof(Image));
    CloseHandle(File);
    return Result;
}

static
void
TestMachine(
    _In_ PFN_GETMACHINETYPEATTRIBUTES pGetMachineTypeAttributes,
    _In_ USHORT Machine,
    _In_ PCWSTR Name)
{
    MACHINE_ATTRIBUTES Attributes = 0;
    STARTUPINFOW StartupInfo = { sizeof(StartupInfo) };
    PROCESS_INFORMATION ProcessInfo;
    WCHAR Path[MAX_PATH];
    DWORD ExitCode = 0, Error;
    HRESULT hr;
    BOOL Result;

    hr = pGetMachineTypeAttributes(Machine, &Attributes);
    ok_hr(hr, S_OK);

    GetTempPathW(RTL_NUMBER_OF(Path), Path);
    wcscat(Path, Name);
    if (!WriteMinimalImage(Path, Machine))
    {
        skip("Cannot write %ls: %lu\n", Path, GetLastError());
        return;
    }

    SetLastError(0xdeadbeef);
    Result = CreateProcessW(Path, NULL, NULL, NULL, FALSE, CREATE_NO_WINDOW | CREATE_SUSPENDED, NULL, NULL,
                            &StartupInfo, &ProcessInfo);
    Error = GetLastError();
    trace("machine %04x attributes %x: CreateProcessW %d error %lu\n", Machine, Attributes, Result, Error);
    if (Attributes & UserEnabled)
    {
        ok(Result, "machine %04x is user enabled, but CreateProcessW failed with %lu\n", Machine, Error);
    }
    else
    {
        ok(!Result, "machine %04x is not user enabled, but CreateProcessW succeeded\n", Machine);
        ok(Error == ERROR_BAD_EXE_FORMAT, "machine %04x: error %lu\n", Machine, Error);
    }

    if (Result)
    {
        ok_int(GetProcessMachine(ProcessInfo.hProcess), Machine);
        ok_long(ResumeThread(ProcessInfo.hThread), 1);
        ok_long(WaitForSingleObject(ProcessInfo.hProcess, 30000), WAIT_OBJECT_0);
        ok(GetExitCodeProcess(ProcessInfo.hProcess, &ExitCode), "GetExitCodeProcess failed: %lu\n", GetLastError());
        ok(ExitCode == 0, "machine %04x: exit code %lx\n", Machine, ExitCode);
        CloseHandle(ProcessInfo.hThread);
        CloseHandle(ProcessInfo.hProcess);
    }
    DeleteFileW(Path);
}

START_TEST(EmulatedMachineProcess)
{
    PFN_GETMACHINETYPEATTRIBUTES pGetMachineTypeAttributes;

    pGetMachineTypeAttributes = (PFN_GETMACHINETYPEATTRIBUTES)GetProcAddress(GetModuleHandleW(L"kernel32.dll"),
                                                                             "GetMachineTypeAttributes");
    if (pGetMachineTypeAttributes == NULL)
    {
        skip("GetMachineTypeAttributes is not available\n");
        return;
    }

    TestMachine(pGetMachineTypeAttributes, IMAGE_FILE_MACHINE_AMD64, L"emx64.exe");
    TestMachine(pGetMachineTypeAttributes, IMAGE_FILE_MACHINE_RISCV64, L"emrv64.exe");
}
