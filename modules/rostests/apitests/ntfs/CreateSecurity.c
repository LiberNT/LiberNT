/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test the security descriptor a new file receives from its directory
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <apitest.h>
#include <sddl.h>
#include <aclapi.h>
#include <strsafe.h>

#define TEST_VOLUME_LABEL L"NTFSTEST"

static WCHAR VolumeRoot[] = L"?:\\";

static
BOOL
FindTestVolume(VOID)
{
    WCHAR Label[MAX_PATH + 1];
    WCHAR FileSystem[MAX_PATH + 1];
    WCHAR System[MAX_PATH];
    WCHAR Letter;

    for (Letter = L'C'; Letter <= L'Z'; Letter++)
    {
        VolumeRoot[0] = Letter;
        if (GetVolumeInformationW(VolumeRoot, Label, ARRAYSIZE(Label), NULL, NULL, NULL,
                                  FileSystem, ARRAYSIZE(FileSystem)) &&
            !wcscmp(Label, TEST_VOLUME_LABEL) &&
            !wcscmp(FileSystem, L"NTFS"))
        {
            return TRUE;
        }
    }

    if (GetSystemDirectoryW(System, ARRAYSIZE(System)) == 0)
        return FALSE;
    VolumeRoot[0] = System[0];
    return GetVolumeInformationW(VolumeRoot, NULL, 0, NULL, NULL, NULL, FileSystem, ARRAYSIZE(FileSystem)) &&
           !wcscmp(FileSystem, L"NTFS");
}

static
VOID
CheckInherited(
    _In_ PCWSTR Path,
    _In_ PSID Users,
    _In_ PSID Owner)
{
    PSECURITY_DESCRIPTOR Descriptor = NULL;
    PSID FileOwner = NULL;
    PACL Dacl = NULL;
    ACL_SIZE_INFORMATION Size;
    BOOL Found = FALSE;
    DWORD Index, Error;

    Error = GetNamedSecurityInfoW((LPWSTR)Path, SE_FILE_OBJECT, OWNER_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION,
                                  &FileOwner, NULL, &Dacl, NULL, &Descriptor);
    ok(Error == ERROR_SUCCESS, "Reading the security of %ls failed: %lu\n", Path, Error);
    if (Error != ERROR_SUCCESS)
        return;

    ok(FileOwner != NULL && EqualSid(FileOwner, Owner), "%ls is not owned by its creator\n", Path);
    ok(Dacl != NULL, "%ls has no DACL\n", Path);
    if (Dacl && GetAclInformation(Dacl, &Size, sizeof(Size), AclSizeInformation))
    {
        for (Index = 0; Index < Size.AceCount; Index++)
        {
            ACCESS_ALLOWED_ACE *Ace;

            if (!GetAce(Dacl, Index, (PVOID *)&Ace) || Ace->Header.AceType != ACCESS_ALLOWED_ACE_TYPE)
                continue;
            if (EqualSid((PSID)&Ace->SidStart, Users))
            {
                Found = TRUE;
                ok(Ace->Header.AceFlags & INHERITED_ACE, "The Users ACE on %ls is not marked inherited: flags 0x%x\n",
                   Path, Ace->Header.AceFlags);
                ok(Ace->Mask == FILE_ALL_ACCESS, "The Users ACE on %ls grants 0x%lx\n", Path, Ace->Mask);
            }
        }
    }
    ok(Found, "%ls did not inherit the directory's Users ACE\n", Path);
    LocalFree(Descriptor);
}

START_TEST(CreateSecurity)
{
    WCHAR Directory[MAX_PATH], File[MAX_PATH], Second[MAX_PATH];
    SECURITY_ATTRIBUTES Attributes;
    PSECURITY_DESCRIPTOR Descriptor = NULL;
    BYTE TokenBuffer[sizeof(TOKEN_OWNER) + SECURITY_MAX_SID_SIZE];
    TOKEN_OWNER *OwnerInfo = (TOKEN_OWNER *)TokenBuffer;
    DWORD Length = 0;
    PSID Users = NULL;
    HANDLE Token, Handle;

    if (!FindTestVolume())
    {
        skip("No NTFS volume to test on\n");
        return;
    }

    ok(ConvertStringSidToSidW(L"S-1-5-32-545", &Users), "ConvertStringSidToSid failed: %lu\n", GetLastError());
    ok(OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &Token), "OpenProcessToken failed: %lu\n", GetLastError());
    ok(GetTokenInformation(Token, TokenOwner, OwnerInfo, sizeof(TokenBuffer), &Length), "GetTokenInformation failed: %lu\n", GetLastError());
    CloseHandle(Token);

    ok(ConvertStringSecurityDescriptorToSecurityDescriptorW(L"D:P(A;OICI;FA;;;BU)(A;OICI;FA;;;SY)(A;OICI;FA;;;BA)",
                                                           SDDL_REVISION_1, &Descriptor, NULL),
       "ConvertStringSecurityDescriptorToSecurityDescriptor failed: %lu\n", GetLastError());
    Attributes.nLength = sizeof(Attributes);
    Attributes.lpSecurityDescriptor = Descriptor;
    Attributes.bInheritHandle = FALSE;

    StringCchPrintfW(Directory, ARRAYSIZE(Directory), L"%screatesec", VolumeRoot);
    StringCchPrintfW(File, ARRAYSIZE(File), L"%s\\child.txt", Directory);
    StringCchPrintfW(Second, ARRAYSIZE(Second), L"%s\\second.txt", Directory);
    DeleteFileW(File);
    DeleteFileW(Second);
    RemoveDirectoryW(Directory);
    ok(CreateDirectoryW(Directory, &Attributes), "Creating %ls failed: %lu\n", Directory, GetLastError());

    Handle = CreateFileW(File, GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
    ok(Handle != INVALID_HANDLE_VALUE, "Creating %ls failed: %lu\n", File, GetLastError());
    if (Handle != INVALID_HANDLE_VALUE)
        CloseHandle(Handle);
    Handle = CreateFileW(Second, GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
    ok(Handle != INVALID_HANDLE_VALUE, "Creating %ls failed: %lu\n", Second, GetLastError());
    if (Handle != INVALID_HANDLE_VALUE)
        CloseHandle(Handle);

    CheckInherited(File, Users, OwnerInfo->Owner);
    CheckInherited(Second, Users, OwnerInfo->Owner);

    ok(DeleteFileW(File), "Deleting %ls failed: %lu\n", File, GetLastError());
    ok(DeleteFileW(Second), "Deleting %ls failed: %lu\n", Second, GetLastError());
    ok(RemoveDirectoryW(Directory), "Removing %ls failed: %lu\n", Directory, GetLastError());
    LocalFree(Descriptor);
    LocalFree(Users);
}
