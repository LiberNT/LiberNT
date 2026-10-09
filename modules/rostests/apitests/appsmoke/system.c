/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests the program folders of a 64-bit system and registry security by object type
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "appsmoke.h"

#include <winreg.h>
#include <aclapi.h>
#include <shlobj.h>

START_TEST(program_folders)
{
    static const struct
    {
        int Id;
        const char *Name;
    } Folders[] =
    {
        { CSIDL_PROGRAM_FILES, "CSIDL_PROGRAM_FILES" },
        { CSIDL_PROGRAM_FILESX86, "CSIDL_PROGRAM_FILESX86" },
        { CSIDL_PROGRAM_FILES_COMMON, "CSIDL_PROGRAM_FILES_COMMON" },
        { CSIDL_PROGRAM_FILES_COMMONX86, "CSIDL_PROGRAM_FILES_COMMONX86" },
    };
    WCHAR Native[MAX_PATH] = L"", Emulated[MAX_PATH] = L"", Current[MAX_PATH] = L"", Folder[MAX_PATH];
    BOOL Wow64 = FALSE;
    DWORD Attributes;
    HRESULT Hr;
    UINT Index;

    IsWow64Process(GetCurrentProcess(), &Wow64);
    if (sizeof(void *) == 4 && !Wow64)
    {
        skip("The system is not 64-bit\n");
        return;
    }

    ok(GetEnvironmentVariableW(L"ProgramW6432", Native, ARRAYSIZE(Native)) != 0, "ProgramW6432 is not set\n");
    ok(GetEnvironmentVariableW(L"ProgramFiles(x86)", Emulated, ARRAYSIZE(Emulated)) != 0,
       "ProgramFiles(x86) is not set\n");
    ok(GetEnvironmentVariableW(L"CommonProgramW6432", Folder, ARRAYSIZE(Folder)) != 0,
       "CommonProgramW6432 is not set\n");
    ok(GetEnvironmentVariableW(L"CommonProgramFiles(x86)", Folder, ARRAYSIZE(Folder)) != 0,
       "CommonProgramFiles(x86) is not set\n");
    ok(GetEnvironmentVariableW(L"ProgramFiles", Current, ARRAYSIZE(Current)) != 0, "ProgramFiles is not set\n");
    ok(!lstrcmpiW(Current, sizeof(void *) == 8 ? Native : Emulated), "ProgramFiles is %ls in a %u-bit process\n",
       Current, (UINT)sizeof(void *) * 8);

    for (Index = 0; Index < ARRAYSIZE(Folders); Index++)
    {
        Folder[0] = 0;
        Hr = SHGetFolderPathW(NULL, Folders[Index].Id, NULL, SHGFP_TYPE_CURRENT, Folder);
        ok(Hr == S_OK, "SHGetFolderPathW(%s) returned %#lx for %ls\n", Folders[Index].Name, Hr, Folder);
        Attributes = GetFileAttributesW(Folder);
        ok(Attributes != INVALID_FILE_ATTRIBUTES && (Attributes & FILE_ATTRIBUTE_DIRECTORY),
           "%s is %ls, which is not a directory\n", Folders[Index].Name, Folder);
    }
}

START_TEST(registry_security)
{
    static const struct
    {
        SE_OBJECT_TYPE Type;
        const char *Name;
    } Types[] =
    {
        { SE_REGISTRY_KEY, "SE_REGISTRY_KEY" },
        { SE_REGISTRY_WOW64_32KEY, "SE_REGISTRY_WOW64_32KEY" },
        { SE_REGISTRY_WOW64_64KEY, "SE_REGISTRY_WOW64_64KEY" },
    };
    WCHAR Name[] = L"MACHINE\\SOFTWARE";
    PSECURITY_DESCRIPTOR Descriptor;
    PSID Owner;
    HKEY Key;
    DWORD Error;
    UINT Index;

    for (Index = 0; Index < ARRAYSIZE(Types); Index++)
    {
        Descriptor = NULL;
        Owner = NULL;
        Error = GetNamedSecurityInfoW(Name, Types[Index].Type, OWNER_SECURITY_INFORMATION, &Owner, NULL, NULL, NULL,
                                      &Descriptor);
        ok(Error == ERROR_SUCCESS, "GetNamedSecurityInfoW(%s) returned %lu\n", Types[Index].Name, Error);
        ok(Error != ERROR_SUCCESS || (Owner && IsValidSid(Owner)), "%s gave no owner\n", Types[Index].Name);
        if (Descriptor)
            LocalFree(Descriptor);

        Error = RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SOFTWARE", 0, READ_CONTROL, &Key);
        ok(Error == ERROR_SUCCESS, "RegOpenKeyExW returned %lu\n", Error);
        if (Error != ERROR_SUCCESS)
            continue;
        Descriptor = NULL;
        Owner = NULL;
        Error = GetSecurityInfo(Key, Types[Index].Type, OWNER_SECURITY_INFORMATION, &Owner, NULL, NULL, NULL,
                                &Descriptor);
        ok(Error == ERROR_SUCCESS, "GetSecurityInfo(%s) returned %lu\n", Types[Index].Name, Error);
        if (Descriptor)
            LocalFree(Descriptor);
        RegCloseKey(Key);
    }
}
