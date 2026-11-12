/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests for the Windows Error Reporting flags and registration APIs
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"
#include <werapi.h>

typedef HRESULT (WINAPI *FN_WerSetFlags)(DWORD);
typedef HRESULT (WINAPI *FN_WerGetFlags)(HANDLE, PDWORD);
typedef HRESULT (WINAPI *FN_WerRegisterFile)(PCWSTR, WER_REGISTER_FILE_TYPE, DWORD);
typedef HRESULT (WINAPI *FN_WerUnregisterFile)(PCWSTR);
typedef HRESULT (WINAPI *FN_WerRegisterMemoryBlock)(PVOID, DWORD);
typedef HRESULT (WINAPI *FN_WerUnregisterMemoryBlock)(PVOID);
typedef HRESULT (WINAPI *FN_WerRegisterRuntimeExceptionModule)(PCWSTR, PVOID);
typedef HRESULT (WINAPI *FN_WerUnregisterRuntimeExceptionModule)(PCWSTR, PVOID);

static FN_WerSetFlags pWerSetFlags;
static FN_WerGetFlags pWerGetFlags;
static FN_WerRegisterFile pWerRegisterFile;
static FN_WerUnregisterFile pWerUnregisterFile;
static FN_WerRegisterMemoryBlock pWerRegisterMemoryBlock;
static FN_WerUnregisterMemoryBlock pWerUnregisterMemoryBlock;
static FN_WerRegisterRuntimeExceptionModule pWerRegisterRuntimeExceptionModule;
static FN_WerUnregisterRuntimeExceptionModule pWerUnregisterRuntimeExceptionModule;

static BYTE Blocks[WER_MAX_MEM_BLOCK_SIZE + 1];

static
VOID
TestFlags(VOID)
{
    DWORD Flags = 0xDEADBEEF;
    HRESULT hr;

    hr = pWerGetFlags(GetCurrentProcess(), &Flags);
    ok_hex(hr, HRESULT_FROM_WIN32(ERROR_NOT_FOUND));
    ok_long(Flags, 0xDEADBEEF);

    hr = pWerSetFlags(WER_FAULT_REPORTING_FLAG_NOHEAP | WER_FAULT_REPORTING_NO_UI);
    ok_hex(hr, S_OK);
    hr = pWerGetFlags(GetCurrentProcess(), &Flags);
    ok_hex(hr, S_OK);
    ok_long(Flags, WER_FAULT_REPORTING_FLAG_NOHEAP | WER_FAULT_REPORTING_NO_UI);

    hr = pWerSetFlags(0x80000000);
    ok_hex(hr, S_OK);
    hr = pWerGetFlags(GetCurrentProcess(), &Flags);
    ok_hex(hr, S_OK);
    ok_long(Flags, 0x80000000);

    hr = pWerSetFlags(0);
    ok_hex(hr, S_OK);
    Flags = 0xDEADBEEF;
    hr = pWerGetFlags(GetCurrentProcess(), &Flags);
    ok_hex(hr, S_OK);
    ok_long(Flags, 0);

    Flags = 0xDEADBEEF;
    hr = pWerGetFlags(NULL, &Flags);
    ok_hex(hr, E_INVALIDARG);
    ok_long(Flags, 0xDEADBEEF);
    hr = pWerGetFlags((HANDLE)(ULONG_PTR)0x1234, &Flags);
    ok_hex(hr, HRESULT_FROM_WIN32(ERROR_INVALID_HANDLE));
    ok_long(Flags, 0xDEADBEEF);
    hr = pWerGetFlags(GetCurrentProcess(), NULL);
    ok_hex(hr, E_INVALIDARG);
}

static
VOID
TestFiles(VOID)
{
    WCHAR Path[MAX_PATH + 1];
    HRESULT hr;
    ULONG Index;

    hr = pWerRegisterFile(L"C:\\ReactOS\\Temp\\WerFile.txt", WerRegFileTypeOther, 0);
    ok_hex(hr, S_OK);
    hr = pWerUnregisterFile(L"c:\\reactos\\temp\\werfile.txt");
    ok_hex(hr, S_OK);
    hr = pWerUnregisterFile(L"C:\\ReactOS\\Temp\\WerFile.txt");
    ok_hex(hr, WER_E_NOT_FOUND);

    hr = pWerRegisterFile(L"C:\\ReactOS\\Temp\\WerDocument.txt", WerRegFileTypeUserDocument,
                          WER_FILE_ANONYMOUS_DATA | WER_FILE_DELETE_WHEN_DONE);
    ok_hex(hr, S_OK);
    hr = pWerUnregisterFile(L"C:\\ReactOS\\Temp\\WerDocument.txt");
    ok_hex(hr, S_OK);

    hr = pWerUnregisterFile(L"C:\\ReactOS\\Temp\\NeverRegistered.txt");
    ok_hex(hr, WER_E_NOT_FOUND);

    hr = pWerRegisterFile(NULL, WerRegFileTypeOther, 0);
    ok(FAILED(hr), "WerRegisterFile(NULL) returned 0x%lx\n", hr);
    hr = pWerRegisterFile(L"C:\\ReactOS\\Temp\\WerFile.txt", (WER_REGISTER_FILE_TYPE)0, 0);
    ok(FAILED(hr), "WerRegisterFile(type 0) returned 0x%lx\n", hr);
    hr = pWerRegisterFile(L"C:\\ReactOS\\Temp\\WerFile.txt", WerRegFileTypeMax, 0);
    ok(FAILED(hr), "WerRegisterFile(WerRegFileTypeMax) returned 0x%lx\n", hr);
    hr = pWerUnregisterFile(NULL);
    ok(FAILED(hr), "WerUnregisterFile(NULL) returned 0x%lx\n", hr);

    Path[0] = L'C';
    Path[1] = L':';
    for (Index = 2; Index < MAX_PATH - 1; Index++)
        Path[Index] = (Index % 16) ? L'a' : L'\\';
    Path[MAX_PATH - 1] = UNICODE_NULL;
    hr = pWerRegisterFile(Path, WerRegFileTypeOther, 0);
    ok_hex(hr, S_OK);
    hr = pWerUnregisterFile(Path);
    ok_hex(hr, S_OK);

    Path[MAX_PATH - 1] = L'a';
    Path[MAX_PATH] = UNICODE_NULL;
    hr = pWerRegisterFile(Path, WerRegFileTypeOther, 0);
    ok(FAILED(hr), "WerRegisterFile(%u chars) returned 0x%lx\n", MAX_PATH, hr);
}

static
VOID
TestMemoryBlocks(VOID)
{
    HRESULT hr;

    hr = pWerRegisterMemoryBlock(Blocks, 64);
    ok_hex(hr, S_OK);
    hr = pWerUnregisterMemoryBlock(Blocks);
    ok_hex(hr, S_OK);
    hr = pWerUnregisterMemoryBlock(Blocks);
    ok_hex(hr, WER_E_NOT_FOUND);
    hr = pWerUnregisterMemoryBlock(Blocks + 1);
    ok_hex(hr, WER_E_NOT_FOUND);

    hr = pWerRegisterMemoryBlock(Blocks, WER_MAX_MEM_BLOCK_SIZE);
    ok_hex(hr, S_OK);
    hr = pWerUnregisterMemoryBlock(Blocks);
    ok_hex(hr, S_OK);

    hr = pWerRegisterMemoryBlock(Blocks, WER_MAX_MEM_BLOCK_SIZE + 1);
    ok(FAILED(hr), "WerRegisterMemoryBlock(WER_MAX_MEM_BLOCK_SIZE + 1) returned 0x%lx\n", hr);
    hr = pWerRegisterMemoryBlock(NULL, 64);
    ok(FAILED(hr), "WerRegisterMemoryBlock(NULL) returned 0x%lx\n", hr);
    hr = pWerRegisterMemoryBlock(Blocks, 0);
    ok(FAILED(hr), "WerRegisterMemoryBlock(size 0) returned 0x%lx\n", hr);
}

static
VOID
TestEntryLimit(VOID)
{
    ULONG Index, Failures = 0;
    HRESULT hr;

    for (Index = 0; Index < WER_MAX_REGISTERED_ENTRIES - 1; Index++)
    {
        if (pWerRegisterMemoryBlock(Blocks + Index, 1) != S_OK)
            Failures++;
    }
    ok_long(Failures, 0);

    hr = pWerRegisterFile(L"C:\\ReactOS\\Temp\\WerLimit.txt", WerRegFileTypeOther, 0);
    ok_hex(hr, S_OK);

    hr = pWerRegisterMemoryBlock(Blocks + WER_MAX_REGISTERED_ENTRIES, 1);
    ok_hex(hr, HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER));
    hr = pWerRegisterFile(L"C:\\ReactOS\\Temp\\WerLimit2.txt", WerRegFileTypeOther, 0);
    ok_hex(hr, HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER));

    hr = pWerUnregisterMemoryBlock(Blocks);
    ok_hex(hr, S_OK);
    hr = pWerRegisterFile(L"C:\\ReactOS\\Temp\\WerLimit2.txt", WerRegFileTypeOther, 0);
    ok_hex(hr, S_OK);

    Failures = 0;
    for (Index = 1; Index < WER_MAX_REGISTERED_ENTRIES - 1; Index++)
    {
        if (pWerUnregisterMemoryBlock(Blocks + Index) != S_OK)
            Failures++;
    }
    ok_long(Failures, 0);
    hr = pWerUnregisterFile(L"C:\\ReactOS\\Temp\\WerLimit.txt");
    ok_hex(hr, S_OK);
    hr = pWerUnregisterFile(L"C:\\ReactOS\\Temp\\WerLimit2.txt");
    ok_hex(hr, S_OK);
}

static
VOID
TestRuntimeExceptionModules(VOID)
{
    WCHAR Name[32];
    ULONG Index, Failures = 0;
    HRESULT hr;

    hr = pWerRegisterRuntimeExceptionModule(L"WerHelper.dll", Blocks);
    ok_hex(hr, S_OK);
    hr = pWerUnregisterRuntimeExceptionModule(L"WerHelper.dll", Blocks + 1);
    ok_hex(hr, WER_E_NOT_FOUND);
    hr = pWerUnregisterRuntimeExceptionModule(L"WERHELPER.DLL", Blocks);
    ok_hex(hr, S_OK);
    hr = pWerUnregisterRuntimeExceptionModule(L"WerHelper.dll", Blocks);
    ok_hex(hr, WER_E_NOT_FOUND);

    hr = pWerRegisterRuntimeExceptionModule(NULL, NULL);
    ok(FAILED(hr), "WerRegisterRuntimeExceptionModule(NULL) returned 0x%lx\n", hr);

    for (Index = 0; Index < WER_MAX_REGISTERED_RUNTIME_EXCEPTION_MODULES; Index++)
    {
        StringCchPrintfW(Name, ARRAYSIZE(Name), L"WerHelper%lu.dll", Index);
        if (pWerRegisterRuntimeExceptionModule(Name, NULL) != S_OK)
            Failures++;
    }
    ok_long(Failures, 0);

    hr = pWerRegisterRuntimeExceptionModule(L"WerHelperExtra.dll", NULL);
    ok_hex(hr, HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER));

    Failures = 0;
    for (Index = 0; Index < WER_MAX_REGISTERED_RUNTIME_EXCEPTION_MODULES; Index++)
    {
        StringCchPrintfW(Name, ARRAYSIZE(Name), L"WerHelper%lu.dll", Index);
        if (pWerUnregisterRuntimeExceptionModule(Name, NULL) != S_OK)
            Failures++;
    }
    ok_long(Failures, 0);
}

START_TEST(WerRegistration)
{
    HMODULE Kernel32 = GetModuleHandleW(L"kernel32.dll");

    pWerSetFlags = (FN_WerSetFlags)GetProcAddress(Kernel32, "WerSetFlags");
    pWerGetFlags = (FN_WerGetFlags)GetProcAddress(Kernel32, "WerGetFlags");
    pWerRegisterFile = (FN_WerRegisterFile)GetProcAddress(Kernel32, "WerRegisterFile");
    pWerUnregisterFile = (FN_WerUnregisterFile)GetProcAddress(Kernel32, "WerUnregisterFile");
    pWerRegisterMemoryBlock = (FN_WerRegisterMemoryBlock)GetProcAddress(Kernel32, "WerRegisterMemoryBlock");
    pWerUnregisterMemoryBlock = (FN_WerUnregisterMemoryBlock)GetProcAddress(Kernel32, "WerUnregisterMemoryBlock");
    pWerRegisterRuntimeExceptionModule = (FN_WerRegisterRuntimeExceptionModule)GetProcAddress(Kernel32, "WerRegisterRuntimeExceptionModule");
    pWerUnregisterRuntimeExceptionModule = (FN_WerUnregisterRuntimeExceptionModule)GetProcAddress(Kernel32, "WerUnregisterRuntimeExceptionModule");
    if (!pWerSetFlags || !pWerGetFlags || !pWerRegisterFile || !pWerUnregisterFile ||
        !pWerRegisterMemoryBlock || !pWerUnregisterMemoryBlock ||
        !pWerRegisterRuntimeExceptionModule || !pWerUnregisterRuntimeExceptionModule)
    {
        skip("WER registration exports are missing\n");
        return;
    }

    TestFlags();
    TestFiles();
    TestMemoryBlocks();
    TestEntryLimit();
    TestRuntimeExceptionModules();
}
