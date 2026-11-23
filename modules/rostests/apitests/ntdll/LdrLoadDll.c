/*
 * PROJECT:     ReactOS API Tests
 * LICENSE:     LGPL-2.0-or-later (https://spdx.org/licenses/LGPL-2.0-or-later)
 * PURPOSE:     Test for LdrLoadDll
 * COPYRIGHT:   Copyright 2017 Thomas Faber <thomas.faber@reactos.org>
 *              Copyright 2024 Katayama Hirofumi MZ <katayama.hirofumi.mz@gmail.com>
 */

#include "precomp.h"

/*

NTSTATUS
NTAPI
LdrLoadDll(
    IN PWSTR SearchPath OPTIONAL,
    IN PULONG DllCharacteristics OPTIONAL,
    IN PUNICODE_STRING DllName,
    OUT PVOID *BaseAddress
);

*/

START_TEST(LdrLoadDll)
{
    NTSTATUS Status = STATUS_SUCCESS;
    UNICODE_STRING DllName;
    PVOID BaseAddress, BaseAddress2;
    WCHAR szWinDir[MAX_PATH], szSysDir[MAX_PATH], szPath[MAX_PATH];
    WCHAR *pch;

    RtlInitEmptyUnicodeString(&DllName, NULL, 0);
    GetWindowsDirectoryW(szWinDir, _countof(szWinDir));
    GetSystemDirectoryW(szSysDir, _countof(szSysDir));

    Status = 0xDEADFACE;
    StartSeh()
        Status = LdrLoadDll(NULL, NULL, &DllName, NULL);
    EndSeh(STATUS_ACCESS_VIOLATION);
    ok_ntstatus(Status, 0xDEADFACE);

    Status = 0xDEADFACE;
    BaseAddress = InvalidPointer;
    StartSeh()
        Status = LdrLoadDll(NULL, NULL, &DllName, &BaseAddress);
    EndSeh(STATUS_ACCESS_VIOLATION);
    ok_ntstatus(Status, 0xDEADFACE);
    ok_ptr(BaseAddress, InvalidPointer);

    Status = 0xDEADFACE;
    StartSeh()
        Status = LdrLoadDll(L"", NULL, &DllName, NULL);
    EndSeh(STATUS_ACCESS_VIOLATION);
    ok_ntstatus(Status, 0xDEADFACE);

    Status = 0xDEADFACE;
    BaseAddress = InvalidPointer;
    StartSeh()
        Status = LdrLoadDll(L"", NULL, &DllName, &BaseAddress);
    EndSeh(STATUS_ACCESS_VIOLATION);
    ok_ntstatus(Status, 0xDEADFACE);
    ok_ptr(BaseAddress, InvalidPointer);

    RtlInitUnicodeString(&DllName, L"advapi32.dll");

    Status = 0xDEADFACE;
    BaseAddress = InvalidPointer;
    StartSeh()
        Status = LdrLoadDll(NULL, NULL, &DllName, NULL);
        if (NT_SUCCESS(Status))
            LdrUnloadDll(BaseAddress);
    EndSeh(STATUS_ACCESS_VIOLATION);
    ok_ntstatus(Status, 0xDEADFACE);
    ok_ptr(BaseAddress, InvalidPointer);

    RtlInitUnicodeString(&DllName, L"advapi32.dll");
    Status = 0xDEADFACE;
    BaseAddress = InvalidPointer;
    StartSeh()
        BaseAddress = InvalidPointer;
        Status = LdrLoadDll(NULL, NULL, &DllName, &BaseAddress);
        ok_ntstatus(Status, STATUS_SUCCESS);
        ok(BaseAddress != NULL && BaseAddress != InvalidPointer, "BaseAddress = %p\n", BaseAddress);
        if (NT_SUCCESS(Status))
        {
            BaseAddress2 = InvalidPointer;
            Status = LdrLoadDll(NULL, NULL, &DllName, &BaseAddress2);
            ok_ntstatus(Status, STATUS_SUCCESS);
            ok_ptr(BaseAddress2, BaseAddress);
            LdrUnloadDll(BaseAddress);
        }
    EndSeh(STATUS_SUCCESS);
    ok_ntstatus(Status, STATUS_SUCCESS);
    ok(BaseAddress != NULL, "BaseAddress was NULL\n");

    RtlInitUnicodeString(&DllName, L"advapi32.dll");
    Status = 0xDEADFACE;
    BaseAddress = InvalidPointer;
    StartSeh()
        Status = LdrLoadDll(L"\\SystemRoot\\System32", NULL, &DllName, NULL);
    EndSeh(STATUS_ACCESS_VIOLATION);
    ok_ntstatus(Status, 0xDEADFACE);
    ok_ptr(BaseAddress, InvalidPointer);

    /* Test with only backslashes in path; no file extension */
    StringCchPrintfW(szPath, _countof(szPath), L"%s\\advapi32", szSysDir);
    RtlInitUnicodeString(&DllName, szPath);
    Status = 0xDEADFACE;
    BaseAddress = InvalidPointer;
    StartSeh()
        Status = LdrLoadDll(szWinDir, NULL, &DllName, &BaseAddress);
        ok_ntstatus(Status, STATUS_SUCCESS);
        if (NT_SUCCESS(Status))
            LdrUnloadDll(BaseAddress);
    EndSeh(STATUS_SUCCESS);
    ok_ntstatus(Status, STATUS_SUCCESS);
    ok(BaseAddress != NULL, "BaseAddress was NULL\n");

    /* Test with only backslashes in path; with file extension */
    StringCchPrintfW(szPath, _countof(szPath), L"%s\\advapi32.dll", szSysDir);
    RtlInitUnicodeString(&DllName, szPath);
    Status = 0xDEADFACE;
    BaseAddress = InvalidPointer;
    StartSeh()
        Status = LdrLoadDll(szWinDir, NULL, &DllName, &BaseAddress);
        ok_ntstatus(Status, STATUS_SUCCESS);
        if (NT_SUCCESS(Status))
            LdrUnloadDll(BaseAddress);
    EndSeh(STATUS_SUCCESS);
    ok_ntstatus(Status, STATUS_SUCCESS);
    ok(BaseAddress != NULL, "BaseAddress was NULL\n");

    /* Test with one forward slash in path; no file extension */
    StringCchPrintfW(szPath, _countof(szPath), L"%s/advapi32", szSysDir);
    RtlInitUnicodeString(&DllName, szPath);
    Status = 0xDEADFACE;
    BaseAddress = InvalidPointer;
    StartSeh()
        Status = LdrLoadDll(szWinDir, NULL, &DllName, &BaseAddress);
        ok_ntstatus(Status, STATUS_SUCCESS);
        if (NT_SUCCESS(Status))
            LdrUnloadDll(BaseAddress);
    EndSeh(STATUS_SUCCESS);
    ok_ntstatus(Status, STATUS_SUCCESS);
    ok(BaseAddress != NULL, "BaseAddress was NULL\n");

    /* Test with one forward slash in path; with file extension */
    StringCchPrintfW(szPath, _countof(szPath), L"%s/advapi32.dll", szSysDir);
    RtlInitUnicodeString(&DllName, szPath);
    Status = 0xDEADFACE;
    BaseAddress = InvalidPointer;
    StartSeh()
        Status = LdrLoadDll(szWinDir, NULL, &DllName, &BaseAddress);
        ok_ntstatus(Status, STATUS_SUCCESS);
        if (NT_SUCCESS(Status))
            LdrUnloadDll(BaseAddress);
    EndSeh(STATUS_SUCCESS);
    ok_ntstatus(Status, STATUS_SUCCESS);
    ok(BaseAddress != NULL, "BaseAddress was NULL\n");

    /* Test with only forward slashes in path; no file extension */
    StringCchPrintfW(szPath, _countof(szPath), L"%s\\advapi32", szSysDir);
    for (pch = szPath; *pch != UNICODE_NULL; ++pch)
    {
        if (*pch == L'\\')
            *pch = L'/';
    }

    RtlInitUnicodeString(&DllName, szPath);
    Status = 0xDEADFACE;
    BaseAddress = InvalidPointer;
    StartSeh()
        Status = LdrLoadDll(szWinDir, NULL, &DllName, &BaseAddress);
        ok_ntstatus(Status, STATUS_SUCCESS);
        if (NT_SUCCESS(Status))
            LdrUnloadDll(BaseAddress);
    EndSeh(STATUS_SUCCESS);
    ok_ntstatus(Status, STATUS_SUCCESS);
    ok(BaseAddress != NULL, "BaseAddress was NULL\n");

    /* Test with only forward slashes in path; with file extension */
    /* Test with only forward slashes in path; with file extension */
    StringCchPrintfW(szPath, _countof(szPath), L"%s\\advapi32.dll", szSysDir);
    for (pch = szPath; *pch != UNICODE_NULL; ++pch)
    {
        if (*pch == L'\\')
            *pch = L'/';
    }

    RtlInitUnicodeString(&DllName, szPath);
    Status = 0xDEADFACE;
    BaseAddress = InvalidPointer;
    StartSeh()
        Status = LdrLoadDll(szWinDir, NULL, &DllName, &BaseAddress);
        ok_ntstatus(Status, STATUS_SUCCESS);
        if (NT_SUCCESS(Status))
            LdrUnloadDll(BaseAddress);
    EndSeh(STATUS_SUCCESS);
    ok_ntstatus(Status, STATUS_SUCCESS);
    ok(BaseAddress != NULL, "BaseAddress was NULL\n");
}

/* Resource 102 is the test DLL with an empty entry point. */
BOOL extract_resource(const WCHAR *Filename, LPCWSTR ResourceName);

static void CheckModuleEntry(HMODULE Module, BOOL ProcessImport, LDR_DLL_LOAD_REASON Reason)
{
    PLDR_DATA_TABLE_ENTRY Entry;
    ULONG Hash;
    NTSTATUS Status = LdrFindEntryForAddress(Module, &Entry);
    ok_ntstatus(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
        return;

    ok(!Entry->MarkedForRemoval, "Live module %p marked for removal, flags %#lx\n", Module, Entry->Flags);
    ok_ntstatus(RtlHashUnicodeString(&Entry->BaseDllName, TRUE, HASH_STRING_ALGORITHM_X65599, &Hash), STATUS_SUCCESS);
    ok_hex(Entry->BaseNameHashValue, Hash);
    ok(Entry->DdagNode != NULL, "Missing dependency node\n");
    if (Entry->DdagNode)
        ok_int(Entry->DdagNode->State, LdrModulesReadyToRun);
    ok_int(Entry->ProcessStaticImport, ProcessImport);
    ok_int(Entry->LoadReason, Reason);
    ok_ptr((PVOID)Entry->OriginalBase, (PVOID)(ULONG_PTR)RtlImageNtHeader(Module)->OptionalHeader.ImageBase);
}

typedef struct _ENTRY_NOTIFICATIONS
{
    UNICODE_STRING Name;
    ULONG Loads, Unloads;
    ULONG LoadFlags, UnloadFlags;
} ENTRY_NOTIFICATIONS;

static VOID CALLBACK CaptureEntryFlags(ULONG Reason, PCLDR_DLL_NOTIFICATION_DATA Data, PVOID Context)
{
    ENTRY_NOTIFICATIONS *Notifications = Context;
    PLDR_DATA_TABLE_ENTRY Entry = NULL;
    ULONG Flags = MAXULONG;

    if (!RtlEqualUnicodeString(&Notifications->Name, Data->Loaded.BaseDllName, TRUE))
        return;
    if (NT_SUCCESS(LdrFindEntryForAddress(Data->Loaded.DllBase, &Entry)))
        Flags = Entry->Flags;
    if (Reason == LDR_DLL_NOTIFICATION_REASON_LOADED)
    {
        ++Notifications->Loads;
        Notifications->LoadFlags = Flags;
    }
    else
    {
        ++Notifications->Unloads;
        Notifications->UnloadFlags = Flags;
    }
}

START_TEST(LdrModuleEntry)
{
    WCHAR Directory[MAX_PATH], Filename[MAX_PATH];
    HMODULE Module;
    DWORD Length;
    ENTRY_NOTIFICATIONS Notifications = {0};
    PVOID Cookie = NULL;
    const ULONG Membership = LDRP_IN_LEGACY_LISTS | LDRP_IN_INDEXES |
                             LDRP_IN_EXCEPTION_TABLE | LDRP_LOAD_CONFIG_PROCESSED;
    const ULONG Mask = Membership | LDRP_IMAGE_DLL | LDRP_ENTRY_PROCESSED |
                       LDRP_LOAD_NOTIFICATIONS_SENT | LDRP_PROCESS_ATTACH_CALLED | LDRP_PROCESS_ATTACH_FAILED;
    const ULONG Ready = Membership | LDRP_IMAGE_DLL | LDRP_LOAD_NOTIFICATIONS_SENT | LDRP_PROCESS_ATTACH_CALLED;

    if (NtCurrentPeb()->OSMajorVersion < 10)
    {
        skip("Requires the modern loader entry layout\n");
        return;
    }

    /* advapi32 is in this executable's import directory. */
    Module = GetModuleHandleW(L"advapi32.dll");
    ok(Module != NULL, "Missing startup import\n");
    if (Module)
        CheckModuleEntry(Module, TRUE, LoadReasonStaticDependency);

    Length = GetTempPathW(_countof(Directory), Directory);
    ok(Length && Length < _countof(Directory), "GetTempPathW failed, error %lu\n", GetLastError());
    if (!Length || Length >= _countof(Directory))
        return;
    if (!GetTempFileNameW(Directory, L"ldr", 0, Filename))
    {
        ok(0, "GetTempFileNameW failed, error %lu\n", GetLastError());
        return;
    }
    if (extract_resource(Filename, (LPCWSTR)102))
    {
        RtlInitUnicodeString(&Notifications.Name, wcsrchr(Filename, L'\\') + 1);
        ok_ntstatus(LdrRegisterDllNotification(0, CaptureEntryFlags, &Notifications, &Cookie), STATUS_SUCCESS);
        Module = LoadLibraryW(Filename);
        ok(Module != NULL, "LoadLibraryW failed, error %lu\n", GetLastError());
        if (Module)
        {
            PLDR_DATA_TABLE_ENTRY Entry;
            HMODULE Second;
            CheckModuleEntry(Module, FALSE, LoadReasonDynamicLoad);
            if (NT_SUCCESS(LdrFindEntryForAddress(Module, &Entry)) && Entry->DdagNode)
            {
                ok_hex(Entry->Flags & Mask, Ready);
                ok_int(Entry->ObsoleteLoadCount, 6);
                ok_int(Entry->ReferenceCount, 1);
                ok_ptr(Entry->NodeModuleLink.Flink, &Entry->DdagNode->Modules);
                ok_ptr(Entry->DdagNode->Modules.Flink, &Entry->NodeModuleLink);
                ok_int(Entry->DdagNode->LoadCount, 1);
                Second = LoadLibraryW(Filename);
                ok_ptr(Second, Module);
                if (Second)
                {
                    ok_int(Entry->DdagNode->LoadCount, 2);
                    ok_int(Entry->ReferenceCount, 1);
                    ok_int(Entry->ObsoleteLoadCount, 6);
                    FreeLibrary(Second);
                    ok_int(Entry->DdagNode->LoadCount, 1);
                }
            }
            FreeLibrary(Module);
        }
        ok_int(Notifications.Loads, 1);
        ok_int(Notifications.Unloads, 1);
        ok_hex(Notifications.LoadFlags & Mask, Membership | LDRP_IMAGE_DLL);
        ok_hex(Notifications.UnloadFlags & Mask, Ready);

        Module = LoadLibraryExW(Filename, NULL, DONT_RESOLVE_DLL_REFERENCES);
        ok(Module != NULL, "LoadLibraryExW failed, error %lu\n", GetLastError());
        if (Module)
        {
            PLDR_DATA_TABLE_ENTRY Entry;
            NTSTATUS Status = LdrFindEntryForAddress(Module, &Entry);
            ok_ntstatus(Status, STATUS_SUCCESS);
            if (NT_SUCCESS(Status))
            {
                ok_hex(Entry->Flags & Mask, Membership);
                ok(Entry->DdagNode && Entry->DdagNode->State == LdrModulesReadyToRun,
                   "Expected a ready node for an unresolved image\n");
            }
            FreeLibrary(Module);
        }
        ok_int(Notifications.Loads, 1);
        ok_int(Notifications.Unloads, 1);
        if (Cookie) LdrUnregisterDllNotification(Cookie);
    }
    DeleteFileW(Filename);
}
