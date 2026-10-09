/*
 * PROJECT:     LiberNT System Setup
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Configuration of a newly installed system from the installer
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"

#include <stdlib.h>
#include <time.h>
#include <lmcons.h>
#include <ntsecapi.h>
#include <ntsam.h>
#include <userenv.h>
#include <strsafe.h>
#include <ndk/obfuncs.h>
#include <ndk/psfuncs.h>
#include <ndk/cmfuncs.h>
#include <ndk/iofuncs.h>
#include <ndk/setypes.h>
#include <tzlib.h>

#define NDEBUG
#include <debug.h>

typedef struct _TARGET_SETTINGS
{
    WCHAR MachineKey[MAX_PATH];
    WCHAR UserKey[MAX_PATH];
    WCHAR SystemVolume[MAX_PATH];
    WCHAR SystemDrive[3];
    WCHAR SystemRoot[MAX_PATH];
    WCHAR ComputerName[MAX_COMPUTERNAME_LENGTH + 1];
    WCHAR UserName[UNLEN + 1];
    WCHAR Password[PWLEN + 1];
    DWORD TimeZoneIndex;
    BOOL AutoDaylight;
} TARGET_SETTINGS, *PTARGET_SETTINGS;

typedef struct _TARGET_REGISTRATION
{
    PVOID DefaultContext;
} TARGET_REGISTRATION, *PTARGET_REGISTRATION;

static HANDLE hTargetDosDevices = NULL;
static HANDLE hTargetDriveLink = NULL;

static VOID
ReportTargetProgress(
    _In_ PCWSTR Text)
{
    HANDLE hOutput = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD Written;

    if (hOutput == NULL || hOutput == INVALID_HANDLE_VALUE)
        return;

    WriteFile(hOutput, Text, (DWORD)(wcslen(Text) * sizeof(WCHAR)), &Written, NULL);
    WriteFile(hOutput, L"\n", sizeof(WCHAR), &Written, NULL);
}

static VOID
ReportTargetPhase(
    _In_ UINT Phase)
{
    WCHAR Text[8];

    StringCchPrintfW(Text, ARRAYSIZE(Text), L"#%u", Phase);
    ReportTargetProgress(Text);
}

static BOOL
CopySetting(
    _Out_writes_(cchDest) PWSTR Dest,
    _In_ SIZE_T cchDest,
    _In_ PCWSTR Value)
{
    return SUCCEEDED(StringCchCopyW(Dest, cchDest, Value));
}

static BOOL
ReadTargetSettings(
    _Out_ PTARGET_SETTINGS Settings)
{
    HANDLE hInput = GetStdHandle(STD_INPUT_HANDLE);
    SIZE_T cbBuffer = 32 * 1024;
    PWSTR Buffer, Line, Next, Value;
    DWORD cbTotal = 0, cbRead;
    BOOL Result = TRUE;

    ZeroMemory(Settings, sizeof(*Settings));
    Settings->TimeZoneIndex = (DWORD)-1;
    Settings->AutoDaylight = TRUE;

    if (hInput == NULL || hInput == INVALID_HANDLE_VALUE)
        return FALSE;

    Buffer = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, cbBuffer + sizeof(WCHAR));
    if (!Buffer)
        return FALSE;

    while (cbTotal < cbBuffer &&
           ReadFile(hInput, (PBYTE)Buffer + cbTotal, (DWORD)(cbBuffer - cbTotal), &cbRead, NULL) &&
           cbRead != 0)
    {
        cbTotal += cbRead;
    }
    Buffer[cbTotal / sizeof(WCHAR)] = UNICODE_NULL;

    for (Line = Buffer; Line && *Line; Line = Next)
    {
        Next = wcschr(Line, L'\n');
        if (Next)
            *Next++ = UNICODE_NULL;
        if (*Line && Line[wcslen(Line) - 1] == L'\r')
            Line[wcslen(Line) - 1] = UNICODE_NULL;

        Value = wcschr(Line, L'=');
        if (!Value)
            continue;
        *Value++ = UNICODE_NULL;

        if (!_wcsicmp(Line, L"MachineKey"))
            Result &= CopySetting(Settings->MachineKey, ARRAYSIZE(Settings->MachineKey), Value);
        else if (!_wcsicmp(Line, L"UserKey"))
            Result &= CopySetting(Settings->UserKey, ARRAYSIZE(Settings->UserKey), Value);
        else if (!_wcsicmp(Line, L"SystemVolume"))
            Result &= CopySetting(Settings->SystemVolume, ARRAYSIZE(Settings->SystemVolume), Value);
        else if (!_wcsicmp(Line, L"SystemDrive"))
            Result &= CopySetting(Settings->SystemDrive, ARRAYSIZE(Settings->SystemDrive), Value);
        else if (!_wcsicmp(Line, L"SystemRoot"))
            Result &= CopySetting(Settings->SystemRoot, ARRAYSIZE(Settings->SystemRoot), Value);
        else if (!_wcsicmp(Line, L"ComputerName"))
            Result &= CopySetting(Settings->ComputerName, ARRAYSIZE(Settings->ComputerName), Value);
        else if (!_wcsicmp(Line, L"UserName"))
            Result &= CopySetting(Settings->UserName, ARRAYSIZE(Settings->UserName), Value);
        else if (!_wcsicmp(Line, L"Password"))
            Result &= CopySetting(Settings->Password, ARRAYSIZE(Settings->Password), Value);
        else if (!_wcsicmp(Line, L"TimeZoneIndex"))
            Settings->TimeZoneIndex = wcstoul(Value, NULL, 10);
        else if (!_wcsicmp(Line, L"AutoDaylight"))
            Settings->AutoDaylight = (wcstoul(Value, NULL, 10) != 0);
    }

    SecureZeroMemory(Buffer, cbBuffer);
    HeapFree(GetProcessHeap(), 0, Buffer);

    return Result &&
           *Settings->MachineKey && *Settings->UserKey &&
           *Settings->SystemVolume && *Settings->SystemDrive &&
           *Settings->SystemRoot && *Settings->ComputerName &&
           *Settings->UserName;
}

static NTSTATUS
OpenTargetKey(
    _In_ PCWSTR Path,
    _In_opt_ PCWSTR SubPath,
    _Out_ PHANDLE KeyHandle)
{
    WCHAR Buffer[MAX_PATH];
    UNICODE_STRING KeyName;
    OBJECT_ATTRIBUTES ObjectAttributes;

    StringCchCopyW(Buffer, ARRAYSIZE(Buffer), Path);
    if (SubPath)
    {
        StringCchCatW(Buffer, ARRAYSIZE(Buffer), L"\\");
        StringCchCatW(Buffer, ARRAYSIZE(Buffer), SubPath);
    }

    RtlInitUnicodeString(&KeyName, Buffer);
    InitializeObjectAttributes(&ObjectAttributes, &KeyName, OBJ_CASE_INSENSITIVE, NULL, NULL);
    return NtOpenKey(KeyHandle, MAXIMUM_ALLOWED, &ObjectAttributes);
}

static DWORD
OpenTargetView(
    _In_ PTARGET_SETTINGS Settings)
{
    HANDLE hMachine = NULL, hClasses = NULL, hUsers = NULL, hDefault = NULL;
    NTSTATUS Status;

    Status = OpenTargetKey(Settings->MachineKey, NULL, &hMachine);
    if (NT_SUCCESS(Status))
        Status = OpenTargetKey(Settings->MachineKey, L"SOFTWARE\\Classes", &hClasses);
    if (NT_SUCCESS(Status))
        Status = OpenTargetKey(Settings->UserKey, NULL, &hUsers);
    if (NT_SUCCESS(Status))
        Status = OpenTargetKey(Settings->UserKey, L".DEFAULT", &hDefault);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("Cannot open the target registry (Status 0x%08lx)\n", Status);
        if (hMachine)
            NtClose(hMachine);
        if (hClasses)
            NtClose(hClasses);
        if (hUsers)
            NtClose(hUsers);
        if (hDefault)
            NtClose(hDefault);
        return RtlNtStatusToDosError(Status);
    }

    RegOverridePredefKey(HKEY_LOCAL_MACHINE, (HKEY)hMachine);
    RegOverridePredefKey(HKEY_CLASSES_ROOT, (HKEY)hClasses);
    RegOverridePredefKey(HKEY_USERS, (HKEY)hUsers);
    RegOverridePredefKey(HKEY_CURRENT_USER, (HKEY)hDefault);

    return ERROR_SUCCESS;
}

static DWORD
MapTargetDrive(
    _In_ PTARGET_SETTINGS Settings)
{
    OBJECT_ATTRIBUTES ObjectAttributes;
    UNICODE_STRING LinkName, LinkTarget;
    NTSTATUS Status;

    InitializeObjectAttributes(&ObjectAttributes, NULL, 0, NULL, NULL);
    Status = NtCreateDirectoryObject(&hTargetDosDevices, DIRECTORY_ALL_ACCESS, &ObjectAttributes);
    if (!NT_SUCCESS(Status))
        return RtlNtStatusToDosError(Status);

    RtlInitUnicodeString(&LinkName, Settings->SystemDrive);
    RtlInitUnicodeString(&LinkTarget, Settings->SystemVolume);
    InitializeObjectAttributes(&ObjectAttributes, &LinkName, OBJ_CASE_INSENSITIVE, hTargetDosDevices, NULL);
    Status = NtCreateSymbolicLinkObject(&hTargetDriveLink, SYMBOLIC_LINK_ALL_ACCESS, &ObjectAttributes, &LinkTarget);
    if (!NT_SUCCESS(Status))
        return RtlNtStatusToDosError(Status);

    Status = NtSetInformationProcess(NtCurrentProcess(),
                                     ProcessDeviceMap,
                                     &hTargetDosDevices,
                                     sizeof(hTargetDosDevices));
    if (!NT_SUCCESS(Status))
        return RtlNtStatusToDosError(Status);

    return ERROR_SUCCESS;
}

static VOID
SetTargetProfileEnvironment(
    _In_ PTARGET_SETTINGS Settings)
{
    WCHAR Profile[MAX_PATH];
    WCHAR Buffer[MAX_PATH];

    StringCchPrintfW(Profile, ARRAYSIZE(Profile), L"%s\\system32\\config\\systemprofile", Settings->SystemRoot);
    SetEnvironmentVariableW(L"USERPROFILE", Profile);

    StringCchPrintfW(Buffer, ARRAYSIZE(Buffer), L"%s\\AppData\\Roaming", Profile);
    SetEnvironmentVariableW(L"APPDATA", Buffer);

    StringCchPrintfW(Buffer, ARRAYSIZE(Buffer), L"%s\\AppData\\Local", Profile);
    SetEnvironmentVariableW(L"LOCALAPPDATA", Buffer);
}

static VOID
ApplyTargetEnvironment(
    _In_ PTARGET_SETTINGS Settings)
{
    PWSTR Environment, Variable, Separator;

    if (CreateEnvironmentBlock((LPVOID*)&Environment, NULL, FALSE))
    {
        for (Variable = Environment; *Variable; Variable += wcslen(Variable) + 1)
        {
            Separator = wcschr(Variable + 1, L'=');
            if (!Separator)
                continue;

            *Separator = UNICODE_NULL;
            SetEnvironmentVariableW(Variable, Separator + 1);
            *Separator = L'=';
        }

        DestroyEnvironmentBlock(Environment);
    }

    SetEnvironmentVariableW(L"COMPUTERNAME", Settings->ComputerName);
    SetTargetProfileEnvironment(Settings);
}

static LONG
SetRegString(
    _In_ HKEY hKey,
    _In_ PCWSTR ValueName,
    _In_ DWORD Type,
    _In_ PCWSTR Data)
{
    return RegSetValueExW(hKey,
                          ValueName,
                          0,
                          Type,
                          (const BYTE*)Data,
                          (DWORD)((wcslen(Data) + 1) * sizeof(WCHAR)));
}

static LONG
SetRegDword(
    _In_ HKEY hKey,
    _In_ PCWSTR ValueName,
    _In_ DWORD Data)
{
    return RegSetValueExW(hKey, ValueName, 0, REG_DWORD, (const BYTE*)&Data, sizeof(Data));
}

static LONG
SetRegTimeFields(
    _In_ HKEY hKey,
    _In_ PCWSTR ValueName,
    _In_ const SYSTEMTIME *Time)
{
    TIME_FIELDS TimeFields;

    TimeFields.Year = Time->wYear;
    TimeFields.Month = Time->wMonth;
    TimeFields.Day = Time->wDay;
    TimeFields.Hour = Time->wHour;
    TimeFields.Minute = Time->wMinute;
    TimeFields.Second = Time->wSecond;
    TimeFields.Milliseconds = Time->wMilliseconds;
    TimeFields.Weekday = Time->wDayOfWeek;

    return RegSetValueExW(hKey, ValueName, 0, REG_BINARY, (const BYTE*)&TimeFields, sizeof(TimeFields));
}

static DWORD
WriteTargetSystemRoot(
    _In_ PTARGET_SETTINGS Settings)
{
    WCHAR Buffer[MAX_PATH];
    HKEY hKey;
    LONG Error;

    Error = RegOpenKeyExW(HKEY_LOCAL_MACHINE,
                          L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion",
                          0,
                          KEY_SET_VALUE,
                          &hKey);
    if (Error != ERROR_SUCCESS)
        return Error;

    SetRegString(hKey, L"PathName", REG_SZ, Settings->SystemRoot);
    SetRegString(hKey, L"SystemRoot", REG_SZ, Settings->SystemRoot);
    SetRegDword(hKey, L"InstallDate", (DWORD)time(NULL));
    RegCloseKey(hKey);

    StringCchPrintfW(Buffer, ARRAYSIZE(Buffer), L"%s\\system", Settings->SystemRoot);
    CreateDirectoryW(Buffer, NULL);

    return ERROR_SUCCESS;
}

static DWORD
WriteTargetComputerName(
    _In_ PTARGET_SETTINGS Settings)
{
    HKEY hKey;
    LONG Error;

    Error = RegCreateKeyExW(HKEY_LOCAL_MACHINE,
                            L"SYSTEM\\CurrentControlSet\\Control\\ComputerName\\ComputerName",
                            0,
                            NULL,
                            REG_OPTION_NON_VOLATILE,
                            KEY_SET_VALUE,
                            NULL,
                            &hKey,
                            NULL);
    if (Error != ERROR_SUCCESS)
        return Error;
    Error = SetRegString(hKey, L"ComputerName", REG_SZ, Settings->ComputerName);
    RegCloseKey(hKey);
    if (Error != ERROR_SUCCESS)
        return Error;

    Error = RegCreateKeyExW(HKEY_LOCAL_MACHINE,
                            L"SYSTEM\\CurrentControlSet\\Services\\Tcpip\\Parameters",
                            0,
                            NULL,
                            REG_OPTION_NON_VOLATILE,
                            KEY_SET_VALUE,
                            NULL,
                            &hKey,
                            NULL);
    if (Error != ERROR_SUCCESS)
        return Error;
    SetRegString(hKey, L"Hostname", REG_SZ, Settings->ComputerName);
    SetRegString(hKey, L"NV Hostname", REG_SZ, Settings->ComputerName);
    SetRegString(hKey, L"NV Domain", REG_SZ, L"");
    RegCloseKey(hKey);

    return ERROR_SUCCESS;
}

static DWORD
WriteTargetTimeZone(
    _In_ PTARGET_SETTINGS Settings)
{
    WCHAR KeyName[128];
    WCHAR StandardName[32];
    WCHAR DaylightName[32];
    ULONG StandardNameSize, DaylightNameSize;
    REG_TZI_FORMAT TimeZoneInfo;
    ULONG Index;
    DWORD cchKeyName, i;
    HKEY hZonesKey, hZoneKey, hKey;
    LONG Error;
    BOOL Found = FALSE;

    if (Settings->TimeZoneIndex == (DWORD)-1)
        return ERROR_SUCCESS;

    Error = RegOpenKeyExW(HKEY_LOCAL_MACHINE,
                          L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Time Zones",
                          0,
                          KEY_ENUMERATE_SUB_KEYS,
                          &hZonesKey);
    if (Error != ERROR_SUCCESS)
        return Error;

    for (i = 0; !Found; i++)
    {
        cchKeyName = ARRAYSIZE(KeyName);
        Error = RegEnumKeyExW(hZonesKey, i, KeyName, &cchKeyName, NULL, NULL, NULL, NULL);
        if (Error != ERROR_SUCCESS)
            break;

        if (RegOpenKeyExW(hZonesKey, KeyName, 0, KEY_QUERY_VALUE, &hZoneKey) != ERROR_SUCCESS)
            continue;

        StandardNameSize = sizeof(StandardName);
        DaylightNameSize = sizeof(DaylightName);
        if (QueryTimeZoneData(hZoneKey,
                              &Index,
                              &TimeZoneInfo,
                              NULL,
                              NULL,
                              StandardName,
                              &StandardNameSize,
                              DaylightName,
                              &DaylightNameSize) == ERROR_SUCCESS &&
            Index == Settings->TimeZoneIndex)
        {
            Found = TRUE;
        }
        RegCloseKey(hZoneKey);
    }
    RegCloseKey(hZonesKey);

    if (!Found)
        return ERROR_NOT_FOUND;

    Error = RegCreateKeyExW(HKEY_LOCAL_MACHINE,
                            L"SYSTEM\\CurrentControlSet\\Control\\TimeZoneInformation",
                            0,
                            NULL,
                            REG_OPTION_NON_VOLATILE,
                            KEY_SET_VALUE,
                            NULL,
                            &hKey,
                            NULL);
    if (Error != ERROR_SUCCESS)
        return Error;

    SetRegDword(hKey, L"Bias", (DWORD)TimeZoneInfo.Bias);
    SetRegString(hKey, L"StandardName", REG_SZ, StandardName);
    SetRegDword(hKey, L"StandardBias", (DWORD)TimeZoneInfo.StandardBias);
    SetRegTimeFields(hKey, L"StandardStart", &TimeZoneInfo.StandardDate);
    SetRegString(hKey, L"DaylightName", REG_SZ, DaylightName);
    SetRegDword(hKey, L"DaylightBias", (DWORD)TimeZoneInfo.DaylightBias);
    SetRegTimeFields(hKey, L"DaylightStart", &TimeZoneInfo.DaylightDate);
    SetRegString(hKey, L"TimeZoneKeyName", REG_SZ, KeyName);
    RegCloseKey(hKey);

    SetAutoDaylight(Settings->AutoDaylight);

    return ERROR_SUCCESS;
}

NTSTATUS
CreateTargetAccount(
    _In_ PCWSTR UserName,
    _In_ PCWSTR Password)
{
    static SID_IDENTIFIER_AUTHORITY NtAuthority = {SECURITY_NT_AUTHORITY};
    PPOLICY_ACCOUNT_DOMAIN_INFO AccountDomain = NULL;
    LSA_OBJECT_ATTRIBUTES ObjectAttributes;
    LSA_HANDLE PolicyHandle = NULL;
    SAM_HANDLE ServerHandle = NULL;
    SAM_HANDLE DomainHandle = NULL;
    SAM_HANDLE BuiltinHandle = NULL;
    SAM_HANDLE UserHandle = NULL;
    SAM_HANDLE AliasHandle = NULL;
    PUSER_CONTROL_INFORMATION ControlInfo = NULL;
    USER_SET_PASSWORD_INFORMATION PasswordInfo;
    PSID BuiltinSid = NULL;
    PSID UserSid = NULL;
    UNICODE_STRING AccountName;
    ULONG GrantedAccess;
    ULONG RelativeId;
    UCHAR SubAuthorityCount, i;
    NTSTATUS Status;

    ZeroMemory(&ObjectAttributes, sizeof(ObjectAttributes));
    ObjectAttributes.Length = sizeof(ObjectAttributes);

    Status = LsaOpenPolicy(NULL, &ObjectAttributes, POLICY_VIEW_LOCAL_INFORMATION, &PolicyHandle);
    if (!NT_SUCCESS(Status))
        goto done;

    Status = LsaQueryInformationPolicy(PolicyHandle,
                                       PolicyAccountDomainInformation,
                                       (PVOID*)&AccountDomain);
    if (!NT_SUCCESS(Status))
        goto done;

    Status = SamConnect(NULL, &ServerHandle, SAM_SERVER_CONNECT | SAM_SERVER_LOOKUP_DOMAIN, NULL);
    if (!NT_SUCCESS(Status))
        goto done;

    Status = SamOpenDomain(ServerHandle,
                           DOMAIN_CREATE_USER | DOMAIN_LOOKUP | DOMAIN_READ_PASSWORD_PARAMETERS,
                           AccountDomain->DomainSid,
                           &DomainHandle);
    if (!NT_SUCCESS(Status))
        goto done;

    RtlInitUnicodeString(&AccountName, UserName);
    Status = SamCreateUser2InDomain(DomainHandle,
                                    &AccountName,
                                    USER_NORMAL_ACCOUNT,
                                    USER_ALL_ACCESS,
                                    &UserHandle,
                                    &GrantedAccess,
                                    &RelativeId);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("SamCreateUser2InDomain() failed (Status 0x%08lx)\n", Status);
        goto done;
    }

    RtlInitUnicodeString(&PasswordInfo.Password, Password);
    PasswordInfo.PasswordExpired = FALSE;
    Status = SamSetInformationUser(UserHandle, UserSetPasswordInformation, &PasswordInfo);
    if (!NT_SUCCESS(Status))
        goto done;

    Status = SamQueryInformationUser(UserHandle, UserControlInformation, (PVOID*)&ControlInfo);
    if (!NT_SUCCESS(Status))
        goto done;

    ControlInfo->UserAccountControl &= ~USER_ACCOUNT_DISABLED;
    ControlInfo->UserAccountControl |= USER_DONT_EXPIRE_PASSWORD;
    Status = SamSetInformationUser(UserHandle, UserControlInformation, ControlInfo);
    if (!NT_SUCCESS(Status))
        goto done;

    Status = RtlAllocateAndInitializeSid(&NtAuthority, 1,
                                         SECURITY_BUILTIN_DOMAIN_RID,
                                         0, 0, 0, 0, 0, 0, 0,
                                         &BuiltinSid);
    if (!NT_SUCCESS(Status))
        goto done;

    Status = SamOpenDomain(ServerHandle, DOMAIN_LOOKUP, BuiltinSid, &BuiltinHandle);
    if (!NT_SUCCESS(Status))
        goto done;

    Status = SamOpenAlias(BuiltinHandle, ALIAS_ADD_MEMBER, DOMAIN_ALIAS_RID_ADMINS, &AliasHandle);
    if (!NT_SUCCESS(Status))
        goto done;

    SubAuthorityCount = *RtlSubAuthorityCountSid(AccountDomain->DomainSid);
    UserSid = RtlAllocateHeap(RtlGetProcessHeap(), 0, RtlLengthRequiredSid(SubAuthorityCount + 1));
    if (!UserSid)
    {
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto done;
    }
    RtlInitializeSid(UserSid, RtlIdentifierAuthoritySid(AccountDomain->DomainSid), SubAuthorityCount + 1);
    for (i = 0; i < SubAuthorityCount; i++)
        *RtlSubAuthoritySid(UserSid, i) = *RtlSubAuthoritySid(AccountDomain->DomainSid, i);
    *RtlSubAuthoritySid(UserSid, SubAuthorityCount) = RelativeId;

    Status = SamAddMemberToAlias(AliasHandle, UserSid);

done:
    if (UserSid)
        RtlFreeHeap(RtlGetProcessHeap(), 0, UserSid);
    if (AliasHandle)
        SamCloseHandle(AliasHandle);
    if (BuiltinHandle)
        SamCloseHandle(BuiltinHandle);
    if (BuiltinSid)
        RtlFreeSid(BuiltinSid);
    if (ControlInfo)
        SamFreeMemory(ControlInfo);
    if (UserHandle)
        SamCloseHandle(UserHandle);
    if (DomainHandle)
        SamCloseHandle(DomainHandle);
    if (ServerHandle)
        SamCloseHandle(ServerHandle);
    if (AccountDomain)
        LsaFreeMemory(AccountDomain);
    if (PolicyHandle)
        LsaClose(PolicyHandle);

    return Status;
}

static DWORD
WriteTargetLogon(
    _In_ PTARGET_SETTINGS Settings)
{
    HKEY hKey;
    LONG Error;

    Error = RegOpenKeyExW(HKEY_LOCAL_MACHINE,
                          L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Winlogon",
                          0,
                          KEY_SET_VALUE,
                          &hKey);
    if (Error != ERROR_SUCCESS)
        return Error;

    SetRegString(hKey, L"DefaultDomainName", REG_SZ, Settings->ComputerName);
    SetRegString(hKey, L"DefaultUserName", REG_SZ, Settings->UserName);
    if (*Settings->Password)
    {
        SetRegString(hKey, L"AutoAdminLogon", REG_SZ, L"0");
        RegDeleteValueW(hKey, L"DefaultPassword");
    }
    else
    {
        SetRegString(hKey, L"AutoAdminLogon", REG_SZ, L"1");
        SetRegString(hKey, L"DefaultPassword", REG_SZ, L"");
    }
    RegCloseKey(hKey);

    return ERROR_SUCCESS;
}

static DWORD
EnableTargetPlugPlay(VOID)
{
    HKEY hKey;
    LONG Error;

    Error = RegOpenKeyExW(HKEY_LOCAL_MACHINE,
                          L"SYSTEM\\CurrentControlSet\\Services\\PlugPlay",
                          0,
                          KEY_SET_VALUE,
                          &hKey);
    if (Error != ERROR_SUCCESS)
        return Error;

    Error = SetRegDword(hKey, L"Start", SERVICE_AUTO_START);
    RegCloseKey(hKey);
    return Error;
}

static UINT CALLBACK
TargetRegistrationCallback(
    _In_ PVOID Context,
    _In_ UINT Notification,
    _In_ UINT_PTR Param1,
    _In_ UINT_PTR Param2)
{
    PTARGET_REGISTRATION Registration = (PTARGET_REGISTRATION)Context;
    PSP_REGISTER_CONTROL_STATUSW StatusInfo;
    PCWSTR FileName;

    if (Notification == SPFILENOTIFY_STARTREGISTRATION)
    {
        StatusInfo = (PSP_REGISTER_CONTROL_STATUSW)Param1;
        FileName = wcsrchr(StatusInfo->FileName, L'\\');
        ReportTargetProgress(FileName ? FileName + 1 : StatusInfo->FileName);
        return FILEOP_DOIT;
    }

    if (Notification == SPFILENOTIFY_ENDREGISTRATION)
    {
        StatusInfo = (PSP_REGISTER_CONTROL_STATUSW)Param1;
        if (StatusInfo->FailureCode != SPREG_SUCCESS)
        {
            DPRINT1("Registration of %S failed (FailureCode %u, Win32Error %u)\n",
                    StatusInfo->FileName, StatusInfo->FailureCode, StatusInfo->Win32Error);
        }
        return FILEOP_DOIT;
    }

    return SetupDefaultQueueCallbackW(Registration->DefaultContext, Notification, Param1, Param2);
}

static VOID
RegisterTargetComponents(VOID)
{
    TARGET_REGISTRATION Registration;

    Registration.DefaultContext = SetupInitDefaultQueueCallback(NULL);

    _SEH2_TRY
    {
        if (!SetupInstallFromInfSectionW(NULL,
                                         hSysSetupInf,
                                         L"RegistrationPhase2",
                                         SPINST_REGISTRY | SPINST_REGISTERCALLBACKAWARE | SPINST_REGSVR,
                                         NULL,
                                         NULL,
                                         0,
                                         TargetRegistrationCallback,
                                         &Registration,
                                         NULL,
                                         NULL))
        {
            DPRINT1("SetupInstallFromInfSectionW(RegistrationPhase2) failed (Error %lu)\n", GetLastError());
        }

        RegisterTypeLibraries(NULL, NULL, hSysSetupInf, L"TypeLibraries");
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        DPRINT1("Exception 0x%08lx while registering the components\n", _SEH2_GetExceptionCode());
    }
    _SEH2_END;

    if (Registration.DefaultContext)
        SetupTermDefaultQueueCallback(Registration.DefaultContext);
}

static BOOL
RelocatePathString(
    _In_reads_(cchData) PCWSTR Data,
    _In_ SIZE_T cchData,
    _In_ PCWSTR OldRoot,
    _In_ PCWSTR NewRoot,
    _Outptr_result_maybenull_ PWSTR* NewData,
    _Out_ PSIZE_T cchNewData)
{
    SIZE_T cchOld = wcslen(OldRoot);
    SIZE_T cchNew = wcslen(NewRoot);
    SIZE_T Count = 0, i, j;
    PWSTR Buffer;
    WCHAR Next;

    *NewData = NULL;
    *cchNewData = 0;

    for (i = 0; i + cchOld <= cchData; i++)
    {
        if (_wcsnicmp(&Data[i], OldRoot, cchOld) != 0)
            continue;
        Next = (i + cchOld < cchData) ? Data[i + cchOld] : UNICODE_NULL;
        if (Next != L'\\' && Next != UNICODE_NULL && Next != L'"' && Next != L';' && Next != L',' && Next != L' ')
            continue;
        Count++;
        i += cchOld - 1;
    }

    if (Count == 0)
        return FALSE;

    Buffer = HeapAlloc(GetProcessHeap(), 0, (cchData + Count * cchNew + 1) * sizeof(WCHAR));
    if (!Buffer)
        return FALSE;

    for (i = 0, j = 0; i < cchData; )
    {
        if (i + cchOld <= cchData && _wcsnicmp(&Data[i], OldRoot, cchOld) == 0)
        {
            Next = (i + cchOld < cchData) ? Data[i + cchOld] : UNICODE_NULL;
            if (Next == L'\\' || Next == UNICODE_NULL || Next == L'"' || Next == L';' || Next == L',' || Next == L' ')
            {
                CopyMemory(&Buffer[j], NewRoot, cchNew * sizeof(WCHAR));
                j += cchNew;
                i += cchOld;
                continue;
            }
        }
        Buffer[j++] = Data[i++];
    }
    Buffer[j] = UNICODE_NULL;

    *NewData = Buffer;
    *cchNewData = j;
    return TRUE;
}

static VOID
RelocateTargetKey(
    _In_ HKEY hKey,
    _In_ PCWSTR OldRoot,
    _In_ PCWSTR NewRoot)
{
    WCHAR Name[256];
    DWORD cchName, cbData, Type, Index;
    DWORD cbMaxData = 0;
    PBYTE Data;
    PWSTR NewData;
    SIZE_T cchNewData;
    HKEY hSubKey;

    if (RegQueryInfoKeyW(hKey, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, &cbMaxData, NULL, NULL) != ERROR_SUCCESS)
        return;

    Data = HeapAlloc(GetProcessHeap(), 0, cbMaxData + 2 * sizeof(WCHAR));
    if (Data)
    {
        for (Index = 0; ; Index++)
        {
            cchName = ARRAYSIZE(Name);
            cbData = cbMaxData;
            if (RegEnumValueW(hKey, Index, Name, &cchName, NULL, &Type, Data, &cbData) != ERROR_SUCCESS)
                break;
            if (Type != REG_SZ && Type != REG_EXPAND_SZ && Type != REG_MULTI_SZ)
                continue;

            if (RelocatePathString((PCWSTR)Data, cbData / sizeof(WCHAR), OldRoot, NewRoot, &NewData, &cchNewData))
            {
                RegSetValueExW(hKey, Name, 0, Type, (const BYTE*)NewData, (DWORD)(cchNewData * sizeof(WCHAR)));
                HeapFree(GetProcessHeap(), 0, NewData);
            }
        }
        HeapFree(GetProcessHeap(), 0, Data);
    }

    for (Index = 0; ; Index++)
    {
        cchName = ARRAYSIZE(Name);
        if (RegEnumKeyExW(hKey, Index, Name, &cchName, NULL, NULL, NULL, NULL) != ERROR_SUCCESS)
            break;
        if (RegOpenKeyExW(hKey, Name, REG_OPTION_OPEN_LINK, KEY_READ | KEY_SET_VALUE, &hSubKey) != ERROR_SUCCESS)
            continue;
        RelocateTargetKey(hSubKey, OldRoot, NewRoot);
        RegCloseKey(hSubKey);
    }
}

static VOID
RelocateTargetRegistry(
    _In_ PCWSTR OldRoot,
    _In_ PCWSTR NewRoot)
{
    static const PCWSTR Roots[] = { L"SOFTWARE", L"SYSTEM\\ControlSet001" };
    HKEY hKey;
    UINT i;

    if (_wcsicmp(OldRoot, NewRoot) == 0)
        return;

    for (i = 0; i < ARRAYSIZE(Roots); i++)
    {
        if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, Roots[i], 0, KEY_READ | KEY_SET_VALUE, &hKey) == ERROR_SUCCESS)
        {
            RelocateTargetKey(hKey, OldRoot, NewRoot);
            RegCloseKey(hKey);
        }
    }

    if (RegOpenKeyExW(HKEY_USERS, L".DEFAULT", 0, KEY_READ | KEY_SET_VALUE, &hKey) == ERROR_SUCCESS)
    {
        RelocateTargetKey(hKey, OldRoot, NewRoot);
        RegCloseKey(hKey);
    }
}

static DWORD
SaveTargetHive(
    _In_ PTARGET_SETTINGS Settings,
    _In_ PCWSTR LiveKeyName,
    _In_ PCWSTR FileName)
{
    WCHAR Path[MAX_PATH];
    UNICODE_STRING KeyName, NtPath;
    OBJECT_ATTRIBUTES ObjectAttributes;
    IO_STATUS_BLOCK IoStatusBlock;
    HANDLE KeyHandle = NULL, FileHandle = NULL;
    NTSTATUS Status;

    StringCchPrintfW(Path, ARRAYSIZE(Path), L"%s\\System32\\config\\%s", Settings->SystemRoot, FileName);
    if (!RtlDosPathNameToNtPathName_U(Path, &NtPath, NULL, NULL))
        return ERROR_PATH_NOT_FOUND;

    RtlInitUnicodeString(&KeyName, LiveKeyName);
    InitializeObjectAttributes(&ObjectAttributes, &KeyName, OBJ_CASE_INSENSITIVE, NULL, NULL);
    Status = NtOpenKey(&KeyHandle, KEY_READ, &ObjectAttributes);
    if (NT_SUCCESS(Status))
    {
        InitializeObjectAttributes(&ObjectAttributes, &NtPath, OBJ_CASE_INSENSITIVE, NULL, NULL);
        Status = NtCreateFile(&FileHandle,
                              FILE_GENERIC_WRITE,
                              &ObjectAttributes,
                              &IoStatusBlock,
                              NULL,
                              FILE_ATTRIBUTE_NORMAL,
                              0,
                              FILE_OVERWRITE_IF,
                              FILE_NON_DIRECTORY_FILE | FILE_SYNCHRONOUS_IO_NONALERT,
                              NULL,
                              0);
    }
    if (NT_SUCCESS(Status))
        Status = NtSaveKey(KeyHandle, FileHandle);

    if (FileHandle)
        NtClose(FileHandle);
    if (KeyHandle)
        NtClose(KeyHandle);
    RtlFreeUnicodeString(&NtPath);

    if (!NT_SUCCESS(Status))
        DPRINT1("Saving %S to %S failed (Status 0x%08lx)\n", LiveKeyName, Path, Status);

    return RtlNtStatusToDosError(Status);
}

static DWORD
SaveTargetSecurityHives(
    _In_ PTARGET_SETTINGS Settings)
{
    DWORD Error;

    pSetupEnablePrivilege(L"SeBackupPrivilege", TRUE);
    Error = SaveTargetHive(Settings, L"\\Registry\\Machine\\SAM", L"SAM");
    if (Error == ERROR_SUCCESS)
        Error = SaveTargetHive(Settings, L"\\Registry\\Machine\\SECURITY", L"SECURITY");
    pSetupEnablePrivilege(L"SeBackupPrivilege", FALSE);

    return Error;
}

static DWORD
CreateTargetBootStatusData(
    _In_ PTARGET_SETTINGS Settings)
{
    WCHAR Path[MAX_PATH];
    RTL_BSD_DATA InitialBsd;
    HANDLE hFile;
    DWORD Written;
    BOOL Success;

    StringCchPrintfW(Path, ARRAYSIZE(Path), L"%s\\bootstat.dat", Settings->SystemRoot);
    hFile = CreateFileW(Path,
                        GENERIC_READ | GENERIC_WRITE,
                        0,
                        NULL,
                        CREATE_NEW,
                        FILE_ATTRIBUTE_SYSTEM,
                        NULL);
    if (hFile == INVALID_HANDLE_VALUE)
        return GetLastError();

    ZeroMemory(&InitialBsd, sizeof(InitialBsd));
    InitialBsd.Version = sizeof(InitialBsd);
    InitialBsd.ProductType = NtProductWinNt;
    InitialBsd.AabEnabled = 1;
    InitialBsd.AabTimeout = 30;
    InitialBsd.LastBootSucceeded = TRUE;

    Success = WriteFile(hFile, &InitialBsd, sizeof(InitialBsd), &Written, NULL);
    CloseHandle(hFile);

    return Success ? ERROR_SUCCESS : GetLastError();
}

DWORD
InstallTargetSystem(VOID)
{
    TARGET_SETTINGS Settings;
    WCHAR LiveRoot[MAX_PATH];
    ITEMSDATA ItemsData = { NULL };
    REGISTRATIONNOTIFY Notify;
    NTSTATUS Status;
    DWORD Error;

    if (!ReadTargetSettings(&Settings))
        return ERROR_INVALID_PARAMETER;

    if (!GetWindowsDirectoryW(LiveRoot, ARRAYSIZE(LiveRoot)))
        return GetLastError();

    Error = OpenTargetView(&Settings);
    if (Error != ERROR_SUCCESS)
        return Error;

    Error = MapTargetDrive(&Settings);
    if (Error != ERROR_SUCCESS)
        return Error;

    SetEnvironmentVariableW(L"SystemDrive", Settings.SystemDrive);
    SetEnvironmentVariableW(L"SystemRoot", Settings.SystemRoot);
    SetEnvironmentVariableW(L"windir", Settings.SystemRoot);
    SetTargetProfileEnvironment(&Settings);

    ReportTargetPhase(1);
    CreateTempDir(L"TEMP");
    CreateTempDir(L"TMP");
    if (!InitializeProgramFilesDir())
        return ERROR_GEN_FAILURE;
    if (!InitializeProfiles())
        return GetLastError();
    ApplyTargetEnvironment(&Settings);
    InitializeDefaultUserLocale();
    Error = WriteTargetSystemRoot(&Settings);
    if (Error != ERROR_SUCCESS)
        return Error;
    Error = SaveDefaultUserHive();
    if (Error != ERROR_SUCCESS)
        return Error;
    if (!CopySystemProfile(0))
        return GetLastError();

    hSysSetupInf = SetupOpenInfFileW(L"syssetup.inf", NULL, INF_STYLE_WIN4, NULL);
    if (hSysSetupInf == INVALID_HANDLE_VALUE)
        return GetLastError();

    ReportTargetPhase(2);
    if (!InstallSysSetupInfDevices())
        return GetLastError();
    if (!InstallSysSetupInfComponents())
        return GetLastError();
    Error = EnableTargetPlugPlay();
    if (Error != ERROR_SUCCESS)
        return Error;

    ReportTargetPhase(3);
    if (!InstallNetworkComponent(L"MS_TCPIP", TRUE) && GetLastError() != ERROR_FILE_NOT_FOUND)
        DPRINT1("InstallNetworkComponent() failed (Error %lu)\n", GetLastError());

    ReportTargetPhase(4);
    DoWriteInstallationType(INSTALLATION_TYPE_WORKSTATION);
    WriteOwnerSettings(Settings.UserName, L"");
    Error = WriteTargetComputerName(&Settings);
    if (Error != ERROR_SUCCESS)
        return Error;
    Error = WriteTargetTimeZone(&Settings);
    if (Error != ERROR_SUCCESS)
        DPRINT1("WriteTargetTimeZone() failed (Error %lu)\n", Error);
    Status = SetAccountsDomainSid(NULL, Settings.ComputerName);
    if (!NT_SUCCESS(Status))
        return RtlNtStatusToDosError(Status);

    ReportTargetPhase(5);
    RegisterTargetComponents();

    ReportTargetPhase(6);
    InstallStartMenuItems(&ItemsData);

    ReportTargetPhase(7);
    ZeroMemory(&Notify, sizeof(Notify));
    InstallSecurity(&ItemsData, &Notify);

    ReportTargetPhase(8);
    Status = CreateTargetAccount(Settings.UserName, Settings.Password);
    Error = NT_SUCCESS(Status) ? WriteTargetLogon(&Settings) : RtlNtStatusToDosError(Status);
    SecureZeroMemory(Settings.Password, sizeof(Settings.Password));
    if (Error != ERROR_SUCCESS)
        return Error;

    ReportTargetPhase(9);
    SetupCloseInfFile(hSysSetupInf);
    hSysSetupInf = INVALID_HANDLE_VALUE;
    Error = CreateTargetBootStatusData(&Settings);
    if (Error != ERROR_SUCCESS)
        DPRINT1("CreateTargetBootStatusData() failed (Error %lu)\n", Error);
    RelocateTargetRegistry(LiveRoot, Settings.SystemRoot);

    return SaveTargetSecurityHives(&Settings);
}
