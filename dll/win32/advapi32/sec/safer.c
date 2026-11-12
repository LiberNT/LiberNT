/*
 * PROJECT:     LiberNT system libraries
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Software restriction policies: levels, rule evaluation and restricted tokens
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <advapi32.h>

WINE_DEFAULT_DEBUG_CHANNEL(advapi);

#define SAFER_POLICY_PATH L"Software\\Policies\\Microsoft\\Windows\\Safer\\CodeIdentifiers"
#define SAFER_EVENT_SOURCE L"Microsoft-Windows-SoftwareRestrictionPolicies"
#define SAFER_EVENT_DEFAULT_LEVEL 865
#define SAFER_EVENT_PATH_RULE 866
#define SAFER_EVENT_PUBLISHER_RULE 867
#define SAFER_EVENT_OTHER_RULE 868
#define SAFER_GUID_CHARS 39
#define SAFER_HASH_CACHE 4

typedef struct _SAFER_BUILTIN_LEVEL
{
    DWORD LevelId;
    PCWSTR FriendlyName;
    PCWSTR Description;
} SAFER_BUILTIN_LEVEL;

/* TODO: localized level names and descriptions */
static const SAFER_BUILTIN_LEVEL SaferBuiltinLevels[] =
{
    { SAFER_LEVELID_DISALLOWED, L"Disallowed",
      L"Software will not run, regardless of the user rights of the user." },
    { SAFER_LEVELID_UNTRUSTED, L"Untrusted",
      L"Allows programs to execute with access only to resources granted to open well-known groups, "
      L"blocking access to Administrator and Power User privileges and personally granted rights." },
    { SAFER_LEVELID_CONSTRAINED, L"Restricted",
      L"Software cannot access certain resources, such as cryptographic keys and credentials, "
      L"regardless of the user rights of the user." },
    { SAFER_LEVELID_NORMALUSER, L"Basic User",
      L"Allows programs to execute as a user that does not have Administrator or Power User user rights. "
      L"Software can access resources accessible by normal users." },
    { SAFER_LEVELID_FULLYTRUSTED, L"Unrestricted",
      L"Software user rights are determined by the user rights of the user." },
};

typedef struct _SAFER_RULE_KIND
{
    DWORD IdentityType;
    PCWSTR Subkey;
    PCWSTR LogName;
} SAFER_RULE_KIND;

static const SAFER_RULE_KIND SaferRuleKinds[] =
{
    { SaferIdentityTypeImageHash, L"Hashes", L"hash" },
    { SaferIdentityTypeImageName, L"Paths", L"path" },
    { SaferIdentityTypeUrlZone, L"UrlZones", L"zone" },
};

static const PCWSTR SaferDefaultExecutableTypes[] =
{
    L"ADE", L"ADP", L"BAS", L"BAT", L"CHM", L"CMD", L"COM", L"CPL", L"CRT", L"EXE", L"HLP",
    L"HTA", L"INF", L"INS", L"ISP", L"JS", L"LNK", L"MDB", L"MDE", L"MSC", L"MSI", L"MSP",
    L"OCX", L"PCD", L"PIF", L"PL", L"REG", L"SCR", L"SHS", L"URL", L"VB", L"VBS", L"WSC",
};

typedef struct _SAFER_LEVEL_OBJECT
{
    LIST_ENTRY Link;
    DWORD ScopeId;
    DWORD LevelId;
    DWORD IdentityType;
    DWORD SaferFlags;
    DWORD ExtendedError;
    GUID RuleGuid;
    PWSTR RulePath;
} SAFER_LEVEL_OBJECT, *PSAFER_LEVEL_OBJECT;

typedef struct _SAFER_MATCH
{
    BOOL Found;
    DWORD IdentityType;
    DWORD ScopeId;
    DWORD LevelId;
    DWORD SaferFlags;
    DWORD Rank;
    GUID Guid;
    PWSTR RulePath;
} SAFER_MATCH, *PSAFER_MATCH;

typedef struct _SAFER_SCOPE_POLICY
{
    BOOL HasDefaultLevel;
    DWORD DefaultLevel;
    BOOL HasTransparentEnabled;
    DWORD TransparentEnabled;
    DWORD PolicyScope;
    DWORD AuthenticodeEnabled;
} SAFER_SCOPE_POLICY, *PSAFER_SCOPE_POLICY;

typedef struct _SAFER_HASH_ENTRY
{
    ALG_ID Algorithm;
    BOOL Valid;
    DWORD Size;
    BYTE Hash[SAFER_MAX_HASH_SIZE];
} SAFER_HASH_ENTRY, *PSAFER_HASH_ENTRY;

typedef struct _SAFER_IMAGE
{
    PSAFER_CODE_PROPERTIES Properties;
    PWSTR Path;
    BOOL DataLoaded;
    BOOL HaveData;
    const BYTE *Data;
    ULONGLONG Size;
    HANDLE File;
    HANDLE Mapping;
    PVOID View;
    SAFER_HASH_ENTRY Hashes[SAFER_HASH_CACHE];
} SAFER_IMAGE, *PSAFER_IMAGE;

typedef VOID (*PSAFER_RULE_CALLBACK)(DWORD ScopeId, DWORD LevelId, HKEY RuleKey, const GUID *Guid, PVOID Context);

enum
{
    SaferSidAdministrators,
    SaferSidPowerUsers,
    SaferSidWorld,
    SaferSidInteractive,
    SaferSidAuthenticatedUsers,
    SaferSidUsers,
    SaferSidRestricted,
    SaferSidCount
};

static const WELL_KNOWN_SID_TYPE SaferWellKnownSidTypes[SaferSidCount] =
{
    WinBuiltinAdministratorsSid,
    WinBuiltinPowerUsersSid,
    WinWorldSid,
    WinInteractiveSid,
    WinAuthenticatedUserSid,
    WinBuiltinUsersSid,
    WinRestrictedCodeSid,
};

typedef struct _SAFER_TOKEN_PLAN
{
    BOOL DisableMaxPrivilege;
    DWORD DisableCount;
    DWORD InvertedCount;
    DWORD AddedCount;
    DWORD RestrictCount;
    PSID_AND_ATTRIBUTES Disable;
    PSID_AND_ATTRIBUTES Inverted;
    PSID_AND_ATTRIBUTES Added;
    PSID_AND_ATTRIBUTES Restrict;
    BYTE WellKnown[SaferSidCount][SECURITY_MAX_SID_SIZE];
} SAFER_TOKEN_PLAN, *PSAFER_TOKEN_PLAN;

static LIST_ENTRY SaferLevelList = { &SaferLevelList, &SaferLevelList };
static SRWLOCK SaferLevelLock = SRWLOCK_INIT;

static PVOID
SaferAlloc(SIZE_T Size)
{
    return HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, Size);
}

static VOID
SaferFree(PVOID Buffer)
{
    if (Buffer)
        HeapFree(GetProcessHeap(), 0, Buffer);
}

static PWSTR
SaferDuplicateString(PCWSTR String)
{
    SIZE_T Size;
    PWSTR Copy;

    if (!String)
        return NULL;

    Size = (wcslen(String) + 1) * sizeof(WCHAR);
    Copy = SaferAlloc(Size);
    if (Copy)
        CopyMemory(Copy, String, Size);
    return Copy;
}

static const SAFER_BUILTIN_LEVEL *
SaferFindBuiltinLevel(DWORD LevelId)
{
    ULONG i;

    for (i = 0; i < ARRAYSIZE(SaferBuiltinLevels); i++)
    {
        if (SaferBuiltinLevels[i].LevelId == LevelId)
            return &SaferBuiltinLevels[i];
    }
    return NULL;
}

static BOOL
SaferIsValidScope(DWORD ScopeId)
{
    return ScopeId == SAFER_SCOPEID_MACHINE || ScopeId == SAFER_SCOPEID_USER;
}

static HKEY
SaferScopeRoot(DWORD ScopeId)
{
    return (ScopeId == SAFER_SCOPEID_MACHINE) ? HKEY_LOCAL_MACHINE : HKEY_CURRENT_USER;
}

static VOID
SaferFormatGuid(const GUID *Guid, PWSTR Buffer)
{
    _snwprintf(Buffer, SAFER_GUID_CHARS,
               L"{%08lx-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x}",
               Guid->Data1, Guid->Data2, Guid->Data3,
               Guid->Data4[0], Guid->Data4[1], Guid->Data4[2], Guid->Data4[3],
               Guid->Data4[4], Guid->Data4[5], Guid->Data4[6], Guid->Data4[7]);
    Buffer[SAFER_GUID_CHARS - 1] = UNICODE_NULL;
}

static BOOL
SaferParseGuid(PCWSTR String, GUID *Guid)
{
    UNICODE_STRING Text;

    RtlInitUnicodeString(&Text, String);
    return NT_SUCCESS(RtlGUIDFromString(&Text, Guid));
}

static LSTATUS
SaferOpenPolicyKey(DWORD ScopeId, PCWSTR SubKey, REGSAM Access, PHKEY Key)
{
    WCHAR Path[MAX_PATH];

    if (SubKey)
        _snwprintf(Path, ARRAYSIZE(Path), L"%s\\%s", SAFER_POLICY_PATH, SubKey);
    else
        _snwprintf(Path, ARRAYSIZE(Path), L"%s", SAFER_POLICY_PATH);
    Path[ARRAYSIZE(Path) - 1] = UNICODE_NULL;

    return RegOpenKeyExW(SaferScopeRoot(ScopeId), Path, 0, Access | KEY_WOW64_64KEY, Key);
}

static LSTATUS
SaferCreatePolicyKey(DWORD ScopeId, PCWSTR SubKey, PHKEY Key)
{
    WCHAR Path[MAX_PATH];

    if (SubKey)
        _snwprintf(Path, ARRAYSIZE(Path), L"%s\\%s", SAFER_POLICY_PATH, SubKey);
    else
        _snwprintf(Path, ARRAYSIZE(Path), L"%s", SAFER_POLICY_PATH);
    Path[ARRAYSIZE(Path) - 1] = UNICODE_NULL;

    return RegCreateKeyExW(SaferScopeRoot(ScopeId), Path, 0, NULL, 0,
                           KEY_SET_VALUE | KEY_QUERY_VALUE | KEY_WOW64_64KEY, NULL, Key, NULL);
}

static BOOL
SaferReadDword(HKEY Key, PCWSTR Name, PDWORD Value)
{
    DWORD Type, Size = sizeof(DWORD), Data;

    if (RegQueryValueExW(Key, Name, NULL, &Type, (PBYTE)&Data, &Size) != ERROR_SUCCESS)
        return FALSE;
    if ((Type != REG_DWORD && Type != REG_BINARY) || Size != sizeof(DWORD))
        return FALSE;

    *Value = Data;
    return TRUE;
}

static BOOL
SaferReadQword(HKEY Key, PCWSTR Name, PULONGLONG Value)
{
    DWORD Type, Size = sizeof(ULONGLONG);
    ULONGLONG Data;

    if (RegQueryValueExW(Key, Name, NULL, &Type, (PBYTE)&Data, &Size) != ERROR_SUCCESS)
        return FALSE;
    if ((Type != REG_QWORD && Type != REG_BINARY) || Size != sizeof(ULONGLONG))
        return FALSE;

    *Value = Data;
    return TRUE;
}

static PWSTR
SaferReadString(HKEY Key, PCWSTR Name)
{
    DWORD Type, Size = 0;
    PWSTR Buffer;

    if (RegQueryValueExW(Key, Name, NULL, &Type, NULL, &Size) != ERROR_SUCCESS)
        return NULL;
    if (Type != REG_SZ && Type != REG_EXPAND_SZ)
        return NULL;

    Buffer = SaferAlloc(Size + 2 * sizeof(WCHAR));
    if (!Buffer)
        return NULL;

    if (RegQueryValueExW(Key, Name, NULL, &Type, (PBYTE)Buffer, &Size) != ERROR_SUCCESS ||
        (Type != REG_SZ && Type != REG_EXPAND_SZ))
    {
        SaferFree(Buffer);
        return NULL;
    }

    Buffer[Size / sizeof(WCHAR)] = UNICODE_NULL;
    return Buffer;
}

static PWSTR
SaferReadMultiString(HKEY Key, PCWSTR Name)
{
    DWORD Type, Size = 0;
    PWSTR Buffer;

    if (RegQueryValueExW(Key, Name, NULL, &Type, NULL, &Size) != ERROR_SUCCESS || Type != REG_MULTI_SZ)
        return NULL;

    Buffer = SaferAlloc(Size + 2 * sizeof(WCHAR));
    if (!Buffer)
        return NULL;

    if (RegQueryValueExW(Key, Name, NULL, &Type, (PBYTE)Buffer, &Size) != ERROR_SUCCESS || Type != REG_MULTI_SZ)
    {
        SaferFree(Buffer);
        return NULL;
    }

    Buffer[Size / sizeof(WCHAR)] = UNICODE_NULL;
    Buffer[Size / sizeof(WCHAR) + 1] = UNICODE_NULL;
    return Buffer;
}

static VOID
SaferReadScopePolicy(DWORD ScopeId, PSAFER_SCOPE_POLICY Policy)
{
    HKEY Key;

    ZeroMemory(Policy, sizeof(*Policy));
    if (SaferOpenPolicyKey(ScopeId, NULL, KEY_QUERY_VALUE, &Key) != ERROR_SUCCESS)
        return;

    Policy->HasDefaultLevel = SaferReadDword(Key, L"DefaultLevel", &Policy->DefaultLevel);
    Policy->HasTransparentEnabled = SaferReadDword(Key, L"TransparentEnabled", &Policy->TransparentEnabled);
    SaferReadDword(Key, L"PolicyScope", &Policy->PolicyScope);
    SaferReadDword(Key, L"AuthenticodeEnabled", &Policy->AuthenticodeEnabled);
    RegCloseKey(Key);
}

static DWORD
SaferEffectiveDefaultLevel(const SAFER_SCOPE_POLICY *Machine, const SAFER_SCOPE_POLICY *User)
{
    DWORD Level = SAFER_LEVELID_FULLYTRUSTED;
    BOOL Defined = FALSE;

    if (Machine->HasDefaultLevel && SaferFindBuiltinLevel(Machine->DefaultLevel))
    {
        Level = Machine->DefaultLevel;
        Defined = TRUE;
    }

    if (User->HasDefaultLevel && SaferFindBuiltinLevel(User->DefaultLevel) &&
        (!Defined || User->DefaultLevel < Level))
    {
        Level = User->DefaultLevel;
    }

    return Level;
}

static BOOL
SaferCreateWellKnown(WELL_KNOWN_SID_TYPE Type, PSID Sid)
{
    DWORD Size = SECURITY_MAX_SID_SIZE;

    return CreateWellKnownSid(Type, NULL, Sid, &Size);
}

static BOOL
SaferCallerIsAdministrator(VOID)
{
    BYTE Sid[SECURITY_MAX_SID_SIZE];
    BOOL Member = FALSE;

    if (!SaferCreateWellKnown(WinBuiltinAdministratorsSid, Sid))
        return FALSE;
    if (!CheckTokenMembership(NULL, Sid, &Member))
        return FALSE;
    return Member;
}

static VOID
SaferNormalizeSlashes(PWSTR Path)
{
    for (; *Path; Path++)
    {
        if (*Path == L'/')
            *Path = L'\\';
    }
}

static VOID
SaferStripLongPrefix(PWSTR Path)
{
    SIZE_T Length = wcslen(Path);

    if (Length >= 8 && !_wcsnicmp(Path, L"\\\\?\\UNC\\", 8))
    {
        MoveMemory(Path + 2, Path + 8, (Length - 8 + 1) * sizeof(WCHAR));
        return;
    }

    if (Length >= 4 && (!wcsncmp(Path, L"\\\\?\\", 4) || !wcsncmp(Path, L"\\??\\", 4)))
        MoveMemory(Path, Path + 4, (Length - 4 + 1) * sizeof(WCHAR));
}

static VOID
SaferTrimTrailingSeparators(PWSTR Path)
{
    SIZE_T Length = wcslen(Path);

    while (Length > 0 && Path[Length - 1] == L'\\')
        Path[--Length] = UNICODE_NULL;
}

static PWSTR
SaferDevicePathToDos(PWSTR Path)
{
    WCHAR Drive[3] = { L'A', L':', UNICODE_NULL };
    WCHAR Target[MAX_PATH];
    SIZE_T TargetLength;
    PWSTR Dos;

    if (_wcsnicmp(Path, L"\\Device\\", 8))
        return Path;

    for (; Drive[0] <= L'Z'; Drive[0]++)
    {
        if (!QueryDosDeviceW(Drive, Target, ARRAYSIZE(Target)))
            continue;

        TargetLength = wcslen(Target);
        if (TargetLength == 0 || _wcsnicmp(Path, Target, TargetLength) ||
            (Path[TargetLength] != L'\\' && Path[TargetLength] != UNICODE_NULL))
        {
            continue;
        }

        Dos = SaferAlloc((wcslen(Path + TargetLength) + 3) * sizeof(WCHAR));
        if (!Dos)
            return Path;

        Dos[0] = Drive[0];
        Dos[1] = L':';
        wcscpy(Dos + 2, Path + TargetLength);
        SaferFree(Path);
        return Dos;
    }

    return Path;
}

static PWSTR
SaferCanonicalPath(PWSTR Path)
{
    DWORD Length;
    PWSTR Full;

    SaferNormalizeSlashes(Path);
    SaferStripLongPrefix(Path);

    Length = GetFullPathNameW(Path, 0, NULL, NULL);
    if (Length)
    {
        Full = SaferAlloc((Length + 1) * sizeof(WCHAR));
        if (Full && GetFullPathNameW(Path, Length + 1, Full, NULL))
        {
            SaferFree(Path);
            Path = Full;
        }
        else
        {
            SaferFree(Full);
        }
    }

    Length = GetLongPathNameW(Path, NULL, 0);
    if (Length)
    {
        Full = SaferAlloc((Length + 1) * sizeof(WCHAR));
        if (Full && GetLongPathNameW(Path, Full, Length + 1))
        {
            SaferFree(Path);
            Path = Full;
        }
        else
        {
            SaferFree(Full);
        }
    }

    SaferStripLongPrefix(Path);
    SaferTrimTrailingSeparators(Path);
    return Path;
}

static PWSTR
SaferImagePathFromProperties(PSAFER_CODE_PROPERTIES Properties)
{
    PWSTR Path = NULL;
    DWORD Length;

    if (Properties->ImagePath && Properties->ImagePath[0])
    {
        Path = SaferDuplicateString(Properties->ImagePath);
    }
    else if (Properties->hImageFileHandle && Properties->hImageFileHandle != INVALID_HANDLE_VALUE)
    {
        Length = GetFinalPathNameByHandleW(Properties->hImageFileHandle, NULL, 0, VOLUME_NAME_DOS);
        if (Length)
        {
            Path = SaferAlloc((Length + 1) * sizeof(WCHAR));
            if (Path && !GetFinalPathNameByHandleW(Properties->hImageFileHandle, Path, Length + 1, VOLUME_NAME_DOS))
            {
                SaferFree(Path);
                Path = NULL;
            }
        }
    }

    if (!Path)
        return NULL;

    SaferNormalizeSlashes(Path);
    if (Properties->dwCheckFlags & SAFER_CRITERIA_IMAGEPATH_NT)
    {
        SaferStripLongPrefix(Path);
        Path = SaferDevicePathToDos(Path);
    }

    return SaferCanonicalPath(Path);
}

static HKEY
SaferHiveFromName(PCWSTR Name, SIZE_T Length)
{
    static const struct { PCWSTR Name; HKEY Key; } Hives[] =
    {
        { L"HKEY_LOCAL_MACHINE", HKEY_LOCAL_MACHINE },
        { L"HKEY_CURRENT_USER", HKEY_CURRENT_USER },
        { L"HKEY_CLASSES_ROOT", HKEY_CLASSES_ROOT },
        { L"HKEY_USERS", HKEY_USERS },
        { L"HKEY_CURRENT_CONFIG", HKEY_CURRENT_CONFIG },
    };
    ULONG i;

    for (i = 0; i < ARRAYSIZE(Hives); i++)
    {
        if (wcslen(Hives[i].Name) == Length && !_wcsnicmp(Hives[i].Name, Name, Length))
            return Hives[i].Key;
    }
    return NULL;
}

static PWSTR
SaferExpandEnvironment(PCWSTR Text)
{
    DWORD Length;
    PWSTR Expanded;

    Length = ExpandEnvironmentStringsW(Text, NULL, 0);
    if (!Length)
        return NULL;

    Expanded = SaferAlloc((Length + 1) * sizeof(WCHAR));
    if (Expanded && !ExpandEnvironmentStringsW(Text, Expanded, Length + 1))
    {
        SaferFree(Expanded);
        return NULL;
    }
    return Expanded;
}

static PWSTR
SaferExpandRegistryPathRule(PCWSTR Rule)
{
    PCWSTR End, HiveEnd, ValueStart, Suffix;
    PWSTR KeyPath, ValueName, Value, Expanded, Result;
    HKEY Root, Key;
    SIZE_T Length;

    End = wcschr(Rule + 1, L'%');
    if (!End)
        return NULL;

    HiveEnd = wcschr(Rule + 1, L'\\');
    if (!HiveEnd || HiveEnd > End)
        return NULL;

    Root = SaferHiveFromName(Rule + 1, HiveEnd - (Rule + 1));
    if (!Root)
        return NULL;

    ValueStart = End;
    while (ValueStart > HiveEnd && ValueStart[-1] != L'\\')
        ValueStart--;
    if (ValueStart <= HiveEnd + 1)
        return NULL;

    KeyPath = SaferAlloc((ValueStart - HiveEnd) * sizeof(WCHAR));
    ValueName = SaferAlloc((End - ValueStart + 1) * sizeof(WCHAR));
    if (!KeyPath || !ValueName)
    {
        SaferFree(KeyPath);
        SaferFree(ValueName);
        return NULL;
    }

    CopyMemory(KeyPath, HiveEnd + 1, (ValueStart - 1 - (HiveEnd + 1)) * sizeof(WCHAR));
    CopyMemory(ValueName, ValueStart, (End - ValueStart) * sizeof(WCHAR));

    Value = NULL;
    if (RegOpenKeyExW(Root, KeyPath, 0, KEY_QUERY_VALUE | KEY_WOW64_64KEY, &Key) == ERROR_SUCCESS)
    {
        Value = SaferReadString(Key, ValueName);
        RegCloseKey(Key);
    }
    SaferFree(KeyPath);
    SaferFree(ValueName);
    if (!Value)
        return NULL;

    Expanded = SaferExpandEnvironment(Value);
    SaferFree(Value);
    if (!Expanded)
        return NULL;

    SaferTrimTrailingSeparators(Expanded);
    Suffix = End + 1;
    Length = wcslen(Expanded) + wcslen(Suffix) + 2;
    Result = SaferAlloc(Length * sizeof(WCHAR));
    if (Result)
    {
        wcscpy(Result, Expanded);
        if (*Suffix)
        {
            wcscat(Result, L"\\");
            wcscat(Result, Suffix);
        }
    }
    SaferFree(Expanded);
    return Result;
}

static PWSTR
SaferExpandPathRule(PCWSTR Rule)
{
    PWSTR Expanded;

    if (Rule[0] == L'%' && !_wcsnicmp(Rule + 1, L"HKEY_", 5))
        Expanded = SaferExpandRegistryPathRule(Rule);
    else
        Expanded = SaferExpandEnvironment(Rule);

    if (!Expanded)
        return NULL;

    SaferNormalizeSlashes(Expanded);
    SaferStripLongPrefix(Expanded);
    SaferTrimTrailingSeparators(Expanded);
    if (!Expanded[0])
    {
        SaferFree(Expanded);
        return NULL;
    }
    return Expanded;
}

static BOOL
SaferWildcardMatch(PCWSTR Pattern, SIZE_T PatternLength, PCWSTR Text, SIZE_T TextLength)
{
    SIZE_T p = 0, t = 0, StarPattern = (SIZE_T)-1, StarText = 0;

    while (t < TextLength)
    {
        if (p < PatternLength &&
            (Pattern[p] == L'?' || RtlUpcaseUnicodeChar(Pattern[p]) == RtlUpcaseUnicodeChar(Text[t])))
        {
            p++;
            t++;
        }
        else if (p < PatternLength && Pattern[p] == L'*')
        {
            StarPattern = p++;
            StarText = t;
        }
        else if (StarPattern != (SIZE_T)-1)
        {
            p = StarPattern + 1;
            t = ++StarText;
        }
        else
        {
            return FALSE;
        }
    }

    while (p < PatternLength && Pattern[p] == L'*')
        p++;

    return p == PatternLength;
}

static DWORD
SaferCountWildcards(PCWSTR Rule)
{
    DWORD Count = 0;

    for (; *Rule; Rule++)
    {
        if (*Rule == L'*' || *Rule == L'?')
            Count++;
    }
    return min(Count, 0xFF);
}

static PCWSTR
SaferNextComponent(PCWSTR Start, PSIZE_T Length)
{
    PCWSTR End = wcschr(Start, L'\\');

    if (!End)
        End = Start + wcslen(Start);
    *Length = End - Start;
    return End;
}

static DWORD
SaferPathRuleRank(PCWSTR Rule, PCWSTR Path)
{
    DWORD Wildcards = SaferCountWildcards(Rule);
    PCWSTR RuleNext, PathNext, Name;
    SIZE_T RuleLength, PathLength;
    DWORD Depth = 0;

    if (!wcschr(Rule, L'\\') && !wcschr(Rule, L':'))
    {
        Name = wcsrchr(Path, L'\\');
        Name = Name ? Name + 1 : Path;
        if (!SaferWildcardMatch(Rule, wcslen(Rule), Name, wcslen(Name)))
            return 0;
        return 0x20000 + (0xFF - Wildcards);
    }

    for (;;)
    {
        RuleNext = SaferNextComponent(Rule, &RuleLength);
        PathNext = SaferNextComponent(Path, &PathLength);

        if (!SaferWildcardMatch(Rule, RuleLength, Path, PathLength))
            return 0;

        Depth++;

        if (*RuleNext == UNICODE_NULL)
        {
            if (*PathNext == UNICODE_NULL)
                return 0x30000 + (0xFF - Wildcards);
            return 0x10000 + (min(Depth, 0xFF) << 8) + (0xFF - Wildcards);
        }

        if (*PathNext == UNICODE_NULL)
            return 0;

        Rule = RuleNext + 1;
        Path = PathNext + 1;
    }
}

static VOID
SaferEnumerateRules(DWORD IdentityType, PSAFER_RULE_CALLBACK Callback, PVOID Context)
{
    static const DWORD Scopes[] = { SAFER_SCOPEID_MACHINE, SAFER_SCOPEID_USER };
    PCWSTR Subkey = NULL;
    WCHAR Name[64];
    WCHAR GuidName[64];
    HKEY TypeKey, RuleKey;
    DWORD NameLength, Index;
    ULONG s, l, k;
    GUID Guid;

    for (k = 0; k < ARRAYSIZE(SaferRuleKinds); k++)
    {
        if (SaferRuleKinds[k].IdentityType == IdentityType)
            Subkey = SaferRuleKinds[k].Subkey;
    }
    if (!Subkey)
        return;

    for (s = 0; s < ARRAYSIZE(Scopes); s++)
    {
        for (l = 0; l < ARRAYSIZE(SaferBuiltinLevels); l++)
        {
            _snwprintf(Name, ARRAYSIZE(Name), L"%lu\\%s", SaferBuiltinLevels[l].LevelId, Subkey);
            Name[ARRAYSIZE(Name) - 1] = UNICODE_NULL;

            if (SaferOpenPolicyKey(Scopes[s], Name, KEY_ENUMERATE_SUB_KEYS, &TypeKey) != ERROR_SUCCESS)
                continue;

            for (Index = 0; ; Index++)
            {
                NameLength = ARRAYSIZE(GuidName);
                if (RegEnumKeyExW(TypeKey, Index, GuidName, &NameLength, NULL, NULL, NULL, NULL) != ERROR_SUCCESS)
                    break;

                if (!SaferParseGuid(GuidName, &Guid))
                    continue;

                if (RegOpenKeyExW(TypeKey, GuidName, 0, KEY_QUERY_VALUE, &RuleKey) != ERROR_SUCCESS)
                    continue;

                Callback(Scopes[s], SaferBuiltinLevels[l].LevelId, RuleKey, &Guid, Context);
                RegCloseKey(RuleKey);
            }

            RegCloseKey(TypeKey);
        }
    }
}

static VOID
SaferSetMatch(PSAFER_MATCH Match, DWORD IdentityType, DWORD ScopeId, DWORD LevelId,
              HKEY RuleKey, const GUID *Guid, DWORD Rank, PCWSTR RulePath)
{
    DWORD Flags = 0;

    SaferReadDword(RuleKey, L"SaferFlags", &Flags);
    SaferFree(Match->RulePath);
    Match->Found = TRUE;
    Match->IdentityType = IdentityType;
    Match->ScopeId = ScopeId;
    Match->LevelId = LevelId;
    Match->SaferFlags = Flags;
    Match->Rank = Rank;
    Match->Guid = *Guid;
    Match->RulePath = SaferDuplicateString(RulePath);
}

typedef struct _SAFER_PATH_CONTEXT
{
    PCWSTR Path;
    SAFER_MATCH Match;
} SAFER_PATH_CONTEXT, *PSAFER_PATH_CONTEXT;

static VOID
SaferPathRuleCallback(DWORD ScopeId, DWORD LevelId, HKEY RuleKey, const GUID *Guid, PVOID Context)
{
    PSAFER_PATH_CONTEXT PathContext = Context;
    PWSTR ItemData, Expanded;
    DWORD Rank;

    ItemData = SaferReadString(RuleKey, L"ItemData");
    if (!ItemData)
        return;

    Expanded = SaferExpandPathRule(ItemData);
    if (Expanded)
    {
        Rank = SaferPathRuleRank(Expanded, PathContext->Path);
        if (Rank &&
            (!PathContext->Match.Found || Rank > PathContext->Match.Rank ||
             (Rank == PathContext->Match.Rank && LevelId < PathContext->Match.LevelId)))
        {
            SaferSetMatch(&PathContext->Match, SaferIdentityTypeImageName, ScopeId, LevelId,
                          RuleKey, Guid, Rank, ItemData);
        }
        SaferFree(Expanded);
    }
    SaferFree(ItemData);
}

static VOID
SaferImageLoadData(PSAFER_IMAGE Image)
{
    PSAFER_CODE_PROPERTIES Properties = Image->Properties;
    HANDLE File = NULL;
    LARGE_INTEGER Size;

    if (Image->DataLoaded)
        return;
    Image->DataLoaded = TRUE;

    if (Properties->pByteBlock && Properties->ImageSize.QuadPart > 0)
    {
        Image->Data = Properties->pByteBlock;
        Image->Size = Properties->ImageSize.QuadPart;
        Image->HaveData = TRUE;
        return;
    }

    if (Properties->hImageFileHandle && Properties->hImageFileHandle != INVALID_HANDLE_VALUE)
    {
        File = Properties->hImageFileHandle;
    }
    else if (Image->Path)
    {
        Image->File = CreateFileW(Image->Path, GENERIC_READ,
                                  FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                  NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
        if (Image->File == INVALID_HANDLE_VALUE)
            Image->File = NULL;
        File = Image->File;
    }

    if (!File || !GetFileSizeEx(File, &Size))
        return;

    Image->Size = Size.QuadPart;
    if (Image->Size == 0)
    {
        Image->HaveData = TRUE;
        return;
    }

    if (Image->Size > (ULONGLONG)(SIZE_T)-1)
        return;

    Image->Mapping = CreateFileMappingW(File, NULL, PAGE_READONLY, 0, 0, NULL);
    if (!Image->Mapping)
        return;

    Image->View = MapViewOfFile(Image->Mapping, FILE_MAP_READ, 0, 0, 0);
    if (!Image->View)
        return;

    Image->Data = Image->View;
    Image->HaveData = TRUE;
}

static VOID
SaferImageRelease(PSAFER_IMAGE Image)
{
    if (Image->View)
        UnmapViewOfFile(Image->View);
    if (Image->Mapping)
        CloseHandle(Image->Mapping);
    if (Image->File)
        CloseHandle(Image->File);
    SaferFree(Image->Path);
}

static BOOL
SaferHashBytes(ALG_ID Algorithm, const BYTE *Data, ULONGLONG Size, PBYTE Hash, PDWORD HashSize)
{
    HCRYPTPROV Provider;
    HCRYPTHASH Handle;
    ULONGLONG Done = 0;
    DWORD Chunk, Length = SAFER_MAX_HASH_SIZE;
    BOOL Ok = FALSE;

    if (!CryptAcquireContextW(&Provider, NULL, NULL, PROV_RSA_AES, CRYPT_VERIFYCONTEXT | CRYPT_SILENT))
        return FALSE;

    if (CryptCreateHash(Provider, Algorithm, 0, 0, &Handle))
    {
        Ok = TRUE;
        _SEH2_TRY
        {
            while (Ok && Done < Size)
            {
                Chunk = (DWORD)min(Size - Done, 0x10000000);
                Ok = CryptHashData(Handle, Data + Done, Chunk, 0);
                Done += Chunk;
            }
        }
        _SEH2_EXCEPT(_SEH2_GetExceptionCode() == EXCEPTION_IN_PAGE_ERROR ?
                     EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH)
        {
            Ok = FALSE;
        }
        _SEH2_END;

        if (Ok)
            Ok = CryptGetHashParam(Handle, HP_HASHVAL, Hash, &Length, 0);
        CryptDestroyHash(Handle);
    }

    CryptReleaseContext(Provider, 0);
    if (Ok)
        *HashSize = Length;
    return Ok;
}

static BOOL
SaferIsHashAlgorithm(ALG_ID Algorithm)
{
    return Algorithm != 0 && GET_ALG_CLASS(Algorithm) == ALG_CLASS_HASH;
}

/* FIXME: images carrying an Authenticode signature are hashed as flat files, not by their signed hash */
static BOOL
SaferImageHashMatches(PSAFER_IMAGE Image, ALG_ID Algorithm, const BYTE *RuleHash,
                      DWORD RuleHashSize, ULONGLONG RuleSize)
{
    PSAFER_CODE_PROPERTIES Properties = Image->Properties;
    PSAFER_HASH_ENTRY Entry = NULL;
    ULONG i;

    if (!SaferIsHashAlgorithm(Algorithm) || RuleHashSize == 0 || RuleHashSize > SAFER_MAX_HASH_SIZE)
        return FALSE;

    if (Properties->dwImageHashSize && Properties->dwImageHashSize <= SAFER_MAX_HASH_SIZE &&
        Properties->ImageSize.QuadPart > 0 && Properties->HashAlgorithm == Algorithm)
    {
        if (RuleSize && RuleSize != (ULONGLONG)Properties->ImageSize.QuadPart)
            return FALSE;
        return Properties->dwImageHashSize == RuleHashSize &&
               !memcmp(Properties->ImageHash, RuleHash, RuleHashSize);
    }

    for (i = 0; i < SAFER_HASH_CACHE; i++)
    {
        if (Image->Hashes[i].Algorithm == Algorithm)
        {
            Entry = &Image->Hashes[i];
            break;
        }
        if (Image->Hashes[i].Algorithm == 0)
        {
            Entry = &Image->Hashes[i];
            Entry->Algorithm = Algorithm;
            SaferImageLoadData(Image);
            if (Image->HaveData)
                Entry->Valid = SaferHashBytes(Algorithm, Image->Data, Image->Size, Entry->Hash, &Entry->Size);
            break;
        }
    }

    if (!Entry || !Entry->Valid)
        return FALSE;
    if (RuleSize && RuleSize != Image->Size)
        return FALSE;
    return Entry->Size == RuleHashSize && !memcmp(Entry->Hash, RuleHash, RuleHashSize);
}

static BOOL
SaferReadRuleHash(HKEY RuleKey, PBYTE Hash, PDWORD HashSize, ALG_ID *Algorithm, PULONGLONG ImageSize)
{
    DWORD Type, Size = SAFER_MAX_HASH_SIZE, Alg;

    if (RegQueryValueExW(RuleKey, L"ItemData", NULL, &Type, Hash, &Size) != ERROR_SUCCESS ||
        Type != REG_BINARY || Size == 0)
    {
        return FALSE;
    }

    if (!SaferReadDword(RuleKey, L"HashAlg", &Alg))
        return FALSE;

    *ImageSize = 0;
    SaferReadQword(RuleKey, L"ItemSize", ImageSize);
    *HashSize = Size;
    *Algorithm = Alg;
    return TRUE;
}

typedef struct _SAFER_HASH_CONTEXT
{
    PSAFER_IMAGE Image;
    SAFER_MATCH Match;
} SAFER_HASH_CONTEXT, *PSAFER_HASH_CONTEXT;

static VOID
SaferHashRuleCallback(DWORD ScopeId, DWORD LevelId, HKEY RuleKey, const GUID *Guid, PVOID Context)
{
    PSAFER_HASH_CONTEXT HashContext = Context;
    BYTE Hash[SAFER_MAX_HASH_SIZE];
    ULONGLONG ImageSize;
    DWORD HashSize;
    ALG_ID Algorithm;

    if (HashContext->Match.Found && LevelId >= HashContext->Match.LevelId)
        return;

    if (!SaferReadRuleHash(RuleKey, Hash, &HashSize, &Algorithm, &ImageSize))
        return;

    if (SaferImageHashMatches(HashContext->Image, Algorithm, Hash, HashSize, ImageSize))
        SaferSetMatch(&HashContext->Match, SaferIdentityTypeImageHash, ScopeId, LevelId, RuleKey, Guid, 0, NULL);
}

typedef struct _SAFER_ZONE_CONTEXT
{
    DWORD ZoneId;
    SAFER_MATCH Match;
} SAFER_ZONE_CONTEXT, *PSAFER_ZONE_CONTEXT;

static VOID
SaferZoneRuleCallback(DWORD ScopeId, DWORD LevelId, HKEY RuleKey, const GUID *Guid, PVOID Context)
{
    PSAFER_ZONE_CONTEXT ZoneContext = Context;
    DWORD Zone;

    if (ZoneContext->Match.Found && LevelId >= ZoneContext->Match.LevelId)
        return;

    if (SaferReadDword(RuleKey, L"ItemData", &Zone) && Zone == ZoneContext->ZoneId)
        SaferSetMatch(&ZoneContext->Match, SaferIdentityTypeUrlZone, ScopeId, LevelId, RuleKey, Guid, 0, NULL);
}

static VOID
SaferTakeMatch(PSAFER_MATCH Target, PSAFER_MATCH Source)
{
    SaferFree(Target->RulePath);
    *Target = *Source;
    Source->RulePath = NULL;
    Source->Found = FALSE;
}

static VOID
SaferEvaluateProperties(PSAFER_CODE_PROPERTIES Properties, DWORD DefaultLevel, PSAFER_MATCH Result)
{
    DWORD Criteria = Properties->dwCheckFlags;
    SAFER_IMAGE Image;
    SAFER_HASH_CONTEXT HashContext;
    SAFER_PATH_CONTEXT PathContext;
    SAFER_ZONE_CONTEXT ZoneContext;

    ZeroMemory(Result, sizeof(*Result));
    Result->IdentityType = SaferIdentityDefault;
    Result->LevelId = DefaultLevel;

    if (Criteria == 0)
        return;

    ZeroMemory(&Image, sizeof(Image));
    Image.Properties = Properties;
    Image.Path = SaferImagePathFromProperties(Properties);

    if (Criteria & (SAFER_CRITERIA_IMAGEHASH | SAFER_CRITERIA_NOSIGNEDHASH))
    {
        ZeroMemory(&HashContext, sizeof(HashContext));
        HashContext.Image = &Image;
        SaferEnumerateRules(SaferIdentityTypeImageHash, SaferHashRuleCallback, &HashContext);
        if (HashContext.Match.Found)
        {
            SaferTakeMatch(Result, &HashContext.Match);
            SaferImageRelease(&Image);
            return;
        }
    }

    /* TODO: certificate rules (AuthenticodeEnabled, SAFER_CRITERIA_AUTHENTICODE) are not evaluated */

    if ((Criteria & (SAFER_CRITERIA_IMAGEPATH | SAFER_CRITERIA_IMAGEPATH_NT)) && Image.Path)
    {
        ZeroMemory(&PathContext, sizeof(PathContext));
        PathContext.Path = Image.Path;
        SaferEnumerateRules(SaferIdentityTypeImageName, SaferPathRuleCallback, &PathContext);
        if (PathContext.Match.Found)
        {
            SaferTakeMatch(Result, &PathContext.Match);
            SaferImageRelease(&Image);
            return;
        }
    }

    if (Criteria & SAFER_CRITERIA_URLZONE)
    {
        ZeroMemory(&ZoneContext, sizeof(ZoneContext));
        ZoneContext.ZoneId = Properties->UrlZoneId;
        SaferEnumerateRules(SaferIdentityTypeUrlZone, SaferZoneRuleCallback, &ZoneContext);
        if (ZoneContext.Match.Found)
            SaferTakeMatch(Result, &ZoneContext.Match);
    }

    SaferImageRelease(&Image);
}

static PCWSTR
SaferRuleLogName(DWORD IdentityType)
{
    ULONG i;

    for (i = 0; i < ARRAYSIZE(SaferRuleKinds); i++)
    {
        if (SaferRuleKinds[i].IdentityType == IdentityType)
            return SaferRuleKinds[i].LogName;
    }
    return (IdentityType == SaferIdentityTypeCertificate) ? L"certificate" : L"default";
}

static VOID
SaferWriteLogEntry(PCWSTR ImagePath, const SAFER_MATCH *Match)
{
    WCHAR Caller[MAX_PATH], Guid[SAFER_GUID_CHARS];
    PWSTR LogFile = NULL, Line;
    PCWSTR CallerName;
    PSTR Ansi;
    HKEY Key;
    HANDLE File;
    SIZE_T Length;
    int AnsiLength;
    DWORD Written;

    if (SaferOpenPolicyKey(SAFER_SCOPEID_MACHINE, NULL, KEY_QUERY_VALUE, &Key) != ERROR_SUCCESS)
        return;
    LogFile = SaferReadString(Key, L"LogFileName");
    RegCloseKey(Key);
    if (!LogFile || !LogFile[0])
    {
        SaferFree(LogFile);
        return;
    }

    Caller[0] = UNICODE_NULL;
    GetModuleFileNameW(NULL, Caller, ARRAYSIZE(Caller));
    Caller[ARRAYSIZE(Caller) - 1] = UNICODE_NULL;
    CallerName = wcsrchr(Caller, L'\\');
    CallerName = CallerName ? CallerName + 1 : Caller;

    SaferFormatGuid(&Match->Guid, Guid);
    if (!ImagePath)
        ImagePath = L"";

    Length = wcslen(CallerName) + wcslen(ImagePath) + 160;
    Line = SaferAlloc(Length * sizeof(WCHAR));
    if (Line)
    {
        _snwprintf(Line, Length - 1, L"%s (PID = %lu) identified %s as %s using %s rule, Guid = %s\r\n",
                   CallerName, GetCurrentProcessId(), ImagePath,
                   SaferFindBuiltinLevel(Match->LevelId)->FriendlyName,
                   SaferRuleLogName(Match->IdentityType), Guid);

        AnsiLength = WideCharToMultiByte(CP_ACP, 0, Line, -1, NULL, 0, NULL, NULL);
        Ansi = (AnsiLength > 1) ? SaferAlloc(AnsiLength) : NULL;
        if (Ansi && WideCharToMultiByte(CP_ACP, 0, Line, -1, Ansi, AnsiLength, NULL, NULL))
        {
            File = CreateFileW(LogFile, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE,
                               NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
            if (File != INVALID_HANDLE_VALUE)
            {
                WriteFile(File, Ansi, AnsiLength - 1, &Written, NULL);
                CloseHandle(File);
            }
        }
        SaferFree(Ansi);
        SaferFree(Line);
    }

    SaferFree(LogFile);
}

static PSAFER_LEVEL_OBJECT
SaferInsertLevel(DWORD ScopeId, PSAFER_MATCH Match)
{
    PSAFER_LEVEL_OBJECT Level = SaferAlloc(sizeof(*Level));

    if (!Level)
        return NULL;

    Level->ScopeId = ScopeId;
    Level->LevelId = Match->LevelId;
    Level->IdentityType = Match->IdentityType;
    Level->SaferFlags = Match->SaferFlags;
    Level->RuleGuid = Match->Guid;
    Level->RulePath = Match->RulePath;
    Match->RulePath = NULL;

    AcquireSRWLockExclusive(&SaferLevelLock);
    InsertTailList(&SaferLevelList, &Level->Link);
    ReleaseSRWLockExclusive(&SaferLevelLock);
    return Level;
}

static PSAFER_LEVEL_OBJECT
SaferLookupLevelLocked(SAFER_LEVEL_HANDLE Handle)
{
    PLIST_ENTRY Entry;

    for (Entry = SaferLevelList.Flink; Entry != &SaferLevelList; Entry = Entry->Flink)
    {
        if ((SAFER_LEVEL_HANDLE)CONTAINING_RECORD(Entry, SAFER_LEVEL_OBJECT, Link) == Handle)
            return CONTAINING_RECORD(Entry, SAFER_LEVEL_OBJECT, Link);
    }
    return NULL;
}

static BOOL
SaferCopyLevel(SAFER_LEVEL_HANDLE Handle, PSAFER_LEVEL_OBJECT Copy, BOOL WithRulePath)
{
    PSAFER_LEVEL_OBJECT Level;

    AcquireSRWLockShared(&SaferLevelLock);
    Level = SaferLookupLevelLocked(Handle);
    if (Level)
    {
        *Copy = *Level;
        Copy->RulePath = WithRulePath ? SaferDuplicateString(Level->RulePath) : NULL;
    }
    ReleaseSRWLockShared(&SaferLevelLock);

    if (!Level)
    {
        SetLastError(ERROR_INVALID_HANDLE);
        return FALSE;
    }
    return TRUE;
}

static HANDLE
SaferOpenCallerToken(VOID)
{
    HANDLE Token;

    if (OpenThreadToken(GetCurrentThread(), TOKEN_QUERY | TOKEN_DUPLICATE, TRUE, &Token))
        return Token;
    if (GetLastError() != ERROR_NO_TOKEN)
        return NULL;
    if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY | TOKEN_DUPLICATE, &Token))
        return Token;
    return NULL;
}

static PVOID
SaferQueryToken(HANDLE Token, TOKEN_INFORMATION_CLASS Class)
{
    DWORD Size = 0;
    PVOID Buffer;

    if (GetTokenInformation(Token, Class, NULL, 0, &Size) || GetLastError() != ERROR_INSUFFICIENT_BUFFER)
        return NULL;

    Buffer = SaferAlloc(Size);
    if (Buffer && !GetTokenInformation(Token, Class, Buffer, Size, &Size))
    {
        SaferFree(Buffer);
        return NULL;
    }
    return Buffer;
}

static BOOL
SaferSidInList(PSID Sid, const SID_AND_ATTRIBUTES *List, DWORD Count)
{
    DWORD i;

    for (i = 0; i < Count; i++)
    {
        if (EqualSid(Sid, List[i].Sid))
            return TRUE;
    }
    return FALSE;
}

static VOID
SaferAppendSid(PSID_AND_ATTRIBUTES List, PDWORD Count, PSID Sid)
{
    List[*Count].Sid = Sid;
    List[*Count].Attributes = 0;
    (*Count)++;
}

static VOID
SaferFreePlan(PSAFER_TOKEN_PLAN Plan)
{
    SaferFree(Plan->Disable);
    SaferFree(Plan->Inverted);
    SaferFree(Plan->Added);
    SaferFree(Plan->Restrict);
    ZeroMemory(Plan, sizeof(*Plan));
}

static BOOL
SaferBuildPlan(DWORD LevelId, const TOKEN_GROUPS *Groups, PSAFER_TOKEN_PLAN Plan)
{
    DWORD GroupCount = Groups ? Groups->GroupCount : 0;
    DWORD Capacity = GroupCount + SaferSidCount;
    DWORD i, Attributes;
    PSID Sid;

    ZeroMemory(Plan, sizeof(*Plan));
    for (i = 0; i < SaferSidCount; i++)
    {
        if (!SaferCreateWellKnown(SaferWellKnownSidTypes[i], Plan->WellKnown[i]))
            return FALSE;
    }

    Plan->Disable = SaferAlloc(Capacity * sizeof(SID_AND_ATTRIBUTES));
    Plan->Inverted = SaferAlloc(Capacity * sizeof(SID_AND_ATTRIBUTES));
    Plan->Added = SaferAlloc(Capacity * sizeof(SID_AND_ATTRIBUTES));
    Plan->Restrict = SaferAlloc(Capacity * sizeof(SID_AND_ATTRIBUTES));
    if (!Plan->Disable || !Plan->Inverted || !Plan->Added || !Plan->Restrict)
    {
        SaferFreePlan(Plan);
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return FALSE;
    }

    switch (LevelId)
    {
        case SAFER_LEVELID_NORMALUSER:
            Plan->DisableMaxPrivilege = TRUE;
            SaferAppendSid(Plan->Disable, &Plan->DisableCount, Plan->WellKnown[SaferSidAdministrators]);
            SaferAppendSid(Plan->Disable, &Plan->DisableCount, Plan->WellKnown[SaferSidPowerUsers]);
            break;

        case SAFER_LEVELID_CONSTRAINED:
            Plan->DisableMaxPrivilege = TRUE;
            SaferAppendSid(Plan->Disable, &Plan->DisableCount, Plan->WellKnown[SaferSidAdministrators]);
            SaferAppendSid(Plan->Disable, &Plan->DisableCount, Plan->WellKnown[SaferSidPowerUsers]);
            SaferAppendSid(Plan->Inverted, &Plan->InvertedCount, Plan->WellKnown[SaferSidAdministrators]);
            SaferAppendSid(Plan->Inverted, &Plan->InvertedCount, Plan->WellKnown[SaferSidPowerUsers]);
            SaferAppendSid(Plan->Added, &Plan->AddedCount, Plan->WellKnown[SaferSidRestricted]);

            for (i = 0; i < GroupCount; i++)
            {
                Sid = Groups->Groups[i].Sid;
                Attributes = Groups->Groups[i].Attributes;
                if (!(Attributes & SE_GROUP_ENABLED) ||
                    (Attributes & (SE_GROUP_USE_FOR_DENY_ONLY | SE_GROUP_INTEGRITY)) ||
                    SaferSidInList(Sid, Plan->Inverted, Plan->InvertedCount) ||
                    SaferSidInList(Sid, Plan->Restrict, Plan->RestrictCount))
                {
                    continue;
                }
                SaferAppendSid(Plan->Restrict, &Plan->RestrictCount, Sid);
            }
            for (i = 0; i < Plan->AddedCount; i++)
            {
                if (!SaferSidInList(Plan->Added[i].Sid, Plan->Restrict, Plan->RestrictCount))
                    SaferAppendSid(Plan->Restrict, &Plan->RestrictCount, Plan->Added[i].Sid);
            }
            break;

        case SAFER_LEVELID_UNTRUSTED:
            Plan->DisableMaxPrivilege = TRUE;
            SaferAppendSid(Plan->Added, &Plan->AddedCount, Plan->WellKnown[SaferSidRestricted]);
            SaferAppendSid(Plan->Added, &Plan->AddedCount, Plan->WellKnown[SaferSidWorld]);
            SaferAppendSid(Plan->Added, &Plan->AddedCount, Plan->WellKnown[SaferSidInteractive]);
            SaferAppendSid(Plan->Added, &Plan->AddedCount, Plan->WellKnown[SaferSidAuthenticatedUsers]);
            SaferAppendSid(Plan->Added, &Plan->AddedCount, Plan->WellKnown[SaferSidUsers]);

            for (i = 0; i < GroupCount; i++)
            {
                Sid = Groups->Groups[i].Sid;
                if ((Groups->Groups[i].Attributes & SE_GROUP_INTEGRITY) ||
                    SaferSidInList(Sid, Plan->Added, Plan->AddedCount) ||
                    SaferSidInList(Sid, Plan->Disable, Plan->DisableCount))
                {
                    continue;
                }
                SaferAppendSid(Plan->Disable, &Plan->DisableCount, Sid);
            }
            for (i = 0; i < Plan->AddedCount; i++)
                SaferAppendSid(Plan->Restrict, &Plan->RestrictCount, Plan->Added[i].Sid);
            break;

        default:
            break;
    }

    return TRUE;
}

static BOOL
SaferPlanRestricts(const SAFER_TOKEN_PLAN *Plan, const TOKEN_GROUPS *Groups, const TOKEN_PRIVILEGES *Privileges)
{
    DWORD i;

    if (Plan->RestrictCount)
        return TRUE;

    if (Plan->DisableMaxPrivilege && Privileges)
    {
        for (i = 0; i < Privileges->PrivilegeCount; i++)
        {
            if (Privileges->Privileges[i].Luid.LowPart != SE_CHANGE_NOTIFY_PRIVILEGE ||
                Privileges->Privileges[i].Luid.HighPart != 0)
            {
                return TRUE;
            }
        }
    }

    if (Groups)
    {
        for (i = 0; i < Groups->GroupCount; i++)
        {
            if (!(Groups->Groups[i].Attributes & SE_GROUP_USE_FOR_DENY_ONLY) &&
                SaferSidInList(Groups->Groups[i].Sid, Plan->Disable, Plan->DisableCount))
            {
                return TRUE;
            }
        }
    }

    return FALSE;
}

static BOOL
SaferAdjustTokenDefaults(HANDLE Token, PSID UserSid, PSAFER_TOKEN_PLAN Plan)
{
    PTOKEN_DEFAULT_DACL Current;
    TOKEN_DEFAULT_DACL Replacement;
    TOKEN_OWNER Owner;
    ACL_SIZE_INFORMATION SizeInfo;
    PACL NewDacl = NULL;
    PACCESS_ALLOWED_ACE Ace;
    DWORD Size, i;
    BOOL Ok;

    Owner.Owner = UserSid;
    if (!SetTokenInformation(Token, TokenOwner, &Owner, sizeof(Owner)))
        return FALSE;

    Current = SaferQueryToken(Token, TokenDefaultDacl);
    if (!Current)
        return FALSE;

    if (!Current->DefaultDacl ||
        !GetAclInformation(Current->DefaultDacl, &SizeInfo, sizeof(SizeInfo), AclSizeInformation))
    {
        SaferFree(Current);
        return TRUE;
    }

    Size = SizeInfo.AclBytesInUse + (SizeInfo.AceCount + 1) * (sizeof(ACCESS_ALLOWED_ACE) + SECURITY_MAX_SID_SIZE);
    NewDacl = SaferAlloc(Size);
    Ok = NewDacl && InitializeAcl(NewDacl, Size, ACL_REVISION);

    for (i = 0; Ok && i < SizeInfo.AceCount; i++)
    {
        Ok = GetAce(Current->DefaultDacl, i, (PVOID *)&Ace);
        if (!Ok)
            break;

        if (Ace->Header.AceType == ACCESS_ALLOWED_ACE_TYPE &&
            SaferSidInList(&Ace->SidStart, Plan->Disable, Plan->DisableCount))
        {
            Ok = AddAccessAllowedAceEx(NewDacl, ACL_REVISION, Ace->Header.AceFlags, Ace->Mask, UserSid);
        }
        else
        {
            Ok = AddAce(NewDacl, ACL_REVISION, MAXDWORD, Ace, Ace->Header.AceSize);
        }
    }

    if (Ok && Plan->RestrictCount)
        Ok = AddAccessAllowedAce(NewDacl, ACL_REVISION, GENERIC_ALL, Plan->WellKnown[SaferSidRestricted]);

    if (Ok)
    {
        Replacement.DefaultDacl = NewDacl;
        Ok = SetTokenInformation(Token, TokenDefaultDacl, &Replacement, sizeof(Replacement));
    }

    SaferFree(NewDacl);
    SaferFree(Current);
    return Ok;
}

static BOOL
SaferWriteOutput(const VOID *Data, DWORD Size, PVOID Buffer, DWORD BufferSize, PDWORD OutSize)
{
    *OutSize = Size;
    if (!Buffer || BufferSize < Size)
    {
        SetLastError(ERROR_INSUFFICIENT_BUFFER);
        return FALSE;
    }

    CopyMemory(Buffer, Data, Size);
    return TRUE;
}

static BOOL
SaferWriteGroups(const SID_AND_ATTRIBUTES *Sids, DWORD Count, PVOID Buffer, DWORD BufferSize, PDWORD OutSize)
{
    PTOKEN_GROUPS Groups = Buffer;
    DWORD Size, i, Length;
    PBYTE Next;

    Size = FIELD_OFFSET(TOKEN_GROUPS, Groups) + Count * sizeof(SID_AND_ATTRIBUTES);
    for (i = 0; i < Count; i++)
        Size += GetLengthSid(Sids[i].Sid);

    *OutSize = Size;
    if (!Buffer || BufferSize < Size)
    {
        SetLastError(ERROR_INSUFFICIENT_BUFFER);
        return FALSE;
    }

    Groups->GroupCount = Count;
    Next = (PBYTE)Buffer + FIELD_OFFSET(TOKEN_GROUPS, Groups) + Count * sizeof(SID_AND_ATTRIBUTES);
    for (i = 0; i < Count; i++)
    {
        Length = GetLengthSid(Sids[i].Sid);
        CopySid(Length, Next, Sids[i].Sid);
        Groups->Groups[i].Sid = Next;
        Groups->Groups[i].Attributes = Sids[i].Attributes;
        Next += Length;
    }
    return TRUE;
}

static BOOL
SaferWriteOwner(PSID Sid, PVOID Buffer, DWORD BufferSize, PDWORD OutSize)
{
    PTOKEN_OWNER Owner = Buffer;
    DWORD Length = GetLengthSid(Sid);

    *OutSize = sizeof(TOKEN_OWNER) + Length;
    if (!Buffer || BufferSize < *OutSize)
    {
        SetLastError(ERROR_INSUFFICIENT_BUFFER);
        return FALSE;
    }

    Owner->Owner = (PSID)(Owner + 1);
    return CopySid(Length, Owner->Owner, Sid);
}

static BOOL
SaferRuleKey(DWORD ScopeId, DWORD LevelId, DWORD IdentityType, const GUID *Guid, BOOL Create, PHKEY Key)
{
    WCHAR Name[MAX_PATH], GuidText[SAFER_GUID_CHARS];
    PCWSTR Subkey = NULL;
    LSTATUS Status;
    ULONG i;

    for (i = 0; i < ARRAYSIZE(SaferRuleKinds); i++)
    {
        if (SaferRuleKinds[i].IdentityType == IdentityType)
            Subkey = SaferRuleKinds[i].Subkey;
    }
    if (!Subkey)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    SaferFormatGuid(Guid, GuidText);
    _snwprintf(Name, ARRAYSIZE(Name), L"%lu\\%s\\%s", LevelId, Subkey, GuidText);
    Name[ARRAYSIZE(Name) - 1] = UNICODE_NULL;

    if (Create)
        Status = SaferCreatePolicyKey(ScopeId, Name, Key);
    else
        Status = SaferOpenPolicyKey(ScopeId, Name, KEY_QUERY_VALUE, Key);

    if (Status != ERROR_SUCCESS)
    {
        SetLastError(Status);
        return FALSE;
    }
    return TRUE;
}

static VOID
SaferReadLastModified(HKEY Key, PFILETIME Time)
{
    ULONGLONG Value = 0;

    SaferReadQword(Key, L"LastModified", &Value);
    Time->dwLowDateTime = (DWORD)Value;
    Time->dwHighDateTime = (DWORD)(Value >> 32);
}

static VOID
SaferReadFixedString(HKEY Key, PCWSTR Name, PWSTR Buffer, SIZE_T Count)
{
    PWSTR Value = SaferReadString(Key, Name);

    Buffer[0] = UNICODE_NULL;
    if (Value)
    {
        wcsncpy(Buffer, Value, Count - 1);
        Buffer[Count - 1] = UNICODE_NULL;
        SaferFree(Value);
    }
}

static BOOL
SaferGetSingleIdentification(PSAFER_LEVEL_OBJECT Level, PVOID Buffer, DWORD BufferSize, PDWORD OutSize)
{
    PSAFER_IDENTIFICATION_HEADER Header = Buffer;
    PSAFER_PATHNAME_IDENTIFICATION PathRule;
    PSAFER_HASH_IDENTIFICATION HashRule;
    PSAFER_URLZONE_IDENTIFICATION ZoneRule;
    GUID Guid;
    HKEY Key;
    PWSTR ItemData;
    DWORD Size, Value;
    ULONGLONG ImageSize;
    ALG_ID Algorithm;
    BYTE Hash[SAFER_MAX_HASH_SIZE];
    BOOL Ok;

    if (!Buffer || BufferSize < sizeof(SAFER_IDENTIFICATION_HEADER))
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    Guid = Header->IdentificationGuid;

    if (SaferRuleKey(Level->ScopeId, Level->LevelId, SaferIdentityTypeImageName, &Guid, FALSE, &Key))
    {
        ItemData = SaferReadString(Key, L"ItemData");
        if (!ItemData)
        {
            RegCloseKey(Key);
            SetLastError(ERROR_NOT_FOUND);
            return FALSE;
        }

        Size = sizeof(SAFER_PATHNAME_IDENTIFICATION) + (DWORD)((wcslen(ItemData) + 1) * sizeof(WCHAR));
        *OutSize = Size;
        Ok = BufferSize >= Size;
        if (Ok)
        {
            PathRule = Buffer;
            ZeroMemory(PathRule, sizeof(*PathRule));
            PathRule->header.dwIdentificationType = SaferIdentityTypeImageName;
            PathRule->header.cbStructSize = sizeof(SAFER_PATHNAME_IDENTIFICATION);
            PathRule->header.IdentificationGuid = Guid;
            SaferReadLastModified(Key, &PathRule->header.lastModified);
            SaferReadFixedString(Key, L"Description", PathRule->Description, SAFER_MAX_DESCRIPTION_SIZE);
            PathRule->ImageName = (PWCHAR)(PathRule + 1);
            wcscpy(PathRule->ImageName, ItemData);
            SaferReadDword(Key, L"SaferFlags", &PathRule->dwSaferFlags);
        }
        else
        {
            SetLastError(ERROR_INSUFFICIENT_BUFFER);
        }
        SaferFree(ItemData);
        RegCloseKey(Key);
        return Ok;
    }

    if (SaferRuleKey(Level->ScopeId, Level->LevelId, SaferIdentityTypeImageHash, &Guid, FALSE, &Key))
    {
        if (!SaferReadRuleHash(Key, Hash, &Size, &Algorithm, &ImageSize))
        {
            RegCloseKey(Key);
            SetLastError(ERROR_NOT_FOUND);
            return FALSE;
        }

        *OutSize = sizeof(SAFER_HASH_IDENTIFICATION);
        Ok = BufferSize >= sizeof(SAFER_HASH_IDENTIFICATION);
        if (Ok)
        {
            HashRule = Buffer;
            ZeroMemory(HashRule, sizeof(*HashRule));
            HashRule->header.dwIdentificationType = SaferIdentityTypeImageHash;
            HashRule->header.cbStructSize = sizeof(SAFER_HASH_IDENTIFICATION);
            HashRule->header.IdentificationGuid = Guid;
            SaferReadLastModified(Key, &HashRule->header.lastModified);
            SaferReadFixedString(Key, L"Description", HashRule->Description, SAFER_MAX_DESCRIPTION_SIZE);
            SaferReadFixedString(Key, L"FriendlyName", HashRule->FriendlyName, SAFER_MAX_FRIENDLYNAME_SIZE);
            HashRule->HashSize = Size;
            CopyMemory(HashRule->ImageHash, Hash, Size);
            HashRule->HashAlgorithm = Algorithm;
            HashRule->ImageSize.QuadPart = (LONGLONG)ImageSize;
            SaferReadDword(Key, L"SaferFlags", &HashRule->dwSaferFlags);
        }
        else
        {
            SetLastError(ERROR_INSUFFICIENT_BUFFER);
        }
        RegCloseKey(Key);
        return Ok;
    }

    if (SaferRuleKey(Level->ScopeId, Level->LevelId, SaferIdentityTypeUrlZone, &Guid, FALSE, &Key))
    {
        if (!SaferReadDword(Key, L"ItemData", &Value))
        {
            RegCloseKey(Key);
            SetLastError(ERROR_NOT_FOUND);
            return FALSE;
        }

        *OutSize = sizeof(SAFER_URLZONE_IDENTIFICATION);
        Ok = BufferSize >= sizeof(SAFER_URLZONE_IDENTIFICATION);
        if (Ok)
        {
            ZoneRule = Buffer;
            ZeroMemory(ZoneRule, sizeof(*ZoneRule));
            ZoneRule->header.dwIdentificationType = SaferIdentityTypeUrlZone;
            ZoneRule->header.cbStructSize = sizeof(SAFER_URLZONE_IDENTIFICATION);
            ZoneRule->header.IdentificationGuid = Guid;
            SaferReadLastModified(Key, &ZoneRule->header.lastModified);
            ZoneRule->UrlZoneId = Value;
            SaferReadDword(Key, L"SaferFlags", &ZoneRule->dwSaferFlags);
        }
        else
        {
            SetLastError(ERROR_INSUFFICIENT_BUFFER);
        }
        RegCloseKey(Key);
        return Ok;
    }

    SetLastError(ERROR_NOT_FOUND);
    return FALSE;
}

static VOID
SaferWriteLastModified(HKEY Key)
{
    FILETIME Now;
    ULONGLONG Value;

    GetSystemTimeAsFileTime(&Now);
    Value = ((ULONGLONG)Now.dwHighDateTime << 32) | Now.dwLowDateTime;
    RegSetValueExW(Key, L"LastModified", 0, REG_QWORD, (const BYTE *)&Value, sizeof(Value));
}

static LSTATUS
SaferWriteStringValue(HKEY Key, PCWSTR Name, PCWSTR Value, SIZE_T MaxCount, DWORD Type)
{
    SIZE_T Length = 0;

    while (Length < MaxCount && Value[Length])
        Length++;

    return RegSetValueExW(Key, Name, 0, Type, (const BYTE *)Value, (DWORD)(Length * sizeof(WCHAR)));
}

static BOOL
SaferDeleteRule(PSAFER_LEVEL_OBJECT Level, const GUID *Guid)
{
    WCHAR Name[MAX_PATH], GuidText[SAFER_GUID_CHARS];
    HKEY Root;
    BOOL Deleted = FALSE;
    LSTATUS Status, Failure = ERROR_NOT_FOUND;
    ULONG i;

    if (SaferOpenPolicyKey(Level->ScopeId, NULL, KEY_ALL_ACCESS, &Root) != ERROR_SUCCESS)
    {
        SetLastError(ERROR_NOT_FOUND);
        return FALSE;
    }

    SaferFormatGuid(Guid, GuidText);
    for (i = 0; i < ARRAYSIZE(SaferRuleKinds); i++)
    {
        _snwprintf(Name, ARRAYSIZE(Name), L"%lu\\%s\\%s", Level->LevelId, SaferRuleKinds[i].Subkey, GuidText);
        Name[ARRAYSIZE(Name) - 1] = UNICODE_NULL;
        Status = RegDeleteKeyExW(Root, Name, KEY_WOW64_64KEY, 0);
        if (Status == ERROR_SUCCESS)
            Deleted = TRUE;
        else if (Status != ERROR_FILE_NOT_FOUND)
            Failure = Status;
    }
    RegCloseKey(Root);

    if (!Deleted)
    {
        SetLastError(Failure);
        return FALSE;
    }
    return TRUE;
}

static BOOL
SaferSetSingleIdentification(PSAFER_LEVEL_OBJECT Level, PVOID Buffer, DWORD BufferSize)
{
    PSAFER_IDENTIFICATION_HEADER Header = Buffer;
    PSAFER_PATHNAME_IDENTIFICATION PathRule = Buffer;
    PSAFER_HASH_IDENTIFICATION HashRule = Buffer;
    PSAFER_URLZONE_IDENTIFICATION ZoneRule = Buffer;
    ULONGLONG ImageSize;
    LSTATUS Status;
    HKEY Key;
    DWORD Type;

    if (!Buffer || BufferSize < sizeof(SAFER_IDENTIFICATION_HEADER))
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    switch (Header->dwIdentificationType)
    {
        case SaferIdentityDefault:
            return SaferDeleteRule(Level, &Header->IdentificationGuid);

        case SaferIdentityTypeImageName:
            if (Header->cbStructSize != sizeof(SAFER_PATHNAME_IDENTIFICATION) ||
                BufferSize < sizeof(SAFER_PATHNAME_IDENTIFICATION) ||
                !PathRule->ImageName || !PathRule->ImageName[0])
            {
                SetLastError(ERROR_INVALID_PARAMETER);
                return FALSE;
            }
            if (!SaferRuleKey(Level->ScopeId, Level->LevelId, SaferIdentityTypeImageName,
                              &Header->IdentificationGuid, TRUE, &Key))
            {
                return FALSE;
            }
            Type = wcschr(PathRule->ImageName, L'%') ? REG_EXPAND_SZ : REG_SZ;
            Status = RegSetValueExW(Key, L"ItemData", 0, Type, (const BYTE *)PathRule->ImageName,
                                    (DWORD)((wcslen(PathRule->ImageName) + 1) * sizeof(WCHAR)));
            if (Status == ERROR_SUCCESS)
                Status = SaferWriteStringValue(Key, L"Description", PathRule->Description,
                                               SAFER_MAX_DESCRIPTION_SIZE, REG_SZ);
            if (Status == ERROR_SUCCESS)
                Status = RegSetValueExW(Key, L"SaferFlags", 0, REG_DWORD,
                                        (const BYTE *)&PathRule->dwSaferFlags, sizeof(DWORD));
            break;

        case SaferIdentityTypeImageHash:
            if ((Header->cbStructSize != sizeof(SAFER_HASH_IDENTIFICATION) &&
                 Header->cbStructSize != sizeof(SAFER_HASH_IDENTIFICATION2)) ||
                BufferSize < Header->cbStructSize ||
                HashRule->HashSize == 0 || HashRule->HashSize > SAFER_MAX_HASH_SIZE ||
                !SaferIsHashAlgorithm(HashRule->HashAlgorithm))
            {
                SetLastError(ERROR_INVALID_PARAMETER);
                return FALSE;
            }
            /* TODO: the second (SHA-256) hash of SAFER_HASH_IDENTIFICATION2 is not stored */
            if (!SaferRuleKey(Level->ScopeId, Level->LevelId, SaferIdentityTypeImageHash,
                              &Header->IdentificationGuid, TRUE, &Key))
            {
                return FALSE;
            }
            ImageSize = (ULONGLONG)HashRule->ImageSize.QuadPart;
            Status = RegSetValueExW(Key, L"ItemData", 0, REG_BINARY, HashRule->ImageHash, HashRule->HashSize);
            if (Status == ERROR_SUCCESS)
                Status = RegSetValueExW(Key, L"HashAlg", 0, REG_DWORD,
                                        (const BYTE *)&HashRule->HashAlgorithm, sizeof(DWORD));
            if (Status == ERROR_SUCCESS)
                Status = RegSetValueExW(Key, L"ItemSize", 0, REG_QWORD, (const BYTE *)&ImageSize, sizeof(ImageSize));
            if (Status == ERROR_SUCCESS)
                Status = SaferWriteStringValue(Key, L"FriendlyName", HashRule->FriendlyName,
                                               SAFER_MAX_FRIENDLYNAME_SIZE, REG_SZ);
            if (Status == ERROR_SUCCESS)
                Status = SaferWriteStringValue(Key, L"Description", HashRule->Description,
                                               SAFER_MAX_DESCRIPTION_SIZE, REG_SZ);
            if (Status == ERROR_SUCCESS)
                Status = RegSetValueExW(Key, L"SaferFlags", 0, REG_DWORD,
                                        (const BYTE *)&HashRule->dwSaferFlags, sizeof(DWORD));
            break;

        case SaferIdentityTypeUrlZone:
            if (Header->cbStructSize != sizeof(SAFER_URLZONE_IDENTIFICATION) ||
                BufferSize < sizeof(SAFER_URLZONE_IDENTIFICATION))
            {
                SetLastError(ERROR_INVALID_PARAMETER);
                return FALSE;
            }
            if (!SaferRuleKey(Level->ScopeId, Level->LevelId, SaferIdentityTypeUrlZone,
                              &Header->IdentificationGuid, TRUE, &Key))
            {
                return FALSE;
            }
            Status = RegSetValueExW(Key, L"ItemData", 0, REG_BINARY,
                                    (const BYTE *)&ZoneRule->UrlZoneId, sizeof(DWORD));
            if (Status == ERROR_SUCCESS)
                Status = RegSetValueExW(Key, L"SaferFlags", 0, REG_DWORD,
                                        (const BYTE *)&ZoneRule->dwSaferFlags, sizeof(DWORD));
            break;

        default:
            /* TODO: certificate rules live in the policy certificate stores, not under the level keys */
            SetLastError(ERROR_NOT_SUPPORTED);
            return FALSE;
    }

    if (Status == ERROR_SUCCESS)
        SaferWriteLastModified(Key);
    RegCloseKey(Key);

    if (Status != ERROR_SUCCESS)
    {
        SetLastError(Status);
        return FALSE;
    }
    return TRUE;
}

static PWSTR
SaferReadExecutableTypes(VOID)
{
    static const DWORD Scopes[] = { SAFER_SCOPEID_MACHINE, SAFER_SCOPEID_USER };
    PWSTR Types = NULL;
    HKEY Key;
    ULONG i;

    for (i = 0; i < ARRAYSIZE(Scopes) && !Types; i++)
    {
        if (SaferOpenPolicyKey(Scopes[i], NULL, KEY_QUERY_VALUE, &Key) != ERROR_SUCCESS)
            continue;
        Types = SaferReadMultiString(Key, L"ExecutableTypes");
        RegCloseKey(Key);
    }
    return Types;
}

static BOOL
SaferExtensionEquals(PCWSTR Entry, PCWSTR Extension)
{
    if (*Entry == L'.')
        Entry++;
    return *Entry && !_wcsicmp(Entry, Extension);
}

BOOL
WINAPI
SaferCreateLevel(
    _In_ DWORD dwScopeId,
    _In_ DWORD dwLevelId,
    _In_ DWORD OpenFlags,
    _Outptr_ SAFER_LEVEL_HANDLE *pLevelHandle,
    _Reserved_ PVOID pReserved)
{
    SAFER_MATCH Match;
    PSAFER_LEVEL_OBJECT Level;

    TRACE("(%lu, 0x%lx, %lu, %p, %p)\n", dwScopeId, dwLevelId, OpenFlags, pLevelHandle, pReserved);

    if (!pLevelHandle)
    {
        SetLastError(ERROR_NOACCESS);
        return FALSE;
    }

    if (!SaferIsValidScope(dwScopeId) || !SaferFindBuiltinLevel(dwLevelId) || (OpenFlags & ~SAFER_LEVEL_OPEN))
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    ZeroMemory(&Match, sizeof(Match));
    Match.IdentityType = SaferIdentityDefault;
    Match.LevelId = dwLevelId;

    Level = SaferInsertLevel(dwScopeId, &Match);
    if (!Level)
    {
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return FALSE;
    }

    *pLevelHandle = (SAFER_LEVEL_HANDLE)Level;
    return TRUE;
}

BOOL
WINAPI
SaferIdentifyLevel(
    _In_ DWORD dwNumProperties,
    _In_reads_opt_(dwNumProperties) PSAFER_CODE_PROPERTIES pCodeProperties,
    _Outptr_ SAFER_LEVEL_HANDLE *pLevelHandle,
    _Reserved_ PVOID pReserved)
{
    SAFER_SCOPE_POLICY Machine, User;
    PSAFER_CODE_PROPERTIES Properties;
    SAFER_MATCH Result, Candidate;
    PSAFER_LEVEL_OBJECT Level;
    PWSTR LoggedPath = NULL;
    SIZE_T Offset = 0;
    DWORD DefaultLevel, i = 0;

    TRACE("(%lu, %p, %p, %p)\n", dwNumProperties, pCodeProperties, pLevelHandle, pReserved);

    if (!pLevelHandle)
    {
        SetLastError(ERROR_NOACCESS);
        return FALSE;
    }

    do
    {
        Properties = (PSAFER_CODE_PROPERTIES)((PBYTE)pCodeProperties + Offset);
        if (Properties->cbSize != sizeof(SAFER_CODE_PROPERTIES_V1) &&
            Properties->cbSize != sizeof(SAFER_CODE_PROPERTIES_V2))
        {
            SetLastError(ERROR_INVALID_PARAMETER);
            return FALSE;
        }
        Offset += Properties->cbSize;
    } while (++i < dwNumProperties);

    SaferReadScopePolicy(SAFER_SCOPEID_MACHINE, &Machine);
    SaferReadScopePolicy(SAFER_SCOPEID_USER, &User);
    DefaultLevel = SaferEffectiveDefaultLevel(&Machine, &User);

    ZeroMemory(&Result, sizeof(Result));
    Result.IdentityType = SaferIdentityDefault;
    Result.LevelId = DefaultLevel;

    if (Machine.PolicyScope == 1 && SaferCallerIsAdministrator())
    {
        Result.LevelId = SAFER_LEVELID_FULLYTRUSTED;
    }
    else
    {
        Offset = 0;
        for (i = 0; i < dwNumProperties; i++)
        {
            Properties = (PSAFER_CODE_PROPERTIES)((PBYTE)pCodeProperties + Offset);
            Offset += Properties->cbSize;

            SaferEvaluateProperties(Properties, DefaultLevel, &Candidate);
            if (i == 0 || Candidate.LevelId < Result.LevelId)
            {
                SaferTakeMatch(&Result, &Candidate);
                SaferFree(LoggedPath);
                LoggedPath = Properties->ImagePath ? SaferDuplicateString(Properties->ImagePath) : NULL;
            }
            SaferFree(Candidate.RulePath);
        }
    }

    SaferWriteLogEntry(LoggedPath, &Result);
    SaferFree(LoggedPath);

    Level = SaferInsertLevel(Result.Found ? Result.ScopeId : SAFER_SCOPEID_MACHINE, &Result);
    SaferFree(Result.RulePath);
    if (!Level)
    {
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return FALSE;
    }

    *pLevelHandle = (SAFER_LEVEL_HANDLE)Level;
    return TRUE;
}

BOOL
WINAPI
SaferCloseLevel(
    _In_ SAFER_LEVEL_HANDLE hLevelHandle)
{
    PSAFER_LEVEL_OBJECT Level;

    TRACE("(%p)\n", hLevelHandle);

    AcquireSRWLockExclusive(&SaferLevelLock);
    Level = SaferLookupLevelLocked(hLevelHandle);
    if (Level)
        RemoveEntryList(&Level->Link);
    ReleaseSRWLockExclusive(&SaferLevelLock);

    if (!Level)
    {
        SetLastError(ERROR_INVALID_HANDLE);
        return FALSE;
    }

    SaferFree(Level->RulePath);
    SaferFree(Level);
    return TRUE;
}

BOOL
WINAPI
SaferGetLevelInformation(
    _In_ SAFER_LEVEL_HANDLE LevelHandle,
    _In_ SAFER_OBJECT_INFO_CLASS dwInfoType,
    _Out_writes_bytes_opt_(dwInBufferSize) PVOID pQueryBuffer,
    _In_ DWORD dwInBufferSize,
    _Out_ PDWORD pdwOutBufferSize)
{
    const SAFER_BUILTIN_LEVEL *Builtin;
    SAFER_LEVEL_OBJECT Level;
    SAFER_TOKEN_PLAN Plan;
    PTOKEN_GROUPS Groups = NULL;
    TOKEN_PRIVILEGES NoPrivileges;
    TOKEN_OWNER Owner;
    PTOKEN_USER CallerUser;
    HANDLE Token;
    PCWSTR Text;
    DWORD Value;
    BOOL Ok;

    TRACE("(%p, %d, %p, %lu, %p)\n", LevelHandle, dwInfoType, pQueryBuffer, dwInBufferSize, pdwOutBufferSize);

    if (!SaferCopyLevel(LevelHandle, &Level, FALSE))
        return FALSE;

    if (!pdwOutBufferSize)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    Builtin = SaferFindBuiltinLevel(Level.LevelId);

    switch (dwInfoType)
    {
        case SaferObjectLevelId:
            return SaferWriteOutput(&Level.LevelId, sizeof(DWORD), pQueryBuffer, dwInBufferSize, pdwOutBufferSize);

        case SaferObjectScopeId:
            return SaferWriteOutput(&Level.ScopeId, sizeof(DWORD), pQueryBuffer, dwInBufferSize, pdwOutBufferSize);

        case SaferObjectFriendlyName:
        case SaferObjectDescription:
            Text = (dwInfoType == SaferObjectFriendlyName) ? Builtin->FriendlyName : Builtin->Description;
            return SaferWriteOutput(Text, (DWORD)((wcslen(Text) + 1) * sizeof(WCHAR)),
                                    pQueryBuffer, dwInBufferSize, pdwOutBufferSize);

        case SaferObjectBuiltin:
            Value = TRUE;
            return SaferWriteOutput(&Value, sizeof(DWORD), pQueryBuffer, dwInBufferSize, pdwOutBufferSize);

        case SaferObjectDisallowed:
            Value = (Level.LevelId == SAFER_LEVELID_DISALLOWED);
            return SaferWriteOutput(&Value, sizeof(DWORD), pQueryBuffer, dwInBufferSize, pdwOutBufferSize);

        case SaferObjectInvertDeletedPrivileges:
            Value = FALSE;
            return SaferWriteOutput(&Value, sizeof(DWORD), pQueryBuffer, dwInBufferSize, pdwOutBufferSize);

        case SaferObjectExtendedError:
            return SaferWriteOutput(&Level.ExtendedError, sizeof(DWORD), pQueryBuffer, dwInBufferSize,
                                    pdwOutBufferSize);

        case SaferObjectDeletedPrivileges:
            ZeroMemory(&NoPrivileges, sizeof(NoPrivileges));
            return SaferWriteOutput(&NoPrivileges, FIELD_OFFSET(TOKEN_PRIVILEGES, Privileges),
                                    pQueryBuffer, dwInBufferSize, pdwOutBufferSize);

        case SaferObjectDefaultOwner:
            if (Level.LevelId == SAFER_LEVELID_FULLYTRUSTED || Level.LevelId == SAFER_LEVELID_DISALLOWED)
            {
                Owner.Owner = NULL;
                return SaferWriteOutput(&Owner, sizeof(Owner), pQueryBuffer, dwInBufferSize, pdwOutBufferSize);
            }
            Token = SaferOpenCallerToken();
            if (!Token)
                return FALSE;
            CallerUser = SaferQueryToken(Token, TokenUser);
            CloseHandle(Token);
            if (!CallerUser)
                return FALSE;
            Ok = SaferWriteOwner(CallerUser->User.Sid, pQueryBuffer, dwInBufferSize, pdwOutBufferSize);
            SaferFree(CallerUser);
            return Ok;

        case SaferObjectDisableMaxPrivilege:
        case SaferObjectSidsToDisable:
        case SaferObjectRestrictedSidsInverted:
        case SaferObjectRestrictedSidsAdded:
            if (dwInfoType == SaferObjectSidsToDisable && Level.LevelId == SAFER_LEVELID_UNTRUSTED)
            {
                Token = SaferOpenCallerToken();
                if (!Token)
                    return FALSE;
                Groups = SaferQueryToken(Token, TokenGroups);
                CloseHandle(Token);
                if (!Groups)
                    return FALSE;
            }

            Ok = SaferBuildPlan(Level.LevelId, Groups, &Plan);
            if (Ok)
            {
                switch (dwInfoType)
                {
                    case SaferObjectDisableMaxPrivilege:
                        Value = Plan.DisableMaxPrivilege;
                        Ok = SaferWriteOutput(&Value, sizeof(DWORD), pQueryBuffer, dwInBufferSize,
                                              pdwOutBufferSize);
                        break;
                    case SaferObjectSidsToDisable:
                        Ok = SaferWriteGroups(Plan.Disable, Plan.DisableCount, pQueryBuffer, dwInBufferSize,
                                              pdwOutBufferSize);
                        break;
                    case SaferObjectRestrictedSidsInverted:
                        Ok = SaferWriteGroups(Plan.Inverted, Plan.InvertedCount, pQueryBuffer, dwInBufferSize,
                                              pdwOutBufferSize);
                        break;
                    default:
                        Ok = SaferWriteGroups(Plan.Added, Plan.AddedCount, pQueryBuffer, dwInBufferSize,
                                              pdwOutBufferSize);
                        break;
                }
                SaferFreePlan(&Plan);
            }
            SaferFree(Groups);
            return Ok;

        case SaferObjectSingleIdentification:
            return SaferGetSingleIdentification(&Level, pQueryBuffer, dwInBufferSize, pdwOutBufferSize);

        case SaferObjectAllIdentificationGuids:
            /* TODO: SAFER_IDENTIFICATION_GUIDS has no published layout */
            SetLastError(ERROR_NOT_SUPPORTED);
            return FALSE;

        default:
            SetLastError(ERROR_INVALID_PARAMETER);
            return FALSE;
    }
}

BOOL
WINAPI
SaferSetLevelInformation(
    _In_ SAFER_LEVEL_HANDLE LevelHandle,
    _In_ SAFER_OBJECT_INFO_CLASS dwInfoType,
    _In_reads_bytes_(dwInBufferSize) PVOID pQueryBuffer,
    _In_ DWORD dwInBufferSize)
{
    SAFER_LEVEL_OBJECT Level;

    TRACE("(%p, %d, %p, %lu)\n", LevelHandle, dwInfoType, pQueryBuffer, dwInBufferSize);

    if (!SaferCopyLevel(LevelHandle, &Level, FALSE))
        return FALSE;

    switch (dwInfoType)
    {
        case SaferObjectSingleIdentification:
            return SaferSetSingleIdentification(&Level, pQueryBuffer, dwInBufferSize);

        case SaferObjectFriendlyName:
        case SaferObjectDescription:
            /* TODO: renaming the built-in levels has no documented store */
            SetLastError(ERROR_NOT_SUPPORTED);
            return FALSE;

        default:
            SetLastError(ERROR_INVALID_PARAMETER);
            return FALSE;
    }
}

BOOL
WINAPI
SaferGetPolicyInformation(
    _In_ DWORD dwScopeId,
    _In_ SAFER_POLICY_INFO_CLASS SaferPolicyInfoClass,
    _In_ DWORD InfoBufferSize,
    _Out_writes_bytes_opt_(InfoBufferSize) PVOID InfoBuffer,
    _Out_ PDWORD InfoBufferRetSize,
    _Reserved_ PVOID pReserved)
{
    SAFER_SCOPE_POLICY Policy;
    DWORD Levels[ARRAYSIZE(SaferBuiltinLevels)];
    DWORD Value;
    ULONG i;

    TRACE("(%lu, %d, %lu, %p, %p, %p)\n", dwScopeId, SaferPolicyInfoClass, InfoBufferSize, InfoBuffer,
          InfoBufferRetSize, pReserved);

    if (!SaferIsValidScope(dwScopeId) || !InfoBufferRetSize)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    SaferReadScopePolicy(dwScopeId, &Policy);

    switch (SaferPolicyInfoClass)
    {
        case SaferPolicyLevelList:
            for (i = 0; i < ARRAYSIZE(SaferBuiltinLevels); i++)
                Levels[i] = SaferBuiltinLevels[i].LevelId;
            return SaferWriteOutput(Levels, sizeof(Levels), InfoBuffer, InfoBufferSize, InfoBufferRetSize);

        case SaferPolicyEnableTransparentEnforcement:
            return SaferWriteOutput(&Policy.TransparentEnabled, sizeof(DWORD), InfoBuffer, InfoBufferSize,
                                    InfoBufferRetSize);

        case SaferPolicyDefaultLevel:
            if (!Policy.HasDefaultLevel)
            {
                SetLastError(ERROR_NOT_FOUND);
                return FALSE;
            }
            return SaferWriteOutput(&Policy.DefaultLevel, sizeof(DWORD), InfoBuffer, InfoBufferSize,
                                    InfoBufferRetSize);

        case SaferPolicyEvaluateUserScope:
            Value = TRUE;
            return SaferWriteOutput(&Value, sizeof(DWORD), InfoBuffer, InfoBufferSize, InfoBufferRetSize);

        case SaferPolicyScopeFlags:
            return SaferWriteOutput(&Policy.PolicyScope, sizeof(DWORD), InfoBuffer, InfoBufferSize,
                                    InfoBufferRetSize);

        case SaferPolicyAuthenticodeEnabled:
            return SaferWriteOutput(&Policy.AuthenticodeEnabled, sizeof(DWORD), InfoBuffer, InfoBufferSize,
                                    InfoBufferRetSize);

        case SaferPolicyDefaultLevelFlags:
            /* TODO: the default level flags have no documented registry value */
            SetLastError(ERROR_NOT_SUPPORTED);
            return FALSE;

        default:
            SetLastError(ERROR_INVALID_PARAMETER);
            return FALSE;
    }
}

BOOL
WINAPI
SaferSetPolicyInformation(
    _In_ DWORD dwScopeId,
    _In_ SAFER_POLICY_INFO_CLASS SaferPolicyInfoClass,
    _In_ DWORD InfoBufferSize,
    _In_reads_bytes_(InfoBufferSize) PVOID InfoBuffer,
    _Reserved_ PVOID pReserved)
{
    PCWSTR Name;
    DWORD Value;
    LSTATUS Status;
    HKEY Key;

    TRACE("(%lu, %d, %lu, %p, %p)\n", dwScopeId, SaferPolicyInfoClass, InfoBufferSize, InfoBuffer, pReserved);

    if (!SaferIsValidScope(dwScopeId) || !InfoBuffer || InfoBufferSize < sizeof(DWORD))
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    Value = *(PDWORD)InfoBuffer;

    switch (SaferPolicyInfoClass)
    {
        case SaferPolicyEnableTransparentEnforcement:
            Name = L"TransparentEnabled";
            if (Value > 2)
            {
                SetLastError(ERROR_INVALID_PARAMETER);
                return FALSE;
            }
            break;

        case SaferPolicyDefaultLevel:
            Name = L"DefaultLevel";
            if (!SaferFindBuiltinLevel(Value))
            {
                SetLastError(ERROR_INVALID_PARAMETER);
                return FALSE;
            }
            break;

        case SaferPolicyScopeFlags:
            Name = L"PolicyScope";
            if (dwScopeId != SAFER_SCOPEID_MACHINE || Value > 1)
            {
                SetLastError(ERROR_INVALID_PARAMETER);
                return FALSE;
            }
            break;

        case SaferPolicyAuthenticodeEnabled:
            Name = L"AuthenticodeEnabled";
            if (dwScopeId != SAFER_SCOPEID_MACHINE || Value > 1)
            {
                SetLastError(ERROR_INVALID_PARAMETER);
                return FALSE;
            }
            break;

        case SaferPolicyLevelList:
        case SaferPolicyEvaluateUserScope:
        case SaferPolicyDefaultLevelFlags:
            /* TODO: custom levels, the user-scope switch and the default level flags have no documented store */
            SetLastError(ERROR_NOT_SUPPORTED);
            return FALSE;

        default:
            SetLastError(ERROR_INVALID_PARAMETER);
            return FALSE;
    }

    Status = SaferCreatePolicyKey(dwScopeId, NULL, &Key);
    if (Status == ERROR_SUCCESS)
    {
        Status = RegSetValueExW(Key, Name, 0, REG_DWORD, (const BYTE *)&Value, sizeof(Value));
        RegCloseKey(Key);
    }

    if (Status != ERROR_SUCCESS)
    {
        SetLastError(Status);
        return FALSE;
    }
    return TRUE;
}

BOOL
WINAPI
SaferComputeTokenFromLevel(
    _In_ SAFER_LEVEL_HANDLE LevelHandle,
    _In_opt_ HANDLE InAccessToken,
    _Out_ PHANDLE OutAccessToken,
    _In_ DWORD dwFlags,
    _Inout_opt_ PVOID pReserved)
{
    SAFER_LEVEL_OBJECT Level;
    SAFER_TOKEN_PLAN Plan;
    PTOKEN_GROUPS Groups = NULL;
    PTOKEN_PRIVILEGES Privileges = NULL;
    PTOKEN_USER User = NULL;
    HANDLE Token, NewToken = NULL;
    DWORD Inert = FALSE, Size, Flags = 0;
    BOOL Restricts, Equal, Ok;

    TRACE("(%p, %p, %p, 0x%lx, %p)\n", LevelHandle, InAccessToken, OutAccessToken, dwFlags, pReserved);

    if (!SaferCopyLevel(LevelHandle, &Level, FALSE))
        return FALSE;

    if (((dwFlags & (SAFER_TOKEN_COMPARE_ONLY | SAFER_TOKEN_WANT_FLAGS)) && !pReserved) ||
        (!(dwFlags & SAFER_TOKEN_COMPARE_ONLY) && !OutAccessToken))
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    if (Level.LevelId == SAFER_LEVELID_DISALLOWED)
    {
        SetLastError(ERROR_ACCESS_DISABLED_BY_POLICY);
        return FALSE;
    }

    Token = InAccessToken ? InAccessToken : SaferOpenCallerToken();
    if (!Token)
        return FALSE;

    Groups = SaferQueryToken(Token, TokenGroups);
    Privileges = SaferQueryToken(Token, TokenPrivileges);
    User = SaferQueryToken(Token, TokenUser);
    Ok = Groups && Privileges && SaferBuildPlan(Level.LevelId, Groups, &Plan);
    if (Ok)
    {
        Restricts = SaferPlanRestricts(&Plan, Groups, Privileges);
        if (dwFlags & SAFER_TOKEN_MAKE_INERT)
            GetTokenInformation(Token, TokenSandBoxInert, &Inert, sizeof(Inert), &Size);
        Equal = !Restricts && (!(dwFlags & SAFER_TOKEN_MAKE_INERT) || Inert);

        if (dwFlags & SAFER_TOKEN_COMPARE_ONLY)
        {
            *(PDWORD)pReserved = Restricts ? (DWORD)-1 : 0;
        }
        else
        {
            if (dwFlags & SAFER_TOKEN_WANT_FLAGS)
                *(PDWORD)pReserved = Level.SaferFlags;

            if (Equal && (dwFlags & SAFER_TOKEN_NULL_IF_EQUAL))
            {
                *OutAccessToken = NULL;
            }
            else
            {
                if (Plan.DisableMaxPrivilege)
                    Flags |= DISABLE_MAX_PRIVILEGE;
                if (dwFlags & SAFER_TOKEN_MAKE_INERT)
                    Flags |= SANDBOX_INERT;

                Ok = CreateRestrictedToken(Token, Flags,
                                           Plan.DisableCount, Plan.DisableCount ? Plan.Disable : NULL,
                                           0, NULL,
                                           Plan.RestrictCount, Plan.RestrictCount ? Plan.Restrict : NULL,
                                           &NewToken);
                if (Ok && Plan.DisableMaxPrivilege)
                {
                    Ok = User && SaferAdjustTokenDefaults(NewToken, User->User.Sid, &Plan);
                    if (!Ok)
                        CloseHandle(NewToken);
                }
                if (Ok)
                    *OutAccessToken = NewToken;
            }
        }
        SaferFreePlan(&Plan);
    }

    SaferFree(Groups);
    SaferFree(Privileges);
    SaferFree(User);
    if (!InAccessToken)
        CloseHandle(Token);
    return Ok;
}

BOOL
WINAPI
SaferRecordEventLogEntry(
    _In_ SAFER_LEVEL_HANDLE hLevel,
    _In_ PCWSTR szTargetPath,
    _Reserved_ PVOID pReserved)
{
    SAFER_LEVEL_OBJECT Level;
    WCHAR Guid[SAFER_GUID_CHARS];
    PCWSTR Strings[3];
    WORD StringCount = 1;
    DWORD EventId;
    HANDLE Source;
    BOOL Ok;

    TRACE("(%p, %s, %p)\n", hLevel, debugstr_w(szTargetPath), pReserved);

    if (!SaferCopyLevel(hLevel, &Level, TRUE))
        return FALSE;

    if (!szTargetPath)
    {
        SaferFree(Level.RulePath);
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    if (Level.LevelId == SAFER_LEVELID_FULLYTRUSTED)
    {
        SaferFree(Level.RulePath);
        return TRUE;
    }

    SaferFormatGuid(&Level.RuleGuid, Guid);
    Strings[0] = szTargetPath;
    Strings[1] = Guid;
    Strings[2] = Level.RulePath ? Level.RulePath : L"";

    switch (Level.IdentityType)
    {
        case SaferIdentityTypeImageName:
            EventId = SAFER_EVENT_PATH_RULE;
            StringCount = 3;
            break;
        case SaferIdentityTypeImageHash:
        case SaferIdentityTypeUrlZone:
            EventId = SAFER_EVENT_OTHER_RULE;
            StringCount = 2;
            break;
        case SaferIdentityTypeCertificate:
            EventId = SAFER_EVENT_PUBLISHER_RULE;
            break;
        default:
            EventId = SAFER_EVENT_DEFAULT_LEVEL;
            break;
    }

    Source = RegisterEventSourceW(NULL, SAFER_EVENT_SOURCE);
    if (!Source)
    {
        SaferFree(Level.RulePath);
        return FALSE;
    }

    Ok = ReportEventW(Source, EVENTLOG_WARNING_TYPE, 0, EventId, NULL, StringCount, 0, Strings, NULL);
    DeregisterEventSource(Source);
    SaferFree(Level.RulePath);
    return Ok;
}

BOOL
WINAPI
SaferiIsExecutableFileType(
    _In_ PCWSTR szFullPath,
    _In_ BOOLEAN bFromShellExecute)
{
    PCWSTR Name, Extension, Entry;
    PWSTR Types;
    BOOL Found = FALSE;
    ULONG i;

    TRACE("(%s, %u)\n", debugstr_w(szFullPath), bFromShellExecute);

    if (!szFullPath || !szFullPath[0])
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    Name = wcsrchr(szFullPath, L'\\');
    Name = Name ? Name + 1 : szFullPath;
    Extension = wcsrchr(Name, L'.');
    if (!Extension || !Extension[1])
        return FALSE;
    Extension++;

    if (bFromShellExecute && !_wcsicmp(Extension, L"EXE"))
        return FALSE;

    Types = SaferReadExecutableTypes();
    if (Types)
    {
        for (Entry = Types; *Entry && !Found; Entry += wcslen(Entry) + 1)
            Found = SaferExtensionEquals(Entry, Extension);
        SaferFree(Types);
        return Found;
    }

    for (i = 0; i < ARRAYSIZE(SaferDefaultExecutableTypes) && !Found; i++)
        Found = SaferExtensionEquals(SaferDefaultExecutableTypes[i], Extension);
    return Found;
}

typedef struct _SAFER_HASH_SEARCH
{
    ALG_ID Algorithm;
    const BYTE *Hash;
    DWORD HashSize;
    DWORD ImageSize;
    SAFER_MATCH Match;
} SAFER_HASH_SEARCH, *PSAFER_HASH_SEARCH;

static VOID
SaferHashSearchCallback(DWORD ScopeId, DWORD LevelId, HKEY RuleKey, const GUID *Guid, PVOID Context)
{
    PSAFER_HASH_SEARCH Search = Context;
    BYTE Hash[SAFER_MAX_HASH_SIZE];
    ULONGLONG ImageSize;
    DWORD HashSize;
    ALG_ID Algorithm;

    if (Search->Match.Found && LevelId >= Search->Match.LevelId)
        return;

    if (!SaferReadRuleHash(RuleKey, Hash, &HashSize, &Algorithm, &ImageSize))
        return;

    if ((Search->Algorithm && Search->Algorithm != Algorithm) ||
        HashSize != Search->HashSize || memcmp(Hash, Search->Hash, HashSize) ||
        (Search->ImageSize && ImageSize && ImageSize != Search->ImageSize))
    {
        return;
    }

    SaferSetMatch(&Search->Match, SaferIdentityTypeImageHash, ScopeId, LevelId, RuleKey, Guid, 0, NULL);
}

BOOL
WINAPI
SaferiSearchMatchingHashRules(
    _In_opt_ ALG_ID HashAlgorithm,
    _In_ PBYTE pHashBytes,
    _In_ DWORD dwHashSize,
    _In_opt_ DWORD dwOriginalImageSize,
    _Out_ PDWORD pdwFoundLevel,
    PDWORD pdwSaferFlags)
{
    SAFER_HASH_SEARCH Search;

    TRACE("(0x%x, %p, %lu, %lu, %p, %p)\n", HashAlgorithm, pHashBytes, dwHashSize, dwOriginalImageSize,
          pdwFoundLevel, pdwSaferFlags);

    if (!pHashBytes || dwHashSize == 0 || dwHashSize > SAFER_MAX_HASH_SIZE || !pdwFoundLevel)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    ZeroMemory(&Search, sizeof(Search));
    Search.Algorithm = HashAlgorithm;
    Search.Hash = pHashBytes;
    Search.HashSize = dwHashSize;
    Search.ImageSize = dwOriginalImageSize;
    SaferEnumerateRules(SaferIdentityTypeImageHash, SaferHashSearchCallback, &Search);

    if (!Search.Match.Found)
    {
        SetLastError(ERROR_NOT_FOUND);
        return FALSE;
    }

    *pdwFoundLevel = Search.Match.LevelId;
    if (pdwSaferFlags)
        *pdwSaferFlags = Search.Match.SaferFlags;
    SaferFree(Search.Match.RulePath);
    return TRUE;
}

/* EOF */
