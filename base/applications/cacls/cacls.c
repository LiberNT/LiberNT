/*
 * ReactOS Control ACLs Program
 * Copyright (C) 2006 Thomas Weidenmueller
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include "precomp.h"

/* command line options */
BOOL OptionT = FALSE, OptionE = FALSE, OptionC = FALSE;
BOOL OptionG = FALSE, OptionR = FALSE, OptionP = FALSE, OptionD = FALSE;
LPCTSTR GUser, GPerm, RUser, PUser, PPerm, DUser;

static GENERIC_MAPPING FileGenericMapping =
{
    FILE_GENERIC_READ,
    FILE_GENERIC_WRITE,
    FILE_GENERIC_EXECUTE,
    FILE_ALL_ACCESS
};


static VOID
PrintError(DWORD dwError)
{
    if (dwError == ERROR_SUCCESS)
        return;

    ConMsgPuts(StdErr, FORMAT_MESSAGE_FROM_SYSTEM,
               NULL, dwError, LANG_USER_DEFAULT);
}

static BOOL
PrintFileDacl(IN LPTSTR FilePath,
              IN LPTSTR FileName)
{
    SIZE_T Length;
    PSECURITY_DESCRIPTOR SecurityDescriptor;
    DWORD SDSize = 0;
    TCHAR FullFileName[MAX_PATH + 1];
    BOOL Error = FALSE, Ret = FALSE;

    Length = _tcslen(FilePath) + _tcslen(FileName);
    if (Length > MAX_PATH)
    {
        /* file name too long */
        SetLastError(ERROR_FILE_NOT_FOUND);
        return FALSE;
    }

    _tcscpy(FullFileName, FilePath);
    _tcscat(FullFileName, FileName);

    /* find out how much memory we need */
    if (!GetFileSecurity(FullFileName,
                         DACL_SECURITY_INFORMATION,
                         NULL,
                         0,
                         &SDSize) &&
        GetLastError() != ERROR_INSUFFICIENT_BUFFER)
    {
        return FALSE;
    }

    SecurityDescriptor = (PSECURITY_DESCRIPTOR)HeapAlloc(GetProcessHeap(),
                                                         0,
                                                         SDSize);
    if (SecurityDescriptor != NULL)
    {
        if (GetFileSecurity(FullFileName,
                            DACL_SECURITY_INFORMATION,
                            SecurityDescriptor,
                            SDSize,
                            &SDSize))
        {
            PACL Dacl;
            BOOL DaclPresent;
            BOOL DaclDefaulted;

            if (GetSecurityDescriptorDacl(SecurityDescriptor,
                                          &DaclPresent,
                                          &Dacl,
                                          &DaclDefaulted))
            {
                if (DaclPresent)
                {
                    PACCESS_ALLOWED_ACE Ace;
                    DWORD AceIndex = 0;

                    /* dump the ACL */
                    while (GetAce(Dacl,
                                  AceIndex,
                                  (PVOID*)&Ace))
                    {
                        SID_NAME_USE Use;
                        DWORD NameSize = 0;
                        DWORD DomainSize = 0;
                        LPTSTR Name = NULL;
                        LPTSTR Domain = NULL;
                        LPTSTR SidString = NULL;
                        size_t IndentAccess;
                        DWORD AccessMask = Ace->Mask;
                        PSID Sid = (PSID)&Ace->SidStart;

                        /* attempt to translate the SID into a readable string */
                        if (!LookupAccountSid(NULL,
                                              Sid,
                                              Name,
                                              &NameSize,
                                              Domain,
                                              &DomainSize,
                                              &Use))
                        {
                            if (GetLastError() == ERROR_NONE_MAPPED || NameSize == 0)
                            {
                                goto BuildSidString;
                            }
                            else
                            {
                                if (GetLastError() != ERROR_INSUFFICIENT_BUFFER)
                                {
                                    Error = TRUE;
                                    break;
                                }

                                Name = (LPTSTR)HeapAlloc(GetProcessHeap(),
                                                         0,
                                                         (NameSize + DomainSize) * sizeof(TCHAR));
                                if (Name == NULL)
                                {
                                    SetLastError(ERROR_NOT_ENOUGH_MEMORY);
                                    Error = TRUE;
                                    break;
                                }

                                Domain = Name + NameSize;
                                Name[0] = _T('\0');
                                if (DomainSize != 0)
                                    Domain[0] = _T('\0');
                                if (!LookupAccountSid(NULL,
                                                      Sid,
                                                      Name,
                                                      &NameSize,
                                                      Domain,
                                                      &DomainSize,
                                                      &Use))
                                {
                                    HeapFree(GetProcessHeap(),
                                             0,
                                             Name);
                                    Name = NULL;
                                    goto BuildSidString;
                                }
                            }
                        }
                        else
                        {
BuildSidString:
                            if (!ConvertSidToStringSid(Sid,
                                                       &SidString))
                            {
                                Error = TRUE;
                                break;
                            }
                        }

                        /* print the file name or space */
                        ConPrintf(StdOut, L"%s ", FullFileName);

                        /* attempt to map the SID to a user name */
                        if (AceIndex == 0)
                        {
                            size_t i = 0;

                            /* overwrite the full file name with spaces so we
                               only print the file name once */
                            while (FullFileName[i] != _T('\0'))
                                FullFileName[i++] = _T(' ');
                        }

                        /* print the domain and/or user if possible, or the SID string */
                        if (Name != NULL && Domain[0] != _T('\0'))
                        {
                            ConPrintf(StdOut, L"%s\\%s:", Domain, Name);
                            IndentAccess = _tcslen(Domain) + _tcslen(Name);
                        }
                        else
                        {
                            LPTSTR DisplayString = (Name != NULL ? Name : SidString);

                            ConPrintf(StdOut, L"%s:", DisplayString);
                            IndentAccess = _tcslen(DisplayString);
                        }

                        /* print the ACE Flags */
                        if (Ace->Header.AceFlags & CONTAINER_INHERIT_ACE)
                        {
                            IndentAccess += (size_t)ConResPuts(StdOut, IDS_ABBR_CI);
                        }
                        if (Ace->Header.AceFlags & OBJECT_INHERIT_ACE)
                        {
                            IndentAccess += (size_t)ConResPuts(StdOut, IDS_ABBR_OI);
                        }
                        if (Ace->Header.AceFlags & INHERIT_ONLY_ACE)
                        {
                            IndentAccess += (size_t)ConResPuts(StdOut, IDS_ABBR_IO);
                        }

                        IndentAccess += 2;

                        /* print the access rights */
                        MapGenericMask(&AccessMask,
                                       &FileGenericMapping);
                        if (Ace->Header.AceType & ACCESS_DENIED_ACE_TYPE)
                        {
                            if (AccessMask == FILE_ALL_ACCESS)
                            {
                                ConResPuts(StdOut, IDS_ABBR_NONE);
                            }
                            else
                            {
                                ConResPuts(StdOut, IDS_DENY);
                                goto PrintSpecialAccess;
                            }
                        }
                        else
                        {
                            if (AccessMask == FILE_ALL_ACCESS)
                            {
                                ConResPuts(StdOut, IDS_ABBR_FULL);
                            }
                            else if (!(Ace->Mask & (GENERIC_READ | GENERIC_EXECUTE)) &&
                                     AccessMask == (FILE_GENERIC_READ | FILE_EXECUTE))
                            {
                                ConResPuts(StdOut, IDS_ABBR_READ);
                            }
                            else if (AccessMask == (FILE_GENERIC_READ | FILE_GENERIC_WRITE | FILE_EXECUTE | DELETE))
                            {
                                ConResPuts(StdOut, IDS_ABBR_CHANGE);
                            }
                            else if (AccessMask == FILE_GENERIC_WRITE)
                            {
                                ConResPuts(StdOut, IDS_ABBR_WRITE);
                            }
                            else
                            {
                                DWORD x;
                                static const struct
                                {
                                    DWORD Access;
                                    UINT uID;
                                }
                                AccessRights[] =
                                {
                                    {FILE_WRITE_ATTRIBUTES, IDS_FILE_WRITE_ATTRIBUTES},
                                    {FILE_READ_ATTRIBUTES, IDS_FILE_READ_ATTRIBUTES},
                                    {FILE_DELETE_CHILD, IDS_FILE_DELETE_CHILD},
                                    {FILE_EXECUTE, IDS_FILE_EXECUTE},
                                    {FILE_WRITE_EA, IDS_FILE_WRITE_EA},
                                    {FILE_READ_EA, IDS_FILE_READ_EA},
                                    {FILE_APPEND_DATA, IDS_FILE_APPEND_DATA},
                                    {FILE_WRITE_DATA, IDS_FILE_WRITE_DATA},
                                    {FILE_READ_DATA, IDS_FILE_READ_DATA},
                                    {FILE_GENERIC_EXECUTE, IDS_FILE_GENERIC_EXECUTE},
                                    {FILE_GENERIC_WRITE, IDS_FILE_GENERIC_WRITE},
                                    {FILE_GENERIC_READ, IDS_FILE_GENERIC_READ},
                                    {GENERIC_ALL, IDS_GENERIC_ALL},
                                    {GENERIC_EXECUTE, IDS_GENERIC_EXECUTE},
                                    {GENERIC_WRITE, IDS_GENERIC_WRITE},
                                    {GENERIC_READ, IDS_GENERIC_READ},
                                    {MAXIMUM_ALLOWED, IDS_MAXIMUM_ALLOWED},
                                    {ACCESS_SYSTEM_SECURITY, IDS_ACCESS_SYSTEM_SECURITY},
                                    {SPECIFIC_RIGHTS_ALL, IDS_SPECIFIC_RIGHTS_ALL},
                                    {STANDARD_RIGHTS_REQUIRED, IDS_STANDARD_RIGHTS_REQUIRED},
                                    {SYNCHRONIZE, IDS_SYNCHRONIZE},
                                    {WRITE_OWNER, IDS_WRITE_OWNER},
                                    {WRITE_DAC, IDS_WRITE_DAC},
                                    {READ_CONTROL, IDS_READ_CONTROL},
                                    {DELETE, IDS_DELETE},
                                    {STANDARD_RIGHTS_ALL, IDS_STANDARD_RIGHTS_ALL},
                                };

                                ConResPuts(StdOut, IDS_ALLOW);

PrintSpecialAccess:
                                ConResPuts(StdOut, IDS_SPECIAL_ACCESS);

                                /* print the special access rights */
                                x = ARRAYSIZE(AccessRights);
                                while (x-- != 0)
                                {
                                    if ((Ace->Mask & AccessRights[x].Access) == AccessRights[x].Access)
                                    {
                                        ConPrintf(StdOut, L"\n%s ", FullFileName);
                                        ConPrintf(StdOut, L"%*s", IndentAccess, L"");
                                        ConResPuts(StdOut, AccessRights[x].uID);
                                    }
                                }

                                ConPuts(StdOut, L"\n");
                            }
                        }

                        ConPuts(StdOut, L"\n");

                        /* free up all resources */
                        if (Name != NULL)
                        {
                            HeapFree(GetProcessHeap(),
                                     0,
                                     Name);
                        }

                        if (SidString != NULL)
                        {
                            LocalFree((HLOCAL)SidString);
                        }

                        AceIndex++;
                    }

                    if (!Error)
                        Ret = TRUE;
                }
                else
                {
                    SetLastError(ERROR_NO_SECURITY_ON_OBJECT);
                }
            }
        }

        HeapFree(GetProcessHeap(),
                 0,
                 SecurityDescriptor);
    }
    else
    {
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
    }

    return Ret;
}

/* Add a backslash at the end of a path string if necessary */
static VOID
AddBackslash(LPTSTR FilePath)
{
    /* Find the last backslash. If there is none, or if it doesn't
     * terminate the string, then append a backslash. */
    LPTSTR pch = _tcsrchr(FilePath, _T('\\'));
    if (!pch || *(pch+1))
        _tcscat(FilePath, _T("\\"));
}

static BOOL
GetPathOfFile(LPTSTR FilePath, LPCTSTR pszFiles)
{
    TCHAR FullPath[MAX_PATH];
    LPTSTR pch;
    DWORD attrs;

    _tcsncpy(FilePath, pszFiles, MAX_PATH);
    pch = _tcsrchr(FilePath, _T('\\'));
    if (pch != NULL)
    {
        *pch = 0;
        if (FilePath[0] == 0 || FilePath[_tcslen(FilePath) - 1] == _T(':'))
            _tcscat(FilePath, _T("\\"));
        if (!GetFullPathName(FilePath, MAX_PATH, FullPath, NULL))
        {
            PrintError(GetLastError());
            return FALSE;
        }
        _tcsncpy(FilePath, FullPath, MAX_PATH);

        attrs = GetFileAttributes(FilePath);
        if (attrs == 0xFFFFFFFF || !(attrs & FILE_ATTRIBUTE_DIRECTORY))
        {
            PrintError(ERROR_DIRECTORY);
            return FALSE;
        }
    }
    else
        GetCurrentDirectory(MAX_PATH, FilePath);

    AddBackslash(FilePath);
    return TRUE;
}

static BOOL
PrintDaclsOfFiles(LPCTSTR pszFiles)
{
    TCHAR FilePath[MAX_PATH];
    WIN32_FIND_DATA FindData;
    HANDLE hFind;
    DWORD LastError;

    /*
     * get the file path
     */
    if (!GetPathOfFile(FilePath, pszFiles))
        return FALSE;

    /*
     * search for the files
     */
    hFind = FindFirstFile(pszFiles, &FindData);
    if (hFind == INVALID_HANDLE_VALUE)
        return FALSE;
    do
    {
        if (FindData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
            continue;

        if (!PrintFileDacl(FilePath, FindData.cFileName))
        {
            LastError = GetLastError();
            if (LastError == ERROR_ACCESS_DENIED)
            {
                PrintError(LastError);
                if (!OptionC)
                {
                    FindClose(hFind);
                    return FALSE;
                }
            }
            else
            {
                break;
            }
        }
        else
        {
            ConPuts(StdOut, L"\n");
        }
    } while (FindNextFile(hFind, &FindData));
    LastError = GetLastError();
    FindClose(hFind);

    if (LastError != ERROR_NO_MORE_FILES)
    {
        PrintError(LastError);
        return FALSE;
    }

    return TRUE;
}

/*
 * In-memory representation of one DACL entry being edited. For the
 * standard ACCESS_ALLOWED/ACCESS_DENIED types, Mask and Sid describe the
 * entry. Any other ACE type found in the source DACL is preserved through
 * RawAce.
 */
typedef struct _DACL_ACE
{
    BYTE Type;
    BYTE Flags;
    DWORD Mask;
    PSID Sid;
    PVOID RawAce;
    WORD RawSize;
} DACL_ACE, *PDACL_ACE;

typedef struct _DACL_EDIT
{
    PDACL_ACE Aces;
    DWORD Count;
    DWORD Capacity;
    PSECURITY_DESCRIPTOR Sd;
    PSID LookupSids[4];
    DWORD LookupSidCount;
} DACL_EDIT, *PDACL_EDIT;

static VOID
EditInit(PDACL_EDIT Edit)
{
    ZeroMemory(Edit, sizeof(*Edit));
}

static VOID
EditFree(PDACL_EDIT Edit)
{
    DWORD i;

    HeapFree(GetProcessHeap(), 0, Edit->Aces);
    if (Edit->Sd != NULL)
        HeapFree(GetProcessHeap(), 0, Edit->Sd);
    for (i = 0; i < Edit->LookupSidCount; i++)
        HeapFree(GetProcessHeap(), 0, Edit->LookupSids[i]);
}

static BOOL
LookupUserSid(LPCTSTR User, PDACL_EDIT Edit, PSID *SidOut)
{
    SID_NAME_USE Use;
    LPTSTR Domain = NULL;
    DWORD DomainSize = 0;
    DWORD SidSize = 0;
    PSID Sid;

    *SidOut = NULL;
    SetLastError(ERROR_SUCCESS);
    LookupAccountName(NULL, User, NULL, &SidSize, NULL, &DomainSize, &Use);
    if (GetLastError() != ERROR_INSUFFICIENT_BUFFER)
        return FALSE;

    Sid = (PSID)HeapAlloc(GetProcessHeap(), 0, SidSize);
    if (Sid == NULL)
    {
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return FALSE;
    }

    if (DomainSize != 0)
    {
        Domain = (LPTSTR)HeapAlloc(GetProcessHeap(), 0,
                                   DomainSize * sizeof(TCHAR));
        if (Domain == NULL)
        {
            HeapFree(GetProcessHeap(), 0, Sid);
            SetLastError(ERROR_NOT_ENOUGH_MEMORY);
            return FALSE;
        }
    }

    if (!LookupAccountName(NULL, User, Sid, &SidSize, Domain, &DomainSize, &Use))
    {
        HeapFree(GetProcessHeap(), 0, Domain);
        HeapFree(GetProcessHeap(), 0, Sid);
        return FALSE;
    }

    HeapFree(GetProcessHeap(), 0, Domain);
    if (Edit->LookupSidCount < ARRAYSIZE(Edit->LookupSids))
        Edit->LookupSids[Edit->LookupSidCount++] = Sid;
    else
        HeapFree(GetProcessHeap(), 0, Sid);
    *SidOut = Sid;
    return TRUE;
}

/* Load the file's DACL into the edit list. With /E the existing entries are
   kept; without it the list starts empty so the options replace the ACL. */
static BOOL
EditLoadDacl(LPCTSTR FullPath, PDACL_EDIT Edit)
{
    PACL Dacl;
    BOOL DaclPresent, DaclDefaulted;
    DWORD SDSize = 0;
    DWORD i;

    if (!OptionE)
        return TRUE;

    SetLastError(ERROR_SUCCESS);
    if (!GetFileSecurity(FullPath, DACL_SECURITY_INFORMATION,
                         NULL, 0, &SDSize) &&
        GetLastError() != ERROR_INSUFFICIENT_BUFFER)
    {
        return FALSE;
    }

    Edit->Sd = (PSECURITY_DESCRIPTOR)HeapAlloc(GetProcessHeap(), 0, SDSize);
    if (Edit->Sd == NULL)
    {
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return FALSE;
    }

    if (!GetFileSecurity(FullPath, DACL_SECURITY_INFORMATION,
                         Edit->Sd, SDSize, &SDSize))
    {
        return FALSE;
    }

    if (!GetSecurityDescriptorDacl(Edit->Sd, &DaclPresent, &Dacl, &DaclDefaulted) ||
        !DaclPresent || Dacl == NULL)
    {
        /* no DACL or an empty one: nothing to preserve */
        return TRUE;
    }

    for (i = 0; ; i++)
    {
        PACE_HEADER Ace;
        PDACL_ACE Entry;

        if (!GetAce(Dacl, i, (PVOID *)&Ace))
            break;

        if (Edit->Count == Edit->Capacity)
        {
            DWORD Cap = Edit->Capacity ? Edit->Capacity * 2 : 8;
            PDACL_ACE NewAces = (PDACL_ACE)(Edit->Aces
                                                        ? HeapReAlloc(GetProcessHeap(), 0,
                                                               Edit->Aces,
                                                               Cap * sizeof(DACL_ACE))
                                                        : HeapAlloc(GetProcessHeap(), 0, Cap * sizeof(DACL_ACE)));
            if (NewAces == NULL)
            {
                SetLastError(ERROR_NOT_ENOUGH_MEMORY);
                return FALSE;
            }
            Edit->Aces = NewAces;
            Edit->Capacity = Cap;
        }

        Entry = &Edit->Aces[Edit->Count++];
        Entry->Type = Ace->AceType;
        Entry->Flags = Ace->AceFlags;
        Entry->RawAce = NULL;
        Entry->RawSize = 0;
        Entry->Mask = 0;
        Entry->Sid = NULL;

        if (Ace->AceType == ACCESS_ALLOWED_ACE_TYPE ||
            Ace->AceType == ACCESS_DENIED_ACE_TYPE)
        {
            Entry->Mask = ((PACCESS_ALLOWED_ACE)Ace)->Mask;
            Entry->Sid = (PSID)&((PACCESS_ALLOWED_ACE)Ace)->SidStart;
        }
        else
        {
            Entry->RawAce = Ace;
            Entry->RawSize = Ace->AceSize;
        }
    }

    return TRUE;
}

/* Append a new allow or deny ACE for Sid to the edit list. */
static BOOL
EditAppendAce(PDACL_EDIT Edit, BYTE Type, DWORD Mask, PSID Sid)
{
    PDACL_ACE Entry;

    if (Edit->Count == Edit->Capacity)
    {
        DWORD Cap = Edit->Capacity ? Edit->Capacity * 2 : 8;
        PDACL_ACE NewAces = (PDACL_ACE)(Edit->Aces
                                                    ? HeapReAlloc(GetProcessHeap(), 0,
                                                           Edit->Aces,
                                                           Cap * sizeof(DACL_ACE))
                                                    : HeapAlloc(GetProcessHeap(), 0, Cap * sizeof(DACL_ACE)));
        if (NewAces == NULL)
        {
            SetLastError(ERROR_NOT_ENOUGH_MEMORY);
            return FALSE;
        }
        Edit->Aces = NewAces;
        Edit->Capacity = Cap;
    }

    Entry = &Edit->Aces[Edit->Count++];
    Entry->Type = Type;
    Entry->Flags = 0;
    Entry->Mask = Mask;
    Entry->Sid = Sid;
    Entry->RawAce = NULL;
    Entry->RawSize = 0;
    return TRUE;
}

/* Drop every ACE whose SID matches. When AllowOnly is set, deny entries for
   the account survive. Returns the number of entries removed. */
static DWORD
EditRemoveUserAces(PDACL_EDIT Edit, PSID Sid, BOOL AllowOnly)
{
    DWORD i = 0, Removed = 0;

    while (i < Edit->Count)
    {
        PDACL_ACE Entry = &Edit->Aces[i];

        if (Entry->Sid != NULL &&
            EqualSid(Entry->Sid, Sid) &&
            (!AllowOnly || Entry->Type == ACCESS_ALLOWED_ACE_TYPE))
        {
            MoveMemory(Entry, Entry + 1,
                       (Edit->Count - i - 1) * sizeof(DACL_ACE));
            Edit->Count--;
            Removed++;
        }
        else
        {
            i++;
        }
    }

    return Removed;
}

/* Map a cacls permission letter to an access mask. N means "no access" and
   produces a deny entry. */
static BOOL
PermToMask(TCHAR Perm, DWORD *Mask, BYTE *Type)
{
    *Type = ACCESS_ALLOWED_ACE_TYPE;
    switch (Perm)
    {
        case _T('R'): /* Read */
            *Mask = FILE_GENERIC_READ | FILE_EXECUTE;
            return TRUE;
        case _T('W'): /* Write */
            *Mask = FILE_GENERIC_WRITE;
            return TRUE;
        case _T('C'): /* Change */
            *Mask = FILE_GENERIC_READ | FILE_GENERIC_WRITE |
                    FILE_EXECUTE | DELETE;
            return TRUE;
        case _T('F'): /* Full control */
            *Mask = FILE_ALL_ACCESS;
            return TRUE;
        case _T('N'): /* None */
            *Mask = FILE_ALL_ACCESS;
            *Type = ACCESS_DENIED_ACE_TYPE;
            return TRUE;
        default:
            break;
    }
    SetLastError(ERROR_INVALID_PARAMETER);
    return FALSE;
}

static BOOL
EditGrantUserAccessRights(PDACL_EDIT Edit, LPCTSTR User, TCHAR Perm)
{
    DWORD Mask;
    BYTE Type;
    PSID Sid;

    if (!PermToMask(Perm, &Mask, &Type) || Type != ACCESS_ALLOWED_ACE_TYPE ||
        !LookupUserSid(User, Edit, &Sid))
    {
        PrintError(GetLastError());
        return FALSE;
    }

    /* the grant states the account's final rights: replace its allow entries */
    EditRemoveUserAces(Edit, Sid, TRUE);
    return EditAppendAce(Edit, ACCESS_ALLOWED_ACE_TYPE, Mask, Sid);
}

static BOOL
EditReplaceUserAccessRights(PDACL_EDIT Edit, LPCTSTR User, TCHAR Perm)
{
    DWORD Mask;
    BYTE Type;
    PSID Sid;

    if (!PermToMask(Perm, &Mask, &Type) ||
        !LookupUserSid(User, Edit, &Sid))
    {
        PrintError(GetLastError());
        return FALSE;
    }

    EditRemoveUserAces(Edit, Sid, FALSE);
    return EditAppendAce(Edit, Type, Mask, Sid);
}

static BOOL
EditDenyUserAccess(PDACL_EDIT Edit, LPCTSTR User)
{
    PSID Sid;

    if (!LookupUserSid(User, Edit, &Sid))
    {
        PrintError(GetLastError());
        return FALSE;
    }

    /* Windows drops the account's existing allow entries when a deny is
       written; otherwise the allow would keep winning for the account. */
    EditRemoveUserAces(Edit, Sid, TRUE);
    return EditAppendAce(Edit, ACCESS_DENIED_ACE_TYPE, FILE_ALL_ACCESS, Sid);
}

static BOOL
EditRevokeUserAccessRights(PDACL_EDIT Edit, LPCTSTR User)
{
    PSID Sid;

    if (!LookupUserSid(User, Edit, &Sid))
    {
        PrintError(GetLastError());
        return FALSE;
    }

    EditRemoveUserAces(Edit, Sid, FALSE);
    return TRUE;
}

/* Write the edited DACL back, deny entries first so the ACL stays canonical. */
static BOOL
EditStoreDacl(LPCTSTR FullPath, PDACL_EDIT Edit)
{
    SECURITY_DESCRIPTOR Sd;
    DWORD AclSize = sizeof(ACL);
    PACL Acl;
    DWORD i;
    BOOL Ret = FALSE;

    for (i = 0; i < Edit->Count; i++)
    {
        PDACL_ACE Entry = &Edit->Aces[i];
        if (Entry->RawAce != NULL)
            AclSize += Entry->RawSize;
        else
            AclSize += FIELD_OFFSET(ACCESS_ALLOWED_ACE, SidStart) +
                       GetLengthSid(Entry->Sid);
    }

    Acl = (PACL)HeapAlloc(GetProcessHeap(), 0, AclSize);
    if (Acl == NULL)
    {
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return FALSE;
    }

    if (InitializeAcl(Acl, AclSize, ACL_REVISION))
    {
        DWORD Pass;

        Ret = TRUE;
        for (Pass = 0; Ret && Pass < 2; Pass++)
        {
            for (i = 0; i < Edit->Count; i++)
            {
                PDACL_ACE Entry = &Edit->Aces[i];

                if (Entry->RawAce != NULL)
                {
                    if (Pass == 1 &&
                        !AddAce(Acl, ACL_REVISION, MAXDWORD,
                                Entry->RawAce, Entry->RawSize))
                    {
                        Ret = FALSE;
                    }
                    continue;
                }

                if ((Pass == 0) != (Entry->Type == ACCESS_DENIED_ACE_TYPE))
                    continue;

                {
                    union
                    {
                        ACCESS_ALLOWED_ACE Ace;
                        BYTE Fill[sizeof(ACCESS_ALLOWED_ACE) + 255];
                    } AceBuf;
                    DWORD SidLen = GetLengthSid(Entry->Sid);

                    AceBuf.Ace.Header.AceType = Entry->Type;
                    AceBuf.Ace.Header.AceFlags = Entry->Flags;
                    AceBuf.Ace.Header.AceSize =
                        (WORD)(FIELD_OFFSET(ACCESS_ALLOWED_ACE, SidStart) + SidLen);
                    AceBuf.Ace.Mask = Entry->Mask;
                    CopyMemory(&AceBuf.Ace.SidStart, Entry->Sid, SidLen);

                    if (!AddAce(Acl, ACL_REVISION, MAXDWORD,
                                &AceBuf, AceBuf.Ace.Header.AceSize))
                    {
                        Ret = FALSE;
                        break;
                    }
                }
            }
        }

        if (Ret &&
            InitializeSecurityDescriptor(&Sd, SECURITY_DESCRIPTOR_REVISION) &&
            SetSecurityDescriptorDacl(&Sd, TRUE, Acl, FALSE))
        {
            Ret = SetFileSecurity(FullPath, DACL_SECURITY_INFORMATION, &Sd);
        }
        else
        {
            Ret = FALSE;
        }
    }

    HeapFree(GetProcessHeap(), 0, Acl);
    return Ret;
}

static BOOL
ChangeFileACL(LPCTSTR FilePath, LPCTSTR File)
{
    TCHAR FullPath[MAX_PATH + 1];
    DACL_EDIT Edit;
    BOOL Ok = TRUE;

    _tcscpy(FullPath, FilePath);
    _tcscat(FullPath, File);

    EditInit(&Edit);

    if (OptionG || OptionP || OptionD || OptionR)
    {
        Ok = EditLoadDacl(FullPath, &Edit);
    }

    if (Ok && OptionG)
    {
        /* Grant specified user access rights. */
        Ok = EditGrantUserAccessRights(&Edit, GUser, *GPerm);
    }

    if (Ok && OptionP)
    {
        /* Replace specified user's access rights. */
        Ok = EditReplaceUserAccessRights(&Edit, PUser, *PPerm);
    }

    if (Ok && OptionD)
    {
        /* Deny specified user access. */
        Ok = EditDenyUserAccess(&Edit, DUser);
    }

    if (Ok && OptionR)
    {
        /* Revoke specified user's access rights. */
        Ok = EditRevokeUserAccessRights(&Edit, RUser);
    }

    if (Ok && (OptionG || OptionP || OptionD || OptionR))
    {
        Ok = EditStoreDacl(FullPath, &Edit);
        if (Ok)
            ConPrintf(StdOut, L"processed file: %s\n", FullPath);
    }

    EditFree(&Edit);
    return Ok;
}

static BOOL
ChangeACLsOfFiles(LPCTSTR pszFiles)
{
    TCHAR FilePath[MAX_PATH];
    HANDLE hFind;
    WIN32_FIND_DATA FindData;
    DWORD LastError;

    /*
     * get the file path
     */
    if (!GetPathOfFile(FilePath, pszFiles))
        return FALSE;

    /*
     * search for files in current directory
     */
    hFind = FindFirstFile(pszFiles, &FindData);
    if (hFind == INVALID_HANDLE_VALUE)
        return FALSE;
    do
    {
        if (FindData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
            continue;

        if (!ChangeFileACL(FilePath, FindData.cFileName))
        {
            LastError = GetLastError();
            if (LastError == ERROR_ACCESS_DENIED)
            {
                PrintError(LastError);
                if (!OptionC)
                {
                    FindClose(hFind);
                    return FALSE;
                }
            }
            else
                break;
        }
    } while (FindNextFile(hFind, &FindData));
    LastError = GetLastError();
    FindClose(hFind);

    if (LastError != ERROR_NO_MORE_FILES)
    {
        PrintError(LastError);
        return FALSE;
    }

    return TRUE;
}

static BOOL
ChangeACLsOfFilesInCurDir(LPCTSTR pszFiles)
{
    HANDLE hFind;
    WIN32_FIND_DATA FindData;
    TCHAR szCurDir[MAX_PATH];
    DWORD LastError;

    /*
     * get the file path (current directory)
     */
    GetCurrentDirectory(MAX_PATH, szCurDir);
    AddBackslash(szCurDir);

    /*
     * search for files in current directory
     */
    hFind = FindFirstFile(pszFiles, &FindData);
    if (hFind == INVALID_HANDLE_VALUE)
        return FALSE;
    do
    {
        if (FindData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
            continue;

        if (!ChangeFileACL(szCurDir, FindData.cFileName))
        {
            LastError = GetLastError();
            if (LastError == ERROR_ACCESS_DENIED)
            {
                PrintError(LastError);
                if (!OptionC)
                {
                    FindClose(hFind);
                    return FALSE;
                }
            }
            else
                break;
        }
    } while (FindNextFile(hFind, &FindData));
    LastError = GetLastError();
    FindClose(hFind);

    if (LastError != ERROR_NO_MORE_FILES)
    {
        PrintError(LastError);
        return FALSE;
    }

    /*
     * search for subdirectory in current directory
     */
    hFind = FindFirstFile(_T("*"), &FindData);
    if (hFind == INVALID_HANDLE_VALUE)
        return FALSE;
    do
    {
        if (_tcscmp(FindData.cFileName, _T(".")) == 0 ||
            _tcscmp(FindData.cFileName, _T("..")) == 0)
            continue;

        if (FindData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
        {
            GetCurrentDirectory(MAX_PATH, szCurDir);
            if (SetCurrentDirectory(FindData.cFileName))
            {
                ChangeACLsOfFilesInCurDir(pszFiles);
                SetCurrentDirectory(szCurDir);
            }
            else
            {
                LastError = GetLastError();
                if (LastError == ERROR_ACCESS_DENIED)
                {
                    PrintError(LastError);
                    if (!OptionC)
                    {
                        FindClose(hFind);
                        return FALSE;
                    }
                }
                else
                    break;
            }
        }
    } while (FindNextFile(hFind, &FindData));
    LastError = GetLastError();
    FindClose(hFind);

    if (LastError != ERROR_NO_MORE_FILES)
    {
        PrintError(LastError);
        return FALSE;
    }
    return TRUE;
}

int _tmain(int argc, const TCHAR *argv[])
{
    INT i;
    LPTSTR pch;
    BOOL InvalidParameter = FALSE;

    /* Initialize the Console Standard Streams */
    ConInitStdStreams();

    if (argc <= 1)
    {
        ConResPuts(StdOut, IDS_HELP);
        return 0;
    }

    // FIXME: Convert to proper parsing, with support for /?

    /*
     * parse command line options
     */
    for (i = 2; i < argc; i++)
    {
        if (_tcsicmp(argv[i], _T("/T")) == 0)
        {
            OptionT = TRUE;
        }
        else if (_tcsicmp(argv[i], _T("/E")) == 0)
        {
            OptionE = TRUE;
        }
        else if (_tcsicmp(argv[i], _T("/C")) == 0)
        {
            OptionC = TRUE;
        }
        else if (_tcsicmp(argv[i], _T("/G")) == 0)
        {
            if (i + 1 < argc)
            {
                pch = _tcschr(argv[++i], _T(':'));
                if (pch != NULL)
                {
                    OptionG = TRUE;
                    *pch = 0;
                    GUser = argv[i];
                    GPerm = pch + 1;
                    continue;
                }
            }
            InvalidParameter = TRUE;
            break;
        }
        else if (_tcsicmp(argv[i], _T("/R")) == 0)
        {
            if (i + 1 < argc)
            {
                RUser = argv[++i];
                OptionR = TRUE;
                continue;
            }
            InvalidParameter = TRUE;
            break;
        }
        else if (_tcsicmp(argv[i], _T("/P")) == 0)
        {
            if (i + 1 < argc)
            {
                pch = _tcschr(argv[++i], _T(':'));
                if (pch != NULL)
                {
                    OptionP = TRUE;
                    *pch = 0;
                    PUser = argv[i];
                    PPerm = pch + 1;
                    continue;
                }
            }
            InvalidParameter = TRUE;
            break;
        }
        else if (_tcsicmp(argv[i], _T("/D")) == 0)
        {
            if (i + 1 < argc)
            {
                OptionD = TRUE;
                DUser = argv[++i];
                continue;
            }
            InvalidParameter = TRUE;
            break;
        }
        else
        {
            InvalidParameter = TRUE;
            break;
        }
    }

    if (InvalidParameter)
    {
        PrintError(ERROR_INVALID_PARAMETER);
        ConResPuts(StdOut, IDS_HELP);
        return 1;
    }

    /* /R is only valid with /E */
    if (OptionR && !OptionE)
    {
        OptionR = FALSE;
    }

    PrintDaclsOfFiles(argv[1]);

    if (OptionT)
    {
        ChangeACLsOfFilesInCurDir(argv[1]);
    }
    else
    {
        ChangeACLsOfFiles(argv[1]);
    }

    return 0;
}
