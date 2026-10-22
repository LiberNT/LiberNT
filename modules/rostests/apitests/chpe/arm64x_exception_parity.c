/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     AMD64-on-ARM64 exception dispatch through ARM64X system DLL code
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <windows.h>
#include <stdarg.h>
#include <stdio.h>

#define TEST_EXCEPTION 0xE0A64C00

typedef SIZE_T (__cdecl *PSTRLEN)(const char *String);

static int
log_result(PCSTR Format, ...)
{
    CHAR Buffer[512];
    va_list Arguments;
    int Length;

    va_start(Arguments, Format);
    Length = vsnprintf(Buffer, sizeof(Buffer), Format, Arguments);
    va_end(Arguments);

    Buffer[sizeof(Buffer) - 1] = ANSI_NULL;
    fputs(Buffer, stdout);
    OutputDebugStringA(Buffer);
    return Length;
}

#define printf log_result

static USHORT
mapped_machine(HMODULE Module)
{
    PIMAGE_DOS_HEADER DosHeader = (PIMAGE_DOS_HEADER)Module;

    return ((PIMAGE_NT_HEADERS)((PBYTE)Module + DosHeader->e_lfanew))->FileHeader.Machine;
}

static USHORT
file_machine(HMODULE Module)
{
    WCHAR Path[MAX_PATH];
    IMAGE_DOS_HEADER DosHeader;
    IMAGE_FILE_HEADER FileHeader;
    DWORD Signature, Read;
    USHORT Machine = 0;
    HANDLE File;

    if (!GetModuleFileNameW(Module, Path, ARRAYSIZE(Path)))
        return 0;

    File = CreateFileW(Path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (File == INVALID_HANDLE_VALUE)
        return 0;

    if (ReadFile(File, &DosHeader, sizeof(DosHeader), &Read, NULL) && Read == sizeof(DosHeader) &&
        SetFilePointer(File, DosHeader.e_lfanew, NULL, FILE_BEGIN) != INVALID_SET_FILE_POINTER &&
        ReadFile(File, &Signature, sizeof(Signature), &Read, NULL) && Read == sizeof(Signature) &&
        ReadFile(File, &FileHeader, sizeof(FileHeader), &Read, NULL) && Read == sizeof(FileHeader))
    {
        Machine = FileHeader.Machine;
    }

    CloseHandle(File);
    return Machine;
}

static DWORD
raise_and_catch(VOID)
{
    __try
    {
        RaiseException(TEST_EXCEPTION, 0, 0, NULL);
    }
    __except (GetExceptionCode() == TEST_EXCEPTION ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH)
    {
        return GetExceptionCode();
    }

    return 0;
}

static DWORD
fault_in_crt(PSTRLEN StringLength)
{
    __try
    {
        StringLength(NULL);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return GetExceptionCode();
    }

    return 0;
}

int
main(void)
{
    HMODULE Kernel32, Msvcrt;
    PSTRLEN StringLength;
    PVOID ReadOnlyPage;
    BOOL BadRead, BadWrite, Passed;
    DWORD Raised, Faulted, OldProtect;

    setvbuf(stdout, NULL, _IONBF, 0);
    printf("CHPE_ARM64X_EXCEPTION_PARITY_BEGIN\n");

    Kernel32 = GetModuleHandleW(L"kernel32.dll");
    Msvcrt = LoadLibraryW(L"msvcrt.dll");
    if (!Kernel32 || !Msvcrt)
    {
        printf("FAIL modules kernel32=%p msvcrt=%p error=%lu\n", Kernel32, Msvcrt, GetLastError());
        return 1;
    }

    printf("VIEW kernel32 mapped=0x%04x file=0x%04x msvcrt mapped=0x%04x file=0x%04x\n",
           mapped_machine(Kernel32), file_machine(Kernel32), mapped_machine(Msvcrt), file_machine(Msvcrt));

    BadRead = IsBadReadPtr((PVOID)(ULONG_PTR)0x10, sizeof(ULONG));
    ReadOnlyPage = VirtualAlloc(NULL, 0x1000, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    BadWrite = ReadOnlyPage && VirtualProtect(ReadOnlyPage, 0x1000, PAGE_READONLY, &OldProtect) &&
               IsBadWritePtr(ReadOnlyPage, sizeof(ULONG));
    printf("INTERNAL IsBadReadPtr=%d IsBadWritePtr=%d\n", BadRead, BadWrite);
    if (ReadOnlyPage)
        VirtualFree(ReadOnlyPage, 0, MEM_RELEASE);

    Raised = raise_and_catch();
    printf("RAISE code=0x%08lx\n", Raised);

    StringLength = (PSTRLEN)GetProcAddress(Msvcrt, "strlen");
    Faulted = StringLength ? fault_in_crt(StringLength) : 0;
    printf("FAULT strlen=%p code=0x%08lx\n", StringLength, Faulted);

    Passed = BadRead && BadWrite && Raised == TEST_EXCEPTION && Faulted == EXCEPTION_ACCESS_VIOLATION;
    printf(Passed ? "CHPE_ARM64X_EXCEPTION_PARITY_PASS\n" : "CHPE_ARM64X_EXCEPTION_PARITY_FAIL\n");
    return Passed ? 0 : 2;
}
