/*
 * PROJECT:     LiberNT KernelBase
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     AppContainer SID-to-moniker registration helpers
 */

#include <ntstatus.h>
#define WIN32_NO_STATUS
#include <windows.h>
#include <winternl.h>
#include <strsafe.h>
#include "wine/debug.h"

NTSYSAPI VOID NTAPI RtlAssert(_In_ PVOID FailedAssertion, _In_ PVOID FileName, _In_ ULONG LineNumber, _In_opt_ PSTR Message);
#if DBG
#define KbAssert(Expression) ((void)((Expression) || (RtlAssert((PVOID)#Expression, (PVOID)__FILE__, __LINE__, NULL), 0)))
#else
#define KbAssert(Expression) ((void)0)
#endif

#ifndef DIRECTORY_ALL_ACCESS
#define DIRECTORY_ALL_ACCESS (STANDARD_RIGHTS_REQUIRED | 0xF)
#endif
#ifndef SYMBOLIC_LINK_QUERY
#define SYMBOLIC_LINK_QUERY 0x0001
#endif
#ifndef SYMBOLIC_LINK_ALL_ACCESS
#define SYMBOLIC_LINK_ALL_ACCESS (STANDARD_RIGHTS_REQUIRED | 0x1)
#endif

#define APPCONTAINER_MAPPINGS_KEY \
    L"Software\\Classes\\Local Settings\\Software\\Microsoft\\Windows\\CurrentVersion\\AppContainer\\Mappings"

static HRESULT
BasepFormatAppContainerMappingPath(
    _In_ PSID AppContainerSid,
    _Out_writes_(PathCount) PWSTR Path,
    _In_ SIZE_T PathCount)
{
    UNICODE_STRING SidString = {0};
    NTSTATUS Status;
    HRESULT Result;

    if (!AppContainerSid || !IsValidSid(AppContainerSid))
        return E_INVALIDARG;
    Status = RtlConvertSidToUnicodeString(&SidString, AppContainerSid, TRUE);
    if (Status < 0)
        return HRESULT_FROM_WIN32(RtlNtStatusToDosError(Status));

    Result = StringCchPrintfW(Path,
                              PathCount,
                              L"%s\\%s",
                              APPCONTAINER_MAPPINGS_KEY,
                              SidString.Buffer);
    RtlFreeUnicodeString(&SidString);
    return Result;
}

HRESULT
WINAPI
AppContainerRegisterSid(
    _In_ PSID AppContainerSid,
    _In_ PCWSTR Moniker,
    _In_ PCWSTR DisplayName)
{
    WCHAR Path[512];
    HKEY Key;
    DWORD Disposition;
    LONG Error;
    HRESULT Result;

    if (!Moniker || !DisplayName)
        return E_INVALIDARG;

    Result = BasepFormatAppContainerMappingPath(AppContainerSid,
                                                Path,
                                                ARRAYSIZE(Path));
    if (FAILED(Result))
        return Result;

    Error = RegCreateKeyExW(HKEY_CURRENT_USER,
                            Path,
                            0,
                            NULL,
                            0,
                            KEY_READ | KEY_WRITE,
                            NULL,
                            &Key,
                            &Disposition);
    if (Error != ERROR_SUCCESS)
        return HRESULT_FROM_WIN32(Error);
    if (Disposition != REG_CREATED_NEW_KEY)
    {
        RegCloseKey(Key);
        return HRESULT_FROM_WIN32(ERROR_ALREADY_EXISTS);
    }

    Error = RegSetValueExW(Key,
                           L"Moniker",
                           0,
                           REG_SZ,
                           (const BYTE *)Moniker,
                           (DWORD)((lstrlenW(Moniker) + 1) * sizeof(WCHAR)));
    if (Error == ERROR_SUCCESS)
    {
        Error = RegSetValueExW(Key,
                               L"DisplayName",
                               0,
                               REG_SZ,
                               (const BYTE *)DisplayName,
                               (DWORD)((lstrlenW(DisplayName) + 1) * sizeof(WCHAR)));
    }
    RegCloseKey(Key);
    if (Error != ERROR_SUCCESS)
    {
        RegDeleteKeyExW(HKEY_CURRENT_USER, Path, 0, 0);
        return HRESULT_FROM_WIN32(Error);
    }
    return S_OK;
}

HRESULT
WINAPI
AppContainerUnregisterSid(
    _In_ PSID AppContainerSid)
{
    WCHAR Path[512];
    LONG Error;
    HRESULT Result;

    Result = BasepFormatAppContainerMappingPath(AppContainerSid,
                                                Path,
                                                ARRAYSIZE(Path));
    if (FAILED(Result))
        return Result;

    Error = RegDeleteKeyExW(HKEY_CURRENT_USER, Path, 0, 0);
    return Error == ERROR_SUCCESS ? S_OK : HRESULT_FROM_WIN32(Error);
}

HRESULT
WINAPI
AppContainerLookupMoniker(
    _In_ PSID AppContainerSid,
    _Outptr_ LPWSTR *Moniker)
{
    WCHAR Path[512];
    DWORD Type, Size = 0;
    SIZE_T AllocationSize;
    LPWSTR Buffer;
    HKEY Key;
    LONG Error;
    HRESULT Result;

    if (!Moniker)
        return E_INVALIDARG;

    Result = BasepFormatAppContainerMappingPath(AppContainerSid,
                                                Path,
                                                ARRAYSIZE(Path));
    if (FAILED(Result))
        return Result;

    Error = RegOpenKeyExW(HKEY_CURRENT_USER, Path, 0, KEY_QUERY_VALUE, &Key);
    if (Error != ERROR_SUCCESS)
        return HRESULT_FROM_WIN32(Error);

    Error = RegQueryValueExW(Key, L"Moniker", NULL, &Type, NULL, &Size);
    if (Error != ERROR_SUCCESS || Type != REG_SZ || Size < sizeof(WCHAR) || Size % sizeof(WCHAR))
    {
        RegCloseKey(Key);
        return Error == ERROR_SUCCESS ? E_INVALIDARG : HRESULT_FROM_WIN32(Error);
    }

    AllocationSize = (SIZE_T)Size + sizeof(WCHAR);
    if (AllocationSize < Size)
    {
        RegCloseKey(Key);
        return E_OUTOFMEMORY;
    }

    Buffer = LocalAlloc(LMEM_FIXED, AllocationSize);
    if (!Buffer)
    {
        RegCloseKey(Key);
        return E_OUTOFMEMORY;
    }

    Error = RegQueryValueExW(Key,
                             L"Moniker",
                             NULL,
                             &Type,
                             (LPBYTE)Buffer,
                             &Size);
    RegCloseKey(Key);
    if (Error != ERROR_SUCCESS || Type != REG_SZ || Size < sizeof(WCHAR) || Size % sizeof(WCHAR))
    {
        LocalFree(Buffer);
        return Error == ERROR_SUCCESS ? E_INVALIDARG : HRESULT_FROM_WIN32(Error);
    }

    Buffer[Size / sizeof(WCHAR)] = UNICODE_NULL;
    *Moniker = Buffer;
    return S_OK;
}

VOID
WINAPI
AppContainerFreeMemory(
    _In_opt_ PVOID Memory)
{
    LocalFree(Memory);
}

#define BASEP_LOWBOX_HANDLE_COUNT 5

static
NTSTATUS
BasepCreateAppContainerLink(
    _In_ HANDLE Directory,
    _In_ PCWSTR LinkName,
    _In_ PCWSTR Target,
    _In_ PSECURITY_DESCRIPTOR Sd,
    _Out_ PHANDLE Handle)
{
    UNICODE_STRING Name, TargetName;
    OBJECT_ATTRIBUTES ObjectAttributes;
    NTSTATUS Status;

    RtlInitUnicodeString(&Name, LinkName);
    RtlInitUnicodeString(&TargetName, Target);
    InitializeObjectAttributes(&ObjectAttributes, &Name, OBJ_CASE_INSENSITIVE | OBJ_OPENIF, Directory, Sd);
    Status = NtCreateSymbolicLinkObject(Handle, SYMBOLIC_LINK_ALL_ACCESS, &ObjectAttributes, &TargetName);
    if (Status == STATUS_OBJECT_NAME_COLLISION || Status == STATUS_OBJECT_TYPE_MISMATCH)
    {
        InitializeObjectAttributes(&ObjectAttributes, &Name, OBJ_CASE_INSENSITIVE, Directory, NULL);
        Status = NtOpenSymbolicLinkObject(Handle, SYMBOLIC_LINK_QUERY, &ObjectAttributes);
    }
    return Status;
}

static
NTSTATUS
BasepCreateAppContainerDirectory(
    _In_ PSID PackageSid,
    _Out_writes_(BASEP_LOWBOX_HANDLE_COUNT) PHANDLE Handles,
    _Out_ PULONG HandleCount)
{
    SID_IDENTIFIER_AUTHORITY WorldAuthority = {SECURITY_WORLD_SID_AUTHORITY};
    SID_IDENTIFIER_AUTHORITY LabelAuthority = {SECURITY_MANDATORY_LABEL_AUTHORITY};
    UCHAR WorldBuffer[SECURITY_MAX_SID_SIZE], LowBuffer[SECURITY_MAX_SID_SIZE];
    PSID WorldSid = (PSID)WorldBuffer, LowSid = (PSID)LowBuffer;
    SECURITY_DESCRIPTOR Sd;
    PACL Dacl, Sacl;
    ULONG DaclSize, SaclSize;
    UNICODE_STRING SidString, Name, SessionString;
    WCHAR NameBuffer[256], SessionBuffer[16];
    OBJECT_ATTRIBUTES ObjectAttributes;
    UNICODE_STRING RpcName;
    HANDLE Directory = NULL;
    NTSTATUS Status;

    *HandleCount = 0;
    SessionString.Buffer = SessionBuffer; SessionString.Length = 0; SessionString.MaximumLength = sizeof(SessionBuffer);
    RtlInitializeSid(WorldSid, &WorldAuthority, 1);
    *RtlSubAuthoritySid(WorldSid, 0) = SECURITY_WORLD_RID;
    RtlInitializeSid(LowSid, &LabelAuthority, 1);
    *RtlSubAuthoritySid(LowSid, 0) = SECURITY_MANDATORY_LOW_RID;

    Status = RtlConvertSidToUnicodeString(&SidString, PackageSid, TRUE);
    if (!NT_SUCCESS(Status)) return Status;
    Name.Buffer = NameBuffer; Name.Length = 0; Name.MaximumLength = sizeof(NameBuffer) - sizeof(UNICODE_NULL);
    Status = RtlAppendUnicodeToString(&Name, L"\\Sessions\\");
    if (NT_SUCCESS(Status)) Status = RtlIntegerToUnicodeString(NtCurrentTeb()->Peb->SessionId, 10, &SessionString);
    if (NT_SUCCESS(Status)) Status = RtlAppendUnicodeStringToString(&Name, &SessionString);
    if (NT_SUCCESS(Status)) Status = RtlAppendUnicodeToString(&Name, L"\\AppContainerNamedObjects\\");
    if (NT_SUCCESS(Status)) Status = RtlAppendUnicodeStringToString(&Name, &SidString);
    RtlFreeUnicodeString(&SidString);
    if (!NT_SUCCESS(Status)) return Status;
    NameBuffer[Name.Length / sizeof(WCHAR)] = UNICODE_NULL;

    DaclSize = sizeof(ACL) + 2 * FIELD_OFFSET(ACCESS_ALLOWED_ACE, SidStart) + RtlLengthSid(PackageSid) + RtlLengthSid(WorldSid);
    SaclSize = sizeof(ACL) + FIELD_OFFSET(SYSTEM_MANDATORY_LABEL_ACE, SidStart) + RtlLengthSid(LowSid);
    Dacl = HeapAlloc(GetProcessHeap(), 0, DaclSize + SaclSize);
    if (!Dacl) return STATUS_NO_MEMORY;
    Sacl = (PACL)((PUCHAR)Dacl + DaclSize);

    Status = RtlCreateAcl(Dacl, DaclSize, ACL_REVISION);
    if (NT_SUCCESS(Status)) Status = RtlAddAccessAllowedAce(Dacl, ACL_REVISION, DIRECTORY_ALL_ACCESS, PackageSid);
    if (NT_SUCCESS(Status)) Status = RtlAddAccessAllowedAce(Dacl, ACL_REVISION, DIRECTORY_ALL_ACCESS, WorldSid);
    if (NT_SUCCESS(Status)) Status = RtlCreateAcl(Sacl, SaclSize, ACL_REVISION);
    if (NT_SUCCESS(Status)) Status = RtlAddMandatoryAce(Sacl, ACL_REVISION, 0, SYSTEM_MANDATORY_LABEL_NO_WRITE_UP, SYSTEM_MANDATORY_LABEL_ACE_TYPE, LowSid);
    if (NT_SUCCESS(Status)) Status = RtlCreateSecurityDescriptor(&Sd, SECURITY_DESCRIPTOR_REVISION);
    if (NT_SUCCESS(Status)) Status = RtlSetDaclSecurityDescriptor(&Sd, TRUE, Dacl, FALSE);
    if (NT_SUCCESS(Status)) Status = RtlSetSaclSecurityDescriptor(&Sd, TRUE, Sacl, FALSE);
    if (NT_SUCCESS(Status))
    {
        InitializeObjectAttributes(&ObjectAttributes, &Name, OBJ_CASE_INSENSITIVE | OBJ_OPENIF, NULL, &Sd);
        Status = NtCreateDirectoryObject(&Directory, DIRECTORY_ALL_ACCESS, &ObjectAttributes);
    }
    if (NT_SUCCESS(Status))
    {
        Handles[(*HandleCount)++] = Directory;
        Status = BasepCreateAppContainerLink(Directory, L"Global", L"\\BaseNamedObjects", &Sd, &Handles[*HandleCount]);
        if (NT_SUCCESS(Status)) (*HandleCount)++;
    }
    if (NT_SUCCESS(Status))
    {
        Status = BasepCreateAppContainerLink(Directory, L"Local", NameBuffer, &Sd, &Handles[*HandleCount]);
        if (NT_SUCCESS(Status)) (*HandleCount)++;
    }
    if (NT_SUCCESS(Status))
    {
        Status = BasepCreateAppContainerLink(Directory, L"Session", L"\\Sessions\\BNOLINKS", &Sd, &Handles[*HandleCount]);
        if (NT_SUCCESS(Status)) (*HandleCount)++;
    }
    if (NT_SUCCESS(Status))
    {
        RtlInitUnicodeString(&RpcName, L"RPC Control");
        InitializeObjectAttributes(&ObjectAttributes, &RpcName, OBJ_CASE_INSENSITIVE | OBJ_OPENIF, Directory, &Sd);
        Status = NtCreateDirectoryObject(&Handles[*HandleCount], DIRECTORY_ALL_ACCESS, &ObjectAttributes);
        if (NT_SUCCESS(Status)) (*HandleCount)++;
    }
    HeapFree(GetProcessHeap(), 0, Dacl);
    if (!NT_SUCCESS(Status))
    {
        while (*HandleCount) NtClose(Handles[--(*HandleCount)]);
    }
    return Status;
}

static
BOOL
BasepIsAppContainerSid(
    _In_opt_ PSID Sid)
{
    static const SID_IDENTIFIER_AUTHORITY PackageAuthority = {SECURITY_APP_PACKAGE_AUTHORITY};

    return Sid != NULL && IsValidSid(Sid) &&
           !memcmp(GetSidIdentifierAuthority(Sid), &PackageAuthority, sizeof(PackageAuthority)) &&
           *GetSidSubAuthorityCount(Sid) != 0 &&
           *GetSidSubAuthority(Sid, 0) == SECURITY_APP_PACKAGE_BASE_RID;
}

BOOL
WINAPI
CreateAppContainerToken(
    _In_opt_ HANDLE TokenHandle,
    _In_ PSECURITY_CAPABILITIES SecurityCapabilities,
    _Out_ PHANDLE OutToken)
{
    HANDLE Handles[BASEP_LOWBOX_HANDLE_COUNT];
    ULONG HandleCount = 0;
    HANDLE BaseToken = TokenHandle;
    HANDLE Token = NULL;
    NTSTATUS Status;

    if (!SecurityCapabilities || !OutToken)
    {
        SetLastError(RtlNtStatusToDosError(STATUS_INVALID_PARAMETER));
        return FALSE;
    }
    if (!BasepIsAppContainerSid(SecurityCapabilities->AppContainerSid))
    {
        SetLastError(RtlNtStatusToDosError(STATUS_NOT_APPCONTAINER));
        return FALSE;
    }

    if (!BaseToken)
    {
        Status = NtOpenProcessToken(NtCurrentProcess(), TOKEN_DUPLICATE | TOKEN_QUERY, &BaseToken);
        if (!NT_SUCCESS(Status))
        {
            SetLastError(RtlNtStatusToDosError(Status));
            return FALSE;
        }
    }

    /* TODO: report a named object directory failure instead of creating the token without its directory handles */
    BasepCreateAppContainerDirectory(SecurityCapabilities->AppContainerSid, Handles, &HandleCount);
    KbAssert(HandleCount <= BASEP_LOWBOX_HANDLE_COUNT);
    Status = NtCreateLowBoxToken(&Token,
                                 BaseToken,
                                 TOKEN_ALL_ACCESS,
                                 NULL,
                                 SecurityCapabilities->AppContainerSid,
                                 SecurityCapabilities->CapabilityCount,
                                 SecurityCapabilities->Capabilities,
                                 HandleCount,
                                 HandleCount ? Handles : NULL);
    while (HandleCount) NtClose(Handles[--HandleCount]);
    if (BaseToken != TokenHandle) NtClose(BaseToken);
    if (!NT_SUCCESS(Status))
    {
        SetLastError(RtlNtStatusToDosError(Status));
        return FALSE;
    }

    KbAssert(Token != NULL);
    *OutToken = Token;
    return TRUE;
}
