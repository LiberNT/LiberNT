/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     A file's security descriptor is read back as it was last written
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"

#include <aclapi.h>
#include <strsafe.h>

static BOOL
ReadDacl(PCWSTR Path, PACL *Dacl, PSECURITY_DESCRIPTOR *Descriptor)
{
    DWORD Error;

    *Dacl = NULL;
    *Descriptor = NULL;
    Error = GetNamedSecurityInfoW((LPWSTR)Path, SE_FILE_OBJECT, DACL_SECURITY_INFORMATION,
                                  NULL, NULL, Dacl, NULL, Descriptor);
    return Error == ERROR_SUCCESS;
}

/*
 * The rights the list grants everyone, and the number of entries it holds.  An
 * entry count on its own says nothing: SetEntriesInAcl folds entries that name
 * the same trustee, so a list can gain a trustee without growing.
 */
static BOOL
ReadEveryoneAccess(PCWSTR Path, PULONG Mask, PULONG AceCount)
{
    PSECURITY_DESCRIPTOR Descriptor = NULL;
    ACL_SIZE_INFORMATION Size = { 0 };
    SID_IDENTIFIER_AUTHORITY World = { SECURITY_WORLD_SID_AUTHORITY };
    PSID Everyone = NULL;
    PACL Dacl = NULL;
    ULONG Index;

    *Mask = 0;
    *AceCount = 0;
    if (!AllocateAndInitializeSid(&World, 1, SECURITY_WORLD_RID, 0, 0, 0, 0, 0, 0, 0, &Everyone))
        return FALSE;
    if (!ReadDacl(Path, &Dacl, &Descriptor))
    {
        FreeSid(Everyone);
        return FALSE;
    }
    if (Dacl == NULL || !GetAclInformation(Dacl, &Size, sizeof(Size), AclSizeInformation))
    {
        LocalFree(Descriptor);
        FreeSid(Everyone);
        return FALSE;
    }

    *AceCount = Size.AceCount;
    for (Index = 0; Index < Size.AceCount; Index++)
    {
        ACCESS_ALLOWED_ACE *Ace = NULL;

        if (!GetAce(Dacl, Index, (LPVOID *)&Ace))
            continue;
        if (Ace->Header.AceType == ACCESS_ALLOWED_ACE_TYPE &&
            EqualSid((PSID)&Ace->SidStart, Everyone))
        {
            *Mask |= Ace->Mask;
        }
    }

    LocalFree(Descriptor);
    FreeSid(Everyone);
    return TRUE;
}

static BOOL
GrantEveryoneRead(PCWSTR Path)
{
    PSECURITY_DESCRIPTOR Descriptor = NULL;
    EXPLICIT_ACCESSW Access;
    PACL Extended = NULL;
    PACL Dacl = NULL;
    DWORD Error;

    if (!ReadDacl(Path, &Dacl, &Descriptor))
        return FALSE;

    RtlZeroMemory(&Access, sizeof(Access));
    Access.grfAccessPermissions = GENERIC_READ;
    Access.grfAccessMode = GRANT_ACCESS;
    Access.grfInheritance = NO_INHERITANCE;
    Access.Trustee.TrusteeForm = TRUSTEE_IS_NAME;
    Access.Trustee.TrusteeType = TRUSTEE_IS_WELL_KNOWN_GROUP;
    Access.Trustee.ptstrName = (LPWSTR)L"EVERYONE";

    Error = SetEntriesInAclW(1, &Access, Dacl, &Extended);
    LocalFree(Descriptor);
    if (Error != ERROR_SUCCESS || Extended == NULL)
        return FALSE;

    Error = SetNamedSecurityInfoW((LPWSTR)Path, SE_FILE_OBJECT, DACL_SECURITY_INFORMATION,
                                  NULL, NULL, Extended, NULL);
    LocalFree(Extended);
    return Error == ERROR_SUCCESS;
}

/*
 * A volume whose records carry their descriptor in an attribute of their own
 * reaches a different cache from one that keeps descriptors behind a security
 * id, so run the same checks on a second volume when the machine has one.
 */
static BOOL
FindOtherNtfsVolume(PWSTR Root, SIZE_T Chars)
{
    WCHAR Drive[4] = L"A:\\";
    WCHAR SystemDirectory[MAX_PATH];
    WCHAR FileSystem[16];
    WCHAR Label[MAX_PATH];
    DWORD Serial, Components, Flags;
    WCHAR SystemDrive = 0;
    WCHAR Letter;

    if (GetSystemDirectoryW(SystemDirectory, MAX_PATH))
        SystemDrive = SystemDirectory[0];
    for (Letter = L'C'; Letter <= L'Z'; Letter++)
    {
        UINT Type;

        Drive[0] = Letter;
        if (Letter == SystemDrive)
            continue;
        Type = GetDriveTypeW(Drive);
        if (Type != DRIVE_FIXED && Type != DRIVE_REMOVABLE)
            continue;
        if (!GetVolumeInformationW(Drive, Label, MAX_PATH, &Serial, &Components,
                                   &Flags, FileSystem, 16))
        {
            continue;
        }
        if (_wcsicmp(FileSystem, L"NTFS") != 0)
            continue;
        return SUCCEEDED(StringCchCopyW(Root, Chars, Drive));
    }
    return FALSE;
}

static void
CheckDirectoryDescriptor(PCWSTR Base)
{
    WCHAR Path[MAX_PATH];
    WCHAR Inner[MAX_PATH];
    ULONG BeforeMask = 0, BeforeCount = 0;
    ULONG AfterMask = 0, AfterCount = 0;
    ULONG AgainMask = 0, AgainCount = 0;
    HANDLE File;

    if (!GetTempFileNameW(Base, L"sed", 0, Path))
    {
        skip("No temporary name under %ls\n", Base);
        return;
    }
    DeleteFileW(Path);
    if (!CreateDirectoryW(Path, NULL))
    {
        skip("CreateDirectoryW failed with %lu\n", GetLastError());
        return;
    }

    if (!ReadEveryoneAccess(Path, &BeforeMask, &BeforeCount))
    {
        skip("%ls carries no per-file security\n", Base);
        RemoveDirectoryW(Path);
        return;
    }

    StringCchCopyW(Inner, MAX_PATH, Path);
    StringCchCatW(Inner, MAX_PATH, L"\\inner.tmp");
    File = CreateFileW(Inner, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
    ok(File != INVALID_HANDLE_VALUE, "CreateFileW in the directory failed with %lu\n",
       GetLastError());
    if (File != INVALID_HANDLE_VALUE)
        CloseHandle(File);

    /* Writing a file rewrites the directory record but not its descriptor. */
    ok(ReadEveryoneAccess(Path, &AfterMask, &AfterCount), "The directory lost its access list\n");
    ok(AfterMask == BeforeMask && AfterCount == BeforeCount,
       "Writing a file into the directory left mask %08lx over %lu entries, expected %08lx over %lu\n",
       AfterMask, AfterCount, BeforeMask, BeforeCount);

    /* Writing the descriptor itself must replace what the next reader sees. */
    if (GrantEveryoneRead(Path))
    {
        ok(ReadEveryoneAccess(Path, &AgainMask, &AgainCount),
           "The directory lost its access list\n");
        ok((AgainMask & FILE_GENERIC_READ) == FILE_GENERIC_READ,
           "Everyone was granted %08lx on the directory, expected at least %08lx\n",
           AgainMask, FILE_GENERIC_READ);
    }
    else
    {
        skip("The directory's access list cannot be written on %ls\n", Base);
    }

    DeleteFileW(Inner);
    RemoveDirectoryW(Path);
}

/*
 * A volume root is written by the formatter, which gives it a descriptor of
 * its own rather than a security id.  Files appearing in the root rewrite that
 * record without touching the descriptor, which must read back unchanged.
 */
static void
CheckRootDescriptor(PCWSTR Root)
{
    WCHAR Path[MAX_PATH];
    ULONG BeforeMask = 0, BeforeCount = 0;
    ULONG AfterMask = 0, AfterCount = 0;
    HANDLE File;

    if (!ReadEveryoneAccess(Root, &BeforeMask, &BeforeCount))
    {
        skip("%ls carries no per-file security\n", Root);
        return;
    }
    ok(BeforeCount != 0, "%ls has an empty access list\n", Root);

    if (!GetTempFileNameW(Root, L"ser", 0, Path))
    {
        skip("No temporary name under %ls\n", Root);
        return;
    }
    File = CreateFileW(Path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
    ok(File != INVALID_HANDLE_VALUE, "CreateFileW in the root failed with %lu\n",
       GetLastError());
    if (File != INVALID_HANDLE_VALUE)
        CloseHandle(File);

    ok(ReadEveryoneAccess(Root, &AfterMask, &AfterCount), "%ls lost its access list\n", Root);
    ok(AfterMask == BeforeMask && AfterCount == BeforeCount,
       "A file in the root left mask %08lx over %lu entries, expected %08lx over %lu\n",
       AfterMask, AfterCount, BeforeMask, BeforeCount);

    DeleteFileW(Path);
}

/*
 * Files created side by side inherit one descriptor, which the volume stores
 * once and names with a security id.  They must read back as the same rights,
 * and writing one of them must not reach the other.
 */
static void
CheckSharedDescriptors(PCWSTR Base)
{
    WCHAR Directory[MAX_PATH];
    WCHAR First[MAX_PATH];
    WCHAR Second[MAX_PATH];
    ULONG FirstMask = 0, FirstCount = 0;
    ULONG SecondMask = 0, SecondCount = 0;
    ULONG AfterMask = 0, AfterCount = 0;
    HANDLE File;
    ULONG Index;

    if (!GetTempFileNameW(Base, L"ses", 0, Directory))
    {
        skip("No temporary name under %ls\n", Base);
        return;
    }
    DeleteFileW(Directory);
    if (!CreateDirectoryW(Directory, NULL))
    {
        skip("CreateDirectoryW failed with %lu\n", GetLastError());
        return;
    }

    StringCchCopyW(First, MAX_PATH, Directory);
    StringCchCatW(First, MAX_PATH, L"\\one.tmp");
    StringCchCopyW(Second, MAX_PATH, Directory);
    StringCchCatW(Second, MAX_PATH, L"\\two.tmp");
    for (Index = 0; Index < 2; Index++)
    {
        File = CreateFileW(Index == 0 ? First : Second, GENERIC_WRITE, 0, NULL,
                           CREATE_ALWAYS, 0, NULL);
        ok(File != INVALID_HANDLE_VALUE, "CreateFileW failed with %lu\n", GetLastError());
        if (File != INVALID_HANDLE_VALUE)
            CloseHandle(File);
    }

    if (ReadEveryoneAccess(First, &FirstMask, &FirstCount) &&
        ReadEveryoneAccess(Second, &SecondMask, &SecondCount))
    {
        ok(FirstMask == SecondMask && FirstCount == SecondCount,
           "Two files of one directory read back %08lx over %lu and %08lx over %lu entries\n",
           FirstMask, FirstCount, SecondMask, SecondCount);

        if (GrantEveryoneRead(First))
        {
            ok(ReadEveryoneAccess(First, &AfterMask, &AfterCount),
               "The first file lost its access list\n");
            ok((AfterMask & FILE_GENERIC_READ) == FILE_GENERIC_READ,
               "Everyone was granted %08lx on the first file, expected at least %08lx\n",
               AfterMask, FILE_GENERIC_READ);

            ok(ReadEveryoneAccess(Second, &AfterMask, &AfterCount),
               "The second file lost its access list\n");
            ok(AfterMask == SecondMask && AfterCount == SecondCount,
               "Writing the first file left the second at %08lx over %lu entries, expected %08lx over %lu\n",
               AfterMask, AfterCount, SecondMask, SecondCount);
        }
        else
        {
            skip("The access list cannot be written under %ls\n", Base);
        }
    }

    DeleteFileW(First);
    DeleteFileW(Second);
    RemoveDirectoryW(Directory);
}

START_TEST(FileSecurityCache)
{
    WCHAR Directory[MAX_PATH];
    WCHAR Path[MAX_PATH];
    ULONG BeforeMask = 0, BeforeCount = 0;
    ULONG AfterMask = 0, AfterCount = 0;
    ULONG AgainMask = 0, AgainCount = 0;
    HANDLE File;

    if (!GetTempPathW(MAX_PATH, Directory))
    {
        skip("No temporary directory: %lu\n", GetLastError());
        return;
    }
    if (!GetTempFileNameW(Directory, L"sec", 0, Path))
    {
        skip("No temporary file: %lu\n", GetLastError());
        return;
    }

    File = CreateFileW(Path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
    ok(File != INVALID_HANDLE_VALUE, "CreateFileW failed with %lu\n", GetLastError());
    if (File == INVALID_HANDLE_VALUE)
        return;
    CloseHandle(File);

    if (!ReadEveryoneAccess(Path, &BeforeMask, &BeforeCount))
    {
        skip("The volume does not carry per-file security\n");
        DeleteFileW(Path);
        return;
    }
    ok(BeforeCount != 0, "The new file has an empty access list\n");

    /* Reading twice must not change what is stored. */
    ok(ReadEveryoneAccess(Path, &AgainMask, &AgainCount), "The file lost its access list\n");
    ok(AgainMask == BeforeMask && AgainCount == BeforeCount,
       "A second read returned mask %08lx over %lu entries, expected %08lx over %lu\n",
       AgainMask, AgainCount, BeforeMask, BeforeCount);

    if (!GrantEveryoneRead(Path))
    {
        skip("The access list cannot be written on this volume\n");
        DeleteFileW(Path);
        return;
    }

    /* The write must be visible to the next reader, and to the one after it. */
    ok(ReadEveryoneAccess(Path, &AfterMask, &AfterCount), "The file lost its access list\n");
    ok((AfterMask & FILE_GENERIC_READ) == FILE_GENERIC_READ,
       "Everyone was granted %08lx, expected at least %08lx\n", AfterMask, FILE_GENERIC_READ);

    ok(ReadEveryoneAccess(Path, &AgainMask, &AgainCount), "The file lost its access list\n");
    ok(AgainMask == AfterMask && AgainCount == AfterCount,
       "A second read returned mask %08lx over %lu entries, expected %08lx over %lu\n",
       AgainMask, AgainCount, AfterMask, AfterCount);

    DeleteFileW(Path);

    CheckDirectoryDescriptor(Directory);
    CheckSharedDescriptors(Directory);
    if (FindOtherNtfsVolume(Path, MAX_PATH))
    {
        CheckRootDescriptor(Path);
        CheckDirectoryDescriptor(Path);
        CheckSharedDescriptors(Path);
    }
    else
        skip("The machine has one NTFS volume, so only its security was checked\n");
}
