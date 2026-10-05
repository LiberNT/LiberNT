/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     NtCreateKey below a cached key that is waiting for delayed close
 * COPYRIGHT:   Copyright 2026 Ahmed Arif <arif193@gmail.com>
 */

#include "precomp.h"

#define CACHED_ROOT L"Software\\LiberNTCmCacheTest"
#define CACHED_ITERATIONS 64

static NTSTATUS
CachedCreate(
    _In_ HANDLE Root,
    _In_ PCWSTR Path,
    _Out_ PHANDLE KeyHandle)
{
    UNICODE_STRING Name;
    OBJECT_ATTRIBUTES Attributes;

    RtlInitUnicodeString(&Name, Path);
    InitializeObjectAttributes(&Attributes, &Name, OBJ_CASE_INSENSITIVE, Root, NULL);
    return NtCreateKey(KeyHandle, KEY_ALL_ACCESS, &Attributes, 0, NULL, REG_OPTION_VOLATILE, NULL);
}

static NTSTATUS
CachedOpen(
    _In_ HANDLE Root,
    _In_ PCWSTR Path,
    _Out_ PHANDLE KeyHandle)
{
    UNICODE_STRING Name;
    OBJECT_ATTRIBUTES Attributes;

    RtlInitUnicodeString(&Name, Path);
    InitializeObjectAttributes(&Attributes, &Name, OBJ_CASE_INSENSITIVE, Root, NULL);
    return NtOpenKey(KeyHandle, KEY_ALL_ACCESS, &Attributes);
}

static NTSTATUS
CachedCreateChain(
    _In_ HANDLE Root,
    _In_ PCWSTR Path)
{
    WCHAR Partial[260];
    PCWSTR Separator = Path;
    HANDLE KeyHandle;
    NTSTATUS Status = STATUS_SUCCESS;

    for (;;)
    {
        Separator = wcschr(Separator + 1, L'\\');
        if (Separator == NULL)
            break;
        if ((SIZE_T)(Separator - Path) >= RTL_NUMBER_OF(Partial))
            return STATUS_NAME_TOO_LONG;
        RtlCopyMemory(Partial, Path, (Separator - Path) * sizeof(WCHAR));
        Partial[Separator - Path] = UNICODE_NULL;
        Status = CachedCreate(Root, Partial, &KeyHandle);
        if (!NT_SUCCESS(Status))
            return Status;
        NtClose(KeyHandle);
    }
    Status = CachedCreate(Root, Path, &KeyHandle);
    if (NT_SUCCESS(Status))
        NtClose(KeyHandle);
    return Status;
}

static BOOLEAN
CachedNameEndsWith(
    _In_ HANDLE KeyHandle,
    _In_ PCWSTR Suffix)
{
    UCHAR Buffer[1024];
    PKEY_NAME_INFORMATION Info = (PKEY_NAME_INFORMATION)Buffer;
    ULONG Length = 0;
    SIZE_T SuffixLength = wcslen(Suffix);
    SIZE_T NameLength;
    NTSTATUS Status;

    Status = NtQueryKey(KeyHandle, KeyNameInformation, Info, sizeof(Buffer) - sizeof(WCHAR), &Length);
    ok(Status == STATUS_SUCCESS, "NtQueryKey(KeyNameInformation) returned 0x%lx\n", Status);
    if (!NT_SUCCESS(Status))
        return FALSE;
    NameLength = Info->NameLength / sizeof(WCHAR);
    Info->Name[NameLength] = UNICODE_NULL;
    if (NameLength < SuffixLength || _wcsicmp(&Info->Name[NameLength - SuffixLength], Suffix) != 0)
    {
        trace("Key name is %ls\n", Info->Name);
        return FALSE;
    }
    return TRUE;
}

static VOID
CachedDelete(
    _In_ HANDLE Root,
    _In_ PCWSTR Path)
{
    HANDLE KeyHandle;

    if (NT_SUCCESS(CachedOpen(Root, Path, &KeyHandle)))
    {
        NtDeleteKey(KeyHandle);
        NtClose(KeyHandle);
    }
}

START_TEST(NtCreateKeyCached)
{
    HANDLE UserRoot, KeyHandle;
    NTSTATUS Status;
    ULONG Iteration;

    Status = RtlOpenCurrentUser(KEY_ALL_ACCESS, &UserRoot);
    ok(Status == STATUS_SUCCESS, "RtlOpenCurrentUser returned 0x%lx\n", Status);
    if (!NT_SUCCESS(Status))
        return;

    for (Iteration = 0; Iteration < CACHED_ITERATIONS; ++Iteration)
    {
        Status = CachedCreateChain(UserRoot, CACHED_ROOT L"\\A\\B");
        ok(Status == STATUS_SUCCESS, "Create A\\B returned 0x%lx\n", Status);
        if (!NT_SUCCESS(Status))
            break;

        Status = CachedCreate(UserRoot, CACHED_ROOT L"\\A\\B\\C", &KeyHandle);
        ok(Status == STATUS_SUCCESS, "Create A\\B\\C returned 0x%lx\n", Status);
        if (!NT_SUCCESS(Status))
            break;
        NtClose(KeyHandle);

        Status = CachedCreate(UserRoot, CACHED_ROOT L"\\A\\B\\C\\D", &KeyHandle);
        ok(Status == STATUS_SUCCESS, "Create A\\B\\C\\D returned 0x%lx\n", Status);
        if (!NT_SUCCESS(Status))
            break;
        ok(CachedNameEndsWith(KeyHandle, L"\\LiberNTCmCacheTest\\A\\B\\C\\D"),
           "Iteration %lu created the key at the wrong path\n", Iteration);
        NtClose(KeyHandle);

        Status = CachedOpen(UserRoot, CACHED_ROOT L"\\A\\B\\C", &KeyHandle);
        ok(Status == STATUS_SUCCESS, "Open A\\B\\C returned 0x%lx\n", Status);
        if (NT_SUCCESS(Status))
            NtClose(KeyHandle);

        Status = CachedOpen(UserRoot, CACHED_ROOT L"\\A\\B\\C\\D", &KeyHandle);
        ok(Status == STATUS_SUCCESS, "Open A\\B\\C\\D returned 0x%lx\n", Status);
        if (NT_SUCCESS(Status))
        {
            ok(CachedNameEndsWith(KeyHandle, L"\\LiberNTCmCacheTest\\A\\B\\C\\D"),
               "Iteration %lu opened the wrong key\n", Iteration);
            NtClose(KeyHandle);
        }

        CachedDelete(UserRoot, CACHED_ROOT L"\\A\\B\\C\\D");
        CachedDelete(UserRoot, CACHED_ROOT L"\\A\\B\\C");
    }

    CachedDelete(UserRoot, CACHED_ROOT L"\\A\\B\\C\\D");
    CachedDelete(UserRoot, CACHED_ROOT L"\\A\\B\\C");
    CachedDelete(UserRoot, CACHED_ROOT L"\\A\\B");
    CachedDelete(UserRoot, CACHED_ROOT L"\\A");
    CachedDelete(UserRoot, CACHED_ROOT);
    NtClose(UserRoot);
}
