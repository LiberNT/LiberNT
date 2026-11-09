/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests for the address lookup of KernelBase GetModuleHandleExW
 * COPYRIGHT:   Copyright 2026 LuRy <lury@lury.cz>
 */

#include "precomp.h"

typedef BOOL (WINAPI *PGET_MODULE_HANDLE_EX_W)(DWORD, LPCWSTR, HMODULE *);
typedef PVOID (NTAPI *PRTL_PC_TO_FILE_HEADER)(PVOID, PVOID *);

static PRTL_PC_TO_FILE_HEADER RealPcToFileHeader;
static PVOID HookAddress;
static PVOID HookBase;

static
PVOID
NTAPI
HookPcToFileHeader(
    _In_ PVOID PcValue,
    _Out_ PVOID *BaseOfImage)
{
    if (PcValue == HookAddress)
    {
        *BaseOfImage = HookBase;
        return HookBase;
    }
    return RealPcToFileHeader(PcValue, BaseOfImage);
}

static
PVOID *
FindImport(
    _In_ HMODULE Module,
    _In_ PCSTR DllName,
    _In_ PCSTR FunctionName)
{
    PIMAGE_IMPORT_DESCRIPTOR Descriptor;
    PIMAGE_THUNK_DATA Names, Thunks;
    PIMAGE_IMPORT_BY_NAME ByName;
    ULONG Size;

    Descriptor = RtlImageDirectoryEntryToData(Module, TRUE, IMAGE_DIRECTORY_ENTRY_IMPORT, &Size);
    for (; Descriptor && Descriptor->Name; Descriptor++)
    {
        if (_stricmp((PCSTR)Module + Descriptor->Name, DllName))
            continue;
        Names = (PIMAGE_THUNK_DATA)((PBYTE)Module + (Descriptor->OriginalFirstThunk ? Descriptor->OriginalFirstThunk
                                                                                    : Descriptor->FirstThunk));
        Thunks = (PIMAGE_THUNK_DATA)((PBYTE)Module + Descriptor->FirstThunk);
        for (; Names->u1.AddressOfData; Names++, Thunks++)
        {
            if (IMAGE_SNAP_BY_ORDINAL(Names->u1.Ordinal))
                continue;
            ByName = (PIMAGE_IMPORT_BY_NAME)((PBYTE)Module + Names->u1.AddressOfData);
            if (!strcmp((PCSTR)ByName->Name, FunctionName))
                return (PVOID *)&Thunks->u1.Function;
        }
    }
    return NULL;
}

START_TEST(KernelBaseModuleHandle)
{
    const DWORD FromAddress = GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT;
    HMODULE KernelBase = GetModuleHandleW(L"kernelbase.dll"), Module;
    PGET_MODULE_HANDLE_EX_W pGetModuleHandleExW;
    PVOID *Import, Memory;
    DWORD Protect;
    BOOL Result;

    pGetModuleHandleExW = (PGET_MODULE_HANDLE_EX_W)GetProcAddress(KernelBase, "GetModuleHandleExW");
    if (!pGetModuleHandleExW)
    {
        skip("KernelBase has no GetModuleHandleExW\n");
        return;
    }

    Module = NULL;
    Result = pGetModuleHandleExW(FromAddress, (LPCWSTR)pGetModuleHandleExW, &Module);
    ok(Result, "GetModuleHandleExW failed with %lu\n", GetLastError());
    ok(Module == KernelBase, "Module is %p, expected %p\n", Module, KernelBase);

    SetLastError(0xdeadbeef);
    Result = pGetModuleHandleExW(FromAddress | GET_MODULE_HANDLE_EX_FLAG_PIN, (LPCWSTR)pGetModuleHandleExW, &Module);
    ok(!Result, "GetModuleHandleExW succeeded\n");
    ok_err(ERROR_INVALID_PARAMETER);

    Memory = VirtualAlloc(NULL, 0x1000, MEM_COMMIT, PAGE_READWRITE);
    ok(Memory != NULL, "VirtualAlloc failed with %lu\n", GetLastError());
    if (!Memory)
        return;

    SetLastError(0xdeadbeef);
    Module = (HMODULE)(ULONG_PTR)1;
    Result = pGetModuleHandleExW(FromAddress, (LPCWSTR)Memory, &Module);
    ok(!Result, "GetModuleHandleExW succeeded\n");
    ok_err(ERROR_MOD_NOT_FOUND);
    ok(Module == NULL, "Module is %p\n", Module);

    /* Loaders that map DLLs from memory replace this import to resolve addresses in such a DLL */
    Import = FindImport(KernelBase, "ntdll.dll", "RtlPcToFileHeader");
    ok(Import != NULL, "KernelBase does not import RtlPcToFileHeader from ntdll\n");
    if (Import && VirtualProtect(Import, sizeof(*Import), PAGE_READWRITE, &Protect))
    {
        RealPcToFileHeader = (PRTL_PC_TO_FILE_HEADER)*Import;
        HookAddress = Memory;
        HookBase = GetModuleHandleW(NULL);
        *Import = (PVOID)HookPcToFileHeader;

        Module = NULL;
        Result = pGetModuleHandleExW(FromAddress, (LPCWSTR)Memory, &Module);
        ok(Result, "GetModuleHandleExW failed with %lu\n", GetLastError());
        ok(Module == HookBase, "Module is %p, expected %p\n", Module, HookBase);

        *Import = (PVOID)RealPcToFileHeader;
        VirtualProtect(Import, sizeof(*Import), Protect, &Protect);
    }

    VirtualFree(Memory, 0, MEM_RELEASE);
}
